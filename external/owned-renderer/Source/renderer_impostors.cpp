// Crown impostors (static_lod.h): the atlas of baked cut-out decoration
// figures and its bake. Each figure and texture is drawn once into one atlas
// slice, kImpostorFrames squared orthographic views over the upper hemisphere,
// while its neighbour registers (normally during the load hold). The native
// shader draws a far crown as one camera-facing quad that samples it.
#include "renderer.h"
#include "neighbour_maps.h"
#include "static_lod.h"
#include "engine_bridge.h"
#include <algorithm>

#include <d3dcompiler.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace owned {
namespace {

const std::string& impostor_bake_source() {
    static const std::string source = std::string(kImpostorFrameHlsl) + R"(
struct ImpostorBakeVertex{float3 position;float3 normal;float2 uv;};
StructuredBuffer<ImpostorBakeVertex> bakeVertices:register(t1);
Texture2D bakeTexture:register(t0);
SamplerState bakeSampler:register(s0);
cbuffer ImpostorBake:register(b0){float4 bakeSphere;uint4 bakeFrame;};
struct ImpostorBakeOutput{float4 p:SV_Position;float3 n:NORMAL0;float2 uv:TEXCOORD0;};
// The frame's orthographic view along -d over the mean shape's bounding
// sphere; nearer the viewer is nearer in depth.
ImpostorBakeOutput impostorBakeVS(uint id:SV_VertexID){
 ImpostorBakeVertex v=bakeVertices[id];
 float3 d=nativeImpostorFrameDirection(bakeFrame.xy);float3 r,u;nativeImpostorBasis(d,r,u);
 float3 q=(v.position-bakeSphere.xyz)/bakeSphere.w;
 ImpostorBakeOutput o;
 o.p=float4(dot(q,r)*kImpostorMargin,dot(q,u)*kImpostorMargin,saturate(.5-.5*dot(q,d)),1);
 o.n=v.normal;o.uv=v.uv;return o;
}
struct ImpostorBakeTargets{float4 albedo:SV_Target0;float4 normal:SV_Target1;};
// The neighbour figures' own alpha test (texture alpha above 200/255,
// renderer_neighbour_maps.cpp); covered texels hold the texture colour and
// the object-space normal, empty ones stay zero (premultiplied by coverage
// through the mips).
ImpostorBakeTargets impostorBakePS(ImpostorBakeOutput i){
 float4 t=bakeTexture.Sample(bakeSampler,i.uv);
 if(t.a<=200.0/255.0)discard;
 float3 n=dot(i.n,i.n)>1e-12?normalize(i.n):float3(0,0,1);
 ImpostorBakeTargets o;o.albedo=float4(t.rgb,1);o.normal=float4(n*.5+.5,1);return o;
}
)";
    return source;
}

struct ImpostorBakeConstants {
    float sphere[4];
    std::uint32_t frame[4];
};

std::string impostor_key(const FigureCpuAsset* figure, std::uint64_t texture) {
    return figure->identity.key() + "|" + std::to_string(texture);
}

// Saves and restores the pipeline state the bake changes (it runs inside the
// world pass, between neighbour uploads).
class ImpostorStateGuard {
public:
    explicit ImpostorStateGuard(ID3D11DeviceContext* context) noexcept : context_(context) {
        context_->IAGetInputLayout(&layout_);
        context_->IAGetPrimitiveTopology(&topology_);
        context_->IAGetIndexBuffer(&index_, &index_format_, &index_offset_);
        context_->VSGetShader(&vs_, nullptr, nullptr);
        context_->PSGetShader(&ps_, nullptr, nullptr);
        context_->HSGetShader(&hs_, nullptr, nullptr);
        context_->DSGetShader(&ds_, nullptr, nullptr);
        context_->GSGetShader(&gs_, nullptr, nullptr);
        constants_.save(context_, 0); // with its ring range (constant_ring.h)
        context_->VSGetShaderResources(1, 1, &vs_resource_);
        context_->PSGetShaderResources(0, 1, &ps_resource_);
        context_->PSGetShaderResources(44, 2, ps_atlas_);
        context_->PSGetSamplers(0, 1, &sampler_);
        viewport_count_ = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        context_->RSGetViewports(&viewport_count_, viewports_);
        context_->RSGetState(&raster_);
        context_->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, targets_, &depth_);
        context_->OMGetBlendState(&blend_, blend_factor_, &sample_mask_);
        context_->OMGetDepthStencilState(&depth_state_, &stencil_);
    }
    ~ImpostorStateGuard() {
        ID3D11RenderTargetView* none[2]{};
        context_->OMSetRenderTargets(2, none, nullptr);
        context_->IASetInputLayout(layout_);
        context_->IASetPrimitiveTopology(topology_);
        context_->IASetIndexBuffer(index_, index_format_, index_offset_);
        context_->VSSetShader(vs_, nullptr, 0);
        context_->PSSetShader(ps_, nullptr, 0);
        context_->HSSetShader(hs_, nullptr, 0);
        context_->DSSetShader(ds_, nullptr, 0);
        context_->GSSetShader(gs_, nullptr, 0);
        constants_.restore(context_, 0);
        constants_.release();
        context_->VSSetShaderResources(1, 1, &vs_resource_);
        context_->PSSetShaderResources(0, 1, &ps_resource_);
        context_->PSSetShaderResources(44, 2, ps_atlas_);
        context_->PSSetSamplers(0, 1, &sampler_);
        context_->RSSetViewports(viewport_count_, viewports_);
        context_->RSSetState(raster_);
        context_->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, targets_, depth_);
        context_->OMSetBlendState(blend_, blend_factor_, sample_mask_);
        context_->OMSetDepthStencilState(depth_state_, stencil_);
        IUnknown* held[] = {layout_, index_, vs_, ps_, hs_, ds_, gs_, vs_resource_,
                            ps_resource_, ps_atlas_[0], ps_atlas_[1], sampler_, raster_, depth_, blend_, depth_state_};
        for (IUnknown* item : held) if (item) item->Release();
        for (ID3D11RenderTargetView* target : targets_) if (target) target->Release();
    }
    ImpostorStateGuard(const ImpostorStateGuard&) = delete;
    ImpostorStateGuard& operator=(const ImpostorStateGuard&) = delete;
private:
    ID3D11DeviceContext* context_;
    ID3D11InputLayout* layout_ = nullptr;
    D3D11_PRIMITIVE_TOPOLOGY topology_ = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    ID3D11Buffer* index_ = nullptr;
    DXGI_FORMAT index_format_ = DXGI_FORMAT_UNKNOWN;
    UINT index_offset_ = 0;
    ID3D11VertexShader* vs_ = nullptr;
    ID3D11PixelShader* ps_ = nullptr;
    ID3D11HullShader* hs_ = nullptr;
    ID3D11DomainShader* ds_ = nullptr;
    ID3D11GeometryShader* gs_ = nullptr;
    owned::SavedConstantSlot constants_;
    ID3D11ShaderResourceView* vs_resource_ = nullptr;
    ID3D11ShaderResourceView* ps_resource_ = nullptr;
    ID3D11ShaderResourceView* ps_atlas_[2]{};
    ID3D11SamplerState* sampler_ = nullptr;
    UINT viewport_count_ = 0;
    D3D11_VIEWPORT viewports_[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
    ID3D11RasterizerState* raster_ = nullptr;
    ID3D11RenderTargetView* targets_[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
    ID3D11DepthStencilView* depth_ = nullptr;
    ID3D11BlendState* blend_ = nullptr;
    FLOAT blend_factor_[4]{};
    UINT sample_mask_ = 0;
    ID3D11DepthStencilState* depth_state_ = nullptr;
    UINT stencil_ = 0;
};

} // namespace

void append_impostor_shader_programs(std::vector<ShaderProgram>& out) {
    const std::string& source = impostor_bake_source();
    out.push_back({source, "impostor_bake", nullptr, "impostorBakeVS", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3});
    out.push_back({source, "impostor_bake", nullptr, "impostorBakePS", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3});
}

void Renderer::impostor_failure(const char* reason) noexcept {
    if (impostors_.failed) return;
    impostors_.failed = true;
    native_impostor_pixels_ = 0;
    std::string text = std::string(reason) + "; crowns draw their own triangles";
    OutputDebugStringA(("EID3D11 impostors: " + text + "\n").c_str());
    bridge_append_probe_event("impostors_omitted", text.c_str());
}

// Grows the atlas to hold at least this many slices (keeping the baked ones).
HRESULT Renderer::ensure_impostor_atlas(UINT slices) noexcept try {
    if (slices <= impostors_.capacity && impostors_.albedo) return S_OK;
    if (slices > D3D11_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION) return E_OUTOFMEMORY;
    const UINT capacity = (std::min)(UINT(D3D11_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION),
        (std::max)({slices, impostors_.capacity * 2u, 16u}));
    const UINT side = kImpostorFrames * kImpostorFrameTexels;
    D3D11_TEXTURE2D_DESC description{};
    description.Width = description.Height = side;
    description.MipLevels = kImpostorMips;
    description.ArraySize = capacity;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    description.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    ComPtr<ID3D11Texture2D> albedo, normal;
    ComPtr<ID3D11ShaderResourceView> albedo_srv, normal_srv;
    HRESULT hr = device_->CreateTexture2D(&description, nullptr, &albedo);
    if (SUCCEEDED(hr)) hr = device_->CreateTexture2D(&description, nullptr, &normal);
    if (SUCCEEDED(hr)) hr = device_->CreateShaderResourceView(albedo.Get(), nullptr, &albedo_srv);
    if (SUCCEEDED(hr)) hr = device_->CreateShaderResourceView(normal.Get(), nullptr, &normal_srv);
    if (FAILED(hr)) return hr;
    if (impostors_.albedo) {
        for (UINT slice = 0; slice < impostors_.count; ++slice)
            for (UINT mip = 0; mip < kImpostorMips; ++mip) {
                const UINT from = D3D11CalcSubresource(mip, slice, kImpostorMips);
                context_->CopySubresourceRegion(albedo.Get(), from, 0, 0, 0, impostors_.albedo.Get(), from, nullptr);
                context_->CopySubresourceRegion(normal.Get(), from, 0, 0, 0, impostors_.normal.Get(), from, nullptr);
            }
    }
    if (!impostors_.depth) {
        D3D11_TEXTURE2D_DESC depth{};
        depth.Width = depth.Height = side;
        depth.MipLevels = depth.ArraySize = 1;
        depth.Format = DXGI_FORMAT_D32_FLOAT;
        depth.SampleDesc.Count = 1;
        depth.Usage = D3D11_USAGE_DEFAULT;
        depth.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        if (FAILED(hr = device_->CreateTexture2D(&depth, nullptr, &impostors_.depth)) ||
            FAILED(hr = device_->CreateDepthStencilView(impostors_.depth.Get(), nullptr, &impostors_.depth_view)))
            return hr;
    }
    impostors_.albedo = std::move(albedo);
    impostors_.normal = std::move(normal);
    impostors_.albedo_srv = std::move(albedo_srv);
    impostors_.normal_srv = std::move(normal_srv);
    impostors_.capacity = capacity;
    return S_OK;
} catch (...) {
    return E_OUTOFMEMORY;
}

HRESULT Renderer::bake_neighbour_impostors(const NeighbourMapUpload& upload) noexcept try {
    if (impostors_.failed || upload.impostor_sources.empty() || !device_ || !context_) return S_OK;
    const auto start = std::chrono::steady_clock::now();
    struct Pending {
        std::string key;
        const NeighbourMapUpload::ImpostorSource* source = nullptr;
        ComPtr<ID3D11ShaderResourceView> texture;
    };
    std::vector<Pending> pending;
    for (const auto& [key, source] : upload.impostor_sources) {
        if (source.source.indices.empty() || !source.node || !source.node->texture) continue;
        std::string name = impostor_key(key.first, key.second);
        if (impostors_.slices.count(name)) continue;
        // The decoration's own texture (the cache entry its records share).
        const auto& node = *source.node;
        NativeOwnedTextureView view{};
        view.identity = node.texture_identity; view.generation = 1;
        view.resource = &node.texture->identity; view.value_source = 1;
        view.format = node.texture_format; view.mips = node.texture_mips.data();
        view.mip_count = static_cast<std::uint32_t>(node.texture_mips.size());
        Pending item{std::move(name), &source, nullptr};
        std::uint64_t backing = 0;
        if (FAILED(get_native_owned_texture(view, item.texture, backing, nullptr)) || !item.texture) continue;
        pending.push_back(std::move(item));
    }
    if (pending.empty()) return S_OK;
    HRESULT hr = ensure_impostor_atlas(impostors_.count + static_cast<UINT>(pending.size()));
    if (FAILED(hr)) { impostor_failure("impostor atlas could not be allocated"); return S_OK; }
    if (!impostors_.vs) {
        std::vector<ShaderProgram> programs;
        append_impostor_shader_programs(programs);
        ComPtr<ID3DBlob> vs, ps, errors;
        if (FAILED(compile_shader(programs[0], &vs, &errors)) || FAILED(compile_shader(programs[1], &ps, &errors)) ||
            FAILED(create_shader(device_.Get(),vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &impostors_.vs)) ||
            FAILED(create_shader(device_.Get(),ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &impostors_.ps))) {
            impostor_failure("impostor bake shaders failed to compile or create");
            return S_OK;
        }
        D3D11_BUFFER_DESC constants{};
        constants.ByteWidth = sizeof(ImpostorBakeConstants);
        constants.Usage = D3D11_USAGE_DYNAMIC;
        constants.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        constants.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        D3D11_RASTERIZER_DESC raster{};
        raster.FillMode = D3D11_FILL_SOLID; raster.CullMode = D3D11_CULL_NONE; raster.DepthClipEnable = TRUE;
        D3D11_DEPTH_STENCIL_DESC depth{};
        depth.DepthEnable = TRUE; depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; depth.DepthFunc = D3D11_COMPARISON_LESS;
        D3D11_BLEND_DESC blend{};
        blend.RenderTarget[0].RenderTargetWriteMask = blend.RenderTarget[1].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        blend.IndependentBlendEnable = TRUE;
        D3D11_SAMPLER_DESC sampler{};
        sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        sampler.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(device_->CreateBuffer(&constants, nullptr, &impostors_.constants)) ||
            FAILED(device_->CreateRasterizerState(&raster, &impostors_.raster)) ||
            FAILED(device_->CreateDepthStencilState(&depth, &impostors_.depth_state)) ||
            FAILED(device_->CreateBlendState(&blend, &impostors_.blend)) ||
            FAILED(device_->CreateSamplerState(&sampler, &impostors_.sampler))) {
            impostors_.vs.Reset();
            impostor_failure("impostor bake states could not be created");
            return S_OK;
        }
    }
    {
        ImpostorStateGuard guard(context_.Get());
        ID3D11ShaderResourceView* no_atlas[2]{};
        context_->PSSetShaderResources(44, 2, no_atlas);
        context_->IASetInputLayout(nullptr);
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->VSSetShader(impostors_.vs.Get(), nullptr, 0);
        context_->PSSetShader(impostors_.ps.Get(), nullptr, 0);
        context_->HSSetShader(nullptr, nullptr, 0);
        context_->DSSetShader(nullptr, nullptr, 0);
        context_->GSSetShader(nullptr, nullptr, 0);
        context_->VSSetConstantBuffers(0, 1, impostors_.constants.GetAddressOf());
        context_->PSSetConstantBuffers(0, 1, impostors_.constants.GetAddressOf());
        context_->PSSetSamplers(0, 1, impostors_.sampler.GetAddressOf());
        context_->RSSetState(impostors_.raster.Get());
        context_->OMSetBlendState(impostors_.blend.Get(), nullptr, 0xffffffffu);
        context_->OMSetDepthStencilState(impostors_.depth_state.Get(), 0);
        for (auto& item : pending) {
            const auto& source = item.source->source;
            D3D11_BUFFER_DESC vertices{};
            vertices.ByteWidth = static_cast<UINT>(source.vertices.size() * sizeof(float));
            vertices.Usage = D3D11_USAGE_IMMUTABLE;
            vertices.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            vertices.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            vertices.StructureByteStride = 8 * sizeof(float);
            D3D11_SUBRESOURCE_DATA vertex_data{source.vertices.data()};
            D3D11_BUFFER_DESC indices{};
            indices.ByteWidth = static_cast<UINT>(source.indices.size() * sizeof(std::uint32_t));
            indices.Usage = D3D11_USAGE_IMMUTABLE;
            indices.BindFlags = D3D11_BIND_INDEX_BUFFER;
            D3D11_SUBRESOURCE_DATA index_data{source.indices.data()};
            ComPtr<ID3D11Buffer> vertex_buffer, index_buffer;
            ComPtr<ID3D11ShaderResourceView> vertex_view;
            const UINT slice = impostors_.count;
            D3D11_RENDER_TARGET_VIEW_DESC target{};
            target.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            target.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
            target.Texture2DArray.FirstArraySlice = slice;
            target.Texture2DArray.ArraySize = 1;
            ComPtr<ID3D11RenderTargetView> albedo_target, normal_target;
            if (FAILED(device_->CreateBuffer(&vertices, &vertex_data, &vertex_buffer)) ||
                FAILED(device_->CreateShaderResourceView(vertex_buffer.Get(), nullptr, &vertex_view)) ||
                FAILED(device_->CreateBuffer(&indices, &index_data, &index_buffer)) ||
                FAILED(device_->CreateRenderTargetView(impostors_.albedo.Get(), &target, &albedo_target)) ||
                FAILED(device_->CreateRenderTargetView(impostors_.normal.Get(), &target, &normal_target)))
                continue;
            const float zero[4]{};
            context_->ClearRenderTargetView(albedo_target.Get(), zero);
            context_->ClearRenderTargetView(normal_target.Get(), zero);
            context_->ClearDepthStencilView(impostors_.depth_view.Get(), D3D11_CLEAR_DEPTH, 1.f, 0);
            ID3D11RenderTargetView* targets[2] = {albedo_target.Get(), normal_target.Get()};
            context_->OMSetRenderTargets(2, targets, impostors_.depth_view.Get());
            context_->VSSetShaderResources(1, 1, vertex_view.GetAddressOf());
            context_->PSSetShaderResources(0, 1, item.texture.GetAddressOf());
            context_->IASetIndexBuffer(index_buffer.Get(), DXGI_FORMAT_R32_UINT, 0);
            for (UINT frame = 0; frame < kImpostorFrames * kImpostorFrames; ++frame) {
                const UINT x = frame % kImpostorFrames, y = frame / kImpostorFrames;
                const D3D11_VIEWPORT viewport{float(x * kImpostorFrameTexels), float(y * kImpostorFrameTexels),
                                              float(kImpostorFrameTexels), float(kImpostorFrameTexels), 0.f, 1.f};
                context_->RSSetViewports(1, &viewport);
                D3D11_MAPPED_SUBRESOURCE mapped{};
                if (FAILED(context_->Map(impostors_.constants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) continue;
                ImpostorBakeConstants values{{source.sphere[0], source.sphere[1], source.sphere[2], source.sphere[3]},
                                             {x, y, 0, 0}};
                std::memcpy(mapped.pData, &values, sizeof(values));
                context_->Unmap(impostors_.constants.Get(), 0);
                context_->DrawIndexed(static_cast<UINT>(source.indices.size()), 0, 0);
            }
            impostors_.slices.emplace(item.key, slice);
            ++impostors_.count;
            ++impostors_.baked;
        }
        ID3D11RenderTargetView* none[2]{};
        context_->OMSetRenderTargets(2, none, nullptr);
        context_->GenerateMips(impostors_.albedo_srv.Get());
        context_->GenerateMips(impostors_.normal_srv.Get());
    }
    impostors_.bake_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return S_OK;
} catch (...) {
    impostor_failure("impostor bake failed");
    return S_OK;
}

std::uint32_t Renderer::neighbour_impostor_slice(const FigureCpuAsset* figure, std::uint64_t texture) const {
    const auto found = impostors_.slices.find(impostor_key(figure, texture));
    return found == impostors_.slices.end() ? 0u : found->second + 1u;
}

} // namespace owned
