struct CLight11Packed
{
    float3 rd;
    float rcpr_range2;
    float4 dcMaterialDiffuse;
    float3 respectCP;
    float junk;
};

cbuffer CB_VS_vs : register(b0)
{
    float4x4 camera;
    float4x4 projection;
    float4x4 clip;
    float4x4 translation;
    float4x4 scale;
    float4x4 translation2;
    float4x4 camera_proj_clip;
    float4x4 ei_projspace;
    float4x4 ei_scrnspace;
    float4x4 reserved3;
    float4 dcvAmbient;
    int nLights;
    float3 dvEmissive;
}

struct GS_INPUT
{    
    float4 inPos : POSITION;
    int4 color : COLOR;
    int4 specular : COLOR1;
    float2 texcoord : TEXCOORD;
};

struct GS_OUTPUT
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float3 v_position: COLOR1;
    float3 normal : TEXCOORD0;
    float2 texcoord : TEXCOORD1;
};

//NO IDEA how to write hlsl gs yet and it requires model shader 5>= or something
[maxvertexcount(6)]
void main(point GS_INPUT input[1], uint primID : SV_PrimitiveID, inout TriangleStream<GS_OUTPUT> tristream)
{
	GS_OUTPUT output;	        
    //float4 final_pos = mul(input.inPos, ei_projspace);    
    //float radius = input.inPos.w;    
    ////float4 final_pos = input.inPos;
    
    float4 v[4];
    v[0] = float4(input[0].inPos + float3(1.0f, -1.0f, 0.0f), 1.0f);
    v[1] = float4(input[0].inPos + float3(1.0f, 1.0f, 0.0f), 1.0f);
    v[2] = float4(input[0].inPos + float3(-1.0f, -1.0f, 0.0f), 1.0f);
    v[3] = float4(input[0].inPos + float3(-1.0f, 1.0f, 0.0f), 1.0f);
    
    [unroll]
    for (int i = 0; i < 4; i++)
    {
        float4 p = mul(v[i], ei_projspace);
        output.position = p;
        output.position = 0;
        output.color = 0;
        output.v_position= 0;
        output.normal = 0;
        output.texcoord = 0;

        tristream.Append(output);
    }
}

float4 PS(GS_OUTPUT input) : SV_Target
{
    //return txDiffuse.Sample( samAnisotropic, input.Tex );
    return float4(1.0f, 1.0f, 1.0f, 1.0f);
}