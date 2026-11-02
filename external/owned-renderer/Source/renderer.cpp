// Renderer core shared by the renderer_*.cpp files: surface memory accounting, native screen extents and the shared statics (renderer_internal.h).
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
#include "x64_remote_link.h"
#include "dlss_frame.h"
#include "stall_watchdog.h"
#include <optional>
#include <sstream>
#include <d3dcompiler.h>
#include <wincodec.h>
#include <objbase.h>
#pragma comment(lib, "windowscodecs.lib")
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

namespace owned {

// Pool identities outlive a Renderer instance; never identify GPU state by address.
std::atomic<std::uint64_t> g_cpu_surface_bytes{0};
std::atomic<std::uint64_t> g_gpu_surface_bytes{0};
std::atomic<std::uint64_t> g_double_resident_bytes{0};
namespace {
std::atomic<std::uint64_t> g_released_surface_bytes{0};
std::atomic<std::uint64_t> g_released_surfaces{0};
}  // namespace
std::atomic<std::uint64_t> g_restore_readbacks{0};
std::atomic<std::uint64_t> g_restore_readback_qpc{0};
std::atomic<std::uint64_t> g_pinned_surfaces{0};
namespace {

void replace_accounted_bytes(std::atomic<std::uint64_t>& total,
                             std::uint64_t previous, std::uint64_t current) noexcept {
    if (current >= previous) total.fetch_add(current - previous, std::memory_order_relaxed);
    else total.fetch_sub(previous - current, std::memory_order_relaxed);
}

// Bytes of one texel, or of one 4x4 block for block-compressed formats.
UINT format_element_bytes(DXGI_FORMAT format, bool& block) noexcept {
    const auto f = static_cast<UINT>(format);
    block = (f >= DXGI_FORMAT_BC1_TYPELESS && f <= DXGI_FORMAT_BC5_SNORM) ||
            (f >= DXGI_FORMAT_BC6H_TYPELESS && f <= DXGI_FORMAT_BC7_UNORM_SRGB);
    if (block)
        return (f <= DXGI_FORMAT_BC1_UNORM_SRGB || (f >= DXGI_FORMAT_BC4_TYPELESS && f <= DXGI_FORMAT_BC4_SNORM)) ? 8u : 16u;
    if (f >= DXGI_FORMAT_R32G32B32A32_TYPELESS && f <= DXGI_FORMAT_R32G32B32A32_SINT) return 16u;
    if (f >= DXGI_FORMAT_R32G32B32_TYPELESS && f <= DXGI_FORMAT_R32G32B32_SINT) return 12u;
    if (f >= DXGI_FORMAT_R16G16B16A16_TYPELESS && f <= DXGI_FORMAT_X32_TYPELESS_G8X24_UINT) return 8u;
    if (f >= DXGI_FORMAT_R8G8_TYPELESS && f <= DXGI_FORMAT_R16_SINT) return 2u;
    if (f >= DXGI_FORMAT_R8_TYPELESS && f <= DXGI_FORMAT_A8_UNORM) return 1u;
    if (format == DXGI_FORMAT_B5G6R5_UNORM || format == DXGI_FORMAT_B5G5R5A1_UNORM ||
        format == DXGI_FORMAT_B4G4R4A4_UNORM) return 2u;
    return 4u;  // the 32-bit colour, depth and packed float formats
}

std::uint64_t surface_bytes(UINT width, UINT height, UINT depth, UINT element, bool block) noexcept {
    if (block) { width = (width + 3) / 4; height = (height + 3) / 4; }
    return std::uint64_t(std::max(1u, width)) * std::max(1u, height) * std::max(1u, depth) * element;
}
}  // namespace


// Storage of one D3D11 buffer or texture from its description (all mips,
// slices and samples). Used for the settings-window video memory breakdown.
std::uint64_t resource_video_bytes(ID3D11Resource* resource) noexcept {
    if (!resource) return 0;
    D3D11_RESOURCE_DIMENSION dimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
    resource->GetType(&dimension);
    bool block = false;
    std::uint64_t bytes = 0;
    switch (dimension) {
    case D3D11_RESOURCE_DIMENSION_BUFFER: {
        D3D11_BUFFER_DESC d{}; static_cast<ID3D11Buffer*>(resource)->GetDesc(&d);
        return d.Usage == D3D11_USAGE_STAGING ? 0 : d.ByteWidth;  // staging copies live in system memory
    }
    case D3D11_RESOURCE_DIMENSION_TEXTURE2D: {
        D3D11_TEXTURE2D_DESC d{}; static_cast<ID3D11Texture2D*>(resource)->GetDesc(&d);
        if (d.Usage == D3D11_USAGE_STAGING) return 0;
        const UINT element = format_element_bytes(d.Format, block);
        for (UINT mip = 0; mip < std::max(1u, d.MipLevels); ++mip)
            bytes += surface_bytes(d.Width >> mip, d.Height >> mip, 1, element, block);
        return bytes * std::max(1u, d.ArraySize) * std::max(1u, d.SampleDesc.Count);
    }
    case D3D11_RESOURCE_DIMENSION_TEXTURE3D: {
        D3D11_TEXTURE3D_DESC d{}; static_cast<ID3D11Texture3D*>(resource)->GetDesc(&d);
        if (d.Usage == D3D11_USAGE_STAGING) return 0;
        const UINT element = format_element_bytes(d.Format, block);
        for (UINT mip = 0; mip < std::max(1u, d.MipLevels); ++mip)
            bytes += surface_bytes(d.Width >> mip, d.Height >> mip, d.Depth >> mip, element, block);
        return bytes;
    }
    case D3D11_RESOURCE_DIMENSION_TEXTURE1D: {
        D3D11_TEXTURE1D_DESC d{}; static_cast<ID3D11Texture1D*>(resource)->GetDesc(&d);
        if (d.Usage == D3D11_USAGE_STAGING) return 0;
        const UINT element = format_element_bytes(d.Format, block);
        for (UINT mip = 0; mip < std::max(1u, d.MipLevels); ++mip)
            bytes += surface_bytes(d.Width >> mip, 1, 1, element, block);
        return bytes * std::max(1u, d.ArraySize);
    }
    default: return 0;
    }
}
namespace {



static_assert(sizeof(NativeVertexComponent) == 6, "verified packed component ABI changed");
static_assert(sizeof(NativeWorldLighting) == 48,
              "native world-light constant layout changed");
static_assert(sizeof(NativeTerrainVertex) == 120, "native terrain vertex layout changed");
static_assert(sizeof(NativeTerrainState) == 72, "native terrain state layout changed");
}  // namespace

// The native world shader source (native_shader.h), composed once.
const std::string& native_shader_source();
const std::string& owned_native_shader_source(){return native_shader_source();}
const std::string& native_shader_source(){
 static const std::string source=compose_native_shader_source();return source;
}
namespace {

UINT fvf_stride_impl(DWORD fvf){
 const bool tl=(fvf&D3DFVF_POSITION_MASK)==D3DFVF_XYZRHW,xyz=(fvf&D3DFVF_POSITION_MASK)==D3DFVF_XYZ;if(!tl&&!xyz)return 0;UINT stride=tl?16:12;if(fvf&D3DFVF_NORMAL)stride+=12;if(fvf&D3DFVF_DIFFUSE)stride+=4;if(fvf&D3DFVF_SPECULAR)stride+=4;const DWORD tc=(fvf&D3DFVF_TEXCOUNT_MASK)>>D3DFVF_TEXCOUNT_SHIFT;stride+=tc*8;return stride;
}
}  // namespace

UINT legacy_fvf_stride(DWORD fvf){return fvf_stride_impl(fvf);}

PlacedHitchArmInputs placed_hitch_arm_inputs(
 const BridgeProfileSnapshot* previous_profile,
 const RetainedSceneIdentitySnapshot& current_scene) noexcept {
 PlacedHitchArmInputs result{};
 if(previous_profile&&previous_profile->frame!=0){
  result.previous_bridge_frame=previous_profile->frame;
  result.previous_bridge_frame_valid=true;
 }
 result.current_world_epoch=current_scene.positive_world_epoch;
 result.current_world_epoch_valid=current_scene.positive_world_epoch!=0;
 return result;
}

bool arm_placed_hitch_profile(
 PlacedHitchProfile& profile,const PlacedHitchArmInputs& inputs) {
 if(!inputs.current_world_epoch_valid)return false;
 return profile.arm(inputs.previous_bridge_frame,
  inputs.previous_bridge_frame_valid,inputs.current_world_epoch);
}

POINT capture_stretch_source_point(const RECT& source, const RECT& destination,
                                   LONG destination_x,
                                   LONG destination_y) noexcept {
 const LONG source_width=source.right-source.left,source_height=source.bottom-source.top;
 const LONG destination_width=destination.right-destination.left,destination_height=destination.bottom-destination.top;
 if(source_width<=0||source_height<=0||destination_width<=0||destination_height<=0)return POINT{-1,-1};
 const LONG local_x=std::clamp(destination_x-destination.left,0L,destination_width-1);
 const LONG local_y=std::clamp(destination_y-destination.top,0L,destination_height-1);
 return POINT{source.left+(LONG)((std::int64_t)local_x*source_width/destination_width),source.top+(LONG)((std::int64_t)local_y*source_height/destination_height)};
}

void update_surface_memory_accounting(SurfaceImage& image) noexcept {
 if(image.scene_snapshot)return;  // the game thread's image is accounted
 const std::uint64_t cpu_bytes=image.pixels.capacity();
 const std::uint64_t gpu_bytes=texture_storage_bytes(image.texture.Get());
 const std::uint64_t double_resident=cpu_bytes&&gpu_bytes?std::min(cpu_bytes,gpu_bytes):0;
 replace_accounted_bytes(g_cpu_surface_bytes,image.accounted_cpu_bytes,cpu_bytes);
 replace_accounted_bytes(g_gpu_surface_bytes,image.accounted_gpu_bytes,gpu_bytes);
 replace_accounted_bytes(g_double_resident_bytes,image.accounted_double_resident_bytes,double_resident);
 const std::uint64_t released=image.cpu_released?static_cast<std::uint64_t>(image.pitch)*
  ((image.format.dwFlags&DDPF_FOURCC)?std::max(1u,(image.height+3u)/4u):image.height):0;
 if((released!=0)!=(image.accounted_released_bytes!=0)){
  if(released)g_released_surfaces.fetch_add(1,std::memory_order_relaxed);
  else g_released_surfaces.fetch_sub(1,std::memory_order_relaxed);
 }
 replace_accounted_bytes(g_released_surface_bytes,image.accounted_released_bytes,released);
 image.accounted_cpu_bytes=cpu_bytes;
 image.accounted_gpu_bytes=gpu_bytes;
 image.accounted_double_resident_bytes=double_resident;
 image.accounted_released_bytes=released;
}

void release_surface_memory_accounting(SurfaceImage& image) noexcept {
 if(image.scene_snapshot)return;
 replace_accounted_bytes(g_cpu_surface_bytes,image.accounted_cpu_bytes,0);
 replace_accounted_bytes(g_gpu_surface_bytes,image.accounted_gpu_bytes,0);
 replace_accounted_bytes(g_double_resident_bytes,image.accounted_double_resident_bytes,0);
 if(image.accounted_released_bytes)g_released_surfaces.fetch_sub(1,std::memory_order_relaxed);
 replace_accounted_bytes(g_released_surface_bytes,image.accounted_released_bytes,0);
 if(image.cpu_pinned)g_pinned_surfaces.fetch_sub(1,std::memory_order_relaxed);
 image.cpu_pinned=false;
 image.accounted_cpu_bytes=0;
 image.accounted_gpu_bytes=0;
 image.accounted_double_resident_bytes=0;
 image.accounted_released_bytes=0;
}

SurfaceResidencyStats surface_residency_stats() noexcept {
 SurfaceResidencyStats stats;
 stats.released_surfaces=g_released_surfaces.load(std::memory_order_relaxed);
 stats.released_bytes=g_released_surface_bytes.load(std::memory_order_relaxed);
 stats.restore_readbacks=g_restore_readbacks.load(std::memory_order_relaxed);
 stats.restore_readback_qpc=g_restore_readback_qpc.load(std::memory_order_relaxed);
 stats.pinned_surfaces=g_pinned_surfaces.load(std::memory_order_relaxed);
 return stats;
}

FixedState::FixedState(){
 render[D3DRENDERSTATE_ZENABLE]=D3DZB_TRUE;render[D3DRENDERSTATE_ZWRITEENABLE]=TRUE;render[D3DRENDERSTATE_ZFUNC]=D3DCMP_LESSEQUAL;render[D3DRENDERSTATE_CULLMODE]=D3DCULL_CCW;render[D3DRENDERSTATE_FILLMODE]=D3DFILL_SOLID;render[D3DRENDERSTATE_SRCBLEND]=D3DBLEND_ONE;render[D3DRENDERSTATE_DESTBLEND]=D3DBLEND_ZERO;render[D3DRENDERSTATE_ALPHAFUNC]=D3DCMP_ALWAYS;render[D3DRENDERSTATE_TEXTUREFACTOR]=0xffffffffu;
 stage[0][D3DTSS_COLOROP]=D3DTOP_MODULATE;stage[0][D3DTSS_COLORARG1]=D3DTA_TEXTURE;stage[0][D3DTSS_COLORARG2]=D3DTA_DIFFUSE;stage[0][D3DTSS_ALPHAOP]=D3DTOP_MODULATE;stage[0][D3DTSS_ALPHAARG1]=D3DTA_TEXTURE;stage[0][D3DTSS_ALPHAARG2]=D3DTA_DIFFUSE;stage[0][D3DTSS_ADDRESSU]=stage[0][D3DTSS_ADDRESSV]=D3DTADDRESS_WRAP;stage[0][D3DTSS_MAGFILTER]=stage[0][D3DTSS_MINFILTER]=D3DTFG_LINEAR;stage[0][D3DTSS_MIPFILTER]=D3DTFP_NONE;
 for(auto& m:transform)identity(m);viewport.dwWidth=viewport.dwHeight=1;viewport.dvMaxZ=1;material.diffuse={1,1,1,1};
}

HRESULT calculate_native_screen_extents(const NativeGeometryView& view,
                                         const D3DVIEWPORT7& viewport,
                                         RECT& extents) noexcept {
 struct ClipVertex { double x, y, z, w; };
 constexpr std::uint32_t kSphere = 1u;
 constexpr std::uint32_t kBox = 2u;
 constexpr std::uint8_t triangles[12][3] = {
  {0,2,3},{0,3,1},{4,5,7},{4,7,6},{0,1,5},{0,5,4},
  {2,6,7},{2,7,3},{0,4,6},{0,6,2},{1,3,7},{1,7,5}};
 extents = {};
 if (!view.bounds_valid || !(view.bounds_flags & (kSphere | kBox)) ||
     !viewport.dwWidth || !viewport.dwHeight) return E_INVALIDARG;
 for (float value : view.object_to_clip)
  if (!std::isfinite(value)) return E_INVALIDARG;

 std::array<double,3> minimum{}, maximum{};
 if (view.bounds_flags & kBox) {
  for (std::size_t axis=0; axis<3; ++axis) {
   minimum[axis]=static_cast<double>(view.bounds_center[axis])+view.bounds_minimum[axis];
   maximum[axis]=static_cast<double>(view.bounds_center[axis])+view.bounds_maximum[axis];
   if (!std::isfinite(minimum[axis]) || !std::isfinite(maximum[axis]) ||
       minimum[axis] > maximum[axis]) return E_INVALIDARG;
  }
 } else {
  if (!std::isfinite(view.bounds_radius) || view.bounds_radius < 0.0f)
   return E_INVALIDARG;
  for (std::size_t axis=0; axis<3; ++axis) {
   minimum[axis]=static_cast<double>(view.bounds_center[axis])-view.bounds_radius;
   maximum[axis]=static_cast<double>(view.bounds_center[axis])+view.bounds_radius;
   if (!std::isfinite(minimum[axis]) || !std::isfinite(maximum[axis]))
    return E_INVALIDARG;
  }
 }

 std::array<ClipVertex,8> corners{};
 const auto& m=view.object_to_clip;
 for (std::size_t corner=0; corner<corners.size(); ++corner) {
  const double x=(corner&1u)?maximum[0]:minimum[0];
  const double y=(corner&2u)?maximum[1]:minimum[1];
  const double z=(corner&4u)?maximum[2]:minimum[2];
  corners[corner]={m[0]*x+m[1]*y+m[2]*z+m[3],
                   m[4]*x+m[5]*y+m[6]*z+m[7],
                   m[8]*x+m[9]*y+m[10]*z+m[11],
                   m[12]*x+m[13]*y+m[14]*z+m[15]};
  if (!std::isfinite(corners[corner].x) || !std::isfinite(corners[corner].y) ||
      !std::isfinite(corners[corner].z) || !std::isfinite(corners[corner].w))
   return E_INVALIDARG;
 }
 const auto distance=[](const ClipVertex& vertex,int plane) {
  switch(plane) { case 0:return vertex.x+vertex.w; case 1:return vertex.w-vertex.x;
   case 2:return vertex.y+vertex.w; case 3:return vertex.w-vertex.y;
   case 4:return vertex.z; default:return vertex.w-vertex.z; }
 };
 double minimum_x=std::numeric_limits<double>::max(),minimum_y=minimum_x;
 double maximum_x=-minimum_x,maximum_y=-minimum_x;
 bool visible=false;
 const auto include_projected=[&](const ClipVertex& vertex) {
  if (std::abs(vertex.w)<=1e-20) return;
  const double reciprocal_w=1.0/vertex.w;
  const double screen_x=viewport.dwX+(vertex.x*reciprocal_w+1.0)*0.5*viewport.dwWidth;
  const double screen_y=viewport.dwY+(1.0-vertex.y*reciprocal_w)*0.5*viewport.dwHeight;
  if (!std::isfinite(screen_x)||!std::isfinite(screen_y)) return;
  minimum_x=std::min(minimum_x,screen_x);minimum_y=std::min(minimum_y,screen_y);
  maximum_x=std::max(maximum_x,screen_x);maximum_y=std::max(maximum_y,screen_y);
  visible=true;
 };
 std::uint32_t outside_everywhere=0x3fu,outside_anywhere=0;
 for (const auto& vertex:corners) {
  std::uint32_t mask=0;
  if(vertex.x < -vertex.w)mask|=1u;if(vertex.x > vertex.w)mask|=2u;
  if(vertex.y < -vertex.w)mask|=4u;if(vertex.y > vertex.w)mask|=8u;
  if(vertex.z < 0.0)mask|=16u;if(vertex.z > vertex.w)mask|=32u;
  outside_everywhere&=mask;outside_anywhere|=mask;
  if(mask==0)include_projected(vertex);
 }
 if(outside_everywhere!=0)return S_FALSE;
 if(outside_anywhere!=0) {
  minimum_x=minimum_y=std::numeric_limits<double>::max();
  maximum_x=maximum_y=-minimum_x;visible=false;
  for (const auto& triangle:triangles) {
   std::array<ClipVertex,16> first{},second{};
   for(std::size_t corner=0;corner<3;++corner)first[corner]=corners[triangle[corner]];
   std::size_t count=3;auto* input=&first;auto* output=&second;
   for(int plane=0;plane<6&&count;++plane) {
    std::size_t output_count=0;ClipVertex previous=(*input)[count-1];
    double previous_distance=distance(previous,plane);
    bool previous_inside=previous_distance>=0.0;
    for(std::size_t index=0;index<count;++index) {
     const ClipVertex current=(*input)[index];
     const double current_distance=distance(current,plane);
     const bool current_inside=current_distance>=0.0;
     if(previous_inside!=current_inside) {
      const double denominator=previous_distance-current_distance;
      if(std::abs(denominator)>1e-20&&output_count<output->size()) {
       const double amount=previous_distance/denominator;
       (*output)[output_count++]={previous.x+(current.x-previous.x)*amount,
        previous.y+(current.y-previous.y)*amount,previous.z+(current.z-previous.z)*amount,
        previous.w+(current.w-previous.w)*amount};
      }
     }
     if(current_inside&&output_count<output->size())(*output)[output_count++]=current;
     previous=current;previous_distance=current_distance;previous_inside=current_inside;
    }
    count=output_count;std::swap(input,output);
   }
   for(std::size_t index=0;index<count;++index)include_projected((*input)[index]);
  }
 }
 if(!visible)return S_FALSE;
 const auto to_long=[](double value) {
  const double bounded=std::clamp(value,
   static_cast<double>(std::numeric_limits<LONG>::min()),
   static_cast<double>(std::numeric_limits<LONG>::max()));
  return static_cast<LONG>(std::nearbyint(bounded));
 };
 extents={to_long(minimum_x),to_long(minimum_y),to_long(maximum_x),to_long(maximum_y)};
 return S_OK;
}

HRESULT calculate_native_aabb_screen_extents(const float* minimum, const float* maximum,
                                              const std::array<float, 16>& object_to_clip,
                                              const D3DVIEWPORT7& viewport,
                                              RECT& extents) noexcept {
 if(!minimum||!maximum)return E_INVALIDARG;
 NativeGeometryView bounds_view{};
 bounds_view.bounds_flags=2u;
 bounds_view.bounds_valid=true;
 for(std::size_t axis=0;axis<3;++axis){
  bounds_view.bounds_minimum[axis]=minimum[axis];
  bounds_view.bounds_maximum[axis]=maximum[axis];
 }
 bounds_view.object_to_clip=object_to_clip;
 return calculate_native_screen_extents(bounds_view,viewport,extents);
}

#ifdef EID3D11_TESTING
HRESULT Renderer::test_render_debug_resource_view(DebugResourceView view,
                                                   DebugOverlayMode mode) noexcept {
 return render_debug_resource_view(view,mode);
}

void Renderer::test_attach_debug_resource_view_device(
    ID3D11Device* device,ID3D11DeviceContext* context,
    ID3D11RenderTargetView* render_target,ID3D11Texture2D* depth,
    ID3D11DepthStencilView* depth_view,
    ID3D11ShaderResourceView* depth_resource,
    ID3D11ShaderResourceView* ao_resource,UINT width,UINT height) noexcept {
 device_=device;context_=context;rtv_=render_target;depth_=depth;dsv_=depth_view;
 depth_srv_=depth_resource;ao_srv_=ao_resource;width_=output_width_=std::max(1u,width);
 height_=output_height_=std::max(1u,height);world_dsv_=interface_dsv_=dsv_;feature_level_=device?device->GetFeatureLevel():
  D3D_FEATURE_LEVEL_9_3;
}

void Renderer::test_set_debug_projection(const D3DMATRIX& projection) noexcept {
 FixedState state{};state.transform[D3DTRANSFORMSTATE_PROJECTION]=projection;
 remember_debug_projection(state,SemanticKind::terrain);
}

void Renderer::test_attach_debug_shadow_maps(ID3D11ShaderResourceView* maps,
    NativeShadowFrame frame,std::uint8_t valid_mask,std::uint8_t rewritten_mask) noexcept {
 native_shadow_depth_srv_=maps;native_shadow_frame_=std::move(frame);
 for(unsigned i=0;i<5;++i)native_shadow_valid_[i]=(valid_mask&(1u<<i))!=0;
 native_shadow_cleared_mask_=rewritten_mask;
 native_shadow_collecting_=native_shadow_failed_=false;
}

DebugResourceViewAvailability
Renderer::test_debug_resource_view_availability() const noexcept {
 return debug_resource_view_availability();
}

#endif

} // namespace owned
