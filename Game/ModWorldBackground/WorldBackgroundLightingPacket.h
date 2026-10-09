#pragma once
#include "WorldBackgroundLightingInputs.h"
#include "WorldBackgroundShadowHeights.h"
#include <memory>

#pragma pack(push,8)
namespace WorldBackground {
struct SectorLightingInputs {
    uint32_t x=0,y=0;
    std::vector<LightingInputs> land,overlay;
};
// Semantic worker output only, not a renderer ABI. Environment changes do not
// rebuild these geometry/material inputs. GPU admission is a separate owner.
class LightingPacket {
public:
    using Cancelled=std::function<bool()>;
    static std::shared_ptr<const LightingPacket> Build(const Prepared& source,
        uint32_t epoch,uint64_t revision,size_t budget,const Cancelled& cancelled=Cancelled()) {
        auto check=[&]{if(cancelled&&cancelled())throw std::runtime_error("Lighting preparation cancelled");};
        check();
        if(!epoch||!revision)throw std::runtime_error("Invalid lighting identity");
        uint64_t bytes=uint64_t(source.terrain.sectors.size())*(1089*sizeof(float)+sizeof(SectorLightingInputs));
        for(const auto& sector:source.terrain.sectors){
            check();
            if(sector.land.size()!=1089||(!sector.overlay.empty()&&sector.overlay.size()!=1089))
                throw std::runtime_error("Invalid lighting sector size");
            bytes+=uint64_t(1089)*sizeof(LightingInputs)*(sector.overlay.empty()?1:2);
            if(bytes>budget)throw std::runtime_error("Lighting payload budget exceeded");
        }
        auto result=std::shared_ptr<LightingPacket>(new LightingPacket());
        result->epoch_=epoch;result->revision_=revision;result->bytes_=size_t(bytes);
        result->identity_=source.placement.entry.identity;
        result->heights_=ShadowHeights::Build(source.terrain,budget,cancelled);
        check();
        result->sectors_.reserve(source.terrain.sectors.size());
        for(const auto& sector:source.terrain.sectors){
            check();SectorLightingInputs next;next.x=sector.x;next.y=sector.y;
            next.land.reserve(1089);if(!sector.overlay.empty())next.overlay.reserve(1089);
            for(size_t i=0;i<1089;++i){
                if((i&63)==0)check();
                next.land.push_back(GetLightingInputs(source.terrain,sector,i,false));
                if(!sector.overlay.empty())next.overlay.push_back(GetLightingInputs(source.terrain,sector,i,true));
            }
            result->sectors_.push_back(std::move(next));
        }
        check();return result;
    }
    uint32_t Epoch() const { return epoch_; }
    uint64_t Revision() const { return revision_; }
    size_t PayloadBytes() const { return bytes_; }
    const Identity& MapIdentity() const { return identity_; }
    const ShadowHeights& Heights() const { return heights_; }
    const std::vector<SectorLightingInputs>& Sectors() const { return sectors_; }
private:
    LightingPacket()=default;
    uint32_t epoch_=0;uint64_t revision_=0;size_t bytes_=0;
    Identity identity_;ShadowHeights heights_;std::vector<SectorLightingInputs> sectors_;
};
}

#pragma pack(pop)
