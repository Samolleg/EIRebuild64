// WDDM local video memory budget enforcement.
//
// On a discrete adapter, use above the LOCAL budget makes the kernel page
// resources over PCIe in the middle of frames: multi-second stalls and, on
// narrow links, driver timeouts (TDR). The renderer therefore keeps the
// process under that budget by shedding its own resources, least visible
// first, and never silently: every action is one bridge event and the
// F-overlay shows the level in effect.
//
// The steps, while use stays above 95% of the budget, aiming below 90%:
//   - evict owned textures and shadow texture-array pages that no material
//     bucket references and that went unused for kIdleFrames (rebuilt from
//     their source on the next use; nothing visible changes);
//   - level 1: reducible world textures rebuild without their top mip;
//   - level 2: sun and lamp shadow maps at half size as well;
//   - level 3: world textures without their top two mips;
//   - level 4 (#289: the 8 GB laptop hung at level 3 after cannot_fit):
//     every texture and array page nothing references and nothing drew for
//     kTopIdleFrames, least recently used first, and the terrain face cache
//     released (faces drawn per pixel, the same image).
// From level 3 on the eviction step itself takes any unreferenced texture
// unused for kTopIdleFrames, least recently used first.
// A level is undone once use plus that level's measured savings (at least
// 5% of the budget) fits under 90% for three polls. With the budget far
// above use nothing happens: level stays 0 and no texture is touched.
//
// UMA adapters (no non-local segment) are not enforced: their "local" memory
// is system memory, so going over the budget pages within the same RAM
// rather than over a bus, and their budget is a share of RAM rather than a
// hardware size. EID3D11_VRAM_BUDGET_MB=<MiB> replaces the budget (and
// enforces it on any adapter) so a large card can rehearse a small one.
#include "renderer.h"
#include "graphics_settings.h"
#include "terrain2_cache.h"
#include "engine_bridge.h"
#include "neighbour_maps.h"
#include "owned_resource_runtime.h"
#include "renderer_internal.h"
#include "retained_static_submission.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unordered_set>
#include <vector>
#include <string>
#include <psapi.h>
#include "error_report.h"
#include "stall_watchdog.h"
#include <array>

namespace owned {
namespace {
constexpr std::uint64_t kPollFrames = 30;       // presents between budget queries
constexpr std::uint64_t kSettleFrames = 90;     // a new level's rebuilds land before the next step
constexpr std::uint64_t kIdleFrames = 120;      // unused this long and unreferenced: evictable
constexpr std::uint64_t kTopIdleFrames = 30;    // from level 3: unreferenced and not drawn for half a second (#289 sizing audit: 2 frames evicted and re-uploaded textures used every few frames)
constexpr std::uint64_t kRebuildBytesPerFrame = 16ull << 20;  // per-frame rebuild work, not a capacity limit
constexpr unsigned kTopLevel = 4;
double mib(std::uint64_t bytes) noexcept { return double(bytes) / 1048576.0; }
}

bool Renderer::vram_take_rebuild(std::uint64_t bytes) noexcept {
 auto& v=vram_budget_;
 if(v.rebuild_frame!=native_frame_index_){v.rebuild_frame=native_frame_index_;v.rebuild_bytes=0;}
 if(v.rebuild_bytes>=kRebuildBytesPerFrame)return false;
 v.rebuild_bytes+=bytes;return true;
}

// Owned textures and immutable shadow texture-array pages that no material
// bucket references and nothing used for kIdleFrames, oldest first, until
// `bytes` are freed. Returns the bytes freed.
std::uint64_t Renderer::evict_idle_textures_for_budget(std::uint64_t bytes,std::uint64_t idle_frames) noexcept try{
 std::unordered_set<ID3D11ShaderResourceView*> queued;
 for(const auto& bucket:native_state_buckets_)if(bucket.occupied&&bucket.texture_srv)queued.insert(bucket.texture_srv.Get());
 std::vector<std::shared_ptr<ID3D11Texture2D>> retained;
 retained_visual_scene().visual_detached_surface_textures(retained);
 std::unordered_set<ID3D11Texture2D*> held;for(const auto& texture:retained)held.insert(texture.get());
 const auto idle=[&](std::uint64_t frame){return native_frame_index_>frame+idle_frames;};
 struct Candidate{std::uint64_t frame;const std::string* key;std::vector<NativeTextureArrayPage>* pages;std::size_t page;};
 std::vector<Candidate> candidates;
 // D3D12: buckets, records, queued commands and surfaces hold their texture
 // by reference (D3D12TextureRef), so an entry only the cache still holds is
 // referenced by nothing, and dropping it frees it (after the frames that
 // read it, the executor's deferred release).
 const auto d3d12_bytes=[&](const NativeOwnedTexture& t)->std::uint64_t{
  UINT width=0,height=0,levels=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
  return d3d12_&&t.d3d12&&d3d12_->texture_info(*t.d3d12,width,height,levels,format)?texture_storage_bytes(width,height,levels,format):0;
 };
 for(const auto& item:native_owned_textures_){const auto& t=item.second;
  if(t.camp_surface_alias||t.surface_shared||!idle(t.last_used_frame))continue;
  if(t.texture?!queued.count(t.srv.Get())&&!held.count(t.texture.Get()):t.d3d12&&t.d3d12.use_count()==1)
   candidates.push_back({t.last_used_frame,&item.first,nullptr,0});}
 for(auto& group:native_texture_arrays_)if(group.first.immutable_values)
  for(std::size_t i=0;i<group.second.size();++i){const auto& page=group.second[i];
   if(idle(page.last_used_frame)&&!queued.count(page.srv.Get()))candidates.push_back({page.last_used_frame,nullptr,&group.second,i});}
 // Oldest first (array pages are erased afterwards, back to front per vector).
 std::sort(candidates.begin(),candidates.end(),[](const Candidate& a,const Candidate& b){return a.frame<b.frame;});
 std::uint64_t freed=0;std::vector<std::pair<std::vector<NativeTextureArrayPage>*,std::size_t>> pages;
 std::vector<std::string> keys;
 for(const auto& c:candidates){
  if(freed>=bytes)break;
  if(c.key){const auto& t=native_owned_textures_.at(*c.key);freed+=t.texture?resource_video_bytes(t.texture.Get()):d3d12_bytes(t);keys.push_back(*c.key);}
  else{freed+=resource_video_bytes((*c.pages)[c.page].texture.Get());pages.push_back({c.pages,c.page});}
 }
 // Pages go first: the (vector, index) pairs name the arrays as they were
 // collected, and dropping a texture's slots below erases pages that lose
 // their last live slot and whole groups that lose their last page.
 std::sort(pages.begin(),pages.end(),[](const auto& a,const auto& b){return a.first!=b.first?a.first<b.first:a.second>b.second;});
 for(const auto& p:pages)p.first->erase(p.first->begin()+static_cast<std::ptrdiff_t>(p.second));
 for(const auto& key:keys){
  const auto found=native_owned_textures_.find(key);
  drop_native_texture_array_slots(found->second.backing,true);
  neighbour_texture_resident(key,false);
  // The game thread looks entries up (retain_detached_surface_texture) while
  // the scene thread owns the cache: structural changes take the lock.
  {std::lock_guard<std::mutex> guard(native_owned_textures_lock_);native_owned_textures_.erase(found);}
 }
 return freed;
}catch(...){return 0;}

// Testers' evidence for eviction (a GTX 1080 kept passes 7-100x slower on
// unchanged work after a 5.7 s Present stall while its video memory use fell
// ~840 MiB with no release of ours): the residency of our big, frequently
// read resources, the largest few of each set. D3D11 only: D3D12 resources
// are not DXGI resources and D3D12 has no residency query (the line keeps
// the adapter's use and budget). Called at the memory_status cadence; the
// query reads the kernel's residency state and waits for no GPU work.
bool Renderer::memory_residency_summary(char* out,std::size_t size) noexcept try{
 if(d3d12_){std::snprintf(out,size,"residency n/a (D3D12 has no per-resource residency query)");return true;}
 ComPtr<IDXGIDevice> dxgi;
 if(!device_||FAILED(device_.As(&dxgi))){std::snprintf(out,size,"residency unknown (no DXGI device)");return true;}
 // arrays: batched texture-array pages and the terrain 2.0 atlas pools;
 // textures: owned world/figure textures (what most buckets sample).
 static constexpr const char* kSets[]={"arrays","textures","geometry","materials","smaa","ground"};
 static constexpr std::size_t kCaps[]={16,16,8,12,2,6};  // at most 60 resources per query
 struct Item{ComPtr<ID3D11Resource> resource;std::uint64_t bytes;};
 std::array<std::vector<Item>,6> sets;
 const auto add=[&](unsigned set,ID3D11Resource* r){if(r)sets[set].push_back({r,resource_video_bytes(r)});};
 const auto add_view=[&](unsigned set,ID3D11View* v){if(!v)return;ComPtr<ID3D11Resource> r;v->GetResource(&r);add(set,r.Get());};
 for(const auto& group:native_texture_arrays_)for(const auto& page:group.second)add(0,page.texture.Get());
 for(const auto& pool:terrain2_pools_){add(0,pool.atlas.Get());add(0,pool.heights.Get());}
 for(const auto& item:native_owned_textures_)add(1,item.second.texture.Get());
 for(const auto& page:native_geometry_pages_)add(2,page.buffer.Get());
 for(const auto& profile:material_profiles_)add_view(3,profile.second.srv.Get());
 add_view(4,scene_composite_.smaa_area_view());add_view(4,scene_composite_.smaa_search_view());
 add(5,gc_color_.Get());
 for(auto* view:{gc_fields_srv_.Get(),gc_tiles_srv_.Get(),gc_bark_srv_.Get(),gc_stone_srv_.Get(),gc_trees_srv_.Get()})add_view(5,view);
 std::vector<ComPtr<IDXGIResource>> resources;std::vector<unsigned> owner;std::vector<std::uint64_t> bytes;
 for(unsigned s=0;s<sets.size();++s){
  auto& items=sets[s];const std::size_t keep=(std::min)(items.size(),kCaps[s]);
  std::partial_sort(items.begin(),items.begin()+static_cast<std::ptrdiff_t>(keep),items.end(),[](const Item& a,const Item& b){return a.bytes>b.bytes;});
  for(std::size_t i=0;i<keep;++i){ComPtr<IDXGIResource> r;
   if(SUCCEEDED(items[i].resource.As(&r))){resources.push_back(r);owner.push_back(s);bytes.push_back(items[i].bytes);}}
 }
 if(resources.empty()){std::snprintf(out,size,"residency: none of our resources exist yet");return true;}
 std::vector<IUnknown*> list;for(const auto& r:resources)list.push_back(r.Get());
 std::vector<DXGI_RESIDENCY> state(list.size(),DXGI_RESIDENCY(0));
 const HRESULT hr=dxgi->QueryResourceResidency(list.data(),state.data(),static_cast<UINT>(list.size()));
 if(FAILED(hr)){std::snprintf(out,size,"residency query failed %08lx",static_cast<unsigned long>(hr));return true;}
 unsigned resident=0,shared=0,disk=0;std::uint64_t total=0,off=0;
 std::array<unsigned,6> checked{},off_count{};std::array<std::uint64_t,6> off_bytes{};
 for(std::size_t i=0;i<state.size();++i){
  total+=bytes[i];++checked[owner[i]];
  if(state[i]==DXGI_RESIDENCY_FULLY_RESIDENT){++resident;continue;}
  if(state[i]==DXGI_RESIDENCY_RESIDENT_IN_SHARED_MEMORY)++shared;else ++disk;
  off+=bytes[i];++off_count[owner[i]];off_bytes[owner[i]]+=bytes[i];
 }
 int n=std::snprintf(out,size,"residency %zu checked, %.0f MiB (arrays %u textures %u geometry %u materials %u smaa %u ground %u): %u resident, %u in shared memory, %u evicted, %.1f MiB not resident",
  state.size(),mib(total),checked[0],checked[1],checked[2],checked[3],checked[4],checked[5],resident,shared,disk,mib(off));
 for(unsigned s=0;s<sets.size()&&n>0&&std::size_t(n)<size;++s)if(off_count[s])
  n+=std::snprintf(out+n,size-std::size_t(n)," [%s %u, %.1f MiB]",kSets[s],off_count[s],mib(off_bytes[s]));
 return off==0&&!shared&&!disk;
}catch(...){std::snprintf(out,size,"residency unknown (out of memory)");return true;}

// Testers' evidence for background paging (a small card whose budget other
// programs take while the game is in the background): every 30 s and at
// each focus loss or return, one line with the process's video memory use
// against its WDDM budget (local and shared), private bytes, CPU and frame
// rate. A few system calls every 30 s; per frame only a tick read.
void Renderer::log_memory_status() noexcept try{
 auto& s=memory_status_;
 const ULONGLONG now=GetTickCount64();
 const bool front=window_in_front();
 const bool focus_change=s.known&&front!=s.in_front;
 if(s.known&&!focus_change&&now-s.at_ms<30000)return;
 FILETIME created{},exited{},kernel{},user{};
 GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user);
 const std::uint64_t cpu=((std::uint64_t(kernel.dwHighDateTime)<<32)|kernel.dwLowDateTime)+((std::uint64_t(user.dwHighDateTime)<<32)|user.dwLowDateTime);
 SYSTEM_INFO system{};GetSystemInfo(&system);
 const double seconds=s.known?double(now-s.at_ms)/1000.0:0.0;
 const double cpu_percent=seconds>0?100.0*double(cpu-s.cpu_100ns)/1e7/seconds/double((std::max)(DWORD(1),system.dwNumberOfProcessors)):0.0;
 const double fps=seconds>0?double(native_frame_index_-s.frame)/seconds:0.0;
 PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);
 GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory));
 DXGI_QUERY_VIDEO_MEMORY_INFO local{},shared{};
 if(adapter3_){adapter3_->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&local);adapter3_->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL,&shared);}
 namespace sw=stall_watch;
 const int slot=(sw::residency_index.load(std::memory_order_relaxed)+1)&1;
 char* residency=sw::residency[slot];
 const bool all_resident=memory_residency_summary(residency,sizeof(sw::residency[slot]));
 sw::residency_index.store(slot,std::memory_order_release);
 char line[640];
 std::snprintf(line,sizeof(line),"%s: video memory %.0f/%.0f MiB, shared %.0f/%.0f MiB, private %.0f MiB, CPU %.0f%%, %.1f frames/s, %s, budget level %u, %s",
  focus_change?(front?"focus back":"focus lost"):"every 30 s",mib(local.CurrentUsage),mib(local.Budget),mib(shared.CurrentUsage),mib(shared.Budget),
  mib(memory.PrivateUsage),cpu_percent,fps,front?"in front":"in the background",vram_budget_.level,residency);
 bridge_report_event("memory_status",line);
 set_error_report_status(line);
 // The D3D12 owner breakdown (#289, developer mode) is no longer on this
 // 30 s line: it walks every texture and mesh and writes dozens of log
 // lines (a 60-113 ms frame tail every 30 s on the 3070 laptop, #344); it
 // runs once per load reveal and on Shift+F11 (report_vram_owners).
 // One event when the checked set stops (or again starts) being all resident.
 if(s.residency_known?all_resident!=s.all_resident:!all_resident){
  SYSTEMTIME t{};GetLocalTime(&t);TIME_ZONE_INFORMATION zone{};GetTimeZoneInformation(&zone);
  char detail[480];
  std::snprintf(detail,sizeof(detail),"%s at %04u-%02u-%02u %02u:%02u:%02u.%03u UTC%+ld min, frame %llu: %s",
   all_resident?"all resident again":"not all resident",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds,
   -static_cast<long>(zone.Bias),static_cast<unsigned long long>(native_frame_index_),residency);
  bridge_report_event("vram_residency",detail);
 }
 s.residency_known=true;s.all_resident=all_resident;
 s.known=true;s.in_front=front;s.at_ms=now;s.frame=native_frame_index_;s.cpu_100ns=cpu;
}catch(...){}

// The D3D12 owner breakdown (#289): live memory by kind and creation site,
// the largest owned textures, geometry by model and held twice, world and
// posed vertices. Developer mode, once per load reveal and on Shift+F11:
// never periodic (#344).
void Renderer::report_vram_owners() noexcept try{
 if(!d3d12_)return;
 DXGI_QUERY_VIDEO_MEMORY_INFO local{};
 if(adapter3_)adapter3_->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&local);
 {
  for(const auto& owners:d3d12_->memory_report(local.CurrentUsage))bridge_report_event("vram_owners",owners.c_str());
  struct Named{std::uint64_t bytes;const std::string* key;UINT width,height;DXGI_FORMAT format;};
  std::vector<Named> named;std::uint64_t total=0;std::array<std::uint64_t,3> by_side{};std::array<unsigned,3> count_side{};
  for(const auto& item:native_owned_textures_){const auto& t=item.second;
   UINT width=0,height=0,levels=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
   if(!t.d3d12||!d3d12_->texture_info(*t.d3d12,width,height,levels,format))continue;
   const std::uint64_t bytes=texture_storage_bytes(width,height,levels,format);
   total+=bytes;const unsigned side=(std::max)(width,height)>=2048?0u:(std::max)(width,height)>=1024?1u:2u;
   by_side[side]+=bytes;++count_side[side];
   named.push_back({bytes,&item.first,width,height,format});
  }
  const std::size_t keep=(std::min)(named.size(),std::size_t(24));
  std::partial_sort(named.begin(),named.begin()+static_cast<std::ptrdiff_t>(keep),named.end(),[](const Named& a,const Named& b){return a.bytes>b.bytes;});
  char text[960];
  int n=std::snprintf(text,sizeof(text),"owned textures %zu, %.0f MiB: 2048+ %u %.0f MiB, 1024+ %u %.0f MiB, smaller %u %.0f MiB; largest:",
   named.size(),mib(total),count_side[0],mib(by_side[0]),count_side[1],mib(by_side[1]),count_side[2],mib(by_side[2]));
  for(std::size_t i=0;i<keep;++i){
   char entry[160];
   std::snprintf(entry,sizeof(entry)," [%.60s %ux%u f%d %.1f MiB]",named[i].key->c_str(),named[i].width,named[i].height,int(named[i].format),mib(named[i].bytes));
   if(std::size_t(n)+std::strlen(entry)>=sizeof(text)){bridge_report_event("vram_textures",text);n=std::snprintf(text,sizeof(text),"largest (cont.):");}
   n+=std::snprintf(text+n,sizeof(text)-std::size_t(n),"%s",entry);
  }
  bridge_report_event("vram_textures",text);
  // Geometry pages by model: bytes, cached variants (shape-resolved copies)
  // and triangles of each resource key, largest 24.
  struct Model{std::uint64_t bytes=0,triangles=0;unsigned variants=0;};
  std::unordered_map<std::string,Model> models;std::uint64_t mesh_bytes=0;
  for(const auto& item:native_meshes_){auto& m=models[item.first.resource_key];
   m.bytes+=item.second.allocation.bytes;m.triangles+=item.first.index_count/3;++m.variants;mesh_bytes+=item.second.allocation.bytes;}
  std::uint64_t page_bytes=0;for(const auto& page:native_geometry_pages_)page_bytes+=page.capacity;
  std::vector<std::pair<const std::string*,Model>> rows;for(const auto& m:models)rows.push_back({&m.first,m.second});
  const std::size_t top=(std::min)(rows.size(),std::size_t(24));
  std::partial_sort(rows.begin(),rows.begin()+static_cast<std::ptrdiff_t>(top),rows.end(),[](const auto& a,const auto& b){return a.second.bytes>b.second.bytes;});
  n=std::snprintf(text,sizeof(text),"meshes %zu in %zu models, %.0f MiB in pages of %.0f MiB (%zu pages); largest:",
   native_meshes_.size(),models.size(),mib(mesh_bytes),mib(page_bytes),native_geometry_pages_.size());
  for(std::size_t i=0;i<top;++i){
   char entry[160];
   std::snprintf(entry,sizeof(entry)," [%.60s %.1f MiB %u var %llu tri]",rows[i].first->c_str(),mib(rows[i].second.bytes),rows[i].second.variants,
    static_cast<unsigned long long>(rows[i].second.triangles));
   if(std::size_t(n)+std::strlen(entry)>=sizeof(text)){bridge_report_event("vram_models",text);n=std::snprintf(text,sizeof(text),"largest (cont.):");}
   n+=std::snprintf(text+n,sizeof(text)-std::size_t(n),"%s",entry);
  }
  bridge_report_event("vram_models",text);
  // The same geometry held more than once (#289, reuse between neighbours
  // and the main map): meshes whose key matches but for the cache selector,
  // by where the copies live (general pages: the main map; dedicated pages:
  // one neighbour each). Copies beyond the largest count as duplicated.
  {
   struct Copies{std::uint64_t general=0,dedicated=0,largest=0;unsigned count=0;bool mixed=false;};
   std::unordered_map<std::string,Copies> copies;
   for(const auto& item:native_meshes_){
    const auto& k=item.first;const auto& m=item.second;
    std::string key=k.resource_key;
    char tail[96];std::snprintf(tail,sizeof(tail),"|%u|%u|%08x%08x%08x|%u|%u",k.world_part,k.shape_count,k.shape_bits[0],k.shape_bits[1],k.shape_bits[2],k.index_count,k.decoded_channels);
    key+=tail;
    auto& c=copies[key];
    const bool dedicated=m.allocation.page<native_geometry_pages_.size()&&native_geometry_pages_[m.allocation.page].dedicated;
    (dedicated?c.dedicated:c.general)+=m.allocation.bytes;c.largest=(std::max)(c.largest,std::uint64_t(m.allocation.bytes));++c.count;
   }
   std::uint64_t main_neighbour=0,neighbour_neighbour=0,general_general=0;unsigned keys=0;
   for(const auto& [key,c]:copies){
    (void)key;if(c.count<2)continue;++keys;
    const std::uint64_t extra=c.general+c.dedicated-c.largest;
    if(c.general&&c.dedicated)main_neighbour+=extra;else if(c.dedicated)neighbour_neighbour+=extra;else general_general+=extra;
   }
   std::uint64_t shape_extra=0;
   for(const auto& [name,m]:models)if(m.variants>1)shape_extra+=m.bytes-m.bytes/m.variants;
   // The neighbours' prebuilt decorations (their own pages, not in this
   // cache), by model part and index count against the main map's meshes.
   std::unordered_set<std::string> main_parts;
   for(const auto& item:native_meshes_){
    // "incarnation:offset:bytes:model/part": the part after the third colon.
    const auto& key=item.first.resource_key;std::size_t at=std::string::npos;
    for(int field=0;field<3;++field){at=key.find(':',at==std::string::npos?0:at+1);if(at==std::string::npos)break;}
    if(at==std::string::npos)continue;
    main_parts.insert(key.substr(at+1)+"|"+std::to_string(item.first.index_count));
   }
   std::uint64_t prebuilt=0,across=0,with_main=0;
   neighbour_prebuilt_duplicates([&](const std::string& part){return main_parts.count(part)!=0;},prebuilt,across,with_main);
   std::snprintf(text,sizeof(text),"duplicated geometry: %u keys held more than once, %.1f MiB beyond one copy: main map and a neighbour %.1f, neighbours among themselves %.1f, "
    "main map twice %.1f; shape-resolved variants beyond one per model about %.1f MiB; neighbour decorations prebuilt %.1f MiB, of models the main map also holds %.1f, "
    "beyond one copy among neighbours %.1f",
    keys,mib(main_neighbour+neighbour_neighbour+general_general),mib(main_neighbour),mib(neighbour_neighbour),mib(general_general),mib(shape_extra),
    mib(prebuilt),mib(with_main),mib(across));
   bridge_report_event("vram_models",text);
  }
  // World vertices (#289): capacity against the high-water mark and the live
  // slots by kind (terrain, posed figures, other world records), plus ranges
  // held for reuse; prepared vertices' capacity.
  {
   std::uint64_t terrain=0,posed=0,other=0,held=0,slots=0,terrain_liquid=0;
   for(const auto& slot:native_prepared_mesh_slots_){
    if(!slot.active||slot.world_base==UINT_MAX)continue;++slots;
    if(slot.decoded_channels&native_decoded_terrain){terrain+=slot.world_count;if(slot.mesh&&slot.mesh->has_liquid)terrain_liquid+=slot.world_count;}
    else if(slot.world_key.pose_slot!=UINT_MAX)posed+=slot.world_count;else other+=slot.world_count;
   }
   for(const auto& range:native_world_ranges_)if(!range.owned)held+=range.count;
   const double vertex=double(sizeof(NativeWorldVertexGpu));
   std::snprintf(text,sizeof(text),"world vertices: capacity %u (%.0f MiB, %u B each), high water %u (%.0f MiB); live in %llu slots: terrain %llu (%.0f MiB), posed %llu (%.0f MiB), other %llu (%.0f MiB); free ranges %llu (%.0f MiB); prepared vertices capacity %u (%.0f MiB)",
    native_world_vertex_capacity_,mib(std::uint64_t(native_world_vertex_capacity_*vertex)),unsigned(vertex),
    native_world_vertex_used_,mib(std::uint64_t(native_world_vertex_used_*vertex)),
    static_cast<unsigned long long>(slots),static_cast<unsigned long long>(terrain),mib(std::uint64_t(terrain*vertex)),
    static_cast<unsigned long long>(posed),mib(std::uint64_t(posed*vertex)),static_cast<unsigned long long>(other),mib(std::uint64_t(other*vertex)),
    static_cast<unsigned long long>(held),mib(std::uint64_t(held*vertex)),
    native_prepared_vertex_capacity_,mib(std::uint64_t(native_prepared_vertex_capacity_)*sizeof(NativePreparedVertexGpu)));
   // Why main-map objects with drawn parts are (not) in object space: objects
   // and parts per reason (the posed ones own world vertices).
   {
    static const char* const names[]={"admitted","not_eligible","unit","hidden","animating","scaled","texture_kind","model_failed","model_pending","refused"};
    static_assert(sizeof(names)/sizeof(names[0])==std::size_t(ObjectSpaceReason::count),"reason names");
    const auto reasons=active_statics_reasons();
    int n=int(std::strlen(text));
    n+=std::snprintf(text+n,sizeof(text)-std::size_t(n),"; object space (objects/parts):");
    for(std::size_t k=0;k<std::size_t(ObjectSpaceReason::count)&&n>0&&std::size_t(n)<sizeof(text)-40;++k)
     if(reasons[k])n+=std::snprintf(text+n,sizeof(text)-std::size_t(n)," %s %u/%u",names[k],reasons[k],reasons[std::size_t(ObjectSpaceReason::count)+k]);
   }
   {int m=int(std::strlen(text));std::snprintf(text+m,sizeof(text)-std::size_t(m),"; terrain records with liquid vertices (overlay) %llu, without (base) %llu",
    static_cast<unsigned long long>(terrain_liquid),static_cast<unsigned long long>(terrain-terrain_liquid));}
   bridge_report_event("vram_world",text);
   // What the posed world vertices are (#289): every live posed slot by its
   // mesh's resource and part, with slots, distinct objects, vertices a copy
   // and in all, largest 40. A slot holds one copy of its mesh's components
   // (unique position/normal/UV combinations, no LOD levels, no fur).
   {
    std::unordered_map<const NativeMesh*,const std::string*> names;
    for(const auto& item:native_meshes_)names.emplace(&item.second,&item.first.resource_key);
    struct Row{std::uint64_t slots=0,vertices=0,per_copy=0;std::unordered_set<std::int32_t> objects;std::unordered_set<const NativeMesh*> meshes;};
    std::unordered_map<std::string,Row> rows;std::uint64_t total=0,slots_all=0;std::unordered_set<std::int32_t> objects_all;
    const auto admitted_ids=active_statics_casters();
    std::uint64_t split[4]{},oldest=0,stale=0; // [object space?2:0 + referenced?1:0]
    for(const auto& slot:native_prepared_mesh_slots_){
     if(!slot.active||slot.world_base==UINT_MAX||(slot.decoded_channels&native_decoded_terrain)||slot.world_key.pose_slot==UINT_MAX)continue;
     const auto found=names.find(slot.mesh);
     auto& row=rows[found!=names.end()?*found->second:std::string("(no mesh)")];
     ++row.slots;row.vertices+=slot.world_count;row.per_copy=(std::max<std::uint64_t>)(row.per_copy,slot.world_count);
     row.objects.insert(slot.identity.signed_id);row.meshes.insert(slot.mesh);
     total+=slot.world_count;++slots_all;objects_all.insert(slot.identity.signed_id);
     // Proof split: is a draw using this copy, and is its object drawn in
     // object space now (its colour record no longer needs the copy)?
     const bool referenced=slot.world_allocation<native_world_ranges_.size()&&native_world_ranges_[slot.world_allocation].references>0;
     const bool admitted=admitted_ids&&std::binary_search(admitted_ids->ids.begin(),admitted_ids->ids.end(),slot.identity.signed_id);
     split[(admitted?2:0)+(referenced?1:0)]+=slot.world_count;
     if(!referenced){const std::uint64_t age=native_frame_index_-slot.world_resolve_frame;oldest=(std::max)(oldest,age);
      if(age>1800)stale+=slot.world_count;}
    }
    std::vector<std::pair<const std::string*,const Row*>> order;for(const auto& r:rows)order.push_back({&r.first,&r.second});
    std::sort(order.begin(),order.end(),[](const auto& a,const auto& b){return a.second->vertices>b.second->vertices;});
    int n=std::snprintf(text,sizeof(text),"posed world vertices %llu in %llu slots of %zu objects, %zu resources; object in object space and no draw using the copy %llu, object space with a draw %llu, posed object without a draw %llu, posed object with a draw %llu; unreferenced and unresolved for over 1800 frames %llu (oldest %llu frames); largest (resource: slots, objects, meshes, vertices a copy, all):",
     static_cast<unsigned long long>(total),static_cast<unsigned long long>(slots_all),objects_all.size(),rows.size(),
     static_cast<unsigned long long>(split[2]),static_cast<unsigned long long>(split[3]),static_cast<unsigned long long>(split[0]),
     static_cast<unsigned long long>(split[1]),static_cast<unsigned long long>(stale),static_cast<unsigned long long>(oldest));
    for(std::size_t i=0;i<order.size()&&i<40;++i){
     char entry[200];const auto& r=*order[i].second;
     std::snprintf(entry,sizeof(entry)," [%.70s: %llu, %zu, %zu, %llu, %llu]",order[i].first->c_str(),static_cast<unsigned long long>(r.slots),r.objects.size(),
      r.meshes.size(),static_cast<unsigned long long>(r.per_copy),static_cast<unsigned long long>(r.vertices));
     if(std::size_t(n)+std::strlen(entry)>=sizeof(text)){bridge_report_event("vram_posed",text);n=std::snprintf(text,sizeof(text),"largest (cont.):");}
     n+=std::snprintf(text+n,sizeof(text)-std::size_t(n),"%s",entry);
    }
    bridge_report_event("vram_posed",text);
   }
  }
 }
}catch(...){}

  // Host memory (#289: the x64 host commits 8-14 GB): committed private
  // memory by kind from one walk of the address space, the CRT heaps'
  // committed and allocated bytes, and the largest single allocations.
// One report per load reveal in developer mode and one on Shift+F11 (the
// walks cost ~240 ms, so never periodic).
void Renderer::report_host_memory() noexcept try{
 char text[960];
  {
   struct Block{std::uintptr_t base;std::uint64_t bytes;DWORD protect;};
   std::vector<Block> blocks;std::uint64_t mapped=0,image=0;
   MEMORY_BASIC_INFORMATION info{};std::uintptr_t at=0;
   while(VirtualQuery(reinterpret_cast<const void*>(at),&info,sizeof(info))==sizeof(info)){
    if(info.State==MEM_COMMIT){
     if(info.Type==MEM_PRIVATE){
      const auto base=reinterpret_cast<std::uintptr_t>(info.AllocationBase);
      if(blocks.empty()||blocks.back().base!=base)blocks.push_back({base,0,info.Protect});
      blocks.back().bytes+=info.RegionSize;
     }else if(info.Type==MEM_MAPPED)mapped+=info.RegionSize;else image+=info.RegionSize;
    }
    const auto next=reinterpret_cast<std::uintptr_t>(info.BaseAddress)+info.RegionSize;
    if(next<=at)break;at=next;
   }
   std::uint64_t heap_committed=0,heap_allocated=0;
   HANDLE heaps[64];const DWORD heap_count=(std::min)(GetProcessHeaps(64,heaps),DWORD(64));
   for(DWORD i=0;i<heap_count;++i){
    HEAP_SUMMARY summary{};summary.cb=sizeof(summary);
    if(HeapSummary(heaps[i],0,&summary)){heap_committed+=summary.cbCommitted;heap_allocated+=summary.cbAllocated;}
   }
   std::uint64_t combined=0,large=0,medium=0,little=0;unsigned large_count=0;
   for(const auto& b:blocks){
    if(b.protect&PAGE_WRITECOMBINE)combined+=b.bytes;
    else if(b.bytes>=(64ull<<20)){large+=b.bytes;++large_count;}
    else if(b.bytes>=(1ull<<20))medium+=b.bytes;else little+=b.bytes;
   }
   std::sort(blocks.begin(),blocks.end(),[](const Block& a,const Block& b){return a.bytes>b.bytes;});
   int n=std::snprintf(text,sizeof(text),"private committed: heaps %llu committed %.0f MiB (allocated %.0f MiB); write-combined %.0f MiB; every other private allocation (heap segments included) >=64 MiB %u %.0f MiB, 1-64 MiB %.0f MiB, <1 MiB %.0f MiB; mapped %.0f MiB, image %.0f MiB; largest:",
    static_cast<unsigned long long>(heap_count),mib(heap_committed),mib(heap_allocated),mib(combined),large_count,mib(large),mib(medium),mib(little),mib(mapped),mib(image));
   for(std::size_t i=0;i<blocks.size()&&i<12&&n>0&&std::size_t(n)<sizeof(text)-48;++i)
    n+=std::snprintf(text+n,sizeof(text)-std::size_t(n)," [%.1f MiB%s]",mib(blocks[i].bytes),(blocks[i].protect&PAGE_WRITECOMBINE)?" wc":"");
   bridge_report_event("host_memory",text);
   if(d3d12_)for(const auto& line:d3d12_->host_memory_report())bridge_report_event("host_report",line.c_str());
  }
}catch(...){}

// The pools sized at a map load that only grow in play (#289 sizing audit):
// at a reveal their load-time size, at the map change their size then (the
// map's peak). D3D12 sizes from the executor's buffers.
void Renderer::log_map_pools(const char* when) noexcept try{
 if(!d3d12_)return;
 const auto bytes=[&](const D3D12BufferRef& b)->std::uint64_t{return b?d3d12_->buffer_bytes(*b):0;};
 std::uint64_t flush=bytes(native12_.dynamic)+bytes(native12_.work)+bytes(native12_.arguments)+bytes(native12_.visible)+
  bytes(native12_.prepared)+bytes(native12_.owners)+bytes(native12_.indices);
 for(const auto& slot:native_slot_buffers12_)flush+=bytes(slot.dynamic)+bytes(slot.arguments)+bytes(slot.visible)+bytes(slot.prepared)+bytes(slot.owners)+bytes(slot.indices);
 char text[256];
 std::snprintf(text,sizeof(text),"%s: world vertex buffer %.0f MiB; world flush buffers %.0f MiB; terrain face pool %u slices (%.0f MiB)",when,
  mib(bytes(native12_.world_vertices)),mib(flush),face_slices_,double(face_slices_)*kTerrainFaceCacheSlice*kTerrainFaceCacheSlice*5/1048576.0);
 bridge_report_event("map_pools",text);
}catch(...){}

// Shift+F11's report: the walks run on a worker thread (they only read this
// process's own address space and the host's allocation table), so the render
// thread never stalls. The package thread waits for host_memory_report_done().
void Renderer::report_host_memory_async() noexcept{
 if(host_memory_running_.exchange(true))return; // a report is already running; its worker clears the package's wait
 try{
  if(host_memory_thread_.joinable())host_memory_thread_.join();
  host_memory_thread_=std::thread([this]{report_host_memory();host_memory_report_done();host_memory_running_=false;});
 }catch(...){host_memory_running_=false;host_memory_report_done();}
}

void Renderer::update_vram_budget() noexcept{
 if(take_host_memory_report_request()){report_vram_owners();report_host_memory_async();} // Shift+F11
 log_memory_status();
 // Map switch memory (#289): one line at each reveal with the video memory
 // and private commit then and the bytes uploaded while the load held. In
 // developer mode also the hold's peaks (every held frame's queries) and the
 // tracked memory by kind at its start and at its video memory peak.
 {
  static const bool developer=renderer_developer_mode();
  auto& p=switch_peak_;
  const bool holding=load_reveal_holding_.load();
  if(holding&&!developer){
   if(!p.active){p.active=true;p.start=native_frame_index_;p.uploaded=d3d12_?d3d12_->uploaded_bytes():0;}
  }else if(holding&&adapter3_){
   DXGI_QUERY_VIDEO_MEMORY_INFO local{};PROCESS_MEMORY_COUNTERS_EX memory{};
   // The tracked memory by kind at the hold's first frame and at its video
   // memory peak (#289: what a hold adds over the map it replaces).
   const auto kinds=[&](std::string& into){
    if(!d3d12_)return;
    try{auto lines=d3d12_->memory_report(local.CurrentUsage);if(!lines.empty())into=std::move(lines.front());}catch(...){}
   };
   if(SUCCEEDED(adapter3_->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&local))){
    if(!p.active){kinds(p.start_kinds);p.uploaded=d3d12_?d3d12_->uploaded_bytes():0;}
    if(local.CurrentUsage>p.video){p.video=local.CurrentUsage;kinds(p.peak_kinds);}
   }
   if(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory)))
    p.commit=(std::max<std::uint64_t>)(p.commit,memory.PrivateUsage);
   p.world=(std::max<std::uint64_t>)(p.world,std::uint64_t(native_world_vertex_capacity_)*sizeof(NativeWorldVertexGpu));
   if(!p.active){p.active=true;p.start=native_frame_index_;}
  }else if(p.active){
   p.active=false;
   DXGI_QUERY_VIDEO_MEMORY_INFO local{};PROCESS_MEMORY_COUNTERS_EX memory{};
   if(adapter3_)adapter3_->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&local);
   GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory));
   char detail[400];
   const double uploaded=mib(d3d12_&&d3d12_->uploaded_bytes()>p.uploaded?d3d12_->uploaded_bytes()-p.uploaded:0);
   if(developer)std::snprintf(detail,sizeof(detail),"held %llu frames: video memory peak %.0f MiB, at the reveal %.0f; private commit peak %.0f MiB, at the reveal %.0f; world vertex buffer peak %.0f MiB, at the reveal %.0f; uploaded %.0f MiB",
    static_cast<unsigned long long>(native_frame_index_-p.start),mib(p.video),mib(local.CurrentUsage),mib(p.commit),mib(memory.PrivateUsage),
    mib(p.world),mib(std::uint64_t(native_world_vertex_capacity_)*sizeof(NativeWorldVertexGpu)),uploaded);
   else std::snprintf(detail,sizeof(detail),"held %llu frames: video memory at the reveal %.0f MiB, private commit %.0f MiB; uploaded %.0f MiB",
    static_cast<unsigned long long>(native_frame_index_-p.start),mib(local.CurrentUsage),mib(memory.PrivateUsage),uploaded);
   bridge_report_event("map_switch_memory",detail);
   log_map_pools("at the reveal (load-time size)");
   if(!p.start_kinds.empty())bridge_report_event("map_switch_memory",("at the hold's start: "+p.start_kinds).c_str());
   if(!p.peak_kinds.empty())bridge_report_event("map_switch_memory",("at the video memory peak: "+p.peak_kinds).c_str());
   p=SwitchPeak{};
   // The developer reports of this load ran under the picture (apply_load_reveal, #344).
  }
 }
 auto& v=vram_budget_;
 if(!adapter3_||native_frame_index_<v.next_poll_frame)return;
 v.next_poll_frame=native_frame_index_+kPollFrames;
 if(!v.known){
  DXGI_QUERY_VIDEO_MEMORY_INFO non_local{};
  v.uma=SUCCEEDED(adapter3_->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL,&non_local))&&!non_local.Budget;
  char value[32]{};
  if(GetEnvironmentVariableA("EID3D11_VRAM_BUDGET_MB",value,sizeof(value))&&std::atoi(value)>0){
   v.overridden=true;v.override_bytes=std::uint64_t(std::atoi(value))<<20;}
  v.enforced=v.overridden||!v.uma;
 }
 DXGI_QUERY_VIDEO_MEMORY_INFO local{};
 if(FAILED(adapter3_->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&local))||!local.Budget)return;
 const bool first=!v.known;
 v.known=true;v.usage=local.CurrentUsage;v.budget=v.overridden?v.override_bytes:local.Budget;
 char detail[400];
 if(first){
  std::snprintf(detail,sizeof(detail),"adapter=%s budget=%.0f MiB%s used=%.0f MiB enforced=%d",
   v.uma?"uma":"discrete",mib(v.budget),v.overridden?" (EID3D11_VRAM_BUDGET_MB)":"",mib(v.usage),v.enforced?1:0);
  bridge_report_event("vram_budget",detail);
 }
 if(!v.enforced)return;
 const std::uint64_t high=v.budget-v.budget/20,low=v.budget-v.budget/10;
 if(v.level&&v.usage_at_step[v.level]>v.usage)
  v.step_savings[v.level]=(std::max)(v.step_savings[v.level],v.usage_at_step[v.level]-v.usage);
 if(v.usage>high){
  v.restore_polls=0;
  const std::uint64_t excess=v.usage-low;
  const std::uint64_t freed=evict_idle_textures_for_budget(excess,v.level>=3?kTopIdleFrames:kIdleFrames);
  if(freed){
   v.evicted_bytes+=freed;
   std::snprintf(detail,sizeof(detail),"action=evict_idle_textures freed=%.1f MiB used=%.0f budget=%.0f target=%.0f MiB level=%u",
    mib(freed),mib(v.usage),mib(v.budget),mib(low),v.level);
   bridge_report_event("vram_budget",detail);
  }
  if(freed>=excess||native_frame_index_<v.settle_until_frame)return;
  if(v.level<kTopLevel){
   ++v.level;v.usage_at_step[v.level]=v.usage;v.step_savings[v.level]=0;
   v.settle_until_frame=native_frame_index_+kSettleFrames;
   std::snprintf(detail,sizeof(detail),"action=reduce level=%u (%s) used=%.0f budget=%.0f target=%.0f MiB",
    v.level,vram_budget_level_text(v.level),mib(v.usage),mib(v.budget),mib(low));
   bridge_report_event("vram_budget",detail);
   return;
  }
  if(!v.cannot_fit_reported){
   v.cannot_fit_reported=true;
   const auto groups=video_memory_breakdown();
   std::string owners;
   if(groups.valid){
    std::vector<std::size_t> order;for(std::size_t i=0;i<groups.bytes.size();++i)order.push_back(i);
    std::sort(order.begin(),order.end(),[&](std::size_t a,std::size_t b){return groups.bytes[a]>groups.bytes[b];});
    for(std::size_t i=0;i<order.size()&&i<5;++i){char item[96];
     std::snprintf(item,sizeof(item),"%s%s %.0f MiB",i?", ":"",video_memory_group_name(static_cast<VideoMemoryGroup>(order[i])),mib(groups.bytes[order[i]]));
     try{owners+=item;}catch(...){}}
   }
   std::snprintf(detail,sizeof(detail),"action=cannot_fit used=%.0f budget=%.0f MiB after every reduction; renderer's largest: %s",
    mib(v.usage),mib(v.budget),owners.c_str());
   bridge_report_event("vram_budget",detail);
   std::fprintf(stderr,"VRAM budget: %s\n",detail);
  }
  return;
 }
 v.cannot_fit_reported=false;
 if(!v.level||native_frame_index_<v.settle_until_frame){v.restore_polls=0;return;}
 const std::uint64_t restore_cost=(std::max)(v.step_savings[v.level],v.budget/20);
 if(v.usage+restore_cost<low){
  if(++v.restore_polls<3)return;
  v.restore_polls=0;
  std::snprintf(detail,sizeof(detail),"action=restore level=%u->%u used=%.0f budget=%.0f MiB step_savings=%.0f MiB",
   v.level,v.level-1,mib(v.usage),mib(v.budget),mib(v.step_savings[v.level]));
  bridge_report_event("vram_budget",detail);
  --v.level;v.settle_until_frame=native_frame_index_+kSettleFrames;
 }else v.restore_polls=0;
}

VramBudgetSnapshot Renderer::vram_budget_snapshot() const noexcept{
 const auto& v=vram_budget_;
 VramBudgetSnapshot s{};
 s.known=v.known;s.enforced=v.enforced;s.uma=v.uma;s.overridden=v.overridden;s.level=v.level;
 s.budget_bytes=v.budget;s.usage_bytes=v.usage;s.evicted_bytes=v.evicted_bytes;
 for(unsigned level=1;level<=v.level&&level<v.step_savings.size();++level)s.shed_bytes+=v.step_savings[level];
 return s;
}

} // namespace owned
