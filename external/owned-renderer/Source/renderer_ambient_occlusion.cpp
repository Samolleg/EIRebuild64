// Ambient occlusion on the ambient term only; see ambient_occlusion.h for the
// contract. Runs once per world frame at the composite boundary, after all
// world geometry (opaque and transparent) and before SMAA and the interface.
#include "renderer.h"
#include "ambient_occlusion.h"
#include "world_fog.h"

#include <d3dcompiler.h>
#include <cstring>

namespace owned {

void Renderer::release_ambient_occlusion_targets() noexcept {
    ao_ambient_texture_.Reset();ao_raw_texture_.Reset();ao_filtered_texture_.Reset();ao_depth_texture_.Reset();
    ao_ambient_rtv_.Reset();ao_raw_rtv_.Reset();ao_filtered_rtv_.Reset();ao_depth_rtv_.Reset();
    ao_ambient_srv_.Reset();ao_raw_srv_.Reset();ao_srv_.Reset();ao_depth_srv_.Reset();
    ao_divisor_=0;ao_ambient_written_=false;
}

// Called at a composite boundary so a whole world frame either writes the
// ambient share or does not.
void append_ambient_occlusion_shader_programs(std::vector<ShaderProgram>& out){
    const std::string_view source(kAmbientOcclusionShader,sizeof(kAmbientOcclusionShader)-1);
    out.push_back({source,nullptr,nullptr,"aoFullscreenVS","vs_4_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
    for(const char* entry:{"aoPreparePS","aoHorizonPS","aoDenoisePS","aoResolvePS"})
        out.push_back({source,nullptr,nullptr,entry,"ps_4_0",D3DCOMPILE_OPTIMIZATION_LEVEL3});
}

HRESULT Renderer::update_ambient_occlusion_targets() noexcept {
    if(!device_||!context_)return E_UNEXPECTED;
    const auto preset=ambient_occlusion_preset(configured_integer(debug_overlay_.config(),"ao_quality",2));
    if(!preset.divisor){release_ambient_occlusion_targets();return S_OK;}
    const UINT low_width=ambient_occlusion_extent(width_,preset.divisor);
    const UINT low_height=ambient_occlusion_extent(height_,preset.divisor);
    if(ao_ambient_rtv_&&ao_divisor_==preset.divisor){
        D3D11_TEXTURE2D_DESC full{},low{};ao_ambient_texture_->GetDesc(&full);ao_raw_texture_->GetDesc(&low);
        if(full.Width==width_&&full.Height==height_&&low.Width==low_width&&low.Height==low_height)return S_OK;
    }
    release_ambient_occlusion_targets();
    HRESULT hr=S_OK;
    if(!ao_vs_){
        // A failed pipeline stays failed until the device is recreated; do not
        // recompile every world frame.
        if(ao_pipeline_failed_)return E_FAIL;
        ao_pipeline_failed_=true;
        ComPtr<ID3DBlob> vs,prepare,horizon,denoise,resolve,error;
        std::vector<ShaderProgram> programs;append_ambient_occlusion_shader_programs(programs);
        if(FAILED(hr=compile_shader(programs[0],&vs,&error))||FAILED(hr=compile_shader(programs[1],&prepare,&error))||
           FAILED(hr=compile_shader(programs[2],&horizon,&error))||
           FAILED(hr=compile_shader(programs[3],&denoise,&error))||FAILED(hr=compile_shader(programs[4],&resolve,&error))){
            OutputDebugStringA("EID3D11 ambient occlusion shader compilation failed; AO omitted\n");
            if(error)OutputDebugStringA(static_cast<const char*>(error->GetBufferPointer()));
            return hr;
        }
        ComPtr<ID3D11VertexShader> vertex;ComPtr<ID3D11PixelShader> p,h,d,r;ComPtr<ID3D11Buffer> constants;
        ComPtr<ID3D11BlendState> blend;
        if(FAILED(hr=create_shader(device_.Get(),vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vertex))||
           FAILED(hr=create_shader(device_.Get(),prepare->GetBufferPointer(),prepare->GetBufferSize(),nullptr,&p))||
           FAILED(hr=create_shader(device_.Get(),horizon->GetBufferPointer(),horizon->GetBufferSize(),nullptr,&h))||
           FAILED(hr=create_shader(device_.Get(),denoise->GetBufferPointer(),denoise->GetBufferSize(),nullptr,&d))||
           FAILED(hr=create_shader(device_.Get(),resolve->GetBufferPointer(),resolve->GetBufferSize(),nullptr,&r)))return hr;
        D3D11_BUFFER_DESC cb{};cb.ByteWidth=sizeof(AmbientOcclusionConstants);cb.Usage=D3D11_USAGE_DYNAMIC;
        cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;cb.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        if(FAILED(hr=device_->CreateBuffer(&cb,nullptr,&constants)))return hr;
        // scene = scene - occluded ambient share; destination alpha is kept.
        D3D11_BLEND_DESC bd{};auto& rt=bd.RenderTarget[0];rt.BlendEnable=TRUE;
        rt.SrcBlend=D3D11_BLEND_ONE;rt.DestBlend=D3D11_BLEND_ONE;rt.BlendOp=D3D11_BLEND_OP_REV_SUBTRACT;
        rt.SrcBlendAlpha=D3D11_BLEND_ZERO;rt.DestBlendAlpha=D3D11_BLEND_ONE;rt.BlendOpAlpha=D3D11_BLEND_OP_ADD;
        rt.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        if(FAILED(hr=device_->CreateBlendState(&bd,&blend)))return hr;
        ao_vs_=std::move(vertex);ao_prepare_ps_=std::move(p);ao_horizon_ps_=std::move(h);ao_denoise_ps_=std::move(d);ao_resolve_ps_=std::move(r);
        ao_constants_=std::move(constants);ao_resolve_blend_=std::move(blend);
        ao_pipeline_failed_=false;
    }
    auto target=[&](UINT w,UINT h,DXGI_FORMAT format,ComPtr<ID3D11Texture2D>& texture,
                    ComPtr<ID3D11RenderTargetView>& rtv,ComPtr<ID3D11ShaderResourceView>& srv){
        D3D11_TEXTURE2D_DESC td{};td.Width=w;td.Height=h;td.MipLevels=td.ArraySize=td.SampleDesc.Count=1;
        td.Format=format;td.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        HRESULT result=device_->CreateTexture2D(&td,nullptr,&texture);
        if(SUCCEEDED(result))result=device_->CreateRenderTargetView(texture.Get(),nullptr,&rtv);
        if(SUCCEEDED(result))result=device_->CreateShaderResourceView(texture.Get(),nullptr,&srv);
        return result;};
    // Alpha carries the snow exclusion alongside the existing ambient share.
    // GPU-only storage; no CPU image or per-instance allocation is introduced.
    if(FAILED(hr=target(width_,height_,DXGI_FORMAT_R16G16B16A16_FLOAT,ao_ambient_texture_,ao_ambient_rtv_,ao_ambient_srv_))||
       FAILED(hr=target(low_width,low_height,DXGI_FORMAT_R16G16_FLOAT,ao_raw_texture_,ao_raw_rtv_,ao_raw_srv_))||
       FAILED(hr=target(low_width,low_height,DXGI_FORMAT_R16G16_FLOAT,ao_filtered_texture_,ao_filtered_rtv_,ao_srv_))||
       FAILED(hr=target(low_width,low_height,DXGI_FORMAT_R32_FLOAT,ao_depth_texture_,ao_depth_rtv_,ao_depth_srv_))){
        release_ambient_occlusion_targets();
        OutputDebugStringA("EID3D11 ambient occlusion targets unavailable; AO omitted\n");
        return hr;
    }
    const float zero[4]{};context_->ClearRenderTargetView(ao_ambient_rtv_.Get(),zero);
    ao_divisor_=preset.divisor;
    return S_OK;
}

HRESULT Renderer::render_ambient_occlusion() noexcept {
    if(!ao_ambient_rtv_||!depth_srv_||!ao_vs_)return S_FALSE;
    const float zero[4]{};
    const auto preset=ambient_occlusion_preset(configured_integer(debug_overlay_.config(),"ao_quality",2));
    // The camera that produced this frame's depth. Without it there is no
    // position reconstruction; the frame keeps its unoccluded ambient.
    const auto scene=bridge_scene_world_to_clip_snapshot();
    AmbientOcclusionConstants constants{};
    if(preset.divisor!=ao_divisor_||!scene.valid||
       !ambient_occlusion_invert(scene.world_to_clip.data(),constants.clip_to_world)){
        context_->ClearRenderTargetView(ao_ambient_rtv_.Get(),zero);return S_FALSE;
    }
    const auto& config=debug_overlay_.config();
    // Night and precipitation from the game's clock and weather (world_fog.h).
    const auto environment=bridge_world_fog_snapshot().environment;
    const float low_light=environment.valid?(std::max)(fog_night(environment.hour),
        environment.cave?0.f:.6f*fog_precipitation(environment)):0.f;
    const auto frame=ambient_occlusion_frame(configured_floating_point(config,"ao_strength",.75f),
        configured_floating_point(config,"ao_radius",1.5f),configured_floating_point(config,"ao_low_light_boost",1.6f),
        native_world_lighting_.ambient.data(),native_world_lighting_.sunlight_diffuse.data(),low_light);
    const UINT low_width=ambient_occlusion_extent(width_,ao_divisor_),low_height=ambient_occlusion_extent(height_,ao_divisor_);
    std::memcpy(constants.world_to_clip,scene.world_to_clip.data(),sizeof(constants.world_to_clip));
    const float full[4]={float(width_),float(height_),1.f/width_,1.f/height_};
    const float low[4]={float(low_width),float(low_height),1.f/low_width,1.f/low_height};
    std::memcpy(constants.full_size,full,sizeof(full));std::memcpy(constants.low_size,low,sizeof(low));
    // Screen radius is capped so near-camera surfaces do not thrash the cache.
    const float params[4]={frame.radius,(std::max)(8.f,height_*.08f),frame.strength,.03f*ao_divisor_};
    std::memcpy(constants.params,params,sizeof(params));
    constants.control[0]=ao_divisor_;constants.control[1]=preset.slices;constants.control[2]=preset.steps;
    D3D11_MAPPED_SUBRESOURCE mapped{};
    HRESULT hr=context_->Map(ao_constants_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped);if(FAILED(hr))return hr;
    std::memcpy(mapped.pData,&constants,sizeof(constants));context_->Unmap(ao_constants_.Get(),0);

    transition_profile_pass("ambient_occlusion");
    ID3D11ShaderResourceView* none[3]{};context_->PSSetShaderResources(0,3,none);
    const float blend_factor[4]={1,1,1,1};
    context_->OMSetBlendState(nullptr,blend_factor,0xffffffffu);context_->OMSetDepthStencilState(nullptr,0);
    context_->RSSetState(nullptr);
    context_->IASetInputLayout(nullptr);context_->IASetVertexBuffers(0,0,nullptr,nullptr,nullptr);
    context_->IASetIndexBuffer(nullptr,DXGI_FORMAT_UNKNOWN,0);context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(ao_vs_.Get(),nullptr,0);context_->PSSetConstantBuffers(1,1,ao_constants_.GetAddressOf());
    // Compact low-resolution depth, then horizon AO from the finished world depth.
    context_->OMSetRenderTargets(1,ao_depth_rtv_.GetAddressOf(),nullptr);
    const D3D11_VIEWPORT low_view{0,0,float(low_width),float(low_height),0,1};context_->RSSetViewports(1,&low_view);
    context_->PSSetShaderResources(0,1,depth_srv_.GetAddressOf());
    context_->PSSetShaderResources(2,1,ao_ambient_srv_.GetAddressOf());
    context_->PSSetShader(ao_prepare_ps_.Get(),nullptr,0);context_->Draw(3,0);
    context_->OMSetRenderTargets(1,ao_raw_rtv_.GetAddressOf(),nullptr);
    context_->PSSetShaderResources(1,1,ao_depth_srv_.GetAddressOf());
    context_->PSSetShader(ao_horizon_ps_.Get(),nullptr,0);context_->Draw(3,0);
    // Depth-aware spatial denoise.
    transition_profile_pass("ambient_occlusion_resolve");
    context_->PSSetShaderResources(1,1,none);
    context_->OMSetRenderTargets(1,ao_filtered_rtv_.GetAddressOf(),nullptr);
    context_->PSSetShaderResources(1,1,ao_raw_srv_.GetAddressOf());
    context_->PSSetShader(ao_denoise_ps_.Get(),nullptr,0);context_->Draw(3,0);
    // Depth-aware upsample and removal of the occluded ambient share.
    context_->PSSetShaderResources(1,1,none);
    context_->OMSetRenderTargets(1,rtv_.GetAddressOf(),nullptr);
    const D3D11_VIEWPORT full_view{0,0,float(width_),float(height_),0,1};context_->RSSetViewports(1,&full_view);
    ID3D11ShaderResourceView* resolve[2]={ao_srv_.Get(),ao_ambient_srv_.Get()};
    context_->PSSetShaderResources(1,2,resolve);
    context_->OMSetBlendState(ao_resolve_blend_.Get(),blend_factor,0xffffffffu);
    context_->PSSetShader(ao_resolve_ps_.Get(),nullptr,0);context_->Draw(3,0);
    context_->PSSetShaderResources(0,3,none);
    // The next world frame accumulates its ambient share from zero.
    context_->ClearRenderTargetView(ao_ambient_rtv_.Get(),zero);
    profiler_.add_draw(4,4);profiler_.add_batch(4);
    return S_OK;
}

}  // namespace owned
