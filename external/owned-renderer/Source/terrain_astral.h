#pragma once
#include "owned_asset_loader.h"
#include "terrain_tile_blend.h"
#include <algorithm>
#include <cmath>

namespace owned {
// Only explicitly authored Astral corners, including mixed shore tiles. Never infer from pixels,
// TERRTYPE (Astral uses ordinary ground), or an atlas page number.
inline std::vector<std::uint8_t> terrain_astral_slots(const TerrainTileFamilies& families) {
    std::vector<std::uint8_t> slots(families.signatures.size(), 0);
    for (std::size_t s=0;s<slots.size();++s) {
        for (unsigned corner=0;corner<4;++corner) {
            const auto id=families.signatures[s][corner];
            const auto it=families.names.find(id);
            if (!id || it==families.names.end()) continue;
            std::string name=it->second;
            for (char& c:name) if(c>='A'&&c<='Z')c+=32;
            if(name=="astral")slots[s]|=std::uint8_t(1u<<corner);
        }
    }
    return slots;
}
inline bool terrain_astral_tile(const TerrainCpuAsset& source, const TerrainTileDescriptor& tile) {
    return !tile.transparent && tile.animation_ordinal==UINT_MAX &&
        tile.vertex_indices[0]<33u*33u &&
        tile.tile_index<source.astral_slots.size() && source.astral_slots[tile.tile_index];
}
// Distance to actual non-Astral base tiles, sampled at shared world-grid
// vertices. A missing tile or an outer map edge is not a luminous shore.
// This is evaluated only during geometry decode, not each frame.
inline float terrain_astral_edge(const TerrainCpuAsset& source, int vx, int vy) {
    float distance=4.f;
    const int tx=vx/2,ty=vy/2;
    for(int y=ty-2;y<=ty+2;++y)for(int x=tx-2;x<=tx+2;++x) {
        if(x<0||y<0||x>=int(source.sector_count_x*16)||y>=int(source.sector_count_y*16))continue;
        const auto index=std::size_t(y/16)*source.sector_count_x+x/16;
        if(index>=source.sectors.size())continue;
        const auto slot=source.sectors[index].base_tile_ids[(y%16)*16+x%16];
        if(slot==UINT16_MAX || (slot<source.astral_slots.size()&&source.astral_slots[slot]==15))continue;
        const float dx=(std::max)(float(x*2-vx),(std::max)(0.f,float(vx-x*2-2)));
        const float dy=(std::max)(float(y*2-vy),(std::max)(0.f,float(vy-y*2-2)));
        distance=(std::min)(distance,std::sqrt(dx*dx+dy*dy));
    }
    return distance;
}

inline unsigned terrain_astral_world_mask(const TerrainCpuAsset& source,const TerrainTileDescriptor& tile,
    const std::array<NativeTextureCoordinate,9>& uv) {
    const unsigned mask=source.astral_slots[tile.tile_index];
    std::array<std::uint32_t,4> corners{};
    for(unsigned k=0;k<4;++k)corners[k]=(mask>>k)&1u;
    const auto world=terrain_tile_world_corners(corners,{{{uv[0].u,uv[0].v},{uv[2].u,uv[2].v},{uv[6].u,uv[6].v},{uv[8].u,uv[8].v}}});
    return world[0]|(world[1]<<1)|(world[2]<<2)|(world[3]<<3);
}

// Shared by production nativePS and the small GPU preview/check.
inline constexpr char kTerrainAstralShader[]=R"(
float terrainAstralCoverage(uint flags,float2 local){
 uint m=(flags>>16)&15u;
 float w=lerp(lerp(float(m&1u),float((m>>1)&1u),local.x),
              lerp(float((m>>2)&1u),float((m>>3)&1u),local.x),local.y);
 return smoothstep(.25,.75,w);
}
float astralHash(float2 p){p=frac(p*float2(.1031,.1030));p+=dot(p,p.yx+33.33);return frac((p.x+p.y)*p.x);}
float astralNoise(float2 p){float2 c=floor(p),f=frac(p);f=f*f*(3-2*f);
 return lerp(lerp(astralHash(c),astralHash(c+float2(1,0)),f.x),lerp(astralHash(c+float2(0,1)),astralHash(c+1),f.x),f.y);}
float astralCloud(float2 p){return .57*astralNoise(p)+.28*astralNoise(p*2.03+17.1)+.15*astralNoise(p*4.07-9.2);}
float3 terrainAstralColor(float3 world,float3 eye,float seconds,float shoreDistance,float footprint){
 // Bounded camera offset: a suggestion of depth, with no geometry or depth-buffer displacement.
 float3 view=eye-world;float2 parallax=clamp(view.xy/max(abs(view.z),8),-2,2);
 // Include height so a steep painted backdrop retains visible structure in a low camera.
 float2 p=world.xy*float2(.055,.105)+world.z*float2(.021,.045);
 float2 warp=float2(astralNoise(p*.65+11),astralNoise(p*.65-23))-.5;
 float2 deep=p+warp*1.6+parallax*.22+seconds*float2(.0022,-.0013);
 float2 near=p*1.8+warp*2+parallax*.07+seconds*float2(-.0031,.0018);
 float cloud=pow(smoothstep(.28,.78,astralCloud(deep)),1.9);
 float veil=pow(smoothstep(.4,.8,astralCloud(near)),1.5);
 float fil=sin(deep.x*2.1+deep.y*4.3+astralCloud(deep*1.4)*8);
 float thin=pow(saturate(1-abs(fil)),10)*smoothstep(.52,.78,astralNoise(deep*.7+41));
 thin*=1-smoothstep(.15,.65,footprint);
 float shore=(1-smoothstep(0,3.5,shoreDistance))*smoothstep(.25,.68,astralCloud(near*.8+7));
 // Keep the black floor; raise only local violet currents and the broken shore haze.
 return float3(.006,.005,.012)+float3(.140,.055,.230)*cloud+
        float3(.100,.035,.170)*veil+float3(.035,.012,.060)*thin+
        float3(.180,.060,.280)*shore;
}
)";
} // namespace owned
