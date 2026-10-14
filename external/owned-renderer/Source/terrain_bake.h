#pragma once

#include <cstdint>
#include <vector>

namespace owned {

// Terrain material cache (issue #148, terrain_bake). The visible terrain of
// the active map, from the eye outward, kept in pages baked from the live
// terrain material and drawn through the lean baked variant where they cover
// (terrain_bake 1/3), or re-baked continuously without use (terrain_bake 2,
// the bake's cost at the per-frame budget).
//
// The cache holds the finished material colour of opaque base tiles (vertex
// flag 16): tile blend, terrain material and cliffs, before snow
// trails and lighting. It is addressed by the rest (untessellated) world XY of
// the active map, a heightfield, so every surface point has one address.
//
// Wanted pages: square nodes of a quadtree over the map's 8 m cells (8 to
// 64 m) inside the view frustum and within reach. A page's density follows
// the screen: min(terrain_bake_density, pixels per metre at its nearest view
// depth), the finest the screen's minor footprint axis can show, times its
// stretch on each axis (sqrt(1 + slope^2) to the nearest power of two; under
// 1.41 none), never past the cap on any axis. A node splits while its larger
// side would exceed terrain_bake_page_texels. Each page's slot is a power of
// two per axis (32 to 1024 texels, the nearest in log scale to its wanted
// size), so slots come from a buddy allocator and are reused; its gutter is
// baked from the real neighbouring surface (up to 8 texels, never more than
// terrain_bake_margin metres).
//
// Residency: each frame the nearest wanted pages without a usable resident
// page bake, within a texel budget that follows terrain_bake_frame_ms of
// measured GPU time (capped by terrain_bake_frame_texels), into a small
// staging atlas;
// the next frame compresses them (BC7 near, BC1 beyond terrain_bake_bc1
// metres; RGBA8 copies with terrain_bake_near_format 0) into their slots in
// the near (t126) and far (t124) atlases and they enter the page table.
// Pages no longer wanted stay until their slot is needed (least recently
// wanted first). A cell without a usable page keeps the live material; a
// resident page serves only while at least 0.7x the wanted density, so
// catching up never shows a blurred page. The bake writes alpha 1 where it
// drew: texels it never reached (off-screen or culled ground, other tiles)
// stay alpha 0 and those pixels keep the live material too.
inline constexpr unsigned terrain_bake_cell_tiles = 4;      // 8 m cells (page table)
inline constexpr float terrain_bake_cell_metres = 8.f;
inline constexpr unsigned terrain_bake_top_cells = 8;       // 64 m quadtree roots
inline constexpr float terrain_bake_margin = .5f;           // m a gutter may reach
inline constexpr unsigned terrain_bake_max_gutter = 8;      // texels
inline constexpr unsigned terrain_bake_mips = 4;            // a gutter of 8 covers mips 0-3
inline constexpr unsigned terrain_bake_page_texels = 512;   // largest wanted page side before a split
inline constexpr unsigned terrain_bake_min_slot = 32;       // slot sides: powers of two, 32..1024
inline constexpr unsigned terrain_bake_max_slot = 1024;
inline constexpr float terrain_bake_max_stretch = 16.f;
inline constexpr unsigned terrain_bake_staging_side = 2048; // staging atlas (RGBA8, mips)
inline constexpr unsigned terrain_bake_atlas_width = 8192;
inline constexpr unsigned terrain_bake_atlas_max_height = 16384; // D3D11 limit
inline constexpr float terrain_bake_far_flag = 16384.f;     // added to a far page's slot x

// b9: the page table's cell counts (zw); the staging atlas (1 / size, sample
// switch); sampling (1 / near width and height, 1 / far width and height).
// The page tables hold two float4 per 8 m cell: a page's inner origin
// (texels; far atlas pages carry terrain_bake_far_flag in x) and density per
// axis (texels per metre; 0: no page), then its world origin, side (m) and
// gutter (texels). t125: the pages drawn from; t123: the pages this frame
// bakes, at their staging origins.
struct TerrainBakeValues {
    float window[4];
    float atlas[4];
    float sample[4];
};
static_assert(sizeof(TerrainBakeValues) % 16 == 0, "constant buffer layout");

// Per map: each 8 m cell's height range and steepest 1 m rise per axis (one
// vertex of margin), from the map's 1 m height grid. Built on a worker at
// map load (Renderer::start_terrain_bake_cells), never on the render thread.
struct TerrainBakeCells {
    int cells_x = 0, cells_y = 0;
    std::vector<float> low, high, rise_x, rise_y;
};
TerrainBakeCells terrain_bake_cells(const std::vector<float>& height, std::uint32_t tiles_x, std::uint32_t tiles_y);

// Nearest power of two to s in log scale (1 below 1.41), clamped to
// [1, terrain_bake_max_stretch].
inline float terrain_bake_stretch_level(float s) noexcept {
    float level = 1.f;
    while (level * 1.41421356f < s && level < terrain_bake_max_stretch) level *= 2.f;
    return level;
}

inline constexpr char kTerrainBakeShader[] = R"hlsl(
// Terrain material cache (terrain_bake.h).
cbuffer TerrainBakeValues:register(b9){float4 terrainBakeWindow;float4 terrainBakeAtlas;float4 terrainBakeSample;};
// b13, per draw: x the candidate cell corner (bake vertex stages) or 1 while
// the batch is routed (pixel stage: its cached pixels already drawn by the
// baked variant); y the batch's map slice's first table entry, zw its cells;
// terrainBakeMap.xy its world origin (0 on the active map).
cbuffer TerrainBakeCorner:register(b13){uint4 terrainBakeCorner;float4 terrainBakeMap;};
StructuredBuffer<float4> terrainBakeBakePages:register(t123);
StructuredBuffer<float4> terrainBakePages:register(t125);
Texture2D<float4> terrainBakeCache:register(t126);
Texture2D<float4> terrainBakeFar:register(t124);
SamplerState terrainBakeSampler:register(s11);
static const float terrainBakeCellMetres=8;
static const float terrainBakeMargin=.5;
static const float terrainBakeFarFlag=16384;
// block: inner origin (texels), density per axis; origin: world XY; side (m);
// gutter (texels).
struct TerrainBakePage{float4 block;float2 origin;float side;float gutter;bool valid;};
// k: a cell of the draw's map slice; bake: the table of the pages baking
// this frame (staging origins). Page origins are world positions.
TerrainBakePage terrainBakeCell(int2 k,bool bake){
 TerrainBakePage page;page.block=0;page.origin=0;page.side=0;page.gutter=0;page.valid=false;
 if(any(k<0)||any(k>=int2(terrainBakeCorner.zw)))return page;
 uint index=terrainBakeCorner.y+2u*uint(k.y*int(terrainBakeCorner.z)+k.x);
 float4 placement;
 [branch]if(bake){page.block=terrainBakeBakePages[index];placement=terrainBakeBakePages[index+1u];}
 else{page.block=terrainBakePages[index];placement=terrainBakePages[index+1u];}
 page.origin=placement.xy;page.side=placement.z;page.gutter=placement.w;page.valid=page.block.z>0;
 return page;
}
bool terrainBakeOverlaps(TerrainBakePage page,float2 lo,float2 hi){
 float2 g=page.gutter/page.block.zw;
 return all(hi>=page.origin-g)&&all(lo<=page.origin+page.side+g);
}
// The page this bake draw writes a rest triangle (a, b, c) into: the baking
// page of one of the 2x2 cells from the triangle's low corner (less the
// margin a gutter can reach), when the triangle overlaps that page with its
// gutter and no lower corner of the four already names the same page. Every
// vertex and patch stage of the triangle computes the same answer.
TerrainBakePage terrainBakePageOf(float2 a,float2 b,float2 c){
 float2 lo=min(a,min(b,c)),hi=max(a,max(b,c));
 int2 base=int2(floor((lo-terrainBakeMap.xy-terrainBakeMargin)/terrainBakeCellMetres));
 uint corner=terrainBakeCorner.x;
 TerrainBakePage page=terrainBakeCell(base+int2(corner&1u,corner>>1),true);
 if(!page.valid||!terrainBakeOverlaps(page,lo,hi)){page.valid=false;return page;}
 for(uint earlier=0;earlier<corner;++earlier){
  TerrainBakePage other=terrainBakeCell(base+int2(earlier&1u,earlier>>1),true);
  if(other.valid&&all(other.origin==page.origin)&&other.side==page.side&&terrainBakeOverlaps(other,lo,hi))page.valid=false;
 }
 return page;
}
// Clip position of rest point p in the staging atlas, and its distances
// (texels) to the page's rectangle with gutter: nothing is drawn into a
// neighbouring page.
float4 terrainBakeClip(TerrainBakePage page,float2 p,out float4 clip){
 float2 t=page.block.xy+(p-page.origin)*page.block.zw;
 float2 lo=page.block.xy-page.gutter,hi=page.block.xy+page.side*page.block.zw+page.gutter;
 clip=float4(t-lo,hi-t);
 return float4(t.x*2*terrainBakeAtlas.x-1,1-t.y*2*terrainBakeAtlas.y,.5,1);
}
// The cached material colour at rest point p (derivatives dx, dy): false
// where the cell has no page, the footprint leaves the mips its gutter holds,
// or any filtered texel was never baked (alpha below 1). The baked variant
// draws a pixel on the loose test; the live variant skips it only on the
// strict one, which implies the loose one with margin, so the two programs'
// rounding can never leave a pixel to neither (a pixel both draw keeps either
// colour).
// The cache also gives way where it is coarser than the screen: a pixel whose
// footprint is smaller than the page's texel (the tap's mip below 0,
// magnified) keeps the live material, so close ground is never softer than
// live. The hand-over is dithered per screen pixel (pixel: SV_Position xy;
// 4x4 ordered threshold) between a magnification of 1.5 and 1.15 texels per
// pixel, so no line shows where cached ground meets live ground.
bool terrainBakeTap(float2 p,float2 dx,float2 dy,float2 pixel,inout float3 rgb,bool strict){
 TerrainBakePage page=terrainBakeCell(int2(floor((p-terrainBakeMap.xy)/terrainBakeCellMetres)),false);
 if(!page.valid)return false;
 bool far=page.block.x>=terrainBakeFarFlag;
 float2 t=float2(page.block.x-(far?terrainBakeFarFlag:0),page.block.y)+(p-page.origin)*page.block.zw;
 float2 gx=dx*page.block.zw,gy=dy*page.block.zw;
 float a=length(gx),b=length(gy),major=max(a,b),minor=min(a,b);
 float lastMip=min(floor(log2(max(page.gutter,1))),3);
 float lod=log2(max(max(minor,major/8),1e-6));
 if(lod>lastMip-(strict?.1:0))return false;
 uint2 q=uint2(pixel)&3u;
 static const float terrainBakeBayer[16]={0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};
 float handover=-.6+.4*(terrainBakeBayer[q.y*4+q.x]+.5)/16;
 if(lod<handover+(strict?.05:0))return false;
 float4 c;
 [branch]if(far){float2 scale=terrainBakeSample.zw;c=terrainBakeFar.SampleGrad(terrainBakeSampler,t*scale,gx*scale,gy*scale);}
 else{float2 scale=terrainBakeSample.xy;c=terrainBakeCache.SampleGrad(terrainBakeSampler,t*scale,gx*scale,gy*scale);}
 if(c.a<(strict?.9995:.999))return false;
 rgb=terrainBakeAtlas.z>1.5?c.rgb*float3(1,.55,1):c.rgb; // terrain_bake 3: cached pixels tinted
 return true;
}
// A rest triangle with a vertex in a cell with a page: the baked variant
// draws it (pixels whose tap fails are discarded there and drawn by the live
// pass).
bool terrainBakeCandidate(float2 a,float2 b,float2 c){
 float2 o=terrainBakeMap.xy;
 return terrainBakeCell(int2(floor((a-o)/terrainBakeCellMetres)),false).valid||terrainBakeCell(int2(floor((b-o)/terrainBakeCellMetres)),false).valid||
        terrainBakeCell(int2(floor((c-o)/terrainBakeCellMetres)),false).valid;
}
)hlsl";

// BC1 compression of the staging atlas (far pages), one 4x4 block per
// thread: the colour bounding box (inset by 1/16) as end points, indices by
// projection onto it. A block with any unbaked texel (alpha under .5) uses
// the three-colour mode, whose fourth index is transparent, so the cache's
// coverage survives compression.
inline constexpr char kTerrainBakeBc1Shader[] = R"hlsl(
Texture2D<float4> bakeAtlas:register(t0);
RWTexture2D<uint2> bakeBlocks:register(u0);
cbuffer TerrainBakeBc1:register(b0){uint2 bc1Offset;uint2 bc1Blocks;uint bc1Mip;uint3 bc1Padding;};
uint bc1Pack(float3 c){uint3 q=uint3(round(saturate(c)*float3(31,63,31)));return (q.x<<11)|(q.y<<5)|q.z;}
float3 bc1Unpack(uint v){return float3((v>>11)&31u,(v>>5)&63u,v&31u)/float3(31,63,31);}
[numthreads(8,8,1)]void terrainBakeBc1CS(uint3 id:SV_DispatchThreadID){
 if(any(id.xy>=bc1Blocks))return;
 float4 texel[16];float3 lo=1,hi=0;bool transparent=false,opaque=false;
 [unroll]for(uint k=0;k<16;++k){
  texel[k]=bakeAtlas.Load(int3(bc1Offset+id.xy*4+uint2(k&3u,k>>2),bc1Mip));
  if(texel[k].a<.5)transparent=true;else{lo=min(lo,texel[k].rgb);hi=max(hi,texel[k].rgb);opaque=true;}
 }
 if(!opaque){bakeBlocks[id.xy]=uint2(0,0xffffffffu);return;}
 float3 inset=(hi-lo)/16;lo=saturate(lo+inset);hi=saturate(hi-inset);
 uint a=bc1Pack(hi),b=bc1Pack(lo);
 float3 ca=bc1Unpack(a),cb=bc1Unpack(b),axis=ca-cb;float span=max(dot(axis,axis),1e-8);
 uint indices=0;
 if(transparent){
  // Three-colour mode needs colour0 <= colour1: 0 colour0, 1 colour1, 2 their mean, 3 transparent.
  uint c0=min(a,b),c1=max(a,b);float3 p0=bc1Unpack(c0),d=bc1Unpack(c1)-p0;float dd=max(dot(d,d),1e-8);
  [unroll]for(uint k=0;k<16;++k){
   uint index=3;
   if(texel[k].a>=.5){float t=dot(texel[k].rgb-p0,d)/dd;index=t<.25?0u:(t>.75?1u:2u);}
   indices|=index<<(2*k);
  }
  bakeBlocks[id.xy]=uint2(c0|(c1<<16),indices);return;
 }
 if(a==b){bakeBlocks[id.xy]=uint2(a|(b<<16),0);return;}
 // Four-colour mode (colour0 > colour1): 0 hi, 1 lo, 2 = 2/3 hi + 1/3 lo, 3 = 1/3 hi + 2/3 lo.
 bool swap=a<b;
 [unroll]for(uint k=0;k<16;++k){
  float t=dot(texel[k].rgb-cb,axis)/span;
  uint index=t>=5.0/6?0u:(t>=.5?2u:(t>=1.0/6?3u:1u));
  if(swap)index^=1u;
  indices|=index<<(2*k);
 }
 bakeBlocks[id.xy]=swap?uint2(b|(a<<16),indices):uint2(a|(b<<16),indices);
}
)hlsl";

// BC7 compression (mode 6: one subset, RGBA end points of 7 bits plus a
// p-bit, 4-bit indices shared by colour and alpha) of the staging atlas, one 4x4
// block per thread: the colour bounding box (inset by 1/16) as end points,
// p-bits 1 so alpha stays exactly 1. A block with any unbaked texel is stored
// fully transparent (those 16 texels keep the live material).
inline constexpr char kTerrainBakeBc7Shader[] = R"hlsl(
Texture2D<float4> bakeAtlas:register(t0);
RWTexture2D<uint4> bakeBlocks:register(u0);
cbuffer TerrainBakeBc7:register(b0){uint2 bc7Offset;uint2 bc7Blocks;uint bc7Mip;uint3 bc7Padding;};
void bc7Put(inout uint4 block,inout uint position,uint value,uint count){
 uint word=position>>5,shift=position&31u;
 [unroll]for(uint k=0;k<4;++k){
  if(k==word)block[k]|=value<<shift;
  if(k==word+1&&shift+count>32)block[k]|=value>>(32-shift);
 }
 position+=count;
}
[numthreads(8,8,1)]void terrainBakeBc7CS(uint3 id:SV_DispatchThreadID){
 if(any(id.xy>=bc7Blocks))return;
 float3 texel[16];float3 lo=1,hi=0;bool transparent=false;
 [unroll]for(uint k=0;k<16;++k){
  float4 c=bakeAtlas.Load(int3(bc7Offset+id.xy*4+uint2(k&3u,k>>2),bc7Mip));
  texel[k]=c.rgb;if(c.a<.999)transparent=true;
  lo=min(lo,c.rgb);hi=max(hi,c.rgb);
 }
 uint4 block=0;uint position=0;
 bc7Put(block,position,64u,7); // mode 6
 if(transparent){bakeBlocks[id.xy]=block;return;} // end points, alpha and indices 0
 float3 inset=(hi-lo)/16;lo=saturate(lo+inset);hi=saturate(hi-inset);
 // p-bit 1: end point = 2q + 1 per channel (alpha 127 -> 255).
 uint3 q0=uint3(clamp(round((lo*255-1)/2),0,127)),q1=uint3(clamp(round((hi*255-1)/2),0,127));
 float3 e0=(float3(q0)*2+1)/255,e1=(float3(q1)*2+1)/255,axis=e1-e0;float span=dot(axis,axis);
 uint index[16];
 [unroll]for(uint k=0;k<16;++k)index[k]=span>1e-10?uint(clamp(round(dot(texel[k]-e0,axis)/span*15),0,15)):0u;
 // The anchor (texel 0) index must fit 3 bits: else swap the end points.
 if(index[0]>=8){uint3 t=q0;q0=q1;q1=t;[unroll]for(uint k=0;k<16;++k)index[k]=15u-index[k];}
 bc7Put(block,position,q0.x,7);bc7Put(block,position,q1.x,7);
 bc7Put(block,position,q0.y,7);bc7Put(block,position,q1.y,7);
 bc7Put(block,position,q0.z,7);bc7Put(block,position,q1.z,7);
 bc7Put(block,position,127u,7);bc7Put(block,position,127u,7);
 bc7Put(block,position,1u,1);bc7Put(block,position,1u,1);
 bc7Put(block,position,index[0],3);
 [unroll]for(uint k=1;k<16;++k)bc7Put(block,position,index[k],4);
 bakeBlocks[id.xy]=block;
}
)hlsl";

} // namespace owned
