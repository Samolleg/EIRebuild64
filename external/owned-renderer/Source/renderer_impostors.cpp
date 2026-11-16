// Crown impostors (static_lod.h): the atlas of baked cut-out decoration
// figures and its bake. Each figure and texture is drawn once into one atlas
// slice, kImpostorFrames squared orthographic views over the upper hemisphere,
// while its neighbour registers (normally during the load hold). The native
// shader draws a far crown as one camera-facing quad that samples it.
#include "renderer.h"
#include "neighbour_maps.h"
#include "neighbour_piece.h"
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
#ifdef EID3D12
// D3D12 (Renderer::bake_neighbour_impostors_d3d12): one instanced draw per
// figure over the slice's whole 4x4 target, an instance a frame, the
// triangles read through their indices (t2). Each frame lands on the pixels
// of the D3D11 per-frame viewport: the same NDC-to-cell mapping, and the
// clip distances clip at the cell's edges as the viewport's clip volume did.
StructuredBuffer<uint> bakeIndices:register(t2);
struct ImpostorBakeOutput{float4 p:SV_Position;float3 n:NORMAL0;float2 uv:TEXCOORD0;float4 cell:SV_ClipDistance0;};
ImpostorBakeOutput impostorBakeVS(uint id:SV_VertexID,uint instance:SV_InstanceID){
 ImpostorBakeVertex v=bakeVertices[bakeIndices[id]];
 uint2 frame=uint2(instance%kImpostorFrames,instance/kImpostorFrames);
#else
struct ImpostorBakeOutput{float4 p:SV_Position;float3 n:NORMAL0;float2 uv:TEXCOORD0;};
ImpostorBakeOutput impostorBakeVS(uint id:SV_VertexID){
 ImpostorBakeVertex v=bakeVertices[id];
 uint2 frame=bakeFrame.xy;
#endif
 // The frame's orthographic view along -d over the mean shape's bounding
 // sphere; nearer the viewer is nearer in depth.
 float3 d=nativeImpostorFrameDirection(frame);float3 r,u;nativeImpostorBasis(d,r,u);
 float3 q=(v.position-bakeSphere.xyz)/bakeSphere.w;
 ImpostorBakeOutput o;
 o.p=float4(dot(q,r)*kImpostorMargin,dot(q,u)*kImpostorMargin,saturate(.5-.5*dot(q,d)),1);
#ifdef EID3D12
 o.cell=float4(1-o.p.x,1+o.p.x,1-o.p.y,1+o.p.y);
 o.p.xy=float2((frame.x+.5+.5*o.p.x)*2.0/kImpostorFrames-1,1-(frame.y+.5-.5*o.p.y)*2.0/kImpostorFrames);
#endif
 o.n=v.normal;o.uv=v.uv;return o;
}
struct ImpostorBakeTargets{float4 albedo:SV_Target0;float4 normal:SV_Target1;};
// The neighbour figures' own alpha test (texture alpha above 200/255,
// renderer_neighbour_maps.cpp); covered texels hold the texture colour and
// the object-space normal, empty ones stay zero (premultiplied by coverage
// through the mips).
ImpostorBakeTargets impostorBakePS(ImpostorBakeOutput i){
 // Preserve leaf coverage while baking the small atlas. Filtered mip alpha
 // removes sparse cards before the impostor ever reaches the screen. Keep
 // colour and coverage from the same source sample.
 float4 t=bakeTexture.SampleLevel(bakeSampler,i.uv,0);
 if(t.a<=200.0/255.0)discard;
 float3 n=dot(i.n,i.n)>1e-12?normalize(i.n):float3(0,0,1);
 ImpostorBakeTargets o;o.albedo=float4(t.rgb,1);o.normal=float4(n*.5+.5,1);return o;
}
)";
    return source;
}

#if !defined(EID3D11_SHADER_EMBED)
struct ImpostorBakeConstants {
    float sphere[4];
    std::uint32_t frame[4];
};

std::string impostor_key(const ResourceIdentity& figure, std::uint64_t texture) {
    return figure.key() + "|" + std::to_string(texture);
}

#endif
} // namespace

void append_impostor_shader_programs(std::vector<ShaderProgram>& out) {
    const std::string& source = impostor_bake_source();
    out.push_back({source, "impostor_bake", nullptr, "impostorBakeVS", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3});
    out.push_back({source, "impostor_bake", nullptr, "impostorBakePS", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3});
}

#if !defined(EID3D11_SHADER_EMBED)
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

HRESULT Renderer::bake_neighbour_impostors(const NeighbourPiece& upload) noexcept try {
    if (impostors_.failed || upload.impostors.empty() || !device_ || !context_) return S_OK;
    const auto start = std::chrono::steady_clock::now();
    struct Pending {
        std::string key;
        const NeighbourPieceImpostor* source = nullptr;
        ComPtr<ID3D11ShaderResourceView> texture;
    };
    std::vector<Pending> pending;
    std::vector<NativeOwnedTextureMip> texture_mips;
    for (const auto& source : upload.impostors) {
        if (!source.index_count || !source.vertex_view || !source.indices) continue;
        const auto& texture_source = upload.textures[source.texture];
        std::string name = impostor_key(upload.identities[upload.figures[source.figure].identity], texture_source.view_identity);
        if (impostors_.slices.count(name)) continue;
        // The decoration's own texture (the cache entry its records share).
        const NativeOwnedTextureView view = neighbour_piece_texture_view(texture_source, texture_mips);
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
        // The bake binds every state it reads and restores nothing: each pass
        // after it (native flushes, legacy draws, the world passes) binds its
        // own. It runs inside the world pass, between neighbour uploads.
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
            // Its triangles, created by the stream (NeighbourPieceImpostor).
            const auto& source = *item.source;
            const UINT slice = impostors_.count;
            D3D11_RENDER_TARGET_VIEW_DESC target{};
            target.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            target.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
            target.Texture2DArray.FirstArraySlice = slice;
            target.Texture2DArray.ArraySize = 1;
            ComPtr<ID3D11RenderTargetView> albedo_target, normal_target;
            if (
                FAILED(device_->CreateRenderTargetView(impostors_.albedo.Get(), &target, &albedo_target)) ||
                FAILED(device_->CreateRenderTargetView(impostors_.normal.Get(), &target, &normal_target)))
                continue;
            const float zero[4]{};
            context_->ClearRenderTargetView(albedo_target.Get(), zero);
            context_->ClearRenderTargetView(normal_target.Get(), zero);
            context_->ClearDepthStencilView(impostors_.depth_view.Get(), D3D11_CLEAR_DEPTH, 1.f, 0);
            // Plan record (frame_plan.h): the atlas slice is a view no plan id
            // names yet; the pass binds both of its targets itself.
            (void)plan_pass("impostor_bake", albedo_target.Get(), impostors_.depth_view.Get());
            ID3D11RenderTargetView* targets[2] = {albedo_target.Get(), normal_target.Get()};
            context_->OMSetRenderTargets(2, targets, impostors_.depth_view.Get());
            context_->VSSetShaderResources(1, 1, source.vertex_view.GetAddressOf());
            context_->PSSetShaderResources(0, 1, item.texture.GetAddressOf());
            context_->IASetIndexBuffer(source.indices.Get(), DXGI_FORMAT_R32_UINT, 0);
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
                context_->DrawIndexed(source.index_count, 0, 0);
            }
            impostors_.slices.emplace(item.key, slice);
            ++impostors_.count;
            ++impostors_.baked;
        }
        // Unbind what references the upload, which dies after registration.
        ID3D11RenderTargetView* none[2]{};
        context_->OMSetRenderTargets(2, none, nullptr);
        ID3D11ShaderResourceView* no_view = nullptr;
        context_->VSSetShaderResources(1, 1, &no_view);
        context_->PSSetShaderResources(0, 1, &no_view);
        context_->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
        context_->GenerateMips(impostors_.albedo_srv.Get());
        context_->GenerateMips(impostors_.normal_srv.Get());
    }
    impostors_.bake_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return S_OK;
} catch (...) {
    impostor_failure("impostor bake failed");
    return S_OK;
}

// renderer_backend=d3d12: the atlas as two storage arrays (each slice's mips
// by generate_mips), grown as on D3D11, and the bake targets.
HRESULT Renderer::ensure_impostor_atlas_d3d12(UINT slices) noexcept try {
    if (slices <= impostors_.capacity && impostors_.albedo12) return S_OK;
    if (slices > D3D11_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION) return E_OUTOFMEMORY;
    const UINT capacity = (std::min)(UINT(D3D11_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION),
        (std::max)({slices, impostors_.capacity * 2u, 16u}));
    const UINT side = kImpostorFrames * kImpostorFrameTexels;
    const DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
    if (!d3d12_->storage_supported(format)) return E_NOTIMPL;
    if (!impostors_.bake_albedo12) {
        HRESULT hr = d3d12_->create_target(side, side, impostors_.bake_albedo12, format, DXGI_FORMAT_D32_FLOAT);
        if (SUCCEEDED(hr)) hr = d3d12_->create_target(side, side, impostors_.bake_normal12, format, DXGI_FORMAT_UNKNOWN);
        if (FAILED(hr)) { impostors_.bake_albedo12.reset(); impostors_.bake_normal12.reset(); return hr; }
    }
    D3D12TextureRef albedo, normal;
    // Not zeroed: a slice is named (slices) only once its frames and every
    // mip are written, and growth copies only those slices.
    HRESULT hr = d3d12_->create_storage_texture(side, side, 0, format, albedo, kImpostorMips, capacity, true);
    if (SUCCEEDED(hr)) hr = d3d12_->create_storage_texture(side, side, 0, format, normal, kImpostorMips, capacity, true);
    if (FAILED(hr)) return hr;
    if (impostors_.albedo12)
        for (UINT slice = 0; slice < impostors_.count; ++slice)
            for (UINT mip = 0; mip < kImpostorMips; ++mip) {
                const UINT at = D3D11CalcSubresource(mip, slice, kImpostorMips);
                if (FAILED(hr = d3d12_->copy_texture(*albedo, at, *impostors_.albedo12, at)) ||
                    FAILED(hr = d3d12_->copy_texture(*normal, at, *impostors_.normal12, at)))
                    return hr;
            }
    impostors_.albedo12 = std::move(albedo);
    impostors_.normal12 = std::move(normal);
    impostors_.capacity = capacity;
    return S_OK;
} catch (...) {
    return E_OUTOFMEMORY;
}

// The D3D11 bake's frames on D3D12 (the EID3D12 form of impostorBakeVS): per
// figure one instanced draw of its 16 frames into the bake pair, copied into
// the figure's slice, then that slice's mips. A figure whose bake or mips
// cannot be recorded yet (a pipeline still building on a first-ever start)
// gets no slice: its crowns draw their own triangles, never an empty quad.
HRESULT Renderer::bake_neighbour_impostors_d3d12(const NeighbourPiece& upload) noexcept try {
    if (impostors_.failed || upload.impostors.empty() || !d3d12_) return S_OK;
    const auto start = std::chrono::steady_clock::now();
    struct Pending {
        std::string key;
        const NeighbourPieceImpostor* source = nullptr;
        D3D12TextureRef texture;
        D3D12BufferRef vertices, indices;
    };
    std::vector<Pending> pending;
    std::vector<NativeOwnedTextureMip> texture_mips;
    for (const auto& source : upload.impostors) {
        if (!source.index_count || !source.vertices12 || !source.indices12) continue;
        const auto& texture_source = upload.textures[source.texture];
        std::string name = impostor_key(upload.identities[upload.figures[source.figure].identity], texture_source.view_identity);
        if (impostors_.slices.count(name)) continue;
        // The decoration's own texture (the cache entry its records share).
        const NativeOwnedTextureView view = neighbour_piece_texture_view(texture_source, texture_mips);
        Pending item{std::move(name), &source, nullptr, nullptr, nullptr};
        std::uint64_t backing = 0;
        const HRESULT texture_hr = get_native_owned_texture_d3d12(view, item.texture, backing, nullptr, true);
        if (FAILED(texture_hr) || !item.texture) continue;
        // The stream prepared both buffers; their copies are recorded here.
        const bool finished = SUCCEEDED(d3d12_->finish_buffer(*source.vertices12, 8 * sizeof(float), 0, item.vertices)) &&
            SUCCEEDED(d3d12_->finish_buffer(*source.indices12, sizeof(std::uint32_t), 0, item.indices));
        if (!finished) continue;
        pending.push_back(std::move(item));
    }
    if (pending.empty()) return S_OK;
    HRESULT hr = ensure_impostor_atlas_d3d12(impostors_.count + static_cast<UINT>(pending.size()));
    if (FAILED(hr)) { impostor_failure("impostor atlas could not be allocated"); return S_OK; }
    static const std::vector<ShaderProgram> programs = [] {
        std::vector<ShaderProgram> made;
        append_impostor_shader_programs(made);
        return made;
    }();
    D3D12NativeState state;
    state.raster.FillMode = D3D11_FILL_SOLID; state.raster.CullMode = D3D11_CULL_NONE; state.raster.DepthClipEnable = TRUE;
    state.depth.DepthEnable = TRUE; state.depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; state.depth.DepthFunc = D3D11_COMPARISON_LESS;
    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    D3D12Texture* targets[2] = {impostors_.bake_albedo12.get(), impostors_.bake_normal12.get()};
    const float zero[4]{};
    bool not_ready = false;
    d3d12_->marker("impostor_bake");
    d3d12_->set_targets(targets, 2, impostors_.bake_albedo12.get());
    d3d12_->clear_slots();
    d3d12_->set_sampler(0, sampler);
    // Every figure's frames, each copied into the next slice; the mips of all
    // of them follow in one pass per array.
    const UINT first = impostors_.count;
    std::vector<const Pending*> baked;
    baked.reserve(pending.size());
    for (auto& item : pending) {
        const auto& source = *item.source;
        const UINT slice = first + static_cast<UINT>(baked.size());
        d3d12_->clear_texture(*impostors_.bake_albedo12, zero);
        d3d12_->clear_texture(*impostors_.bake_normal12, zero);
        d3d12_->clear_depth_of(*impostors_.bake_albedo12, 1.f, 0);
        const ImpostorBakeConstants values{{source.sphere[0], source.sphere[1], source.sphere[2], source.sphere[3]}, {0, 0, 0, 0}};
        d3d12_->set_constants(0, &values, sizeof(values));
        d3d12_->set_resource(0, item.texture.get());
        d3d12_->set_resource(1, item.vertices.get());
        d3d12_->set_resource(2, item.indices.get());
        hr = d3d12_->draw_instanced(programs[0], programs[1], state, nullptr, source.index_count,
                                    kImpostorFrames * kImpostorFrames, false);
        if (FAILED(hr)) { impostor_failure("impostor bake draw failed"); break; }
        // Not ready (the pipeline still building, or the frame's rings full):
        // the rest would fare the same, so none of them is baked now.
        if (d3d12_not_done(hr)) { not_ready = true; break; }
        const UINT at = D3D11CalcSubresource(0, slice, kImpostorMips);
        if (FAILED(hr = d3d12_->copy_texture(*impostors_.albedo12, at, *impostors_.bake_albedo12, 0)) ||
            FAILED(hr = d3d12_->copy_texture(*impostors_.normal12, at, *impostors_.bake_normal12, 0))) {
            impostor_failure("impostor bake copy failed");
            break;
        }
        baked.push_back(&item);
    }
    if (!baked.empty() && !impostors_.failed) {
        const UINT count = static_cast<UINT>(baked.size());
        hr = d3d12_->generate_mips(*impostors_.albedo12, d3d12_mip_array_range_program(), first, count);
        if (hr == S_OK) hr = d3d12_->generate_mips(*impostors_.normal12, d3d12_mip_array_range_program(), first, count);
        if (FAILED(hr)) impostor_failure("impostor atlas mips failed");
        else if (hr != S_OK) not_ready = true; // the slices are baked over by the next bake
        else for (UINT index = 0; index < count; ++index) {
            impostors_.slices.emplace(baked[index]->key, first + index);
            ++impostors_.count;
            ++impostors_.baked;
        }
    }
    if (not_ready) d3d12_->omit("neighbour crown impostors (bake pipeline not ready: those crowns draw triangles)", false);
    // Every pass after this binds its own slots; the world targets come back
    // for the rest of the world pass this adoption runs in.
    d3d12_->clear_slots();
    if (world_d3d12_bound_ && world_d3d12_) bind_world_targets_d3d12(world_history_bound12_);
    else d3d12_->set_target(nullptr);
    impostors_.bake_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return S_OK;
} catch (...) {
    impostor_failure("impostor bake failed");
    return S_OK;
}

std::uint32_t Renderer::neighbour_impostor_slice(const ResourceIdentity& figure, std::uint64_t texture) const {
    const auto found = impostors_.slices.find(impostor_key(figure, texture));
    return found == impostors_.slices.end() ? 0u : found->second + 1u;
}

#endif
} // namespace owned
