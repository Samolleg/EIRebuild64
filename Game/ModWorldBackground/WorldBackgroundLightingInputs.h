#pragma once
#include "WorldBackgroundData.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#pragma pack(push,8)
namespace WorldBackground {
// Detached semantic inputs, shared by a future GPU producer and CPU oracle.
// No renderer ABI, native pointer, light formula or runtime policy lives here.
struct LightingInputs {
    std::array<float,3> normal={},emission={};
    uint16_t ownerMaterial=65535;
    uint8_t alpha=255;
    float depth=0,transparencyInverse=1;
    bool layer=false,covered=false;
};
inline LightingInputs GetLightingInputs(const Terrain& terrain,const Sector& sector,size_t index,bool layer){
    if(index>=1089||sector.land.size()!=1089||(!sector.overlay.empty()&&sector.overlay.size()!=1089)||(layer&&sector.overlay.empty()))
        throw std::runtime_error("Invalid lighting vertex");
    LightingInputs result;result.layer=layer;
    const auto& vertex=layer?sector.overlay[index]:sector.land[index];
    result.normal={{vertex.nx,vertex.ny,vertex.nz}};
    for(float n:result.normal)if(!std::isfinite(n))throw std::runtime_error("Invalid lighting normal");
    if(sector.overlay.empty())return result;
    // Native initializes lower depth independently of material presence.
    result.depth=sector.overlay[index].z-sector.land[index].z;
    if(!std::isfinite(result.depth))throw std::runtime_error("Invalid lighting depth");
    result.covered=!layer&&result.depth>0;
    const int row=int(index/33),col=int(index%33);
    // Native SObjFindInfo: first present tile in x-major neighbor order.
    for(int x=(col-1)/2-(col==0?1:0);x<=col/2&&result.ownerMaterial==65535;++x)
        for(int y=(row-1)/2-(row==0?1:0);y<=row/2;++y){
            if(x<0||x>=16||y<0||y>=16)continue;
            const uint16_t id=sector.overlayMaterials[size_t(y)*16+x];
            if(id!=65535){result.ownerMaterial=id;break;}
        }
    if(result.ownerMaterial==65535)return result;
    if(result.ownerMaterial>=terrain.materials.size())throw std::runtime_error("Invalid lighting material");
    const auto& values=terrain.materials[result.ownerMaterial].values;
    const float opacity=values[3];
    if(!std::isfinite(opacity)||opacity<0||opacity>1)throw std::runtime_error("Invalid lighting opacity");
    // Opacity1 produces +infinity in native material conversion. Preserve that
    // fact explicitly; a future producer must handle 0*infinity, never clamp it
    // to an invented arbitrary absorption constant.
    result.transparencyInverse=opacity==1?std::numeric_limits<float>::infinity():float(1.0/(1.0-double(opacity)));
    result.alpha=layer?uint8_t(double(opacity)*255.0):uint8_t(255);
    // Native lower-light branch checks the sign bit, so +0 participates.
    if(layer||!std::signbit(result.depth))for(size_t i=0;i<3;++i){
        if(!std::isfinite(values[i])||!std::isfinite(values[4]))throw std::runtime_error("Invalid material emission");
        const float emission=(std::min)(values[i]*values[4],1.0f);
        result.emission[i]=layer?emission:(std::max)(emission,0.0f);
    }
    return result;
}
}
#pragma pack(pop)
