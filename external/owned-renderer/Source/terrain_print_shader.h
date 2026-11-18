#pragma once
namespace owned {
// Appended to the production game-effect and terrain-cliff shader sources.
// Original terrain patches are tessellated before the decal is clipped in PS.
inline constexpr char kTerrainPrintShader[]=R"(
struct PrintPoint{float4 p:POSITION;float3 world:TEXCOORD3;float3 normal:TEXCOORD4;
 float limit:TEXCOORD5;float2 uv:TEXCOORD6;float4 color:COLOR0;float2 data:TEXCOORD7;};
PrintPoint terrainPrintVS(uint id:SV_VertexID){
 GameParticle q=gpParticles[gpFirst+id];PrintPoint o;
 o.world=q.centerRadius.xyz;o.p=mul(gpObjectToClip,float4(o.world,1));
 float2 xy=float2(int(q.padding.x<<16)>>16,int(q.padding.x)>>16)/32767.0;
 o.normal=normalize(float3(xy,sqrt(saturate(1-dot(xy,xy)))));
 o.limit=(gpPadding.x&2u)!=0?terrainTessLimit(o.world,o.normal.z,.7,gpSoft):0;
 o.limit*=1-smoothstep(.02,.15,q.uv.z);
 o.uv=q.uv.xy;o.color=float4((q.color>>16)&255,(q.color>>8)&255,q.color&255,q.color>>24)/255.0;
 o.data=float2(q.padding.y,q.centerRadius.w);return o;
}
struct PrintFactors{float edges[3]:SV_TessFactor;float inside:SV_InsideTessFactor;};
PrintFactors terrainPrintFactors(InputPatch<PrintPoint,3> p){
 PrintFactors f;
 [unroll]for(uint e=0;e<3;++e){uint a=(e+1)%3,b=(e+2)%3;
  f.edges[e]=terrainTessEdgeFactor(p[a].world,p[b].world,p[a].normal,p[b].normal,
    p[a].limit,p[b].limit,8,p[a].p.w,p[b].p.w,gpClipHalf.w);
  if(p[a].normal.z>=terrainFlatEdgeZ&&p[b].normal.z>=terrainFlatEdgeZ)f.edges[e]=1;
 }
 f.inside=max(f.edges[0],max(f.edges[1],f.edges[2]));return f;
}
[domain("tri")][partitioning("integer")][outputtopology("triangle_cw")][outputcontrolpoints(3)]
[patchconstantfunc("terrainPrintFactors")][maxtessfactor(16.0)]
PrintPoint terrainPrintHS(InputPatch<PrintPoint,3> p,uint i:SV_OutputControlPointID){return p[i];}
[domain("tri")]
GameParticleOut terrainPrintDS(PrintFactors factors,float3 b:SV_DomainLocation,const OutputPatch<PrintPoint,3> p){
 precise float4 position=b.x*p[0].p+b.y*p[1].p+b.z*p[2].p;
 precise float3 world=b.x*p[0].world+b.y*p[1].world+b.z*p[2].world;
 float limit=b.x*p[0].limit+b.y*p[1].limit+b.z*p[2].limit;
 if(limit>.002){float3 k=terrainTessPhong(b,p[0].world,p[1].world,p[2].world,p[0].normal,p[1].normal,p[2].normal,limit);
  position+=k.x*mul(gpObjectToClip,float4(p[0].normal,0))+k.y*mul(gpObjectToClip,float4(p[1].normal,0))+k.z*mul(gpObjectToClip,float4(p[2].normal,0));
 }
 // The original 1 cm lift is applied AFTER smoothing, never to its control points.
 position+=mul(gpObjectToClip,float4(0,0,.01,0));
 GameParticleOut o=(GameParticleOut)0;
 o.p=gpClipFromScreen((position.x+position.w)*gpClipHalf.x,(position.w-position.y)*gpClipHalf.y,position);
 o.d=b.x*p[0].color+b.y*p[1].color+b.z*p[2].color;
 o.uv=b.x*p[0].uv+b.y*p[1].uv+b.z*p[2].uv;o.r=world.z;o.printData=p[0].data;return o;
}
)";
}
