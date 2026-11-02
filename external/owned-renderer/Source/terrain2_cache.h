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
//  [1] x level bias, y edge band (texels), z filter margin (texels);
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
// packed in, terrain2SurfacePack); an R8 layer of the same slices and atlas
// holds the blended material height (both backends at t78/t79).
Texture2DArray<float> terrain2CacheHeightFine:register(t78);
Texture2D<float> terrain2CacheHeightCoarse:register(t79);
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
// Level l at world xy, false outside its valid rectangle less the filter
// margin. c: colour and roughness, Catmull-Rom (five bilinear taps): a texel
// already holds the colour filtered over its own footprint, and a bilinear
// read of it blurred it a second time, so a cached pixel read about a texel
// softer than the per-pixel path at the same level (save1165, F11
// frame_031732/031927; bias -1 A/B). The spline interpolates the texels
// instead. h (with surface, S3): the height, one bilinear read.
bool terrain2CacheRead(uint l,float2 x,bool surface,out float4 c,out float h){
 float4 valid=terrain2CacheParams[2+4*l],window=terrain2CacheParams[3+4*l],frame=terrain2CacheParams[4+4*l];
 float2 t=x*frame.z-frame.xy;c=0;h=.5;
 float m=terrain2CacheParams[1].z;
 if(any(t<valid.xy+m)||any(t>valid.zw-m))return false;
 float2 t1=floor(t-.5)+.5,f=t-t1;
 float2 w0=f*(-.5+f*(1-.5*f)),w1=1+f*f*(-2.5+1.5*f),w2=f*(.5+f*(2-1.5*f)),w3=f*f*(-.5+.5*f);
 float2 w12=w1+w2,t12=t1+w2/w12,t0=t1-1,t3=t1+2;
 float4 wc=float4(w12.x*w0.y,w0.x*w12.y,w3.x*w12.y,w12.x*w3.y);float wm=w12.x*w12.y;
 c=terrain2CacheBilinear(l,t12)*wm+terrain2CacheBilinear(l,float2(t12.x,t0.y))*wc.x+terrain2CacheBilinear(l,float2(t0.x,t12.y))*wc.y+
   terrain2CacheBilinear(l,float2(t3.x,t12.y))*wc.z+terrain2CacheBilinear(l,float2(t12.x,t3.y))*wc.w;
 c=saturate(c/(wm+dot(wc,1)));
 [branch]if(surface){
  [branch]if(frame.w>=0)h=terrain2CacheHeightFine.SampleLevel(terrain2CacheSampler,float3(t/terrain2CacheParams[0].y,frame.w),0);
  else{float4 p=terrain2CacheParams[5+4*l];
   h=terrain2CacheHeightCoarse.SampleLevel(terrain2CacheSampler,(p.xy+clamp(t-window.xy,.5,window.zw-window.xy-.5))*p.zw,0);}
 }
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
bool terrain2CacheTap(float2 x,float2 dx,float2 dy,bool surface,inout float3 color,inout float2 surf){
 float4 head=terrain2CacheParams[0];
 float first=head.z,last=head.w;
 float want=log2(max(max(length(dx),length(dy))*128,1e-8))+terrain2CacheParams[1].x;
 [branch]if(want<first)return false;
 float lambda=min(want,last);
 uint l=uint(lambda);float w1=l<uint(last)?lambda-l:0;
 float w0=(1-w1)*terrain2CacheFade(l,x);w1=1-w0;
 float4 c0=0,c1=0;float h0=.5,h1=.5;
 [branch]if(w0>1.0/256&&!terrain2CacheRead(l,x,surface,c0,h0))return false;
 [branch]if(w1>1.0/256){if(l>=uint(last)||!terrain2CacheRead(l+1,x,surface,c1,h1))return false;}
 color=c0.rgb*w0+c1.rgb*w1;
 surf=float2(h0*w0+h1*w1,c0.a*w0+c1.a*w1);
 [branch]if(head.x>1.5){ // terrain2_cache 2: tinted by level (the finer of the two)
  float3 tint=frac(float3(.0,.33,.67)+float(l)*.27);color=lerp(color,tint,.35);
 }
 return true;
}
bool terrain2CacheTap(float2 x,float2 dx,float2 dy,inout float3 color){float2 surf=0;return terrain2CacheTap(x,dx,dy,false,color,surf);}
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
float4 terrain2CacheBake(int2 g,out float h){
 float s=terrain2BakeTexel.x;
 float3 x=float3((float2(g)+.5)*s,0);
 float3 p=x-nativeContactOrigin.xyz;p.z=0;
 float3 pw=float3(p.xy+nativeContactLayout.xy,0);
 float2 m=p.xy;
 float3 n=normalize(float3(gcHeight(contactGround,m-float2(1,0))-gcHeight(contactGround,m+float2(1,0)),
                           gcHeight(contactGround,m-float2(0,1))-gcHeight(contactGround,m+float2(0,1)),2));
 t2Surface=true;float2 surf;
 float3 c=terrain2Color(uint(nativeContactLayout.w)-1u,x,p,pw,float3(s,0,0),float3(0,s,0),n,float3(0,0,1),0,1e3,surf);
 h=saturate(surf.x);
 return float4(saturate(c),saturate(surf.y));
}
// The D3D11 bake: the render-target pixel under the full-screen triangle.
struct Terrain2CacheBakeOut{float4 color:SV_Target0;float height:SV_Target1;};
Terrain2CacheBakeOut nativeTerrain2CachePS(float4 position:SV_Position){
 Terrain2CacheBakeOut o;o.color=terrain2CacheBake(int2(position.xy)+terrain2BakeOffset.xy,o.height);return o;
}
#ifdef EID3D12
// The D3D12 bake: the same texels by compute, written to the fine slice or
// the coarse atlas (renderer_terrain2_cache.cpp); target texel t is the
// pixel shader's int2(position.xy).
RWTexture2DArray<unorm float4> terrain2CacheFineOut:register(u0);
RWTexture2D<unorm float4> terrain2CacheCoarseOut:register(u1);
RWTexture2DArray<unorm float> terrain2CacheHeightFineOut:register(u2);
RWTexture2D<unorm float> terrain2CacheHeightCoarseOut:register(u3);
[numthreads(8,8,1)]
void nativeTerrain2CacheCS(uint3 id:SV_DispatchThreadID){
 if(any(int2(id.xy)>=terrain2BakeTarget.zw))return;
 int2 t=terrain2BakeTarget.xy+int2(id.xy);
 float h;float4 c=terrain2CacheBake(t+terrain2BakeOffset.xy,h);
 [branch]if(terrain2BakeTexel.y>=0){uint3 f=uint3(t,uint(terrain2BakeTexel.y));terrain2CacheFineOut[f]=c;terrain2CacheHeightFineOut[f]=h;}
 else{terrain2CacheCoarseOut[uint2(t)]=c;terrain2CacheHeightCoarseOut[uint2(t)]=h;}
}
#endif
#endif
)hlsl";

} // namespace owned
