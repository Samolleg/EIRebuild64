#pragma once
// Terrain 2.0 material cache (option terrain2_cache; issue #215 M5,
// outputs/terrain-2-design.md). A camera-centred clipmap of terrain2Color,
// the finished flat-ground colour of terrain 2.0, so a covered pixel reads
// one or two cached texels instead of running the blend.
//
// Why it cannot drift (the rejected terrain_bake drifted while panning: its
// pages took the screen's density, any value, and re-baked whenever that
// left a 0.7-1.5x window, so the texel grid and every hard decision sampled
// on it moved with the camera; and live and cached pixels changed hands by
// a dithered magnification threshold that also moved with the camera):
//  - every level has a fixed power-of-two density, 128 >> l texels per
//    metre, on one world-aligned grid: texel (i, j) of level l always holds
//    terrain2Color at world ((i + .5) / D, (j + .5) / D) with a footprint of
//    one texel. A texel is a pure function of (l, i, j) and the inputs, so
//    baking it again gives the same bits;
//  - the bake is a full-screen pass per rectangle (no geometry, no screen
//    derivatives, no camera term): position from the texel index in
//    integer arithmetic;
//  - the window moves in whole steps of kTerrain2CacheStep texels; moving
//    never rewrites a texel that stays inside it (toroidal addressing);
//  - a pixel picks its levels from its own footprint (trilinear between the
//    two levels around it, as the per-pixel path samples the atlas), never
//    from the camera position;
//  - everything the colour depends on is hashed every frame
//    (terrain2_cache_inputs); a change empties every level, and until a
//    level is baked again the pixels that need it run the per-pixel path.
//
// What is cached: terrain2Color with the top projection only (planes 0 0 1),
// no liquid bed and the slope normal from the map's ground grid. The pixel
// stage uses the cache only where it would compute exactly that: walkable
// ground (projection planes 0 0 1) with no liquid over it. Faces, wall feet,
// rock and liquid beds stay per pixel (a top-down texel on a steep face
// covers a stretched strip of it; side projections need the screen's own
// footprint). Lighting, shadows, fog, wetness, snow and trails are applied
// after the material and are not in the cache.
//
// Layout: levels l_top..l_last. l_top is the finest the nearest visible
// ground needs; l_last the first whose world covers 128 texels. A "fine"
// level (the world wider than the window) is an N x N slice of a
// Texture2DArray (t118), addressed toroidally around the eye; a "coarse"
// level holds the whole world and sits in a small 2D atlas (t116). N is
// kTerrain2CacheSideFactor times the screen's pixels per metre at depth 1,
// the smallest window whose edge never holds a texel the per-pixel path
// would sample finer (flat ground, isotropic footprint: the edge of level l
// lies where the footprint is already two level-l texels).
#include <algorithm>
#include <array>
#include <cstdint>

namespace owned {

inline constexpr unsigned kTerrain2CacheLevels = 12;     // l = 0..11: 128 .. 1/16 texels per metre
inline constexpr float kTerrain2CacheTopDensity = 128.f; // texels per metre at l = 0
inline constexpr int kTerrain2CacheStep = 16;            // window moves in steps of this many texels
inline constexpr int kTerrain2CacheMargin = 4;           // texels baked beyond a map's edge
inline constexpr float kTerrain2CacheSideFactor = 3.f;   // N = factor x pixels per metre at depth 1
inline constexpr unsigned kTerrain2CacheParamCount = 2 + 4 * kTerrain2CacheLevels; // float4s (t117)

// A rectangle of global texels of one level, [x0, x1) x [y0, y1).
struct Terrain2CacheRect {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool empty() const noexcept { return x0 >= x1 || y0 >= y1; }
    long long area() const noexcept { return empty() ? 0 : (long long)(x1 - x0) * (y1 - y0); }
};
inline Terrain2CacheRect terrain2_cache_intersect(const Terrain2CacheRect& a, const Terrain2CacheRect& b) noexcept {
    return {(std::max)(a.x0, b.x0), (std::max)(a.y0, b.y0), (std::min)(a.x1, b.x1), (std::min)(a.y1, b.y1)};
}
// a minus b as at most four rectangles (bottom and top bands, then the sides).
template <class Out>
void terrain2_cache_subtract(const Terrain2CacheRect& a, const Terrain2CacheRect& b, Out&& out) {
    const Terrain2CacheRect c = terrain2_cache_intersect(a, b);
    if (c.empty()) { if (!a.empty()) out(a); return; }
    if (a.y0 < c.y0) out(Terrain2CacheRect{a.x0, a.y0, a.x1, c.y0});
    if (c.y1 < a.y1) out(Terrain2CacheRect{a.x0, c.y1, a.x1, a.y1});
    if (a.x0 < c.x0) out(Terrain2CacheRect{a.x0, c.y0, c.x0, c.y1});
    if (c.x1 < a.x1) out(Terrain2CacheRect{c.x1, c.y0, a.x1, c.y1});
}
inline int terrain2_cache_floor_div(int a, int b) noexcept { return a >= 0 ? a / b : -((-a + b - 1) / b); }
inline int terrain2_cache_mod(int a, int b) noexcept { const int m = a % b; return m < 0 ? m + b : m; }

// Pixel stage and bake (spliced after terrain2.hlsl into the native shader).
// t117 terrain2CacheParams:
//  [0] x mode (0 off, 1 on, 2 tinted by level), y fine side N (texels),
//      z first level, w last level;
//  [1] x level bias, y edge band (texels), z filter margin (texels), w 1 when
//      the height layer holds the bed flag (x64);
//  per level l at 2 + 4l:
//   valid rectangle (texels from the frame origin): lo.xy hi.xy, empty when lo > hi;
//   window rectangle (same frame): lo.xy hi.xy;
//   frame: xy origin (global texels; a multiple of N on fine levels), z
//   density (texels per metre), w the fine slice (-1: coarse);
//   coarse placement: xy atlas texel of the window's low corner, zw 1 / atlas size.
inline constexpr char kTerrain2CacheShader[] = R"hlsl(
// Terrain 2.0 material cache (terrain2_cache.h, option terrain2_cache).
#ifdef EID3D12
// D3D12 (one t table, kTerrain2Cache*SlotD3D12): t116-t118 hold the snow
// field and the prepass inputs there.
Texture2DArray<float4> terrain2CacheFine:register(t75);
Texture2D<float4> terrain2CacheCoarse:register(t76);
StructuredBuffer<float4> terrain2CacheParams:register(t77);
#else
Texture2DArray<float4> terrain2CacheFine:register(t118);
Texture2D<float4> terrain2CacheCoarse:register(t116);
StructuredBuffer<float4> terrain2CacheParams:register(t117);
#endif
// S3 surface: alpha of the colour levels holds the roughness (crevices
// packed in, terrain2SurfacePack); an R8G8 layer of the same slices and
// atlas holds the blended material height (x) and, in y, 1 where the
// liquid-bed fade could change the texel on dry ground under a liquid's
// footprint (terrain2.hlsl t2BedSensitive); both backends at t78/t79.
Texture2DArray<float2> terrain2CacheHeightFine:register(t78);
Texture2D<float2> terrain2CacheHeightCoarse:register(t79);
SamplerState terrain2CacheSampler:register(s15); // bilinear, wrap
// Level l's colour at world xy (bilinear) and its window's edge fade; false
// outside its valid rectangle less the filter margin.
float terrain2CacheFade(uint l,float2 x){
 float4 window=terrain2CacheParams[3+4*l],frame=terrain2CacheParams[4+4*l];
 [branch]if(frame.w<0)return 1; // coarse: the whole world
 float2 t=x*frame.z-frame.xy,e=min(t-window.xy,window.zw-t);
 return saturate(min(e.x,e.y)/terrain2CacheParams[1].y);
}
// Level l's bilinear colour and roughness at texel coordinate t of its
// frame: a fine slice wraps around (toroidal), the coarse atlas holds each
// tap inside the level's rectangle.
float4 terrain2CacheBilinear(uint l,float2 t){
 float4 window=terrain2CacheParams[3+4*l],frame=terrain2CacheParams[4+4*l];
 [branch]if(frame.w>=0)return terrain2CacheFine.SampleLevel(terrain2CacheSampler,float3(t/terrain2CacheParams[0].y,frame.w),0);
 float4 p=terrain2CacheParams[5+4*l];
 return terrain2CacheCoarse.SampleLevel(terrain2CacheSampler,(p.xy+clamp(t-window.xy,.5,window.zw-window.xy-.5))*p.zw,0);
}
// Level l's height layer (bilinear) at texel coordinate t of its frame:
// x the height, y the bed flag.
float2 terrain2CacheHeightBilinear(uint l,float2 t){
 float4 window=terrain2CacheParams[3+4*l],frame=terrain2CacheParams[4+4*l];
 [branch]if(frame.w>=0)return terrain2CacheHeightFine.SampleLevel(terrain2CacheSampler,float3(t/terrain2CacheParams[0].y,frame.w),0);
 float4 p=terrain2CacheParams[5+4*l];
 return terrain2CacheHeightCoarse.SampleLevel(terrain2CacheSampler,(p.xy+clamp(t-window.xy,.5,window.zw-window.xy-.5))*p.zw,0);
}
// Level l at world xy, false outside its valid rectangle less the filter
// margin. c: colour and roughness, Catmull-Rom (five bilinear taps): a texel
// already holds the colour filtered over its own footprint, and a bilinear
// read of it blurred it a second time, so a cached pixel read about a texel
// softer than the per-pixel path at the same level (save1165, F11
// frame_031732/031927; bias -1 A/B). The spline interpolates the texels
// instead. h (with surface, S3): the height, one bilinear read.
// dryBed (a pixel on dry ground under a liquid's footprint): also false
// where any texel the five taps weigh carries the bed flag (the same
// positions and filter, so a flagged texel the colour weighs is never missed).
bool terrain2CacheRead(uint l,float2 x,bool surface,bool dryBed,out float4 c,out float h){
 float4 valid=terrain2CacheParams[2+4*l],window=terrain2CacheParams[3+4*l],frame=terrain2CacheParams[4+4*l];
 float2 t=x*frame.z-frame.xy;c=0;h=.5;
 float m=terrain2CacheParams[1].z;
 if(any(t<valid.xy+m)||any(t>valid.zw-m))return false;
 float2 t1=floor(t-.5)+.5,f=t-t1;
 float2 w0=f*(-.5+f*(1-.5*f)),w1=1+f*f*(-2.5+1.5*f),w2=f*(.5+f*(2-1.5*f)),w3=f*f*(-.5+.5*f);
 float2 w12=w1+w2,t12=t1+w2/w12,t0=t1-1,t3=t1+2;
 [branch]if(dryBed){
  float flagged=terrain2CacheHeightBilinear(l,t12).y+terrain2CacheHeightBilinear(l,float2(t12.x,t0.y)).y+terrain2CacheHeightBilinear(l,float2(t0.x,t12.y)).y+
                terrain2CacheHeightBilinear(l,float2(t3.x,t12.y)).y+terrain2CacheHeightBilinear(l,float2(t12.x,t3.y)).y;
  if(flagged>0)return false;
 }
 float4 wc=float4(w12.x*w0.y,w0.x*w12.y,w3.x*w12.y,w12.x*w3.y);float wm=w12.x*w12.y;
 c=terrain2CacheBilinear(l,t12)*wm+terrain2CacheBilinear(l,float2(t12.x,t0.y))*wc.x+terrain2CacheBilinear(l,float2(t0.x,t12.y))*wc.y+
   terrain2CacheBilinear(l,float2(t3.x,t12.y))*wc.z+terrain2CacheBilinear(l,float2(t12.x,t3.y))*wc.w;
 c=saturate(c/(wm+dot(wc,1)));
 [branch]if(surface)h=terrain2CacheHeightBilinear(l,t).x;
 return true;
}
// The cached terrain2Color at world rest xy with screen derivatives dx, dy:
// trilinear between the two levels around the footprint (the larger axis,
// as the per-pixel path's trilinear atlas reads), the finer one fading out
// over its window's edge band. False (the caller runs terrain2Color) when a
// level it needs is not baked there, and where the footprint is finer than
// the first level: ground rising toward the camera nearer than the ground
// the levels were chosen for read the first level magnified (blurred near
// ground, save1165); it runs per pixel instead.
// surf (surface true, S3): the cached (height, roughness) at the same weights.
bool terrain2CacheTap(float2 x,float2 dx,float2 dy,bool surface,bool dryBed,inout float3 color,inout float2 surf){
 float4 head=terrain2CacheParams[0];
 float first=head.z,last=head.w;
 [branch]if(dryBed&&terrain2CacheParams[1].w<=0)return false; // no bed flag stored (the 32-bit renderer's R8 layer)
 float want=log2(max(max(length(dx),length(dy))*128,1e-8))+terrain2CacheParams[1].x;
 [branch]if(want<first)return false;
 float lambda=min(want,last);
 uint l=uint(lambda);float w1=l<uint(last)?lambda-l:0;
 float w0=(1-w1)*terrain2CacheFade(l,x);w1=1-w0;
 float4 c0=0,c1=0;float h0=.5,h1=.5;
 [branch]if(w0>1.0/256&&!terrain2CacheRead(l,x,surface,dryBed,c0,h0))return false;
 [branch]if(w1>1.0/256){if(l>=uint(last)||!terrain2CacheRead(l+1,x,surface,dryBed,c1,h1))return false;}
 color=c0.rgb*w0+c1.rgb*w1;
 surf=float2(h0*w0+h1*w1,c0.a*w0+c1.a*w1);
 [branch]if(head.x>1.5){ // terrain2_cache 2: tinted by level (the finer of the two)
  float3 tint=frac(float3(.0,.33,.67)+float(l)*.27);color=lerp(color,tint,.35);
 }
 return true;
}
bool terrain2CacheTap(float2 x,float2 dx,float2 dy,inout float3 color){float2 surf=0;return terrain2CacheTap(x,dx,dy,false,false,color,surf);}
// #243 (terrain_relief_derivfree): the cached height's gradient over world xy
// (height units per metre) where terrain2CacheTap answered at x with the same
// dx, dy: the tap's two levels and weights, read half the footprint to each
// side (central differences, one bilinear read of the height layer each). dx
// and dy only choose the level, as in the tap.
float terrain2CacheHeightAt(uint l,float w0,float w1,float2 x){
 float h=0;
 [branch]if(w0>1.0/256){float4 frame=terrain2CacheParams[4+4*l];h+=w0*terrain2CacheHeightBilinear(l,x*frame.z-frame.xy).x;}
 [branch]if(w1>1.0/256){float4 frame=terrain2CacheParams[8+4*l];h+=w1*terrain2CacheHeightBilinear(l+1,x*frame.z-frame.xy).x;}
 return h;
}
// Whether the levels the gradient reads hold x (terrain2CacheRead's own
// rectangle test with the filter margin, and the cache on): a pixel that did
// not take its colour from the cache may still read its height's slope there.
bool terrain2CacheHeightHeld(uint l,float2 x){
 float4 valid=terrain2CacheParams[2+4*l],frame=terrain2CacheParams[4+4*l];
 float2 t=x*frame.z-frame.xy;float m=terrain2CacheParams[1].z+1;
 return !(any(t<valid.xy+m)||any(t>valid.zw-m));
}
bool terrain2CacheHeightGradientHeld(float2 x,float2 dx,float2 dy){
 float4 head=terrain2CacheParams[0];
 [branch]if(head.x<=0)return false;
 float footprint=max(max(length(dx),length(dy)),1e-8);
 // Ground finer than the first level (nearer the camera than the levels
 // were chosen for) reads the first level: its colour would be blurred, so
 // the tap leaves such ground per pixel, but a slope from the first level
 // is only smoother than the per-pixel one, and the pixels that ask for it
 // are single pixels along triangle edges.
 float lambda=clamp(log2(footprint*128)+terrain2CacheParams[1].x,head.z,head.w);
 uint l=uint(lambda);float w1=l<uint(head.w)?lambda-l:0;
 float w0=(1-w1)*terrain2CacheFade(l,x);w1=1-w0;
 bool held=true;
 [branch]if(w0>1.0/256)held=terrain2CacheHeightHeld(l,x);
 [branch]if(held&&w1>1.0/256)held=l<uint(head.w)&&terrain2CacheHeightHeld(l+1,x);
 return held;
}
float2 terrain2CacheHeightGradient(float2 x,float2 dx,float2 dy){
 float4 head=terrain2CacheParams[0];
 float footprint=max(max(length(dx),length(dy)),1e-8);
 float lambda=clamp(log2(footprint*128)+terrain2CacheParams[1].x,head.z,head.w);
 uint l=uint(lambda);float w1=l<uint(head.w)?lambda-l:0;
 float w0=(1-w1)*terrain2CacheFade(l,x);w1=1-w0;
 // Half a texel of the level blend to each side (a texel of level l is 1/frame.z metres).
 float e=.5*exp2(lambda-l)/terrain2CacheParams[4+4*l].z;
 return float2(terrain2CacheHeightAt(l,w0,w1,x+float2(e,0))-terrain2CacheHeightAt(l,w0,w1,x-float2(e,0)),
               terrain2CacheHeightAt(l,w0,w1,x+float2(0,e))-terrain2CacheHeightAt(l,w0,w1,x-float2(0,e)))/(2*e);
}
// Bake (renderer_terrain2_cache.cpp): one full-screen triangle per
// rectangle and map, into a fine slice or the coarse atlas; b12 per draw:
// xy the global texel of render-target pixel (0, 0), z the texel size (m).
#ifdef EID3D12
// D3D12 (nativeTerrain2CacheCS): target xy the rectangle's first target
// texel, zw its size; terrain2BakeTexel.y the fine slice (-1: the coarse atlas).
cbuffer Terrain2CacheBake:register(b12){int4 terrain2BakeOffset;float4 terrain2BakeTexel;int4 terrain2BakeTarget;};
#else
cbuffer Terrain2CacheBake:register(b12){int4 terrain2BakeOffset;float4 terrain2BakeTexel;};
#endif
float4 nativeTerrain2CacheVS(uint id:SV_VertexID):SV_Position{
 float2 p=float2((id<<1)&2,id&2);return float4(p*float2(2,-2)+float2(-1,1),0,1);
}
#ifdef NATIVE_TERRAIN_PS
// terrain2Color at the texel's world position as walkable ground draws it:
// top plane, no liquid bed, the slope normal from the map's ground grid
// (the one-ring central difference terrain_slope_normal takes, on the 1 m
// grid), the texel as the footprint. The batch constants of the map being
// baked (contact origin, layout, table entry) and its contact views are
// bound as for its terrain batches. g: the global texel. No screen
// derivative or implicit level is read on this path (terrain2Color reads
// with explicit gradients or levels), so the pixel and compute bakes agree.
// S3: alpha the roughness, h the height (always baked, whatever the level).
float4 terrain2CacheBake(int2 g,out float2 h){ // h.y: the bed flag (t2BedSensitive)
 float s=terrain2BakeTexel.x;
 float3 x=float3((float2(g)+.5)*s,0);
 float3 p=x-nativeContactOrigin.xyz;p.z=0;
 float3 pw=float3(p.xy+nativeContactLayout.xy,0);
 float2 m=p.xy;
 float3 n=normalize(float3(gcHeight(contactGround,m-float2(1,0))-gcHeight(contactGround,m+float2(1,0)),
                           gcHeight(contactGround,m-float2(0,1))-gcHeight(contactGround,m+float2(0,1)),2));
 t2Surface=true;t2BedSensitive=false;float2 surf;
 float3 c=terrain2Color(uint(nativeContactLayout.w)-1u,x,p,pw,float3(s,0,0),float3(0,s,0),n,float3(0,0,1),0,1e3,surf);
 h=float2(saturate(surf.x),t2BedSensitive?1:0);
 return float4(saturate(c),saturate(surf.y));
}
// The D3D11 bake: the render-target pixel under the full-screen triangle.
struct Terrain2CacheBakeOut{float4 color:SV_Target0;float2 height:SV_Target1;};
Terrain2CacheBakeOut nativeTerrain2CachePS(float4 position:SV_Position){
 Terrain2CacheBakeOut o;o.color=terrain2CacheBake(int2(position.xy)+terrain2BakeOffset.xy,o.height);return o;
}
#ifdef EID3D12
// The D3D12 bake: the same texels by compute, written to the fine slice or
// the coarse atlas (renderer_terrain2_cache.cpp); target texel t is the
// pixel shader's int2(position.xy).
RWTexture2DArray<unorm float4> terrain2CacheFineOut:register(u0);
RWTexture2D<unorm float4> terrain2CacheCoarseOut:register(u1);
RWTexture2DArray<unorm float2> terrain2CacheHeightFineOut:register(u2);
RWTexture2D<unorm float2> terrain2CacheHeightCoarseOut:register(u3);
[numthreads(8,8,1)]
void nativeTerrain2CacheCS(uint3 id:SV_DispatchThreadID){
 if(any(int2(id.xy)>=terrain2BakeTarget.zw))return;
 int2 t=terrain2BakeTarget.xy+int2(id.xy);
 float2 h;float4 c=terrain2CacheBake(t+terrain2BakeOffset.xy,h);
 [branch]if(terrain2BakeTexel.y>=0){uint3 f=uint3(t,uint(terrain2BakeTexel.y));terrain2CacheFineOut[f]=c;terrain2CacheHeightFineOut[f]=h;}
 else{terrain2CacheCoarseOut[uint2(t)]=c;terrain2CacheHeightCoarseOut[uint2(t)]=h;}
}
#endif
#endif
)hlsl";


// Terrain 2.0 face cache (option terrain_face_cache; terrain_face_atlas.h,
// renderer_terrain_face_cache.cpp). Faces (projection planes z < .999) of
// every map in the terrain 2.0 table that has an atlas (the active map's
// built at map load, a neighbour's on the neighbour stream) read a baked
// page of the exact terrain2Color result instead of computing it per pixel.
// The maps share one page pool and one feedback bitset; their grids, cells,
// charts and page tables are concatenated, each map's offsets in its header.
//  - At map load the steep LOD0 base triangles (any corner's slope normal
//    past the walk limit) are grouped into
//    near-planar charts, each projected on its seed triangle's plane and
//    scaled per axis so a texel spans at most 1 / D of surface. Every level
//    l (D = 128 >> l texels per metre) packs the charts into its own atlas
//    with a 2-texel gutter; a per-base-cell table names each triangle's chart.
//  - The atlas is virtual: 64 x 64 texel pages, made resident on demand in a
//    pool of 68 x 68 slots (a 2-texel page border, so the Catmull-Rom read
//    never leaves its slot); one page table per level holds slot + 1.
//  - A texel is a pure function of (level, chart, texel) and the inputs: the
//    bake rasterises the page's triangles into atlas space (the vertex grid
//    gives positions, slope normal, facet flatness and rise) and runs the
//    pixel stage's planes and terrain2Color with one texel along each atlas
//    axis as the footprint. The facet split's edge band is sized in texels
//    of the level (terrainCliffFacetKeep), a fixed surface width per level.
//    A first instance draws each triangle grown by 2.5 texels (its plane
//    extended into the gutter), the second the exact triangles over it.
//  - A pixel finds its LOD0 triangle from its plan position (the cell walk
//    and the cell's diagonal), the LOD0 surface point over that position,
//    its chart and atlas texel; it reads the two levels around its own
//    footprint in chart texels (trilinear); a level not resident, a finer
//    footprint than level 0, or a triangle without a chart runs terrain2Color.
// D3D11 t100 colour + roughness (RGBA8 array), t101 height (R8 array),
// t102 the vertex grid, t103 the cell table, t104 data, t105 page tables.
// Bake only: t106 the frame's triangle jobs.
inline constexpr unsigned kTerrainFaceCacheLevels = 8;          // 128 .. 1 texels per metre
inline constexpr int kTerrainFaceCachePage = 64, kTerrainFaceCacheBorder = 2;
inline constexpr int kTerrainFaceCacheSlot = kTerrainFaceCachePage + 2 * kTerrainFaceCacheBorder; // 68
inline constexpr int kTerrainFaceCacheRow = 30;                 // slots per slice row
inline constexpr int kTerrainFaceCacheSlice = kTerrainFaceCacheRow * kTerrainFaceCacheSlot; // 2040 texels
inline constexpr unsigned kTerrainFaceCacheSlotsPerSlice = kTerrainFaceCacheRow * kTerrainFaceCacheRow;
inline constexpr float kTerrainFaceCacheGutter = 2.f;           // atlas texels around every chart (the read reaches 2)
inline constexpr unsigned kTerrainFaceCacheMaps = 9;              // terrain 2.0 table entries (Renderer::kTerrain2TableMaps)
inline constexpr unsigned kTerrainFaceCacheMapHead = 2 + kTerrainFaceCacheLevels; // float4s per map header
inline constexpr unsigned kTerrainFaceCacheHead = 2 + kTerrainFaceCacheMaps * kTerrainFaceCacheMapHead; // data float4s before the charts
inline constexpr unsigned kTerrainFaceCacheMapShift = 28;          // triangle ids: table entry << 28 | the map's own id
static_assert(kTerrainFaceCacheHead == 92 && kTerrainFaceCacheMapHead == 10 && kTerrainFaceCacheLevels == 8 && kTerrainFaceCacheSlice == 2040,
              "kTerrainFaceCacheShader constants");
// t104 data (float4):
//  [0] x mode (0 off, 1 on, 2 tinted by level), y level bias, zw unused;
//  [1] x chart count C (all maps);
//  [2 + 10k ...] table entry k's header (y of the first float4 0: no atlas):
//    [+0] x its first vertex in t102, y grid vertices along x, z along y, w its first cell in t103;
//    [+1] xyz its origin (world = map-local + origin), w its first chart;
//    [+2 + l] x its level l page table's first entry in t105, y pages along x, z along y;
//  [kTerrainFaceCacheHead + 3c ...] chart c: (t.xyz, u0), (b.xyz, v0), (su, sv, 0, 0):
//      chart metres = ((x.t - u0) su, (x.b - v0) sv) for map-local x;
//  [kTerrainFaceCacheHead + 3C + 4c + l / 2] chart c's atlas origin of
//      level l (texels; xy even levels, zw odd), the gutter not included.
// t103 cells: per base cell (row-major over its map's cells), low 16 bits the
// map's own chart of triangle (a, b, d), high 16 bits of (a, d, c); 0xffff
// none (a, b, c, d the cell's corners (x, y), (x+1, y), (x, y+1), (x+1, y+1)).
// Triangle ids (bake jobs, feedback, check): entry << 28 | cell * 2 + half.
inline constexpr char kTerrainFaceCacheShader[] = R"hlsl(
#ifdef NATIVE_TERRAIN_PS
static const uint kTerrainFaceCacheHead=92,kTerrainFaceCacheLevels=8; // terrain2_cache.h
uint faceCacheMap(uint k){return 2+10*k;} // table entry k's header
Texture2DArray<float4> faceCacheColor:register(t100);
Texture2DArray<float> faceCacheHeight:register(t101);
StructuredBuffer<float4> faceCacheGrid:register(t102); // per base vertex: xyz map-local rest position, w rise; asuint x packed slope normal, y facet flatness
StructuredBuffer<uint> faceCacheCells:register(t103);
StructuredBuffer<float4> faceCacheData:register(t104);
StructuredBuffer<uint> faceCachePages:register(t105);
float faceCacheSide(float2 a,float2 b,float2 p){float2 e=b-a,d=p-a;return e.x*d.y-e.y*d.x;}
// The LOD0 base triangle of table entry k under map-local plan point p:
// its chart (all maps' numbering; 0xffffffff none) and q, the point of its
// plane over p.
uint faceCacheChart(uint k,float2 p,out float3 q){
 float4 g=faceCacheData[faceCacheMap(k)];uint gx=uint(g.y),v0=uint(g.x);int2 hi=int2(g.yz)-2;
 int2 c=clamp(int2(floor(p)),0,hi);
 float3 a=0,b=0,u=0,d=0;
 [loop]for(uint n=0;n<3;++n){
  uint i=v0+uint(c.y)*gx+uint(c.x);
  a=faceCacheGrid[2*i].xyz;b=faceCacheGrid[2*i+2].xyz;u=faceCacheGrid[2*(i+gx)].xyz;d=faceCacheGrid[2*(i+gx)+2].xyz;
  int2 s=int2(faceCacheSide(b.xy,d.xy,p)<0?1:faceCacheSide(u.xy,a.xy,p)<0?-1:0,
              faceCacheSide(a.xy,b.xy,p)<0?-1:faceCacheSide(d.xy,u.xy,p)<0?1:0);
  int2 n=clamp(c+s,0,hi);
  if(all(n==c))break;
  c=n;
 }
 bool first=faceCacheSide(a.xy,d.xy,p)<=0; // (a, b, d): right of the diagonal
 float3 v=first?b:d,w=first?d:u,nt=cross(v-a,w-a);
 q=float3(p,abs(nt.z)>1e-12?a.z-(nt.x*(p.x-a.x)+nt.y*(p.y-a.y))/nt.z:a.z);
 uint e=faceCacheCells[uint(g.w)+uint(c.y)*(gx-1)+uint(c.x)];
 e=first?e&0xffffu:e>>16;
 return e==0xffffu?0xffffffffu:uint(faceCacheData[faceCacheMap(k)+1].w)+e;
}
// Chart c's atlas texel coordinate of chart metres m at level l.
float2 faceCacheAtlas(uint c,uint l,float2 m){
 float4 o=faceCacheData[kTerrainFaceCacheHead+3*uint(faceCacheData[1].x)+4*c+(l>>1)];
 return m*float(128u>>l)+2+((l&1)!=0?o.zw:o.xy);
}
#if defined(NATIVE_TERRAIN_SLIM)&&defined(EID3D12)
// The slim pass's page reads (renderer_terrain_face_cache.cpp): a bit per
// page table entry, as the feedback pass's, for every page a drawn face
// pixel reads, resident or not. The quarter-resolution feedback draws the
// base triangles and missed pages the drawn pixels want (a finer level, on
// steep rock: probe 2026-11-13, 6 to 37 percent of the second pass's pixels,
// its most expensive ones); these marks are the pixels' own.
RWStructuredBuffer<uint> faceSlimBits:register(u6);
void faceSlimMark(float4 lv,float2 t1){
 uint2 pg=min(uint2(max(t1,0))/64u,uint2(lv.yz)-1u);
 uint e=uint(lv.x)+pg.y*uint(lv.y)+pg.x,w=e>>5,b=1u<<(e&31u);
 [branch]if((faceSlimBits[w]&b)==0)InterlockedOr(faceSlimBits[w],b);
}
#endif
// Level l of table entry k at chart metres m: 0 read (c colour and
// roughness, Catmull-Rom as terrain2CacheRead; h the height), 1 its page is
// not resident.
uint faceCacheRead(uint k,uint l,uint chart,float2 m,out float4 c,out float h){
 float4 lv=faceCacheData[faceCacheMap(k)+2+l];c=0;h=.5;
 float2 t=faceCacheAtlas(chart,l,m);
 float2 t1=floor(t-.5)+.5,f=t-t1;
#if defined(NATIVE_TERRAIN_SLIM)&&defined(EID3D12)
 faceSlimMark(lv,t1);
#endif
 uint2 pg=uint2(t1)/64u;
 uint slot=faceCachePages[uint(lv.x)+pg.y*uint(lv.y)+pg.x];
 if(slot==0u)return 1;
 --slot;uint r=slot%900u;float slice=float(slot/900u);
 float2 o=float2(r%30u,r/30u)*68+2-float2(pg)*64; // slice texel of atlas texel 0 (this page's frame)
 const float s=1.0/2040;
 float2 w0=f*(-.5+f*(1-.5*f)),w1=1+f*f*(-2.5+1.5*f),w2=f*(.5+f*(2-1.5*f)),w3=f*f*(-.5+.5*f);
 float2 w12=w1+w2,t12=o+t1+w2/w12,t0=o+t1-1,t3=o+t1+2;
 float4 wc=float4(w12.x*w0.y,w0.x*w12.y,w3.x*w12.y,w12.x*w3.y);float wm=w12.x*w12.y;
 c=faceCacheColor.SampleLevel(terrain2CacheSampler,float3(t12*s,slice),0)*wm+
   faceCacheColor.SampleLevel(terrain2CacheSampler,float3(float2(t12.x,t0.y)*s,slice),0)*wc.x+
   faceCacheColor.SampleLevel(terrain2CacheSampler,float3(float2(t0.x,t12.y)*s,slice),0)*wc.y+
   faceCacheColor.SampleLevel(terrain2CacheSampler,float3(float2(t3.x,t12.y)*s,slice),0)*wc.z+
   faceCacheColor.SampleLevel(terrain2CacheSampler,float3(float2(t12.x,t3.y)*s,slice),0)*wc.w;
 c=saturate(c/(wm+dot(wc,1)));
 h=faceCacheHeight.SampleLevel(terrain2CacheSampler,float3((o+t)*s,slice),0);
 return 0;
}
// The cached face colour of table entry k at map-local rest position p with screen
// derivatives dx, dy: trilinear between the two levels around the
// footprint in chart texels (the larger axis). 0 read, 1 a level it needs
// is not resident or out of range, 2 no chart there. surf: (height,
// roughness) at the same weights.
uint faceCacheTap(uint k,float3 p,float3 dx,float3 dy,inout float3 color,inout float2 surf){
 float3 q;uint chart=faceCacheChart(k,p.xy,q);
 [branch]if(chart==0xffffffffu)return 2;
 uint cb=kTerrainFaceCacheHead+3*chart;
 float4 r0=faceCacheData[cb],r1=faceCacheData[cb+1],r2=faceCacheData[cb+2];
 float2 m=float2(dot(q,r0.xyz)-r0.w,dot(q,r1.xyz)-r1.w)*r2.xy;
 float2 mx=float2(dot(dx,r0.xyz),dot(dx,r1.xyz))*r2.xy,my=float2(dot(dy,r0.xyz),dot(dy,r1.xyz))*r2.xy;
 float want=log2(max(max(length(mx),length(my))*128,1e-8))+faceCacheData[0].y;
 [branch]if(want<0||want>float(kTerrainFaceCacheLevels))return 1;
 float lambda=min(want,float(kTerrainFaceCacheLevels-1));
 uint l=uint(lambda);float w1=lambda-l;
 float4 c0=0,c1=0;float h0=.5,h1=.5;uint r=0;
 [branch]if(w1<1-1.0/256)r=faceCacheRead(k,l,chart,m,c0,h0);
#if defined(NATIVE_TERRAIN_SLIM)&&defined(EID3D12)
 // (Both levels read, so a miss of the first still marks the second.)
 [branch]if(w1>1.0/256){uint r1=faceCacheRead(k,min(l+1,kTerrainFaceCacheLevels-1),chart,m,c1,h1);if(r==0)r=r1;}
#else
 [branch]if(r==0&&w1>1.0/256)r=faceCacheRead(k,min(l+1,kTerrainFaceCacheLevels-1),chart,m,c1,h1);
#endif
 [branch]if(r!=0)return r;
 color=lerp(c0.rgb,c1.rgb,w1);
 surf=float2(lerp(h0,h1,w1),lerp(c0.a,c1.a,w1));
 [branch]if(faceCacheData[0].x>1.5){float3 tint=frac(float3(.0,.33,.67)+float(l)*.27);color=lerp(color,tint,.35);}
 return 0;
}
)hlsl" R"hlsl(
// Bake (renderer_terrain_face_cache.cpp): per page, one draw of its
// triangles (t106, from terrain2BakeOffset.x) into its slot (the viewport),
// instance 0 grown into the gutter, instance 1 exact; terrain2BakeOffset.y
// the level, zw the page (atlas pages). The batch constants of the page's
// map are bound as for its terrain batches (bake_terrain2_cache_rect).
StructuredBuffer<uint> faceCacheJobs:register(t106); // triangle: entry << 28 | cell * 2 + (0: (a, b, d), 1: (a, d, c))
StructuredBuffer<float> faceCacheDepth:register(t107); // per base vertex: the signed liquid-bed depth (active: live, neighbours: rest; 0 none)
// Triangle tri's vertices (t102 indices), its chart (all maps' numbering); returns its table entry.
uint faceCacheTriangle(uint tri,out uint3 v,out uint chart){
 uint k=tri>>28;tri&=0x0fffffffu;
 float4 g=faceCacheData[faceCacheMap(k)];uint gx=uint(g.y),cx=gx-1;
 uint cell=tri>>1,i=uint(g.x)+(cell/cx)*gx+cell%cx;
 v=(tri&1u)==0u?uint3(i,i+1,i+gx+1):uint3(i,i+gx+1,i+gx);
 uint e=faceCacheCells[uint(g.w)+cell];chart=uint(faceCacheData[faceCacheMap(k)+1].w)+((tri&1u)==0u?e&0xffffu:e>>16);
 return k;
}
float2 faceCacheAtlasOf(uint chart,uint l,float3 x){
 uint cb=kTerrainFaceCacheHead+3*chart;
 float4 r0=faceCacheData[cb],r1=faceCacheData[cb+1],r2=faceCacheData[cb+2];
 return faceCacheAtlas(chart,l,float2(dot(x,r0.xyz)-r0.w,dot(x,r1.xyz)-r1.w)*r2.xy);
}
struct FaceCacheBakeVertex{float4 p:SV_Position;float3 b:TEXCOORD0;nointerpolation uint tri:TEXCOORD1;};
FaceCacheBakeVertex nativeFaceCacheVS(uint id:SV_VertexID,uint instance:SV_InstanceID){
 FaceCacheBakeVertex o;o.tri=faceCacheJobs[uint(terrain2BakeOffset.x)+id/3u];
 uint k=id%3u,l=uint(terrain2BakeOffset.y);
 uint3 v;uint chart;faceCacheTriangle(o.tri,v,chart);
 float2 a[3]={faceCacheAtlasOf(chart,l,faceCacheGrid[2*v.x].xyz),faceCacheAtlasOf(chart,l,faceCacheGrid[2*v.y].xyz),faceCacheAtlasOf(chart,l,faceCacheGrid[2*v.z].xyz)};
 float2 p=a[k];
 float2 e1=a[1]-a[0],e2=a[2]-a[0];float det=e1.x*e2.y-e1.y*e2.x;
 [branch]if(instance==0u&&abs(det)>1e-6){
  // Both edges at this corner moved out by 2.5 texels (the gutter the read reaches, and half a texel).
  float2 f0=a[(k+1)%3]-p,f1=p-a[(k+2)%3];float sg=det<0?-1:1;
  float2 n0=normalize(float2(f0.y,-f0.x))*sg,n1=normalize(float2(f1.y,-f1.x))*sg;
  float2 off=(n0+n1)*(2.5/max(1+dot(n0,n1),.125));
  p+=off;
 }
 // p's barycentric coordinates in the triangle (extrapolated outside it).
 float2 d=p-a[0];
 float b1=(d.x*e2.y-d.y*e2.x)/det,b2=(e1.x*d.y-e1.y*d.x)/det;
 o.b=abs(det)>1e-6?float3(1-b1-b2,b1,b2):float3(k==0,k==1,k==2);
 float2 local=p-(float2(terrain2BakeOffset.zw)*64-2);
 o.p=float4(local.x/34-1,1-local.y/34,0,1);
 return o;
}
struct FaceCacheBakeOut{float4 color:SV_Target0;float height:SV_Target1;};
// One texel of level l at barycentric b of triangle tri (extended past it in the gutter).
FaceCacheBakeOut faceCacheBakeTexel(uint tri,float3 b,uint l){
 uint3 v;uint chart;faceCacheTriangle(tri,v,chart);
 float4 A=faceCacheGrid[2*v.x],B=faceCacheGrid[2*v.y],C=faceCacheGrid[2*v.z];
 uint2 a1=asuint(faceCacheGrid[2*v.x+1].xy),b1=asuint(faceCacheGrid[2*v.y+1].xy),c1=asuint(faceCacheGrid[2*v.z+1].xy);
 float4 x=b.x*A+b.y*B+b.z*C; // rest position, rise
 // One atlas texel along x and y: its barycentric and surface steps.
 float2 pa=faceCacheAtlasOf(chart,l,A.xyz),e1=faceCacheAtlasOf(chart,l,B.xyz)-pa,e2=faceCacheAtlasOf(chart,l,C.xyz)-pa;
 float det=e1.x*e2.y-e1.y*e2.x;det=abs(det)>1e-12?det:1e-12;
 float2 gx=float2(e2.y,-e1.y)/det,gy=float2(-e2.x,e1.x)/det;
 float3 bDx=float3(-gx.x-gx.y,gx),bDy=float3(-gy.x-gy.y,gy);
 float3 dx=bDx.x*A.xyz+bDx.y*B.xyz+bDx.z*C.xyz,dy=bDy.x*A.xyz+bDy.y*B.xyz+bDy.z*C.xyz;
 float3 slope=b.x*terrainSlopeNormal(a1.x)+b.y*terrainSlopeNormal(b1.x)+b.z*terrainSlopeNormal(c1.x);
 float2 flat=(b.x*float2(a1.y&255u,(a1.y>>8)&255u)+b.y*float2(b1.y&255u,(b1.y>>8)&255u)+b.z*float2(c1.y&255u,(c1.y>>8)&255u))/255.0;
 float3 n2=dot(slope,slope)>1e-8?normalize(slope):float3(0,0,1);
 float3 planes=nativeTerrainCliff.x>0?terrainCliffFacetPlanes(terrainCliffProjectionPlanes(n2,flat),dx,dy,terrainCliffFacetKeep(b,bDx,bDy)):float3(0,0,1);
 [branch]if(T2_LEAN&&all(planes>.002)){float lo=min(planes.x,min(planes.y,planes.z));planes=max(planes-lo,0);planes/=planes.x+planes.y+planes.z;}
 float3 p=x.xyz,pw=float3(p.xy+nativeContactLayout.xy,p.z);
 t2Surface=true;float2 surf;
 float bed=b.x*faceCacheDepth[v.x]+b.y*faceCacheDepth[v.y]+b.z*faceCacheDepth[v.z];
 float3 col=terrain2Color(uint(nativeContactLayout.w)-1u,p+nativeContactOrigin.xyz,p,pw,dx,dy,n2,planes,bed,x.w,surf);
 FaceCacheBakeOut o;o.color=float4(saturate(col),saturate(surf.y));o.height=saturate(surf.x);
 return o;
}
FaceCacheBakeOut nativeFaceCachePS(FaceCacheBakeVertex i){return faceCacheBakeTexel(i.tri,i.b,uint(terrain2BakeOffset.y));}
#ifdef EID3D12
// The D3D12 bake (renderer_terrain_face_cache.cpp): the same texels by
// compute, the pixel bake's rasterisation done per texel. Each of the slot's
// 68 x 68 texels takes the page's triangle holding its centre, else the
// nearest one whose edges it lies within 2.5 texels of (the grown first
// instance), the barycentric extended past it. terrain2BakeTarget: the
// slot's first texel (xy), its slice, the page's triangle count.
RWTexture2DArray<unorm float4> faceCacheColorOut:register(u0);
RWTexture2DArray<unorm float> faceCacheHeightOut:register(u1);
[numthreads(8,8,1)]
void nativeFaceCacheCS(uint3 id:SV_DispatchThreadID){
 if(any(id.xy>=68u))return;
 uint l=uint(terrain2BakeOffset.y);
 float2 p=float2(terrain2BakeOffset.zw)*64-2+float2(id.xy)+.5; // atlas texel centre
 uint best=0xffffffffu;float bestOut=2.5;float3 bestB=0;
 [loop]for(uint k=0;k<uint(terrain2BakeTarget.w);++k){
  uint tri=faceCacheJobs[uint(terrain2BakeOffset.x)+k];
  uint3 v;uint chart;faceCacheTriangle(tri,v,chart);
  float2 a0=faceCacheAtlasOf(chart,l,faceCacheGrid[2*v.x].xyz),a1=faceCacheAtlasOf(chart,l,faceCacheGrid[2*v.y].xyz),a2=faceCacheAtlasOf(chart,l,faceCacheGrid[2*v.z].xyz);
  float2 e1=a1-a0,e2=a2-a0;float det=e1.x*e2.y-e1.y*e2.x;
  [branch]if(abs(det)<=1e-6)continue;
  float2 d=p-a0;float b1=(d.x*e2.y-d.y*e2.x)/det,b2=(e1.x*d.y-e1.y*d.x)/det;float3 b=float3(1-b1-b2,b1,b2);
  // Texels outside each edge: b_i over its gradient's length (opposite edge / |det|).
  float3 g=float3(length(a2-a1),length(a2-a0),length(a1-a0))/abs(det);
  float o=max(max(-b.x/g.x,-b.y/g.y),-b.z/g.z);
  [branch]if(o<=0){best=tri;bestB=b;break;}
  [branch]if(o<bestOut){bestOut=o;best=tri;bestB=b;}
 }
 [branch]if(best==0xffffffffu)return;
 FaceCacheBakeOut r=faceCacheBakeTexel(best,bestB,l);
 uint3 t=uint3(uint2(terrain2BakeTarget.xy)+id.xy,uint(terrain2BakeTarget.z));
 faceCacheColorOut[t]=r.color;faceCacheHeightOut[t]=r.height;
}
#endif
#ifdef NATIVE_FACE_CHECK
// The baked/live check (option terrain_face_cache_check; renderer_terrain_face_cache.cpp):
// the charted triangles of the view drawn with the frame's camera (b12
// terrain2BakeTexel: none; the world-to-clip matrix in faceCheckClip), each
// pixel running the face path as the terrain pixel stage does (its own
// screen derivatives, the facet split in output pixels) and the cache read
// at the same footprint. Counters (u1): [0] pixels compared, [1] misses,
// [2] any channel off by more than 2/255, [3] by more than 8/255, [4] faces
// the projection keeps on the top plane, [5] cached reads near black.
// Output: |baked - live| x 8 (3: the cached colour, 4: the live one), misses blue.
// With the occlusion pyramid (faceCheckMode.y; t108, made with faceCheckHiZClip)
// pixels behind it count as hidden [6] and nothing else (the counts are then
// of visible face pixels, as the feedback pass sees them).
cbuffer FaceCheck:register(b13){row_major float4x4 faceCheckClip;float4 faceCheckMode;row_major float4x4 faceCheckHiZClip;float4 faceCheckEye;}; // mode x: 3 the image shows the cached colour, 4 the live colour; y 1 with the pyramid
RWStructuredBuffer<uint> faceCheckCounters:register(u1);
Texture2D<float> faceCheckHiZ:register(t108);
struct FaceCheckVertex{float4 p:SV_Position;float3 rest:TEXCOORD0;float3 slope:TEXCOORD1;float4 flat:TEXCOORD2;float3 b:TEXCOORD3;nointerpolation uint map:TEXCOORD4;};
FaceCheckVertex nativeFaceCheckVS(uint id:SV_VertexID){
 FaceCheckVertex o;uint tri=faceCacheJobs[id/3u],k=id%3u;
 uint3 v;uint chart;o.map=faceCacheTriangle(tri,v,chart);
 uint at=k==0u?v.x:k==1u?v.y:v.z;
 float4 g=faceCacheGrid[2*at];uint2 a=asuint(faceCacheGrid[2*at+1].xy);
 o.rest=g.xyz;o.slope=terrainSlopeNormal(a.x);o.flat=float4(float2(a.y&255u,(a.y>>8)&255u)/255.0,g.w,faceCacheDepth[at]);
 o.b=float3(k==0u,k==1u,k==2u);
 o.p=mul(faceCheckClip,float4(g.xyz+faceCacheData[faceCacheMap(o.map)+1].xyz,1));
 return o;
}
[earlydepthstencil]
float4 nativeFaceCheckPS(FaceCheckVertex i):SV_Target{
 float3 restDx=ddx(i.rest),restDy=ddy(i.rest);
 float3 n2=dot(i.slope,i.slope)>1e-8?normalize(i.slope):float3(0,0,1);
 float3 planes=nativeTerrainCliff.x>0?terrainCliffFacetPlanes(terrainCliffProjectionPlanes(n2,i.flat.xy),restDx,restDy,terrainCliffFacetKeep(i.b,ddx(i.b),ddy(i.b))):float3(0,0,1);
 [branch]if(T2_LEAN&&all(planes>.002)){float lo=min(planes.x,min(planes.y,planes.z));planes=max(planes-lo,0);planes/=planes.x+planes.y+planes.z;}
 [branch]if(faceCheckMode.y>0){
  float3 w=i.rest+faceCacheData[faceCacheMap(i.map)+1].xyz,e=faceCheckEye.xyz-w;float d=length(e);
  w+=e*min((.3+.03*d)/max(d,1e-3),.9);
  float4 c=mul(faceCheckHiZClip,float4(w,1));
  [branch]if(c.w>1e-4){
   float2 uv=float2(c.x,-c.y)/c.w*.5+.5;uint2 size;faceCheckHiZ.GetDimensions(size.x,size.y);
   [branch]if(all(uv>=0)&&all(uv<1)&&c.z/c.w>faceCheckHiZ.Load(int3(min(uint2(uv*float2(size)),size-1u),0))){InterlockedAdd(faceCheckCounters[6],1u);return float4(0,0,0,1);}
  }
 }
 [branch]if(planes.z>=.999){InterlockedAdd(faceCheckCounters[4],1u);return float4(0,0,0,1);}
 float3 p=i.rest,pw=float3(p.xy+nativeContactLayout.xy,p.z);
 t2Surface=true;float2 surf,cachedSurf=0;float3 cached=0;
 float3 live=saturate(terrain2Color(uint(nativeContactLayout.w)-1u,p+nativeContactOrigin.xyz,p,pw,restDx,restDy,n2,planes,i.flat.w,i.flat.z,surf));
 [branch]if(faceCacheTap(i.map,p,restDx,restDy,cached,cachedSurf)!=0u){InterlockedAdd(faceCheckCounters[1],1u);return float4(0,0,1,1);}
 float3 d=abs(cached-live);float m=max(d.x,max(d.y,d.z));
 InterlockedAdd(faceCheckCounters[0],1u);
 if(max(cached.x,max(cached.y,cached.z))<4.0/255)InterlockedAdd(faceCheckCounters[5],1u); // read black (an unbaked slot)
 if(m>2.5/255)InterlockedAdd(faceCheckCounters[2],1u);
 if(m>8.5/255)InterlockedAdd(faceCheckCounters[3],1u);
 return faceCheckMode.x>3.5?float4(live,1):faceCheckMode.x>2.5?float4(cached,1):float4(saturate(d*8),1);
}
#endif
)hlsl" R"hlsl(
#ifdef NATIVE_FACE_FEEDBACK
// Page feedback (renderer_terrain_face_cache.cpp): the charted triangles of
// the patches in the frustum drawn with the frame's camera at a quarter of
// the output resolution (jittered over 16 frames) after a depth-only draw of
// the same triangles; each visible pixel marks the pages faceCacheTap reads
// there (the same level choice, its derivatives scaled back to output
// pixels) in a bit per page table entry (u1), read back a few frames later.
// Pixels behind the last occlusion pyramid (t108, made with
// faceFeedbackHiZClip) mark nothing.
cbuffer FaceFeedback:register(b13){row_major float4x4 faceFeedbackClip;row_major float4x4 faceFeedbackHiZClip;float4 faceFeedbackParams;float4 faceFeedbackEye;};
// params: x output pixels per feedback pixel (inverse), y 1 with the pyramid, zw the NDC jitter; eye: xyz world eye
Texture2D<float> faceFeedbackHiZ:register(t108);
StructuredBuffer<uint2> faceFeedbackPatches:register(t109); // per patch in view: its first entry in t110, its triangle count (<= 32)
StructuredBuffer<uint> faceFeedbackTriangles:register(t110); // the atlas's charted triangles, by chart
RWStructuredBuffer<uint> faceFeedbackBits:register(u1);
struct FaceFeedbackVertex{float4 p:SV_Position;float3 rest:TEXCOORD0;nointerpolation uint2 chart:TEXCOORD1;}; // chart, its table entry
FaceFeedbackVertex nativeFaceFeedbackVS(uint id:SV_VertexID){
 FaceFeedbackVertex o;o.p=0;o.rest=0;o.chart=0;
 uint2 range=faceFeedbackPatches[id/96u];uint k=(id/3u)%32u;
 [branch]if(k>=range.y)return o; // past the patch's triangles: degenerate
 uint3 v;uint chart;uint map=faceCacheTriangle(faceFeedbackTriangles[range.x+k],v,chart);
 uint c=id%3u,at=c==0u?v.x:c==1u?v.y:v.z;
 o.rest=faceCacheGrid[2*at].xyz;o.chart=uint2(chart,map);
 o.p=mul(faceFeedbackClip,float4(o.rest+faceCacheData[faceCacheMap(map)+1].xyz,1));
 o.p.xy+=faceFeedbackParams.zw*o.p.w;
 return o;
}
void faceFeedbackMark(uint k,uint l,uint chart,float2 m){
 float4 lv=faceCacheData[faceCacheMap(k)+2+l];
 float2 t1=floor(faceCacheAtlas(chart,l,m)-.5)+.5;
 uint2 pg=min(uint2(max(t1,0))/64u,uint2(lv.yz)-1u);
 uint e=uint(lv.x)+pg.y*uint(lv.y)+pg.x;
 InterlockedOr(faceFeedbackBits[e>>5],1u<<(e&31u));
}
[earlydepthstencil]
void nativeFaceFeedbackPS(FaceFeedbackVertex i){
 float3 dx=ddx(i.rest)*faceFeedbackParams.x,dy=ddy(i.rest)*faceFeedbackParams.x; // before any return
 [branch]if(faceFeedbackParams.y>0){
  // Moved towards the eye by the slack of the terrain's LOD and displacement.
  float3 w=i.rest+faceCacheData[faceCacheMap(i.chart.y)+1].xyz,e=faceFeedbackEye.xyz-w;float d=length(e);
  w+=e*min((.3+.03*d)/max(d,1e-3),.9);
  float4 c=mul(faceFeedbackHiZClip,float4(w,1));
  [branch]if(c.w>1e-4){
   float2 uv=float2(c.x,-c.y)/c.w*.5+.5;uint2 size;faceFeedbackHiZ.GetDimensions(size.x,size.y);
   [branch]if(all(uv>=0)&&all(uv<1)&&c.z/c.w>faceFeedbackHiZ.Load(int3(min(uint2(uv*float2(size)),size-1u),0)))return;
  }
 }
 uint cb=kTerrainFaceCacheHead+3*i.chart.x;
 float4 r0=faceCacheData[cb],r1=faceCacheData[cb+1],r2=faceCacheData[cb+2];
 float2 m=float2(dot(i.rest,r0.xyz)-r0.w,dot(i.rest,r1.xyz)-r1.w)*r2.xy;
 float2 mx=float2(dot(dx,r0.xyz),dot(dx,r1.xyz))*r2.xy,my=float2(dot(dy,r0.xyz),dot(dy,r1.xyz))*r2.xy;
 float want=log2(max(max(length(mx),length(my))*128,1e-8))+faceCacheData[0].y;
 [branch]if(want<0||want>float(kTerrainFaceCacheLevels))return;
 float lambda=min(want,float(kTerrainFaceCacheLevels-1));
 uint l=uint(lambda);float w1=lambda-l;
 if(w1<1-1.0/256)faceFeedbackMark(i.chart.y,l,i.chart.x,m);
 if(w1>1.0/256)faceFeedbackMark(i.chart.y,min(l+1,kTerrainFaceCacheLevels-1),i.chart.x,m);
}
#endif
#endif
)hlsl";

} // namespace owned
