#pragma once
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstdint>
#include <vector>
#include <new>

namespace owned {
// Convert an immutable GPU blit snapshot before filtering/scaling. The key is
// a packed RGB range in the original surface format, never a range per channel.
// Alpha in the source framebuffer is deliberately ignored, as in CPU blit.
class GpuBlitKey {
    template<class T> using Ptr = Microsoft::WRL::ComPtr<T>;
    Ptr<ID3D11ComputeShader> shader_;
    Ptr<ID3D11Buffer> constants_;
    struct Parameters { UINT masks[4], shifts[4], extent_key[4]; };
    static bool channel(UINT mask, UINT& shift) noexcept {
        shift=0;if(!mask)return true;
        while(!(mask&1u)){mask>>=1;++shift;}
        return mask<=255u && (mask&(mask+1u))==0;
    }
    static UINT pack(UINT byte, UINT mask, UINT shift) noexcept {
        return (((byte*(mask>>shift)+127u)/255u)<<shift)&mask;
    }
public:
    void reset() noexcept { shader_.Reset();constants_.Reset(); }
    HRESULT convert(ID3D11Device* device,ID3D11DeviceContext* context,
                    ID3D11ShaderResourceView* source,UINT width,UINT height,
                    UINT red,UINT green,UINT blue,UINT low,UINT high,
                    Ptr<ID3D11Texture2D>& texture,Ptr<ID3D11ShaderResourceView>& view) {
        if(!device||!context||!source||!width||!height||low>high)return E_INVALIDARG;
        Parameters p{{red,green,blue,0},{},{width,height,low,high}};
        if((red&green)||(red&blue)||(green&blue))return E_INVALIDARG;
        for(unsigned i=0;i<3;++i)if(!channel(p.masks[i],p.shifts[i]))return E_INVALIDARG;
        Ptr<ID3D11Device> source_device,context_device;
        source->GetDevice(&source_device);context->GetDevice(&context_device);
        if(source_device.Get()!=device||context_device.Get()!=device)return E_INVALIDARG;
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};source->GetDesc(&sd);
        if(sd.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2D||sd.Texture2D.MostDetailedMip!=0||
           (sd.Format!=DXGI_FORMAT_B8G8R8A8_UNORM&&sd.Format!=DXGI_FORMAT_R8G8B8A8_UNORM))return E_NOTIMPL;
        Ptr<ID3D11Resource> resource;source->GetResource(&resource);
        Ptr<ID3D11Texture2D> input;HRESULT hr=resource.As(&input);if(FAILED(hr))return hr;
        D3D11_TEXTURE2D_DESC td{};input->GetDesc(&td);
        if(td.Width!=width||td.Height!=height||td.ArraySize!=1||td.SampleDesc.Count!=1)return E_INVALIDARG;
        D3D11_TEXTURE2D_DESC output{};output.Width=width;output.Height=height;
        output.MipLevels=output.ArraySize=output.SampleDesc.Count=1;
        output.Format=DXGI_FORMAT_R8G8B8A8_UNORM;output.Usage=D3D11_USAGE_DEFAULT;
        output.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        Ptr<ID3D11Texture2D> result;Ptr<ID3D11ShaderResourceView> result_view;
        if(device->GetFeatureLevel()<D3D_FEATURE_LEVEL_11_0){
            // Older feature levels cannot write a typed UNORM UAV. Retain the
            // same conversion using readback, without modifying the source.
            td.Usage=D3D11_USAGE_STAGING;td.BindFlags=0;
            td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;td.MiscFlags=0;
            Ptr<ID3D11Texture2D> staging;if(FAILED(hr=device->CreateTexture2D(&td,nullptr,&staging)))return hr;
            context->CopyResource(staging.Get(),input.Get());
            std::vector<UINT> pixels;
            try{pixels.resize(size_t(width)*height);}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if(FAILED(hr=context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)))return hr;
            for(UINT y=0;y<height;++y)for(UINT x=0;x<width;++x){
                const UINT raw=reinterpret_cast<const UINT*>(static_cast<const char*>(mapped.pData)+size_t(y)*mapped.RowPitch)[x];
                const UINT r=(raw>>(sd.Format==DXGI_FORMAT_B8G8R8A8_UNORM?16:0))&255u;
                const UINT g=(raw>>8)&255u,b=(raw>>(sd.Format==DXGI_FORMAT_B8G8R8A8_UNORM?0:16))&255u;
                const UINT key=pack(r,red,p.shifts[0])|pack(g,green,p.shifts[1])|pack(b,blue,p.shifts[2]);
                pixels[size_t(y)*width+x]=r|(g<<8)|(b<<16)|((key>=low&&key<=high)?0u:0xff000000u);
            }
            context->Unmap(staging.Get(),0);
            D3D11_SUBRESOURCE_DATA data{pixels.data(),width*4,0};
            if(FAILED(hr=device->CreateTexture2D(&output,&data,&result)))return hr;
        }else{
            if(!shader_){
                static const char code[]=R"(
Texture2D<float4> source:register(t0);
RWTexture2D<float4> target:register(u0);
cbuffer Key:register(b0){uint4 masks;uint4 shifts;uint4 extent_key;};
[numthreads(8,8,1)] void main(uint3 id:SV_DispatchThreadID){
 if(any(id.xy>=extent_key.xy))return;
 float4 pixel=source.Load(int3(id.xy,0));
 uint3 bytes=(uint3)floor(saturate(pixel.rgb)*255.0+0.5);
 uint3 packed=(((bytes*(masks.xyz>>shifts.xyz)+127u)/255u)<<shifts.xyz)&masks.xyz;
 uint key=packed.x|packed.y|packed.z;
 target[id.xy]=float4(pixel.rgb,(key>=extent_key.z&&key<=extent_key.w)?0.0:1.0);
})";
                Ptr<ID3DBlob> bytecode,error;
                if(FAILED(hr=D3DCompile(code,sizeof(code)-1,nullptr,nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&bytecode,&error)))return hr;
                if(FAILED(hr=device->CreateComputeShader(bytecode->GetBufferPointer(),bytecode->GetBufferSize(),nullptr,&shader_)))return hr;
            }
            if(!constants_){
                D3D11_BUFFER_DESC bd{};bd.ByteWidth=sizeof(p);bd.Usage=D3D11_USAGE_DEFAULT;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
                if(FAILED(hr=device->CreateBuffer(&bd,nullptr,&constants_)))return hr;
            }
            output.BindFlags|=D3D11_BIND_UNORDERED_ACCESS;
            if(FAILED(hr=device->CreateTexture2D(&output,nullptr,&result)))return hr;
            Ptr<ID3D11UnorderedAccessView> uav;
            if(FAILED(hr=device->CreateUnorderedAccessView(result.Get(),nullptr,&uav)))return hr;
            Ptr<ID3D11ComputeShader> old_shader;Ptr<ID3D11Buffer> old_constants;
            Ptr<ID3D11ShaderResourceView> old_source;Ptr<ID3D11UnorderedAccessView> old_target;
            ID3D11ClassInstance* instances[D3D11_SHADER_MAX_INTERFACES]{};UINT count=D3D11_SHADER_MAX_INTERFACES;
            context->CSGetShader(&old_shader,instances,&count);
            context->CSGetConstantBuffers(0,1,&old_constants);context->CSGetShaderResources(0,1,&old_source);
            context->CSGetUnorderedAccessViews(0,1,&old_target);
            context->UpdateSubresource(constants_.Get(),0,nullptr,&p,0,0);
            context->CSSetShader(shader_.Get(),nullptr,0);context->CSSetConstantBuffers(0,1,constants_.GetAddressOf());
            context->CSSetShaderResources(0,1,&source);context->CSSetUnorderedAccessViews(0,1,uav.GetAddressOf(),nullptr);
            context->Dispatch((width+7)/8,(height+7)/8,1);
            ID3D11UnorderedAccessView* empty=nullptr;context->CSSetUnorderedAccessViews(0,1,&empty,nullptr);
            context->CSSetShaderResources(0,1,old_source.GetAddressOf());
            context->CSSetUnorderedAccessViews(0,1,old_target.GetAddressOf(),nullptr);
            context->CSSetConstantBuffers(0,1,old_constants.GetAddressOf());context->CSSetShader(old_shader.Get(),instances,count);
            for(UINT i=0;i<count;++i)instances[i]->Release();
        }
        if(FAILED(hr=device->CreateShaderResourceView(result.Get(),nullptr,&result_view)))return hr;
        texture=std::move(result);view=std::move(result_view);return S_OK;
    }
};
}
