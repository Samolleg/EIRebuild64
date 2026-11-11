// Offline bind-mesh extraction. Section 9 is essential: a lid edit is a blend
// of skinned sources, not a skin with its influences truncated to four bones.
#include "skn_asset.h"
#define NALA_SEPARATE_EYE_UV 1
#include "nala/UnityBodyAppearance.h"
#include "nala/head105_v1/NalaFaceSourceData.h"
#include "nala/head105_v1/NalaUpperArcIdentity.h"
#include "nala/morphs_head105_v1/NalaBodyShape.h"
#include <numeric>
#include "shape_export.h"

namespace {
void f32(Bytes& b,double x) {
  float f=float(x);if(!std::isfinite(f))throw std::runtime_error("nonfinite bind value");
  unsigned bits;std::memcpy(&bits,&f,4);u32(b,bits);
}
void u16(Bytes& b,unsigned x) {b.push_back(std::uint8_t(x));b.push_back(std::uint8_t(x>>8));}
unsigned text(Bytes& b,const std::string& s) {
  unsigned at=unsigned(b.size());b.insert(b.end(),s.begin(),s.end());b.push_back(0);return at;
}
unsigned half(float f) {
  unsigned x;std::memcpy(&x,&f,4);
  unsigned sign=(x>>16)&32768,m=x&0x7fffff,e=(x>>23)&255;
  if(e==255||e>142)throw std::runtime_error("invalid half UV");
  int exponent=int(e)-112;
  if(exponent<=0){if(exponent<-10)return sign;m=(m|0x800000)>>(1-exponent);return sign+((m+4095+((m>>13)&1))>>13);}
  return sign+(unsigned(exponent)<<10)+((m+4095+((m>>13)&1))>>13);
}
Bytes envelope(const std::vector<Bytes>& tables,const std::vector<unsigned>& stride) {
  Bytes b;for(unsigned x:{16u,unsigned(tables.size()-1),16u,0u})u32(b,x);
  b.resize(16+16*(tables.size()-1));
  for(unsigned k=1;k<tables.size();++k){
    while(b.size()%16)b.push_back(0);
    unsigned at=16+(k-1)*16;set32(b,at,k);set32(b,at+4,unsigned(b.size()));
    set32(b,at+8,unsigned(tables[k].size()/stride[k]));set32(b,at+12,stride[k]);
    b.insert(b.end(),tables[k].begin(),tables[k].end());
  }return b;
}
Bytes bind_mesh(const Data& d) {
  const unsigned sources=unsigned(d.vertices.size()),visible=NalaLidContact::RenderCount;
  if(sources!=NalaLidContact::NativeCount+std::size(NalaLidContact::Proxies))
    throw std::runtime_error("bind source/proxy topology mismatch");
  std::vector<V> p,n;
  if(!NalaLidContact::Render(d.vertices,d.normals,p,n))throw std::runtime_error("lid render topology rejected");
  std::array<Bytes,15> s;s[1].push_back(0);
  // In the section-13 mode section 2 enumerates skin slots. The authoritative
  // affine bind frames are in section 13, which also preserves duplicate slots.
  for(unsigned i=0;i<d.bindInverse.size();++i){
    u32(s[2],text(s[1],"nala.slot."+std::to_string(i)));u32(s[2],~0u);
    for(double v:{0.,0.,0.,0.,0.,0.,1.,1.,1.,1.})f32(s[2],v);
    for(float v:owned::skn::identity())f32(s[2],v);
  }
  V lo{1e30,1e30,1e30},hi{-1e30,-1e30,-1e30};
  auto vertex=[&](V position,V normal,const Skin& skin,float u,float v){
    for(double x:{position.x,position.y,position.z})f32(s[3],x);
    normal=Unit(normal);V tangent=Unit(std::abs(normal.z)<.9?V{-normal.y,normal.x,0}:V{0,-normal.z,normal.y});
    for(V vector:{normal,tangent}){for(double x:{vector.x,vector.y,vector.z})u16(s[3],unsigned(std::int16_t(std::lround(x*32767))));u16(s[3],Dot(vector,tangent)>.999?32767:0);}
    u16(s[3],half(u));u16(s[3],half(v));
    double sum=0;for(unsigned k=0;k<4;++k){if(skin.bone[k]>=d.bindInverse.size()||!std::isfinite(skin.weight[k])||skin.weight[k]<0)throw std::runtime_error("invalid source influence");sum+=skin.weight[k];u16(s[3],skin.bone[k]);}
    if(std::abs(sum-1)>1e-5)throw std::runtime_error("source weights not normalized");
    // Largest-remainder quantization preserves the exact integer weight sum.
    std::array<unsigned,4> q{};std::array<double,4> remainder{};unsigned total=0;
    for(unsigned k=0;k<4;++k){double x=skin.weight[k]/sum*65535;q[k]=unsigned(std::floor(x));total+=q[k];remainder[k]=x-q[k];}
    while(total<65535){auto k=unsigned(std::max_element(remainder.begin(),remainder.end())-remainder.begin());++q[k];remainder[k]=-1;++total;}
    for(unsigned x:q)u16(s[3],x);
    lo={std::min(lo.x,position.x),std::min(lo.y,position.y),std::min(lo.z,position.z)};
    hi={std::max(hi.x,position.x),std::max(hi.y,position.y),std::max(hi.z,position.z)};
  };
  for(unsigned i=0;i<sources;++i)vertex(d.vertices[i],d.normals[i],d.skin[i],0,0);
  // Visible vertices are overwritten by section-9 anchors after skinning.
  // Their placeholder skin is never a reduction of an edit's influences.
  const float* uv=NalaLidContact::UV();
  for(unsigned i=0;i<visible;++i)vertex(p[i],n[i],d.skin[0],uv[2*i],uv[2*i+1]);
  for(unsigned i:NalaLidContact::Indices){if(i>=visible)throw std::runtime_error("render index out of range");u32(s[4],sources+i);}
  for(unsigned x:{0u,unsigned(std::size(NalaLidContact::Indices)),0u,0u})u32(s[5],x);
  for(unsigned x:{0u,sources+visible,0u,unsigned(std::size(NalaLidContact::Indices)),0u,1u})u32(s[6],x);
  f32(s[6],0);f32(s[6],.1);
  for(double x:{lo.x,lo.y,lo.z,hi.x,hi.y,hi.z})f32(s[6],x);
  for(double x:{1.,1.,1.,1.,0.,0.,0.,.6,0.,1.,.5})f32(s[7],x);
  u32(s[7],0);u32(s[7],0);u32(s[7],text(s[1],"nala/201.dds"));
  for(unsigned i=0;i<5;++i)u32(s[7],0);
  std::vector<Bytes> tables(7);
  for(unsigned x:{text(s[1],"nala.sources"),0u,sources,0u,0u,0u,1u,0u,
                  text(s[1],"nala.visible"),sources,visible,0u,unsigned(std::size(NalaLidContact::Indices)),0u,0u,0u})u32(tables[1],x);
  for(unsigned x:{0u,0u,1u,0u,visible,0u,0u,0u})u32(tables[2],x);
  for(unsigned i=0;i<visible;++i){
    std::array<unsigned,3> source{};std::array<double,3> weight{1,0,0};
    if(i<std::size(NalaAppearance::SourceVertex))source.fill(NalaAppearance::SourceVertex[i]);
    bool found=i<std::size(NalaAppearance::SourceVertex);
    for(const auto& edit:NalaLidContact::Edits)if(edit.render==i){for(unsigned k=0;k<3;++k){source[k]=edit.source[k];weight[k]=edit.weight[k];}found=true;}
    if(!found)throw std::runtime_error("visible vertex has no source mapping");
    u32(tables[3],i);for(unsigned x:source){if(x>=sources)throw std::runtime_error("lid source out of range");u32(tables[3],x);}
    for(double x:weight)f32(tables[3],x);
    for(unsigned k=0;k<5;++k)u32(tables[3],0);
  }
  s[8]=shape_section();s[14]=attachment_section();
  s[9]=envelope(tables,{0,32,32,48,12,8,16});s[13]=encode(d);
  const std::vector<unsigned> kinds{1,2,3,4,5,6,7,8,9,13,14};
  const unsigned strides[]={0,1,112,48,4,16,56,76,1,1,1,1,4,1,1};
  Bytes b(32+24*kinds.size());set32(b,0,0x004e4b53);set32(b,4,1);set32(b,12,32);set32(b,16,unsigned(kinds.size()));set32(b,20,32);
  for(unsigned i=0;i<kinds.size();++i){unsigned k=kinds[i];while(b.size()%16)b.push_back(0);unsigned at=32+i*24;
    set32(b,at,k);set32(b,at+4,unsigned(b.size()));set32(b,at+8,unsigned(s[k].size()));set32(b,at+12,unsigned(s[k].size()/strides[k]));set32(b,at+16,strides[k]);set32(b,at+20,1);b.insert(b.end(),s[k].begin(),s[k].end());}
  set32(b,8,unsigned(b.size()));
  owned::skn::Asset decoded;std::string error;
  if(!owned::skn::decode(b,decoded,error))throw std::runtime_error(error);
  if(encode(*decoded.nala_rig)!=s[13])throw std::runtime_error("bind rig round trip mismatch");
  // Check the shape program read from the finished container against the
  // accepted authoring result before publishing the model.
  const auto controls=owned::skn::nala_shape::resolve(*decoded.nala_shape,{.3,.3,.85,25,25});
  Data rebuilt;owned::skn::nala_shape::build(*decoded.nala_shape,controls,rebuilt);
  if(rebuilt.vertices.size()!=d.vertices.size())throw std::runtime_error("serialized shape vertex count");
  for(unsigned i=0;i<d.vertices.size();++i) {
    const V dp=Sub(rebuilt.vertices[i],d.vertices[i]),dn=Sub(rebuilt.normals[i],d.normals[i]);
    if(Dot(dp,dp)>1e-20 || Dot(dn,dn)>1e-20)throw std::runtime_error("serialized shape differs at vertex "+std::to_string(i));
  }
  const auto expected=owned::skn::nala_pose::decode(s[13]);
  auto same_matrix=[](const M& a,const M& b) {
    V dt=Sub(a.t,b.t);if(Dot(dt,dt)>1e-20)return false;
    for(unsigned row=0;row<3;++row)for(unsigned col=0;col<3;++col)if(std::abs(a.r[row][col]-b.r[row][col])>1e-10)return false;
    return true;
  };
  for(unsigned i=0;i<expected.rest.size();++i)
    if(!same_matrix(rebuilt.rest[i],expected.rest[i]) || !same_matrix(rebuilt.local[i],expected.local[i]) || !same_matrix(rebuilt.stretch[i],expected.stretch[i]))
      throw std::runtime_error("serialized shape rig differs at node "+std::to_string(i));
  for(unsigned i=0;i<expected.bindInverse.size();++i)
    if(!same_matrix(rebuilt.bindInverse[i],expected.bindInverse[i]))throw std::runtime_error("serialized shape inverse bind differs");
  // Verify the serialized correspondence against the accepted render function,
  // including all lid edits. Source renderer-local coordinates can exceed 100;
  // use the SKN 1e-4 metre tolerance for their binary32 conversion.
  // This is part of cooking, not a runtime CPU path.
  double worst=0;
  for(const auto& c:decoded.nala_contacts){
    V position{},normal{};
    for(unsigned k=0;k<3;++k){const auto& v=decoded.vertices[c.source[k]];
      position=Add(position,Scale({v.position[0],v.position[1],v.position[2]},c.weight[k]));
      normal=Add(normal,Scale(Unit({v.normal[0]/32767.,v.normal[1]/32767.,v.normal[2]/32767.}),c.weight[k]));}
    const auto index=c.target-sources;V difference=Sub(position,p[index]);
    worst=std::max(worst,std::sqrt(Dot(difference,difference)));
    if(Dot(Unit(normal),n[index])<.99999)throw std::runtime_error("serialized contact normal mismatch");
  }
  if(worst>1e-4)throw std::runtime_error("serialized contact bind mismatch: "+std::to_string(worst)+" m");
  std::cout<<"serialized bind/contact maximum position error: "<<worst<<" m\n";
  return b;
}
}
