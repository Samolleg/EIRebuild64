// Crown impostors (static_lod.h): the atlas of baked decoration objects with
// a cut-out part, and its bake. Each object model, all its parts, is drawn once into one atlas
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
cbuffer ImpostorBake:register(b0){float4 bakeSphere;uint4 bakeFrame;float4 bakeColor;float4 bakeAlpha;};
#ifdef EID3D12
// D3D12 (Renderer::bake_neighbour_impostors_d3d12): as D3D11, one draw per
// frame into that frame's viewport cell (bakeFrame), the triangles read
// through their indices (t2) instead of an index buffer.
StructuredBuffer<uint> bakeIndices:register(t2);
#endif
struct ImpostorBakeOutput{float4 p:SV_Position;float3 n:NORMAL0;float2 uv:TEXCOORD0;};
ImpostorBakeOutput impostorBakeVS(uint id:SV_VertexID){
#ifdef EID3D12
 ImpostorBakeVertex v=bakeVertices[bakeIndices[id]];
#else
 ImpostorBakeVertex v=bakeVertices[id];
#endif
 uint2 frame=bakeFrame.xy;
 // The frame's orthographic view along -d over the mean shape's bounding
 // sphere; nearer the viewer is nearer in depth.
 float3 d=nativeImpostorFrameDirection(frame);float3 r,u;nativeImpostorBasis(d,r,u);
 float3 q=(v.position-bakeSphere.xyz)/bakeSphere.w;
 ImpostorBakeOutput o;
 o.p=float4(dot(q,r)*kImpostorMargin,dot(q,u)*kImpostorMargin,saturate(.5-.5*dot(q,d)),1);
 o.n=v.normal;o.uv=v.uv;return o;
}
struct ImpostorBakeTargets{float4 albedo:SV_Target0;float4 normal:SV_Target1;};
// The neighbour figures' own alpha test (texture alpha above 200/255,
// renderer_neighbour_maps.cpp); covered texels hold the texture colour and
// the object-space normal, empty ones stay zero (premultiplied by coverage
// through the mips). The normal image's alpha is the texel's depth along the
// frame's view over the bounding sphere (0 its near side, .5 the centre's
// plane, 1 its far side; the nearest surface, by the bake's depth test),
// premultiplied like the rest: nativeImpostorSurface reads it over the
// albedo's coverage.
ImpostorBakeTargets impostorBakePS(ImpostorBakeOutput i){
 float4 t=bakeTexture.Sample(bakeSampler,i.uv);
 if(bakeAlpha.z<0){if(t.a<=bakeAlpha.x)discard;}
 else if(bakeAlpha.z==1&&t.a*bakeAlpha.y<bakeAlpha.x)discard;
 t.rgb*=bakeColor.rgb;
 float3 n=dot(i.n,i.n)>1e-12?normalize(i.n):float3(0,0,1);
 ImpostorBakeTargets o;o.albedo=float4(t.rgb,1);o.normal=float4(n*.5+.5,saturate(i.p.z));return o;
}
// D3D12 atlas levels (bake_neighbour_impostors_d3d12): each smaller level of
// a slice is the 2x2 average of the level above (the images are premultiplied
// by coverage, so the box average is the mip a GenerateMips would make),
// drawn into a render target of that level's size and copied into the slice.
struct ImpostorLevelOutput{float4 p:SV_Position;};
ImpostorLevelOutput impostorLevelVS(uint id:SV_VertexID){
 ImpostorLevelOutput o;float2 t=float2((id<<1)&2,id&2);o.p=float4(t*float2(2,-2)+float2(-1,1),0,1);return o;
}
Texture2D levelAlbedo:register(t3);Texture2D levelNormal:register(t4);
ImpostorBakeTargets impostorLevelPS(ImpostorLevelOutput i){
 int2 p=int2(i.p.xy)*2;ImpostorBakeTargets o;
 o.albedo=(levelAlbedo.Load(int3(p,0))+levelAlbedo.Load(int3(p+int2(1,0),0))+levelAlbedo.Load(int3(p+int2(0,1),0))+levelAlbedo.Load(int3(p+int2(1,1),0)))*.25;
 o.normal=(levelNormal.Load(int3(p,0))+levelNormal.Load(int3(p+int2(1,0),0))+levelNormal.Load(int3(p+int2(0,1),0))+levelNormal.Load(int3(p+int2(1,1),0)))*.25;
 return o;
}
// Slice stamp: texel (0,0) of each slice's level 0 (the empty border)
// holds ((slice & 255) / 255, 90 / 255), as the D3D11 bake's byte; the
// impostor debug view checks it at draw time (slice / 255 saturated past 255).
ImpostorBakeTargets impostorMarkPS(ImpostorLevelOutput i){
 ImpostorBakeTargets o;o.albedo=float4((bakeFrame.z&255u)/255.0,90.0/255.0,0,1);o.normal=o.albedo;return o;
}
)";
    return source;
}

#if !defined(EID3D11_SHADER_EMBED)
struct ImpostorBakeConstants {
    float sphere[4];
    std::uint32_t frame[4];
    float color[4]{1,1,1,1};
    float alpha[4]{200.f/255.f,1,-1,0};
};

#endif
} // namespace

void append_impostor_shader_programs(std::vector<ShaderProgram>& out) {
    const std::string& source = impostor_bake_source();
    out.push_back({source, "impostor_bake", nullptr, "impostorBakeVS", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3});
    out.push_back({source, "impostor_bake", nullptr, "impostorBakePS", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3});
    out.push_back({source, "impostor_bake", nullptr, "impostorLevelVS", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3});
    out.push_back({source, "impostor_bake", nullptr, "impostorLevelPS", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3});
    out.push_back({source, "impostor_bake", nullptr, "impostorMarkPS", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3}); // the slice stamp
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

// A bake part's texture: its piece texture, or (item 5) its game surface.
static NativeOwnedTextureView impostor_part_texture_view(const NeighbourPiece& upload, const NeighbourPieceImpostorPart& part,
    std::vector<NativeOwnedTextureMip>& mips) {
    if (part.surface) {
        NativeOwnedTextureView view{};
        view.identity = part.surface->id; view.generation = part.surface->generation; view.surface = part.surface.get();
        return view;
    }
    return neighbour_piece_texture_view(upload.textures[part.texture], mips);
}

HRESULT Renderer::bake_neighbour_impostors(const NeighbourPiece& upload) noexcept try {
    if (impostors_.failed || upload.impostors.empty() || !device_ || !context_) return S_OK;
    const auto start = std::chrono::steady_clock::now();
    struct Pending {
        const NeighbourPieceImpostor* source = nullptr;
        std::vector<ComPtr<ID3D11ShaderResourceView>> textures; // per part
    };
    std::vector<Pending> pending;
    std::vector<NativeOwnedTextureMip> texture_mips;
    for (const auto& source : upload.impostors) {
        if (source.parts.empty() || impostors_.slices.count(source.key)) continue;
        Pending item{&source, {}};
        bool ready = true;
        for (const auto& part : source.parts) {
            if (!part.index_count || !part.vertex_view || !part.indices) { ready = false; break; }
            // The part's own texture (the cache entry its records share).
            const NativeOwnedTextureView view = impostor_part_texture_view(upload, part, texture_mips);
            ComPtr<ID3D11ShaderResourceView> texture;
            std::uint64_t backing = 0;
            if (FAILED(get_native_owned_texture(view, texture, backing, nullptr)) || !texture) { ready = false; break; }
            item.textures.push_back(std::move(texture));
        }
        if (ready) pending.push_back(std::move(item));
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
            for (UINT frame = 0; frame < kImpostorFrames * kImpostorFrames; ++frame) {
                const UINT x = frame % kImpostorFrames, y = frame / kImpostorFrames;
                const D3D11_VIEWPORT viewport{float(x * kImpostorFrameTexels), float(y * kImpostorFrameTexels),
                                              float(kImpostorFrameTexels), float(kImpostorFrameTexels), 0.f, 1.f};
                context_->RSSetViewports(1, &viewport);
                ImpostorBakeConstants values{{source.sphere[0], source.sphere[1], source.sphere[2], source.sphere[3]},
                                             {x, y, 0, 0}};
                // Every part of the object, each with its own texture.
                for (std::size_t part = 0; part < source.parts.size(); ++part) {
                    std::copy_n(source.parts[part].color.begin(),4,values.color);
                    std::copy_n(source.parts[part].alpha_policy.begin(),4,values.alpha);
                    D3D11_MAPPED_SUBRESOURCE mapped{};
                    if(FAILED(context_->Map(impostors_.constants.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))continue;
                    std::memcpy(mapped.pData,&values,sizeof(values));context_->Unmap(impostors_.constants.Get(),0);
                    context_->VSSetShaderResources(1, 1, source.parts[part].vertex_view.GetAddressOf());
                    context_->PSSetShaderResources(0, 1, item.textures[part].GetAddressOf());
                    context_->IASetIndexBuffer(source.parts[part].indices.Get(), DXGI_FORMAT_R32_UINT, 0);
                    context_->DrawIndexed(source.parts[part].index_count, 0, 0);
                }
            }
            { // the slice stamp into texel (0,0) of level 0
                const std::uint8_t stamp[4] = {std::uint8_t(slice), 90, 0, 255};
                const D3D11_BOX texel{0, 0, 0, 1, 1, 1};
                context_->UpdateSubresource(impostors_.albedo.Get(), D3D11CalcSubresource(0, slice, kImpostorMips), &texel, stamp, 4, 4);
            }
            impostors_.slices.emplace(source.key, slice);
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
    if (!impostors_.bake_albedo12) {
        HRESULT hr = d3d12_->create_target(side, side, impostors_.bake_albedo12, format, DXGI_FORMAT_D32_FLOAT);
        if (SUCCEEDED(hr)) hr = d3d12_->create_target(side, side, impostors_.bake_normal12, format, DXGI_FORMAT_UNKNOWN);
        for (UINT level = 1; level < kImpostorMips && SUCCEEDED(hr); ++level) {
            hr = d3d12_->create_target(side >> level, side >> level, impostors_.bake_level_albedo12[level - 1], format, DXGI_FORMAT_UNKNOWN);
            if (SUCCEEDED(hr)) hr = d3d12_->create_target(side >> level, side >> level, impostors_.bake_level_normal12[level - 1], format, DXGI_FORMAT_UNKNOWN);
        }
        if (FAILED(hr)) {
            impostors_.bake_albedo12.reset(); impostors_.bake_normal12.reset();
            for (auto& level : impostors_.bake_level_albedo12) level.reset();
            for (auto& level : impostors_.bake_level_normal12) level.reset();
            return hr;
        }
    }
    D3D12TextureRef albedo, normal;
    // Plain arrays (zeroed, no unordered access): only ordered copies write them.
    HRESULT hr = d3d12_->create_texture_array(side, side, format, kImpostorMips, capacity, albedo);
    if (SUCCEEDED(hr)) hr = d3d12_->create_texture_array(side, side, format, kImpostorMips, capacity, normal);
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
    struct Part {
        std::array<float,4> color{},alpha_policy{};
        D3D12TextureRef texture;
        D3D12BufferRef vertices, indices;
        UINT index_count = 0;
    };
    struct Pending {
        const NeighbourPieceImpostor* source = nullptr;
        std::vector<Part> parts;
    };
    std::vector<Pending> pending;
    std::vector<NativeOwnedTextureMip> texture_mips;
    for (const auto& source : upload.impostors) {
        if (source.parts.empty() || impostors_.slices.count(source.key)) continue;
        Pending item{&source, {}};
        bool ready = true;
        for (const auto& part : source.parts) {
            if (!part.index_count || !part.vertices12 || !part.indices12) { ready = false; break; }
            // The part's own texture (the cache entry its records share).
            const NativeOwnedTextureView view = impostor_part_texture_view(upload, part, texture_mips);
            Part made;
            made.color=part.color;made.alpha_policy=part.alpha_policy;
            made.index_count = part.index_count;
            std::uint64_t backing = 0;
            const HRESULT texture_hr = get_native_owned_texture_d3d12(view, made.texture, backing, nullptr, true);
            // The stream prepared both buffers; their copies are recorded here.
            if (FAILED(texture_hr) || !made.texture ||
                FAILED(d3d12_->finish_buffer(*part.vertices12, 8 * sizeof(float), 0, made.vertices)) ||
                FAILED(d3d12_->finish_buffer(*part.indices12, sizeof(std::uint32_t), 0, made.indices))) {
                ready = false;
                break;
            }
            item.parts.push_back(std::move(made));
        }
        if (ready) pending.push_back(std::move(item));
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
    D3D12NativeState level_state;  // the level passes: no depth, no blend, every pixel written
    level_state.raster.FillMode = D3D11_FILL_SOLID; level_state.raster.CullMode = D3D11_CULL_NONE; level_state.raster.DepthClipEnable = TRUE;
    level_state.depth.DepthEnable = FALSE; level_state.depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    D3D12Texture* targets[2] = {impostors_.bake_albedo12.get(), impostors_.bake_normal12.get()};
    const float zero[4]{};
    bool not_ready = false;
    // The bake and mip pipelines are waited for when still building (a
    // first start, or a PC whose builder is slower than the first neighbour
    // build): the bake runs once per load, and an omitted bake left those
    // crowns on their triangles until some later rebuild.
    struct WaitForPipelines {
        D3D12Frame& frame;
        explicit WaitForPipelines(D3D12Frame& f) noexcept : frame(f) { frame.set_wait_for_pipelines(true); }
        ~WaitForPipelines() { frame.set_wait_for_pipelines(false); }
    } wait_for_pipelines{*d3d12_};
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
        // As D3D11: per frame, every part with its own texture into the
        // frame's viewport cell.
        hr = S_OK;
        for (UINT frame = 0; frame < kImpostorFrames * kImpostorFrames && hr == S_OK; ++frame) {
            const UINT x = frame % kImpostorFrames, y = frame / kImpostorFrames;
            const RECT cell{LONG(x * kImpostorFrameTexels), LONG(y * kImpostorFrameTexels),
                            LONG((x + 1) * kImpostorFrameTexels), LONG((y + 1) * kImpostorFrameTexels)};
            ImpostorBakeConstants values{{source.sphere[0], source.sphere[1], source.sphere[2], source.sphere[3]}, {x, y, 0, 0}};
            d3d12_->set_viewport(&cell);
            for (const auto& part : item.parts) {
                std::copy_n(part.color.begin(),4,values.color);std::copy_n(part.alpha_policy.begin(),4,values.alpha);
                d3d12_->set_constants(0,&values,sizeof(values));
                d3d12_->set_resource(0, part.texture.get());
                d3d12_->set_resource(1, part.vertices.get());
                d3d12_->set_resource(2, part.indices.get());
                hr = d3d12_->draw(programs[0], programs[1], state, &cell, part.index_count);
                if (hr != S_OK) break;
            }
        }
        if (hr == S_OK) { // the slice stamp into texel (0,0)
            const RECT mark{0, 0, 1, 1};
            const ImpostorBakeConstants stamp{{0, 0, 0, 1}, {0, 0, slice, 0}};
            d3d12_->set_constants(0, &stamp, sizeof(stamp));
            d3d12_->set_viewport(&mark);
            hr = d3d12_->draw(programs[2], programs[4], state, &mark, 3);
        }
        d3d12_->set_viewport(nullptr);
        if (FAILED(hr)) { impostor_failure("impostor bake draw failed"); break; }
        // Not ready (the pipeline still building, or the frame's rings full):
        // the rest would fare the same, so none of them is baked now.
        if (d3d12_not_done(hr)) { not_ready = true; break; }
        UINT at = D3D11CalcSubresource(0, slice, kImpostorMips);
        if (FAILED(hr = d3d12_->copy_texture(*impostors_.albedo12, at, *impostors_.bake_albedo12, 0, true)) ||
            FAILED(hr = d3d12_->copy_texture(*impostors_.normal12, at, *impostors_.bake_normal12, 0, true))) {
            impostor_failure("impostor bake copy failed");
            break;
        }
        // The smaller levels: each the 2x2 average of the one above, drawn
        // into that level's target pair and copied into the slice.
        D3D12Texture* above_albedo = impostors_.bake_albedo12.get();
        D3D12Texture* above_normal = impostors_.bake_normal12.get();
        bool levels_done = true;
        for (UINT level = 1; level < kImpostorMips; ++level) {
            D3D12Texture* level_targets[2] = {impostors_.bake_level_albedo12[level - 1].get(),
                                              impostors_.bake_level_normal12[level - 1].get()};
            d3d12_->set_targets(level_targets, 2, nullptr);
            d3d12_->clear_slots();
            d3d12_->set_resource(3, above_albedo);
            d3d12_->set_resource(4, above_normal);
            hr = d3d12_->draw(programs[2], programs[3], level_state, nullptr, 3);
            d3d12_->clear_slots();
            if (FAILED(hr)) { impostor_failure("impostor level draw failed"); levels_done = false; break; }
            if (d3d12_not_done(hr)) { not_ready = true; levels_done = false; break; }
            at = D3D11CalcSubresource(level, slice, kImpostorMips);
            if (FAILED(hr = d3d12_->copy_texture(*impostors_.albedo12, at, *level_targets[0], 0, true)) ||
                FAILED(hr = d3d12_->copy_texture(*impostors_.normal12, at, *level_targets[1], 0, true))) {
                impostor_failure("impostor level copy failed");
                levels_done = false;
                break;
            }
            above_albedo = level_targets[0];
            above_normal = level_targets[1];
        }
        if (!levels_done) break;
        // The next figure bakes into the full-size pair again.
        d3d12_->set_targets(targets, 2, impostors_.bake_albedo12.get());
        d3d12_->set_sampler(0, sampler);
        baked.push_back(&item);
    }
    // Every level of every baked slice was written by an ordered copy: the
    // slices are named now (an omitted figure gets no slice and is baked over).
    if (!impostors_.failed)
        for (UINT index = 0; index < static_cast<UINT>(baked.size()); ++index) {
            impostors_.slices.emplace(baked[index]->source->key, first + index);
            ++impostors_.count;
            ++impostors_.baked;
        }
    if (not_ready) {
        d3d12_->omit("neighbour crown impostors (bake not recorded: those crowns draw triangles until the rebuild)", false);
        impostors_.rebake = true;
    }
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

std::uint32_t Renderer::neighbour_impostor_slice(const std::string& key) const {
    const auto found = impostors_.slices.find(key);
    return found == impostors_.slices.end() ? 0u : found->second + 1u;
}

#endif
} // namespace owned

namespace owned {
// Impostor atlas dump (harness runs): atlas_sNNN_mM.rgba (RGBA8 rows, 256 >> M square) for every
// slice and level, and atlas_slices.txt (slice key), in the harness folder,
// once per impostor_debug turn-on. D3D12: at most 8 readbacks in flight.
void Renderer::impostor_atlas_dump() noexcept try {
    char directory[MAX_PATH]{};
    if (!GetEnvironmentVariableA("EI_RENDERER_HARNESS_DIR", directory, MAX_PATH)) return;
    const UINT side = kImpostorFrames * kImpostorFrameTexels;
    const auto write = [&](UINT slice, UINT mip, const std::uint8_t* rows, UINT pitch) {
        const UINT size = side >> mip;
        char path[MAX_PATH + 64];
        std::snprintf(path, sizeof(path), "%s\\atlas_s%03u_m%u.rgba", directory, slice, mip);
        if (FILE* f = std::fopen(path, "wb")) {
            for (UINT y = 0; y < size; ++y) std::fwrite(rows + std::size_t(y) * pitch, 1, size * 4, f);
            std::fclose(f);
        }
    };
    if (d3d12_) {
        for (auto it = atlas_dump_tickets_.begin(); it != atlas_dump_tickets_.end();) {
            std::vector<std::uint8_t> bytes;
            const HRESULT hr = d3d12_->poll_readback(it->ticket, bytes);
            if (hr == S_FALSE) { ++it; continue; }
            const UINT size = side >> it->mip, pitch = (std::max)(256u, (size * 4u + 255u) & ~255u);
            if (hr == S_OK && !it->stale && bytes.size() >= std::size_t(pitch) * (size - 1) + size * 4u) write(it->slice, it->mip, bytes.data(), pitch);
            it = atlas_dump_tickets_.erase(it);
        }
        // At most 8 in flight: the executor keeps 32 unpolled readbacks and
        // drops the oldest, which would evict other passes' copies.
        while (!atlas_dump_pending_.empty() && atlas_dump_tickets_.size() < 8 && impostors_.albedo12) {
            const auto next = atlas_dump_pending_.back();
            atlas_dump_pending_.pop_back();
            std::uint64_t ticket = 0;
            if (SUCCEEDED(d3d12_->request_texture_readback(*impostors_.albedo12, D3D11CalcSubresource(next.mip, next.slice, kImpostorMips), ticket, "impostor atlas dump")) && ticket)
                atlas_dump_tickets_.push_back({ticket, next.slice, next.mip});
        }
    }
    const bool turned_on = native_impostor_debug_ && !atlas_dump_debug_;
    atlas_dump_debug_ = native_impostor_debug_;
    if (!turned_on) return;
    {
        char path[MAX_PATH + 64];
        std::snprintf(path, sizeof(path), "%s\\atlas_slices.txt", directory);
        if (FILE* f = std::fopen(path, "w")) {
            for (const auto& [key, slice] : impostors_.slices) std::fprintf(f, "%u %s\n", slice, key.c_str());
            std::fclose(f);
        }
    }
    if (d3d12_) {
        atlas_dump_pending_.clear();
        for (UINT slice = 0; slice < impostors_.count; ++slice)
            for (UINT mip = 0; mip < kImpostorMips; ++mip) atlas_dump_pending_.push_back({0, slice, mip});
        return;
    }
    if (!impostors_.albedo || !device_ || !context_) return;
    for (UINT mip = 0; mip < kImpostorMips; ++mip) {
        D3D11_TEXTURE2D_DESC staging{};
        staging.Width = staging.Height = side >> mip;
        staging.MipLevels = staging.ArraySize = 1;
        staging.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        staging.SampleDesc.Count = 1;
        staging.Usage = D3D11_USAGE_STAGING;
        staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> copy;
        if (FAILED(device_->CreateTexture2D(&staging, nullptr, &copy))) return;
        for (UINT slice = 0; slice < impostors_.count; ++slice) {
            context_->CopySubresourceRegion(copy.Get(), 0, 0, 0, 0, impostors_.albedo.Get(), D3D11CalcSubresource(mip, slice, kImpostorMips), nullptr);
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(context_->Map(copy.Get(), 0, D3D11_MAP_READ, 0, &mapped))) continue;
            write(slice, mip, static_cast<const std::uint8_t*>(mapped.pData), mapped.RowPitch);
            context_->Unmap(copy.Get(), 0);
        }
    }
} catch (...) {}
} // namespace owned
