#pragma once
#include "shader_cache.h"
namespace owned {
inline constexpr char kEmbeddedFigureLayerShader[] = R"(
Texture2D base:register(t0); Texture2D layer:register(t1);
SamplerState filtering:register(s0);
struct Vertex { float4 position:SV_Position; float2 uv:TEXCOORD0; };
Vertex vs(uint id:SV_VertexID) {
 Vertex o; o.uv=float2((id<<1)&2,id&2);
 o.position=float4(o.uv*float2(2,-2)+float2(-1,1),0,1); return o;
}
float4 ps(Vertex v):SV_Target {
 float4 a=base.Sample(filtering,v.uv),b=layer.Sample(filtering,v.uv);
 return float4(lerp(a.rgb,b.rgb,b.a),a.a);
})";
inline const ShaderProgram& embedded_figure_layer_program(bool pixel) noexcept {
 static const ShaderProgram vs{{kEmbeddedFigureLayerShader,sizeof(kEmbeddedFigureLayerShader)-1},nullptr,nullptr,"vs","vs_5_0",0};
 static const ShaderProgram ps{{kEmbeddedFigureLayerShader,sizeof(kEmbeddedFigureLayerShader)-1},nullptr,nullptr,"ps","ps_5_0",0};
 return pixel?ps:vs;
}
}
