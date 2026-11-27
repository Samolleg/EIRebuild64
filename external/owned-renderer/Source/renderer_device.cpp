// Renderer device: swap chain, device creation and settings (ensure), targets, shader preparation and specialisation, pipelines, the native geometry arena, and teardown (release).
#include <intrin.h>
#include "render_resources.h"
#include "../../../OwnedRenderer/Module/GraphicsApi.h"
#include "point_shadow_policy.h"
#include "cursor_image.h"
#include "shadow_softness.h"
#include "shadow_foliage.h"
#include "contact_blend.h"
#include "native_instance_flags.h"
#include "terrain_cliff.h"
#include "ground_cover_surface.h"
#include "shadow_far_retention.h"
#include "liquid_surface_shader.h"
#include "world_fog.h"
#include "ambient_occlusion.h"
#include "game_particles.h"
#include "terrain_print_shader.h"
#include "sky_clouds.h"
#include "cave_lighting.h"
#include "terrain_material.h"
#include "native_shader.h"
#include "owned_terrain_decode.h"
#include "renderer.h"
#include "graphics_settings.h"
#include "gpu_adapter.h"
#include "ui_language.h"
#include "game_focus.h"
#include "cursor_confinement.h"
#include "combat_ui.h"
#include "world_texture_sampler.h"
#include "texture_mips.h"
#include "character_pane_owned_text.h"
#include "owned_resource_runtime.h"
#include "neighbour_maps.h"
#include "native_prebuilt.h"
#include "neighbour_client.h"
#include "neighbour_piece.h"
#include "static_lod.h"
#include "owned_resource_store.h"
#include "res_mapping.h"
#include "res_cache_release.h"
#include "engine_bridge.h"
#include "camp_inventory.h"
#include "retained_static_scene.h"
#include "widescreen.h"
#include "test_harness.h"
#include "x64_remote_link.h"
#include "dlss_frame.h"
#include "stall_watchdog.h"
#include <optional>
#include <sstream>
#include <d3dcompiler.h>
#include <wincodec.h>
#include <objbase.h>
#include <bcrypt.h>
#include <psapi.h>
#include <malloc.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iterator>
#include <thread>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <unordered_set>
#include "renderer_internal.h"
#include "scene_commands_ui.h"

// Created-in-play detector (frame_accounting.h): a shader object created while
// the world is shown is a hitch the player sees. One event each, naming the
// kind, the creation time, the DXBC checksum and our calling site (an offset
// into this module, resolved with its .map). Silent when nothing is created.
void frame_acct::report_shader_created_in_play(const char* kind,double ms,SIZE_T bytes,const void* code,const void* caller) noexcept{
#ifndef EID3D11_SHADER_EMBED
 HMODULE self=nullptr;
 GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
  reinterpret_cast<LPCWSTR>(&frame_acct::report_shader_created_in_play),&self);
 HMODULE from=nullptr;
 GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
  reinterpret_cast<LPCWSTR>(caller),&from);
 const unsigned long long offset=from?static_cast<unsigned long long>(static_cast<const char*>(caller)-reinterpret_cast<const char*>(from)):0ull;
 // DXBC: "DXBC", then a 16-byte checksum.
 std::uint32_t checksum[2]{};
 if(code&&bytes>=20&&std::memcmp(code,"DXBC",4)==0)std::memcpy(checksum,static_cast<const char*>(code)+4,sizeof(checksum));
 char text[192];
 std::snprintf(text,sizeof(text),"kind=%s ms=%.2f bytes=%zu dxbc=%08x%08x caller=%s+0x%llx thread=%lu",
  kind,ms,static_cast<std::size_t>(bytes),checksum[0],checksum[1],from==self?"renderer":from?"other":"?",offset,GetCurrentThreadId());
 owned::bridge_report_event("shader_created_in_play",text);
#endif
}

namespace owned {
void address_space_mb(std::uint64_t& used,std::uint64_t& largest_free,bool walk) noexcept; // renderer_diagnostics.cpp

void report_slow_scene_command(const char* name,const char* site,double ms) noexcept{
#ifndef EID3D11_SHADER_EMBED
 char text[384];
 std::snprintf(text,sizeof(text),"ms=%.2f frame=%llu site=%.60s command=%.260s",ms,
  static_cast<unsigned long long>(frame_acct::g_presentation_frame.load(std::memory_order_relaxed)),site?site:"?",name?name:"?");
 bridge_report_event("slow_scene_command",text);
#endif
}

namespace {

struct NativeShadowHistoryConstants {
    std::array<float,16> previous_world_to_clip{};
    std::array<float,4> settings{};
    std::array<float,4> jitter{}; // x,y: per-frame soft-shadow disk offset while history integrates
};
static_assert(sizeof(NativeShadowHistoryConstants)==96);
constexpr char kShader[] = R"(
cbuffer C:register(b0){row_major float4x4 world;row_major float4x4 view;row_major float4x4 projection;float4 viewport;float4 fogColor;float4 textureFactor;uint transformed;uint textured;uint colorOp;uint alphaOp;uint colorArg1;uint colorArg2;uint alphaArg1;uint alphaArg2;uint alphaTest;uint alphaFunc;float alphaRef;uint fogEnable;uint specularEnable;uint nativeTriangleBase;uint2 rasterPadding;float4 contactUnused;uint4 contactParamsUnused;uint2 combinerUnused;float hudOpacity;uint hudUnused;};
Texture2D tex0:register(t0);SamplerState samp0:register(s0);
struct VI{float4 p:POSITION;float4 d:COLOR0;float4 s:COLOR1;float2 uv:TEXCOORD0;};
struct VO{float4 p:SV_Position;float4 d:COLOR0;float4 s:COLOR1;float2 uv:TEXCOORD0;};
VO vsMain(VI i){VO o;if(transformed!=0){float r=max(abs(i.p.w),1e-8),w=1/r;float2 n=float2(((i.p.x-viewport.x)/viewport.z)*2-1,1-((i.p.y-viewport.y)/viewport.w)*2);o.p=float4(n*w,i.p.z*w,w);}else{float4 p=mul(i.p,world);p=mul(p,view);o.p=mul(p,projection);}o.d=i.d.bgra;o.s=i.s.bgra;o.uv=i.uv;return o;}
bool cmp(float a){if(alphaFunc==1)return false;if(alphaFunc==2)return a<alphaRef;if(alphaFunc==3)return a==alphaRef;if(alphaFunc==4)return a<=alphaRef;if(alphaFunc==5)return a>alphaRef;if(alphaFunc==6)return a!=alphaRef;if(alphaFunc==7)return a>=alphaRef;return true;}
float4 stageArg(int selector,float4 diffuse,float4 specular,float4 textureSample){int source=selector;bool alphaReplicate=source>=32;if(alphaReplicate)source-=32;bool complement=source>=16;if(complement)source-=16;float4 value=source==0||source==1?diffuse:(source==2?textureSample:(source==3?textureFactor:(source==4?specular:0)));if(alphaReplicate)value=value.aaaa;if(complement)value=1-value;return value;}
float3 colorResult(uint op,float4 a,float4 b,float3 current){if(op==2)return a.rgb;if(op==3)return b.rgb;if(op==4)return a.rgb*b.rgb;if(op==5)return saturate(2*a.rgb*b.rgb);if(op==7)return saturate(a.rgb+b.rgb);return current;}
float alphaResult(uint op,float4 a,float4 b,float current){if(op==2)return a.a;if(op==3)return b.a;if(op==4)return a.a*b.a;if(op==5)return saturate(2*a.a*b.a);if(op==7)return saturate(a.a+b.a);return current;}
float4 psMain(VO i):SV_Target{float4 t=textured!=0?tex0.Sample(samp0,i.uv):float4(1,1,1,1),c=i.d;float4 ca=stageArg(colorArg1,i.d,i.s,t),cb=stageArg(colorArg2,i.d,i.s,t),aa=stageArg(alphaArg1,i.d,i.s,t),ab=stageArg(alphaArg2,i.d,i.s,t);c.rgb=colorResult(colorOp,ca,cb,c.rgb);c.a=alphaResult(alphaOp,aa,ab,c.a);if(specularEnable!=0)c.rgb=saturate(c.rgb+i.s.rgb);if(alphaTest!=0&&!cmp(c.a))discard;if(fogEnable!=0)c.rgb=lerp(fogColor.rgb,c.rgb,i.s.a);c.a*=hudOpacity;return c;})";

constexpr char kClearShader[] = R"(
cbuffer ClearValues:register(b7){float4 clearColor;float clearDepth;float3 clearPadding;};
float4 clearVS(uint vertexId:SV_VertexID):SV_Position{
 float2 position=vertexId==0?float2(-1,-1):(vertexId==1?float2(-1,3):float2(3,-1));
 return float4(position,clearDepth,1);
}
struct ClearOutput{float4 color:SV_Target;float depth:SV_Depth;};
ClearOutput clearPS(){ClearOutput output;output.color=clearColor;output.depth=clearDepth;return output;})";

// Close sun-map scroll (L-077): copies the cached static layer, shifted by
// whole texels, back into the cleared cache. Pixels with no source keep 1.
constexpr char kShadowScrollShader[] = R"(
Texture2DArray<float> shadowScrollSource:register(t0);
cbuffer ShadowScroll:register(b7){int4 shadowScroll;};
float4 shadowScrollVS(uint vertexId:SV_VertexID):SV_Position{
 float2 position=vertexId==0?float2(-1,-1):(vertexId==1?float2(-1,3):float2(3,-1));
 return float4(position,0,1);
}
float shadowScrollPS(float4 position:SV_Position):SV_Depth{
 int2 source=int2(position.xy)-shadowScroll.xy;
 if(any(source<0)||any(source>=shadowScroll.w))discard;
 return shadowScrollSource.Load(int4(source,shadowScroll.z,0));
})";

// Owned DrawFrameSelection overlay: clip-space positions and straight colour.
// The soft fill behind a pillarboxed full-screen picture: a wide blur of the
// same image, darkened by the vertex colour.
constexpr char kBackdropShader[] = R"(
Texture2D tex0:register(t0);SamplerState samp0:register(s0);
struct BackdropIn{float4 p:SV_Position;float4 d:COLOR0;float4 s:COLOR1;float2 uv:TEXCOORD0;};
float4 backdropPS(BackdropIn i):SV_Target{
 float3 c=0;
 [unroll]for(int y=-2;y<=2;++y)[unroll]for(int x=-2;x<=2;++x)c+=tex0.Sample(samp0,saturate(i.uv+float2(x,y)*float2(.012,.021))).rgb;
 return float4(c*(i.d.rgb/25),1);
}
)";
// Interface quads composed below 100% UI scale: a colour-key-aware bilinear
// filter (each texel's colour weighted by its alpha, so transparent texels
// never bleed their key colour into an edge), modulated like the UI blit.
constexpr char kInterfaceScaledShader[] = R"(
cbuffer C:register(b0){row_major float4x4 world;row_major float4x4 view;row_major float4x4 projection;float4 viewport;float4 fogColor;float4 textureFactor;uint transformed;uint textured;uint colorOp;uint alphaOp;};
Texture2D tex0:register(t0);
struct InterfaceIn{float4 p:SV_Position;float4 d:COLOR0;float4 s:COLOR1;float2 uv:TEXCOORD0;};
float4 interfaceScaledPS(InterfaceIn i):SV_Target{
 uint w,h;tex0.GetDimensions(w,h);
 float2 q=i.uv*float2(w,h)-.5;int2 b=int2(floor(q));float2 f=q-b;int2 m=int2(w,h)-1;
 float4 t00=tex0.Load(int3(clamp(b,0,m),0)),t10=tex0.Load(int3(clamp(b+int2(1,0),0,m),0));
 float4 t01=tex0.Load(int3(clamp(b+int2(0,1),0,m),0)),t11=tex0.Load(int3(clamp(b+int2(1,1),0,m),0));
 float4 wt=float4((1-f.x)*(1-f.y),f.x*(1-f.y),(1-f.x)*f.y,f.x*f.y);
 float a=dot(wt,float4(t00.a,t10.a,t01.a,t11.a));
 float3 c=wt.x*t00.a*t00.rgb+wt.y*t10.a*t10.rgb+wt.z*t01.a*t01.rgb+wt.w*t11.a*t11.rgb;
 c=a>0?c/a:0;
 return float4(c*i.d.rgb,(alphaOp==3?i.d.a:a*i.d.a)*textureFactor.a);
}
)";
// Owned glyphs whose output size differs from their raster size (the
// composition changed after they were rasterized, or a non-uniform scale):
// an exact box filter over the pixel's footprint in the atlas, so every
// stroke keeps its weight instead of the 1-or-2 pixel strokes of two-tap
// bilinear or nearest. Covers up to 4 texels per axis (scale >= 0.5).
constexpr char kInterfaceTextShader[] = R"(
cbuffer C:register(b0){row_major float4x4 world;row_major float4x4 view;row_major float4x4 projection;float4 viewport;float4 fogColor;float4 textureFactor;uint transformed;uint textured;uint colorOp;uint alphaOp;};
Texture2D tex0:register(t0);
struct InterfaceIn{float4 p:SV_Position;float4 d:COLOR0;float4 s:COLOR1;float2 uv:TEXCOORD0;};
float4 interfaceTextPS(InterfaceIn i):SV_Target{
 uint w,h;tex0.GetDimensions(w,h);
 float2 t=i.uv*float2(w,h);float2 fw=clamp(fwidth(t),1e-4,2);
 float2 a=t-.5*fw,b=t+.5*fw;int2 lo=int2(floor(a));int2 m=int2(w,h)-1;
 float total=0,coverage=0;float3 c=0;
 [unroll]for(int y=0;y<4;++y){
  float wy=max(0,min(b.y,lo.y+y+1)-max(a.y,lo.y+y));
  [unroll]for(int x=0;x<4;++x){
   float wx=max(0,min(b.x,lo.x+x+1)-max(a.x,lo.x+x));
   float4 s=tex0.Load(int3(clamp(lo+int2(x,y),0,m),0));
   float k=wx*wy;total+=k;coverage+=k*s.a;c+=k*s.a*s.rgb;
  }
 }
 float alpha=total>0?coverage/total:0;
 c=coverage>0?c/coverage:0;
 return float4(c*i.d.rgb,(alphaOp==3?i.d.a:alpha*i.d.a)*textureFactor.a);
}
)";
constexpr char kScreenRectangleShader[] = R"(
struct RectangleVertex{float2 position:POSITION;float4 color:COLOR;};
struct RectanglePixel{float4 position:SV_Position;float4 color:COLOR;};
RectanglePixel rectangleVS(RectangleVertex input){RectanglePixel output;output.position=float4(input.position,0,1);output.color=input.color.bgra;return output;}
float4 rectanglePS(RectanglePixel input):SV_Target{return input.color;})";
#if !defined(EID3D11_SHADER_EMBED)
struct ScreenRectangleVertex { float x, y; std::uint32_t color; };
constexpr UINT kScreenRectangleMaximumVertices = 5u * 6u;
#endif

constexpr char kDebugResourceViewShader[] = R"(
cbuffer DebugViewConstants:register(b0){float4 projectionCoefficients;uint debugMode;uint shadowEnabled;uint shadowValid;uint debugPadding;float4 shadowBoard;float4 receiverRectangle[5];};
Texture2D<float> debugFloat:register(t0);
#ifdef EID3D12
Texture2D<uint2> debugUint:register(t1);
#else
Texture2D<uint> debugUint:register(t1);
#endif
Texture2DArray<float> debugShadow:register(t2);
SamplerState pointClamp:register(s0);
struct FullscreenOutput{float4 position:SV_Position;float2 uv:TEXCOORD0;};
FullscreenOutput debugTextureVS(uint vertexId:SV_VertexID){FullscreenOutput o;float2 uv=float2((vertexId<<1)&2,vertexId&2);o.uv=uv;o.position=float4(uv.x*2-1,1-uv.y*2,0,1);return o;}
float4 debugTexturePS(FullscreenOutput input):SV_Target{
 if(debugMode==5u){
  float2 board=(input.position.xy-shadowBoard.xy)/shadowBoard.z;
  if(any(board<0)||board.x>=3||board.y>=2)return float4(.015,.015,.015,1);
  uint2 cell=(uint2)board;uint slice=cell.x+3*cell.y;float2 uv=frac(board);
  if(slice>=5)return float4(.015,.015,.015,1);
  const float3 colors[5]={float3(1,.25,.2),float3(.2,1,.25),float3(.2,.5,1),float3(1,.85,.2),float3(.8,.35,1)};
  float edge=1.5/shadowBoard.z;
  if(any(uv<edge)||any(uv>1-edge))return float4(colors[slice],1);
  // Disabled/invalid slices may still contain old depth. Never sample it.
  if((shadowEnabled&shadowValid&(1u<<slice))==0){
   float stripe=fmod(floor(input.position.x/6)+floor(input.position.y/6),2);
   return float4(lerp(float3(.035,.035,.035),float3(.12,.055,.12),stripe),1);
  }
  float4 rectangle=receiverRectangle[slice];
  bool inside=all(uv>=rectangle.xy-edge)&&all(uv<=rectangle.zw+edge);
  bool outline=any(abs(uv-rectangle.xy)<=edge)||any(abs(uv-rectangle.zw)<=edge);
  if(inside&&outline)return float4(colors[slice],1);
  float depth=debugShadow.SampleLevel(pointClamp,float3(uv,slice),0);
  if(depth>=.999999)return float4(.025,.055,.12,1);
  return float4(depth,depth,depth,1);
 }
 float value=0;
 if(debugMode==3u){uint w,h;debugUint.GetDimensions(w,h);
#ifdef EID3D12
 value=debugUint.Load(int3(int2(input.uv*float2(w,h)),0)).g/255.0;
#else
 value=debugUint.Load(int3(int2(input.uv*float2(w,h)),0))/255.0;
#endif
 }
 else value=debugFloat.SampleLevel(pointClamp,input.uv,0);
 if(debugMode==2u){
  float a=projectionCoefficients.x,b=projectionCoefficients.y;
  float c=projectionCoefficients.z,d=projectionCoefficients.w;
  float viewZ=(b-value*d)/(value*c-a);
  float nearZ=b/-a;float farZ=(b-d)/(c-a);
  value=saturate((viewZ-nearZ)/(farZ-nearZ));
 }
 return float4(value,value,value,1);})";

#if !defined(EID3D11_SHADER_EMBED)
struct DebugViewConstants {
    std::array<float, 4> projection{};
    std::uint32_t mode = 0;
    std::uint32_t shadow_enabled = 0, shadow_valid = 0, padding = 0;
    std::array<float, 4> shadow_board{};
    std::array<std::array<float, 4>, 5> receiver_rectangle{};
};

UINT present_interval(){static const UINT interval=[](){char value[8]{};return GetEnvironmentVariableA("EI_RENDERER_VSYNC",value,sizeof(value))&&value[0]=='1'?1u:0u;}();return interval;}
// General geometry page size: in-place growth stops here and further meshes
// open another page (allocate_native_geometry_span). A bound on one frame's
// new allocation and copy, not a capacity limit: the page count is unlimited.
// Same size as a neighbour prebuilt page; draws batch per page, so smaller
// pages would split batches. Ground-cover growths of 57-85 MB without a copy did not
// stall on the zone 3 route; 135 and 256 MiB did.
constexpr UINT kNativeGeneralPageBytes=64u<<20;
}  // namespace

// Flip-model presentation: the compositor takes our buffers directly (no blit
// copy), and with tearing allowed an uncapped Present is not held to vblank.
// Older systems without flip support keep the legacy blit model.
namespace {

// " budget=<MiB> usage=<MiB>" of the adapter's local memory.
std::string local_memory_text(IDXGIAdapter3* adapter) noexcept try{
 DXGI_QUERY_VIDEO_MEMORY_INFO memory{};
 if(!adapter||FAILED(adapter->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&memory)))return " budget=unknown";
 char text[64]{};
 std::snprintf(text,sizeof(text)," budget=%lluMiB usage=%lluMiB",
  static_cast<unsigned long long>(memory.Budget>>20),static_cast<unsigned long long>(memory.CurrentUsage>>20));
 return text;
}catch(...){return {};}
}  // namespace

void Renderer::report_adapter() noexcept try{
 ComPtr<IDXGIDevice> dxgi_device;ComPtr<IDXGIAdapter> adapter;DXGI_ADAPTER_DESC desc{};
 if(FAILED(device_.As(&dxgi_device))||FAILED(dxgi_device->GetAdapter(&adapter))||FAILED(adapter->GetDesc(&desc)))return;
 char name[128]{};
 WideCharToMultiByte(CP_UTF8,0,desc.Description,-1,name,static_cast<int>(sizeof(name))-1,nullptr,nullptr);
 for(char& c:name)if(c=='"'||c=='\\'||(c>0&&c<0x20))c='_';
 // The user-mode driver version as DXGI reports it for the adapter.
 char driver[32]="unknown";LARGE_INTEGER umd{};
 if(SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice),&umd)))
  std::snprintf(driver,sizeof(driver),"%u.%u.%u.%u",
   static_cast<unsigned>(HIWORD(umd.HighPart)),static_cast<unsigned>(LOWORD(umd.HighPart)),
   static_cast<unsigned>(HIWORD(umd.LowPart)),static_cast<unsigned>(LOWORD(umd.LowPart)));
 char summary[256]{};
 // No DXGI_ADAPTER_DESC memory sizes: they are SIZE_T, which wraps above
 // 4 GiB in this 32-bit process. The local budget below is the real figure.
 std::snprintf(summary,sizeof(summary),"adapter=%s vendor=%04x device=%04x driver=%s",
  name,desc.VendorId,desc.DeviceId,driver);
 adapter_summary_=summary;
 ComPtr<IDXGIAdapter3> adapter3;adapter.As(&adapter3);
 // The non-local (system memory) segment's budget: on an integrated GPU the
 // local figure above is already shared system memory.
 char shared[48]=" shared_budget=unknown";DXGI_QUERY_VIDEO_MEMORY_INFO non_local{};
 if(adapter3&&SUCCEEDED(adapter3->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL,&non_local)))
  std::snprintf(shared,sizeof(shared)," shared_budget=%lluMiB",static_cast<unsigned long long>(non_local.Budget>>20));
 // The real OS build (GetVersionEx answers per manifest) and its update revision.
 char os[64]="os=unknown";
 using RtlGetVersionFn=LONG(WINAPI*)(OSVERSIONINFOW*);
 if(const auto get_version=reinterpret_cast<RtlGetVersionFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlGetVersion"))){
  OSVERSIONINFOW version{};version.dwOSVersionInfoSize=sizeof(version);
  if(get_version(&version)==0){
   DWORD revision=0,size=sizeof(revision);HKEY key=nullptr;
   if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",0,KEY_QUERY_VALUE,&key)==ERROR_SUCCESS){
    DWORD type=0;if(RegQueryValueExW(key,L"UBR",nullptr,&type,reinterpret_cast<BYTE*>(&revision),&size)!=ERROR_SUCCESS||type!=REG_DWORD)revision=0;
    RegCloseKey(key);
   }
   std::snprintf(os,sizeof(os),"os=%lu.%lu.%lu.%lu",version.dwMajorVersion,version.dwMinorVersion,version.dwBuildNumber,revision);
  }
 }
 // Capabilities that separate FL 11_0 parts (Intel Gen7 class) from newer
 // ones; the renderer's shaders need none of the optional ones (doubles,
 // typed UAV loads beyond R32, render-target array index from the VS).
 D3D11_FEATURE_DATA_DOUBLES doubles{};D3D11_FEATURE_DATA_THREADING threading{};
 D3D11_FEATURE_DATA_D3D11_OPTIONS2 options2{};D3D11_FEATURE_DATA_D3D11_OPTIONS3 options3{};
 const bool have_doubles=SUCCEEDED(device_->CheckFeatureSupport(D3D11_FEATURE_DOUBLES,&doubles,sizeof(doubles)));
 const bool have_threading=SUCCEEDED(device_->CheckFeatureSupport(D3D11_FEATURE_THREADING,&threading,sizeof(threading)));
 const bool have_options2=SUCCEEDED(device_->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS2,&options2,sizeof(options2)));
 const bool have_options3=SUCCEEDED(device_->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS3,&options3,sizeof(options3)));
 const auto flag=[](bool known,BOOL value){return known?(value?"1":"0"):"?";};
 char detail[640]{};
 std::snprintf(detail,sizeof(detail),"%s subsys=%08x revision=%02x%s%s feature_level=%x %s"
  " doubles=%s concurrent_creates=%s typed_uav_load_formats=%s uma=%s vp_rt_index_any_stage=%s",
  summary,desc.SubSysId,desc.Revision,local_memory_text(adapter3.Get()).c_str(),shared,static_cast<unsigned>(feature_level_),os,
  flag(have_doubles,doubles.DoublePrecisionFloatShaderOps),flag(have_threading,threading.DriverConcurrentCreates),
  flag(have_options2,options2.TypedUAVLoadAdditionalFormats),flag(have_options2,options2.UnifiedMemoryArchitecture),
  flag(have_options3,options3.VPAndRTArrayIndexFromAnyShaderFeedingRasterizer));
 bridge_report_event("device_adapter",detail);
 // A driver without concurrent creates gets them serialised by the runtime
 // anyway: preparing on one worker loses nothing and keeps its compiler
 // single-threaded from our side.
 driver_concurrent_creates_=have_threading&&threading.DriverConcurrentCreates;
 if(!driver_concurrent_creates_)
  bridge_report_event("shader_preparation_serial","driver reports no concurrent creates (D3D11_FEATURE_THREADING); one worker");
}catch(...){}

void Renderer::report_device_removed(HRESULT reason) noexcept try{
#if defined(EID3D11_EMBEDDED) && !defined(EID3D11_SHADER_EMBED)
 dlss_backend_.device_lost(reason);
#endif
 static bool reported=false;
 if(reported)return;
 reported=true;
 const char* name="unknown";
 switch(static_cast<unsigned long>(reason)){
 case 0x887A0005ul:name="DXGI_ERROR_DEVICE_REMOVED";break;
 case 0x887A0006ul:name="DXGI_ERROR_DEVICE_HUNG";break;
 case 0x887A0007ul:name="DXGI_ERROR_DEVICE_RESET";break;
 case 0x887A0020ul:name="DXGI_ERROR_DRIVER_INTERNAL_ERROR";break;
 case 0x887A0001ul:name="DXGI_ERROR_INVALID_CALL";break;
 case 0x8007000Eul:name="E_OUTOFMEMORY";break;
 default:break;
 }
 char detail[512]{};
 std::snprintf(detail,sizeof(detail),"reason=%08lx/%s %s%s",static_cast<unsigned long>(reason),name,
  adapter_summary_.empty()?"adapter=unknown":adapter_summary_.c_str(),local_memory_text(adapter3_.Get()).c_str());
 bridge_report_event("device_removed",detail);
 // The prepared shader objects belong to the lost device.
 if(shader_objects_)shader_objects_->clear();
}catch(...){}
namespace {

// The scene thread needs a core of its own beside the game thread: below 4
// logical processors (as Windows reports them to this process) it defaults
// off. A saved choice still wins.
bool scene_thread_default() noexcept{return std::thread::hardware_concurrency()>=4;}

// Settings migration. The ini saves every registered key, so a changed
// registered default never reaches an existing ini by itself. Each entry is a
// key whose default changed, the settings_version that changed it and the
// default before (bool 0/1): an ini saved before that version that still
// holds the old default drops it and takes the new one. Any other saved value
// is a choice and stays. Raise kSettingsVersion with each new entry.
// Version 7: the ray tracing preset became detected from the rt_* options
// (migrate_rt_quality), not a stored tier that overrode them.
// Version 8: dune_wisps joined the graphics presets (Low: off).
// Version 9: tree_detail_distance joined the graphics presets (#238).
// Version 10: fire_burn joined the graphics presets (#254; Low off, Medium
// and up on, so its registered default is on). No kChangedDefaults row: a
// Custom file keeps its saved value, off unless the player turned it on.
// Version 11: the 64-bit render host and Direct3D 12 became the defaults
// (x64_render_host on, renderer_backend 1; user decision 2026-11-20). A
// file saved on a named preset that still holds the old defaults takes the
// new ones (migrate_settings); a Custom file keeps its values. A host or
// Direct3D 12 start that fails turns its option off again, once.
// Version 12: hud_width 2 (anchored HUD capped to a centred 16:9 area) -> 0
// (full width): on 21:9 and wider the default HUD sits in the screen corners
// (player report 2026-11-21). proxy.cpp applies the same rule to the value it
// reads before this runs; HUD profiles no longer carry hud_width.
// Version 13: ground_cover_shadows, hosted_plants (new), tree_sway,
// weather_wind_variation, tumbleweeds, soft_particles, portrait_head and
// water_simple (stock-look water, Lowest only)
// joined the graphics presets (#318): Low drops cover shadows and hosted
// plants, Lowest also the rest. A file on a named preset takes them; Custom
// keeps its values.
constexpr int kSettingsVersion=14;
constexpr struct{const char* key;int version;float old_default;} kChangedDefaults[]={
 {"depth_of_field",1,2.f}, // 2 -> 0, in no preset (candidate-67)
 {"render_thread",1,0.f},  // off -> on, off below 4 logical processors (candidate-71)
 {"hud_width",12,2.f},     // 16:9 cap -> full width; in no preset
 {"fur_mode",5,0.f},       // prototype off -> full fur (High preset), in the player menu; an old 3 clamps to 2 (full)
 {"rt_reflection_roughness",7,.4f}, // 0.4 -> 0.5 (user decision 2026-11-14); no ray tracing preset sets it, so every preset with reflections takes it
};

// Version 3 split the one Upscaling list (dlss_mode, with FSR's own
// fsr_sharpness) into method, quality and one sharpening for either
// upscaler. The old keys are no longer registered, so the loader skipped
// them: they are read once from the file here and the next save drops them.
std::string migrate_upscaling(RendererConfig& config) noexcept try{
 const std::wstring path=renderer_config_path();
 const int legacy=static_cast<int>(GetPrivateProfileIntW(L"graphics",L"dlss_mode",0,path.c_str()));
 int method=kUpscalerOff,quality=0;legacy_upscaling_choice(legacy,method,quality);
 // A launcher may already have written v3 keys to a fresh/older ini.
 // Migrate only missing keys, without skipping unrelated version migrations.
 wchar_t current[32]{};
 const bool has_method=GetPrivateProfileStringW(L"graphics",L"upscaler",L"",current,32,path.c_str())>0;
 const bool has_quality=GetPrivateProfileStringW(L"graphics",L"upscaler_quality",L"",current,32,path.c_str())>0;
 if(!has_method)config.set_integer("upscaler",method);
 if(!has_quality&&method!=kUpscalerOff)config.set_integer("upscaler_quality",quality);
 wchar_t text[32]{};
 bool sharpness_read=false;float sharpness=0.f;
 if(GetPrivateProfileStringW(L"graphics",L"fsr_sharpness",L"",text,32,path.c_str())>0){
  wchar_t* end=nullptr;sharpness=std::wcstof(text,&end);
  sharpness_read=end!=text&&std::isfinite(sharpness);
  if(sharpness_read)config.set_floating_point("upscale_sharpness",std::clamp(sharpness,0.f,1.f));
 }
 char detail[96];
 std::snprintf(detail,sizeof(detail),"legacy %d -> upscaler %d quality %d (v3 keys kept); fsr_sharpness %s",legacy,method,quality,sharpness_read?"kept":"absent");
 return detail;
}catch(...){return "failed";}

}  // namespace

// Ensure (the overlay's ini) runs it once after loading and saves to
// save_path. Core mode: the game process migrates its copy without saving
// (null, core_ensure) and the host, the only writer of the ini there,
// migrates and saves the game's ini (scene_core::Config).
void migrate_settings(RendererConfig& config,const wchar_t* save_path) noexcept try{
 const int saved=config.settings_version();
 if(saved>=kSettingsVersion)return;
 const std::string upscaling=saved<3?migrate_upscaling(config):std::string("current");
 // Version 4 split terrain_quality Low (0) / High (1) into Low / Medium /
 // High: each saved value moves up one step so the look stays (old Low is
 // the Medium shader, old High is High); only a new choice reaches Low.
 int terrain_quality=-1;
 if(saved<4)if(const RendererOption* option=config.find("terrain_quality");option&&option->type==RendererOptionType::integer){
  terrain_quality=std::min(option->integer_value+1,2);config.set_integer("terrain_quality",terrain_quality);
 }
 // A file saved on a named preset of the old table takes that preset's
 // current values (graphics_settings.cpp keeps the old rows for version 1).
 // Version 6: a file saved at 5 holds fur_mode 2 on a named preset only from
 // version 5's default reset (graphics_settings.cpp kPresetRowsBeforeVersion6);
 // any other value there is a choice and stays.
 const RendererOption* fur=config.find("fur_mode");
 const int fur_saved=fur&&fur->type==RendererOptionType::integer?fur->integer_value:-1;
 const GraphicsPreset preset=reapply_graphics_preset_before(config,saved);
 if(terrain_quality>=0)config.set_integer("terrain_quality",terrain_quality); // the old Low preset keeps Medium
 if(saved==5&&preset!=GraphicsPreset::custom&&fur_saved>=0&&fur_saved!=2)config.set_integer("fur_mode",fur_saved);
 std::string reset;

 for(const auto& change:kChangedDefaults){
  if(change.version<=saved)continue;
  // A row of the re-applied preset holds the preset's value (Low: fur_mode
  // 0), not the old default: version 5 turned full fur on at Low this way.
  if(preset!=GraphicsPreset::custom&&graphics_preset_row(change.key))continue;
  const RendererOption* option=config.find(change.key);
  if(!option)continue;
  const bool old_default=option->type==RendererOptionType::boolean?option->boolean_value==(change.old_default!=0.f):
   option->type==RendererOptionType::integer?option->integer_value==static_cast<int>(std::lround(change.old_default)):
   option->type==RendererOptionType::floating_point&&std::fabs(option->floating_value-change.old_default)<1e-4f;
  if(old_default&&config.reset_to_default(change.key)){reset+=reset.empty()?"":" ";reset+=change.key;}
 }
 // Version 7: a tier saved in rt_quality overrode the rt_* options; its
 // values move into them (the look stays) and rt_quality goes back to Custom.
 const int rt_saved=saved<7?static_cast<int>(migrate_rt_quality(config)):static_cast<int>(RtQuality::custom);
 config.set_settings_version(kSettingsVersion);
 if(save_path&&config.save(save_path))config.clear_dirty();
 char detail[416];
 std::snprintf(detail,sizeof(detail),"settings_version %d -> %d; old defaults reset: %s; preset %s%s; upscaling: %s; rt_quality %d%s",saved,kSettingsVersion,
  reset.empty()?"none":reset.c_str(),graphics_preset_name(preset),preset!=GraphicsPreset::custom?" re-applied from the old table":" (no old preset matched)",
  upscaling.c_str(),rt_saved,rt_saved!=static_cast<int>(RtQuality::custom)?" moved into the rt_* options":"");
 bridge_report_event("settings_migrated",detail);
}catch(...){}

// Crash counter (#299). eid3d11.running exists while a game-process session
// runs and holds how many sessions before it in a row ended without a clean
// exit. The game's orderly quit (WM_QUIT reaching its main loop: menu Quit,
// Alt+F4, the harness quit), a clean exit (Renderer::release from the game's
// teardown) or two minutes in a world deletes it, so quitting after playing
// never counts against a working setup. WM_QUIT is the owner: on the D3D11
// path WinMain's teardown need not reach ~DD (a9468590 smoke run: exit code
// 0, marker left, the reset box on the next launch), the reason the
// over-budget marker clears there too (cf4da0662). A crash, hang or kill
// posts no WM_QUIT. Developer mode (the test launcher) skips it: harness runs
// are killed on purpose and must never block on a message box.
namespace {
std::atomic<bool> g_crash_session_open{false};
HHOOK g_crash_quit_hook=nullptr;
unsigned g_crash_world_seconds=0;
ULONGLONG g_crash_next_check=0;
std::wstring crash_session_path(){
 const std::wstring ini=renderer_config_path();
 return ini.substr(0,ini.find_last_of(L"\\/")+1)+L"eid3d11.running";
}
bool write_crash_session(unsigned crashes) noexcept{
 const std::wstring path=crash_session_path();
 const HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(file==INVALID_HANDLE_VALUE)return false;
 char text[16];const int length=std::snprintf(text,sizeof(text),"%u",crashes);DWORD written=0;
 const bool ok=WriteFile(file,text,static_cast<DWORD>(length),&written,nullptr)&&written==static_cast<DWORD>(length);
 CloseHandle(file);return ok;
}
void end_crash_session(const char* why) noexcept{
 if(!g_crash_session_open.exchange(false))return;
 DeleteFileW(crash_session_path().c_str());
 bridge_report_event("crash_count_cleared",why);
}
// WH_GETMESSAGE on the game window's thread: WM_QUIT retrieved by WinMain.
LRESULT CALLBACK crash_quit_hook(int code,WPARAM wparam,LPARAM lparam){
 const MSG* message=reinterpret_cast<const MSG*>(lparam);
 if(code==HC_ACTION&&message&&message->message==WM_QUIT)end_crash_session("WM_QUIT: orderly quit");
 return CallNextHookEx(g_crash_quit_hook,code,wparam,lparam);
}
// Kept by the reset: the player's language, interface and HUD layout,
// interface profile, input and presentation (vsync, frame limits) choices.
bool kept_by_crash_reset(const RendererOption& option) noexcept{
 return option.group=="Language"||option.group=="Interface"||option.group=="HUD layout"||
  option.group=="Profiles"||option.group=="Input"||option.group=="Presentation";
}
} // namespace

void Renderer::start_crash_session() noexcept try{
 static bool started=false;
 if(started)return;started=true;
 if(renderer_developer_mode())return;
 const std::wstring path=crash_session_path();
 unsigned crashes=0;
 const HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(file!=INVALID_HANDLE_VALUE){
  char text[16]{};DWORD read=0;ReadFile(file,text,sizeof(text)-1,&read,nullptr);CloseHandle(file);
  crashes=static_cast<unsigned>(std::strtoul(text,nullptr,10))+1;  // the last session ended without a clean exit
 }
 // No settings file: nothing to reset (a fresh install, or one the player
 // already reset by hand), so the count starts over without a prompt.
 if(crashes>=2&&GetFileAttributesW(renderer_config_path().c_str())==INVALID_FILE_ATTRIBUTES){
  bridge_report_event("crash_reset_prompt","skipped: no settings file to reset");
  crashes=0;
 }
 if(crashes>=2){
  SYSTEMTIME now{};GetLocalTime(&now);wchar_t stamp[32];
  swprintf_s(stamp,L"%04u%02u%02u-%02u%02u%02u",now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond);
  const std::wstring ini=renderer_config_path();
  const std::wstring backup=ini+L".backup-"+stamp;
  const std::wstring backup_name=backup.substr(backup.find_last_of(L"\\/")+1);
  const std::wstring text=
   L"The game crashed "+std::to_wstring(crashes)+L" times in a row with the current graphics settings.\n\n"
   L"Reset graphics settings to defaults? Your current settings will be saved as "+backup_name+L".\n"
   L"Language, interface layout and input settings are kept.\n\n"
   L"\u0418\u0433\u0440\u0430 \u0434\u0432\u0430\u0436\u0434\u044b \u043f\u043e\u0434\u0440\u044f\u0434 \u0437\u0430\u0432\u0435\u0440\u0448\u0438\u043b\u0430\u0441\u044c \u0430\u0432\u0430\u0440\u0438\u0439\u043d\u043e \u0441 \u0442\u0435\u043a\u0443\u0449\u0438\u043c\u0438 \u043d\u0430\u0441\u0442\u0440\u043e\u0439\u043a\u0430\u043c\u0438 \u0433\u0440\u0430\u0444\u0438\u043a\u0438.\n\n"
   L"\u0421\u0431\u0440\u043e\u0441\u0438\u0442\u044c \u043d\u0430\u0441\u0442\u0440\u043e\u0439\u043a\u0438 \u0433\u0440\u0430\u0444\u0438\u043a\u0438 \u043f\u043e \u0443\u043c\u043e\u043b\u0447\u0430\u043d\u0438\u044e? \u0422\u0435\u043a\u0443\u0449\u0438\u0435 \u043d\u0430\u0441\u0442\u0440\u043e\u0439\u043a\u0438 \u0431\u0443\u0434\u0443\u0442 \u0441\u043e\u0445\u0440\u0430\u043d\u0435\u043d\u044b \u0432 \u0444\u0430\u0439\u043b\u0435 "+backup_name+L".\n"
   L"\u042f\u0437\u044b\u043a, \u0440\u0430\u0441\u043f\u043e\u043b\u043e\u0436\u0435\u043d\u0438\u0435 \u0438\u043d\u0442\u0435\u0440\u0444\u0435\u0439\u0441\u0430 \u0438 \u043d\u0430\u0441\u0442\u0440\u043e\u0439\u043a\u0438 \u0443\u043f\u0440\u0430\u0432\u043b\u0435\u043d\u0438\u044f \u0441\u043e\u0445\u0440\u0430\u043d\u044f\u0442\u0441\u044f.";
  const int answer=MessageBoxW(hwnd_,text.c_str(),L"Evil Islands: graphics settings / \u0421\u0431\u0440\u043e\u0441 \u043d\u0430\u0441\u0442\u0440\u043e\u0435\u043a \u0433\u0440\u0430\u0444\u0438\u043a\u0438",
   MB_YESNO|MB_ICONWARNING|MB_TOPMOST|MB_SETFOREGROUND);
  // The game draws only while its window is active (laptop job 113: after
  // this box the window stayed inactive and the menu never drew until a
  // click). Hand the foreground back; the box made this process the
  // foreground one, so the request is honoured.
  if(hwnd_){SetForegroundWindow(hwnd_);SetActiveWindow(hwnd_);}
  char detail[160];
  if(answer==IDYES){
   // Every registered option outside the kept groups back to its default;
   // [setup] first_run_done goes, so the first-run suggestion runs again.
   const bool copied=CopyFileW(ini.c_str(),backup.c_str(),FALSE)!=FALSE;
   register_config_options();
   RendererConfig& config=debug_overlay_.config();
   config.load(ini.c_str());
   unsigned reset=0;
   for(const RendererOption& option:config.options())
    if(!kept_by_crash_reset(option)&&config.reset_to_default(option.key.c_str()))++reset;
   config.set_first_run_done(false);config.set_over_budget_pending(false);
   const bool saved=copied&&config.save(ini.c_str());
   if(saved)config.clear_dirty();
   std::snprintf(detail,sizeof(detail),"%u crashes in a row: player chose reset; %u options back to defaults, backup %s, saved %s",
    crashes,reset,copied?"written":"FAILED",saved?"yes":"no (backup failed, ini untouched)");
  }else{
   std::snprintf(detail,sizeof(detail),"%u crashes in a row: player kept the settings; asks again after 2 more",crashes);
  }
  bridge_report_event("crash_reset_prompt",detail);
  crashes=0;
 }
 g_crash_session_open=write_crash_session(crashes);
 if(g_crash_session_open&&hwnd_&&!g_crash_quit_hook)
  g_crash_quit_hook=SetWindowsHookExW(WH_GETMESSAGE,crash_quit_hook,nullptr,GetWindowThreadProcessId(hwnd_,nullptr));
} catch(...){}

void Renderer::note_crash_session_present() noexcept{
 if(!g_crash_session_open)return;
 const ULONGLONG now=GetTickCount64();
 if(now<g_crash_next_check)return;
 g_crash_next_check=now+1000;
 AmbientWorldSource world;
 if(bridge_ambient_world_source(world)&&world.terrain&&++g_crash_world_seconds>=120)
  end_crash_session("2 minutes in a world");
}

#if defined(EID3D11_EMBEDDED) && !defined(EID3D11_SHADER_EMBED)
HRESULT Renderer::ensure(HWND hwnd,UINT w,UINT h){
 if(!embedded_sky().available_to(this))return E_UNEXPECTED;
 if(embedded_d3d12_failed_)return E_FAIL;
 if(d3d12_)return S_OK;
 output_width_=std::max(1u,w);output_height_=std::max(1u,h);
 camp_inventory_output(canvas_width(),canvas_height());
 set_world_size();hwnd_=hwnd;
 register_config_options();
 return ensure_embedded_d3d12();
}
HRESULT Renderer::fail_embedded_d3d12(const char* operation,HRESULT result,const char* reason) noexcept{
 if(!embedded_d3d12_failed_){
  embedded_d3d12_failed_=true;
  char text[2048]{};
  std::snprintf(text,sizeof(text),"Direct3D 12: %s failed (0x%08lx). %s",
   operation,static_cast<unsigned long>(result),reason&&*reason?reason:"See the engine journal for details.");
  bridge_report_event("d3d12_failed",text);
  // A modal dialog here parks the game thread inside a render entry point.
  // Keep the failure latched and return it after journalling the reason.
 }
 return FAILED(result)?result:E_FAIL;
}
// Caller registers/loads configuration and sets hwnd_/output dimensions first.
// Selected before D3D11 allocation. Failure returns directly to the caller;
// it never starts a different backend.
HRESULT Renderer::ensure_embedded_d3d12(){
 if(d3d12_)return embedded_d3d12_failed_?E_FAIL:S_OK;
 std::string reason;
 const auto failed=[&](const char* step,HRESULT hr){
  fail_embedded_d3d12(step,hr,reason.empty()?embedded_d3d12_.failure():reason.c_str());
  release();
  return hr;
 };
 try{
  if(!embedded_d3d12_.initialize(hwnd_,reason))return failed("device/swap chain creation",E_FAIL);
  d3d12_=embedded_d3d12_.frame();
  debug_overlay_set_running_renderer("64-bit embedded, Direct3D 12",false);
  debug_overlay_set_running_d3d12(true);
  if(IDXGIAdapter* adapter=d3d12_->adapter())adapter->QueryInterface(IID_PPV_ARGS(adapter3_.ReleaseAndGetAddressOf()));
  debug_overlay_.set_renderer(&d3d12_->overlay_renderer());
  HRESULT hr=debug_overlay_.initialize(hwnd_,nullptr,nullptr,nullptr,d3d12_->adapter());
  if(FAILED(hr))return failed("overlay initialization",hr);
  auto& config=debug_overlay_.config();
  migrate_settings(config,renderer_config_path().c_str());
  debug_overlay_.sync_interface_profile();recover_shader_crash();
  apply_config_settings();
  {x64remote::DlssCaps caps{};
   const bool available=embedded_d3d12_.dlss_caps(caps);
   dlss_initializing_=caps.initializing;
   caps.reason[sizeof(caps.reason)-1]=0;
   set_dlss_unavailable_reason(available?nullptr:(caps.reason[0]?caps.reason:"not available"));}
  set_fsr_unavailable_reason(d3d12_->fsr_upscale_unavailable());
  render_scale_percent_=std::clamp(configured_integer(config,"render_scale",100),kRenderScaleMinimum,100);
  dlss_mode_=configured_dlss_mode();fsr_mode_=configured_fsr_mode();set_world_size();
  scene_composited_=true;
  retained_visual_scene().visual_set_surface_texture_retainer(&Renderer::retain_detached_surface_texture,this);
  profiler_.set_timestamps(&d3d12_->profile_timestamps());
  if(FAILED(hr=profiler_.initialize(nullptr,nullptr)))return failed("profiler initialization",hr);
  neighbour_maps_set_d3d12(d3d12_->preparer());
  if(FAILED(hr=native_d3d12_ensure()))return failed("native pipeline initialization",hr);
  frame_statistics_.reset();system_telemetry_snapshot_={};system_telemetry_active_=false;
  system_telemetry_.start(adapter3_.Get());start_liquid_maps_job();
  if(!embedded_sky().attach(this))return failed("sky attachment",E_UNEXPECTED);
  embedded_d3d12_failed_=false;
  bridge_report_event("renderer_backend","Direct3D 12 embedded executor initialized");
  return S_OK;
 }catch(const std::bad_alloc&){return failed("initialization allocation",E_OUTOFMEMORY);}
 catch(...){return failed("initialization exception",E_FAIL);}
}
#endif

// The player and developer options (eid3d11.ini [graphics] and friends):
// registered in every Renderer, so a core-mode game process (which owns the
// ini) and the x64 host (which applies the values it is sent) share one list.
void Renderer::register_config_options(){
 auto& config=debug_overlay_.config();
#ifndef EID3D11_EMBEDDED
config.register_boolean("camera_pan_smoothing","Camera pan smoothing (experimental)","Input",false,false,"Smooths horizontal camera panning. Off by default. Requires compatible camera controls; unavailable if another component owns the same updates. A missed camera reset may briefly slide the view; turn this off if loads, camera cuts or picking look wrong.");
#endif
config.register_boolean("confine_cursor","Confine cursor to game client area","Input",true,false,"Keeps pointer-driven camera and interaction input inside the active game client area; releases immediately when the game loses focus.");config.register_boolean("vsync","Vertical sync","Presentation",present_interval()!=0,false,"Use a synchronized Present interval. Disabled remains the uncapped performance default. Forced off in developer mode.");config.register_integer("background_mode","In the background","Presentation",0,0,1,1,false,"What the game does while its window is not in front. 0: keeps running and drawing (capped by the background frame limit). 1: pauses; the world stops and the last frame stays on screen. Either way it never minimises itself or reloads its textures on return.");config.register_integer("background_frame_limit","Background frame limit (FPS)","Presentation",30,0,240,1,false,"While the game window is not in front, Present is paced to at most this many frames per second, so a game left in the background idles quietly. 0: off.");config.register_integer("frame_limit","Frame rate limit (FPS)","Presentation",0,0,500,1,false,"0: off (uncapped). Otherwise Present is paced to this many frames per second, measured from the previous present (high-resolution timer plus a short final spin). Forced off in developer mode.");config.register_boolean("shadows","Shadows","Graphics",true,false,"Render owned directional shadow maps and sample them in world shading. Changes apply at the next scene boundary.");config.register_boolean("terrain_normal_sunlight","Terrain normal sunlight","Graphics",true,false,"Compare terrain shading. On uses terrain normals for direct sun; off applies full direct sun while keeping projected shadows, ambient light, textures, and fog.");
config.register_boolean("terrain_relief_derivfree","Terrain relief without screen differences (#243)","Materials",true,false,"Ground whose height comes from the Terrain 2.0 material cache takes its relief slope from the cache's height layer beside the pixel instead of from the differences between the screen pixels of a 2x2 block, which another triangle's pixels can upset (dotted lines along terrain triangle edges on a Radeon RX 580). A pixel whose block test fails on ground the cache holds reads its slope there too. Off: the screen differences (the comparison).");
config.register_boolean("smaa","Anti-aliasing: SMAA","Graphics",true,false,"SMAA 1x on the world scene, before interface rendering.");
config.register_integer("rt_quality","Ray tracing preset (experimental)","Lighting",5,0,5,1,false,"The ray tracing preset, its own choice beside the graphics preset (which never changes a ray traced option). Off, Low, Medium, High: choosing one writes its values into the ray traced options it names (graphics_settings.cpp, the ray tracing preset table) and nothing else: no preset reads or writes upscaling, Ray Reconstruction or frame generation (user decision 2026-11-15). The menu shows the preset those options match, Custom after one of them changed, Off while no ray traced term is on. D3D12 with a ray tracing tier 1.1 adapter. Low: ray traced sun shadows, ambient occlusion (occlusion rays alone, a quarter of the render size a side) and water reflections. Medium: adds the indirect light (half of the render size a side, one spatial sample, no final visibility ray; a quarter flickered at DLSS Performance) and the lamp light. High: the indirect light at half of the render size with two spatial samples and the final visibility ray, and reflections on smooth surfaces (in a preset only with rt_ray_reconstruction on). Every traced term runs without Ray Reconstruction, the player's own toggle (off by default: ~13 ms GPU at DLSS Quality and ~27 ms at DLAA on a 3070 at 2560x1600). The former Ultra (High's options at DLAA, told apart by the upscaler) is folded into High. The stored value only carries an older build's choice into the options once (settings version 7; its 4, Ultra, becomes High); a harness 'config rt_quality N' applies preset N (0 Off to 3 High).");
config.register_boolean("rt_sun_shadows","Ray traced sun shadows (experimental)","Lighting",false,false,"D3D12 with a ray tracing tier 1.1 adapter only (elsewhere it does nothing and names the reason once). The sun shadow of opaque and cut-out world surfaces comes from rays instead of the sun shadow maps: from each visible point toward the sun through the active map's terrain and the static objects of the active and neighbour maps (foliage cards by their texture's alpha). Units cast from their pose of the frame before, and only those on screen. Not traced: grass as a caster. While it is on, the sun shadow maps drop to one close map at the smallest size plus the far maps: blended surfaces, fog shafts and grass and fur shading keep reading them. It keeps the depth prepass on whatever depth_prepass says (the traced mask shades the prepassed receivers) and turns the sun shadow receivers on by itself, so it also works with the raster Sun shadows option off.");
config.register_boolean("rt_ray_reconstruction","DLSS Ray Reconstruction (experimental)","Lighting",false,false,"Only while ray tracing is on (rt_sun_shadows or rt_direct_lights, on a ray tracing adapter) and with NVIDIA DLSS upscaling on (Upscaling: NVIDIA DLSS, any quality including DLAA), on D3D12; without ray tracing, with DLSS off or with another upscaler it does nothing and DLSS Super Resolution or the plain path stays. DLSS Ray Reconstruction replaces DLSS Super Resolution as the upscaler and anti-aliasing of the world image and denoises the ray traced lighting itself; the traced options also run without it, except a ray tracing preset's smooth-surface reflections (rt_reflection_surfaces), which a preset draws only with it on. The world's opaque and cut-out surfaces and the ground cover also write its guide images (surface normal with roughness, diffuse colour, specular colour) through their own program variants. Needs an RTX card, driver 580 or newer and material quality above Off; otherwise DLSS Super Resolution stays and the reason is named once. Not yet in the guide images: creature fur, critters, water surfaces, particles and precipitation (they keep the guides of the surface behind them); strand hair writes its own guides through its depth hull and takes no traced light. DLSS exposure and sharpening do not apply to it; the Sharpening option still runs after it. No ray tracing preset writes it (off by default: it costs ~13 ms GPU at Medium/High and ~27 ms at Ultra/DLAA on a 3070 at 2560x1600).");
config.register_boolean("rt_sun_cover_shadows","Ray traced sun shadows: ground cover and critter shadows","Lighting",true,false,"With rt_sun_shadows: the grass and other ground cover (by the Ground cover shadows setting) and the critters are not in the ray tracing scene, so the rays do not see them. On, they are drawn every frame into their own shadow map around the near view (rt_sun_cover_range) and their shadow joins the traced one there. Off, they cast no sun shadow while rt_sun_shadows is on.");
config.register_floating_point("rt_sun_cover_range","Ray traced sun shadows: ground cover shadow range (metres)","Lighting",16.f,4.f,64.f,1.f,false,"Half the side of the square near the camera in which the ground cover and the critters cast sun shadows while rt_sun_shadows is on (rt_sun_cover_shadows). Smaller is sharper: the square always takes one whole shadow map.");
config.register_floating_point("rt_sun_softness","Ray traced sun shadows: sun disc radius (degrees)","Lighting",.5f,0.f,3.f,.05f,false,"Angular radius of the sun disc the shadow rays sample: 0 gives hard shadows, larger values wider penumbrae far from the caster. The real sun is 0.27.");
config.register_integer("rt_sun_rays","Ray traced sun shadows: rays per pixel","Lighting",1,1,8,1,false,"Shadow rays per visible pixel each frame. One relies on the shadow history to average the penumbra; more cost proportionally more and leave less noise.");
config.register_floating_point("rt_sun_history","Ray traced sun shadows: history weight","Lighting",.9f,0.f,.98f,.01f,false,"Share of the previous frames kept in the traced sun shadow each frame. Higher is smoother and slower to follow the sun; 0 shows each frame's own rays.");
config.register_floating_point("rt_unit_shadow_softness","Ray traced sun shadows: character shadow softness (degrees)","Lighting",1.f,0.f,5.f,.1f,false,"How soft the shadows characters and creatures cast are: the sun disc radius their penumbra is widened for, so it grows with the height of the part casting it and stays sharp where they touch the ground. Units move, so their shadow takes no history; a screen filter softens it instead (at most 6 pixels at 1080 lines). 0 gives the hard edge of one ray.");
config.register_boolean("rt_direct_lights","Ray traced lamp light (experimental)","Lighting",false,false,"D3D12 with a ray tracing tier 1.1 adapter and material quality above Off only (elsewhere it does nothing and names the reason once). The direct light of lamps, torches, fires and emissive surfaces (lava) on the world's opaque and cut-out surfaces comes from ReSTIR direct lighting (NVIDIA RTXDI) with ray traced visibility instead of the lamp loop and the point shadow cubes: every lamp is a small sphere and every lava light a rectangle, so their shadows are soft by distance. The sun stays on rt_sun_shadows or the sun maps. Water, particles, fur, critters, ground cover and the fog keep the lamp loop and the shadow cubes. It runs with or without rt_ray_reconstruction (user decision 2026-11-15); with it, DLSS Ray Reconstruction also denoises the result. Memory: 72 bytes a render pixel plus 8 for the wider guide images (logged once).");
config.register_floating_point("rt_direct_light_radius","Ray traced lamp light: lamp radius (metres)","Lighting",.15f,0.f,1.f,.01f,false,"Radius of the sphere a lamp's shadow rays aim at: 0 gives hard lamp shadows, larger values wider penumbrae.");
config.register_floating_point("rt_direct_light_clearance","Ray traced lamp light: clearance around a lamp (metres)","Diagnostics",.35f,0.f,2.f,.05f,false,"A lamp's shadow rays end this far before the lamp, so the lamp's own model (the lantern or torch around the light) does not shadow it; the point shadow cubes' near plane does the same.");
config.register_boolean("rt_indirect_light","Ray traced indirect light (experimental)","Lighting",false,false,"D3D12 with a ray tracing tier 1.1 adapter and material quality above Off only (elsewhere it does nothing and names the reason once). One bounce of diffuse indirect light on the world's opaque and cut-out surfaces from rays (ReSTIR GI, NVIDIA RTXDI) at half of the render resolution, instead of the bounce light volume on those surfaces: what a ray from the surface hits is lit by the sun (one shadow ray), a lamp of its place (one shadow ray) and the bounce light volume there (the later bounces), and sends that light back with its own colour. The ambient light stays as it is. Water, particles, fur, critters, ground cover and the fog keep the bounce light volume. It runs with or without rt_ray_reconstruction (user decision 2026-11-15); with it, DLSS Ray Reconstruction also denoises the result. Memory: 28 bytes a render pixel plus 8 for the wider guide images (logged once).");
config.register_boolean("rt_ambient_occlusion","Ray traced ambient occlusion (experimental)","Lighting",false,false,"D3D12, a ray tracing adapter. The occlusion of the ambient light comes from the indirect light's bounce rays instead of the screen-space search (without rt_indirect_light those rays are traced for the occlusion alone), so it knows what is off screen or hidden behind something: the ground under a roof, between buildings and under tree crowns loses its sky light, not only creases. With rt_indirect_light no extra rays are cast. It replaces the ambient occlusion passes' search (also when Ambient occlusion is 0) and keeps their strength setting; a surface the ray traced indirect light does not cover (fur, critters, particles) gets no occlusion of its own. Without rt_indirect_light the screen-space ambient occlusion stays.");
config.register_boolean("rt_ao_contact","Ray traced ambient occlusion: screen-space contact detail","Lighting",true,false,"With rt_ambient_occlusion: grass blades, fur and critters are not in the ray tracing scene, so the rays do not see them. On, the screen-space search (reach rt_ao_contact_radius) adds their contact shading: each point takes the darker of the ray traced and the screen-space occlusion, so nothing the screen-space ambient occlusion shades is lost. Off shows the rays alone and skips the search.");
config.register_floating_point("rt_ao_contact_radius","Ray traced ambient occlusion: contact reach (metres)","Lighting",1.5f,.1f,4.f,.05f,false,"World-space reach of the screen-space contact search rt_ao_contact adds (never more than the AO radius).");
config.register_floating_point("rt_ao_radius","Ray traced ambient occlusion: reach (metres)","Lighting",5.f,.5f,30.f,.5f,false,"How far away a surface above a point still hides the sky from it outdoors: an upward bounce ray's hit closer than this occludes, fully next to the point and fading to nothing at this distance. Level and downward rays (toward the ground) occlude within the AO radius only. In a cave the reach is the AO radius setting (contact shading only: a cave has no sky).");
config.register_boolean("rt_indirect_cave_exits","Ray traced indirect light: cave exit daylight","Lighting",false,false,"With rt_indirect_light in a cave: a bounce ray that leaves through a cave exit's opening returns the outdoor light of the hour, so daylight floods in through the exits from rays. Off, the exits' light reaches a traced surface only as a later bounce from the bounce light volume. It assumes nothing of the cave's own geometry closes the opening in front of the exit.");
config.register_boolean("rt_reflections","Ray traced reflections (experimental)","Lighting",false,false,"D3D12 with a ray tracing tier 1.1 adapter and material quality above Off only (elsewhere it does nothing and names the reason once). Water reflects what its mirror rays meet in the ray tracing scene, on screen or not: the screen's colour where the hit is visible, else the hit lit by the sun, the lamps and the ambient light; a ray that meets nothing keeps the screen reflection and the sky. Smooth surfaces (polished metal, crystal, glass, lacquer, wet material) reflect their surroundings from one ray a pixel instead of the sky probe alone; rougher ones keep the probe (rt_reflection_roughness). Off: the screen-space water reflections and the sky probe, unchanged.");
config.register_floating_point("rt_reflection_roughness","Ray traced reflections: roughness cut-off","Lighting",.5f,.1f,1.f,.05f,false,"With rt_reflections: surfaces rougher than this keep the sky probe reflection; the traced share fades in over the 0.1 of roughness below it.");
config.register_floating_point("rt_reflection_water_reach","Ray traced reflections: water ray reach (metres)","Lighting",250.f,10.f,4000.f,10.f,false,"With rt_reflections: how far a water reflection ray looks for the scene. A hit in the last two fifths of the reach fades into the screen reflection and the sky, so distant backdrop relief does not darken open water.");
config.register_integer("rt_indirect_resolution","Ray traced indirect light: resolution","Lighting",0,0,2,1,false,"The size the indirect light's bounce rays and reservoirs run at (rt_indirect_light, rt_ambient_occlusion). 0: half of the render size a side (a ray for every fourth render pixel each frame). 1: a quarter (a ray for every sixteenth); the ambient occlusion passes then run at a quarter too. 2: the full render size (a ray for every render pixel); rt_ambient_occlusion then traces its own occlusion rays at half the render size. With DLSS Ray Reconstruction 0 also runs at the full size (Ray Reconstruction needs an independent sample in every pixel).");
config.register_boolean("rt_indirect_visibility","Ray traced indirect light: final visibility ray","Lighting",true,false,"With rt_indirect_light: after the spatial resampling one ray checks that the kept sample is visible from the pixel (ReSTIR GI's final visibility). Off saves that ray; bounce light may then leak through thin walls and into corners.");
config.register_boolean("rt_indirect_denoise","Ray traced indirect light: denoiser","Lighting",true,false,"With rt_indirect_light and without DLSS Ray Reconstruction: the bounce light is accumulated over the frames a surface stays in view (up to 24) and filtered across neighbouring pixels of the same surface (a temporal pass and three edge-aware a-trous passes, as SVGF and NVIDIA's ReLAX do for diffuse light), so it does not show as grain. Ray Reconstruction denoises it itself and takes the undenoised light. Memory: 48 bytes a render pixel. Off: the bounce light as traced.");config.register_floating_point("rt_indirect_rr_randomization","Ray traced indirect light: Ray Reconstruction history randomization","Lighting",0.f,0.f,200.f,10.f,false,"With rt_indirect_light and DLSS Ray Reconstruction: the temporal reuse takes its history sample from a random place within this radius (pixels per 960 of the width) around the pixel's reprojected place, so neighbouring pixels do not share one sample for many frames (RTX Remix's DLSS-RR compatibility mode, which uses 80). 0 (default): the reprojected place itself; measured 2026-11-15 on the save1403 sand, 20 to 80 added grain and brightened the bounce light by 2 to 5 percent, and at the Ingos pine it made no reliable difference.");config.register_floating_point("rt_indirect_outlier_clamp","Ray traced indirect light: outlier clamp","Lighting",5.f,0.f,20.f,.5f,false,"With rt_indirect_light and DLSS Ray Reconstruction: a pixel's bounce light is held to this many times the mean of its 8x8 pixel block, so a rare bright sample does not come out of Ray Reconstruction as a bright flickering dot. 0: no clamp.");
config.register_integer("rt_indirect_spatial_samples","Ray traced indirect light: spatial samples","Lighting",2,0,4,1,false,"With rt_indirect_light: how many neighbours the spatial resampling takes a sample from each frame (ReSTIR GI). 0: no spatial resampling. More leaves less noise for the denoiser and costs more.");
config.register_boolean("rt_reflection_surfaces","Ray traced reflections: smooth surfaces","Lighting",true,false,"With rt_reflections: polished metal, crystal, glass, lacquer and wet material reflect their surroundings from rays. Off: only water takes its reflection from rays; the surfaces keep the sky probe. In a ray tracing preset (not Custom) they apply only with rt_ray_reconstruction on (rt_reflections_omitted names it once).");
config.register_boolean("rt_emissive","Light from particles (and lava under ray tracing)","Lighting",true,false,"Magic and fire light the world, with or without ray tracing: the game's additive particle effects (sparks, healing, protection spheres, portals, lightning blasts), lightning bolts, and the bright, saturated flame and spell colours of the other particle effects (fireball, firewall, fire, campfires; grey or dark smoke and dust do not): they are grouped by place into lamps whose strength follows the particles' own colour and size. Without ray tracing the lamp loop adds them without shadows; rt_direct_lights gives them ray traced shadows and rt_indirect_light a bounce, and where the ray traced lamp light does not reach (water, fur, ground cover) the lamp loop adds them without shadows. Each flame adds its own lamp beside the map's lights. Torch and campfire flames have 4-8 m reach from their size and brightness and bridge brief sample gaps before fading; fireball and firewall keep their size-based reach. Buffs and spell sparkles light as a soft sphere of their own cloud (a surface inside it is lit from all round), at most 1.5 m and 0.5 per channel. With rt_indirect_light, a bounce ray that reaches lava returns the lava's own colour beyond the reach of the lava glow lights (Emissive lighting), so lava lights far cliffs and cave walls. The lights follow game time: frozen on pause.");
config.register_floating_point("rt_emissive_particle_strength","Particle light strength","Lighting",1.f,0.f,4.f,.05f,false,"Brightness of the lamps rt_emissive makes from glowing particles. 1 is the strength the particles' own colour and size give.");
config.register_boolean("rt_indirect_rr_half","Ray traced bounce light: half size under Ray Reconstruction","Lighting",false,false,"With DLSS Ray Reconstruction on and the bounce light resolution at Half, trace it at half size instead of the full render size; each render pixel takes one of its four nearest traced pixels' samples, chosen at random by how well its surface matches, so Ray Reconstruction still gets one sample a pixel. Faster; check for grain. Off: full size under Ray Reconstruction.");
config.register_boolean("rt_figure_refit","Ray tracing: refit the units' structure","Diagnostics",true,false,"The units' ray tracing structure is refitted to their new pose each frame while their meshes stay, and the scene's top level to their new bounds, instead of both being built again every frame (NVIDIA's guidance for deforming meshes). Either is built again when a unit's mesh changes or a unit joins or leaves, and every rt_figure_rebuild_frames frames. Off builds both every frame: the comparison this option exists for. The rt_figures event counts builds and refits.");
config.register_integer("rt_figure_rebuild_frames","Ray tracing: units' structure rebuild interval","Diagnostics",30,1,600,1,false,"With rt_figure_refit: the units' structure and the scene's top level are built again at least once in this many frames, so a refitted tree does not loosen as units walk.");
config.register_boolean("rt_instancing","Ray tracing: instanced static casters","Diagnostics",true,false,"The static casters of the ray tracing scene as instances of their unique meshes, each built once in object space. Off holds a world-space copy of every caster instead (more memory, a smaller top level): the comparison this option exists for. host.log names the bottom-level memory and the top level's size after each change; the rt_statics event names the arenas.");config.register_integer("rt_term_view","Ray tracing term view (diagnostic)","Diagnostics",0,0,12,1,false,"With a ray traced option on: replace the world image with one input or term of the traced passes per pixel. 1: the owner the guide images name (red a unit, green terrain, blue another lit surface, yellow ground cover and other surfaces the traced passes do not light, grey drawn without guides, black sky). 2: the sun term. 3: the indirect light (x4). 4: the ambient occlusion's visibility. 5: the guide albedo. 6: the guide normal. 7: the object motion (blue where a posed record wrote it). 8: the lighting factor and the fog share. 9-12 (rt_indirect_light): the bounce light alone, what it adds to the lighting factor without albedo or any other term: 9 linear (x4) and 10 tonemapped as the upscaler's input (its output shows what DLSS or Ray Reconstruction makes of it); 11 linear and 12 tonemapped drawn over the upscaled output from the render pixels, the signal exactly as the upscaler received it (a temporal upscaler only).");config.register_integer("rt_debug_view","Ray tracing debug view (diagnostic)","Diagnostics",0,0,7,1,false,"5 to 7 (shading data at ray hits): replace the world image with what a ray from the eye through each pixel returns at its nearest hit, not the drawn image: 5 the hit's albedo (terrain from the terrain 2.0 material cache's whole-world level, a brown constant where that is off or not baked; objects their texture; units their texture's mean colour), 6 the hit lit by the ambient light, the sun through one shadow ray and the lamps of its place, 7 its normal. Dark blue: no hit. 4 (with rt_sun_shadows on; the world image stays): magenta where a receiver that faces the sun starts its ray under the scene's terrain (its nearest terrain surface toward the sun is an underside), the acceptance check of the rest-position rule: none beyond single pixels is expected, with terrain tessellation on or off. Modes 1-3: D3D12 with a ray tracing tier 1.1 adapter only (elsewhere it does nothing and names the reason once). 1: replaces the world image with one hard sun ray per pixel traced from the world depth through the ray tracing scene (the active map's untessellated terrain and the static casters of rt_sun_shadows, every triangle opaque): warm where the sun reaches, dark blue where terrain blocks it or the surface is turned away from the sun. 2: terrain alone, from the drawn depth position (not the rest position the traced mask uses, so red on tessellated cliffs is expected; mode 4 checks the mask's own receivers): the side of the nearest terrain surface each ray hits (blue top: terrain shadow; red underside: a sunward receiver under the scene surface; grey none; near black: turned away from the sun, not traced), a check of the scene itself. 3: where a traced ray is blocked by terrain, green if this frame's raster sun-shadow mask shadows the pixel too, magenta if it does not (yellow: no mask). Turning it on builds the terrain's ray tracing scene once per map; 0 releases it and runs nothing.");
config.register_integer("prepass_depth_check","Depth prepass check (diagnostic)","Diagnostics",0,0,3,1,false,"D3D12: after each prepassed world batch, mark in red (green and blue zeroed) its fragments in front of the prepass depth that the alpha test keeps: what the material pass's depth EQUAL rejects. It also starts the shadow mask at no value, so a receiver the mask pass's stencil test skipped shows magenta. Calibration: 2 marks every fragment, 3 inverts the stencil test.");
config.register_boolean("shader_split","Material instance shaders","Performance",true,false,"World figures and objects draw the pixel program compiled for their material instance (unit, world object, foliage, crown impostor; with or without a material profile). Off is a debug comparison only: the general program draws them, with the same image.");
config.register_integer("terrain_slim_stencil","Terrain second pass: stencil mark","Performance",2,0,2,1,false,"Debug comparison (D3D12, terrain slim pass): before the second passes, the 2x2 quads holding a pixel the slim pass missed are marked in stencil, so the full terrain program starts only on them, not on every pixel of a missed triangle. 2: the slim pass lists the missed quads and the mark draws only those. 1: the mark is a full-screen pass reading the misses. 0: no mark; the program discards the other pixels itself. The same image in all three.");
config.register_boolean("prepass_object_program","Depth prepass: object program","Performance",true,false,"Debug comparison (D3D12): material instance batches (figures, objects, foliage, crown impostors) draw their depth prepass with a program without the terrain receiver code. Off: the general prepass program, with the same image.");
config.register_boolean("prepass_cutouts_last","Depth prepass: cut-outs last","Performance",true,false,"Debug comparison (D3D12): the depth prepass draws alpha-tested batches (crowns, foliage, their impostors) after the other batches, so depth rejects their hidden fragments before the texture read. Off: build order, with the same image.");
config.register_integer("object_program_cuts","Object program variants","Performance",3,0,3,1,false,"Debug comparison (D3D12), two switches added together. 1: world figures and objects draw the dry-frame programs (no wet or snow code) while the wet level and snow cover of the frame are both 0. 2: world object and foliage batches with no draw in reach of the ground draw the programs without the ground contact blend, as crown impostor batches always do. 0: the full programs, with the same image.");
config.register_integer("shader_split_check","Material instance check","Performance",0,0,2,1,false,"Debug. 1: material instance batches draw the check programs, which paint a marker where a record's own flags disagree with its instance (red unit, green foliage or impostor, blue contact or profile) and, in the D3D12 hazards build, report them. 2: the same with every batch given the next domain's check program (a known-bad run: the markers and reports must appear). Set before the game starts (the check programs are created with the pipeline).");
config.register_boolean("deferred_world_material","Terrain material after objects","Performance",true,false,"Direct3D 12: the terrain and neighbour-map material draws after the ground cover, critters and objects have drawn their depth, so the ground they hide is not shaded (the same image). Off: the terrain material draws before them.");
config.register_boolean("indexed_object_draws","Indexed object draws","Performance",true,false,"Direct3D 12: world figure and object batches drawn through their material instance programs use index lists, so the GPU reuses a vertex its neighbouring triangles share instead of running the vertex program three times a triangle (the same image). Off: every triangle corner runs it.");
config.register_boolean("depth_prepass","Depth prepass and shadow mask","Performance",true,false,"Opaque and alpha-tested world surfaces first draw depth only; one full-screen pass then takes the sun and cloud shadow per visible pixel, and the full material shading runs once per pixel. Off draws every surface with its own shadow lookups (more overdraw). Direct3D 12: ray traced sun shadows keep it on while they are on.");
config.register_integer("ground_cover_owner_cull","Ground cover owner culling","Performance",1,0,1,1,false,"D3D12. 1: the cover depth prepass records which instance each covered pixel shows, and the material pass draws only those instances (same image). 0: every visible instance draws twice.");
config.register_integer("ground_cover_neighbour_reuse","Ground cover neighbour placement reuse","Performance",1,0,1,1,false,"D3D12. 1: while the camera and a neighbour map's cover inputs stay the same, that map keeps last frame's placed cover instead of placing it again (same image). 0: placed every frame.");
config.register_integer("ground_cover_occlusion","Ground cover occlusion","Performance",1,0,1,1,false,"D3D12. 1: ground cover instances hidden behind this frame's terrain are not drawn (their sun shadows stay). 0: every placed instance draws.");
config.register_integer("upscaler","Upscaling","Graphics",kUpscalerOff,kUpscalerOff,kUpscalerMethodMaximum,1,false,"0: off (the world renders at the render scale). 1: NVIDIA DLSS (64-bit render host and an RTX card). 2: AMD FSR (FSR 3.1 with Direct3D 12, FSR 2 with Direct3D 11; most cards). The world renders at a lower resolution set by the upscaling quality and is reconstructed at the screen size; it replaces SMAA and the render scale. Applies at the next frame.");
config.register_integer("upscaler_quality","Upscaling quality","Graphics",0,0,kUpscalerQualityMaximum,1,false,"0: Quality, 1: Balanced, 2: Performance, 3: Ultra Performance, 4: native resolution anti-aliasing (DLAA with DLSS, Native AA with FSR). Sets the internal resolution while upscaling is on.");
config.register_floating_point("upscale_sharpness","Sharpening","Graphics",.2f,0.f,1.f,.05f,false,"Strength of the RCAS sharpening after either upscaler (DLSS or FSR). 0 turns it off; keep it low to stay close to the game's look.");
config.register_integer("render_scale","Render scale (%)","Graphics",100,kRenderScaleMinimum,100,1,false,"The 3D world (and every world effect) renders at this percentage of the screen size and is upscaled before the interface, which always stays at full resolution. Applies at the next frame.");
config.register_integer("camp_ui_layout","Camp layout","Interface",1,0,1,1,false,"Switch the open camp without restarting: original UI comparison or inventory grid. Keeps the current mode and transaction; supports compact 16:10 and wider displays.");
config.register_integer("ui_language","Menu language","Language",0,0,2,1,false,"Language of the renderer's menus. 0: auto (Russian when the Windows display language is Russian or its ANSI code page is 1251), 1: English, 2: Russian. Applies at the next frame.");
#ifndef EID3D11_EMBEDDED
config.register_boolean("x64_render_host","64-bit render host","Display",true,true,"Runs the renderer and the graphics driver in a separate 64-bit process, leaving the game much more memory (fewer out-of-memory crashes on big maps); DLSS needs it. Applies after restarting the game. If the host does not start, the option turns itself off.");
config.register_boolean("harness_hide_hud","Harness shots without the interface","Diagnostics",false,false,"Test harness: screenshot commands capture the next world frame's image before any interface (HUD, portraits, text) is drawn, instead of the composed frame.");
#endif
config.register_boolean("harness_shot_after_overlay","Harness shots after the overlay","Diagnostics",false,false,"Test harness: screenshot commands capture the frame after the F10 overlay and the cursor (to check the overlay) instead of the game's image before them.");
config.register_integer("frame_generation","Frame generation","Presentation",0,0,2,1,false,"0: off. 1: NVIDIA DLSS Frame Generation: one generated frame between each two rendered frames, presented at even spacing by the renderer (the interface, cursor and F10 overlay are drawn on every presented frame); GeForce RTX 40 or 50. 2: AMD FSR 3.1 frame generation (FidelityFX): one generated frame between each two rendered frames, presented and paced by FidelityFX; any Direct3D 12 GPU (interface, cursor and overlay on generated frames are taken from the rendered frame). Both: Direct3D 12 backend of the 64-bit render host; need DLSS or FSR upscaling (its depth and motion vectors); add about one rendered frame of latency. F10 shows presented and rendered FPS.");
config.register_integer("frame_generation_min_fps","Frame generation minimum FPS","Presentation",30,0,120,5,false,"Frame generation pauses while the rendered rate stays below this for 2 s (only real frames are shown; logged) and resumes once the rendered rate, less what generating costs, clears it by 10% for 3 s. 0: never pauses. AMD designs FSR 3.1 frame generation for 60 rendered FPS and its swap chain for at least 30; below that generated frames show heavy artifacts and add a long rendered frame of latency. Not a frame cap.");
config.register_integer("reflex","NVIDIA Reflex","Presentation",0,0,2,1,false,"Test (Direct3D 12 backend, NVIDIA GPUs): 0 off. 1: Reflex low latency mode with latency markers (the driver keeps the GPU queue short). 2: also the Reflex sleep before the render host releases the game's next frame. Never a frame cap. host.log reports the game-frame-to-present latency every 5 s in every mode.");
config.register_integer("latency_pacing","Latency pacing","Presentation",0,0,1,1,false,"Test (Direct3D 12 backend, any GPU): 1 starts the game's next frame later, by as much as its frame would otherwise wait for the render host, so input is read closer to when the frame is drawn. Never a frame cap; the delay stays under 3/4 of a frame. host.log reports the delay and the remaining wait every 5 s.");
config.register_boolean("fg_frame_image","Frame generation inputs (D3D12)","Diagnostics",false,false,"D3D12 backend: the frame draws into its own image (world and interface) with a HUD-less copy of the world, then is copied to the screen before the F10 overlay, cursor, load reveal and gamma, as frame generation will need. Looks the same; costs two full-screen copies a frame. Harness captures write hudless.bmp beside ui_composition.bmp.");
config.register_integer("display_mode","Display mode","Display",0,0,1,1,false,"0: borderless fullscreen on the primary monitor, 1: a movable window of the chosen window size. Use Apply display to change this during gameplay.");
config.register_integer("window_width","Window width","Display",0,0,16384,1,false,"Client width of the windowed mode (0: the monitor's size). Use Apply display to change this during gameplay.");
config.register_integer("window_height","Window height","Display",0,0,16384,1,false,"Client height of the windowed mode (0: the monitor's size). Use Apply display to change this during gameplay.");
config.register_boolean("owned_ui_text","Owned interface text","Performance",false,false,owned_ui_text_forced()?
 "Interface text and line drawing is recorded instead of drawn, and a panel is redrawn (with the same GDI output) only when what it draws changes. Off draws every panel through GDI each time the game asks. (Forced on: the system code page isn't 1251, and only this path draws the game's Russian text readably.)":
 "Interface text and line drawing is recorded instead of drawn, and a panel is redrawn (with the same GDI output) only when what it draws changes. Off draws every panel through GDI each time the game asks.");
config.register_integer("anisotropy","Anisotropic filtering","Graphics",3,0,4,1,false,"Texture filtering for the world, previews, interface and images: 1, 2, 4, 8 or 16 samples.");
config.register_boolean("water_reflections","Water reflections","Graphics",true,false,
 "Reflect the current opaque scene on non-emissive liquids; misses use the sky.");
config.register_integer("water_reflection_quality","Water reflection quality","Graphics",2,0,2,1,false,
 "Detail of the screen-space water reflection search. 0: low (12 steps, for weak GPUs; small reflected edges break up more), 1: medium (20 steps), 2: high (40 steps; finer reflected edges where the screen shows the reflected surface, a little more GPU time on water).");
config.register_floating_point("water_reflection_step_pixels","Water reflection step bound (px)","Performance",8.f,0.f,64.f,1.f,false,
 "0: every reflection ray takes the quality's full step count. Otherwise a ray whose whole search projects short on screen (distant water) takes only as many steps as keep each step within this many world-image pixels; rays that already need the full count (near water) are unchanged.");
config.register_boolean("water_simple","Original water","Graphics",false,false,
 "For weak GPUs (the Lowest preset): liquid surfaces draw like the game's own water through the world's ground shader: the liquid tile's own texture with its tile animation, the game's vertex lighting and authored opacity, and the wave glint. No reflections, refraction, foam, shore or ripples.");
config.register_boolean("water_physical","Physical water","Graphics",true,false,
 "With water reflections: Snell refraction and per-channel absorption along the view path through the water depth, a GGX sun highlight widened by distant ripple detail, and rounded liquid-tile outlines. Off keeps the previous water shading.");
config.register_integer("water_surf_quality","Sea surf quality","Graphics",2,0,2,1,false,
 "Detail of the sea surf. 0: two chop octaves fewer than set, one displaced, no foam bubbles or foam edge (for weak GPUs); 1: one octave fewer, two displaced; 2: all octaves, three displaced.");
config.register_integer("water_surf_debug","Sea surf debug view","Diagnostics",0,0,3,1,false,
 "Temporary. 1: sea water drawn unlit by its sea surf exposure (blue sheltered, through yellow, to red fully exposed), with darker stripes along the incoming wave direction. 2: the shore impact (how steep the shore is: dark green beach, through yellow, to magenta cliff or quay) over the coastal water and the shore ground. 3: only the surf foam (on the water) and the foam thrown onto the shore ground, as brightness.");
// The sea surf look (liquid_surface.hlsl liquidSurfWaveParams..LookParams), live.
config.register_floating_point("water_surf_wave_height","Sea swell height (m)","Graphics",.6f,0.f,2.f,.05f,false,
 "Crest-to-trough height of a large swell crest rolling in toward the shore; crests grow a little more as the water shoals and break where it is about as deep as they are high.");
config.register_floating_point("water_surf_wavelength","Sea swell wavelength (m)","Graphics",9.f,2.f,30.f,.5f,false,
 "Average distance between swell crests; the chop's longest octave scales with it.");
config.register_floating_point("water_surf_speed","Sea swell speed (m/s)","Graphics",1.2f,.1f,4.f,.05f,false,
 "How fast swell crests roll toward the shore, in game seconds (freezes on pause, follows game speed). The chop drifts at about half this.");
config.register_floating_point("water_surf_choppiness","Sea chop sharpness","Graphics",2.5f,1.f,6.f,.1f,false,
 "Peaking of the chop's crests: 1 round, higher sharper crests over broader troughs. Each finer octave is a little rounder.");
config.register_integer("water_surf_octaves","Sea chop octaves","Graphics",4,1,6,1,false,
 "Layers of chop at rising frequency on High surf quality (Medium one fewer, Low two fewer). More layers: finer detail, more GPU time on sea water.");
config.register_floating_point("water_surf_chop_height","Sea chop height (m)","Graphics",.25f,0.f,1.f,.01f,false,
 "Height of the noise-warped chop over the whole sea, beside the swell. Calmer in the breaking shallows.");
config.register_floating_point("water_surf_segment","Sea crest segment length (m)","Graphics",18.f,4.f,60.f,1.f,false,
 "Typical length of a swell crest segment along the coast: crests arrive, break and land segment by segment, at scattered places and times.");
config.register_floating_point("water_surf_randomness","Sea crest randomness","Graphics",.75f,0.f,1.f,.05f,false,
 "0: long, even crests landing along the coast together; 1: short segments with low gaps, arrivals well out of step.");
config.register_floating_point("water_surf_shore_foam","Sea shore foam amount","Graphics",1.f,0.f,2.f,.05f,false,
 "Strength of the foam clumps, lace and scalloped edge along beaches.");
config.register_floating_point("water_surf_shore_width","Sea shore foam width (m of water)","Graphics",.6f,.05f,1.5f,.05f,false,
 "Water depth up to which the shore foam reaches (it swells with each arriving crest).");
config.register_floating_point("water_surf_crest_foam","Sea crest foam threshold","Graphics",.95f,.6f,1.f,.005f,false,
 "Foam on steep wave faces: where the surface normal's upward share falls below this. Higher: more crest foam.");
config.register_floating_point("water_surf_foam_cell","Sea foam cell size (m)","Graphics",1.6f,.3f,4.f,.05f,false,
 "Size of the foam clumps, lace cells and scallops.");
config.register_floating_point("water_surf_shallow_tint","Sea shallow tint","Graphics",.7f,0.f,1.f,.05f,false,
 "How strongly clear sea shallows turn turquoise over the bed, and deeper blue with depth.");
config.register_floating_point("water_surf_absorption","Sea absorption","Graphics",1.f,0.f,3.f,.05f,false,
 "Beer-Lambert absorption of the sea tint per metre of water (red first, then green): higher turns blue sooner.");
config.register_floating_point("water_surf_cliff_burst","Sea cliff burst strength","Graphics",1.f,0.f,3.f,.05f,false,
 "How high the white water and spray burst up cliffs and quays, and how far the foam is thrown back off the rock, per arriving crest.");
config.register_text("water_surf_no_sea_maps","Maps without sea","Graphics",
 "Comma-separated map names (for example zone7) whose water never counts as sea: no surf, sea colour or foam there, whatever its open water. Takes effect on the next map load.","zone7");
config.register_text("water_lake_maps","Maps with a lake","Graphics",
 "Comma-separated map names (default zone18 and zone19, Suslanger) whose open water is an inland lake, not the sea: plain water with only the ordinary ripples and the map's own water colour, with no sea swell, shore waves, surf, foam or sea colour. Takes effect on the next map load.","zone18,zone19");
config.register_floating_point("water_surf_edge_fade","Sea surf edge fade (m)","Graphics",40.f,0.f,120.f,5.f,false,
 "Where the sea runs off the maps around the hero, the surf, swell and foam fade into plain water over this distance.");
config.register_integer("bloom","Bloom","Graphics",2,0,2,1,false,"Soft glow around the brightest world light: sun highlights, sky near the sun, lava, fire, lamps and magic. 0: off, 1: low (4 half-resolution blur levels), 2: high (6 levels, wider glow). The interface never glows.");
config.register_integer("emissive_lights","Emissive lighting","Graphics",1,0,2,1,false,"Lava and other self-lit surfaces light the ground, objects and characters around them with a soft glow: point lights just above the surface, no shadows, pulsing slowly with game time. 0: off, 1: on (one light per 8 m cell of lava), 2: finer (one per 4 m cell; about 3x the lights and a little more GPU time near lava).");
config.register_floating_point("emissive_light_strength","Emissive light strength","Graphics",1.f,0.f,3.f,.05f,false,"Brightness of the glow that emissive surfaces cast on their surroundings.");
config.register_floating_point("bloom_strength","Bloom strength","Graphics",1.f,0.f,3.f,.05f,false,"How much glow is added to the world image.");
config.register_floating_point("bloom_threshold","Bloom threshold","Graphics",1.f,1.f,3.f,.05f,false,"Where glow starts, relative to the brightest lit surface or fog this frame (1: just above it). Only light brighter than that glows: fire, lamps, lava, magic and sun glints; fogged objects and the sky never do.");
config.register_integer("depth_of_field","Depth of field","Graphics",0,0,2,1,false,"Softly blurs the far background when the camera is tilted toward the horizon; the area around the screen centre stays sharp. The steep gameplay view (50 degrees and steeper) never blurs. 0: off, 1: low (16-tap blur), 2: high (40-tap blur, smoother large blur). The interface never blurs.");
config.register_floating_point("depth_of_field_strength","Depth of field strength","Graphics",.2f,0.f,3.f,.05f,false,"How strongly the background blurs and how close behind the focus the blur begins. 1 is subtle; 3 is a strong cinematic blur.");
config.register_integer("depth_of_field_debug_view","Depth of field debug view","Graphics",0,0,8,1,false,"Direct3D 12 diagnostic. 1: the depth of field path (red blur radius, green sharp, blue sky with its blur coverage; after DLSS/FSR red the stable blend weight, blue the stable layer's weight). 2-5 after DLSS/FSR, marked over the image: 2 motion source (magenta: object motion differs from the camera reprojection of the depth, yellow: zero motion written where the camera moved, cyan: sky, faint green: camera motion); 3 history alignment (red: last frame's colour fetched along the history motion does not match, orange: only the nearest-depth dilation misses, green: moved and matched, white: off screen); 4 history lag (red: the shown blur differs from this frame's, blue: the neighbourhood clamp cut the history); 5 blur radius per pixel (red radius, green sharp, cyan sky); 7 motion scale where the colour has contrast (the scale of the pixel's motion at which last frame's colour matches: blue 0, cyan 0.5, green 1 = right, yellow 1.5, red 2, magenta -1). 6 disocclusion (magenta: the blur layer drops its history, where last frame held a nearer surface than anything around the pixel now); 8 blend weight lag (red: blur laid where this frame is sharp, blue: sharp where this frame blurs, faint yellow: all-sky pixels). Views 2-8 show the last frame whose camera moved.");
config.register_boolean("bloom_debug_view","Bloom debug view","Graphics",false,false,"Shows only the glow that bloom adds, on black; the interface still draws on top.");
config.register_integer("ground_cover","Ground cover","Effects",2,0,3,1,false,"Procedural grass and debris on the ground: lush grass and flowers on meadows, dry tufts, fallen leaves and twigs near trees, small stones, dead bushes, reeds around swamps, kelp and shells on lake and sea shores, seagrass under open water, snow piles in Ingos. Grass sways in the map wind and parts around units. Distant cover thins to a sparser far level instead of ending. 0: off, 1: low (shorter full-density range, half density), 2: normal, 3: high (full density 1.6x farther).");
config.register_boolean("fire_burn","Fire burns the land (D3D12 only)","Effects",true,false,"Fireballs, fire walls and the ground lightning spell ignite grass and trees in their blast. Fire spreads through crowns; foliage burns away, trunks and stumps stay whole and become sooty. Lightning bolts (spell and weather) that reach the ground ignite there. Live moss, lichen and vines char; dead vines and stone stay unchanged. Wood and straw structures have a separate option. Cosmetic only, on the game clock; loading an area resets it. D3D12 only.");
config.register_integer("fire_burn_debug_view","Fire burn debug view (diagnostic)","Diagnostics",0,0,3,1,false,"With fire_burn, while the area has a burn: every surface drawn by a burn program shows a flat colour instead of its image (anything in its normal colours was drawn without the burn programs). 1 burn labels: objects grey inert, orange wood, green foliage, blue structure, magenta a surface that is not a shadow receiver; terrain red where the burn mask holds heat on soil, dark blue heat on inert ground, black none; ground cover yellow burnable, cyan not burnable. 2 burn state: red the flame, green the char, blue an ignited tree crown at that point. 3 the snowfall cover over a burn: magenta where the snow cover code never ran, else green the burn's erase of it and blue its level before.");
config.register_boolean("fire_burn_rt_leaves","Fire burn: traced rays see burnt leaves (diagnostic)","Diagnostics",true,false,"With fire_burn and ray traced indirect light, ambient occlusion or reflections: their rays pass through the leaves a burnt crown lost. Off: those rays treat burnt leaves as intact (to measure the test's own cost; the sun shadows and the raster passes keep the burn).");
config.register_boolean("fire_burn_structures","Fire chars structures (D3D12 only)","Effects",true,false,"With fire_burn (D3D12 only): straw huts, tents, fences and light wood burn black, catch whole and pass the fire to what touches them; heavy wood, hide and stone-and-wood buildings char; stone and metal only take soot, near the ground the fire touched. Cosmetic only: structures keep their geometry and gameplay.");
config.register_integer("critters","Critters","Effects",2,0,2,1,false,"Small ambient creatures, miniatures of the game's own models and procedural ones: rats, spiders and crawler larvae in caves, rats in the city of Suslanger, frogs by swamps, lizards and snakes, scolopendras and dung beetles in Suslanger, shore crabs, bird flocks, ravens near the undead and outside the Dead City wall, vultures circling high over Suslanger, snow birds and snow mice in Ingos, schools of fish in rivers, lakes, swamps and coastal water, squirrels by trees in Gipat and Ingos, prairie dogs in Suslanger, cats in the towns of Ingos and Suslanger, carrion birds at corpses. They hide or flee from units they can hear or see (louder, more visible, running units from further away; crawling or standing still is quieter) and stay away from combat. 0: off, 1: low (rarer, smaller flocks), 2: high.");
config.register_integer("critter_fish_showcase","Fish showcase (diagnostic)","Diagnostics",0,0,19,1,false,"Diagnostic: keeps one school of the chosen fish species (1 roach, 2 perch, 3 bream, 4 rudd, 5 tench, 6 pike, 7 char, 8 grayling, 9 whitefish, 10 burbot, 11 tilapia, 12 killifish, 13 catfish, 14 barbel, 15 mullet, 16 wels, 17 jewel cichlid, 18 rosy barb, 19 panchax) in the water nearest the screen centre, whatever the zone or the party's sight. 0 off.");
config.register_boolean("tumbleweeds","Tumbleweeds","Effects",true,false,"Balls of dry twigs rolling with the wind over the Suslanger desert; one rests now and then in the lee of a rock or a wall. They pause with the game, follow its speed, and a unit walking into one kicks it away. Off: none appear; ones already out leave.");
config.register_integer("tumbleweed_showcase","Tumbleweed showcase (diagnostic)","Diagnostics",0,0,1,1,false,"Diagnostic: in Suslanger, puts two tumbleweeds 5 m and 14 m upwind of the party for the wind to carry into it, once each time this is switched on, and logs where they start and each push. 0 off.");
for(const auto& type:kCritterTypeSettings){
 config.register_boolean(type.key,type.label,"Effects",true,false,(std::string(type.tooltip)+" Off: this type never appears; ones already out vanish.").c_str());
 config.register_integer(type.weight_key,(std::string(type.label)+" spawn weight (%)").c_str(),"Effects",kCritterWeightDefault,0,kCritterWeightMaximum,5,false,
  "Multiplies this type's spawn weight: how often it is picked against the other types that fit the place (100: the normal mix). The number of critters on screen stays the same.");
}
config.register_integer("ambient_particles","Ambient particles","Effects",1,0,2,1,false,"Area-tied atmospheric particles: pollen and grass bits over meadows, falling leaves near trees, grey motes in the Dead City, dust and drips in caves, embers and ash over lava, dust in the desert, fireflies on Gipat nights. 0: off, 1: low, 2: high.");
config.register_floating_point("ambient_particle_density","Ambient particle density","Effects",1.f,0.f,3.f,.05f,false,"Multiplier on the number of ambient particles at the chosen level.");
config.register_floating_point("ambient_particle_size","Ambient particle size","Effects",1.f,.25f,3.f,.05f,false,"Multiplier on the size of ambient particles.");
config.register_integer("shadow_close_range","Close shadow range","Shadows",0,0,400,8,false,"Metres covered by the close shadow maps past where the view meets the ground; 0: 64, 80 or 96 by cascade resolution.");
config.register_integer("shadow_cascade_quality","Cascade resolution","Shadows",1,0,2,1,false,"0: 1024, 1: 2048, 2: 4096 pixels per directional map. Higher quality uses more GPU memory.");
config.register_integer("shadow_close_cascades","Close cascades","Shadows",3,1,3,1,false,"Use one to three close directional maps; fewer maps reduce shadow draw work.");
config.register_boolean("shadow_far","Far shadows","Shadows",true,false,"Render the middle and broad directional maps beyond the close cascades.");
config.register_boolean("shadow_membership_scroll","Partial sun map refresh on membership change","Shadows",true,false,"A sun map whose projection did not move but whose terrain membership changed (far maps follow their receivers while panning) keeps its cached static layer and draws only the parts that entered. Off redraws the whole static layer, as before.");
config.register_integer("shadow_neighbour_casters","Neighbour-map sun casters","Shadows",2,0,2,1,false,"Decorations of the neighbouring maps cast sun shadows and shelter, with the same rules as the active map's statics. 0: off. 1: the close maps (C0-C2) and the shelter. 2: all five maps. Changing it redraws cached sun depth.");config.register_boolean("shadow_far_foliage_opaque","Opaque far foliage shadows","Shadows",true,false,"Draw tree and bush foliage as solid depth in the two far sun maps (cascades 3 and 4). Close maps and point-light shadows keep leaf cutouts. Off restores exact cutout shadows; changing this rebuilds cached sun depth.");
config.register_boolean("shadow_early_out","Shadow filter early-out","Shadows",true,false,"Skip the soft-shadow filter where the sun map proves a pixel fully lit or fully shaded (per 16-texel cell depth bounds). The result is the same; off runs the full filter everywhere, for comparison.");
config.register_boolean("hero_light","Hero light","Graphics",false,false,"Illuminate the area around player-controlled heroes with their attached light. Changes apply at the next world frame; other map lights are unchanged.");
config.register_boolean("point_shadows","Point-light shadows","Shadows",true,false,"Cast shadows from point lights while keeping their illumination.");
config.register_integer("point_shadow_quality","Point-shadow resolution","Shadows",2,0,2,1,false,"0: 128, 1: 256, 2: 512 pixels per cube face. Higher quality uses more GPU memory per shadowed lamp.");
config.register_integer("material_quality","Material quality","Materials",2,0,3,1,false,"Metal, leather, cloth and bone respond differently to the sun, the sky and lamps on characters and equipment. 0 off (the plain image), 1 sun and sky highlights, 2 adds lamp and fire highlights, painted-crevice occlusion and cloth sheen, 3 adds the sky reflection and lamp highlights on world objects. World objects (buildings, rocks, props) get relief, crevices and a rough sun highlight from their own texture from 2 up. The painted colours and the game's lighting stay; the response is added on top.");
config.register_integer("material_view","Material view","Materials",0,0,3,1,false,"Debug: 0 shaded, 1 roughness, 2 metal (magenta: a lit unit without a material profile), 3 dielectric F0 (red), sheen (green), crevice occlusion (blue).");
config.register_floating_point("material_reflections","Sky reflection strength","Materials",1.f,0.f,2.f,.1f,false,"Strength of the roughness-filtered owned-sky reflection on material surfaces. It does not reflect nearby objects.");
config.register_boolean("material_metal_mask","Metal masks","Materials",true,false,"Metal equipment: texels clearly off the layer's metal tint (brown straps, coloured cloth) read as leather or cloth, and dark chain-mail gaps lose their metal shine relative to their surroundings (uniformly dark metals keep it). Off reads each metal layer as uniformly metal.");
config.register_boolean("material_world","World object materials","Materials",true,false,"Buildings, rocks and props: relief, crevice occlusion and a rough sun highlight derived from their own texture (material_quality 2 and up; 3 adds the sky reflection and lamp highlights). Off draws them as before.");
config.register_floating_point("material_relief","World relief strength","Materials",1.f,0.f,2.f,.1f,false,"Strength of the relief world objects take from their texture brightness. 0 keeps the flat normal (the highlight and crevices remain).");
config.register_boolean("material_scenery","Scenery materials","Materials",true,false,"Gates, doors, walls and rocks listed in eid3d_materials\scenery.json take their material classes (iron fittings shine as metal; stone and wood stay rough). Off: they take the plain world object response. Needs World object materials on.");config.register_boolean("material_crystal_see_through","Crystal see-through","Materials",true,false,"World crystals (classes.json crystal) show the scene behind them, bent by the stone: one copy of the world colour on frames that draw a crystal. Off: they draw opaque, lit from inside.");config.register_floating_point("material_world_roughness","World roughness offset","Materials",0.f,-.4f,.2f,.05f,false,"Added to the world objects' base roughness (.75). Negative is glossier.");
config.register_boolean("material_terrain","Terrain materials","Materials",true,false,"Terrain 2.0 ground: relief from each material's baked height (material_quality 1 and up), a sun highlight at the roughness of its material class with crevices rough (2 and up; dry soil, grass and rock stay near matte), and the sky on smooth snow, ice and mud (3). Off draws it as before.");
config.register_floating_point("material_terrain_relief","Terrain relief depth","Materials",.06f,0.f,.2f,.01f,false,"Metres of relief per unit of a terrain 2.0 material's baked height. 0 keeps the flat normal.");
config.register_floating_point("material_terrain_crevices","Terrain crevice roughness","Materials",1.f,0.f,2.f,.1f,false,"How much a terrain texel darker than its material's mean reads rough (no highlight there).");
config.register_integer("shadow_softness","Shadow softness","Shadows",1,0,2,1,false,"0: fixed soft edge (previous filter). 1: contact-hardening soft shadows, low sample count. 2: high sample count. Penumbrae widen with the distance between caster and receiver.");
config.register_integer("sun_shadow_correction","Sun shadow motion","Shadows",0,0,1,1,false,"Close sun maps keep their sun until it turns 0.25 degrees. 1: shadow lookups follow the exact sun across a held map, so shadows move smoothly. 0: lookups along the held sun (shadows step).");
config.register_integer("sun_foliage_filter","Foliage shadow filter","Shadows",kDefaultSunFoliageFilter,0,2,1,false,"Shadow edges for grass, tree crowns and bare branches. 0: soft, as solid objects. 1: steady soft, the stable, safe default while shadows move. 2: a fixed soft edge with sharper branch shadows.");
config.register_integer("shadow_sun_angle","Sun size (0.1 degree)","Shadows",15,1,60,1,false,"Angular diameter of the sun in tenths of a degree when shadow softness is on; larger values give wider sun penumbrae.");
config.register_integer("point_shadow_light_size","Lamp size (%)","Shadows",100,10,400,10,false,"Scales the spherical size of shadow-casting lamps when shadow softness is on; larger lamps give wider penumbrae.");
#ifdef EID3D11_EMBEDDED
config.register_boolean("change_driven_object_poses","Change-driven object poses","Performance",true,false,"Reuse unchanged pose publications after reading the current game placement. Off republishes placement every world draw; animation selection and the picture are preserved.");
#else
config.register_boolean("change_driven_object_poses","Change-driven object poses","Performance",true,false,"World objects re-read their placement only when the game moves them (plus a slow background sweep) instead of every frame.");
#endif
config.register_boolean("terrain_far_shadow_prewarm","Prewarm terrain meshes","Performance",true,false,"After each sun shadow pass, create terrain meshes ahead of time in short time slices so a camera pan does not create many in one frame: the far-cascade shadow meshes for the whole map, and near shadow and transparent terrain meshes in a widened region around the camera. Off creates them on first use. Read every frame.");
config.register_boolean("merge_character_parts","Merge character parts","Performance",true,false,"Draw each character as one record per shared texture/material instead of one per body part. Off draws every part separately for comparison.");
#ifdef EID3D11_EMBEDDED
config.register_boolean("skip_scenery_draw_walk","Skip scenery draw walk","Performance",true,false,"Unanimated scenery uses the object bounds and skips the per-figure visibility walk after publishing its pose. Units, levers, torches, traps and scientific objects keep the full walk. Applies at the next world frame.");
#else
config.register_boolean("skip_scenery_draw_walk","Skip scenery draw walk","Performance",true,false,"Buildings, trees and clutter skip the game's own per-object draw pass (units, levers, torches and traps keep it). Applies at the next scene boundary.");
#endif
config.register_boolean("occlusion_culling","Occlusion culling","Performance",true,false,"Skip objects hidden behind terrain, using a depth pyramid built after opaque terrain.");
config.register_boolean("render_thread","Scene thread","Performance",scene_thread_default(),false,"Scene preparation (visual values, bounds, culling, shadow casters, draw lists), GPU recording and Present run on a separate scene thread one frame behind, while the game thread only copies the frame's game state. Off prepares and presents on the game thread as before. Switches between frames.");
config.register_integer("render_thread_frame_latency","Scene thread frame latency","Performance",2,1,3,1,false,"Test: with the scene thread on, the most frames the GPU may have queued (DXGI maximum frame latency; 3 is the default with it off). The scene thread starts each frame right after Present, so at 3 its per-frame buffer discards ran out of driver copies in the middle of the world and waited there. Applies at the next frame.");
config.register_integer("cull_min_screen_permille","Minimum object size","Performance",5,0,50,1,false,"Objects smaller than this many thousandths of the screen height are not drawn (5 = 0.5%). 0 disables.");
config.register_integer("shadow_cull_min_texels","Minimum shadow caster size","Performance",2,0,8,1,false,"Sun-shadow casters smaller than this many shadow-map texels are not drawn into the map. 0 disables.");
config.register_boolean("highlight_alt","Highlight interactables while Alt is held","Interface",true,false,"While Alt is held, levers, chests, traps, loot on the ground and corpses are filled with a soft colour that shows through walls and terrain.");config.register_boolean("highlight_briefing","Highlight briefing characters","Highlight",false,false,"On the base screen, characters with something to discuss (and the current speakers) glow with the game's white highlight. Off draws them with normal shading.");config.register_boolean("highlight_selected","Highlight selected characters","Highlight",false,false,"Selected party characters glow with the game's white highlight. Off keeps their normal shading and shadows.");config.register_boolean("highlight_hover_characters","Highlight characters under the cursor","Highlight",true,false,"A softer glow on the character the cursor would act on: briefing characters you can talk to on the base screen, enemies you can attack and spell or steal targets on maps. Follows the game's own cursor decision.");config.register_integer("highlight_alt_distance","Alt highlight distance (m)","Highlight",120,20,300,10,false,"While Alt is held, usable objects, ground loot and corpses are filled up to this distance from the camera, fading out over the next quarter of it.");config.register_boolean("highlight_hover_objects","Highlight objects under the cursor","Highlight",true,false,"The same softer glow on what the cursor would use: chests, levers, traps and lootable corpses. Follows the game's own cursor decision.");config.register_integer("cursor_scale","Cursor scale (%)","Interface",100,100,400,25,false,"Size of the game cursor. 100 draws the original pixels exactly; larger sizes are smoothed at the edges.");config.register_integer("pillarbox_fill","Pillarbox fill","Interface",1,0,1,1,false,"Loading screens and movies made for another screen shape are shown at their own shape. 0 fills the bars black, 1 with a soft, darkened blur of the same picture.");config.register_integer("ui_scale","UI scale (%)","Interface",100,50,120,5,false,"Size of the in-mission HUD (map, portrait, weapons, spells, skills, stance wheel, clock, chat). Adjusts immediately from 50% to 120% around each screen corner, including clickable areas. Menus, inventory, shops and dialogs keep their size.");config.register_floating_point("message_panel_width","Message panel width (%)","HUD layout",100.f,50.f,800.f,.1f,false,"Logical width of the loot and quest message panel. Resizing reflows its text.");
config.register_boolean("world_map_previews","World map previews","Interface",false,false,"Terrain and scenery previews for world-map locations. Prepared while a location loads.");
config.register_boolean("portrait_head","Party portraits: real head","Interface",true,false,"The party portraits show each member's own head and hair (strand hair, materials, expressions) through the game's face animation instead of the baked portrait faces. A portrait that cannot be drawn shows its face magenta.");config.register_boolean("portrait_helmet","Party portraits: helmet","Interface",false,false,"Portraits wear the helmet the member wears. Off (the original look) shows the bare head and hair.");
config.register_integer("hud_fade_duration","HUD fade-out duration (ms)","Interface",250,0,10000,50,false,"Shared fade-out duration for all widgets after the idle delay. 0 makes the transition immediate.");
config.register_integer("hud_fade_in_duration","HUD fade-in duration (ms)","Interface",250,0,2000,50,false,"Shared fade-in duration on hover or meaningful updates. 0 restores visibility immediately. Repeated updates keep the fade moving.");
config.register_integer("hud_fade_delay","HUD idle time before fade-out (ms)","Interface",2000,0,60000,100,false,"Time without hover or meaningful updates before fading out. A fade-in finishes first, including at 0 idle time.");
for(const auto& panel:kHudPanels) {
    const std::string prefix=panel.key;
    config.register_boolean((prefix+"_auto_fade").c_str(),(std::string(panel.label)+": auto fade").c_str(),"Interface",false,false,"Fade after the configured idle time without hover or meaningful content changes. Animation alone does not count. Unlocked widgets stay visible.");
    config.register_integer((prefix+"_fade_transparency").c_str(),(std::string(panel.label)+": faded transparency (%)").c_str(),"Interface",80,0,100,5,false,"0% stays fully solid; 100% becomes fully hidden until hover or a content update. Unlocked widgets stay visible.");
}
for(const auto panel:{HudPanel::stance,HudPanel::game_speed}) {
    const auto& description=kHudPanels[unsigned(panel)];
    config.register_floating_point((std::string(description.key)+"_scale").c_str(),(std::string(description.label)+": scale (%)").c_str(),"Interface",100.f,50.f,300.f,5.f,false,"Individual scale multiplied by global UI scale. Fixed in the screen corner, independent of HUD width.");
}
for(unsigned i=0;i<7;++i) {
    const auto& panel=kHudPanels[i];
    const std::string prefix=panel.key;
    config.register_floating_point((prefix+"_scale").c_str(),panel.label,"HUD layout",100.f,50.f,300.f,.1f,false);
    config.register_floating_point((prefix+"_x").c_str(),panel.label,"HUD layout",-1.f,-1.f,100.f,.1f,false);
    config.register_floating_point((prefix+"_y").c_str(),panel.label,"HUD layout",-1.f,-1.f,100.f,.1f,false);
}
config.register_text("interface_profile","Interface profile","Profiles",interface_profiles_help(),"default");
config.register_floating_point("quest_items_scale","Quest items","HUD layout",100.f,50.f,300.f,.1f,false);
config.register_floating_point("quest_items_x","Quest items","HUD layout",-1.f,-1.f,100.f,.1f,false);
config.register_floating_point("quest_items_y","Quest items","HUD layout",-1.f,-1.f,100.f,.1f,false);
for(const auto panel:{HudPanel::minimap,HudPanel::character_info,HudPanel::messages}) {
    const std::string prefix=kHudPanels[unsigned(panel)].key;
    config.register_integer((prefix+"_border_padding").c_str(),"Border padding (px)","HUD layout",3,-100,100,1,false,"Inset between the frame and the content (map, character, text); the button strip and the background are not affected. Global UI scale applies; widget scale does not.");
    config.register_integer((prefix+"_buttons_scale").c_str(),"Buttons scale (%)","HUD layout",100,50,200,5,false);
    config.register_integer((prefix+"_buttons_side").c_str(),"Button alignment","HUD layout",0,0,5,1,false);
}
config.register_integer("spell_bar_direction","Spell icon alignment","Interface",0,0,3,1,false,"Bottom to top, top to bottom, left to right, or right to left. Icons stay upright and retain their bindings.");
config.register_integer("ability_bar_direction","Ability icon alignment","Interface",0,0,3,1,false,"Bottom to top, top to bottom, left to right, or right to left. Icons stay upright and retain their bindings.");
config.register_integer("portrait_direction","Character portrait alignment","Interface",0,0,4,1,false,"Original centered layout, top to bottom, bottom to top, left to right, or right to left. Each portrait keeps its bars, selection and click target.");
config.register_integer("weapon_switcher_direction","Weapon icon alignment","Interface",2,0,3,1,false,"Left to right by default. Changes placement while preserving weapon slots.");
config.register_integer("quick_items_direction","Quick item alignment","Interface",3,0,3,1,false,"Right to left by default. Changes placement while preserving the quick-item slots; quest items are their own widget.");
for(const unsigned i:{0u,1u,4u,5u,6u}) {
    const std::string prefix=kHudPanels[i].key;
    config.register_integer((prefix+"_padding").c_str(),"Icon padding (px)","HUD layout",0,-100,100,1,false);
    config.register_boolean((prefix+"_preview").c_str(),"Preview slots","HUD layout",false,false,"Faint labeled placeholders, visible only while the HUD is unlocked.");
    config.register_integer((prefix+"_preview_count").c_str(),"Preview slot count","HUD layout",i==5||i==6?4:3,1,i==5||i==6?4:32,1,false);
    if(i!=4)config.register_integer((prefix+"_slots_per_column").c_str(),"Slots per column","HUD layout",0,0,32,1,false,"Wrap the bar into another column (or row, for left/right alignment) every this many slots. 0 keeps one line (spell bar: wraps after 8 slots, so Dejavu's 16 slots take two lines).");
}
config.register_integer("message_panel_text_scale","Info panel text scale (%)","Interface",100,50,300,5,false,"Size of loot and quest text, independent of the panel dimensions and multiplied by global HUD scale.");
config.register_integer("hud_border_scale","Panel border scale (%)","Interface",100,0,300,5,false,"Width of gameplay panel borders. Global UI scale also applies; individual widget size does not.");
config.register_integer("hud_border_transparency","Panel border transparency (%)","Interface",0,0,100,5,false,"Transparency of gameplay panel borders. Editor guides remain visible while unlocked.");
config.register_integer("combat_text_scale","Combat text scale (%)","Interface",100,50,300,5,false,"Independent size of scrolling damage numbers and floating combat indicators, including backstab, critical hits and experience. Applies immediately around each existing moving text anchor.");
config.register_floating_point("message_panel_height","Message panel height (%)","HUD layout",100.f,50.f,600.f,.1f,false,"Logical height of the loot and quest message panel.");
config.register_floating_point("message_panel_x","Message panel horizontal position (%)","HUD layout",50.f,0.f,100.f,.1f,false,"Position of the panel's centre across the screen. The panel stays inside the screen.");
config.register_floating_point("message_panel_y","Message panel vertical position (%)","HUD layout",0.f,0.f,100.f,.1f,false,"Position of the panel's top down the screen. Unlock the HUD layout in F10 to drag or resize it.");
config.register_integer("hud_width","HUD width","Interface",0,0,2,1,true,"How far apart the edge-anchored HUD (map, portrait, spells, weapons) may sit on ultrawide screens: 0 full width (the screen corners; the default since settings_version 12), 1 at most 21:9, 2 at most 16:9. A display setting, not part of a HUD layout profile. Applies after restarting the game.");config.register_integer("point_shadow_fast_overlays","Point-shadow full-rate lamps","Shadows",4,0,32,1,false,"This many most important lamps (brightness times reach to the view focus) redraw moving-caster shadows every frame; the others every third frame. 0 redraws all every frame.");config.register_integer("point_shadow_merge","Point-shadow merge distance","Shadows",3,0,8,1,false,"Shadow-casting lights closer than this share one shadow cube (value x 0.5 world units; 0 disables). Illumination stays per light.");
config.register_boolean("weather_wind_variation","Varying wind","Graphics",true,false,"Wind that changes over game time: calm and windier spells, slight shifts of direction, and gustier wind in rain and snow. Moves tree sway, ambient particles and fog wisps. Off keeps the map's steady wind.");
config.register_floating_point("wind_floor","Wind floor","Graphics",kDefaultWindFloor,0.f,.5f,.01f,false,"Gentle minimum wind so grass, reeds and foliage always move a little, even on maps whose own wind is zero. Stronger map winds are unchanged. 0 uses the map's wind only.");
config.register_integer("wind_trails","Wind trails","Graphics",1,0,2,1,false,"Thin translucent streaks that trace the gusts near the camera when the wind blows hard: none on calm spells or in caves, more in storms. Dust-tinted in Suslanger, pale specks in the snow. 0: off, 1: default, 2: dense.");
config.register_integer("dune_wisps","Dune wisps","Graphics",1,0,2,1,false,"Thin sand tendrils blowing off the crests of the Suslanger dunes in the wind direction, more in strong wind, fewer in rain; frozen while paused. 0: off, 1: normal, 2: dense.");
config.register_integer("precipitation","Precipitation quality","Graphics",3,0,4,1,false,"Rain and snow drawn by the renderer in place of the game's own: rain streaks and snowflakes that fall around the view, stop at the ground, follow the wind and the game clock (frozen while paused). 0: off (no rain or snow is drawn), 1: low, 2: medium, 3: high, 4: ultra. Higher levels draw denser rain.");
config.register_boolean("precipitation_surface","Snow cover and wet ground","Graphics",true,false,"Snow settles on the ground, roofs and trees, rain leaves a wet sheen, and units leave tracks in snow and soft sand. Off: rain and snow still fall, the ground stays dry and bare.");
config.register_integer("precip_strength","Rain and snow strength","Graphics",0,0,3,1,false,"How hard it rains or snows. 0: varies per shower, 1: light, 2: medium, 3: heavy. Only the picture changes: the game's rain and snow (sight 0.9) are the same at every strength.");


config.register_integer("precip_test","Precipitation test","Diagnostics",0,0,3,1,false,"Render-only: 1 rain, 2 snow, 3 sand storm (its strength from Precipitation strength test, default heavy), whatever the game's weather; 0 follows the game. The whole visual weather follows (drops, fog, sky, wind, water rings); the game's weather and gameplay are untouched.");
config.register_integer("precip_strength_override","Precipitation strength test","Diagnostics",0,0,3,1,false,"Render-only: 1 light, 2 medium, 3 heavy instead of the Rain and snow strength setting; 0 uses the setting.");
config.register_floating_point("contact_blend","Ground contact blend","Graphics",.7f,0.f,1.f,.05f,false,"Buildings, rocks, fences, trunks and other rigid objects take an uneven band of the ground's own texture (grass, pebbles, dirt, sand, snow) where they meet the terrain, with slight contact darkening. Grass creeps higher in tufts; bare ground stays low. 0 disables it. Characters are never blended.");
config.register_floating_point("contact_blend_height","Ground contact height (m)","Graphics",.4f,.1f,1.5f,.05f,false,"Maximum reach of the ground contact blend on walls (small objects use 12.5% of their transformed height); its edge is ragged, grass reaches higher and bare ground lower, and upward faces collect up to two thirds more.");
config.register_boolean("tree_sway","Tree sway","Graphics",true,false,"Tree and bush foliage sways gently in the map's wind. Trunks stay still; shadows keep the resting shape.");config.register_integer("tree_sway_strength","Tree sway strength (%)","Graphics",100,0,200,10,false,"How far foliage moves in the wind. 100 is gentle; 200 is strong.");
config.register_integer("ao_quality","Ambient occlusion","Graphics",2,0,2,1,false,"Contact shading on the ambient light only (direct sun stays unoccluded). 0: off, 1: low (quarter resolution), 2: high (half resolution). Applies at the next frame.");
config.register_floating_point("ao_strength","AO strength","Graphics",.75f,.5f,4.f,.1f,false,"Exponent on the ambient visibility. 1 is the physical estimate; higher darkens creases more.");
config.register_floating_point("ao_radius","AO radius (m)","Graphics",1.5f,.25f,4.f,.25f,false,"World-space reach of the occlusion search.");
config.register_floating_point("ao_low_light_boost","AO low-light boost","Graphics",1.6f,1.f,3.f,.1f,false,"Strength multiplier reached when the ambient light dominates the sun (night, overcast); 1 disables. Radius grows up to 25%.");
config.register_integer("sky_clouds","Sky and clouds","Graphics",2,0,2,1,false,"Owned sky with volumetric clouds, sun, moon, stars and moving cloud shadows on the ground; follows the game hour, rain and snow, and the region. Water reflects it. 0: the game's original sky, 1: low (quarter-resolution clouds), 2: high (half resolution, more detail).");
config.register_floating_point("cave_brightness","Cave brightness","Graphics",kDefaultCaveBrightness,.25f,1.25f,.05f,false,"Light level in caves and dungeons: scales the cave ambient (1 is the game's own cave table). Caves have no sun light or sun shadows; lava, torches, lamps and glowing objects light them and add on top unchanged. Outdoor zones are not affected.");
config.register_floating_point("cave_ambient_floor","Cave darkness floor","Graphics",kDefaultCaveAmbientFloor,0.f,.6f,.02f,false,"Lowest ambient light in caves (brightest channel, 0-1): keeps unlit cave areas visible. 0 lets them go as dark as the cave table and Cave brightness make them; .30 is the default.");
config.register_integer("cave_gi","Bounce light","Graphics",2,0,2,1,false,"Light from torches, campfires, lamps, lava and other glowing ground bounces once off the ground, rock and walls and lights the surroundings beyond the lamp's direct reach. Outdoors the sun also bounces off the ground; in caves the exits let in the outdoor light of the hour. It only ever adds light. 0: off, 1: low (4 m grid, cheaper), 2: high (2 m grid). A part of it is recomputed every frame.");
config.register_boolean("bounce_debug","Bounce light view","Diagnostics",false,false,"Shows the bounce light alone, eight times brighter, in place of the lamps' direct light.");
config.register_floating_point("sun_bounce_strength","Sun bounce strength","Graphics",kDefaultSunBounceStrength,0.f,4.f,.1f,false,"Brightness of the sunlight bounced off the ground outdoors (1: the physical estimate from the painted ground colour).");
config.register_floating_point("cave_gi_strength","Bounce light strength","Graphics",kDefaultCaveBounceStrength,0.f,8.f,.1f,false,"Brightness of the bounced light of lamps, campfires and lava (1, the default: the physical estimate from the painted ground colour).");
config.register_floating_point("sky_brightness","Sky brightness","Graphics",1.f,.5f,2.f,.05f,false,"Exposure of the owned sky and clouds.");
config.register_floating_point("sky_night_brightness","Night sky brightness","Graphics",1.5f,.5f,3.f,.1f,false,"Moonlight, stars and the deep blue night sky. 1 is the physical estimate; higher is brighter and more readable.");
config.register_floating_point("sky_cloud_shadows","Cloud shadow strength","Graphics",1.f,0.f,1.f,.05f,false,"How much moving cloud shadows dim the direct sun on the ground and objects. 0 disables them.");
config.register_floating_point("sky_cloud_shadow_speed","Cloud shadow speed","Graphics",8.f,0.f,60.f,1.f,false,"Calm drift speed of the cloud shadows on the ground in m/s of game time; the map wind adds up to 2 and storms 4. The distant sky drifts at the cloud wind speed.");
config.register_floating_point("sky_cloud_wind_speed","Cloud wind speed","Graphics",25.f,0.f,60.f,1.f,false,"Calm drift speed of the clouds and their ground shadows in m/s of game time; the map wind adds up to 6 and storms 10. Higher reads as moving sky at the game camera's distances, but shadows cross the view faster.");
config.register_floating_point("sky_cloud_motion","Cloud motion","Graphics",1.f,0.f,3.f,.05f,false,"Speed of cloud drift with the wind and of their changing shapes, on game time (pause freezes, fast speed accelerates). 0 holds the clouds still.");
config.register_integer("fog_preset","Fog","Graphics",2,0,2,1,false,"Keeps the game's map-edge fog and hourly fog colour. 0: faithful (the original fog distances with a smooth fade), 1: atmospheric (natural haze by time of day and weather, ground fog, sun glow), 2: full (volumetric: adds swamp mist and light shafts from the sun and lamps).");
config.register_integer("fog_volume_quality","Volumetric fog quality","Graphics",1,0,1,1,false,"Full fog preset only. 0: low (80x45x48 cells), 1: high (160x90x96 cells).");
config.register_integer("fog_debug_component","Fog debug: show only","Graphics",0,0,5,1,false,"0: all fog. 1: air haze, 2: height fog, 3: swamp/water mist and wisps, 4: sun light shafts, 5: lamp in-scatter.");
config.register_integer("sun_shafts","Sun shafts","Graphics",2,0,3,1,false,"Light shafts from the sun through foliage, fog and mist. 0: off, 1: screen-space rays (cheap, for weak PCs; streaks from sky and sunlit gaps), 2: volumetric (shadowed beams in the full fog preset; screen-space rays when the volume is off), 3: both. Strongest in morning fog, after rain and in swamp mist; faint at clear noon, none at night.");
config.register_floating_point("sun_shaft_strength","Sun shaft strength","Graphics",kSunShaftDefaultStrength,0.f,3.f,.05f,false,"Scales how strongly sun shadow carves the haze and how bright lit air glows toward the sun (and the screen-space rays).");
config.register_floating_point("fog_strength","Fog strength","Graphics",1.f,0.f,3.f,.05f,false,"One knob for the added haze, ground fog and mist (0 leaves only the map-edge fog).");
config.register_boolean("fog_test_dense","Fog test: dense morning fog","Graphics",false,false,"Debug: forces dense morning fog and wet ground at the current hour to confirm the fog pipeline, independent of time and weather.");
config.register_floating_point("fog_visibility","Fog visibility (m)","Graphics",5000.f,100.f,5000.f,25.f,false,"Atmospheric and full presets: distance at which clear-noon haze hides detail. Dusk, night, rain, wet ground and morning fog shorten it.");
config.register_floating_point("fog_density","Ground fog density","Graphics",.1f,0.f,4.f,.1f,false,"Scales the height fog over low ground and the swamp mist.");
config.register_floating_point("fog_morning_chance","Morning fog chance","Graphics",.35f,0.f,1.f,.05f,false,"Chance that a game day starts with ground fog between 3:00 and 10:00.");
config.register_floating_point("fog_after_rain_hours","Fog after rain (game hours)","Graphics",4.f,.25f,12.f,.25f,false,"How long thicker ground fog lingers after rain or snow stops.");
config.register_boolean("neighbour_maps","Neighbouring maps","Graphics",true,false,"Render-only saved-layout terrain and decoration. No simulation or interaction.");
config.register_boolean("load_reveal_hold","Hold the loading screen until the world is ready","Graphics",true,false,"After a map load, keep the loading picture on screen until the map, its neighbouring maps and its ground cover are prepared (at most 3 s), so nothing pops in after the loading screen. A single-player mission is paused meanwhile. Off shows the world at once.");
config.register_floating_point("neighbour_fog_range","Neighbour fog range","Graphics",1.f,.5f,4.f,.25f,false,"Over edges with a neighbouring map, fog starts 20 m inside the border, is light at the border and full 80 m into the neighbour; this scales both distances. Higher shows more of the neighbours and costs more; anything in full fog is not drawn. Capped by draw_distance. Changing it reloads the neighbours.");
config.register_integer("ground_cover_distance","Ground cover distance (m)","Effects",200,50,1000,25,false,"How far grass and other ground cover is drawn, independent of draw_distance. The far sparse level ends here.");
config.register_integer("ground_cover_shadows","Ground cover shadows","Effects",1,0,2,1,false,"Sun shadows of the procedural ground cover in the close shadow layers. 0: none. 1: large cover only (fallen logs and branches, dead bushes, cave wall roots). 2: everything except seagrass, beach wrack, snow and sand piles and the distant tuft meshes; the swaying grass then redraws the close shadow layers every frame. Needs sun shadows and ground cover. Critters always cast sun shadows when sun shadows are on.");
config.register_integer("effect_distance","Effect distance (m)","Effects",300,100,1000,25,false,"How far game effects are kept and drawn: campfires, torches, map particle sources, lights and lightning. Capped by draw_distance. The game itself keeps them only within 100 m.");
config.register_boolean("impostors","Crown impostors","Graphics",true,false,"Cut-out parts of neighbouring-map objects (tree crowns, bushes) draw a baked camera-facing impostor where they are smaller on screen than the impostor size, crossfading with a screen-door dither. Off draws every triangle.");config.register_floating_point("impostor_pixels","Crown impostor size (px)","Graphics",64.f,16.f,256.f,8.f,false,"On-screen size (pixels across the crown's bounding sphere) below which a crown draws its impostor. The crossfade runs from this size to one and a half times it.");config.register_boolean("impostor_debug_raw","Impostor texel diagnostic","Graphics",false,false,"With the impostor diagnostic view: each impostor pixel shows the texel it samples, unlit and unfogged.");config.register_boolean("impostor_debug_frames","Impostor view diagnostic","Graphics",false,false,"With the impostor diagnostic view: colour each impostor pixel by the baked view it samples (red: column, green: row).");config.register_integer("fur_debug","Fur layer diagnostic","Graphics",0,0,4,1,false,"Furry units, world and previews: 1 fur only (the part's own colour and wound layers hidden); 2 the colour records only (no fur, no wound layer); 3 the wound layer only, its alpha as grey; 4 identity colours: wound layer magenta, a furry part's own colour record blue, fur green. 0 off. Debug only: while on, figures and objects draw the material instance check programs (shader_split on), which alone carry this code, and units skip the depth prepass.");config.register_boolean("debug_no_fog","No fog (diagnostic)","Graphics",false,false,"Diagnostic captures: the world draws without any fog (game, map-edge and added fog).");config.register_floating_point("soft_particles","Soft particles","Graphics",.5f,0.f,2.f,.25f,false,"Fire, smoke, magic and dust fade out where they meet the ground, walls and objects instead of cutting a hard line, over this many particle radii. 0 turns it off.");config.register_boolean("soft_particles_debug","Soft particle diagnostic","Graphics",false,false,"Draws game particle effects in flat colours by class: soft particles red (faded) to green (unfaded); white hard particles, blue ribbons, magenta selection, cyan text, yellow ground prints; orange/purple are depth self-check failures.");config.register_boolean("impostor_debug","Crown impostor diagnostic view","Graphics",false,false,"Draws every crown impostor quad as a solid magenta square (placement check).");config.register_boolean("main_map_object_space","Main-map statics in object space","Graphics",true,false,"Trees and stones of the current map draw like neighbouring-map objects: static LOD levels, crown impostors and their own sun casters, built once per map on a worker. An object that animates, hides or changes draws its posed geometry for that frame. Off draws every main-map object posed.");config.register_boolean("caster_check","Main-map caster check (diagnostic)","Diagnostics",false,false,"D3D12 with GPU reports (EI_RENDERER_D3D12_HAZARDS): each static-layer redraw counts, on the GPU, the admitted main-map statics drawn into the sun maps by their object-space casters, by the posed caster store and by both (doubled); the host log names the counts.");config.register_boolean("lod","Static object LOD","Graphics",true,false,"Opaque parts of neighbouring-map objects draw a reduced triangle level where its largest deviation projects below the pixel limit, crossfading between levels with a screen-door dither. Off draws every triangle.");config.register_boolean("hosted_plants","Moss, vines and lichen","Graphics",true,false,"Moss, vines and lichen growing on stones, stumps, walls and trees. Off: none of them is uploaded or drawn.");config.register_integer("hosted_lod","Hosted plant LOD tier (diagnostic)","Graphics",0,0,3,1,false,"Moss, vines and lichen on stones, stumps and walls draw a reduced tier where its deviation projects below the static LOD pixel limit (lod_pixel_error), crossfading with a screen-door dither. Diagnostic: 1 forces every placement to full detail, 2 to the mid tier, 3 to the far tier (where a model has it). 0 automatic.");config.register_floating_point("lod_pixel_error","Static LOD pixel limit","Graphics",1.f,.25f,8.f,.25f,false,"Largest on-screen deviation (pixels at the current resolution) a reduced level may have where it is drawn. The crossfade to the next level runs from this limit to twice it.");config.register_integer("impostor_far_pixels","Far crown impostor size (px)","Graphics",160,0,512,8,false,"Beyond impostor_far_distance, crowns switch to their impostor below this on-screen size instead of impostor_pixels (0: off). Ramps in continuously with distance, so the crossfade never pops.");config.register_integer("impostor_far_distance","Far crown impostor distance (m)","Graphics",150,10,1000,10,false,"Distance where impostor_far_pixels is fully reached at 1600 output lines (ramps in from 3/4 of it); scales with output height, so smaller screens start impostors nearer.");config.register_integer("tree_detail_distance","Tree detail distance","Graphics",1,0,2,1,false,"How far crowns stay full 3D before their impostor: 0 Near (0.7 times the switch distances), 1 Normal, 2 Far (1.5 times). Scales impostor_pixels, impostor_far_pixels and impostor_far_distance together. In the graphics presets: Low Near, Medium to Ultra Normal; Far (about +1.1 ms GPU on a 3070 Laptop at 2560x1600, laptop run 080) only by choice.");config.register_floating_point("impostor_distance_scale","Crown impostor distance scale","Graphics",1.f,.25f,4.f,.05f,false,"Multiplies every crown impostor switch distance on top of tree_detail_distance (2: 3D crowns twice as far). Tuning slider; the player setting is tree_detail_distance.");config.register_integer("draw_distance","Draw distance (m)","Graphics",300,100,1000,25,false,"How far terrain, objects and neighbouring maps are drawn, in world metres (the unit of the game's FarClipDistance). The last tenth fades into the sky or fog colour; beyond it nothing is drawn, and nearer where fog already hides everything. Shadows inside the range keep their casters. Also caps how deep neighbouring maps load (neighbour_fog_range), so changing it can reload them.");
config.register_boolean("terrain_lod","Terrain LOD","Graphics",false,false,"Switch terrain detail by distance. Disable to keep the highest detail everywhere; this costs terrain geometry work.");
config.register_integer("terrain_quality","Terrain detail","Graphics",2,0,2,1,false,"Terrain 2.0 ground quality. 2: high (two warp octaves on material edges, fine edge noise, large-scale brightness variation). 1: medium (one warp octave, no fine noise or large-scale variation). 0: low, for weak cards (also hex detail from two cells instead of three, the two strongest materials only, edges decided by noise without the textures' heights, no wall-foot band).");
config.register_boolean("terrain_antitile","Terrain anti-repetition","Graphics",true,false,"Replace repeated tile detail with randomly turned and shifted reads on a hex lattice, plus faint large-scale brightness variation. Tile colours, paths and transitions are kept. Off is cheapest.");
config.register_boolean("terrain_cliffs","Terrain cliffs","Graphics",true,false,"Unwalkable slopes (past the game's 40 degree walk limit) show the map's own tile art read through world-space side projections, so it no longer stretches down cliffs. Colours, contrast and the map's tile transitions stay as authored; only the fine detail is re-read, fading in with the slope and cross-faded between neighbouring tiles, so no new edges appear. Walkable ground is unchanged. Only steep pixels pay extra texture reads. Replaces Terrain cliff projection on the current map while on. Off keeps the authored tile mapping. Needs Terrain material blend: with blend 0 cliffs stay off (this value is kept).");
config.register_floating_point("cliff_rock_amount","Rock amount","Cliff blend",.55f,0.f,1.f,.05f,false,"How much of a steep face shows the nearby ground or stone through its cover (grass, snow, sand). 0: none, 0.5: about half of a vertical face.");
config.register_floating_point("cliff_rock_softness","Rock edge softness","Cliff blend",.15f,.02f,.4f,.01f,false,"Width of the edge between rock and cover. Low: crisp ragged edges; high: soft, misty transitions.");
config.register_floating_point("cliff_rock_scale","Rock patch size","Cliff blend",1.f,.3f,4.f,.1f,false,"Size of the rock patches and wash marks (1: patches about 2 m, marks about 1.3 m across).");
config.register_floating_point("cliff_streak_length","Rock streak length","Cliff blend",2.f,1.f,5.f,.1f,false,"How much the rock marks stretch down the face (1: round patches, 2: twice as long as wide).");
config.register_floating_point("cliff_steep_start","Rock from slope (deg)","Cliff blend",40.f,20.f,75.f,1.f,false,"Slope angle where rock starts to show through the cover.");
config.register_floating_point("cliff_steep_full","Rock full at slope (deg)","Cliff blend",60.f,25.f,85.f,1.f,false,"Slope angle where the rock amount is reached in full.");
config.register_floating_point("cliff_snow_rock","Rock on snow","Cliff blend",.35f,0.f,1.f,.05f,false,"Rock amount on snow and ice faces, as a share of the normal rock amount (0: snow faces show no rock, 1: as much as other faces). Only a stone or ground material of that cliff can show.");
config.register_floating_point("cliff_face_warp","Face boundary wander","Cliff blend",.6f,0.f,2.f,.05f,false,"How far material boundaries wander with height on faces (tile units), so they do not run straight down a cliff.");
config.register_floating_point("cliff_rock_detail","Rock shape detail","Cliff blend",.6f,0.f,1.f,.05f,false,"How much the rock's own texture shapes the patch outlines: 0 smooth noise blobs; 1 bright stone pokes through the cover and dark cracks stay covered along the edges.");
config.register_boolean("neighbour_fog_skip","Skip blend under neighbour fog","Graphics",true,false,"Temporary A/B switch: neighbour terrain under nearly full seam fog keeps its tile art (no blend or cliffs).");
config.register_boolean("terrain_blend_neighbours","Terrain blend on neighbour maps","Graphics",true,false,"Temporary cost switch: neighbour maps' terrain reads their own tile grid and atlas for the material blend and cliffs. Off binds none, so neighbour terrain keeps its tile art.");
config.register_integer("terrain_blend","Terrain material blend","Graphics",1,0,1,1,false,"Tile transitions (a path meeting snow, grass meeting dirt) are redrawn from the plain textures of the materials at the map grid's corners, mixed by a smooth noisy field so edges meander naturally instead of repeating 2 m tile shapes; snow, sand and grass settle into the joints of stone. Corner materials come from the map editor's atlas profile shipped with the map, else its built-in table. Long and animated tiles and maps without tile families keep their art. Steep faces keep their painted cover (grass stays grass, snow stays snow) read through world-space side projections, and nearby ground or stone shows through in irregular patches and down-slope streaks where the face is steepest. Terrain 2.0 draws it; 0 also turns off the cliff texturing (an old saved 2 loads as 1).");
config.register_integer("terrain_tessellation","Terrain cliff tessellation","Graphics",1,0,1,1,false,"Round off the crests and feet of steep terrain with GPU tessellation (smooth Phong surface through the game's own heightfield and slope normals; no texture relief). Subdivision is fixed in world space, so it never changes as the camera moves, and flat or evenly sloped ground is never subdivided. Walkable ground moves at most 15 cm, steeper faces at most 70 cm; nothing moves at the map border. 0: off (standard terrain), 1: on (up to 8x). Shadow maps keep the untessellated terrain. Costs GPU time on every terrain triangle while on.");
config.register_integer("terrain2_cache","Terrain 2.0 material cache","Graphics",1,0,2,1,false,"With terrain 2.0 on. 0: every terrain 2.0 pixel computes its blend (the reference). 1: walkable ground without liquid over it reads a camera-centred cache of the finished blend (world-aligned texel grid at power-of-two densities, re-baked only where the view moves into new ground or the terrain inputs change); faces, rock, wall feet and liquid beds stay per pixel. 2: as 1 with cached pixels tinted by cache level. Switch 0/1 at the same camera for the comparison. Off frees the cache.");
config.register_integer("terrain2_cache_nearest","Terrain 2.0 cache: nearest visible ground","Performance",1,0,1,1,false,"0: the finest cache level is chosen for the ground level under the camera (ground rising toward the camera nearer than that computes its material per pixel). 1 (default): also for the nearest visible ground of the active map, so that ground reads the cache; costs one more fine level (window side squared times 6 bytes: about 48 MB at 2560x1600, 100 MB at 4K) while such ground is in view. The log line terrain2_cache names the levels and megabytes.");
config.register_integer("terrain2_cache_ahead","Terrain 2.0 cache: windows ahead of the camera","Performance",0,0,1,1,false,"0: every fine cache level is a square around the camera position. 1: the squares sit ahead of the camera along the view (half of the depth a level serves), so the ground at the sides of the screen stays inside them; no more memory, but turning the camera now moves the windows and bakes their new edges.");
config.register_integer("terrain2_cache_density","Terrain 2.0 cache size (%)","Graphics",100,25,200,25,false,"Window side of the fine cache levels as a share of the size at which no cached texel is coarser than the per-pixel reference on flat ground (3x the screen's pixels per metre at depth 1). Lower: less video memory, softer ground near each level's edge.");
config.register_integer("terrain2_cache_bake","Terrain 2.0 cache bake per frame (k texels)","Graphics",4096,256,65536,256,false,"Most cache texels baked in one frame (nearest levels last). While a level is incomplete the pixels that need it stay per pixel.");
config.register_floating_point("terrain2_cache_bias","Terrain 2.0 cache level bias","Diagnostics",0.f,-1.f,1.f,.25f,false,"Shifts the cache level each pixel reads: negative sharper (needs a larger window), positive softer.");
config.register_integer("terrain_face_cache","Terrain 2.0 face cache","Graphics",1,0,2,1,false,"With terrain 2.0 on. 0: faces (cliffs and slopes past the walk limit) compute their blend per pixel (the reference). 1: faces of the active map read a baked atlas of the finished blend (near-planar charts of the steep base triangles, pages baked on demand at power-of-two densities). 2: as 1 with cached face pixels tinted by level.");
config.register_integer("terrain_slim","Terrain slim pass","Graphics",2,0,3,1,false,"With the terrain 2.0 material cache on. 1: prepassed terrain draws first with a program without the per-pixel material (fewer registers); only the pixels the material and face caches miss draw again with the full program (the same image). 0: one pass with the full program (the reference). 2 (default): 1 with the D3D12 backend, 0 with D3D11 (where the two passes cost more than one). 3: as 1 with the second pass's pixels tinted red.");
config.register_integer("coverage_cuts","Water and sky coverage cuts","Performance",63,0,63,1,false,"Direct3D 12 (the same image; for in-run comparison), switches added together. 1: the sky and the cave exit glow draw after the world depth prepass, which rejects their covered pixels. 2: the water reflection trace clears only the two targets its passes do not overwrite whole. 4: neighbour-map liquid sectors outside the view are not queued, and with none in view their liquid pass and its reflection frame are skipped. 8: the sky's final colour below the horizon is the haze alone (half of the reflection probe). 16: the cloud march and resolve run only over the screen box above the horizon. 32: the cloud shadow field integrates each ground column once and gathers its penumbra from that field. 0: off, 63 (default): all.");
config.register_integer("terrain_slim_cuts","Terrain slim pass cuts","Performance",3,0,3,1,false,"Direct3D 12, with the terrain slim pass (the same image; for in-run comparison). 0: off. Bit 1: the slim pass draws programs compiled for plain terrain records (no crown impostor, foliage, unit or ground contact code). Bit 2 (with 1): a pixel the caches miss leaves the slim program before the lighting (the second pass shades it). 3 (default): both.");
config.register_integer("terrain_face_cache_bake","Terrain 2.0 face cache bake per frame (pages)","Graphics",64,1,4096,16,false,"Most 64 x 64 face cache pages baked in one frame (coarse levels first). Pages are requested by what the view's pixels read (GPU feedback, a few frames late); pixels whose page is not baked yet compute per pixel.");
config.register_floating_point("terrain_face_cache_bias","Terrain 2.0 face cache level bias","Diagnostics",0.f,-1.f,1.f,.25f,false,"Shifts the face cache level each pixel reads: negative sharper, positive softer.");
config.register_integer("terrain_face_cache_check","Terrain 2.0 face cache check","Diagnostics",0,0,4,1,false,"With the face cache on (1, untinted). Every 60 frames the charted faces in view are drawn offscreen and each pixel compares the cache read with the per-pixel blend at its own footprint; the counts (compared, misses, off by more than 2/255 and 8/255) go to the probe log as terrain_face_cache_check. 2 also writes the difference x8 (misses blue) as face-check-<frame>.bmp beside the F11 captures; 3 writes the cached colour there, 4 the live one.");
config.register_boolean("terrain_height_blend","Terrain transition sharpening","Graphics",true,false,"Bias soft tile transitions by the texture's own detail so the brighter material breaks through. Off is cheapest.");
config.register_integer("sky_weather_test","Sky weather test","Diagnostics",0,0,2,1,false,"Render-only sky test: 1 builds a rain storm in the owned sky (deck, rain shafts, lightning), 2 clears it, 0 follows the game's weather. The game's rain, fog and gameplay are untouched.");
config.register_integer("critter_labels","Critter labels","Diagnostics",0,0,1,1,false,"Label every live ambient critter and bird with species, state and distance to the nearest party member, including hidden critters outside party sight. Render only.");
config.register_integer("fur_mode","Fur and hair","Graphics",2,0,2,1,false,"0 off; 1 creature fur and fur armour (hair keeps its mesh); 2 full: creature fur, fur armour and hair strands grown from the scalp. Game time sways it; paused freezes it.");config.register_integer("fur_shells","Fur shells","Diagnostics",16,2,48,2,false,"Shell layers per furry triangle (fur mode 1).");config.register_integer("fur_length","Fur length (%)","Diagnostics",100,10,400,10,false,"Fur and hair strand length relative to the creature's coat.");config.register_integer("fur_density","Fur density (%)","Diagnostics",100,10,400,10,false,"Strand density of shells and strands.");config.register_integer("fur_quality","Fur quality","Diagnostics",1,0,1,1,false,"Fur prototype cost level. 0: low (half the strands, fewer strand segments, half the shells, no hair-on-body collision), 1: high.");config.register_integer("critter_showcase","Critter showcase","Diagnostics",0,0,3,1,false,"Debug: one of every critter species, a small flock of every bird and a vulture group (plus a vulture pair on its thermal) in labelled rows a few metres in front of the camera focus, at their normal in-game size (2: twice the size; 3: normal size drawn without the depth test, to tell buried from small). Render only: no simulation, spawn rules or budgets; the ambient critters pause while it is on.");
  set_test_initialization_stage("renderer-settings-registered");
 register_combat_ui_options(config);
}

// Startup values of the loaded options: renderer members and the game-side
// bridge switches (the host answers the bridge setters as no-ops).
void Renderer::apply_config_settings(){
 auto& config=debug_overlay_.config();
#ifndef EID3D11_EMBEDDED
if(!core_mode_&&!bridge_set_world_map_previews(this,configured_boolean(config,"world_map_previews",false)))
 (void)config.set_boolean("world_map_previews",false);
bridge_set_camera_pan_smoothing(configured_boolean(config,"camera_pan_smoothing",false));
#elif !defined(EID3D11_SHADER_EMBED)
 sync_embedded_world_map_preview_option();
#endif
camp_inventory_set_wide(configured_integer(config,"camp_ui_layout",1)!=0);
#ifdef EID3D11_EMBEDDED
// This existing restart-required option is latched once, after config load.
embedded_hud_layout().set_hud_width(configured_integer(config,"hud_width",2));
#endif
native_shadows_enabled_=configured_boolean(config,"shadows",true);
native_shadow_side_=1024u<<configured_integer(config,"shadow_cascade_quality",1);
native_point_shadow_side_=128u<<configured_integer(config,"point_shadow_quality",2);
native_cull_min_screen_=float(configured_integer(config,"cull_min_screen_permille",5))/1000.f;native_occlusion_enabled_=configured_boolean(config,"occlusion_culling",true);native_shadow_cull_min_texels_=float(configured_integer(config,"shadow_cull_min_texels",2));
native_close_shadow_cascades_=static_cast<std::uint8_t>(configured_integer(config,"shadow_close_cascades",3));
native_far_shadows_enabled_=configured_boolean(config,"shadow_far",true);
native_far_foliage_opaque_=configured_boolean(config,"shadow_far_foliage_opaque",true);
native_point_shadows_enabled_=configured_boolean(config,"point_shadows",true);
native_sun_filter_={configured_integer(config,"sun_shadow_correction",0)>=1?1.f:0.f,
 float(std::clamp(configured_integer(config,"sun_foliage_filter",kDefaultSunFoliageFilter),0,2))};
native_shadow_softness_=shadow_softness_constants(configured_integer(config,"shadow_softness",1),
 configured_integer(config,"shadow_sun_angle",15),configured_integer(config,"point_shadow_light_size",100));
// The common coverage/scheduler and the GPU consume the same settings,
// including when EIRebuild supplies the scene directly.
bridge_set_shadow_settings(native_shadow_side_,native_close_shadow_cascades_,
 native_far_shadows_enabled_,native_point_shadows_enabled_);
// Core mode, game process: the host applies the shadow switch at its
// BeginScene (begin_scene_renderer never runs here), so the flag the world
// capture reads (native_shadows_enabled) follows the same option here (#204).
if(core_mode_)native_shadows_enabled_=configured_boolean(config,"shadows",true)||configured_boolean(config,"rt_sun_shadows",false); // as begin_scene_renderer (traced sun shadows need the receivers)
neighbour_maps_enable(configured_boolean(config,"neighbour_maps",true));
bridge_set_terrain_lod_enabled(configured_boolean(config,"terrain_lod",false));
bridge_set_owned_sky(configured_integer(config,"sky_clouds",2)>0);
bridge_set_fire_burn(configured_boolean(config,"fire_burn",false));
}

void Renderer::detect_device_options() noexcept{
 D3D11_FEATURE_DATA_D3D11_OPTIONS options{};
 const bool offsets=SUCCEEDED(device_->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS,&options,sizeof(options)))&&
  options.ConstantBufferOffsetting&&options.MapNoOverwriteOnDynamicConstantBuffer;
 refresh_constant_ring_context();constant_ring_supported_=offsets&&context1_;
 structured_ring_supported_=options.MapNoOverwriteOnDynamicBufferSRV!=FALSE;
 char detail[256];std::snprintf(detail,sizeof(detail),"ring=%d context1=%d offsetting=%d no_overwrite_cb=%d no_overwrite_srv=%d%s%s",
  constant_ring_supported_?1:0,context1_?1:0,int(options.ConstantBufferOffsetting),int(options.MapNoOverwriteOnDynamicConstantBuffer),
  int(options.MapNoOverwriteOnDynamicBufferSRV),
  constant_ring_supported_?"":"; per-draw constants keep one DISCARD per write",
  structured_ring_supported_?"":"; instance and pose uploads keep one DISCARD per flush");
 bridge_report_event("constant_ring",detail);
}

// x64 host core (Host/core_host.cpp): the host's one device, adopted idle.
// The host presenter owns the window and present, so no swap chain; targets,
// shaders, jobs and settings wait for the record stream (S3).
HRESULT Renderer::adopt_host_device(ID3D11Device* device,ID3D11DeviceContext* context,UINT w,UINT h) noexcept{
 if(!device||!context)return E_INVALIDARG;
 if(device_)return device_.Get()==device?S_OK:E_FAIL;
 output_width_=std::max(1u,w);output_height_=std::max(1u,h);hwnd_=nullptr;
 device_=device;context_=context;feature_level_=device->GetFeatureLevel();
 detect_device_options();
 report_adapter();
 return S_OK;
}

// x64 host, renderer_backend=d3d12 (M1a): the D3D12 executor owns the swap
// chain, targets and pipelines; the adopted D3D11 device creates nothing
// and receives no work (it only answers adapter queries).
#ifndef EID3D11_EMBEDDED
HRESULT Renderer::ensure_host_d3d12(HWND window,UINT w,UINT h){
 core_host_=true;
 output_width_=std::max(1u,w);output_height_=std::max(1u,h);hwnd_=window;
 debug_overlay_set_running_renderer("64-bit render host, Direct3D 12",true);debug_overlay_set_running_d3d12(true);
 // DLSS starts asynchronously on the executor's device (core_host.cpp)
 // and FSR 3.1 through the host's FidelityFX upscaler (D3D12Frame::fsr_upscale).
 {x64remote::DlssCaps caps{};
  if(!x64remote::dlss_caps(caps)){caps.reason[sizeof(caps.reason)-1]=0;set_dlss_unavailable_reason(caps.reason[0]?caps.reason:"not available");}
  else set_dlss_unavailable_reason(nullptr);
  dlss_initializing_=caps.initializing;}
 set_fsr_unavailable_reason(d3d12_->fsr_upscale_unavailable());
 // The executor's adapter (the device that owns every GPU resource here):
 // the VRAM budget, the point shadow pool's pressure check and the telemetry
 // query its local segment.
 if(IDXGIAdapter* adapter=d3d12_->adapter())adapter->QueryInterface(IID_PPV_ARGS(adapter3_.ReleaseAndGetAddressOf()));
 register_config_options();
 apply_config_settings();
 // The render scale and the upscaler modes follow the options from the first
 // frame on (apply_upscaler_mode_d3d12 between frames, as apply_render_scale).
 render_scale_percent_=std::clamp(configured_integer(debug_overlay_.config(),"render_scale",100),kRenderScaleMinimum,100);
 dlss_mode_=configured_dlss_mode();fsr_mode_=configured_fsr_mode();set_world_size();
 scene_composited_=true;
 frame_statistics_.reset();
 system_telemetry_snapshot_={};
 system_telemetry_active_=false;
 system_telemetry_.start(adapter3_.Get());
 // The profiler (F10 performance panel, F11 and harness captures) times
 // passes with the executor's GPU timestamps; off unless profiling is on.
 profiler_.set_timestamps(&d3d12_->profile_timestamps());
 if(FAILED(profiler_.initialize(device_.Get(),context_.Get())))bridge_report_event("d3d12_profiler","the profiler did not start");
 // The neighbour stream has no device here: it creates its pages and textures
 // through the executor's preparer; adoption only records their copies.
 neighbour_maps_set_d3d12(d3d12_->preparer());
 // The native queue's resources on the executor (interface meshes: the HUD).
 if(FAILED(native_d3d12_ensure()))bridge_report_event("d3d12_native","the native programs or the first geometry page failed; interface meshes are omitted");
 // The generated liquid maps on a worker (CPU levels; the first water frame uploads them).
 start_liquid_maps_job();
 return S_OK;
}

// x64 host, before ensure_host (renderer.h). Same steps as the host's first
// config command took (the overlay, then the settings migration), earlier:
// the start's shader preparation (D3D11 prepare_shaders, D3D12 below) shows
// its progress window only with an initialized overlay.
bool Renderer::host_start_overlay(HWND window,const wchar_t* ini_path) noexcept try{
 if(debug_overlay_.initialized())return true;
 if(!window||!ini_path||!*ini_path||!device_||!context_)return false;
 register_config_options();
 if(d3d12_)debug_overlay_.set_renderer(&d3d12_->overlay_renderer());
 if(FAILED(debug_overlay_.initialize_host(window,device_.Get(),context_.Get(),ini_path)))return false;
 migrate_settings(debug_overlay_.config(),ini_path);
 return true;
}catch(...){return false;}

// x64 host, D3D12 executor: the start's pipelines are built before the first
// real frame. A cold library or driver cache takes many seconds (28 s on a
// tester's first start). Count from host entry, not from this late polling
// loop: executor creation/NGX and start_warming itself can do most of the
// work. The host window thread paints a plain message until this GPU UI is
// ready, including during device creation and the pipeline library save.
// Its counter is published by the pipeline queue even while this thread
// is still inside executor/NGX/overlay setup.
void Renderer::host_warm_d3d12(HWND window,ULONGLONG startup_started,void (*after_present)() noexcept) noexcept try{
 if(!d3d12_)return;
 const std::int64_t begin=perf_now();
 // The recorded pipelines these settings use (#325); a feature that is off
 // is prepared when a setting turns it on, never at start.
 d3d12_pipeline_features_=d3d12_features_for_settings(debug_overlay_.config(),!d3d12_->raytracing_unavailable());
 // The menu's own first: the start waits only for the records tagged as drawn
 // in the main menu; the rest is queued after this wait and builds behind the
 // menu, and the first world load's hold waits for what is left (#325).
 d3d12_->set_menu_scene(true);
 (void)d3d12_->request_pipelines(d3d12_pipeline_features_,true);
 bool shown=false;std::size_t done=0,total=0;
 while(d3d12_->start_warming(done,total)){
  // If the first poll completes warming, leave the GDI counter up until
  // the first game frame. A completion-only ImGui frame auto-sizes its
  // window invisibly and would replace the message with a black frame.
  if(debug_overlay_.initialized()&&GetTickCount64()-startup_started>=2000){
   const float background[4]={.035f,.035f,.04f,1.f};
   d3d12_->clear(background);
   const bool visible=debug_overlay_.render_shader_preparation(done,total);
   const HRESULT hr=d3d12_->present(0);  // a failure is the executor's, named by its first frame
   if(visible&&hr==S_OK&&!IsIconic(GetParent(window))){
    if(after_present){after_present();after_present=nullptr;}
    if(!shown){
     shown=true;
     char detail[96];std::snprintf(detail,sizeof(detail),"done=%zu total=%zu host_ms=%llu",done,total,
      static_cast<unsigned long long>(GetTickCount64()-startup_started));
     bridge_report_event("start_pipelines_window",detail);
    }
   }
  }
  Sleep(16);
 }
 // The rest builds behind the menu. Not on the first run: its graphics
 // check times menu frames and may step the preset down, so the rest waits
 // for the first world load's hold, with the chosen preset's features.
 if(!first_run_settings_due(debug_overlay_.config().first_run_done(),renderer_developer_mode()))(void)d3d12_->request_pipelines(d3d12_pipeline_features_,false);
 else pipeline_rest_pending_=true;
 const double ms=1000.0*static_cast<double>(perf_now()-begin)/static_cast<double>(perf_frequency());
 char text[128];std::snprintf(text,sizeof(text),"pipelines=%zu ms=%.0f host_ms=%llu window=%s",total,ms,
  static_cast<unsigned long long>(GetTickCount64()-startup_started),shown?"shown":"not_shown");
 bridge_report_event("start_pipelines",text);
}catch(...){}

// x64 host, core mode (Host/core_host.cpp), after adopt_host_device: the
// frame resources ensure() creates, presenting into `window` (the host's
// child of the game window). The options start at the game's ini
// (host_start_overlay) or, without it, at their registered defaults, and
// take the game's values from its first config command, which precedes
// every frame.
// Not yet in core mode (listed): DLSS (the host's NGX feature is the
// call-stream replay's), NVAPI's shader disk cache switch, the debug overlay.
#endif // !EID3D11_EMBEDDED
HRESULT Renderer::create_targets(){
 reset_water_reflections();
 rtv_.Reset();depth_.Reset();dsv_.Reset();depth_srv_.Reset();debug_stencil_srv_.Reset();release_ambient_occlusion_targets();HRESULT hr;
 world_dsv_.Reset();interface_dsv_.Reset();interface_depth_.Reset();
 if(swap_){ComPtr<ID3D11Texture2D> back;if(FAILED(hr=swap_->GetBuffer(0,IID_PPV_ARGS(&back))))return hr;if(FAILED(hr=device_->CreateRenderTargetView(back.Get(),nullptr,&rtv_)))return hr;}
 else{D3D11_TEXTURE2D_DESC td{};td.Width=output_width_;td.Height=output_height_;td.MipLevels=td.ArraySize=td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_B8G8R8A8_UNORM;td.BindFlags=D3D11_BIND_RENDER_TARGET;ComPtr<ID3D11Texture2D> color;if(FAILED(hr=device_->CreateTexture2D(&td,nullptr,&color)))return hr;if(FAILED(hr=device_->CreateRenderTargetView(color.Get(),nullptr,&rtv_)))return hr;}
 D3D11_TEXTURE2D_DESC zd{};zd.Width=width_;zd.Height=height_;zd.MipLevels=zd.ArraySize=zd.SampleDesc.Count=1;zd.Format=DXGI_FORMAT_R24G8_TYPELESS;zd.BindFlags=D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE;if(FAILED(hr=device_->CreateTexture2D(&zd,nullptr,&depth_)))return hr;D3D11_DEPTH_STENCIL_VIEW_DESC dvd{};dvd.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;dvd.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;if(FAILED(hr=device_->CreateDepthStencilView(depth_.Get(),&dvd,&dsv_)))return hr;D3D11_SHADER_RESOURCE_VIEW_DESC svd{};svd.Format=DXGI_FORMAT_R24_UNORM_X8_TYPELESS;svd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;svd.Texture2D.MipLevels=1;if(FAILED(hr=device_->CreateShaderResourceView(depth_.Get(),&svd,&depth_srv_)))return hr;
 world_dsv_=dsv_;interface_dsv_=dsv_;
 if(width_!=output_width_||height_!=output_height_){
  zd.Width=output_width_;zd.Height=output_height_;zd.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;zd.BindFlags=D3D11_BIND_DEPTH_STENCIL;
  interface_dsv_.Reset();
  if(FAILED(hr=device_->CreateTexture2D(&zd,nullptr,&interface_depth_)))return hr;
  if(FAILED(hr=device_->CreateDepthStencilView(interface_depth_.Get(),nullptr,&interface_dsv_)))return hr;
 }
 if(FAILED(create_shadow_history_targets()))
  OutputDebugStringA("EID3D11 directional shadow history unavailable; rendering current owned shadows\n");
 if(FAILED(create_prepass_targets()))
  bridge_append_probe_event("depth_prepass_omitted","depth prepass targets could not be created; world flushes draw without the prepass and shadow mask");
 backbuffer_rtv_=rtv_;
 dlss_reset_=true;dlss_previous_valid_=false;
 if(FAILED(hr=scene_composite_.create(device_.Get(),width_,height_,output_width_,output_height_,temporal_upscaler()))){
  OutputDebugStringA("EID3D11 scene/SMAA resources unavailable; renderer initialization failed\n");rtv_.Reset();return hr;
 }
 rtv_=scene_composite_.target();scene_composited_=false;scene_updated_=false;
 const float black[4]{};context_->ClearRenderTargetView(rtv_.Get(),black);
 context_->OMSetRenderTargets(1,rtv_.GetAddressOf(),dsv_.Get());return S_OK;
}


// Render scale: the world targets follow the option between frames; the
// swap chain and the interface keep the output size.
void Renderer::apply_render_scale(){
 if(!device_||!context_)return;
 const int percent=std::clamp(configured_integer(debug_overlay_.config(),"render_scale",100),kRenderScaleMinimum,100);
 const int dlss=configured_dlss_mode(),fsr=configured_fsr_mode();
 const bool had_upscaler=temporal_upscaler();
 const bool suspension=update_upscaler_suspension(dlss>=0||fsr>=0);
 if(!suspension&&percent==render_scale_percent_&&dlss==dlss_mode_&&fsr==fsr_mode_)return;
 render_scale_percent_=percent;
 const UINT old_width=width_,old_height=height_;const bool mode_changed=suspension||dlss!=dlss_mode_||fsr!=fsr_mode_;const int old_mode=dlss_mode_;
 dlss_mode_=dlss;fsr_mode_=fsr;set_world_size();
 // FSR off: its images go (an FSR frame rebuilds them at its sizes).
 if(fsr_mode_<0)fsr_.reset();
 if(dlss_mode_<0)upscale_sharpen_.reset();
 if(!mode_changed&&width_==old_width&&height_==old_height)return;
 context_->OMSetRenderTargets(0,nullptr,nullptr);
 ID3D11ShaderResourceView* empty_history[2]{};context_->PSSetShaderResources(20,2,empty_history);
 if(FAILED(create_targets()))OutputDebugStringA("EID3D11 render-scale targets unavailable; world omitted\n");
 // Every temporal upscaler off: the targets above were rebuilt without the
 // upscaler-only images (output, motion, object motion and depth); the
 // previous-vertex copy goes too. DLSS off also releases the host's NGX feature.
 if(had_upscaler&&!temporal_upscaler()){
  context_->VSSetShaderResources(46,1,&empty_history[0]);
  dlss_previous_world_.Reset();dlss_previous_world_srv_.Reset();dlss_previous_world_uav_.Reset();dlss_motion_constants_.Reset();
 }
#if defined(EID3D11_EMBEDDED) && !defined(EID3D11_SHADER_EMBED)
 if(old_mode>=0&&dlss_mode_<0)dlss_backend_.release_feature();
#else
 if(old_mode>=0&&dlss_mode_<0)x64remote::dlss_release();
#endif
}

// DLSS (dlss_frame.h): the NGX mode of the upscaler method and quality, -1
// when off or when the x64 host cannot run it (the method list shows why).
int Renderer::configured_dlss_mode() noexcept {
#if !defined(EID3D11_EMBEDDED) || defined(EID3D11_SHADER_EMBED)
 if(dlss_unavailable_reason())return -1;
#endif
 // Embedded retains selected DLSS even when unavailable: refuse the world
 // frame instead of interpreting the selection as Off or ordinary upscale.
 const auto& config=debug_overlay_.config();
 return dlss_ngx_mode(configured_integer(config,"upscaler",kUpscalerOff),configured_integer(config,"upscaler_quality",0));
}

// FSR 2 (fsr2_upscaler.h): the FSR mode of the same options, -1 when off,
// another upscaler, or when this device cannot run it (reason on the list).
int Renderer::configured_fsr_mode() noexcept {
 if(fsr_unavailable_reason())return -1;
 const auto& config=debug_overlay_.config();
 return fsr_mode_of(configured_integer(config,"upscaler",kUpscalerOff),configured_integer(config,"upscaler_quality",0));
}

// The world size: the FSR ratio of its mode, the host's DLSS render size for
// its mode, else the output scaled by the render scale option.
void Renderer::set_world_size() noexcept {
 // The global map screen (update_upscaler_suspension): the output size, no upscaler.
 if(upscaler_suspended_){width_=output_width_;height_=output_height_;return;}
 if(fsr_mode_>=0){width_=fsr_render_extent(output_width_,fsr_mode_);height_=fsr_render_extent(output_height_,fsr_mode_);return;}
 if(dlss_mode_>=0){
#if defined(EID3D11_EMBEDDED) && !defined(EID3D11_SHADER_EMBED)
  if(const char* reason=dlss_unavailable_reason()){
   bridge_report_event("dlss_frame_refused",reason);
   // Allocation sizes keep UI usable, never a substitute world result.
   width_=output_width_;height_=output_height_;return;
  }
#endif
  UINT w=0,h=0;
#if defined(EID3D11_EMBEDDED) && !defined(EID3D11_SHADER_EMBED)
  const bool sized=d3d12_?embedded_d3d12_.dlss_size(output_width_,output_height_,dlss_mode_,w,h,ray_reconstruction_on()):
   dlss_backend_.optimal(output_width_,output_height_,static_cast<DlssQuality>(dlss_mode_),w,h);
#else
  const bool sized=x64remote::dlss_render_size(output_width_,output_height_,dlss_mode_,w,h,ray_reconstruction_on());
#endif
  if(sized&&w&&h&&w<=output_width_&&h<=output_height_){
   width_=w;height_=h;return;
  }
  char reason[96];std::snprintf(reason,sizeof(reason),"DLSS has no render size for %ux%u",output_width_,output_height_);
#if defined(EID3D11_EMBEDDED) && !defined(EID3D11_SHADER_EMBED)
  const char* error=d3d12_?reason:dlss_backend_.last_error();
  set_dlss_unavailable_reason(error[0]?error:reason);
  bridge_report_event("dlss_frame_refused",dlss_unavailable_reason());
  width_=output_width_;height_=output_height_;return;
#else
  set_dlss_unavailable_reason(reason);bridge_append_probe_event("dlss_omitted",reason);dlss_mode_=-1;
#endif
 }
 width_=render_scale_extent(output_width_,render_scale_percent_);height_=render_scale_extent(output_height_,render_scale_percent_);
}

// Settings-specialised pixel programs (renderer.h native_spec_*).
// The general list's pixel programs in order: the object nativePS, the 12
// variants of create_native_pipeline's table, the two mask passes.
using NativePixelMember=ComPtr<ID3D11PixelShader> Renderer::*;
const NativePixelMember* Renderer::native_specialised_members() noexcept{
 static constexpr NativePixelMember members[]={
 &Renderer::native_ps_,
 &Renderer::native_terrain_ps_,&Renderer::native_snow_ps_,&Renderer::native_low_ps_,&Renderer::native_low_unlit_ps_,
 
 &Renderer::native_terrain_flat_ps_,&Renderer::native_snow_flat_ps_,
 &Renderer::native_masked_ps_,&Renderer::native_masked_low_ps_,
 &Renderer::native_masked_terrain_ps_,&Renderer::native_masked_snow_ps_,
 &Renderer::native_masked_terrain_flat_ps_,&Renderer::native_masked_snow_flat_ps_,
 &Renderer::native_shadow_mask_ps_,&Renderer::native_low_shadow_mask_ps_,
 &Renderer::native_masked_terrain_slim_ps_,&Renderer::native_masked_snow_slim_ps_,
 &Renderer::native_masked_terrain_slim_flat_ps_,&Renderer::native_masked_snow_slim_flat_ps_,
 &Renderer::native_gltf_ps_,&Renderer::native_gltf_low_ps_,
 &Renderer::native_gltf_masked_ps_,&Renderer::native_gltf_masked_low_ps_};
 static_assert(std::size(members)==kNativeSpecialisedMembers,"one specialised slot per member");
 return members;
}
#else
} // namespace
#endif
namespace {
// The key's defines (renderer.h key bits).
constexpr const char* kNativeSpecDefines[]={"NATIVE_OFF_ANTITILE","NATIVE_OFF_HEIGHT_BLEND","NATIVE_OFF_CONTACT","NATIVE_OFF_CLIFFS",
 "NATIVE_OFF_BLEND","NATIVE_OFF_POINT_SHADOWS","NATIVE_OFF_SUN_SHADOWS"};
// Bits 9-12 (bits 7 and 8 are the fog quality).
// Bit 12: material quality Off (the S1/S2/S3 material code compiled out;
// D3D11 only: the D3D12 programs test the level uniformly).
// Bit 13: a second terrain 2.0 pool exists (terrain2.hlsl T2_POOL; D3D11 only).
// Bit 14: terrain_quality Low (terrain2.hlsl T2_LOW; D3D11 only: the D3D12
// programs test it uniformly).
constexpr const char* kNativeSpecDefinesHigh[]={"NATIVE_DEBUG_COVERAGE","NATIVE_OFF_POINT_SOFT","NATIVE_FAST_COMBINER","NATIVE_OFF_MATERIALS",
 "NATIVE_TERRAIN2_POOLS","NATIVE_TERRAIN_LOW"};
#if !defined(EID3D11_SHADER_EMBED)
// The disk cache next to the DLL (eid3d11_shader_cache\<program key>.dxbc).
std::wstring native_spec_cache_path(const ShaderKey& key){
 std::wstring path=renderer_config_path();
 const auto slash=path.find_last_of(L"\\/");
 path=(slash==std::wstring::npos?std::wstring():path.substr(0,slash+1))+L"eid3d11_shader_cache\\";
 return path+native_spec_cache_file_name(key);
}
bool native_spec_cache_read(const std::wstring& path,std::vector<char>& code){
 FILE* file=nullptr;if(_wfopen_s(&file,path.c_str(),L"rb")||!file)return false;
 std::fseek(file,0,SEEK_END);const long size=std::ftell(file);std::fseek(file,0,SEEK_SET);
 bool ok=size>32&&size<(64l<<20);
 if(ok){code.resize(static_cast<std::size_t>(size));ok=std::fread(code.data(),1,code.size(),file)==code.size();}
 std::fclose(file);
 // A DXBC container whose recorded size is the file's.
 std::uint32_t recorded=0;if(ok)std::memcpy(&recorded,code.data()+24,4);
 return ok&&std::memcmp(code.data(),"DXBC",4)==0&&recorded==code.size();
}
#endif
}  // namespace

std::wstring native_spec_cache_file_name(const ShaderKey& key){
 wchar_t name[48];swprintf_s(name,L"%016llx%016llx.dxbc",static_cast<unsigned long long>(key.a),static_cast<unsigned long long>(key.b));
 return name;
}

bool native_specialised_programs(std::uint32_t key,NativeSpecialisedPrograms& out){
 out.programs.clear();out.macros.clear();
 std::vector<ShaderProgram> general;append_native_shader_programs(general);
 for(const auto& program:general){
  if(std::strcmp(program.entry,"nativePS")!=0&&std::strcmp(program.entry,"nativeShadowMaskPS")!=0)continue;
  bool check=false;for(const D3D_SHADER_MACRO* define=program.defines;define&&define->Name;++define)check|=std::strcmp(define->Name,"NATIVE_DOMAIN_CHECK")==0;
  if(check)continue; // F10 shader_split_check: general table only
  if(program.defines&&std::strcmp(program.defines[0].Name,"NATIVE_ASTRAL")==0)continue; // astral rift: general programs only
  std::vector<D3D_SHADER_MACRO> macros;
  for(const D3D_SHADER_MACRO* define=program.defines;define&&define->Name;++define)macros.push_back(*define);
  for(unsigned bit=0;bit<std::size(kNativeSpecDefines);++bit)if(key&(1u<<bit))macros.push_back({kNativeSpecDefines[bit],"1"});
  if(key&128u)macros.push_back({"NATIVE_FOG_VOLUME_QUALITY","0"});else if(key&256u)macros.push_back({"NATIVE_FOG_VOLUME_QUALITY","1"});
  for(unsigned bit=0;bit<std::size(kNativeSpecDefinesHigh);++bit)if(key&(512u<<bit))macros.push_back({kNativeSpecDefinesHigh[bit],"1"});
  macros.push_back({nullptr,nullptr});
  out.macros.push_back(std::move(macros));out.programs.push_back(program);
 }
 for(std::size_t n=0;n<out.programs.size();++n)out.programs[n].defines=out.macros[n].data();
 return out.programs.size()==kNativeSpecialisedPrograms+4;
}
namespace {

// The key a preset gives in play (map loaded, valid camera and sun cascades;
// renderer.h bits from the same rules as the uploaded values), and its
// point-shadow sibling. Key 0 (the general programs) is left out.
// The key settings give in play (value(name, fallback) reads one setting).
template<class Value> std::uint32_t native_play_specialisation_key(const Value& value){
 std::uint32_t key=0;
 const float terrain_quality=value("terrain_quality",2.f);
 if(terrain_quality<2)key|=1u|2u; // terrain_material_switches: anti-tiling and height blend
 if(terrain_quality<1)key|=16384u; // NATIVE_TERRAIN_LOW
 if(value("contact_blend",.7f)<=0)key|=4u;
 const bool blend=value("terrain_blend",1.f)>0;
 if(!blend||value("terrain_cliffs",1.f)<=0)key|=8u; // configured_terrain_cliffs
 if(!blend)key|=16u;
 if(value("point_shadows",1.f)<=0)key|=32u;
 if(value("shadows",1.f)<=0&&value("rt_sun_shadows",0.f)<=0)key|=64u; // traced sun shadows need the receivers
 const float fog=value("fog_preset",2.f);
 if(fog<1)key|=128u;else if(fog<2)key|=256u;
 if(value("shadow_softness",1.f)<=0)key|=1024u; // shadowBias[5].z
 if(value("material_quality",2.f)<=0)key|=4096u; // nativeMaterialLevel 0: no material code
 return key;
}
}  // namespace

std::vector<std::uint32_t> native_shipped_specialisation_keys(){
 std::size_t count=0;const PlayerGraphicsSetting* settings=player_graphics_settings(count);
 std::vector<std::uint32_t> keys;
 for(int preset=0;preset<kGraphicsPresetCount;++preset){
  const std::uint32_t key=native_play_specialisation_key([&](const char* name,float fallback){
   for(std::size_t n=0;n<count;++n)if(std::strcmp(settings[n].key,name)==0&&settings[n].in_preset)return settings[n].values[preset];
   return fallback;
  });
  // The main menu's backdrop uploads none of the terrain bits (0-4, 14), so
  // presets that switch terrain features off draw the menu under a key of
  // their own (Lowest 0x54ff in play, 0x14e0 in the menu: laptop jobs
  // 108-111). Without its set the menu draws the general programs, about
  // 1.5 s of driver compile per pipeline on an Intel Iris Xe (#304).
  const std::uint32_t menu=key&~(31u|16384u);
  for(const std::uint32_t root:{key,menu})
  for(const std::uint32_t base:{root,root|2048u})
   for(const std::uint32_t k:{base,base^32u,base|4u,(base|4u)^32u})
    if(k&&std::find(keys.begin(),keys.end(),k)==keys.end())keys.push_back(k);
  // Material quality Off (an F10/player choice outside the presets): the
  // preset's own programs without the material code, so Off draws exactly
  // the image (and cost) of the build before material classes.
  for(const std::uint32_t k:{key|4096u,key|6144u})
   if(std::find(keys.begin(),keys.end(),k)==keys.end())keys.push_back(k);
 }
 // The pools set: the general programs with NATIVE_TERRAIN2_POOLS, exact for
 // every key with a second terrain 2.0 pool until that key's own set is in
 // (native_terrain2_pools_set). One set, whatever the presets.
 keys.push_back(8192u);
 return keys;
}

// One work queue of programs across every set being built, drained by
// workers sized by the machine (one core left for the render thread). The
// workers exit when the queue is empty, so idle sessions hold no thread
// stacks; the render thread joins them once none runs (reap). A set started
// first-in-line (the current key's own) goes to the front of the queue.
#if !defined(EID3D11_SHADER_EMBED)
struct Renderer::NativeSpecialisationPool{
 ComPtr<ID3D11Device> device;
 std::mutex mutex;
 std::deque<std::pair<std::shared_ptr<NativeSpecialisationJob>,std::size_t>> queue;
 std::vector<std::thread> workers; // running and exited, joined by reap
 unsigned running=0,target=1;
 std::atomic<bool> cancel{false};
 static void build(ID3D11Device* device,NativeSpecialisationJob& job,std::size_t index,const std::atomic<bool>* cancel=nullptr) noexcept{
  try{
   // The prebuilt file (shader_embed --preset-cache). The 32-bit renderer
   // never compiles: a D3DCompile of these programs holds 350-630 MB of the
   // 32-bit address space; an in-game compile (#272, fcb11f3a) made the
   // harness's screenshot and other allocations fail within seconds. The x64
   // host compiles a key that was not prebuilt (compile_program).
   std::vector<char> code;
   const void* embedded=nullptr;std::size_t embedded_size=0;
   if(index<job.keys.size()&&shader_cache_detail::find(job.keys[index],embedded,embedded_size)) {
    ++job.cached;
    if(FAILED(device->CreatePixelShader(embedded,embedded_size,nullptr,&job.shaders[index])))++job.failed;
   } else {
   if(native_spec_cache_read(job.files[index],code))++job.cached;
#if defined(_WIN64)
   else if(index<job.compile.programs.size()&&compile_program(job,index,code,cancel))++job.compiled;
#endif
    if(code.empty()||FAILED(device->CreatePixelShader(code.data(),code.size(),nullptr,&job.shaders[index])))++job.failed;
   }
  }catch(...){++job.failed;}
  ++job.finished;
 }
#if defined(_WIN64)
 // x64 host: one program compiled and written to the cache (the 32-bit
 // renderer reads it in later sessions). Each compile can hold 350-630 MB,
 // so at most two run at once (one below 16 GB of physical memory or 4
 // cores), and one starts only while about 2 GB stay available for each
 // compile running with it, checked again before every start (testers'
 // PCs have 8-16 GB), at idle thread priority. The compile runs on its own thread, which owns what it reads
 // and writes the cache file itself: on cancel (the pool released, the
 // host shutting down) the worker stops waiting at once and the compile
 // finishes on its own (its file still serves the next session), so
 // shutdown never waits for a D3DCompile.
 struct CompileSlots{std::mutex mutex;std::condition_variable freed;unsigned running=0;};
 // Never destroyed: a compile thread may still run while the host's static
 // destructors do (exit() before ExitProcess).
 static CompileSlots& compile_slots(){static CompileSlots* slots=new CompileSlots;return *slots;}
 static bool compile_program(NativeSpecialisationJob& job,std::size_t index,std::vector<char>& code,const std::atomic<bool>* cancel) noexcept{
  try{
   const auto cancelled=[&]{return cancel&&cancel->load();};
   auto& slots=compile_slots();
   // Whether one more compile may start next to `running` ones (a 16 GB
   // PC reports a little under 16 GB, hence the 15 GB line).
   const auto may_start=[](unsigned running){
    MEMORYSTATUSEX s{};s.dwLength=sizeof(s);
    if(!GlobalMemoryStatusEx(&s))return running==0;
    const unsigned cap=s.ullTotalPhys<(15ull<<30)||std::thread::hardware_concurrency()<4?1u:2u;
    return running<cap&&s.ullAvailPhys>=(2ull<<30)*(running+1);};
   {
    std::unique_lock<std::mutex> lock(slots.mutex);
    while(!may_start(slots.running)){if(cancelled())return false;slots.freed.wait_for(lock,std::chrono::milliseconds(100));}
    if(cancelled())return false;
    ++slots.running;
   }
   // What the compile thread owns: copies of the program's text, names,
   // defines and file name (exit() destroys the static shader source while
   // a detached compile may still read it).
   struct Compile{
    std::mutex mutex;std::condition_variable done;bool finished=false;
    ShaderProgram program;std::string source,name,entry,profile;
    std::deque<std::string> strings;std::vector<D3D_SHADER_MACRO> defines;std::wstring file;
    std::vector<char> code;unsigned ms=0;
   };
   auto work=std::make_shared<Compile>();
   const ShaderProgram& from=job.compile.programs[index];
   work->file=job.files[index];
   work->source.assign(from.source.data(),from.source.size());
   if(from.name)work->name=from.name;
   work->entry=from.entry;work->profile=from.profile;
   for(const D3D_SHADER_MACRO* d=from.defines;d&&d->Name;++d){
    work->strings.emplace_back(d->Name);const char* name=work->strings.back().c_str();
    work->strings.emplace_back(d->Definition?d->Definition:"");
    work->defines.push_back({name,work->strings.back().c_str()});
   }
   work->defines.push_back({nullptr,nullptr});
   work->program=from;work->program.source=work->source;
   work->program.name=from.name?work->name.c_str():nullptr;
   work->program.entry=work->entry.c_str();work->program.profile=work->profile.c_str();
   work->program.defines=work->defines.data();
   try{
    std::thread([work,&slots]{
     SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_IDLE);
     const std::int64_t start=perf_now();
     ComPtr<ID3DBlob> blob,messages;
     const ShaderProgram& p=work->program;
     const HRESULT hr=D3DCompile(p.source.data(),p.source.size(),p.name,p.defines,nullptr,p.entry,p.profile,p.flags,0,&blob,&messages);
     std::vector<char> code;
     if(SUCCEEDED(hr)&&blob){
      const char* bytes=static_cast<const char*>(blob->GetBufferPointer());
      code.assign(bytes,bytes+blob->GetBufferSize());
      // The cache file (another process may write the same name: the
      // rename keeps whichever is whole).
      const std::wstring temporary=work->file+L".tmp";
      CreateDirectoryW(work->file.substr(0,work->file.find_last_of(L"\\/")).c_str(),nullptr);
      if(FILE* out=nullptr;!_wfopen_s(&out,temporary.c_str(),L"wb")&&out){
       const bool written=std::fwrite(code.data(),1,code.size(),out)==code.size();
       std::fclose(out);
       if(!written||!MoveFileExW(temporary.c_str(),work->file.c_str(),MOVEFILE_REPLACE_EXISTING))DeleteFileW(temporary.c_str());
      }
     }
     {std::lock_guard<std::mutex> lock(slots.mutex);--slots.running;}
     slots.freed.notify_one();
     std::lock_guard<std::mutex> lock(work->mutex);
     work->code=std::move(code);work->ms=unsigned(1000.0*double(perf_now()-start)/double(perf_frequency()));work->finished=true;
     work->done.notify_all();
    }).detach();
   }catch(...){
    {std::lock_guard<std::mutex> lock(slots.mutex);--slots.running;}
    slots.freed.notify_one();
    return false;
   }
   std::unique_lock<std::mutex> lock(work->mutex);
   while(!work->finished){if(cancelled())return false;work->done.wait_for(lock,std::chrono::milliseconds(100));}
   for(unsigned now=job.compile_ms.load();work->ms>now&&!job.compile_ms.compare_exchange_weak(now,work->ms);){}
   code=std::move(work->code);
   return !code.empty();
  }catch(...){code.clear();return false;}
 }
#endif
 void run() noexcept{
  const BackgroundWorkerPriority priority;
  for(;;){
   std::pair<std::shared_ptr<NativeSpecialisationJob>,std::size_t> item;
   {
    std::lock_guard<std::mutex> lock(mutex);
    if(cancel||queue.empty()){--running;return;}
    item=std::move(queue.front());queue.pop_front();
   }
   build(device.Get(),*item.first,item.second,&cancel);
  }
 }
 // Queues every program of job and starts workers up to the pool size (no
 // more than the queued programs need).
 void add(const std::shared_ptr<NativeSpecialisationJob>& job,bool first) {
  std::lock_guard<std::mutex> lock(mutex);
  const std::size_t count=job->files.size();
  for(std::size_t n=0;n<count;++n){
   if(first)queue.emplace_front(job,count-1-n);else queue.emplace_back(job,n);
  }
  while(running<target&&running<queue.size()){
   try{workers.emplace_back([this]{run();});++running;}
   catch(...){break;}
  }
  // No worker could start: the programs fail here (general programs stay).
  if(!running)for(auto& item:queue){++item.first->failed;++item.first->finished;}
  if(!running)queue.clear();
 }
 // Joins the exited workers once none runs.
 void reap() noexcept{
  std::vector<std::thread> exited;
  {std::lock_guard<std::mutex> lock(mutex);if(running||workers.empty())return;exited.swap(workers);}
  for(auto& worker:exited)if(worker.joinable())worker.join();
 }
 ~NativeSpecialisationPool(){
  {std::lock_guard<std::mutex> lock(mutex);cancel=true;queue.clear();}
  for(auto& worker:workers)if(worker.joinable())worker.join();
 }
};

// The key of the values this flush uploaded (renderer.h bits).
std::uint32_t Renderer::native_specialisation_key() const noexcept{
 std::uint32_t key=native_spec_terrain_bits_|native_spec_fog_bits_;
 bool slots=false;for(const auto& light:native_point_lights_)slots|=light.shadow_slots[0]>0;
 if(!slots)key|=32u;
 if(!native_sun_shadows_live_)key|=64u;
 // The cascade coverage debug view (flush_native constants.padding[0]).
 if(debug_overlay_.mode()==DebugOverlayMode::full&&debug_overlay_.config().debug_resource_view()==DebugResourceView::shadow_cascade_coverage)key|=512u;
 if(!native_point_soft_live_)key|=1024u;
 if(!material_level())key|=4096u; // NATIVE_OFF_MATERIALS (world objects, S2, need no class library)
 for(unsigned p=1;p<kTerrain2Pools;++p)if(terrain2_pools_[p].atlas)key|=8192u; // NATIVE_TERRAIN2_POOLS
 return key;
}

void Renderer::release_native_specialisation() noexcept{
 native_spec_pool_.reset(); // stops the queue and joins the workers
 native_spec_jobs_.clear();
 native_spec_active_=nullptr;native_spec_sets_.clear();native_pools_set_loaded_here_=false;
}

// Starts the set of key on the worker threads (compile or disk cache, and
// the shader objects); the render thread only swaps finished sets in.
// At pipeline creation: the shipped keys whose program files all exist. The
// native source is hashed once (shader_source_state); each program's key
// then hashes only its defines and entry.
void Renderer::find_prebuilt_native_specialisations() noexcept try{
 native_spec_files_.clear();native_spec_keys_.clear();
 const std::string& source=owned_native_shader_source();
 const auto state=shader_source_state(source);
 unsigned prebuilt=0,missing=0;
 for(const std::uint32_t key:native_shipped_specialisation_keys()){
  NativeSpecialisedPrograms list;
  if(!native_specialised_programs(key,list)||list.programs.size()!=std::tuple_size<NativeSpecialisedSet>::value){++missing;continue;}
  std::vector<std::wstring> files;files.reserve(list.programs.size());
  std::vector<ShaderKey> program_keys;program_keys.reserve(list.programs.size());
  bool complete=true;
  for(const auto& program:list.programs){
   const ShaderKey program_key=program.source.data()==source.data()?shader_key(state,program):shader_key(program);
   const void* embedded=nullptr;std::size_t embedded_size=0;
   const bool bundled=shader_cache_detail::find(program_key,embedded,embedded_size);
   files.push_back(native_spec_cache_path(program_key));program_keys.push_back(program_key);
   if(!bundled&&GetFileAttributesW(files.back().c_str())==INVALID_FILE_ATTRIBUTES){complete=false;break;}
  }
  if(!complete){++missing;continue;}
  native_spec_files_.emplace(key,std::move(files));
  native_spec_keys_.emplace(key,std::move(program_keys));++prebuilt;
 }
 char text[96];std::snprintf(text,sizeof(text),"prebuilt sets=%u missing=%u",prebuilt,missing);
 bridge_report_event("shader_specialisation",text);
}catch(...){native_spec_files_.clear();native_spec_keys_.clear();}

bool Renderer::native_specialisation_building(std::uint32_t key) const noexcept{
 for(const auto& job:native_spec_jobs_)if(job->key==key)return true;
 return false;
}

bool Renderer::native_specialisation_compiling() const noexcept{
 for(const auto& job:native_spec_jobs_)if(!job->compile.programs.empty())return true;
 return false;
}

void Renderer::start_native_specialisation(std::uint32_t key,bool first) noexcept try{
 if(native_specialisation_building(key))return;
#ifdef EID3D11_EMBEDDED
 // Embedded in EIRebuild the game thread stalls while the workers create a
 // set (3-8 s with a cold driver). Build sets only at startup and while a
 // load is revealed (the loading picture is held); during play the exact
 // general programs serve until the next load. Building none at all left
 // heavy scenes on the general programs (80 ms GPU in Suslanger caves).
 if(native_frame_index_>0&&!load_reveal_tracking_)return;
#endif
 // A key without its prebuilt files: the general programs draw (exact for
 // every key; a second terrain 2.0 pool draws with the shipped pools set,
 // native_terrain2_pools_set). The x64 host compiles only the current key's
 // own set (first: update_native_specialisation, after the key held), one
 // set at a time; siblings and the startup queue keep the current set (a
 // missing sibling falls back in native_pixel). The 32-bit renderer never
 // compiles.
 const auto found=native_spec_files_.find(key);
 std::vector<std::wstring> files;NativeSpecialisedPrograms compile;
 if(found!=native_spec_files_.end())files=found->second;
 else{
#if defined(_WIN64)
  if(first)for(const auto& job:native_spec_jobs_)if(!job->compile.programs.empty())return; // one compiled set at a time: asked again next flush
  if(first){
  const std::string& source=owned_native_shader_source();
  const auto state=shader_source_state(source);
  if(native_specialised_programs(key,compile)&&compile.programs.size()==std::tuple_size<NativeSpecialisedSet>::value){
   for(const auto& program:compile.programs)
    files.push_back(native_spec_cache_path(program.source.data()==source.data()?shader_key(state,program):shader_key(program)));
   char text[128];std::snprintf(text,sizeof(text),"not_prebuilt key=0x%03x: the x64 host compiles it (general programs%s meanwhile)",key,(key&8192u)?" or the pools set":"");
   bridge_report_event("shader_specialisation",text);
  }else files.clear();
  }
  if(files.empty())
#endif
  {
   char text[112];std::snprintf(text,sizeof(text),"not_prebuilt key=0x%03x (general programs%s stay for it)",key,(key&8192u)?" or the pools set":"");
   bridge_report_event("shader_specialisation",text);
   native_spec_sets_[key]; // an empty set: never requested again
   return;
  }
 }
 if(!native_spec_pool_){
  native_spec_pool_=std::make_shared<NativeSpecialisationPool>();
  native_spec_pool_->device=device_;
  // Two cores stay free for the game and render threads and for input and
  // composition elsewhere. The 32-bit renderer creates one program at a
  // time: a driver that compiles inside CreatePixelShader (Intel's igc32)
  // holds hundreds of MB per call for these programs, and on an Intel
  // Iris Xe laptop (#296) six or more creates at once ran the game's address
  // space out (bad_alloc in igc32, E_OUTOFMEMORY, then a driver crash).
#if defined(_WIN64)
  const unsigned cores=std::thread::hardware_concurrency();
  native_spec_pool_->target=cores>2?cores-2:1;
#else
  native_spec_pool_->target=1;
#endif
 }
 auto job=std::make_shared<NativeSpecialisationJob>();
 job->key=key;job->begin=perf_now();job->files=std::move(files);job->compile=std::move(compile);
 if(key&8192u)address_space_mb(job->va_used_before,job->va_free_before,true);
 const auto keys=native_spec_keys_.find(key);
 if(keys!=native_spec_keys_.end())job->keys=keys->second;
 job->shaders.resize(job->files.size());
 native_spec_jobs_.reserve(native_spec_jobs_.size()+1);
 native_spec_pool_->add(job,first);
 native_spec_jobs_.push_back(std::move(job)); // reserved: no throw once queued
}catch(...){}
namespace {

// The sets prepared after a key's own: the same key with the point-shadow
// bit flipped, and both without the contact blend (bound per batch where no
// draw reaches the ground). The fast-combiner sets (bit 11) serve every batch
// with fast stages.
std::array<std::uint32_t,7> native_specialisation_siblings(std::uint32_t key) noexcept{
 return {key|2048u,(key|2048u)^32u,key|2052u,(key|2052u)^32u,key^32u,key|4u,(key|4u)^32u};
}
}  // namespace

// Prepare the active play key at pipeline creation. Sibling variants are
// optional fast paths and are warmed after the first stable world frames;
// compiling all of them during startup can starve the menu and loading UI.
void Renderer::queue_startup_specialisations() noexcept try{
 const RendererConfig& config=debug_overlay_.config();
 const std::uint32_t key=native_play_specialisation_key([&](const char* name,float fallback){
  const RendererOption* option=config.find(name);
  if(!option)return fallback;
  switch(option->type){
   case RendererOptionType::boolean:return option->boolean_value?1.f:0.f;
   case RendererOptionType::integer:return static_cast<float>(option->integer_value);
   case RendererOptionType::floating_point:return option->floating_value;
   default:return fallback;
  }
 });
 native_spec_queue_.clear();
 const auto queue=[&](std::uint32_t k){
  if(k&&std::find(native_spec_queue_.begin(),native_spec_queue_.end(),k)==native_spec_queue_.end())native_spec_queue_.push_back(k);
 };
 // The main menu's own key first (#325): its backdrop uploads none of the
 // terrain bits (bits 0-4, 14), so presets that switch terrain features off
 // draw the menu under a key of their own (Lowest: 0x14e0 in the menu,
 // 0x54ff in play; native_shipped_specialisation_keys ships both). Queued
 // only for the play key before, the menu's set waited for the world flush's
 // 60-frame hold: laptop job 147 L1 (Iris Xe, Lowest) drew the menu's first
 // frames with the general programs, 7.6 s and 7.9 s each.
 const std::uint32_t menu=key&~(31u|16384u);
 if(menu!=key){queue(menu);for(const std::uint32_t sibling:native_specialisation_siblings(menu))queue(sibling);}
 queue(key);
 for(const std::uint32_t sibling:native_specialisation_siblings(key))queue(sibling);
 // The pools set (key 8192) is not queued: a second terrain 2.0 pool is
 // rare (neighbour pages are normalised to pool 0), and its 39 programs cost
 // the 32-bit game address space for nothing otherwise (#272, x86 Ingos
 // runs). native_terrain2_pools_set creates it when a second pool appears.
 pump_native_specialisation(true);
}catch(...){native_spec_queue_.clear();}

// Takes in every finished set and, with start_queued, starts every queued
// startup set not yet resident (their programs share the workers' queue).
// The world flush starts the current key's own sets first
// (update_native_specialisation); presents without a world flush (startup,
// intro, menus, loading) start queued ones.
void Renderer::pump_native_specialisation(bool start_queued) noexcept{
 for(std::size_t index=0;index<native_spec_jobs_.size();){
  auto& job=*native_spec_jobs_[index];
  if(job.finished!=job.files.size()){++index;continue;}
  const double ms=1000.0*static_cast<double>(perf_now()-job.begin)/static_cast<double>(perf_frequency());
  char text[320];
  auto& set=native_spec_sets_[job.key];
  if(job.failed==0){
   for(std::size_t n=0;n<set.size();++n)set[n]=job.shaders[n];
   const int written=std::snprintf(text,sizeof(text),"ready key=0x%03x programs=%zu in %.1f s: cached=%u compiled=%u (longest %u ms) resident_sets=%zu building=%zu workers=%u",
    job.key,job.files.size(),ms/1000.0,job.cached.load(),job.compiled.load(),job.compile_ms.load(),native_spec_sets_.size(),native_spec_jobs_.size()-1,
    native_spec_pool_?native_spec_pool_->target:0u);
   if((job.key&8192u)&&written>0&&std::size_t(written)<sizeof(text)){ // what a pools set cost the address space
    std::uint64_t used=0,largest=0;address_space_mb(used,largest,true);
    std::snprintf(text+written,sizeof(text)-std::size_t(written),"; address space used %llu -> %llu MB, largest free %llu -> %llu MB",
     static_cast<unsigned long long>(job.va_used_before),static_cast<unsigned long long>(used),
     static_cast<unsigned long long>(job.va_free_before),static_cast<unsigned long long>(largest));
   }
  }else std::snprintf(text,sizeof(text),"failed key=0x%03x programs=%zu failed=%u (general programs stay for it)",job.key,job.files.size(),job.failed.load());
  bridge_report_event("shader_specialisation",text);
  native_spec_jobs_.erase(native_spec_jobs_.begin()+static_cast<std::ptrdiff_t>(index));
 }
 if(native_spec_jobs_.empty()&&native_spec_pool_)native_spec_pool_->reap();
 while(start_queued&&device_&&!native_spec_queue_.empty()){
  const std::uint32_t key=native_spec_queue_.front();native_spec_queue_.erase(native_spec_queue_.begin());
  if(key&&!native_spec_sets_.count(key))start_native_specialisation(key,false);
 }
}

std::size_t Renderer::native_specialisation_programs_remaining() const noexcept{
 std::size_t remaining=0;
 for(const auto& job:native_spec_jobs_)remaining+=job->files.size()-job->finished.load();
 return remaining;
}

bool Renderer::native_specialisation_settled() const noexcept{
 if(native_spec_updated_frame_!=native_frame_index_)return true;
 if(!native_spec_jobs_.empty())return false;
 const auto resident=[&](std::uint32_t k){return k==0||native_spec_sets_.count(k)!=0;};
 return resident(native_spec_current_key_);
}

// Per world flush: takes in a finished set, binds the resident set of the
// current key (same frame; the general programs while none is built yet),
// starts the current key's set once it has held for 60 frames (at once while
// a load is being revealed: the hold waits for the sets) at the front of the
// workers' queue, and with it all its siblings behind it.
void Renderer::update_native_specialisation() noexcept{
 const std::uint32_t key=native_specialisation_key();
 pump_native_specialisation(false);
 // An interface flush after composition (owned HUD meshes, portraits)
 // uploads its own values: a zero fog frame (bit 7), no sun shadows (bit 6)
 // and no lamp shadow slots (bit 5). When that key's set is prebuilt it
 // takes the same path as a world flush (sets the current key, starts the
 // set, the hold waits for it): at Lowest and Low the interface key is the
 // menu's and the world's own, and laptop jobs 141, 147 and 148 (Iris Xe)
 // were slower to the menu and the first world without it. A key that is
 // not prebuilt (High's interface key 0x0e0) gets none of that: the general
 // programs draw it, nothing compiles and no reveal waits for it. Before,
 // such a key started at once during a load (the 60-frame hold is skipped
 // while a reveal is tracked), the hold waited its 3 s cap on every D3D11
 // load and the x64 host compiled it with FXC for about 5 minutes (#325:
 // laptop job 137, PC job 23).
 if(scene_composited_&&!native_spec_files_.count(key)){
  native_spec_active_=(key&8192u)?native_terrain2_pools_set():nullptr;
  return;
 }
 // Only the world flush names the candidate. An interface flush after
 // composition (owned HUD meshes, portraits) uploads its own fog values, so
 // with fog_preset above faithful its key differs from the world's in bits
 // 7-8 every frame; counted here it reset the 60-frame hold twice a frame
 // and a preset changed in play never got its set (issue #243: Medium stayed
 // on the general programs).
 if(!scene_composited_&&key!=native_spec_candidate_){
  // #344: a world key change in play binds another set (or the general
  // programs) whose first draws may compile on this thread. Named once per
  // change with the bits that changed (at most 100 lines a session).
  static unsigned key_change_lines=0;
  if(!load_reveal_tracking_&&native_spec_candidate_&&key_change_lines<100){
   ++key_change_lines;
   char text[160];std::snprintf(text,sizeof(text),"key_change in play 0x%04x -> 0x%04x (bits 0x%04x) frame=%llu new set %s",
    native_spec_candidate_,key,native_spec_candidate_^key,static_cast<unsigned long long>(native_frame_index_),
    native_spec_sets_.count(key)?"resident":native_specialisation_building(key)?"building":"absent");
   bridge_report_event("shader_specialisation",text);
  }
  native_spec_candidate_=key;native_spec_candidate_frame_=native_frame_index_;
 }
 const auto resident=[&](std::uint32_t k){return k==0||native_spec_sets_.count(k)!=0;};
 if(device_){
  // Then the same key with the point-shadow bit flipped, and both without
  // the contact blend (bound per batch where no draw reaches the ground).
  // The fast-combiner sets (bit 11) serve every batch with fast stages.
  if(!resident(key)&&!native_specialisation_building(key)&&
     (load_reveal_tracking_||native_frame_index_-native_spec_candidate_frame_>=60))
   start_native_specialisation(key,true);
  if(resident(key)||native_specialisation_building(key))
   for(const std::uint32_t sibling:native_specialisation_siblings(key))
    if(!resident(sibling))start_native_specialisation(sibling,false);
 }
 // The current key's family is complete or waiting to hold: the startup queue continues.
 pump_native_specialisation(true);
 native_spec_current_key_=key;native_spec_updated_frame_=native_frame_index_;
 const auto found=key?native_spec_sets_.find(key):native_spec_sets_.end();
 native_spec_active_=found!=native_spec_sets_.end()&&found->second[0]?&found->second:nullptr;
 // The general programs serve every key except a second terrain 2.0 pool
 // (terrain2.hlsl T2_ONE_POOL draws such a map in the marker colour): until
 // that key's own set is in, the pools set (key 8192: the general programs
 // with NATIVE_TERRAIN2_POOLS) draws it.
 if(!native_spec_active_&&(key&8192u))native_spec_active_=native_terrain2_pools_set();
}

// The pools set (key 8192, shipped, not queued at startup), resident or, the
// first time a second pool needs it, its prebuilt files read and created on
// this thread: one hitch instead of the marker. Logged with the address
// space before and after (what the set costs the 32-bit game).
Renderer::NativeSpecialisedSet* Renderer::native_terrain2_pools_set() noexcept try{
 constexpr std::uint32_t pools=8192u;
 const auto resident=native_spec_sets_.find(pools);
 if(resident!=native_spec_sets_.end()&&resident->second[0])return &resident->second;
 if(native_pools_set_loaded_here_||!device_)return nullptr;
 native_pools_set_loaded_here_=true;
 const auto files=native_spec_files_.find(pools);
 if(files==native_spec_files_.end()){
  // A build without the preset cache: only then can a second-pool map show
  // the marker (named once).
  bridge_report_event("shader_specialisation","pools set 0x2000 not prebuilt (no preset cache): a second-pool map draws the marker");
  return nullptr;
 }
 std::uint64_t used_before=0,free_before=0,used_after=0,free_after=0;
 address_space_mb(used_before,free_before,true);
 NativeSpecialisationJob job;job.key=pools;job.begin=perf_now();job.files=files->second;job.shaders.resize(job.files.size());
 for(std::size_t n=0;n<job.files.size();++n)NativeSpecialisationPool::build(device_.Get(),job,n);
 const double ms=1000.0*static_cast<double>(perf_now()-job.begin)/static_cast<double>(perf_frequency());
 address_space_mb(used_after,free_after,true);
 char text[256];
 if(job.failed){
  std::snprintf(text,sizeof(text),"pools set 0x2000 failed on the render thread (%u of %zu programs)",job.failed.load(),job.files.size());
  bridge_report_event("shader_specialisation",text);
  return nullptr;
 }
 auto& set=native_spec_sets_[pools];
 for(std::size_t n=0;n<set.size();++n)set[n]=job.shaders[n];
 std::snprintf(text,sizeof(text),"pools set 0x2000 created on the render thread in %.0f ms (a second pool appeared); address space used %llu -> %llu MB, largest free %llu -> %llu MB",
  ms,static_cast<unsigned long long>(used_before),static_cast<unsigned long long>(used_after),
  static_cast<unsigned long long>(free_before),static_cast<unsigned long long>(free_after));
 bridge_report_event("shader_specialisation",text);
 return &set;
}catch(...){return nullptr;}

// Per batch: contact false selects the set with the contact blend compiled
// out (key bit 2), fast_combiner the set without the general combiner
// (bit 11), each once resident; else the current choice.
ID3D11PixelShader* Renderer::native_pixel(ComPtr<ID3D11PixelShader> Renderer::* member,bool contact,bool fast_combiner) const noexcept{
 const std::uint32_t base=native_spec_current_key_,extra=(contact?0u:4u)|(fast_combiner?2048u:0u);
 for(const std::uint32_t key:{base|extra,base|(extra&4u)}){
  if(key==base)break;
  const auto found=native_spec_sets_.find(key);
  if(found!=native_spec_sets_.end()&&found->second[0]){
   const NativePixelMember* members=native_specialised_members();
   for(std::size_t n=0;n<kNativeSpecialisedMembers;++n)if(members[n]==member)return found->second[n].Get();
  }
 }
 return native_pixel(member);
}

// The contact ground grid (1 m vertices, the same values as the contact
// blend's t13) as 4 m cell maxima, each covering its cell's corner vertices.
void Renderer::build_native_contact_cells(const std::vector<float>& ground,std::uint32_t verts_x,std::uint32_t verts_y) noexcept try{
 native_contact_cell_high_.clear();native_contact_cells_x_=native_contact_cells_y_=0;++native_contact_generation_;
 if(verts_x<2||verts_y<2||ground.size()!=std::size_t(verts_x)*verts_y)return;
 const std::uint32_t cells_x=(verts_x-1+3)/4,cells_y=(verts_y-1+3)/4;
 native_contact_cell_high_.assign(std::size_t(cells_x)*cells_y,-std::numeric_limits<float>::max());
 for(std::uint32_t y=0;y<verts_y;++y)for(std::uint32_t x=0;x<verts_x;++x){
  const float h=ground[std::size_t(y)*verts_x+x];
  // A vertex on a cell edge bounds the cells on both sides.
  for(std::uint32_t cy=(y?(y-1)/4:0);cy<=std::min(cells_y-1,y/4);++cy)for(std::uint32_t cx=(x?(x-1)/4:0);cx<=std::min(cells_x-1,x/4);++cx){
   float& high=native_contact_cell_high_[std::size_t(cy)*cells_x+cx];high=std::max(high,h);
  }
 }
 native_contact_cells_x_=cells_x;native_contact_cells_y_=cells_y;
}catch(...){native_contact_cell_high_.clear();native_contact_cells_x_=native_contact_cells_y_=0;}

// Conservative: the contact flag, and the draw's world bounds (static,
// unposed, active map) reach within the contact band of the ground under its
// footprint (the shader samples the grid clamped to its edges, so the edge
// cells cover a footprint beyond them).
bool Renderer::native_draw_may_contact(const NativeQueuedDraw& draw,UINT contact_map) const noexcept{
 if(!(draw.instance.flags&native_ground_contact_flag))return false;
 if(contact_map||!draw.instance.world_geometry||draw.instance.pose_node!=UINT_MAX||native_contact_cell_high_.empty()||
    !std::isfinite(draw.bounds_min[0])||!std::isfinite(draw.bounds_min[1])||!std::isfinite(draw.bounds_min[2])||
    !std::isfinite(draw.bounds_max[0])||!std::isfinite(draw.bounds_max[1]))return true;
 const auto cell=[](float v,std::uint32_t count){return static_cast<std::uint32_t>(std::clamp(std::floor(v/4.f),0.f,float(count-1)));};
 const std::uint32_t x0=cell(draw.bounds_min[0],native_contact_cells_x_),x1=cell(draw.bounds_max[0],native_contact_cells_x_);
 const std::uint32_t y0=cell(draw.bounds_min[1],native_contact_cells_y_),y1=cell(draw.bounds_max[1],native_contact_cells_y_);
 float high=-std::numeric_limits<float>::max();
 for(std::uint32_t y=y0;y<=y1;++y)for(std::uint32_t x=x0;x<=x1;++x)high=std::max(high,native_contact_cell_high_[std::size_t(y)*native_contact_cells_x_+x]);
 return draw.bounds_min[2]-high<=native_contact_reach_;
}

ID3D11PixelShader* Renderer::native_pixel(ComPtr<ID3D11PixelShader> Renderer::* member) const noexcept{
 if(native_spec_active_){
  const NativePixelMember* members=native_specialised_members();
  for(std::size_t n=0;n<kNativeSpecialisedMembers;++n)
   if(members[n]==member)return (*native_spec_active_)[n].Get();
 }
 return (this->*member).Get();
}

// F10 shader_split_check: the check programs from the embedded table, once
// (not with the pipeline: 16 programs of 32-bit address space nobody uses).
void Renderer::ensure_native_instance_check_programs() noexcept try{
 if(native_instance_check_tried_||!device_)return;
 native_instance_check_tried_=true;
 std::vector<ShaderProgram> programs;append_native_shader_programs(programs);
 unsigned created=0;
 for(const auto& program:programs)
  for(std::size_t c=0;c<kNativeInstanceVertexClasses;++c)
   if(program.defines==native_instance_vertex_defines(c,false,true)){
    ComPtr<ID3DBlob> shader,error;
    if(SUCCEEDED(compile_shader(program,&shader,&error))&&
       SUCCEEDED(device_->CreateVertexShader(shader->GetBufferPointer(),shader->GetBufferSize(),nullptr,native_instance_check_vs_[c].ReleaseAndGetAddressOf())))++created;
   }
 for(const auto& program:programs)
  for(std::size_t slot=0;slot<kNativeInstanceSlots;++slot)for(const bool masked:{false,true})
   if(program.defines==native_instance_defines(slot,masked,true,false)){
    ComPtr<ID3DBlob> shader,error;
    if(SUCCEEDED(compile_shader(program,&shader,&error))&&
       SUCCEEDED(device_->CreatePixelShader(shader->GetBufferPointer(),shader->GetBufferSize(),nullptr,native_instance_check_ps_[slot*2u+(masked?1u:0u)].ReleaseAndGetAddressOf())))++created;
   }
 char text[64];std::snprintf(text,sizeof(text),"check programs created=%u",created);
 bridge_report_event("shader_split",text);
}catch(...){}

// A batch's material instance program: the resident specialised set of the
// current key with the contact and fast-combiner extras (as native_pixel),
// else the current key's set, else the general table's; with F10
// shader_split_check the check program. Null for a missing program: the
// caller omits the batch (never the general program in its place).
ID3D11PixelShader* Renderer::native_instance_pixel(std::uint8_t instance,bool masked,bool contact,bool fast_combiner,bool check) const noexcept{
 if(!(instance&7u)||(instance&7u)>kNativeInstanceDomains)return nullptr;
 const std::size_t index=native_instance_slot(instance)*2u+(masked?1u:0u);
 if(!native_instance_built(index/2u))return nullptr; // omitted and named by the caller
 if(check)return native_instance_check_ps_[index].Get();
 const std::size_t position=kNativeSpecialisedMembers+native_built_instance_slots_before(index/2u)*2u+(index&1u);
 const std::uint32_t base=native_spec_current_key_,extra=(contact?0u:4u)|(fast_combiner?2048u:0u);
 for(const std::uint32_t key:{base|extra,base|(extra&4u)}){
  if(key==base)break;
  const auto found=native_spec_sets_.find(key);
  if(found!=native_spec_sets_.end()&&found->second[0])return found->second[position].Get();
 }
 if(native_spec_active_)return (*native_spec_active_)[position].Get();
 return native_instance_ps_[index].Get();
}

// Depth prepass and sun-shadow mask targets (world size) and pass states.
HRESULT Renderer::create_prepass_targets(){
 prepass_rest_.Reset();prepass_normal_.Reset();shadow_mask_.Reset();
 prepass_rest_rtv_.Reset();prepass_normal_rtv_.Reset();shadow_mask_rtv_.Reset();
 prepass_rest_srv_.Reset();prepass_normal_srv_.Reset();shadow_mask_srv_.Reset();
 world_depth_read_only_dsv_.Reset();
 terrain_miss_.Reset();terrain_miss_uav_.Reset();terrain_miss_srv_.Reset();
 if(feature_level_<D3D_FEATURE_LEVEL_11_0||!depth_)return S_FALSE;
 HRESULT hr=S_OK;
 // The slim terrain pass's mask (terrain_miss_) is made by the first flush
 // that draws the slim pass (renderer_flush.cpp), at this size.
 const auto target=[&](DXGI_FORMAT format,ComPtr<ID3D11Texture2D>& texture,ComPtr<ID3D11RenderTargetView>& rtv,ComPtr<ID3D11ShaderResourceView>& srv){
  D3D11_TEXTURE2D_DESC desc{};desc.Width=width_;desc.Height=height_;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
  desc.Format=format;desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
  HRESULT result=device_->CreateTexture2D(&desc,nullptr,&texture);
  if(SUCCEEDED(result))result=device_->CreateRenderTargetView(texture.Get(),nullptr,&rtv);
  if(SUCCEEDED(result))result=device_->CreateShaderResourceView(texture.Get(),nullptr,&srv);
  return result;
 };
 if(FAILED(hr=target(DXGI_FORMAT_R16G16B16A16_FLOAT,prepass_rest_,prepass_rest_rtv_,prepass_rest_srv_))||
    FAILED(hr=target(DXGI_FORMAT_R16G16_UNORM,prepass_normal_,prepass_normal_rtv_,prepass_normal_srv_))||
    FAILED(hr=target(DXGI_FORMAT_R16G16_FLOAT,shadow_mask_,shadow_mask_rtv_,shadow_mask_srv_)))return hr;
 D3D11_DEPTH_STENCIL_VIEW_DESC read_only{};read_only.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;
 read_only.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;read_only.Flags=D3D11_DSV_READ_ONLY_DEPTH;
 if(FAILED(hr=device_->CreateDepthStencilView(depth_.Get(),&read_only,&world_depth_read_only_dsv_)))return hr;
 if(!masked_depth_state_){
  // Prepass: the batch's own depth test, depth written, stencil bit 0x80 set
  // where it passes. Material pass: EQUAL, nothing written. Mask pass: the
  // marked pixels only, clearing the mark.
  for(unsigned index=0;index<2;++index){
   D3D11_DEPTH_STENCIL_DESC desc{};desc.DepthEnable=TRUE;desc.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;
   desc.DepthFunc=index?D3D11_COMPARISON_LESS_EQUAL:D3D11_COMPARISON_LESS;
   desc.StencilEnable=TRUE;desc.StencilReadMask=0x80;desc.StencilWriteMask=0x80;
   desc.FrontFace={D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_REPLACE,D3D11_COMPARISON_ALWAYS};
   desc.BackFace=desc.FrontFace;
   if(FAILED(hr=device_->CreateDepthStencilState(&desc,&prepass_depth_states_[index])))return hr;
  }
  D3D11_DEPTH_STENCIL_DESC equal{};equal.DepthEnable=TRUE;equal.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;equal.DepthFunc=D3D11_COMPARISON_EQUAL;
  if(FAILED(hr=device_->CreateDepthStencilState(&equal,&masked_depth_state_)))return hr;
  D3D11_DEPTH_STENCIL_DESC mask{};mask.DepthEnable=FALSE;mask.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;mask.DepthFunc=D3D11_COMPARISON_ALWAYS;
  mask.StencilEnable=TRUE;mask.StencilReadMask=0x80;mask.StencilWriteMask=0x80;
  mask.FrontFace={D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_KEEP,D3D11_STENCIL_OP_ZERO,D3D11_COMPARISON_EQUAL};
  mask.BackFace=mask.FrontFace;
  if(FAILED(hr=device_->CreateDepthStencilState(&mask,&shadow_mask_depth_state_))){masked_depth_state_.Reset();return hr;}
  D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;
  if(FAILED(hr=device_->CreateRasterizerState(&raster,&shadow_mask_raster_))){masked_depth_state_.Reset();return hr;}
 }
 return S_OK;
}

HRESULT Renderer::create_shadow_history_targets(){
 for(auto& value:shadow_history_srv_)value.Reset();
 for(auto& value:shadow_history_rtv_)value.Reset();
 for(auto& value:shadow_history_mask_)value.Reset();
 shadow_history_depth_srv_.Reset();shadow_history_depth_.Reset();
 shadow_history_constants_.Reset();shadow_history_sampler_.Reset();
 shadow_history_valid_=shadow_history_written_=shadow_history_committed_=false;
 shadow_history_previous_frame_=shadow_history_current_frame_=UINT64_MAX;
 if(feature_level_<D3D_FEATURE_LEVEL_11_0)return S_FALSE;
 HRESULT hr=S_OK;
 D3D11_TEXTURE2D_DESC mask{};mask.Width=width_;mask.Height=height_;
 mask.MipLevels=mask.ArraySize=mask.SampleDesc.Count=1;
 mask.Format=DXGI_FORMAT_R8_UNORM;
 mask.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
 for(unsigned index=0;index<2;++index){
  if(FAILED(hr=device_->CreateTexture2D(&mask,nullptr,&shadow_history_mask_[index])))return hr;
  if(FAILED(hr=device_->CreateRenderTargetView(shadow_history_mask_[index].Get(),nullptr,
      &shadow_history_rtv_[index])))return hr;
  if(FAILED(hr=device_->CreateShaderResourceView(shadow_history_mask_[index].Get(),nullptr,
      &shadow_history_srv_[index])))return hr;
 }
 D3D11_TEXTURE2D_DESC depth{};depth.Width=width_;depth.Height=height_;
 depth.MipLevels=depth.ArraySize=depth.SampleDesc.Count=1;
 depth.Format=DXGI_FORMAT_R24G8_TYPELESS;
 depth.BindFlags=D3D11_BIND_SHADER_RESOURCE;
 if(FAILED(hr=device_->CreateTexture2D(&depth,nullptr,&shadow_history_depth_)))return hr;
 D3D11_SHADER_RESOURCE_VIEW_DESC view{};view.Format=DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
 view.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;view.Texture2D.MipLevels=1;
 if(FAILED(hr=device_->CreateShaderResourceView(shadow_history_depth_.Get(),&view,
     &shadow_history_depth_srv_)))return hr;
 D3D11_BUFFER_DESC constants{};constants.ByteWidth=sizeof(NativeShadowHistoryConstants);
 constants.Usage=D3D11_USAGE_DYNAMIC;constants.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
 constants.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
 if(FAILED(hr=device_->CreateBuffer(&constants,nullptr,&shadow_history_constants_)))return hr;
 D3D11_SAMPLER_DESC sampler{};sampler.Filter=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
 sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
 sampler.MaxLOD=D3D11_FLOAT32_MAX;
 return device_->CreateSamplerState(&sampler,&shadow_history_sampler_);
}

// Every pass that reads the history (world flushes, ground cover) calls this;
// when the history is unavailable its constants say so (no reuse, no jitter).
bool Renderer::prepare_shadow_history() noexcept {
 const auto upload=[this](const NativeShadowHistoryConstants& constants){
  // D3D12: kept for the passes' b6 (bind_shadow_history_d3d12).
  if(d3d12_){std::memcpy(shadow_history_constants12_.data(),&constants,sizeof(constants));return true;}
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if(!shadow_history_constants_||FAILED(context_->Map(shadow_history_constants_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))
   return false;
  std::memcpy(mapped.pData,&constants,sizeof(constants));
  context_->Unmap(shadow_history_constants_.Get(),0);
  return true;
 };
 const auto disabled=[&]{(void)upload(NativeShadowHistoryConstants{});return false;};
 const bool resources=d3d12_?shadow_history_mask12_[0]&&shadow_history_mask12_[1]&&shadow_history_depth12_:
  shadow_history_rtv_[0]&&shadow_history_rtv_[1]&&shadow_history_depth_srv_&&shadow_history_constants_&&shadow_history_sampler_;
 if(!native_shadows_enabled_||!resources||!native_world_clip_valid_)return disabled();
 if(debug_overlay_.mode()==DebugOverlayMode::full&&
    debug_overlay_.config().debug_resource_view()==DebugResourceView::shadow_cascade_coverage)
  return disabled();
 if(shadow_history_current_frame_!=native_frame_index_){
  shadow_history_current_frame_=native_frame_index_;
  shadow_history_current_index_=1u-shadow_history_previous_index_;
  shadow_history_current_epoch_=bridge_current_world_epoch();
#ifdef EID3D11_TESTING
  if(test_shadow_history_epoch_)shadow_history_current_epoch_=test_shadow_history_epoch_;
#endif
  shadow_history_written_=shadow_history_committed_=false;
  const float clear[]{1,0,0,1};
  if(d3d12_)d3d12_->clear_texture(*shadow_history_mask12_[shadow_history_current_index_],clear);
  else context_->ClearRenderTargetView(shadow_history_rtv_[shadow_history_current_index_].Get(),clear);
 }
 if(shadow_history_committed_)return disabled();
 NativeShadowHistoryConstants constants{};
 constants.previous_world_to_clip=shadow_history_previous_clip_;
 const bool can_reuse=shadow_history_valid_&&shadow_history_current_epoch_&&
  shadow_history_current_epoch_==shadow_history_previous_epoch_&&
  shadow_history_previous_frame_!=UINT64_MAX&&
  shadow_history_previous_frame_+1==native_frame_index_;
 UINT history_width=width_,history_height=height_;
 if(d3d12_){UINT levels=0;DXGI_FORMAT format{};(void)d3d12_->texture_info(*shadow_history_mask12_[0],history_width,history_height,levels,format);}
 constants.settings={float(history_width),float(history_height),.8f,can_reuse?1.f:0.f};
 // Soft-shadow disks rotate per frame only while history can integrate them.
 const float phase=float(native_frame_index_%64u);
 if(can_reuse)constants.jitter={phase*7.f,phase*13.f,0,0};
 if(can_reuse&&d3d12_)d3d12_->count("sun-shadow history reused");
 return upload(constants);
}

void Renderer::commit_shadow_history() noexcept {
 if(shadow_history_committed_||!shadow_history_written_||
    shadow_history_current_frame_!=native_frame_index_||
    (d3d12_?!shadow_history_depth12_||!world_d3d12_:!shadow_history_depth_))return;
 const auto scene_clip=bridge_scene_world_to_clip_snapshot();
 std::array<float,16> clip{};
 if(scene_clip.valid)clip=scene_clip.world_to_clip;
#ifdef EID3D11_TESTING
 else if(test_shadow_history_epoch_&&native_world_clip_valid_)clip=native_world_clip_;
#endif
 else {shadow_history_valid_=false;shadow_history_committed_=true;return;}
 UINT history_width=width_,history_height=height_;
 if(d3d12_){
  // The world depth into the history depth, in stream order (executor copy).
  UINT levels=0;DXGI_FORMAT format{};
  if(!d3d12_->texture_info(*shadow_history_depth12_,history_width,history_height,levels,format)||
     FAILED(d3d12_->copy_depth(*shadow_history_depth12_,*world_d3d12_))){
   shadow_history_valid_=false;shadow_history_committed_=true;return;}
 }else{
  ID3D11ShaderResourceView* empty[2]{};
  context_->PSSetShaderResources(20,2,empty);
  context_->OMSetRenderTargets(0,nullptr,nullptr);
  context_->CopyResource(shadow_history_depth_.Get(),depth_.Get());
  context_->OMSetRenderTargets(1,rtv_.GetAddressOf(),dsv_.Get());
 }
 // The history was drawn with this frame's DLSS viewport jitter: it reprojects
 // into the jittered image (zero jitter when DLSS is off).
 shadow_history_previous_clip_=dlss_jittered_clip(clip,dlss_jitter_[0],dlss_jitter_[1],history_width,history_height);
 shadow_history_previous_epoch_=shadow_history_current_epoch_;
 shadow_history_previous_frame_=native_frame_index_;
 shadow_history_previous_index_=shadow_history_current_index_;
 shadow_history_valid_=shadow_history_current_epoch_!=0;
 shadow_history_committed_=true;
}

HRESULT Renderer::ensure_debug_stencil_view() noexcept {
 if(debug_stencil_srv_)return S_OK;
 if(!device_||!depth_)return E_UNEXPECTED;
 D3D11_TEXTURE2D_DESC texture{};depth_->GetDesc(&texture);
 if(texture.Format!=DXGI_FORMAT_R24G8_TYPELESS||
    !(texture.BindFlags&D3D11_BIND_SHADER_RESOURCE))return DXGI_ERROR_UNSUPPORTED;
 D3D11_SHADER_RESOURCE_VIEW_DESC view{};
 view.Format=DXGI_FORMAT_X24_TYPELESS_G8_UINT;
 view.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;
 view.Texture2D.MipLevels=1;
 return device_->CreateShaderResourceView(depth_.Get(),&view,&debug_stencil_srv_);
}

HRESULT Renderer::ensure_debug_texture_pipeline() noexcept {
 if(debug_texture_vs_&&debug_texture_ps_&&debug_texture_constants_&&
    debug_texture_sampler_)return S_OK;
 if(!device_)return E_UNEXPECTED;
 ComPtr<ID3DBlob> vertex,pixel,error;
 const stall_watch::Exempt fxc_compile;
 HRESULT hr=D3DCompile(kDebugResourceViewShader,
  sizeof(kDebugResourceViewShader)-1,nullptr,nullptr,nullptr,"debugTextureVS",
  "vs_4_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&vertex,&error);
 if(FAILED(hr))return hr;
 hr=D3DCompile(kDebugResourceViewShader,sizeof(kDebugResourceViewShader)-1,
  nullptr,nullptr,nullptr,"debugTexturePS","ps_4_0",
  D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&pixel,&error);
 if(FAILED(hr))return hr;
 ComPtr<ID3D11VertexShader> vertex_shader;ComPtr<ID3D11PixelShader> pixel_shader;
 ComPtr<ID3D11Buffer> constants;ComPtr<ID3D11SamplerState> sampler;
 if(FAILED(hr=device_->CreateVertexShader(vertex->GetBufferPointer(),
  vertex->GetBufferSize(),nullptr,&vertex_shader)))return hr;
 if(FAILED(hr=device_->CreatePixelShader(pixel->GetBufferPointer(),
  pixel->GetBufferSize(),nullptr,&pixel_shader)))return hr;
 D3D11_BUFFER_DESC constant_description{};
 constant_description.ByteWidth=sizeof(DebugViewConstants);
 constant_description.Usage=D3D11_USAGE_DYNAMIC;
 constant_description.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
 constant_description.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
 if(FAILED(hr=device_->CreateBuffer(&constant_description,nullptr,&constants)))return hr;
 D3D11_SAMPLER_DESC sampler_description{};
 sampler_description.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
 sampler_description.AddressU=sampler_description.AddressV=
  sampler_description.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
 sampler_description.MaxLOD=D3D11_FLOAT32_MAX;
 if(FAILED(hr=device_->CreateSamplerState(&sampler_description,&sampler)))return hr;
 debug_texture_vs_=std::move(vertex_shader);debug_texture_ps_=std::move(pixel_shader);
 debug_texture_constants_=std::move(constants);debug_texture_sampler_=std::move(sampler);
 ++debug_resource_view_work_.pipeline_creations;
 return S_OK;
}

void Renderer::remember_debug_projection(const FixedState& state,
                                         SemanticKind semantic_kind) noexcept {
 const bool world=semantic_kind==SemanticKind::figure||
  semantic_kind==SemanticKind::terrain||semantic_kind==SemanticKind::simple_geometry;
 if(!world)return;
 const D3DMATRIX& projection=state.transform[D3DTRANSFORMSTATE_PROJECTION];
 const std::array<float,4> coefficients{
  projection._33,projection._43,projection._34,projection._44};
 bool valid=true;for(float value:coefficients)valid&=std::isfinite(value);
 const float a=coefficients[0],b=coefficients[1],c=coefficients[2],d=coefficients[3];
 valid&=std::fabs(a)>1e-8f&&std::fabs(c-a)>1e-8f;
 const float near_z=valid?b/-a:0.0f;
 const float far_z=valid?(b-d)/(c-a):0.0f;
 valid&=std::isfinite(near_z)&&std::isfinite(far_z)&&near_z>=0.0f&&far_z>near_z;
 debug_projection_coefficients_=coefficients;debug_projection_valid_=valid;
}

DebugResourceViewAvailability Renderer::debug_resource_view_availability() const noexcept {
 DebugResourceViewAvailability result{};
 if(d3d12_){
  result.raw_depth=world_d3d12_!=nullptr;
  result.linearized_depth=result.raw_depth&&debug_projection_valid_;
  result.stencil=result.raw_depth; // world target uses D24S8
  result.ambient_occlusion=ao12_.filtered!=nullptr;
  result.shadows=shadow12_.maps!=nullptr;
  result.shadow_cascade_coverage=native12_.ready;
  return result;
 }
 result.raw_depth=depth_srv_!=nullptr;
 result.linearized_depth=result.raw_depth&&debug_projection_valid_;
 if(depth_){D3D11_TEXTURE2D_DESC description{};depth_->GetDesc(&description);
  result.stencil=description.Format==DXGI_FORMAT_R24G8_TYPELESS&&
   (description.BindFlags&D3D11_BIND_SHADER_RESOURCE)!=0;}
 result.ambient_occlusion=ao_srv_!=nullptr;
 result.shadows=native_shadow_depth_srv_!=nullptr;
 result.shadow_cascade_coverage=native_ps_!=nullptr;
 return result;
}

DebugShadowInfo Renderer::debug_shadow_info() const noexcept {
 DebugShadowInfo result{};
 result.feature_enabled=native_shadows_enabled_;
 result.pass_in_progress=native_shadow_collecting_;
 if(d3d12_?!shadow12_.maps:!native_shadow_depth_srv_)return result;
 if(!native_shadow_collecting_&&!native_shadow_failed_)
  result.rewritten_mask=native_shadow_cleared_mask_;
 for(unsigned i=0;i<5;++i){
  const auto& source=native_shadow_frame_.slices[i];
  if(!source.enabled)continue;
  result.enabled_mask|=1u<<i;
  if(native_shadow_valid_[i]&&!native_shadow_collecting_&&!native_shadow_failed_)
   result.valid_mask|=1u<<i;
  auto& slice=result.slices[i];
  slice.world_units_per_texel=source.bias[0];slice.radius=source.bias[0]*(native_shadow_side_*.5f);
  slice.receiver_minimum=source.receiver_minimum;slice.receiver_maximum=source.receiver_maximum;
  const float depth_span=1.f/source.bias[1];
  slice.light_depth={-source.world_to_light[11]*depth_span,
                    (1.f-source.world_to_light[11])*depth_span};
  slice.caster_revision_entries=source.validity.size();
 }
 return result;
}

HRESULT Renderer::render_debug_texture_d3d12(DebugResourceView view) noexcept {
 std::uint32_t mode=0;
 switch(view){
 case DebugResourceView::raw_depth:mode=1;break;
 case DebugResourceView::linearized_depth:mode=2;break;
 case DebugResourceView::stencil:mode=3;break;
 case DebugResourceView::ambient_occlusion:mode=4;break;
 case DebugResourceView::shadows:mode=5;break;
 default:return E_INVALIDARG;
 }
 DebugViewConstants constants{};constants.projection=debug_projection_coefficients_;constants.mode=mode;
 if(mode==5){
  const auto info=debug_shadow_info();
  constants.shadow_enabled=info.enabled_mask;constants.shadow_valid=info.valid_mask;
  const float width=float(target_width()),height=float(target_height());
  const float side=(std::min)(width/3.f,height/2.f);
  constants.shadow_board={(width-3*side)*.5f,(height-2*side)*.5f,side,0};
  for(unsigned i=0;i<5;++i){
   if((info.valid_mask&(1u<<i))==0)continue;
   const auto& slice=native_shadow_frame_.slices[i];
   const float inverse_radius=1.f/info.slices[i].radius;
   constants.receiver_rectangle[i]={
    .5f+.5f*(slice.receiver_minimum[0]*inverse_radius+slice.world_to_light[3]),
    .5f-.5f*(slice.receiver_maximum[1]*inverse_radius+slice.world_to_light[7]),
    .5f+.5f*(slice.receiver_maximum[0]*inverse_radius+slice.world_to_light[3]),
    .5f-.5f*(slice.receiver_minimum[1]*inverse_radius+slice.world_to_light[7])};
  }
 }
 const std::string_view shader(kDebugResourceViewShader,sizeof(kDebugResourceViewShader)-1);
 const ShaderProgram vertex{shader,nullptr,nullptr,"debugTextureVS","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3};
 const ShaderProgram pixel{shader,nullptr,nullptr,"debugTexturePS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3};
 D3D12NativeState state{};
 state.raster.FillMode=D3D11_FILL_SOLID;state.raster.CullMode=D3D11_CULL_NONE;state.raster.DepthClipEnable=TRUE;
 D3D11_SAMPLER_DESC sampler{};sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
 sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
 sampler.MaxLOD=D3D11_FLOAT32_MAX;
 d3d12_->clear_slots();d3d12_->set_target(nullptr);
 d3d12_->set_constants(0,&constants,sizeof(constants));d3d12_->set_sampler(0,sampler);
 if(mode==5)d3d12_->set_resource(2,shadow12_.maps.get());
 else if(mode==4)d3d12_->set_resource(0,ao12_.filtered.get());
 else if(mode==3)d3d12_->set_stencil_resource(1,world_d3d12_.get());
 else d3d12_->set_depth_resource(0,world_d3d12_.get());
 const HRESULT hr=d3d12_->draw(vertex,pixel,state,nullptr,3);
 d3d12_->clear_slots();
 if(hr==S_OK){++debug_resource_view_work_.draws;profiler_.add_draw(1,1);profiler_.add_batch();}
 return hr;
}
HRESULT Renderer::render_debug_texture(DebugResourceView view) noexcept {
 if(d3d12_)return render_debug_texture_d3d12(view);
 HRESULT hr=ensure_debug_texture_pipeline();if(FAILED(hr))return hr;
 if(view==DebugResourceView::stencil&&FAILED(hr=ensure_debug_stencil_view()))return hr;
 ID3D11ShaderResourceView* source=nullptr;std::uint32_t mode=0;
 switch(view){
 case DebugResourceView::raw_depth:source=depth_srv_.Get();mode=1;break;
 case DebugResourceView::linearized_depth:source=depth_srv_.Get();mode=2;break;
 case DebugResourceView::stencil:source=debug_stencil_srv_.Get();mode=3;break;
 case DebugResourceView::ambient_occlusion:source=ao_srv_.Get();mode=4;break;
 case DebugResourceView::shadows:source=native_shadow_depth_srv_.Get();mode=5;break;
 default:return E_INVALIDARG;
 }
 if(!source||!rtv_||!context_)return E_UNEXPECTED;
 // Binds every state it reads and restores nothing; the overlay and cursor
 // passes after it bind their own.
 bind_plan_targets(plan_pass("debug_resource_view",rtv_.Get(),nullptr));
 context_->OMSetBlendState(nullptr,nullptr,0xffffffffu);
 context_->OMSetDepthStencilState(nullptr,0);
 context_->RSSetState(nullptr);
 D3D11_VIEWPORT viewport{0,0,static_cast<float>(target_width()),static_cast<float>(target_height()),0,1};
 context_->RSSetViewports(1,&viewport);
 context_->RSSetScissorRects(0,nullptr);
 context_->IASetInputLayout(nullptr);
 context_->IASetVertexBuffers(0,0,nullptr,nullptr,nullptr);
 context_->IASetIndexBuffer(nullptr,DXGI_FORMAT_UNKNOWN,0);
 context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
 context_->VSSetShader(debug_texture_vs_.Get(),nullptr,0);
 context_->PSSetShader(debug_texture_ps_.Get(),nullptr,0);
 context_->GSSetShader(nullptr,nullptr,0);context_->HSSetShader(nullptr,nullptr,0);
 context_->DSSetShader(nullptr,nullptr,0);
 DebugViewConstants constants{};constants.projection=debug_projection_coefficients_;constants.mode=mode;
 if(mode==5){
  const auto info=debug_shadow_info();
  constants.shadow_enabled=info.enabled_mask;constants.shadow_valid=info.valid_mask;
  const float board_width=float(target_width()),board_height=float(target_height());
  const float side=(std::min)(board_width/3.f,board_height/2.f);
  constants.shadow_board={(board_width-3*side)*.5f,(board_height-2*side)*.5f,side,0};
  for(unsigned i=0;i<5;++i){
   if((info.valid_mask&(1u<<i))==0)continue;
   const auto& slice=native_shadow_frame_.slices[i];
   const float inverse_radius=1.f/info.slices[i].radius;
   constants.receiver_rectangle[i]={
    .5f+.5f*(slice.receiver_minimum[0]*inverse_radius+slice.world_to_light[3]),
    .5f-.5f*(slice.receiver_maximum[1]*inverse_radius+slice.world_to_light[7]),
    .5f+.5f*(slice.receiver_maximum[0]*inverse_radius+slice.world_to_light[3]),
    .5f-.5f*(slice.receiver_minimum[1]*inverse_radius+slice.world_to_light[7])};
  }
 }
 D3D11_MAPPED_SUBRESOURCE mapped{};
 if(FAILED(hr=context_->Map(debug_texture_constants_.Get(),0,
  D3D11_MAP_WRITE_DISCARD,0,&mapped)))return hr;
 std::memcpy(mapped.pData,&constants,sizeof(constants));
 context_->Unmap(debug_texture_constants_.Get(),0);
 ID3D11Buffer* constant=debug_texture_constants_.Get();
 context_->PSSetConstantBuffers(0,1,&constant);
 ID3D11ShaderResourceView* resources[3]{};
 resources[mode==5?2:(mode==3?1:0)]=source;
 context_->PSSetShaderResources(0,3,resources);
 ID3D11SamplerState* sampler=debug_texture_sampler_.Get();
 context_->PSSetSamplers(0,1,&sampler);
 context_->Draw(3,0);
 ID3D11ShaderResourceView* null_resources[3]{};
 context_->PSSetShaderResources(0,3,null_resources);
 ++debug_resource_view_work_.draws;
 return device_->GetDeviceRemovedReason();
}

HRESULT Renderer::render_debug_resource_view(DebugResourceView view,
                                             DebugOverlayMode mode) noexcept {
 // Coverage is emitted by the existing native color draw, with no extra pass.
 if(mode!=DebugOverlayMode::full||view==DebugResourceView::disabled||
    view==DebugResourceView::shadow_cascade_coverage)return S_FALSE;
 const auto availability=debug_resource_view_availability();
 if(!availability.available(view))return S_FALSE;
 const HRESULT hr=render_debug_texture(view);
 if(hr==S_OK)++debug_resource_view_work_.passes;
 return hr;
}

#endif
void append_debug_resource_shader_programs(std::vector<ShaderProgram>& out){
 const std::string_view shader(kDebugResourceViewShader,sizeof(kDebugResourceViewShader)-1);
 out.push_back({shader,nullptr,nullptr,"debugTextureVS","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 out.push_back({shader,nullptr,nullptr,"debugTexturePS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
}
// Feature level 9_3 interface programs: the game pipeline (vsMain, psMain),
// then the screen rectangle.
#pragma warning(push)
#pragma warning(error:4062)   // every named program has its case
ShaderProgram interface_shader_program(InterfaceShader shader){
 const std::string_view game(kShader,sizeof(kShader)-1),rectangle(kScreenRectangleShader,sizeof(kScreenRectangleShader)-1);
 switch(shader){
 case InterfaceShader::game_vs:return {game,nullptr,nullptr,"vsMain","vs_4_0_level_9_3",0};
 case InterfaceShader::game_ps:return {game,nullptr,nullptr,"psMain","ps_4_0_level_9_3",0};
 case InterfaceShader::rectangle_vs:return {rectangle,nullptr,nullptr,"rectangleVS","vs_4_0_level_9_3",0};
 case InterfaceShader::rectangle_ps:return {rectangle,nullptr,nullptr,"rectanglePS","ps_4_0_level_9_3",0};
 case InterfaceShader::backdrop_ps:return {{kBackdropShader,sizeof(kBackdropShader)-1},nullptr,nullptr,"backdropPS","ps_4_0_level_9_3",0};
 case InterfaceShader::count:break;
 }
 return {};
}

// Feature level 11 world support: the depth/colour clear, then game particles.
ShaderProgram world_support_shader_program(WorldSupportShader shader){
 static const std::string particles=std::string(kAtmosphereShader)+kTerrainCliffShader+kGameParticleShader+kTerrainPrintShader;
 const std::string_view clear(kClearShader,sizeof(kClearShader)-1),scroll(kShadowScrollShader,sizeof(kShadowScrollShader)-1);
 constexpr UINT optimized=D3DCOMPILE_OPTIMIZATION_LEVEL3;
 switch(shader){
 case WorldSupportShader::clear_vs:return {clear,nullptr,nullptr,"clearVS","vs_5_0",0};
 case WorldSupportShader::clear_ps:return {clear,nullptr,nullptr,"clearPS","ps_5_0",0};
 case WorldSupportShader::game_particle_vs:return {particles,"game_particles",nullptr,"gameParticleVS","vs_5_0",optimized};
 case WorldSupportShader::game_particle_ps:return {particles,"game_particles",nullptr,"gameParticlePS","ps_5_0",optimized};
 case WorldSupportShader::shadow_scroll_vs:return {scroll,nullptr,nullptr,"shadowScrollVS","vs_5_0",0};
 case WorldSupportShader::shadow_scroll_ps:return {scroll,nullptr,nullptr,"shadowScrollPS","ps_5_0",0};
 case WorldSupportShader::interface_scaled_ps:
  return {{kInterfaceScaledShader,sizeof(kInterfaceScaledShader)-1},nullptr,nullptr,"interfaceScaledPS","ps_5_0",0};
 case WorldSupportShader::game_particle_depth_ps:return {particles,"game_particles",nullptr,"gameParticleDepthPS","ps_5_0",optimized};
 case WorldSupportShader::game_ribbon_vs:return {particles,"game_particles",nullptr,"gameRibbonVS","vs_5_0",optimized};
 case WorldSupportShader::game_selection_vs:return {particles,"game_particles",nullptr,"gameSelectionVS","vs_5_0",optimized};
 case WorldSupportShader::game_mesh_vs:return {particles,"game_particles",nullptr,"gameMeshVS","vs_5_0",optimized};
 case WorldSupportShader::game_glyph_vs:return {particles,"game_particles",nullptr,"gameGlyphVS","vs_5_0",optimized};
 case WorldSupportShader::interface_text_ps:
  return {{kInterfaceTextShader,sizeof(kInterfaceTextShader)-1},nullptr,nullptr,"interfaceTextPS","ps_5_0",0};
 case WorldSupportShader::game_particle_debug_ps:{
  static const D3D_SHADER_MACRO debug[]={{"GP_SOFT_DEBUG","1"},{nullptr,nullptr}};
  return {particles,"game_particles",debug,"gameParticlePS","ps_5_0",optimized};}
 case WorldSupportShader::terrain_print_vs:return {particles,"game_particles",nullptr,"terrainPrintVS","vs_5_0",optimized};
 case WorldSupportShader::terrain_print_hs:return {particles,"game_particles",nullptr,"terrainPrintHS","hs_5_0",optimized};
 case WorldSupportShader::terrain_print_ds:return {particles,"game_particles",nullptr,"terrainPrintDS","ds_5_0",optimized};
 case WorldSupportShader::count:break;
 }
 return {};
}
#pragma warning(pop)
static_assert(static_cast<std::size_t>(InterfaceShader::count)==5,"interface list size changed: name the new program");
static_assert(static_cast<std::size_t>(WorldSupportShader::count)==17,"world-support list size changed: name the new program");

// The preparation lists: every named program, in enum order.
void append_interface_shader_programs(std::vector<ShaderProgram>& out){
 for(std::size_t i=0;i<static_cast<std::size_t>(InterfaceShader::count);++i)
  out.push_back(interface_shader_program(static_cast<InterfaceShader>(i)));
}

void append_world_support_shader_programs(std::vector<ShaderProgram>& out){
 for(std::size_t i=0;i<static_cast<std::size_t>(WorldSupportShader::count);++i)
  out.push_back(world_support_shader_program(static_cast<WorldSupportShader>(i)));
}
namespace {

// Program groups and the option that turns each on, with the fallbacks the
// features themselves use. Quality levels inside a feature share programs.
// The first two groups serve every feature level; the rest need 11_0.
// name is the breadcrumb (shader crash marker) and log name. option names
// the feature to the player and disable turns it off after the driver
// crashed creating the group's programs (null: the renderer needs the group).
struct ShaderGroup {
 const char* name;
 void (*append)(std::vector<ShaderProgram>&);
#if defined(EID3D11_SHADER_EMBED)
 // The offline list needs every producer, without runtime settings callbacks.
 template<class Used,class Disable>
 constexpr ShaderGroup(const char* n,void (*a)(std::vector<ShaderProgram>&),Used,const char*,Disable)
  :name(n),append(a){}
#else
 bool (*used)(const RendererConfig&);
 const char* option;
 void (*disable)(RendererConfig&);
#endif
};
constexpr ShaderGroup kShaderGroups[]={
 {"interface",append_interface_shader_programs,[](const RendererConfig&){return true;},nullptr,nullptr},
 {"scene_composite",SceneComposite::shader_programs,[](const RendererConfig&){return true;},nullptr,nullptr},
 {"world_support",append_world_support_shader_programs,[](const RendererConfig&){return true;},nullptr,nullptr},
 {"native",append_native_shader_programs,[](const RendererConfig&){return true;},nullptr,nullptr},
 {"liquid_waves",append_liquid_wave_shader_programs,[](const RendererConfig&){return true;},nullptr,nullptr},
 {"interact_overlay",append_interact_overlay_shader_programs,[](const RendererConfig&){return true;},nullptr,nullptr},
 {"occlusion_culling",append_hiz_shader_programs,
  [](const RendererConfig& c){return configured_boolean(c,"occlusion_culling",true);},
  "occlusion_culling",[](RendererConfig& c){c.set_boolean("occlusion_culling",false);}},
 {"shadow_early_out",append_shadow_range_shader_programs,
  [](const RendererConfig& c){return configured_boolean(c,"shadows",true)&&configured_boolean(c,"shadow_early_out",true);},
  "shadow_early_out",[](RendererConfig& c){c.set_boolean("shadow_early_out",false);}},
 {"terrain_tessellation",append_terrain_tessellation_shader_programs,
  [](const RendererConfig& c){return configured_integer(c,"terrain_tessellation",1)>0;},
  "terrain_tessellation",[](RendererConfig& c){c.set_integer("terrain_tessellation",0);}},
 {"ambient_occlusion",append_ambient_occlusion_shader_programs,
  [](const RendererConfig& c){return configured_integer(c,"ao_quality",2)>0;},
  "ao_quality",[](RendererConfig& c){c.set_integer("ao_quality",0);}},
 {"sky_clouds",append_sky_shader_programs,
  [](const RendererConfig& c){return configured_integer(c,"sky_clouds",2)>0;},
  "sky_clouds",[](RendererConfig& c){c.set_integer("sky_clouds",0);}},
 {"fog_volume",append_fog_volume_shader_programs,
  [](const RendererConfig& c){return configured_integer(c,"fog_preset",2)>=2;},
  "fog_preset",[](RendererConfig& c){c.set_integer("fog_preset",1);}},
 {"sun_shafts",SunRaysPass::shader_programs,
  [](const RendererConfig& c){return configured_integer(c,"sun_shafts",2)>0;},
  "sun_shafts",[](RendererConfig& c){c.set_integer("sun_shafts",0);}},
 {"bloom",BloomPass::shader_programs,
  [](const RendererConfig& c){return configured_integer(c,"bloom",0)>0;},
  "bloom",[](RendererConfig& c){c.set_integer("bloom",0);}},
 {"ambient_particles",append_ambient_particle_shader_programs,
  [](const RendererConfig& c){return configured_integer(c,"ambient_particles",1)>0;},
  "ambient_particles",[](RendererConfig& c){c.set_integer("ambient_particles",0);}},
 {"wind_trails",append_wind_trail_shader_programs,
  [](const RendererConfig& c){return configured_integer(c,"wind_trails",1)>0;},
  "wind_trails",[](RendererConfig& c){c.set_integer("wind_trails",0);}},
 {"dune_wisps",append_dune_wisp_shader_programs,
  [](const RendererConfig& c){return configured_integer(c,"dune_wisps",1)>0;},
  "dune_wisps",[](RendererConfig& c){c.set_integer("dune_wisps",0);}},
 {"precipitation",append_precipitation_shader_programs,
  [](const RendererConfig& c){return configured_integer(c,"precipitation",3)>0;},
  "precipitation",[](RendererConfig& c){c.set_integer("precipitation",0);}},
 {"ground_cover",append_ground_cover_shader_programs,
  [](const RendererConfig& c){return configured_integer(c,"ground_cover",2)>0;},
  "ground_cover",[](RendererConfig& c){c.set_integer("ground_cover",0);}},
 {"critters",append_critter_shader_programs,
  [](const RendererConfig& c){
  return configured_integer(c,"critters",2)>0||configured_integer(c,"critter_showcase",0)>0;},
  "critters",[](RendererConfig& c){c.set_integer("critters",0);c.set_integer("critter_showcase",0);}},
 {"impostors",append_impostor_shader_programs,
  [](const RendererConfig& c){return configured_boolean(c,"impostors",true)&&configured_boolean(c,"neighbour_maps",true);},
  "impostors",[](RendererConfig& c){c.set_boolean("impostors",false);}},
 {"depth_of_field",DepthOfFieldPass::shader_programs,
  [](const RendererConfig& c){return configured_integer(c,"depth_of_field",0)>0;},
  "depth_of_field",[](RendererConfig& c){c.set_integer("depth_of_field",0);}},
 {"fsr2",Fsr2Upscaler::shader_programs,
  // FSR 2's programs: FSR chosen, or DLSS that can run here with
  // sharpening on (its RCAS program sharpens after DLSS). Nothing for a
  // DLSS choice the device cannot run (standard mode).
  [](const RendererConfig& c){
   const int method=configured_integer(c,"upscaler",kUpscalerOff);
   return method==kUpscalerFsr||(method==kUpscalerDlss&&!dlss_unavailable_reason()&&
    configured_floating_point(c,"upscale_sharpness",.2f)>0.f);},
  "upscaler",[](RendererConfig& c){c.set_integer("upscaler",kUpscalerOff);}},
 {"terrain2",[](std::vector<ShaderProgram>& out){append_terrain2_shader_programs(out);append_terrain2_cover_programs(out);append_snow_surface_program(out);},[](const RendererConfig&){return true;},nullptr,nullptr},
 // The material cache's bake, with terrain 2.0 (its objects are created after the preparation).
 {"terrain2_cache",append_terrain2_cache_shader_programs,[](const RendererConfig& c){return configured_integer(c,"terrain2_cache",1)>0;},
  "terrain2_cache",[](RendererConfig& c){c.set_integer("terrain2_cache",0);}},
 // The face cache's bake (terrain_face_atlas.h).
 {"terrain_face_cache",append_terrain_face_cache_shader_programs,[](const RendererConfig& c){return configured_integer(c,"terrain_face_cache",1)>0;},
  "terrain_face_cache",[](RendererConfig& c){c.set_integer("terrain_face_cache",0);}},
 {"cave_gi",append_cave_irradiance_shader_programs,
  [](const RendererConfig& c){return configured_integer(c,"cave_gi",2)>0;},
  "cave_gi",[](RendererConfig& c){c.set_integer("cave_gi",0);}},
 {"fur",append_fur_shader_programs,
  [](const RendererConfig& c){return configured_integer(c,"fur_mode",0)>0;},
  "fur_mode",[](RendererConfig& c){c.set_integer("fur_mode",0);}},
};
static_assert(std::size(kShaderGroups)<=32,"one bit per shader group");
}  // namespace

#if !defined(EID3D11_SHADER_EMBED)
std::uint32_t shader_groups_for_settings(const RendererConfig& config,bool feature_level_11){
 std::uint32_t groups=0;
 for(std::uint32_t group=0;group<std::size(kShaderGroups);++group)
  if((group<2||feature_level_11)&&kShaderGroups[group].used(config))groups|=1u<<group;
 return groups;
}

#endif
void append_shader_groups(std::uint32_t groups,std::vector<ShaderProgram>& out){
 for(std::uint32_t group=0;group<std::size(kShaderGroups);++group)
  if(groups&(1u<<group))kShaderGroups[group].append(out);
}
#if !defined(EID3D11_SHADER_EMBED)
namespace {

// The shader crash breadcrumb beside the ini. Workers write it when the
// highest program group handed to the driver advances, so it names the last
// group the driver received (its compiler may still be working on it on a
// thread of its own); the presenting thread deletes it after a completed
// present. A few small writes per preparation.
struct ShaderMarker {
 std::mutex mutex;
 int group=-1;  // highest kShaderGroups index handed in the open preparation
 const char* phase="preparation";
 unsigned workers=0;  // preparation workers handing programs to the driver
 const std::vector<std::uint8_t>* program_groups=nullptr;  // group of each program in the list
 bool abandoned=false;  // x64 host: the game process is gone; no marker is written again
};
ShaderMarker& shader_marker() noexcept{static ShaderMarker marker;return marker;}

std::wstring shader_marker_path(){
 std::wstring path=renderer_config_path();
 const std::size_t slash=path.find_last_of(L"\\/");
 // One marker per writer: the x64 host (the only 64-bit build of this file)
 // and the in-process renderer share the ini folder, and each recovers only
 // from its own preparations. A tester's in-process start read a marker the
 // killed host had left and switched to serial preparation for good.
#if defined(_WIN64)
 const wchar_t* const name=L"eid3d11_shader_crash_x64.txt";
#else
 const wchar_t* const name=L"eid3d11_shader_crash.txt";
#endif
 return (slash==std::wstring::npos?std::wstring():path.substr(0,slash+1))+name;
}

// Caller holds the marker's mutex.
void write_shader_marker(const ShaderMarker& marker) noexcept try{
 if(marker.group<0||marker.abandoned)return;
 char text[128];
 const int length=std::snprintf(text,sizeof(text),"group=%s\r\nphase=%s\r\nworkers=%u\r\n",kShaderGroups[marker.group].name,
  marker.phase,marker.workers);
 const HANDLE file=CreateFileW(shader_marker_path().c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(file==INVALID_HANDLE_VALUE)return;
 DWORD written=0;WriteFile(file,text,static_cast<DWORD>(std::max(length,0)),&written,nullptr);
 CloseHandle(file);
}catch(...){}

void shader_handoff(void*,std::size_t index) noexcept{
 ShaderMarker& marker=shader_marker();
 std::lock_guard<std::mutex> lock(marker.mutex);
 if(!marker.program_groups||index>=marker.program_groups->size())return;
 const int group=(*marker.program_groups)[index];
 if(group<=marker.group)return;
 marker.group=group;
 write_shader_marker(marker);
}

// The value after "key=" on its own line, or empty.
std::string marker_value(const std::string& text,const char* key){
 const std::string prefix=std::string(key)+"=";
 for(std::size_t line=0;line<text.size();){
  const std::size_t end=std::min(text.find_first_of("\r\n",line),text.size());
  if(text.compare(line,prefix.size(),prefix)==0)return text.substr(line+prefix.size(),end-line-prefix.size());
  line=text.find_first_not_of("\r\n",end);if(line==std::string::npos)break;
 }
 return {};
}
}  // namespace

// The x64 host calls this once the game process is gone: a preparation it
// is in was not ended by a driver crash (the game was closed or killed), so
// its marker must not turn on crash recovery at the next start.
void abandon_shader_marker() noexcept{
 ShaderMarker& marker=shader_marker();
 std::lock_guard<std::mutex> lock(marker.mutex);
 marker.abandoned=true;
 try{DeleteFileW(shader_marker_path().c_str());}catch(...){}
}

void Renderer::close_shader_marker() noexcept{
 if(!shader_marker_open_.exchange(false))return;
 ShaderMarker& marker=shader_marker();
 std::lock_guard<std::mutex> lock(marker.mutex);
 marker.group=-1;marker.program_groups=nullptr;
 try{DeleteFileW(shader_marker_path().c_str());}catch(...){}
}

// Runs once per device, after the ini is loaded and before the first
// preparation. First step: a crash under parallel preparation switches it to
// one worker for good and turns nothing off (old drivers' compilers can fail
// on concurrent creates even when no single program is at fault). A crash
// under serial preparation turns off one optional group per crashed start:
// the marked group when it is optional and on, else the nearest earlier one
// that is (the driver may still have been compiling an earlier group); each
// further crash walks one group back until a start completes its first
// present. A required group leaves nothing to turn off: said, never hidden.
void Renderer::recover_shader_crash() noexcept try{
 const std::wstring path=shader_marker_path();
 std::string text;
 {const HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(file==INVALID_HANDLE_VALUE)return;
  char buffer[256];DWORD read=0;
  if(ReadFile(file,buffer,sizeof(buffer),&read,nullptr))text.assign(buffer,read);
  CloseHandle(file);}
 DeleteFileW(path.c_str());
 const std::string group_name=marker_value(text,"group"),phase=marker_value(text,"phase"),workers=marker_value(text,"workers");
 auto& config=debug_overlay_.config();
 const char* shown_group=group_name.empty()?"?":group_name.c_str();
 char detail[256];
 if(workers!="1"){
  config.set_serial_shader_preparation(true);
  if(config.save(renderer_config_path().c_str()))config.clear_dirty();
  std::snprintf(detail,sizeof(detail),"group=%s phase=%s workers=%s disabled=none preparation=serial_from_now_on",
   group_name.empty()?"unknown":group_name.c_str(),phase.empty()?"unknown":phase.c_str(),workers.empty()?"unknown":workers.c_str());
  bridge_report_event("startup_shader_crash_recovered",detail);
  char notice[1024];
  std::snprintf(notice,sizeof(notice),ui_text("The graphics driver crashed last time while preparing the renderer's shaders (%s). "
   "They are now prepared one at a time; if the driver crashes again, that feature will be turned off."),shown_group);
  debug_overlay_.add_graphics_reset_notice(notice);
  return;
 }
 const std::uint32_t enabled=shader_groups_for_settings(config,feature_level_>=D3D_FEATURE_LEVEL_11_0);
 int marked=-1;
 for(int group=0;group<static_cast<int>(std::size(kShaderGroups));++group)
  if(group_name==kShaderGroups[group].name)marked=group;
 int chosen=-1;
 for(int group=marked;group>=0;--group)
  if(kShaderGroups[group].disable&&(enabled&(1u<<group))){chosen=group;break;}
 std::string disabled="none";
 const char* label=nullptr;
 if(chosen>=0){
  const ShaderGroup& group=kShaderGroups[chosen];
  group.disable(config);
  if(group.used(config))disabled="none (option change failed)";
  else{
   disabled=std::string(group.name)+":"+group.option;
   if(config.save(renderer_config_path().c_str()))config.clear_dirty();
   const RendererOption* option=config.find(group.option);
   label=option?ui_option_label(*option):group.option;
  }
 }
 std::snprintf(detail,sizeof(detail),"group=%s phase=%s workers=1 disabled=%s preparation=serial",
  group_name.empty()?"unknown":group_name.c_str(),phase.empty()?"unknown":phase.c_str(),disabled.c_str());
 bridge_report_event("startup_shader_crash_recovered",detail);
 char notice[1024];
 if(label)
  std::snprintf(notice,sizeof(notice),ui_text("The graphics driver crashed last time while preparing the renderer's shaders (%s). "
   "Turned off: %s; it can be turned on again in F10 (a newer graphics driver may fix the crash)."),shown_group,label);
 else
  std::snprintf(notice,sizeof(notice),ui_text("The graphics driver crashed last time while preparing shaders the renderer cannot run "
   "without (%s). Nothing could be turned off; a newer graphics driver may fix the crash."),shown_group);
 debug_overlay_.add_graphics_reset_notice(notice);
}catch(...){}

namespace {
// The shader table the given groups reach (DX12 plan M0, pipeline table
// enumeration): programs per group and stage, and the distinct programs.
// Pipelines pair these with blend/raster/depth/topology/target states,
// which are not enumerated yet (unknown).
void report_shader_table(std::uint32_t groups,std::size_t programs_per_set,std::size_t prebuilt_specialised_sets) noexcept try{
 std::string per_group;std::vector<ShaderKey> keys;
 unsigned stages[6]{};const char* const stage_names="vpchdg";
 std::size_t programs=0;
 for(std::uint32_t group=0;group<std::size(kShaderGroups);++group){
  if(!(groups&(1u<<group)))continue;
  std::vector<ShaderProgram> list;kShaderGroups[group].append(list);
  programs+=list.size();
  per_group+=' ';per_group+=kShaderGroups[group].name;per_group+='=';per_group+=std::to_string(list.size());
  for(const auto& program:list){
   const ShaderKey key=shader_key(program);
   if(std::find(keys.begin(),keys.end(),key)!=keys.end())continue;
   keys.push_back(key);
   if(const char* stage=std::strchr(stage_names,program.profile[0]))++stages[stage-stage_names];
  }
 }
 char head[256];
 std::snprintf(head,sizeof(head),"programs=%zu distinct=%zu vs=%u ps=%u cs=%u hs=%u ds=%u gs=%u specialised_ps=up to 8 sets x %zu (prebuilt shipped sets %zu) groups:",
  programs,keys.size(),stages[0],stages[1],stages[2],stages[3],stages[4],stages[5],
  programs_per_set,prebuilt_specialised_sets);
 bridge_report_event("shader_table",(head+per_group).c_str());
}catch(...){}
}  // namespace

void Renderer::prepare_shaders() noexcept try{
 namespace detail=shader_cache_detail;
 // What the current settings can reach, reported once a settings change
 // (preparation time only, never per frame).
 if(const std::uint32_t reachable=shader_groups_for_settings(debug_overlay_.config(),feature_level_>=D3D_FEATURE_LEVEL_11_0);
    reachable!=shader_table_reported_){
  shader_table_reported_=reachable;report_shader_table(reachable,std::tuple_size<NativeSpecialisedSet>::value,native_spec_files_.size());
 }
 // A feature compiled outside a preparation (a program the embedded table
 // lacks and the group list misses) is a main-thread FXC stall: name it.
 if(const std::uint32_t on_demand=detail::on_demand_compiles.load();on_demand!=shader_on_demand_reported_){
  shader_on_demand_reported_=on_demand;const char* entry=detail::last_on_demand_entry.load();
  char detail_text[160];std::snprintf(detail_text,sizeof(detail_text),"compiles=%u last_entry=%s",on_demand,entry?entry:"?");
  bridge_report_event("shader_compiled_on_demand",detail_text);
 }
 // A shader object no preparation made: the driver translated it on the
 // creating thread (a first-use hitch). Named per new miss.
 if(shader_objects_)if(const std::uint32_t unprepared=shader_objects_->unprepared.load();unprepared!=shader_unprepared_reported_){
  shader_unprepared_reported_=unprepared;
  char detail_text[96];std::snprintf(detail_text,sizeof(detail_text),"creates=%u retained=%zu",unprepared,shader_objects_->size());
  bridge_report_event("shader_created_unprepared",detail_text);
 }
 const std::uint32_t groups=shader_groups_for_settings(debug_overlay_.config(),feature_level_>=D3D_FEATURE_LEVEL_11_0)&
  ~shader_groups_prepared_;
 if(!groups)return;
 std::vector<ShaderProgram> programs;std::vector<std::uint8_t> program_groups;
 for(std::uint32_t group=0;group<std::size(kShaderGroups);++group)
  if(groups&(1u<<group)){kShaderGroups[group].append(programs);program_groups.resize(programs.size(),static_cast<std::uint8_t>(group));}
 const std::int64_t begin=perf_now();
 // Workers create the shader objects (the driver translates the bytecode).
 // Match the specialisation pool: leave two logical processors for the
 // game, progress rendering and input. Keep crash recovery's serial mode.
 const unsigned cores=std::thread::hardware_concurrency();
 const unsigned available_threads=cores>2?cores-2:1;
 const bool serial=debug_overlay_.config().serial_shader_preparation()||!driver_concurrent_creates_;
 const unsigned threads=std::min<unsigned>(serial?1u:available_threads,static_cast<unsigned>(programs.size()));
 // The breadcrumb follows the handoffs from here until a completed present.
 {ShaderMarker& marker=shader_marker();std::lock_guard<std::mutex> lock(marker.mutex);
  marker.group=-1;marker.phase="preparation";marker.workers=std::min<unsigned>(threads,static_cast<unsigned>(programs.size()));
  marker.program_groups=&program_groups;shader_marker_open_=true;}
 ShaderPreparation preparation(std::move(programs),threads,device_.Get(),&shader_handoff,nullptr);
 bool shown=false;
 // Short preparations finish before a progress frame would help.
 const bool quick=preparation.wait_for(std::chrono::milliseconds(150));
 if(!quick&&!(swap_&&device_&&context_))
  while(!preparation.wait_for(std::chrono::seconds(1))){}
 else if(!quick){
  shown=true;
  // Runs between frames (after Present or at device creation): the targets
  // stay unbound after it; the next frame's passes bind their own.
  do{
   // Keeps Windows from marking the window as not responding without
   // dispatching anything: queued and sent messages wait for the game's loop.
   // Minimised: nothing to draw (Present would only report occlusion).
   MSG message;PeekMessageW(&message,nullptr,0,0,PM_NOREMOVE|PM_QS_INPUT|PM_QS_POSTMESSAGE|PM_QS_PAINT);
   ComPtr<ID3D11Texture2D> back;ComPtr<ID3D11RenderTargetView> target;
   if(!(hwnd_&&IsIconic(hwnd_))&&SUCCEEDED(swap_->GetBuffer(0,IID_PPV_ARGS(&back)))&&SUCCEEDED(device_->CreateRenderTargetView(back.Get(),nullptr,&target))){
    const float background[4]={.035f,.035f,.04f,1.f};
    context_->OMSetRenderTargets(1,target.GetAddressOf(),nullptr);
    context_->ClearRenderTargetView(target.Get(),background);
    if(debug_overlay_.initialized())debug_overlay_.render_shader_preparation(preparation.done(),preparation.total());
    context_->OMSetRenderTargets(0,nullptr,nullptr);
    swap_->Present(0,tearing_supported_?DXGI_PRESENT_ALLOW_TEARING:0u);
   }
  }while(!preparation.wait_for(std::chrono::milliseconds(16)));
  frame_statistics_.reset();
 }
 shader_groups_prepared_|=groups;
 // The driver may still compile on its own threads: the marker keeps the
 // last group, now in the after_preparation phase, until the present.
 {ShaderMarker& marker=shader_marker();std::lock_guard<std::mutex> lock(marker.mutex);
  marker.program_groups=nullptr;marker.phase="after_preparation";write_shader_marker(marker);}
 // The material cache's shader objects now, within this preparation, never
 // on its first bake frame mid-play.
 if(feature_level_>=D3D_FEATURE_LEVEL_11_0)(void)ensure_terrain2_cache_programs();
 const double ms=1000.0*static_cast<double>(perf_now()-begin)/static_cast<double>(perf_frequency());
 char text[224];
 std::snprintf(text,sizeof(text),"programs=%zu created=%zu compiled=%zu failed=%zu retained=%zu threads=%u ms=%.0f window=%s groups=0x%x",
  preparation.total(),preparation.created(),preparation.compiled(),preparation.failed(),shader_objects_?shader_objects_->size():0u,
  preparation.worker_count(),ms,shown?"shown":"not_needed",groups);
 bridge_report_event("shader_preparation",text);
}catch(...){
 // The features compile their programs themselves when preparation fails.
}

HRESULT Renderer::create_pipeline(){
 ComPtr<ID3DBlob> vb,pb,err;HRESULT hr=compile_shader(interface_shader_program(InterfaceShader::game_vs),&vb,&err);if(FAILED(hr))return hr;hr=compile_shader(interface_shader_program(InterfaceShader::game_ps),&pb,&err);if(FAILED(hr))return hr;if(FAILED(hr=create_shader(device_.Get(),vb->GetBufferPointer(),vb->GetBufferSize(),nullptr,&vs_)))return hr;if(FAILED(hr=create_shader(device_.Get(),pb->GetBufferPointer(),pb->GetBufferSize(),nullptr,&ps_)))return hr;
 {ComPtr<ID3DBlob> backdrop;if(FAILED(hr=compile_shader(interface_shader_program(InterfaceShader::backdrop_ps),&backdrop,&err)))return hr;if(FAILED(hr=create_shader(device_.Get(),backdrop->GetBufferPointer(),backdrop->GetBufferSize(),nullptr,&backdrop_ps_)))return hr;}
 D3D11_INPUT_ELEMENT_DESC il[]={{"POSITION",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},{"COLOR",0,DXGI_FORMAT_R8G8B8A8_UNORM,0,16,D3D11_INPUT_PER_VERTEX_DATA,0},{"COLOR",1,DXGI_FORMAT_R8G8B8A8_UNORM,0,20,D3D11_INPUT_PER_VERTEX_DATA,0},{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0}};
 if(FAILED(hr=device_->CreateInputLayout(il,ARRAYSIZE(il),vb->GetBufferPointer(),vb->GetBufferSize(),&layout_)))return hr;D3D11_BUFFER_DESC bd{};bd.ByteWidth=(sizeof(Constants)+15)&~15u;bd.Usage=D3D11_USAGE_DYNAMIC;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;bd.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;if(FAILED(hr=device_->CreateBuffer(&bd,nullptr,&constants_)))return hr;
 if(FAILED(hr=create_clear_pipeline()))return hr;
 // Game effect programs (particles, lightning ribbons, selection marker,
 // terrain prints) are created here from the embedded table, never on first
 // use, so the first fire, bolt or footprint in view does not hitch.
 if(feature_level_>=D3D_FEATURE_LEVEL_11_0){
  const HRESULT effects=ensure_game_particle_pipeline();
  char text[64];std::snprintf(text,sizeof(text),"%s hr=%08lx",SUCCEEDED(effects)?"created_at_device_init":"failed",
   static_cast<unsigned long>(effects));
  bridge_report_event("game_effect_pipeline",text);
 }
 // Native object-space rendering is optional. Compatibility rendering remains
 // available when the adapter exposes less than feature level 11 or creation fails.
 // A failure is named once (the native path then draws nothing; no silent fallback).
 if(const HRESULT native=create_native_pipeline();FAILED(native)){
  char text[64];std::snprintf(text,sizeof(text),"hr=%08lx",static_cast<unsigned long>(native));
  bridge_report_event("native_pipeline_failed",text);
 }
 return S_OK;
}

HRESULT Renderer::queue_screen_rectangle(const ScreenRectangleRequest& request,
                                         const D3DVIEWPORT7& viewport){
 if(scene_queue()&&core_mode_){core_post_command(scene_ui::ScreenRectangle{request,viewport},"queue_screen_rectangle");return S_OK;}
 if(scene_queue()){scene_post_frame([this,command=scene_ui::ScreenRectangle{request,viewport}]{report_scene_failure("queue_screen_rectangle",queue_screen_rectangle(command.request,command.viewport));});return S_OK;}
 // Before the composite the bound target is the world's (D3D12: the world
 // target): the rectangle waits for the composite as the first interface
 // output, never through SMAA, the post passes or the upscaler.
 if(d3d12_&&scene_composited_)return draw_screen_rectangle_d3d12(request,viewport);
 if(!d3d12_&&!context_)return E_FAIL;
 if(!d3d12_&&scene_composited_)return draw_screen_rectangle(request,viewport);
 try{pending_screen_rectangles_.push_back({request,viewport});}
 catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
 return S_OK;
}

HRESULT Renderer::draw_screen_rectangle(const ScreenRectangleRequest& request,
                                        const D3DVIEWPORT7& viewport,float grid_step){
 if(!device_||!context_||!rtv_)return E_FAIL;
 HRESULT hr=S_OK;
 if(!screen_rectangle_vs_){
  ComPtr<ID3DBlob> vertex,pixel,error;
  if(FAILED(hr=compile_shader(interface_shader_program(InterfaceShader::rectangle_vs),&vertex,&error)))return hr;
  if(FAILED(hr=compile_shader(interface_shader_program(InterfaceShader::rectangle_ps),&pixel,&error)))return hr;
  ComPtr<ID3D11VertexShader> vertex_shader;ComPtr<ID3D11PixelShader> pixel_shader;
  ComPtr<ID3D11InputLayout> layout;ComPtr<ID3D11Buffer> vertices;
  ComPtr<ID3D11RasterizerState> raster;ComPtr<ID3D11BlendState> blend;
  ComPtr<ID3D11DepthStencilState> depth;
  if(FAILED(hr=create_shader(device_.Get(),vertex->GetBufferPointer(),vertex->GetBufferSize(),
   nullptr,&vertex_shader)))return hr;
  if(FAILED(hr=create_shader(device_.Get(),pixel->GetBufferPointer(),pixel->GetBufferSize(),
   nullptr,&pixel_shader)))return hr;
  const D3D11_INPUT_ELEMENT_DESC elements[]={
   {"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
   {"COLOR",0,DXGI_FORMAT_R8G8B8A8_UNORM,0,8,D3D11_INPUT_PER_VERTEX_DATA,0}};
  if(FAILED(hr=device_->CreateInputLayout(elements,ARRAYSIZE(elements),
   vertex->GetBufferPointer(),vertex->GetBufferSize(),&layout)))return hr;
  D3D11_BUFFER_DESC buffer{};buffer.ByteWidth=sizeof(ScreenRectangleVertex)*kScreenRectangleMaximumVertices;
  buffer.Usage=D3D11_USAGE_DYNAMIC;buffer.BindFlags=D3D11_BIND_VERTEX_BUFFER;
  buffer.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
  if(FAILED(hr=device_->CreateBuffer(&buffer,nullptr,&vertices)))return hr;
  D3D11_RASTERIZER_DESC raster_description{};raster_description.FillMode=D3D11_FILL_SOLID;
  raster_description.CullMode=D3D11_CULL_NONE;raster_description.DepthClipEnable=TRUE;
  if(FAILED(hr=device_->CreateRasterizerState(&raster_description,&raster)))return hr;
  // The stock fill's alpha ramp (fades) blends source-alpha over the frame.
  D3D11_BLEND_DESC blend_description{};auto& target=blend_description.RenderTarget[0];
  target.BlendEnable=TRUE;target.SrcBlend=target.SrcBlendAlpha=D3D11_BLEND_SRC_ALPHA;
  target.DestBlend=target.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;
  target.BlendOp=target.BlendOpAlpha=D3D11_BLEND_OP_ADD;
  target.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
  if(FAILED(hr=device_->CreateBlendState(&blend_description,&blend)))return hr;
  D3D11_DEPTH_STENCIL_DESC depth_description{};depth_description.DepthEnable=FALSE;
  depth_description.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;
  depth_description.DepthFunc=D3D11_COMPARISON_ALWAYS;
  if(FAILED(hr=device_->CreateDepthStencilState(&depth_description,&depth)))return hr;
  screen_rectangle_ps_=std::move(pixel_shader);screen_rectangle_layout_=std::move(layout);
  screen_rectangle_vertices_=std::move(vertices);screen_rectangle_capacity_=kScreenRectangleMaximumVertices;screen_rectangle_raster_state_=std::move(raster);
  screen_rectangle_blend_state_=std::move(blend);screen_rectangle_depth_state_=std::move(depth);
  screen_rectangle_vs_=std::move(vertex_shader);
 }
 ScreenRectangleQuad quads[5]{};
 const unsigned quad_count=screen_rectangle_quads(request,quads);
 if(!quad_count)return S_OK;
 // Same game-pixel to target mapping as the stock pre-transformed draw.
 const float origin_x=(float)viewport.dwX,origin_y=(float)viewport.dwY;
 const float scale_x=2.f/(float)std::max<DWORD>(1,viewport.dwWidth);
 const float scale_y=2.f/(float)std::max<DWORD>(1,viewport.dwHeight);
 std::array<ScreenRectangleVertex,kScreenRectangleMaximumVertices> small_vertices{};
 std::vector<ScreenRectangleVertex> grid_vertices;
 ScreenRectangleVertex* vertices=small_vertices.data();
 const unsigned grid_count=grid_step>0?unsigned(std::ceil(viewport.dwWidth/grid_step)+std::ceil(viewport.dwHeight/grid_step))+2u:0u;
 if(grid_count) {
  try{grid_vertices.resize((quad_count+grid_count)*6u);}
  catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
  vertices=grid_vertices.data();
 }
 UINT vertex_count=0;
 const auto append=[&](const ScreenRectangleQuad& quad){
  const float left=(float(quad.left)-origin_x)*scale_x-1.f,right=(float(quad.right)-origin_x)*scale_x-1.f;
  const float top=1.f-(float(quad.top)-origin_y)*scale_y,bottom=1.f-(float(quad.bottom)-origin_y)*scale_y;
  const ScreenRectangleVertex corners[6]={{left,top,quad.color},{right,top,quad.color},
   {right,bottom,quad.color},{left,top,quad.color},{right,bottom,quad.color},{left,bottom,quad.color}};
  for(const auto& corner:corners)vertices[vertex_count++]=corner;
 };
 for(unsigned index=0;index<quad_count;++index)append(quads[index]);
 if(grid_count) {
  for(float x=0;x<viewport.dwWidth;x+=grid_step)append({int(x),0,int(x)+1,int(viewport.dwHeight),0x40808080u});
  for(float y=0;y<viewport.dwHeight;y+=grid_step)append({0,int(y),int(viewport.dwWidth),int(y)+1,0x40808080u});
  // Brighter two-pixel guides identify the centre independently of grid spacing.
  const int centre_x=int(viewport.dwWidth)/2,centre_y=int(viewport.dwHeight)/2;
  append({centre_x-1,0,centre_x+1,int(viewport.dwHeight),0xC0FFCC66u});
  append({0,centre_y-1,int(viewport.dwWidth),centre_y+1,0xC0FFCC66u});
 }
 if(vertex_count>screen_rectangle_capacity_) {
  D3D11_BUFFER_DESC description{};description.ByteWidth=sizeof(ScreenRectangleVertex)*vertex_count;
  description.Usage=D3D11_USAGE_DYNAMIC;description.BindFlags=D3D11_BIND_VERTEX_BUFFER;description.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
  ComPtr<ID3D11Buffer> replacement;
  if(FAILED(hr=device_->CreateBuffer(&description,nullptr,&replacement)))return hr;
  screen_rectangle_vertices_=std::move(replacement);screen_rectangle_capacity_=vertex_count;
 }
 D3D11_MAPPED_SUBRESOURCE mapped{};
 if(FAILED(hr=context_->Map(screen_rectangle_vertices_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))return hr;
 std::memcpy(mapped.pData,vertices,sizeof(ScreenRectangleVertex)*vertex_count);
 context_->Unmap(screen_rectangle_vertices_.Get(),0);
 // Binds every state it reads and restores nothing: the interface draws
 // after it bind their own (draw: IA, shaders, targets, viewport, b0;
 // apply_state: raster, scissor, blend, depth, s0; the UI blits and the
 // native flush likewise).
 const UINT stride=sizeof(ScreenRectangleVertex),offset=0;
 ID3D11Buffer* buffer=screen_rectangle_vertices_.Get();
 context_->IASetInputLayout(screen_rectangle_layout_.Get());
 context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
 context_->IASetVertexBuffers(0,1,&buffer,&stride,&offset);
 context_->VSSetShader(screen_rectangle_vs_.Get(),nullptr,0);
 context_->PSSetShader(screen_rectangle_ps_.Get(),nullptr,0);
 context_->GSSetShader(nullptr,nullptr,0);context_->HSSetShader(nullptr,nullptr,0);
 context_->DSSetShader(nullptr,nullptr,0);
 const D3D11_VIEWPORT target_viewport{0,0,(float)target_width(),(float)target_height(),0,1};
 context_->RSSetViewports(1,&target_viewport);
 context_->RSSetState(screen_rectangle_raster_state_.Get());
 bind_plan_targets(plan_pass("screen_rectangle",rtv_.Get(),nullptr));
 context_->OMSetBlendState(screen_rectangle_blend_state_.Get(),nullptr,0xffffffffu);
 context_->OMSetDepthStencilState(screen_rectangle_depth_state_.Get(),0);
 context_->Draw(vertex_count,0);
 profiler_.add_draw(vertex_count/3u,1);profiler_.add_batch();
 return S_OK;
}

HRESULT Renderer::create_clear_pipeline(){
 clear_vs_.Reset();clear_ps_.Reset();clear_constants_.Reset();clear_raster_state_.Reset();
 shadow_scroll_vs_.Reset();shadow_scroll_ps_.Reset();shadow_scroll_constants_.Reset();shadow_scroll_raster_.Reset();
 clear_blend_state_.Reset();for(auto& state:clear_depth_states_)state.Reset();
 if(feature_level_<D3D_FEATURE_LEVEL_11_0)return S_FALSE;
 ComPtr<ID3DBlob> vertex,pixel,error;HRESULT hr=compile_shader(world_support_shader_program(WorldSupportShader::clear_vs),&vertex,&error);if(FAILED(hr))return hr;
 hr=compile_shader(world_support_shader_program(WorldSupportShader::clear_ps),&pixel,&error);if(FAILED(hr))return hr;
 if(FAILED(hr=create_shader(device_.Get(),vertex->GetBufferPointer(),vertex->GetBufferSize(),nullptr,&clear_vs_)))return hr;
 if(FAILED(hr=create_shader(device_.Get(),pixel->GetBufferPointer(),pixel->GetBufferSize(),nullptr,&clear_ps_)))return hr;
 D3D11_BUFFER_DESC constants{};constants.ByteWidth=sizeof(ClearConstants);constants.Usage=D3D11_USAGE_DYNAMIC;
 constants.BindFlags=D3D11_BIND_CONSTANT_BUFFER;constants.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
 if(FAILED(hr=device_->CreateBuffer(&constants,nullptr,&clear_constants_)))return hr;
 D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;
 raster.DepthClipEnable=TRUE;raster.ScissorEnable=TRUE;
 if(FAILED(hr=device_->CreateRasterizerState(&raster,&clear_raster_state_)))return hr;
 D3D11_BLEND_DESC blend_state{};blend_state.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
 if(FAILED(hr=device_->CreateBlendState(&blend_state,&clear_blend_state_)))return hr;
 for(UINT index=0;index<clear_depth_states_.size();++index){
  const bool clear_depth=index==0||index==2;const bool clear_stencil=index==1||index==2;
  D3D11_DEPTH_STENCIL_DESC depth_state{};depth_state.DepthEnable=clear_depth;
  depth_state.DepthWriteMask=clear_depth?D3D11_DEPTH_WRITE_MASK_ALL:D3D11_DEPTH_WRITE_MASK_ZERO;
  depth_state.DepthFunc=D3D11_COMPARISON_ALWAYS;depth_state.StencilEnable=clear_stencil;
  depth_state.StencilReadMask=D3D11_DEFAULT_STENCIL_READ_MASK;
  depth_state.StencilWriteMask=D3D11_DEFAULT_STENCIL_WRITE_MASK;
  depth_state.FrontFace.StencilFailOp=D3D11_STENCIL_OP_REPLACE;
  depth_state.FrontFace.StencilDepthFailOp=D3D11_STENCIL_OP_REPLACE;
  depth_state.FrontFace.StencilPassOp=D3D11_STENCIL_OP_REPLACE;
  depth_state.FrontFace.StencilFunc=D3D11_COMPARISON_ALWAYS;
  depth_state.BackFace=depth_state.FrontFace;
  if(FAILED(hr=device_->CreateDepthStencilState(&depth_state,&clear_depth_states_[index])))return hr;
 }
 // Close sun-map scroll: without it every moved close map redraws in full;
 // a failure leaves the clear pipeline intact.
 [&]{
  ComPtr<ID3DBlob> scroll_vertex,scroll_pixel,scroll_error;
  D3D11_BUFFER_DESC scroll_constants{};scroll_constants.ByteWidth=16;scroll_constants.Usage=D3D11_USAGE_DYNAMIC;
  scroll_constants.BindFlags=D3D11_BIND_CONSTANT_BUFFER;scroll_constants.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
  D3D11_RASTERIZER_DESC scroll_raster{};scroll_raster.FillMode=D3D11_FILL_SOLID;scroll_raster.CullMode=D3D11_CULL_NONE;
  scroll_raster.DepthClipEnable=TRUE;
  if(FAILED(compile_shader(world_support_shader_program(WorldSupportShader::shadow_scroll_vs),&scroll_vertex,&scroll_error))||
     FAILED(compile_shader(world_support_shader_program(WorldSupportShader::shadow_scroll_ps),&scroll_pixel,&scroll_error))||
     FAILED(create_shader(device_.Get(),scroll_vertex->GetBufferPointer(),scroll_vertex->GetBufferSize(),nullptr,&shadow_scroll_vs_))||
     FAILED(device_->CreateBuffer(&scroll_constants,nullptr,&shadow_scroll_constants_))||
     FAILED(device_->CreateRasterizerState(&scroll_raster,&shadow_scroll_raster_))||
     // Created last: its presence enables scrolling (begin_native_shadows).
     FAILED(create_shader(device_.Get(),scroll_pixel->GetBufferPointer(),scroll_pixel->GetBufferSize(),nullptr,&shadow_scroll_ps_))){
   shadow_scroll_vs_.Reset();shadow_scroll_ps_.Reset();shadow_scroll_constants_.Reset();shadow_scroll_raster_.Reset();
   bridge_append_probe_event("shadow_scroll_unavailable","close sun maps redraw in full when they move");
  }
 }();
 return S_OK;
}

#endif
// Native world programs in create_native_pipeline's order: nativeVS, nativePS,
// then the compute and shadow stages (the stage follows the entry suffix),
// last the two terrain pixel variants, embedded at build time like the rest,
// so binding them never compiles at run time.
void append_native_shader_programs(std::vector<ShaderProgram>& out){
#ifdef EID3D11_TESTING
 static const D3D_SHADER_MACRO defines[]={{"EID3D11_TESTING","1"},{nullptr,nullptr}};
#else
 const D3D_SHADER_MACRO* const defines=nullptr;
#endif
 const std::string& source=native_shader_source();
 out.push_back({source,nullptr,nullptr,"nativeVS","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 out.push_back({source,nullptr,nullptr,"nativePS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 static const std::string poseSource=compose_native_shader_source({},true);
 for(const char* entry:{"nativeBuildCS","nativeTerrainPhaseCS","nativeInstanceUpdateCS","nativePoseCS","nativeShadowPrepareCS",
                        "nativeShadowCommitCS","nativeShadowBuildCS","nativeShadowArgsGuardCS","nativeShadowVS","nativeShadowPS","nativeTerrainMissCompactCS"})
  out.push_back({std::strcmp(entry,"nativePoseCS")==0?poseSource:source,nullptr,defines,entry,std::strstr(entry,"CS")?"cs_5_0":(std::strstr(entry,"VS")?"vs_5_0":"ps_5_0"),
                 D3DCOMPILE_OPTIMIZATION_LEVEL3});
 // Then the terrain pixel variant (NATIVE_TERRAIN_PS: tile blend, terrain
 // material, cliffs), bound to terrain batches; then the same
 // with the snow trail code (NATIVE_SNOW_TRAILS), bound to terrain batches
 // instead while a trail field exists. nativePS above is the lean shader of
 // figures and objects. Liquid triangles draw through nativeLiquidPS.
 static const D3D_SHADER_MACRO terrain[]={{"NATIVE_TERRAIN_PS","1"},{nullptr,nullptr}};
 out.push_back({source,nullptr,terrain,"nativePS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 static const D3D_SHADER_MACRO snow_trails[]={{"NATIVE_TERRAIN_PS","1"},{"NATIVE_SNOW_TRAILS","1"},{nullptr,nullptr}};
 out.push_back({source,nullptr,snow_trails,"nativePS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 // The Low pixel path (NATIVE_LOW, native_low_pixel_path): the lean shader
 // without the stages Low settings switch off, so weak GPUs neither run nor
 // hold registers for them, and a four-tap sun shadow. Low terrain batches
 // draw with it too: every terrain stage is off there.
 static const D3D_SHADER_MACRO low[]={{"NATIVE_LOW","1"},{nullptr,nullptr}};
 out.push_back({source,nullptr,low,"nativePS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 // Low unlit ordered batches (the stock sky of sky_clouds 0): the same
 // shader without the lighting such draws never use.
 static const D3D_SHADER_MACRO low_unlit[]={{"NATIVE_LOW","1"},{"NATIVE_UNLIT","1"},{nullptr,nullptr}};
 out.push_back({source,nullptr,low_unlit,"nativePS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 // Liquid surfaces: the liquid triangles of liquid batches, then the same
 // on the Low path (reflection and physical water compiled out).
 // The water body: full; without the reflection and physical-water code
 // (NATIVE_LOW: the Low path, and batches drawn without the reflection
 // frame, which compile to the same program); without physical water; then
 // the foam layer (the same program on every path).
 static const D3D_SHADER_MACRO liquid_body[]={{"NATIVE_LIQUID_BODY","1"},{nullptr,nullptr}};
 static const D3D_SHADER_MACRO liquid_body_low[]={{"NATIVE_LIQUID_BODY","1"},{"NATIVE_LOW","1"},{nullptr,nullptr}};
 static const D3D_SHADER_MACRO liquid_ssr[]={{"NATIVE_LIQUID_BODY","1"},{"NATIVE_LIQUID_NO_PHYSICAL","1"},{nullptr,nullptr}};
 static const D3D_SHADER_MACRO liquid_foam[]={{"NATIVE_LIQUID_FOAM","1"},{nullptr,nullptr}};
 // Water-only batches (full, without physical water, Low), and lava-only
 // batches (one program: Low only removes code lava never runs).
 static const D3D_SHADER_MACRO water[]={{"NATIVE_LIQUID_BODY","1"},{"NATIVE_LIQUID_WATER","1"},{nullptr,nullptr}};
 static const D3D_SHADER_MACRO water_ssr[]={{"NATIVE_LIQUID_BODY","1"},{"NATIVE_LIQUID_WATER","1"},{"NATIVE_LIQUID_NO_PHYSICAL","1"},{nullptr,nullptr}};
 static const D3D_SHADER_MACRO water_low[]={{"NATIVE_LIQUID_BODY","1"},{"NATIVE_LIQUID_WATER","1"},{"NATIVE_LOW","1"},{nullptr,nullptr}};
 static const D3D_SHADER_MACRO lava[]={{"NATIVE_LIQUID_BODY","1"},{"NATIVE_LIQUID_LAVA","1"},{nullptr,nullptr}};
 // The half-resolution reflection's inputs (every liquid type), then its march.
 static const D3D_SHADER_MACRO liquid_ssr_input[]={{"NATIVE_LIQUID_BODY","1"},{"NATIVE_LIQUID_SSR_INPUT","1"},{nullptr,nullptr}};
 // The full liquid and water bodies with the swimmer lookup (fish marked in
 // the depth snapshot's stencil): bound only for such a snapshot, so the
 // programs above carry none of it.
 static const D3D_SHADER_MACRO liquid_swim[]={{"NATIVE_LIQUID_BODY","1"},{"NATIVE_LIQUID_SWIMMERS","1"},{nullptr,nullptr}};
 static const D3D_SHADER_MACRO water_swim[]={{"NATIVE_LIQUID_BODY","1"},{"NATIVE_LIQUID_WATER","1"},{"NATIVE_LIQUID_SWIMMERS","1"},{nullptr,nullptr}};
 for(const D3D_SHADER_MACRO* variant:{liquid_body,liquid_body_low,liquid_ssr,liquid_foam,water,water_ssr,water_low,lava,liquid_ssr_input,liquid_swim,water_swim})
  out.push_back({source,nullptr,variant,"nativeLiquidPS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 out.push_back({source,nullptr,nullptr,"nativeLiquidSsrPS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 out.push_back({source,nullptr,nullptr,"nativeLiquidSsrFilterPS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 // The flat tile class (NATIVE_TERRAIN_FLAT: no side projection; the
 // first region of terrain batches whose tiles are classified): terrain,
 // then snow terrain.
 static const D3D_SHADER_MACRO flat[]={{"NATIVE_TERRAIN_PS","1"},{"NATIVE_TERRAIN_FLAT","1"},{nullptr,nullptr}};
 out.push_back({source,nullptr,flat,"nativePS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 static const D3D_SHADER_MACRO snow_flat[]={{"NATIVE_TERRAIN_PS","1"},{"NATIVE_SNOW_TRAILS","1"},{"NATIVE_TERRAIN_FLAT","1"},{nullptr,nullptr}};
 out.push_back({source,nullptr,snow_flat,"nativePS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 // The material variants of prepassed batches (NATIVE_SHADOW_MASK: the sun
 // and cloud shadow read from the mask): object, Low, then terrain, snow
 // terrain and flat as above.
 static const D3D_SHADER_MACRO masked[]={{"NATIVE_SHADOW_MASK","1"},{nullptr,nullptr}};
 static const D3D_SHADER_MACRO masked_low[]={{"NATIVE_SHADOW_MASK","1"},{"NATIVE_LOW","1"},{nullptr,nullptr}};
 static const D3D_SHADER_MACRO masked_terrain[]={{"NATIVE_SHADOW_MASK","1"},{"NATIVE_TERRAIN_PS","1"},{nullptr,nullptr}};
 static const D3D_SHADER_MACRO masked_snow[]={{"NATIVE_SHADOW_MASK","1"},{"NATIVE_TERRAIN_PS","1"},{"NATIVE_SNOW_TRAILS","1"},{nullptr,nullptr}};
 static const D3D_SHADER_MACRO masked_flat[]={{"NATIVE_SHADOW_MASK","1"},{"NATIVE_TERRAIN_PS","1"},{"NATIVE_TERRAIN_FLAT","1"},{nullptr,nullptr}};
 static const D3D_SHADER_MACRO masked_snow_flat[]={{"NATIVE_SHADOW_MASK","1"},{"NATIVE_TERRAIN_PS","1"},{"NATIVE_SNOW_TRAILS","1"},{"NATIVE_TERRAIN_FLAT","1"},{nullptr,nullptr}};
 for(const D3D_SHADER_MACRO* variant:{masked,masked_low,masked_terrain,masked_snow,masked_flat,masked_snow_flat})
  out.push_back({source,nullptr,variant,"nativePS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 // The depth prepass pixel entry and the shadow mask stages, each then on
 // the Low path.
 out.push_back({source,nullptr,nullptr,"nativePrepassVS","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 out.push_back({source,nullptr,nullptr,"nativeDepthClearVS","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3}); // point atlas cells
 out.push_back({source,nullptr,nullptr,"nativePrepassPS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 out.push_back({source,nullptr,low,"nativePrepassPS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 out.push_back({source,nullptr,nullptr,"nativeShadowMaskVS","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 // DLSS object motion stages (posed records).
 out.push_back({source,nullptr,nullptr,"nativeMotionVS","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 out.push_back({source,nullptr,nullptr,"nativeMotionPS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 out.push_back({source,nullptr,nullptr,"nativeShadowMaskPS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 out.push_back({source,nullptr,low,"nativeShadowMaskPS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 // The slim terrain pass (terrain_slim): the prepassed terrain, snow terrain
 // and their flat variants without the per-pixel material.
 static const D3D_SHADER_MACRO masked_slim[]={{"NATIVE_SHADOW_MASK","1"},{"NATIVE_TERRAIN_PS","1"},{"NATIVE_TERRAIN_SLIM","1"},{nullptr,nullptr}};
 static const D3D_SHADER_MACRO masked_snow_slim[]={{"NATIVE_SHADOW_MASK","1"},{"NATIVE_TERRAIN_PS","1"},{"NATIVE_SNOW_TRAILS","1"},{"NATIVE_TERRAIN_SLIM","1"},{nullptr,nullptr}};
 static const D3D_SHADER_MACRO masked_slim_flat[]={{"NATIVE_SHADOW_MASK","1"},{"NATIVE_TERRAIN_PS","1"},{"NATIVE_TERRAIN_FLAT","1"},{"NATIVE_TERRAIN_SLIM","1"},{nullptr,nullptr}};
 static const D3D_SHADER_MACRO masked_snow_slim_flat[]={{"NATIVE_SHADOW_MASK","1"},{"NATIVE_TERRAIN_PS","1"},{"NATIVE_SNOW_TRAILS","1"},{"NATIVE_TERRAIN_FLAT","1"},{"NATIVE_TERRAIN_SLIM","1"},{nullptr,nullptr}};
 for(const D3D_SHADER_MACRO* variant:{masked_slim,masked_snow_slim,masked_slim_flat,masked_snow_slim_flat})
  out.push_back({source,nullptr,variant,"nativePS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 static const D3D_SHADER_MACRO gltf[]={{"NATIVE_GLTF","1"},{nullptr,nullptr}};
 static const D3D_SHADER_MACRO gltf_low[]={{"NATIVE_GLTF","1"},{"NATIVE_LOW","1"},{nullptr,nullptr}};
 static const D3D_SHADER_MACRO gltf_masked[]={{"NATIVE_GLTF","1"},{"NATIVE_SHADOW_MASK","1"},{nullptr,nullptr}};
 static const D3D_SHADER_MACRO gltf_masked_low[]={{"NATIVE_GLTF","1"},{"NATIVE_SHADOW_MASK","1"},{"NATIVE_LOW","1"},{nullptr,nullptr}};
 for(const D3D_SHADER_MACRO* variant:{gltf,gltf_low,gltf_masked,gltf_masked_low})
  out.push_back({source,nullptr,variant,"nativePS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 // The material instance programs (renderer.h kNativeInstance*): each slot's
 // object and prepassed programs, then the same with NATIVE_DOMAIN_CHECK
 // (F10 shader_split_check; never in the specialised sets).
 for(const bool check:{false,true})
  for(std::size_t slot=0;slot<kNativeInstanceSlots;++slot)
   for(const bool prepassed:{false,true})if(native_instance_built(slot))
    out.push_back({source,nullptr,native_instance_defines(slot,prepassed,check,false),"nativePS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 // Their vertex programs by class: colour, then prepass.
 for(std::size_t vertex_class=0;vertex_class<kNativeInstanceVertexClasses;++vertex_class)
  for(const char* entry:{"nativeVS","nativePrepassVS"})
   out.push_back({source,nullptr,native_instance_vertex_defines(vertex_class,false),entry,"vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 // The colour programs' check twins (debug: they carry the fur_debug vertex bits).
 for(std::size_t vertex_class=0;vertex_class<kNativeInstanceVertexClasses;++vertex_class)
  out.push_back({source,nullptr,native_instance_vertex_defines(vertex_class,false,true),"nativeVS","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 // The astral rift programs (NATIVE_ASTRAL, astral_rift.h): the vertex
 // program, the pixel program and its Low twin. Only batches of astral
 // records (NativeMaterialKey::program 1) draw with them, so no other
 // program carries the effect.
 static const D3D_SHADER_MACRO astral[]={{"NATIVE_ASTRAL","1"},{nullptr,nullptr}};
 static const D3D_SHADER_MACRO astral_low[]={{"NATIVE_ASTRAL","1"},{"NATIVE_LOW","1"},{nullptr,nullptr}};
 out.push_back({source,nullptr,astral,"nativeVS","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 out.push_back({source,nullptr,astral,"nativePS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 out.push_back({source,nullptr,astral_low,"nativePS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
}

const D3D_SHADER_MACRO* native_instance_vertex_defines(std::size_t vertex_class,bool motion,bool check) noexcept{
 static const D3D_SHADER_MACRO lists[kNativeInstanceVertexClasses][2][2][4]={
  {{{{"NATIVE_DOMAIN","2"},{nullptr,nullptr}},{{"NATIVE_DOMAIN","2"},{"NATIVE_DOMAIN_CHECK","1"},{nullptr,nullptr}}},
   {{{"NATIVE_DOMAIN","2"},{"NATIVE_OBJECT_MOTION","1"},{nullptr,nullptr}},{{"NATIVE_DOMAIN","2"},{"NATIVE_OBJECT_MOTION","1"},{"NATIVE_DOMAIN_CHECK","1"},{nullptr,nullptr}}}},
  {{{{"NATIVE_DOMAIN","4"},{nullptr,nullptr}},{{"NATIVE_DOMAIN","4"},{"NATIVE_DOMAIN_CHECK","1"},{nullptr,nullptr}}},
   {{{"NATIVE_DOMAIN","4"},{"NATIVE_OBJECT_MOTION","1"},{nullptr,nullptr}},{{"NATIVE_DOMAIN","4"},{"NATIVE_OBJECT_MOTION","1"},{"NATIVE_DOMAIN_CHECK","1"},{nullptr,nullptr}}}}};
 return vertex_class<kNativeInstanceVertexClasses?lists[vertex_class][motion?1:0][check?1:0]:nullptr;
}

const D3D_SHADER_MACRO* native_instance_defines(std::size_t slot,bool masked,bool check,bool motion,bool no_contact,bool dry) noexcept{
 // Per slot, 32 lists (masked | check<<1 | motion<<2 | no_contact<<3 | dry<<4) of at most 7 defines and the terminator.
 static const auto table=[]{
  std::array<std::array<D3D_SHADER_MACRO,8>,kNativeInstanceSlots*32u> lists{};
  static const char* const domains[kNativeInstanceDomains]={"1","2","3","4","5","6"};
  for(std::size_t s=0;s<kNativeInstanceSlots;++s)for(unsigned variant=0;variant<32u;++variant){
   auto& list=lists[s*32u+variant];std::size_t n=0;
   list[n++]={"NATIVE_DOMAIN",domains[s/2u]};
   list[n++]={"NATIVE_PROFILED",(s&1u)?"1":"0"};
   if(variant&4u)list[n++]={"NATIVE_OBJECT_MOTION","1"};
   if(variant&1u)list[n++]={"NATIVE_SHADOW_MASK","1"};
   if(variant&2u)list[n++]={"NATIVE_DOMAIN_CHECK","1"};
   if(variant&8u)list[n++]={"NATIVE_OFF_CONTACT","1"};
   if(variant&16u)list[n++]={"NATIVE_FRAME_DRY","1"};
   list[n]={nullptr,nullptr};
  }
  return lists;
 }();
 if(slot>=kNativeInstanceSlots)return nullptr;
 return table[slot*32u+(masked?1u:0u)+(check?2u:0u)+(motion?4u:0u)+(no_contact?8u:0u)+(dry?16u:0u)].data();
}
// D3D12 (it has no specialisation sets): the native pixel programs again
// without the S1-S3 material code (NATIVE_OFF_MATERIALS), drawn while
// material quality is Off. Present but switched off, that code cost ~0.2 ms
// of the terrain passes on the save1027 frame. The Low programs never carry it.
void append_native_off_material_programs(std::vector<ShaderProgram>& out){
 static const auto programs=[]{
  struct Built{std::vector<ShaderProgram> programs;std::vector<std::vector<D3D_SHADER_MACRO>> macros;};
  Built built;std::vector<ShaderProgram> general;append_native_shader_programs(general);
  for(const auto& program:general){
   if(std::strcmp(program.entry,"nativePS")!=0)continue;
   std::vector<D3D_SHADER_MACRO> macros;bool low=false;
   for(const D3D_SHADER_MACRO* define=program.defines;define&&define->Name;++define){macros.push_back(*define);low|=std::strcmp(define->Name,"NATIVE_LOW")==0||std::strcmp(define->Name,"NATIVE_DOMAIN")==0;}
   if(low)continue; // nor the material instances (material quality Off draws the general programs)
   macros.push_back({"NATIVE_OFF_MATERIALS","1"});macros.push_back({nullptr,nullptr});
   built.macros.push_back(std::move(macros));built.programs.push_back(program);
  }
  for(std::size_t n=0;n<built.programs.size();++n)built.programs[n].defines=built.macros[n].data();
  return built;
 }();
 out.insert(out.end(),programs.programs.begin(),programs.programs.end());
}

// D3D12: the slim terrain pass's programs again with NATIVE_TERRAIN_LEAN
// (option terrain_slim_cuts, flush_native_d3d12): terrain, snow terrain and
// their flat variants. None without the material code: material quality Off
// draws the slim programs above.
void append_native_slim_lean_programs(std::vector<ShaderProgram>& out){
 const std::string& source=native_shader_source();
 static const D3D_SHADER_MACRO lean[]={{"NATIVE_SHADOW_MASK","1"},{"NATIVE_TERRAIN_PS","1"},{"NATIVE_TERRAIN_SLIM","1"},{"NATIVE_TERRAIN_LEAN","1"},{nullptr,nullptr}};
 static const D3D_SHADER_MACRO snow_lean[]={{"NATIVE_SHADOW_MASK","1"},{"NATIVE_TERRAIN_PS","1"},{"NATIVE_SNOW_TRAILS","1"},{"NATIVE_TERRAIN_SLIM","1"},{"NATIVE_TERRAIN_LEAN","1"},{nullptr,nullptr}};
 static const D3D_SHADER_MACRO lean_flat[]={{"NATIVE_SHADOW_MASK","1"},{"NATIVE_TERRAIN_PS","1"},{"NATIVE_TERRAIN_FLAT","1"},{"NATIVE_TERRAIN_SLIM","1"},{"NATIVE_TERRAIN_LEAN","1"},{nullptr,nullptr}};
 static const D3D_SHADER_MACRO snow_lean_flat[]={{"NATIVE_SHADOW_MASK","1"},{"NATIVE_TERRAIN_PS","1"},{"NATIVE_SNOW_TRAILS","1"},{"NATIVE_TERRAIN_FLAT","1"},{"NATIVE_TERRAIN_SLIM","1"},{"NATIVE_TERRAIN_LEAN","1"},{nullptr,nullptr}};
 for(const D3D_SHADER_MACRO* variant:{lean,snow_lean,lean_flat,snow_lean_flat})
  out.push_back({source,nullptr,variant,"nativePS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
}

// D3D12: the terrain second passes' stencil mark (option terrain_slim_stencil):
// the full-screen pixel program (1) and the listed quads' vertex stage (2).
void append_native_slim_mark_program(std::vector<ShaderProgram>& out){
 out.push_back({native_shader_source(),nullptr,nullptr,"nativeSlimMissMarkPS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 out.push_back({native_shader_source(),nullptr,nullptr,"nativeSlimQuadMarkVS","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
}

// D3D12: the depth prepass program of material instance batches
// (NATIVE_PREPASS_OBJECT: no terrain record code; option prepass_object_program).
void append_native_prepass_object_program(std::vector<ShaderProgram>& out){
 static const D3D_SHADER_MACRO object[]={{"NATIVE_PREPASS_OBJECT","1"},{nullptr,nullptr}};
 out.push_back({native_shader_source(),nullptr,object,"nativePrepassPS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
}

// D3D12: the material pass's object motion stages (NATIVE_OBJECT_MOTION,
// flush_native_d3d12): the vertex stage with the last-frame clip position,
// then the object pixel program and its prepassed (shadow mask) variant,
// each also without the material code (material quality Off).
void append_native_motion_programs(std::vector<ShaderProgram>& out){
 const std::string& source=native_shader_source();
 static const D3D_SHADER_MACRO motion[]={{"NATIVE_OBJECT_MOTION","1"},{nullptr,nullptr}};
 static const D3D_SHADER_MACRO masked[]={{"NATIVE_OBJECT_MOTION","1"},{"NATIVE_SHADOW_MASK","1"},{nullptr,nullptr}};
 static const D3D_SHADER_MACRO off[]={{"NATIVE_OBJECT_MOTION","1"},{"NATIVE_OFF_MATERIALS","1"},{nullptr,nullptr}};
 static const D3D_SHADER_MACRO masked_off[]={{"NATIVE_OBJECT_MOTION","1"},{"NATIVE_SHADOW_MASK","1"},{"NATIVE_OFF_MATERIALS","1"},{nullptr,nullptr}};
 out.push_back({source,nullptr,motion,"nativeMotionWorldVS","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 for(std::size_t vertex_class=0;vertex_class<kNativeInstanceVertexClasses;++vertex_class)
  for(const bool check:{false,true})
   out.push_back({source,nullptr,native_instance_vertex_defines(vertex_class,true,check),"nativeMotionWorldVS","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 for(const D3D_SHADER_MACRO* variant:{motion,masked,off,masked_off})
  out.push_back({source,nullptr,variant,"nativePS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 static const D3D_SHADER_MACRO gltf_motion[]={{"NATIVE_GLTF","1"},{"NATIVE_OBJECT_MOTION","1"},{nullptr,nullptr}};
 static const D3D_SHADER_MACRO gltf_motion_masked[]={{"NATIVE_GLTF","1"},{"NATIVE_OBJECT_MOTION","1"},{"NATIVE_SHADOW_MASK","1"},{nullptr,nullptr}};
 for(const D3D_SHADER_MACRO* variant:{gltf_motion,gltf_motion_masked})
  out.push_back({source,nullptr,variant,"nativePS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 // The material instances' motion programs (object and prepassed, then the check programs).
 for(const bool check:{false,true})
  for(std::size_t slot=0;slot<kNativeInstanceSlots;++slot)
   for(const bool prepassed:{false,true})if(native_instance_built(slot))
    out.push_back({source,nullptr,native_instance_defines(slot,prepassed,check,true),"nativePS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 // The per-batch and per-frame variants of the material instances (object, prepassed, then with the
 // object motion): without the contact blend for world object, foliage and crown impostor batches
 // out of contact reach (NativeBatch::contact false; D3D11 binds the contact-off specialised set
 // instead; unit programs carry no contact code), and every instance program again for dry frames
 // (NATIVE_FRAME_DRY, native_frame_dry_).
 for(const bool dry:{false,true})for(const bool no_contact:{false,true}){
  if(!dry&&!no_contact)continue; // the programs above
  for(std::size_t slot=no_contact?2u:0u;slot<kNativeInstanceSlots;++slot)if(native_instance_built(slot))
   for(const bool motion:{false,true})for(const bool prepassed:{false,true})
    out.push_back({source,nullptr,native_instance_defines(slot,prepassed,false,motion,no_contact,dry),"nativePS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 }
}

// D3D12 indexed object draws (option indexed_object_draws, native_shader.h
// NATIVE_INDEXED): the material instance vertex programs that read their
// vertex owner by SV_VertexID. Per class: colour, prepass, object motion.
const D3D_SHADER_MACRO* native_indexed_vertex_defines(std::size_t vertex_class,bool motion) noexcept{
 static const D3D_SHADER_MACRO lists[kNativeInstanceVertexClasses][2][4]={
  {{{"NATIVE_DOMAIN","2"},{"NATIVE_INDEXED","1"},{nullptr,nullptr}},{{"NATIVE_DOMAIN","2"},{"NATIVE_INDEXED","1"},{"NATIVE_OBJECT_MOTION","1"},{nullptr,nullptr}}},
  {{{"NATIVE_DOMAIN","4"},{"NATIVE_INDEXED","1"},{nullptr,nullptr}},{{"NATIVE_DOMAIN","4"},{"NATIVE_INDEXED","1"},{"NATIVE_OBJECT_MOTION","1"},{nullptr,nullptr}}}};
 return vertex_class<kNativeInstanceVertexClasses?lists[vertex_class][motion?1:0]:nullptr;
}
void append_native_indexed_programs(std::vector<ShaderProgram>& out){
 const std::string& source=native_shader_source();
 for(std::size_t vertex_class=0;vertex_class<kNativeInstanceVertexClasses;++vertex_class){
  for(const char* entry:{"nativeVS","nativePrepassVS"})
   out.push_back({source,nullptr,native_indexed_vertex_defines(vertex_class,false),entry,"vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
  out.push_back({source,nullptr,native_indexed_vertex_defines(vertex_class,true),"nativeMotionWorldVS","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
 }
}

// D3D12, rt_ray_reconstruction: which world material pixel programs have a
// guide twin: every nativePS of the native lists but the Low path (no
// material response), the unlit ordered ones (the stock sky: the guide's sky
// defaults stay) and the debug check programs. The material-off programs
// have twins too: they write the matte dielectric guide (the geometric
// normal, roughness 1, F0 .04), so the traced lamp and bounce light, the
// traced occlusion and Ray Reconstruction run with material quality Off
// (a custom mix: the original look with ray traced light).
bool native_guide_twinned(const ShaderProgram& program) noexcept{
 // The liquid bodies (issue 283): NATIVE_LIQUID_GUIDES twins that scale the bed's guides by the water's transmittance.
 if(std::strcmp(program.entry,"nativeLiquidPS")==0){
  bool body=false;
  for(const D3D_SHADER_MACRO* define=program.defines;define&&define->Name;++define){
   if(std::strcmp(define->Name,"NATIVE_LIQUID_BODY")==0)body=true;
   for(const char* excluded:{"NATIVE_LIQUID_SSR_INPUT","EID3D11_TESTING"})if(std::strcmp(define->Name,excluded)==0)return false;
  }
  return body;
 }
 if(std::strcmp(program.entry,"nativePS")!=0)return false;
 for(const D3D_SHADER_MACRO* define=program.defines;define&&define->Name;++define)
  for(const char* excluded:{"NATIVE_LOW","NATIVE_UNLIT","NATIVE_DOMAIN_CHECK","EID3D11_TESTING"})
   if(std::strcmp(define->Name,excluded)==0)return false;
 return true;
}
void append_native_guide_programs(std::vector<ShaderProgram>& out){
 static const auto programs=[]{
  struct Built{std::vector<ShaderProgram> programs;std::vector<std::vector<D3D_SHADER_MACRO>> macros;};
  Built built;std::vector<ShaderProgram> sources;
  append_native_shader_programs(sources);append_native_off_material_programs(sources);append_native_motion_programs(sources);
  append_native_indexed_programs(sources);append_native_slim_lean_programs(sources); // native12_.programs' order (the lean slim terrain programs are twinned)
  for(const auto& program:sources){
   if(!native_guide_twinned(program))continue;
   std::vector<D3D_SHADER_MACRO> macros;
   for(const D3D_SHADER_MACRO* define=program.defines;define&&define->Name;++define)macros.push_back(*define);
   macros.push_back({std::strcmp(program.entry,"nativeLiquidPS")==0?"NATIVE_LIQUID_GUIDES":"NATIVE_RR_GUIDES","1"});macros.push_back({nullptr,nullptr});
   built.macros.push_back(std::move(macros));built.programs.push_back(program);
  }
  for(std::size_t n=0;n<built.programs.size();++n)built.programs[n].defines=built.macros[n].data();
  return built;
 }();
 out.insert(out.end(),programs.programs.begin(),programs.programs.end());
}

#if !defined(EID3D11_SHADER_EMBED)
HRESULT Renderer::create_native_pipeline(){
#ifdef EID3D11_TESTING
 for(const char* entry:{"nativeBuildCS","nativeShadowPrepareCS"}){
  ComPtr<ID3DBlob> production_shader,production_error;
  const HRESULT result=D3DCompile(native_shader_source().data(),native_shader_source().size(),nullptr,nullptr,nullptr,
   entry,"cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&production_shader,&production_error);
  if(FAILED(result)){if(production_error)std::fwrite(production_error->GetBufferPointer(),1,production_error->GetBufferSize(),stderr);return result;}
 }
#endif
 retire_native_poses();native_pose_cs_.Reset();
 retire_native_shadows();
 native_shadow_prepare_cs_.Reset();native_shadow_commit_cs_.Reset();native_shadow_build_cs_.Reset();native_shadow_args_guard_cs_.Reset();shadow_args_report_.Reset();shadow_args_report_uav_.Reset();shadow_args_report_copy_.Reset();shadow_args_copy_pending_=false;shadow_args_ticket_=0;shadow_args_next_read_=0;native_shadow_vs_.Reset();native_shadow_ps_.Reset();
 bridge_set_native_terrain_animation_owned(false);
 for(auto& program:native_instance_ps_)program.Reset();for(auto& program:native_instance_check_ps_)program.Reset();
 for(auto& program:native_instance_vs_)program.Reset();for(auto& program:native_instance_prepass_vs_)program.Reset();for(auto& program:native_instance_check_vs_)program.Reset();
 native_astral_vs_.Reset();native_astral_ps_.Reset();native_astral_low_ps_.Reset();
 native_gltf_ps_.Reset();native_gltf_low_ps_.Reset();native_gltf_masked_ps_.Reset();native_gltf_masked_low_ps_.Reset();
 native_vs_.Reset();native_ps_.Reset();native_terrain_ps_.Reset();native_snow_ps_.Reset();native_low_ps_.Reset();native_low_unlit_ps_.Reset();native_terrain_flat_ps_.Reset();native_snow_flat_ps_.Reset();native_masked_ps_.Reset();native_masked_low_ps_.Reset();native_masked_terrain_ps_.Reset();native_masked_snow_ps_.Reset();native_masked_terrain_flat_ps_.Reset();native_masked_snow_flat_ps_.Reset();native_masked_terrain_slim_ps_.Reset();native_masked_snow_slim_ps_.Reset();native_masked_terrain_slim_flat_ps_.Reset();native_masked_snow_slim_flat_ps_.Reset();native_terrain_miss_compact_cs_.Reset();terrain_miss_list_constants_.Reset();native_prepass_ps_.Reset();native_low_prepass_ps_.Reset();native_shadow_mask_vs_.Reset();native_shadow_mask_ps_.Reset();native_low_shadow_mask_ps_.Reset();native_prepass_vs_.Reset();native_depth_clear_vs_.Reset();native_liquid_ps_.Reset();native_low_liquid_ps_.Reset();native_liquid_ssr_ps_.Reset();native_liquid_foam_ps_.Reset();native_water_ps_.Reset();native_water_ssr_ps_.Reset();native_low_water_ps_.Reset();native_lava_ps_.Reset();native_liquid_swim_ps_.Reset();native_water_swim_ps_.Reset();native_build_cs_.Reset();native_world_lighting_constants_.Reset();native_world_lighting_valid_=false;native_world_lighting_dirty_=false;native_point_light_constants_.Reset();native_point_light_buffer_.Reset();native_point_light_srv_.Reset();native_point_light_capacity_=0;native_point_light_grid_buffer_.Reset();native_point_light_grid_srv_.Reset();native_point_light_grid_capacity_=0;native_point_light_grid_={};native_point_light_grid_inputs_.clear();native_point_lights_.clear();native_point_light_inputs_.clear();native_point_lights_dirty_=false;native_prepared_vertices_.Reset();native_prepared_vertex_srv_.Reset();native_prepared_vertex_uav_.Reset();native_prepared_vertex_capacity_=0;native_world_vertices_.Reset();native_world_vertex_srv_.Reset();native_world_vertex_uav_.Reset();native_world_vertex_capacity_=native_world_vertex_used_=0;
 if(feature_level_<D3D_FEATURE_LEVEL_11_0)return S_FALSE;
 std::vector<ShaderProgram> programs;append_native_shader_programs(programs);
 ComPtr<ID3DBlob> shader,error;HRESULT hr=compile_shader(programs[0],&shader,&error);if(FAILED(hr)){if(error){std::fwrite(error->GetBufferPointer(),1,error->GetBufferSize(),stderr);std::fputc('\n',stderr);}return hr;}
 if(FAILED(hr=create_shader(device_.Get(),shader->GetBufferPointer(),shader->GetBufferSize(),nullptr,&native_vs_)))return hr;
 shader.Reset();error.Reset();hr=compile_shader(programs[1],&shader,&error);if(FAILED(hr)){if(error){std::fwrite(error->GetBufferPointer(),1,error->GetBufferSize(),stderr);std::fputc('\n',stderr);}return hr;}
 if(FAILED(hr=create_shader(device_.Get(),shader->GetBufferPointer(),shader->GetBufferSize(),nullptr,&native_ps_)))return hr;
 shader.Reset();error.Reset();hr=compile_shader(programs[2],&shader,&error);if(FAILED(hr)){if(error){std::fwrite(error->GetBufferPointer(),1,error->GetBufferSize(),stderr);std::fputc('\n',stderr);}return hr;}
  if(FAILED(hr=create_shader(device_.Get(),shader->GetBufferPointer(),shader->GetBufferSize(),nullptr,&native_build_cs_)))return hr;
  std::size_t pixel_variant=0; // the next nativePS variant (programs[1] is the object shader)
  std::size_t liquid_variant=0; // the next nativeLiquidPS variant
  const auto native_program_failed=[](std::size_t index,const char* name,HRESULT result){
   char text[96];std::snprintf(text,sizeof(text),"program=%zu entry=%s hr=%08lx",index,name,static_cast<unsigned long>(result));
   bridge_report_event("native_program_failed",text);return result;
  };
  for(std::size_t program=3;program<programs.size();++program){
   shader.Reset();error.Reset();const char* entry=programs[program].entry;
   hr=compile_shader(programs[program],&shader,&error);
   if(FAILED(hr)){if(error)std::fwrite(error->GetBufferPointer(),1,error->GetBufferSize(),stderr);return native_program_failed(program,entry,hr);}
   if(std::strcmp(entry,"nativeTerrainPhaseCS")==0)hr=create_shader(device_.Get(),shader->GetBufferPointer(),shader->GetBufferSize(),nullptr,&native_terrain_phase_cs_);
   else if(std::strcmp(entry,"nativeInstanceUpdateCS")==0)hr=create_shader(device_.Get(),shader->GetBufferPointer(),shader->GetBufferSize(),nullptr,&native_instance_update_cs_);
   else if(std::strcmp(entry,"nativePoseCS")==0)hr=create_shader(device_.Get(),shader->GetBufferPointer(),shader->GetBufferSize(),nullptr,&native_pose_cs_);
   else if(std::strcmp(entry,"nativeShadowPrepareCS")==0)hr=create_shader(device_.Get(),shader->GetBufferPointer(),shader->GetBufferSize(),nullptr,&native_shadow_prepare_cs_);
   else if(std::strcmp(entry,"nativeShadowCommitCS")==0)hr=create_shader(device_.Get(),shader->GetBufferPointer(),shader->GetBufferSize(),nullptr,&native_shadow_commit_cs_);
   else if(std::strcmp(entry,"nativeShadowBuildCS")==0)hr=create_shader(device_.Get(),shader->GetBufferPointer(),shader->GetBufferSize(),nullptr,&native_shadow_build_cs_);
   else if(std::strcmp(entry,"nativeShadowArgsGuardCS")==0)hr=create_shader(device_.Get(),shader->GetBufferPointer(),shader->GetBufferSize(),nullptr,&native_shadow_args_guard_cs_);
   else if(std::strcmp(entry,"nativeTerrainMissCompactCS")==0)hr=create_shader(device_.Get(),shader->GetBufferPointer(),shader->GetBufferSize(),nullptr,&native_terrain_miss_compact_cs_);
   else if(programs[program].defines&&std::strcmp(programs[program].defines[0].Name,"NATIVE_ASTRAL")==0){
    // The astral rift programs (append_native_shader_programs' last three).
    if(std::strcmp(entry,"nativeVS")==0)hr=create_shader(device_.Get(),shader->GetBufferPointer(),shader->GetBufferSize(),nullptr,native_astral_vs_.ReleaseAndGetAddressOf());
    else hr=create_shader(device_.Get(),shader->GetBufferPointer(),shader->GetBufferSize(),nullptr,
     programs[program].defines[1].Name?native_astral_low_ps_.ReleaseAndGetAddressOf():native_astral_ps_.ReleaseAndGetAddressOf());
   }
   else if(std::strstr(entry,"VS")&&programs[program].defines&&std::strcmp(programs[program].defines[0].Name,"NATIVE_DOMAIN")==0){
    // A material instance vertex program (class from its define list).
    std::size_t vertex_class=SIZE_MAX;bool check=false;
    for(std::size_t c=0;c<kNativeInstanceVertexClasses;++c){
     if(programs[program].defines==native_instance_vertex_defines(c,false))vertex_class=c;
     else if(programs[program].defines==native_instance_vertex_defines(c,false,true)){vertex_class=c;check=true;}
    }
    if(vertex_class==SIZE_MAX)return native_program_failed(program,entry,E_UNEXPECTED);
    if(check)continue; // created with the check pixel programs, on first use
    auto& target=std::strcmp(entry,"nativePrepassVS")==0?native_instance_prepass_vs_[vertex_class]:native_instance_vs_[vertex_class];
    hr=create_shader(device_.Get(),shader->GetBufferPointer(),shader->GetBufferSize(),nullptr,target.ReleaseAndGetAddressOf());
   }
   else if(std::strcmp(entry,"nativePS")==0&&programs[program].defines&&std::strcmp(programs[program].defines[0].Name,"NATIVE_DOMAIN")==0){
    // Its slot from its define list (native_instance_defines' own pointers).
    std::size_t index=SIZE_MAX;bool check=false;
    for(std::size_t slot=0;slot<kNativeInstanceSlots&&index==SIZE_MAX;++slot)for(unsigned variant=0;variant<4u;++variant)
     if(programs[program].defines==native_instance_defines(slot,(variant&1u)!=0,(variant&2u)!=0,false)){index=slot*2u+(variant&1u);check=(variant&2u)!=0;break;}
    if(index==SIZE_MAX)return native_program_failed(program,entry,E_UNEXPECTED);
    // The check programs are created on first use (ensure_native_instance_check_programs).
    if(check)continue;
    hr=create_shader(device_.Get(),shader->GetBufferPointer(),shader->GetBufferSize(),nullptr,native_instance_ps_[index].ReleaseAndGetAddressOf());
   }
   else if(std::strcmp(entry,"nativePS")==0){
    // The one index->member mapping, in append_native_shader_programs order.
    // Pointers to members: WRL's &comptr is a ComPtrRef whose conversion to
    // ComPtr* nulls the member, so an array of &member_ rebuilt per program
    // released every variant created before it (only the last survived).
    static constexpr ComPtr<ID3D11PixelShader> Renderer::* variants[]={
     &Renderer::native_terrain_ps_,&Renderer::native_snow_ps_,&Renderer::native_low_ps_,&Renderer::native_low_unlit_ps_,
     
     &Renderer::native_terrain_flat_ps_,&Renderer::native_snow_flat_ps_,
     &Renderer::native_masked_ps_,&Renderer::native_masked_low_ps_,
     &Renderer::native_masked_terrain_ps_,&Renderer::native_masked_snow_ps_,
     &Renderer::native_masked_terrain_flat_ps_,&Renderer::native_masked_snow_flat_ps_,
     &Renderer::native_masked_terrain_slim_ps_,&Renderer::native_masked_snow_slim_ps_,
     &Renderer::native_masked_terrain_slim_flat_ps_,&Renderer::native_masked_snow_slim_flat_ps_,
 &Renderer::native_gltf_ps_,&Renderer::native_gltf_low_ps_,
 &Renderer::native_gltf_masked_ps_,&Renderer::native_gltf_masked_low_ps_};
    if(pixel_variant>=std::size(variants))return native_program_failed(program,entry,E_UNEXPECTED);
    hr=create_shader(device_.Get(),shader->GetBufferPointer(),shader->GetBufferSize(),nullptr,(this->*variants[pixel_variant++]).ReleaseAndGetAddressOf());
   }
   else if(std::strcmp(entry,"nativeLiquidPS")==0){
    static constexpr ComPtr<ID3D11PixelShader> Renderer::* liquids[]={&Renderer::native_liquid_ps_,&Renderer::native_low_liquid_ps_,
     &Renderer::native_liquid_ssr_ps_,&Renderer::native_liquid_foam_ps_,
     &Renderer::native_water_ps_,&Renderer::native_water_ssr_ps_,&Renderer::native_low_water_ps_,&Renderer::native_lava_ps_,
     &Renderer::native_liquid_ssr_input_ps_,&Renderer::native_liquid_swim_ps_,&Renderer::native_water_swim_ps_};
    if(liquid_variant>=std::size(liquids))return native_program_failed(program,entry,E_UNEXPECTED);
    hr=create_shader(device_.Get(),shader->GetBufferPointer(),shader->GetBufferSize(),nullptr,(this->*liquids[liquid_variant++]).ReleaseAndGetAddressOf());
   }
   else if(std::strcmp(entry,"nativeLiquidSsrFilterPS")==0)
    hr=create_shader(device_.Get(),shader->GetBufferPointer(),shader->GetBufferSize(),nullptr,native_liquid_ssr_filter_ps_.ReleaseAndGetAddressOf());
   else if(std::strcmp(entry,"nativeLiquidSsrPS")==0)
    hr=create_shader(device_.Get(),shader->GetBufferPointer(),shader->GetBufferSize(),nullptr,native_liquid_ssr_march_ps_.ReleaseAndGetAddressOf());
   else if(std::strcmp(entry,"nativePrepassPS")==0)
    hr=create_shader(device_.Get(),shader->GetBufferPointer(),shader->GetBufferSize(),nullptr,
     !native_prepass_ps_?native_prepass_ps_.ReleaseAndGetAddressOf():native_low_prepass_ps_.ReleaseAndGetAddressOf());
   else if(std::strcmp(entry,"nativeDepthClearVS")==0)
    hr=create_shader(device_.Get(),shader->GetBufferPointer(),shader->GetBufferSize(),nullptr,native_depth_clear_vs_.ReleaseAndGetAddressOf());
   else if(std::strcmp(entry,"nativePrepassVS")==0)
    hr=create_shader(device_.Get(),shader->GetBufferPointer(),shader->GetBufferSize(),nullptr,native_prepass_vs_.ReleaseAndGetAddressOf());
   else if(std::strcmp(entry,"nativeMotionVS")==0)
    hr=create_shader(device_.Get(),shader->GetBufferPointer(),shader->GetBufferSize(),nullptr,native_motion_vs_.ReleaseAndGetAddressOf());
   else if(std::strcmp(entry,"nativeMotionPS")==0)
    hr=create_shader(device_.Get(),shader->GetBufferPointer(),shader->GetBufferSize(),nullptr,native_motion_ps_.ReleaseAndGetAddressOf());
   else if(std::strcmp(entry,"nativeShadowMaskVS")==0)
    hr=create_shader(device_.Get(),shader->GetBufferPointer(),shader->GetBufferSize(),nullptr,native_shadow_mask_vs_.ReleaseAndGetAddressOf());
   else if(std::strcmp(entry,"nativeShadowMaskPS")==0)
    hr=create_shader(device_.Get(),shader->GetBufferPointer(),shader->GetBufferSize(),nullptr,
     !native_shadow_mask_ps_?native_shadow_mask_ps_.ReleaseAndGetAddressOf():native_low_shadow_mask_ps_.ReleaseAndGetAddressOf());
   else if(std::strstr(entry,"VS"))hr=create_shader(device_.Get(),shader->GetBufferPointer(),shader->GetBufferSize(),nullptr,&native_shadow_vs_);
   else hr=create_shader(device_.Get(),shader->GetBufferPointer(),shader->GetBufferSize(),nullptr,&native_shadow_ps_);
   if(FAILED(hr))return native_program_failed(program,entry,hr);
  }
  find_prebuilt_native_specialisations(); // the prebuilt specialised sets, once (key changes only look up)
  queue_startup_specialisations();
  (void)create_shelter_filter_shaders();  // never at the first rain or snow bake in play
  D3D11_BUFFER_DESC shadows{};shadows.ByteWidth=sizeof(NativeShadowConstants);shadows.Usage=D3D11_USAGE_DYNAMIC;shadows.BindFlags=D3D11_BIND_CONSTANT_BUFFER;shadows.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
  if(FAILED(hr=reserve_native_owned_texture_bytes(shadows.ByteWidth,true)))return hr;
  if(FAILED(hr=device_->CreateBuffer(&shadows,nullptr,&native_shadow_constants_))||FAILED(hr=upload_native_shadow_constants(0)))return hr;
  D3D11_BUFFER_DESC lighting{};lighting.ByteWidth=sizeof(NativeWorldLighting);lighting.Usage=D3D11_USAGE_DYNAMIC;lighting.BindFlags=D3D11_BIND_CONSTANT_BUFFER;lighting.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
  if(FAILED(hr=device_->CreateBuffer(&lighting,nullptr,&native_world_lighting_constants_)))return hr;
  // 32 bytes of point-light grid, then the cave volume (cave_irradiance.h).
  D3D11_BUFFER_DESC point_count{};point_count.ByteWidth=112;point_count.Usage=D3D11_USAGE_DYNAMIC;point_count.BindFlags=D3D11_BIND_CONSTANT_BUFFER;point_count.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
  const std::array<UINT,28> no_point_lights{};D3D11_SUBRESOURCE_DATA initial_count{};initial_count.pSysMem=no_point_lights.data();
  if(FAILED(hr=device_->CreateBuffer(&point_count,&initial_count,&native_point_light_constants_)))return hr;
  native_point_constants_dirty_=true;
 D3D11_BUFFER_DESC work_range{};work_range.ByteWidth=32;work_range.Usage=D3D11_USAGE_DYNAMIC;
 work_range.BindFlags=D3D11_BIND_CONSTANT_BUFFER;work_range.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
 if(FAILED(hr=device_->CreateBuffer(&work_range,nullptr,&native_work_range_constants_)))return hr;
 // Terrain value-noise lattice (terrainNoise): each texel is the top byte of
 // the shader's integer lattice hash (terrainHash) of its own coordinates,
 // integer math only, so every startup and machine makes the same bytes.
 if(!terrain_noise_srv_){
  constexpr UINT side=1024;
  std::vector<std::uint8_t> lattice(std::size_t(side)*side);
  for(std::uint32_t y=0;y<side;++y)for(std::uint32_t x=0;x<side;++x){
   std::uint32_t h=x*0x8da6b343u^y*0xd8163841u;
   h^=h>>16;h*=0x7feb352du;h^=h>>15;h*=0x846ca68bu;h^=h>>16;
   lattice[std::size_t(y)*side+x]=static_cast<std::uint8_t>(h>>24);
  }
  D3D11_TEXTURE2D_DESC noise{};noise.Width=noise.Height=side;noise.MipLevels=noise.ArraySize=1;noise.Format=DXGI_FORMAT_R8_UNORM;
  noise.SampleDesc.Count=1;noise.Usage=D3D11_USAGE_IMMUTABLE;noise.BindFlags=D3D11_BIND_SHADER_RESOURCE;
  const D3D11_SUBRESOURCE_DATA initial_noise{lattice.data(),side,0};
  ComPtr<ID3D11Texture2D> noise_texture;
  if(FAILED(hr=device_->CreateTexture2D(&noise,&initial_noise,&noise_texture))||
     FAILED(hr=device_->CreateShaderResourceView(noise_texture.Get(),nullptr,&terrain_noise_srv_)))return hr;
  D3D11_SAMPLER_DESC wrap{};wrap.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
  wrap.AddressU=wrap.AddressV=wrap.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;wrap.MaxLOD=0;
  if(FAILED(hr=device_->CreateSamplerState(&wrap,&terrain_noise_sampler_))){terrain_noise_srv_.Reset();return hr;}
 }
 hr=create_native_geometry_arena();
 if(SUCCEEDED(hr))bridge_set_native_terrain_animation_owned(true);
 return hr;
}

HRESULT Renderer::create_native_geometry_resource(
    UINT capacity,ComPtr<ID3D11Buffer>& buffer,
    ComPtr<ID3D11ShaderResourceView>& srv){
 buffer.Reset();srv.Reset();
 if(!device_||capacity<16||capacity%4)return E_INVALIDARG;
 if(capacity>kNativeRawBufferLimit)
  return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
#ifdef EID3D11_TESTING
 if(test_native_geometry_buffer_failures_){
  --test_native_geometry_buffer_failures_;return E_OUTOFMEMORY;
 }
#endif
 D3D11_BUFFER_DESC description{};description.ByteWidth=capacity;
 description.Usage=D3D11_USAGE_DEFAULT;
 description.BindFlags=D3D11_BIND_SHADER_RESOURCE;
 description.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
 HRESULT hr=device_->CreateBuffer(&description,nullptr,&buffer);
 if(FAILED(hr))return hr;
 D3D11_SHADER_RESOURCE_VIEW_DESC view{};view.Format=DXGI_FORMAT_R32_TYPELESS;
 view.ViewDimension=D3D11_SRV_DIMENSION_BUFFEREX;
 view.BufferEx.NumElements=capacity/4;
 view.BufferEx.Flags=D3D11_BUFFEREX_SRV_FLAG_RAW;
#ifdef EID3D11_TESTING
 if(test_native_geometry_view_failures_){
  --test_native_geometry_view_failures_;buffer.Reset();return E_FAIL;
 }
#endif
 hr=device_->CreateShaderResourceView(buffer.Get(),&view,&srv);
 if(FAILED(hr))buffer.Reset();
 return hr;
}

HRESULT Renderer::create_native_geometry_arena(){
 if(!d3d12_&&!device_)return E_FAIL;
 if(d3d12_){ // D3D12 backend: the first page as a D3D12 buffer (as native_d3d12_ensure makes it)
  NativeGeometryPage page;page.capacity=16;page.high_water=16;
  try{page.free.reserve(1);}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
  const HRESULT hr=d3d12_->create_buffer(16,0,0,page.d3d12);if(FAILED(hr))return hr;
  try{std::vector<NativeGeometryPage> pages;pages.push_back(std::move(page));native_geometry_pages_=std::move(pages);}
  catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
  native_static_content_changed();native_meshes_.clear();
  native_geometry_live_bytes_=0;native_mesh_resident_bytes_=0;
  return S_OK;
 }
 ComPtr<ID3D11Buffer> buffer;ComPtr<ID3D11ShaderResourceView> srv;
 const HRESULT hr=create_native_geometry_resource(16,buffer,srv);
 if(FAILED(hr))return hr;
 try{
  std::vector<NativeGeometryPage> pages;pages.reserve(1);
  NativeGeometryPage page;page.buffer=std::move(buffer);page.srv=std::move(srv);
  page.capacity=16;page.high_water=16;page.free.reserve(1);
  pages.push_back(std::move(page));native_geometry_pages_=std::move(pages);
 }catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
 native_static_content_changed();native_meshes_.clear();
 native_geometry_live_bytes_=0;native_mesh_resident_bytes_=0;
 return S_OK;
}

UINT Renderer::native_geometry_page_limit() const noexcept{
#ifdef EID3D11_TESTING
 if(test_native_geometry_page_limit_)
  return std::clamp(test_native_geometry_page_limit_,16u,kNativeRawBufferLimit);
#endif
 return kNativeRawBufferLimit;
}

std::uint64_t Renderer::native_geometry_capacity_bytes() const noexcept{
 std::uint64_t total=0;for(const auto& page:native_geometry_pages_)total+=page.capacity;
 return total;
}

std::uint64_t Renderer::native_geometry_high_water_bytes() const noexcept{
 std::uint64_t total=0;for(const auto& page:native_geometry_pages_)total+=page.high_water;
 return total;
}

std::uint64_t Renderer::native_geometry_free_bytes() const noexcept{
 std::uint64_t total=0;for(const auto& page:native_geometry_pages_)
  for(const auto& span:page.free)total+=span.bytes;
 return total;
}

ID3D11ShaderResourceView* Renderer::native_geometry_page_srv(UINT page) const noexcept{
 return page<native_geometry_pages_.size()?native_geometry_pages_[page].srv.Get():nullptr;
}

HRESULT Renderer::ensure_native_geometry_page_capacity(UINT page_index,UINT required){
 UINT aligned=0;
 const UINT limit=native_geometry_page_limit();
 if(!align_up_checked(required,16,aligned)||aligned>limit)
  return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
 required=aligned;
 if(page_index>=native_geometry_pages_.size())return E_INVALIDARG;
 auto& page=native_geometry_pages_[page_index];
 if(required<=page.capacity)return S_OK;
 auto* color=active_color_resource_;
 ColorDiagnosticTicks arena_timing(color?&color->arena_ticks:nullptr,
  color&&!color_mesh_active_?&color->nonmesh_arena_ticks:nullptr);
 if(color)++color->arena_grows;
 if(d3d12_){
  // D3D12 backend: a larger page buffer with the live bytes copied in stream order.
  if(!page.d3d12)return E_FAIL;
  UINT target=demand_growth_capacity(required,page.capacity,limit);
  if(!target)return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
  D3D12BufferRef replacement;
  HRESULT hr=d3d12_->create_buffer(target,0,0,replacement);
  if(FAILED(hr)&&target!=required){target=required;hr=d3d12_->create_buffer(target,0,0,replacement);}
  if(FAILED(hr))return hr;
  if(page.high_water&&FAILED(hr=d3d12_->copy_buffer(*replacement,*page.d3d12,page.high_water)))return hr;
  page.d3d12=std::move(replacement);page.capacity=target;
  return S_OK;
 }
 if(!device_||!context_||!page.buffer)return E_FAIL;
 const frame_acct::Scope growth_scope(frame_acct::geometry_growth);

 UINT target=demand_growth_capacity(required,page.capacity,limit);
 if(!target)return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
 if(target!=required&&adapter3_){
  DXGI_QUERY_VIDEO_MEMORY_INFO memory{};
  if(SUCCEEDED(adapter3_->QueryVideoMemoryInfo(
       0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&memory))){
   const std::uint64_t headroom=memory.Budget>memory.CurrentUsage?
    memory.Budget-memory.CurrentUsage:0;
   // The replacement coexists with the live arena until its copy is queued.
   // Budget data is therefore only a hint to omit speculative slack; the
   // allocation/view HRESULT remains the admission authority.
   if(headroom<target)target=required;
  }
 }

 const auto allocate=[&](UINT capacity,ComPtr<ID3D11Buffer>& buffer,
                         ComPtr<ID3D11ShaderResourceView>& srv){
  return create_native_geometry_resource(capacity,buffer,srv);
 };
 ComPtr<ID3D11Buffer> replacement;
 ComPtr<ID3D11ShaderResourceView> replacement_srv;
 UINT created_capacity=target;
 HRESULT hr=allocate(target,replacement,replacement_srv);
 if(FAILED(hr)&&target!=required){
  replacement.Reset();replacement_srv.Reset();
  created_capacity=required;
  hr=allocate(required,replacement,replacement_srv);
 }
 if(FAILED(hr))return hr;
 if(page.high_water){
  D3D11_BOX box{};box.right=page.high_water;box.bottom=box.back=1;
  context_->CopySubresourceRegion(replacement.Get(),0,0,0,0,
                                  page.buffer.Get(),0,&box);
  if(color)color->arena_copy_bytes+=page.high_water;
 }
 page.buffer=std::move(replacement);page.srv=std::move(replacement_srv);
 page.capacity=created_capacity;
 return S_OK;
}

HRESULT Renderer::ensure_native_geometry_capacity(UINT required){
 return native_geometry_pages_.empty()?E_FAIL:
  ensure_native_geometry_page_capacity(0,required);
}

HRESULT Renderer::allocate_native_geometry_span(UINT requested,
                                                 NativeGeometrySpan& output){
 output={};UINT bytes=0;const UINT limit=native_geometry_page_limit();
 if(!requested||!align_up_checked(requested,16,bytes)||bytes>limit-16u)
  return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
 if(native_geometry_pages_.empty()||(!d3d12_&&(!device_||!context_)))return E_FAIL;
 UINT best_page=UINT_MAX,best_slot=UINT_MAX,best_bytes=UINT_MAX;
 for(UINT page_index=0;page_index<native_geometry_pages_.size();++page_index){
  const auto& page=native_geometry_pages_[page_index];
  if(page.dedicated)continue;
  for(UINT slot=0;slot<page.free.size();++slot)if(page.free[slot].bytes>=bytes&&
     page.free[slot].bytes<best_bytes){best_page=page_index;best_slot=slot;
      best_bytes=page.free[slot].bytes;}
 }
 if(best_page!=UINT_MAX){
  auto& page=native_geometry_pages_[best_page];
  try{page.free.reserve(page.free.size()+1);}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
  const UINT offset=page.free[best_slot].offset;
  if(page.free[best_slot].bytes==bytes)page.free.erase(page.free.begin()+best_slot);
  else {page.free[best_slot].offset+=bytes;page.free[best_slot].bytes-=bytes;}
  output={best_page,offset,bytes};native_geometry_live_bytes_+=bytes;
  native_mesh_resident_bytes_=native_geometry_live_bytes_;return S_OK;
 }
 for(std::size_t reverse=0;reverse<native_geometry_pages_.size();++reverse){
  const UINT page_index=static_cast<UINT>(native_geometry_pages_.size()-1-reverse);
  auto& page=native_geometry_pages_[page_index];
  if(page.dedicated)continue;
  const UINT usable=(std::min)(page.capacity,limit);
  if(page.high_water<=usable&&bytes<=usable-page.high_water){
   try{page.free.reserve(page.free.size()+1);}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
   output={page_index,page.high_water,bytes};page.high_water+=bytes;
   native_geometry_live_bytes_+=bytes;native_mesh_resident_bytes_=native_geometry_live_bytes_;
   return S_OK;
  }
 }
 HRESULT growth_failure=E_FAIL;
 // A general page grows in place (new buffer plus a copy of its live bytes)
 // only up to kNativeGeneralPageBytes; past that the arena opens another
 // page instead. Doubling page 0 from 128 to 256 MiB in play (one lazily
 // created terrain mesh) stalled the next Present ~25 ms (zone 3 route,
 // frame ~1058): the new allocation is paid on the GPU's first use, and
 // the copy moves the whole live arena.
 const UINT general_page_bytes=(std::min)(kNativeGeneralPageBytes,limit);
 UINT newest=static_cast<UINT>(native_geometry_pages_.size()-1);
 while(newest&&native_geometry_pages_[newest].dedicated)--newest; // page 0 is general
 auto& page=native_geometry_pages_[newest];
 if(!page.dedicated&&page.high_water<=limit&&bytes<=limit-page.high_water&&
    std::uint64_t(page.high_water)+bytes<=general_page_bytes){
  try{page.free.reserve(page.free.size()+1);}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
  growth_failure=ensure_native_geometry_page_capacity(newest,page.high_water+bytes);
  if(SUCCEEDED(growth_failure)){
   ++native_geometry_growths_;
   output={newest,page.high_water,bytes};page.high_water+=bytes;
   native_geometry_live_bytes_+=bytes;native_mesh_resident_bytes_=native_geometry_live_bytes_;
   return S_OK;
  }
 }
 const UINT required=16u+bytes;
 // A new general page starts at the general page size (a larger mesh gets
 // its own power-of-two page), so later meshes fill it without growth.
 UINT target=grow_capacity(required,general_page_bytes,limit);if(!target||target<required)
  return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
 if(adapter3_&&target!=required){DXGI_QUERY_VIDEO_MEMORY_INFO memory{};
  if(SUCCEEDED(adapter3_->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&memory))){
   const std::uint64_t headroom=memory.Budget>memory.CurrentUsage?
    memory.Budget-memory.CurrentUsage:0;if(headroom<target)target=required;}}
 try{native_geometry_pages_.reserve(native_geometry_pages_.size()+1);}
 catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
 NativeGeometryPage created;created.capacity=target;created.high_water=16u+bytes;
 try{created.free.reserve(1);}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
 auto* color=active_color_resource_;
 ColorDiagnosticTicks arena_timing(color?&color->arena_ticks:nullptr,
  color&&!color_mesh_active_?&color->nonmesh_arena_ticks:nullptr);
 if(color)++color->arena_grows;
 HRESULT hr=d3d12_?d3d12_->create_buffer(target,0,0,created.d3d12):create_native_geometry_resource(target,created.buffer,created.srv);
 if(FAILED(hr)&&target!=required){created.buffer.Reset();created.srv.Reset();created.d3d12.reset();
  created.capacity=required;hr=d3d12_?d3d12_->create_buffer(required,0,0,created.d3d12):create_native_geometry_resource(required,created.buffer,created.srv);}
 if(FAILED(hr))return FAILED(growth_failure)?hr:growth_failure;
 // An empty general slot (release_empty_native_geometry_pages) takes it first.
 UINT page_index=static_cast<UINT>(native_geometry_pages_.size());
 for(UINT i=1;i<native_geometry_pages_.size();++i)
  if(!native_geometry_pages_[i].dedicated&&!native_geometry_pages_[i].buffer&&!native_geometry_pages_[i].d3d12){page_index=i;break;}
 if(page_index<native_geometry_pages_.size())native_geometry_pages_[page_index]=std::move(created);
 else native_geometry_pages_.push_back(std::move(created));
 ++native_geometry_growths_;
 output={page_index,16u,bytes};native_geometry_live_bytes_+=bytes;
 native_mesh_resident_bytes_=native_geometry_live_bytes_;return S_OK;
}

bool Renderer::release_native_geometry_span(const NativeGeometrySpan& span) noexcept{
 if(span.page>=native_geometry_pages_.size()||!span.bytes||span.offset<16||
    span.offset>native_geometry_pages_[span.page].high_water||
    span.bytes>native_geometry_pages_[span.page].high_water-span.offset)return false;
 auto& free=native_geometry_pages_[span.page].free;
 try{free.reserve(free.size()+1);}catch(const std::bad_alloc&){return false;}
 const auto position=std::lower_bound(free.begin(),free.end(),span.offset,
  [](const NativeGeometryFreeSpan& item,UINT offset){return item.offset<offset;});
 if(position!=free.begin()){
  const auto& previous=*(position-1);if(previous.offset+previous.bytes>span.offset)return false;
 }
 if(position!=free.end()&&span.offset+span.bytes>position->offset)return false;
 auto inserted=free.insert(position,{span.offset,span.bytes});
 if(inserted!=free.begin()){
  auto previous=inserted-1;if(previous->offset+previous->bytes==inserted->offset){
   previous->bytes+=inserted->bytes;inserted=free.erase(inserted);inserted=previous;}
 }
 if(inserted+1!=free.end()&&inserted->offset+inserted->bytes==(inserted+1)->offset){
  inserted->bytes+=(inserted+1)->bytes;free.erase(inserted+1);
 }
 if(native_geometry_live_bytes_>=span.bytes)native_geometry_live_bytes_-=span.bytes;
 native_mesh_resident_bytes_=native_geometry_live_bytes_;
 auto& owner=native_geometry_pages_[span.page];
 if(owner.dedicated){
  owner.live=owner.live>span.bytes?owner.live-span.bytes:0;
  if(!owner.live){ // its last mesh: the slot stays, empty, for the next adoption
   owner.buffer.Reset();owner.srv.Reset();owner.d3d12.reset();owner.capacity=0;owner.high_water=16;owner.free.clear();
  }
 }
 return true;
}

#ifdef EID3D11_TESTING
HRESULT Renderer::test_reset_native_geometry_arena(UINT capacity){
 if(!native_pending_draws_.empty())return E_UNEXPECTED;
 ComPtr<ID3D11Buffer> buffer;ComPtr<ID3D11ShaderResourceView> srv;
 HRESULT hr=create_native_geometry_resource(capacity,buffer,srv);if(FAILED(hr))return hr;
 std::vector<NativeGeometryPage> pages;
 try{pages.reserve(1);NativeGeometryPage page;page.buffer=std::move(buffer);
  page.srv=std::move(srv);page.capacity=capacity;page.high_water=16;page.free.reserve(1);
  pages.push_back(std::move(page));}
 catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
 invalidate_native_prepared_handles();native_meshes_.clear();native_geometry_pages_=std::move(pages);
 native_geometry_live_bytes_=0;native_mesh_resident_bytes_=0;
 return S_OK;
}

HRESULT Renderer::test_reclaim_native_geometry_working_set(){
 retire_native_source_values();
 return retire_stale_native_geometry(native_frame_index_);
}

HRESULT Renderer::test_reserve_native_geometry(UINT required){
 return ensure_native_geometry_capacity(required);
}

UINT Renderer::test_native_geometry_capacity() const noexcept{
 return static_cast<UINT>((std::min<std::uint64_t>)(
  native_geometry_capacity_bytes(),UINT_MAX));
}

UINT Renderer::test_native_geometry_used() const noexcept{
 return static_cast<UINT>((std::min<std::uint64_t>)(
  native_geometry_high_water_bytes(),UINT_MAX));
}

std::uint64_t Renderer::test_native_geometry_free() const noexcept{
 return native_geometry_free_bytes();
}

bool Renderer::test_native_mesh_span(std::uintptr_t cache_identity,UINT& page,
                                     UINT& offset,UINT& bytes) const noexcept{
 page=offset=bytes=0;
 for(const auto& entry:native_meshes_)if(entry.first.selector.cache_identity==cache_identity){
  page=entry.second.allocation.page;offset=entry.second.allocation.offset;
  bytes=entry.second.allocation.bytes;return true;
 }
 return false;
}

bool Renderer::test_native_dispatch_grid(UINT count,UINT& width,UINT& height) const noexcept{
 return native_dispatch_dimensions(count,width,height);
}

HRESULT Renderer::test_seed_native_prepared_slot_boundaries(
 UINT mesh_slots,UINT material_slots,bool first_reusable){
 if(!native_pending_draws_.empty())return E_UNEXPECTED;
 if(mesh_slots==UINT_MAX||material_slots==UINT_MAX)
  return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
 invalidate_native_prepared_handles();
 native_prepared_mesh_slots_.clear();native_prepared_material_slots_.clear();
 try{
  native_prepared_mesh_slots_.resize(mesh_slots);
  native_prepared_material_slots_.resize(material_slots);
 }catch(const std::bad_alloc&){
  native_prepared_mesh_slots_.clear();native_prepared_material_slots_.clear();
  return E_OUTOFMEMORY;
 }
 for(auto& slot:native_prepared_mesh_slots_){
  slot.active=true;slot.source_revision=1;slot.last_used_frame=native_frame_index_;
 }
 for(auto& slot:native_prepared_material_slots_){
  slot.active=true;slot.persistent=true;slot.last_used_frame=native_frame_index_;
 }
 if(first_reusable){
  if(!native_prepared_mesh_slots_.empty())native_prepared_mesh_slots_.front().active=false;
  if(!native_prepared_material_slots_.empty())native_prepared_material_slots_.front().active=false;
 }
 native_prepared_mesh_reuse_cursor_=0;
 native_prepared_material_reuse_cursor_=0;
 native_prepared_mesh_reuse_sweep_frame_=native_frame_index_;
 native_prepared_material_reuse_sweep_frame_=native_frame_index_;
 return S_OK;
}

bool Renderer::test_native_prepared_record_addresses_current(
 const NativePreparedMeshHandle& mesh_handle,
 const NativePreparedMaterialHandle& material_handle,bool shadows) const noexcept{
 if(mesh_handle.slot>=native_prepared_mesh_slots_.size()||
    material_handle.slot>=native_prepared_material_slots_.size())return false;
 const auto& mesh_slot=native_prepared_mesh_slots_[mesh_handle.slot];
 const auto& material_slot=native_prepared_material_slots_[material_handle.slot];
 if(!mesh_slot.active||mesh_slot.generation!=mesh_handle.generation||!mesh_slot.mesh||
    !material_slot.active||material_slot.generation!=material_handle.generation)return false;
 const UINT record_index=material_slot.persistent_instance[shadows?1:0];
 if(record_index>=native_instance_records_.size())return false;
 const auto& record=native_instance_records_[record_index];const auto& mesh=*mesh_slot.mesh;
 const auto& instance=record.command.instance;const auto& descriptor=record.descriptor;
 return record.occupied&&record.owned&&record.command.mesh==mesh_slot.mesh&&
  record.command.geometry_page==mesh.allocation.page&&
  instance.geometry_offsets==std::array<UINT,4>{mesh.position_offset,mesh.normal_offset,
   mesh.texture_coordinate_offset,mesh.component_offset}&&
  instance.color_offsets==std::array<UINT,2>{mesh.diffuse_offset,mesh.specular_offset}&&
  instance.terrain_vertex_offset==mesh.terrain_vertex_offset&&
  descriptor.component_offset==mesh.component_offset&&descriptor.index_offset==mesh.index_offset;
}
#endif

HRESULT Renderer::upload_native_geometry(UINT page_index,UINT offset,const void* data,UINT bytes){
 PERF06_SCOPE("upload_geometry");
 if((!d3d12_&&!context_)||page_index>=native_geometry_pages_.size()||!data||!bytes)return E_INVALIDARG;
 if(d3d12_){
  auto& page=native_geometry_pages_[page_index];
  if(!page.d3d12||offset>page.capacity||bytes>page.capacity-offset)return E_INVALIDARG;
  return d3d12_->write_buffer_bulk(*page.d3d12,offset,data,bytes); // load geometry: the bulk upload pool
 }
#ifdef EID3D11_TESTING
 if(test_native_geometry_upload_failure_&&!--test_native_geometry_upload_failure_)
  return E_FAIL;
#endif
 auto& page=native_geometry_pages_[page_index];
 if(!page.buffer||offset>page.capacity||bytes>page.capacity-offset)return E_INVALIDARG;
 D3D11_BOX box{};box.left=offset;box.right=offset+bytes;box.top=0;box.bottom=1;box.front=0;box.back=1;
 const frame_acct::Scope upload_scope(frame_acct::mesh_upload);
 auto* color=active_color_resource_;
 ColorDiagnosticTicks upload_timing(color?&color->geometry_upload_ticks:nullptr,
  color&&!color_mesh_active_?&color->nonmesh_geometry_upload_ticks:nullptr);
 context_->UpdateSubresource(page.buffer.Get(),0,&box,data,bytes,0);
 if(active_color_resource_)active_color_resource_->geometry_upload_bytes+=bytes;
 profiler_.add_upload(bytes);return S_OK;
}

HRESULT Renderer::upload_native_work_range(const NativeDispatchRange& range,UINT motion_stamp,UINT chunks,UINT scratch,bool capture_lod){
 PERF06_SCOPE("upload_work");
 if(!context_||!native_work_range_constants_||!range.count)return E_INVALIDARG;
 const std::array<UINT,8> values{range.first,range.count,motion_stamp,0,chunks,scratch,capture_lod?1u:0u,0}; // nativeMotionStamp (storeNativeWorldVertex), nativeWorkChunks, nativeWorkScratch
 D3D11_MAPPED_SUBRESOURCE mapped{};
 const HRESULT hr=context_->Map(native_work_range_constants_.Get(),0,
  D3D11_MAP_WRITE_DISCARD,0,&mapped);if(FAILED(hr))return hr;
 std::memcpy(mapped.pData,values.data(),sizeof(values));
 context_->Unmap(native_work_range_constants_.Get(),0);
 context_->CSSetConstantBuffers(4,1,native_work_range_constants_.GetAddressOf());
 return S_OK;
}
Renderer::~Renderer() {
#ifdef EID3D11_EMBEDDED
 embedded_sky().detach(this);
#endif
 join_host_memory_worker();release_native_specialisation();widescreen_clear_hud_frame_sink(this);retained_visual_scene().visual_clear_surface_texture_retainer(this);}

void Renderer::release() {
#ifdef EID3D11_EMBEDDED
 (void)finish_embedded_scene_commands();
 embedded_sky().detach(this);
#endif
 // Scene thread: run and present what is queued, end it; the game thread
 // owns the renderer for teardown.
 set_scene_mode(false);
 // Retained map references must go before the mesh and instance tables below.
 // The host has no game-side bridge registration to retire them for us.
 discard_world_map_markers();
 clear_world_map_preview_gpu();
#ifndef EID3D11_EMBEDDED
 bridge_clear_world_map_previews(this);
#endif
 world_map_preview_.deactivate();
 world_map_preview_generation_ = 0;
 world_map_preview_reasons_.clear();
 scene_thread_.reset();
#if defined(EID3D11_EMBEDDED) && !defined(EID3D11_SHADER_EMBED)
 embedded_d3d12_.stop_ngx(); // joined frame owner; executor/device still alive
 dlss_backend_.shutdown(); // no remaining frame calls; before any device resources are released
#endif
 write_harness_path_rows(); // frame accounting rows still buffered
#ifndef EID3D11_EMBEDDED
 camp_inventory_shutdown_icons();
#endif
 release_interact_overlay();
 camp_inventory_retire_textures();
 // Detached copies belong to this device and must not reach the next one.
 retained_visual_scene().visual_clear_surface_texture_retainer(this);
 neighbour_client_stop();neighbour_maps_set_device(nullptr);neighbour_maps_set_d3d12(nullptr);release_neighbour_maps();
 // Tile family jobs (CPU only): cancelled and joined before the device goes.
 tile_families_job_={};tile_families_prestart_={};tile_families_prestart_terrain_.reset();
 join_tile_family_jobs();
 load_progress_movie_={};load_progress_movie_valid_=false;cancel_load_progress_decode(); // its frame textures belong to this device
 if(liquid_maps_job_.valid())liquid_maps_job_.wait();liquid_maps_job_={};
 liquid_normal_srv_.Reset();liquid_detail_srv_.Reset();liquid_normal_sampler_.Reset();
 liquid_swamp_frame_srv_.Reset();liquid_swamp_frame_generation_=UINT64_MAX;
 if(liquid_surf_job_.valid())liquid_surf_job_.wait();liquid_surf_job_={};
 liquid_surf_srv_.Reset();liquid_surf_level_srv_.Reset();liquid_surf_built_serial_=0; // the CPU fields stay; the next device composites them again
 for(unsigned k=0;k<2;++k){liquid_wave_uav_[k].Reset();liquid_wave_srv_[k].Reset();liquid_wave_texture_[k].Reset();}
 liquid_wave_cs_.Reset();liquid_wave_constants_.Reset();liquid_wave_origin_valid_=liquid_wave_failed_=false;
 liquid_wave_clock_=-1;liquid_wave_accumulator_=0;
 bloom_.reset();sun_rays_.reset();depth_of_field_.reset();
 glow_.Reset();glow_rtv_.Reset();glow_srv_.Reset();glow_width_=glow_height_=0;glow_frame_=UINT64_MAX;
 native_terrain_frame_constants_.Reset();native_terrain_frame_srv_.Reset();
 terrain_noise_srv_.Reset();terrain_noise_sampler_.Reset();
 // Terrain 2.0's session resources (the per-map ones go with the contact maps).
 terrain2_masks_srv_.Reset();terrain2_mask_sampler_.Reset();terrain2_mask_bits_=0;terrain2_masks_tried_=false;
 terrain2_style_buffer_.Reset();terrain2_style_srv_.Reset();terrain2_style_uploaded_.fill(0);
 for(auto& cs:terrain2_bake_cs_)cs.Reset();
 terrain2_bake_constants_.Reset();terrain2_bake_sampler_.Reset();
 release_terrain2_cache();terrain2_cache_vs_.Reset();terrain2_cache_ps_.Reset();terrain2_cache_sampler_.Reset();terrain2_cache_raster_.Reset();
 terrain2_cache_bake_constants_.Reset();terrain2_cache_params_.Reset();terrain2_cache_params_srv_.Reset();
 terrain2_cache_failed_=false;terrain2_cache_frame_=UINT64_MAX;
 for(auto& timing:terrain2_bake_timing_)timing=Terrain2BakeTiming{}; // (the measured cost per texel stays: the same GPU)
 release_terrain_face_cache();face_vs_.Reset();face_ps_.Reset();face_sampler_.Reset();face_raster_.Reset();face_bake_constants_.Reset();
 face_failed_=false;face_frame_=UINT64_MAX;face_map_=UINT64_MAX;face_atlas_job_={};
 face_fb_vs_.Reset();face_fb_ps_.Reset();face_fb_constants_.Reset();face_fb_depth_.Reset();face_fb_dsv_.Reset();face_fb_depth_write_.Reset();face_fb_depth_test_.Reset();face_programs12_.clear();
 face_check_vs_.Reset();face_check_ps_.Reset();face_check_view_.Reset();face_check_depth_.Reset();face_check_staging_view_.Reset();face_check_rtv_.Reset();
 face_check_dsv_.Reset();face_check_depth_state_.Reset();face_check_counters_.Reset();face_check_staging_.Reset();face_check_constants_.Reset();face_check_uav_.Reset();face_check_pending_=0;
 retire_native_shadows();
 native_shadow_prepare_cs_.Reset();native_shadow_commit_cs_.Reset();native_shadow_build_cs_.Reset();native_shadow_args_guard_cs_.Reset();shadow_args_report_.Reset();shadow_args_report_uav_.Reset();shadow_args_report_copy_.Reset();shadow_args_copy_pending_=false;shadow_args_ticket_=0;shadow_args_next_read_=0;native_shadow_vs_.Reset();native_shadow_ps_.Reset();
 finish_placed_hitch_profile(PlacedHitchFinishReason::renderer_release);
 bridge_set_native_terrain_animation_owned(false);
 flush_native();
 // The lossless mask targets belong to this device, including saved views.
 hit_test_active_=hit_test_failed_=false;
 hit_test_color_.Reset();hit_test_rtv_.Reset();hit_test_depth_.Reset();hit_test_dsv_.Reset();
 hit_test_saved_rtv_.Reset();hit_test_saved_dsv_.Reset();hit_test_width_=hit_test_height_=0;
 system_telemetry_.stop();
 system_telemetry_active_=false;
 system_telemetry_snapshot_={};
 frame_statistics_.reset();
 // The only clean-exit path (~DD). Core mode: the host writes the ini; it
 // clears the marker when it sees this flag after the game process exits.
#ifndef EID3D11_EMBEDDED
 if(core_mode_)x64remote::signal_clean_exit();
 else
#endif
 debug_overlay_.clear_over_budget_marker();
 end_crash_session("clean exit");
 if(g_crash_quit_hook){UnhookWindowsHookEx(g_crash_quit_hook);g_crash_quit_hook=nullptr;}
 close_shader_marker();
 debug_overlay_.shutdown();
 bridge_set_profile_enabled(false);
 profiler_.shutdown();
#if defined(EID3D11_EMBEDDED) && !defined(EID3D11_SHADER_EMBED)
 // Both clients have finished using the executor above. Their borrowed
 // interfaces must be detached even if the following ensure selects D3D11.
 profiler_.set_timestamps(nullptr);
 debug_overlay_.set_renderer(nullptr);
#endif
 if(context_)context_->ClearState();

 game_particle_vs_.Reset();game_particle_ps_.Reset();game_particle_debug_ps_.Reset();game_particle_constants_.Reset();game_particle_buffer_.Reset();game_particle_srv_.Reset();
 terrain_print_vs_.Reset();terrain_print_hs_.Reset();terrain_print_ds_.Reset();
 game_ribbon_vs_.Reset();game_selection_vs_.Reset();game_mesh_vs_.Reset();game_glyph_vs_.Reset();std::vector<GameParticleGpu>().swap(game_effect_elements_);
 game_particle_capacity_=0;game_particle_pipeline_failed_=false;std::vector<std::uint32_t>().swap(game_particle_order_);
 reset_pipeline_warmup();warmup_query_.Reset();warmup_targets_.clear();warmup_depths_.clear();warmup_state_.Reset();
 pending_blits_.clear();
 pending_cursor_={};cursor_pending_=false;
 reset_camp_popup();
 camp_art_srv_.Reset();camp_art_texture_.Reset();camp_art_attempted_=false;
 camp_control_srv_.Reset();camp_control_texture_.Reset();
 camp_construction_srv_.Reset();camp_construction_texture_.Reset();
 for(auto& art:camp_art_d3d12_)art.reset();
 ui_blit_pixel_spares_.clear();
 native_pending_upload_.clear();
 native_terrain_phase_allocations_.clear();
 native_pending_draws_.clear();
 native_draw_order_.clear();
 native_world_lighting_={};
 native_world_lighting_valid_=false;
 native_world_lighting_dirty_=false;
 native_point_lights_.clear();native_point_light_inputs_.clear();native_point_lights_dirty_=false;native_point_light_capacity_=0;
 native_point_light_grid_={};native_point_light_grid_inputs_.clear();native_point_light_grid_capacity_=0;
 release_cave_irradiance();native_world_cave_=false;
 for(auto& layout:native_submission_layouts_)layout={};
 native_static_shadow_bake_={};native_terrain_shadow_bake_={};native_neighbour_shadow_bake_={};native_terrain_shadow_records_.clear();
 native_shadow_work_mask_srv_.Reset();native_shadow_work_mask_.Reset();native_shadow_work_mask_capacity_=0;native_shadow_work_mask12_.reset();
 native_layout_changed();
 native_frame_stats_={};
 overlay_textures_.clear();
 native_texture_arrays_.clear();
 {std::lock_guard<std::mutex> guard(native_owned_textures_lock_);native_owned_textures_.clear();}neighbour_textures_clear();
 invalidate_native_prepared_handles();
 native_meshes_.clear();
 native_prepared_mesh_slots_.clear();
 native_prepared_material_slots_.clear();
 native_prepared_mesh_reuse_cursor_=native_prepared_material_reuse_cursor_=0;
 native_prepared_mesh_reuse_sweep_frame_=native_prepared_material_reuse_sweep_frame_=0;
 native_mesh_resident_bytes_=0;
 native_world_reclaim_pending_=false;
 camp_unclipped_raster_=nullptr;camp_raster_failure_reported_=false;
 raster_states_.clear();
 depth_states_.clear();
 blend_states_.clear();
 sampler_states_.clear();

 dynamic_vertices_.Reset();
 dynamic_indices_.Reset();
 native_dynamic_.Reset();
 native_dynamic_srv_.Reset();
 native_world_lighting_constants_.Reset();
 native_point_light_constants_.Reset();native_point_light_buffer_.Reset();native_point_light_srv_.Reset();
 native_point_light_grid_buffer_.Reset();native_point_light_grid_srv_.Reset();
 native_draws_.Reset();
 native_draw_srv_.Reset();
 native_draw_work_.Reset();
 native_draw_work_srv_.Reset();
 native_visible_triangles_.Reset();
 native_visible_triangle_srv_.Reset();
 native_visible_triangle_uav_.Reset();
 native_prepared_vertices_.Reset();
 native_prepared_vertex_srv_.Reset();
 native_prepared_vertex_uav_.Reset();
 native_indirect_args_.Reset();
 native_indirect_args_uav_.Reset();
 native_geometry_pages_.clear();
 dynamic_vertex_capacity_=0;
 dynamic_index_capacity_=0;
 dynamic_vertex_offset_=0;
 dynamic_index_offset_=0;
 native_dynamic_capacity_=0;
 native_draw_work_capacity_=0;
 native_visible_triangle_capacity_=0;
 native_prepared_vertex_capacity_=0;native_world_vertices_.Reset();native_world_vertex_srv_.Reset();native_world_vertex_uav_.Reset();native_world_vertex_capacity_=native_world_vertex_used_=0;
 native_indirect_args_capacity_=0;
 native_geometry_live_bytes_=0;
 discard_vertices_=true;
 discard_indices_=true;constant_ring_.release();draw_constants_bytes_=0;draw_constants_range_={};
 native_build_cs_.Reset();native_pose_cs_.Reset();retire_native_poses();
  native_gltf_ps_.Reset();native_gltf_low_ps_.Reset();native_gltf_masked_ps_.Reset();native_gltf_masked_low_ps_.Reset();
 native_ps_.Reset();native_terrain_ps_.Reset();
 native_snow_ps_.Reset();native_low_ps_.Reset();native_low_unlit_ps_.Reset();native_astral_vs_.Reset();native_astral_ps_.Reset();native_astral_low_ps_.Reset();native_terrain_flat_ps_.Reset();native_snow_flat_ps_.Reset();native_masked_ps_.Reset();native_masked_low_ps_.Reset();native_masked_terrain_ps_.Reset();native_masked_snow_ps_.Reset();native_masked_terrain_flat_ps_.Reset();native_masked_snow_flat_ps_.Reset();native_masked_terrain_slim_ps_.Reset();native_masked_snow_slim_ps_.Reset();native_masked_terrain_slim_flat_ps_.Reset();native_masked_snow_slim_flat_ps_.Reset();native_terrain_miss_compact_cs_.Reset();terrain_miss_list_constants_.Reset();native_prepass_ps_.Reset();native_low_prepass_ps_.Reset();native_shadow_mask_vs_.Reset();native_shadow_mask_ps_.Reset();native_low_shadow_mask_ps_.Reset();native_liquid_ps_.Reset();native_low_liquid_ps_.Reset();native_liquid_ssr_ps_.Reset();native_liquid_foam_ps_.Reset();native_water_ps_.Reset();native_water_ssr_ps_.Reset();native_low_water_ps_.Reset();native_lava_ps_.Reset();native_liquid_swim_ps_.Reset();native_water_swim_ps_.Reset();
 
 native_vs_.Reset();
 for(auto& value:shadow_history_srv_)value.Reset();
 for(auto& value:shadow_history_rtv_)value.Reset();
 for(auto& value:shadow_history_mask_)value.Reset();
 shadow_history_depth_srv_.Reset();shadow_history_depth_.Reset();
 shadow_history_constants_.Reset();shadow_history_sampler_.Reset();
 shadow_history_valid_=shadow_history_written_=shadow_history_committed_=false;
 shadow_history_previous_frame_=shadow_history_current_frame_=UINT64_MAX;

 clear_depth_states_={};
 clear_blend_state_.Reset();
 clear_raster_state_.Reset();
 clear_constants_.Reset();
 clear_ps_.Reset();
 clear_vs_.Reset();shadow_scroll_vs_.Reset();shadow_scroll_ps_.Reset();shadow_scroll_constants_.Reset();shadow_scroll_raster_.Reset();
 pending_screen_rectangles_.clear();
 screen_rectangle_depth_state_.Reset();
 screen_rectangle_blend_state_.Reset();
 screen_rectangle_raster_state_.Reset();
 screen_rectangle_vertices_.Reset();
 screen_rectangle_layout_.Reset();
 screen_rectangle_ps_.Reset();
 screen_rectangle_vs_.Reset();
 native_work_range_constants_.Reset();
 debug_texture_sampler_.Reset();
 debug_texture_constants_.Reset();
 debug_texture_ps_.Reset();
 debug_texture_vs_.Reset();
 debug_projection_coefficients_={};
 debug_projection_valid_=false;
 debug_resource_view_work_={};
 constants_.Reset();
 layout_.Reset();
 ps_.Reset();
 backdrop_ps_.Reset();interface_scaled_ps_.Reset();interface_scaled_ps_failed_=false;interface_text_ps_.Reset();interface_text_ps_failed_=false;gpu_blit_key_.reset();
 vs_.Reset();
 release_ambient_occlusion_targets();
 ao_pipeline_failed_=false;ao_vs_.Reset();ao_horizon_ps_.Reset();ao_denoise_ps_.Reset();ao_resolve_ps_.Reset();
 ao_constants_.Reset();ao_resolve_blend_.Reset();
 release_world_fog_volume();fog_inject_cs_.Reset();fog_integrate_cs_.Reset();fog_frame_constants_.Reset();fog_sampler_.Reset();
 fog_mist_srv_.Reset();fog_mist_generation_=0;fog_prepared_frame_=UINT64_MAX;fog_volume_failed_=false;fog_volume_active_=false;
 fog_volume_reason_="";fog_status_state_.clear();
 release_owned_sky();
 ambient_geometry_.reset();
 reset_water_reflections();
 debug_stencil_srv_.Reset();
 depth_srv_.Reset();
 dsv_.Reset();world_dsv_.Reset();world_depth_read_only_dsv_.Reset();prepass_rest_.Reset();prepass_normal_.Reset();shadow_mask_.Reset();prepass_rest_rtv_.Reset();prepass_normal_rtv_.Reset();shadow_mask_rtv_.Reset();prepass_rest_srv_.Reset();prepass_normal_srv_.Reset();shadow_mask_srv_.Reset();terrain_miss_.Reset();terrain_miss_uav_.Reset();terrain_miss_srv_.Reset();terrain_miss_tris_.Reset();terrain_miss_tris_uav_.Reset();terrain_miss_tris_srv_.Reset();terrain_miss_tris_words_=0;terrain_miss_origin_.Reset();terrain_miss_origin_uav_.Reset();terrain_miss_origin_srv_.Reset();terrain_miss_origin_words_=0;for(auto& state:prepass_depth_states_)state.Reset();masked_depth_state_.Reset();shadow_mask_depth_state_.Reset();shadow_mask_raster_.Reset();interface_dsv_.Reset();interface_depth_.Reset();
 depth_.Reset();
 rtv_.Reset();backbuffer_rtv_.Reset();scene_composite_.reset();scene_composited_=false;
 dlss_previous_world_.Reset();dlss_previous_world_srv_.Reset();dlss_previous_world_uav_.Reset();dlss_motion_constants_.Reset();dlss_motion_depth_.Reset();
 release_fur();
 native_motion_vs_.Reset();native_motion_ps_.Reset();dlss_mode_=-1;fsr_.reset();upscale_sharpen_.reset();fsr_mode_=-1;
 gamma_lut_.Reset();gamma_lut_srv_.Reset();gamma_copy_.Reset();gamma_copy_srv_.Reset();
 gamma_vs_.Reset();gamma_ps_.Reset();gamma_sampler_.Reset();gamma_failed_=false;scene_updated_=false;deferred_ground_marks_.clear();deferred_combat_text_.clear();
 adapter3_.Reset();
 frame_acct::uninstall_device_counters();
 context1_.Reset();context_.Reset();
 // The prepared shader objects hold the device: drop them before it.
 if(shader_objects_)shader_objects_->detach(device_.Get());
 shader_objects_.Reset();shader_groups_prepared_=0;
 device_.Reset();
 swap_.Reset();
 feature_level_=D3D_FEATURE_LEVEL_9_3;
 last_present_tick_=0;
 present_interval_ms_=0.0;
 present_smoothed_ms_=0.0;
#ifdef EID3D11_EMBEDDED
 embedded_figure_layers_.clear(); embedded_figure_vs_.Reset(); embedded_figure_ps_.Reset(); embedded_figure_sampler_.Reset();
#if !defined(EID3D11_SHADER_EMBED)
 // release() also serves repeated ensure(): histories and cached FG
 // capability/failure values belong to one device. Retire frame images
 // after NGX stops, while the executor still owns its descriptor heaps.
 if(d3d12_){
  // The final native flush and pose/instance retirement above have finished.
  // Drop persistent and frame buffers, including ready/failed and upload
  // revisions: no native resource can be adopted by a replacement executor.
  // Map producers and their device caches must retire after the last draw.
  // Reuse the world release to invalidate the same-map fast path as well.
  release_ground_cover();
  release_ambient_world();
  // CritterState owns immutable GPU meshes/skins as well as frame buffers.
  // World release deliberately keeps them and its ready gate. Dropping the
  // state also joins its value-only asset/shader futures, which capture
  // neither Renderer nor the executor.
  critters_.reset();critters_failed_=false;
  critters_drawn_frame_=critters_status_frame_=UINT64_MAX;
  critters_summary_={};
  std::snprintf(critters_status_,sizeof(critters_status_),"Critters: pass not reached yet");
  ambient12_={};
  gc_t2_cell_class12_.reset();gc_t2_classes12_.reset();gc_t2_color12_.reset();
  gc_t2_programs12_.clear();
  gc_t2_cell_class_srv_.Reset();gc_t2_tile_classes_.clear();gc_t2_rows_generation_=0;
  terrain2_table12_={};terrain2_table_={};terrain2_table_count_=0;
  terrain2_records_first_=0;terrain2_table_full_logged_=false;
  terrain2_cache_params12_.reset();
  terrain2_cache_programs12_.clear();terrain2_bake12_programs_.clear();
  snow_surface12_.reset();snow_surface_key_=0;snow_surface12_program_.clear();
  // The liquid workers have joined above. Their upload/null checks must
  // see an empty owner even if the next device draws the same source map.
  liquid12_={};
  // The common D3D11 release functions reset shared validity/capacity gates,
  // but these parallel executor allocations must retire here as well.
  water12_={};fog12_={};cave12_={};
  liquid_swamp_frame_generation_=UINT64_MAX;liquid_surf_built_serial_=0;
  liquid_wave_current_=0;
  // A saved loading picture cannot be copied by a replacement executor.
  load_picture12_.reset();
  load_picture_this_frame_=load_picture_fitted_=false;
  load_reveal_tracking_=load_reveal_holding_=false;
  native12_={};
  terrain_textures_d3d12_.clear();terrain_pages_d3d12_={};
  native_terrain_frame_d3d12_.clear();
  scene_mip_textures_.clear();
  // Keep source bytes for exact-generation snapshot recovery. GPU-only
  // captures retain their marker so upload rejects them until recaptured.
  for(auto& item:core_surfaces_){
   if(!item.second)continue;
   item.second->d3d12.reset();
   item.second->uploaded_shape={};
   ++item.second->texture_serial;
  }
  // Atlas slices and the fixed bake targets all belong to this executor.
  impostors_={};
  native_impostor_pixels_uploaded_=-1.f;
  native_impostor_debug_uploaded_=false;
  // Preserve source texture identities, but rebuild their device-specific
  // profiles even when the composition serial (or fixed unit table) is equal.
  for(auto& item:material_profiles_){
   auto& profile=item.second;
   profile.profile12.reset();profile.class12.reset();
   profile.srv.Reset();profile.relief_srv.Reset();profile.class_srv.Reset();
   profile.serial=0;profile.strands=profile.failed=false;
  }
  ++material_profiles_revision_;material_profiles_indexed_=UINT64_MAX;
  material_profiles_dirty_=true;
  post12_={};upscale12_={};ao12_={};shadow12_={};
  world_d3d12_.reset();d3d12_white_.reset();native_hiz12_.reset();
  prepass_rest12_.reset();prepass_normal12_.reset();shadow_mask12_.reset();
  prepass_failed_width12_=prepass_failed_height12_=0;
  shadow_history_mask12_={};shadow_history_depth12_.reset();shadow_history_constants12_={};
  world_d3d12_bound_=world_history_bound12_=false;
  hit_test12_.reset();hit_test_saved_world12_.reset();particle_depth12_={};
  gamma_lut_d3d12_.reset();gamma_frame_d3d12_.reset();
  hudless_d3d12_.reset();hudless_gamma_d3d12_.reset();
  fg_caps_checked_=fg_previous_generated_=fg_frame_image_=false;
  fg_dlssg_unavailable_[0]=0;for(auto& reason:fg_failed_)reason[0]=0;
  fg_reason_mode_=-1;frame_generation_mode_=0;fsr_prepared_=false;
  world_output_this_present_=hudless_this_present_=fg_upscaler_valid_=false;
  fg_upscaler_reset_=true;fg_skip_logged_="";
  fg_not_game_frame_=0;fg_not_game_frame_logged_=~0u;
  fg_stats_real_=fg_stats_generated_=0;fg_stats_tick_=0;
  fg_presented_fps_=fg_real_fps_=0.;
  fg_low_rate_=fg_rate_previous_game_=false;fg_low_rate_windows_=0;
  fg_rate_window_ms_=fg_rate_on_ms_=0.;fg_rate_cost_ms_=-1.;fg_rate_window_count_=0;
  dlss_reset_=true;dlss_previous_valid_=false;embedded_d3d12_presented_=false;
 }
 // Every renderer worker/client and GPU owner above has retired. External
 // snapshots/preparers retain their session identity for late release;
 // the executor invalidates that owner before its heaps/device disappear.
 // Detach dispatch before destruction so repeated release/ensure cannot
 // submit work to a destroyed executor.
 d3d12_=nullptr;
 embedded_d3d12_.shutdown();
#endif
#endif
}

#endif
} // namespace owned
