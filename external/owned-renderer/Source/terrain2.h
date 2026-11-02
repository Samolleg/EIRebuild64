#pragma once

// Terrain 2.0 (option terrain2; outputs/terrain-2-design.md, issue #215).
// terrain2=0 keeps today's terrain unchanged. terrain2=1 draws the active
// map's walkable ground from the map's own material cells
// (terrain_tile_blend.h vertex cells and contact atlas), never from a tile's
// art, so the game's pre-blended transition tiles are not drawn there:
//  - extent: each tile's four corner materials, as the game draws it: the
//    corner pattern (outer 1 of 4, edge 2 of 4, diagonal, inside 3 of 4)
//    selects the EI Mapper's final reviewed mask of the pair owner's style
//    (3-4 materials: each its own, composed), drawn in tile space with a
//    hashed variant and mirror per tile; near each tile edge the weights
//    move to that edge's own profile, so neighbour tiles always agree
//    (load_terrain2_masks; embedded through camp_art.rc from
//    Assets/terrain2/masks);
//  - blend: a height blend (terrain2HeightBlend) with each material's height,
//    baked once a map per material cell from its atlas art
//    (bake_terrain2_heights, kTerrain2HeightShader) into terrain2Height, a
//    shared R8 array parallel to the contact atlas;
//  - materials: every plain slot the map places is its own material
//    (owned_terrain_tile_blend_rows slot_variants), so a family's variants
//    blend like any two materials.
// Each map resolves only its own materials: styles come from its own family
// names, cells from its own atlas. The active map and the neighbour maps
// each draw their own (the map table, update_terrain2_table); within 1.5
// tiles of a shared edge both maps' finished results mix by mirrored
// weights (M2, terrain2Color). Not yet 2.0: steep faces and rock (the old
// path, cross-faded over the cliff steepness range), long and animated
// tiles.

#include <cstdint>
#include <string>

namespace owned {

// The EI Mapper's blend styles in its priority order (lowest first). The
// mask resources and the shader (terrain2.hlsl) use these indices.
inline constexpr const char* kTerrain2Styles[] = {"organic", "soil", "paving", "stone", "swamp", "ice",
                                                  "lava", "gravel", "sand", "grass", "snow", "water"};
inline constexpr std::uint32_t kTerrain2StyleCount = 12, kTerrain2NoStyle = 15;
// Shader slots: the map table's resources are D3D11 t85 (stacked grids),
// t86-t89 (atlas pools) and t90-t93 (height pools), D3D12 bindless indices
// (terrain2.hlsl terrain2Index). The material cache on D3D12 moves off the
// slots the D3D12 frame uses for the snow field (t116) and the prepass
// (t117-t120).
inline constexpr unsigned kTerrain2CacheFineSlotD3D12 = 75, kTerrain2CacheCoarseSlotD3D12 = 76, kTerrain2CacheParamsSlotD3D12 = 77;
// Mask layers per style: edge v1, edge v2, outer-1of4 v1, v2, inside-3of4 v1, v2.
inline constexpr std::uint32_t kTerrain2MaskLayers = 6, kTerrain2MaskSide = 128;

// Style of an editor family name ("Grass / Common"), the Mapper's first-word rule.
inline std::uint32_t terrain2_style(std::string name) {
    for (char& c : name) if (c >= 'A' && c <= 'Z') c = char(c + 32);
    while (!name.empty() && name.front() == ' ') name.erase(name.begin());
    const auto starts = [&](const char* word) { return name.rfind(word, 0) == 0; };
    const auto has = [&](const char* word) { return name.find(word) != std::string::npos; };
    if (starts("grass")) return 9;
    if (has("snow")) return 10;
    if (starts("sand")) return 8;
    if (starts("rubble") || has("gravel")) return 7;
    if (starts("water") || has("/ water")) return 11;
    if (starts("ice")) return 5;
    if (starts("lava")) return 6;
    if (starts("swamp")) return 4;
    if (starts("paving") || starts("slabs")) return 2;
    if (starts("stone") || starts("rock")) return 3;
    if (starts("ground") || has("soil") || has("dirt") || has("mud")) return 1;
    return 0;
}

// Per-style mask blur (Gaussian sigma in texels of a 256 px tile; the
// accepted prototype used 7). Snow is softer: its masks' crisp claw tips.
inline float terrain2_mask_sigma(std::uint32_t style) { return style == 10 ? 11.f : 7.f; }

// Height bake (outputs/terrain-2-design.md "Height map bake", frozen from
// prototype v10): one isolated step from a material's tile art to its
// height, run once a map per material cell (plain slot, folded quadrant or
// donor slot) on the GPU, where the atlas (any format, block-compressed
// included) and the height array already live. The cell's art is read as
// a periodic tile, resampled to N = 256 (bicubic), then: delight (/ blur
// 64 px), colour-patch high-pass (hp px), dark-tail clip (dc std), slopes
// at 1/4/12 px (RMS-normalised, soft-clipped at 2.5 RMS, style weights),
// Frankot-Chellappa integration through a periodic FFT, 1/99% percentile
// normalisation and the style amplitude: hb = .5 + amp*(h01 - .5), written
// box-filtered into the cell's footprint of the R8 height array.
struct Terrain2BakeStyle { float w1, w4, w12, amp, hp, dc; };
inline Terrain2BakeStyle terrain2_bake_style(std::uint32_t style) {
    switch (style) {
    case 3: return {.6f, .6f, .15f, 1.f, 0.f, 0.f};    // stone
    case 2: return {.3f, .5f, .6f, 1.f, 0.f, 0.f};     // paving
    case 7: return {.4f, .6f, .3f, 1.f, 0.f, 0.f};     // gravel (v9 values)
    case 9: return {.8f, .3f, .1f, .75f, 0.f, .75f};   // grass
    case 8: case 10: return {.2f, .4f, .15f, .35f, 32.f, 0.f}; // sand; snow uses sand (not tuned)
    case 5: return {.2f, .4f, .5f, .5f, 0.f, 0.f};     // ice
    default: return {.3f, .5f, .1f, .6f, 32.f, 0.f};   // soil; organic, lava, swamp, water, none (not tuned)
    }
}
// Work buffer t2B: four complex 256x256 planes (B0..B3, base = plane*65536).
inline constexpr char kTerrain2HeightShader[] = R"hlsl(
Texture2DArray<float4> t2Atlas:register(t0);
SamplerState t2Sampler:register(s0);
RWStructuredBuffer<float2> t2B:register(u0);
RWStructuredBuffer<float> t2Stats:register(u1); // mean, std, rms, lo, hi
RWTexture2DArray<unorm float> t2Height:register(u2);
cbuffer Terrain2Bake:register(b0){
 float2 bLo;float2 bSize;float2 bDir;float bFold;float bAw;
 uint bN;uint bSlice;uint bSrc;uint bDst;
 float bSigma;float bWeight;uint bInverse;uint bAxis;
 uint2 bOrigin;uint bSide;uint bMode;
 float bAmp;float bClip;float2 bPad;
};
static const uint N=256,P1=65536,P2=131072,P3=196608;static const float Pi=3.14159265;
float2 cmul(float2 a,float2 b){return float2(a.x*b.x-a.y*b.y,a.x*b.y+a.y*b.x);}
uint at(int2 p){p&=255;return uint(p.y)*N+uint(p.x);}
float2 omega(uint2 p){float2 k=float2(p);k=k<128?k:k-256;return 2*Pi*k/N;}
// The cell's art at q (tile units, period 1): plain, or a mirror-folded quadrant.
float2 cellUv(float2 q){return bFold>0?bLo+bDir*abs(1-2*frac(q))*bFold:bLo+frac(q)*bSize;}
float lattice(int2 j){
 int n=int(bN);j=((j%n)+n)%n;
 float3 c=t2Atlas.SampleLevel(t2Sampler,float3(cellUv((float2(j)+.5)/n),bSlice),0).rgb;
 return dot(c,float3(.299,.587,.114));
}
float cubic(float x){x=abs(x);return x<1?(1.5*x-2.5)*x*x+1:x<2?((-.5*x+2.5)*x-4)*x+2:0;}
// B0 = B1 = luma, the art resampled periodically to N (bicubic, a = -.5).
[numthreads(16,16,1)]
void terrain2BakeLoadCS(uint3 id:SV_DispatchThreadID){
 float2 s=(float2(id.xy)+.5)*float(bN)/N-.5,f=s-floor(s);int2 b=int2(floor(s));
 float L=0;
 [loop]for(int y=-1;y<=2;++y){float wy=cubic(f.y-y);[unroll]for(int x=-1;x<=2;++x)L+=wy*cubic(f.x-x)*lattice(b+int2(x,y));}
 t2B[at(int2(id.xy))]=float2(saturate(L)+.001,0);t2B[P1+at(int2(id.xy))]=float2(saturate(L)+.001,0);
}
// One 256-point line of plane bSrc (rows bAxis 0, columns 1), radix 2 in place.
groupshared float2 t2Line[256];
[numthreads(128,1,1)]
void terrain2BakeFftCS(uint3 g:SV_GroupID,uint t:SV_GroupIndex){
 uint base=bSrc*P1,ln=g.x;
 [unroll]for(uint k=0;k<2;++k){uint i=t+k*128;t2Line[reversebits(i)>>24]=t2B[base+(bAxis==0?ln*N+i:i*N+ln)];}
 [loop]for(uint span=1;span<N;span*=2){
  GroupMemoryBarrierWithGroupSync();
  uint j=t%span,a=(t/span)*2*span+j;
  float s,c;sincos((bInverse!=0?Pi:-Pi)*float(j)/span,s,c);
  float2 x=t2Line[a],y=cmul(float2(c,s),t2Line[a+span]);
  t2Line[a]=x+y;t2Line[a+span]=x-y;
 }
 GroupMemoryBarrierWithGroupSync();
 float scale=bInverse!=0?1.0/N:1;
 [unroll]for(uint k2=0;k2<2;++k2){uint i=t+k2*128;t2B[base+(bAxis==0?ln*N+i:i*N+ln)]=t2Line[i]*scale;}
}
// Plane bDst = plane bSrc (a spectrum) x Gaussian of bSigma px.
[numthreads(16,16,1)]
void terrain2BakeGaussCS(uint3 id:SV_DispatchThreadID){
 float2 w=omega(id.xy);uint i=at(int2(id.xy));
 t2B[bDst*P1+i]=t2B[bSrc*P1+i]*exp(-.5*bSigma*bSigma*dot(w,w));
}
// Per-texel steps (bMode).
[numthreads(16,16,1)]
void terrain2BakeCombineCS(uint3 id:SV_DispatchThreadID){
 int2 p=int2(id.xy);uint i=at(p);
 float2 b0=t2B[i],b1=t2B[P1+i];
 [branch]if(bMode==0)t2B[P1+i]=float2(b1.x/b0.x,0);           // delight: D = L / blur64(L)
 else if(bMode==1)t2B[i]=float2(b1.x,0);                       // B0 = D
 else if(bMode==2)t2B[P1+i]=float2(b1.x-b0.x+1,0);            // colour-patch high-pass
 else if(bMode==3){float m=t2Stats[0],d=b1.x-m;                // dark-tail clip
  if(d<0)t2B[P1+i]=float2(m+d/(1+abs(d)/(bClip*t2Stats[1])),0);}
 else if(bMode==4)t2B[P1+i]=0;                                 // G = 0
 else if(bMode==5){float2 s=t2B[P3+i]/t2Stats[2];              // G += w * soft-clipped RMS-normalised slope
  t2B[P1+i]=b1+bWeight*s/(1+length(s)/2.5);}
 else if(bMode==6){float h=t2B[P2+i].x;                        // slope of the blurred D (forward, wrapped)
  t2B[P3+i]=float2(t2B[P2+at(p+int2(1,0))].x-h,t2B[P2+at(p+int2(0,1))].x-h);}
 else{                                                         // Frankot-Chellappa: B2 = H^ from B1 = FFT(Gx + i Gy)
  float2 z=b1,zn=t2B[P1+at(-p)];zn.y=-zn.y;
  float2 fx=.5*(z+zn),d=z-zn,fy=.5*float2(d.y,-d.x);
  float2 w=omega(id.xy),dx=float2(cos(w.x)-1,-sin(w.x)),dy=float2(cos(w.y)-1,-sin(w.y));
  float den=dot(dx,dx)+dot(dy,dy);
  t2B[P2+i]=all(p==0)?0:(cmul(fx,dx)+cmul(fy,dy))/max(den,1e-12);
 }
}
// One group: bMode 0 mean/std of B1.x, 1 RMS of B3, 2 the 1/99% percentiles of B2.x.
groupshared float t2Sum[1024];groupshared float t2Max[1024];groupshared uint t2Bins[1024];
float reduceSum(uint t,float v){
 t2Sum[t]=v;
 [loop]for(uint s=512;s>0;s>>=1){GroupMemoryBarrierWithGroupSync();if(t<s)t2Sum[t]+=t2Sum[t+s];}
 GroupMemoryBarrierWithGroupSync();float r=t2Sum[0];GroupMemoryBarrierWithGroupSync();return r;
}
[numthreads(1024,1,1)]
void terrain2BakeReduceCS(uint t:SV_GroupIndex){
 [branch]if(bMode==0){
  float s=0;for(uint i=t;i<P1;i+=1024)s+=t2B[P1+i].x;
  float m=reduceSum(t,s)/P1;s=0;
  for(uint j=t;j<P1;j+=1024){float d=t2B[P1+j].x-m;s+=d*d;}
  float v=reduceSum(t,s)/P1;
  if(t==0){t2Stats[0]=m;t2Stats[1]=max(sqrt(v),1e-9);}
 }else if(bMode==1){
  float s=0;for(uint i=t;i<P1;i+=1024)s+=dot(t2B[P3+i],t2B[P3+i]);
  float v=reduceSum(t,s)/P1;
  if(t==0)t2Stats[2]=sqrt(v)+1e-9;
 }else{
  float lo=1e30,hi=-1e30;
  for(uint i=t;i<P1;i+=1024){float h=t2B[P2+i].x;lo=min(lo,h);hi=max(hi,h);}
  t2Sum[t]=lo;t2Max[t]=hi;t2Bins[t]=0;
  [loop]for(uint s=512;s>0;s>>=1){GroupMemoryBarrierWithGroupSync();if(t<s){t2Sum[t]=min(t2Sum[t],t2Sum[t+s]);t2Max[t]=max(t2Max[t],t2Max[t+s]);}}
  GroupMemoryBarrierWithGroupSync();
  lo=t2Sum[0];hi=t2Max[0];float span=max(hi-lo,1e-9);
  for(uint j=t;j<P1;j+=1024)InterlockedAdd(t2Bins[min(uint((t2B[P2+j].x-lo)/span*1024),1023u)],1u);
  GroupMemoryBarrierWithGroupSync();
  if(t==0){
   uint count=0;float a=lo,b=hi;bool low=false;
   for(uint k=0;k<1024;++k){
    count+=t2Bins[k];float centre=lo+(k+.5)/1024*span;
    if(!low&&count>=P1/100){a=centre;low=true;}
    if(count>=P1-P1/100){b=centre;break;}
   }
   t2Stats[3]=a;t2Stats[4]=max(b,a+1e-9);
  }
 }
}
float heightAt(float2 c){
 float2 f=c-floor(c);int2 b=int2(floor(c));
 return lerp(lerp(t2B[P2+at(b)].x,t2B[P2+at(b+int2(1,0))].x,f.x),lerp(t2B[P2+at(b+int2(0,1))].x,t2B[P2+at(b+1)].x,f.x),f.y);
}
// The cell's footprint in the height array (bOrigin, bSide texels): hb,
// box-filtered (4x4 taps) from the 256 px height. A folded cell writes only
// its quadrant (and the padding beside it).
[numthreads(16,16,1)]
void terrain2BakeWriteCS(uint3 id:SV_DispatchThreadID){
 if(any(id.xy>=bSide))return;
 uint2 texel=bOrigin+id.xy;float2 uv=(float2(texel)+.5)/bAw,q;
 if(bFold>0){float2 t=(uv-bLo)*bDir/bFold;if(any(t>1))return;q=(1-t)*.5;}
 else q=(uv-bLo)/bSize;
 float footprint=float(N)/bN,h=0;float2 c=q*N-.5;
 [unroll]for(int y=0;y<4;++y)[unroll]for(int x=0;x<4;++x)h+=heightAt(c+(float2(x,y)-1.5)*.25*footprint);
 float h01=saturate((h/16-t2Stats[3])/(t2Stats[4]-t2Stats[3]));
 t2Height[uint3(texel,bSlice)]=.5+bAmp*(h01-.5);
}
)hlsl";

// The snow surface (update_snow_surface): the active map's snow material cell
// (art and baked height, the cell read as a periodic tile like the bake's
// cellUv) copied into one tiling 256 px texture, rgb the art, a the height;
// its levels are made after. Object snow build-up samples it top-down
// (native_shader.h nativeSnowSurface, t73).
inline constexpr unsigned kSnowSurfaceSlot = 73, kSnowSurfaceSampler = 11, kSnowSurfaceSide = 256;
inline constexpr char kSnowSurfaceShader[] = R"hlsl(
Texture2DArray<float4> ssAtlas:register(t0);
Texture2DArray<float> ssHeight:register(t1);
SamplerState ssSampler:register(s0);
RWTexture2D<unorm float4> ssOut:register(u0);
cbuffer SnowSurface:register(b0){float2 sLo;float2 sSize;float2 sDir;float sFold;float sSlice;};
[numthreads(16,16,1)]
void snowSurfaceCS(uint3 id:SV_DispatchThreadID){
 float2 q=(float2(id.xy)+.5)/256;
 float3 w=float3(sFold>0?sLo+sDir*abs(1-2*q)*sFold:sLo+q*sSize,sSlice);
 ssOut[id.xy]=float4(ssAtlas.SampleLevel(ssSampler,w,0).rgb,ssHeight.SampleLevel(ssSampler,w,0));
}
)hlsl";

// Inserted after the terrain material blend in the native pixel shader.
inline constexpr char kTerrain2Shader[] = "#if 0"
#include "shaders/terrain2.hlsl"
"\n#endif\n";

} // namespace owned
