#pragma once
#include <d3dcompiler.h>
#include <fstream>
#include <iterator>
#include <array>

// Runs the production HLSL (extracted, never reimplemented) on the actual
// BC2 atlas. TGA outputs are GPU readbacks, not artist-painted previews.
inline void check_astral_alpha_gpu(const char* shaderPath,const char* atlasPath,const char* output) {
 using Microsoft::WRL::ComPtr;
 const auto ok=[](HRESULT h){if(FAILED(h))throw std::runtime_error("astral WARP setup/readback failed");};
 const auto read=[](const char* path){std::ifstream f(path,std::ios::binary);if(!f)throw std::runtime_error(path);return std::string(std::istreambuf_iterator<char>(f),{});};
 const auto atlas=read(atlasPath),source=read(shaderPath);
 const auto word=[&](unsigned offset){std::uint32_t n;std::memcpy(&n,atlas.data()+offset,4);return n;};
 if(atlas.size()<76||word(16)!=0x33545844)throw std::runtime_error("Expected actual DXT3 MMP");
 const UINT width=word(4),height=word(8),offset=76+word(72);
 if(width!=1024||height!=1024||offset+width*height>atlas.size())throw std::runtime_error("Unexpected atlas layout");
 const auto begin=source.find("// Astral border repair begin"),end=source.find("NativePixelResult nativePS",begin);
 if(begin==std::string::npos||end==std::string::npos)throw std::runtime_error("Production shader block missing");
 std::string hlsl=R"(
 Texture2D image:register(t0);SamplerState linearSampler:register(s0);
 cbuffer Settings:register(b0){float4 settings;}
 struct NativeOutput{uint textureReference;float2 uv;float4 astral;};
 float4 terrainTapGrad(uint ref,float2 uv,float2 dx,float2 dy){return image.SampleGrad(linearSampler,uv,dx,dy);}
 )"+source.substr(begin,end-begin)+R"(
 float4 vs(uint id:SV_VertexID):SV_Position{float2 p[3]={float2(-1,-1),float2(-1,3),float2(3,-1)};return float4(p[id],0,1);}
 float4 ps(float4 position:SV_Position):SV_Target{
  NativeOutput i=(NativeOutput)0;i.uv=position.xy/settings.zw;i.astral=float4(settings.y,1,1,settings.x>0?1:0);
  float2 dx=ddx(i.uv),dy=ddy(i.uv);
  if(i.uv.x<.0166015625||i.uv.x>.4833984375||i.uv.y<.5166015625||i.uv.y>.9833984375)return terrainTapGrad(0,i.uv,dx,dy);
  if(abs(settings.x)>1.5)return nativeAstralSample(i);
  return nativeAstralVeilTap(i,i.uv,dx,dy);
 })";
 ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
 ok(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context));
 D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.MipLevels=desc.ArraySize=1;desc.SampleDesc.Count=1;
 desc.Format=DXGI_FORMAT_BC2_UNORM;desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
 D3D11_SUBRESOURCE_DATA data{atlas.data()+offset,width*4,width*height};ComPtr<ID3D11Texture2D> texture;
 ok(device->CreateTexture2D(&desc,&data,&texture));ComPtr<ID3D11ShaderResourceView> srv;ok(device->CreateShaderResourceView(texture.Get(),nullptr,&srv));
 desc.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
 ComPtr<ID3D11Texture2D> target,staging;ok(device->CreateTexture2D(&desc,nullptr,&target));
 ComPtr<ID3D11RenderTargetView> rtv;ok(device->CreateRenderTargetView(target.Get(),nullptr,&rtv));
 desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ok(device->CreateTexture2D(&desc,nullptr,&staging));
 ComPtr<ID3DBlob> vs,ps,error;
 ok(D3DCompile(hlsl.data(),hlsl.size(),nullptr,nullptr,nullptr,"vs","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&vs,&error));
 auto compiled=D3DCompile(hlsl.data(),hlsl.size(),nullptr,nullptr,nullptr,"ps","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&ps,&error);
 if(FAILED(compiled)){if(error)std::printf("%s\n",static_cast<const char*>(error->GetBufferPointer()));ok(compiled);}
 ComPtr<ID3D11VertexShader> vertex;ComPtr<ID3D11PixelShader> pixel;
 ok(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vertex));ok(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&pixel));
 D3D11_BUFFER_DESC bd{};bd.ByteWidth=16;bd.Usage=D3D11_USAGE_DEFAULT;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;ComPtr<ID3D11Buffer> cb;ok(device->CreateBuffer(&bd,nullptr,&cb));
 D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;ComPtr<ID3D11RasterizerState> rs;ok(device->CreateRasterizerState(&raster,&rs));context->RSSetState(rs.Get());
 D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=0;ComPtr<ID3D11SamplerState> sampler;ok(device->CreateSamplerState(&sd,&sampler));
 D3D11_VIEWPORT viewport{0,0,float(width),float(height),0,1};context->RSSetViewports(1,&viewport);context->OMSetRenderTargets(1,rtv.GetAddressOf(),nullptr);
 context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);context->VSSetShader(vertex.Get(),nullptr,0);context->PSSetShader(pixel.Get(),nullptr,0);
 context->PSSetShaderResources(0,1,srv.GetAddressOf());context->PSSetShaderResources(0,1,srv.GetAddressOf());context->PSSetSamplers(0,1,sampler.GetAddressOf());context->PSSetConstantBuffers(0,1,cb.GetAddressOf());
 using Pixel=std::array<float,4>;
 const auto render=[&](float mode,float phase,const std::string& name){
  const float settings[]={mode,phase,float(width),float(height)};context->UpdateSubresource(cb.Get(),0,nullptr,settings,0,0);context->Draw(3,0);
  context->CopyResource(staging.Get(),target.Get());D3D11_MAPPED_SUBRESOURCE mapped{};ok(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped));
  std::vector<Pixel> pixels(width*height);
  for(UINT y=0;y<height;++y)std::memcpy(pixels.data()+y*width,static_cast<const char*>(mapped.pData)+y*mapped.RowPitch,width*sizeof(Pixel));
  context->Unmap(staging.Get(),0);
  std::ofstream f(std::string(output)+"/"+name+".tga",std::ios::binary);
  unsigned char header[18]{};header[2]=2;header[12]=width&255;header[13]=width>>8;header[14]=height&255;header[15]=height>>8;header[16]=32;header[17]=0x28;f.write(reinterpret_cast<char*>(header),18);
  for(const auto& p:pixels){unsigned char bgra[4];for(unsigned c=0;c<4;++c)bgra[c]=static_cast<unsigned char>(std::lround(std::clamp(p[c==0?2:c==2?0:c],0.f,1.f)*255));f.write(reinterpret_cast<char*>(bgra),4);}
  return pixels;
 };
 const auto original=render(0,0,"original"),repaired=render(1,0,"repaired");
 unsigned unchanged=0,filled=0;
 for(UINT y=0;y<height;++y)for(UINT x=0;x<width;++x){
  const auto& a=original[y*width+x];const auto& b=repaired[y*width+x];
  const bool veil=x>=17&&x<495&&y>=529&&y<1007;
  if(!veil||a[3]>=.35f){for(unsigned c=0;c<4;++c)if(std::abs(a[c]-b[c])>1e-6f)throw std::runtime_error("Existing texel changed");++unchanged;}
  else {if(b[3]<.34999f||b[2]<.1f)throw std::runtime_error("Hole or black fill remains");++filled;}
 }
 for(unsigned phase=0;phase<6;++phase){
  auto p=render(2,phase/6.f,"repaired-phase-"+std::to_string(phase));render(-2,phase/6.f,"original-phase-"+std::to_string(phase));
  for(UINT y=530;y<1006;++y)for(UINT x=18;x<494;++x)if(p[y*width+x][3]<.3499f)throw std::runtime_error("Scrolling reopened hole");
 }
 std::printf("Actual atlas + production HLSL WARP: %u unchanged texels; %u holes filled; six scrolling phases PASS\n",unchanged,filled);
}
