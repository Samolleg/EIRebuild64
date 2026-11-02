#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace owned {

// Slope normal for terrain projection weights. The stored MPR vertex normals
// are smoothed for lighting and still point well up on cliffs, so a triplanar
// blend weighted by them keeps half of the stretched top projection there.
// This one-ring central difference over actual node positions follows the
// faces around the vertex instead. Lighting keeps the stored normals.
// `position(x,y)` returns the zone-grid node position; callers clamp at the
// zone edge. A heightfield normal always faces up, so only x and y are packed
// (signed 16-bit each); the shader rebuilds z.
template<class Position>
std::uint32_t terrain_slope_normal(const Position& position, int x, int y) {
    const std::array<float,3> l=position(x-1,y),r=position(x+1,y),d=position(x,y-1),u=position(x,y+1);
    const float ax=r[0]-l[0],ay=r[1]-l[1],az=r[2]-l[2];
    const float bx=u[0]-d[0],by=u[1]-d[1],bz=u[2]-d[2];
    float nx=ay*bz-az*by,ny=az*bx-ax*bz,nz=ax*by-ay*bx;
    const float length=std::sqrt(nx*nx+ny*ny+nz*nz);
    if(!(length>1e-12f)||!(nz>0))return 0;
    nx/=length;ny/=length;
    const auto pack=[](float value){
        return static_cast<std::uint32_t>(static_cast<std::uint16_t>(static_cast<std::int16_t>(
            std::lround((std::clamp)(value,-1.f,1.f)*32767.f))));
    };
    return pack(nx)|(pack(ny)<<16);
}

// Facet flatness of a vertex: the drawn triangles around node (x,y) (LOD0
// cells split 00-10-11 / 00-11-01), as the up component of the flattest
// one's normal (low byte) and of their mean (high byte), 0..255. A ledge or
// saddle between two cliffs is flat in its facets although the one-ring slope
// normal above sees the cliffs on both sides. Stored per vertex, it
// interpolates smoothly across facets, unlike a per-pixel facet normal (which
// jumps at every mesh fold and cut hard texture edges there).
template<class Position>
std::uint32_t terrain_facet_flatness(const Position& position, int x, int y) {
    float best=0,sum=0;int count=0;
    for(int cy=y-1;cy<=y;++cy)for(int cx=x-1;cx<=x;++cx){
        const std::array<float,3> a=position(cx,cy),b=position(cx+1,cy),c=position(cx+1,cy+1),d=position(cx,cy+1);
        const std::array<float,3>* tris[2][3]={{&a,&b,&c},{&a,&c,&d}};
        for(int t=0;t<2;++t){
            // Triangle t of cell (cx,cy) touches the node unless the node is
            // the corner the diagonal leaves out.
            const bool uses_b=t==0,uses_d=t==1;
            const bool node_a=cx==x&&cy==y,node_b=cx+1==x&&cy==y,node_c=cx+1==x&&cy+1==y,node_d=cx==x&&cy+1==y;
            if(!(node_a||node_c||(uses_b&&node_b)||(uses_d&&node_d)))continue;
            const auto& p0=*tris[t][0];const auto& p1=*tris[t][1];const auto& p2=*tris[t][2];
            const float ux=p1[0]-p0[0],uy=p1[1]-p0[1],uz=p1[2]-p0[2],vx=p2[0]-p0[0],vy=p2[1]-p0[1],vz=p2[2]-p0[2];
            const float nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,nz=ux*vy-uy*vx;
            const float l=std::sqrt(nx*nx+ny*ny+nz*nz);
            if(!(l>1e-12f))continue;
            const float up=std::fabs(nz)/l;best=(std::max)(best,up);sum+=up;++count;
        }
    }
    if(!count)return 255u|(255u<<8);
    const auto q=[](float v){return static_cast<std::uint32_t>(std::lround((std::clamp)(v,0.f,1.f)*255.f));};
    return q(best)|(q(sum/count)<<8);
}

// terrain_quality: 2 (high, default) honours each terrain material switch;
// 1 (medium) and 0 (low) drop the large-scale variation (antitile) and
// Terrain 2.0's fine warp and noise octaves (height_blend); low also takes
// Terrain 2.0's Low path (terrain2.hlsl T2_LOW). (The material's own side projection,
// terrain_triplanar, is gone: Terrain cliffs replaced it on every tile while
// on, and without them it smeared faces. The large-scale variation,
// terrain_macro_variation, is gone too: no visible tiling change in play.)
struct TerrainMaterialSwitches { bool antitile, height_blend; };
inline TerrainMaterialSwitches terrain_material_switches(int quality, bool antitile, bool height_blend) {
    return {quality >= 2 && antitile, quality >= 2 && height_blend};
}

// Shared verbatim by the production native shader and its GPU regression.
// The includer defines, before this text:
//   float4 terrainTapGrad(uint2 ref,float2 uv,float2 dx,float2 dy);
//   float4 terrainTapLevel(uint2 ref,float2 uv,float lod);
//   float2 terrainTapSize(uint2 ref,out float levels);
// The material works on the authored MPR tile art only. It keeps the tile's
// own low-frequency image (transitions, paths, colour) exactly and replaces
// only detail above ~1/5 tile with detail read through randomly turned hex
// cells. Every feature fades to the unmodified tile with distance. Steep
// faces belong to Terrain cliffs (terrain_cliff.h).
inline constexpr char kTerrainMaterialShader[] = R"(
static const float terrainTileUnits = 2;         // A base MPR tile covers 2x2 world units.
static const float terrainTileMargin = .125;     // Atlas padding a tile (decoder: 8 texels of the 64-texel MPR tile; HD pages scale it).
static const float terrainLowpassTexels = 5.66;  // Coarse image kept exactly: ~5.7 texels a tile.
static const float terrainHexLattice = .28867513; // Lattice units a tile: neighbouring cells one tile apart.
static const float terrainDetailGainLimit = 1.6; // Variance-preserving blend never boosts beyond this.
static const float terrainMacroAmount = .12;     // Peak-to-peak world-space brightness variation.

float3 terrainSlopeNormal(uint packed){
 float2 xy=float2(int(packed<<16)>>16,int(packed)>>16)/32767.0;
 return float3(xy,sqrt(saturate(1-dot(xy,xy))));
}

// Hexagonal lattice (Mikkelsen 2022, "Practical Real-Time Hex-Tiling"):
// barycentric weights and the three cells around `st` (lattice units).
void terrainHexGrid(float2 st,out float3 w,out int2 v1,out int2 v2,out int2 v3){
 st*=3.46410162;
 float2 skewed=float2(st.x-.57735027*st.y,1.15470054*st.y);
 int2 base=int2(floor(skewed));
 float3 t=float3(frac(skewed),0);t.z=1-t.x-t.y;
 float s=step(0,-t.z),s2=2*s-1;
 w=float3(-t.z*s2,s-t.y*s2,s-t.x*s2);
 int i=int(s);
 v1=base+int2(i,i);v2=base+int2(i,1-i);v3=base+int2(1-i,i);
}
float2 terrainHexCentre(int2 v){return float2(v.x+.5*v.y,.8660254*v.y)/3.46410162;}
uint terrainHash(int2 v){
 uint h=uint(v.x)*0x8da6b343u^uint(v.y)*0xd8163841u;
 h^=h>>16;h*=0x7feb352du;h^=h>>15;h*=0x846ca68bu;return h^(h>>16);
}
float2 terrainHash2(int2 v){uint h=terrainHash(v);return float2(h&65535u,h>>16)/65535.0;}
// A cell turns its content around its own centre and shifts it by a random
// fraction of a tile, so the three cells never show the same repeat.
float2x2 terrainHexRotation(int2 v){
 float angle=6.28318531*terrainHash2(v+int2(7919,104729)).x;
 float c=cos(angle),s=sin(angle);
 return float2x2(c,-s,s,c);
}
// Value noise from a texture (terrain_noise_texture): uniform random lattice
// values in a tileable 1024 x 1024 R8 texture, read with one gather of the
// four corners and the same smoothstep interpolation the hashed lattice used
// (gather keeps full-precision weights; filtered bilinear weights are 8-bit).
// Each use is a layer with its own turn and offset, so no two layers repeat
// in alignment; a layer of lattice spacing L metres repeats every 1024 L.
Texture2D<float> terrainNoiseTexture:register(t122);
SamplerState terrainNoiseSampler:register(s12); // wrap
// Layer: cos, sin of the turn (Pythagorean triples), offset (lattice units).
// The rock streak layers (5, 6) read stretched coordinates: quarter turns
// only, so their lattice stays aligned with the stretch as before.
static const float4 terrainNoiseLayers[29]={
 float4(.6,.8,113.7,529.3),float4(.923077,.384615,871.1,77.9),float4(.470588,.882353,344.5,911.2),float4(.96,.28,702.3,268.8),
 float4(-.689655,.724138,51.9,640.4),float4(0,1,455.6,333.1),float4(-1,0,989.4,187.6),float4(-.849057,-.528302,238.2,802.7),
 float4(.983607,.180328,617.8,458.5),float4(.507692,-.861538,26.3,981.9),float4(-.969231,.246154,780.6,610.2),float4(.657534,.753425,398.9,13.4),
 float4(.988235,-.152941,164.1,717.7),float4(-.423529,.905882,934.7,401.5),float4(.438202,-.898876,507.3,145.8),float4(.742268,.670103,289.4,866.6),
 float4(-.6,-.8,666.6,590.1),float4(.384615,-.923077,82.8,344.9),float4(-.882353,.470588,845.2,236.4),float4(.28,-.96,431.7,954.3),
 float4(.724138,.689655,201.5,49.8),float4(-.324324,-.945946,573.9,772.6),float4(-.97561,.219512,12.4,507.1),float4(.528302,-.849057,759.8,690.3),
 float4(-.180328,.983607,362.6,121.7),float4(.861538,.507692,905.5,863.2),float4(-.246154,-.969231,128.9,398.4),float4(-.753425,.657534,644.3,935.9),
 float4(.6,.8,311.3,577.9)};
// Layers: 0-2 blend score octaves, 3-4 rock warp, 5-6 rock streaks, 7 rock
// patches, 8 rock edge, 9-10 field warp, 11-12 fine field warp, 13-24 face
// warps (three octaves, two planes, two channels), 25 single-vertex edge,
// 26-27 terrain material macro, 28 map-edge tile handover.
float terrainNoise(float2 p,uint layer){
 float4 l=terrainNoiseLayers[layer];
 p=float2(l.x*p.x-l.y*p.y,l.y*p.x+l.x*p.y)+l.zw;
 float2 i=floor(p),f=p-i;f=f*f*(3-2*f);
 // w (i,j), z (i+1,j), x (i,j+1), y (i+1,j+1): the texel corner at i+1.
 float4 g=terrainNoiseTexture.GatherRed(terrainNoiseSampler,(float2(int2(i)&1023)+1)/1024.0);
 return lerp(lerp(g.w,g.z,f.x),lerp(g.x,g.y,f.x),f.y);
}
// Two independent channels (layers `layer` and `layer`+1).
float2 terrainNoise2(float2 p,uint layer){return float2(terrainNoise(p,layer),terrainNoise(p,layer+1));}
float terrainMacro(float2 p){
 float n=.6*terrainNoise(p/23.0,26)+.4*terrainNoise(p/7.3,27);
 return 1+terrainMacroAmount*(n-.5);
}

// Long tiles (native_terrain_long_tile: a 1 x 8 strip of a long texture on
// one tile, the authored cliff art) take no material stage, so a distant
// cliff of them shows the strip repeating every tile. Once a pixel covers
// the strip's coarse image (~5.7 texels across), the strip is read through
// three hex cells over world XY, each shifted across the strip (mirrored, so
// continuous) but never along it (the strip's top-to-bottom art follows the
// cliff), blended about the strip's column mean with variance kept.
float4 terrainLongFar(uint2 ref,float2 uv,float2 uvDx,float2 uvDy,float3 p,float tiles,float4 orig){
 float levels;float2 size=terrainTapSize(ref,levels);
 if(tiles<1)return orig;
 float tile=1/tiles,stripTexels=size.x*tile;
 float lowLod=log2(max(stripTexels/terrainLowpassTexels,1));
 float fineLod=log2(max(max(length(uvDx*size),length(uvDy*size)),1e-6));
 float far=saturate(fineLod-(lowLod-.5));
 [branch]if(far<=.001||stripTexels<32||levels<lowLod+2)return orig;
 float u0=floor(uv.x*tiles)*tile,margin=terrainTileMargin*tile,inner=tile-2*margin;
 float x=(uv.x-u0-margin)/inner;
 float lod=min(max(lowLod,fineLod),log2(stripTexels)-1);
 float3 mean=terrainTapLevel(ref,float2(u0+.5*tile,uv.y),log2(stripTexels)).rgb;
 float3 w;int2 v1,v2,v3;
 terrainHexGrid(p.xy/terrainTileUnits*terrainHexLattice,w,v1,v2,v3);
 w=pow(max(w,0),3);w/=max(w.x+w.y+w.z,1e-6);
 int2 cells[3]={v1,v2,v3};
 float3 sum=0;float total=0,squares=0;
 [unroll]for(int k=0;k<3;++k){
  [branch]if(w[k]>=1.0/256){
   float t=frac((x+terrainHash2(cells[k]).x)*.5)*2,q=t<1?t:2-t;
   float3 c=terrainTapLevel(ref,float2(u0+margin+q*inner,uv.y),lod).rgb;
   sum+=w[k]*(c-mean);total+=w[k];squares+=w[k]*w[k];
  }
 }
 float3 color=squares>0?saturate(mean+sum*min(rsqrt(squares),terrainDetailGainLimit/max(total,1e-4))):orig.rgb;
 return float4(lerp(orig.rgb,color,far),orig.a);
}

)";

} // namespace owned
