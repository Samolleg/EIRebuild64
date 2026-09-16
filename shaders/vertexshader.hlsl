#include "constant_buffers.hlsl"
#include "vs_input.hlsl"

struct VS_INPUT
{
    float4 model_pos : POSITION;
    float4 normal : NORMAL;
    float4 color : COLOR;
    float4 specular : SPECULAR;
    float2 texcoord : TEXCOORD;
    bool useVertexColor : USE_VERTEX_COLOR;
};


float4 calculateLighting(float3 in_pos, float3 in_normal)
{
    float4 sdf = sunlight.dcMaterialDiffuse;
    float3 snl = sunlight.rd;
    float cos_alpha = dot(in_normal, snl);
    float4 ambient_color = dcvAmbient;
    float4 vertex_color = float4(max(ambient_color.rgb, cos_alpha * sdf.rgb), sdf.a);
    
    float3 dr = in_pos; // model part space
    for (int j = 0; j < nLights; j++)
    {
        float3 nl, nc, dc1, lcp, dl;
        float dl2, k, kdist;
        lcp = lights[j].respectCP;
        dl = lcp - dr;
        dl2 = dot(dl, dl);
        
        //float rcpr_range2 = lights[j].rd.w;
        float rcpr_range2 = lights[j].rcpr_range2;
        kdist = 1.0f - dl2 * rcpr_range2;
        if (kdist < 0)
            continue;
        
        nl = dl * (1.0f / float(sqrt(dl2)));
        cos_alpha = dot(in_normal, nl);
        k = kdist * cos_alpha;

        //rgb, right?
        float3 df = lights[j].dcMaterialDiffuse.rgb;
        vertex_color.rgb = max(vertex_color.rgb, df.rgb * k);
    }
    vertex_color.rgb = min(1.0, vertex_color.rgb + dvEmissive.rgb);
    return vertex_color;
}

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT output;
    float4 input_pos = float4(input.model_pos.xyz, 1.0);
    //mul matrices manually
    
    //float4 input_pos_man = input4;
    //input_pos_man = mul(input_pos_man, translation2);
    //input_pos_man = mul(input_pos_man, scale);
    //input_pos_man = mul(input_pos_man, translation);
    //float3 vertex_position = input_pos_man.rgb;
    //output.v_position = vertex_position;
    //input_pos_man = mul(input_pos_man, camera_proj_clip);
    //input_pos_man final_pos = input_pos_man;
    
    // ignore and use ei matrix    
    float4 final_pos = mul(input_pos, ei_projspace);
    output.position = final_pos;
    
    float4 diffuse_color = input.useVertexColor > 0 ? input.color : calculateLighting(input.model_pos.rgb, input.normal.rgb);
    //float4 diffuse_color = input.color;
    output.color = diffuse_color;
    output.texcoord = input.texcoord;
    output.specular = input.specular;
    
    //float fogFactor = calculateFogFactor();
    // OK, doesn't matter because...
    //"""If an application does not use Direct3D for transformation and lighting, 
    //the application must perform fog calculations. In this case, place the fog factor that 
    //is computed in the alpha component of the specular color for each vertex."""            
    float fogFactor = input.specular.a;
    //float fogFactor = input.useVertexColor ? 1.0 : input.specular.a;    

    output.fogFactor = fogFactor;
    
    return output;
}

float calculateFogFactor()
{
    const float FogStart = 10.0;
    const float FogEnd = 50.0;
    //float fogDist = distance(float3(final_pos.xy, 0), float3(0, 0, 0));
    //float fogDist = input_pos.z / input_pos.w;
    //float fogFactor = saturate((fogDist - FogStart) / (FogEnd - FogStart));        
}