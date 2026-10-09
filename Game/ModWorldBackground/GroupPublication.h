#pragma once
#include "WorldBackgroundVisual.h"
#include "Capacity.h"
#include <atomic>
#include <mutex>
#include <memory>
#include <algorithm>
#include <cstring>
#include <chrono>
#include <dxgi.h>

namespace ModWorldBackground {
// Value-only contract shared by the CPU producer, DX11 owner and SMALL inspector.
class GroupPublication {
public:
    using Key=std::array<uint32_t,4>;
    enum Mode { Keep,Geometry,Replace };
    enum Phase { Idle,Queued,Preparing,Prepared,Uploading,Ready,Committed,Cancelled,Error };
    enum Reason { Residency=1,Lighting=2 };
    enum Refusal { None,Deferred,Active,Additional,Meshes,Stale };
    enum FaultScope { Local,Global };
    struct Box {std::array<double,3> low={},high={};};
    struct Meta {Key key={};uint64_t geometry=0,texture=0,bytes=0;uint32_t vertices=0,width=0,height=0;
        ET_Material2 material={};float x=0,y=0;Box box;};
    struct Item {Meta meta;Mode mode=Replace;int source=-1,old=-1;uint64_t transfer=0;};
    struct Ticket {uint32_t epoch=0,group=0;uint64_t revision=0,generation=0,token=0,light=0,topology=0;Reason reason=Residency;};
    struct Set {std::vector<Item> items;std::vector<Box> removed;uint64_t bytes=0,token=0,light=0,revision=0,topology=0;bool partial=false,reduction=false;};
    struct Lane {Ticket desired,accepted;Set current,pending;Phase phase=Idle;uint64_t sequence=0,generation=1,topology=0,since=0,retryAt=0,submitted=0;
        uint64_t reserved=0,retirement=0,finalBytes=0;uint32_t newMeshes=0;Refusal refusal=None;bool cancel=false,holdReduction=false;uint64_t plannedBytes=0,pressurePeerRevision=0,releaseBytes=0;uint32_t releaseMeshes=0;};
    class RootPool:public std::enable_shared_from_this<RootPool> {
        std::atomic<size_t> count{0};
    public:
        struct Lease {std::shared_ptr<RootPool> pool;explicit Lease(std::shared_ptr<RootPool> p):pool(std::move(p)){}~Lease(){--pool->count;}Lease(const Lease&)=delete;Lease& operator=(const Lease&)=delete;};
        size_t Size()const{return count.load();}
        std::shared_ptr<Lease> Acquire(){size_t n=count.load();while(n<Capacity::Snapshots){if(count.compare_exchange_weak(n,n+1)){
                try{return std::make_shared<Lease>(shared_from_this());}catch(...){--count;throw;}}}return {};}
    };
private:
    mutable std::mutex mutex;std::array<Lane,2> lanes;uint32_t epoch=0;unsigned admitted=0;
    uint64_t retiredBytes=0;uint32_t retiredMeshes=0;
    std::shared_ptr<RootPool> roots=std::make_shared<RootPool>();
    static bool Busy(const Lane& l){return l.phase==Preparing||l.phase==Prepared||l.phase==Uploading||l.phase==Ready;}
    static unsigned Rank(const Ticket& t){return t.reason==Residency?(t.group==0?0u:1u):2u;}
    int SelectLocked(uint64_t now)const{
        if(retiredBytes||lanes[0].holdReduction)return -1;for(const auto& l:lanes)if(l.phase==Preparing)return -1;
        int best=-1;for(int i=0;i<2;++i){const auto& l=lanes[i];if(Busy(l)||l.cancel||now<l.retryAt||!l.desired.revision||l.desired.revision<=l.current.revision||(l.desired.reason==Lighting&&l.desired.topology!=l.current.topology))continue;
            if(best<0||Rank(l.desired)<Rank(lanes[best].desired))best=i;}
        if(admitted>=4){int aged=-1;for(int i=0;i<2;++i){const auto& l=lanes[i];if(Busy(l)||l.cancel||now<l.retryAt||!l.desired.revision||l.desired.revision<=l.current.revision||(l.desired.reason==Lighting&&l.desired.topology!=l.current.topology)||now-l.since<2000000)continue;
                if(aged<0||l.since<lanes[aged].since)aged=i;}if(aged>=0)best=aged;}return best;
    }
    bool Matches(const Lane& l,const Ticket& t)const{return t.epoch==epoch&&t.generation==l.generation&&t.revision==l.accepted.revision;}
public:
    std::shared_ptr<RootPool> Roots()const{return roots;}
    void Reset(uint32_t e){std::lock_guard<std::mutex> lock(mutex);epoch=e;for(auto& l:lanes){const auto generation=l.generation+1;l={};l.generation=generation;}admitted=0;retiredBytes=0;retiredMeshes=0;}
    Lane Read(unsigned g)const{std::lock_guard<std::mutex> lock(mutex);return lanes.at(g);}
    unsigned Admissions()const{std::lock_guard<std::mutex> lock(mutex);return admitted;}
    int Select(uint64_t now)const{std::lock_guard<std::mutex> lock(mutex);return SelectLocked(now);}
    void Retired(uint64_t bytes,uint32_t count){std::lock_guard<std::mutex> lock(mutex);retiredBytes=bytes;retiredMeshes=count;}
    void Request(unsigned g,uint32_t e,uint64_t token,uint64_t light,uint64_t now,uint64_t estimate=0){
        std::lock_guard<std::mutex> lock(mutex);auto& l=lanes.at(g);if(e!=epoch||!token||!light)return;
        if(g==0&&(l.plannedBytes!=estimate||l.pressurePeerRevision!=lanes[1].current.revision)){l.plannedBytes=estimate;l.pressurePeerRevision=lanes[1].current.revision;l.holdReduction=estimate>Capacity::PayloadBytes-(std::min)(Capacity::PayloadBytes,lanes[1].current.bytes);}
        if(!l.current.partial&&l.current.token==token&&l.current.light==light&&!Busy(l)){l.desired={};return;}
        const bool residency=l.current.partial||l.current.token!=token;
        if(l.desired.revision>l.current.revision&&l.desired.token==token&&l.desired.light==light&&l.desired.reason==(residency?Residency:Lighting)&&(residency||l.desired.topology==l.current.topology))return;
        if(!l.desired.revision||l.desired.revision<=l.current.revision)l.since=now;
        if(residency&&Busy(l)&&l.accepted.reason==Lighting){++l.generation;l.cancel=true;}
        Ticket t;t.epoch=e;t.group=g;t.revision=++l.sequence;t.generation=l.generation;t.token=token;t.light=light;t.reason=residency?Residency:Lighting;
        t.topology=residency?++l.topology:l.current.topology;l.desired=t;
        // Native one-time cooperative preemption applies to CPU decor only.
        if(g==0&&residency&&lanes[1].phase==Preparing&&now-lanes[1].since<2000000){++lanes[1].generation;lanes[1].cancel=true;}
    }
    bool Begin(unsigned g,uint64_t now,Ticket& out){std::lock_guard<std::mutex> lock(mutex);if(SelectLocked(now)!=int(g))return false;
        auto& l=lanes[g];l.accepted=l.desired;l.accepted.generation=l.generation;l.phase=Preparing;l.submitted=now;l.refusal=None;out=l.accepted;return true;}
    bool Valid(const Ticket& t)const{std::lock_guard<std::mutex> lock(mutex);const auto& l=lanes.at(t.group);return Matches(l,t)&&!l.cancel;}
    static bool SameMaterial(const Meta& a,const Meta& b){return !std::memcmp(&a.material,&b.material,sizeof(a.material));}
    static Set Describe(const std::vector<Meta>& target,const Set& base,bool lighting=false){
        Set next;std::vector<int> slots(target.size(),-1);std::vector<bool> used(base.items.size(),false);
        for(size_t i=0;i<target.size();++i)for(size_t j=0;j<base.items.size();++j)if(!used[j]&&target[i].key==base.items[j].meta.key){slots[i]=int(j);used[j]=true;break;}
        for(size_t i=0;i<target.size();++i)if(slots[i]<0&&target[i].key[1]==0x80000000u)for(size_t j=0;j<base.items.size();++j){const auto& b=base.items[j].meta;const auto& a=target[i];
            if(!used[j]&&a.key[0]==b.key[0]&&a.key[2]==b.key[2]&&a.texture==b.texture&&a.width==b.width&&a.height==b.height&&SameMaterial(a,b)){slots[i]=int(j);used[j]=true;break;}}
        for(size_t i=0;i<target.size();++i){Item item;item.meta=target[i];item.source=int(i);item.old=slots[i];item.mode=Replace;
            if(item.old>=0){const auto& old=base.items[size_t(item.old)].meta;
                if(old.texture==item.meta.texture&&old.width==item.meta.width&&old.height==item.meta.height&&SameMaterial(old,item.meta))
                    item.mode=old.key==item.meta.key&&old.geometry==item.meta.geometry&&old.x==item.meta.x&&old.y==item.meta.y?Keep:Geometry;}
            if(lighting&&item.mode==Keep)item.mode=Geometry;
            item.transfer=item.mode==Keep?0:uint64_t(item.meta.vertices)*sizeof(ET_Vertex)+(item.mode==Replace?uint64_t(item.meta.width)*item.meta.height*4:0);
            next.bytes+=item.meta.bytes;next.items.push_back(item);}return next;
    }
    static bool Stage(const Set& base,Set& next,double x,double y){
        constexpr uint64_t tranche=8ull*1024*1024;std::vector<size_t> order;uint64_t total=0;
        for(size_t i=0;i<next.items.size();++i)if(next.items[i].mode!=Keep){order.push_back(i);total+=next.items[i].transfer;}
        if(total<=tranche||order.size()<2||!std::isfinite(x)||!std::isfinite(y))return false;
        auto distance=[&](size_t i){const auto& b=next.items[i].meta.box;double dx=(std::max)(0.0,(std::max)(b.low[0]-x,x-b.high[0]));double dy=(std::max)(0.0,(std::max)(b.low[1]-y,y-b.high[1]));return dx*dx+dy*dy;};
        std::stable_sort(order.begin(),order.end(),[&](size_t a,size_t b){return distance(a)<distance(b);});Set staged=base;
        for(size_t i=0;i<staged.items.size();++i){staged.items[i].mode=Keep;staged.items[i].old=int(i);staged.items[i].source=-1;staged.items[i].transfer=0;}
        uint64_t uploaded=0;size_t changed=0;
        for(size_t i:order){const auto& item=next.items[i];if(changed&&(uploaded>=tranche||item.transfer>tranche-uploaded))break;
            size_t slot=staged.items.size();if(item.old>=0){slot=size_t(item.old);if(base.items[slot].meta.key!=item.meta.key)continue;}
            if(slot==staged.items.size()&&staged.items.size()>=(std::max)(base.items.size(),next.items.size()))continue;
            const uint64_t bytes=staged.bytes-(slot<staged.items.size()?staged.items[slot].meta.bytes:0)+item.meta.bytes;
            if(bytes>(std::max)(base.bytes,next.bytes))continue;
            if(slot==staged.items.size())staged.items.push_back(item);else staged.items[slot]=item;
            staged.bytes=bytes;uploaded+=item.transfer;++changed;}
        if(!changed||changed==order.size())return false;
        staged.partial=true;staged.removed.clear();next=std::move(staged);return true;
    }
    Refusal AdmitLocked(unsigned g,const Set& candidate)const{
        const auto& own=lanes[g];const auto& peer=lanes[1-g];const uint64_t peerBytes=Busy(peer)?(std::max)(peer.current.bytes,peer.finalBytes):peer.current.bytes;
        const size_t peerCount=Busy(peer)?(std::max)(peer.current.items.size(),peer.pending.items.size()):peer.current.items.size();
        if(peerBytes>Capacity::PayloadBytes||candidate.bytes>Capacity::PayloadBytes-peerBytes||candidate.items.size()+peerCount>Capacity::Meshes)return Active;
        uint64_t reserve=0;uint32_t changes=0;for(const auto& item:candidate.items)if(item.mode!=Keep){reserve+=item.meta.bytes;++changes;}
        uint64_t additional=retiredBytes;if(Busy(peer))additional+=(std::max)(peer.retirement,peer.reserved);
        if(additional>Capacity::PayloadBytes||(std::max)(reserve,own.current.bytes)>Capacity::PayloadBytes-additional)return Additional;
        size_t count=retiredMeshes+own.current.items.size()+peer.current.items.size()+changes;if(Busy(peer))count+=peer.newMeshes;
        return count>Capacity::Meshes?Meshes:None;
    }
    Refusal Submit(const Ticket& t,Set candidate,uint64_t now,double x=0,double y=0){
        std::lock_guard<std::mutex> lock(mutex);auto& l=lanes.at(t.group);if(!Matches(l,t)||l.cancel||l.phase!=Preparing)return Stale;
        if(t.reason==Lighting){if(t.topology!=l.current.topology||candidate.items.size()!=l.current.items.size())return Stale;
            for(const auto& item:candidate.items){if(item.mode!=Geometry||item.old<0||size_t(item.old)>=l.current.items.size())return Stale;const auto& old=l.current.items[size_t(item.old)].meta;
                if(old.key!=item.meta.key||old.vertices!=item.meta.vertices||old.x!=item.meta.x||old.y!=item.meta.y)return Stale;}}
        if(t.group==1&&t.reason==Residency&&!l.current.partial)Stage(l.current,candidate,x,y);
        candidate.token=t.token;candidate.light=t.light;candidate.topology=t.topology;candidate.revision=t.revision;
        l.refusal=AdmitLocked(t.group,candidate);if(l.refusal!=None){l.phase=Error;l.retryAt=now+1000000;l.accepted={};return l.refusal;}
        l.pending=std::move(candidate);l.finalBytes=l.pending.bytes;l.retirement=l.current.bytes;l.reserved=0;l.newMeshes=0;
        for(const auto& item:l.pending.items)if(item.mode!=Keep){l.reserved+=item.meta.bytes;++l.newMeshes;}
        l.phase=Prepared;++admitted;if(now-l.since>=2000000)admitted=0;return None;
    }
    bool Upload(const Ticket& t){std::lock_guard<std::mutex> lock(mutex);auto& l=lanes.at(t.group);if(!Matches(l,t)||l.cancel||l.phase!=Prepared)return false;l.phase=Uploading;return true;}
    bool MarkReady(const Ticket& t){std::lock_guard<std::mutex> lock(mutex);auto& l=lanes.at(t.group);if(!Matches(l,t)||l.cancel||l.phase!=Uploading)return false;l.phase=Ready;return true;}
    bool Commit(const Ticket& t){std::lock_guard<std::mutex> lock(mutex);auto& l=lanes.at(t.group);if(!Matches(l,t)||l.cancel||l.phase!=Ready)return false;
        std::vector<bool> kept(l.current.items.size(),false);for(const auto& item:l.pending.items)if(item.mode==Keep&&item.old>=0)kept.at(size_t(item.old))=true;
        l.releaseBytes=0;l.releaseMeshes=0;for(size_t i=0;i<l.current.items.size();++i)if(!kept[i]){l.releaseBytes+=l.current.items[i].meta.bytes;++l.releaseMeshes;}
        retiredBytes+=l.releaseBytes;retiredMeshes+=l.releaseMeshes;
        l.current=std::move(l.pending);l.pending={};l.phase=Committed;l.reserved=l.retirement=l.finalBytes=0;l.newMeshes=0;return true;}
    void Released(unsigned g){std::lock_guard<std::mutex> lock(mutex);auto& l=lanes.at(g);retiredBytes-=l.releaseBytes;retiredMeshes-=l.releaseMeshes;l.releaseBytes=0;l.releaseMeshes=0;}
    void Cancel(unsigned g,uint64_t now,bool failure){std::lock_guard<std::mutex> lock(mutex);auto& l=lanes.at(g);++l.generation;l.pending={};l.accepted={};l.cancel=false;
        l.reserved=l.retirement=l.finalBytes=0;l.newMeshes=0;l.phase=failure?Error:Cancelled;if(failure)l.retryAt=now+1000000;}
    bool Settled(unsigned g)const{std::lock_guard<std::mutex> lock(mutex);const auto& l=lanes.at(g);return l.phase==Error||l.phase==Cancelled||
        (l.current.revision&&!l.current.partial&&!l.current.reduction&&l.current.token!=UINT64_MAX&&!Busy(l)&&(!l.desired.revision||(l.current.token==l.desired.token&&l.current.light==l.desired.light)));}
    std::array<unsigned,2> UploadOrder(uint64_t now,uint64_t serial)const{std::lock_guard<std::mutex> lock(mutex);std::array<unsigned,2> order={0,1};
        if(Busy(lanes[0])&&Busy(lanes[1])&&Rank(lanes[1].accepted)<Rank(lanes[0].accepted))std::swap(order[0],order[1]);
        const auto& low=lanes[order[1]];if(serial%4==0&&low.phase==Uploading&&now-low.submitted>=2000000)std::swap(order[0],order[1]);return order;}
    static bool Outside(const Box& box,const std::array<float,16>& clip){std::array<bool,6> outside={true,true,true,true,true,true};double magnitude=0;
        for(float v:clip){if(!std::isfinite(v))return false;magnitude+=std::abs(v);}if(!magnitude)return false;
        for(unsigned corner=0;corner<8;++corner){double p[4]={corner&1?box.high[0]:box.low[0],corner&2?box.high[1]:box.low[1],corner&4?box.high[2]:box.low[2],1},q[4]={};
            for(unsigned row=0;row<4;++row)for(unsigned col=0;col<4;++col)q[row]+=clip[row*4+col]*p[col];
            const double planes[6]={q[0]+q[3],q[3]-q[0],q[1]+q[3],q[3]-q[1],q[2],q[3]-q[2]};for(unsigned i=0;i<6;++i)outside[i]=outside[i]&&std::isfinite(planes[i])&&planes[i]<-.01;}
        return std::any_of(outside.begin(),outside.end(),[](bool b){return b;});}
    bool Reduce(uint64_t target,const std::array<float,16>& clip,uint64_t now){std::lock_guard<std::mutex> lock(mutex);auto& l=lanes[1];
        if(Busy(lanes[0])||Busy(l)||l.cancel||lanes[0].cancel||retiredBytes||l.current.bytes<=target)return false;
        Set next;for(size_t i=0;i<l.current.items.size();++i){auto item=l.current.items[i];if(Outside(item.meta.box,clip)){next.removed.push_back(item.meta.box);continue;}
            item.mode=Keep;item.old=int(i);item.source=-1;item.transfer=0;next.bytes+=item.meta.bytes;next.items.push_back(item);}
        if(next.removed.empty()||next.bytes>target)return false;
        Ticket t;t.epoch=epoch;t.group=1;t.revision=++l.sequence;t.generation=l.generation;t.token=UINT64_MAX;t.light=l.current.light;t.topology=++l.topology;
        next.token=t.token;next.light=t.light;next.topology=t.topology;next.revision=t.revision;next.partial=l.current.partial;next.reduction=true;
        l.desired=l.accepted=t;l.pending=std::move(next);l.phase=Prepared;l.finalBytes=l.pending.bytes;l.retirement=l.current.bytes;l.submitted=now;l.reserved=0;l.newMeshes=0;return true;}
    void Pressure(const std::array<float,16>& clip,uint64_t now){
        uint64_t target=0;{std::lock_guard<std::mutex> lock(mutex);auto& terrain=lanes[0];
            if(!terrain.holdReduction)return;
            if(terrain.plannedBytes>Capacity::PayloadBytes){terrain.holdReduction=false;return;}
            target=Capacity::PayloadBytes-terrain.plannedBytes;
            if(lanes[1].current.bytes<=target){terrain.holdReduction=false;return;}
            if(Busy(lanes[0])||Busy(lanes[1])||retiredBytes||lanes[0].cancel||lanes[1].cancel)return;}
        if(!Reduce(target,clip,now)){std::lock_guard<std::mutex> lock(mutex);lanes[0].holdReduction=false;}
    }
    bool Safe(const Set& value,const std::array<float,16>& clip)const{for(const auto& box:value.removed)if(!Outside(box,clip))return false;return true;}
    static bool GlobalFailure(HRESULT hr,HRESULT health,bool common=false){return common||FAILED(health)||hr==DXGI_ERROR_DEVICE_REMOVED||hr==DXGI_ERROR_DEVICE_RESET||hr==DXGI_ERROR_DEVICE_HUNG||hr==DXGI_ERROR_DRIVER_INTERNAL_ERROR;}
    FaultScope Failure(unsigned g,HRESULT hr,HRESULT health,uint64_t now,bool common=false){if(GlobalFailure(hr,health,common))return Global;Cancel(g,now,true);return Local;}
    static uint64_t Now(){return uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
};
}
