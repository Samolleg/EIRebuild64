#pragma once
#include <d3dcompiler.h>

// Offscreen WARP readback of the production texel/opacity values under the
// retained path's SRCALPHA/INVSRCALPHA policy. This is not a game screenshot.
inline void check_astral_alpha_gpu() {
    using Microsoft::WRL::ComPtr;
    const auto ok=[](HRESULT h){if(FAILED(h))throw std::runtime_error("astral WARP setup/readback failed");};
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    ok(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context));
    const auto source=owned::astral_border_plain_texture();
    D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=desc.MipLevels=desc.ArraySize=1;desc.SampleDesc.Count=1;
    desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA bytes{source->source->data(),4,4};ComPtr<ID3D11Texture2D> texture;
    ok(device->CreateTexture2D(&desc,&bytes,&texture));ComPtr<ID3D11ShaderResourceView> srv;ok(device->CreateShaderResourceView(texture.Get(),nullptr,&srv));
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> target,staging;ok(device->CreateTexture2D(&desc,nullptr,&target));
    ComPtr<ID3D11RenderTargetView> rtv;ok(device->CreateRenderTargetView(target.Get(),nullptr,&rtv));
    desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ok(device->CreateTexture2D(&desc,nullptr,&staging));
    const char* hlsl="Texture2D image:register(t0);cbuffer Opacity:register(b0){float4 opacity;} float4 vs(uint id:SV_VertexID):SV_Position {float2 p[3]={float2(-1,-1),float2(-1,3),float2(3,-1)};return float4(p[id],0,1);} float4 ps():SV_Target{return image.Load(int3(0,0,0))*float4(1,1,1,opacity.x);}";
    ComPtr<ID3DBlob> vs,ps,error;ok(D3DCompile(hlsl,std::strlen(hlsl),nullptr,nullptr,nullptr,"vs","vs_5_0",0,0,&vs,&error));
    ok(D3DCompile(hlsl,std::strlen(hlsl),nullptr,nullptr,nullptr,"ps","ps_5_0",0,0,&ps,&error));
    ComPtr<ID3D11VertexShader> vertex;ComPtr<ID3D11PixelShader> pixel;
    ok(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vertex));ok(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&pixel));
    D3D11_BUFFER_DESC bd{};bd.ByteWidth=16;bd.Usage=D3D11_USAGE_DEFAULT;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;ComPtr<ID3D11Buffer> cb;ok(device->CreateBuffer(&bd,nullptr,&cb));
    D3D11_BLEND_DESC blend{};auto& rt=blend.RenderTarget[0];rt.BlendEnable=TRUE;rt.SrcBlend=D3D11_BLEND_SRC_ALPHA;rt.DestBlend=D3D11_BLEND_INV_SRC_ALPHA;rt.BlendOp=D3D11_BLEND_OP_ADD;
    rt.SrcBlendAlpha=D3D11_BLEND_ONE;rt.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;rt.BlendOpAlpha=D3D11_BLEND_OP_ADD;rt.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
    ComPtr<ID3D11BlendState> bs;ok(device->CreateBlendState(&blend,&bs));context->OMSetBlendState(bs.Get(),nullptr,~0u);
    D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;ComPtr<ID3D11RasterizerState> rs;ok(device->CreateRasterizerState(&raster,&rs));context->RSSetState(rs.Get());
    D3D11_VIEWPORT viewport{0,0,1,1,0,1};context->RSSetViewports(1,&viewport);context->OMSetRenderTargets(1,rtv.GetAddressOf(),nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);context->VSSetShader(vertex.Get(),nullptr,0);context->PSSetShader(pixel.Get(),nullptr,0);
    context->PSSetShaderResources(0,1,srv.GetAddressOf());context->PSSetConstantBuffers(0,1,cb.GetAddressOf());
    const float background[]={20/255.f,50/255.f,80/255.f,1};const float color[]={189,171,233};
    for(const double seconds:{0.,1.5,4.5})for(unsigned layers:{1u,4u}){
        owned::NativePreparedPartValues values;owned::astral_border_opacity(seconds/.055,values);
        const float constants[]={values.alpha,0,0,0};context->UpdateSubresource(cb.Get(),0,nullptr,constants,0,0);context->ClearRenderTargetView(rtv.Get(),background);
        for(unsigned i=0;i<layers;++i)context->Draw(3,0);
        context->CopyResource(staging.Get(),target.Get());D3D11_MAPPED_SUBRESOURCE mapped{};ok(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped));
        const auto* actual=static_cast<const unsigned char*>(mapped.pData);bool matches=true;
        const double transmission=std::pow(1-48/255.0*values.alpha,layers);
        for(unsigned c=0;c<3;++c){const double expected=background[c]*255*transmission+color[c]*(1-transmission);matches&=std::abs(actual[c]-expected)<3;}
        std::printf("WARP alpha: %.1fs, %u layer(s), RGB %u/%u/%u, background %.3f\n",seconds,layers,actual[0],actual[1],actual[2],transmission);
        context->Unmap(staging.Get(),0);if(!matches||transmission<=.43)throw std::runtime_error("astral WARP alpha became opaque or mismatched blend");
    }
}
