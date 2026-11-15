#pragma once
// Immutable common curves + masked relative equipment layers. No author solver.
#include "skm_asset.h"
#include <functional>
#include <memory>
namespace owned::skm {
using Bytes=std::vector<std::uint8_t>;
using Factor=std::array<double,16>; // quaternion WXYZ, affine stretch 3x3, translation
struct Channel {
 unsigned kind=0,bits=32,width=0;int drop=-1;bool symmetric=false;
 std::vector<float> times;std::vector<std::uint8_t> packed;
 std::array<float,9> low{},span{};std::vector<unsigned> columns;
 std::array<float,9> key(unsigned frame)const;
 std::array<double,9> sample(double phase)const;
};
using Curve=std::shared_ptr<const Channel>;
struct Node{std::string name;std::int32_t parent=-1;Matrix rebase=identity();};
struct Mount{std::string name;unsigned anchor=0;};
struct Clip {
 std::string name;float scale=1;double first=0,last=0;
 std::vector<float> phases;
 std::vector<Curve> common,layer,mounts;
 std::vector<unsigned> mask;
};
struct ClipRef{std::shared_ptr<const Clip> clip;};
struct Context{std::string name,neutral;std::set<std::string> equipment;std::map<std::string,ClipRef> clips;};
struct Asset {
 std::array<float,3> shape{};float radius=0;
 std::string correction_clip,root_name;
 std::vector<Node> nodes;std::vector<unsigned> order;std::vector<Mount> mounts;
 std::vector<std::int32_t> correction_map;
 std::vector<float> correction_phases,corrections;unsigned correction_count=0;
 std::vector<Context> contexts;
 std::size_t unique_channels=0,packed_curve_bytes=0;
};
using ResourceReader=std::function<Bytes(const std::string&)>;
using SharedResourceReader=std::function<std::shared_ptr<const Bytes>(const std::string&)>;
Asset decode_layered(const Bytes& descriptor,const Bytes& mesh,const SharedResourceReader& read);
Asset decode_layered(const Bytes& descriptor,const Bytes& mesh,const ResourceReader& read);
std::string sha256(const Bytes& bytes);
struct Interval{unsigned first=0,second=0;double mix=0;};
inline Interval interval(const std::vector<float>&times,double phase){
 if(times.empty()||!std::isfinite(phase)||phase<0)throw std::runtime_error("NLC invalid phase");
 auto p=std::upper_bound(times.begin(),times.end(),phase);
 if(p==times.end())return {unsigned(times.size()-1),unsigned(times.size()-1),0};
 unsigned second=unsigned(p-times.begin());if(!second)return {};
 return {second-1,second,(phase-times[second-1])/(times[second]-times[second-1])};
}
inline void mix_quaternion(const double*a,const double*b,double t,double*out){
 double dot=0;for(unsigned k=0;k<4;++k)dot+=a[k]*b[k];double sign=dot<0?-1:1;
 dot=(std::min)(1.,std::abs(dot));double angle=std::acos(dot),sine=std::sin(angle),l=1-t,r=t;
 if(sine>1e-10){l=std::sin((1-t)*angle)/sine;r=std::sin(t*angle)/sine;}
 double length=0;for(unsigned k=0;k<4;++k){out[k]=a[k]*l+b[k]*sign*r;length+=out[k]*out[k];}
 if(length<1e-20)throw std::runtime_error("NLC zero quaternion");for(unsigned k=0;k<4;++k)out[k]/=std::sqrt(length);
}
inline Factor factor(const std::vector<Curve>&tracks,unsigned bone,double phase){
 Factor out{};const unsigned offsets[]={0,4,13},widths[]={4,9,3};
 for(unsigned k=0;k<3;++k){auto value=tracks.at(bone*3+k)->sample(phase);std::copy_n(value.begin(),widths[k],out.begin()+offsets[k]);}return out;
}
inline Factor compose(Factor a,const Factor&d,double weight){
 double q[4],unit[4]={1,0,0,0};mix_quaternion(unit,d.data(),weight,q);
 const double w=a[0],x=a[1],y=a[2],z=a[3];
 a[0]=w*q[0]-x*q[1]-y*q[2]-z*q[3];a[1]=w*q[1]+x*q[0]+y*q[3]-z*q[2];
 a[2]=w*q[2]-x*q[3]+y*q[0]+z*q[1];a[3]=w*q[3]+x*q[2]-y*q[1]+z*q[0];
 for(unsigned k=4;k<16;++k)a[k]+=d[k]*weight;return a;
}
inline Matrix matrix(const Factor&a,const Factor&b,double t){
 double q[4];mix_quaternion(a.data(),b.data(),t,q);double w=q[0],x=q[1],y=q[2],z=q[3];
 const double r[9]={1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w),2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w),2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)};
 Matrix out=identity();for(unsigned row=0;row<3;++row){for(unsigned col=0;col<3;++col){out[row*4+col]=0;for(unsigned k=0;k<3;++k)out[row*4+col]+=r[row*3+k]*(a[4+k*3+col]*(1-t)+b[4+k*3+col]*t);}out[row*4+3]=a[13+row]*(1-t)+b[13+row]*t;}return out;
}
inline Matrix sample_local(const Clip&c,unsigned bone,double phase,double weight){
 if(c.layer.empty()||weight==0){auto f=factor(c.common,bone,phase);return matrix(f,f,0);}
 auto i=interval(c.phases,phase);auto a=compose(factor(c.common,bone,c.phases[i.first]),factor(c.layer,bone,c.phases[i.first]),weight);
 auto b=i.second==i.first?a:compose(factor(c.common,bone,c.phases[i.second]),factor(c.layer,bone,c.phases[i.second]),weight);
 return matrix(a,b,i.mix);
}
struct Pose{Matrix root;std::vector<Matrix> raw,native,mounts;std::vector<std::array<double,6>> corrections;};
inline Pose evaluate(const Asset&a,const Clip&c,double phase,double weight=1){
 if(!std::isfinite(phase)||phase<0||!std::isfinite(weight)||weight<0||weight>1)throw std::runtime_error("NLC invalid sample request");
 Pose p;p.root=sample_local(c,unsigned(a.nodes.size()),phase,weight);p.raw.resize(a.nodes.size());p.native.resize(a.nodes.size());
 for(auto n:a.order){auto local=sample_local(c,n,phase,weight);p.raw[n]=a.nodes[n].parent<0?local:mul(p.raw[a.nodes[n].parent],local);auto world=p.raw[n];for(unsigned j=0;j<3;++j)world[j*4+3]*=c.scale;p.native[n]=mul(p.root,mul(world,a.nodes[n].rebase));}
 for(unsigned m=0;m<a.mounts.size();++m){auto world=p.raw[a.mounts[m].anchor];for(unsigned j=0;j<3;++j)world[j*4+3]*=c.scale;
  auto f=c.mounts.empty()?Factor{1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0}:factor(c.mounts,m,phase);
  p.mounts.push_back(mul(p.root,mul(world,matrix(f,f,0))));}
 if(c.name==a.correction_clip){auto t=interval(a.correction_phases,phase);p.corrections.resize(a.correction_count);for(unsigned v=0;v<a.correction_count;++v)for(unsigned k=0;k<6;++k)p.corrections[v][k]=a.corrections[(std::size_t(t.first)*a.correction_count+v)*6+k]*(1-t.mix)+a.corrections[(std::size_t(t.second)*a.correction_count+v)*6+k]*t.mix;}
 return p;
}
} // namespace owned::skm
