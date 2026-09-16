#include "constant_buffers.hlsl"
#include "vs_input.hlsl"

struct VS_INPUT
{    
    float4 world_pos : POSITION;
    float4 color : COLOR;
    float4 specular : SPECULAR;
    float2 texcoord : TEXCOORD;
};

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT output;	        
    float4 final_pos = mul(input.world_pos, ei_projspace);
    //float4 final_pos = input.inPos;
    
    output.position = final_pos;    
    output.color = input.color;
	output.texcoord = input.texcoord;
    output.specular = input.specular;
    output.fogFactor = input.specular.a;
	return output;
}