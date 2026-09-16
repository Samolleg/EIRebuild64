#pragma once
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <vector>
#include <stdexcept>
#pragma comment(lib, "d3dcompiler.lib")

// Direct engine integration of the mod's maximum-opacity composition.
// No original-executable addresses or DirectDraw device hooks.
class ModShadowDX11 {
    template<class T> using Ptr = Microsoft::WRL::ComPtr<T>;
    ID3D11Device *d;
    ID3D11DeviceContext *c;
    Ptr<ID3D11Texture2D> terrainTexture;
    Ptr<ID3D11ShaderResourceView> terrainSRV, terrainSavedSource;
    unsigned terrainWidth=0,terrainHeight=0;
    Ptr<ID3D11Texture2D> mask;
    Ptr<ID3D11RenderTargetView> maskRT;
    Ptr<ID3D11ShaderResourceView> maskSRV;
    Ptr<ID3D11VertexShader> fullscreen;
    Ptr<ID3D11PixelShader> coverage, terrainCoverage, composite, alpha;
    Ptr<ID3D11BlendState> maximum, multiply;
    Ptr<ID3D11DepthStencilState> depthRead, depthOff;
    Ptr<ID3D11RasterizerState> noCull;
    Ptr<ID3D11SamplerState> linear;
    UINT width=0, height=0;
    struct State {
        Ptr<ID3D11RenderTargetView> rt;
        Ptr<ID3D11DepthStencilView> ds;
        Ptr<ID3D11BlendState> blend;
        Ptr<ID3D11DepthStencilState> depth;
        Ptr<ID3D11RasterizerState> raster;
        Ptr<ID3D11VertexShader> vs;
        Ptr<ID3D11PixelShader> ps;
        Ptr<ID3D11InputLayout> layout;
        Ptr<ID3D11ShaderResourceView> srv;
        Ptr<ID3D11SamplerState> sampler;
        D3D11_PRIMITIVE_TOPOLOGY topology;
        D3D11_VIEWPORT viewport[16]; UINT count=16, stencil=0, sampleMask=0;
        FLOAT factor[4];
        void Save(ID3D11DeviceContext *c) {
            c->OMGetRenderTargets(1,rt.ReleaseAndGetAddressOf(),ds.ReleaseAndGetAddressOf());
            c->OMGetBlendState(blend.ReleaseAndGetAddressOf(),factor,&sampleMask);
            c->OMGetDepthStencilState(depth.ReleaseAndGetAddressOf(),&stencil);
            c->RSGetState(raster.ReleaseAndGetAddressOf()); count=16;c->RSGetViewports(&count,viewport);
            c->VSGetShader(vs.ReleaseAndGetAddressOf(),nullptr,nullptr);
            c->PSGetShader(ps.ReleaseAndGetAddressOf(),nullptr,nullptr);
            c->IAGetInputLayout(layout.ReleaseAndGetAddressOf());c->IAGetPrimitiveTopology(&topology);
            c->PSGetShaderResources(0,1,srv.ReleaseAndGetAddressOf());
            c->PSGetSamplers(0,1,sampler.ReleaseAndGetAddressOf());
        }
        void Restore(ID3D11DeviceContext *c) {
            ID3D11ShaderResourceView *nil=nullptr;c->PSSetShaderResources(0,1,&nil);
            c->OMSetRenderTargets(1,rt.GetAddressOf(),ds.Get());
            c->OMSetBlendState(blend.Get(),factor,sampleMask);c->OMSetDepthStencilState(depth.Get(),stencil);
            c->RSSetState(raster.Get());c->RSSetViewports(count,viewport);
            c->VSSetShader(vs.Get(),nullptr,0);c->PSSetShader(ps.Get(),nullptr,0);
            c->IASetInputLayout(layout.Get());c->IASetPrimitiveTopology(topology);
            c->PSSetShaderResources(0,1,srv.GetAddressOf());c->PSSetSamplers(0,1,sampler.GetAddressOf());
        }
    } saved;
    static void Check(HRESULT hr) { if(FAILED(hr)) throw std::runtime_error("Mod shadow DX11 operation failed"); }
    Ptr<ID3DBlob> Compile(const char *code,const char *entry,const char *target) {
        Ptr<ID3DBlob> out,errors;
        HRESULT hr=D3DCompile(code,strlen(code),"ModShadowDX11",nullptr,nullptr,entry,target,
            D3DCOMPILE_ENABLE_STRICTNESS,0,out.GetAddressOf(),errors.GetAddressOf());
        if(FAILED(hr))throw std::runtime_error(errors ? static_cast<const char*>(errors->GetBufferPointer()) : "shadow shader compile failed");
        return out;
    }
    void Fullscreen(ID3D11PixelShader *ps) {
        c->IASetInputLayout(nullptr);c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        c->VSSetShader(fullscreen.Get(),nullptr,0);c->PSSetShader(ps,nullptr,0);
        c->RSSetState(noCull.Get());c->OMSetDepthStencilState(depthOff.Get(),0);
        c->PSSetSamplers(0,1,linear.GetAddressOf());c->Draw(3,0);
    }
public:
    bool active=false,terrainPass=false;
    unsigned TerrainWidth()const{return terrainWidth;}
    unsigned TerrainHeight()const{return terrainHeight;}
    ModShadowDX11(ID3D11Device *device,ID3D11DeviceContext *context):d(device),c(context) {
        const char *shader=R"(
Texture2D source:register(t0); SamplerState samp:register(s0);
struct V {float4 position:SV_POSITION;float2 uv:TEXCOORD;};
V vs(uint id:SV_VertexID) { V o;o.uv=float2((id<<1)&2,id&2);o.position=float4(o.uv*float2(2,-2)+float2(-1,1),0,1);return o; }
float4 copy(V i):SV_TARGET {return source.Sample(samp,i.uv).rrrr;}
float4 readAlpha(V i):SV_TARGET {return source.SampleLevel(samp,i.uv,0).aaaa;}
struct P {float4 position:SV_POSITION;float4 color:COLOR;float4 specular:SPECULAR;float fogFactor:FOG;float2 uv:TEXCOORD;};
// ContinuousShadowEffectiveOpacity from ddraw_proxy.cpp/e2c4867, selected INI curve.
float opacity(float raw) {
    uint x=(uint)clamp(floor(raw*255+0.5),0,255);
    uint anchors[6]={1,17,33,65,113,177};uint values[6]={6,14,24,35,49,64};
    uint px=0,py=0;if(x==0)return 0;
    [unroll]for(uint n=0;n<6;n++){if(x<=anchors[n]){uint span=anchors[n]-px;return (py+((x-px)*(values[n]-py)+span/2)/span)/255.0;}px=anchors[n];py=values[n];}
    return py/255.0;
}
float4 terrainShadow(P i):SV_TARGET {float value=opacity(source.Sample(samp,i.uv).r)*saturate(i.fogFactor);return value.xxxx;}
float4 shadow(P i):SV_TARGET {
    float a=source.Sample(samp,i.uv).a;
    float value=opacity(a)*saturate(i.color.a*255/128)*saturate(i.fogFactor);
    return float4(value,value,value,value);
})";
        auto vs=Compile(shader,"vs","vs_4_0");Check(d->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,fullscreen.GetAddressOf()));
        auto ps=Compile(shader,"shadow","ps_4_0");Check(d->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,coverage.GetAddressOf()));
        ps=Compile(shader,"terrainShadow","ps_4_0");Check(d->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,terrainCoverage.GetAddressOf()));
        ps=Compile(shader,"copy","ps_4_0");Check(d->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,composite.GetAddressOf()));
        ps=Compile(shader,"readAlpha","ps_4_0");Check(d->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,alpha.GetAddressOf()));
        D3D11_BLEND_DESC b={};auto &rt=b.RenderTarget[0];rt.BlendEnable=TRUE;rt.SrcBlend=rt.DestBlend=D3D11_BLEND_ONE;rt.BlendOp=D3D11_BLEND_OP_MAX;rt.SrcBlendAlpha=rt.DestBlendAlpha=D3D11_BLEND_ONE;rt.BlendOpAlpha=D3D11_BLEND_OP_MAX;rt.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        Check(d->CreateBlendState(&b,maximum.GetAddressOf()));
        rt.SrcBlend=D3D11_BLEND_ZERO;rt.DestBlend=D3D11_BLEND_INV_SRC_COLOR;rt.BlendOp=D3D11_BLEND_OP_ADD;rt.RenderTargetWriteMask=7;
        Check(d->CreateBlendState(&b,multiply.GetAddressOf()));
        D3D11_DEPTH_STENCIL_DESC ds={};ds.DepthEnable=TRUE;ds.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;ds.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;
        Check(d->CreateDepthStencilState(&ds,depthRead.GetAddressOf()));ds.DepthEnable=FALSE;Check(d->CreateDepthStencilState(&ds,depthOff.GetAddressOf()));
        D3D11_RASTERIZER_DESC rs={};rs.FillMode=D3D11_FILL_SOLID;rs.CullMode=D3D11_CULL_NONE;rs.DepthClipEnable=TRUE;Check(d->CreateRasterizerState(&rs,noCull.GetAddressOf()));
        D3D11_SAMPLER_DESC sm={};sm.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sm.AddressU=sm.AddressV=sm.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sm.MaxLOD=D3D11_FLOAT32_MAX;Check(d->CreateSamplerState(&sm,linear.GetAddressOf()));
    }
    void Begin(UINT w,UINT h) {
        if(active)throw std::runtime_error("nested shadow pass");
        saved.Save(c);
        if(w!=width||h!=height){maskRT.Reset();maskSRV.Reset();mask.Reset();D3D11_TEXTURE2D_DESC t={};t.Width=w;t.Height=h;t.MipLevels=t.ArraySize=t.SampleDesc.Count=1;t.Format=DXGI_FORMAT_R8_UNORM;t.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
            Check(d->CreateTexture2D(&t,nullptr,mask.GetAddressOf()));Check(d->CreateRenderTargetView(mask.Get(),nullptr,maskRT.GetAddressOf()));Check(d->CreateShaderResourceView(mask.Get(),nullptr,maskSRV.GetAddressOf()));width=w;height=h;}
        FLOAT zero[4]={};c->ClearRenderTargetView(maskRT.Get(),zero);c->OMSetRenderTargets(1,maskRT.GetAddressOf(),saved.ds.Get());active=true;Bind();
    }
    void Bind() {c->OMSetBlendState(maximum.Get(),nullptr,~0u);c->OMSetDepthStencilState(depthRead.Get(),0);c->RSSetState(noCull.Get());c->PSSetShader(terrainPass?terrainCoverage.Get():coverage.Get(),nullptr,0);c->PSSetSamplers(0,1,linear.GetAddressOf());}
    void End() {
        if(!active)return;
        c->OMSetRenderTargets(1,saved.rt.GetAddressOf(),nullptr);c->OMSetBlendState(multiply.Get(),nullptr,~0u);c->PSSetShaderResources(0,1,maskSRV.GetAddressOf());Fullscreen(composite.Get());saved.Restore(c);active=false;
    }
    // Normalize arbitrary BC/16/32-bit foliage textures to the original 256-square alpha input.
    std::vector<WORD> ReadAlpha(ID3D11ShaderResourceView *source) {
        State state;state.Save(c);
        struct Restore {State &s;ID3D11DeviceContext*c;~Restore(){s.Restore(c);}} restore{state,c};
        D3D11_TEXTURE2D_DESC t={};t.Width=t.Height=256;t.MipLevels=t.ArraySize=t.SampleDesc.Count=1;t.Format=DXGI_FORMAT_R8_UNORM;t.BindFlags=D3D11_BIND_RENDER_TARGET;
        Ptr<ID3D11Texture2D> tmp,staging;Ptr<ID3D11RenderTargetView> rt;
        Check(d->CreateTexture2D(&t,nullptr,tmp.GetAddressOf()));Check(d->CreateRenderTargetView(tmp.Get(),nullptr,rt.GetAddressOf()));
        c->OMSetRenderTargets(1,rt.GetAddressOf(),nullptr);c->OMSetBlendState(nullptr,nullptr,~0u);D3D11_VIEWPORT vp={0,0,256,256,0,1};c->RSSetViewports(1,&vp);c->PSSetShaderResources(0,1,&source);Fullscreen(alpha.Get());
        t.BindFlags=0;t.Usage=D3D11_USAGE_STAGING;t.CPUAccessFlags=D3D11_CPU_ACCESS_READ;Check(d->CreateTexture2D(&t,nullptr,staging.GetAddressOf()));c->CopyResource(staging.Get(),tmp.Get());
        D3D11_MAPPED_SUBRESOURCE m={};Check(c->Map(staging.Get(),0,D3D11_MAP_READ,0,&m));std::vector<WORD> result(256*256);
        for(UINT y=0;y<256;y++)for(UINT x=0;x<256;x++)result[y*256+x]=static_cast<BYTE*>(m.pData)[y*m.RowPitch+x]>=128?0x8000:0;
        c->Unmap(staging.Get(),0);return result;
    }
    void UpdateTerrain(unsigned w,unsigned h,const unsigned char *pixels) {
        if(w!=terrainWidth||h!=terrainHeight){terrainSRV.Reset();terrainTexture.Reset();D3D11_TEXTURE2D_DESC t={};t.Width=w;t.Height=h;t.MipLevels=t.ArraySize=t.SampleDesc.Count=1;t.Format=DXGI_FORMAT_R8_UNORM;t.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            Check(d->CreateTexture2D(&t,nullptr,terrainTexture.GetAddressOf()));Check(d->CreateShaderResourceView(terrainTexture.Get(),nullptr,terrainSRV.GetAddressOf()));terrainWidth=w;terrainHeight=h;}
        c->UpdateSubresource(terrainTexture.Get(),0,nullptr,pixels,w,0);
    }
    void SetTerrainPass(bool enabled){
        if(enabled)c->PSGetShaderResources(0,1,terrainSavedSource.ReleaseAndGetAddressOf());
        terrainPass=enabled;Bind();
        if(!enabled){c->PSSetShaderResources(0,1,terrainSavedSource.GetAddressOf());terrainSavedSource.Reset();}
    }
    void BindTerrainResource(){c->PSSetShaderResources(0,1,terrainSRV.GetAddressOf());}
};
