#define NOMINMAX
#include <windows.h>
#include <d3dcompiler.h>
#include <iostream>
#include <string>
#include <array>
#include "world_fog.h"
#include "terrain_cliff.h"
#include "game_particles.h"
#include "terrain_print_shader.h"
int main(){
 const std::string source=std::string(owned::kAtmosphereShader)+owned::kTerrainCliffShader+owned::kGameParticleShader+owned::kTerrainPrintShader;
 const char* entries[][2]={{"terrainPrintVS","vs_5_0"},{"terrainPrintHS","hs_5_0"},{"terrainPrintDS","ds_5_0"},
  {"gameParticleVS","vs_5_0"},{"gameMeshVS","vs_5_0"},{"gameParticlePS","ps_5_0"},{"gameParticleDepthPS","ps_5_0"},
  {"gameRibbonVS","vs_5_0"},{"gameSelectionVS","vs_5_0"},{"gameGlyphVS","vs_5_0"}};
 for(const auto& e:entries){ID3DBlob* code=nullptr,*errors=nullptr;
  HRESULT hr=D3DCompile(source.data(),source.size(),"terrain-print",nullptr,nullptr,e[0],e[1],D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
  if(errors){std::cout.write(static_cast<const char*>(errors->GetBufferPointer()),errors->GetBufferSize());errors->Release();}
  if(FAILED(hr))return 1;if(code)code->Release();std::cout<<"PASS "<<e[0]<<'\n';
 }
 static_assert(sizeof(owned::GameParticleGpu)==48);static_assert(sizeof(owned::GameParticleConstants)==208);
 std::cout<<"PASS unchanged particle buffer/constant ABI\n";return 0;
}
