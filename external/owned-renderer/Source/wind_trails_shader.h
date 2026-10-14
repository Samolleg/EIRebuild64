#pragma once
// Shipped wind ribbon shader, also compiled by its focused regression.
namespace owned::wind_trails {
constexpr char kWindTrailShader[] = R"(
cbuffer WindTrails:register(b0){
 row_major float4x4 wtClip;float4 wtEye;float4 wtStyle;float4 wtColor;
 float4 wtFogRange;float4 wtFogControl;float4 wtFogColor;float4 wtDepth;
 float4 wtFogSeam;float4 wtFogCurve;float4 wtFogSpanLo;float4 wtFogSpanHi; // neighbour seam fog (neighbour_fog.h)
 float4 wtTrail[192];
};
Texture2D<float> wtHeight:register(t0);
Texture2D<float> wtSceneDepth:register(t2);
struct WtOut{float4 position:SV_Position;float4 color:COLOR0;float3 uv:TEXCOORD0;float viewW:TEXCOORD1;float border:TEXCOORD2;};
float wtGround(float2 p){
 uint w,h;wtHeight.GetDimensions(w,h);
 float2 c=clamp(p,0,float2(w-1,h-1));int2 i=int2(floor(c));int2 j=min(i+1,int2(w-1,h-1));float2 f=c-i;
 return lerp(lerp(wtHeight.Load(int3(i,0)),wtHeight.Load(int3(j.x,i.y,0)),f.x),
             lerp(wtHeight.Load(int3(i.x,j.y,0)),wtHeight.Load(int3(j,0)),f.x),f.y);
}
// Head position s seconds after spawn. T0 origin.xy, height, age; T1 wind.xy,
// speed, trail seconds; T2 lateral amp, spatial rate, phase, life;
// T3 lift, signed loop radius (zero for S-curves), width, seed.
float3 wtPath(uint b,float s){
 float4 t0=wtTrail[b],t1=wtTrail[b+1],t2=wtTrail[b+2],t3=wtTrail[b+3];
 float2 f=t1.xy,n=float2(-f.y,f.x);
 float d=wtTravel(s,t2.w,t1.z);
 float side=wtSide(d,t2.x,t2.y,t2.z,t3.y);
 float lift=t3.x*(sin(.35*d+t2.z*1.7)-sin(t2.z*1.7));
 float2 xy=t0.xy+f*wtAlong(d,t3.y)+n*side;
 return float3(xy,wtGround(xy)+t0.z+lift);
}
WtOut windTrailVS(uint vertex:SV_VertexID,uint instance:SV_InstanceID){
 WtOut o=(WtOut)0;
 uint b=instance*4u;
 float4 t0=wtTrail[b],t1=wtTrail[b+1],t2=wtTrail[b+2],t3=wtTrail[b+3];
 float u=(vertex>>1)/32.0,side=(vertex&1u)?1:-1;
 float age=t0.w,s=max(age-t1.w*(1-u),0);
 float3 p=wtPath(b,s),tangent=wtPath(b,min(s+.02,t2.w))-p;
 if(dot(tangent,tangent)<1e-10)tangent=float3(t1.xy,0);
 float3 view=p-wtEye.xyz;
 float3 across=cross(tangent,view);float l=length(across);
 across=l>1e-6?across/l:float3(-t1.y,t1.x,0);
 float4 clipP=mul(wtClip,float4(p,1));
 // Thin world width tapering to the tail, never below wtStyle.z pixels;
 // a wider-than-real ribbon keeps its coverage by fading.
 float width=wtStyle.y*t3.z*(.1+.9*smoothstep(0,1,u));
 float shown=max(width,wtStyle.z*max(clipP.w,.05)/max(wtEye.w,1e-3));
 o.position=mul(wtClip,float4(p+across*side*shown*.5,1));o.viewW=o.position.w;
 float dist=length(view);
 float fade=smoothstep(0,.35,age)*smoothstep(0,.6,t2.w-age)*(width/shown);
 fade*=saturate((dist-2)/3)*saturate((wtDepth.w-dist)/(.3*wtDepth.w));
 if(clipP.w<=.05)fade=0;
 float fog=1;
 if(wtFogControl.y!=0&&wtFogControl.x>0){
  fog=1-borderFogDensity(p.xy,wtFogRange,wtFogControl.x,wtFogSeam,wtFogCurve,wtFogSpanLo,wtFogSpanHi);
 }
 o.border=fog;
 o.color=float4(wtColor.rgb,wtStyle.x*fade);
 o.uv=float3(u,side,t3.w);
 return o;
}
float4 windTrailPS(WtOut i):SV_Target{
 float u=i.uv.x,v=i.uv.y;
 // Bright soft head, fading towards the tail; soft across the ribbon.
 float a=i.color.a*smoothstep(0,.8,u)*(1-smoothstep(.8,1,u));
 float edge=saturate(1-v*v);a*=edge*edge;
 // Snow: the streak breaks into specks.
 if(wtStyle.w>0){float k=frac(sin((floor(u*40)+i.uv.z*97)*12.9898)*43758.547);a*=lerp(1,step(.5,k)*1.8,wtStyle.w);}
 float d=wtSceneDepth.Load(int3(i.position.xy,0));
 float denom=d-wtDepth.x,sceneW=abs(denom)>1e-7?wtDepth.y/denom:1e9;
 if(sceneW<0)sceneW=1e9;
 a*=saturate((sceneW-i.viewW)/wtDepth.z);
 // Premultiplied coverage takes the fog colour and in-scatter like a world surface.
 float4 c=float4(i.color.rgb*a,a);
 float f=nativeFogFactor(i.border,i.position);
 c.rgb=c.rgb*f+borderFogColor(i.position,wtFogColor.rgb)*(c.a*(1-f));
 float3 added=0;float t=nativeAtmosphere(added,i.position,wtFogColor.rgb);
 c.rgb=c.rgb*t+added*c.a;
 return c;
}
)";
} // namespace owned::wind_trails
