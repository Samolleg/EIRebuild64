#pragma once

// The world's native (GPU-driven) shader: one source for the terrain, object,
// shadow and tessellation stages, assembled from the shared HLSL fragments of
// the feature headers. The renderer compiles compose_native_shader_source()
// with the embedded feature text; Prototypes/shader-lab composes the same
// source with the feature .hlsl files it reads from disk (Source/shaders).

#include "cave_irradiance.h"
#include "gltf_animation_shader.h"
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
#include "terrain_cliff.h"
#include "terrain_material.h"
#include "terrain_astral.h"
#include "terrain_tile_blend.h"
#include "terrain2.h"
#include "terrain2_cache.h"
#include "weather_wind.h"
#include "world_fog.h"

#include <string>
#include <string_view>
#include <utility>

namespace owned {

inline constexpr char kNativeShader[] = R"(
#if defined(NATIVE_DOMAIN)&&NATIVE_DOMAIN>=5
// Crystal material instances (renderer.h kNativeInstance*, classes.json
// "crystal"): domain 5 is a world object, 6 a unit, each with a crystal class
// in its profile. They are those domains' programs plus NATIVE_CRYSTAL (1 world
// object: see-through; 2 unit: a closed body, no see-through).
#if NATIVE_DOMAIN==5
#define NATIVE_CRYSTAL 1
#undef NATIVE_DOMAIN
#define NATIVE_DOMAIN 2
#else
#define NATIVE_CRYSTAL 2
#undef NATIVE_DOMAIN
#define NATIVE_DOMAIN 1
#endif
#endif
struct NativeInstance{float4 astral;row_major float4x4 objectToClip;row_major float4x4 objectToWorld;uint4 geometryOffsets;uint2 colorOffsets;float2 nativeUvShift;uint nativeFlags;uint nativeDefaultDiffuse;uint nativeDefaultSpecular;uint morphOffset;uint morphCount;uint lightingEnabled;uint4 nativePadding;uint terrainVertexOffset;uint terrainPhaseOffset;uint terrainPhaseCount;uint terrainTilesPerSide;uint terrainFlags;float2 terrainWindDirection;float terrainWindForce;float terrainWindForceNormalized;float terrainWindPhase;float terrainPreciseTime;float2 terrainFogMin;float2 terrainFogMax;float terrainFogDistance;uint terrainFogEnabled;float4 nativeEmissive;float4 nativeSpecular;float nativeMaterialAlpha;float3 nativeMaterialDiffuse;uint worldGeometry;uint shapeCount;uint positionGroups;uint secondMorphOffset;float morphFraction;float3 shape;uint poseNode;uint4 sknMaps;float4 sknFactors;float4 sknEmission;float4 sknBase;};
struct NativeTerrainVertex{float noise;float windMultiplier;float3 objectColor;float depth;uint flags;uint animationIndex;uint animationBaseTile;uint gridIndex;float opacity;float3 emissive;float liquidDepth;float liquidMultiplier;float4 liquidUvBounds;float liquidBend;float liquidCascade;float2 liquidCurrent;float liquidSelfIllumination;float liquidBedEmission;uint slopeNormal;uint shiftObjects;float liquidDrop;float liquidFill;};
struct NativeDraw{uint instanceIndex;uint componentOffset;uint indexOffset;uint indexCount;uint outputTriangleBase;uint indirectArgsByteOffset;uint orderedTriangleOffset;uint preservesOrder;float4 boundsMin;float4 boundsMax;uint preparedVertexBase;uint componentCount;uint prepareWorld;uint colorVisible;uint worldState;uint2 worldGeneration;float geometryRadius;uint steepFirstTriangle;};
struct NativePreparedVertex{float4 p;float4 d;float4 s;float2 uv;uint2 textureReference;};
struct NativeWorldVertex{float4 p;float4 normal;float2 uv;float2 undeformedXY;uint diffuse;uint specular;uint sourcePosition;uint partNode;float4 tangent;};
// b0 and b1, one member list each. D3D11 reads them as constant buffers.
// EID3D12 reads the same bytes through root raw views (t0/t1 space4, the
// executor binds the b0/b1 upload blocks there). As constant buffers, on the
// D3D12 backend (NVIDIA), interface draws read world-flush values for some
// b0/b1 components (fog on, world ambient: black portraits) while the bound
// block held their own values, per component, even beside a fresh component
// of the same register (2026-11-02). Buffer loads read the bytes.
#define NATIVE_RASTER_MEMBERS row_major float4x4 nativeWorld;row_major float4x4 nativeView;row_major float4x4 nativeProjection;float4 nativeViewport;float4 nativeFogColor;float4 nativeTextureFactor;uint nativeTransformed;uint nativeTextured;uint nativeColorOp;uint nativeAlphaOp;uint nativeColorArg1;uint nativeColorArg2;uint nativeAlphaArg1;uint nativeAlphaArg2;uint nativeAlphaTest;uint nativeAlphaFunc;float nativeAlphaRef;uint nativeFogEnable;uint nativeSpecularEnable;uint nativeTriangleBase;uint2 nativeRasterTail;float4 nativeContactOrigin;uint2 nativeContactMapMaterials;uint nativeLiquidLayer;uint nativeContactTail;uint2 nativeCombiner;float nativeHudOpacity;uint nativeCombinerTail;float4 nativeContactLayout;
#define NATIVE_WORLD_LIGHTING_MEMBERS float4 nativeWorldAmbient;float4 nativeWorldSunDirection;float4 nativeWorldSunDiffuse;
#ifdef EID3D12
struct NativeRasterBlock{NATIVE_RASTER_MEMBERS};
struct NativeWorldLightingBlock{NATIVE_WORLD_LIGHTING_MEMBERS};
ByteAddressBuffer nativeRasterBytes:register(t0,space4);
ByteAddressBuffer nativeWorldLightingBytes:register(t1,space4);
#define nativeWorld (nativeRasterBytes.Load<NativeRasterBlock>(0).nativeWorld)
#define nativeView (nativeRasterBytes.Load<NativeRasterBlock>(0).nativeView)
#define nativeProjection (nativeRasterBytes.Load<NativeRasterBlock>(0).nativeProjection)
#define nativeViewport (nativeRasterBytes.Load<NativeRasterBlock>(0).nativeViewport)
#define nativeFogColor (nativeRasterBytes.Load<NativeRasterBlock>(0).nativeFogColor)
#define nativeTextureFactor (nativeRasterBytes.Load<NativeRasterBlock>(0).nativeTextureFactor)
#define nativeTransformed (nativeRasterBytes.Load<NativeRasterBlock>(0).nativeTransformed)
#define nativeTextured (nativeRasterBytes.Load<NativeRasterBlock>(0).nativeTextured)
#define nativeColorOp (nativeRasterBytes.Load<NativeRasterBlock>(0).nativeColorOp)
#define nativeAlphaOp (nativeRasterBytes.Load<NativeRasterBlock>(0).nativeAlphaOp)
#define nativeColorArg1 (nativeRasterBytes.Load<NativeRasterBlock>(0).nativeColorArg1)
#define nativeColorArg2 (nativeRasterBytes.Load<NativeRasterBlock>(0).nativeColorArg2)
#define nativeAlphaArg1 (nativeRasterBytes.Load<NativeRasterBlock>(0).nativeAlphaArg1)
#define nativeAlphaArg2 (nativeRasterBytes.Load<NativeRasterBlock>(0).nativeAlphaArg2)
#define nativeAlphaTest (nativeRasterBytes.Load<NativeRasterBlock>(0).nativeAlphaTest)
#define nativeAlphaFunc (nativeRasterBytes.Load<NativeRasterBlock>(0).nativeAlphaFunc)
#define nativeAlphaRef (nativeRasterBytes.Load<NativeRasterBlock>(0).nativeAlphaRef)
#define nativeFogEnable (nativeRasterBytes.Load<NativeRasterBlock>(0).nativeFogEnable)
#define nativeSpecularEnable (nativeRasterBytes.Load<NativeRasterBlock>(0).nativeSpecularEnable)
#define nativeTriangleBase (nativeRasterBytes.Load<NativeRasterBlock>(0).nativeTriangleBase)
#define nativeRasterTail (nativeRasterBytes.Load<NativeRasterBlock>(0).nativeRasterTail)
#define nativeContactOrigin (nativeRasterBytes.Load<NativeRasterBlock>(0).nativeContactOrigin)
#define nativeContactMapMaterials (nativeRasterBytes.Load<NativeRasterBlock>(0).nativeContactMapMaterials)
#define nativeLiquidLayer (nativeRasterBytes.Load<NativeRasterBlock>(0).nativeLiquidLayer)
#define nativeContactTail (nativeRasterBytes.Load<NativeRasterBlock>(0).nativeContactTail)
#define nativeCombiner (nativeRasterBytes.Load<NativeRasterBlock>(0).nativeCombiner)
#define nativeHudOpacity (nativeRasterBytes.Load<NativeRasterBlock>(0).nativeHudOpacity)
#define nativeCombinerTail (nativeRasterBytes.Load<NativeRasterBlock>(0).nativeCombinerTail)
#define nativeContactLayout (nativeRasterBytes.Load<NativeRasterBlock>(0).nativeContactLayout)
#define nativeWorldAmbient (nativeWorldLightingBytes.Load<NativeWorldLightingBlock>(0).nativeWorldAmbient)
#define nativeWorldSunDirection (nativeWorldLightingBytes.Load<NativeWorldLightingBlock>(0).nativeWorldSunDirection)
#define nativeWorldSunDiffuse (nativeWorldLightingBytes.Load<NativeWorldLightingBlock>(0).nativeWorldSunDiffuse)
#else
cbuffer NativeRasterConstants:register(b0){NATIVE_RASTER_MEMBERS};
cbuffer NativeWorldLightingConstants:register(b1){NATIVE_WORLD_LIGHTING_MEMBERS};
#endif
// nativeCombinerTail bit 2: an interface flush (native_batch_constants).
#define NATIVE_INTERFACE ((nativeCombinerTail&4u)!=0)
// caveVolume*: the bounce light volume (cave_irradiance.h), strength 0 while it is off;
// caveVolumeTail: flags (bit 0 the F10 bounce_debug view, bit 1 outdoors,
// bits 8+ the cave exit count, caveExits t84).
cbuffer NativePointLightConstants:register(b5){uint nativePointLightCount;uint2 nativePointLightGridSize;uint nativePointLightGlowCount;float2 nativePointLightGridOrigin;float nativePointLightGridInverseCell;float nativePointLightGridTail;float3 caveVolumeOrigin;float caveVolumeStrength;float3 caveFineSize;float caveVolumeTail;float3 caveWorldOrigin;float caveFineInverseCell;float3 caveWorldSize;float caveWorldInverseCell;float3 caveTexelScale;float caveFineCell;};
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
// Bounce light (cave_irradiance.h): L1 spherical harmonics per colour channel
// on a world grid (constants in NativePointLightConstants); evaluated for the
// surface normal as irradiance, pi*Y0*c0 + (2pi/3)*Y1(n).c1 (the L1
// clamped-cosine convolution). t83: the sky visibility outdoors, as L1 of the
// visible environment (1 for open ground).
Texture3D<float4> caveIrradianceR:register(t80);
Texture3D<float4> caveIrradianceG:register(t81);
Texture3D<float4> caveIrradianceB:register(t82);
SamplerState caveIrradianceSampler:register(s13);
// The textures hold the active map's volume (z texels [0, caveFineSize.z))
// and, outdoors with neighbour maps, the coarse world volume after it
// (cave_irradiance.h kCaveWorldCell). Past the active volume's horizontal
// edge the world volume lights the neighbours; over the active volume's last
// world cell the two blend, so both sides of the map edge read the same
// world value there (no step at the edge).
float3 nativeCaveBounce(float3 worldPosition,float3 normal){
 [branch]if(!(caveVolumeStrength>0))return 0;
 float4 basis=float4(.886227,1.023328*normal.y,1.023328*normal.z,1.023328*normal.x);
 float3 f=(worldPosition-caveVolumeOrigin)*caveFineInverseCell,w=(worldPosition-caveWorldOrigin)*caveWorldInverseCell;
 float2 inside=min(f.xy,caveFineSize.xy-f.xy);
 float fine=f.z>=0&&f.z<=caveFineSize.z&&all(inside>=0)?1:0;
 [branch]if(caveWorldInverseCell>0)fine*=saturate(min(inside.x,inside.y)*caveFineCell*caveWorldInverseCell);
 float world=caveWorldInverseCell>0&&w.z>=0&&w.z<=caveWorldSize.z?1-fine:0;
 // One sampling body for both volumes (k 0 the active one, 1 the world one).
 float3 e=0;
 [loop]for(uint k=0;k<2;++k){
  float weight=k?world:fine;
  [branch]if(weight>0){
   float3 size=k?caveWorldSize:caveFineSize;
   float3 uvw=(clamp(k?w:f,.5,size-.5)+float3(0,0,k?caveFineSize.z:0))*caveTexelScale;
   e+=weight*float3(dot(caveIrradianceR.SampleLevel(caveIrradianceSampler,uvw,0),basis),
    dot(caveIrradianceG.SampleLevel(caveIrradianceSampler,uvw,0),basis),
    dot(caveIrradianceB.SampleLevel(caveIrradianceSampler,uvw,0),basis));
  }
 }
 return max(e,0)*caveVolumeStrength;
}
// Caves: the light the volume holds (cave exits, lamps) lights the fog there
// (in-scattering, isotropic: the mean incoming radiance, Y0 c0 / (4 pi Y0^2)),
// so a cave mouth's haze takes the outdoor light of the hour instead of the
// black cave fog and fades to it deeper in. Clamped to the volume, so the
// border fog past an exit at the map edge continues its light.
float3 nativeCaveFogLight(float3 p){
 [branch]if(!(caveVolumeStrength>0)||(((uint)caveVolumeTail)&2u)!=0)return 0;
 float3 uvw=clamp((p-caveVolumeOrigin)*caveFineInverseCell,.5,caveFineSize-.5)*caveTexelScale;
 return max(float3(caveIrradianceR.SampleLevel(caveIrradianceSampler,uvw,0).x,
  caveIrradianceG.SampleLevel(caveIrradianceSampler,uvw,0).x,
  caveIrradianceB.SampleLevel(caveIrradianceSampler,uvw,0).x)*.282095,0);
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
// EI_RENDERER_D3D12_HAZARDS, the GPU side (EID3D12_HAZARDS: the hazards
// variants of the DXIL table, dxil_table.h; production DXIL and FXC never see
// it, since a pixel shader with a UAV tests depth only after shading): the
// executor's report buffer, bound to every native program. Words: [0] the
// loop cap, [1] reports taken, [2] the bindless limit (0: off), [3] the live
// index bitmap's byte offset, [4] the fallback (null view) index; reports
// from byte 32: {id | origin << 16, value read, who (texture reference or
// vertex), pixel}. origin names the entry point (d3d12_frame.cpp's table;
// 0: one that set none).
// A loop past the cap stops and reports its id and bound; an index that is
// not a live bindless texture reports and samples the null view instead.
#ifdef EID3D12_HAZARDS
RWByteAddressBuffer nativeGpuReport:register(u0,space3);
static uint nativeLoopCap=0,nativeLoopWho=0,nativeLoopPixel=0xFFFFFFFFu,nativeLoopOrigin=0;
static bool nativeLoopTripped=false;
void nativeGpuReportBegin(uint origin,uint who,uint pixel){nativeLoopCap=nativeGpuReport.Load(0);nativeLoopOrigin=origin;nativeLoopWho=who;nativeLoopPixel=pixel;}
void nativeGpuReportWrite(uint id,uint value){uint slot;nativeGpuReport.InterlockedAdd(4,1u,slot);
 if(slot<63u)nativeGpuReport.Store4(32u+slot*16u,uint4(id|(nativeLoopOrigin<<16),value,nativeLoopWho,nativeLoopPixel));}
bool nativeLoopRunaway(uint id,uint iteration,uint bound){
 if(nativeLoopCap==0u||iteration<nativeLoopCap)return false;nativeLoopTripped=true;nativeGpuReportWrite(id,bound);return true;}
uint nativeBindlessIndex(uint index){const uint limit=nativeGpuReport.Load(8);if(limit==0u)return index;
 if(index<limit&&((nativeGpuReport.Load(nativeGpuReport.Load(12)+(index>>5)*4u)>>(index&31u))&1u)!=0u)return index;
 nativeGpuReportWrite(100u,index);return nativeGpuReport.Load(16);}
#define NATIVE_LOOP_GUARD(id,iteration,bound) if(nativeLoopRunaway(id,iteration,bound))break;
#define NATIVE_LOOP_ALIVE &&!nativeLoopTripped
#define NATIVE_REPORT_PIXEL(origin,ref,position) nativeGpuReportBegin(origin,ref,uint(position.x)|(uint(position.y)<<16));
#else
#define NATIVE_REPORT_PIXEL(origin,ref,position)
#define NATIVE_LOOP_GUARD(id,iteration,bound)
#define NATIVE_LOOP_ALIVE
#ifdef EID3D12
#define nativeBindlessIndex(index) (index)
#endif
#endif
// The material texture by its reference (x slice or bindless index, y array).
// EID3D12 (the D3D12 backend's DXIL only; FXC never sees it): x is the
// texture's index in the shader-visible heap. The D3D11 expansion is the
// expression these macros replaced, token for token.
#ifdef EID3D12
float4 nativeBindlessSample(uint index,float2 uv){Texture2D t=ResourceDescriptorHeap[NonUniformResourceIndex(nativeBindlessIndex(index))];return t.Sample(nativeSampler,uv);}
#define NATIVE_SAMPLE(ref,uv) nativeBindlessSample(ref.x,uv)
#else
#define NATIVE_SAMPLE(ref,uv) ref.y!=0?nativeTextureArray.Sample(nativeSampler,float3(uv,ref.x)):nativeTexture.Sample(nativeSampler,uv)
#endif
// Explicit gradients and levels: a real branch samples only the texture the
// reference names (the ?: form sampled both).
#ifdef EID3D12
float4 terrainTapGrad(uint2 ref,float2 uv,float2 dx,float2 dy){Texture2D t=ResourceDescriptorHeap[NonUniformResourceIndex(nativeBindlessIndex(ref.x))];return t.SampleGrad(nativeSampler,uv,dx,dy);}
float4 terrainTapLevel(uint2 ref,float2 uv,float lod){Texture2D t=ResourceDescriptorHeap[NonUniformResourceIndex(nativeBindlessIndex(ref.x))];return t.SampleLevel(nativeSampler,uv,lod);}
float2 terrainTapSize(uint2 ref,out float levels){Texture2D t=ResourceDescriptorHeap[NonUniformResourceIndex(nativeBindlessIndex(ref.x))];float w,h;t.GetDimensions(0,w,h,levels);return float2(w,h);}
#else
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
#endif
@TERRAIN_MATERIAL@
ByteAddressBuffer nativeGeometry:register(t1);
ByteAddressBuffer nativeDynamic:register(t2);
StructuredBuffer<uint4> nativeVisibleTriangles:register(t4);
StructuredBuffer<NativeDraw> nativeDraws:register(t5);
struct NativeDrawWork{uint record;uint outputTriangleBase;uint indirectArgsByteOffset;uint orderedTriangleOffset;uint flags;uint preparedVertexBase;uint workCount;uint steepOutputTriangleBase;uint steepArgsByteOffset;uint ownerBase;};
StructuredBuffer<NativeDrawWork> nativeDrawWork:register(t16);
// nativeIndexedBuild (D3D12, option indexed_object_draws): nonzero while this
// flush draws its indexed-capable records (work flag 4) through index lists.
// nativeWorkChunks, nativeWorkScratch: nativeBuildCS's step (bits 16-17) and
// chunk depth (bits 0-15), and the byte offset of its record entries in
// nativeIndirectArgs (nativeBuildSelect). Zero: the whole-record build.
cbuffer NativeWorkRange:register(b4){uint nativeWorkBase;uint nativeWorkCount;uint nativeMotionStamp;uint nativeIndexedBuild;
 uint nativeWorkChunks;uint nativeWorkScratch;uint2 nativeWorkPadding;};
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
struct NativePose{uint asset;uint outputBase;uint frame;float fraction;float3 shape;float heightScale;float3 centerShift;uint workCount;row_major float4x4 rootToWorld;uint2 generation;uint gltfClip;uint gltfPrevious;float gltfTime;float gltfPreviousTime;float gltfBlend;};
struct NativePoseNode{float4 rotation;float4 translation;float4 row0;float4 row1;float4 row2;uint morphFirst;uint morphSecond;uint morphCount;float morphFraction;uint2 generation;};
ByteAddressBuffer nativeAnimation:register(t10);
StructuredBuffer<NativePose> nativePoses:register(t11);
StructuredBuffer<NativePoseNode> nativePoseNodes:register(t12);
RWStructuredBuffer<NativePoseNode> nativePoseOutput:register(u4);
cbuffer NativeWorldClip:register(b3){row_major float4x4 nativeCurrentWorldClip;row_major float4x4 nativeCullWorldClip;float4 nativeCullParams;float4 nativeHiZParams;float4 nativeDistanceCull;float4 nativeImpostorParams;float4 nativeDitherParams;};
// nativeDitherParams.y: the derivative-free terrain relief (terrain_relief_derivfree, #243).
#define TERRAIN_RELIEF_DERIVFREE (nativeDitherParams.y!=0)
Texture2D<float> nativeHiZ:register(t26);
StructuredBuffer<NativeInstance> nativePersistentInstances:register(t13);
// Pixel t13 is the contact height map; SKN reads the instance rows at t126.
StructuredBuffer<NativeInstance> nativeSknInstances:register(t126);
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
 // max factor (0 off); z blend rows bound (0 or 1), w a slot's atlas padding as
 // a fraction of its side (terrain_tile_blend.h).
 // [1] Cliff tessellation: x largest displacement on unwalkable faces (m);
 // y unused; z the Cliff blend rock shape detail (cliff_rock_detail); w unused.
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
 float4 nativeSnowValue;
 // Terrain 2.0 (terrain2.hlsl): x 1 while it draws walkable ground, z the
 // styles with edge masks (bits, as a float value), w the map table's
 // entries; per entry four records (terrain2Record).
 float4 nativeTerrain2Value;float4 nativeTerrain2MapsValue[36];
 // Material classes (S1, renderer_materials.cpp): x level (0 off, 1 low,
 // 2 medium, 3 high), y metal diffuse retention, z debug view, w reflection scale.
 float4 nativeMaterialValue;
 // World objects (S2): x level (0 off; 2 relief, sun and crevices; 3 adds the
 // sky and lamps), y relief strength, z roughness offset, w unused.
 float4 nativeMaterialWorldValue;
 // Terrain (S3, terrain 2.0 ground): x level (0 off; 1 relief; 2 adds sun
 // GGX and crevices; 3 the sky on smooth snow, ice and mud), y relief
 // (metres per unit of baked height), z crevice strength, w unused; then
 // the roughness of the terrain 2.0 cell classes 0-7 (classes.json "terrain").
 float4 nativeMaterialTerrainValue;float4 nativeMaterialTerrainRoughValue[2];};
)" R"(
#ifdef NATIVE_FIRE_BURN
// Fire burn twins only (option fire_burn, D3D12; renderer_fire_burn.cpp): the
// area's burn mask. Map rows: abs(r) heat 0..1 (negative on inert ground), g game second
// the fire front reaches the ground there, b game second the fire reaches
// that part of an ignited crown, a crown burn 0..1 (whole trees: their crown
// disk); map metres times nativeBurnRow(0).y = texels. Its last row holds
// this frame's values: (0) x game seconds now, y texels per metre, z a
// crown's burning seconds, w structure-char enabled; (1) leaf darkness,
// reserved leaf-loss value, the ground's burning
// seconds, w 1 while the area has a burn.
Texture2D<float4> nativeBurnMask:register(t72);
Texture2D<uint> nativeBurnMaterials:register(t74);
uint nativeBurnMaterial(uint reference,float2 uv){
#ifdef EID3D12
 uint w,h;nativeBurnMaterials.GetDimensions(w,h);
 if(w==0||reference/1024u>=h)return 0;
 uint index=nativeBurnMaterials.Load(int3(reference%1024u,reference/1024u,0));
 if(index==0)return 0;
 Texture2D<uint> labels=ResourceDescriptorHeap[NonUniformResourceIndex(nativeBindlessIndex(index))];
 labels.GetDimensions(w,h);
 return labels.Load(int3(min(uint2(frac(uv)*float2(w,h)),uint2(w-1,h-1)),0));
#else
 return 0;
#endif
}
float4 nativeBurnRow(uint k){uint w,h;nativeBurnMask.GetDimensions(w,h);return h?nativeBurnMask.Load(int3(k,h-1,0)):0;}
#endif
Texture2D<float4> nativeShelter:register(t70); // shelterBlurCS: r rain cover, g snow cover, b blurred caster depth, a caster share
StructuredBuffer<NativeTerrainFrameValues> nativeTerrainFrame:register(t127);
// The element the frame values are read from: always 0. Terrain 2.0's map
// loop (terrain2Color) sets it from the map index (0 too), so fxc reads the
// values in the loop instead of holding each in a register across it
// (~15 temps); every other program sees a constant 0.
static uint nativeFrameLane=0;
#define nativeTerrainWind nativeTerrainFrame[nativeFrameLane].nativeTerrainWindValue
#define nativeTerrainClock nativeTerrainFrame[nativeFrameLane].nativeTerrainClockValue
#define nativeTerrainFogRange nativeTerrainFrame[nativeFrameLane].nativeTerrainFogRangeValue
#define nativeTerrainFogControl nativeTerrainFrame[nativeFrameLane].nativeTerrainFogControlValue
#define nativeTerrainMaterialSwitches nativeTerrainFrame[nativeFrameLane].nativeTerrainMaterialSwitchesValue
#define nativeWeatherWind nativeTerrainFrame[nativeFrameLane].nativeWeatherWindValue
#define nativeTerrainActiveBounds nativeTerrainFrame[nativeFrameLane].nativeTerrainActiveBoundsValue
#define nativeTerrainContactShared nativeTerrainFrame[nativeFrameLane].nativeTerrainContactSharedValue
#define nativeNeighbourSeam nativeTerrainFrame[nativeFrameLane].nativeNeighbourSeamValue
#define nativeNeighbourCurve nativeTerrainFrame[nativeFrameLane].nativeNeighbourCurveValue
#define nativeTerrainCliffShared nativeTerrainFrame[nativeFrameLane].nativeTerrainCliffSharedValue
)" R"(
#define nativeNeighbourSpanLo nativeTerrainFrame[nativeFrameLane].nativeNeighbourSpanLoValue
#define nativeNeighbourSpanHi nativeTerrainFrame[nativeFrameLane].nativeNeighbourSpanHiValue
#define nativeNeighbourPieceInfo nativeTerrainFrame[nativeFrameLane].nativeNeighbourPieceInfoValue
#define nativeNeighbourPieces nativeTerrainFrame[nativeFrameLane].nativeNeighbourPiecesValue
#define nativeCliffBlend nativeTerrainFrame[nativeFrameLane].nativeCliffBlendValue
#define nativeSnowTrailWindow nativeTerrainFrame[nativeFrameLane].nativeSnowTrailWindowValue
#define nativeSnowTrailTiles nativeTerrainFrame[nativeFrameLane].nativeSnowTrailTilesValue
// NATIVE_FRAME_DRY: the object programs of a frame without weather on its
// surfaces. The renderer binds them only while the uploaded wet level
// (nativeWetValue.x) and snow cover (nativeSnowValue.x) are both 0, so the
// wet and snow build-up code, which those values switch off, compiles out.
#ifdef NATIVE_FRAME_DRY
#define nativeWet float4(0,nativeTerrainFrame[nativeFrameLane].nativeWetValue.yzw)
#else
#define nativeWet nativeTerrainFrame[nativeFrameLane].nativeWetValue
#endif
#define nativeShelterRow nativeTerrainFrame[nativeFrameLane].nativeShelterValue
#ifdef NATIVE_FRAME_DRY
#define nativeSnowCover float4(0,nativeTerrainFrame[nativeFrameLane].nativeSnowValue.yzw)
#else
#define nativeSnowCover nativeTerrainFrame[nativeFrameLane].nativeSnowValue
#endif
#define nativeTerrain2 nativeTerrainFrame[nativeFrameLane].nativeTerrain2Value
#define nativeTerrain2Maps nativeTerrainFrame[nativeFrameLane].nativeTerrain2MapsValue
#define nativeMaterialLevel nativeTerrainFrame[nativeFrameLane].nativeMaterialValue.x
#define nativeMaterialDiffuseKeep nativeTerrainFrame[nativeFrameLane].nativeMaterialValue.y
#define nativeMaterialView nativeTerrainFrame[nativeFrameLane].nativeMaterialValue.z
#define nativeMaterialReflection nativeTerrainFrame[nativeFrameLane].nativeMaterialValue.w
#define nativeMaterialWorldLevel nativeTerrainFrame[nativeFrameLane].nativeMaterialWorldValue.x
#define nativeMaterialRelief nativeTerrainFrame[nativeFrameLane].nativeMaterialWorldValue.y
#define nativeMaterialWorldRoughness nativeTerrainFrame[nativeFrameLane].nativeMaterialWorldValue.z
#define nativeMaterialMetalMask nativeTerrainFrame[nativeFrameLane].nativeMaterialWorldValue.w
#define nativeMaterialTerrainLevel nativeTerrainFrame[nativeFrameLane].nativeMaterialTerrainValue.x
#define nativeMaterialTerrainRelief nativeTerrainFrame[nativeFrameLane].nativeMaterialTerrainValue.y
#define nativeMaterialTerrainCavity nativeTerrainFrame[nativeFrameLane].nativeMaterialTerrainValue.z
#define nativeMaterialTerrainRough nativeTerrainFrame[nativeFrameLane].nativeMaterialTerrainRoughValue
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
)" R"(
#ifdef NATIVE_FIRE_BURN
// Fire burn (fireball and fire wall; renderer-only, reset with the area).
// The burn mask bilinear at map point p (four loads, clamped).
float4 nativeBurnSample(float2 p){
 uint w,h;nativeBurnMask.GetDimensions(w,h);
 const float2 t=p*nativeBurnRow(0).y-.5;const int2 base=int2(floor(t));const float2 f=t-base;
 float4 s=0;
 [unroll]for(uint k=0;k<4;++k){
  float4 tap=nativeBurnMask.Load(int3(clamp(base+int2(k&1u,k>>1),int2(0,0),int2(w-1,h-2)),0));
  tap.r=abs(tap.r); // heat can burn a plant on inert terrain
  s+=((k&1u)?f.x:1-f.x)*((k>>1)?f.y:1-f.y)*tap;
 }
 return s;
}
// nativeBurnAt: x the settled burn at ground point p (0 untouched, 1 fully
// burnt; the mask's soft edge made ragged by two noise octaves), y game
// seconds since the fire front reached p (negative: not yet). Frozen while
// paused, scaled by game speed (the game clock).
float2 nativeBurnAt(float2 p){
 [branch]if(nativeBurnRow(1).w<=0)return float2(0,-1);
 const float4 s=nativeBurnSample(p);
 [branch]if(s.r<=0)return float2(0,-1);
 const float n=nativeSnowNoise(p*.7)*.65+nativeSnowNoise(p*2.3+17)*.35;
 return float2(smoothstep(.25,.75,s.r+(n-.5)*.45),nativeBurnRow(0).x-s.g);
}
float2 nativeBurnGroundAt(float2 p){
 uint w,h;nativeBurnMask.GetDimensions(w,h);
 float heat=nativeBurnMask.Load(int3(clamp(int2(floor(p*nativeBurnRow(0).y)),int2(0,0),int2(w-1,h-2)),0)).r;
 return heat<=0?float2(0,-1):nativeBurnAt(p);
}
// nativeBurnCrownAt: the same for an ignited crown's leaves at p (its whole
// disk; y counts from when the spread through the crown reaches p).
// One texel: a crown burns whole, its disk needs no filtering.
float2 nativeBurnCrownAt(float2 p){
 [branch]if(nativeBurnRow(1).w<=0)return float2(0,-1);
 uint w,h;nativeBurnMask.GetDimensions(w,h);
 const float2 s=nativeBurnMask.Load(int3(clamp(int2(floor(p*nativeBurnRow(0).y)),int2(0,0),int2(w-1,h-2)),0)).ba;
 return s.y<=0?float2(0,-1):float2(smoothstep(.2,.6,s.y),nativeBurnRow(0).x-s.x);
}
// The two looks of a burn: x the flame (the burning moment: up fast once the
// front arrives, out over the burning seconds), y the char (settles over the
// first ~60% of the burning seconds and stays).
float2 nativeBurnPhaseFor(float2 burn,float seconds){
 [branch]if(burn.x<=0||burn.y<0)return 0;
 seconds=max(seconds,.5);
 return burn.x*float2(saturate(burn.y/.35)*(1-smoothstep(.35*seconds,seconds,burn.y)),smoothstep(0,.6*seconds,burn.y));
}
float2 nativeBurnPhase(float2 burn){return nativeBurnPhaseFor(burn,nativeBurnRow(1).z);}
float2 nativeBurnCrownPhase(float2 burn){return nativeBurnPhaseFor(burn,nativeBurnRow(0).z);}
// Ember glow (linear, added light) of a burning point: flickering orange
// hot spots, sparse, on game time.
float3 nativeBurnEmber(float3 p,float flame){
 [branch]if(flame<=0)return 0;
 const float t=nativeBurnRow(0).x;
 const float hot=nativeSnowNoise(p.xy*3.1+p.z*1.7+float2(t*1.9,-t*1.3))*.6+nativeSnowNoise(p.xy*7.3-p.z*2.9+float2(-t*3.7,t*2.3))*.4;
 return float3(1.6,.5,.1)*flame*smoothstep(.55,.9,hot);
}
// The ground's own burning: a band at the fire front (its first second),
// fading as the front moves on.
float nativeBurnFront(float2 burn){
 return burn.y<0?0:burn.x*saturate(burn.y/.15)*(1-smoothstep(.25,1.1,burn.y));
}
// Leaves of a burnt crown: dead brown leaves and black char (darkness), in
// patches about a metre wide.
float3 nativeBurnLeafColor(float3 leaf,float3 p,float charred){
 const float black=saturate(nativeBurnRow(1).x*1.25-.25+(nativeSnowNoise(p.xy*1.3+p.z*.9+31)-.5)*.6);
 const float luma=dot(leaf,float3(.3,.59,.11));
 const float3 dead=luma*float3(1.05,.72,.42)*.75;
 return lerp(leaf,lerp(dead,float3(.035,.03,.025),black),charred);
}
// Only explicitly labelled foliage loses coverage; wood keeps its silhouette.
float nativeBurnLeafAlpha(float alpha,float3 p,float charred){
 // No noise holes or permanent surviving islands. Only explicit foliage
 // labels call this; wood never changes alpha in any pass.
 return alpha*(1-smoothstep(.35,.95,charred));
}
float2 nativeBurnPlantAt(float3 p,float4 root){
 float2 at=nativeBurnCrownAt(p.xy);
 if(root.w<=0)return at;
 float2 burn=nativeBurnCrownAt(root.xy);
 if(burn.x<=0)return at;
 // Keep the stamped front's original direction inside the crown disk.
 if(at.x>0){at.x=1;return at;}
 // Root ignition reaches the entire authored crown, including fronds
 // beyond the old fixed disk. Its shape remains the asset's.
 burn.x=1;burn.y-=min(length(p.xy-root.xy)/1.2,nativeBurnRow(0).z);
 return burn;
}
float2 nativeBurnSurfacePhase(float3 p,float4 root){
 return max(nativeBurnPhase(nativeBurnAt(p.xy)),nativeBurnCrownPhase(nativeBurnPlantAt(p,root)));
}
float3 nativeBurnWoodColor(float3 wood,float3 p,float charred){
 float soot=lerp(.10,.22,nativeSnowNoise(p.xy*1.7+p.z*.8));
 return lerp(wood,wood*soot+float3(.009,.008,.007),charred);
}
// Scorched ground: mottled soot, grey ash patches and the ground's own
// darkened colour showing through, so its detail stays.
float3 nativeBurnGroundColor(float3 ground,float2 p,float charred){
 const float ash=nativeSnowNoise(p*1.9+5)*.6+nativeSnowNoise(p*6.1)*.4;
 const float luma=dot(ground,float3(.3,.59,.11));
 const float3 soot=lerp(float3(.07,.06,.05),ground*.35,.4);
 const float3 scorch=lerp(soot,luma*float3(.78,.76,.73)+.04,smoothstep(.5,.85,ash)*.65);
 // Snow does not char: the fire melts it (the snow trail field's stamp);
 // bright, unsaturated ground takes only a faint wet darkening.
 const float snow=smoothstep(.55,.75,luma)*(1-smoothstep(.04,.12,max(ground.r,max(ground.g,ground.b))-min(ground.r,min(ground.g,ground.b))));
 return lerp(ground,lerp(scorch,ground*.82,snow),charred*.85);
}
#endif
)" R"(
// The build-up's large drift shape as a height slope (world metres per metre
// of q, twice the terrain snow's relief scale): how far the build-up's drift
// octave (4 m; the patch and fine octaves at their mean) stands above its
// threshold, so the snow rises toward each drift and still undulates at full
// cover. One noise evaluation with its analytic gradient.
float2 nativeSnowDriftSlope(float2 q,float threshold,float up){
 float2 p=q/4,c=floor(p),f=p-c,u=f*f*(3-2*f),du=6*f*(1-f);
 float a=nativeSnowHash(c),b=nativeSnowHash(c+float2(1,0)),d=nativeSnowHash(c+float2(0,1)),e=nativeSnowHash(c+1);
 float n=lerp(lerp(a,b,u.x),lerp(d,e,u.x),u.y);
 float2 g=du*float2(lerp(b-a,e-d,u.y),lerp(d-a,e-b,u.x))*.25;
 float s=saturate(((n*.45+.275)*.8+(up-.5)*.2-threshold+.05)/.85);
 return (2*nativeMaterialTerrainRelief*6/.85*.36)*s*(1-s)*g;
}
// The snow surface (terrain2.h kSnowSurfaceShader, update_snow_surface): the
// active map's snow material, rgb its art and a its baked height, tiling
// every 2 m tile; unbound (zero size) on maps without a snow material.
Texture2D<float4> nativeSnowSurface:register(t73);
SamplerState nativeSnowSurfaceSampler:register(s11); // wrap, anisotropic
// The snow surface at world point p (metres; dx, dy its screen derivatives)
// without its 2 m tiling showing on wide roofs (1102 user note: tiled, still
// visible zoomed out with a two-layer blend): hex-tile stochastic sampling.
// A triangle grid of about 1.7 m cells; each cell reads the tile at its own
// random offset and angle, and the three cells around the point blend by their
// sharpened barycentric weights, so no repeat lines up anywhere.
float2 nativeSnowSurfaceOffset(int2 c){
 uint n=uint(c.x)*1597334677u^uint(c.y)*3812015801u;n=(n^(n>>16))*2246822519u;n^=n>>13;
 return float2(n&0xffffu,n>>16)/65535.;
}
// One cell's read: the tile turned by the cell's own angle (the art's
// shapes never share an orientation) and moved by its own offset.
float4 nativeSnowSurfaceCell(int2 c,float2 uv,float2 ux,float2 uy){
 float2 r=nativeSnowSurfaceOffset(c),cs;sincos(r.x*6.2831853,cs.y,cs.x);
 float2x2 turn={cs.x,-cs.y,cs.y,cs.x};
 return nativeSnowSurface.SampleGrad(nativeSnowSurfaceSampler,mul(turn,uv)+r.yx,mul(turn,ux),mul(turn,uy));
}
float4 nativeSnowSurfaceAt(float2 p,float2 dx,float2 dy){
 float2 uv=p*.5,ux=dx*.5,uy=dy*.5;
 float2 skewed=float2(uv.x-.57735027*uv.y,1.15470054*uv.y)*1.2;
 int2 base=int2(floor(skewed));float2 f=frac(skewed);float z=1-f.x-f.y;
 float3 w;int2 c0,c1,c2;
 if(z>0){w=float3(z,f.y,f.x);c0=base;c1=base+int2(0,1);c2=base+int2(1,0);}
 else{w=float3(-z,1-f.y,1-f.x);c0=base+int2(1,1);c1=base+int2(1,0);c2=base+int2(0,1);}
 w=w*w*w;w/=w.x+w.y+w.z;
 return w.x*nativeSnowSurfaceCell(c0,uv,ux,uy)+w.y*nativeSnowSurfaceCell(c1,uv,ux,uy)+w.z*nativeSnowSurfaceCell(c2,uv,ux,uy);
}
// Shelter cover at p for a surface at height z: 0 open, 1 under a roof or
// canopy (its top 0.3-0.9 m above z). One bilinear read of the pre-filtered
// map (shelterBlurCS, once per bake or crown fill-in step): r the rain
// cover, g the snow cover (each texel's crown/roof rule, Gaussian-blurred
// over about 0.5 m, the width of the former per-pixel 7-tap feather), b/a
// the blurred caster depth over the blurred caster share, so the height
// test compares the surface with the mean top of the casters around it (a
// sloped roof's mean is its own height: no self cover). The 7-tap feather
// (28 loads per wet pixel near casters) cost about 0.45 ms of 4K terrain
// and figures in rain (rain-cost A/B run, b7e114c0). Snow, rain-wet
// surfaces, leaves, figures and grass all take this one value.
// Without the soft map (row 2 w, filter unavailable and logged) t70 holds
// the raw depth: a plain bilinear height test.
float nativeShelterCover(float2 p,float z,bool snow){
 [branch]if(nativeShelterRow[0].w<.5)return 0;
 float2 n=float2(dot(nativeShelterRow[0].xy,p)+nativeShelterRow[0].z,dot(nativeShelterRow[1].xy,p)+nativeShelterRow[1].z);
 if(any(abs(n)>=1))return 0;
 uint w,h;nativeShelter.GetDimensions(w,h);
 float2 c=float2(n.x*.5+.5,.5-.5*n.y)*float2(w,h)-.5;
 int2 base=int2(floor(c));float2 f=c-base;
 bool raw=nativeShelterRow[2].w>.5;
 float4 s=0;
 [unroll]for(uint k=0;k<4;++k){
  float4 m=nativeShelter.Load(int3(clamp(base+int2(k&1u,k>>1),int2(0,0),int2(w-1,h-1)),0));
  float weight=(k&1u?f.x:1-f.x)*(k>>1?f.y:1-f.y);
  s+=weight*(raw?(m.r>=1?0:saturate((nativeShelterRow[2].y-m.r*nativeShelterRow[2].x-z-.3)/.6)):m);
 }
 [branch]if(raw)return s.r;
 float top=s.a>1e-4?s.b/s.a:1;
 return (snow?s.g:s.r)*saturate((nativeShelterRow[2].y-top*nativeShelterRow[2].x-z-.3)/.6);
}
// Partial shelter breaks up with the 0.7 m noise in the transition only
// (full cover and open ground unchanged), so a shelter edge reads as a
// patchy thinning, never the map's texel or closing outline (31e0350d user
// note: sharp crown edges). keep: 1 open, 0 sheltered; q: the noise point;
// footprint: metres per pixel (the noise fades to its mean below ~2 px).
float nativeShelterPatchy(float keep,float2 q,float footprint){
 [branch]if(keep<=0||keep>=1)return keep;
 float mid=lerp(.5,nativeSnowNoise(q/.7+11.3),saturate(1-footprint/.35));
 return saturate(keep+(mid-.5)*2.4*keep*(1-keep));
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
)" R"(
// A drawn triangle's rest (untessellated) corners near an active trail tile.
// The slim terrain pass's per-triangle misses (terrain_slim): bit t of the
// flush's visible triangle t, set by the slim pass where any pixel of it
// missed the caches; nativeTerrainMissCompactCS lists those triangles for
// the second pass.
ByteAddressBuffer terrainMissTriangles:register(t124);
bool terrainTriangleMissed(uint index){return ((terrainMissTriangles.Load((index>>5)*4)>>(index&31u))&1u)!=0;}
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
// (cliffs x), NATIVE_OFF_BLEND (material blend z). Switch x is always 0;
// w is 1 at terrain_quality Low (terrain2.hlsl T2_LOW): constant in the D3D11
// NATIVE_TERRAIN_LOW specialisation, tested uniformly by every other program
// (a build without the preset cache, or a key outside the shipped presets,
// binds the general programs, which must draw Low too).
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
#ifdef NATIVE_TERRAIN_LOW
#define NATIVE_SPEC_TERRAIN_LOW 1
#else
#define NATIVE_SPEC_TERRAIN_LOW nativeTerrainMaterialSwitches.w
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
#define nativeTerrainMaterial float4(nativeTerrainMaterialSwitches.x,NATIVE_SPEC_ANTITILE,NATIVE_SPEC_HEIGHT_BLEND,NATIVE_SPEC_TERRAIN_LOW)
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
 [loop]for(uint k=0;k<count;++k){NATIVE_LOOP_GUARD(1u,k,count)float4 r=nativeNeighbourPieces[k];float2 s=alongY?r.yw:r.xz,a=alongY?r.xz:r.yw;
  if(across>=a.x&&across<=a.y&&along>=s.x&&along<=s.y){found=true;lo=min(lo,s.x);hi=max(hi,s.y);}}
 if(!found)return 0;
 [loop]for(uint sweep=0;sweep<count NATIVE_LOOP_ALIVE;++sweep)[loop]for(uint m=0;m<count;++m){NATIVE_LOOP_GUARD(2u,sweep*count+m,count)float4 r=nativeNeighbourPieces[m];float2 s=alongY?r.yw:r.xz,a=alongY?r.xz:r.yw;
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
// Separate, bounded observation output; one lane writes each record.
#ifdef EID3D12
RWByteAddressBuffer nativeLodObservation:register(u8);
#else
RWByteAddressBuffer nativeLodObservation:register(u4);
#endif
float4x4 nativeNodeMatrix(uint node);
void nativeObserveLod(NativeDraw draw,NativeInstance instance,uint state,uint firstLevel,uint secondLevel,
                     uint firstCount,uint secondCount,uint fade){
 [branch]if(nativeWorkPadding.x==0)return; // uniform request flag, never a shader variant
 uint bytes;nativeLodObservation.GetDimensions(bytes);
 uint record=draw.instanceIndex&0x7fffffffu;if(record>=bytes/48u)return;
 uint levels=(instance.worldGeometry&@WORLD_LOD@)!=0?min(nativeGeometry.Load(draw.indexOffset-64),3u)+1u:1u;
 float3 position=float3(instance.objectToWorld[0].w,instance.objectToWorld[1].w,instance.objectToWorld[2].w);
 if(instance.poseNode!=0xffffffffu){float4x4 nodeTransform=nativeNodeMatrix(instance.poseNode);position=float3(nodeTransform[0].w,nodeTransform[1].w,nodeTransform[2].w);}
 uint at=record*48u;
 nativeLodObservation.Store4(at,uint4(state,firstLevel,secondLevel,firstCount));
 nativeLodObservation.Store4(at+16,uint4(secondCount,levels,draw.indexCount/3,fade));
 nativeLodObservation.Store4(at+32,uint4(asuint(position),(instance.nativeFlags&0x40u)!=0?1u:0u));
}
RWStructuredBuffer<uint4> nativeTriangleOutput:register(u0);
RWByteAddressBuffer nativeIndirectArgs:register(u1);
RWStructuredBuffer<NativePreparedVertex> nativePreparedOutput:register(u2);
RWStructuredBuffer<NativeWorldVertex> nativeWorldOutput:register(u3);
// Object motion (nativeMotionStamp nonzero; else unused): what a world
// vertex was before this world frame's first rewrite, marked with the frame's
// stamp in its specular (storeNativeWorldVertex).
RWStructuredBuffer<NativeWorldVertex> nativePreviousWorldOutput:register(u5);
struct NativeWorldState{uint2 geometry;uint2 pose;uint sknLod;uint preparations;uint components;};
RWStructuredBuffer<NativeWorldState> nativeWorldStateOutput:register(u6);
StructuredBuffer<NativeWorldState> nativeWorldStates:register(t15);
#ifdef EID3D12
// Indexed object draws (D3D12, option indexed_object_draws; Renderer::
// flush_native_d3d12). A non-indexed draw runs the vertex program three times
// a triangle; an indexed one lets the GPU reuse a vertex its neighbouring
// triangles share. A vertex is one component of one visible record: its
// owner entry (instance; prepared index with the crossfade code, the values
// a visible triangle's corner holds) sits at the record's own id range
// (NativeDrawWork::ownerBase: the first level's components, then the
// crossfade successor's or the four impostor corners), and each triangle
// writes three ids at its visible-triangle slot. NATIVE_INDEXED vertex
// programs read the owner entry by SV_VertexID (the index value).
RWStructuredBuffer<uint2> nativeOwnerOutput:register(u4);
RWByteAddressBuffer nativeIndexOutput:register(u7);
#endif
#ifdef NATIVE_INDEXED
StructuredBuffer<uint2> nativeVertexOwners:register(t40);
#endif
// Astral rift effects (astral_rift.h): only the astral programs
// (NATIVE_ASTRAL: one vertex and one pixel program, and the Low pixel
// program) carry them; batches of astral records draw with those. Records
// mark themselves in nativeFlags bits 16-20 (native_instance_flags.h).
#define NATIVE_ASTRAL_U0 .0166015625
#define NATIVE_ASTRAL_UW .466796875
#define NATIVE_ASTRAL_CLUMP_V0 .5166015625
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
 float3 shadowPosition:TEXCOORD20;
#ifdef NATIVE_FIRE_BURN
 nointerpolation float4 burnRoot:TEXCOORD31;
#endif
#ifdef NATIVE_ASTRAL
 // Astral rift part (nativeAstralVertex): kind, veil index, phase, clump fade.
 nointerpolation float4 astral:TEXCOORD31;
#endif
#ifdef NATIVE_OBJECT_MOTION
 // The upscaler's object motion (nativeMotionWorldVS): this vertex's
 // last-frame clip position; w 0 where its motion is the camera's.
 float4 motionClip:TEXCOORD30;
#endif
};
uint nativeSknInstance(NativeOutput i){
#ifdef NATIVE_GLTF
 // The native batch selected this program from sknFactors.w. Every vertex
 // carries the same instance index; round away interpolation roundoff.
 return (uint)(i.liquidFlow.w+.5);
#else
 return 0; // Both native backends select NATIVE_GLTF for every SKN material.
#endif
}
@LIQUID_SURFACE@
// Rain rings on wet flat ground: the height gradient of the drop rings,
// rendered once per game-time step into a tiling texture (rainRingsCS,
// Renderer::update_native_rain_rings: 512 x 512 RG16F over a 6.25 m tile,
// the four drop layers combined there) and read with one bilinear tap at
// world xy. The per-pixel 4-layer ring loop (liquidRainRipples, which the
// water keeps) cost about 0.39 ms of 4K terrain and figures in rain. Rings
// finer than the pixel footprint fade out (the water's band weight).
Texture2D<float2> nativeRainRings:register(t71);
float2 nativeRainRingSlope(float2 p,float footprint){
 float detail=liquidBandWeight(footprint/liquidRainWavelength);
 [branch]if(detail<=0)return 0;
 uint w,h;nativeRainRings.GetDimensions(w,h);
 float2 c=frac(p/6.25)*float2(w,h)-.5;
 int2 base=int2(floor(c));float2 f=c-base;
 float2 s=0;
 [unroll]for(uint k=0;k<4;++k)
  s+=(k&1u?f.x:1-f.x)*(k>>1?f.y:1-f.y)*nativeRainRings.Load(int3(uint2(base+int2(k&1u,k>>1))&uint2(w-1,h-1),0));
 return s*detail;
}
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
@GLTF_ANIMATION@
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
 gltfPose(pose,lane);
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
NativePreparedVertex prepareNativeVertex(NativeInstance instance,uint componentAddress){NativePreparedVertex o;uint4 component=nativeGeometry.Load4(componentAddress);float4 position=loadPacked(instance.geometryOffsets.x,component.x);float4 normal=loadPacked(instance.geometryOffsets.y,component.y);if(component.x<instance.morphCount)position.xyz+=loadMorph(instance,component.x);float2 originalWorldPosition=0;NativeTerrainVertex terrain=(NativeTerrainVertex)0;bool terrainVertex=instance.terrainVertexOffset!=0xffffffff;if(terrainVertex){originalWorldPosition=mul(instance.objectToWorld,float4(position.xyz,1)).xy;terrain=loadTerrainVertex(instance,component.x);{float layerShift=terrainLayerShift(instance,terrain.shiftObjects&0xffffu);position.z+=layerShift;terrain.depth+=layerShift;}if((instance.terrainFlags&1)!=0&&(terrain.flags&1)!=0){float phase=(position.x*instance.terrainWindDirection.x+position.y*instance.terrainWindDirection.y+instance.terrainWindPhase*terrain.windMultiplier+terrain.noise);float settle=1-smoothstep(.015,.08,length(terrain.liquidCurrent));float wave=sin(phase)*instance.terrainWindForce*terrain.windMultiplier*settle;if((terrain.flags&2)!=0&&terrain.depth<0){position.xy+=wave.xx;}else{uint gridX=(terrain.gridIndex&65535u)%33;uint gridY=(terrain.gridIndex&65535u)/33;position.z+=wave*0.25;position.x+=sin(gridX*0.7853981634+instance.terrainPreciseTime*0.05)*instance.terrainWindForce*terrain.windMultiplier*settle*3;position.y+=sin(gridY*0.7853981634+instance.terrainPreciseTime*0.05)*instance.terrainWindForce*terrain.windMultiplier*settle*3;}}}o.p=mul(instance.objectToClip,float4(position.xyz,1));if(instance.lightingEnabled!=0&&(nativeCombinerTail&1u)==0){float3 emissive=terrainVertex?terrain.emissive:instance.nativeEmissive.rgb;float opacity=terrainVertex?terrain.opacity:instance.nativeMaterialAlpha;o.d=calculateWorldLighting(instance,normalize(normal.xyz),emissive,opacity);o.s=terrainVertex?float4(0,0,0,terrainFogFactor(instance,originalWorldPosition)):instance.nativeSpecular;if(!terrainVertex)o.s.a=(instance.nativeDefaultSpecular>>24)/255.0;}else{uint diffuse=(instance.nativeFlags&4)!=0||instance.colorOffsets.x==0xffffffff?instance.nativeDefaultDiffuse:nativeGeometry.Load(instance.colorOffsets.x+component.y*4);uint specular=(instance.nativeFlags&4)!=0||instance.colorOffsets.y==0xffffffff?instance.nativeDefaultSpecular:nativeGeometry.Load(instance.colorOffsets.y+component.y*4);o.d=unpackColor(diffuse);o.s=unpackColor(specular);}if((instance.nativeFlags&@PREVIEW_SPECULAR_RGB_FLAG@)!=0)o.s.rgb=unpackColor(instance.nativeDefaultSpecular).rgb;if(terrainVertex&&(instance.terrainFlags&1)!=0&&(terrain.flags&1)!=0){float phase=(position.x*instance.terrainWindDirection.x+position.y*instance.terrainWindDirection.y+instance.terrainWindPhase*terrain.windMultiplier+terrain.noise);float wave=max(sin(phase),0);o.s.rgb=saturate(terrain.objectColor*terrain.windMultiplier*instance.terrainWindForceNormalized*50*wave);}o.uv=asfloat(nativeGeometry.Load2(instance.geometryOffsets.z+component.z*8))+instance.nativeUvShift;if(terrainVertex&&(instance.terrainFlags&2)!=0&&(terrain.flags&4)!=0&&terrain.animationIndex<instance.terrainPhaseCount&&instance.terrainTilesPerSide!=0){uint phase=((instance.terrainFlags&4)!=0?nativeAnimation.Load(instance.terrainPhaseOffset+terrain.animationIndex*4):nativeDynamic.Load(instance.terrainPhaseOffset+terrain.animationIndex*4));uint baseTile=terrain.animationBaseTile;uint currentTile=baseTile+phase;float side=(float)instance.terrainTilesPerSide;o.uv+=float2(((int)(currentTile%instance.terrainTilesPerSide)-(int)(baseTile%instance.terrainTilesPerSide))/side,-((int)(currentTile/instance.terrainTilesPerSide)-(int)(baseTile/instance.terrainTilesPerSide))/side);}o.textureReference=uint2(instance.nativePadding.x,instance.nativePadding.y);return o;}
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
 bool skinned=(component.y&0x80000000u)!=0;component.y&=0x7fffffffu;
 // Merged characters (flag 0x40): each component names its part's node and
 // position base; the part animates exactly as its own record would.
 uint partNode=0,positionBase=0;
 if(!skinned&&(instance.nativeFlags&0x40u)!=0){partNode=component.w&0xffffu;positionBase=component.w>>16;}
 if(!skinned&&instance.poseNode!=0xffffffff)instance.objectToWorld=nativeNodeMatrix(instance.poseNode+partNode);
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
 if(!skinned&&instance.poseNode!=0xffffffff){
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
 if(skinned){
  uint4 joints=nativeGeometry.Load4(component.w);
  float4 weights=asfloat(nativeGeometry.Load4(component.w+16));
  float3 world=0,worldNormal=0,worldTangent=0;
  float4 tangent=asfloat(nativeGeometry.Load4(component.w+224));
  uint correction=nativeGeometry.Load(component.w+240);
  if(correction!=0xffffffffu&&(correction&0x80000000u)!=0){
   NativePoseNode target=nativePoseNodes[instance.poseNode+(correction&0x7fffffffu)];
   position+=target.row0.xyz;normal+=target.row1.xyz;tangent.xyz+=target.row2.xyz;
  }
  [unroll]for(uint influence=0;influence<4;++influence){
   if(weights[influence]>0){
    uint address=component.w+32+influence*48;
    // Three rows, uploaded from the column-vector disk contract once.
    float4x4 inverseBind=float4x4(asfloat(nativeGeometry.Load4(address)),
      asfloat(nativeGeometry.Load4(address+16)),asfloat(nativeGeometry.Load4(address+32)),float4(0,0,0,1));
    float4x4 palette=mul(nativeNodeMatrix(instance.poseNode+joints[influence]),inverseBind);
    world+=weights[influence]*mul(palette,float4(position,1)).xyz;
    worldNormal+=weights[influence]*mul(transpose(inverseObjectLinear(palette)),normal);
    worldTangent+=weights[influence]*mul((float3x3)palette,tangent.xyz);
   }
  }
  output.p=float4(world,1);output.undeformedXY=world.xy;
  output.normal.xyz=normalize(worldNormal);
  output.tangent=float4(normalize(worldTangent),tangent.w);
  if(correction!=0xffffffffu&&(correction&0x80000000u)==0){
   NativePoseNode target=nativePoseNodes[instance.poseNode+correction];
   if(target.row0.w!=0){
    output.p.xyz+=target.row0.xyz;output.undeformedXY=output.p.xy;
    output.normal.xyz=normalize(target.row1.xyz);
    float3 correctedTangent=worldTangent-output.normal.xyz*dot(worldTangent,output.normal.xyz);
    output.tangent.xyz=normalize(correctedTangent);
   }
  }
 }
 [branch]if((instance.worldGeometry&@WORLD_VERTEX_OPACITY@)!=0)output.normal.w=loadPacked(instance.geometryOffsets.y,component.y).w;
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
 // Clamped as floats before the conversion: a far crown seen edge-on lands on
 // the grid's edge, where floor() can give -1, and a negative float to uint
 // is undefined (FXC gives 0; DXC on D3D12 need not, and the quad then showed
 // another frame of the slice).
 uint2 base=(uint2)clamp(floor(grid),0.0,float(kImpostorFrames-2));
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
 [branch]if((instance.worldGeometry&@WORLD_VERTEX_OPACITY@)!=0)output.normal.w=loadPacked(instance.geometryOffsets.y,component.y).w;
 output.uv=asfloat(nativeGeometry.Load2(instance.geometryOffsets.z+component.z*8));
 output.diffuse=instance.colorOffsets.x==0xffffffff?0xffffffff:nativeGeometry.Load(instance.colorOffsets.x+component.y*4);
 output.specular=instance.colorOffsets.y==0xffffffff?0xff000000:nativeGeometry.Load(instance.colorOffsets.y+component.y*4);
 output.sourcePosition=component.x;
 return output;
}

)" R"(
groupshared uint nativeGroupVisible;
groupshared uint nativeGroupPrepare;
groupshared uint nativeShadowGroupBase; // also nativeBuildWhole's flat range
groupshared uint nativeBuildSteepBase;
groupshared uint4 nativeBuildSelection[2];
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
// Outside its range it leaves the static LOD's choice (and crossfade) as is.
#ifdef NATIVE_FIRE_BURN
bool treeSwayAnchored(NativeInstance instance);
float3 treeSwayBase(NativeInstance instance);
#endif
void nativeImpostorSelect(NativeDraw draw,NativeInstance instance,inout uint firstCount,inout uint secondCount,
                          inout uint fade,out bool impostorSecond){
 impostorSecond=false;
 float4 sphere=asfloat(nativeGeometry.Load4(draw.componentOffset-64));
 float3 centre=mul(instance.objectToWorld,float4(sphere.xyz,1)).xyz;
#ifdef NATIVE_FIRE_BURN
 // An intact baked crown cannot represent changed material coverage.
 // Its source triangles (and followers) own the burning silhouette.
 float2 burnPoint=(instance.nativeFlags&0x200u)!=0&&treeSwayAnchored(instance)?treeSwayBase(instance).xy:centre.xy;
 if(nativeBurnCrownAt(burnPoint).x>0||nativeBurnAt(burnPoint).x>0)return;
#endif
 float w=dot(instance.objectToClip[3],float4(centre,1));
 if(w<=1e-3)return;
 float3x3 m=(float3x3)instance.objectToWorld;
 float scale=max(max(length(float3(m[0][0],m[1][0],m[2][0])),length(float3(m[0][1],m[1][1],m[2][1]))),
                 length(float3(m[0][2],m[1][2],m[2][2])));
 // Output pixels (nativeImpostorParams.w: output over render height), so an
 // upscaler's lower render resolution does not move the switch nearer.
 float size=2*sphere.w*scale*length(instance.objectToClip[1].xyz)*nativeCullParams.y*max(nativeImpostorParams.w,1)*.5/w;
 float limit=nativeImpostorParams.x;
 // Far switch size (impostor_far_pixels/impostor_far_distance, default 160 px at 150 m):
 // continuous in distance, so the crossfade never pops.
 [branch]if(nativeDitherParams.z>limit&&nativeDistanceCull.x>0){
  float metres=w*nativeDistanceCull.y/nativeDistanceCull.x;
  limit=lerp(limit,nativeDitherParams.z,smoothstep(.75*nativeDitherParams.w,nativeDitherParams.w,metres));
 }
 float fadeEnd=limit*(1+nativeImpostorParams.y);
 if(size>=fadeEnd)return;
 uint coverage=(uint)round(saturate((fadeEnd-size)/max(fadeEnd-limit,1e-6))*255);
 if(size>limit&&coverage==0)return;
 // An object's other parts (static_lod.h native_world_impostor_follower):
 // the same decision on the same sphere, but the lead draws the quad.
 bool follower=(instance.worldGeometry&@WORLD_IMPOSTOR_FOLLOWER@)!=0;
 secondCount=follower?0:2;impostorSecond=!follower;
 if(size<=limit){firstCount=0;fade=0;return;}
 fade=512u|coverage;
}
// SKN switches discretely. Update history during world preparation, before
// either colour or shadow index selection, using the camera projection.
groupshared uint nativeSknSelectedLevel;
uint nativeSknLodUpdate(NativeDraw draw,NativeInstance instance){
 if(instance.sknFactors.w==0||draw.worldState==0xffffffffu||(instance.worldGeometry&@WORLD_LOD@)==0)return 0;
 uint table=draw.indexOffset-64,levels=min(nativeGeometry.Load(table),3u);
 NativeWorldState state=nativeWorldStateOutput[draw.worldState];
 uint chosen=all(state.geometry==draw.worldGeneration)?min(state.sknLod,levels):0;
 float4x4 node=nativeNodeMatrix(instance.poseNode);
 float3 center=float3(node[0].w,node[1].w,node[2].w);
 float w=dot(nativeCurrentWorldClip[3],float4(center,1));
 float3x3 m=(float3x3)nativeNodeMatrix(instance.poseNode);
 float scale=max(max(length(float3(m[0][0],m[1][0],m[2][0])),length(float3(m[0][1],m[1][1],m[2][1]))),length(float3(m[0][2],m[1][2],m[2][2])));
 float pixels=scale*length(nativeCurrentWorldClip[1].xyz)*nativeCullParams.y*.5/max(w,1e-3);
 float limit=nativeDistanceCull.z;
 if(w<=1e-3||limit<=0)chosen=0;
 else{
  [loop]while(chosen>0){float error=asfloat(nativeGeometry.Load(table+chosen*16+8))*pixels;
   float h=asfloat(nativeGeometry.Load(table+chosen*16+12));if(error<=limit*(1+h))break;--chosen;}
  [loop]while(chosen<levels){float error=asfloat(nativeGeometry.Load(table+(chosen+1)*16+8))*pixels;
   float h=asfloat(nativeGeometry.Load(table+(chosen+1)*16+12));if(error>limit*(1-h))break;++chosen;}
 }
 state.sknLod=chosen;nativeWorldStateOutput[draw.worldState]=state;
 return chosen;
}
void nativeStaticLodSelect(NativeDraw draw,NativeInstance instance,inout uint firstOffset,inout uint firstCount,
                           out uint secondOffset,out uint secondCount,out uint fade,out uint firstLevel,out uint secondLevel,uint sknSelectedLevel){
 secondOffset=0;secondCount=0;fade=0;firstLevel=0;secondLevel=0xffffffffu;
 uint table=draw.indexOffset-64;
 uint levels=min(nativeGeometry.Load(table),3u);
 if(instance.sknFactors.w>0){
  firstCount=nativeGeometry.Load(table+4);firstLevel=sknSelectedLevel;
  if(firstLevel>0){uint2 entry=nativeGeometry.Load2(table+firstLevel*16);firstOffset=entry.x;firstCount=entry.y;}
  return;
 }
 float3 center=(draw.boundsMin.xyz+draw.boundsMax.xyz)*.5;
 float w=dot(instance.objectToClip[3],float4(center,1));
 if(levels==0||w<=1e-3)return;
 float3x3 m=(float3x3)instance.objectToWorld;
 if(instance.poseNode!=0xffffffffu)m=(float3x3)nativeNodeMatrix(instance.poseNode);
 float scale=max(max(length(float3(m[0][0],m[1][0],m[2][0])),length(float3(m[0][1],m[1][1],m[2][1]))),
                 length(float3(m[0][2],m[1][2],m[2][2])));
 // Screen pixels per object unit at the centre (half the target height per
 // unit of clip y over w).
 float pixels=scale*length(instance.objectToClip[1].xyz)*nativeCullParams.y*max(nativeImpostorParams.w,1)*.5/w; // output pixels
 float limit=nativeDistanceCull.z,fadeEnd=limit*(1+nativeDistanceCull.w);
 uint chosen=0;float next=0;
 [loop]for(uint level=1;level<=levels;++level){NATIVE_LOOP_GUARD(3u,level-1u,levels)
  float projected=asfloat(nativeGeometry.Load(table+level*16+8))*pixels;
  if(projected>limit){next=projected;break;}
  chosen=level;
 }
 firstLevel=chosen;
 if(chosen!=0){uint2 entry=nativeGeometry.Load2(table+chosen*16);firstOffset=entry.x;firstCount=entry.y;}
 if(chosen<levels&&next<fadeEnd){
  uint coverage=(uint)round(saturate((fadeEnd-next)/max(fadeEnd-limit,1e-6))*255);
  if(coverage!=0){uint2 entry=nativeGeometry.Load2(table+(chosen+1)*16);secondOffset=entry.x;secondCount=entry.y;fade=512u|coverage;secondLevel=chosen+1;}
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
// Bend length of a plant: its height (world units; the producer applies the
// instance Z scale) plus 1.5, so a 10 m tree's crown top bends ~2.3x as far
// as a 2.5 m bush and saplings and reeds still move visibly. The former
// fixed 6 cm field swung every crown top by ~2 cm: about a pixel at gameplay
// camera distance.
float treeSwayLength(uint flags){return max(((flags>>22)&63u)*.25,.25)+1.5;}
// The bend's along share is <= .55*lean+.45 <= lean (lean >= 1) beside a .2
// across share, so the bend reaches <= .03*length*1.02*lean*drive; the
// flutter <= .03*sqrt(1+1+.5*.5) < .046 times its drive. Strength times both,
// with drive*lean at its swell-1 maximum.
float treeSwayReach(NativeInstance instance){
 return treeSwayStrength(instance)*(.0306*treeSwayLength(instance.nativeFlags)+.046)*max(1,treeSwayDrive(1)*treeSwayLean(1));
}
// The sway at a world-segment clock (nativeTerrainClock.y for the frame;
// the previous frame's for the upscaler's motion, nativeMotionVS).
float3 treeSwayOffsetAt(float3 position,NativeInstance instance,float time){
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
 float bendLength=treeSwayLength(flags);
 float2 wind=nativeTerrainWind.xy;float windLength=length(wind);
 float2 steady=windLength>1e-6?wind/windLength:float2(.7071068,.7071068);
 float2 along=nativeWeatherWind.direction,across=float2(-along.y,along.x);
 float force=saturate(nativeWeatherWind.force); // map force raised to the wind floor
 // time: world segments, stops and scales with the game clock
 float seed=frac(sin(dot(floor(base.xy*8),float2(12.9898,78.233)))*43758.5453);
 float phase=seed*6.2831853,rate=.85+.3*frac(seed*7.31);
 // Soft swells ~40 units apart drifting downwind at ~3 units per second.
 float front=dot(base.xy,steady)*.157-time*.026;
 float swell=smoothstep(.35,1.0,.5+.5*sin(front))*(.6+.4*sin(front*.43+dot(base.xy,float2(-steady.y,steady.x))*.05+1.3));
 float drive=treeSwayDrive(swell);
 float motion=.3+.7*swell;
 // Bend periods ~4.4 and ~2.7 s, side ~3.5 s (18 segments per game second).
 float bend=motion*(.65*sin(time*.08*rate+phase)+.35*sin(time*.13*rate+phase*1.7));
 float side=motion*sin(time*.1*rate+phase*2.3);
 float3 offset=float3(strength*.03*bendLength*(.35+.65*force)*weight*drive*
  (along*(.55*(.25+.75*swell)*treeSwayLean(swell)+.45*bend)+across*.2*side),0);
 // Up to ~3 cm of leaf flutter, part of the same field.
 float3 wobble=float3(sin(time*.3*rate+dot(relative,float3(2.1,1.7,1.3))),
                      sin(time*.37*rate+dot(relative,float3(1.5,2.3,1.9))),
                      .5*sin(time*.27*rate+dot(relative,float3(1.9,1.1,2.4))));
 return offset+strength*.03*(.5+.5*force)*weight*motion*lerp(1,drive,.5)*wobble;
}
float3 treeSwayOffset(float3 position,NativeInstance instance){return treeSwayOffsetAt(position,instance,nativeTerrainClock.y);}
)" R"(
// A prepared world vertex into its slot. With a stamp (a world frame with a temporal
// upscaler) the slot's old contents are kept first, once a world frame, so
// the motion stages read where a vertex rewritten this frame was; a slot
// without this frame's stamp has not moved.
void storeNativeWorldVertex(uint index,NativeWorldVertex value){
 [branch]if(nativeMotionStamp!=0&&nativePreviousWorldOutput[index].specular!=nativeMotionStamp){
  NativeWorldVertex before=nativeWorldOutput[index];before.specular=nativeMotionStamp;
  nativePreviousWorldOutput[index]=before;
 }
 nativeWorldOutput[index]=value;
}
// The build runs in steps per geometry page range (nativeWorkChunks bits
// 16-17), so a large record -- a merged character, a water body -- spreads
// across many groups while its triangles keep their order:
// 0 whole (one group per record, all of it): ranges whose records fit one
//   chunk or are many enough to fill the GPU (native_build_chunked);
// 1 select (one thread per record): the visibility, the static LOD and
//   impostor choice and the record's whole output ranges (one atomic each,
//   as the whole build reserves them), written to its 32-byte entry in the
//   tail of nativeIndirectArgs (nativeWorkScratch);
// 2 chunks (one group per 512 of a record's components and triangles,
//   dispatch z, nativeWorkChunks bits 0-15 deep; the last chunk takes any
//   rest): prepares its components and writes its triangles at their place
//   in the record's range, from the entry alone;
// 3 commit (one thread per record): the prepared world state, after every
//   chunk, so no chunk reads a state another chunk of the record advanced.
static const uint kNativeBuildChunk=512;
// Prepares components [first,last) when the record's world state is behind
// (nativeGroupPrepare); the caller records the new state.
void nativePrepareWorld(NativeDraw draw,NativeInstance instance,uint lane,uint first,uint last){
 if(lane==0){nativeGroupPrepare=0;
  if(draw.worldState!=0xffffffffu){NativeWorldState previous=nativeWorldStateOutput[draw.worldState];
   nativeGroupPrepare=any(previous.geometry!=draw.worldGeneration)||any(previous.pose!=nativePoseGeneration(instance));
  }
 }
 GroupMemoryBarrierWithGroupSync();
 if(nativeGroupPrepare==0)return;
 for(uint component=first+lane;component<last;component+=64)
  storeNativeWorldVertex(draw.preparedVertexBase+component,prepareNativeWorldVertex(instance,draw.componentOffset+component*16));
}
// A record's world state after its preparation.
void nativeCommitWorld(NativeDraw draw,NativeInstance instance){
 NativeWorldState value=nativeWorldStateOutput[draw.worldState];value.geometry=draw.worldGeneration;value.pose=nativePoseGeneration(instance);
#ifdef EID3D11_TESTING
 value.preparations++;value.components+=draw.componentCount;
#endif
 nativeWorldStateOutput[draw.worldState]=value;
}
// A record's selection entry: x visible (bit 31), impostor second (bit 30),
// fade (bits 0-9); y, z the first level's index offset and triangles; w, then
// the second's; the flat and steep output bases (vertices); the flat count.
uint nativeBuildEntry(uint local){return nativeWorkScratch+(nativeWorkBase+local)*32u;}
)" R"(
void nativeBuildWhole(uint3 groupId,uint lane){
 NativeDraw draw;if(!loadNativeDraw(groupId,draw))return;NativeInstance instance=loadNativeInstance(draw.instanceIndex);
 if(lane==0)nativeSknSelectedLevel=nativeSknLodUpdate(draw,instance);
 GroupMemoryBarrierWithGroupSync();
 if(instance.worldGeometry!=0){
  nativeCurrentBounds(draw,instance);
  float swayReach=treeSwayReach(instance);
  draw.boundsMin.xyz-=swayReach;draw.boundsMax.xyz+=swayReach;
  if(lane==0)nativeGroupVisible=draw.colorVisible!=0&&(draw.preservesOrder!=0||drawVisible(draw,instance));
  GroupMemoryBarrierWithGroupSync();if(nativeGroupVisible==0){if(lane==0)nativeObserveLod(draw,instance,1,0xffffffffu,0xffffffffu,0,0,0);return;}
  if(draw.prepareWorld!=0){
   nativePrepareWorld(draw,instance,lane,0,draw.componentCount);
   if(nativeGroupPrepare!=0){DeviceMemoryBarrierWithGroupSync();if(lane==0)nativeCommitWorld(draw,instance);}
  }
  if(draw.colorVisible==0)return;
 }else{
  if(lane==0)nativeGroupVisible=draw.preservesOrder!=0||drawVisible(draw,instance);
  GroupMemoryBarrierWithGroupSync();if(nativeGroupVisible==0){if(lane==0)nativeObserveLod(draw,instance,1,0xffffffffu,0xffffffffu,0,0,0);return;}
  for(uint component=lane;component<draw.componentCount;component+=64)
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
 uint firstLevel=0,secondLevel=0xffffffffu;
 [branch]if((instance.worldGeometry&@WORLD_LOD@)!=0&&((nativeDistanceCull.z>0&&draw.preservesOrder==0)||instance.sknFactors.w>0)&&!split)
  nativeStaticLodSelect(draw,instance,firstOffset,firstCount,secondOffset,secondCount,fade,firstLevel,secondLevel,nativeSknSelectedLevel);
 // Crown impostors: the record's quad far away, both in the crossfade band.
 bool impostorSecond=false;
 [branch]if((instance.worldGeometry&@WORLD_IMPOSTOR@)!=0&&nativeImpostorParams.x>0&&draw.preservesOrder==0&&!split)
  nativeImpostorSelect(draw,instance,firstCount,secondCount,fade,impostorSecond);
 if(lane==0)nativeObserveLod(draw,instance,2,firstCount!=0?firstLevel:0xffffffffu,
  impostorSecond?0xfffffffeu:secondLevel,firstCount,secondCount,fade);
 uint total=firstCount+secondCount;
 // Every triangle of a visible record is emitted, so the group reserves its
 // whole range (and, when split, its steep range) with one atomic each.
 uint flatCount=split?min(draw.steepFirstTriangle,total):total;
 if(lane==0&&draw.preservesOrder==0){
  uint base=0;if(flatCount!=0)nativeIndirectArgs.InterlockedAdd(draw.indirectArgsByteOffset,flatCount*3,base);
  nativeShadowGroupBase=base;
  base=0;if(total>flatCount)nativeIndirectArgs.InterlockedAdd(work.steepArgsByteOffset,(total-flatCount)*3,base);
  nativeBuildSteepBase=base;
 }
 GroupMemoryBarrierWithGroupSync();
#ifdef EID3D12
 // Indexed records (never ordered or split): the owner entries of both
 // levels, with the codes the triangle loop below gives their corners.
 bool indexed=nativeIndexedBuild!=0&&(work.flags&4u)!=0&&draw.preservesOrder==0&&!split;
 uint secondOwners=work.ownerBase+draw.componentCount;
 uint ownerVertexBase=(instance.worldGeometry&(@WORLD_OBJECT_SPACE@|@WORLD_LOD@))!=0?0:draw.preparedVertexBase;
 [branch]if(indexed){
  uint firstCode=fade!=0?fade<<16:0u;
  uint secondCode=(fade!=0?(fade|256u)<<16:0u)|(impostorSecond?1024u<<16:0u);
  for(uint component=lane;component<draw.componentCount;component+=64){
   if(firstCount!=0)nativeOwnerOutput[work.ownerBase+component]=uint2(draw.instanceIndex,(ownerVertexBase+component)|firstCode);
   if(secondCount!=0&&!impostorSecond)nativeOwnerOutput[secondOwners+component]=uint2(draw.instanceIndex,(ownerVertexBase+component)|secondCode);
  }
  if(impostorSecond&&lane<4)nativeOwnerOutput[secondOwners+lane]=uint2(draw.instanceIndex,(ownerVertexBase+lane)|secondCode);
 }
#endif
 for(uint tri=lane;tri<total;tri+=64){
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
#ifdef EID3D12
  [branch]if(indexed){nativeIndexOutput.Store3(outputTriangle*12u,(second?secondOwners:work.ownerBase)+componentIndex);continue;}
#endif
  // Crossfade code above the 16-bit component index (object-space records
  // only): the successor level takes the other half of the dither.
  uint code=(fade!=0?(fade|(second?256u:0u))<<16:0u)|(second&&impostorSecond?1024u<<16:0u);
  uint base=(instance.worldGeometry&(@WORLD_OBJECT_SPACE@|@WORLD_LOD@))!=0?0:draw.preparedVertexBase;
  nativeTriangleOutput[outputTriangle]=uint4(draw.instanceIndex,(base+componentIndex.x)|code,
                                           (base+componentIndex.y)|code,(base+componentIndex.z)|code);
 }
}
)" R"(
void nativeBuildSelect(uint local){
 NativeDraw draw;if(!loadNativeDraw(uint3(local%65535u,local/65535u,0),draw))return;
 NativeInstance instance=loadNativeInstance(draw.instanceIndex);
 // This dispatch selects 64 different records: its LOD belongs to the lane,
 // not to the workgroup shared by those records.
 uint sknSelectedLevel=nativeSknLodUpdate(draw,instance);
 bool visible;
 if(instance.worldGeometry!=0){
  nativeCurrentBounds(draw,instance);
  float swayReach=treeSwayReach(instance);
  draw.boundsMin.xyz-=swayReach;draw.boundsMax.xyz+=swayReach;
  visible=draw.colorVisible!=0&&(draw.preservesOrder!=0||drawVisible(draw,instance));
 }else visible=draw.preservesOrder!=0||drawVisible(draw,instance);
 uint entry=nativeBuildEntry(local);
 if(!visible){nativeObserveLod(draw,instance,1,0xffffffffu,0xffffffffu,0,0,0);nativeIndirectArgs.Store(entry,0);return;}
 // The whole build's choices (nativeBuildWhole), once per record.
 NativeDrawWork work=nativeDrawWork[nativeWorkBase+local];
 bool split=work.steepArgsByteOffset!=0xffffffffu;
 uint firstOffset=draw.indexOffset,firstCount=draw.indexCount/3,secondOffset=0,secondCount=0,fade=0;
 uint firstLevel=0,secondLevel=0xffffffffu;
 [branch]if((instance.worldGeometry&@WORLD_LOD@)!=0&&((nativeDistanceCull.z>0&&draw.preservesOrder==0)||instance.sknFactors.w>0)&&!split)
  nativeStaticLodSelect(draw,instance,firstOffset,firstCount,secondOffset,secondCount,fade,firstLevel,secondLevel,sknSelectedLevel);
 bool impostorSecond=false;
 [branch]if((instance.worldGeometry&@WORLD_IMPOSTOR@)!=0&&nativeImpostorParams.x>0&&draw.preservesOrder==0&&!split)
  nativeImpostorSelect(draw,instance,firstCount,secondCount,fade,impostorSecond);
 nativeObserveLod(draw,instance,2,firstCount!=0?firstLevel:0xffffffffu,impostorSecond?0xfffffffeu:secondLevel,firstCount,secondCount,fade);
 uint total=firstCount+secondCount;
 uint flatCount=split?min(draw.steepFirstTriangle,total):total,flatBase=0,steepBase=0;
 if(draw.preservesOrder==0){
  if(flatCount!=0)nativeIndirectArgs.InterlockedAdd(draw.indirectArgsByteOffset,flatCount*3,flatBase);
  if(total>flatCount)nativeIndirectArgs.InterlockedAdd(work.steepArgsByteOffset,(total-flatCount)*3,steepBase);
 }
 nativeIndirectArgs.Store4(entry,uint4(0x80000000u|(impostorSecond?0x40000000u:0u)|fade,firstOffset,firstCount,secondOffset));
 nativeIndirectArgs.Store4(entry+16,uint4(secondCount,flatBase,steepBase,flatCount));
}
void nativeBuildCommit(uint local){
 NativeDraw draw;if(!loadNativeDraw(uint3(local%65535u,local/65535u,0),draw))return;
 if(draw.prepareWorld==0||draw.worldState==0xffffffffu)return;
 if((nativeIndirectArgs.Load(nativeBuildEntry(local))&0x80000000u)==0)return;
 NativeInstance instance=loadNativeInstance(draw.instanceIndex);
 if(instance.worldGeometry==0)return;
 NativeWorldState value=nativeWorldStateOutput[draw.worldState];
 if(all(value.geometry==draw.worldGeneration)&&all(value.pose==nativePoseGeneration(instance)))return;
 nativeCommitWorld(draw,instance);
}
)" R"(
// One chunk of a selected record (step 2): its entry, its components and its
// triangles in [512*z, 512*(z+1)), the last chunk to the end.
void nativeBuildChunk(uint3 groupId,uint lane){
 NativeDraw draw;if(!loadNativeDraw(groupId,draw))return;
 uint local=groupId.x+groupId.y*65535u;
 // The record's entry, through group memory (one read, uniform flow).
 if(lane<2)nativeBuildSelection[lane]=nativeIndirectArgs.Load4(nativeBuildEntry(local)+lane*16);
 GroupMemoryBarrierWithGroupSync();
 uint4 selection=nativeBuildSelection[0],ranges=nativeBuildSelection[1];
 if((selection.x&0x80000000u)==0)return;
 uint firstOffset=selection.y,firstCount=selection.z,secondOffset=selection.w,secondCount=ranges.x;
 uint fade=selection.x&1023u,total=firstCount+secondCount,flatCount=ranges.w;
 bool impostorSecond=(selection.x&0x40000000u)!=0;
 uint chunkFirst=groupId.z*kNativeBuildChunk;
 bool lastChunk=groupId.z+1>=(nativeWorkChunks&0xffffu);
 if(chunkFirst>=max(draw.componentCount,total))return;
 NativeInstance instance=loadNativeInstance(draw.instanceIndex);
 uint componentLast=lastChunk?draw.componentCount:min(chunkFirst+kNativeBuildChunk,draw.componentCount);
 if(instance.worldGeometry!=0){
  if(draw.prepareWorld!=0)nativePrepareWorld(draw,instance,lane,chunkFirst,componentLast);
 }else
  for(uint component=chunkFirst+lane;component<componentLast;component+=64)
   nativePreparedOutput[draw.preparedVertexBase+component]=prepareNativeVertex(instance,draw.componentOffset+component*16);
 NativeDrawWork work=nativeDrawWork[nativeWorkBase+local];
 bool split=work.steepArgsByteOffset!=0xffffffffu;
#ifdef EID3D12
 // Indexed records: this chunk's owner entries (the impostor quad's with the first chunk).
 bool indexed=nativeIndexedBuild!=0&&(work.flags&4u)!=0&&draw.preservesOrder==0&&!split;
 uint secondOwners=work.ownerBase+draw.componentCount;
 uint ownerVertexBase=(instance.worldGeometry&(@WORLD_OBJECT_SPACE@|@WORLD_LOD@))!=0?0:draw.preparedVertexBase;
 [branch]if(indexed){
  uint firstCode=fade!=0?fade<<16:0u;
  uint secondCode=(fade!=0?(fade|256u)<<16:0u)|(impostorSecond?1024u<<16:0u);
  for(uint component=chunkFirst+lane;component<componentLast;component+=64){
   if(firstCount!=0)nativeOwnerOutput[work.ownerBase+component]=uint2(draw.instanceIndex,(ownerVertexBase+component)|firstCode);
   if(secondCount!=0&&!impostorSecond)nativeOwnerOutput[secondOwners+component]=uint2(draw.instanceIndex,(ownerVertexBase+component)|secondCode);
  }
  if(impostorSecond&&groupId.z==0&&lane<4)nativeOwnerOutput[secondOwners+lane]=uint2(draw.instanceIndex,(ownerVertexBase+lane)|secondCode);
 }
#endif
 uint last=lastChunk?total:min(chunkFirst+kNativeBuildChunk,total);
 for(uint tri=chunkFirst+lane;tri<last;tri+=64){
  bool second=tri>=firstCount;
  uint indexBase=second?secondOffset+(tri-firstCount)*12:firstOffset+tri*12;
  uint3 componentIndex;
  [branch]if(second&&impostorSecond)componentIndex=tri==firstCount?uint3(0,1,2):uint3(2,1,3);
  else componentIndex=uint3(nativeGeometry.Load(indexBase),nativeGeometry.Load(indexBase+4),nativeGeometry.Load(indexBase+8));
  uint outputTriangle;
  if(draw.preservesOrder!=0)outputTriangle=draw.outputTriangleBase+draw.orderedTriangleOffset+tri;
  else if(tri>=flatCount)outputTriangle=work.steepOutputTriangleBase+ranges.z/3+(tri-flatCount);
  else outputTriangle=draw.outputTriangleBase+ranges.y/3+tri;
#ifdef EID3D12
  [branch]if(indexed){nativeIndexOutput.Store3(outputTriangle*12u,(second?secondOwners:work.ownerBase)+componentIndex);continue;}
#endif
  uint code=(fade!=0?(fade|(second?256u:0u))<<16:0u)|(second&&impostorSecond?1024u<<16:0u);
  uint base=(instance.worldGeometry&(@WORLD_OBJECT_SPACE@|@WORLD_LOD@))!=0?0:draw.preparedVertexBase;
  nativeTriangleOutput[outputTriangle]=uint4(draw.instanceIndex,(base+componentIndex.x)|code,
                                           (base+componentIndex.y)|code,(base+componentIndex.z)|code);
 }
}
[numthreads(64,1,1)]void nativeBuildCS(uint3 groupId:SV_GroupID,uint3 threadId:SV_GroupThreadID){
 uint step=(nativeWorkChunks>>16)&3u;
 if(step==0){nativeBuildWhole(groupId,threadId.x);return;}
 if(step==2){nativeBuildChunk(groupId,threadId.x);return;}
 uint local=(groupId.x+groupId.y*65535u)*64u+threadId.x;
 if(step==1)nativeBuildSelect(local);else nativeBuildCommit(local);
}

)" R"(
// The slim terrain pass's miss lists (terrain_slim; NativeBatch::miss_list_*):
// between a batch's two passes, the triangles of one region (nativeBuildCS
// output) with a missed pixel (terrainMissTriangles, set by the slim pass)
// are copied to the region's list, so the second pass rasterises only those.
cbuffer NativeTerrainMissList:register(b9){uint missRegionBase;uint missRegionMax;uint missListBase;uint missRegionArgs;
 uint missListArgs;uint3 missListPadding;};
RWByteAddressBuffer terrainMissOriginOutput:register(u2); // each list entry's slot in its region (terrainMissOrigin)
groupshared uint missGroupCount;
groupshared uint missGroupBase;
[numthreads(64,1,1)]void nativeTerrainMissCompactCS(uint3 groupId:SV_GroupID,uint3 threadId:SV_GroupThreadID){
 uint index=(groupId.x+groupId.y*65535u)*64u+threadId.x;
 uint count=min(nativeIndirectArgs.Load(missRegionArgs)/3u,missRegionMax);
 if(threadId.x==0)missGroupCount=0;
 GroupMemoryBarrierWithGroupSync();
 bool keep=index<count&&terrainTriangleMissed(missRegionBase+index);
 uint local=0;
 if(keep)InterlockedAdd(missGroupCount,1u,local);
 GroupMemoryBarrierWithGroupSync();
 if(threadId.x==0){uint base=0;if(missGroupCount!=0)nativeIndirectArgs.InterlockedAdd(missListArgs,missGroupCount*3u,base);missGroupBase=base/3u;}
 GroupMemoryBarrierWithGroupSync();
 if(keep){
  nativeTriangleOutput[missListBase+missGroupBase+local]=nativeTriangleOutput[missRegionBase+index];
  terrainMissOriginOutput.Store((missListBase+missGroupBase+local)*4u,missRegionBase+index);
 }
}
)" R"(
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
  if(groupId.z==0)nativeSknLodUpdate(draw,instance);
  nativeGroupVisible=nativeShadowPrepareVisible(draw,instance)?1:0;nativeGroupPrepare=0;
  if(nativeGroupVisible!=0&&draw.worldState!=0xffffffffu){NativeWorldState previous=nativeWorldStateOutput[draw.worldState];
   nativeGroupPrepare=any(previous.geometry!=draw.worldGeneration)||any(previous.pose!=nativePoseGeneration(instance));}
 }
 GroupMemoryBarrierWithGroupSync();if(nativeGroupPrepare==0)return;
 uint last=min(first+kNativeShadowChunk,draw.componentCount);
 for(uint component=first+threadId.x;component<last;component+=64)
  storeNativeWorldVertex(draw.preparedVertexBase+component,prepareNativeWorldVertex(instance,draw.componentOffset+component*16));
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
 [loop]for(uint level=1;level<=levels;++level){NATIVE_LOOP_GUARD(3u,level-1u,levels)
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
 if(instance.sknFactors.w>0&&(instance.worldGeometry&@WORLD_LOD@)!=0){
  uint table=draw.indexOffset-64,level=nativeWorldStates[draw.worldState].sknLod;
  allTriangles=nativeGeometry.Load(table+4);
  if(level>0){uint2 entry=nativeGeometry.Load2(table+level*16);indexOffset=entry.x;allTriangles=entry.y;}
  if(firstTriangle>=allTriangles)return;
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
#ifdef EID3D12_HAZARDS
  // caster_check (item 5): the renderer tags the records of every admitted
  // main-map static (bits 0-29 its check object, 30-31 its path: 1 posed in
  // the static caster store, 2 object-space caster). Each object's paths that
  // drew into a sun map this frame are or-ed into its mask; the header counts
  // objects per path and those drawn by both (doubled). Header at byte 1040
  // (the last report record): {table offset, table entries, objects of path
  // 1 (low 16 bits) and 2 (high), doubled}; the masks follow the 2 MB table.
  [branch]if(nativeGroupVisible!=0&&groupId.z==0){const uint checkTable=nativeGpuReport.Load(1040);
   const uint checkRecord=draw.instanceIndex&0x7fffffffu;
   [branch]if(checkTable!=0&&checkRecord<nativeGpuReport.Load(1044)){const uint tag=nativeGpuReport.Load(checkTable+checkRecord*4);
    [branch]if(tag!=0){const uint bit=tag>>30;uint previous=0,ignored=0;
     nativeGpuReport.InterlockedOr(checkTable+(1u<<21)+(tag&0x3fffffffu)*4,bit,previous);
     if((previous&bit)==0)nativeGpuReport.InterlockedAdd(1048,bit==1u?1u:65536u,ignored);
     if((previous|bit)==3u&&previous!=3u)nativeGpuReport.InterlockedAdd(1052,1u,ignored);}}}
#endif
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
// F10 fur_debug draws through the material instance check programs
// (NATIVE_DOMAIN_CHECK, debug only): only they carry its vertex bits,
// discards and colours, so the shipping programs pay nothing for it.
#ifdef NATIVE_DOMAIN_CHECK
#define NATIVE_FUR_DEBUG 1
#endif
// Material instance vertex programs (the shader split, renderer.h
// kNativeInstance*): an instance record is never terrain (native_material_
// instance keeps terrain-vertex records general) and, outside the crown
// impostor domain 4, never draws an impostor quad (no impostor table; the
// pixel check run counted none). 72 -> 56 temps for nativeVS.
#if defined(NATIVE_DOMAIN)&&NATIVE_DOMAIN!=0
#define NATIVE_VS_TERRAIN(instance) false
#else
#define NATIVE_VS_TERRAIN(instance) ((instance).terrainVertexOffset!=0xffffffff)
#endif
#if defined(NATIVE_DOMAIN)&&NATIVE_DOMAIN>=1&&NATIVE_DOMAIN<=3
#define NATIVE_VS_IMPOSTOR(objectSpace,preparedIndex) false
#else
#define NATIVE_VS_IMPOSTOR(objectSpace,preparedIndex) ((objectSpace)&&(((preparedIndex)>>16)&1024u)!=0)
#endif
#ifdef NATIVE_ASTRAL
// Astral rift veils and clumps (astral_rift.h; nativeFlags bits 16-17 kind,
// 18-20 veil index), on the game clock (nativeTerrainClock.y: world segments of 55 ms,
// frozen on pause, scaled with the game speed; one cycle per 6 s). Applied
// to the cached rest vertex through the node's matrix, so the object's
// rotation and scale carry the motion.
// Veils sway across the strip, still at its two edges. Clumps (U: id*2 +
// 0..1) drift along and away from the node's axis and rise, fading over
// their life; their UV becomes their atlas cell.
void nativeAstralVertex(NativeInstance instance,inout NativeWorldVertex world,out float4 astral){
 float phase=frac(nativeTerrainClock.y*(.055/6.));
 float3x3 basis=(float3x3)instance.objectToWorld;
 astral=float4((instance.nativeFlags>>16)&3u,(instance.nativeFlags>>18)&7u,phase,1);
 [branch]if(astral.x<1.5){
  float u=saturate((world.uv.x-NATIVE_ASTRAL_U0)/NATIVE_ASTRAL_UW);
  float w=sin(3.14159265*u);w*=w;
  float wave=sin(u*9.42477796+astral.y+phase*6.28318531)*w;
  world.p.xyz+=mul(basis,float3(.10,.12,.28)*wave);
 }else{
  float id=floor(world.uv.x*.5),age=frac(phase*(2+fmod(id,2))+id*.61803399);
  float3 local=mul(inverseObjectLinear(instance.objectToWorld),
   world.p.xyz-float3(instance.objectToWorld[0][3],instance.objectToWorld[1][3],instance.objectToWorld[2][3]));
  float2 outward=local.xy/max(length(local.xy),.001);
  float3 drift=float3(-outward.y,outward.x,.15)*age*3+float3(outward*.9,.55)*age*age*2.6;
  world.p.xyz+=mul(basis,drift);
  float fade=sin(3.14159265*age);astral.w=fade*fade*.8;
  world.uv=float2(NATIVE_ASTRAL_U0+fmod(world.uv.x,2)*NATIVE_ASTRAL_UW,NATIVE_ASTRAL_CLUMP_V0+world.uv.y*NATIVE_ASTRAL_UW);
 }
}
#endif
)" R"(
NativeOutput nativeVertex(uint vertexId,bool sway){
#ifdef NATIVE_INDEXED
 // An indexed draw: the vertex id is the index value, its owner entry the
 // instance and prepared index (nativeBuildCS). No terrain record draws
 // indexed, so the per-triangle terrain paths below never run.
 uint2 owner=nativeVertexOwners[vertexId];
 uint4 triangleRef=uint4(owner.x,owner.yyy);
 uint preparedIndex=owner.y;
#else
 uint4 triangleRef=nativeVisibleTriangles[nativeTriangleBase+vertexId/3];
 uint preparedIndex=(vertexId%3)==0?triangleRef.y:((vertexId%3)==1?triangleRef.z:triangleRef.w);
#endif
 NativeInstance instance=loadNativeInstance(triangleRef.x);NativeOutput output=(NativeOutput)0;
 if(instance.poseNode!=0xffffffff&&instance.worldGeometry==0)instance.objectToWorld=nativeNodeMatrix(instance.poseNode);
 if(instance.worldGeometry==0){
  NativePreparedVertex input=nativePreparedVertices[preparedIndex];
  output.p=input.p;output.d=input.d;output.s=input.s;output.uv=input.uv;output.textureReference=input.textureReference;
  return output;
 }
 NativeWorldVertex world;
 bool objectSpace=(instance.worldGeometry&@WORLD_OBJECT_SPACE@)!=0;
 bool impostor=NATIVE_VS_IMPOSTOR(objectSpace,preparedIndex);
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
#ifdef NATIVE_FIRE_BURN
 if((instance.nativeFlags&0x200u)!=0&&treeSwayAnchored(instance))output.burnRoot=float4(treeSwayBase(instance),1);
#endif
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
 // Bits 24-26: the F10 fur_debug code of a unit record (instance flag bits
 // 10-12 without the sway flag, native_instance_flags.h); debug programs only.
#ifdef NATIVE_FUR_DEBUG
 if((instance.nativeFlags&0x200u)==0)output.swaying|=((instance.nativeFlags>>10)&7u)<<24;
#endif
 if(instance.poseNode!=0xffffffff)instance.objectToWorld=nativeNodeMatrix(instance.poseNode+world.partNode);
#ifdef NATIVE_ASTRAL
 nativeAstralVertex(instance,world,output.astral);
#endif
 output.worldPosition=world.p.xyz;output.worldNormal=world.normal.xyz;

 output.p=mul(instance.objectToClip,world.p);
 bool isTerrain=NATIVE_VS_TERRAIN(instance);
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
 if(instance.lightingEnabled!=0&&(nativeCombinerTail&1u)==0){ // nativeCombinerTail: the zone hit-test frame takes the default colour
  // Preserve the established object-space material response, independently of
  // the world normal used by the light-space geometry/receiver passes.
  float3 normal=normalize(mul(transpose((float3x3)instance.objectToWorld),world.normal.xyz));
  output.d=calculateWorldLighting(instance,normal,isTerrain?terrain.emissive:instance.nativeEmissive.rgb,
                                  (instance.worldGeometry&@WORLD_VERTEX_OPACITY@)!=0?world.normal.w:
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
   // Dry material vertices: x the rise over the local floor (wall-foot band).
   // yzw: this corner's barycentric coordinate (terrainCliffFacetKeep).
   if(!liquidSurfaceEnabled(terrain.flags)){
    output.liquidFall=float4(terrain.liquidDrop,(vertexId%3)==0,(vertexId%3)==1,(vertexId%3)==2);
    // x: how near a wall rises beside the vertex (terrainFootContact).
    output.liquidDirections=float4(terrain.liquidBend,0,0,0);
   }
  }
  if((terrain.flags&16384u)!=0) {
   output.liquidDirections.y=terrain.noise;
   output.liquidTileLocal=float2((terrain.flags>>25)&3u,(terrain.flags>>27)&3u)*.5;
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
 // Interface records (nativeCombinerTail bit 2: portraits, panel doll) have
 // no world camera: their view direction comes from their own clip, in the
 // liquid view field figures leave free (fogEye is the world camera's).
 [branch]if((nativeCombinerTail&4u)!=0&&!isTerrain)output.liquidView=liquidViewDirection(instance.objectToClip,output.worldPosition);
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
 if(instance.sknFactors.w>0){output.swaying|=1u<<28;output.liquidTintDepth=world.tangent;output.liquidFlow.w=float((triangleRef.x&0x7fffffffu)+1);}
 return output;
}
NativeOutput nativeVS(uint vertexId:SV_VertexID){
#ifdef EID3D12_HAZARDS
 nativeGpuReportBegin(1u,vertexId,0xFFFFFFFFu);
#endif
 NativeOutput o=nativeVertex(vertexId,true);precise float4 position=o.p;o.p=position;return o;}
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
// Held sun maps (sun_shadow_smooth). A close sun map keeps the sun it was
// drawn along until the sun has turned far enough to redraw it, so lookups
// along the held sun step the shadows across the ground. The ray from a
// receiver toward the exact sun (nativeWorldSunDirection) crosses the held
// map along a line: a blocker whose depth lies g (map depth) above the
// receiver's sits at texel + shear * g. Zero while the correction is off
// (shadowCamera[5][0].x: sun maps leave the sixth matrix to the lamp cubes)
// or the map's sun is the exact one. The forward material programs keep the
// held lookup (shader size gate); their surfaces are a small share of the
// sun's receivers next to the mask pass, terrain and ground.
float2 nativeShadowShear(uint slice,float side){
#if defined(NATIVE_DOMAIN)&&!defined(NATIVE_SHADOW_MASK)
 return 0;
#else
 if(shadowCamera[5][0].x<.5)return 0;
 const float3 sun=nativeWorldSunDirection.xyz;
 const float rise=dot(shadowLight[slice][2].xyz,sun);
 if(abs(rise)<1e-12)return 0;
 const float2 shear=float2(dot(shadowLight[slice][0].xyz,sun),dot(shadowLight[slice][1].xyz,sun))*float2(-.5*side,.5*side)/rise;
 return all(isfinite(shear))?shear:0;
#endif
}
// The soft filter for thin and cut-out casters (sun_foliage_filter,
// shadowCamera[5][0].y): 0 soft as everywhere, 1 steady soft, 2 the fixed
// filter where the blockers look cut-out or thin. The forward material
// programs keep 0 (size gate).
uint nativeSunFoliageFilter(){
#if defined(NATIVE_DOMAIN)&&!defined(NATIVE_SHADOW_MASK)
 return 0u;
#else
 return uint(shadowCamera[5][0].y+.5);
#endif
}
// The soft filter's blocker search: x = blockers found, y = their summed
// depth, z/w = nearest and farthest blocker depth. Each tap compares against
// the receiver plane (with its bias) at the read texel's centre, which is
// not its own blocker. gathered (steady soft): a tap compares its 2x2
// footprint and counts the blocked share, so a cut-out caster's texel-sized
// holes move the estimate by a quarter tap, not a whole one, when its map is
// drawn again.
float4 nativeShadowBlockers(uint slice,float3 coordinate,float2 texel,float2 depthStep,float bias,float side,
                            float search,uint searchTaps,float2 start,bool gathered){
 // nativeVogel's disk without a sincos per tap (as the ground cover's
 // filter): each tap turns the direction by the golden angle (cos, sin of
 // 2.39996323).
 const float2 golden=float2(-.73736887,.67549029);
 float2 direction=start;
 float4 found=float4(0,0,1,0);
 const float searchScale=search*rsqrt(float(searchTaps)); // sqrt((k+.5)/searchTaps)*search
 [loop]for(uint k=0;k<searchTaps;++k){NATIVE_LOOP_GUARD(5u,k,searchTaps)
  const float2 at=clamp(texel+direction*(sqrt(k+.5)*searchScale),.5,side-.5);
  direction=float2(direction.x*golden.x-direction.y*golden.y,direction.x*golden.y+direction.y*golden.x);
  [branch]if(gathered){
   // Gather returns (0,1) (1,1) (1,0) (0,0) of the 2x2 block at base.
   const float2 base=floor(at-.5);
   const float4 stored=shadowDepth.GatherRed(shadowGatherSampler,float3((base+1)/side,slice));
   const float2 corner=base+.5-texel;
   const float4 plane=coordinate.z-bias+float4(dot(depthStep,corner+float2(0,1)),dot(depthStep,corner+1),
                                               dot(depthStep,corner+float2(1,0)),dot(depthStep,corner));
   const float4 hit=stored<plane?1:0;
   found.x+=dot(hit,.25);found.y+=dot(hit,stored)*.25;
   found.z=min(found.z,min(min(stored.x,stored.y),min(stored.z,stored.w))+(1-max(max(hit.x,hit.y),max(hit.z,hit.w))));
   found.w=max(found.w,max(max(hit.x*stored.x,hit.y*stored.y),max(hit.z*stored.z,hit.w*stored.w)));
  }else{
   const float stored=shadowDepth.Load(int4(int2(at),slice,0));
   if(stored<coordinate.z+dot(depthStep,floor(at)+.5-texel)-bias)
    found=float4(found.x+1,found.y+stored,min(found.z,stored),max(found.w,stored));
  }
 }
 return found;
}
// The fixed filters' centre on a held map: walk the exact sun's ray across
// the map from the receiver's texel and stop at the first texel whose own
// blocker gap puts it on that ray (within a texel), so a caster one texel
// wide (a bare branch) is found wherever it lies, not only when a filter
// tap lands on it. With no crossing, the held blocker at the receiver's
// texel (if any) is off the exact ray: the centre moves by its gap, off it.
// At most 32 texels (nativeShadowFilter widens its early-out reach by it).
float2 nativeShadowRayShift(uint slice,float3 coordinate,float2 texel,float2 depthStep,float bias,float side,float2 shear){
 const float speed=length(shear),reach=min(speed*coordinate.z,32.0);
 if(reach<.25)return 0;
 const float2 along=shear/speed;
 const float2 own=clamp(texel,.5,side-.5);
 const float held=coordinate.z-bias-shadowDepth.Load(int4(int2(own),slice,0));
 const float ownGap=held+dot(depthStep,floor(own)+.5-texel);
 if(ownGap>0&&speed*ownGap<=1)return along*min(speed*ownGap,reach);
 const uint steps=uint(ceil(reach));
 [loop]for(uint i=1;i<=steps;++i){NATIVE_LOOP_GUARD(12u,i,steps+1u)
  const float2 at=clamp(texel+along*float(i),.5,side-.5);
  const float gap=coordinate.z+dot(depthStep,floor(at)+.5-texel)-bias-shadowDepth.Load(int4(int2(at),slice,0));
  if(gap>0&&abs(speed*gap-float(i))<=1)return along*min(speed*gap,reach);
 }
 return held>0?along*min(speed*held,reach):0;
}
// The soft filter. found: the blocker search (nativeShadowBlockers) at
// radius search; shear moves the disk to the exact sun's blocker
// (nativeShadowShear), kept inside the search.
float nativeShadowSoft(uint slice,float3 coordinate,float2 texel,float2 depthStep,float bias,float side,
                       float minTexels,float search,float4 found,float2 start,float2 shear){
 // Orthographic maps: depth is linear, so depth differences are world gaps.
 float texelWorld=max(shadowBias[slice].x,1e-6),depthWorld=1/max(shadowBias[slice].y,1e-12);
 bool high=shadowBias[5].z>1.5;
 // Every receiver is filtered: a missed search only narrows the kernel, it
 // never makes a binary lit decision along the texel grid.
 float radius=minTexels;
 float2 shift=0;
 if(found.x>0){
  float gap=max(coordinate.z-found.y/found.x,0);
  radius=clamp(gap*depthWorld*shadowBias[5].x/texelWorld,minTexels,search);
  shift=shear*gap;
  const float length2=dot(shift,shift);
  if(length2>search*search)shift*=search*rsqrt(length2);
 }
 uint taps=high?24:12;
 float visibility=0;const float scale=radius*rsqrt(float(taps)); // sqrt((t+.5)/taps)*radius
 // The filter disk is turned 1.3 rad from the search disk (cos, sin of 1.3),
 // and turns by the golden angle per tap (nativeShadowBlockers).
 const float2 golden=float2(-.73736887,.67549029);
 float2 direction=float2(start.x*.26749883-start.y*.96355819,start.x*.96355819+start.y*.26749883);
 [loop]for(uint t=0;t<taps;++t){NATIVE_LOOP_GUARD(6u,t,taps)
  float2 offset=direction*(sqrt(t+.5)*scale);
  direction=float2(direction.x*golden.x-direction.y*golden.y,direction.x*golden.y+direction.y*golden.x);
  visibility+=nativeShadowPlaneTap(slice,side,texel+shift+offset,texel,coordinate.z-bias,depthStep);
 }
 return visibility/taps;
}
)" R"(
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
  const uint foliage=nativeSunFoliageFilter();
  // Steady soft never takes a kernel narrower than the fixed filter's 2.5
  // texels, so a re-drawn cut-out caster moves only a small share of taps.
  if(soft&&foliage==1u)minTexels=max(minTexels,2.5);
  const float search=nativeShadowSearch(slice,minTexels);
  // Held-map correction: the soft disk moves by at most the search, the
  // fixed filters' centre by at most the ray walk's 32 texels.
  const float2 shear=nativeShadowShear(slice,side);
  const float shearReach=min(length(shear)*coordinate.z,soft&&foliage!=2u?search:32.0);
  // Read reach per axis: the soft taps' radius stays within the search, the
  // far 3x3 taps within 1.5 texels, the 5x5 taps within 2.5; the plane
  // compare's 2x2 Gather adds 1.5.
  int early=nativeShadowEarly(slice,texel,side,coordinate.z-bias,depthStep,
   (soft?search:farFootprint?1.5:2.5)+1.5+shearReach);
  [branch]if(early>=0)return early;
  // The soft kernel never falls below the fixed filter's footprint, so the
  // penumbra (a world radius) only widens the existing edge.
  [branch]if(soft){
   float2 start;sincos(rotation,start.y,start.x);
   const float4 found=nativeShadowBlockers(slice,coordinate,texel,depthStep,bias,side,search,
                                           shadowBias[5].z>1.5?16u:8u,start,foliage==1u);
   // Fixed foliage filter: blockers that look cut-out (none found; grass
   // within a metre of the receiver; crowns, needles and bare branches whose
   // depths spread over more than half a metre in the search) take the
   // fixed filter below. Solid casters stay soft.
   const float depthWorld=1/max(shadowBias[slice].y,1e-12);
   const bool cutOut=foliage==2u&&(found.x==0||(coordinate.z-found.y/found.x)*depthWorld<1||(found.w-found.z)*depthWorld>.5);
   [branch]if(!cutOut)
    return nativeShadowSoft(slice,coordinate,texel,depthStep,bias,side,minTexels,search,found,start,shear);
  }
  // Held-map correction for the fixed filters: their centre moves to where
  // the exact sun's ray crosses a blocker; the receiver plane stays anchored
  // at the receiver's own texel.
  float2 lookupTexel=texel;
  [branch]if(any(shear!=0))lookupTexel+=nativeShadowRayShift(slice,coordinate,texel,depthStep,bias,side,shear);
  // The owned spatial maps carry a world-space texel size for this footprint.
  if(farFootprint){
    // Keep the far filter's tap centers within one world unit even when a
    // broad map covers much more ground per texel. The bilinear plane compare
    // supplies the remaining subtexel transition.
    float spacing=min(1.5,.5/max(shadowBias[slice].x,1e-6));
    float visibility=0;
    [loop]for(int y=-1;y<=1;y++)[loop]for(int x=-1;x<=1;x++){
     float2 sampleTexel=lookupTexel+float2(x,y)*spacing;
     float weight=(x==0?2:1)*(y==0?2:1);
     visibility+=weight*nativeShadowPlaneTap(slice,side,sampleTexel,texel,coordinate.z-bias,depthStep);
    }
    return visibility/16;
  }
  float2 center=floor(lookupTexel)+.5,f=lookupTexel-center;
  // A compact, C1-continuous 5x5 footprint softens moving raster silhouettes
  // without changing the sun or losing per-texel receiver-plane depth bias.
  // A tap reaches zero weight as it enters/leaves at 2.5 texels.
  // Tap weights are recomputed per tap (no weight arrays), so the taps loop.
  float2 sum=0;
  [unroll]for(int tap=-2;tap<=2;tap++){
   float2 distance=abs(float2(tap,tap)-f);
   float2 taper=saturate(1-distance*distance*(4.0/25.0));
   sum+=taper*taper;
  }
  float visibility=0;
  [loop]for(int y=-2;y<=2;y++){
   float wy=saturate(1-(y-f.y)*(y-f.y)*(4.0/25.0));wy*=wy;
   [loop]for(int x=-2;x<=2;x++){
    float wx=saturate(1-(x-f.x)*(x-f.x)*(4.0/25.0));
    float2 sampleTexel=center+float2(x,y);
    // Extrapolation beyond the depth range must still compare clear depth as lit.
    float depth=saturate(coordinate.z+dot(depthStep,sampleTexel-texel)-bias);
    visibility+=wx*wx*wy*shadowDepth.SampleCmpLevelZero(
     shadowSampler,float3(sampleTexel/side,slice),depth);
   }
  }
  return visibility/(sum.x*sum.y);
}
float nativeShadowVisibility(float3 worldPosition,float3 positionDx,float3 positionDy,float2 pixel,bool temporal){
 uint slice=nativeShadowSlice(worldPosition);
 if(slice==5)return 1;
 if((shadowControl.x&(1u<<slice))==0)
  return shadowBias[4].z>.5?-1:1;
 float rotation=nativeShadowRotation(pixel,temporal);
 // The blend toward the next coarser map is decided before filtering, so
 // one inlined filter serves both maps (a loop of one or two passes).
 float blend=0;uint next=5;
 [branch]if(shadowBias[4].z>=.5){
  float4 light=mul(shadowLight[slice],float4(worldPosition,1));
  float edge=max(abs(light.x),abs(light.y))/light.w;
  const float blendStart=.75,blendEnd=1-7.0/max(1.0,(float)shadowControl.z);
  blend=smoothstep(blendStart,blendEnd,edge);
  if(slice<3&&shadowBias[slice].w>shadowBias[slice].z){
   float4 camera=mul(shadowCamera[slice],float4(worldPosition,1));
   // Perspective clip Z crowds most visible ground near 1. Blend over the
   // actual linear view-distance overlap shared with the next close map.
   blend=max(blend,smoothstep(shadowBias[slice].z,shadowBias[slice].w,camera.w));
  }
  // The first enabled coarser map holding the point (its containment test
  // only for enabled maps past slice, and none after the first hit).
  [branch]if(blend>0&&slice<4){
   [unroll]for(uint coarser=0;coarser<5;coarser++){
    [branch]if(next==5&&coarser>slice&&(shadowControl.x&(1u<<coarser))!=0){
     [branch]if(nativeShadowContains(coarser,worldPosition))next=coarser;
    }
   }
  }
 }
 // No coarser map: the blended share is lit. The loop carries three
 // values (keeps the program's registers at the single-map count): the
 // maps still to filter (4 bits each, 5 ends), the share and the sum.
 float visibility=next<5?0:blend,weight=1-blend;
 uint maps=slice|(next<<4)|(5u<<8);
 [loop]for(uint current=slice;current<5;current=(maps>>=4)&15u){
  visibility+=weight*nativeShadowFilter(current,worldPosition,positionDx,positionDy,rotation);
  weight=1-weight;
 }
 return visibility;
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
// The colour output is the shelter bake's foliage mask: the draw's own
// instance foliage flag (swaying bit 30, nativeFlags 0x100). A shadow batch
// takes its foliage flag from its first draw and may merge plant parts with
// other draws of the same state bucket, so the batch flag cannot mark
// foliage per texel. renderer_shadows.cpp binds the mask target only in that
// bake, every caster drawing with this stage, so the top surface's class
// wins; sun and point maps bind no colour target, which discards it.
// D3D12 binds rain12_.foliage beside the shelter slice as well.
float4 nativeShadowPS(NativeOutput i):SV_Target{
 NATIVE_REPORT_PIXEL(3u,i.textureReference.x,i.p)
 float4 t=1;
 // Shelter bake foliage (nativeContactLayout.z 1, renderer_shadows.cpp): the
 // straight-down view foreshortens steep cards, so the mip follows the
 // card's finer screen axis; the isotropic mip averaged the leaves away.
 float2 uvDx=ddx(i.uv),uvDy=ddy(i.uv);
 [branch]if(nativeTextured!=0&&nativeContactLayout.z>0){
  float2 g=dot(uvDx,uvDx)<dot(uvDy,uvDy)?uvDx:uvDy;
#ifdef EID3D12
  t=terrainTapGrad(i.textureReference,i.uv,g,g);
#else
  t=i.textureReference.y!=0?nativeTextureArray.SampleGrad(nativeSampler,float3(i.uv,i.textureReference.x),g,g):nativeTexture.SampleGrad(nativeSampler,i.uv,g,g);
#endif
 }else if(nativeTextured!=0)t=NATIVE_SAMPLE(i.textureReference,i.uv);
#ifdef NATIVE_FIRE_BURN
 // A burnt crown casts through its lost leaves (NATIVE_BURN_ALPHA below).
 [branch]if(nativeBurnRow(1).w>0&&((i.swaying>>29)&1u)==0&&nativeBurnMaterial(i.textureReference.x,i.uv)==2u){
  float charred=nativeBurnSurfacePhase(i.shadowPosition,i.burnRoot).y;
  if(charred>=.95)discard;
  t.a=nativeBurnLeafAlpha(t.a,i.shadowPosition,charred);
 }
#endif
 float alpha=nativeAlphaResult(nativeAlphaOp,nativeStageArg(nativeAlphaArg1,i.d,i.s,t),nativeStageArg(nativeAlphaArg2,i.d,i.s,t),i.d.a);
 if(nativeAlphaTest!=0&&!nativeCompare(alpha))discard;
 return ((i.swaying>>30)&1u)!=0?1:0;
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
  [loop]for(uint s=0;s<searchTaps;++s){NATIVE_LOOP_GUARD(7u,s,searchTaps)
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
// Material classes (pseudo-PBR S1, renderer_materials.cpp): a composed
// character atlas' profile (r roughness, g metal, b dielectric F0 x4, a
// cloth/leather sheen) at t37 while nativeCombinerTail bit 1 is set. The
// game's diffuse lighting stays as it is; specular, sky reflection and sheen
// are added on top, and metals keep nativeMaterialDiffuseKeep of the diffuse.
// Beside it at t38, the class relief: how strongly the texture's luma tilts the normal.
Texture2D<float4> nativeSknNormal:register(t123);
Texture2D<float4> nativeSknOrm:register(t124);
Texture2D<float4> nativeSknEmission:register(t125);
float4 nativeSknTap(uint map,uint reference,float2 uv,float2 dx,float2 dy){
#ifdef EID3D12
 Texture2D<float4> t=ResourceDescriptorHeap[NonUniformResourceIndex(nativeBindlessIndex(reference))];return t.SampleGrad(nativeSampler,uv,dx,dy);
#else
 if(map==0)return nativeSknNormal.SampleGrad(nativeSampler,uv,dx,dy);
 if(map==1)return nativeSknOrm.SampleGrad(nativeSampler,uv,dx,dy);
 return nativeSknEmission.SampleGrad(nativeSampler,uv,dx,dy);
#endif
}
Texture2D<float4> nativeMaterialProfile:register(t37);
Texture2D<float> nativeMaterialReliefMap:register(t38);
// D3D12: one state's draws share a batch across textures, so each draw names
// its profile itself: textureReference.y is the bindless index (0 none) of a
// two-slice array, slice 0 the profile, slice 1 the relief in red.
#ifdef EID3D12
#define NATIVE_MATERIAL_BOUND(i) ((i).textureReference.y!=0)
float4 nativeMaterialProfileTap(uint ref,float2 uv){Texture2DArray<float4> t=ResourceDescriptorHeap[NonUniformResourceIndex(nativeBindlessIndex(ref))];return t.Sample(nativeSampler,float3(uv,0));}
float nativeMaterialReliefTap(uint ref,float2 uv,float2 dx,float2 dy){Texture2DArray<float4> t=ResourceDescriptorHeap[NonUniformResourceIndex(nativeBindlessIndex(ref))];return t.SampleGrad(nativeSampler,float3(uv,1),dx,dy).r;}
#else
#define NATIVE_MATERIAL_BOUND(i) ((nativeCombinerTail&2u)!=0)
#define nativeMaterialProfileTap(ref,uv) nativeMaterialProfile.Sample(nativeSampler,uv)
#define nativeMaterialReliefTap(ref,uv,dx,dy) nativeMaterialReliefMap.SampleGrad(nativeSampler,uv,dx,dy)
#endif
)" R"(
// Material instance switches (the shader split): each record's material
// picks them once at registration (Renderer::native_material_instance); its
// batches draw the program compiled for them. NATIVE_DOMAIN: 0 the general
// program (every record), 1 units (unit flag), 2 world objects (no unit,
// foliage or impostor flag), 3 foliage parts without a crown impostor table,
// 4 records with a crown impostor table (static_lod.h native_world_impostor:
// trunk and crown parts of one decoration object). NATIVE_PROFILED 0: the texture has no material profile entry, so
// the S1 figure material code is compiled out. Each folded test is the value
// the record's own flags give at run time, so a program draws exactly the
// general program's pixels; NATIVE_DOMAIN_CHECK (F10 shader_split_check)
// compiles the run-time tests back in and paints a marker where one disagrees.
#ifndef NATIVE_DOMAIN
#define NATIVE_DOMAIN 0
#endif
#ifndef NATIVE_PROFILED
#define NATIVE_PROFILED 1
#endif
#define NATIVE_RT_UNIT(i) ((((i).swaying>>29)&1u)!=0)
#define NATIVE_RT_FOLIAGE(i) ((((i).swaying>>30)&1u)!=0)
#define NATIVE_RT_IMPOSTOR(i) ((((i).swaying>>8)&1024u)!=0)
#define NATIVE_RT_CONTACT(i) ((i).contact!=0)
#ifdef NATIVE_FIRE_BURN
// A burnt crown's leaf loss at an alpha test (nativeBurnLeafAlpha): every
// test of a foliage or crown impostor texel takes it, so the depth prepass,
// the shadow maps and the material pass drop the same leaves.
#define NATIVE_BURN_ALPHA(i,t) [branch]if(nativeBurnRow(1).w>0&&!NATIVE_RT_UNIT(i)&&nativeBurnMaterial((i).textureReference.x,(i).uv)==2u)\
 t.a=nativeBurnLeafAlpha(t.a,(i).shadowPosition,nativeBurnSurfacePhase((i).shadowPosition,(i).burnRoot).y)
#else
#define NATIVE_BURN_ALPHA(i,t)
#endif
#ifdef NATIVE_TERRAIN_LEAN
// NATIVE_TERRAIN_LEAN (D3D12, option terrain_slim_cuts): the slim terrain
// programs for batches whose every record is a plain terrain record
// (NativeBatch::terrain_records: terrain vertices, neither the unit nor the
// foliage flag, not object space). The vertex stage gives such a record
// contact 0 and none of the unit, foliage or impostor bits, so these tests
// are the constants the general terrain program reads at run time and the
// object-only code behind them (crown impostor texel, foliage and unit
// branches of wet and snow, the ground contact blend) compiles out.
#define NATIVE_REC_UNIT(i) false
#define NATIVE_REC_FOLIAGE(i) false
#define NATIVE_REC_IMPOSTOR(i) false
#define NATIVE_REC_CONTACT(i) false
#elif NATIVE_DOMAIN==1
#define NATIVE_REC_UNIT(i) true
#define NATIVE_REC_FOLIAGE(i) false
#define NATIVE_REC_IMPOSTOR(i) false
#define NATIVE_REC_CONTACT(i) false
#elif NATIVE_DOMAIN==2
#define NATIVE_REC_UNIT(i) false
#define NATIVE_REC_FOLIAGE(i) false
#define NATIVE_REC_IMPOSTOR(i) false
#define NATIVE_REC_CONTACT(i) NATIVE_RT_CONTACT(i)
#elif NATIVE_DOMAIN==3
#define NATIVE_REC_UNIT(i) false
#define NATIVE_REC_FOLIAGE(i) true
#define NATIVE_REC_IMPOSTOR(i) false
#define NATIVE_REC_CONTACT(i) NATIVE_RT_CONTACT(i)
#elif NATIVE_DOMAIN==4
#define NATIVE_REC_UNIT(i) false
#define NATIVE_REC_FOLIAGE(i) NATIVE_RT_FOLIAGE(i)
#define NATIVE_REC_IMPOSTOR(i) NATIVE_RT_IMPOSTOR(i)
#define NATIVE_REC_CONTACT(i) NATIVE_RT_CONTACT(i)
#else
#define NATIVE_REC_UNIT(i) NATIVE_RT_UNIT(i)
#define NATIVE_REC_FOLIAGE(i) NATIVE_RT_FOLIAGE(i)
#define NATIVE_REC_IMPOSTOR(i) NATIVE_RT_IMPOSTOR(i)
#define NATIVE_REC_CONTACT(i) NATIVE_RT_CONTACT(i)
#endif
#ifdef EID3D12
#define NATIVE_RT_PROFILE(i) ((i).textureReference.y!=0)
#else
#define NATIVE_RT_PROFILE(i) ((nativeCombinerTail&2u)!=0)
#endif
// The contact blend after the lamps and lighting (nativePS): crown impostor
// records only (45/47 -> 42/44 temps); world objects and plants keep the early
// blend (37 vs 39 temps prepassed late) and the general program stays as it was.
#define NATIVE_CONTACT_LATE (NATIVE_DOMAIN==4)
#if !NATIVE_PROFILED
#undef NATIVE_MATERIAL_BOUND
#define NATIVE_MATERIAL_BOUND(i) false
#endif
#if !NATIVE_PROFILED&&NATIVE_DOMAIN!=0
#define NATIVE_MATERIAL_WORLD materialActive
#elif NATIVE_DOMAIN==1
#define NATIVE_MATERIAL_WORLD false
#else
#define NATIVE_MATERIAL_WORLD materialWorld
#endif
// A crystal texel (classes.json "crystal"): a dielectric whose profile F0 is
// the crystal marker .10 (b = .4; gems take .12 and up). Only the crystal
// instance programs test it; every other program has no crystal code.
#ifdef NATIVE_CRYSTAL
#define NATIVE_CRYSTAL_TEXEL(profile) ((profile).g<.5&&abs((profile).b-.4)<.02)
#else
#define NATIVE_CRYSTAL_TEXEL(profile) false
#endif
static bool materialActive=false;
// materialWorld: a world object's profile derived from its own texture (S2);
// materialLamps: the lamp loop adds GGX highlights for this pixel.
static bool materialWorld=false,materialLamps=false;
// Plants (S2 foliage): leaves of trees and bushes (foliage parts, swaying
// bit 30) and crown impostors, a rough waxy dielectric with a soft sheen
// that lets the sun through when backlit. Trunks and branches carry no
// foliage flag and take the world-object response like rock and wood.
static bool materialFoliage=false;
static const float kFoliageRoughness=.62,kFoliageSheen=.5,kFoliageTransmission=.55;
// Rain-wet surfaces in the material response (Medium and up): a soaked
// surface is smoother, never a mirror. Porous stone, wood, soil and cloth
// toward .3, leaves (waxy, water beads) .25, metal keeps 70% of its
// roughness, but never below 45% of the dry value (grass ground .95 stops
// at .43: lower, its relief glittered like wet plastic, wet-pbr run b).
// Relief and normal detail flatten by up to a quarter under the film; the
// terrain's film highlight (S3, specular only) sees half its relief.
static const float kWetRoughness=.3,kWetFoliageRoughness=.25,kWetMetalRoughness=.7,kWetReliefDamp=.25,kWetFilmFlatten=.5;
float materialWetRoughness(float r,float target,float wet){return lerp(r,min(r,max(target,r*.45)),wet);}
static float materialAlpha2=1,materialWear=0;
static float3 materialView=float3(0,0,1);
// materialNormal: a figure's texture relief normal, taken just before the lamp
// loop (materialReliefLamps): its lamps, glow and lamp highlights light it.
static float3 materialNormal=float3(0,0,1);static bool materialReliefLamps=false;
// How much of the texture relief a pixel keeps. World objects (S2): none past
// ~6 texels per pixel (their slope there is mip noise on walls seen edge-on).
// Figures (S1): the taps follow the colour read's gradients, so at any
// distance they read neighbouring texels of the mip the colour uses, a sound
// slope; only a stretched footprint (a silhouette seen edge-on: 4:1 and more)
// fades it. The texel rule faded world armour to nothing at play distance
// (save1177 bz14h: ~5 texels per pixel, relief .2 of the doll's).
float materialReliefFade(float2 dx,float2 dy,float2 size,float texels,bool figure){
 if(!figure)return saturate((6-texels)*.25);
 float a=length(dx*size),b=length(dy*size);
 return saturate((8-max(a,b)/max(min(a,b),1e-3))*.25);
}
// The lamps' GGX sums: Schlick F = F0(1-Fc)+Fc, so the texel's F0 applies after the loop.
static float3 materialPointSpecular=0,materialPointFresnel=0;
float materialLuminance(float3 c){return dot(c,float3(.2126,.7152,.0722));}
// GGX distribution times the height-correlated Smith visibility, times nl.
float materialGgx(float nh,float nv,float nl,float a2){
 float d=nh*nh*(a2-1)+1;
 float gv=nl*sqrt(nv*nv*(1-a2)+a2),gl=nv*sqrt(nl*nl*(1-a2)+a2);
 return a2/(3.14159265*d*d)*.5/max(gv+gl,1e-5)*nl;
}
// The split-sum environment BRDF (scale, bias on F0): analytic fit.
float2 materialEnvBrdf(float roughness,float nv){
 const float4 c0=float4(-1,-.0275,-.572,.022),c1=float4(1,.0425,1.04,-.04);
 float4 r=roughness*c0+c1;float a004=min(r.x*r.x,exp2(-9.28*nv))*r.x+r.y;
 return float2(-1.04,1.04)*a004+r.zw;
}
// Directional albedo of the Charlie sheen lobe under uniform light (the
// cloth term of an environment), the published exponential fit (Estevez
// and Kulla 2017 sheen, as fitted in three.js's IBLSheenBRDF, MIT).
float materialSheenAlbedo(float nv,float roughness){
 float r2=roughness*roughness;bool smooth=roughness<.25;
 float a=smooth?-339.2*r2+161.4*roughness-25.9:-8.48*r2+14.3*roughness-9.95;
 float b=smooth?44*r2-23.7*roughness+3.26:1.97*r2-3.27*roughness+.72;
 return saturate(exp(a*nv+b)+(smooth?0:.1*(roughness-.25)));
}
// Terrain surface (pseudo-PBR S3, terrain 2.0 ground): the material stage
// sets (blended baked height, roughness) for pixels it draws completely
// (y -1: none); the snow build-up its share for the roughness.
static float2 terrainSurface=float2(.5,-1);
// #243 (terrain_relief_derivfree): the pixel's terrainSurface came from the
// material cache (terrain2CacheTap), whose height layer the relief can read
// beside the pixel instead of differencing the height across the quad.
static bool terrainSurfaceCached=false;
static float terrainSnowShare=0;
// The build-up's base normal (objects: the face's geometric normal), slope
// share and patch threshold, for the covered share's relief (S2: its drift).
static float3 terrainSnowFace=float3(0,0,1);static float terrainSnowUp=0,terrainSnowThreshold=1;
// The relief normal from a surface height's screen derivatives dh (metres),
// the position derivatives and the base normal (surface gradient through
// the screen-space cotangent frame: the terrain carries no tangents).
float3 terrainReliefNormal(float3 n0,float3 dpdx,float3 dpdy,float2 dh){
 float3 r1=cross(dpdy,n0),r2=cross(n0,dpdx);float det=dot(dpdx,r1);
 float3 n=abs(det)*n0-(det<0?-1:1)*(dh.x*r1+dh.y*r2);
 return dot(n,n)>1e-20?normalize(n):n0;
}
float3 nativePointLighting(NativeOutput i,out float3 glow,bool glowOnly,bool ground,float3 groundNormal,out float3 groundLocal,out float3 groundGlow){
 float3 local=0;glow=0;groundLocal=0;groundGlow=0;
 float3 normal=normalize(i.worldNormal);
#if !defined(NATIVE_OFF_MATERIALS)&&!defined(NATIVE_LOW)
 // A figure's texture relief normal where it has one (materialNormal): lamp-
 // lit gear (lava, camp fires) read flat while only its sun took the relief.
 // The point shadow's bias takes it too (a small tilt; one normal held).
 if(materialReliefLamps)normal=materialNormal;
#endif
 uint2 lightRange=nativePointLightRange(i.worldPosition);
 [loop]for(uint lightSlot=lightRange.x;lightSlot<lightRange.y;++lightSlot){NATIVE_LOOP_GUARD(8u,lightSlot-lightRange.x,lightRange.y-lightRange.x)
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
#if 1
  // Lamp and fire highlights on material pixels (characters Medium, world objects High).
  [branch]if(materialLamps&&facing>0){
   float3 h=normalize(direction+materialView);
   float lobe=materialGgx(saturate(dot(normal,h)),max(saturate(dot(normal,materialView)),1e-3),saturate(incidence),materialAlpha2);
   float fc=pow(1-saturate(dot(materialView,h)),5);
   float3 radiance=light.color.rgb*(falloff*visibility*intensity*lobe);
   materialPointSpecular+=radiance*(1-fc);materialPointFresnel+=radiance*fc;
  }
#endif
  local+=light.color.rgb*i.pointMaterial.rgb*(falloff*facing*visibility*intensity);
  groundLocal+=light.color.rgb*(falloff*groundFacing*visibility*intensity);
 }
 // The bounce light: lamps, emissive surfaces, cave exits and the sun off
 // the ground (cave_irradiance.h).
 // F10 bounce_debug (caveVolumeTail): the bounce alone, eight times
 // brighter, instead of the lamps' direct light.
 [branch]if(!glowOnly&&caveVolumeStrength>0&&(((uint)caveVolumeTail)&1u)!=0){
  local=nativeCaveBounce(i.worldPosition,normal)*8;
  if(ground)groundLocal=nativeCaveBounce(i.worldPosition,groundNormal)*8;
 }
 else [branch]if(!glowOnly&&caveVolumeStrength>0){
  local+=nativeCaveBounce(i.worldPosition,normal)*i.pointMaterial.rgb;
  if(ground)groundLocal+=nativeCaveBounce(i.worldPosition,groundNormal);
 }
 return local;
}
float3 nativePointLighting(NativeOutput i,out float3 glow,bool glowOnly){float3 a,b;return nativePointLighting(i,glow,glowOnly,false,0,a,b);}
// The contact blend's ground receiver alone (nativePointLighting's ground
// terms, the same lamps, order, shared lamp-shadow lookup and bounce light;
// pointLit: the pixel's own lamps ran, which gate the bounce): its own loop
// where the blend runs, so nothing of it is held through the pixel's lighting.
void nativeGroundPointLighting(NativeOutput i,bool pointLit,float3 groundNormal,out float3 groundLocal,out float3 groundGlow){
 groundLocal=0;groundGlow=0;
 float3 normal=normalize(i.worldNormal);
 uint2 lightRange=nativePointLightRange(i.worldPosition);
 [loop]for(uint lightSlot=lightRange.x;lightSlot<lightRange.y;++lightSlot){NATIVE_LOOP_GUARD(8u,lightSlot-lightRange.x,lightRange.y-lightRange.x)
  NativePointLight light=nativePointLights[nativePointLightGrid[lightSlot]];
  float3 delta=light.centerInverseRadiusSquared.xyz-i.worldPosition;
  float distanceSquared=dot(delta,delta);
  float reach=1-distanceSquared*light.centerInverseRadiusSquared.w;
  if(reach<=0)continue;
  float falloff=reach*reach;
  float3 direction=delta*rsqrt(max(distanceSquared,1e-12));
  float incidence=dot(normal,direction);
  float groundIncidence=dot(groundNormal,direction);
  if(light.color.w<0){groundGlow+=light.color.rgb*(falloff*saturate(groundIncidence*.6+.4));continue;}
  float groundFacing=saturate(1+min(groundIncidence,0));
  if(groundFacing<=0)continue;
  float visibility=1;
#if !defined(NATIVE_LOW)&&!defined(NATIVE_OFF_POINT_SHADOWS)
  if(light.shadowSlots.x>0&&!liquidSurfaceEnabled(i.terrainVertexFlags)&&
     max(i.emissive.r,max(i.emissive.g,i.emissive.b))<.25){
   visibility=nativePointVisibility(light,i.shadowPosition,normal,incidence,shadowBias[5].z);
  }
#endif
  float intensity=light.color.w>0?.65:1;
  groundLocal+=light.color.rgb*(falloff*groundFacing*visibility*intensity);
 }
 [branch]if(pointLit&&caveVolumeStrength>0&&(((uint)caveVolumeTail)&1u)!=0)groundLocal=nativeCaveBounce(i.worldPosition,groundNormal)*8;
 else [branch]if(pointLit&&caveVolumeStrength>0)groundLocal+=nativeCaveBounce(i.worldPosition,groundNormal);
}
float3 nativePointLighting(NativeOutput i,out float3 glow){return nativePointLighting(i,glow,false);}
#if !defined(NATIVE_OFF_MATERIALS)&&!defined(NATIVE_LOW)
// Gems (classes.json gem): the lamps' light through the stone, strongest from
// behind, over the same lamps and falloff as nativePointLighting. Its own
// loop, run by gem pixels only: kept in the shared lamp loop, the
// accumulator and the gem flag stayed live through it for every material
// pixel (two more registers on all figures).
float3 nativeGemLampThrough(NativeOutput i){
 float3 through=0;
 float3 normal=normalize(i.worldNormal);
 uint2 lightRange=nativePointLightRange(i.worldPosition);
 [loop]for(uint lightSlot=lightRange.x;lightSlot<lightRange.y;++lightSlot){NATIVE_LOOP_GUARD(8u,lightSlot-lightRange.x,lightRange.y-lightRange.x)
  NativePointLight light=nativePointLights[nativePointLightGrid[lightSlot]];
  float3 delta=light.centerInverseRadiusSquared.xyz-i.worldPosition;
  float distanceSquared=dot(delta,delta);
  float reach=1-distanceSquared*light.centerInverseRadiusSquared.w;
  if(reach<=0||light.color.w<0)continue;
  float incidence=dot(normal,delta*rsqrt(max(distanceSquared,1e-12)));
  through+=light.color.rgb*(reach*reach*saturate((.4-incidence)/1.4));
 }
 return through;
}
#endif
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
#ifdef EID3D12
Texture2D<float> contactGround:register(t114); // kContactGroundSlotD3D12 (contact_blend.h)
Texture2D<float4> contactColor:register(t115);
#else
Texture2D<float> contactGround:register(t13);
Texture2D<float4> contactColor:register(t14);
#endif
// The terrain's own art: per 2 m tile its atlas corners (00,10,01 as unorm16
// pairs; the mapping is affine) and material slice (w bits 0-7; 255 none; the
// upper bits carry the authored-family policy, terrain_tile_blend.h), t15,
// and the drawn terrain atlases copied per material into one array, t16.
// nativeContactMapMaterials (per batch: the active map's or a neighbour's
// contact maps): bits of the slices already copied.
Texture2D<uint4> contactTiles:register(t15);
// With blend rows (nativeTerrainCliff.z) the grid holds h art rows, h blend
// rows, contactMarginRows seam margin rows (kTerrainTileSeamMarginRows,
// terrain_tile_seam_margin_count) and the fold row: h of a grid of gh rows.
static const uint contactMarginRows=6;
uint contactBlendTileRows(uint gh){return gh>contactMarginRows+1u?(gh-1u-contactMarginRows)/2u:0u;}
Texture2DArray<float4> contactAtlas:register(t16);
SamplerState contactSampler:register(s8);
bool contactTerrainArtMap(float2 s,float2 sDx,float2 sDy,uint2 materials,out float3 art,out bool preserveAuthored){
 art=0;preserveAuthored=false;uint w,h;contactTiles.GetDimensions(w,h);
 float2 t=s*.5;int2 c=int2(floor(t));
 if(any(c<0)||c.x>=int(w)||c.y>=int(h))return false;
 uint4 v=contactTiles.Load(int3(c,0));
 uint m=v.w&255u; // policy bits never form part of an atlas slice
 if(m>=64u||(((m<32u?materials.x:materials.y)>>(m&31u))&1u)==0)return false;
 float2 uv00=float2(v.x&65535u,v.x>>16)/65535.0;
 float2 du=float2(v.y&65535u,v.y>>16)/65535.0-uv00,dv=float2(v.z&65535u,v.z>>16)/65535.0-uv00;
 float2 f=t-c;
 // Analytic gradients: the tile's affine map times the world footprint, so
 // a quad straddling two tiles keeps its mip.
 art=contactAtlas.SampleGrad(contactSampler,float3(uv00+f.x*du+f.y*dv,m),
  (sDx.x*du+sDx.y*dv)*.5,(sDy.x*du+sDy.y*dv)*.5).rgb;
 preserveAuthored=(v.w&0x1000u)!=0; // the original slot is homogeneous in a protected family
 return true;
}
// The existing ground-cover caller needs only the art, from the same read.
bool contactTerrainArtMap(float2 s,float2 sDx,float2 sDy,uint2 materials,out float3 art){
 bool preserveAuthored;return contactTerrainArtMap(s,sDx,sDy,materials,art,preserveAuthored);
}
bool contactTerrainArt(float2 s,float2 sDx,float2 sDy,out float3 art,out bool preserveAuthored){
 return contactTerrainArtMap(s,sDx,sDy,nativeContactMapMaterials,art,preserveAuthored);
}
)" R"(
@TERRAIN_CLIFF@
// Terrain cliffs (terrain_cliff.h): the map's own tile art on unwalkable
// faces, re-read through world-space side projections. A tile's art is one
// atlas cell: its inner rectangle (lo, size; the padding margin around it is
// size/6) and its source, the drawn texture (own) or the contact atlas slice.
// fold > 0: a folded blend cell (terrain_tile_blend.hlsl terrainTileBlendCellFrom):
// q runs a 1..0..1 triangle wave over the tile, from lo along dir by up to
// fold (one corner quadrant mirrored); the gradients keep the tile's scale.
// shift: a colour offset added to every contact atlas tap (a folded family
// drawing a donor's plain art at its own mean colour; 0 otherwise).
struct TerrainCliffCell{float2 lo;float2 size;float slice;bool own;float width;float lowLod;float marginLod;float fold;float2 dir;float3 shift;};
float2 terrainCliffCellUv(TerrainCliffCell k,float2 q){
 return k.fold>0?k.lo+k.dir*abs(1-2*frac(q))*k.fold:k.lo+q*k.size;
}
// Coarse level (terrainLowpassTexels a tile) and margin level of a cell in a
// texture of `width` texels.
void terrainCliffCellLevels(inout TerrainCliffCell k,float width){
 k.width=width;float texels=k.size.x*width;k.lowLod=log2(max(texels/terrainLowpassTexels,1));k.marginLod=log2(max(texels/6,1));
}
@TERRAIN_TILE_BLEND@
)" R"(
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
 float3 art;bool preserveAuthored;
 float3 terrain=ground.rgb;float detail=0;
 [branch]if(contactTerrainArt(s,sDx,sDy,art,preserveAuthored)){
  // Large-scale brightness of the terrain's anti-tiling stage, when enabled.
  if(nativeTerrainMaterial.y>0&&!preserveAuthored)art*=terrainMacro(s+nativeContactLayout.xy); // the terrain's own frame
  float3 luma=float3(.2126,.7152,.0722);
  detail=dot(art,luma)-dot(ground.rgb,luma);
  terrain=art;
 }
 float2 k=contactBlendWeight(rise,band,n.z,noise,contactGrass(ground.rgb),detail);
 blend=float2(k.x*strength,lerp(1,k.y,strength));
 return saturate(terrain);
}
)" R"(
#ifdef NATIVE_TERRAIN_SLIM
RWTexture2D<uint> terrainMissMask:register(u4); // the slim pass's winner keys (R32_UINT; terrainSlimKey)
RWByteAddressBuffer terrainMissTriangleFlags:register(u5); // and their triangles (terrainMissTriangles)
static bool terrainSlimMiss=false;
// The list mark (terrain_slim_stencil 2, nativeLiquidLayer bit 2048; D3D12
// only): a miss lists its 2x2 quad once per flush, so the stencil mark
// before the second passes draws those quads instead of the whole screen.
// terrainMissQuads: each quad's last stamp (the flush sequence + 1; cleared
// with the miss mask, when the sequence wraps). terrainMissQuadList: words
// 0-3 the mark's draw arguments (instance count = quads listed), then one
// word per quad (x | y << 16, in quads). Sized to the screen's quads, so a
// flush can never list more than it holds. D3D12 DXIL only (EID3D12): the
// D3D11 programs have no mark and stay without this code.
#ifdef EID3D12
RWTexture2D<uint> terrainMissQuads:register(u3);
RWByteAddressBuffer terrainMissQuadList:register(u7);
void terrainSlimListQuad(uint2 p){
 [branch]if((nativeLiquidLayer&2048u)==0)return;
 uint2 quad=p>>1;uint stamp=((nativeLiquidLayer>>24)&127u)+1u,last;
 InterlockedExchange(terrainMissQuads[quad],stamp,last);
 [branch]if(last!=stamp){uint at;terrainMissQuadList.InterlockedAdd(4u,1u,at);terrainMissQuadList.Store(16u+at*4u,quad.x|(quad.y<<16));}
}
#else
void terrainSlimListQuad(uint2 p){}
#endif
#endif
#ifdef NATIVE_TERRAIN_PS
// Terrain-only material stages (blend, terrain material, cliffs) live in the terrain pixel variant: in the shared shader their
// registers slowed every figure and object pixel.
// Every blend and material decision (field lookup, face warps and side-plane
// reads, rock noise, hex and anti-tiling cells, noise and score positions)
// reads the untessellated surface: i.shadowPosition, the base patch position
// the domain stage interpolates linearly, and its screen derivatives
// (restDx, restDy). The tessellated position is lifted by an amount that
// follows the distance-based tessellation factor, so reading it moved the
// material boundaries with the camera. Untessellated, the two positions are
// the same. Lighting and normals keep the drawn surface.
float4 nativeTerrainMaterialStages(NativeOutput i,float4 t,float3 restDx,float3 restDy,float2 uvDx,float2 uvDy){
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
  // Terrain 2.0 (terrain2.hlsl): walkable ground of the active map and the
  // neighbour maps in the map table (the batch's entry + 1 in
  // nativeContactLayout.w; 0: not in the table, its tile art stays), faces
  // and rock included (projection planes, the map's own rock cells). Terrain
  // 1.0 (tile blend, cliffs, edge mix) is retired. With blending off the
  // owned tile sample above stays unchanged; tiles 2.0 does not draw
  // (long, animated, liquid, maps beyond the table) keep their tile art.
  // Neighbour ground under nearly full seam fog fades to its tile art
  // between 3% and 1% fog factor: the pixel is at least 97% fog colour
  // there, and the fade spares the farthest ground the whole 2.0 cost.
  [branch]if(nativeTerrain2.x>0&&nativeContactLayout.w>0&&nativeTextured!=0&&(i.terrainVertexFlags&16u)!=0){
   float3 t2Color=0;float t2Share=i.neighbour!=0?smoothstep(.01,.03,i.s.a):1;
   [branch]if(t2Share>.001){
    float3 n2=dot(i.terrainSlope,i.terrainSlope)>1e-8?normalize(i.terrainSlope):float3(0,0,1); // (a zero slope would be NaN)
    // Projection planes as Terrain cliffs draws them: side share from the
    // smooth normal between the walk limit and 55 degrees, top-down raised
    // where the facets are flat, the facet split eased in from triangle edges.
#ifdef NATIVE_TERRAIN_FLAT
    float3 planes=float3(0,0,1);
#else
    float3 planes=nativeTerrainCliff.x>0?terrainCliffFacetPlanes(terrainCliffProjectionPlanes(n2,i.terrainFlat),restDx,restDy,i.liquidFall.y):float3(0,0,1);
#endif
    // terrain_quality Medium and Low (T2_LEAN): biplanar, the weakest of three
    // planes subtracted from all (continuous: it is 0 where two planes meet).
    [branch]if(T2_LEAN&&all(planes>.002)){float lo=min(planes.x,min(planes.y,planes.z));planes=max(planes-lo,0);planes/=planes.x+planes.y+planes.z;}
    // The material cache (terrain2_cache.h) where it holds exactly this
    // colour: walkable ground (top plane only) where the liquid-bed fade
    // leaves terrain2Color as baked: no liquid (depth 0), deep liquid (depth
    // past t2BedBand: no fade), and dry ground under a liquid's footprint
    // (negative depth) where no texel the read weighs carries the bed flag
    // (x64; terrain2CacheTap). Ground within t2BedBand under liquid fades per pixel.
    // S3 (material level on): the reads also take the materials' heights;
    // cached ground reads its height layer and stored roughness.
    bool t2Cached=false;float2 t2Surf=float2(.5,-1);
#if !defined(NATIVE_OFF_MATERIALS)&&!defined(NATIVE_LOW)
    t2Surface=nativeMaterialTerrainLevel>0;
#endif
    bool t2DryBed=i.bedLiquid.x<0;
    bool t2Eligible=terrain2CacheParams[0].x>0&&planes.z>=.999&&(i.bedLiquid.x<=0||i.bedLiquid.x>=t2BedBand);
    [branch]if(t2Eligible)
     t2Cached=terrain2CacheTap(i.shadowPosition.xy,restDx.xy,restDy.xy,t2Surface,t2DryBed,t2Color,t2Surf);
    terrainSurfaceCached=t2Cached&&t2Share>=.999;
#ifndef NATIVE_TERRAIN_FLAT
    // The face cache (terrain2_cache.h kTerrainFaceCacheShader): faces of a
    // map with an atlas (its table entry's header) read its baked pages
    // (the active map's baked with the live liquid-bed depth).
    uint faceMap=uint(nativeContactLayout.w)-1u;
    [branch]if(!t2Cached&&planes.z<.999&&faceCacheData[0].x>0&&faceCacheData[faceCacheMap(faceMap)].y>0)
     t2Cached=faceCacheTap(faceMap,tileP,restDx,restDy,t2Color,t2Surf)==0u;
#endif
#ifdef NATIVE_TERRAIN_SLIM
    // The slim pass (terrain_slim): a pixel the caches miss is marked for the
    // full program's second pass (terrainMissMask) and its colour here is
    // overwritten there.
    [branch]if(!t2Cached)terrainSlimMiss=true;
#else
    [branch]if(!t2Cached)
     t2Color=terrain2Color(uint(nativeContactLayout.w)-1u,i.shadowPosition,tileP,layoutP,restDx,restDy,n2,planes,i.bedLiquid.x,i.liquidFall.x,t2Surf);
#endif
    if(t2Share>=.999)terrainSurface=t2Surf;
    t.rgb=lerp(t.rgb,t2Color,t2Share);
   }
  }
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
#ifdef NATIVE_FIRE_BURN
 nointerpolation float4 burnRoot:TEXCOORD31;
#endif
 // Crown impostor frames (nativeVertex).
 float4 liquidTintDepth:TEXCOORD8;nointerpolation float4 liquidFlow:TEXCOORD9;float4 liquidMaterial:TEXCOORD10;nointerpolation float4 liquidUvBounds:TEXCOORD14;};
NativeDepthOutput nativeDepthOf(NativeOutput o){
 NativeDepthOutput r;r.p=o.p;r.d=o.d;r.s=o.s;r.uv=o.uv;r.textureReference=o.textureReference;r.worldPosition=o.worldPosition;
 r.shadowMode=o.shadowMode;r.terrainVertexFlags=o.terrainVertexFlags;r.terrainSlope=o.terrainSlope;r.swaying=o.swaying;
 r.shadowPosition=o.shadowPosition;r.neighbour=o.neighbour;
#ifdef NATIVE_FIRE_BURN
 r.burnRoot=o.burnRoot;
#endif
 r.liquidTintDepth=o.liquidTintDepth;r.liquidFlow=o.liquidFlow;r.liquidMaterial=o.liquidMaterial;r.liquidUvBounds=o.liquidUvBounds;return r;
}
#ifdef EID3D12
// One t table serves every stage on D3D12 and the vertex stage reads t1, t2,
// t4 and t5 (geometry, dynamic, visible triangles, draws): the DXIL reads
// free slots (kPrepassSlotsD3D12, renderer_native_d3d12.cpp).
Texture2D<float4> nativeReceiverRest:register(t117);
Texture2D<float2> nativeReceiverNormal:register(t118);
Texture2D<float> nativeSceneDepth:register(t119);
Texture2D<float2> nativeShadowMask:register(t120);
#else
Texture2D<float4> nativeReceiverRest:register(t1);
Texture2D<float2> nativeReceiverNormal:register(t2);
Texture2D<float> nativeSceneDepth:register(t4);
Texture2D<float2> nativeShadowMask:register(t5);
#endif
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
// The slim terrain pass's misses (terrainMissMask) for the full programs'
// second pass (nativePS, nativeLiquidLayer bit 64).
Texture2D<uint> terrainMissInput:register(t125);
ByteAddressBuffer terrainMissOrigin:register(t126); // a miss list entry's slot in its region (nativeTerrainMissCompactCS)
// The slim pass's winner key: this flush's sequence (nativeLiquidLayer bits
// 24-30; the mask is cleared when it wraps), the triangle's slot (24 bits:
// the layout's triangle limit is 16M) and its miss. Slots rise in draw order
// (a batch's flat region, then its steep one, batch after batch), so the
// largest key at a pixel is the single pass's last writer at that pixel,
// whatever order the GPU runs the fragments in.
uint terrainSlimKey(uint slot,bool miss){return (((nativeLiquidLayer>>24)&127u)<<25)|((slot&0xffffffu)<<1)|(miss?1u:0u);}
// The second passes' stencil mark (terrain_slim_stencil): full screen, before
// a slim run's second passes, every pixel of a 2x2 quad (the rasteriser's
// own, at even coordinates) holding a miss of this flush (sequence in
// nativeLiquidLayer bits 24-30) marks the stencil. A second-pass pixel
// survives only on its own triangle's miss key, so the second passes test
// the mark and the other quads never start the full program. Whole quads:
// a kept quad's pixels that are not the triangle's run on as real pixels and
// leave at the program's end, after the screen differences (#243).
void nativeSlimMissMarkPS(float4 position:SV_Position){
 int2 quad=int2(position.xy)&~1;
 uint sequence=(nativeLiquidLayer>>24)&127u,missed=0;
 [unroll]for(uint k=0;k<4;++k){
  uint key=terrainMissInput.Load(int3(quad+int2(k&1u,k>>1),0));
  missed|=(key&1u)!=0&&(key>>25)==sequence?1u:0u;
 }
 if(missed==0)discard;
}
// The list mark's vertex stage (terrain_slim_stencil 2): instance = one
// listed quad (terrainMissQuadList from word 4, at t123), two triangles over
// its 2x2 pixels; drawn without a pixel program, the stencil only. D3D12 only.
#ifdef EID3D12
ByteAddressBuffer terrainMissQuadInput:register(t123);
float4 nativeSlimQuadMarkVS(uint vertex:SV_VertexID,uint instance:SV_InstanceID):SV_Position{
 uint packed=terrainMissQuadInput.Load(16u+instance*4u),width,height;
 terrainMissInput.GetDimensions(width,height);
 float2 corner=float2((0x32u>>vertex)&1u,(0x2Cu>>vertex)&1u);
 float2 pixel=(float2(packed&0xffffu,packed>>16)+corner)*2;
 return float4(pixel.x/width*2-1,1-pixel.y/height*2,0,1);
}
#endif

// Static LOD crossfade (nativeStaticLodSelect, code in swaying bits 8-17):
// a level in its band keeps the pixels the other level leaves, by a fixed
// per-pixel threshold (interleaved gradient noise), so the two levels cover
// each pixel once and the prepass and the material pass agree.
// Crown impostor texel (static_lod.h): the four frames' albedo (premultiplied
// by coverage) and object-space normal, blended by the record's weights; a
// frame position outside its image is empty. Weighted coverage
// is resolved into a binary cutout shared by both passes. The sampler is the batch's
// own (bilinear, mips); positions stay inside their frame's image.
Texture2DArray nativeImpostorAlbedo:register(t44);
Texture2DArray nativeImpostorNormal:register(t45);
// Mip of the frame images from the first frame's footprint (taken outside
// any branch: the impostor flag differs between neighbouring triangles).
float nativeImpostorLod(float2 local){
 float2 dx=ddx(local),dy=ddy(local);
 // Upscaled: the output-resolution footprint (the negative mip bias of the
 // upscaler guides, as the world textures take it).
 return clamp(log2(max(max(length(dx),length(dy))*kImpostorFrameTexels/max(nativeImpostorParams.w,1),1e-6)),0,kImpostorMips-1);
}
float4 nativeImpostorTexel(float4 frames01,float4 frames23,float4 weights,float4 flow,float lod,bool withNormal,out float3 normal){
 // The slice and frame base arrive as whole numbers in floats and are rounded
 // as floats. With (float)(uint)(x+.5) instead, D3D12 sampled every odd slice
 // as the NEXT slice (another object's impostor: the floating "stones" and
 // "domes", 2026-11-06), measured in game by the self-checks below; D3D11 was
 // correct. DXC's DXIL keeps the exact fptoui/uitofp pair, so the fold of
 // that pair back to x+.5 (layer slice+.5, rounded to even) happens after
 // DXIL, in the driver's compile (inference; not seen in SASS). Avoid
 // float->int->float round trips on non-integral values in shaders.
 const float layer=round(flow.x);const float2 base=round(flow.yz);
 uint slice=(uint)layer;uint2 b=uint2(base);
 float2 uvs[4]={frames01.xy,frames01.zw,frames23.xy,frames23.zw};
 float4 albedo=0;float3 n=0;float coverage=0;
 [unroll]for(uint k=0;k<4;++k){
  float2 frame=base+float2(k&1u,k>>1);
  float2 local=uvs[k];
  float3 at=float3((frame+saturate(local))/kImpostorFrames,layer);
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
 // Diagnostic self-checks (impostor_debug and its frame view): pure blue where
 // the bound atlas is not the impostor atlas (size, mips, slice range), where
 // the slice's bake stamp (texel (0,0): slice / 255, 90 / 255) is not this
 // slice's, or where the filtered level-0 sample is covered while the texels
 // around that spot are empty (the 2026-11-06 floating impostors).
 [branch]if(flow.w>.5&&flow.w<2.5){
  normal=float3(0,0,1);
  uint aw,ah,ae,al;nativeImpostorAlbedo.GetDimensions(0,aw,ah,ae,al);
  if(slice>=ae||aw!=kImpostorFrames*(uint)kImpostorFrameTexels||ah!=aw||al!=kImpostorMips)return float4(0,0,1,1);
  const float4 stamp=nativeImpostorAlbedo.Load(int4(0,0,slice,0));
  if(abs(stamp.r*255-float(slice&255u))>.5||abs(stamp.g*255-90)>.5)return float4(0,0,1,1);
  [unroll]for(uint q=0;q<4;++q){
   const float2 lq=uvs[q];
   if(!(all(lq>=0)&&all(lq<=1))||weights[q]<=0)continue;
   const float3 aq=float3((base+float2(q&1u,q>>1)+saturate(lq))/kImpostorFrames,layer);
   const int2 t0=clamp(int2(floor(aq.xy*aw-.5)),int2(0,0),int2(aw-2,aw-2));
   float loaded=0;
   [unroll]for(uint c=0;c<4;++c)loaded=max(loaded,nativeImpostorAlbedo.Load(int4(t0+int2(c&1u,c>>1),slice,0)).a);
   if(nativeImpostorAlbedo.SampleLevel(nativeSampler,aq,0).a>=.5&&loaded<.1)return float4(0,0,1,1);
   break;
  }
 }
 // Frame view (impostor_debug_frames, w 2): the chosen view base, red its
 // column, green its row (0-2 of the 4x4 grid).
 if(flow.w>1.5&&flow.w<2.5){normal=float3(0,0,1);return float4(base/2,0,coverage>=.5?1:0);}
 // Diagnostic view (F10 impostor_debug, w 1): the impostor's own silhouette in
 // a solid colour per atlas slice (the debug labels use the same colour).
 if(flow.w>.5&&flow.w<1.5){normal=float3(0,0,1);uint h=slice*2654435761u;
  float3 tint=.25+.75*float3((h>>8)&255u,(h>>16)&255u,(h>>24)&255u)/255.0;return float4(tint,coverage>=.5?1:0);}
 return float4(albedo.a>1e-4?albedo.rgb/albedo.a:0,coverage>=.5?1:0);
}
#if defined(EID3D12)&&defined(NATIVE_RT_SUN)
// The crown's visible surface at an impostor pixel, in bounding sphere radii
// from the quad's plane toward the viewer (-1 to 1): the four frames' baked
// depth (the normal image's alpha, premultiplied by coverage as the albedo's
// alpha is; renderer_impostors.cpp impostorBakePS).
float nativeImpostorSurface(float4 frames01,float4 frames23,float4 weights,float4 flow,float lod){
 const float layer=round(flow.x);const float2 base=round(flow.yz);
 float2 uvs[4]={frames01.xy,frames01.zw,frames23.xy,frames23.zw};
 float depth=0,cover=0;
 [unroll]for(uint k=0;k<4;++k){
  float2 local=uvs[k];
  [branch]if(all(local>=0)&&all(local<=1)&&weights[k]>0){
   float3 at=float3((base+float2(k&1u,k>>1)+saturate(local))/kImpostorFrames,layer);
   depth+=nativeImpostorNormal.SampleLevel(nativeSampler,at,lod).a*weights[k];
   cover+=nativeImpostorAlbedo.SampleLevel(nativeSampler,at,lod).a*weights[k];
  }
 }
 return cover>1e-4?clamp(1-2*depth/cover,-1,1):0;
}
#endif
bool nativeLodDiscard(uint swaying,float4 position){
 uint code=swaying>>8;
 if((code&512u)==0)return false;
 float noise=frac(52.9829189*frac(dot(floor(position.xy),float2(.06711056,.00583715)))+nativeDitherParams.x); // + per-frame offset under upscalers
 float coverage=(code&255u)/255.0;
 return (code&256u)!=0?noise>=coverage:noise<coverage;
}
)" R"(
NativePrepassResult nativePrepassPS(NativeDepthOutput i){
 NATIVE_REPORT_PIXEL(4u,i.textureReference.x,i.p)
#ifdef NATIVE_FIRE_BURN
 // Opaque leaf geometry must leave depth as well as colour when consumed.
 if(!NATIVE_RT_UNIT(i)&&nativeBurnMaterial(i.textureReference.x,i.uv)==2u&&
    nativeBurnSurfacePhase(i.shadowPosition,i.burnRoot).y>=.95)discard;
#endif
 // The rest triangle's plane for the trail trough receiver below: taken
 // here, at the top level (FXC moves a derivative into the branch that
 // uses it, #243). NATIVE_PREPASS_OBJECT: the program of material instance
 // batches (no terrain records: no lifted receiver, no trail trough).
#ifndef NATIVE_PREPASS_OBJECT
 float3 restDx=ddx(i.shadowPosition),restDy=ddy(i.shadowPosition);
#endif
 if(nativeLodDiscard(i.swaying,i.p))discard;
#ifdef NATIVE_FUR_DEBUG
 if(((i.swaying>>24)&7u)==4u)discard;  // F10 fur_debug: a hidden layer
#endif
 float impostorLod=nativeImpostorLod(i.liquidTintDepth.xy);
#if defined(EID3D12)&&defined(NATIVE_RT_SUN)
 // The first frame position's screen steps (the impostor receiver below), at the top level like restDx.
 float2 frameDx=ddx(i.liquidTintDepth.xy),frameDy=ddy(i.liquidTintDepth.xy);
#endif
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
#ifdef EID3D12
   t=terrainTapGrad(i.textureReference,i.uv,textureDx,textureDy);
#else
   [branch]if(i.textureReference.y!=0)t=nativeTextureArray.SampleGrad(nativeSampler,float3(i.uv,i.textureReference.x),textureDx,textureDy);
   else t=nativeTexture.SampleGrad(nativeSampler,i.uv,textureDx,textureDy);
#endif
  }
#else
  if(nativeTextured!=0)t=NATIVE_SAMPLE(i.textureReference,i.uv);
#endif
  }
  NATIVE_BURN_ALPHA(i,t);
  if(!nativeCompare(nativeCombineAlpha(i.d,i.s,t)))discard;
 }
 float3 fogPoint=i.worldPosition;
 [branch]if(i.shadowMode==0&&(fogStock.w>0||fogEye.w>.5))fogPoint=fogUnproject(i.p.xy*fogScreen.xy,i.p.z);
 if(nativeFogEnable==0&&nativeDrawDissolvedAt(i.p,fogPoint))discard;
 bool receiver=i.shadowMode==1||(i.shadowMode==2&&nativeRasterTail.y!=0);
#ifdef NATIVE_PREPASS_OBJECT
 uint flags=(receiver?1u:0u)|(nativeRasterTail.x==0&&(i.swaying&255u)==0?2u:0u);
 NativePrepassResult r;
 r.rest=float4(i.shadowPosition-fogUnproject(i.p.xy*fogScreen.xy,nativeDepth24(i.p.z)),flags);
 r.normal=float2(.5,.5);
 return r;
#else
 bool lifted=(i.terrainVertexFlags&16)!=0&&dot(i.terrainSlope,i.terrainSlope)>1e-8;
 // 8: a unit (swaying bit 29), for the traced sun mask's self-hit distance.
 uint flags=(receiver?1u:0u)|(nativeRasterTail.x==0&&(i.swaying&255u)==0?2u:0u)|(lifted?4u:0u);
#if defined(EID3D12)&&defined(NATIVE_RT_SUN)
 flags|=(i.swaying&(1u<<29))!=0?8u:0u;
#endif
 // Snow trail troughs and berms (nativeTessGroundPosition) are drawn off the
 // untessellated terrain the sun maps hold. The receiver follows the drawn
 // point along its ray to the sun up to that terrain, so a shadow lands on
 // the trough floor where the drawn surface meets it, and the terrain in the
 // map never shadows its own trough. Only where the drawn point left the
 // rest point (the snow stages ran; untessellated trails stay at rest).
 // That terrain is the rest triangle's own plane (normal n, from the rest
 // position's screen derivatives: exact inside a triangle). The ray from the
 // drawn point (rest + h up) meets it after t = -h n.z / dot(n, sun), and
 // the receiver's height follows the plane there. Keeping the rest height
 // (#217) was right on level ground only: where the ground rises toward the
 // sun the receiver lay under the map's terrain by |h| cot(sun elevation)
 // times that rise, past the receiver bias, and the mask shadowed the whole
 // trough. The step keeps its old limit (4 |h| on level ground) and the
 // receiver stays on the plane under it. A triangle steeper than about 78
 // degrees or seen edge-on keeps the rest point.
 float3 rest=i.shadowPosition;
 [branch]if((i.terrainVertexFlags&16)!=0&&i.neighbour==0&&nativeSnowTrailWindow.w>0&&abs(i.worldPosition.z-rest.z)>1e-5&&nativeWorldSunDirection.z>0){
  float h=snowTrailHeight(snowTrailRaw(nativeSnowTrail,nativeSnowTrailWindow,nativeSnowTrailTiles,i.worldPosition.xy));
  float3 sun=normalize(nativeWorldSunDirection.xyz);
  float3 n=cross(restDx,restDy);float nn=dot(n,n);
  if(nn>1e-20){
   n*=rsqrt(nn)*(n.z<0?-1:1);
   if(n.z>.2){
    float2 slide=-h*n.z/max(dot(n,sun),.25*n.z)*sun.xy;
    rest.xy+=slide;rest.z-=dot(n.xy,slide)/n.z;
   }
  }
 }
#if defined(EID3D12)&&defined(NATIVE_RT_SUN)
 // A crown impostor under the traced sun (liquidFlow.w below 0: set only on
 // a frame whose mask traces rays). Its quad is a plane through the crown's
 // centre, and the ray tracing scene holds the crown itself: a sun ray from
 // the plane starts inside the crown and its near half shadows it whatever
 // side the sun is on (user capture 011216: the far trees half dark). The
 // receiver is the crown's visible surface instead: the quad's point moved
 // toward the eye by the baked depth of the impostor's frames, as the
 // crown's own triangles would stand there. The bounding sphere's world
 // radius comes from the frame position's rate over the quad (a frame image
 // spans the sphere's diameter over kImpostorMargin).
 [branch]if(((i.swaying>>8)&1024u)!=0&&i.liquidFlow.w<-.5){
  float radius=kImpostorMargin*.5*sqrt((dot(restDx,restDx)+dot(restDy,restDy))/max(dot(frameDx,frameDx)+dot(frameDy,frameDy),1e-12));
  float surface=nativeImpostorSurface(i.liquidTintDepth,i.liquidMaterial,i.liquidUvBounds,i.liquidFlow,impostorLod);
  float3 toEye=fogEye.xyz-rest;
  if(isfinite(radius)&&radius<200)rest+=toEye*rsqrt(max(dot(toEye,toEye),1e-12))*(surface*radius);
 }
#endif
 NativePrepassResult r;
 r.rest=float4(rest-fogUnproject(i.p.xy*fogScreen.xy,nativeDepth24(i.p.z)),flags);
 r.normal=lifted?nativeOctEncode(normalize(i.terrainSlope)):float2(.5,.5);
 return r;
#endif
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
)" R"(
#if defined(EID3D12)&&defined(NATIVE_RT_SUN)
// rt_sun_shadows (D3D12 only; renderer_native_d3d12.cpp update_raytracing_scene_d3d12):
// the mask's sun term from rays through the ray tracing scene instead of the
// sun maps. rtSunRecords: a 32-byte header {float tan of the sun disc's
// radius, history weight, self-hit distance at the eye, and its growth per
// metre of view distance; uint frame, rays a pixel, the foliage card mip
// distance (float bits), records}, then four words a static record {its first
// vertex, its first triangle, its texture's bindless index, its alpha
// reference}; a record is a hit's instance id plus its geometry index. The
// terrain's instance mask is 1, the static records' 2, the units' 4 (opaque).
// rtSunVertices: 20 bytes a vertex (world position, texture coordinate);
// rtSunIndices: three words a triangle, relative to the record's first vertex.
RaytracingAccelerationStructure rtSunScene:register(t94);
ByteAddressBuffer rtSunVertices:register(t95);
ByteAddressBuffer rtSunIndices:register(t99);
ByteAddressBuffer rtSunRecords:register(t113);
// 1 when the sun reaches origin along direction, else 0. Terrain is opaque
// and one-sided: its underside is culled, so a receiver the tessellation or
// the depth's precision put under the scene's untessellated terrain is not
// shadowed by the ground it lies on (a ray toward a sun above the horizon
// enters solid ground only through its top side). Objects are two-sided (an
// instance flag); cut-out cards pass where their texture's alpha fails the
// record's alpha test, read at mip lod. mask: the instances the ray meets
// (0xFF all; 0xFB the static scene; 4 the units).
float nativeRtSunRay(float3 origin,float3 direction,float nearest,float lod,uint mask){
 RayDesc ray;ray.Origin=origin;ray.Direction=direction;ray.TMin=nearest;ray.TMax=100000;
 RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES|RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH|RAY_FLAG_CULL_BACK_FACING_TRIANGLES> query;
 query.TraceRayInline(rtSunScene,RAY_FLAG_NONE,mask,ray);
 while(query.Proceed()){
  if(query.CandidateType()!=CANDIDATE_NON_OPAQUE_TRIANGLE)continue;
  uint4 record=rtSunRecords.Load4(32+(query.CandidateInstanceID()+query.CandidateGeometryIndex())*16);
  uint3 corner=rtSunIndices.Load3((record.y+query.CandidatePrimitiveIndex())*12);
  float2 b=query.CandidateTriangleBarycentrics();
  float2 uv=asfloat(rtSunVertices.Load2((record.x+corner.x)*20+12))*(1-b.x-b.y)+
            asfloat(rtSunVertices.Load2((record.x+corner.y)*20+12))*b.x+
            asfloat(rtSunVertices.Load2((record.x+corner.z)*20+12))*b.y;
  Texture2D card=ResourceDescriptorHeap[NonUniformResourceIndex(nativeBindlessIndex(record.z))];
  if(card.SampleLevel(nativeSampler,uv,lod).a>=asfloat(record.w))query.CommitNonOpaqueTriangleHit();
 }
 return query.CommittedStatus()==COMMITTED_TRIANGLE_HIT?0:1;
}
// The receiver's sun visibility: rays inside the sun disc's cone. A receiver
// with history takes a different point of the disc each frame (a
// low-discrepancy sequence offset per pixel), which the history averages; one
// without (swaying leaves, critters, a frame without history) traces the
// disc's centre, a hard shadow, instead of per-frame noise. lifted: a terrain
// receiver with its slope normal; turned away from the sun it is shadowed
// without a ray (its ray would start inside the ground and leave only
// through a culled underside). unit: a creature or character; the scene
// holds its pose of the frame before, so its own surface is skipped over a
// longer distance (it keeps the shadows of its farther parts). mask: the
// instances the rays meet (nativeRtSunRay).
float nativeRtSunVisibility(float3 rest,int2 c,bool lifted,bool history,bool unit,uint mask){
 float4 setup=asfloat(rtSunRecords.Load4(0));uint4 control=rtSunRecords.Load4(16);
 float3 sun=nativeWorldSunDirection.xyz;
 if(dot(sun,sun)<=1e-12)return 1;
 sun=normalize(sun);
 if(lifted&&dot(nativeOctDecode(nativeReceiverNormal.Load(int3(c,0))),sun)<=0)return 0;
 float distance=length(rest-fogEye.xyz);
 float nearest=setup.z+setup.w*distance+(unit?.25:0);
 // rt_debug_view 4 (frame word bit 16): a sunward receiver whose nearest
 // terrain surface toward the sun is an underside lies under the scene's
 // terrain; -1 is the mask's failure value (magenta in the material pass).
 [branch]if((control.x&0x10000u)!=0){
  RayDesc probe;probe.Origin=rest;probe.Direction=sun;probe.TMin=nearest;probe.TMax=100000;
  RayQuery<RAY_FLAG_FORCE_OPAQUE|RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> under;
  under.TraceRayInline(rtSunScene,RAY_FLAG_NONE,0x01,probe);
  under.Proceed();
  if(under.CommittedStatus()==COMMITTED_TRIANGLE_HIT&&!under.CommittedTriangleFrontFace())return -1;
 }
 float lod=clamp(log2(max(distance,1e-3)/max(asfloat(control.z),1e-3)),0,3);
 uint rays=history?clamp(control.y,1u,8u):1u;
 float3 side=normalize(cross(sun,abs(sun.z)<.9?float3(0,0,1):float3(1,0,0))),up=cross(sun,side);
 float2 offset=float2(frac(52.9829189*frac(dot(float2(c),float2(.06711056,.00583715)))),
                      frac(52.9829189*frac(dot(float2(c)+float2(47.3,113.7),float2(.06711056,.00583715)))));
 float lit=0;
 [loop]for(uint k=0;k<rays;k++){
  float3 direction=sun;
  if(history){
   float index=float((control.x&1023u)*rays+k);
   float2 disc=frac(offset+index*float2(.7548776662,.5698402910));
   float angle=6.2831853*disc.y;
   direction=normalize(sun+setup.x*sqrt(disc.x)*(cos(angle)*side+sin(angle)*up));
  }
  lit+=nativeRtSunRay(rest,direction,nearest,lod,mask);
 }
 return lit/rays;
}
// The units' sun shadow this frame alone (instance mask 4; one ray at the
// disc's centre: a unit's penumbra on the ground under it is a few
// centimetres). It never enters the history, so a walking unit leaves no
// trail of its old shadow. rest: a receiver that is not a unit.
float nativeRtUnitShadow(float3 rest){
 float3 sun=nativeWorldSunDirection.xyz;
 if(dot(sun,sun)<=1e-12)return 1;
 float4 setup=asfloat(rtSunRecords.Load4(0));
 return nativeRtSunRay(rest,normalize(sun),setup.z+setup.w*length(rest-fogEye.xyz),0,4u);
}
// The renderer's own sun casters under rt_sun_shadows (ground cover, critters:
// not in the ray tracing scene): the close sun map's layer that holds only
// them (shadowControl.x bit 8: there is one; bits 9-11 its slice of the map
// array and of shadowLight and shadowBias: a window of the close map's light
// space around the near view), compared on the receiver's plane like the sun
// maps' fixed filter, over 3x3 texels. The world's casters are not in that
// layer, so the traced shadow is not counted twice. It fades to 1 over the
// outer fifth of the window; 1 outside. Its casters sway, roll and walk, so
// it is this frame's alone, applied after the history (nativeShadowMaskPS).
float nativeRtCoverShadow(float3 worldPosition,float3 positionDx,float3 positionDy){
 uint info=shadowControl.x;
 [branch]if((info&256u)==0)return 1;
 uint slice=(info>>9)&7u;float layer=float(slice);
 float4 light=mul(shadowLight[slice],float4(worldPosition,1));float3 coordinate=light.xyz/light.w;
 if(abs(coordinate.x)>1||abs(coordinate.y)>1||coordinate.z<0||coordinate.z>1)return 1;
 float inside=saturate((1-max(abs(coordinate.x),abs(coordinate.y)))*5);
 float3 dx=mul((float3x3)shadowLight[slice],positionDx);
 float3 dy=mul((float3x3)shadowLight[slice],positionDy);
 float side=max(1.0,(float)shadowControl.z);
 float halfSide=.5*side;dx.xy*=float2(halfSide,-halfSide);dy.xy*=float2(halfSide,-halfSide);
 float3 plane=cross(dx,dy);
 if(plane.z==0||!all(isfinite(plane)))return 1;
 float2 depthStep=-plane.xy/plane.z;
 if(!all(isfinite(depthStep)))return 1;
 float bias=.5*shadowBias[slice].x*shadowBias[slice].y;
 float2 texel=float2(coordinate.x*.5+.5,.5-coordinate.y*.5)*side;
 float visibility=0;
 [loop]for(int y=-1;y<=1;y++)[loop]for(int x=-1;x<=1;x++){
  float2 sampleTexel=texel+float2(x,y);
  float depth=saturate(coordinate.z+dot(depthStep,sampleTexel-texel)-bias);
  visibility+=(x==0?2:1)*(y==0?2:1)*shadowDepth.SampleCmpLevelZero(shadowSampler,float3(sampleTexel/side,layer),depth);
 }
 return lerp(1,visibility/16,inside);
}
#endif
)" R"(
float2 nativeShadowMaskPS(float4 position:SV_Position):SV_Target{
 // floor first: float2(c)+.5 below must stay this pixel's centre even if a
 // compiler folds the int round trip (the impostor slice lesson, 2026-11-06).
 int2 c=int2(floor(position.xy));
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
#if defined(EID3D12)&&defined(NATIVE_RT_SUN)
  // The rays start at the rest position itself (on the scene's untessellated
  // terrain; the sun maps' slope lift is their bias, not a ray's). The
  // history is a plain running average (the sun maps' clamp to their own
  // filter's spread, nativeShadowHistoryClamp, would reject every one-ray
  // sample) of the static scene alone: the history follows the camera only,
  // so a moving caster's shadow in it stays where it was and fades over the
  // history's length (a dark trail behind every walking unit and rolling
  // tumbleweed, cand-1123). The units (instance mask 4) and the cover's
  // layer (critters, tumbleweeds, swaying cover) join after it, this
  // frame's alone, and the history target (the mask's y) keeps the static
  // term. A unit's own surface moves under the camera-only reprojection: it
  // takes no history and traces the whole scene.
  bool unitReceiver=(flags&8u)!=0;
  float3 drawn=centre.drawn;float2 historyUv;int2 historyPixel;
  bool reuse=temporal&&!unitReceiver&&nativeShadowHistoryAt(drawn,nativeTexelStep(px.valid,px.drawn,nx.valid,nx.drawn,drawn),
   nativeTexelStep(py.valid,py.drawn,ny.valid,ny.drawn,drawn),historyUv,historyPixel);
  visibility=nativeRtSunVisibility(rest,c,(flags&4u)!=0,reuse,unitReceiver,unitReceiver?0xFFu:0xFBu);
  [branch]if(reuse&&visibility>=0)visibility=lerp(visibility,previousShadowVisibility.SampleLevel(shadowHistorySampler,historyUv,0),
   asfloat(rtSunRecords.Load(4)));
  filtered=visibility;
  [branch]if(!unitReceiver&&visibility>0)visibility=min(visibility,nativeRtUnitShadow(rest));
  [branch]if(visibility>0)visibility=min(visibility,nativeRtCoverShadow(lookup,shadowDx,shadowDy));
#else
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
#endif
 }
 if(visibility>=0)visibility=min(visibility,cloud);
 return float2(visibility,filtered);
}
)" R"(
@CAVE_EXIT_GLOW@
StructuredBuffer<CaveExit> caveExits:register(t84);
float3 nativeCaveExitGlow(float3 p){
 uint count=((uint)caveVolumeTail)>>8;float3 glow=0;
 [branch]if(count>0&&caveVolumeStrength>0)glow=caveExitGlow(caveExits,count,fogEye.xyz,normalize(p-fogEye.xyz));
 return glow;
}
// ambient: the part of color contributed by the ambient light, for AO (see ambient_occlusion.h).
#if defined(NATIVE_RR_GUIDES)||defined(GC_RR_GUIDES)
// DLSS Ray Reconstruction's specular albedo (DLSS-RR Integration Guide,
// appendix, EnvBRDFApprox2: Ray Tracing Gems chapter 32), from F0, the
// squared roughness and N.V. Also the ground cover twins' (GC_RR_GUIDES).
float3 rrSpecularAlbedo(float3 specularColor,float alpha,float nv){
 nv=abs(nv);
 float4 x=float4(1,nv,nv*nv,nv*nv*nv),y=float4(1,alpha,alpha*alpha,alpha*alpha*alpha);
 float2x2 m1=float2x2(.99044,-1.28514,1.29678,-.755907);
 float3x3 m2=float3x3(1,2.92338,59.4188,20.3225,-27.0302,222.592,121.563,626.13,316.627);
 float2x2 m3=float2x2(.0365463,3.32707,9.0632,-9.04756);
 float3x3 m4=float3x3(1,3.59685,-1.36772,9.04401,-16.3174,9.22949,5.56589,19.7886,-20.2123);
 float bias=dot(mul(m1,x.xy),y.xy)*rcp(dot(mul(m2,x.xyw),y.xyw));
 float scale=dot(mul(m3,x.xy),y.xy)*rcp(dot(mul(m4,x.xzw),y.xyw));
 bias*=saturate(specularColor.g*50);
 return mad(specularColor,max(0,scale),max(0,bias));
}
#endif
#ifdef NATIVE_RR_GUIDES
// rt_ray_reconstruction (D3D12 only; renderer_native_d3d12.cpp
// append_native_guide_programs): the guide targets of DLSS Ray Reconstruction
// (DLSS-RR Integration Guide 3.4), written by the twins of the world's opaque
// and cut-out material programs from the values their shading computes.
// Target 5: the world shading normal, the linear roughness in alpha (3.4.3,
// 3.4.4: packed); 6: the diffuse albedo; 7: the specular albedo. Targets 3
// and 4 stay the object motion's. A pixel no twin writes keeps the frame's
// clear values, the guide's sky defaults.
// They are out parameters written where the shading computes each value (the
// geometric normal, roughness 1 and the dielectric F0 where no material
// response runs), so no guide value is held in a register to the program's end.
struct NativeGuides{float4 normal:SV_Target5;float4 diffuse:SV_Target6;float4 specular:SV_Target7;};
// rt_direct_lights (renderer_native_d3d12.cpp direct_lights_d3d12): what its
// last pass needs to add the traced lamp light exactly as the lamp loop
// would have, in the two albedo targets' alpha (16 bits each while the
// option is on; Ray Reconstruction reads their colour only). x, the diffuse
// target: the lighting factor's red and green (7 bits each: i.d at the
// combine step, already saturated, so saturate(it + lamps) - it is the lamp
// loop's share of saturate(base + lamps) whether the sum was clamped or
// not), bit 14 a lit receiver (it takes lamps; else the emissive glow
// only), bit 15 the terrain's grazing-light facing rule. y, the specular
// target: the factor's blue, then 8 bits of what the fog chain and a liquid
// over the pixel leave of an added light (the product the ambient share
// takes; 0: no traced light here), bit 15 a unit. Bit 16 (never written):
// a shadow receiver.
static uint2 nativeGuideLight=uint2(0,0);
#define NATIVE_GUIDE_OUT ,out NativeGuides guides
#define NATIVE_GUIDE_PASS ,guides
void nativeGuideSurface(out NativeGuides guides,float3 n,float roughness,float3 f0,float3 view,float3 diffuse){
 n=dot(n,n)>1e-8?normalize(n):float3(0,0,1);
 guides.normal=float4(n,roughness);
 guides.diffuse=float4(saturate(diffuse),1);
 guides.specular=float4(saturate(rrSpecularAlbedo(f0,roughness*roughness,dot(n,view))),1);
}
// rt_reflections (renderer_native_d3d12.cpp reflections_d3d12): on a traced
// frame the twins draw with the roughness cut-off in the lamp constants'
// spare word (0: not traced), and a surface smoother than it leaves its
// environment reflection (the sky probe or the ambient) to the traced pass,
// by the same weight that pass adds with: none below cut-off - .1, all of
// its own at the cut-off and above.
float nativeGuideUntraced(float roughness){return nativePointLightGridTail>0?smoothstep(nativePointLightGridTail-.1,nativePointLightGridTail,roughness):1;}
#define NATIVE_GUIDE_UNTRACED(roughness) *nativeGuideUntraced(roughness)
#else
#define NATIVE_GUIDE_OUT
#define NATIVE_GUIDE_PASS
#define NATIVE_GUIDE_UNTRACED(roughness)
#endif
#if defined(NATIVE_TERRAIN_PS)||defined(NATIVE_LOW)
@TERRAIN_ASTRAL_SHADER@
#endif
struct NativePixelResult{float4 color:SV_Target0;float4 shadow:SV_Target1;float4 ambient:SV_Target2;};
#ifdef NATIVE_TERRAIN_SLIM
// The slim terrain pass (option terrain_slim; renderer_flush.cpp): the
// prepassed terrain programs without terrain2Color. Every pixel takes the
// larger of its key and the mask's (terrainMissMask, terrainSlimKey); the prepassed
// full programs then draw again with nativeLiquidLayer bit 64: only the
// triangles with a missed pixel (terrainMissTriangleFlags) reach the
// rasteriser, and only the pixels whose mask is their own missed key
// survive. Early depth/stencil here: the depth EQUAL test rejects before
// the pixel stage (nothing writes depth or stencil), so only visible pixels
// key the mask.
[earlydepthstencil]
#endif
#ifdef NATIVE_OBJECT_MOTION
#define nativePS nativeMaterialPS // the motion entry below wraps it
#endif
#ifdef NATIVE_TERRAIN_SLIM
#define nativePS nativeSlimMaterialPS // the entry below writes Astral's hit key after all derivatives
#endif
#if !defined(NATIVE_OFF_MATERIALS)&&!defined(NATIVE_TERRAIN_PS)&&!defined(NATIVE_LOW)
// A lamp-lit figure's texture relief normal (Medium and up), derived as the
// material block derives it for the sun, here so that only the lamp loop
// holds it (the camp-fire and lava-lit plates at bz14h read flat).
void materialFigureLampRelief(NativeOutput i,float2 uvDx,float2 uvDy,float3 positionDx,float3 positionDy){
 float levels;float2 size=terrainTapSize(i.textureReference,levels);
 float2 su=float2(max(1/size.x,max(abs(uvDx.x),abs(uvDy.x))),0),sv=float2(0,max(1/size.y,max(abs(uvDx.y),abs(uvDy.y))));
 // The colour read comes after the lighting: the centre tap here (the same address).
 float centre=materialLuminance(terrainTapGrad(i.textureReference,i.uv,uvDx,uvDy).rgb);
 float hu=materialLuminance(terrainTapGrad(i.textureReference,i.uv+su,uvDx,uvDy).rgb)-centre;
 float hv=materialLuminance(terrainTapGrad(i.textureReference,i.uv+sv,uvDx,uvDy).rgb)-centre;
 float3 n=normalize(i.worldNormal),c1=cross(positionDy,n),c2=cross(n,positionDx);
 float3 tu=c1*uvDx.x+c2*uvDy.x,tv=c1*uvDx.y+c2*uvDy.y;
 float frame=rsqrt(max(max(dot(tu,tu),dot(tv,tv)),1e-20));
 float texels=max(su.x*size.x,sv.y*size.y);
 float relief=nativeMaterialReliefTap(i.textureReference.y,i.uv,uvDx,uvDy);
 materialNormal=normalize(n-(hu*tu+hv*tv)*(frame*6*nativeMaterialRelief*relief*materialReliefFade(uvDx,uvDy,size,texels,true)));
 materialReliefLamps=true;
}
#define NATIVE_FIGURE_LAMP_RELIEF(pointLit) [branch]if((pointLit)&&materialActive&&!NATIVE_MATERIAL_WORLD&&NATIVE_REC_UNIT(i)&&nativeMaterialLevel>=2&&nativeTextured!=0)materialFigureLampRelief(i,uvDx,uvDy,positionDx,positionDy);
#else
#define NATIVE_FIGURE_LAMP_RELIEF(pointLit)
#endif
#ifdef NATIVE_ASTRAL
// Astral rift veils and clumps (astral_rift.h; i.astral: kind, veil index,
// phase, clump fade). Veils: the strip's U scrolls with the phase, read
// twice half a period apart and blended with complementary sin^2 weights,
// so the wrap never shows; V stays. Clumps: their cell of the atlas, faded
// at the cell edges and over their life. Both read with the drawn UV's own
// gradients (uvDx, uvDy), so the mip level follows the surface.
float4 nativeAstralSample(NativeOutput i,float2 uvDx,float2 uvDy){
 [branch]if(i.astral.x<1.5){
  float x=frac((i.uv.x-NATIVE_ASTRAL_U0)/NATIVE_ASTRAL_UW+i.astral.z+i.astral.y*.173);
  float w=sin(3.14159265*x);w*=w;
  float4 a=terrainTapGrad(i.textureReference,float2(NATIVE_ASTRAL_U0+x*NATIVE_ASTRAL_UW,i.uv.y),uvDx,uvDy);
  float4 b=terrainTapGrad(i.textureReference,float2(NATIVE_ASTRAL_U0+frac(x+.5)*NATIVE_ASTRAL_UW,i.uv.y),uvDx,uvDy);
  return lerp(b,a,w);
 }
 float4 t=terrainTapGrad(i.textureReference,i.uv,uvDx,uvDy);
 float2 cell=saturate((i.uv-float2(NATIVE_ASTRAL_U0,NATIVE_ASTRAL_CLUMP_V0))/NATIVE_ASTRAL_UW);
 t.a*=sin(3.14159265*cell.x)*sin(3.14159265*cell.y)*i.astral.w;
 return t;
}
#endif
#if defined(NATIVE_TERRAIN_SLIM)||(defined(NATIVE_TERRAIN_PS)&&defined(NATIVE_SHADOW_MASK))
NativePixelResult nativePS(NativeOutput i,uint primitive:SV_PrimitiveID,bool frontFace:SV_IsFrontFace NATIVE_GUIDE_OUT){
#else
NativePixelResult nativePS(NativeOutput i,bool frontFace:SV_IsFrontFace NATIVE_GUIDE_OUT){
#endif
#ifdef NATIVE_RR_GUIDES
 // (The early returns below belong to frames that bind no guide target.)
 guides.normal=0;guides.diffuse=0;guides.specular=0;
#endif
 bool slimForeign=false; // #243: the slim second pass's pixel that is not this triangle's (below)
#if defined(NATIVE_TERRAIN_PS)&&defined(NATIVE_SHADOW_MASK)&&!defined(NATIVE_TERRAIN_SLIM)
 // The second pass after the slim pass: only the pixels this triangle won
 // with a miss (its slot: from the miss list's origin, bit 256, else drawn whole).
 // #243 (the tester's Radeon RX 580, edge_check 16, package r4): a 2x2 quad
 // leaves only when none of its pixels is this triangle's. A pixel of the
 // quad that is not (the pixel outside the triangle along its edges holds the
 // neighbour's key) runs on and leaves at the end. Discarded here, it never
 // entered the material branch on the Radeon (view 16: green and blue along
 // every triangle edge), so the screen differences the quad's own pixels took
 // against it (the relief's block test) failed and the relief dropped along
 // the edges. Unconfirmed on that GPU until its next package.
 [branch]if((nativeLiquidLayer&64u)!=0){
)" R"(
  uint slot=nativeTriangleBase+primitive;
  [branch]if((nativeLiquidLayer&256u)!=0)slot=terrainMissOrigin.Load(slot*4u);
  float own=terrainMissInput.Load(int3(int2(i.p.xy),0))==terrainSlimKey(slot,true)?1:0;
  if(own+abs(ddx_fine(own))+abs(ddy_fine(own))+abs(ddx_fine(ddy_fine(own)))==0)discard;
  slimForeign=own==0;
 }
#endif
#ifdef EID3D12_HAZARDS
 nativeGpuReportBegin(2u,i.textureReference.x,uint(i.p.x)|(uint(i.p.y)<<16));
#endif
 // The zone hit-test frame (#200, nativeCombinerTail): the vertex colour
 // (the figure's default colour, unlit) exactly, opaque; nothing else.
 [branch]if((nativeCombinerTail&1u)!=0){if(slimForeign)discard;NativePixelResult flat=(NativePixelResult)0;flat.color=float4(i.d.rgb,1);return flat;}
 if(nativeLodDiscard(i.swaying,i.p))discard;
#if defined(NATIVE_DOMAIN_CHECK)&&NATIVE_DOMAIN!=0
 {
  uint wrong=0;
  if(NATIVE_RT_UNIT(i)!=NATIVE_REC_UNIT(i))wrong|=1u;
  if(NATIVE_RT_FOLIAGE(i)!=NATIVE_REC_FOLIAGE(i)||NATIVE_RT_IMPOSTOR(i)!=NATIVE_REC_IMPOSTOR(i))wrong|=2u;
  if(NATIVE_RT_CONTACT(i)!=NATIVE_REC_CONTACT(i))wrong|=4u;
  if(!NATIVE_PROFILED&&NATIVE_RT_PROFILE(i))wrong|=8u;
  [branch]if(wrong!=0){NativePixelResult marker=(NativePixelResult)0;
#ifdef EID3D12_HAZARDS
   // The GPU's own count (the hazards build's report buffer, d3d12_frame.cpp): id 200 + the wrong bits.
   nativeGpuReportBegin(2u,i.textureReference.x,uint(i.p.x)|(uint(i.p.y)<<16));nativeGpuReportWrite(200u+wrong,NATIVE_DOMAIN|(NATIVE_PROFILED<<3));
#endif
   marker.color=float4((wrong&1u)?1:0,(wrong&2u)?1:0,(wrong&12u)?1:.5,1);if(slimForeign)discard;return marker;}
 }
#endif
#ifdef NATIVE_FUR_DEBUG
 if(((i.swaying>>24)&7u)==4u)discard;  // F10 fur_debug: a hidden layer
#endif
 // Crown impostor: the baked albedo replaces the texture read below, and its
 // object-space normal relights the pixel exactly as the vertex stage lights
 // a crown vertex (calculateWorldLighting: ambient, sun, emissive).
 bool impostor=NATIVE_REC_IMPOSTOR(i);
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
#if !defined(NATIVE_LOW)&&!defined(NATIVE_TERRAIN_PS)&&!defined(NATIVE_SHADOW_MASK)
 // Alpha-tested batches test before the lighting (as the Low path does):
 // pixels the test discards (foliage cards, cutout edges) skip the lamps,
 // the sun shadow and the material response. The lighting changes only
 // i.d.rgb, so the alpha is the one the late combine and test below see; the
 // texel is read with the quad's own gradients and only here, so nothing
 // stays live across the lighting (the texture read below is unchanged).
 // Not in the prepassed programs (NATIVE_SHADOW_MASK): they draw with depth
 // EQUAL over a prepass that already alpha-tested, so a fragment this test
 // would reject has no matching depth and never reaches the shader.
 [branch]if(nativeAlphaTest!=0){
  float4 coverage=float4(1,1,1,1);
  if(impostor)coverage=impostorTexel;else [branch]if(nativeTextured!=0)coverage=terrainTapGrad(i.textureReference,i.uv,uvDx,uvDy);
#ifdef NATIVE_ASTRAL
  coverage=nativeAstralSample(i,uvDx,uvDy);
#endif
  NATIVE_BURN_ALPHA(i,coverage);
  if(!nativeCompare(nativeCombineAlpha(i.d,i.s,coverage)))discard;
 }
#endif
#ifdef EID3D12_HAZARDS
 // prepass_depth_check on the GPU (renderer_native_d3d12.cpp): report word 7
 // is the check mode while a check draw runs. Every fragment the alpha test
 // keeps counts in word 6; word 5 counts those in front of the prepass depth
 // (mode 1: what the material pass's EQUAL loses; mode 2 also counts equal
 // depth, calibrating the counter). The check draw writes no colour.
 {const uint depthCheck=nativeGpuReport.Load(28);
  [branch]if(depthCheck!=0){
   const float stored=nativeSceneDepth.Load(int3(i.p.xy,0));uint previous;
   nativeGpuReport.InterlockedAdd(24,1u,previous);
   // Fragments the material pass discards later anyway (the draw-distance
   // dissolve without fog, which the prepass applies early) are not losses.
   float3 checkFog=i.worldPosition;
   [branch]if(i.shadowMode==0&&(fogStock.w>0||fogEye.w>.5))checkFog=fogUnproject(i.p.xy*fogScreen.xy,i.p.z);
   const bool dissolved=nativeFogEnable==0&&nativeDrawDissolvedAt(i.p,checkFog);
   [branch]if(!dissolved&&(i.p.z<stored||(depthCheck==2&&i.p.z==stored))){
    nativeGpuReport.InterlockedAdd(20,1u,previous);
    // The first reports name the batch kind (report 300; value bits: 1
    // terrain variant, 2 impostor, 4 swaying, 8 neighbour, 16 terrain vertex
    // flags set, 32 tessellation-split flag 16; bits 8+ the depth gap in
    // 1e-7 units, capped).
    [branch]if(depthCheck==1){
     uint kind=0;
#ifdef NATIVE_TERRAIN_PS
     kind|=1u;
#endif
     kind|=((i.swaying>>8)&1024u)!=0?2u:0u;kind|=(i.swaying&255u)!=0?4u:0u;kind|=i.neighbour!=0?8u:0u;
     kind|=i.terrainVertexFlags!=0?16u:0u;kind|=(i.terrainVertexFlags&16u)!=0?32u:0u;
     nativeGpuReportWrite(300u,kind|(min(uint((stored-i.p.z)*1e7),0xFFFFu)<<8));
    }
   }
   discard;
  }}
#endif
#if defined(NATIVE_TERRAIN_PS)||defined(NATIVE_LOW)
 // Pure Astral is an opaque emissive material on the existing terrain.
 // Keep the current atmosphere -> border -> distance order, including the
 // cave tint and fog-off dissolve; it has no surface-lighting/AO share.
 [branch]if((i.terrainVertexFlags&16384u)!=0&&((i.terrainVertexFlags>>16)&15u)==15u){
  float footprint=length(abs(positionDx.xy)+abs(positionDy.xy))*.075;
  float3 color=terrainAstralColor(i.worldPosition,fogEye.xyz,nativeTerrainClock.y*.055,
                                i.liquidDirections.y,footprint);
  if(nativeFogEnable!=0){
   float3 caveFog=nativeCaveFogLight(i.worldPosition)+nativeCaveExitGlow(i.worldPosition);
   float fogFactor=nativeFogFactor(i.s.a,i.p);
   [branch]if(nativeFogEnable==2u)fogFactor=min(fogFactor,smoothstep(0.,.1045,normalize(fogUnproject(i.p.xy*fogScreen.xy,i.p.z)-fogEye.xyz).z));
   nativeAtmosphereCoreAt(color,i.p,saturate(nativeFogColor.rgb+caveFog),i.worldPosition,fogHazeColorFrame.w>.5);
   color=lerp(saturate(borderFogColorAt(i.worldPosition,nativeFogColor.rgb)+caveFog),color,fogFactor);
   nativeDrawFadeToAt(color,i.worldPosition,nativeFogColor.rgb);
  }else if(nativeDrawDissolvedAt(i.p,i.worldPosition))discard;
#ifdef NATIVE_RR_GUIDES
  float3 normal=dot(i.worldNormal,i.worldNormal)>1e-8?normalize(i.worldNormal):float3(0,0,1);
  guides.normal=float4(normal,1);guides.diffuse=0;guides.specular=0;
#endif
#if defined(NATIVE_TERRAIN_PS)&&defined(NATIVE_SHADOW_MASK)&&!defined(NATIVE_TERRAIN_SLIM)
  if(slimForeign)discard;
  [branch]if((nativeLiquidLayer&128u)!=0)color=lerp(color,float3(1,0,0),.3);
#endif
  NativePixelResult astral;
  astral.color=float4(color,nativeHudOpacity);astral.shadow=float4(1,0,0,1);
  astral.ambient=float4(0,0,0,nativeHudOpacity);return astral;
 }
#endif
// Not in the D3D11 flat variants (#243, L-098): they have no facet split, and
// with no top-level use FXC took these derivatives two branches deep, in
// the snow foliage code terrain never runs.
#if defined(NATIVE_TERRAIN_PS)&&(defined(EID3D12)||!defined(NATIVE_TERRAIN_FLAT))
 // Dry material terrain: liquidFall.y becomes the facet split's edge weight.
 {float3 b=i.liquidFall.yzw,bDx=ddx(b),bDy=ddy(b);float keep=terrainCliffFacetKeep(b,bDx,bDy);
  if((i.terrainVertexFlags&(16u|4096u))!=0&&!liquidSurfaceEnabled(i.terrainVertexFlags))i.liquidFall.y=keep;}
#endif
#ifdef NATIVE_TERRAIN_PS
 // The terrain material runs before the lighting: it reads only the
 // interpolated inputs (never the lit colour), and none of the lighting's
 // values are held in registers across its stages.
#ifndef NATIVE_LOW
#ifdef EID3D12
  float4 t=float4(1,1,1,1);if(impostor)t=impostorTexel;else if(nativeTextured!=0)t=NATIVE_SAMPLE(i.textureReference,i.uv);
#else
  // D3D11 (#243, L-098): the read is taken at the top level with the top
  // level's gradients, as the Low read since 912ce172. Nested under the
  // impostor and textured tests FXC compiled it two branches deep (the
  // RX 580 drew dark dots along terrain triangle edges). Both reads use
  // uvDx and uvDy directly under the top level, which also keeps those
  // derivatives out of the long-tile branch, where FXC had taken them three
  // branches deep. An impostor or untextured pixel reads a texel it does
  // not use.
  float4 t;
  [branch]if(i.textureReference.y!=0)t=nativeTextureArray.SampleGrad(nativeSampler,float3(i.uv,i.textureReference.x),uvDx,uvDy);
  else t=nativeTexture.SampleGrad(nativeSampler,i.uv,uvDx,uvDy);
  if(impostor)t=impostorTexel;else if(nativeTextured==0)t=float4(1,1,1,1);
#endif
#endif
  t=nativeTerrainMaterialStages(i,t,shadowDx,shadowDy,uvDx,uvDy);
)" R"(
#if !defined(NATIVE_OFF_MATERIALS)&&!defined(NATIVE_LOW)
 // S3 relief: the blended baked height's screen derivatives (fine: per
 // pixel pair), taken here in uniform flow; cached and live pixels hand the
 // same height. A quad with a pixel lacking it (old-path tiles, fading
 // neighbour ground, a quad edge) takes no relief, so nothing steps there.
 float terrainHeight=terrainSurface.x*nativeMaterialTerrainRelief,terrainHas=terrainSurface.y>=0?1:0;
 float2 terrainHeightD=float2(ddx_fine(terrainHeight),ddy_fine(terrainHeight));
 bool terrainRelief=terrainHas>0&&ddx_fine(terrainHas)==0&&ddy_fine(terrainHas)==0&&nativeMaterialTerrainLevel>=1;
#endif
#if defined(NATIVE_TERRAIN_SLIM)&&defined(NATIVE_TERRAIN_LEAN)
 // Layer bit 512: a missed quad leaves here, before the lighting. The second
 // pass redraws every output of a missed pixel, so what the slim program
 // would go on to shade for it is never seen. The miss spreads over its 2x2
 // quad first (the rule at the end of this program, the same fine
 // differences), so all four lanes leave together and no lane that stays
 // takes a derivative against one that left. What decides whether a pixel
 // keys the mask at the end of the program is decided here the same way:
 // the alpha test (the alpha never depends on the lighting) and, with fog
 // off, the draw-distance dissolve.
 [branch]if((nativeLiquidLayer&512u)!=0){
  {float lane=terrainSlimMiss?1:0;
   float spread=abs(ddx_fine(lane))+abs(ddy_fine(lane))+abs(ddx_fine(ddy_fine(lane)));
   if(spread>0)terrainSlimMiss=true;}
  [branch]if(terrainSlimMiss){
   [branch]if(nativeAlphaTest!=0){if(!nativeCompare(nativeCombineAlpha(i.d,i.s,t)))discard;}
   float3 missFogPoint=i.worldPosition;
   [branch]if(i.shadowMode==0&&(fogStock.w>0||fogEye.w>.5))missFogPoint=fogUnproject(i.p.xy*fogScreen.xy,i.p.z);
   if(nativeFogEnable==0&&nativeDrawDissolvedAt(i.p,missFogPoint))discard;
   InterlockedMax(terrainMissMask[uint2(i.p.xy)],terrainSlimKey(nativeTriangleBase+primitive,true));
   terrainSlimListQuad(uint2(i.p.xy));
   uint missTriangle=nativeTriangleBase+primitive,missAddress=(missTriangle>>5)*4,missBit=1u<<(missTriangle&31u);
   [branch]if((terrainMissTriangleFlags.Load(missAddress)&missBit)==0)terrainMissTriangleFlags.InterlockedOr(missAddress,missBit);
   NativePixelResult missed=(NativePixelResult)0;return missed;
  }
 }
#endif
#endif
)" R"(
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
#ifdef EID3D12
 [branch]if(impostor)t=impostorTexel;
 else [branch]if(nativeTextured!=0){
  float2 textureDx=ddx_coarse(i.uv),textureDy=ddy_coarse(i.uv);
  t=terrainTapGrad(i.textureReference,i.uv,textureDx,textureDy);
 }
#else
 // D3D11 (#243, unconfirmed on the reporting GPU): the read is not nested
 // under the impostor and textured tests. FXC moves a derivative down to the
 // block that holds all its uses, so nested there the compiled program took
 // the gradients inside two levels of flow control, whatever the source
 // order (the RX 580 drew dark dots along terrain triangle edges). With both
 // reads directly under the top level the gradients are taken at the top
 // level; an impostor or untextured pixel reads a texel it does not use.
 {float2 textureDx=ddx_coarse(i.uv),textureDy=ddy_coarse(i.uv);
  [branch]if(i.textureReference.y!=0)t=nativeTextureArray.SampleGrad(nativeSampler,float3(i.uv,i.textureReference.x),textureDx,textureDy);
  else t=nativeTexture.SampleGrad(nativeSampler,i.uv,textureDx,textureDy);
  if(impostor)t=impostorTexel;else if(nativeTextured==0)t=float4(1,1,1,1);}
#endif
#ifdef NATIVE_ASTRAL
 t=nativeAstralSample(i,uvDx,uvDy);
#endif
 NATIVE_BURN_ALPHA(i,t);
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
 // The bounce light volume lights them too, with or without lamps.
 bool pointLit=i.shadowMode==1&&(nativePointLightCount!=0||caveVolumeStrength>0);
#if !defined(NATIVE_TERRAIN_PS)
 // Material pixels: lit figures drawn with a bound profile (nativeCombinerTail bit 1).
#ifdef NATIVE_GLTF
 const bool sknMaterial=true;
#else
 bool sknMaterial=nativeSknInstance(i)!=0;
#endif
 float3 sknBaseNormal=i.worldNormal;
 float sknAo=1;float3 sknEmission=0;
 float4 materialProfile=0;
 [branch]if(sknMaterial){
  NativeInstance m=nativeSknInstances[nativeSknInstance(i)-1];
  float3 orm=1;
  if((m.sknMaps.w&2u)!=0)orm=nativeSknTap(1,m.sknMaps.y,i.uv,uvDx,uvDy).rgb;
  orm*=m.sknFactors.zxy;
  materialProfile=float4(clamp(orm.y,.08,1),saturate(orm.z),.16,0);sknAo=saturate(orm.x);
  if((m.sknMaps.w&1u)!=0){
   float2 xy=nativeSknTap(0,m.sknMaps.x,i.uv,uvDx,uvDy).rg*2-1;
   float3 n=normalize(i.worldNormal),t=normalize(i.liquidTintDepth.xyz-n*dot(n,i.liquidTintDepth.xyz));
   float3 b=cross(n,t)*i.liquidTintDepth.w;
   i.worldNormal=normalize(t*xy.x+b*xy.y+n*sqrt(saturate(1-dot(xy,xy))));
  }
  // CULL_NONE uses clockwise front faces; SKN's front winding is CCW.
  // Flip the completed tangent-space normal, retaining the original vertex
  // normal as the baseline of the sun's diffuse correction below.
  if(frontFace&&m.sknFactors.w>1)i.worldNormal=-i.worldNormal;
  sknEmission=m.sknEmission.rgb;
  if((m.sknMaps.w&4u)!=0)sknEmission*=nativeSknTap(2,m.sknMaps.z,i.uv,uvDx,uvDy).rgb;
  else sknEmission=0;
  materialActive=materialLamps=true;materialView=NATIVE_INTERFACE?normalize(i.liquidView):normalize(fogEye.xyz-i.worldPosition);
  materialAlpha2=pow(materialProfile.r,4);
 }

#ifndef NATIVE_OFF_MATERIALS
 [branch]if(!sknMaterial&&NATIVE_MATERIAL_BOUND(i)&&nativeMaterialLevel>0&&i.shadowMode==1&&!impostor){
  materialProfile=nativeMaterialProfileTap(i.textureReference.y,i.uv);
  materialActive=true;
  materialView=NATIVE_INTERFACE?normalize(i.liquidView):normalize(fogEye.xyz-i.worldPosition);
  float roughness=clamp(materialProfile.r,.08,.98);
#ifndef NATIVE_LOW
  // Worn metal (Medium and up; not chain mail, whose weave rides in the sheen
  // slot): the texel's luma against its surroundings at least ~3 texels wide
  // (also when magnified close up, where the 4x-gradient crevice tap reads
  // the texel itself: crevice occlusion stayed 1 on blades). Darker holds
  // grime (rougher, occluded), brighter is a polished bevel or edge. Set
  // here so the lamp lobes take it too; the class constant alone left a blade
  // one uniform lobe over its smooth paint, which read as plastic
  // (weapons-1105: save996 mithril blade .15 everywhere in view 1).
  [branch]if(nativeMaterialLevel>=2&&materialProfile.g>.5&&materialProfile.a<=0&&nativeTextured!=0){
   float levels;float2 size=terrainTapSize(i.textureReference,levels);
   float2 wx=uvDx*4,wy=uvDy*4;
   wx*=max(1,3/(max(length(wx*size),1e-4)));wy*=max(1,3/(max(length(wy*size),1e-4)));
   materialWear=materialLuminance(terrainTapGrad(i.textureReference,i.uv,uvDx,uvDy).rgb)-
    materialLuminance(terrainTapGrad(i.textureReference,i.uv,wx,wy).rgb);
   roughness=clamp(roughness+materialProfile.g*(min(max(-materialWear,0)*3.5,.3)-min(max(materialWear,0)*2.5,.12)),.06,.95);
  }
#endif
  materialAlpha2=roughness*roughness*roughness*roughness;
  materialLamps=nativeMaterialLevel>=2;
 }
#ifndef NATIVE_LOW
 // World objects and plants (S2, Medium and up): lit, textured static
 // meshes, tree trunks included, and leaves (foliage parts, bit 30) with
 // their crown impostors; not figures (bit 29) or beds under liquid. Objects
 // are a rough dielectric whose roughness, relief and crevices come from
 // their own texture below; leaves take the foliage profile (materialFoliage,
 // no extra texture taps). The lamp loop takes the base roughness.
 else [branch]if(!sknMaterial&&nativeMaterialWorldLevel>=2&&i.shadowMode==1&&!NATIVE_REC_UNIT(i)&&
  (nativeTextured!=0||impostor)&&i.bedLiquid.x<=0){
  materialActive=materialWorld=true;
  materialFoliage=NATIVE_REC_FOLIAGE(i)||impostor;
  materialView=NATIVE_INTERFACE?normalize(i.liquidView):normalize(fogEye.xyz-i.worldPosition);
  float roughness=saturate((materialFoliage?kFoliageRoughness:.75)+nativeMaterialWorldRoughness);materialAlpha2=roughness*roughness*roughness*roughness;
  materialProfile=float4(roughness,0,.16,materialFoliage?kFoliageSheen:0); // metal 0, F0 .04 (x4); leaves a soft sheen
  materialLamps=nativeMaterialWorldLevel>=3;
 }
#endif
#endif
#endif
#if NATIVE_CONTACT_LATE
 // The lamps of this pixel. The ground contact blend (contact_blend.h) and
 // its ground receiver's lamps run where they are blended, below: carried
 // through this loop and the lighting, their values held ~14 registers (the
 // general program 46 -> 44 temps, the profiled crown impostor 47 -> 44).
 // A pixel the contact blend alone brought here before (no lamps of its
 // own) took only the emissive glow, which no light gives it without glow
 // lights (nativePointLightGlowCount) or which it never adds (no receiver).
 [branch]if(pointLit||(i.shadowMode==2&&nativePointLightGlowCount!=0&&nativeRasterTail.y!=0)){
  NATIVE_FIGURE_LAMP_RELIEF(pointLit)
  float3 lit=nativePointLighting(i,glow,!pointLit);
  materialReliefLamps=false;
  if(pointLit)local=lit;
 }
#else
 // Ground contact (contact_blend.h): the ground colour, blend weights and
 // ground normal first, so the lamp pass lights the ground receiver too (the
 // early blend: fewer registers than the late one for world objects and
 // plants, 37 vs 39 temps prepassed).
 float2 contactK=0;float3 contactArt=0;float3 contactNormal=float3(0,0,1);bool contactLamps=false;
#ifndef NATIVE_LOW // Low: ground contact blend off
 [branch]if(NATIVE_REC_CONTACT(i)&&nativeContactBlend.x>0){
  contactArt=contactBlendColor(i.worldPosition,i.worldNormal,positionDx,positionDy,i.contactHeight,contactK);
  [branch]if(contactK.x>0){
   contactNormal=gcGroundNormal(contactGround,i.worldPosition.xy-nativeContactOrigin.xy);
   contactLamps=nativePointLightCount!=0;
  }
 }
#endif
 float3 groundLocal=0,groundGlow=0;
 [branch]if(pointLit||contactLamps||(i.shadowMode==2&&nativePointLightGlowCount!=0&&nativeRasterTail.y!=0)){
  NATIVE_FIGURE_LAMP_RELIEF(pointLit)
  float3 lit=nativePointLighting(i,glow,!pointLit,contactLamps,contactNormal,groundLocal,groundGlow);
  materialReliefLamps=false;
  if(pointLit)local=lit;
 }
#endif
 // Ordered packed colors keep their existing behavior; lit materials retain
 // their existing receiver policy until transparent reception is decided.
 // Interface records receive no world sun or cloud shadow, wetness or snow (the
 // shelter, cloud and weather inputs are the world's, at the record's own coordinates).
 bool shadowReceiver=(i.shadowMode==1||(i.shadowMode==2&&nativeRasterTail.y!=0))&&!NATIVE_INTERFACE;
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
   // F10 bounce_debug: the bounce alone (x8, set in nativePointLighting).
   if(caveVolumeStrength>0&&(((uint)caveVolumeTail)&1u)!=0)base=0;
   i.d.rgb=saturate(base+local+i.emissive);
   unlit=saturate(i.directional*visibility+local+i.emissive);
  }else{
   // Current retained packed-world materials modulate TEXTURE by DIFFUSE.
   unlit=i.d.rgb*visibility;
   i.d.rgb*=lerp(saturate(nativeWorldAmbient.rgb),float3(1,1,1),visibility);
  }
 }
 else if(i.shadowMode==1&&nativePointLightCount!=0){
  float3 base=max(nativeWorldAmbient.rgb,i.directional);
  if(caveVolumeStrength>0&&(((uint)caveVolumeTail)&1u)!=0)base=0; // F10 bounce_debug: the bounce alone
  i.d.rgb=saturate(base+local+i.emissive);
  unlit=saturate(i.directional+local+i.emissive);
 }
 // Without lamps the vertex-lit colour stays (foliage and objects keep their
 // own lighting): the bounce light only adds to it.
 else if(i.shadowMode==1&&caveVolumeStrength>0){
  i.d.rgb=(((uint)caveVolumeTail)&1u)!=0?saturate(local):saturate(i.d.rgb+local);
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
#if defined(NATIVE_TERRAIN_PS)&&!defined(NATIVE_OFF_MATERIALS)&&!defined(NATIVE_LOW)&&!defined(NATIVE_UNLIT)
 // Terrain relief (S3, material level 1 and up): the height normal rescales
 // the per-vertex sun inside the game's max(ambient, sun) model, as the snow
 // trails do: tilts toward the sun brighten, away darken; an unchanged
 // normal keeps the existing sun. The ambient floor is untouched.
 float3 terrainN0=dot(i.terrainSlope,i.terrainSlope)>1e-8?normalize(i.terrainSlope):normalize(i.worldNormal),terrainN=terrainN0;
 // #243 (terrain_relief_derivfree): a pixel whose height came from the
 // material cache takes the height's world gradient from the cache's height
 // layer (central differences beside the pixel, terrain2CacheHeightGradient)
 // as a surface gradient on the vertex normal: no screen difference of the
 // height or of the position, so what the other pixels of its 2x2 quad hold
 // (another triangle's helper pixels, a pixel that computed its material per
 // pixel) cannot reach it. A pixel without a cached height keeps the screen
 // differences only where its whole quad has none either; a quad of both
 // takes no relief on its uncached pixels. A pixel whose block test failed
 // (terrainRelief false: on the tester's Radeon RX 580 every other pixel
 // along triangle edges, as the block's pixel outside the triangle holds no
 // height) and whose colour was computed per pixel reads the slope from the
 // cache's height layer too where the cache holds its place and the ground
 // is not a steep face (the cache is baked from above).
 bool reliefFree=false;float3 reliefFreeN=terrainN0;
 float reliefSource=terrainSurfaceCached?1:0;
 bool reliefMixed=ddx_fine(reliefSource)!=0||ddy_fine(reliefSource)!=0;
 bool reliefFromCache=terrainSurfaceCached;
 [branch]if(TERRAIN_RELIEF_DERIVFREE&&!reliefFromCache&&!terrainRelief&&terrainHas>0&&terrainN0.z>.7&&i.shadowMode==1)
  reliefFromCache=terrain2CacheHeightGradientHeld(i.shadowPosition.xy,shadowDx.xy,shadowDy.xy);
 [branch]if(TERRAIN_RELIEF_DERIVFREE&&reliefFromCache&&terrainHas>0&&nativeMaterialTerrainLevel>=1&&i.shadowMode==1){
  float3 gradient=float3(terrain2CacheHeightGradient(i.shadowPosition.xy,shadowDx.xy,shadowDy.xy)*nativeMaterialTerrainRelief,0);
  float3 lifted=terrainN0-(gradient-terrainN0*dot(gradient,terrainN0));
  reliefFreeN=dot(lifted,lifted)>1e-20?normalize(lifted):terrainN0;reliefFree=true;
 }
 // D3D11 (#243, L-098): flattened. As a branch FXC moved terrainHeightD's
 // derivatives into it, and terrainRelief differs between the pixels of a
 // quad next to a pixel without a height: the pixel diagonal to that one
 // then took the difference against a quad pixel that had skipped the
 // branch and never held the height (sun scale 0: a dark dot).
#ifdef EID3D12
 [branch]
#else
 [flatten]
#endif
 // #243: a pixel with a cached height slope (reliefFree) needs no other
 // pixel of its block and takes the relief whatever the block test says.
 if((terrainRelief||reliefFree)&&i.shadowMode==1){
  terrainN=terrainReliefNormal(terrainN0,positionDx,positionDy,terrainHeightD);
  if(reliefFree)terrainN=reliefFreeN;
  else if(TERRAIN_RELIEF_DERIVFREE&&reliefMixed)terrainN=terrainN0;
  float lengthSquared=dot(nativeWorldSunDirection.xyz,nativeWorldSunDirection.xyz);
  float3 sun=lengthSquared>1e-12?nativeWorldSunDirection.xyz*rsqrt(lengthSquared):float3(0,0,1);
  float baseFacing=saturate(dot(terrainN0,sun));
  float sunScale=clamp(1+(saturate(dot(terrainN,sun))-baseFacing)/max(baseFacing,.25),0,1.6);
  float3 sunLight=i.directional*(filteredShadow>=0?min(saturate(filteredShadow),cloud):1);
  float3 before=max(nativeWorldAmbient.rgb,sunLight),after=max(nativeWorldAmbient.rgb,sunLight*sunScale);
  i.d.rgb=saturate(i.d.rgb+after-before);unlit=saturate(unlit+sunLight*(sunScale-1));
 }
#endif
// Low: read and alpha-tested before the lighting; terrain: read with its material above.
#if !defined(NATIVE_LOW)&&!defined(NATIVE_TERRAIN_PS)
  float4 t=float4(1,1,1,1);if(impostor)t=impostorTexel;else if(nativeTextured!=0)t=NATIVE_SAMPLE(i.textureReference,i.uv);
#ifdef NATIVE_ASTRAL
  t=nativeAstralSample(i,uvDx,uvDy);
#endif
#endif
#ifdef NATIVE_SNOW_TRAILS
  // Packed trough floor: barely darker, cooler snow.
  t.rgb*=lerp(1,float3(.975,.98,.995),snowPacked);
#endif
#ifdef NATIVE_FIRE_BURN
 // Fire burn (nativeBurnAt): scorched ground and burnt crowns, their embers
 // while they burn (added after the game's combine, as the emissive glow).
 float3 burnGlow=0;
 [branch]if(nativeBurnRow(1).w>0&&i.shadowMode==1){
#ifdef NATIVE_TERRAIN_PS
  const float2 raw=nativeBurnGroundAt(i.worldPosition.xy),burn=nativeBurnPhase(raw);
  t.rgb=nativeBurnGroundColor(t.rgb,i.worldPosition.xy,burn.y);
  burnGlow=nativeBurnEmber(i.worldPosition,nativeBurnFront(raw)*.7);
#else
  const uint burnMaterial=NATIVE_REC_UNIT(i)?0:nativeBurnMaterial(i.textureReference.x,i.uv);
  [branch]if(burnMaterial==1u||burnMaterial==2u||(burnMaterial==3u&&nativeBurnRow(0).w>0)){
   const float2 burn=burnMaterial==3u?nativeBurnPhase(nativeBurnAt(i.shadowPosition.xy)):
    nativeBurnSurfacePhase(i.shadowPosition,i.burnRoot);
   t.rgb=burnMaterial==2u?nativeBurnLeafColor(t.rgb,i.shadowPosition,burn.y):nativeBurnWoodColor(t.rgb,i.shadowPosition,burn.y);
   if(burnMaterial==2u){if(burn.y>=.95)discard;t.a=nativeBurnLeafAlpha(t.a,i.shadowPosition,burn.y);}
   burnGlow=nativeBurnEmber(i.shadowPosition,burn.x);
  }
#endif
 }
#endif

#ifndef NATIVE_UNLIT
 // Wet surfaces (owned precipitation, issue #166). Porous materials (soil,
 // stone, wood, cloth) mostly darken and saturate when wet; their water
 // film stays rough (a broad, faint sheen). A sharp mirror-like highlight
 // belongs only to standing water (wetPuddle): for now the surf sheet below.
 // Vertical faces, figures and foliage stay subtle. The sheen is added
 // after the lighting; its roughness values are the inputs a roughness
 // material (#123) will replace. Dry frames cost one uniform test.
 float wet=0,wetSheen=0,wetPuddle=0;float3 wetNormal=float3(0,0,1);
 // wetPbr: the material response below carries this pixel's wet look (its
 // roughness, relief and Fresnel reflection), so the legacy film lobe keeps
 // only standing water and the rain-ring crests. Medium and up only: Low,
 // material_quality 0/1 and unprofiled pixels keep the legacy film.
 bool wetPbr=false;
#if !defined(NATIVE_LOW)&&!defined(NATIVE_OFF_MATERIALS)
#ifdef NATIVE_TERRAIN_PS
 wetPbr=terrainSurface.y>=0&&nativeMaterialTerrainLevel>=2&&i.shadowMode==1;
#else
 wetPbr=materialActive&&(NATIVE_MATERIAL_WORLD?nativeMaterialWorldLevel:nativeMaterialLevel)>=2;
#endif
#endif
 [branch]if(nativeWet.x>0&&shadowReceiver&&i.bedLiquid.x<=0){
  bool wetGround=(i.terrainVertexFlags&16)!=0;
  bool wetFoliage=NATIVE_REC_FOLIAGE(i)||impostor,wetUnit=NATIVE_REC_UNIT(i);
  wetNormal=wetGround&&dot(i.terrainSlope,i.terrainSlope)>1e-8?normalize(i.terrainSlope):normalize(i.worldNormal);
  float up=saturate((wetNormal.z-.15)/.65);
  wet=nativeWet.x*lerp(.3,1,up)*(wetGround?1:.75);
  // Dry under roofs, bridges and canopies (the precipitation shelter map).
  // The snow's edge (the pre-filtered cover, nativeShelterPatchy). Leaves
  // and figures skip the patchy noise: their wetness is faint (little
  // darkening, a weak sheen) and a crown's own cards fill the screen.
  float shelterCover=nativeShelterCover(i.worldPosition.xy,i.worldPosition.z-.3,false);
  [branch]if(!(wetFoliage||wetUnit))shelterCover=1-nativeShelterPatchy(1-shelterCover,i.worldPosition.xy+i.worldPosition.z*float2(.37,.61),length(abs(positionDx)+abs(positionDy)));
  wet*=1-shelterCover;
  // No wet darkening while snow lies (frozen ground): seen through the thin
  // snow of a shelter edge it drew a dark band (frame_001039).
  wet*=1-saturate(nativeSnowCover.x*3);
  // Leaves shed water (little darkening); figures take half.
  float porous=wet*(wetFoliage?.3:wetUnit?.5:1);
  t.rgb=lerp(t.rgb,pow(saturate(t.rgb),1.4)*.72,porous);
  // Film sheen: up-facing faces; faint on vertical faces, figures and leaves.
  wetSheen=wet*lerp(.2,1,up*up)*(wetFoliage?.25:wetUnit?.4:1);
  // No rain puddles yet: noise-placed patches read as random glare blobs
  // (c27 run). They return with a real terrain low-spot/roughness term.
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
    wetSheen=max(wetSheen,max(shore.z*.75,shore.x));wetPuddle=max(wetPuddle,shore.x);wetNormal=up; // the swash sheet is standing water
    wet=max(wet,shore.z*.75); // the wet sand's material response (S3)
   }
   // water_surf_debug 2: the shore impact over the shore ground too; 3: the
   // surf foam on the ground, as brightness.
   float surfDebug=fmod(liquidSurfMode.y,4);
   [branch]if(abs(surfDebug-2)<.5){t.rgb=liquidSurfImpactDebug(sea.y);wetSheen=wetPuddle=0;}
   else if(abs(surfDebug-3)<.5){t.rgb=saturate(max(shore.y,shore.w)).xxx;wetSheen=wetPuddle=0;}
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
  bool foliage=(NATIVE_REC_FOLIAGE(i)||impostor)&&!snowGround;
  // Creatures and characters (bit 29: the unit flag) take no
  // build-up: at most a faint sheen on their up-facing surfaces, from their
  // own normal only, so nothing slides as they move (c71 user note).
  bool animated=NATIVE_REC_UNIT(i);
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
  // Shelter: what lies overhead here, with the shared pre-filtered edge
  // (nativeShelterCover; rain-wet surfaces take the same).
  float covered=0;
  float2 q=i.worldPosition.xy+i.worldPosition.z*float2(.37,.61);
  [branch]if(up>0&&!foliage)covered=nativeShelterCover(i.worldPosition.xy,i.worldPosition.z,true);
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
   shelterKeep=nativeShelterPatchy(shelterKeep,q,footprint);
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
   // Snow carries no water film (the wet darkening is already off: snow cover).
   wetSheen*=1-snowPatch*shelterKeep*upKeep;wetPuddle*=1-snowPatch*shelterKeep*upKeep;
   [branch]if(snowPatch>0){
    // Clean bright snow lit by the scene, never darker than the texture.
    // Up close a fine crystalline grain (3 cm, Medium and up) keeps the
    // surface crisp instead of a blurry flat white (c71); volume: the body
    // brightens inward from the patch edge.
    // Objects (not ground or crowns) take the map's snow material's own
    // detail instead (nativeSnowSurface, its brightness about its mean, the
    // levels filtering it with distance): the grain aliased into speckle at
    // the game's camera distance (626b5ee6 user note).
    float grainWeight=nativeSnowCover.w>=2?saturate(1-footprint/.03):0,detail=1;
#ifndef NATIVE_TERRAIN_PS
    [branch]if(!snowGround&&!foliage){
     grainWeight=0;
     uint snowWidth,snowHeight;nativeSnowSurface.GetDimensions(snowWidth,snowHeight);
     [branch]if(snowWidth>0)detail=clamp(materialLuminance(nativeSnowSurfaceAt(i.worldPosition.xy,positionDx.xy,positionDy.xy).rgb)/
      max(materialLuminance(nativeSnowSurface.SampleLevel(nativeSnowSurfaceSampler,float2(.5,.5),8).rgb),.05),.75,1.2);
    }
#endif
    float grain=lerp(.5,nativeSnowNoise(q/.03+5.7),grainWeight);
    float body=smoothstep(threshold,threshold+.3,field);
    float3 snow=float3(.95,.97,1)*lerp(.88,1.02,body)*lerp(1,lerp(.94,1.04,grain),grainWeight)*detail;
    // Ground that is already snow (terrain) gains volume more than colour,
    // so its build-up never outlines shapes against the painted snow.
    t.rgb=lerp(t.rgb,max(t.rgb,snow),snowPatch*(snowGround?.65:1)*foliageKeep*shelterKeep*upKeep);
    terrainSnowShare=snowPatch*shelterKeep*upKeep; // S3: fresh snow takes the snow roughness
    terrainSnowFace=plane_normal;terrainSnowUp=up;terrainSnowThreshold=threshold;
   }
  }
  }
 }
#endif
)" R"(
#ifdef NATIVE_RR_GUIDES
 // The surface as the game's lighting sees it, before any material response
 // below replaces it: the texture after its wet, snow and surf changes as
 // the diffuse albedo; matte, F0 .04.
#if defined(NATIVE_TERRAIN_PS)&&!defined(NATIVE_OFF_MATERIALS)&&!defined(NATIVE_LOW)&&!defined(NATIVE_UNLIT)
 // The ground: its relief normal and its cell classes' roughness (none: matte).
 nativeGuideSurface(guides,terrainN,terrainSurface.y>=0?clamp(terrainSurface.y,.08,1):1,float3(.04,.04,.04),normalize(fogEye.xyz-i.worldPosition),t.rgb);
#else
 // The crown's baked normal, the ground's smooth slope normal, else the vertex normal.
 nativeGuideSurface(guides,impostor?impostorNormal:((i.terrainVertexFlags&16)!=0&&dot(i.terrainSlope,i.terrainSlope)>1e-8?i.terrainSlope:i.worldNormal),
  1,float3(.04,.04,.04),normalize(fogEye.xyz-i.worldPosition),t.rgb);
#endif
 {uint3 lit=uint3(saturate(i.d.rgb)*127+.5);
  nativeGuideLight=uint2(lit.r|(lit.g<<7)|(i.shadowMode==1?0x4000u:0u)|(i.pointMaterial.w>.5?0x8000u:0u),
                         lit.b|(NATIVE_REC_UNIT(i)?0x8000u:0u)|(shadowReceiver?0x10000u:0u));}
#endif
 float4 c=i.d;c.rgb=nativeCombineColor(i.d,i.s,t);c.a=nativeCombineAlpha(i.d,i.s,t);
  float4 d0=float4(unlit,i.d.a);float3 c0=nativeCombineColor(d0,i.s,t);
  if(nativeSpecularEnable!=0){c.rgb=saturate(c.rgb+i.s.rgb);c0=saturate(c0+i.s.rgb);}
  // Emissive-surface glow: direct light on top of the clamped game lighting
  // (shadow receivers only, so ordered effects keep their colour).
  if(shadowReceiver){c.rgb+=t.rgb*glow;c0+=t.rgb*glow;}
#if !defined(NATIVE_TERRAIN_PS)&&!defined(NATIVE_UNLIT)
  if(sknMaterial)c.rgb-=max(c.rgb-c0,0)*(1-sknAo);
#endif
#ifdef NATIVE_FIRE_BURN
  c.rgb+=burnGlow;c0+=burnGlow;
#endif
  // Blend finished material colours: blending t before the texture stage
  // also multiplied the ground by the object's diffuse/material response.
#ifndef NATIVE_LOW // Low: ground contact blend off
  [branch]if(NATIVE_REC_CONTACT(i)&&nativeContactBlend.x>0){
#if NATIVE_CONTACT_LATE
   // The ground colour, blend weights and ground normal, then the ground
   // receiver's lamps (nativeGroundPointLighting: the sums the shared lamp
   // loop gave it, in the same order).
   float2 k=0;float3 ground=contactBlendColor(i.worldPosition,i.worldNormal,positionDx,positionDy,i.contactHeight,k);
#else
   float2 k=contactK;float3 ground=contactArt;
#endif
   float3 groundColor=0,groundDirect=0;
   [branch]if(k.x>0){
#if NATIVE_CONTACT_LATE
    float3 contactNormal=gcGroundNormal(contactGround,i.worldPosition.xy-nativeContactOrigin.xy);
    float3 groundLocal=0,groundGlow=0;
    [branch]if(nativePointLightCount!=0)nativeGroundPointLighting(i,pointLit,contactNormal,groundLocal,groundGlow);
#endif
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
#if defined(NATIVE_TERRAIN_PS)&&!defined(NATIVE_OFF_MATERIALS)&&!defined(NATIVE_LOW)&&!defined(NATIVE_UNLIT)
  // Terrain response (S3, Medium and up): GGX of the sun (F0 .04, no metal)
  // at the cell classes' roughness with crevices packed in (rough crevices
  // hold no highlight) and fresh snow at the snow class's roughness, on the
  // relief normal; added to the lit colour like S1/S2, so AO never darkens
  // it. Dry soil, grass and rock stay near matte by their roughness. High:
  // the owned sky reflected by the smooth classes only (snow, ice, mud).
  // Rain-wet ground (wet, shelter and snow aware) is the same response at a
  // wetter roughness (kWetRoughness) on a flatter relief normal; Medium adds
  // the ambient's Fresnel reflection by the wetness (no probe read). The
  // legacy film below keeps only standing water and the rain-ring crests.
  [branch]if(wetPbr){
   float roughness=clamp(lerp(materialWetRoughness(terrainSurface.y,kWetRoughness,wet),nativeMaterialTerrainRough[0].w,saturate(terrainSnowShare)),.08,1);
   float3 n=normalize(lerp(terrainN,wetNormal,kWetFilmFlatten*wet)),v=normalize(fogEye.xyz-i.worldPosition);
#ifdef NATIVE_RR_GUIDES
   nativeGuideSurface(guides,n,roughness,float3(.04,.04,.04),v,t.rgb); // the response's own normal and roughness
#endif
   float sunLength=dot(nativeWorldSunDirection.xyz,nativeWorldSunDirection.xyz);
   float3 l=sunLength>1e-12?nativeWorldSunDirection.xyz*rsqrt(sunLength):float3(0,0,1);
   float3 h=normalize(l+v);float nl=saturate(dot(n,l)),nv=max(saturate(dot(n,v)),1e-3);
   float alpha2=roughness*roughness*roughness*roughness;
   float fresnel=.04+.96*pow(1-saturate(dot(v,h)),5);
   // Times pi: the game's light is irradiance/pi (see the S1 response).
   float3 specular=nativeWorldSunDiffuse.rgb*min(max(filteredShadow,0),cloud)*(fresnel*materialGgx(saturate(dot(n,h)),nv,nl,alpha2)*3.14159265);
   bool probe=nativeMaterialTerrainLevel>=3&&fogTemporal.z>.5&&fogEye.w>.5;
   float environmentWeight=probe?1:wet;
   [branch]if(environmentWeight>0&&roughness<.7){
    float3 r=reflect(-v,n),environment=nativeWorldAmbient.rgb;
    [branch]if(probe){
     float probeWidth,probeHeight,probeLevels;skyProbe.GetDimensions(0,probeWidth,probeHeight,probeLevels);
     float az=atan2(r.y,r.x)*(.5/3.14159265);az+=az<0?1:0;
     float3 sky=skyProbe.SampleLevel(fogLinearClamp,float2(az,.5-asin(clamp(r.z,-1,1))/3.14159265),roughness*(probeLevels-1)).rgb;
     environment=lerp(nativeWorldAmbient.rgb,sky,smoothstep(-.15,.2,r.z));
    }
    float2 brdf=materialEnvBrdf(roughness,nv);
    specular+=environment*(.04*brdf.x+brdf.y)*(saturate((.7-roughness)/.35)*nativeMaterialReflection*environmentWeight NATIVE_GUIDE_UNTRACED(roughness));
   }
   // Standing water (the surf swash) keeps the sharp film below instead.
   specular*=1-saturate(wetPuddle);
   c.rgb+=specular;c0+=specular;
   // F10 material_view 1: the roughness.
   if((uint)nativeMaterialView==1){c.rgb=roughness.xxx;c0=c.rgb;}
  }
#endif
#ifndef NATIVE_UNLIT
  // Wet sheen: a water-film lobe of the sun (shadowed like the surface) and
  // the sky reflected at grazing angles. Roughness: the soaked porous film
  // stays broad (.65 damp to .5 soaked, Blinn power 9 to 30); standing
  // water is sharp (.3, power 245). The grazing Fresnel rise is limited by
  // roughness, so rough wet faces never turn to mirrors.
  // Rain rings (the water's own drop rings, from the ring texture:
  // nativeRainRingSlope) mark every flat wet face while it rains, as far as
  // it is wet (sheltered ground takes none). Only the ring crests carry the
  // sharp water lobe; the film around them stays rough. 54690799 gated the
  // rings on puddles and 85ef7d4a removed the rain puddles, so the ground
  // lost its rings.
  float wetAmount=max(wetSheen,wetPuddle);
  [branch]if(wetAmount>0&&nativeWet.w>=2){
   float3 n=wetNormal;
   float puddleShare=wetPuddle/wetAmount;
#if defined(NATIVE_TERRAIN_PS)&&!defined(NATIVE_OFF_MATERIALS)&&!defined(NATIVE_LOW)
   // S3: the soaked film follows the ground's relief; standing water lies flat.
   if(terrainRelief&&(i.terrainVertexFlags&16)!=0)n=normalize(lerp(terrainN,n,puddleShare));
#endif
#ifndef NATIVE_LOW
   [branch]if(nativeWet.y>0&&nativeWet.w>=3&&n.z>.8){
    float2 rings=nativeRainRingSlope(i.worldPosition.xy,length(fwidth(i.worldPosition.xy)));
    float ringWet=saturate(wetAmount*2)*smoothstep(.8,.9,n.z);
    n=normalize(float3(n.xy-rings.xy*.5*max(puddleShare,ringWet),n.z));
    puddleShare=max(puddleShare,ringWet*saturate(length(rings.xy)*4));
   }
#endif
   // Material pixels (wetPbr): the material response carries the soaked
   // film; only standing water and the ring crests keep this sharp lobe.
   if(wetPbr)wetAmount*=puddleShare;
   [branch]if(wetAmount>0){
   float3 v=NATIVE_INTERFACE?normalize(i.liquidView):normalize(fogEye.xyz-i.worldPosition);
   float sunLength=dot(nativeWorldSunDirection.xyz,nativeWorldSunDirection.xyz);
   float3 l=sunLength>1e-12?nativeWorldSunDirection.xyz*rsqrt(sunLength):float3(0,0,1);
   float3 h=normalize(l+v);
   float nl=saturate(dot(n,l)),nh=saturate(dot(n,h)),nv=saturate(dot(n,v));
   float roughness=wetPbr?.3:lerp(lerp(.65,.5,nativeWet.x),.3,puddleShare);
   float alpha2=roughness*roughness*roughness*roughness;
   float power=2/alpha2-2;
   float lobe=pow(nh,power)*(power+8)*(1/25.13)*nl;
   float fresnel=.02+(1-roughness-.02)*pow(1-nv,5);
   float3 direct=nativeWorldSunDiffuse.rgb*min(max(filteredShadow,0),cloud);
   // Units (figures): the sky film is ambient light, as S1 treats the sky
   // reflection. The raw probe (the displayed sky, far above the game's
   // ambient) put a grey-blue film on rain-wet figures that sun shadow did
   // not touch, so their shadowed parts read light grey (user report,
   // new-game save in rain; pbr-rain-route2: a guard's hat gained +28 blue
   // in heavy rain at material_quality 0 while the scene darkened). For
   // units the probe is brought to the ambient level, a surface in sun shadow
   // keeps a fifth, and the film joins the ambient share (SSAO occludes it).
   bool unitFilm=NATIVE_REC_UNIT(i);
   float3 r=reflect(-v,n),sky=nativeWorldAmbient.rgb*(unitFilm?1:1.5);
   [branch]if(fogTemporal.z>.5&&fogEye.w>.5){
    float az=atan2(r.y,r.x)*(.5/3.14159265);az+=az<0?1:0;
    sky=skyProbe.SampleLevel(fogLinearClamp,float2(az,.5-asin(clamp(r.z,-1,1))/3.14159265),0).rgb;
    [branch]if(unitFilm){
     float probeWidth,probeHeight,probeLevels;skyProbe.GetDimensions(0,probeWidth,probeHeight,probeLevels);
     float upperLevel=max(probeLevels-3,0);
     float3 upper=(skyProbe.SampleLevel(fogLinearClamp,float2(.25,.25),upperLevel).rgb+skyProbe.SampleLevel(fogLinearClamp,float2(.75,.25),upperLevel).rgb)*.5;
     sky*=min(materialLuminance(nativeWorldAmbient.rgb)/max(materialLuminance(upper),1e-3),1);
    }
   }
   float3 sun=wetAmount*direct*lobe*(.02+fresnel)*1.5;
   float3 film=wetAmount*sky*fresnel*.6*saturate(n.z)*(unitFilm?lerp(.2,1,min(saturate(filteredShadow),cloud)):1);
   c.rgb+=sun+film;c0+=sun+(unitFilm?0:film);
   }
  }
#endif
)" R"(
#if !defined(NATIVE_TERRAIN_PS)&&!defined(NATIVE_UNLIT)
  // Material response (pseudo-PBR S1): energy-conserving GGX of the sun and
  // the lamps, the owned sky through a split-sum reflection, specular
  // occlusion and a cloth sheen, added to the game's lit colour. Both c and
  // c0 take the sun and lamp terms, so screen-space AO (the ambient share)
  // never darkens them; the sky reflection joins the ambient share.
  [branch]if(materialActive){
   float metal=materialProfile.g,dielectricF0=materialProfile.b*.25,sheenWeight=materialProfile.a;
   // Woven metal (chain mail, classes.json weave): a figure's metal texel
   // carries its weave in the sheen slot (metals take no cloth sheen).
   float weave=!NATIVE_MATERIAL_WORLD&&metal>.5?sheenWeight:0,weaveLink=0;
   if(weave>0)sheenWeight=0;
   float luminance=materialLuminance(t.rgb);
   // Dark paint between metal (chain links, rivet gaps) reads rougher;
   // polished presets keep most of their polish. Dielectrics keep their
   // class roughness: dark dye is not a rougher surface (the roughening put
   // EI's dark cloth, leather and skin at the .98 clamp, with no highlight
   // left), and their painted crevices lose specular through the occlusion.
   float roughness=sknMaterial?materialProfile.r:clamp(materialProfile.r+(1-smoothstep(.18,.65,luminance))*.28*saturate(materialProfile.r*1.5)*metal,.08,.98);
   if(!sknMaterial&&!NATIVE_MATERIAL_WORLD)roughness=clamp(roughness+metal*(min(max(-materialWear,0)*3.5,.3)-min(max(materialWear,0)*2.5,.12)),.06,.98);
   float3 n=normalize(i.worldNormal),n0=sknMaterial?normalize(sknBaseNormal):n,v=materialView;
   // Leaves are thin two-sided cards: the side facing the viewer (an
   // impostor: its baked crown normal) takes the response.
   if(materialFoliage){n=impostor?impostorNormal:n;n=dot(n,v)<0?-n:n;n0=n;}
   bool medium=(NATIVE_MATERIAL_WORLD?nativeMaterialWorldLevel:nativeMaterialLevel)>=2;
   // Specular occlusion: a painted crevice (texel darker than its blurred
   // surroundings) holds no highlight (Medium and up).
   float occlusion=1;
#ifndef NATIVE_LOW
   [branch]if(!sknMaterial&&medium&&nativeTextured!=0&&!materialFoliage){
    // Relief (world objects, S2; figures, S1): the texture's luma is the
    // height. Two taps one screen pixel (at least one texel) along u and v,
    // at the colour read's gradients, give its slope; a cotangent frame from
    // the screen derivatives (the game's meshes carry no tangents) tilts the
    // normal. World objects lose it past ~6 texels per pixel, figures only on
    // a stretched footprint (materialReliefFade). Figures scale it by their
    // class relief (t38).
    float2 dx=uvDx,dy=uvDy; // outside the branches (derivatives)
    float levels;float2 size=terrainTapSize(i.textureReference,levels);
    float2 su=float2(max(1/size.x,max(abs(dx.x),abs(dy.x))),0),sv=float2(0,max(1/size.y,max(abs(dx.y),abs(dy.y))));
    float hu=materialLuminance(terrainTapGrad(i.textureReference,i.uv+su,dx,dy).rgb)-luminance;
    float hv=materialLuminance(terrainTapGrad(i.textureReference,i.uv+sv,dx,dy).rgb)-luminance;
    float3 dp1=positionDx,dp2=positionDy;
    float3 c1=cross(dp2,n),c2=cross(n,dp1);
    float3 tu=c1*dx.x+c2*dy.x,tv=c1*dx.y+c2*dy.y;
    float frame=rsqrt(max(max(dot(tu,tu),dot(tv,tv)),1e-20));
    float texels=max(su.x*size.x,sv.y*size.y);
#ifdef EID3D12
    float relief=.5;[branch]if(!NATIVE_MATERIAL_WORLD)relief=nativeMaterialReliefTap(i.textureReference.y,i.uv,dx,dy);
#else
    float relief=NATIVE_MATERIAL_WORLD?.5:nativeMaterialReliefMap.SampleGrad(nativeSampler,i.uv,dx,dy);
#endif
    // The class relief map stores half the class value (classes run 0..2).
    float strength=6*nativeMaterialRelief*relief*materialReliefFade(dx,dy,size,texels,!NATIVE_MATERIAL_WORLD&&NATIVE_REC_UNIT(i))*(1-kWetReliefDamp*(wetPbr?wet:0)); // a water film flattens it
    n=normalize(n-(hu*tu+hv*tv)*(frame*strength));
    // Crevices: darker than the surroundings two mips up (a 4x wider read).
    float around=materialLuminance(terrainTapGrad(i.textureReference,i.uv,dx*4,dy*4).rgb);
    float crevice=max(around-luminance,0);
    occlusion=saturate(1-crevice*3);
    // Worn metal (figures, set with the lamp roughness): its grime holds less light too.
    occlusion=min(occlusion,saturate(1-max(-materialWear,0)*3));
    // World roughness: crevices and dark paint rougher, bright smooth paint a little less rough.
    // Profiled scenery (not a figure, bit 29): its stone and wood vary the
    // same way, its iron keeps the metal path below.
    // Crystal texels keep their class roughness (the crystal programs only;
    // the others keep this test as it was, so their bytecode stays the same).
#ifdef NATIVE_CRYSTAL
    if(NATIVE_MATERIAL_WORLD||(!NATIVE_REC_UNIT(i)&&metal<.5&&!NATIVE_CRYSTAL_TEXEL(materialProfile)))roughness=clamp(materialProfile.r+crevice*.5-smoothstep(.45,.85,luminance)*.2,.3,.98);
#else
    if(NATIVE_MATERIAL_WORLD||(!NATIVE_REC_UNIT(i)&&metal<.5))roughness=clamp(materialProfile.r+crevice*.5-smoothstep(.45,.85,luminance)*.2,.3,.98);
#endif
    // Woven metal: a link is brighter than the weave around it (read at
    // least ~3 texels wide, also when the texture is magnified close up),
    // a gap darker. Links: polished full metal; gaps: rough, no metal, no
    // highlight. One uniform class read as a flat sheet (user, many times).
    else if(weave>0){
     float2 wx=dx*4,wy=dy*4;
     wx*=max(1,3/(max(length(wx*size),1e-4)));wy*=max(1,3/(max(length(wy*size),1e-4)));
     float weaveAround=materialLuminance(terrainTapGrad(i.textureReference,i.uv,wx,wy).rgb);
     weaveLink=smoothstep(.82,1.18,(luminance+.02)/(weaveAround+.02));
     metal=lerp(metal*(1-weave),metal,weaveLink);
     roughness=lerp(lerp(roughness,.9,weave),max(roughness-.22*weave,.12),weaveLink);
     occlusion*=lerp(1-.75*weave,1,weaveLink);
    }
    // Metal masks: a chain-mail gap, much darker than its 4x-wider
    // surroundings, loses its metal. Relative, never absolute: a uniformly
    // dark metal (meteorite, blackened steel) matches its surroundings and
    // keeps full metal; the offset keeps near-black noise from reading as gaps.
    else if(nativeMaterialMetalMask>.5&&metal>0)metal*=smoothstep(.35,.7,(luminance+.02)/(around+.02));
   }
#endif
)" R"(
   // Rain-wet (Medium and up: wetPbr; shelter and snow aware): smoother by
   // class (kWetRoughness), never rougher; soaked cloth loses half its sheen.
   [branch]if(wetPbr&&wet>0){
    float wetTarget=materialFoliage?kWetFoliageRoughness:lerp(kWetRoughness,roughness*kWetMetalRoughness,metal);
    roughness=materialWetRoughness(roughness,wetTarget,wet);sheenWeight*=1-.5*wet;
   }
   // Snow build-up on objects and crowns (terrainSnowShare, set by the build-up above; units take none), after
   // the world relief's texture roughness (which replaced it when it ran first): the covered share answers as
   // fresh snow, as the terrain does (S3): the snow class roughness, no metal or sheen.
   // The snow also buries the texture's relief and painted crevices (both read from the covered texture):
   // a relief normal bent by the hidden planks under white snow and the glossier snow roughness turned
   // snowed roofs and rails into chrome, half glare, half dark blue sky (1102 user report, black blotches).
   // The covered share takes the snow's own relief instead, through the terrain's relief normal
   // (terrainReliefNormal): the map's snow material height (nativeSnowSurface, top-down, its levels
   // filtering it with distance; forward differences one pixel along, as this branch is not in uniform
   // flow) on the build-up's large drift slope (nativeSnowDriftSlope; the drift alone without a snow
   // material). Its base is the face's geometric normal, as the build-up's cover takes it: the smoothed
   // vertex normals of a low-poly roof swing across its flat face (x -.85 to +.9 across the save1193
   // roof) and drew a crease down its middle. The relief's diffuse change is measured from that base
   // too (n0), so only the snow's own relief changes the light.
   [branch]if(terrainSnowShare>0){
    float snowShare=saturate(terrainSnowShare);roughness=lerp(roughness,nativeMaterialTerrainRough[0].w,snowShare);metal*=1-snowShare;sheenWeight*=1-snowShare;
    float3 snowN=n0;
    [branch]if(!materialFoliage){
     const float2 lean=float2(.37,.61);
     float2 slope=nativeSnowDriftSlope(i.worldPosition.xy+i.worldPosition.z*lean,terrainSnowThreshold,terrainSnowUp);
     float2 dh=float2(dot(slope,positionDx.xy+positionDx.z*lean),dot(slope,positionDy.xy+positionDy.z*lean));
     uint snowWidth,snowHeight;nativeSnowSurface.GetDimensions(snowWidth,snowHeight);
     [branch]if(snowWidth>0){
      float2 p=i.worldPosition.xy,px=positionDx.xy,py=positionDy.xy;
      float h=nativeSnowSurfaceAt(p,px,py).a;
      dh+=(float2(nativeSnowSurfaceAt(p+px,px,py).a,nativeSnowSurfaceAt(p+py,px,py).a)-h)*nativeMaterialTerrainRelief;
     }
     snowN=terrainReliefNormal(terrainSnowFace,positionDx,positionDy,dh);
     n0=normalize(lerp(n0,terrainSnowFace,snowShare));
    }
    n=normalize(lerp(n,snowN,snowShare));occlusion=lerp(occlusion,1,snowShare);
   }
   float alpha2=roughness*roughness*roughness*roughness;
   float nv=max(saturate(dot(n,v)),1e-3);
   float3 f0=lerp(dielectricF0.xxx,t.rgb,metal);
   f0=lerp(f0,saturate(f0*1.6+.06),weaveLink*weave);   // polished link tops
#ifdef NATIVE_RR_GUIDES
   // The response's normal, roughness and F0; metals keep the share of the
   // diffuse colour their shading keeps.
   nativeGuideSurface(guides,n,roughness,f0,v,t.rgb*lerp(1,nativeMaterialDiffuseKeep,metal));
#endif
   // Metals keep part of their diffuse light; emissive and highlight are exempt.
   float3 emission=t.rgb*i.emissive+(nativeSpecularEnable!=0?i.s.rgb:float3(0,0,0));
   float keep=lerp(1,nativeMaterialDiffuseKeep,metal);
   c.rgb=max(c.rgb-emission,0)*keep+emission;c0=max(c0-emission,0)*keep+emission;
   // The game's own per-vertex specular on profiled pixels (user decision):
   // metal keeps it; a dielectric keeps it by its F0 (1 at .04) and
   // smoothness, so bone, leather and cloth stop shining like metal.
   if(!NATIVE_MATERIAL_WORLD&&nativeSpecularEnable!=0){
    float smooth=1-roughness;
    float3 drop=i.s.rgb*(1-lerp(saturate(dielectricF0*25)*smooth*smooth,1,metal));
    c.rgb=max(c.rgb-drop,0);c0=max(c0-drop,0);
   }
   float sunLength=dot(nativeWorldSunDirection.xyz,nativeWorldSunDirection.xyz);
   float3 l=sunLength>1e-12?nativeWorldSunDirection.xyz*rsqrt(sunLength):float3(0,0,1);
   float3 h=normalize(l+v);float nl=saturate(dot(n,l));
   float3 sunLight=nativeWorldSunDiffuse.rgb*min(saturate(filteredShadow),cloud);
   // Relief: the sun's diffuse change from the derived normal (zero on
   // average; none in shadow; metals keep their diffuse share). Emission
   // and highlight stay as they were.
// Authored SKN normals affect diffuse light at every quality level.
   {
    float3 relief=sunLight*t.rgb*keep*(nl-saturate(dot(n0,l)));
#ifdef NATIVE_GLTF
    float3 unlitRelief=relief;
    // The vertex stage uses max(ambient, sun), so a back face or authored
    // normal must replace that maximum, preserving the ambient floor.
    if(sknMaterial){
     float3 before=max(nativeWorldAmbient.rgb,sunLight*saturate(dot(n0,l)));
     float3 after=max(nativeWorldAmbient.rgb,sunLight*nl);
     relief=(after-before)*t.rgb*keep;
    }
#endif
#ifdef NATIVE_GLTF
    c.rgb=max(c.rgb+relief,0);c0=max(c0+unlitRelief,0);
#else
    c.rgb=max(c.rgb+relief,0);c0=max(c0+relief,0);
#endif
   }
   // Figures (Medium and up): the relief and the painted crevices shade the
   // ambient share too, so gear keeps its detail in shade. The game's
   // ambient has no direction; the sky's does (brightest overhead, leaning
   // toward the sun's side), so a normal tilted toward it gains and one
   // tilted away loses (zero on average); crevices hold less sky light.
   // Not on profiled scenery (bit 29 clear): like the S2 world response,
   // its dark painted crevices and strips (wicker, planks) went near black.
   [branch]if(medium&&!NATIVE_MATERIAL_WORLD&&NATIVE_REC_UNIT(i)){
    float3 skyDirection=normalize(float3(l.xy*.5,1));
    float ambientLight=max(1+3*dot(n-n0,skyDirection),0)*lerp(1,occlusion,.8);
    c.rgb+=max(c.rgb-c0,0)*(ambientLight-1);
   }
   float3 fresnel=f0+(1-f0)*pow(1-saturate(dot(v,h)),5);
   float3 specular=sunLight*fresnel*materialGgx(saturate(dot(n,h)),nv,nl,alpha2);
   specular+=f0*materialPointSpecular+materialPointFresnel;
   // The game's diffuse is albedo times its light (no 1/pi), so its light
   // value is irradiance/pi: a BRDF lobe takes that light times pi, on
   // figures (S1), world objects (S2) and terrain (S3) alike.
   specular*=3.14159265;
   // The owned sky probe at a roughness-selected mip; caves and the stock
   // sky reflect the authoritative ambient; below the horizon, the ground's.
   // World objects reflect the sky at High only; leaves never (a rough card's
   // sky share is invisible, but its probe tap ran on every overdrawn leaf
   // pixel: ~0.4 ms of the figure pass at the save1027/save1133 4K views).
   bool reflecting=!NATIVE_MATERIAL_WORLD||(nativeMaterialWorldLevel>=3&&!materialFoliage);
   float3 r=reflect(-v,n),environment=nativeWorldAmbient.rgb;
   [branch]if(reflecting&&fogTemporal.z>.5&&fogEye.w>.5){
    float probeWidth,probeHeight,probeLevels;skyProbe.GetDimensions(0,probeWidth,probeHeight,probeLevels);
    float az=atan2(r.y,r.x)*(.5/3.14159265);az+=az<0?1:0;
    float3 sky=skyProbe.SampleLevel(fogLinearClamp,float2(az,.5-asin(clamp(r.z,-1,1))/3.14159265),roughness*(probeLevels-1)).rgb;
    // The probe holds the displayed sky, far brighter than the light the
    // game gives a surface in shade (its ambient). Reflected sky radiance is
    // brought to the game's own sky-light level: the upper-hemisphere average
    // (the 4x2 mip's top row) maps to the ambient, never above the probe.
    float upperLevel=max(probeLevels-3,0);
    float3 upper=(skyProbe.SampleLevel(fogLinearClamp,float2(.25,.25),upperLevel).rgb+skyProbe.SampleLevel(fogLinearClamp,float2(.75,.25),upperLevel).rgb)*.5;
    sky*=min(materialLuminance(nativeWorldAmbient.rgb)/max(materialLuminance(upper),1e-3),1);
    environment=lerp(nativeWorldAmbient.rgb,sky,smoothstep(-.15,.2,r.z));
   }
   // The game has no sky-visibility term: a surface in sun shadow (under a
   // roof, beside a wall) sees its surroundings, which the game lights at its
   // ambient level, rather than the open sky. It reflects that level. (A
   // fifth of the sky, the earlier rule, left metal, which keeps only part of
   // its diffuse, at about half its q0 brightness in shade.)
   float sunVisibility=min(saturate(filteredShadow),cloud);
   environment=lerp(nativeWorldAmbient.rgb,environment,sunVisibility);
   float2 brdf=materialEnvBrdf(roughness,nv);
   // The sky reflection is ambient light: the split-sum energy it reflects
   // leaves the diffuse ambient share (the game's colour above its direct
   // light), and it joins that share, so screen-space AO occludes it like the
   // ambient. Sun and lamp specular stay outside the AO.
   // Rain-wet world objects and leaves that skip the sky probe (Medium; leaves
   // always) reflect the ambient by their wetness: the film's Fresnel sheen,
   // ALU only (environment stays the ambient here).
   float3 skyReflection=0;
)" R"(
   float reflectionWeight=reflecting?1:wetPbr?wet:0;
   [branch]if(reflectionWeight>0){
    float3 reflected=saturate((f0*brdf.x+brdf.y)*nativeMaterialReflection)*reflectionWeight;
    c.rgb-=max(c.rgb-c0,0)*reflected;
    skyReflection=environment*reflected*occlusion*(sknMaterial?sknAo:1) NATIVE_GUIDE_UNTRACED(roughness);
   }
   specular*=occlusion;
   // Cloth and leather sheen in ambient light (Medium and up): the sheen
   // lobe's directional albedo under the environment, a soft rim toward the
   // silhouette in shade as in sun. The diffuse ambient gives up the same
   // share (Filament's cloth energy rule), so it joins the ambient share.
   [branch]if(medium&&sheenWeight>0){
    float3 sheenColor=sqrt(t.rgb)*sheenWeight;
    float sheenAlbedo=materialSheenAlbedo(nv,roughness);
    c.rgb-=max(c.rgb-c0,0)*(max(sheenColor.r,max(sheenColor.g,sheenColor.b))*sheenAlbedo);
    skyReflection+=environment*sheenColor*(sheenAlbedo*occlusion);
   }
   // Cloth and leather sheen (Medium and up): the Charlie sheen distribution
   // with Neubelt visibility (Estevez and Kulla 2017, as in Filament's cloth
   // model), sheen colour sqrt(albedo) scaled by the class sheen; the sun
   // light times pi as above.
   [branch]if(medium&&sheenWeight>0){
    float nh=saturate(dot(n,h)),inverseAlpha=1/max(roughness*roughness,.07);
    float sheenD=(2+inverseAlpha)*pow(max(1-nh*nh,1e-4),inverseAlpha*.5)*(.5/3.14159265);
    float sheenV=1/(4*max(nl+nv-nl*nv,1e-3));
    specular+=sunLight*sqrt(t.rgb)*(sheenWeight*sheenD*sheenV*nl*3.14159265*occlusion);
   }
   // Backlit leaves: sun light through the leaf in its own colour, strongest
   // looking toward the sun (forward scatter), plus a little diffuse
   // transmission on the leaf's far side; leaves in a crown's sun shadow pass
   // none (sunLight carries the sun visibility).
   if(medium&&materialFoliage)
    specular+=sunLight*t.rgb*(kFoliageTransmission*(pow(saturate(dot(-v,l)),4)+.25*saturate(-dot(n,l))));
#if !defined(NATIVE_OFF_MATERIALS)&&!defined(NATIVE_LOW)
   // Gems (diamond, figures only; one flag test elsewhere). Fire: the sun and
   // lamp highlights and the sky reflection split into a spectrum whose phase
   // follows the reflection's angle to the sun and the painted facets' relief
   // normal, so it sparkles face by face as the view turns. Light through: a
   // wrap term from the side away from the light and a forward-scatter peak
   // looking toward it, a soft core tinted by the stone's colour. Neither
   // takes the sun shadow (at the stone the shadow is the stone itself).
   // A gem: a figure texel with dielectric F0 .12+ (profile b x4 > .48).
   [branch]if(!NATIVE_MATERIAL_WORLD&&materialProfile.g<.5&&materialProfile.b>=122./255.){
    float phase=dot(r,l)*7+dot(n-n0,float3(5.3,7.1,3.7));
    float3 spectrum=saturate(.5+.5*cos(6.2831853*(phase+float3(0,.333,.667))));
    specular*=lerp(1,spectrum*2.2,.65);skyReflection*=lerp(1,spectrum*2,.35);
    // Cut stone: the painted facets facing away from the light go dark, those
    // facing it stay bright, so the body reads as cut faces with sharp
    // highlights rather than an evenly lit translucent candy (user 2026-11-06).
    float body=lerp(.25,.85,saturate(.5+3*dot(n-n0,l)));c.rgb*=body;c0*=body;
    float through=.25*saturate((.4-dot(n,l))/1.4)+.6*pow(saturate(dot(-v,l)),4);
    float3 light=nativeWorldSunDiffuse.rgb*cloud*through;
    [branch]if(nativePointLightCount!=0)light+=nativeGemLampThrough(i)*.5;
    float3 transmitted=light*sqrt(max(t.rgb,1e-3))*(.6+.4*nv);
    c.rgb+=transmitted;c0+=transmitted;
   }
#endif
#if defined(NATIVE_CRYSTAL)&&!defined(NATIVE_OFF_MATERIALS)
   // Crystal texels (the crystal instance programs only, NATIVE_CRYSTAL):
   // big, smooth-painted, flat-faceted stones, where a glossy class alone
   // left the sun highlight on no facet and the gem branch dulled the body
   // (crystal preview 2026-11-14, user choice V+S+T). The body is lit from
   // inside: each geometric facet (from the position derivatives, so smooth
   // vertex normals still read as faces) bends the light to its own hashed
   // direction, so faces go bright or dark as the sun or view turns; the
   // sun through the stone (no sun shadow, as the gem), a wrap term and the
   // lamps' light through it, all tinted by an absorption that deepens with
   // the thickness the view crosses (.35 at the rim to 1 facing the view);
   // a small inner glow in the art's own colour; world crystals then show
   // the scene behind them (the crystal scene copy, liquidSceneColor slice
   // 0, taken at the flush's first crystal batch; no copy bound: opaque),
   // displaced along the refracted ray. A Schlick rim (F0 .05, quartz) of
   // the sky probe and the class's GGX highlights sit on top.
   [branch]if(NATIVE_CRYSTAL_TEXEL(materialProfile)){
    float3 face=normalize(cross(positionDx,positionDy));face*=dot(face,n0)<0?-1:1;
    float turn=6.2831853*frac(sin(dot(round(face*6),float3(12.9898,78.233,37.719)))*43758.5453);
    float turnSin,turnCos;sincos(turn,turnSin,turnCos);
    float3 inner=normalize(float3(face.x*turnCos-face.y*turnSin,face.x*turnSin+face.y*turnCos,abs(face.z)*.5+.35));
    float3 sunThrough=nativeWorldSunDiffuse.rgb*cloud,ambient=nativeWorldAmbient.rgb;
    float peak=max(t.r,max(t.g,t.b));
    float3 tint=saturate(t.rgb/max(peak,.05)*sqrt(peak));
    float facet=.12+1.5*pow(saturate(dot(inner,l)),3)+.35*saturate(inner.z);
    float faceLight=saturate(dot(face,l)),forward=saturate(dot(-v,l));
    float3 body=t.rgb*(ambient*.35+sunLight*(.25*faceLight))+tint*(sunLight*(facet*1.1)+ambient*.3+sunThrough*(.6*forward*forward*forward))+t.rgb*.2;
    float thick=.35+.65*nv;
    float3 absorb=pow(max(tint,1e-3),1+2*thick);
    float back=1.4*forward*forward+.35*saturate(-dot(face,l));
    float3 lamps=0;[branch]if(nativePointLightCount!=0)lamps=nativeGemLampThrough(i);
    body=body*.55+absorb*(sunThrough*back+sunLight*(.55*saturate((dot(face,l)+.6)/1.6))+ambient*.55+lamps*.5);
#if NATIVE_CRYSTAL==1
    uint sceneWidth,sceneHeight,sceneSlices;liquidSceneColor.GetDimensions(sceneWidth,sceneHeight,sceneSlices);
    [branch]if(sceneWidth>0){
     float3 bend=(refract(-v,n,1/1.55)+v)*(.25*thick);
     float2 shift=clamp(float2(dot(bend,positionDx)/max(dot(positionDx,positionDx),1e-8),dot(bend,positionDy)/max(dot(positionDy,positionDy),1e-8)),-64,64);
     float3 behind=liquidSceneColor.SampleLevel(liquidSceneSampler,float3((i.p.xy+shift)/float2(sceneWidth,sceneHeight),0),0).rgb;
     behind*=pow(max(sqrt(tint),1e-3),.4+1.2*thick)*1.25;
     body=lerp(body,behind,.6)+body*.12;
    }
#endif
    float rim=.05+.95*pow(1-nv,5);
    c.rgb=body*(1-rim)+environment*(1.5*rim)+specular;c0=c.rgb;
    specular=0;skyReflection=0;
   }
#endif
   c.rgb+=specular+skyReflection;c0+=specular;
   // F10 material_view: 1 roughness, 2 metal, 3 F0 / sheen / occlusion.
   uint materialViewMode=(uint)nativeMaterialView;
   if(materialViewMode==1){c.rgb=roughness.xxx;c0=c.rgb;}
   else if(materialViewMode==2){c.rgb=metal.xxx;c0=c.rgb;}
   else if(materialViewMode==3){c.rgb=float3(dielectricF0*4,sheenWeight,occlusion);c0=c.rgb;}
  }
  // material_view 2: a lit unit drawn without a profile shows magenta.
  else if((uint)nativeMaterialView==2&&nativeMaterialLevel>0&&NATIVE_REC_UNIT(i)&&i.shadowMode==1){c.rgb=float3(1,0,1);c0=c.rgb;}
#endif
  // Caustics on ground under water only (bed depth > 0, no bed emission);
  // everything else keeps bed 1 without the caustic terms.
#if !defined(NATIVE_TERRAIN_PS)&&!defined(NATIVE_UNLIT)
  if(sknMaterial){c.rgb+=sknEmission;c0+=sknEmission;}
#endif
  float causticDepth=i.bedLiquid.y>0?0:i.bedLiquid.x;float3 bed=liquidBedTransmission(i.bedLiquid.x);
  [branch]if(causticDepth>0)bed*=1+liquidBedCaustics(i.shadowPosition.xy,causticDepth,float4(shadowDx.xy,shadowDy.xy))*smoothstep(.6,.85,normalize(i.worldNormal).z)*dot(saturate(nativeWorldSunDiffuse.rgb),float3(.2126,.7152,.0722));
  c.rgb*=bed;
#if !defined(NATIVE_TERRAIN_PS)
  // glTF unlit keeps authored colour and alpha before the ordinary fog stage.
  if(nativeSknInstance(i)!=0 && nativeSknInstances[nativeSknInstance(i)-1].sknEmission.w>0){
   c.rgb=t.rgb;c0=c.rgb/ max(bed,1e-6);filteredShadow=1;
  }
#endif
#ifndef NATIVE_LOW // Low: alpha-tested before the lighting (c.a is the same value)
  [branch]if(nativeAlphaTest!=0){if(!nativeCompare(c.a))discard;}
#endif
  float3 ambientShare=max(c.rgb-c0*bed,0);
  // Screen-space AO rebuilds its normal from depth, which is flat per
  // coarse terrain triangle: on steep faces that drew lines along the
  // triangle edges. Faces keep only part of the AO (terrain_cliff.h).
  [branch]if((i.terrainVertexFlags&16)!=0&&dot(i.terrainSlope,i.terrainSlope)>1e-8){float3 aoPlanes=terrainCliffPlaneWeights(normalize(i.terrainSlope));ambientShare*=lerp(1,terrainCliffAoKeep,aoPlanes.x+aoPlanes.y);}
  // Wall foot contact (terrain_cliff.h): a soft band along the whole foot,
  // from per-vertex rise and wall height, replaces the screen-space AO there
  // (whose horizons pile up into blobs at every riser corner).
  [branch]if((i.terrainVertexFlags&16)!=0&&!liquidSurfaceEnabled(i.terrainVertexFlags)){
)" R"(
   float contact=terrainFootContact(i.liquidFall.x,i.liquidDirections.x);
   c.rgb-=ambientShare*terrainFootContactDepth*contact;
   ambientShare*=1-contact;
  }
#if defined(NATIVE_TERRAIN_PS)||defined(NATIVE_LOW)
  // Land keeps its normal shading. Astral replaces only its authored share,
  // before the common fog chain, and contributes no ambient or traced light.
  [branch]if((i.terrainVertexFlags&16384u)!=0){
   float coverage=terrainAstralCoverage(i.terrainVertexFlags,i.liquidTileLocal);
   float3 astral=terrainAstralColor(i.worldPosition,fogEye.xyz,nativeTerrainClock.y*.055,
                                  i.liquidDirections.y,length(abs(positionDx.xy)+abs(positionDy.xy))*.075);
   c.rgb=lerp(c.rgb,astral,coverage);ambientShare*=1-coverage;
#ifdef NATIVE_RR_GUIDES
   guides.diffuse.rgb*=1-coverage;guides.specular.rgb*=1-coverage;
#endif
  }
#endif
  // World geometry (shadow mode 1 or 2: object_to_clip is the world camera)
  // measures fog and draw distance from its interpolated world position,
  // the point the pixel would unproject to; prepared vertices unproject.
  float3 fogPoint=i.worldPosition;
  [branch]if(i.shadowMode==0&&(fogStock.w>0||fogEye.w>.5))fogPoint=fogUnproject(i.p.xy*fogScreen.xy,i.p.z);
#ifdef NATIVE_RR_GUIDES
  float lightKept=dot(bed,1.0/3); // what reaches the eye of a light added to the lit colour (rt_direct_lights)
#endif
  if(nativeFogEnable!=0){
   // Caves: the exits' light in the fog (nativeCaveFogLight; 0 elsewhere),
   // and near an exit the border fog takes the exit glow's colour along
   // this view ray, so the ground strip blends into the glowing mouth.
   float3 caveFog=nativeCaveFogLight(fogPoint)+nativeCaveExitGlow(fogPoint);
   // The atmosphere first, then the border fog, then the draw-distance fade:
   // both of the last two end the world, so where either is complete the
   // pixel is exactly the sky behind (borderFogColorAt). Haze laid over fully
   // border-fogged land left it lighter than the sky beside it: a ghost slab
   // with a hard vertical edge where a neighbour map ends.
   float fogFactor=nativeFogFactor(i.s.a,i.p);
   // The stock sky dome (fog mode 2, renderer_flush.cpp native_batch_constants):
   // the full game fog colour at and below the eye horizon, rising smoothly to
   // its own vertex fog at 6 degrees of elevation (sin 6 = .1045), so every
   // end-of-world fog (game fog colour with the stock sky) fades up into the
   // sky with no line; the stock clouds take the same fade. 14 degrees covered
   // nearly all the sky the game camera shows (a flat fog sky, rejected).
   [branch]if(nativeFogEnable==2u)fogFactor=min(fogFactor,smoothstep(0.,.1045,normalize(fogUnproject(i.p.xy*fogScreen.xy,i.p.z)-fogEye.xyz).z));
   float kept=nativeAtmosphereCoreAt(c.rgb,i.p,saturate(nativeFogColor.rgb+caveFog),fogPoint,fogHazeColorFrame.w>.5);
   c.rgb=lerp(saturate(borderFogColorAt(fogPoint,nativeFogColor.rgb)+caveFog),c.rgb,fogFactor);
   float through=fogFactor*kept*nativeDrawFadeToAt(c.rgb,fogPoint,nativeFogColor.rgb);
   ambientShare*=through;
#ifdef NATIVE_RR_GUIDES
   lightKept*=through;
#endif
  }
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
  // Raw impostor view (impostor_debug_raw, w 3): the sampled impostor texel, unlit.
  [branch]if(impostor&&i.liquidFlow.w>2.5){c.rgb=impostorTexel.rgb;ambientShare=0;}
  // F10 fur_debug: 1 a wound layer (magenta), 2 a furry part's own colour
  // record (blue), 3 a wound layer's alpha as grey, opaque.
#ifdef NATIVE_FUR_DEBUG
  {const uint furDebug=(i.swaying>>24)&7u;
   [branch]if(furDebug!=0){c=furDebug==1u?float4(1,0,1,1):furDebug==2u?float4(0,0,1,1):float4(c.aaa,1);ambientShare=0;}}
#endif
#ifdef NATIVE_RR_GUIDES
  guides.diffuse.a=(nativeGuideLight.x&0xffffu)/65535.0;
  guides.specular.a=(nativeGuideLight.y&0x10000u)!=0?((nativeGuideLight.y&0x807fu)|(uint(saturate(lightKept)*255+.5)<<7))/65535.0:0;
#endif
  NativePixelResult result;
  c.a*=nativeHudOpacity;
  result.color=c;
  result.shadow=float4(filteredShadow,0,0,1);
  result.ambient=float4(ambientShare,c.a);
#ifdef NATIVE_TERRAIN_SLIM
  // A miss marks its whole 2x2 quad: later stages take screen derivatives
  // of material values (height, roughness), and a hit lane beside a miss
  // lane differentiated against the value the slim program never computed.
  // The full program redraws the quad (exact fine differences: any lane missed).
  {float lane=terrainSlimMiss?1:0;
   float spread=abs(ddx_fine(lane))+abs(ddy_fine(lane))+abs(ddx_fine(ddy_fine(lane)));
   if(spread>0)terrainSlimMiss=true;}
  // Every slim pixel keys the mask (terrainSlimKey, InterlockedMax without a
  // return: no order between draws or fragments needed, nothing waits on it).
  // A hit that kept out of the mask would let an earlier triangle's miss at
  // the same pixel and depth redraw over the single pass's last writer.
  InterlockedMax(terrainMissMask[uint2(i.p.xy)],terrainSlimKey(nativeTriangleBase+primitive,terrainSlimMiss));
  // A triangle's missed pixels all set the same bit: read first, so only
  // the first few of them contend for the atomic.
  [branch]if(terrainSlimMiss){
   terrainSlimListQuad(uint2(i.p.xy));
   uint t=nativeTriangleBase+primitive,address=(t>>5)*4,bit=1u<<(t&31u);
   [branch]if((terrainMissTriangleFlags.Load(address)&bit)==0)terrainMissTriangleFlags.InterlockedOr(address,bit);
  }
#endif
#if defined(NATIVE_TERRAIN_PS)&&defined(NATIVE_SHADOW_MASK)&&!defined(NATIVE_TERRAIN_SLIM)
  // terrain_slim 3: the slim pass's misses tinted red.
  [branch]if((nativeLiquidLayer&128u)!=0)result.color.rgb=lerp(result.color.rgb,float3(1,0,0),.3);
  // The second pass's pixels that are not this triangle's leave here, after
  // every screen difference (#243, above).
  if(slimForeign)discard;
#endif
  return result;}
#ifdef NATIVE_TERRAIN_SLIM
#undef nativePS
[earlydepthstencil]
NativePixelResult nativePS(NativeOutput i,uint primitive:SV_PrimitiveID,bool frontFace:SV_IsFrontFace NATIVE_GUIDE_OUT){
 NativePixelResult result=nativeSlimMaterialPS(i,primitive,frontFace NATIVE_GUIDE_PASS);
 // Pure Astral needs no cached material, but must own the pixel's hit key.
 // Keep this UAV write outside its early-return branch: FXC otherwise makes
 // the later slim miss-quad derivatives depend on the UAV (X3662, ps_5_0).
 // Discarded pixels never reach this write; the normal slim path is unchanged.
 [branch]if((nativeCombinerTail&1u)==0&&(i.terrainVertexFlags&16384u)!=0&&((i.terrainVertexFlags>>16)&15u)==15u)
  InterlockedMax(terrainMissMask[uint2(i.p.xy)],terrainSlimKey(nativeTriangleBase+primitive,false));
 return result;
}
#endif
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
 if(nativeFogEnable!=0){float3 before=c.rgb;float kept=nativeAtmosphereCoreAt(c.rgb,i.p,nativeFogColor.rgb,i.worldPosition,fogHazeColorFrame.w>.5);haze=haze*kept+c.rgb-before*kept;}
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
// Upscaler object motion in the material pass (the DLSS programming guide's
// velocity: written by the geometry where it moves on its own, the camera
// reprojection from depth everywhere else). Batches holding a posed or
// swaying record draw with nativeMotionWorldVS and the NATIVE_OBJECT_MOTION
// pixel variant, which also write the object motion targets 3 and 4 (motion
// in render pixels toward last frame, and the depth it belongs to);
// cameraMotionPS keeps it where that depth is still the visible one.
// World-vertex records (merged characters, posed figures) take the change of
// their prepared position since the previous frame's copy of the world
// vertices (same slot, same source vertex and part); swaying plant parts the
// sway at last frame's clock; other records keep the camera motion.
cbuffer NativeMotionConstants:register(b11){row_major float4x4 nativeMotionPreviousClip;float4 nativeMotionJitter;float4 nativeMotionClock;};
StructuredBuffer<NativeWorldVertex> nativePreviousWorldVertices:register(t46); // also the fur strands
#ifdef NATIVE_OBJECT_MOTION
NativeOutput nativeMotionWorldVS(uint vertexId:SV_VertexID){
 NativeOutput o=nativeVertex(vertexId,true);precise float4 position=o.p;o.p=position;
#ifdef NATIVE_INDEXED
 uint2 owner=nativeVertexOwners[vertexId];
 uint4 triangleRef=uint4(owner.x,owner.yyy);
#else
 uint4 triangleRef=nativeVisibleTriangles[nativeTriangleBase+vertexId/3];
#endif
 NativeInstance instance=loadNativeInstance(triangleRef.x);
 o.motionClip=0;
 [branch]if(instance.worldGeometry!=0){
  float3 previous=o.worldPosition;bool own=false;
  [branch]if((instance.worldGeometry&@WORLD_OBJECT_SPACE@)==0){
   uint preparedIndex=(vertexId%3)==0?triangleRef.y:((vertexId%3)==1?triangleRef.z:triangleRef.w);
   NativeWorldVertex now=nativeWorldVertices[preparedIndex],before=nativePreviousWorldVertices[preparedIndex];
   // (No whole copy is kept: only a previous vertex with this world
   // frame's stamp, nativeMotionClock.z, was rewritten this frame; any other has not moved.)
   if(before.specular==asuint(nativeMotionClock.z)&&before.sourcePosition==now.sourcePosition&&before.partNode==now.partNode)previous+=before.p.xyz-now.p.xyz;
   own=true;
  }
  [branch]if((instance.nativeFlags&0x200u)!=0&&nativeMotionClock.x!=nativeMotionClock.y&&treeSwayStrength(instance)>0){
   // The drawn position less the rest position is this frame's sway (nativeVertex adds
   // treeSwayOffset to the rest position and nothing else for these records), so only
   // last frame's sway is evaluated here.
   previous+=treeSwayOffsetAt(o.shadowPosition,instance,nativeMotionClock.x)-(o.worldPosition-o.shadowPosition);
   own=true;
  }
  if(own)o.motionClip=mul(nativeMotionPreviousClip,float4(previous,1));
 }
 return o;
}
struct NativeMotionPixelResult{float4 color:SV_Target0;float4 shadow:SV_Target1;float4 ambient:SV_Target2;float2 motion:SV_Target3;float motionDepth:SV_Target4;};
#undef nativePS
NativeMotionPixelResult nativePS(NativeOutput i,bool frontFace:SV_IsFrontFace NATIVE_GUIDE_OUT){
 NativePixelResult material=nativeMaterialPS(i,frontFace NATIVE_GUIDE_PASS);
 NativeMotionPixelResult r;r.color=material.color;r.shadow=material.shadow;r.ambient=material.ambient;
 // Depth 2 is never a world depth: the camera motion there.
 r.motion=0;r.motionDepth=2;
 float4 cameraClip=mul(nativeMotionPreviousClip,float4(i.worldPosition,1));
 [branch]if(i.motionClip.w>1e-6&&cameraClip.w>1e-6){
  float2 size=nativeMotionJitter.zw;
  float2 pixel=i.p.xy-nativeMotionJitter.xy;
  float2 objectPixel=float2(i.motionClip.x/i.motionClip.w*.5+.5,.5-i.motionClip.y/i.motionClip.w*.5)*size;
  float2 cameraPixel=float2(cameraClip.x/cameraClip.w*.5+.5,.5-cameraClip.y/cameraClip.w*.5)*size;
  // A reused slot whose old contents happen to match (same source vertex)
  // would give a jump across the screen: more than a tenth of the width
  // beyond the camera motion keeps the camera motion.
  if(length(objectPixel-cameraPixel)<=.1*size.x){r.motion=objectPixel-pixel;r.motionDepth=i.p.z;}
 }
 return r;
}
#endif
// Direct3D 11 (renderer_frame.cpp draw_dlss_object_motion): posed batches
// redraw through these stages after their material draw instead.
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
   if(before.specular==asuint(nativeMotionClock.z)&&before.sourcePosition==now.sourcePosition&&before.partNode==now.partNode)previous+=before.p.xyz-now.p.xyz;
  }
  // A swaying plant part: where the sway had it at last frame's clock.
  [branch]if((instance.nativeFlags&0x200u)!=0&&nativeMotionClock.x!=nativeMotionClock.y&&treeSwayStrength(instance)>0)
   previous+=treeSwayOffsetAt(o.shadowPosition,instance,nativeMotionClock.x)-treeSwayOffsetAt(o.shadowPosition,instance,nativeMotionClock.y);
  r.objectClip=mul(nativeMotionPreviousClip,float4(previous,1));
 }
 return r;
}
struct NativeMotionResult{float2 motion:SV_Target0;float depth:SV_Target1;};
NativeMotionResult nativeMotionPS(NativeMotionOutput i){
 NATIVE_REPORT_PIXEL(5u,i.textureReference.x,i.p)
 if(nativeLodDiscard(i.swaying,i.p))discard;
 // Records without a world position stay with the camera pass.
 if(i.cameraClip.w<=1e-6||i.objectClip.w<=1e-6)discard;
 [branch]if(nativeAlphaTest!=0){
  float4 t=float4(1,1,1,1);
  if(nativeTextured!=0)t=NATIVE_SAMPLE(i.textureReference,i.uv);
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
  const std::string astral_marker="@TERRAIN_ASTRAL_SHADER@";
  text.replace(text.find(astral_marker),astral_marker.size(),kTerrainAstralShader);
  const std::string liquid_marker="@LIQUID_SURFACE@";
  text.replace(text.find(liquid_marker),liquid_marker.size(),std::string(kAtmosphereShader)+kSkyNativeShader+std::string(features.liquid_surface));
  const std::string glow_marker="@CAVE_EXIT_GLOW@";
  text.replace(text.find(glow_marker),glow_marker.size(),kCaveExitGlowHlsl);
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
  const std::string vertex_opacity_marker="@WORLD_VERTEX_OPACITY@";
  for(auto at=text.find(vertex_opacity_marker);at!=std::string::npos;at=text.find(vertex_opacity_marker,at))
   text.replace(at,vertex_opacity_marker.size(),std::to_string(native_world_vertex_opacity)+"u");
  const std::string lod_marker="@WORLD_LOD@";
  for(auto at=text.find(lod_marker);at!=std::string::npos;at=text.find(lod_marker,at))
   text.replace(at,lod_marker.size(),std::to_string(native_world_lod)+"u");
  const std::string impostor_marker="@WORLD_IMPOSTOR@";
  for(auto at=text.find(impostor_marker);at!=std::string::npos;at=text.find(impostor_marker,at))
   text.replace(at,impostor_marker.size(),std::to_string(native_world_impostor)+"u");
  const std::string follower_marker="@WORLD_IMPOSTOR_FOLLOWER@";
  text.replace(text.find(follower_marker),follower_marker.size(),std::to_string(native_world_impostor_follower)+"u");
  const std::string frames_marker="@IMPOSTOR_FRAMES@";
  const std::string gltf_marker="@GLTF_ANIMATION@";
  text.replace(text.find(gltf_marker),gltf_marker.size(),gltf::kAnimationShader);
  text.replace(text.find(frames_marker),frames_marker.size(),std::string(kImpostorFrameHlsl)+
   "static const uint kImpostorMips="+std::to_string(kImpostorMips)+";\n");
  const std::string plane_marker="@SHADOW_PLANE_COMPARE@";
  text.replace(text.find(plane_marker),plane_marker.size(),kShadowPlaneCompareShader);
  const std::string terrain_marker="@TERRAIN_MATERIAL@";
  text.replace(text.find(terrain_marker),terrain_marker.size(),kTerrainMaterialShader);
  const std::string cliff_marker="@TERRAIN_CLIFF@";
  text.replace(text.find(cliff_marker),cliff_marker.size(),std::string(features.terrain_cliff));
  const std::string blend_marker="@TERRAIN_TILE_BLEND@";
  // Terrain 2.0 (terrain2.h) follows the blend whose cells and reads it uses,
  // only in the programs that draw or bake 2.0 ground (the terrain pixel
  // variants and the cache bake, NATIVE_TERRAIN_PS) or show it on snow piles
  // (ground cover GC_PS_KIND 3): every other program preprocesses it away,
  // so a terrain edit leaves them in the build's shader store (shader_embed).
  text.replace(text.find(blend_marker),blend_marker.size(),std::string(features.terrain_tile_blend)+
   "\n#if defined(NATIVE_TERRAIN_PS)||GC_PS_KIND==3\n"+kTerrain2Shader+kTerrain2CacheShader+kTerrainFaceCacheShader+"\n#endif\n");
#ifdef EID3D11_EMBEDDED
  embedded_triangle_shader(text);
#endif
  return text;
}

} // namespace owned
