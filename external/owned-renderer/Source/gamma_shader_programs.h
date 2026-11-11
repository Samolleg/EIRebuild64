#pragma once
#include "shader_cache.h"

namespace owned {
namespace {

// The game's gamma ramp (game.exe 00504A60 builds it from the brightness,
// contrast and gamma options and hands it to IDirectDrawGammaControl). A
// display ramp maps the 8-bit value i to ramp[i] >> 8, so the game's neutral
// ramp, i * 256, is the identity. Applied to the whole finished frame, cursor
// included, as the hardware ramp was; captures and harness screenshots are
// taken before it, as screen reads were.
constexpr char kGammaShader[] = R"(
Texture2D<float4> gammaFrame:register(t0);
Texture2D<float4> gammaLut:register(t1); // 256x1: rgb = each channel's ramp
SamplerState gammaSampler:register(s0);  // linear clamp
float4 gammaVS(uint id:SV_VertexID):SV_Position{float2 uv=float2((id<<1)&2,id&2);return float4(uv.x*2-1,1-uv.y*2,0,1);}
float4 gammaPS(float4 position:SV_Position):SV_Target{
 float4 color=gammaFrame.Load(int3(position.xy,0));
 float3 at=(saturate(color.rgb)*255+.5)/256;
 return float4(gammaLut.SampleLevel(gammaSampler,float2(at.r,.5),0).r,gammaLut.SampleLevel(gammaSampler,float2(at.g,.5),0).g,
               gammaLut.SampleLevel(gammaSampler,float2(at.b,.5),0).b,color.a);
}
)";
}  // namespace

// gammaVS, gammaPS: apply_gamma_ramp's programs, and the D3D12 backend's
// (append_d3d12_shader_programs puts them in the host's DXIL table).
inline void append_gamma_shader_programs(std::vector<ShaderProgram>& out){
 out.push_back({{kGammaShader,sizeof(kGammaShader)-1},nullptr,nullptr,"gammaVS","vs_4_0",0});
 out.push_back({{kGammaShader,sizeof(kGammaShader)-1},nullptr,nullptr,"gammaPS","ps_4_0",0});
}

} // namespace owned
