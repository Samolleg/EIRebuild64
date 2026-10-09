#pragma once
#include "WorldBackgroundLightingPacket.h"
#include "WorldBackgroundEnvironment.h"
#include "WorldBackgroundSdk/world_background_lighting.h"
#include <cstring>
#pragma pack(push,8)
namespace WorldBackground {
// Worker-owned ABI storage. Heights borrow immutable packets retained here.
class LightingTransport {
    struct SectorStorage {std::vector<ET_LightVertex1> ground,overlay;};
    struct MapStorage {
        std::shared_ptr<const LightingPacket> packet;
        std::vector<SectorStorage> storage;
        std::vector<ET_LightSector1> sectors;
    };
    std::vector<MapStorage> storage_;
    std::vector<ET_LightMap1> maps_;
    ET_LightSnapshot1 view_={};uint64_t bytes_=0;
    static bool Same(const Identity& a,const Identity& b){return a.profile==b.profile&&a.allod==b.allod&&a.zone==b.zone&&a.mpr==b.mpr;}
    static ET_LightVertex1 Convert(const LightingInputs& v,bool overlay){
        ET_LightVertex1 out={};std::memcpy(out.normal,v.normal.data(),sizeof(out.normal));
        std::memcpy(out.emission,v.emission.data(),sizeof(out.emission));
        out.depth=v.depth;out.inverseTransparency=v.transparencyInverse;out.ownerMaterial=v.ownerMaterial;out.alpha=v.alpha;
        out.flags=uint8_t((v.layer?ET_LIGHT_LAYER:0)|(v.covered?ET_LIGHT_COVERED:0)|(overlay?ET_LIGHT_HAS_OVERLAY:0));return out;
    }
public:
    static std::shared_ptr<const LightingTransport> Build(const Snapshot& data,
        const std::vector<std::shared_ptr<const LightingPacket>>& packets,uint32_t epoch,Point origin,
        uint64_t budget,const std::function<bool()>& cancelled={}){
        auto check=[&]{if(cancelled&&cancelled())throw std::runtime_error("Lighting transport cancelled");};check();
        if(!epoch||data.neighbors.size()>256||packets.size()!=data.neighbors.size())throw std::runtime_error("Incomplete lighting packets");
        auto out=std::shared_ptr<LightingTransport>(new LightingTransport());
        out->storage_.resize(data.neighbors.size());out->maps_.resize(data.neighbors.size());
        for(size_t i=0;i<data.neighbors.size();++i){
            check();const auto& source=data.neighbors[i];auto& storage=out->storage_[i];
            for(const auto& packet:packets)if(packet&&Same(source.placement.entry.identity,packet->MapIdentity())){
                if(storage.packet)throw std::runtime_error("Duplicate lighting identity");storage.packet=packet;}
            if(!storage.packet||storage.packet->Epoch()!=epoch)throw std::runtime_error("Missing or stale lighting packet");
            for(size_t j=0;j<i;++j)if(Same(source.placement.entry.identity,data.neighbors[j].placement.entry.identity))throw std::runtime_error("Duplicate neighbor identity");
            const auto& p=*storage.packet;const auto& h=p.Heights();
            out->bytes_+=40;
            for(const auto& sector:p.Sectors())out->bytes_+=20+1089*4+uint64_t(sector.land.size()+sector.overlay.size())*36;
            if(out->bytes_>budget)throw std::runtime_error("Lighting transport budget exceeded");
            storage.storage.resize(p.Sectors().size());storage.sectors.resize(p.Sectors().size());
            for(size_t s=0;s<p.Sectors().size();++s){
                check();const auto& sector=p.Sectors()[s];auto& target=storage.storage[s];
                target.ground.reserve(1089);target.overlay.reserve(sector.overlay.size());
                for(const auto& v:sector.land)target.ground.push_back(Convert(v,!sector.overlay.empty()));
                for(const auto& v:sector.overlay)target.overlay.push_back(Convert(v,true));
                storage.sectors[s]={sector.x,sector.y,h.Samples().data()+(size_t(sector.y)*h.SectorsX()+sector.x)*1089,
                    target.ground.data(),target.overlay.empty()?nullptr:target.overlay.data()};
            }
            const float x=float(origin.x+source.placement.offset.x),y=float(origin.y+source.placement.offset.y);
            if(!std::isfinite(x)||!std::isfinite(y))throw std::runtime_error("Invalid lighting origin");
            out->maps_[i]={uint64_t(i)+1,p.Revision(),h.SectorsX(),h.SectorsY(),uint32_t(storage.sectors.size()),storage.sectors.data(),x,y};
        }
        check();out->view_={sizeof(ET_LightSnapshot1),uint32_t(out->maps_.size()),0,out->maps_.data(),nullptr};return out;
    }
    uint64_t Bytes()const{return bytes_;}
    const ET_LightSnapshot1& View()const{return view_;}
};
// Render-owner only. Byte comparison preserves signed-zero changes; serial is
// stamped separately so unchanged semantic input can keep its revision.
class LightingEnvironmentTransport {
    ET_LightEnvironment1 previous_={};uint64_t revision_=0;
public:
    void Clear(){previous_={};revision_=0;}
    ET_LightEnvironment1 Make(const VisualEnvironment& env,uint32_t epoch,uint32_t serial){
        ET_LightEnvironment1 value={};value.cbSize=sizeof(value);value.mapEpoch=epoch;
        value.valid=env.valid&(ET_LIGHT_ENV_SUN|ET_LIGHT_ENV_CLOCK|ET_LIGHT_ENV_POSITION|ET_LIGHT_ENV_ATMOSPHERE);
        if(value.valid&ET_LIGHT_ENV_SUN){std::memcpy(value.sunlight,env.light.data(),12);std::memcpy(value.direction,env.light.data()+3,12);std::memcpy(value.ambient,env.light.data()+6,12);}
        if(value.valid&ET_LIGHT_ENV_POSITION)std::memcpy(value.sunPosition,env.sunPosition.data(),12);
        if(value.valid&ET_LIGHT_ENV_ATMOSPHERE){std::memcpy(value.atmosphereRange,env.atmosphereRange.data(),12);std::memcpy(value.atmosphereProjection,env.atmosphereProjection.data(),12);}
        if(value.valid&ET_LIGHT_ENV_CLOCK)value.visualSeconds=env.clock;
        if(!revision_||std::memcmp(&value,&previous_,sizeof(value))!=0){
            if(revision_==UINT64_MAX)throw std::overflow_error("Lighting environment revision overflow");
            ++revision_;previous_=value;}
        value.revision=revision_;value.frameSerial=serial;return value;
    }
};
}
#pragma pack(pop)
