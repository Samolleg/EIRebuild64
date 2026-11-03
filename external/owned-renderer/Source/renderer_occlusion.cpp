// Hierarchical depth ("Hi-Z") for GPU occlusion culling. Built from the
// scene depth once opaque terrain is drawn (before objects, before water),
// so terrain occludes objects in the same frame. Standard depth (0 near,
// 1 far): each texel keeps the FARTHEST depth of its footprint, so a box is
// rejected only when every covered pixel is nearer than the box's nearest
// point -- the pyramid can only under-cull.
#include "renderer.h"

#include <d3dcompiler.h>

namespace owned {
namespace {
constexpr char kHiZShader[] = R"(
Texture2D<float> source:register(t0);
RWTexture2D<float> destination:register(u0);
[numthreads(8,8,1)]void hizCopyCS(uint3 id:SV_DispatchThreadID){
 uint2 size;destination.GetDimensions(size.x,size.y);if(any(id.xy>=size))return;
 uint2 full;source.GetDimensions(full.x,full.y);
 int2 a=int2(id.xy)*2;int2 b=min(a+1,int2(full)-1);
 destination[id.xy]=max(max(source.Load(int3(a,0)),source.Load(int3(b.x,a.y,0))),
                        max(source.Load(int3(a.x,b.y,0)),source.Load(int3(b,0))));
}
[numthreads(8,8,1)]void hizReduceCS(uint3 id:SV_DispatchThreadID){
 uint2 size;destination.GetDimensions(size.x,size.y);if(any(id.xy>=size))return;
 uint2 parent;source.GetDimensions(parent.x,parent.y);
 int2 a=int2(id.xy)*2;int2 b=min(a+1,int2(parent)-1);
 float d=max(max(source.Load(int3(a,0)),source.Load(int3(b.x,a.y,0))),
             max(source.Load(int3(a.x,b.y,0)),source.Load(int3(b,0))));
 // Odd parent dimensions fold their last row/column into the edge texel.
 if(id.x==size.x-1&&(parent.x&1)!=0)d=max(d,max(source.Load(int3(parent.x-1,a.y,0)),source.Load(int3(parent.x-1,b.y,0))));
 if(id.y==size.y-1&&(parent.y&1)!=0)d=max(d,max(source.Load(int3(a.x,parent.y-1,0)),source.Load(int3(b.x,parent.y-1,0))));
 destination[id.xy]=d;
}
)";
}

void append_hiz_shader_programs(std::vector<ShaderProgram>& out) {
    out.push_back({{kHiZShader, sizeof(kHiZShader) - 1}, nullptr, nullptr, "hizCopyCS", "cs_5_0", 0});
    out.push_back({{kHiZShader, sizeof(kHiZShader) - 1}, nullptr, nullptr, "hizReduceCS", "cs_5_0", 0});
}

#if !defined(EID3D11_SHADER_EMBED)
HRESULT Renderer::build_native_hiz() noexcept {
    if (!native_occlusion_enabled_ || native_hiz_failed_ || !device_ || !context_ || !depth_srv_) return S_FALSE;
    HRESULT hr = S_OK;
    if (!native_hiz_copy_cs_ || !native_hiz_reduce_cs_) {
        Microsoft::WRL::ComPtr<ID3DBlob> code, errors;
        std::vector<ShaderProgram> programs;
        append_hiz_shader_programs(programs);
        const auto compile = [&](const ShaderProgram& program, Microsoft::WRL::ComPtr<ID3D11ComputeShader>& shader) {
            code.Reset(); errors.Reset();
            HRESULT result = compile_shader(program, &code, &errors);
            if (SUCCEEDED(result))
                result = create_shader(device_.Get(),code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader);
            return result;
        };
        if (FAILED(hr = compile(programs[0], native_hiz_copy_cs_)) ||
            FAILED(hr = compile(programs[1], native_hiz_reduce_cs_))) {
            native_hiz_failed_ = true;
            return hr;
        }
    }
    const UINT width = (std::max)(1u, width_ / 2), height = (std::max)(1u, height_ / 2);
    if (!native_hiz_ || native_hiz_width_ != width || native_hiz_height_ != height) {
        native_hiz_.Reset(); native_hiz_srv_.Reset(); native_hiz_uav_.clear(); native_hiz_mip_srv_.clear();
        UINT mips = 1;
        for (UINT side = (std::max)(width, height); side > 1; side >>= 1) ++mips;
        D3D11_TEXTURE2D_DESC description{};
        description.Width = width; description.Height = height; description.MipLevels = mips; description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R32_FLOAT; description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        if (FAILED(hr = device_->CreateTexture2D(&description, nullptr, &native_hiz_))) { native_hiz_failed_ = true; return hr; }
        if (FAILED(hr = device_->CreateShaderResourceView(native_hiz_.Get(), nullptr, &native_hiz_srv_))) { native_hiz_failed_ = true; return hr; }
        native_hiz_uav_.resize(mips); native_hiz_mip_srv_.resize(mips);
        for (UINT mip = 0; mip < mips; ++mip) {
            D3D11_UNORDERED_ACCESS_VIEW_DESC uav{}; uav.Format = DXGI_FORMAT_R32_FLOAT;
            uav.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D; uav.Texture2D.MipSlice = mip;
            D3D11_SHADER_RESOURCE_VIEW_DESC srv{}; srv.Format = DXGI_FORMAT_R32_FLOAT;
            srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; srv.Texture2D.MostDetailedMip = mip; srv.Texture2D.MipLevels = 1;
            if (FAILED(hr = device_->CreateUnorderedAccessView(native_hiz_.Get(), &uav, &native_hiz_uav_[mip])) ||
                FAILED(hr = device_->CreateShaderResourceView(native_hiz_.Get(), &srv, &native_hiz_mip_srv_[mip]))) {
                native_hiz_failed_ = true; return hr;
            }
        }
        native_hiz_width_ = width; native_hiz_height_ = height; native_hiz_mips_ = mips;
    }
    // The depth buffer is read, not written, while the pyramid is built.
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    ID3D11ShaderResourceView* no_srv = nullptr; ID3D11UnorderedAccessView* no_uav = nullptr;
    context_->CSSetShaderResources(26, 1, &no_srv);
    ID3D11ShaderResourceView* depth = depth_srv_.Get();
    transition_profile_pass("compute_hiz");
    context_->CSSetShader(native_hiz_copy_cs_.Get(), nullptr, 0);
    context_->CSSetShaderResources(0, 1, &depth);
    context_->CSSetUnorderedAccessViews(0, 1, native_hiz_uav_[0].GetAddressOf(), nullptr);
    context_->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
    context_->CSSetShader(native_hiz_reduce_cs_.Get(), nullptr, 0);
    for (UINT mip = 1; mip < native_hiz_mips_; ++mip) {
        context_->CSSetUnorderedAccessViews(0, 1, &no_uav, nullptr);
        ID3D11ShaderResourceView* parent = native_hiz_mip_srv_[mip - 1].Get();
        context_->CSSetShaderResources(0, 1, &parent);
        context_->CSSetUnorderedAccessViews(0, 1, native_hiz_uav_[mip].GetAddressOf(), nullptr);
        const UINT w = (std::max)(1u, width >> mip), h = (std::max)(1u, height >> mip);
        context_->Dispatch((w + 7) / 8, (h + 7) / 8, 1);
    }
    context_->CSSetUnorderedAccessViews(0, 1, &no_uav, nullptr);
    context_->CSSetShaderResources(0, 1, &no_srv);
    context_->CSSetShader(nullptr, nullptr, 0);
    context_->OMSetRenderTargets(1, rtv_.GetAddressOf(), dsv_.Get());
    native_hiz_frame_ = native_frame_index_;
    native_hiz_clip_ = native_world_clip_;
    native_world_clip_dirty_ = true;   // publish the occlusion parameters
    return S_OK;
}

// The same pyramid on D3D12, at the same point of the frame: hizCopyCS from
// the world depth into level 0, then each level by hizReduceCS through the
// executor's per-level views (generate_mips: t0 -> u0, 8x8 groups, the
// levels' read/write barriers). Not built (pipeline compiling, ring full):
// the frame's build reads no pyramid, as D3D11 without one.
HRESULT Renderer::build_native_hiz_d3d12() noexcept {
    if (!native_occlusion_enabled_ || native_hiz_failed_ || !d3d12_ || !world_effect_targets()) return S_FALSE;
    UINT full_width = 0, full_height = 0, full_mips = 0; DXGI_FORMAT full_format = DXGI_FORMAT_UNKNOWN;
    if (!d3d12_->texture_info(*world_d3d12_, full_width, full_height, full_mips, full_format)) return E_FAIL;
    if (native_hiz12_programs_.empty()) {
        try { append_hiz_shader_programs(native_hiz12_programs_); }
        catch (const std::bad_alloc&) { native_hiz12_programs_.clear(); return E_OUTOFMEMORY; }
    }
    const UINT width = (std::max)(1u, full_width / 2), height = (std::max)(1u, full_height / 2);
    HRESULT hr = S_OK;
    if (!native_hiz12_ || native_hiz_width_ != width || native_hiz_height_ != height) {
        native_hiz12_.reset();
        UINT mips = 1;
        for (UINT side = (std::max)(width, height); side > 1; side >>= 1) ++mips;
        if (FAILED(hr = d3d12_->create_storage_texture(width, height, 0, DXGI_FORMAT_R32_FLOAT, native_hiz12_, mips))) {
            native_hiz_failed_ = true; return hr;
        }
        native_hiz_width_ = width; native_hiz_height_ = height; native_hiz_mips_ = mips;
    }
    transition_profile_pass("compute_hiz");
    d3d12_->clear_slots();
    d3d12_->set_depth_resource(0, world_d3d12_.get());
    d3d12_->set_unordered(0, native_hiz12_.get());
    hr = d3d12_->dispatch(native_hiz12_programs_[0], (width + 7) / 8, (height + 7) / 8, 1);
    d3d12_->clear_slots();
    if (hr == S_OK) hr = d3d12_->generate_mips(*native_hiz12_, native_hiz12_programs_[1]);
    d3d12_->clear_slots();
    if (FAILED(hr)) return hr;
    if (hr != S_OK) { d3d12_->omit("occlusion pyramid (pipeline not ready: no occlusion culling)", false); return hr; }
    native_hiz_frame_ = native_frame_index_;
    native_hiz_clip_ = native_world_clip_;
    native_world_clip_dirty_ = true;   // publish the occlusion parameters
    d3d12_->count("occlusion pyramids");
    return S_OK;
}

bool Renderer::native_occlusion_active() const noexcept {
    return native_occlusion_enabled_ && (native_hiz_ || native_hiz12_) && native_hiz_frame_ == native_frame_index_ &&
        !native_cull_frozen_ && native_world_clip_valid_ && native_hiz_clip_ == native_world_clip_;
}

#endif
} // namespace owned
