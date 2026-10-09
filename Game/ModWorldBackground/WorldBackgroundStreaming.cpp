#include "WorldBackgroundStreaming.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

namespace WorldBackground {
namespace {
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
uint32_t Word(const Blob& b,size_t offset){Check(offset<=b.size()&&4<=b.size()-offset,"Missing atlas header");return uint32_t(b[offset])|(uint32_t(b[offset+1])<<8)|(uint32_t(b[offset+2])<<16)|(uint32_t(b[offset+3])<<24);}
struct Bounds {double xmin,ymin,zmin,xmax,ymax,zmax;};
Bounds BoundsFor(const Prepared& p,const Sector& s,double margin){
    Bounds b={1e300,1e300,1e300,-1e300,-1e300,-1e300};
    for(const auto* grid:{&s.land,&s.overlay})for(const auto& v:*grid){
        double x=double(v.x)+p.placement.offset.x,y=double(v.y)+p.placement.offset.y,z=v.z;
        Check(std::isfinite(x)&&std::isfinite(y)&&std::isfinite(z),"Nonfinite sector bound");
        b.xmin=std::min(b.xmin,x);b.ymin=std::min(b.ymin,y);b.zmin=std::min(b.zmin,z);
        b.xmax=std::max(b.xmax,x);b.ymax=std::max(b.ymax,y);b.zmax=std::max(b.zmax,z);
    }
    Check(b.xmin<=b.xmax,"Empty sector");b.zmin-=margin;b.zmax+=margin;return b;
}
bool Visible(const Bounds& b,const SectorView& view){
    if(!view.useFrustum)return true;
    // Reject only if all 8 corners are outside the same homogeneous clip plane.
    bool outside[6]={true,true,true,true,true,true};
    for(unsigned corner=0;corner<8;++corner){
        double point[4]={corner&1?b.xmax:b.xmin,corner&2?b.ymax:b.ymin,corner&4?b.zmax:b.zmin,1};
        double q[4]={};for(int row=0;row<4;++row)for(int column=0;column<4;++column)q[row]+=view.worldToClip[row*4+column]*point[column];
        double plane[6]={q[0]+q[3],q[3]-q[0],q[1]+q[3],q[3]-q[1],q[2],q[3]-q[2]};
        for(int i=0;i<6;++i)outside[i]=outside[i]&&plane[i]<0;
    }
    for(bool plane:outside)if(plane)return false;return true;
}
}
SectorCost MeasureSector(const Prepared& p,size_t index){
    Check(index<p.terrain.sectors.size(),"Invalid sector index");const auto& s=p.terrain.sectors[index];
    Check(s.land.size()==1089&&(s.overlay.empty()||s.overlay.size()==1089),"Invalid sector grids");
    // (material tag, atlas): land=0, overlay=1+material index.
    std::map<std::pair<uint32_t,uint32_t>,uint32_t> patches;
    for(size_t tile=0;tile<256;++tile){
        ++patches[{0,uint32_t((s.landTiles[tile]>>6)&255)}];
        if(!s.overlay.empty()&&s.overlayMaterials[tile]!=65535){
            Check(s.overlayMaterials[tile]<p.terrain.materials.size(),"Invalid overlay material");
            ++patches[{1u+s.overlayMaterials[tile],uint32_t((s.overlayTiles[tile]>>6)&255)}];
        }
    }
    SectorCost result;
    for(const auto& patch:patches){
        uint32_t atlas=patch.first.second;Check(atlas<p.textures.size(),"Atlas payload not prepared");
        const Blob& b=p.textures[atlas].mmp;Check(Word(b,0)==0x00504d4d,"Invalid atlas signature");
        uint32_t w=Word(b,4),h=Word(b,8);Check(w&&h&&w<=4096&&h<=4096,"Invalid atlas dimensions");
        // 8 triangles per tile; output uses ET_Vertex28 and no retained indices.
        uint32_t vertices=patch.second*24;Check(vertices<=65535,"Patch exceeds renderer vertex limit");
        result.bytes+=uint64_t(vertices)*28+uint64_t(w)*h*4;
        result.vertices+=vertices;++result.meshes;
        result.patches.push_back({patch.first.first,atlas,vertices,uint64_t(w)*h*4});
    }
    return result;
}
void BatchCost::Add(size_t neighbor,const SectorCost& cost){
    for(const auto& patch:cost.patches){
        uint32_t copies=1;
        if(patch.tag==0){
            auto& count=landVertices_[{neighbor,patch.atlas}];
            const uint32_t before=(count+BatchVertexLimit-1)/BatchVertexLimit;
            Check(patch.vertices<=0xffffffffu-count,"Batch vertex overflow");count+=patch.vertices;
            copies=(count+BatchVertexLimit-1)/BatchVertexLimit-before;
        }
        vertices+=patch.vertices;bytes+=uint64_t(patch.vertices)*28+uint64_t(copies)*patch.textureBytes;meshes+=copies;
    }
}
const SectorSelection& SectorStreaming::Update(const Snapshot& snapshot,uint64_t epoch,uint64_t layoutRevision,
    bool enabled,const SectorView& view,uint64_t budget,uint32_t meshLimit,uint64_t staticInputBytes,bool lighting,uint64_t futureBudget){
    const bool sameDomain=enabled_&&selection_.epoch==epoch&&selection_.layoutRevision==layoutRevision;
    SectorSelection next;next.epoch=epoch;next.layoutRevision=layoutRevision;
    std::set<SectorKey> previous;if(sameDomain)previous.insert(selection_.sectors.begin(),selection_.sectors.end());
    if(enabled) { const bool valid=std::isfinite(view.camera.x)&&std::isfinite(view.camera.y)&&std::isfinite(view.radius)&&view.radius>=0&&
        std::isfinite(view.prefetch)&&view.prefetch>=0&&std::isfinite(view.retention)&&view.retention>=0&&
        std::isfinite(view.verticalMargin)&&view.verticalMargin>=0&&std::isfinite(view.radius+view.prefetch+view.retention)&&budget<=MaxPayloadBudget&&meshLimit<=256;
        if(!valid){Clear();throw std::runtime_error("Invalid streaming view or renderer limits");}
        if(view.useFrustum){double magnitude=0;for(float v:view.worldToClip){if(!std::isfinite(v)){Clear();throw std::runtime_error("Invalid clip matrix");}magnitude+=std::abs(v);}if(magnitude==0){Clear();throw std::runtime_error("Empty clip matrix");}}
    }
    struct Candidate{SectorKey key;SectorCost cost;double distance;bool visible,required,future;};
    std::vector<Candidate> candidates;
    if(enabled)for(size_t ni=0;ni<snapshot.neighbors.size();++ni){const auto& p=snapshot.neighbors[ni];
        for(size_t si=0;si<p.terrain.sectors.size();++si)try{
            SectorKey key;key.neighbor=ni;key.sector=si;
            const Bounds b=BoundsFor(p,p.terrain.sectors[si],view.verticalMargin);
            double dx=std::max(0.0,std::max(b.xmin-view.camera.x,view.camera.x-b.xmax));
            double dy=std::max(0.0,std::max(b.ymin-view.camera.y,view.camera.y-b.ymax));
            double distance=std::hypot(dx,dy);
            double range=view.radius+view.prefetch+(previous.count(key)?view.retention:0);
            const bool future=distance>range;
            if(future){
                if(!view.anticipate||!std::isfinite(view.anticipated.x)||!std::isfinite(view.anticipated.y))continue;
                const double ax=std::max(0.0,std::max(b.xmin-view.anticipated.x,view.anticipated.x-b.xmax));
                const double ay=std::max(0.0,std::max(b.ymin-view.anticipated.y,view.anticipated.y-b.ymax));
                if(std::hypot(ax,ay)>view.radius+view.prefetch)continue;
            }
            bool visible=Visible(b,view),required=visible&&distance<=view.radius;
            candidates.push_back({key,MeasureSector(p,si),distance,visible,required,future});
        }catch(const std::exception&){++next.invalidSkipped;}
    }
    std::sort(candidates.begin(),candidates.end(),[&](const Candidate& a,const Candidate& b){
        if(a.future!=b.future)return a.future<b.future;
        if(a.required!=b.required)return a.required>b.required;
        if(a.visible!=b.visible)return a.visible>b.visible;
        if(a.distance!=b.distance)return a.distance<b.distance;
        bool ar=previous.count(a.key)!=0,br=previous.count(b.key)!=0;
        if(ar!=br)return ar>br;return a.key<b.key;
    });
    bool exhausted=false;BatchCost batched;
    for(const auto& c:candidates){
        // Keep a nearest-first prefix. Do not create distant islands by skipping
        // an expensive near sector in favour of a cheaper but more distant one.
        BatchCost proposed=batched;proposed.Add(c.key.neighbor,c.cost);
        if(exhausted||(c.future&&proposed.bytes>futureBudget)||proposed.bytes+staticInputBytes+(lighting?proposed.vertices*16+uint64_t(proposed.meshes)*24:0)>budget||proposed.meshes>meshLimit){exhausted=true;++next.budgetSkipped;if(c.required)++next.visibleSkipped;continue;}
        batched=std::move(proposed);next.sectors.push_back(c.key);next.bytes=batched.bytes;next.meshes=batched.meshes;
    }
    std::sort(next.sectors.begin(),next.sectors.end());
    std::set<SectorKey> desired(next.sectors.begin(),next.sectors.end());
    for(const auto& key:selection_.sectors)if(!sameDomain||!desired.count(key))next.removed.push_back(key);
    for(const auto& key:next.sectors)if(!sameDomain||!previous.count(key))next.added.push_back(key);
    const bool changed=!sameDomain||enabled!=enabled_||next.sectors!=selection_.sectors;
    next.request=changed?++sequence_:selection_.request;selection_=std::move(next);enabled_=enabled;
    return selection_;
}
bool SectorStreaming::Accepts(uint64_t epoch,uint64_t revision,uint64_t request)const{
    return enabled_&&epoch==selection_.epoch&&revision==selection_.layoutRevision&&request==selection_.request;
}
void SectorStreaming::Clear(){selection_=SectorSelection();selection_.request=++sequence_;enabled_=false;}
SectorApplyResult SectorResidency::Apply(const SectorSelection& plan,const Operation& publish,const Operation& remove){
    SectorApplyResult result;
    if(plan.epoch<epoch_)throw std::runtime_error("Stale sector map epoch");
    // DDraw invalidates all handles before delivering a new map epoch. Never
    // attempt Remove using a handle from an older epoch.
    if(epoch_!=plan.epoch){resident_.clear();epoch_=plan.epoch;request_=0;layoutRevision_=plan.layoutRevision;}
    if(plan.request<request_)throw std::runtime_error("Stale sector publication request");
    request_=plan.request;
    const bool changedLayout=layoutRevision_!=plan.layoutRevision;
    std::set<SectorKey> desired(plan.sectors.begin(),plan.sectors.end());
    std::vector<SectorKey> retained;
    for(const auto& key:resident_){
        if(!changedLayout&&desired.count(key)){retained.push_back(key);continue;}
        bool ok=false;try{ok=remove(key);}catch(const std::exception&){ok=false;}
        if(ok)++result.removed;else{++result.removeFailures;retained.push_back(key);}
    }
    resident_.swap(retained);
    // Failed removals still consume real resources; do not exceed the planned cap.
    if(result.removeFailures)return result;
    layoutRevision_=plan.layoutRevision;request_=plan.request;
    std::set<SectorKey> existing(resident_.begin(),resident_.end());
    for(const auto& key:plan.sectors)if(!existing.count(key)){
        bool ok=false;try{ok=publish(key);}catch(const std::exception&){ok=false;}
        if(ok){resident_.push_back(key);existing.insert(key);++result.loaded;}else ++result.loadFailures;
    }
    std::sort(resident_.begin(),resident_.end());return result;
}
}
