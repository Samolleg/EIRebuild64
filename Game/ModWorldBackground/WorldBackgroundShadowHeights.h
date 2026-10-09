#pragma once
#include "WorldBackgroundData.h"
#include <cmath>
#include <limits>
#include <stdexcept>

#pragma pack(push,8)
namespace WorldBackground {
// Detached full-MPR land heights. Not a GPU ABI and not a shadow calculator.
// Preserve all 33x33 samples of each sector, including distinct seam duplicates.
// Publish as const after worker construction; no pointers into Terrain survive.
class ShadowHeights {
public:
    static ShadowHeights Build(const Terrain& terrain, size_t payloadBudgetBytes, const std::function<bool()>& cancelled=std::function<bool()>()) {
        auto check=[&]{if(cancelled&&cancelled())throw std::runtime_error("Shadow preparation cancelled");};
        check();
        const uint64_t count=uint64_t(terrain.sectorsX)*terrain.sectorsY;
        if(!count||count!=terrain.sectors.size()||
            count>uint64_t(payloadBudgetBytes)/(1089*sizeof(float))||
            count>uint64_t((std::numeric_limits<size_t>::max)())/1089)
            throw std::runtime_error("Invalid or over-budget shadow height dimensions");
        ShadowHeights result;
        result.sectorsX_=terrain.sectorsX;result.sectorsY_=terrain.sectorsY;
        result.heights_.assign(size_t(count)*1089,std::numeric_limits<float>::quiet_NaN());
        for(const auto& sector:terrain.sectors){
            check();
            if(sector.x>=terrain.sectorsX||sector.y>=terrain.sectorsY||sector.land.size()!=1089)
                throw std::runtime_error("Invalid shadow height sector");
            const size_t offset=(size_t(sector.y)*terrain.sectorsX+sector.x)*1089;
            if(!std::isnan(result.heights_[offset]))throw std::runtime_error("Duplicate shadow height sector");
            for(size_t i=0;i<1089;++i){
                if((i&63)==0)check();
                const float z=sector.land[i].z;
                if(!std::isfinite(z)||z<0)throw std::runtime_error("Invalid shadow height");
                result.heights_[offset+i]=z;
            }
        }
        return result;
    }
    // Caller owns native TraceToSun stepping: -1 -> previous sector/31,
    // 33 -> next sector/1. Do not merge sector boundary samples or wrap maps.
    bool Read(int64_t sectorX,int64_t sectorY,int x,int y,float& height) const {
        if(sectorX<0||sectorY<0||uint64_t(sectorX)>=sectorsX_||uint64_t(sectorY)>=sectorsY_||
            x<0||x>32||y<0||y>32)return false;
        height=heights_[(size_t(sectorY)*sectorsX_+size_t(sectorX))*1089+size_t(y)*33+x];
        return true;
    }
    uint32_t SectorsX() const { return sectorsX_; }
    uint32_t SectorsY() const { return sectorsY_; }
    size_t Bytes() const { return heights_.size()*sizeof(float); }
    const std::vector<float>& Samples() const { return heights_; }
private:
    uint32_t sectorsX_=0,sectorsY_=0;
    std::vector<float> heights_;
};
}

#pragma pack(pop)
