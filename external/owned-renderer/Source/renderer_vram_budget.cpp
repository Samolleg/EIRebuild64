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
//   - level 3: world textures without their top two mips.
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
#include "engine_bridge.h"
#include "neighbour_maps.h"
#include "owned_resource_runtime.h"
#include "renderer_internal.h"
#include <algorithm>
#include <cstdio>
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
constexpr std::uint64_t kRebuildBytesPerFrame = 16ull << 20;  // per-frame rebuild work, not a capacity limit
constexpr unsigned kTopLevel = 3;
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
std::uint64_t Renderer::evict_idle_textures_for_budget(std::uint64_t bytes) noexcept try{
 std::unordered_set<ID3D11ShaderResourceView*> queued;
 for(const auto& bucket:native_state_buckets_)if(bucket.occupied&&bucket.texture_srv)queued.insert(bucket.texture_srv.Get());
 std::vector<std::shared_ptr<ID3D11Texture2D>> retained;
 retained_visual_scene().visual_detached_surface_textures(retained);
 std::unordered_set<ID3D11Texture2D*> held;for(const auto& texture:retained)held.insert(texture.get());
 const auto idle=[&](std::uint64_t frame){return native_frame_index_>frame+kIdleFrames;};
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

void Renderer::update_vram_budget() noexcept{
 log_memory_status();
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
  const std::uint64_t freed=evict_idle_textures_for_budget(excess);
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
