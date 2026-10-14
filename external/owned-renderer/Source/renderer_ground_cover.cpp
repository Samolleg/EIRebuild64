#include "embedded_perf06.h"
// Renderer-owned procedural ground cover: grass tufts, flowers, reeds,
// pebbles, stones, twigs, logs, dead bushes, leaves, snow piles, shells,
// kelp and seagrass. Nothing in the game draws or owns it.
//
// GPU-driven, no CPU instance data: every frame a compute pass walks the 4 m
// cells of a window around the eye, rejects cells outside the frustum and
// places candidates by a deterministic hash of cell, rule and index. Each
// candidate reads the shared area classes of the ambient particles (terrain
// type, liquid layer, leafy-tree count; ambient_particles.h), the ground and
// liquid-surface heights, per-metre shore/swamp/snow fields and a terrain
// colour map built once per terrain source from the game's own atlases.
// Survivors are thinned by eye distance (survivors grow), frustum-culled,
// bent by nearby units and appended per mesh; the per-mesh counts are the
// instance counts of indirect draws. Pools grow from a non-blocking count
// readback: sized by demand, never capped.
//
// Pass: opaque world colour after terrain and before the static objects and
// liquids (flush_native_static). Lit with the shared world light and point
// lights, receives the sun cascades, writes the AO ambient share, fogged with
// the border fog and world atmosphere. Casts no shadows.
#include "ground_cover.h"
#include "contact_blend.h"
#include "ground_cover_surface.h"
#include "ground_cover_interaction.h"
#include "ground_cover_wood.h"
#include "ground_cover_snow.h"
#include "ground_cover_cave.h"
#include "ambient_occlusion.h"
#include "ambient_terrain_grids.h"
#include "neighbour_maps.h"
#include "owned_asset_loader.h"
#include "owned_terrain_decode.h"
#include "renderer.h"
#include "engine_bridge.h"
#include "world_fog.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <deque>
#include <future>
#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace owned {
namespace {
using namespace ground_cover;
static_assert(gm_count == Renderer::kGroundCoverMeshes, "renderer pool count follows the mesh table");
static_assert(gm_blades_far == 18 && gm_flower_far == 30 && gm_reed_far == 31 && gm_cattail_far == 32 && gm_seagrass_far == 33 &&
              gm_count <= 36, "placement shader far-mesh ids and the 9 x uint4 pool arrays");

constexpr double kGroundCoverSecondsPerSegment = ground_cover::kSecondsPerSegment; // the ambient particles' game-clock scale
constexpr float kCellSize = 4.0f;
constexpr UINT kTrailTexelsPerCell = 32; // 12.5 cm world-anchored texels; R16, GPU-only
// Count readback words: per mesh pool demand (gcFinalizeCS).
constexpr UINT kGcCountWords = gm_count;
// Candidates one class round holds at least (a round never passes its split
// by more than one cell); the stage buffers hold one round. Rounds per class
// at most 64 (gcScanCS kGcMaxRounds).
constexpr UINT kGcRoundSlots = 131072, kGcMaxRounds = 64;
// One dispatch dimension: the stage buffers never outgrow what one indirect
// dispatch of 64-thread groups can walk (D3D11 limit).
constexpr UINT kGcStageLimit = 65535u * 64u;
constexpr char kGroundCoverShader[] = R"(
struct GcRule{uint mesh;uint areas;uint zones;uint field;float density;float scaleMin;float scaleMax;float jitter;
 float sxMin;float sxMax;float szMin;float szMax;float3 tint;float ownMix;float3 own;float frost;
 float clumpFreq;float clumpLo;float clumpHi;float nearTrees;float align;float normalBlend;float sink;float contact;
 float blendHeight;float material;float lodNear;float lodFar;float fieldA;float fieldB;float palette;uint flags;
 float farKeep;float growMax;float cluster;float bury;};
struct GcVertex{float3 p;float3 n;float4 c;float2 uv;float2 ex;};
// root xyz, scale | yaw cos/sin, stretch x/z, terrain normal xy, push xy (f16 pairs) |
// terrain tint rgba8 (a flatten), own rgba8 (a own mix) | ground z, blend height/contact, packed bytes, hash
struct GcInstance{float4 root;uint4 shape;uint4 color;uint4 extra;};
struct GcFootprint{float4 rectangle;float4 effect;};
cbuffer GcFrame:register(b11){
 row_major float4x4 gcClip;float4 gcPlanes[6];
 float4 gcEye;float4 gcWindow;float4 gcGrid;float4 gcQuality;float4 gcWind;float4 gcSwitches;
 float4 gcFogRange;float4 gcFogControl;float4 gcFogColor;float4 gcBarkRect;float4 gcEndRect;float4 gcStoneRect;
 float4 gcView;   // x farthest visible ground (cover limit)
 float4 gcWind2;  // x gust intensity, y gust pulse, z terrain full-sun comparison
 float4 gcTrailWindow; // world-XY origin, inverse extent
 float4 gcTrailStep;   // xy previous-state texel shift (scroll), z press, w release this frame
 float4 gcObjectWindow; // current mask world origin and inverse extent
 float4 gcFogSeam;float4 gcFogCurve;float4 gcFogSpanLo;float4 gcFogSpanHi; // neighbour seam fog (neighbour_fog.h)
 // Neighbour map pass: placement reads the neighbour's grids in its own map
 // metres; xyz is their world origin (0 for the active map), w 1 selects the
 // far meshes only.
 float4 gcOrigin;
 uint4 gcCapacity[9];uint4 gcBase[9];
 // Rendered pieces for neighbour cover's own band (neighbour_fog_piece_constants).
 float4 gcFogPieceInfo;float4 gcFogPieces[NEIGHBOUR_FOG_PIECES];
 uint4 gcSnowMaterials;
 // Snow trails (snow_trails.h): the field window (snowTrailRaw) and its
 // update step: xy previous-state texel shift, z game seconds elapsed.
 float4 gcSnowWindow;float4 gcSnowStep;
 // Active tiles (readers) and the tiles the update writes (active and
 // their neighbours; all after a window move), bit y*10+x.
 uint4 gcSnowTiles;uint4 gcSnowWrite;
 // x: byte offset of this placement pass's state in gcOverflow (the active
 // map's pass first, then each neighbour's).
 uint4 gcPass;
 // Placement: x candidate and y survivor capacity of a round, z jobs per
 // class (the window's cells), w window rows.
 uint4 gcStage;
 // x: round split (the candidate capacity less the largest cell).
 uint4 gcRound;
};
cbuffer GcDraw:register(b12){uint gcDrawBase;uint3 gcDrawPadding;};
// Placement kernels (compute only; b12 is free there): the global rule
// indices this kernel walks, in table order. The rule index keeps hashing
// every candidate exactly as one all-rules pass did.
// gcClassSlot: this class's 16 bytes in the stage counters.
cbuffer GcPlace:register(b12){uint gcClassCount;uint gcClassSlot;uint2 gcClassPadding;uint4 gcClassRules[16];};
cbuffer GcPaint:register(b13){uint gcPaintMaterial;uint gcPaintTilesX;uint gcPaintTilesY;uint gcPaintTexels;};
// Vertex programs (GC_VS) keep their inputs below t25: the lamp loop reads
// the point-shadow pages there (t25 on).
#ifdef GC_VS
StructuredBuffer<GcVertex> gcVertices:register(t2);
StructuredBuffer<GcInstance> gcInstances:register(t3);
#else
StructuredBuffer<GcVertex> gcVertices:register(t27);
StructuredBuffer<GcInstance> gcInstances:register(t28);
#endif
// Per pool slot, per frame (gcPrepCS): fog as colour*w+rgb, x cloud
// shadow, y wind sway; all depend on the instance position only.
struct GcDrawInput{float4 fog;float4 extra;};
StructuredBuffer<GcDrawInput> gcDrawInputs:register(t4);
RWStructuredBuffer<GcDrawInput> gcDrawInputOut:register(u6);
Texture2D<uint> gcArea:register(t29);
Texture2D<float> gcSurface:register(t30);
#if defined(GC_POINT_SHADOW_RECEIVER)||defined(GC_VS)
Texture2D<float> gcGround:register(t0);
#else
Texture2D<float> gcGround:register(t31);
#endif
Texture2D<float4> gcFields:register(t32);
#if defined(GC_POINT_SHADOW_RECEIVER)||defined(GC_VS)
Texture2D<float4> gcColor:register(t1);
#else
Texture2D<float4> gcColor:register(t33);
#endif
StructuredBuffer<GcRule> gcRules:register(t34);
#ifdef GC_POINT_SHADOW_RECEIVER
Texture2D gcBark:register(t2);
#else
Texture2D gcBark:register(t36);
#endif
#ifdef GC_POINT_SHADOW_RECEIVER
Texture2D gcStone:register(t3);
#else
Texture2D gcStone:register(t37);
#endif
struct GcTile{float2 uv00;float2 uv10;float2 uv01;float2 uv11;uint material;uint3 tilePadding;};
StructuredBuffer<GcTile> gcTiles:register(t38);
Texture2D gcAtlas:register(t39);
Texture2D<float> gcSea:register(t41);
// Base terrain type (ETERRTYPE) per 2 m tile, without overlays or liquid layers.
Texture2D<uint> gcBaseType:register(t43);
#ifdef GC_POINT_SHADOW_RECEIVER
Texture2D<float> gcTrail:register(t5);
#else
Texture2D<float> gcTrail:register(t44);
#endif
StructuredBuffer<GcFootprint> gcFootprints:register(t45);
// The previous smoothed trail state (ping-pong partner or the outgrown texture).
Texture2D<float> gcTrailPrevious:register(t46);
Texture2D<float2> gcTrailSources:register(t49);
// Snow trails for the vertex and placement stages (the pixel stage reads
// nativeSnowTrail at t8; t51 there is a point-shadow page).
#ifdef GC_VS
Texture2D<float2> gcSnowTrail:register(t6);
#else
Texture2D<float2> gcSnowTrail:register(t51);
#endif
struct GcSnowSegment{float2 a;float2 b;float halfWidth;float depth;float berm;float along;uint seed;uint3 padding;};
StructuredBuffer<GcSnowSegment> gcSnowSegments:register(t52);
Texture2D<float2> gcSnowPrevious:register(t53);
// Rendered terrain rectangles and their zone classes; 32 bytes per piece.
struct GcZonePiece{float4 bounds;uint zone;uint3 padding;};
StructuredBuffer<GcZonePiece> gcZonePieces:register(t50);
float gcZoneWeight(float2 world,uint zones){
 uint count,stride;gcZonePieces.GetDimensions(count,stride);
 float total=0,accepted=0;
 [loop]for(uint i=0;i<count;++i){
  GcZonePiece piece=gcZonePieces[i];
  float2 delta=max(max(piece.bounds.xy-world,world-piece.bounds.zw),0);
  // Eight metres inside either adjoining piece, equal mixture at the seam.
  float w=1-smoothstep(0,8,length(delta));
  total+=w;if(((zones>>piece.zone)&1u)!=0)accepted+=w;
 }
 return total>0?accepted/total:0;
}
// Independent deciduous, conifer and woody influence, from placed MOB models.
Texture2D<float4> gcTreesMap:register(t47);
#ifdef GC_POINT_SHADOW_RECEIVER
Texture2D<float> gcObjectMask:register(t4);
#else
Texture2D<float> gcObjectMask:register(t48);
#endif
#ifdef GC_POINT_SHADOW_RECEIVER
// Snow art and cover masks use the same clamp/filter state. Keep one live
// declaration: FXC rejects two samplers occupying s8 in the pixel entry.
#define gcClamp contactSampler
#else
SamplerState gcClamp:register(s8);
#endif
SamplerState gcWrap:register(s9);
RWStructuredBuffer<GcInstance> gcInstanceOut:register(u0);
RWByteAddressBuffer gcArgs:register(u1);
RWByteAddressBuffer gcCounts:register(u2);
RWTexture2D<float4> gcColorOut:register(u3);
// Per pass and mesh, 8 bytes: demand this frame (every instance placed,
// before the pool), and 1 - keep fraction (float) from the previous frame.
RWByteAddressBuffer gcOverflow:register(u4);
// Per pool slot, written by placement for the sun shadow pass (gcShadeCS):
// the tuft's mid height z, its ground slope xy and the blocker-search hash,
// exactly the values placement held (the instance keeps only f16 normals).
RWStructuredBuffer<uint4> gcShadeInput:register(u5);
// Per frame rise of the keep fraction once the pool fits again: thinned
// instances grow back in over a few dozen frames instead of popping.
static const float kGcOverflowRecover=.02;

static const uint kGcWater=6,kGcSwamp=5,kGcSnow=4,kGcDry=2,kGcSand=3,kGcBladesFar=18;
uint gcHash(uint x){x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;return x;}
float gcRand(inout uint s){s=gcHash(s+0x9e3779b9u);return (s&0xffffffu)*(1.0/16777216.0);}
float gcLattice(int2 p,uint seed){return (gcHash(uint(p.x)*73856093u^uint(p.y)*19349663u^seed)&0xffffffu)*(1.0/16777216.0);}
// The bilinear helpers below walk their corners in constant-bound loops
// (one inlined corner each) and keep the original lerp order exactly.
float gcNoise(float2 p,uint seed){
 int2 i=int2(floor(p));float2 f=p-i;f=f*f*(3-2*f);
 float row0=0,row1=0;
 [loop]for(int y=0;y<2;++y){
  float a=0,b=0;
  [loop]for(int x=0;x<2;++x){float v=gcLattice(i+int2(x,y),seed);if(x==0)a=v;else b=v;}
  if(y==0)row0=lerp(a,b,f.x);else row1=lerp(a,b,f.x);
 }
 return lerp(row0,row1,f.y);
}
float gcFbm(float2 p,uint seed){
 float o0=0,o1=0;
 [loop]for(uint o=0;o<2;++o){
  if(o==0)o0=gcNoise(p,seed)*.65;else o1=gcNoise(p*2.3,seed+7)*.35;
 }
 return o0+o1;
}
float2 gcMapMetres(){return gcGrid.xy*2;}
float gcVertexDepth(int2 v){
 uint w,h;gcGround.GetDimensions(w,h);
 if(any(v<0)||v.x>=int(w)||v.y>=int(h))return 0;
 return gcSurface.Load(int3(v,0))-gcGround.Load(int3(v,0));
}
uint gcAreaAt(int2 t){if(any(t<0)||t.x>=int(gcGrid.x)||t.y>=int(gcGrid.y))return 0xffu;return gcArea.Load(int3(t,0));}
uint gcBaseTypeRaw(int2 t){if(any(t<0)||t.x>=int(gcGrid.x)||t.y>=int(gcGrid.y))return 0xffu;return gcBaseType.Load(int3(t,0));}
// ETERRTYPE of the base tile (0xff: none); upper bits mark cover families.
uint gcBaseTypeAt(int2 t){uint v=gcBaseTypeRaw(t);return v==0xffu?v:(v&15u);}
// Base tile whose atlas slot has a sand corner family (ambient_terrain_grids.h
// kSandFamilyType); set once the map's tile families load.
bool gcSandFamilyAt(int2 t){uint v=gcBaseTypeRaw(t);return v!=0xffu&&(v&16u)!=0;}
// The area class of the base terrain type (ambient_particles.h area_from_terrain_type).
uint gcTypeArea(uint type){
 if(type==0xffu)return 0xffu;
 if(type==0)return 1;if(type==5||type==11)return 2;if(type==3)return 3;
 if(type==9||type==10||type==12)return 4;if(type==14)return 5;if(type==6)return 6;if(type==13)return 7;return 0;
}
// Land cover classifies the resolved ground (base tile); underwater cover the
// liquid class (only where that liquid actually lies over the ground). Beach
// wrack (fd_beach) also counts sand-family tiles as sand.
uint gcSurfaceArea(int2 t,bool underwater,bool beach){
 if(underwater){uint v=gcAreaAt(t);return v==0xffu?0xffu:(v&15u);}
 if(beach&&gcSandFamilyAt(t))return kGcSand;
 uint type=gcBaseTypeAt(t);
 // A plain editor soil/paving family can carry a grassy ETERRTYPE. Keep
 // liquid, snow and sand exclusions; mixed/unclassified slots keep their type.
 if((gcBaseTypeRaw(t)&32u)!=0&&(type==0u||type==5u||type==11u))return 0;
 return gcTypeArea(type);
}
// Share of soil/paving/rock at p from the base tile's bare world corners
// (bits 8-11, ambient_terrain_grids.h terrain_cover_types), bilinear across
// the tile so neighbouring tiles meet on their shared corners; 0 until the
// map's tile families load and for unassigned corners.
float gcBareShare(float2 p){
 uint v=gcBaseTypeRaw(int2(floor(p*.5)));
 if(v==0xffu)return 0;
 uint m=v>>8;float2 f=frac(p*.5);
 return lerp(lerp(float(m&1u),float((m>>1)&1u),f.x),lerp(float((m>>2)&1u),float((m>>3)&1u),f.x),f.y);
}
float gcIn(uint areas,int2 t,bool underwater,bool beach){uint v=gcSurfaceArea(t,underwater,beach);return v==0xffu?0:float((areas>>v)&1u);}
bool gcOwnsPoint(float2 p,float2 mapSize){return all(p>=0)&&all(p<mapSize);}
float2 gcCoverageTile(float2 p,float2 grid){return clamp(p*.5-.5,0,grid-1);}
// Rule coverage bilinear between tile centres: soft edges instead of the 2 m tile grid.
float gcCoverage(uint areas,float2 p,bool underwater,bool beach){
 // Extend the boundary tile to its actual edge; outside is not an empty tile.
 float2 t=gcCoverageTile(p,gcGrid.xy);int2 i=int2(floor(t));float2 f=t-i;
 float row0=0,row1=0;
 [loop]for(int y=0;y<2;++y){
  float a=0,b=0;
  [loop]for(int x=0;x<2;++x){float v=gcIn(areas,i+int2(x,y),underwater,beach);if(x==0)a=v;else b=v;}
  if(y==0)row0=lerp(a,b,f.x);else row1=lerp(a,b,f.x);
 }
 return lerp(row0,row1,f.y);
}
float3 gcTreeInfluence(float2 p){return gcTreesMap.SampleLevel(gcClamp,p/gcMapMetres(),0).rgb;}
// Actual height-field cliffs and projected static footprints supply walls.
// Two perpendicular contacts identify corners for web tufts.
float4 gcCaveWall(float2 p){
 float nearest=5,corner=0;float2 direction=0,first=0;float z=gcHeight(gcGround,p);
 // Loops, not unrolled: the unrolled 8x4 walk with its break took about
 // 3500 instruction slots and 270 temporaries (register spill, issue #152).
 [loop]for(uint k=0;k<8;++k){
  float2 dir=float2(cos(k*.78539816),sin(k*.78539816));
  [loop]for(uint j=0;j<4;++j){
   float radius=.5*exp2(float(j));float2 q=p+dir*radius;
   if(any(q<0)||any(q>=gcMapMetres()))continue;
   bool wall=gcHeight(gcGround,q)>z+radius*.8||gcObjectMask.SampleLevel(gcClamp,(q-gcObjectWindow.xy)*gcObjectWindow.zw,0)>.95;
   if(!wall)continue;
   if(nearest==5)first=dir;
   if(abs(dot(first,dir))<.3)corner=1;
   if(radius<nearest){nearest=radius;direction=dir;}
   break;
  }
 }
 return float4(nearest,direction,corner);
}
float4 gcFieldsAt(float2 p){return gcFields.SampleLevel(gcClamp,p/gcMapMetres(),0);}
// Ground height above the first water surface down the shore field (the
// direction of the nearest water line); 1e9 when none lies within reach.
static const float kGcKelpSlopeCos=.9397,kGcKelpAboveWater=.8; // ground_cover.h kelp_ground
float gcAboveWater(float2 p,float ground,float shore){
 const float2 g=float2(gcFieldsAt(p+float2(.5,0)).r-gcFieldsAt(p-float2(.5,0)).r,gcFieldsAt(p+float2(0,.5)).r-gcFieldsAt(p-float2(0,.5)).r);
 if(dot(g,g)<1e-10)return 1e9;
 const float2 dir=-normalize(g);
 [loop]for(float r=.5;r<=shore+2;r+=.5){
  const float2 q=p+dir*r;
  if(any(q<0)||any(q>=gcMapMetres()))break;
  const float s=gcHeight(gcSurface,q);
  if(s-gcHeight(gcGround,q)>.05)return ground-s;
 }
 return 1e9;
}
float3 gcTerrainColor(float2 p,float level){
 float3 c=gcColor.SampleLevel(gcClamp,p/gcMapMetres(),level).rgb;
 return c;
}
uint gcPack2(float a,float b){return f32tof16(a)|(f32tof16(b)<<16);}
float2 gcUnpack2(uint v){return float2(f16tof32(v&0xffffu),f16tof32(v>>16));}
uint gcPackColor(float4 c){uint4 u=uint4(saturate(c)*255+.5);return u.x|(u.y<<8)|(u.z<<16)|(u.w<<24);}
float4 gcUnpackColor(uint v){return float4(v&255,(v>>8)&255,(v>>16)&255,v>>24)/255.0;}
bool gcOutside(float3 c,float radius){
 [unroll]for(uint k=0;k<6;++k)if(dot(gcPlanes[k].xyz,c)+gcPlanes[k].w< -radius)return true;
 return false;
}
)" R"(
// Sun blocker gap for contact-hardening shadows on this instance (world
// metres from the receiver to the mean blocker; 0 when nothing blocks): the
// world filter's blocker search (cascade, receiver plane on the terrain
// gradient, bias, search width) once per instance at its mid height instead
// of in every fragment. 8 taps on a disk turned by the instance hash.
// Settings permutations (selected per frame from the renderer's state; a
// program without the define keeps the runtime test): GC_SUN_SOFT soft
// shadows (shadow_softness > 0), GC_SUN_FAR the far shadow maps (shadow_far).
bool gcSunSoft(){
#ifdef GC_SUN_SOFT
 return GC_SUN_SOFT!=0;
#else
 return shadowBias[5].z>.5;
#endif
}
bool gcSunFar(){
#ifdef GC_SUN_FAR
 return GC_SUN_FAR!=0;
#else
 return shadowBias[4].z>.5;
#endif
}
// slice: nativeShadowSlice(worldPosition), shared with gcSunVisibilityIn.
float gcSunGap(uint slice,float3 worldPosition,float2 groundSlope,uint hash){
 [branch]if(shadowControl.x==0||!gcSunSoft())return 0;
 if(slice==5||(shadowControl.x&(1u<<slice))==0)return 0;
 float4 light=mul(shadowLight[slice],float4(worldPosition,1));float3 coordinate=light.xyz/light.w;
 if(abs(coordinate.x)>1||abs(coordinate.y)>1||coordinate.z<0||coordinate.z>1)return 0;
 float3 dx=mul((float3x3)shadowLight[slice],float3(1,0,groundSlope.x)),dy=mul((float3x3)shadowLight[slice],float3(0,1,groundSlope.y));
 float side=max(1.0,(float)shadowControl.z),halfSide=.5*side;
 dx.xy*=float2(halfSide,-halfSide);dy.xy*=float2(halfSide,-halfSide);
 float3 plane=cross(dx,dy);
 if(plane.z==0||!all(isfinite(plane)))return 0;
 float2 depthStep=-plane.xy/plane.z;
 if(!all(isfinite(depthStep)))return 0;
 float texelWorld=max(shadowBias[slice].x,1e-6);
 float bias=.5*shadowBias[slice].x*shadowBias[slice].y;
 float2 texel=float2(coordinate.x*.5+.5,.5-coordinate.y*.5)*side;
 float radius=slice>=3&&gcSunFar()?min(1.5,.5/texelWorld):1.5;
 float search=clamp(shadowBias[5].y/texelWorld,max(2.0,radius),24.0);
 const float2 golden=float2(-.73736887,.67549029);
 float2 direction;sincos((hash&0xffffu)*(6.2831853/65536.0),direction.y,direction.x);
 float blockers=0,blockerDepth=0;
 [unroll]for(uint k=0;k<8;++k){
  float2 at=clamp(texel+direction*(sqrt((k+.5)/8)*search),.5,side-.5);
  float stored=shadowDepth.Load(int4(int2(at),slice,0));
  if(stored<coordinate.z+dot(depthStep,floor(at)+.5-texel)-bias){blockers+=1;blockerDepth+=stored;}
  direction=float2(direction.x*golden.x-direction.y*golden.y,direction.x*golden.y+direction.y*golden.x);
 }
 return blockers>0?max(coordinate.z-blockerDepth/blockers,0)/max(shadowBias[slice].y,1e-12):0;
}
// Kept fraction at a distance: full near, thinning to the far LOD density that
// lasts to the farthest visible ground (cover never fades out of the view).
float gcKeep(GcRule r,float d){return gcDensityKeep(r.lodNear,r.lodFar,r.farKeep,gcQuality.x,d);}

float gcTrailCoverage(float2 p){
 if(gcView.w==0)return 0;
 float2 uv=(p-gcTrailWindow.xy)*gcTrailWindow.zw;
 if(any(uv<0)||any(uv>=1))return 0;
 uint w,h;gcTrail.GetDimensions(w,h);
 // Point lookup: no filtering can spread pressing outside a visible print.
 return gcTrail.Load(int3(int2(floor(uv*float2(w,h))),0));
}

// snowTrailSlope (snow_trails.h) with one inlined field read.
float2 gcSnowTrailSlope(float2 p){
 const float d=1/gcSnowWindow.z;float4 h=0;
 [loop]for(uint k=0;k<4;++k){
  const float v=snowTrailHeight(snowTrailRaw(gcSnowTrail,gcSnowWindow,gcSnowTiles,p+(k==0?float2(d,0):k==1?float2(-d,0):k==2?float2(0,d):float2(0,-d))));
  if(k==0)h.x=v;else if(k==1)h.y=v;else if(k==2)h.z=v;else h.w=v;
 }
 return float2(h.x-h.y,h.z-h.w)*(.5*gcSnowWindow.z);
}
struct GcObjectOut{float4 p:SV_Position;float2 local:TEXCOORD0;nointerpolation float2 halfSize:TEXCOORD1;};
GcObjectOut gcObjectVS(uint vertex:SV_VertexID,uint instance:SV_InstanceID){
 const float2 corners[6]={float2(-1,-1),float2(1,-1),float2(-1,1),float2(-1,1),float2(1,-1),float2(1,1)};
 GcFootprint f=gcFootprints[instance];GcObjectOut o;
 o.local=corners[vertex]*(f.rectangle.zw+.2);o.halfSize=f.rectangle.zw;
 float2 world=f.rectangle.xy+o.local;
 o.p=float4((world-gcObjectWindow.xy)*gcObjectWindow.zw*float2(2,-2)+float2(-1,1),0,1);return o;
}
float gcObjectPS(GcObjectOut i):SV_Target{
 float2 edge=max(abs(i.local)-i.halfSize,0);
 return 1-smoothstep(0,.2,length(edge));
}

struct GcTrailOut{float4 p:SV_Position;float2 local:TEXCOORD0;
 nointerpolation float4 rectangle:TEXCOORD1;nointerpolation float4 effect:TEXCOORD2;};
GcTrailOut gcTrailVS(uint vertex:SV_VertexID,uint instance:SV_InstanceID){
 const float2 corners[6]={float2(-1,-1),float2(1,-1),float2(-1,1),float2(-1,1),float2(1,-1),float2(1,1)};
 GcFootprint f=gcFootprints[instance];
 float2 texel=gcView.yz;
 float2 margin=.5*float2(abs(f.effect.x)*texel.x+abs(f.effect.y)*texel.y,
                        abs(f.effect.y)*texel.x+abs(f.effect.x)*texel.y);
 float2 local=corners[vertex]*(f.rectangle.zw+margin);
 float2 world=f.rectangle.xy+float2(local.x*f.effect.x-local.y*f.effect.y,local.x*f.effect.y+local.y*f.effect.x);
 GcTrailOut o;o.p=float4((world-gcTrailWindow.xy)*gcTrailWindow.zw*float2(2,-2)+float2(-1,1),0,1);
 o.local=local;o.rectangle=f.rectangle;o.effect=f.effect;return o;
}
// Previous state at this world texel; the window scrolls in whole texels.
float gcTrailBefore(float2 position){
 uint w,h;gcTrailPrevious.GetDimensions(w,h);
 int2 t=int2(position)+int2(gcTrailStep.xy);
 return any(t<0)||t.x>=int(w)||t.y>=int(h)?0:gcTrailPrevious.Load(int3(t,0));
}
// Compose current source strength/marker coverage, then recover the old field
// only where no current marker owns the deformation.
float4 gcTrailFullVS(uint vertex:SV_VertexID):SV_Position{
 float2 u=float2((vertex<<1)&2,vertex&2);return float4(u*float2(2,-2)+float2(-1,1),0,1);
}
float gcTrailDecayPS(float4 p:SV_Position):SV_Target{
 float2 source=gcTrailSources.Load(int3(int2(p.xy),0));
 return gcTrailRecovery(gcTrailBefore(p.xy),source.x,source.y,gcTrailStep.w);
}
)" R"(
// Trail ground share at p: snow and snowballs (base types 9, 12; never ice,
// 10), bilinear between tile centres so a trail fades out where the snow
// ends; plus soft sand at 60% depth, by material identity: the soft sand
// family corners (bits 12-15, terrain_tile_soft_sand), bilinear across the
// tile like the bare corners. Wet, hard and mixed sands take none.
float gcSnowTile(int2 t){uint v=gcBaseTypeAt(t);return v==9u||v==12u?1:0;}
float gcSoftSandShare(float2 p){
 uint v=gcBaseTypeRaw(int2(floor(p*.5)));
 if(v==0xffu)return 0;
 uint m=v>>12;float2 f=frac(p*.5);
 return lerp(lerp(float(m&1u),float((m>>1)&1u),f.x),lerp(float((m>>2)&1u),float((m>>3)&1u),f.x),f.y);
}
float gcSnowGround(float2 p){
 float2 t=gcCoverageTile(p,gcGrid.xy);int2 i=int2(floor(t));float2 f=t-i;
 float snow=lerp(lerp(gcSnowTile(i),gcSnowTile(i+int2(1,0)),f.x),lerp(gcSnowTile(i+int2(0,1)),gcSnowTile(i+int2(1,1)),f.x),f.y);
 return max(snow,.6*gcSoftSandShare(p));
}
// One capsule per unit step, MAX-composed into the field (snow_trails.h).
struct GcSnowOut{float4 p:SV_Position;float2 world:TEXCOORD0;nointerpolation float4 ends:TEXCOORD1;nointerpolation float3 shape:TEXCOORD2;nointerpolation float2 path:TEXCOORD3;};
GcSnowOut gcSnowStampVS(uint vertex:SV_VertexID,uint instance:SV_InstanceID){
 const float2 corners[6]={float2(-1,-1),float2(1,-1),float2(-1,1),float2(-1,1),float2(1,-1),float2(1,1)};
 GcSnowSegment s=gcSnowSegments[instance];
 float2 ab=s.b-s.a;float l=length(ab);
 float2 u=l>1e-5?ab/l:float2(1,0),v=float2(-u.y,u.x);
 float reach=2.3*s.halfWidth+2/gcSnowWindow.z;
 float2 world=(s.a+s.b)*.5+u*corners[vertex].x*(l*.5+reach)+v*corners[vertex].y*reach;
 GcSnowOut o;o.p=float4((world-gcSnowWindow.xy)*(gcSnowWindow.z/gcSnowWindow.w)*float2(2,-2)+float2(-1,1),0,1);
 o.world=world;o.ends=float4(s.a,s.b);o.shape=float3(s.halfWidth,s.depth,s.berm);o.path=float2(s.along,asfloat(s.seed));return o;
}
float2 gcSnowStampPS(GcSnowOut i):SV_Target{return snowTrailStamp(i.world,i.ends.xy,i.ends.zw,i.shape,i.path)*gcSnowGround(i.world);}
// The previous field moved to the new window origin and filled in by the
// elapsed game seconds; texels new to the window start untouched. Only the
// written tiles are drawn: one quad per tile, collapsed when its bit is off.
float4 gcSnowTileVS(uint vertex:SV_VertexID,uint instance:SV_InstanceID):SV_Position{
 const float2 corners[6]={float2(0,0),float2(1,0),float2(0,1),float2(0,1),float2(1,0),float2(1,1)};
 if(((gcSnowWrite[instance>>5]>>(instance&31u))&1u)==0)return float4(2,2,2,1);
 float2 t=(float2(instance%10u,instance/10u)+corners[vertex])*.1;
 return float4(t*float2(2,-2)+float2(-1,1),0,1);
}
float2 gcSnowRecoverPS(float4 p:SV_Position):SV_Target{
 int2 t=int2(p.xy)+int2(gcSnowStep.xy);uint w,h;gcSnowPrevious.GetDimensions(w,h);
 return any(t<0)||t.x>=int(w)||t.y>=int(h)?0:snowTrailRecover(gcSnowPrevious.Load(int3(t,0)),gcSnowStep.z);
}
float2 gcTrailPS(GcTrailOut i):SV_Target{
 const float target=gcPressTexel(i.local,i.rectangle.zw,i.effect,gcView.yz);
 const float before=gcTrailBefore(i.p.xy);
 // Live and retained body stamps share the same immediate radial press.
 float pressed=i.effect.w!=0?target:min(before+gcTrailStep.z,target);
 float4 full=i.effect;full.z=1;
 float marked=i.effect.w<0&&gcPressTexel(i.local,i.rectangle.zw,full,gcView.yz)>0?1:0;
 return float2(pressed,marked);
}

)" R"(
groupshared uint gcPrefix[65];
groupshared uint gcCellVisible;
groupshared uint gcCellZones;
groupshared float gcCellNear;
// Sun shadow for cover fragments: the world filter's cascade choice, bias,
// minimum footprint, penumbra width and Vogel disks (rotated per pixel, and
// per frame while the temporal shadow history integrates them, exactly like
// world pixels), but 4+4 taps (8+8 at high quality) instead of 8+12 (16+24).
// Cover joins the history, so the integrated penumbra matches the terrain's.
// Receiver plane: the terrain under the cover, never the blade's own facing.
// Cover casts no shadow, so the ground is the only surface it can self-shadow
// against, and a thin blade's screen-derivative plane extrapolates its depth
// far off across the kernel (taps flip fully lit or fully shadowed).
float gcSunFilter(uint slice,float3 worldPosition,float2 groundSlope,float2 rotation,float gap,uint taps){
 float4 light=mul(shadowLight[slice],float4(worldPosition,1));float3 coordinate=light.xyz/light.w;
 if(abs(coordinate.x)>1||abs(coordinate.y)>1||coordinate.z<0||coordinate.z>1)return 1;
 float3 dx=mul((float3x3)shadowLight[slice],float3(1,0,groundSlope.x)),dy=mul((float3x3)shadowLight[slice],float3(0,1,groundSlope.y));
 float side=max(1.0,(float)shadowControl.z),halfSide=.5*side;
 dx.xy*=float2(halfSide,-halfSide);dy.xy*=float2(halfSide,-halfSide);
 float3 plane=cross(dx,dy);
 if(plane.z==0||!all(isfinite(plane)))return 1;
 float2 depthStep=-plane.xy/plane.z;
 if(!all(isfinite(depthStep)))return 1;
 float texelWorld=max(shadowBias[slice].x,1e-6);
 float bias=.5*shadowBias[slice].x*shadowBias[slice].y;
 float2 texel=float2(coordinate.x*.5+.5,.5-coordinate.y*.5)*side;
 float radius=slice>=3&&gcSunFar()?min(1.5,.5/texelWorld):1.5;
 // nativeVogel(k,n,rotation) without per-tap sincos: each step turns the
 // direction by the golden angle.
 const float2 golden=float2(-.73736887,.67549029);
 // Contact hardening from a blocker gap (gcSunGap): the penumbra width
 // varies over metres, a tuft spans centimetres. Gap 0: the fixed disk.
 [branch]if(gcSunSoft()&&gap>0){
  float search=clamp(shadowBias[5].y/texelWorld,max(2.0,radius),24.0);
  radius=clamp(gap*shadowBias[5].x/texelWorld,radius,search);
 }
 // The world's filter disk is turned 1.3 rad from its search disk.
 float2 direction=float2(rotation.x*.26749883-rotation.y*.96355819,rotation.x*.96355819+rotation.y*.26749883);
 float visibility=0;
 [loop]for(uint t=0;t<taps;++t){
  float2 offset=direction*(sqrt((t+.5)/taps)*radius);
  visibility+=nativeShadowPlaneTap(slice,side,texel+offset,texel,coordinate.z-bias,depthStep);
  direction=float2(direction.x*golden.x-direction.y*golden.y,direction.x*golden.y+direction.y*golden.x);
 }
 return visibility/taps;
}
// The world visibility's cascade and far-map blending around gcSunFilter.
float gcSunVisibilityIn(uint slice,float3 worldPosition,float2 groundSlope,uint seed,float gap,uint taps){
 if(slice==5)return 1;
 if((shadowControl.x&(1u<<slice))==0)return gcSunFar()?-1:1;
 float2 rotation;
 // The world's per-pixel rotation decorrelated neighbouring pixels: on 1-4 px
 // blades a wide penumbra disk then missed the texture cache on every tap
 // (about 1.1 ms of ground_cover_draw at 4K, issue #147). One disk per tuft,
 // the same per-frame jitter.
 sincos(nativeShadowRotation(float2(seed&4095u,(seed>>12)&4095u),true),rotation.y,rotation.x);
 float visibility=gcSunFilter(slice,worldPosition,groundSlope,rotation,gap,taps);
 if(!gcSunFar())return visibility;
 float4 light=mul(shadowLight[slice],float4(worldPosition,1));
 float edge=max(abs(light.x),abs(light.y))/light.w;
 float blend=smoothstep(.75,1-7.0/max(1.0,(float)shadowControl.z),edge);
 if(slice==4)return lerp(visibility,1,blend);
 if(slice<3&&shadowBias[slice].w>shadowBias[slice].z)
  blend=max(blend,smoothstep(shadowBias[slice].z,shadowBias[slice].w,mul(shadowCamera[slice],float4(worldPosition,1)).w));
 if(blend<=0)return visibility;
 // The first enabled coarser map containing the point, then one filter
 // (a filter call per unrolled candidate inlined gcSunFilter five times).
 uint coarse=5;
 [loop]for(uint coarser=0;coarser<5;coarser++){
  [branch]if(coarse==5&&coarser>slice&&(shadowControl.x&(1u<<coarser))!=0&&nativeShadowContains(coarser,worldPosition))coarse=coarser;
 }
 if(coarse==5)return lerp(visibility,1,blend);
 return lerp(visibility,gcSunFilter(coarse,worldPosition,groundSlope,rotation,gap,taps),blend);
}
float gcSunVisibility(float3 worldPosition,float2 groundSlope,uint seed,float gap,uint taps){
 return gcSunVisibilityIn(nativeShadowSlice(worldPosition),worldPosition,groundSlope,seed,gap,taps);
}
// Share of a cover fragment's sun visibility taken from its tuft's own
// contact-hardening value (placement) rather than its per-pixel fixed-disk
// filter: 0 for contact-hard edges (filter radius under 8 cm, penumbra under
// ~16 cm, sharp across a near tuft), 1 from 20 cm (penumbra ~40 cm and wider
// varies little across a 0.3-0.7 m tuft). 0 without soft shadows.
float gcTuftShadowShare(float gap){return gcSunSoft()?smoothstep(.08,.2,gap*shadowBias[5].x):0;}
)" R"(
// Placement kernels: one per rule class (GC_PLACE_*), each dispatched over
// only its own rules (GcPlace) and compiled with only the features those
// rules use. The renderer assigns every rule to the kernel whose features
// cover it (place_kernel_for); a statically absent feature is one none of
// the kernel's rules can reach, so every rule places exactly as in a single
// all-features pass.
#if defined(GC_PLACE_GRASS)
#define GC_F_EDGE 1
#define GC_F_CLUSTER 1
#define GC_F_FAR 1
#define GC_F_TRAIL 1
#define GC_F_PAL1 1
#elif defined(GC_PLACE_TALL)
#define GC_F_FAR 1
#define GC_F_TRAIL 1
#elif defined(GC_PLACE_DEBRIS)
#define GC_F_PAL23 1
#elif defined(GC_PLACE_WALL)
#define GC_F_WALL 1
#elif defined(GC_PLACE_BEACH)
#define GC_F_BEACH 1
#elif defined(GC_PLACE_PILE)
#define GC_F_PILE 1
#elif defined(GC_PLACE_NEIGHBOUR)
// Neighbour maps: every rule in one kernel, never trails (none on neighbours).
#define GC_F_EDGE 1
#define GC_F_CLUSTER 1
#define GC_F_FAR 1
#define GC_F_PAL1 1
#define GC_F_PAL23 1
#define GC_F_WALL 1
#define GC_F_BEACH 1
#define GC_F_PILE 1
#endif
#ifndef GC_F_EDGE
#define GC_F_EDGE 0
#endif
#ifndef GC_F_CLUSTER
#define GC_F_CLUSTER 0
#endif
#ifndef GC_F_FAR
#define GC_F_FAR 0
#endif
#ifndef GC_F_TRAIL
#define GC_F_TRAIL 0
#endif
#ifndef GC_F_PAL1
#define GC_F_PAL1 0
#endif
#ifndef GC_F_PAL23
#define GC_F_PAL23 0
#endif
#ifndef GC_F_WALL
#define GC_F_WALL 0
#endif
#ifndef GC_F_BEACH
#define GC_F_BEACH 0
#endif
#ifndef GC_F_PILE
#define GC_F_PILE 0
#endif
// Placement runs every frame (issue #152), staged so no program spills or
// carries the whole rule set:
// - gcCellsCS tests every window cell once (height box, frustum, nearest
//   eye distance, zones) and appends each visible cell to the job list of
//   every class with candidate slots there;
// - gcScanCS splits each class's jobs into rounds that fit the stage
//   buffers (every slot of a round can become a candidate: exact);
// - per class and round, gcPlaceCS walks a job's slots (position, zone,
//   object footprint, distance thinning, surface class, coverage),
//   gcHabitatCS applies the habitat, gcBuildCS sizes, colours, bends and
//   appends the instance.
// Neighbour maps run the same three stages fused in one kernel per map
// (gcNeighbourCS): their few far-mesh candidates do not pay for staging.
// Every stage function runs the original statements in their original
// order, so each candidate draws the same random sequence and places exactly
// as one all-rules pass did.
struct GcCandidate{float2 p;uint seed;uint rule;float thin;float keep;float ground;float depth;uint here;uint patch;uint2 padding;};
struct GcSurvivor{float2 p;uint seed;uint rule;float thin;float keep;float ground;float depth;uint here;uint patch;
 float canopy;float swamp;float3 n;float3 terrain;float2 wall;};
// A visible cell of one class: cell (x | y << 16), nearest eye distance,
// zone classes within 8 m, and its candidate slots in that class.
struct GcCellJob{uint cell;float near;uint zones;uint slots;};
RWStructuredBuffer<GcCellJob> gcCellJobOut:register(u6);
StructuredBuffer<GcCellJob> gcCellJobs:register(t56);
// Per class: job count (gcCellsCS), then per round (gcScanCS) the dispatch
// arguments (groups, 1, 1) and the round's first job, 16 bytes each.
RWByteAddressBuffer gcCellCountOut:register(u3);
ByteAddressBuffer gcCellCounts:register(t58);
RWByteAddressBuffer gcRoundOut:register(u7);
ByteAddressBuffer gcRounds:register(t57);
StructuredBuffer<uint> gcRuleClass:register(t59); // placement class of each rule
// Per cell (x, y) the rules that can place there (a superset of every test
// below, renderer gc_cell_rule_masks): the others are skipped, exactly.
Texture2D<uint2> gcCellMasks:register(t61);
bool gcRuleMasked(uint2 mask,uint r){return ((r<32u?mask.x>>r:mask.y>>(r-32u))&1u)!=0;}
static const uint kGcMaxRounds=64,kGcPlaceClasses=6;
// Per dispatch: the round of this class (gcPlaceCS).
cbuffer GcRun:register(b13){uint gcRunRound;uint3 gcRunPadding;};
RWStructuredBuffer<GcCandidate> gcCandidateOut:register(u7);
RWStructuredBuffer<GcCandidate> gcCandidateIn:register(u6);
RWStructuredBuffer<GcSurvivor> gcSurvivorOut:register(u7);
RWStructuredBuffer<GcSurvivor> gcSurvivorIn:register(u6);
RWByteAddressBuffer gcStageOut:register(u3);
ByteAddressBuffer gcStageIn:register(t54);
// Stage input index for this thread, or false past the stage's count.
// Stage counters per class and round (16 bytes each, reset once per pass).
uint gcStageSlot(){return (gcClassSlot*kGcMaxRounds+gcRunRound)*16;}
bool gcStageItem(uint index,uint capacity){return index<min(gcStageIn.Load(gcStageSlot()+12),capacity);}
// Appends to the next stage (a round never exceeds its capacity).
bool gcStageAppend(uint capacity,out uint index){
 gcStageOut.InterlockedAdd(gcStageSlot()+12,1,index);
 if(index>=capacity)return false;
 if((index&63u)==0)gcStageOut.InterlockedAdd(gcStageSlot(),1);
 return true;
}
float3 gcFlowerColor(uint i){return i==0u?float3(1,1,1):i==1u?float3(1,.85,.3):i==2u?float3(.75,.55,1):float3(1,.55,.6);}
float3 gcDryLeafColor(uint i){return i==0u?float3(.95,.78,.28):i==1u?float3(1,.88,.4):float3(.85,.62,.22);}
float3 gcTreeLeafColor(uint i){return i==0u?float3(.72,.5,.2):i==1u?float3(.82,.62,.24):i==2u?float3(.55,.52,.2):i==3u?float3(.86,.45,.16):float3(.5,.38,.2);}
// The per-frame cell test: its height box (tallest cover after growth, and
// unsampled relief), frustum, nearest eye distance and zones within 8 m.
bool gcCellTest(int2 cell,out float near,out uint zones){
 const float size=gcWindow.w;const float2 lo=cell*size,hi=lo+size;
 float zmin=1e9,zmax=-1e9;
 [loop]for(uint s=0;s<9;++s){
  float2 q=lo+float2(s%3,s/3)*size*.5;
  float g=gcHeight(gcGround,q),w=gcHeight(gcSurface,q);
  zmin=min(zmin,g);zmax=max(zmax,max(g,w));
 }
 zmin-=1;zmax+=3.5;
 float3 bmin=float3(lo,zmin)+gcOrigin.xyz,bmax=float3(hi,zmax)+gcOrigin.xyz;
 bool visible=true;
 [unroll]for(uint k=0;k<6;++k){
  float3 v=float3(gcPlanes[k].x>=0?bmax.x:bmin.x,gcPlanes[k].y>=0?bmax.y:bmin.y,gcPlanes[k].z>=0?bmax.z:bmin.z);
  if(dot(gcPlanes[k].xyz,v)+gcPlanes[k].w<0)visible=false;
 }
 float3 nearest=clamp(gcEye.xyz,bmin,bmax);
 near=length(nearest-gcEye.xyz);
 zones=0;
 if(!(visible&&near<gcView.x&&all(hi>0)&&all(lo<gcMapMetres())))return false;
 uint count,stride;gcZonePieces.GetDimensions(count,stride);
 [loop]for(uint i=0;i<count;++i){
  GcZonePiece piece=gcZonePieces[i];
  if(all(hi+gcOrigin.xy>piece.bounds.xy-8)&&all(lo+gcOrigin.xy<piece.bounds.zw+8))zones|=1u<<piece.zone;
 }
 return true;
}
// Candidate slots of a rule in a cell: stratified by their thinning value,
// so a cell walks only the slots its nearest point can keep.
uint gcCellSlots(GcRule r,float near,uint zones){
 const float size=gcWindow.w;
 return (r.zones&zones)!=0?(uint)ceil(ceil(r.density*gcQuality.y*size*size)*gcKeep(r,near)):0u;
}
// Slots of rule ri walked in a cell: none where the rule can never place.
uint gcCellRuleSlots(int2 cell,uint ri,float near,uint zones){
 return gcRuleMasked(gcCellMasks.Load(int3(cell,0)),ri)?gcCellSlots(gcRules[ri],near,zones):0u;
}
)" R"(
// Stage 1: slot k of rule ri in a cell, up to its coverage test.
bool gcCandidateAt(int2 cell,uint ri,uint k,out GcCandidate c){
 c=(GcCandidate)0;
 const float size=gcWindow.w;const float2 lo=cell*size;
 const float2 mapSize=gcMapMetres();
 const GcRule rule=gcRules[ri];
 uint seed=gcHash(uint(cell.x)*73856093u^uint(cell.y)*19349663u^gcHash(ri*83492791u+k*2654435761u+1u));
 const float expected=rule.density*gcQuality.y*size*size,slots=ceil(expected);
 if(float(k)>=floor(expected)&&gcRand(seed)>=frac(expected))return false;
 const float thin=(k+gcRand(seed))/slots;
 float2 p=lo+float2(gcRand(seed),gcRand(seed))*size;
 // Clustered rules: into the patch of the candidate's lattice cell, or none.
#if GC_F_CLUSTER
 const uint cluster=(uint)rule.cluster;
#else
 const uint cluster=0;
#endif
 uint patchId=0;
 if(cluster!=0){
  const float4 shape=gcClusterShape(cluster);
  const GcPatch pc=gcPatchAt(p,shape.x,shape.yz,cluster*0x9e3779b9u);
  float presence=shape.w;
  if(cluster==1u){ // flowers: open sunny meadow and wood edges, few under trees or beside roads
   const float trees=dot(gcTreeInfluence(pc.centre).rg,1);
   presence*=(1+.6*smoothstep(.02,.2,trees)*(1-smoothstep(.3,.6,trees)))*(1-saturate(trees*1.2-.35));
   bool road=false;
   [loop]for(uint q=0;q<4;++q){
    const float2 nb=q==0?float2(1,0):q==1?float2(-1,0):q==2?float2(0,1):float2(0,-1);
    road=road||gcBaseTypeAt(int2(floor((pc.centre+2.5*nb)*.5)))==7u;
   }
   if(road)presence*=.35;
  }
  if(pc.chance>=presence)return false;
  p=gcPatchPoint(pc,float2(gcRand(seed),gcRand(seed)));patchId=pc.id;
 }
 if(!gcOwnsPoint(p,mapSize))return false;
 const float zoneWeight=gcZoneWeight(p+gcOrigin.xy,rule.zones);
 if(zoneWeight<=0||(zoneWeight<1&&gcRand(seed)>=zoneWeight))return false;
 const float blocked=gcObjectMask.SampleLevel(gcClamp,(p-gcObjectWindow.xy)*gcObjectWindow.zw,0);
 if(blocked>=1||gcRand(seed)<blocked)return false;
 const float ground=gcHeight(gcGround,p),surface=gcHeight(gcSurface,p);
 const float dist=length(float3(p,ground)+gcOrigin.xyz-gcEye.xyz);
 if(dist>=gcView.x)return false;
 const float keep=gcKeep(rule,dist);
 if(thin>=keep)return false;
 const float depth=max(surface-ground,0);
 const bool underwater=((rule.areas>>kGcWater)&1u)!=0;
 // Classified from the surface actually drawn here: the base ground where it
 // is dry, the liquid only where it lies over the ground.
 if(underwater?depth<.05:depth>.02)return false;
 const int2 tile=int2(floor(p*.5));
 const bool beach=rule.field==2; // kelp, shells, conches: ETERRTYPE sand or a sand-family tile
 const uint here=gcSurfaceArea(tile,underwater,beach);
 if(here==0xffu||((rule.areas>>here)&1u)==0)return false;
 if(gcRand(seed)>smoothstep(.2,.75,gcCoverage(rule.areas,p,underwater,beach)))return false;
 c.p=p;c.seed=seed;c.rule=ri;c.thin=thin;c.keep=keep;c.ground=ground;c.depth=depth;c.here=here;c.patch=patchId;
 return true;
}
)" R"(
// Stage 2: the habitat of a candidate.
bool gcHabitatOf(GcCandidate c,out GcSurvivor o){
 o=(GcSurvivor)0;
 const uint ri=c.rule;const GcRule rule=gcRules[ri];
 uint seed=c.seed;const float2 p=c.p;const float ground=c.ground,depth=c.depth;const uint here=c.here;
 const float2 mapSize=gcMapMetres();
#if GC_F_CLUSTER
 const uint cluster=(uint)rule.cluster;
#else
 const uint cluster=0;
#endif
 const bool underwater=((rule.areas>>kGcWater)&1u)!=0;
 const int2 tile=int2(floor(p*.5));
 const uint baseType=gcBaseTypeAt(tile);
 if((rule.flags&16u)!=0&&baseType!=2&&baseType!=4&&baseType!=7&&baseType!=15)return false;
 // Dead bushes belong to soil/field/drygrass, never grass, sand, paving or rock.
 if(rule.mesh==10u&&baseType!=1u&&baseType!=5u&&baseType!=11u)return false;
 // Bare dirt/roads grow only a little grass, predominantly along meadow
 // edges. The same world-space coverage field serves active and neighbour maps.
#if GC_F_EDGE
 if(rule.mesh==0u&&here==0u){
  // The meadow edge is read through a ~2 m world-space warp (low frequency,
  // so a family boundary never lines the tufts up in a row) and the
  // acceptance is scaled by a clump field averaging about 1, so edge tufts
  // gather in irregular clumps at about the same overall amount (save1130:
  // a straight row along zone2's grass/stone tile families).
  const float2 wq=p+gcOrigin.xy;
  float3 edgeNoise=0; // the two warp fields and the clump field, one inlined fbm
  [loop]for(uint f=0;f<3;++f){
   const float v=gcFbm(f==0?wq*.22:f==1?wq*.22+float2(17.3,-9.1):wq*.55,f==0?0x7a11u:f==1?0x51d3u:0x3c6eu);
   if(f==0)edgeNoise.x=v;else if(f==1)edgeNoise.y=v;else edgeNoise.z=v;
  }
  const float2 warp=(edgeNoise.xy-.5)*4;
  const float clumps=2*smoothstep(.35,.65,edgeNoise.z);
  float edge=gcCoverage(1u<<1u,p+warp,false,false);
  if(gcRand(seed)>.01+.99*saturate(edge*clumps))return false;
 }else
#endif
 if((rule.flags&1u)!=0&&!underwater&&gcRand(seed)>(baseType==2||baseType==4||baseType==15?.08:(baseType==7?.3:1)))return false;
 // Grass/road transition tiles keep a grassy ETERRTYPE: meadow and dry
 // grass and flowers thin toward their soil/paving corners, about where the
 // terrain blend shows the road (share .5), with a noisy soft edge.
#if GC_F_EDGE
 if((rule.mesh==0u||cluster==1u)&&(here==1u||here==2u)){
  const float bare=gcBareShare(p);
  if(bare>0){
   const float road=smoothstep(.3,.75,bare+.3*(gcFbm(p*.7,0x5eed1u)-.5));
   if(gcRand(seed)>lerp(1,.03,road))return false;
  }
 }
#endif
 const float4 fields=gcFieldsAt(p);
 const float shore=fields.r*8,swamp=fields.b*8;
 const bool sea=fields.g>.5;
 float accept=1;
 if(rule.field==1)accept=swamp<.05?0:smoothstep(rule.fieldB,rule.fieldB*.35,swamp);
 else if(rule.field==2||rule.field==3){
  float edge=shore;bool outside=false;
#if GC_F_BEACH
  if((rule.flags&32u)!=0){ // shore kelp: irregular clumps, each nearer or farther from the water
   const float4 ks=gcKelpShape();const GcPatch kp=gcPatchAt(p,ks.x,ks.yz,0x4b1d0e57u);
   const float2 d=p-kp.centre;const float a=atan2(d.y,d.x);
   const float lump=1+kGcPatchLump*(.6*sin(2*a+(kp.id&255u)*.0246)+.4*sin(3*a+((kp.id>>8)&255u)*.0246));
   outside=kp.chance>=ks.w||length(d)>kp.radius*lump;
   edge-=(((kp.id>>16)&255u)/255.0*2-1)+(gcRand(seed)-.5);
  }
#endif
  if((rule.field==2)!=sea||shore>=6||outside)accept=0;
  else accept=rule.fieldA>0?smoothstep(rule.fieldA*.4,rule.fieldA,edge)*smoothstep(rule.fieldB,rule.fieldB*.75,edge)
                           :smoothstep(rule.fieldB,rule.fieldB*.3,edge);
 }else if(rule.field==4)accept=sea?smoothstep(rule.fieldA,rule.fieldA+1,depth)*smoothstep(rule.fieldB,rule.fieldB-2,depth):0;
 float4 wall=0;
 if(rule.field==5)accept=smoothstep(rule.fieldB,rule.fieldB*.35,fields.a*8);
#if GC_F_WALL
 else if(rule.field==6){
  float3 art=gcColor.SampleLevel(gcClamp,p/mapSize,0).rgb;
  // Neutral stone/dirt art is not evidence of a dry cave. Wall pockets
  // support moss and understory even away from open water; darker art
  // strengthens that habitat without making the whole floor uniformly lush.
  wall=gcCaveWall(p);
  float wet=max(smoothstep(rule.fieldB,rule.fieldB*.3,shore),smoothstep(rule.fieldB,rule.fieldB*.3,swamp));
  float dark=1-saturate(dot(art,float3(.2126,.7152,.0722)));
  float pocket=smoothstep(rule.fieldB,rule.fieldB*.3,wall.x)*(.35+.65*dark);
  float damp=max(max(wet,pocket),saturate((art.g-max(art.r,art.b))*8));
  accept=fields.a*8<2?0:damp;
 }else if(rule.field==7||rule.field==9){
  wall=gcCaveWall(p);accept=smoothstep(rule.fieldB,rule.fieldB*.3,wall.x);
  if(wall.x<rule.fieldA)accept=0;
  if(rule.field==9)accept*=wall.w;
 }
#endif
 else if(rule.field==8)accept=gcTreesMap.SampleLevel(gcClamp,p/mapSize,0).a;
 if(rule.field!=0&&(accept<=0||gcRand(seed)>=accept))return false;
 if(rule.clumpHi>rule.clumpLo&&gcRand(seed)>smoothstep(rule.clumpLo,rule.clumpHi,gcFbm(p*rule.clumpFreq,ri*131u+7u)))return false;
 const float3 treeKinds=gcTreeInfluence(p);
 const float trees=saturate(treeKinds.r+treeKinds.g);
 const float litter=rule.nearTrees<0?treeKinds.g:(rule.nearTrees==1?treeKinds.r:
  (rule.nearTrees==3?treeKinds.b:saturate(treeKinds.b+treeKinds.g)));
 if(rule.nearTrees!=0&&(litter<=0||gcRand(seed)>=litter))return false;
 float2 slopeDelta=0;
 [loop]for(uint axis=0;axis<2;++axis){
  const float2 d=axis==0?float2(.5,0):float2(0,.5);const float h=gcHeight(gcGround,p-d)-gcHeight(gcGround,p+d);
  if(axis==0)slopeDelta.x=h;else slopeDelta.y=h;
 }
 const float3 n=normalize(float3(slopeDelta,1));
 const bool tuft=(rule.flags&1u)!=0;
 if(tuft&&n.z<.75)return false;
 // Beach wrack (kelp, shells, conches): gentle ground near the water level,
 // never cliffs or banks above the sea.
#if GC_F_BEACH
 if(rule.field==2&&(n.z<kGcKelpSlopeCos||gcAboveWater(p,ground,shore)>kGcKelpAboveWater))return false;
#endif
 const float canopy=saturate(trees*1.2-.35);
 if((rule.mesh==0||rule.mesh==1)&&gcRand(seed)<.8*canopy)return false;
 if(tuft&&rule.field==0&&sea&&gcRand(seed)<smoothstep(3,.8,shore))return false; // beaches stay bare at the waterline
#if GC_F_PILE
 if((rule.flags&2u)!=0){
  if(n.z<.85)return false;
  // Snow piles on snow (never ice or snowy-looking stone art); sand piles on sand.
  if((rule.flags&64u)!=0?gcSoftSandShare(p)<.999:(baseType!=9&&baseType!=12))return false;
 }
#endif
 const float4 painted=gcColor.SampleLevel(gcClamp,p/mapSize,1);
 if(painted.a<.5)return false; // atlas not loaded yet: visibly absent, never a guessed colour
 // Some soil transition tiles carry green grass art despite their soil type.
 if(rule.mesh==10u&&painted.g>painted.r*1.15&&painted.g>painted.b*1.25)return false;
 o.p=p;o.seed=seed;o.rule=ri;o.thin=c.thin;o.keep=c.keep;o.ground=ground;o.depth=depth;o.here=here;o.patch=c.patch;
 o.canopy=canopy;o.swamp=swamp;o.n=n;o.terrain=painted.rgb;o.wall=wall.yz;
 return true;
}

)" R"(
// GC_BUILD_TRAILS 0: neither unit tracks nor snow trails this pass.
#ifndef GC_BUILD_TRAILS
#define GC_BUILD_TRAILS 1
#endif
// Stage 3: the instance of a survivor, appended to its mesh pool.
void gcBuildOf(GcSurvivor c){
 const uint ri=c.rule;const GcRule rule=gcRules[ri];
 uint seed=c.seed;const float2 p=c.p;const float ground=c.ground,depth=c.depth,thin=c.thin,keep=c.keep;
 const uint here=c.here,patchId=c.patch;const float canopy=c.canopy,swamp=c.swamp;const float3 n=c.n,terrain=c.terrain;
#if GC_F_CLUSTER
 const uint cluster=(uint)rule.cluster;
#else
 const uint cluster=0;
#endif
 const bool tuft=(rule.flags&1u)!=0;
 uint mesh=rule.mesh;
 float s=lerp(rule.scaleMin,rule.scaleMax,gcRand(seed));
 float yaw=gcRand(seed)*6.2831853;
#if GC_F_WALL
 if(rule.mesh==22u)yaw=atan2(c.wall.y,c.wall.x);
#endif
 if(mesh==8u)s=lerp(.25,.45,gcRand(seed));
#if GC_F_BEACH
 if((rule.flags&4u)!=0){ // wrack lies along the shoreline
  float gx=gcFieldsAt(p+float2(.5,0)).r-gcFieldsAt(p-float2(.5,0)).r,gy=gcFieldsAt(p+float2(0,.5)).r-gcFieldsAt(p-float2(0,.5)).r;
  yaw=atan2(gy,gx)+1.5708+(gcRand(seed)-.5)*.7;
 }
#endif
 float szs=lerp(rule.szMin,rule.szMax,gcRand(seed));
 float sink=rule.sink*s*szs*.4;
 // Thinned survivors grow (the far LOD clumps stay readable); instances near
 // their threshold grow in and shrink out instead of popping.
 s*=min(1/sqrt(max(keep,.05)),rule.growMax)*smoothstep(thin,min(thin+.12,1.0),keep);
 if((rule.flags&8u)!=0)s=min(s,.85*depth/szs); // stays below the water surface
 if(rule.bury>0)sink=s*szs*(rule.bury-.2*gcRand(seed)); // of the drawn (grown) height: tips above snow
#if GC_F_FAR
 { // Far meshes of the dense and tall tufts, by root leaf width on screen.
  const uint farMesh=mesh==0u?kGcBladesFar:mesh==1u?30u:mesh==11u?31u:mesh==12u?32u:mesh==17u?33u:mesh;
  const float leafWidth=mesh==0u?.12:mesh==1u?.1:mesh==17u?.065:.06;
  if(farMesh!=mesh&&(gcOrigin.w>0||gcTuftUsesFarMesh(keep,gcTuftPixels(gcClip,float3(p,ground)+gcOrigin.xyz,s,gcGrid.w)*leafWidth,
                                        (gcHash(seed^0x5bd1e995u)&0xffffu)/65535.0)))mesh=farMesh;
 }
#endif
 const float j=1+(gcRand(seed)*2-1)*rule.jitter;
 float3 own=rule.own;
 const uint palette=(uint)rule.palette;
 if(false){}
#if GC_F_PAL1
 else if(palette==1){
  if(cluster!=0){ // one species per patch, or a second mixed in
   const uint a=patchId%4u,b=(a+1u+(patchId>>4)%3u)%4u;
   own=gcFlowerColor(gcRand(seed)<(((patchId>>6)&1u)!=0?.35:0)?b:a);
  }else own=gcFlowerColor(uint(gcFbm(p*.2,99u)*7)%4);
 }
#endif
#if GC_F_PAL23
 else if(palette==2&&mesh!=8u){
  const bool yellow=terrain.r>.85*terrain.g&&terrain.g>1.5*terrain.b&&terrain.r+terrain.g>.55;
  const uint dryIndex=uint(gcRand(seed)*3)%3,treeIndex=uint(gcRand(seed)*5)%5;
  own=(here==kGcDry||here==kGcSand||yellow)?gcDryLeafColor(dryIndex):gcTreeLeafColor(treeIndex);
 }else if(palette==3){float g=lerp(.85,1.15,gcRand(seed));own=float3(g,g*lerp(.97,1.02,gcRand(seed)),g*lerp(.93,1.03,gcRand(seed)));}
#endif
 if(mesh==8u)own=1;
 float3 tt=rule.tint*j;
 { // low-frequency drift, darker/olive under canopies and around swamps
  float2 driftNoise=0;
  [loop]for(uint f=0;f<2;++f){const float v=gcFbm(f==0?p*.12:p*.09,f==0?55u:77u);if(f==0)driftNoise.x=v;else driftNoise.y=v;}
  float drift=.88+.24*driftNoise.x,hue=(driftNoise.y-.5)*.24;
  tt*=float3(drift*(1+hue)*(1-.28*canopy),drift*(1-.2*canopy),drift*(1-hue)*(1-.32*canopy));
  if(tuft&&swamp>.05){float e=smoothstep(3.5,.5,swamp);tt*=float3(1-.2*e,1-.12*e,1-.4*e);}
 }
 const float sxs=lerp(rule.sxMin,rule.sxMax,gcRand(seed));
#if GC_F_PILE
 if((rule.flags&2u)!=0){
  bool allSnow=true;float radius=s*max(sxs,1)*1.18;
  [loop]for(uint q=0;q<8;++q){
   uint type=gcBaseTypeAt(int2(floor((p+radius*float2(cos(q*.78539816),sin(q*.78539816)))*.5)));
   float2 q2=p+radius*float2(cos(q*.78539816),sin(q*.78539816));
   allSnow=allSnow&&((rule.flags&64u)!=0?gcSoftSandShare(q2)>=.999:(type==9||type==12));
  }
  if(!allSnow)return;
 }
 if(mesh==13u)sink=gcSnowBurial(gcGround,p,s*max(sxs,1)*1.18,s*szs);
#endif
 const float z=ground-sink;
 if(gcOutside(float3(p,z+.5*s*szs)+gcOrigin.xyz,s*max(sxs,1)+.3))return;
 // Live bodies part the blades around them.
 float2 push=0;float flatten=0;
#if GC_F_TRAIL&&GC_BUILD_TRAILS
 if(tuft||rule.mesh==10u){
  // Visible game tracks and visible live bodies share the recovering mask.
  const float pressed=gcTrailCoverage(p+gcOrigin.xy);
  // Snow trails lay tufts down along the trough, away from its centre line.
  const float2 snowRaw=snowTrailRaw(gcSnowTrail,gcSnowWindow,gcSnowTiles,p+gcOrigin.xy);
  if(snowRaw.x>0){
   const float packed=snowTrailCompaction(snowRaw);
   const float2 rise=gcSnowTrailSlope(p+gcOrigin.xy);const float rl=length(rise);
   push+=(rl>1e-4?rise/rl:float2(cos(yaw),sin(yaw)))*packed;flatten=max(flatten,packed);
  }
  if(pressed>0){
   float2 step=gcView.yz, world=p+gcOrigin.xy;
   float2 gradient=0;
   [loop]for(uint axis=0;axis<2;++axis){
    const float2 d=axis==0?float2(step.x,0):float2(0,step.y);const float g=gcTrailCoverage(world+d)-gcTrailCoverage(world-d);
    if(axis==0)gradient.x=g;else gradient.y=g;
   }
   float l=length(gradient);
   float2 away=l>1e-4?-gradient/l:float2(cos(yaw),sin(yaw));
   push+=away*pressed;flatten=max(flatten,pressed);
  }
  float l=length(push);if(l>1)push/=l;
 }
#endif
 float blend=rule.blendHeight*s*szs,contact=rule.contact,normalBlend=rule.normalBlend;
 if(mesh==8u){blend=.03*s;contact=.35;}
 if(mesh==13u)blend=.08*s*szs*1.6;
 GcInstance o;
 o.root=float4(float3(p,z)+gcOrigin.xyz,s); // world
 o.shape=uint4(gcPack2(cos(yaw),sin(yaw)),gcPack2(sxs,szs),gcPack2(n.x,n.y),gcPack2(push.x,push.y));
 // Tuft bases take the ground colour at the root, once per instance (the
 // colour map's 0.5 m texels do not vary across a tuft) instead of per blade vertex.
 o.color=uint4(gcPackColor(float4(terrain*tt,flatten)),gcPackColor(float4(own*j,rule.ownMix)),
  tuft?gcPackColor(float4(gcTerrainColor(p,0),1)):0u,0);
 // Sun shadow at the tuft's mid height (gap and soft visibility) follows
 // in gcShadeCS from the exact values held here.
 const float3 shadeAt=float3(p,z+.5*s*szs)+gcOrigin.xyz;const float2 shadeSlope=-n.xy/max(n.z,.2);
 const uint4 shade=uint4(asuint(shadeAt.z),asuint(shadeSlope),gcHash(seed^0x9e3779b9u));
 o.extra=uint4(asuint(ground+gcOrigin.z),gcPack2(blend,contact),
  (uint(rule.material)&15u)|(uint(saturate(rule.frost)*15+.5)<<4)|(uint(saturate(rule.align)*15+.5)<<8)|(uint(saturate(normalBlend)*15+.5)<<12)|(tuft?0x10000u:0u),
  gcHash(seed));
 // Pool overflow (the demand readback grows the pool a few frames later)
 // thins a stable subset in the distance thinning's own order instead of
 // dropping whatever the GPU appends last, which differs every frame and
 // flickered. Instances near the cut shrink out and grow back in.
 {
  const uint state=gcPass.x+mesh*8;
  gcOverflow.InterlockedAdd(state,1);
  const float kept=1-asfloat(gcOverflow.Load(state+4)),order=thin/max(keep,1e-6);
  if(order>=kept)return;
  o.root.w*=smoothstep(order,min(order+.12,1.0),kept);
 }
 uint index;gcArgs.InterlockedAdd(mesh*20+4,1,index);
 if(index<gcCapacity[mesh>>2][mesh&3]){gcInstanceOut[gcBase[mesh>>2][mesh&3]+index]=o;gcShadeInput[gcBase[mesh>>2][mesh&3]+index]=shade;}
}

)" R"(
// Every window cell once per frame: visible cells join the job list of each
// class with candidate slots there.
[numthreads(64,1,1)]void gcCellsCS(uint3 id:SV_DispatchThreadID){
 const uint width=uint(gcWindow.z),index=id.x;
 if(index>=width*gcStage.w)return; // gcStage.w: window rows
 const int2 cell=int2(gcWindow.xy)+int2(index%width,index/width);
 float near;uint zones;
 if(!gcCellTest(cell,near,zones))return;
 uint rules,stride;gcRules.GetDimensions(rules,stride);
 // One walk over the rules (the mask read once), slots summed per class.
 const uint2 mask=gcCellMasks.Load(int3(cell,0));
 uint4 low=0;uint2 high=0;
 [loop]for(uint r=0;r<rules;++r){
  if(!gcRuleMasked(mask,r))continue;
  const uint n=gcCellSlots(gcRules[r],near,zones),k=gcRuleClass[r];
  low+=uint4(k==0u?n:0u,k==1u?n:0u,k==2u?n:0u,k==3u?n:0u);high+=uint2(k==4u?n:0u,k==5u?n:0u);
 }
 [loop]for(uint c=0;c<kGcPlaceClasses;++c){
  const uint slots=c==0u?low.x:c==1u?low.y:c==2u?low.z:c==3u?low.w:c==4u?high.x:high.y;
  if(slots==0)continue;
  uint at;gcCellCountOut.InterlockedAdd(c*4,1,at);
  if(at>=gcStage.z)continue; // never: a class holds every window cell
  GcCellJob job;job.cell=uint(cell.x)|(uint(cell.y)<<16);job.near=near;job.zones=zones;job.slots=slots;
  gcCellJobOut[c*gcStage.z+at]=job;
 }
}
// Per class (one group each): the jobs' slot prefix split into rounds of at
// most gcStage.x candidates: a job belongs to the round its first slot falls
// in (gcRound.x apart: the capacity less the largest cell), so a round holds
// at most the split plus one cell. Rounds are contiguous job ranges; each
// gets its dispatch arguments and first job.
groupshared uint gcScanSum[1024];
groupshared uint gcRoundFirst[kGcMaxRounds],gcRoundJobs[kGcMaxRounds];
[numthreads(1024,1,1)]void gcScanCS(uint3 group:SV_GroupID,uint lane:SV_GroupIndex){
 const uint c=group.x,jobs=min(gcCellCounts.Load(c*4),gcStage.z),per=(jobs+1023)/1024,first=lane*per,last=min(first+per,jobs);
 uint sum=0;
 [loop]for(uint j=first;j<last;++j)sum+=gcCellJobs[c*gcStage.z+j].slots;
 gcScanSum[lane]=sum;
 if(lane<kGcMaxRounds){gcRoundFirst[lane]=jobs;gcRoundJobs[lane]=0;}
 GroupMemoryBarrierWithGroupSync();
 // Inclusive prefix of the 1024 partial sums in ten steps, then exclusive.
 [unroll]for(uint step=1;step<1024;step<<=1){
  const uint add=lane>=step?gcScanSum[lane-step]:0u;
  GroupMemoryBarrierWithGroupSync();
  gcScanSum[lane]+=add;
  GroupMemoryBarrierWithGroupSync();
 }
 uint prefix=gcScanSum[lane]-sum,ignored;
 [loop]for(uint j=first;j<last;++j){
  const uint round=min(prefix/gcRound.x,kGcMaxRounds-1); // the renderer keeps the total within the rounds
  InterlockedMin(gcRoundFirst[round],j);InterlockedAdd(gcRoundJobs[round],1,ignored);
  prefix+=gcCellJobs[c*gcStage.z+j].slots;
 }
 GroupMemoryBarrierWithGroupSync();
 if(lane<kGcMaxRounds)gcRoundOut.Store4((c*kGcMaxRounds+lane)*16,uint4(gcRoundJobs[lane],1,1,gcRoundFirst[lane]));
}
[numthreads(64,1,1)]void gcPlaceCS(uint3 group:SV_GroupID,uint lane:SV_GroupIndex){
 const uint jobIndex=gcRounds.Load((gcClassSlot*kGcMaxRounds+gcRunRound)*16+12)+group.x;
 const GcCellJob job=gcCellJobs[gcClassSlot*gcStage.z+jobIndex];
 const int2 cell=int2(job.cell&0xffffu,job.cell>>16);
 const uint ruleCount=gcClassCount;
 if(lane<ruleCount)gcPrefix[lane+1]=gcCellRuleSlots(cell,gcClassRules[lane>>2][lane&3],job.near,job.zones);
 GroupMemoryBarrierWithGroupSync();
 if(lane==0){gcPrefix[0]=0;for(uint k=1;k<=ruleCount;++k)gcPrefix[k]+=gcPrefix[k-1];}
 GroupMemoryBarrierWithGroupSync();
 const uint total=gcPrefix[ruleCount];
 [loop]for(uint item=lane;item<total;item+=64){
  uint li=0;[loop]while(li+1<ruleCount&&gcPrefix[li+1]<=item)++li;
  GcCandidate c;
  if(!gcCandidateAt(cell,gcClassRules[li>>2][li&3],item-gcPrefix[li],c))continue;
  uint index;
  if(!gcStageAppend(gcStage.x,index))continue;
  gcCandidateOut[index]=c;
 }
}
[numthreads(64,1,1)]void gcHabitatCS(uint3 id:SV_DispatchThreadID){
 if(!gcStageItem(id.x,gcStage.x))return;
 GcSurvivor o;
 if(!gcHabitatOf(gcCandidateIn[id.x],o))return;
 uint index;
 if(!gcStageAppend(gcStage.y,index))return;
 gcSurvivorOut[index]=o;
}
[numthreads(64,1,1)]void gcBuildCS(uint3 id:SV_DispatchThreadID){
 if(!gcStageItem(id.x,gcStage.y))return;
 gcBuildOf(gcSurvivorIn[id.x]);
}
// A neighbour map's cells and stage 1 in one dispatch (GC_PLACE_NEIGHBOUR:
// every rule, no trails): per window cell its test, then every rule's slots.
// Its habitat and build stages are the all-rules programs; neighbours' few
// far-mesh candidates do not pay for classes and rounds.
[numthreads(64,1,1)]void gcNeighbourCS(uint3 group:SV_GroupID,uint lane:SV_GroupIndex){
 const int2 cell=int2(gcWindow.xy)+int2(group.xy);
 if(lane==0){float near;uint zones;gcCellVisible=gcCellTest(cell,near,zones)?1u:0u;gcCellNear=near;gcCellZones=zones;}
 GroupMemoryBarrierWithGroupSync();
 if(gcCellVisible==0)return;
 uint rules,stride;gcRules.GetDimensions(rules,stride);
 if(lane<rules)gcPrefix[lane+1]=gcCellRuleSlots(cell,lane,gcCellNear,gcCellZones);
 GroupMemoryBarrierWithGroupSync();
 if(lane==0){gcPrefix[0]=0;for(uint k=1;k<=rules;++k)gcPrefix[k]+=gcPrefix[k-1];}
 GroupMemoryBarrierWithGroupSync();
 const uint total=gcPrefix[rules];
 [loop]for(uint item=lane;item<total;item+=64){
  uint ri=0;[loop]while(ri+1<rules&&gcPrefix[ri+1]<=item)++ri;
  GcCandidate c;
  if(!gcCandidateAt(cell,ri,item-gcPrefix[ri],c))continue;
  uint index;
  if(!gcStageAppend(gcStage.x,index))continue;
  gcCandidateOut[index]=c;
 }
}

)" R"(
// Pool demand for the readback (the largest of the frame's placement passes,
// active map and neighbours; cleared once per frame); draws use the count
// clamped to the pool. The next frame's keep fraction for this pass keeps
// the demand within 95% of the pool, rising slowly once it fits again.
// The pass's placed instances for the per-instance passes (gcShadeCS,
// gcPrepCS): their dispatch arguments (groups, 1, 1), the total, and per
// mesh the first instance index (exclusive prefix of the clamped counts).
// They walk only placed instances, never the whole pool.
RWByteAddressBuffer gcWorkOut:register(u3);
ByteAddressBuffer gcWork:register(t55);
groupshared uint gcWorkCount[64];
[numthreads(64,1,1)]void gcFinalizeCS(uint lane:SV_GroupIndex){
 uint placed=0;
 if(lane<uint(gcGrid.z)){
  const uint capacity=gcCapacity[lane>>2][lane&3],state=gcPass.x+lane*8;
  uint count=gcArgs.Load(lane*20+4),demand=gcOverflow.Load(state),before;
  gcOverflow.Store(state,0);
  const float fit=float(capacity)*.95,target=float(demand)>fit?fit/float(demand):1;
  const float kept=min(target,1-asfloat(gcOverflow.Load(state+4))+kGcOverflowRecover);
  gcOverflow.Store(state+4,asuint(1-saturate(kept)));
  gcCounts.InterlockedMax(lane*4,demand,before);
  placed=min(count,capacity);
  gcArgs.Store(lane*20+4,placed);
 }
 gcWorkCount[lane]=placed;
 GroupMemoryBarrierWithGroupSync();
 if(lane==0){
  uint total=0;
  for(uint m=0;m<uint(gcGrid.z);++m){gcWorkOut.Store(16+m*4,total);total+=gcWorkCount[m];}
  gcWorkOut.Store3(0,uint3((total+63)/64,1,1));gcWorkOut.Store(12,total);
 }
}
// The pool slot of placed instance i (gcFinalizeCS's prefix), or false past the total.
bool gcWorkSlot(uint i,out uint slot){
 slot=0;
 if(i>=gcWork.Load(12))return false;
 uint mesh=0;
 [loop]for(uint m=1;m<uint(gcGrid.z);++m)if(i>=gcWork.Load(16+m*4))mesh=m;
 slot=gcBase[mesh>>2][mesh&3]+i-gcWork.Load(16+mesh*4);
 return true;
}

// Sun shadow per placed instance (after gcFinalizeCS clamped the counts):
// the blocker gap at the tuft's mid height (contact hardening) and the full
// soft visibility with that gap, from the exact placement values.
[numthreads(64,1,1)]void gcShadeCS(uint3 id:SV_DispatchThreadID){
 uint slot;
 if(!gcWorkSlot(id.x,slot))return;
 const uint4 shade=gcShadeInput[slot];const GcInstance inst=gcInstanceOut[slot];
 const float3 at=float3(inst.root.xy,asfloat(shade.x));const float2 slope=asfloat(shade.yz);
 const uint slice=nativeShadowSlice(at);
 const float gap=gcSunGap(slice,at,slope,shade.w);
 float tuftSun=-1;
 [branch]if(gcTuftShadowShare(gap)>0)tuftSun=gcSunVisibilityIn(slice,at,slope,inst.extra.w,gap,8);
 gcInstanceOut[slot].color.w=f32tof16(gap)|(tuftSun<0?0x1000000u:uint(saturate(tuftSun)*255+.5)<<16);
}
// Per instance and frame, everything the vertex stage used to evaluate per
// vertex from the instance position alone: the fog chain and cloud shadow at
// the tuft's mid height, and the wind sway (exactly the per-vertex value: it
// depends on the root and the instance phase only). GC_PREP_NEIGHBOUR:
// neighbour cover, the neighbour piece fog band.
#ifndef GC_PREP_NEIGHBOUR
#define GC_PREP_NEIGHBOUR 0
#endif
// GC_PREP_FOG 0: the game's fog is off this frame; GC_PREP_CLOUD 0: no cloud
// shadow map (sky clouds off).
#ifndef GC_PREP_FOG
#define GC_PREP_FOG 1
#endif
#ifndef GC_PREP_CLOUD
#define GC_PREP_CLOUD 1
#endif
[numthreads(64,1,1)]void gcPrepCS(uint3 id:SV_DispatchThreadID){
 uint slot;
 if(!gcWorkSlot(id.x,slot))return;
 const GcInstance inst=gcInstanceOut[slot];
 const float3 at=inst.root.xyz+float3(0,0,.5*inst.root.w*gcUnpack2(inst.shape.y).y);
 const float4 clip=mul(gcClip,float4(at,1));
 float4 fog=float4(0,0,0,1);
#if GC_PREP_FOG
 float border=1;
 if(gcFogControl.y!=0&&gcFogControl.x>0){
  // Neighbour cover (gcFogRange holds the piece's raw edges) fogs exactly
  // like its terrain and decorations: its band gives way where pieces adjoin.
#if GC_PREP_NEIGHBOUR
  float density=neighbourPieceFogDensity(at.xy,gcFogRange,gcFogControl.x,gcFogSeam,gcFogCurve,gcFogPieces,uint(gcFogPieceInfo.x));
#else
  float density=borderFogDensity(at.xy,gcFogRange,gcFogControl.x,gcFogSeam,gcFogCurve,gcFogSpanLo,gcFogSpanHi);
#endif
  border=1-floor(density*255)/255;
 }
 // The world's fog chain (border fog, atmosphere, draw-distance fade) is
 // affine in the surface colour: run it on black for the added light and keep
 // the product of its transmittances for the colour (and the AO share).
 fog=float4(0,0,0,1);
 if(gcSwitches.y>0&&clip.w>1e-5){
  float3 ndc=clip.xyz/clip.w;
  float4 position=float4(float2(ndc.x*.5+.5,.5-ndc.y*.5)/fogScreen.xy,ndc.z,clip.w);
  float f=nativeFogFactor(border,position);
  float3 added=borderFogColorAt(at,gcFogColor.rgb)*(1-f);
  float kept=f*nativeAtmosphereAt(added,position,gcFogColor.rgb,at);
  fog=float4(added,kept);
 }
#endif
 const float phase=(inst.extra.w&0xffffu)*(6.2831853/65536.0);
 #if GC_PREP_CLOUD
 const float cloud=skyCloudVisibility(at);
#else
 const float cloud=1; // skyCloudVisibility without a cloud map
#endif
 GcDrawInput d;d.fog=fog;d.extra=float4(cloud,gcWindSway(inst.root.xy,gcWind.xy,gcEye.w,gcWind.w,gcWind2.x,phase),0,0);
 gcDrawInputOut[slot]=d;
}
// Terrain colour map: 4 texels per 2 m tile, from each tile's own atlas cell.
[numthreads(8,8,1)]void gcPaintCS(uint3 id:SV_DispatchThreadID){
 uint2 tile=id.xy/gcPaintTexels;
 if(tile.x>=gcPaintTilesX||tile.y>=gcPaintTilesY)return;
 GcTile t=gcTiles[tile.y*gcPaintTilesX+tile.x];
 if(t.material!=gcPaintMaterial)return;
 float2 f=(float2(id.xy%gcPaintTexels)+.5)/gcPaintTexels;
 float3 sum=0;
 [unroll]for(uint b=0;b<4;++b)[unroll]for(uint a=0;a<4;++a){
  float2 g=f+(float2(a,b)+.5)/(4.0*gcPaintTexels)-.5/gcPaintTexels;
  float2 uv=lerp(lerp(t.uv00,t.uv10,g.x),lerp(t.uv01,t.uv11,g.x),g.y);
  sum+=gcAtlas.SampleLevel(gcClamp,uv,0).rgb;
 }
 gcColorOut[id.xy]=float4(sum/16,1);
}
// Per-metre fields: distance to the actual water line (vertices with water over
// the ground) and whether that water is the sea, distance to swamp, snow
// within 2 tiles.
[numthreads(8,8,1)]void gcFieldsCS(uint3 id:SV_DispatchThreadID){
 if(id.x>=uint(gcGrid.x*2)||id.y>=uint(gcGrid.y*2))return;
 float2 p=float2(id.xy)+.5;int2 t=int2(floor(p*.5));
 float shore=8,sea=0,swamp=8,lava=8,nearest=1e9;
 [loop]for(int vy=-6;vy<=7;++vy)[loop]for(int vx=-6;vx<=7;++vx){
  int2 v=int2(id.xy)+int2(vx,vy);
  if(gcVertexDepth(v)<=.05)continue;
  float d=length(float2(v)-p);
  int2 vt=clamp(v/2,int2(0,0),int2(gcGrid.xy)-1);uint liquid=gcAreaAt(vt)&15u;
  if(liquid==kGcSwamp){swamp=min(swamp,max(d-.5,0));continue;}
  if(liquid==7u){lava=min(lava,max(d-.5,0));continue;}
  if(liquid!=kGcWater)continue;
  if(d<nearest){nearest=d;sea=gcSea.Load(int3(vt,0));}
 }
 if(nearest<1e9)shore=max(nearest-.5,0);
 if(gcHeight(gcSurface,p)-gcHeight(gcGround,p)>.05&&(gcAreaAt(t)&15u)==kGcWater)shore=0;
 [loop]for(int y=-4;y<=4;++y)[loop]for(int x=-4;x<=4;++x){
  int2 q=t+int2(x,y);uint v=gcAreaAt(q);if(v==0xffu)continue;
  float2 lo2=float2(q)*2,d2=max(max(lo2-p,p-(lo2+2)),0);
  uint a=v&15u;
  if(a==7u)lava=min(lava,length(d2));
 }
 gcColorOut[id.xy]=float4(shore/8,sea,swamp/8,lava/8);
}

struct GcOut{float4 p:SV_Position;float3 wp:TEXCOORD0;float3 n:TEXCOORD1;float3 alb:TEXCOORD2;float2 uv:TEXCOORD3;
 float frost:TEXCOORD4;float4 gnd:TEXCOORD5;float4 own:TEXCOORD6;nointerpolation uint mat:TEXCOORD7;
 float wrap:TEXCOORD9;nointerpolation float snowHeightScale:TEXCOORD10;float2 groundSlope:TEXCOORD11;
 // Border fog and atmosphere as colour*w+rgb (affine in the surface colour),
 // and the cloud shadow: both vary over metres, evaluated per instance (gcPrepCS).
 nointerpolation float4 fog:TEXCOORD8;nointerpolation float cloud:TEXCOORD12;
 // Sun blocker gap (world metres, 0 none) found once per instance at placement.
 nointerpolation float sunGap:TEXCOORD13;
 // Instance hash: the sun filter disk turns once per tuft (and per frame
 // while the history integrates), so a tuft's pixels read the same texels.
 nointerpolation uint shadowSeed:TEXCOORD14;
 // The tuft's contact-hardening sun visibility (placement); -1 none.
 nointerpolation float sunTuft:TEXCOORD15;
 // Point lamps and emissive glow at the vertex (cover; the snow pile shades
 // per pixel as terrain).
 float3 lamp:TEXCOORD16;float3 glow:TEXCOORD17;};
// Vertex permutations, chosen per draw: GC_VS_SNOW for the snow pile mesh
// (every one of its rules is snow material), GC_VS_POINT_SHADOWS when a lamp
// has a shadow slot this frame.
#ifndef GC_VS_SNOW
#define GC_VS_SNOW 0
#endif
#ifndef GC_VS_POINT_SHADOWS
#define GC_VS_POINT_SHADOWS 0
#endif
#ifndef GC_SNOW_TRAILS
#define GC_SNOW_TRAILS 1
#endif
// gcGroundNormal (ground_cover_surface.h) with one inlined height read
// (p+(-d) is exactly p-d).
float3 gcGroundNormalLoop(Texture2D<float> t,float2 p){
 float4 h=0;
 [loop]for(uint k=0;k<4;++k){
  const float v=gcHeight(t,p+(k==0?float2(-.5,0):k==1?float2(.5,0):k==2?float2(0,-.5):float2(0,.5)));
  if(k==0)h.x=v;else if(k==1)h.y=v;else if(k==2)h.z=v;else h.w=v;
 }
 return normalize(float3(h.x-h.y,h.z-h.w,1));
}
)" R"(
// The vertex's placement: shared verbatim by the material and depth-only
// vertex programs, its position precise, so both rasterize bit-identical
// depth (the material pass tests EQUAL against the depth prepass).
struct GcVertexFrame{GcVertex v;GcInstance inst;GcDrawInput drawInput;float3 root;float scale;float2 yaw;float2 stretch;
 float3 tn;float3 nn;float3 wp;float4 p;};
GcVertexFrame gcVertexFrame(uint vertexId,uint instanceId){
 GcVertex v=gcVertices[vertexId];GcInstance inst=gcInstances[gcDrawBase+instanceId];
 float3 root=inst.root.xyz;float scale=inst.root.w;
 float2 yaw=gcUnpack2(inst.shape.x),stretch=gcUnpack2(inst.shape.y),tn2=gcUnpack2(inst.shape.z),push=gcUnpack2(inst.shape.w);
 float3 tn=float3(tn2,sqrt(saturate(1-dot(tn2,tn2))));
 float4 tint=gcUnpackColor(inst.color.x),own=gcUnpackColor(inst.color.y);
 float ground=asfloat(inst.extra.x);float2 contact=gcUnpack2(inst.extra.y);uint bits=inst.extra.z;
 uint mat=bits&15u;float frost=((bits>>4)&15u)/15.0,align=((bits>>8)&15u)/15.0,normalBlend=((bits>>12)&15u)/15.0;
 bool tuft=(bits&0x10000u)!=0;
 float3 lp=v.p*float3(stretch.x,1,stretch.y);
 float3 r=float3(lp.x*yaw.x-lp.y*yaw.y,lp.x*yaw.y+lp.y*yaw.x,lp.z);
 float3 rn=float3(v.n.x*yaw.x-v.n.y*yaw.y,v.n.x*yaw.y+v.n.y*yaw.x,v.n.z);
 float3 up=normalize(lerp(float3(0,0,1),tn,align));
 float3 tx=normalize(float3(1,0,0)-up*up.x),ty=cross(up,tx);
 precise float3 offset=(r.x*tx+r.y*ty+r.z*up)*scale;
 float3 nn=normalize(rn.x*tx+rn.y*ty+rn.z*up);
 // Wind (the shared renderer wind): gust bands travelling downwind plus a
 // per-instance flutter; the authored bend weight keeps roots planted.
 const GcDrawInput drawInput=gcDrawInputs[gcDrawBase+instanceId];
 float bend=v.ex.y*scale;
 offset.xy+=gcWind.xy*bend*gcWind.z*drawInput.extra.y*(1-tint.a);
 // Units and current game-visible tracks part/press blades (per instance).
 offset.xy+=push*bend*1.6;offset.z-=tint.a*bend*.9;
 // Snow follows the actual height field at every vertex, including the rim;
 // snow trails cut it down to their floor and heap it into their berms.
 if(GC_VS_SNOW){
  offset.xy=r.xy*scale;
  float2 p=root.xy+offset.xy;
#if GC_SNOW_TRAILS
  float2 trail=snowTrailRaw(gcSnowTrail,gcSnowWindow,gcSnowTiles,p);
#else
  float2 trail=0; // snowTrailRaw without a field
#endif
  float3 world=gcSnowPosition(gcGround,p-gcOrigin.xy,r.z*scale,ground-root.z,trail)+gcOrigin.xyz;
  offset=world-root;
  tn=gcGroundNormalLoop(gcGround,world.xy-gcOrigin.xy);
  // Smooth shading normal: the ground slope plus the pile's own analytic
  // slope (mesh normal through its stretch and yaw), giving way to the
  // trail's slope where a trail cuts the pile.
  float2 pile=-v.n.xy/max(v.n.z,.05)*float2(stretch.y/stretch.x,stretch.y);
  pile=float2(pile.x*yaw.x-pile.y*yaw.y,pile.x*yaw.y+pile.y*yaw.x);
  float cut=snowTrailCompaction(trail);
#if GC_SNOW_TRAILS
  float2 slope=-tn.xy/max(tn.z,.05)+(1-cut)*pile+cut*gcSnowTrailSlope(p);
#else
  float2 slope=-tn.xy/max(tn.z,.05)+(1-cut)*pile+cut*float2(0,0);
#endif
  nn=normalize(float3(-slope,1));
 }
 GcVertexFrame f;f.v=v;f.inst=inst;f.drawInput=drawInput;f.root=root;f.scale=scale;f.yaw=yaw;f.stretch=stretch;f.tn=tn;f.nn=nn;
 precise float3 wp=root+offset;precise float4 clip=mul(gcClip,float4(wp,1));
 f.wp=wp;f.p=clip;
 return f;
}
GcOut gcVS(uint vertexId:SV_VertexID,uint instanceId:SV_InstanceID){
 const GcVertexFrame f=gcVertexFrame(vertexId,instanceId);
 const GcVertex v=f.v;const GcInstance inst=f.inst;const GcDrawInput drawInput=f.drawInput;
 const float3 root=f.root,tn=f.tn,nn=f.nn;const float scale=f.scale;const float2 stretch=f.stretch;
 float4 tint=gcUnpackColor(inst.color.x),own=gcUnpackColor(inst.color.y);
 float ground=asfloat(inst.extra.x);float2 contact=gcUnpack2(inst.extra.y);uint bits=inst.extra.z;
 uint mat=bits&15u;float frost=((bits>>4)&15u)/15.0,normalBlend=((bits>>12)&15u)/15.0;
 bool tuft=(bits&0x10000u)!=0;
 GcOut o;o.snowHeightScale=scale*stretch.y;
 // The terrain gradient under the cover: its sun-shadow receiver plane.
 o.groundSlope=-tn.xy/max(tn.z,.2);
 o.wp=f.wp;o.p=f.p;
 float3 below;
 if(tuft)below=gcUnpackColor(inst.color.z).rgb;else below=gcTerrainColor(o.wp.xy-gcOrigin.xy,0);
 o.alb=lerp(tint.rgb,v.c.rgb*own.rgb,saturate(v.c.a+own.a))*v.ex.x;
 o.wrap=.4;
 if(tuft){
  // Blade bases are the ground they grow from: its painted colour (slightly
  // shaded), its normal and its lighting, fading to the blade over the lower
  // third, so no cut line shows where cover meets terrain.
  float base=1-smoothstep(0,.3,v.p.z);
  // Buried tufts (snow grass): the painted snow rises 5 cm up the blades
  // from the snow line, a little snow banked at the base.
  const float buried=ground-root.z;
  if(buried>1e-3)base=1-smoothstep(0,.05,v.p.z*scale*stretch.y-buried);
  o.gnd=float4(below*(buried>1e-3?1:.8),base);
  o.n=normalize(lerp(lerp(nn,tn,normalBlend),tn,base));
  o.wrap=.4*(1-base);
 }else if(GC_VS_SNOW){
  // Plain snow of the zone: the painted colour map's 2 m average (the art
  // under the pile without its texture detail). The pixel shader keeps the
  // ground's own art only along the rim. The normal is smooth (above).
  o.gnd=float4(gcTerrainColor(o.wp.xy-gcOrigin.xy,2),1);
  o.n=nn;
  o.wrap=0;
 }else{
  // Debris takes the painted ground colour at its base and fades to its own
  // colour over the blend height.
  o.gnd=float4(below*.85*v.ex.x,contact.x>0?(1-smoothstep(0,1,saturate((o.wp.z-ground)/contact.x)))*contact.y:0);
  o.n=normalize(lerp(nn,tn,normalBlend));
 }
 o.own=float4(own.rgb,v.ex.x);o.mat=mat|(bits&0x10000u);o.uv=v.uv;o.frost=frost;
 o.fog=drawInput.fog;o.cloud=drawInput.extra.x;
 // Point lamps per vertex: the pixel loop's terms at the vertex normal.
 float3 lamp=0,glow=0;
#if !GC_VS_SNOW
 {
  const float3 n=o.n;
  uint2 lightRange=nativePointLightRange(o.wp);
  [loop]for(uint k=lightRange.x;k<lightRange.y;++k){
   NativePointLight light=nativePointLights[nativePointLightGrid[k]];
   float3 delta=light.centerInverseRadiusSquared.xyz-o.wp;float d2=dot(delta,delta);
   float reachLight=1-d2*light.centerInverseRadiusSquared.w;if(reachLight<=0)continue;
   float incidence=dot(n,delta*rsqrt(max(d2,1e-12)));
   if(light.color.w<0){glow+=light.color.rgb*(reachLight*reachLight*saturate(incidence*.6+.4));continue;} // emissive surfaces
   float wrapFacing=saturate((incidence+.4)/1.4);if(wrapFacing<=0)continue;
   // Only the current grid cell's reaching lights with an active shadow slot
   // sample cubes. Fixed PCF, no blocker search or point temporal history.
   float pointVisibility=1;
#if GC_VS_POINT_SHADOWS
   [branch]if(light.shadowSlots.x>0&&light.shadowSlots.z>0)
    pointVisibility=nativePointVisibility(light,o.wp,n,incidence,0);
#endif
   lamp+=light.color.rgb*(reachLight*reachLight*wrapFacing*pointVisibility*(light.color.w>0?.65:1));
  }
 }
#endif
 o.lamp=lamp;o.glow=glow;
 o.sunGap=f16tof32(inst.color.w&0xffffu);
 o.shadowSeed=inst.extra.w;
 o.sunTuft=(inst.color.w&0x1000000u)!=0?-1:((inst.color.w>>16)&255u)/255.0;
 return o;
}
// Depth prepass: the same placement, only what the alpha test reads.
struct GcDepthOut{float4 p:SV_Position;float3 wp:TEXCOORD0;nointerpolation float snowHeightScale:TEXCOORD10;};
GcDepthOut gcDepthVS(uint vertexId:SV_VertexID,uint instanceId:SV_InstanceID){
 const GcVertexFrame f=gcVertexFrame(vertexId,instanceId);
 GcDepthOut o;o.p=f.p;o.wp=f.wp;o.snowHeightScale=f.scale*f.stretch.y;return o;
}
float3 gcRectSample(Texture2D t,float4 rect,float2 uv){
 return t.SampleGrad(gcWrap,rect.xy+frac(uv)*rect.zw,ddx(uv)*rect.zw,ddy(uv)*rect.zw).rgb;
}
)" R"(
struct GcPixel{float4 color:SV_Target0;float4 shadow:SV_Target1;float4 ambient:SV_Target2;};
// Pixel permutations, chosen per draw from the mesh's rule materials:
// GC_PS_KIND 0 procedural colour (plain, ground, wet, glow), 1 bark,
// 2 stone, 3 snow pile (terrain lighting per pixel; NATIVE_LOW when no lamp
// has a shadow slot this frame). Cover lamps come from the vertex stage.
#ifndef GC_PS_KIND
#define GC_PS_KIND 0
#endif
// GC_SUN 0: sun shadows off (shadows setting, or no enabled map); GC_AO 0:
// no ambient occlusion share written (ao off); GC_SNOW_TRAILS 0: no snow
// trail field this frame.
#ifndef GC_SUN
#define GC_SUN 1
#endif
#ifndef GC_AO
#define GC_AO 1
#endif
#ifndef GC_SNOW_TRAILS
#define GC_SNOW_TRAILS 1
#endif
// GC_SUN_MASK: the depth prepass and the world's mask pass took the sun and
// cloud shadow of this pixel (t5, RG16F: x combined, y sun only; 2 = not
// shaded); the material pass runs with depth EQUAL.
#ifndef GC_SUN_MASK
#define GC_SUN_MASK 0
#endif
#if GC_SUN_MASK
Texture2D<float2> gcShadowMask:register(t5);
#endif
// The pixel's sun visibility (-1: an enabled map missing, shown as an
// error), with the temporal history; the one place a pixel shadow is read.
float gcPixelSunVisibility(GcOut i,float3 sun,float3 positionDx,float3 positionDy){
 float visibility=1;
#if GC_SUN
 [branch]if(shadowControl.x!=0||shadowBias[4].z>.5){
#if GC_PS_KIND==3
  visibility=nativeShadowVisibility(i.wp,positionDx,positionDy,i.p.xy,true);
#else
  {
   // Wide penumbrae from the tuft's contact-hardening value (placement);
   // contact-hard edges from this pixel's fixed small disk (the softness-0
   // filter: a wide disk per pixel missed the texture cache on every tap).
   float share=i.sunTuft<0?0:gcTuftShadowShare(i.sunGap);
   visibility=share>=1?i.sunTuft:gcSunVisibility(i.wp+sun*.05,i.groundSlope,i.shadowSeed,0,gcSunSoft()&&shadowBias[5].z>1.5?8:4);
   if(visibility>=0&&share>0&&share<1)visibility=lerp(visibility,i.sunTuft,share);
  }
#endif
  // The world's temporal history (cover writes it too): the visibility of
  // the same world point last frame, so swaying blades reproject correctly.
  // Exact pixel read: blades are a pixel or two wide, and a filtered read
  // would mix in the ground behind them.
  float2 historyUv;int2 historyPixel;
  [branch]if(visibility>=0&&all(isfinite(positionDx))&&all(isfinite(positionDy))&&
     nativeShadowHistoryAt(i.wp,positionDx,positionDy,historyUv,historyPixel))
   visibility=lerp(visibility,previousShadowVisibility.Load(int3(historyPixel,0)),shadowHistorySettings.z);
 }
#endif
 return visibility;
}
// The cover's alpha test, shared by the depth prepass and the material pass:
// fragments over a prop's current footprint (dithered over its margin) and,
// for the snow pile, buried fragments.
void gcCoverClip(float3 wp,float2 pixel,float snowHeightScale){
 // A surviving centre can still have blades or debris crossing a prop. Clip
 // their fragments against the same current footprint, including its margin.
 float blocked=gcObjectMask.SampleLevel(gcClamp,(wp.xy-gcObjectWindow.xy)*gcObjectWindow.zw,0);
 uint2 footprintPixel=uint2(int2(floor(wp.xy*64)));
 float footprintThreshold=((gcHash(footprintPixel.x*73856093u^footprintPixel.y*19349663u)&65535u)+.5)/65536;
 clip(1-blocked-footprintThreshold);
#if GC_PS_KIND==3
 // Reject buried fragments even between vertices on a terrain crease. Dither
 // the remaining intersection; discarded pixels retain terrain depth/colour.
 float rise=wp.z-gcOrigin.z-gcHeight(gcGround,wp.xy-gcOrigin.xy);
 clip(gcSnowCoverage(rise,snowHeightScale)-gcSnowDither(uint2(pixel)));
#endif
}
// The depth prepass writes the world's sun-shadow receiver record (renderer
// resolve_world_shadow_mask, native_shader.h nativeShadowMaskPS): target 0
// the rest (shadow-lookup) position minus the point reconstructed from the
// D24-rounded depth, w flags (1 receiver, 2 temporal history); target 1 the
// terrain-normal offset normal (none: the octahedral centre). The lookup is
// the cover's own: 5 cm toward the sun for cover, the surface for the snow
// pile, both with the temporal history.
float gcDepth24(float z){return round(saturate(z)*16777215.0)/16777215.0;}
struct GcDepthResult{float4 rest:SV_Target0;float2 normal:SV_Target1;};
GcDepthResult gcDepthPS(GcDepthOut i){
 gcCoverClip(i.wp,i.p.xy,i.snowHeightScale);
 float3 rest=i.wp;
#if GC_PS_KIND!=3
 if(dot(nativeWorldSunDirection.xyz,nativeWorldSunDirection.xyz)>1e-12)rest+=normalize(nativeWorldSunDirection.xyz)*.05;
#endif
 GcDepthResult r;
 r.rest=float4(rest-fogUnproject(i.p.xy*fogScreen.xy,gcDepth24(i.p.z)),3);
 r.normal=float2(.5,.5);
 return r;
}
GcPixel gcPS(GcOut i){
 float3 positionDx=ddx(i.wp),positionDy=ddy(i.wp);
 gcCoverClip(i.wp,i.p.xy,i.snowHeightScale);
 // Thin tufts keep no AO share: the horizon AO of their own blades is noise,
 // while the ground between them still darkens from the blades' depth.
 const bool thin=(i.mat&0x10000u)!=0;i.mat&=15u;
 float3 n=normalize(i.n);
 float3 alb=i.alb;
#if GC_PS_KIND==3
 {
  float2 p=i.wp.xy-gcOrigin.xy;
  float rise=i.wp.z-gcOrigin.z-gcHeight(gcGround,p);
  // Smooth pile normal; the deformed triangles' own normal only where a
  // trail cuts the pile (its walls are steeper than the mesh resolves).
#if GC_SNOW_TRAILS
  const float2 trailRaw=snowTrailRaw(nativeSnowTrail,gcSnowWindow,gcSnowTiles,i.wp.xy);
#else
  const float2 trailRaw=0; // snowTrailRaw without a field
#endif
  float cut=snowTrailCompaction(trailRaw);
  n=normalize(lerp(n,gcSnowNormal(gcGroundNormalLoop(gcGround,p),positionDx,positionDy,rise,i.snowHeightScale),cut));
  float3 art;
  // Missing terrain art is visible, never disguised by the coarse colour map.
  if(!contactTerrainArtMap(p,positionDx.xy,positionDy.xy,gcSnowMaterials.xy,art)){
   GcPixel missing;missing.color=float4(1,0,1,1);missing.shadow=0;missing.ambient=0;return missing;
  }
  i.gnd.rgb=lerp(art,i.gnd.rgb,smoothstep(0,.06*i.snowHeightScale,rise))*
   snowTrailPackedTone(trailRaw);
 }
#endif

#if GC_PS_KIND==1
 { // the game's tree bark and log end grain
  // Missing art must be visible, never disguised as plain-coloured wood.
  if((uint(gcSwitches.w)&1u)==0){GcPixel missing;missing.color=float4(1,0,1,1);missing.shadow=0;missing.ambient=0;return missing;}
  float3 tex=i.uv.x>5?gcRectSample(gcBark,gcEndRect,saturate(i.uv-10)*.999):gcRectSample(gcBark,gcBarkRect,i.uv)*1.35;
  alb=tex*i.own.rgb*i.own.a;
 }
#elif GC_PS_KIND==0
 if(i.mat==4u||i.mat==5u){
  // Slab fragments and stains use the painted cave tile art and its colour.
  alb=lerp(alb,gcTerrainColor(i.wp.xy-gcOrigin.xy,0),.35);
  if(i.mat==5u)alb*=.5;
 }
#elif GC_PS_KIND==2
 if((uint(gcSwitches.w)&2u)!=0){ // the rock art of the placed stone props, triplanar
  float3 w3=pow(abs(n),4);w3/=dot(w3,1);
  float3 st=gcRectSample(gcStone,gcStoneRect,i.wp.xy*.45)*w3.z+gcRectSample(gcStone,gcStoneRect,i.wp.xz*.45)*w3.y+
            gcRectSample(gcStone,gcStoneRect,i.wp.yz*.45)*w3.x;
  alb=st*1.1*i.own.rgb*i.own.a;
 }
#endif
 alb=lerp(alb,i.gnd.rgb,i.gnd.w);
 alb=lerp(alb,float3(.92,.95,1),i.frost*saturate(n.z*1.8-.6));
 float3 sun=dot(nativeWorldSunDirection.xyz,nativeWorldSunDirection.xyz)>1e-12?normalize(nativeWorldSunDirection.xyz):float3(0,0,1);
 // The terrain's own facing (or its full-sun comparison) blended to wrapped
 // light for thin cover above the base.
 float terrainFacing=gcWind2.z>0?1:saturate(dot(n,sun));
 float facing=lerp(terrainFacing,saturate((dot(n,sun)+.4)/1.4),saturate(i.wrap/.4));
#if GC_SUN_MASK
 const float2 shadowMask=gcShadowMask.Load(int3(i.p.xy,0));
 float visibility=shadowMask.x>1.5?1:shadowMask.x,sunVisibility=shadowMask.x>1.5?1:shadowMask.y;
#else
 float visibility=gcPixelSunVisibility(i,sun,positionDx,positionDy);
 // Cloud shadow joins the sun shadow as one visibility (min), as on world
 // surfaces; the shadow target keeps the sun term only, like nativePS.
 float sunVisibility=visibility;
 if(visibility>=0)visibility=min(visibility,i.cloud);
#endif
 float3 local=0,glow=0;
 #if GC_PS_KIND==3
 {
  // Snow is terrain: use its grazing-light response and full point-shadow
  // filter, rather than the wrapped, reduced-tap foliage receiver.
  NativeOutput terrain=(NativeOutput)0;
  terrain.worldPosition=i.wp;terrain.shadowPosition=i.wp;terrain.worldNormal=n;
  terrain.pointMaterial=1;
  local=nativePointLighting(terrain,glow);
 }
#else
 local=i.lamp;glow=i.glow;
#endif
 GcPixel result;
 if(visibility<0){result.color=float4(1,0,1,1);result.shadow=float4(1,0,0,1);result.ambient=0;return result;}
 float3 directional=nativeWorldSunDiffuse.rgb*facing*visibility;
 float3 c=alb*(saturate(max(nativeWorldAmbient.rgb,directional)+local)+glow);
 #if GC_PS_KIND==0
 if(i.mat==6u)c+=i.alb*float3(.08,.24,.17);
#endif
 // Reduced AO on debris: full horizon AO darkened its rims and made it stand out.
 #if GC_AO
 float4 share=aoSurfaceShare(c,alb*(saturate(directional+local)+glow),thin,GC_PS_KIND==3);
#else
 const float4 share=0; // no ambient target bound
#endif
 float3 ambientShare=share.rgb;
 c=c*i.fog.w+i.fog.rgb;ambientShare*=i.fog.w;
 result.color=float4(c,1);result.shadow=float4(sunVisibility,0,0,1);result.ambient=float4(ambientShare,share.a);
 return result;
}
)";

// ---------------------------------------------------------------- meshes
// Independently authored procedural meshes of the approved prototype
// (Prototypes/ground_cover/ground_cover.cpp, C-v4), unit-sized.
float lattice(int x, int y, std::uint32_t seed) {
    return mesh_hash(std::uint32_t(x) * 73856093u ^ std::uint32_t(y) * 19349663u ^ seed) * (1.0f / 4294967296.0f);
}
float value_noise(float x, float y, std::uint32_t seed) {
    const int ix = int(std::floor(x)), iy = int(std::floor(y));
    float fx = x - ix, fy = y - iy;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
    const float a = lattice(ix, iy, seed), b = lattice(ix + 1, iy, seed), c = lattice(ix, iy + 1, seed), d = lattice(ix + 1, iy + 1, seed);
    return (a + (b - a) * fx) * (1 - fy) + (c + (d - c) * fx) * fy;
}

// Tuft of curved tapered blades: darker root (ao), tip slightly own-coloured, wind ~ height^2.
GcMesh make_blades(int blades, float width, std::uint32_t seed, float tip_mix, V3 tip, float spread = .3f, int segs = 3) {
    GcMesh m; Rng r(seed);
    for (int b = 0; b < blades; ++b) {
        // Roots spread over a disc and lean in mixed directions: a full base,
        // not a fan from one point.
        const float a = r.r(0, 6.283f), rad = std::sqrt(r.f()) * spread, hgt = r.r(0.6f, 1.0f);
        const float lean = r.r(0.05f, 0.35f), la = a + r.r(-1.3f, 1.3f), face = la + r.r(-0.6f, 0.6f) + 1.57f;
        const float bx = std::cos(a) * rad, by = std::sin(a) * rad, dx = std::cos(la), dy = std::sin(la);
        const float fx = std::cos(face), fy = std::sin(face);
        const auto base = std::uint32_t(m.v.size());
        for (int s = 0; s <= segs; ++s) {
            const float t = float(s) / segs, w = width * (1 - t * 0.85f) * 0.5f;
            const float cx = bx + dx * lean * t * t * hgt, cy = by + dy * lean * t * t * hgt, cz = hgt * (t - 0.18f * t * t * lean);
            const V3 n{-dx * 0.4f + fy * 0.2f, -dy * 0.4f - fx * 0.2f, 0.9f};
            const float col[4]{tip.x, tip.y, tip.z, tip_mix * t * t};
            const float ao = 0.68f + 0.6f * t;
            if (s == segs) { add(m, {cx, cy, cz}, n, col, .5f, t, ao, t * t * hgt); break; }
            add(m, {cx - fx * w, cy - fy * w, cz}, n, col, 0, t, ao, t * t * hgt);
            add(m, {cx + fx * w, cy + fy * w, cz}, n, col, 1, t, ao, t * t * hgt);
        }
        for (int s = 0; s < segs - 1; ++s) { const std::uint32_t q = base + s * 2; tri(m, q, q + 1, q + 2); tri(m, q + 1, q + 3, q + 2); }
        const std::uint32_t q = base + (segs - 1) * 2; tri(m, q, q + 1, q + 2);
    }
    return m;
}

GcMesh make_ico(std::uint32_t seed, float noise, float z_scale, bool faceted, V3 col, float mix) {
    const float t = 1.618034f;
    std::vector<V3> v = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t},
                         {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
    std::vector<std::uint32_t> f = {0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11, 1, 5, 9, 5, 11, 4, 11, 10, 2,
                                    10, 7, 6, 7, 1, 8, 3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9, 4, 9, 5,
                                    2, 4, 11, 6, 2, 10, 8, 6, 7, 9, 8, 1};
    for (auto& p : v) p = unit3(p);
    std::vector<std::uint32_t> nf;
    const auto mid = [&](std::uint32_t a, std::uint32_t b) {
        const V3 m = unit3(v[a] + v[b]);
        for (std::uint32_t i = 12; i < v.size(); ++i)
            if (std::fabs(v[i].x - m.x) + std::fabs(v[i].y - m.y) + std::fabs(v[i].z - m.z) < 1e-5f) return i;
        v.push_back(m); return std::uint32_t(v.size() - 1);
    };
    for (std::size_t i = 0; i < f.size(); i += 3) {
        const std::uint32_t a = f[i], b = f[i + 1], c = f[i + 2], ab = mid(a, b), bc = mid(b, c), ca = mid(c, a);
        const std::uint32_t q[] = {a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca};
        nf.insert(nf.end(), q, q + 12);
    }
    f.swap(nf);
    Rng r(seed);
    for (auto& p : v) {
        const float k = 1.0f + noise * (value_noise(p.x * 1.7f + 5, p.y * 1.7f + p.z * 1.3f, seed) * 2 - 1) + noise * 0.3f * (r.f() - 0.5f);
        p.x *= k; p.y *= k * 0.85f; p.z = std::max(p.z * k * z_scale, -0.15f * z_scale);
    }
    GcMesh m;
    const float c[4]{col.x, col.y, col.z, mix};
    const auto ao_at = [&](float z) { return 0.62f + 0.5f * std::clamp(z / z_scale, 0.0f, 1.0f); };
    if (faceted) {
        for (std::size_t i = 0; i < f.size(); i += 3) {
            V3 n = unit3(cross3(v[f[i + 1]] - v[f[i]], v[f[i + 2]] - v[f[i]]));
            if (dot3(n, v[f[i]]) < 0) n = n * -1;
            const auto base = std::uint32_t(m.v.size());
            for (int k = 0; k < 3; ++k) add(m, v[f[i + k]], n, c, 0, 0, ao_at(v[f[i + k]].z), 0);
            tri(m, base, base + 1, base + 2);
        }
    } else {
        std::vector<V3> acc(v.size(), V3{0, 0, 0});
        for (std::size_t i = 0; i < f.size(); i += 3) {
            const V3 n = cross3(v[f[i + 1]] - v[f[i]], v[f[i + 2]] - v[f[i]]);
            for (int k = 0; k < 3; ++k) acc[f[i + k]] = acc[f[i + k]] + n;
        }
        for (std::size_t i = 0; i < v.size(); ++i) {
            V3 n = unit3(acc[i]);
            if (dot3(n, v[i]) < 0) n = n * -1;
            add(m, v[i], n, c, 0, 0, ao_at(v[i].z), 0);
        }
        m.i = f;
    }
    return m;
}

GcMesh make_leaf() {
    GcMesh m;
    const float c[4]{1, 1, 1, .85f};
    add(m, {0, 0, 0.03f}, {0, 0, 1}, c, 0, 0, 1.0f, 0);
    const int n = 10;
    for (int k = 0; k < n; ++k) {
        const float a = k * 6.283f / n;
        add(m, {0.5f * std::cos(a), 0.22f * std::sin(a) * (1 - 0.3f * std::cos(a)), 0.012f + 0.01f * std::fabs(std::sin(a))}, {0, 0, 1}, c, 0, 0, 0.9f, 0);
    }
    for (int k = 0; k < n; ++k) tri(m, 0, 1 + k, 1 + (k + 1) % n);
    return m;
}
// Far flower: two two-segment blades and the same three heads as five-petal
// fans, without stems (sub-pixel at that size).
GcMesh make_flower(std::uint32_t seed, bool distant = false) {
    GcMesh m = distant ? make_blades(2, 0.08f, seed, 0.0f, {0, 0, 0}, .14f, 2) : make_blades(4, 0.05f, seed, 0.0f, {0, 0, 0}, .14f);
    Rng r(seed + 3);
    for (int k = 0; k < 3; ++k) {
        const float cx = r.r(-0.12f, 0.12f), cy = r.r(-0.12f, 0.12f), cz = r.r(0.75f, 1.05f), rad = r.r(0.13f, 0.18f);
        const auto c = std::uint32_t(m.v.size());
        const float centre[4]{1.0f, 0.9f, 0.45f, 1.0f}, petal[4]{1, 1, 1, 1.0f};
        add(m, {cx, cy, cz + 0.01f}, {0, 0, 1}, centre, 0, 0, 1.15f, cz * cz);
        const int petals = distant ? 5 : 10;
        for (int s = 0; s < petals; ++s) {
            const float a = s * 6.283f / petals, rr = distant ? rad * .85f : rad * (s % 2 ? 0.55f : 1.0f);
            add(m, {cx + std::cos(a) * rr, cy + std::sin(a) * rr, cz}, {0, 0, 1}, petal, 0, 0, 1.1f, cz * cz);
        }
        for (int s = 0; s < petals; ++s) tri(m, c, c + 1 + s, c + 1 + (s + 1) % petals);
        if (!distant) add_prism(m, {{cx * 0.3f, cy * 0.3f, 0}, {cx, cy, cz}}, 0.012f, 0.008f, 3, {0.3f, 0.45f, 0.2f}, 0.0f, false, {});
    }
    return m;
}
// EI-style sedge/reed clump in the shipped reed colours (dark root, yellow-green highlights).
GcMesh make_reeds(std::uint32_t seed, int leaves, int segs = 4, float width_scale = 1) {
    GcMesh m; Rng r(seed);
    const V3 dark{0.07f, 0.13f, 0.015f}, light{0.24f, 0.36f, 0.06f};
    for (int b = 0; b < leaves; ++b) {
        const float a = r.r(0, 6.283f), rad = r.r(0.0f, 0.1f), hgt = r.r(0.55f, 1.0f), lean = r.r(0.3f, 0.95f);
        const float face = a + 1.57f + r.r(-0.3f, 0.3f), w0 = r.r(0.05f, 0.075f);
        const float bx = std::cos(a) * rad, by = std::sin(a) * rad, dx = std::cos(a), dy = std::sin(a), fx = std::cos(face), fy = std::sin(face);
        const auto base = std::uint32_t(m.v.size());
        for (int s = 0; s <= segs; ++s) {
            const float t = float(s) / segs, w = w0 * width_scale * (1 - t * 0.9f) * 0.5f;
            const float cx = bx + dx * lean * t * t * hgt * 0.8f, cy = by + dy * lean * t * t * hgt * 0.8f, cz = hgt * (t - 0.45f * t * t * lean);
            const V3 n{-dx * 0.3f, -dy * 0.3f, 0.95f};
            const float k = std::min(1.0f, t * 1.3f);
            const float col[4]{dark.x + (light.x - dark.x) * k, dark.y + (light.y - dark.y) * k, dark.z + (light.z - dark.z) * k, 0.85f};
            if (s == segs) { add(m, {cx, cy, cz}, n, col, .5f, t, 0.75f + 0.4f * t, t * t * hgt); break; }
            add(m, {cx - fx * w, cy - fy * w, cz}, n, col, 0, t, 0.75f + 0.4f * t, t * t * hgt);
            add(m, {cx + fx * w, cy + fy * w, cz}, n, col, 1, t, 0.75f + 0.4f * t, t * t * hgt);
        }
        for (int s = 0; s < segs - 1; ++s) { const std::uint32_t q = base + s * 2; tri(m, q, q + 1, q + 2); tri(m, q + 1, q + 3, q + 2); }
        const std::uint32_t q = base + (segs - 1) * 2; tri(m, q, q + 1, q + 2);
    }
    return m;
}
// Far cattail: three wide two-segment leaves, three of the stalks with
// three-sided shafts and four-sided heads.
GcMesh make_cattail(std::uint32_t seed, bool distant = false) {
    GcMesh m = distant ? make_reeds(seed + 1, 3, 2, 1.8f) : make_reeds(seed + 1, 5);
    Rng r(seed);
    for (int k = 0; k < (distant ? 3 : 4); ++k) {
        const float x = r.r(-0.08f, 0.08f), y = r.r(-0.08f, 0.08f), h = r.r(0.95f, 1.3f), lx = r.r(-0.08f, 0.08f), ly = r.r(-0.08f, 0.08f);
        if (distant) add_prism(m, {{x, y, 0}, {x + lx, y + ly, h}}, 0.012f, 0.008f, 3, {0.36f, 0.32f, 0.12f}, 0.9f, false, {});
        else add_prism(m, {{x, y, 0}, {x + lx * 0.5f, y + ly * 0.5f, h * 0.5f}, {x + lx, y + ly, h}}, 0.012f, 0.008f, 4, {0.36f, 0.32f, 0.12f}, 0.9f, false, {});
        const float hz = h * r.r(0.72f, 0.8f);
        add_prism(m, {{x + lx * hz / h, y + ly * hz / h, hz}, {x + lx * (hz + 0.17f) / h, y + ly * (hz + 0.17f) / h, hz + 0.17f}},
                  0.034f, 0.03f, distant ? 4 : 6, {0.29f, 0.21f, 0.1f}, 0.95f, true, {0.33f, 0.24f, 0.12f});
    }
    // Stalks bend with the leaves: weight by height like the blades.
    for (auto& v : m.v) if (v.ex[1] == 0) v.ex[1] = std::max(v.p[2], 0.0f) * std::max(v.p[2], 0.0f);
    return m;
}
GcMesh make_shell() {
    GcMesh m;
    const int rings = 4, sectors = 14;
    const float top[4]{0.93f, 0.86f, 0.74f, 0.9f};
    add(m, {0, 0, 0.3f}, {0, 0, 1}, top, 0, 0, 1.05f, 0);
    for (int r = 1; r <= rings; ++r) {
        const float rr = float(r) / rings;
        for (int k = 0; k < sectors; ++k) {
            const float a = 3.14159f * (0.05f + 0.9f * k / (sectors - 1)), rib = 1.0f + 0.06f * std::cos(k * 3.14159f);
            const float x = std::cos(a) * rr * 0.5f * rib, y = std::sin(a) * rr * 0.5f * rib - 0.2f, z = 0.3f * (1 - rr * rr) + 0.01f;
            const float t = rr > 0.9f ? 0.45f : 1.0f - 0.2f * rr; // pale body, darker lip so it reads on sand
            const float c[4]{t, t * 0.84f, t * 0.8f, 1.0f};
            add(m, {x, y, z}, unit3({x, y + 0.2f, 0.5f}), c, 0, 0, 0.8f + 0.25f * (1 - rr), 0);
        }
    }
    for (int k = 0; k + 1 < sectors; ++k) tri(m, 0, 1 + k, 1 + k + 1);
    for (int r = 1; r < rings; ++r)
        for (int k = 0; k + 1 < sectors; ++k) {
            const std::uint32_t a = 1 + (r - 1) * sectors + k, b = a + 1;
            tri(m, a, a + sectors, b); tri(m, b, a + sectors, b + sectors);
        }
    return m;
}
GcMesh make_conch() {
    GcMesh m;
    std::vector<V3> pts;
    for (int k = 0; k <= 10; ++k) {
        const float t = k / 10.0f, a = t * 7.0f;
        pts.push_back({t - 0.5f, 0.08f * (1 - t) * std::cos(a), 0.12f + 0.08f * (1 - t) * std::sin(a)});
    }
    add_prism(m, pts, 0.16f, 0.015f, 8, {0.9f, 0.76f, 0.6f}, 0.9f, true, {0.95f, 0.62f, 0.58f});
    return m;
}
GcMesh make_kelp(std::uint32_t seed) {
    GcMesh m; Rng r(seed);
    for (int s = 0; s < 6; ++s) {
        const float y0 = r.r(-0.18f, 0.18f), ph = r.r(0, 6.28f), w = r.r(0.05f, 0.09f), len = r.r(0.6f, 1.0f), tone = r.r(0.85f, 1.15f);
        const float col[4]{0.3f * tone, 0.27f * tone, 0.11f * tone, 0.9f};
        const auto base = std::uint32_t(m.v.size());
        const int segs = 10;
        for (int k = 0; k <= segs; ++k) {
            const float t = float(k) / segs, x = (t - 0.5f) * len, y = y0 + 0.05f * std::sin(t * 9 + ph);
            const float z = 0.008f + 0.01f * std::fabs(std::sin(t * 5 + ph));
            add(m, {x, y - w, z}, {0, 0, 1}, col, 0, 0, 0.95f, 0);
            add(m, {x, y + w, z}, {0, 0, 1}, col, 0, 0, 0.95f, 0);
        }
        for (int k = 0; k < segs; ++k) { const std::uint32_t q = base + k * 2; tri(m, q, q + 1, q + 2); tri(m, q + 1, q + 3, q + 2); }
    }
    return m;
}
GcMesh make_seagrass(std::uint32_t seed, int blades = 9, int segs = 5, float width_scale = 1) {
    GcMesh m; Rng r(seed);
    const V3 dark{0.12f, 0.26f, 0.1f}, light{0.34f, 0.5f, 0.2f};
    for (int b = 0; b < blades; ++b) {
        const float a = r.r(0, 6.283f), rad = r.r(0.0f, 0.12f), hgt = r.r(0.6f, 1.0f), lean = r.r(0.05f, 0.3f), face = a + 1.57f, w0 = r.r(0.05f, 0.08f);
        const float bx = std::cos(a) * rad, by = std::sin(a) * rad, dx = std::cos(a), dy = std::sin(a), fx = std::cos(face), fy = std::sin(face);
        const auto base = std::uint32_t(m.v.size());
        for (int s = 0; s <= segs; ++s) {
            const float t = float(s) / segs, w = w0 * width_scale * (1 - t * 0.6f) * 0.5f;
            const float cx = bx + dx * lean * t * hgt, cy = by + dy * lean * t * hgt, cz = hgt * t;
            const float col[4]{dark.x + (light.x - dark.x) * t, dark.y + (light.y - dark.y) * t, dark.z + (light.z - dark.z) * t, 0.85f};
            add(m, {cx - fx * w, cy - fy * w, cz}, {-dx * 0.3f, -dy * 0.3f, 0.95f}, col, 0, t, 0.75f + 0.35f * t, t * t * hgt * 2.5f);
            add(m, {cx + fx * w, cy + fy * w, cz}, {-dx * 0.3f, -dy * 0.3f, 0.95f}, col, 1, t, 0.75f + 0.35f * t, t * t * hgt * 2.5f);
        }
        for (int s = 0; s < segs; ++s) { const std::uint32_t q = base + s * 2; tri(m, q, q + 1, q + 2); tri(m, q + 1, q + 3, q + 2); }
    }
    return m;
}
GcMesh make_needles(std::uint32_t seed) {
    GcMesh m; Rng r(seed);
    for (int k = 0; k < 40; ++k) {
        const float cx = r.r(-0.5f, 0.5f), cy = r.r(-0.5f, 0.5f);
        if (cx * cx + cy * cy > 0.25f) continue;
        const float a = r.r(0, 6.28f), len = r.r(0.06f, 0.12f), w = 0.012f, tone = r.r(0.8f, 1.15f);
        const float dx = std::cos(a) * len, dy = std::sin(a) * len, px = -std::sin(a) * w, py = std::cos(a) * w;
        const float col[4]{0.4f * tone, 0.3f * tone, 0.2f * tone, 0.9f};
        const auto b = std::uint32_t(m.v.size());
        const float z = 0.006f + r.r(0, 0.01f);
        add(m, {cx - dx - px, cy - dy - py, z}, {0, 0, 1}, col, 0, 0, 0.9f, 0);
        add(m, {cx - dx + px, cy - dy + py, z}, {0, 0, 1}, col, 0, 0, 0.9f, 0);
        add(m, {cx + dx - px, cy + dy - py, z}, {0, 0, 1}, col, 0, 0, 0.9f, 0);
        add(m, {cx + dx + px, cy + dy + py, z}, {0, 0, 1}, col, 0, 0, 0.9f, 0);
        tri(m, b, b + 1, b + 2); tri(m, b + 1, b + 3, b + 2);
    }
    return m;
}

GcMesh make_cone() {
    GcMesh m;
    // Overlapping scale rings on a small fallen cone, bark-bound like wood.
    for (unsigned ring = 0; ring < 7; ++ring) {
        const float x = -.45f + ring * .13f;
        const float radius = .23f * std::sin((ring + .5f) * 3.14159f / 7);
        std::vector<V3> pts{{x,0,.2f},{x+.12f,0,.2f}};
        add_prism(m,pts,radius,radius*.7f,8,{.42f,.3f,.18f},.9f,false,{});
    }
    return m;
}

std::array<GcMesh, gm_count> build_meshes() {
    return {make_blades(9, 0.12f, 11, 0.3f, {0.92f, 0.9f, 0.5f}), make_flower(17),
            make_ico(5, 0.18f, 0.55f, false, {0.5f, 0.48f, 0.45f}, 0.55f), make_ico(9, 0.28f, 0.62f, true, {0.52f, 0.5f, 0.47f}, 0.5f),
            make_twig(3), make_twig_fork(5), make_twig_broken(), make_leaf(), make_needles(81), make_log(), make_dead_bush(51),
            make_reeds(31, 11), make_cattail(41), make_mound(21), make_shell(), make_conch(), make_kelp(61), make_seagrass(71),
            make_blades(4, 0.2f, 13, 0.3f, {0.92f, 0.9f, 0.5f}, .34f, 2), make_cone(),
            make_cave_patch(),make_cave_fern(),make_cave_roots(),make_cave_mushrooms(),make_cave_slab(),
            make_cave_bones(),make_cave_web(),make_cave_crystals(),make_cave_slab(true),make_cave_skull(),
            make_flower(17, true), make_reeds(31, 5, 2, 2.2f), make_cattail(41, true), make_seagrass(71, 5, 2, 1.8f)};
}

struct GroundCoverConstants {
    float clip[16];
    float planes[6][4];
    float eye[4], window[4], grid[4], quality[4], wind[4], switches[4];
    float fog_range[4], fog_control[4], fog_color[4], bark_rect[4], end_rect[4], stone_rect[4];
    float view[4], wind2[4], trail_window[4], trail_step[4], object_window[4];
    float fog_seam[4], fog_curve[4], fog_span_lo[4], fog_span_hi[4], origin[4];
    std::uint32_t capacity[36], base[36];
    float fog_piece_info[4], fog_pieces[kNeighbourFogPieces][4];
    std::uint32_t snow_materials[4];
    float snow_window[4], snow_step[4];
    std::uint32_t snow_tiles[4], snow_write[4];
    std::uint32_t pass[4];
    std::uint32_t stage[4]; // placement: candidate and survivor capacity, jobs per class, window rows
    std::uint32_t round[4]; // placement: round split (candidate capacity less the largest cell)
};
static_assert(sizeof(GroundCoverConstants) % 16 == 0);
struct GcTileGpu { float uv[8]; std::uint32_t material, padding[3]; };
static_assert(sizeof(GcTileGpu) == 48);
struct GcInstanceGpu { float root[4]; std::uint32_t shape[4], color[4], extra[4]; };
static_assert(sizeof(GcInstanceGpu) == 64);

HRESULT make_buffer(ID3D11Device* device, UINT bytes, UINT bind, UINT misc, UINT stride, const void* data,
                    ComPtr<ID3D11Buffer>& buffer, D3D11_USAGE usage = D3D11_USAGE_DEFAULT, UINT cpu = 0) {
    D3D11_BUFFER_DESC d{}; d.ByteWidth = bytes; d.BindFlags = bind; d.MiscFlags = misc; d.StructureByteStride = stride;
    d.Usage = usage; d.CPUAccessFlags = cpu;
    D3D11_SUBRESOURCE_DATA initial{data, 0, 0};
    return device->CreateBuffer(&d, data ? &initial : nullptr, &buffer);
}
HRESULT make_structured_srv(ID3D11Device* device, ID3D11Buffer* buffer, UINT count, ComPtr<ID3D11ShaderResourceView>& srv) {
    D3D11_SHADER_RESOURCE_VIEW_DESC v{}; v.Format = DXGI_FORMAT_UNKNOWN; v.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    v.Buffer.NumElements = count;
    return device->CreateShaderResourceView(buffer, &v, &srv);
}
HRESULT make_raw_uav(ID3D11Device* device, ID3D11Buffer* buffer, UINT bytes, ComPtr<ID3D11UnorderedAccessView>& uav) {
    D3D11_UNORDERED_ACCESS_VIEW_DESC u{}; u.Format = DXGI_FORMAT_R32_TYPELESS; u.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    u.Buffer.NumElements = bytes / 4; u.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
    return device->CreateUnorderedAccessView(buffer, &u, &uav);
}
HRESULT make_texture(ID3D11Device* device, UINT width, UINT height, DXGI_FORMAT format, UINT mips, UINT bind, UINT misc,
                     ComPtr<ID3D11Texture2D>& texture) {
    D3D11_TEXTURE2D_DESC d{}; d.Width = width; d.Height = height; d.MipLevels = mips; d.ArraySize = 1; d.Format = format;
    d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = bind; d.MiscFlags = misc;
    return device->CreateTexture2D(&d, nullptr, &texture);
}
UINT dispatch_groups(UINT count, UINT size) { return (count + size - 1) / size; }

// Open-water tiles connected (4-neighbour) to the map edge, in components of at
// least 2048 tiles (8192 m2): the sea. Lakes and rivers inside the map, and
// small water bodies cut by the edge, are not.
std::vector<std::uint8_t> ground_cover_sea_mask(const std::vector<std::uint8_t>& area, UINT width, UINT height) {
    std::vector<std::uint8_t> mask(std::size_t(width) * height, 0);
    if (area.size() != mask.size()) return mask;
    std::vector<std::int32_t> label(mask.size(), -1), queue;
    const auto water = [&](std::size_t i) { return (area[i] & 15u) == ambient::area_water; };
    for (std::size_t start = 0; start < mask.size(); ++start) {
        if (!water(start) || label[start] >= 0) continue;
        queue.clear(); queue.push_back(std::int32_t(start)); label[start] = std::int32_t(start);
        bool edge = false;
        for (std::size_t head = 0; head < queue.size(); ++head) {
            const UINT i = UINT(queue[head]), x = i % width, y = i / width;
            edge |= x == 0 || y == 0 || x + 1 == width || y + 1 == height;
            const UINT next[4]{x ? i - 1 : i, x + 1 < width ? i + 1 : i, y ? i - width : i, y + 1 < height ? i + width : i};
            for (const UINT n : next)
                if (water(n) && label[n] < 0) { label[n] = std::int32_t(start); queue.push_back(std::int32_t(n)); }
        }
        if (edge && queue.size() >= 2048) for (const auto i : queue) mask[std::size_t(i)] = 255;
    }
    return mask;
}

HRESULT make_sea_srv(ID3D11Device* device, const std::vector<std::uint8_t>& area, UINT tiles_x, UINT tiles_y,
                     ComPtr<ID3D11ShaderResourceView>& srv) {
    try {
        const std::vector<std::uint8_t> mask = ground_cover_sea_mask(area, tiles_x, tiles_y);
        D3D11_TEXTURE2D_DESC d{}; d.Width = tiles_x; d.Height = tiles_y; d.MipLevels = d.ArraySize = 1;
        d.Format = DXGI_FORMAT_R8_UNORM; d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_IMMUTABLE; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA initial{mask.data(), tiles_x, 0};
        ComPtr<ID3D11Texture2D> texture;
        HRESULT hr = device->CreateTexture2D(&d, &initial, &texture);
        return FAILED(hr) ? hr : device->CreateShaderResourceView(texture.Get(), nullptr, &srv);
    } catch (...) { return E_OUTOFMEMORY; }
}
// An immutable texture with its initial data (the per-tile and per-vertex grids).
HRESULT make_grid_srv(ID3D11Device* device, UINT width, UINT height, DXGI_FORMAT format, const void* data, UINT pitch,
                      ComPtr<ID3D11ShaderResourceView>& srv) {
    D3D11_TEXTURE2D_DESC d{}; d.Width = width; d.Height = height; d.MipLevels = d.ArraySize = 1; d.Format = format;
    d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_IMMUTABLE; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial{data, pitch, 0};
    ComPtr<ID3D11Texture2D> texture;
    HRESULT hr = device->CreateTexture2D(&d, &initial, &texture);
    return FAILED(hr) ? hr : device->CreateShaderResourceView(texture.Get(), nullptr, &srv);
}

// Tile atlas cells (base tiles only): material and corner UVs by world corner.
// used: per material, 1 where a base tile uses it.
std::vector<GcTileGpu> ground_cover_tiles(const TerrainCpuAsset& terrain, UINT tiles_x, UINT tiles_y,
                                          std::vector<std::uint8_t>& used) {
    std::vector<GcTileGpu> tiles(std::size_t(tiles_x) * tiles_y);
    for (auto& tile : tiles) tile.material = 0xffffffffu;
    constexpr std::uint32_t layer = 33u * 33u;
    for (const auto& sector : terrain.sectors) {
        if (sector.map_x >= terrain.sector_count_x || sector.map_y >= terrain.sector_count_y) continue;
        for (const auto& tile : sector.tiles) {
            if (tile.transparent || tile.vertex_indices[0] >= layer) continue;
            std::uint32_t lx = 33, ly = 33;
            for (const unsigned k : {0u, 2u, 6u, 8u}) {
                lx = std::min(lx, std::uint32_t(tile.vertex_indices[k] % 33u)); ly = std::min(ly, std::uint32_t(tile.vertex_indices[k] / 33u));
            }
            const std::uint32_t gx = sector.map_x * 16u + lx / 2u, gy = sector.map_y * 16u + ly / 2u;
            if (gx >= tiles_x || gy >= tiles_y || lx > 30 || ly > 30) continue;
            auto& out = tiles[std::size_t(gy) * tiles_x + gx];
            bool complete = true;
            const auto tile_uvs = owned::terrain_tile_uvs(terrain, tile);
            for (const unsigned k : {0u, 2u, 6u, 8u}) {
                const std::uint32_t cx = (tile.vertex_indices[k] % 33u - lx) / 2u, cy = (tile.vertex_indices[k] / 33u - ly) / 2u;
                if (cx > 1 || cy > 1) { complete = false; break; }
                out.uv[(cy * 2 + cx) * 2] = tile_uvs[k].u;
                out.uv[(cy * 2 + cx) * 2 + 1] = tile_uvs[k].v;
            }
            if (!complete) continue;
            out.material = tile.material_index;
            if (tile.material_index >= used.size()) used.resize(std::size_t(tile.material_index) + 1, 0);
            used[tile.material_index] = 1;
        }
    }
    return tiles;
}

// Per base tile: the slot signature corner (NW, NE, SE, SW) at each world
// corner cy*2+cx, two bits each (terrain_cover_types lanes). Same base tile
// as terrain_base_slots: the MPR decoder stores positional vertex indices
// (index 0 is the tile's min corner; rotation lives only in the UVs) and
// marks exactly the layer tiles transparent, whose indices start past the
// base layer, so both skip the same tiles and land on the same cell.
std::vector<std::uint8_t> ground_cover_corner_lanes(const std::vector<GcTileGpu>& tiles) {
    std::vector<std::uint8_t> lanes(tiles.size(), 0);
    for (std::size_t t = 0; t < tiles.size(); ++t) {
        if (tiles[t].material == 0xffffffffu) continue;
        const float* uv = tiles[t].uv;
        const auto corner = terrain_tile_world_corners({0, 1, 2, 3}, {{{uv[0], uv[1]}, {uv[2], uv[3]}, {uv[4], uv[5]}, {uv[6], uv[7]}}});
        for (unsigned k = 0; k < 4; ++k) lanes[t] = std::uint8_t(lanes[t] | (corner[k] << (2 * k)));
    }
    return lanes;
}

// Per tile: deciduous, conifer and non-conifer woody influence of the placed MOB trees,
// and the undead affinity of cave burial sites (alpha).
std::vector<std::array<std::uint8_t,4>> ground_cover_trees(const OwnedMapSnapshot& map, UINT tiles_x, UINT tiles_y) {
    std::vector<std::array<std::uint8_t,4>> trees(std::size_t(tiles_x)*tiles_y);
    for (const auto& object : map.objects) {
        const auto kind = ambient::tree_kind(object.visual_reference.c_str());
        if (kind == ambient::tree_none) continue;
        const int cx = int(std::floor(object.position[0]*.5f)), cy = int(std::floor(object.position[1]*.5f));
        for (int dy=-3;dy<=3;++dy) for (int dx=-3;dx<=3;++dx) {
            const int x=cx+dx,y=cy+dy;
            if (x<0||y<0||x>=int(tiles_x)||y>=int(tiles_y)) continue;
            auto& value=trees[std::size_t(y)*tiles_x+x];
            const float influence = tree_litter_falloff(x*2.f+1.f-object.position[0], y*2.f+1.f-object.position[1]);
            add_tree_litter_influence(value, kind, influence);
        }
    }
    if(map.mob && map.mob->bytes) {
        const auto& bytes=*map.mob->bytes;
        const auto sites=[&](const auto& self,const std::vector<MobChunkRecord>& chunks)->void {
            for(const auto& chunk:chunks) {
                if(chunk.id==0xbbbb0000u) {
                    const MobChunkRecord* model=nullptr;const MobChunkRecord* position=nullptr;bool duplicate=false;
                    for(const auto& field:chunk.children) {
                        if(field.id==0xb006u){duplicate|=model!=nullptr;model=&field;}
                        if(field.id==0xb009u){duplicate|=position!=nullptr;position=&field;}
                    }
                    std::array<float,2> site{};
                    // Chunk extents were validated once by parse_mob_document.
                    if(!duplicate && model && position && cave_undead_site(bytes.data()+model->payload_offset,model->payload_bytes,
                        bytes.data()+position->payload_offset,position->payload_bytes,site)) {
                        const int cx=int(std::floor(site[0]*.5f)),cy=int(std::floor(site[1]*.5f));
                        for(int dy=-10;dy<=10;++dy)for(int dx=-10;dx<=10;++dx) {
                            const int x=cx+dx,y=cy+dy;if(x<0||y<0||x>=int(tiles_x)||y>=int(tiles_y))continue;
                            auto& value=trees[std::size_t(y)*tiles_x+x][3];
                            const auto affinity=std::uint8_t(std::max(0.f,1.f-std::sqrt(float(dx*dx+dy*dy))/10.f)*255);
                            value=std::max(value,affinity);
                        }
                    }
                }
                self(self,chunk.children);
            }
        };
        sites(sites,map.mob->roots);
    }
    return trees;
}

std::string lower_name(std::string name) {
    for (char& c : name) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    return name;
}
} // namespace

// Pure CPU, from the welded neighbour terrain and its map: the shared grids,
// tile atlas cells and used materials, corner lanes, trees, tile families
// (their job starts here), and the material blend inputs. Runs on the
// neighbour stream's worker; add_ground_cover_neighbour turns them into GPU
// resources on the render thread.
struct GroundCoverNeighbourInputs {
    ambient::TerrainGrids grids;
    std::vector<GcTileGpu> tiles;
    std::vector<std::uint8_t> used, base_lanes;
    std::vector<std::array<std::uint8_t,4>> trees;
    std::future<TerrainTileFamilies> families;
    std::vector<std::uint16_t> base_slots;
    std::vector<std::uint32_t> slot_types;
    std::shared_ptr<TerrainTileBlendInput> blend_input; // only for 8-tile atlas sides
    std::shared_ptr<const TerrainBakeCells> bake_cells;  // terrain material cache (terrain_bake.h)
    bool grids_valid = false;
};
std::shared_ptr<GroundCoverNeighbourInputs> prepare_ground_cover_neighbour_inputs(
    const NeighbourMapUpload& upload) noexcept try {
    if (!upload.map || !upload.map->terrain) return nullptr;
    const auto& terrain = *upload.map->terrain;
    auto inputs = std::make_shared<GroundCoverNeighbourInputs>();
    inputs->grids_valid = !terrain.sectors.empty() && ambient::build_terrain_grids(terrain, upload.map.get(), inputs->grids);
    if (!inputs->grids_valid) return inputs;
    const UINT tx = inputs->grids.tiles_x, ty = inputs->grids.tiles_y;
    inputs->families = load_terrain_tile_families_async(terrain);
    if (inputs->families.valid()) { inputs->base_slots = ambient::terrain_base_slots(terrain); inputs->slot_types = terrain.tile_types; }
    inputs->tiles = ground_cover_tiles(terrain, tx, ty, inputs->used);
    if (inputs->families.valid()) inputs->base_lanes = ground_cover_corner_lanes(inputs->tiles);
    inputs->trees = ground_cover_trees(*upload.map, tx, ty);
    inputs->bake_cells = std::make_shared<const TerrainBakeCells>(terrain_bake_cells(inputs->grids.height, tx, ty));
    if (inputs->families.valid() && terrain.tiles_per_texture_side == 8)
        inputs->blend_input = std::make_shared<TerrainTileBlendInput>(owned_terrain_tile_blend_input(terrain, tx, ty));
    return inputs;
} catch (...) { return nullptr; }

// One production source for asynchronous pipeline compilation and shader checks.
const std::string& owned_ground_cover_shader_source(){
    static const std::string source=
        owned_native_shader_source()+kAmbientOcclusionSurfaceShader+kSurfaceShader+kInteractionShader+kGroundCoverShader;
    return source;
}

namespace {
// Program order of append_ground_cover_shader_programs (the blob order).
enum GcProgram : unsigned {
    gp_cells, gp_scan, gp_place, gp_place_grass, gp_place_neighbour,
    gp_habitat, gp_habitat_grass, gp_habitat_wall, gp_habitat_beach, gp_habitat_pile, gp_habitat_all,
    gp_build_grass, gp_build_grass_still, gp_build_tall, gp_build_tall_still, gp_build_debris, gp_build_wall, gp_build_beach,
    gp_build_pile, gp_build_all,  // _still: without unit tracks and snow trails
    gp_shade,                     // 2: [far maps] (soft shadows only)
    gp_prep = gp_shade + 2,       // 8: [neighbour * 4 + fog * 2 + cloud]
    gp_finalize = gp_prep + 8, gp_paint, gp_fields,
    gp_vs,                        // 4: cover, cover with point shadows, snow pile without / with trails
    gp_ps = gp_vs + 4,            // 30: [(kind * 5 + sun state) * 2 + ambient occlusion]
    gp_ps_snow = gp_ps + 30,      // 8: [(sun * 2 + point shadows) * 2 + snow trails]
    gp_ps_masked = gp_ps_snow + 8, // 10: cover [kind * 2 + ambient occlusion], snow pile [point shadows * 2 + snow trails]
    gp_depth_vs = gp_ps_masked + 10, // 3: cover, snow pile without / with trails
    gp_depth_ps = gp_depth_vs + 3, // 2: cover, snow pile
    gp_trail_vs = gp_depth_ps + 2, gp_trail_ps, gp_trail_full_vs, gp_trail_decay_ps, gp_object_vs, gp_object_ps,
    gp_snow_stamp_vs, gp_snow_stamp_ps, gp_snow_recover_ps, gp_snow_tile_vs, gp_count};
// The placement shader's per-rule features (its GC_F_* guards).
enum PlaceFeature : unsigned { pf_edge = 1, pf_cluster = 2, pf_far = 4, pf_trail = 8, pf_pal1 = 16, pf_pal23 = 32,
                               pf_wall = 64, pf_beach = 128, pf_pile = 256 };
// Placement classes, smallest first: a rule takes the first class whose
// features cover the ones it reaches. Programs per stage (cells, habitat,
// build with trails, build without); classes whose stage needs no feature of
// its own share a program.
struct PlaceClass { const char* name; unsigned features; unsigned program[4]; };
constexpr unsigned kPlaceClassCount = 6;
constexpr PlaceClass kPlaceClasses[kPlaceClassCount] = {
    {"debris", pf_pal23, {gp_place, gp_habitat, gp_build_debris, gp_build_debris}},
    {"tall tufts", pf_far | pf_trail, {gp_place, gp_habitat, gp_build_tall, gp_build_tall_still}},
    {"grass", pf_edge | pf_cluster | pf_far | pf_trail | pf_pal1, {gp_place_grass, gp_habitat_grass, gp_build_grass, gp_build_grass_still}},
    {"cave walls", pf_wall, {gp_place, gp_habitat_wall, gp_build_wall, gp_build_wall}},
    {"beach wrack", pf_beach, {gp_place, gp_habitat_beach, gp_build_beach, gp_build_beach}},
    {"piles", pf_pile, {gp_place, gp_habitat_pile, gp_build_pile, gp_build_pile}}};
// Sun states of the cover pixel programs: 0 off, then hard/soft x near/far maps.
unsigned gc_sun_state(bool on, bool soft, bool far_maps) { return on ? 1u + (soft ? 2u : 0u) + (far_maps ? 1u : 0u) : 0u; }
// Mirrors every rule-dependent guard of gcPlaceCS/gcHabitatCS/gcBuildCS.
unsigned place_features(const GroundCoverRule& r) {
    unsigned f = 0;
    const auto cluster = std::uint32_t(r.cluster), palette = std::uint32_t(r.palette);
    if (cluster != 0) f |= pf_cluster;
    if (r.mesh == gm_blades || cluster == cl_flower_patch) f |= pf_edge;
    if (r.mesh == gm_blades || r.mesh == gm_flower || r.mesh == gm_reed || r.mesh == gm_cattail || r.mesh == gm_seagrass) f |= pf_far;
    if ((r.flags & rf_tuft) || r.mesh == gm_dead_bush) f |= pf_trail;
    if (palette == pal_flower) f |= pf_pal1;
    if ((palette == pal_leaf && r.mesh != gm_needles) || palette == pal_stone) f |= pf_pal23;
    if (r.field == fd_damp || r.field == fd_wall || r.field == fd_corner || r.mesh == gm_roots) f |= pf_wall;
    if (r.field == fd_beach || (r.flags & (rf_shore_tangent | rf_shore_clumps))) f |= pf_beach;
    if ((r.flags & rf_snow_pile) || r.mesh == gm_mound) f |= pf_pile;
    return f;
}
// Pixel material kinds (GC_PS_KIND): procedural colour (plain, ground, wet,
// glow), bark, stone, snow pile.
constexpr unsigned kPixelKindMaterials[4] = {(1u << mat_plain) | (1u << mat_ground) | (1u << mat_wet) | (1u << mat_glow),
                                             1u << mat_bark, 1u << mat_stone, 1u << mat_snow};
} // namespace

// Order is the pipeline's blob order in ensure_ground_cover_pipeline (GcProgram).
void append_ground_cover_shader_programs(std::vector<ShaderProgram>& out){
    using M = D3D_SHADER_MACRO;
    static const M grass[]{{"GC_PLACE_GRASS", "1"}, {}}, debris[]{{"GC_PLACE_DEBRIS", "1"}, {}}, tall[]{{"GC_PLACE_TALL", "1"}, {}},
        neighbour[]{{"GC_PLACE_NEIGHBOUR", "1"}, {}},
        grass_still[]{{"GC_PLACE_GRASS", "1"}, {"GC_BUILD_TRAILS", "0"}, {}}, tall_still[]{{"GC_PLACE_TALL", "1"}, {"GC_BUILD_TRAILS", "0"}, {}},
        wall[]{{"GC_PLACE_WALL", "1"}, {}}, beach[]{{"GC_PLACE_BEACH", "1"}, {}}, pile[]{{"GC_PLACE_PILE", "1"}, {}};
    // Settings permutations: each list lives as long as the program table.
    static std::deque<std::vector<M>> lists;
    const auto list = [&](std::initializer_list<M> macros) {
        lists.emplace_back(macros); lists.back().push_back({}); return lists.back().data();
    };
    const char* const digit[]{"0", "1", "2", "3"};
    const auto add = [&](const char* entry, const char* profile, const M* defines) {
        out.push_back({owned_ground_cover_shader_source(), "ground_cover", defines, entry, profile, D3DCOMPILE_OPTIMIZATION_LEVEL3});
    };
    add("gcCellsCS", "cs_5_0", nullptr); add("gcScanCS", "cs_5_0", nullptr);
    add("gcPlaceCS", "cs_5_0", nullptr); add("gcPlaceCS", "cs_5_0", grass); add("gcNeighbourCS", "cs_5_0", neighbour);
    add("gcHabitatCS", "cs_5_0", nullptr); add("gcHabitatCS", "cs_5_0", grass); add("gcHabitatCS", "cs_5_0", wall);
    add("gcHabitatCS", "cs_5_0", beach); add("gcHabitatCS", "cs_5_0", pile); add("gcHabitatCS", "cs_5_0", neighbour);
    add("gcBuildCS", "cs_5_0", grass); add("gcBuildCS", "cs_5_0", grass_still); add("gcBuildCS", "cs_5_0", tall);
    add("gcBuildCS", "cs_5_0", tall_still); add("gcBuildCS", "cs_5_0", debris); add("gcBuildCS", "cs_5_0", wall);
    add("gcBuildCS", "cs_5_0", beach); add("gcBuildCS", "cs_5_0", pile); add("gcBuildCS", "cs_5_0", neighbour);
    // Per-instance sun shadow: only with soft shadows (the pixel programs read it only then).
    for (unsigned far_maps = 0; far_maps < 2; ++far_maps)
        add("gcShadeCS", "cs_5_0", list({{"GC_SUN_SOFT", "1"}, {"GC_SUN_FAR", digit[far_maps]}}));
    for (unsigned k = 0; k < 8; ++k)
        add("gcPrepCS", "cs_5_0", list({{"GC_PREP_NEIGHBOUR", digit[k >> 2]}, {"GC_PREP_FOG", digit[(k >> 1) & 1]}, {"GC_PREP_CLOUD", digit[k & 1]}}));
    add("gcFinalizeCS", "cs_5_0", nullptr); add("gcPaintCS", "cs_5_0", nullptr);
    add("gcFieldsCS", "cs_5_0", nullptr);
    add("gcVS", "vs_5_0", list({{"GC_VS", "1"}}));
    add("gcVS", "vs_5_0", list({{"GC_VS", "1"}, {"GC_VS_POINT_SHADOWS", "1"}}));
    for (unsigned trails = 0; trails < 2; ++trails)
        add("gcVS", "vs_5_0", list({{"GC_VS", "1"}, {"GC_VS_SNOW", "1"}, {"GC_SNOW_TRAILS", digit[trails]}}));
    for (unsigned kind = 0; kind < 3; ++kind) for (unsigned sun = 0; sun < 5; ++sun) for (unsigned ao = 0; ao < 2; ++ao) {
        const unsigned soft = sun >= 3 ? 1 : 0, far_maps = sun ? (sun - 1) & 1 : 0;
        add("gcPS", "ps_5_0", list({{"GC_POINT_SHADOW_RECEIVER", "1"}, {"GC_PS_KIND", digit[kind]}, {"GC_SUN", digit[sun ? 1 : 0]},
                                    {"GC_SUN_SOFT", digit[soft]}, {"GC_SUN_FAR", digit[far_maps]}, {"GC_AO", digit[ao]}}));
    }
    // The snow pile shades as terrain: without point shadows it takes the
    // native Low lighting, whose only difference here is no point shadow test.
    for (unsigned sun = 0; sun < 2; ++sun) for (unsigned point = 0; point < 2; ++point) for (unsigned trails = 0; trails < 2; ++trails) {
        if (point) add("gcPS", "ps_5_0", list({{"GC_POINT_SHADOW_RECEIVER", "1"}, {"GC_PS_KIND", "3"}, {"GC_SUN", digit[sun]}, {"GC_SNOW_TRAILS", digit[trails]}}));
        else add("gcPS", "ps_5_0", list({{"GC_POINT_SHADOW_RECEIVER", "1"}, {"GC_PS_KIND", "3"}, {"GC_SUN", digit[sun]}, {"GC_SNOW_TRAILS", digit[trails]},
                                        {"NATIVE_LOW", "1"}}));
    }
    // Masked material variants (depth prepass + world mask pass): cover kinds x
    // ambient occlusion, then the snow pile by point shadows x snow trails.
    for (unsigned kind = 0; kind < 3; ++kind) for (unsigned ao = 0; ao < 2; ++ao)
        add("gcPS", "ps_5_0", list({{"GC_POINT_SHADOW_RECEIVER", "1"}, {"GC_PS_KIND", digit[kind]}, {"GC_SUN_MASK", "1"},
                                    {"GC_SUN", "0"}, {"GC_AO", digit[ao]}}));
    for (unsigned point = 0; point < 2; ++point) for (unsigned trails = 0; trails < 2; ++trails) {
        if (point) add("gcPS", "ps_5_0", list({{"GC_POINT_SHADOW_RECEIVER", "1"}, {"GC_PS_KIND", "3"}, {"GC_SUN_MASK", "1"}, {"GC_SUN", "0"},
                                              {"GC_SNOW_TRAILS", digit[trails]}}));
        else add("gcPS", "ps_5_0", list({{"GC_POINT_SHADOW_RECEIVER", "1"}, {"GC_PS_KIND", "3"}, {"GC_SUN_MASK", "1"}, {"GC_SUN", "0"},
                                        {"GC_SNOW_TRAILS", digit[trails]}, {"NATIVE_LOW", "1"}}));
    }
    // Depth prepass (alpha test only): the material programs' placement and clip.
    add("gcDepthVS", "vs_5_0", list({{"GC_VS", "1"}}));
    for (unsigned trails = 0; trails < 2; ++trails)
        add("gcDepthVS", "vs_5_0", list({{"GC_VS", "1"}, {"GC_VS_SNOW", "1"}, {"GC_SNOW_TRAILS", digit[trails]}}));
    add("gcDepthPS", "ps_5_0", list({{"GC_POINT_SHADOW_RECEIVER", "1"}, {"GC_PS_KIND", "0"}}));
    add("gcDepthPS", "ps_5_0", list({{"GC_POINT_SHADOW_RECEIVER", "1"}, {"GC_PS_KIND", "3"}}));
    add("gcTrailVS", "vs_5_0", nullptr); add("gcTrailPS", "ps_5_0", nullptr); add("gcTrailFullVS", "vs_5_0", nullptr);
    add("gcTrailDecayPS", "ps_5_0", nullptr); add("gcObjectVS", "vs_5_0", nullptr); add("gcObjectPS", "ps_5_0", nullptr);
    add("gcSnowStampVS", "vs_5_0", nullptr); add("gcSnowStampPS", "ps_5_0", nullptr); add("gcSnowRecoverPS", "ps_5_0", nullptr);
    add("gcSnowTileVS", "vs_5_0", nullptr);
}

void Renderer::release_ground_cover() noexcept {
    gc_place_cs_.Reset(); gc_finalize_cs_.Reset(); gc_paint_cs_.Reset(); gc_fields_cs_.Reset();
    for (auto& value : gc_shade_cs_) value.Reset();
    gc_cells_cs_.Reset(); gc_scan_cs_.Reset(); gc_neighbour_cs_.Reset(); gc_habitat_all_cs_.Reset(); gc_build_all_cs_.Reset();
    gc_cell_jobs_.Reset(); gc_cell_job_uav_.Reset(); gc_cell_job_srv_.Reset(); gc_cell_job_capacity_ = 0;
    gc_cell_counts_.Reset(); gc_cell_count_uav_.Reset(); gc_cell_count_srv_.Reset();
    gc_rounds_.Reset(); gc_round_uav_.Reset(); gc_round_srv_.Reset(); gc_rule_class_.Reset(); gc_rule_class_srv_.Reset();
    std::vector<std::uint64_t>().swap(gc_cell_masks_); gc_cell_masks_key_ = UINT64_MAX; gc_cell_mask_srv_.Reset();
    for (auto& stage : gc_stage_cs_) for (auto& value : stage) value.Reset();
    for (auto& value : gc_class_constants_) value.Reset();
    gc_class_rule_count_.fill(0);
    gc_candidates_.Reset(); gc_survivors_.Reset(); gc_shade_input_.Reset();
    gc_candidate_uav_.Reset(); gc_survivor_uav_.Reset(); gc_shade_input_uav_.Reset();
    for (auto& value : gc_stage_counts_) value.Reset(); for (auto& value : gc_stage_count_uav_) value.Reset();
    for (auto& value : gc_stage_count_srv_) value.Reset();
    gc_candidate_capacity_ = gc_survivor_capacity_ = 0;
    for (auto& value : gc_vs_) value.Reset();
    for (auto& value : gc_ps_) value.Reset();
    for (auto& value : gc_prep_cs_) value.Reset();
    for (auto& value : gc_depth_vs_) value.Reset(); for (auto& value : gc_depth_ps_) value.Reset();
    gc_draw_inputs_.Reset(); gc_draw_input_srv_.Reset(); gc_draw_input_uav_.Reset();
    std::vector<std::uint32_t>().swap(gc_tile_classes_); gc_tile_classes_key_ = UINT64_MAX;
    std::vector<std::uint8_t>().swap(gc_tree_mask_);
    for (auto& list : gc_class_rules_) list.clear();
    for (auto& value : gc_round_constants_) value.Reset();
    gc_work_.Reset(); gc_work_uav_.Reset(); gc_work_srv_.Reset();
    gc_frame_constants_.Reset(); for (auto& value : gc_draw_constants_) value.Reset(); gc_paint_constants_.Reset();
    gc_trail_vs_.Reset(); gc_trail_ps_.Reset(); gc_trail_full_vs_.Reset(); gc_trail_decay_ps_.Reset();
    gc_object_vs_.Reset();gc_object_ps_.Reset();gc_object_mask_.Reset();gc_object_rtv_.Reset();gc_object_srv_.Reset();
    gc_objects_.Reset();gc_object_input_srv_.Reset();std::vector<FootprintSplat>().swap(gc_object_input_);
    std::vector<FootprintSplat>().swap(gc_object_drawn_);gc_object_mask_current_=false;
    gc_object_capacity_=gc_object_width_=gc_object_height_=0;
    for (unsigned k = 0; k < 2; ++k) { gc_trail_[k].Reset(); gc_trail_rtv_[k].Reset(); gc_trail_srv_[k].Reset(); }
    gc_trail_sources_.Reset();gc_trail_sources_rtv_.Reset();gc_trail_sources_srv_.Reset();
    gc_trail_index_ = 0; gc_trail_recovery_ = 0; gc_trail_seconds_ = -1;
    gc_footprints_.Reset(); gc_footprint_srv_.Reset(); gc_trail_blend_.Reset(); gc_trail_depth_.Reset();
    std::vector<FootprintSplat>().swap(gc_footprint_input_);
    gc_footprint_capacity_ = gc_trail_width_ = gc_trail_height_ = 0;
    gc_snow_stamp_vs_.Reset(); gc_snow_stamp_ps_.Reset(); gc_snow_recover_ps_.Reset(); gc_snow_tile_vs_.Reset();
    gc_snow_tiles_ = {}; gc_snow_active_ = {}; gc_snow_full_writes_ = 0;
    for (unsigned k = 0; k < 2; ++k) { gc_snow_[k].Reset(); gc_snow_rtv_[k].Reset(); gc_snow_srv_[k].Reset(); }
    gc_snow_segments_.Reset(); gc_snow_segment_srv_.Reset(); gc_snow_segment_capacity_ = 0; gc_snow_index_ = 0;
    gc_snow_valid_ = false; gc_snow_seconds_ = -1; gc_snow_tracks_.clear(); snow_terrain_srv_.Reset();
    gc_vertices_.Reset(); gc_vertex_srv_.Reset(); gc_indices_.Reset(); gc_rules_.Reset(); gc_rule_srv_.Reset();
    gc_args_.Reset(); gc_args_uav_.Reset(); gc_counts_.Reset(); gc_counts_uav_.Reset();
    gc_overflow_.Reset(); gc_overflow_uav_.Reset(); gc_overflow_passes_ = 0;
    for (auto& value : gc_count_readback_) value.Reset();
    gc_instances_.Reset(); gc_instance_srv_.Reset(); gc_instance_uav_.Reset();
    gc_fields_srv_.Reset(); gc_color_.Reset(); gc_color_srv_.Reset(); gc_color_uav_.Reset();
    gc_tiles_srv_.Reset(); gc_bark_srv_.Reset(); gc_stone_srv_.Reset(); gc_trees_srv_.Reset();
    gc_zone_pieces_.Reset(); gc_zone_srv_.Reset(); gc_zone_uploaded_values_.clear();
    gc_raster_.Reset(); gc_depth_.Reset(); gc_blend_.Reset(); gc_clamp_.Reset(); gc_wrap_.Reset();
    gc_map_.reset(); gc_material_state_.clear(); gc_neighbours_.clear();
    gc_capacity_.fill(0); gc_instance_capacity_ = 0; gc_readback_head_ = gc_readback_pending_ = 0;
    gc_map_generation_ = 0; gc_failed_ = false; gc_grow_ = false;
    if (gc_compile_.valid()) gc_compile_ = {}; // waits for a running compile
}

// Shaders load on a worker as soon as the option is on, so no frame waits;
// cover stays absent until they are ready. The build embeds their bytecode
// (shader_cache.h); FXC (about 37 s over the shared world source) runs only
// for a program missing from that table. S_FALSE: not ready yet.
HRESULT Renderer::ensure_ground_cover_pipeline() noexcept {
    if (gc_place_cs_) return S_OK;
    if (!gc_compile_.valid()) {
        try {
            std::vector<ShaderProgram> programs;
            append_ground_cover_shader_programs(programs);
            gc_compile_ = std::async(std::launch::async, [programs = std::move(programs)]() {
                std::vector<ComPtr<ID3DBlob>> blobs(programs.size());
                for (std::size_t k = 0; k < programs.size(); ++k) {
                    ComPtr<ID3DBlob> errors;
                    if (FAILED(compile_shader(programs[k], &blobs[k], &errors))) {
                        if (errors) OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
                        blobs.clear();
                        break;
                    }
                }
                return blobs;
            });
        } catch (...) { return E_OUTOFMEMORY; }
        return S_FALSE;
    }
    if (gc_compile_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return S_FALSE;
    std::vector<ComPtr<ID3DBlob>> blobs;
    try { blobs = gc_compile_.get(); } catch (...) { return E_FAIL; }
    if (blobs.size() != gp_count) return E_FAIL;
    for (const auto& blob : blobs) if (!blob) return E_FAIL;
    static_assert(kPlaceClassCount == kGcPlaceClasses, "placement class table");
    HRESULT hr = S_OK;
    const auto blob_cs = [&](unsigned program, ComPtr<ID3D11ComputeShader>& shader) {
        return device_->CreateComputeShader(blobs[program]->GetBufferPointer(), blobs[program]->GetBufferSize(), nullptr, &shader);
    };
    const auto blob_vs = [&](unsigned program, ComPtr<ID3D11VertexShader>& shader) {
        return device_->CreateVertexShader(blobs[program]->GetBufferPointer(), blobs[program]->GetBufferSize(), nullptr, &shader);
    };
    const auto blob_ps = [&](unsigned program, ComPtr<ID3D11PixelShader>& shader) {
        return device_->CreatePixelShader(blobs[program]->GetBufferPointer(), blobs[program]->GetBufferSize(), nullptr, &shader);
    };
    if (FAILED(hr = blob_cs(gp_finalize, gc_finalize_cs_)) || FAILED(hr = blob_cs(gp_paint, gc_paint_cs_)) ||
        FAILED(hr = blob_cs(gp_shade, gc_shade_cs_[0])) || FAILED(hr = blob_cs(gp_shade + 1, gc_shade_cs_[1])) ||
        FAILED(hr = blob_vs(gp_object_vs, gc_object_vs_)) || FAILED(hr = blob_ps(gp_object_ps, gc_object_ps_)) ||
        FAILED(hr = blob_cs(gp_fields, gc_fields_cs_)) ||
        FAILED(hr = blob_vs(gp_trail_vs, gc_trail_vs_)) || FAILED(hr = blob_ps(gp_trail_ps, gc_trail_ps_)) ||
        FAILED(hr = blob_vs(gp_trail_full_vs, gc_trail_full_vs_)) || FAILED(hr = blob_ps(gp_trail_decay_ps, gc_trail_decay_ps_)) ||
        FAILED(hr = blob_vs(gp_snow_stamp_vs, gc_snow_stamp_vs_)) || FAILED(hr = blob_ps(gp_snow_stamp_ps, gc_snow_stamp_ps_)) ||
        FAILED(hr = blob_ps(gp_snow_recover_ps, gc_snow_recover_ps_)) || FAILED(hr = blob_vs(gp_snow_tile_vs, gc_snow_tile_vs_))) return hr;
    for (unsigned k = 0; k < gc_vs_.size(); ++k)
        if (FAILED(hr = blob_vs(gp_vs + k, gc_vs_[k]))) return hr;
    for (unsigned k = 0; k < gc_ps_.size(); ++k)
        if (FAILED(hr = blob_ps(gp_ps + k, gc_ps_[k]))) return hr;
    for (unsigned k = 0; k < gc_prep_cs_.size(); ++k)
        if (FAILED(hr = blob_cs(gp_prep + k, gc_prep_cs_[k]))) return hr;
    if (FAILED(hr = blob_cs(gp_cells, gc_cells_cs_)) || FAILED(hr = blob_cs(gp_scan, gc_scan_cs_)) ||
        FAILED(hr = blob_cs(gp_place_neighbour, gc_neighbour_cs_)) || FAILED(hr = blob_cs(gp_habitat_all, gc_habitat_all_cs_)) ||
        FAILED(hr = blob_cs(gp_build_all, gc_build_all_cs_))) return hr;
    for (unsigned k = 0; k < gc_depth_vs_.size(); ++k)
        if (FAILED(hr = blob_vs(gp_depth_vs + k, gc_depth_vs_[k]))) return hr;
    for (unsigned k = 0; k < gc_depth_ps_.size(); ++k)
        if (FAILED(hr = blob_ps(gp_depth_ps + k, gc_depth_ps_[k]))) return hr;
    for (UINT round = 0; round < kGcMaxRounds; ++round) { // GcRun: the round index, bound per round
        const UINT values[4]{round, 0, 0, 0};
        if (FAILED(hr = make_buffer(device_.Get(), 16, D3D11_BIND_CONSTANT_BUFFER, 0, 0, values, gc_round_constants_[round],
                D3D11_USAGE_IMMUTABLE))) return hr;
    }
    // Cell job counts (6 words) and per class and round the dispatch arguments
    // and first job; each rule's placement class.
    {
        const auto raw = [&](UINT bytes, UINT misc, ComPtr<ID3D11Buffer>& buffer, ComPtr<ID3D11UnorderedAccessView>& uav,
                             ComPtr<ID3D11ShaderResourceView>& srv) {
            D3D11_SHADER_RESOURCE_VIEW_DESC v{}; v.Format = DXGI_FORMAT_R32_TYPELESS; v.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
            v.BufferEx.NumElements = bytes / 4; v.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
            HRESULT result = make_buffer(device_.Get(), bytes, D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE,
                misc | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0, nullptr, buffer);
            if (SUCCEEDED(result)) result = make_raw_uav(device_.Get(), buffer.Get(), bytes, uav);
            if (SUCCEEDED(result)) result = device_->CreateShaderResourceView(buffer.Get(), &v, &srv);
            return result;
        };
        if (FAILED(hr = raw(kPlaceClassCount * 4 + 8, 0, gc_cell_counts_, gc_cell_count_uav_, gc_cell_count_srv_)) ||
            FAILED(hr = raw(kPlaceClassCount * kGcMaxRounds * 16, D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS, gc_rounds_, gc_round_uav_, gc_round_srv_)))
            return hr;
    }
    // Placement classes: every rule in the first class covering its features;
    // each class walks its rules in table order (the rule index hashes them).
    {
        const auto& rules = rule_table();
        for (auto& list : gc_class_rules_) list.clear();
        struct { std::uint32_t count, slot, padding[2], rules[64]; } classes[kPlaceClassCount]{};
        static_assert(sizeof(classes[0]) == 272, "GcPlace constants");
        for (std::uint32_t index = 0; index < rules.size(); ++index) {
            const unsigned needs = place_features(rules[index]);
            unsigned c = 0;
            while (c < kPlaceClassCount && (needs & ~kPlaceClasses[c].features)) ++c;
            if (c == kPlaceClassCount || classes[c].count >= 64) {
                char text[160];
                std::snprintf(text, sizeof(text), "EID3D11 ground cover: rule %u (mesh %u) fits no placement kernel; ground cover omitted.\n",
                              index, rules[index].mesh);
                OutputDebugStringA(text);
                return E_FAIL;
            }
            classes[c].rules[classes[c].count++] = index;
            gc_class_rules_[c].push_back(index);
        }
        for (unsigned c = 0; c < kPlaceClassCount; ++c) {
            classes[c].slot = c; gc_class_rule_count_[c] = classes[c].count;
            if (FAILED(hr = make_buffer(device_.Get(), sizeof(classes[c]), D3D11_BIND_CONSTANT_BUFFER, 0, 0, &classes[c],
                    gc_class_constants_[c], D3D11_USAGE_IMMUTABLE))) return hr;
            for (unsigned stage = 0; stage < 4; ++stage)
                if (FAILED(hr = blob_cs(kPlaceClasses[c].program[stage], gc_stage_cs_[stage][c]))) return hr;
        }
        // Each rule's class for the cell pass, from the assignment just made.
        std::vector<std::uint32_t> rule_class(rules.size(), 0);
        for (unsigned c = 0; c < kPlaceClassCount; ++c) for (const std::uint32_t r : gc_class_rules_[c]) rule_class[r] = c;
        if (FAILED(hr = make_buffer(device_.Get(), UINT(rule_class.size() * 4), D3D11_BIND_SHADER_RESOURCE, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 4,
                rule_class.data(), gc_rule_class_, D3D11_USAGE_IMMUTABLE)) ||
            FAILED(hr = make_structured_srv(device_.Get(), gc_rule_class_.Get(), UINT(rule_class.size()), gc_rule_class_srv_))) return hr;
        // Pixel and vertex permutation per mesh from its rules' materials (far
        // meshes take their near mesh's rules); one kind must cover them all.
        std::array<unsigned, gm_count> materials{};
        for (const auto& rule : rules) {
            const unsigned bit = 1u << (std::uint32_t(rule.material) & 15u);
            materials[rule.mesh] |= bit;
            const std::uint32_t far_mesh = rule.mesh == gm_blades ? gm_blades_far : rule.mesh == gm_flower ? gm_flower_far :
                rule.mesh == gm_reed ? gm_reed_far : rule.mesh == gm_cattail ? gm_cattail_far : rule.mesh == gm_seagrass ? gm_seagrass_far : rule.mesh;
            materials[far_mesh] |= bit;
        }
        for (unsigned m = 0; m < gm_count; ++m) {
            unsigned kind = 0;
            while (kind < kGcPixelKinds && (materials[m] & ~kPixelKindMaterials[kind])) ++kind;
            if (kind == kGcPixelKinds) {
                OutputDebugStringA("EID3D11 ground cover: a cover mesh mixes pixel material kinds; ground cover omitted.\n");
                return E_FAIL;
            }
            gc_mesh_kind_[m] = std::uint8_t(kind);
        }
    }
    // Placed-instance work of each pass (gcFinalizeCS): dispatch arguments,
    // total and per-mesh prefix, read by the per-instance passes.
    {
        D3D11_SHADER_RESOURCE_VIEW_DESC v{}; v.Format = DXGI_FORMAT_R32_TYPELESS; v.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
        v.BufferEx.NumElements = 4 + gm_count; v.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
        if (FAILED(hr = make_buffer(device_.Get(), (4 + gm_count) * 4, D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE,
                D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0, nullptr, gc_work_)) ||
            FAILED(hr = make_raw_uav(device_.Get(), gc_work_.Get(), (4 + gm_count) * 4, gc_work_uav_)) ||
            FAILED(hr = device_->CreateShaderResourceView(gc_work_.Get(), &v, &gc_work_srv_))) return hr;
    }
    // Stage counts and dispatch arguments, one buffer per producing stage:
    // per class and round (groups, 1, 1, count), reset once per placement pass.
    for (unsigned k = 0; k < 2; ++k) {
        const UINT bytes = kPlaceClassCount * kGcMaxRounds * 16;
        D3D11_SHADER_RESOURCE_VIEW_DESC v{}; v.Format = DXGI_FORMAT_R32_TYPELESS; v.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
        v.BufferEx.NumElements = bytes / 4; v.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
        if (FAILED(hr = make_buffer(device_.Get(), bytes, D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE,
                D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0, nullptr, gc_stage_counts_[k])) ||
            FAILED(hr = make_raw_uav(device_.Get(), gc_stage_counts_[k].Get(), bytes, gc_stage_count_uav_[k])) ||
            FAILED(hr = device_->CreateShaderResourceView(gc_stage_counts_[k].Get(), &v, &gc_stage_count_srv_[k]))) return hr;
    }
    // Immutable meshes: one vertex buffer read by vertex ID, absolute indices.
    try {
        const auto meshes = build_meshes();
        std::vector<GcVertex> vertices; std::vector<std::uint32_t> indices;
        for (unsigned m = 0; m < gm_count; ++m) {
            const auto base = static_cast<std::uint32_t>(vertices.size());
            gc_mesh_first_index_[m] = static_cast<UINT>(indices.size());
            gc_mesh_index_count_[m] = static_cast<UINT>(meshes[m].i.size());
            vertices.insert(vertices.end(), meshes[m].v.begin(), meshes[m].v.end());
            for (const auto index : meshes[m].i) indices.push_back(base + index);
        }
        if (FAILED(hr = make_buffer(device_.Get(), UINT(vertices.size() * sizeof(GcVertex)), D3D11_BIND_SHADER_RESOURCE,
                D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, sizeof(GcVertex), vertices.data(), gc_vertices_, D3D11_USAGE_IMMUTABLE)) ||
            FAILED(hr = make_structured_srv(device_.Get(), gc_vertices_.Get(), UINT(vertices.size()), gc_vertex_srv_)) ||
            FAILED(hr = make_buffer(device_.Get(), UINT(indices.size() * 4), D3D11_BIND_INDEX_BUFFER, 0, 0, indices.data(),
                gc_indices_, D3D11_USAGE_IMMUTABLE))) return hr;
    } catch (...) { return E_OUTOFMEMORY; }
    const auto& rules = rule_table();
    if (FAILED(hr = make_buffer(device_.Get(), UINT(sizeof(rules)), D3D11_BIND_SHADER_RESOURCE, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,
            sizeof(GroundCoverRule), rules.data(), gc_rules_, D3D11_USAGE_IMMUTABLE)) ||
        FAILED(hr = make_structured_srv(device_.Get(), gc_rules_.Get(), UINT(rules.size()), gc_rule_srv_))) return hr;
    // Indirect arguments (5 per mesh; instance counts written by the placement
    // atomics) and the per-mesh counts read back to size the pools.
    for (unsigned m = 0; m < gm_count; ++m)
        gc_args_template_[m] = {gc_mesh_index_count_[m], 0u, gc_mesh_first_index_[m], 0u, 0u};
    if (FAILED(hr = make_buffer(device_.Get(), UINT(sizeof(gc_args_template_)), D3D11_BIND_UNORDERED_ACCESS,
            D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0, gc_args_template_.data(), gc_args_)) ||
        FAILED(hr = make_raw_uav(device_.Get(), gc_args_.Get(), UINT(sizeof(gc_args_template_)), gc_args_uav_)) ||
        FAILED(hr = make_buffer(device_.Get(), kGcCountWords * 4, D3D11_BIND_UNORDERED_ACCESS, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0,
            nullptr, gc_counts_)) ||
        FAILED(hr = make_raw_uav(device_.Get(), gc_counts_.Get(), kGcCountWords * 4, gc_counts_uav_))) return hr;
    for (auto& readback : gc_count_readback_)
        if (FAILED(hr = make_buffer(device_.Get(), kGcCountWords * 4, 0, 0, 0, nullptr, readback, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ)))
            return hr;
    if (FAILED(hr = make_buffer(device_.Get(), sizeof(GroundCoverConstants), D3D11_BIND_CONSTANT_BUFFER, 0, 0, nullptr,
            gc_frame_constants_, D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE)) ||
        FAILED(hr = make_buffer(device_.Get(), 16, D3D11_BIND_CONSTANT_BUFFER, 0, 0, nullptr, gc_paint_constants_,
            D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE))) return hr;
    D3D11_RASTERIZER_DESC raster{}; raster.FillMode = D3D11_FILL_SOLID; raster.CullMode = D3D11_CULL_NONE; raster.DepthClipEnable = TRUE;
    D3D11_DEPTH_STENCIL_DESC depth{}; depth.DepthEnable = TRUE; depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    D3D11_BLEND_DESC blend{}; for (auto& target : blend.RenderTarget) target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    D3D11_BLEND_DESC trail_blend{};
    auto& trail_target = trail_blend.RenderTarget[0];
    trail_target.BlendEnable = TRUE; trail_target.SrcBlend = trail_target.DestBlend = D3D11_BLEND_ONE;
    trail_target.BlendOp = trail_target.BlendOpAlpha = D3D11_BLEND_OP_MAX;
    trail_target.SrcBlendAlpha = trail_target.DestBlendAlpha = D3D11_BLEND_ONE;
    trail_target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED|D3D11_COLOR_WRITE_ENABLE_GREEN;
    D3D11_DEPTH_STENCIL_DESC trail_depth{}; trail_depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
    D3D11_SAMPLER_DESC clamp{}; clamp.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    clamp.AddressU = clamp.AddressV = clamp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; clamp.MaxLOD = D3D11_FLOAT32_MAX;
    D3D11_SAMPLER_DESC wrap = clamp; wrap.Filter = D3D11_FILTER_ANISOTROPIC; wrap.MaxAnisotropy = 8;
    wrap.AddressU = wrap.AddressV = wrap.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    if (FAILED(hr = device_->CreateRasterizerState(&raster, &gc_raster_)) || FAILED(hr = device_->CreateDepthStencilState(&depth, &gc_depth_)) ||
        FAILED(hr = device_->CreateDepthStencilState(&trail_depth, &gc_trail_depth_)) ||
        FAILED(hr = device_->CreateBlendState(&trail_blend, &gc_trail_blend_)) ||
        FAILED(hr = device_->CreateBlendState(&blend, &gc_blend_)) || FAILED(hr = device_->CreateSamplerState(&clamp, &gc_clamp_)) ||
        FAILED(hr = device_->CreateSamplerState(&wrap, &gc_wrap_))) return hr;
    // Last: the placement shader marks the pipeline complete.
    return blob_cs(gp_place, gc_place_cs_);
}

// Fields per metre (gcFieldsCS) from one terrain's area classes, heights,
// base types and sea mask, and its empty colour map (texels per 2 m tile,
// with mips, alpha 0 until painted). Shared by the active and neighbour maps.
HRESULT Renderer::build_ground_cover_surfaces(ID3D11ShaderResourceView* area, ID3D11ShaderResourceView* height,
    ID3D11ShaderResourceView* ground, ID3D11ShaderResourceView* type, ID3D11ShaderResourceView* sea, UINT tiles_x, UINT tiles_y,
    UINT color_texels, ComPtr<ID3D11ShaderResourceView>& fields_srv, ComPtr<ID3D11Texture2D>& color,
    ComPtr<ID3D11ShaderResourceView>& color_srv, ComPtr<ID3D11UnorderedAccessView>& color_uav) noexcept {
    HRESULT hr = S_OK;
    const UINT bind = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    ComPtr<ID3D11Texture2D> fields;
    ComPtr<ID3D11UnorderedAccessView> fields_uav;
    if (FAILED(hr = make_texture(device_.Get(), tiles_x * 2, tiles_y * 2, DXGI_FORMAT_R8G8B8A8_UNORM, 1, bind, 0, fields)) ||
        FAILED(hr = device_->CreateUnorderedAccessView(fields.Get(), nullptr, &fields_uav)) ||
        FAILED(hr = device_->CreateShaderResourceView(fields.Get(), nullptr, &fields_srv))) return hr;
    if (FAILED(hr = make_texture(device_.Get(), tiles_x * color_texels, tiles_y * color_texels, DXGI_FORMAT_R8G8B8A8_UNORM, 0,
            bind | D3D11_BIND_RENDER_TARGET, D3D11_RESOURCE_MISC_GENERATE_MIPS, color))) return hr;
    D3D11_UNORDERED_ACCESS_VIEW_DESC uav{}; uav.Format = DXGI_FORMAT_R8G8B8A8_UNORM; uav.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
    if (FAILED(hr = device_->CreateUnorderedAccessView(color.Get(), &uav, &color_uav)) ||
        FAILED(hr = device_->CreateShaderResourceView(color.Get(), nullptr, &color_srv))) return hr;
    const float zero[4]{};
    context_->ClearUnorderedAccessViewFloat(color_uav.Get(), zero);
    context_->GenerateMips(color_srv.Get());
    // Field passes (b11 carries the grid; t29 the area classes).
    {
        GroundCoverConstants grid{};
        grid.grid[0] = float(tiles_x); grid.grid[1] = float(tiles_y);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(hr = context_->Map(gc_frame_constants_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return hr;
        std::memcpy(mapped.pData, &grid, sizeof(grid)); context_->Unmap(gc_frame_constants_.Get(), 0);
    }
    ID3D11ShaderResourceView* grids[3]{area, height, ground};
    context_->CSSetShaderResources(29, 3, grids);
    context_->CSSetShaderResources(41, 1, &sea);
    context_->CSSetShaderResources(43, 1, &type);
    context_->CSSetConstantBuffers(11, 1, gc_frame_constants_.GetAddressOf());
    ID3D11UnorderedAccessView* none[1]{};
    context_->CSSetShader(gc_fields_cs_.Get(), nullptr, 0);
    context_->CSSetUnorderedAccessViews(3, 1, fields_uav.GetAddressOf(), nullptr);
    context_->Dispatch(dispatch_groups(tiles_x * 2, 8), dispatch_groups(tiles_y * 2, 8), 1);
    context_->CSSetUnorderedAccessViews(3, 1, none, nullptr);
    ID3D11ShaderResourceView* nulls[15]{};
    context_->CSSetShaderResources(29, 15, nulls);
    return S_OK;
}

// One atlas material's tiles into a colour map (gcPaintCS).
HRESULT Renderer::dispatch_ground_cover_paint(ID3D11ShaderResourceView* tiles, ID3D11ShaderResourceView* atlas,
    ID3D11UnorderedAccessView* color, std::uint32_t material, UINT tiles_x, UINT tiles_y, UINT texels) noexcept {
    D3D11_MAPPED_SUBRESOURCE mapped{};
    const HRESULT hr = context_->Map(gc_paint_constants_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) return hr;
    const std::uint32_t values[4]{material, tiles_x, tiles_y, texels};
    std::memcpy(mapped.pData, values, sizeof(values)); context_->Unmap(gc_paint_constants_.Get(), 0);
    ID3D11ShaderResourceView* srvs[2]{tiles, atlas};
    context_->CSSetShaderResources(38, 2, srvs);
    context_->CSSetConstantBuffers(13, 1, gc_paint_constants_.GetAddressOf());
    context_->CSSetSamplers(8, 1, gc_clamp_.GetAddressOf());
    context_->CSSetShader(gc_paint_cs_.Get(), nullptr, 0);
    context_->CSSetUnorderedAccessViews(3, 1, &color, nullptr);
    context_->Dispatch(dispatch_groups(tiles_x * texels, 8), dispatch_groups(tiles_y * texels, 8), 1);
    ID3D11UnorderedAccessView* none = nullptr; context_->CSSetUnorderedAccessViews(3, 1, &none, nullptr);
    ID3D11ShaderResourceView* nulls[2]{}; context_->CSSetShaderResources(38, 2, nulls);
    return S_OK;
}

// Per terrain source: shore/swamp/snow fields from the shared area classes,
// tile atlas descriptors and an empty colour map that is painted as the
// game's atlases become available.
HRESULT Renderer::update_ground_cover_maps(const AmbientWorldSource& inputs) noexcept try {
    const auto& terrain = *inputs.terrain;
    const UINT tiles_x = ambient_tiles_x_, tiles_y = ambient_tiles_y_;
    HRESULT hr = S_OK;
    if (gc_map_generation_ != ambient_map_generation_) {
        gc_fields_srv_.Reset(); gc_color_.Reset(); gc_color_srv_.Reset(); gc_color_uav_.Reset(); gc_tiles_srv_.Reset();
        gc_material_state_.clear(); gc_bark_srv_.Reset(); gc_stone_srv_.Reset(); gc_trees_srv_.Reset(); gc_map_.reset();
        gc_map_generation_ = ambient_map_generation_;
        gc_color_unpainted_ = true; gc_paint_failure_ = nullptr;
        gc_trail_recovery_ = 0; // another map: no previous trail state is read again
        // Fields per metre from the water line and a per-tile sea mask: open
        // water connected to the map edge and large (a lake or river is not
        // the sea). Built from the shared area classes' CPU grid; temporary.
        ComPtr<ID3D11ShaderResourceView> sea_srv;
        if (FAILED(hr = make_sea_srv(device_.Get(), ambient_area_, tiles_x, tiles_y, sea_srv))) return hr;
        std::vector<std::uint8_t> used;
        try {
            const auto tiles = ground_cover_tiles(terrain, tiles_x, tiles_y, used);
            ComPtr<ID3D11Buffer> buffer;
            if (FAILED(hr = make_buffer(device_.Get(), UINT(tiles.size() * sizeof(GcTileGpu)), D3D11_BIND_SHADER_RESOURCE,
                    D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, sizeof(GcTileGpu), tiles.data(), buffer, D3D11_USAGE_IMMUTABLE)) ||
                FAILED(hr = make_structured_srv(device_.Get(), buffer.Get(), UINT(tiles.size()), gc_tiles_srv_))) return hr;
        } catch (...) { return E_OUTOFMEMORY; }
        try { gc_material_state_.assign(used.begin(), used.end()); } catch (...) { return E_OUTOFMEMORY; }
        if (FAILED(hr = build_ground_cover_surfaces(ambient_area_srv_.Get(), ambient_height_srv_.Get(), ambient_ground_srv_.Get(),
                ambient_type_srv_.Get(), sea_srv.Get(), tiles_x, tiles_y, 4, gc_fields_srv_, gc_color_, gc_color_srv_, gc_color_uav_)))
            return hr;
        gc_paint_retry_frame_ = 0;
    }
    // The game's bark and stone art, when this map's objects use it.
    if (inputs.map && inputs.map != gc_map_.lock()) {
        // Temporary cold CPU grid; only its small GPU texture survives upload.
        const auto trees = ground_cover_trees(*inputs.map, tiles_x, tiles_y);
        gc_tree_mask_.assign(trees.size(), 0); ++gc_tree_epoch_;
        for (std::size_t t = 0; t < trees.size(); ++t)
            for (unsigned k = 0; k < 4; ++k) if (trees[t][k]) gc_tree_mask_[t] |= std::uint8_t(1u << k);
        D3D11_TEXTURE2D_DESC td{};td.Width=tiles_x;td.Height=tiles_y;td.MipLevels=td.ArraySize=1;
        td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.SampleDesc.Count=1;td.Usage=D3D11_USAGE_IMMUTABLE;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA initial{trees.data(),tiles_x*4,0};ComPtr<ID3D11Texture2D> texture;
        if (FAILED(hr=device_->CreateTexture2D(&td,&initial,&texture)) ||
            FAILED(hr=device_->CreateShaderResourceView(texture.Get(),nullptr,&gc_trees_srv_))) return hr;
        gc_map_ = inputs.map;
        const auto art = [&](const char* name, ComPtr<ID3D11ShaderResourceView>& srv) {
            srv.Reset();
            for (const auto& [key, texture] : inputs.map->textures) {
                const auto folded = lower_name(key);
                if (!texture || !art_texture_matches(folded, name, texture->width, texture->height) || !texture->source ||
                    texture->mips.empty() || texture->mips.size() > 16) continue;
                std::array<NativeOwnedTextureMip, 16> mips{};
                bool valid = true;
                for (std::size_t mip = 0; mip < texture->mips.size(); ++mip) {
                    const auto& m = texture->mips[mip];
                    if (m.byte_offset > texture->source->size() || m.bytes > texture->source->size() - m.byte_offset) { valid = false; break; }
                    mips[mip] = {texture->source->data() + m.byte_offset, m.width, m.height, m.bytes};
                }
                if (!valid) continue;
                NativeOwnedTextureView view{};
                view.value_source = static_cast<std::uint32_t>(texture->value_source) + 1;
                view.identity = texture->value_source == TextureValueSource::archive ? 1 : texture->value_serial;
                view.generation = 1; view.resource = texture->value_source == TextureValueSource::archive ? &texture->identity : nullptr;
                switch (texture->format) {
                case MmpFormat::dxt1: view.format = NativeOwnedTextureFormat::dxt1; break;
                case MmpFormat::dxt3: view.format = NativeOwnedTextureFormat::dxt3; break;
                case MmpFormat::rgb565: view.format = NativeOwnedTextureFormat::rgb565; break;
                case MmpFormat::argb1555: view.format = NativeOwnedTextureFormat::argb1555; break;
                case MmpFormat::argb4444: view.format = NativeOwnedTextureFormat::argb4444; break;
                default: view.format = NativeOwnedTextureFormat::argb8888; break;
                }
                view.mips = mips.data(); view.mip_count = static_cast<std::uint32_t>(texture->mips.size());
                view.opaque_mips = texture->opaque_mips;
                std::uint64_t backing = 0;
                const HRESULT upload = get_native_owned_texture(view, srv, backing, nullptr);
                if (std::strcmp(name, "tree02") == 0) {
                    char detail[384]{};
                    std::snprintf(detail, sizeof(detail),
                        "epoch=%llu key=%.96s size=%ux%u format=%u mips=%u hr=%08lx srv=%u",
                        static_cast<unsigned long long>(bridge_current_world_epoch()), key.c_str(),
                        texture->width, texture->height, unsigned(view.format), view.mip_count,
                        static_cast<unsigned long>(upload), srv ? 1u : 0u);
                    bridge_report_event("bush_bark_upload", detail);
                }
                if (SUCCEEDED(upload)) return;
                srv.Reset();
            }
        };
        art("tree02", gc_bark_srv_);
        art("stone02", gc_stone_srv_);
        {
            char detail[256]{};
            std::snprintf(detail, sizeof(detail),
                "epoch=%llu textures=%zu bark=%u stone=%u missing_bark_magenta=%u",
                static_cast<unsigned long long>(bridge_current_world_epoch()), inputs.map->textures.size(),
                gc_bark_srv_ ? 1u : 0u, gc_stone_srv_ ? 1u : 0u, gc_bark_srv_ ? 0u : 1u);
            bridge_report_event("bush_bark_state", detail);
        }
        static bool bark_missing_reported = false;
        if (!gc_bark_srv_ && !bark_missing_reported) {
            bark_missing_reported = true;
            bridge_append_probe_event("ground_cover_bark_unavailable",
                "tree02 unavailable or upload rejected; wood is magenta until its atlas is published");
        }
    }
    // Paint every material whose atlas is loaded; retry the rest now and then.
    if (!gc_material_state_.empty() && native_frame_index_ >= gc_paint_retry_frame_) {
        // Every frame while the loading screen is held, else now and then.
        gc_paint_retry_frame_ = native_frame_index_ + (load_reveal_holding_ ? 1 : 30);
        bool painted = false, pending = false;
        for (std::uint32_t material = 0; material < gc_material_state_.size(); ++material) {
            if (gc_material_state_[material] != 1) continue;
            std::shared_ptr<const OwnedVisualSurface> surface;
            ComPtr<ID3D11ShaderResourceView> atlas;std::uint64_t backing = 0;
            NativeOwnedTextureView view{};
            const char* failure = nullptr;
            if (bridge_terrain_atlas(material, surface, &failure)) {
                view.identity = surface->id; view.generation = surface->generation; view.surface = surface.get();
            }
            if (surface && FAILED(get_native_owned_texture(view, atlas, backing, nullptr))) failure = "terrain atlas upload failed";
            if (!surface || !atlas) { if (!pending) gc_paint_failure_ = failure; pending = true; continue; }
            if (FAILED(hr = dispatch_ground_cover_paint(gc_tiles_srv_.Get(), atlas.Get(), gc_color_uav_.Get(), material,
                    tiles_x, tiles_y, 4))) return hr;
            gc_material_state_[material] = 2; painted = true;
        }
        if (painted) { context_->GenerateMips(gc_color_srv_.Get()); gc_color_unpainted_ = false; gc_painted_frame_ = native_frame_index_; }
        if (!pending) { gc_material_state_.clear(); gc_tiles_srv_.Reset(); } // complete: descriptors released
    }
    return S_OK;
} catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }

// Neighbour-map cover inputs, once per uploaded neighbour (render thread,
// while its decoded map is still alive): the shared grids, sea mask, trees
// map and tile atlas cells go to immutable GPU textures; the CPU values die
// here. Fields and the colour map follow on the first cover frame with the
// pipeline ready (prepare_ground_cover_neighbour). Contact shares these inputs.
void Renderer::add_ground_cover_neighbour(const NeighbourMapUpload& upload,
    const std::array<float,16>& active_to_world,UINT contact_map) noexcept {
    const bool tile_blend = configured_integer(debug_overlay_.config(), "terrain_blend", 1) > 0;
    if (!device_ || !upload.map || !upload.map->terrain ||
        (configured_integer(debug_overlay_.config(), "ground_cover", 2) <= 0 &&
         configured_floating_point(debug_overlay_.config(), "contact_blend", .7f) <= 0 && !tile_blend)) return;
    const auto report = [&](const char* text) {
        char line[224]; std::snprintf(line, sizeof(line), "map=%s ground_cover=%s", upload.selection.placement.mpr.c_str(), text);
        bridge_neighbour_diagnostic(line);
    };
    try {
        GcNeighbour n;n.contact_map=contact_map;
        n.name = lower_name(upload.selection.placement.mpr);
        if (!neighbour_cover_origin(active_to_world, upload.selection.x, upload.selection.y, n.origin))
            return report("omitted reason=placement_not_translation");
        n.layout_origin = {neighbour_layout_tile_origin(upload.selection.placement.x), neighbour_layout_tile_origin(upload.selection.placement.y)};
        const auto& terrain = *upload.map->terrain;
        // The stream computed the CPU inputs after the weld; compute them here
        // only when it could not.
        auto inputs = upload.ground_cover_inputs ? upload.ground_cover_inputs : prepare_ground_cover_neighbour_inputs(upload);
        if (!inputs) return report("omitted reason=cpu_allocation");
        if (!inputs->grids_valid) return report("omitted reason=empty_terrain");
        const auto& grids = inputs->grids;
        n.tiles_x = grids.tiles_x; n.tiles_y = grids.tiles_y;
        n.bake_cells = inputs->bake_cells;
        n.fog_band = {active_to_world[3] + float(upload.fog_edges.x0), active_to_world[7] + float(upload.fog_edges.y0),
                      active_to_world[3] + float(upload.fog_edges.x1), active_to_world[7] + float(upload.fog_edges.y1)};
        n.lowest = terrain.sectors.front().bounds.minimum[2]; n.highest = terrain.sectors.front().bounds.maximum[2];
        for (const auto& sector : terrain.sectors) {
            n.lowest = std::min(n.lowest, sector.bounds.minimum[2]); n.highest = std::max(n.highest, sector.bounds.maximum[2]);
        }
        const UINT tx = n.tiles_x, ty = n.tiles_y, vx = tx * 2 + 1, vy = ty * 2 + 1;
        // Sand-family tiles for the beach rules join n.type once this
        // neighbour's tile families load; ETERRTYPE sand only until then.
        n.families = std::move(inputs->families);
        if (n.families.valid()) { n.base_slots = std::move(inputs->base_slots); n.slot_types = std::move(inputs->slot_types); }
        else report("beach_sand=terrain_type_only reason=no_resource_mounts");
        const auto& used = inputs->used;
        const auto& tiles = inputs->tiles;
        if (n.families.valid()) n.base_lanes = std::move(inputs->base_lanes);
        const auto& trees = inputs->trees;
        n.tile_classes.resize(std::size_t(tx) * ty);
        for (std::size_t t = 0; t < n.tile_classes.size(); ++t) {
            std::uint32_t channels = 0;
            for (unsigned k = 0; k < 4; ++k) if (t < trees.size() && trees[t][k]) channels |= 1u << k;
            n.tile_classes[t] = std::uint32_t(grids.area[t]) | (std::uint32_t(grids.types[t]) << 8) | (channels << 16);
        }
        ComPtr<ID3D11Buffer> tile_buffer;
        if (FAILED(make_grid_srv(device_.Get(), tx, ty, DXGI_FORMAT_R8_UINT, grids.area.data(), tx, n.area)) ||
            FAILED(make_grid_srv(device_.Get(), tx, ty, DXGI_FORMAT_R8_UINT, grids.types.data(), tx, n.type)) ||
            FAILED(make_grid_srv(device_.Get(), vx, vy, DXGI_FORMAT_R32_FLOAT, grids.height.data(), vx * 4, n.height)) ||
            FAILED(make_grid_srv(device_.Get(), vx, vy, DXGI_FORMAT_R32_FLOAT, grids.ground.data(), vx * 4, n.ground)) ||
            FAILED(make_grid_srv(device_.Get(), tx, ty, DXGI_FORMAT_R8G8B8A8_UNORM, trees.data(), tx * 4, n.trees)) ||
            FAILED(make_sea_srv(device_.Get(), grids.area, tx, ty, n.sea)) ||
            FAILED(make_buffer(device_.Get(), UINT(tiles.size() * sizeof(GcTileGpu)), D3D11_BIND_SHADER_RESOURCE,
                D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, sizeof(GcTileGpu), tiles.data(), tile_buffer, D3D11_USAGE_IMMUTABLE)) ||
            FAILED(make_structured_srv(device_.Get(), tile_buffer.Get(), UINT(tiles.size()), n.tiles)))
            return report("omitted reason=grid_upload_failed");
        // The neighbour terrain's own atlas pages, as its terrain draws use
        // them (same view, so the owned texture pool returns the same SRV).
        n.atlases.resize(used.size());
        UINT missing = 0;
        for (std::size_t material = 0; material < used.size(); ++material) {
            if (!used[material]) continue;
            const auto page_index = material % (terrain.texture_count + 2u);
            if (page_index >= terrain.pages.size() || !terrain.pages[page_index].texture || terrain.pages[page_index].mips.empty()) {
                ++missing; continue;
            }
            const auto& page = terrain.pages[page_index];
            NativeOwnedTextureView view{};
            view.identity = page.texture->identity.container_incarnation; view.generation = 1;
            view.resource = &page.texture->identity; view.value_source = 1;
            view.format = static_cast<NativeOwnedTextureFormat>(page.texture->format);
            view.mips = page.mips.data(); view.mip_count = static_cast<std::uint32_t>(page.mips.size());
            std::uint64_t backing = 0;
            if (FAILED(get_native_owned_texture(view, n.atlases[material], backing, nullptr, true))) { n.atlases[material].Reset(); ++missing; }
        }
        if(configured_floating_point(debug_overlay_.config(),"contact_blend",.7f)>0||
           configured_integer(debug_overlay_.config(),"ground_cover",2)>0||tile_blend){
            if(FAILED(build_contact_tiles(terrain,tx,ty,n.contact,&n.contact_rows)))return report("contact omitted reason=tile_upload_failed");
            // The neighbour's terrain draws read this grid (terrain cliffs and
            // the material blend at its own origin), so it carries the blend
            // half like the active map's: no material until the families load.
            if(!n.contact_rows.empty()){
                auto grid=n.contact_rows;
                grid.resize(grid.size()*2,std::array<std::uint32_t,4>{~0u,~0u,0u,~0u});
                if(FAILED(make_grid_srv(device_.Get(),tx,ty*2,DXGI_FORMAT_R32G32B32A32_UINT,grid.data(),tx*16u,n.contact.tiles)))
                    return report("contact omitted reason=tile_upload_failed");
                if(tile_blend&&n.families.valid()&&terrain.tiles_per_texture_side==8)
                    n.blend_input=inputs->blend_input?inputs->blend_input:
                        std::make_shared<TerrainTileBlendInput>(owned_terrain_tile_blend_input(terrain,tx,ty));
                else std::vector<std::array<std::uint32_t,4>>().swap(n.contact_rows);
            }
            for(UINT material=0;material<n.atlases.size()&&material<contact_blend_max_materials;++material){
                if(!n.atlases[material]||!(n.contact.pending&(std::uint64_t{1}<<material)))continue;
                ComPtr<ID3D11Resource> resource;n.atlases[material]->GetResource(&resource);
                ComPtr<ID3D11Texture2D> texture;
                D3D11_SHADER_RESOURCE_VIEW_DESC desc{};n.atlases[material]->GetDesc(&desc);
                if(FAILED(resource.As(&texture))||desc.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2D||
                   copy_contact_atlas(texture.Get(),0,material,n.contact)!=S_OK)
                    report("contact omitted reason=atlas_copy_failed");
            }
        }
        // Resident after preparation: grids, trees, fields (per metre) and the
        // colour map (2 texels per tile, mips). Sea, tiles and atlas
        // references are released once painted.
        const std::uint64_t t = std::uint64_t(tx) * ty, v = std::uint64_t(vx) * vy;
        n.bytes = t * 2 + v * 8 + t * 4 + t * 4 * 4 + t * 4 * 4 * 4 / 3;
        if (missing) {
            char text[96]; std::snprintf(text, sizeof(text), "partial unpainted_materials=%u (cover absent there)", missing);
            report(text);
        }
        gc_neighbours_.push_back(std::move(n));
    } catch (...) { report("omitted reason=cpu_allocation"); }
}

void Renderer::apply_neighbour_tile_families(GcNeighbour& n) noexcept try {
    if (!n.families.valid() || n.families.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    const auto families = n.families.get();
    const auto sand = terrain_tile_sand_slots(families);
    const auto bare = terrain_tile_bare_corners(families);
    const auto count = std::count(sand.begin(), sand.end(), true);
    ComPtr<ID3D11ShaderResourceView> type;
    if (count || std::any_of(bare.begin(), bare.end(), [](std::uint8_t m) { return m != 0; })) {
        const auto types = ambient::terrain_cover_types(n.base_slots, n.base_lanes, n.slot_types, sand, bare);
        if (SUCCEEDED(make_grid_srv(device_.Get(), n.tiles_x, n.tiles_y, DXGI_FORMAT_R16_UINT, types.data(), n.tiles_x * 2, type)))
            n.type = type;
    }
    ++n.type_epoch; // its placement cache regenerates with the families
    char line[320]; std::snprintf(line, sizeof(line), "map=%s ground_cover_beach_sand=%s sand_slots=%lld (%s)", n.name.c_str(),
        type ? "terrain_type_or_family" : "terrain_type_only", static_cast<long long>(count), families.source.c_str());
    bridge_neighbour_diagnostic(line);
    std::vector<std::uint16_t>().swap(n.base_slots); std::vector<std::uint32_t>().swap(n.slot_types);
    std::vector<std::uint8_t>().swap(n.base_lanes);
    // Terrain material blend: the neighbour's own families and plain cells
    // in the blend half of its contact grid (the same rows the active map
    // builds). Without them its transitions keep their tile art.
    if (n.blend_input && n.contact_rows.size() == std::size_t(n.tiles_x) * n.tiles_y) {
        const auto& input = *n.blend_input;
        char text[480];
        if (families.signatures.size() == input.slots && input.tiles_x == n.tiles_x && input.tiles_y == n.tiles_y) {
            std::size_t blended = 0, plain = 0, bed = 0;
            const auto rows_start = std::chrono::steady_clock::now();
            const auto rows = owned_terrain_tile_blend_rows(input, families, blended, plain, bed);
            const double rows_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - rows_start).count();
            auto grid = std::move(n.contact_rows);
            grid.insert(grid.end(), rows.begin(), rows.end());
            ComPtr<ID3D11ShaderResourceView> tiles;
            const bool ok = SUCCEEDED(make_grid_srv(device_.Get(), n.tiles_x, n.tiles_y * 2, DXGI_FORMAT_R32G32B32A32_UINT,
                grid.data(), n.tiles_x * 16u, tiles));
            if (ok) n.contact.tiles = tiles;
            std::snprintf(text, sizeof(text), "map=%s terrain_tile_blend=%s %zu of %zu tiles near transitions blend, %zu plain materials, %zu liquid-bed families, rows %.2f ms (%s)",
                n.name.c_str(), ok ? "ready" : "omitted reason=grid_upload_failed", blended, rows.size(), plain, bed, rows_ms, families.source.c_str());
        } else {
            std::snprintf(text, sizeof(text), "map=%s terrain_tile_blend=omitted reason=no_families (%s)", n.name.c_str(), families.source.c_str());
        }
        bridge_neighbour_diagnostic(text);
    }
    n.blend_input.reset(); std::vector<std::array<std::uint32_t,4>>().swap(n.contact_rows);
} catch (...) {
    n.blend_input.reset(); std::vector<std::array<std::uint32_t,4>>().swap(n.contact_rows);
    bridge_neighbour_diagnostic("neighbour tile families could not be applied; its transitions keep their tile art");
}

HRESULT Renderer::prepare_ground_cover_neighbour(GcNeighbour& n) noexcept {
    ComPtr<ID3D11UnorderedAccessView> color_uav;
    HRESULT hr = build_ground_cover_surfaces(n.area.Get(), n.height.Get(), n.ground.Get(), n.type.Get(), n.sea.Get(),
        n.tiles_x, n.tiles_y, 2, n.fields, n.color_texture, n.color, color_uav);
    if (FAILED(hr)) return hr;
    for (std::size_t material = 0; material < n.atlases.size(); ++material) {
        if (!n.atlases[material]) continue;
        if (FAILED(hr = dispatch_ground_cover_paint(n.tiles.Get(), n.atlases[material].Get(), color_uav.Get(),
                std::uint32_t(material), n.tiles_x, n.tiles_y, 2))) return hr;
    }
    context_->GenerateMips(n.color.Get());
    n.sea.Reset(); n.tiles.Reset(); std::vector<ComPtr<ID3D11ShaderResourceView>>().swap(n.atlases);
    n.prepared = true;
    char line[224]; std::snprintf(line, sizeof(line),
        "map=%s ground_cover=ready gpu_bytes=%llu tiles=%ux%u origin=%.0f,%.0f density=%.1f far_meshes_only=1",
        n.name.c_str(), static_cast<unsigned long long>(n.bytes), n.tiles_x, n.tiles_y, n.origin[0], n.origin[1], kNeighbourCoverDensity);
    bridge_neighbour_diagnostic(line);
    return S_OK;
}


// ---------------------------------------------------------------- placement bounds
namespace {
constexpr UINT kGcCandidateBytes = 48, kGcSurvivorBytes = 80;
// The shader's gcTypeArea (ambient_particles.h area_from_terrain_type).
std::uint32_t gc_type_area(std::uint32_t type) {
    if (type == 0) return 1; if (type == 5 || type == 11) return 2; if (type == 3) return 3;
    if (type == 9 || type == 10 || type == 12) return 4; if (type == 14) return 5; if (type == 6) return 6; if (type == 13) return 7; return 0;
}
// A cell's slot count for one rule at eye distance d (the shader's
// gcCellSlots at its nearest point), plus one slot against float rounding;
// never more than the rule's slots per cell.
UINT gc_slot_count(const GroundCoverRule& r, float density, float distance, float d) {
    const float slots = std::ceil(r.density * density * kCellSize * kCellSize);
    const float lo = r.lod_near * distance, hi = r.lod_far * distance;
    const float t = std::clamp((d - lo) / (hi - lo), 0.f, 1.f);
    const float keep = std::max(r.far_keep, 1 - t * t * (3 - 2 * t));
    return std::min(UINT(std::ceil(slots * keep)) + 1u, UINT(slots));
}
// Slots of one cell at full density over every rule: a round passes its
// split by at most this.
UINT gc_cell_slot_bound(float density) {
    UINT total = 0;
    for (const auto& r : rule_table()) total += UINT(std::ceil(r.density * density * kCellSize * kCellSize));
    return total;
}
// Summed-area table of one tile flag, for radius queries.
struct GcTileSums {
    UINT w = 0, h = 0; std::vector<std::uint32_t> sum;
    template <class Flag> void build(const std::vector<std::uint32_t>& tiles, UINT tx, UINT ty, Flag flag) {
        w = tx; h = ty; sum.assign(std::size_t(tx + 1) * (ty + 1), 0);
        for (UINT y = 0; y < ty; ++y) for (UINT x = 0; x < tx; ++x)
            sum[std::size_t(y + 1) * (tx + 1) + x + 1] = (flag(tiles[std::size_t(y) * tx + x]) ? 1u : 0u) +
                sum[std::size_t(y) * (tx + 1) + x + 1] + sum[std::size_t(y + 1) * (tx + 1) + x] - sum[std::size_t(y) * (tx + 1) + x];
    }
    bool any(int x0, int y0, int x1, int y1) const { // tiles [x0, x1] x [y0, y1], clipped
        x0 = std::max(x0, 0); y0 = std::max(y0, 0); x1 = std::min(x1, int(w) - 1); y1 = std::min(y1, int(h) - 1);
        if (x1 < x0 || y1 < y0) return false;
        const auto at = [&](int x, int y) { return sum[std::size_t(y) * (w + 1) + x]; };
        return at(x1 + 1, y1 + 1) - at(x0, y1 + 1) - at(x1 + 1, y0) + at(x0, y0) != 0;
    }
};
std::uint64_t gc_hash(std::uint64_t h, std::uint64_t v) { h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); return h; }

// Every rule that can place a candidate in a cell, from the map's tiles and
// zone pieces: a superset of the shader's acceptance (zones within 8 m, the
// candidate tile's surface classes, base-type rules, and the fields' reach:
// swamp, water and lava within their accepted distance plus the 1 m field
// texels, tree influence within its bilinear texel). Bounds the stage
// buffers: a rule left out could never have placed there.
std::vector<std::uint64_t> gc_cell_rule_masks(const std::vector<std::uint32_t>& tiles, UINT tiles_x, UINT tiles_y,
                                              const std::vector<std::array<float, 5>>& zones, const float origin[3]) {
    const auto& rules = rule_table();
    static_assert(std::tuple_size<std::decay_t<decltype(rule_table())>>::value <= 64, "64-bit rule masks per cell");
    const UINT cells_x = (tiles_x + 1) / 2, cells_y = (tiles_y + 1) / 2;
    std::vector<std::uint64_t> masks(std::size_t(cells_x) * cells_y, 0);
    GcTileSums swamp, water, lava, tree[4];
    swamp.build(tiles, tiles_x, tiles_y, [](std::uint32_t v) { return (v & 255u) != 255u && (v & 15u) == 5u; });
    water.build(tiles, tiles_x, tiles_y, [](std::uint32_t v) { return (v & 255u) != 255u && (v & 15u) == 6u; });
    lava.build(tiles, tiles_x, tiles_y, [](std::uint32_t v) { return (v & 255u) != 255u && (v & 15u) == 7u; });
    for (unsigned k = 0; k < 4; ++k) tree[k].build(tiles, tiles_x, tiles_y, [k](std::uint32_t v) { return ((v >> 16) & (1u << k)) != 0; });
    for (UINT cy = 0; cy < cells_y; ++cy) for (UINT cx = 0; cx < cells_x; ++cx) {
        const float lo[2]{cx * kCellSize + origin[0], cy * kCellSize + origin[1]}, hi[2]{lo[0] + kCellSize, lo[1] + kCellSize};
        std::uint32_t cell_zones = 0;
        for (const auto& z : zones)
            // The shader's 8 m, a hair wider against float rounding (a superset stays exact).
            if (hi[0] > z[0] - 8.01f && hi[1] > z[1] - 8.01f && lo[0] < z[2] + 8.01f && lo[1] < z[3] + 8.01f) cell_zones |= 1u << std::uint32_t(z[4]);
        const int tx0 = int(cx) * 2, ty0 = int(cy) * 2;
        std::uint64_t mask = 0;
        for (std::uint32_t index = 0; index < rules.size(); ++index) {
            const auto& r = rules[index];
            if ((r.zones & cell_zones) == 0) continue;
            const bool underwater = ((r.areas >> 6) & 1u) != 0, beach = r.field == fd_beach;
            bool ok = false;
            for (int dy = 0; dy < 2 && !ok; ++dy) for (int dx = 0; dx < 2 && !ok; ++dx) {
                const int x = tx0 + dx, y = ty0 + dy;
                if (x >= int(tiles_x) || y >= int(tiles_y)) continue;
                const std::uint32_t v = tiles[std::size_t(y) * tiles_x + x], area = v & 255u, type = (v >> 8) & 255u, base = type == 255u ? 255u : (type & 15u);
                bool tile = false;
                if (underwater) tile = area != 255u && ((r.areas >> (area & 15u)) & 1u) != 0;
                else if (base != 255u) {
                    std::uint32_t classes = 1u << gc_type_area(base);
                    if (base == 0 || base == 5 || base == 11) classes |= 1u;  // soil/paving family over a grassy type
                    if (beach) classes |= 1u << 3;                             // sand-family tile
                    tile = (r.areas & classes) != 0;
                }
                if ((r.flags & rf_rock) && base != 2 && base != 4 && base != 7 && base != 15) tile = false;
                if (r.mesh == gm_dead_bush && base != 1 && base != 5 && base != 11) tile = false;
                if ((r.flags & rf_snow_pile) && !(r.flags & rf_sand_pile) && base != 9 && base != 12) tile = false;
                ok = tile;
            }
            if (!ok) continue;
            const auto reaches = [&](const GcTileSums& sums, int radius) { return sums.any(tx0 - radius, ty0 - radius, tx0 + 1 + radius, ty0 + 1 + radius); };
            if (r.field == fd_swamp_edge && !reaches(swamp, 4)) continue;
            if ((r.field == fd_beach || r.field == fd_river) && !reaches(water, 5)) continue;
            if (r.field == fd_lava_edge && !reaches(lava, 5)) continue;
            if (r.field == fd_undead && !reaches(tree[3], 2)) continue;
            const float trees = r.near_trees;
            if (trees < 0 && !reaches(tree[1], 2)) continue;
            if (trees == 1 && !reaches(tree[0], 2)) continue;
            if (trees == 3 && !reaches(tree[2], 2)) continue;
            if (trees == 2 && !reaches(tree[1], 2) && !reaches(tree[2], 2)) continue;
            mask |= std::uint64_t{1} << index;
        }
        masks[std::size_t(cy) * cells_x + cx] = mask;
    }
    return masks;
}

// This frame's candidate bound per class over a placement window: every cell
// whose box (the map's lowest and highest ground, the shader's margins) can
// touch the frustum, each rule of its mask at the cell's horizontal distance
// from the eye rounded down to 4 m (never farther than the shader's
// distance, so never fewer slots).
std::array<UINT, 6> gc_window_bounds(const std::vector<std::uint64_t>& masks, UINT cells_x, const float eye[3], int x0, int y0, int x1, int y1,
                                     float low, float high, const float (*planes)[4], const float origin[3], float density, float distance,
                                     const std::array<std::vector<std::uint32_t>, 6>& class_rules) {
    const auto& rules = rule_table();
    std::array<UINT, 6> bounds{};
    if (masks.empty()) return bounds;
    // Rules are immutable. Counts depend on quality and distance bands only,
    // not on the camera, map, frustum or selected classes.
    struct CountCache {
        std::vector<UINT> table;
        UINT bands = 0;
        float density = -1, distance = -1;
    };
    static thread_local CountCache counts;
    if(counts.density!=density || counts.distance!=distance) {
        counts.table.clear();counts.bands=0;
        counts.density=density;counts.distance=distance;
    }
    auto& table=counts.table;
    auto& bands=counts.bands;
    const auto count = [&](UINT band, std::uint32_t r) {
        while (band >= bands) {
            // Reserve the whole row before mutation, so allocation failure
            // cannot leave a partial cached band.
            const auto required=(std::size_t(bands)+1)*rules.size();
            if(required>table.capacity())table.reserve((std::max)(required,table.capacity()*2));
            for (std::uint32_t k = 0; k < rules.size(); ++k) table.push_back(gc_slot_count(rules[k], density, distance, bands * 4.f));
            ++bands;
        }
        return table[std::size_t(band) * rules.size() + r];
    };
    for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) {
        const std::uint64_t mask = masks[std::size_t(y) * cells_x + x];
        if (!mask) continue;
        // Height margins wider than the shader's (liquid surfaces above the ground bounds).
        const float lo[3]{x * kCellSize + origin[0], y * kCellSize + origin[1], low + origin[2] - 32},
                    hi[3]{lo[0] + kCellSize, lo[1] + kCellSize, high + origin[2] + 32};
        bool visible = true;
        for (unsigned k = 0; k < 6 && visible; ++k) {
            const float v[3]{planes[k][0] >= 0 ? hi[0] : lo[0], planes[k][1] >= 0 ? hi[1] : lo[1], planes[k][2] >= 0 ? hi[2] : lo[2]};
            if (planes[k][0] * v[0] + planes[k][1] * v[1] + planes[k][2] * v[2] + planes[k][3] < -1e-3f) visible = false;
        }
        if (!visible) continue;
        const float dx = std::max({lo[0] - eye[0], 0.f, eye[0] - hi[0]}), dy = std::max({lo[1] - eye[1], 0.f, eye[1] - hi[1]});
        const UINT band = UINT(std::sqrt(dx * dx + dy * dy) / 4.f);
        for (unsigned c = 0; c < 6; ++c)
            for (const std::uint32_t r : class_rules[c]) if ((mask >> r) & 1u) bounds[c] += count(band, r);
    }
    return bounds;
}
} // namespace

// Candidate and survivor buffers for one round (exact: every slot of a round
// can survive), sized before the frame constants carry them.
HRESULT Renderer::gc_ensure_stage_buffers(UINT capacity) {
    if (capacity <= gc_candidate_capacity_) return S_OK;
    gc_candidates_.Reset(); gc_candidate_uav_.Reset(); gc_survivors_.Reset(); gc_survivor_uav_.Reset();
    gc_candidate_capacity_ = gc_survivor_capacity_ = 0;
    const auto stage_buffer = [&](UINT stride, ComPtr<ID3D11Buffer>& buffer, ComPtr<ID3D11UnorderedAccessView>& uav) {
        HRESULT result = make_buffer(device_.Get(), capacity * stride, D3D11_BIND_UNORDERED_ACCESS, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,
            stride, nullptr, buffer);
        D3D11_UNORDERED_ACCESS_VIEW_DESC v{}; v.Format = DXGI_FORMAT_UNKNOWN; v.ViewDimension = D3D11_UAV_DIMENSION_BUFFER; v.Buffer.NumElements = capacity;
        if (SUCCEEDED(result)) result = device_->CreateUnorderedAccessView(buffer.Get(), &v, &uav);
        return result;
    };
    HRESULT hr;
    if (FAILED(hr = stage_buffer(kGcCandidateBytes, gc_candidates_, gc_candidate_uav_)) ||
        FAILED(hr = stage_buffer(kGcSurvivorBytes, gc_survivors_, gc_survivor_uav_))) return hr;
    gc_candidate_capacity_ = gc_survivor_capacity_ = capacity;
    return S_OK;
}

// Cell job lists: per class one entry per window cell (16 bytes).
HRESULT Renderer::gc_ensure_cell_jobs(UINT cells) {
    if (cells <= gc_cell_job_capacity_ && gc_cell_jobs_) return S_OK;
    const UINT capacity = std::max(cells + cells / 4, 1024u), count = capacity * kGcPlaceClasses;
    gc_cell_jobs_.Reset(); gc_cell_job_uav_.Reset(); gc_cell_job_srv_.Reset(); gc_cell_job_capacity_ = 0;
    HRESULT hr = make_buffer(device_.Get(), count * 16, D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE,
        D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 16, nullptr, gc_cell_jobs_);
    D3D11_UNORDERED_ACCESS_VIEW_DESC u{}; u.Format = DXGI_FORMAT_UNKNOWN; u.ViewDimension = D3D11_UAV_DIMENSION_BUFFER; u.Buffer.NumElements = count;
    if (SUCCEEDED(hr)) hr = device_->CreateUnorderedAccessView(gc_cell_jobs_.Get(), &u, &gc_cell_job_uav_);
    if (SUCCEEDED(hr)) hr = make_structured_srv(device_.Get(), gc_cell_jobs_.Get(), count, gc_cell_job_srv_);
    if (SUCCEEDED(hr)) gc_cell_job_capacity_ = capacity;
    return hr;
}

HRESULT Renderer::render_ground_cover(const FixedState& frame, const std::array<float,16>& world_to_clip) noexcept {
 PERF06_SCOPE("ground_cover_total");
    const int level = configured_integer(debug_overlay_.config(), "ground_cover", 2);
    if (level <= 0) {
        // Neighbour contact owns a use of these same grids and paint resources.
        if(configured_floating_point(debug_overlay_.config(),"contact_blend",.7f)<=0&&
            (gc_place_cs_||gc_failed_))release_ground_cover();
        return S_OK;
    }
    const auto fail = [&](const char* reason) {
        gc_failed_ = true; OutputDebugStringA(reason); OutputDebugStringA("\n");
        bridge_append_probe_event("ground_cover_omitted", reason);
        return S_OK;
    };
    // A world frame whose cover draws nothing for a missing input: one line
    // once the same reason has held for kSkipReportMs (the shader compile,
    // map entry and atlas loading normally pass sooner). Drawing re-arms it.
    const auto skip = [&](const char* reason) {
        if (gc_skip_frame_ == native_frame_index_) return S_OK;
        gc_skip_frame_ = native_frame_index_;
        const ULONGLONG now = GetTickCount64();
        if (reason != gc_skip_reason_) { gc_skip_reason_ = reason; gc_skip_since_ms_ = now; gc_skip_reported_ = false; }
        constexpr ULONGLONG kSkipReportMs = 10000;
        if (!gc_skip_reported_ && now - gc_skip_since_ms_ >= kSkipReportMs) {
            gc_skip_reported_ = true;
            char detail[192];
            std::snprintf(detail, sizeof(detail), "%s; no cover drawn for %llu s", reason,
                static_cast<unsigned long long>((now - gc_skip_since_ms_) / 1000));
            bridge_append_probe_event("ground_cover_skipped", detail);
        }
        return S_OK;
    };
    try {
    // One draw per world frame (a later call in a drawn frame is not a skip).
    if (gc_failed_ || !device_ || !context_ || gc_drawn_frame_ == native_frame_index_) return S_OK;
    if (!rtv_ || !dsv_) return skip("no world colour/depth target");
    if (scene_composited_) return skip("world already composited before the cover pass");
    const HRESULT pipeline = ensure_ground_cover_pipeline();
    if (FAILED(pipeline)) return fail("EID3D11 ground cover: shaders or pipeline objects unavailable; ground cover omitted.");
    if (pipeline == S_FALSE) return skip("shaders still compiling");
    // Only in a frame whose terrain was drawn.
    if (ambient_terrain_frame_ != native_frame_index_) return skip("no terrain frame published this frame");
    gc_drawn_frame_ = native_frame_index_;
    // The CPU preparation below (scene walks, uploads) is ground cover work;
    // open the scope before it so it is not billed to the preceding pass.
    transition_profile_pass("ground_cover");
    AmbientWorldSource inputs;
    if (!bridge_ambient_world_source(inputs) || !inputs.terrain) return skip("no retained terrain source");
    const WorldEnvironment environment = bridge_world_fog_snapshot().environment;
    if (!environment.valid) return skip("game environment snapshot invalid this frame");
    // The main-menu map is a real outdoor scene: it grows cover like any map
    // (ambient particles and critters keep their own menu exclusion).
    if (!rebuild_ambient_particle_map(inputs, environment) || !ambient_ground_srv_ || !ambient_type_srv_)
        return skip("shared area/height grid unavailable");
    // Cover accepts sand corners and suppresses meadow cover on plain soil/
    // paving families once they load off-thread; per-tile types until then.
    if (gc_sand_generation_ != ambient_map_generation_ && poll_terrain_tile_families()) {
        gc_sand_generation_ = ambient_map_generation_;
        const auto sand = terrain_tile_sand_slots(tile_families_);
        const auto bare = terrain_tile_bare_corners(tile_families_);
        const auto soft = terrain_tile_soft_sand_corners(tile_families_);
        gc_soft_sand_ = std::any_of(soft.begin(), soft.end(), [](std::uint8_t m) { return m != 0; });
        const auto count = std::count(sand.begin(), sand.end(), true);
        ComPtr<ID3D11ShaderResourceView> type;
        if (count || std::any_of(bare.begin(), bare.end(), [](std::uint8_t m) { return m != 0; })) {
            std::vector<std::uint8_t> used;
            const auto lanes = ground_cover_corner_lanes(ground_cover_tiles(*inputs.terrain, ambient_tiles_x_, ambient_tiles_y_, used));
            const auto types = ambient::terrain_cover_types(ambient::terrain_base_slots(*inputs.terrain), lanes, inputs.terrain->tile_types, sand, bare, soft);
            if (types.size() == std::size_t(ambient_tiles_x_) * ambient_tiles_y_ &&
                SUCCEEDED(make_grid_srv(device_.Get(), ambient_tiles_x_, ambient_tiles_y_, DXGI_FORMAT_R16_UINT, types.data(), ambient_tiles_x_ * 2, type)))
                ambient_type_srv_ = type;
        }
        char detail[320]; std::snprintf(detail, sizeof(detail), "%s: %lld sand slots (%s)",
            type ? "terrain type or sand family" : "terrain type only", static_cast<long long>(count), tile_families_.source.c_str());
        bridge_append_probe_event("ground_cover_beach_sand", detail);
    }
    if (FAILED(update_ground_cover_maps(inputs)))
        return fail("EID3D11 ground cover: terrain fields or colour map unavailable; ground cover omitted.");
    // Prepare before publishing the blend field: failed pieces never flavour
    // another map's cover. Only compact rectangles survive on the CPU.
    struct ZonePiece { float bounds[4]; std::uint32_t zone, padding[3]{}; };
    static_assert(sizeof(ZonePiece)==32);
    std::vector<ZonePiece> zone_pieces;
    zone_pieces.push_back({{0,0,float(ambient_tiles_x_)*2,float(ambient_tiles_y_)*2},std::uint32_t(ambient_zone_)});
    for(std::size_t i=0;i<gc_neighbours_.size();) {
        auto& n=gc_neighbours_[i];
        if(!n.prepared && FAILED(prepare_ground_cover_neighbour(n))) {
            char line[192];std::snprintf(line,sizeof(line),"map=%s ground_cover=omitted reason=fields_or_colour_map_unavailable",n.name.c_str());
            bridge_neighbour_diagnostic(line);
            gc_neighbours_.erase(gc_neighbours_.begin()+std::ptrdiff_t(i));
            continue;
        }
        const auto zone=ambient::classify_zone(n.name.c_str(),false,environment.region);
        zone_pieces.push_back({{n.origin[0],n.origin[1],n.origin[0]+float(n.tiles_x)*2,n.origin[1]+float(n.tiles_y)*2},std::uint32_t(zone)});
        ++i;
    }
    // Compare current authoritative placements each frame. Only changed bytes
    // need uploading; the SRV dimensions remain the exact live piece count.
    const UINT zone_bytes=UINT(zone_pieces.size()*sizeof(ZonePiece));
    D3D11_BUFFER_DESC zone_desc{};
    if(gc_zone_pieces_)gc_zone_pieces_->GetDesc(&zone_desc);
    if(zone_desc.ByteWidth!=zone_bytes) {
        gc_zone_srv_.Reset();gc_zone_pieces_.Reset();gc_zone_uploaded_values_.clear();
        if(FAILED(make_buffer(device_.Get(),zone_bytes,D3D11_BIND_SHADER_RESOURCE,D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,
            sizeof(ZonePiece),nullptr,gc_zone_pieces_,D3D11_USAGE_DYNAMIC,D3D11_CPU_ACCESS_WRITE)) ||
           FAILED(make_structured_srv(device_.Get(),gc_zone_pieces_.Get(),UINT(zone_pieces.size()),gc_zone_srv_)))
            return fail("EID3D11 ground cover: zone blend buffer unavailable; cover omitted.");
    }
    if(gc_zone_uploaded_values_.size()!=zone_bytes ||
       std::memcmp(gc_zone_uploaded_values_.data(),zone_pieces.data(),zone_bytes)!=0) {
        const auto* bytes=reinterpret_cast<const std::uint8_t*>(zone_pieces.data());
        std::vector<std::uint8_t> next(bytes,bytes+zone_bytes);
        D3D11_MAPPED_SUBRESOURCE zone_mapped{};
        if(FAILED(context_->Map(gc_zone_pieces_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&zone_mapped)))
            return fail("EID3D11 ground cover: zone blend upload failed; cover omitted.");
        std::memcpy(zone_mapped.pData,zone_pieces.data(),zone_bytes);context_->Unmap(gc_zone_pieces_.Get(),0);
        gc_zone_uploaded_values_.swap(next);
    }
    if(FAILED(update_contact_blend_maps()))
        return fail("EID3D11 snow terrain art unavailable; cover omitted.");
    const Quality quality = quality_level(level);
    if (level != gc_level_) { gc_level_ = level; gc_capacity_.fill(0); gc_instances_.Reset(); }

    // Camera.
    float inverse[16]{};
    if (!ambient_occlusion_invert(world_to_clip.data(), inverse) || std::abs(inverse[14]) < 1e-12f)
        return skip("world camera not invertible");
    const float eye[3]{inverse[2] / inverse[14], inverse[6] / inverse[14], inverse[10] / inverse[14]};
    // Cover reaches every ground height the camera can show. Sector bounds
    // come from the decoded terrain, including lower ground beyond the eye.
    if (inputs.terrain->sectors.empty()) return skip("terrain has no sectors");
    float lowest = inputs.terrain->sectors.front().bounds.minimum[2];
    float highest = inputs.terrain->sectors.front().bounds.maximum[2];
    for (const auto& sector : inputs.terrain->sectors) {
        lowest = std::min(lowest, sector.bounds.minimum[2]);
        highest = std::max(highest, sector.bounds.maximum[2]);
    }
    const float map_x = float(ambient_tiles_x_) * 2, map_y = float(ambient_tiles_y_) * 2;
    // F10 ground_cover_distance: its own limit, not the draw distance.
    const float cover_limit = float(configured_integer(debug_overlay_.config(), "ground_cover_distance", 200));
    const float reach = std::min(visible_ground_reach(inverse, eye, lowest, highest, map_x, map_y), cover_limit);
    if (!(reach > 0)) return skip("camera shows no ground");
    const int cells_x = int(std::ceil(map_x / kCellSize)), cells_y = int(std::ceil(map_y / kCellSize));
    const int x0 = std::max(0, int(std::floor((eye[0] - reach) / kCellSize))), y0 = std::max(0, int(std::floor((eye[1] - reach) / kCellSize)));
    const int x1 = std::min(cells_x, int(std::ceil((eye[0] + reach) / kCellSize))), y1 = std::min(cells_y, int(std::ceil((eye[1] + reach) / kCellSize)));
    if (x1 <= x0 || y1 <= y0) return skip("camera window outside the map");

    // Pools: sized from the counts of an earlier frame (non-blocking readback).
    // Every finished readback is read, oldest first; a slot still in flight
    // is never copied into again. The former 3-slot ring re-copied each slot
    // every frame, so with the GPU 3 frames behind (default frame latency)
    // no copy was ever finished when mapped: 479 frames without a count in
    // a zone2 neighbour-corner run, while the pool overflowed and flickered.
    while (gc_readback_pending_) {
        auto& staging = gc_count_readback_[gc_readback_head_];
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context_->Map(staging.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped))) break;
        gc_readback_head_ = (gc_readback_head_ + 1) % UINT(gc_count_readback_.size()); --gc_readback_pending_;
        {
            const auto* counts = static_cast<const std::uint32_t*>(mapped.pData);
            for (unsigned m = 0; m < gm_count; ++m)
                if (counts[m] > gc_capacity_[m] - gc_capacity_[m] / 10) { // grow at 90%, before the 95% thinning
                    gc_capacity_[m] = counts[m] + counts[m] / 4 + 64; gc_grow_ = true;
                }
            context_->Unmap(staging.Get(), 0);
        }
    }
    if (!gc_instances_ || gc_grow_) {
        if (!gc_instances_) for (const auto& rule : rule_table()) { // first estimate (densest rule); demand corrects it
            const float band = .5f * (rule.lod_near + rule.lod_far) * quality.distance;
            gc_capacity_[rule.mesh] = std::max(gc_capacity_[rule.mesh], UINT(rule.density * quality.density * 3.14159f * band * band * .3f) + 64);
        }
        UINT total = 0;
        for (unsigned m = 0; m < gm_count; ++m) { gc_base_[m] = total; total += gc_capacity_[m]; }
        if (total > kGcStageLimit) return fail("EID3D11 ground cover: instance pools exceed one shadow dispatch; ground cover omitted.");
        gc_instances_.Reset(); gc_instance_srv_.Reset(); gc_instance_uav_.Reset();
        gc_shade_input_.Reset(); gc_shade_input_uav_.Reset();
        HRESULT hr = make_buffer(device_.Get(), total * UINT(sizeof(GcInstanceGpu)), D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
            D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, sizeof(GcInstanceGpu), nullptr, gc_instances_);
        D3D11_UNORDERED_ACCESS_VIEW_DESC u{}; u.Format = DXGI_FORMAT_UNKNOWN; u.ViewDimension = D3D11_UAV_DIMENSION_BUFFER; u.Buffer.NumElements = total;
        if (SUCCEEDED(hr)) hr = make_structured_srv(device_.Get(), gc_instances_.Get(), total, gc_instance_srv_);
        if (SUCCEEDED(hr)) hr = device_->CreateUnorderedAccessView(gc_instances_.Get(), &u, &gc_instance_uav_);
        // Sun shadow inputs per pool slot (gcShadeCS), 16 bytes.
        if (SUCCEEDED(hr)) hr = make_buffer(device_.Get(), total * 16, D3D11_BIND_UNORDERED_ACCESS, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 16,
            nullptr, gc_shade_input_);
        if (SUCCEEDED(hr)) hr = device_->CreateUnorderedAccessView(gc_shade_input_.Get(), &u, &gc_shade_input_uav_);
        // Per-frame draw inputs per pool slot (gcPrepCS), 32 bytes.
        gc_draw_inputs_.Reset(); gc_draw_input_srv_.Reset(); gc_draw_input_uav_.Reset();
        if (SUCCEEDED(hr)) hr = make_buffer(device_.Get(), total * 32, D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE,
            D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 32, nullptr, gc_draw_inputs_);
        if (SUCCEEDED(hr)) hr = make_structured_srv(device_.Get(), gc_draw_inputs_.Get(), total, gc_draw_input_srv_);
        if (SUCCEEDED(hr)) hr = device_->CreateUnorderedAccessView(gc_draw_inputs_.Get(), &u, &gc_draw_input_uav_);
        // Each mesh's pool base is constant until the pool grows again.
        for (unsigned mesh = 0; mesh < gm_count && SUCCEEDED(hr); ++mesh) {
            const std::uint32_t values[4]{gc_base_[mesh], 0, 0, 0};
            gc_draw_constants_[mesh].Reset();
            hr = make_buffer(device_.Get(), 16, D3D11_BIND_CONSTANT_BUFFER, 0, 0, values, gc_draw_constants_[mesh], D3D11_USAGE_IMMUTABLE);
        }
        if (FAILED(hr)) return fail("EID3D11 ground cover: instance pool allocation failed; ground cover omitted.");
        gc_instance_capacity_ = total; gc_grow_ = false;
    }


    // Units near the eye bend the grass: position, reach and strength by body size.
    const float grass_reach = reach + kCellSize;
    bridge_ground_cover_units(eye[0], eye[1], grass_reach, gc_unit_input_, nullptr, &gc_unit_trails_);
    // Game-owned lasting tracks. Read the entire validated list, regardless
    // of unit visibility; the GPU clips stamps to the camera's cover window.
    // Capacity is retained, not allocated anew each frame, and has no cap.
    if (!bridge_ground_cover_footprints(gc_footprint_input_, &gc_snow_prints_)) {
        gc_failed_ = true; // the bridge already logged the exact first failure
        return S_OK;
    }
    // Corpses the game still shows press the same mask while the body lasts.
    bridge_ground_cover_corpses(eye[0], eye[1], reach + kCellSize, gc_footprint_input_);
    // Reuse the authoritative shown, grounded, live-unit snapshot. Hidden,
    // undetected and invisible units were rejected by that bridge (the unit
    // drawing rule) and never stamp here, so cover cannot reveal them.
    for (const auto& unit : gc_unit_input_)
        gc_footprint_input_.push_back(live_unit_splat(unit[0], unit[1], unit[3]));
    if (gc_footprint_input_.size() > UINT_MAX / sizeof(FootprintSplat))
        return fail("EID3D11 ground cover: footprint buffer exceeds D3D11 addressable size; cover omitted.");
    const UINT footprint_count = static_cast<UINT>(gc_footprint_input_.size());

    // Recovery advances in authoritative game seconds: pause freezes it
    // and game speed scales it. Marker targets use the same precise clock.
    const double seconds = environment.world_time * kGroundCoverSecondsPerSegment;
    const float trail_dt = gc_trail_seconds_ >= 0 ? float(std::max(seconds - gc_trail_seconds_, 0.0)) : 0.f;
    gc_trail_seconds_ = seconds;
    const bool trail_was_clear = gc_trail_recovery_ <= 0;
    // No pressing source and a fully recovered state: no upload, pass or read.
    const bool trail_active = footprint_count || !trail_was_clear;
    bool trail_update = false;
    ComPtr<ID3D11ShaderResourceView> trail_previous; // null: the state starts from zero
    int shift_x = 0, shift_y = 0;
    if (trail_active) {
        const std::uint64_t width = std::uint64_t(x1 - x0) * kTrailTexelsPerCell;
        const std::uint64_t height = std::uint64_t(y1 - y0) * kTrailTexelsPerCell;
        if (width > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || height > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION)
            return fail("EID3D11 ground cover: footprint window exceeds D3D11 texture dimensions; cover omitted.");
        if (!trail_was_clear) {
            // World-anchored: the window origin moves in whole 4 m cells.
            trail_previous = gc_trail_srv_[gc_trail_index_];
            shift_x = (x0 - gc_trail_origin_x_) * int(kTrailTexelsPerCell);
            shift_y = (y0 - gc_trail_origin_y_) * int(kTrailTexelsPerCell);
        }
        const bool grow = width > gc_trail_width_ || height > gc_trail_height_;
        if (grow) {
            // trail_previous keeps the outgrown state alive for this frame's copy.
            gc_trail_width_ = std::max(gc_trail_width_, UINT(width));
            gc_trail_height_ = std::max(gc_trail_height_, UINT(height));
            for (unsigned k = 0; k < 2; ++k) {
                gc_trail_[k].Reset(); gc_trail_rtv_[k].Reset(); gc_trail_srv_[k].Reset();
                if (FAILED(make_texture(device_.Get(), gc_trail_width_, gc_trail_height_, DXGI_FORMAT_R16_UNORM, 1,
                        D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, gc_trail_[k])) ||
                    FAILED(device_->CreateRenderTargetView(gc_trail_[k].Get(), nullptr, &gc_trail_rtv_[k])) ||
                    FAILED(device_->CreateShaderResourceView(gc_trail_[k].Get(), nullptr, &gc_trail_srv_[k])))
                    return fail("EID3D11 ground cover: footprint mask allocation failed; cover omitted.");
            }
            gc_trail_sources_.Reset();gc_trail_sources_rtv_.Reset();gc_trail_sources_srv_.Reset();
            if(FAILED(make_texture(device_.Get(),gc_trail_width_,gc_trail_height_,DXGI_FORMAT_R16G16_FLOAT,1,
                    D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE,0,gc_trail_sources_))||
               FAILED(device_->CreateRenderTargetView(gc_trail_sources_.Get(),nullptr,&gc_trail_sources_rtv_))||
               FAILED(device_->CreateShaderResourceView(gc_trail_sources_.Get(),nullptr,&gc_trail_sources_srv_)))
                return fail("EID3D11 ground cover: current marker field allocation failed; cover omitted.");
            gc_trail_index_ = 1; // the update writes [0]
        }
        // With no game time elapsed the law changes nothing: a static window
        // keeps its state without a pass (pause with tracks in view).
        trail_update = trail_was_clear || grow || trail_dt > 0 || shift_x || shift_y;
        gc_trail_recovery_ = footprint_count ? kTrailReleaseSeconds : std::max(0.f, gc_trail_recovery_ - trail_dt);
    }
    if (trail_update && footprint_count) {
        if (footprint_count > gc_footprint_capacity_) {
            gc_footprints_.Reset(); gc_footprint_srv_.Reset();
            if (FAILED(make_buffer(device_.Get(), footprint_count * UINT(sizeof(FootprintSplat)), D3D11_BIND_SHADER_RESOURCE,
                    D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, sizeof(FootprintSplat), nullptr, gc_footprints_,
                    D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE)) ||
                FAILED(make_structured_srv(device_.Get(), gc_footprints_.Get(), footprint_count, gc_footprint_srv_)))
                return fail("EID3D11 ground cover: footprint buffer allocation failed; cover omitted.");
            gc_footprint_capacity_ = footprint_count;
        }
        D3D11_MAPPED_SUBRESOURCE track_mapped{};
        if (FAILED(context_->Map(gc_footprints_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &track_mapped)))
            return fail("EID3D11 ground cover: footprint upload failed; cover omitted.");
        std::memcpy(track_mapped.pData, gc_footprint_input_.data(), footprint_count * sizeof(FootprintSplat));
        context_->Unmap(gc_footprints_.Get(), 0);
    }

    // Snow trails (snow_trails.h): one capsule per walking unit from its
    // previous cover frame position, joined by lifecycle id; recovery on the
    // same game seconds as the cover trail. Maps without snow ground keep no
    // field (terrain then reads none).
    if (gc_snow_map_generation_ != ambient_map_generation_) {
        gc_snow_map_generation_ = ambient_map_generation_;
        gc_snow_map_ = std::any_of(ambient_area_.begin(), ambient_area_.end(), [](std::uint8_t v) { return (v & 15u) == ambient::area_snow; });
        gc_snow_valid_ = false; gc_snow_tracks_.clear(); gc_snow_seconds_ = -1; gc_snow_tiles_ = {};
    }
    const float snow_dt = gc_snow_seconds_ >= 0 ? float(std::max(seconds - gc_snow_seconds_, 0.0)) : 0.f;
    gc_snow_seconds_ = seconds;
    gc_snow_segment_input_.clear();
    // Trail ground: snow, or soft sand families once the map's families load.
    const bool trail_map = gc_snow_map_ || (gc_soft_sand_ && gc_sand_generation_ == ambient_map_generation_);
    if (trail_map && gc_unit_trails_.size() == gc_unit_input_.size()) {
        for (std::size_t k = 0; k < gc_unit_input_.size(); ++k) {
            const auto& unit = gc_unit_input_[k];
            const std::uint64_t id = gc_unit_trails_[k].id;
            float from[2]{unit[0], unit[1]}, along = 0;
            auto track = std::find_if(gc_snow_tracks_.begin(), gc_snow_tracks_.end(), [&](const SnowTrack& t) { return t.id == id; });
            if (track == gc_snow_tracks_.end()) { gc_snow_tracks_.push_back({id, unit[0], unit[1], native_frame_index_, 0}); }
            else {
                const float dx = unit[0] - track->x, dy = unit[1] - track->y, step = std::sqrt(dx * dx + dy * dy);
                if (step <= snow_trails::kLongestStep) { from[0] = track->x; from[1] = track->y; along = track->along; }
                // Path distance keys the trail's irregularity; wrapped to keep float precision.
                track->along = std::fmod(along + (step <= snow_trails::kLongestStep ? step : 0.f), 4096.f);
                if (track->along < along) along -= 4096.f;
                track->x = unit[0]; track->y = unit[1]; track->frame = native_frame_index_;
            }
            const auto shape = snow_trails::body_shape(gc_unit_trails_[k].half_extent, unit[3]);
            gc_snow_segment_input_.push_back({{from[0], from[1]}, {unit[0], unit[1]}, shape.half_width, shape.depth, shape.berm, along,
                snow_trails::unit_seed(id), {}});
        }
        // Units the game does not show still leave the prints it shows: join
        // each such maker's visible prints in creation order, so its trail
        // lasts exactly as long as its prints. Shown walkers stamp live above.
        std::sort(gc_snow_prints_.begin(), gc_snow_prints_.end(), [](const FootprintTrailPoint& a, const FootprintTrailPoint& b) {
            return a.id != b.id ? a.id < b.id : a.tick < b.tick; });
        for (std::size_t k = 0; k < gc_snow_prints_.size(); ++k) {
            const auto& print = gc_snow_prints_[k];
            if (std::any_of(gc_unit_trails_.begin(), gc_unit_trails_.end(), [&](const GroundUnitTrail& t) { return t.id == print.id; })) continue;
            float from[2]{print.x, print.y};
            if (k > 0 && gc_snow_prints_[k - 1].id == print.id &&
                std::hypot(print.x - gc_snow_prints_[k - 1].x, print.y - gc_snow_prints_[k - 1].y) <= snow_trails::kLongestPrintGap) {
                from[0] = gc_snow_prints_[k - 1].x; from[1] = gc_snow_prints_[k - 1].y;
            }
            // Stable path distance from world position along the step (no
            // per-maker history): continuous across gently turning prints.
            const float dx = print.x - from[0], dy = print.y - from[1], l = std::hypot(dx, dy);
            const float along = std::fmod(l > 1e-4f ? (from[0] * dx + from[1] * dy) / l : from[0], 4096.f);
            const auto shape = snow_trails::body_shape(print.half_extent, 0);
            gc_snow_segment_input_.push_back({{from[0], from[1]}, {print.x, print.y}, shape.half_width, shape.depth, shape.berm, along,
                snow_trails::unit_seed(print.id), {}});
        }
    }
    // A unit not seen this frame starts a new trail where it reappears.
    gc_snow_tracks_.erase(std::remove_if(gc_snow_tracks_.begin(), gc_snow_tracks_.end(),
        [&](const SnowTrack& t) { return t.frame != native_frame_index_; }), gc_snow_tracks_.end());
    // The window follows the ground at the middle of the view (at the mean
    // height of the units, else the terrain's mid height), in whole texels.
    float snow_centre[2]{eye[0], eye[1]};
    {
        float plane = .5f * (lowest + highest);
        if (!gc_unit_input_.empty()) { plane = 0; for (const auto& unit : gc_unit_input_) plane += unit[2]; plane /= float(gc_unit_input_.size()); }
        float far_point[4]{};
        for (unsigned r = 0; r < 4; ++r) far_point[r] = inverse[r * 4 + 2] + inverse[r * 4 + 3];
        if (std::abs(far_point[3]) > 1e-12f) {
            const float d[3]{far_point[0] / far_point[3] - eye[0], far_point[1] / far_point[3] - eye[1], far_point[2] / far_point[3] - eye[2]};
            if (d[2] < -1e-6f && plane < eye[2]) {
                const float t = std::min((plane - eye[2]) / d[2], 1.f);
                if (std::isfinite(t)) { snow_centre[0] = eye[0] + d[0] * t; snow_centre[1] = eye[1] + d[1] * t; }
            }
        }
    }
    const auto snow_origin = snow_trails::window_origin(snow_centre[0], snow_centre[1]);
    // Activity tiles: a moved window keeps each world tile's clock and
    // rewrites every tile twice (both ping-pong textures); steps mark the
    // tiles they reach. Nothing runs, and nothing reads the field, while no
    // tile is active (no trail anywhere near the view).
    if (gc_snow_valid_ && snow_origin != gc_snow_origin_) {
        gc_snow_tiles_.shift((snow_origin[0] - gc_snow_origin_[0]) / int(snow_trails::kTileTexels),
                             (snow_origin[1] - gc_snow_origin_[1]) / int(snow_trails::kTileTexels));
        gc_snow_full_writes_ = 2;
    }
    if (!gc_snow_valid_) { gc_snow_tiles_ = {}; gc_snow_full_writes_ = 2; }
    for (const auto& segment : gc_snow_segment_input_) gc_snow_tiles_.touch(segment, snow_origin, seconds);
    const auto snow_tiles = gc_snow_tiles_.mask(seconds, false);
    const bool snow_update = trail_map && (snow_tiles[0] | snow_tiles[1] | snow_tiles[2] | snow_tiles[3]) != 0;
    if (!snow_update) gc_snow_valid_ = false;
    if (snow_update && !gc_snow_[0]) {
        for (unsigned k = 0; k < 2; ++k)
            if (FAILED(make_texture(device_.Get(), snow_trails::kSide, snow_trails::kSide, DXGI_FORMAT_R16G16_FLOAT, 1,
                    D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, gc_snow_[k])) ||
                FAILED(device_->CreateRenderTargetView(gc_snow_[k].Get(), nullptr, &gc_snow_rtv_[k])) ||
                FAILED(device_->CreateShaderResourceView(gc_snow_[k].Get(), nullptr, &gc_snow_srv_[k])))
                return fail("EID3D11 ground cover: snow trail field allocation failed; cover omitted.");
    }
    const UINT snow_count = UINT(gc_snow_segment_input_.size());
    if (snow_update && snow_count > gc_snow_segment_capacity_) {
        gc_snow_segments_.Reset(); gc_snow_segment_srv_.Reset();
        const UINT capacity = snow_count + snow_count / 2 + 16;
        if (FAILED(make_buffer(device_.Get(), capacity * UINT(sizeof(snow_trails::Segment)), D3D11_BIND_SHADER_RESOURCE,
                D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, sizeof(snow_trails::Segment), nullptr, gc_snow_segments_,
                D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE)) ||
            FAILED(make_structured_srv(device_.Get(), gc_snow_segments_.Get(), capacity, gc_snow_segment_srv_)))
            return fail("EID3D11 ground cover: snow trail segment buffer allocation failed; cover omitted.");
        gc_snow_segment_capacity_ = capacity;
    }
    if (snow_update && snow_count) {
        D3D11_MAPPED_SUBRESOURCE segments{};
        if (FAILED(context_->Map(gc_snow_segments_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &segments)))
            return fail("EID3D11 ground cover: snow trail segment upload failed; cover omitted.");
        std::memcpy(segments.pData, gc_snow_segment_input_.data(), snow_count * sizeof(snow_trails::Segment));
        context_->Unmap(gc_snow_segments_.Get(), 0);
    }

    // Frame constants.
    GroundCoverConstants c{};
    // The field this frame's cover reads: the updated one, else none.
    c.snow_window[0] = float(snow_origin[0]) * snow_trails::kTexel; c.snow_window[1] = float(snow_origin[1]) * snow_trails::kTexel;
    c.snow_window[2] = 1.f / snow_trails::kTexel; c.snow_window[3] = snow_update ? float(snow_trails::kSide) : 0.f;
    c.snow_step[0] = float(snow_origin[0] - gc_snow_origin_[0]); c.snow_step[1] = float(snow_origin[1] - gc_snow_origin_[1]);
    c.snow_step[2] = snow_dt;
    const auto snow_write = gc_snow_full_writes_ ? std::array<std::uint32_t, 4>{~0u, ~0u, ~0u, ~0u} : gc_snow_tiles_.mask(seconds, true);
    std::memcpy(c.snow_tiles, snow_tiles.data(), sizeof(c.snow_tiles)); std::memcpy(c.snow_write, snow_write.data(), sizeof(c.snow_write));
    c.snow_materials[0]=std::uint32_t(contact_maps_.ready);
    c.snow_materials[1]=std::uint32_t(contact_maps_.ready>>32);
    std::memcpy(c.clip, world_to_clip.data(), sizeof(c.clip));
    const auto& m = world_to_clip;
    const auto plane = [&](unsigned index, float sign, unsigned row) {
        float v[4];
        for (unsigned k = 0; k < 4; ++k) v[k] = row == 2 && sign == 0 ? m[8 + k] : m[12 + k] + sign * m[row * 4 + k];
        const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        for (unsigned k = 0; k < 4; ++k) c.planes[index][k] = l > 1e-12f ? v[k] / l : v[k];
    };
    plane(0, 1, 0); plane(1, -1, 0); plane(2, 1, 1); plane(3, -1, 1); plane(4, 0, 2); plane(5, -1, 2);
    c.eye[0] = eye[0]; c.eye[1] = eye[1]; c.eye[2] = eye[2]; c.eye[3] = float(std::fmod(seconds, 16384.0));
    c.window[0] = float(x0); c.window[1] = float(y0); c.window[2] = float(x1 - x0); c.window[3] = kCellSize;
    c.grid[0] = float(ambient_tiles_x_); c.grid[1] = float(ambient_tiles_y_);c.grid[2]=float(gm_count);c.grid[3]=float(height_);
    c.quality[0] = quality.distance; c.quality[1] = quality.density; c.quality[2] = 0.f; c.quality[3] = float(ambient_zone_);
    // The shared renderer wind (weather_wind.h): direction, spells, storms, gusts.
    const WeatherWindGpu& shared_wind = weather_wind();
    const bool sheltered = ambient_zone_ == ambient::zone_cave || ambient_zone_ == ambient::zone_gipat_cave;
    c.wind[0] = shared_wind.direction[0]; c.wind[1] = shared_wind.direction[1];
    // shared_wind.force is the map's force raised to the wind floor.
    const SwayDrive sway = sway_drive(shared_wind.force, shared_wind.speed, sheltered);
    c.wind[2] = sway.amplitude;
    c.wind[3] = sway.travel;
    c.wind2[0] = shared_wind.gust; c.wind2[1] = shared_wind.pulse; c.wind2[2] = terrain_full_sun_ ? 1.f : 0.f;
    c.switches[0] = 0.f; // unused (was the removed terrain macro tint)
    c.switches[1] = frame.render[D3DRENDERSTATE_FOGENABLE] != 0 ? 1.f : 0.f;
    c.switches[2] = float(rule_table().size());
    c.switches[3] = float((gc_bark_srv_ ? 1u : 0u) | (gc_stone_srv_ ? 2u : 0u));
    // Zone pieces and the per-cell rule masks of the active map (from its
    // tiles: generation counters only, never addresses) bound the stage
    // buffers exactly.
    std::vector<std::array<float, 5>> zones;
    std::uint64_t zones_key = 0;
    for (const auto& piece : zone_pieces) {
        zones.push_back({piece.bounds[0], piece.bounds[1], piece.bounds[2], piece.bounds[3], float(piece.zone)});
        for (unsigned k = 0; k < 4; ++k) { std::uint32_t bits; std::memcpy(&bits, &piece.bounds[k], 4); zones_key = gc_hash(zones_key, bits); }
        zones_key = gc_hash(zones_key, piece.zone);
    }
    {
        const std::uint64_t tiles_key = gc_hash(gc_hash(ambient_map_generation_, ambient_liquid_level_updates_), gc_tree_epoch_);
        if (tiles_key != gc_tile_classes_key_) {
            const auto base = ambient::terrain_base_types(ambient::terrain_base_slots(*inputs.terrain), inputs.terrain->tile_types, {});
            const std::size_t count = std::size_t(ambient_tiles_x_) * ambient_tiles_y_;
            gc_tile_classes_.assign(count, 0xffffu);
            for (std::size_t t = 0; t < count; ++t) {
                const std::uint32_t area = t < ambient_area_.size() ? ambient_area_[t] : 255u, type = t < base.size() ? base[t] : 255u;
                const std::uint32_t trees = t < gc_tree_mask_.size() ? gc_tree_mask_[t] : 0u;
                gc_tile_classes_[t] = area | (type << 8) | (trees << 16);
            }
            gc_tile_classes_key_ = tiles_key;
        }
        const std::uint64_t key = gc_hash(tiles_key, zones_key);
        if (key != gc_cell_masks_key_) {
            const float origin[3]{};
            gc_cell_masks_ = gc_cell_rule_masks(gc_tile_classes_, ambient_tiles_x_, ambient_tiles_y_, zones, origin);
            gc_cell_mask_srv_.Reset();
            if (FAILED(make_grid_srv(device_.Get(), (ambient_tiles_x_ + 1) / 2, (ambient_tiles_y_ + 1) / 2, DXGI_FORMAT_R32G32_UINT,
                    gc_cell_masks_.data(), ((ambient_tiles_x_ + 1) / 2) * 8, gc_cell_mask_srv_)))
                return fail("EID3D11 ground cover: cell rule masks unavailable; ground cover omitted.");
            gc_cell_masks_key_ = key;
        }
    }
    const auto& fog = ambient_terrain_state_;
    c.fog_range[0] = fog.fog_min[0]; c.fog_range[1] = fog.fog_min[1]; c.fog_range[2] = fog.fog_max[0]; c.fog_range[3] = fog.fog_max[1];
    c.fog_control[0] = fog.fog_distance; c.fog_control[1] = fog.fog_enabled ? 1.f : 0.f;
    neighbour_fog_constants(neighbour_fog_, c.fog_seam, c.fog_curve, c.fog_span_lo, c.fog_span_hi);
    neighbour_fog_piece_constants(neighbour_fog_, c.fog_piece_info, c.fog_pieces);
    c.fog_color[0] = ((ambient_fog_color_ >> 16) & 255) / 255.f; c.fog_color[1] = ((ambient_fog_color_ >> 8) & 255) / 255.f;
    c.fog_color[2] = (ambient_fog_color_ & 255) / 255.f;
    // tree02 bark tile and end-grain rings; stone02 rock area (prototype crops, pixels / size).
    const float bark[4]{412 / 512.f, 370 / 512.f, 88 / 512.f, 134 / 512.f}, end[4]{435 / 512.f, 8 / 512.f, 65 / 512.f, 67 / 512.f},
                stone[4]{0, 64 / 256.f, 200 / 256.f, 192 / 256.f};
    std::memcpy(c.bark_rect, bark, sizeof(bark)); std::memcpy(c.end_rect, end, sizeof(end)); std::memcpy(c.stone_rect, stone, sizeof(stone));
    for (unsigned k = 0; k < gm_count; ++k) { c.capacity[k] = gc_capacity_[k]; c.base[k] = gc_base_[k]; }
    c.view[0] = reach;
    // The trail texture spans its whole capacity at fixed world texels from
    // the window origin, so a scroll is an exact whole-texel shift.
    constexpr float trail_texel = kCellSize / float(kTrailTexelsPerCell);
    c.trail_window[0] = float(x0) * kCellSize; c.trail_window[1] = float(y0) * kCellSize;
    c.trail_window[2] = gc_trail_width_ ? 1.f / (float(gc_trail_width_) * trail_texel) : 0;
    c.trail_window[3] = gc_trail_height_ ? 1.f / (float(gc_trail_height_) * trail_texel) : 0;
    c.view[1] = c.view[2] = trail_texel;
    c.view[3] = trail_active ? 1.f : 0.f;
    c.trail_step[0] = float(shift_x); c.trail_step[1] = float(shift_y);
    c.trail_step[2] = trail_dt / kTrailPressSeconds; c.trail_step[3] = trail_dt / kTrailReleaseSeconds;
    // Rebuild from current posed scene boxes: moved/removed objects leave no
    // stale footprint, and no legacy draw stream is consumed or cached.
    if(!bridge_ground_cover_objects(eye[0],eye[1],reach+kCellSize,gc_object_input_))
        return fail("EID3D11 ground cover: static scene bounds unavailable; cover omitted.");
    const std::uint64_t ow=std::uint64_t(x1-x0)*kTrailTexelsPerCell,oh=std::uint64_t(y1-y0)*kTrailTexelsPerCell;
    if(ow>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION||oh>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION||
       gc_object_input_.size()>UINT_MAX/sizeof(FootprintSplat))
        return fail("EID3D11 ground cover: object mask exceeds D3D11 dimensions; cover omitted.");
    if(!gc_object_mask_||ow>gc_object_width_||oh>gc_object_height_) {
        gc_object_width_=std::max(gc_object_width_,UINT(ow));gc_object_height_=std::max(gc_object_height_,UINT(oh));
        if(FAILED(make_texture(device_.Get(),gc_object_width_,gc_object_height_,DXGI_FORMAT_R8_UNORM,1,
            D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET,0,gc_object_mask_))||
           FAILED(device_->CreateRenderTargetView(gc_object_mask_.Get(),nullptr,&gc_object_rtv_))||
           FAILED(device_->CreateShaderResourceView(gc_object_mask_.Get(),nullptr,&gc_object_srv_)))
            return fail("EID3D11 ground cover: object mask allocation failed; cover omitted.");
        gc_object_mask_current_=false;
    }
    const UINT object_count=UINT(gc_object_input_.size());
    // The mask is window-relative: the same footprints at the same origin in
    // the same texture leave it unchanged (a still or panning-within-a-cell
    // camera over static objects). Anything else redraws it.
    const bool object_mask_update=!object_mask_current(gc_object_mask_current_,gc_object_origin_x_,gc_object_origin_y_,
        gc_object_drawn_,x0,y0,gc_object_input_);
    if(object_mask_update&&object_count>gc_object_capacity_) {
        if(FAILED(make_buffer(device_.Get(),object_count*UINT(sizeof(FootprintSplat)),D3D11_BIND_SHADER_RESOURCE,
            D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,sizeof(FootprintSplat),nullptr,gc_objects_,D3D11_USAGE_DYNAMIC,D3D11_CPU_ACCESS_WRITE))||
           FAILED(make_structured_srv(device_.Get(),gc_objects_.Get(),object_count,gc_object_input_srv_)))
            return fail("EID3D11 ground cover: object buffer unavailable; cover omitted.");
        gc_object_capacity_=object_count;
    }
    if(object_mask_update&&object_count) {
        D3D11_MAPPED_SUBRESOURCE objects{};
        if(FAILED(context_->Map(gc_objects_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&objects)))
            return fail("EID3D11 ground cover: object upload failed; cover omitted.");
        std::memcpy(objects.pData,gc_object_input_.data(),object_count*sizeof(FootprintSplat));context_->Unmap(gc_objects_.Get(),0);
    }
    c.object_window[0]=float(x0)*kCellSize;c.object_window[1]=float(y0)*kCellSize;
    c.object_window[2]=1.f/(gc_object_width_*trail_texel);c.object_window[3]=1.f/(gc_object_height_*trail_texel);
    // This frame's candidate bound per class (its visible-box cells, each
    // rule's slots at the cell's distance rounded down): rounds of a capacity
    // that holds the largest class in at most 60 rounds, the stage buffers
    // sized to it before the constants carry it. Nothing can overflow.
    const UINT max_cell = gc_cell_slot_bound(quality.density);
    std::array<UINT, kPlaceClassCount> class_rounds{};
    UINT round_capacity = 0;
    {
        const float origin[3]{};
        const auto bounds = gc_window_bounds(gc_cell_masks_, UINT((ambient_tiles_x_ + 1) / 2), eye, x0, y0, x1, y1, lowest, highest, c.planes, origin,
            quality.density, quality.distance, gc_class_rules_);
        UINT most = 0;
        for (const UINT bound : bounds) most = std::max(most, bound);
        // At most about eight rounds per class: each round is three dispatches.
        round_capacity = std::max(kGcRoundSlots, most / 8 + max_cell + 1);
        for (unsigned k = 0; k < kPlaceClassCount; ++k) class_rounds[k] = bounds[k] ? bounds[k] / (round_capacity - max_cell) + 1 : 0;
        if (FAILED(gc_ensure_stage_buffers(round_capacity)) || FAILED(gc_ensure_cell_jobs(UINT((x1 - x0) * (y1 - y0)))))
            return fail("EID3D11 ground cover: placement buffer allocation failed; ground cover omitted.");
    }
    c.stage[0] = gc_candidate_capacity_; c.stage[1] = gc_survivor_capacity_; c.stage[2] = gc_cell_job_capacity_; c.stage[3] = UINT(y1 - y0);
    c.round[0] = gc_candidate_capacity_ - max_cell;
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context_->Map(gc_frame_constants_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        gc_trail_recovery_ = 0; // the state was not advanced: never read it again
        return S_OK;
    }
    std::memcpy(mapped.pData, &c, sizeof(c)); context_->Unmap(gc_frame_constants_.Get(), 0);

    if(object_mask_update) {
    const float clear_mask[4]{};
    context_->ClearRenderTargetView(gc_object_rtv_.Get(),clear_mask);
    if(object_count) {
        context_->OMSetRenderTargets(1,gc_object_rtv_.GetAddressOf(),nullptr);
        D3D11_VIEWPORT mask_viewport{0,0,float(gc_object_width_),float(gc_object_height_),0,1};
        context_->RSSetViewports(1,&mask_viewport);context_->RSSetState(gc_raster_.Get());
        context_->OMSetDepthStencilState(gc_trail_depth_.Get(),0);
        const float factor[4]{1,1,1,1};context_->OMSetBlendState(gc_trail_blend_.Get(),factor,~0u);
        context_->IASetInputLayout(nullptr);context_->IASetIndexBuffer(nullptr,DXGI_FORMAT_UNKNOWN,0);
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);context_->GSSetShader(nullptr,nullptr,0);
        context_->VSSetShader(gc_object_vs_.Get(),nullptr,0);context_->PSSetShader(gc_object_ps_.Get(),nullptr,0);
        context_->VSSetConstantBuffers(11,1,gc_frame_constants_.GetAddressOf());context_->PSSetConstantBuffers(11,1,gc_frame_constants_.GetAddressOf());
        context_->VSSetShaderResources(45,1,gc_object_input_srv_.GetAddressOf());context_->DrawInstanced(6,object_count,0,0);
        ID3D11ShaderResourceView* empty=nullptr;context_->VSSetShaderResources(45,1,&empty);
        context_->OMSetRenderTargets(1,rtv_.GetAddressOf(),dsv_.Get());profiler_.add_draw(1,1);profiler_.add_batch(1);
    }
    gc_object_drawn_=gc_object_input_;gc_object_origin_x_=x0;gc_object_origin_y_=y0;gc_object_mask_current_=true;
    }
    // MAX-compose current sources and marker coverage, then recover the old
    // field outside markers. Within a marker, its current fading target
    // replaces history; a stronger current live body still wins the MAX.
    if (trail_update) {
        const UINT target = gc_trail_index_ ^ 1u;
        context_->OMSetRenderTargets(1,gc_trail_sources_rtv_.GetAddressOf(),nullptr);
        const float clear_sources[4]{};context_->ClearRenderTargetView(gc_trail_sources_rtv_.Get(),clear_sources);
        D3D11_VIEWPORT trail_viewport{0, 0, float(gc_trail_width_), float(gc_trail_height_), 0, 1};
        context_->RSSetViewports(1, &trail_viewport); context_->RSSetState(gc_raster_.Get());
        context_->OMSetDepthStencilState(gc_trail_depth_.Get(), 0);
        const float factor[4]{1, 1, 1, 1}; context_->OMSetBlendState(gc_blend_.Get(), factor, ~0u);
        context_->IASetInputLayout(nullptr); context_->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->GSSetShader(nullptr, nullptr, 0);
        context_->VSSetConstantBuffers(11, 1, gc_frame_constants_.GetAddressOf());
        context_->PSSetConstantBuffers(11, 1, gc_frame_constants_.GetAddressOf());
        context_->PSSetShaderResources(46, 1, trail_previous.GetAddressOf());
        UINT trail_draws = 1;
        if (footprint_count) {
            context_->OMSetBlendState(gc_trail_blend_.Get(), factor, ~0u);
            context_->VSSetShader(gc_trail_vs_.Get(), nullptr, 0); context_->PSSetShader(gc_trail_ps_.Get(), nullptr, 0);
            context_->VSSetShaderResources(45, 1, gc_footprint_srv_.GetAddressOf());
            context_->DrawInstanced(6, footprint_count, 0, 0);
            ID3D11ShaderResourceView* no_footprints = nullptr;
            context_->VSSetShaderResources(45, 1, &no_footprints);
            ++trail_draws;
        }
        context_->OMSetRenderTargets(1,gc_trail_rtv_[target].GetAddressOf(),nullptr);
        context_->OMSetBlendState(gc_blend_.Get(),factor,~0u);
        context_->PSSetShaderResources(49,1,gc_trail_sources_srv_.GetAddressOf());
        context_->VSSetShader(gc_trail_full_vs_.Get(),nullptr,0);context_->PSSetShader(gc_trail_decay_ps_.Get(),nullptr,0);
        context_->Draw(3,0);
        ID3D11ShaderResourceView* no_sources=nullptr;context_->PSSetShaderResources(49,1,&no_sources);
        ID3D11ShaderResourceView* no_previous = nullptr;
        context_->PSSetShaderResources(46, 1, &no_previous);
        context_->OMSetRenderTargets(1, rtv_.GetAddressOf(), dsv_.Get());
        gc_trail_index_ = target; gc_trail_origin_x_ = x0; gc_trail_origin_y_ = y0;
        profiler_.add_draw(trail_draws, trail_draws); profiler_.add_batch(trail_draws);
    }
    // Snow trails: the previous field moved and filled in, then this frame's
    // steps MAX-composed over it, into the other ping-pong texture.
    if (snow_update) {
        const UINT target = gc_snow_index_ ^ 1u;
        ID3D11ShaderResourceView* previous = gc_snow_valid_ ? gc_snow_srv_[gc_snow_index_].Get() : nullptr;
        context_->OMSetRenderTargets(1, gc_snow_rtv_[target].GetAddressOf(), nullptr);
        D3D11_VIEWPORT snow_viewport{0, 0, float(snow_trails::kSide), float(snow_trails::kSide), 0, 1};
        context_->RSSetViewports(1, &snow_viewport); context_->RSSetState(gc_raster_.Get());
        context_->OMSetDepthStencilState(gc_trail_depth_.Get(), 0);
        const float factor[4]{1, 1, 1, 1}; context_->OMSetBlendState(gc_blend_.Get(), factor, ~0u);
        context_->IASetInputLayout(nullptr); context_->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); context_->GSSetShader(nullptr, nullptr, 0);
        context_->VSSetConstantBuffers(11, 1, gc_frame_constants_.GetAddressOf());
        context_->PSSetConstantBuffers(11, 1, gc_frame_constants_.GetAddressOf());
        context_->PSSetShaderResources(53, 1, &previous);
        context_->VSSetShader(gc_snow_tile_vs_.Get(), nullptr, 0); context_->PSSetShader(gc_snow_recover_ps_.Get(), nullptr, 0);
        context_->DrawInstanced(6, snow_trails::kTiles * snow_trails::kTiles, 0, 0);
        ID3D11ShaderResourceView* none = nullptr; context_->PSSetShaderResources(53, 1, &none);
        UINT snow_draws = 1;
        if (snow_count) {
            context_->OMSetBlendState(gc_trail_blend_.Get(), factor, ~0u);
            context_->VSSetShader(gc_snow_stamp_vs_.Get(), nullptr, 0); context_->PSSetShader(gc_snow_stamp_ps_.Get(), nullptr, 0);
            context_->VSSetShaderResources(52, 1, gc_snow_segment_srv_.GetAddressOf());
            context_->PSSetShaderResources(43, 1, ambient_type_srv_.GetAddressOf());
            context_->DrawInstanced(6, snow_count, 0, 0);
            context_->VSSetShaderResources(52, 1, &none); context_->PSSetShaderResources(43, 1, &none);
            ++snow_draws;
        }
        context_->OMSetRenderTargets(1, rtv_.GetAddressOf(), dsv_.Get());
        gc_snow_index_ = target; gc_snow_origin_ = snow_origin; gc_snow_active_ = snow_tiles;
        gc_snow_valid_ = true;
        if (gc_snow_full_writes_) --gc_snow_full_writes_;
        profiler_.add_draw(snow_draws, snow_draws); profiler_.add_batch(snow_draws);
    }
    ID3D11ShaderResourceView* const snow_srv = snow_update ? gc_snow_srv_[gc_snow_index_].Get() : nullptr;
    ID3D11ShaderResourceView* const trail_srv = trail_active ? gc_trail_srv_[gc_trail_index_].Get() : nullptr;

    // Overflow state, one slot per placement pass (sized by the passes this
    // frame can run); a new buffer starts at zero (full keep, no demand).
    if (gc_overflow_passes_ < 1 + gc_neighbours_.size()) {
        const UINT passes = UINT(1 + gc_neighbours_.size()), bytes = passes * gm_count * 8;
        gc_overflow_uav_.Reset(); gc_overflow_.Reset(); gc_overflow_passes_ = 0;
        if (FAILED(make_buffer(device_.Get(), bytes, D3D11_BIND_UNORDERED_ACCESS, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0,
                nullptr, gc_overflow_)) || FAILED(make_raw_uav(device_.Get(), gc_overflow_.Get(), bytes, gc_overflow_uav_)))
            return fail("EID3D11 ground cover: overflow state allocation failed; ground cover omitted.");
        const UINT zero[4]{};
        context_->ClearUnorderedAccessViewUint(gc_overflow_uav_.Get(), zero);
        gc_overflow_passes_ = passes;
    }

    // Placement.
    context_->UpdateSubresource(gc_args_.Get(), 0, nullptr, gc_args_template_.data(), 0, 0);
    const UINT zero_counts[4]{};
    context_->ClearUnorderedAccessViewUint(gc_counts_uav_.Get(), zero_counts);
    ID3D11ShaderResourceView* place_srvs[18]{nullptr, nullptr, ambient_area_srv_.Get(), ambient_height_srv_.Get(), ambient_ground_srv_.Get(),
                                            gc_fields_srv_.Get(), gc_color_srv_.Get(), gc_rule_srv_.Get(), nullptr,
                                            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, ambient_type_srv_.Get(), trail_srv};
    context_->CSSetShaderResources(27, 18, place_srvs);
    context_->CSSetShaderResources(47, 1, gc_trees_srv_.GetAddressOf());
    context_->CSSetShaderResources(48, 1, gc_object_srv_.GetAddressOf());
    context_->CSSetShaderResources(50, 1, gc_zone_srv_.GetAddressOf());
    context_->CSSetShaderResources(51, 1, &snow_srv);
    // This frame's sun cascades for the per-instance blocker search (gcSunGap).
    // The compute slots it borrows are restored after placement.
    ComPtr<ID3D11Buffer> saved_cs_shadow, saved_cs_history; ComPtr<ID3D11SamplerState> saved_cs_gather;
    context_->CSGetConstantBuffers(2, 1, saved_cs_shadow.GetAddressOf());
    context_->CSGetConstantBuffers(6, 1, saved_cs_history.GetAddressOf());
    context_->CSGetSamplers(10, 1, saved_cs_gather.GetAddressOf());
    // The placement stages also borrow CS u5-u7, t54-t60, b12 and b13, which
    // fce219b's single pass left untouched (the native flush binds u6 itself).
    std::array<ComPtr<ID3D11UnorderedAccessView>, 3> saved_cs_uavs; std::array<ComPtr<ID3D11ShaderResourceView>, 8> saved_cs_stage;
    ComPtr<ID3D11Buffer> saved_cs_place, saved_cs_run;
    {
        ID3D11UnorderedAccessView* views[3]{}; context_->CSGetUnorderedAccessViews(5, 3, views);
        for (unsigned k = 0; k < 3; ++k) saved_cs_uavs[k].Attach(views[k]);
        ID3D11ShaderResourceView* stage[8]{}; context_->CSGetShaderResources(54, 8, stage);
        for (unsigned k = 0; k < 8; ++k) saved_cs_stage[k].Attach(stage[k]);
        context_->CSGetConstantBuffers(12, 1, saved_cs_place.GetAddressOf());
        context_->CSGetConstantBuffers(13, 1, saved_cs_run.GetAddressOf());
    }
    const auto restore_cs_shadow = [&] {
        context_->CSSetConstantBuffers(2, 1, saved_cs_shadow.GetAddressOf());
        context_->CSSetConstantBuffers(6, 1, saved_cs_history.GetAddressOf());
        context_->CSSetSamplers(10, 1, saved_cs_gather.GetAddressOf());
        ID3D11UnorderedAccessView* views[3]{saved_cs_uavs[0].Get(), saved_cs_uavs[1].Get(), saved_cs_uavs[2].Get()};
        context_->CSSetUnorderedAccessViews(5, 3, views, nullptr);
        ID3D11ShaderResourceView* stage[8]{};
        for (unsigned k = 0; k < 8; ++k) stage[k] = saved_cs_stage[k].Get();
        context_->CSSetShaderResources(54, 8, stage);
        context_->CSSetConstantBuffers(12, 1, saved_cs_place.GetAddressOf());
        context_->CSSetConstantBuffers(13, 1, saved_cs_run.GetAddressOf());
    };
    context_->CSSetConstantBuffers(2, 1, native_shadow_constants_.GetAddressOf());
    context_->CSSetShaderResources(9, 1, native_shadow_depth_srv_.GetAddressOf());
    // Per-tuft soft visibility: the plane-compare gather sampler and the
    // history's per-frame disk jitter (gcSunVisibility).
    context_->CSSetSamplers(10, 1, native_shadow_gather_sampler_.GetAddressOf());
    context_->CSSetConstantBuffers(6, 1, shadow_history_constants_.GetAddressOf());
    context_->CSSetConstantBuffers(11, 1, gc_frame_constants_.GetAddressOf());
    context_->CSSetSamplers(8, 1, gc_clamp_.GetAddressOf());
    ID3D11UnorderedAccessView* uavs[5]{gc_instance_uav_.Get(), gc_args_uav_.Get(), gc_counts_uav_.Get(), nullptr, gc_overflow_uav_.Get()};
    ID3D11UnorderedAccessView* no_uavs[8]{};
    // Settings permutations of this frame: the sun shadow state as the shadow
    // constants carry it (enabled maps, far maps, softness), trails, fog and
    // cloud shadow; every program variant compiles the off state out.
    bool sun_maps = false;
    for (UINT index = 0; index < 5; ++index)
        sun_maps |= native_shadows_enabled_ && native_shadow_frame_.slices[index].enabled && native_shadow_valid_[index];
    const bool sun_far = native_shadow_frame_.slices[4].bias[2] > .5f, sun_on = sun_maps || sun_far;
    const bool sun_soft = native_shadow_softness_[2] > .5f;
    const unsigned sun_state = gc_sun_state(sun_on, sun_soft, sun_far);
    // Bounded CPU-side diagnostics only; no GPU readback or render-state changes.
    {
        static std::uint64_t reportedEpoch = ~std::uint64_t(0);
        static unsigned previous = ~0u, reports = 0;
        const auto epoch = bridge_current_world_epoch();
        if (reportedEpoch != epoch) { reportedEpoch = epoch; previous = ~0u; reports = 0; }
        unsigned enabled = 0, valid = 0;
        for (unsigned i = 0; i < 5; ++i) {
            if (native_shadow_frame_.slices[i].enabled) enabled |= 1u << i;
            if (native_shadow_valid_[i]) valid |= 1u << i;
        }
        const unsigned state = enabled | (valid << 5) | (unsigned(sun_state) << 10) |
            (unsigned(native_shadows_enabled_) << 14) | (unsigned(bool(gc_bark_srv_)) << 15);
        if (reports < 32 && state != previous) {
            previous = state; ++reports;
            char detail[384]{};
            std::snprintf(detail, sizeof(detail),
                "epoch=%llu frame_epoch=%llu enabled=%02x valid=%02x effective=%02x far=%u sun_state=%u shadows=%u depth_srv=%u bark=%u sample=%u/32",
                static_cast<unsigned long long>(epoch),
                static_cast<unsigned long long>(native_shadow_frame_.world_epoch), enabled, valid,
                native_shadows_enabled_ ? enabled & valid : 0u, sun_far ? 1u : 0u, sun_state,
                native_shadows_enabled_ ? 1u : 0u, native_shadow_depth_srv_ ? 1u : 0u,
                gc_bark_srv_ ? 1u : 0u, reports);
            bridge_report_event("bush_shadow_state", detail);
        }
    }
    const bool snow_trails = c.snow_window[3] > 0 && snow_srv != nullptr;
    const bool active_trails = c.view[3] != 0 || snow_trails;
    // Stage counters of every class and round: (0 groups, 1, 1, 0 count).
    static const std::vector<UINT> stage_reset = [] {
        std::vector<UINT> values(kPlaceClassCount * kGcMaxRounds * 4, 0);
        for (std::size_t k = 0; k < values.size(); k += 4) values[k + 1] = values[k + 2] = 1;
        return values;
    }();
    ID3D11ShaderResourceView* no_stage[8]{};
    // Habitat and build of one class (or all rules) after its stage 1, each
    // stage's count feeding the next stage's indirect dispatch.
    const auto stages_23 = [&](UINT offset, ID3D11ComputeShader* habitat, ID3D11ComputeShader* build) {
        ID3D11UnorderedAccessView* habitat_out[5]{gc_stage_count_uav_[1].Get(), uavs[4], nullptr, gc_candidate_uav_.Get(), gc_survivor_uav_.Get()};
        context_->CSSetUnorderedAccessViews(3, 5, habitat_out, nullptr);
        context_->CSSetShaderResources(54, 1, gc_stage_count_srv_[0].GetAddressOf());
        context_->CSSetShader(habitat, nullptr, 0);
        context_->DispatchIndirect(gc_stage_counts_[0].Get(), offset);
        ID3D11UnorderedAccessView* build_out[5]{nullptr, uavs[4], gc_shade_input_uav_.Get(), gc_survivor_uav_.Get(), nullptr};
        context_->CSSetUnorderedAccessViews(3, 5, build_out, nullptr);
        context_->CSSetShaderResources(54, 1, gc_stage_count_srv_[1].GetAddressOf());
        context_->CSSetShader(build, nullptr, 0);
        context_->DispatchIndirect(gc_stage_counts_[1].Get(), offset);
        context_->CSSetShaderResources(54, 1, no_stage);
    };
    // The pool finalize and the per-instance sun shadow of a pass.
    const auto finish_pass = [&]() {
        ID3D11UnorderedAccessView* work_out[5]{gc_work_uav_.Get(), uavs[4], gc_shade_input_uav_.Get(), nullptr, nullptr};
        context_->CSSetUnorderedAccessViews(3, 5, work_out, nullptr);
        context_->CSSetShader(gc_finalize_cs_.Get(), nullptr, 0);
        context_->Dispatch(1, 1, 1);
        ID3D11UnorderedAccessView* no_work = nullptr;
        context_->CSSetUnorderedAccessViews(3, 1, &no_work, nullptr);
        UINT dispatches = 1;
        if (sun_on && sun_soft) { // the pixel programs read the per-tuft shadow only with soft shadows
            context_->CSSetShaderResources(55, 1, gc_work_srv_.GetAddressOf());
            context_->CSSetShader(gc_shade_cs_[sun_far ? 1 : 0].Get(), nullptr, 0);
            context_->DispatchIndirect(gc_work_.Get(), 0);
            context_->CSSetShaderResources(55, 1, no_stage);
            ++dispatches;
        }
        context_->CSSetUnorderedAccessViews(0, 8, no_uavs, nullptr); // u5-u7, t54-t60, b12, b13 return in restore_cs_shadow
        return dispatches;
    };
    // The active map: each window cell tested once (gcCellsCS) into per-class
    // job lists, split into rounds (gcScanCS); per class and round its three
    // stages. Returns the dispatches issued.
    const auto active_pass = [&]() {
        UINT dispatches = 0;
        context_->CSSetUnorderedAccessViews(0, 5, uavs, nullptr);
        const UINT zero[4]{};
        context_->ClearUnorderedAccessViewUint(gc_cell_count_uav_.Get(), zero);
        ID3D11ShaderResourceView* rule_class = gc_rule_class_srv_.Get();
        context_->CSSetShaderResources(59, 1, &rule_class);
        context_->CSSetShaderResources(61, 1, gc_cell_mask_srv_.GetAddressOf());
        ID3D11UnorderedAccessView* cells_out[4]{gc_cell_count_uav_.Get(), uavs[4], nullptr, gc_cell_job_uav_.Get()};
        context_->CSSetUnorderedAccessViews(3, 4, cells_out, nullptr);
        context_->CSSetShader(gc_cells_cs_.Get(), nullptr, 0);
        context_->Dispatch((UINT((x1 - x0) * (y1 - y0)) + 63) / 64, 1, 1);
        ID3D11UnorderedAccessView* scan_out[5]{nullptr, uavs[4], nullptr, nullptr, gc_round_uav_.Get()};
        context_->CSSetUnorderedAccessViews(3, 5, scan_out, nullptr);
        ID3D11ShaderResourceView* scan_in[3]{gc_cell_job_srv_.Get(), nullptr, gc_cell_count_srv_.Get()};
        context_->CSSetShaderResources(56, 3, scan_in);
        context_->CSSetShader(gc_scan_cs_.Get(), nullptr, 0);
        context_->Dispatch(kPlaceClassCount, 1, 1);
        ID3D11UnorderedAccessView* no_round = nullptr;
        context_->CSSetUnorderedAccessViews(7, 1, &no_round, nullptr);
        ID3D11ShaderResourceView* place_in[2]{gc_cell_job_srv_.Get(), gc_round_srv_.Get()};
        context_->CSSetShaderResources(56, 2, place_in);
        dispatches += 2;
        for (const auto& counts : gc_stage_counts_) context_->UpdateSubresource(counts.Get(), 0, nullptr, stage_reset.data(), 0, 0);
        for (unsigned c = 0; c < kPlaceClassCount; ++c) {
            if (!class_rounds[c]) continue;
            context_->CSSetConstantBuffers(12, 1, gc_class_constants_[c].GetAddressOf());
            for (UINT round = 0; round < class_rounds[c]; ++round) {
                context_->CSSetConstantBuffers(13, 1, gc_round_constants_[round].GetAddressOf());
                // Cell jobs -> candidates (counts in gc_stage_counts_[0]).
                ID3D11UnorderedAccessView* place_out[5]{gc_stage_count_uav_[0].Get(), uavs[4], nullptr, nullptr, gc_candidate_uav_.Get()};
                context_->CSSetUnorderedAccessViews(3, 5, place_out, nullptr);
                context_->CSSetShader(gc_stage_cs_[0][c].Get(), nullptr, 0);
                context_->DispatchIndirect(gc_rounds_.Get(), (c * kGcMaxRounds + round) * 16);
                stages_23((c * kGcMaxRounds + round) * 16, gc_stage_cs_[1][c].Get(), gc_stage_cs_[active_trails ? 2 : 3][c].Get());
                dispatches += 3;
            }
        }
        context_->CSSetShaderResources(56, 6, no_stage);
        return dispatches + finish_pass();
    };
    // A neighbour map: its cells and stage 1 over every rule in one dispatch,
    // then the all-rules habitat and build (no trails on neighbour maps).
    const auto neighbour_pass = [&](UINT groups_x, UINT groups_y, ID3D11ShaderResourceView* masks) {
        context_->CSSetShaderResources(61, 1, &masks);
        context_->CSSetUnorderedAccessViews(0, 5, uavs, nullptr);
        context_->CSSetConstantBuffers(12, 1, gc_class_constants_[0].GetAddressOf()); // stage counters of class 0, round 0
        context_->CSSetConstantBuffers(13, 1, gc_round_constants_[0].GetAddressOf());
        for (const auto& counts : gc_stage_counts_) {
            const D3D11_BOX box{0, 0, 0, 16, 1, 1};
            context_->UpdateSubresource(counts.Get(), 0, &box, stage_reset.data(), 0, 0);
        }
        ID3D11UnorderedAccessView* place_out[5]{gc_stage_count_uav_[0].Get(), uavs[4], nullptr, nullptr, gc_candidate_uav_.Get()};
        context_->CSSetUnorderedAccessViews(3, 5, place_out, nullptr);
        context_->CSSetShader(gc_neighbour_cs_.Get(), nullptr, 0);
        context_->Dispatch(groups_x, groups_y, 1);
        stages_23(0, gc_habitat_all_cs_.Get(), gc_build_all_cs_.Get());
        ID3D11ShaderResourceView* no_masks = nullptr;
        context_->CSSetShaderResources(61, 1, &no_masks);
        return 3 + finish_pass();
    };
    const UINT place_dispatches = active_pass();
    ID3D11ShaderResourceView* no_srvs[18]{};
    context_->CSSetShaderResources(27, 18, no_srvs);
    context_->CSSetShaderResources(47, 1, no_srvs);
    context_->CSSetShaderResources(48, 1, no_srvs);
    context_->CSSetShaderResources(51, 1, no_srvs);
    context_->CSSetShaderResources(9, 1, no_srvs);
    restore_cs_shadow();
    context_->CSSetShader(nullptr, nullptr, 0);
    perf_native_compute_dispatches_ += place_dispatches; native_frame_stats_.native_compute_dispatches += place_dispatches;

    // Draw into the world colour (AO ambient share alongside, like world pixels).
    // Its own scope: placement (with the mask and trail passes) and the lit
    // draws scale with different settings (density vs. pixels and shading).
    transition_profile_pass("ground_cover_draw");
    const bool write_ambient = ao_ambient_rtv_ && rtv_.Get() == scene_composite_.target();
    ao_ambient_written_ |= write_ambient;
    // Cover reads and writes the temporal sun-shadow history like world pixels.
    const bool history = prepare_shadow_history();
    ID3D11RenderTargetView* targets[3]{rtv_.Get(), history ? shadow_history_rtv_[shadow_history_current_index_].Get() : nullptr,
                                       write_ambient ? ao_ambient_rtv_.Get() : nullptr};
    context_->OMSetRenderTargets(write_ambient ? 3u : history ? 2u : 1u, targets, dsv_.Get());
    ID3D11ShaderResourceView* history_srvs[2]{history ? shadow_history_srv_[shadow_history_previous_index_].Get() : nullptr,
                                              history ? shadow_history_depth_srv_.Get() : nullptr};
    context_->PSSetShaderResources(20, 2, history_srvs);
    D3D11_VIEWPORT viewport{0, 0, float(target_width()), float(target_height()), 0, 1}; context_->RSSetViewports(1, &viewport);
    context_->RSSetState(gc_raster_.Get()); context_->OMSetDepthStencilState(gc_depth_.Get(), 0);
    const float factor[4]{1, 1, 1, 1}; context_->OMSetBlendState(gc_blend_.Get(), factor, ~0u);
    context_->IASetInputLayout(nullptr); context_->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
    context_->IASetIndexBuffer(gc_indices_.Get(), DXGI_FORMAT_R32_UINT, 0);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->GSSetShader(nullptr, nullptr, 0);
    // Permutations per mesh kind; point shadows only when a light has a
    // shadow slot this frame (otherwise the test never passes).
    bool point_shadows = false;
    for (const auto& light : native_point_lights_) point_shadows |= light.shadow_slots[0] > 0;
    // masked: the active map's cover went through the depth prepass and the
    // world's mask pass (its material pass tests EQUAL and reads the mask).
    bool masked = false;
    const auto bind_mesh_shaders = [&](unsigned mesh, bool neighbour) {
        const unsigned kind = gc_mesh_kind_[mesh];
        const unsigned trails = !neighbour && snow_trails ? 1u : 0u; // no snow trails on neighbour maps
        context_->VSSetShader(gc_vs_[kind == 3 ? 2 + trails : point_shadows ? 1 : 0].Get(), nullptr, 0);
        if (masked && !neighbour)
            context_->PSSetShader(kind == 3 ? gc_ps_[44 + (point_shadows ? 2u : 0u) + trails].Get()
                                            : gc_ps_[38 + kind * 2 + (write_ambient ? 1u : 0u)].Get(), nullptr, 0);
        else
            context_->PSSetShader(kind == 3 ? gc_ps_[30 + ((sun_on ? 2u : 0u) + (point_shadows ? 1u : 0u)) * 2 + trails].Get()
                                            : gc_ps_[(kind * 5 + sun_state) * 2 + (write_ambient ? 1u : 0u)].Get(), nullptr, 0);
    };
    bind_owned_sky_pixel_inputs();  // borderFogColor reads the sky probe
    context_->VSSetConstantBuffers(11, 1, gc_frame_constants_.GetAddressOf());
    context_->PSSetConstantBuffers(11, 1, gc_frame_constants_.GetAddressOf());
    ID3D11SamplerState* samplers[2]{gc_clamp_.Get(), gc_wrap_.Get()};
    context_->VSSetSamplers(8, 2, samplers); context_->PSSetSamplers(8, 2, samplers);
    // Pixel inputs stay below t25: the entire point-shadow page range is live.
    ID3D11ShaderResourceView* pixel_inputs[5]{ambient_ground_srv_.Get(), gc_color_srv_.Get(),
        gc_bark_srv_.Get(), gc_stone_srv_.Get(), gc_object_srv_.Get()};
    context_->PSSetShaderResources(0, 5, pixel_inputs);
    context_->PSSetShaderResources(8,1,&snow_srv);
    ID3D11ShaderResourceView* snow_art[2]{contact_maps_.tiles.Get(),contact_maps_.atlas.Get()};
    context_->PSSetShaderResources(15,2,snow_art);
    // Shared world light, sun cascades, point lights and atmosphere.
    context_->PSSetConstantBuffers(1, 1, native_world_lighting_constants_.GetAddressOf());
    context_->PSSetConstantBuffers(2, 1, native_shadow_constants_.GetAddressOf());
    context_->PSSetConstantBuffers(5, 1, native_point_light_constants_.GetAddressOf());
    context_->PSSetConstantBuffers(6, 1, shadow_history_constants_.GetAddressOf());
    context_->PSSetShaderResources(9, 1, native_shadow_depth_srv_.GetAddressOf());
    ID3D11ShaderResourceView* point_lights = native_point_lights_.empty() ? nullptr : native_point_light_srv_.Get();
    context_->PSSetShaderResources(18, 1, &point_lights);
    ID3D11ShaderResourceView* point_light_grid = native_point_lights_.empty() ? nullptr : native_point_light_grid_srv_.Get();
    context_->PSSetShaderResources(17, 1, &point_light_grid);
    std::array<ID3D11ShaderResourceView*,PointShadowTextureLayout::array_bindings> point_views{};
    for(std::size_t i=0;i<native_point_shadow_pages_.size();++i)point_views[i]=native_point_shadow_pages_[i].srv.Get();
    context_->PSSetShaderResources(PointShadowTextureLayout::first_srv,static_cast<UINT>(point_views.size()),point_views.data());
    context_->PSSetSamplers(2, 1, native_point_shadow_sampler_.GetAddressOf());
    context_->PSSetSamplers(1, 1, native_shadow_sampler_.GetAddressOf());
    context_->PSSetSamplers(4, 1, native_shadow_far_sampler_.GetAddressOf());
    context_->PSSetSamplers(10, 1, native_shadow_gather_sampler_.GetAddressOf());
    (void)bind_world_fog();
    // Per instance and frame (gcPrepCS): fog, atmosphere and cloud shadow at
    // the tuft's mid height (the world's fog constants, froxels, sky probe and
    // cloud shadow) and the wind sway. Runs after each placement pass, before
    // its draws; the compute slots it borrows are restored after it.
    ComPtr<ID3D11Buffer> fog_constants; context_->PSGetConstantBuffers(10, 1, fog_constants.GetAddressOf());
    std::array<ComPtr<ID3D11ShaderResourceView>, 3> fog_views;
    { ID3D11ShaderResourceView* views[3]{}; context_->PSGetShaderResources(10, 3, views); for (unsigned k = 0; k < 3; ++k) fog_views[k].Attach(views[k]); }
    ComPtr<ID3D11SamplerState> fog_sampler; context_->PSGetSamplers(7, 1, fog_sampler.GetAddressOf());
    const auto prep_pass = [&](bool neighbour) {
        ID3D11ShaderResourceView* no_vs_inputs[2]{};
        context_->VSSetShaderResources(3, 2, no_vs_inputs); // the pool and its draw inputs are written
        ComPtr<ID3D11Buffer> saved_constants; context_->CSGetConstantBuffers(10, 1, saved_constants.GetAddressOf());
        ID3D11ShaderResourceView* saved_views[3]{}; context_->CSGetShaderResources(10, 3, saved_views);
        ComPtr<ID3D11SamplerState> saved_sampler; context_->CSGetSamplers(7, 1, saved_sampler.GetAddressOf());
        ID3D11UnorderedAccessView* saved_uavs[7]{}; context_->CSGetUnorderedAccessViews(0, 7, saved_uavs);
        context_->CSSetConstantBuffers(10, 1, fog_constants.GetAddressOf());
        context_->CSSetConstantBuffers(11, 1, gc_frame_constants_.GetAddressOf());
        ID3D11ShaderResourceView* views[3]{fog_views[0].Get(), fog_views[1].Get(), fog_views[2].Get()};
        context_->CSSetShaderResources(10, 3, views);
        context_->CSSetSamplers(7, 1, fog_sampler.GetAddressOf());
        ID3D11UnorderedAccessView* uavs_prep[7]{gc_instance_uav_.Get(), gc_args_uav_.Get(), nullptr, nullptr, nullptr, nullptr, gc_draw_input_uav_.Get()};
        context_->CSSetUnorderedAccessViews(0, 7, uavs_prep, nullptr);
        ComPtr<ID3D11ShaderResourceView> saved_work; context_->CSGetShaderResources(55, 1, saved_work.GetAddressOf());
        context_->CSSetShaderResources(55, 1, gc_work_srv_.GetAddressOf());
        const unsigned fog_on = c.switches[1] > 0 ? 1u : 0u, cloud_on = fog_cloud_visible_ ? 1u : 0u;
        context_->CSSetShader(gc_prep_cs_[(neighbour ? 4u : 0u) + fog_on * 2 + cloud_on].Get(), nullptr, 0);
        context_->DispatchIndirect(gc_work_.Get(), 0);
        context_->CSSetShader(nullptr, nullptr, 0);
        context_->CSSetShaderResources(55, 1, saved_work.GetAddressOf());
        context_->CSSetUnorderedAccessViews(0, 7, saved_uavs, nullptr);
        context_->CSSetShaderResources(10, 3, saved_views);
        context_->CSSetConstantBuffers(10, 1, saved_constants.GetAddressOf());
        context_->CSSetSamplers(7, 1, saved_sampler.GetAddressOf());
        for (auto* view : saved_views) if (view) view->Release();
        for (auto* view : saved_uavs) if (view) view->Release();
        ++perf_native_compute_dispatches_; ++native_frame_stats_.native_compute_dispatches;
    };
    prep_pass(false);
    // Vertex inputs below t25 (GC_VS): ground, colour, meshes, pool, draw
    // inputs, snow trails; then the shared point lights and, with point
    // shadows, their cube pages and constants (per-vertex lamps).
    ID3D11ShaderResourceView* vs_srvs[7]{ambient_ground_srv_.Get(), gc_color_srv_.Get(), gc_vertex_srv_.Get(), gc_instance_srv_.Get(),
                                         gc_draw_input_srv_.Get(), nullptr, snow_srv};
    context_->VSSetShaderResources(0, 7, vs_srvs);
    context_->VSSetShaderResources(17, 1, &point_light_grid);
    context_->VSSetShaderResources(18, 1, &point_lights);
    context_->VSSetConstantBuffers(5, 1, native_point_light_constants_.GetAddressOf());
    if (point_shadows) {
        context_->VSSetShaderResources(PointShadowTextureLayout::first_srv, static_cast<UINT>(point_views.size()), point_views.data());
        context_->VSSetConstantBuffers(2, 1, native_shadow_constants_.GetAddressOf());
        context_->VSSetSamplers(2, 1, native_point_shadow_sampler_.GetAddressOf());
    }
    // Depth prepass and the world's sun-shadow mask (renderer.cpp
    // resolve_world_shadow_mask): the cover
    // writes depth, the receiver record and the stencil mark with its
    // alpha test, the mask pass takes the sun and cloud shadow of the marked
    // pixels, and the material pass then shades with depth EQUAL, reading
    // the mask at PS t5. Neighbour cover keeps its own sun filter.
    masked = configured_boolean(debug_overlay_.config(), "depth_prepass", true) && fog_camera_valid_ && !scene_composited_ &&
        rtv_.Get() == scene_composite_.target() && dsv_.Get() == world_dsv_.Get() && prepass_rest_rtv_ && prepass_normal_rtv_ &&
        shadow_mask_rtv_ && world_depth_read_only_dsv_ && masked_depth_state_ && depth_srv_;
    if (masked) {
        transition_profile_pass("ground_cover_prepass");
        ID3D11RenderTargetView* prepass_targets[2]{prepass_rest_rtv_.Get(), prepass_normal_rtv_.Get()};
        context_->OMSetRenderTargets(2, prepass_targets, dsv_.Get());
        context_->OMSetDepthStencilState(prepass_depth_states_[1].Get(), 0x80); // the cover's LESS_EQUAL; stencil mark 0x80
        context_->OMSetBlendState(nullptr, nullptr, 0xffffffffu);
        for (unsigned mesh = 0; mesh < gm_count; ++mesh) {
            if (!gc_capacity_[mesh]) continue;
            const bool snow = gc_mesh_kind_[mesh] == 3;
            context_->VSSetShader(gc_depth_vs_[snow ? 1u + (snow_trails ? 1u : 0u) : 0u].Get(), nullptr, 0);
            context_->PSSetShader(gc_depth_ps_[snow ? 1 : 0].Get(), nullptr, 0);
            context_->VSSetConstantBuffers(12, 1, gc_draw_constants_[mesh].GetAddressOf());
            context_->PSSetConstantBuffers(12, 1, gc_draw_constants_[mesh].GetAddressOf());
            context_->DrawIndexedInstancedIndirect(gc_args_.Get(), mesh * 20u);
        }
        if (FAILED(resolve_world_shadow_mask())) masked = false; // nothing marked is read: the unmasked variants below
        // The mask pass rebinds the world targets and leaves these to the caller.
        context_->OMSetRenderTargets(write_ambient ? 3u : history ? 2u : 1u, targets, dsv_.Get());
        context_->PSSetShaderResources(0, 5, pixel_inputs);
        context_->PSSetShaderResources(20, 2, history_srvs);
        context_->PSSetConstantBuffers(1, 1, native_world_lighting_constants_.GetAddressOf());
        context_->PSSetConstantBuffers(2, 1, native_shadow_constants_.GetAddressOf());
        context_->PSSetConstantBuffers(6, 1, shadow_history_constants_.GetAddressOf());
        context_->PSSetShaderResources(9, 1, native_shadow_depth_srv_.GetAddressOf());
        context_->PSSetSamplers(1, 1, native_shadow_sampler_.GetAddressOf());
        context_->PSSetSamplers(4, 1, native_shadow_far_sampler_.GetAddressOf());
        context_->PSSetSamplers(10, 1, native_shadow_gather_sampler_.GetAddressOf());
        context_->PSSetSamplers(8, 2, samplers);
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); context_->IASetInputLayout(nullptr);
        context_->IASetIndexBuffer(gc_indices_.Get(), DXGI_FORMAT_R32_UINT, 0);
        context_->RSSetViewports(1, &viewport); context_->RSSetState(gc_raster_.Get());
        context_->OMSetBlendState(gc_blend_.Get(), factor, ~0u);
        context_->OMSetDepthStencilState(masked ? masked_depth_state_.Get() : gc_depth_.Get(), 0);
        transition_profile_pass("ground_cover_draw");
    }
    UINT draws = 0;
    for (unsigned mesh = 0; mesh < gm_count; ++mesh) {
        if (!gc_capacity_[mesh]) continue;
        bind_mesh_shaders(mesh, false);
        context_->VSSetConstantBuffers(12, 1, gc_draw_constants_[mesh].GetAddressOf());
        context_->PSSetConstantBuffers(12, 1, gc_draw_constants_[mesh].GetAddressOf());
        context_->DrawIndexedInstancedIndirect(gc_args_.Get(), mesh * 20u);
        ++draws;
    }
    if (history && draws) shadow_history_written_ = true;
    profiler_.add_draw(draws, draws); profiler_.add_batch(draws);
    // Neighbour maps: the same rules on each neighbour's own grids at its
    // world origin, thinner (kNeighbourCoverDensity) and far meshes only; no
    // units, trails or object footprints. Placed into the same pool after the
    // active draws (the pool is sized by the largest pass of the frame).
    if (!gc_neighbours_.empty()) {
        transition_profile_pass("ground_cover_neighbours");
        context_->OMSetDepthStencilState(gc_depth_.Get(), 0); // neighbour cover: no prepass, its own sun filter
        UINT neighbour_draws = 0;
        for (std::size_t index = 0; index < gc_neighbours_.size();) {
            auto& n = gc_neighbours_[index];
            ++index;
            apply_neighbour_tile_families(n);
            const float nx = float(n.tiles_x) * 2, ny = float(n.tiles_y) * 2;
            const float low = n.lowest + n.origin[2], high = n.highest + n.origin[2];
            float corner_reach = 0;
            for (unsigned k = 0; k < 4; ++k) {
                const float dx = n.origin[0] + (k & 1 ? nx : 0) - eye[0], dy = n.origin[1] + (k & 2 ? ny : 0) - eye[1];
                const float dz = std::max(std::abs(low - eye[2]), std::abs(high - eye[2]));
                corner_reach = std::max(corner_reach, std::sqrt(dx * dx + dy * dy + dz * dz));
            }
            const float neighbour_reach = std::min({visible_ground_reach(inverse, eye, low, high, INFINITY, INFINITY), corner_reach, cover_limit});
            CellWindow w;
            if (!neighbour_cover_window(eye, n.origin, neighbour_reach, nx, ny, kCellSize, w)) continue;
            // Nothing where the neighbour seam fog is full (neighbour_fog_hides).
            if (!neighbour_cover_fog_window(n.origin, kCellSize, neighbour_fog_world(), w)) continue;
            GroundCoverConstants nc = c;
            nc.snow_materials[0]=std::uint32_t(n.contact.ready);
            nc.snow_materials[1]=std::uint32_t(n.contact.ready>>32);
            nc.window[0] = float(w.x0); nc.window[1] = float(w.y0); nc.window[2] = float(w.x1 - w.x0);
            nc.grid[0] = float(n.tiles_x); nc.grid[1] = float(n.tiles_y);
            nc.quality[1] = quality.density * kNeighbourCoverDensity;
            // Outdoor neighbours of the same allod: its region's zone, never a cave.
            nc.quality[3] = float(ambient::classify_zone(n.name.c_str(), false, environment.region));
            // The piece's raw edges, as its terrain and decorations fog (fog_edges).
            std::memcpy(nc.fog_range, n.fog_band.data(), sizeof(nc.fog_range));
            nc.view[0] = neighbour_reach; nc.view[3] = 0;
            std::memset(nc.trail_step, 0, sizeof(nc.trail_step));
            nc.snow_window[3] = 0; // no snow trails on neighbour maps
            nc.origin[0] = n.origin[0]; nc.origin[1] = n.origin[1]; nc.origin[2] = n.origin[2]; nc.origin[3] = 1;
            nc.pass[0] = UINT(index) * gm_count * 8; // index is this neighbour's position + 1
            // Its per-cell rule masks bound its candidates (all rules in one round).
            {
                const std::uint64_t key = gc_hash(gc_hash(n.type_epoch, zones_key), n.tile_classes.size());
                if (key != n.masks_key) {
                    n.masks = gc_cell_rule_masks(n.tile_classes, n.tiles_x, n.tiles_y, zones, n.origin.data());
                    n.mask_srv.Reset();
                    if (FAILED(make_grid_srv(device_.Get(), (n.tiles_x + 1) / 2, (n.tiles_y + 1) / 2, DXGI_FORMAT_R32G32_UINT, n.masks.data(),
                            ((n.tiles_x + 1) / 2) * 8, n.mask_srv)))
                        return fail("EID3D11 ground cover: neighbour cell rule masks unavailable; ground cover omitted.");
                    n.masks_key = key;
                }
            }
            UINT neighbour_bound = 0;
            {
                std::array<std::vector<std::uint32_t>, kPlaceClassCount> all_rules;
                for (std::uint32_t r = 0; r < rule_table().size(); ++r) all_rules[0].push_back(r);
                neighbour_bound = gc_window_bounds(n.masks, (n.tiles_x + 1) / 2, eye, w.x0, w.y0, w.x1, w.y1, n.lowest, n.highest, c.planes,
                    n.origin.data(), quality.density * kNeighbourCoverDensity, quality.distance, all_rules)[0];
            }
            if (!neighbour_bound) continue; // nothing can place in its visible cells: no pass, no draws
            if (FAILED(gc_ensure_stage_buffers(neighbour_bound)))
                return fail("EID3D11 ground cover: neighbour placement buffer allocation failed; ground cover omitted.");
            nc.stage[0] = gc_candidate_capacity_; nc.stage[1] = gc_survivor_capacity_;
            D3D11_MAPPED_SUBRESOURCE neighbour_mapped{};
            if (FAILED(context_->Map(gc_frame_constants_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &neighbour_mapped))) break;
            std::memcpy(neighbour_mapped.pData, &nc, sizeof(nc)); context_->Unmap(gc_frame_constants_.Get(), 0);
            // The pool is written again: no stage may still read it as input.
            ID3D11ShaderResourceView* no_vs[2]{};
            context_->VSSetShaderResources(3, 2, no_vs);
            context_->UpdateSubresource(gc_args_.Get(), 0, nullptr, gc_args_template_.data(), 0, 0);
            ID3D11ShaderResourceView* neighbour_srvs[18]{nullptr, nullptr, n.area.Get(), n.height.Get(), n.ground.Get(),
                n.fields.Get(), n.color.Get(), gc_rule_srv_.Get(), nullptr,
                nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, n.type.Get(), nullptr};
            context_->CSSetShaderResources(27, 18, neighbour_srvs);
            context_->CSSetShaderResources(47, 1, n.trees.GetAddressOf());
            context_->CSSetConstantBuffers(2, 1, native_shadow_constants_.GetAddressOf());
            context_->CSSetShaderResources(9, 1, native_shadow_depth_srv_.GetAddressOf());
            context_->CSSetSamplers(10, 1, native_shadow_gather_sampler_.GetAddressOf());
            context_->CSSetConstantBuffers(6, 1, shadow_history_constants_.GetAddressOf());
            context_->CSSetConstantBuffers(11, 1, gc_frame_constants_.GetAddressOf());
            context_->CSSetSamplers(8, 1, gc_clamp_.GetAddressOf());
            const UINT neighbour_dispatches = neighbour_pass(UINT(w.x1 - w.x0), UINT(w.y1 - w.y0), n.mask_srv.Get());
            context_->CSSetShaderResources(27, 18, no_srvs);
            context_->CSSetShaderResources(47, 1, no_srvs);
            context_->CSSetShaderResources(9, 1, no_srvs);
            restore_cs_shadow();
            context_->CSSetShader(nullptr, nullptr, 0);
            perf_native_compute_dispatches_ += neighbour_dispatches; native_frame_stats_.native_compute_dispatches += neighbour_dispatches;
            prep_pass(true);
            ID3D11ShaderResourceView* neighbour_vs[7]{n.ground.Get(), n.color.Get(), gc_vertex_srv_.Get(), gc_instance_srv_.Get(),
                                                      gc_draw_input_srv_.Get(), nullptr, nullptr};
            context_->VSSetShaderResources(0, 7, neighbour_vs);
            ID3D11ShaderResourceView* no_trail = nullptr;
            context_->PSSetShaderResources(0, 1, n.ground.GetAddressOf());
            context_->PSSetShaderResources(1, 1, n.color.GetAddressOf());
            context_->PSSetShaderResources(4, 1, &no_trail); // no object footprints on neighbours
            context_->PSSetShaderResources(5, 1, &no_trail);
            ID3D11ShaderResourceView* neighbour_art[2]{n.contact.tiles.Get(),n.contact.atlas.Get()};
            context_->PSSetShaderResources(15,2,neighbour_art);
            for (unsigned mesh = 0; mesh < gm_count; ++mesh) {
                if (!gc_capacity_[mesh]) continue;
                bind_mesh_shaders(mesh, true);
                context_->VSSetConstantBuffers(12, 1, gc_draw_constants_[mesh].GetAddressOf());
                context_->PSSetConstantBuffers(12, 1, gc_draw_constants_[mesh].GetAddressOf());
                context_->DrawIndexedInstancedIndirect(gc_args_.Get(), mesh * 20u);
                ++neighbour_draws;
            }
        }
        profiler_.add_draw(neighbour_draws, neighbour_draws); profiler_.add_batch(neighbour_draws);
    }
    if (gc_readback_pending_ < gc_count_readback_.size()) { // all in flight: this frame's count is not read back
        context_->CopyResource(gc_count_readback_[(gc_readback_head_ + gc_readback_pending_) % UINT(gc_count_readback_.size())].Get(), gc_counts_.Get());
        ++gc_readback_pending_;
    }
    ID3D11ShaderResourceView* no_zones=nullptr;
    context_->CSSetShaderResources(50,1,&no_zones);
    // Restore the slots this pass bound beyond the caller's state guard.
    ID3D11ShaderResourceView* empty[7]{};
    context_->VSSetShaderResources(0, 7, empty);
    context_->VSSetShaderResources(17, 2, empty);
    context_->PSSetShaderResources(0, 6, empty);
    context_->PSSetShaderResources(15, 2, empty);
    point_views.fill(nullptr);
    context_->PSSetShaderResources(PointShadowTextureLayout::first_srv,static_cast<UINT>(point_views.size()),point_views.data());
    if (point_shadows) {
        context_->VSSetShaderResources(PointShadowTextureLayout::first_srv, static_cast<UINT>(point_views.size()), point_views.data());
        ID3D11Buffer* no_shadow_constants = nullptr; context_->VSSetConstantBuffers(2, 1, &no_shadow_constants);
        ID3D11SamplerState* no_point_sampler = nullptr; context_->VSSetSamplers(2, 1, &no_point_sampler);
    }
    context_->PSSetShaderResources(8, 1, empty);
    context_->PSSetShaderResources(17, 2, empty);
    context_->PSSetShaderResources(20, 2, empty);
    ID3D11Buffer* no_buffers[2]{};
    context_->VSSetConstantBuffers(11, 2, no_buffers); context_->PSSetConstantBuffers(11, 2, no_buffers);
    context_->VSSetConstantBuffers(5, 1, no_buffers);
    context_->PSSetConstantBuffers(5, 2, no_buffers);
    ID3D11SamplerState* no_samplers[2]{};
    context_->VSSetSamplers(8, 2, no_samplers); context_->PSSetSamplers(8, 2, no_samplers); context_->PSSetSamplers(4, 1, no_samplers);
    context_->PSSetSamplers(10, 1, no_samplers);
    context_->PSSetSamplers(2, 1, no_samplers);
    context_->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
    context_->OMSetRenderTargets(1, rtv_.GetAddressOf(), dsv_.Get());
    scene_updated_ = true;
    // The placement rejects every unpainted ground texel: no colour, no cover.
    if (gc_color_unpainted_)
        return skip(gc_material_state_.empty() ? "no terrain tile materials to colour" :
                    gc_paint_failure_ ? gc_paint_failure_ : "terrain atlases not painted yet");
    gc_skip_reason_ = nullptr; gc_skip_reported_ = false;
    return S_OK;
    } catch (const std::bad_alloc&) {
        return fail("EID3D11 ground cover: CPU allocation failed; ground cover omitted.");
    }
}

} // namespace owned
