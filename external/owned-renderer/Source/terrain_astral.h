#pragma once

namespace owned {

// A colour-only terrain material. The native terrain pixel variants supply
// world position, camera, game seconds and the decoded shore distance.
inline constexpr char kTerrainAstralShader[] = R"(
float terrainAstralCoverage(uint flags,float2 local){
 uint mask=(flags>>16)&15u;
 float4 corners=float4(mask&1u,(mask>>1)&1u,(mask>>2)&1u,(mask>>3)&1u);
 float2 row=lerp(corners.xz,corners.yw,saturate(local.x));
 return smoothstep(.25,.75,lerp(row.x,row.y,saturate(local.y)));
}

float terrainAstralHash(float2 cell){
 float2 h=frac(cell*float2(.1031,.1030));
 h+=dot(h,h.yx+33.33);
 return frac((h.x+h.y)*h.x);
}
float terrainAstralNoise(float2 p){
 float2 cell=floor(p),fraction=frac(p);
 float2 blend=fraction*fraction*(3-2*fraction);
 float2 lower=float2(terrainAstralHash(cell),terrainAstralHash(cell+float2(1,0)));
 float2 upper=float2(terrainAstralHash(cell+float2(0,1)),terrainAstralHash(cell+1));
 float2 rows=lerp(lower,upper,blend.y);
 return lerp(rows.x,rows.y,blend.x);
}
float terrainAstralCloud(float2 p){
 float3 octaves=float3(terrainAstralNoise(p),terrainAstralNoise(p*2.03+17.1),
                      terrainAstralNoise(p*4.07-9.2));
 return dot(octaves,float3(.57,.28,.15));
}
float3 terrainAstralColor(float3 world,float3 eye,float seconds,float shoreDistance,float footprint){
 float3 toEye=eye-world;
 float2 parallax=clamp(toEye.xy/max(abs(toEye.z),8),-2,2);
 // Height keeps currents visible on the menu's steep terrain backdrop.
 float2 field=world.xy*float2(.055,.105)+world.z*float2(.021,.045);
 float2 warp=float2(terrainAstralNoise(field*.65+11),terrainAstralNoise(field*.65-23))-.5;
 float2 deep=field+1.6*warp+.22*parallax+seconds*float2(.0022,-.0013);
 float2 near=1.8*field+2*warp+.07*parallax+seconds*float2(-.0031,.0018);

 float cloud=pow(smoothstep(.28,.78,terrainAstralCloud(deep)),1.9);
 float veil=pow(smoothstep(.4,.8,terrainAstralCloud(near)),1.5);
 float threadPhase=dot(deep,float2(2.1,4.3))+8*terrainAstralCloud(deep*1.4);
 float threads=pow(saturate(1-abs(sin(threadPhase))),10);
 threads*=smoothstep(.52,.78,terrainAstralNoise(deep*.7+41));
 threads*=1-smoothstep(.15,.65,footprint);
 float haze=(1-smoothstep(0,3.5,shoreDistance))*smoothstep(.25,.68,terrainAstralCloud(near*.8+7));

 return float3(.006,.005,.012)
      + float3(.140,.055,.230)*cloud
      + float3(.100,.035,.170)*veil
      + float3(.035,.012,.060)*threads
      + float3(.180,.060,.280)*haze;
}
)";

} // namespace owned
