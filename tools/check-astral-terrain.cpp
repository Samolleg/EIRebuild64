#include "terrain_astral.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <fstream>
#include <filesystem>
#include <cstring>
#include <iostream>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
static void require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
static void hr(HRESULT h){require(SUCCEEDED(h),"D3D11 call failed");}
static ComPtr<ID3DBlob> compile(const std::string& s,const char* entry,const char* target){
 ComPtr<ID3DBlob> code,error;auto result=D3DCompile(s.data(),s.size(),nullptr,nullptr,nullptr,entry,target,D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&error);
 if(error)std::cerr<<static_cast<const char*>(error->GetBufferPointer());hr(result);return code;
}
int main(int argc,char** argv)try{
 using namespace owned;require(argc>=2,"output directory required");
 if(argc==3){
  unsigned profiles=0;
  for(const auto& file:std::filesystem::directory_iterator(argv[2])){
   if(file.path().extension()!=".json")continue;
   std::ifstream input(file.path());std::string json((std::istreambuf_iterator<char>(input)),{});
   const auto root=small_json::Reader(json).root();TerrainTileFamilies authored;
   const auto space=root.field("mprNamespace").str();const auto count=unsigned(root.field("atlasSlotCount").num());
   require(terrain_tile_signatures_from_profile(json,space,count,authored.signatures,&authored.names),"installed profile parse");
   const auto selected=terrain_astral_slots(authored);unsigned n=0;for(bool value:selected)n+=value;
   require(n>0,"installed Astral slots found");
   require(!terrain_tile_signatures_from_profile(json,"wrong_namespace",count,authored.signatures,&authored.names),"namespace rejection");
   require(!terrain_tile_signatures_from_profile(json,space,count+1,authored.signatures,&authored.names),"slot count rejection");
   std::cout<<"PASS installed profile "<<space<<": "<<n<<" Astral slots; mismatched namespace/count rejected\n";++profiles;
  }
  require(profiles>0,"no profiles supplied");return 0;
 }
 TerrainTileFamilies f;f.names={{1,"Astral"},{2,"Ground"},{3,"ASTRAL"},{4,"Astral rock"}};
 f.signatures={{{1,1,1,1}},{{2,2,2,2}},{{1,2,1,1}},{{0,0,0,0}},{{3,3,3,3}},{{4,4,4,4}}};
 const auto slots=terrain_astral_slots(f);require(slots==std::vector<std::uint8_t>({15,0,13,0,15,0}),"explicit selection");
 TerrainCpuAsset map;map.astral_slots={0,15};map.sector_count_x=2;map.sector_count_y=1;map.sectors.resize(2);
 for(auto& sector:map.sectors)sector.base_tile_ids.fill(1);
 require(terrain_astral_edge(map,0,0)==4,"outer map edge must not glow");
 map.sectors[0].base_tile_ids[8*16+15]=0;
 require(terrain_astral_edge(map,32,17)==0,"shore across sector seam");
 require(terrain_astral_edge(map,34,17)==2,"shore distance");
 require(terrain_astral_edge(map,38,17)==4,"distant abyss");
 TerrainTileDescriptor tile;tile.tile_index=1;require(terrain_astral_tile(map,tile),"base astral");
 tile.transparent=true;require(!terrain_astral_tile(map,tile),"overlay exclusion");tile.transparent=false;
 tile.animation_ordinal=0;require(!terrain_astral_tile(map,tile),"animation exclusion");tile.animation_ordinal=UINT_MAX;
 tile.vertex_indices[0]=1089;require(!terrain_astral_tile(map,tile),"upper-layer exclusion");
 // The authored NW corner must follow the UV rotation, not the page or tile position.
 map.astral_slots.push_back(1);tile.vertex_indices[0]=0;tile.tile_index=2;
 std::array<NativeTextureCoordinate,9> uv{};
 uv[0]={0,1};uv[2]={1,1};uv[6]={0,0};uv[8]={1,0};
 require(terrain_astral_world_mask(map,tile,uv)==4,"world corner orientation");
 std::swap(uv[0],uv[8]);std::swap(uv[2],uv[6]);
 require(terrain_astral_world_mask(map,tile,uv)==2,"rotated corner orientation");
 std::cout<<"PASS explicit material selection, negative cases, sector shore and outer map edge\n";
 ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level;
 hr(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context));
 std::string shader=kTerrainAstralShader;shader+=R"(
 cbuffer Params:register(b0){float seconds;float shoreOn;float2 unused;};
 float4 vs(uint id:SV_VertexID):SV_Position{return float4(id==2?3:-1,id==1?3:-1,0,1);}
 float4 ps(float4 pixel:SV_Position):SV_Target{
  float2 uv=pixel.xy/float2(960,640);float3 p=float3(uv.x*70,uv.y*46,0);
  if(unused.x>0)return float4(terrainAstralCoverage(10u<<16,uv),0,0,1);
  float3 eye=float3(28,-15,55);
  if(unused.y>0){p=float3(25,20,uv.y*60);eye=p+float3(3,-4,8);}
  float shore=shoreOn>0?max(0,(uv.x-.12)*70):4;
  return float4(terrainAstralColor(p,eye,seconds,shore,.006),1);
 })";
 auto v=compile(shader,"vs","vs_5_0"),p=compile(shader,"ps","ps_5_0");
 ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;hr(device->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&vs));hr(device->CreatePixelShader(p->GetBufferPointer(),p->GetBufferSize(),nullptr,&ps));
 D3D11_TEXTURE2D_DESC td{};td.Width=960;td.Height=640;td.MipLevels=td.ArraySize=1;td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;td.SampleDesc.Count=1;td.BindFlags=D3D11_BIND_RENDER_TARGET;
 ComPtr<ID3D11Texture2D> target,read;hr(device->CreateTexture2D(&td,nullptr,&target));td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;hr(device->CreateTexture2D(&td,nullptr,&read));
 ComPtr<ID3D11RenderTargetView> rtv;hr(device->CreateRenderTargetView(target.Get(),nullptr,&rtv));
 D3D11_BUFFER_DESC bd{};bd.ByteWidth=16;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;ComPtr<ID3D11Buffer> cb;hr(device->CreateBuffer(&bd,nullptr,&cb));
 D3D11_VIEWPORT vp{0,0,960,640,0,1};context->RSSetViewports(1,&vp);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);context->VSSetShader(vs.Get(),nullptr,0);context->PSSetShader(ps.Get(),nullptr,0);
 auto rt=rtv.Get();context->OMSetRenderTargets(1,&rt,nullptr);auto buffer=cb.Get();context->PSSetConstantBuffers(0,1,&buffer);
 std::vector<float> frames[6];
 for(unsigned frame=0;frame<6;++frame){
  float constants[4]={frame==1?30.f:0.f,(frame==3||frame==5)?0.f:1.f,frame==4?1.f:0.f,frame==5?1.f:0.f};context->UpdateSubresource(cb.Get(),0,nullptr,constants,0,0);context->Draw(3,0);context->CopyResource(read.Get(),target.Get());
  D3D11_MAPPED_SUBRESOURCE mapped{};hr(context->Map(read.Get(),0,D3D11_MAP_READ,0,&mapped));auto& pixels=frames[frame];pixels.resize(960*640*4);
  for(unsigned y=0;y<640;++y)std::memcpy(pixels.data()+y*960*4,static_cast<char*>(mapped.pData)+y*mapped.RowPitch,960*16);context->Unmap(read.Get(),0);
  for(float c:pixels)require(std::isfinite(c)&&c>=0&&c<=1,"finite bounded output");
  std::ofstream out(std::string(argv[1])+"/astral-"+std::to_string(frame)+".ppm",std::ios::binary);out<<"P6\n960 640\n255\n";
  for(unsigned i=0;i<960*640;++i)for(unsigned c=0;c<3;++c){float x=pixels[i*4+c];unsigned char b=static_cast<unsigned char>(std::lround(255*(x<=.0031308?12.92*x:1.055*std::pow(x,1/2.4)-.055)));out.put(char(b));}
 }
 require(frames[4][0]==0&&frames[4][959*4]==1&&frames[4][480*4]>.49f&&frames[4][480*4]<.51f,"mixed material coverage");
 require(frames[0]==frames[2],"same game time must produce identical pixels");
 double delta=0,edge=0,distant=0;for(unsigned i=0;i<960*640;++i){delta+=std::abs(frames[0][i*4]-frames[1][i*4]);if(i%960<150)edge+=frames[0][i*4]-frames[3][i*4];if(i%960>300)distant+=std::abs(frames[0][i*4]-frames[3][i*4]);}
 require(delta>1&&edge>1&&distant==0,"animation and local shore haze");
 std::vector<float> luminance;float minWall=1,maxWall=0;
 for(unsigned i=0;i<960*640;++i){
  const auto* p=frames[3].data()+i*4;luminance.push_back(p[0]*.2126f+p[1]*.7152f+p[2]*.0722f);
  minWall=(std::min)(minWall,frames[5][i*4]);maxWall=(std::max)(maxWall,frames[5][i*4]);
 }
 std::sort(luminance.begin(),luminance.end());
 std::cout<<"Luminance (linear, no shore): p10="<<luminance[luminance.size()/10]<<" p50="<<luminance[luminance.size()/2]
          <<" p95="<<luminance[luminance.size()*95/100]<<" max="<<luminance.back()<<" wall_red_range="<<maxWall-minWall<<'\n';
 require(luminance[luminance.size()/10]<.02f&&luminance[luminance.size()/2]>.04f&&luminance[luminance.size()/2]<.065f&&luminance.back()<.18f,"readable currents, dark gulf, bounded highlights");
 require(maxWall-minWall>.01f,"flow varies over the height of steep Astral terrain");
 std::cout<<"PASS production HLSL on D3D11 WARP; finite pixels, deterministic pause, animated currents, localized haze\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
