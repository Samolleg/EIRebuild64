HRESULT Renderer::compose_embedded_figure_texture(ID3D11ShaderResourceView* primary,
 ID3D11ShaderResourceView* secondary, ComPtr<ID3D11ShaderResourceView>& combined) {
 HRESULT hr=S_OK;
  ComPtr<ID3D11Device> owner; secondary->GetDevice(&owner);
  if(owner.Get()!=device_.Get())return E_INVALIDARG;
  // Combine the two authored layers before c56's ordinary material pipeline.
  // Overlay alpha changes RGB only; primary alpha still owns alpha testing.
  const auto& shader=kEmbeddedFigureLayerShader;
  if (embedded_figure_vs_) {
   ComPtr<ID3D11Device> shaderDevice; embedded_figure_vs_->GetDevice(&shaderDevice);
   if(shaderDevice.Get()!=device_.Get()) {
    embedded_figure_vs_.Reset(); embedded_figure_ps_.Reset(); embedded_figure_sampler_.Reset();
   }
  }
  if (!embedded_figure_vs_ || !embedded_figure_ps_) {
  ComPtr<ID3DBlob> bytecode, errors;
  hr=D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"vs","vs_5_0",0,0,&bytecode,&errors);
  if (FAILED(hr)) return hr;
  hr=device_->CreateVertexShader(bytecode->GetBufferPointer(),bytecode->GetBufferSize(),nullptr,&embedded_figure_vs_);
  if (FAILED(hr)) return hr;
  bytecode.Reset(); errors.Reset();
  hr=D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"ps","ps_5_0",0,0,&bytecode,&errors);
  if (FAILED(hr)) return hr;
  hr=device_->CreatePixelShader(bytecode->GetBufferPointer(),bytecode->GetBufferSize(),nullptr,&embedded_figure_ps_);
  if (FAILED(hr)) return hr;
  }
  ComPtr<ID3D11Resource> resource; primary->GetResource(&resource);
  ComPtr<ID3D11Texture2D> source;
  hr=resource.As(&source); if (FAILED(hr)) return hr;
  D3D11_TEXTURE2D_DESC desc{}; source->GetDesc(&desc);
  desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM; desc.ArraySize=1;
  desc.MipLevels=0; desc.SampleDesc={1,0}; desc.Usage=D3D11_USAGE_DEFAULT;
  desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
  desc.CPUAccessFlags=0; desc.MiscFlags=D3D11_RESOURCE_MISC_GENERATE_MIPS;
  ComPtr<ID3D11Texture2D> texture;
  hr=device_->CreateTexture2D(&desc,nullptr,&texture); if (FAILED(hr)) return hr;
  ComPtr<ID3D11RenderTargetView> target;
  hr=device_->CreateRenderTargetView(texture.Get(),nullptr,&target); if (FAILED(hr)) return hr;
  hr=device_->CreateShaderResourceView(texture.Get(),nullptr,&combined); if (FAILED(hr)) return hr;
  D3D11_SAMPLER_DESC sampling{}; sampling.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  sampling.AddressU=sampling.AddressV=sampling.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
  sampling.MaxLOD=D3D11_FLOAT32_MAX;
  if(!embedded_figure_sampler_) {
   hr=device_->CreateSamplerState(&sampling,&embedded_figure_sampler_); if (FAILED(hr)) return hr;
  }
  std::array<ComPtr<ID3D11RenderTargetView>,D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> oldTargets;
  std::array<ID3D11RenderTargetView*,D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> rawTargets{};
  ComPtr<ID3D11DepthStencilView> oldDepth;
  context_->OMGetRenderTargets(UINT(rawTargets.size()),rawTargets.data(),&oldDepth);
  for(UINT i=0;i<rawTargets.size();++i)oldTargets[i].Attach(rawTargets[i]);
  ComPtr<ID3D11RasterizerState> oldRaster;
  ComPtr<ID3D11BlendState> oldBlend; FLOAT blendFactor[4]; UINT mask=0;
  ComPtr<ID3D11DepthStencilState> oldStencil; UINT reference=0;
  context_->RSGetState(&oldRaster);
  context_->OMGetBlendState(&oldBlend,blendFactor,&mask);
  context_->OMGetDepthStencilState(&oldStencil,&reference);
  std::array<D3D11_VIEWPORT,D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> oldViewports{};
  UINT count=UINT(oldViewports.size());
  context_->RSGetViewports(&count,oldViewports.data());
  D3D11_VIEWPORT viewport{0,0,float(desc.Width),float(desc.Height),0,1};
  context_->OMSetRenderTargets(1,target.GetAddressOf(),nullptr);
  context_->RSSetViewports(1,&viewport);
  context_->RSSetState(nullptr); context_->OMSetBlendState(nullptr,nullptr,0xffffffff);
  context_->OMSetDepthStencilState(nullptr,0);
  context_->IASetInputLayout(nullptr);
  context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_->VSSetShader(embedded_figure_vs_.Get(),nullptr,0); context_->PSSetShader(embedded_figure_ps_.Get(),nullptr,0);
  context_->GSSetShader(nullptr,nullptr,0);
  ID3D11ShaderResourceView* inputs[]={primary,secondary};
  context_->PSSetShaderResources(0,2,inputs);
  context_->PSSetSamplers(0,1,embedded_figure_sampler_.GetAddressOf());
  context_->Draw(3,0);
  ID3D11ShaderResourceView* empty[]={nullptr,nullptr};
  context_->PSSetShaderResources(0,2,empty);
  context_->OMSetRenderTargets(UINT(rawTargets.size()),rawTargets.data(),oldDepth.Get());
  context_->RSSetViewports(count,oldViewports.data());
  context_->RSSetState(oldRaster.Get());
  context_->OMSetBlendState(oldBlend.Get(),blendFactor,mask);
  context_->OMSetDepthStencilState(oldStencil.Get(),reference);
  context_->GenerateMips(combined.Get());
  return S_OK;

}
// Included only by the embedded renderer. Retail rendering stays unchanged.
HRESULT Renderer::queue_embedded_records(const NativePreparedSubmission& submission,
 const FixedState& frame, std::uint64_t primaryId,
 const OwnedVisualSurface* secondary, NativeGeometryResult* result) {
 if(native_shadow_collecting_)return queue_native_records(submission,frame,nullptr,result);
 if(submission.wound_surface_interface && !secondary)
  return native_geometry_failure(result,E_INVALIDARG,"embedded.records_wound_surface");
 if(!submission.parts||!submission.pose||!submission.world_to_clip)
  return native_geometry_failure(result,E_INVALIDARG,"embedded.records_identity");
 for(UINT i=0;i<submission.count;++i) {
  const auto* part=submission.parts[i];
  if(!part||!part->source)
   return native_geometry_failure(result,E_INVALIDARG,"embedded.records_part");
  const auto& source=*part->source;
  NativeGeometryView view=source;
  const auto& values=part->values;
  view.prepared_identity=submission.identity;
  view.prepared_identity.part_index=source.prepared_identity.part_index;
  view.world.pose=submission.pose;view.world.owner=&submission.pose->owner;
  view.world.world_to_clip=*submission.world_to_clip;view.object_to_clip=*submission.world_to_clip;
  view.world.object_to_world=submission.pose->root_to_world;view.world.shape=submission.pose->shape;
  view.contribution=part->contribution;view.bounds_minimum=part->bounds.minimum;view.bounds_maximum=part->bounds.maximum;
  view.contact_world_height=submission.pose->contact_world_height;
  view.flags=values.flags;view.default_diffuse=values.default_diffuse;view.default_specular=values.default_specular;
  view.lighting.enabled=values.lighting_enabled;view.lighting.material_alpha=values.alpha;
  view.lighting.material_diffuse=values.diffuse;view.lighting.emissive=values.emissive;view.lighting.specular=values.specular;
  view.u_shift=values.u_shift;view.v_shift=values.v_shift;
  view.ordering=static_cast<NativeOrdering>(values.ordering);
  view.owned_fixed_state.specular_enable=values.specular_enable;
  const auto state=native_owned_submission_state(frame,view.owned_fixed_state);
  HRESULT hr=S_OK;
  if(submission.wound_surface_interface) {
   view.wound_only=true;
   hr=queue_embedded_wound(view,state,*secondary,result);
  }
  else if(!secondary || source.owned_texture.identity!=primaryId)hr=queue_native_owned(view,state,result);
  else if(d3d12_) {
   D3D12TextureRef primary,overlay;
   std::uint64_t backing=0;
   hr=get_native_owned_texture_d3d12(source.owned_texture,primary,backing,result,true);
   NativeOwnedTextureView second{};
   second.identity=secondary->id;second.generation=secondary->generation;second.surface=secondary;
   if(SUCCEEDED(hr))hr=get_native_owned_texture_d3d12(second,overlay,backing,result,true);
   if(FAILED(hr))return hr;
   auto& layers=embedded_figure_layers_[{primaryId,secondary->id}];
   if(!layers.texture12||layers.primary_generation!=source.owned_texture.generation||
      layers.secondary_generation!=secondary->generation) {
    hr=flush_native();if(FAILED(hr))return hr;
    D3D12TextureRef combined;
    hr=compose_embedded_figure_texture_d3d12(primary,overlay,combined);
    if(hr!=S_OK)return native_geometry_failure(result,FAILED(hr)?hr:E_PENDING,"embedded.records_layers_d3d12");
    layers.texture12=std::move(combined);
    layers.primary_generation=source.owned_texture.generation;layers.secondary_generation=secondary->generation;
   }
   hr=queue_native_impl(view,state,nullptr,result,false,nullptr,
       std::uint64_t(reinterpret_cast<std::uintptr_t>(layers.texture12.get())),1,false,0,nullptr,&layers.texture12);
  }
  else {
   ComPtr<ID3D11ShaderResourceView> primary, overlay;
   std::uint64_t backing=0;
   hr=get_native_owned_texture(source.owned_texture,primary,backing,result,true);
   NativeOwnedTextureView second{};
   second.identity=secondary->id;second.generation=secondary->generation;second.surface=secondary;
   if(SUCCEEDED(hr))hr=get_native_owned_texture(second,overlay,backing,result,true);
   if(FAILED(hr))return hr;
   auto& layers=embedded_figure_layers_[{primaryId,secondary->id}];
   if(!layers.texture||layers.primary_generation!=source.owned_texture.generation||
      layers.secondary_generation!=secondary->generation) {
    hr=compose_embedded_figure_texture(primary.Get(),overlay.Get(),layers.texture);
    if(FAILED(hr))return hr;
    layers.primary_generation=source.owned_texture.generation;layers.secondary_generation=secondary->generation;
   }
   hr=queue_native_impl(view,state,nullptr,result,false,layers.texture.Get(),
       std::uint64_t(reinterpret_cast<std::uintptr_t>(layers.texture.Get())),1,false,0,nullptr);
  }
  if(FAILED(hr)) {
   if(result && !result->failure_operation)
    native_geometry_failure(result,hr,"embedded.records_part_submit");
   return hr;
  }
 }
 return S_OK;
}
HRESULT Renderer::queue_embedded_figure(const NativeGeometryView& view,
 const FixedState& state, ID3D11ShaderResourceView* primary,
 ID3D11ShaderResourceView* secondary, const NativeWorldLighting* lighting,
 const std::vector<RetainedWorldPointLight>& lights) {
 HRESULT hr = has_pending_ui() ? flush_ui() : S_OK;
 if (SUCCEEDED(hr)) hr = flush_native();
 if (FAILED(hr)) return hr;
 const auto savedLighting=native_world_lighting_;
 const bool savedValid=native_world_lighting_valid_;
 const auto savedLights=lighting?native_point_light_inputs_:std::vector<RetainedWorldPointLight>{};
 double worldTime=0; bridge_current_world_time(worldTime);
 const auto restoreLighting=[&]{
  if (lighting) {
   set_native_world_lighting(savedLighting);
   set_native_point_lights(savedLights,worldTime);
   native_world_lighting_valid_=savedValid;
  }
 };
 struct Restore { decltype(restoreLighting)& run; ~Restore(){run();} } restore{restoreLighting};
 if (lighting && (!set_native_world_lighting(*lighting) ||
     !set_native_point_lights(lights, worldTime))) return E_INVALIDARG;
 ComPtr<ID3D11ShaderResourceView> combined;
 if (primary && secondary) {
  hr=compose_embedded_figure_texture(primary,secondary,combined);
  if(FAILED(hr))return hr;
  primary=combined.Get();
 }
 hr=queue_native_direct(view,state,primary);
 if (SUCCEEDED(hr)) hr=flush_native();
 if (FAILED(hr)) discard_native();
 return hr;
}
