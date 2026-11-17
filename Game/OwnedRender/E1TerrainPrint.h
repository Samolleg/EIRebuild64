#pragma once
#include "owned_asset_loader.h"
#include "owned_terrain_front_end.h"
#include "game_particles.h"
#include <algorithm>
#include <cmath>

namespace OwnedRender {
// Keep original terrain triangles intact. Clipping a triangle before Phong
// tessellation changes its control points and therefore changes its surface.
inline void BuildTerrainPrint(const owned::TerrainCpuAsset& terrain,float x,float y,
    float sx,float sy,float rotation,float mapRotation,float u0,float v0,float us,float vs,
    float ceiling,std::vector<owned::GameParticleGpu>& output) {
    output.clear();if(!(sx>0&&sy>0))return;
    const float c=std::cos(rotation),s=std::sin(rotation),mc=std::cos(mapRotation),ms=std::sin(mapRotation);
    const float rx=std::abs(c*sx)+std::abs(s*sy),ry=std::abs(s*sx)+std::abs(c*sy);
    constexpr unsigned faces[]={0,1,4,0,4,3,1,2,5,1,5,4,3,4,7,3,7,6,4,5,8,4,8,7};
    for(const auto& sector:terrain.sectors){
        if(sector.bounds.maximum[0]<x-rx||sector.bounds.minimum[0]>x+rx||sector.bounds.maximum[1]<y-ry||sector.bounds.minimum[1]>y+ry)continue;
        for(const auto& tile:sector.tiles){
            if(tile.transparent||tile.vertex_indices[0]>=1089)continue;
            for(unsigned f=0;f<24;f+=3){
                unsigned ids[3];for(unsigned j=0;j<3;++j)ids[j]=tile.vertex_indices[faces[f+j]];
                if(ids[0]>=sector.vertices.size()||ids[1]>=sector.vertices.size()||ids[2]>=sector.vertices.size())continue;
                const auto& a=sector.vertices[ids[0]];const auto& b=sector.vertices[ids[1]];const auto& d=sector.vertices[ids[2]];
                if((std::max)({a.x,b.x,d.x})<x-rx||(std::min)({a.x,b.x,d.x})>x+rx||
                   (std::max)({a.y,b.y,d.y})<y-ry||(std::min)({a.y,b.y,d.y})>y+ry)continue;
                for(auto id:ids){
                    const auto& p=sector.vertices[id];owned::GameParticleGpu q{};
                    q.center_radius[0]=p.x;q.center_radius[1]=p.y;q.center_radius[2]=p.z;q.center_radius[3]=ceiling;
                    const float u=-(c*(p.x-x)+s*(p.y-y))/(2*sx),v=-(-s*(p.x-x)+c*(p.y-y))/(2*sy);
                    q.uv[0]=(u*mc-v*ms+.5f)*us+u0;q.uv[1]=(u*ms+v*mc+.5f)*vs+v0;
                    // The same submerged-ground attenuation as nativeTessVS.
                    if(sector.has_layers&&id+1089<sector.vertices.size()){
                        const auto& layer=sector.vertices[id+1089];
                        if(layer.geometry_object!=UINT16_MAX)q.uv[2]=layer.z-p.z;
                    }
                    q.padding[0]=owned::owned_terrain_slope_normal(terrain,sector,id);
                    q.padding[1]=(u0>=.5f?1u:0u)|(v0>=.5f?2u:0u);
                    q.padding[2]=owned::kTerrainPrintTag;output.push_back(q);
                }
            }
        }
    }
}
}
