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
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <unordered_set>
#include <vector>

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
 for(const auto& item:native_owned_textures_){const auto& t=item.second;
  if(!t.camp_surface_alias&&!t.surface_shared&&t.texture&&idle(t.last_used_frame)&&!queued.count(t.srv.Get())&&!held.count(t.texture.Get()))
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
  if(c.key){const auto& t=native_owned_textures_.at(*c.key);freed+=resource_video_bytes(t.texture.Get());keys.push_back(*c.key);}
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

void Renderer::update_vram_budget() noexcept{
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
