#pragma once
#include "PhysicsFxCore.h"
#include "owned_asset_loader.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace PhysicsFx {
struct GroundHit {Vec point,normal;unsigned material=8;bool found=false;};
// Independent of camera LOD. Base tiles are 3x3 source vertices with eight faces.
inline constexpr unsigned TerrainIndices[24]={0,1,4,0,4,3,1,2,5,1,5,4,3,4,7,3,7,6,4,5,8,4,8,7};
inline bool SolidTerrain(unsigned material){return material!=6&&material!=13&&material!=14&&material!=8;}
inline bool StoneTerrain(unsigned material){return material==2||material==4||material==15;}
inline bool PlaneHit(const Triangle& t,Vec point,GroundHit& out){
    const Vec u{t.b.x-t.a.x,t.b.y-t.a.y,t.b.z-t.a.z},v{t.c.x-t.a.x,t.c.y-t.a.y,t.c.z-t.a.z};
    Vec n{u.y*v.z-u.z*v.y,u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x};
    if(n.z<.00001f)return false;
    const float x=point.x-t.a.x,y=point.y-t.a.y;
    const float b=(x*v.y-y*v.x)/n.z,c=(u.x*y-u.y*x)/n.z;
    if(b<-.0001f||c<-.0001f||b+c>1.0001f)return false;
    const float z=t.a.z+b*u.z+c*v.z;
    if(out.found&&z<=out.point.z)return false;
    const float len=std::sqrt(n.x*n.x+n.y*n.y+n.z*n.z);
    out.point={point.x,point.y,z};out.normal={n.x/len,n.y/len,n.z/len};out.found=true;return true;
}
inline GroundHit ExtractTerrain(const owned::TerrainCpuAsset& terrain,Vec center,Vec query,std::vector<Triangle>& triangles){
    GroundHit hit;triangles.clear();
    for(const auto& sector:terrain.sectors){
        if(sector.bounds.maximum[0]<center.x-PatchRadius||sector.bounds.minimum[0]>center.x+PatchRadius||
           sector.bounds.maximum[1]<center.y-PatchRadius||sector.bounds.minimum[1]>center.y+PatchRadius)continue;
        for(const auto& tile:sector.tiles){
            if(tile.transparent||tile.object_id!=UINT16_MAX||tile.vertex_indices[0]>=1089||tile.tile_index>=terrain.tile_types.size())continue;
            const unsigned material=terrain.tile_types[tile.tile_index];
            if(!SolidTerrain(material)||(tile.tile_index<terrain.astral_slots.size()&&terrain.astral_slots[tile.tile_index]))continue;
            for(unsigned face=0;face<24;face+=3){
                Triangle t;Vec* vertices[]={&t.a,&t.b,&t.c};bool valid=true;
                for(unsigned j=0;j<3;++j){const auto index=tile.vertex_indices[TerrainIndices[face+j]];
                    if(index>=sector.vertices.size()){valid=false;break;}const auto& p=sector.vertices[index];*vertices[j]={p.x,p.y,p.z};}
                if(!valid)continue;
                if((std::max)({t.a.x,t.b.x,t.c.x})<center.x-PatchRadius||(std::min)({t.a.x,t.b.x,t.c.x})>center.x+PatchRadius||
                   (std::max)({t.a.y,t.b.y,t.c.y})<center.y-PatchRadius||(std::min)({t.a.y,t.b.y,t.c.y})>center.y+PatchRadius)continue;
                if(PlaneHit(t,query,hit))hit.material=material;
                triangles.push_back(t);
                if(triangles.size()>16384){triangles.clear();return {};}
            }
        }
    }return hit;
}
}
