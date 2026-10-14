#pragma once

namespace owned::ground_cover {
// Shared by the production cover shaders and the focused WARP regression.
// The existing renderer height texture contains the drawn terrain vertices.
// Guarded: the native object shader includes it too (ground contact blend).
constexpr char kSurfaceShader[] = R"(
#ifndef GC_SURFACE_SHADER
#define GC_SURFACE_SHADER
// Matches the owned terrain LOD0 triangles (00,10,11) and (00,11,01).
float gcHeight(Texture2D<float> t,float2 p){
 uint w,h;t.GetDimensions(w,h);
 p=clamp(p,0,float2(w,h)-1.001);int2 i=int2(floor(p));float2 f=p-i;
 float a=t.Load(int3(i,0)),b=t.Load(int3(i+int2(1,0),0)),c=t.Load(int3(i+int2(0,1),0)),d=t.Load(int3(i+int2(1,1),0));
 return f.x>=f.y?a+(b-a)*f.x+(d-b)*f.y:a+(d-c)*f.x+(c-a)*f.y;
}
float3 gcGroundNormal(Texture2D<float> t,float2 p){
 return normalize(float3(gcHeight(t,p-float2(.5,0))-gcHeight(t,p+float2(.5,0)),
                         gcHeight(t,p-float2(0,.5))-gcHeight(t,p+float2(0,.5)),1));
}
float gcSnowBurial(Texture2D<float> t,float2 p,float radius,float heightScale){
 float lo=gcHeight(t,p),hi=lo;
 [unroll]for(int y=-1;y<=1;++y)[unroll]for(int x=-1;x<=1;++x){
  float z=gcHeight(t,p+float2(x,y)*radius);lo=min(lo,z);hi=max(hi,z);
 }
 return .005*heightScale+.02*(hi-lo);
}
// A snow pile vertex: the pile's own rise over the drawn ground, less its
// burial. A snow trail (snow_trails.h raw field) cuts the pile down to the
// trough floor where it packs the snow and heaps the pushed snow into its
// berms, more where more of the pile stands; rims stay on the ground.
float3 gcSnowPosition(Texture2D<float> t,float2 p,float rise,float burial,float2 trail){
 float above=max(rise-burial,0);
 float z=lerp(rise-burial,-trail.x,snowTrailCompaction(trail));
 z+=trail.y*saturate(above/.05)*(1+3*above);
 return float3(p,gcHeight(t,p)+z);
}
// The actual deformed triangles supply dome, trench and bank normals.
// Orient up independently of screen winding, and join the terrain at the rim.
float3 gcSnowNormal(float3 terrain,float3 dx,float3 dy,float rise,float heightScale){
 float3 n=cross(dx,dy);if(n.z<0)n=-n;
 n=dot(n,n)>1e-20?normalize(n):terrain;
 return normalize(lerp(terrain,n,smoothstep(0,.05*heightScale,rise)));
}
float gcSnowCoverage(float aboveGround,float heightScale){
 return smoothstep(0,.008*heightScale,aboveGround);
}
float gcSnowDither(uint2 pixel){
 const uint bayer[16]={0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};
 return (bayer[(pixel.x&3)+4*(pixel.y&3)]+.5)/16.0;
}
#endif
)";
} // namespace owned::ground_cover
