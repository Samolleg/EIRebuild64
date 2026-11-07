#pragma once

// Ground cover on Terrain 2.0 (option terrain2; terrain2.h). With terrain2 on,
// the active map's cover places by the 2.0 ground's own blended materials,
// not by the game's per-tile terrain types: one compute pass a map evaluates
// terrain2Ground itself (shaders/terrain2.hlsl, included unchanged) over a
// grid of kTerrain2CoverTexels a tile and writes
//  - the share of each cover area class (ambient AreaClass 0..7) at each
//    texel: two RGBA8 slices, classes 0-3 and 4-7 (gcT2Classes, t62 of the
//    cover placement), read bilinearly as a rule's coverage;
//  - the blend of the materials' mean colours at half that resolution, with
//    mips, in the colour map's format: the cover's terrain colour in place of
//    the per-tile paint while 2.0 is on.
// One evaluation, no second copy of the field or edge scores: the
// pass supplies terrain2Ground's material reads, and each read returns a
// per-material value instead of the art (a class indicator, or the mean
// colour). The blend is linear in those values (the variance-preserving step
// is the identity when every tap equals the mean), so the result is exactly
// the blend weights the ground draws summed by class (terrain2Read hands
// each material's mean colour to terrain2CoverValue under TERRAIN2_COVER). The weights are those
// of flat ground (top plane, no rock or wall foot); the edge decisions read
// at a fixed resolution, so they match the view at every distance.
// A cell not copied yet reads terrain2Ground's marker (1,0,1); pass 0's z
// channel, 0 for every real cell, is then the missing share, subtracted from
// the channels the marker adds to: such places get no cover (visibly absent).

#include <cstdint>
#include <string>
#include "terrain_material.h"
#include "terrain2.h"

namespace owned {

inline constexpr std::uint32_t kTerrain2CoverTexels = 8; // class texels a 2 m tile (25 cm); the colour map has half

// Cover area class of a 2.0 material cell: a plain slot keeps the class the
// cover gives its own terrain type (gc_type_area of type & 15, here `area`;
// an authoritative soil, paving, stone, gravel or organic family over a grassy
// type is bare). Known plain cells may retain their native area when only a
// placement-inferred name supplies that exclusion. A folded cell (no slot type
// of its own) takes it from its family's Mapper style. 255: no class.
inline bool terrain2_cover_bare_style(std::uint32_t style) noexcept {
    return style == 0 || style == 1 || style == 2 || style == 3 || style == 7;
}
inline std::uint8_t terrain2_cover_class(std::uint32_t style, int area, bool authoritative_bare = true) {
    if (area >= 0) return std::uint8_t((area == 1 || area == 2) &&
        terrain2_cover_bare_style(style) && authoritative_bare ? 0 : area);
    switch (style) {
    case 9: return 1;          // grass
    case 8: return 3;          // sand
    case 5: case 10: return 4; // ice, snow
    case 4: return 5;          // swamp
    case 11: return 6;         // water
    case 6: return 7;          // lava
    case kTerrain2NoStyle: return 255;
    default: return 0;         // organic, soil, paving, stone, gravel
    }
}

struct Terrain2CoverConstants {
    float terrain2[4]; // nativeTerrain2 (x on, z mask styles)
    float material[4]; // nativeTerrainMaterial (z 0: Medium or Low; w 1: the Low path)
    float cliff[4];    // nativeTerrainCliff (w the atlas cell margin)
    std::uint32_t copied[2]; // nativeContactMapMaterials
    float layout[2];   // nativeContactLayout.xy (the map's saved-layout origin)
    std::uint32_t grid[4]; // class texels x, y; class texels a tile
    float maps[36][4]; // nativeTerrain2Maps (native_shader.h size; only entry 0 is set: record 1 w, 2 zw, 3 terrain2_cover_record)
};
static_assert(sizeof(Terrain2CoverConstants) == 656, "terrain2 cover constant layout");

// The environment terrain2.hlsl expects from the native pixel shader, as a
// compute pass: the values above under their native names, the contact grid
// and atlas, and the material reads (only the parts terrain2Ground uses).
inline constexpr char kTerrain2CoverHead[] = R"hlsl(
#define TERRAIN2_COVER 1
cbuffer Terrain2Cover:register(b0){float4 t2cTerrain2;float4 t2cMaterial;float4 t2cCliff;uint2 t2cCopied;float2 t2cLayout;uint4 t2cGrid;float4 t2cMaps[36];};
#define nativeTerrain2Maps t2cMaps
static const float4 nativeCliffBlend[2]={float4(0,0,1,1),float4(.766,.5,0,0)}; // flat: no rock exposure, top plane only (rock noise scale and streak 1, unused)
static const float4 nativeTerrainTess=float4(0,0,0,0);
#define nativeTerrain2 t2cTerrain2
#define nativeTerrainMaterial t2cMaterial
#define nativeTerrainCliff t2cCliff
#define nativeContactMapMaterials t2cCopied
static const uint contactMarginRows=6; // native_shader.h
uint contactBlendTileRows(uint gh){return gh>contactMarginRows+1u?(gh-1u-contactMarginRows)/2u:0u;}
#ifdef EID3D12
#define nativeBindlessIndex(index) (index) // native_shader.h (no hazards variant here)
#endif
// terrain_material.h's includer contract; its tile-art functions are not used here.
float4 terrainTapGrad(uint2 ref,float2 uv,float2 dx,float2 dy){return 0;}
float4 terrainTapLevel(uint2 ref,float2 uv,float lod){return 0;}
float2 terrainTapSize(uint2 ref,out float levels){levels=1;return 1;}
)hlsl";

inline constexpr char kTerrain2CoverBody[] = R"hlsl(
// A contact atlas cell (native_shader.h TerrainCliffCell, the fields
// terrain2.hlsl reads) and terrain_tile_blend.hlsl's cell geometry.
struct TerrainCliffCell{float2 lo;float2 size;float slice;float width;float fold;float2 dir;float3 shift;};
float2 terrainCliffCellUv(TerrainCliffCell k,float2 q){
 return k.fold>0?k.lo+k.dir*abs(1-2*frac(q))*k.fold:k.lo+q*k.size;
}
bool terrainTileBlendCellFrom(uint v,uint e,float aw,uint2 ready,out TerrainCliffCell k,out uint donor){
 uint slice=(v>>6)&63u,pos=v&63u,quadrant=0;bool folded=slice==63u;
 [branch]if(folded){slice=(e>>6)&63u;pos=e&63u;quadrant=(e>>12)&3u;}
 k.slice=slice;k.width=aw;k.fold=0;k.dir=1;k.shift=0;
 float margin=nativeTerrainCliff.w*.125;
 k.lo=float2((pos%8u)*.125+margin,1-(pos/8u+1)*.125+margin);k.size=.125-2*margin;
 [branch]if(folded){
  float2 corner=float2(quadrant==1u||quadrant==2u?1:0,quadrant>=2u?1:0);
  k.lo+=corner*k.size;k.dir=1-2*corner;k.fold=k.size.x*.3;
 }
 bool copied=slice<63u&&(((slice<32u?ready.x:ready.y)>>(slice&31u))&1u)!=0;
 uint d=(e>>14)&4095u,ds=d>>6;
 donor=folded&&copied&&((e>>26)&1u)!=0&&ds<63u&&(((ds<32u?ready.x:ready.y)>>(ds&31u))&1u)!=0?d:0xffffffffu;
 return copied;
}
void terrainTileBlendDonorCell(inout TerrainCliffCell k,uint d){
 float margin=nativeTerrainCliff.w*.125;
 k.slice=d>>6;k.fold=0;k.dir=1;
 k.lo=float2(((d&63u)%8u)*.125+margin,1-((d&63u)/8u+1)*.125+margin);
}
Buffer<uint> t2cCellClass:register(t17); // cell & 4095 -> cover area class (terrain2_cover_class; 255 none)
static uint t2cPass; // 0..2: class indicators (classes 0 1 -; 2 3 4; 5 6 7), 3: mean colour
// A material's value in this pass (terrain2Read: its mean colour given).
float3 terrain2CoverValue(uint cell,float3 mean){
 uint c=t2cCellClass.Load(int(cell&4095u));
 uint3 slot=t2cPass==0?uint3(0,1,99):t2cPass==1?uint3(2,3,4):uint3(5,6,7);
 return t2cPass==3?mean:float3(slot==c);
}
)hlsl";

inline constexpr char kTerrain2CoverKernel[] = R"hlsl(
RWTexture2DArray<unorm float4> t2cClasses:register(u0);
RWTexture2D<unorm float4> t2cColor:register(u1);
// terrain2Ground at map-local p (metres) as the close view draws it: a
// footprint far under every detail, flat (top plane, no liquid, no wall).
float3 t2cGround(float2 p,uint mode){
 t2cPass=mode;
 float2 surf;float3 unvaried;return terrain2Ground(0,float3(p,0),float3(p+t2cLayout,0),float3(1e-3,0,0),float3(0,1e-3,0),float3(0,0,1),float3(0,0,1),0,1e3,surf,unvaried);
}
[numthreads(8,8,1)]
void terrain2CoverCS(uint3 id:SV_DispatchThreadID){
 if(any(id.xy>=t2cGrid.xy))return;
 float metres=2.0/t2cGrid.z;
 float2 p=(float2(id.xy)+.5)*metres;
 // One terrain2Ground in a loop (compiled once): the three class passes
 // here, then for the even texels the colour texel (half resolution) at its
 // own centre: its marker share, then its mean colour.
 float2 q=(float2(id.xy/2)+.5)*2*metres;
 uint count=all((id.xy&1u)==0)?5u:3u;
 float3 r[5];
 [loop]for(uint k=0;k<count;++k)r[k]=t2cGround(k<3u?p:q,k<3u?k:(k==3u?0u:3u));
 float m=r[0].z; // the marker's share (cells not copied yet)
 t2cClasses[uint3(id.xy,0)]=saturate(float4(r[0].x-m,r[0].y,r[1].x-m,r[1].y));
 t2cClasses[uint3(id.xy,1)]=saturate(float4(r[1].z-m,r[2].x-m,r[2].y,r[2].z-m));
 [branch]if(count==5u){
  float mq=r[3].z;
  float3 colour=mq<1?saturate((r[4]-mq*float3(1,0,1))/(1-mq)):0;
  t2cColor[id.xy/2]=float4(colour,1-mq);
 }
}
)hlsl";

// The pass source: the environment, terrain_material.h, terrain2.hlsl, the kernel.
inline const std::string& terrain2_cover_shader_source() {
    static const std::string source = std::string(kTerrain2CoverHead) + kTerrainMaterialShader + kTerrain2CoverBody +
                                      kTerrain2Shader + kTerrain2CoverKernel;
    return source;
}

} // namespace owned
