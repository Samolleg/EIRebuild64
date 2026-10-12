#include "vs_input.hlsl"

static const uint am_texture = 0;
static const uint am_multiply = 1;
static const uint am_vertex = 2;

cbuffer CB_ps_main : register(b0)
{
    // bool/uint[2] is 20
    // bool/uint[4] is 52
    // array alignment is weird:
    // each bool/uint element is 4 bytes, but
    // each array element is 16 bytes, so
    // 16 bytes * 1 + 4 bytes (last item) = 16 + 4 = 20
    // 16 bytes * 3 + 4 bytes (last item) = 48 + 4 = 52        
    // float4 / uint4 arrays are "easy", albeit may be bulky due to all that padding
    // it's possible to force packed arrays / buffers, but seems unworth
    uint4 isTexture[4];
    float4 fogColor;
    float alphaRef = 0.0;
    uint alphaMode = 0;
    uint useSpecular = 0;
}

Texture2D objTexture : TEXTURE : register(t0);
SamplerState objSamplerState : SAMPLER : register(s0);
Texture2D objTexture2 : TEXTURE : register(t1);
SamplerState objSamplerState2 : SAMPLER : register(s1);

float4 main(PS_INPUT input) : SV_TARGET
{
    float2 txcs = float2(input.texcoord.x, input.texcoord.y);
    //return float4(txcs, 0.0, 1.0);
    float4 dst = objTexture.Sample(objSamplerState, txcs);
    float4 src = objTexture2.Sample(objSamplerState2, txcs);
    if (isTexture[1].x)
    {
        dst.rgb = src.a * src.rgb + (1 - src.a) * dst.rgb;
    }        
    float4 texColor = dst;
               
    float4 pixelColor = input.color;
    if (isTexture[0].x)
    {
        pixelColor *=  texColor;
        if (alphaMode == am_texture)
            pixelColor.a = texColor.a;
        else if (alphaMode == am_multiply)
            pixelColor.a = texColor.a * input.color.a;
        else if (alphaMode == am_vertex)
            pixelColor.a = input.color.a;
    }    
    
    if (pixelColor.a < alphaRef)
        discard;
        
    // D3D7 color sum follows texture modulation; alpha is not specular.
    if (useSpecular)
        pixelColor.rgb = saturate(pixelColor.rgb + input.specular.rgb);

    float fogFactor = input.fogFactor;    
    //fogFactor = 0.0;
    pixelColor = lerp(fogColor, pixelColor, fogFactor);        
        
    
    return pixelColor;
    
}