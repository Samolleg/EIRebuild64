#pragma once
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include "shader_cache.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace owned {

// Sun shafts. F10 `sun_shafts`: 0 off, 1 screen-space rays, 2 volumetric
// (the froxel fog, world_fog.h; screen-space rays when the volume is not
// running), 3 both. `sun_shaft_strength` scales either.
//
// Volumetric: the froxel in-scatter already samples the owned sun cascades
// (alpha-tested leaf cutouts in maps 0-2, far foliage solid in 3-4 per
// shadow_far_foliage_opaque) and the cloud shadow map. The haze itself is
// lit by the game's ambient plus sun; air in sun shadow keeps only the
// ambient part, and lit air looking toward the sun gains a forward
// Henyey-Greenstein lobe. Shafts therefore follow the fog density: strong in
// morning fog, after rain and in swamp mist, faint in clear noon air.
// Default of F10 `sun_shaft_strength` (the user's chosen look, 2026-10-04).
inline constexpr float kSunShaftDefaultStrength = 2.f;

enum SunShaftMode : int { sun_shafts_off = 0, sun_shafts_screen = 1, sun_shafts_volume = 2, sun_shafts_both = 3 };

struct SunShaftVolume {
    float shadow_share = 0.f;  // haze lost in full sun shadow (share of the haze)
    float forward = 0.f;       // scale of the forward lobe on lit air
    float headroom = 0.f;      // allowed rise above the lit-white fog budget
    float anisotropy = 0.f;    // Henyey-Greenstein blend against isotropic haze
};

inline float sun_shaft_luma(const float c[3]) noexcept { return .2126f * c[0] + .7152f * c[1] + .0722f * c[2]; }

// Share of the lit haze that comes from the sun. Owned surfaces take
// max(ambient, sun), so air in shadow keeps the ambient part only. Fades
// below the horizon and at night.
inline float sun_shaft_sun_share(const float ambient[3], const float sun[3], float sun_elevation,
                                 float night) noexcept {
    const float a = sun_shaft_luma(ambient), s = sun_shaft_luma(sun);
    const float lit = (std::max)(a, s);
    if (!(lit > 1e-4f)) return 0.f;
    const float t = std::clamp((sun_elevation + .02f) / .12f, 0.f, 1.f);
    return (lit - a) / lit * t * t * (3.f - 2.f * t) * (1.f - std::clamp(night, 0.f, 1.f));
}

inline bool sun_shafts_volumetric(int mode, bool volume_active) noexcept {
    return volume_active && (mode == sun_shafts_volume || mode == sun_shafts_both);
}
inline bool sun_shafts_screen_space(int mode, bool volume_active) noexcept {
    return mode == sun_shafts_screen || mode == sun_shafts_both || (mode == sun_shafts_volume && !volume_active);
}

// Morning fog and wet air scatter more forward light: the forward lobe grows
// with them; the shadow share is the physical sun share times strength.
inline SunShaftVolume sun_shaft_volume(int mode, bool volume_active, float strength, float sun_share,
                                       float morning, float wetness) noexcept {
    SunShaftVolume v;
    if (!sun_shafts_volumetric(mode, volume_active) || !(strength > 0.f) || !(sun_share > 0.f)) return v;
    v.shadow_share = (std::min)(sun_share * strength, .9f);
    v.forward = sun_share * strength * (1.f + .5f * std::clamp(morning, 0.f, 1.f) + .3f * std::clamp(wetness, 0.f, 1.f));
    v.headroom = .8f * (std::min)(strength, 2.f);
    v.anisotropy = .35f;
    return v;
}

// Screen-space rays: the sun's screen position (uv, may be off screen) and
// the fade from the angle between the view axis and the sun. world_to_clip
// is row-major with clip = M * (x, y, z, 1), as the world shaders use it.
struct SunRayScreen {
    bool active = false;
    float center[2]{};
    float fade = 0.f;
};
inline SunRayScreen sun_ray_screen(const float m[16], const float toward_sun[3]) noexcept {
    SunRayScreen r;
    const float d[3] = {toward_sun[0], toward_sun[1], toward_sun[2]};
    // Direction at infinity: clip = M * (d, 0).
    const float x = m[0] * d[0] + m[1] * d[1] + m[2] * d[2];
    const float y = m[4] * d[0] + m[5] * d[1] + m[6] * d[2];
    const float w = m[12] * d[0] + m[13] * d[1] + m[14] * d[2];
    const float axis = std::sqrt(m[12] * m[12] + m[13] * m[13] + m[14] * m[14]);
    if (!(axis > 0.f) || !(w > 1e-4f * axis)) return r;  // sun behind the camera plane
    const float facing = w / axis;  // cos(view axis, sun)
    const float t = std::clamp((facing - .05f) / .45f, 0.f, 1.f);
    r.fade = t * t * (3.f - 2.f * t);
    r.center[0] = x / w * .5f + .5f;
    r.center[1] = .5f - y / w * .5f;
    r.active = r.fade > 0.f;
    return r;
}

// Shader source of SunRaysPass (also compiled by its regression).
inline constexpr char kSunRaysShader[] = R"(
cbuffer SunRays:register(b0){float4 rayCenter;float4 rayTint;float4 rayMask;};
Texture2D raySource:register(t0);
Texture2D<float> rayDepth:register(t1);
SamplerState rayLinear:register(s0);
struct V{float4 position:SV_Position;float2 uv:TEXCOORD0;};
V sunRaysVS(uint id:SV_VertexID){V o;o.uv=float2((id<<1)&2,id&2);o.position=float4(o.uv*float2(2,-2)+float2(-1,1),0,1);return o;}
// Quarter-resolution occluder mask: open sky counts fully; sunlit surfaces
// (bright against shadow) count by rayTint.w, so canopy gaps streak in a
// top-down view where the sky is rarely visible. Shadowed/occluding pixels are 0.
float sunRayMask(float2 uv,int2 depthPixel){
 float3 c=clamp(raySource.SampleLevel(rayLinear,uv,0).rgb,0,4);
 float luma=dot(c,float3(.2126,.7152,.0722));
 // Sky coverage over 2x2 depth texels, not one: a single texel flipped between
 // sky and leaf as the camera moved and the radial blur stretched each flip
 // into a flickering ray.
 float sky=.25*(float(rayDepth.Load(int3(depthPixel,0))>=.99999)+float(rayDepth.Load(int3(depthPixel+int2(1,0),0))>=.99999)+
                float(rayDepth.Load(int3(depthPixel+int2(0,1),0))>=.99999)+float(rayDepth.Load(int3(depthPixel+int2(1,1),0))>=.99999));
 // Only light above the horizon haze (rayMask.w, its luminance) is a source:
 // fogged sky and fogged ground are uniform, and a uniform source blurred
 // and added is a veil that brightens all fog, not a ray. The sun glow and
 // sunlit cloud gaps or canopy gaps stand out above it.
 // Near the sun the whole sky glow is a source (rays radiate from it); away
 // from it only sky brighter than the haze counts (cloud gaps).
 uint w,h;raySource.GetDimensions(w,h);
 float2 d=(uv-rayCenter.xy)*float2(float(w)/h,1);
 float glow=exp(-dot(d,d)*9);
 float base=rayMask.w,above=saturate((luma-base)/max(1-base,.15));
 return lerp(rayTint.w*luma*smoothstep(rayMask.x,rayMask.y,luma),max(luma*glow,above),sky);
}
float4 sunRayMaskPS(V i):SV_Target{
 uint w,h;rayDepth.GetDimensions(w,h);
 float2 texel=1/float2(w,h);int2 p=int2(i.uv*float2(w,h));
 float m=sunRayMask(i.uv+texel*float2(-1,-1),p+int2(-1,-1))+sunRayMask(i.uv+texel*float2(1,-1),p+int2(1,-1))+
         sunRayMask(i.uv+texel*float2(-1,1),p+int2(-1,1))+sunRayMask(i.uv+texel*float2(1,1),p+int2(1,1));
 return float4(.25*m,0,0,0);
}
// Radial blur toward the sun (GPU Gems 3 ch. 13 style): exponentially
// decaying taps, dithered start, normalised by the summed weights. A far
// off-screen sun becomes a directional streak capped at rayCenter.z.
float4 sunRayBlurPS(V i):SV_Target{
 float2 d=rayCenter.xy-i.uv;float len=length(d);
 if(len>rayCenter.z)d*=rayCenter.z/len;
 float count=rayCenter.w;float2 stepUv=d/count;
 float dither=frac(52.9829189*frac(dot(i.position.xy,float2(.06711056,.00583715))));
 float2 uv=i.uv+stepUv*dither;
 float sum=0,weight=0,decay=1;
 [loop]for(float k=0;k<count;k+=1){
  if(all(uv>=0)&&all(uv<=1))sum+=raySource.SampleLevel(rayLinear,uv,0).r*decay;
  weight+=decay;decay*=rayMask.z;uv+=stepUv;
 }
 return float4(sum/max(weight,1e-4),0,0,0);
}
float4 sunRayCompositePS(V i):SV_Target{return float4(raySource.SampleLevel(rayLinear,i.uv,0).r*rayTint.rgb,0);}
)";

// Screen-space sun rays for weak PCs (sun_shafts 1) or on top of the volume
// (3). Quarter-resolution mask and one 48-tap radial blur, added to the
// resolved world image before any interface pass (like bloom), so the UI
// never streaks. Reference: GD3D11's god-ray post effect (mask, zoom blur at
// quarter resolution, additive blend, fade as the sun leaves the view).
// Caller saves/restores pipeline state; slots used: VS/PS b0, PS t0-t1, PS s0.
class SunRaysPass {
    template<class T> using Ptr = Microsoft::WRL::ComPtr<T>;
    Ptr<ID3D11Texture2D> mask_, blur_;
    Ptr<ID3D11RenderTargetView> mask_rtv_, blur_rtv_;
    Ptr<ID3D11ShaderResourceView> mask_srv_, blur_srv_;
    Ptr<ID3D11VertexShader> vs_;
    Ptr<ID3D11PixelShader> mask_ps_, blur_ps_, composite_ps_;
    Ptr<ID3D11Buffer> constants_;
    Ptr<ID3D11SamplerState> linear_;
    Ptr<ID3D11BlendState> add_;
    UINT width_ = 0, height_ = 0, size_[2]{};
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
        Ptr<ID3D11PixelShader>* shaders[] = {&mask_ps_, &blur_ps_, &composite_ps_};
        for (unsigned n = 0; n < 3; ++n) {
            code.Reset();
            if (FAILED(hr = compile(programs[1 + n], code))) return hr;
            if (FAILED(hr = create_shader(device,code->GetBufferPointer(), code->GetBufferSize(), nullptr,
                                                      shaders[n]->GetAddressOf()))) return hr;
        }
        D3D11_BUFFER_DESC b{}; b.ByteWidth = 48; b.Usage = D3D11_USAGE_DYNAMIC;
        b.BindFlags = D3D11_BIND_CONSTANT_BUFFER; b.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(hr = device->CreateBuffer(&b, nullptr, &constants_))) return hr;
        D3D11_SAMPLER_DESC s{}; s.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        s.AddressU = s.AddressV = s.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; s.MaxLOD = 0;
        if (FAILED(hr = device->CreateSamplerState(&s, &linear_))) return hr;
        D3D11_BLEND_DESC blend{}; auto& target = blend.RenderTarget[0];
        target.BlendEnable = TRUE; target.SrcBlend = target.DestBlend = D3D11_BLEND_ONE;
        target.BlendOp = D3D11_BLEND_OP_ADD; target.SrcBlendAlpha = D3D11_BLEND_ZERO;
        target.DestBlendAlpha = D3D11_BLEND_ONE; target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
        return device->CreateBlendState(&blend, &add_);
    }

    HRESULT create_targets(ID3D11Device* device, UINT width, UINT height) {
        mask_.Reset(); blur_.Reset(); mask_rtv_.Reset(); blur_rtv_.Reset(); mask_srv_.Reset(); blur_srv_.Reset();
        width_ = width; height_ = height;  // a failed size is not retried until it changes
        size_[0] = (std::max)(1u, width / 4); size_[1] = (std::max)(1u, height / 4);
        auto target = [&](Ptr<ID3D11Texture2D>& texture, Ptr<ID3D11RenderTargetView>& rtv, Ptr<ID3D11ShaderResourceView>& srv) {
            D3D11_TEXTURE2D_DESC d{}; d.Width = size_[0]; d.Height = size_[1];
            d.MipLevels = d.ArraySize = d.SampleDesc.Count = 1; d.Format = DXGI_FORMAT_R16_FLOAT;
            d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            HRESULT hr = device->CreateTexture2D(&d, nullptr, &texture);
            if (SUCCEEDED(hr)) hr = device->CreateRenderTargetView(texture.Get(), nullptr, &rtv);
            if (SUCCEEDED(hr)) hr = device->CreateShaderResourceView(texture.Get(), nullptr, &srv);
            return hr;
        };
        HRESULT hr = target(mask_, mask_rtv_, mask_srv_);
        if (SUCCEEDED(hr)) hr = target(blur_, blur_rtv_, blur_srv_);
        if (FAILED(hr)) {
            mask_srv_.Reset(); blur_srv_.Reset();
            OutputDebugStringA("EID3D11 sun ray targets unavailable; screen-space sun rays omitted\n");
        }
        return hr;
    }

public:
    // sunRaysVS, then the mask, blur and composite passes.
    static void shader_programs(std::vector<ShaderProgram>& out) {
        const std::string_view source(kSunRaysShader, sizeof(kSunRaysShader) - 1);
        out.push_back({source, "SunRays", nullptr, "sunRaysVS", "vs_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3});
        for (const char* entry : {"sunRayMaskPS", "sunRayBlurPS", "sunRayCompositePS"})
            out.push_back({source, "SunRays", nullptr, entry, "ps_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3});
    }
    static constexpr float kSamples = 48.f;
    std::uint64_t storage_bytes() const { return blur_srv_ ? std::uint64_t(size_[0]) * size_[1] * 4u : 0; }
    void reset() { *this = SunRaysPass{}; }

    // Adds sun rays of `scene` (unclamped world colour) masked by `depth` to
    // `output` (resolved world image). `tint` is the sun colour times
    // strength and fade. `draws` receives the fullscreen draw count.
    HRESULT render(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11ShaderResourceView* scene,
                   ID3D11ShaderResourceView* depth, ID3D11RenderTargetView* output, UINT width, UINT height,
                   const float center[2], const float tint[3], float haze_luma, UINT* draws = nullptr) {
        if (draws) *draws = 0;
        if (shaders_failed_) return E_FAIL;
        HRESULT hr;
        if (!vs_ && FAILED(hr = create_shaders(device))) {
            OutputDebugStringA("EID3D11 sun ray shaders unavailable; screen-space sun rays omitted\n");
            shaders_failed_ = true; return hr;
        }
        if ((width != width_ || height != height_) && FAILED(hr = create_targets(device, width, height))) return hr;
        if (!blur_srv_) return E_FAIL;
        D3D11_MAPPED_SUBRESOURCE map{};
        if (FAILED(hr = context->Map(constants_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) return hr;
        // Streak length at most 45% of the screen; sunlit surfaces count 35%
        // of the sky, above a soft luminance knee; 0.965 decay per tap; only
        // light above the horizon haze luminance counts.
        const float values[12] = {center[0], center[1], .45f, kSamples, tint[0], tint[1], tint[2], .35f, .35f, .85f, .965f, haze_luma};
        std::copy(values, values + 12, static_cast<float*>(map.pData));
        context->Unmap(constants_.Get(), 0);
        ID3D11ShaderResourceView* none[2]{};
        context->OMSetRenderTargets(0, nullptr, nullptr);
        context->IASetInputLayout(nullptr); context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vs_.Get(), nullptr, 0); context->GSSetShader(nullptr, nullptr, 0);
        context->HSSetShader(nullptr, nullptr, 0); context->DSSetShader(nullptr, nullptr, 0);
        context->RSSetState(nullptr); context->OMSetDepthStencilState(nullptr, 0);
        context->PSSetConstantBuffers(0, 1, constants_.GetAddressOf());
        context->PSSetSamplers(0, 1, linear_.GetAddressOf());
        UINT issued = 0;
        auto pass = [&](ID3D11RenderTargetView* target, UINT w, UINT h, ID3D11PixelShader* shader,
                        ID3D11ShaderResourceView* input, ID3D11ShaderResourceView* depth_input, bool additive) {
            context->PSSetShaderResources(0, 2, none);
            context->OMSetRenderTargets(1, &target, nullptr);
            const D3D11_VIEWPORT viewport{0, 0, float(w), float(h), 0, 1};
            context->RSSetViewports(1, &viewport);
            context->OMSetBlendState(additive ? add_.Get() : nullptr, nullptr, ~0u);
            ID3D11ShaderResourceView* inputs[2] = {input, depth_input};
            context->PSSetShaderResources(0, 2, inputs);
            context->PSSetShader(shader, nullptr, 0); context->Draw(3, 0); ++issued;
        };
        pass(mask_rtv_.Get(), size_[0], size_[1], mask_ps_.Get(), scene, depth, false);
        pass(blur_rtv_.Get(), size_[0], size_[1], blur_ps_.Get(), mask_srv_.Get(), nullptr, false);
        pass(output, width, height, composite_ps_.Get(), blur_srv_.Get(), nullptr, true);
        context->PSSetShaderResources(0, 2, none);
        context->OMSetBlendState(nullptr, nullptr, ~0u);
        if (draws) *draws = issued;
        return S_OK;
    }
};

}  // namespace owned
