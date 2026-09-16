struct VS_OUTPUT
{
    float4 position : SV_POSITION;
    float4 color : COLOR;    
    float4 specular: SPECULAR;
    float fogFactor : FOG;
    float2 texcoord : TEXCOORD;    
};

typedef VS_OUTPUT PS_INPUT;