#pragma once

// The world's native (GPU-driven) shader: one source for the terrain, object,
// shadow and tessellation stages, assembled from the shared HLSL fragments of
// the feature headers. The renderer compiles compose_native_shader_source()
// with the embedded feature text; Prototypes/shader-lab composes the same
// source with the feature .hlsl files it reads from disk (Source/shaders).

#include "contact_blend.h"
#include "ground_cover_surface.h"
#include "liquid_surface_shader.h"
#include "native_instance_flags.h"
#ifdef EID3D11_EMBEDDED
#include "embedded_triangle_shader.h"
#endif
#include "neighbour_fog.h"
#include "point_shadow_types.h"
#include "shadow_softness.h"
#include "sky_clouds.h"
#include "snow_trails.h"
#include "static_lod.h"
#include "terrain_bake.h"
#include "terrain_cliff.h"
#include "terrain_material.h"
#include "terrain_tile_blend.h"
#include "weather_wind.h"
#include "world_fog.h"

#include <string>
#include <string_view>

namespace owned {

inline constexpr char kNativeShader[] = R"(
struct NativeInstance{row_major float4x4 objectToClip;row_major float4x4 objectToWorld;uint4 geometryOffsets;uint2 colorOffsets;float2 nativeUvShift;uint nativeFlags;uint nativeDefaultDiffuse;uint nativeDefaultSpecular;uint morphOffset;uint morphCount;uint lightingEnabled;uint4 nativePadding;uint terrainVertexOffset;uint terrainPhaseOffset;uint terrainPhaseCount;uint terrainTilesPerSide;uint terrainFlags;float2 terrainWindDirection;float terrainWindForce;float terrainWindForceNormalized;float terrainWindPhase;float terrainPreciseTime;float2 terrainFogMin;float2 terrainFogMax;float terrainFogDistance;uint terrainFogEnabled;float4 nativeEmissive;float4 nativeSpecular;float nativeMaterialAlpha;float3 nativeMaterialDiffuse;uint worldGeometry;uint shapeCount;uint positionGroups;uint secondMorphOffset;float morphFraction;float3 shape;uint poseNode;};
struct NativeTerrainVertex{float noise;float windMultiplier;float3 objectColor;float depth;uint flags;uint animationIndex;uint animationBaseTile;uint gridIndex;float opacity;float3 emissive;float liquidDepth;float liquidMultiplier;float4 liquidUvBounds;float liquidBend;float liquidCascade;float2 liquidCurrent;float liquidSelfIllumination;float liquidBedEmission;uint slopeNormal;uint shiftObjects;float liquidDrop;float liquidFill;};
struct NativeDraw{uint instanceIndex;uint componentOffset;uint indexOffset;uint indexCount;uint outputTriangleBase;uint indirectArgsByteOffset;uint orderedTriangleOffset;uint preservesOrder;float4 boundsMin;float4 boundsMax;uint preparedVertexBase;uint componentCount;uint prepareWorld;uint colorVisible;uint worldState;uint2 worldGeneration;float geometryRadius;uint steepFirstTriangle;};
struct NativePreparedVertex{float4 p;float4 d;float4 s;float2 uv;uint2 textureReference;};
struct NativeWorldVertex{float4 p;float4 normal;float2 uv;float2 undeformedXY;uint diffuse;uint specular;uint sourcePosition;uint partNode;};
cbuffer NativeRasterConstants:register(b0){row_major float4x4 nativeWorld;row_major float4x4 nativeView;row_major float4x4 nativeProjection;float4 nativeViewport;float4 nativeFogColor;float4 nativeTextureFactor;uint nativeTransformed;uint nativeTextured;uint nativeColorOp;uint nativeAlphaOp;uint nativeColorArg1;uint nativeColorArg2;uint nativeAlphaArg1;uint nativeAlphaArg2;uint nativeAlphaTest;uint nativeAlphaFunc;float nativeAlphaRef;uint nativeFogEnable;uint nativeSpecularEnable;uint nativeTriangleBase;uint2 nativeRasterTail;float4 nativeContactOrigin;uint2 nativeContactMapMaterials;uint nativeLiquidLayer;uint nativeContactTail;uint2 nativeCombiner;float nativeHudOpacity;uint nativeCombinerTail;float4 nativeContactLayout;};
cbuffer NativeWorldLightingConstants:register(b1){float4 nativeWorldAmbient;float4 nativeWorldSunDirection;float4 nativeWorldSunDiffuse;};
cbuffer NativePointLightConstants:register(b5){uint nativePointLightCount;uint2 nativePointLightGridSize;uint nativePointLightGlowCount;float2 nativePointLightGridOrigin;float nativePointLightGridInverseCell;float nativePointLightGridTail;};
struct NativePointLight{float4 centerInverseRadiusSquared;float4 color;float4 shadowCenter;float4 shadowSlots;};
StructuredBuffer<NativePointLight> nativePointLights:register(t18);
// World X/Y light grid (point_light_grid.h): cell starts, then light indices
// in ascending order. A pixel visits only its cell's lights; every light that
// can reach the pixel is listed, so the lighting sum is unchanged.
StructuredBuffer<uint> nativePointLightGrid:register(t17);
uint2 nativePointLightRange(float3 worldPosition){
 float2 cell=floor((worldPosition.xy-nativePointLightGridOrigin)*nativePointLightGridInverseCell);
 if(!(all(cell>=0)&&all(cell<(float2)nativePointLightGridSize)))return uint2(0,0);
 uint index=(uint)cell.y*nativePointLightGridSize.x+(uint)cell.x;
 return uint2(nativePointLightGrid[index],nativePointLightGrid[index+1]);
}
)" R"(
// Point shadow atlases (renderer_point_shadows.cpp): static cubes (D16, t25)
// and overlay cubes (D32, t26). Slots below NATIVE_POINT_ATLAS_CUBES are
// static cubes, the others overlay cubes. Each slice holds a
// NATIVE_POINT_ATLAS_TILES-square grid of cube faces of shadowControl.w
// texels, each inside a one-texel gutter held at depth 1 (what the former
// per-face arrays' border colour gave the comparison filter).
Texture2DArray<float> nativePointShadowStatic:register(t25);
Texture2DArray<float> nativePointShadowOverlay:register(t26);
cbuffer NativeShadowConstants:register(b2){row_major float4x4 shadowLight[6];row_major float4x4 shadowCamera[6];float4 shadowBias[6];uint4 shadowControl;float4 shadowScrollCopied;uint4 shadowScrollControl;};
// Sun shadows live (a cascade enabled, or the missing-cascade marker on);
// NATIVE_OFF_SUN_SHADOWS: a specialised program bound while neither is.
#ifdef NATIVE_OFF_SUN_SHADOWS
#define NATIVE_SUN_SHADOWS_LIVE false
#else
#define NATIVE_SUN_SHADOWS_LIVE (shadowControl.x!=0||shadowBias[4].z>.5)
#endif
Texture2DArray<float> shadowDepth:register(t9);
// Early-out cells of the sun maps (nativeShadowEarly): nearest, 1 - farthest.
Texture2DArray<float2> shadowRange:register(t64);
SamplerComparisonState shadowSampler:register(s1);
SamplerComparisonState pointShadowSampler:register(s2);
SamplerComparisonState shadowFarSampler:register(s4);
cbuffer NativeShadowHistoryConstants:register(b6){row_major float4x4 previousWorldClip;float4 shadowHistorySettings;float4 shadowJitter;};
Texture2D<float> previousShadowVisibility:register(t20);
Texture2D<float> previousSceneDepth:register(t21);
SamplerState shadowHistorySampler:register(s3);
// Point/clamp, non-comparison: raw sun-map depth for shadowPlaneCompare.
SamplerState shadowGatherSampler:register(s10);
Texture2D nativeTexture:register(t0);
Texture2DArray nativeTextureArray:register(t6);
SamplerState nativeSampler:register(s0);
// Explicit gradients and levels: a real branch samples only the texture the
// reference names (the ?: form sampled both).
float4 terrainTapGrad(uint2 ref,float2 uv,float2 dx,float2 dy){
 [branch]if(ref.y!=0)return nativeTextureArray.SampleGrad(nativeSampler,float3(uv,ref.x),dx,dy);
 return nativeTexture.SampleGrad(nativeSampler,uv,dx,dy);
}
float4 terrainTapLevel(uint2 ref,float2 uv,float lod){
 [branch]if(ref.y!=0)return nativeTextureArray.SampleLevel(nativeSampler,float3(uv,ref.x),lod);
 return nativeTexture.SampleLevel(nativeSampler,uv,lod);
}
float2 terrainTapSize(uint2 ref,out float levels){
 float w,h,e;if(ref.y!=0)nativeTextureArray.GetDimensions(0,w,h,e,levels);else nativeTexture.GetDimensions(0,w,h,levels);
 return float2(w,h);
}
@TERRAIN_MATERIAL@
ByteAddressBuffer nativeGeometry:register(t1);
ByteAddressBuffer nativeDynamic:register(t2);
StructuredBuffer<uint4> nativeVisibleTriangles:register(t4);
StructuredBuffer<NativeDraw> nativeDraws:register(t5);
struct NativeDrawWork{uint record;uint outputTriangleBase;uint indirectArgsByteOffset;uint orderedTriangleOffset;uint flags;uint preparedVertexBase;uint workCount;uint steepOutputTriangleBase;uint steepArgsByteOffset;};
StructuredBuffer<NativeDrawWork> nativeDrawWork:register(t16);
cbuffer NativeWorkRange:register(b4){uint nativeWorkBase;uint nativeWorkCount;uint2 nativeWorkPadding;};
RWStructuredBuffer<NativeDraw> nativeDrawOutput:register(u7);
RWByteAddressBuffer nativePhaseOutput:register(u0);
[numthreads(64,1,1)]void nativeTerrainPhaseCS(uint3 group:SV_GroupID,uint lane:SV_GroupIndex){
 NativeDrawWork work=nativeDrawWork[group.x];
 for(uint i=lane;i<work.indirectArgsByteOffset;i+=64)
  nativePhaseOutput.Store(work.outputTriangleBase+i*4,nativeDynamic.Load(work.record+i*4));
}
bool loadNativeDraw(uint3 group,out NativeDraw draw){
 draw=(NativeDraw)0;uint local=group.x+group.y*65535u;
 if(local>=nativeWorkCount)return false;uint index=nativeWorkBase+local;
 NativeDrawWork work=nativeDrawWork[index];if(work.record==0xffffffffu)return false;
 draw=nativeDraws[work.record];
 draw.outputTriangleBase=work.outputTriangleBase;draw.indirectArgsByteOffset=work.indirectArgsByteOffset;
 draw.orderedTriangleOffset=work.orderedTriangleOffset;draw.preservesOrder=work.flags&1;
 draw.prepareWorld=(work.flags&2)?(1u|((work.flags>>8)&0xff00u)):0u;
 draw.colorVisible=(work.flags>>8)&255;draw.preparedVertexBase=work.preparedVertexBase;
 return true;
}
StructuredBuffer<NativePreparedVertex> nativePreparedVertices:register(t7);
StructuredBuffer<NativeWorldVertex> nativeWorldVertices:register(t8);
struct NativePose{uint asset;uint outputBase;uint frame;float fraction;float3 shape;float heightScale;float3 centerShift;uint workCount;row_major float4x4 rootToWorld;uint2 generation;};
struct NativePoseNode{float4 rotation;float4 translation;float4 row0;float4 row1;float4 row2;uint morphFirst;uint morphSecond;uint morphCount;float morphFraction;uint2 generation;};
ByteAddressBuffer nativeAnimation:register(t10);
StructuredBuffer<NativePose> nativePoses:register(t11);
StructuredBuffer<NativePoseNode> nativePoseNodes:register(t12);
RWStructuredBuffer<NativePoseNode> nativePoseOutput:register(u4);
cbuffer NativeWorldClip:register(b3){row_major float4x4 nativeCurrentWorldClip;row_major float4x4 nativeCullWorldClip;float4 nativeCullParams;float4 nativeHiZParams;float4 nativeDistanceCull;float4 nativeImpostorParams;};
Texture2D<float> nativeHiZ:register(t26);
StructuredBuffer<NativeInstance> nativePersistentInstances:register(t13);
struct NativeInstanceUpdate{uint destination;uint count;uint flags;NativeInstance value;NativeDraw descriptor;};
StructuredBuffer<NativeInstanceUpdate> nativeInstanceUpdates:register(t14);
RWStructuredBuffer<NativeInstance> nativeInstanceOutput:register(u5);
@WEATHER_WIND@// Terrain frame values (settings, clock, fog, wind, seam fog, cliff blend).
// Read from a one-element structured buffer, not a constant buffer: the
// NVIDIA driver constant-folds rarely changing cbuffer values into new pixel
// shader variants (one set per settings change); buffer loads are not folded.
// Every member is 16-byte aligned, so the layout equals the old b8 registers.
struct NativeTerrainFrameValues{float4 nativeTerrainWindValue;float4 nativeTerrainClockValue;float4 nativeTerrainFogRangeValue;float4 nativeTerrainFogControlValue;float4 nativeTerrainMaterialSwitchesValue;WeatherWind nativeWeatherWindValue;float4 nativeTerrainActiveBoundsValue;
 // x ground contact blend strength (0 off), y band height (m); zw bits of the
 // contact atlas slices already copied (materials 0-31, 32-63). Then the
 // neighbour seam fog (neighbour_fog.h): seam edges and curve (inside, outside, seam density).
 float4 nativeTerrainContactSharedValue;float4 nativeNeighbourSeamValue;float4 nativeNeighbourCurveValue;
 // [0] Terrain cliffs (terrain_cliff.h): x side projection (0 off), y tessellation
 // max factor (0 off); z terrain material blend (0 off, 1 fast, 2 full; rows
 // bound), w a slot's atlas padding as a fraction of its side (terrain_tile_blend.h).
 // [1] Cliff tessellation: x largest displacement on unwalkable faces (m);
 // y temporary terrain blend diagnostic view (terrain_blend_debug); z the
 // Cliff blend rock shape detail (cliff_rock_detail).
 // Then the seam's touched runs.
 float4 nativeTerrainCliffSharedValue[2];float4 nativeNeighbourSpanLoValue;float4 nativeNeighbourSpanHiValue;
 // Rendered piece rectangles (neighbour_fog_piece_constants): x of the info is the count.
 float4 nativeNeighbourPieceInfoValue;float4 nativeNeighbourPiecesValue[NEIGHBOUR_FOG_PIECES];
 // The live Cliff blend options (terrain_tile_blend.h).
 float4 nativeCliffBlendValue[2];
 // Snow trail field window (snow_trails.h snowTrailRaw); w 0: no field bound.
 // Then its active tile bits.
 float4 nativeSnowTrailWindowValue;uint4 nativeSnowTrailTilesValue;
 // Wet surfaces (owned precipitation, renderer_precipitation.cpp): x wetness
 // 0..1 (0 dry), y rain on the ground now (ripples), z game seconds, w the
 // precipitation quality (2+: water-film sheen, 3+: rain ripples).
 float4 nativeWetValue;
 // The precipitation shelter map (straight-down depth of the static objects,
 // nativeShelter t70): map x/y = row.x*x+row.y*y+row.z for rows 0 and 1; a
 // surface at depth d lies at row2.y-d*row2.x; row0.w 1 while one is bound.
 float4 nativeShelterValue[3];
 // Snow build-up (owned precipitation): x cover 0..1 (0 none), yz the snow
 // trail window's smooth centre (snowTrailRaw's fade), w the precipitation
 // quality (2+: fine noise octave).
 float4 nativeSnowValue;};
Texture2D<float4> nativeShelter:register(t70); // shelterFilterCS: r closed depth, g/b narrow/wide occupancy, a foliage share
StructuredBuffer<NativeTerrainFrameValues> nativeTerrainFrame:register(t127);
#define nativeTerrainWind nativeTerrainFrame[0].nativeTerrainWindValue
#define nativeTerrainClock nativeTerrainFrame[0].nativeTerrainClockValue
#define nativeTerrainFogRange nativeTerrainFrame[0].nativeTerrainFogRangeValue
#define nativeTerrainFogControl nativeTerrainFrame[0].nativeTerrainFogControlValue
#define nativeTerrainMaterialSwitches nativeTerrainFrame[0].nativeTerrainMaterialSwitchesValue
#define nativeWeatherWind nativeTerrainFrame[0].nativeWeatherWindValue
#define nativeTerrainActiveBounds nativeTerrainFrame[0].nativeTerrainActiveBoundsValue
#define nativeTerrainContactShared nativeTerrainFrame[0].nativeTerrainContactSharedValue
#define nativeNeighbourSeam nativeTerrainFrame[0].nativeNeighbourSeamValue
#define nativeNeighbourCurve nativeTerrainFrame[0].nativeNeighbourCurveValue
#define nativeTerrainCliffShared nativeTerrainFrame[0].nativeTerrainCliffSharedValue
)" R"(
#define nativeNeighbourSpanLo nativeTerrainFrame[0].nativeNeighbourSpanLoValue
#define nativeNeighbourSpanHi nativeTerrainFrame[0].nativeNeighbourSpanHiValue
#define nativeNeighbourPieceInfo nativeTerrainFrame[0].nativeNeighbourPieceInfoValue
#define nativeNeighbourPieces nativeTerrainFrame[0].nativeNeighbourPiecesValue
#define nativeCliffBlend nativeTerrainFrame[0].nativeCliffBlendValue
#define nativeSnowTrailWindow nativeTerrainFrame[0].nativeSnowTrailWindowValue
#define nativeSnowTrailTiles nativeTerrainFrame[0].nativeSnowTrailTilesValue
#define nativeWet nativeTerrainFrame[0].nativeWetValue
#define nativeShelterRow nativeTerrainFrame[0].nativeShelterValue
#define nativeSnowCover nativeTerrainFrame[0].nativeSnowValue
@SNOW_TRAILS@
// Value noise for the snow build-up's unevenness (world metres).
// Integer lattice hash: a sin() hash of world coordinates quantises into
// visible blocks at map-scale arguments (run p2: square snow patches).
float nativeSnowHash(float2 c){
 uint2 q=uint2(int2(c));uint h=q.x*0x8da6b343u^q.y*0xd8163841u;
 h^=h>>16;h*=0x7feb352du;h^=h>>15;h*=0x846ca68bu;h^=h>>16;return (h&0xffffffu)/16777216.0;
}
float nativeSnowNoise(float2 p){
 float2 i=floor(p),f=p-i;f=f*f*(3-2*f);
 return lerp(lerp(nativeSnowHash(i),nativeSnowHash(i+float2(1,0)),f.x),lerp(nativeSnowHash(i+float2(0,1)),nativeSnowHash(i+1),f.x),f.y);
}
// Height of the highest static surface above p, or -1e9 (none or outside).
// Shelter at one texel of the soft map (shelterFilterCS) for build-up fill
// 0..1: roofs and other solids take a 0.25 m feather inside their outline;
// crowns (filled leaf gaps nearby) shelter from the centre out, and as snow
// builds up it fills in from the rim toward the trunk, by distance inside
// the closed crown outline, down to a core of half the crown's radius
// (frame_001039: the blurred occupancy, mixed with the roof edge by a
// varying crown share, sheltered the rim more than the inner ring).
float shelterTexelKeep(float4 m,float fill){
 if(nativeShelterRow[2].w>.5)return 1; // the raw map (soft filter unavailable): height test only
 // Crowns fill in slowly (the square of the build-up) and keep a bare core
 // of half their radius (9252c204 user note: the core vanished within
 // minutes); roofs and other solids never fill in.
 // fill: the crown fill-in (Renderer::shelter_fill_, its own slow clock).
 // Crowns: a soft edge over 35% of their radius from the rim, moving in by
 // at most a quarter of the radius, so the inner 40% always stays sheltered
 // (c6c05f03: a fill by distance in metres against 2-3 m crowns left
 // nothing); roofs over 0.25 m (with the 0.25 m tap ring, about half a metre).
 float inside=m.g*4,radius=max(m.a*4,.5),crown=m.b;
 float start=fill*.25*radius;
 return lerp(smoothstep(0,.25,inside),smoothstep(start,start+.35*radius,inside),crown);
}
// Shelter cover at p for a surface at height z: 0 open, 1 under a roof or
// canopy (its top 0.3-0.9 m above z), bilinear between the map's texels.
// Each texel is compared first, then blended, so a roof edge never blends
// heights; point loads drew staircase texel edges (frame_002927). The map
// is the bake's soft version (shelterFilterCS): crowns are closed (no
// per-leaf holes) and fade in over about a metre inside their silhouette;
// roofs keep a narrow edge.
float nativeShelterCover(float2 p,float z){
 [branch]if(nativeShelterRow[0].w<.5)return 0;
 float2 n=float2(dot(nativeShelterRow[0].xy,p)+nativeShelterRow[0].z,dot(nativeShelterRow[1].xy,p)+nativeShelterRow[1].z);
 if(any(abs(n)>=1))return 0;
 uint w,h;nativeShelter.GetDimensions(w,h);
 float2 c=float2(n.x*.5+.5,.5-.5*n.y)*float2(w,h)-.5;
 int2 base=int2(floor(c));float2 f=c-base;
 float v[4];
 [unroll]for(uint k=0;k<4;++k){
  int2 t=clamp(base+int2(k&1u,k>>1),int2(0,0),int2(w-1,h-1));
  float4 m=nativeShelter.Load(int3(t,0));
  v[k]=m.r>=1?0:saturate((nativeShelterRow[2].y-m.r*nativeShelterRow[2].x-z-.3)/.6)*shelterTexelKeep(m,nativeShelterRow[1].w);
 }
 return lerp(lerp(v[0],v[1],f.x),lerp(v[2],v[3],f.x),f.y);
}
// Snow trails: whether the world rectangle lo..hi, grown by 3 m, touches an
// active trail tile (the 8 m tiles of the field window, 10 x 10,
// snow_trails.h; snowTrailRaw reads nothing outside them). False without a
// field. Flat tiles take the tessellation stages only where this holds
// (flush_native), and flat-class edges stay whole elsewhere. 3 m is more
// than a terrain triangle's extent (2.83 m, the 2 m tile's diagonal): every
// edge of a triangle that reaches trail relief is itself near, so such a
// triangle subdivides exactly as with every flat edge free.
bool nativeSnowTrailNear(float2 lo,float2 hi){
 [branch]if(nativeSnowTrailWindow.w<=0)return false;
 float scale=nativeSnowTrailWindow.z/64;
 int2 a=int2(max(floor((lo-3-nativeSnowTrailWindow.xy)*scale),0));
 int2 b=int2(min(floor((hi+3-nativeSnowTrailWindow.xy)*scale),9));
 bool near=false;
 [loop]for(int y=a.y;y<=b.y;++y)[loop]for(int x=a.x;x<=b.x;++x){
  uint bit=uint(y)*10u+uint(x);near=near||((nativeSnowTrailTiles[bit>>5]>>(bit&31u))&1u)!=0;
 }
 return near;
}
// A drawn triangle's rest (untessellated) corners near an active trail tile.
bool nativeTriangleNearTrail(uint4 triangleRef){
 float2 a=nativeWorldVertices[triangleRef.y].p.xy,b=nativeWorldVertices[triangleRef.z].p.xy,c=nativeWorldVertices[triangleRef.w].p.xy;
 return nativeSnowTrailNear(min(a,min(b,c)),max(a,max(b,c)));
}
// The names of the former PS b9 view of the same values.
// Settings-specialised programs (renderer.cpp native specialisation): a
// feature its setting switched off reads as the 0 the renderer then uploads,
// so its code compiles out. Such a program is bound only while the uploaded
// value is 0: NATIVE_OFF_ANTITILE (switches y), NATIVE_OFF_HEIGHT_BLEND
// (switches z), NATIVE_OFF_CONTACT (contact strength), NATIVE_OFF_CLIFFS
// (cliffs x), NATIVE_OFF_BLEND (material blend z). Switches x and w are
// always 0.
#ifdef NATIVE_OFF_ANTITILE
#define NATIVE_SPEC_ANTITILE 0
#else
#define NATIVE_SPEC_ANTITILE nativeTerrainMaterialSwitches.y
#endif
#ifdef NATIVE_OFF_HEIGHT_BLEND
#define NATIVE_SPEC_HEIGHT_BLEND 0
#else
#define NATIVE_SPEC_HEIGHT_BLEND nativeTerrainMaterialSwitches.z
#endif
#ifdef NATIVE_OFF_CONTACT
#define NATIVE_SPEC_CONTACT 0
#else
#define NATIVE_SPEC_CONTACT nativeTerrainContactShared.x
#endif
#ifdef NATIVE_OFF_CLIFFS
#define NATIVE_SPEC_CLIFFS 0
#else
#define NATIVE_SPEC_CLIFFS nativeTerrainCliffShared[0].x
#endif
#ifdef NATIVE_OFF_BLEND
#define NATIVE_SPEC_BLEND 0
#else
#define NATIVE_SPEC_BLEND nativeTerrainCliffShared[0].z
#endif
#define nativeTerrainMaterial float4(0,NATIVE_SPEC_ANTITILE,NATIVE_SPEC_HEIGHT_BLEND,0)
#define nativeTerrainActiveBoundsPS nativeTerrainActiveBounds
#define nativeContactBlend float2(NATIVE_SPEC_CONTACT,nativeTerrainContactShared.y)
#define nativeContactMaterials asuint(nativeTerrainContactShared.zw)
#define nativeTerrainCliff float4(NATIVE_SPEC_CLIFFS,nativeTerrainCliffShared[0].y,NATIVE_SPEC_BLEND,nativeTerrainCliffShared[0].w)
#define nativeTerrainTess nativeTerrainCliffShared[1]
// neighbourPieceSideCover / neighbourPieceFogDensity (neighbour_fog.h, the
// same math) reading the frame's piece list in place: passed as an array
// parameter, the list was copied into a local array at every inlined side
// (eight [8] float4 arrays in every native vertex shader).
float nativeNeighbourPieceSideCover(float edge,float outward,float along,bool alongY,float d,uint count){
 float across=edge+outward*.5,lo=along,hi=along;bool found=false;
 [loop]for(uint k=0;k<count;++k){float4 r=nativeNeighbourPieces[k];float2 s=alongY?r.yw:r.xz,a=alongY?r.xz:r.yw;
  if(across>=a.x&&across<=a.y&&along>=s.x&&along<=s.y){found=true;lo=min(lo,s.x);hi=max(hi,s.y);}}
 if(!found)return 0;
 [loop]for(uint sweep=0;sweep<count;++sweep)[loop]for(uint m=0;m<count;++m){float4 r=nativeNeighbourPieces[m];float2 s=alongY?r.yw:r.xz,a=alongY?r.xz:r.yw;
  if(across>=a.x&&across<=a.y){if(s.x<=hi&&s.y>hi)hi=s.y;if(s.y>=lo&&s.x<lo)lo=s.x;}}
 return saturate(min(along-lo,hi-along)/d);
}
float nativeNeighbourPieceFogDensity(float2 p,float4 piece,float d,float4 seam,float4 curve,uint count){
 float4 t=min(float4(p-piece.xy-(d+1),piece.zw-(d+1)-p),0)/d;
 float sum=0;
 [unroll]for(uint side=0;side<4;++side)[branch]if(t[side]<0){
  bool alongY=(side&1)==0;
  float keep=1-nativeNeighbourPieceSideCover(piece[side],side<2?-1:1,alongY?p.y:p.x,alongY,d,count);
  sum+=t[side]*keep*t[side]*keep;
 }
 return saturate(sqrt(sum+neighbourSeamFog(p,seam,curve)));
}
NativeInstance loadNativeInstance(uint index){
 NativeInstance value=nativePersistentInstances[index&0x7fffffffu];
 if((index&0x80000000u)!=0)value.objectToClip=nativeCurrentWorldClip;
 // Terrain globals come from the frame, so registered terrain records never
 // carry a stale clock, wind or fog.
 if(value.terrainTilesPerSide!=0&&nativeTerrainClock.w!=0){
  value.terrainWindDirection=nativeTerrainWind.xy;value.terrainWindForce=nativeTerrainWind.z;
  value.terrainWindForceNormalized=nativeTerrainWind.w;value.terrainWindPhase=nativeTerrainClock.x;
  value.terrainPreciseTime=nativeTerrainClock.y;
  // Liquid detail clock (flow, detail drift, rain rings): neighbour records
  // are uploaded once and would otherwise keep time 0, freezing their water
  // against the animated water of the active map across a shared edge.
  value.nativePadding.z=asuint(nativeTerrainClock.y);
  value.terrainFlags=(value.terrainFlags&~8u)|(nativeTerrainClock.z!=0?8u:0u);
  if((value.nativeFlags&@NEIGHBOUR_FLAG@)!=0){
   // Raw piece edges: neighbourPieceFogDensity insets them itself.
  }else{value.terrainFogMin=nativeTerrainFogRange.xy;value.terrainFogMax=nativeTerrainFogRange.zw;}
  value.terrainFogDistance=nativeTerrainFogControl.x;value.terrainFogEnabled=nativeTerrainFogControl.y!=0?1u:0u;
 }
 return value;
}
[numthreads(64,1,1)]void nativeInstanceUpdateCS(uint3 group:SV_GroupID,uint lane:SV_GroupIndex){
 uint index=(group.x+group.y*65535u)*64u+lane;
 if(index<nativeWorkCount){NativeInstanceUpdate update=nativeInstanceUpdates[nativeWorkBase+index];
  if((update.flags&1)!=0)nativeInstanceOutput[update.destination]=update.value;
  if((update.flags&2)!=0)nativeDrawOutput[update.destination]=update.descriptor;
 }
}
RWStructuredBuffer<uint4> nativeTriangleOutput:register(u0);
RWByteAddressBuffer nativeIndirectArgs:register(u1);
RWStructuredBuffer<NativePreparedVertex> nativePreparedOutput:register(u2);
RWStructuredBuffer<NativeWorldVertex> nativeWorldOutput:register(u3);
struct NativeWorldState{uint2 geometry;uint2 pose;uint preparations;uint components;};
RWStructuredBuffer<NativeWorldState> nativeWorldStateOutput:register(u6);
StructuredBuffer<NativeWorldState> nativeWorldStates:register(t15);
struct NativeOutput{float4 p:SV_Position;float4 d:COLOR0;float4 s:COLOR1;float2 uv:TEXCOORD0;nointerpolation uint2 textureReference:TEXCOORD1;float3 worldPosition:TEXCOORD2;float3 worldNormal:TEXCOORD3;float3 directional:TEXCOORD4;float3 emissive:TEXCOORD5;nointerpolation uint shadowMode:TEXCOORD6;float4 pointMaterial:TEXCOORD7;
 float4 liquidTintDepth:TEXCOORD8;nointerpolation float4 liquidFlow:TEXCOORD9;
 float4 liquidMaterial:TEXCOORD10;float3 liquidView:TEXCOORD11;
 float2 bedLiquid:TEXCOORD12;nointerpolation uint terrainVertexFlags:TEXCOORD13;
 nointerpolation float4 liquidUvBounds:TEXCOORD14;float4 liquidDirections:TEXCOORD15;float2 liquidTileLocal:TEXCOORD16;
 float3 terrainSlope:TEXCOORD17;nointerpolation float terrainTiles:TEXCOORD18;nointerpolation uint swaying:TEXCOORD19;nointerpolation uint neighbour:TEXCOORD21;
 nointerpolation uint contact:TEXCOORD22;nointerpolation float contactHeight:TEXCOORD23;
 // Its own register (zw unused): packed beside liquidTileLocal, the drop and
 // pit fill reached the pixels as garbage that changed every frame on the
 // untessellated draws (NVIDIA, save1152/zone3obr waterfall pool).
 float4 liquidFall:TEXCOORD27;
 // Facet flatness (terrain_facet_flatness: flattest, mean), interpolated per vertex.
 float2 terrainFlat:TEXCOORD29;
 // Rest (undisplaced) world position: shadow lookups stay attached to swaying leaves.
 float3 shadowPosition:TEXCOORD20;};
@LIQUID_SURFACE@
// Fine detail is world-space only. Mud may use one coarse frame-average tint;
// it fades to shared authored values at tile boundaries, never exposes texels.
)" R"(
// Algae patch colour only: the current flipbook frame of the liquid's own tile
// (liquidUvBounds, already phase-shifted), repeated per liquidSwampTile in
// world space. Gradients are explicit; the mip is capped where the atlas
// padding would bleed and fades to the frame's mean beyond it. mean: the
// frame's mean colour, the swamp colour the game itself draws.
// A tile flagged native_terrain_swamp_art_map (no same-page pure swamp frame
// to borrow) reads the map swamp frame's own wrapping, mipped texture.
Texture2D<float4> liquidSwampFrame:register(t33);
bool liquidSwampFrameBound(){float w,h,levels;liquidSwampFrame.GetDimensions(0,w,h,levels);return w>0;}
float4 nativeSwampArt(NativeOutput i,float4 gradient,out float3 mean) {
 mean=0;
 float2 drift=liquidSafeNormal(float3(i.liquidFlow.xy,0)).xy*i.liquidFlow.w*liquidSecondsPerTime*liquidSwampDrift;
 float2 s=(i.worldPosition.xy-drift)/liquidSwampTile;
 [branch]if((i.terrainVertexFlags&8192u)!=0){
  float w,h,levels;liquidSwampFrame.GetDimensions(0,w,h,levels);
  if(w<=0)return 0;
  mean=liquidSwampFrame.SampleLevel(liquidNormalSampler,float2(.5,.5),levels-1).rgb;
  return float4(liquidSwampFrame.SampleGrad(liquidNormalSampler,s,gradient.xy/liquidSwampTile,gradient.zw/liquidSwampTile).rgb,1);
 }
 float4 b=i.liquidUvBounds;float2 span=b.zw-b.xy;
 if(nativeTextured==0||any(span<=0))return 0;
 float2 dx=gradient.xy/liquidSwampTile*span,dy=gradient.zw/liquidSwampTile*span;
 float levels;float2 size=terrainTapSize(i.textureReference,levels);
 float lod=log2(max(max(length(dx*size),length(dy*size)),1e-6));
 float padding=min(span.x*size.x,span.y*size.y)*terrainTileMargin/(1-2*terrainTileMargin);
 float safe=max(log2(max(padding,1))-.5,0);
 float k=exp2(min(safe-lod,0));
 float3 fine=terrainTapGrad(i.textureReference,b.xy+frac(s)*span,dx*k,dy*k).rgb;
 mean=terrainTapLevel(i.textureReference,(b.xy+b.zw)*.5,log2(max(min(span.x*size.x,span.y*size.y),1))).rgb;
 return float4(lerp(fine,mean,smoothstep(safe,safe+1.5,lod)),1);
}
float4 nativeLiquidSurfaceColor(NativeOutput i,float3 ambient,float3 sunDirection,
 float3 sunColor,float3 skyColor,float footprint=0,float depthFootprint=0,float4 gradient=0,float2 heightGradient=0) {
 LiquidSurfaceInput liquid;
 liquid.footprint=footprint;liquid.depthFootprint=depthFootprint;liquid.gradient=gradient;
 liquid.heightGradient=heightGradient;
 liquid.position=i.worldPosition;liquid.viewDirection=i.liquidView;
 liquid.bend=i.liquidDirections.x;liquid.cascade=i.liquidDirections.y;liquid.current=i.liquidDirections.zw;
 liquid.drop=i.liquidFall.x;liquid.fill=i.liquidFall.y;
 // The water body (liquid layer 2) and the foam layer (3) draw through their
 // own programs (NATIVE_LIQUID_BODY / NATIVE_LIQUID_FOAM).
#if defined(NATIVE_LIQUID_FOAM)
 liquid.layer=1;
#elif defined(NATIVE_LIQUID_BODY)
 liquid.layer=0;
#else
 liquid.layer=nativeLiquidLayer&1u;
#endif
 liquid.tint=i.liquidTintDepth.rgb;liquid.depth=i.liquidTintDepth.a;
 liquid.emissive=i.emissive;liquid.opacity=i.liquidMaterial.y;liquid.multiplier=i.liquidMaterial.x;
 liquid.selfIllumination=i.liquidMaterial.z;
 // Swamp share of the liquid's own tiles (their tile type, decoded per vertex).
 float mud=saturate(i.liquidMaterial.w)*liquidShoreEnabled(liquid.selfIllumination);
#ifdef NATIVE_LIQUID_WATER
 liquid.selfIllumination=0;mud=0; // no lava or swamp vertex in the batch
#endif
#ifdef NATIVE_LIQUID_LAVA
 mud=0; // every liquid vertex self-illuminated: no swamp share
#endif
 // Every swamp term follows one coverage edge (irregular, soft, world space).
 // A tile that reads the map swamp frame takes no swamp while it is unbound.
 mud=(i.terrainVertexFlags&8192u)!=0&&!liquidSwampFrameBound()?0:liquidSwampCoverage(i.worldPosition.xy,mud);
 liquid.mud=mud;
 liquid.art=0;liquid.artMean=0;
#ifndef NATIVE_LIQUID_SSR_INPUT
 [branch]if(mud>0)liquid.art=nativeSwampArt(i,gradient,liquid.artMean);
#endif
 liquid.windDirection=i.liquidFlow.xy;liquid.windForce=i.liquidFlow.z;liquid.time=i.liquidFlow.w;
 liquid.pixel=i.p.xy;liquid.edge=liquidTileField(i.terrainVertexFlags,i.liquidTileLocal);liquid.coverage=smoothstep(.25,.5,liquid.edge);
 return shadeLiquidSurface(liquid,ambient,sunDirection,sunColor,skyColor);
}
float3 liquidViewDirection(row_major float4x4 clip,float3 position) {
 // Intersection of the clip X/Y/W planes gives the perspective eye. An
 // orthographic clip has no finite eye, so use its constant backwards ray.
 float3 a=clip[0].xyz,b=clip[1].xyz,c=clip[3].xyz;
 float3 bc=cross(b,c),ca=cross(c,a),ab=cross(a,b);
 float determinant=dot(a,bc);
 if(abs(determinant)>1e-8)
  return (-clip[0].w*bc-clip[1].w*ca-clip[3].w*ab)/determinant-position;
 return -ab*(dot(ab,clip[2].xyz)<0?-1:1);
}
float4 nativeQuaternionMultiply(float4 a,float4 b){return float4(a.x*b.x-dot(a.yzw,b.yzw),a.x*b.yzw+b.x*a.yzw+cross(a.yzw,b.yzw));}
float3 nativeQuaternionRotate(float4 q,float3 v){return nativeQuaternionMultiply(nativeQuaternionMultiply(q,float4(0,v)),float4(q.x,-q.yzw)).yzw;}
float4x4 nativeNodeMatrix(uint node){NativePoseNode value=nativePoseNodes[node];return float4x4(value.row0,value.row1,value.row2,float4(0,0,0,1));}
[numthreads(64,1,1)]void nativePoseCS(uint3 group:SV_GroupID,uint lane:SV_GroupIndex){
 uint work=group.x+group.y*65535u;
 if(work>=nativeWorkCount)return;
 NativePose pose=nativePoses[nativeWorkBase+work];uint4 header=nativeAnimation.Load4(pose.asset);
 uint levelNodes=pose.asset+nativeAnimation.Load(pose.asset+16);
 bool rigid=nativeAnimation.Load(pose.asset+20)!=0;
 for(uint level=0;level<header.y;++level){
  uint2 interval=nativeAnimation.Load2(pose.asset+header.w+level*4);
  for(uint item=interval.x+lane;item<interval.y;item+=64){
   uint node=nativeAnimation.Load(levelNodes+item*4);
   uint address=pose.asset+header.z+node*48;
   uint4 a=nativeAnimation.Load4(address);uint4 b=nativeAnimation.Load4(address+16);
   uint3 morph=nativeAnimation.Load3(address+32);
   NativePoseNode result=(NativePoseNode)0;result.rotation=float4(1,0,0,0);
   if(morph.y!=0){uint first=min(pose.frame,morph.y-1),second=min(first+1,morph.y-1);
    result.morphFirst=pose.asset+morph.x+first*morph.z*12;result.morphSecond=pose.asset+morph.x+second*morph.z*12;
    result.morphCount=morph.z;result.morphFraction=first==second?0:pose.fraction;
   }
   if(b.x!=0){
    uint frame=min(pose.frame,b.x-1),next=min(frame+1,b.x-1);
    float4 first=asfloat(nativeAnimation.Load4(pose.asset+a.w+frame*16));
    result.rotation=first;
    if(frame!=next){
     float4 second=asfloat(nativeAnimation.Load4(pose.asset+a.w+next*16));
     float product=dot(first,second);if(product<0){second=-second;product=-product;}
     float left=1-pose.fraction,right=pose.fraction;
     if(1.0-product>1e-4){
      float angle=acos(product),sine=sin(angle);
      left=sin((1-pose.fraction)*angle)/sine;
      right=sin(pose.fraction*angle)/sine;
     }
     result.rotation=first*left+second*right;
    }
   }
   float3 translation=0;
   if(a.x==0xffffffff&&!rigid){
    if(b.z!=0){uint frame=min(pose.frame,b.z-1),next=min(frame+1,b.z-1);
     float3 first=asfloat(nativeAnimation.Load3(pose.asset+b.y+frame*12));
     float3 second=asfloat(nativeAnimation.Load3(pose.asset+b.y+next*12));
     translation=lerp(first,second,frame==next?0:pose.fraction);
    }
    translation=translation*pose.heightScale-pose.centerShift;
   }else{
    float3 bone=0;
    for(uint endpoint=0;endpoint<a.z;++endpoint){
     float3 weights=float3((endpoint&1)?pose.shape.x:1-pose.shape.x,
      (endpoint&2)?pose.shape.y:1-pose.shape.y,(endpoint&4)?pose.shape.z:1-pose.shape.z);
     bone+=asfloat(nativeAnimation.Load3(pose.asset+a.y+endpoint*12))*(a.z==1?1:weights.x*weights.y*weights.z);
    }
    if(a.x==0xffffffff)translation=bone;
    else{NativePoseNode parent=nativePoseOutput[pose.outputBase+a.x];
     translation=parent.translation.xyz+nativeQuaternionRotate(parent.rotation,bone);
     result.rotation=nativeQuaternionMultiply(result.rotation,parent.rotation);
    }
   }
   result.translation=float4(translation,0);
   float3 x=mul(pose.rootToWorld,float4(nativeQuaternionRotate(result.rotation,float3(1,0,0)),0)).xyz;
   float3 y=mul(pose.rootToWorld,float4(nativeQuaternionRotate(result.rotation,float3(0,1,0)),0)).xyz;
   float3 z=mul(pose.rootToWorld,float4(nativeQuaternionRotate(result.rotation,float3(0,0,1)),0)).xyz;
   float3 position=mul(pose.rootToWorld,float4(translation,1)).xyz;
   result.row0=float4(x.x,y.x,z.x,position.x);result.row1=float4(x.y,y.y,z.y,position.y);result.row2=float4(x.z,y.z,z.z,position.z);
   result.generation=pose.generation;
   nativePoseOutput[pose.outputBase+node]=result;
  }
  DeviceMemoryBarrierWithGroupSync();
 }
}
)" R"(
float4 unpackColor(uint value){return float4((value>>16)&255,(value>>8)&255,value&255,(value>>24)&255)/255.0;}
float4 loadPacked(uint byteBase,uint flatIndex){uint groupBase=byteBase+(flatIndex>>2)*64;uint lane=(flatIndex&3)*4;return float4(asfloat(nativeGeometry.Load(groupBase+lane)),asfloat(nativeGeometry.Load(groupBase+16+lane)),asfloat(nativeGeometry.Load(groupBase+32+lane)),asfloat(nativeGeometry.Load(groupBase+48+lane)));}
float3 loadMorph(NativeInstance instance,uint flatIndex){uint base=instance.morphOffset+flatIndex*12;return float3(asfloat(nativeDynamic.Load(base)),asfloat(nativeDynamic.Load(base+4)),asfloat(nativeDynamic.Load(base+8)));}
NativeTerrainVertex loadTerrainVertex(NativeInstance instance,uint flatIndex){uint base=instance.terrainVertexOffset+flatIndex*120;NativeTerrainVertex value;value.noise=asfloat(nativeGeometry.Load(base));value.windMultiplier=asfloat(nativeGeometry.Load(base+4));value.objectColor=asfloat(nativeGeometry.Load3(base+8));value.depth=asfloat(nativeGeometry.Load(base+20));value.flags=nativeGeometry.Load(base+24);value.animationIndex=nativeGeometry.Load(base+28);value.animationBaseTile=nativeGeometry.Load(base+32);value.gridIndex=nativeGeometry.Load(base+36);value.opacity=asfloat(nativeGeometry.Load(base+40));value.emissive=asfloat(nativeGeometry.Load3(base+44));value.liquidDepth=asfloat(nativeGeometry.Load(base+56));value.liquidMultiplier=asfloat(nativeGeometry.Load(base+60));value.liquidUvBounds=asfloat(nativeGeometry.Load4(base+64));value.liquidBend=asfloat(nativeGeometry.Load(base+80));value.liquidCascade=asfloat(nativeGeometry.Load(base+84));value.liquidCurrent=asfloat(nativeGeometry.Load2(base+88));value.liquidSelfIllumination=asfloat(nativeGeometry.Load(base+96));value.liquidBedEmission=asfloat(nativeGeometry.Load(base+100));value.slopeNormal=nativeGeometry.Load(base+104);value.shiftObjects=nativeGeometry.Load(base+108);value.liquidDrop=asfloat(nativeGeometry.Load(base+112));value.liquidFill=asfloat(nativeGeometry.Load(base+116));return value;}
// Live per-object layer shifts (game water levels), set each frame by the
// terrain owner. Neighbour maps keep their authored rest layers.
StructuredBuffer<float> nativeTerrainLayerShifts:register(t19);
float terrainLayerShift(NativeInstance instance,uint object){return object==0xffffu||(instance.nativeFlags&@NEIGHBOUR_FLAG@)!=0?0:nativeTerrainLayerShifts[object];}
float3x3 inverseObjectLinear(float4x4 objectMatrix){float3 row0=objectMatrix[0].xyz,row1=objectMatrix[1].xyz,row2=objectMatrix[2].xyz;float3 column0=cross(row1,row2),column1=cross(row2,row0),column2=cross(row0,row1);float determinant=dot(row0,column0);float safeDeterminant=abs(determinant)>1e-12?determinant:(determinant<0?-1e-12:1e-12);column0/=safeDeterminant;column1/=safeDeterminant;column2/=safeDeterminant;return float3x3(float3(column0.x,column1.x,column2.x),float3(column0.y,column1.y,column2.y),float3(column0.z,column1.z,column2.z));}
float4 calculateWorldLighting(NativeInstance instance,float3 normal,float3 emissive,float opacity){float3x3 worldToObject=inverseObjectLinear(instance.objectToWorld);float3 sunlightVector=mul(worldToObject,nativeWorldSunDirection.xyz);float lengthSquared=dot(sunlightVector,sunlightVector);float3 direction=sunlightVector*rsqrt(max(lengthSquared,1e-12));float facing=lengthSquared>1e-12?((instance.terrainFlags&8)!=0?1:max(dot(normal,direction),0)):0;float3 color=max(nativeWorldAmbient.rgb,nativeWorldSunDiffuse.rgb*instance.nativeMaterialDiffuse*facing);return float4(saturate(color+emissive),opacity);}
// Neighbour terrain and decorations take their own piece's band (raw edges in the
// instance), giving way wherever another rendered piece adjoins.
float terrainFogFactor(NativeInstance instance,float2 worldPosition){if(instance.terrainFogEnabled==0||instance.terrainFogDistance<=0)return 1;float4 piece=float4(instance.terrainFogMin,instance.terrainFogMax);float density=(instance.nativeFlags&@NEIGHBOUR_FLAG@)!=0?nativeNeighbourPieceFogDensity(worldPosition,piece,instance.terrainFogDistance,nativeNeighbourSeam,nativeNeighbourCurve,uint(nativeNeighbourPieceInfo.x)):borderFogDensity(worldPosition,piece,instance.terrainFogDistance,nativeNeighbourSeam,nativeNeighbourCurve,nativeNeighbourSpanLo,nativeNeighbourSpanHi);float fog=floor(density*255)/255;return 1-fog;}
NativePreparedVertex prepareNativeVertex(NativeInstance instance,uint componentAddress){NativePreparedVertex o;uint4 component=nativeGeometry.Load4(componentAddress);float4 position=loadPacked(instance.geometryOffsets.x,component.x);float4 normal=loadPacked(instance.geometryOffsets.y,component.y);if(component.x<instance.morphCount)position.xyz+=loadMorph(instance,component.x);float2 originalWorldPosition=0;NativeTerrainVertex terrain=(NativeTerrainVertex)0;bool terrainVertex=instance.terrainVertexOffset!=0xffffffff;if(terrainVertex){originalWorldPosition=mul(instance.objectToWorld,float4(position.xyz,1)).xy;terrain=loadTerrainVertex(instance,component.x);{float layerShift=terrainLayerShift(instance,terrain.shiftObjects&0xffffu);position.z+=layerShift;terrain.depth+=layerShift;}if((instance.terrainFlags&1)!=0&&(terrain.flags&1)!=0){float phase=(position.x*instance.terrainWindDirection.x+position.y*instance.terrainWindDirection.y+instance.terrainWindPhase*terrain.windMultiplier+terrain.noise);float settle=1-smoothstep(.015,.08,length(terrain.liquidCurrent));float wave=sin(phase)*instance.terrainWindForce*terrain.windMultiplier*settle;if((terrain.flags&2)!=0&&terrain.depth<0){position.xy+=wave.xx;}else{uint gridX=(terrain.gridIndex&65535u)%33;uint gridY=(terrain.gridIndex&65535u)/33;position.z+=wave*0.25;position.x+=sin(gridX*0.7853981634+instance.terrainPreciseTime*0.05)*instance.terrainWindForce*terrain.windMultiplier*settle*3;position.y+=sin(gridY*0.7853981634+instance.terrainPreciseTime*0.05)*instance.terrainWindForce*terrain.windMultiplier*settle*3;}}}o.p=mul(instance.objectToClip,float4(position.xyz,1));if(instance.lightingEnabled!=0){float3 emissive=terrainVertex?terrain.emissive:instance.nativeEmissive.rgb;float opacity=terrainVertex?terrain.opacity:instance.nativeMaterialAlpha;o.d=calculateWorldLighting(instance,normalize(normal.xyz),emissive,opacity);o.s=terrainVertex?float4(0,0,0,terrainFogFactor(instance,originalWorldPosition)):instance.nativeSpecular;if(!terrainVertex)o.s.a=(instance.nativeDefaultSpecular>>24)/255.0;}else{uint diffuse=(instance.nativeFlags&4)!=0||instance.colorOffsets.x==0xffffffff?instance.nativeDefaultDiffuse:nativeGeometry.Load(instance.colorOffsets.x+component.y*4);uint specular=(instance.nativeFlags&4)!=0||instance.colorOffsets.y==0xffffffff?instance.nativeDefaultSpecular:nativeGeometry.Load(instance.colorOffsets.y+component.y*4);o.d=unpackColor(diffuse);o.s=unpackColor(specular);}if((instance.nativeFlags&@PREVIEW_SPECULAR_RGB_FLAG@)!=0)o.s.rgb=unpackColor(instance.nativeDefaultSpecular).rgb;if(terrainVertex&&(instance.terrainFlags&1)!=0&&(terrain.flags&1)!=0){float phase=(position.x*instance.terrainWindDirection.x+position.y*instance.terrainWindDirection.y+instance.terrainWindPhase*terrain.windMultiplier+terrain.noise);float wave=max(sin(phase),0);o.s.rgb=saturate(terrain.objectColor*terrain.windMultiplier*instance.terrainWindForceNormalized*(50.0/255.0)*wave);}o.uv=asfloat(nativeGeometry.Load2(instance.geometryOffsets.z+component.z*8))+instance.nativeUvShift;if(terrainVertex&&(instance.terrainFlags&2)!=0&&(terrain.flags&4)!=0&&terrain.animationIndex<instance.terrainPhaseCount&&instance.terrainTilesPerSide!=0){uint phase=((instance.terrainFlags&4)!=0?nativeAnimation.Load(instance.terrainPhaseOffset+terrain.animationIndex*4):nativeDynamic.Load(instance.terrainPhaseOffset+terrain.animationIndex*4));uint baseTile=terrain.animationBaseTile;uint currentTile=baseTile+phase;float side=(float)instance.terrainTilesPerSide;o.uv+=float2(((int)(currentTile%instance.terrainTilesPerSide)-(int)(baseTile%instance.terrainTilesPerSide))/side,-((int)(currentTile/instance.terrainTilesPerSide)-(int)(baseTile/instance.terrainTilesPerSide))/side);}o.textureReference=uint2(instance.nativePadding.x,instance.nativePadding.y);return o;}
bool drawVisible(NativeDraw draw,NativeInstance instance){float4x4 cull=(draw.instanceIndex&0x80000000u)!=0?nativeCullWorldClip:instance.objectToClip;uint outside=63u;bool front=true;float2 lo=1e30,hi=-1e30;float nearest=1,nearestW=1e30;[unroll]for(uint corner=0;corner<8;corner++){float3 p=float3((corner&1)?draw.boundsMax.x:draw.boundsMin.x,(corner&2)?draw.boundsMax.y:draw.boundsMin.y,(corner&4)?draw.boundsMax.z:draw.boundsMin.z);float4 clip=mul(cull,float4(p,1));uint mask=0;if(clip.x < -clip.w)mask|=1;if(clip.x > clip.w)mask|=2;if(clip.y < -clip.w)mask|=4;if(clip.y > clip.w)mask|=8;if(clip.z < 0)mask|=16;if(clip.z > clip.w)mask|=32;outside&=mask;nearestW=min(nearestW,clip.w);if(clip.w<=1e-5)front=false;else{float2 ndc=clip.xy/clip.w;lo=min(lo,ndc);hi=max(hi,ndc);nearest=min(nearest,clip.z/clip.w);}}if(outside!=0)return false;
 // Draw distance (draw_distance, or nearer where fog fully covers): the nearest
 // corner's clip w is the box's least planar view depth, never more than its
 // eye distance, so a culled box lies wholly where the fog fade is complete.
 if(front&&nativeDistanceCull.x>0&&nearestW>nativeDistanceCull.x)return false;
 // A box wholly in front of the camera that covers less than the threshold
 // fraction of the screen height contributes nothing visible (resolution
 // independent: a fixed pixel count is nothing at 4K).
 if(front&&nativeCullParams.z>0){float2 size=(hi-lo)*.5;size.x*=nativeCullParams.x/max(nativeCullParams.y,1.0);if(max(size.x,size.y)<nativeCullParams.z)return false;}
 // Occlusion: pick the pyramid level where the box spans at most 2x2 texels;
 // hidden only when the farthest occluder there is nearer than the box.
 if(front&&nativeHiZParams.x>0){
  float2 uv0=saturate(float2(lo.x,-hi.y)*.5+.5),uv1=saturate(float2(hi.x,-lo.y)*.5+.5);
  float2 size=nativeHiZParams.yz;float2 p0=uv0*size,p1=uv1*size;float2 extent=max(p1-p0,1e-4);
  uint mip=(uint)clamp(ceil(log2(max(extent.x,extent.y))),0.0,nativeHiZParams.w-1);
  float scale=exp2((float)mip);int2 last=max(int2(size)>>mip,int2(1,1))-1;
  int2 t0=clamp(int2(p0/scale),int2(0,0),last),t1=clamp(int2(p1/scale),int2(0,0),last);
  float farthest=max(max(nativeHiZ.Load(int3(t0,mip)),nativeHiZ.Load(int3(t1.x,t0.y,mip))),
                     max(nativeHiZ.Load(int3(t0.x,t1.y,mip)),nativeHiZ.Load(int3(t1,mip))));
  if(nearest>farthest)return false;
 }
 return true;}
)" R"(
// 0 on a seam an accepted neighbour touches (within its touched run), 1 from
// 4 m inside; 1 everywhere without neighbours (seams sit at +-1e30).
float neighbourSeamWaveWeight(float2 p){
 float4 inside=float4(p-nativeNeighbourSeam.xy,nativeNeighbourSeam.zw-p);
 float4 along=p.yxyx;
 float4 touched=(float4)(nativeNeighbourSpanHi>nativeNeighbourSpanLo)*step(nativeNeighbourSpanLo,along)*step(along,nativeNeighbourSpanHi);
 float4 w=lerp(1,saturate(inside/4),touched);
 return min(min(w.x,w.y),min(w.z,w.w));
}
NativeWorldVertex prepareNativeWorldVertex(NativeInstance instance,uint componentAddress){
 NativeWorldVertex output=(NativeWorldVertex)0;
 uint4 component=nativeGeometry.Load4(componentAddress);
 // Merged characters (flag 0x40): each component names its part's node and
 // position base; the part animates exactly as its own record would.
 uint partNode=0,positionBase=0;
 if((instance.nativeFlags&0x40u)!=0){partNode=component.w&0xffffu;positionBase=component.w>>16;}
 if(instance.poseNode!=0xffffffff)instance.objectToWorld=nativeNodeMatrix(instance.poseNode+partNode);
 float3 position=0;
 if(instance.shapeCount==8){
  [unroll]for(uint shape=0;shape<8;shape++){
   float3 endpoint=float3((shape&1)!=0?instance.shape.x:1-instance.shape.x,
                         (shape&2)!=0?instance.shape.y:1-instance.shape.y,
                         (shape&4)!=0?instance.shape.z:1-instance.shape.z);
   position+=loadPacked(instance.geometryOffsets.x+shape*instance.positionGroups*64,component.x).xyz*
             endpoint.x*endpoint.y*endpoint.z;
  }
 }else position=loadPacked(instance.geometryOffsets.x,component.x).xyz;
 if(instance.poseNode!=0xffffffff){
  NativePoseNode node=nativePoseNodes[instance.poseNode+partNode];
  uint local=component.x-positionBase;
  if(local<node.morphCount){
   float3 first=asfloat(nativeAnimation.Load3(node.morphFirst+local*12));
   float3 second=asfloat(nativeAnimation.Load3(node.morphSecond+local*12));
   position+=lerp(first,second,node.morphFraction);
  }
 }
 output.undeformedXY=mul(instance.objectToWorld,float4(position,1)).xy;
 if(instance.terrainVertexOffset!=0xffffffff){
  NativeTerrainVertex terrain=loadTerrainVertex(instance,component.x);
  // Layer vertices decode at rest; the live shift lifts them here, before waves.
  const float layerShift=terrainLayerShift(instance,terrain.shiftObjects&0xffffu);
  position.z+=layerShift;terrain.depth+=layerShift;
  if((instance.terrainFlags&1)!=0&&(terrain.flags&1)!=0){
   // Neighbour maps keep their authored rest layers; along a touched seam
   // the active map's waves fade out over the last metres, so both maps'
   // liquids meet at the shared authored edge heights without a step or gap.
   instance.terrainWindForce*=neighbourSeamWaveWeight(output.undeformedXY);
   // Running water does not heave with the game's waves (liquidRiverOnset):
   // a river sheet bobbing up and down read as jelly; its motion is the flow.
   instance.terrainWindForce*=1-smoothstep(.015,.08,length(terrain.liquidCurrent));
   float phase=dot(position.xy,instance.terrainWindDirection)+instance.terrainWindPhase*terrain.windMultiplier+terrain.noise;
   float wave=sin(phase)*instance.terrainWindForce*terrain.windMultiplier;
   if((terrain.flags&2)!=0&&terrain.depth<0)position.xy+=wave.xx;
   else{
    position.z+=wave*.25;
    position.x+=sin(((terrain.gridIndex&65535u)%33)*.7853981634+instance.terrainPreciseTime*.05)*instance.terrainWindForce*terrain.windMultiplier*3;
    position.y+=sin(((terrain.gridIndex&65535u)/33)*.7853981634+instance.terrainPreciseTime*.05)*instance.terrainWindForce*terrain.windMultiplier*3;
   }
   output.normal.w=max(sin(dot(position.xy,instance.terrainWindDirection)+
                     instance.terrainWindPhase*terrain.windMultiplier+terrain.noise),0);
  }
 }
 output.p=mul(instance.objectToWorld,float4(position,1));
 float3 normal=loadPacked(instance.geometryOffsets.y,component.y).xyz;
 output.normal.xyz=normalize(mul(transpose(inverseObjectLinear(instance.objectToWorld)),normal));
 output.uv=asfloat(nativeGeometry.Load2(instance.geometryOffsets.z+component.z*8));
 output.diffuse=instance.colorOffsets.x==0xffffffff?0xffffffff:nativeGeometry.Load(instance.colorOffsets.x+component.y*4);
 output.specular=instance.colorOffsets.y==0xffffffff?0xff000000:nativeGeometry.Load(instance.colorOffsets.y+component.y*4);
 output.sourcePosition=component.x;output.partNode=partNode;
 return output;
}
// Object-space records (worldGeometry bit 1, static_lod.h): the world vertex of a
// shared mesh, evaluated per vertex from the record's transform instead of
// read from a per-record prepared copy. A shape-resolved mesh (shapeCount 1,
// the CPU's selection) reads its position; an 8-shape mesh repeats the CPU's
// selection (retained_static_submission.cpp select_endpoint: nested
// a+(b-a)*t, x then y then z). Precise, so every pass that draws the record
// computes the same position.
float3 nativeShapeBlend(float3 a,float3 b,float t){precise float3 r=a+(b-a)*t;return r;}
@IMPOSTOR_FRAMES@
// Crown impostor quad corner (static_lod.h): a camera-facing square over the
// mesh's bounding sphere (64 bytes before its components), and the corner's
// image position in the four atlas frames nearest the view (bilinear over the
// hemi-octahedral grid, weights per record). Object-space records only.
// The quad's centre, half size and view direction (world), shared by the
// corner placement and the frame images.
void nativeImpostorFrame(NativeInstance instance,out float4 sphere,out float3 centre,out float scale,out float3 view){
 sphere=asfloat(nativeGeometry.Load4(instance.geometryOffsets.w-64));
 centre=mul(instance.objectToWorld,float4(sphere.xyz,1)).xyz;
 float3x3 m=(float3x3)instance.objectToWorld;
 scale=max(max(length(float3(m[0][0],m[1][0],m[2][0])),length(float3(m[0][1],m[1][1],m[2][1]))),
           length(float3(m[0][2],m[1][2],m[2][2])));
 float3 toEye=liquidViewDirection(instance.objectToClip,centre);
 view=dot(toEye,toEye)>1e-12?normalize(toEye):float3(0,0,1);
}
// The corner's image position in the four atlas frames nearest the view
// (bilinear over the hemi-octahedral grid; the weights are per record). Run
// at the vertex stage's end, written straight into the output: FXC placed
// these values in output registers when they came back from the corner
// function, and the shared return path then overwrote them with the liquid
// fields' defaults (zero weights and positions reached the pixel stage).
void nativeImpostorImages(NativeInstance instance,float3 p,out float4 frames01,out float4 frames23,
                          out float4 weights,out uint frameBase){
 float4 sphere;float3 centre,view;float scale;nativeImpostorFrame(instance,sphere,centre,scale,view);
 float3x3 inverse=inverseObjectLinear(instance.objectToWorld);
 float3 local=mul(inverse,view);local=dot(local,local)>1e-12?normalize(local):float3(0,0,1);
 float2 grid=nativeImpostorGrid(local)*(kImpostorFrames-1);
 uint2 base=min((uint2)floor(grid),uint2(kImpostorFrames-2,kImpostorFrames-2));
 float2 t=saturate(grid-base);
 weights=float4((1-t.x)*(1-t.y),t.x*(1-t.y),(1-t.x)*t.y,t.x*t.y);
 float3 q=mul(inverse,p-centre)/max(sphere.w,1e-6);
 float2 uv[4];
 [unroll]for(uint k=0;k<4;++k){
  float3 r,u;nativeImpostorBasis(nativeImpostorFrameDirection(base+uint2(k&1u,k>>1)),r,u);
  uv[k]=float2(dot(q,r),-dot(q,u))*kImpostorMargin*.5+.5;
 }
 frames01=float4(uv[0],uv[1]);frames23=float4(uv[2],uv[3]);frameBase=base.x|(base.y<<8);
}
NativeWorldVertex nativeImpostorVertex(NativeInstance instance,uint corner){
 NativeWorldVertex output=(NativeWorldVertex)0;
 float4 sphere;float3 centre,view;float scale;nativeImpostorFrame(instance,sphere,centre,scale,view);
 float3 right,up;nativeImpostorBasis(view,right,up);
 float2 c=float2((corner&1u)!=0?1:-1,(corner&2u)!=0?1:-1);
 // Precise: the prepass and the material pass place the quad identically
 // (the material pass tests depth for equality).
 precise float3 p=centre+(c.x*right+c.y*up)*(sphere.w*scale);
 output.p=float4(p,1);output.normal=float4(view,0);output.undeformedXY=p.xy;
 output.diffuse=0xffffffff;output.specular=0xff000000;
 return output;
}
NativeWorldVertex nativeObjectSpaceVertex(NativeInstance instance,uint componentIndex){
 NativeWorldVertex output=(NativeWorldVertex)0;
 uint4 component=nativeGeometry.Load4(instance.geometryOffsets.w+componentIndex*16);
 precise float3 position;
 // A shape-resolved mesh (one shape, the CPU's selection) reads its position.
 [branch]if(instance.shapeCount!=8)position=loadPacked(instance.geometryOffsets.x,component.x).xyz;
 else{
  uint stride=instance.positionGroups*64;
  float3 v[8];
  [unroll]for(uint shape=0;shape<8;shape++)v[shape]=loadPacked(instance.geometryOffsets.x+shape*stride,component.x).xyz;
  float3 t=instance.shape;
  float3 low=nativeShapeBlend(nativeShapeBlend(v[0],v[1],t.x),nativeShapeBlend(v[2],v[3],t.x),t.y);
  float3 high=nativeShapeBlend(nativeShapeBlend(v[4],v[5],t.x),nativeShapeBlend(v[6],v[7],t.x),t.y);
  position=nativeShapeBlend(low,high,t.z);
 }
 precise float4 world=mul(instance.objectToWorld,float4(position,1));
 output.p=world;output.undeformedXY=world.xy;
 float3 normal=loadPacked(instance.geometryOffsets.y,component.y).xyz;
 output.normal.xyz=normalize(mul(transpose(inverseObjectLinear(instance.objectToWorld)),normal));
 output.uv=asfloat(nativeGeometry.Load2(instance.geometryOffsets.z+component.z*8));
 output.diffuse=instance.colorOffsets.x==0xffffffff?0xffffffff:nativeGeometry.Load(instance.colorOffsets.x+component.y*4);
 output.specular=instance.colorOffsets.y==0xffffffff?0xff000000:nativeGeometry.Load(instance.colorOffsets.y+component.y*4);
 output.sourcePosition=component.x;
 return output;
}

)" R"(
groupshared uint nativeGroupVisible;
groupshared uint nativeGroupPrepare;
groupshared uint nativeShadowGroupBase; // also nativeBuildCS's first (flat) range
groupshared uint nativeBuildSteepBase;
// Static LOD (static_lod.h, worldGeometry bit 2): the record's mesh holds a
// 64-byte table before its full index list, {levels} then per reduced level
// {index offset, triangles, error (object units)}, errors not decreasing.
// The coarsest level whose error projects within the pixel limit
// (nativeDistanceCull.z) at the record's centre is drawn. Its successor fades
// in while the successor's error projects between the limit and
// limit * (1 + band) (nativeDistanceCull.w): both levels are emitted, with
// complementary halves of a per-pixel dither (fade: 512 | coverage 0-255 of
// the successor), the same in the prepass and the material pass. Stateless
// and continuous with distance, so a level never pops.
// Crown impostors (static_lod.h native_world_impostor): a cut-out object-space
// record whose bounding sphere (64 bytes before its components) projects
// below the impostor size (nativeImpostorParams.x pixels across) draws its
// impostor quad instead of its triangles; up to (1 + band) times that size
// both draw, with complementary halves of the dither (as the static LOD).
void nativeImpostorSelect(NativeDraw draw,NativeInstance instance,inout uint firstCount,out uint secondCount,
                          inout uint fade,out bool impostorSecond){
 secondCount=0;impostorSecond=false;
 float4 sphere=asfloat(nativeGeometry.Load4(draw.componentOffset-64));
 float3 centre=mul(instance.objectToWorld,float4(sphere.xyz,1)).xyz;
 float w=dot(instance.objectToClip[3],float4(centre,1));
 if(w<=1e-3)return;
 float3x3 m=(float3x3)instance.objectToWorld;
 float scale=max(max(length(float3(m[0][0],m[1][0],m[2][0])),length(float3(m[0][1],m[1][1],m[2][1]))),
                 length(float3(m[0][2],m[1][2],m[2][2])));
 float size=2*sphere.w*scale*length(instance.objectToClip[1].xyz)*nativeCullParams.y*.5/w;
 float limit=nativeImpostorParams.x,fadeEnd=limit*(1+nativeImpostorParams.y);
 if(size>=fadeEnd)return;
 impostorSecond=true;secondCount=2;
 if(size<=limit){firstCount=0;fade=0;return;}
 uint coverage=(uint)round(saturate((fadeEnd-size)/max(fadeEnd-limit,1e-6))*255);
 if(coverage==0){impostorSecond=false;secondCount=0;return;}
 fade=512u|coverage;
}
void nativeStaticLodSelect(NativeDraw draw,NativeInstance instance,inout uint firstOffset,inout uint firstCount,
                           out uint secondOffset,out uint secondCount,out uint fade){
 secondOffset=0;secondCount=0;fade=0;
 uint table=draw.indexOffset-64;
 uint levels=min(nativeGeometry.Load(table),3u);
 float3 center=(draw.boundsMin.xyz+draw.boundsMax.xyz)*.5;
 float w=dot(instance.objectToClip[3],float4(center,1));
 if(levels==0||w<=1e-3)return;
 float3x3 m=(float3x3)instance.objectToWorld;
 float scale=max(max(length(float3(m[0][0],m[1][0],m[2][0])),length(float3(m[0][1],m[1][1],m[2][1]))),
                 length(float3(m[0][2],m[1][2],m[2][2])));
 // Screen pixels per object unit at the centre (half the target height per
 // unit of clip y over w).
 float pixels=scale*length(instance.objectToClip[1].xyz)*nativeCullParams.y*.5/w;
 float limit=nativeDistanceCull.z,fadeEnd=limit*(1+nativeDistanceCull.w);
 uint chosen=0;float next=0;
 [loop]for(uint level=1;level<=levels;++level){
  float projected=asfloat(nativeGeometry.Load(table+level*16+8))*pixels;
  if(projected>limit){next=projected;break;}
  chosen=level;
 }
 if(chosen!=0){uint2 entry=nativeGeometry.Load2(table+chosen*16);firstOffset=entry.x;firstCount=entry.y;}
 if(chosen<levels&&next<fadeEnd){
  uint coverage=(uint)round(saturate((fadeEnd-next)/max(fadeEnd-limit,1e-6))*255);
  if(coverage!=0){uint2 entry=nativeGeometry.Load2(table+(chosen+1)*16);secondOffset=entry.x;secondCount=entry.y;fade=512u|coverage;}
 }
}
void nativeCurrentBounds(inout NativeDraw draw,NativeInstance instance){
 if(instance.poseNode==0xffffffffu)return;
 NativePoseNode node=nativePoseNodes[instance.poseNode];
 float3 center=float3(node.row0.w,node.row1.w,node.row2.w);
 // Merged groups carry twice the already-scaled world selection radius.
 // Individual parts carry a node-local radius and still need its linear scale.
 float3 radiusScale=(instance.nativeFlags&0x40u)!=0?float3(1,1,1):
  float3(length(node.row0.xyz),length(node.row1.xyz),length(node.row2.xyz));
 float3 extent=draw.geometryRadius*radiusScale;
 // The immutable radius was rounded outwards on the CPU. Account for the
 // matrix/length operations here without a scene-distance or actor exception.
 extent+=8*1.192092896e-7*(abs(center)+extent+1);
 draw.boundsMin=float4(center-extent,1);draw.boundsMax=float4(center+extent,1);
}
// pancaked: a caster drawn without depth clipping (neighbour-map decorations,
// flush_native_shadows) is kept when it lies wholly in front of the near
// plane; it lands on it and still occludes everything behind.
bool nativeLightBoundsVisibleFrom(NativeDraw draw,uint slice,bool pancaked){
 uint outside=63;float2 lo=1e30,hi=-1e30;
 [unroll]for(uint corner=0;corner<8;corner++){
  float3 p=float3((corner&1)?draw.boundsMax.x:draw.boundsMin.x,(corner&2)?draw.boundsMax.y:draw.boundsMin.y,(corner&4)?draw.boundsMax.z:draw.boundsMin.z);
  float4 clip=mul(shadowLight[slice],float4(p,1));
  uint bits=0;if(clip.x < -clip.w)bits|=1;if(clip.x > clip.w)bits|=2;
  if(clip.y < -clip.w)bits|=4;if(clip.y > clip.w)bits|=8;
  if(clip.z < 0&&!pancaked)bits|=16;if(clip.z > clip.w)bits|=32;outside&=bits;
  lo=min(lo,clip.xy);hi=max(hi,clip.xy);
 }
 // Sun cascades are orthographic (last row 0,0,0,1): a caster covering less
 // than the texel threshold cannot change the map.
 const float4 last=float4(shadowLight[slice][3]);
 if(outside==0&&nativeCullParams.w>0&&all(abs(last-float4(0,0,0,1))<1e-5)){
  const float side=2.0/max(length(shadowLight[slice][0].xyz)*shadowBias[slice].x,1e-12);
  const float2 texels=(hi-lo)*.5*side;
  if(max(texels.x,texels.y)<nativeCullParams.w)return false;
 }
 return outside==0;
}
bool nativeLightBoundsVisible(NativeDraw draw,uint slice){return nativeLightBoundsVisibleFrom(draw,slice,false);}
uint2 nativePoseGeneration(NativeInstance instance){
 return instance.poseNode==0xffffffffu?uint2(0,0):nativePoseNodes[instance.poseNode].generation;
}
)" R"(
// Foliage wind sway, procedural and in world space (bit layout:
// native_instance_flags.h). Instance flag 0x200 marks
// every part of a swaying plant; bits 10-15 carry its trunk top (1/4 unit),
// 16-21 its trunk radius (1/16 unit), 22-27 its height (1/4 unit) and 28-31
// the part node's distance from the root pose node. All parts evaluate one
// displacement field around that root node, so parts that touch move
// identically and never separate. The field is exactly zero inside the trunk
// (radius and top) and below the root, then grows with height relative to the
// plant's own size, so bushes and reeds sway in proportion. Gust swells travel downwind across the map; each tree has its
// own phase and rate. The terrain frame supplies the game's wind, the world
// clock (segments, ~18/s) and the configured strength (0 = off). The shared
// renderer wind (weather_wind.h) scales the motion on a saturating curve
// (treeSwayWindDrive), deepens the steady lean in stronger gusts
// (treeSwayWindLean) and turns the bend direction; the oscillation rates never
// follow the wind. Fronts keep the map's steady direction so a wandering wind
// never sweeps them across distant trees. With wind variation off it is speed
// 1, gust 0 and the map's direction: the previous motion.
// The plant's root point: a posed record reads its root pose node; an
// object-space record (no pose, no morph) carries it in its morph words
// (renderer.cpp, NativeWorldGeometry::sway_root). Same field, same rule.
bool treeSwayAnchored(NativeInstance instance){
 return instance.poseNode!=0xffffffffu||(instance.worldGeometry&@WORLD_OBJECT_SPACE@)!=0;
}
float3 treeSwayBase(NativeInstance instance){
 [branch]if(instance.poseNode==0xffffffffu)
  return float3(asfloat(instance.morphOffset),asfloat(instance.morphCount),instance.morphFraction);
 NativePoseNode root=nativePoseNodes[instance.poseNode-(instance.nativeFlags>>28)];
 return float3(root.row0.w,root.row1.w,root.row2.w);
}
float treeSwayStrength(NativeInstance instance){
 return (instance.nativeFlags&0x200u)!=0&&treeSwayAnchored(instance)&&nativeTerrainClock.w!=0?
  max(nativeTerrainFogControl.z,0.0):0;
}
float treeSwayDrive(float swell){return treeSwayWindDrive(nativeWeatherWind,swell);}
float treeSwayLean(float swell){return treeSwayWindLean(nativeWeatherWind,swell);}
// Height is already in world units (the producer applies instance Z scale).
// Keep the old full-amplitude threshold, but give saplings at least 75% of
// that motion: more relative flexibility without changing large trees.
float treeSwaySize(float height){return .75+.25*saturate(height/5.2);}
// Size stays <= 1. The bend plus flutter reach is at most
// .06*sqrt(1+.2*.2)+.02*sqrt(1+1+.5*.5) < .1, times strength and wind drive;
// the gust lean (<= 1.3 on the .55 steady share) is bounded by its factor.
float treeSwayReach(NativeInstance instance){return treeSwayStrength(instance)*.1*max(1,treeSwayDrive(1)*treeSwayLean(1));}
float3 treeSwayOffset(float3 position,NativeInstance instance){
 float strength=treeSwayStrength(instance);
 if(strength<=0)return 0;
 uint flags=instance.nativeFlags;
 float3 base=treeSwayBase(instance);
 float top=((flags>>10)&63u)*.25,trunk=((flags>>16)&63u)/16.0,height=max(((flags>>22)&63u)*.25,.25);
 float3 relative=position-base;
 float rise=smoothstep(0,height,max(relative.z,0));
 float clear=max(smoothstep(trunk,trunk+.15+.5*trunk+.08*height,length(relative.xy)),
                 smoothstep(top,top+.2+.15*height,relative.z));
 float weight=rise*rise*clear;
 if(weight<=0)return 0;
 float size=treeSwaySize(height);
 float2 wind=nativeTerrainWind.xy;float windLength=length(wind);
 float2 steady=windLength>1e-6?wind/windLength:float2(.7071068,.7071068);
 float2 along=nativeWeatherWind.direction,across=float2(-along.y,along.x);
 float force=saturate(nativeWeatherWind.force); // map force raised to the wind floor
 float time=nativeTerrainClock.y; // world segments: stops and scales with the game clock
 float seed=frac(sin(dot(floor(base.xy*8),float2(12.9898,78.233)))*43758.5453);
 float phase=seed*6.2831853,rate=.85+.3*frac(seed*7.31);
 // Soft swells ~40 units apart drifting downwind at ~3 units per second.
 float front=dot(base.xy,steady)*.157-time*.026;
 float swell=smoothstep(.35,1.0,.5+.5*sin(front))*(.6+.4*sin(front*.43+dot(base.xy,float2(-steady.y,steady.x))*.05+1.3));
 float drive=treeSwayDrive(swell);
 float motion=.3+.7*swell;
 float bend=motion*(.65*sin(time*.04*rate+phase)+.35*sin(time*.067*rate+phase*1.7));
 float side=motion*sin(time*.05*rate+phase*2.3);
 float3 offset=float3(strength*.06*size*(.35+.65*force)*weight*drive*
  (along*(.55*(.25+.75*swell)*treeSwayLean(swell)+.45*bend)+across*.2*side),0);
 // About a centimetre of slower flutter, part of the same field.
 float3 wobble=float3(sin(time*.3*rate+dot(relative,float3(2.1,1.7,1.3))),
                      sin(time*.37*rate+dot(relative,float3(1.5,2.3,1.9))),
                      .5*sin(time*.27*rate+dot(relative,float3(1.9,1.1,2.4))));
 return offset+strength*.02*size*(.5+.5*force)*weight*motion*lerp(1,drive,.5)*wobble;
}
)" R"(
void nativePrepareWorld(NativeDraw draw,NativeInstance instance,uint lane){
 uint2 pose=nativePoseGeneration(instance);
 if(lane==0){nativeGroupPrepare=0;
  if(draw.worldState!=0xffffffffu){NativeWorldState previous=nativeWorldStateOutput[draw.worldState];
   nativeGroupPrepare=any(previous.geometry!=draw.worldGeneration)||any(previous.pose!=pose);
  }
 }
 GroupMemoryBarrierWithGroupSync();
 if(nativeGroupPrepare==0)return;
 for(uint component=lane;component<draw.componentCount;component+=64)
  nativeWorldOutput[draw.preparedVertexBase+component]=prepareNativeWorldVertex(instance,draw.componentOffset+component*16);
 DeviceMemoryBarrierWithGroupSync();
 if(lane==0){NativeWorldState value=nativeWorldStateOutput[draw.worldState];value.geometry=draw.worldGeneration;value.pose=pose;
#ifdef EID3D11_TESTING
  value.preparations++;value.components+=draw.componentCount;
#endif
  nativeWorldStateOutput[draw.worldState]=value;
 }
}
[numthreads(64,1,1)]void nativeBuildCS(uint3 groupId:SV_GroupID,uint3 threadId:SV_GroupThreadID){
 NativeDraw draw;if(!loadNativeDraw(groupId,draw))return;NativeInstance instance=loadNativeInstance(draw.instanceIndex);
 if(instance.worldGeometry!=0){
  nativeCurrentBounds(draw,instance);
  float swayReach=treeSwayReach(instance);
  draw.boundsMin.xyz-=swayReach;draw.boundsMax.xyz+=swayReach;
  if(threadId.x==0)nativeGroupVisible=draw.colorVisible!=0&&(draw.preservesOrder!=0||drawVisible(draw,instance));
  GroupMemoryBarrierWithGroupSync();if(nativeGroupVisible==0)return;
  if(draw.prepareWorld!=0)nativePrepareWorld(draw,instance,threadId.x);
  if(draw.colorVisible==0)return;
 }else{
  if(threadId.x==0)nativeGroupVisible=draw.preservesOrder!=0||drawVisible(draw,instance);
  GroupMemoryBarrierWithGroupSync();if(nativeGroupVisible==0)return;
  for(uint component=threadId.x;component<draw.componentCount;component+=64)
   nativePreparedOutput[draw.preparedVertexBase+component]=prepareNativeVertex(instance,draw.componentOffset+component*16);
 }
 uint triangleCount=draw.indexCount/3;
 // Terrain tile classes: a split batch collects its draws' steep-class
 // triangles (from steepFirstTriangle on) in its own region and arguments,
 // drawn with the steep pixel variant.
 NativeDrawWork work=nativeDrawWork[nativeWorkBase+groupId.x+groupId.y*65535u];
 bool split=work.steepArgsByteOffset!=0xffffffffu;
 // Static LOD: the record's level (and, in a crossfade band, its successor).
 uint firstOffset=draw.indexOffset,firstCount=triangleCount,secondOffset=0,secondCount=0,fade=0;
 [branch]if((instance.worldGeometry&@WORLD_LOD@)!=0&&nativeDistanceCull.z>0&&draw.preservesOrder==0&&!split)
  nativeStaticLodSelect(draw,instance,firstOffset,firstCount,secondOffset,secondCount,fade);
 // Crown impostors: the record's quad far away, both in the crossfade band.
 bool impostorSecond=false;
 [branch]if((instance.worldGeometry&@WORLD_IMPOSTOR@)!=0&&nativeImpostorParams.x>0&&draw.preservesOrder==0&&!split)
  nativeImpostorSelect(draw,instance,firstCount,secondCount,fade,impostorSecond);
 uint total=firstCount+secondCount;
 // Every triangle of a visible record is emitted, so the group reserves its
 // whole range (and, when split, its steep range) with one atomic each.
 uint flatCount=split?min(draw.steepFirstTriangle,total):total;
 if(threadId.x==0&&draw.preservesOrder==0){
  uint base=0;if(flatCount!=0)nativeIndirectArgs.InterlockedAdd(draw.indirectArgsByteOffset,flatCount*3,base);
  nativeShadowGroupBase=base;
  base=0;if(total>flatCount)nativeIndirectArgs.InterlockedAdd(work.steepArgsByteOffset,(total-flatCount)*3,base);
  nativeBuildSteepBase=base;
 }
 GroupMemoryBarrierWithGroupSync();
 for(uint tri=threadId.x;tri<total;tri+=64){
  bool second=tri>=firstCount;
  uint indexBase=second?secondOffset+(tri-firstCount)*12:firstOffset+tri*12;
  uint3 componentIndex;
  // An impostor's two triangles name its quad corners (nativeImpostorVertex).
  [branch]if(second&&impostorSecond)componentIndex=tri==firstCount?uint3(0,1,2):uint3(2,1,3);
  else componentIndex=uint3(nativeGeometry.Load(indexBase),nativeGeometry.Load(indexBase+4),nativeGeometry.Load(indexBase+8));
  uint outputTriangle;
  if(draw.preservesOrder!=0)outputTriangle=draw.outputTriangleBase+draw.orderedTriangleOffset+tri;
  else if(tri>=flatCount)outputTriangle=work.steepOutputTriangleBase+nativeBuildSteepBase/3+(tri-flatCount);
  else outputTriangle=draw.outputTriangleBase+nativeShadowGroupBase/3+tri;
  // LOD records encode a mesh-relative 16-bit component plus the existing
  // crossfade code. Prepared world ranges can start above 65535: their base
  // lives in secondMorphOffset, independently of these dither bits.
  uint code=(fade!=0?(fade|(second?256u:0u))<<16:0u)|(second&&impostorSecond?1024u<<16:0u);
  uint base=(instance.worldGeometry&(@WORLD_OBJECT_SPACE@|@WORLD_LOD@))!=0?0:draw.preparedVertexBase;
  nativeTriangleOutput[outputTriangle]=uint4(draw.instanceIndex,(base+componentIndex.x)|code,
                                           (base+componentIndex.y)|code,(base+componentIndex.z)|code);
 }
}

// Shadow world preparation is split into chunks of a record's components
// (dispatch z) so a large record -- a merged character -- spreads across many
// groups. Chunks never write the record's prepared state; the commit pass
// after the whole dispatch does, with the same visibility decision.
static const uint kNativeShadowChunk=512;
bool nativeShadowPrepareVisible(inout NativeDraw draw,NativeInstance instance){
 nativeCurrentBounds(draw,instance);
 [unroll]for(uint slice=0;slice<6;slice++)
  if((draw.prepareWorld&(256u<<slice))!=0&&nativeLightBoundsVisible(draw,slice))return true;
 return false;
}
[numthreads(64,1,1)]void nativeShadowPrepareCS(uint3 groupId:SV_GroupID,uint3 threadId:SV_GroupThreadID){
 NativeDraw draw;if(!loadNativeDraw(groupId,draw))return;
 if(draw.prepareWorld==0)return;
 uint first=groupId.z*kNativeShadowChunk;if(first>=draw.componentCount)return;
 NativeInstance instance=loadNativeInstance(draw.instanceIndex);
 if(threadId.x==0){
  nativeGroupVisible=nativeShadowPrepareVisible(draw,instance)?1:0;nativeGroupPrepare=0;
  if(nativeGroupVisible!=0&&draw.worldState!=0xffffffffu){NativeWorldState previous=nativeWorldStateOutput[draw.worldState];
   nativeGroupPrepare=any(previous.geometry!=draw.worldGeneration)||any(previous.pose!=nativePoseGeneration(instance));}
 }
 GroupMemoryBarrierWithGroupSync();if(nativeGroupPrepare==0)return;
 uint last=min(first+kNativeShadowChunk,draw.componentCount);
 for(uint component=first+threadId.x;component<last;component+=64)
  nativeWorldOutput[draw.preparedVertexBase+component]=prepareNativeWorldVertex(instance,draw.componentOffset+component*16);
}
[numthreads(1,1,1)]void nativeShadowCommitCS(uint3 groupId:SV_GroupID){
 NativeDraw draw;if(!loadNativeDraw(groupId,draw))return;
 if(draw.prepareWorld==0||draw.worldState==0xffffffffu)return;
 NativeInstance instance=loadNativeInstance(draw.instanceIndex);
 if(!nativeShadowPrepareVisible(draw,instance))return;
 NativeWorldState value=nativeWorldStateOutput[draw.worldState];
 uint2 pose=nativePoseGeneration(instance);
 if(all(value.geometry==draw.worldGeneration)&&all(value.pose==pose))return;
 value.geometry=draw.worldGeneration;value.pose=pose;
#ifdef EID3D11_TESTING
 value.preparations++;value.components+=draw.componentCount;
#endif
 nativeWorldStateOutput[draw.worldState]=value;
}
// Terrain sun caster bake: this redraw's map mask per work entry (bits 0-4).
StructuredBuffer<uint> nativeShadowWorkMask:register(t35);
// Static LOD in the sun maps (object-space records with a level table): the
// coarsest level whose error stays within one texel of the map, the world
// size of a texel being shadowBias.x. It depends only on the map, never on
// the camera, so a cached static layer stays exact; no crossfade (depth has
// no dither). Cut-out crowns have no levels and their impostor quad faces the
// camera, so a crown casts its own triangles at every distance.
void nativeShadowLodSelect(NativeDraw draw,NativeInstance instance,uint slice,inout uint offset,inout uint triangles){
 uint table=draw.indexOffset-64;
 uint levels=min(nativeGeometry.Load(table),3u);
 float3x3 m=(float3x3)instance.objectToWorld;
 float scale=max(max(length(float3(m[0][0],m[1][0],m[2][0])),length(float3(m[0][1],m[1][1],m[2][1]))),
                 length(float3(m[0][2],m[1][2],m[2][2])));
 float texel=shadowBias[slice].x;
 [loop]for(uint level=1;level<=levels;++level){
  if(asfloat(nativeGeometry.Load(table+level*16+8))*scale>texel)break;
  uint2 entry=nativeGeometry.Load2(table+level*16);offset=entry.x;triangles=entry.y;
 }
}
[numthreads(64,1,1)]void nativeShadowBuildCS(uint3 groupId:SV_GroupID,uint3 threadId:SV_GroupThreadID){
 NativeDraw draw;if(!loadNativeDraw(groupId,draw))return;
 if(shadowScrollControl.z!=0&&(nativeShadowWorkMask[nativeWorkBase+groupId.x+groupId.y*65535u]&(1u<<shadowControl.y))==0)return;
 uint firstTriangle=groupId.z*kNativeShadowChunk;
 if(firstTriangle>=draw.indexCount/3||(draw.colorVisible&(1u<<shadowControl.y))==0)return;
 NativeInstance instance=loadNativeInstance(draw.instanceIndex);
 // Object-space records (neighbour-map decorations) evaluate their vertices
 // in the vertex stage: no prepared world copy to wait for.
 bool objectSpace=(instance.worldGeometry&@WORLD_OBJECT_SPACE@)!=0;
 uint indexOffset=draw.indexOffset,allTriangles=draw.indexCount/3;
 if(objectSpace){
  [branch]if((instance.worldGeometry&@WORLD_LOD@)!=0&&nativeDistanceCull.z>0)
   nativeShadowLodSelect(draw,instance,shadowControl.y,indexOffset,allTriangles);
  if(firstTriangle>=allTriangles)return;
 }else{
  if(draw.worldState==0xffffffffu)return;
  NativeWorldState prepared=nativeWorldStates[draw.worldState];
  if(any(prepared.geometry!=draw.worldGeneration)||any(prepared.pose!=nativePoseGeneration(instance)))return;
 }
 uint triangleCount=min(kNativeShadowChunk,allTriangles-firstTriangle);
 nativeCurrentBounds(draw,instance);
 if(threadId.x==0){
  nativeGroupVisible=nativeLightBoundsVisibleFrom(draw,shadowControl.y,objectSpace);
  // Static caster bake on a scrolled map: only records reaching the exposed
  // strip are drawn (shadow_scroll_strip_reached: inside the new square
  // widened by the inset, not wholly inside the copied square shrunk by it).
  // The extra 1e-4 keeps this a superset of the CPU rule; drawing a caster
  // again over its copied depth is harmless.
  if(nativeGroupVisible!=0&&shadowScrollControl.x!=0){
   float2 lo=1e30,hi=-1e30;
   [unroll]for(uint corner=0;corner<8;corner++){
    float3 p=float3((corner&1)?draw.boundsMax.x:draw.boundsMin.x,(corner&2)?draw.boundsMax.y:draw.boundsMin.y,(corner&4)?draw.boundsMax.z:draw.boundsMin.z);
    float2 q=float2(dot(shadowLight[shadowControl.y][0].xyz,p)+shadowLight[shadowControl.y][0].w,
                    dot(shadowLight[shadowControl.y][1].xyz,p)+shadowLight[shadowControl.y][1].w);
    lo=min(lo,q);hi=max(hi,q);
   }
   const float inset=4.0/float(max(shadowScrollControl.y,1u)),reach=inset+1e-4,keep=inset+1e-4;
   const bool outside=hi.x< -1-reach||lo.x>1+reach||hi.y< -1-reach||lo.y>1+reach;
   const bool copied=lo.x>=shadowScrollCopied.x+keep&&hi.x<=shadowScrollCopied.y-keep&&
                     lo.y>=shadowScrollCopied.z+keep&&hi.y<=shadowScrollCopied.w-keep;
   if(outside||copied)nativeGroupVisible=0;
  }
  // Every triangle survives with its group, so reserve its whole range once.
  if(nativeGroupVisible!=0)
   nativeIndirectArgs.InterlockedAdd(draw.indirectArgsByteOffset,triangleCount*3,nativeShadowGroupBase);
 }
 GroupMemoryBarrierWithGroupSync();if(nativeGroupVisible==0)return;
 for(uint tri=threadId.x;tri<triangleCount;tri+=64){
  uint3 component=nativeGeometry.Load3(indexOffset+(firstTriangle+tri)*12);
  // Retained world LOD keeps the full caster geometry. Use the same
  // relative-index representation as its color command, without dither.
  uint base=(instance.worldGeometry&@WORLD_LOD@)!=0?0:draw.preparedVertexBase;
  nativeTriangleOutput[draw.outputTriangleBase+nativeShadowGroupBase/3+tri]=uint4(draw.instanceIndex,base+component.x,base+component.y,base+component.z);
 }
}

)" R"(
// Units on deformed snow and sand (issue #137): the cliff tessellation sinks
// trail troughs into the ground (nativeTessGroundPosition), while the game
// places a unit at the undeformed height, so units floated above their own
// trough. A unit (instance flag 0x2) lowers whole by the trail height at its
// root: the same field, window, tiles and distance fade (snowTrailRaw) as the
// terrain displacement, 0 wherever the ground is undeformed. Only while that
// displacement runs (a field and cliff tessellation on); the shading-only
// path draws no trough. The root is the pose's rootToWorld origin, recovered
// from the instance's own node (row columns = rootToWorld * R(rotation),
// position = rootToWorld * translation), so every part and attachment of one
// unit samples the same point and the body moves as one. Precise: the depth
// prepass and the material pass place the unit identically. The field's
// copy for the vertex stage: t8 there is nativeWorldVertices.
Texture2D<float2> nativeFigureSnowTrail:register(t36);
float nativeFigureTrailSink(NativeInstance instance){
 [branch]if((instance.nativeFlags&0x2u)==0||instance.poseNode==0xffffffffu||nativeSnowTrailWindow.w<=0||nativeTerrainCliff.y<=0)return 0;
 NativePoseNode node=nativePoseNodes[instance.poseNode];
 float3x3 m=float3x3(node.row0.xyz,node.row1.xyz,node.row2.xyz);
 precise float2 root=float2(node.row0.w,node.row1.w)-mul(m,nativeQuaternionRotate(float4(node.rotation.x,-node.rotation.yzw),node.translation.xyz)).xy;
 precise float sink=snowTrailHeight(snowTrailRaw(nativeFigureSnowTrail,nativeSnowTrailWindow,nativeSnowTrailTiles,root));
 return sink;
}
NativeOutput nativeVertex(uint vertexId,bool sway){
 uint4 triangleRef=nativeVisibleTriangles[nativeTriangleBase+vertexId/3];
 uint preparedIndex=(vertexId%3)==0?triangleRef.y:((vertexId%3)==1?triangleRef.z:triangleRef.w);
 NativeInstance instance=loadNativeInstance(triangleRef.x);NativeOutput output=(NativeOutput)0;
 if(instance.poseNode!=0xffffffff&&instance.worldGeometry==0)instance.objectToWorld=nativeNodeMatrix(instance.poseNode);
 if(instance.worldGeometry==0){
  NativePreparedVertex input=nativePreparedVertices[preparedIndex];
  output.p=input.p;output.d=input.d;output.s=input.s;output.uv=input.uv;output.textureReference=input.textureReference;
  return output;
 }
 NativeWorldVertex world;
 bool objectSpace=(instance.worldGeometry&@WORLD_OBJECT_SPACE@)!=0;
 bool impostor=objectSpace&&((preparedIndex>>16)&1024u)!=0;
 [branch]if(impostor)world=nativeImpostorVertex(instance,preparedIndex&3u);
 else if(objectSpace)world=nativeObjectSpaceVertex(instance,preparedIndex&0xffffu);
 // Units sink into trail troughs, in every pass (shadow casters and the
 // receiver position included), so the shadow and self-shadow follow.
 else{
  uint worldIndex=(instance.worldGeometry&@WORLD_LOD@)!=0?
   instance.secondMorphOffset+(preparedIndex&0xffffu):preparedIndex;
  world=nativeWorldVertices[worldIndex];world.p.z+=nativeFigureTrailSink(instance);
 }
 // Depth-map passes keep the resting shape so cached static shadow maps stay valid.
 output.shadowPosition=world.p.xyz;
 // Height within the plant, 0 bottom to 1 top, for the snow on its branches
 // (liquidFall is free on figures; y 1 marks it valid). Two sources, one
 // per kind of record that draws a crown:
 // - object-space cut-out records with a crown impostor table (worldGeometry
 //   bit 3, static_lod.h native_world_impostor; decorations on both maps):
 //   the table's bounding sphere. Their impostor quad faces the camera, so
 //   its corner height is corrected for the camera pitch (the plant's top
 //   projects to the quad's up.z, not to its edge);
 // - retained swaying plants drawn through pose nodes (the main map's
 //   trees, world vertices): the plant root node and the plant height the
 //   sway fields carry (instance flags 0x200, bits 22-27).
 // cab694fe set it only for the first kind: the main map's pines took the
 // fixed fallback and every pine looked evenly covered.
 [branch]if(objectSpace&&(instance.worldGeometry&8u)!=0){
  float4 sphere;float3 centre,impostorView;float scale;nativeImpostorFrame(instance,sphere,centre,scale,impostorView);
  float h;
  [branch]if(impostor){
   float3 right,up;nativeImpostorBasis(impostorView,right,up);
   h=((preparedIndex&2u)!=0?1:-1)/max(up.z,.3)*.5+.5;
  }else h=(world.p.z-centre.z)/max(sphere.w*scale,1e-3)*.5+.5;
  output.liquidFall=float4(saturate(h),1,0,0);
 }else [branch]if((instance.nativeFlags&0x200u)!=0&&treeSwayAnchored(instance)){
  // Object-space swaying parts without an impostor table read the same
  // anchor from their record (treeSwayBase).
  float plantHeight=max(((instance.nativeFlags>>22)&63u)*.25,.25);
  output.liquidFall=float4(saturate((world.p.z-treeSwayBase(instance).z)/plantHeight),1,0,0);
 }
 if(sway){world.p.xyz+=treeSwayOffset(world.p.xyz,instance);output.swaying=treeSwayStrength(instance)>0?1u:0u;}
 // Static LOD crossfade code (nativeStaticLodSelect) for the pixel stages.
 if(objectSpace||(instance.worldGeometry&@WORLD_LOD@)!=0)output.swaying|=(preparedIndex>>16)<<8;
 // Bit 30: a foliage part (instance flag 0x100, native_foliage_part_flag)
 // for the pixel stages' snow build-up; the LOD code above ends at bit 23.
 if((instance.nativeFlags&0x100u)!=0)output.swaying|=1u<<30;
 // Bit 29: a unit (instance flag 0x2, native_unit_flag): creatures and
 // characters take no snow build-up. Posed statics (houses, props drawn
 // through pose nodes) are not units (c71 0c971bc2: keying on the pose node
 // left every roof bare).
 if((instance.nativeFlags&0x2u)!=0)output.swaying|=1u<<29;
 if(instance.poseNode!=0xffffffff)instance.objectToWorld=nativeNodeMatrix(instance.poseNode+world.partNode);
 output.worldPosition=world.p.xyz;output.worldNormal=world.normal.xyz;
 output.p=mul(instance.objectToClip,world.p);
 bool isTerrain=instance.terrainVertexOffset!=0xffffffff;
 NativeTerrainVertex terrain=(NativeTerrainVertex)0;
 if(isTerrain){
  terrain=loadTerrainVertex(instance,world.sourcePosition);
)" R"(
  // Neighbour liquid layers are prepared once at rest. They take the active
  // map's game waves here, per frame, phased by world position and fading in
  // over their first 4 m beyond the active map (whose waves fade out over its
  // last 4 m at a touched seam): both maps' water moves alike and meets at
  // the shared authored edge heights (frame switch nativeTerrainFogControl.w).
  [branch]if((instance.nativeFlags&@NEIGHBOUR_FLAG@)!=0&&nativeTerrainFogControl.w>0&&(terrain.flags&1)!=0&&liquidSurfaceEnabled(terrain.flags)){
   float4 b=nativeTerrainActiveBounds;float2 q=world.p.xy;
   float force=instance.terrainWindForce*terrain.windMultiplier*saturate(max(max(b.x-q.x,q.x-b.z),max(b.y-q.y,q.y-b.w))/4)*
               (1-smoothstep(.015,.08,length(terrain.liquidCurrent))); // running water does not heave
   // World phase only (no per-map noise or grid index), so two neighbours
   // also agree on the vertices they share.
   float wave=sin(dot(q,instance.terrainWindDirection)+instance.terrainWindPhase*terrain.windMultiplier)*force;
   world.p.z+=wave*.25;
   world.p.xy+=sin(q*.7853981634+instance.terrainPreciseTime*.05)*force*3;
   output.worldPosition=world.p.xyz;output.p=mul(instance.objectToClip,world.p);
  }
  // Depths decode at rest; the live shifts give the current water depth.
  terrain.depth+=terrainLayerShift(instance,terrain.shiftObjects&0xffffu);
  // Signed: a buried liquid edge beyond the bank is negative, so the depth
  // crosses zero at the real waterline inside shore triangles and bed effects
  // (caustics) stay below the surface instead of spreading up the dry bank.
  if((terrain.shiftObjects>>16)!=0xffffu)terrain.liquidDepth+=terrainLayerShift(instance,terrain.shiftObjects>>16);
  // A liquid surface dipping into a closed pit (a fall's impact funnel) is
  // lifted to the level it spills at: the pool lies flat.
  if(liquidSurfaceEnabled(terrain.flags)&&terrain.liquidFill>0){
   world.p.z+=terrain.liquidFill;terrain.depth+=terrain.liquidFill;
   output.worldPosition=world.p.xyz;output.p=mul(instance.objectToClip,world.p);
  }
 }
 // Liquid batches draw twice (flush_native): bit 1 keeps only the liquid
 // triangles (nativeLiquidPS), bit 2 only the others (the ground variant).
 // Every vertex of a decoded tile carries the tile's liquid flag, so a
 // triangle collapses whole to zero area.
 if((nativeLiquidLayer&6u)!=0&&(isTerrain&&liquidSurfaceEnabled(terrain.flags))!=((nativeLiquidLayer&2u)!=0)){
  output.p=float4(0,0,0,1);return output;
 }
 // Snow trail field, flat tile class drawn untessellated (bit 16): the
 // triangles near an active trail tile collapse; the tessellated draw of the
 // same region (nativeTessVS, bit 32) keeps exactly those.
 [branch]if((nativeLiquidLayer&16u)!=0&&nativeTriangleNearTrail(triangleRef)){output.p=float4(0,0,0,1);return output;}
 // Liquid foam layer pass (nativeLiquidLayer): whitewater is its own surface,
 // drawn after the water body and pushed out along the liquid's normal. A
 // triangle without foam at any corner collapses to zero area, so lakes,
 // land and calm water add no pixels.
 if((nativeLiquidLayer&1u)!=0){
  bool keep=false;
  if(isTerrain&&liquidSurfaceEnabled(terrain.flags)){
   NativeTerrainVertex a=loadTerrainVertex(instance,nativeWorldVertices[triangleRef.y].sourcePosition);
   NativeTerrainVertex b=loadTerrainVertex(instance,nativeWorldVertices[triangleRef.z].sourcePosition);
   NativeTerrainVertex c=loadTerrainVertex(instance,nativeWorldVertices[triangleRef.w].sourcePosition);
   keep=max(liquidFoamLayerWeight(a.liquidCascade,a.liquidCurrent,a.liquidSelfIllumination),
        max(liquidFoamLayerWeight(b.liquidCascade,b.liquidCurrent,b.liquidSelfIllumination),
            liquidFoamLayerWeight(c.liquidCascade,c.liquidCurrent,c.liquidSelfIllumination)))>0;
  }
  if(!keep){output.p=float4(0,0,0,1);return output;}
  float2 current=mul((float3x3)instance.objectToWorld,float3(terrain.liquidCurrent,0)).xy;
  world.p.xyz+=liquidSafeNormal(float3(current,1))*liquidFoamLayerLift;
  output.worldPosition=world.p.xyz;output.p=mul(instance.objectToClip,world.p);
 }
 if(instance.lightingEnabled!=0){
  // Preserve the established object-space material response, independently of
  // the world normal used by the light-space geometry/receiver passes.
  float3 normal=normalize(mul(transpose((float3x3)instance.objectToWorld),world.normal.xyz));
  output.d=calculateWorldLighting(instance,normal,isTerrain?terrain.emissive:instance.nativeEmissive.rgb,
                                  isTerrain?terrain.opacity:instance.nativeMaterialAlpha);
  float3 light=mul(inverseObjectLinear(instance.objectToWorld),nativeWorldSunDirection.xyz);
  float lengthSquared=dot(light,light);
  float facing=lengthSquared>1e-12?((instance.terrainFlags&8)!=0?1:max(dot(normal,light*rsqrt(lengthSquared)),0)):0;
  output.directional=nativeWorldSunDiffuse.rgb*instance.nativeMaterialDiffuse*facing;
  output.emissive=isTerrain?terrain.emissive:instance.nativeEmissive.rgb;
  output.pointMaterial=isTerrain?float4(1,1,1,1):float4(instance.nativeMaterialDiffuse,0);
  output.shadowMode=1; // Lit material: shadow only the directional term.
  output.s=isTerrain?float4(0,0,0,terrainFogFactor(instance,world.undeformedXY)):instance.nativeSpecular;
  if(!isTerrain)output.s.a=(instance.nativeDefaultSpecular>>24)/255.0;
 }else{
  output.d=unpackColor((instance.nativeFlags&4)!=0||instance.colorOffsets.x==0xffffffff?instance.nativeDefaultDiffuse:world.diffuse);
  output.s=unpackColor((instance.nativeFlags&4)!=0||instance.colorOffsets.y==0xffffffff?instance.nativeDefaultSpecular:world.specular);
  output.shadowMode=2; // Packed world color still has a receiver position.
 }
 if((instance.nativeFlags&@PREVIEW_SPECULAR_RGB_FLAG@)!=0)output.s.rgb=unpackColor(instance.nativeDefaultSpecular).rgb;
 if(isTerrain) {
  output.bedLiquid=float2(terrain.liquidDepth,terrain.liquidBedEmission);
  output.terrainVertexFlags=terrain.flags;
  // Opaque static base tiles only: the material reads the tile's atlas cell.
  if((terrain.flags&(16u|4096u))!=0) {
   output.terrainSlope=mul((float3x3)instance.objectToWorld,terrainSlopeNormal(terrain.slopeNormal));
   output.terrainFlat=float2((terrain.gridIndex>>16)&255u,terrain.gridIndex>>24)/255.0;
   output.terrainTiles=(float)instance.terrainTilesPerSide;
  }
  if(liquidSurfaceEnabled(terrain.flags)) {
   output.shadowMode=0; // Liquid overlays neither receive nor join caster commands.
   // Stock-look water (nativeLiquidLayer bit 3, water_simple): drawn by the
   // ground variant, which must not see bed caustics on the surface.
   if((nativeLiquidLayer&8u)!=0)output.bedLiquid=0;
   output.emissive=terrain.emissive;
   output.liquidTintDepth=float4(terrain.objectColor,terrain.depth);
   float multiplier=(terrain.flags&1)!=0?terrain.liquidMultiplier:0;
   output.liquidMaterial=float4(multiplier,terrain.opacity,terrain.liquidSelfIllumination,((terrain.flags>>29)&7u)/7.0);
   float2 wind=mul((float3x3)instance.objectToWorld,float3(instance.terrainWindDirection,0)).xy;
   output.liquidFlow=float4(wind,instance.terrainWindForceNormalized,asfloat(instance.nativePadding.z));
   output.liquidView=liquidViewDirection(instance.objectToClip,world.p.xyz);
   output.liquidTileLocal=float2((terrain.flags>>25)&3,(terrain.flags>>27)&3)*.5;
   // Slope and cascade foam are scalars; the downhill current turns with the instance.
   output.liquidDirections=float4(terrain.liquidBend,terrain.liquidCascade,
       mul((float3x3)instance.objectToWorld,float3(terrain.liquidCurrent,0)).xy);
   output.liquidFall=float4(terrain.liquidDrop,terrain.liquidFill,0,0);
  }
 }
 // Sector.cpp stores wave glint in byte RGB; normalize its 50-unit scale to 0..1.
 // The game's wave glint (specular) on wave vertices; liquid surfaces shade
 // their own, except stock-look water (bit 3), which keeps the game's.
 if(isTerrain&&(!liquidSurfaceEnabled(terrain.flags)||(nativeLiquidLayer&8u)!=0)&&(instance.terrainFlags&1)!=0&&(terrain.flags&1)!=0)
  output.s.rgb=saturate(terrain.objectColor*terrain.windMultiplier*instance.terrainWindForceNormalized*(50.0/255.0)*world.normal.w);
 output.uv=world.uv+instance.nativeUvShift;
 if(isTerrain&&(instance.terrainFlags&2)!=0&&(terrain.flags&4)!=0&&
    terrain.animationIndex<instance.terrainPhaseCount&&instance.terrainTilesPerSide!=0){
  uint phase=((instance.terrainFlags&4)!=0?nativeAnimation.Load(instance.terrainPhaseOffset+terrain.animationIndex*4):nativeDynamic.Load(instance.terrainPhaseOffset+terrain.animationIndex*4));
  uint baseTile=terrain.animationBaseTile,currentTile=baseTile+phase;
  float side=(float)instance.terrainTilesPerSide;
  output.uv+=float2(((int)(currentTile%instance.terrainTilesPerSide)-(int)(baseTile%instance.terrainTilesPerSide))/side,
                   -((int)(currentTile/instance.terrainTilesPerSide)-(int)(baseTile/instance.terrainTilesPerSide))/side);
 }
 if(isTerrain&&liquidSurfaceEnabled(terrain.flags)) {
  float2 shift=output.uv-world.uv;
  // A borrowed swamp frame (native_terrain_swamp_art_borrowed) is not this
  // water tile's flipbook: the water's phase must not move it.
  output.liquidUvBounds=terrain.liquidUvBounds+((terrain.flags&32u)!=0?0:shift.xyxy);
 }
 output.neighbour=(instance.nativeFlags&@NEIGHBOUR_FLAG@)!=0?1u:0u;
 if(output.neighbour!=0 && !isTerrain)output.s.a=terrainFogFactor(instance,world.undeformedXY);
 output.contact=!isTerrain&&(instance.nativeFlags&@CONTACT_FLAG@)!=0?1u:0u;
 output.contactHeight=asfloat(instance.nativePadding.w);
 output.textureReference=instance.nativePadding.xy;
 // Crown impostor (figures only, so the liquid fields are free): the frame
 // images and weights, the atlas slice, and what the pixel stage needs to
 // light the baked object-space normals as the vertex lighting lights the
 // crown's own (calculateWorldLighting).
 [branch]if(impostor){
  float4 impostorFrames01,impostorFrames23,impostorWeights;uint impostorBase;
  nativeImpostorImages(instance,output.shadowPosition,impostorFrames01,impostorFrames23,impostorWeights,impostorBase);
  output.liquidTintDepth=impostorFrames01;output.liquidMaterial=impostorFrames23;output.liquidUvBounds=impostorWeights;
  // Plain small integers as floats (bit patterns of small integers are
  // denormals, which the attribute path may flush). w: diagnostic view.
  output.liquidFlow=float4(instance.secondMorphOffset,impostorBase&255u,impostorBase>>8,nativeImpostorParams.z);
  float3 light=mul(inverseObjectLinear(instance.objectToWorld),nativeWorldSunDirection.xyz);
  float lengthSquared=dot(light,light);
  output.liquidView=lengthSquared>1e-12?light*rsqrt(lengthSquared):0;
  output.liquidDirections=float4(instance.nativeMaterialDiffuse,lengthSquared>1e-12?1:0);
 }
 return output;
}
NativeOutput nativeVS(uint vertexId:SV_VertexID){NativeOutput o=nativeVertex(vertexId,true);precise float4 position=o.p;o.p=position;return o;}
// Dry base terrain casts with its authored single-vertex pits filled
// (owned_terrain_pit_fill, L-085): lighting never shows them, so the walls
// they turn away from a light only drew wedges and self-shadow on flat sand.
float nativeShadowPitFill(uint vertexId){
 uint4 t=nativeVisibleTriangles[nativeTriangleBase+vertexId/3];
 uint index=(vertexId%3)==0?t.y:((vertexId%3)==1?t.z:t.w);
 NativeInstance instance=loadNativeInstance(t.x);
 if(instance.terrainVertexOffset==0xffffffff||instance.worldGeometry==0)return 0; // terrain is never object-space
 NativeTerrainVertex terrain=loadTerrainVertex(instance,nativeWorldVertices[index].sourcePosition);
 return liquidSurfaceEnabled(terrain.flags)?0:max(terrain.liquidFill,0);
}
NativeOutput nativeShadowVS(uint vertexId:SV_VertexID){
 NativeOutput output=nativeVertex(vertexId,false);
 float3 caster=output.worldPosition;caster.z+=nativeShadowPitFill(vertexId);
 output.p=mul(shadowLight[shadowControl.y],float4(caster,1));return output;
}
)" R"(
// A map is selected by its camera depth band and (spatial selection) the
// guarded light square, never by the camera's x/y frustum: terrain lookups
// are lifted along the slope normal (terrainShadowLookup), which carries the
// ground in the top rows of the screen above the frustum. An x/y test left
// those rows with no map, so their shadows ended along a straight line below
// the top edge.
bool nativeShadowContains(uint candidate,float3 worldPosition){
 float4 camera=mul(shadowCamera[candidate],float4(worldPosition,1));
 if(camera.w<=0||camera.z<0||camera.z>camera.w)return false;
 if(shadowBias[4].z<.5)return true;
 float4 light=mul(shadowLight[candidate],float4(worldPosition,1));
 float guard=1-7.0/max(1.0,(float)shadowControl.z);
 return light.w>0&&abs(light.x)<=guard*light.w&&
  abs(light.y)<=guard*light.w&&light.z>=0&&light.z<=light.w;
}
uint nativeShadowSlice(float3 worldPosition){
 // FXC /O3 miscompiles continue in the unrolled cascade/filter loop when
 // leading camera selectors are empty. Select first, then filter only once.
 // The containment test (two matrix products) runs only until a map holds
 // the point: the nested branch skips the later candidates (no continue).
 uint slice=5;
 [unroll]for(uint candidate=0;candidate<5;candidate++){
  [branch]if(slice==5){
   [branch]if(nativeShadowContains(candidate,worldPosition))slice=candidate;
  }
 }
 return slice;
}
float3 nativeShadowCoverage(float3 worldPosition){
 uint slice=nativeShadowSlice(worldPosition);
 if(slice==5)return float3(.35,.35,.35);
 if((shadowControl.x&(1u<<slice))==0)return float3(1,0,1);
 float4 light=mul(shadowLight[slice],float4(worldPosition,1));
 float3 coordinate=light.xyz/light.w;
 if(abs(coordinate.x)>1||abs(coordinate.y)>1||coordinate.z<0||coordinate.z>1)return float3(0,1,1);
 const float3 colors[5]={float3(1,.25,.2),float3(.2,1,.25),float3(.2,.5,1),float3(1,.85,.2),float3(.8,.35,1)};
 return colors[slice];
}
// Soft shadows (PCSS). shadowBias[5]: x = tan(half sun angle), y = sun blocker
// search radius (world), z = quality (0 keeps the tapered PCF), w = point source
// scale. Disks rotate per pixel; shadowJitter rotates them per frame only while
// the temporal shadow history integrates the result.
@SHADOW_PLANE_COMPARE@
// Gather reads raw depth through the dedicated clamp sampler at s10, bound by
// every pass that filters the sun maps (world, ground cover, critters).
float nativeShadowPlaneTap(uint slice,float side,float2 at,float2 origin,float receiver,float2 depthStep){
 return shadowPlaneCompare(shadowDepth,shadowGatherSampler,slice,side,at,origin,receiver,depthStep);
}
float2 nativeVogel(uint k,uint count,float rotation){
 float angle=k*2.39996323+rotation;
 return sqrt((k+.5)/count)*float2(cos(angle),sin(angle));
}
// temporal: this receiver blends the shadow history. A receiver that skips it
// (swaying leaves, critters) keeps a fixed per-pixel disk; rotating it every
// frame with nothing to average turns the penumbra into per-frame noise.
float nativeShadowRotation(float2 pixel,bool temporal){
 float2 jitter=temporal?shadowJitter.xy:0;
 return 6.2831853*frac(52.9829189*frac(dot(floor(pixel)+jitter,float2(.06711056,.00583715))));
}
// The soft filter's blocker search radius in texels; its taps stay inside it.
float nativeShadowSearch(uint slice,float minTexels){
 return clamp(shadowBias[5].y/max(shadowBias[slice].x,1e-6),max(2.0,minTexels),24.0);
}
// Exact early-out (shadow_early_out): 1 when every texel the filter can read
// (within `reach` texels per axis of `texel`) compares lit, 0 when every one
// is a blocker, -1 otherwise. Those are the full filter's own results: each
// tap is a 0/1 compare against the receiver plane, bilinear-weighted. The
// cell holds the nearest and 1 - farthest depth within two cells of the
// pixel's cell (build_native_shadow_range); shadowScrollControl.w marks the
// maps whose cells are current. An unbound view reads zeros: never a skip.
int nativeShadowEarly(uint slice,float2 texel,float side,float receiver,float2 depthStep,float reach){
 if((shadowScrollControl.w&(1u<<slice))==0)return -1;
 int cells=int(side)/16;
 float2 range=shadowRange.Load(int4(clamp(int2(texel/16),0,cells-1),slice,0));
 float extent=(abs(depthStep.x)+abs(depthStep.y))*reach;
 if(range.x>saturate(receiver+extent))return 1;
 if(1-range.y+1e-6<saturate(receiver-extent))return 0;
 return -1;
}
float nativeShadowSoft(uint slice,float3 coordinate,float2 texel,float2 depthStep,float bias,float side,
                       float minTexels,float rotation){
 // Orthographic maps: depth is linear, so depth differences are world gaps.
 float texelWorld=max(shadowBias[slice].x,1e-6),depthWorld=1/max(shadowBias[slice].y,1e-12);
 bool high=shadowBias[5].z>1.5;
 float search=nativeShadowSearch(slice,minTexels);
 uint searchTaps=high?16:8;
 float blockers=0,blockerDepth=0;
 [loop]for(uint k=0;k<searchTaps;++k){
  float2 at=clamp(texel+nativeVogel(k,searchTaps,rotation)*search,.5,side-.5);
  float stored=shadowDepth.Load(int4(int2(at),slice,0));
  // The receiver plane (with its bias) at the loaded texel's centre is not
  // its own blocker.
  if(stored<coordinate.z+dot(depthStep,floor(at)+.5-texel)-bias){blockers+=1;blockerDepth+=stored;}
 }
 // Every receiver is filtered: a missed search only narrows the kernel, it
 // never makes a binary lit decision along the texel grid.
 float radius=minTexels;
 if(blockers>0){
  float gap=max(coordinate.z-blockerDepth/blockers,0)*depthWorld;
  radius=clamp(gap*shadowBias[5].x/texelWorld,minTexels,search);
 }
 uint taps=high?24:12;
 float visibility=0;
 [loop]for(uint t=0;t<taps;++t){
  float2 offset=nativeVogel(t,taps,rotation+1.3)*radius;
  visibility+=nativeShadowPlaneTap(slice,side,texel+offset,texel,coordinate.z-bias,depthStep);
 }
 return visibility/taps;
}
float nativeShadowFilter(uint slice,float3 worldPosition,float3 positionDx,float3 positionDy,float rotation){
  float4 light=mul(shadowLight[slice],float4(worldPosition,1));float3 coordinate=light.xyz/light.w;
  if(abs(coordinate.x)>1||abs(coordinate.y)>1||coordinate.z<0||coordinate.z>1)return 1;
  // Directional maps are orthographic. Solve the rasterized receiver plane in
  // shadow-texel/depth coordinates; authored normals can differ from this plane.
  float3 dx=mul((float3x3)shadowLight[slice],positionDx);
  float3 dy=mul((float3x3)shadowLight[slice],positionDy);
  float side=max(1.0,(float)shadowControl.z);
  float halfSide=.5*side;dx.xy*=float2(halfSide,-halfSide);dy.xy*=float2(halfSide,-halfSide);
  float3 plane=cross(dx,dy);
  // A collapsed light projection cannot supply a finite receiver plane.
  if(plane.z==0||!all(isfinite(plane)))return 1;
  float2 depthStep=-plane.xy/plane.z;
  if(!all(isfinite(depthStep)))return 1;
  float bias=.5*shadowBias[slice].x*shadowBias[slice].y;
  float2 texel=float2(coordinate.x*.5+.5,.5-coordinate.y*.5)*side;
  bool farFootprint=slice>=3&&shadowBias[4].z>.5;
  bool soft=shadowBias[5].z>.5;
  float minTexels=farFootprint?min(1.5,.5/max(shadowBias[slice].x,1e-6)):1.5;
  // Read reach per axis: the soft taps' radius stays within the search, the
  // far 3x3 taps within 1.5 texels, the 5x5 taps within 2.5; the plane
  // compare's 2x2 Gather adds 1.5.
  int early=nativeShadowEarly(slice,texel,side,coordinate.z-bias,depthStep,
   (soft?nativeShadowSearch(slice,minTexels):farFootprint?1.5:2.5)+1.5);
  [branch]if(early>=0)return early;
  // The soft kernel never falls below the fixed filter's footprint, so the
  // penumbra (a world radius) only widens the existing edge.
  [branch]if(soft)
   return nativeShadowSoft(slice,coordinate,texel,depthStep,bias,side,minTexels,rotation);
  // The owned spatial maps carry a world-space texel size for this footprint.
  if(farFootprint){
    // Keep the far filter's tap centers within one world unit even when a
    // broad map covers much more ground per texel. The bilinear plane compare
    // supplies the remaining subtexel transition.
    float spacing=min(1.5,.5/max(shadowBias[slice].x,1e-6));
    float visibility=0;
    [unroll]for(int y=-1;y<=1;y++)[unroll]for(int x=-1;x<=1;x++){
     float2 sampleTexel=texel+float2(x,y)*spacing;
     float weight=(x==0?2:1)*(y==0?2:1);
     visibility+=weight*nativeShadowPlaneTap(slice,side,sampleTexel,texel,coordinate.z-bias,depthStep);
    }
    return visibility/16;
  }
  float2 center=floor(texel)+.5,f=texel-center;
  // A compact, C1-continuous 5x5 footprint softens moving raster silhouettes
  // without changing the sun or losing per-texel receiver-plane depth bias.
  // A tap reaches zero weight as it enters/leaves at 2.5 texels.
  float wx[5],wy[5],sumx=0,sumy=0;
  [unroll]for(int tap=-2;tap<=2;tap++){
   float2 distance=abs(float2(tap,tap)-f);
   float2 taper=saturate(1-distance*distance*(4.0/25.0));
   float2 weight=taper*taper;
   wx[tap+2]=weight.x;wy[tap+2]=weight.y;
   sumx+=weight.x;sumy+=weight.y;
  }
  float visibility=0;
  [unroll]for(int y=-2;y<=2;y++)[unroll]for(int x=-2;x<=2;x++){
   float2 sampleTexel=center+float2(x,y);
   // Extrapolation beyond the depth range must still compare clear depth as lit.
   float depth=saturate(coordinate.z+dot(depthStep,sampleTexel-texel)-bias);
   visibility+=wx[x+2]*wy[y+2]*shadowDepth.SampleCmpLevelZero(
    shadowSampler,float3(sampleTexel/side,slice),depth);
  }
  return visibility/(sumx*sumy);
}
float nativeShadowVisibility(float3 worldPosition,float3 positionDx,float3 positionDy,float2 pixel,bool temporal){
 uint slice=nativeShadowSlice(worldPosition);
 if(slice==5)return 1;
 if((shadowControl.x&(1u<<slice))==0)
  return shadowBias[4].z>.5?-1:1;
 float rotation=nativeShadowRotation(pixel,temporal);
 float visibility=nativeShadowFilter(slice,worldPosition,positionDx,positionDy,rotation);
 if(shadowBias[4].z<.5)return visibility;
 float4 light=mul(shadowLight[slice],float4(worldPosition,1));
 float edge=max(abs(light.x),abs(light.y))/light.w;
 const float blendStart=.75,blendEnd=1-7.0/max(1.0,(float)shadowControl.z);
 float blend=smoothstep(blendStart,blendEnd,edge);
 if(slice==4)return lerp(visibility,1,blend);
 if(slice<3&&shadowBias[slice].w>shadowBias[slice].z){
  float4 camera=mul(shadowCamera[slice],float4(worldPosition,1));
  // Perspective clip Z crowds most visible ground near 1. Blend over the
  // actual linear view-distance overlap shared with the next close map.
  blend=max(blend,smoothstep(shadowBias[slice].z,shadowBias[slice].w,camera.w));
 }
 if(blend<=0)return visibility;
 // The first enabled coarser map holding the point (its containment test
 // only for enabled maps past slice, and none after the first hit).
 uint next=5;
 [unroll]for(uint coarser=0;coarser<5;coarser++){
  [branch]if(next==5&&coarser>slice&&(shadowControl.x&(1u<<coarser))!=0){
   [branch]if(nativeShadowContains(coarser,worldPosition))next=coarser;
  }
 }
 [branch]if(next<5)return lerp(visibility,nativeShadowFilter(next,worldPosition,positionDx,positionDy,rotation),blend);
 return lerp(visibility,1,blend);
}
#ifdef NATIVE_LOW
)" R"(
// Low pixel path sun shadow. Low has only the close maps 1 and 2
// (shadow_close_cascades <= 2 and shadow_far off: native_low_pixel_path), so
// both are tested with constant indices: no containment loop over five maps
// and no dynamically indexed shadow constants (per-lane memory loads on AMD).
// The selection is nativeShadowSlice's for these maps: the first camera band
// that holds the point, with the guarded light containment of spatial
// selection. A close map Low switches off has a zero camera and never holds.
// As in nativeShadowContains there is no camera x/y frustum test: lookups lifted
// past the screen edge, which keep their shadow instead of turning lit.
struct NativeLowShadowMap{float3 light;float distance;bool holds;};
NativeLowShadowMap nativeLowShadowMap(uint slice,float4 p,float guard,bool spatial){
 NativeLowShadowMap map;
 float z=dot(shadowCamera[slice][2],p);
 map.distance=dot(shadowCamera[slice][3],p); // linear view distance
 // Directional maps are orthographic: row 3 of world_to_light is 0,0,0,1.
 map.light=mul((float3x4)shadowLight[slice],p);
 map.holds=map.distance>0&&z>=0&&z<=map.distance&&
  (!spatial||(max(abs(map.light.x),abs(map.light.y))<=guard&&map.light.z>=0&&map.light.z<=1));
 return map;
}
// Four point compare taps half a texel around the receiver, each at the
// rasterized receiver plane's depth (no acne on slopes), averaged.
float nativeLowShadowTaps(float3 light,float3x3 toLight,float bias,float slice,float3 positionDx,float3 positionDy){
 if(abs(light.x)>1||abs(light.y)>1||light.z<0||light.z>1)return 1;
 float side=max(1.0,(float)shadowControl.z),halfSide=.5*side;
 float3 dx=mul(toLight,positionDx),dy=mul(toLight,positionDy);
 dx.xy*=float2(halfSide,-halfSide);dy.xy*=float2(halfSide,-halfSide);
 float3 plane=cross(dx,dy);
 // A collapsed light projection cannot supply a finite receiver plane.
 if(plane.z==0||!all(isfinite(plane)))return 1;
 float2 depthStep=-plane.xy/plane.z;
 if(!all(isfinite(depthStep)))return 1;
 float2 texel=float2(light.x*.5+.5,.5-light.y*.5)*side;
 float visibility=0;
 [unroll]for(int tap=0;tap<4;tap++){
  float2 offset=float2((tap&1)!=0?.5:-.5,(tap&2)!=0?.5:-.5);
  // Extrapolation beyond the depth range must still compare clear depth as lit.
  float depth=saturate(light.z+dot(depthStep,offset)-bias);
  visibility+=shadowDepth.SampleCmpLevelZero(shadowSampler,float3((texel+offset)/side,slice),depth);
 }
 return visibility*.25;
}
float nativeLowShadowVisibility(float3 worldPosition,float3 positionDx,float3 positionDy){
 float4 p=float4(worldPosition,1);
 bool spatial=shadowBias[4].z>.5;
 float guard=1-7.0/max(1.0,(float)shadowControl.z);
 NativeLowShadowMap near=nativeLowShadowMap(1,p,guard,spatial),far=nativeLowShadowMap(2,p,guard,spatial);
 if(!near.holds&&!far.holds)return 1;
 // A selected map without a valid depth map is a contract failure (magenta).
 if((shadowControl.x&(near.holds?2u:4u))==0)return spatial?-1:1;
 bool farValid=far.holds&&(shadowControl.x&4u)!=0;
 float farVisibility=1;
 float4 farBias=shadowBias[2];
 float farEdge=max(abs(far.light.x),abs(far.light.y));
 [branch]if(near.holds){
  float4 bias=shadowBias[1];
  float visibility=nativeLowShadowTaps(near.light,(float3x3)shadowLight[1],.5*bias.x*bias.y,1,positionDx,positionDy);
  if(!spatial)return visibility;
  // One blend at Low: map 1 hands over to map 2 across the published view
  // distance overlap and the outer tenth of its square (the full path starts
  // that band at .75); map 2 holds the band.
  float blend=smoothstep(.9,guard,max(abs(near.light.x),abs(near.light.y)));
  if(bias.w>bias.z)blend=max(blend,smoothstep(bias.z,bias.w,near.distance));
  [branch]if(blend>0&&farValid)
   farVisibility=nativeLowShadowTaps(far.light,(float3x3)shadowLight[2],.5*farBias.x*farBias.y,2,positionDx,positionDy);
  return lerp(visibility,farVisibility,blend);
 }
 float visibility=nativeLowShadowTaps(far.light,(float3x3)shadowLight[2],.5*farBias.x*farBias.y,2,positionDx,positionDy);
 if(!spatial)return visibility;
 // Map 2 is the last at Low: it fades to lit at its square's edge and over
 // its far view-distance band, as the full path does without far maps.
 float blend=smoothstep(.75,guard,farEdge);
 if(farBias.w>farBias.z)blend=max(blend,smoothstep(farBias.z,farBias.w,far.distance));
 return lerp(visibility,1,blend);
}
// The cascade coverage view (debug_resource_view) for the Low selection.
float3 nativeLowShadowCoverage(float3 worldPosition){
 float4 p=float4(worldPosition,1);
 bool spatial=shadowBias[4].z>.5;
 float guard=1-7.0/max(1.0,(float)shadowControl.z);
 NativeLowShadowMap near=nativeLowShadowMap(1,p,guard,spatial),far=nativeLowShadowMap(2,p,guard,spatial);
 if(!near.holds&&!far.holds)return float3(.35,.35,.35);
 if((shadowControl.x&(near.holds?2u:4u))==0)return float3(1,0,1);
 float3 light=near.holds?near.light:far.light;
 if(abs(light.x)>1||abs(light.y)>1||light.z<0||light.z>1)return float3(0,1,1);
 return near.holds?float3(.2,1,.25):float3(.2,.5,1);
}
#endif
bool nativeCompare(float a){if(nativeAlphaFunc==1)return false;if(nativeAlphaFunc==2)return a<nativeAlphaRef;if(nativeAlphaFunc==3)return a==nativeAlphaRef;if(nativeAlphaFunc==4)return a<=nativeAlphaRef;if(nativeAlphaFunc==5)return a>nativeAlphaRef;if(nativeAlphaFunc==6)return a!=nativeAlphaRef;if(nativeAlphaFunc==7)return a>=nativeAlphaRef;return true;}
float4 nativeStageArg(uint selector,float4 diffuse,float4 specular,float4 textureSample){uint source=selector&15;float4 value=source==0||source==1?diffuse:(source==2?textureSample:(source==3?nativeTextureFactor:(source==4?specular:0)));if((selector&32)!=0)value=value.aaaa;if((selector&16)!=0)value=1-value;return value;}
float3 nativeColorResult(uint op,float4 a,float4 b,float3 current){if(op==2)return a.rgb;if(op==3)return b.rgb;if(op==4)return a.rgb*b.rgb;if(op==5)return saturate(2*a.rgb*b.rgb);if(op==7)return saturate(a.rgb+b.rgb);return current;}
float nativeAlphaResult(uint op,float4 a,float4 b,float current){if(op==2)return a.a;if(op==3)return b.a;if(op==4)return a.a*b.a;if(op==5)return saturate(2*a.a*b.a);if(op==7)return saturate(a.a+b.a);return current;}
// Fast combiner (renderer.cpp native_fast_combiner_mode, per batch): x the
// colour, y the alpha stage as 1 texture*diffuse, 2 texture, 3 diffuse (the
// current value); 0 runs the general selector chains. For those stage states
// the general chains compute the same products and selections.
// NATIVE_FAST_COMBINER: a specialised program bound only to batches whose
// colour and alpha stages both have a fast mode, so the general selector
// chains compile out.
float3 nativeCombineColor(float4 d,float4 s,float4 t){
 uint mode=nativeCombiner.x;
#ifdef NATIVE_FAST_COMBINER
 return mode==1?t.rgb*d.rgb:(mode==2?t.rgb:d.rgb);
#else
 [branch]if(mode!=0)return mode==1?t.rgb*d.rgb:(mode==2?t.rgb:d.rgb);
 return nativeColorResult(nativeColorOp,nativeStageArg(nativeColorArg1,d,s,t),nativeStageArg(nativeColorArg2,d,s,t),d.rgb);
#endif
}
float nativeCombineAlpha(float4 d,float4 s,float4 t){
 uint mode=nativeCombiner.y;
#ifdef NATIVE_FAST_COMBINER
 return mode==1?t.a*d.a:(mode==2?t.a:d.a);
#else
 [branch]if(mode!=0)return mode==1?t.a*d.a:(mode==2?t.a:d.a);
 return nativeAlphaResult(nativeAlphaOp,nativeStageArg(nativeAlphaArg1,d,s,t),nativeStageArg(nativeAlphaArg2,d,s,t),d.a);
#endif
}
void nativeShadowPS(NativeOutput i){
 float4 t=1;if(nativeTextured!=0)t=i.textureReference.y!=0?nativeTextureArray.Sample(nativeSampler,float3(i.uv,i.textureReference.x)):nativeTexture.Sample(nativeSampler,i.uv);
 float alpha=nativeAlphaResult(nativeAlphaOp,nativeStageArg(nativeAlphaArg1,i.d,i.s,t),nativeStageArg(nativeAlphaArg2,i.d,i.s,t),i.d.a);
 if(nativeAlphaTest!=0&&!nativeCompare(alpha))discard;
}
@POINT_DEPTH_SWITCH@
)" R"(
void nativePointCubeCoordinate(float3 ray,out uint face,out float2 uv,out float major){
 float3 a=abs(ray);float2 plane;
 if(a.x>=a.y&&a.x>=a.z){face=ray.x>=0?0:1;major=a.x;plane=float2(ray.x>=0?-ray.z:ray.z,ray.y);}
 else if(a.y>=a.z){face=ray.y>=0?2:3;major=a.y;plane=float2(ray.x,ray.y>=0?-ray.z:ray.z);}
 else{face=ray.z>=0?4:5;major=a.z;plane=float2(ray.z>=0?ray.x:-ray.x,ray.y);}
 major=max(major,1e-4);uv=float2(.5+plane.x/(2*major),.5-plane.y/(2*major));
}
// A cube face's place in its atlas: the slice, and the texel of the face's
// first interior texel (its grid cell plus the gutter).
float nativePointAtlasPlace(uint slot,uint face,out float2 origin,out float side){
 uint index=slot<NATIVE_POINT_ATLAS_CUBES?slot:slot-NATIVE_POINT_ATLAS_CUBES;
 uint f=index*6+face,tile=f%(NATIVE_POINT_ATLAS_TILES*NATIVE_POINT_ATLAS_TILES);
 side=max(1.0,(float)shadowControl.w);
 origin=float2(tile%NATIVE_POINT_ATLAS_TILES,tile/NATIVE_POINT_ATLAS_TILES)*(side+2)+1;
 return (float)(f/(NATIVE_POINT_ATLAS_TILES*NATIVE_POINT_ATLAS_TILES));
}
float nativePointFetchCompare(uint slot,float2 uv,uint face,float depth){
 float2 origin;float side;float slice=nativePointAtlasPlace(slot,face,origin,side);
 float3 at=float3((origin+uv*side)/((side+2)*NATIVE_POINT_ATLAS_TILES),slice);
 [branch]if(slot>=NATIVE_POINT_ATLAS_CUBES)return nativePointShadowOverlay.SampleCmpLevelZero(pointShadowSampler,at,depth);
 return nativePointShadowStatic.SampleCmpLevelZero(pointShadowSampler,at,depth);
}
// Stored (non-compared) depth for the soft-shadow blocker search.
float nativePointFetchRaw(uint slot,float2 uv,uint face){
 float2 origin;float side;float slice=nativePointAtlasPlace(slot,face,origin,side);
 int4 at=int4(int2(origin+clamp(uv*side,0,side-1)),(int)slice,0);
 [branch]if(slot>=NATIVE_POINT_ATLAS_CUBES)return nativePointShadowOverlay.Load(at);
 return nativePointShadowStatic.Load(at);
}
// A point-shadow atlas cell (face and gutter) reset to depth 1 before its
// face is drawn (the viewport covers just that cell).
float4 nativeDepthClearVS(uint id:SV_VertexID):SV_Position{
 float2 uv=float2((id<<1)&2,id&2);return float4(uv*float2(2,-2)+float2(-1,1),1,1);
}
// Shared instantaneous point visibility: sun history never contains point light.
// softQuality=0 keeps the world's cheapest tier (fixed 12-tap PCF).
float nativePointVisibility(NativePointLight light,float3 shadowPosition,float3 normal,float incidence,float softQuality){
 if(light.shadowSlots.x<=0)return 1;
 // Soft shadows from a spherical light. Every tap offsets the receiver ray
 // in 3D around the light and re-selects its cube face, so wide filters
 // cross face seams instead of clamping at them.
 float radius=light.shadowCenter.w; // cube bake range
 float nearPlane=radius*.02;
 float depthScale=1-nearPlane/radius;
 float side=max(1.0,(float)shadowControl.w);
 float texelWorld=2*length(shadowPosition-light.shadowCenter.xyz)/side;
 // Normal offset: move the receiver about one texel off its surface so
 // grazing-angle receivers stop self-shadowing into crawling acne.
 float3 receiver=shadowPosition+normal*(texelWorld*(1+1.5*(1-saturate(incidence))));
 float3 ray=receiver-light.shadowCenter.xyz;
 float distance=max(length(ray),1e-4);
 // A constant subtraction in projected depth grows into a large world-space
 // gap at the outer half of a cube; bound the receiver bias by a texel and
 // the light radius before projecting it.
 // Slope-scaled: surfaces turning edge-on to a low light (lava, fires) need a
 // larger comparison bias or depth texels alias into stripes along them.
 float cosine=max(saturate(incidence),.15);
 // Far grazing receivers span many depth units per texel; a tight cap turns
 // whole texels into alternating lit/shadowed acne blocks.
 float receiverBias=min(texelWorld*(.5+.75*sqrt(1-cosine*cosine)/cosine),radius*.05);
 float3 axis=ray/distance;
 float3 tangent=normalize(cross(abs(axis.z)<.99?float3(0,0,1):float3(1,0,0),axis));
 float3 bitangent=cross(axis,tangent);
 uint staticSlot=(uint)light.shadowSlots.x-1;
 bool overlay=light.shadowSlots.y>0;uint overlaySlot=overlay?(uint)light.shadowSlots.y-1:0;
 // 12-point Vogel spiral, fixed orientation: smooth penumbrae without the grain
 // of per-cell random rotation, and stable under camera motion.
 const float2 disk[12]={float2(.204,0),float2(-.260,.238),float2(.040,-.455),float2(.328,.428),
  float2(-.603,-.107),float2(.570,-.364),float2(-.191,.710),float2(-.363,-.701),float2(.736,.339),
  float2(-.802,.302),float2(.450,-.804),float2(.253,.955)};
 // Fixed-kernel PCF on every receiver: no blocker-search early-out. Any
 // binary lit/unlit decision follows the texel grid and shows as sharp lit
 // teeth along shadow edges, whatever the search pattern.
 float filterWorld=1.5*texelWorld;
 // NATIVE_OFF_POINT_SOFT: a specialised program bound while the soft
 // shadow quality (shadowBias[5].z) is 0, so the blocker search compiles out.
#ifndef NATIVE_OFF_POINT_SOFT
 [branch]if(softQuality>.5){
  // PCSS: the lamp is a sphere (the scheduler's source radius, taken from the
  // bake range and scaled by the light-size setting). The blocker search only
  // widens the same always-filtered kernel, so it keeps the no-teeth rule.
  float source=clamp(radius*.03,.15,.6)*shadowBias[5].w;
  float widest=(softQuality>1.5?4.0:3.0)*texelWorld;
  uint searchTaps=softQuality>1.5?12:8;
  float blockers=0,ratio=0;
  [loop]for(uint s=0;s<searchTaps;++s){
   uint face;float2 uv;float major;
   nativePointCubeCoordinate(ray+disk[s].x*tangent*widest+disk[s].y*bitangent*widest,face,uv,major);
   float compareDepth=(1-nearPlane/max(major-receiverBias,nearPlane))/depthScale;
   float stored=nativePointFetchRaw(staticSlot,uv,face);
   if(overlay&&light.shadowSlots.w>.5)stored=min(stored,nativePointFetchRaw(overlaySlot,uv,face));
   if(stored<compareDepth){blockers+=1;ratio+=nearPlane/max(1-stored*depthScale,1e-4)/major;}
  }
  if(blockers>0){
   float q=max(ratio/blockers,1e-3); // blocker distance / receiver distance along each tap
   filterWorld=clamp(source*(1-q)/q,filterWorld,widest);
  }
 }
#endif
 float3 u=tangent*filterWorld;
 float3 v=bitangent*filterWorld;
 float shadow=0;
 // The static and the overlay map share one fetch call site (each call
 // inlines the page switch): with an overlay, step k reads tap k/2 from the
 // static map (even k) or the overlay (odd k). One flat loop: a nested
 // per-tap loop around the switch cost the shader about 90 temps.
 uint step=overlay?2u:1u;float tap=1;
 [loop]for(uint k=0;k<12u*step;++k){
  uint t=k/step;bool second=k-t*step!=0;
  uint face;float2 uv;float major;
  nativePointCubeCoordinate(ray+disk[t].x*u+disk[t].y*v,face,uv,major);
  float compareDepth=(1-nearPlane/max(major-receiverBias,nearPlane))/depthScale;
  float fetched=nativePointFetchCompare(second?overlaySlot:staticSlot,uv,face,compareDepth);
  tap=second?min(tap,lerp(1,fetched,light.shadowSlots.w)):fetched;
  if(second||!overlay)shadow+=tap;
 }
 return lerp(1,shadow/12,light.shadowSlots.z);
}
// glow: emissive-surface lights (colour.w < 0, lava). They add texture-
// modulated light after the clamped lighting sum, so their glow stays visible
// under full sunlight instead of vanishing in the saturate of the game's
// vertex-light model; game lamps keep that model unchanged.
// glowOnly: packed (pre-lit) receivers take only the emissive glow.
// ground: the contact blend's ground receiver lit in the same pass (its own
// normal and facing, a white grazing-light material, every lamp): groundLocal
// and groundGlow as a separate call with that receiver gave, except that the
// shared lamp-shadow lookup uses the object's normal offset.
float3 nativePointLighting(NativeOutput i,out float3 glow,bool glowOnly,bool ground,float3 groundNormal,out float3 groundLocal,out float3 groundGlow){
 float3 local=0;glow=0;groundLocal=0;groundGlow=0;
 float3 normal=normalize(i.worldNormal);
 uint2 lightRange=nativePointLightRange(i.worldPosition);
 [loop]for(uint lightSlot=lightRange.x;lightSlot<lightRange.y;++lightSlot){
  NativePointLight light=nativePointLights[nativePointLightGrid[lightSlot]];

  float3 delta=light.centerInverseRadiusSquared.xyz-i.worldPosition;
  float distanceSquared=dot(delta,delta);
  float reach=1-distanceSquared*light.centerInverseRadiusSquared.w;
  if(reach<=0)continue;
  float falloff=reach*reach;
  float3 direction=delta*rsqrt(max(distanceSquared,1e-12));
  float incidence=dot(normal,direction);
  float groundIncidence=ground?dot(groundNormal,direction):0;
  if(light.color.w<0){
   glow+=light.color.rgb*(falloff*saturate(incidence*.6+.4));
   if(ground)groundGlow+=light.color.rgb*(falloff*saturate(groundIncidence*.6+.4));
   continue;
  }
  // Terrain keeps grazing light; figure materials retain their directional response.
  float facing=glowOnly?0:i.pointMaterial.w>.5?saturate(1+min(incidence,0)):max(incidence,0);
  float groundFacing=ground?saturate(1+min(groundIncidence,0)):0;
)" R"(
  if(facing<=0&&groundFacing<=0)continue;
  float visibility=1;
// Low: point shadows off; NATIVE_OFF_POINT_SHADOWS: a specialised program
// bound while no published light holds a shadow slot.
#if !defined(NATIVE_LOW)&&!defined(NATIVE_OFF_POINT_SHADOWS)
  // Emissive surfaces (lava) glow on their own; shadow-testing them against a
  // light sitting at their surface only produces grazing-angle acne.
  // Liquid surfaces never receive point shadows (as in the original); their light
  // often sits at the surface, which only yields grazing acne and stripes.
  if(light.shadowSlots.x>0&&!liquidSurfaceEnabled(i.terrainVertexFlags)&&
     max(i.emissive.r,max(i.emissive.g,i.emissive.b))<.25){
   visibility=nativePointVisibility(light,i.shadowPosition,normal,incidence,shadowBias[5].z);
  }
#endif
  float intensity=light.color.w>0?.65:1;
  local+=light.color.rgb*i.pointMaterial.rgb*(falloff*facing*visibility*intensity);
  groundLocal+=light.color.rgb*(falloff*groundFacing*visibility*intensity);
 }
 return local;
}
float3 nativePointLighting(NativeOutput i,out float3 glow,bool glowOnly){float3 a,b;return nativePointLighting(i,glow,glowOnly,false,0,a,b);}
float3 nativePointLighting(NativeOutput i,out float3 glow){return nativePointLighting(i,glow,false);}
// The previous frame's screen position of this world point, when the previous
// depth there is the same surface (uv for filtered reads, pixel for exact ones).
bool nativeShadowHistoryAt(float3 worldPosition,float3 positionDx,float3 positionDy,out float2 uv,out int2 pixel){
 uv=0;pixel=0;
 if(shadowHistorySettings.w<.5)return false;
 float4 previous=mul(previousWorldClip,float4(worldPosition,1));
 if(previous.w<=0)return false;
 float3 ndc=previous.xyz/previous.w;
 uv=float2(ndc.x*.5+.5,.5-ndc.y*.5);
 if(any(uv<=0)||any(uv>=1)||ndc.z<0||ndc.z>=1)return false;
 pixel=int2(uv*shadowHistorySettings.xy);
 float depth=previousSceneDepth.Load(int3(pixel,0));
 if(depth>=.999999)return false;
 float4 dx=mul(previousWorldClip,float4(positionDx,0));
 float4 dy=mul(previousWorldClip,float4(positionDy,0));
 float2 depthStep=abs(float2(dx.z-ndc.z*dx.w,dy.z-ndc.z*dy.w)/previous.w);
 float threshold=min(.001,max(.00002,2.5*max(depthStep.x,depthStep.y)));
 return abs(depth-ndc.z)<=threshold;
}
// The history reprojects with the camera only, so on a static receiver it
// still holds where a moving caster's shadow was. It may only stand in for
// this frame's estimate within that estimate's own spread: the deviation of
// the soft filter's taps (24 High, 12 otherwise, treated as independent
// lit/shadowed samples) at two sigma, plus one tap. Fully lit or fully
// shadowed pixels then take no stale shadow, while the per-frame disk jitter
// of a static penumbra (within that spread) still averages out.
float nativeShadowHistoryClamp(float current,float history){
 float taps=shadowBias[5].z>1.5?24:12;
 float spread=2*sqrt(saturate(current*(1-current))/taps)+1/taps;
 return clamp(history,current-spread,current+spread);
}
float nativeTemporalShadow(float current,float3 worldPosition,float3 positionDx,float3 positionDy){
 float2 uv;int2 pixel;
 if(!nativeShadowHistoryAt(worldPosition,positionDx,positionDy,uv,pixel))return current;
 float history=previousShadowVisibility.SampleLevel(shadowHistorySampler,uv,0);
 return lerp(current,nativeShadowHistoryClamp(current,history),shadowHistorySettings.z);
}
@CONTACT_BLEND@
// Ground contact: the drawn terrain height (the cover's height grid, t13) and
// colour map (painted from the game's atlases, t14; mip 1 is one texel per
// metre). Unbound or unpainted (alpha 0) leaves the object's colour unchanged.
Texture2D<float> contactGround:register(t13);
Texture2D<float4> contactColor:register(t14);
// The terrain's own art: per 2 m tile its atlas corners (00,10,01 as unorm16
// pairs; the mapping is affine) and material slice (w bits 0-7; 255 none; the
// upper bits name the tile's cliff rock, terrain_cliff.h), t15,
// and the drawn terrain atlases copied per material into one array, t16.
// nativeContactMapMaterials (per batch: the active map's or a neighbour's
// contact maps): bits of the slices already copied.
Texture2D<uint4> contactTiles:register(t15);
Texture2DArray<float4> contactAtlas:register(t16);
SamplerState contactSampler:register(s8);
bool contactTerrainArtMap(float2 s,float2 sDx,float2 sDy,uint2 materials,out float3 art){
 art=0;uint w,h;contactTiles.GetDimensions(w,h);
 float2 t=s*.5;int2 c=int2(floor(t));
 if(any(c<0)||c.x>=int(w)||c.y>=int(h))return false;
 uint4 v=contactTiles.Load(int3(c,0));
 uint m=v.w&255u; // the upper bits carry the cliff rock (terrain_cliff.h)
 if(m>=64u||(((m<32u?materials.x:materials.y)>>(m&31u))&1u)==0)return false;
 float2 uv00=float2(v.x&65535u,v.x>>16)/65535.0;
 float2 du=float2(v.y&65535u,v.y>>16)/65535.0-uv00,dv=float2(v.z&65535u,v.z>>16)/65535.0-uv00;
 float2 f=t-c;
 // Analytic gradients: the tile's affine map times the world footprint, so
 // a quad straddling two tiles keeps its mip.
 art=contactAtlas.SampleGrad(contactSampler,float3(uv00+f.x*du+f.y*dv,m),
  (sDx.x*du+sDx.y*dv)*.5,(sDy.x*du+sDy.y*dv)*.5).rgb;
 return true;
}
bool contactTerrainArt(float2 s,float2 sDx,float2 sDy,out float3 art){
 return contactTerrainArtMap(s,sDx,sDy,nativeContactMapMaterials,art);
}
)" R"(
@TERRAIN_CLIFF@
// Terrain cliffs (terrain_cliff.h): the map's own tile art on unwalkable
// faces, re-read through world-space side projections. A tile's art is one
// atlas cell: its inner rectangle (lo, size; the padding margin around it is
// size/6) and its source, the drawn texture (own) or the contact atlas slice.
struct TerrainCliffCell{float2 lo;float2 size;float slice;bool own;float width;float lowLod;float marginLod;};
float3 terrainCliffTap(TerrainCliffCell k,uint2 ref,float2 q,float2 dx,float2 dy){
 float2 uv=k.lo+q*k.size;dx*=k.size;dy*=k.size;
 [branch]if(k.own)return terrainTapGrad(ref,uv,dx,dy).rgb;
 return contactAtlas.SampleGrad(contactSampler,float3(uv,k.slice),dx,dy).rgb;
}
float3 terrainCliffTapLevel(TerrainCliffCell k,uint2 ref,float2 q,float lod){
 float2 uv=k.lo+q*k.size;
 [branch]if(k.own)return terrainTapLevel(ref,uv,lod).rgb;
 return contactAtlas.SampleLevel(contactSampler,float3(uv,k.slice),lod).rgb;
}
// Coarse level (terrainLowpassTexels a tile) and margin level of a cell in a
// texture of `width` texels.
void terrainCliffCellLevels(inout TerrainCliffCell k,float width){
 k.width=width;float texels=k.size.x*width;k.lowLod=log2(max(texels/terrainLowpassTexels,1));k.marginLod=log2(max(texels/6,1));
}
// The neighbouring 2 m tile c from the contact tile grid (any rotation: the
// axis-aligned inner rectangle). False when absent or its slice is not copied
// yet; `authored` when it is a long (non-square) tile, which keeps its
// authored mapping and so takes no projected detail across its edge.
bool terrainCliffContactCell(int2 c,out TerrainCliffCell k,out bool authored){
 k.lo=0;k.size=0;k.slice=0;k.own=false;k.width=0;k.lowLod=0;k.marginLod=0;authored=false;
 uint w,h;contactTiles.GetDimensions(w,h);
 if(any(c<0)||c.x>=int(w)||c.y>=int(h))return false;
 uint4 v=contactTiles.Load(int3(c,0));uint m=v.w&255u;
 if(m>=64u||(((m<32u?nativeContactMapMaterials.x:nativeContactMapMaterials.y)>>(m&31u))&1u)==0)return false;
 float2 a=float2(v.x&65535u,v.x>>16)/65535.0,b=float2(v.y&65535u,v.y>>16)/65535.0,d=float2(v.z&65535u,v.z>>16)/65535.0,e=b+d-a;
 k.lo=min(min(a,b),min(d,e));k.size=max(max(a,b),max(d,e))-k.lo;k.slice=m;
 float aw,ah,ae,levels;contactAtlas.GetDimensions(0,aw,ah,ae,levels);
 terrainCliffCellLevels(k,aw);
 float aspect=k.size.x*aw/max(k.size.y*ah,1e-6);
 authored=aspect<.99||aspect>1.01;
 return all(k.size>0)&&!authored;
}
// The detail (fine minus coarse) of one tile through one side plane (s and
// its derivatives in tile units), read through the terrain's turned and
// shifted hex cells (the same world cells for every tile, so two tiles'
// reads line up). Only reads whose coarse colour matches the pixel's are
// kept (rock detail stays on rock, grass on grass), renormalised with
// variance preservation and weighted by the matched share (w, 0..1).
// The whole tile through one side plane (s and its derivatives in tile
// units): turned and shifted hex reads blended with variance kept about
// their coarse colour. The footprint is held inside the cell's padding
// instead of falling back to the stretched authored image. w is 1 where read.
// Far faces: once the footprint passes what the cell's padding holds, the
// hex reads stay at the padding's level and a big distant face shows the
// tile repeating. There the colour cross-fades (over terrainCliffFarRange
// LODs) to the tile read at a larger scale, turned off the tile axes through
// a mirrored repeat (continuous at every repeat line), as the blend's far
// face reads do. One level tap.
static const float terrainCliffFarScale=.4;  // Frequency of the larger-scale read (tiles per tile). @param 0.1 1
static const float terrainCliffFarRange=3;   // LODs past the padding's level over which it takes over. @param 0.5 6
float3 terrainCliffFar(TerrainCliffCell k,uint2 ref,float2 s,float3 color,float lodRaw){
 float far=smoothstep(0,terrainCliffFarRange,lodRaw-(k.marginLod-.5));
 [branch]if(far<=.001)return color;
 float2 m=mul(float2x2(.8,-.6,.6,.8),s*terrainCliffFarScale)+float2(11.3,-7.9),t=frac(m*.5)*2,q=t<1?t:2-t;
 return lerp(color,terrainCliffTapLevel(k,ref,q,k.marginLod-.5),far);
}
float4 terrainCliffDetail(TerrainCliffCell k,uint2 ref,float2 s,float2 sDx,float2 sDy,float3 coarse){
 float3 w;int2 v1,v2,v3;terrainHexGrid(s*terrainHexLattice,w,v1,v2,v3);w=terrainHexWeights(w);
 int2 cells[3]={v1,v2,v3};
 float lod=log2(max(max(length(sDx),length(sDy))*k.size.x*k.width,1e-8)),lodRaw=lod;
 float hold=exp2(min(k.marginLod-.5-lod,0));sDx*=hold;sDy*=hold;lod=min(lod,k.marginLod-.5);
 float3 sum=0,lows=0;float total=0,squares=0;
 [unroll]for(int j=0;j<3;++j){
  // Cells under 1/64 of the blend are not read.
  [branch]if(w[j]>=1.0/64){
   float2x2 r=terrainHexRotation(cells[j]);
   float2 q=frac(terrainHexSampleCoord(s,cells[j]));
   float3 low=terrainCliffTapLevel(k,ref,q,max(k.lowLod,lod));
   float3 detail=terrainCliffTap(k,ref,q,mul(r,sDx),mul(r,sDy))-low;
   sum+=w[j]*detail;lows+=w[j]*low;total+=w[j];squares+=w[j]*w[j];
  }
 }
 return squares>0?float4(terrainCliffFar(k,ref,s,lows/total+sum*min(rsqrt(squares),terrainDetailGainLimit/max(total,1e-4)),lodRaw),1):0;
}
// Fast cliffs (nativeTerrainCliff.x 2: terrain detail below high, which has
// no anti-tiling): one plain tap of the tile repeated in world space per
// side plane, like the fast blend's face reads (the 2 m repeat shows). The
// footprint stays inside the cell's padding.
float4 terrainCliffFastDetail(TerrainCliffCell k,uint2 ref,float2 s,float2 sDx,float2 sDy){
 float lod=log2(max(max(length(sDx),length(sDy))*k.size.x*k.width,1e-8));
 float hold=exp2(min(k.marginLod-.5-lod,0));
 return float4(terrainCliffTap(k,ref,frac(s),sDx*hold,sDy*hold),1);
}
// The cliff colour of a terrain pixel: its own tile read whole through the
// side planes (never the authored top-down image, which stretches down a
// face) and, within terrainCliffEdgeBand of a tile edge, cross-faded into the
// neighbouring tile's (the same mix on both sides, so no edge shows).
// Walkable ground keeps t. p is map-local (grid lookups), pa the material
// layout frame (side-plane reads; nativeTerrainMaterialStages layoutP).
float3 terrainCliffColor(uint2 ref,float2 uv,float2 uvDx,float2 uvDy,float tiles,float3 p,float3 pa,float3 pDx,float3 pDy,float3 slope,float2 facets,float3 t){
 float3 planes=terrainCliffProjectionPlanes(slope,facets);
 float side=planes.x+planes.y;
 if(side<=.002||tiles<1)return t;
 float levels;float2 texels=terrainTapSize(ref,levels);
 TerrainCliffCell own;own.own=true;own.slice=0;
 own.lo=(floor(uv*tiles)+terrainTileMargin)/tiles;own.size=(1-2*terrainTileMargin)/tiles;
 terrainCliffCellLevels(own,texels.x);
 if(own.size.x*texels.x<24||levels<own.lowLod+1)return t;
 // The authored coarse image at the pixel (never coarser than its footprint).
 float fine=log2(max(max(length(uvDx*texels),length(uvDy*texels)),1e-8));
 float3 coarse=terrainTapLevel(ref,uv,max(own.lowLod,fine)).rgb;
 float fade=1;
 float2 u=frac(p.xy*.5);
 int2 c=int2(floor(p.xy*.5)),toward=int2(u.x<.5?-1:1,u.y<.5?-1:1);
 float2 share=terrainCliffEdgeShare(u);
 // This tile (c0) and its neighbours across x, y and the corner (c1-c3),
 // with their shares in weight.xyzw (no arrays: a loop rotates them).
 TerrainCliffCell c0=own,c1=own,c2=own,c3=own;float4 weight=0;bool3 authored=false;
 // Neighbours are looked up only inside the edge band.
 [branch]if(share.x>0&&terrainCliffContactCell(c+int2(toward.x,0),c1,authored.x))weight.y=share.x*(1-share.y);
 [branch]if(share.y>0&&terrainCliffContactCell(c+int2(0,toward.y),c2,authored.y))weight.z=(1-share.x)*share.y;
 [branch]if(share.x*share.y>0&&terrainCliffContactCell(c+toward,c3,authored.z))weight.w=share.x*share.y;
 // A missing neighbour's share stays with this tile (its own art, never black).
 weight.x=1-weight.y-weight.z-weight.w;
 // Toward a long tile (authored mapping on its side) the projection yields to
 // the authored detail, reaching it on the shared edge.
 if(authored.x)fade*=1-2*share.x;
 if(authored.y)fade*=1-2*share.y;
 if(authored.z)fade*=1-4*share.x*share.y;
 float3 detail=0;float matched=0;
 bool fast=nativeTerrainCliff.x>1.5;
 // One copy of the side-plane read: step k reads tile c0 (rotated every
 // second step) through the YZ plane (even k) or the XZ plane (odd k).
 // Unrolled, the hex read was inlined eight times.
 [loop]for(uint k=0;k<8u;++k){
  bool second=(k&1u)!=0;float plane=second?planes.y:planes.x;
  [branch]if(weight.x>0&&plane>0){
   float2 fs=second?pa.xz*.5:pa.yz*.5,fdx=second?pDx.xz*.5:pDx.yz*.5,fdy=second?pDy.xz*.5:pDy.yz*.5;
   float4 d;
   [branch]if(fast)d=terrainCliffFastDetail(c0,ref,fs,fdx,fdy);
   else d=terrainCliffDetail(c0,ref,fs,fdx,fdy,coarse);
   float tileShare=weight.x*plane/side;
   detail+=tileShare*d.rgb;matched+=tileShare*d.w;
  }
  [branch]if(second){TerrainCliffCell rotated=c0;c0=c1;c1=c2;c2=c3;c3=rotated;weight=weight.yzwx;}
 }
 float3 cliff=saturate(matched>0?detail/matched:t);
 return lerp(t,cliff,side*fade);
}
@TERRAIN_TILE_BLEND@
)" R"(
@TERRAIN_BAKE@
float3 contactBlendColor(float3 p,float3 n,float3 pDx,float3 pDy,float worldHeight,out float2 blend){
 blend=float2(0,1);
 float band=contactBandHeight(worldHeight,nativeContactBlend.y);
 p=contactLocalPosition(p,nativeContactOrigin.xyz);
 uint w,h,levels;uint colorMip=uint(nativeContactOrigin.w);contactColor.GetDimensions(colorMip,w,h,levels);
 if(w==0)return 0; // ground cover maps not bound this frame
 float rise=p.z-gcHeight(contactGround,p.xy);
 if(rise>max(band*contactBlendTop,.15))return 0;
 n=normalize(n);
 // Walls read the ground in front of them turned up (continuous with it at
 // the contact line); upward faces read the ground below them.
 float2 fold=contactFoldDirection(n);
 float2 s=p.xy+fold*max(rise,0);
 float2 sDx=pDx.xy+fold*pDx.z,sDy=pDy.xy+fold*pDy.z;
 float strength=nativeContactBlend.x;
 float noise=contactNoise(s);
 // Above every possible reach only the contact shading remains: no ground reads.
 [branch]if(rise>=contactReachBound(band,n.z,noise))
 {blend.y=lerp(1,contactBlendWeight(rise,band,n.z,noise,0,0).y,strength);return 0;}
 float2 q=clamp(s-.5,0,float2(w,h)-1.001);int2 c=int2(floor(q));float2 f=q-c;
 float4 ground=lerp(lerp(contactColor.Load(int3(c,colorMip)),contactColor.Load(int3(c+int2(1,0),colorMip)),f.x),
                    lerp(contactColor.Load(int3(c+int2(0,1),colorMip)),contactColor.Load(int3(c+int2(1,1),colorMip)),f.x),f.y);
 if(ground.a<.99)return 0;
 float3 art;
 float3 terrain=ground.rgb;float detail=0;
 [branch]if(contactTerrainArt(s,sDx,sDy,art)){
  // Large-scale brightness of the terrain's anti-tiling stage, when enabled.
  if(nativeTerrainMaterial.y>0)art*=terrainMacro(s+nativeContactLayout.xy); // the terrain's own frame
  float3 luma=float3(.2126,.7152,.0722);
  detail=dot(art,luma)-dot(ground.rgb,luma);
  terrain=art;
 }
 float2 k=contactBlendWeight(rise,band,n.z,noise,contactGrass(ground.rgb),detail);
 blend=float2(k.x*strength,lerp(1,k.y,strength));
 return saturate(terrain);
}
)" R"(
#ifdef NATIVE_TERRAIN_PS
// Terrain-only material stages (blend, terrain material, cliffs) live in the terrain pixel variant: in the shared shader their
// registers slowed every figure and object pixel. The live terrain pixel and
// the material cache's bake (terrain_bake.h) run this same function; the
// bake passes its own page-space derivatives. debugView: the temporary blend
// diagnostic view, returned unlit and untinted.
// Every blend and material decision (field lookup, face warps and side-plane
// reads, rock noise, hex and anti-tiling cells, noise and score positions)
// reads the untessellated surface: i.shadowPosition, the base patch position
// the domain stage interpolates linearly, and its screen derivatives
// (restDx, restDy). The tessellated position is lifted by an amount that
// follows the distance-based tessellation factor, so reading it moved the
// material boundaries with the camera. Untessellated, the two positions are
// the same. Lighting and normals keep the drawn surface.
float4 nativeTerrainMaterialStages(NativeOutput i,float4 t,float3 restDx,float3 restDy,float2 uvDx,float2 uvDy,out bool debugView){
  debugView=false;
  // Temporary diagnostic (terrain_blend_debug 5): the pixel's material class,
  // green material base tile, magenta long tile, cyan animated, red other.
  [branch]if(nativeTerrainTess.y>4.5&&nativeTerrainTess.y<5.5){
   debugView=true;uint f=i.terrainVertexFlags;
   return float4((f&16u)!=0?float3(0,1,0):(f&4096u)!=0?float3(1,0,1):(f&4u)!=0?float3(0,1,1):float3(1,0,0),1);
  }
  // A neighbour map reads its own tile grid and atlas (bound per batch with
  // its contact maps) at its own origin, nativeContactOrigin (0 on the
  // active map): tileP is map-local. The material layouts (noise, hex cells,
  // world-space tile reads, far reads) use layoutP, map-local plus the map's
  // saved-layout origin on whole tiles (nativeContactLayout), so a map draws
  // the same whether it is the active map or a neighbour.
  float3 tileP=i.shadowPosition-nativeContactOrigin.xyz;
  float3 layoutP=float3(tileP.xy+nativeContactLayout.xy,tileP.z);
  // Long tiles: only the far de-tiling (terrain_material.h).
  [branch]if((i.terrainVertexFlags&4096u)!=0){
   [branch]if(nativeTextured!=0&&nativeTerrainMaterial.y>0)t=terrainLongFar(i.textureReference,i.uv,uvDx,uvDy,layoutP,i.terrainTiles,t);
   return t;
  }
  // Terrain cliffs (terrain_cliff.h) own the side projection of the map's
  // terrain while on; the terrain material never projects faces.
  // Neighbour ground under nearly full seam fog (specular-alpha fog factor
  // below 2%) shows the fog colour: blend and cliffs would cost the most
  // expensive terrain path for no visible change, so it keeps its tile art.
  bool fogged=i.neighbour!=0&&i.s.a<.02&&nativeContactTail==0; // tail: temporary A/B switch
#ifdef NATIVE_TERRAIN_FLAT
  bool cliffs=false; // flat tile class: no side projection anywhere
#else
  bool cliffs=nativeTextured!=0&&(i.terrainVertexFlags&16)!=0&&nativeTerrainCliff.x>0&&i.terrainTiles>=1&&!fogged;
#endif
  // Transition tiles: their parent materials blended in world space
  // (terrain_tile_blend.h), before cliffs re-read steep faces.
  // The blend comes first and reports how much of the finished tile colour
  // it still shows (own art of plain ground, distance fade, edges toward
  // tiles that keep their art); the terrain material stage runs only when
  // that share is not zero.
  bool blendFaces=false;float4 blend=float4(0,0,0,1);
  [branch]if(nativeTextured!=0&&(i.terrainVertexFlags&16)!=0&&nativeTerrainCliff.z>0&&!fogged)
   blend=terrainTileBlendColor(tileP,restDx,restDy,normalize(i.terrainSlope),i.terrainFlat,t.rgb,i.bedLiquid.x,blendFaces);
  [branch]if(blend.a<0){debugView=true;return float4(blend.rgb,1);}
  [branch]if(blend.a>.001&&nativeTextured!=0&&(i.terrainVertexFlags&16)!=0&&i.terrainTiles>=1&&any(nativeTerrainMaterial>0))
   t=terrainMaterialColor(i.textureReference,i.uv,uvDx,uvDy,layoutP,restDx,restDy,i.terrainTiles,(i.terrainVertexFlags>>8)&15u,
    nativeTerrainMaterial,t);
  // Unwalkable faces read the map's own tile art through world-space side
  // projections instead of stretching it; walkable ground is unchanged. It
  // applies to the tile-art share the blend still shows (all of it without
  // the blend), so no stretched image reaches a face through that share.
  [branch]if(cliffs&&blend.a>.001)t.rgb=terrainCliffColor(i.textureReference,i.uv,uvDx,uvDy,i.terrainTiles,tileP,layoutP,restDx,restDy,normalize(i.terrainSlope),i.terrainFlat,t.rgb);
  t.rgb=blend.rgb+blend.a*t.rgb;
  return t;
}
#endif
)" R"(
// Depth prepass and sun-shadow mask (renderer.cpp flush_native). Opaque and
// overwrite-cutout batches of a world flush first draw depth only, marking
// stencil bit 0x80 and writing their sun-shadow receiver record:
//  t1 (prepass target 0, RGBA16F): xyz the rest (shadow-lookup) position
//     minus the depth-reconstructed point, w flags (1 receiver, 2 temporal,
//     4 terrain normal offset);
//  t2 (prepass target 1, RG16 UNORM): the smooth terrain slope normal,
//     octahedral.
// The mask pass (full screen, stencil 0x80, which it clears) rebuilds the
// rest position from depth (t4) plus that offset, takes the sun shadow the
// material pass would (cascade choice and blend, filter, temporal history)
// and the cloud shadow, and writes t5 (RG16F): x the combined visibility
// (2: the material pass would not shade), y the sun visibility alone. The
// material variants with NATIVE_SHADOW_MASK read t5 at their pixel with
// depth EQUAL. These PS slots hold vertex-stage buffers only in the vertex
// stage; no pixel entry reads both.
// The depth prepass stages' output (the values nativePrepassPS reads; see
// nativePrepassVS).
struct NativeDepthOutput{float4 p:SV_Position;float4 d:COLOR0;float4 s:COLOR1;float2 uv:TEXCOORD0;nointerpolation uint2 textureReference:TEXCOORD1;
 float3 worldPosition:TEXCOORD2;nointerpolation uint shadowMode:TEXCOORD6;nointerpolation uint terrainVertexFlags:TEXCOORD13;
 float3 terrainSlope:TEXCOORD17;nointerpolation uint swaying:TEXCOORD19;float3 shadowPosition:TEXCOORD20;nointerpolation uint neighbour:TEXCOORD21;
 // Crown impostor frames (nativeVertex).
 float4 liquidTintDepth:TEXCOORD8;nointerpolation float4 liquidFlow:TEXCOORD9;float4 liquidMaterial:TEXCOORD10;nointerpolation float4 liquidUvBounds:TEXCOORD14;};
NativeDepthOutput nativeDepthOf(NativeOutput o){
 NativeDepthOutput r;r.p=o.p;r.d=o.d;r.s=o.s;r.uv=o.uv;r.textureReference=o.textureReference;r.worldPosition=o.worldPosition;
 r.shadowMode=o.shadowMode;r.terrainVertexFlags=o.terrainVertexFlags;r.terrainSlope=o.terrainSlope;r.swaying=o.swaying;
 r.shadowPosition=o.shadowPosition;r.neighbour=o.neighbour;
 r.liquidTintDepth=o.liquidTintDepth;r.liquidFlow=o.liquidFlow;r.liquidMaterial=o.liquidMaterial;r.liquidUvBounds=o.liquidUvBounds;return r;
}
Texture2D<float4> nativeReceiverRest:register(t1);
Texture2D<float2> nativeReceiverNormal:register(t2);
Texture2D<float> nativeSceneDepth:register(t4);
Texture2D<float2> nativeShadowMask:register(t5);
float2 nativeOctEncode(float3 n){
 n/=abs(n.x)+abs(n.y)+abs(n.z);
 float2 e=n.z>=0?n.xy:(1-abs(n.yx))*float2(n.x>=0?1:-1,n.y>=0?1:-1);
 return e*.5+.5;
}
float3 nativeOctDecode(float2 e){
 e=e*2-1;float3 n=float3(e,1-abs(e.x)-abs(e.y));
 float t=saturate(-n.z);n.xy+=float2(n.x>=0?-t:t,n.y>=0?-t:t);
 return normalize(n);
}
// The depth the D24 target stores for z (round to nearest), so the offset
// written here cancels the reconstruction exactly in the mask pass.
float nativeDepth24(float z){return round(saturate(z)*16777215.0)/16777215.0;}
struct NativePrepassResult{float4 rest:SV_Target0;float2 normal:SV_Target1;};
// Static LOD crossfade (nativeStaticLodSelect, code in swaying bits 8-17):
// a level in its band keeps the pixels the other level leaves, by a fixed
// per-pixel threshold (interleaved gradient noise), so the two levels cover
// each pixel once and the prepass and the material pass agree.
// Crown impostor texel (static_lod.h): the four frames' albedo (premultiplied
// by coverage) and object-space normal, blended by the record's weights; a
// frame position outside its image is empty. Coverage of at least one half
// passes the record's own alpha test (alpha 1). The sampler is the batch's
// own (bilinear, mips); positions stay inside their frame's image.
Texture2DArray nativeImpostorAlbedo:register(t44);
Texture2DArray nativeImpostorNormal:register(t45);
// Mip of the frame images from the first frame's footprint (taken outside
// any branch: the impostor flag differs between neighbouring triangles).
float nativeImpostorLod(float2 local){
 float2 dx=ddx(local),dy=ddy(local);
 return clamp(log2(max(max(length(dx),length(dy))*kImpostorFrameTexels,1e-6)),0,kImpostorMips-1);
}
float4 nativeImpostorTexel(float4 frames01,float4 frames23,float4 weights,float4 flow,float lod,bool withNormal,out float3 normal){
 uint slice=(uint)(flow.x+.5);uint2 b=uint2((uint)(flow.y+.5),(uint)(flow.z+.5));
 float2 uvs[4]={frames01.xy,frames01.zw,frames23.xy,frames23.zw};
 float4 albedo=0;float3 n=0;float coverage=0;
 [unroll]for(uint k=0;k<4;++k){
  float2 frame=float2(b+uint2(k&1u,k>>1));
  float2 local=uvs[k];
  float3 at=float3((frame+saturate(local))/kImpostorFrames,slice);
  bool inside=all(local>=0)&&all(local<=1);
  [branch]if(inside&&weights[k]>0){
   albedo+=nativeImpostorAlbedo.SampleLevel(nativeSampler,at,lod)*weights[k];
   // Coverage from the full-resolution image: the averaged coverage of the
   // mips falls below one half wherever leaves are sparser than that, so a
   // far crown lost every pixel. One point of the baked image per pixel keeps
   // the baked density, as the crown's own alpha test does.
   coverage+=nativeImpostorAlbedo.SampleLevel(nativeSampler,at,0).a*weights[k];
   if(withNormal)n+=nativeImpostorNormal.SampleLevel(nativeSampler,at,lod).xyz*weights[k];
  }
 }
 // Both images are premultiplied by coverage (empty texels hold zero).
 normal=albedo.a>1e-4?n/albedo.a*2-1:0;
 // Diagnostic view (F10 impostor_debug): the whole quad, magenta.
 if(flow.w>.5){normal=float3(0,0,1);return float4(1,0,1,1);}
 return float4(albedo.a>1e-4?albedo.rgb/albedo.a:0,coverage>=.5?1:0);
}
bool nativeLodDiscard(uint swaying,float4 position){
 uint code=swaying>>8;
 if((code&512u)==0)return false;
 float noise=frac(52.9829189*frac(dot(floor(position.xy),float2(.06711056,.00583715))));
 float coverage=(code&255u)/255.0;
 return (code&256u)!=0?noise>=coverage:noise<coverage;
}
NativePrepassResult nativePrepassPS(NativeDepthOutput i){
 if(nativeLodDiscard(i.swaying,i.p))discard;
 float impostorLod=nativeImpostorLod(i.liquidTintDepth.xy);
 // The discards of nativePS that decide coverage: the neighbour interior,
 // the alpha test (the same texture read; terrain batches with an alpha
 // test stay out of the prepass) and the draw-distance dissolve.
 if(i.neighbour!=0 && all(i.worldPosition.xy>nativeTerrainActiveBoundsPS.xy) &&
    all(i.worldPosition.xy<nativeTerrainActiveBoundsPS.zw))discard;
 [branch]if(nativeAlphaTest!=0){
  float4 t=float4(1,1,1,1);
  [branch]if(((i.swaying>>8)&1024u)!=0){float3 unused;t=nativeImpostorTexel(i.liquidTintDepth,i.liquidMaterial,i.liquidUvBounds,i.liquidFlow,impostorLod,false,unused);}
  else{
#ifdef NATIVE_LOW
  [branch]if(nativeTextured!=0){
   float2 textureDx=ddx_coarse(i.uv),textureDy=ddy_coarse(i.uv);
   [branch]if(i.textureReference.y!=0)t=nativeTextureArray.SampleGrad(nativeSampler,float3(i.uv,i.textureReference.x),textureDx,textureDy);
   else t=nativeTexture.SampleGrad(nativeSampler,i.uv,textureDx,textureDy);
  }
#else
  if(nativeTextured!=0)t=i.textureReference.y!=0?nativeTextureArray.Sample(nativeSampler,float3(i.uv,i.textureReference.x)):nativeTexture.Sample(nativeSampler,i.uv);
#endif
  }
  if(!nativeCompare(nativeCombineAlpha(i.d,i.s,t)))discard;
 }
 float3 fogPoint=i.worldPosition;
 [branch]if(i.shadowMode==0&&(fogStock.w>0||fogEye.w>.5))fogPoint=fogUnproject(i.p.xy*fogScreen.xy,i.p.z);
 if(nativeFogEnable==0&&nativeDrawDissolvedAt(i.p,fogPoint))discard;
 bool receiver=i.shadowMode==1||(i.shadowMode==2&&nativeRasterTail.y!=0);
 bool lifted=(i.terrainVertexFlags&16)!=0&&dot(i.terrainSlope,i.terrainSlope)>1e-8;
 uint flags=(receiver?1u:0u)|(nativeRasterTail.x==0&&(i.swaying&255u)==0?2u:0u)|(lifted?4u:0u);
 // Snow trail troughs and berms (nativeTessGroundPosition) are drawn off the
 // untessellated terrain the sun maps hold. The receiver follows the drawn
 // point along its ray to the sun up to that terrain, so a shadow lands on
 // the trough floor where the drawn surface meets it, and the terrain in the
 // map never shadows its own trough. Only where the drawn point left the
 // rest point (the snow stages ran; untessellated trails stay at rest).
 float3 rest=i.shadowPosition;
 [branch]if((i.terrainVertexFlags&16)!=0&&i.neighbour==0&&nativeSnowTrailWindow.w>0&&abs(i.worldPosition.z-rest.z)>1e-5&&nativeWorldSunDirection.z>0){
  float h=snowTrailHeight(snowTrailRaw(nativeSnowTrail,nativeSnowTrailWindow,nativeSnowTrailTiles,i.worldPosition.xy));
  float3 sun=normalize(nativeWorldSunDirection.xyz);
  rest.xy-=h*sun.xy/max(sun.z,.25);
 }
 NativePrepassResult r;
 r.rest=float4(rest-fogUnproject(i.p.xy*fogScreen.xy,nativeDepth24(i.p.z)),flags);
 r.normal=lifted?nativeOctEncode(normalize(i.terrainSlope)):float2(.5,.5);
 return r;
}
float4 nativeShadowMaskVS(uint id:SV_VertexID):SV_Position{
 float2 uv=float2((id<<1)&2,id&2);return float4(uv*float2(2,-2)+float2(-1,1),0,1);
}
// One receiver texel: the drawn position (the scene depth reconstructed),
// the rest position (drawn plus the prepass offset) and the prepass flags
// (valid false: off the target or the far plane). The mask loads each texel
// once: the rest-position steps and the temporal history's drawn-position
// steps read the same depth texels.
struct NativeReceiverTexel{bool valid;float3 drawn;float3 rest;uint flags;};
NativeReceiverTexel nativeReceiverTexel(int2 c,int2 size){
 NativeReceiverTexel t;t.valid=false;t.drawn=0;t.rest=0;t.flags=0;
 if(any(c<0)||c.x>=size.x||c.y>=size.y)return t;
 float z=nativeSceneDepth.Load(int3(c,0));
 if(z>=1)return t;
 float4 r=nativeReceiverRest.Load(int3(c,0));
 t.valid=true;t.flags=uint(r.w);
 t.drawn=fogUnproject((float2(c)+.5)*fogScreen.xy,z);t.rest=t.drawn+r.xyz;
 return t;
}
// One screen-space derivative: the neighbour step along the same surface
// (the smaller of the two one-sided differences; ddx in nativePS
// differences the quad's own pixels). a: the texel at +axis, b: at -axis.
float3 nativeTexelStep(bool hasA,float3 a,bool hasB,float3 b,float3 centre){
 float3 forward=a-centre,backward=centre-b;
 if(hasA&&hasB)return dot(forward,forward)<=dot(backward,backward)?forward:backward;
 return hasA?forward:hasB?backward:float3(0,0,0);
}
float2 nativeShadowMaskPS(float4 position:SV_Position):SV_Target{
 int2 c=int2(position.xy);
 uint w,h;nativeSceneDepth.GetDimensions(w,h);int2 size=int2(w,h);
 NativeReceiverTexel centre=nativeReceiverTexel(c,size);
 if(!centre.valid||(centre.flags&1u)==0)return float2(2,1);
 float3 rest=centre.rest;uint flags=centre.flags;
 NativeReceiverTexel px=nativeReceiverTexel(c+int2(1,0),size),nx=nativeReceiverTexel(c-int2(1,0),size),
                     py=nativeReceiverTexel(c+int2(0,1),size),ny=nativeReceiverTexel(c-int2(0,1),size);
 float3 shadowDx=nativeTexelStep(px.valid,px.rest,nx.valid,nx.rest,rest),shadowDy=nativeTexelStep(py.valid,py.rest,ny.valid,ny.rest,rest);
 bool sunShadow=all(isfinite(shadowDx))&&all(isfinite(shadowDy))&&NATIVE_SUN_SHADOWS_LIVE;
 float cloud=skyCloudVisibility(rest);
 [branch]if(!sunShadow&&cloud>=1)return float2(2,1);
 float visibility=1,filtered=1;
 [branch]if(sunShadow){
  bool temporal=(flags&2u)!=0;
  float3 lookup=rest;
  if((flags&4u)!=0&&dot(nativeWorldSunDirection.xyz,nativeWorldSunDirection.xyz)>1e-12)
   lookup=terrainShadowLookup(rest,nativeOctDecode(nativeReceiverNormal.Load(int3(c,0))),normalize(nativeWorldSunDirection.xyz));
#ifdef NATIVE_LOW
  visibility=nativeLowShadowVisibility(lookup,shadowDx,shadowDy);
#else
  visibility=nativeShadowVisibility(lookup,shadowDx,shadowDy,position.xy,temporal);
#endif
  // The history reprojects the drawn surface, as nativePS did with its
  // interpolated world position: on tessellated terrain the rest position
  // lies off it, so its depth test dropped or misplaced the history.
  [branch]if(visibility>=0&&temporal){
   float3 drawn=centre.drawn;
   visibility=nativeTemporalShadow(visibility,drawn,nativeTexelStep(px.valid,px.drawn,nx.valid,nx.drawn,drawn),
    nativeTexelStep(py.valid,py.drawn,ny.valid,ny.drawn,drawn));
  }
  filtered=visibility;
 }
 if(visibility>=0)visibility=min(visibility,cloud);
 return float2(visibility,filtered);
}
)" R"(
// ambient: the part of color contributed by the ambient light, for AO (see ambient_occlusion.h).
struct NativePixelResult{float4 color:SV_Target0;float4 shadow:SV_Target1;float4 ambient:SV_Target2;};
NativePixelResult nativePS(NativeOutput i){
 if(nativeLodDiscard(i.swaying,i.p))discard;
 // Crown impostor: the baked albedo replaces the texture read below, and its
 // object-space normal relights the pixel exactly as the vertex stage lights
 // a crown vertex (calculateWorldLighting: ambient, sun, emissive).
 bool impostor=((i.swaying>>8)&1024u)!=0;
 float4 impostorTexel=float4(1,1,1,1);float3 impostorNormal=float3(0,0,1);
 float impostorLod=nativeImpostorLod(i.liquidTintDepth.xy);
 [branch]if(impostor){
  float3 n;impostorTexel=nativeImpostorTexel(i.liquidTintDepth,i.liquidMaterial,i.liquidUvBounds,i.liquidFlow,impostorLod,true,n);
  n=dot(n,n)>1e-8?normalize(n):float3(0,0,1);impostorNormal=n;
  float facing=i.liquidDirections.w>0?(nativeTerrainClock.z!=0?1:max(dot(n,i.liquidView),0)):0;
  i.directional=nativeWorldSunDiffuse.rgb*i.liquidDirections.xyz*facing;
  i.d.rgb=saturate(max(nativeWorldAmbient.rgb,i.directional)+i.emissive);
 }
 // Background geometry stays outside the authoritative map's open interior.
 if(i.neighbour!=0 && all(i.worldPosition.xy>nativeTerrainActiveBoundsPS.xy) &&
    all(i.worldPosition.xy<nativeTerrainActiveBoundsPS.zw))discard;
 // Evaluate derivatives before both the receiver branch and per-pixel
 // cascade selection. Adjacent lanes need not select the same receiver/map.
 float3 positionDx=ddx(i.worldPosition),positionDy=ddy(i.worldPosition);
 float2 uvDx=ddx(i.uv),uvDy=ddy(i.uv);
 // Sun-shadow receiver plane and lookups use the rest geometry (identical to
 // the drawn position everywhere except on swaying foliage).
 float3 shadowDx=ddx(i.shadowPosition),shadowDy=ddy(i.shadowPosition);
#if defined(NATIVE_TERRAIN_PS)||defined(NATIVE_TERRAIN_BAKED)
 // Material cache routing (terrain_bake.h). Two-pass (b13.x 1): the baked
 // variant draws first and keeps the pixels the cache covers; the live
 // variant then skips exactly those (strict test), before any lighting or
 // material work. Single-pass (b13.x 2): the live variant draws every pixel
 // once and uses the cached colour instead of the material stages where the
 // cache covers (loose test), so the geometry and tessellation run once.
 bool terrainBakeCovered=false;float3 terrainBakeColor=0;
 [branch]if(terrainBakeAtlas.z>0&&terrainBakeCorner.x>=1u&&nativeTextured!=0&&(i.terrainVertexFlags&(16u|4096u))!=0)
#ifdef NATIVE_TERRAIN_BAKED
  terrainBakeCovered=terrainBakeTap(i.shadowPosition.xy,shadowDx.xy,shadowDy.xy,i.p.xy,terrainBakeColor,false);
 if(!terrainBakeCovered)discard;
#else
  terrainBakeCovered=terrainBakeTap(i.shadowPosition.xy,shadowDx.xy,shadowDy.xy,i.p.xy,terrainBakeColor,terrainBakeCorner.x==1u);
 if(terrainBakeCovered&&terrainBakeCorner.x==1u)discard;
#endif
#endif
#ifdef NATIVE_TERRAIN_PS
 // The terrain material runs before the lighting: it reads only the
 // interpolated inputs (never the lit colour), and none of the lighting's
 // values are held in registers across its stages.
#ifndef NATIVE_LOW
  float4 t=float4(1,1,1,1);if(impostor)t=impostorTexel;else if(nativeTextured!=0)t=i.textureReference.y!=0?nativeTextureArray.Sample(nativeSampler,float3(i.uv,i.textureReference.x)):nativeTexture.Sample(nativeSampler,i.uv);
#endif
  // Single-pass routing: the cached colour where the cache covers.
  [branch]if(terrainBakeCovered)t.rgb=terrainBakeColor;
  else{bool debugView;t=nativeTerrainMaterialStages(i,t,shadowDx,shadowDy,uvDx,uvDy,debugView);
   // Temporary blend diagnostic view: unlit, untinted.
   [branch]if(debugView){NativePixelResult debugResult;debugResult.color=t;debugResult.shadow=float4(1,0,0,1);debugResult.ambient=float4(0,0,0,1);return debugResult;}}
#endif
 // Liquid surfaces never reach this entry: liquid batches draw their liquid
 // triangles through nativeLiquidPS and the rest through this one (the
 // vertex stage collapses the other part, nativeLiquidLayer bits 1 and 2).
#ifdef NATIVE_LOW
 // Low: one texture read (the shared line's ternary read both the array and
 // the plain texture), with the hardware's per-quad gradients. The alpha test
 // runs before the lighting, so pixels it discards (foliage cards) skip the
 // point lights and the sun shadow; alpha never depends on the lighting, which
 // only changes i.d.rgb.
 float4 t=float4(1,1,1,1);
 [branch]if(impostor)t=impostorTexel;
 else [branch]if(nativeTextured!=0){
  float2 textureDx=ddx_coarse(i.uv),textureDy=ddy_coarse(i.uv);
  [branch]if(i.textureReference.y!=0)t=nativeTextureArray.SampleGrad(nativeSampler,float3(i.uv,i.textureReference.x),textureDx,textureDy);
  else t=nativeTexture.SampleGrad(nativeSampler,i.uv,textureDx,textureDy);
 }
 [branch]if(nativeAlphaTest!=0){if(!nativeCompare(nativeCombineAlpha(i.d,i.s,t)))discard;}
#endif
 // unlit: the same diffuse with the ambient term removed.
 float3 local=0;float filteredShadow=1;float3 unlit=i.d.rgb;float3 glow=0;
#ifdef NATIVE_UNLIT
 // Unlit ordered draws only (the stock sky; flush_native checks every draw
 // of the batch): lighting off (shadow mode 2, or 0 for prepared vertices)
 // and ordered, so no sun-shadow receiver, and the emissive glow only feeds
 // receivers. The general path computes exactly these values for them.
 bool shadowReceiver=false;
#else
 // One call site (each call inlines the point lighting and its shadows):
 // lit receivers take the lamps, packed (pre-lit) receivers the emissive
 // glow only (none without glow lights).
 bool pointLit=i.shadowMode==1&&nativePointLightCount!=0;
 // Ground contact (contact_blend.h): the ground colour, blend weights and
 // ground normal first, so the lamp pass lights the ground receiver too.
 float2 contactK=0;float3 contactArt=0;float3 contactNormal=float3(0,0,1);bool contactLamps=false;
#ifndef NATIVE_LOW // Low: ground contact blend off
 [branch]if(i.contact!=0&&nativeContactBlend.x>0){
  contactArt=contactBlendColor(i.worldPosition,i.worldNormal,positionDx,positionDy,i.contactHeight,contactK);
  [branch]if(contactK.x>0){
   contactNormal=gcGroundNormal(contactGround,i.worldPosition.xy-nativeContactOrigin.xy);
   contactLamps=nativePointLightCount!=0;
  }
 }
#endif
 float3 groundLocal=0,groundGlow=0;
 [branch]if(pointLit||contactLamps||(i.shadowMode==2&&nativePointLightGlowCount!=0&&nativeRasterTail.y!=0)){
  float3 lit=nativePointLighting(i,glow,!pointLit,contactLamps,contactNormal,groundLocal,groundGlow);
  if(pointLit)local=lit;
 }
 // Ordered packed colors keep their existing behavior; lit materials retain
 // their existing receiver policy until transparent reception is decided.
 bool shadowReceiver=i.shadowMode==1||(i.shadowMode==2&&nativeRasterTail.y!=0);
#ifdef NATIVE_SHADOW_MASK
 // The mask pass took the sun and cloud shadow for this pixel (see
 // nativeShadowMaskPS); cloud keeps the value the contact blend's
 // min(filteredShadow,cloud) needs.
 float2 shadowMask=nativeShadowMask.Load(int3(i.p.xy,0));
 float cloud=shadowMask.x>1.5?1:max(shadowMask.x,0);
 [branch]if(shadowMask.x<1.5)
 {
  float visibility=shadowMask.x;filteredShadow=shadowMask.y;
#else
 bool sunShadow=all(isfinite(shadowDx))&&all(isfinite(shadowDy))&&shadowReceiver&&
    NATIVE_SUN_SHADOWS_LIVE;
 // Cloud shadow and sun shadow are one visibility (min): shadowed ground
 // never darkens twice. Both look up the rest position so swaying leaves
 // keep their pattern. The temporal history keeps the sun shadow only.
 float cloud=shadowReceiver?skyCloudVisibility(i.shadowPosition):1;
 [branch]if(sunShadow||cloud<1)
 {
  float visibility=1;
  [branch]if(sunShadow){
   // Moving foliage reprojects onto stale history; use the current map.
   bool temporal=nativeRasterTail.x==0&&(i.swaying&255u)==0;
   float3 shadowLookup=i.shadowPosition;
   if((i.terrainVertexFlags&16)!=0&&dot(i.terrainSlope,i.terrainSlope)>1e-8&&dot(nativeWorldSunDirection.xyz,nativeWorldSunDirection.xyz)>1e-12)
    shadowLookup=terrainShadowLookup(i.shadowPosition,i.terrainSlope,normalize(nativeWorldSunDirection.xyz));
#ifdef NATIVE_LOW
   visibility=nativeLowShadowVisibility(shadowLookup,shadowDx,shadowDy);
#else
   visibility=nativeShadowVisibility(shadowLookup,shadowDx,shadowDy,i.p.xy,temporal);
#endif
   if(visibility>=0&&temporal)
    visibility=nativeTemporalShadow(visibility,i.worldPosition,positionDx,positionDy);
   filteredShadow=visibility;
  }
  if(visibility>=0)visibility=min(visibility,cloud);
#endif
  if(visibility<0){i.d.rgb=float3(1,0,1);unlit=i.d.rgb;}
  else if(i.shadowMode==1){
   float3 base=max(nativeWorldAmbient.rgb,i.directional*visibility);
   i.d.rgb=saturate(base+local+i.emissive);
   unlit=saturate(i.directional*visibility+local+i.emissive);
  }else{
   // Current retained packed-world materials modulate TEXTURE by DIFFUSE.
   unlit=i.d.rgb*visibility;
   i.d.rgb*=lerp(saturate(nativeWorldAmbient.rgb),float3(1,1,1),visibility);
  }
 }
 else if(i.shadowMode==1&&nativePointLightCount!=0){
  i.d.rgb=saturate(max(nativeWorldAmbient.rgb,i.directional)+local+i.emissive);
  unlit=saturate(i.directional+local+i.emissive);
 }
 else if(i.shadowMode==1)unlit=saturate(i.directional+i.emissive);
#endif
#ifdef NATIVE_SNOW_TRAILS
 // Snow trails (snow_trails.h) on the active map's opaque base tiles; the
 // field is zero off snow. Only the terrain variant compiled with
 // NATIVE_SNOW_TRAILS (bound while a trail field exists) carries this code,
 // and it runs after the lighting so its values stay live briefly: extra
 // registers held across the shader slowed every terrain pixel.
 // Terrain sunlight is per vertex: the trail's relief rescales it per pixel
 // (walls toward the light brighter, away darker), and the sky light falls
 // off into the trough, so the groove reads at night when ambient dominates.
 float snowPacked=0;
 [branch]if((i.terrainVertexFlags&16)!=0&&i.neighbour==0&&nativeSnowTrailWindow.w>0&&i.shadowMode==1){
  float2 snowRaw=snowTrailRaw(nativeSnowTrail,nativeSnowTrailWindow,nativeSnowTrailTiles,i.worldPosition.xy);
  [branch]if(snowRaw.x+snowRaw.y>1e-4){
   // Forward differences from the centre sample: two more reads, not four.
   float d=1/nativeSnowTrailWindow.z,here=snowTrailHeight(snowRaw);
   float2 slope=float2(snowTrailHeight(snowTrailRaw(nativeSnowTrail,nativeSnowTrailWindow,nativeSnowTrailTiles,i.worldPosition.xy+float2(d,0)))-here,
                       snowTrailHeight(snowTrailRaw(nativeSnowTrail,nativeSnowTrailWindow,nativeSnowTrailTiles,i.worldPosition.xy+float2(0,d)))-here)*nativeSnowTrailWindow.z;
   float3 snowBase=dot(i.terrainSlope,i.terrainSlope)>1e-8?normalize(i.terrainSlope):float3(0,0,1);
   float3 snowRelief=normalize(snowBase-float3(slope,0)*snowBase.z);
   float lengthSquared=dot(nativeWorldSunDirection.xyz,nativeWorldSunDirection.xyz);
   float3 sun=lengthSquared>1e-12?nativeWorldSunDirection.xyz*rsqrt(lengthSquared):float3(0,0,1);
   float sunScale=clamp(saturate(dot(snowRelief,sun))/max(saturate(dot(snowBase,sun)),.25),0,1.6);
   // The sunward wall shades the floor: two steps toward the sun against
   // its elevation over the trail's own relief.
   float run=length(sun.xy);
   [branch]if(run>1e-3){
    float rise=sun.z/run,blocked=0;
    [unroll]for(uint k=1;k<=2;++k){
     float step=.12*k;
     blocked=max(blocked,saturate((snowTrailHeight(snowTrailRaw(nativeSnowTrail,nativeSnowTrailWindow,nativeSnowTrailTiles,i.worldPosition.xy+sun.xy/run*step))-here-step*rise)/.01));
    }
    sunScale*=1-.3*blocked;
   }
   snowPacked=snowTrailCompaction(snowRaw);
   float skyScale=(1-.04*snowPacked)*(.9+.1*snowRelief.z/max(snowBase.z,.2));
   // Each light term takes its own relief factor inside the game's
   // max(ambient, sun) model: the relief rescales the shadowed sun, never the
   // ambient floor. Scaling the whole lit colour by sunScale dropped sunlit
   // trail walls below the shadowed ambient level, so a shadow crossing a
   // trail read as a half-transparent patch (frame_012091, frame_012440).
   float3 sunLight=i.directional*(filteredShadow>=0?min(saturate(filteredShadow),cloud):1);
   float3 before=max(nativeWorldAmbient.rgb,sunLight),after=max(nativeWorldAmbient.rgb*skyScale,sunLight*sunScale);
   i.d.rgb=saturate(i.d.rgb+after-before);unlit=saturate(unlit+sunLight*(sunScale-1));
  }
 }
#endif
)" R"(
// Low: read and alpha-tested before the lighting; terrain: read with its material above.
#if !defined(NATIVE_LOW)&&!defined(NATIVE_TERRAIN_PS)
  float4 t=float4(1,1,1,1);if(impostor)t=impostorTexel;else if(nativeTextured!=0)t=i.textureReference.y!=0?nativeTextureArray.Sample(nativeSampler,float3(i.uv,i.textureReference.x)):nativeTexture.Sample(nativeSampler,i.uv);
#endif
#ifdef NATIVE_TERRAIN_BAKED
  // The cached finished material colour (tap above); alpha stays the tile's.
  t.rgb=terrainBakeColor;
#endif
#ifdef NATIVE_SNOW_TRAILS
  // Packed trough floor: barely darker, cooler snow.
  t.rgb*=lerp(1,float3(.975,.98,.995),snowPacked);
#endif

#ifndef NATIVE_UNLIT
 // Wet surfaces (owned precipitation, issue #166): rain darkens the
 // material, porous ground most and upward faces more; the water film's
 // sheen is added after the lighting. Dry frames cost one uniform test.
 float wet=0;float3 wetNormal=float3(0,0,1);
 [branch]if(nativeWet.x>0&&shadowReceiver&&i.bedLiquid.x<=0){
  bool wetGround=(i.terrainVertexFlags&16)!=0;
  wetNormal=wetGround&&dot(i.terrainSlope,i.terrainSlope)>1e-8?normalize(i.terrainSlope):normalize(i.worldNormal);
  float up=saturate((wetNormal.z-.15)/.65);
  wet=nativeWet.x*lerp(.3,1,up)*(wetGround?1:.75);
  // Dry under roofs, bridges and canopies (the precipitation shelter map).
  wet*=1-nativeShelterCover(i.worldPosition.xy,i.worldPosition.z-.3);
  // No wet darkening while snow lies (frozen ground): seen through the thin
  // snow of a shelter edge it drew a dark band (frame_001039).
  wet*=1-saturate(nativeSnowCover.x*3);
  t.rgb=lerp(t.rgb,pow(saturate(t.rgb),1.35)*.82,wet);
 }
#ifdef NATIVE_TERRAIN_PS // ground only: the object and Low programs never carry it
 // Sea surf on the shore ground (liquidSurfShoreAt): the water mesh ends at
 // the waterline, so the swash sheet each arriving crest pushes up the sand,
 // its scalloped foam edge and lace, the darker wet sand, and on steep shores
 // the white water and spray bursting up the face, are drawn on the dry
 // ground here, on the water's own clock, measured as the ground's height
 // above the sea surface (t63). Only ground near the sea's waterline with
 // sea-mask surf; everywhere else the test leaves the ground unchanged. Maps
 // without sea surf (no field bound: liquidSurfMode.x 0) skip it on one
 // uniform test. Ground: material base tiles (flag 16) and long tiles (4096,
 // the stretched cliff and bank faces).
 float surfFootprint=max(length(ddx(i.worldPosition.xy)),length(ddy(i.worldPosition.xy))); // m per pixel, outside the branch (derivatives)
 [branch]if(liquidSurfMode.x>.5&&(i.terrainVertexFlags&(16u|4096u))!=0&&i.bedLiquid.x<=0&&shadowReceiver) {
  float4 surfField=liquidSurfAt(i.worldPosition.xy);
  [branch]if(surfField.r>1e-3&&surfField.a<2.5&&surfField.a>-liquidSurfRunupInland) {
   float3 up=dot(i.terrainSlope,i.terrainSlope)>1e-8?normalize(i.terrainSlope):normalize(i.worldNormal);
   float4 sea=liquidSurfLevelAt(i.worldPosition.xy); // sea level, shore impact, smooth swell distance and depth
   float4 shore=liquidSurfShoreAt(i.worldPosition.xy,surfField,sea,nativeTerrainClock.y*liquidSecondsPerTime,i.worldPosition.z-sea.x,up,surfFootprint);
   [branch]if(any(shore>0)) {
    t.rgb=lerp(t.rgb,pow(saturate(t.rgb),1.3)*(1-liquidSurfWetDarken),shore.z); // wet sand and rock
    t.rgb=lerp(t.rgb,t.rgb*.7+float3(.06,.13,.14),shore.x*.6);                  // the thin sheet over the sand
    t.rgb=lerp(t.rgb,liquidSurfFoamWhite,max(shore.y,shore.w)*.92);             // foam edge, lace, white water, spray
    wet=max(wet,max(shore.z*.75,shore.x));wetNormal=up;
   }
   // water_surf_debug 2: the shore impact over the shore ground too; 3: the
   // surf foam on the ground, as brightness.
   float surfDebug=fmod(liquidSurfMode.y,4);
   [branch]if(abs(surfDebug-2)<.5){t.rgb=liquidSurfImpactDebug(sea.y);wet=0;}
   else if(abs(surfDebug-3)<.5){t.rgb=saturate(max(shore.y,shore.w)).xxx;wet=0;}
  }
 }
#endif
#endif
)" R"(
#ifndef NATIVE_UNLIT
 // Snow build-up (owned precipitation, issue #166): while it snows, every
 // up-facing lit surface (roofs, rocks, logs, fences, branches and foliage,
 // and the ground) gathers uneven snow that fills hollows first; nothing
 // under a roof or canopy (the shelter map). Foliage keeps its needle and
 // leaf detail (capped, texture-keyed cover); thickness reads as a brighter
 // patch centre and a darker rim. Snow-free frames cost one uniform test.
 [branch]if(nativeSnowCover.x>0&&shadowReceiver&&i.bedLiquid.x<=0){
  bool snowGround=(i.terrainVertexFlags&16)!=0;
  // Objects take the face's own geometric normal (smoothed vertex normals
  // lean up near a top edge and smeared snow down vertical faces: ecfd6605
  // chimney), oriented like the vertex normal.
  // Foliage parts (the authoritative instance foliage flag, carried in the
  // swaying bit 30) keep the vertex normal and count as exposed, so tree
  // crowns gather snow (capped below); other alpha-tested meshes
  // (fences, grilles) take the face normal like any solid mesh.
  // One foliage rule for every path that draws a crown: main-map plant
  // parts carry the foliage flag (retained statics), neighbour-map cut-out
  // figures carry it from their texture's transparency (the decoration
  // submission), and a crown impostor (a cut-out figure's far stand-in, on
  // either map) is foliage with its baked crown normal. Impostors without
  // the flag took solid-object snow from their camera-facing quad, so a
  // pine whitened as it switched to its impostor (2bea38da user note).
  bool foliage=(((i.swaying>>30)&1u)!=0||impostor)&&!snowGround;
  // Creatures and characters (bit 29: the unit flag) take no
  // build-up: at most a faint sheen on their up-facing surfaces, from their
  // own normal only, so nothing slides as they move (c71 user note).
  bool animated=((i.swaying>>29)&1u)!=0;
  float3 sn=impostor?impostorNormal:normalize(i.worldNormal),plane_normal=sn;
  [branch]if(animated){
   float sheen=nativeSnowCover.x*saturate((sn.z-.55)/.4)*.18;
   t.rgb=lerp(t.rgb,max(t.rgb,float3(.93,.95,1)),sheen);
  }else{
  // Moderate slopes collect snow, thinner as they steepen: full to about
  // 30 degrees, none past about 60 (a 35 degree roof keeps most of it).
  float vertexUp=saturate((sn.z-.5)/.37),up=vertexUp;
  // Solid meshes: the face's geometric normal decides, smoothly. It is
  // exact on planar roofs (smoothed vertex normals of low-poly roofs
  // streaked them: ae0822d4) and the same at every distance. Faces steeper
  // than about 63 degrees take none (castle walls held snow far away and
  // lost it up close inside the shelter map: verticals take no build-up),
  // and a wide ramp instead of a per-face switch between the vertex and
  // face normals leaves no straight cuts along roof faces (c71, the
  // doorstep and roof cuts).
  float3 face=cross(positionDx,positionDy);
  if(!foliage&&!snowGround&&dot(face,face)>1e-12){
   face=normalize(face);if(dot(face,fogEye.xyz-i.worldPosition)<0)face=-face;plane_normal=face;
   float faceSoft=smoothstep(.45,.8,face.z);
   up=faceSoft*lerp(saturate((sn.z-.3)/.5),1,faceSoft);
  }
  if(snowGround&&dot(i.terrainSlope,i.terrainSlope)>1e-8){sn=normalize(i.terrainSlope);plane_normal=sn;vertexUp=saturate((sn.z-.5)/.37);up=vertexUp;}
  // Foliage: snow settles on the crown's upper faces, never as a full white
  // wash (ce6849f4 user note): at most about 45% cover, thinner on the
  // undersides, patchy at crown scale. Nothing here depends on distance:
  // the texel-contrast and card-alpha terms read the screen's mip level
  // (and impostors took a fixed share), so pines whitened or greened as the
  // camera moved (ce0fedee user note).
  float foliageKeep=1;
  // Lower branches carry less build-up (the reference near look, from the
  // crown above them): by height within the plant, on every path. The
  // shelter map no longer covers foliage itself (it reached only the
  // pines inside its 96 m square, so near and far pines differed).
  if(foliage){
   up=max(up,.75);
   float plantHeight=i.liquidFall.y>.5?i.liquidFall.x:.75;
   // Bottom third mostly bare (15% of the cap), the full cap in the top
   // third (cab694fe user note: the gradient was too weak to see).
   foliageKeep=.5*lerp(.3,1,smoothstep(-.35,.75,sn.z))*lerp(.15,1,smoothstep(.25,.85,plantHeight));
  }
  // Shelter: what lies overhead here, feathered over a fixed world width
  // (about 0.5 m at every zoom): the bilinear cover here plus six on a
  // 0.25 m ring, compared against the surface's own plane (a sloped roof is
  // not its own cover). Point taps on a cross drew staircase texel edges and
  // square notches around chimneys (frame_002927); wide rings drew circles
  // around posts and trunks (c71).
  float covered=0;
  float2 q=i.worldPosition.xy+i.worldPosition.z*float2(.37,.61);
  [branch]if(up>0&&!foliage){
   float2 rise=-plane_normal.xy/max(plane_normal.z,.2);
   covered=nativeShelterCover(i.worldPosition.xy,i.worldPosition.z)*.4;
   [unroll]for(uint k=0;k<6;++k){
    float angle=k*1.0471976+.5236;float2 o=.25*float2(cos(angle),sin(angle));
    covered+=nativeShelterCover(i.worldPosition.xy+o,i.worldPosition.z+max(dot(rise,o),0))*.1;
   }
  }
  // Shelter thins the snow as a clean fade (patches keep the open snow's
  // shape): raising the patch threshold under partial cover carved noise
  // holes into speckle, lacy blotches and grey contour lines (frame_003062
  // troughs, frame_000314/000359 under a crown). Slopes fade the same way,
  // so a steep face never holds full-strength patches.
  // Linear in the cover (the soft map already shapes the edge); partial
  // cover turns patchy below (the 0.7 m noise), never a cut line.
  // Squared: partial shelter already thins the snow a good deal (c6c05f03
  // user note: under crowns it read only slightly thinner than open snow).
  float shelterKeep=1-saturate(covered),upKeep=smoothstep(.05,.35,up);shelterKeep*=shelterKeep;
  float snowLevel=shelterKeep*upKeep>0?nativeSnowCover.x:0;
  [branch]if(up>0&&snowLevel>0){
   // Uneven at three scales: drifts (4 m), patches (0.7 m) and grain
   // (0.15 m, Medium and up), each fading to its mean below about two
   // pixels of world footprint (no aliasing); full cover leaves only small
   // gaps (c71: large dark blotches read as a camouflage pattern).
   float footprint=length(abs(positionDx)+abs(positionDy)); // derivatives taken at the top of nativePS (outside flow control)
   float fineWeight=nativeSnowCover.w>=2?saturate(1-footprint/.08):0;
   float midWeight=saturate(1-footprint/.35);
   float fine=lerp(.5,nativeSnowNoise(q/.15),fineWeight);
   float mid=lerp(.5,nativeSnowNoise(q/.7+11.3),midWeight);
   // Thin, patchy snow where the shelter is partial: the keep breaks up with
   // the 0.7 m noise in the transition only (full cover and open ground are
   // unchanged), so the edge reads as thinning snow.
   shelterKeep=saturate(shelterKeep+(mid-.5)*2.4*shelterKeep*(1-shelterKeep));
   // Foliage takes the drift scale only (its finer octaves fade with
   // distance and changed the crowns as the camera moved).
   float field=foliage?nativeSnowNoise(q/4):nativeSnowNoise(q/4)*.45+mid*.35+fine*.2;
   field=field*.8+(up-.5)*.2;
   float threshold=lerp(.92,.12,snowLevel);
   float edge=.06+footprint*.6;
   float snowPatch=smoothstep(threshold-edge*.6,threshold+edge,field);
   // A faint contact shade just outside each patch (no texel-brightness
   // mask: it followed magnified texels into blocks, c71).
   float rim=smoothstep(threshold-edge*2,threshold-edge*.6,field)*(1-snowPatch);
   t.rgb*=1-.07*rim*shelterKeep*upKeep;
   [branch]if(snowPatch>0){
    // Clean bright snow lit by the scene, never darker than the texture.
    // Up close a fine crystalline grain (3 cm, Medium and up) keeps the
    // surface crisp instead of a blurry flat white (c71); volume: the body
    // brightens inward from the patch edge.
    float grainWeight=nativeSnowCover.w>=2?saturate(1-footprint/.03):0;
    float grain=lerp(.5,nativeSnowNoise(q/.03+5.7),grainWeight);
    float body=smoothstep(threshold,threshold+.3,field);
    float3 snow=float3(.95,.97,1)*lerp(.88,1.02,body)*lerp(1,lerp(.94,1.04,grain),grainWeight);
    // Ground that is already snow (terrain) gains volume more than colour,
    // so its build-up never outlines shapes against the painted snow.
    t.rgb=lerp(t.rgb,max(t.rgb,snow),snowPatch*(snowGround?.65:1)*foliageKeep*shelterKeep*upKeep);
   }
  }
  }
 }
#endif
)" R"(
 float4 c=i.d;c.rgb=nativeCombineColor(i.d,i.s,t);c.a=nativeCombineAlpha(i.d,i.s,t);
  float4 d0=float4(unlit,i.d.a);float3 c0=nativeCombineColor(d0,i.s,t);
  if(nativeSpecularEnable!=0){c.rgb=saturate(c.rgb+i.s.rgb);c0=saturate(c0+i.s.rgb);}
  // Emissive-surface glow: direct light on top of the clamped game lighting
  // (shadow receivers only, so ordered effects keep their colour).
  if(shadowReceiver){c.rgb+=t.rgb*glow;c0+=t.rgb*glow;}
  // Blend finished material colours: blending t before the texture stage
  // also multiplied the ground by the object's diffuse/material response.
#ifndef NATIVE_LOW // Low: ground contact blend off
  [branch]if(i.contact!=0&&nativeContactBlend.x>0){
   float2 k=contactK;float3 ground=contactArt;
   float3 groundColor=0,groundDirect=0;
   [branch]if(k.x>0){
    float lengthSquared=dot(nativeWorldSunDirection.xyz,nativeWorldSunDirection.xyz);
    float3 sun=nativeWorldSunDirection.xyz*rsqrt(max(lengthSquared,1e-12));
    float facing=nativeTerrainClock.z>0?1:saturate(dot(contactNormal,sun));
    float3 directional=nativeWorldSunDiffuse.rgb*facing*min(max(filteredShadow,0),cloud);
    groundColor=ground*(saturate(max(nativeWorldAmbient.rgb,directional)+groundLocal)+groundGlow);
    groundDirect=ground*(saturate(directional+groundLocal)+groundGlow);
   }
   c.rgb=contactBlendLitColor(c.rgb,groundColor,k.x,k.y);
   c0=contactBlendLitColor(c0,groundDirect,k.x,k.y);
  }
#endif
#ifndef NATIVE_UNLIT
  // Wet sheen: a glossy water-film lobe of the sun (shadowed like the
  // surface) and the sky reflected at grazing angles; on flat ground the
  // falling rain rings it (liquidRainRipples, the water's own rings).
  [branch]if(wet>0&&nativeWet.w>=2){
   float3 n=wetNormal;
#ifndef NATIVE_LOW
   [branch]if(nativeWet.y>0&&nativeWet.w>=3&&n.z>.8){
    float3 rings=liquidRainRipples(i.worldPosition.xy,nativeWet.z,nativeWet.y,length(fwidth(i.worldPosition.xy)));
    n=normalize(float3(n.xy-rings.xy*.5,n.z));
   }
#endif
   float3 v=normalize(fogEye.xyz-i.worldPosition);
   float sunLength=dot(nativeWorldSunDirection.xyz,nativeWorldSunDirection.xyz);
   float3 l=sunLength>1e-12?nativeWorldSunDirection.xyz*rsqrt(sunLength):float3(0,0,1);
   float3 h=normalize(l+v);
   float nl=saturate(dot(n,l)),nh=saturate(dot(n,h)),nv=saturate(dot(n,v));
   float power=lerp(30,160,wet);
   float lobe=pow(nh,power)*(power+8)*(1/25.13)*nl;
   float fresnel=.02+.98*pow(1-nv,5);
   float3 direct=nativeWorldSunDiffuse.rgb*min(max(filteredShadow,0),cloud);
   float3 r=reflect(-v,n),sky=nativeWorldAmbient.rgb*1.5;
   [branch]if(fogTemporal.z>.5&&fogEye.w>.5){
    float az=atan2(r.y,r.x)*(.5/3.14159265);az+=az<0?1:0;
    sky=skyProbe.SampleLevel(fogLinearClamp,float2(az,.5-asin(clamp(r.z,-1,1))/3.14159265),0).rgb;
   }
   float3 sheen=wet*(direct*lobe*(.04+fresnel)*1.5+sky*fresnel*.6*saturate(n.z));
   c.rgb+=sheen;c0+=sheen;
  }
#endif
  // Caustics on ground under water only (bed depth > 0, no bed emission);
  // everything else keeps bed 1 without the caustic terms.
  float causticDepth=i.bedLiquid.y>0?0:i.bedLiquid.x;float3 bed=liquidBedTransmission(i.bedLiquid.x);
  [branch]if(causticDepth>0)bed*=1+liquidBedCaustics(i.shadowPosition.xy,causticDepth,float4(shadowDx.xy,shadowDy.xy))*smoothstep(.6,.85,normalize(i.worldNormal).z)*dot(saturate(nativeWorldSunDiffuse.rgb),float3(.2126,.7152,.0722));
  c.rgb*=bed;
#ifndef NATIVE_LOW // Low: alpha-tested before the lighting (c.a is the same value)
  [branch]if(nativeAlphaTest!=0){if(!nativeCompare(c.a))discard;}
#endif
  float3 ambientShare=max(c.rgb-c0*bed,0);
  // Screen-space AO rebuilds its normal from depth, which is flat per
  // coarse terrain triangle: on steep faces that drew lines along the
  // triangle edges. Faces keep only part of the AO (terrain_cliff.h).
  [branch]if((i.terrainVertexFlags&16)!=0&&dot(i.terrainSlope,i.terrainSlope)>1e-8){float3 aoPlanes=terrainCliffPlaneWeights(normalize(i.terrainSlope));ambientShare*=lerp(1,terrainCliffAoKeep,aoPlanes.x+aoPlanes.y);}
  // World geometry (shadow mode 1 or 2: object_to_clip is the world camera)
  // measures fog and draw distance from its interpolated world position,
  // the point the pixel would unproject to; prepared vertices unproject.
  float3 fogPoint=i.worldPosition;
  [branch]if(i.shadowMode==0&&(fogStock.w>0||fogEye.w>.5))fogPoint=fogUnproject(i.p.xy*fogScreen.xy,i.p.z);
  if(nativeFogEnable!=0){float fogFactor=nativeFogFactor(i.s.a,i.p);c.rgb=lerp(borderFogColorAt(fogPoint,nativeFogColor.rgb),c.rgb,fogFactor);ambientShare*=fogFactor*nativeAtmosphereAt(c.rgb,i.p,nativeFogColor.rgb,fogPoint);}
  else if(nativeDrawDissolvedAt(i.p,fogPoint))discard;
  // Preserve material alpha/discard and leave UI/prepared visuals unchanged.
#ifdef NATIVE_LOW
  // The cascade coverage debug view (nativeRasterTail.x): only the debug
  // specialisation (NATIVE_DEBUG_COVERAGE, bound while the view is on) has it.
#ifdef NATIVE_DEBUG_COVERAGE
  [branch]if(nativeRasterTail.x!=0&&shadowReceiver){c.rgb=nativeLowShadowCoverage(i.shadowPosition);ambientShare=0;}
#endif
#else
#ifdef NATIVE_DEBUG_COVERAGE
  [branch]if(nativeRasterTail.x!=0&&shadowReceiver){c.rgb=nativeShadowCoverage(i.shadowPosition);ambientShare=0;}
#endif
#endif
  NativePixelResult result;
  c.a*=nativeHudOpacity;
  result.color=c;
  result.shadow=float4(filteredShadow,0,0,1);
  result.ambient=float4(ambientShare,c.a);
  return result;}
// Liquid surfaces (liquid_surface.hlsl): their own pixel entry, bound to the
// liquid triangles of liquid batches only. Ground pixels never hold the
// liquid shader's registers and liquid pixels never hold the terrain
// material stages'. Per-vertex classification: a layer batch may hold other
// layer tiles beside liquids; the flag is constant across each decoded tile.
NativePixelResult nativeLiquidPS(NativeOutput i){
 if(i.neighbour!=0 && all(i.worldPosition.xy>nativeTerrainActiveBoundsPS.xy) &&
    all(i.worldPosition.xy<nativeTerrainActiveBoundsPS.zw))discard;
 float3 positionDx=ddx(i.worldPosition),positionDy=ddy(i.worldPosition);
 float liquidDepthFootprint=fwidth(i.liquidTintDepth.a);
#ifdef NATIVE_LIQUID_SSR_INPUT
 // Drawn at half resolution: the full-resolution pixel's derivatives, so
 // each sample takes the surface a full-resolution pixel there would.
 positionDx*=.5;positionDy*=.5;liquidDepthFootprint*=.5;
#endif
 float liquidFootprint=length(abs(positionDx.xy)+abs(positionDy.xy));
#ifdef NATIVE_LIQUID_SSR_INPUT
 nativeLiquidSurfaceColor(i,0,nativeWorldSunDirection.xyz,0,0,liquidFootprint,liquidDepthFootprint,
                          float4(positionDx.xy,positionDy.xy),float2(positionDx.z,positionDy.z));
 NativePixelResult input;input.color=liquidSsrRayOut;input.shadow=liquidSsrPlaceOut;input.ambient=0;return input;
#else
 float4 c=nativeLiquidSurfaceColor(i,nativeWorldAmbient.rgb,nativeWorldSunDirection.xyz,
                                  nativeWorldSunDiffuse.rgb*skyCloudVisibility(i.worldPosition),nativeFogColor.rgb,liquidFootprint,liquidDepthFootprint,
                                  float4(positionDx.xy,positionDy.xy),float2(positionDx.z,positionDy.z));
 float border=saturate(1-nativeFogFactor(i.s.a,i.p));
 // Terrain: the interpolated world position is the pixel's world point.
 // Fog is linear (x*a+b): only the surface's own share takes it. The refracted
 // scene (liquidTransmitted) and the blended terrain behind are fogged already,
 // so alpha keeps its value (raising it with the fog, and fogging the
 // refraction again, made water opaque and pale toward every map edge).
 float3 haze=borderFogColorAt(i.worldPosition,nativeFogColor.rgb);
 c.rgb=lerp(c.rgb-liquidTransmitted.rgb,haze,border);haze*=border;
 if(nativeFogEnable!=0){float3 before=c.rgb;float kept=nativeAtmosphereCoreAt(c.rgb,i.p,nativeFogColor.rgb,i.worldPosition);haze=haze*kept+c.rgb-before*kept;}
 c.rgb+=liquidTransmitted.rgb-liquidTransmitted.a*haze;
 c.a=lerp(1,c.a,nativeDrawFadeToAt(c.rgb,i.worldPosition,nativeFogColor.rgb));
 if(nativeAlphaTest!=0&&!nativeCompare(c.a))discard;
 NativePixelResult result;result.color=c;result.shadow=float4(1,0,0,1);result.ambient=float4(0,0,0,c.a);return result;
#endif
}
)" R"(
// The half-resolution water reflection (Renderer::trace_water_reflections):
// each sample's ray from the input pass, marched against the scene snapshot
// with liquidSceneReflection at the full-resolution step counts. A sample
// whose water lies behind the snapshot's surface there (water under a bank,
// a bridge or an object) is no usable sample (key w -1).
struct NativeLiquidSsrResult{float4 color:SV_Target0;float4 key:SV_Target1;};
NativeLiquidSsrResult nativeLiquidSsrPS(float4 position:SV_Position){
 int3 texel=int3(int2(position.xy),0);
 float4 ray=liquidSsrRay.Load(texel),place=liquidSsrPlace.Load(texel);
 NativeLiquidSsrResult result;result.color=0;result.key=float4(ray.xyz,-1);
 [branch]if(place.w<=0)return result;
 float4 clip=mul(liquidWorldClip,float4(place.xyz,1));float2 uv;
 if(!liquidProject(place.xyz,uv))return result;
 if(clip.z/clip.w>liquidSceneDepth.Load(int3(int2(uv*liquidReflectionTarget.xy),0)))return result;
 float hidden;
 float4 scene=liquidSceneReflection(place.xyz,ray.xyz,0,1,position.xy*2,place.w,ray.w,hidden);
 result.color=float4(scene.rgb*scene.a,scene.a);result.key.w=hidden;
 return result;
}
// The half-resolution reflection's filter (between the march and the water
// bodies' resolve): each sample's march starts at its own dither offset, a
// fine grain at full resolution that reads as a dithered pattern once the
// samples are twice the pixel size and blended. A 3x3 bilateral pass over
// the samples of the same surface (clip depth within about 2%) and ray
// (agreement to the 16th power) averages it out, and so do hit/miss
// patterns along reflected edges; it never crosses a depth edge or another
// water surface, and unusable samples (w < 0) neither take nor give.
NativeLiquidSsrResult nativeLiquidSsrFilterPS(float4 position:SV_Position){
 int2 c=int2(position.xy);uint width,height;liquidSsrColor.GetDimensions(width,height);
 float4 key=liquidSsrKey.Load(int3(c,0)),place=liquidSsrPlace.Load(int3(c,0));
 NativeLiquidSsrResult result;result.color=liquidSsrColor.Load(int3(c,0));result.key=key;
 [branch]if(place.w<=0||key.w<0)return result;
 float depth=dot(liquidWorldClip[3],float4(place.xyz,1));
 float4 sum=0;float hiddenSum=0,total=0;
 [unroll]for(int y=-1;y<=1;++y)[unroll]for(int x=-1;x<=1;++x){
  int3 texel=int3(clamp(c+int2(x,y),int2(0,0),int2(width,height)-1),0);
  float4 k=liquidSsrKey.Load(texel),p=liquidSsrPlace.Load(texel);
  if(p.w<=0||k.w<0)continue;
  float relative=(dot(liquidWorldClip[3],float4(p.xyz,1))-depth)/max(abs(depth),1e-3);
  float weight=(x==0?2:1)*(y==0?2:1)*exp(-relative*relative*2500)*pow(saturate(dot(k.xyz,key.xyz)),4);
  sum+=liquidSsrColor.Load(texel)*weight;hiddenSum+=k.w*weight;total+=weight;
 }
 result.color=sum/max(total,1e-6);result.key=float4(key.xyz,hiddenSum/max(total,1e-6));
 return result;
}
)" R"(
// Cliff tessellation (terrain_tessellation, terrain_cliff.h): Phong smoothing
// of the drawn terrain from its own per-vertex slope normals. Terrain batches
// draw through these stages as 3-point patches. Each vertex carries that
// normal (also in clip space) and its largest offset, from its position and
// normal only, so every patch sharing a vertex agrees and nothing depends on
// the camera.
struct NativeTessPoint{NativeOutput o;float3 tessNormal:TEXCOORD24;float4 tessClipNormal:TEXCOORD25;float tessLimit:TEXCOORD26;
 // Clip-space world up of active-map ground for snow trail displacement (0: none).
 float4 tessClipUp:TEXCOORD28;}; // TEXCOORD23: NativeOutput.contactHeight; 27 liquidFall, 29 terrainFlat
// Liquids: tessNormal is the horizontal flow direction (world), tessClipNormal
// its clip-space image and tessLimit the corner's liquidTessWeight.
NativeTessPoint nativeTessVS(uint vertexId:SV_VertexID){
 // Snow trail field, flat tile class drawn tessellated (bit 32): only the
 // triangles near an active trail tile (the untessellated draw takes the
 // rest); the others arrive collapsed and their patches are culled.
 [branch]if((nativeLiquidLayer&32u)!=0&&!nativeTriangleNearTrail(nativeVisibleTriangles[nativeTriangleBase+vertexId/3])){
  NativeTessPoint none=(NativeTessPoint)0;none.o.p=float4(-2,-2,.5,1);return none;
 }
 NativeTessPoint t;t.o=nativeVertex(vertexId,true);{precise float4 position=t.o.p;t.o.p=position;}t.tessNormal=float3(0,0,1);t.tessClipNormal=0;t.tessLimit=0;t.tessClipUp=0;
 uint4 triangleRef=nativeVisibleTriangles[nativeTriangleBase+vertexId/3];
 NativeInstance instance=loadNativeInstance(triangleRef.x);
 if(instance.worldGeometry==0||instance.terrainVertexOffset==0xffffffff||t.o.neighbour!=0||nativeTerrainTess.x<=0)return t;
 uint preparedIndex=(vertexId%3)==0?triangleRef.y:((vertexId%3)==1?triangleRef.z:triangleRef.w);
 NativeTerrainVertex terrain=loadTerrainVertex(instance,nativeWorldVertices[preparedIndex].sourcePosition);
 if(liquidSurfaceEnabled(terrain.flags)){
  float2 current=mul((float3x3)instance.objectToWorld,float3(terrain.liquidCurrent,0)).xy;
  float run=length(current);
  float3 h=run>1e-5?float3(current/run,0):float3(0,0,0);
  t.tessNormal=h;t.tessClipNormal=mul(instance.objectToClip,float4(h,0));
  t.tessLimit=liquidTessWeight(terrain.liquidCascade,terrain.liquidCurrent,terrain.liquidSelfIllumination);
  // Sea surf: corners on sea water with surf (the coastal band and the
  // exposed sea beyond it) split for the wave height, which the domain lifts
  // along world up (liquidSurfHeight).
  float4 surfField=liquidSurfLoad(t.o.worldPosition.xy);
  if(surfField.r>1e-3&&surfField.a>-1.5&&terrain.liquidSelfIllumination<=0)t.tessLimit+=liquidSurfTessFlag;
  t.tessClipUp=mul(instance.objectToClip,float4(0,0,1,0));
  return t;
 }
 // Layer vertices carry no slope (straight up) and so never bend an edge.
 float3 n=normalize(mul((float3x3)instance.objectToWorld,terrainSlopeNormal(terrain.slopeNormal)));
 t.tessNormal=n;t.tessClipNormal=mul(instance.objectToClip,float4(n,0));
 t.tessLimit=terrainTessLimit(t.o.worldPosition,n.z,nativeTerrainTess.x,nativeTerrainActiveBounds);
 // Submerged ground takes no smoothing: on a shallow bed beside a drop the
 // Phong bulge lifted the bed up to and through the water surface (save1144:
 // sandbar edges standing out of 0.2 m of water like rocks). Live layer
 // shift as in nativeVertex.
 float bedDepth=terrain.liquidDepth;
 if((terrain.shiftObjects>>16)!=0xffffu)bedDepth+=terrainLayerShift(instance,terrain.shiftObjects>>16);
 t.tessLimit*=1-smoothstep(.02,.15,bedDepth);
 // Same object-to-clip use of a world vector as the slope normal above.
 if((terrain.flags&16)!=0&&nativeSnowTrailWindow.w>0)t.tessClipUp=mul(instance.objectToClip,float4(0,0,1,0));
 return t;
}
// Snow trails: an edge crossing trail relief is split to ~20 cm pieces. The
// samples are symmetric in a and b, so both patches sharing an edge agree.
float snowTrailEdgeFactor(float3 a,float3 b){
 float2 p[5]={a.xy,b.xy,(a.xy+b.xy)*.5,a.xy*.75+b.xy*.25,b.xy*.75+a.xy*.25};
 float relief=0;
 [unroll]for(uint k=0;k<5;++k){float2 r=snowTrailRaw(nativeSnowTrail,nativeSnowTrailWindow,nativeSnowTrailTiles,p[k]);relief=max(relief,r.x+r.y);}
 return relief>1e-3?clamp(length(b.xy-a.xy)/.2,1,16):1;
}
struct NativeTessFactors{float edges[3]:SV_TessFactor;float inside:SV_InsideTessFactor;};
// One patch corner as the factor and position code reads it (the full and
// the depth-prepass stages pass the same values, so both subdivide and
// place identically).
struct NativeTessCorner{float4 p;float3 world;float3 rest;uint flags;float3 normal;float4 clipNormal;float limit;float4 clipUp;};
NativeTessCorner nativeTessCornerOf(NativeTessPoint t){
 NativeTessCorner c;c.p=t.o.p;c.world=t.o.worldPosition;c.rest=t.o.shadowPosition;c.flags=t.o.terrainVertexFlags;
 c.normal=t.tessNormal;c.clipNormal=t.tessClipNormal;c.limit=t.tessLimit;c.clipUp=t.tessClipUp;return c;
}
// pixelsAtUnitW: the far-distance cap (0: none); snow: snow trail subdivision.
NativeTessFactors nativeTessFactorsOf(NativeTessCorner c[3],float pixelsAtUnitW,bool snow){
 NativeTessFactors f;
 // A foam-layer triangle without foam arrives collapsed: cull it.
 if(all(c[0].p==c[1].p)&&all(c[0].p==c[2].p)){f.edges[0]=f.edges[1]=f.edges[2]=f.inside=0;return f;}
 [branch]if(liquidSurfaceEnabled(c[0].flags)){
  [unroll]for(uint e=0;e<3;++e){
   uint a=(e+1)%3,b=(e+2)%3;
   f.edges[e]=liquidTessEdgeFactor(c[a].rest,c[b].rest,liquidTessFall(c[a].limit),liquidTessFall(c[b].limit),nativeTerrainCliff.y);
   if(liquidTessSurf(c[a].limit)||liquidTessSurf(c[b].limit))
    f.edges[e]=max(f.edges[e],liquidSurfTessEdgeFactor(c[a].rest,c[b].rest,nativeTerrainCliff.y));
  }
  f.inside=max(f.edges[0],max(f.edges[1],f.edges[2]));
  return f;
 }
 [unroll]for(uint e=0;e<3;++e){
  uint a=(e+1)%3,b=(e+2)%3; // edge e lies opposite control point e
  f.edges[e]=terrainTessEdgeFactor(c[a].world,c[b].world,c[a].normal,c[b].normal,
   c[a].limit,c[b].limit,nativeTerrainCliff.y,c[a].p.w,c[b].p.w,pixelsAtUnitW);
  // The flat tile class draws untessellated (flush_native) except its
  // triangles near an active snow trail tile, so every edge a flat tile can
  // own (both corners in the flat class) stays whole on its neighbours too
  // unless the edge itself is near a trail tile (then both triangles that
  // share it are near and tessellated): no crack. Rest positions, as the
  // vertex stage's triangle test.
  if(c[a].normal.z>=terrainFlatEdgeZ&&c[b].normal.z>=terrainFlatEdgeZ&&
     !nativeSnowTrailNear(min(c[a].rest.xy,c[b].rest.xy),max(c[a].rest.xy,c[b].rest.xy)))f.edges[e]=1;
 }
 f.inside=max(f.edges[0],max(f.edges[1],f.edges[2]));
 [branch]if(snow&&nativeSnowTrailWindow.w>0&&dot(c[0].clipUp,c[0].clipUp)>0){
  [unroll]for(uint s=0;s<3;++s){
   uint a=(s+1)%3,b=(s+2)%3;
   f.edges[s]=max(f.edges[s],snowTrailEdgeFactor(c[a].world,c[b].world));
  }
  float3 centre=(c[0].world+c[1].world+c[2].world)/3;
  float2 r=snowTrailRaw(nativeSnowTrail,nativeSnowTrailWindow,nativeSnowTrailTiles,centre.xy);
  float inner=r.x+r.y>1e-3?clamp(length(c[1].world.xy-c[0].world.xy)/.2,1,16):1;
  f.inside=max(max(f.inside,inner),max(f.edges[0],max(f.edges[1],f.edges[2])));
 }
 return f;
}
NativeTessFactors nativeTessFactorsFor(InputPatch<NativeTessPoint,3> p,float pixelsAtUnitW,bool snow){
 NativeTessCorner c[3]={nativeTessCornerOf(p[0]),nativeTessCornerOf(p[1]),nativeTessCornerOf(p[2])};
 return nativeTessFactorsOf(c,pixelsAtUnitW,snow);
}
// The ground (non-liquid) domain position: the corners' clip and world
// positions interpolated, the Phong lift below the tessellation limit and the
// snow trail displacement. precise: the depth prepass computes the same
// depth through its own stages (depth EQUAL in the material pass).
void nativeTessGroundPosition(float3 b,NativeTessCorner c[3],bool snow,out float4 clip,out float3 world){
 precise float4 position=b.x*c[0].p+b.y*c[1].p+b.z*c[2].p;
 precise float3 placed=b.x*c[0].world+b.y*c[1].world+b.z*c[2].world;
 float limit=b.x*c[0].limit+b.y*c[1].limit+b.z*c[2].limit;
 [branch]if(limit>.002){
  // The shadow position stays put: shadow maps hold the untessellated terrain.
  float3 k=terrainTessPhong(b,c[0].world,c[1].world,c[2].world,c[0].normal,c[1].normal,c[2].normal,limit);
  placed+=k.x*c[0].normal+k.y*c[1].normal+k.z*c[2].normal;
  position+=k.x*c[0].clipNormal+k.y*c[1].clipNormal+k.z*c[2].clipNormal;
 }
 // Snow trails: troughs sink and berms rise (the pixel shader shades the
 // same field, so untessellated terrain keeps the identical look).
)" R"(
 float4 up=b.x*c[0].clipUp+b.y*c[1].clipUp+b.z*c[2].clipUp;
 [branch]if(snow&&nativeSnowTrailWindow.w>0&&dot(up,up)>0){
  float h=snowTrailHeight(snowTrailRaw(nativeSnowTrail,nativeSnowTrailWindow,nativeSnowTrailTiles,placed.xy));
  placed.z+=h;position+=h*up;
 }
 clip=position;world=placed;
}
NativeTessFactors nativeTessPatch(InputPatch<NativeTessPoint,3> p){
 // Output pixels per metre at clip w = 1 (b3: the pass camera and output
 // size): the far-distance cap of terrainTessEdgeFactor.
 return nativeTessFactorsFor(p,.5*nativeCullParams.y*length(nativeCurrentWorldClip[1].xyz),true);
}
// The domain keeps the patch's own winding (barycentric x, y, z -> points 0, 1, 2).
[domain("tri")][partitioning("integer")][outputtopology("triangle_cw")][outputcontrolpoints(3)]
[patchconstantfunc("nativeTessPatch")][maxtessfactor(16.0)]
NativeTessPoint nativeTerrainHS(InputPatch<NativeTessPoint,3> p,uint i:SV_OutputControlPointID){return p[i];}
// snow: snow trail displacement (the material cache's bake leaves it out).
NativeOutput nativeTerrainDomain(float3 b,const OutputPatch<NativeTessPoint,3> p,bool snow){
 NativeOutput o=p[0].o; // per-tile (nointerpolation) values are equal on all three points
#define NATIVE_TESS_LERP(field) o.field=b.x*p[0].o.field+b.y*p[1].o.field+b.z*p[2].o.field
 NATIVE_TESS_LERP(p);NATIVE_TESS_LERP(d);NATIVE_TESS_LERP(s);NATIVE_TESS_LERP(uv);NATIVE_TESS_LERP(worldPosition);
 NATIVE_TESS_LERP(worldNormal);NATIVE_TESS_LERP(directional);NATIVE_TESS_LERP(emissive);NATIVE_TESS_LERP(pointMaterial);
 NATIVE_TESS_LERP(liquidTintDepth);NATIVE_TESS_LERP(liquidMaterial);NATIVE_TESS_LERP(liquidView);NATIVE_TESS_LERP(bedLiquid);
 NATIVE_TESS_LERP(liquidDirections);NATIVE_TESS_LERP(liquidFall);NATIVE_TESS_LERP(liquidTileLocal);NATIVE_TESS_LERP(terrainSlope);NATIVE_TESS_LERP(terrainFlat);NATIVE_TESS_LERP(shadowPosition);
#undef NATIVE_TESS_LERP
 [branch]if(liquidSurfaceEnabled(o.terrainVertexFlags)){
  // Falling and running water: the scalar displacement along the corners'
  // flow directions, from interpolated values only. Both liquid passes (water
  // body and foam layer) use the rest position, so they move together.
  float fallLimit=b.x*liquidTessFall(p[0].tessLimit)+b.y*liquidTessFall(p[1].tessLimit)+b.z*liquidTessFall(p[2].tessLimit);
  [branch]if(fallLimit>0){
   float k=liquidTessDisplacement(o.shadowPosition,o.liquidDirections.zw,o.liquidFall.x,o.liquidTintDepth.a,
                                  o.liquidFlow.w*liquidSecondsPerTime);
   // None at a touched seam (the neighbour's water is not tessellated):
   // the sides pulling back along the flow opened a strip of bed there.
   k*=neighbourSeamWaveWeight(o.shadowPosition.xy);
   o.worldPosition+=k*(b.x*p[0].tessNormal+b.y*p[1].tessNormal+b.z*p[2].tessNormal);
   o.p+=k*(b.x*p[0].tessClipNormal+b.y*p[1].tessClipNormal+b.z*p[2].tessClipNormal);
  }
  // Sea surf: the wave height along world up, where any corner is on sea
  // water with surf (0 wherever the field has none). None at a touched
  // neighbour seam, whose water is not tessellated.
  [branch]if(liquidTessSurf(p[0].tessLimit)||liquidTessSurf(p[1].tessLimit)||liquidTessSurf(p[2].tessLimit)){
   float4 surfField=liquidSurfLoad(o.worldPosition.xy);
   [branch]if(surfField.r>1e-3){
    // Never on swamp, lava or running water (the pixel stage's surf gate),
    // also where the filtered field reaches half a tile past the sea.
    float waterlike=(1-saturate(o.liquidMaterial.w))*(o.liquidMaterial.z>0?0:1)*
                    (1-smoothstep(liquidRiverOnset.x,liquidRiverOnset.y,length(o.liquidDirections.zw)));
    float h=liquidSurfHeight(o.worldPosition.xy,surfField,liquidSurfLevelLoad(o.worldPosition.xy),o.liquidFlow.w*liquidSecondsPerTime,
                             o.liquidTintDepth.a)*
            neighbourSeamWaveWeight(o.shadowPosition.xy)*waterlike;
    o.worldPosition.z+=h;
    o.p+=h*(b.x*p[0].tessClipUp+b.y*p[1].tessClipUp+b.z*p[2].tessClipUp);
   }
  }
  return o;
 }
 NativeTessCorner c[3]={nativeTessCornerOf(p[0]),nativeTessCornerOf(p[1]),nativeTessCornerOf(p[2])};
 nativeTessGroundPosition(b,c,snow,o.p,o.worldPosition);
 return o;
}
[domain("tri")]
NativeOutput nativeTerrainDS(NativeTessFactors factors,float3 b:SV_DomainLocation,const OutputPatch<NativeTessPoint,3> p){
 return nativeTerrainDomain(b,p,true);
}
// Without a snow trail field (nativeSnowTrailWindow.w 0, so the snow terms
// above never apply): the same stages without the trail code, bound while
// no field exists (most of the patch function was trail lookups).
NativeTessFactors nativeTessPatchPlain(InputPatch<NativeTessPoint,3> p){
 return nativeTessFactorsFor(p,.5*nativeCullParams.y*length(nativeCurrentWorldClip[1].xyz),false);
}
[domain("tri")][partitioning("integer")][outputtopology("triangle_cw")][outputcontrolpoints(3)]
[patchconstantfunc("nativeTessPatchPlain")][maxtessfactor(16.0)]
NativeTessPoint nativeTerrainHSPlain(InputPatch<NativeTessPoint,3> p,uint i:SV_OutputControlPointID){return p[i];}
[domain("tri")]
NativeOutput nativeTerrainDSPlain(NativeTessFactors factors,float3 b:SV_DomainLocation,const OutputPatch<NativeTessPoint,3> p){
 return nativeTerrainDomain(b,p,false);
}
// Depth prepass stages (flush_native; nativePrepassPS): the vertex and
// tessellation work of the material pass reduced to the position and the
// values the prepass pixel stage reads. Positions come from the same
// functions (nativeVertex, nativeTessFactorsOf, nativeTessGroundPosition;
// precise), so the material pass's depth EQUAL test meets the same depth.
// Liquid batches never take the prepass, so the domain has no liquid path.
NativeDepthOutput nativePrepassVS(uint vertexId:SV_VertexID){
 NativeOutput o=nativeVertex(vertexId,true);precise float4 position=o.p;o.p=position;return nativeDepthOf(o);
}
// DLSS object motion (flush_native, posed batches after their material
// draw): the same vertex as the material pass (precise, so a LESS_EQUAL test
// meets its depth), plus where it was last frame. World-vertex records
// (merged characters, posed figures) take the change of their prepared
// position since the previous frame's copy of the world vertices (same
// slot, same source vertex and part); other records only the camera.
cbuffer NativeMotionConstants:register(b11){row_major float4x4 nativeMotionPreviousClip;float4 nativeMotionJitter;};
StructuredBuffer<NativeWorldVertex> nativePreviousWorldVertices:register(t46);
struct NativeMotionOutput{float4 p:SV_Position;float4 d:COLOR0;float4 s:COLOR1;float2 uv:TEXCOORD0;nointerpolation uint2 textureReference:TEXCOORD1;
 float4 objectClip:TEXCOORD2;float4 cameraClip:TEXCOORD3;nointerpolation uint swaying:TEXCOORD4;};
NativeMotionOutput nativeMotionVS(uint vertexId:SV_VertexID){
 NativeOutput o=nativeVertex(vertexId,true);precise float4 position=o.p;
 NativeMotionOutput r;r.p=position;r.d=o.d;r.s=o.s;r.uv=o.uv;r.textureReference=o.textureReference;r.swaying=o.swaying;
 uint4 triangleRef=nativeVisibleTriangles[nativeTriangleBase+vertexId/3];
 uint preparedIndex=(vertexId%3)==0?triangleRef.y:((vertexId%3)==1?triangleRef.z:triangleRef.w);
 NativeInstance instance=loadNativeInstance(triangleRef.x);
 r.objectClip=0;r.cameraClip=0;
 [branch]if(instance.worldGeometry!=0){
  float3 previous=o.worldPosition;
  r.cameraClip=mul(nativeMotionPreviousClip,float4(previous,1));
  [branch]if((instance.worldGeometry&@WORLD_OBJECT_SPACE@)==0){
   uint worldIndex=(instance.worldGeometry&@WORLD_LOD@)!=0?
    instance.secondMorphOffset+(preparedIndex&0xffffu):preparedIndex;
   NativeWorldVertex now=nativeWorldVertices[worldIndex],before=nativePreviousWorldVertices[worldIndex];
   if(before.sourcePosition==now.sourcePosition&&before.partNode==now.partNode)previous+=before.p.xyz-now.p.xyz;
  }
  r.objectClip=mul(nativeMotionPreviousClip,float4(previous,1));
 }
 return r;
}
struct NativeMotionResult{float2 motion:SV_Target0;float depth:SV_Target1;};
NativeMotionResult nativeMotionPS(NativeMotionOutput i){
 if(nativeLodDiscard(i.swaying,i.p))discard;
 // Records without a world position stay with the camera pass.
 if(i.cameraClip.w<=1e-6||i.objectClip.w<=1e-6)discard;
 [branch]if(nativeAlphaTest!=0){
  float4 t=float4(1,1,1,1);
  if(nativeTextured!=0)t=i.textureReference.y!=0?nativeTextureArray.Sample(nativeSampler,float3(i.uv,i.textureReference.x)):nativeTexture.Sample(nativeSampler,i.uv);
  if(!nativeCompare(nativeCombineAlpha(i.d,i.s,t)))discard;
 }
 float2 size=nativeMotionJitter.zw;
 float2 pixel=i.p.xy-nativeMotionJitter.xy;
 float2 objectPixel=float2(i.objectClip.x/i.objectClip.w*.5+.5,.5-i.objectClip.y/i.objectClip.w*.5)*size;
 float2 cameraPixel=float2(i.cameraClip.x/i.cameraClip.w*.5+.5,.5-i.cameraClip.y/i.cameraClip.w*.5)*size;
 // A reused slot whose old contents happen to match (same source vertex)
 // would give a jump across the screen: more than a tenth of the width
 // beyond the camera motion keeps the camera motion.
 float2 motion=objectPixel-pixel;
 if(length(objectPixel-cameraPixel)>.1*size.x)motion=cameraPixel-pixel;
 NativeMotionResult o;o.motion=motion;o.depth=i.p.z;return o;
}
struct NativeDepthTessPoint{NativeDepthOutput o;float3 tessNormal:TEXCOORD24;float4 tessClipNormal:TEXCOORD25;float tessLimit:TEXCOORD26;float4 tessClipUp:TEXCOORD28;};
NativeDepthTessPoint nativePrepassTessVS(uint vertexId:SV_VertexID){
 NativeTessPoint t=nativeTessVS(vertexId);
 NativeDepthTessPoint r;r.o=nativeDepthOf(t.o);r.tessNormal=t.tessNormal;r.tessClipNormal=t.tessClipNormal;r.tessLimit=t.tessLimit;r.tessClipUp=t.tessClipUp;
 return r;
}
NativeTessCorner nativeDepthCornerOf(NativeDepthTessPoint t){
 NativeTessCorner c;c.p=t.o.p;c.world=t.o.worldPosition;c.rest=t.o.shadowPosition;c.flags=t.o.terrainVertexFlags;
 c.normal=t.tessNormal;c.clipNormal=t.tessClipNormal;c.limit=t.tessLimit;c.clipUp=t.tessClipUp;return c;
}
NativeTessFactors nativePrepassPatchFor(InputPatch<NativeDepthTessPoint,3> p,bool snow){
 NativeTessCorner c[3]={nativeDepthCornerOf(p[0]),nativeDepthCornerOf(p[1]),nativeDepthCornerOf(p[2])};
 return nativeTessFactorsOf(c,.5*nativeCullParams.y*length(nativeCurrentWorldClip[1].xyz),snow);
}
NativeTessFactors nativePrepassPatch(InputPatch<NativeDepthTessPoint,3> p){return nativePrepassPatchFor(p,false);}
NativeTessFactors nativePrepassPatchSnow(InputPatch<NativeDepthTessPoint,3> p){return nativePrepassPatchFor(p,true);}
[domain("tri")][partitioning("integer")][outputtopology("triangle_cw")][outputcontrolpoints(3)]
[patchconstantfunc("nativePrepassPatch")][maxtessfactor(16.0)]
NativeDepthTessPoint nativePrepassHS(InputPatch<NativeDepthTessPoint,3> p,uint i:SV_OutputControlPointID){return p[i];}
[domain("tri")][partitioning("integer")][outputtopology("triangle_cw")][outputcontrolpoints(3)]
[patchconstantfunc("nativePrepassPatchSnow")][maxtessfactor(16.0)]
NativeDepthTessPoint nativePrepassSnowHS(InputPatch<NativeDepthTessPoint,3> p,uint i:SV_OutputControlPointID){return p[i];}
NativeDepthOutput nativePrepassDomain(float3 b,const OutputPatch<NativeDepthTessPoint,3> p,bool snow){
 NativeDepthOutput o=p[0].o;
 o.d=b.x*p[0].o.d+b.y*p[1].o.d+b.z*p[2].o.d;o.s=b.x*p[0].o.s+b.y*p[1].o.s+b.z*p[2].o.s;o.uv=b.x*p[0].o.uv+b.y*p[1].o.uv+b.z*p[2].o.uv;
 o.terrainSlope=b.x*p[0].o.terrainSlope+b.y*p[1].o.terrainSlope+b.z*p[2].o.terrainSlope;
 o.shadowPosition=b.x*p[0].o.shadowPosition+b.y*p[1].o.shadowPosition+b.z*p[2].o.shadowPosition;
 NativeTessCorner c[3]={nativeDepthCornerOf(p[0]),nativeDepthCornerOf(p[1]),nativeDepthCornerOf(p[2])};
 nativeTessGroundPosition(b,c,snow,o.p,o.worldPosition);
 return o;
}
[domain("tri")]
NativeDepthOutput nativePrepassDS(NativeTessFactors factors,float3 b:SV_DomainLocation,const OutputPatch<NativeDepthTessPoint,3> p){return nativePrepassDomain(b,p,false);}
[domain("tri")]
NativeDepthOutput nativePrepassSnowDS(NativeTessFactors factors,float3 b:SV_DomainLocation,const OutputPatch<NativeDepthTessPoint,3> p){return nativePrepassDomain(b,p,true);}
)" R"(
// Material cache bake (terrain_bake.h): the frame's visible terrain
// triangles drawn again into the cache, each into the page (one of four
// candidates per draw, terrainBakeCorner) its rest position falls in, through
// the live material stages. The position comes from the rest (untessellated)
// XY; everything the material reads (tessellated world position, slope,
// facets, UV) is interpolated exactly as in the live draw.
struct NativeBakeOutput{NativeOutput o;float4 clip:SV_ClipDistance0;};
float3 nativeBakeRest(uint index){return nativeWorldVertices[index].p.xyz;}
// No page: all three corners at one point outside the cache (nothing drawn).
NativeBakeOutput nativeBakePlace(NativeOutput o,TerrainBakePage page){
 NativeBakeOutput r;r.o=o;r.clip=-1;
 if(page.valid)r.o.p=terrainBakeClip(page,o.shadowPosition.xy,r.clip);
 else r.o.p=float4(-2,-2,.5,1);
 return r;
}
// The page is found from the triangle's three rest positions first: most
// triangles of a batch reach no baking page and skip the full vertex work.
NativeBakeOutput nativeTerrainBakeVS(uint vertexId:SV_VertexID){
 uint4 triangleRef=nativeVisibleTriangles[nativeTriangleBase+vertexId/3];
 TerrainBakePage page=terrainBakePageOf(nativeBakeRest(triangleRef.y).xy,nativeBakeRest(triangleRef.z).xy,nativeBakeRest(triangleRef.w).xy);
 if(!page.valid){NativeBakeOutput none=(NativeBakeOutput)0;none.o.p=float4(-2,-2,.5,1);none.clip=-1;return none;}
 return nativeBakePlace(nativeVertex(vertexId,false),page);
}
// Tessellated bake: the same early test before the control point's full
// work; a triangle without a page arrives collapsed and its patch is culled.
NativeTessPoint nativeTerrainBakeTessVS(uint vertexId:SV_VertexID){
 uint4 triangleRef=nativeVisibleTriangles[nativeTriangleBase+vertexId/3];
 if(!terrainBakePageOf(nativeBakeRest(triangleRef.y).xy,nativeBakeRest(triangleRef.z).xy,nativeBakeRest(triangleRef.w).xy).valid){
  NativeTessPoint none=(NativeTessPoint)0;none.o.p=float4(-2,-2,.5,1);return none;
 }
 return nativeTessVS(vertexId);
}
NativeTessFactors nativeTerrainBakePatch(InputPatch<NativeTessPoint,3> p){
 NativeTessFactors f;f.edges[0]=f.edges[1]=f.edges[2]=f.inside=0;
 if(all(p[0].o.p==p[1].o.p)&&all(p[0].o.p==p[2].o.p))return f; // no page (collapsed by the vertex stage)
 if(!terrainBakePageOf(p[0].o.shadowPosition.xy,p[1].o.shadowPosition.xy,p[2].o.shadowPosition.xy).valid)return f;
 // The world-space subdivision without the camera's far cap or snow trails.
 return nativeTessFactorsFor(p,0,false);
}
[domain("tri")][partitioning("integer")][outputtopology("triangle_cw")][outputcontrolpoints(3)]
[patchconstantfunc("nativeTerrainBakePatch")][maxtessfactor(16.0)]
NativeTessPoint nativeTerrainBakeHS(InputPatch<NativeTessPoint,3> p,uint i:SV_OutputControlPointID){return p[i];}
[domain("tri")]
NativeBakeOutput nativeTerrainBakeDS(NativeTessFactors factors,float3 b:SV_DomainLocation,const OutputPatch<NativeTessPoint,3> p){
 TerrainBakePage page=terrainBakePageOf(p[0].o.shadowPosition.xy,p[1].o.shadowPosition.xy,p[2].o.shadowPosition.xy);
 return nativeBakePlace(nativeTerrainDomain(b,p,false),page);
}
// Routing (terrain_bake 1/3): the baked variant's pass over a terrain batch
// keeps only triangles with a vertex in a window page; the rest collapse.
NativeOutput nativeTerrainBakedVS(uint vertexId:SV_VertexID){
 NativeOutput o=nativeVertex(vertexId,true);
 uint4 triangleRef=nativeVisibleTriangles[nativeTriangleBase+vertexId/3];
 if((o.terrainVertexFlags&(16u|4096u))==0||
    !terrainBakeCandidate(nativeBakeRest(triangleRef.y).xy,nativeBakeRest(triangleRef.z).xy,nativeBakeRest(triangleRef.w).xy))o.p=float4(-2,-2,.5,1);
 return o;
}
NativeTessFactors nativeTerrainBakedPatch(InputPatch<NativeTessPoint,3> p){
 NativeTessFactors f;f.edges[0]=f.edges[1]=f.edges[2]=f.inside=0;
 if((p[0].o.terrainVertexFlags&(16u|4096u))==0||
    !terrainBakeCandidate(p[0].o.shadowPosition.xy,p[1].o.shadowPosition.xy,p[2].o.shadowPosition.xy))return f;
 return nativeTessPatch(p);
}
[domain("tri")][partitioning("integer")][outputtopology("triangle_cw")][outputcontrolpoints(3)]
[patchconstantfunc("nativeTerrainBakedPatch")][maxtessfactor(16.0)]
NativeTessPoint nativeTerrainBakedHS(InputPatch<NativeTessPoint,3> p,uint i:SV_OutputControlPointID){return p[i];}
#ifdef NATIVE_TERRAIN_PS
// Active-map opaque base tiles only; alpha 1 marks the texel as baked.
float4 nativeTerrainBakePS(NativeBakeOutput b):SV_Target{
 NativeOutput i=b.o;
 float3 restDx=ddx(i.shadowPosition),restDy=ddy(i.shadowPosition); // the untessellated surface (nativeTerrainMaterialStages)
 float2 uvDx=ddx(i.uv),uvDy=ddy(i.uv);
 if((i.terrainVertexFlags&(16u|4096u))==0||nativeTextured==0)discard;
 // Fog-independent: the neighbour seam-fog shortcut (tile art under nearly
 // full fog) depends on the camera at bake time.
 i.s.a=1;
 float4 t=i.textureReference.y!=0?nativeTextureArray.Sample(nativeSampler,float3(i.uv,i.textureReference.x)):nativeTexture.Sample(nativeSampler,i.uv);
 bool debugView;t=nativeTerrainMaterialStages(i,t,restDx,restDy,uvDx,uvDy,debugView);
 if(debugView)discard;
 return float4(saturate(t.rgb),1);
}
#endif
)";

// Feature fragments spliced into kNativeShader; the defaults are the text the
// game embeds at build time.
struct NativeShaderFeatures {
    std::string_view terrain_cliff = kTerrainCliffShader;
    std::string_view terrain_tile_blend = kTerrainTileBlendShader;
    std::string_view liquid_surface = kLiquidSurfaceShader;
};

// Generate literal SM5 resource indices from the actual binding contract. This
// avoids both dynamic resource indexing and MSVC's individual literal limit.
inline std::string compose_native_shader_source(const NativeShaderFeatures& features = {}) {
  std::string text=kNativeShader;
  const std::string liquid_marker="@LIQUID_SURFACE@";
  text.replace(text.find(liquid_marker),liquid_marker.size(),std::string(kAtmosphereShader)+kSkyNativeShader+std::string(features.liquid_surface));
  // The point-shadow atlas layout (point_shadow_types.h).
  const std::string code="static const uint NATIVE_POINT_ATLAS_CUBES="+std::to_string(PointShadowTextureLayout::cubes_per_atlas)+
   ";\nstatic const uint NATIVE_POINT_ATLAS_TILES="+std::to_string(PointShadowTextureLayout::atlas_tiles)+";\n";
  const std::string marker="@POINT_DEPTH_SWITCH@";text.replace(text.find(marker),marker.size(),code);
  const std::string wind_marker="@WEATHER_WIND@";text.replace(text.find(wind_marker),wind_marker.size(),std::string(kWeatherWindHlsl)+kNeighbourFogHlsl);
  const std::string contact_marker="@CONTACT_BLEND@";
  text.replace(text.find(contact_marker),contact_marker.size(),std::string(ground_cover::kSurfaceShader)+kContactBlendShader);
  // Before the vertex stage: units sink into trail troughs (nativeFigureTrailSink).
  const std::string snow_marker="@SNOW_TRAILS@";
  text.replace(text.find(snow_marker),snow_marker.size(),snow_trails::kShader);
  const std::string flag_marker="@CONTACT_FLAG@";
  text.replace(text.find(flag_marker),flag_marker.size(),std::to_string(native_ground_contact_flag)+"u");
  const std::string preview_marker="@PREVIEW_SPECULAR_RGB_FLAG@",preview_flag=std::to_string(native_preview_specular_rgb_flag)+"u";
  for(auto at=text.find(preview_marker);at!=std::string::npos;at=text.find(preview_marker,at))
   text.replace(at,preview_marker.size(),preview_flag);
  const std::string neighbour_marker="@NEIGHBOUR_FLAG@",neighbour_flag=std::to_string(native_neighbour_flag)+"u";
  for(auto at=text.find(neighbour_marker);at!=std::string::npos;at=text.find(neighbour_marker,at))
   text.replace(at,neighbour_marker.size(),neighbour_flag);
  const std::string object_space_marker="@WORLD_OBJECT_SPACE@";
  for(auto at=text.find(object_space_marker);at!=std::string::npos;at=text.find(object_space_marker,at))
   text.replace(at,object_space_marker.size(),std::to_string(native_world_object_space)+"u");
  const std::string lod_marker="@WORLD_LOD@";
  for(auto at=text.find(lod_marker);at!=std::string::npos;at=text.find(lod_marker,at))
   text.replace(at,lod_marker.size(),std::to_string(native_world_lod)+"u");
  const std::string impostor_marker="@WORLD_IMPOSTOR@";
  text.replace(text.find(impostor_marker),impostor_marker.size(),std::to_string(native_world_impostor)+"u");
  const std::string frames_marker="@IMPOSTOR_FRAMES@";
  text.replace(text.find(frames_marker),frames_marker.size(),std::string(kImpostorFrameHlsl)+
   "static const uint kImpostorMips="+std::to_string(kImpostorMips)+";\n");
  const std::string plane_marker="@SHADOW_PLANE_COMPARE@";
  text.replace(text.find(plane_marker),plane_marker.size(),kShadowPlaneCompareShader);
  const std::string terrain_marker="@TERRAIN_MATERIAL@";
  text.replace(text.find(terrain_marker),terrain_marker.size(),kTerrainMaterialShader);
  const std::string cliff_marker="@TERRAIN_CLIFF@";
  text.replace(text.find(cliff_marker),cliff_marker.size(),std::string(features.terrain_cliff));
  const std::string bake_marker="@TERRAIN_BAKE@";
  text.replace(text.find(bake_marker),bake_marker.size(),kTerrainBakeShader);
  const std::string blend_marker="@TERRAIN_TILE_BLEND@";
  text.replace(text.find(blend_marker),blend_marker.size(),std::string(features.terrain_tile_blend));
#ifdef EID3D11_EMBEDDED
  embedded_triangle_shader(text);
#endif
  return text;
}

} // namespace owned
