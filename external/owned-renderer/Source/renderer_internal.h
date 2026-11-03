#pragma once

// Internal helpers shared by the renderer*.cpp files split from
// renderer.cpp; not for other modules. Types and small functions keep
// their internal linkage (one copy per file); the counters and the
// composed native shader source are defined once, in renderer.cpp.

#include "embedded_perf06.h"
#include "combat_text.h"
#ifdef EID3D11_EMBEDDED
#include "embedded_host.h"
#include "embedded_sky.h"
#include "hud_layout_context.h"
#include "../../../Game/CorePerf.h"
#endif
#include "renderer.h"
#include "engine_bridge.h"
#include "fixed_state_descs.h"
#include <atomic>
#include <cstdint>
#include <string>

namespace owned {

extern std::atomic<std::uint64_t> g_cpu_surface_bytes;
extern std::atomic<std::uint64_t> g_gpu_surface_bytes;
extern std::atomic<std::uint64_t> g_double_resident_bytes;
extern std::atomic<std::uint64_t> g_restore_readbacks;
extern std::atomic<std::uint64_t> g_restore_readback_qpc;
extern std::atomic<std::uint64_t> g_pinned_surfaces;
const std::string& native_shader_source();

namespace {



HRESULT native_geometry_failure(NativeGeometryResult* result, HRESULT hr,
    const char* operation, bool texture = false) noexcept {
 if(result && FAILED(hr)) {
  result->failure_hresult=hr;result->failure_operation=operation;result->failure_texture=texture;
 }
 return hr;
}

class SceneRendererTimingScope {
public:
    explicit SceneRendererTimingScope(
        SceneRendererTimingKind kind) noexcept
        : token_(bridge_scene_renderer_timing_begin(kind)) {}
    ~SceneRendererTimingScope() {
        bridge_scene_renderer_timing_end(token_);
    }

    SceneRendererTimingScope(const SceneRendererTimingScope&) = delete;
    SceneRendererTimingScope& operator=(const SceneRendererTimingScope&) = delete;

private:
    SceneRendererTimingToken token_{};
};

// Bytes of one slice of a width x height texture with mip_levels levels.
std::uint64_t texture_storage_bytes(UINT width, UINT height, UINT mip_levels, DXGI_FORMAT format) noexcept {
    std::uint64_t result = 0;
    mip_levels = std::max(1u, mip_levels);
    for (UINT mip = 0; mip < mip_levels; ++mip) {
        if (format == DXGI_FORMAT_BC1_UNORM ||
            format == DXGI_FORMAT_BC1_UNORM_SRGB) {
            result += static_cast<std::uint64_t>(std::max(1u, (width + 3) / 4)) *
                      std::max(1u, (height + 3) / 4) * 8u;
        } else if (format == DXGI_FORMAT_BC2_UNORM ||
                   format == DXGI_FORMAT_BC2_UNORM_SRGB ||
                   format == DXGI_FORMAT_BC3_UNORM ||
                   format == DXGI_FORMAT_BC3_UNORM_SRGB) {
            result += static_cast<std::uint64_t>(std::max(1u, (width + 3) / 4)) *
                      std::max(1u, (height + 3) / 4) * 16u;
        } else if (format == DXGI_FORMAT_R8_UNORM) {
            result += static_cast<std::uint64_t>(width) * height;
        } else if (format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
            result += static_cast<std::uint64_t>(width) * height * 8u;
        } else if (format == DXGI_FORMAT_B5G6R5_UNORM ||
                   format == DXGI_FORMAT_B5G5R5A1_UNORM ||
                   format == DXGI_FORMAT_B4G4R4A4_UNORM) {
            result += static_cast<std::uint64_t>(width) * height * 2u;
        } else {
            result += static_cast<std::uint64_t>(width) * height * 4u;
        }
        width = std::max(1u, width / 2);
        height = std::max(1u, height / 2);
    }
    return result;
}

std::uint64_t texture_storage_bytes(ID3D11Texture2D* texture) noexcept {
    if (!texture) return 0;
    D3D11_TEXTURE2D_DESC description{};
    texture->GetDesc(&description);
    return texture_storage_bytes(description.Width, description.Height, description.MipLevels, description.Format) *
           std::max(1u, description.ArraySize) * std::max(1u, description.SampleDesc.Count);
}


using Vertex = FixedFunctionVertex;
struct Constants {
    D3DMATRIX world,view,projection;
    float viewport[4],fog_color[4],texture_factor[4];
    UINT transformed,textured,color_op,alpha_op;
    UINT color_arg1,color_arg2,alpha_arg1,alpha_arg2;
    UINT alpha_test,alpha_func; float alpha_ref; UINT fog_enable;
    UINT specular_enable,native_triangle_base,padding[2];
    float contact_origin[4]{}; // xyz world origin, w colour mip
    UINT contact_materials[2]{},liquid_layer=0,contact_padding=0; // liquid_layer (nativeLiquidLayer): bit 0 foam layer pass, bit 1 liquid triangles only, bit 2 the other triangles only, bit 3 stock-look water
    UINT combiner[2]{}; float hud_opacity=1.f; UINT combiner_padding=0; // nativeCombiner: fast colour and alpha stage modes (native_fast_combiner_mode)
    // nativeContactLayout: xy the batch map's saved-layout origin on whole
    // tiles (neighbour_layout_tile_origin), the frame of its terrain material
    // layouts; z 1 for shelter bake foliage (renderer_shadows.cpp), w the batch
    // map's terrain 2.0 table entry + 1 (update_terrain2_table; 0 none).
    float layout_origin[4]{};
};

struct ClearConstants {
    float color[4];
    float depth;
    float padding[3];
};

class DebugViewStateGuard {
public:
 explicit DebugViewStateGuard(ID3D11DeviceContext* context) noexcept : context_(context) {
  if(!context_)return;
  context_->IAGetInputLayout(&input_layout_);
  context_->IAGetPrimitiveTopology(&topology_);
  context_->IAGetVertexBuffers(0,1,&vertex_buffer_,&vertex_stride_,&vertex_offset_);
  context_->IAGetIndexBuffer(&index_buffer_,&index_format_,&index_offset_);
  context_->VSGetShader(&vertex_shader_,nullptr,nullptr);
  context_->PSGetShader(&pixel_shader_,nullptr,nullptr);
  context_->GSGetShader(&geometry_shader_,nullptr,nullptr);
  context_->HSGetShader(&hull_shader_,nullptr,nullptr);
  context_->DSGetShader(&domain_shader_,nullptr,nullptr);
  draw_constants_.save(context_,0); // with its ring range (constant_ring.h)
  context_->PSGetShaderResources(0,3,pixel_resources_);
  context_->PSGetSamplers(0,1,&pixel_sampler_);
  context_->PSGetSamplers(1,1,&second_sampler_);
  context_->PSGetConstantBuffers(1,1,&second_constant_);
  viewport_count_=static_cast<UINT>(viewports_.size());
  context_->RSGetViewports(&viewport_count_,viewports_.data());
  scissor_count_=static_cast<UINT>(scissors_.size());
  context_->RSGetScissorRects(&scissor_count_,scissors_.data());
  context_->RSGetState(&rasterizer_);
  context_->OMGetRenderTargets(static_cast<UINT>(render_targets_.size()),
                               render_targets_.data(),&depth_view_);
  context_->OMGetBlendState(&blend_state_,blend_factor_,&sample_mask_);
  context_->OMGetDepthStencilState(&depth_state_,&stencil_reference_);
 }
 ~DebugViewStateGuard(){restore();}
 DebugViewStateGuard(const DebugViewStateGuard&)=delete;
 DebugViewStateGuard& operator=(const DebugViewStateGuard&)=delete;
 void restore() noexcept {
  if(!context_)return;
  ID3D11ShaderResourceView* null_resources[3]{};
  context_->PSSetShaderResources(0,3,null_resources);
  context_->IASetInputLayout(input_layout_);
  context_->IASetPrimitiveTopology(topology_);
  context_->IASetVertexBuffers(0,1,&vertex_buffer_,&vertex_stride_,&vertex_offset_);
  context_->IASetIndexBuffer(index_buffer_,index_format_,index_offset_);
  context_->VSSetShader(vertex_shader_,nullptr,0);
  context_->PSSetShader(pixel_shader_,nullptr,0);
  context_->GSSetShader(geometry_shader_,nullptr,0);
  context_->HSSetShader(hull_shader_,nullptr,0);
  context_->DSSetShader(domain_shader_,nullptr,0);
  draw_constants_.restore(context_,0);
  context_->PSSetShaderResources(0,3,pixel_resources_);
  context_->PSSetSamplers(0,1,&pixel_sampler_);
  context_->PSSetSamplers(1,1,&second_sampler_);
  context_->PSSetConstantBuffers(1,1,&second_constant_);
  context_->RSSetViewports(viewport_count_,viewports_.data());
  context_->RSSetScissorRects(scissor_count_,scissors_.data());
  context_->RSSetState(rasterizer_);
  context_->OMSetRenderTargets(static_cast<UINT>(render_targets_.size()),
                               render_targets_.data(),depth_view_);
  context_->OMSetBlendState(blend_state_,blend_factor_,sample_mask_);
  context_->OMSetDepthStencilState(depth_state_,stencil_reference_);
  release();context_=nullptr;
 }
private:
 template<class T> static void release_one(T*& value) noexcept {
  if(value)value->Release();value=nullptr;
 }
 void release() noexcept {
  release_one(input_layout_);release_one(vertex_buffer_);release_one(index_buffer_);
  release_one(vertex_shader_);release_one(pixel_shader_);release_one(geometry_shader_);
  release_one(hull_shader_);release_one(domain_shader_);draw_constants_.release();
  for(auto*& value:pixel_resources_)release_one(value);
  release_one(pixel_sampler_);release_one(second_sampler_);release_one(second_constant_);release_one(rasterizer_);
  for(auto*& value:render_targets_)release_one(value);
  release_one(depth_view_);release_one(blend_state_);release_one(depth_state_);
 }
 ID3D11DeviceContext* context_=nullptr;
 ID3D11InputLayout* input_layout_=nullptr;D3D11_PRIMITIVE_TOPOLOGY topology_=D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
 ID3D11Buffer* vertex_buffer_=nullptr;UINT vertex_stride_=0,vertex_offset_=0;
 ID3D11Buffer* index_buffer_=nullptr;DXGI_FORMAT index_format_=DXGI_FORMAT_UNKNOWN;UINT index_offset_=0;
 ID3D11VertexShader* vertex_shader_=nullptr;ID3D11PixelShader* pixel_shader_=nullptr;
 ID3D11GeometryShader* geometry_shader_=nullptr;ID3D11HullShader* hull_shader_=nullptr;ID3D11DomainShader* domain_shader_=nullptr;
 owned::SavedConstantSlot draw_constants_;
  ID3D11SamplerState* second_sampler_=nullptr;ID3D11Buffer* second_constant_=nullptr;
 ID3D11ShaderResourceView* pixel_resources_[3]{};ID3D11SamplerState* pixel_sampler_=nullptr;
 std::array<D3D11_VIEWPORT,D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> viewports_{};UINT viewport_count_=0;
 std::array<D3D11_RECT,D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> scissors_{};UINT scissor_count_=0;
 ID3D11RasterizerState* rasterizer_=nullptr;
 std::array<ID3D11RenderTargetView*,D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> render_targets_{};
 ID3D11DepthStencilView* depth_view_=nullptr;ID3D11BlendState* blend_state_=nullptr;
 float blend_factor_[4]{};UINT sample_mask_=0xffffffffu;ID3D11DepthStencilState* depth_state_=nullptr;UINT stencil_reference_=0;
};

void identity(D3DMATRIX& m){std::memset(&m,0,sizeof(m));m._11=m._22=m._33=m._44=1;}
D3D11_TEXTURE_ADDRESS_MODE address(DWORD a){return a==D3DTADDRESS_CLAMP?D3D11_TEXTURE_ADDRESS_CLAMP:a==D3DTADDRESS_MIRROR?D3D11_TEXTURE_ADDRESS_MIRROR:D3D11_TEXTURE_ADDRESS_WRAP;}
bool perf_enabled(){static const bool enabled=[](){char value[8]{};return GetEnvironmentVariableA("EI_RENDERER_PERF_LOG",value,sizeof(value))&&value[0]=='1';}();return enabled;}
bool placed_hitch_profile_enabled(){static const bool enabled=[](){char value[8]{};return GetEnvironmentVariableA("EI_RENDERER_OUTER_FRAME_GAP_PROFILE",value,sizeof(value))&&value[0]=='1';}();return enabled;}
std::int64_t perf_now(){LARGE_INTEGER value{};QueryPerformanceCounter(&value);return value.QuadPart;}
std::int64_t perf_frequency(){static const std::int64_t frequency=[](){LARGE_INTEGER value{};QueryPerformanceFrequency(&value);return value.QuadPart;}();return frequency;}
struct ColorDiagnosticTicks {
 std::int64_t* total=nullptr;std::int64_t* nested=nullptr;std::int64_t* extra=nullptr;
 std::int64_t begin=0;
 bool* marker=nullptr;bool previous=false;
 ColorDiagnosticTicks(std::int64_t* value,std::int64_t* child=nullptr,bool* active=nullptr,
  std::int64_t* other=nullptr) noexcept
  :total(value),nested(child),extra(other),begin(value?perf_now():0),
   marker(active),previous(active?*active:false){
  if(marker&&total)*marker=true;
 }
 ~ColorDiagnosticTicks() noexcept {
  if(marker&&total)*marker=previous;
  if(!total)return;
  const auto end=perf_now();if(end<begin)return;
  *total+=end-begin;if(nested)*nested+=end-begin;if(extra)*extra+=end-begin;
 }
};
void report_placed_hitch_lifecycle(const char* event,const char* detail) noexcept {
 bridge_report_event(event,detail);
 char message[1024]{};
 std::snprintf(message,sizeof(message),"EID3D11 %s: %s\n",
  event?event:"outer_frame_gap_profile",detail?detail:"");
 OutputDebugStringA(message);
}
struct PerfScope{std::int64_t* total{};std::int64_t start{};explicit PerfScope(std::int64_t* value):total(perf_enabled()?value:nullptr),start(total?perf_now():0){}~PerfScope(){if(total)*total+=perf_now()-start;}};
constexpr UINT kNativeDispatchDimensionLimit=65535u;
UINT grow_capacity(UINT required,UINT minimum,UINT maximum);

bool align_up_checked(UINT value,UINT alignment,UINT& result) noexcept{
 const std::uint64_t aligned=(static_cast<std::uint64_t>(value)+alignment-1u)&
  ~static_cast<std::uint64_t>(alignment-1u);
 if(aligned>UINT_MAX)return false;
 result=static_cast<UINT>(aligned);return true;
}

bool native_dispatch_dimensions(UINT count,UINT& width,UINT& height) noexcept{
 width=height=0;if(!count)return false;
 width=std::min(count,kNativeDispatchDimensionLimit);
 height=static_cast<UINT>((static_cast<std::uint64_t>(count)+
  kNativeDispatchDimensionLimit-1u)/kNativeDispatchDimensionLimit);
 return height<=kNativeDispatchDimensionLimit;
}

UINT demand_growth_capacity(UINT required,UINT current,UINT maximum) noexcept{
 if(!required||required>maximum)return 0;
 const std::uint64_t doubled=current?
  std::min<std::uint64_t>(maximum,static_cast<std::uint64_t>(current)*2u):required;
 const UINT minimum=static_cast<UINT>(std::max<std::uint64_t>(required,doubled));
 return grow_capacity(required,minimum,maximum);
}
UINT grow_capacity(UINT required,UINT minimum,UINT maximum){
 std::uint64_t result=std::max(required,minimum);result--;result|=result>>1;result|=result>>2;result|=result>>4;result|=result>>8;result|=result>>16;result++;return static_cast<UINT>(std::min<std::uint64_t>(result,maximum));
}

std::uint8_t expand_channel(std::uint32_t value,std::uint32_t mask,std::uint8_t absent){
 if(!mask)return absent;unsigned long shift=0;_BitScanForward(&shift,mask);const std::uint32_t maximum=mask>>shift;return static_cast<std::uint8_t>(((value&mask)>>shift)*255u/maximum);
}
std::uint32_t pack_channel(std::uint8_t value,std::uint32_t mask){
 // One bit scan, not a loop per bit: surface readbacks call this per pixel
 // and channel (a 290-540 ms hitch per 16-bit readback in a tester's log).
 if(!mask)return 0;unsigned long shift=0;_BitScanForward(&shift,mask);const std::uint32_t maximum=mask>>shift;return ((std::uint32_t(value)*maximum+127u)/255u<<shift)&mask;
}
// pack_channel of every byte value per channel, built once per readback: a
// BGRA texel then packs with four lookups (same result) instead of four bit
// scans and divisions plus a CRT copy call per pixel.
struct PackedChannelTables{
 std::array<std::uint32_t,256> r{},g{},b{},a{};
 explicit PackedChannelTables(const DDPIXELFORMAT& format){
  for(std::uint32_t v=0;v<256;++v){const auto byte=static_cast<std::uint8_t>(v);
   r[v]=pack_channel(byte,format.dwRBitMask);g[v]=pack_channel(byte,format.dwGBitMask);
   b[v]=pack_channel(byte,format.dwBBitMask);a[v]=pack_channel(byte,format.dwRGBAlphaBitMask);}
 }
 std::uint32_t pack(std::uint32_t bgra) const noexcept{
  return r[(bgra>>16)&255u]|g[(bgra>>8)&255u]|b[bgra&255u]|a[bgra>>24];
 }
};
// Writes the low `bytes` (1-4) bytes of a packed texel for each of `count` BGRA texels.
void pack_bgra_row(std::uint8_t* destination,const std::uint32_t* source,UINT count,UINT bytes,
                   const PackedChannelTables& tables) noexcept{
 if(bytes==4){for(UINT x=0;x<count;++x){const std::uint32_t raw=tables.pack(source[x]);std::memcpy(destination+(size_t)x*4,&raw,4);}return;}
 if(bytes==2){for(UINT x=0;x<count;++x){const auto raw=static_cast<std::uint16_t>(tables.pack(source[x]));std::memcpy(destination+(size_t)x*2,&raw,2);}return;}
 for(UINT x=0;x<count;++x){const std::uint32_t raw=tables.pack(source[x]);std::memcpy(destination+(size_t)x*bytes,&raw,bytes);}
}
}  // namespace

// One set being built: its prebuilt program files and the shader objects the
// workers create (the driver translates the bytecode in CreatePixelShader).
struct Renderer::NativeSpecialisationJob{
 std::uint32_t key=0;
 std::vector<std::wstring> files; // the prebuilt programs (native_spec_files_)
 std::vector<ShaderKey> keys;
 std::vector<ComPtr<ID3D11PixelShader>> shaders;
 std::atomic<std::size_t> finished{0};
 std::atomic<unsigned> cached{0},compiled{0},failed{0};
 std::int64_t begin=0;
};

}  // namespace owned
