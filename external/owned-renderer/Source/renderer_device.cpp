// Renderer device: swap chain, device creation and settings (ensure), targets, shader preparation and specialisation, pipelines, the native geometry arena, and teardown (release).
#include <intrin.h>
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
#include "sky_clouds.h"
#include "cave_lighting.h"
#include "terrain_material.h"
#include "native_shader.h"
#include "owned_terrain_decode.h"
#include "renderer.h"
#include "graphics_settings.h"
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
#include "x64_remote_device.h"
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
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <unordered_set>
#include "renderer_internal.h"
#include "scene_commands_ui.h"

namespace owned {
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

constexpr char kDebugResourceViewShader[] = R"(
cbuffer DebugViewConstants:register(b0){float4 projectionCoefficients;uint debugMode;uint shadowEnabled;uint shadowValid;uint debugPadding;float4 shadowBoard;float4 receiverRectangle[5];};
Texture2D<float> debugFloat:register(t0);Texture2D<uint> debugUint:register(t1);
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
 if(debugMode==3u){uint w,h;debugUint.GetDimensions(w,h);value=debugUint.Load(int3(int2(input.uv*float2(w,h)),0))/255.0;}
 else value=debugFloat.SampleLevel(pointClamp,input.uv,0);
 if(debugMode==2u){
  float a=projectionCoefficients.x,b=projectionCoefficients.y;
  float c=projectionCoefficients.z,d=projectionCoefficients.w;
  float viewZ=(b-value*d)/(value*c-a);
  float nearZ=b/-a;float farZ=(b-d)/(c-a);
  value=saturate((viewZ-nearZ)/(farZ-nearZ));
 }
 return float4(value,value,value,1);})";

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
HRESULT Renderer::create_swap_chain(){
 set_test_initialization_stage("swap-chain-entry");
 swap_.Reset();swap_flags_=0;tearing_supported_=false;
 ComPtr<IDXGIDevice> dxgi_device;ComPtr<IDXGIAdapter> adapter;ComPtr<IDXGIFactory2> factory;
 HRESULT hr=device_.As(&dxgi_device);
 if(SUCCEEDED(hr))hr=dxgi_device->GetAdapter(&adapter);
 if(SUCCEEDED(hr))hr=adapter->GetParent(IID_PPV_ARGS(&factory));
 if(FAILED(hr))return hr;
 ComPtr<IDXGIFactory5> factory5;BOOL tearing=FALSE;
 if(SUCCEEDED(factory.As(&factory5))&&
    SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,&tearing,sizeof(tearing))))
  tearing_supported_=tearing!=FALSE;
 DXGI_SWAP_CHAIN_DESC1 desc{};desc.Width=output_width_;desc.Height=output_height_;desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
 desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=3;
 desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;desc.Scaling=DXGI_SCALING_STRETCH;desc.AlphaMode=DXGI_ALPHA_MODE_IGNORE;
 desc.Flags=tearing_supported_?DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING:0u;
 ComPtr<IDXGISwapChain1> chain;
 set_test_initialization_stage("create-swap-chain-for-hwnd");
 hr=factory->CreateSwapChainForHwnd(device_.Get(),hwnd_,&desc,nullptr,nullptr,&chain);
 if(FAILED(hr)){
  OutputDebugStringA("EID3D11: flip-model swap chain unavailable; using the blit presentation model.\n");
  tearing_supported_=false;desc.Flags=0;desc.BufferCount=2;desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
   set_test_initialization_stage("create-fallback-swap-chain-for-hwnd");
   if(FAILED(hr=factory->CreateSwapChainForHwnd(device_.Get(),hwnd_,&desc,nullptr,nullptr,&chain)))return hr;
 }
  set_test_initialization_stage("swap-chain-created");
  swap_flags_=desc.Flags;
 // The game owns its window mode; DXGI must not switch to exclusive fullscreen.
  factory->MakeWindowAssociation(hwnd_,DXGI_MWA_NO_ALT_ENTER);
  set_test_initialization_stage("swap-chain-complete");
 return chain.As(&swap_);
}
namespace {

// The NVIDIA driver's per-game disk shader cache (landmine L-087):
// %LOCALAPPDATA%\NVIDIA\DXCache\<8-hex game id><hex>.nvph, mapped whole into
// the 32-bit process when the device is created.
std::wstring nvidia_cache_directory(){
 wchar_t local[MAX_PATH]{};
 const DWORD length=GetEnvironmentVariableW(L"LOCALAPPDATA",local,MAX_PATH);
 if(!length||length>=MAX_PATH)return {};
 return std::wstring(local)+L"\\NVIDIA\\DXCache\\";
}
bool starts_with_cache_id(const wchar_t* text) noexcept {
 for(int n=0;n<8;++n)if(!iswxdigit(text[n]))return false;
 return true;
}

// Before device creation: delete every cache file of this game recorded by an
// earlier run ([setup] nvidia_cache_id). Other games' files are never touched;
// locked files are skipped.
void delete_recorded_nvidia_cache() noexcept try{
 wchar_t id[17]{};
 GetPrivateProfileStringW(L"setup",L"nvidia_cache_id",L"",id,17,renderer_config_path().c_str());
 if(std::wcslen(id)!=8||!starts_with_cache_id(id))return;
 ComPtr<IDXGIFactory1> factory;ComPtr<IDXGIAdapter1> adapter;DXGI_ADAPTER_DESC1 desc{};
 if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))||FAILED(factory->EnumAdapters1(0,&adapter))||
    FAILED(adapter->GetDesc1(&desc))||desc.VendorId!=0x10de)return;
 const std::wstring directory=nvidia_cache_directory();
 if(directory.empty())return;
 WIN32_FIND_DATAW found{};
 const HANDLE search=FindFirstFileW((directory+id+L"*").c_str(),&found);
 if(search==INVALID_HANDLE_VALUE)return;
 unsigned count=0;std::uint64_t bytes=0;
 do{
  if((found.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)||_wcsnicmp(found.cFileName,id,8)!=0)continue;
  if(DeleteFileW((directory+found.cFileName).c_str())){
   ++count;bytes+=(static_cast<std::uint64_t>(found.nFileSizeHigh)<<32)|found.nFileSizeLow;
  }
 }while(FindNextFileW(search,&found));
 FindClose(search);
 if(!count)return;
 char detail[96]{};
 std::snprintf(detail,sizeof(detail),"id=%ls files=%u mb=%.1f",id,count,static_cast<double>(bytes)/1048576.0);
 bridge_report_event("nvidia_shader_cache_deleted",detail);
}catch(...){}

// After device creation: the game id of the largest .nvph file the driver
// mapped into this process from the DXCache directory, or empty.
std::string mapped_nvidia_cache_id() noexcept try{
 const std::wstring directory=nvidia_cache_directory();
 if(directory.size()<3||directory[1]!=L':')return {};
 const wchar_t drive[3]={directory[0],L':',0};wchar_t volume[MAX_PATH]{};
 if(!QueryDosDeviceW(drive,volume,MAX_PATH))return {};
 const std::wstring device_directory=std::wstring(volume)+directory.substr(2);
 std::string id;std::uint64_t largest=0;
 const void* base=nullptr;std::uint64_t bytes=0;bool mapped=false;
 const auto finish=[&]{
  if(!base||!mapped||bytes<=largest)return;
  wchar_t path[MAX_PATH]{};
  if(GetMappedFileNameW(GetCurrentProcess(),const_cast<void*>(base),path,MAX_PATH)<=device_directory.size()||
     _wcsnicmp(path,device_directory.c_str(),device_directory.size())!=0)return;
  const wchar_t* name=path+device_directory.size();const std::size_t length=std::wcslen(name);
  if(std::wcschr(name,L'\\')||length<=5||_wcsicmp(name+length-5,L".nvph")!=0||!starts_with_cache_id(name))return;
  largest=bytes;id.clear();
  for(int n=0;n<8;++n)id.push_back(static_cast<char>(name[n]));
 };
 SYSTEM_INFO info{};GetSystemInfo(&info);
 auto* address=static_cast<std::uint8_t*>(info.lpMinimumApplicationAddress);
 MEMORY_BASIC_INFORMATION region{};
 while(address<info.lpMaximumApplicationAddress&&VirtualQuery(address,&region,sizeof(region))==sizeof(region)){
  if(region.State!=MEM_FREE&&region.AllocationBase==base)bytes+=region.RegionSize;
  else{
   finish();
   base=region.State==MEM_FREE?nullptr:region.AllocationBase;bytes=region.RegionSize;mapped=region.Type==MEM_MAPPED;
  }
  address=static_cast<std::uint8_t*>(region.BaseAddress)+region.RegionSize;
 }
 finish();
 return id;
}catch(...){return {};}
}  // namespace

Renderer::NvidiaShaderCacheApi::~NvidiaShaderCacheApi() { reset(); }

void Renderer::NvidiaShaderCacheApi::reset() noexcept {
 if(unload)unload();
 if(module)FreeLibrary(module);
 unload=nullptr;module=nullptr;
}

HRESULT Renderer::disable_nvidia_shader_disk_cache() noexcept {
 ComPtr<IDXGIDevice> dxgi_device;ComPtr<IDXGIAdapter> adapter;DXGI_ADAPTER_DESC desc{};
 HRESULT hr=device_.As(&dxgi_device);
 if(SUCCEEDED(hr))hr=dxgi_device->GetAdapter(&adapter);
 if(SUCCEEDED(hr))hr=adapter->GetDesc(&desc);
 if(FAILED(hr)){
  bridge_report_event("nvidia_shader_disk_cache_rejected","adapter identification failed");
  return hr;
 }
 if(desc.VendorId!=0x10de)return S_OK;
 const auto fail=[](const char* stage,int status){
  char detail[160]{};
  std::snprintf(detail,sizeof(detail),"%s failed (status %d); device initialization rejected",stage,status);
  bridge_report_event("nvidia_shader_disk_cache_rejected",detail);
  return E_FAIL;
 };
#if defined(_WIN64)
 HMODULE module=LoadLibraryExW(L"nvapi64.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
#else
 HMODULE module=LoadLibraryExW(L"nvapi.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
#endif
 if(!module)return fail("load NVAPI",static_cast<int>(GetLastError()));
 using QueryInterfaceFn=void* (__cdecl*)(unsigned int);
 using InitializeFn=int (__cdecl*)();
 using DisableCacheFn=int (__cdecl*)(IUnknown*);
 const auto query=reinterpret_cast<QueryInterfaceFn>(GetProcAddress(module,"nvapi_QueryInterface"));
 // Public NVAPI interface IDs and cdecl ABI, independently used here:
 // https://github.com/NVIDIA/nvapi/blob/main/nvapi_interface.h
 const auto initialize=query?reinterpret_cast<InitializeFn>(query(0x0150e828)):nullptr;
 const auto unload=query?reinterpret_cast<InitializeFn>(query(0xd22bdd7e)):nullptr;
 const auto disable=query?reinterpret_cast<DisableCacheFn>(query(0xd0cbca7d)):nullptr;
 if(!initialize||!unload||!disable){
  FreeLibrary(module);
  return fail("resolve NVAPI shader disk cache interface",-1);
 }
 const int initialized=initialize();
 if(initialized!=0){FreeLibrary(module);return fail("initialize NVAPI",initialized);}
 nvidia_shader_cache_api_.module=module;
 nvidia_shader_cache_api_.unload=unload;
 const int disabled=disable(device_.Get());
 if(disabled!=0)return fail("disable device shader disk cache",disabled);
 bridge_report_event("nvidia_shader_disk_cache_disabled","owned D3D11 device; before shader creation");
 return S_OK;
}
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
 driver_concurrent_creates_=!have_threading||threading.DriverConcurrentCreates;
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
constexpr int kSettingsVersion=3;
constexpr struct{const char* key;int version;float old_default;} kChangedDefaults[]={
 {"depth_of_field",1,2.f}, // 2 -> 0, in no preset (candidate-67)
 {"render_thread",1,0.f},  // off -> on, off below 4 logical processors (candidate-71)
};

// Version 3 split the one Upscaling list (dlss_mode, with FSR's own
// fsr_sharpness) into method, quality and one sharpening for either
// upscaler. The old keys are no longer registered, so the loader skipped
// them: they are read once from the file here and the next save drops them.
std::string migrate_upscaling(RendererConfig& config) noexcept try{
 const std::wstring path=renderer_config_path();
 const int legacy=static_cast<int>(GetPrivateProfileIntW(L"graphics",L"dlss_mode",0,path.c_str()));
 int method=kUpscalerOff,quality=0;legacy_upscaling_choice(legacy,method,quality);
 config.set_integer("upscaler",method);
 if(method!=kUpscalerOff)config.set_integer("upscaler_quality",quality);
 wchar_t text[32]{};
 bool sharpness_read=false;float sharpness=0.f;
 if(GetPrivateProfileStringW(L"graphics",L"fsr_sharpness",L"",text,32,path.c_str())>0){
  wchar_t* end=nullptr;sharpness=std::wcstof(text,&end);
  sharpness_read=end!=text&&std::isfinite(sharpness);
  if(sharpness_read)config.set_floating_point("upscale_sharpness",std::clamp(sharpness,0.f,1.f));
 }
 char detail[96];
 std::snprintf(detail,sizeof(detail),"dlss_mode %d -> upscaler %d quality %d; fsr_sharpness %s",legacy,method,quality,sharpness_read?"kept":"absent");
 return detail;
}catch(...){return "failed";}

}  // namespace

// Ensure (the overlay's ini) and core mode's game process (core_ensure,
// which owns the ini there) run it once after loading.
void migrate_settings(RendererConfig& config) noexcept try{
 const int saved=config.settings_version();
 if(saved>=kSettingsVersion)return;
 const std::string upscaling=saved<3?migrate_upscaling(config):std::string("current");
 // A file saved on a named preset of the old table takes that preset's
 // current values (graphics_settings.cpp keeps the old rows for version 1).
 const GraphicsPreset preset=reapply_graphics_preset_before(config,saved);
 std::string reset;
 for(const auto& change:kChangedDefaults){
  if(change.version<=saved)continue;
  const RendererOption* option=config.find(change.key);
  if(!option)continue;
  const bool old_default=option->type==RendererOptionType::boolean?option->boolean_value==(change.old_default!=0.f):
   option->type==RendererOptionType::integer?option->integer_value==static_cast<int>(std::lround(change.old_default)):
   option->type==RendererOptionType::floating_point&&std::fabs(option->floating_value-change.old_default)<1e-4f;
  if(old_default&&config.reset_to_default(change.key)){reset+=reset.empty()?"":" ";reset+=change.key;}
 }
 config.set_settings_version(kSettingsVersion);
 if(config.save(renderer_config_path().c_str()))config.clear_dirty();
 char detail[384];
 std::snprintf(detail,sizeof(detail),"settings_version %d -> %d; old defaults reset: %s; preset %s%s; upscaling: %s",saved,kSettingsVersion,
  reset.empty()?"none":reset.c_str(),graphics_preset_name(preset),preset!=GraphicsPreset::custom?" re-applied from the old table":" (no old preset matched)",
  upscaling.c_str());
 bridge_report_event("settings_migrated",detail);
}catch(...){}

HRESULT Renderer::ensure(HWND hwnd,UINT w,UINT h){
 return ensure_with_driver(hwnd,w,h,D3D_DRIVER_TYPE_HARDWARE);
}
// Existing embedded device entry retained from the integration baseline.
HRESULT Renderer::ensure_for_test_warp(HWND hwnd,UINT w,UINT h){
 return ensure_with_driver(hwnd,w,h,D3D_DRIVER_TYPE_WARP);
}
HRESULT Renderer::ensure_with_driver(HWND hwnd,UINT w,UINT h,D3D_DRIVER_TYPE driver){SCENE_ENTRY("ensure");
#ifdef EID3D11_EMBEDDED
 if(!embedded_sky().available_to(this))return E_UNEXPECTED;
#endif
 output_width_=std::max(1u,w);output_height_=std::max(1u,h);camp_inventory_output(canvas_width(),canvas_height());set_world_size();hwnd_=hwnd;if(device_)return rtv_&&scene_composite_.target()?S_OK:E_FAIL;
 // Core mode (EI_RENDERER_X64_CORE=1): the host's Renderer draws; no device here.
 if(core_mode_)return x64remote::core_active()?S_OK:E_FAIL;
 if(x64remote::core_requested())return core_ensure();
 // NVIDIA's threaded optimisation retires shader variants on its worker while
 // their background compiles finish on its pool: an ABBA deadlock inside
 // nvwgf2um (dumps of 2026-10-11..13). Ask the driver to keep its internal
 // threads off this device.
 UINT flags=D3D11_CREATE_DEVICE_BGRA_SUPPORT|D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS;D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_1,D3D_FEATURE_LEVEL_10_0,D3D_FEATURE_LEVEL_9_3};HRESULT hr;
 // F10 x64_render_host (or EI_RENDERER_X64_REMOTE=1): proxy device, context
 // and swap chain; the x64 host owns the real device (x64_remote_device.h).
 // A host that does not start turns the option off and this session runs
 // on the local device below, with a notice (user decision 2026-10-24).
 bool remote=x64remote::remote_wanted();
 if(remote){
  hr=x64remote::create_remote_device(hwnd_,flags,&device_,&feature_level_,&context_);
  if(FAILED(hr)){remote=false;device_.Reset();context_.Reset();}
 }
 if(!remote){
#if !defined(EID3D11_EMBEDDED) || !defined(_WIN64)
  delete_recorded_nvidia_cache();
#endif
  hr=D3D11CreateDevice(nullptr,driver,nullptr,flags,levels,ARRAYSIZE(levels),D3D11_SDK_VERSION,&device_,&feature_level_,&context_);
 }
 if(FAILED(hr))return hr;
 debug_overlay_set_running_renderer(remote?"64-bit host (call stream)":"32-bit in the game process",false);
#if defined(EID3D11_EMBEDDED) && !defined(EID3D11_SHADER_EMBED)
 dlss_backend_.initialize(device_.Get(),[](const char* text){bridge_report_event("dlss_backend",text);});
 set_dlss_unavailable_reason(dlss_backend_.capability().available ? nullptr : dlss_backend_.capability().reason);
#else
 // DLSS runs only in the x64 host; the Upscaling option shows why not.
 if(!remote)set_dlss_unavailable_reason("needs the 64-bit render host");
 else{x64remote::DlssCaps caps{};
  if(!x64remote::dlss_caps(caps))set_dlss_unavailable_reason("the 64-bit host did not answer");
  else{caps.reason[sizeof(caps.reason)-1]=0;set_dlss_unavailable_reason(caps.available?nullptr:(caps.reason[0]?caps.reason:"not available"));}}
#endif
 // FSR 2 runs on this device in either mode; the Upscaling list shows why not.
 set_fsr_unavailable_reason(Fsr2Upscaler::unavailable_reason(device_.Get()));
 shader_objects_=ShaderObjects::attach(device_.Get());
 frame_acct::install_device_counters(device_.Get(),context_.Get());
 detect_device_options();
 report_adapter();
 // NVAPI takes the real device: the host owns it in remote mode.
 if(!remote&&FAILED(hr=disable_nvidia_shader_disk_cache()))return hr;
 retained_visual_scene().visual_set_surface_texture_retainer(&Renderer::retain_detached_surface_texture,this);
 set_test_initialization_stage("create-swap-chain");
 if(hwnd_&&FAILED(hr=create_swap_chain()))return hr;
 ComPtr<IDXGIDevice> dxgi_device;if(SUCCEEDED(device_.As(&dxgi_device))){ComPtr<IDXGIAdapter> adapter;if(SUCCEEDED(dxgi_device->GetAdapter(&adapter)))adapter.As(&adapter3_);}
 set_test_initialization_stage("register-renderer-settings");
 register_config_options();
 auto& config=debug_overlay_.config();
 if(hwnd_){debug_overlay_.initialize(hwnd_,device_.Get(),context_.Get());if(debug_overlay_.initialized()){migrate_settings(config);debug_overlay_.sync_interface_profile();recover_shader_crash();}}
 // Record which cache files the driver mapped, for deletion at the next start.
 if(debug_overlay_.initialized()){
  const std::string cache_id=mapped_nvidia_cache_id();
  if(!cache_id.empty()&&cache_id!=config.nvidia_cache_id()){
   config.set_nvidia_cache_id(cache_id);config.save(renderer_config_path().c_str());
  }
 }
 apply_config_settings();
 render_scale_percent_=std::clamp(configured_integer(config,"render_scale",100),kRenderScaleMinimum,100);
 dlss_mode_=configured_dlss_mode();fsr_mode_=configured_fsr_mode();set_world_size();
  set_test_initialization_stage("renderer-settings-applied");
  // The build's shader bytecode (EID3D11_SHADERS resource, Tools/shader_embed.cpp)
 // serves every program before the first pipeline (SMAA in create_targets).
  set_test_initialization_stage("register-shaders");
  register_embedded_shaders();
  set_test_initialization_stage("prepare-shaders");
 prepare_shaders();
 start_liquid_maps_job();
 if(!x64remote::remote_active())neighbour_maps_set_device(device_.Get()); // remote mode: the host runs the stream
 if(FAILED(hr=create_targets()))return hr;if(FAILED(hr=create_pipeline()))return hr;if(FAILED(hr=profiler_.initialize(device_.Get(),context_.Get())))return hr;
 frame_statistics_.reset();
 system_telemetry_snapshot_={};
 system_telemetry_active_=false;
 system_telemetry_.start(adapter3_.Get());
#ifdef EID3D11_EMBEDDED
  set_test_initialization_stage("attach-embedded-sky");
 if(!embedded_sky().attach(this))return E_UNEXPECTED;
#endif
  set_test_initialization_stage("complete");
 return S_OK;
}

// The player and developer options (eid3d11.ini [graphics] and friends):
// registered in every Renderer, so a core-mode game process (which owns the
// ini) and the x64 host (which applies the values it is sent) share one list.
void Renderer::register_config_options(){
 auto& config=debug_overlay_.config();
config.register_boolean("confine_cursor","Confine cursor to game client area","Input",true,false,"Keeps pointer-driven camera and interaction input inside the active game client area; releases immediately when the game loses focus.");config.register_boolean("vsync","Vertical sync","Presentation",present_interval()!=0,false,"Use a synchronized Present interval. Disabled remains the uncapped performance default. Forced off in developer mode.");config.register_integer("background_mode","In the background","Presentation",0,0,1,1,false,"What the game does while its window is not in front. 0: keeps running and drawing (capped by the background frame limit). 1: pauses; the world stops and the last frame stays on screen. Either way it never minimises itself or reloads its textures on return.");config.register_integer("background_frame_limit","Background frame limit (FPS)","Presentation",30,0,240,1,false,"While the game window is not in front, Present is paced to at most this many frames per second, so a game left in the background idles quietly. 0: off.");config.register_integer("frame_limit","Frame rate limit (FPS)","Presentation",0,0,500,1,false,"0: off (uncapped). Otherwise Present is paced to this many frames per second, measured from the previous present (high-resolution timer plus a short final spin). Forced off in developer mode.");config.register_boolean("shadows","Shadows","Graphics",true,false,"Render owned directional shadow maps and sample them in world shading. Changes apply at the next scene boundary.");config.register_boolean("terrain_normal_sunlight","Terrain normal sunlight","Graphics",true,false,"Compare terrain shading. On uses terrain normals for direct sun; off applies full direct sun while keeping projected shadows, ambient light, textures, and fog.");
config.register_boolean("smaa","Anti-aliasing: SMAA","Graphics",true,false,"SMAA 1x on the world scene, before interface rendering.");
config.register_boolean("depth_prepass","Depth prepass and shadow mask","Performance",true,false,"Opaque and alpha-tested world surfaces first draw depth only; one full-screen pass then takes the sun and cloud shadow per visible pixel, and the full material shading runs once per pixel. Off draws every surface with its own shadow lookups (more overdraw).");
config.register_integer("upscaler","Upscaling","Graphics",kUpscalerOff,kUpscalerOff,kUpscalerMethodMaximum,1,false,"0: off (the world renders at the render scale). 1: NVIDIA DLSS (64-bit render host and an RTX card). 2: AMD FSR 2 (any Direct3D 11.3 card, both modes). The world renders at a lower resolution set by the upscaling quality and is reconstructed at the screen size; it replaces SMAA and the render scale. Applies at the next frame.");
config.register_integer("upscaler_quality","Upscaling quality","Graphics",0,0,kUpscalerQualityMaximum,1,false,"0: Quality, 1: Balanced, 2: Performance, 3: Ultra Performance, 4: native resolution anti-aliasing (DLAA with DLSS, Native AA with FSR). Sets the internal resolution while upscaling is on.");
config.register_floating_point("upscale_sharpness","Sharpening","Graphics",.2f,0.f,1.f,.05f,false,"Strength of the RCAS sharpening after either upscaler (DLSS or FSR 2). 0 turns it off; keep it low to stay close to the game's look.");
config.register_integer("render_scale","Render scale (%)","Graphics",100,kRenderScaleMinimum,100,1,false,"The 3D world (and every world effect) renders at this percentage of the screen size and is upscaled before the interface, which always stays at full resolution. Applies at the next frame.");
config.register_integer("camp_ui_layout","Camp layout","Interface",1,0,1,1,false,"Switch the open camp without restarting: original UI comparison or inventory grid. Keeps the current mode and transaction; supports compact 16:10 and wider displays.");
config.register_integer("ui_language","Menu language","Language",0,0,2,1,false,"Language of the renderer's menus. 0: auto (Russian when the Windows display language is Russian or its ANSI code page is 1251), 1: English, 2: Russian. Applies at the next frame.");
#ifndef EID3D11_EMBEDDED
config.register_boolean("x64_render_host","64-bit render host","Display",false,true,"Moves the graphics card and driver into a separate 64-bit process, leaving the game more memory (fewer out-of-memory crashes on big maps). Applies after restarting the game. If the host does not start, the option turns itself off.");
#endif
config.register_integer("display_mode","Display mode","Display",0,0,1,1,false,"0: borderless fullscreen on the primary monitor, 1: a movable window of the chosen window size. Use Apply display to change this during gameplay.");
config.register_integer("window_width","Window width","Display",0,0,16384,1,false,"Client width of the windowed mode (0: the monitor's size). Use Apply display to change this during gameplay.");
config.register_integer("window_height","Window height","Display",0,0,16384,1,false,"Client height of the windowed mode (0: the monitor's size). Use Apply display to change this during gameplay.");
config.register_boolean("owned_ui_text","Owned interface text","Performance",false,false,"Interface text and line drawing is recorded instead of drawn, and a panel is redrawn (with the same GDI output) only when what it draws changes. Off draws every panel through GDI each time the game asks.");
config.register_integer("anisotropy","Anisotropic filtering","Graphics",3,0,4,1,false,"World texture filtering: 1, 2, 4, 8 or 16 samples. Interface filtering is preserved.");
config.register_boolean("water_reflections","Water reflections","Graphics",true,false,
 "Reflect the current opaque scene on non-emissive liquids; misses use the sky.");
config.register_integer("water_reflection_quality","Water reflection quality","Graphics",2,0,2,1,false,
 "Detail of the screen-space water reflection search. 0: low (12 steps, for weak GPUs; small reflected edges break up more), 1: medium (20 steps), 2: high (40 steps; finer reflected edges where the screen shows the reflected surface, a little more GPU time on water).");
config.register_floating_point("water_reflection_step_pixels","Water reflection step bound (px)","Performance",8.f,0.f,64.f,1.f,false,
 "0: every reflection ray takes the quality's full step count. Otherwise a ray whose whole search projects short on screen (distant water) takes only as many steps as keep each step within this many world-image pixels; rays that already need the full count (near water) are unchanged.");
config.register_boolean("water_simple","Stock-look water (prototype)","Graphics",false,false,
 "Prototype for weak GPUs, in no preset: liquid surfaces draw like the game's own water through the world's ground shader: the liquid tile's own texture with its tile animation, the game's vertex lighting and authored opacity, and the wave glint. No reflections, refraction, foam, shore or ripples.");
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
config.register_floating_point("water_surf_edge_fade","Sea surf edge fade (m)","Graphics",40.f,0.f,120.f,5.f,false,
 "Where the sea runs off the maps around the hero, the surf, swell and foam fade into plain water over this distance.");
config.register_integer("bloom","Bloom","Graphics",2,0,2,1,false,"Soft glow around the brightest world light: sun highlights, sky near the sun, lava, fire, lamps and magic. 0: off, 1: low (4 half-resolution blur levels), 2: high (6 levels, wider glow). The interface never glows.");
config.register_integer("emissive_lights","Emissive lighting","Graphics",1,0,2,1,false,"Lava and other self-lit surfaces light the ground, objects and characters around them with a soft glow: point lights just above the surface, no shadows, pulsing slowly with game time. 0: off, 1: on (one light per 8 m cell of lava), 2: finer (one per 4 m cell; about 3x the lights and a little more GPU time near lava).");
config.register_floating_point("emissive_light_strength","Emissive light strength","Graphics",1.f,0.f,3.f,.05f,false,"Brightness of the glow that emissive surfaces cast on their surroundings.");
config.register_floating_point("bloom_strength","Bloom strength","Graphics",1.f,0.f,3.f,.05f,false,"How much glow is added to the world image.");
config.register_floating_point("bloom_threshold","Bloom threshold","Graphics",1.f,1.f,3.f,.05f,false,"Where glow starts, relative to the brightest lit surface or fog this frame (1: just above it). Only light brighter than that glows: fire, lamps, lava, magic and sun glints; fogged objects and the sky never do.");
config.register_integer("depth_of_field","Depth of field","Graphics",0,0,2,1,false,"Softly blurs the far background when the camera is tilted toward the horizon; the area around the screen centre stays sharp. The steep gameplay view (50 degrees and steeper) never blurs. 0: off, 1: low (16-tap blur), 2: high (40-tap blur, smoother large blur). The interface never blurs.");
config.register_floating_point("depth_of_field_strength","Depth of field strength","Graphics",.2f,0.f,3.f,.05f,false,"How strongly the background blurs and how close behind the focus the blur begins. 1 is subtle; 3 is a strong cinematic blur.");
config.register_boolean("bloom_debug_view","Bloom debug view","Graphics",false,false,"Shows only the glow that bloom adds, on black; the interface still draws on top.");
config.register_integer("ground_cover","Ground cover","Effects",2,0,3,1,false,"Procedural grass and debris on the ground: lush grass and flowers on meadows, dry tufts, fallen leaves and twigs near trees, small stones, dead bushes, reeds around swamps, kelp and shells on lake and sea shores, seagrass under open water, snow piles in Ingos. Grass sways in the map wind and parts around units. Distant cover thins to a sparser far level instead of ending. 0: off, 1: low (shorter full-density range, half density), 2: normal, 3: high (full density 1.6x farther).");
config.register_integer("critters","Critters","Effects",2,0,2,1,false,"Small ambient creatures, miniatures of the game's own models and procedural ones: rats, spiders and crawler larvae in caves, frogs by swamps, lizards and snakes, scolopendras and dung beetles in Suslanger, shore crabs, bird flocks, ravens near the undead and outside the Dead City wall, vultures circling high over Suslanger, snow birds and snow mice in Ingos, carrion birds at corpses. They hide or flee from units they can hear or see (louder, more visible, running units from further away; crawling or standing still is quieter) and stay away from combat. 0: off, 1: low (rarer, smaller flocks), 2: high.");
for(const auto& type:kCritterTypeSettings){
 config.register_boolean(type.key,type.label,"Effects",true,false,(std::string(type.tooltip)+" Off: this type never appears; ones already out vanish.").c_str());
 config.register_integer(type.weight_key,(std::string(type.label)+" spawn weight (%)").c_str(),"Effects",kCritterWeightDefault,0,kCritterWeightMaximum,5,false,
  "Multiplies this type's spawn weight: how often it is picked against the other types that fit the place (100: the normal mix). The number of critters on screen stays the same.");
}
config.register_integer("ambient_particles","Ambient particles","Effects",1,0,2,1,false,"Area-tied atmospheric particles: pollen and grass bits over meadows, falling leaves near trees, grey motes in the Dead City, dust and drips in caves, embers and ash over lava, dust in the desert, fireflies on Gipat nights. 0: off, 1: low, 2: high.");
config.register_floating_point("ambient_particle_density","Ambient particle density","Effects",1.f,0.f,3.f,.05f,false,"Multiplier on the number of ambient particles at the chosen level.");
config.register_floating_point("ambient_particle_size","Ambient particle size","Effects",1.f,.25f,3.f,.05f,false,"Multiplier on the size of ambient particles.");
config.register_integer("shadow_cascade_quality","Cascade resolution","Shadows",1,0,2,1,false,"0: 1024, 1: 2048, 2: 4096 pixels per directional map. Higher quality uses more GPU memory.");
config.register_integer("shadow_close_cascades","Close cascades","Shadows",3,1,3,1,false,"Use one to three close directional maps; fewer maps reduce shadow draw work.");
config.register_boolean("shadow_far","Far shadows","Shadows",true,false,"Render the middle and broad directional maps beyond the close cascades.");
config.register_boolean("shadow_membership_scroll","Partial sun map refresh on membership change","Shadows",true,false,"A sun map whose projection did not move but whose terrain membership changed (far maps follow their receivers while panning) keeps its cached static layer and draws only the parts that entered. Off redraws the whole static layer, as before.");
config.register_integer("shadow_neighbour_casters","Neighbour-map sun casters","Shadows",2,0,2,1,false,"Decorations of the neighbouring maps cast sun shadows and shelter, with the same rules as the active map's statics. 0: off. 1: the close maps (C0-C2) and the shelter. 2: all five maps. Changing it redraws cached sun depth.");config.register_boolean("shadow_far_foliage_opaque","Opaque far foliage shadows","Shadows",true,false,"Draw tree and bush foliage as solid depth in the two far sun maps (cascades 3 and 4). Close maps and point-light shadows keep leaf cutouts. Off restores exact cutout shadows; changing this rebuilds cached sun depth.");
config.register_boolean("shadow_early_out","Shadow filter early-out","Shadows",true,false,"Skip the soft-shadow filter where the sun map proves a pixel fully lit or fully shaded (per 16-texel cell depth bounds). The result is the same; off runs the full filter everywhere, for comparison.");
config.register_boolean("hero_light","Hero light","Graphics",false,false,"Illuminate the area around player-controlled heroes with their attached light. Changes apply at the next world frame; other map lights are unchanged.");
config.register_boolean("point_shadows","Point-light shadows","Shadows",true,false,"Cast shadows from point lights while keeping their illumination.");
config.register_integer("point_shadow_quality","Point-shadow resolution","Shadows",2,0,2,1,false,"0: 128, 1: 256, 2: 512 pixels per cube face. Higher quality uses more GPU memory per shadowed lamp.");
config.register_integer("shadow_softness","Shadow softness","Shadows",1,0,2,1,false,"0: fixed soft edge (previous filter). 1: contact-hardening soft shadows, low sample count. 2: high sample count. Penumbrae widen with the distance between caster and receiver.");
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
config.register_boolean("highlight_briefing","Highlight briefing characters","Highlight",false,false,"On the base screen, characters with something to discuss (and the current speakers) glow with the game's white highlight. Off draws them with normal shading.");config.register_boolean("highlight_selected","Highlight selected characters","Highlight",false,false,"Selected party characters glow with the game's white highlight. Off keeps their normal shading and shadows.");config.register_boolean("highlight_hover_characters","Highlight characters under the cursor","Highlight",true,false,"A softer glow on the character the cursor would act on: briefing characters you can talk to on the base screen, enemies you can attack and spell or steal targets on maps. Follows the game's own cursor decision.");config.register_boolean("highlight_hover_objects","Highlight objects under the cursor","Highlight",true,false,"The same softer glow on what the cursor would use: chests, levers, traps and lootable corpses. Follows the game's own cursor decision.");config.register_integer("cursor_scale","Cursor scale (%)","Interface",100,100,400,25,false,"Size of the game cursor. 100 draws the original pixels exactly; larger sizes are smoothed at the edges.");config.register_integer("pillarbox_fill","Pillarbox fill","Interface",1,0,1,1,false,"Loading screens and movies made for another screen shape are shown at their own shape. 0 fills the bars black, 1 with a soft, darkened blur of the same picture.");config.register_integer("ui_scale","UI scale (%)","Interface",100,50,120,5,false,"Size of the in-mission HUD (map, portrait, weapons, spells, skills, stance wheel, clock, chat). Adjusts immediately from 50% to 120% around each screen corner, including clickable areas. Menus, inventory, shops and dialogs keep their size.");config.register_floating_point("message_panel_width","Message panel width (%)","HUD layout",100.f,50.f,800.f,.1f,false,"Logical width of the loot and quest message panel. Resizing reflows its text.");
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
    if(i!=4)config.register_integer((prefix+"_slots_per_column").c_str(),"Slots per column","HUD layout",0,0,32,1,false,"Wrap the bar into another column (or row, for left/right alignment) every this many slots. 0 keeps one line.");
}
config.register_integer("message_panel_text_scale","Info panel text scale (%)","Interface",100,50,300,5,false,"Size of loot and quest text, independent of the panel dimensions and multiplied by global HUD scale.");
config.register_integer("hud_border_scale","Panel border scale (%)","Interface",100,0,300,5,false,"Width of gameplay panel borders. Global UI scale also applies; individual widget size does not.");
config.register_integer("hud_border_transparency","Panel border transparency (%)","Interface",0,0,100,5,false,"Transparency of gameplay panel borders. Editor guides remain visible while unlocked.");
config.register_integer("combat_text_scale","Combat text scale (%)","Interface",100,50,300,5,false,"Independent size of scrolling damage numbers and floating combat indicators, including backstab, critical hits and experience. Applies immediately around each existing moving text anchor.");
config.register_floating_point("message_panel_height","Message panel height (%)","HUD layout",100.f,50.f,600.f,.1f,false,"Logical height of the loot and quest message panel.");
config.register_floating_point("message_panel_x","Message panel horizontal position (%)","HUD layout",50.f,0.f,100.f,.1f,false,"Position of the panel's centre across the screen. The panel stays inside the screen.");
config.register_floating_point("message_panel_y","Message panel vertical position (%)","HUD layout",0.f,0.f,100.f,.1f,false,"Position of the panel's top down the screen. Unlock the HUD layout in F10 to drag or resize it.");
config.register_integer("hud_width","HUD width","Interface",2,0,2,1,true,"How far apart the edge-anchored HUD (map, portrait, spells, weapons) may sit on ultrawide screens: 0 full width, 1 at most 21:9, 2 at most 16:9. Applies after restarting the game.");config.register_integer("point_shadow_fast_overlays","Point-shadow full-rate lamps","Shadows",4,0,32,1,false,"This many most important lamps (brightness times reach to the view focus) redraw moving-caster shadows every frame; the others every third frame. 0 redraws all every frame.");config.register_integer("point_shadow_merge","Point-shadow merge distance","Shadows",3,0,8,1,false,"Shadow-casting lights closer than this share one shadow cube (value x 0.5 world units; 0 disables). Illumination stays per light.");
config.register_boolean("weather_wind_variation","Varying wind","Graphics",true,false,"Wind that changes over game time: calm and windier spells, slight shifts of direction, and gustier wind in rain and snow. Moves tree sway, ambient particles and fog wisps. Off keeps the map's steady wind.");
config.register_floating_point("wind_floor","Wind floor","Graphics",kDefaultWindFloor,0.f,.5f,.01f,false,"Gentle minimum wind so grass, reeds and foliage always move a little, even on maps whose own wind is zero. Stronger map winds are unchanged. 0 uses the map's wind only.");
config.register_integer("wind_trails","Wind trails","Graphics",1,0,2,1,false,"Thin translucent streaks that trace the gusts near the camera when the wind blows hard: none on calm spells or in caves, more in storms. Dust-tinted in Suslanger, pale specks in the snow. 0: off, 1: default, 2: dense.");
config.register_integer("precipitation","Precipitation quality","Graphics",3,0,4,1,false,"Rain and snow drawn by the renderer in place of the game's own: rain streaks and snowflakes that fall around the view, stop at the ground, follow the wind and the game clock (frozen while paused). 0: off (no rain or snow is drawn), 1: low, 2: medium, 3: high, 4: ultra. Higher levels draw denser rain.");
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
config.register_floating_point("cave_brightness","Cave brightness","Graphics",kDefaultCaveBrightness,.25f,1.25f,.05f,false,"Light level in caves and dungeons: scales the cave ambient and the game's fixed cave light together (1 is the game's own cave table). Lava, torches, lamps and glowing objects add on top unchanged. Outdoor zones are not affected.");
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
config.register_boolean("load_reveal_hold","Hold the loading screen until the world is ready","Graphics",true,false,"After a map load, keep the loading picture on screen until the map, its neighbouring maps and its ground cover are prepared (at most 3 s), so nothing pops in after the loading screen. The game keeps running meanwhile. Off shows the world at once.");
config.register_floating_point("neighbour_fog_range","Neighbour fog range","Graphics",1.f,.5f,4.f,.25f,false,"Over edges with a neighbouring map, fog starts 20 m inside the border, is light at the border and full 80 m into the neighbour; this scales both distances. Higher shows more of the neighbours and costs more; anything in full fog is not drawn. Capped by draw_distance. Changing it reloads the neighbours.");
config.register_integer("ground_cover_distance","Ground cover distance (m)","Effects",200,50,1000,25,false,"How far grass and other ground cover is drawn, independent of draw_distance. The far sparse level ends here.");
config.register_integer("ground_cover_shadows","Ground cover shadows","Effects",1,0,2,1,false,"Sun shadows of the procedural ground cover in the close shadow layers. 0: none. 1: large cover only (snow and sand piles, fallen logs and branches, dead bushes, cave wall roots). 2: everything except seagrass, beach wrack and the distant tuft meshes; the swaying grass then redraws the close shadow layers every frame. Needs sun shadows and ground cover. Critters always cast sun shadows when sun shadows are on.");
config.register_integer("effect_distance","Effect distance (m)","Effects",300,100,1000,25,false,"How far game effects are kept and drawn: campfires, torches, map particle sources, lights and lightning. Capped by draw_distance. The game itself keeps them only within 100 m.");
config.register_boolean("impostors","Crown impostors","Graphics",true,false,"Cut-out parts of neighbouring-map objects (tree crowns, bushes) draw a baked camera-facing impostor where they are smaller on screen than the impostor size, crossfading with a screen-door dither. Off draws every triangle.");config.register_floating_point("impostor_pixels","Crown impostor size (px)","Graphics",64.f,16.f,256.f,8.f,false,"On-screen size (pixels across the crown's bounding sphere) below which a crown draws its impostor. The crossfade runs from this size to one and a half times it.");config.register_boolean("impostor_debug","Crown impostor diagnostic view","Graphics",false,false,"Draws every crown impostor quad as a solid magenta square (placement check).");config.register_boolean("lod","Static object LOD","Graphics",true,false,"Opaque parts of neighbouring-map objects draw a reduced triangle level where its largest deviation projects below the pixel limit, crossfading between levels with a screen-door dither. Off draws every triangle.");config.register_floating_point("lod_pixel_error","Static LOD pixel limit","Graphics",1.f,.25f,8.f,.25f,false,"Largest on-screen deviation (pixels at the current resolution) a reduced level may have where it is drawn. The crossfade to the next level runs from this limit to twice it.");config.register_integer("draw_distance","Draw distance (m)","Graphics",300,100,1000,25,false,"How far terrain, objects and neighbouring maps are drawn, in world metres (the unit of the game's FarClipDistance). The last tenth fades into the sky or fog colour; beyond it nothing is drawn, and nearer where fog already hides everything. Shadows inside the range keep their casters. Also caps how deep neighbouring maps load (neighbour_fog_range), so changing it can reload them.");
config.register_boolean("terrain_lod","Terrain LOD","Graphics",false,false,"Switch terrain detail by distance. Disable to keep the highest detail everywhere; this costs terrain geometry work.");
config.register_integer("terrain_quality","Terrain detail","Graphics",1,0,1,1,false,"0: low (the authored tile mapping only), 1: high (anti-repetition and transition sharpening, each with its own switch below). Low saves about 0.2 ms of terrain shading at 4K. With Cliff texturing on, faces on low read each tile with one plain tap (its 2 m repeat shows), high through the anti-tiling cells.");
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
config.register_integer("terrain_blend_debug","Terrain blend debug view","Diagnostics",0,0,6,1,false,"Temporary. Terrain material blend pixels drawn unlit as: 1 field (red top material weight, green contested, blue tile-art share), 2 plane weights (red YZ side, green XZ side, blue top), 3 masks (red exposed rock, green cover, blue fade), 4 winning material (hashed colour, white where exposed rock wins), 5 every terrain pixel's material class (green material base tile, magenta long tile, cyan animated, red other), 6 candidate overflow (red where a material beyond the blend's four candidate slots carried weight above the tail, brighter with more weight; dim green elsewhere).");
config.register_boolean("neighbour_fog_skip","Skip blend under neighbour fog","Graphics",true,false,"Temporary A/B switch: neighbour terrain under nearly full seam fog keeps its tile art (no blend or cliffs).");
config.register_boolean("terrain_blend_neighbours","Terrain blend on neighbour maps","Graphics",true,false,"Temporary cost switch: neighbour maps' terrain reads their own tile grid and atlas for the material blend and cliffs. Off binds none, so neighbour terrain keeps its tile art.");
config.register_boolean("terrain_far_detile","Terrain blend: far de-tiling","Graphics",true,false,"On: the full blend's steep faces keep their turned anti-repetition reads at every distance (mip-filtered, the larger-scale read added only past the atlas padding), and distant blended ground keeps its larger-scale read instead of the plain 2 m tile. Off: faces switch to one plain read once a texel is under a third of a pixel, and distant ground past the padding fades the plain read to the material's mean (the previous look).");
config.register_integer("terrain_blend","Terrain material blend","Graphics",1,0,2,1,false,"Tile transitions (a path meeting snow, grass meeting dirt) are redrawn from the plain textures of the materials at the map grid's corners, mixed by a smooth noisy field so edges meander naturally instead of repeating 2 m tile shapes; snow, sand and grass settle into the joints of stone. Corner materials come from the map editor's atlas profile shipped with the map, else its built-in table. Long and animated tiles and maps without tile families keep their art. 0: off, 1: fast (one texture read a material, broad edge noise), 2: full (adds a finer edge-noise octave, a finer boundary warp and turned anti-repetition reads on steep faces; fast reads faces with one plain tap, which costs about a third of the steep-face shading). Steep faces keep their painted cover (grass stays grass, snow stays snow) read through world-space side projections (a second turned read near the camera breaks the repeat), and nearby ground or stone shows through in irregular patches and down-slope streaks where the face is steepest; this takes over Terrain cliffs on those tiles.");
config.register_integer("terrain_tessellation","Terrain cliff tessellation","Graphics",1,0,1,1,false,"Round off the crests and feet of steep terrain with GPU tessellation (smooth Phong surface through the game's own heightfield and slope normals; no texture relief). Subdivision is fixed in world space, so it never changes as the camera moves, and flat or evenly sloped ground is never subdivided. Walkable ground moves at most 15 cm, steeper faces at most 70 cm; nothing moves at the map border. 0: off (standard terrain), 1: on (up to 8x). Shadow maps keep the untessellated terrain. Costs GPU time on every terrain triangle while on.");
config.register_integer("terrain_bake","Terrain material cache (test)","Diagnostics",0,0,3,1,false,"Terrain material cache. 1: keep the visible ground's finished terrain material in resident pages (baked nearest first within the per-frame budget) and draw the covered pixels through the lean cached variant, the rest through the live material; 0/1 at the same paused camera gives the comparison pair. 2: re-bake at the per-frame budget every frame without using it (the bake's GPU cost, profiler pass terrain_bake). 3: as 1 with every cached pixel tinted magenta (shows what the cache covers). Off frees the cache.");
config.register_integer("terrain_bake_density","Terrain material cache density cap (texels/m)","Diagnostics",96,16,128,16,false,"Largest cache density; each page otherwise takes the screen's pixels per metre at its nearest point, and steep pages more along their slope. 48 is the HD terrain's own density, 96 twice it.");
config.register_integer("terrain_bake_reach","Terrain material cache reach (m)","Diagnostics",1000,50,1000,50,false,"Visible terrain within this distance of the eye gets cache pages.");
config.register_integer("terrain_bake_budget","Terrain material cache budget (MB)","Diagnostics",128,32,1024,32,false,"Largest cache atlas (with mips); the nearest pages are kept when the visible terrain needs more, the rest keeps the live material.");
config.register_integer("terrain_bake_stretch","Terrain material cache slope stretch limit","Diagnostics",16,1,16,1,false,"Largest density gain along a slope (powers of two). Lower values soften steep faces in the cache and cut its size.");
config.register_integer("terrain_bake_bc1","Terrain material cache BC1 distance (m)","Diagnostics",60,0,1000,10,false,"Cache pages farther than this keep BC1 (an eighth of the memory); 0 keeps every page uncompressed (beyond ultra).");
config.register_integer("terrain_bake_near_format","Terrain material cache near format","Diagnostics",1,0,1,1,false,"Cache pages nearer than the BC1 distance: 1 BC7 (a quarter of the memory), 0 uncompressed (with BC1 distance 0: beyond ultra).");
config.register_integer("terrain_bake_frame_texels","Terrain material cache bake cap per frame (k texels)","Diagnostics",4096,256,4096,256,false,"Largest slot area the cache bakes in one frame (the staging atlas holds 4096 k).");
config.register_integer("terrain_bake_frame_ms","Terrain material cache bake time per frame (0.1 ms)","Diagnostics",3,1,50,1,false,"GPU time the cache's bake may take per frame while pages are missing, measured with timestamps; nearest pages first, nothing when all are resident.");
config.register_integer("terrain_bake_route","Terrain material cache routing","Diagnostics",2,1,2,1,false,"1: two passes (cached pixels through the lean variant, then the live variant for the rest; geometry and tessellation run twice). 2: one pass (the live variant uses the cached colour where it covers).");
config.register_boolean("terrain_bake_neighbours","Terrain material cache on neighbour maps","Diagnostics",false,false,"Cache and route neighbour-map terrain too (a net cost on the perf frame: distant ground is cheap live).");
config.register_boolean("terrain_height_blend","Terrain transition sharpening","Graphics",true,false,"Bias soft tile transitions by the texture's own detail so the brighter material breaks through. Off is cheapest.");
config.register_integer("sky_weather_test","Sky weather test","Diagnostics",0,0,2,1,false,"Render-only sky test: 1 builds a rain storm in the owned sky (deck, rain shafts, lightning), 2 clears it, 0 follows the game's weather. The game's rain, fog and gameplay are untouched.");
config.register_integer("critter_labels","Critter labels","Diagnostics",0,0,1,1,false,"Label every live ambient critter and bird with species, state and distance to the nearest party member, including hidden critters outside party sight. Render only.");
 config.register_integer("critter_showcase","Critter showcase","Diagnostics",0,0,3,1,false,"Debug: one of every critter species, a small flock of every bird and a vulture group (plus a vulture pair on its thermal) in labelled rows a few metres in front of the camera focus, at their normal in-game size (2: twice the size; 3: normal size drawn without the depth test, to tell buried from small). Render only: no simulation, spawn rules or budgets; the ambient critters pause while it is on.");
  set_test_initialization_stage("renderer-settings-registered");
 register_combat_ui_options(config);
}

// Startup values of the loaded options: renderer members and the game-side
// bridge switches (the host answers the bridge setters as no-ops).
void Renderer::apply_config_settings(){
 auto& config=debug_overlay_.config();
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
native_shadow_softness_=shadow_softness_constants(configured_integer(config,"shadow_softness",1),
 configured_integer(config,"shadow_sun_angle",15),configured_integer(config,"point_shadow_light_size",100));
// The common coverage/scheduler and the GPU consume the same settings,
// including when EIRebuild supplies the scene directly.
bridge_set_shadow_settings(native_shadow_side_,native_close_shadow_cascades_,
 native_far_shadows_enabled_,native_point_shadows_enabled_,native_shadows_enabled_);
neighbour_maps_enable(configured_boolean(config,"neighbour_maps",true));
bridge_set_terrain_lod_enabled(configured_boolean(config,"terrain_lod",false));
bridge_set_owned_sky(configured_integer(config,"sky_clouds",2)>0);
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

// x64 host, core mode (Host/core_host.cpp), after adopt_host_device: the
// frame resources ensure() creates, presenting into `window` (the host's
// child of the game window). The game process owns the ini: the options
// start at their registered defaults and take the game's values from its
// first config command, which precedes every frame.
// Not yet in core mode (listed): DLSS (the host's NGX feature is the
// call-stream replay's), NVAPI's shader disk cache switch, the debug overlay.
HRESULT Renderer::ensure_host(HWND window,UINT w,UINT h){
 if(!device_||!context_)return E_FAIL;
 if(swap_)return S_OK;
 core_host_=true;
 output_width_=std::max(1u,w);output_height_=std::max(1u,h);hwnd_=window;
 debug_overlay_set_running_renderer("64-bit host (core mode)",true);
 // DLSS on this device (the host's NGX instance, host_bridge.cpp).
#if defined(EID3D11_EMBEDDED) && !defined(EID3D11_SHADER_EMBED)
 set_dlss_unavailable_reason(dlss_backend_.capability().available ? nullptr : dlss_backend_.capability().reason);
#else
 {x64remote::DlssCaps caps{};
  if(!x64remote::dlss_caps(caps)){caps.reason[sizeof(caps.reason)-1]=0;set_dlss_unavailable_reason(caps.reason[0]?caps.reason:"not available");}
  else set_dlss_unavailable_reason(nullptr);}
#endif
 set_fsr_unavailable_reason(Fsr2Upscaler::unavailable_reason(device_.Get()));
 shader_objects_=ShaderObjects::attach(device_.Get());
 frame_acct::install_device_counters(device_.Get(),context_.Get());
 retained_visual_scene().visual_set_surface_texture_retainer(&Renderer::retain_detached_surface_texture,this);
 HRESULT hr;
 if(FAILED(hr=create_swap_chain()))return hr;
 ComPtr<IDXGIDevice> dxgi_device;if(SUCCEEDED(device_.As(&dxgi_device))){ComPtr<IDXGIAdapter> adapter;if(SUCCEEDED(dxgi_device->GetAdapter(&adapter)))adapter.As(&adapter3_);}
 set_test_initialization_stage("register-renderer-settings");
 register_config_options();
 apply_config_settings();
 auto& config=debug_overlay_.config();
 render_scale_percent_=std::clamp(configured_integer(config,"render_scale",100),kRenderScaleMinimum,100);
 dlss_mode_=configured_dlss_mode();fsr_mode_=configured_fsr_mode();set_world_size();
 register_embedded_shaders();
 prepare_shaders();
  set_test_initialization_stage("start-liquid-map-job");
 start_liquid_maps_job();
  set_test_initialization_stage("set-neighbour-device");
 neighbour_maps_set_device(device_.Get());
  set_test_initialization_stage("create-targets");
 if(FAILED(hr=create_targets()))return hr;
  set_test_initialization_stage("create-pipeline");
 if(FAILED(hr=create_pipeline()))return hr;
  set_test_initialization_stage("initialize-profiler");
 if(FAILED(hr=profiler_.initialize(device_.Get(),context_.Get())))return hr;
 frame_statistics_.reset();
 system_telemetry_snapshot_={};
 system_telemetry_active_=false;
 system_telemetry_.start(adapter3_.Get());
#ifdef EID3D11_EMBEDDED
  set_test_initialization_stage("attach-embedded-sky");
 if(!embedded_sky().attach(this))return E_UNEXPECTED;
#endif
  set_test_initialization_stage("complete");
 return S_OK;
}

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
 if(percent==render_scale_percent_&&dlss==dlss_mode_&&fsr==fsr_mode_)return;
 render_scale_percent_=percent;
 const UINT old_width=width_,old_height=height_;const bool mode_changed=dlss!=dlss_mode_||fsr!=fsr_mode_;const int old_mode=dlss_mode_;
 const bool had_upscaler=temporal_upscaler();
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
  dlss_previous_world_.Reset();dlss_previous_world_srv_.Reset();dlss_motion_constants_.Reset();
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
  const bool sized=dlss_backend_.optimal(output_width_,output_height_,static_cast<DlssQuality>(dlss_mode_),w,h);
#else
  const bool sized=x64remote::dlss_render_size(output_width_,output_height_,dlss_mode_,w,h);
#endif
  if(sized&&w&&h&&w<=output_width_&&h<=output_height_){
   width_=w;height_=h;return;
  }
  char reason[96];std::snprintf(reason,sizeof(reason),"DLSS has no render size for %ux%u",output_width_,output_height_);
#if defined(EID3D11_EMBEDDED) && !defined(EID3D11_SHADER_EMBED)
  const char* error=dlss_backend_.last_error();
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
 &Renderer::native_shadow_mask_ps_,&Renderer::native_low_shadow_mask_ps_};
 static_assert(std::size(members)==std::tuple_size<NativeSpecialisedSet>::value,"one specialised slot per member");
 return members;
}
#else
} // namespace
#endif
namespace {
// The key's defines (renderer.h key bits).
constexpr const char* kNativeSpecDefines[]={"NATIVE_OFF_ANTITILE","NATIVE_OFF_HEIGHT_BLEND","NATIVE_OFF_CONTACT","NATIVE_OFF_CLIFFS",
 "NATIVE_OFF_BLEND","NATIVE_OFF_POINT_SHADOWS","NATIVE_OFF_SUN_SHADOWS"};
// Bits 9-11 (bits 7 and 8 are the fog quality).
constexpr const char* kNativeSpecDefinesHigh[]={"NATIVE_DEBUG_COVERAGE","NATIVE_OFF_POINT_SOFT","NATIVE_FAST_COMBINER"};
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
  std::vector<D3D_SHADER_MACRO> macros;
  for(const D3D_SHADER_MACRO* define=program.defines;define&&define->Name;++define)macros.push_back(*define);
  for(unsigned bit=0;bit<std::size(kNativeSpecDefines);++bit)if(key&(1u<<bit))macros.push_back({kNativeSpecDefines[bit],"1"});
  if(key&128u)macros.push_back({"NATIVE_FOG_VOLUME_QUALITY","0"});else if(key&256u)macros.push_back({"NATIVE_FOG_VOLUME_QUALITY","1"});
  for(unsigned bit=0;bit<std::size(kNativeSpecDefinesHigh);++bit)if(key&(512u<<bit))macros.push_back({kNativeSpecDefinesHigh[bit],"1"});
  macros.push_back({nullptr,nullptr});
  out.macros.push_back(std::move(macros));out.programs.push_back(program);
 }
 for(std::size_t n=0;n<out.programs.size();++n)out.programs[n].defines=out.macros[n].data();
 return out.programs.size()==15;
}
namespace {

// The key a preset gives in play (map loaded, valid camera and sun cascades;
// renderer.h bits from the same rules as the uploaded values), and its
// point-shadow sibling. Key 0 (the general programs) is left out.
// The key settings give in play (value(name, fallback) reads one setting).
template<class Value> std::uint32_t native_play_specialisation_key(const Value& value){
 std::uint32_t key=0;
 if(value("terrain_quality",1.f)<1)key|=1u|2u; // terrain_material_switches: anti-tiling and height blend
 if(value("contact_blend",.7f)<=0)key|=4u;
 const bool blend=value("terrain_blend",1.f)>0;
 if(!blend||value("terrain_cliffs",1.f)<=0)key|=8u; // configured_terrain_cliffs
 if(!blend)key|=16u;
 if(value("point_shadows",1.f)<=0)key|=32u;
 if(value("shadows",1.f)<=0)key|=64u;
 const float fog=value("fog_preset",2.f);
 if(fog<1)key|=128u;else if(fog<2)key|=256u;
 if(value("shadow_softness",1.f)<=0)key|=1024u; // shadowBias[5].z
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
  for(const std::uint32_t base:{key,key|2048u})
   for(const std::uint32_t k:{base,base^32u,base|4u,(base|4u)^32u})
    if(k&&std::find(keys.begin(),keys.end(),k)==keys.end())keys.push_back(k);
 }
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
 bool cancel=false;
 static void build(ID3D11Device* device,NativeSpecialisationJob& job,std::size_t index) noexcept{
  try{
   // Prebuilt files only (shader_embed --preset-cache): no compile in the
   // game process (a D3DCompile of these programs held 350-630 MB of the
   // 32-bit address space).
   std::vector<char> code;
   const void* embedded=nullptr;std::size_t embedded_size=0;
   if(index<job.keys.size()&&shader_cache_detail::find(job.keys[index],embedded,embedded_size)) {
    ++job.cached;
    if(FAILED(device->CreatePixelShader(embedded,embedded_size,nullptr,&job.shaders[index])))++job.failed;
   } else {
    if(native_spec_cache_read(job.files[index],code))++job.cached;
    if(code.empty()||FAILED(device->CreatePixelShader(code.data(),code.size(),nullptr,&job.shaders[index])))++job.failed;
   }
  }catch(...){++job.failed;}
  ++job.finished;
 }
 void run() noexcept{
  const BackgroundWorkerPriority priority;
  for(;;){
   std::pair<std::shared_ptr<NativeSpecialisationJob>,std::size_t> item;
   {
    std::lock_guard<std::mutex> lock(mutex);
    if(cancel||queue.empty()){--running;return;}
    item=std::move(queue.front());queue.pop_front();
   }
   build(device.Get(),*item.first,item.second);
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
 return key;
}

void Renderer::release_native_specialisation() noexcept{
 native_spec_pool_.reset(); // stops the queue and joins the workers
 native_spec_jobs_.clear();
 native_spec_active_=nullptr;native_spec_sets_.clear();
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
 // A key without its prebuilt files keeps the general programs (exact): the
 // game process never compiles shaders, and a key change only looks up.
 const auto found=native_spec_files_.find(key);
 if(found==native_spec_files_.end()){
  char text[96];std::snprintf(text,sizeof(text),"not_prebuilt key=0x%03x (general programs stay for it)",key);
  bridge_report_event("shader_specialisation",text);
  native_spec_sets_[key]; // an empty set: never requested again, binds the general programs
  return;
 }
 if(!native_spec_pool_){
  native_spec_pool_=std::make_shared<NativeSpecialisationPool>();
  native_spec_pool_->device=device_;
  // Two cores stay free for the game and render threads and for input and
  // composition elsewhere (32-bit address space: each worker reserves the
  // image's 1 MB stack only while programs are queued).
  const unsigned cores=std::thread::hardware_concurrency();
  native_spec_pool_->target=cores>2?cores-2:1;
 }
 auto job=std::make_shared<NativeSpecialisationJob>();
 job->key=key;job->begin=perf_now();job->files=found->second;
 const auto keys=native_spec_keys_.find(key);if(keys==native_spec_keys_.end())return;
 job->keys=keys->second;
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
 if(key)native_spec_queue_.push_back(key);
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
  char text[224];
  auto& set=native_spec_sets_[job.key];
  if(job.failed==0){
   for(std::size_t n=0;n<set.size();++n)set[n]=job.shaders[n];
   std::snprintf(text,sizeof(text),"ready key=0x%03x programs=%zu cached=%u compiled=%u worker_ms=%.0f resident_sets=%zu building=%zu workers=%u",
    job.key,job.files.size(),job.cached.load(),job.compiled.load(),ms,native_spec_sets_.size(),native_spec_jobs_.size()-1,
    native_spec_pool_?native_spec_pool_->target:0u);
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
 if(key!=native_spec_candidate_){native_spec_candidate_=key;native_spec_candidate_frame_=native_frame_index_;}
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
}

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
   for(std::size_t n=0;n<found->second.size();++n)if(members[n]==member)return found->second[n].Get();
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
  for(std::size_t n=0;n<native_spec_active_->size();++n)
   if(members[n]==member)return (*native_spec_active_)[n].Get();
 }
 return (this->*member).Get();
}

// Depth prepass and sun-shadow mask targets (world size) and pass states.
HRESULT Renderer::create_prepass_targets(){
 prepass_rest_.Reset();prepass_normal_.Reset();shadow_mask_.Reset();
 prepass_rest_rtv_.Reset();prepass_normal_rtv_.Reset();shadow_mask_rtv_.Reset();
 prepass_rest_srv_.Reset();prepass_normal_srv_.Reset();shadow_mask_srv_.Reset();
 world_depth_read_only_dsv_.Reset();
 if(feature_level_<D3D_FEATURE_LEVEL_11_0||!depth_)return S_FALSE;
 HRESULT hr=S_OK;
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
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if(!shadow_history_constants_||FAILED(context_->Map(shadow_history_constants_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))
   return false;
  std::memcpy(mapped.pData,&constants,sizeof(constants));
  context_->Unmap(shadow_history_constants_.Get(),0);
  return true;
 };
 const auto disabled=[&]{(void)upload(NativeShadowHistoryConstants{});return false;};
 if(!native_shadows_enabled_||!shadow_history_rtv_[0]||!shadow_history_rtv_[1]||
    !shadow_history_depth_srv_||!shadow_history_constants_||!shadow_history_sampler_||
    !native_world_clip_valid_)return disabled();
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
  context_->ClearRenderTargetView(shadow_history_rtv_[shadow_history_current_index_].Get(),clear);
 }
 if(shadow_history_committed_)return disabled();
 NativeShadowHistoryConstants constants{};
 constants.previous_world_to_clip=shadow_history_previous_clip_;
 const bool can_reuse=shadow_history_valid_&&shadow_history_current_epoch_&&
  shadow_history_current_epoch_==shadow_history_previous_epoch_&&
  shadow_history_previous_frame_!=UINT64_MAX&&
  shadow_history_previous_frame_+1==native_frame_index_;
 constants.settings={float(width_),float(height_),.8f,can_reuse?1.f:0.f};
 // Soft-shadow disks rotate per frame only while history can integrate them.
 const float phase=float(native_frame_index_%64u);
 if(can_reuse)constants.jitter={phase*7.f,phase*13.f,0,0};
 return upload(constants);
}

void Renderer::commit_shadow_history() noexcept {
 if(shadow_history_committed_||!shadow_history_written_||
    shadow_history_current_frame_!=native_frame_index_||!shadow_history_depth_)return;
 const auto scene_clip=bridge_scene_world_to_clip_snapshot();
 std::array<float,16> clip{};
 if(scene_clip.valid)clip=scene_clip.world_to_clip;
#ifdef EID3D11_TESTING
 else if(test_shadow_history_epoch_&&native_world_clip_valid_)clip=native_world_clip_;
#endif
 else {shadow_history_valid_=false;shadow_history_committed_=true;return;}
 ID3D11ShaderResourceView* empty[2]{};
 context_->PSSetShaderResources(20,2,empty);
 context_->OMSetRenderTargets(0,nullptr,nullptr);
 context_->CopyResource(shadow_history_depth_.Get(),depth_.Get());
 context_->OMSetRenderTargets(1,rtv_.GetAddressOf(),dsv_.Get());
 // The history was drawn with this frame's DLSS viewport jitter: it reprojects
 // into the jittered image (zero jitter when DLSS is off).
 shadow_history_previous_clip_=dlss_jittered_clip(clip,dlss_jitter_[0],dlss_jitter_[1],width_,height_);
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
 if(!native_shadow_depth_srv_)return result;
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

HRESULT Renderer::render_debug_texture(DebugResourceView view) noexcept {
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
 DebugViewStateGuard state(context_.Get());
 ID3D11RenderTargetView* target=rtv_.Get();
 context_->OMSetRenderTargets(1,&target,nullptr);
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
 static const std::string particles=std::string(kAtmosphereShader)+kGameParticleShader;
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
 case WorldSupportShader::count:break;
 }
 return {};
}
#pragma warning(pop)
static_assert(static_cast<std::size_t>(InterfaceShader::count)==5,"interface list size changed: name the new program");
static_assert(static_cast<std::size_t>(WorldSupportShader::count)==13,"world-support list size changed: name the new program");

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
 // Always prepared at startup: enabling the cache in play must not open a second preparation.
 {"terrain_bake",append_terrain_bake_shader_programs,[](const RendererConfig&){return true;},nullptr,nullptr},
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
  "upscaler",[](RendererConfig& c){
#if defined(EID3D11_EMBEDDED) && !defined(EID3D11_SHADER_EMBED)
   if(configured_integer(c,"upscaler",kUpscalerOff)==kUpscalerDlss){
    set_dlss_unavailable_reason("DLSS shader preparation failed; selected DLSS world frames are refused");
    bridge_report_event("dlss_frame_refused",dlss_unavailable_reason());return;
   }
#endif
   c.set_integer("upscaler",kUpscalerOff);}},
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
};
ShaderMarker& shader_marker() noexcept{static ShaderMarker marker;return marker;}

std::wstring shader_marker_path(){
 std::wstring path=renderer_config_path();
 const std::size_t slash=path.find_last_of(L"\\/");
 return (slash==std::wstring::npos?std::wstring():path.substr(0,slash+1))+L"eid3d11_shader_crash.txt";
}

// Caller holds the marker's mutex.
void write_shader_marker(const ShaderMarker& marker) noexcept try{
 if(marker.group<0)return;
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

// The DLL's EID3D11_SHADERS resource; test executables have none and compile.
void Renderer::register_embedded_shaders() noexcept{
 HMODULE module=nullptr;
 if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
     reinterpret_cast<LPCWSTR>(&append_shader_groups),&module))return;
 const HRSRC resource=FindResourceW(module,L"EID3D11_SHADERS",MAKEINTRESOURCEW(10)/*RT_RCDATA*/);
 const HGLOBAL loaded=resource?LoadResource(module,resource):nullptr;
 const void* data=loaded?LockResource(loaded):nullptr;
 const std::uint32_t count=data?use_embedded_shaders(data,SizeofResource(module,resource)):0;
 char text[96];std::snprintf(text,sizeof(text),"programs=%u%s",count,resource&&!count?" (malformed table rejected)":"");
 bridge_report_event("shader_table",text);
}

void Renderer::prepare_shaders() noexcept try{
 namespace detail=shader_cache_detail;
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
 // A program missing from the table is compiled first; FXC holds up to ~250
 // MB for the largest ones, so later preparations (a settings change in a
 // map) use at most two workers to keep the 32-bit address space calm.
 const unsigned start_threads=std::clamp(std::thread::hardware_concurrency(),2u,5u)-1u;
 // One worker after the driver crashed in a parallel preparation, or when
 // it reports no concurrent creates (the runtime would serialise them).
 const bool serial=debug_overlay_.config().serial_shader_preparation()||!driver_concurrent_creates_;
 const unsigned threads=serial?1u:shader_groups_prepared_?std::min(2u,start_threads):start_threads;
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
  ComPtr<ID3D11RenderTargetView> previous_target;ComPtr<ID3D11DepthStencilView> previous_depth;
  context_->OMGetRenderTargets(1,&previous_target,&previous_depth);
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
  ID3D11RenderTargetView* restore=previous_target.Get();
  context_->OMSetRenderTargets(restore?1:0,restore?&restore:nullptr,previous_depth.Get());
  frame_statistics_.reset();
 }
 shader_groups_prepared_|=groups;
 // The driver may still compile on its own threads: the marker keeps the
 // last group, now in the after_preparation phase, until the present.
 {ShaderMarker& marker=shader_marker();std::lock_guard<std::mutex> lock(marker.mutex);
  marker.program_groups=nullptr;marker.phase="after_preparation";write_shader_marker(marker);}
 // The material cache's shader objects now, within this preparation, never
 // on its first bake frame mid-play (terrain_bake_programs).
 if(feature_level_>=D3D_FEATURE_LEVEL_11_0)(void)ensure_terrain_bake_programs();
 const double ms=1000.0*static_cast<double>(perf_now()-begin)/static_cast<double>(perf_frequency());
 char text[224];
 std::snprintf(text,sizeof(text),"programs=%zu created=%zu compiled=%zu failed=%zu retained=%zu threads=%u ms=%.0f window=%s groups=0x%x",
  preparation.total(),preparation.created(),preparation.compiled(),preparation.failed(),shader_objects_?shader_objects_->size():0u,
  threads,ms,shown?"shown":"not_needed",groups);
 bridge_report_event("shader_preparation",text);
}catch(...){
 // The features compile their programs themselves when preparation fails.
}

HRESULT Renderer::create_pipeline(){
 ComPtr<ID3DBlob> vb,pb,err;HRESULT hr=compile_shader(interface_shader_program(InterfaceShader::game_vs),&vb,&err);if(FAILED(hr))return hr;hr=compile_shader(interface_shader_program(InterfaceShader::game_ps),&pb,&err);if(FAILED(hr))return hr;if(FAILED(hr=create_shader(device_.Get(),vb->GetBufferPointer(),vb->GetBufferSize(),nullptr,&vs_)))return hr;if(FAILED(hr=create_shader(device_.Get(),pb->GetBufferPointer(),pb->GetBufferSize(),nullptr,&ps_)))return hr;
 {ComPtr<ID3DBlob> backdrop;if(FAILED(hr=compile_shader(interface_shader_program(InterfaceShader::backdrop_ps),&backdrop,&err)))return hr;if(FAILED(hr=create_shader(device_.Get(),backdrop->GetBufferPointer(),backdrop->GetBufferSize(),nullptr,&backdrop_ps_)))return hr;
  D3D11_SAMPLER_DESC sampler{};sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sampler.MaxLOD=D3D11_FLOAT32_MAX;if(FAILED(hr=device_->CreateSamplerState(&sampler,&backdrop_sampler_)))return hr;}
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
 if(!context_)return E_FAIL;
 if(scene_composited_)return draw_screen_rectangle(request,viewport);
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
 DebugViewStateGuard guard(context_.Get());
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
 context_->OMSetRenderTargets(1,rtv_.GetAddressOf(),nullptr);
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
 for(const char* entry:{"nativeBuildCS","nativeTerrainPhaseCS","nativeInstanceUpdateCS","nativePoseCS","nativeShadowPrepareCS",
                        "nativeShadowCommitCS","nativeShadowBuildCS","nativeShadowVS","nativeShadowPS"})
  out.push_back({source,nullptr,defines,entry,std::strstr(entry,"CS")?"cs_5_0":(std::strstr(entry,"VS")?"vs_5_0":"ps_5_0"),
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
 for(const D3D_SHADER_MACRO* variant:{liquid_body,liquid_body_low,liquid_ssr,liquid_foam,water,water_ssr,water_low,lava,liquid_ssr_input})
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
 native_shadow_prepare_cs_.Reset();native_shadow_commit_cs_.Reset();native_shadow_build_cs_.Reset();native_shadow_vs_.Reset();native_shadow_ps_.Reset();
 bridge_set_native_terrain_animation_owned(false);
 native_vs_.Reset();native_ps_.Reset();native_terrain_ps_.Reset();native_snow_ps_.Reset();native_low_ps_.Reset();native_low_unlit_ps_.Reset();native_terrain_flat_ps_.Reset();native_snow_flat_ps_.Reset();native_masked_ps_.Reset();native_masked_low_ps_.Reset();native_masked_terrain_ps_.Reset();native_masked_snow_ps_.Reset();native_masked_terrain_flat_ps_.Reset();native_masked_snow_flat_ps_.Reset();native_prepass_ps_.Reset();native_low_prepass_ps_.Reset();native_shadow_mask_vs_.Reset();native_shadow_mask_ps_.Reset();native_low_shadow_mask_ps_.Reset();native_prepass_vs_.Reset();native_depth_clear_vs_.Reset();native_liquid_ps_.Reset();native_low_liquid_ps_.Reset();native_liquid_ssr_ps_.Reset();native_liquid_foam_ps_.Reset();native_water_ps_.Reset();native_water_ssr_ps_.Reset();native_low_water_ps_.Reset();native_lava_ps_.Reset();native_build_cs_.Reset();native_world_lighting_constants_.Reset();native_world_lighting_valid_=false;native_world_lighting_dirty_=false;native_point_light_constants_.Reset();native_point_light_buffer_.Reset();native_point_light_srv_.Reset();native_point_light_capacity_=0;native_point_light_grid_buffer_.Reset();native_point_light_grid_srv_.Reset();native_point_light_grid_capacity_=0;native_point_light_grid_={};native_point_light_grid_inputs_.clear();native_point_lights_.clear();native_point_light_inputs_.clear();native_point_lights_dirty_=false;native_prepared_vertices_.Reset();native_prepared_vertex_srv_.Reset();native_prepared_vertex_uav_.Reset();native_prepared_vertex_capacity_=0;native_world_vertices_.Reset();native_world_vertex_srv_.Reset();native_world_vertex_uav_.Reset();native_world_vertex_capacity_=native_world_vertex_used_=0;
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
     &Renderer::native_masked_terrain_flat_ps_,&Renderer::native_masked_snow_flat_ps_};
    if(pixel_variant>=std::size(variants))return native_program_failed(program,entry,E_UNEXPECTED);
    hr=create_shader(device_.Get(),shader->GetBufferPointer(),shader->GetBufferSize(),nullptr,(this->*variants[pixel_variant++]).ReleaseAndGetAddressOf());
   }
   else if(std::strcmp(entry,"nativeLiquidPS")==0){
    static constexpr ComPtr<ID3D11PixelShader> Renderer::* liquids[]={&Renderer::native_liquid_ps_,&Renderer::native_low_liquid_ps_,
     &Renderer::native_liquid_ssr_ps_,&Renderer::native_liquid_foam_ps_,
     &Renderer::native_water_ps_,&Renderer::native_water_ssr_ps_,&Renderer::native_low_water_ps_,&Renderer::native_lava_ps_,
     &Renderer::native_liquid_ssr_input_ps_};
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
  D3D11_BUFFER_DESC shadows{};shadows.ByteWidth=sizeof(NativeShadowConstants);shadows.Usage=D3D11_USAGE_DYNAMIC;shadows.BindFlags=D3D11_BIND_CONSTANT_BUFFER;shadows.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
  if(FAILED(hr=reserve_native_owned_texture_bytes(shadows.ByteWidth,true)))return hr;
  if(FAILED(hr=device_->CreateBuffer(&shadows,nullptr,&native_shadow_constants_))||FAILED(hr=upload_native_shadow_constants(0)))return hr;
  D3D11_BUFFER_DESC lighting{};lighting.ByteWidth=sizeof(NativeWorldLighting);lighting.Usage=D3D11_USAGE_DYNAMIC;lighting.BindFlags=D3D11_BIND_CONSTANT_BUFFER;lighting.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
  if(FAILED(hr=device_->CreateBuffer(&lighting,nullptr,&native_world_lighting_constants_)))return hr;
  D3D11_BUFFER_DESC point_count{};point_count.ByteWidth=32;point_count.Usage=D3D11_USAGE_DYNAMIC;point_count.BindFlags=D3D11_BIND_CONSTANT_BUFFER;point_count.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
  const std::array<UINT,8> no_point_lights{};D3D11_SUBRESOURCE_DATA initial_count{};initial_count.pSysMem=no_point_lights.data();
  if(FAILED(hr=device_->CreateBuffer(&point_count,&initial_count,&native_point_light_constants_)))return hr;
 D3D11_BUFFER_DESC work_range{};work_range.ByteWidth=16;work_range.Usage=D3D11_USAGE_DYNAMIC;
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
 if(!device_)return E_FAIL;
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
 if(native_geometry_pages_.empty()||!device_||!context_)return E_FAIL;
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
 HRESULT hr=create_native_geometry_resource(target,created.buffer,created.srv);
 if(FAILED(hr)&&target!=required){created.buffer.Reset();created.srv.Reset();
  created.capacity=required;hr=create_native_geometry_resource(required,created.buffer,created.srv);}
 if(FAILED(hr))return FAILED(growth_failure)?hr:growth_failure;
 // An empty general slot (release_empty_native_geometry_pages) takes it first.
 UINT page_index=static_cast<UINT>(native_geometry_pages_.size());
 for(UINT i=1;i<native_geometry_pages_.size();++i)
  if(!native_geometry_pages_[i].dedicated&&!native_geometry_pages_[i].buffer){page_index=i;break;}
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
   owner.buffer.Reset();owner.srv.Reset();owner.capacity=0;owner.high_water=16;owner.free.clear();
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
 if(!context_||page_index>=native_geometry_pages_.size()||!data||!bytes)return E_INVALIDARG;
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

HRESULT Renderer::upload_native_work_range(const NativeDispatchRange& range){
 PERF06_SCOPE("upload_work");
 if(!context_||!native_work_range_constants_||!range.count)return E_INVALIDARG;
 const std::array<UINT,4> values{range.first,range.count,0,0};
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
 release_native_specialisation();widescreen_clear_hud_frame_sink(this);retained_visual_scene().visual_clear_surface_texture_retainer(this);}

void Renderer::release() {
#ifdef EID3D11_EMBEDDED
 embedded_sky().detach(this);
#endif
 // Scene thread: run and present what is queued, end it; the game thread
 // owns the renderer for teardown.
 set_scene_mode(false);
 scene_thread_.reset();
#if defined(EID3D11_EMBEDDED) && !defined(EID3D11_SHADER_EMBED)
 dlss_backend_.shutdown(); // no remaining frame calls; before any device resources are released
#endif
 write_harness_path_rows(); // frame accounting rows still buffered
#ifndef EID3D11_EMBEDDED
 camp_inventory_shutdown_icons();
#endif
 camp_inventory_retire_textures();
 // Detached copies belong to this device and must not reach the next one.
 retained_visual_scene().visual_clear_surface_texture_retainer(this);
 neighbour_client_stop();neighbour_maps_set_device(nullptr);release_neighbour_maps();
 // Tile family jobs (CPU only): cancelled and joined before the device goes.
 tile_families_job_={};tile_families_prestart_={};tile_families_prestart_terrain_.reset();
 join_tile_family_jobs();
 load_progress_movie_={};load_progress_movie_valid_=false;cancel_load_progress_decode(); // its frame textures belong to this device
 report_shadow_diagnostics("teardown");
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
 retire_native_shadows();
 native_shadow_prepare_cs_.Reset();native_shadow_commit_cs_.Reset();native_shadow_build_cs_.Reset();native_shadow_vs_.Reset();native_shadow_ps_.Reset();
 finish_placed_hitch_profile(PlacedHitchFinishReason::renderer_release);
 bridge_set_native_terrain_animation_owned(false);
 flush_native();
 system_telemetry_.stop();
 system_telemetry_active_=false;
 system_telemetry_snapshot_={};
 frame_statistics_.reset();
 debug_overlay_.clear_over_budget_marker(); // the only clean-exit path (~DD)
 close_shader_marker();
 debug_overlay_.shutdown();
 bridge_set_profile_enabled(false);
 profiler_.shutdown();
 if(context_)context_->ClearState();

 game_particle_vs_.Reset();game_particle_ps_.Reset();game_particle_constants_.Reset();game_particle_buffer_.Reset();game_particle_srv_.Reset();
 game_ribbon_vs_.Reset();game_selection_vs_.Reset();game_mesh_vs_.Reset();game_glyph_vs_.Reset();std::vector<GameParticleGpu>().swap(game_effect_elements_);
 game_particle_capacity_=0;game_particle_pipeline_failed_=false;std::vector<std::uint32_t>().swap(game_particle_order_);
 reset_pipeline_warmup();warmup_query_.Reset();warmup_targets_.clear();warmup_depths_.clear();
 pending_blits_.clear();
 pending_cursor_={};cursor_pending_=false;
 reset_camp_popup();
 camp_art_srv_.Reset();camp_art_texture_.Reset();camp_art_attempted_=false;
 camp_control_srv_.Reset();camp_control_texture_.Reset();
 camp_construction_srv_.Reset();camp_construction_texture_.Reset();
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
 for(auto& layout:native_submission_layouts_)layout={};
 native_static_shadow_bake_={};native_terrain_shadow_bake_={};native_neighbour_shadow_bake_={};native_terrain_shadow_records_.clear();
 native_shadow_work_mask_srv_.Reset();native_shadow_work_mask_.Reset();native_shadow_work_mask_capacity_=0;
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
 native_ps_.Reset();native_terrain_ps_.Reset();
 native_snow_ps_.Reset();native_low_ps_.Reset();native_low_unlit_ps_.Reset();native_terrain_flat_ps_.Reset();native_snow_flat_ps_.Reset();native_masked_ps_.Reset();native_masked_low_ps_.Reset();native_masked_terrain_ps_.Reset();native_masked_snow_ps_.Reset();native_masked_terrain_flat_ps_.Reset();native_masked_snow_flat_ps_.Reset();native_prepass_ps_.Reset();native_low_prepass_ps_.Reset();native_shadow_mask_vs_.Reset();native_shadow_mask_ps_.Reset();native_low_shadow_mask_ps_.Reset();native_liquid_ps_.Reset();native_low_liquid_ps_.Reset();native_liquid_ssr_ps_.Reset();native_liquid_foam_ps_.Reset();native_water_ps_.Reset();native_water_ssr_ps_.Reset();native_low_water_ps_.Reset();native_lava_ps_.Reset();
 
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
 backdrop_ps_.Reset();backdrop_sampler_.Reset();interface_scaled_ps_.Reset();interface_scaled_ps_failed_=false;interface_text_ps_.Reset();interface_text_ps_failed_=false;gpu_blit_key_.reset();
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
 dsv_.Reset();world_dsv_.Reset();world_depth_read_only_dsv_.Reset();prepass_rest_.Reset();prepass_normal_.Reset();shadow_mask_.Reset();prepass_rest_rtv_.Reset();prepass_normal_rtv_.Reset();shadow_mask_rtv_.Reset();prepass_rest_srv_.Reset();prepass_normal_srv_.Reset();shadow_mask_srv_.Reset();for(auto& state:prepass_depth_states_)state.Reset();masked_depth_state_.Reset();shadow_mask_depth_state_.Reset();shadow_mask_raster_.Reset();interface_dsv_.Reset();interface_depth_.Reset();
 depth_.Reset();
 rtv_.Reset();backbuffer_rtv_.Reset();scene_composite_.reset();scene_composited_=false;
 dlss_previous_world_.Reset();dlss_previous_world_srv_.Reset();dlss_motion_constants_.Reset();dlss_motion_depth_.Reset();
 native_motion_vs_.Reset();native_motion_ps_.Reset();dlss_mode_=-1;fsr_.reset();upscale_sharpen_.reset();fsr_mode_=-1;
 gamma_lut_.Reset();gamma_lut_srv_.Reset();gamma_copy_.Reset();gamma_copy_srv_.Reset();
 gamma_vs_.Reset();gamma_ps_.Reset();gamma_sampler_.Reset();gamma_failed_=false;scene_updated_=false;deferred_ground_marks_.clear();
 adapter3_.Reset();
 frame_acct::uninstall_device_counters();
 context1_.Reset();context_.Reset();
 // The prepared shader objects hold the device: drop them before it.
 if(shader_objects_)shader_objects_->detach(device_.Get());
 shader_objects_.Reset();shader_groups_prepared_=0;
 device_.Reset();
 swap_.Reset();
 nvidia_shader_cache_api_.reset();
 feature_level_=D3D_FEATURE_LEVEL_9_3;
 last_present_tick_=0;
 present_interval_ms_=0.0;
 present_smoothed_ms_=0.0;
#ifdef EID3D11_EMBEDDED
 embedded_figure_layers_.clear(); embedded_figure_vs_.Reset(); embedded_figure_ps_.Reset(); embedded_figure_sampler_.Reset();
#endif
}

#endif
} // namespace owned
