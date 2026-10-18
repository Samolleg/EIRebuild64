#pragma once
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include "shader_cache.h"
#include <algorithm>
#include <array>
#include <cstdint>

namespace owned {

// Bloom extracts only light above the scene's non-emissive ceiling: the
// brightest colour a lit, non-emissive surface or the fog can reach this
// frame (lit white, raised by the volumetric shaft headroom; sky pixels are
// excluded by depth). Every fogged or lit object, the fog itself and the sky
// stay below it, so only emissive and additive light (fire, lamps, lava,
// magic, sun glints) blooms (issue #99). `threshold` is relative to the
// ceiling (1 = exactly the ceiling). The soft knee starts at the threshold
// and reaches full rate a quarter of it above.
// Layout: {start, 2*knee, 0.25/knee, start+knee}.
inline std::array<float,4> bloom_knee_curve(float ceiling, float threshold) {
    const float start = std::max(ceiling, 1e-2f)*std::max(threshold, 1.f);
    const float knee = start*0.25f;
    return {start, 2.f*knee, 0.25f/knee, start+knee};
}

// Non-emissive ceiling: owned surfaces take max(ambient, sun) clamped to 1
// (lamps and emissive on top are light), and the fog stays within that lit
// white, raised by `fog_headroom` where volumetric shafts brighten the haze
// toward the sun.
inline float bloom_ceiling(const float ambient[3], const float sun[3], float fog_headroom) {
    float lit = 0.f;
    for (int k = 0; k < 3; ++k) lit = std::max(lit, std::clamp(std::max(ambient[k], sun[k]), 0.f, 1.f));
    return lit*(1.f + std::max(fog_headroom, 0.f));
}

// Shader source of BloomPass (also compiled by its regression).
inline constexpr char kBloomShader[] = R"(
cbuffer Bloom : register(b0) { float4 bloomCurve; float4 bloomScale; };
Texture2D source : register(t0);
Texture2D<float> sceneDepth : register(t1);
Texture2D glow : register(t2); // additive light drawn this frame (unbound: none)
SamplerState linearClamp : register(s0);
struct V { float4 position : SV_Position; float2 uv : TEXCOORD0; };
V bloomVS(uint id : SV_VertexID) {
    V o; o.uv = float2((id << 1) & 2, id & 2);
    o.position = float4(o.uv*float2(2, -2) + float2(-1, 1), 0, 1); return o;
}
float2 texel() { uint w, h; source.GetDimensions(w, h); return 1.0/float2(w, h); }
// Clamp rejects negative, infinite and NaN input before it can spread.
float3 tap(float2 uv, float2 t, float2 offset) {
    return clamp(source.SampleLevel(linearClamp, uv + offset*t, 0).rgb, 0, 64);
}
float luma(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }
// Karis average (Jimenez 2014): a lone extreme texel cannot dominate.
float3 karis(float3 a, float3 b, float3 c, float3 d) {
    float4 w = 1.0/(1.0 + float4(luma(a), luma(b), luma(c), luma(d)));
    return (a*w.x + b*w.y + c*w.z + d*w.w)/dot(w, 1);
}
float3 brightPass(float3 c) {
    float brightness = max(c.r, max(c.g, c.b));
    float soft = clamp(brightness - bloomCurve.x, 0, bloomCurve.y);
    soft = soft*soft*bloomCurve.z;
    return c*(max(soft, brightness - bloomCurve.w)/max(brightness, 1e-4));
}
// The sky (depth at the cleared far plane) is background, never a light
// source; an unbound depth reads 0 and excludes nothing. Additive light
// (glow) is emissive by definition and blooms without the ceiling.
float3 brightTap(float2 uv, float2 t, float2 offset) {
    float2 p = uv + offset*t;
    uint w, h; sceneDepth.GetDimensions(w, h);
    int2 pixel = clamp(int2(p*float2(w, h)), 0, int2(w, h) - 1);
    float3 g = clamp(glow.SampleLevel(linearClamp, p, 0).rgb, 0, 64)*bloomScale.z;
    return g + (sceneDepth.Load(int3(pixel, 0)) >= 1 ? 0 : brightPass(tap(uv, t, offset)));
}
float4 prefilter13PS(V i) : SV_Target {
    float2 t = texel(); float2 uv = i.uv;
    float3 a = brightTap(uv, t, float2(-2, -2)), b = brightTap(uv, t, float2(0, -2)), c = brightTap(uv, t, float2(2, -2));
    float3 d = brightTap(uv, t, float2(-2, 0)), e = brightTap(uv, t, float2(0, 0)), f = brightTap(uv, t, float2(2, 0));
    float3 g = brightTap(uv, t, float2(-2, 2)), h = brightTap(uv, t, float2(0, 2)), k = brightTap(uv, t, float2(2, 2));
    float3 j = brightTap(uv, t, float2(-1, -1)), l = brightTap(uv, t, float2(1, -1));
    float3 m = brightTap(uv, t, float2(-1, 1)), n = brightTap(uv, t, float2(1, 1));
    float3 result = karis(j, l, m, n)*0.5 +
        (karis(a, b, d, e) + karis(b, c, e, f) + karis(d, e, g, h) + karis(e, f, h, k))*0.125;
    return float4(result, 0);
}
float4 prefilter4PS(V i) : SV_Target {
    float2 t = texel();
    return float4(karis(brightTap(i.uv, t, float2(-1, -1)), brightTap(i.uv, t, float2(1, -1)),
                       brightTap(i.uv, t, float2(-1, 1)), brightTap(i.uv, t, float2(1, 1))), 0);
}
float4 downPS(V i) : SV_Target {
    float2 t = texel(); float2 uv = i.uv;
    float3 outer = tap(uv, t, float2(-2, -2)) + tap(uv, t, float2(2, -2)) + tap(uv, t, float2(-2, 2)) + tap(uv, t, float2(2, 2));
    float3 cross = tap(uv, t, float2(0, -2)) + tap(uv, t, float2(-2, 0)) + tap(uv, t, float2(2, 0)) + tap(uv, t, float2(0, 2));
    float3 inner = tap(uv, t, float2(-1, -1)) + tap(uv, t, float2(1, -1)) + tap(uv, t, float2(-1, 1)) + tap(uv, t, float2(1, 1));
    return float4(tap(uv, t, 0)*0.125 + outer*0.03125 + cross*0.0625 + inner*0.125, 0);
}
float4 upPS(V i) : SV_Target {
    float2 t = texel(); float2 uv = i.uv;
    float3 s = tap(uv, t, 0)*4 +
        (tap(uv, t, float2(0, -1)) + tap(uv, t, float2(-1, 0)) + tap(uv, t, float2(1, 0)) + tap(uv, t, float2(0, 1)))*2 +
        tap(uv, t, float2(-1, -1)) + tap(uv, t, float2(1, -1)) + tap(uv, t, float2(-1, 1)) + tap(uv, t, float2(1, 1));
    return float4(s*(bloomScale.y/16), 0);
}
float4 compositePS(V i) : SV_Target {
    return float4(source.SampleLevel(linearClamp, i.uv, 0).rgb*bloomScale.x, 0);
}
)";

// World bloom: soft-knee bright pass above the non-emissive ceiling with a Karis-weighted first downsample,
// a 13-tap downsample chain and a tent-filtered additive upsample chain
// (Jimenez, "Next Generation Post Processing in Call of Duty: Advanced
// Warfare", SIGGRAPH 2014). The composite adds the half-resolution result to
// the already resolved world image, before any interface pass draws, so the
// scene target is never modified and UI, cursor and text never bloom.
// Quality 1 ("low") uses four mips and a 4-tap Karis prefilter; quality 2
// ("high") six mips and the 13-tap Karis prefilter.
// Caller saves/restores pipeline state; slots used: VS/PS b0, PS t0-t2, PS s0.
class BloomPass {
    template<class T> using Ptr = Microsoft::WRL::ComPtr<T>;
    static constexpr UINT kMaxMips = 6;
    static constexpr float kWide = 1.2f; // per-level boost of the wider blur levels
    // Gain on the extracted excess. Emissive light here sits only a little
    // above the ceiling (additive sprites; emissive meshes clamp at 1) and
    // the blur spreads it widely: at strength 1 a fire or lamp must still
    // read as a clear glow.
    static constexpr float kGain = 4.f;
    // Share of additive particle light (fire, magic) fed to the glow.
    static constexpr float kGlow = .5f;
    Ptr<ID3D11Texture2D> mip_[kMaxMips];
    Ptr<ID3D11RenderTargetView> mip_rtv_[kMaxMips];
    Ptr<ID3D11ShaderResourceView> mip_srv_[kMaxMips];
    Ptr<ID3D11VertexShader> vs_;
    Ptr<ID3D11PixelShader> prefilter13_ps_, prefilter4_ps_, down_ps_, up_ps_, composite_ps_;
    Ptr<ID3D11Buffer> constants_;
    Ptr<ID3D11SamplerState> linear_;
    Ptr<ID3D11BlendState> add_;
    UINT width_ = 0, height_ = 0, mips_ = 0, sizes_[kMaxMips][2]{};
    int quality_ = 0;
    bool shaders_failed_ = false;

    HRESULT create_shaders(ID3D11Device* device) {
        std::vector<ShaderProgram> programs;
        shader_programs(programs);
        auto compile = [&](const ShaderProgram& program, Ptr<ID3DBlob>& code) {
            Ptr<ID3DBlob> error;
            const HRESULT hr = compile_shader(program, &code, &error);
            if (FAILED(hr) && error) OutputDebugStringA(static_cast<const char*>(error->GetBufferPointer()));
            return hr;
        };
        Ptr<ID3DBlob> code; HRESULT hr;
        if (FAILED(hr = compile(programs[0], code))) return hr;
        if (FAILED(hr = create_shader(device,code->GetBufferPointer(), code->GetBufferSize(), nullptr, &vs_))) return hr;
        Ptr<ID3D11PixelShader>* shaders[] = {&prefilter13_ps_, &prefilter4_ps_, &down_ps_, &up_ps_, &composite_ps_};
        for (unsigned n = 0; n < 5; ++n) {
            code.Reset();
            if (FAILED(hr = compile(programs[1 + n], code))) return hr;
            if (FAILED(hr = create_shader(device,code->GetBufferPointer(), code->GetBufferSize(), nullptr,
                                                      shaders[n]->GetAddressOf()))) return hr;
        }
        D3D11_BUFFER_DESC b{}; b.ByteWidth = 32; b.Usage = D3D11_USAGE_DYNAMIC;
        b.BindFlags = D3D11_BIND_CONSTANT_BUFFER; b.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(hr = device->CreateBuffer(&b, nullptr, &constants_))) return hr;
        D3D11_SAMPLER_DESC s{}; s.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        s.AddressU = s.AddressV = s.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; s.MaxLOD = 0;
        if (FAILED(hr = device->CreateSamplerState(&s, &linear_))) return hr;
        D3D11_BLEND_DESC blend{}; auto& target = blend.RenderTarget[0];
        target.BlendEnable = TRUE; target.SrcBlend = target.DestBlend = D3D11_BLEND_ONE;
        target.BlendOp = D3D11_BLEND_OP_ADD; target.SrcBlendAlpha = D3D11_BLEND_ZERO;
        target.DestBlendAlpha = D3D11_BLEND_ONE; target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED|D3D11_COLOR_WRITE_ENABLE_GREEN|D3D11_COLOR_WRITE_ENABLE_BLUE;
        return device->CreateBlendState(&blend, &add_);
    }

    HRESULT create_targets(ID3D11Device* device, UINT width, UINT height, int quality) {
        for (UINT n = 0; n < kMaxMips; ++n) { mip_[n].Reset(); mip_rtv_[n].Reset(); mip_srv_[n].Reset(); }
        // A failed size/quality is not retried until one of them changes.
        width_ = width; height_ = height; quality_ = quality; mips_ = 0;
        const UINT wanted = quality >= 2 ? 6u : 4u;
        UINT count = 0;
        // Stop before the shorter side drops under 8 pixels; the last mip then
        // still holds a meaningful blur instead of a few edge-clamped texels.
        while (count < wanted && (std::min(width, height) >> (count+1)) >= 8) ++count;
        count = std::max(count, 1u);
        for (UINT n = 0; n < count; ++n) {
            D3D11_TEXTURE2D_DESC d{}; d.Width = std::max(1u, width >> (n+1)); d.Height = std::max(1u, height >> (n+1));
            d.MipLevels = d.ArraySize = d.SampleDesc.Count = 1; d.Format = DXGI_FORMAT_R11G11B10_FLOAT;
            d.BindFlags = D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
            HRESULT hr = device->CreateTexture2D(&d, nullptr, &mip_[n]);
            if (SUCCEEDED(hr)) hr = device->CreateRenderTargetView(mip_[n].Get(), nullptr, &mip_rtv_[n]);
            if (SUCCEEDED(hr)) hr = device->CreateShaderResourceView(mip_[n].Get(), nullptr, &mip_srv_[n]);
            if (FAILED(hr)) {
                OutputDebugStringA("EID3D11 bloom targets unavailable; bloom omitted\n");
                return hr;
            }
            sizes_[n][0] = d.Width; sizes_[n][1] = d.Height;
        }
        mips_ = count;
        return S_OK;
    }

public:
    // bloomVS, then the five pixel passes in create_shaders order.
    static void shader_programs(std::vector<ShaderProgram>& out) {
        const std::string_view source(kBloomShader, sizeof(kBloomShader) - 1);
        out.push_back({source, "Bloom", nullptr, "bloomVS", "vs_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3});
        for (const char* entry : {"prefilter13PS", "prefilter4PS", "downPS", "upPS", "compositePS"})
            out.push_back({source, "Bloom", nullptr, entry, "ps_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3});
    }
    UINT mip_count() const { return mips_; }
    std::uint64_t storage_bytes() const {
        std::uint64_t bytes = 0;
        for (UINT n = 0; n < mips_; ++n) bytes += std::uint64_t(sizes_[n][0])*sizes_[n][1]*4u;
        return bytes;
    }
    void reset() { *this = BloomPass{}; }

    // Adds bloom of `scene` (the unclamped world colour) to `output` (the
    // resolved world image, width x height). `depth` is the world depth of
    // `scene` (sky excluded; may be null), `glow` the additive light drawn this
    // frame (may be null), `ceiling` the non-emissive ceiling
    // (bloom_ceiling) and `threshold` relative to it. Resources follow the requested
    // size and quality; `draws` receives the number of fullscreen draws.
    // A failure is logged once where it occurs and leaves `output` untouched.
    // `contribution_only` replaces `output` with the glow alone (debug view).
    HRESULT render(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11ShaderResourceView* scene,
                   ID3D11ShaderResourceView* depth, ID3D11ShaderResourceView* glow,
                   ID3D11RenderTargetView* output, UINT width, UINT height,
                   int quality, float strength, float ceiling, float threshold, UINT* draws = nullptr,
                   bool contribution_only = false) {
        if (draws) *draws = 0;
        if (quality <= 0 || strength <= 0.f) return S_OK;
        if (shaders_failed_) return E_FAIL;
        HRESULT hr;
        if (!vs_ && FAILED(hr = create_shaders(device))) {
            OutputDebugStringA("EID3D11 bloom shaders unavailable; bloom omitted\n");
            shaders_failed_ = true; return hr;
        }
        if ((width != width_ || height != height_ || quality != quality_) &&
            FAILED(hr = create_targets(device, width, height, quality))) return hr;
        if (!mips_) return E_FAIL;

        D3D11_MAPPED_SUBRESOURCE map{};
        if (FAILED(hr = context->Map(constants_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) return hr;
        const auto curve = bloom_knee_curve(ceiling, threshold);
        // Each upsample scales the wider levels by kWide, so level n ends with
        // weight kWide^n: a soft wide halo for atmosphere around a tight core.
        // Normalise by the summed weights so strength means the same at each quality.
        float weights = 0, weight = 1;
        for (UINT n = 0; n < mips_; ++n, weight *= kWide) weights += weight;
        const float values[8] = {curve[0], curve[1], curve[2], curve[3], strength*kGain/weights, kWide, kGlow, 0};
        std::copy(values, values+8, static_cast<float*>(map.pData));
        context->Unmap(constants_.Get(), 0);

        ID3D11ShaderResourceView* none = nullptr;
        context->OMSetRenderTargets(0, nullptr, nullptr);
        context->IASetInputLayout(nullptr); context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vs_.Get(), nullptr, 0); context->GSSetShader(nullptr, nullptr, 0);
        context->HSSetShader(nullptr, nullptr, 0); context->DSSetShader(nullptr, nullptr, 0);
        context->RSSetState(nullptr); context->OMSetDepthStencilState(nullptr, 0);
        context->PSSetConstantBuffers(0, 1, constants_.GetAddressOf());
        context->PSSetSamplers(0, 1, linear_.GetAddressOf());
        UINT issued = 0;
        auto pass = [&](ID3D11RenderTargetView* target, UINT w, UINT h, ID3D11PixelShader* shader,
                        ID3D11ShaderResourceView* input, bool additive) {
            context->PSSetShaderResources(0, 1, &none);
            context->OMSetRenderTargets(1, &target, nullptr);
            const D3D11_VIEWPORT viewport{0, 0, float(w), float(h), 0, 1};
            context->RSSetViewports(1, &viewport);
            context->OMSetBlendState(additive ? add_.Get() : nullptr, nullptr, ~0u);
            context->PSSetShaderResources(0, 1, &input);
            context->PSSetShader(shader, nullptr, 0); context->Draw(3, 0); ++issued;
        };
        ID3D11ShaderResourceView* inputs[2] = {depth, glow};
        context->PSSetShaderResources(1, 2, inputs);
        pass(mip_rtv_[0].Get(), sizes_[0][0], sizes_[0][1],
             quality >= 2 ? prefilter13_ps_.Get() : prefilter4_ps_.Get(), scene, false);
        ID3D11ShaderResourceView* nones[2] = {};
        context->PSSetShaderResources(1, 2, nones);
        for (UINT n = 1; n < mips_; ++n)
            pass(mip_rtv_[n].Get(), sizes_[n][0], sizes_[n][1], down_ps_.Get(), mip_srv_[n-1].Get(), false);
        // Each level becomes its own downsample plus the blurred level below.
        for (UINT n = mips_-1; n > 0; --n)
            pass(mip_rtv_[n-1].Get(), sizes_[n-1][0], sizes_[n-1][1], up_ps_.Get(), mip_srv_[n].Get(), true);
        pass(output, width, height, composite_ps_.Get(), mip_srv_[0].Get(), !contribution_only);
        context->PSSetShaderResources(0, 1, &none);
        context->OMSetBlendState(nullptr, nullptr, ~0u);
        if (draws) *draws = issued;
        return S_OK;
    }
};

} // namespace owned
