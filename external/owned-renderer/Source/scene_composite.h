#pragma once
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include "shader_cache.h"
#include <cstdio>
#include <cstdint>
#include "smaa_embedded.h"
#include "../ThirdParty/smaa/Textures/AreaTex.h"
#include "../ThirdParty/smaa/Textures/SearchTex.h"

namespace owned {
// A clear rectangle in output (game) pixels mapped onto the smaller world
// target at a render scale below 100%, rounded outward.
inline D3D11_RECT scale_screen_rectangle(const D3D11_RECT& source, UINT from_width, UINT from_height,
                                         UINT to_width, UINT to_height) noexcept {
    const auto down = [](LONG value, UINT to, UINT from) {
        return static_cast<LONG>(static_cast<std::uint64_t>(value) * to / from);
    };
    const auto up = [](LONG value, UINT to, UINT from) {
        return static_cast<LONG>((static_cast<std::uint64_t>(value) * to + from - 1) / from);
    };
    return {down(source.left, to_width, from_width), down(source.top, to_height, from_height),
            up(source.right, to_width, from_width), up(source.bottom, to_height, from_height)};
}

// Renderer-owned world colour and SMAA 1x resources. Device resources survive
// resize; reset() releases them when the renderer releases its device.
// Caller saves/restores pipeline state and establishes the next pass's target.
// Below 100% render scale the world (width x height) is smaller than the
// output: SMAA/copy, sun rays and bloom resolve into a world-size LDR image,
// which upscale() filters to the output while copying the world depth into
// the output-size interface depth, before the first interface pass.
class SceneComposite {
    friend struct SceneCompositeSmokeAccess;
    template<class T> using Ptr = Microsoft::WRL::ComPtr<T>;
    Ptr<ID3D11Texture2D> color_, edges_, weights_, resolved_;
    Ptr<ID3D11RenderTargetView> color_rtv_, edges_rtv_, weights_rtv_, resolved_rtv_;
    Ptr<ID3D11ShaderResourceView> color_srv_, edges_srv_, weights_srv_, resolved_srv_, area_, search_;
    Ptr<ID3D11VertexShader> vs_;
    Ptr<ID3D11PixelShader> edge_ps_, weight_ps_, blend_ps_, copy_ps_, upscale_ps_, upscale_depth_ps_;
    Ptr<ID3D11Buffer> metrics_;
    Ptr<ID3D11SamplerState> linear_, point_;
    Ptr<ID3D11DepthStencilState> depth_copy_;
    UINT width_ = 0, height_ = 0, output_width_ = 0, output_height_ = 0;
public:
    ID3D11RenderTargetView* target() const { return color_rtv_.Get(); }
    ID3D11Texture2D* color() const { return color_.Get(); }
    ID3D11ShaderResourceView* color_view() const { return color_srv_.Get(); }
    std::uint64_t storage_bytes() const { return color_?std::uint64_t(width_)*height_*(resolved_?14u:10u)+AREATEX_SIZE+SEARCHTEX_SIZE:0; }
    bool scaled() const { return resolved_ != nullptr; }
    // Where SMAA/copy, sun rays and bloom write: the output itself at 100%.
    ID3D11RenderTargetView* resolve_target(ID3D11RenderTargetView* output) const {
        return resolved_ ? resolved_rtv_.Get() : output;
    }
    void reset() { *this = SceneComposite{}; }
    void reset_targets() {
        color_srv_.Reset(); edges_srv_.Reset(); weights_srv_.Reset(); resolved_srv_.Reset();
        color_rtv_.Reset(); edges_rtv_.Reset(); weights_rtv_.Reset(); resolved_rtv_.Reset();
        color_.Reset(); edges_.Reset(); weights_.Reset(); resolved_.Reset(); metrics_.Reset();
        width_ = height_ = output_width_ = output_height_ = 0;
    }
    // width x height is the world render size; the output (swap chain) size
    // defaults to it. A smaller world adds the resolved image and upscale().
    HRESULT create(ID3D11Device* device, UINT width, UINT height, UINT output_width = 0, UINT output_height = 0) {
        reset_targets();
        if (!point_) {
            const HRESULT hr = create_device_resources(device);
            if (FAILED(hr)) { reset(); return hr; }
        }
        width_ = width; height_ = height;
        output_width_ = output_width ? output_width : width;
        output_height_ = output_height ? output_height : height;
        auto target = [&](DXGI_FORMAT format, Ptr<ID3D11Texture2D>& texture,
                          Ptr<ID3D11RenderTargetView>& rtv, Ptr<ID3D11ShaderResourceView>& srv) {
            D3D11_TEXTURE2D_DESC d{}; d.Width=width; d.Height=height;
            d.MipLevels=d.ArraySize=d.SampleDesc.Count=1; d.Format=format;
            d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
            HRESULT hr=device->CreateTexture2D(&d,nullptr,&texture);
            if(SUCCEEDED(hr))hr=device->CreateRenderTargetView(texture.Get(),nullptr,&rtv);
            if(SUCCEEDED(hr))hr=device->CreateShaderResourceView(texture.Get(),nullptr,&srv);
            return hr;
        };
        HRESULT hr=target(DXGI_FORMAT_R11G11B10_FLOAT,color_,color_rtv_,color_srv_);
        if(FAILED(hr))return hr;
        if(FAILED(hr=target(DXGI_FORMAT_R8G8_UNORM,edges_,edges_rtv_,edges_srv_)))return hr;
        if(FAILED(hr=target(DXGI_FORMAT_R8G8B8A8_UNORM,weights_,weights_rtv_,weights_srv_)))return hr;
        if((output_width_!=width||output_height_!=height)&&
           FAILED(hr=target(DXGI_FORMAT_B8G8R8A8_UNORM,resolved_,resolved_rtv_,resolved_srv_)))return hr;
        const float values[]={1.f/width,1.f/height,float(width),float(height)};
        D3D11_BUFFER_DESC b{};b.ByteWidth=16;b.Usage=D3D11_USAGE_IMMUTABLE;b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA data{values,0,0};return device->CreateBuffer(&b,&data,&metrics_);
    }
private:
    // The SMAA source (empty when the library's sampler declarations are not found).
    static std::string build_shader_source() {
        std::string library=smaa_shader_library();
        // Explicit slots replace effect-framework sampler declarations only.
        const auto replace=[&](const char* name,const char* declaration) {
            const auto start=library.find(name);const auto end=library.find("};",start);
            if(start==std::string::npos||end==std::string::npos)return false;
            library.replace(start,end+2-start,declaration);return true;
        };
        if(!replace("SamplerState LinearSampler", "SamplerState LinearSampler : register(s0);" )||
           !replace("SamplerState PointSampler", "SamplerState PointSampler : register(s1);"))return std::string();
        return std::string(
            "#define SMAA_HLSL_4_1 1\n#define SMAA_PRESET_HIGH 1\n"
            "cbuffer Metrics : register(b0) { float4 metrics; };\n#define SMAA_RT_METRICS metrics\n")+library+R"(
Texture2D input0 : register(t0);
Texture2D input1 : register(t1);
Texture2D input2 : register(t2);
struct FullVertex { float4 position : SV_Position; float2 uv : TEXCOORD0; };
FullVertex sceneVS(uint id : SV_VertexID) {
    FullVertex o; o.uv=float2((id<<1)&2,id&2);
    o.position=float4(o.uv*float2(2,-2)+float2(-1,1),0,1);return o;
}
float2 edgePS(FullVertex i) : SV_Target {
    float4 offsets[3]; SMAAEdgeDetectionVS(i.uv,offsets);
    return SMAALumaEdgeDetectionPS(i.uv,offsets,input0);
}
float4 weightPS(FullVertex i) : SV_Target {
    float2 pixel;float4 offsets[3];SMAABlendingWeightCalculationVS(i.uv,pixel,offsets);
    return SMAABlendingWeightCalculationPS(i.uv,pixel,offsets,input0,input1,input2,0);
}
float4 blendPS(FullVertex i) : SV_Target {
    float4 offset;SMAANeighborhoodBlendingVS(i.uv,offset);
    return float4(saturate(SMAANeighborhoodBlendingPS(i.uv,offset,input0,input1).rgb),1);
}
float4 copyPS(FullVertex i) : SV_Target {
    return float4(saturate(input0.Load(int3(i.position.xy,0)).rgb),1);
}
// Render-scale upscale of the resolved world image (metrics: its size).
// Catmull-Rom from nine bilinear taps: a sharp bicubic whose negative lobes
// keep the edge contrast a plain bilinear stretch loses.
float3 catmullRom(float2 uv) {
    float2 position = uv * metrics.zw;
    float2 center = floor(position - .5) + .5;
    float2 f = position - center;
    float2 w0 = f * (-.5 + f * (1 - .5 * f));
    float2 w1 = 1 + f * f * (-2.5 + 1.5 * f);
    float2 w2 = f * (.5 + f * (2 - 1.5 * f));
    float2 w3 = f * f * (-.5 + .5 * f);
    float2 w12 = w1 + w2;
    float2 t0 = (center - 1) * metrics.xy, t3 = (center + 2) * metrics.xy;
    float2 t12 = (center + w2 / w12) * metrics.xy;
    float3 c = 0;
    c += input0.SampleLevel(LinearSampler, float2(t0.x, t0.y), 0).rgb * (w0.x * w0.y);
    c += input0.SampleLevel(LinearSampler, float2(t12.x, t0.y), 0).rgb * (w12.x * w0.y);
    c += input0.SampleLevel(LinearSampler, float2(t3.x, t0.y), 0).rgb * (w3.x * w0.y);
    c += input0.SampleLevel(LinearSampler, float2(t0.x, t12.y), 0).rgb * (w0.x * w12.y);
    c += input0.SampleLevel(LinearSampler, float2(t12.x, t12.y), 0).rgb * (w12.x * w12.y);
    c += input0.SampleLevel(LinearSampler, float2(t3.x, t12.y), 0).rgb * (w3.x * w12.y);
    c += input0.SampleLevel(LinearSampler, float2(t0.x, t3.y), 0).rgb * (w0.x * w3.y);
    c += input0.SampleLevel(LinearSampler, float2(t12.x, t3.y), 0).rgb * (w12.x * w3.y);
    c += input0.SampleLevel(LinearSampler, float2(t3.x, t3.y), 0).rgb * (w3.x * w3.y);
    return saturate(c);
}
float4 upscalePS(FullVertex i) : SV_Target { return float4(catmullRom(i.uv), 1); }
// The same, also giving interface passes the world depth (nearest world
// texel). Their stencil starts cleared.
Texture2D<float> worldDepth : register(t3);
struct UpscaleOutput { float4 color : SV_Target; float depth : SV_Depth; };
UpscaleOutput upscaleDepthPS(FullVertex i) {
    UpscaleOutput o; o.color = float4(catmullRom(i.uv), 1);
    int2 texel = min(int2(i.uv * metrics.zw), int2(metrics.zw) - 1);
    o.depth = worldDepth.Load(int3(texel, 0)); return o;
}
)";
    }
public:
    // sceneVS, then the edge, weight, blend, copy and the two upscale passes;
    // none when the source is unavailable.
    static void shader_programs(std::vector<ShaderProgram>& out) {
        static const std::string source=build_shader_source();
        if(source.empty())return;
        out.push_back({source,"SMAA",nullptr,"sceneVS","vs_4_1",D3DCOMPILE_OPTIMIZATION_LEVEL3});
        for(const char* entry:{"edgePS","weightPS","blendPS","copyPS","upscalePS","upscaleDepthPS"})
            out.push_back({source,"SMAA",nullptr,entry,"ps_4_1",D3DCOMPILE_OPTIMIZATION_LEVEL3});
    }
private:
    HRESULT create_device_resources(ID3D11Device* device) {
        HRESULT hr;
        auto lookup = [&](UINT w,UINT h,UINT pitch,DXGI_FORMAT format,const void* bytes,
                          Ptr<ID3D11ShaderResourceView>& srv) {
            D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.Format=format;
            d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_IMMUTABLE;
            d.BindFlags=D3D11_BIND_SHADER_RESOURCE;D3D11_SUBRESOURCE_DATA data{bytes,pitch,0};
            Ptr<ID3D11Texture2D> texture;HRESULT result=device->CreateTexture2D(&d,&data,&texture);
            return FAILED(result)?result:device->CreateShaderResourceView(texture.Get(),nullptr,&srv);
        };
        if(FAILED(hr=lookup(AREATEX_WIDTH,AREATEX_HEIGHT,AREATEX_PITCH,DXGI_FORMAT_R8G8_UNORM,areaTexBytes,area_)))return hr;
        if(FAILED(hr=lookup(SEARCHTEX_WIDTH,SEARCHTEX_HEIGHT,SEARCHTEX_PITCH,DXGI_FORMAT_R8_UNORM,searchTexBytes,search_)))return hr;
        std::vector<ShaderProgram> programs;
        shader_programs(programs);
        if(programs.empty())return E_FAIL;
        auto compile=[&](const ShaderProgram& program,Ptr<ID3DBlob>& code) {
            Ptr<ID3DBlob> error;HRESULT result=compile_shader(program,&code,&error);
            if(FAILED(result)&&error)OutputDebugStringA(static_cast<const char*>(error->GetBufferPointer()));
            return result;
        };
        Ptr<ID3DBlob> code;
        if(FAILED(hr=compile(programs[0],code)))return hr;
        if(FAILED(hr=device->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&vs_)))return hr;
        Ptr<ID3D11PixelShader>* shaders[]={&edge_ps_,&weight_ps_,&blend_ps_,&copy_ps_,&upscale_ps_,&upscale_depth_ps_};
        for(unsigned i=0;i<6;++i){code.Reset();if(FAILED(hr=compile(programs[1+i],code)))return hr;
            if(FAILED(hr=device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,shaders[i]->GetAddressOf())))return hr;}
        D3D11_SAMPLER_DESC s{};s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
        s.MaxLOD=0;s.Filter=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        if(FAILED(hr=device->CreateSamplerState(&s,&linear_)))return hr;
        D3D11_DEPTH_STENCIL_DESC depth{};depth.DepthEnable=TRUE;depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;
        depth.DepthFunc=D3D11_COMPARISON_ALWAYS;
        if(FAILED(hr=device->CreateDepthStencilState(&depth,&depth_copy_)))return hr;
        s.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;return device->CreateSamplerState(&s,&point_);
    }
public:
    void render(ID3D11DeviceContext* context,ID3D11RenderTargetView* output,bool smaa) const {
        ID3D11ShaderResourceView* empty[3]{};
        context->OMSetRenderTargets(0,nullptr,nullptr);
        context->IASetInputLayout(nullptr);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vs_.Get(),nullptr,0);context->GSSetShader(nullptr,nullptr,0);
        context->HSSetShader(nullptr,nullptr,0);context->DSSetShader(nullptr,nullptr,0);
        context->RSSetState(nullptr);D3D11_VIEWPORT viewport{0,0,float(width_),float(height_),0,1};
        context->RSSetViewports(1,&viewport);context->OMSetDepthStencilState(nullptr,0);
        context->OMSetBlendState(nullptr,nullptr,~0u);context->PSSetConstantBuffers(0,1,metrics_.GetAddressOf());
        ID3D11SamplerState* samplers[]={linear_.Get(),point_.Get()};context->PSSetSamplers(0,2,samplers);
        auto pass=[&](ID3D11RenderTargetView* target,ID3D11PixelShader* pixel,
                      ID3D11ShaderResourceView* a,ID3D11ShaderResourceView* b,ID3D11ShaderResourceView* c) {
            context->PSSetShaderResources(0,3,empty);context->OMSetRenderTargets(1,&target,nullptr);
            ID3D11ShaderResourceView* views[]={a,b,c};context->PSSetShaderResources(0,3,views);
            context->PSSetShader(pixel,nullptr,0);context->Draw(3,0);
        };
        if(smaa){const float zero[4]{};context->ClearRenderTargetView(edges_rtv_.Get(),zero);
            pass(edges_rtv_.Get(),edge_ps_.Get(),color_srv_.Get(),nullptr,nullptr);
            pass(weights_rtv_.Get(),weight_ps_.Get(),edges_srv_.Get(),area_.Get(),search_.Get());}
        pass(output,smaa?blend_ps_.Get():copy_ps_.Get(),color_srv_.Get(),smaa?weights_srv_.Get():nullptr,nullptr);
        context->PSSetShaderResources(0,3,empty);
    }
    // Scaled only: the resolved world image to the output, and the world
    // depth to the interface depth when both are given. One full-screen draw.
    void upscale(ID3D11DeviceContext* context,ID3D11RenderTargetView* output,
                 ID3D11ShaderResourceView* world_depth,ID3D11DepthStencilView* interface_depth) const {
        if(!resolved_)return;
        const bool depth=world_depth&&interface_depth;
        ID3D11ShaderResourceView* empty[4]{};
        context->PSSetShaderResources(0,4,empty);
        context->OMSetRenderTargets(1,&output,depth?interface_depth:nullptr);
        if(depth)context->ClearDepthStencilView(interface_depth,D3D11_CLEAR_STENCIL,1.f,0);
        context->IASetInputLayout(nullptr);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vs_.Get(),nullptr,0);context->GSSetShader(nullptr,nullptr,0);
        context->HSSetShader(nullptr,nullptr,0);context->DSSetShader(nullptr,nullptr,0);
        context->RSSetState(nullptr);D3D11_VIEWPORT viewport{0,0,float(output_width_),float(output_height_),0,1};
        context->RSSetViewports(1,&viewport);context->OMSetDepthStencilState(depth?depth_copy_.Get():nullptr,0);
        context->OMSetBlendState(nullptr,nullptr,~0u);context->PSSetConstantBuffers(0,1,metrics_.GetAddressOf());
        ID3D11SamplerState* samplers[]={linear_.Get(),point_.Get()};context->PSSetSamplers(0,2,samplers);
        ID3D11ShaderResourceView* views[4]={resolved_srv_.Get(),nullptr,nullptr,depth?world_depth:nullptr};
        context->PSSetShaderResources(0,4,views);
        context->PSSetShader(depth?upscale_depth_ps_.Get():upscale_ps_.Get(),nullptr,0);context->Draw(3,0);
        context->PSSetShaderResources(0,4,empty);
    }
};
}
