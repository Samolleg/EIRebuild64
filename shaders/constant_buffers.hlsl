struct D3DLIGHT7
{
    int dltType; /* Type of light source */
    float4 dcvDiffuse; /* Diffuse color of light */
    float4 dcvSpecular; /* Specular color of light */
    float4 dcvAmbient; /* Ambient color of light */
    float3 dvPosition; /* Position in world space */
    float3 dvDirection; /* Direction in world space */
    float dvRange; /* Cutoff range */
    float dvFalloff; /* Falloff */
    float dvAttenuation0; /* Constant attenuation */
    float dvAttenuation1; /* Linear attenuation */
    float dvAttenuation2; /* Quadratic attenuation */
    float dvTheta; /* Inner angle of spotlight cone */
    float dvPhi; /* Outer angle of spotlight cone */
};

struct CLight11
{
    D3DLIGHT7 light;
    float fpAddHeight; // additional height
    float range2; // range*range
    float rcpr_range2; // 1.0f/range2
    float3 rd; // respect direction
    float4 dcMaterialDiffuse; // light color * material diffuse reflection
    float4 dcMaterialSpecular; // light color * material specular reflection    
};

struct CLight11Packed
{
    float3 rd;
    float rcpr_range2;
    float4 dcMaterialDiffuse;
    float3 respectCP;
    float junk;
    float3 position;
    float junk2;
    float3 direction;
    float junk3;
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
    float4x4 model_to_world;    
    float4 dcvAmbient;
    int nLights;
    float3 dvEmissive;
    float3 cameraPosition;
}

cbuffer CB_VS_lights : register(b1)
{    
    CLight11Packed sunlight;
    CLight11Packed lights[16];
}