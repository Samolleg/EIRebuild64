// Native packets on the x64 host's D3D12 executor (renderer_backend=d3d12):
// the native queue's own records (layout, batches, work, arguments, instance
// rows, b0 constants) drawn through Source/d3d12_frame.h instead of the D3D11
// context. Interface and world batches use native resource bindings.
// Every function here runs only with Renderer::d3d12_ set.
//
// Binding: the shared native HLSL keeps its D3D11 registers; each dispatch
// and draw sets its slots on the executor, which copies them into ring
// tables (a thin per-flush translation, not a D3D11 emulation layer).
#include "renderer.h"
#include "gamma_shader_programs.h"
#include "embedded_figure_shader.h"
#include "graphics_settings.h"
#include "renderer_internal.h"
#include "world_texture_sampler.h"
#include "contact_blend.h"
#include "terrain2.h"
#include "texture_mips.h"
#include "owned_asset_loader.h"
#include "owned_terrain_decode.h"
#include "neighbour_maps.h"
#include "neighbour_piece.h"
#include "ground_cover.h"
#include "ground_cover_neighbour.h"
#include "native_instance_flags.h"
#include "native_prebuilt.h"
#include "owned_resource_runtime.h"
#include "retained_static_scene.h"
#include "shadow_foliage.h"
#include "ambient_occlusion.h"
#include "world_fog.h"
#include "visual_highlight.h"
#include "dlss_frame.h"
#include "x64_remote_link.h"
#include <future>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <chrono>
#include <thread>

namespace owned {

// D3D11 GenerateMips on D3D12 (D3D12Frame::generate_mips): each level the 2x2
// box of the one above, clamped at odd edges. Only the x64 host's DXIL table
// holds it (shader_embed --dxil-sources); the D3D11 table never sees it.
namespace {
constexpr char kD3D12SurfaceEffectShader[] = R"hlsl(
Texture2D<float4> sourceImage : register(t0);
Texture2D<float4> destinationImage : register(t1);
SamplerState linearClamp : register(s0);
cbuffer SurfaceEffect : register(b0) { float4 viewport; uint fade; float3 padding; };
float4 surfaceEffectVS(uint id : SV_VertexID) : SV_Position {
 float2 uv = float2((id << 1) & 2, id & 2);
 return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}
float4 surfaceEffectPS(float4 position : SV_Position) : SV_Target {
 float2 uv = (position.xy - viewport.xy) / viewport.zw;
 if (any(uv < 0) || any(uv >= 1)) return destinationImage.Load(int3(position.xy, 0));
 float4 pixel = sourceImage.SampleLevel(linearClamp, uv, 0);
 if (fade) {
  // RetailModalFade integer RGB32 rule, including its dimming coefficient.
  uint3 rgb = (uint3)round(saturate(pixel.rgb) * 255.0);
  uint gray = (2u * rgb.r + 4u * rgb.g + rgb.b) >> 4u;
  pixel.rgb = float3(gray, gray, gray) / 255.0;
 }
 return pixel;
}
)hlsl";
const ShaderProgram kD3D12SurfaceEffectVS{{kD3D12SurfaceEffectShader,sizeof(kD3D12SurfaceEffectShader)-1},
 nullptr,nullptr,"surfaceEffectVS","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3};
const ShaderProgram kD3D12SurfaceEffectPS{{kD3D12SurfaceEffectShader,sizeof(kD3D12SurfaceEffectShader)-1},
 nullptr,nullptr,"surfaceEffectPS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3};
// Captured BGRA content stays on the GPU. Sample its native view into an
// RGBA UAV (BGRA has no typed UAV store), preserving logical channels.
constexpr char kD3D12SurfaceCopyShader[] =
"Texture2D<float4> surfaceSource:register(t0);RWTexture2D<float4> surfaceTarget:register(u0);\n"
"[numthreads(8,8,1)]void surfaceCopyCS(uint3 id:SV_DispatchThreadID){\n"
" uint w,h;surfaceTarget.GetDimensions(w,h);if(id.x<w&&id.y<h)surfaceTarget[id.xy]=surfaceSource.Load(int3(id.xy,0));}\n";
const ShaderProgram kD3D12SurfaceCopyProgram{{kD3D12SurfaceCopyShader,sizeof(kD3D12SurfaceCopyShader)-1},
 nullptr,nullptr,"surfaceCopyCS","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3};
constexpr char kD3D12MipShader[] =
"Texture2D<float4> mipSource:register(t0);RWTexture2D<float4> mipTarget:register(u0);\n"
"[numthreads(8,8,1)]void mipBoxCS(uint3 id:SV_DispatchThreadID){\n"
" uint w,h,sw,sh;mipTarget.GetDimensions(w,h);mipSource.GetDimensions(sw,sh);if(id.x>=w||id.y>=h)return;\n"
" uint2 a=min(id.xy*2,uint2(sw-1,sh-1)),b=min(id.xy*2+1,uint2(sw-1,sh-1));\n"
" mipTarget[id.xy]=(mipSource[a]+mipSource[uint2(b.x,a.y)]+mipSource[uint2(a.x,b.y)]+mipSource[b])*.25;}\n";
const ShaderProgram kD3D12MipProgram{{kD3D12MipShader, sizeof(kD3D12MipShader) - 1}, nullptr, nullptr, "mipBoxCS", "cs_5_0",
                                     D3DCOMPILE_OPTIMIZATION_LEVEL3};
// The same for one slice of an array (its views hold that slice alone).
constexpr char kD3D12MipArrayShader[] =
"Texture2DArray<float4> mipSource:register(t0);RWTexture2DArray<float4> mipTarget:register(u0);\n"
"[numthreads(8,8,1)]void mipBoxArrayCS(uint3 id:SV_DispatchThreadID){\n"
" uint w,h,n,sw,sh,sn;mipTarget.GetDimensions(w,h,n);mipSource.GetDimensions(sw,sh,sn);if(id.x>=w||id.y>=h)return;\n"
" uint2 a=min(id.xy*2,uint2(sw-1,sh-1)),b=min(id.xy*2+1,uint2(sw-1,sh-1));\n"
" mipTarget[uint3(id.xy,0)]=(mipSource[uint3(a,0)]+mipSource[uint3(b.x,a.y,0)]+mipSource[uint3(a.x,b.y,0)]+mipSource[uint3(b,0)])*.25;}\n";
const ShaderProgram kD3D12MipArrayProgram{{kD3D12MipArrayShader, sizeof(kD3D12MipArrayShader) - 1}, nullptr, nullptr, "mipBoxArrayCS",
                                          "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3};
// The same for every slice of the views at once, dispatch z a slice
// (generate_mips with a slice count: the crown impostor atlas's new slices).
constexpr char kD3D12MipArrayRangeShader[] =
"Texture2DArray<float4> mipSource:register(t0);RWTexture2DArray<float4> mipTarget:register(u0);\n"
"[numthreads(8,8,1)]void mipBoxArrayRangeCS(uint3 id:SV_DispatchThreadID){\n"
" uint w,h,n,sw,sh,sn;mipTarget.GetDimensions(w,h,n);mipSource.GetDimensions(sw,sh,sn);if(id.x>=w||id.y>=h||id.z>=n)return;\n"
" uint2 a=min(id.xy*2,uint2(sw-1,sh-1)),b=min(id.xy*2+1,uint2(sw-1,sh-1));\n"
" mipTarget[id]=(mipSource[uint3(a,id.z)]+mipSource[uint3(b.x,a.y,id.z)]+mipSource[uint3(a.x,b.y,id.z)]+mipSource[uint3(b,id.z)])*.25;}\n";
const ShaderProgram kD3D12MipArrayRangeProgram{{kD3D12MipArrayRangeShader, sizeof(kD3D12MipArrayRangeShader) - 1}, nullptr, nullptr,
                                               "mipBoxArrayRangeCS", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3};
// A unit's composed atlas chain on the GPU (unit_atlas_mips_d3d12), the
// alpha-coverage rule of texture_mips.h: level 0 copied with its alpha facts
// (texels 255 / partial, words 0 and 1), the box levels, each level's alpha
// histogram (256 words at 16 + level * 256), one solve of each level's scale
// (float words at 4112 + level), then each level's alpha scaled.
constexpr char kUnitMipShader[] = R"hlsl(
Texture2D<float4> umSource:register(t0);
RWTexture2D<float4> umLevel:register(u0);
RWByteAddressBuffer umStats:register(u1);
cbuffer UnitMips:register(b0){uint umIndex;uint umWidth;uint umHeight;uint umLevels;};
groupshared uint umBins[256];
[numthreads(8,8,1)]void unitMipCopyCS(uint3 id:SV_DispatchThreadID,uint gi:SV_GroupIndex){
 if(gi<2)umBins[gi]=0;GroupMemoryBarrierWithGroupSync();
 if(id.x<umWidth&&id.y<umHeight){
  const float4 c=umSource[id.xy];umLevel[id.xy]=c;
  const uint a=uint(round(saturate(c.a)*255));
  if(a==255)InterlockedAdd(umBins[0],1);else if(a!=0)InterlockedAdd(umBins[1],1);
 }
 GroupMemoryBarrierWithGroupSync();
 if(gi<2&&umBins[gi])umStats.InterlockedAdd(gi*4,umBins[gi]);
}
[numthreads(8,8,1)]void unitMipHistogramCS(uint3 id:SV_DispatchThreadID,uint gi:SV_GroupIndex){
 uint w,h;umLevel.GetDimensions(w,h);
 for(uint b=gi;b<256;b+=64)umBins[b]=0;GroupMemoryBarrierWithGroupSync();
 if(id.x<w&&id.y<h)InterlockedAdd(umBins[uint(round(saturate(umLevel[id.xy].a)*255))],1);
 GroupMemoryBarrierWithGroupSync();
 for(uint k=gi;k<256;k+=64)if(umBins[k])umStats.InterlockedAdd((16+umIndex*256+k)*4,umBins[k]);
}
[numthreads(16,1,1)]void unitMipSolveCS(uint3 id:SV_DispatchThreadID){
 const uint level=id.x;if(level==0||level>=umLevels)return;
 const uint solid=umStats.Load(0),partial=umStats.Load(4),total=umWidth*umHeight;
 const uint count=max(1u,umWidth>>level)*max(1u,umHeight>>level);
 float scale=1;
 if(partial==0&&solid>0&&solid<total&&count>=16){
  const float target=float(solid)/float(total)*float(count);
  uint passing=0,edge=0;
  for(uint b=255;b>=1;--b){passing+=umStats.Load((16+level*256+b)*4);if(float(passing)>=target){edge=b;break;}}
  scale=edge>=201?1:edge?min(8.,200.5/edge):8;
 }
 umStats.Store((4112+level)*4,asuint(scale));
}
[numthreads(8,8,1)]void unitMipScaleCS(uint3 id:SV_DispatchThreadID){
 uint w,h;umLevel.GetDimensions(w,h);
 const float scale=asfloat(umStats.Load((4112+umIndex)*4));
 if(scale==1||id.x>=w||id.y>=h)return;
 float4 c=umLevel[id.xy];c.a=min(1,c.a*scale);umLevel[id.xy]=c;
}
)hlsl";
const ShaderProgram kUnitMipCopyProgram{{kUnitMipShader,sizeof(kUnitMipShader)-1},nullptr,nullptr,"unitMipCopyCS","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3};
const ShaderProgram kUnitMipHistogramProgram{{kUnitMipShader,sizeof(kUnitMipShader)-1},nullptr,nullptr,"unitMipHistogramCS","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3};
const ShaderProgram kUnitMipSolveProgram{{kUnitMipShader,sizeof(kUnitMipShader)-1},nullptr,nullptr,"unitMipSolveCS","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3};
const ShaderProgram kUnitMipScaleProgram{{kUnitMipShader,sizeof(kUnitMipShader)-1},nullptr,nullptr,"unitMipScaleCS","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3};
// rt_debug_view (D3D12 only: these programs are in the host's DXIL table
// alone, FXC never compiles them). One hard ray a pixel from the world depth's
// position toward the sun through the ray tracing scene (t1), with an inline
// ray query. rtControl.x 1: lit or shadowed. A surface turned away from the
// sun (the depth's own facet, oriented toward the eye) is shadowed without a
// ray: its ray starts inside the ground it lies on and can only leave through
// an underside (mode 2 near black). The scene holds the untessellated
// terrain, and the drawn surface (tessellated, or off it by the depth's
// precision) can lie under it; a ray toward a sun above the horizon can enter
// solid ground only through its top side, so hits on the underside are
// culled instead of biasing the origin. 2: the nearest terrain hit's side
// instead (instance mask 1: the active and neighbour terrain alone; with the
// two-sided statics and cards in it, red and blue named their faces, not a
// receiver under the ground), nothing culled, from the depth's own position,
// not the rest position the traced mask uses: red on tessellated cliffs is
// the drawn surface under the untessellated scene and is expected here;
// rt_debug_view 4 is the check of the mask's own receivers
// (blue the top side: terrain shadow; red the underside: a
// sunward receiver under the scene's surface, which mode 1's cull lets the sun
// reach; grey no hit). 3: where a traced ray is blocked by terrain, whether
// the raster sun-shadow mask of this frame (t2, x below .9) shadows the pixel
// too: green agrees, magenta is a traced terrain shadow the sun maps do not
// have (wrong direction or reach, or past the maps' range), yellow no mask.
constexpr char kRaytracingDebugShader[] =
"cbuffer RtDebugConstants:register(b1){row_major float4x4 rtClipToWorld;float4 rtSun;float4 rtSize;uint4 rtControl;};\n"
"Texture2D<float> rtDepth:register(t0);\n"
"RaytracingAccelerationStructure rtScene:register(t1);\n"
"Texture2D<float2> rtRasterMask:register(t2);\n"
"float4 rtDebugVS(uint id:SV_VertexID):SV_Position{float2 uv=float2((id<<1)&2,id&2);return float4(uv.x*2-1,1-uv.y*2,0,1);}\n"
"float4 rtDebugPS(float4 position:SV_Position):SV_Target{\n"
" int2 c=int2(floor(position.xy));\n"
" float z=rtDepth.Load(int3(c,0));\n"
" float2 uv=(float2(c)+.5)*rtSize.zw;\n"
" float4 p=mul(rtClipToWorld,float4(uv.x*2-1,1-uv.y*2,z,1));\n"
" float3 world=p.xyz/p.w;\n"
" // The depth's own facet, oriented toward the eye (the pixel's near-plane point).\n"
" float4 eyeSide=mul(rtClipToWorld,float4(uv.x*2-1,1-uv.y*2,0,1));\n"
" float3 facet=cross(ddx(world),ddy(world));\n"
" facet=dot(facet,facet)>1e-20?normalize(facet):float3(0,0,1);\n"
" if(dot(facet,world-eyeSide.xyz/eyeSide.w)>0)facet=-facet;\n"
" float facing=dot(facet,rtSun.xyz);\n"
" float shade=.45+.55*saturate(facing);\n"
" if(z>=1)return float4(.02,.03,.06,1);\n"
" if(rtSun.z<=1e-3)return float4(.2,.2,.2,1);\n"
" RayDesc ray;ray.Origin=world;ray.Direction=rtSun.xyz;ray.TMin=.02;ray.TMax=rtSun.w;\n"
" if(rtControl.x==2){\n"
"  if(facing<=0)return float4(.03,.03,.03,1);\n"
"  RayQuery<RAY_FLAG_FORCE_OPAQUE|RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> nearest;\n"
"  nearest.TraceRayInline(rtScene,RAY_FLAG_NONE,0x01,ray);\n"
"  nearest.Proceed();\n"
"  if(nearest.CommittedStatus()!=COMMITTED_TRIANGLE_HIT)return float4(shade,shade,shade,1);\n"
"  return nearest.CommittedTriangleFrontFace()?float4(.05,.2,1,1):float4(1,.1,.05,1);\n"
" }\n"
" RayQuery<RAY_FLAG_FORCE_OPAQUE|RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES|RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH|RAY_FLAG_CULL_BACK_FACING_TRIANGLES> any;\n"
" if(facing<=0)return float4(.05,.07,.14,1);\n"
" any.TraceRayInline(rtScene,RAY_FLAG_NONE,0xFF,ray);\n"
" any.Proceed();\n"
" bool blocked=any.CommittedStatus()==COMMITTED_TRIANGLE_HIT;\n"
" if(rtControl.x==3){\n"
"  if(!blocked)return float4(shade,shade,shade,1);\n"
"  if(rtControl.y==0)return float4(1,.9,0,1);\n"
"  return rtRasterMask.Load(int3(c,0)).x<.9?float4(.05,.8,.1,1):float4(1,0,1,1);\n"
" }\n"
" return blocked?float4(.05,.07,.14,1):float4(float3(1,.93,.75)*shade,1);\n"
"}\n";
const ShaderProgram kRaytracingDebugVertex{{kRaytracingDebugShader, sizeof(kRaytracingDebugShader) - 1}, nullptr, nullptr, "rtDebugVS",
                                           "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3};
const ShaderProgram kRaytracingDebugPixel{{kRaytracingDebugShader, sizeof(kRaytracingDebugShader) - 1}, nullptr, nullptr, "rtDebugPS",
                                          "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3};
// The static records of the ray tracing scene, repacked (update_raytracing_statics_d3d12):
// per job (one object-space record) its components' world positions at rest
// and texture coordinates (native_shader.h nativeObjectSpaceVertex: the
// packed position groups, the 8-shape blend, the record's transform; no
// sway), 20 bytes a vertex, and its triangles' component indices, both at
// the job's place in the scene's arenas. One dispatch covers a run of jobs
// on one geometry page (t60): thread n belongs to the last job whose first
// thread is not above n, and writes that job's vertex n and triangle n.
struct RaytracingRepackJob{
 float row[3][4];                 // object to world, rows 0-2
 std::uint32_t position_offset,component_offset,uv_offset,index_offset;
 std::uint32_t shape_count,position_groups,component_count,triangle_count;
 float shape[3];std::uint32_t vertex_base;
 std::uint32_t triangle_base;float uv_shift[2];std::uint32_t first_thread;
 // Terrain records: their terrain vertices (120 bytes each, flags at 24);
 // UINT32_MAX: none. A triangle with a corner that is not an opaque base
 // tile vertex (flag 16) is written degenerate.
 // padding[0]: a posed record's first world vertex (UINT32_MAX: none); its
 // positions and texture coordinates are read from the world vertices.
 std::uint32_t terrain_vertex_offset,padding[3];
};
// The static caster store's probe (update_raytracing_statics_d3d12): one
// word a candidate, 1 when the GPU's world state of its range holds its
// generation (the store bake's prepare pass wrote its world vertices).
constexpr char kRaytracingProbeShader[] =
"struct RtProbe{uint state;uint2 generation;uint probePadding;};\n"
"struct RtWorldState{uint2 geometry;uint2 pose;uint preparations;uint components;};\n"
"cbuffer RtProbeConstants:register(b12){uint rtProbeCount;uint3 rtProbePadding;};\n"
"StructuredBuffer<RtProbe> rtProbes:register(t40);\n"
"StructuredBuffer<RtWorldState> rtWorldStates:register(t60);\n"
"RWByteAddressBuffer rtProbeOutput:register(u0);\n"
"[numthreads(64,1,1)]void rtProbeCS(uint3 id:SV_DispatchThreadID){\n"
" if(id.x>=rtProbeCount)return;\n"
" RtProbe probe=rtProbes[id.x];\n"
" uint current=0;\n"
" if(probe.state!=0xffffffffu){RtWorldState state=rtWorldStates[probe.state];current=all(state.geometry==probe.generation)?1u:0u;}\n"
" rtProbeOutput.Store(id.x*4,current);\n"
"}\n";
const ShaderProgram kRaytracingProbe{{kRaytracingProbeShader, sizeof(kRaytracingProbeShader) - 1}, nullptr, nullptr, "rtProbeCS",
                                     "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3};
static_assert(sizeof(RaytracingRepackJob)==128);
constexpr char kRaytracingRepackShader[] =
"struct RtJob{float4 row0;float4 row1;float4 row2;uint positionOffset;uint componentOffset;uint uvOffset;uint indexOffset;\n"
" uint shapeCount;uint positionGroups;uint componentCount;uint triangleCount;float3 shape;uint vertexBase;\n"
" uint triangleBase;float2 uvShift;uint firstThread;uint terrainVertexOffset;uint worldBase;uint2 jobPadding;};\n"
"struct RtWorldVertex{float4 p;float4 normal;float2 uv;float2 undeformed;uint4 tail;};\n"
"StructuredBuffer<RtWorldVertex> rtWorld:register(t65);\n"
"cbuffer RtRepack:register(b12){uint rtJobFirst;uint rtJobCount;uint rtThreads;uint rtRepackPadding;};\n"
"StructuredBuffer<RtJob> rtJobs:register(t40);\n"
"ByteAddressBuffer rtPage:register(t60);\n"
"RWByteAddressBuffer rtVertexOutput:register(u0);\n"
"RWByteAddressBuffer rtIndexOutput:register(u1);\n"
"float3 rtPacked(uint byteBase,uint flatIndex){uint group=byteBase+(flatIndex>>2)*64;uint lane=(flatIndex&3)*4;\n"
" return float3(asfloat(rtPage.Load(group+lane)),asfloat(rtPage.Load(group+16+lane)),asfloat(rtPage.Load(group+32+lane)));}\n"
"float3 rtBlend(float3 a,float3 b,float t){return a+(b-a)*t;}\n"
"[numthreads(64,1,1)]void rtRepackCS(uint3 id:SV_DispatchThreadID){\n"
" uint thread=id.x;\n"
" if(thread>=rtThreads||rtJobCount==0)return;\n"
" uint low=0,high=rtJobCount;\n"
" while(high-low>1){uint middle=(low+high)>>1;if(rtJobs[rtJobFirst+middle].firstThread<=thread)low=middle;else high=middle;}\n"
" RtJob job=rtJobs[rtJobFirst+low];\n"
" uint local=thread-job.firstThread;\n"
" if(local<job.componentCount){\n"
"  uint4 component=rtPage.Load4(job.componentOffset+local*16);\n"
"  float3 position;\n"
"  if(job.shapeCount!=8)position=rtPacked(job.positionOffset,component.x);\n"
"  else{\n"
"   uint stride=job.positionGroups*64;\n"
"   float3 v[8];\n"
"   for(uint shape=0;shape<8;shape++)v[shape]=rtPacked(job.positionOffset+shape*stride,component.x);\n"
"   float3 lowShape=rtBlend(rtBlend(v[0],v[1],job.shape.x),rtBlend(v[2],v[3],job.shape.x),job.shape.y);\n"
"   float3 highShape=rtBlend(rtBlend(v[4],v[5],job.shape.x),rtBlend(v[6],v[7],job.shape.x),job.shape.y);\n"
"   position=rtBlend(lowShape,highShape,job.shape.z);\n"
"  }\n"
"  float4 p=float4(position,1);\n"
"  float3 world=float3(dot(job.row0,p),dot(job.row1,p),dot(job.row2,p));\n"
"  float2 uv=asfloat(rtPage.Load2(job.uvOffset+component.z*8))+job.uvShift;\n"
"  if(job.worldBase!=0xffffffffu){RtWorldVertex posed=rtWorld[job.worldBase+local];world=posed.p.xyz;uv=posed.uv+job.uvShift;}\n"
"  uint at=(job.vertexBase+local)*20;\n"
"  rtVertexOutput.Store3(at,asuint(world));rtVertexOutput.Store2(at+12,asuint(uv));\n"
" }\n"
" if(local<job.triangleCount){\n"
"  uint3 corner=min(rtPage.Load3(job.indexOffset+local*12),job.componentCount-1);\n"
"  if(job.terrainVertexOffset!=0xffffffffu){\n"
"   uint flags=rtPage.Load(job.terrainVertexOffset+rtPage.Load(job.componentOffset+corner.x*16)*120+24)&\n"
"    rtPage.Load(job.terrainVertexOffset+rtPage.Load(job.componentOffset+corner.y*16)*120+24)&\n"
"    rtPage.Load(job.terrainVertexOffset+rtPage.Load(job.componentOffset+corner.z*16)*120+24);\n"
"   if((flags&16u)==0)corner=uint3(0,0,0);\n"
"  }\n"
"  rtIndexOutput.Store3((job.triangleBase+local)*12,corner);\n"
" }\n"
"}\n";
const ShaderProgram kRaytracingRepack{{kRaytracingRepackShader, sizeof(kRaytracingRepackShader) - 1}, nullptr, nullptr, "rtRepackCS",
                                      "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3};
// Stage 2b: shading data at ray hits (D3D12 only). rtTrace returns the
// nearest hit of a ray through the scene (cut-out cards by their alpha test,
// as the sun rays) with what a bounce shades it with: position, the
// triangle's normal turned toward the ray, albedo, emissive and the material
// profile's bindless index (0: none). By the instance id: 0x800000 the active
// map's terrain (positions and indices kept at t96/t97; albedo from the
// terrain 2.0 material cache's last, whole-world level by world X/Y, the
// finished flat-ground colour; a constant where the cache is off or that
// level is not baked there), 0x400000 the units (rtFigureShade by geometry;
// their mesh's index list is not bound, so the normal faces the ray and the
// albedo is their texture's smallest mip, its mean colour), else a static
// record (rtShade beside rtRecords; kind 1, neighbour terrain, takes the
// terrain albedo). rtLit: that hit lit by the ambient light, the sun (one
// shadow ray) and its grid cell's lamps (unshadowed), plus its emissive.
constexpr char kRaytracingHitShader[] =
"RaytracingAccelerationStructure rtScene:register(t94);\n"
"ByteAddressBuffer rtVertices:register(t95);\n"
"ByteAddressBuffer rtTerrainVertices:register(t96);\n"
"ByteAddressBuffer rtTerrainIndices:register(t97);\n"
"ByteAddressBuffer rtIndices:register(t99);\n"
"ByteAddressBuffer rtRecords:register(t113);\n"
"ByteAddressBuffer rtShade:register(t114);\n"
"ByteAddressBuffer rtFigureShade:register(t115);\n"
"Texture2D<float4> rtTerrainCoarse:register(t76);\n"
"StructuredBuffer<float4> rtTerrainParams:register(t77);\n"
"// (the water trace of rt_reflections runs inside the world flush, whose batches own s0: its program takes s14)\n"
"#ifndef RT_CARD_SAMPLER\n"
"#define RT_CARD_SAMPLER s0\n"
"#endif\n"
"SamplerState rtCardSampler:register(RT_CARD_SAMPLER);\n"
"SamplerState rtTerrainSampler:register(s15);\n"
"cbuffer RtPointLightConstants:register(b5){uint rtLightCount;uint2 rtLightGridSize;uint rtLightGlowCount;float2 rtLightGridOrigin;float rtLightGridInverseCell;float rtLightGridTail;\n"
" float3 rtCaveOrigin;float rtCaveStrength;float3 rtCaveFineSize;float rtCaveTail;float3 rtCaveWorldOrigin;float rtCaveFineInverseCell;\n"
" float3 rtCaveWorldSize;float rtCaveWorldInverseCell;float3 rtCaveTexelScale;float rtCaveFineCell;};\n"
"struct RtPointLight{float4 centerInverseRadiusSquared;float4 color;float4 shadowCenter;float4 shadowSlots;};\n"
"StructuredBuffer<uint> rtLightGrid:register(t17);\n"
"StructuredBuffer<RtPointLight> rtLights:register(t18);\n"
"static const float3 rtTerrainUnknown=float3(.30,.27,.20);\n"
"struct RtHit{bool hit;float t;float3 position;float3 normal;float3 albedo;float3 emissive;uint profile;uint kind;float2 uv;bool under;};\n"
"float rtCardLod(float t){return clamp(log2(max(t,1e-3)/20),0,3);}\n"
"bool rtCardOpaque(uint id,uint geometry,uint primitive,float2 b,float t){\n"
" uint4 record=rtRecords.Load4(32+(id+geometry)*16);\n"
" uint3 corner=rtIndices.Load3((record.y+primitive)*12);\n"
" float2 uv=asfloat(rtVertices.Load2((record.x+corner.x)*20+12))*(1-b.x-b.y)+\n"
"           asfloat(rtVertices.Load2((record.x+corner.y)*20+12))*b.x+\n"
"           asfloat(rtVertices.Load2((record.x+corner.z)*20+12))*b.y;\n"
" Texture2D card=ResourceDescriptorHeap[NonUniformResourceIndex(record.z)];\n"
" return card.SampleLevel(rtCardSampler,uv,rtCardLod(t)).a>=asfloat(record.w);\n"
"}\n"
"float3 rtTerrainAlbedo(float2 xy){\n"
" float4 head=rtTerrainParams[0];\n"
" if(head.x<.5)return rtTerrainUnknown;\n"
" uint l=uint(head.w);\n"
" float4 valid=rtTerrainParams[2+4*l],window=rtTerrainParams[3+4*l],frame=rtTerrainParams[4+4*l],place=rtTerrainParams[5+4*l];\n"
" float2 t=xy*frame.z-frame.xy;\n"
" if(frame.w>=0||any(t<valid.xy)||any(t>valid.zw))return rtTerrainUnknown;\n"
" return rtTerrainCoarse.SampleLevel(rtTerrainSampler,(place.xy+clamp(t-window.xy,.5,window.zw-window.xy-.5))*place.zw,0).rgb;\n"
"}\n"
"float3 rtShadeEmissive(uint word){return float3(word&1023u,(word>>10)&1023u,(word>>20)&1023u)/1023.0;}\n"
"float3 rtShadeDiffuse(uint word){return float3(word&255u,(word>>8)&255u,(word>>16)&255u)/255.0;}\n"
"// flags: RAY_FLAG_CULL_BACK_FACING_TRIANGLES for a ray that starts on a drawn surface (the sun rays' rule:\n"
"// the terrain is one-sided, so a receiver the tessellation drew under the scene's flat terrain does not\n"
"// hit the underside of the ground it lies on; objects are two-sided by their instance flag).\n"
"RtHit rtTrace(float3 origin,float3 direction,float nearest,float farthest,uint flags){\n"
" RayDesc ray;ray.Origin=origin;ray.Direction=direction;ray.TMin=nearest;ray.TMax=farthest;\n"
" RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;\n"
" query.TraceRayInline(rtScene,flags,0xFF,ray);\n"
" while(query.Proceed()){\n"
"  if(query.CandidateType()!=CANDIDATE_NON_OPAQUE_TRIANGLE)continue;\n"
"  if(rtCardOpaque(query.CandidateInstanceID(),query.CandidateGeometryIndex(),query.CandidatePrimitiveIndex(),\n"
"   query.CandidateTriangleBarycentrics(),query.CandidateTriangleRayT()))query.CommitNonOpaqueTriangleHit();\n"
" }\n"
" RtHit h=(RtHit)0;\n"
" if(query.CommittedStatus()!=COMMITTED_TRIANGLE_HIT)return h;\n"
" h.hit=true;h.t=query.CommittedRayT();h.position=origin+direction*h.t;h.normal=-direction;\n"
" uint id=query.CommittedInstanceID(),geometry=query.CommittedGeometryIndex(),primitive=query.CommittedPrimitiveIndex();\n"
" float2 b=query.CommittedTriangleBarycentrics();\n"
" float3 p0=0,p1=0,p2=0;bool corners=true;\n"
" if(id==0x800000u){\n"
"  uint3 corner=rtTerrainIndices.Load3(primitive*12);\n"
"  p0=asfloat(rtTerrainVertices.Load3(corner.x*12));p1=asfloat(rtTerrainVertices.Load3(corner.y*12));p2=asfloat(rtTerrainVertices.Load3(corner.z*12));\n"
"  h.kind=1;h.albedo=rtTerrainAlbedo(h.position.xy);\n"
" }else if(id==0x400000u){\n"
"  uint4 shade=rtFigureShade.Load4(geometry*16);\n"
"  corners=false;h.kind=2;h.profile=shade.y;h.emissive=rtShadeEmissive(shade.z);\n"
"  Texture2D skin=ResourceDescriptorHeap[NonUniformResourceIndex(shade.x)];\n"
"  h.albedo=(shade.x!=0?skin.SampleLevel(rtCardSampler,float2(.5,.5),16).rgb:float3(1,1,1))*rtShadeDiffuse(shade.w);\n"
" }else{\n"
"  uint4 record=rtRecords.Load4(32+(id+geometry)*16);\n"
"  uint4 shade=rtShade.Load4((id+geometry)*16);\n"
"  uint3 corner=rtIndices.Load3((record.y+primitive)*12);\n"
"  float3x4 place=query.CommittedObjectToWorld3x4();\n"
"  p0=mul(place,float4(asfloat(rtVertices.Load3((record.x+corner.x)*20)),1));\n"
"  p1=mul(place,float4(asfloat(rtVertices.Load3((record.x+corner.y)*20)),1));\n"
"  p2=mul(place,float4(asfloat(rtVertices.Load3((record.x+corner.z)*20)),1));\n"
"  h.uv=asfloat(rtVertices.Load2((record.x+corner.x)*20+12))*(1-b.x-b.y)+\n"
"       asfloat(rtVertices.Load2((record.x+corner.y)*20+12))*b.x+\n"
"       asfloat(rtVertices.Load2((record.x+corner.z)*20+12))*b.y;\n"
"  h.profile=shade.y;h.emissive=rtShadeEmissive(shade.z);\n"
"  if((shade.z>>30)==1u){h.kind=1;h.albedo=rtTerrainAlbedo(h.position.xy);}\n"
"  else{\n"
"   Texture2D surface=ResourceDescriptorHeap[NonUniformResourceIndex(shade.x)];\n"
"   h.albedo=(shade.x!=0?surface.SampleLevel(rtCardSampler,h.uv,rtCardLod(h.t)).rgb:float3(1,1,1))*rtShadeDiffuse(shade.w);\n"
"  }\n"
" }\n"
" if(corners){\n"
"  float3 n=cross(p1-p0,p2-p0);\n"
"  if(dot(n,n)>1e-20){n=normalize(n);h.under=id==0x800000u&&dot(n,direction)>0;h.normal=dot(n,direction)>0?-n:n;}\n"
" }\n"
" return h;\n"
"}\n"
"// 1 when nothing lies between origin and the end of the ray (the sun rays' rule:\n"
"// terrain undersides culled, cut-out cards by their alpha test).\n"
"float rtVisible(float3 origin,float3 direction,float nearest,float farthest){\n"
" RayDesc ray;ray.Origin=origin;ray.Direction=direction;ray.TMin=nearest;ray.TMax=farthest;\n"
" RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES|RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH|RAY_FLAG_CULL_BACK_FACING_TRIANGLES> query;\n"
" query.TraceRayInline(rtScene,RAY_FLAG_NONE,0xFF,ray);\n"
" while(query.Proceed()){\n"
"  if(query.CandidateType()!=CANDIDATE_NON_OPAQUE_TRIANGLE)continue;\n"
"  if(rtCardOpaque(query.CandidateInstanceID(),query.CandidateGeometryIndex(),query.CandidatePrimitiveIndex(),\n"
"   query.CandidateTriangleBarycentrics(),query.CandidateTriangleRayT()))query.CommitNonOpaqueTriangleHit();\n"
" }\n"
" return query.CommittedStatus()==COMMITTED_TRIANGLE_HIT?0:1;\n"
"}\n"
"float3 rtLit(RtHit h,float3 sun,float3 sunDiffuse,float3 ambient){\n"
" float3 light=ambient;\n"
" float facing=dot(h.normal,sun);\n"
" if(dot(sun,sun)>1e-12&&facing>0)light+=sunDiffuse*(facing*rtVisible(h.position+h.normal*.03,sun,.02,100000));\n"
" float2 cell=floor((h.position.xy-rtLightGridOrigin)*rtLightGridInverseCell);\n"
" if(rtLightCount!=0&&all(cell>=0)&&all(cell<(float2)rtLightGridSize)){\n"
"  uint index=(uint)cell.y*rtLightGridSize.x+(uint)cell.x;\n"
"  uint first=rtLightGrid[index],last=min(rtLightGrid[index+1],first+64u);\n"
"  for(uint slot=first;slot<last;++slot){\n"
"   RtPointLight lamp=rtLights[rtLightGrid[slot]];\n"
"   float3 delta=lamp.centerInverseRadiusSquared.xyz-h.position;\n"
"   float reach=1-dot(delta,delta)*lamp.centerInverseRadiusSquared.w;\n"
"   if(reach<=0)continue;\n"
"   light+=lamp.color.rgb*(reach*reach*saturate(dot(h.normal,normalize(delta)))*(lamp.color.w>0?.65:1));\n"
"  }\n"
" }\n"
" return h.albedo*light+h.emissive;\n"
"}\n";
// rt_debug_view 5 to 7: what a ray from the eye through each pixel returns
// (rtTrace above; not the drawn image): 5 the hit's albedo, 6 the hit lit
// (rtLit), 7 its normal. Dark blue: no hit.
constexpr char kRaytracingHitDebugTail[] =
"cbuffer RtHitDebugConstants:register(b1){row_major float4x4 rtClipToWorld;float4 rtSun;float4 rtSize;uint4 rtControl;float4 rtAmbient;float4 rtSunDiffuse;};\n"
"float4 rtHitDebugPS(float4 position:SV_Position):SV_Target{\n"
" float2 uv=position.xy*rtSize.zw;\n"
" float4 from=mul(rtClipToWorld,float4(uv.x*2-1,1-uv.y*2,0,1)),to=mul(rtClipToWorld,float4(uv.x*2-1,1-uv.y*2,1,1));\n"
" float3 origin=from.xyz/from.w,direction=normalize(to.xyz/to.w-origin);\n"
" RtHit h=rtTrace(origin,direction,0,100000,RAY_FLAG_NONE);\n"
" if(!h.hit)return float4(.02,.03,.06,1);\n"
" if(rtControl.x==5)return float4(h.albedo,1);\n"
" if(rtControl.x==7)return float4(h.normal*.5+.5,1);\n"
" return float4(rtLit(h,rtSun.xyz,rtSunDiffuse.rgb,rtAmbient.rgb),1);\n"
"}\n";
const std::string& raytracing_hit_debug_source(){
 static const std::string source=std::string(kRaytracingHitShader)+kRaytracingHitDebugTail;
 return source;
}
// append_raytracing_shader_programs order.
enum : std::size_t { kRtDebugVertex, kRtDebugPixel, kRtRepack, kRtSunMask, kRtProbe, kRtHitDebugPixel, kRtPrograms };
// After kRtPrograms: rt_term_view's program, then the depth prepass of traced
// sun frames (append_raytracing_shader_programs).
constexpr std::size_t kRtPrepass=kRtPrograms+1;
// A hit's shading words (Renderer::RaytracingD3D12::shade) from its record's draw.
constexpr std::size_t kRtShadeWords = 4;
constexpr std::uint32_t kRtTerrainInstance = 0x800000u, kRtFigureInstance = 0x400000u;
template<class Draw> void raytracing_shade_words(const Draw& draw,bool terrain,std::uint32_t words[kRtShadeWords]){
 const auto& instance=draw.instance;
 const auto unorm=[](float value,float scale){return std::uint32_t((std::clamp)(std::isfinite(value)?value:0.f,0.f,1.f)*scale+.5f);};
 words[0]=draw.texture12?instance.padding[0]:0u;
 words[1]=draw.texture12?instance.padding[1]:0u;
 // What the surface emits: its authored emissive, not the game's highlight (visual_highlight.h).
 float emitted[3];visual_emitted_colour(instance.emissive,emitted);
 words[2]=unorm(emitted[0],1023.f)|(unorm(emitted[1],1023.f)<<10)|(unorm(emitted[2],1023.f)<<20)|(terrain?1u<<30:0u);
 words[3]=unorm(instance.material_diffuse[0],255.f)|(unorm(instance.material_diffuse[1],255.f)<<8)|(unorm(instance.material_diffuse[2],255.f)<<16);
}
// rt_direct_lights (stage 2c, D3D12 only; Renderer::direct_lights_d3d12):
// ReSTIR direct lighting of the published point lights with the NVIDIA RTXDI
// library 3.2.0. The library's HLSL headers are included from the SDK folder
// when dxc compiles these two programs (the RTXDI_SDK define adds the include
// path, Tools/shader_embed.cpp); everything here is this project's side of
// it, the "application bridge" (the RAB_ functions and types the headers
// call) and the passes.
// The bridge's model:
// - a surface is a render pixel of the guide targets: position from the world
//   depth, the guide normal and roughness, and three flags the guide twins
//   left in the albedo targets' alpha (native_shader.h nativeGuideLight): a
//   lit receiver (it takes lamps, else the emissive glow only), the terrain's
//   grazing-light facing rule, a unit. Packed in 8 bytes for the spatial pass
//   and the next frame;
// - a light is a published point light (rdLights, the light grid's indices):
//   a lamp is a sphere around its shadow centre, an emissive (lava) light a
//   horizontal rectangle of its cluster's cell. A sample is a point on that
//   shape; it only aims the visibility ray. What the light gives a surface
//   (rdResponse) is the lamp loop's own term from the light's centre: colour,
//   the game's falloff (1 - d2/r2) squared, its facing rule and the .65 of a
//   shadow-casting lamp; a glow light the glow term. So the estimate's
//   expected value without occluders is the lamp loop's sum;
// - the target function is that response's luminance; a sample's solid-angle
//   density is 1 (the response is already per sample), no BRDF sampling;
// - visibility is one inline ray through the scene, cut-out cards by their
//   alpha test, ending rdEye.w metres before the sample (the lamp's own model
//   stands around it).
// rtDirectSampleCS: the pixel's surface packed; up to rdCounts.x candidates
// of its grid cell's lights streamed uniformly (every one when the cell holds
// no more: no selection noise), one visibility ray for the chosen one,
// temporal resampling from last frame's final reservoirs (array 0), the
// boiling filter; into array 1.
// rtDirectShadeCS: spatial resampling over array 1, the final visibility ray
// (reused for rdVisibility.x frames), the reservoir into array 0, and the
// light the lamp loop would have added to this pixel's colour: with d the
// lighting factor the twin shaded with and x the traced lamp light,
// albedo * (saturate(d + x) - d) for a lamp (the lamp loop's share of
// saturate(base + lamps), clamped or not), albedo * x for a glow light, and
// a lamp's highlight on material pixels, all times the share the pixel's fog
// leaves. With rt_indirect_light's factor image at t9 (rdFrame.z bit 3) d is
// saturate(d + indirect): that pass added its own share of the clamp first.
constexpr char kRtDirectShader[] =
"#define RTXDI_ENABLE_PRESAMPLING 0\n"
"#include \"Rtxdi/RtxdiParameters.h\"\n"
"#include \"Rtxdi/DI/ReSTIRDIParameters.h\"\n"
"#include \"Rtxdi/Utils/RandomSamplerState.hlsli\"\n"
"cbuffer RtDirectConstants:register(b1){row_major float4x4 rdClipToWorld;row_major float4x4 rdWorldToClip;\n"
" row_major float4x4 rdPreviousClipToWorld;row_major float4x4 rdPreviousWorldToClip;\n"
" float4 rdEye;float4 rdPreviousEye;float4 rdSizeJitter;float4 rdPreviousJitter;\n"
" uint4 rdFrame;uint4 rdReservoir;uint4 rdCounts;float4 rdSpatial;float4 rdVisibility;};\n"
"cbuffer RtDirectLightGrid:register(b5){uint rdLightCount;uint2 rdGridSize;uint rdGlowCount;float2 rdGridOrigin;float rdGridInverseCell;float rdGridTail;};\n"
"struct RdLight{float3 centre;float inverseRadiusSquared;float3 color;float kind;float3 extent;float intensity;float3 target;float lightPadding;};\n"
"Texture2D<float> rdDepth:register(t0);\n"
"Texture2D<float4> rdGuideNormal:register(t1);\n"
"Texture2D<float4> rdGuideDiffuse:register(t2);\n"
"Texture2D<float4> rdGuideSpecular:register(t3);\n"
"Texture2D<uint2> rdPreviousSurfaces:register(t4);\n"
"Texture2D<float2> rdObjectMotion:register(t5);\n"
"Texture2D<float> rdObjectDepth:register(t6);\n"
"StructuredBuffer<float2> rdNeighbourOffsets:register(t7);\n"
"Texture2D<uint2> rdSurfaces:register(t8);\n"
"Texture2D<float4> rdIndirect:register(t9);\n"
"StructuredBuffer<uint> rdLightGrid:register(t17);\n"
"StructuredBuffer<RdLight> rdLights:register(t40);\n"
"StructuredBuffer<int> rdLightRemap:register(t41);\n"
"RaytracingAccelerationStructure rdScene:register(t94);\n"
"ByteAddressBuffer rdSceneVertices:register(t95);\n"
"ByteAddressBuffer rdSceneIndices:register(t99);\n"
"ByteAddressBuffer rdSceneRecords:register(t113);\n"
"SamplerState rdCardSampler:register(s0);\n"
"RWStructuredBuffer<RTXDI_PackedDIReservoir> rdReservoirs:register(u0);\n"
"#ifdef RT_DIRECT_SHADE\n"
"RWTexture2D<float4> rdLightOutput:register(u1);\n"
"#else\n"
"RWTexture2D<uint2> rdSurfaceOutput:register(u1);\n"
"#endif\n"
"// ---- surfaces\n"
"struct RAB_Surface{float3 worldPos;float3 normal;float3 viewDir;float linearDepth;float roughness;uint flags;};\n"
"struct RAB_Material{float roughness;uint flags;};\n"
"RAB_Surface RAB_EmptySurface(){RAB_Surface s=(RAB_Surface)0;return s;}\n"
"bool RAB_IsSurfaceValid(RAB_Surface s){return (s.flags&8u)!=0;}\n"
"float3 RAB_GetSurfaceWorldPos(RAB_Surface s){return s.worldPos;}\n"
"float3 RAB_GetSurfaceNormal(RAB_Surface s){return s.normal;}\n"
"float RAB_GetSurfaceLinearDepth(RAB_Surface s){return s.linearDepth;}\n"
"RAB_Material RAB_GetMaterial(RAB_Surface s){RAB_Material m;m.roughness=s.roughness;m.flags=s.flags;return m;}\n"
"bool RAB_AreMaterialsSimilar(RAB_Material a,RAB_Material b){return a.flags==b.flags&&abs(a.roughness-b.roughness)<.25;}\n"
"uint rdPackNormal(float3 n){\n"
" float2 p=n.xy/max(abs(n.x)+abs(n.y)+abs(n.z),1e-6);\n"
" if(n.z<0)p=(1-abs(p.yx))*float2(p.x>=0?1.0:-1.0,p.y>=0?1.0:-1.0);\n"
" uint2 q=uint2(saturate(p*.5+.5)*4095+.5);\n"
" return q.x|(q.y<<12);\n"
"}\n"
"float3 rdUnpackNormal(uint v){\n"
" float2 p=float2(v&4095u,(v>>12)&4095u)/4095.0*2-1;\n"
" float3 n=float3(p,1-abs(p.x)-abs(p.y));\n"
" float t=saturate(-n.z);\n"
" n.x+=n.x>=0?-t:t;n.y+=n.y>=0?-t:t;\n"
" return normalize(n);\n"
"}\n"
"// The packed surface of a pixel of this or of the last frame (x the depth's bits, 1: none;\n"
"// y the normal in 24 bits, the roughness in 5, the three flags).\n"
"RAB_Surface rdUnpackSurface(uint2 packed,int2 pixel,bool previous){\n"
" RAB_Surface s=RAB_EmptySurface();\n"
" float z=asfloat(packed.x);\n"
" if(!(z<1))return s;\n"
" float2 uv=(float2(pixel)+.5-(previous?rdPreviousJitter.xy:rdSizeJitter.zw))/rdSizeJitter.xy;\n"
" float4 clip=float4(uv.x*2-1,1-uv.y*2,z,1);\n"
" float4 world=previous?mul(rdPreviousClipToWorld,clip):mul(rdClipToWorld,clip);\n"
" s.worldPos=world.xyz/world.w;\n"
" s.linearDepth=previous?mul(rdPreviousWorldToClip,float4(s.worldPos,1)).w:mul(rdWorldToClip,float4(s.worldPos,1)).w;\n"
" s.viewDir=normalize((previous?rdPreviousEye.xyz:rdEye.xyz)-s.worldPos);\n"
" s.normal=rdUnpackNormal(packed.y);\n"
" s.roughness=float((packed.y>>24)&31u)/31.0;\n"
" s.flags=(packed.y>>29)|8u;\n"
" return s;\n"
"}\n"
"RAB_Surface RAB_GetGBufferSurface(int2 pixel,bool previousFrame){\n"
" if(any(pixel<0)||any(pixel>=int2(rdSizeJitter.xy))||(previousFrame&&(rdFrame.z&1u)==0))return RAB_EmptySurface();\n"
" uint2 packed=previousFrame?rdPreviousSurfaces.Load(int3(pixel,0)):rdSurfaces.Load(int3(pixel,0));\n"
" return rdUnpackSurface(packed,pixel,previousFrame);\n"
"}\n"
"int2 RAB_ClampSamplePositionIntoView(int2 pixel,bool previousFrame){return clamp(pixel,int2(0,0),int2(rdSizeJitter.xy)-1);}\n"
"// ---- lights\n"
"struct RAB_LightInfo{float3 centre;float inverseRadiusSquared;float3 color;float kind;float3 extent;float intensity;float3 target;};\n"
"struct RAB_LightSample{float3 position;float3 direction;float3 radiance;float kind;float intensity;};\n"
"RAB_LightInfo RAB_EmptyLightInfo(){RAB_LightInfo light=(RAB_LightInfo)0;return light;}\n"
"RAB_LightSample RAB_EmptyLightSample(){RAB_LightSample s=(RAB_LightSample)0;return s;}\n"
"// A light index of this frame as last frame's (currentToPrevious) or back; -1: not there.\n"
"int RAB_TranslateLightIndex(uint index,bool currentToPrevious){\n"
" if((rdFrame.z&4u)==0)return index<rdFrame.y?int(index):-1;\n"
" if(currentToPrevious)return index<rdFrame.y?rdLightRemap[rdFrame.w+index]:-1;\n"
" return index<rdFrame.w?rdLightRemap[index]:-1;\n"
"}\n"
"// previousFrame: by last frame's index; the light as it is now (the lights hold no history).\n"
"RAB_LightInfo RAB_LoadLightInfo(uint index,bool previousFrame){\n"
" RAB_LightInfo light=RAB_EmptyLightInfo();\n"
" if(previousFrame){int now=RAB_TranslateLightIndex(index,false);if(now<0)return light;index=uint(now);}\n"
" if(index>=rdFrame.y)return light;\n"
" RdLight l=rdLights[index];\n"
" light.centre=l.centre;light.inverseRadiusSquared=l.inverseRadiusSquared;light.color=l.color;light.kind=l.kind;\n"
" light.extent=l.extent;light.intensity=l.intensity;light.target=l.target;\n"
" return light;\n"
"}\n"
"RAB_LightInfo RAB_LoadCompactLightInfo(uint index){return RAB_EmptyLightInfo();}\n"
"RAB_LightSample RAB_SamplePolymorphicLight(RAB_LightInfo light,RAB_Surface surface,float2 uv){\n"
" RAB_LightSample s=RAB_EmptyLightSample();\n"
" float3 delta=light.centre-surface.worldPos;\n"
" float distanceSquared=dot(delta,delta);\n"
" float reach=1-distanceSquared*light.inverseRadiusSquared;\n"
" if(!(reach>0)||!(light.inverseRadiusSquared>0))return s;\n"
" s.direction=delta*rsqrt(max(distanceSquared,1e-12));\n"
" s.radiance=light.color*(reach*reach);s.kind=light.kind;s.intensity=light.intensity;\n"
" if(light.kind<.5){\n"
"  float z=1-2*uv.x,r=sqrt(saturate(1-z*z)),a=6.2831853*uv.y;\n"
"  s.position=light.target+light.extent.x*float3(r*cos(a),r*sin(a),z);\n"
" }else s.position=light.target+float3((uv*2-1)*light.extent.xy,0);\n"
" return s;\n"
"}\n"
"// What the sample's light adds to the surface's lighting factor before visibility: the lamp loop's term.\n"
"float3 rdResponse(RAB_LightSample s,RAB_Surface surface){\n"
" float incidence=dot(surface.normal,s.direction);\n"
" if(s.kind>.5)return s.radiance*saturate(incidence*.6+.4);\n"
" if((surface.flags&1u)==0)return 0;\n"
" float facing=(surface.flags&2u)!=0?saturate(1+min(incidence,0)):max(incidence,0);\n"
" return s.radiance*(facing*s.intensity);\n"
"}\n"
"float RAB_GetLightSampleTargetPdfForSurface(RAB_LightSample s,RAB_Surface surface){return dot(rdResponse(s,surface),float3(.2126,.7152,.0722));}\n"
"float RAB_LightSampleSolidAnglePdf(RAB_LightSample s){return 1;}\n"
"bool RAB_IsAnalyticLightSample(RAB_LightSample s){return true;}\n"
"void RAB_GetLightDirDistance(RAB_Surface surface,RAB_LightSample s,out float3 o_lightDir,out float o_lightDistance){\n"
" float3 toLight=s.position-surface.worldPos;o_lightDistance=length(toLight);o_lightDir=toLight/max(o_lightDistance,1e-6);\n"
"}\n"
"// No BRDF sampling and no environment light: the lights are not in the scene.\n"
"float RAB_SurfaceEvaluateBrdfPdf(RAB_Surface surface,float3 dir){return 0;}\n"
"bool RAB_SurfaceImportanceSampleBrdf(RAB_Surface surface,inout RTXDI_RandomSamplerState rng,out float3 dir){dir=float3(0,0,1);return false;}\n"
"bool RAB_TraceRayForLocalLight(float3 origin,float3 direction,float tMin,float tMax,out uint o_lightIndex,out float2 o_randXY){\n"
" o_lightIndex=RTXDI_InvalidLightIndex;o_randXY=0;return false;\n"
"}\n"
"float RAB_EvaluateLocalLightSourcePdf(uint lightIndex){return 0;}\n"
"float2 RAB_GetEnvironmentMapRandXYFromDir(float3 direction){return 0;}\n"
"float RAB_EvaluateEnvironmentMapSamplingPdf(float3 direction){return 0;}\n"
"// ---- visibility\n"
"bool rdVisible(RAB_Surface surface,float3 target){\n"
" float3 toLight=target-surface.worldPos;\n"
" float distance=length(toLight);\n"
" float nearest=rdPreviousJitter.z+rdPreviousJitter.w*length(surface.worldPos-rdEye.xyz)+((surface.flags&4u)!=0?.25:0);\n"
" float farthest=distance-rdEye.w;\n"
" if(!(farthest>nearest))return true;\n"
" RayDesc ray;ray.Origin=surface.worldPos;ray.Direction=toLight/distance;ray.TMin=nearest;ray.TMax=farthest;\n"
" RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES|RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH|RAY_FLAG_CULL_BACK_FACING_TRIANGLES> query;\n"
" query.TraceRayInline(rdScene,RAY_FLAG_NONE,0xFF,ray);\n"
" while(query.Proceed()){\n"
"  if(query.CandidateType()!=CANDIDATE_NON_OPAQUE_TRIANGLE)continue;\n"
"  uint4 record=rdSceneRecords.Load4(32+(query.CandidateInstanceID()+query.CandidateGeometryIndex())*16);\n"
"  uint3 corner=rdSceneIndices.Load3((record.y+query.CandidatePrimitiveIndex())*12);\n"
"  float2 b=query.CandidateTriangleBarycentrics();\n"
"  float2 uv=asfloat(rdSceneVertices.Load2((record.x+corner.x)*20+12))*(1-b.x-b.y)+\n"
"            asfloat(rdSceneVertices.Load2((record.x+corner.y)*20+12))*b.x+\n"
"            asfloat(rdSceneVertices.Load2((record.x+corner.z)*20+12))*b.y;\n"
"  Texture2D card=ResourceDescriptorHeap[NonUniformResourceIndex(record.z)];\n"
"  if(card.SampleLevel(rdCardSampler,uv,1).a>=asfloat(record.w))query.CommitNonOpaqueTriangleHit();\n"
" }\n"
" return query.CommittedStatus()!=COMMITTED_TRIANGLE_HIT;\n"
"}\n"
"bool RAB_GetConservativeVisibility(RAB_Surface surface,RAB_LightSample s){return rdVisible(surface,s.position);}\n"
"bool RAB_GetTemporalConservativeVisibility(RAB_Surface surface,RAB_Surface previousSurface,RAB_LightSample s){return rdVisible(surface,s.position);}\n"
"// ---- the library\n"
"#define RTXDI_ALLOWED_BIAS_CORRECTION RTXDI_BIAS_CORRECTION_BASIC\n"
"#define RTXDI_LIGHT_RESERVOIR_BUFFER rdReservoirs\n"
"#define RTXDI_NEIGHBOR_OFFSETS_BUFFER rdNeighbourOffsets\n"
"#define RTXDI_ENABLE_BOILING_FILTER\n"
"#define RTXDI_BOILING_FILTER_GROUP_SIZE 8\n"
"#include \"Rtxdi/DI/InitialSampling.hlsli\"\n"
"#include \"Rtxdi/DI/TemporalResampling.hlsli\"\n"
"#include \"Rtxdi/DI/BoilingFilter.hlsli\"\n"
"#include \"Rtxdi/DI/SpatialResampling.hlsli\"\n"
"RTXDI_RuntimeParameters rdRuntime(){RTXDI_RuntimeParameters p;p.neighborOffsetMask=rdReservoir.z;p.activeCheckerboardField=0;p.frameIndex=rdFrame.x;p.pad1=0;return p;}\n"
"RTXDI_ReservoirBufferParameters rdBuffer(){RTXDI_ReservoirBufferParameters p;p.reservoirBlockRowPitch=rdReservoir.x;p.reservoirArrayPitch=rdReservoir.y;p.pad1=0;p.pad2=0;return p;}\n"
"#ifndef RT_DIRECT_SHADE\n"
"// The pixel's place in last frame's reservoirs (pixels, from this pixel) and its depth change: the\n"
"// camera's, or the object motion a posed record wrote there (scene_composite.h cameraMotionPS).\n"
"float3 rdMotion(int2 pixel,RAB_Surface s){\n"
" float2 unjittered=float2(pixel)+.5-rdSizeJitter.zw,before=unjittered;float change=0;\n"
" float4 clip=mul(rdPreviousWorldToClip,float4(s.worldPos,1));\n"
" if(clip.w>1e-6){before=float2(clip.x/clip.w*.5+.5,.5-clip.y/clip.w*.5)*rdSizeJitter.xy;change=clip.w-s.linearDepth;}\n"
" if((rdFrame.z&2u)!=0){\n"
"  float written=rdObjectDepth.Load(int3(pixel,0));\n"
"  if(written<0||abs(written-rdDepth.Load(int3(pixel,0)))<=1e-6){before=unjittered+rdObjectMotion.Load(int3(pixel,0));change=0;}\n"
" }\n"
" return float3(before+rdPreviousJitter.xy-(float2(pixel)+.5),change);\n"
"}\n"
"[numthreads(8,8,1)]void rtDirectSampleCS(uint3 id:SV_DispatchThreadID,uint3 local:SV_GroupThreadID){\n"
" int2 pixel=int2(id.xy);\n"
" bool inside=all(id.xy<uint2(rdSizeJitter.xy));\n"
" RAB_Surface surface=RAB_EmptySurface();\n"
" if(inside){\n"
"  uint2 packed=uint2(asuint(1.0),0);\n"
"  float z=rdDepth.Load(int3(pixel,0));\n"
"  float4 n=rdGuideNormal.Load(int3(pixel,0));\n"
"  uint a=uint(rdGuideDiffuse.Load(int3(pixel,0)).a*65535+.5),b=uint(rdGuideSpecular.Load(int3(pixel,0)).a*65535+.5);\n"
"  if(z<1&&dot(n.xyz,n.xyz)>.25&&((b>>7)&255u)!=0){\n"
"   uint flags=((a>>14)&3u)|(((b>>15)&1u)<<2);\n"
"   packed=uint2(asuint(z),rdPackNormal(normalize(n.xyz))|(uint(saturate(n.w)*31+.5)<<24)|(flags<<29));\n"
"   surface=rdUnpackSurface(packed,pixel,false);\n"
"  }\n"
"  rdSurfaceOutput[pixel]=packed;\n"
" }\n"
" RTXDI_DIReservoir reservoir=RTXDI_EmptyDIReservoir();\n"
" RAB_LightSample selected=RAB_EmptyLightSample();\n"
" if(RAB_IsSurfaceValid(surface)){\n"
"  float2 cell=floor((surface.worldPos.xy-rdGridOrigin)*rdGridInverseCell);\n"
"  if(all(cell>=0)&&all(cell<(float2)rdGridSize)){\n"
"   uint index=(uint)cell.y*rdGridSize.x+(uint)cell.x;\n"
"   uint first=rdLightGrid[index],count=rdLightGrid[index+1]-first;\n"
"   if(count!=0&&count<0x10000u){\n"
"    RTXDI_RandomSamplerState rng=RTXDI_InitRandomSampler(id.xy,rdFrame.x,RTXDI_DI_GENERATE_INITIAL_SAMPLES_RANDOM_SEED);\n"
"    RTXDI_DIInitialSamplingParameters initial=(RTXDI_DIInitialSamplingParameters)0;\n"
"    initial.numLocalLightSamples=min(count,rdCounts.x);\n"
"    RTXDI_InitialSamplingMisData mis=RTXDI_ComputeInitialSamplingMisData(initial);\n"
"    for(uint k=0;k<initial.numLocalLightSamples;k++){\n"
"     float pick=(RTXDI_GetNextRandom(rng)+k)/initial.numLocalLightSamples;\n"
"     uint light=rdLightGrid[first+min(uint(pick*count),count-1)];\n"
"     float2 uv=RTXDI_RandomlySelectLocalLightUV(rng);\n"
"     RTXDI_StreamLocalLightAtUVIntoReservoir(rng,mis,surface,0,mis.localLightMisWeight,light,uv,float(count),\n"
"      RAB_LoadLightInfo(light,false),reservoir,selected);\n"
"    }\n"
"    RTXDI_FinalizeResampling(reservoir,1.0,mis.numMisSamples);\n"
"    reservoir.M=1;\n"
"    if(RTXDI_IsValidDIReservoir(reservoir)&&!RAB_GetConservativeVisibility(surface,selected))RTXDI_StoreVisibilityInDIReservoir(reservoir,0,true);\n"
"    if((rdFrame.z&1u)!=0){\n"
"     RTXDI_DITemporalResamplingParameters temporal=(RTXDI_DITemporalResamplingParameters)0;\n"
"     temporal.maxHistoryLength=rdCounts.y;temporal.biasCorrectionMode=RTXDI_BIAS_CORRECTION_BASIC;\n"
"     temporal.depthThreshold=rdSpatial.y;temporal.normalThreshold=rdSpatial.z;\n"
"     temporal.enablePermutationSampling=1;temporal.uniformRandomNumber=rdReservoir.w;temporal.enableFallbackSampling=1;\n"
"     RTXDI_RandomSamplerState temporalRng=RTXDI_InitRandomSampler(id.xy,rdFrame.x,RTXDI_DI_TEMPORAL_RESAMPLING_RANDOM_SEED);\n"
"     int2 temporalPixel;\n"
"     reservoir=RTXDI_DITemporalResampling(id.xy,surface,reservoir,temporalRng,rdRuntime(),rdBuffer(),rdMotion(pixel,surface),0,\n"
"      temporal,temporalPixel,selected);\n"
"    }\n"
"   }\n"
"  }\n"
" }\n"
" RTXDI_BoilingFilter(local.xy,rdSpatial.w,reservoir);\n"
" if(inside)RTXDI_StoreDIReservoir(reservoir,rdBuffer(),id.xy,1);\n"
"}\n"
"#else\n"
"[numthreads(8,8,1)]void rtDirectShadeCS(uint3 id:SV_DispatchThreadID){\n"
" if(any(id.xy>=uint2(rdSizeJitter.xy)))return;\n"
" int2 pixel=int2(id.xy);\n"
" RAB_Surface surface=RAB_GetGBufferSurface(pixel,false);\n"
" RTXDI_DIReservoir reservoir=RTXDI_LoadDIReservoir(rdBuffer(),id.xy,1);\n"
" float3 light=0;\n"
" if(RAB_IsSurfaceValid(surface)&&reservoir.M>0){\n"
"  RAB_LightSample selected=RAB_EmptyLightSample();\n"
"  RTXDI_RandomSamplerState rng=RTXDI_InitRandomSampler(id.xy,rdFrame.x,RTXDI_DI_SPATIAL_RESAMPLING_RANDOM_SEED);\n"
"  RTXDI_DISpatialResamplingParameters spatial=(RTXDI_DISpatialResamplingParameters)0;\n"
"  spatial.numSamples=rdCounts.z;spatial.numDisocclusionBoostSamples=rdCounts.w;spatial.samplingRadius=rdSpatial.x;\n"
"  spatial.biasCorrectionMode=RTXDI_BIAS_CORRECTION_BASIC;spatial.depthThreshold=rdSpatial.y;spatial.normalThreshold=rdSpatial.z;\n"
"  spatial.targetHistoryLength=rdCounts.y;spatial.enableMaterialSimilarityTest=1;\n"
"  spatial.neighborOffsetSamplingMode=RTXDI_DI_NEIGHBOR_OFFSET_SAMPLING_MODE_LOW_DISCREPANCY;\n"
"  reservoir=RTXDI_DISpatialResampling(id.xy,surface,reservoir,rng,rdRuntime(),rdBuffer(),1,spatial,selected);\n"
"  if(RTXDI_IsValidDIReservoir(reservoir)){\n"
"   RAB_LightSample s=RAB_SamplePolymorphicLight(RAB_LoadLightInfo(RTXDI_GetDIReservoirLightIndex(reservoir),false),surface,\n"
"    RTXDI_GetDIReservoirSampleUV(reservoir));\n"
"   float3 visibility=0;\n"
"   RTXDI_VisibilityReuseParameters reuse;reuse.maxAge=uint(rdVisibility.x);reuse.maxDistance=rdVisibility.y;\n"
"   if(!RTXDI_GetDIReservoirVisibility(reservoir,reuse,visibility)){\n"
"    visibility=rdVisible(surface,s.position)?1:0;\n"
"    RTXDI_StoreVisibilityInDIReservoir(reservoir,visibility,true);\n"
"   }\n"
"   float weight=RTXDI_GetDIReservoirInvPdf(reservoir)*visibility.x;\n"
"   if(weight>0){\n"
"    uint a=uint(rdGuideDiffuse.Load(int3(pixel,0)).a*65535+.5),b=uint(rdGuideSpecular.Load(int3(pixel,0)).a*65535+.5);\n"
"    float3 factor=float3(a&127u,(a>>7)&127u,b&127u)/127.0;\n"
"    // rt_indirect_light added saturate(factor + indirect) - factor already: the lamps' share starts there.\n"
"    if((rdFrame.z&8u)!=0)factor=saturate(factor+rdIndirect.Load(int3(pixel,0)).rgb);\n"
"    float kept=float((b>>7)&255u)/255.0;\n"
"    float3 added=rdResponse(s,surface)*weight;\n"
"    light=rdGuideDiffuse.Load(int3(pixel,0)).rgb*(s.kind>.5?added:saturate(factor+added)-factor);\n"
"    // A lamp's highlight on a material pixel (the lamp loop's GGX lobe; F0 from the specular guide).\n"
"    float4 guide=rdGuideNormal.Load(int3(pixel,0));\n"
"    float incidence=dot(surface.normal,s.direction);\n"
"    if(s.kind<.5&&(surface.flags&3u)==1u&&guide.w<.9&&incidence>0){\n"
"     float3 h=normalize(s.direction+surface.viewDir);\n"
"     float nh=saturate(dot(surface.normal,h)),nv=max(saturate(dot(surface.normal,surface.viewDir)),1e-3),a2=guide.w*guide.w*guide.w*guide.w;\n"
"     float d=nh*nh*(a2-1)+1;\n"
"     float gv=incidence*sqrt(nv*nv*(1-a2)+a2),gl=nv*sqrt(incidence*incidence*(1-a2)+a2);\n"
"     float lobe=a2/(3.14159265*d*d)*.5/max(gv+gl,1e-5)*incidence;\n"
"     float fresnel=pow(1-saturate(dot(surface.viewDir,h)),5);\n"
"     float3 f0=rdGuideSpecular.Load(int3(pixel,0)).rgb;\n"
"     light+=s.radiance*(s.intensity*lobe*weight)*(f0*(1-fresnel)+fresnel);\n"
"    }\n"
"    light*=kept;\n"
"    if(any(isnan(light))||any(isinf(light)))light=0;\n"
"   }\n"
"  }\n"
" }\n"
" RTXDI_StoreDIReservoir(reservoir,rdBuffer(),id.xy,0);\n"
" rdLightOutput[pixel]=float4(light,1);\n"
"}\n"
"#endif\n";
constexpr char kRtDirectAddShader[] =
"Texture2D<float4> rdLight:register(t0);\n"
"float4 rtDirectAddPS(float4 position:SV_Position):SV_Target{return float4(rdLight.Load(int3(position.xy,0)).rgb,0);}\n";
// append_rt_direct_shader_programs order.
enum : std::size_t { kRtDirectSample, kRtDirectShade, kRtDirectAdd, kRtDirectPrograms };
void append_rt_direct_shader_programs(std::vector<ShaderProgram>& out){
 // RTXDI_SDK: dxc gets the RTXDI library's include folder for these two (Tools/shader_embed.cpp).
 static const D3D_SHADER_MACRO sample[]={{"RTXDI_SDK","1"},{nullptr,nullptr}};
 static const D3D_SHADER_MACRO shade[]={{"RTXDI_SDK","1"},{"RT_DIRECT_SHADE","1"},{nullptr,nullptr}};
 out.push_back({{kRtDirectShader,sizeof(kRtDirectShader)-1},nullptr,sample,"rtDirectSampleCS","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 out.push_back({{kRtDirectShader,sizeof(kRtDirectShader)-1},nullptr,shade,"rtDirectShadeCS","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 out.push_back({{kRtDirectAddShader,sizeof(kRtDirectAddShader)-1},nullptr,nullptr,"rtDirectAddPS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
}
// The passes' b1 (kRtDirectShader RtDirectConstants) and a light (RdLight).
struct RtDirectConstants{
 float clip_to_world[16],world_to_clip[16],previous_clip_to_world[16],previous_world_to_clip[16];
 float eye[4];              // w: the clearance a visibility ray keeps from its lamp
 float previous_eye[4];
 float size_jitter[4];      // the render size, this frame's jitter (pixels)
 float previous_jitter[4];  // last frame's jitter; z, w: a ray's self-hit distance and its growth per metre of view distance
 std::uint32_t frame[4];    // the frame, the light count, flags (1 history, 2 object motion, 4 light remap, 8 the indirect factor image at t9), last frame's light count
 std::uint32_t reservoir[4];// block row pitch, array pitch, neighbour offset mask, the frame's random word
 std::uint32_t counts[4];   // initial candidates, history length, spatial samples, spatial samples on disocclusion
 float spatial[4];          // spatial radius (pixels), depth threshold, normal threshold, boiling filter strength
 float visibility[4];       // final visibility reuse: frames, pixels
};
static_assert(sizeof(RtDirectConstants)==400);
struct RtDirectLight{float centre[3],inverse_radius_squared,color[3],kind,extent[3],intensity,target[3],padding;};
static_assert(sizeof(RtDirectLight)==64);
// The library's reservoir layout as these passes use it: 16-pixel blocks, 24 bytes a packed reservoir.
constexpr UINT kRtReservoirBlock = 16, kRtReservoirBytes = 24, kRtNeighbourOffsets = 8192;
// A spatial pass's neighbour offsets (x, y pairs): a low-discrepancy sequence (R2) over the unit disc.
std::vector<float> rt_neighbour_offsets(){
 std::vector<float> offsets(std::size_t(kRtNeighbourOffsets)*2u);
 for(UINT n=0;n<kRtNeighbourOffsets;++n){
  const double u=std::fmod(.5+.7548776662466927*double(n+1),1.0),v=std::fmod(.5+.5698402909980532*double(n+1),1.0);
  const double radius=std::sqrt(u),angle=6.283185307179586*v;
  offsets[n*2u]=float(radius*std::cos(angle));offsets[n*2u+1u]=float(radius*std::sin(angle));
 }
 return offsets;
}
// rt_indirect_light (stage 2d, D3D12 only; Renderer::indirect_light_d3d12):
// one bounce of diffuse indirect light on the opaque and cut-out world by
// ReSTIR GI (the NVIDIA RTXDI library 3.2.0's GI headers, included at build
// time like the direct light programs'), at half of the render resolution:
// a half pixel stands for one render pixel of its 2x2 quad (riOffsets, a
// different one each frame). This text follows kRaytracingHitShader (rtTrace,
// rtVisible, the light grid and the bounce volume's constants at b5).
// Units: the lighting factor (what the lamp loop adds before the texture).
// A surface lit with factor f sends albedo * f back, so a cosine-distributed
// ray's radiance is the factor it adds to its receiver.
// rtIndirectSampleCS (half resolution): the surface packed (the direct light
// passes' packing); one cosine-distributed ray; its hit lit by the sun (one
// shadow ray), one lamp of its light grid cell (up to riCounts.w candidates,
// one kept by its unshadowed response with the lamp loop's facing rule (the
// ground keeps grazing light), one visibility ray; a glow light
// unshadowed) and the bounce volume at the hit for the hit's normal (the
// later bounces; cave_irradiance.h), as albedo * saturate(light + emissive):
// what the hit shows on screen without the ambient light, which stays the
// sky's term; a miss returns nothing. rt_indirect_cave_exits (riFrame.y bit
// 2): a ray crossing a cave exit's rectangle before any hit returns that
// exit's outdoor colour. Then temporal resampling from last frame's final
// reservoirs (array 0) and the boiling filter; into array 1.
// rt_indirect_resolution: "half pixel" below reads a traced pixel of riReuse.y
// render pixels a side (2, or 4 at a quarter of the render size).
// riFrame.y bit 32 (rt_ambient_occlusion without rt_indirect_light): the ray
// is traced for its occlusion alone: its hit is not lit, nothing is
// resampled and no reservoir is stored.
// With RT_INDIRECT_AO (rt_ambient_occlusion) the same ray is the half
// pixel's ambient occlusion sample (ray traced ambient occlusion with a
// cosine-distributed ray: the mean of the rays' visibility is the estimate):
// 1 for a miss, (t / radius)^2 for a hit within the radius: riAo.x (the sky
// reach) for a ray going up, riAo.z (the contact reach) for a level or
// downward one, blended over the first quarter of the upward tilt. A hit on
// the underside of the scene's flat terrain (a receiver the tessellation
// drew below it) does not occlude. The sample joins the half pixel's
// history (the visibility and its frame count, up to riAo.y frames), found
// at the pixel's place in last frame's image among the four half pixels
// around it whose surface agrees (normal, expected depth); the visibility
// with the surface's linear depth goes to the ambient occlusion passes'
// denoise and resolve (ambient_occlusion.h: a half pixel without a lit
// surface as their sky).
// rtIndirectSpatialCS (half resolution): spatial resampling over array 1,
// one visibility ray to the kept sample (its result in the packed
// reservoir's spare bits, bit 16: occluded); into array 0.
// rtIndirectShadeCS (render resolution): a pixel's factor from its four
// nearest half pixels' reservoirs, weighted by how well their surface agrees
// with its own (normal, distance from its plane), each evaluated for its own
// normal: radiance * cos / pi * weight. Without an agreeing neighbour the
// bounce volume at the pixel (the untraced value).
// rtIndirectTemporalCS and rtIndirectAtrousCS (render resolution,
// rt_indirect_denoise, without Ray Reconstruction only): the factor
// accumulated over the frames the pixel's surface stayed (up to 24) and
// filtered by three a-trous passes that stop at normal, plane and luminance
// edges (the luminance's by the history's variance), back into the factor
// image. Ray Reconstruction takes the noisy factor (its guide: independent
// samples, no other denoiser in front of it).
// rtIndirectAddPS: albedo * (saturate(d + indirect) - d) * the fog's share
// over the world colour, d the lighting factor the guide twin shaded with
// (drawn with the volume's strength 0 on a traced frame).
constexpr char kRtIndirectShader[] = R"hlsl(
#include "Rtxdi/RtxdiParameters.h"
#include "Rtxdi/GI/ReSTIRGIParameters.h"
#include "Rtxdi/Utils/RandomSamplerState.hlsli"
cbuffer RtIndirectConstants:register(b1){row_major float4x4 riClipToWorld;row_major float4x4 riWorldToClip;
 row_major float4x4 riPreviousClipToWorld;row_major float4x4 riPreviousWorldToClip;
 float4 riEye;float4 riPreviousEye;float4 riSizeJitter;float4 riPreviousJitter;float4 riSun;float4 riSunDiffuse;
 uint4 riFrame;uint4 riReservoir;uint4 riOffsets;uint4 riCounts;float4 riSpatial;float4 riAo;float4 riReuse;uint4 riLava;float4 riLavaReach;
 uint4 riAoOffsets;}; // half-size AO's selected render pixel: this frame, previous frame
Texture2D<float> riDepth:register(t0);
Texture2D<float4> riGuideNormal:register(t1);
Texture2D<float4> riGuideDiffuse:register(t2);
Texture2D<float4> riGuideSpecular:register(t3);
Texture2D<uint2> riPreviousSurfaces:register(t4);
Texture2D<float2> riObjectMotion:register(t5);
Texture2D<float> riObjectDepth:register(t6);
StructuredBuffer<float2> riNeighbourOffsets:register(t7);
Texture2D<uint2> riSurfaces:register(t8);
Texture3D<float4> riVolumeR:register(t80);
Texture3D<float4> riVolumeG:register(t81);
Texture3D<float4> riVolumeB:register(t82);
SamplerState riVolumeSampler:register(s13);
struct RiCaveExit{float4 centre;float4 across;float4 inwardHeight;float4 colour;};
StructuredBuffer<RiCaveExit> riExits:register(t84);
StructuredBuffer<uint> riLavaTiles:register(t85);
StructuredBuffer<float4> riLavaLevels:register(t86);
RWStructuredBuffer<RTXDI_PackedGIReservoir> riReservoirs:register(u0);
#if defined(RT_INDIRECT_SHADE)
RWTexture2D<float4> riOutput:register(u1);
#elif !defined(RT_INDIRECT_SPATIAL)&&!defined(RT_INDIRECT_DENOISE)
RWTexture2D<uint2> riSurfaceOutput:register(u1);
#ifdef RT_INDIRECT_AO
Texture2D<float2> riAoPrevious:register(t9);
Texture2D<uint2> riAoPreviousSurfaces:register(t10);
RWTexture2D<float2> riAoHistory:register(u2);
RWTexture2D<float2> riAoOutput:register(u3);
RWTexture2D<uint2> riAoSurfaceOutput:register(u4);
#endif
#endif
static const float riPi=3.14159265;
uint2 riHalfSize(){return uint2(riReservoir.w&0xffffu,riReservoir.w>>16);}
// The render pixel a half pixel stands for, this frame or the last (riReuse.y: render pixels a half pixel's side, 2 or 4).
int2 riFull(int2 pixel,bool previous){return min(pixel*int(riReuse.y)+int2(previous?riOffsets.zw:riOffsets.xy),int2(riSizeJitter.xy)-1);}
// The bounce volume for a normal (native_shader.h nativeCaveBounce, the same blend of the active and the world volume).
float3 riVolume(float3 position,float3 normal){
 if(!(rtCaveStrength>0))return 0;
 float4 basis=float4(.886227,1.023328*normal.y,1.023328*normal.z,1.023328*normal.x);
 float3 f=(position-rtCaveOrigin)*rtCaveFineInverseCell,w=(position-rtCaveWorldOrigin)*rtCaveWorldInverseCell;
 float2 inside=min(f.xy,rtCaveFineSize.xy-f.xy);
 float fine=f.z>=0&&f.z<=rtCaveFineSize.z&&all(inside>=0)?1:0;
 if(rtCaveWorldInverseCell>0)fine*=saturate(min(inside.x,inside.y)*rtCaveFineCell*rtCaveWorldInverseCell);
 float world=rtCaveWorldInverseCell>0&&w.z>=0&&w.z<=rtCaveWorldSize.z?1-fine:0;
 float3 e=0;
 for(uint k=0;k<2;++k){
  float weight=k?world:fine;
  if(weight>0){
   float3 size=k?rtCaveWorldSize:rtCaveFineSize;
   float3 uvw=(clamp(k?w:f,.5,size-.5)+float3(0,0,k?rtCaveFineSize.z:0))*rtCaveTexelScale;
   e+=weight*float3(dot(riVolumeR.SampleLevel(riVolumeSampler,uvw,0),basis),
    dot(riVolumeG.SampleLevel(riVolumeSampler,uvw,0),basis),
    dot(riVolumeB.SampleLevel(riVolumeSampler,uvw,0),basis));
  }
 }
 return max(e,0)*rtCaveStrength;
}
// ---- surfaces (the library's bridge)
struct RAB_Surface{float3 worldPos;float3 normal;float3 viewDir;float linearDepth;float roughness;uint flags;};
struct RAB_Material{uint flags;};
RAB_Surface RAB_EmptySurface(){RAB_Surface s=(RAB_Surface)0;return s;}
bool RAB_IsSurfaceValid(RAB_Surface s){return (s.flags&8u)!=0;}
float3 RAB_GetSurfaceWorldPos(RAB_Surface s){return s.worldPos;}
float3 RAB_GetSurfaceNormal(RAB_Surface s){return s.normal;}
float RAB_GetSurfaceLinearDepth(RAB_Surface s){return s.linearDepth;}
RAB_Material RAB_GetMaterial(RAB_Surface s){RAB_Material m;m.flags=s.flags;return m;}
bool RAB_AreMaterialsSimilar(RAB_Material a,RAB_Material b){return a.flags==b.flags;}
uint riPackNormal(float3 n){
 float2 p=n.xy/max(abs(n.x)+abs(n.y)+abs(n.z),1e-6);
 if(n.z<0)p=(1-abs(p.yx))*float2(p.x>=0?1.0:-1.0,p.y>=0?1.0:-1.0);
 uint2 q=uint2(saturate(p*.5+.5)*4095+.5);
 return q.x|(q.y<<12);
}
float3 riUnpackNormal(uint v){
 float2 p=float2(v&4095u,(v>>12)&4095u)/4095.0*2-1;
 float3 n=float3(p,1-abs(p.x)-abs(p.y));
 float t=saturate(-n.z);
 n.x+=n.x>=0?-t:t;n.y+=n.y>=0?-t:t;
 return normalize(n);
}
// A render pixel's surface from its depth and packed word (x the depth's bits, 1: none; y the normal in
// 24 bits, the roughness in 5, the flags: 1 a lit receiver, 2 the terrain's facing rule, 4 a unit).
RAB_Surface riSurfaceAt(uint2 packed,int2 full,bool previous){
 RAB_Surface s=RAB_EmptySurface();
 float z=asfloat(packed.x);
 if(!(z<1))return s;
 float2 uv=(float2(full)+.5-(previous?riPreviousJitter.xy:riSizeJitter.zw))/riSizeJitter.xy;
 float4 clip=float4(uv.x*2-1,1-uv.y*2,z,1);
 float4 world=previous?mul(riPreviousClipToWorld,clip):mul(riClipToWorld,clip);
 s.worldPos=world.xyz/world.w;
 s.linearDepth=previous?mul(riPreviousWorldToClip,float4(s.worldPos,1)).w:mul(riWorldToClip,float4(s.worldPos,1)).w;
 s.viewDir=normalize((previous?riPreviousEye.xyz:riEye.xyz)-s.worldPos);
 s.normal=riUnpackNormal(packed.y);
 s.roughness=float((packed.y>>24)&31u)/31.0;
 s.flags=(packed.y>>29)|8u;
 return s;
}
// The packed word of a render pixel of this frame's guide targets; a surface the lamps do not light takes no bounce either.
uint2 riPackPixel(int2 full){
 float z=riDepth.Load(int3(full,0));
 float4 n=riGuideNormal.Load(int3(full,0));
 uint a=uint(riGuideDiffuse.Load(int3(full,0)).a*65535+.5),b=uint(riGuideSpecular.Load(int3(full,0)).a*65535+.5);
 if(!(z<1)||!(dot(n.xyz,n.xyz)>.25)||((b>>7)&255u)==0||((a>>14)&1u)==0)return uint2(asuint(1.0),0);
 uint flags=((a>>14)&3u)|(((b>>15)&1u)<<2);
 return uint2(asuint(z),riPackNormal(normalize(n.xyz))|(uint(saturate(n.w)*31+.5)<<24)|(flags<<29));
}
// By half pixel.
RAB_Surface RAB_GetGBufferSurface(int2 pixel,bool previousFrame){
 if(any(pixel<0)||any(pixel>=int2(riHalfSize()))||(previousFrame&&(riFrame.y&1u)==0))return RAB_EmptySurface();
 uint2 packed=previousFrame?riPreviousSurfaces.Load(int3(pixel,0)):riSurfaces.Load(int3(pixel,0));
 return riSurfaceAt(packed,riFull(pixel,previousFrame),previousFrame);
}
int2 RAB_ClampSamplePositionIntoView(int2 pixel,bool previousFrame){return clamp(pixel,int2(0,0),int2(riHalfSize())-1);}
float RAB_GetGISampleTargetPdfForSurface(float3 samplePosition,float3 sampleRadiance,RAB_Surface surface){
 float3 toSample=samplePosition-surface.worldPos;
 float cosine=saturate(dot(surface.normal,toSample*rsqrt(max(dot(toSample,toSample),1e-12))));
 return dot(sampleRadiance,float3(.2126,.7152,.0722))*cosine/riPi;
}
bool RAB_ValidateGISampleWithJacobian(inout float jacobian){
 if(!(jacobian>.1)||jacobian>10)return false;
 jacobian=clamp(jacobian,1.0/3,3.0);
 return true;
}
float riSelfHit(RAB_Surface surface){return riPreviousJitter.z+riPreviousJitter.w*length(surface.worldPos-riEye.xyz)+((surface.flags&4u)!=0?.25:0);}
// Nothing between a surface and a sample point on another surface.
bool riVisible(RAB_Surface surface,float3 position){
 float3 toSample=position-surface.worldPos;
 float distance=length(toSample),nearest=riSelfHit(surface),farthest=distance*.99-.02;
 if(!(farthest>nearest))return true;
 return rtVisible(surface.worldPos,toSample/distance,nearest,farthest)>.5;
}
)hlsl" R"hlsl(
#define RTXDI_GI_ALLOWED_BIAS_CORRECTION RTXDI_BIAS_CORRECTION_BASIC
#define RTXDI_GI_RESERVOIR_BUFFER riReservoirs
#define RTXDI_NEIGHBOR_OFFSETS_BUFFER riNeighbourOffsets
#define RTXDI_ENABLE_BOILING_FILTER
#define RTXDI_BOILING_FILTER_GROUP_SIZE 8
#include "Rtxdi/GI/Reservoir.hlsli"
#include "Rtxdi/GI/TemporalResampling.hlsli"
#include "Rtxdi/GI/BoilingFilter.hlsli"
#include "Rtxdi/GI/SpatialResampling.hlsli"
RTXDI_RuntimeParameters riRuntime(){RTXDI_RuntimeParameters p;p.neighborOffsetMask=riReservoir.z;p.activeCheckerboardField=0;p.frameIndex=riFrame.x;p.pad1=0;return p;}
RTXDI_ReservoirBufferParameters riBuffer(){RTXDI_ReservoirBufferParameters p;p.reservoirBlockRowPitch=riReservoir.x;p.reservoirArrayPitch=riReservoir.y;p.pad1=0;p.pad2=0;return p;}
#if defined(RT_INDIRECT_SHADE)
// Each lit pixel's factor luminance in its 8x8 group (-1: none), for the outlier clamp.
groupshared float riGroupLuma[64];
[numthreads(8,8,1)]void rtIndirectShadeCS(uint3 id:SV_DispatchThreadID,uint groupIndex:SV_GroupIndex){
 const bool inside=all(id.xy<uint2(riSizeJitter.xy));
 int2 pixel=int2(id.xy);
 float3 indirect=0;
 RAB_Surface surface=RAB_EmptySurface();
 if(inside)surface=riSurfaceAt(riPackPixel(pixel),pixel,false);
 if(RAB_IsSurfaceValid(surface)){
  float2 at=(float2(pixel)-float2(riOffsets.xy))/riReuse.y;
  int2 base=int2(floor(at));
  float2 f=at-float2(base);
  float reach=.05+.02*surface.linearDepth,total=0;
  // At the full render size (riReuse.y 1) a pixel has its own reservoir.
  for(uint k=0;k<(riReuse.y>1?4u:1u);++k){
   int2 q=base+int2(k&1u,k>>1);
   RAB_Surface other=RAB_GetGBufferSurface(q,false);
   if(!RAB_IsSurfaceValid(other))continue;
   float weight=((k&1u)!=0?f.x:1-f.x)*((k>>1)!=0?f.y:1-f.y)+.02;
   float facing=saturate(dot(surface.normal,other.normal));
   weight*=facing*facing*facing*facing*exp(-abs(dot(other.worldPos-surface.worldPos,surface.normal))/reach);
   if(!(weight>1e-4))continue;
   total+=weight;
   uint misc;
   RTXDI_GIReservoir reservoir=RTXDI_LoadGIReservoir(riBuffer(),uint2(q),0,misc);
   if(!RTXDI_IsValidGIReservoir(reservoir)||(misc&0x10000u)!=0||!(reservoir.weightSum>0))continue;
   float3 toSample=reservoir.position-surface.worldPos;
   float cosine=saturate(dot(surface.normal,toSample*rsqrt(max(dot(toSample,toSample),1e-12))));
   indirect+=reservoir.radiance*(weight*cosine/riPi*reservoir.weightSum);
  }
  indirect=total>0?indirect/total:riVolume(surface.worldPos,surface.normal);
  if(any(isnan(indirect))||any(isinf(indirect)))indirect=0;
  indirect=clamp(indirect,0,2);
 }
 // Under Ray Reconstruction (riReuse.z, the multiple; 0: off): a pixel whose bounce light exceeds
 // riReuse.z times its group's mean is scaled down to it. A rare bright sample stays in its half
 // pixel for frames, and Ray Reconstruction keeps it as a bright dot that changes every frame.
 // Every thread of the group reaches the barrier.
 float luma=dot(indirect,float3(.2126,.7152,.0722));
 riGroupLuma[groupIndex]=RAB_IsSurfaceValid(surface)?luma:-1;
 GroupMemoryBarrierWithGroupSync();
 if(riReuse.z>0&&luma>0){
  float sum=0,count=0;
  for(uint k=0;k<64;++k){float v=riGroupLuma[k];if(v>=0){sum+=v;count+=1;}}
  float limit=riReuse.z*sum/max(count,1);
  if(luma>limit)indirect*=limit/luma;
 }
 if(inside)riOutput[pixel]=float4(indirect,1);
}
)hlsl" R"hlsl(
#elif defined(RT_INDIRECT_DENOISE)
// rt_indirect_denoise (without Ray Reconstruction): the shading pass's factor
// accumulated over frames and filtered in space (a temporal pass and three
// edge-aware a-trous passes, as SVGF and NRD ReLAX do for the diffuse term).
// dnStep.x: the a-trous step in render pixels (0: the temporal pass); y: 1 on
// the last step (into the factor image); z: the history's greatest length.
// dnRay: a render pixel's view ray scaled to one unit of linear depth, eye +
// depth * (dnRay[0] + u * dnRay[1] + v * dnRay[2]) (u, v the unjittered
// screen position, 0-1); dnRay[3].xyz the eye. dnPreviousRay: last frame's.
cbuffer RtIndirectDenoise:register(b2){uint4 dnStep;float4 dnRay[4];float4 dnPreviousRay[4];};
Texture2D<float4> dnInput:register(t10);
Texture2D<float4> dnHistory:register(t11);
Texture2D<float2> dnMoments:register(t12);
Texture2D<uint2> dnPackedBefore:register(t13);
Texture2D<uint2> dnPacked:register(t14);
RWTexture2D<float4> dnOutput:register(u1);
#ifdef RT_INDIRECT_TEMPORAL
RWTexture2D<float4> dnHistoryOutput:register(u2);
RWTexture2D<float2> dnMomentsOutput:register(u3);
RWTexture2D<uint2> dnPackedOutput:register(u4);
#endif
float dnLuma(float3 c){return dot(c,float3(.2126,.7152,.0722));}
// A render pixel's surface for the denoiser: x the linear depth (0: none), y the guide pixel's normal and flags.
struct DnSurface{float3 position;float3 normal;float depth;};
DnSurface dnSurface(uint2 packed,int2 pixel,bool previous){
 DnSurface s;s.depth=asfloat(packed.x);s.normal=riUnpackNormal(packed.y);
 float2 uv=(float2(pixel)+.5-(previous?riPreviousJitter.xy:riSizeJitter.zw))/riSizeJitter.xy;
 float3 ray=previous?dnPreviousRay[0].xyz+uv.x*dnPreviousRay[1].xyz+uv.y*dnPreviousRay[2].xyz:dnRay[0].xyz+uv.x*dnRay[1].xyz+uv.y*dnRay[2].xyz;
 s.position=(previous?dnPreviousRay[3].xyz:dnRay[3].xyz)+s.depth*ray;
 return s;
}
#ifdef RT_INDIRECT_TEMPORAL
[numthreads(8,8,1)]void rtIndirectTemporalCS(uint3 id:SV_DispatchThreadID){
 if(any(id.xy>=uint2(riSizeJitter.xy)))return;
 int2 pixel=int2(id.xy);
 RAB_Surface surface=riSurfaceAt(riPackPixel(pixel),pixel,false);
 if(!RAB_IsSurfaceValid(surface)){
  dnPackedOutput[pixel]=uint2(0,0);dnHistoryOutput[pixel]=0;dnMomentsOutput[pixel]=0;dnOutput[pixel]=0;return;
 }
 dnPackedOutput[pixel]=uint2(asuint(surface.linearDepth),riPackNormal(surface.normal));
 float3 now=dnInput.Load(int3(pixel,0)).rgb;
 float luma=dnLuma(now);
 // Last frame's place of the pixel (riMotion's rule at the render size), its four pixels around it whose surface agrees.
 float4 history=0;float2 moments=0;float total=0;
 if((riFrame.y&1u)!=0){
  float2 unjittered=float2(pixel)+.5-riSizeJitter.zw,before=unjittered;
  float4 clip=mul(riPreviousWorldToClip,float4(surface.worldPos,1));
  if(clip.w>1e-6)before=float2(clip.x/clip.w*.5+.5,.5-clip.y/clip.w*.5)*riSizeJitter.xy;
  if((riFrame.y&2u)!=0){
   float written=riObjectDepth.Load(int3(pixel,0));
   if(written<0||abs(written-riDepth.Load(int3(pixel,0)))<=1e-6)before=unjittered+riObjectMotion.Load(int3(pixel,0));
  }
  float2 at=before+riPreviousJitter.xy-.5;
  int2 base=int2(floor(at));
  float2 f=at-float2(base);
  float reach=.05+.02*surface.linearDepth;
  for(uint k=0;k<4;++k){
   int2 q=base+int2(k&1u,k>>1);
   if(any(q<0)||any(q>=int2(riSizeJitter.xy)))continue;
   uint2 packed=dnPackedBefore.Load(int3(q,0));
   if(!(asfloat(packed.x)>0))continue;
   DnSurface was=dnSurface(packed,q,true);
   if(dot(was.normal,surface.normal)<.9||abs(dot(was.position-surface.worldPos,surface.normal))>reach)continue;
   float weight=((k&1u)!=0?f.x:1-f.x)*((k>>1)!=0?f.y:1-f.y)+1e-3;
   history+=dnHistory.Load(int3(q,0))*weight;moments+=dnMoments.Load(int3(q,0))*weight;total+=weight;
  }
 }
 float frames=0;
 if(total>0){history/=total;moments/=total;frames=history.a;}
 frames=min(frames+1,float(dnStep.z));
 float3 colour=lerp(history.rgb,now,1/frames);
 moments=lerp(moments,float2(luma,luma*luma),max(1/frames,.2));
 // A short history's variance is not known yet: the a-trous passes then filter by the surface alone.
 float variance=frames<4?1:max(moments.y-moments.x*moments.x,0);
 if(any(isnan(colour))||any(isinf(colour))){colour=now;frames=1;moments=float2(luma,luma*luma);variance=1;}
 dnHistoryOutput[pixel]=float4(colour,frames);dnMomentsOutput[pixel]=moments;dnOutput[pixel]=float4(colour,variance);
}
#else
// The same 5x5 filter, but decode each tile surface once, not once per tap
// per output pixel. Specialising the three strides keeps the shared allocation
// at 9.4 / 15 / 30 KiB, rather than reserving the largest halo for every pass.
#define DN_TILE_WIDTH (16+4*RT_INDIRECT_ATROUS_STEP)
#define DN_TILE_HEIGHT (8+4*RT_INDIRECT_ATROUS_STEP)
groupshared float4 dnTileColour[DN_TILE_WIDTH*DN_TILE_HEIGHT];
groupshared float3 dnTileNormal[DN_TILE_WIDTH*DN_TILE_HEIGHT];
groupshared float3 dnTilePosition[DN_TILE_WIDTH*DN_TILE_HEIGHT];
[numthreads(16,8,1)]void rtIndirectAtrousCS(uint3 id:SV_DispatchThreadID,uint3 local:SV_GroupThreadID,uint groupIndex:SV_GroupIndex){
 const int border=2*RT_INDIRECT_ATROUS_STEP;
 int2 origin=int2(id.xy)-int2(local.xy)-border,last=int2(riSizeJitter.xy)-1;
 for(uint k=groupIndex;k<DN_TILE_WIDTH*DN_TILE_HEIGHT;k+=128u){
  int2 q=clamp(origin+int2(k%DN_TILE_WIDTH,k/DN_TILE_WIDTH),0,last);
  uint2 p=dnPacked.Load(int3(q,0));
  DnSurface s=(DnSurface)0;
  if(asfloat(p.x)>0)s=dnSurface(p,q,false);
  dnTileColour[k]=dnInput.Load(int3(q,0));
  dnTileNormal[k]=s.normal;
  dnTilePosition[k]=s.position;
 }
 GroupMemoryBarrierWithGroupSync(); // including lanes outside an odd-sized image
 if(any(id.xy>=uint2(riSizeJitter.xy)))return;
 int2 pixel=int2(id.xy);
 int centreIndex=(int(local.y)+border)*DN_TILE_WIDTH+int(local.x)+border;
 float4 centre=dnTileColour[centreIndex];
 float3 normal=dnTileNormal[centreIndex],position=dnTilePosition[centreIndex];
 if(!any(normal)){dnOutput[pixel]=dnStep.y!=0?float4(0,0,0,1):0;return;}
 float depth=asfloat(dnPacked.Load(int3(pixel,0)).x);
 // The luminance's stopping width from the variance's 3x3 mean.
 float variance=0;
 [unroll]
 for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x)
  variance+=dnTileColour[centreIndex+y*DN_TILE_WIDTH+x].a*(x==0?.5:.25)*(y==0?.5:.25);
 float width=4*sqrt(max(variance,0))+1e-3,luma=dnLuma(centre.rgb);
 float reach=(.05+.02*depth)*float(RT_INDIRECT_ATROUS_STEP);
 const float kernel[3]={.375,.25,.0625};
 float3 sum=0;float sumVariance=0,total=0;
 [unroll]
 for(int j=-2;j<=2;++j)for(int i=-2;i<=2;++i){
  int2 q=pixel+int2(i,j)*RT_INDIRECT_ATROUS_STEP;
  if(any(q<0)||any(q>last))continue;
  int tap=centreIndex+(j*DN_TILE_WIDTH+i)*RT_INDIRECT_ATROUS_STEP;
  float3 otherNormal=dnTileNormal[tap];
  if(!any(otherNormal))continue;
  float4 c=dnTileColour[tap];
  float facing=saturate(dot(normal,otherNormal));
  float weight=kernel[abs(i)]*kernel[abs(j)]*pow(facing,32)*
   exp(-abs(dot(dnTilePosition[tap]-position,normal))/reach-abs(dnLuma(c.rgb)-luma)/width);
  sum+=c.rgb*weight;sumVariance+=c.a*weight*weight;total+=weight;
 }
 float4 result=total>0?float4(sum/total,sumVariance/(total*total)):centre;
 if(dnStep.y!=0)result.a=1;
 dnOutput[pixel]=result;
}
#endif
)hlsl" R"hlsl(
#elif defined(RT_INDIRECT_SPATIAL)
[numthreads(8,8,1)]void rtIndirectSpatialCS(uint3 id:SV_DispatchThreadID,uint3 local:SV_GroupThreadID){
 bool inside=all(id.xy<riHalfSize());
 int2 pixel=int2(id.xy);
 RAB_Surface surface=RAB_EmptySurface();
 if(inside)surface=RAB_GetGBufferSurface(pixel,false);
 RTXDI_GIReservoir reservoir=RTXDI_EmptyGIReservoir();
 uint misc=0;
 if(RAB_IsSurfaceValid(surface)){
  reservoir=RTXDI_LoadGIReservoir(riBuffer(),id.xy,1);
  if(riCounts.z!=0){
   RTXDI_RandomSamplerState rng=RTXDI_InitRandomSampler(id.xy,riFrame.x,RTXDI_GI_SPATIAL_RESAMPLING_RANDOM_SEED);
   RTXDI_GISpatialResamplingParameters spatial=(RTXDI_GISpatialResamplingParameters)0;
   spatial.depthThreshold=riSpatial.y;spatial.normalThreshold=riSpatial.z;spatial.numSamples=riCounts.z;
   spatial.samplingRadius=riSpatial.x;spatial.biasCorrectionMode=RTXDI_BIAS_CORRECTION_BASIC;
   reservoir=RTXDI_GISpatialResampling(id.xy,surface,1,reservoir,rng,riRuntime(),riBuffer(),spatial);
  }
 }
 // The library's boiling filter once more, on the reservoirs the shading pass reads (riAo.w; every thread of
 // the group reaches it: it synchronises the group).
 if(riAo.w>0)RTXDI_GIBoilingFilter(local.xy,riAo.w,reservoir);
 if(RAB_IsSurfaceValid(surface)&&(riFrame.y&8u)!=0&&RTXDI_IsValidGIReservoir(reservoir)&&reservoir.weightSum>0&&any(reservoir.radiance>0)){
  if(!riVisible(surface,reservoir.position))misc=0x10000u;
 }
 if(inside)RTXDI_StoreGIReservoir(reservoir,misc,riBuffer(),id.xy,0);
}
#else
// The pixel's place in last frame's reservoirs (half pixels, from this half pixel) and its depth change: the
// camera's, or the object motion a posed record wrote there (the direct light passes' rule, by half pixel).
float3 riMotion(int2 pixel,RAB_Surface s){
 int2 full=riFull(pixel,false);
 float2 unjittered=float2(full)+.5-riSizeJitter.zw,before=unjittered;float change=0;
 float4 clip=mul(riPreviousWorldToClip,float4(s.worldPos,1));
 if(clip.w>1e-6){before=float2(clip.x/clip.w*.5+.5,.5-clip.y/clip.w*.5)*riSizeJitter.xy;change=clip.w-s.linearDepth;}
 if((riFrame.y&2u)!=0){
  float written=riObjectDepth.Load(int3(full,0));
  if(written<0||abs(written-riDepth.Load(int3(full,0)))<=1e-6){before=unjittered+riObjectMotion.Load(int3(full,0));change=0;}
 }
 return float3((before+riPreviousJitter.xy-.5-float2(riOffsets.zw))/riReuse.y-float2(pixel),change);
}
// What lights a hit, without the ambient light: the sun, one lamp of its place, the bounce volume.
float3 riHitLight(RtHit h,inout RTXDI_RandomSamplerState rng){
 float3 light=riVolume(h.position,h.normal);
 float facing=dot(h.normal,riSun.xyz);
 if(riSun.w>.5&&facing>0)light+=riSunDiffuse.rgb*(facing*rtVisible(h.position+h.normal*.03,riSun.xyz,.02,100000));
 float2 cell=floor((h.position.xy-rtLightGridOrigin)*rtLightGridInverseCell);
 if(rtLightCount==0||!all(cell>=0)||!all(cell<(float2)rtLightGridSize))return light;
 uint index=(uint)cell.y*rtLightGridSize.x+(uint)cell.x;
 uint first=rtLightGrid[index],count=rtLightGrid[index+1]-first;
 if(count==0||count>=0x10000u)return light;
 uint tries=min(count,riCounts.w);
 float sum=0,keptWeight=0;float3 kept=0,target=0;bool shadowed=false;
 for(uint k=0;k<tries;++k){
  float pick=(RTXDI_GetNextRandom(rng)+k)/tries;
  RtPointLight lamp=rtLights[rtLightGrid[first+min(uint(pick*count),count-1)]];
  float3 delta=lamp.centerInverseRadiusSquared.xyz-h.position;
  float reach=1-dot(delta,delta)*lamp.centerInverseRadiusSquared.w;
  if(!(reach>0))continue;
  float incidence=dot(h.normal,delta*rsqrt(max(dot(delta,delta),1e-12)));
  // The lamp loop's facing rule (native_shader.h nativePointLighting): the ground keeps grazing light.
  float facing=h.kind==1?saturate(1+min(incidence,0)):max(incidence,0);
  float3 response=lamp.color.rgb*(reach*reach*(lamp.color.w<0?saturate(incidence*.6+.4):facing*(lamp.color.w>0?.65:1)));
  float weight=dot(response,float3(.2126,.7152,.0722));
  if(!(weight>0))continue;
  sum+=weight;
  if(RTXDI_GetNextRandom(rng)*sum<weight){
   kept=response;keptWeight=weight;shadowed=!(lamp.color.w<0);
   target=lamp.color.w>0?lamp.shadowCenter.xyz:lamp.centerInverseRadiusSquared.xyz;
  }
 }
 if(!(keptWeight>0))return light;
 if(shadowed){
  float3 toLamp=target-h.position;
  float distance=length(toLamp),farthest=distance-riEye.w;
  if(farthest>.03){
   if(rtVisible(h.position+h.normal*.03,toLamp/distance,.02,farthest)<.5)return light;
  }
 }
 return light+kept*(sum/keptWeight*float(count)/float(tries));
}
// The nearest cave exit rectangle a ray crosses outward before limit (rt_indirect_cave_exits).
bool riExit(float3 origin,float3 direction,inout float limit,out float3 colour,out float3 normal){
 colour=0;normal=0;bool found=false;
 uint count=min(riFrame.z,16u);
 for(uint n=0;n<count;++n){
  RiCaveExit e=riExits[n];
  float3 inward=float3(e.inwardHeight.xy,0);
  float toward=dot(direction,inward);
  if(!(toward<-1e-4))continue;
  float t=dot(e.centre.xyz-origin,inward)/toward;
  if(!(t>0)||!(t<limit))continue;
  float3 q=origin+direction*t-e.centre.xyz;
  float along=dot(q.xy,e.across.xy)/max(dot(e.across.xy,e.across.xy),1e-6);
  if(abs(along)>1||q.z<0||q.z>e.inwardHeight.z)continue;
  limit=t;colour=max(e.colour.rgb,0);normal=inward;found=true;
 }
 return found;
}
// rt_emissive: the nearest lava surface a ray crosses from above before limit. The active map's lava
// is flat 2 m tiles at a few levels (riLava: tiles across, tiles down, levels; a tile's word its
// level's index plus one; a level its height and the lava's rendered colour).
bool riLavaCrossed(float3 origin,float3 direction,inout float limit,out float3 colour){
 colour=0;bool found=false;
 if(!(direction.z<-1e-4))return false;
 for(uint n=0;n<riLava.z;++n){
  float4 level=riLavaLevels[n];
  float t=(level.x-origin.z)/direction.z;
  if(!(t>0)||!(t<limit))continue;
  float2 tile=(origin.xy+direction.xy*t)*.5;
  if(any(tile<0)||any(tile>=float2(riLava.xy)))continue;
  if(riLavaTiles[uint(tile.y)*riLava.x+uint(tile.x)]!=n+1)continue;
  limit=t;colour=level.yzw;found=true;
 }
 return found;
}
[numthreads(8,8,1)]void rtIndirectSampleCS(uint3 id:SV_DispatchThreadID,uint3 local:SV_GroupThreadID){
 int2 pixel=int2(id.xy);
 bool inside=all(id.xy<riHalfSize());
 RAB_Surface surface=RAB_EmptySurface();
 if(inside){
  int2 full=riFull(pixel,false);
  uint2 packed=riPackPixel(full);
  surface=riSurfaceAt(packed,full,false);
  riSurfaceOutput[pixel]=packed;
 }
 RTXDI_GIReservoir reservoir=RTXDI_EmptyGIReservoir();
 float3 motion=0;
#ifdef RT_INDIRECT_AO
 float aoSample=-1; // none: no ray this frame
#endif
 if(RAB_IsSurfaceValid(surface)){
  if((riFrame.y&1u)!=0)motion=riMotion(pixel,surface);
  RTXDI_RandomSamplerState rng=RTXDI_InitRandomSampler(id.xy,riFrame.x,RTXDI_GI_GENERATE_INITIAL_SAMPLES_RANDOM_SEED);
  // Cosine-distributed around the normal: the normal plus a point of the unit sphere.
  float z=1-2*RTXDI_GetNextRandom(rng),ring=sqrt(saturate(1-z*z)),angle=6.2831853*RTXDI_GetNextRandom(rng);
  float3 direction=surface.normal+float3(ring*cos(angle),ring*sin(angle),z);
  float cosine=0;
  if(dot(direction,direction)>1e-6){direction=normalize(direction);cosine=dot(surface.normal,direction);}
  if(cosine>1e-3){
   float3 radiance=0,position=surface.worldPos+direction*1000,normal=-direction;
   // The terrain's undersides culled: a pixel drawn under the scene's flat terrain (tessellated relief) met
   // the underside of its own ground a few centimetres away, and that hit's sun and lamp rays (which cull
   // undersides) left through the ground: sun-lit bounce light in full shade, in pools.
   RtHit h=rtTrace(surface.worldPos,direction,riSelfHit(surface),100000,RAY_FLAG_CULL_BACK_FACING_TRIANGLES);
#ifdef RT_INDIRECT_AO
   // The long reach is the sky's: only a ray that goes up can lose it. A ray
   // that goes level or down meets the ground, which hides no sky and sends
   // its own light back (the bounce above): it occludes at contact reach only.
   {float reach=saturate(h.t/lerp(riAo.z,riAo.x,saturate(direction.z*4)));aoSample=h.hit&&!h.under?reach*reach:1;}
#endif
   float limit=h.hit?h.t:1e30;
   float3 exitColour=0,exitNormal=0;bool left=false;
   if((riFrame.y&4u)!=0)left=riExit(surface.worldPos,direction,limit,exitColour,exitNormal);
   if(left){
    radiance=exitColour;position=surface.worldPos+direction*limit;normal=exitNormal;
   }else if((riFrame.y&32u)==0){
    // rt_emissive: lava the ray reaches before anything else sends its own colour, weighed in only
    // past the reach of the lava's glow lights (riLavaReach: where it starts, one over its width):
    // nearer, those lights are the lava's light and the hit below it counts as before.
    float3 lava=0;float share=0;
    if(riLava.z!=0&&riLavaCrossed(surface.worldPos,direction,limit,lava))share=saturate((limit-riLavaReach.x)*riLavaReach.y);
    if(h.hit&&share<1){radiance=h.albedo*saturate(riHitLight(h,rng)+h.emissive);position=h.position;normal=h.normal;}
    if(share>0){radiance=lerp(radiance,lava,share);position=surface.worldPos+direction*limit;normal=float3(0,0,1);}
   }
   if(any(isnan(radiance))||any(isinf(radiance)))radiance=0;
   reservoir=RTXDI_MakeGIReservoir(position,normal,radiance,cosine/riPi);
   reservoir.M=uint(max(riReuse.x,1)+.5);
  }
  if((riFrame.y&33u)==1u){
   RTXDI_GITemporalResamplingParameters temporal=(RTXDI_GITemporalResamplingParameters)0;
   temporal.depthThreshold=riSpatial.y;temporal.normalThreshold=riSpatial.z;temporal.maxHistoryLength=riCounts.x;
   temporal.enableFallbackSampling=1;temporal.biasCorrectionMode=RTXDI_BIAS_CORRECTION_BASIC;temporal.maxReservoirAge=riCounts.y;
   temporal.enablePermutationSampling=1;temporal.uniformRandomNumber=riFrame.w;
   RTXDI_RandomSamplerState temporalRng=RTXDI_InitRandomSampler(id.xy,riFrame.x,RTXDI_GI_TEMPORAL_RESAMPLING_RANDOM_SEED);
   // Under Ray Reconstruction (riReuse.w, the radius in traced pixels; 0: off): the reprojected place moved by a
   // random offset within the radius, so neighbouring pixels do not keep the same history sample (RTX Remix's
   // DLSS-RR compatibility mode; the library's fallback sample stays at the pixel itself).
   float3 temporalMotion=motion;
   if(riReuse.w>0){
    float angle=6.2831853*RTXDI_GetNextRandom(temporalRng),radius=riReuse.w*sqrt(RTXDI_GetNextRandom(temporalRng));
    temporalMotion.xy+=float2(cos(angle),sin(angle))*radius;
   }
   reservoir=RTXDI_GITemporalResampling(id.xy,surface,temporalMotion,0,reservoir,temporalRng,riRuntime(),riBuffer(),temporal);
  }
 }
 RTXDI_GIBoilingFilter(local.xy,riSpatial.w,reservoir);
 if(inside&&(riFrame.y&32u)==0)RTXDI_StoreGIReservoir(reservoir,riBuffer(),id.xy,1);
#ifdef RT_INDIRECT_AO
 // Full-size bounce already traced the same cosine-distributed visibility estimator.
 // Only the selected pixel of each AO quad updates its half-size history. The
 // bounce RNG and reservoirs stay untouched; AO takes that pixel's fresh ray.
 bool separateAoGrid=riReuse.y==1;
 int2 aoPixel=separateAoGrid?pixel/2:pixel;
 int2 aoLast=int2(riSizeJitter.xy)-1;
 bool aoSelected=!separateAoGrid||all(pixel==min(aoPixel*2+int2(riAoOffsets.xy),aoLast));
 if(inside&&aoSelected){
  int2 aoSize=separateAoGrid?(int2(riSizeJitter.xy)+1)/2:int2(riHalfSize());
  float2 aoMotion=separateAoGrid?(float2(pixel)+motion.xy-float2(riAoOffsets.zw))*.5-float2(aoPixel):motion.xy;
  if(separateAoGrid)riAoSurfaceOutput[aoPixel]=riPackPixel(pixel);
  float2 history=float2(1,0),shown=float2(1,65000); // no lit surface: unoccluded, the ambient occlusion passes' sky depth
  // The cover (both the terrain and the unit flag) keeps the sky's value: its blades are not in the scene,
  // and the screen-space contact layer is its occlusion.
  if(RAB_IsSurfaceValid(surface)&&(surface.flags&6u)!=6u){
   float sum=0,frames=0,total=0;
   if((riFrame.y&16u)!=0){
    float2 at=float2(aoPixel)+aoMotion;
    int2 base=int2(floor(at));
    float2 f=at-float2(base);
    for(uint k=0;k<4;++k){
     int2 q=base+int2(k&1u,k>>1);
     if(any(q<0)||any(q>=aoSize))continue;
     uint2 beforePacked=separateAoGrid?riAoPreviousSurfaces.Load(int3(q,0)):riPreviousSurfaces.Load(int3(q,0));
     int2 beforeFull=separateAoGrid?min(q*2+int2(riAoOffsets.zw),aoLast):riFull(q,true);
     RAB_Surface before=riSurfaceAt(beforePacked,beforeFull,true);
     if(!RAB_IsSurfaceValid(before)||dot(before.normal,surface.normal)<.9||
        abs(before.linearDepth-(surface.linearDepth+motion.z))>riSpatial.y*surface.linearDepth)continue;
     float weight=((k&1u)!=0?f.x:1-f.x)*((k>>1)!=0?f.y:1-f.y)+.01;
     float2 was=riAoPrevious.Load(int3(q,0));
     sum+=was.x*weight;frames+=was.y*weight;total+=weight;
    }
   }
   if(total>0){sum/=total;frames/=total;}
   history=float2(total>0?sum:1,frames);
   if(aoSample>=0){
    frames=min(frames+1,riAo.y);
    history=float2(lerp(sum,aoSample,1/frames),frames);
   }
   if(any(isnan(history)))history=float2(1,0);
   shown=float2(saturate(history.x),surface.linearDepth);
  }
  riAoHistory[aoPixel]=history;riAoOutput[aoPixel]=shown;
 }
#endif
}
#endif
)hlsl";
constexpr char kRtIndirectAddShader[] =
"Texture2D<float4> riIndirect:register(t0);\n"
"Texture2D<float4> riGuideDiffuse:register(t1);\n"
"Texture2D<float4> riGuideSpecular:register(t2);\n"
"float4 rtIndirectAddPS(float4 position:SV_Position):SV_Target{\n"
" int3 at=int3(position.xy,0);\n"
" float4 diffuse=riGuideDiffuse.Load(at);\n"
" uint a=uint(diffuse.a*65535+.5),b=uint(riGuideSpecular.Load(at).a*65535+.5);\n"
" float3 factor=float3(a&127u,(a>>7)&127u,b&127u)/127.0;\n"
" return float4(diffuse.rgb*(saturate(factor+riIndirect.Load(at).rgb)-factor)*(float((b>>7)&255u)/255.0),0);\n"
"}\n";
const std::string& rt_indirect_source(){
 static const std::string source=std::string(kRaytracingHitShader)+kRtIndirectShader;
 return source;
}
// append_rt_indirect_shader_programs order.
enum : std::size_t { kRtIndirectSample, kRtIndirectSpatial, kRtIndirectShade, kRtIndirectAdd, kRtIndirectSampleAo, kRtIndirectTemporal, kRtIndirectAtrous, kRtIndirectAtrous2, kRtIndirectAtrous4, kRtIndirectPrograms };
void append_rt_indirect_shader_programs(std::vector<ShaderProgram>& out){
 static const D3D_SHADER_MACRO sample[]={{"RTXDI_SDK","1"},{nullptr,nullptr}};
 static const D3D_SHADER_MACRO spatial[]={{"RTXDI_SDK","1"},{"RT_INDIRECT_SPATIAL","1"},{nullptr,nullptr}};
 static const D3D_SHADER_MACRO shade[]={{"RTXDI_SDK","1"},{"RT_INDIRECT_SHADE","1"},{nullptr,nullptr}};
 static const D3D_SHADER_MACRO sample_ao[]={{"RTXDI_SDK","1"},{"RT_INDIRECT_AO","1"},{nullptr,nullptr}}; // rt_ambient_occlusion: the sampling pass with its ray's occlusion
 static const D3D_SHADER_MACRO temporal[]={{"RTXDI_SDK","1"},{"RT_INDIRECT_DENOISE","1"},{"RT_INDIRECT_TEMPORAL","1"},{nullptr,nullptr}}; // rt_indirect_denoise
 static const D3D_SHADER_MACRO atrous[]={{"RTXDI_SDK","1"},{"RT_INDIRECT_DENOISE","1"},{"RT_INDIRECT_ATROUS_STEP","1"},{nullptr,nullptr}};
 static const D3D_SHADER_MACRO atrous2[]={{"RTXDI_SDK","1"},{"RT_INDIRECT_DENOISE","1"},{"RT_INDIRECT_ATROUS_STEP","2"},{nullptr,nullptr}};
 static const D3D_SHADER_MACRO atrous4[]={{"RTXDI_SDK","1"},{"RT_INDIRECT_DENOISE","1"},{"RT_INDIRECT_ATROUS_STEP","4"},{nullptr,nullptr}};
 const std::string& source=rt_indirect_source();
 out.push_back({{source.data(),source.size()},nullptr,sample,"rtIndirectSampleCS","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 out.push_back({{source.data(),source.size()},nullptr,spatial,"rtIndirectSpatialCS","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 out.push_back({{source.data(),source.size()},nullptr,shade,"rtIndirectShadeCS","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 out.push_back({{kRtIndirectAddShader,sizeof(kRtIndirectAddShader)-1},nullptr,nullptr,"rtIndirectAddPS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 out.push_back({{source.data(),source.size()},nullptr,sample_ao,"rtIndirectSampleCS","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 out.push_back({{source.data(),source.size()},nullptr,temporal,"rtIndirectTemporalCS","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 out.push_back({{source.data(),source.size()},nullptr,atrous,"rtIndirectAtrousCS","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 out.push_back({{source.data(),source.size()},nullptr,atrous2,"rtIndirectAtrousCS","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 out.push_back({{source.data(),source.size()},nullptr,atrous4,"rtIndirectAtrousCS","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
}
// rt_indirect_denoise's b2 (kRtIndirectShader RtIndirectDenoise).
struct RtIndirectDenoiseConstants{
 std::uint32_t step[4];     // the a-trous step (0: the temporal pass), 1 on the last, the history's greatest length
 float ray[16];             // this frame's view ray at the screen's top left, its change across and down (per unit of linear depth), the eye
 float previous_ray[16];    // last frame's
};
static_assert(sizeof(RtIndirectDenoiseConstants)==144);
// A render pixel's view ray per unit of linear depth (clip w), affine in the
// screen position: out[0-2] at the top left, out[4-6] across, out[8-10] down,
// out[12-14] the eye.
void rt_indirect_view_rays(const float clip_to_world[16],const float world_to_clip[16],float out[16]){
 const auto unproject=[&](float x,float y,float z,float p[3]){
  float h[4];
  for(int i=0;i<4;++i)h[i]=clip_to_world[i*4]*x+clip_to_world[i*4+1]*y+clip_to_world[i*4+2]*z+clip_to_world[i*4+3];
  for(int i=0;i<3;++i)p[i]=h[i]/h[3];
 };
 const auto depth=[&](const float p[3]){return world_to_clip[12]*p[0]+world_to_clip[13]*p[1]+world_to_clip[14]*p[2]+world_to_clip[15];};
 static constexpr float kX[3]{-1.f,1.f,-1.f},kY[3]{1.f,1.f,-1.f}; // the screen's top left, top right, bottom left
 float ray[3][3]{},eye[3]{};
 for(int c=0;c<3;++c){
  float a[3],b[3];unproject(kX[c],kY[c],.25f,a);unproject(kX[c],kY[c],.75f,b);
  const float da=depth(a),db=depth(b),scale=std::fabs(db-da)>1e-9f?1.f/(db-da):0.f;
  for(int i=0;i<3;++i)ray[c][i]=(b[i]-a[i])*scale;
  if(c==0)for(int i=0;i<3;++i)eye[i]=a[i]-da*ray[0][i];
 }
 std::fill(out,out+16,0.f);
 for(int i=0;i<3;++i){out[i]=ray[0][i];out[4+i]=ray[1][i]-ray[0][i];out[8+i]=ray[2][i]-ray[0][i];out[12+i]=eye[i];}
}
// The passes' b1 (kRtIndirectShader RtIndirectConstants).
struct RtIndirectConstants{
 float clip_to_world[16],world_to_clip[16],previous_clip_to_world[16],previous_world_to_clip[16];
 float eye[4];              // w: the clearance a visibility ray keeps from its lamp
 float previous_eye[4];
 float size_jitter[4];      // the render size, this frame's jitter (render pixels)
 float previous_jitter[4];  // last frame's jitter; z, w: a ray's self-hit distance and its growth per metre of view distance
 float sun[4];              // toward the sun, unit; w 1: there is one
 float sun_diffuse[4];
 std::uint32_t frame[4];    // the frame, flags (1 history, 2 object motion, 4 cave exits, 8 the final visibility ray, 16 an ambient occlusion history, 32 occlusion only), the cave exit count, the frame's random word
 std::uint32_t reservoir[4];// block row pitch, array pitch, neighbour offset mask, the half size (x, y << 16)
 std::uint32_t offsets[4];  // the render pixel of its quad a half pixel stands for: this frame's, last frame's
 std::uint32_t counts[4];   // history length, a sample's greatest age (frames), spatial samples, lamp candidates at a hit
 float spatial[4];          // spatial radius (half pixels), depth threshold, normal threshold, boiling filter strength
 float ao[4];               // rt_ambient_occlusion: the sky reach (metres), the history's greatest length (frames), the contact reach; w: the boiling filter's strength on the final reservoirs (0: none)
 float reuse[4];            // x: a new sample's M against the history's; y: render pixels a traced pixel's side (rt_indirect_resolution); z: the outlier clamp's multiple of the group mean (0: none)
 std::uint32_t lava[4];     // rt_emissive: the lava table's tiles across and down, its levels (0: none)
 float lava_reach[4];       // where a lava hit starts to count (metres along the ray), one over the width of that start
 std::uint32_t ao_offsets[4]; // half-size AO's render-pixel offsets, this and last frame (full-size bounce only)
};
static_assert(sizeof(RtIndirectConstants)==512);
constexpr UINT kRtIndirectReservoirBytes = 32; // RTXDI_PackedGIReservoir
// rt_reflections (D3D12 only; Renderer::reflections_d3d12 and the water
// trace of flush_native_d3d12): reflections by rays through the ray tracing
// scene, shaded as stage 2b shades a hit (rtTrace, rtLit). Both texts follow
// kRaytracingHitShader.
// rtReflectCS (render resolution): a guide pixel whose roughness lies under
// the cut-off (rfControl.x; the twins left that share of their environment
// reflection out, native_shader.h nativeGuideUntraced) casts one ray from
// the GGX lobe of its normal and roughness (visible normals, the lobe's
// outer fifth left out). A hit returns its lit colour; a miss the sky probe
// at the roughness's level, brought to the ambient's level as the material
// programs bring it, or the ambient without a probe. Times the guide's
// specular albedo (the split-sum weight of F0, roughness and N.V: tinted on
// metal, weak and rising toward grazing on a dielectric), the reflection
// strength and the share the pixel's fog leaves.
// rtWaterPS (the water trace's half resolution, after the screen march): the
// water point's mirror ray. A hit takes the screen's colour where the hit
// point is the visible surface there (the march's own colour rule), else its
// lit colour, at full confidence; a miss keeps the march's sample (what the
// scene does not hold: cover, particles, the sky as drawn) without its
// hidden share: nothing stood in the ray's way.
constexpr char kRtReflectShader[] = R"hlsl(
cbuffer RtReflectConstants:register(b1){row_major float4x4 rfClipToWorld;float4 rfEye;float4 rfSizeJitter;float4 rfSun;float4 rfSunDiffuse;
 float4 rfAmbient;float4 rfControl;uint4 rfFrame;float4 rfFog;row_major float4x4 rfWorldClip;};
// A ray hit's colour behind the air between `from` and it, as the scene fogs a surface: the analytic atmosphere
// (world_fog.h nativeAtmosphereCoreAt without the fog volume, which exists along the camera's rays only) or the
// stock distance fog, then the draw-distance fade from the eye. rfFog: the game's fog colour; a 0: no fog.
float3 rfFogged(float3 colour,float3 from,float3 p){
 if(rfFog.a<.5)return colour;
 float3 view=p-from;float distance=length(view);view/=max(distance,1e-4);
 if(fogEye.w>.5){
  float h=max(fogHeight.z,.1);
  float a=clamp((from.z-fogHeight.y)/h,-40,80),b=clamp((p.z-fogHeight.y)/h,-40,80);
  float mean=abs(b-a)>1e-3?(exp(-a)-exp(-b))/(b-a):exp(-a);
  float transmittance=exp(-fogAirOpticalDepth(distance)-fogHeight.x*distance*mean);
  float3 inscatter=fogSunColor.rgb*fogSunResponse(dot(view,fogSun.xyz))*(1-transmittance);
  colour=colour*transmittance+fogLighting(fogHazeColorAt(rfFog.rgb,view,fogHazeColorFrame.w>.5),transmittance,inscatter,0);
 }else if(fogProjection.w>.5)colour=lerp(borderFogColorAt(p,rfFog.rgb),colour,1-smoothstep(fogStock.x,fogStock.y,distance));
 if(fogStock.w>0)colour=lerp(borderFogColorAt(p,rfFog.rgb),colour,nativeDrawFadeDistance(length(p-fogEye.xyz)));
 return colour;
}
#ifdef RT_REFLECT_WATER
cbuffer RtWaterFrame:register(b7){row_major float4x4 rwWorldClip;row_major float4x4 rwInverseWorldClip;float4 rwTarget;float4 rwQuality;
 row_major float4x4 rwHistoryClip;row_major float4x4 rwHistoryInverse;float4 rwMarch;};
Texture2DArray<float4> rwSceneColor:register(t3);
Texture2D<float> rwSceneDepth:register(t112);
SamplerState rwSceneSampler:register(s5);
Texture2D<float4> rwMarchColor:register(t29);
Texture2D<float4> rwMarchKey:register(t30);
Texture2D<float4> rwPlace:register(t31);
Texture2D<float4> rwRay:register(t32);
struct RwResult{float4 color:SV_Target0;float4 key:SV_Target1;};
RwResult rtWaterPS(float4 position:SV_Position){
 int3 texel=int3(int2(position.xy),0);
 RwResult result;result.color=rwMarchColor.Load(texel);result.key=rwMarchKey.Load(texel);
 float4 place=rwPlace.Load(texel);
 if(place.w<=0||result.key.w<0)return result;
 float3 direction=rwRay.Load(texel).xyz;
 if(!(dot(direction,direction)>.25))return result;
 direction=normalize(direction);
 // A mirror ray a wave facet turned below the horizon would meet the bed under its own water (the scene
 // holds the bed, not the water): the march's sample stays.
 if(direction.z<.01)return result;
 RtHit h=rtTrace(place.xyz,direction,rfControl.z,rfControl.w,RAY_FLAG_NONE);
 result.key.w=0;
 if(!h.hit)return result;
 // Off screen: the hit lit, behind the air the eye would see it through (what the screen shows of a surface).
 float3 colour=rfFogged(clamp(rtLit(h,rfSun.xyz,rfSunDiffuse.rgb,rfAmbient.rgb),0,1.5),fogEye.xyz,h.position);
 float4 clip=mul(rwWorldClip,float4(h.position,1));
 float2 uv=clip.xy/max(clip.w,1e-6)*float2(.5,-.5)+.5;
 if(clip.w>0&&all(uv>0)&&all(uv<1)&&clip.z>=0&&clip.z<=clip.w){
  int2 pixel=int2(uv*rwTarget.xy);
  float z=rwSceneDepth.Load(int3(pixel,0));
  float2 ndc=(float2(pixel)+.5)/rwTarget.xy*float2(2,-2)+float2(-1,1);
  float4 scene=mul(rwInverseWorldClip,float4(ndc,z,1));
  if(z<1&&abs(scene.w)>1e-6){
   // The visible surface there is the hit: within a tolerance that grows with view depth (a pixel's world size).
   float depth=dot(rwWorldClip[3],float4(h.position,1))*rsqrt(max(dot(rwWorldClip[3].xyz,rwWorldClip[3].xyz),1e-12));
   float limit=.15+.02*max(depth,0);
   float edge=min(min(uv.x,uv.y),min(1-uv.x,1-uv.y));
   float seen=saturate(edge/.06)*saturate(2-length(scene.xyz/scene.w-h.position)/limit);
   if(seen>0){
    float level=min(log2(max(h.t*place.w,1)),rwQuality.y);
    float3 shown=rwSceneColor.SampleLevel(rwSceneSampler,float3(uv,0),0).rgb;
    if(rwQuality.z>.5){
     float4 before=mul(rwHistoryClip,float4(h.position,1));
     float2 previous=before.xy/max(before.w,1e-6)*float2(.5,-.5)+.5;
     if(before.w>0&&all(previous>0)&&all(previous<1))shown=rwSceneColor.SampleLevel(rwSceneSampler,float3(previous,1),level).rgb;
    }
    colour=lerp(colour,shown,seen);
   }
  }
 }
 if(any(isnan(colour))||any(isinf(colour)))colour=0;
 // A hit near the end of the rays' reach hands over to the march's sample (the sky, mostly): far backdrop
 // relief is lit and fogged too coarsely here to own the open water's look.
 float owned=1-smoothstep(rfControl.w*.6,rfControl.w,h.t);
 result.color=lerp(result.color,float4(colour,1),owned);
 return result;
}
#else
Texture2D<float> rfDepth:register(t0);
Texture2D<float4> rfGuideNormal:register(t1);
Texture2D<float4> rfGuideSpecular:register(t3);
RWTexture2D<float4> rfOutput:register(u0);
// DLSS Ray Reconstruction's specular hit distance (DLSS-RR Integration Guide 3.4.9): the world distance from the
// pixel's surface to its reflection ray's hit; 65504 (the guide's sky value) where no ray was cast or it met nothing.
RWTexture2D<float> rfHitDistance:register(u1);
// The world colour of this frame as drawn so far (the lit world with the traced lamp and bounce light and the
// game's effects): a hit that is the visible surface at its place on screen returns it, the water rays' rule,
// so a unit reflects with its own texture and shading instead of its texture's mean colour.
Texture2D<float4> rfSceneColor:register(t4);
// The game's world particles of this frame (Renderer::build_reflection_particles_d3d12): a structure of bounding
// octahedra, eight triangles a particle, every triangle a candidate, and the particles by primitive index / 8.
// mode: 1 the colour takes the texture, 2 the diffuse; 4 the alpha takes the texture, 8 the diffuse; 16 added
// light (else blended over what lies behind); 32 the added light is weighed by the alpha.
RaytracingAccelerationStructure rfParticleScene:register(t5);
struct RfParticle{float4 centerRadius;float4 uv;uint color;uint image;uint mode;uint padding;};
StructuredBuffer<RfParticle> rfParticles:register(t6);
// What the particles a ray passes before `farthest` leave of `behind`. Each is a square that faces the ray
// through its centre (a quad that faces the camera is edge-on to most reflected rays; NVIDIA's ray traced
// reflections of particles orient them to the ray), read once whichever of its bounding triangles the ray meets.
// Added light sums; blended particles show their alpha-weighted mean colour over the share of `behind` they
// leave, which needs no order. At most 16 particles a ray; the rest are passed through.
float3 rfParticleLight(float3 origin,float3 direction,float farthest,float3 behind){
 RayDesc ray;ray.Origin=origin;ray.Direction=direction;ray.TMin=0;ray.TMax=farthest;
 RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
 query.TraceRayInline(rfParticleScene,RAY_FLAG_NONE,0xFF,ray);
 uint seen[16];uint count=0;
 float3 added=0,blended=0;float weight=0,through=1;
 float3 right=normalize(abs(direction.z)<.99?cross(direction,float3(0,0,1)):float3(1,0,0)),up=cross(right,direction);
 while(query.Proceed()){
  if(query.CandidateType()!=CANDIDATE_NON_OPAQUE_TRIANGLE)continue;
  uint index=query.CandidatePrimitiveIndex()/8u;
  bool known=count>=16;
  for(uint k=0;k<count;++k)known=known||seen[k]==index;
  if(known)continue;
  seen[count++]=index;
  RfParticle q=rfParticles[index];
  float3 centre=q.centerRadius.xyz-origin;
  float along=dot(centre,direction);
  if(along<=0||along>=farthest)continue;
  float3 offset=direction*along-centre;
  float2 place=float2(dot(offset,right),dot(offset,up))/max(q.centerRadius.w,1e-5);
  if(any(abs(place)>1))continue;
  float4 t=1;
  if(q.image!=0){
   Texture2D image=ResourceDescriptorHeap[NonUniformResourceIndex(q.image)];
   t=image.SampleLevel(rtTerrainSampler,lerp(q.uv.xy,q.uv.zw,float2(place.x,-place.y)*.5+.5),0);
  }
  float4 d=float4((q.color>>16)&255u,(q.color>>8)&255u,q.color&255u,q.color>>24)/255.0;
  float3 colour=((q.mode&1u)!=0?t.rgb:1)*((q.mode&2u)!=0?d.rgb:1);
  float alpha=saturate(((q.mode&4u)!=0?t.a:1)*((q.mode&8u)!=0?d.a:1));
  if((q.mode&16u)!=0)added+=colour*((q.mode&32u)!=0?alpha:1);
  else{blended+=colour*alpha;weight+=alpha;through*=1-alpha;}
 }
 return behind*through+(weight>1e-4?blended/weight*(1-through):0)+added;
}
uint rfHash(uint v){v^=v>>16;v*=0x7feb352du;v^=v>>15;v*=0x846ca68bu;v^=v>>16;return v;}
// A half vector of the GGX lobe's visible normals (Heitz 2018), view and result in the surface's frame.
float3 rfVisibleNormal(float3 view,float alpha,float2 u){
 float3 stretched=normalize(float3(alpha*view.xy,view.z));
 float lengthSquared=dot(stretched.xy,stretched.xy);
 float3 t1=lengthSquared>1e-12?float3(-stretched.y,stretched.x,0)*rsqrt(lengthSquared):float3(1,0,0);
 float3 t2=cross(stretched,t1);
 float r=sqrt(u.x),phi=6.2831853*u.y;
 float a=r*cos(phi),b=r*sin(phi),s=.5*(1+stretched.z);
 b=(1-s)*sqrt(saturate(1-a*a))+s*b;
 float3 n=a*t1+b*t2+sqrt(saturate(1-a*a-b*b))*stretched;
 return normalize(float3(alpha*n.xy,max(n.z,1e-4)));
}
[numthreads(8,8,1)]void rtReflectCS(uint3 id:SV_DispatchThreadID){
 if(any(id.xy>=uint2(rfSizeJitter.xy)))return;
 int3 at=int3(id.xy,0);
 float3 light=0;float reach=65504;
 float z=rfDepth.Load(at);
 float4 n=rfGuideNormal.Load(at);
 float4 specular=rfGuideSpecular.Load(at);
 uint b=uint(specular.a*65535+.5);
 float kept=float((b>>7)&255u)/255.0;
 float roughness=saturate(n.w);
 float share=(1-smoothstep(rfControl.x-.1,rfControl.x,roughness))*kept*rfControl.y;
 if(z<1&&dot(n.xyz,n.xyz)>.25&&share*max(specular.r,max(specular.g,specular.b))>.004){
  float2 uv=(float2(id.xy)+.5-rfSizeJitter.zw)/rfSizeJitter.xy;
  float4 world=mul(rfClipToWorld,float4(uv.x*2-1,1-uv.y*2,z,1));
  float3 p=world.xyz/world.w,normal=normalize(n.xyz),view=normalize(rfEye.xyz-p);
  // A normal the relief bent away from the eye: the view lifted to graze it.
  float facing=dot(normal,view);
  if(facing<.02)view=normalize(view+normal*(.02-facing));
  float3 t1=normalize(abs(normal.z)<.99?cross(float3(0,0,1),normal):float3(1,0,0)),t2=cross(normal,t1);
  uint seed=rfHash(id.x+rfHash(id.y+rfHash(rfFrame.x)));
  float2 u=float2(seed&0xffffu,seed>>16)/65536.0;
  u.x*=.8;
  float alpha=max(roughness*roughness,.002);
  float3 halfway=rfVisibleNormal(float3(dot(view,t1),dot(view,t2),dot(view,normal)),alpha,u);
  float3 direction=reflect(-view,t1*halfway.x+t2*halfway.y+normal*halfway.z);
  float up=dot(direction,normal);
  if(up<.02)direction=normalize(direction+normal*(.02-up));
  float nearest=rfControl.z+rfControl.w*length(p-rfEye.xyz)+((b>>15)!=0?.25:0);
  RtHit h=rtTrace(p,direction,nearest,100000,RAY_FLAG_NONE);
  float3 radiance=rfAmbient.rgb;
  if(h.hit){
   radiance=rfFogged(clamp(rtLit(h,rfSun.xyz,rfSunDiffuse.rgb,rfAmbient.rgb),0,1.5),p,h.position);reach=min(h.t,65504);
   // On screen: where the visible surface at the hit's place is the hit (within a tolerance that grows with view
   // depth, a pixel's world size; rtWaterPS's rule), the colour drawn there, fading toward the screen's edge.
   float4 clip=mul(rfWorldClip,float4(h.position,1));
   float2 screen=clip.xy/max(clip.w,1e-6)*float2(.5,-.5)+.5;
   if(rfFrame.z!=0&&clip.w>0&&all(screen>0)&&all(screen<1)){
    int2 pixel=int2(clamp(screen*rfSizeJitter.xy+rfSizeJitter.zw,0,rfSizeJitter.xy-1));
    float drawnZ=rfDepth.Load(int3(pixel,0));
    float2 centre=(float2(pixel)+.5-rfSizeJitter.zw)/rfSizeJitter.xy;
    float4 drawn=mul(rfClipToWorld,float4(centre.x*2-1,1-centre.y*2,drawnZ,1));
    if(drawnZ<1&&abs(drawn.w)>1e-6){
     float limit=.15+.02*max(clip.w*rsqrt(max(dot(rfWorldClip[3].xyz,rfWorldClip[3].xyz),1e-12)),0);
     float edge=min(min(screen.x,screen.y),min(1-screen.x,1-screen.y));
     float seen=saturate(edge/.06)*saturate(2-length(drawn.xyz/drawn.w-h.position)/limit);
     if(seen>0)radiance=lerp(radiance,clamp(rfSceneColor.Load(int3(pixel,0)).rgb,0,4),seen);
    }
   }
  }
  else if(fogTemporal.z>.5&&fogEye.w>.5){
   float probeWidth,probeHeight,probeLevels;skyProbe.GetDimensions(0,probeWidth,probeHeight,probeLevels);
   float azimuth=atan2(direction.y,direction.x)*(.5/3.14159265);azimuth+=azimuth<0?1:0;
   float3 sky=skyProbe.SampleLevel(fogLinearClamp,float2(azimuth,.5-asin(clamp(direction.z,-1,1))/3.14159265),roughness*(probeLevels-1)).rgb;
   float upperLevel=max(probeLevels-3,0);
   float3 upper=(skyProbe.SampleLevel(fogLinearClamp,float2(.25,.25),upperLevel).rgb+skyProbe.SampleLevel(fogLinearClamp,float2(.75,.25),upperLevel).rgb)*.5;
   const float3 luma=float3(.2126,.7152,.0722);
   sky*=min(dot(rfAmbient.rgb,luma)/max(dot(upper,luma),1e-3),1);
   radiance=lerp(rfAmbient.rgb,sky,smoothstep(-.15,.2,direction.z));
  }
  // The game's particles between the surface and the hit (rfFrame.y: how many the structure holds).
  if(rfFrame.y!=0)radiance=rfParticleLight(p,direction,h.hit?h.t:100000,radiance);
  light=radiance*specular.rgb*share;
  if(any(isnan(light))||any(isinf(light)))light=0;
 }
 rfOutput[id.xy]=float4(light,1);
 rfHitDistance[id.xy]=reach;
}
#endif
)hlsl";
const std::string& rt_reflect_source(){
 static const std::string source=std::string(kRaytracingHitShader)+kAtmosphereShader+kRtReflectShader;
 return source;
}
// append_rt_reflection_shader_programs order.
enum : std::size_t { kRtReflectTrace, kRtReflectWater, kRtReflectAdd, kRtReflectPrograms };
void append_rt_reflection_shader_programs(std::vector<ShaderProgram>& out){
 static const D3D_SHADER_MACRO water[]={{"RT_REFLECT_WATER","1"},{"RT_CARD_SAMPLER","s14"},{nullptr,nullptr}};
 const std::string& source=rt_reflect_source();
 out.push_back({{source.data(),source.size()},nullptr,nullptr,"rtReflectCS","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 out.push_back({{source.data(),source.size()},nullptr,water,"rtWaterPS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 out.push_back({{kRtDirectAddShader,sizeof(kRtDirectAddShader)-1},nullptr,nullptr,"rtDirectAddPS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
}
// rtReflectCS's and rtWaterPS's b1 (kRtReflectShader RtReflectConstants).
struct RtReflectConstants{
 float clip_to_world[16];
 float eye[4];
 float size_jitter[4];   // the render size, this frame's jitter (pixels)
 float sun[4];           // toward the sun, unit; all 0 without one (a cave)
 float sun_diffuse[4];
 float ambient[4];
 float control[4];       // surfaces: the roughness cut-off, the reflection strength, a ray's self-hit distance and its growth per metre of view distance; water: -, -, a ray's start and reach
 std::uint32_t frame[4]; // the noise's frame word; surfaces: y the particles in their structure, z 1: the world colour is bound (a hit seen on screen takes it)
 float fog[4];           // the game's fog colour; a 1: hits are fogged
 float world_clip[16];   // surfaces: this frame's world to clip
};
static_assert(sizeof(RtReflectConstants)==256);
// The scene's record table (native_shader.h rtSunRecords): a header of eight
// words, then four words a record.
constexpr std::size_t kRtHeaderWords = 8, kRtRecordWords = 4;
constexpr std::uint32_t kRtVertexBytes = 20, kRtTriangleBytes = 12;
// The active map's base terrain as the near sun maps draw it: every base
// (non-layer) tile at full detail without stitching (terrain_index_template
// level 0), untessellated, at its authored rest position. Liquid and other
// layer tiles are left out. Map-local positions are world positions on the
// active map (the terrain face cache's rule for its table entry 0).
bool raytracing_terrain_mesh(const TerrainCpuAsset& terrain,std::vector<float>& positions,std::vector<std::uint32_t>& indices){
 constexpr std::uint32_t kBase=33u*33u;
 const TerrainIndexTemplate topology=terrain_index_template(0,0,false,false);
 positions.clear();indices.clear();
 for(const auto& sector:terrain.sectors){
  const std::uint32_t count=std::uint32_t((std::min<std::size_t>)(sector.vertices.size(),kBase));
  const std::uint32_t first=std::uint32_t(positions.size()/3);
  for(std::uint32_t index=0;index<count;++index){
   const auto& vertex=sector.vertices[index];
   if(!std::isfinite(vertex.x)||!std::isfinite(vertex.y)||!std::isfinite(vertex.z))return false;
   positions.insert(positions.end(),{vertex.x,vertex.y,vertex.z});
  }
  for(const auto& tile:sector.tiles){
   if(tile.transparent)continue;
   bool base=true;
   for(const std::uint16_t index:tile.vertex_indices)base=base&&index<count;
   if(!base)continue;
   for(std::uint32_t index=0;index<topology.count;++index)indices.push_back(first+tile.vertex_indices[topology.indices[index]]);
  }
 }
 return !indices.empty();
}
D3D12NativeState post_state(bool additive,bool over){
 D3D12NativeState state;
 state.raster.FillMode=D3D11_FILL_SOLID;state.raster.CullMode=D3D11_CULL_BACK;state.raster.DepthClipEnable=TRUE;
 auto& blend=state.custom_blend;
 if(additive){ // colour added, the target alpha kept
  blend.custom=1;blend.source=blend.destination=D3D11_BLEND_ONE;
  blend.source_alpha=D3D11_BLEND_ZERO;blend.destination_alpha=D3D11_BLEND_ONE;
  blend.write_mask=D3D11_COLOR_WRITE_ENABLE_RED|D3D11_COLOR_WRITE_ENABLE_GREEN|D3D11_COLOR_WRITE_ENABLE_BLUE;
 }else if(over){ // lerp(target, source, source alpha), the target alpha kept
  blend.custom=1;blend.source=D3D11_BLEND_SRC_ALPHA;blend.destination=D3D11_BLEND_INV_SRC_ALPHA;
  blend.source_alpha=D3D11_BLEND_ZERO;blend.destination_alpha=D3D11_BLEND_ONE;
  blend.write_mask=D3D11_COLOR_WRITE_ENABLE_RED|D3D11_COLOR_WRITE_ENABLE_GREEN|D3D11_COLOR_WRITE_ENABLE_BLUE;
 }
 return state;
}
D3D11_SAMPLER_DESC post_linear_clamp(){
 D3D11_SAMPLER_DESC s{};s.Filter=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
 s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;s.MaxLOD=0;
 return s;
}
}  // namespace

void append_d3d12_shader_programs(std::vector<ShaderProgram>& out) {
 out.push_back(kD3D12SurfaceEffectVS);
 out.push_back(kD3D12SurfaceEffectPS);
 out.push_back(kD3D12MipProgram);
 out.push_back(kD3D12SurfaceCopyProgram);
 out.push_back(kD3D12MipArrayProgram);
 out.push_back(kD3D12MipArrayRangeProgram);
 out.push_back(kUnitMipCopyProgram);out.push_back(kUnitMipHistogramProgram);
 out.push_back(kUnitMipSolveProgram);out.push_back(kUnitMipScaleProgram);
 append_gamma_shader_programs(out);
 append_debug_resource_shader_programs(out);
 append_shadow_range_shader_programs(out);
 out.push_back(embedded_figure_layer_program(false));
 out.push_back(embedded_figure_layer_program(true));
 append_terrain2_cache_d3d12_programs(out);
 append_terrain_face_cache_d3d12_programs(out);
 append_native_off_material_programs(out);
 append_native_motion_programs(out);
 append_native_indexed_programs(out);
 append_native_slim_lean_programs(out);
 append_native_slim_mark_program(out);
 append_native_prepass_object_program(out);
 append_raytracing_shader_programs(out);
 append_rt_direct_shader_programs(out);    // rt_direct_lights
 append_rt_indirect_shader_programs(out);  // rt_indirect_light
 append_rt_reflection_shader_programs(out);// rt_reflections
 append_native_guide_programs(out);        // rt_ray_reconstruction: the guide twins
 append_ground_cover_guide_programs(out);
 append_fire_burn_programs(out);           // fire_burn: the NATIVE_FIRE_BURN twins
}
const ShaderProgram& d3d12_mip_program() noexcept { return kD3D12MipProgram; }
const ShaderProgram& d3d12_mip_array_program() noexcept { return kD3D12MipArrayProgram; }
const ShaderProgram& d3d12_mip_array_range_program() noexcept { return kD3D12MipArrayRangeProgram; }

// A native program's lookup key (NativeD3D12::by_defines): its entry, then
// its define names sorted (values are not part of it).
static std::string native_program_key(const char* entry,std::vector<const char*> names){
 std::sort(names.begin(),names.end(),[](const char* a,const char* b){return std::strcmp(a,b)<0;});
 std::string key(entry);
 for(const char* name:names){key+='|';key+=name;}
 return key;
}

// The native programs and the first geometry page on the D3D12 backend.
#if !defined(EID3D11_SHADER_EMBED)
HRESULT Renderer::surface_effect_d3d12(const SurfaceImage& source,const SurfaceImage& destination,
 const RECT& viewport,bool fade,std::vector<std::uint8_t>& pixels){
 if(!d3d12_||viewport.right<=0||viewport.bottom<=0||
    (destination.format.dwFlags&DDPF_FOURCC)||destination.format.dwRGBBitCount!=32)return E_INVALIDARG;
 HRESULT hr=flush_native();
 if(FAILED(hr))return hr;
 // Game publications keep their CPU generation. Use isolated uploads so a
 // later edit of the same game image cannot reuse an old GPU generation.
 auto input=source;
 auto background=destination;
 input.d3d12.reset();
 input.uploaded_generation=0;
 input.uploaded_shape={};
 background.d3d12.reset();
 background.uploaded_generation=0;
 background.uploaded_shape={};
 if(FAILED(hr=upload_d3d12(input,nullptr))||FAILED(hr=upload_d3d12(background,nullptr)))return hr;
 D3D12TextureRef target;
 if(FAILED(hr=d3d12_->create_target(destination.width,destination.height,target,
  DXGI_FORMAT_B8G8R8A8_UNORM,DXGI_FORMAT_UNKNOWN)))return hr;
 struct alignas(16) EffectConstants {
  float viewport[4];
  UINT fade;
  UINT padding[3];
 } constants{{float(viewport.left),float(viewport.top),float(viewport.right),float(viewport.bottom)},fade?1u:0u,{}};
 try{
  d3d12_->clear_slots();
  d3d12_->set_target(target.get());
  d3d12_->set_resource(0,input.d3d12.get());
  d3d12_->set_resource(1,background.d3d12.get());
  d3d12_->set_sampler(0,post_linear_clamp());
  d3d12_->set_constants(0,&constants,sizeof(constants));
  // Surface APIs are one-shot writes. Wait for their PSO as for a picking frame.
  d3d12_->set_hit_test_frame(true);
  hr=d3d12_->draw(kD3D12SurfaceEffectVS,kD3D12SurfaceEffectPS,post_state(false,false),nullptr,3);
  if(hr==S_OK){
   UINT width=0,height=0;
   hr=d3d12_->read_texture(*target,pixels,width,height);
   if(hr==S_OK&&(width!=destination.width||height!=destination.height))hr=E_UNEXPECTED;
  }else if(SUCCEEDED(hr))hr=E_PENDING;
 }catch(const std::bad_alloc&){hr=E_OUTOFMEMORY;}
 catch(...){hr=E_FAIL;}
 d3d12_->set_hit_test_frame(hit_test_active_);
 d3d12_->clear_slots();
 if(camp_popup_active_&&camp_popup_d3d12_)
  d3d12_->set_target(camp_popup_d3d12_.get());
 else if(world_d3d12_bound_)
  bind_world_targets_d3d12();
 else
  d3d12_->set_target(nullptr);
 // Readback preserves bytes for the existing game/DC publication contract.
 // Resampling, BC decoding and fade arithmetic all ran on the GPU.
 return hr;
}

#ifdef EID3D11_EMBEDDED
HRESULT Renderer::compose_embedded_figure_texture_d3d12(const D3D12TextureRef& primary,
 const D3D12TextureRef& secondary,D3D12TextureRef& combined){
 combined.reset();
 if(!d3d12_||!primary||!secondary)return E_INVALIDARG;
 UINT width=0,height=0,mips=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
 if(!d3d12_->texture_info(*primary,width,height,mips,format)||!width||!height)return E_INVALIDARG;
 UINT sw=0,sh=0,sm=0;DXGI_FORMAT sf=DXGI_FORMAT_UNKNOWN;
 if(!d3d12_->texture_info(*secondary,sw,sh,sm,sf))return E_INVALIDARG;
 // RGBA keeps sampled channel values and permits typed UAV mip writes.
 D3D12TextureRef target,output;
 HRESULT hr=d3d12_->create_target(width,height,target,DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_UNKNOWN);
 if(FAILED(hr))return hr;
 UINT levels=1;for(UINT size=(std::max)(width,height);size>1;size>>=1)++levels;
 hr=d3d12_->create_storage_texture(width,height,0,DXGI_FORMAT_R8G8B8A8_UNORM,output,levels);
 if(FAILED(hr))return hr;
 D3D11_SAMPLER_DESC sampling{};sampling.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
 sampling.AddressU=sampling.AddressV=sampling.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
 sampling.MaxLOD=D3D11_FLOAT32_MAX;
 D3D12NativeState state;
 state.raster.FillMode=D3D11_FILL_SOLID;state.raster.CullMode=D3D11_CULL_BACK;state.raster.DepthClipEnable=TRUE;
 d3d12_->clear_slots();d3d12_->set_resource(0,primary.get());d3d12_->set_resource(1,secondary.get());
 d3d12_->set_sampler(0,sampling);d3d12_->set_target(target.get());
 hr=d3d12_->draw(embedded_figure_layer_program(false),embedded_figure_layer_program(true),state,nullptr,3);
 d3d12_->clear_slots();
 if(world_d3d12_bound_)bind_world_targets_d3d12();else d3d12_->set_target(nullptr);
 // A pending pipeline must never publish an unwritten composite.
 if(hr!=S_OK)return hr;
 hr=d3d12_->copy_texture(*output,0,*target,0);
 if(hr!=S_OK)return hr;
 hr=d3d12_->generate_mips(*output,d3d12_mip_program());
 if(hr==S_OK)combined=std::move(output);
 return hr;
}
#endif

HRESULT Renderer::native_d3d12_ensure(){
 if(native12_.ready)return S_OK;
 if(native12_.failed||!d3d12_)return E_FAIL;
 try{
  native12_.programs.clear();append_native_shader_programs(native12_.programs);
  append_native_off_material_programs(native12_.programs); // after the general list (programs[n] stay)
  append_native_motion_programs(native12_.programs);
  append_native_indexed_programs(native12_.programs);
  append_native_slim_lean_programs(native12_.programs);
  append_native_slim_mark_program(native12_.programs);
  append_native_prepass_object_program(native12_.programs);
  // rt_ray_reconstruction: each twinned program's guide twin (the same walk
  // and order as append_native_guide_programs). After the last append:
  // the map holds addresses of native12_.programs.
  native12_.guides.clear();native12_.guide_twins.clear();append_native_guide_programs(native12_.guides);
  {std::size_t twin=0;
   for(const auto& program:native12_.programs)
    if(native_guide_twinned(program)&&twin<native12_.guides.size())native12_.guide_twins.emplace(&program,&native12_.guides[twin++]);
   if(twin!=native12_.guides.size()){native12_.guide_twins.clear();d3d12_->omit("Ray Reconstruction guide programs (the twin list does not match the native programs)",false);}}
  native12_.tessellation.clear();append_terrain_tessellation_shader_programs(native12_.tessellation);
  if(native12_.programs.size()<3){native12_.failed=true;return E_FAIL;}
  // flush_native_d3d12's choices (the rules its per-flush scans had): the
  // first program of each entry and define-name set; the material instance
  // pixel and vertex programs, with and without the check programs; the
  // indexed vertex programs.
  native12_.by_defines.clear();
  for(const auto& program:native12_.programs){
   std::vector<const char*> names;
   for(const D3D_SHADER_MACRO* define=program.defines;define&&define->Name;++define)names.push_back(define->Name);
   native12_.by_defines.emplace(native_program_key(program.entry,std::move(names)),&program);
  }
  for(const bool check:{false,true}){
   auto& pixel=native12_.instance_pixel[check?1:0];auto& vertex=native12_.instance_vertex[check?1:0];
   pixel.fill(nullptr);vertex.fill(nullptr);
   for(const auto& program:native12_.programs){
    if(std::strcmp(program.entry,"nativePS")!=0||!program.defines||std::strcmp(program.defines->Name,"NATIVE_DOMAIN")!=0)continue;
    for(std::size_t slot=0;slot<kNativeInstanceSlots;++slot)for(unsigned variant=0;variant<16u;++variant)
     if(program.defines==native_instance_defines(slot,(variant&1u)!=0,check,(variant&2u)!=0,(variant&4u)!=0,(variant&8u)!=0))
      pixel[(variant&14u)/2u*kNativeInstancePrograms+slot*2u+(variant&1u)]=&program;
   }
   for(const auto& program:native12_.programs)
    for(std::size_t c=0;c<kNativeInstanceVertexClasses;++c){
     // Colour and motion: the check twins with the check pixel programs.
     if(program.defines==native_instance_vertex_defines(c,false,check)&&std::strcmp(program.entry,"nativeVS")==0)vertex[c*3u]=&program;
     else if(program.defines==native_instance_vertex_defines(c,false)&&std::strcmp(program.entry,"nativePrepassVS")==0)vertex[c*3u+1u]=&program;
     else if(program.defines==native_instance_vertex_defines(c,true,check))vertex[c*3u+2u]=&program;
    }
  }
  native12_.indexed_vertex.fill(nullptr);
  for(const auto& program:native12_.programs)
   for(std::size_t c=0;c<kNativeInstanceVertexClasses;++c){
    if(program.defines==native_indexed_vertex_defines(c,false))native12_.indexed_vertex[c*3u+(std::strcmp(program.entry,"nativeVS")==0?0u:1u)]=&program;
    else if(program.defines==native_indexed_vertex_defines(c,true))native12_.indexed_vertex[c*3u+2u]=&program;
   }
  if(native_geometry_pages_.empty()){
   NativeGeometryPage page;page.capacity=16;page.high_water=16;page.free.reserve(1);
   const HRESULT hr=d3d12_->create_buffer(16,0,0,page.d3d12);
   if(FAILED(hr)){native12_.failed=true;return hr;}
   native_geometry_pages_.push_back(std::move(page));
   native_meshes_.clear();native_geometry_live_bytes_=0;native_mesh_resident_bytes_=0;
  }
 }catch(const std::bad_alloc&){native12_.failed=true;return E_OUTOFMEMORY;}
 native12_.ready=true;
 return S_OK;
}

// A frame buffer of at least `bytes` (grown by demand; contents not kept).
HRESULT Renderer::native_d3d12_buffer(D3D12BufferRef& buffer,UINT64 bytes,UINT stride,UINT flags){
 bytes=(std::max<UINT64>)(bytes,16);
 if(buffer&&d3d12_->buffer_bytes(*buffer)>=bytes)return S_OK;
 const UINT64 current=buffer?d3d12_->buffer_bytes(*buffer):0;
 UINT64 target=(std::max<UINT64>)({bytes,current*2,64u*1024u});
 if(stride)target=(target+stride-1)/stride*stride;
 D3D12BufferRef created;
 HRESULT hr=d3d12_->create_buffer(target,stride,flags,created);
 if(FAILED(hr)&&target!=bytes)hr=d3d12_->create_buffer(bytes,stride,flags,created);
 if(FAILED(hr))return hr;
 buffer=std::move(created);
 return S_OK;
}

HRESULT Renderer::native_d3d12_persistent(D3D12BufferRef& buffer,UINT64 bytes,UINT stride,UINT64 keep){
 bytes=(std::max<UINT64>)(bytes,16);
 if(buffer&&d3d12_->buffer_bytes(*buffer)>=bytes)return S_OK;
 D3D12BufferRef created;
 HRESULT hr=d3d12_->create_buffer(bytes,stride,kD3D12BufferUnordered,created);
 if(FAILED(hr))return hr;
 if(buffer&&keep&&FAILED(hr=d3d12_->copy_buffer(*created,*buffer,(std::min)(keep,d3d12_->buffer_bytes(*buffer)))))return hr;
 buffer=std::move(created);
 return S_OK;
}

HRESULT Renderer::terrain_texture_d3d12(SurfaceImage& image,const TerrainTexturePage* page,D3D12TextureRef& output){
 output.reset();
 const bool bc1=(image.format.dwFlags&DDPF_FOURCC)&&image.format.dwFourCC==MAKEFOURCC('D','X','T','1');
 auto& entry=terrain_textures_d3d12_[image.id];
 if(entry.texture&&entry.generation==image.generation){output=entry.texture;return S_OK;}
 // Every terrain page texture is recorded (the contact atlas copies find
 // them by surface id and generation).
 const auto record=[&](const D3D12TextureRef& texture,const char* kind)->HRESULT{
  if(!texture)return E_FAIL;
  entry.generation=image.generation;entry.texture=texture;entry.surface_alias=texture==image.d3d12;output=texture;
  d3d12_->count(kind);
  return S_OK;
 };
 if(!bc1){ // not a terrain page format: the surface's own texture (one level; D3D11 generates mips here)
  const HRESULT hr=upload_d3d12(image,nullptr);if(FAILED(hr))return hr;
  return record(image.d3d12,"terrain pages: not BC1 (one level)");
 }
 // A queued command carries a pixel-free snapshot (surface_snapshot): the
 // host table's image of the same generation holds the bytes (this backend
 // never releases them). Without bytes the surface keeps its own single
 // level, as the D3D11 slice does without the shipped chain.
 const SurfaceImage* source=&image;
 if(image.pixels.empty()){
  const auto found=core_surfaces_.find(image.id);
  if(found!=core_surfaces_.end()&&found->second&&found->second->generation==image.generation&&!found->second->pixels.empty())
   source=found->second.get();
 }
 const UINT block_rows=(std::max)(1u,(image.height+3u)/4u);
 if(source->pixels.empty()||source->pitch<=0||std::uint64_t(source->pitch)*block_rows>source->pixels.size()){
  // upload_d3d12 also checks executor ownership before its generation hit.
  const HRESULT hr=upload_d3d12(image,nullptr);if(FAILED(hr))return hr;
  return record(image.d3d12,"terrain pages: BC1 without bytes (one level)");
 }
 UINT mip_count=1;
 bool authored=false;
 if(page){
  prepare_terrain_pages_d3d12(*page); // the whole terrain's pages at its first miss, on workers
  ResourceFailure failure;
  authored=page->ensure_upload_bytes(failure)&&!page->mips.empty()&&
   authored_bc1_chain_matches(page->mips.data(),page->mip_count,image.width,image.height,source->pixels.data(),
    source->pixels.size(),static_cast<std::size_t>(source->pitch));
  if(authored)mip_count=page->mip_count;
 }
 // Its prepared texture: the shipped chain, whose level 0 the match above
 // found equal to the surface's bytes; only the copies are recorded here.
 if(authored&&terrain_pages_d3d12_.terrain){
  const auto& pages=terrain_pages_d3d12_.terrain->pages;
  if(page>=pages.data()&&page<pages.data()+pages.size()){
   auto prepared=std::move(terrain_pages_d3d12_.uploads[std::size_t(page-pages.data())]);
   D3D12TextureRef texture;
   if(prepared&&SUCCEEDED(d3d12_->finish_texture(*prepared,texture))){
    page->release_upload_bytes();
    return record(texture,"terrain pages: BC1 with the shipped chain (prepared)");
   }
  }
 }
 D3D12TextureRef texture;
 HRESULT hr=d3d12_->create_texture(image.width,image.height,DXGI_FORMAT_BC1_UNORM,texture,mip_count);
 if(SUCCEEDED(hr))hr=d3d12_->write_texture(*texture,RECT{0,0,LONG(image.width),LONG(image.height)},source->pixels.data(),UINT(source->pitch),0);
 for(UINT level=1;SUCCEEDED(hr)&&level<mip_count;++level){
  const UINT w=(std::max)(1u,image.width>>level),h=(std::max)(1u,image.height>>level);
  hr=d3d12_->write_texture(*texture,RECT{0,0,LONG(w),LONG(h)},page->mips[level].data,(std::max)(1u,(w+3u)/4u)*8u,level);
 }
 if(authored)page->release_upload_bytes();
 if(FAILED(hr))return hr;
 return record(texture,authored?"terrain pages: BC1 with the shipped chain":"terrain pages: BC1 one level (chain absent or different)");
}

// Every page of the active terrain prepared at once at its first miss: the
// pages' archive reads and parses split over workers, then one heap and one
// upload buffer for all of them (prepare_texture_batch; a texture and an
// upload buffer per page cost 624 ms of creation over 54 pages). Each worker
// reads and keeps the bytes of its own pages only (the render thread waits,
// so no page is touched twice); the match against the game surface stays at
// each page's miss.
void Renderer::prepare_terrain_pages_d3d12(const TerrainTexturePage& page){
 const auto contains=[&page](const TerrainCpuAsset& terrain){
  return !terrain.pages.empty()&&&page>=terrain.pages.data()&&&page<terrain.pages.data()+terrain.pages.size();};
 if(terrain_pages_d3d12_.terrain&&contains(*terrain_pages_d3d12_.terrain))return; // prepared already
 const auto source=retained_static_terrain_source_snapshot();
 if(!source.terrain||!contains(*source.terrain))return; // not the active terrain's page: one at a time as before
 const auto preparer=d3d12_->preparer();
 if(!preparer)return;
 terrain_pages_d3d12_={};
 try{
  const auto& terrain=*source.terrain;
  const std::size_t count=terrain.pages.size();
  // Per page: its levels as the batch reads them (empty: not prepared).
  struct Levels {std::vector<const void*> data;std::vector<UINT> pitches;UINT width=0,height=0;};
  std::vector<Levels> pages(count);
  const unsigned workers=(std::max)(1u,(std::min)(8u,std::thread::hardware_concurrency()/2));
  const auto read=[&](unsigned first) noexcept {
   for(std::size_t index=first;index<count;index+=workers)try{
    const TerrainTexturePage& p=terrain.pages[index];
    ResourceFailure failure;
    const bool loaded=p.store&&p.ensure_upload_bytes(failure);
    if(!loaded||p.mips.empty()||p.mip_count!=p.mips.size())continue;
    const UINT width=p.mips[0].width,height=p.mips[0].height;
    Levels levels;levels.width=width;levels.height=height;
    levels.data.resize(p.mip_count);levels.pitches.resize(p.mip_count);
    bool usable=width&&height;
    for(UINT level=0;usable&&level<p.mip_count;++level){
     const UINT w=(std::max)(1u,width>>level),h=(std::max)(1u,height>>level);
     const UINT row=(std::max)(1u,(w+3u)/4u)*8u,rows=(std::max)(1u,(h+3u)/4u);
     const auto& mip=p.mips[level];
     usable=mip.data&&mip.width==w&&mip.height==h&&mip.bytes>=std::uint64_t(row)*rows;
     levels.data[level]=mip.data;levels.pitches[level]=row;
    }
    if(usable)pages[index]=std::move(levels);
   }catch(...){}
  };
  std::vector<std::thread> threads;threads.reserve(workers);
  unsigned started=1;
  try{for(;started<workers;++started)threads.emplace_back(read,started);}catch(...){}
  read(0);
  for(unsigned first=started;first<workers;++first)read(first); // threads that could not start
  for(auto& thread:threads)thread.join();
  std::vector<D3D12Preparer::TextureLevels> batch(count);
  for(std::size_t index=0;index<count;++index)if(!pages[index].data.empty()){
   auto& entry=batch[index];
   entry.width=pages[index].width;entry.height=pages[index].height;entry.mips=static_cast<UINT>(pages[index].data.size());
   entry.format=DXGI_FORMAT_BC1_UNORM;entry.levels=pages[index].data.data();entry.pitches=pages[index].pitches.data();
  }
  std::vector<D3D12PreparedUploadRef> uploads;
  (void)preparer->prepare_texture_batch(batch.data(),count,uploads);
  uploads.resize(count);
  terrain_pages_d3d12_.terrain=source.terrain;terrain_pages_d3d12_.uploads=std::move(uploads);
 }catch(...){terrain_pages_d3d12_={};}
}

HRESULT Renderer::copy_contact_atlas_d3d12(D3D12Texture& source,UINT material,ContactBlendMaps& maps){
 UINT width=0,height=0,mips=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
 if(!d3d12_->texture_info(source,width,height,mips,format))return E_FAIL;
 if(!maps.atlas12){
  UINT skip=0;
  while(((width>>skip)>contact_blend_atlas_side||(height>>skip)>contact_blend_atlas_side)&&skip+1<mips)++skip;
  UINT slices=0;for(std::uint32_t m=0;m<contact_blend_max_materials;++m)if((maps.pending>>m)&1u)slices=m+1;
  const HRESULT hr=d3d12_->create_texture_array((std::max)(1u,width>>skip),(std::max)(1u,height>>skip),format,mips-skip,(std::max)(1u,slices),maps.atlas12);
  if(FAILED(hr))return hr;
  maps.width=(std::max)(1u,width>>skip);maps.height=(std::max)(1u,height>>skip);maps.mips=mips-skip;maps.format=format;
  maps.slices=(std::max)(1u,slices);
 }
 UINT skip=0;
 while(skip<mips&&(std::max)(1u,width>>skip)>maps.width)++skip;
 maps.pending&=~(std::uint64_t{1}<<material);
 if(format!=maps.format||skip+maps.mips>mips||(std::max)(1u,width>>skip)!=maps.width||(std::max)(1u,height>>skip)!=maps.height)return S_FALSE;
 for(UINT mip=0;mip<maps.mips;++mip){
  const HRESULT hr=d3d12_->copy_texture(*maps.atlas12,mip+material*maps.mips,source,mip+skip);
  if(FAILED(hr))return hr;
 }
 maps.ready|=std::uint64_t{1}<<material;
 return S_OK;
}

// bake_terrain2_heights on D3D12: the same six programs and passes per
// pending material cell (kTerrain2HeightShader), the scratch planes and
// statistics as structured unordered buffers, the heights an R8 storage
// array with the atlas's size, levels and slices (cleared to .5, the flat
// height), its levels made once a batch is in. A dispatch still compiling
// (S_FALSE) or dropped stops the batch before that cell's write; the map
// retries next frame. The D3D12 backend does not wait for the GPU here, so
// the log carries no bake time.
void Renderer::bake_terrain2_heights_d3d12(Terrain2Map& map,const ContactBlendMaps& contact,const char* name) noexcept try{
 map.dirty=false;map.seen_ready=contact.ready;
 if(map.failed||!contact.atlas12)return;
 std::vector<const Terrain2BakeCell*> pending;
 for(const auto& c:map.bake_cells)
  if(c.slice<64&&((contact.ready>>c.slice)&1u)&&!map.baked.count(c.key))pending.push_back(&c);
 if(pending.empty()&&!map.mips12)return;
 HRESULT hr=S_OK;
 const auto fail=[&](const char* what){
  map.failed=true;
  char text[224];std::snprintf(text,sizeof(text),"map=%s %s failed (0x%08lx, D3D12); terrain 2.0 blends with flat material heights",name,what,static_cast<unsigned long>(hr));
  bridge_append_probe_event("terrain2_height_omitted",text);
 };
 if(terrain2_bake12_programs_.empty())append_terrain2_shader_programs(terrain2_bake12_programs_);
 if(terrain2_bake12_programs_.size()<6){hr=E_FAIL;return fail("height bake programs");}
 const UINT width=contact.width,height=contact.height;
 if(width%128u||height!=width){hr=E_INVALIDARG;return fail("height bake atlas layout");}
 if(!map.height12){
  if(!d3d12_->storage_supported(DXGI_FORMAT_R8_UNORM)){hr=E_NOTIMPL;return fail("height array format");}
  if(FAILED(hr=d3d12_->create_storage_texture(width,height,0,DXGI_FORMAT_R8_UNORM,map.height12,(std::max)(1u,contact.mips),(std::max)(1u,contact.slices)))){
   map.height12.reset();return fail("height array");
  }
  const float flat[4]{.5f,.5f,.5f,.5f};d3d12_->clear_texture(*map.height12,flat);
  map.mips12=true;
 }
 // Work planes (4 complex 256x256) and the statistics, for this batch only.
 D3D12BufferRef planes,stats;
 if(!pending.empty()&&(FAILED(hr=d3d12_->create_buffer(4ull*65536u*8u,8,kD3D12BufferUnordered,planes))||
                       FAILED(hr=d3d12_->create_buffer(8u*4u,4,kD3D12BufferUnordered,stats))))
  return fail("height bake scratch");
 D3D11_SAMPLER_DESC sampler{};sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
 sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sampler.MaxLOD=D3D11_FLOAT32_MAX;
 struct Constants{float lo[2],size[2],dir[2],fold,aw;std::uint32_t n,slice,src,dst;float sigma,weight;std::uint32_t inverse,axis;
  std::uint32_t origin[2],side,mode;float amp,clip,pad[2];}k{};
 static_assert(sizeof(Constants)==96);
 bool stopped=false;
 const auto run=[&](unsigned shader,UINT groups_x,UINT groups_y){
  if(stopped)return;
  d3d12_->clear_slots();
  d3d12_->set_constants(0,&k,sizeof(k));
  d3d12_->set_resource(0,contact.atlas12.get());d3d12_->set_sampler(0,sampler);
  d3d12_->set_unordered(0,planes.get());d3d12_->set_unordered(1,stats.get());d3d12_->set_unordered(2,map.height12.get());
  const HRESULT done=d3d12_->dispatch(terrain2_bake12_programs_[shader],groups_x,groups_y,1);
  if(done!=S_OK){stopped=true;if(FAILED(done))hr=done;}
 };
 enum{Load,Fft,Gauss,Combine,Reduce,Write};
 const auto fft=[&](std::uint32_t plane,bool inverse){
  k.src=plane;k.inverse=inverse;for(std::uint32_t axis=0;axis<2;++axis){k.axis=axis;run(Fft,256,1);}
 };
 const auto gauss=[&](std::uint32_t from,std::uint32_t to,float sigma){k.src=from;k.dst=to;k.sigma=sigma;run(Gauss,16,16);};
 const auto combine=[&](std::uint32_t mode){k.mode=mode;run(Combine,16,16);};
 const auto reduce=[&](std::uint32_t mode){k.mode=mode;run(Reduce,1,1);};
 const float aw=float(width),margin=tile_blend_margin_*.125f,size=.125f-2*margin;
 const UINT slot=width/8u;
 std::size_t baked=0;
 for(const Terrain2BakeCell* c:pending){
  const Terrain2BakeStyle style=terrain2_bake_style(c->style);
  k=Constants{};k.aw=aw;k.slice=c->slice;k.amp=style.amp;k.clip=style.dc;
  k.lo[0]=(c->pos%8u)*.125f+margin;k.lo[1]=1-(c->pos/8u+1)*.125f+margin;k.size[0]=k.size[1]=size;k.dir[0]=k.dir[1]=1;
  if(c->folded){
   const float cx=(c->quadrant==1||c->quadrant==2)?1.f:0.f,cy=c->quadrant>=2?1.f:0.f;
   k.lo[0]+=cx*size;k.lo[1]+=cy*size;k.dir[0]=1-2*cx;k.dir[1]=1-2*cy;k.fold=size*.3f;
   k.n=std::max(2u,UINT(std::lround(2*k.fold*aw)));
  }else k.n=std::max(1u,UINT(std::lround(size*aw)));
  run(Load,16,16);
  fft(0,false);gauss(0,0,64);fft(0,true);combine(0);
  if(style.hp>0){combine(1);fft(0,false);gauss(0,0,style.hp);fft(0,true);combine(2);}
  if(style.dc>0){reduce(0);combine(3);}
  combine(1);fft(0,false);combine(4);
  const float scales[3]{1,4,12},weights[3]{style.w1,style.w4,style.w12};
  for(int s=0;s<3;++s){
   if(weights[s]<=0)continue;
   gauss(0,2,scales[s]);fft(2,true);combine(6);reduce(1);k.weight=weights[s];combine(5);
  }
  fft(1,false);combine(7);fft(2,true);reduce(2);
  k.side=slot;k.origin[0]=(c->pos%8u)*slot;k.origin[1]=width-(c->pos/8u+1)*slot;
  run(Write,(slot+15u)/16u,(slot+15u)/16u);
  if(stopped)break;
  map.baked.insert(c->key);++baked;map.mips12=true;
 }
 d3d12_->clear_slots();
 if(FAILED(hr))return fail("height bake dispatch");
 if(stopped){map.dirty=true;d3d12_->omit("terrain 2.0 material heights (pipeline not ready; retried)",false);}
 // The levels once nothing of the batch is outstanding (D3D11 GenerateMips).
 if(!stopped&&map.mips12){
  const HRESULT mips=d3d12_->generate_mips(*map.height12,d3d12_mip_array_range_program(),0,(std::max)(1u,contact.slices));
  if(FAILED(mips)){hr=mips;return fail("height levels");}
  if(mips==S_OK){map.mips12=false;++terrain2_cache_revision_;}else map.dirty=true;
 }
 if(baked){
  char text[224];std::snprintf(text,sizeof(text),"map=%s cells=%zu baked=%zu array=%ux%ux%u (%.1f MB) backend=D3D12",name,baked,map.baked.size(),
   width,height,(std::max)(1u,contact.slices),double(width)*height*(std::max)(1u,contact.slices)*4/3/1048576.0);
  bridge_append_probe_event("terrain2_heights",text);
  ++terrain2_cache_revision_; // the material cache bakes again with the new heights
 }
}catch(...){}

// The snow surface on D3D12 (update_snow_surface's copy): one dispatch into a
// 256 px RGBA8 storage texture, then its levels. A dispatch or downsample not
// ready (S_FALSE) retries next frame; the texture is bound once complete.
void Renderer::update_snow_surface_d3d12() noexcept try{
 std::uint64_t key=0;const Terrain2BakeCell* cell=snow_surface_cell(key);
 if(key==snow_surface_key_)return;
 if(!cell||!contact_maps_.atlas12||!terrain2_map_.height12){snow_surface_key_=key;snow_surface12_.reset();return;}
 HRESULT hr=S_OK;
 const auto fail=[&](const char* what){
  snow_surface_key_=key;snow_surface12_.reset();
  char text[160];std::snprintf(text,sizeof(text),"%s failed (0x%08lx, D3D12); snowed objects keep the drift relief",what,static_cast<unsigned long>(hr));
  bridge_append_probe_event("snow_surface_omitted",text);
 };
 if(snow_surface12_program_.empty())append_snow_surface_program(snow_surface12_program_);
 if(!d3d12_->storage_supported(DXGI_FORMAT_R8G8B8A8_UNORM)){hr=E_NOTIMPL;return fail("snow surface format");}
 D3D12TextureRef texture;
 UINT mips=1;for(UINT n=kSnowSurfaceSide;n>1;n>>=1)++mips;
 if(FAILED(hr=d3d12_->create_storage_texture(kSnowSurfaceSide,kSnowSurfaceSide,0,DXGI_FORMAT_R8G8B8A8_UNORM,texture,mips)))return fail("snow surface texture");
 const float margin=tile_blend_margin_*.125f,size=.125f-2*margin;
 struct{float lo[2],size[2],dir[2],fold,slice;}k{};
 k.lo[0]=(cell->pos%8u)*.125f+margin;k.lo[1]=1-(cell->pos/8u+1)*.125f+margin;k.size[0]=k.size[1]=size;k.dir[0]=k.dir[1]=1;k.slice=float(cell->slice);
 if(cell->folded){
  const float cx=(cell->quadrant==1||cell->quadrant==2)?1.f:0.f,cy=cell->quadrant>=2?1.f:0.f;
  k.lo[0]+=cx*size;k.lo[1]+=cy*size;k.dir[0]=1-2*cx;k.dir[1]=1-2*cy;k.fold=size*.3f;
 }
 D3D11_SAMPLER_DESC clamp{};clamp.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
 clamp.AddressU=clamp.AddressV=clamp.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;clamp.MaxLOD=D3D11_FLOAT32_MAX;
 d3d12_->clear_slots();d3d12_->set_constants(0,&k,sizeof(k));
 d3d12_->set_resource(0,contact_maps_.atlas12.get());d3d12_->set_resource(1,terrain2_map_.height12.get());d3d12_->set_sampler(0,clamp);
 d3d12_->set_unordered(0,texture.get());
 hr=d3d12_->dispatch(snow_surface12_program_[0],kSnowSurfaceSide/16u,kSnowSurfaceSide/16u,1);
 d3d12_->clear_slots();
 if(FAILED(hr))return fail("snow surface dispatch");
 if(hr!=S_OK)return; // compiling: next frame
 hr=d3d12_->generate_mips(*texture,d3d12_mip_program());
 if(FAILED(hr))return fail("snow surface levels");
 if(hr!=S_OK)return;
 snow_surface_key_=key;snow_surface12_=std::move(texture);
 char text[96];std::snprintf(text,sizeof(text),"cell=%u slice=%u folded=%d side=%u backend=D3D12",cell->key,cell->slice,cell->folded?1:0,kSnowSurfaceSide);
 bridge_append_probe_event("snow_surface",text);
}catch(...){}

// The terrain 2.0 map table on D3D12: its entries are read by bindless
// index (record 3, terrain2_records_d3d12); a height bake in this flush
// (after the frame values went up) changes which heights are readable, so
// the values go up again when an index changed. Then the contact sampler
// terrain2Sampler reads (s9). Once a world flush.
void Renderer::bind_terrain2_d3d12() noexcept{
 if(terrain2_records_first_&&native_terrain_frame_d3d12_.size()>terrain2_records_first_&&
    terrain2_records_d3d12(native_terrain_frame_d3d12_.data()+terrain2_records_first_))
  (void)upload_native_terrain_frame_d3d12();
 D3D11_SAMPLER_DESC contact{};contact.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
 contact.AddressU=contact.AddressV=contact.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;contact.MaxLOD=D3D11_FLOAT32_MAX;
 d3d12_->set_sampler(9,contact);
 // The material cache (bind_terrain2_cache's t116-t118 and s15, at
 // kTerrain2Cache*SlotD3D12); the params say off until a frame set them up.
 const bool cache=terrain2_cache_on_;
 d3d12_->set_resource(kTerrain2CacheCoarseSlotD3D12,cache?terrain2_cache_coarse12_.get():nullptr);
 d3d12_->set_resource(kTerrain2CacheParamsSlotD3D12,cache?terrain2_cache_params12_.get():nullptr);
 d3d12_->set_resource(kTerrain2CacheFineSlotD3D12,cache?terrain2_cache_fine12_.get():nullptr);
 d3d12_->set_resource(78,cache?terrain2_cache_height_fine12_.get():nullptr); // S3 height layer (terrain2_cache.h t78/t79)
 d3d12_->set_resource(79,cache?terrain2_cache_height_coarse12_.get():nullptr);
 D3D11_SAMPLER_DESC wrap{};wrap.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
 wrap.AddressU=wrap.AddressV=wrap.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;wrap.MaxLOD=0;
 d3d12_->set_sampler(15,wrap);
 bind_terrain_face_cache_d3d12(); // the face cache (t100-t105; s15 above)
}

void Renderer::add_neighbour_blend_maps_d3d12(const NeighbourPiece& upload,const std::array<float,16>& active_to_world,UINT contact_map) noexcept{
 const bool tile_blend=configured_integer(debug_overlay_.config(),"terrain_blend",1)>0;
 const bool cover=configured_integer(debug_overlay_.config(),"ground_cover",2)>0;
 if(!tile_blend&&!cover&&!configured_terrain_cliffs(debug_overlay_.config()))return;
 const auto report=[&](const char* text){
  char line[224];std::snprintf(line,sizeof(line),"map=%s blend_maps=%s",upload.selection.placement.mpr.c_str(),text);
  bridge_neighbour_diagnostic(line);
 };
 try{
  GcNeighbour n;n.contact_map=contact_map;n.face_grid=upload.face_grid;n.name=upload.selection.placement.mpr;for(char& c:n.name)if(c>='A'&&c<='Z')c=static_cast<char>(c+32);
  if(!ground_cover::neighbour_cover_origin(active_to_world,upload.selection.x,upload.selection.y,n.origin))return report("omitted reason=placement_not_translation");
  n.layout_origin={neighbour_layout_tile_origin(upload.selection.placement.x),neighbour_layout_tile_origin(upload.selection.placement.y)};
  const auto& inputs=upload.ground_cover;
  if(!inputs||!inputs->grids_valid)return report("omitted reason=empty_terrain");
  n.tiles_x=inputs->grids.tiles_x;n.tiles_y=inputs->grids.tiles_y;
        // Its decoded terrain's vertex heights every 2 m (its own frame): the
        // ground of the world bounce volume (compose_cave_world), whatever the
        // cover and blend options build.
        {const auto& h=inputs->grids.height;const UINT vx=n.tiles_x*2u+1u;
         if(h.size()==std::size_t(vx)*(n.tiles_y*2u+1u)){
          n.bounce_heights.resize(std::size_t(n.tiles_x+1u)*(n.tiles_y+1u));
          for(UINT j=0;j<=n.tiles_y;++j)for(UINT i=0;i<=n.tiles_x;++i)
           n.bounce_heights[std::size_t(j)*(n.tiles_x+1u)+i]=h[std::size_t(2u*j)*vx+2u*i];}}
  n.lowest=inputs->lowest;n.highest=inputs->highest;
  n.fog_band={active_to_world[3]+float(upload.fog_edges.x0),active_to_world[7]+float(upload.fog_edges.y0),
              active_to_world[3]+float(upload.fog_edges.x1),active_to_world[7]+float(upload.fog_edges.y1)};
  const UINT tx=n.tiles_x,ty=n.tiles_y;
  if(FAILED(build_contact_tiles(inputs->contact_tiles,inputs->contact_used,tx,ty,n.contact,&n.contact_rows)))return report("omitted reason=tile_upload_failed");
  if(!n.contact_rows.empty()){
   // Its blend half: no material until its families load (as on D3D11).
   auto grid=n.contact_rows;
   grid.resize(grid.size()*2,std::array<std::uint32_t,4>{~0u,~0u,0u,~0u});
   D3D12TextureRef tiles;
   if(FAILED(d3d12_->create_texture(tx,ty*2,DXGI_FORMAT_R32G32B32A32_UINT,tiles))||
      FAILED(d3d12_->write_texture(*tiles,RECT{0,0,LONG(tx),LONG(ty*2)},grid.data(),tx*16u)))return report("omitted reason=tile_upload_failed");
   n.contact.tiles12=std::move(tiles);
   if(tile_blend&&inputs->families_valid&&inputs->tiles_per_texture_side==8&&inputs->blend_input){
    n.blend_input=inputs->blend_input;
    std::promise<TerrainTileFamilies> loaded;loaded.set_value(inputs->families);
    n.families=TileFamiliesJob(loaded.get_future(),nullptr);
   }else std::vector<std::array<std::uint32_t,4>>().swap(n.contact_rows);
  }
  std::vector<NativeOwnedTextureMip> page_mips;
  for(std::size_t material=0;material<inputs->used.size()&&material<contact_blend_max_materials;++material){
   if(!inputs->used[material]||!(n.contact.pending&(std::uint64_t{1}<<material)))continue;
   const auto texture=material<inputs->atlas_textures.size()?inputs->atlas_textures[material]:UINT32_MAX;
   if(texture>=upload.textures.size())continue;
   D3D12TextureRef page;std::uint64_t backing=0;
   if(FAILED(get_native_owned_texture_d3d12(neighbour_piece_texture_view(upload.textures[texture],page_mips),page,backing,nullptr,true))||!page)continue;
   if(copy_contact_atlas_d3d12(*page,static_cast<UINT>(material),n.contact)!=S_OK)report("contact art omitted for one material");
  }
  // Its ground cover map: the same grids, fields and paint the active map
  // builds (build_ground_cover_map_d3d12), from the stream's inputs, with its
  // own terrain atlas pages; 2 colour texels per tile (contact reads mip 0).
  if(cover){
   const auto& grids=inputs->grids;
   const auto types=inputs->families_valid?ambient::terrain_cover_types(inputs->base_slots,inputs->base_lanes,inputs->slot_types,
     terrain_tile_sand_slots(inputs->families),terrain_tile_bare_corners(inputs->families),terrain_tile_soft_sand_corners(inputs->families)):
     std::vector<std::uint16_t>(grids.types.begin(),grids.types.end());
   auto& map=n.cover12;
   if(FAILED(build_ground_cover_map_d3d12(map,tx,ty,2,grids.area,grids.height,grids.ground,types,grids.types,inputs->tiles,inputs->used,inputs->trees))){
    n.cover12={};report("ground_cover=omitted reason=grid_upload_failed");
   }else{
    map.atlases.resize(map.paint.size());
    UINT missing=0;
    for(std::size_t material=0;material<map.paint.size();++material){
     if(!map.paint[material])continue;
     const auto texture=material<inputs->atlas_textures.size()?inputs->atlas_textures[material]:UINT32_MAX;
     std::uint64_t backing=0;
     if(texture>=upload.textures.size()||FAILED(get_native_owned_texture_d3d12(neighbour_piece_texture_view(upload.textures[texture],page_mips),
        map.atlases[material],backing,nullptr,true))||!map.atlases[material]){map.atlases[material].reset();map.paint[material]=0;++missing;}
    }
    char text[160];std::snprintf(text,sizeof(text),"ground_cover=ready tiles=%ux%u origin=%.0f,%.0f unpainted_materials=%u%s",tx,ty,
     n.origin[0],n.origin[1],missing,inputs->families_valid?"":" beach_sand=terrain_type_only");
    report(text);
   }
  }
  gc_neighbours_.push_back(std::move(n));
 }catch(...){report("omitted reason=cpu_allocation");}
}

// The ground contact and terrain material maps on D3D12 (update_contact_blend_maps):
// the tile grid (t15) and the terrain atlases copied, every level, into one
// array (t16) as each becomes resident, from the D3D12 terrain textures.
HRESULT Renderer::update_contact_maps_d3d12(){
 const auto& config=debug_overlay_.config();
 if(contact_map_generation_!=ambient_map_generation_){
  AmbientWorldSource inputs;
  if(!bridge_ambient_world_source(inputs)||!inputs.terrain)return S_OK;
  release_contact_blend_maps();
  contact_map_generation_=ambient_map_generation_;contact_retry_frame_=0;contact_incompatible_logged_=false;
  const auto& terrain=*inputs.terrain;
  const UINT tiles_x=ambient_tiles_x_,tiles_y=ambient_tiles_y_;
  if(!tiles_x||!tiles_y)return S_OK;
  HRESULT hr=build_contact_tiles(terrain,tiles_x,tiles_y,contact_maps_,&contact_tile_rows_);
  if(FAILED(hr))return hr;
  if(!contact_tile_rows_.empty()){contact_tiles_x_=tiles_x;contact_tiles_y_=tiles_y;}
  tile_blend_margin_=terrain.tile_size?8.f/float(terrain.tile_size):0.f;
 }
 if(contact_maps_.tiles12&&!tile_blend_ready_&&
    contact_map_generation_==ambient_map_generation_&&poll_terrain_tile_families()){
  const HRESULT hr=apply_terrain_tile_blend(tile_families_);if(FAILED(hr))return hr;
 }
 // Terrain 2.0 (update_contact_blend_maps' rules): own edge families
 // (tile_blend_own_edges_); each map's heights follow its copied atlas slices.
 {
  if(terrain2_map_.dirty||contact_maps_.ready!=terrain2_map_.seen_ready)bake_terrain2_heights_d3d12(terrain2_map_,contact_maps_,"active");
  for(auto& n:gc_neighbours_)
   if(n.terrain2.dirty||n.contact.ready!=n.terrain2.seen_ready)bake_terrain2_heights_d3d12(n.terrain2,n.contact,n.name.c_str());
  update_snow_surface_d3d12();
 }
 if(tile_blend_ready_&&!tile_blend_rows_&&!terrain2_marker_logged_){
  terrain2_marker_logged_=true;
  bridge_append_probe_event("terrain2_no_materials","the map has no material families (atlas profile or preset); terrain 2.0 ground shows the marker colour");
 }
 if(!contact_maps_.pending||native_frame_index_<contact_retry_frame_)return S_OK;
 contact_retry_frame_=native_frame_index_+(load_reveal_holding_?1:30);
 for(std::uint32_t material=0;material<contact_blend_max_materials;++material){
  const std::uint64_t bit=std::uint64_t{1}<<material;
  if(!(contact_maps_.pending&bit))continue;
  std::shared_ptr<const OwnedVisualSurface> surface;
  if(!bridge_terrain_atlas(material,surface)||!surface){d3d12_->count("contact atlas: atlas surface not captured (retried)");continue;}
  // Resolve the captured atlas generation, as the author's D3D11 path
  // does. Owned terrain bypasses terrain_texture_d3d12, so its legacy
  // page cache can stay empty forever. Without these copies Terrain 2.0
  // has no class field and GPU placement rejects every land candidate.
  NativeOwnedTextureView view{};view.identity=surface->id;view.generation=surface->generation;view.surface=surface.get();
  D3D12TextureRef source;UINT64 backing=0;
  const HRESULT resolved=get_native_owned_texture_d3d12(view,source,backing,nullptr,true);
  if(FAILED(resolved))return resolved;
  if(!source)continue;
  const HRESULT copied=copy_contact_atlas_d3d12(*source,material,contact_maps_);
  if(FAILED(copied))return copied;
  if(copied==S_FALSE){ // each material is refused once (its pending bit is cleared)
   UINT width=0,height=0,mips=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;d3d12_->texture_info(source,width,height,mips,format);
   char text[256];
   std::snprintf(text,sizeof(text),"terrain atlas format or size differs (material=%u page=%ux%u mips=%u format=%u atlas=%ux%u mips=%u format=%u); affected contact art omitted",
    material,width,height,mips,unsigned(format),contact_maps_.width,contact_maps_.height,contact_maps_.mips,unsigned(contact_maps_.format));
   bridge_append_probe_event("contact_blend_atlas_incompatible",text);
   continue;
  }
  char detail[224]{};std::snprintf(detail,sizeof(detail),
   "epoch=%llu material=%u surface=%llu generation=%llu ready=%016llx pending=%016llx backend=D3D12",
   static_cast<unsigned long long>(bridge_current_world_epoch()),material,
   static_cast<unsigned long long>(surface->id),static_cast<unsigned long long>(surface->generation),
   static_cast<unsigned long long>(contact_maps_.ready),static_cast<unsigned long long>(contact_maps_.pending));
  bridge_report_event("contact_terrain_atlas",detail);
  d3d12_->count("contact atlas: materials copied");
 }
 return S_OK;
}

// The authoritative terrain frame and live liquid-layer shifts serve both
// colour and caster shaders. Shadow submission can precede the colour flush.
HRESULT Renderer::upload_native_terrain_frame_d3d12(){
 HRESULT hr=S_OK;
 if(!native_terrain_frame_d3d12_.empty()){
  if(terrain2_records_first_&&native_terrain_frame_d3d12_.size()>terrain2_records_first_)
   (void)terrain2_records_d3d12(native_terrain_frame_d3d12_.data()+terrain2_records_first_); // the table's bindless indices
  const UINT64 bytes=native_terrain_frame_d3d12_.size()*sizeof(float);
  if(FAILED(hr=native_d3d12_buffer(native12_.terrain_frame,bytes,UINT(bytes),0))||
     FAILED(hr=d3d12_->write_buffer(*native12_.terrain_frame,0,native_terrain_frame_d3d12_.data(),bytes)))return hr;
 }
 if(native12_.layer_shift_revision!=native_terrain_layer_shift_revision_&&!native_terrain_layer_shifts_.empty()){
  const UINT64 bytes=native_terrain_layer_shifts_.size()*sizeof(float);
  if(FAILED(hr=native_d3d12_buffer(native12_.layer_shifts,bytes,sizeof(float),0))||
     FAILED(hr=d3d12_->write_buffer(*native12_.layer_shifts,0,native_terrain_layer_shifts_.data(),bytes)))return hr;
  native12_.layer_shift_revision=native_terrain_layer_shift_revision_;
 }
 return S_OK;
}

// Point lights (t18), their grid (t17) and constants (b5), as the D3D11 flush
// uploads them when set_native_point_lights changed them.
HRESULT Renderer::upload_native_point_lights_d3d12(){
 if(!native_point_lights_dirty_)return S_OK;
 const UINT count=static_cast<UINT>(native_point_lights_.size());
 HRESULT hr=S_OK;
 if(count){
  const UINT64 bytes=UINT64(count)*sizeof(NativePointLightGpu);
  if(FAILED(hr=native_d3d12_buffer(native12_.point_lights,bytes,sizeof(NativePointLightGpu),0))||
     FAILED(hr=d3d12_->write_buffer(*native12_.point_lights,0,native_point_lights_.data(),bytes)))return hr;
 }
 bool grid_changed=native_point_light_grid_inputs_.size()!=native_point_lights_.size();
 for(std::size_t n=0;!grid_changed&&n<native_point_lights_.size();++n)
  grid_changed=native_point_lights_[n].center_inverse_radius_squared!=native_point_light_grid_inputs_[n];
 if(grid_changed){
  std::vector<std::array<float,4>> grid_inputs;
  try{grid_inputs.reserve(count);for(const auto& light:native_point_lights_)grid_inputs.push_back(light.center_inverse_radius_squared);
   if(!build_point_light_grid(grid_inputs,native_point_light_grid_))return E_INVALIDARG;}
  catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
  native_point_light_grid_inputs_=std::move(grid_inputs);
  const UINT64 bytes=UINT64(native_point_light_grid_.data.size())*sizeof(UINT);
  if(bytes&&(FAILED(hr=native_d3d12_buffer(native12_.point_light_grid,bytes,sizeof(UINT),0))||
             FAILED(hr=d3d12_->write_buffer(*native12_.point_light_grid,0,native_point_light_grid_.data.data(),bytes))))return hr;
 }
 const auto& grid=native_point_light_grid_;
 // The shared b5 layout; the bounce-volume tail is written every flush
 // (write_cave_sample_constants_d3d12).
 struct{UINT count,width,height,glow;float origin[2],inverse_cell,tail;CaveSampleConstants cave;}values{
  count,grid.width,grid.height,static_cast<UINT>(emissive_published_),{grid.origin[0],grid.origin[1]},grid.inverse_cell,0,{}};
 static_assert(sizeof(values)==sizeof(native12_.point_light_constants),"point-light constant layout");
 std::memcpy(native12_.point_light_constants.data(),&values,sizeof(values));
 native_point_lights_dirty_=false;
 return S_OK;
}

// The terrain value-noise lattice (t122, terrainNoise): the same bytes the
// D3D11 device creation makes, as a D3D12 texture.
HRESULT Renderer::ensure_terrain_noise_d3d12(){
 if(native12_.terrain_noise)return S_OK;
 constexpr UINT side=1024;
 std::vector<std::uint8_t> lattice;
 try{lattice.resize(std::size_t(side)*side);}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
 for(std::uint32_t y=0;y<side;++y)for(std::uint32_t x=0;x<side;++x){
  std::uint32_t h=x*0x8da6b343u^y*0xd8163841u;
  h^=h>>16;h*=0x7feb352du;h^=h>>15;h*=0x846ca68bu;h^=h>>16;
  lattice[std::size_t(y)*side+x]=static_cast<std::uint8_t>(h>>24);
 }
 D3D12TextureRef texture;
 HRESULT hr=d3d12_->create_texture(side,side,DXGI_FORMAT_R8_UNORM,texture);
 if(SUCCEEDED(hr))hr=d3d12_->write_texture(*texture,RECT{0,0,LONG(side),LONG(side)},lattice.data(),side);
 if(FAILED(hr))return hr;
 native12_.terrain_noise=std::move(texture);
 return S_OK;
}

// upload_native_terrain_phases on the D3D12 backend: the same copies
// (nativeTerrainPhaseCS: the frame's phase values from the dynamic bytes into
// each animated range of the animation buffer), on their own work list.
HRESULT Renderer::upload_native_terrain_phases_d3d12(){
 native_terrain_phase_copies_.clear();
 for(const auto& work:native_terrain_phase_work_)if(native_animation_ranges_[work.allocation].references){
  if(std::uint64_t(work.offset)+work.count*4ull>native_pending_upload_.size())return E_FAIL;
  NativeDrawWorkGpu copy{};copy.record=work.offset;
  copy.output_triangle_base=native_animation_ranges_[work.allocation].base;
  copy.indirect_args_byte_offset=work.count;native_terrain_phase_copies_.push_back(copy);
 }
 if(native_terrain_phase_copies_.empty())return S_OK;
 if(!native12_.animation)return E_FAIL;
 const UINT64 bytes=UINT64(native_terrain_phase_copies_.size())*sizeof(NativeDrawWorkGpu);
 HRESULT hr=native_d3d12_buffer(native12_.phase_work,bytes,sizeof(NativeDrawWorkGpu),0);
 if(SUCCEEDED(hr))hr=d3d12_->write_buffer(*native12_.phase_work,0,native_terrain_phase_copies_.data(),bytes);
 if(FAILED(hr))return hr;
 transition_profile_pass("compute_terrain_phase");
 d3d12_->clear_slots();
 d3d12_->set_resource(2,native12_.dynamic.get());
 d3d12_->set_resource(16,native12_.phase_work.get());
 d3d12_->set_unordered(0,native12_.animation.get());
 hr=d3d12_->dispatch(native12_.programs[3],static_cast<UINT>(native_terrain_phase_copies_.size()),1,1);
 if(FAILED(hr))return hr;
 if(d3d12_not_done(hr)){if(hr==S_FALSE)d3d12_->omit("terrain phases (pipeline not ready)",false);return S_OK;}
 for(const auto& work:native_terrain_phase_work_)if(native_animation_ranges_[work.allocation].references){
  auto& slot=native_terrain_phases_[work.slot];
  if(slot.allocation==work.allocation&&slot.revision==work.revision)slot.valid=true;
 }
 return S_OK;
}

// The persistent instance rows and draw descriptors (t13, t5): the dirty
// records written straight in (the D3D11 path scatters the same rows with
// nativeInstanceUpdateCS).
HRESULT Renderer::upload_native_instances_d3d12(){
 const UINT required=static_cast<UINT>(native_instance_records_.size());
 if(required>native12_.instance_capacity||!native12_.instances||!native12_.draws){
  const UINT capacity=(std::max)({required,native12_.instance_capacity*2,64u});
  D3D12BufferRef instances,draws;
  HRESULT hr=d3d12_->create_buffer(UINT64(capacity)*sizeof(NativeInstanceGpu),sizeof(NativeInstanceGpu),0,instances);
  if(SUCCEEDED(hr))hr=d3d12_->create_buffer(UINT64(capacity)*sizeof(NativeDrawGpu),sizeof(NativeDrawGpu),0,draws);
  if(FAILED(hr))return hr;
  if(native12_.instances&&native12_.instance_capacity){
   if(FAILED(hr=d3d12_->copy_buffer(*instances,*native12_.instances,UINT64(native12_.instance_capacity)*sizeof(NativeInstanceGpu))))return hr;
   if(FAILED(hr=d3d12_->copy_buffer(*draws,*native12_.draws,UINT64(native12_.instance_capacity)*sizeof(NativeDrawGpu))))return hr;
  }
  native12_.instances=std::move(instances);native12_.draws=std::move(draws);native12_.instance_capacity=capacity;
 }
 for(UINT index:native_instance_dirty_){
  auto& record=native_instance_records_[index];
  if(!record.references)continue;
  if(!record.uploaded){
   const HRESULT hr=d3d12_->write_buffer(*native12_.instances,UINT64(index)*sizeof(NativeInstanceGpu),&record.command.instance,sizeof(NativeInstanceGpu));
   if(FAILED(hr))return hr;
   record.uploaded=true;
  }
  if(!record.descriptor_uploaded){
   const HRESULT hr=d3d12_->write_buffer(*native12_.draws,UINT64(index)*sizeof(NativeDrawGpu),&record.descriptor,sizeof(NativeDrawGpu));
   if(FAILED(hr))return hr;
   record.descriptor_uploaded=true;
  }
 }
 return S_OK;
}

// get_native_owned_texture for the D3D12 backend: an owned source with its
// mip chain (the HUD's atlas textures) becomes one D3D12 texture per key
// and generation, cached in native_owned_textures_ like the D3D11 copy.
HRESULT Renderer::unit_atlas_mips_d3d12(SurfaceImage& image,D3D12TextureRef& output){
 output.reset();
 constexpr DXGI_FORMAT format=DXGI_FORMAT_R8G8B8A8_UNORM;
 const UINT width=image.width,height=image.height,levels=full_mip_count(width,height);
 if(!image.d3d12||levels>16||!d3d12_->storage_supported(format)||!d3d12_->unordered_load_supported(format))return E_NOTIMPL;
 HRESULT hr=S_OK;
 if(!unit_mip_stats12_&&FAILED(hr=d3d12_->create_buffer((4112+16)*4,0,kD3D12BufferUnordered,unit_mip_stats12_)))return hr;
 D3D12TextureRef texture;
 if(FAILED(hr=d3d12_->create_storage_texture(width,height,0,format,texture,levels,1,true)))return hr;
 struct{UINT index,width,height,levels;}k{0,width,height,levels};
 const auto pass=[&](const ShaderProgram& program,UINT level,UINT w,UINT h)->HRESULT{
  k.index=level;d3d12_->clear_slots();d3d12_->set_constants(0,&k,sizeof(k));
  if(&program==&kUnitMipCopyProgram){d3d12_->set_resource(0,image.d3d12.get());d3d12_->set_unordered(0,texture.get());}
  else if(&program!=&kUnitMipSolveProgram)d3d12_->set_unordered_level(0,texture.get(),level);
  d3d12_->set_unordered(1,unit_mip_stats12_.get());
  const HRESULT result=d3d12_->dispatch(program,(w+7u)/8u,(h+7u)/8u,1);
  d3d12_->clear_slots();return result;
 };
 d3d12_->clear_buffer_uint(*unit_mip_stats12_,0);
 if((hr=pass(kUnitMipCopyProgram,0,width,height))!=S_OK)return hr;
 if((hr=d3d12_->generate_mips(*texture,d3d12_mip_program()))!=S_OK)return hr;
 for(UINT level=1;level<levels;++level)
  if((hr=pass(kUnitMipHistogramProgram,level,(std::max)(1u,width>>level),(std::max)(1u,height>>level)))!=S_OK)return hr;
 if((hr=pass(kUnitMipSolveProgram,0,8,1))!=S_OK)return hr; // one group: 16 levels
 for(UINT level=1;level<levels;++level)
  if((hr=pass(kUnitMipScaleProgram,level,(std::max)(1u,width>>level),(std::max)(1u,height>>level)))!=S_OK)return hr;
 output=std::move(texture);
 return S_OK;
}

HRESULT Renderer::get_native_owned_texture_d3d12(const NativeOwnedTextureView& source,D3D12TextureRef& output,
 std::uint64_t& backing,NativeGeometryResult* result,bool world_mips,bool budget_reducible,bool unit){
 const auto fail=[&](HRESULT hr,const char* operation){
  char detail[512]{};
  std::snprintf(detail,sizeof(detail),"operation=%s hr=0x%08lx format=%u mips=%u width=%u height=%u source=%u identity=%llu generation=%llu",
   operation,static_cast<unsigned long>(hr),unsigned(source.format),source.mip_count,
   source.mips&&source.mip_count?source.mips[0].width:0,source.mips&&source.mip_count?source.mips[0].height:0,
   source.value_source,static_cast<unsigned long long>(source.identity),static_cast<unsigned long long>(source.generation));
  bridge_report_event("d3d12_failed",detail);
  return native_geometry_failure(result,hr,operation,true);
 };
 output.reset();backing=0;
 // A camp icon: the surface table's image (the host icon worker's decode),
 // uploaded once per generation; its entry retires with retire_camp_texture.
 if(source.surface&&source.camp_surface_alias){
  if(source.surface->id!=source.identity||source.surface->generation!=source.generation)return fail(E_INVALIDARG,"texture.surface_generation");
  std::string key;
  try{key="surface:"+std::to_string(source.identity)+"|mips=authored|camp";}catch(const std::bad_alloc&){return fail(E_OUTOFMEMORY,"texture.key_allocation");}
  const auto existing=native_owned_textures_.find(key);
  if(existing!=native_owned_textures_.end()&&existing->second.generation==source.generation&&existing->second.d3d12){
   auto& texture=existing->second;
   texture.last_used=native_queue_epoch_;texture.last_used_frame=native_frame_index_;
   output=texture.d3d12;backing=texture.backing;return S_OK;
  }
  if(!surface_texture_resolver_)return fail(E_FAIL,"texture.surface_resolver");
  ComPtr<ID3D11Texture2D> unused;SurfaceImage* image=nullptr;
  HRESULT hr=surface_texture_resolver_(surface_texture_resolver_context_,device_.Get(),source.identity,source.generation,unused,image);
  if(FAILED(hr)||!image)return fail(FAILED(hr)?hr:E_FAIL,"texture.surface_unavailable");
  if(FAILED(hr=upload_d3d12(*image,nullptr))||!image->d3d12)return fail(FAILED(hr)?hr:E_FAIL,"texture.camp_upload");
  // The GPU home is the only copy from here: the icon worker decodes the
  // bytes again if this home is ever released (Host/core_camp.cpp).
  image->pixels.clear();image->cpu_released=true;
  if(native_owned_texture_serial_>=((1ull<<63)-1))return fail(E_OUTOFMEMORY,"texture.serial");
  try{
   NativeOwnedTexture value{};
   value.backing=(1ull<<63)|++native_owned_texture_serial_;
   value.generation=source.generation;value.width=image->width;value.height=image->height;value.mip_count=1;
   value.surface_id=source.identity;value.camp_surface_alias=true;
   value.last_used=native_queue_epoch_;value.last_used_frame=native_frame_index_;value.d3d12=image->d3d12;
   backing=value.backing;output=image->d3d12;
   native_owned_textures_[key]=std::move(value);
  }catch(const std::bad_alloc&){return fail(E_OUTOFMEMORY,"texture.cache_allocation");}
  return S_OK;
 }
 // A completed client surface (not a camp icon): its own texture from the
 // host table's bytes. A static world surface takes its full mip chain, as
 // D3D11's per-generation copy: the surface texture itself when its upload
 // already built the chain, else one entry per generation built from the
 // same bytes (retired with the surface, retire_surface).
 if(source.surface){
  if(source.surface->id!=source.identity||source.surface->generation!=source.generation)return fail(E_INVALIDARG,"texture.surface_generation");
  // A block-compressed world surface draws its archive member's mip chain
  // (authored_surface_chain); its own one-level texture is then not created.
  if(world_mips){
   AuthoredSurfaceLoad load;
   if(AuthoredSurfaceChain* chain=authored_surface_chain(source,load)){
    if(load.view.mips){
     if(SUCCEEDED(get_native_owned_texture_d3d12(load.view,output,backing,nullptr,true,budget_reducible))&&output){backing=authored_surface_backing(*chain,backing);return S_OK;}
     output.reset();backing=0; // not created now: the surface's own level draws
    }else{
     auto& texture=native_owned_textures_.find(chain->key)->second;
     texture.last_used=native_queue_epoch_;texture.last_used_frame=native_frame_index_;
     output=texture.d3d12;backing=authored_surface_backing(*chain,texture.backing);return S_OK;
    }
   }
  }
  if(!surface_texture_resolver_)return fail(E_FAIL,"texture.surface_resolver");
  ComPtr<ID3D11Texture2D> unused;SurfaceImage* image=nullptr;
  HRESULT hr=surface_texture_resolver_(surface_texture_resolver_context_,device_.Get(),source.identity,source.generation,unused,image);
  if(FAILED(hr)||!image)return fail(FAILED(hr)?hr:E_FAIL,"texture.surface_unavailable");
  if(FAILED(hr=upload_d3d12(*image,nullptr))||!image->d3d12)return fail(FAILED(hr)?hr:E_FAIL,"texture.surface_upload");
  backing=(2ull<<62)|source.identity;output=image->d3d12;
  // A unit's composed atlas (32-bit, no static hint) takes the chain as well.
  const bool unit_atlas=unit&&source.surface->format.dwRGBBitCount==32&&source.surface->format.dwRGBAlphaBitMask==0xff000000u;
  if(!(world_mips&&(source.surface->static_mip_hint||unit_atlas)&&!(source.surface->format.dwFlags&DDPF_FOURCC)))return S_OK;
  UINT stored_width=0,stored_height=0,stored_levels=0;DXGI_FORMAT stored_format=DXGI_FORMAT_UNKNOWN;
  if(d3d12_->texture_info(*image->d3d12,stored_width,stored_height,stored_levels,stored_format)&&stored_levels>1)return S_OK;
  if(unit_atlas){
   // On the GPU from the surface's uploaded level: an atlas may change often
   // (equipment, the dialogue mouth frames), no CPU pass per change. The
   // chain then becomes the surface's GPU home and the one-level upload is
   // released (a 1024x1024 atlas: 4 MiB), so a unit atlas is resident once;
   // the next generation uploads a new one level (upload_d3d12).
   D3D12TextureRef texture;
   hr=unit_atlas_mips_d3d12(*image,texture);
   if(hr==S_FALSE)return S_OK; // a program compiling: the one level draws this frame
   if(FAILED(hr)){
    d3d12_->omit("unit atlas mip chains: the GPU pass failed or is unsupported here; unit atlases keep one level",false);
    return S_OK;
   }
   image->d3d12=texture;++image->texture_serial;image->unit_chain=true;
   output=std::move(texture);
   return S_OK;
  }
  std::string key;
  try{key="surface:"+std::to_string(source.identity)+"|mips=full";}catch(const std::bad_alloc&){return fail(E_OUTOFMEMORY,"texture.key_allocation");}
  const auto existing=native_owned_textures_.find(key);
  if(existing!=native_owned_textures_.end()&&existing->second.generation==source.generation&&existing->second.d3d12){
   auto& texture=existing->second;
   texture.last_used=native_queue_epoch_;texture.last_used_frame=native_frame_index_;
   output=texture.d3d12;backing=texture.backing;return S_OK;
  }
  D3D12TextureRef texture;
  UINT levels=1;for(UINT size=(std::max)(image->width,image->height);size>1;size>>=1)++levels;
  if(image->gpu_captured){
   hr=d3d12_->create_storage_texture(image->width,image->height,0,DXGI_FORMAT_R8G8B8A8_UNORM,texture,levels);
   if(FAILED(hr))return fail(hr,"texture.captured_mip_allocation");
   d3d12_->clear_slots();d3d12_->set_resource(0,image->d3d12.get());d3d12_->set_unordered(0,texture.get());
   hr=d3d12_->dispatch(kD3D12SurfaceCopyProgram,(image->width+7)/8,(image->height+7)/8,1);
   d3d12_->clear_slots();
   if(hr==S_OK)hr=d3d12_->generate_mips(*texture,d3d12_mip_program());
   // Keep the original level while a pipeline compiles; retry next draw.
   // Never cache an incomplete chain.
   if(hr!=S_OK)return FAILED(hr)?fail(hr,"texture.captured_mips"):hr;
  }else if(FAILED(hr=surface_mips_d3d12(*image,texture)))return fail(hr,"texture.surface_mips");
  if(native_owned_texture_serial_>=((1ull<<63)-1))return fail(E_OUTOFMEMORY,"texture.serial");
  try{
   NativeOwnedTexture value{};
   value.backing=(1ull<<63)|++native_owned_texture_serial_;
   value.generation=source.generation;value.format=image->gpu_captured?DXGI_FORMAT_R8G8B8A8_UNORM:DXGI_FORMAT_B8G8R8A8_UNORM;
   value.width=image->width;value.height=image->height;
   value.mip_count=levels;value.surface_id=source.identity;
   value.last_used=native_queue_epoch_;value.last_used_frame=native_frame_index_;value.d3d12=texture;
   backing=value.backing;
   native_owned_textures_[key]=std::move(value);
  }catch(const std::bad_alloc&){return fail(E_OUTOFMEMORY,"texture.cache_allocation");}
  output=std::move(texture);
  return S_OK;
 }
 if(source.value_source==1&&!source.resource)return fail(E_INVALIDARG,"texture.archive_identity");
 if(!source.mips&&source.value_source==1&&source.resource){
  // A neighbour piece texture (archive member) without mips: its resident
  // entry (adopt_prebuilt_textures) serves it, as on D3D11.
  std::string key;
  try{key=native_owned_archive_texture_key(*source.resource,source.generation,false);}catch(const std::bad_alloc&){return fail(E_OUTOFMEMORY,"texture.key_allocation");}
  const auto existing=native_owned_textures_.find(key);
  if(existing==native_owned_textures_.end()||existing->second.generation!=source.generation||!existing->second.d3d12)
   return fail(E_FAIL,"texture.not_resident");
  auto& texture=existing->second;
  texture.last_used=native_queue_epoch_;texture.last_used_frame=native_frame_index_;
  output=texture.d3d12;backing=texture.backing;return S_OK;
 }
 if(!source.mips||!source.mip_count||source.mip_count>32)return fail(E_INVALIDARG,"texture.source");
 const bool static_source=source.value_source==1||source.value_source==3;
 const bool uncompressed=source.format!=NativeOwnedTextureFormat::dxt1&&source.format!=NativeOwnedTextureFormat::dxt3&&
  source.format!=NativeOwnedTextureFormat::dxt5;
 const bool generate=world_mips&&static_source&&source.mip_count==1&&uncompressed;
 std::string key;
 // A neighbour's uncompressed textures arrive under this key prepared by the
 // stream (build_native_prebuilt_texture), the same chain or levels.
 // A block-compressed archive member has the archive key on both paths (the
 // stream's prebuilt texture, the mips-less lookup above): one upload.
 try{key=!uncompressed&&source.value_source==1&&source.resource?
  native_owned_archive_texture_key(*source.resource,source.generation,false):native_owned_mip_texture_key(source,generate);}
 catch(const std::bad_alloc&){return fail(E_OUTOFMEMORY,"texture.key_allocation");}
 // VRAM budget (renderer_vram_budget.cpp): a reducible world texture with a
 // mip chain leaves out its top levels while the local budget is short, as
 // get_native_owned_texture does; small textures stay whole (vram_texture_skip).
 const UINT top_width=source.mips[0].width,top_height=source.mips[0].height;
 UINT skip=budget_reducible&&world_mips?vram_texture_skip(top_width,top_height,generate?full_mip_count(top_width,top_height):source.mip_count):0u;
 if(!uncompressed&&((std::max)(1u,top_width>>skip)%4u||(std::max)(1u,top_height>>skip)%4u))skip=0; // block formats: whole blocks at the top level
 const auto existing=native_owned_textures_.find(key);
 if(existing!=native_owned_textures_.end()&&existing->second.generation==source.generation&&existing->second.d3d12){
  auto& texture=existing->second;
  texture.last_used=native_queue_epoch_;texture.last_used_frame=native_frame_index_;
  // Built at another budget level: rebuilt when this frame's rebuild work
  // allows (the old texture goes with the last record that holds it).
  UINT stored_width=0,stored_height=0,stored_levels=0;DXGI_FORMAT stored_format=DXGI_FORMAT_UNKNOWN;
  if(texture.budget_skip==skip||!d3d12_->texture_info(*texture.d3d12,stored_width,stored_height,stored_levels,stored_format)||
     !vram_take_rebuild(texture_storage_bytes(stored_width,stored_height,stored_levels,stored_format))){
   output=texture.d3d12;backing=texture.backing;return S_OK;
  }
 }
 DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;UINT block_bytes=0,pixel_bytes=0;
 switch(source.format){
 case NativeOwnedTextureFormat::dxt1:format=DXGI_FORMAT_BC1_UNORM;block_bytes=8;break;
 case NativeOwnedTextureFormat::dxt3:format=DXGI_FORMAT_BC2_UNORM;block_bytes=16;break;
 case NativeOwnedTextureFormat::dxt5:format=DXGI_FORMAT_BC3_UNORM;block_bytes=16;break;
 case NativeOwnedTextureFormat::bc5:format=DXGI_FORMAT_BC5_UNORM;block_bytes=16;break;
 case NativeOwnedTextureFormat::bc7:format=DXGI_FORMAT_BC7_UNORM;block_bytes=16;break;
 case NativeOwnedTextureFormat::bc7_srgb:format=DXGI_FORMAT_BC7_UNORM_SRGB;block_bytes=16;break;
 case NativeOwnedTextureFormat::rgb565:format=DXGI_FORMAT_B5G6R5_UNORM;pixel_bytes=2;break;
 case NativeOwnedTextureFormat::argb1555:format=DXGI_FORMAT_B5G5R5A1_UNORM;pixel_bytes=2;break;
 case NativeOwnedTextureFormat::argb4444:format=DXGI_FORMAT_B4G4R4A4_UNORM;pixel_bytes=2;break;
 case NativeOwnedTextureFormat::argb8888:format=DXGI_FORMAT_B8G8R8A8_UNORM;pixel_bytes=4;break;
 default:return fail(E_INVALIDARG,"texture.format");
 }
 const UINT width=source.mips[0].width,height=source.mips[0].height;
 if(!width||!height||width>16384||height>16384)return fail(E_INVALIDARG,"texture.dimensions");
 if(generate){
  // The BGRA8 expansion (the D3D11 path's conversion), then each level a 2x2
  // box of the one above (native_owned_box_mips, shared with the stream).
  const NativeOwnedTextureMip& top=source.mips[0];
  if(!top.data||std::uint64_t(width)*height*pixel_bytes>top.bytes)return fail(E_INVALIDARG,"texture.mip_bytes");
  std::vector<std::vector<std::uint32_t>> chain;
  try{if(!native_owned_box_mips(source,chain))return fail(E_INVALIDARG,"texture.mip_bytes");}
  catch(const std::bad_alloc&){return fail(E_OUTOFMEMORY,"texture.mip_allocation");}
  const UINT levels=static_cast<UINT>(chain.size());
  const UINT kept=levels>skip?skip:0u; // the top levels the budget leaves out
  D3D12TextureRef texture;
  HRESULT hr=d3d12_->create_texture((std::max)(1u,width>>kept),(std::max)(1u,height>>kept),DXGI_FORMAT_B8G8R8A8_UNORM,texture,levels-kept);
  if(FAILED(hr))return fail(hr,"texture.create");
  for(UINT index=0,w=width,h=height;index<levels;++index,w=(std::max)(1u,w/2),h=(std::max)(1u,h/2))
   if(index>=kept&&FAILED(hr=d3d12_->write_texture(*texture,RECT{0,0,LONG(w),LONG(h)},chain[index].data(),w*4u,index-kept)))return fail(hr,"texture.write");
  if(native_owned_texture_serial_>=((1ull<<63)-1))return fail(E_OUTOFMEMORY,"texture.serial");
  try{
   NativeOwnedTexture value{};
   value.backing=(1ull<<63)|++native_owned_texture_serial_;
   value.generation=source.generation;value.format=format;value.width=width;value.height=height;value.mip_count=source.mip_count;
   value.budget_skip=kept;
   value.last_used=native_queue_epoch_;value.last_used_frame=native_frame_index_;value.d3d12=texture;
   backing=value.backing;
   native_owned_textures_[key]=std::move(value);
  }catch(const std::bad_alloc&){return fail(E_OUTOFMEMORY,"texture.cache_allocation");}
  output=std::move(texture);
  return S_OK;
 }
 // Authored levels upload from level `skip` (the budget's top levels left out).
 const UINT kept=source.mip_count>skip?skip:0u;
 D3D12TextureRef texture;
 HRESULT hr=d3d12_->create_texture((std::max)(1u,width>>kept),(std::max)(1u,height>>kept),format,texture,source.mip_count-kept);
 if(FAILED(hr))return fail(hr,"texture.create");
 for(UINT index=0;index<source.mip_count;++index){
  const NativeOwnedTextureMip& mip=source.mips[index];
  if(!mip.data||mip.width!=(std::max)(1u,width>>index)||mip.height!=(std::max)(1u,height>>index))return fail(E_INVALIDARG,"texture.mip_layout");
  const UINT row_pitch=block_bytes?(std::max)(1u,(mip.width+3u)/4u)*block_bytes:mip.width*pixel_bytes;
  const UINT rows=block_bytes?(std::max)(1u,(mip.height+3u)/4u):mip.height;
  if(std::uint64_t(row_pitch)*rows>mip.bytes)return fail(E_INVALIDARG,"texture.mip_bytes");
  if(index>=kept&&FAILED(hr=d3d12_->write_texture(*texture,RECT{0,0,LONG(mip.width),LONG(mip.height)},mip.data,row_pitch,index-kept)))return fail(hr,"texture.write");
 }
 if(native_owned_texture_serial_>=((1ull<<63)-1))return fail(E_OUTOFMEMORY,"texture.serial");
 try{
  NativeOwnedTexture value{};
  value.backing=(1ull<<63)|++native_owned_texture_serial_;
  value.generation=source.generation;value.format=format;value.width=width;value.height=height;value.mip_count=source.mip_count;
  value.budget_skip=kept;
  value.last_used=native_queue_epoch_;value.last_used_frame=native_frame_index_;value.d3d12=texture;
  backing=value.backing;
  native_owned_textures_[key]=std::move(value);
 }catch(const std::bad_alloc&){return fail(E_OUTOFMEMORY,"texture.cache_allocation");}
 output=std::move(texture);
 return S_OK;
}

// draw_game_effect on the D3D12 backend: the same programs, constants and
// fixed-function translation; the elements in t7 (written in stream order
// before the draw), the texture in t0, world fog in b10.
HRESULT Renderer::draw_game_effect_d3d12(const GameEffectDraw& draw,const FixedState& st,SurfaceImage* texture){
 const UINT count=draw.element_count;
 if(!scene_composited_)scene_updated_=true;
 // The transparent effects start (FSR reactive mask); the game's terrain
 // prints draw inline before the water and belong to the opaque world.
 const bool world_print=draw.vertex_program==WorldSupportShader::game_mesh_vs&&!drawing_ground_marks_;
 if(!scene_composited_&&!world_print)capture_opaque_world_d3d12();
 transition_profile_pass(draw.profile_pass);
 const UINT64 bytes=UINT64(count)*sizeof(GameParticleGpu);
 HRESULT hr=native_d3d12_buffer(native12_.game_effects,bytes,sizeof(GameParticleGpu),0);
 if(SUCCEEDED(hr))hr=d3d12_->write_buffer(*native12_.game_effects,0,game_effect_elements_.data(),bytes);
 if(FAILED(hr))return hr;
 GameParticleConstants c{};game_effect_constants(draw,st,texture!=nullptr,c);
 // Soft particles read the world depth they are tested against (read-only view).
 const bool soft=c.soft[3]!=0&&world_d3d12_bound_&&world_d3d12_&&!drawing_ground_marks_;
 if(!soft)c.soft[3]=0;
 const D3D12Texture* texture12=nullptr;
 if(texture){
  texture->world_mip_use=!scene_composited_;if(scene_composited_)texture->static_mip_hint=false;
  if(FAILED(hr=upload(*texture)))return hr;
  texture12=texture->d3d12.get();
 }
 // rt_reflections: this world particle call's billboards for the reflection rays (reflections_d3d12).
 if(rt_reflect_enabled_&&!scene_composited_&&world_d3d12_bound_&&!hit_test_active_&&!drawing_ground_marks_&&
    draw.vertex_program==WorldSupportShader::game_particle_vs)
  collect_reflection_particles_d3d12(draw,c,texture?texture->d3d12:D3D12TextureRef{});
 if(c.fog_mode&&FAILED(hr=bind_world_fog()))return hr;
 d3d12_->clear_slots();
 d3d12_->set_constants(0,&c,sizeof(c));
 d3d12_->set_constants(10,&fog_frame_d3d12_,sizeof(fog_frame_d3d12_));
 bind_world_fog_volume_d3d12();
 // The draw-distance fade blends to the sky probe's colour (borderFogColor,
 // t111 through s7): unbound, flames and smoke past the fade went black.
 d3d12_->set_resource(kSkyCloudShadowSlotD3D12,sky_inputs_active_?sky12_.shadow.get():nullptr);
 d3d12_->set_resource(kSkyProbeSlotD3D12,sky_inputs_active_?sky12_.probe[sky_index_].get():nullptr);
 {D3D11_SAMPLER_DESC linear{};linear.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  linear.AddressU=linear.AddressV=linear.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;linear.MaxLOD=D3D11_FLOAT32_MAX;
  d3d12_->set_sampler(7,linear);}
 d3d12_->set_resource(0,texture12);
 d3d12_->set_resource(7,native12_.game_effects.get());
 if(soft)d3d12_->set_depth_resource(2,world_d3d12_.get());
 const auto sampling=world_texture_sampler(st.stage[0][D3DTSS_MINFILTER],st.stage[0][D3DTSS_MAGFILTER],st.stage[0][D3DTSS_MIPFILTER],
  st.stage[0][D3DTSS_ADDRESSU],st.stage[0][D3DTSS_ADDRESSV],!scene_composited_,1u<<configured_integer(debug_overlay_.config(),"anisotropy",3));
 d3d12_->set_sampler(0,sampling.description);
 {D3D11_SAMPLER_DESC linear{};linear.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  linear.AddressU=linear.AddressV=linear.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;linear.MaxLOD=D3D11_FLOAT32_MAX;d3d12_->set_sampler(7,linear);}
 D3D12NativeState state;
 state.raster=fixed_raster_desc(st.render[D3DRENDERSTATE_CULLMODE],st.render[D3DRENDERSTATE_FILLMODE]==D3DFILL_WIREFRAME,true);
 state.depth=fixed_depth_desc(st.render[D3DRENDERSTATE_ZENABLE],st.render[D3DRENDERSTATE_ZWRITEENABLE],st.render[D3DRENDERSTATE_ZFUNC]);
 state.blend=fixed_blend_choice(st.render[D3DRENDERSTATE_ALPHABLENDENABLE],st.render[D3DRENDERSTATE_SRCBLEND],st.render[D3DRENDERSTATE_DESTBLEND],false);
 // Blended world particles also feed the bloom glow target (cleared on first
 // use each frame), as the D3D11 draw_game_effect binds it: the world colour
 // and the glow as two targets over the depth the world pass has bound now
 // (the pre-water snapshot for ground marks under water, composite_world_d3d12).
 D3D12Texture* glow=nullptr;D3D12Texture* depth=nullptr;bool rebound=false;
 if(!scene_composited_&&world_d3d12_bound_&&world_d3d12_&&st.render[D3DRENDERSTATE_ALPHABLENDENABLE]&&c.coverage_mode!=3u&&
    configured_integer(debug_overlay_.config(),"bloom",0)>0){
  auto& bloom=post12_.bloom;
  UINT width=0,height=0,levels=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
  if(d3d12_->texture_info(*world_d3d12_,width,height,levels,format)&&
     (!bloom.glow||!d3d12_->texture_matches(*bloom.glow,width,height,kGlowFormat))){
   bloom.glow.reset();bloom.glow_frame=UINT64_MAX;
   if(FAILED(d3d12_->create_target(width,height,bloom.glow,kGlowFormat,DXGI_FORMAT_UNKNOWN))){
    bloom.glow.reset();bridge_report_event("bloom_glow","target_creation_failed (D3D12)");}
  }
  if(bloom.glow){
   if(bloom.glow_frame!=native_frame_index_){const float black[4]{};d3d12_->clear_texture(*bloom.glow,black);bloom.glow_frame=native_frame_index_;}
   glow=bloom.glow.get();
   depth=drawing_ground_marks_&&ground_marks_under_water_d3d12()?water12_.depth.get():world_d3d12_.get();
   D3D12Texture* targets[2]={world_d3d12_.get(),glow};
   d3d12_->set_targets(targets,2,depth);rebound=true;
  }
 }
 if(soft){
  if(!rebound){D3D12Texture* world=world_d3d12_.get();depth=world;d3d12_->set_targets(&world,1,world);rebound=true;}
  d3d12_->set_depth_read_only(true);
 }
 const WorldSupportShader pixel=configured_boolean(debug_overlay_.config(),"soft_particles_debug",false)?
  WorldSupportShader::game_particle_debug_ps:WorldSupportShader::game_particle_ps;
 hr=d3d12_->draw(world_support_shader_program(draw.vertex_program),world_support_shader_program(pixel),
                 state,nullptr,draw.vertex_count);
 if(soft){d3d12_->set_depth_read_only(false);d3d12_->set_depth_resource(2,nullptr);}
 // The particle's depth for the water reflections: the second draw of the
 // same call, as D3D11 draw_game_particle_depth follows it.
 if(hr==S_OK&&draw.reflection_depth&&c.coverage_mode!=3u&&!scene_composited_&&particle_depth_wanted())
  (void)draw_game_particle_depth_d3d12(world_support_shader_program(draw.vertex_program),draw.vertex_count,state);
 d3d12_->clear_slots();
 if(rebound){ // back to the world target and the depth bound before
  if(depth!=world_d3d12_.get()){D3D12Texture* world=world_d3d12_.get();d3d12_->set_targets(&world,1,depth);}
  else bind_world_targets_d3d12();
 }
 if(FAILED(hr))return hr;
 if(d3d12_not_done(hr)){if(hr==S_FALSE)d3d12_->omit("game effects (pipeline not ready)",false);return S_OK;}
 d3d12_->count("game effect draws");
 return S_OK;
}

// draw_game_particle_depth on D3D12: the particle call just drawn again
// (same vertex stage, elements, texture, constants) with gameParticleDepthPS,
// min-blending the screen depth of its visible coverage into the current R32F
// slot, tested against the world depth without writing it. Slots, clears and
// the hand-over to the next frame's reflections (capture_water_history) are
// the D3D11 ones. A failure turns particle reflections off, named once.
HRESULT Renderer::draw_game_particle_depth_d3d12(const ShaderProgram& vertex,UINT vertex_count,const D3D12NativeState& particle){
 if(!world_d3d12_)return E_FAIL;
 const auto unavailable=[&](HRESULT hr,const char* name){
  particle_depth12_={};particle_depth_failed_=true;
  bridge_report_event("particle_reflections","unavailable");d3d12_->omit(name,false);return hr;
 };
 UINT width=0,height=0,levels=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
 if(!d3d12_->texture_info(*world_d3d12_,width,height,levels,format))return E_FAIL;
 HRESULT hr=S_OK;
 if(!particle_depth12_[0]||!particle_depth12_[1]||particle_depth_width_!=width||particle_depth_height_!=height){
  particle_depth12_={};particle_depth_frame_=particle_depth_history_frame_=UINT64_MAX;particle_depth_width_=particle_depth_height_=0;
  for(auto& slot:particle_depth12_)
   if(FAILED(hr=d3d12_->create_target(width,height,slot,kParticleDepthFormat,DXGI_FORMAT_UNKNOWN)))
    return unavailable(hr,"particle depth for water reflections (targets unavailable)");
  particle_depth_width_=width;particle_depth_height_=height;
 }
 transition_profile_pass("particle_reflection_depth");
 D3D12Texture* const output=particle_depth12_[particle_depth_current_].get();
 if(particle_depth_frame_!=native_frame_index_){
  const float far_depth[4]={1,1,1,1};d3d12_->clear_texture(*output,far_depth);
  particle_depth_frame_=native_frame_index_;
 }
 D3D12NativeState state=particle;
 auto& blend=state.custom_blend;
 blend.custom=1;blend.source=blend.destination=blend.source_alpha=blend.destination_alpha=D3D11_BLEND_ONE;
 blend.op=blend.op_alpha=D3D11_BLEND_OP_MIN;blend.write_mask=D3D11_COLOR_WRITE_ENABLE_RED;
 state.depth={};state.depth.DepthEnable=TRUE;state.depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;state.depth.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;
 D3D12Texture* targets[1]={output};
 d3d12_->set_targets(targets,1,world_d3d12_.get());
 hr=d3d12_->draw(vertex,world_support_shader_program(WorldSupportShader::game_particle_depth_ps),state,nullptr,vertex_count);
 bind_world_targets_d3d12();
 if(FAILED(hr))return unavailable(hr,"particle depth for water reflections (draw failed)");
 if(hr==S_FALSE)d3d12_->omit("particle depth for water reflections (pipeline not ready)",false);
 if(hr==S_OK){profiler_.add_draw(vertex_count/3u,1);profiler_.add_batch();d3d12_->count("particle reflection depth draws");}
 return hr;
}

void Renderer::d3d12_omit(const char* name,bool world) noexcept{if(d3d12_)d3d12_->omit(name,world);}

// prepare_native_poses on the D3D12 backend: the frame's pose jobs evaluated
// by nativePoseCS (skeleton assets in the animation buffer, t10; the jobs,
// t11; the work range, b4) into the pose nodes (u4).
HRESULT Renderer::prepare_native_poses_d3d12(){
 if(native_pose_work_.empty())return S_OK;
 try{
  native_pose_upload_.clear();
  for(const auto& job:native_pose_work_){
   if(job.allocation>=native_pose_ranges_.size()||!native_pose_ranges_[job.allocation].references||
      native_pose_ranges_[job.allocation].generation!=job.allocation_generation)continue;
   if(job.baked){
    auto nodes=NativeWorldGeometry::Pose::place_baked(*job.baked,job.value.root_to_world,job.value.generation);
    const HRESULT written=d3d12_->write_buffer(*native12_.pose_nodes,UINT64(job.value.output)*sizeof(NativePoseNodeGpu),nodes.data(),nodes.size()*sizeof(NativePoseNodeGpu));
    if(FAILED(written))return written;
    profiler_.add_upload(nodes.size()*sizeof(NativePoseNodeGpu));
    auto& slot=native_pose_slots_[job.slot];if(slot.revision==job.revision&&slot.value.output==job.value.output)slot.valid=true;
    continue;
   }
   native_pose_upload_.push_back(job.value);
  }
  if(native_pose_upload_.empty())return S_OK;
  const auto count=static_cast<UINT>(native_pose_upload_.size());
  const UINT width=(std::min)(count,65535u),height=(count+width-1)/width;
  if(height>65535u)return E_OUTOFMEMORY;
  const ShaderProgram* pose_cs=nullptr;
  for(const auto& program:native12_.programs)if(std::strcmp(program.entry,"nativePoseCS")==0){pose_cs=&program;break;}
  if(!pose_cs||!native12_.animation||!native12_.pose_nodes)return E_FAIL;
  const UINT64 bytes=UINT64(count)*sizeof(NativePoseGpu);
  HRESULT hr=native_d3d12_buffer(native12_.pose_inputs,bytes,sizeof(NativePoseGpu),0);
  if(SUCCEEDED(hr))hr=d3d12_->write_buffer(*native12_.pose_inputs,0,native_pose_upload_.data(),bytes);
  if(FAILED(hr))return hr;
  transition_profile_pass("compute_pose");
  d3d12_->clear_slots();
  const std::array<UINT,4> work_range{0,count,0,0};
  d3d12_->set_constants(4,work_range.data(),sizeof(work_range));
  d3d12_->set_resource(10,native12_.animation.get());
  d3d12_->set_resource(11,native12_.pose_inputs.get());
  d3d12_->set_unordered(4,native12_.pose_nodes.get());
  hr=d3d12_->dispatch(*pose_cs,width,height,1);
  if(FAILED(hr))return hr;
  if(d3d12_not_done(hr)){if(hr==S_FALSE)d3d12_->omit("poses (pipeline not ready)",false);return S_OK;}
  for(const auto& job:native_pose_work_){auto& slot=native_pose_slots_[job.slot];
   if(job.allocation<native_pose_ranges_.size()&&native_pose_ranges_[job.allocation].references&&
      native_pose_ranges_[job.allocation].generation==job.allocation_generation&&
      slot.revision==job.revision&&slot.value.output==job.value.output)slot.valid=true;}
  return S_OK;
 }catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
}

// The world target for this engine frame: created at the world size (the
// render scale of the output size, or a temporal upscaler's render size),
// bound and cleared as D3D11 binds scene_composite_.target() and world_dsv_
// at begin_engine_frame_at. With an upscaler its targets too; below 100%
// render scale without one, the world-size resolved image the composite
// writes and upscales (SceneComposite::create's resolved_).
HRESULT Renderer::bind_world_d3d12(){
 // A frame begin inside the zone hit-test frame keeps drawing into its target
 // (begin_engine_frame_at's hit-test rebind on D3D11).
 if(hit_test_active_){world_d3d12_bound_=true;bind_world_targets_d3d12();return S_OK;}
 if(!world_d3d12_||!d3d12_->texture_matches(*world_d3d12_,width_,height_,DXGI_FORMAT_R11G11B10_FLOAT)){
  world_d3d12_.reset();
  const HRESULT hr=d3d12_->create_target(width_,height_,world_d3d12_,DXGI_FORMAT_R11G11B10_FLOAT);
  if(FAILED(hr)){d3d12_->omit("world target (creation failed)",true);return hr;}
 }
 ensure_guide_targets_d3d12();
 if(temporal_upscaler()){if(FAILED(ensure_upscaler_d3d12()))d3d12_->omit("temporal upscaler targets (creation failed)",true);}
 else if(width_!=output_width_||height_!=output_height_){
  auto& up=upscale12_;
  if(!up.resolved||!d3d12_->texture_matches(*up.resolved,width_,height_,DXGI_FORMAT_B8G8R8A8_UNORM)){
   up.resolved.reset();
   if(FAILED(d3d12_->create_target(width_,height_,up.resolved,DXGI_FORMAT_B8G8R8A8_UNORM,DXGI_FORMAT_UNKNOWN))){
    up.resolved.reset();d3d12_->omit("render-scale resolved image (creation failed)",true);
   }
  }
 }else upscale12_.resolved.reset();
 world_d3d12_bound_=true;
 if(FAILED(ensure_shadow_history_d3d12()))d3d12_->omit("temporal sun-shadow history (targets unavailable)",false);
 // The AO ambient share for this world frame (cleared at its start), then the
 // world's targets: its colour, and with AO the share at slot 2.
 if(FAILED(update_ambient_occlusion_d3d12()))d3d12_->omit("screen-space ambient occlusion (targets unavailable)",false);
 bind_world_targets_d3d12();
 return S_OK;
}

// create_shadow_history_targets on D3D12: two R8 visibility targets and a
// depth-only target of the world depth's format (D24S8), at the world
// target's size. Remade with the world size; a remake starts without history.
HRESULT Renderer::ensure_shadow_history_d3d12(){
 UINT width=0,height=0,levels=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
 if(!world_d3d12_||!d3d12_->texture_info(*world_d3d12_,width,height,levels,format))return E_FAIL;
 if(shadow_history_mask12_[0]&&shadow_history_mask12_[1]&&shadow_history_depth12_&&
    d3d12_->texture_matches(*shadow_history_mask12_[0],width,height,DXGI_FORMAT_R8_UNORM))return S_OK;
 for(auto& mask:shadow_history_mask12_)mask.reset();shadow_history_depth12_.reset();
 shadow_history_valid_=shadow_history_written_=shadow_history_committed_=false;
 shadow_history_previous_frame_=shadow_history_current_frame_=UINT64_MAX;
 HRESULT hr=S_OK;
 for(auto& mask:shadow_history_mask12_)
  if(FAILED(hr=d3d12_->create_target(width,height,mask,DXGI_FORMAT_R8_UNORM,DXGI_FORMAT_UNKNOWN)))break;
 if(SUCCEEDED(hr))hr=d3d12_->create_target(width,height,shadow_history_depth12_,DXGI_FORMAT_UNKNOWN,DXGI_FORMAT_D24_UNORM_S8_UINT);
 if(FAILED(hr)){for(auto& mask:shadow_history_mask12_)mask.reset();shadow_history_depth12_.reset();}
 return hr;
}

// The prepass receiver record (rest offset and flags, RGBA16F; terrain
// normal, RG16 UNORM) and the mask (RG16F), colour only, at the world
// target's size (the D3D11 create_targets formats). A failed size is not
// retried until the world size changes.
HRESULT Renderer::ensure_prepass_targets_d3d12(){
 UINT width=0,height=0,levels=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
 if(!world_d3d12_||!d3d12_->texture_info(*world_d3d12_,width,height,levels,format))return E_FAIL;
 if(prepass_rest12_&&prepass_normal12_&&shadow_mask12_&&d3d12_->texture_matches(*prepass_rest12_,width,height,DXGI_FORMAT_R16G16B16A16_FLOAT))return S_OK;
 prepass_rest12_.reset();prepass_normal12_.reset();shadow_mask12_.reset();terrain_miss12_.reset();terrain_miss_quads12_.reset();
 if(prepass_failed_width12_==width&&prepass_failed_height12_==height)return E_FAIL;
 HRESULT hr=d3d12_->create_target(width,height,prepass_rest12_,DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_UNKNOWN);
 if(SUCCEEDED(hr))hr=d3d12_->create_target(width,height,prepass_normal12_,DXGI_FORMAT_R16G16_UNORM,DXGI_FORMAT_UNKNOWN);
 if(SUCCEEDED(hr))hr=d3d12_->create_target(width,height,shadow_mask12_,DXGI_FORMAT_R16G16_FLOAT,DXGI_FORMAT_UNKNOWN);
 if(FAILED(hr)){
  prepass_rest12_.reset();prepass_normal12_.reset();shadow_mask12_.reset();
  prepass_failed_width12_=width;prepass_failed_height12_=height;
  return hr;
 }
 prepass_failed_width12_=prepass_failed_height12_=0;
 // The slim terrain pass's mask (terrain_miss12_) is made by the first flush
 // that draws the slim pass, at this size.
 return S_OK;
}

// The receiver record's rest offset reads zero until a prepass writes it;
// then its targets with the world depth. The mask pass shades only the
// pixels this prepass marked in stencil, and every marking fragment also
// wrote the record (nativePrepassPS, gcDepthPS); of the other texels it
// reads only the offset (nativeTexelStep's neighbours), never the flags. So
// the clear's w is free: 1, the target's optimized clear value
// (create_target), so the driver can fast-clear instead of writing 8 B a
// pixel (once per prepass, up to 4 a frame).
void Renderer::begin_world_prepass_d3d12() noexcept{
 const float zero[4]{0.f,0.f,0.f,1.f};
 d3d12_->clear_texture(*prepass_rest12_,zero);
 D3D12Texture* const targets[2]={prepass_rest12_.get(),prepass_normal12_.get()};
 d3d12_->set_targets(targets,2,world_d3d12_.get());
}

// The prepass's receiver mark (the D3D11 prepass's stencil bit 0x80): every
// depth-passing, unrejected prepass fragment increments the world stencil
// (saturating), so a non-zero value marks a receiver; the reference stays the
// command list's 0 (no stencil reference is set on this backend). The mask
// pass clears the marks once it has shaded them (resolve_world_shadow_mask_d3d12).
void Renderer::mark_receiver_stencil(D3D11_DEPTH_STENCIL_DESC& depth) noexcept{
 depth.StencilEnable=TRUE;depth.StencilReadMask=0xFF;depth.StencilWriteMask=0xFF;
 depth.FrontFace={D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_INCR_SAT,D3D11_COMPARISON_ALWAYS};
 depth.BackFace=depth.FrontFace;
}

// The mask pass (nativeShadowMaskVS/PS, full screen): the world depth read as
// a shader input and bound read-only for the stencil test (only the marked
// pixels shade, as the D3D11 shadow_mask_depth_state_), the receiver record at t117/t118,
// the depth at t119 and the owned sky's cloud shadow it takes with the sun
// shadow. The mask is then bound at t120 for the material pass. S_FALSE: the
// pipeline is not ready (the caller shades unmasked).
HRESULT Renderer::resolve_world_shadow_mask_d3d12(){
 const ShaderProgram* vertex=nullptr;const ShaderProgram* pixel=nullptr;
 for(const auto& program:native12_.programs){
  if(program.defines)continue;
  if(std::strcmp(program.entry,"nativeShadowMaskVS")==0)vertex=&program;
  else if(std::strcmp(program.entry,"nativeShadowMaskPS")==0)pixel=&program;
 }
 if(!vertex||!pixel||!shadow_mask12_)return E_FAIL;
 // rt_sun_shadows: the mask's sun term traced through this frame's ray
 // tracing scene (the NATIVE_RT_SUN program; update_raytracing_scene_d3d12).
 const bool traced=rt12_.shadow_frame==native_frame_index_&&rt12_.scene&&rt12_.records&&rt12_.programs.size()>kRtSunMask;
 transition_profile_pass(traced?"rt_sun_shadows":"shadow_mask");
 D3D12Texture* const mask=shadow_mask12_.get();
 // prepass_depth_check: the mask starts at -1 (no value), which a masked
 // material pixel shows as the magenta failure colour, so a receiver the
 // stencil test skipped is visible; 3 inverts the stencil test (calibration:
 // every masked receiver turns magenta).
 const int depth_check=configured_integer(debug_overlay_.config(),"prepass_depth_check",0);
 if(depth_check!=0){const float none[4]{-1.f,-1.f,0.f,0.f};d3d12_->clear_texture(*mask,none);}
 d3d12_->set_targets_read_depth(&mask,1,world_d3d12_.get());
 d3d12_->set_resource(kShadowMaskSlotD3D12,static_cast<const D3D12Texture*>(nullptr));
 d3d12_->set_resource(kPrepassRestSlotD3D12,prepass_rest12_.get());
 d3d12_->set_resource(kPrepassNormalSlotD3D12,prepass_normal12_.get());
 d3d12_->set_depth_resource(kPrepassDepthSlotD3D12,world_d3d12_.get());
 d3d12_->set_resource(kSkyCloudShadowSlotD3D12,sky_inputs_active_?sky12_.shadow.get():nullptr);
 {D3D11_SAMPLER_DESC linear{};linear.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  linear.AddressU=linear.AddressV=linear.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;linear.MaxLOD=D3D11_FLOAT32_MAX;d3d12_->set_sampler(7,linear);}
 D3D12NativeState state;
 state.raster.FillMode=D3D11_FILL_SOLID;state.raster.CullMode=D3D11_CULL_NONE;state.raster.DepthClipEnable=TRUE;
 state.depth.DepthEnable=FALSE;state.depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;state.depth.DepthFunc=D3D11_COMPARISON_ALWAYS;
 state.depth.StencilEnable=TRUE;state.depth.StencilReadMask=0xFF;state.depth.StencilWriteMask=0;
 state.depth.FrontFace={D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,depth_check==3?D3D11_COMPARISON_EQUAL:D3D11_COMPARISON_NOT_EQUAL};
 state.depth.BackFace=state.depth.FrontFace;
 HRESULT hr=S_FALSE;
 if(traced){
  d3d12_->set_resource(94,rt12_.scene.get());d3d12_->set_resource(95,rt12_.vertices.get());
  d3d12_->set_resource(99,rt12_.indices.get());d3d12_->set_resource(113,rt12_.records.get());
  // The cut-out cards' alpha (s0; every material batch sets its own).
  D3D11_SAMPLER_DESC card{};card.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  card.AddressU=card.AddressV=card.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;card.MaxLOD=D3D11_FLOAT32_MAX;
  d3d12_->set_sampler(0,card);
  hr=d3d12_->draw(*vertex,rt12_.programs[kRtSunMask],state,nullptr,3);
  for(const UINT slot:{94u,95u,99u,113u})d3d12_->set_resource(slot,static_cast<const D3D12Buffer*>(nullptr));
  if(hr==S_OK)d3d12_->count("ray traced sun shadow masks");
  // Its pipeline still compiling: this pass takes the sun maps (named).
  else if(hr==S_FALSE){d3d12_->omit("rt_sun_shadows (pipeline not ready: sun maps this pass)",false);transition_profile_pass("shadow_mask");}
 }
 if(hr==S_FALSE)hr=d3d12_->draw(*vertex,*pixel,state,nullptr,3);
 // The receiver inputs leave; the depth returns to the world target's use.
 d3d12_->set_resource(kPrepassRestSlotD3D12,static_cast<const D3D12Texture*>(nullptr));
 d3d12_->set_resource(kPrepassNormalSlotD3D12,static_cast<const D3D12Texture*>(nullptr));
 d3d12_->set_depth_resource(kPrepassDepthSlotD3D12,nullptr);
 if(hr!=S_OK){
  if(hr==S_FALSE)d3d12_->omit("shadow mask (pipeline not ready: unmasked shading)",false);
  return FAILED(hr)?hr:S_FALSE;
 }
 profiler_.add_draw(1,1);profiler_.add_batch();
 // The marks are consumed, as the D3D11 mask pass zeroes its bit: a later
 // flush's mask pass shades only its own prepass's receivers instead of
 // launching on every earlier one. The read-only view above cannot write
 // stencil (the shader reads the depth plane in the same draw), so the
 // stencil plane is cleared here; nothing else reads the marks.
 d3d12_->clear_stencil_of(*world_d3d12_,0);
 d3d12_->set_resource(kShadowMaskSlotD3D12,mask);
 return S_OK;
}

// The history inputs of a pass that reads it (the D3D11 flush's b6, t20,
// t21 and s3): the b6 values prepare_shadow_history just wrote (zero when
// inactive), the previous visibility and the previous world depth.
void Renderer::bind_shadow_history_d3d12(bool active) noexcept{
 d3d12_->set_constants(6,shadow_history_constants12_.data(),UINT(shadow_history_constants12_.size()));
 d3d12_->set_resource(20,active?shadow_history_mask12_[shadow_history_previous_index_].get():nullptr);
 d3d12_->set_depth_resource(21,active?shadow_history_depth12_.get():nullptr);
 D3D11_SAMPLER_DESC sampler{};sampler.Filter=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
 sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sampler.MaxLOD=D3D11_FLOAT32_MAX;
 d3d12_->set_sampler(3,sampler);
}

// The world pass's targets, as the D3D11 flush binds them for world batches:
// the world colour and depth, with history the current shadow-history target
// at slot 1 (until the frame's history is committed), and while AO runs this
// frame the ambient share at slot 2. Every world draw returns to these; the
// world pixel programs write the visibility (SV_Target1) and share (SV_Target2).
// guides (rt_ray_reconstruction): the Ray Reconstruction guide targets at 5-7
// for a draw of a guide twin (NATIVE_RR_GUIDES, GC_RR_GUIDES); the object
// motion targets keep 3 and 4 (null without object_motion).
void Renderer::bind_world_targets_d3d12(bool history,bool object_motion,bool guides) noexcept{
 // The zone hit-test frame: its target alone (as rtv_ alone on D3D11), not
 // the world's role (no marker colour in the zone mask; hit_test_frame_apply).
 if(hit_test_active_){world_history_bound12_=false;d3d12_->set_target(world_d3d12_.get(),false);return;}
 D3D12Texture* const visibility=history&&!scene_composited_&&!shadow_history_committed_&&
  shadow_history_current_frame_==native_frame_index_?shadow_history_mask12_[shadow_history_current_index_].get():nullptr;
 world_history_bound12_=visibility!=nullptr;
 D3D12Texture* const ambient=ao12_.frame==native_frame_index_&&ao12_.ambient&&!scene_composited_?ao12_.ambient.get():nullptr;
 if(guides&&guide_frame_d3d12()){
  D3D12Texture* targets[8]={world_d3d12_.get(),visibility,ambient,object_motion?upscale12_.object_motion.get():nullptr,
                            object_motion?upscale12_.object_depth.get():nullptr,
                            upscale12_.guide_normal.get(),upscale12_.guide_diffuse.get(),upscale12_.guide_specular.get()};
  d3d12_->set_targets(targets,8,world_d3d12_.get());
 }else if(object_motion){ // the object motion targets at 3 and 4 (the ground cover material pass's GC_PIXEL_MOTION)
  D3D12Texture* targets[5]={world_d3d12_.get(),visibility,ambient,upscale12_.object_motion.get(),upscale12_.object_depth.get()};
  d3d12_->set_targets(targets,5,world_d3d12_.get());
 }else if(ambient){
  D3D12Texture* targets[3]={world_d3d12_.get(),visibility,ao12_.ambient.get()};
  d3d12_->set_targets(targets,3,world_d3d12_.get());
 }else if(visibility){
  D3D12Texture* targets[2]={world_d3d12_.get(),visibility};
  d3d12_->set_targets(targets,2,world_d3d12_.get());
 }else d3d12_->set_target(world_d3d12_.get(),true);
}

// update_ambient_occlusion_targets on D3D12: the full-size ambient share and
// the low-resolution depth, raw and filtered AO for the preset's divisor,
// created on demand (a failed size is not retried until the size or preset
// changes); the share is cleared once for this world frame. bind_world_d3d12
// runs again inside the frame (ground cover): that call keeps the share the
// terrain and neighbour material already wrote, as D3D11 clears it only at
// the composite (update_ambient_occlusion_targets). Clearing it there left
// those surfaces without screen-space AO on D3D12.
// rt_indirect_resolution: 0 half, 1 a quarter, 2 the full render size (a ray and a reservoir a render pixel).
// Under Ray Reconstruction 0 is the full size too: Ray Reconstruction assumes independent per-pixel samples (NVIDIA's DLSS-RR guide, 3.5).
UINT Renderer::rt_indirect_divisor() const noexcept{const int v=configured_integer(debug_overlay_.config(),"rt_indirect_resolution",0);return v>=2||(v==0&&ray_reconstruction_on())?1u:v==1?4u:2u;}
int Renderer::ambient_occlusion_quality_d3d12() const noexcept{return rt_ao_enabled_?(rt_indirect_divisor()==4u?1:2):configured_integer(debug_overlay_.config(),"ao_quality",2);}
HRESULT Renderer::update_ambient_occlusion_d3d12(){
 auto& ao=ao12_;
 const auto preset=ambient_occlusion_preset(ambient_occlusion_quality_d3d12());
 if(ao.frame==native_frame_index_&&preset.divisor&&ao.divisor==preset.divisor&&ao.ambient&&world_d3d12_&&
    d3d12_->texture_matches(*ao.ambient,width_,height_,DXGI_FORMAT_R16G16B16A16_FLOAT))return S_OK;
 ao.frame=UINT64_MAX;
 if(!preset.divisor||!world_d3d12_){ao.ambient.reset();ao.raw.reset();ao.filtered.reset();ao.depth.reset();ao.divisor=0;return S_OK;}
 UINT width=0,height=0,levels=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
 if(!d3d12_->texture_info(*world_d3d12_,width,height,levels,format))return E_FAIL;
 const UINT low_width=ambient_occlusion_extent(width,preset.divisor),low_height=ambient_occlusion_extent(height,preset.divisor);
 if(!ao.ambient||ao.divisor!=preset.divisor||!d3d12_->texture_matches(*ao.ambient,width,height,DXGI_FORMAT_R16G16B16A16_FLOAT)){
  if(ao.failed_width==width&&ao.failed_height==height&&ao.failed_divisor==preset.divisor)return E_FAIL;
  ao.ambient.reset();ao.raw.reset();ao.filtered.reset();ao.depth.reset();ao.divisor=0;
  HRESULT hr=S_OK;
  if(FAILED(hr=d3d12_->create_target(width,height,ao.ambient,DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_UNKNOWN))||
     FAILED(hr=d3d12_->create_target(low_width,low_height,ao.raw,DXGI_FORMAT_R16G16_FLOAT,DXGI_FORMAT_UNKNOWN))||
     FAILED(hr=d3d12_->create_target(low_width,low_height,ao.filtered,DXGI_FORMAT_R16G16_FLOAT,DXGI_FORMAT_UNKNOWN))||
     FAILED(hr=d3d12_->create_target(low_width,low_height,ao.depth,DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_UNKNOWN))){
   ao.ambient.reset();ao.raw.reset();ao.filtered.reset();ao.depth.reset();
   ao.failed_width=width;ao.failed_height=height;ao.failed_divisor=preset.divisor;
   return hr;
  }
  ao.divisor=preset.divisor;ao.failed_width=ao.failed_height=ao.failed_divisor=0;
 }
 const float zero[4]{};d3d12_->clear_texture(*ao.ambient,zero);
 ao.frame=native_frame_index_;
 return S_OK;
}

// render_ambient_occlusion on D3D12: the same four passes, constants and
// targets (compact low-resolution depth, horizon AO, depth-aware denoise,
// upsample removing the occluded ambient share from the world colour by a
// reverse-subtract blend that keeps its alpha). S_FALSE: no camera this frame.
// traced (rt_ambient_occlusion): the visibility and linear depth per half
// pixel the indirect light's sampling pass wrote this frame, in the horizon
// pass's place (no compact depth, no horizon search); the denoise and the
// resolve as they are.
HRESULT Renderer::ambient_occlusion_d3d12(const D3D12Texture* traced){
 auto& ao=ao12_;
 if(ao.programs.empty())append_ambient_occlusion_shader_programs(ao.programs); // aoFullscreenVS, prepare, horizon, denoise, resolve
 if(ao.programs.size()<5)return E_FAIL;
 const auto preset=ambient_occlusion_preset(ambient_occlusion_quality_d3d12());
 const auto scene=bridge_scene_world_to_clip_snapshot();
 AmbientOcclusionConstants constants{};
 if(preset.divisor!=ao.divisor||!scene.valid||!ambient_occlusion_invert(scene.world_to_clip.data(),constants.clip_to_world))return S_FALSE;
 UINT width=0,height=0,levels=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
 if(!d3d12_->texture_info(*world_d3d12_,width,height,levels,format))return E_FAIL;
 const auto& config=debug_overlay_.config();
 const auto environment=bridge_world_fog_snapshot().environment;
 const float low_light=environment.valid?(std::max)(fog_night(environment.hour),
  environment.cave?0.f:.6f*fog_precipitation(environment)):0.f;
 const auto frame=ambient_occlusion_frame(configured_floating_point(config,"ao_strength",.75f),
  configured_floating_point(config,"ao_radius",1.5f),configured_floating_point(config,"ao_low_light_boost",1.6f),
  native_world_lighting_.ambient.data(),native_world_lighting_.sunlight_diffuse.data(),low_light);
 const UINT low_width=ambient_occlusion_extent(width,ao.divisor),low_height=ambient_occlusion_extent(height,ao.divisor);
 std::memcpy(constants.world_to_clip,scene.world_to_clip.data(),sizeof(constants.world_to_clip));
 const float full[4]={float(width),float(height),1.f/width,1.f/height};
 const float low[4]={float(low_width),float(low_height),1.f/low_width,1.f/low_height};
 std::memcpy(constants.full_size,full,sizeof(full));std::memcpy(constants.low_size,low,sizeof(low));
 const float params[4]={frame.radius,(std::max)(8.f,height*.08f),frame.strength,.03f*ao.divisor};
 std::memcpy(constants.params,params,sizeof(params));
 constants.control[0]=ao.divisor;constants.control[1]=preset.slices;constants.control[2]=preset.steps;
 bool least=false; // the next pass keeps the smaller of its output and the target (rt_ao_contact)
 const auto pass=[&](D3D12Texture* target,const ShaderProgram& pixel,const D3D12Texture* low_input,bool resolve)->HRESULT{
  d3d12_->clear_slots();
  d3d12_->set_constants(1,&constants,sizeof(constants));
  d3d12_->set_depth_resource(0,world_d3d12_.get());
  d3d12_->set_resource(1,low_input);
  d3d12_->set_resource(2,ao.ambient.get());
  d3d12_->set_targets(&target,1,nullptr); // no depth: the world depth is read
  D3D12NativeState state=post_state(false,false);
  if(resolve){ // world = world - occluded share; its alpha kept
   auto& blend=state.custom_blend;blend.custom=1;
   blend.source=blend.destination=D3D11_BLEND_ONE;blend.op=D3D11_BLEND_OP_REV_SUBTRACT;
   blend.source_alpha=D3D11_BLEND_ZERO;blend.destination_alpha=D3D11_BLEND_ONE;blend.op_alpha=D3D11_BLEND_OP_ADD;
  }else if(least){ // the smaller visibility (and depth: a half pixel without a lit surface holds the sky's) stays
   auto& blend=state.custom_blend;blend.custom=1;
   blend.source=blend.destination=blend.source_alpha=blend.destination_alpha=D3D11_BLEND_ONE;
   blend.op=blend.op_alpha=D3D11_BLEND_OP_MIN;
  }
  return d3d12_->draw(ao.programs[0],pixel,state,nullptr,3);
 };
 HRESULT hr=S_OK;
 if(traced){
  UINT passes=2;
  hr=pass(ao.filtered.get(),ao.programs[3],traced,false);
  // rt_ao_contact: what is not in the ray tracing scene (grass blades, fur,
  // critters, a unit's present pose within the rays' 0.25 m skip) still
  // occludes at contact range: the horizon search at rt_ao_contact_radius
  // (the screen-space reach by default, so nothing the screen-space passes
  // shade is lost), its denoised visibility kept where it is the smaller one
  // (the usual pairing of ray traced and screen-space occlusion; the
  // smaller, not the product, so a crease both see is not darkened twice).
  if(hr==S_OK&&configured_boolean(config,"rt_ao_contact",true)){
   const float radius=constants.params[0];
   constants.params[0]=(std::min)(radius,(std::clamp)(configured_floating_point(config,"rt_ao_contact_radius",1.5f),.1f,4.f));
   transition_profile_pass("rt_ambient_occlusion_contact");
   if((hr=pass(ao.depth.get(),ao.programs[1],nullptr,false))==S_OK&&
      (hr=pass(ao.raw.get(),ao.programs[2],ao.depth.get(),false))==S_OK){
    least=true;hr=pass(ao.filtered.get(),ao.programs[3],ao.raw.get(),false);least=false;
   }
   constants.params[0]=radius;
   transition_profile_pass("rt_ambient_occlusion");
   passes=5;
  }
  if(hr==S_OK)hr=pass(world_d3d12_.get(),ao.programs[4],ao.filtered.get(),true);
  d3d12_->clear_slots();
  if(hr==S_OK){profiler_.add_draw(passes,passes);profiler_.add_batch(passes);}
  return hr;
 }
 if((hr=pass(ao.depth.get(),ao.programs[1],nullptr,false))==S_OK)
  hr=pass(ao.raw.get(),ao.programs[2],ao.depth.get(),false);
 if(hr==S_OK){
  transition_profile_pass("ambient_occlusion_resolve");
  if((hr=pass(ao.filtered.get(),ao.programs[3],ao.raw.get(),false))==S_OK)
   hr=pass(world_d3d12_.get(),ao.programs[4],ao.filtered.get(),true);
 }
 d3d12_->clear_slots();
 if(hr==S_OK){profiler_.add_draw(4,4);profiler_.add_batch(4);}
 return hr;
}

// rt_term_view (F10, diagnostic): the world image replaced by one input or
// term of the ray traced passes per pixel, read from this frame's images:
// 1 the owner the guides name (red a unit, green terrain by its facing
//   rule, lime the ground cover on a traced frame, blue another lit
//   receiver, yellow a surface with guides the traced passes do not light;
//   grey a drawn pixel without
//   guides; black the sky), the depth test's winner among the guide writers;
// 2 the sun term of the mask; 3 the indirect factor (x4); 4 the ambient
//   occlusion's visibility as resolved (half resolution, nearest);
// 5 the guide albedo; 6 the guide normal; 7 the object motion a posed
//   record wrote (red and green: 8 pixels a unit, blue: a record wrote it);
// 8 the lighting factor the twin shaded with and the fog share (blue);
// 9-12 the bounce light alone (rt_indirect_light): what it adds to the
//   lighting factor, saturate(d + indirect) - d, no albedo, no other term;
//   9 linear (x4) and 10 tonemapped (x8 / (1 + x8)) in the world image (the
//   upscaler, Ray Reconstruction included, takes it as its input); 11 and
//   12 the same drawn over the upscaled output, the render pixel nearest
//   to each output pixel: the signal as the upscaler received it.
// tvControl.zw: render pixels per output pixel (asfloat; 1 in the world image).
constexpr char kRtTermViewShader[] =
"cbuffer RtTermView:register(b1){uint4 tvControl;};\n"
"Texture2D<float> tvDepth:register(t0);\n"
"Texture2D<float4> tvNormal:register(t1);\n"
"Texture2D<float4> tvDiffuse:register(t2);\n"
"Texture2D<float4> tvSpecular:register(t3);\n"
"Texture2D<float2> tvMask:register(t4);\n"
"Texture2D<float4> tvIndirect:register(t5);\n"
"Texture2D<float2> tvOcclusion:register(t6);\n"
"Texture2D<float2> tvMotion:register(t7);\n"
"Texture2D<float> tvMotionDepth:register(t8);\n"
"float4 rtTermViewPS(float4 position:SV_Position):SV_Target{\n"
" int3 at=int3(position.xy*asfloat(tvControl.zw),0);\n"
" float z=tvDepth.Load(at);\n"
" float4 n=tvNormal.Load(at),d=tvDiffuse.Load(at),sp=tvSpecular.Load(at);\n"
" uint a=uint(d.a*65535+.5),b=uint(sp.a*65535+.5);\n"
" bool guided=dot(n.xyz,n.xyz)>.25,lit=guided&&((b>>7)&255u)!=0&&((a>>14)&1u)!=0;\n"
" uint mode=tvControl.x;\n"
" if(mode==1){\n"
"  if(!(z<1))return float4(0,0,0,1);\n"
"  if(!guided)return float4(.5,.5,.5,1);\n"
"  if(!lit)return float4(1,.9,0,1);\n"
"  if(((b>>15)&1u)!=0)return ((a>>15)&1u)!=0?float4(.6,1,.1,1):float4(1,.1,.1,1);\n"
"  return ((a>>15)&1u)!=0?float4(.1,.8,.1,1):float4(.15,.3,1,1);\n"
" }\n"
" if(mode==2){float v=tvMask.Load(at).x;return v>1.5?float4(.3,0,.3,1):float4(v,v,v,1);}\n"
" if(mode==3)return float4(tvIndirect.Load(at).rgb*4,1);\n"
" if(mode==4){float2 o=tvOcclusion.Load(int3(at.xy/max(tvControl.y,1u),0));return o.y>=65000?float4(0,0,.4,1):float4(o.x,o.x,o.x,1);}\n"
" if(mode==5)return float4(d.rgb,1);\n"
" if(mode==6)return float4(n.xyz*.5+.5,1);\n"
" if(mode==7){float2 m=tvMotion.Load(at);float w=tvMotionDepth.Load(at);return float4(abs(m)/8,w!=0?1:0,1);}\n"
" if(mode>=9){\n"
"  float3 factor=float3(a&127u,(a>>7)&127u,b&127u)/127.0,bounce=saturate(factor+tvIndirect.Load(at).rgb)-factor;\n"
"  if(!lit)bounce=0;\n"
"  if(mode==9||mode==11)return float4(bounce*4,1);\n"
"  float3 v=bounce*8;return float4(v/(1+v),1);\n"
" }\n"
" return float4(float(a&127u)/127.0,float((a>>7)&127u)/127.0,float((b>>7)&255u)/255.0,1);\n"
"}\n";
const ShaderProgram kRtTermViewPixel{{kRtTermViewShader, sizeof(kRtTermViewShader) - 1}, nullptr, nullptr, "rtTermViewPS",
                                     "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3};
void append_raytracing_shader_programs(std::vector<ShaderProgram>& out){
 // The mask pass with the sun term traced (native_shader.h NATIVE_RT_SUN).
 static const D3D_SHADER_MACRO rt_sun[]={{"NATIVE_RT_SUN","1"},{nullptr,nullptr}};
 out.push_back(kRaytracingDebugVertex);
 out.push_back(kRaytracingDebugPixel);
 out.push_back(kRaytracingRepack);
 out.push_back({owned_native_shader_source(),nullptr,rt_sun,"nativeShadowMaskPS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 out.push_back(kRaytracingProbe);
 const std::string& hit_debug=raytracing_hit_debug_source();
 out.push_back({{hit_debug.data(),hit_debug.size()},nullptr,nullptr,"rtHitDebugPS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 out.push_back(kRtTermViewPixel); // rt_term_view (after kRtPrograms' entries: drawn by its own constant)
 // kRtPrepass: the depth prepass of rt_sun_shadows frames, with the crown
 // impostor receiver and the unit flag the traced mask reads (native_shader.h
 // NATIVE_RT_SUN). Every other frame draws the plain prepass, so the RT code
 // costs the raster path no registers.
 out.push_back({owned_native_shader_source(),nullptr,rt_sun,"nativePrepassPS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
}

void Renderer::release_rt_direct_d3d12() noexcept{
 auto& direct=rt_direct12_;
 direct.reservoirs.reset();direct.lights.reset();direct.remap.reset();direct.offsets.reset();
 direct.surfaces[0].reset();direct.surfaces[1].reset();direct.light.reset();
 direct.width=direct.height=direct.failed_width=direct.failed_height=0;
 direct.light_capacity=direct.remap_capacity=direct.previous_light_count=0;
 direct.light_keys.clear();direct.lights_uploaded.clear();
 direct.history_frame=direct.traced_frame=UINT64_MAX;direct.ready=false;
}

// rt_direct_lights (renderer.h RtDirectD3D12), once a world frame after the
// world flush and the ambient occlusion: the sampling pass (surface, initial
// candidates from the pixel's light grid cell, one visibility ray, temporal
// resampling, boiling filter), the shading pass (spatial resampling, final
// visibility, the lamp light as the lamp loop would have added it: the
// lighting factor and fog share the guide twins left in the albedo targets'
// alpha) and the additive pass over the world colour. A frame whose twins
// drew without lamps (traced_frame) and whose passes then fail shows no lamp
// light on those surfaces, named once.
void Renderer::direct_lights_d3d12() noexcept try{
 auto& direct=rt_direct12_;auto& rt=rt12_;auto& up=upscale12_;
 if(!rt_direct_enabled_||hit_test_active_||!world_d3d12_)return;
 const bool traced=direct.traced_frame==native_frame_index_;
 // Never silent: a pass that has not run for 120 world frames is named once
 // with its reason (a pipeline compiling for a few frames is not news).
 const auto missing=[&](const char* why){
  direct.ready=false;direct.history_frame=UINT64_MAX;
  if(traced)d3d12_->omit(why,false);
  if(++direct.idle_frames>=120&&direct.idle_logged!=why){direct.idle_logged=why;bridge_append_probe_event("rt_direct_lights_omitted",why);}
 };
 if(native_point_lights_.empty()){direct.history_frame=UINT64_MAX;return;}
 if(!guide_frame_d3d12()||rt.frame!=native_frame_index_||!rt.scene||!rt.records)return missing("rt_direct_lights (no guide images or ray tracing scene this frame)");
 const auto scene=bridge_scene_world_to_clip_snapshot();
 RtDirectConstants constants{};
 if(!scene.valid||!ambient_occlusion_invert(scene.world_to_clip.data(),constants.clip_to_world))return missing("rt_direct_lights (no world camera this frame)");
 if(direct.programs.empty())append_rt_direct_shader_programs(direct.programs);
 if(direct.programs.size()<kRtDirectPrograms||rt.programs.size()<kRtPrograms)return missing("rt_direct_lights (programs missing)");
 // The images at the world's size.
 const UINT width=width_,height=height_;
 if(width==direct.failed_width&&height==direct.failed_height)return missing("rt_direct_lights (its images could not be created)");
 const UINT block_row=((width+kRtReservoirBlock-1)/kRtReservoirBlock)*kRtReservoirBlock*kRtReservoirBlock;
 const UINT array_pitch=block_row*((height+kRtReservoirBlock-1)/kRtReservoirBlock);
 if(direct.width!=width||direct.height!=height||!direct.reservoirs||!direct.light){
  direct.reservoirs.reset();direct.surfaces[0].reset();direct.surfaces[1].reset();direct.light.reset();
  direct.history_frame=UINT64_MAX;direct.width=width;direct.height=height;
  const UINT64 reservoir_bytes=UINT64(array_pitch)*2u*kRtReservoirBytes;
  if(!d3d12_->storage_supported(DXGI_FORMAT_R32G32_UINT)||!d3d12_->storage_supported(DXGI_FORMAT_R16G16B16A16_FLOAT)||
     FAILED(d3d12_->create_buffer(reservoir_bytes,kRtReservoirBytes,kD3D12BufferUnordered,direct.reservoirs))||
     FAILED(d3d12_->create_storage_texture(width,height,0,DXGI_FORMAT_R32G32_UINT,direct.surfaces[0]))||
     FAILED(d3d12_->create_storage_texture(width,height,0,DXGI_FORMAT_R32G32_UINT,direct.surfaces[1]))||
     FAILED(d3d12_->create_storage_texture(width,height,0,DXGI_FORMAT_R16G16B16A16_FLOAT,direct.light))){
   direct.reservoirs.reset();direct.surfaces[0].reset();direct.surfaces[1].reset();direct.light.reset();
   direct.failed_width=width;direct.failed_height=height;
   bridge_append_probe_event("rt_direct_lights_omitted","its reservoir, surface or light images could not be created at this size; the lamp loop stays");
   return missing("rt_direct_lights (its images could not be created)");
  }
  // A new reservoir buffer holds zeros: empty reservoirs.
  const double pixels=double(width)*double(height);
  char text[240];
  std::snprintf(text,sizeof(text),"%ux%u render pixels: reservoirs %.1f MB, surfaces %.1f MB, lamp light %.1f MB, wider guide images %.1f MB, total %.1f MB",
   width,height,double(reservoir_bytes)/1048576.0,pixels*16.0/1048576.0,pixels*8.0/1048576.0,pixels*8.0/1048576.0,
   (double(reservoir_bytes)+pixels*32.0)/1048576.0);
  bridge_append_probe_event("rt_direct_lights_memory",text); // once a size (a session's upscaler mode)
 }
 if(!direct.offsets){
  const std::vector<float> offsets=rt_neighbour_offsets();
  if(FAILED(d3d12_->create_buffer(UINT64(offsets.size())*sizeof(float),8,0,direct.offsets))||
     FAILED(d3d12_->write_buffer(*direct.offsets,0,offsets.data(),UINT64(offsets.size())*sizeof(float)))){
   direct.offsets.reset();return missing("rt_direct_lights (neighbour offsets could not be created)");
  }
 }
 // The lights: the published list in its order, each with its shape.
 const auto& config=debug_overlay_.config();
 const float lamp_radius=(std::clamp)(configured_floating_point(config,"rt_direct_light_radius",.15f),0.f,1.f);
 const UINT light_count=UINT(native_point_lights_.size());
 std::vector<RtDirectLight> lights(light_count);std::vector<std::uint64_t> keys(light_count);
 {const std::size_t game=(std::min)(native_point_light_inputs_.size(),native_point_lights_.size());
  const float cell_half=emissive_level_>=2?2.f:4.f; // a lava cluster's cell: 2 or 4 tiles of 2 m
  std::unordered_map<std::uint64_t,std::uint32_t> seen;
  for(UINT n=0;n<light_count;++n){
   const auto& source=native_point_lights_[n];auto& light=lights[n];
   for(int axis=0;axis<3;++axis){light.centre[axis]=source.center_inverse_radius_squared[axis];light.color[axis]=source.color[axis];}
   light.inverse_radius_squared=source.center_inverse_radius_squared[3];
   const bool glow=source.color[3]<0;
   light.kind=glow?1.f:0.f;light.intensity=source.color[3]>0?.65f:1.f;
   // rt_emissive: a particle cluster's lamp is a sphere of the cluster's size.
   const std::size_t particle=n>=game+emissive_published_?n-game-emissive_published_:SIZE_MAX;
   const bool cluster_lamp=particle<particle_lights_published_.size();
   if(glow){light.extent[0]=light.extent[1]=cell_half;}
   else light.extent[0]=cluster_lamp?(std::clamp)(particle_lights_published_[particle].spread,lamp_radius,1.5f):lamp_radius;
   // A shadow-casting lamp's rays aim at its shadow centre (where its cube was baked from).
   const bool cast=!glow&&n<game&&native_point_light_inputs_[n].casts_shadow;
   for(int axis=0;axis<3;++axis)light.target[axis]=cast?source.shadow_center[axis]:light.centre[axis];
   // Its identity across frames: the game's source id and its place among that id's lights; a lava light its cell.
   std::uint64_t key=0;
   if(n<game){const std::uint64_t id=std::uint64_t(std::uint32_t(native_point_light_inputs_[n].source_id));key=(id<<24)|(seen[id]++&0xFFFFFFu);}
   else if(cluster_lamp)key=(1ull<<62)|particle_lights_published_[particle].id;
   else{const std::size_t cluster=n-game;key=(1ull<<63)|(cluster<emissive_clusters_.size()?emissive_clusters_[cluster].cell:std::uint64_t(cluster));}
   keys[n]=key;
  }}
 HRESULT hr=S_OK;
 const UINT64 light_bytes=UINT64(light_count)*sizeof(RtDirectLight);
 if(!direct.lights||direct.light_capacity<light_count){
  direct.lights.reset();direct.lights_uploaded.clear();direct.light_capacity=light_count+light_count/2+8;
  if(FAILED(d3d12_->create_buffer(UINT64(direct.light_capacity)*sizeof(RtDirectLight),sizeof(RtDirectLight),0,direct.lights))){
   direct.lights.reset();direct.light_capacity=0;return missing("rt_direct_lights (light buffer could not be created)");
  }
 }
 if(direct.lights_uploaded.size()!=light_bytes||std::memcmp(direct.lights_uploaded.data(),lights.data(),light_bytes)!=0){
  if(FAILED(d3d12_->write_buffer(*direct.lights,0,lights.data(),light_bytes)))return missing("rt_direct_lights (light upload failed)");
  direct.lights_uploaded.assign(reinterpret_cast<const std::uint8_t*>(lights.data()),reinterpret_cast<const std::uint8_t*>(lights.data())+light_bytes);
 }
 const bool history=direct.history_frame!=UINT64_MAX&&direct.history_frame+1==native_frame_index_;
 bool remapped=false;
 const UINT previous_count=UINT(direct.light_keys.size());
 if(history&&keys!=direct.light_keys){
  // Last frame's index of each light to this frame's, then this frame's back (-1: gone or new).
  std::vector<std::int32_t> remap(std::size_t(previous_count)+light_count,-1);
  std::unordered_map<std::uint64_t,std::uint32_t> now;
  for(UINT n=0;n<light_count;++n)now.emplace(keys[n],n);
  for(UINT n=0;n<previous_count;++n)if(const auto found=now.find(direct.light_keys[n]);found!=now.end()){
   remap[n]=std::int32_t(found->second);remap[previous_count+found->second]=std::int32_t(n);}
  if(!direct.remap||direct.remap_capacity<remap.size()){
   direct.remap.reset();direct.remap_capacity=UINT(remap.size()+remap.size()/2+16);
   if(FAILED(d3d12_->create_buffer(UINT64(direct.remap_capacity)*4u,4,0,direct.remap))){direct.remap.reset();direct.remap_capacity=0;}
  }
  remapped=direct.remap&&SUCCEEDED(d3d12_->write_buffer(*direct.remap,0,remap.data(),UINT64(remap.size())*4u));
 }
 const bool reuse=history&&(keys==direct.light_keys||remapped);
 // The frame's values.
 std::memcpy(constants.world_to_clip,scene.world_to_clip.data(),sizeof(constants.world_to_clip));
 std::memcpy(constants.previous_clip_to_world,reuse?direct.previous_clip_to_world.data():constants.clip_to_world,sizeof(constants.previous_clip_to_world));
 std::memcpy(constants.previous_world_to_clip,reuse?direct.previous_world_to_clip.data():scene.world_to_clip.data(),sizeof(constants.previous_world_to_clip));
 for(int axis=0;axis<3;++axis){constants.eye[axis]=fog_frame_d3d12_.eye[axis];constants.previous_eye[axis]=reuse?direct.previous_eye[axis]:fog_frame_d3d12_.eye[axis];}
 constants.eye[3]=(std::clamp)(configured_floating_point(config,"rt_direct_light_clearance",.35f),0.f,2.f);
 constants.size_jitter[0]=float(width);constants.size_jitter[1]=float(height);constants.size_jitter[2]=dlss_jitter_[0];constants.size_jitter[3]=dlss_jitter_[1];
 constants.previous_jitter[0]=reuse?direct.previous_jitter[0]:dlss_jitter_[0];constants.previous_jitter[1]=reuse?direct.previous_jitter[1]:dlss_jitter_[1];
 constants.previous_jitter[2]=.02f;constants.previous_jitter[3]=.0015f; // the sun rays' self-hit distance and its growth per metre of view distance
 const bool object_motion=temporal_upscaler()&&up.object_motion&&up.object_depth;
 constants.frame[0]=std::uint32_t(native_frame_index_);constants.frame[1]=light_count;
 // rt_indirect_light added its share of the clamp this frame: the lamps' share starts over its factor.
 const bool over_indirect=rt_indirect12_.factor_frame==native_frame_index_&&rt_indirect12_.factor;
 constants.frame[2]=(reuse?1u:0u)|(object_motion?2u:0u)|(remapped?4u:0u)|(over_indirect?8u:0u);constants.frame[3]=previous_count;
 std::uint32_t random=std::uint32_t(native_frame_index_)*2654435761u;random^=random>>15;random*=2246822519u;random^=random>>13;
 constants.reservoir[0]=block_row;constants.reservoir[1]=array_pitch;constants.reservoir[2]=kRtNeighbourOffsets-1u;constants.reservoir[3]=random;
 constants.counts[0]=8;constants.counts[1]=20;constants.counts[2]=1;constants.counts[3]=8; // the library's defaults (one spatial sample: the low-discrepancy table)
 constants.spatial[0]=32.f;constants.spatial[1]=.1f;constants.spatial[2]=.5f;constants.spatial[3]=.2f;
 constants.visibility[0]=4.f;constants.visibility[1]=16.f;
 D3D12Texture* const now_surfaces=direct.surfaces[direct.parity].get();
 D3D12Texture* const before_surfaces=direct.surfaces[1u-direct.parity].get();
 D3D12Texture* world=world_d3d12_.get();
 const auto bind_common=[&]{
  d3d12_->clear_slots();
  d3d12_->set_targets(&world,1,nullptr); // no depth bound: the world depth is read
  d3d12_->set_constants(1,&constants,sizeof(constants));
  d3d12_->set_constants(5,native12_.point_light_constants.data(),sizeof(native12_.point_light_constants));
  d3d12_->set_depth_resource(0,world_d3d12_.get());
  d3d12_->set_resource(1,up.guide_normal.get());d3d12_->set_resource(2,up.guide_diffuse.get());d3d12_->set_resource(3,up.guide_specular.get());
  d3d12_->set_resource(17,native12_.point_light_grid.get());
  d3d12_->set_resource(9,over_indirect?rt_indirect12_.factor.get():nullptr);
  d3d12_->set_resource(40,direct.lights.get());d3d12_->set_resource(41,remapped?direct.remap.get():nullptr);
  d3d12_->set_resource(94,rt.scene.get());d3d12_->set_resource(95,rt.vertices.get());
  d3d12_->set_resource(99,rt.indices.get());d3d12_->set_resource(113,rt.records.get());
  D3D11_SAMPLER_DESC card{};card.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  card.AddressU=card.AddressV=card.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;card.MaxLOD=D3D11_FLOAT32_MAX;
  d3d12_->set_sampler(0,card);
  d3d12_->set_unordered(0,direct.reservoirs.get());
 };
 const UINT groups_x=(width+7u)/8u,groups_y=(height+7u)/8u;
 transition_profile_pass("rt_direct_sample");
 bind_common();
 d3d12_->set_resource(4,reuse?before_surfaces:nullptr);
 d3d12_->set_resource(5,object_motion?up.object_motion.get():nullptr);d3d12_->set_resource(6,object_motion?up.object_depth.get():nullptr);
 d3d12_->set_unordered(1,now_surfaces);
 hr=d3d12_->dispatch(direct.programs[kRtDirectSample],groups_x,groups_y,1);
 if(hr==S_OK){
  transition_profile_pass("rt_direct_shade");
  bind_common();
  d3d12_->set_resource(7,direct.offsets.get());d3d12_->set_resource(8,now_surfaces);
  d3d12_->set_unordered(1,direct.light.get());
  hr=d3d12_->dispatch(direct.programs[kRtDirectShade],groups_x,groups_y,1);
 }
 d3d12_->clear_slots();
 if(hr==S_OK&&traced){
  // The lamp light over the world colour (waited for: one small program).
  transition_profile_pass("rt_direct_add");
  d3d12_->set_targets(&world,1,nullptr);
  d3d12_->set_resource(0,direct.light.get());
  d3d12_->set_wait_for_pipelines(true);
  const HRESULT added=d3d12_->draw(rt.programs[kRtDebugVertex],direct.programs[kRtDirectAdd],post_state(true,false),nullptr,3);
  d3d12_->set_wait_for_pipelines(false);
  d3d12_->clear_slots();
  if(added==S_OK){profiler_.add_draw(1,1);profiler_.add_batch();d3d12_->count("world frames with ray traced lamp light");}
  else hr=FAILED(added)?added:S_FALSE;
 }
 bind_world_targets_d3d12();
 if(hr!=S_OK){
  // A pipeline still compiling (S_FALSE) or a failure: the next frames' twins keep their lamps until the passes ran.
  missing(hr==S_FALSE?"rt_direct_lights (pipeline not ready: no lamp light on the traced surfaces this frame)":"rt_direct_lights (a pass failed: no lamp light on the traced surfaces this frame)");
  return;
 }
 direct.ready=true;direct.idle_frames=0;direct.idle_logged=nullptr;
 direct.history_frame=native_frame_index_;direct.parity=1u-direct.parity;
 direct.light_keys=std::move(keys);direct.previous_light_count=light_count;
 std::memcpy(direct.previous_world_to_clip.data(),scene.world_to_clip.data(),sizeof(float)*16);
 std::memcpy(direct.previous_clip_to_world.data(),constants.clip_to_world,sizeof(float)*16);
 for(int axis=0;axis<3;++axis)direct.previous_eye[axis]=fog_frame_d3d12_.eye[axis];
 direct.previous_jitter[0]=dlss_jitter_[0];direct.previous_jitter[1]=dlss_jitter_[1];
}catch(const std::bad_alloc&){
 rt_direct12_.ready=false;rt_direct12_.history_frame=UINT64_MAX;
 bridge_append_probe_event("rt_direct_lights_omitted","out of memory preparing the lights");
}

void Renderer::release_rt_indirect_d3d12() noexcept{
 auto& indirect=rt_indirect12_;
 indirect.reservoirs.reset();indirect.offsets.reset();
 indirect.lava_tiles.reset();indirect.lava_levels.reset();indirect.lava_revision=UINT64_MAX;indirect.lava_level_count=0;
 indirect.surfaces[0].reset();indirect.surfaces[1].reset();indirect.factor.reset();
 release_rt_indirect_denoise_d3d12();
 indirect.width=indirect.height=indirect.failed_width=indirect.failed_height=0;
 indirect.history_frame=indirect.traced_frame=indirect.factor_frame=UINT64_MAX;indirect.ready=false;
 release_rt_ambient_occlusion_d3d12();
}
void Renderer::release_rt_indirect_denoise_d3d12() noexcept{
 auto& indirect=rt_indirect12_;
 for(int k=0;k<2;++k){indirect.denoise_packed[k].reset();indirect.denoise_history[k].reset();indirect.denoise_moments[k].reset();}
 indirect.denoise_work.reset();indirect.denoise_frame=UINT64_MAX;indirect.denoise_failed=false;
}
void Renderer::release_rt_ambient_occlusion_d3d12() noexcept{
 auto& indirect=rt_indirect12_;
 indirect.ao_history[0].reset();indirect.ao_history[1].reset();indirect.ao_now.reset();indirect.ao_surfaces[0].reset();indirect.ao_surfaces[1].reset();
 indirect.ao_frame=indirect.ao_history_frame=UINT64_MAX;indirect.ao_failed=false;
}

// rt_indirect_light (renderer.h RtIndirectD3D12), once a world frame after
// the ambient occlusion and before the direct light passes (which clamp over
// its factor image): the sampling pass (a bounce ray a half pixel, its hit
// lit, temporal resampling, boiling filter), the spatial pass (spatial
// resampling, the final visibility ray), the shading pass (the factor of
// every render pixel) and the additive pass over the world colour. A frame
// whose twins drew without the bounce volume (traced_frame) and whose passes
// then fail shows no bounce light on those surfaces, named once.
void Renderer::indirect_light_d3d12() noexcept try{
 auto& indirect=rt_indirect12_;auto& rt=rt12_;auto& up=upscale12_;
 if(!rt_indirect_enabled_||hit_test_active_||!world_d3d12_)return;
 const bool traced=indirect.traced_frame==native_frame_index_;
 // Never silent (as direct_lights_d3d12).
 const auto missing=[&](const char* why){
  indirect.ready=false;indirect.history_frame=UINT64_MAX;
  if(traced)d3d12_->omit(why,false);
  if(++indirect.idle_frames>=120&&indirect.idle_logged!=why){indirect.idle_logged=why;bridge_append_probe_event("rt_indirect_light_omitted",why);}
 };
 // A scene without static casters has no vertex and index arenas (a hit never reads them then).
 if(!guide_frame_d3d12()||rt.frame!=native_frame_index_||!rt.scene||!rt.records||!rt.shade)
  return missing("rt_indirect_light (no guide images or ray tracing scene this frame)");
 const auto scene=bridge_scene_world_to_clip_snapshot();
 RtIndirectConstants constants{};
 if(!scene.valid||!ambient_occlusion_invert(scene.world_to_clip.data(),constants.clip_to_world))return missing("rt_indirect_light (no world camera this frame)");
 if(indirect.programs.empty())append_rt_indirect_shader_programs(indirect.programs);
 if(indirect.programs.size()<kRtIndirectPrograms||rt.programs.size()<kRtPrograms)return missing("rt_indirect_light (programs missing)");
 // The reservoirs and surfaces at half or a quarter of the world's size (rt_indirect_resolution), the factor image at its size.
 const bool lit=rt_indirect_lit_;
 // Occlusion alone needs no full-size bounce grid; its consumer runs at half at most.
 const UINT divisor=lit?rt_indirect_divisor():(std::max)(2u,rt_indirect_divisor());
 const UINT width=width_,height=height_,half_width=(width+divisor-1u)/divisor,half_height=(height+divisor-1u)/divisor;
 if(width==indirect.failed_width&&height==indirect.failed_height)return missing("rt_indirect_light (its images could not be created)");
 const UINT block_row=((half_width+kRtReservoirBlock-1)/kRtReservoirBlock)*kRtReservoirBlock*kRtReservoirBlock;
 const UINT array_pitch=block_row*((half_height+kRtReservoirBlock-1)/kRtReservoirBlock);
 if(indirect.width!=width||indirect.height!=height||indirect.divisor!=divisor||!indirect.reservoirs||!indirect.factor){
  indirect.reservoirs.reset();indirect.surfaces[0].reset();indirect.surfaces[1].reset();indirect.factor.reset();
  release_rt_ambient_occlusion_d3d12();release_rt_indirect_denoise_d3d12();
  indirect.history_frame=UINT64_MAX;indirect.width=width;indirect.height=height;indirect.divisor=divisor;
  const UINT64 reservoir_bytes=UINT64(array_pitch)*2u*kRtIndirectReservoirBytes;
  if(!d3d12_->storage_supported(DXGI_FORMAT_R32G32_UINT)||!d3d12_->storage_supported(DXGI_FORMAT_R16G16B16A16_FLOAT)||
     FAILED(d3d12_->create_buffer(reservoir_bytes,kRtIndirectReservoirBytes,kD3D12BufferUnordered,indirect.reservoirs))||
     FAILED(d3d12_->create_storage_texture(half_width,half_height,0,DXGI_FORMAT_R32G32_UINT,indirect.surfaces[0]))||
     FAILED(d3d12_->create_storage_texture(half_width,half_height,0,DXGI_FORMAT_R32G32_UINT,indirect.surfaces[1]))||
     FAILED(d3d12_->create_storage_texture(width,height,0,DXGI_FORMAT_R16G16B16A16_FLOAT,indirect.factor))){
   indirect.reservoirs.reset();indirect.surfaces[0].reset();indirect.surfaces[1].reset();indirect.factor.reset();
   indirect.failed_width=width;indirect.failed_height=height;
   bridge_append_probe_event("rt_indirect_light_omitted","its reservoir, surface or factor images could not be created at this size; the bounce volume stays");
   return missing("rt_indirect_light (its images could not be created)");
  }
  // A new reservoir buffer holds zeros: empty reservoirs.
  const double pixels=double(width)*double(height),half_pixels=double(half_width)*double(half_height);
  char text[240];
  std::snprintf(text,sizeof(text),"%ux%u render pixels, traced at %ux%u: reservoirs %.1f MB, surfaces %.1f MB, indirect factor %.1f MB, total %.1f MB",
   width,height,half_width,half_height,double(reservoir_bytes)/1048576.0,half_pixels*16.0/1048576.0,pixels*8.0/1048576.0,
   (double(reservoir_bytes)+half_pixels*16.0+pixels*8.0)/1048576.0);
  bridge_append_probe_event("rt_indirect_light_memory",text); // once a size (a session's upscaler mode)
 }
 if(!indirect.offsets){
  const std::vector<float> offsets=rt_neighbour_offsets();
  if(FAILED(d3d12_->create_buffer(UINT64(offsets.size())*sizeof(float),8,0,indirect.offsets))||
     FAILED(d3d12_->write_buffer(*indirect.offsets,0,offsets.data(),UINT64(offsets.size())*sizeof(float)))){
   indirect.offsets.reset();return missing("rt_indirect_light (neighbour offsets could not be created)");
  }
 }
 const auto& config=debug_overlay_.config();
 const bool reuse=indirect.history_frame!=UINT64_MAX&&indirect.history_frame+1==native_frame_index_;
 // rt_ambient_occlusion: the half pixels' visibility history (this and last
 // frame's) and the image the ambient occlusion passes read, made on demand;
 // without them the frame keeps the screen-space passes (named once).
 bool occlusion=rt_ao_enabled_&&!indirect.ao_failed;
 // At full-size bounce the selected ray of each 2x2 quad supplies the half-size AO grid.
 // The separate surfaces retain that grid's reprojection contract, without another ray dispatch.
 const bool separate_occlusion=divisor==1u;
 const UINT ao_width=separate_occlusion?(width+1u)/2u:half_width,ao_height=separate_occlusion?(height+1u)/2u:half_height;
 if(occlusion&&!indirect.ao_now){
  if(!d3d12_->storage_supported(DXGI_FORMAT_R16G16_FLOAT)||
     FAILED(d3d12_->create_storage_texture(ao_width,ao_height,0,DXGI_FORMAT_R16G16_FLOAT,indirect.ao_history[0]))||
     FAILED(d3d12_->create_storage_texture(ao_width,ao_height,0,DXGI_FORMAT_R16G16_FLOAT,indirect.ao_history[1]))||
     FAILED(d3d12_->create_storage_texture(ao_width,ao_height,0,DXGI_FORMAT_R16G16_FLOAT,indirect.ao_now))||
     (separate_occlusion&&(FAILED(d3d12_->create_storage_texture(ao_width,ao_height,0,DXGI_FORMAT_R32G32_UINT,indirect.ao_surfaces[0]))||
                           FAILED(d3d12_->create_storage_texture(ao_width,ao_height,0,DXGI_FORMAT_R32G32_UINT,indirect.ao_surfaces[1]))))){
   release_rt_ambient_occlusion_d3d12();indirect.ao_failed=true;occlusion=false;
   bridge_append_probe_event("rt_ambient_occlusion_omitted","its half resolution images could not be created at this size; the screen-space ambient occlusion stays");
  }else{
   char text[160];
   std::snprintf(text,sizeof(text),"traced at %ux%u: visibility history and output %.1f MB",ao_width,ao_height,double(ao_width)*double(ao_height)*(separate_occlusion?28.0:12.0)/1048576.0);
   bridge_append_probe_event("rt_ambient_occlusion_memory",text);
  }
 }
 // rt_ambient_occlusion alone and no image for it: nothing to trace for.
 if(!lit&&!occlusion)return missing("rt_ambient_occlusion (its images could not be created)");
 const bool occlusion_history=occlusion&&reuse&&indirect.ao_history_frame==indirect.history_frame;
 // The frame's values.
 std::memcpy(constants.world_to_clip,scene.world_to_clip.data(),sizeof(constants.world_to_clip));
 std::memcpy(constants.previous_clip_to_world,reuse?indirect.previous_clip_to_world.data():constants.clip_to_world,sizeof(constants.previous_clip_to_world));
 std::memcpy(constants.previous_world_to_clip,reuse?indirect.previous_world_to_clip.data():scene.world_to_clip.data(),sizeof(constants.previous_world_to_clip));
 for(int axis=0;axis<3;++axis){constants.eye[axis]=fog_frame_d3d12_.eye[axis];constants.previous_eye[axis]=reuse?indirect.previous_eye[axis]:fog_frame_d3d12_.eye[axis];}
 constants.eye[3]=(std::clamp)(configured_floating_point(config,"rt_direct_light_clearance",.35f),0.f,2.f);
 constants.size_jitter[0]=float(width);constants.size_jitter[1]=float(height);constants.size_jitter[2]=dlss_jitter_[0];constants.size_jitter[3]=dlss_jitter_[1];
 constants.previous_jitter[0]=reuse?indirect.previous_jitter[0]:dlss_jitter_[0];constants.previous_jitter[1]=reuse?indirect.previous_jitter[1]:dlss_jitter_[1];
 constants.previous_jitter[2]=.02f;constants.previous_jitter[3]=.0015f; // the sun rays' self-hit distance and its growth per metre of view distance
 // Toward the sun (the bridge's lighting vector), unit length; none in a cave.
 {const auto& sun=native_world_lighting_.sunlight_direction;
  const float length=std::sqrt(sun[0]*sun[0]+sun[1]*sun[1]+sun[2]*sun[2]);
  if(length>1e-6f&&!native_world_cave_){
   for(int axis=0;axis<3;++axis){constants.sun[axis]=sun[axis]/length;constants.sun_diffuse[axis]=native_world_lighting_.sunlight_diffuse[axis];}
   constants.sun[3]=1.f;
  }}
 const bool object_motion=temporal_upscaler()&&up.object_motion&&up.object_depth;
 const bool exits=lit&&configured_boolean(config,"rt_indirect_cave_exits",false)&&cave_volume_active_&&!cave_volume_outdoor_&&cave_exit_count_!=0&&cave12_.exits;
 std::uint32_t random=std::uint32_t(native_frame_index_)*2654435761u;random^=random>>15;random*=2246822519u;random^=random>>13;
 constants.frame[0]=std::uint32_t(native_frame_index_);
 constants.frame[1]=(reuse?1u:0u)|(object_motion?2u:0u)|(exits?4u:0u)|(configured_boolean(config,"rt_indirect_visibility",true)?8u:0u)|
  (occlusion_history?16u:0u)|(lit?0u:32u);
 // The occlusion ray's reach: rt_ao_radius outdoors (how far a roof, a wall
 // or a crown still hides the sky); a cave has no sky and keeps the contact
 // reach of the screen-space passes (ao_radius). A history of 24 frames.
 constants.ao[0]=native_world_cave_?(std::clamp)(configured_floating_point(config,"ao_radius",1.5f),.25f,4.f):
  (std::clamp)(configured_floating_point(config,"rt_ao_radius",5.f),.5f,30.f);
 constants.ao[1]=24.f;
 constants.ao[2]=(std::min)(constants.ao[0],(std::clamp)(configured_floating_point(config,"ao_radius",1.5f),.25f,4.f));
 constants.reuse[1]=float(divisor);
 constants.frame[2]=exits?cave_exit_count_:0u;constants.frame[3]=random;
 constants.reservoir[0]=block_row;constants.reservoir[1]=array_pitch;constants.reservoir[2]=kRtNeighbourOffsets-1u;
 constants.reservoir[3]=half_width|(half_height<<16);
 // The render pixel of its quad a traced pixel stands for: all four over four frames, or all sixteen over sixteen
 // (each run of four covers the four 2x2 corners of the 4x4 quad).
 static constexpr std::uint32_t kQuad[4][2]{{0,0},{1,1},{1,0},{0,1}};
 static constexpr std::uint32_t kQuad16[16][2]{{0,0},{2,2},{2,0},{0,2},{1,1},{3,3},{3,1},{1,3},{1,0},{3,2},{3,0},{1,2},{0,1},{2,3},{2,1},{0,3}};
 static constexpr std::uint32_t kFull[2]{0,0};
 const auto& quad=divisor==1u?kFull:divisor==4u?kQuad16[native_frame_index_&15u]:kQuad[native_frame_index_&3u];
 constants.offsets[0]=quad[0];constants.offsets[1]=quad[1];
 constants.offsets[2]=reuse?indirect.previous_offset[0]:quad[0];constants.offsets[3]=reuse?indirect.previous_offset[1]:quad[1];
 const auto& occlusion_quad=kQuad[native_frame_index_&3u];
 constants.ao_offsets[0]=occlusion_quad[0];constants.ao_offsets[1]=occlusion_quad[1];
 constants.ao_offsets[2]=reuse?indirect.ao_previous_offset[0]:occlusion_quad[0];
 constants.ao_offsets[3]=reuse?indirect.ao_previous_offset[1]:occlusion_quad[1];
 // The library's defaults for ReSTIR GI (history 8 frames, a sample's age at most 30, 2 spatial samples;
 // the radius in half pixels), four lamp candidates at a hit.
 // rt_indirect_spatial_samples; the radius covers the same share of the screen at either size.
 constants.counts[0]=8;constants.counts[1]=30;constants.counts[2]=std::uint32_t((std::clamp)(configured_integer(config,"rt_indirect_spatial_samples",2),0,4));constants.counts[3]=4;
 constants.spatial[0]=divisor==1u?48.f:divisor==4u?12.f:24.f;constants.spatial[1]=.1f;constants.spatial[2]=.6f;constants.spatial[3]=.2f;
 // Under DLSS Ray Reconstruction a kept sample that stays in its half pixel for many frames reads as detail,
 // not noise: it comes out as a bright dot (snow in shade, cave floors). NVIDIA's Ray Reconstruction preset
 // for ReSTIR GI (RTX Remix) answers with less coherent samples: a new sample weighs about four of the
 // history's (M / 0.26), and the boiling filter also runs on the final reservoirs at 15 times the group's
 // mean (strength 10 / (15 + 9)). Without Ray Reconstruction: the library's defaults.
 const bool reconstruction=ray_reconstruction_on();
 constants.reuse[0]=reconstruction?4.f:1.f;constants.ao[3]=reconstruction?.42f:0.f;
 // rt_indirect_outlier_clamp: under Ray Reconstruction a render pixel's bounce light is held to that
 // multiple of its 8x8 group's mean in the shading pass (0: no clamp).
 constants.reuse[2]=reconstruction?(std::clamp)(configured_floating_point(config,"rt_indirect_outlier_clamp",5.f),0.f,20.f):0.f;
 // rt_indirect_rr_randomization: under Ray Reconstruction the temporal reuse looks up its history sample at a
 // random place within this radius (traced pixels per 960 of the traced width; RTX Remix uses 80) of the reprojected one. Default 0: on the save1403 sand any radius added grain and brightened the bounce light.
 constants.reuse[3]=reconstruction?(std::clamp)(configured_floating_point(config,"rt_indirect_rr_randomization",0.f),0.f,200.f)*float(half_width)/960.f:0.f;
 // rt_emissive: the active map's lava tiles for the bounce rays, made again when the lava lights' clusters
 // are (a new map, a changed level) or the strength option changes. Only while those lights are
 // published: with emissive_lights off the lava lights nothing.
 {const float strength=(std::clamp)(configured_floating_point(config,"emissive_light_strength",1.f),0.f,3.f);
  const bool lava=rt_emissive_enabled_&&configured_integer(config,"emissive_lights",1)>0&&!ambient_menu_&&
   emissive_generation_==ambient_map_generation_&&!emissive_tiles_.empty()&&ambient_tiles_x_&&ambient_tiles_y_&&strength>0;
  if(!lava){indirect.lava_level_count=0;indirect.lava_revision=UINT64_MAX;}
  else if(indirect.lava_revision!=emissive_revision_||indirect.lava_strength!=strength||!indirect.lava_tiles||!indirect.lava_levels){
   indirect.lava_revision=emissive_revision_;indirect.lava_strength=strength;indirect.lava_level_count=0;
   // Levels a quarter of a metre apart; a level's height the mean of its tiles'.
   std::vector<std::uint32_t> tiles(std::size_t(ambient_tiles_x_)*ambient_tiles_y_,0u);
   std::vector<long> keys;std::vector<std::array<double,5>> sums; // height, colour, tiles
   std::array<float,3> emission{-1.f,-1.f,-1.f},colour{}; // the last tile's: a map's lava has one emission
   double middle[2]={0,0};
   for(const auto& tile:emissive_tiles_){
    if(tile.x>=ambient_tiles_x_||tile.y>=ambient_tiles_y_||!std::isfinite(tile.z))continue;
    const long key=std::lround(tile.z*4.f);
    std::size_t level=std::size_t(std::find(keys.begin(),keys.end(),key)-keys.begin());
    if(level==keys.size()){keys.push_back(key);sums.push_back({});}
    if(tile.emission!=emission){emission=tile.emission;colour=emissive::rendered_color(emission,liquid_lava_samples_);}
    sums[level][0]+=tile.z;sums[level][1]+=colour[0];sums[level][2]+=colour[1];sums[level][3]+=colour[2];sums[level][4]+=1;
    tiles[std::size_t(tile.y)*ambient_tiles_x_+tile.x]=std::uint32_t(level)+1u;
    middle[0]+=2.0*tile.x+1.0;middle[1]+=2.0*tile.y+1.0;
   }
   std::vector<float> levels;levels.reserve(sums.size()*4u);
   for(const auto& sum:sums)for(int k=0;k<4;++k)levels.push_back(float(sum[k]/sum[4])*(k?strength:1.f));
   indirect.lava_tiles.reset();indirect.lava_levels.reset();
   if(!levels.empty()&&
      SUCCEEDED(d3d12_->create_buffer(UINT64(tiles.size())*4u,4,0,indirect.lava_tiles))&&
      SUCCEEDED(d3d12_->write_buffer(*indirect.lava_tiles,0,tiles.data(),UINT64(tiles.size())*4u))&&
      SUCCEEDED(d3d12_->create_buffer(UINT64(levels.size())*4u,16,0,indirect.lava_levels))&&
      SUCCEEDED(d3d12_->write_buffer(*indirect.lava_levels,0,levels.data(),UINT64(levels.size())*4u))){
    indirect.lava_level_count=UINT(sums.size());indirect.lava_tiles_x=ambient_tiles_x_;indirect.lava_tiles_y=ambient_tiles_y_;
    char text[240];
    std::snprintf(text,sizeof(text),"lava at bounce hits: %zu tiles on %zu levels around %.0f %.0f (first level at %.1f m), colour %.2f %.2f %.2f, counted past %.0f to %.0f m",
     emissive_tiles_.size(),sums.size(),middle[0]/double(emissive_tiles_.size()),middle[1]/double(emissive_tiles_.size()),levels[0],
     levels[1],levels[2],levels[3],emissive::kRadius,emissive::kRadius+emissive::kRadiusCoverage);
    bridge_append_probe_event("rt_emissive_lava",text);
   }else if(!levels.empty()){
    indirect.lava_tiles.reset();indirect.lava_levels.reset();
    bridge_append_probe_event("rt_emissive_lava","the lava tables could not be created: lava sends no light of its own to bounce rays");
   }
  }
  if(indirect.lava_level_count){
   constants.lava[0]=indirect.lava_tiles_x;constants.lava[1]=indirect.lava_tiles_y;constants.lava[2]=indirect.lava_level_count;
   constants.lava_reach[0]=emissive::kRadius;constants.lava_reach[1]=1.f/emissive::kRadiusCoverage;
  }}
 D3D12Texture* const now_surfaces=indirect.surfaces[indirect.parity].get();
 D3D12Texture* const before_surfaces=indirect.surfaces[1u-indirect.parity].get();
 D3D12Texture* world=world_d3d12_.get();
 const auto bind_common=[&]{
  d3d12_->clear_slots();
  d3d12_->set_targets(&world,1,nullptr); // no depth bound: the world depth is read
  d3d12_->set_constants(1,&constants,sizeof(constants));
  // The lamps and the bounce volume as published (the twins' copy has the volume's strength 0).
  d3d12_->set_constants(5,native12_.point_light_constants.data(),sizeof(native12_.point_light_constants));
  d3d12_->set_depth_resource(0,world_d3d12_.get());
  d3d12_->set_resource(1,up.guide_normal.get());d3d12_->set_resource(2,up.guide_diffuse.get());d3d12_->set_resource(3,up.guide_specular.get());
  d3d12_->set_resource(17,native_point_lights_.empty()?nullptr:native12_.point_light_grid.get());
  d3d12_->set_resource(18,native_point_lights_.empty()?nullptr:native12_.point_lights.get());
  d3d12_->set_resource(kTerrain2CacheCoarseSlotD3D12,terrain2_cache_on_?terrain2_cache_coarse12_.get():nullptr);
  d3d12_->set_resource(kTerrain2CacheParamsSlotD3D12,terrain2_cache_on_?terrain2_cache_params12_.get():nullptr);
  bind_cave_irradiance_d3d12(); // t80-t82, the exits at t84, s13
  d3d12_->set_resource(85,indirect.lava_level_count?indirect.lava_tiles.get():nullptr);
  d3d12_->set_resource(86,indirect.lava_level_count?indirect.lava_levels.get():nullptr);
  d3d12_->set_resource(94,rt.scene.get());d3d12_->set_resource(95,rt.vertices.get());
  d3d12_->set_resource(96,rt.terrain_vertices.get());d3d12_->set_resource(97,rt.terrain_indices.get());
  d3d12_->set_resource(99,rt.indices.get());d3d12_->set_resource(113,rt.records.get());
  d3d12_->set_resource(114,rt.shade.get());d3d12_->set_resource(115,rt.figures_in_scene?rt.figure_shade.get():nullptr);
  D3D11_SAMPLER_DESC wrap{};wrap.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  wrap.AddressU=wrap.AddressV=wrap.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;wrap.MaxLOD=D3D11_FLOAT32_MAX;
  d3d12_->set_sampler(0,wrap);wrap.MaxLOD=0;d3d12_->set_sampler(15,wrap);
  d3d12_->set_unordered(0,indirect.reservoirs.get());
 };
 transition_profile_pass("rt_indirect_sample");
 bind_common();
 d3d12_->set_resource(4,reuse?before_surfaces:nullptr);
 d3d12_->set_resource(5,object_motion?up.object_motion.get():nullptr);d3d12_->set_resource(6,object_motion?up.object_depth.get():nullptr);
 d3d12_->set_unordered(1,now_surfaces);
 if(occlusion){
  d3d12_->set_resource(9,occlusion_history?indirect.ao_history[1u-indirect.parity].get():nullptr);
  d3d12_->set_unordered(2,indirect.ao_history[indirect.parity].get());d3d12_->set_unordered(3,indirect.ao_now.get());
  if(separate_occlusion){
   d3d12_->set_resource(10,occlusion_history?indirect.ao_surfaces[1u-indirect.parity].get():nullptr);
   d3d12_->set_unordered(4,indirect.ao_surfaces[indirect.parity].get());
  }
 }
 HRESULT hr=d3d12_->dispatch(indirect.programs[occlusion?kRtIndirectSampleAo:kRtIndirectSample],(half_width+7u)/8u,(half_height+7u)/8u,1);
 // The sampling pass ran: its occlusion image is this frame's whatever the later passes do.
 const bool occlusion_written=occlusion&&hr==S_OK;
 if(occlusion_written)indirect.ao_frame=native_frame_index_;
 if(hr==S_OK&&lit){
  transition_profile_pass("rt_indirect_spatial");
  bind_common();
  d3d12_->set_resource(7,indirect.offsets.get());d3d12_->set_resource(8,now_surfaces);
  hr=d3d12_->dispatch(indirect.programs[kRtIndirectSpatial],(half_width+7u)/8u,(half_height+7u)/8u,1);
 }
 if(hr==S_OK&&lit){
  transition_profile_pass("rt_indirect_shade");
  bind_common();
  d3d12_->set_resource(8,now_surfaces);
  d3d12_->set_unordered(1,indirect.factor.get());
  hr=d3d12_->dispatch(indirect.programs[kRtIndirectShade],(width+7u)/8u,(height+7u)/8u,1);
 }
 // rt_indirect_denoise (without Ray Reconstruction): the temporal pass, then three a-trous passes
 // (steps 1, 2, 4) through the work image back into the factor image. Its images on demand (named once).
 bool denoised=false;
 if(hr==S_OK&&lit&&!reconstruction&&!indirect.denoise_failed&&configured_boolean(config,"rt_indirect_denoise",true)){
  if(!indirect.denoise_work){
   bool made=d3d12_->storage_supported(DXGI_FORMAT_R16G16_FLOAT)&&
    SUCCEEDED(d3d12_->create_storage_texture(width,height,0,DXGI_FORMAT_R16G16B16A16_FLOAT,indirect.denoise_work));
   for(int k=0;k<2&&made;++k)
    made=SUCCEEDED(d3d12_->create_storage_texture(width,height,0,DXGI_FORMAT_R32G32_UINT,indirect.denoise_packed[k]))&&
         SUCCEEDED(d3d12_->create_storage_texture(width,height,0,DXGI_FORMAT_R16G16B16A16_FLOAT,indirect.denoise_history[k]))&&
         SUCCEEDED(d3d12_->create_storage_texture(width,height,0,DXGI_FORMAT_R16G16_FLOAT,indirect.denoise_moments[k]));
   if(!made){
    release_rt_indirect_denoise_d3d12();indirect.denoise_failed=true;
    bridge_append_probe_event("rt_indirect_denoise_omitted","its images could not be created at this size; the bounce light is not denoised");
   }else{
    char text[160];
    std::snprintf(text,sizeof(text),"%ux%u render pixels: history, moments, surfaces and work images %.1f MB",width,height,double(width)*double(height)*48.0/1048576.0);
    bridge_append_probe_event("rt_indirect_denoise_memory",text);
   }
  }
  if(indirect.denoise_work){
   const unsigned now=indirect.parity,before=1u-indirect.parity;
   RtIndirectConstants denoise_constants=constants;
   const bool history=reuse&&indirect.denoise_frame==indirect.history_frame;
   denoise_constants.frame[1]=(constants.frame[1]&~1u)|(history?1u:0u);
   RtIndirectDenoiseConstants step{};
   rt_indirect_view_rays(constants.clip_to_world,scene.world_to_clip.data(),step.ray);
   std::memcpy(step.previous_ray,history?indirect.previous_ray.data():step.ray,sizeof(step.previous_ray));
   step.step[2]=24;
   transition_profile_pass("rt_indirect_denoise");
   bind_common();
   d3d12_->set_constants(1,&denoise_constants,sizeof(denoise_constants));d3d12_->set_constants(2,&step,sizeof(step));
   d3d12_->set_resource(5,object_motion?up.object_motion.get():nullptr);d3d12_->set_resource(6,object_motion?up.object_depth.get():nullptr);
   d3d12_->set_resource(10,indirect.factor.get());d3d12_->set_resource(11,indirect.denoise_history[before].get());
   d3d12_->set_resource(12,indirect.denoise_moments[before].get());d3d12_->set_resource(13,indirect.denoise_packed[before].get());
   d3d12_->set_unordered(1,indirect.denoise_work.get());d3d12_->set_unordered(2,indirect.denoise_history[now].get());
   d3d12_->set_unordered(3,indirect.denoise_moments[now].get());d3d12_->set_unordered(4,indirect.denoise_packed[now].get());
   hr=d3d12_->dispatch(indirect.programs[kRtIndirectTemporal],(width+7u)/8u,(height+7u)/8u,1);
   // work -> factor (step 1), factor -> work (2), work -> factor (4, the last).
   for(std::uint32_t pass=0;pass<3u&&hr==S_OK;++pass){
    step.step[0]=1u<<pass;step.step[1]=pass==2u?1u:0u;
    const bool into_factor=(pass&1u)==0u;
    bind_common();
    d3d12_->set_constants(1,&denoise_constants,sizeof(denoise_constants));d3d12_->set_constants(2,&step,sizeof(step));
    d3d12_->set_resource(10,into_factor?indirect.denoise_work.get():indirect.factor.get());
    d3d12_->set_resource(14,indirect.denoise_packed[now].get());
    d3d12_->set_unordered(1,into_factor?indirect.factor.get():indirect.denoise_work.get());
    hr=d3d12_->dispatch(indirect.programs[kRtIndirectAtrous+pass],(width+15u)/16u,(height+7u)/8u,1);
   }
   if(hr==S_OK){denoised=true;std::memcpy(indirect.previous_ray.data(),step.ray,sizeof(step.ray));}
  }
 }
 d3d12_->clear_slots();
 if(hr==S_OK&&traced){
  // The bounce light over the world colour (waited for: one small program).
  transition_profile_pass("rt_indirect_add");
  d3d12_->set_targets(&world,1,nullptr);
  d3d12_->set_resource(0,indirect.factor.get());d3d12_->set_resource(1,up.guide_diffuse.get());d3d12_->set_resource(2,up.guide_specular.get());
  d3d12_->set_wait_for_pipelines(true);
  const HRESULT added=d3d12_->draw(rt.programs[kRtDebugVertex],indirect.programs[kRtIndirectAdd],post_state(true,false),nullptr,3);
  d3d12_->set_wait_for_pipelines(false);
  d3d12_->clear_slots();
  if(added==S_OK){
   profiler_.add_draw(1,1);profiler_.add_batch();d3d12_->count("world frames with ray traced indirect light");
   indirect.factor_frame=native_frame_index_;
  }
  else hr=FAILED(added)?added:S_FALSE;
 }
 bind_world_targets_d3d12();
 if(hr!=S_OK){
  // A pipeline still compiling (S_FALSE) or a failure: the next frames' twins keep the bounce volume until the passes ran.
  missing(hr==S_FALSE?"rt_indirect_light (pipeline not ready: no bounce light on the traced surfaces this frame)":"rt_indirect_light (a pass failed: no bounce light on the traced surfaces this frame)");
  return;
 }
 indirect.ready=true;indirect.idle_frames=0;indirect.idle_logged=nullptr;
 if(occlusion_written)indirect.ao_history_frame=native_frame_index_; // with the surfaces of the same parity
 indirect.ao_previous_offset[0]=occlusion_quad[0];indirect.ao_previous_offset[1]=occlusion_quad[1];
 indirect.denoise_frame=denoised?native_frame_index_:UINT64_MAX;
 indirect.history_frame=native_frame_index_;indirect.parity=1u-indirect.parity;
 indirect.previous_offset[0]=quad[0];indirect.previous_offset[1]=quad[1];
 std::memcpy(indirect.previous_world_to_clip.data(),scene.world_to_clip.data(),sizeof(float)*16);
 std::memcpy(indirect.previous_clip_to_world.data(),constants.clip_to_world,sizeof(float)*16);
 for(int axis=0;axis<3;++axis)indirect.previous_eye[axis]=fog_frame_d3d12_.eye[axis];
 indirect.previous_jitter[0]=dlss_jitter_[0];indirect.previous_jitter[1]=dlss_jitter_[1];
}catch(const std::bad_alloc&){
 rt_indirect12_.ready=false;rt_indirect12_.history_frame=UINT64_MAX;
 bridge_append_probe_event("rt_indirect_light_omitted","out of memory preparing its passes");
}

void Renderer::release_rt_reflect_d3d12() noexcept{
 auto& reflect=rt_reflect12_;
 reflect.light.reset();reflect.hit_distance.reset();reflect.water[0].reset();reflect.water[1].reset();reflect.hit_frame=UINT64_MAX;
 reflect.width=reflect.height=reflect.failed_width=reflect.failed_height=0;
 reflect.traced_frame=UINT64_MAX;reflect.ready=false;reflect.cutoff=0.f;
 reflect.particles_next.clear();reflect.particle_textures_next.clear();reflect.particle_textures.clear();reflect.particles_frame=UINT64_MAX;
 reflect.particle_scene.reset();reflect.particle_bottom.reset();
 reflect.particle_vertices.reset();reflect.particle_indices.reset();reflect.particle_records.reset();
}

// rtReflectCS's particle record (kRtReflectShader RfParticle).
struct RtReflectParticleGpu{float center_radius[4];float uv[4];std::uint32_t color,image,mode,padding;};
static_assert(sizeof(RtReflectParticleGpu)==48);
constexpr std::size_t kRtReflectParticleLimit=4096;
// One world particle call of the game (draw_game_effect_d3d12, its elements in game_effect_elements_) kept for
// this frame's reflection rays: each particle's clip position by the call's own matrix (what its quad is drawn
// with), its clip half size, texture rectangle and colour, and the call's fixed-function combiner and blend as
// rtReflectCS's mode word. A call the word cannot say (a darkening blend, a 2x or adding combiner, a texture
// factor, a complemented or alpha-replicated argument) is not reflected, counted.
void Renderer::collect_reflection_particles_d3d12(const GameEffectDraw& draw,const GameParticleConstants& c,const D3D12TextureRef& texture) noexcept try{
 auto& reflect=rt_reflect12_;
 if(reflect.particles_frame!=native_frame_index_){
  reflect.particles_next.clear();reflect.particle_textures_next.clear();reflect.particles_frame=native_frame_index_;
 }
 if(!draw.object_to_clip||!draw.element_count||draw.element_count>game_effect_elements_.size())return;
 std::uint32_t mode=0;
 const auto argument=[&](std::uint32_t value,std::uint32_t texture_bit,std::uint32_t diffuse_bit){
  if(value&~15u)return false;                    // complement, alpha replicate
  const std::uint32_t source=value&15u;
  if(source<=1u)mode|=diffuse_bit;               // diffuse, current (stage 0: the diffuse)
  else if(source==2u){if(c.textured)mode|=texture_bit;}
  else return false;                             // texture factor, specular
  return true;
 };
 const auto combiner=[&](std::uint32_t op,std::uint32_t arg1,std::uint32_t arg2,std::uint32_t texture_bit,std::uint32_t diffuse_bit){
  if(op==2u)return argument(arg1,texture_bit,diffuse_bit);
  if(op==3u)return argument(arg2,texture_bit,diffuse_bit);
  if(op==4u)return argument(arg1,texture_bit,diffuse_bit)&&argument(arg2,texture_bit,diffuse_bit);
  if(op==5u||op==7u)return false;                // modulate 2x, add
  mode|=diffuse_bit;return true;                 // gpColor's default: the diffuse
 };
 if(c.coverage_mode==3u||!combiner(c.color_op,c.color_arg1,c.color_arg2,1u,2u)||!combiner(c.alpha_op,c.alpha_arg1,c.alpha_arg2,4u,8u)){
  d3d12_->count("game particle calls the reflection rays cannot show (their blend or combiner)");return;
 }
 if(c.coverage_mode==1u)mode|=16u|32u;else if(c.coverage_mode==2u)mode|=16u;
 std::uint32_t image=0;
 if((mode&5u)!=0){
  if(!texture)return;
  image=d3d12_->bindless_index(*texture);
  if(image==UINT32_MAX||image==0u){d3d12_->count("game particle calls without a bindless texture index (not reflected)");return;}
  if(reflect.particle_textures_next.empty()||reflect.particle_textures_next.back()!=texture)reflect.particle_textures_next.push_back(texture);
 }
 const float* m=draw.object_to_clip;
 for(UINT n=0;n<draw.element_count;++n){
  if(reflect.particles_next.size()>=kRtReflectParticleLimit){d3d12_->count("game particles over the reflection structure's limit (not reflected)");break;}
  const GameParticleGpu& g=game_effect_elements_[n];
  RtReflectD3D12::Particle particle{};
  for(int row=0;row<4;++row)
   particle.clip[row]=m[row*4]*g.center_radius[0]+m[row*4+1]*g.center_radius[1]+m[row*4+2]*g.center_radius[2]+m[row*4+3];
  particle.radius=g.center_radius[3]*c.clip_half[2];
  std::memcpy(particle.uv,g.uv,sizeof(particle.uv));
  particle.color=g.color;particle.texture=image;particle.mode=mode;
  reflect.particles_next.push_back(particle);
 }
}catch(const std::bad_alloc&){
 rt_reflect12_.particles_next.clear();rt_reflect12_.particle_textures_next.clear();
}

// This frame's kept particles as rtReflectCS traces them: each back in the world through the scene's clip to
// world matrix (the particle calls and the world draw on one screen and test one depth, so their clip spaces
// agree; a call whose matrix is not the scene's is named once), its world radius from its clip half size, a
// bounding octahedron of the square's circumscribed sphere (six vertices, eight triangles, no triangle opaque)
// in a bottom level built again each frame, and a top level of that one instance. Returns the particles held.
UINT Renderer::build_reflection_particles_d3d12(const float clip_to_world[16]) noexcept try{
 auto& reflect=rt_reflect12_;
 auto pending=std::move(reflect.particles_next);reflect.particles_next.clear();
 auto textures=std::move(reflect.particle_textures_next);reflect.particle_textures_next.clear();
 if(reflect.particles_frame!=native_frame_index_||pending.empty())return 0;
 std::vector<RtReflectParticleGpu> records;std::vector<float> vertices;std::vector<std::uint32_t> indices;
 records.reserve(pending.size());vertices.reserve(pending.size()*18u);indices.reserve(pending.size()*24u);
 const auto world=[&](const float k[4],float out[3]){
  const float* m=clip_to_world;
  const float w=m[12]*k[0]+m[13]*k[1]+m[14]*k[2]+m[15]*k[3];
  if(!(std::abs(w)>1e-12f))return false;
  for(int axis=0;axis<3;++axis)out[axis]=(m[axis*4]*k[0]+m[axis*4+1]*k[1]+m[axis*4+2]*k[2]+m[axis*4+3]*k[3])/w;
  return std::isfinite(out[0])&&std::isfinite(out[1])&&std::isfinite(out[2]);
 };
 static const std::uint32_t kFaces[24]={0,2,4, 2,1,4, 1,3,4, 3,0,4, 2,0,5, 1,2,5, 3,1,5, 0,3,5};
 for(const auto& particle:pending){
  float centre[3],edge[3];
  const float shifted[4]={particle.clip[0]+particle.radius,particle.clip[1],particle.clip[2],particle.clip[3]};
  if(!(particle.clip[3]>1e-4f)||!world(particle.clip,centre)||!world(shifted,edge))continue;
  const float radius=std::sqrt((edge[0]-centre[0])*(edge[0]-centre[0])+(edge[1]-centre[1])*(edge[1]-centre[1])+(edge[2]-centre[2])*(edge[2]-centre[2]));
  if(!(radius>1e-4f)||radius>64.f)continue;
  RtReflectParticleGpu record{};
  std::memcpy(record.center_radius,centre,sizeof(centre));record.center_radius[3]=radius;
  std::memcpy(record.uv,particle.uv,sizeof(record.uv));
  record.color=particle.color;record.image=particle.texture;record.mode=particle.mode;
  // The square's corners lie sqrt(2) radii out; an octahedron holds a sphere of its vertex distance / sqrt(3).
  const float reach=radius*2.46f;
  const std::uint32_t base=std::uint32_t(records.size())*6u;
  for(int axis=0;axis<3;++axis)for(int side=0;side<2;++side){
   float v[3]={centre[0],centre[1],centre[2]};v[axis]+=side?-reach:reach;
   vertices.insert(vertices.end(),v,v+3);
  }
  for(const std::uint32_t corner:kFaces)indices.push_back(base+corner);
  records.push_back(record);
 }
 if(records.empty())return 0;
 const UINT64 record_bytes=UINT64(records.size())*sizeof(RtReflectParticleGpu),vertex_bytes=UINT64(vertices.size())*sizeof(float),
  index_bytes=UINT64(indices.size())*sizeof(std::uint32_t);
 HRESULT hr=native_d3d12_buffer(reflect.particle_records,record_bytes,sizeof(RtReflectParticleGpu),0);
 if(SUCCEEDED(hr))hr=native_d3d12_buffer(reflect.particle_vertices,vertex_bytes,0,0);
 if(SUCCEEDED(hr))hr=native_d3d12_buffer(reflect.particle_indices,index_bytes,0,0);
 if(SUCCEEDED(hr))hr=d3d12_->write_buffer(*reflect.particle_records,0,records.data(),record_bytes);
 if(SUCCEEDED(hr))hr=d3d12_->write_buffer(*reflect.particle_vertices,0,vertices.data(),vertex_bytes);
 if(SUCCEEDED(hr))hr=d3d12_->write_buffer(*reflect.particle_indices,0,indices.data(),index_bytes);
 if(SUCCEEDED(hr)){
  D3D12RaytracingGeometry geometry;
  geometry.vertex_count=UINT(records.size()*6u);geometry.index_count=UINT(indices.size());geometry.opaque=false;
  hr=d3d12_->build_raytracing_bottom(*reflect.particle_vertices,12,*reflect.particle_indices,&geometry,1,reflect.particle_bottom,true);
 }
 if(SUCCEEDED(hr)){
  D3D12RaytracingInstance instance{reflect.particle_bottom.get(),0u,true,0xFFu};
  hr=d3d12_->build_raytracing_top(&instance,1,reflect.particle_scene,true);
 }
 if(FAILED(hr)){
  reflect.particle_scene.reset();reflect.particle_bottom.reset();
  d3d12_->omit("rt_reflections: the particles' structure could not be built (no particle in reflections this frame)",false);
  return 0;
 }
 reflect.particle_textures=std::move(textures);
 d3d12_->count("world frames with particles in ray traced reflections");
 return UINT(records.size());
}catch(const std::bad_alloc&){
 return 0;
}

// What a reflection ray's hit is lit with (rtLit): the sun (none in a cave),
// its colour and the ambient light, as rt_debug_view 6 gives them.
template<class Lighting> static void rt_reflect_lighting(const Lighting& lighting,bool cave,const float fog[3],RtReflectConstants& constants){
 for(int axis=0;axis<3;++axis)constants.fog[axis]=fog[axis];
 constants.fog[3]=1.f;
 const auto& sun=lighting.sunlight_direction;
 const float length=std::sqrt(sun[0]*sun[0]+sun[1]*sun[1]+sun[2]*sun[2]);
 for(int axis=0;axis<3;++axis){
  constants.ambient[axis]=lighting.ambient[axis];
  if(length>1e-6f&&!cave){constants.sun[axis]=sun[axis]/length;constants.sun_diffuse[axis]=lighting.sunlight_diffuse[axis];}
 }
}

// rt_reflections on the smooth surfaces of the opaque and cut-out world
// (renderer.h RtReflectD3D12), once a world frame after the traced lamp and
// bounce light: rtReflectCS over the guide images into the light image, then
// the additive pass over the world colour. A surface's environment reflection
// has one owner a frame: the material programs' sky probe or ambient term, or
// this pass for the share under the roughness cut-off (the twins of a traced
// frame leave that share out). A traced frame whose passes then fail shows no
// environment reflection on those surfaces, named once.
void Renderer::reflections_d3d12() noexcept try{
 auto& reflect=rt_reflect12_;auto& rt=rt12_;auto& up=upscale12_;
 if(!rt_reflect_enabled_||hit_test_active_||!world_d3d12_)return;
 // rt_reflection_surfaces off: water alone is traced (water_reflection_rays_d3d12); the twins keep their own reflection.
 if(!rt_reflect_surfaces_){reflect.ready=false;return;}
 const bool traced=reflect.traced_frame==native_frame_index_;
 const auto missing=[&](const char* why){
  reflect.ready=false;
  if(traced)d3d12_->omit(why,false);
  if(++reflect.idle_frames>=120&&reflect.idle_logged!=why){reflect.idle_logged=why;bridge_append_probe_event("rt_reflections_omitted",why);}
 };
 if(!guide_frame_d3d12()||rt.frame!=native_frame_index_||!rt.scene||!rt.records||!rt.shade)
  return missing("rt_reflections (no guide images or ray tracing scene this frame)");
 const auto scene=bridge_scene_world_to_clip_snapshot();
 RtReflectConstants constants{};
 if(!scene.valid||!ambient_occlusion_invert(scene.world_to_clip.data(),constants.clip_to_world))return missing("rt_reflections (no world camera this frame)");
 if(reflect.programs.empty())append_rt_reflection_shader_programs(reflect.programs);
 if(reflect.programs.size()<kRtReflectPrograms||rt.programs.size()<kRtPrograms)return missing("rt_reflections (programs missing)");
 const UINT width=width_,height=height_;
 if(width==reflect.failed_width&&height==reflect.failed_height)return missing("rt_reflections (its image could not be created)");
 if(reflect.width!=width||reflect.height!=height||!reflect.light||!reflect.hit_distance){
  reflect.light.reset();reflect.hit_distance.reset();reflect.width=width;reflect.height=height;
  if(!d3d12_->storage_supported(DXGI_FORMAT_R16G16B16A16_FLOAT)||!d3d12_->storage_supported(DXGI_FORMAT_R16_FLOAT)||
     FAILED(d3d12_->create_storage_texture(width,height,0,DXGI_FORMAT_R16G16B16A16_FLOAT,reflect.light))||
     FAILED(d3d12_->create_storage_texture(width,height,0,DXGI_FORMAT_R16_FLOAT,reflect.hit_distance))){
   reflect.light.reset();reflect.hit_distance.reset();reflect.failed_width=width;reflect.failed_height=height;
   bridge_append_probe_event("rt_reflections_omitted","its light image could not be created at this size; the sky probe reflection stays");
   return missing("rt_reflections (its image could not be created)");
  }
  char text[160];
  std::snprintf(text,sizeof(text),"%ux%u render pixels: reflected light %.1f MB, specular hit distance %.1f MB",width,height,
   double(width)*double(height)*8.0/1048576.0,double(width)*double(height)*2.0/1048576.0);
  bridge_append_probe_event("rt_reflections_memory",text); // once a size
 }
 const auto& config=debug_overlay_.config();
 for(int axis=0;axis<3;++axis)constants.eye[axis]=fog_frame_d3d12_.eye[axis];
 constants.size_jitter[0]=float(width);constants.size_jitter[1]=float(height);constants.size_jitter[2]=dlss_jitter_[0];constants.size_jitter[3]=dlss_jitter_[1];
 rt_reflect_lighting(native_world_lighting_,native_world_cave_,sky_fog_color_,constants);
 // The cut-off this frame's twins drew with (0: they kept their own reflection; the pass still runs once, for its pipelines).
 constants.control[0]=traced?reflect.cutoff:(std::clamp)(configured_floating_point(config,"rt_reflection_roughness",.5f),.1f,1.f);
 constants.control[1]=(std::clamp)(configured_floating_point(config,"material_reflections",1.f),0.f,2.f);
 constants.control[2]=.02f;constants.control[3]=.0015f; // the sun rays' self-hit distance and its growth per metre of view distance
 // A new ray each frame only where an upscaler accumulates frames; else one fixed pattern (no shimmer).
 constants.frame[0]=temporal_upscaler()?std::uint32_t(native_frame_index_):0u;
 std::copy(scene.world_to_clip.begin(),scene.world_to_clip.end(),constants.world_clip);
 D3D12Texture* world=world_d3d12_.get();
 transition_profile_pass("rt_reflections");
 d3d12_->clear_slots();
 // The game's particles of this frame as a structure of their own (0: none).
 constants.frame[1]=build_reflection_particles_d3d12(constants.clip_to_world);
 constants.frame[2]=1u;
 d3d12_->set_target(nullptr); // nothing bound: the world colour and depth are read
 d3d12_->set_constants(1,&constants,sizeof(constants));
 d3d12_->set_constants(5,native12_.point_light_constants.data(),sizeof(native12_.point_light_constants));
 d3d12_->set_depth_resource(0,world_d3d12_.get());
 d3d12_->set_resource(1,up.guide_normal.get());d3d12_->set_resource(3,up.guide_specular.get());
 d3d12_->set_resource(4,static_cast<const D3D12Texture*>(world));
 if(constants.frame[1]){d3d12_->set_resource(5,reflect.particle_scene.get());d3d12_->set_resource(6,reflect.particle_records.get());}
 d3d12_->set_resource(17,native_point_lights_.empty()?nullptr:native12_.point_light_grid.get());
 d3d12_->set_resource(18,native_point_lights_.empty()?nullptr:native12_.point_lights.get());
 d3d12_->set_resource(kTerrain2CacheCoarseSlotD3D12,terrain2_cache_on_?terrain2_cache_coarse12_.get():nullptr);
 d3d12_->set_resource(kTerrain2CacheParamsSlotD3D12,terrain2_cache_on_?terrain2_cache_params12_.get():nullptr);
 // The atmosphere a hit is fogged with and the sky probe a miss returns (bind_world_effect_d3d12's b10, t111, s7).
 d3d12_->set_constants(10,&fog_frame_d3d12_,sizeof(fog_frame_d3d12_));
 d3d12_->set_resource(kSkyProbeSlotD3D12,sky_inputs_active_?sky12_.probe[sky_index_].get():nullptr);
 d3d12_->set_resource(94,rt.scene.get());d3d12_->set_resource(95,rt.vertices.get());
 d3d12_->set_resource(96,rt.terrain_vertices.get());d3d12_->set_resource(97,rt.terrain_indices.get());
 d3d12_->set_resource(99,rt.indices.get());d3d12_->set_resource(113,rt.records.get());
 d3d12_->set_resource(114,rt.shade.get());d3d12_->set_resource(115,rt.figures_in_scene?rt.figure_shade.get():nullptr);
 {D3D11_SAMPLER_DESC wrap{};wrap.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  wrap.AddressU=wrap.AddressV=wrap.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;wrap.MaxLOD=D3D11_FLOAT32_MAX;
  d3d12_->set_sampler(0,wrap);wrap.MaxLOD=0;d3d12_->set_sampler(15,wrap);
  D3D11_SAMPLER_DESC linear{};linear.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  linear.AddressU=linear.AddressV=linear.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;linear.MaxLOD=D3D11_FLOAT32_MAX;
  d3d12_->set_sampler(7,linear);}
 d3d12_->set_unordered(0,reflect.light.get());d3d12_->set_unordered(1,reflect.hit_distance.get());
 HRESULT hr=d3d12_->dispatch(reflect.programs[kRtReflectTrace],(width+7u)/8u,(height+7u)/8u,1);
 d3d12_->clear_slots();
 if(hr==S_OK){
  // Ray Reconstruction's matrices for the hit distance (guide 3.4.9; row vectors, as the dxvk-remix reference hands
  // its column-major ones over): the camera's rigid frame from the unprojected axes and the eye, and the
  // projection that completes this frame's world to clip matrix after it.
  const auto point=[&](float x,float y,float z,float out[3]){
   const float* m=constants.clip_to_world;const float w=m[12]*x+m[13]*y+m[14]*z+m[15];
   for(int axis=0;axis<3;++axis)out[axis]=(m[axis*4]*x+m[axis*4+1]*y+m[axis*4+2]*z+m[axis*4+3])/w;
  };
  float a[3],b[3],forward[3],right[3],up[3];
  const auto unit=[](float v[3]){const float l=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);if(l>1e-12f)for(int axis=0;axis<3;++axis)v[axis]/=l;return l>1e-12f;};
  const auto dot=[](const float p[3],const float q[3]){return p[0]*q[0]+p[1]*q[1]+p[2]*q[2];};
  point(0,0,0,a);point(0,0,1,b);for(int axis=0;axis<3;++axis)forward[axis]=b[axis]-a[axis];
  point(-1,0,.5f,a);point(1,0,.5f,b);for(int axis=0;axis<3;++axis)right[axis]=b[axis]-a[axis];
  point(0,-1,.5f,a);point(0,1,.5f,b);for(int axis=0;axis<3;++axis)up[axis]=b[axis]-a[axis];
  bool framed=unit(forward);
  {const float along=dot(right,forward);for(int axis=0;axis<3;++axis)right[axis]-=forward[axis]*along;}
  framed=framed&&unit(right);
  {const float along=dot(up,forward),across=dot(up,right);for(int axis=0;axis<3;++axis)up[axis]-=forward[axis]*along+right[axis]*across;}
  framed=framed&&unit(up);
  reflect.hit_frame=UINT64_MAX;
  if(framed){
   const float* eye=constants.eye;const float* rows[3]={right,up,forward};
   float view[16]{},inverse[16]{},projection[16]{};
   for(int row=0;row<3;++row){
    for(int axis=0;axis<3;++axis){view[row*4+axis]=rows[row][axis];inverse[axis*4+row]=rows[row][axis];}
    view[row*4+3]=-dot(rows[row],eye);inverse[row*4+3]=eye[row];
   }
   view[15]=inverse[15]=1.f;
   for(int row=0;row<4;++row)for(int column=0;column<4;++column)
    for(int k=0;k<4;++k)projection[row*4+column]+=scene.world_to_clip[row*4+k]*inverse[k*4+column];
   for(int row=0;row<4;++row)for(int column=0;column<4;++column){
    reflect.world_to_view[row*4+column]=view[column*4+row];reflect.view_to_clip[row*4+column]=projection[column*4+row];
   }
   reflect.hit_frame=native_frame_index_;
  }
 }
 if(hr==S_OK&&traced){
  transition_profile_pass("rt_reflections_add");
  d3d12_->set_targets(&world,1,nullptr);
  d3d12_->set_resource(0,reflect.light.get());
  d3d12_->set_wait_for_pipelines(true);
  const HRESULT added=d3d12_->draw(rt.programs[kRtDebugVertex],reflect.programs[kRtReflectAdd],post_state(true,false),nullptr,3);
  d3d12_->set_wait_for_pipelines(false);
  d3d12_->clear_slots();
  if(added==S_OK){profiler_.add_draw(1,1);profiler_.add_batch();d3d12_->count("world frames with ray traced reflections");}
  else hr=FAILED(added)?added:S_FALSE;
 }
 bind_world_targets_d3d12();
 if(hr!=S_OK){
  missing(hr==S_FALSE?"rt_reflections (pipeline not ready: no environment reflection on the traced surfaces this frame)":"rt_reflections (a pass failed: no environment reflection on the traced surfaces this frame)");
  return;
 }
 reflect.ready=true;reflect.idle_frames=0;reflect.idle_logged=nullptr;
}catch(const std::bad_alloc&){
 rt_reflect12_.ready=false;
 bridge_append_probe_event("rt_reflections_omitted","out of memory preparing its pass");
}

// rt_reflections on water: the water trace's samples after the screen march
// (ssr 2 and 3), each traced along its mirror ray through the ray tracing
// scene (rtWaterPS), into reflect.water. Inside the world flush: it leaves
// the flush's own bindings as they were (the lighting constants at b1 set
// again; the cut-out cards' sampler at s14, not a batch's s0).
bool Renderer::water_reflection_rays_d3d12(const ShaderProgram& full_screen_vs,const D3D12NativeState& full_screen){
 auto& reflect=rt_reflect12_;auto& rt=rt12_;
 if(!rt_reflect_enabled_||hit_test_active_||rt.frame!=native_frame_index_||!rt.scene||!rt.records||!rt.shade)return false;
 try{if(reflect.programs.empty())append_rt_reflection_shader_programs(reflect.programs);}catch(const std::bad_alloc&){return false;}
 if(reflect.programs.size()<kRtReflectPrograms)return false;
 UINT width=0,height=0,levels=0;DXGI_FORMAT colour=DXGI_FORMAT_UNKNOWN,key=DXGI_FORMAT_UNKNOWN;
 if(!water12_.ssr[2]||!water12_.ssr[3]||!d3d12_->texture_info(*water12_.ssr[3],width,height,levels,key)||
    !d3d12_->texture_info(*water12_.ssr[2],width,height,levels,colour))return false;
 if(!reflect.water[0]||!reflect.water[1]||!d3d12_->texture_matches(*reflect.water[0],width,height,colour)||
    !d3d12_->texture_matches(*reflect.water[1],width,height,key)){
  reflect.water[0].reset();reflect.water[1].reset();
  if(FAILED(d3d12_->create_target(width,height,reflect.water[0],colour,DXGI_FORMAT_UNKNOWN))||
     FAILED(d3d12_->create_target(width,height,reflect.water[1],key,DXGI_FORMAT_UNKNOWN))){
   reflect.water[0].reset();reflect.water[1].reset();
   if(!reflect.water_reported){reflect.water_reported=true;bridge_append_probe_event("rt_reflections_omitted","the water trace's ray images could not be created; water keeps its screen reflections");}
   return false;
  }
 }
 RtReflectConstants constants{};
 rt_reflect_lighting(native_world_lighting_,native_world_cave_,sky_fog_color_,constants);
 // A ray's start and reach (metres; a hit hands over to the screen march's sample over the last two fifths).
 constants.control[2]=.05f;
 constants.control[3]=(std::clamp)(configured_floating_point(debug_overlay_.config(),"rt_reflection_water_reach",250.f),10.f,4000.f);
 transition_profile_pass("rt_water_reflections");
 D3D12Texture* targets[2]={reflect.water[0].get(),reflect.water[1].get()};
 d3d12_->set_targets(targets,2,nullptr);
 d3d12_->set_constants(1,&constants,sizeof(constants));
 d3d12_->set_resource(29,water12_.ssr[2].get());d3d12_->set_resource(30,water12_.ssr[3].get());
 d3d12_->set_resource(31,water12_.ssr[1].get());d3d12_->set_resource(32,water12_.ssr[0].get());
 d3d12_->set_resource(94,rt.scene.get());d3d12_->set_resource(95,rt.vertices.get());
 d3d12_->set_resource(96,rt.terrain_vertices.get());d3d12_->set_resource(97,rt.terrain_indices.get());
 d3d12_->set_resource(99,rt.indices.get());d3d12_->set_resource(113,rt.records.get());
 d3d12_->set_resource(114,rt.shade.get());d3d12_->set_resource(115,rt.figures_in_scene?rt.figure_shade.get():nullptr);
 {D3D11_SAMPLER_DESC wrap{};wrap.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  wrap.AddressU=wrap.AddressV=wrap.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;wrap.MaxLOD=D3D11_FLOAT32_MAX;
  d3d12_->set_sampler(14,wrap);}
 // The atmosphere an off-screen hit is fogged with (the world batches' own b10, t111 and s7 values).
 d3d12_->set_constants(10,&fog_frame_d3d12_,sizeof(fog_frame_d3d12_));
 d3d12_->set_resource(kSkyProbeSlotD3D12,sky_inputs_active_?sky12_.probe[sky_index_].get():nullptr);
 {D3D11_SAMPLER_DESC linear{};linear.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  linear.AddressU=linear.AddressV=linear.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;linear.MaxLOD=D3D11_FLOAT32_MAX;d3d12_->set_sampler(7,linear);}
 const HRESULT drawn=d3d12_->draw(full_screen_vs,reflect.programs[kRtReflectWater],full_screen,nullptr,3);
 for(const UINT slot:{94u,95u,96u,97u,99u,113u,114u,115u})d3d12_->set_resource(slot,static_cast<const D3D12Buffer*>(nullptr));
 d3d12_->set_resource(32,static_cast<const D3D12Texture*>(nullptr));
 d3d12_->set_constants(1,&native_world_lighting_,sizeof(native_world_lighting_));
 transition_profile_pass("water_ssr");
 if(drawn!=S_OK){d3d12_->count(drawn==S_FALSE?"water reflection rays waiting for their pipeline":"water reflection rays failed");return false;}
 profiler_.add_draw(1,1);profiler_.add_batch();d3d12_->count("water traces with reflection rays");
 return true;
}

void Renderer::release_raytracing_d3d12() noexcept{
 auto& rt=rt12_;
 rt.scene.reset();rt.terrain.reset();rt.map=UINT64_MAX;
 rt.figure_bottom.reset();rt.figures_next.clear();rt.figures_in_scene=false;
 rt.chunks.clear();rt.members.clear();
 rt.vertices.reset();rt.indices.reset();rt.records.reset();
 rt.shade.reset();rt.figure_shade.reset();rt.figure_textures.clear();rt.terrain_vertices.reset();rt.terrain_indices.reset();
 rt.vertex_capacity=rt.vertex_used=rt.vertex_dropped=0;
 rt.triangle_capacity=rt.triangle_used=rt.triangle_dropped=0;
 rt.source_valid=false;rt.scene_dirty=false;rt.shadow_frame=UINT64_MAX;
 rt.neighbour_terrain.clear();rt.neighbour_terrain_layout=rt.neighbour_terrain_key=0;
 rt.instances.clear();
 rt.store_valid.clear();rt.store_probe_ids.clear();rt.store_ticket=rt.store_probe_frame=0;
}

// The static casters of the scene brought up to their sources' state: the
// active map's object-space caster records from their registry (the objects
// the bridge published and active_statics_.casters: what the active sun bake
// is built from, and what a cave, which has no sun bake, registers for this
// scene alone), the neighbour maps' from their bake.
// S_OK: they are current (scene_dirty when chunks changed). S_FALSE: the
// repack pipeline is not ready; nothing was kept, the next frame tries again.
// A failure is not retried until the sources change.
HRESULT Renderer::update_raytracing_statics_d3d12(){
 auto& rt=rt12_;
 const auto active_casters=active_statics_casters();
 const bool active=active_casters&&!active_casters->ids.empty()&&!active_statics_.casters.empty();
 const NativeStaticShadowBake& neighbours=native_neighbour_shadow_bake_;
 // A cut-out record: its texture's alpha against its reference (the native
 // alpha_ref scale, as float bits); a record without an alpha test or
 // texture, or with another alpha function, casts whole.
 const auto raytracing_record_cutout=[](const NativeQueuedDraw& draw,const FixedState& state,bool& cutout,std::uint32_t& reference_bits){
  const DWORD function=state.render[D3DRENDERSTATE_ALPHAFUNC];
  cutout=state.render[D3DRENDERSTATE_ALPHATESTENABLE]!=FALSE&&(function==D3DCMP_GREATER||function==D3DCMP_GREATEREQUAL)&&
   draw.texture12&&draw.instance.padding[0]!=kD3D12NullTextureIndex;
  const float reference=(state.render[D3DRENDERSTATE_ALPHAREF]&255)/256.f;
  reference_bits=0;
  if(cutout)std::memcpy(&reference_bits,&reference,sizeof(reference_bits));
 };
 const auto mix=[](std::uint64_t hash,std::uint64_t value){hash^=value+0x9E3779B97F4A7C15ull+(hash<<6)+(hash>>2);return hash;};
 std::uint64_t source=mix(0x52542D4D31ull,native_static_content_revision_);
 source=mix(mix(source,active?active_casters->revision:0u),active?active_statics_.caster_revision:0u);
 source=mix(source,neighbours.built?1u:0u);source=mix(source,neighbours.built_key);
 source=mix(source,neighbours.built_content_revision);source=mix(source,neighbours.records.size());
 // The neighbour maps' terrain records (world records of terrain meshes with
 // ground, flagged neighbour), found again when the record table changed.
 const auto neighbour_terrain_record=[&](const NativeInstanceRecord& record){
  const auto& draw=record.command;
  return record.occupied&&draw.mesh&&draw.mesh->has_ground&&(draw.instance.flags&native_neighbour_flag)!=0&&
   draw.instance.terrain_vertex_offset!=UINT_MAX&&draw.instance.world_geometry!=0&&
   !(draw.instance.world_geometry&native_world_object_space)&&draw.instance.pose_node==UINT_MAX;
 };
 if(rt.neighbour_terrain_layout!=native_layout_revision_){
  rt.neighbour_terrain_layout=native_layout_revision_;
  rt.neighbour_terrain.clear();rt.neighbour_terrain_key=0;
  for(std::size_t index=0;index<native_instance_records_.size();++index){
   const auto& record=native_instance_records_[index];
   if(!neighbour_terrain_record(record))continue;
   rt.neighbour_terrain.push_back(UINT(index));
   rt.neighbour_terrain_key=mix(mix(mix(rt.neighbour_terrain_key,index),record.command.instance.geometry_offsets[0]),record.command.geometry_page);
   std::uint32_t placement[2];std::memcpy(placement,&record.command.instance.object_to_world._14,4);std::memcpy(placement+1,&record.command.instance.object_to_world._24,4);
   rt.neighbour_terrain_key=mix(mix(rt.neighbour_terrain_key,placement[0]),placement[1]);
  }
 }
 source=mix(source,rt.neighbour_terrain_key);
 // The static caster store's posed records and the probe of their world vertices.
 struct Stored{UINT record;std::uint64_t id;};
 std::vector<Stored> stored;
 for(const auto& selected:native_static_shadow_bake_.records){
  if(selected.record>=native_instance_records_.size())continue;
  const auto& record=native_instance_records_[selected.record];
  const auto& draw=record.command;
  if(!record.occupied||!draw.mesh||draw.state_bucket>=native_state_buckets_.size()||!draw.instance.world_geometry||
     (draw.instance.world_geometry&native_world_object_space)||draw.world_allocation>=native_world_ranges_.size()||
     draw.geometry_page>=native_geometry_pages_.size()||!native_geometry_pages_[draw.geometry_page].d3d12||
     draw.index_count<3||!draw.component_count||(std::max)(draw.index_count/3,draw.component_count)>4000000u)continue;
  stored.push_back({selected.record,mix(mix(mix(0x53544F5245ull,selected.record),draw.world_allocation),native_world_ranges_[draw.world_allocation].generation)});
 }
 if(rt.store_ticket){
  std::vector<std::uint8_t> answers;
  const HRESULT polled=d3d12_->poll_readback(rt.store_ticket,answers);
  if(polled!=S_FALSE){
   for(std::size_t n=0;polled==S_OK&&n<rt.store_probe_ids.size()&&(n+1)*4<=answers.size();++n){
    std::uint32_t current=0;std::memcpy(&current,answers.data()+n*4,4);
    if(current)rt.store_valid.insert(rt.store_probe_ids[n]);
   }
   rt.store_ticket=0;rt.store_probe_ids.clear();++rt.store_revision;
  }
 }
 if(rt.store_valid.size()>stored.size()*4+1024){rt.store_valid.clear();++rt.store_revision;} // ids of records long gone
 std::size_t store_waiting=0;
 for(const auto& entry:stored)if(!rt.store_valid.count(entry.id))++store_waiting;
 // Under the load hold every frame probes: the records confirmed there are
 // built there, not in the first second of play.
 if(store_waiting&&!rt.store_ticket&&native12_.world_states&&native_frame_index_>=rt.store_probe_frame+(load_reveal_holding_?1u:30u)){
  std::vector<std::uint32_t> probes;std::vector<std::uint64_t> ids;
  const UINT64 states=d3d12_->buffer_bytes(*native12_.world_states)/sizeof(NativeWorldStateGpu);
  for(const auto& entry:stored){
   if(rt.store_valid.count(entry.id))continue;
   const auto& draw=native_instance_records_[entry.record].command;
   const std::uint64_t generation=native_world_ranges_[draw.world_allocation].generation;
   probes.insert(probes.end(),{draw.world_allocation<states?draw.world_allocation:UINT32_MAX,std::uint32_t(generation),std::uint32_t(generation>>32),0u});
   ids.push_back(entry.id);
  }
  D3D12BufferRef probe_buffer,answer_buffer;
  const UINT count=UINT(ids.size());
  if(SUCCEEDED(d3d12_->create_buffer(UINT64(count)*16,16,0,probe_buffer))&&
     SUCCEEDED(d3d12_->write_buffer(*probe_buffer,0,probes.data(),UINT64(count)*16))&&
     SUCCEEDED(d3d12_->create_buffer(UINT64(count)*4,0,kD3D12BufferUnordered,answer_buffer))){
   const UINT constants[4]={count,0,0,0};
   d3d12_->set_constants(12,constants,sizeof(constants));
   d3d12_->set_resource(40,probe_buffer.get());d3d12_->set_resource(60,native12_.world_states.get());
   d3d12_->set_unordered(0,answer_buffer.get());
   const HRESULT probed=d3d12_->dispatch(rt.programs[kRtProbe],(count+63u)/64u,1,1);
   d3d12_->set_resource(40,static_cast<const D3D12Buffer*>(nullptr));d3d12_->set_resource(60,static_cast<const D3D12Buffer*>(nullptr));
   d3d12_->set_unordered(0,static_cast<const D3D12Buffer*>(nullptr));
   if(probed==S_OK&&SUCCEEDED(d3d12_->request_readback(*answer_buffer,UINT64(count)*4,rt.store_ticket,"ray tracing store probe"))){
    rt.store_probe_ids=std::move(ids);rt.store_probe_frame=native_frame_index_;
   }else rt.store_ticket=0;
  }
 }
 source=mix(mix(source,rt.store_revision),stored.size());
 if(store_waiting||rt.store_ticket)rt.statics_busy_frame=native_frame_index_;
 const bool instancing=configured_boolean(debug_overlay_.config(),"rt_instancing",true);
 source=mix(source,instancing?1u:0u);
 if(rt.source_valid&&rt.source_key==source)return S_OK;
 const std::int64_t begin=perf_now();
 // What the sources cast now: each object-space record once, by what would be repacked.
 struct Wanted{std::uint64_t key;UINT record;UINT page;bool terrain;bool instanced;bool world=false;};
 std::vector<Wanted> wanted;
 std::vector<RaytracingD3D12::Instance> instances;
 std::size_t posed=0,oversized=0;
 // mesh: the key of the mesh at its shape alone (rt_instancing), without
 // the record's transform, texture and alpha test.
 const auto record_key=[&](const NativeQueuedDraw& draw,const FixedState& state,bool mesh=false){
  std::uint64_t hash=mesh?0x84222325cbf29ce4ull:0xcbf29ce484222325ull;
  const auto bytes=[&](const void* data,std::size_t size){
   const auto* at=static_cast<const unsigned char*>(data);
   for(std::size_t n=0;n<size;++n){hash^=at[n];hash*=0x100000001b3ull;}
  };
  const auto& instance=draw.instance;
  bytes(&draw.geometry_page,sizeof(draw.geometry_page));
  bytes(instance.geometry_offsets.data(),sizeof(instance.geometry_offsets));
  bytes(&draw.mesh->index_offset,sizeof(UINT));
  bytes(&draw.index_count,sizeof(UINT));bytes(&draw.component_count,sizeof(UINT));
  if(!mesh)bytes(&instance.object_to_world,sizeof(float)*12);
  bytes(&instance.shape_count,sizeof(UINT));bytes(&instance.position_groups,sizeof(UINT));
  bytes(instance.shape,sizeof(instance.shape));bytes(instance.uv_shift,sizeof(instance.uv_shift));
  if(!mesh){
   bytes(&instance.padding[0],sizeof(UINT));
   const DWORD alpha[3]={state.render[D3DRENDERSTATE_ALPHATESTENABLE],state.render[D3DRENDERSTATE_ALPHAFUNC],state.render[D3DRENDERSTATE_ALPHAREF]};
   bytes(alpha,sizeof(alpha));
  }
  if(draw.mesh->geometry_revision_valid)bytes(draw.mesh->geometry_revision.data(),sizeof(draw.mesh->geometry_revision));
  return hash;
 };
 std::vector<UINT> sources;
 if(active)for(const auto id:active_casters->ids){
  const auto found=active_statics_.casters.find(id);
  if(found!=active_statics_.casters.end())sources.insert(sources.end(),found->second.records.begin(),found->second.records.end());
 }
 for(const auto& selected:neighbours.records)sources.push_back(selected.record);
 for(const UINT caster:sources){
  if(caster>=native_instance_records_.size())continue;
  const auto& record=native_instance_records_[caster];
  const auto& draw=record.command;
  if(!record.occupied||!draw.mesh||draw.state_bucket>=native_state_buckets_.size())continue;
  if(!(draw.instance.world_geometry&native_world_object_space)){++posed;continue;}
  if(draw.geometry_page>=native_geometry_pages_.size()||!native_geometry_pages_[draw.geometry_page].d3d12)continue;
  if(draw.index_count<3||!draw.component_count)continue;
  // One dispatch holds a job's threads (65535 groups of 64).
  if((std::max)(draw.index_count/3,draw.component_count)>4000000u){++oversized;continue;}
  const FixedState& state=native_state_buckets_[draw.state_bucket].state;
  if(!instancing){wanted.push_back({record_key(draw,state),caster,draw.geometry_page,false,false});continue;}
  RaytracingD3D12::Instance instance;
  instance.mesh=record_key(draw,state,true);
  std::memcpy(instance.transform,&draw.instance.object_to_world,sizeof(instance.transform));
  raytracing_record_cutout(draw,state,instance.cutout,instance.alpha_reference);
  if(instance.cutout)instance.texture=draw.instance.padding[0];
  instance.kept=draw.texture12;raytracing_shade_words(draw,false,instance.shade);
  instances.push_back(std::move(instance));
  wanted.push_back({instances.back().mesh,caster,draw.geometry_page,false,true});
 }
 // The store's confirmed records: world-space copies from their world vertices.
 std::size_t store_traced=0;
 for(const auto& entry:stored){
  if(!rt.store_valid.count(entry.id))continue;
  const auto& draw=native_instance_records_[entry.record].command;
  wanted.push_back({mix(record_key(draw,native_state_buckets_[draw.state_bucket].state),entry.id),entry.record,draw.geometry_page,false,false,true});
  ++store_traced;
 }
 std::size_t neighbour_terrain=0;
 for(const UINT index:rt.neighbour_terrain){
  if(index>=native_instance_records_.size()||!neighbour_terrain_record(native_instance_records_[index]))continue;
  const auto& draw=native_instance_records_[index].command;
  if(draw.state_bucket>=native_state_buckets_.size()||draw.geometry_page>=native_geometry_pages_.size()||
     !native_geometry_pages_[draw.geometry_page].d3d12||draw.index_count<3||!draw.component_count)continue;
  if((std::max)(draw.index_count/3,draw.component_count)>4000000u){++oversized;continue;}
  // Its key also covers the terrain vertices' place (the triangle filter reads them).
  wanted.push_back({mix(record_key(draw,native_state_buckets_[draw.state_bucket].state),0x7465727261696Eull+draw.instance.terrain_vertex_offset),
   index,draw.geometry_page,true,false});
  ++neighbour_terrain;
 }
 std::sort(wanted.begin(),wanted.end(),[](const Wanted& a,const Wanted& b){return a.key<b.key;});
 wanted.erase(std::unique(wanted.begin(),wanted.end(),[](const Wanted& a,const Wanted& b){return a.key==b.key;}),wanted.end());
 const auto is_wanted=[&](std::uint64_t key){
  const auto it=std::lower_bound(wanted.begin(),wanted.end(),key,[](const Wanted& entry,std::uint64_t value){return entry.key<value;});
  return it!=wanted.end()&&it->key==key;
 };
 // A chunk that lost a record goes; its other records are built again below.
 for(auto chunk=rt.chunks.begin();chunk!=rt.chunks.end();){
  if(std::all_of(chunk->keys.begin(),chunk->keys.end(),is_wanted)){++chunk;continue;}
  for(const auto key:chunk->keys)rt.members.erase(key);
  rt.vertex_dropped+=chunk->vertices;rt.triangle_dropped+=chunk->triangles;
  chunk=rt.chunks.erase(chunk);rt.scene_dirty=true;
 }
 const auto demand=[&](const std::vector<Wanted>& list,std::uint64_t& vertices,std::uint64_t& triangles){
  vertices=triangles=0;
  for(const auto& entry:list){const auto& draw=native_instance_records_[entry.record].command;vertices+=draw.component_count;triangles+=draw.index_count/3;}
 };
 std::vector<Wanted> pending;
 for(const auto& entry:wanted)if(rt.members.find(entry.key)==rt.members.end())pending.push_back(entry);
 std::uint64_t pending_vertices=0,pending_triangles=0;
 demand(pending,pending_vertices,pending_triangles);
 const std::size_t instances_now=instances.size();
 bool arena_moved=false; // the kept chunks moved into larger arenas (below)
 const auto report=[&](const char* state){
  std::size_t records=0;std::uint64_t triangles=0;
  for(const auto& chunk:rt.chunks){records+=chunk.keys.size();triangles+=chunk.triangles;}
  char text[400];
  std::snprintf(text,sizeof(text),"%s chunks=%zu records=%zu triangles=%llu instances=%zu built_now=%zu arena_vertices=%u/%u arena_triangles=%u/%u "
   "neighbour_terrain_records=%zu store_traced=%zu not_traced: posed_records=%zu oversized=%zu store_waiting_for_world_vertices=%zu arena_moved=%d cpu_ms=%.2f",state,rt.chunks.size(),records,
   static_cast<unsigned long long>(triangles),instances_now,pending.size(),rt.vertex_used,rt.vertex_capacity,rt.triangle_used,rt.triangle_capacity,
   neighbour_terrain,store_traced,posed,oversized,store_waiting,arena_moved?1:0,double(perf_now()-begin)*1000.0/double(perf_frequency()));
  bridge_append_probe_event("rt_statics",text);
 };
 // The instances of this state; the top level leaves out one whose mesh is not built.
 if(instancing||!rt.instances.empty()){rt.instances=std::move(instances);rt.scene_dirty=true;}
 if(pending.empty()){
  rt.source_key=source;rt.source_valid=true;
  if(rt.scene_dirty)report("reduced");
  return S_OK;
 }
 rt.statics_busy_frame=native_frame_index_; // a build (GPU work) follows
 // The arenas hold the pending records at their end. When they do not (or
 // dropped chunks left more than half of them unused), the kept chunks'
 // ranges are copied, in order and without gaps, into arenas sized by the
 // kept and pending demand (a quarter more), and their record tables are
 // rebased: a bottom-level structure does not read its build inputs after the
 // build (D3D12 BuildRaytracingAccelerationStructure), so no kept chunk is
 // built again. Rebuilding every chunk here (the 1758 structures, 1.75M
 // triangles of a Suslanger walk, when a few sun-bake store records changed)
 // stalled the frame 380-630 ms each time on this PC (cand-1122 1122b run).
 const auto arena_overflow=[&]{
  return rt.vertex_used+pending_vertices>rt.vertex_capacity||rt.triangle_used+pending_triangles>rt.triangle_capacity||
   rt.vertex_dropped>rt.vertex_capacity/2||rt.triangle_dropped>rt.triangle_capacity/2;};
 if(rt.vertices&&rt.indices&&!rt.chunks.empty()&&arena_overflow()){
  std::uint64_t kept_vertices=0,kept_triangles=0;
  for(const auto& chunk:rt.chunks){kept_vertices+=chunk.vertices;kept_triangles+=chunk.triangles;}
  const std::uint64_t want_vertices=kept_vertices+pending_vertices,want_triangles=kept_triangles+pending_triangles;
  const std::uint64_t vertices=want_vertices+want_vertices/4+4096,triangles=want_triangles+want_triangles/4+4096;
  D3D12BufferRef moved_vertices,moved_indices;
  HRESULT hr=vertices*kRtVertexBytes>0xF0000000ull||triangles*kRtTriangleBytes>0xF0000000ull?E_OUTOFMEMORY:S_OK;
  if(SUCCEEDED(hr))hr=d3d12_->create_buffer(vertices*kRtVertexBytes,0,kD3D12BufferUnordered,moved_vertices);
  if(SUCCEEDED(hr))hr=d3d12_->create_buffer(triangles*kRtTriangleBytes,0,kD3D12BufferUnordered,moved_indices);
  // Chunks sit in the arenas in build order: a run of chunks adjacent there
  // moves with one copy.
  UINT vertex_cursor=0,triangle_cursor=0;
  std::uint64_t run_vertex=0,run_triangle=0,run_vertices=0,run_triangles=0;UINT run_to_vertex=0,run_to_triangle=0;
  const auto flush_run=[&]{
   if(SUCCEEDED(hr)&&run_vertices)hr=d3d12_->copy_buffer(*moved_vertices,*rt.vertices,run_vertices*kRtVertexBytes,
    UINT64(run_to_vertex)*kRtVertexBytes,run_vertex*kRtVertexBytes);
   if(SUCCEEDED(hr)&&run_triangles)hr=d3d12_->copy_buffer(*moved_indices,*rt.indices,run_triangles*kRtTriangleBytes,
    UINT64(run_to_triangle)*kRtTriangleBytes,run_triangle*kRtTriangleBytes);
   run_vertices=run_triangles=0;
  };
  for(auto& chunk:rt.chunks){
   if(FAILED(hr))break;
   if(chunk.table.size()<kRtRecordWords)continue;
   const UINT from_vertex=chunk.table[0],from_triangle=chunk.table[1];
   if(run_vertices+run_triangles==0||run_vertex+run_vertices!=from_vertex||run_triangle+run_triangles!=from_triangle){
    flush_run();
    run_vertex=from_vertex;run_triangle=from_triangle;run_to_vertex=vertex_cursor;run_to_triangle=triangle_cursor;
   }
   run_vertices+=chunk.vertices;run_triangles+=chunk.triangles;
   for(std::size_t word=0;word+1<chunk.table.size();word+=kRtRecordWords){
    chunk.table[word]=chunk.table[word]-from_vertex+vertex_cursor;chunk.table[word+1]=chunk.table[word+1]-from_triangle+triangle_cursor;
   }
   vertex_cursor+=chunk.vertices;triangle_cursor+=chunk.triangles;
  }
  flush_run();
  if(SUCCEEDED(hr)){
   rt.vertices=std::move(moved_vertices);rt.indices=std::move(moved_indices);
   rt.vertex_capacity=UINT(vertices);rt.triangle_capacity=UINT(triangles);
   rt.vertex_used=vertex_cursor;rt.triangle_used=triangle_cursor;rt.vertex_dropped=rt.triangle_dropped=0;
   rt.scene_dirty=true;
   arena_moved=true;
  }else{
   // Some tables may be rebased already: nothing of the old arenas is kept,
   // the whole build below runs (and names its own failure).
   rt.chunks.clear();rt.members.clear();rt.scene_dirty=true;
   rt.vertices.reset();rt.indices.reset();
   rt.vertex_capacity=rt.vertex_used=rt.vertex_dropped=rt.triangle_capacity=rt.triangle_used=rt.triangle_dropped=0;
  }
 }
 // No arenas yet (or the move failed above): everything is built into arenas
 // sized by the whole demand (a quarter more).
 if(!rt.vertices||!rt.indices||arena_overflow()){
  rt.chunks.clear();rt.members.clear();rt.scene_dirty=true;
  rt.vertices.reset();rt.indices.reset();
  rt.vertex_capacity=rt.vertex_used=rt.vertex_dropped=rt.triangle_capacity=rt.triangle_used=rt.triangle_dropped=0;
  pending=wanted;demand(pending,pending_vertices,pending_triangles);
  const std::uint64_t vertices=pending_vertices+pending_vertices/4+4096,triangles=pending_triangles+pending_triangles/4+4096;
  // Raw views address bytes with 32 bits.
  if(vertices*kRtVertexBytes>0xF0000000ull||triangles*kRtTriangleBytes>0xF0000000ull){
   rt.source_key=source;rt.source_valid=true;
   bridge_append_probe_event("rt_omitted","static casters: more geometry than one arena addresses; the scene holds none of them");
   return E_OUTOFMEMORY;
  }
  HRESULT hr=d3d12_->create_buffer(vertices*kRtVertexBytes,0,kD3D12BufferUnordered,rt.vertices);
  if(SUCCEEDED(hr))hr=d3d12_->create_buffer(triangles*kRtTriangleBytes,0,kD3D12BufferUnordered,rt.indices);
  if(FAILED(hr)){
   rt.vertices.reset();rt.indices.reset();rt.source_key=source;rt.source_valid=true;
   bridge_append_probe_event("rt_omitted","static casters: their arenas could not be created; the scene holds none of them");
   return hr;
  }
  rt.vertex_capacity=UINT(vertices);rt.triangle_capacity=UINT(triangles);
 }
 // Jobs in page order, cut into chunks, and the dispatch runs over them.
 std::sort(pending.begin(),pending.end(),[](const Wanted& a,const Wanted& b){
  return a.terrain!=b.terrain?b.terrain:a.page!=b.page?a.page<b.page:a.key<b.key;});
 constexpr std::uint32_t kChunkTriangles=262144,kChunkRecords=4096,kRunThreads=4000000;
 struct Run{UINT first,count,threads,page;};
 struct Cut{std::size_t first,count;bool terrain,instanced;};
 std::vector<RaytracingRepackJob> jobs(pending.size());
 std::vector<Run> runs;std::vector<Cut> cuts;
 UINT vertex_cursor=rt.vertex_used,triangle_cursor=rt.triangle_used,chunk_triangles=0;
 for(std::size_t n=0;n<pending.size();++n){
  const auto& draw=native_instance_records_[pending[n].record].command;
  const auto& instance=draw.instance;
  const UINT triangles=draw.index_count/3,threads=(std::max)(triangles,draw.component_count);
  if(cuts.empty()||pending[n].instanced||cuts.back().instanced||cuts.back().terrain!=pending[n].terrain||cuts.back().count>=kChunkRecords||
     chunk_triangles+triangles>kChunkTriangles){
   cuts.push_back({n,0,pending[n].terrain,pending[n].instanced});chunk_triangles=0;}
  const bool new_chunk=cuts.back().count==0;
  ++cuts.back().count;chunk_triangles+=triangles;
  if(runs.empty()||new_chunk||runs.back().page!=pending[n].page||runs.back().threads+threads>kRunThreads)runs.push_back({UINT(n),0,0,pending[n].page});
  auto& job=jobs[n];
  std::memcpy(job.row,&instance.object_to_world,sizeof(job.row));
  if(pending[n].instanced){ // object space: its instances carry the transform
   std::memset(job.row,0,sizeof(job.row));job.row[0][0]=job.row[1][1]=job.row[2][2]=1.f;
  }
  job.position_offset=instance.geometry_offsets[0];job.component_offset=instance.geometry_offsets[3];
  job.uv_offset=instance.geometry_offsets[2];job.index_offset=draw.mesh->index_offset;
  job.shape_count=instance.shape_count;job.position_groups=instance.position_groups;
  job.component_count=draw.component_count;job.triangle_count=triangles;
  std::copy(std::begin(instance.shape),std::end(instance.shape),job.shape);
  job.vertex_base=vertex_cursor;job.triangle_base=triangle_cursor;
  job.uv_shift[0]=instance.uv_shift[0];job.uv_shift[1]=instance.uv_shift[1];
  job.first_thread=runs.back().threads;
  job.terrain_vertex_offset=pending[n].terrain?instance.terrain_vertex_offset:UINT32_MAX;
  job.padding[0]=pending[n].world?draw.world_base:UINT32_MAX;job.padding[1]=job.padding[2]=0;
  ++runs.back().count;runs.back().threads+=threads;
  vertex_cursor+=draw.component_count;triangle_cursor+=triangles;
 }
 D3D12BufferRef job_buffer;
 const UINT64 job_bytes=UINT64(jobs.size())*sizeof(RaytracingRepackJob);
 HRESULT hr=d3d12_->create_buffer(job_bytes,sizeof(RaytracingRepackJob),0,job_buffer);
 if(SUCCEEDED(hr))hr=d3d12_->write_buffer_bulk(*job_buffer,0,jobs.data(),job_bytes);
 if(FAILED(hr)){rt.source_key=source;rt.source_valid=true;bridge_append_probe_event("rt_omitted","static casters: the repack jobs could not be uploaded");return hr;}
 transition_profile_pass("rt_scene");
 d3d12_->set_resource(40,job_buffer.get());d3d12_->set_resource(65,native12_.world_vertices.get());
 d3d12_->set_unordered(0,rt.vertices.get());d3d12_->set_unordered(1,rt.indices.get());
 for(const auto& run:runs){
  const UINT constants[4]={run.first,run.count,run.threads,0};
  d3d12_->set_constants(12,constants,sizeof(constants));
  d3d12_->set_resource(60,native_geometry_pages_[run.page].d3d12.get());
  hr=d3d12_->dispatch(rt.programs[kRtRepack],(run.threads+63u)/64u,1,1);
  if(hr!=S_OK)break;
 }
 d3d12_->set_resource(40,static_cast<const D3D12Buffer*>(nullptr));d3d12_->set_resource(60,static_cast<const D3D12Buffer*>(nullptr));
 d3d12_->set_resource(65,static_cast<const D3D12Buffer*>(nullptr));
 d3d12_->set_unordered(0,static_cast<const D3D12Buffer*>(nullptr));d3d12_->set_unordered(1,static_cast<const D3D12Buffer*>(nullptr));
 if(d3d12_not_done(hr))return S_FALSE; // nothing kept: the arenas' used ends did not move
 if(FAILED(hr)){rt.source_key=source;rt.source_valid=true;bridge_append_probe_event("rt_omitted","static casters: their repack dispatch failed");return hr;}
 // One bottom-level structure a chunk, one geometry a record.
 std::vector<D3D12RaytracingGeometry> geometries;
 for(const auto& cut:cuts){
  RaytracingD3D12::Chunk chunk;
  chunk.terrain=cut.terrain;chunk.instanced=cut.instanced;
  geometries.clear();
  for(std::size_t n=cut.first;n<cut.first+cut.count;++n){
   const auto& draw=native_instance_records_[pending[n].record].command;
   const FixedState& state=native_state_buckets_[draw.state_bucket].state;
   // An instanced mesh's own flag is opaque: each instance forces its own.
   bool cutout=false;std::uint32_t reference_bits=0;
   if(!cut.terrain&&!cut.instanced)raytracing_record_cutout(draw,state,cutout,reference_bits);
   D3D12RaytracingGeometry geometry;
   geometry.vertex_offset=UINT64(jobs[n].vertex_base)*kRtVertexBytes;geometry.vertex_count=jobs[n].component_count;
   geometry.index_offset=UINT64(jobs[n].triangle_base)*kRtTriangleBytes;geometry.index_count=jobs[n].triangle_count*3u;
   geometry.opaque=!cutout;
   geometries.push_back(geometry);
   chunk.keys.push_back(pending[n].key);
   chunk.table.insert(chunk.table.end(),{jobs[n].vertex_base,jobs[n].triangle_base,cutout?draw.instance.padding[0]:0u,cutout?reference_bits:0u});
   if(draw.texture12)chunk.textures.push_back(draw.texture12);
   {std::uint32_t shade[kRtShadeWords];raytracing_shade_words(draw,cut.terrain,shade);chunk.shade.insert(chunk.shade.end(),shade,shade+kRtShadeWords);}
   chunk.vertices+=jobs[n].component_count;chunk.triangles+=jobs[n].triangle_count;
  }
  hr=d3d12_->build_raytracing_bottom(*rt.vertices,kRtVertexBytes,*rt.indices,geometries.data(),UINT(geometries.size()),chunk.bottom);
  if(FAILED(hr)){
   rt.source_key=source;rt.source_valid=true;
   bridge_append_probe_event("rt_omitted","static casters: a bottom-level structure could not be built (see host.log); its records cast no traced shadow");
   report("failed");
   return hr;
  }
  for(const auto key:chunk.keys)rt.members[key]=1;
  rt.vertex_used+=chunk.vertices;rt.triangle_used+=chunk.triangles;
  rt.chunks.push_back(std::move(chunk));rt.scene_dirty=true;
 }
 rt.source_key=source;rt.source_valid=true;
 report("built");
 return S_OK;
}

// The ray tracing scene, once a world frame (flush_native_d3d12, before the
// flush binds anything). Off (rt_sun_shadows and rt_debug_view both), it
// reads the options and returns, releasing a scene an earlier frame made.
void Renderer::update_raytracing_scene_d3d12() noexcept try{
 auto& rt=rt12_;
 if(rt.frame==native_frame_index_)return;
 const auto& config=debug_overlay_.config();
 const bool shadows=configured_boolean(config,"rt_sun_shadows",false);
 if(!rt_scene_wanted(config)&&configured_integer(config,"rt_debug_view",0)<=0){
  if(rt.scene||rt.terrain||rt.vertices||rt.map!=UINT64_MAX)release_raytracing_d3d12();
  return;
 }
 if(const char* unavailable=d3d12_->raytracing_unavailable()){
  if(!rt.reported){rt.reported=true;bridge_append_probe_event("rt_omitted",unavailable);}
  return;
 }
 if(!world_d3d12_)return;
 rt.frame=native_frame_index_;
 const auto terrain=ambient_terrain_.lock();
 if(!terrain){release_raytracing_d3d12();return;}
 if(rt.programs.empty())append_raytracing_shader_programs(rt.programs);
 if(rt.programs.size()<kRtPrograms)return;
 if(rt.map!=ambient_map_generation_){
  // One attempt a map (the terrain asset is immutable; liquids and their
  // level changes are not in it): a failed build is not retried every frame.
  rt.terrain.reset();rt.terrain_vertices.reset();rt.terrain_indices.reset();rt.map=ambient_map_generation_;rt.scene_dirty=true;
  std::vector<float> positions;std::vector<std::uint32_t> indices;
  const std::int64_t begin=perf_now();
  if(!raytracing_terrain_mesh(*terrain,positions,indices))
   bridge_append_probe_event("rt_omitted","terrain: the active map has no base terrain triangles (or a non-finite vertex)");
  else{
   transition_profile_pass("rt_scene");
   D3D12BufferRef vertex_buffer,index_buffer;
   const UINT64 vertex_bytes=UINT64(positions.size())*sizeof(float),index_bytes=UINT64(indices.size())*sizeof(std::uint32_t);
   HRESULT built=d3d12_->create_buffer(vertex_bytes,0,0,vertex_buffer);
   if(SUCCEEDED(built))built=d3d12_->write_buffer_bulk(*vertex_buffer,0,positions.data(),vertex_bytes);
   if(SUCCEEDED(built))built=d3d12_->create_buffer(index_bytes,0,0,index_buffer);
   if(SUCCEEDED(built))built=d3d12_->write_buffer_bulk(*index_buffer,0,indices.data(),index_bytes);
   if(SUCCEEDED(built)){
    D3D12RaytracingGeometry geometry;
    geometry.vertex_count=UINT(positions.size()/3);geometry.index_count=UINT(indices.size());
    built=d3d12_->build_raytracing_bottom(*vertex_buffer,12,*index_buffer,&geometry,1,rt.terrain);
   }
   // Kept for the hits' normals (stage 2b).
   if(SUCCEEDED(built)){rt.terrain_vertices=vertex_buffer;rt.terrain_indices=index_buffer;}
   char text[200];
   if(FAILED(built)){
    rt.terrain.reset();
    std::snprintf(text,sizeof(text),"terrain: its structure could not be built (0x%08lX; see host.log)",static_cast<unsigned long>(built));
    bridge_append_probe_event("rt_omitted",text);
   }else{
    std::snprintf(text,sizeof(text),"map=%s triangles=%zu vertices=%zu cpu_ms=%.2f",ambient_map_name_.c_str(),indices.size()/3,positions.size()/3,
     double(perf_now()-begin)*1000.0/double(perf_frequency()));
    bridge_append_probe_event("rt_terrain_scene",text);
   }
  }
 }
 // Structures the executor compacted moved: the top level names them again.
 if(d3d12_->compact_raytracing()){rt.scene_dirty=true;rt.statics_busy_frame=native_frame_index_;}
 (void)update_raytracing_statics_d3d12(); // its failures are named; the scene keeps what was built
 std::size_t records=0;
 for(const auto& chunk:rt.chunks)records+=chunk.keys.size();
 // The header (native_shader.h rtSunRecords), new each frame.
 std::uint32_t header[kRtHeaderWords]{};
 {const float radius=(std::clamp)(configured_floating_point(config,"rt_sun_softness",.5f),0.f,5.f)*3.14159265f/180.f;
  const float values[4]={std::tan(radius),(std::clamp)(configured_floating_point(config,"rt_sun_history",.9f),0.f,.98f),.02f,.0015f};
  std::memcpy(header,values,sizeof(values));
  const float card_mip_distance=20.f;
  // The frame (the shader takes its low 10 bits); bit 16: rt_debug_view 4.
  header[4]=(std::uint32_t(native_frame_index_)&1023u)|(configured_integer(config,"rt_debug_view",0)==4?0x10000u:0u);
  header[5]=std::uint32_t((std::clamp)(configured_integer(config,"rt_sun_rays",1),1,8));
  std::memcpy(&header[6],&card_mip_distance,sizeof(float));
  header[7]=std::uint32_t(records);}
 // The units of the frame before, from their posed world vertices: every
 // record still holding the range it was drawn from (a released or
 // re-ranged record is left out; its mesh is alive while its record is).
 std::vector<D3D12RaytracingGeometry> figure_geometries;
 std::vector<std::uint32_t> figure_shade;std::vector<D3D12TextureRef> figure_textures;
 {auto drawn=std::move(rt.figures_next);rt.figures_next.clear();
  std::sort(drawn.begin(),drawn.end(),[](const auto& a,const auto& b){return a.record<b.record;});
  drawn.erase(std::unique(drawn.begin(),drawn.end(),[](const auto& a,const auto& b){return a.record==b.record;}),drawn.end());
  const UINT64 world_vertices=native12_.world_vertices?d3d12_->buffer_bytes(*native12_.world_vertices)/sizeof(NativeWorldVertexGpu):0;
  for(const auto& figure:drawn){
   if(figure.record>=native_instance_records_.size())continue;
   const auto& record=native_instance_records_[figure.record];
   const auto& draw=record.command;
   if(!record.occupied||!draw.mesh||draw.mesh!=figure.mesh||draw.world_base!=figure.world_base||draw.world_generation!=figure.world_generation||
      !draw.instance.world_geometry||(draw.instance.world_geometry&native_world_object_space)||
      draw.geometry_page>=native_geometry_pages_.size()||!native_geometry_pages_[draw.geometry_page].d3d12||
      draw.index_count<3||!draw.component_count||UINT64(draw.world_base)+draw.component_count>world_vertices)continue;
   D3D12RaytracingGeometry geometry;
   geometry.vertex_offset=UINT64(draw.world_base)*sizeof(NativeWorldVertexGpu);geometry.vertex_count=draw.component_count;
   geometry.index_buffer=native_geometry_pages_[draw.geometry_page].d3d12.get();
   geometry.index_offset=draw.mesh->index_offset;geometry.index_count=draw.index_count-draw.index_count%3u;
   figure_geometries.push_back(geometry);
   std::uint32_t shade[kRtShadeWords];raytracing_shade_words(draw,false,shade);
   figure_shade.insert(figure_shade.end(),shade,shade+kRtShadeWords);
   if(draw.texture12)figure_textures.push_back(draw.texture12);
  }}
 bool figures=false;
 if(!figure_geometries.empty()){
  transition_profile_pass("rt_figures");
  figures=SUCCEEDED(d3d12_->build_raytracing_bottom(*native12_.world_vertices,UINT(sizeof(NativeWorldVertexGpu)),*native12_.world_vertices,
   figure_geometries.data(),UINT(figure_geometries.size()),rt.figure_bottom,true));
  if(!figures&&!rt.omitted_logged){rt.omitted_logged=true;bridge_append_probe_event("rt_omitted","units: their structure could not be built (see host.log); they cast no traced shadow");}
  // Their shading words by geometry, new each frame (without the buffer a unit hit reads zeros: white, no profile).
  const UINT64 shade_bytes=UINT64(figure_shade.size())*sizeof(std::uint32_t);
  if(figures&&(!rt.figure_shade||d3d12_->buffer_bytes(*rt.figure_shade)<shade_bytes)){
   rt.figure_shade.reset();
   if(FAILED(d3d12_->create_buffer(shade_bytes+shade_bytes/2,0,0,rt.figure_shade)))rt.figure_shade.reset();
  }
  if(figures&&rt.figure_shade&&SUCCEEDED(d3d12_->write_buffer(*rt.figure_shade,0,figure_shade.data(),shade_bytes)))rt.figure_textures=std::move(figure_textures);
  else rt.figure_shade.reset();
 }
 const bool table_changed=rt.scene_dirty||!rt.records;
 if(table_changed||figures||rt.figures_in_scene){
  rt.scene_dirty=false;
  std::vector<D3D12RaytracingInstance> instances;
  std::vector<std::uint32_t> table(header,header+kRtHeaderWords),shade_table;
  if(rt.terrain)instances.push_back({rt.terrain.get(),kRtTerrainInstance,false,1u}); // opaque: only a hit's shading reads its id
  std::unordered_map<std::uint64_t,const RaytracingD3D12::Chunk*> meshes;
  for(const auto& chunk:rt.chunks){
   if(chunk.instanced){if(!chunk.keys.empty()&&chunk.table.size()>=kRtRecordWords)meshes.emplace(chunk.keys[0],&chunk);continue;}
   instances.push_back({chunk.bottom.get(),UINT((table.size()-kRtHeaderWords)/kRtRecordWords),!chunk.terrain,chunk.terrain?1u:2u});
   table.insert(table.end(),chunk.table.begin(),chunk.table.end());
   shade_table.insert(shade_table.end(),chunk.shade.begin(),chunk.shade.end());
   shade_table.resize(table.size()-kRtHeaderWords); // one entry a record, whatever the chunk held
  }
  // rt_instancing: one instance and one record a static caster, its mesh's place in the arenas.
  for(const auto& placed:rt.instances){
   const auto mesh=meshes.find(placed.mesh);
   if(mesh==meshes.end())continue;
   D3D12RaytracingInstance instance{mesh->second->bottom.get(),UINT((table.size()-kRtHeaderWords)/kRtRecordWords),true,2u};
   instance.placed=true;instance.opacity=placed.cutout?2u:1u;
   std::memcpy(instance.transform,placed.transform,sizeof(instance.transform));
   instances.push_back(instance);
   table.insert(table.end(),{mesh->second->table[0],mesh->second->table[1],placed.texture,placed.alpha_reference});
   shade_table.resize(table.size()-kRtHeaderWords-kRtRecordWords);
   shade_table.insert(shade_table.end(),placed.shade,placed.shade+kRtShadeWords);
  }
  if(figures)instances.push_back({rt.figure_bottom.get(),kRtFigureInstance,true,4u}); // opaque: only a hit's shading reads its id
  rt.figures_in_scene=figures;
  HRESULT hr=S_OK;
  if(table_changed){
   table.resize(table.size()+kRtRecordWords); // never an empty table
   rt.records.reset();
   const UINT64 table_bytes=UINT64(table.size())*sizeof(std::uint32_t);
   hr=d3d12_->create_buffer(table_bytes,0,0,rt.records);
   if(SUCCEEDED(hr))hr=d3d12_->write_buffer_bulk(*rt.records,0,table.data(),table_bytes);
   // The hits' shading words beside it (no header).
   shade_table.resize(table.size()-kRtHeaderWords);rt.shade.reset();
   const UINT64 shade_bytes=UINT64(shade_table.size())*sizeof(std::uint32_t);
   if(SUCCEEDED(hr))hr=d3d12_->create_buffer(shade_bytes,0,0,rt.shade);
   if(SUCCEEDED(hr))hr=d3d12_->write_buffer_bulk(*rt.shade,0,shade_table.data(),shade_bytes);
  }else hr=d3d12_->write_buffer(*rt.records,0,header,sizeof(header));
  if(SUCCEEDED(hr)){
   if(instances.empty())rt.scene.reset();
   else{
    // Built again in place every frame while units are in it.
    transition_profile_pass(table_changed?"rt_scene":"rt_figures");
    hr=d3d12_->build_raytracing_top(instances.data(),UINT(instances.size()),rt.scene,true);
   }
  }
  if(FAILED(hr)){
   rt.scene.reset();rt.records.reset();rt.shade.reset();rt.figures_in_scene=false;
   if(!rt.omitted_logged){rt.omitted_logged=true;bridge_append_probe_event("rt_omitted","the scene's top level or record table could not be made (see host.log); nothing traces");}
   return;
  }
 }else if(FAILED(d3d12_->write_buffer(*rt.records,0,header,sizeof(header))))return;
 if(shadows&&rt.scene&&rt.records)rt.shadow_frame=native_frame_index_;
}catch(const std::bad_alloc&){
 release_raytracing_d3d12();
 bridge_append_probe_event("rt_omitted","out of memory building the ray tracing scene");
}

// rt_debug_view: one full-screen draw over the world colour, through the
// scene this frame's flush brought up to date (terrain and static casters;
// its rays treat every triangle as opaque).
void Renderer::rt_term_view_d3d12(bool after_upscaler) noexcept{
 const int mode=configured_integer(debug_overlay_.config(),"rt_term_view",0);
 auto& up=upscale12_;auto& rt=rt12_;
 // Modes 11 and 12 draw over the upscaled output only (the world image stays the upscaler's input).
 if(mode<=0||(mode>=11)!=after_upscaler||hit_test_active_||!world_d3d12_||rt.programs.size()<kRtPrograms)return;
 if(!guide_frame_d3d12()){d3d12_->omit("rt_term_view (no guide images this frame: it needs a ray traced option on)",false);return;}
 const float scale_x=after_upscaler?float(width_)/float((std::max)(output_width_,1u)):1.f,scale_y=after_upscaler?float(height_)/float((std::max)(output_height_,1u)):1.f;
 std::uint32_t control[4]{std::uint32_t(mode),ao12_.divisor,0,0};
 std::memcpy(&control[2],&scale_x,4);std::memcpy(&control[3],&scale_y,4);
 const bool object_motion=temporal_upscaler()&&up.object_motion&&up.object_depth;
 transition_profile_pass("rt_debug_view");
 d3d12_->clear_slots();
 d3d12_->set_constants(1,control,sizeof(control));
 d3d12_->set_depth_resource(0,world_d3d12_.get());
 d3d12_->set_resource(1,up.guide_normal.get());d3d12_->set_resource(2,up.guide_diffuse.get());d3d12_->set_resource(3,up.guide_specular.get());
 d3d12_->set_resource(4,shadow_mask12_.get());
 d3d12_->set_resource(5,rt_indirect12_.factor_frame==native_frame_index_?rt_indirect12_.factor.get():nullptr);
 d3d12_->set_resource(6,ao12_.frame==native_frame_index_?ao12_.filtered.get():nullptr);
 d3d12_->set_resource(7,object_motion?up.object_motion.get():nullptr);d3d12_->set_resource(8,object_motion?up.object_depth.get():nullptr);
 D3D12Texture* target=world_d3d12_.get();
 if(after_upscaler)d3d12_->set_target(nullptr); // the output (present_upscaled_d3d12's target)
 else d3d12_->set_targets(&target,1,nullptr);
 const HRESULT drawn=d3d12_->draw(rt.programs[kRtDebugVertex],kRtTermViewPixel,post_state(false,false),nullptr,3);
 d3d12_->clear_slots();
 if(!after_upscaler)bind_world_targets_d3d12();
 if(drawn==S_OK)d3d12_->count("ray tracing term views");
 else d3d12_->omit(drawn==S_FALSE?"rt_term_view (pipeline not ready)":"rt_term_view (draw failed)",false);
}

void Renderer::raytracing_debug_view_d3d12() noexcept{
 auto& rt=rt12_;
 const int mode=configured_integer(debug_overlay_.config(),"rt_debug_view",0);
 const bool hits=mode>=5&&mode<=7; // stage 2b: what a ray returns at its hit
 if(mode<=0||(mode>3&&!hits)||hit_test_active_||!world_d3d12_||rt.frame!=native_frame_index_||!rt.scene||rt.programs.size()<kRtPrograms)return;
 if(hits&&(!rt.records||!rt.shade))return; // without static casters there are no arenas (never read then)
 const auto scene=bridge_scene_world_to_clip_snapshot();
 struct{float clip_to_world[16];float sun[4];float size[4];std::uint32_t control[4];float ambient[4];float sun_diffuse[4];}constants{};
 for(int axis=0;axis<3;++axis){constants.ambient[axis]=native_world_lighting_.ambient[axis];constants.sun_diffuse[axis]=native_world_lighting_.sunlight_diffuse[axis];}
 UINT width=0,height=0,levels=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
 if(!scene.valid||!ambient_occlusion_invert(scene.world_to_clip.data(),constants.clip_to_world)||
    !d3d12_->texture_info(*world_d3d12_,width,height,levels,format))return;
 // Toward the sun (the bridge's lighting vector), unit length; w: the ray's reach in metres.
 const auto& sun=native_world_lighting_.sunlight_direction;
 const float length=std::sqrt(sun[0]*sun[0]+sun[1]*sun[1]+sun[2]*sun[2]);
 if(!(length>1e-6f))return;
 for(int axis=0;axis<3;++axis)constants.sun[axis]=sun[axis]/length;
 constants.sun[3]=100000.f;
 constants.size[0]=float(width);constants.size[1]=float(height);constants.size[2]=1.f/width;constants.size[3]=1.f/height;
 constants.control[0]=std::uint32_t(mode);constants.control[1]=shadow_mask12_?1u:0u;
 transition_profile_pass("rt_debug_view");
 d3d12_->clear_slots();
 d3d12_->set_constants(1,&constants,sizeof(constants));
 d3d12_->set_depth_resource(0,world_d3d12_.get());
 d3d12_->set_resource(1,rt.scene.get());
 d3d12_->set_resource(2,mode==3?shadow_mask12_.get():nullptr);
 if(hits){
  d3d12_->set_resource(94,rt.scene.get());d3d12_->set_resource(95,rt.vertices.get());
  d3d12_->set_resource(96,rt.terrain_vertices.get());d3d12_->set_resource(97,rt.terrain_indices.get());
  d3d12_->set_resource(99,rt.indices.get());d3d12_->set_resource(113,rt.records.get());
  d3d12_->set_resource(114,rt.shade.get());d3d12_->set_resource(115,rt.figures_in_scene?rt.figure_shade.get():nullptr);
  d3d12_->set_resource(kTerrain2CacheCoarseSlotD3D12,terrain2_cache_on_?terrain2_cache_coarse12_.get():nullptr);
  d3d12_->set_resource(kTerrain2CacheParamsSlotD3D12,terrain2_cache_on_?terrain2_cache_params12_.get():nullptr);
  d3d12_->set_constants(5,native12_.point_light_constants.data(),sizeof(native12_.point_light_constants));
  d3d12_->set_resource(17,native_point_lights_.empty()?nullptr:native12_.point_light_grid.get());
  d3d12_->set_resource(18,native_point_lights_.empty()?nullptr:native12_.point_lights.get());
  D3D11_SAMPLER_DESC wrap{};wrap.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  wrap.AddressU=wrap.AddressV=wrap.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;wrap.MaxLOD=D3D11_FLOAT32_MAX;
  d3d12_->set_sampler(0,wrap);wrap.MaxLOD=0;d3d12_->set_sampler(15,wrap);
 }
 D3D12Texture* target=world_d3d12_.get();
 d3d12_->set_targets(&target,1,nullptr); // no depth: the world depth is read
 const HRESULT drawn=d3d12_->draw(rt.programs[kRtDebugVertex],rt.programs[hits?kRtHitDebugPixel:kRtDebugPixel],post_state(false,false),nullptr,3);
 d3d12_->clear_slots();
 bind_world_targets_d3d12();
 if(drawn==S_OK){profiler_.add_draw(1,1);profiler_.add_batch();d3d12_->count("ray tracing debug views");}
 else if(drawn==S_FALSE)d3d12_->omit("rt_debug_view (pipeline not ready)",false);
 else if(FAILED(drawn))d3d12_->omit("rt_debug_view (draw failed)",false);
}

// Ground marks after the world pass test against the pre-water depth
// snapshot on frames the water reflected (composite_world_d3d12).
bool Renderer::ground_marks_under_water_d3d12() const noexcept{
 return world_d3d12_bound_&&world_d3d12_&&water12_.depth&&water_capture_ready_&&water_capture_frame_==native_frame_index_;
}

// The composite_world effects' shared state on D3D12 (D3D11: their own
// blend/raster objects, rtv_ alone with depth_srv_ at t2, or world_dsv_
// read-only for the waterfall shells; bind_world_fog's b10/t11/s7 and the
// sky probe).
D3D12NativeState Renderer::world_effect_state_d3d12(bool depth_test) const noexcept{
 D3D12NativeState state;
 state.raster.FillMode=D3D11_FILL_SOLID;state.raster.CullMode=D3D11_CULL_NONE;state.raster.DepthClipEnable=TRUE;
 auto& blend=state.custom_blend; // premultiplied: ONE, INV_SRC_ALPHA for colour and alpha
 blend.custom=1;blend.source=blend.source_alpha=D3D11_BLEND_ONE;
 blend.destination=blend.destination_alpha=D3D11_BLEND_INV_SRC_ALPHA;
 if(depth_test){state.depth.DepthEnable=TRUE;state.depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;state.depth.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;}
 return state;
}
void Renderer::bind_world_effect_d3d12(bool depth_test){
 D3D12Texture* world=world_d3d12_.get();
 d3d12_->clear_slots();
 d3d12_->set_targets(&world,1,depth_test?world:nullptr);
 if(!depth_test)d3d12_->set_depth_resource(2,world);
 d3d12_->set_constants(10,&fog_frame_d3d12_,sizeof(fog_frame_d3d12_));
 bind_world_fog_volume_d3d12(); // t11 while this frame's fog volume ran
 d3d12_->set_resource(kSkyCloudShadowSlotD3D12,sky_inputs_active_?sky12_.shadow.get():nullptr);
 d3d12_->set_resource(kSkyProbeSlotD3D12,sky_inputs_active_?sky12_.probe[sky_index_].get():nullptr);
 D3D11_SAMPLER_DESC linear{};linear.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
 linear.AddressU=linear.AddressV=linear.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;linear.MaxLOD=D3D11_FLOAT32_MAX;
 d3d12_->set_sampler(7,linear);
}

// composite_world on the D3D12 backend: the world's queue, then the world
// target copied into the back buffer (D3D11 scene_copy). The steps D3D11
// runs around that copy are named omitted while they are configured on.
HRESULT Renderer::composite_world_d3d12(){
 // Kept world materials never outlive the world pass (the frame's static
 // flush draws them; this only meets a frame that ended its world early).
 if(!deferred_world12_.empty()){const HRESULT resumed=resume_deferred_world_d3d12(true);if(FAILED(resumed))d3d12_->omit("world material (kept for the static flush; failed)",true);}
 const HRESULT world=flush_native();
 // This frame's shadow history: the world depth its visibility was drawn
 // with, for the next frame's reprojection (D3D11 commits at the world's
 // depth clear and end_scene, before composite_world). After the flush,
 // so the depth includes every batch that wrote visibility.
 if(world_d3d12_bound_)commit_shadow_history();
 // Neighbour-map liquids last in the world pass (composite_world), after the
 // figures the game submitted.
 if(neighbour_liquids_pending_){
  neighbour_liquids_pending_=false;
  if(neighbour_liquids_frame_index_==native_frame_index_&&set_native_world_clip(neighbour_liquids_clip_)){
   const HRESULT liquids=render_neighbour_maps(neighbour_liquids_state_,neighbour_liquids_clip_,true);
   if(FAILED(liquids))d3d12_->omit("neighbour liquids (their flush failed)",false);
  }
 }
 // Ground marks held back from the world pass (selection, prints) draw over
 // the finished water, as composite_world draws them: on frames the water
 // reflected, against its pre-water depth snapshot (marks on the bed stay
 // visible, unrefracted).
 if(!deferred_ground_marks_.empty()){
  auto marks=std::move(deferred_ground_marks_);deferred_ground_marks_.clear();
  const bool under_water=ground_marks_under_water_d3d12();
  if(under_water){D3D12Texture* world=world_d3d12_.get();d3d12_->set_targets(&world,1,water12_.depth.get());}
  drawing_ground_marks_=true;
  for(auto& mark:marks)if(FAILED(mark())){d3d12_->omit("ground marks (a draw failed)",false);break;}
  drawing_ground_marks_=false;
  if(under_water)bind_world_targets_d3d12();
 }
 // The occlusion pyramid describes the world pass only (composite_world's
 // rule): the interface 3D after this point (HUD spell and skill icons,
 // previews) must not be culled against the world's depth.
 native_hiz_frame_=UINT64_MAX;native_world_clip_dirty_=true;
 // World-space effects precede the reflection history and post-processing.
 if(world_d3d12_bound_&&world_d3d12_){
  // AO needs the finished world depth and ambient share and precedes SMAA
  // and every interface pass (the world targets bound again after it).
  // rt_ambient_occlusion: the occlusion comes from the indirect light's
  // bounce rays, so it follows those passes (it removes a share of the
  // ambient light, they add the bounce light: the order does not change the
  // sum). A frame those rays did not run (no scene, a pipeline still
  // compiling) keeps the screen-space passes, counted.
  const auto ambient_occlusion=[&](const D3D12Texture* traced){
   transition_profile_pass(traced?"rt_ambient_occlusion":"ambient_occlusion");
   const HRESULT ao=ambient_occlusion_d3d12(traced);
   if(FAILED(ao))d3d12_->omit(traced?"ray traced ambient occlusion (pass failed)":"screen-space ambient occlusion (pass failed)",false);
   else if(ao==S_OK)d3d12_->count(traced?"ray traced ambient occlusion passes":rt_ao_enabled_?"ambient occlusion passes (screen-space: the bounce rays did not run this frame)":"ambient occlusion passes");
   else d3d12_->count("ambient occlusion frames without a camera or pipeline");
   bind_world_targets_d3d12();
  };
  if(ao12_.frame==native_frame_index_&&!rt_ao_enabled_)ambient_occlusion(nullptr);
  // rt_direct_lights: the lamps' light of the opaque and cut-out world, over
  // the ambient occlusion and under the effects below (off: one flag read).
  // rt_indirect_light first: the lamps clamp over its bounce light.
  indirect_light_d3d12();
  if(ao12_.frame==native_frame_index_&&rt_ao_enabled_)
   ambient_occlusion(rt_indirect12_.ao_frame==native_frame_index_?rt_indirect12_.ao_now.get():nullptr);
  direct_lights_d3d12();
  // rt_reflections: the smooth surfaces' environment reflection from rays, over both.
  reflections_d3d12();
  // The world effects composite_world draws before the history copy, in its
  // order; each binds what it reads and returns the world target bound.
  capture_opaque_world_d3d12(); // unless a game effect already took it this frame
  render_ambient_particles();
  render_wind_trails();
  render_precipitation();
  // Diagnostic, over the finished world image (rt_debug_view; off: one option read).
  raytracing_debug_view_d3d12();
  rt_term_view_d3d12();
 }
 // The finished world colour for the next frame's reflections.
 if(world_d3d12_bound_&&world_d3d12_)capture_water_history();
 // Seal the last HDR boundary while its target and encoding are still current.
 end_profile_pass();
 d3d12_->set_target(nullptr);
 scene_composited_=true;
 // Selection rectangles requested after the world pass and before this
 // composite: the first interface output (composite_world's order), on the
 // back buffer, after the HUD-less copy.
 const auto draw_pending_rectangles=[this]{
  draw_deferred_combat_text(); // held back from the world pass (draw_game_glyphs_held)
  d3d12_->set_target(nullptr);
  for(const auto& pending:pending_screen_rectangles_)
   if(FAILED(draw_screen_rectangle_d3d12(pending.request,pending.viewport)))d3d12_->omit("selection rectangle (draw failed)",false);
  pending_screen_rectangles_.clear();
 };
 if(!world_d3d12_bound_||!world_d3d12_){
  draw_pending_rectangles();
  const HRESULT dimmed=dim_hud_layout_d3d12();
  transition_profile_pass("ui_composition");return FAILED(world)?world:dimmed;
 }
 world_d3d12_bound_=false;
 const auto& config=debug_overlay_.config();
 // The world image to the output: SMAA 1x or the plain copy (SceneComposite::render);
 // with a temporal upscaler the plain copy into the render-size resolved
 // image (the upscalers anti-alias themselves), and the post passes over it.
 const bool smaa=!temporal_upscaler()&&configured_boolean(config,"smaa",true);
 transition_profile_pass(smaa?"scene_smaa":"scene_copy");
 const HRESULT copied=resolve_world_d3d12(smaa);
 if(SUCCEEDED(copied))d3d12_->count(smaa?"world composites anti-aliased (SMAA)":"world composites copied");
 // Depth of field, sun rays and bloom over the resolved image, as composite_world orders them.
 const auto post=[&](const char* name,HRESULT result){
  if(result==S_OK)d3d12_->count(name);
  else if(FAILED(result))d3d12_->omit(name,false); // logged once with the pass named
 };
 // With a temporal upscaler depth of field runs after it (depth_of_field_after_upscaler_d3d12).
 if(!temporal_upscaler())depth_of_field_world_d3d12(false);
 if(sun_rays_frame_==native_frame_index_){
  transition_profile_pass("scene_sun_rays");
  post("scene_sun_rays",sun_rays_d3d12());
 }
 // Only frames that draw a map's terrain (the D3D11 rule): the global map
 // screen is no world scene and is not bloomed whole.
 if(const int bloom=configured_integer(config,"bloom",0);bloom>0&&ambient_terrain_frame_==native_frame_index_){
  transition_profile_pass("scene_bloom");
  post("scene_bloom",bloom_d3d12(bloom,configured_floating_point(config,"bloom_strength",1.f),
   bloom_ceiling(fog_prepared_frame_==native_frame_index_*2u?bloom_fog_headroom_:0.f),
   configured_floating_point(config,"bloom_threshold",1.f),configured_boolean(config,"bloom_debug_view",false)));
 }
 // The upscaler (or, failing it this frame, the filtered upscale) into the
 // output; below 100% render scale without one, the filtered upscale
 // (composite_world's scene_upscale).
 // Seal the resolved post image before upscaling or HUD work changes it.
 // At native resolution no upscale pass opens a new profiling boundary.
 end_profile_pass();
 const HRESULT upscaled=!scene_output_d3d12()?S_OK:temporal_upscaler()?upscale_world_d3d12():filtered_upscale_d3d12();
 d3d12_->clear_slots();d3d12_->set_target(nullptr);
 world_output_this_present_=true;
 capture_hudless_d3d12();
 draw_pending_rectangles();
 const HRESULT dimmed=dim_hud_layout_d3d12();
 transition_profile_pass("ui_composition");
 return FAILED(world)?world:FAILED(copied)?copied:FAILED(upscaled)?upscaled:dimmed;
}

// ---- frame_generation (DLSS-G) ----
namespace{
const char* const kFgLowRate="rendered rate below frame_generation_min_fps (auto-off, fg_auto_off)";
// Our matrices act on column vectors (m * v), stored row by row.
void fg_multiply(const float* a,const float* b,float* out) noexcept{
 for(int r=0;r<4;++r)for(int c=0;c<4;++c){float sum=0;for(int k=0;k<4;++k)sum+=a[r*4+k]*b[k*4+c];out[r*4+c]=sum;}
}
// NGX takes row-major matrices for row vectors (v * M): the transpose.
void fg_transposed(const float* in,float* out) noexcept{for(int r=0;r<4;++r)for(int c=0;c<4;++c)out[c*4+r]=in[r*4+c];}
bool fg_unproject(const float* clip_to_world,float x,float y,float z,float w,float out[3]) noexcept{
 const float clip[4]={x,y,z,w};float world[4]{};
 for(int r=0;r<4;++r)for(int c=0;c<4;++c)world[r]+=clip_to_world[r*4+c]*clip[c];
 if(std::fabs(world[3])<1e-12f)return false;
 for(int i=0;i<3;++i)out[i]=world[i]/world[3];
 return true;
}
float fg_length(const float v[3]) noexcept{return std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);}
}

// The camera block from the scene's world-to-clip matrices, the same ones
// the DLSS SR motion pass uses (not a separate view/projection record):
// the eye is the clip point (0,0,1,0) unprojected, the basis and planes come
// from the unprojected screen centre and edges. False: no perspective camera.
static bool fg_camera(const float* clip_to_world,const float* world_to_clip,x64remote::DlssgEvaluate& e) noexcept{
 float eye[3],near_c[3],far_c[3],right_c[3],up_c[3];
 if(!fg_unproject(clip_to_world,0,0,1,0,eye)||!fg_unproject(clip_to_world,0,0,0,1,near_c)||!fg_unproject(clip_to_world,0,0,1,1,far_c)||
    !fg_unproject(clip_to_world,1,0,0,1,right_c)||!fg_unproject(clip_to_world,0,1,0,1,up_c))return false;
 float forward[3],right[3],up[3],to_near[3],to_far[3];
 for(int i=0;i<3;++i){forward[i]=far_c[i]-near_c[i];right[i]=right_c[i]-near_c[i];up[i]=up_c[i]-near_c[i];to_near[i]=near_c[i]-eye[i];to_far[i]=far_c[i]-eye[i];}
 const float lf=fg_length(forward),half_w=fg_length(right),half_h=fg_length(up),near_d=fg_length(to_near),far_d=fg_length(to_far);
 if(lf<=0||half_w<=0||half_h<=0||near_d<=0||far_d<=near_d)return false;
 for(int i=0;i<3;++i){forward[i]/=lf;right[i]/=half_w;up[i]/=half_h;}
 std::memcpy(e.position,eye,sizeof(eye));std::memcpy(e.forward,forward,sizeof(forward));
 std::memcpy(e.right,right,sizeof(right));std::memcpy(e.up,up,sizeof(up));
 e.near_plane=near_d;e.far_plane=far_d;e.fov_y=2.f*std::atan(half_h/near_d);e.aspect=half_w/half_h;
 // View space: right, up, forward at the eye (left-handed, +z forward).
 const float view_to_world[16]={right[0],up[0],forward[0],eye[0], right[1],up[1],forward[1],eye[1],
                                right[2],up[2],forward[2],eye[2], 0,0,0,1};
 float world_to_view[16],view_to_clip[16],clip_to_view[16];
 if(!ambient_occlusion_invert(view_to_world,world_to_view))return false;
 fg_multiply(world_to_clip,view_to_world,view_to_clip);
 fg_multiply(world_to_view,clip_to_world,clip_to_view);
 fg_transposed(view_to_clip,e.view_to_clip);fg_transposed(clip_to_view,e.clip_to_view);
 return true;
}

// One generated frame between the previous real frame and this one: the
// frame image (world and interface) as the backbuffer, the HUD-less copy,
// and the upscaler's render-size world depth and motion (camera and object
// motion, current to previous, in render pixels: DLSS-FG guide, scale 1).
// Recorded before the frame image is composited. False: none this present.
bool Renderer::evaluate_frame_generation_d3d12() noexcept{
 D3D12Texture* const frame=d3d12_->frame_image_texture();
 D3D12Texture* const output=d3d12_->generated_image();
 const char* const skipped=fg_not_game_frame_?"not a game frame (fg_game_frame)":fg_low_rate_?kFgLowRate:!d3d12_->frame_generation_active(1)||!frame||!output?"the presenter is not in this mode yet":
  !hudless_this_present_||!hudless_d3d12_?"no HUD-less copy this present":!fg_upscaler_valid_?"no upscaler camera frame this present (no scene camera or the upscaler did not run)":
  !world_d3d12_||!upscale12_.motion?"no world depth or upscaler motion":nullptr;
 note_frame_generation(skipped);
 if(skipped){fg_previous_generated_=false;return false;}
 x64remote::DlssgEvaluate e{};
 e.width=output_width_;e.height=output_height_;e.render_w=width_;e.render_h=height_;
 float world_to_clip[16],previous_inverse[16],clip_to_prev[16],prev_to_clip[16];
 std::memcpy(world_to_clip,fg_world_to_clip_.data(),sizeof(world_to_clip));
 if(!fg_camera(fg_clip_to_world_,world_to_clip,e)||!ambient_occlusion_invert(fg_previous_clip_.data(),previous_inverse)){
  fg_previous_generated_=false;d3d12_->count("frame generation skipped (no perspective camera)");return false;
 }
 fg_multiply(fg_previous_clip_.data(),fg_clip_to_world_,clip_to_prev);
 fg_multiply(world_to_clip,previous_inverse,prev_to_clip);
 fg_transposed(clip_to_prev,e.clip_to_prev_clip);fg_transposed(prev_to_clip,e.prev_clip_to_clip);
 // The upscaler's pixel jitter as a clip offset (x64_remote_link.h DlssEvaluate).
 e.jitter_x=2.f*fg_jitter_[0]/float(std::max(1u,width_));e.jitter_y=-2.f*fg_jitter_[1]/float(std::max(1u,height_));
 e.mv_scale_x=e.mv_scale_y=1.f;
 e.reset=fg_upscaler_reset_||!fg_previous_generated_;
 e.depth_inverted=false;
 transition_profile_pass("fg_dlssg");
 d3d12_->clear_slots();
 const D3D12Frame::ExternalUse uses[5]={{frame,false,false},{world_d3d12_.get(),true,false},{upscale12_.motion.get(),false,false},
                                        {hudless_d3d12_.get(),false,false},{output,false,true}};
 const bool first=!fg_previous_generated_&&!fg_stats_tick_;
 const HRESULT hr=d3d12_->record_external("dlss-g",uses,5,[&](ID3D12GraphicsCommandList* list,ID3D12Resource* const* resources){
#ifdef EID3D11_EMBEDDED
  return embedded_d3d12_.fg_evaluate(list,resources,e);
#else
  return x64remote::dlssg_evaluate_d3d12(list,resources,e);
#endif
 });
 transition_profile_pass("present_bookkeeping");
 if(FAILED(hr)){
  char reason[120];std::snprintf(reason,sizeof(reason),
#ifdef EID3D11_EMBEDDED
   "DLSS Frame Generation failed (0x%08lX); see engine journal"
#else
   "DLSS Frame Generation failed (0x%08lX); see host.log"
#endif
   ,static_cast<unsigned long>(hr));
  frame_generation_off(1,reason);return false;
 }
 if(first){
#ifdef EID3D11_EMBEDDED
  const std::uint64_t bytes=embedded_d3d12_.fg_vram(output_width_,output_height_,width_,height_);
#else
  const std::uint64_t bytes=x64remote::dlssg_vram_estimate(output_width_,output_height_,width_,height_);
#endif
  char text[160];std::snprintf(text,sizeof(text),"DLSS Frame Generation on: %ux%u, depth/motion %ux%u; NGX VRAM estimate %.1f MiB",
   output_width_,output_height_,width_,height_,double(bytes)/(1024.0*1024.0));
  bridge_report_event("frame_generation",text);
 }
 fg_previous_generated_=true;
 d3d12_->count("frame generation evaluations");
 profiler_.add_draw(1,1);profiler_.add_batch();
 return true;
}

// frame_generation_min_fps: pauses generation while the rendered rate stays
// low and resumes with hysteresis (FidelityFX frame interpolation swap chain
// guide, "Minimum Framerate": disable it when the rate consistently drops
// below the minimum; revert only when the rate changes by more than what
// disabling it explains). Only intervals between two game frames count, and
// stalls over 200 ms are left out (a load or hitch is no low rate).
void Renderer::update_frame_generation_auto_off(int output_limit) noexcept{
 const int min_fps=frame_generation_mode_?configured_integer(debug_overlay_.config(),"frame_generation_min_fps",30):0;
 if(min_fps<=0){
  if(fg_low_rate_)bridge_report_event("fg_auto_off","frame generation resumed: auto-off disabled or frame generation off");
  fg_low_rate_=fg_rate_previous_game_=false;fg_low_rate_windows_=0;fg_rate_window_ms_=0.0;fg_rate_window_count_=0;fg_rate_cost_ms_=-1.0;
  return;
 }
 // A total-output cap cannot sustain more than half that rate while
 // generating. Keep the user's minimum guard, and do not repeatedly resume
 // from the faster one-present rate while the same cap still forbids it.
 if(output_limit>0&&double(output_limit)*.5<min_fps){
  if(!fg_low_rate_){char text[192];std::snprintf(text,sizeof(text),
   "frame generation paused: output cap %d FPS allows less than frame_generation_min_fps %d rendered FPS",output_limit,min_fps);
   bridge_report_event("fg_auto_off",text);}
  fg_low_rate_=true;fg_rate_previous_game_=false;fg_low_rate_windows_=0;
  fg_rate_window_ms_=0;fg_rate_window_count_=0;fg_rate_cost_ms_=0;
  return;
 }
 const bool game=!fg_not_game_frame_;
 const bool sample=game&&fg_rate_previous_game_&&present_interval_ms_>0.0&&present_interval_ms_<200.0;
 fg_rate_previous_game_=game;
 if(!sample)return;
 fg_rate_window_ms_+=present_interval_ms_;++fg_rate_window_count_;
 if(fg_rate_window_ms_<1000.0)return;
 const double mean=fg_rate_window_ms_/double(fg_rate_window_count_);
 fg_rate_window_ms_=0.0;fg_rate_window_count_=0;
 const double target_ms=1000.0/double(min_fps);
 char text[220];
 if(!fg_low_rate_){
  if(mean<=target_ms){fg_low_rate_windows_=0;return;}
  if(++fg_low_rate_windows_<2)return;
  fg_low_rate_=true;fg_low_rate_windows_=0;fg_rate_on_ms_=mean;fg_rate_cost_ms_=-1.0;
  std::snprintf(text,sizeof(text),"present %llu: frame generation paused: rendered %.1f FPS (%.2f ms) below frame_generation_min_fps %d for 2 s",
   static_cast<unsigned long long>(presentation_frame_index_),1000.0/mean,mean,min_fps);
  bridge_report_event("fg_auto_off",text);
  return;
 }
 if(fg_rate_cost_ms_<0.0){
  // The first window off: what generating cost (bounded to a quarter of
  // the frame, so a scene change at the switch cannot block resuming).
  fg_rate_cost_ms_=std::clamp(fg_rate_on_ms_-mean,0.0,0.25*fg_rate_on_ms_);
  std::snprintf(text,sizeof(text),"present %llu: frame generation off: rendered %.2f ms (on: %.2f ms); generating counted as %.2f ms",
   static_cast<unsigned long long>(presentation_frame_index_),mean,fg_rate_on_ms_,fg_rate_cost_ms_);
  bridge_report_event("fg_auto_off",text);
  return;
 }
 if((output_limit>0&&double(output_limit)*.5<double(min_fps)*1.1)||
    (mean+fg_rate_cost_ms_)*1.1>target_ms){fg_low_rate_windows_=0;return;}
 if(++fg_low_rate_windows_<3)return;
 fg_low_rate_=false;fg_low_rate_windows_=0;
 std::snprintf(text,sizeof(text),"present %llu: frame generation resumed: rendered %.1f FPS off (%.1f with generating) clears frame_generation_min_fps %d by 10%% for 3 s",
  static_cast<unsigned long long>(presentation_frame_index_),1000.0/mean,1000.0/(mean+fg_rate_cost_ms_),min_fps);
 bridge_report_event("fg_auto_off",text);
}

// Why frame generation did or did not generate from this present, one
// event on each change (the no-perspective-camera skip is counted instead).
void Renderer::note_frame_generation(const char* skipped) noexcept{
 if(skipped==fg_skip_logged_)return;
 fg_skip_logged_=skipped;
 char text[200];std::snprintf(text,sizeof(text),"present %llu: %s%s",static_cast<unsigned long long>(presentation_frame_index_),
  skipped?"not generated: ":"generating",skipped?skipped:"");
 bridge_report_event("fg_generation",text);
}

// A failure turns that frame generation mode off for the session: one log
// line, the F10 note on the option (blocked_option_note), the feature
// released (DLSS-G here; FSR's context goes with the proxy swap chain).
void Renderer::frame_generation_off(int mode,const char* reason) noexcept{
 if(mode<1||mode>2)return;
 std::snprintf(fg_failed_[mode],sizeof(fg_failed_[mode]),"%s",reason&&reason[0]?reason:"failed");
 if(frame_generation_mode_==mode){frame_generation_mode_=0;set_frame_generation_unavailable_reason(fg_failed_[mode]);fg_reason_mode_=mode;}
 char text[240];std::snprintf(text,sizeof(text),"%s: %s",mode==1?"DLSS Frame Generation":"FSR frame generation",fg_failed_[mode]);
 bridge_report_event("frame_generation_failed",text);
 if(mode==1){fg_previous_generated_=false;
#ifdef EID3D11_EMBEDDED
  embedded_d3d12_.fg_release();
#else
  x64remote::dlssg_release();
#endif
 }
 else fsr_prepared_=false;
 if(d3d12_&&frame_generation_mode_==0)d3d12_->set_frame_generation(0);
}

// FSR frame generation (mode 2): FidelityFX's prepare for this frame on the
// frame's list (the upscaler's render-size depth and motion, current to
// previous in render pixels: scale 1; the same camera block as DLSS-G).
// The proxy swap chain interpolates inside Present from the back buffer and
// the HUD-less image (set_fsr_hudless). False: no generation this present.
bool Renderer::prepare_fsr_frame_generation_d3d12() noexcept{
 const char* const skipped=fg_not_game_frame_?"not a game frame (fg_game_frame)":fg_low_rate_?kFgLowRate:!d3d12_->frame_generation_active(2)?"the presenter is not in this mode yet":
  !hudless_this_present_||!hudless_d3d12_?"no HUD-less copy this present":!fg_upscaler_valid_?"no upscaler camera frame this present (no scene camera or the upscaler did not run)":
  !world_d3d12_||!upscale12_.motion?"no world depth or upscaler motion":nullptr;
 note_frame_generation(skipped);
 if(skipped){fg_previous_generated_=false;return false;}
 x64remote::DlssgEvaluate e{};
 float world_to_clip[16];
 std::memcpy(world_to_clip,fg_world_to_clip_.data(),sizeof(world_to_clip));
 if(!fg_camera(fg_clip_to_world_,world_to_clip,e)){
  fg_previous_generated_=false;d3d12_->count("frame generation skipped (no perspective camera)");return false;
 }
 D3D12Frame::FsrPrepare p{};
 p.render_w=width_;p.render_h=height_;
 p.jitter_x=fg_jitter_[0];p.jitter_y=fg_jitter_[1];  // the upscaler's, as FSR takes it
 const std::int64_t now=perf_now();
 p.frame_ms=fsr_prepare_tick_?float(double(now-fsr_prepare_tick_)*1000.0/double(perf_frequency())):16.7f;
 fsr_prepare_tick_=now;
 p.reset=fg_upscaler_reset_||!fg_previous_generated_;
 p.near_plane=e.near_plane;p.far_plane=e.far_plane;p.fov_y=e.fov_y;
 std::memcpy(p.position,e.position,sizeof(p.position));std::memcpy(p.up,e.up,sizeof(p.up));
 std::memcpy(p.right,e.right,sizeof(p.right));std::memcpy(p.forward,e.forward,sizeof(p.forward));
 transition_profile_pass("fg_fsr_prepare");
 d3d12_->clear_slots();
 const D3D12Frame::ExternalUse uses[2]={{world_d3d12_.get(),true,false},{upscale12_.motion.get(),false,false}};
 const bool first=!fg_previous_generated_&&!fg_stats_tick_;
 const HRESULT hr=d3d12_->record_external("fsr-fg prepare",uses,2,[&](ID3D12GraphicsCommandList* list,ID3D12Resource* const* resources){
  return d3d12_->fsr_prepare(list,resources[0],resources[1],p);
 });
 transition_profile_pass("present_bookkeeping");
 if(FAILED(hr)){
#ifdef EID3D11_EMBEDDED
  frame_generation_off(2,"FidelityFX frame generation prepare failed; see engine journal");
#else
  frame_generation_off(2,"FidelityFX frame generation prepare failed; see host.log");
#endif
  return false;
 }
 if(first){
  char text[160];std::snprintf(text,sizeof(text),"FSR frame generation on: %ux%u, depth/motion %ux%u; FidelityFX context VRAM %.1f MiB",
   output_width_,output_height_,width_,height_,double(d3d12_->fsr_vram())/(1024.0*1024.0));
  bridge_report_event("frame_generation",text);
 }
 fg_previous_generated_=true;
 d3d12_->count("frame generation prepares (FSR)");
 return true;
}

// FSR: the HUD-less image as presented colours: itself under an identity
// gamma ramp, else a gamma-mapped copy (the back buffer carries the ramp,
// and FidelityFX takes every difference from it as interface). Null: none.
D3D12Texture* Renderer::fsr_hudless_d3d12() noexcept{
 if(!hudless_d3d12_)return nullptr;
 {std::lock_guard<std::mutex> lock(gamma_lock_);if(gamma_identity_)return hudless_d3d12_.get();}
 if(!hudless_gamma_d3d12_||!d3d12_->texture_matches(*hudless_gamma_d3d12_,output_width_,output_height_,DXGI_FORMAT_B8G8R8A8_UNORM)){
  hudless_gamma_d3d12_.reset();
  if(FAILED(d3d12_->create_target(output_width_,output_height_,hudless_gamma_d3d12_,DXGI_FORMAT_B8G8R8A8_UNORM,DXGI_FORMAT_UNKNOWN))){
   hudless_gamma_d3d12_.reset();d3d12_->omit("FSR frame generation HUD-less gamma copy (texture creation failed)",false);return nullptr;
  }
 }
 if(apply_gamma_ramp_d3d12(hudless_d3d12_.get(),hudless_gamma_d3d12_.get())!=S_OK){
  d3d12_->omit("FSR frame generation HUD-less gamma copy (pass failed)",false);return nullptr;
 }
 return hudless_gamma_d3d12_.get();
}

// Presents per second from the pacer's counts, every half second (measured,
// never assumed 2x).
void Renderer::update_frame_generation_rates() noexcept{
 if(!d3d12_)return;
 const auto stats=d3d12_->frame_generation_stats();
 const std::int64_t now=perf_now();
 if(!stats.active){fg_stats_tick_=0;fg_presented_fps_=fg_real_fps_=0.0;return;}
 if(!fg_stats_tick_){fg_stats_tick_=now;fg_stats_real_=stats.real_presents;fg_stats_generated_=stats.generated_presents;return;}
 const double seconds=double(now-fg_stats_tick_)/double(perf_frequency());
 if(seconds<0.5)return;
 const double real=double(stats.real_presents-fg_stats_real_),generated=double(stats.generated_presents-fg_stats_generated_);
 fg_real_fps_=real/seconds;fg_presented_fps_=(real+generated)/seconds;
 fg_stats_tick_=now;fg_stats_real_=stats.real_presents;fg_stats_generated_=stats.generated_presents;
}

// fg_frame_image: the finished world in the frame image (upscaled and
// post-processed, before the HUD-editor dim and any interface), copied as
// frame generation's HUD-less input. One full-screen copy a world frame.
void Renderer::capture_hudless_d3d12() noexcept{
 if(!fg_frame_image_)return;
 transition_profile_pass("fg_hudless_copy");
 if(!hudless_d3d12_||!d3d12_->texture_matches(*hudless_d3d12_,output_width_,output_height_,DXGI_FORMAT_B8G8R8A8_UNORM)){
  hudless_d3d12_.reset();
  if(FAILED(d3d12_->create_texture(output_width_,output_height_,DXGI_FORMAT_B8G8R8A8_UNORM,hudless_d3d12_))){
   hudless_d3d12_.reset();d3d12_->omit("frame generation HUD-less copy (texture creation failed)",false);return;
  }
 }
 const HRESULT hr=d3d12_->capture(*hudless_d3d12_,0,0,RECT{0,0,LONG(output_width_),LONG(output_height_)});
 if(hr==S_OK){hudless_this_present_=true;d3d12_->count("frame generation HUD-less copies");}
 else if(FAILED(hr))d3d12_->omit("frame generation HUD-less copy (capture failed)",false);
}

// The HUD layout editor dims the composited world under its grid (the end
// of the D3D11 composite_world), before every interface pass.
HRESULT Renderer::dim_hud_layout_d3d12(){
 if(!debug_overlay_.hud_editing())return S_OK;
 transition_profile_pass("hud_layout_dim");
 const ScreenRectangleRequest dim{0,0,int(output_width_),int(output_height_),120,false};
 const D3DVIEWPORT7 viewport{0,0,output_width_,output_height_,0.f,1.f};
 return draw_screen_rectangle_d3d12(dim,viewport,debug_overlay_.hud_grid_step());
}

// SceneComposite::render on D3D12: SMAA 1x (edges, weights, then the
// neighbourhood blend into the output) or copyPS, with the composite's own
// programs, lookup textures, targets and samplers; the output is the back
// buffer, or with a temporal upscaler the render-size resolved image.
HRESULT Renderer::resolve_world_d3d12(bool smaa){
 auto& post=post12_;
 D3D12Texture* const output=scene_output_d3d12();
 if(post.programs.empty())SceneComposite::shader_programs(post.programs); // sceneVS, edge, weight, blend, copy, ...
 if(post.programs.size()<5){d3d12_->omit("scene composite programs (SMAA library missing)",true);return S_OK;}
 UINT width=0,height=0,levels=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
 if(!d3d12_->texture_info(*world_d3d12_,width,height,levels,format))return E_FAIL;
 HRESULT hr=S_OK;
 if(smaa){
  if(!post.area){
   D3D12TextureRef area,search;
   if(FAILED(hr=d3d12_->create_texture(AREATEX_WIDTH,AREATEX_HEIGHT,DXGI_FORMAT_R8G8_UNORM,area))||
      FAILED(hr=d3d12_->write_texture(*area,RECT{0,0,AREATEX_WIDTH,AREATEX_HEIGHT},areaTexBytes,AREATEX_PITCH))||
      FAILED(hr=d3d12_->create_texture(SEARCHTEX_WIDTH,SEARCHTEX_HEIGHT,DXGI_FORMAT_R8_UNORM,search))||
      FAILED(hr=d3d12_->write_texture(*search,RECT{0,0,SEARCHTEX_WIDTH,SEARCHTEX_HEIGHT},searchTexBytes,SEARCHTEX_PITCH)))return hr;
   post.area=std::move(area);post.search=std::move(search);
  }
  if(!post.edges||!d3d12_->texture_matches(*post.edges,width,height,DXGI_FORMAT_R8G8_UNORM)){
   post.edges.reset();post.weights.reset();
   if(FAILED(hr=d3d12_->create_target(width,height,post.edges,DXGI_FORMAT_R8G8_UNORM,DXGI_FORMAT_UNKNOWN))||
      FAILED(hr=d3d12_->create_target(width,height,post.weights,DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_UNKNOWN)))return hr;
  }
 }
 const float metrics[4]={1.f/float(width),1.f/float(height),float(width),float(height)};
 D3D11_SAMPLER_DESC linear{};linear.AddressU=linear.AddressV=linear.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
 linear.MaxLOD=0;linear.Filter=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
 D3D11_SAMPLER_DESC point=linear;point.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
 D3D12NativeState state; // opaque, no depth; the D3D11 default raster
 state.raster.FillMode=D3D11_FILL_SOLID;state.raster.CullMode=D3D11_CULL_BACK;state.raster.DepthClipEnable=TRUE;
 const auto pass=[&](D3D12Texture* target,const ShaderProgram& pixel,const D3D12Texture* a,const D3D12Texture* b,const D3D12Texture* c)->HRESULT{
  d3d12_->clear_slots();
  d3d12_->set_constants(0,metrics,sizeof(metrics));
  d3d12_->set_sampler(0,linear);d3d12_->set_sampler(1,point);
  d3d12_->set_resource(0,a);d3d12_->set_resource(1,b);d3d12_->set_resource(2,c);
  d3d12_->set_target(target);
  return d3d12_->draw(post.programs[0],pixel,state,nullptr,3);
 };
 if(smaa){
  const float zero[4]{};d3d12_->clear_texture(*post.edges,zero);
  if((hr=pass(post.edges.get(),post.programs[1],world_d3d12_.get(),nullptr,nullptr))==S_OK)
   hr=pass(post.weights.get(),post.programs[2],post.edges.get(),post.area.get(),post.search.get());
  if(hr==S_OK)hr=pass(output,post.programs[3],world_d3d12_.get(),post.weights.get(),nullptr);
 }else hr=pass(output,post.programs[4],world_d3d12_.get(),nullptr,nullptr);
 d3d12_->clear_slots();d3d12_->set_target(nullptr);
 if(d3d12_not_done(hr)){ // a program still compiling (or a ring exhausted): the plain copy through the interface program
  if(hr==S_FALSE)d3d12_->count("world composites waiting for their pipeline");
  const float w=float(output?width:output_width_),h=float(output?height:output_height_);
  const Vertex quad[4]={{0,0,0,1,0xffffffffu,0xffffffffu,0,0},{w,0,0,1,0xffffffffu,0xffffffffu,1,0},
                        {0,h,0,1,0xffffffffu,0xffffffffu,0,1},{w,h,0,1,0xffffffffu,0xffffffffu,1,1}};
  Constants block{};block.viewport[2]=w;block.viewport[3]=h;
  block.texture_factor[0]=block.texture_factor[1]=block.texture_factor[2]=block.texture_factor[3]=1.0f;
  block.transformed=TRUE;block.textured=TRUE;
  block.color_op=D3DTOP_SELECTARG1;block.color_arg1=D3DTA_TEXTURE;block.color_arg2=D3DTA_CURRENT;
  block.alpha_op=D3DTOP_SELECTARG1;block.alpha_arg1=D3DTA_TEXTURE;block.alpha_arg2=D3DTA_CURRENT;
  block.alpha_func=D3DCMP_ALWAYS;block.hud_opacity=1.0f;
  D3D12UiDraw draw;draw.texture=world_d3d12_.get();draw.program=D3D12UiProgram::game;
  draw.blend=fixed_blend_choice(FALSE,D3DBLEND_ONE,D3DBLEND_ZERO,false);
  draw.sampler=point;
  draw.first_vertex=0;draw.vertex_count=4;
  D3D12UiPass ui;
  ui.vertices=quad;ui.vertex_bytes=sizeof(quad);ui.vertex_stride=sizeof(Vertex);
  ui.constants=&block;ui.constant_block_bytes=sizeof(Constants);ui.constant_blocks=1;
  ui.draws=&draw;ui.draw_count=1;
  d3d12_->set_target(output);
  const HRESULT drawn=d3d12_->draw_ui(ui);
  d3d12_->set_target(nullptr);
  return drawn;
 }
 return hr;
}

// Deferred world material (renderer.h deferred_world12_). The frame flows,
// with the option on (flush_native_static_terrain, flush_native_static):
// terrain set and neighbour sets build, prepass and mask (phase 1); HiZ;
// ground cover and critters, whole; the
// static set's build, prepass and mask (phase 1); then the terrain and
// neighbour material (phase 2) and the static
// material (phase 2). Every mask and the HiZ meet the same depth as before
// and each mask its own receiver record, so they hold the same values; the
// terrain and neighbour material meet the final opaque depth (EQUAL against
// their own prepass), so what cover, critters and objects hide is not
// shaded and what they cover keeps their colour. A set whose material pass
// writes depth without a prepass draws its kept material at once
// (native_deferred_depth_writer_), as do any other flush and the composite.
bool Renderer::depth_prepass_d3d12() const noexcept{
 // Ray traced sun shadows keep it on whatever the option says (user decision
 // 2026-11-14): the traced mask shades only prepassed receivers.
 const auto& config=debug_overlay_.config();
 return configured_boolean(config,"depth_prepass",true)||
  (d3d12_&&configured_boolean(config,"rt_sun_shadows",false)&&!d3d12_->raytracing_unavailable());
}
bool Renderer::deferred_world_wanted_d3d12() const noexcept{
 return d3d12_&&configured_boolean(debug_overlay_.config(),"deferred_world_material",true)&&
  depth_prepass_d3d12()&&
  configured_integer(debug_overlay_.config(),"prepass_depth_check",0)==0;
}

// Indexed object draws (option indexed_object_draws; native_shader.h
// nativeOwnerOutput): only while material instance batches draw their own
// shipping programs. The general programs (shader_split 0, material quality
// Off), the check programs (F10 shader_split_check, fur_debug) and the
// prepass depth check's second draw read visible triangles.
bool Renderer::indexed_object_draws_d3d12() const noexcept{
 const auto& config=debug_overlay_.config();
 return d3d12_&&configured_boolean(config,"indexed_object_draws",true)&&material_level()&&
  configured_boolean(config,"shader_split",true)&&configured_integer(config,"shader_split_check",0)==0&&!fur_debug_&&
  configured_integer(config,"prepass_depth_check",0)==0;
}

void Renderer::swap_native_slot_buffers12(unsigned layout_slot) noexcept{
 if(layout_slot<2||layout_slot>4)return;
 auto& own=native_slot_buffers12_[layout_slot-2];
 std::swap(own.dynamic,native12_.dynamic);std::swap(own.arguments,native12_.arguments);
 std::swap(own.visible,native12_.visible);std::swap(own.prepared,native12_.prepared);
 std::swap(own.owners,native12_.owners);std::swap(own.indices,native12_.indices);
}

// all: every kept material in order; else the terrain and neighbour ones
// (flush_native_static draws the static set's after the cover).
HRESULT Renderer::resume_deferred_world_d3d12(bool all){
 HRESULT result=S_OK;
 while(!deferred_world12_.empty()){
  const auto next=std::find_if(deferred_world12_.begin(),deferred_world12_.end(),
   [&](const DeferredWorldFlush12& entry){return all||entry.slot!=2;});
  if(next==deferred_world12_.end())break;
  const unsigned slot=next->slot;
  const FixedState* const caller_frame=native_static_frame_;
  native_static_frame_=next->has_frame?&next->frame:nullptr;
  native_static_reuse_=true;native_flush_phase_=2;
  const HRESULT hr=flush_native_d3d12(slot);
  native_static_reuse_=false;native_flush_phase_=0;native_static_frame_=caller_frame;
  const auto done=std::find_if(deferred_world12_.begin(),deferred_world12_.end(),[&](const DeferredWorldFlush12& entry){return entry.slot==slot;});
  if(done!=deferred_world12_.end())deferred_world12_.erase(done);
  if(FAILED(hr)&&SUCCEEDED(result))result=hr;
 }
 if(deferred_world12_.empty())native_deferred_depth_writer_=false;
 return result;
}

// flush_native on the D3D12 backend: the same record preparation, then the
// build dispatches and the interface batches through the executor.
HRESULT Renderer::flush_native_d3d12(unsigned layout_slot){
 const bool shadow_coverage=debug_overlay_.mode()==DebugOverlayMode::full&&
  debug_overlay_.config().debug_resource_view()==DebugResourceView::shadow_cascade_coverage;
 if(native_shadow_collecting_)return flush_native_shadows(); // the sun maps (flush_native_shadows_d3d12)
 // A reused static bake draws with an empty queue (flush_native_static_set).
 // Taken once: any flush nested in this one is an ordinary flush.
 const bool static_reuse=native_static_reuse_,static_bake=native_static_bake_;
 native_static_reuse_=native_static_bake_=false;
 // Deferred world material (deferred_world12_): phase 1 keeps this set's
 // material for later when it can (below), phase 2 draws a kept one. Any
 // other flush draws the kept materials first, so the frame's order holds.
 const int phase=native_flush_phase_;native_flush_phase_=0;
 native_flush_deferred_=false;
 const bool slot_set=layout_slot>=2&&layout_slot<=4;
 const bool may_defer=phase==1&&slot_set&&(static_reuse||static_bake)&&
  std::none_of(deferred_world12_.begin(),deferred_world12_.end(),[&](const DeferredWorldFlush12& entry){return entry.slot==layout_slot;})&&!scene_composited_&&!hit_test_active_&&world_d3d12_bound_&&
  depth_prepass_d3d12()&&
  configured_integer(debug_overlay_.config(),"prepass_depth_check",0)==0;
 if(phase==1&&!may_defer)d3d12_->count(slot_set&&!(static_reuse||static_bake)?"deferred world material: set not baked drawn whole":"deferred world material: world flush drawn whole");
 if(phase!=2&&!may_defer&&!deferred_world12_.empty()){const HRESULT resumed=resume_deferred_world_d3d12(true);if(FAILED(resumed))return resumed;}
 if(native_pending_draws_.empty()&&!static_reuse){if(phase!=2)discard_native();return S_OK;}
 // Phase 2 has no queue of its own: the queue then is the flush that called
 // resume_deferred_world_d3d12's, which stays.
 struct Discard{Renderer& renderer;bool on;~Discard(){if(on)renderer.discard_native();}} discard{*this,phase!=2};
 // The static object (2), terrain (3) and neighbour opaque (4) sets draw.
 if(layout_slot>=5&&layout_slot<kMapOpaqueLayout){
  d3d12_->omit("native static sets",true);
  return S_OK;
 }
 if(FAILED(native_d3d12_ensure())){d3d12_->omit("native packets (native resources failed)",false);return E_FAIL;}
 // The ray tracing scene of this world frame (rt_sun_shadows, rt_debug_view;
 // off: two option reads), before this flush binds anything.
 if(world_d3d12_bound_&&!scene_composited_&&!hit_test_active_)update_raytracing_scene_d3d12();
 // Material classes (S1): profiles of newly composed atlases, then every
 // record's profile index once the set changed; profiles of retired textures go.
 {
  // Temporary (equipment lag, 2026-11-06): material profile builds over 10 ms.
  LARGE_INTEGER material_start{},material_end{},material_frequency{};
  QueryPerformanceCounter(&material_start);
  const auto built_before=material_profiles_built_;
  prepare_material_profiles();refresh_material_profile_indices_d3d12();refresh_native_material_instances();trim_material_profiles();
  QueryPerformanceCounter(&material_end);QueryPerformanceFrequency(&material_frequency);
  const double material_ms=1000.0*double(material_end.QuadPart-material_start.QuadPart)/double(material_frequency.QuadPart);
  static unsigned material_slow_lines=0;
  if(material_ms>10.0&&material_slow_lines<200){
   ++material_slow_lines;
   char line[160];std::snprintf(line,sizeof(line),"material profiles took %.1f ms (%llu built, %zu entries)",material_ms,
    static_cast<unsigned long long>(material_profiles_built_-built_before),material_profiles_.size());
   bridge_report_event("material_profiles_slow",line);
  }
 }
 auto& layout=native_submission_layouts_[layout_slot];
 // The static sets (the D3D11 flush's rule): an unchanged set reuses its
 // bake -- the layout, work and per-batch totals of the frame that built it
 // (flush_native_static_set checked its keys); any other use of a static
 // layout rebuilds it, so its bake stops being valid first.
 const NativeStaticSet* static_set=native_static_set_for_layout(layout_slot);
 auto& bake=layout.static_bake;
 HRESULT hr=S_OK;
 if(!static_reuse){
  if(static_set)bake.set_revision=bake.content_revision=0;
  const bool static_order=static_set&&layout.static_revision==static_set->revision&&
   layout.source_revision==native_layout_revision_&&layout.source_draws.size()==native_pending_draws_.size();
  if(static_order)native_draw_order_=layout.source_draws;
  else try{order_native_groups(false);}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
  if(FAILED(hr=prepare_native_submission_layout(false,layout_slot)))return hr;
  layout.contact_generation=native_contact_generation_;
  if(static_set)layout.static_revision=static_set->revision;
 }
 if(layout.work.empty())return S_OK;
 if(!static_reuse){
  refresh_native_draw_work(layout,false);
  if(static_set&&static_bake&&FAILED(hr=bake_native_static_layout(layout)))return hr;
 }
 // Embedded game producers submit every native semantic, including particle meshes.
 const auto interface_batch=[](const NativeBatch&){return true;};
 // Deferred material: the set's build outputs stay in its own buffers from
 // phase 1 to phase 2. Posed records keep theirs too: their world vertices
 // live in their prepared mesh slot's own range (retire_native_world_range),
 // which no later flush of the frame rewrites.
 const bool defer=may_defer;
 struct SlotBuffers{Renderer& renderer;unsigned slot;bool on;~SlotBuffers(){if(on)renderer.swap_native_slot_buffers12(slot);}}
  slot_buffers{*this,layout_slot,defer||phase==2};
 if(slot_buffers.on)swap_native_slot_buffers12(layout_slot);
 const auto kept_entry=phase==2?std::find_if(deferred_world12_.begin(),deferred_world12_.end(),
  [&](const DeferredWorldFlush12& entry){return entry.slot==layout_slot;}):deferred_world12_.end();
 if(phase==2&&(kept_entry==deferred_world12_.end()||kept_entry->prepassed.size()!=layout.batches.size()))return E_UNEXPECTED;
 // A world flush with work: the frame has world output (the D3D11 flush's
 // rule; the upscalers start their history over on frames without it).
 if(!scene_composited_)scene_updated_=true;
 const auto& programs=native12_.programs;
 const UINT draw_count=static_cast<UINT>(layout.work.size());
 // Frame buffers: dynamic bytes, the work list, the arguments (written by the
 // build dispatch from the batches' starting values) and its outputs.
 // (Phase 2: the set's own buffers from phase 1, the same layout: none grows.)
 if(FAILED(hr=native_d3d12_buffer(native12_.dynamic,phase==2?0:native_pending_upload_.size(),0,0))||
    FAILED(hr=native_d3d12_buffer(native12_.work,UINT64(draw_count)*sizeof(NativeDrawWorkGpu),sizeof(NativeDrawWorkGpu),0))||
    // The arguments, then the build's 32-byte entry per record (nativeBuildSelect).
    FAILED(hr=native_d3d12_buffer(native12_.arguments,UINT64(layout.arguments.size())*sizeof(UINT)+UINT64(draw_count)*32u,0,kD3D12BufferUnordered|kD3D12BufferIndirect))||
    FAILED(hr=native_d3d12_buffer(native12_.visible,UINT64((std::max)(1u,layout.triangles))*16u,16u,kD3D12BufferUnordered))||
    FAILED(hr=native_d3d12_buffer(native12_.prepared,UINT64((std::max)(1u,layout.components))*sizeof(NativePreparedVertexGpu),sizeof(NativePreparedVertexGpu),kD3D12BufferUnordered))||
    FAILED(hr=native_d3d12_persistent(native12_.world_vertices,UINT64(sizeof(NativeWorldVertexGpu))*64u,sizeof(NativeWorldVertexGpu),0))||
    FAILED(hr=ensure_native_world_states())||
    FAILED(hr=native_d3d12_persistent(native12_.world_states,UINT64(sizeof(NativeWorldStateGpu))*64u,sizeof(NativeWorldStateGpu),0))||
    FAILED(hr=native_d3d12_persistent(native12_.animation,64u,0,0))||
    FAILED(hr=native_d3d12_persistent(native12_.pose_nodes,UINT64(sizeof(NativePoseNodeGpu))*16u,sizeof(NativePoseNodeGpu),0)))
  return hr;
 // Indexed object draws (option indexed_object_draws; native_shader.h
 // nativeOwnerOutput): while they draw, the build gives the layout's indexed
 // batches vertex owner entries (8 bytes an id) and index triples at their
 // visible triangles' slots (12 bytes each) instead of visible triangles.
 // Phase 2 draws what phase 1 built, from the set's own buffers.
 if(phase!=2){
  layout.indexed_build=layout.owners!=0&&indexed_object_draws_d3d12();
  if(layout.indexed_build&&
     (FAILED(hr=native_d3d12_buffer(native12_.owners,UINT64(layout.owners)*8u,8u,kD3D12BufferUnordered))||
      FAILED(hr=native_d3d12_buffer(native12_.indices,UINT64((std::max)(1u,layout.triangles))*12u,0,kD3D12BufferUnordered))))return hr;
 }
 const bool indexed_draws=layout.indexed_build;
 // A batch's arguments: an indexed batch's own five-word slot (also its plain draw's), else its four-word slot.
 const auto batch_arguments=[](const NativeBatch& batch,std::size_t index){return batch.indexed?batch.indexed_args_offset:static_cast<UINT>(index)*16u;};
 if(phase!=2&&!native_pending_upload_.empty()&&FAILED(hr=d3d12_->write_buffer(*native12_.dynamic,0,native_pending_upload_.data(),native_pending_upload_.size())))return hr;
 // Static layouts keep their work in their own buffer (bake.work12), written
 // only when the layout is built; the bake becomes reusable once it is.
 if(static_set){
  if(!static_reuse){
   if(FAILED(hr=upload_native_static_work(layout)))return hr;
   if(static_bake){bake.set_revision=static_set->revision;bake.content_revision=native_static_content_revision_;}
  }
 }else if(FAILED(hr=d3d12_->write_buffer(*native12_.work,0,layout.work.data(),UINT64(draw_count)*sizeof(NativeDrawWorkGpu))))return hr;
 D3D12Buffer* const work=static_set?bake.work12.get():native12_.work.get();
 if(!work)return E_FAIL;
 // A reused bake's meshes stay current like every drawn mesh
 // (retire_stale_native_geometry keeps a mesh used this frame).
 if(static_reuse)for(auto* mesh:bake.meshes)mesh->last_used_frame=native_frame_index_;
 // Phase 2 draws what phase 1 built and uploaded this frame: the arguments
 // (culled counts), instances, terrain frame and phases, poses, lights. Its
 // uploads would also consume the calling flush's pose and phase work.
 if(phase!=2){
 if(!layout.arguments.empty()&&FAILED(hr=d3d12_->write_buffer(*native12_.arguments,0,layout.arguments.data(),layout.arguments.size()*sizeof(UINT))))return hr;
 if(FAILED(hr=upload_native_instances_d3d12()))return hr;
 // The terrain frame (t127) and layer shifts (t19) the set_* calls kept.
 if(FAILED(hr=upload_native_terrain_frame_d3d12()))return hr;
 if(FAILED(hr=upload_native_terrain_phases_d3d12()))return hr;
 if(FAILED(hr=prepare_native_poses_d3d12()))return hr;
 // Lava and other self-lit ground as point lights for the next frame's
 // set_native_point_lights and as bounce sources (the D3D11 flush's call).
 if(!scene_composited_)refresh_emissive_lights();
 if(FAILED(hr=upload_native_point_lights_d3d12()))return hr;
 // The point lights' first bounce (cave_irradiance.h) from the lights just
 // uploaded, and its sampling constants in the b5 tail (the D3D11 flush order).
 update_cave_irradiance();
 {const CaveSampleConstants cave=cave_sample_constants();
  static_assert(sizeof(cave)==80&&sizeof(native12_.point_light_constants)==112,"point-light constant layout");
  std::memcpy(reinterpret_cast<std::uint8_t*>(native12_.point_light_constants.data())+32,&cave,sizeof(cave));}
 }
 if(!scene_composited_&&contact_update_frame_!=native_frame_index_){
  contact_update_frame_=native_frame_index_;
  // The shared per-map grids (tile counts) without the ground cover.
  AmbientWorldSource inputs;
  if(bridge_ambient_world_source(inputs)&&inputs.terrain)rebuild_ambient_particle_map(inputs,bridge_world_fog_snapshot().environment);
  if(FAILED(update_contact_maps_d3d12())){
   release_contact_blend_maps();
   bridge_append_probe_event("contact_blend_art_omitted","contact atlas or tile grid unavailable; contact bands show the painted ground colour on this map");
  }
 }
 if(FAILED(hr=ensure_terrain_noise_d3d12()))return hr;
 // The game's hourly fog colour of this world flush (the D3D11 flush sets it
 // before bind_world_fog): the owned sky's horizon and the shaft haze meet the
 // fog in it. Unset, both used the initial (.5, .6, .7).
 if(!scene_composited_&&!layout.batches.empty()){
  const auto& first=native_state_buckets_[layout.batches.front().state_bucket].state;
  const DWORD fog=(native_static_frame_?*native_static_frame_:first).render[D3DRENDERSTATE_FOGCOLOR];
  sky_fog_color_[0]=((fog>>16)&255)/255.f;sky_fog_color_[1]=((fog>>8)&255)/255.f;sky_fog_color_[2]=(fog&255)/255.f;
 }
 // The world fog frame values (b10) for this flush (bind_world_fog keeps them).
 if(FAILED(hr=bind_world_fog()))return hr;
 std::array<std::uint8_t,sizeof(native_world_clip_)*2+80> clip;native_world_clip_values(clip);
 if(phase==2)clip=kept_entry->clip; // the values its prepass drew with
 if(phase!=2){
 transition_profile_pass("compute_build");
 // The build dispatches (nativeBuildCS), per geometry page range.
 d3d12_->clear_slots();
 // prepareNativeVertex also consumes the flat-colour flag for records
 // without retained world geometry; raster-stage constants arrive later.
 Constants build_constants{};build_constants.combiner_padding=hit_test_active_?1u:0u;
 d3d12_->set_constants(0,&build_constants,sizeof(build_constants));
 d3d12_->set_constants(1,&native_world_lighting_,sizeof(native_world_lighting_));
 d3d12_->set_constants(3,clip.data(),UINT(clip.size()));
 d3d12_->set_resource(2,native12_.dynamic.get());
 d3d12_->set_resource(5,native12_.draws.get());
 d3d12_->set_resource(13,native12_.instances.get());
 d3d12_->set_resource(126,native12_.instances.get());
 d3d12_->set_resource(16,work);
 // The occlusion pyramid (nativeHiZ, t26) while b3 says it is active.
 d3d12_->set_resource(26,native_occlusion_active()?native_hiz12_.get():nullptr);
 d3d12_->set_unordered(0,native12_.visible.get());
 d3d12_->set_unordered(1,native12_.arguments.get());
 d3d12_->set_unordered(2,native12_.prepared.get());
 d3d12_->set_unordered(3,native12_.world_vertices.get());
 // The prepared world's inputs and outputs (as the D3D11 flush binds them).
 d3d12_->set_resource(10,native12_.animation.get());
 d3d12_->set_resource(12,native12_.pose_nodes.get());
 d3d12_->set_resource(19,native12_.layer_shifts.get());
 d3d12_->set_resource(127,native12_.terrain_frame.get());d3d12_->set_resource(72,fire_burn_mask12());if(fire_burn_bound_)d3d12_->set_resource(74,fire_burn_materials12());
 d3d12_->set_unordered(6,native12_.world_states.get());
 const bool capture_lod=begin_native_lod_capture(layout);
 d3d12_->set_unordered(8,capture_lod?native_lod_observations12_.get():nullptr);
 // Object motion: rewritten world vertices keep their old contents (u5, the b4 stamp).
 D3D12Buffer* const previous_world=previous_world_vertices_d3d12();
 d3d12_->set_unordered(5,previous_world);
 d3d12_->set_unordered(4,indexed_draws?native12_.owners.get():nullptr);
 d3d12_->set_unordered(7,indexed_draws?native12_.indices.get():nullptr);
 // nativeBuildCS per range: whole, one group per record, or, for a few large
 // records, their selection and output ranges (one thread each, entries after
 // the arguments), the chunks of their components and triangles (dispatch z),
 // then, when the range prepares world vertices, the commit of their state.
 const UINT build_scratch=static_cast<UINT>(layout.arguments.size()*sizeof(UINT));
 for(const auto& range:layout.dispatches){
  if(range.geometry_page>=native_geometry_pages_.size()||!native_geometry_pages_[range.geometry_page].d3d12)return E_FAIL;
  d3d12_->set_resource(1,native_geometry_pages_[range.geometry_page].d3d12.get());
  UINT width=0,height=0,record_width=0,record_height=0;
  if(!native_dispatch_dimensions(range.count,width,height)||!native_dispatch_dimensions((range.count+63u)/64u,record_width,record_height))
   return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
  const auto step=[&](UINT chunks,UINT x,UINT y,UINT z){
   const std::array<UINT,8> work_range{range.first,range.count,previous_world?upscale12_.motion_stamp:0u,indexed_draws?1u:0u,chunks,build_scratch,capture_lod?1u:0u,0u};
   d3d12_->set_constants(4,work_range.data(),sizeof(work_range));
   return d3d12_->dispatch(programs[2],x,y,z);
  };
  if(!range.native_build_chunked())hr=step(0,width,height,1);
  else{
   hr=step(1u<<16,record_width,record_height,1);
   if(hr==S_OK)hr=step((2u<<16)|range.build_chunks,width,height,range.build_chunks);
   if(hr==S_OK&&range.preparations)hr=step(3u<<16,record_width,record_height,1);
  }
  if(FAILED(hr))return hr;
  if(d3d12_not_done(hr)){if(hr==S_FALSE)d3d12_->omit("native packets (build pipeline not ready)",false);return S_OK;}
  d3d12_->count("native build dispatches");
 }
 }
 // Terrain 2.0 material cache: this frame's bake (after the terrain frame
 // values are uploaded), then bind_terrain2_d3d12 binds its views.
 update_terrain2_cache();
 update_terrain_face_cache(); // the face cache (terrain_face_atlas.h): its bake and feedback; bind_terrain2_d3d12 binds its views
 // The slim terrain pass (option terrain_slim; the D3D11 flush's rule): with this frame's material cache on.
 const int terrain_slim_drawn=terrain_slim_mode(debug_overlay_.config(),true);
 // (A deferring phase 1 draws no terrain material: its phase 2 takes the pass.)
 bool terrain_slim=terrain2_cache_on_&&!scene_composited_&&!hit_test_active_&&terrain_slim_drawn>0&&!defer;
 // Its mask (R32_UINT winner keys, terrainSlimKey), made when the pass is
 // first wanted, at the prepass targets' size (ensure_prepass_targets_d3d12
 // drops it when they change).
 if(terrain_slim&&!terrain_miss12_&&prepass_rest12_){
  UINT width=0,height=0,levels=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
  if(!d3d12_->texture_info(*prepass_rest12_,width,height,levels,format)||
     FAILED(d3d12_->create_storage_texture(width,height,0,DXGI_FORMAT_R32_UINT,terrain_miss12_)))terrain_miss12_.reset();
  terrain_miss_clear_needed_=true; // a new mask is cleared before use
 }
 // terrain_slim_stencil 2 (the list mark): the quads' stamps at half the
 // mask's size, made and cleared with the mask (the same sequence wrap), and
 // the quad list sized to the screen's quads (a quad is listed at most once
 // a flush, so the list cannot overflow). Unavailable: the full-screen mark
 // (1) draws instead, with the same image, and the reason is logged once.
 const int slim_mark_mode=configured_integer(debug_overlay_.config(),"terrain_slim_stencil",2);
 bool slim_quad_list=false;
 if(terrain_slim&&terrain_miss12_&&slim_mark_mode==2){
  UINT width=0,height=0,levels=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
  const char* failed=nullptr;
  if(!d3d12_->texture_info(*terrain_miss12_,width,height,levels,format))failed="terrain slim list mark: miss mask size unknown";
  else{
   const UINT quads_x=(width+1u)/2u,quads_y=(height+1u)/2u;
   if(!terrain_miss_quads12_){
    if(FAILED(d3d12_->create_storage_texture(quads_x,quads_y,0,DXGI_FORMAT_R32_UINT,terrain_miss_quads12_))){terrain_miss_quads12_.reset();failed="terrain slim list mark: quad stamp texture allocation failed";}
    else terrain_miss_clear_needed_=true; // stamps start at zero (with the mask)
   }
   if(!failed&&FAILED(native_d3d12_buffer(terrain_miss_quad_list12_,16u+UINT64(quads_x)*quads_y*4u,0,kD3D12BufferUnordered|kD3D12BufferIndirect)))
    failed="terrain slim list mark: quad list allocation failed";
  }
  if(failed){
   d3d12_->count(failed);
   if(!terrain_quad_list_failed_logged_){terrain_quad_list_failed_logged_=true;bridge_report_event("terrain_slim_list_mark",failed);}
  }else slim_quad_list=true;
 }
 // The per-triangle miss flags of this flush's visible triangles, cleared,
 // and the miss lists' entry slots (terrainMissOrigin).
 if(terrain_slim){
  const UINT64 words=(std::max)(1u,(layout.triangles+31u)/32u),slots=(std::max)(1u,layout.triangles);
  terrain_slim=SUCCEEDED(native_d3d12_buffer(terrain_miss_tris12_,words*4u,0,kD3D12BufferUnordered))&&terrain_miss_tris12_&&
   SUCCEEDED(native_d3d12_buffer(terrain_miss_origin12_,slots*4u,0,kD3D12BufferUnordered))&&terrain_miss_origin12_;
  if(terrain_slim)d3d12_->clear_buffer_uint(*terrain_miss_tris12_,0);
  // The mask: cleared only when the flush sequence wraps or a key could
  // outrank new ones (the D3D11 flush's rule).
  if(terrain_slim&&terrain_miss12_&&(terrain_miss_clear_needed_||terrain_slim_flush_>=128u)){
   const UINT zero[4]{};d3d12_->clear_texture_uint(*terrain_miss12_,zero);
   if(terrain_miss_quads12_)d3d12_->clear_texture_uint(*terrain_miss_quads12_,zero);
   terrain_miss_clear_needed_=false;terrain_slim_flush_=0;
  }
  // The list mark's draw arguments: 6 vertices (two triangles a quad), no quads yet.
  if(terrain_slim&&slim_quad_list){
   const UINT arguments[4]{6u,0u,0u,0u};
   slim_quad_list=SUCCEEDED(d3d12_->write_buffer(*terrain_miss_quad_list12_,0,arguments,sizeof(arguments)));
  }
 }
 // This flush's key sequence (terrainSlimKey; nativeLiquidLayer bits 24-30).
 const bool slim_keys=terrain_slim&&terrain_miss12_;
 const UINT slim_flush=slim_keys?terrain_slim_flush_++:0u;
 if(slim_keys)count_terrain_slim_flush();
 // The owned sky behind the frame's first world material (draw_owned_sky),
 // and in caves the daylight haze past each exit (draw_cave_exit_glow): both
 // one triangle at the far plane, depth tested, never written. A deferring
 // phase 1 draws depth and opaque depth-writing batches only (its early ones;
 // cover and critters follow it, opaque too), so the two wait for the first
 // flush that draws material (the kept sets' phase 2, or any other flush):
 // the prepass depth then rejects every covered pixel before its program
 // runs, where the first flush of the frame shaded the whole screen
 // (sky_draw then costs the visible sky). The pixels left are the same.
 // Option coverage_cuts without 1: in the frame's first flush, as before.
 const int coverage_cuts=configured_integer(debug_overlay_.config(),"coverage_cuts",63);
 if(!defer||!(coverage_cuts&1)){
  // Held back: the frame's world targets again (cover and the masks bound
  // theirs since; phase 2 binds its kept set's below).
  if(phase==2&&(sky_draw_pending_||cave_glow_pending_)&&world_d3d12_bound_&&!scene_composited_)bind_world_targets_d3d12();
  if(sky_draw_pending_)draw_owned_sky_d3d12();
  draw_cave_exit_glow_d3d12();
 }
 // The liquid frame (the D3D11 batch loop prepares it at its batches): the
 // ripple values and their wave steps, the generated maps, the swamp frame.
 prepare_liquid_ripples();
 (void)ensure_liquid_normal_map(); // unavailable: the water draws without them, as on D3D11
 bind_liquid_swamp_frame();
 if(!scene_composited_)ensure_native_shadow_range();
 // The interface batches: nativeVS and the object nativePS over the build's
 // outputs, each batch's fixed state and texture as the D3D11 flush binds them.
 d3d12_->clear_slots();
 d3d12_->set_constants(1,&native_world_lighting_,sizeof(native_world_lighting_));
 d3d12_->set_constants(3,clip.data(),UINT(clip.size()));
 d3d12_->set_resource(2,native12_.dynamic.get());
 d3d12_->set_resource(4,native12_.visible.get());
 d3d12_->set_resource(5,native12_.draws.get());
 d3d12_->set_resource(7,native12_.prepared.get());
 d3d12_->set_resource(40,indexed_draws?native12_.owners.get():nullptr); // nativeVertexOwners
 d3d12_->set_resource(8,native12_.world_vertices.get());
 d3d12_->set_resource(13,native12_.instances.get());
 d3d12_->set_resource(126,native12_.instances.get());
 d3d12_->set_resource(10,native12_.animation.get());
 d3d12_->set_resource(12,native12_.pose_nodes.get());
 d3d12_->set_resource(19,native12_.layer_shifts.get());
 d3d12_->set_resource(127,native12_.terrain_frame.get());d3d12_->set_resource(72,fire_burn_mask12());if(fire_burn_bound_)d3d12_->set_resource(74,fire_burn_materials12());
 // Snow trail field (nativeSnowTrailWindow.w is 0 without it; the D3D11
 // flush's t8 and t36): the terrain pixel and cliff tessellation stages read
 // it at kFieldSlotD3D12; the vertex stage at t36 sinks units into troughs
 // (nativeFigureTrailSink).
 const D3D12Texture* const snow_field=scene_composited_?nullptr:snow_terrain12_.get();
 d3d12_->set_resource(snow_trails::kFieldSlotD3D12,snow_field);
 d3d12_->set_resource(36,snow_field);
 // The rain shelter for wet and snowy surfaces (t70): the soft map, else the raw one (renderer_terrain.cpp rows).
 d3d12_->set_resource(70,!native_rain_valid_?nullptr:native_shelter_soft_valid_?rain12_.soft[0].get():rain12_.shelter.get());
 // The rain ring texture (t71) while the terrain frame reads it (renderer_terrain.cpp).
 d3d12_->set_resource(71,native_rain_rings_bound_?rain12_.rings.get():nullptr);
 // The snow surface for the object build-up (t73, s11; none: its drift relief only).
 d3d12_->set_resource(kSnowSurfaceSlot,snow_surface12_.get());
 d3d12_->set_sampler(kSnowSurfaceSampler,world_texture_sampler(D3DTFN_LINEAR,D3DTFG_LINEAR,D3DTFP_LINEAR,
  D3DTADDRESS_WRAP,D3DTADDRESS_WRAP,true,1u<<configured_integer(debug_overlay_.config(),"anisotropy",3)).description);
 // Crown impostor atlas (t44/t45, nativeImpostorTexel in nativePS and nativePrepassPS); null before any bake.
 d3d12_->set_resource(44,impostors_.albedo12.get());d3d12_->set_resource(45,impostors_.normal12.get());
 // Lights, fog and the noise lattice the world pixel variants read.
 d3d12_->set_constants(5,native12_.point_light_constants.data(),sizeof(native12_.point_light_constants));
 d3d12_->set_constants(10,&fog_frame_d3d12_,sizeof(fog_frame_d3d12_));
 bind_world_fog_volume_d3d12();
 d3d12_->set_resource(17,native_point_lights_.empty()?nullptr:native12_.point_light_grid.get());
 d3d12_->set_resource(18,native_point_lights_.empty()?nullptr:native12_.point_lights.get());
 d3d12_->set_resource(PointShadowTextureLayout::terrain_noise_srv,native12_.terrain_noise.get());
 bind_cave_irradiance_d3d12(); // the bounce light and the cave fog (t80-t84, s13)
 {D3D11_SAMPLER_DESC wrap{};wrap.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
  wrap.AddressU=wrap.AddressV=wrap.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;wrap.MaxLOD=0;d3d12_->set_sampler(12,wrap);}
 {D3D11_SAMPLER_DESC linear{};linear.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  linear.AddressU=linear.AddressV=linear.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;linear.MaxLOD=D3D11_FLOAT32_MAX;d3d12_->set_sampler(7,linear);}
 // The sun maps and their constants (b2, t9; the samplers as the D3D11 flush
 // sets them). Unbound (no pass yet), the constants read zero: no map valid.
 // A map left unwritten because its pass waited for a compiling pipeline
 // (flush_native_shadows_d3d12 wait) is not a contract failure: with the
 // spatial selection off its receivers read lit instead of the failure
 // magenta. A failed pass keeps the magenta.
 {NativeShadowConstants receivers=shadow12_.constants;
  if(!native_shadow_failed_&&native_shadows_enabled_)
   for(UINT slice=0;slice<5;++slice)
    if(native_shadow_frame_.slices[slice].enabled&&!(receivers.control[0]&(1u<<slice))){receivers.bias[4][2]=0;break;}
  d3d12_->set_constants(2,&receivers,sizeof(receivers));}
 d3d12_->set_resource(9,shadow12_.maps.get());
 d3d12_->set_resource(64,shadow12_.range.get());
 // The temporal sun-shadow history (b6, t20/t21, s3): read by every world
 // batch, written (slot 1) by the batches the D3D11 flush's write_history picks.
 const bool history_active=prepare_shadow_history();
 bind_shadow_history_d3d12(history_active);
 // The interface after composition (portraits, panel doll, HUD meshes) sits
 // in its own space, not the world's: no sun or cloud shadow, history, lamps
 // or bounce light (as its fog, bind_world_fog). With the world's b2 the
 // portrait faces sampled the sun maps at their HUD coordinates and flickered
 // as those maps changed. Zero blocks read as "nothing bound" (renderer_flush.cpp).
 if(scene_composited_){
  const NativeShadowConstants no_shadows{};
  const decltype(native12_.point_light_constants) no_lights{};
  const decltype(shadow_history_constants12_) no_history{};
  d3d12_->set_constants(2,&no_shadows,sizeof(no_shadows));
  d3d12_->set_constants(5,no_lights.data(),UINT(sizeof(no_lights)));
  d3d12_->set_constants(6,no_history.data(),UINT(sizeof(no_history)));
 }
 for(UINT page=0;page<PointShadowTextureLayout::array_bindings;++page)
  d3d12_->set_resource(PointShadowTextureLayout::first_srv+page,
   page<native_point_shadow_pages_.size()?native_point_shadow_pages_[page].depth12.get():nullptr);
 {D3D11_SAMPLER_DESC compare{};compare.Filter=D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
  compare.AddressU=compare.AddressV=compare.AddressW=D3D11_TEXTURE_ADDRESS_BORDER;
  compare.BorderColor[0]=compare.BorderColor[1]=compare.BorderColor[2]=compare.BorderColor[3]=1;
  compare.ComparisonFunc=D3D11_COMPARISON_LESS_EQUAL;compare.MaxLOD=D3D11_FLOAT32_MAX;
  d3d12_->set_sampler(1,compare);
  compare.Filter=D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
  d3d12_->set_sampler(2,compare);d3d12_->set_sampler(4,compare);
  D3D11_SAMPLER_DESC gather{};gather.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
  gather.AddressU=gather.AddressV=gather.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;gather.MaxLOD=D3D11_FLOAT32_MAX;
  d3d12_->set_sampler(10,gather);}
 bind_liquid_d3d12();
 // The owned sky's cloud shadow and probe (t110/t111 in the DXIL, world_fog.h).
 if(sky_inputs_active_){
  d3d12_->set_resource(kSkyCloudShadowSlotD3D12,sky12_.shadow.get());
  d3d12_->set_resource(kSkyProbeSlotD3D12,sky12_.probe[sky_index_].get());
 }
 // Terrain 2.0's map table and contact sampler (no entries while it is off).
 bind_terrain2_d3d12();
 // The terrain pixel variants (append_native_shader_programs: NATIVE_TERRAIN_PS,
 // and with NATIVE_TERRAIN_FLAT for the flat tile class); the general
 // combiner programs serve every combiner configuration (the D3D11 fast
 // variants compile the same results with the general path left out).
 // Material quality Off: the same programs without the material code
 // (append_native_off_material_programs).
 const bool off_materials=!material_level();
 // The programs by entry and define names (NativeD3D12::by_defines, resolved once).
 const auto find_program=[&](const char* entry,std::vector<const char*> names)->const ShaderProgram*{
  const auto found=native12_.by_defines.find(native_program_key(entry,std::move(names)));
  return found==native12_.by_defines.end()?nullptr:found->second;
 };
 const auto pixel_program=[&](std::initializer_list<const char*> names)->const ShaderProgram*{
  std::vector<const char*> list(names);
  if(off_materials)list.push_back("NATIVE_OFF_MATERIALS");
  return find_program("nativePS",std::move(list));
 };
 const ShaderProgram* const object_ps=pixel_program({});
 const ShaderProgram* const gltf_ps=pixel_program({"NATIVE_GLTF"});
 const ShaderProgram* const gltf_masked_ps=pixel_program({"NATIVE_GLTF","NATIVE_SHADOW_MASK"});
 const ShaderProgram* const gltf_motion_ps=pixel_program({"NATIVE_GLTF","NATIVE_OBJECT_MOTION"});
 const ShaderProgram* const gltf_motion_masked_ps=pixel_program({"NATIVE_GLTF","NATIVE_OBJECT_MOTION","NATIVE_SHADOW_MASK"});
 // rt_ray_reconstruction: an opaque or cut-out world batch draws the guide
 // twin of its pixel program (NATIVE_RR_GUIDES) with the guide targets bound
 // at 5-7 for that draw; blended batches, the interface and programs without
 // a twin (liquid surfaces) draw as always and leave the guides behind them.
 const bool guide_flush=guide_frame_d3d12()&&world_d3d12_bound_&&!scene_composited_&&!hit_test_active_&&!native12_.guide_twins.empty();
 // rt_direct_lights: a guide twin of a traced frame draws with an empty lamp
 // grid and no glow lights (its lamp loop runs zero lights; the count stays,
 // so its lighting takes the same branch); direct_lights_d3d12 adds its lamp
 // light. Every other draw keeps the lamps.
 const bool lamps_traced=rt_direct12_.traced_frame==native_frame_index_&&!scene_composited_&&!hit_test_active_;
 // rt_indirect_light: a guide twin of a traced frame draws with the bounce
 // volume's strength 0 (caveVolumeStrength, the block's word 11);
 // indirect_light_d3d12 adds its bounce light. Every other draw keeps the volume.
 const bool bounce_traced=rt_indirect12_.traced_frame==native_frame_index_&&!scene_composited_&&!hit_test_active_;
 // rt_reflections: a guide twin of a traced frame draws with the roughness cut-off in the lamp constants'
 // spare word and leaves the share under it of its environment reflection to reflections_d3d12.
 const bool reflections_traced=rt_reflect12_.traced_frame==native_frame_index_&&!scene_composited_&&!hit_test_active_;
 const auto twin_lamps=[&](bool twin){
  if(!lamps_traced&&!bounce_traced&&!reflections_traced)return;
  auto block=native12_.point_light_constants;
  if(twin&&lamps_traced)block[1]=block[2]=block[3]=0;
  if(twin&&bounce_traced)block[11]=0;
  if(twin&&reflections_traced)std::memcpy(&block[7],&rt_reflect12_.cutoff,sizeof(float)); // nativePointLightGridTail
  d3d12_->set_constants(5,block.data(),sizeof(block));
 };
 // blended: a draw of the opaque or cut-out class whose state still blends
 // (a unit the game draws with its blend on) takes its twin too; the caller
 // draws it with the guide targets unblended (kCustomGuideTargetsOpaque).
 const auto guide_twin=[&](const ShaderProgram& pixel,const NativeStateBucket& bucket,const D3D12NativeState& state,bool blended=false)->const ShaderProgram*{
  if(!guide_flush)return nullptr;
  // Why a world draw of a guide frame writes no guides (session counts in host.log).
  if(bucket.opacity==NativeOpacityClass::ordered){d3d12_->count("world draws without guides: ordered (blended in order)");return nullptr;}
  if(state.custom_blend.custom||(!blended&&fixed_blend_desc(state.blend).RenderTarget[0].BlendEnable)){d3d12_->count("world draws without guides: blend enabled");return nullptr;}
  const auto found=native12_.guide_twins.find(&pixel);
  if(found==native12_.guide_twins.end()){d3d12_->count("world draws without guides: their program has no twin");return nullptr;}
  return found->second;
 };
 // The astral rift programs (NATIVE_ASTRAL): batches of astral records only.
 const ShaderProgram* const astral_vs=find_program("nativeVS",{"NATIVE_ASTRAL"});
 const ShaderProgram* const astral_ps=pixel_program({"NATIVE_ASTRAL"});
 // Material instances (renderer.h kNativeInstance*; the D3D11 flush's rule):
 // world figure and object batches draw their instance's program (object,
 // prepassed, and each with the object motion), F10 shader_split_check the
 // check programs; shader_split 0 (debug comparison) the general ones.
 // Material quality Off keeps its own general programs (no material code to split).
 const int split_check_mode=configured_integer(debug_overlay_.config(),"shader_split_check",0);
 // F10 fur_debug draws through the check programs too (only they carry its code).
 const bool split_check=split_check_mode>0||fur_debug_!=0;
 const bool split=!off_materials&&(split_check||configured_boolean(debug_overlay_.config(),"shader_split",true));
 // slot*2+masked, then the motion programs, then both again without the contact blend (not units),
 // then all four groups again for dry frames (NATIVE_FRAME_DRY). A group without a program for a slot
 // (the check programs, units without the contact blend) is never selected for it.
 static const std::array<const ShaderProgram*,kNativeInstancePrograms*8u> no_instance_programs{};
 const auto& instance_programs=split?native12_.instance_pixel[split_check?1:0]:no_instance_programs;
 // The terrain frame of this flush (uploaded above) holds wet level 0 and snow cover 0: its instance
 // batches draw the dry-frame programs (F10 object_program_cuts 0: the full ones, the same image).
 const int program_cuts=configured_integer(debug_overlay_.config(),"object_program_cuts",3);
 const bool dry_frame=native_frame_dry_&&!split_check&&(program_cuts&1)!=0;
 // Their vertex programs by class: colour, prepass, object motion.
 static const std::array<const ShaderProgram*,kNativeInstanceVertexClasses*3u> no_vertex_programs{};
 const auto& instance_vertex=split?native12_.instance_vertex[split_check?1:0]:no_vertex_programs;
 // Their indexed twins (append_native_indexed_programs), drawn by a layout's indexed batches while indexed_draws.
 const auto& indexed_vertex=indexed_draws?native12_.indexed_vertex:no_vertex_programs;
 // With a trail field: the snow trail variants (NATIVE_SNOW_TRAILS), as the D3D11 flush picks them.
 const ShaderProgram* const terrain_ps=snow_field?pixel_program({"NATIVE_TERRAIN_PS","NATIVE_SNOW_TRAILS"}):pixel_program({"NATIVE_TERRAIN_PS"});
 const ShaderProgram* const terrain_flat_ps=snow_field?pixel_program({"NATIVE_TERRAIN_PS","NATIVE_SNOW_TRAILS","NATIVE_TERRAIN_FLAT"}):
  pixel_program({"NATIVE_TERRAIN_PS","NATIVE_TERRAIN_FLAT"});
 // The liquid programs (append_native_shader_programs' nativeLiquidPS variants).
 const auto liquid_program=[&](std::initializer_list<const char*> names)->const ShaderProgram*{
  return find_program("nativeLiquidPS",std::vector<const char*>(names));
 };
 const ShaderProgram* const water_low_ps=liquid_program({"NATIVE_LIQUID_BODY","NATIVE_LIQUID_WATER","NATIVE_LOW"});
 const ShaderProgram* const liquid_low_ps=liquid_program({"NATIVE_LIQUID_BODY","NATIVE_LOW"});
 const ShaderProgram* const lava_ps=liquid_program({"NATIVE_LIQUID_BODY","NATIVE_LIQUID_LAVA"});
 const ShaderProgram* const foam_ps=liquid_program({"NATIVE_LIQUID_FOAM"});
 const bool water_simple=configured_boolean(debug_overlay_.config(),"water_simple",false);
 // With a reflection frame (prepare_water_reflections): the full, the
 // no-physical and the half-resolution trace programs.
 const ShaderProgram* const water_ps=liquid_program({"NATIVE_LIQUID_BODY","NATIVE_LIQUID_WATER"});
 // The full bodies with the swimmer lookup: drawn only while the depth snapshot carries swimmer marks.
 const ShaderProgram* const water_swim_ps=liquid_program({"NATIVE_LIQUID_BODY","NATIVE_LIQUID_WATER","NATIVE_LIQUID_SWIMMERS"});
 const ShaderProgram* const liquid_swim_ps=liquid_program({"NATIVE_LIQUID_BODY","NATIVE_LIQUID_SWIMMERS"});
 const ShaderProgram* const water_ssr_ps=liquid_program({"NATIVE_LIQUID_BODY","NATIVE_LIQUID_WATER","NATIVE_LIQUID_NO_PHYSICAL"});
 const ShaderProgram* const liquid_ps=liquid_program({"NATIVE_LIQUID_BODY"});
 const ShaderProgram* const liquid_ssr_ps=liquid_program({"NATIVE_LIQUID_BODY","NATIVE_LIQUID_NO_PHYSICAL"});
 const ShaderProgram* const ssr_input_ps=liquid_program({"NATIVE_LIQUID_BODY","NATIVE_LIQUID_SSR_INPUT"});
 const auto plain_program=[&](const char* entry)->const ShaderProgram*{
  return find_program(entry,{}); // no list has no names but nullptr
 };
 const ShaderProgram* const ssr_march_ps=plain_program("nativeLiquidSsrPS");
 const ShaderProgram* const ssr_filter_ps=plain_program("nativeLiquidSsrFilterPS");
 const ShaderProgram* const full_screen_vs=plain_program("nativeShadowMaskVS");
 const bool water_physical=configured_boolean(debug_overlay_.config(),"water_physical",true);
 bool water_ssr_done=false,crystal_bound=false;
 D3D11_SAMPLER_DESC water_sampler{};water_sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
 water_sampler.AddressU=water_sampler.AddressV=water_sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;water_sampler.MaxLOD=D3D11_FLOAT32_MAX;
 // A batch's wading units (the D3D11 bind_batch_units): only those that can
 // reach its water; a neighbour map's water keeps the frame's full set.
 // A reused bake has no queue: its per-batch totals stand in for the draw walks.
 const auto baked_batch=[&](std::size_t index){return static_reuse?&bake.batches[index]:nullptr;};
 const auto bind_batch_units=[&](const NativeBatch& item,const NativeSubmissionLayout::StaticBake::Batch* baked){
  bool neighbour=false;
  float lo[2]{std::numeric_limits<float>::max(),std::numeric_limits<float>::max()},hi[2]{-std::numeric_limits<float>::max(),-std::numeric_limits<float>::max()};
  if(baked){
   neighbour=baked->neighbour;
   for(int axis=0;axis<2;++axis){lo[axis]=baked->xy_min[axis];hi[axis]=baked->xy_max[axis];}
  }else for(std::size_t n=item.first_draw;n<item.first_draw+item.draw_count;++n){
   const auto& queued=native_queued_draw(native_draw_order_[n]);
   neighbour|=(queued.instance.flags&native_neighbour_flag)!=0;
   for(int axis=0;axis<2;++axis){lo[axis]=std::min(lo[axis],queued.bounds_min[axis]);hi[axis]=std::max(hi[axis],queued.bounds_max[axis]);}
  }
  if(!neighbour)bind_liquid_ripple_units(lo,hi);
 };
 const bool water_reflections_wanted=configured_boolean(debug_overlay_.config(),"water_reflections",true);
 // Cliff tessellation (the D3D11 flush's choice): with a snow trail field the
 // hull and domain stages with the trail code (nativeTerrainHS/DS: the trail
 // groove and berm displacement), else the plain ones (exact: the trail terms
 // need nativeSnowTrailWindow.w > 0, set only with a field).
 const bool tessellate_terrain=!scene_composited_&&terrain_tessellation_frame_==native_frame_index_&&native12_.tessellation.size()>=5;
 const ShaderProgram* const terrain_hull=tessellate_terrain?&native12_.tessellation[snow_field?1:3]:nullptr;
 const ShaderProgram* const terrain_domain=tessellate_terrain?&native12_.tessellation[snow_field?2:4]:nullptr;
 const bool neighbour_terrain_blend=configured_boolean(debug_overlay_.config(),"terrain_blend_neighbours",true);
 const bool neighbour_fog_skip=configured_boolean(debug_overlay_.config(),"neighbour_fog_skip",true);
 const int depth_check=configured_integer(debug_overlay_.config(),"prepass_depth_check",0);
 // Depth prepass and sun-shadow mask (the D3D11 flush's, native_shader.h
 // nativePrepassPS and nativeShadowMaskPS): batches whose depth is final
 // before shading draw depth and the receiver record first; the mask pass
 // takes their pixels' sun and cloud shadow once (only the pixels the
 // prepass marked in stencil: mark_receiver_stencil); their material pass then
 // tests EQUAL against that depth (the D3D11 flush's masked_depth_state_:
 // only the visible surface passes, so a cutout's alpha-rejected texels in
 // front of it never shade) and reads the mask (NATIVE_SHADOW_MASK
 // variants). Both passes take the position from the same precise
 // nativeVertex. The receiver record is cleared before each prepass
 // (begin_world_prepass_d3d12) and the mask pass clears its stencil marks,
 // so it never relaunches on a pixel an earlier flush marked.
 const ShaderProgram* const prepass_vs=plain_program("nativePrepassVS");
 // Upscaler object motion in the material pass (append_native_motion_programs):
 // batches with a posed or swaying record draw through these and also write
 // the object motion targets 3 and 4.
 // (The general one, NATIVE_OBJECT_MOTION alone: the list also holds the instance classes' and their check and indexed twins.)
 const ShaderProgram* const motion_vs=find_program("nativeMotionWorldVS",{"NATIVE_OBJECT_MOTION"});
 const ShaderProgram* const motion_object_ps=pixel_program({"NATIVE_OBJECT_MOTION"});
 const ShaderProgram* const motion_masked_ps=pixel_program({"NATIVE_OBJECT_MOTION","NATIVE_SHADOW_MASK"});
 const ShaderProgram* const general_prepass_ps=plain_program("nativePrepassPS");
 // rt_sun_shadows: the prepass of traced sun frames (kRtPrepass); the plain
 // one while its pipeline is not built.
 const ShaderProgram* const rt_prepass_ps=native_traced_sun_&&rt12_.programs.size()>kRtPrepass?&rt12_.programs[kRtPrepass]:nullptr;
 // Material instance batches (no terrain records): the prepass program without the terrain receiver code.
 const ShaderProgram* const object_prepass_ps=configured_boolean(debug_overlay_.config(),"prepass_object_program",true)?
  find_program("nativePrepassPS",{"NATIVE_PREPASS_OBJECT"}):nullptr;
 const ShaderProgram* const masked_object_ps=pixel_program({"NATIVE_SHADOW_MASK"});
 const ShaderProgram* const masked_terrain_ps=snow_field?pixel_program({"NATIVE_SHADOW_MASK","NATIVE_TERRAIN_PS","NATIVE_SNOW_TRAILS"}):
  pixel_program({"NATIVE_SHADOW_MASK","NATIVE_TERRAIN_PS"});
 const ShaderProgram* const masked_terrain_flat_ps=snow_field?pixel_program({"NATIVE_SHADOW_MASK","NATIVE_TERRAIN_PS","NATIVE_SNOW_TRAILS","NATIVE_TERRAIN_FLAT"}):
  pixel_program({"NATIVE_SHADOW_MASK","NATIVE_TERRAIN_PS","NATIVE_TERRAIN_FLAT"});
 // The slim terrain pass's programs (NATIVE_TERRAIN_SLIM; none with material quality Off).
 const ShaderProgram* const slim_terrain_ps=snow_field?pixel_program({"NATIVE_SHADOW_MASK","NATIVE_TERRAIN_PS","NATIVE_SNOW_TRAILS","NATIVE_TERRAIN_SLIM"}):
  pixel_program({"NATIVE_SHADOW_MASK","NATIVE_TERRAIN_PS","NATIVE_TERRAIN_SLIM"});
 const ShaderProgram* miss_compact_cs=nullptr; // the slim pass's miss lists (nativeTerrainMissCompactCS)
 for(const auto& program:programs)if(std::strcmp(program.entry,"nativeTerrainMissCompactCS")==0){miss_compact_cs=&program;break;}
 const ShaderProgram* const slim_terrain_flat_ps=snow_field?pixel_program({"NATIVE_SHADOW_MASK","NATIVE_TERRAIN_PS","NATIVE_SNOW_TRAILS","NATIVE_TERRAIN_FLAT","NATIVE_TERRAIN_SLIM"}):
  pixel_program({"NATIVE_SHADOW_MASK","NATIVE_TERRAIN_PS","NATIVE_TERRAIN_FLAT","NATIVE_TERRAIN_SLIM"});
 // Option terrain_slim_cuts (the same image; 0 draws the slim programs above):
 // bit 1 the slim pass of a batch of plain terrain records
 // (NativeBatch::terrain_records) draws the NATIVE_TERRAIN_LEAN programs, bit 2
 // their missed quads leave before the lighting (layer bit 512).
 const int slim_cuts=configured_integer(debug_overlay_.config(),"terrain_slim_cuts",3);
 const ShaderProgram* const lean_terrain_ps=!(slim_cuts&1)?nullptr:
  snow_field?pixel_program({"NATIVE_SHADOW_MASK","NATIVE_TERRAIN_PS","NATIVE_SNOW_TRAILS","NATIVE_TERRAIN_SLIM","NATIVE_TERRAIN_LEAN"}):
  pixel_program({"NATIVE_SHADOW_MASK","NATIVE_TERRAIN_PS","NATIVE_TERRAIN_SLIM","NATIVE_TERRAIN_LEAN"});
 const ShaderProgram* const lean_terrain_flat_ps=!(slim_cuts&1)?nullptr:
  snow_field?pixel_program({"NATIVE_SHADOW_MASK","NATIVE_TERRAIN_PS","NATIVE_SNOW_TRAILS","NATIVE_TERRAIN_FLAT","NATIVE_TERRAIN_SLIM","NATIVE_TERRAIN_LEAN"}):
  pixel_program({"NATIVE_SHADOW_MASK","NATIVE_TERRAIN_PS","NATIVE_TERRAIN_FLAT","NATIVE_TERRAIN_SLIM","NATIVE_TERRAIN_LEAN"});
 // append_terrain_tessellation_shader_programs [5] nativePrepassTessVS, [6] HS, [7] snow HS, [8] DS, [9] snow DS.
 const bool prepass_tessellation=native12_.tessellation.size()>=10;
 const bool prepass_ready=depth_prepass_d3d12()&&world_d3d12_bound_&&!scene_composited_&&
  !hit_test_active_&&fog_camera_valid_&&prepass_vs&&general_prepass_ps&&full_screen_vs&&masked_object_ps&&masked_terrain_ps&&masked_terrain_flat_ps&&
  SUCCEEDED(ensure_prepass_targets_d3d12());
 // Per batch: 0 not prepassed, 1 its depth is drawn (material EQUAL),
 // 2 drawn and the mask is ready (the masked program).
 // prepass_depth_check (diagnostic): after each prepassed batch's material
 // draw, the batch again with no colour but red where its unrejected
 // fragments lie in front of the prepass depth (1: what EQUAL loses and
 // LESS_EQUAL would shade; 2: every fragment, calibrating the marker; 3:
 // no marker, the mask pass's stencil test inverted, calibrating its check).
 std::vector<std::uint8_t> prepassed;
 try{prepassed.assign(layout.batches.size(),0);}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
 // The ray tracing scene's units of the next frame: the unit records this
 // world flush draws posed (their world vertices are prepared by it).
 if(phase!=2&&!static_reuse&&rt12_.frame==native_frame_index_&&world_d3d12_bound_&&!scene_composited_&&!hit_test_active_)try{
  for(const auto& batch:layout.batches){
   if(!batch.posed||native_state_buckets_[batch.state_bucket].opacity==NativeOpacityClass::ordered)continue;
   for(std::size_t n=batch.first_draw;n<batch.first_draw+batch.draw_count;++n){
    const UINT record=native_pending_draws_[native_draw_order_[n]].record;
    const auto& draw=native_instance_records_[record].command;
    if((draw.instance.flags&native_unit_flag)&&draw.instance.pose_node!=UINT_MAX&&draw.mesh)
     rt12_.figures_next.push_back({record,draw.mesh,draw.world_base,draw.world_generation});
   }
  }
 }catch(const std::bad_alloc&){rt12_.figures_next.clear();}
 bool any_prepass=false,any_unposed=false;
 for(std::size_t batch_index=0;phase!=2&&prepass_ready&&batch_index<layout.batches.size();++batch_index){
  const NativeBatch& batch=layout.batches[batch_index];
  const NativeStateBucket& bucket=native_state_buckets_[batch.state_bucket];
  // Posed batches (characters, animated figures) too: both passes read the
  // same compute-prepared world vertices through nativeVertex, so their
  // material pass meets the prepass depth (EQUAL) and reads the mask.
  if(!interface_batch(batch)||bucket.opacity==NativeOpacityClass::ordered)continue;
  const auto* baked=baked_batch(batch_index);
  const NativeQueuedDraw& draw=baked?native_instance_records_[baked->first_record].command:native_queued_draw(native_draw_order_[batch.first_draw]);
  if(draw.hud_composition.opacity<1.f)continue;
  const FixedState& st=bucket.state;
  const bool blend=st.render[D3DRENDERSTATE_ALPHABLENDENABLE]!=FALSE;
  const bool overwrite=!blend||(st.render[D3DRENDERSTATE_SRCBLEND]==D3DBLEND_ONE&&st.render[D3DRENDERSTATE_DESTBLEND]==D3DBLEND_ZERO);
  if(bucket.opacity==NativeOpacityClass::cutout&&!overwrite)continue;
  if(st.render[D3DRENDERSTATE_ZENABLE]==D3DZB_FALSE||!st.render[D3DRENDERSTATE_ZWRITEENABLE])continue;
  const DWORD depth_func=st.render[D3DRENDERSTATE_ZFUNC];
  if(depth_func!=D3DCMP_LESS&&depth_func!=D3DCMP_LESSEQUAL)continue;
  const bool terrain=bucket.semantic_kind==SemanticKind::terrain;
  if(terrain&&(st.render[D3DRENDERSTATE_ALPHATESTENABLE]!=FALSE||(tessellate_terrain&&!prepass_tessellation)))continue;
  bool liquid=baked&&baked->liquid;
  for(std::size_t n=batch.first_draw;!baked&&terrain&&!liquid&&n<batch.first_draw+batch.draw_count;++n)
   liquid=native_queued_draw(native_draw_order_[n]).mesh->has_liquid;
  if(liquid)continue;
  // F10 fur_debug hides unit layers in the material pass (the check
  // programs' discard), which the shipping prepass program cannot.
  if(fur_debug_&&((batch.material_instance&7u)==1u||(batch.material_instance&7u)==kNativeCrystalUnitDomain))continue;
  prepassed[batch_index]=1;any_prepass=true;any_unposed|=!batch.posed;
 }
 // A flush whose only eligible batches are posed (a characters-only flush)
 // keeps them unmasked: its own prepass would add a receiver clear and a
 // whole mask pass for a few pixels (laptop: a 4th shadow_mask call).
 // Under rt_sun_shadows they are prepassed: their sun shadow is the traced
 // mask's, as on every other opaque surface (the sun maps are reduced).
 if(any_prepass&&!any_unposed&&rt12_.shadow_frame!=native_frame_index_){std::fill(prepassed.begin(),prepassed.end(),std::uint8_t(0));any_prepass=false;}
 // Phase 2: the kept material's prepass results and history binding (its
 // prepass and mask ran in phase 1; the mask is still the frame's mask at
 // every pixel this set shows: a later mask writes only the pixels its own
 // prepass marked, which a later surface covers).
 if(phase==2){
  prepassed=kept_entry->prepassed;
  for(const auto value:prepassed)any_prepass|=value!=0;
  if(std::find(prepassed.begin(),prepassed.end(),std::uint8_t(2))!=prepassed.end())
   d3d12_->set_resource(kShadowMaskSlotD3D12,shadow_mask12_.get());
  bind_world_targets_d3d12(kept_entry->history);
 }
 if(phase!=2&&any_prepass){
  const bool history=world_history_bound12_;
  begin_world_prepass_d3d12();
  transition_profile_pass("depth_prepass");
  const auto& stages=native12_.tessellation;
  const ShaderProgram* const prepass_hull=prepass_tessellation?&stages[snow_field?7:6]:nullptr;
  const ShaderProgram* const prepass_domain=prepass_tessellation?&stages[snow_field?9:8]:nullptr;
  // Cut-out batches (alpha-tested: crowns, foliage, their impostors) draw
  // after every other prepassed batch, so the opaque depth already there
  // rejects their hidden fragments before the texture read and discard.
  // Every prepassed batch tests LESS or LESSEQUAL, so the depth and the
  // receiver marks are the same in either order (only a fragment at exactly
  // the depth of another batch's can leave a different receiver record).
  // prepass_cutouts_last 0 (debug comparison): build order.
  const bool cutouts_last=configured_boolean(debug_overlay_.config(),"prepass_cutouts_last",true);
  for(int round=0;round<(cutouts_last?2:1);++round)
  for(std::size_t batch_index=0;batch_index<layout.batches.size();++batch_index){
   if(!prepassed[batch_index])continue;
   const NativeBatch& batch=layout.batches[batch_index];
   const NativeStateBucket& bucket=native_state_buckets_[batch.state_bucket];
   if(cutouts_last&&(bucket.opacity==NativeOpacityClass::cutout||bucket.state.render[D3DRENDERSTATE_ALPHATESTENABLE]!=FALSE)!=(round==1))continue;
   const auto* baked=baked_batch(batch_index);
   const NativeQueuedDraw& draw=baked?native_instance_records_[baked->first_record].command:native_queued_draw(native_draw_order_[batch.first_draw]);
   const FixedState& st=bucket.state;
   ID3D11ShaderResourceView* contact_views[4]{};
   Constants constants;native_batch_constants(batch,draw,bucket,contact_views,neighbour_terrain_blend,neighbour_fog_skip,shadow_coverage,&constants);
   d3d12_->set_constants(0,&constants,sizeof(constants));
   d3d12_->set_resource(1,native_geometry_pages_[batch.geometry_page].d3d12.get());
   D3D12NativeState state;
   state.raster=fixed_raster_desc(st.render[D3DRENDERSTATE_CULLMODE],st.render[D3DRENDERSTATE_FILLMODE]==D3DFILL_WIREFRAME,true);
   state.depth=fixed_depth_desc(D3DZB_TRUE,TRUE,st.render[D3DRENDERSTATE_ZFUNC]);
   mark_receiver_stencil(state.depth);
   state.blend=fixed_blend_choice(FALSE,D3DBLEND_ONE,D3DBLEND_ZERO,false);
   d3d12_->set_sampler(0,world_texture_sampler(st.stage[0][D3DTSS_MINFILTER],st.stage[0][D3DTSS_MAGFILTER],st.stage[0][D3DTSS_MIPFILTER],
    st.stage[0][D3DTSS_ADDRESSU],st.stage[0][D3DTSS_ADDRESSV],true,1u<<configured_integer(debug_overlay_.config(),"anisotropy",3)).description);
   profiler_.add_draw(0,batch.draw_count);profiler_.add_batch();
   // The material pass's regions and stages (below), so both passes meet the same depth.
   const bool terrain=bucket.semantic_kind==SemanticKind::terrain;
   const bool tessellated_batch=terrain&&tessellate_terrain&&bucket.material_key.contact_map==0;
   bool drawn=true;
   // A material instance batch: the prepass program of its colour draw's vertex class (depth EQUAL there).
   const ShaderProgram* const instance_prepass_vs=split&&!terrain&&batch.material_instance?
    instance_vertex[native_instance_vertex_class(batch.material_instance)*3u+1u]:nullptr;
   // Its program has no terrain records (NATIVE_VS_TERRAIN false), so no
   // fragment is lifted (flag 4) and the mask pass never reads its terrain
   // normal: target 1 is not written (4 B a fragment less).
   // An indexed batch (NativeBatch::indexed): the indexed prepass program over
   // its index triples, which start at its first visible triangle's slot.
   // The build wrote it no visible triangles: without its program it is
   // left out (named), and its material draw too.
   const bool indexed_batch=indexed_draws&&batch.indexed;
   const ShaderProgram* const indexed_prepass_vs=indexed_batch&&instance_prepass_vs?
    indexed_vertex[native_instance_vertex_class(batch.material_instance)*3u+1u]:nullptr;
   if(instance_prepass_vs)state.custom_blend.custom=D3D12NativeState::kCustomTarget1Off;
   const ShaderProgram* const prepass_ps=instance_prepass_vs&&object_prepass_ps?object_prepass_ps:general_prepass_ps;
   const auto depth_draw=[&](UINT offset,bool tessellate)->HRESULT{
    if(indexed_batch&&!indexed_prepass_vs){drawn=false;d3d12_->omit("world batch (indexed vertex program missing)",true);return S_FALSE;}
    const auto draw_with=[&](const ShaderProgram& pixel)->HRESULT{
     if(indexed_prepass_vs){
      d3d12_->set_index_buffer(native12_.indices.get(),UINT64(batch.output_triangle_base)*12u);
      const HRESULT result=d3d12_->draw_indexed_indirect(*indexed_prepass_vs,pixel,state,nullptr,*native12_.arguments,offset);
      d3d12_->set_index_buffer(nullptr);
      return result;
     }
     return tessellate?
      d3d12_->draw_indirect(stages[5],pixel,state,nullptr,*native12_.arguments,offset,prepass_hull,prepass_domain):
      d3d12_->draw_indirect(instance_prepass_vs?*instance_prepass_vs:*prepass_vs,pixel,state,nullptr,*native12_.arguments,offset);
    };
    HRESULT result=rt_prepass_ps?draw_with(*rt_prepass_ps):S_FALSE;
    if(rt_prepass_ps&&result==S_FALSE)d3d12_->count("depth prepass batches without the traced sun receiver (pipeline not ready)");
    if(!rt_prepass_ps||result==S_FALSE)result=draw_with(*prepass_ps);
    if(result==S_FALSE){drawn=false;d3d12_->omit("depth prepass batch (pipeline not ready: unmasked shading)",false);}
    return result;
   };
   if(!terrain){
    if(FAILED(hr=depth_draw(batch_arguments(batch,batch_index),false)))return hr;
   }else{
    if(tessellated_batch&&batch.flat_pixels&&snow_field){
     for(const UINT layer_bits:{16u,32u}){
      Constants part=constants;part.liquid_layer|=layer_bits;
      d3d12_->set_constants(0,&part,sizeof(part));
      if(FAILED(hr=depth_draw(static_cast<UINT>(batch_index)*16u,layer_bits==32u)))return hr;
     }
    }else if(FAILED(hr=depth_draw(static_cast<UINT>(batch_index)*16u,tessellated_batch&&!batch.flat_pixels)))return hr;
    if(batch.steep_args_slot!=UINT_MAX){
     Constants steep=constants;steep.native_triangle_base=batch.steep_triangle_base;
     d3d12_->set_constants(0,&steep,sizeof(steep));
     if(FAILED(hr=depth_draw(batch.steep_args_slot*16u,tessellated_batch)))return hr;
    }
   }
   // A part left undrawn (its pipeline compiling) leaves the depth and the
   // receiver record incomplete: the batch shades unmasked with its own depth
   // test and write, as if not prepassed.
   if(!drawn)prepassed[batch_index]=0x81;
  }
  const HRESULT masked=resolve_world_shadow_mask_d3d12();
  if(FAILED(masked)&&masked!=E_FAIL)return masked;
  if(masked==S_OK)for(auto& value:prepassed)if(value==1)value=2;
  for(auto& value:prepassed)if(value==0x81)value=0;
  bind_world_targets_d3d12(history);
 }
 // Phase 1: the material waits (resume_deferred_world_d3d12). A batch that
 // writes depth in the material pass without a prepass would be missing
 // from the later masks' and the HiZ's depth. An opaque one (alpha-tested
 // terrain, unblended cutouts) draws now instead, before the kept material:
 // where it is in front the kept material's EQUAL fails as it would have
 // been overwritten, where it is behind it fails its own LESS either way.
 // A blended or liquid one needs the colour under it first:
 // native_deferred_depth_writer_ asks the caller to draw the kept materials
 // at once (today's order).
 // material_pass: 0 every batch; 1 (phase 1) only the early ones; 2 (phase 2) all but those.
 int material_pass=phase==2?2:0;
 std::vector<std::uint8_t> early;
 if(phase==2)early=kept_entry->early;
 if(defer){
  try{early.assign(layout.batches.size(),0);}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
  for(std::size_t index=0;index<layout.batches.size();++index){
   const NativeBatch& batch=layout.batches[index];
   if(prepassed[index]||!interface_batch(batch))continue;
   const NativeStateBucket& bucket=native_state_buckets_[batch.state_bucket];
   const FixedState& st=bucket.state;
   if(st.render[D3DRENDERSTATE_ZENABLE]==D3DZB_FALSE||!st.render[D3DRENDERSTATE_ZWRITEENABLE])continue;
   const auto* baked=baked_batch(index);
   bool liquid=baked&&baked->liquid;
   for(std::size_t n=batch.first_draw;!baked&&bucket.semantic_kind==SemanticKind::terrain&&!liquid&&n<batch.first_draw+batch.draw_count;++n)
    liquid=native_queued_draw(native_draw_order_[n]).mesh->has_liquid;
   const NativeQueuedDraw& draw=baked?native_instance_records_[baked->first_record].command:native_queued_draw(native_draw_order_[batch.first_draw]);
   const bool blend=st.render[D3DRENDERSTATE_ALPHABLENDENABLE]!=FALSE;
   const bool overwrite=!blend||(st.render[D3DRENDERSTATE_SRCBLEND]==D3DBLEND_ONE&&st.render[D3DRENDERSTATE_DESTBLEND]==D3DBLEND_ZERO);
   if(!liquid&&overwrite&&draw.hud_composition.opacity>=1.f){early[index]=1;d3d12_->count("deferred world material: opaque batch without a prepass drawn first");continue;}
   native_deferred_depth_writer_=true;
   d3d12_->count(liquid?"deferred world material: set with a liquid batch drawn at once":"deferred world material: set with a blended depth writer drawn at once");
  }
  try{
   DeferredWorldFlush12 entry;entry.slot=layout_slot;entry.history=world_history_bound12_;entry.prepassed=prepassed;entry.early=early;
   entry.has_frame=native_static_frame_!=nullptr;if(entry.has_frame)entry.frame=*native_static_frame_;
   entry.clip=clip;
   deferred_world12_.push_back(std::move(entry));
  }catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
  native_flush_deferred_=true;
  d3d12_->count(layout_slot==3?"deferred world material: terrain set kept":layout_slot==4?"deferred world material: neighbour set kept":"deferred world material: static set kept");
  material_pass=1;
 }

 // The slim pass by runs (the D3D11 batch loop's rule): consecutive slim
 // batches draw their slim passes, then one compaction builds all their miss
 // lists, then their second passes, in batch order.
 const auto slim_candidate=[&](std::size_t index){
  const NativeBatch& candidate=layout.batches[index];
  const NativeStateBucket& candidate_bucket=native_state_buckets_[candidate.state_bucket];
  return terrain_slim&&terrain_miss12_&&slim_terrain_ps&&slim_terrain_flat_ps&&miss_compact_cs&&interface_batch(candidate)&&
   prepassed[index]==2&&candidate_bucket.semantic_kind==SemanticKind::terrain&&
   candidate.miss_list_slot[0]!=UINT_MAX&&
   // The trail split draws the flat region twice (untessellated, then near
   // the trails): not in slot order, which the winner keys need.
   !(tessellate_terrain&&snow_field&&candidate.flat_pixels&&candidate_bucket.material_key.contact_map==0);
 };
 std::vector<std::uint8_t> slim_drawn;
 try{slim_drawn.assign(layout.batches.size(),0);}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
 std::size_t slim_run_first=SIZE_MAX,slim_resume=0;bool slim_replay=false,slim_lists=false,slim_marked=false;
 // terrain_slim_stencil: the stencil mark's programs (1: the mask pass's
 // full-screen vertex stage and the miss-reading pixel program; 2: the
 // listed quads' vertex stage, no pixel program).
 const ShaderProgram* slim_mark_ps=nullptr;const ShaderProgram* mask_vs=nullptr;const ShaderProgram* quad_mark_vs=nullptr;
 if(slim_mark_mode>0)
  for(const auto& program:programs){
   if(program.defines)continue;
   if(std::strcmp(program.entry,"nativeSlimMissMarkPS")==0)slim_mark_ps=&program;
   else if(std::strcmp(program.entry,"nativeShadowMaskVS")==0)mask_vs=&program;
   else if(std::strcmp(program.entry,"nativeSlimQuadMarkVS")==0)quad_mark_vs=&program;
  }
 if(slim_quad_list&&!quad_mark_vs){
  slim_quad_list=false;d3d12_->count("terrain slim list mark: vertex program missing (full-screen mark instead)");
 }
 const auto compact_slim_run=[&](std::size_t first,std::size_t last)->HRESULT{
  transition_profile_pass("terrain_slim_compact");
  d3d12_->set_unordered(4,static_cast<const D3D12Texture*>(nullptr));d3d12_->set_unordered(5,static_cast<const D3D12Buffer*>(nullptr));
  d3d12_->set_unordered(6,static_cast<const D3D12Buffer*>(nullptr));
  d3d12_->set_unordered(3,static_cast<const D3D12Texture*>(nullptr));d3d12_->set_unordered(7,static_cast<const D3D12Buffer*>(nullptr)); // the list mark's
  d3d12_->set_resource(4,static_cast<const D3D12Buffer*>(nullptr));
  d3d12_->set_unordered(0,native12_.visible.get());d3d12_->set_unordered(1,native12_.arguments.get());
  d3d12_->set_unordered(2,terrain_miss_origin12_.get());
  d3d12_->set_resource(124,terrain_miss_tris12_.get());
  slim_lists=true;
  for(std::size_t index=first;index<last&&slim_lists;++index){
   if(!slim_drawn[index])continue;
   const NativeBatch& run_batch=layout.batches[index];
   for(unsigned region=0;region<2&&slim_lists;++region){
    if(run_batch.miss_list_slot[region]==UINT_MAX)continue;
    UINT width=0,height=0;
    if(!native_dispatch_dimensions((run_batch.miss_list_max[region]+63u)/64u,width,height)){slim_lists=false;break;}
    const UINT values[8]={region?run_batch.steep_triangle_base:run_batch.output_triangle_base,run_batch.miss_list_max[region],run_batch.miss_list_base[region],
     region?run_batch.steep_args_slot*16u:static_cast<UINT>(index)*16u,run_batch.miss_list_slot[region]*16u,0,0,0};
    d3d12_->set_constants(9,values,sizeof(values));
    const HRESULT dispatched=d3d12_->dispatch(*miss_compact_cs,width,height,1);
    if(FAILED(dispatched))return dispatched;
    slim_lists=dispatched==S_OK;
   }
  }
  d3d12_->set_unordered(0,static_cast<const D3D12Buffer*>(nullptr));d3d12_->set_unordered(1,static_cast<const D3D12Buffer*>(nullptr));
  d3d12_->set_unordered(2,static_cast<const D3D12Buffer*>(nullptr));
  d3d12_->set_resource(124,static_cast<const D3D12Buffer*>(nullptr));
  d3d12_->set_resource(4,native12_.visible.get());
  d3d12_->set_resource(125,terrain_miss12_.get());d3d12_->set_resource(126,terrain_miss_origin12_.get()); // the second passes read them
  if(!slim_lists)d3d12_->count("terrain slim: second pass whole (compaction not ready)");
  // The run's missed pixels marked in stencil (terrain_slim_stencil): the
  // second passes test the mark (stencil not 0), so their other pixels are
  // rejected before the full program. Not drawn (pipeline not ready): the
  // second passes run untested, with the same image.
  slim_marked=false;
  if(slim_quad_list||(slim_mark_ps&&mask_vs)){
   Constants sequence{};sequence.liquid_layer=slim_flush<<24;
   d3d12_->set_constants(0,&sequence,sizeof(sequence));
   D3D12NativeState mark;
   mark.raster.FillMode=D3D11_FILL_SOLID;mark.raster.CullMode=D3D11_CULL_NONE;mark.raster.DepthClipEnable=TRUE;
   mark.depth.DepthEnable=FALSE;mark.depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;mark.depth.DepthFunc=D3D11_COMPARISON_ALWAYS;
   mark.depth.StencilEnable=TRUE;mark.depth.StencilReadMask=0xFF;mark.depth.StencilWriteMask=0xFF;
   mark.depth.FrontFace={D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_INCR_SAT,D3D11_COMPARISON_ALWAYS};
   mark.depth.BackFace=mark.depth.FrontFace;
   const bool history=world_history_bound12_;
   d3d12_->set_targets(nullptr,0,world_d3d12_.get());
   // 2: the quads the slim pass listed (this flush's so far; instance count
   // from the list's own arguments), the stencil only. 1: the full screen.
   HRESULT marked=S_OK;
   if(slim_quad_list){
    d3d12_->set_resource(123,terrain_miss_quad_list12_.get());
    // The vertex stage places the quads in clip space by the mask's size, so
    // the viewport is the mask's (the world target's) size. With no colour
    // target bound the default viewport is the presenter's (3840x2160 under
    // a 2560x1440 world at DLSS-Q): the quads landed scaled by 1.5 and the
    // misses stayed black (run 204). The full-screen mark reads its own pixel
    // position, so it never depended on the viewport.
    UINT mask_width=0,mask_height=0,mask_levels=0;DXGI_FORMAT mask_format=DXGI_FORMAT_UNKNOWN;
    d3d12_->texture_info(*terrain_miss12_,mask_width,mask_height,mask_levels,mask_format);
    const RECT mask_rect{0,0,LONG(mask_width),LONG(mask_height)};
    d3d12_->set_viewport(&mask_rect);
    marked=d3d12_->draw_indirect(*quad_mark_vs,ShaderProgram{},mark,nullptr,*terrain_miss_quad_list12_,0);
    d3d12_->set_viewport(nullptr);
    d3d12_->set_resource(123,static_cast<const D3D12Buffer*>(nullptr));
   }else marked=d3d12_->draw(*mask_vs,*slim_mark_ps,mark,nullptr,3);
   bind_world_targets_d3d12(history);
   if(FAILED(marked))return marked;
   slim_marked=marked==S_OK;
   if(!slim_marked)d3d12_->count("terrain slim: second pass without the stencil mark (pipeline not ready)");
  }
  return S_OK;
 };
 for(std::size_t batch_index=0;;++batch_index){
  // A run ends at the first batch that cannot take the slim pass (or the
  // flush's end): its compaction, then its batches again (second passes).
  if(!slim_replay&&slim_run_first!=SIZE_MAX&&(batch_index>=layout.batches.size()||!slim_candidate(batch_index))){
   if(FAILED(hr=compact_slim_run(slim_run_first,batch_index)))return hr;
   slim_replay=true;slim_resume=batch_index;batch_index=slim_run_first;slim_run_first=SIZE_MAX;
  }
  if(slim_replay&&batch_index>=slim_resume){
   slim_replay=false;
   // The marks are consumed: a later flush's prepass and mask pass start from zero.
   if(slim_marked){d3d12_->clear_stencil_of(*world_d3d12_,0);slim_marked=false;}
   d3d12_->set_resource(125,static_cast<const D3D12Texture*>(nullptr));d3d12_->set_resource(126,static_cast<const D3D12Buffer*>(nullptr));
  }
  if(batch_index>=layout.batches.size())break;
  if(slim_replay&&!slim_drawn[batch_index])continue;
  if(!slim_replay&&slim_run_first==SIZE_MAX&&slim_candidate(batch_index))slim_run_first=batch_index;
  const NativeBatch& batch=layout.batches[batch_index];
  const NativeStateBucket& bucket=native_state_buckets_[batch.state_bucket];
  if(!interface_batch(batch))continue;
  if(material_pass!=0&&(early.size()>batch_index&&early[batch_index])!=(material_pass==1))continue;
  if(batch.geometry_page>=native_geometry_pages_.size()||!native_geometry_pages_[batch.geometry_page].d3d12)return E_FAIL;
  // write_history (the D3D11 flush): opaque and cutout world batches write
  // this frame's sun visibility into the history target at slot 1.
  {const bool write_history=history_active&&!scene_composited_&&!hit_test_active_&&bucket.opacity!=NativeOpacityClass::ordered;
   if(world_d3d12_bound_&&!scene_composited_&&write_history!=world_history_bound12_)bind_world_targets_d3d12(write_history);
   if(world_history_bound12_)shadow_history_written_=true;}
  const auto* baked=baked_batch(batch_index);
  const NativeQueuedDraw& draw=baked?native_instance_records_[baked->first_record].command:native_queued_draw(native_draw_order_[batch.first_draw]);
  const FixedState& st=bucket.state;
  ID3D11ShaderResourceView* contact_views[4]{};
  Constants constants;native_batch_constants(batch,draw,bucket,contact_views,neighbour_terrain_blend,neighbour_fog_skip,shadow_coverage,&constants);
  d3d12_->set_constants(0,&constants,sizeof(constants));
  d3d12_->set_resource(1,native_geometry_pages_[batch.geometry_page].d3d12.get());
  // The batch's contact maps (native_batch_constants chose the active map's).
  // As native_batch_constants' active_art: the active map's grid and atlas
  // while they belong to this map (the ready bits select atlas slices in the
  // shader; they do not gate the binding).
  {const ContactBlendMaps* maps=nullptr;
   if(!scene_composited_&&bucket.material_key.contact_map==0){
    if(contact_maps_.tiles12&&contact_maps_.atlas12&&contact_map_generation_==ambient_map_generation_&&
       contact_constants_generation_==contact_map_generation_)maps=&contact_maps_;
   }else if(!scene_composited_&&(bucket.semantic_kind!=SemanticKind::terrain||neighbour_terrain_blend)){
    // A neighbour's own grid and atlas (native_batch_constants' rule).
    for(const auto& n:gc_neighbours_)if(n.contact_map==bucket.material_key.contact_map){maps=&n.contact;break;}
   }
   d3d12_->set_resource(15,maps?maps->tiles12.get():nullptr);
   d3d12_->set_resource(16,maps?maps->atlas12.get():nullptr);
   // Its map's painted cover colour and ground (the active map's or the
   // neighbour's own; native_batch_constants sets their origin and mip).
   const GcMapD3D12* cover=contact_cover_map_d3d12(bucket.material_key.contact_map);
   d3d12_->set_resource(kContactGroundSlotD3D12,cover?cover->ground.get():nullptr);
   d3d12_->set_resource(kContactColorSlotD3D12,cover?cover->color.get():nullptr);}
  {D3D11_SAMPLER_DESC contact{};contact.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
   contact.AddressU=contact.AddressV=contact.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;contact.MaxLOD=D3D11_FLOAT32_MAX;d3d12_->set_sampler(8,contact);}
  // The material texture: each draw's bindless index (instance texture
  // reference), so a batch may span textures; no t0/t6 binding.
  // apply_state's choices, as the D3D11 flush makes them per batch.
  const DWORD cull=st.render[D3DRENDERSTATE_CULLMODE];
  const bool wireframe=st.render[D3DRENDERSTATE_FILLMODE]==D3DFILL_WIREFRAME;
  D3D12NativeState state;
  state.raster=fixed_raster_desc(cull,wireframe,true);
  state.depth=fixed_depth_desc(st.render[D3DRENDERSTATE_ZENABLE],st.render[D3DRENDERSTATE_ZWRITEENABLE],st.render[D3DRENDERSTATE_ZFUNC]);
  state.blend=fixed_blend_choice(st.render[D3DRENDERSTATE_ALPHABLENDENABLE],st.render[D3DRENDERSTATE_SRCBLEND],
                                 st.render[D3DRENDERSTATE_DESTBLEND],draw.hud_composition.opacity<1.f);
  // Drawn in this flush's prepass: only its visible surface passes (depth
  // final, nothing written), shaded once with the mask when it is ready.
  const bool masked=prepassed[batch_index]==2;
  if(prepassed[batch_index]){
   state.depth.DepthFunc=D3D11_COMPARISON_EQUAL;state.depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;
  }
  // prepass_depth_check: the marker state (red channel kept, green and blue
  // zeroed in every target; LESS, or no depth test to calibrate).
  D3D12NativeState check_state=state;
  if(prepassed[batch_index]&&(depth_check==1||depth_check==2)){
   check_state.depth.DepthEnable=depth_check==1;check_state.depth.DepthFunc=D3D11_COMPARISON_LESS;
   check_state.custom_blend.custom=D3D12NativeState::kCustomBlendOver;
   check_state.custom_blend.source=check_state.custom_blend.source_alpha=D3D11_BLEND_ZERO;
   check_state.custom_blend.destination=check_state.custom_blend.destination_alpha=D3D11_BLEND_ZERO;
   check_state.custom_blend.write_mask=D3D11_COLOR_WRITE_ENABLE_GREEN|D3D11_COLOR_WRITE_ENABLE_BLUE;
  }
  const bool depth_checked=prepassed[batch_index]&&(depth_check==1||depth_check==2);
  // The check draw: the world colour target only (its writes masked to the
  // marker), the world depth read-only and at t119 for the hazards variant's
  // in-shader count (report word 7 = the mode while it runs).
  const auto checked_draw=[&](auto&& draw)->HRESULT{
   D3D12Texture* const world=world_d3d12_.get();
   d3d12_->set_gpu_report_word(7,UINT(depth_check));
   d3d12_->set_targets_read_depth(&world,1,world);
   d3d12_->set_depth_resource(kPrepassDepthSlotD3D12,world);
   const HRESULT result=draw();
   d3d12_->set_depth_resource(kPrepassDepthSlotD3D12,nullptr);
   d3d12_->set_gpu_report_word(7,0);
   bind_world_targets_d3d12(world_history_bound12_);
   return result;
  };
  const auto sampling=world_texture_sampler(st.stage[0][D3DTSS_MINFILTER],st.stage[0][D3DTSS_MAGFILTER],st.stage[0][D3DTSS_MIPFILTER],
   st.stage[0][D3DTSS_ADDRESSU],st.stage[0][D3DTSS_ADDRESSV],!scene_composited_,
   1u<<configured_integer(debug_overlay_.config(),"anisotropy",3));
  d3d12_->set_sampler(0,sampling.description);
  RECT clip_rect{};
  const bool clipped=ui_clip_rect(draw.hud_composition,draw.camp_clip,clip_rect);
  const bool terrain=bucket.semantic_kind==SemanticKind::terrain&&!scene_composited_;
  // A liquid batch counts as the D3D11 flush names it (liquid_reflections
  // with a reflection frame, else liquid_surface): set at its branch below.
  const char* const native_pass=bucket.semantic_kind==SemanticKind::terrain?"gpu_driven_terrain":
   (draw.instance.lighting_enabled?"gpu_driven_figures":"gpu_driven_object_meshes");
  bool liquid=false,ground=false;std::uint8_t liquid_types=0;
  if(terrain){
   if(baked){liquid=baked->liquid;ground=baked->ground;liquid_types=baked->liquid_types;}
   else for(std::size_t n=batch.first_draw;n<batch.first_draw+batch.draw_count;++n){
    const NativeMesh& mesh=*native_queued_draw(native_draw_order_[n]).mesh;
    liquid|=mesh.has_liquid;ground|=mesh.has_ground;liquid_types|=mesh.liquid_types;
   }
  }
  if(!liquid){transition_profile_pass(native_pass);if(!slim_replay){profiler_.add_draw(0,batch.draw_count);profiler_.add_batch();}}
  if(!terrain){
   // Upscaling: a batch with a posed or swaying record (units, animated
   // figures, swaying plants) writes its own motion in this draw (targets 3
   // and 4, unblended like the old separate draw: opaque and cutout batches
   // only); b11: the previous clip, the jitter, the render size and the sway
   // clocks; t46: last frame's world vertices.
   const bool object_motion=(batch.posed||batch.swaying)&&bucket.opacity!=NativeOpacityClass::ordered&&
    !state.custom_blend.custom&&!fixed_blend_desc(state.blend).RenderTarget[0].BlendEnable&&
    motion_vs&&motion_object_ps&&motion_masked_ps&&object_motion_ready_d3d12();
   const ShaderProgram* instance_ps=nullptr;const ShaderProgram* instance_vs=nullptr;
   const ShaderProgram* instance_twins[4]{};UINT instance_twin_count=0;
   const bool astral_batch=bucket.material_key.program==1u;
   if(astral_batch){
    // Astral rift records: their own programs (ordered: no motion, prepass or index list).
    if(!astral_vs||!astral_ps){d3d12_->omit("world batch (astral program missing)",true);continue;}
    instance_vs=astral_vs;instance_ps=astral_ps;
   }else if(split&&batch.material_instance){
    // World object, foliage and crown impostor batches out of contact reach (the D3D11 contact rule)
    // take the programs without the blend (F10 object_program_cuts without 2: crown impostors only).
    const std::uint8_t instance_domain=batch.material_instance&7u;
    const bool no_contact=(instance_domain==kNativeCrownImpostorDomain||(instance_domain>=2u&&(program_cuts&2)!=0))&&!split_check&&
     !batch.contact&&layout.contact_generation==native_contact_generation_;
    const std::size_t instance_index=native_instance_slot(split_check_mode==2?native_instance_rotated(batch.material_instance):batch.material_instance)*2u+(masked?1u:0u);
    const auto instance_variant=[&](bool dry,bool without_contact){
     return instance_programs[((dry?4u:0u)+(without_contact?2u:0u)+(object_motion?1u:0u))*kNativeInstancePrograms+instance_index];};
    instance_ps=instance_variant(dry_frame,no_contact);
    // The variants the records of this batch can switch to in play (weather starting or ending, a draw
    // coming into contact reach): requested with this pipeline, so the switch compiles nothing.
    if(!split_check){
     for(const bool dry:{false,true})for(const bool without_contact:{false,true})
      if(const ShaderProgram* twin=instance_variant(dry,without_contact);twin&&twin!=instance_ps)instance_twins[instance_twin_count++]=twin;
    }
    // A missing instance program omits the batch (world marker), never the general program.
    instance_vs=instance_vertex[native_instance_vertex_class(batch.material_instance)*3u+(object_motion?2u:0u)];
    if(!instance_ps||!instance_vs){d3d12_->omit("world batch (material instance program missing)",true);continue;}
   }
   // An indexed batch: its indexed program over its index triples (the
   // prepass's rule; the build wrote it no visible triangles).
   const ShaderProgram* indexed_vs=nullptr;
   if(indexed_draws&&batch.indexed){
    indexed_vs=instance_ps?indexed_vertex[native_instance_vertex_class(batch.material_instance)*3u+(object_motion?2u:0u)]:nullptr;
    if(!indexed_vs){d3d12_->omit("world batch (indexed vertex program missing)",true);continue;}
   }
   const bool gltf=draw.instance.skn_factors[3]>0;
   const ShaderProgram* gltf_pixel=object_motion?(masked?gltf_motion_masked_ps:gltf_motion_ps):(masked?gltf_masked_ps:gltf_ps);
   if(gltf&&!gltf_pixel)return E_FAIL;
   const ShaderProgram& batch_ps=gltf?*gltf_pixel:instance_ps?*instance_ps:object_motion?(masked?*motion_masked_ps:*motion_object_ps):masked?*masked_object_ps:object_ps?*object_ps:programs[1];
   const ShaderProgram* const guide_ps=guide_twin(batch_ps,bucket,state,true);
   // Symptom this rule answers: the player's unit, drawn with its blend on,
   // wrote no guides: the traced passes and Ray Reconstruction read the
   // ground behind it at its pixels (rt_term_view 1 showed terrain there).
   D3D12NativeState guide_state=state;
   if(guide_ps&&fixed_blend_desc(state.blend).RenderTarget[0].BlendEnable){
    guide_state.custom_blend.custom=D3D12NativeState::kCustomGuideTargetsOpaque;
    d3d12_->count("world batches with their guides unblended (the state blends)");
   }
   if(object_motion||guide_ps)bind_world_targets_d3d12(world_history_bound12_,object_motion,guide_ps!=nullptr);
   if(guide_ps)twin_lamps(true);
   if(object_motion){
    d3d12_->set_constants(11,upscale12_.motion_constants.data(),UINT(sizeof(upscale12_.motion_constants)));
    d3d12_->set_resource(46,upscale12_.previous_world.get());
   }
   // World crystals (domain 5): the flush's first crystal batch copies the
   // scene they see through (prepare_crystal_scene; t3 slice 0, s5), else none.
   const bool crystal_batch=instance_ps&&(batch.material_instance&7u)==kNativeCrystalWorldDomain;
   if(crystal_batch||crystal_bound){
    const bool scene=crystal_batch&&prepare_crystal_scene();
    d3d12_->set_resource(3,scene?water12_.scene.get():nullptr);d3d12_->set_sampler(5,water_sampler);
    crystal_bound=crystal_batch;
   }
   const ShaderProgram& batch_vs=instance_vs?*instance_vs:object_motion?*motion_vs:programs[0];
   const auto batch_draw=[&](const ShaderProgram& pixel,const D3D12NativeState& drawn_state)->HRESULT{
    if(!indexed_vs)return d3d12_->draw_indirect(batch_vs,pixel,drawn_state,clipped?&clip_rect:nullptr,*native12_.arguments,batch_arguments(batch,batch_index));
    d3d12_->set_index_buffer(native12_.indices.get(),UINT64(batch.output_triangle_base)*12u);
    const HRESULT drawn=d3d12_->draw_indexed_indirect(*indexed_vs,pixel,drawn_state,clipped?&clip_rect:nullptr,*native12_.arguments,batch_arguments(batch,batch_index));
    d3d12_->set_index_buffer(nullptr);
    return drawn;
   };
   // A guide twin's draw requests the guide twins of the batch's variants
   // (the pipeline's targets are the guide targets), not the variants.
   const ShaderProgram* guide_variants[4]{};UINT guide_variant_count=0;
   if(guide_ps)for(UINT variant=0;variant<instance_twin_count;++variant)
    if(const auto found=native12_.guide_twins.find(instance_twins[variant]);found!=native12_.guide_twins.end())guide_variants[guide_variant_count++]=found->second;
   if(guide_ps){if(guide_variant_count)d3d12_->set_pixel_twins(guide_variants,guide_variant_count);}
   else if(instance_twin_count)d3d12_->set_pixel_twins(instance_twins,instance_twin_count);
   hr=guide_ps?batch_draw(*guide_ps,guide_state):batch_draw(batch_ps,state);
   d3d12_->set_pixel_twins(nullptr,0);
   if(guide_ps)twin_lamps(false);
   if(guide_ps&&hr==S_FALSE){
    // The twin's pipeline is not built yet: the batch still draws this frame,
    // with its own program and without its guides (counted, never hidden).
    bind_world_targets_d3d12(world_history_bound12_,object_motion);
    if(instance_twin_count)d3d12_->set_pixel_twins(instance_twins,instance_twin_count);
    hr=batch_draw(batch_ps,state);
    d3d12_->set_pixel_twins(nullptr,0);
    d3d12_->count("world batches without Ray Reconstruction guides (twin pipeline not ready)");
   }else if(guide_ps&&hr==S_OK)d3d12_->count("world batches with their Ray Reconstruction guides");
   if(indexed_vs&&hr==S_OK)d3d12_->count("world batches drawn indexed");
   if(guide_ps&&!object_motion)bind_world_targets_d3d12(world_history_bound12_);
   if(depth_checked&&hr==S_OK)
    hr=checked_draw([&]{return d3d12_->draw_indirect(programs[0],masked?*masked_object_ps:object_ps?*object_ps:programs[1],
                             check_state,clipped?&clip_rect:nullptr,*native12_.arguments,batch_arguments(batch,batch_index));});
   if(object_motion){
    // b11 is also the ground cover and critter frame slot, t46 the ground cover's trail history.
    const std::array<float,24> zero{};
    d3d12_->set_constants(11,zero.data(),UINT(sizeof(zero)));
    d3d12_->set_resource(46,static_cast<const D3D12Buffer*>(nullptr));
    bind_world_targets_d3d12(world_history_bound12_);
    if(hr==S_OK)d3d12_->count("world batches with their object motion");
   }
   if(FAILED(hr))return hr;
   const char* drawn_name=scene_composited_?"interface batches drawn":bucket.semantic_kind==SemanticKind::figure?"world figure batches drawn":
    bucket.semantic_kind==SemanticKind::simple_geometry?"world object batches drawn":"world batches drawn (other kinds)";
   if(hr==S_FALSE)d3d12_->omit(scene_composited_?"native interface batch (pipeline not ready)":"world batch (pipeline not ready)",false);
   else if(hr==S_OK)d3d12_->count(drawn_name);
   continue;
  }
  // Terrain (the D3D11 batch loop's choices): a liquid batch draws its ground
  // part only (the water surfaces are M2e); a split batch draws its flat
  // region with the flat variant, then its steep region from its own
  // arguments slot and triangle base with the full one; an unsplit batch of
  // flat tiles takes the flat variant.
  if(!terrain_ps||!terrain_flat_ps){d3d12_->omit("terrain (pixel variant missing)",true);continue;}
  // A prepassed batch with the mask: the NATIVE_SHADOW_MASK variants.
  const ShaderProgram& full_ps=masked?*masked_terrain_ps:*terrain_ps;
  const ShaderProgram& flat_ps=masked?*masked_terrain_flat_ps:*terrain_flat_ps;
  // Neighbour-map terrain draws untessellated (the D3D11 rule); the active
  // map's flat region too without a trail field.
  const bool tessellated_batch=tessellate_terrain&&bucket.material_key.contact_map==0;
  const auto terrain_draw=[&](const ShaderProgram& pixel,UINT offset,bool tessellate)->HRESULT{
   const auto& stages=native12_.tessellation;
   const auto draw=[&](const ShaderProgram& program){
    return tessellate?
     d3d12_->draw_indirect(stages[0],program,state,clipped?&clip_rect:nullptr,*native12_.arguments,offset,terrain_hull,terrain_domain):
     d3d12_->draw_indirect(programs[0],program,state,clipped?&clip_rect:nullptr,*native12_.arguments,offset);
   };
   // rt_ray_reconstruction: the ground's guide twin, the guide targets bound for its draw.
   const ShaderProgram* const guide_ps=guide_twin(pixel,bucket,state);
   if(guide_ps){bind_world_targets_d3d12(world_history_bound12_,false,true);twin_lamps(true);}
   HRESULT drawn=draw(guide_ps?*guide_ps:pixel);
   if(guide_ps){
    bind_world_targets_d3d12(world_history_bound12_);twin_lamps(false);
    if(drawn==S_OK)d3d12_->count("terrain draws with their Ray Reconstruction guides");
    else if(drawn==S_FALSE){ // the twin's pipeline is not built yet: the ground still draws, without its guides this frame
     drawn=draw(pixel);d3d12_->count("terrain draws without Ray Reconstruction guides (twin pipeline not ready)");
    }
   }
   if(depth_checked&&drawn==S_OK){
    drawn=checked_draw([&]{return tessellate?
     d3d12_->draw_indirect(stages[0],pixel,check_state,clipped?&clip_rect:nullptr,*native12_.arguments,offset,terrain_hull,terrain_domain):
     d3d12_->draw_indirect(programs[0],pixel,check_state,clipped?&clip_rect:nullptr,*native12_.arguments,offset);});
   }
   // Missing ground is a visible failure (world marker, F11), never a gap.
   if(drawn==S_FALSE)d3d12_->omit("terrain batch (pipeline not ready)",true);
   else if(drawn==S_OK)d3d12_->count("terrain batches drawn");
   return drawn;
  };
  if(liquid){
   // The D3D11 batch loop's liquid draws without a reflection frame (the
   // water_reflections 0 programs): stock-look water whole through the
   // ground variant; else the ground part, the water body, then the foam.
   // The liquid layers (water body, foam, SSR input) take the stages only
   // where they can split and displace: falling or running water (the
   // batch's native_liquid_flowing vertices) or water the bound surf field
   // reaches (liquid_layers_tessellated). Calm water arrives at factor 1 with no
   // displacement, so the stages returned its triangles unchanged at the
   // full hull/domain cost. The ground layer keeps them (cliff smoothing).
   const bool liquid_tessellated=tessellated_batch&&liquid_layers_tessellated(batch,baked);
   const auto layer_draw=[&](const ShaderProgram& pixel,UINT layer_bits,const D3D12NativeState& layer_state,bool tessellate)->HRESULT{
    Constants layer=constants;layer.liquid_layer=layer_bits;
    d3d12_->set_constants(0,&layer,sizeof(layer));
    const auto& stages=native12_.tessellation;
    const auto draw=[&](const ShaderProgram& program){
     return tessellate?
      d3d12_->draw_indirect(stages[0],program,layer_state,clipped?&clip_rect:nullptr,*native12_.arguments,static_cast<UINT>(batch_index)*16u,terrain_hull,terrain_domain):
      d3d12_->draw_indirect(programs[0],program,layer_state,clipped?&clip_rect:nullptr,*native12_.arguments,static_cast<UINT>(batch_index)*16u);
    };
    // rt_ray_reconstruction: the ground part's guide twin (the liquid programs have none).
    const ShaderProgram* const guide_ps=guide_twin(pixel,bucket,layer_state);
    if(guide_ps){bind_world_targets_d3d12(world_history_bound12_,false,true);twin_lamps(true);}
    HRESULT drawn=draw(guide_ps?*guide_ps:pixel);
    if(guide_ps){
     bind_world_targets_d3d12(world_history_bound12_);twin_lamps(false);
     if(drawn==S_FALSE){ // the twin's pipeline is not built yet: the ground part still draws, without its guides this frame
      drawn=draw(pixel);d3d12_->count("terrain draws without Ray Reconstruction guides (twin pipeline not ready)");
     }
    }
    if(drawn==S_FALSE){
     const bool ground=&pixel==terrain_ps||&pixel==terrain_flat_ps; // missing ground shows the world marker
     d3d12_->omit(ground?"liquid batch ground (pipeline not ready)":"liquid batch (pipeline not ready)",ground);
    }
    return drawn;
   };
   if(water_simple){
    transition_profile_pass("liquid_surface");profiler_.add_draw(0,batch.draw_count);profiler_.add_batch();
    if(FAILED(hr=layer_draw(*terrain_ps,8u,state,tessellated_batch)))return hr;
    d3d12_->count("stock-look water batches drawn");
    continue;
   }
   // The reflection frame: this flush's scene copy and depth snapshot (taken
   // at its first liquid batch), and the half-resolution trace once.
   const bool water_reflections=water_reflections_wanted&&prepare_water_reflections(st);
   const char* const liquid_pass=water_reflections?"liquid_reflections":"liquid_surface";
   transition_profile_pass(liquid_pass);profiler_.add_draw(0,batch.draw_count);profiler_.add_batch();
   if(water_reflections&&!water_ssr_done){
    water_ssr_done=true;
    for(UINT slot=29;slot<33;++slot)d3d12_->set_resource(slot,static_cast<const D3D12Texture*>(nullptr));
    if(!ssr_input_ps||!ssr_march_ps||!full_screen_vs||FAILED(create_water_ssr_targets_d3d12())){
     if(!water_ssr_failure_reported_){
      water_ssr_failure_reported_=true;
      bridge_report_event("water_ssr","unavailable: half-resolution reflection programs or targets; water shows no screen reflections");
     }
    }else{
     transition_profile_pass("water_ssr_inputs");
     // The input pass covers only water: its place target (w 0: no water)
     // and depth are cleared. The march and the filter write every texel of
     // theirs (2-5), and every reader of a texel's ray, colour or key first
     // tests its place (w > 0) and key (w >= 0, which the march sets to -1
     // wherever the place is empty), so the ray target's old texels and the
     // other four's are never used (coverage_cuts without 2: all six cleared).
     const float zero[4]{};
     if(coverage_cuts&2)d3d12_->clear_texture(*water12_.ssr[1],zero);
     else for(const auto& target:water12_.ssr)d3d12_->clear_texture(*target,zero);
     d3d12_->clear_depth_of(*water12_.ssr_depth,1.f,0);
     D3D12Texture* inputs[2]={water12_.ssr[0].get(),water12_.ssr[1].get()};
     d3d12_->set_targets(inputs,2,water12_.ssr_depth.get());
     d3d12_->set_constants(7,&water12_.constants,sizeof(water12_.constants));
     // Every liquid batch of the flush writes its rays and water points (the
     // input program, its own depth: the nearest water), with the stages its
     // water body draws with.
     for(std::size_t index=0;index<layout.batches.size();++index){
      const NativeBatch& item=layout.batches[index];
      const NativeStateBucket& item_bucket=native_state_buckets_[item.state_bucket];
      if(item_bucket.semantic_kind!=SemanticKind::terrain)continue;
      const auto* item_baked=baked_batch(index);
      bool item_liquid=item_baked&&item_baked->liquid;
      for(std::size_t n=item.first_draw;!item_baked&&!item_liquid&&n<item.first_draw+item.draw_count;++n)
       item_liquid=native_queued_draw(native_draw_order_[n]).mesh->has_liquid;
      if(!item_liquid)continue;
      const NativeQueuedDraw& item_draw=item_baked?native_instance_records_[item_baked->first_record].command:
       native_queued_draw(native_draw_order_[item.first_draw]);
      ID3D11ShaderResourceView* item_contact[4]{};Constants item_constants;
      native_batch_constants(item,item_draw,item_bucket,item_contact,neighbour_terrain_blend,neighbour_fog_skip,shadow_coverage,&item_constants);
      item_constants.liquid_layer=2u; // the liquid triangles only
      d3d12_->set_constants(0,&item_constants,sizeof(item_constants));
      d3d12_->set_resource(1,native_geometry_pages_[item.geometry_page].d3d12.get());
      D3D12NativeState input_state;
      input_state.raster=fixed_raster_desc(item_bucket.state.render[D3DRENDERSTATE_CULLMODE],item_bucket.state.render[D3DRENDERSTATE_FILLMODE]==D3DFILL_WIREFRAME,true);
      input_state.depth.DepthEnable=TRUE;input_state.depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;input_state.depth.DepthFunc=D3D11_COMPARISON_LESS;
      bind_batch_units(item,item_baked);
      // The stages its water body draws with (liquid_tessellated).
      const bool item_tessellated=tessellate_terrain&&item_bucket.material_key.contact_map==0&&
       liquid_layers_tessellated(item,item_baked);
      const auto& stages=native12_.tessellation;
      const HRESULT traced=item_tessellated?
       d3d12_->draw_indirect(stages[0],*ssr_input_ps,input_state,nullptr,*native12_.arguments,static_cast<UINT>(index)*16u,terrain_hull,terrain_domain):
       d3d12_->draw_indirect(programs[0],*ssr_input_ps,input_state,nullptr,*native12_.arguments,static_cast<UINT>(index)*16u);
      if(FAILED(traced))return traced;
      if(traced==S_FALSE)d3d12_->count("water trace inputs waiting for their pipeline");
     }
     if(liquid_ripple_ready_)d3d12_->set_constants(8,&liquid_ripple_values_,sizeof(liquid_ripple_values_));
     // The march: one full-screen triangle over the half-resolution targets.
     transition_profile_pass("water_ssr");
     D3D12NativeState full_screen;
     full_screen.raster.FillMode=D3D11_FILL_SOLID;full_screen.raster.CullMode=D3D11_CULL_NONE;full_screen.raster.DepthClipEnable=TRUE;
     D3D12Texture* traced_targets[2]={water12_.ssr[2].get(),water12_.ssr[3].get()};
     d3d12_->set_targets(traced_targets,2,nullptr);
     d3d12_->set_resource(31,water12_.ssr[1].get());d3d12_->set_resource(32,water12_.ssr[0].get()); // water points, rays
     d3d12_->set_resource(3,water12_.scene.get());d3d12_->set_depth_resource(112,water12_.depth.get());
     d3d12_->set_resource(28,particle_depth12_[particle_depth_current_^1u].get()); // the previous frame's particle depth (constants say if it is valid)
     d3d12_->set_sampler(5,water_sampler);
     HRESULT marched=d3d12_->draw(*full_screen_vs,*ssr_march_ps,full_screen,nullptr,3);
     if(FAILED(marched))return marched;
     // rt_reflections: the samples traced through the ray tracing scene first (their own colour and key images).
     const bool rays=marched==S_OK&&water_reflection_rays_d3d12(*full_screen_vs,full_screen);
     D3D12Texture* const traced_colour=rays?rt_reflect12_.water[0].get():water12_.ssr[2].get();
     D3D12Texture* const traced_key=rays?rt_reflect12_.water[1].get():water12_.ssr[3].get();
     // The filter: the traced colour and key in, the filtered pair out.
     bool filtered=false;
     if(marched==S_OK&&ssr_filter_ps){
      transition_profile_pass("water_ssr_filter");
      D3D12Texture* smoothed[2]={water12_.ssr[4].get(),water12_.ssr[5].get()};
      d3d12_->set_targets(smoothed,2,nullptr);
      d3d12_->set_resource(29,traced_colour);d3d12_->set_resource(30,traced_key);d3d12_->set_resource(31,water12_.ssr[1].get());
      const HRESULT result=d3d12_->draw(*full_screen_vs,*ssr_filter_ps,full_screen,nullptr,3);
      if(FAILED(result))return result;
      filtered=result==S_OK;
     }
     bind_world_targets_d3d12(world_history_bound12_);
     if(marched==S_OK){
      // The water bodies read the result (t29-t31).
      d3d12_->set_resource(29,filtered?water12_.ssr[4].get():traced_colour);d3d12_->set_resource(30,filtered?water12_.ssr[5].get():traced_key);
      d3d12_->set_resource(31,water12_.ssr[1].get());
      d3d12_->count("water traces");
     }else{
      for(UINT slot=29;slot<33;++slot)d3d12_->set_resource(slot,static_cast<const D3D12Texture*>(nullptr));
      d3d12_->count("water traces waiting for their pipeline");
     }
     d3d12_->set_resource(32,static_cast<const D3D12Texture*>(nullptr));
     // This batch's geometry page again (its constants come with each draw).
     d3d12_->set_resource(1,native_geometry_pages_[batch.geometry_page].d3d12.get());
     transition_profile_pass(liquid_pass);
    }
   }
   // The reflection frame's inputs (t3 scene copy, t112 depth snapshot, b7, s5), else none.
   const WaterReflectionConstants no_reflection{};
   d3d12_->set_resource(3,water_reflections?water12_.scene.get():nullptr);
   d3d12_->set_depth_resource(112,water_reflections?water12_.depth.get():nullptr);
   const bool swim=water_reflections&&water_physical&&water_swimmers_&&water_swim_ps&&liquid_swim_ps;
   d3d12_->set_stencil_resource(113,swim?water12_.depth.get():nullptr); // the swimmer marks
   d3d12_->set_resource(28,water_reflections?particle_depth12_[particle_depth_current_^1u].get():nullptr);
   d3d12_->set_constants(7,water_reflections?&water12_.constants:&no_reflection,sizeof(no_reflection));
   d3d12_->set_sampler(5,water_sampler);
   // Profiler: the liquid sectors' ground as its own pass (not water cost).
   if(ground){
    transition_profile_pass("liquid_ground");
    if(FAILED(hr=layer_draw(*terrain_ps,4u,state,tessellated_batch)))return hr;
    transition_profile_pass(liquid_pass);
   }
   // The water body's program (the D3D11 choice; this backend has no Low
   // pixel path): without a reflection frame the reflection-free variants.
   const unsigned body_types=liquid_types&native_liquid_type_mask;
   const ShaderProgram* const body=body_types==2u?lava_ps:
    body_types==1u?(!water_reflections?water_low_ps:!water_physical?water_ssr_ps:swim?water_swim_ps:water_ps):
    (!water_reflections?liquid_low_ps:!water_physical?liquid_ssr_ps:swim?liquid_swim_ps:liquid_ps);
   if(!body||!foam_ps){d3d12_->omit("water surfaces (liquid program missing)",true);continue;}
   bind_batch_units(batch,baked);
   if(liquid_tessellated)d3d12_->count("liquid batches tessellated");
   if(FAILED(hr=layer_draw(*body,2u,state,liquid_tessellated)))return hr;
   // Foam: the liquid triangles again (layer bit 0), depth tested, never written.
   D3D12NativeState foam_state=state;foam_state.depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;
   if(FAILED(hr=layer_draw(*foam_ps,3u,foam_state,liquid_tessellated)))return hr;
   if(liquid_ripple_ready_)d3d12_->set_constants(8,&liquid_ripple_values_,sizeof(liquid_ripple_values_));
   d3d12_->count("liquid batches drawn");
   continue;
  }
  // The slim terrain pass (the D3D11 batch loop's rule): a prepassed batch
  // draws first with the slim programs (misses marked at u4), then with its
  // own on the misses only (bit 64, the mask at t125). A pipeline not built
  // yet is built at its draw (7f329a2c), so a slim candidate always takes it.
  bool slim=slim_run_first!=SIZE_MAX&&slim_candidate(batch_index);
  // terrain_slim_cuts: this batch's slim pass draws the lean programs.
  const bool lean=lean_terrain_ps&&lean_terrain_flat_ps&&batch.terrain_records;
  const ShaderProgram* const slim_steep_ps=lean?lean_terrain_ps:slim_terrain_ps;
  const ShaderProgram* const slim_flat_ps=batch.flat_pixels?(lean?lean_terrain_flat_ps:slim_terrain_flat_ps):slim_steep_ps;
  if(slim_replay)slim=true; // its slim pass drew (slim_drawn)
  // Why a terrain batch draws in one pass (session counts in host.log).
  if(slim_replay){}
  else if(slim)d3d12_->count("terrain slim: two passes");
  else if(!terrain_slim)d3d12_->count("terrain slim: one pass (off, no cache or not a world frame)");
  else if(!masked)d3d12_->count("terrain slim: one pass (not prepassed)");
  else if(!terrain_miss12_||!slim_terrain_ps||!slim_terrain_flat_ps)d3d12_->count("terrain slim: one pass (no mask or slim program)");
  else if(tessellated_batch&&batch.flat_pixels&&snow_field)d3d12_->count("terrain slim: one pass (trail split)");
  // Slim batches: the slim pass in the run, the second pass when it replays.
  const UINT first_phase=slim&&slim_replay?1u:0u;
  for(UINT phase=first_phase;phase<first_phase+1u;++phase){
  const bool slim_pass=slim&&phase==0;
  if(slim_pass)slim_drawn[batch_index]=1;
  Constants base=constants;
  // Profiler: the two passes as their own passes (the next batch returns to its own).
  if(slim)transition_profile_pass(phase==0?"terrain_slim_pass1":"terrain_slim_pass2");
  // The second pass draws the run's miss lists (compact_slim_run); without
  // them (compaction not ready) the regions whole.
  const bool use_lists=slim&&phase==1&&slim_lists;
  if(use_lists)base.native_triangle_base=batch.miss_list_base[0];
  if(slim){
   base.liquid_layer=(base.liquid_layer&~0x7f000000u)|(slim_flush<<24); // the key sequence (terrainSlimKey)
   if(slim_pass){d3d12_->set_resource(125,static_cast<const D3D12Texture*>(nullptr));
    d3d12_->set_unordered(4,terrain_miss12_.get());d3d12_->set_unordered(5,terrain_miss_tris12_.get());
    d3d12_->set_unordered(6,face_slim_bits12_.get()); // the face pages its pixels read (faceSlimBits)
    if(slim_quad_list){ // terrain_slim_stencil 2: misses list their quads (terrainSlimListQuad)
     d3d12_->set_unordered(3,terrain_miss_quads12_.get());d3d12_->set_unordered(7,terrain_miss_quad_list12_.get());
     base.liquid_layer|=2048u;
    }
    if(lean){
     if(slim_cuts&2)base.liquid_layer|=512u;
     d3d12_->count("terrain slim: lean programs");
    }else if(slim_cuts&1)d3d12_->count("terrain slim: cuts off for a batch (not plain terrain records, or no lean program)");
   }
   else base.liquid_layer|=(terrain_slim_drawn==3?192u:64u)|(use_lists?256u:0u); // the mask at t125, the lists' slots at t126
   d3d12_->set_constants(0,&base,sizeof(base));
  }
  if(slim&&!slim_pass&&slim_marked){
   state.depth.StencilEnable=TRUE;state.depth.StencilReadMask=0xFF;state.depth.StencilWriteMask=0;
   state.depth.FrontFace={D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_COMPARISON_NOT_EQUAL};
   state.depth.BackFace=state.depth.FrontFace;
  }
  const ShaderProgram& first_ps=slim_pass?*slim_flat_ps:batch.flat_pixels?flat_ps:full_ps;
  const ShaderProgram& steep_ps=slim_pass?*slim_steep_ps:full_ps;
  if(tessellated_batch&&batch.flat_pixels&&snow_field){
   // With a trail field the active map's flat region draws in two parts (the
   // D3D11 trail split): untessellated with the triangles near an active
   // trail tile collapsed (layer bit 16), then tessellated with only those
   // (bit 32; nativeTriangleNearTrail), where the trail displacement needs the stages.
   for(const UINT layer_bits:{16u,32u}){
    Constants part=base;part.liquid_layer|=layer_bits;
    d3d12_->set_constants(0,&part,sizeof(part));
    if(FAILED(hr=terrain_draw(first_ps,use_lists?batch.miss_list_slot[0]*16u:static_cast<UINT>(batch_index)*16u,layer_bits==32u)))return hr;
   }
  }else if(FAILED(hr=terrain_draw(first_ps,use_lists?batch.miss_list_slot[0]*16u:static_cast<UINT>(batch_index)*16u,
     tessellated_batch&&!batch.flat_pixels)))return hr;
  if(batch.steep_args_slot!=UINT_MAX){
   Constants steep=base;steep.native_triangle_base=use_lists?batch.miss_list_base[1]:batch.steep_triangle_base;
   d3d12_->set_constants(0,&steep,sizeof(steep));
   if(FAILED(hr=terrain_draw(steep_ps,use_lists?batch.miss_list_slot[1]*16u:batch.steep_args_slot*16u,tessellated_batch)))return hr;
  }
  }
  // The slim passes keep u4/u5 through the run; compact_slim_run unbinds them.
 }
 // Fur prototype (renderer_fur.cpp): this flush's unit records with fur,
 // while their world vertices and the flush's lighting are still bound
 // (once: with the material kept, in its phase 2).
 if(material_pass!=1)draw_fur_records();
 // Later world passes (effects, marks) never write the history.
 if(world_history_bound12_&&world_d3d12_bound_&&!scene_composited_)bind_world_targets_d3d12();
 d3d12_->clear_slots();
 return S_OK;
}

// flush_native_shadows on D3D12 (M2e shadows): the shadow layout's records,
// or a bake's from its own layout and work list, into the sun maps, in the
// D3D11 order: the world preparations (prepare, commit), then per redrawn map
// its clear or static-layer copy, the build dispatches and the depth draws.
// Point faces use the same submission with atlas-cell targets, the rain
// shelter map its one plain slice. This does not
// include the static-layer scroll
// (shadow_scroll_ps_ is never made here: moved maps redraw their layer).
HRESULT Renderer::flush_native_shadows_d3d12(){
 const bool point=native_point_shadow_active_;
 const bool rain=native_rain_active_; // the shelter map: one plain slice, no sun-map state
 auto* const active=point?nullptr:native_shadow_bake_active_;
 const bool bake=active!=nullptr;
 auto& layout=native_submission_layouts_[bake?active->layout_slot:1];
 if(!native_shadow_collecting_||native_shadow_failed_)return E_FAIL;
 if(bake&&(!native_pending_draws_.empty()||!layout.static_bake.work12))return E_FAIL;
 const auto finish=[&](HRESULT hr){
  d3d12_->clear_slots();
  if(world_d3d12_bound_)bind_world_targets_d3d12();else d3d12_->set_target(nullptr);
  if(FAILED(hr)){
   native_shadow_failed_=true;
   if(!point&&!rain)for(UINT slice=0;slice<5;++slice)if(native_shadow_dirty_mask_&(1u<<slice)){native_shadow_valid_[slice]=false;native_shadow_static_valid_[slice]=false;}
  }
  discard_native();end_profile_pass();return hr;
 };
 // A program still compiling (or a ring exhausted, named by the executor):
 // this pass's maps stay invalid (receivers unshadowed there) and are
 // redrawn next frame; never a failed pass.
 const auto wait=[&](HRESULT why=S_FALSE){
  // A collection can flush between terrain and figure batches. Keep its
  // existing failure bit until end_point_faces so a later empty flush cannot
  // turn this incomplete bake into a successful publication.
  if(point||rain)native_shadow_failed_=true;
  if(!point&&!rain)for(UINT slice=0;slice<5;++slice)if(native_shadow_dirty_mask_&(1u<<slice)){native_shadow_valid_[slice]=false;native_shadow_static_valid_[slice]=false;}
  if(why==S_FALSE)d3d12_->count(point?"point shadow passes waiting for their pipelines":"sun shadow passes waiting for their pipelines");
  return finish(point||rain?S_FALSE:S_OK); // an incomplete point bake must not be published
 };
 if(!native_shadow_dirty_mask_)return finish(S_OK);
 // The shelter collection without its map (begin_native_rain_occlusion side 0)
 // prepares the store's world vertices and draws no slice.
 const bool rain_map=rain&&native_rain_map_;
 if(FAILED(native_d3d12_ensure())||(rain?rain_map&&(!rain12_.shelter||!rain12_.foliage):!point&&(!shadow12_.maps||!shadow12_.statics)))return finish(E_FAIL);
 try{
  HRESULT hr=S_OK;
  if(bake){begin_native_preparations();native_preparation_count_=active->preparations;}
  else{
   order_native_groups(true);
   if(FAILED(hr=prepare_native_submission_layout(true,1)))return finish(hr);
   refresh_native_draw_work(layout,true);
  }
  const auto& programs=native12_.programs;
  const auto program=[&](const char* entry)->const ShaderProgram*{
   for(const auto& candidate:programs)if(std::strcmp(candidate.entry,entry)==0)return &candidate;
   return nullptr;
  };
  const ShaderProgram* const prepare_cs=program("nativeShadowPrepareCS");
  const ShaderProgram* const commit_cs=program("nativeShadowCommitCS");
  const ShaderProgram* const build_cs=program("nativeShadowBuildCS");
  const ShaderProgram* const shadow_vs=program("nativeShadowVS");
  const ShaderProgram* const shadow_ps=program("nativeShadowPS");
  if(!prepare_cs||!commit_cs||!build_cs||!shadow_vs||!shadow_ps)return finish(E_FAIL);
  const UINT records=static_cast<UINT>(layout.work.size()),batches=static_cast<UINT>(layout.batches.size());
  std::array<std::uint8_t,sizeof(native_world_clip_)*2+80> clip;native_world_clip_values(clip);
  D3D12Buffer* work=nullptr;
  if(records){
   if(FAILED(hr=native_d3d12_buffer(native12_.dynamic,native_pending_upload_.size(),0,0))||
      FAILED(hr=native_d3d12_buffer(native12_.work,UINT64(records)*sizeof(NativeDrawWorkGpu),sizeof(NativeDrawWorkGpu),0))||
      FAILED(hr=native_d3d12_buffer(native12_.arguments,UINT64(layout.arguments.size())*sizeof(UINT),0,kD3D12BufferUnordered|kD3D12BufferIndirect))||
      FAILED(hr=native_d3d12_buffer(native12_.visible,UINT64((std::max)(1u,layout.triangles))*16u,16u,kD3D12BufferUnordered))||
      FAILED(hr=native_d3d12_persistent(native12_.world_vertices,UINT64(sizeof(NativeWorldVertexGpu))*64u,sizeof(NativeWorldVertexGpu),0))||
      FAILED(hr=ensure_native_world_states())||
      FAILED(hr=native_d3d12_persistent(native12_.world_states,UINT64(sizeof(NativeWorldStateGpu))*64u,sizeof(NativeWorldStateGpu),0))||
      FAILED(hr=native_d3d12_persistent(native12_.animation,64u,0,0))||
      FAILED(hr=native_d3d12_persistent(native12_.pose_nodes,UINT64(sizeof(NativePoseNodeGpu))*16u,sizeof(NativePoseNodeGpu),0)))
    return finish(hr);
   if(!bake){
    if(!native_pending_upload_.empty()&&FAILED(hr=d3d12_->write_buffer(*native12_.dynamic,0,native_pending_upload_.data(),native_pending_upload_.size())))return finish(hr);
    if(FAILED(hr=d3d12_->write_buffer(*native12_.work,0,layout.work.data(),UINT64(records)*sizeof(NativeDrawWorkGpu))))return finish(hr);
    profiler_.add_upload(native_pending_upload_.size()+records*sizeof(NativeDrawWorkGpu));
   }
   work=bake?layout.static_bake.work12.get():native12_.work.get();
   if(FAILED(hr=upload_native_instances_d3d12()))return finish(hr);
   if(FAILED(hr=upload_native_terrain_frame_d3d12()))return finish(hr);
   if(FAILED(hr=upload_native_terrain_phases_d3d12()))return finish(hr);
   if(FAILED(hr=prepare_native_poses_d3d12()))return finish(hr);
  }
  // The slots every shadow pass reads (as the D3D11 flush binds them).
  const auto bind_common=[&](){
   d3d12_->clear_slots();
   d3d12_->set_constants(1,&native_world_lighting_,sizeof(native_world_lighting_));
   const auto& constants=rain?rain12_.constants:point?shadow12_.point_constants:shadow12_.constants;
   d3d12_->set_constants(2,&constants,sizeof(constants));
   d3d12_->set_constants(3,clip.data(),UINT(clip.size()));
   d3d12_->set_resource(2,native12_.dynamic.get());
   d3d12_->set_resource(5,native12_.draws.get());
   d3d12_->set_resource(10,native12_.animation.get());
   d3d12_->set_resource(12,native12_.pose_nodes.get());
   d3d12_->set_resource(13,native12_.instances.get());
 d3d12_->set_resource(126,native12_.instances.get());
   d3d12_->set_resource(16,work);
   if(bake&&active->masked)d3d12_->set_resource(35,native_shadow_work_mask12_.get()); // terrain bake: this redraw's per-work map masks
   d3d12_->set_resource(19,native12_.layer_shifts.get());
   d3d12_->set_resource(127,native12_.terrain_frame.get());d3d12_->set_resource(72,fire_burn_mask12());if(fire_burn_bound_)d3d12_->set_resource(74,fire_burn_materials12());
   // Units cast from where they are drawn: sunk into trail troughs (nativeFigureTrailSink, t36).
   d3d12_->set_resource(36,scene_composited_?nullptr:snow_terrain12_.get());
  };
  if(records&&native_preparation_count_){
   bind_common();
   d3d12_->set_unordered(3,native12_.world_vertices.get());
   d3d12_->set_unordered(6,native12_.world_states.get());
   // Object motion: rewritten world vertices keep their old contents (u5, the b4 stamp).
   D3D12Buffer* const previous_world=previous_world_vertices_d3d12();
   d3d12_->set_unordered(5,previous_world);
   transition_profile_pass("compute_shadow_prepare");
   for(std::size_t dispatch=0;dispatch<layout.dispatches.size();++dispatch){
    const auto& range=layout.dispatches[dispatch];if(!range.preparations)continue;
    if(range.geometry_page>=native_geometry_pages_.size()||!native_geometry_pages_[range.geometry_page].d3d12)return finish(E_FAIL);
    d3d12_->set_resource(1,native_geometry_pages_[range.geometry_page].d3d12.get());
    const std::array<UINT,4> work_range{range.first,range.count,previous_world?upscale12_.motion_stamp:0u,0};
    d3d12_->set_constants(4,work_range.data(),sizeof(work_range));
    UINT width=0,height=0;if(!native_dispatch_dimensions(range.count,width,height))return finish(HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW));
    const UINT chunks=bake?active->dispatch_chunks[dispatch]:native_shadow_chunks(layout,range,false);
    if((hr=d3d12_->dispatch(*prepare_cs,width,height,chunks))!=S_OK||(hr=d3d12_->dispatch(*commit_cs,width,height,1))!=S_OK)
     return FAILED(hr)?finish(hr):wait(hr);
    ++perf_native_compute_dispatches_;++native_frame_stats_.native_compute_dispatches;
   }
   if(bake){native_world_prepared_vertices_+=active->prepared_vertices;native_shadow_prepared_records_+=active->preparations;}
   else for(const auto& item:layout.work)if(item.record!=UINT_MAX&&(item.flags&2u)){native_world_prepared_vertices_+=native_instance_records_[item.record].command.component_count;++native_shadow_prepared_records_;}
  }
  if(records)for(const auto& selected:native_pending_draws_){
   const auto& queued=native_instance_records_[selected.record].command;
   if(queued.world_slot>=native_prepared_mesh_slots_.size())continue;
   auto& slot=native_prepared_mesh_slots_[queued.world_slot];if(slot.world_generation==queued.world_generation&&slot.world_base==queued.world_base)slot.world_valid=true;
  }
  const UINT side=rain?native_rain_side_:point?native_point_shadow_side_:native_shadow_side_;
  for(UINT slice=0;slice<(rain?(rain_map?1u:0u):point?6u:5u);++slice){
   const UINT bit=1u<<slice;
   if(!(native_shadow_dirty_mask_&bit))continue;
   const bool static_layer=!point&&!rain&&native_shadow_phase_==1;
   D3D12Texture* target=rain?rain12_.shelter.get():static_layer?shadow12_.statics.get():shadow12_.maps.get();
   UINT target_slice=slice;
   RECT viewport{0,0,LONG(side),LONG(side)},cell{};
   if(point){
    const UINT page=native_point_shadow_slot_/PointShadowTextureLayout::cubes_per_atlas;
    const UINT face=(native_point_shadow_slot_%PointShadowTextureLayout::cubes_per_atlas)*6u+slice;
    const UINT tile=face%PointShadowTextureLayout::faces_per_slice,pitch=side+2;
    target=native_point_shadow_pages_[page].depth12.get();
    target_slice=face/PointShadowTextureLayout::faces_per_slice;
    cell={LONG(tile%PointShadowTextureLayout::atlas_tiles*pitch),LONG(tile/PointShadowTextureLayout::atlas_tiles*pitch),0,0};
    cell.right=cell.left+LONG(pitch);cell.bottom=cell.top+LONG(pitch);
    viewport={cell.left+1,cell.top+1,cell.right-1,cell.bottom-1};
   }
   D3D12Texture& maps=*target;
   if(static_layer){
    if(!(native_shadow_static_cleared_mask_&bit)){
     d3d12_->clear_depth_slice(maps,slice,1.f);
     native_shadow_static_cleared_mask_|=bit;native_shadow_static_valid_[slice]=false;
    }
   }else if(!(native_shadow_cleared_mask_&bit)){
    // The dynamic overlay starts from the cached static layer.
    if(point)d3d12_->clear_depth_slice(maps,target_slice,1.f,&cell);
    else if(!rain&&native_shadow_phase_==2&&native_shadow_static_valid_[slice]){
     if(FAILED(hr=d3d12_->copy_texture(maps,slice,*shadow12_.statics,slice)))return finish(hr);
    }else{
     d3d12_->clear_depth_slice(maps,slice,1.f);
     if(rain){const float none[4]{};d3d12_->clear_texture(*rain12_.foliage,none);}
    }
    native_shadow_cleared_mask_|=bit;
   }
   if(records){
    if(FAILED(hr=upload_native_shadow_constants(slice)))return finish(hr);
    // The build: this map's visible triangles and arguments from the batches' starting values.
    if(!layout.arguments.empty()&&FAILED(hr=d3d12_->write_buffer(*native12_.arguments,0,layout.arguments.data(),layout.arguments.size()*sizeof(UINT))))return finish(hr);
    bind_common();
    d3d12_->set_resource(15,native12_.world_states.get());
    d3d12_->set_unordered(0,native12_.visible.get());
    d3d12_->set_unordered(1,native12_.arguments.get());
    transition_profile_pass("compute_shadow_build");
    for(std::size_t dispatch=0;dispatch<layout.geometry_dispatches.size();++dispatch){
     const auto& range=layout.geometry_dispatches[dispatch];
     if(range.geometry_page>=native_geometry_pages_.size()||!native_geometry_pages_[range.geometry_page].d3d12)return finish(E_FAIL);
     d3d12_->set_resource(1,native_geometry_pages_[range.geometry_page].d3d12.get());
     const std::array<UINT,4> work_range{range.first,range.count,0,0};
     d3d12_->set_constants(4,work_range.data(),sizeof(work_range));
     UINT width=0,height=0;if(!native_dispatch_dimensions(range.count,width,height))return finish(HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW));
     const UINT chunks=bake?active->geometry_chunks[dispatch]:native_shadow_chunks(layout,range,true);
     if((hr=d3d12_->dispatch(*build_cs,width,height,chunks))!=S_OK)return FAILED(hr)?finish(hr):wait(hr);
     ++perf_native_compute_dispatches_;++native_frame_stats_.native_compute_dispatches;
    }
    // The depth draws: every batch with triangles in this map.
    bind_common();
    d3d12_->set_resource(4,native12_.visible.get());
    d3d12_->set_resource(8,native12_.world_vertices.get());
    // The shelter bake also writes its foliage mask (nativeShadowPS colour);
    // sun and point maps bind no colour target.
    d3d12_->set_depth_target(&maps,target_slice,viewport,rain?rain12_.foliage.get():nullptr);
    static const char* const cascades[5]={"owned_directional_shadow_c0","owned_directional_shadow_c1","owned_directional_shadow_c2","owned_directional_shadow_c3","owned_directional_shadow_c4"};
    transition_profile_pass(rain?"precip_shelter":point?"owned_point_shadow":cascades[slice]);
    for(UINT group=0;group<batches;++group){
     const auto& batch=layout.batches[group];
     const auto group_triangles=batch.slice_triangles[slice];const auto group_draw_count=batch.slice_records[slice];
     if(!group_triangles)continue; // CPU-known empty means no indirect call
     if(batch.geometry_page>=native_geometry_pages_.size()||!native_geometry_pages_[batch.geometry_page].d3d12)return finish(E_FAIL);
     const auto& bucket=native_state_buckets_[batch.state_bucket];
     d3d12_->set_resource(1,native_geometry_pages_[batch.geometry_page].d3d12.get());
     FixedState state=bucket.state;
     state.render[D3DRENDERSTATE_ZENABLE]=D3DZB_TRUE;state.render[D3DRENDERSTATE_ZWRITEENABLE]=TRUE;state.render[D3DRENDERSTATE_ZFUNC]=D3DCMP_LESSEQUAL;
     state.render[D3DRENDERSTATE_ALPHABLENDENABLE]=FALSE;
     // A cut-out caster casts two-sided, as the D3D11 flush: colour-queued
     // records (object-space and neighbour casters) carry the game's cull mode.
     if(bucket.opacity==NativeOpacityClass::cutout)state.render[D3DRENDERSTATE_CULLMODE]=D3DCULL_NONE;
     if(shadow_foliage_opaque(native_far_foliage_opaque_,point,slice,batch.foliage?native_foliage_part_flag:0u,
        state.render[D3DRENDERSTATE_ALPHATESTENABLE]!=0))
      state.render[D3DRENDERSTATE_ALPHATESTENABLE]=FALSE;
     // Shelter bake foliage, as the D3D11 flush: alpha tested at a low fixed
     // reference, its mip along the card's less foreshortened axis
     // (nativeShadowPS, nativeContactLayout.z), so only leaf texels cover.
     const bool shelter_foliage=rain&&batch.foliage&&(bucket.texture_srv||bucket.texture_d3d12);
     if(shelter_foliage){
      state.render[D3DRENDERSTATE_ALPHATESTENABLE]=TRUE;
      state.render[D3DRENDERSTATE_ALPHAFUNC]=D3DCMP_GREATER;state.render[D3DRENDERSTATE_ALPHAREF]=64; // alpha > 0.25
     }
     Constants constants{};constants.native_triangle_base=batch.output_triangle_base;
     constants.layout_origin[2]=shelter_foliage?1.f:0.f;
     constants.textured=state.render[D3DRENDERSTATE_ALPHATESTENABLE]&&(bucket.texture_srv||bucket.texture_d3d12)?1u:0u;
     constants.alpha_test=state.render[D3DRENDERSTATE_ALPHATESTENABLE]!=0;constants.alpha_func=state.render[D3DRENDERSTATE_ALPHAFUNC];
     constants.alpha_ref=(state.render[D3DRENDERSTATE_ALPHAREF]&255)/256.f;
     constants.alpha_op=state.stage[0][D3DTSS_ALPHAOP];constants.alpha_arg1=state.stage[0][D3DTSS_ALPHAARG1];constants.alpha_arg2=state.stage[0][D3DTSS_ALPHAARG2];
     constants.texture_factor[3]=(state.render[D3DRENDERSTATE_TEXTUREFACTOR]>>24)/255.f;
     d3d12_->set_constants(0,&constants,sizeof(constants));
     D3D12NativeState draw_state;
     const DWORD cull=state.render[D3DRENDERSTATE_CULLMODE];
     draw_state.raster=fixed_raster_desc(cull,false,true);
     draw_state.depth=fixed_depth_desc(state.render[D3DRENDERSTATE_ZENABLE],state.render[D3DRENDERSTATE_ZWRITEENABLE],state.render[D3DRENDERSTATE_ZFUNC]);
     // Neighbour-map casters are pancaked (no depth clip), as on D3D11.
     if(active==&native_neighbour_shadow_bake_)draw_state.raster.DepthClipEnable=FALSE;
     // Terrain sun casters take a slope-scaled bias clamped in world depth (L-084).
     // D3D12 bakes the bias into the pipeline. The exact clamp follows the
     // map's depth range (bias[1], 16-unit bins that move with pans and zoom),
     // so each new range was a new pipeline whose first use left every map of
     // the pass unshadowed for that frame (wait below): the D3D12 shadow
     // blink. Quarter-octave floor of the clamp: at most 16% under the D3D11
     // value, a small fixed pipeline set the start warm-up keeps.
     if(bucket.semantic_kind==SemanticKind::terrain){
      constexpr float terrain_slope_texels=2.f,terrain_clamp_world=.5f;
      draw_state.raster.SlopeScaledDepthBias=terrain_slope_texels;
      const float clamp=point?0.f:terrain_clamp_world*native_shadow_frame_.slices[slice].bias[1];
      draw_state.raster.DepthBiasClamp=clamp>0?std::exp2(std::floor(std::log2(clamp)*4.f)/4.f):0.f;
     }
     if(constants.alpha_test){
      const auto sampling=world_texture_sampler(state.stage[0][D3DTSS_MINFILTER],state.stage[0][D3DTSS_MAGFILTER],state.stage[0][D3DTSS_MIPFILTER],
       state.stage[0][D3DTSS_ADDRESSU],state.stage[0][D3DTSS_ADDRESSV],true,1u<<configured_integer(debug_overlay_.config(),"anisotropy",3));
      d3d12_->set_sampler(0,sampling.description);
     }
     // Every shelter caster runs the pixel stage, so the mask holds the top
     // surface's class (a roof over a crown is solid).
     static const ShaderProgram no_pixel{};
     hr=d3d12_->draw_indirect(*shadow_vs,constants.alpha_test||rain?*shadow_ps:no_pixel,draw_state,&viewport,*native12_.arguments,group*16u);
     if(FAILED(hr))return finish(hr);
     if(d3d12_not_done(hr)){
      // Self-report: which map and caster class lost this frame's sun shadows.
      static unsigned reports=0;
      if(!rain&&!point&&reports<64){
       ++reports;char detail[192];
       std::snprintf(detail,sizeof(detail),"frame=%llu slice=%u phase=%u %s caster=%s clamp=%.6g dirty=0x%x",
        static_cast<unsigned long long>(native_frame_index_),slice,native_shadow_phase_,hr==S_FALSE?"pipeline_compiling":"ring_dropped",
        bucket.semantic_kind==SemanticKind::terrain?"terrain":constants.alpha_test?"cutout":"opaque",
        double(draw_state.raster.DepthBiasClamp),unsigned(native_shadow_dirty_mask_));
       bridge_append_probe_event("d3d12_sun_shadow_pass_waited",detail);
      }
      return wait(hr);
     }
     ++native_shadow_draws_;native_shadow_triangles_+=group_triangles;
     ++native_frame_stats_.native_shadow_draws;native_frame_stats_.native_shadow_triangles+=group_triangles;
     perf_native_draws_+=group_draw_count;++perf_native_batches_;perf_native_vertices_+=group_triangles*3;
     profiler_.add_draw(group_triangles,group_draw_count);profiler_.add_batch();
     d3d12_->count(point?"point shadow caster batches drawn":"sun shadow caster batches drawn");
    }
   }
   // Renderer-owned per-frame casters (ground cover, critters) join the close maps'
   // per-frame layer once per collection, through the same map, viewport and
   // slice constants as the records, as the D3D11 flush draws them.
   if(!bake&&!point&&!rain&&!static_layer&&slice<3&&!(native_shadow_extras_mask_&bit)){
    native_shadow_extras_mask_|=std::uint8_t(bit);
    if(FAILED(hr=upload_native_shadow_constants(slice)))return finish(hr);
    transition_profile_pass("owned_shadow_casters");
    d3d12_->clear_slots();
    d3d12_->set_constants(2,&shadow12_.constants,sizeof(shadow12_.constants));
    d3d12_->set_depth_target(&maps,target_slice,viewport);
    draw_ground_cover_shadows_d3d12(slice,&viewport);draw_critter_shadows_d3d12(slice,&viewport);
    // rt_sun_shadows: the traced mask replaces the sun maps' result on every
    // prepassed receiver, and these casters are not in the ray tracing scene.
    // They are drawn once more, alone, into close slice 0, which the single
    // close map of that mode (slice 2, the whole close range) leaves free;
    // the traced mask takes the smaller of its rays and that layer
    // (nativeRtCoverShadow). The layer's fit is a window of this map's light
    // space (the same light axes and depth, so this map's caster lists hold
    // every caster of it): rt_sun_cover_range metres around a point that far
    // ahead of the eye, its centre on whole texels (no crawl as the camera
    // moves). Blade shadows need those texels; past the window the cover
    // casts no sun shadow in this mode. Slice 0 draws the cover with its
    // live sway (gcShadowVS: frozen only on maps 1 and 2).
    native_rt_cover_source_=native_rt_cover_layer_=0xFF;
    if(native_traced_sun_&&slice==2&&native_shadow_frame_.slices[2].enabled&&!native_shadow_frame_.slices[0].enabled&&
       configured_boolean(debug_overlay_.config(),"rt_sun_cover_shadows",true)){
     const auto& fit=native_shadow_frame_.slices[2];
     const auto scene=bridge_scene_world_to_clip_snapshot();
     float clip_to_world[16];
     const float per_metre=std::sqrt(fit.world_to_light[0]*fit.world_to_light[0]+fit.world_to_light[1]*fit.world_to_light[1]+fit.world_to_light[2]*fit.world_to_light[2]);
     if(scene.valid&&ambient_occlusion_invert(scene.world_to_clip.data(),clip_to_world)&&per_metre>1e-9f){
      const float range=(std::clamp)(configured_floating_point(debug_overlay_.config(),"rt_sun_cover_range",16.f),4.f,64.f);
      // The view's horizontal direction through the screen centre.
      const auto unproject=[&](float z,float out[3]){
       float w=clip_to_world[14]*z+clip_to_world[15];if(std::fabs(w)<1e-12f)w=1e-12f;
       for(int axis=0;axis<3;++axis)out[axis]=(clip_to_world[axis*4+2]*z+clip_to_world[axis*4+3])/w;
      };
      float near_point[3],far_point[3];unproject(0.f,near_point);unproject(.5f,far_point);
      float forward[2]={far_point[0]-near_point[0],far_point[1]-near_point[1]};
      const float length=std::sqrt(forward[0]*forward[0]+forward[1]*forward[1]);
      if(length>1e-6f){forward[0]/=length;forward[1]/=length;}else forward[0]=forward[1]=0;
      // (The eye is the near plane's centre of this snapshot: the fog frame's eye is the last drawn frame's.)
      const float centre[3]={near_point[0]+forward[0]*range*.75f,near_point[1]+forward[1]*range*.75f,near_point[2]};
      const float zoom=(std::max)(1.f,1.f/(range*per_metre));
      float at[2];
      for(int row=0;row<2;++row){
       at[row]=fit.world_to_light[row*4]*centre[0]+fit.world_to_light[row*4+1]*centre[1]+fit.world_to_light[row*4+2]*centre[2]+fit.world_to_light[row*4+3];
       const float texel=2.f/(float(side)*zoom); // one layer texel in this map's NDC
       at[row]=std::floor(at[row]/texel+.5f)*texel;
      }
      native_rt_cover_light_=fit.world_to_light;
      for(int row=0;row<2;++row){
       for(int column=0;column<4;++column)native_rt_cover_light_[row*4+column]*=zoom;
       native_rt_cover_light_[row*4+3]-=zoom*at[row];
      }
      native_rt_cover_bias_=fit.bias;native_rt_cover_bias_[0]/=zoom;
      native_rt_cover_source_=2;native_rt_cover_layer_=0;
      // The layer's pass: its matrix at slice 0 of the block (b2's current slice 0).
      native_rt_cover_drawing_=true;const HRESULT uploaded=upload_native_shadow_constants(0);native_rt_cover_drawing_=false;
      if(SUCCEEDED(uploaded)){
       transition_profile_pass("rt_sun_cover_casters");
       d3d12_->set_constants(2,&shadow12_.constants,sizeof(shadow12_.constants));
       d3d12_->clear_depth_slice(maps,0,1.f);
       d3d12_->set_depth_target(&maps,0,viewport);
       draw_ground_cover_shadows_d3d12(slice,&viewport);draw_critter_shadows_d3d12(slice,&viewport);
       d3d12_->count("rt_sun_shadows: ground cover and critter caster layers drawn");
      }else native_rt_cover_source_=native_rt_cover_layer_=0xFF;
     }
    }
   }
   if(static_layer){native_shadow_static_valid_[slice]=true;native_shadow_neighbour_drawn_[slice]=object_caster_key();}
   else if(!point&&!rain)native_shadow_valid_[slice]=true;
  }
  return finish(S_OK);
 }catch(const std::bad_alloc&){return finish(E_OUTOFMEMORY);}
}

// ---- Temporal upscalers on D3D12 (DLSS, FSR 2): composite_world's order on
// D3D11. The plain copy and the post passes write the render-size resolved
// image; the shared begin_upscaler_frame decides the reset and draws the
// camera motion (camera_motion_d3d12); the upscaler writes the output-size
// image; finish_upscaler_frame presents it (present_upscaled_d3d12). A frame
// the upscaler cannot take shows the filtered upscale of the resolved image
// (SceneComposite::upscale), as D3D11 does.

// The upscaler's targets at this frame's sizes (render: width_ x height_,
// output: the back buffer's), remade when a size changes.
HRESULT Renderer::ensure_upscaler_d3d12(){
 auto& up=upscale12_;
 HRESULT hr=S_OK;
 // All four render-size images together: the plain render-scale path leaves
 // only resolved at this size, so a switch to DLSS/FSR at the same size must
 // still create the motion and object images.
 if(!up.resolved||!d3d12_->texture_matches(*up.resolved,width_,height_,DXGI_FORMAT_B8G8R8A8_UNORM)||
    !up.motion||!d3d12_->texture_matches(*up.motion,width_,height_,DXGI_FORMAT_R16G16_FLOAT)||
    !up.object_motion||!d3d12_->texture_matches(*up.object_motion,width_,height_,DXGI_FORMAT_R16G16_FLOAT)||
    !up.object_depth||!d3d12_->texture_matches(*up.object_depth,width_,height_,DXGI_FORMAT_R32_FLOAT)){
  up.resolved.reset();up.motion.reset();up.object_motion.reset();up.object_depth.reset();
  if(FAILED(hr=d3d12_->create_target(width_,height_,up.resolved,DXGI_FORMAT_B8G8R8A8_UNORM,DXGI_FORMAT_UNKNOWN))||
     FAILED(hr=d3d12_->create_target(width_,height_,up.motion,DXGI_FORMAT_R16G16_FLOAT,DXGI_FORMAT_UNKNOWN))||
     FAILED(hr=d3d12_->create_target(width_,height_,up.object_motion,DXGI_FORMAT_R16G16_FLOAT,DXGI_FORMAT_UNKNOWN))||
     FAILED(hr=d3d12_->create_target(width_,height_,up.object_depth,DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_UNKNOWN))){
   up.resolved.reset();up.motion.reset();up.object_motion.reset();up.object_depth.reset();
   return hr;
  }
  dlss_reset_=true;
 }
 if(!up.output||!d3d12_->texture_matches(*up.output,output_width_,output_height_,DXGI_FORMAT_R8G8B8A8_UNORM)){
  up.output.reset();
  if(FAILED(hr=d3d12_->create_storage_texture(output_width_,output_height_,0,DXGI_FORMAT_R8G8B8A8_UNORM,up.output)))return hr;
  dlss_reset_=true;
 }
 return S_OK;
}

// The guide targets at the render size, at every world bind: DLSS Ray
// Reconstruction's guides (rt_ray_reconstruction; DLSS-RR Integration Guide
// 3.4: normals RGBA16F, the format it recommends, with the roughness packed
// in alpha; the albedos any 3-channel format that is not sRGB) and the
// surfaces rt_direct_lights samples. With rt_direct_lights the albedo
// targets are RGBA16: their alpha carries the lighting factor and the fog
// share (native_shader.h nativeGuideLight). A failure leaves DLSS Super
// Resolution, or the lamp loop, and names it.
void Renderer::ensure_guide_targets_d3d12(){
 auto& up=upscale12_;
 if(!guides_wanted_d3d12()){up.guide_normal.reset();up.guide_diffuse.reset();up.guide_specular.reset();return;}
 const bool light=guide_light_wanted_d3d12(); // rt_direct_lights or rt_indirect_light
 const DXGI_FORMAT albedo=light?DXGI_FORMAT_R16G16B16A16_UNORM:DXGI_FORMAT_R8G8B8A8_UNORM;
 if(up.guide_normal&&d3d12_->texture_matches(*up.guide_normal,width_,height_,DXGI_FORMAT_R16G16B16A16_FLOAT)&&
    up.guide_diffuse&&d3d12_->texture_matches(*up.guide_diffuse,width_,height_,albedo)&&
    up.guide_specular&&d3d12_->texture_matches(*up.guide_specular,width_,height_,albedo))return;
 up.guide_normal.reset();up.guide_diffuse.reset();up.guide_specular.reset();
 if(FAILED(d3d12_->create_target(width_,height_,up.guide_normal,DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_UNKNOWN))||
    FAILED(d3d12_->create_target(width_,height_,up.guide_diffuse,albedo,DXGI_FORMAT_UNKNOWN))||
    FAILED(d3d12_->create_target(width_,height_,up.guide_specular,albedo,DXGI_FORMAT_UNKNOWN))){
  up.guide_normal.reset();up.guide_diffuse.reset();up.guide_specular.reset();
  if(ray_reconstruction_on()){
   std::snprintf(rr_failure_,sizeof(rr_failure_),"%s","its guide images could not be created");
   bridge_append_probe_event("dlss_rr_failed",rr_failure_);
  }
  if(rt_direct_enabled_)d3d12_->omit("rt_direct_lights (guide images could not be created: the lamp loop stays)",false);
  if(rt_indirect_enabled_)d3d12_->omit("rt_indirect_light (guide images could not be created: the bounce volume stays)",false);
  return;
 }
 dlss_reset_=true;
 char text[200];
 std::snprintf(text,sizeof(text),"%ux%u: normal and roughness RGBA16F, diffuse albedo %s, specular albedo %s, %.1f MB",width_,height_,
               light?"RGBA16":"RGBA8",light?"RGBA16":"RGBA8",
               double(width_)*double(height_)*(light?24.0:16.0)/1048576.0);
 bridge_append_probe_event("dlss_rr_guides",text);
}

// rt_ray_reconstruction, between frames: whether DLSS Ray Reconstruction
// takes DLSS Super Resolution's place from the next world frame, and one
// dlss_mode event (the probe log) whenever the answer or its reason changes.
bool Renderer::decide_ray_reconstruction_d3d12(int dlss_mode,int fsr_mode){
 const bool wanted=configured_boolean(debug_overlay_.config(),"rt_ray_reconstruction",false);
 const char* reason=nullptr;
 if(wanted){
  if(rr_available_<0)rr_available_=x64remote::dlss_ray_reconstruction_caps(rr_unavailable_,sizeof(rr_unavailable_))?1:0;
  const auto& config=debug_overlay_.config();
  if(dlss_mode<0||fsr_mode>=0)reason="DLSS upscaling is off (it applies only with Upscaling: NVIDIA DLSS)";
  // Only the ray traced lighting needs its denoiser (user decision 2026-11-13).
  else if(!configured_boolean(config,"rt_sun_shadows",false)&&!configured_boolean(config,"rt_direct_lights",false)&&
          !configured_boolean(config,"rt_indirect_light",false)&&!configured_boolean(config,"rt_reflections",false))
   reason="ray tracing is off (it applies only with rt_sun_shadows, rt_direct_lights or rt_indirect_light on)";
  else if(d3d12_->raytracing_unavailable())reason="this adapter cannot trace rays (rt_sun_shadows, rt_direct_lights and rt_indirect_light do nothing here)";
  else if(!rr_available_)reason=rr_unavailable_[0]?rr_unavailable_:"not available";
  else if(rr_failure_[0])reason=rr_failure_;
  else if(native12_.ready&&native12_.guide_twins.empty())reason="the guide programs are missing";
 }
 const bool on=wanted&&!reason;
 const int state=(wanted?1:0)|(on?2:0)|(dlss_mode>=0&&fsr_mode<0?4:0);
 if(state==rr_logged_state_&&reason==rr_logged_reason_)return on;
 // The first state of a session without DLSS and without the option is not news.
 const bool news=!(rr_logged_state_<0&&!wanted&&dlss_mode<0);
 rr_logged_state_=state;rr_logged_reason_=reason;
 if(!news)return on;
 std::string line;
 if(on)line="DLSS Ray Reconstruction (rt_ray_reconstruction on): it replaces DLSS Super Resolution; guide images written";
 else if(dlss_mode>=0&&fsr_mode<0)line=wanted?std::string("DLSS Super Resolution; rt_ray_reconstruction is on but not applied: ")+reason:
                                               std::string("DLSS Super Resolution (rt_ray_reconstruction off)");
 else line=wanted?std::string("no DLSS; rt_ray_reconstruction is on but not applied: ")+reason:std::string("no DLSS (rt_ray_reconstruction off)");
 bridge_append_probe_event("dlss_mode",line.c_str());
 return on;
}

// apply_render_scale on D3D12, between frames: the render scale and the
// upscaler modes of the options, the world size, and what an upscaler turned
// off leaves (FSR's images, the targets, the host's NGX feature). The world
// targets follow the size at the next bind_world_d3d12. Never while a world
// target is bound: that frame keeps the size it was drawn at.
void Renderer::apply_upscaler_mode_d3d12(){
 if(world_d3d12_bound_)return;
 // Init owns no feature and snapshots no quality. Publish readiness here,
 // then let the usual current-config comparison resize/reset the next frame.
 // Retry the capability checks which may have observed the pending state,
 // once only; later evaluate failures remain session failures.
 if(dlss_initializing_){
  x64remote::DlssCaps caps{};x64remote::dlss_caps(caps);
  dlss_initializing_=caps.initializing;
  if(!dlss_initializing_){
   set_dlss_unavailable_reason(caps.available?nullptr:caps.reason);
   rr_available_=-1;
   fg_caps_checked_=false;fg_dlssg_unavailable_[0]=0;fg_reason_mode_=-1;
  }
 }
 const int percent=std::clamp(configured_integer(debug_overlay_.config(),"render_scale",100),kRenderScaleMinimum,100);
 const int dlss=configured_dlss_mode(),fsr=configured_fsr_mode();
 // rt_ray_reconstruction: Ray Reconstruction in Super Resolution's place (its
 // own render size; the host recreates the feature, its history starts over).
 const bool rr=decide_ray_reconstruction_d3d12(dlss,fsr);
 // The lamp light and the bounce light run without Ray Reconstruction too (user decision 2026-11-15, laptop
 // j227: +2.2 to +5.7 ms over the traced sun alone, at the noise level the user accepted).
 // rt_direct_lights from the next world frame; off, its images go at once.
 {const bool wanted=configured_boolean(debug_overlay_.config(),"rt_direct_lights",false);
  const char* reason=!wanted?nullptr:d3d12_->raytracing_unavailable()?d3d12_->raytracing_unavailable():
   native12_.ready&&native12_.guide_twins.empty()?"the guide programs are missing":nullptr;
  const bool on=wanted&&!reason;
  if(reason&&!rt_direct12_.unavailable_logged){
   rt_direct12_.unavailable_logged=true;
   bridge_append_probe_event("rt_direct_lights_omitted",reason);
  }
  if(!wanted)rt_direct12_.unavailable_logged=false;
  if(on!=rt_direct_enabled_){rt_direct_enabled_=on;if(!on)release_rt_direct_d3d12();}}
 // rt_indirect_light, by the same rule; rt_ambient_occlusion alone runs its bounce rays too (for their occlusion only).
 {const bool lit_wanted=configured_boolean(debug_overlay_.config(),"rt_indirect_light",false);
  const bool wanted=lit_wanted||configured_boolean(debug_overlay_.config(),"rt_ambient_occlusion",false);
  const char* reason=!wanted?nullptr:d3d12_->raytracing_unavailable()?d3d12_->raytracing_unavailable():
   native12_.ready&&native12_.guide_twins.empty()?"the guide programs are missing":nullptr;
  const bool on=wanted&&!reason;
  if(reason&&!rt_indirect12_.unavailable_logged){
   rt_indirect12_.unavailable_logged=true;
   bridge_append_probe_event("rt_indirect_light_omitted",reason);
  }
  if(!wanted)rt_indirect12_.unavailable_logged=false;
  // Either change starts the images and the history over (the reservoirs of an occlusion-only run are empty).
  if(on!=rt_indirect_enabled_||(on&&lit_wanted)!=rt_indirect_lit_){rt_indirect_enabled_=on;rt_indirect_lit_=on&&lit_wanted;release_rt_indirect_d3d12();}}
 // rt_reflections: the water rays need only the ray tracing scene (and the
 // water's own reflection pass, Water reflections); the smooth surfaces read
 // the guide images and the material response's roughness, so they need
 // material quality above Off (the material-off twins' guide is matte).
 {const bool wanted=configured_boolean(debug_overlay_.config(),"rt_reflections",false);
  const char* reason=!wanted?nullptr:d3d12_->raytracing_unavailable()?d3d12_->raytracing_unavailable():nullptr;
  const bool on=wanted&&!reason;
  if(reason&&!rt_reflect12_.unavailable_logged){
   rt_reflect12_.unavailable_logged=true;
   bridge_append_probe_event("rt_reflections_omitted",reason);
  }
  if(!wanted)rt_reflect12_.unavailable_logged=false;
  if(on!=rt_reflect_enabled_){rt_reflect_enabled_=on;if(!on)release_rt_reflect_d3d12();}
  const bool surfaces=on&&configured_boolean(debug_overlay_.config(),"rt_reflection_surfaces",true);
  // A preset's smooth-surface reflections stay tied to Ray Reconstruction (director decision 2026-11-15:
  // only the lamp and bounce light were measured without it); a custom mix shows what its options say.
  const char* surfaces_reason=!surfaces?nullptr:!material_level()?"smooth surfaces: material quality is Off (their roughness comes from the material response); water keeps its traced rays":
   !rr&&detect_rt_quality(debug_overlay_.config())!=RtQuality::custom?"smooth surfaces: a ray tracing preset draws them only with DLSS Ray Reconstruction on; water keeps its traced rays":
   native12_.ready&&native12_.guide_twins.empty()?"smooth surfaces: the guide programs are missing":nullptr;
  if(surfaces_reason&&!rt_reflect12_.surfaces_logged){rt_reflect12_.surfaces_logged=true;bridge_append_probe_event("rt_reflections_omitted",surfaces_reason);}
  if(!surfaces_reason)rt_reflect12_.surfaces_logged=false;
  rt_reflect_surfaces_=surfaces&&!surfaces_reason;}
 // rt_ambient_occlusion: the bounce rays' occlusion (with or without rt_indirect_light).
 {const bool on=rt_indirect_enabled_&&configured_boolean(debug_overlay_.config(),"rt_ambient_occlusion",false);
  if(on!=rt_ao_enabled_){rt_ao_enabled_=on;if(!on)release_rt_ambient_occlusion_d3d12();}}
 // rt_emissive: with either of the two (particle lamps, lava at bounce hits).
 rt_emissive_enabled_=(rt_direct_enabled_||rt_indirect_enabled_)&&configured_boolean(debug_overlay_.config(),"rt_emissive",true);
 if(percent==render_scale_percent_&&dlss==dlss_mode_&&fsr==fsr_mode_&&rr==rr_enabled_)return;
 render_scale_percent_=percent;
 const bool mode_changed=dlss!=dlss_mode_||fsr!=fsr_mode_;
 const int old_dlss=dlss_mode_;
 if(rr!=rr_enabled_)dlss_reset_=true;
 dlss_mode_=dlss;fsr_mode_=fsr;rr_enabled_=rr;set_world_size();
 if(!mode_changed)return; // a render scale change alone: the targets follow the size
 dlss_reset_=true;dlss_previous_valid_=false;
 if(fsr_mode_<0){d3d12_->fsr_upscale_release();upscale12_.reactive.reset();}
 if(dlss_mode_<0)upscale12_.bias.reset();
 if(!temporal_upscaler())upscale12_.opaque.reset();
 if(dlss_mode_<0)upscale12_.sharpen.reset();
 if(!temporal_upscaler()){
  auto& up=upscale12_;
  up.resolved.reset();up.motion.reset();up.object_motion.reset();up.object_depth.reset();up.output.reset();up.previous_world.reset();
 }
 if(old_dlss>=0&&dlss_mode_<0){
#ifdef EID3D11_EMBEDDED
  embedded_d3d12_.dlss_release();
#else
  x64remote::dlss_release();
#endif
 }
}

// begin_dlss_object_motion on D3D12, at a world frame's start with an
// upscaler: no object motion anywhere (SceneComposite::clear_object_motion:
// zero motion, depth beyond any surface), and a new stamp for the world
// vertices rewritten from here on. D3D11 copies the whole used range every
// frame; here the prepare dispatches keep a vertex's old contents as they
// rewrite it (storeNativeWorldVertex, marked with the stamp), so only what
// moves is copied: a slot without this frame's stamp reads as unmoved,
// which is what the whole copy gave it.
void Renderer::begin_object_motion_d3d12() noexcept{
 auto& up=upscale12_;
 up.motion_constants_frame=UINT64_MAX;
 // rt_ray_reconstruction: the guide targets start each world frame at the
 // guide's sky defaults (DLSS-RR Integration Guide 3.4.1-3.4.4: diffuse
 // albedo 0.5, specular albedo, normal and roughness 0); the guide twins of
 // this frame's world draws write over them.
 up.guides_frame=UINT64_MAX;
 if(guides_wanted_d3d12()&&up.guide_normal&&up.guide_diffuse&&up.guide_specular&&!hit_test_active_){
  const float none[4]{},grey[4]{.5f,.5f,.5f,1.f};
  d3d12_->clear_texture(*up.guide_normal,none);d3d12_->clear_texture(*up.guide_diffuse,grey);d3d12_->clear_texture(*up.guide_specular,none);
  up.guides_frame=native_frame_index_;
  // rt_direct_lights: this frame's guide twins draw without their lamps
  // once the passes ran (their pipelines exist) and there are lamps to trace.
  if(rt_direct_enabled_&&rt_direct12_.ready&&!native_point_lights_.empty())rt_direct12_.traced_frame=native_frame_index_;
  // rt_indirect_light: and without the bounce volume, by the same rule.
  if(rt_indirect_lit_&&rt_indirect12_.ready)rt_indirect12_.traced_frame=native_frame_index_;
  // rt_reflections: and without the traced share of their environment reflection (the cut-off they draw with).
  if(rt_reflect_enabled_&&rt_reflect_surfaces_&&rt_reflect12_.ready){
   rt_reflect12_.traced_frame=native_frame_index_;
   rt_reflect12_.cutoff=(std::clamp)(configured_floating_point(debug_overlay_.config(),"rt_reflection_roughness",.5f),.1f,1.f);
  }
 }
 if(!temporal_upscaler()||!up.object_motion||!up.object_depth){up.previous_world.reset();return;}
 const float zero[4]{},far_away[4]{2.f,2.f,2.f,2.f};
 d3d12_->clear_texture(*up.object_motion,zero);
 d3d12_->clear_texture(*up.object_depth,far_away);
 if(!native12_.world_vertices||!native_world_vertex_used_){up.previous_world.reset();return;}
 // The stamps start over: no slot may still carry one of the last round.
 if(++up.motion_stamp==0){up.motion_stamp=1;up.previous_world.reset();}
 (void)previous_world_vertices_d3d12();
}

// The buffer the prepare dispatches keep rewritten world vertices' old
// contents in (u5) and the motion stages read (t46): the world vertex
// buffer's size (the same slots), zero when made (no slot carries a stamp).
// A world buffer grown since is followed here with what was kept so far (a
// vertex the shadow prepare rewrote before the growth is not rewritten
// again by the colour pass: its kept contents and stamp must survive).
// Null without an upscaler's world frame (the dispatches then keep nothing).
D3D12Buffer* Renderer::previous_world_vertices_d3d12() noexcept{
 auto& up=upscale12_;
 if(!up.motion_stamp||!temporal_upscaler()||!up.object_motion||!up.object_depth||!native12_.world_vertices)return nullptr;
 const UINT64 bytes=d3d12_->buffer_bytes(*native12_.world_vertices);
 if(!up.previous_world||d3d12_->buffer_bytes(*up.previous_world)!=bytes){
  D3D12BufferRef created;
  // Growth only (the same slots, more of them); a smaller buffer is a new world's.
  const UINT64 kept=up.previous_world?d3d12_->buffer_bytes(*up.previous_world):0;
  if(FAILED(d3d12_->create_buffer(bytes,sizeof(NativeWorldVertexGpu),kD3D12BufferUnordered,created))||
     (kept&&kept<bytes&&FAILED(d3d12_->copy_buffer(*created,*up.previous_world,kept)))){
   up.previous_world.reset();
   static bool reported=false;
   if(!reported){reported=true;bridge_append_probe_event("dlss_object_motion_omitted","previous world vertex copy could not be created; figures use camera motion");}
   return nullptr;
  }
  up.previous_world=std::move(created);
 }
 return up.previous_world.get();
}

// The object motion targets can be written now (a world pass with a temporal
// upscaler, its motion targets and last frame's world vertices), with the b11
// constants (NativeMotionConstants: last frame's unjittered world clip, the
// jitter and the render size) written once a world frame, as
// dlss_motion_constants_current on D3D11.
bool Renderer::object_motion_ready_d3d12() noexcept{
 auto& up=upscale12_;
 if(!temporal_upscaler()||scene_composited_||hit_test_active_||!world_d3d12_bound_||!world_d3d12_||!up.previous_world||
    !up.object_motion||!up.object_depth)return false;
 if(up.motion_constants_frame!=native_frame_index_){
  const auto scene=bridge_scene_world_to_clip_snapshot();
  const std::array<float,16>& previous=dlss_previous_valid_?dlss_previous_clip_:scene.valid?scene.world_to_clip:native_world_clip_;
  std::memcpy(up.motion_constants.data(),previous.data(),64);
  up.motion_constants[16]=dlss_jitter_[0];up.motion_constants[17]=dlss_jitter_[1];
  up.motion_constants[18]=float(width_);up.motion_constants[19]=float(height_);
  up.motion_constants[20]=sway_clock_[0];up.motion_constants[21]=sway_clock_[1]; // tree sway: last and this world frame's clock
  std::memcpy(&up.motion_constants[22],&up.motion_stamp,sizeof(UINT)); // the bits: this frame's rewritten world vertices (storeNativeWorldVertex)
  up.motion_constants_frame=native_frame_index_;
 }
 return true;
}

// SceneComposite::camera_motion on D3D12: cameraMotionPS over the render
// size from the world depth (t3), the object motion where a posed record
// wrote it (t1, t2), the unjittered current inverse and previous clip (b1).
void Renderer::camera_motion_d3d12(const float clip_to_world[16],const float previous[16]){
 auto& post=post12_;auto& up=upscale12_;
 if(post.programs.empty())SceneComposite::shader_programs(post.programs);
 if(post.programs.size()<10||!up.motion){d3d12_->omit("upscaler camera motion (programs or target missing)",false);return;}
 float values[48]{};
 std::memcpy(values,clip_to_world,64);std::memcpy(values+16,previous,64);
 values[32]=dlss_jitter_[0];values[33]=dlss_jitter_[1];values[34]=float(width_);values[35]=float(height_);
 const float metrics[4]={1.f/float(width_),1.f/float(height_),float(width_),float(height_)};
 d3d12_->clear_slots();
 d3d12_->set_constants(0,metrics,sizeof(metrics));
 d3d12_->set_constants(1,values,sizeof(values));
 d3d12_->set_resource(1,up.object_motion.get());
 d3d12_->set_resource(2,up.object_depth.get());
 d3d12_->set_depth_resource(3,world_d3d12_.get());
 d3d12_->set_target(up.motion.get());
 const HRESULT hr=d3d12_->draw(post.programs[0],post.programs[7],post_state(false,false),nullptr,3);
 d3d12_->clear_slots();d3d12_->set_target(nullptr);
 if(hr!=S_OK){ // no motion this frame: zero (the history is still reprojected by the camera next frame)
  const float zero[4]{};d3d12_->clear_texture(*up.motion,zero);
  d3d12_->omit("upscaler camera motion (pipeline not ready)",false);
 }
}

// SceneComposite::dlss_present on D3D12: the upscaled image to the back
// buffer (colour only: the D3D12 interface depth is never the world's).
void Renderer::present_upscaled_d3d12(){
 auto& post=post12_;
 const float metrics[4]={1.f/float(output_width_),1.f/float(output_height_),float(output_width_),float(output_height_)};
 d3d12_->clear_slots();
 d3d12_->set_constants(0,metrics,sizeof(metrics));
 d3d12_->set_resource(0,upscale12_.output.get());
 d3d12_->set_target(nullptr);
 const HRESULT hr=d3d12_->draw(post.programs[0],post.programs[8],post_state(false,false),nullptr,3);
 d3d12_->clear_slots();
 if(hr!=S_OK)d3d12_->omit("upscaled world present (pipeline not ready)",true);
 else profiler_.add_draw(1,1),profiler_.add_batch();
}

// The upscaler of the frame (DLSS or FSR 2), or the filtered upscale of the
// resolved image when it cannot run this frame (SceneComposite::upscale).
HRESULT Renderer::upscale_world_d3d12(){
 auto& post=post12_;
 if(post.programs.empty())SceneComposite::shader_programs(post.programs);
 if(post.programs.size()<10||!upscale12_.output||!upscale12_.resolved||!upscale12_.motion){d3d12_->omit("temporal upscaler (programs or targets missing)",true);return S_OK;}
 if(fsr_mode_>=0?render_fsr_d3d12():render_dlss_d3d12()){d3d12_->count(fsr_mode_>=0?"world frames upscaled (FSR 3.1)":"world frames upscaled (DLSS)");return S_OK;}
 const HRESULT hr=filtered_upscale_d3d12();
 if(hr==S_OK)d3d12_->count("world frames the upscaler did not take (filtered upscale)");
 return hr;
}

// SceneComposite::upscale on D3D12: the resolved image (render size) filtered
// to the back buffer with upscalePS, colour only (the D3D12 interface depth is
// never the world's). Below 100% render scale every world frame; with a
// temporal upscaler the frames it cannot take.
HRESULT Renderer::filtered_upscale_d3d12(){
 auto& post=post12_;
 if(post.programs.empty())SceneComposite::shader_programs(post.programs);
 if(post.programs.size()<6||!upscale12_.resolved){d3d12_->omit("world upscale (programs or resolved image missing)",true);return S_FALSE;}
 transition_profile_pass("scene_upscale");
 const float metrics[4]={1.f/float(width_),1.f/float(height_),float(width_),float(height_)};
 D3D11_SAMPLER_DESC linear{};linear.AddressU=linear.AddressV=linear.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
 linear.MaxLOD=0;linear.Filter=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
 D3D11_SAMPLER_DESC point=linear;point.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
 d3d12_->clear_slots();
 d3d12_->set_constants(0,metrics,sizeof(metrics));
 d3d12_->set_sampler(0,linear);d3d12_->set_sampler(1,point);
 d3d12_->set_resource(0,upscale12_.resolved.get());
 d3d12_->set_target(nullptr);
 const HRESULT hr=d3d12_->draw(post.programs[0],post.programs[5],post_state(false,false),nullptr,3);
 d3d12_->clear_slots();
 if(hr!=S_OK){d3d12_->omit("world upscale (pipeline not ready)",true);return S_FALSE;}
 if(!temporal_upscaler())d3d12_->count("world frames upscaled (render scale)");
 profiler_.add_draw(1,1);profiler_.add_batch();
 return S_OK;
}

// render_dlss on D3D12: NGX records its evaluation into the executor's list
// (record_external moves the inputs and the output to their states). False:
// DLSS failed this frame and is turned off with the reason on the list.
bool Renderer::render_dlss_d3d12(){
 // The first-Present deferral of render_dlss, kept for the same host.
#ifdef EID3D11_EMBEDDED
 if(!embedded_d3d12_presented_){dlss_reset_=true;dlss_previous_valid_=false;return false;}
#else
 if(!x64remote::host_presented()){dlss_reset_=true;dlss_previous_valid_=false;return false;}
#endif
 auto& up=upscale12_;
 // rt_ray_reconstruction on but this world frame has no guide targets (they
 // were not made or cleared for it): no evaluation this frame, rather than a
 // Super Resolution feature created for one frame in Ray Reconstruction's place.
 if(ray_reconstruction_on()&&!guide_frame_d3d12()){
  dlss_reset_=true;dlss_previous_valid_=false;d3d12_->count("world frames without Ray Reconstruction guide targets (no DLSS evaluation)");return false;
 }
 const UpscalerFrame frame=begin_upscaler_frame();
 static const float jitter_sign=[]{char value[8]{};return GetEnvironmentVariableA("EI_RENDERER_DLSS_JITTER_SIGN",value,sizeof(value))&&value[0]=='-'?-1.f:1.f;}();
 x64remote::DlssEvaluate evaluate{};
 evaluate.jitter_x=jitter_sign*dlss_jitter_[0];evaluate.jitter_y=jitter_sign*dlss_jitter_[1];
 evaluate.mv_scale_x=evaluate.mv_scale_y=1.f;
 evaluate.render_w=width_;evaluate.render_h=height_;evaluate.out_w=output_width_;evaluate.out_h=output_height_;
 evaluate.mode=dlss_mode_;evaluate.sharpness=0.f;evaluate.reset=frame.reset;evaluate.hdr=false;evaluate.depth_inverted=false;
 // The bias-current-colour mask: reactivePS compares the world colour as the
 // transparent effects started (capture_opaque_world_d3d12) with the finished one.
 evaluate.bias_mask=false;
 if(up.bias&&up.opaque&&up.opaque_frame==native_frame_index_&&post12_.programs.size()>10){
  transition_profile_pass("scene_dlss_bias");
  const float metrics[4]={1.f/float(width_),1.f/float(height_),float(width_),float(height_)};
  d3d12_->clear_slots();d3d12_->set_constants(0,metrics,sizeof(metrics));
  d3d12_->set_resource(0,up.opaque.get());d3d12_->set_resource(1,world_d3d12_.get());d3d12_->set_target(up.bias.get());
  const HRESULT drawn=d3d12_->draw(post12_.programs[0],post12_.programs[10],post_state(false,false),nullptr,3);
  d3d12_->clear_slots();
  if(drawn==S_OK){evaluate.bias_mask=true;profiler_.add_draw(1,1);profiler_.add_batch();d3d12_->count("DLSS frames with a bias mask");}
  else d3d12_->omit("DLSS bias mask (pipeline not ready)",false);
 }
 // rt_ray_reconstruction: DLSS Ray Reconstruction with this frame's guide
 // targets in Super Resolution's place (the same colour, depth, motion,
 // output and bias mask).
 const bool ray_reconstruction=ray_reconstruction_on();
 evaluate.ray_reconstruction=ray_reconstruction;
 // rt_reflections: this frame's specular hit distances and the camera matrices they are read with (guide 3.4.9).
 const bool hit_distance=ray_reconstruction&&rt_reflect12_.hit_frame==native_frame_index_&&rt_reflect12_.hit_distance;
 if(hit_distance){
  evaluate.specular_hit_distance=true;
  std::memcpy(evaluate.world_to_view,rt_reflect12_.world_to_view,sizeof(evaluate.world_to_view));
  std::memcpy(evaluate.view_to_clip,rt_reflect12_.view_to_clip,sizeof(evaluate.view_to_clip));
 }
 transition_profile_pass(ray_reconstruction?"scene_dlss_rr":"scene_dlss");
 d3d12_->clear_slots();d3d12_->set_target(nullptr);
 const D3D12Frame::ExternalUse uses[9]={{up.resolved.get(),false,false},{world_d3d12_.get(),true,false},
                                        {up.motion.get(),false,false},{up.output.get(),false,true},
                                        {evaluate.bias_mask||!ray_reconstruction?up.bias.get():up.motion.get(),false,false},
                                        {up.guide_normal.get(),false,false},{up.guide_diffuse.get(),false,false},{up.guide_specular.get(),false,false},
                                        {hit_distance?rt_reflect12_.hit_distance.get():up.motion.get(),false,false}};
 const HRESULT hr=d3d12_->record_external(ray_reconstruction?"dlss_rr":"dlss",uses,hit_distance?9u:ray_reconstruction?8u:evaluate.bias_mask?5u:4u,
  [&](ID3D12GraphicsCommandList* list,ID3D12Resource* const* resources){
#ifdef EID3D11_EMBEDDED
  return embedded_d3d12_.dlss_evaluate(list,resources,evaluate);
#else
  return x64remote::dlss_evaluate_d3d12(list,resources,evaluate);
#endif
 });
 if(FAILED(hr)&&ray_reconstruction){
  // Ray Reconstruction failed: DLSS Super Resolution from the next frame
  // (decide_ray_reconstruction_d3d12 names it); this frame takes the filtered upscale.
  std::snprintf(rr_failure_,sizeof(rr_failure_),"its evaluation failed this session (0x%08lX; host.log has the NGX result)",static_cast<unsigned long>(hr));
  bridge_append_probe_event("dlss_rr_failed",rr_failure_);
  dlss_reset_=true;dlss_previous_valid_=false;return false;
 }
 if(FAILED(hr)){
  char reason[96];std::snprintf(reason,sizeof(reason),"DLSS evaluation failed (0x%08lX)",static_cast<unsigned long>(hr));
  set_dlss_unavailable_reason(reason);bridge_append_probe_event("dlss_failed",reason);
  OutputDebugStringA("EID3D11 DLSS evaluation failed; DLSS turned off\n");
  dlss_previous_valid_=false;return false;
 }
 profiler_.add_draw(1,1);profiler_.add_batch();
 // The player's Sharpening after DLSS: RCAS over the output-size image in place.
 if(const float sharpness=std::clamp(configured_floating_point(debug_overlay_.config(),"upscale_sharpness",.2f),0.f,1.f);sharpness>0.f){
  transition_profile_pass("scene_dlss_rcas");
  const HRESULT sharpened=up.sharpen.apply(*d3d12_,*up.output,sharpness);
  if(FAILED(sharpened)){
   static bool reported=false;
   if(!reported){reported=true;bridge_append_probe_event("dlss_sharpen_omitted","RCAS sharpening after DLSS could not be created; the DLSS image is shown unsharpened");}
  }else if(sharpened!=S_OK)d3d12_->omit("RCAS after DLSS (pipeline not ready: unsharpened)",false);
  profiler_.add_draw(1,1);profiler_.add_batch();
 }else up.sharpen.reset();
 finish_upscaler_frame(frame);
 return true;
}

// FSR's reactive mask on D3D12: the world colour as the transparent effects
// start (game particles and selection, the ambient particles, wind trails and
// precipitation), once a world frame; render_fsr_d3d12 has the SDK compare it
// with the finished world colour (FSR) or reactivePS writes DLSS's bias mask.
void Renderer::capture_opaque_world_d3d12() noexcept{
 auto& up=upscale12_;
 if(!temporal_upscaler()||!world_d3d12_bound_||!world_d3d12_||up.opaque_frame==native_frame_index_)return;
 UINT width=0,height=0,levels=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
 if(!d3d12_->texture_info(*world_d3d12_,width,height,levels,format))return;
 // FSR: the mask as an unordered-access image the SDK writes; DLSS: a target reactivePS draws.
 D3D12TextureRef& mask=fsr_mode_>=0?up.reactive:up.bias;(fsr_mode_>=0?up.bias:up.reactive).reset();
 if(!up.opaque||!d3d12_->texture_matches(*up.opaque,width,height,format)||!mask||!d3d12_->texture_matches(*mask,width,height,DXGI_FORMAT_R8_UNORM)){
  up.opaque.reset();mask.reset();
  if(FAILED(d3d12_->create_target(width,height,up.opaque,format,DXGI_FORMAT_UNKNOWN))||
     FAILED(fsr_mode_>=0?d3d12_->create_storage_texture(width,height,0,DXGI_FORMAT_R8_UNORM,mask):
                         d3d12_->create_target(width,height,mask,DXGI_FORMAT_R8_UNORM,DXGI_FORMAT_UNKNOWN))){
   up.opaque.reset();mask.reset();d3d12_->omit("upscaler reactive mask (images could not be created)",false);return;
  }
 }
 if(FAILED(d3d12_->copy_texture(*up.opaque,0,*world_d3d12_,0))){d3d12_->omit("upscaler reactive mask (opaque copy failed)",false);return;}
 up.opaque_frame=native_frame_index_;
}
// render_fsr on D3D12: FidelityFX FSR 3.1 in the render host, recorded into
// the frame's list like DLSS (D3D12Frame::fsr_upscale). False: FSR failed
// and is turned off for the session with the reason on the Upscaling list.
bool Renderer::render_fsr_d3d12(){
 auto& up=upscale12_;
 const auto fail=[&](const char* reason){
  set_fsr_unavailable_reason(reason);bridge_append_probe_event("fsr_failed",reason);
  OutputDebugStringA("EID3D11 FSR 3.1 failed; FSR turned off\n");
  d3d12_->fsr_upscale_release();dlss_previous_valid_=false;return false;
 };
 const UpscalerFrame frame=begin_upscaler_frame();
 D3D12Frame::FsrUpscale input{};
 float tan_half_fov=.5f;input.near_plane=1.f;input.far_plane=1000.f;
 if(frame.camera&&!upscaler_camera_from_clip(frame.world_to_clip.data(),input.near_plane,input.far_plane,tan_half_fov))
  return fail("the camera is not a perspective projection FSR understands");
 input.fov_y=2.f*std::atan(tan_half_fov);
 const auto now=std::chrono::steady_clock::now();
 input.frame_ms=fsr_last_frame_.time_since_epoch().count()?std::chrono::duration<float,std::milli>(now-fsr_last_frame_).count():16.7f;
 fsr_last_frame_=now;
 input.render_w=width_;input.render_h=height_;input.out_w=output_width_;input.out_h=output_height_;
 input.jitter_x=dlss_jitter_[0];input.jitter_y=dlss_jitter_[1];
 input.sharpness=std::clamp(configured_floating_point(debug_overlay_.config(),"upscale_sharpness",.2f),0.f,1.f);
 input.reset=frame.reset;
 transition_profile_pass("scene_fsr");
 d3d12_->clear_slots();d3d12_->set_target(nullptr);
 // The reactive mask when this frame's opaque world colour was taken
 // (capture_opaque_world_d3d12): it and the finished world colour go in.
 const bool reactive=up.opaque&&up.reactive&&up.opaque_frame==native_frame_index_;
 const D3D12Frame::ExternalUse uses[7]={{up.resolved.get(),false,false},{world_d3d12_.get(),true,false},
                                        {up.motion.get(),false,false},{up.output.get(),false,true},
                                        {up.reactive.get(),false,true},{up.opaque.get(),false,false},{world_d3d12_.get(),false,false}};
 const HRESULT hr=d3d12_->record_external("fsr",uses,reactive?7u:4u,[&](ID3D12GraphicsCommandList* list,ID3D12Resource* const* resources){
  ID3D12Resource* const all[8]={resources[0],resources[1],resources[2],resources[3],
                                reactive?resources[4]:nullptr,nullptr,reactive?resources[5]:nullptr,reactive?resources[6]:nullptr};
  return d3d12_->fsr_upscale(list,all,input);
 });
 if(reactive)d3d12_->count("FSR frames with a reactive mask");
 if(FAILED(hr))return fail("FSR 3.1 upscaling failed; see host.log");
 profiler_.add_draw(1,1);profiler_.add_batch();
 finish_upscaler_frame(frame);
 return true;
}
// ---- Scene post on D3D12 (composite_world's passes after the resolve):
// depth of field, screen-space sun rays and bloom with their own programs,
// targets and constants, each a sequence of full-screen triangles into its
// targets and finally over the output (scene_output_d3d12). The world colour
// and depth are read from the world target. S_FALSE: nothing drawn (an
// inactive lens, or a program still compiling: counted).

// after_upscaler: over the upscaled output-size image onto the back buffer,
// the render-size jittered depth read at the matching unjittered point (the
// vendors' order: depth of field after DLSS/FSR, so the upscaler never sees
// a blur edge that moves with the jitter). Else the world colour onto the
// resolved image, as composite_world.
HRESULT Renderer::depth_of_field_d3d12(int quality,float strength,const float clip_to_world[16],double seconds,bool after_upscaler){
 auto& dof=post12_.dof;
 quality=std::clamp(quality,0,2);
 UINT width=0,height=0,levels=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
 const D3D12Texture* const source=after_upscaler?upscale12_.output.get():world_d3d12_.get();
 D3D12Texture* const target=after_upscaler?nullptr:scene_output_d3d12(); // null: the back buffer
 if(!source||!d3d12_->texture_info(*source,width,height,levels,format))return E_FAIL;
 const float depth_map[4]={after_upscaler?dlss_jitter_[0]:0.f,after_upscaler?dlss_jitter_[1]:0.f,after_upscaler?float(width_)/float(width):1.f,0.f};
 const float work_height=float(height>1440?(height+2)/3:(height+1)/2);
 const DepthOfFieldLens lens=quality>0?depth_of_field_lens(clip_to_world,strength,work_height):DepthOfFieldLens{};
 if(!lens.active){dof.history_valid=dof.temporal_valid=false;return S_FALSE;}
 if(dof.programs.empty())DepthOfFieldPass::shader_programs(dof.programs); // quality 1, then 2: dofVS and five passes each, then the temporal two
 if(dof.programs.size()<17)return E_FAIL;
 const ShaderProgram* const own=dof.programs.data()+(quality>=2?6:0);
 HRESULT hr=S_OK;
 if(width!=dof.width||height!=dof.height){
  for(auto& target:dof.work)target.reset();
  for(auto& target:dof.focus)target.reset();
  for(auto& target:dof.layer)target.reset();
  for(auto& target:dof.alpha)target.reset();
  for(auto& target:dof.probe)target.reset();
  for(auto& target:dof.latch)target.reset();
  for(auto& target:dof.layer_motion)target.reset();
  dof.history_valid=dof.temporal_valid=false;dof.width=width;dof.height=height; // a failed size is not retried until it changes
  dof.divisor=height>1440?3u:2u;
  dof.work_width=(std::max)(1u,(width+dof.divisor-1)/dof.divisor);dof.work_height=(std::max)(1u,(height+dof.divisor-1)/dof.divisor);
  for(UINT n=0;n<2;++n)
   if(FAILED(hr=d3d12_->create_target(dof.work_width,dof.work_height,dof.work[n],DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_UNKNOWN))||
      FAILED(hr=d3d12_->create_target(1,1,dof.focus[n],DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_UNKNOWN))){
    dof.work[0].reset();return hr;
   }
 }
 if(!dof.work[0]||!dof.work[1]||!dof.focus[0]||!dof.focus[1])return E_FAIL;
 // After a temporal upscaler the depth is jittered per frame: the blur
 // layer and its full-size weight are averaged along the upscaler's motion
 // (depth_of_field.h, stabilizePS and alphaPS). A camera cut, a new size,
 // lens or option starts them over.
 const bool temporal=after_upscaler&&upscale12_.motion;
 if(temporal&&!dof.layer[0]){
  for(UINT n=0;n<2;++n)
   if(FAILED(hr=d3d12_->create_target(dof.work_width,dof.work_height,dof.layer[n],DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_UNKNOWN))||
      FAILED(hr=d3d12_->create_target(width,height,dof.alpha[n],DXGI_FORMAT_R16_FLOAT,DXGI_FORMAT_UNKNOWN))){
    dof.layer[0].reset();dof.width=0;return hr; // retried at the next size
   }
  for(UINT n=0;n<2;++n)
   if(FAILED(hr=d3d12_->create_target(dof.work_width,dof.work_height,dof.layer_motion[n],DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_UNKNOWN))){
    dof.layer[0].reset();dof.width=0;return hr;
   }
 }
 if(!temporal||fg_upscaler_reset_||quality!=dof.temporal_quality||strength!=dof.temporal_strength)dof.temporal_valid=false;
 dof.temporal_quality=quality;dof.temporal_strength=strength;
 // This frame's share: a jitter cycle is 8-18 frames; 0.15 settles in ~6.
 const float share=temporal?(dof.temporal_valid?.15f:1.f):0.f;
 const double elapsed=seconds-dof.last_seconds;
 const float blend=dof.history_valid&&elapsed>=0?float(1.0-std::exp(-std::min(elapsed,10.0)/DepthOfFieldPass::kFocusSeconds)):1.f;
 // Debug views (depth_of_field.h dofDebugView): 2-8 read the temporal path's
 // state; 3 also keeps this and last frame's prefiltered colour.
 const int debug_view=std::clamp(configured_integer(debug_overlay_.config(),"depth_of_field_debug_view",0),0,8);
 if(temporal&&(debug_view==3||debug_view==7)&&!dof.probe[0]&&dof.programs.size()>14)
  for(UINT n=0;n<2;++n)
   if(FAILED(d3d12_->create_target(dof.work_width,dof.work_height,dof.probe[n],DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_UNKNOWN))){
    dof.probe[0].reset();dof.probe[1].reset();break;
   }
 const bool probe=temporal&&(debug_view==3||debug_view==7)&&dof.probe[0]&&dof.probe[1];
 // The upscaler motion's camera (this frame's clip to world, last frame's
 // world to clip) and whether it moved: views 2-8 keep the last moving
 // frame's markers in the latch.
 float debug[36]{};
 if(temporal&&debug_view>=2){
  std::memcpy(debug,fg_clip_to_world_,64);std::memcpy(debug+16,fg_previous_clip_.data(),64);
  // Moved: the screen centre at clip depth 0.99 lands over half a render
  // pixel away in last frame's view (the harness's resting frames drift by
  // hundredths of a pixel as the camera settles on the ground).
  if(fg_upscaler_valid_&&!fg_upscaler_reset_){
   float world[4]{},previous[4]{};const float centre[4]={0,0,.99f,1};
   for(int r=0;r<4;++r)for(int c=0;c<4;++c)world[r]+=fg_clip_to_world_[r*4+c]*centre[c];
   if(std::fabs(world[3])>1e-12f){
    for(float& v:world)v/=world[3];
    for(int r=0;r<4;++r)for(int c=0;c<4;++c)previous[r]+=fg_previous_clip_[r*4+c]*world[c];
    if(previous[3]>1e-6f&&std::hypot(previous[0]/previous[3],previous[1]/previous[3])*.5f*float(width_)>.5f)debug[32]=1.f;
   }
  }
  if(!dof.latch[0])
   for(UINT n=0;n<2;++n)
    if(FAILED(d3d12_->create_target(width,height,dof.latch[n],DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_UNKNOWN))){dof.latch[0].reset();dof.latch[1].reset();break;}
 }
 const bool latch=temporal&&debug_view>=2&&dof.latch[0]&&dof.latch[1];
 const float values[20]={clip_to_world[12],clip_to_world[13],clip_to_world[14],clip_to_world[15],
                         lens.aperture,lens.margin,float(debug_view),blend,
                         float(width),float(height),1.f/width,1.f/height,
                         float(dof.work_width),float(dof.work_height),float(dof.divisor),share,
                         depth_map[0],depth_map[1],depth_map[2],depth_map[3]};
 const auto pass=[&](D3D12Texture* target,const ShaderProgram& pixel,const D3D12Texture* input,const D3D12Texture* focus,bool over,
                       const D3D12Texture* history=nullptr,const D3D12Texture* alpha=nullptr){
  d3d12_->clear_slots();
  d3d12_->set_constants(0,values,sizeof(values));
  d3d12_->set_sampler(0,post_linear_clamp());
  d3d12_->set_depth_resource(0,world_d3d12_.get());
  d3d12_->set_resource(1,input);d3d12_->set_resource(2,focus);
  if(temporal){d3d12_->set_resource(3,upscale12_.motion.get());d3d12_->set_resource(4,history);d3d12_->set_resource(5,alpha);}
  if(temporal){ // the layer's motion and depth, this frame's (once written) and last frame's
   if(target!=dof.layer_motion[dof.current_layer^1u].get())d3d12_->set_resource(9,dof.layer_motion[dof.current_layer^1u].get());
   d3d12_->set_resource(11,dof.layer_motion[dof.current_layer].get());
  }
  if(latch&&(over||&pixel==&dof.programs[15])){ // the debug views: last frame's layer, this frame's fill result, the colour probes, the latch
   d3d12_->set_resource(10,dof.latch[over?dof.current_latch:dof.current_latch^1u].get());
   d3d12_->set_resource(5,dof.alpha[dof.current_layer^1u].get()); // this frame's stable weight (view 8)
   d3d12_->set_constants(1,debug,sizeof(debug));
   d3d12_->set_resource(4,dof.layer[dof.current_layer].get());d3d12_->set_resource(6,dof.work[0].get());
   if(probe){d3d12_->set_resource(7,dof.probe[dof.current_probe].get());d3d12_->set_resource(8,dof.probe[dof.current_probe^1u].get());}
  }
  d3d12_->set_target(target);
  return d3d12_->draw(own[0],pixel,post_state(false,over),nullptr,3);
 };
 const UINT next=dof.current_focus^1u;
 if((hr=pass(dof.focus[next].get(),own[1],nullptr,dof.focus[dof.current_focus].get(),false))!=S_OK)return hr;
 dof.current_focus=next;dof.last_seconds=seconds;
 const D3D12Texture* const focus=dof.focus[dof.current_focus].get();
 if((hr=pass(dof.work[0].get(),own[2],source,focus,false))!=S_OK)return hr;
 if(probe){ // debug view 3: this frame's prefiltered colour, before the gather overwrites it
  dof.current_probe^=1u;
  if((hr=pass(dof.probe[dof.current_probe].get(),dof.programs[14],dof.work[0].get(),focus,false))!=S_OK)return hr;
 }
 if((hr=pass(dof.work[1].get(),own[3],dof.work[0].get(),focus,false))!=S_OK||
    (hr=pass(dof.work[0].get(),own[4],dof.work[1].get(),focus,false))!=S_OK)return hr;
 if(temporal){
  const UINT now=dof.current_layer^1u,last=dof.current_layer;
  const ShaderProgram* const stable=dof.programs.data()+12;
  // The layer's motion and depth first (motionPS: the 3x3 nearest depth's),
  // then the layer and weight along it, dropping disoccluded history.
  if((hr=pass(dof.layer_motion[now].get(),dof.programs[16],dof.work[0].get(),focus,false))!=S_OK||
     (hr=pass(dof.layer[now].get(),stable[0],dof.work[0].get(),focus,false,dof.layer[last].get()))!=S_OK||
     (hr=pass(dof.alpha[now].get(),stable[1],dof.layer[now].get(),focus,false,dof.alpha[last].get()))!=S_OK||
     (latch&&(dof.current_latch^=1u,hr=pass(dof.latch[dof.current_latch].get(),dof.programs[15],dof.layer[now].get(),focus,false))!=S_OK)||
     (hr=pass(target,own[5],dof.layer[now].get(),focus,true,nullptr,dof.alpha[now].get()))!=S_OK){dof.temporal_valid=false;return hr;}
  dof.current_layer=now;dof.temporal_valid=true;
 }else if((hr=pass(target,own[5],dof.work[0].get(),focus,true))!=S_OK)return hr;
 dof.history_valid=true;
 const UINT drawn=temporal?8u:5u;
 profiler_.add_draw(drawn,drawn);profiler_.add_batch(drawn);
 return S_OK;
}

// The configured depth of field on D3D12 (composite_world's conditions):
// before the upscaler-less output, or after DLSS/FSR (present_upscaled_d3d12).
void Renderer::depth_of_field_world_d3d12(bool after_upscaler){
 const auto& config=debug_overlay_.config();
 const int dof=configured_integer(config,"depth_of_field",0);
 if(dof<=0||fog_prepared_frame_!=native_frame_index_*2u)return;
 const auto clip=bridge_scene_world_to_clip_snapshot();float clip_to_world[16];
 if(!clip.valid||!ambient_occlusion_invert(clip.world_to_clip.data(),clip_to_world))return;
 transition_profile_pass("scene_dof");
 const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
 const HRESULT hr=depth_of_field_d3d12(dof,configured_floating_point(config,"depth_of_field_strength",.2f),clip_to_world,seconds,after_upscaler);
 d3d12_->clear_slots();
 if(hr==S_OK)d3d12_->count(after_upscaler?"scene_dof (after the upscaler)":"scene_dof");
 else if(FAILED(hr))d3d12_->omit("scene_dof",false);
}
HRESULT Renderer::sun_rays_d3d12(){
 auto& rays=post12_.rays;
 UINT width=0,height=0,levels=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
 if(!d3d12_->texture_info(*world_d3d12_,width,height,levels,format))return E_FAIL;
 if(rays.programs.empty())SunRaysPass::shader_programs(rays.programs); // sunRaysVS, mask, blur, composite
 if(rays.programs.size()<4)return E_FAIL;
 HRESULT hr=S_OK;
 if(width!=rays.width||height!=rays.height){
  rays.mask.reset();rays.blur.reset();rays.width=width;rays.height=height; // a failed size is not retried until it changes
  const UINT w=(std::max)(1u,width/4),h=(std::max)(1u,height/4);
  if(FAILED(hr=d3d12_->create_target(w,h,rays.mask,DXGI_FORMAT_R16_FLOAT,DXGI_FORMAT_UNKNOWN))||
     FAILED(hr=d3d12_->create_target(w,h,rays.blur,DXGI_FORMAT_R16_FLOAT,DXGI_FORMAT_UNKNOWN))){rays.blur.reset();return hr;}
 }
 if(!rays.mask||!rays.blur)return E_FAIL;
 // As SunRaysPass::render: streak length, samples, tint, sky share, knee, decay, haze luminance.
 const float values[12]={sun_rays_center_[0],sun_rays_center_[1],.45f,SunRaysPass::kSamples,sun_rays_tint_[0],sun_rays_tint_[1],sun_rays_tint_[2],
                         .35f,.35f,.85f,.965f,sun_rays_haze_luma_};
 const auto pass=[&](D3D12Texture* target,const ShaderProgram& pixel,const D3D12Texture* input,bool depth,bool additive){
  d3d12_->clear_slots();
  d3d12_->set_constants(0,values,sizeof(values));
  d3d12_->set_sampler(0,post_linear_clamp());
  d3d12_->set_resource(0,input);
  d3d12_->set_depth_resource(1,depth?world_d3d12_.get():nullptr);
  d3d12_->set_target(target);
  return d3d12_->draw(rays.programs[0],pixel,post_state(additive,false),nullptr,3);
 };
 if((hr=pass(rays.mask.get(),rays.programs[1],world_d3d12_.get(),true,false))!=S_OK||
    (hr=pass(rays.blur.get(),rays.programs[2],rays.mask.get(),false,false))!=S_OK||
    (hr=pass(scene_output_d3d12(),rays.programs[3],rays.blur.get(),false,true))!=S_OK)return hr;
 profiler_.add_draw(3,3);profiler_.add_batch(3);
 return S_OK;
}

HRESULT Renderer::bloom_d3d12(int quality,float strength,float ceiling,float threshold,bool contribution_only){
 auto& bloom=post12_.bloom;
 if(quality<=0||strength<=0.f)return S_OK;
 UINT width=0,height=0,levels=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
 if(!d3d12_->texture_info(*world_d3d12_,width,height,levels,format))return E_FAIL;
 if(bloom.programs.empty())BloomPass::shader_programs(bloom.programs); // bloomVS, prefilter13, prefilter4, down, up, composite
 if(bloom.programs.size()<6)return E_FAIL;
 HRESULT hr=S_OK;
 if(width!=bloom.width||height!=bloom.height||quality!=bloom.quality){
  for(auto& mip:bloom.mips)mip.reset();
  bloom.width=width;bloom.height=height;bloom.quality=quality;bloom.count=0; // a failed size/quality is not retried until one changes
  const UINT wanted=quality>=2?6u:4u;
  UINT count=0;
  while(count<wanted&&(std::min(width,height)>>(count+1))>=8)++count;
  count=(std::max)(count,1u);
  for(UINT n=0;n<count;++n)
   if(FAILED(hr=d3d12_->create_target((std::max)(1u,width>>(n+1)),(std::max)(1u,height>>(n+1)),bloom.mips[n],
      DXGI_FORMAT_R11G11B10_FLOAT,DXGI_FORMAT_UNKNOWN)))return hr;
  bloom.count=count;
 }
 if(!bloom.count)return E_FAIL;
 const auto curve=bloom_knee_curve(ceiling,threshold);
 float weights=0,weight=1;
 for(UINT n=0;n<bloom.count;++n,weight*=BloomPass::kWide)weights+=weight;
 const float values[8]={curve[0],curve[1],curve[2],curve[3],strength*BloomPass::kGain/weights,BloomPass::kWide,BloomPass::kGlow,0};
 const auto pass=[&](D3D12Texture* target,const ShaderProgram& pixel,const D3D12Texture* input,bool first,bool additive){
  d3d12_->clear_slots();
  d3d12_->set_constants(0,values,sizeof(values));
  d3d12_->set_sampler(0,post_linear_clamp());
  d3d12_->set_resource(0,input);
  // The prefilter reads the world depth (t1) and this frame's particle glow (t2).
  d3d12_->set_depth_resource(1,first?world_d3d12_.get():nullptr);
  d3d12_->set_resource(2,first&&bloom.glow_frame==native_frame_index_?bloom.glow.get():nullptr);
  d3d12_->set_target(target);
  return d3d12_->draw(bloom.programs[0],pixel,post_state(additive,false),nullptr,3);
 };
 UINT issued=0;
 if((hr=pass(bloom.mips[0].get(),bloom.programs[quality>=2?1:2],world_d3d12_.get(),true,false))!=S_OK)return hr;
 ++issued;
 for(UINT n=1;n<bloom.count;++n,++issued)if((hr=pass(bloom.mips[n].get(),bloom.programs[3],bloom.mips[n-1].get(),false,false))!=S_OK)return hr;
 for(UINT n=bloom.count-1;n>0;--n,++issued)if((hr=pass(bloom.mips[n-1].get(),bloom.programs[4],bloom.mips[n].get(),false,true))!=S_OK)return hr;
 if((hr=pass(scene_output_d3d12(),bloom.programs[5],bloom.mips[0].get(),false,!contribution_only))!=S_OK)return hr;
 ++issued;
 profiler_.add_draw(issued,issued);profiler_.add_batch(issued);
 return S_OK;
}

// apply_gamma_ramp on the D3D12 backend: the back buffer copied (capture),
// then gammaPS maps each channel through the ramp's LUT (256x1; float texels,
// the executor's upload formats) back into it. An identity ramp costs nothing.
HRESULT Renderer::apply_gamma_ramp_d3d12(D3D12Texture* source,D3D12Texture* target) noexcept{
 std::array<WORD,768> ramp{};bool changed=false;
 {std::lock_guard<std::mutex> lock(gamma_lock_);
  if(gamma_identity_)return S_FALSE;
  ramp=gamma_ramp_;changed=gamma_changed_;gamma_changed_=false;}
 if(gamma_failed_)return S_FALSE;
 const auto fail=[&](HRESULT result){gamma_failed_=true;return result;};
 HRESULT hr=S_OK;
 try{if(gamma_programs_d3d12_.empty())append_gamma_shader_programs(gamma_programs_d3d12_);}catch(const std::bad_alloc&){return fail(E_OUTOFMEMORY);}
 if(gamma_programs_d3d12_.size()<2)return fail(E_FAIL);
 if(!gamma_lut_d3d12_){
  if(FAILED(hr=d3d12_->create_texture(256,1,DXGI_FORMAT_R32G32B32A32_FLOAT,gamma_lut_d3d12_)))return fail(hr);
  changed=true;
 }
 if(changed){
  std::array<float,1024> texels{};
  const auto level=[](WORD value){return float(std::min(1.0,value/65280.0));};
  for(unsigned i=0;i<256;++i){texels[i*4]=level(ramp[i]);texels[i*4+1]=level(ramp[256+i]);texels[i*4+2]=level(ramp[512+i]);texels[i*4+3]=1.f;}
  if(FAILED(hr=d3d12_->write_texture(*gamma_lut_d3d12_,RECT{0,0,256,1},texels.data(),256*16)))return fail(hr);
 }
 const UINT width=output_width_,height=output_height_;
 if(!source){
  if(!gamma_frame_d3d12_||!d3d12_->texture_matches(*gamma_frame_d3d12_,width,height,DXGI_FORMAT_B8G8R8A8_UNORM)){
   gamma_frame_d3d12_.reset();
   if(FAILED(hr=d3d12_->create_texture(width,height,DXGI_FORMAT_B8G8R8A8_UNORM,gamma_frame_d3d12_)))return fail(hr);
  }
  transition_profile_pass("gamma_ramp");
  hr=d3d12_->capture(*gamma_frame_d3d12_,0,0,RECT{0,0,LONG(width),LONG(height)});
  if(hr!=S_OK)return SUCCEEDED(hr)?S_FALSE:hr; // minimized: nothing drawn
 }else transition_profile_pass("fg_fsr_hudless_gamma");
 d3d12_->clear_slots();
 d3d12_->set_target(target);
 d3d12_->set_resource(0,source?source:gamma_frame_d3d12_.get());d3d12_->set_resource(1,gamma_lut_d3d12_.get());
 D3D11_SAMPLER_DESC linear{};linear.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
 linear.AddressU=linear.AddressV=linear.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;linear.MaxLOD=D3D11_FLOAT32_MAX;
 d3d12_->set_sampler(0,linear);
 hr=d3d12_->draw(gamma_programs_d3d12_[0],gamma_programs_d3d12_[1],post_state(false,false),nullptr,3);
 d3d12_->clear_slots();
 if(FAILED(hr))return hr;
 if(d3d12_not_done(hr)){if(hr==S_FALSE)d3d12_->omit("gamma_ramp (pipeline not ready)",false);return S_OK;}
 d3d12_->count("gamma ramp passes");
 profiler_.add_draw(1,1);profiler_.add_batch();
 return S_OK;
}

#endif // !EID3D11_SHADER_EMBED
} // namespace owned
