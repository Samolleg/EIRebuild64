#pragma once
namespace owned::gltf {
inline constexpr char kPhysiqueTable[] = R"(
 uint shapeTable=(nodeBase&0x80000000u)?pose.asset+nativeAnimation.Load(pose.asset+32):0;
 nodeBase&=0x7fffffffu;
)";
inline constexpr char kPhysiqueLocal[] = R"(
   if(shapeTable!=0){
    uint record=shapeTable+node*52,mode=nativeAnimation.Load(record+48);
    if(mode!=0){
     float4 a=asfloat(nativeAnimation.Load4(record)),b=asfloat(nativeAnimation.Load4(record+16)),c=asfloat(nativeAnimation.Load4(record+32));
     float3x3 adjust=float3x3(a.xyz,b.xyz,c.xyz),lin=(float3x3)local;
     float3x3 shaped=mode==1?mul(adjust,lin):mul(lin,adjust);
     local=float4x4(float4(shaped[0],t.x+a.w),float4(shaped[1],t.y+b.w),float4(shaped[2],t.z+c.w),float4(0,0,0,1));
    }
   }
)";
inline constexpr char kAnimationShader[] = R"(
// glTF quaternions are x,y,z,w. Legacy ANM helpers use w,x,y,z.
float4 gltfNormalize(float4 q){return q*rsqrt(max(dot(q,q),1e-16));}
float4 gltfSlerp(float4 a,float4 b,float t){
 a=gltfNormalize(a);b=gltfNormalize(b);float d=dot(a,b);
 if(d<0){b=-b;d=-d;}d=saturate(d);
 float x=1-t,y=t;
 if(d<.9995){float angle=acos(d),s=sin(angle);x=sin((1-t)*angle)/s;y=sin(t*angle)/s;}
 return gltfNormalize(x*a+y*b);
}
uint gltfTrack(uint base,uint clip,uint node,uint path){
 if(clip==0xffffffffu)return 0;
 uint table=nativeAnimation.Load(base+12);
 uint channels=nativeAnimation.Load(base+table+clip*4);
 return nativeAnimation.Load(base+channels+node*16+path*4);
}
float4 gltfInterval(uint base,uint track,float time){
 uint count=nativeAnimation.Load(base+track),times=base+nativeAnimation.Load(base+track+12);
 if(time<=asfloat(nativeAnimation.Load(times)))return 0;
 if(time>=asfloat(nativeAnimation.Load(times+(count-1)*4)))return float4(count-1,count-1,0,0);
 uint lo=0,hi=count-1;
 [loop]while(hi-lo>1){uint m=lo+(hi-lo)/2;
  if(asfloat(nativeAnimation.Load(times+m*4))<=time)lo=m;else hi=m;}
 float a=asfloat(nativeAnimation.Load(times+lo*4)),b=asfloat(nativeAnimation.Load(times+hi*4));
 return float4(lo,hi,(time-a)/(b-a),b-a);
}
float gltfComponent(uint base,uint track,float4 interval,uint component){
 uint width=nativeAnimation.Load(base+track+4),mode=nativeAnimation.Load(base+track+8);
 uint values=base+nativeAnimation.Load(base+track+16),stride=width*(mode==2?3:1);
 uint first=(uint)interval.x,second=(uint)interval.y;
 float a=asfloat(nativeAnimation.Load(values+(first*stride+(mode==2?width:0)+component)*4));
 if(first==second||mode==0)return a;
 float b=asfloat(nativeAnimation.Load(values+(second*stride+(mode==2?width:0)+component)*4));
 float t=interval.z;
 if(mode!=2)return lerp(a,b,t);
 float outT=asfloat(nativeAnimation.Load(values+(first*stride+2*width+component)*4))*interval.w;
 float inT=asfloat(nativeAnimation.Load(values+(second*stride+component)*4))*interval.w;
 float t2=t*t,t3=t2*t;
 return (2*t3-3*t2+1)*a+(t3-2*t2+t)*outT+(-2*t3+3*t2)*b+(t3-t2)*inT;
}
float4 gltfSample(uint base,uint clip,uint node,uint path,float time,float4 fallback){
 uint track=gltfTrack(base,clip,node,path);if(track==0)return fallback;
 float4 interval=gltfInterval(base,track,time);uint width=nativeAnimation.Load(base+track+4);
 if(path==1&&nativeAnimation.Load(base+track+8)==1&&interval.x!=interval.y){
  uint values=base+nativeAnimation.Load(base+track+16);
  float4 a=asfloat(nativeAnimation.Load4(values+(uint)interval.x*16));
  float4 b=asfloat(nativeAnimation.Load4(values+(uint)interval.y*16));return gltfSlerp(a,b,interval.z);
 }
 float4 value=0;[unroll]for(uint c=0;c<4;++c)if(c<width)value[c]=gltfComponent(base,track,interval,c);
 return path==1?gltfNormalize(value):value;
}
float gltfWeight(uint base,uint clip,uint node,uint index,float time){
 uint track=gltfTrack(base,clip,node,3);
 uint nodeBase=base+nativeAnimation.Load(base+8)+node*64;
 float value=asfloat(nativeAnimation.Load(base+nativeAnimation.Load(nodeBase+8)+index*4));
 if(track!=0){float4 interval=gltfInterval(base,track,time);value=gltfComponent(base,track,interval,index);}
 return value;
}
float4x4 gltfTrs(float3 t,float4 q,float3 s){
 q=gltfNormalize(q);float x=q.x,y=q.y,z=q.z,w=q.w;
 return float4x4((1-2*(y*y+z*z))*s.x,2*(x*y-z*w)*s.y,2*(x*z+y*w)*s.z,t.x,
  2*(x*y+z*w)*s.x,(1-2*(x*x+z*z))*s.y,2*(y*z-x*w)*s.z,t.y,
  2*(x*z-y*w)*s.x,2*(y*z+x*w)*s.y,(1-2*(x*x+y*y))*s.z,t.z,0,0,0,1);
}
void gltfPose(NativePose pose,uint lane){
 uint relative=nativeAnimation.Load(pose.asset+24);if(relative==0)return;
 uint base=pose.asset+relative,nodeBase=nativeAnimation.Load(pose.asset+28);
@PHYSIQUE_TABLE@ uint nodes=nativeAnimation.Load(base),nodeTable=base+nativeAnimation.Load(base+8);
 uint levels=nativeAnimation.Load(base+24),ranges=base+nativeAnimation.Load(base+28),order=base+nativeAnimation.Load(base+32);
 const float4x4 yToZ=float4x4(1,0,0,0,0,0,-1,0,0,1,0,0,0,0,0,1);
 for(uint level=0;level<levels;++level){
  uint2 range=nativeAnimation.Load2(ranges+level*4);
  for(uint item=range.x+lane;item<range.y;item+=64){
   uint node=nativeAnimation.Load(order+item*4),address=nodeTable+node*64;
   uint parent=nativeAnimation.Load(address);
   float4 restT=asfloat(nativeAnimation.Load4(address+16)),restR=asfloat(nativeAnimation.Load4(address+32)),restS=asfloat(nativeAnimation.Load4(address+48));
   float4 t=gltfSample(base,pose.gltfClip,node,0,pose.gltfTime,restT);
   float4 r=gltfSample(base,pose.gltfClip,node,1,pose.gltfTime,restR);
   float4 s=gltfSample(base,pose.gltfClip,node,2,pose.gltfTime,restS);
   if(pose.gltfBlend<1){
    t=lerp(gltfSample(base,pose.gltfPrevious,node,0,pose.gltfPreviousTime,restT),t,pose.gltfBlend);
    r=gltfSlerp(gltfSample(base,pose.gltfPrevious,node,1,pose.gltfPreviousTime,restR),r,pose.gltfBlend);
    s=lerp(gltfSample(base,pose.gltfPrevious,node,2,pose.gltfPreviousTime,restS),s,pose.gltfBlend);
   }
   float4x4 local=gltfTrs(t.xyz,r,s.xyz),world;
@PHYSIQUE_LOCAL@   if(parent==0xffffffffu)world=mul(mul(pose.rootToWorld,yToZ),local);
   else{NativePoseNode p=nativePoseOutput[pose.outputBase+nodeBase+parent];
    world=mul(float4x4(p.row0,p.row1,p.row2,float4(0,0,0,1)),local);}
   NativePoseNode result=(NativePoseNode)0;
   result.row0=world[0];result.row1=world[1];result.row2=world[2];result.generation=pose.generation;
   nativePoseOutput[pose.outputBase+nodeBase+node]=result;
  }
  DeviceMemoryBarrierWithGroupSync();
 }
 uint morphs=nativeAnimation.Load(base+16),morphTable=base+nativeAnimation.Load(base+20);
 for(uint i=lane;i<morphs;i+=64){
  uint3 m=nativeAnimation.Load3(morphTable+i*16);float3 p=0,n=0,t=0;
  for(uint j=0;j<m.y;++j){float weight=gltfWeight(base,pose.gltfClip,m.x,j,pose.gltfTime);
   if(pose.gltfBlend<1)weight=lerp(gltfWeight(base,pose.gltfPrevious,m.x,j,pose.gltfPreviousTime),weight,pose.gltfBlend);
   uint delta=base+m.z+j*36;
   p+=weight*asfloat(nativeAnimation.Load3(delta));n+=weight*asfloat(nativeAnimation.Load3(delta+12));t+=weight*asfloat(nativeAnimation.Load3(delta+24));
  }
  NativePoseNode result=(NativePoseNode)0;
  result.row0=float4(p,1);result.row1=float4(n,0);result.row2=float4(t,0);result.generation=pose.generation;
  nativePoseOutput[pose.outputBase+nodeBase+nodes+i]=result;
 }
}
)";
} // namespace owned::gltf
