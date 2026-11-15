#pragma once
// SKM/SKC v1: immutable, equipment-selected baked affine motion. No author solver.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace owned::skm {
using Matrix=std::array<double,16>; // row major, column vectors
inline Matrix identity(){return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
inline Matrix mul(const Matrix&a,const Matrix&b){Matrix c{};for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j)for(unsigned k=0;k<4;++k)c[i*4+j]+=a[i*4+k]*b[k*4+j];return c;}
inline std::array<double,3> vector(const Matrix&m,const double*p){return {m[0]*p[0]+m[1]*p[1]+m[2]*p[2],m[4]*p[0]+m[5]*p[1]+m[6]*p[2],m[8]*p[0]+m[9]*p[1]+m[10]*p[2]};}
inline std::array<double,3> normal(const Matrix&m,const double*p){
 const double a=m[0],b=m[1],c=m[2],d=m[4],e=m[5],f=m[6],g=m[8],h=m[9],i=m[10];
 const double det=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g);
 if(std::abs(det)<1e-15)throw std::runtime_error("SKM singular placement");
 std::array<double,3> n{((e*i-f*h)*p[0]+(f*g-d*i)*p[1]+(d*h-e*g)*p[2])/det,
 ((c*h-b*i)*p[0]+(a*i-c*g)*p[1]+(b*g-a*h)*p[2])/det,
 ((b*f-c*e)*p[0]+(c*d-a*f)*p[1]+(a*e-b*d)*p[2])/det};
 double length=std::sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);if(length<1e-12)throw std::runtime_error("SKM zero normal");for(auto&v:n)v/=length;return n;
}
inline std::uint64_t hash(const std::vector<std::uint8_t>&b){std::uint64_t h=14695981039346656037ull;for(auto v:b)h=(h^v)*1099511628211ull;return h;}
struct Reader {
 const std::vector<std::uint8_t>&bytes;std::size_t at=0;
 void read(void*p,std::size_t n){if(at>bytes.size()||n>bytes.size()-at)throw std::runtime_error("SKM truncated resource");std::memcpy(p,bytes.data()+at,n);at+=n;}
 std::uint32_t u(){std::uint32_t v;read(&v,4);return v;}
 std::uint64_t q(){std::uint64_t v;read(&v,8);return v;}
 float f(){float v;read(&v,4);if(!std::isfinite(v))throw std::runtime_error("SKM nonfinite value");return v;}
 unsigned count(unsigned max){auto v=u();if(v>max)throw std::runtime_error("SKM count limit");return v;}
 std::string s(){auto n=count(256);std::string v(n,'\0');read(v.data(),n);if(v.empty()||v.find('\0')!=std::string::npos)throw std::runtime_error("SKM invalid name");return v;}
 void header(const char*magic,unsigned version=1){char v[4];read(v,4);if(std::memcmp(v,magic,4)||u()!=version)throw std::runtime_error("SKM unsupported version");}
 void end(){if(at!=bytes.size())throw std::runtime_error("SKM trailing bytes");}
};
struct Node{std::string name;std::int32_t parent=-1;Matrix rebase=identity();};
struct Mount{std::string name;unsigned anchor=0;};
struct ClipRef{std::string resource;std::uint64_t hash=0;};
struct Context{std::string name,neutral;std::set<std::string> equipment;std::map<std::string,ClipRef> clips;};
struct Asset {
 std::uint64_t mesh_hash=0,skeleton_hash=0;
 std::array<float,3> shape{};float radius=0;
 std::string correction_clip,root_name;
 std::vector<Node> nodes;std::vector<unsigned> order;std::vector<Mount> mounts;
 std::vector<std::int32_t> correction_map;
 std::vector<float> correction_phases,corrections;unsigned correction_count=0;
 std::vector<Context> contexts;
};
inline void phases(Reader&r,std::vector<float>&v){float last=-1;for(auto&f:v){f=r.f();if(f<0||f<=last)throw std::runtime_error("SKM phases not strictly increasing");last=f;}if(v.empty()||v.front()!=0)throw std::runtime_error("SKM missing initial phase");}
inline Asset decode(const std::vector<std::uint8_t>&bytes){
 Reader r{bytes};r.header("SKM1");Asset a;a.mesh_hash=r.q();a.skeleton_hash=r.q();for(auto&v:a.shape)v=r.f();a.radius=r.f();
 if(a.radius<=0||a.radius>100)throw std::runtime_error("SKM invalid bounds");
 auto nodes=r.count(4096),vertices=r.count(1000000),frames=r.count(100000);a.correction_count=r.count(1000000);auto mounts=r.count(16),contexts=r.count(128);
 if(!nodes||!vertices||!contexts||!frames||std::uint64_t(frames)*a.correction_count>10000000)throw std::runtime_error("SKM invalid dimensions");
 a.correction_clip=r.s();a.root_name=r.s();a.nodes.resize(nodes);std::set<std::string> names;
 for(auto&n:a.nodes){n.name=r.s();if(!names.insert(n.name).second)throw std::runtime_error("SKM duplicate node");n.parent=static_cast<std::int32_t>(r.u());if(n.parent< -1||n.parent>=int(nodes))throw std::runtime_error("SKM invalid parent");for(unsigned i=0;i<12;++i)n.rebase[i]=r.f();}
 std::vector<bool> visited(nodes);while(a.order.size()<nodes){auto old=a.order.size();for(unsigned i=0;i<nodes;++i)if(!visited[i]&&(a.nodes[i].parent<0||visited[a.nodes[i].parent])){a.order.push_back(i);visited[i]=true;}if(old==a.order.size())throw std::runtime_error("SKM skeleton cycle");}
 for(unsigned i=0;i<mounts;++i){Mount m;m.name=r.s();m.anchor=r.u();if(m.anchor>=nodes||!names.insert(m.name).second)throw std::runtime_error("SKM invalid mount");a.mounts.push_back(m);}
 a.correction_map.resize(vertices);for(auto&v:a.correction_map){v=static_cast<std::int32_t>(r.u());if(v< -1||v>=int(a.correction_count))throw std::runtime_error("SKM correction index");}
 a.correction_phases.resize(frames);phases(r,a.correction_phases);
 a.corrections.resize(std::size_t(frames)*a.correction_count*6);for(auto&v:a.corrections)v=r.f();
 std::set<std::set<std::string>> signatures;
 for(unsigned i=0;i<contexts;++i){Context c;c.name=r.s();auto count=r.count(16);for(unsigned j=0;j<count;++j)if(!c.equipment.insert(r.s()).second)throw std::runtime_error("SKM duplicate equipment");if(!signatures.insert(c.equipment).second)throw std::runtime_error("SKM ambiguous context");c.neutral=r.s();auto clips=r.count(10000);for(unsigned j=0;j<clips;++j){auto name=r.s();ClipRef ref;ref.resource=r.s();ref.hash=r.q();if(ref.resource.find_first_of("/\\:")!=std::string::npos||!c.clips.emplace(name,ref).second)throw std::runtime_error("SKM invalid clip reference");}if(!c.clips.count(c.neutral))throw std::runtime_error("SKM neutral clip missing");a.contexts.push_back(std::move(c));}
 r.end();return a;
}
struct Track {std::vector<std::array<float,16>> keys;}; // qWXYZ, H3x3, t3
struct Clip{std::uint64_t skeleton_hash=0;std::string name;float scale=1;std::vector<float> phases;std::vector<Track> tracks;};
inline Clip decode_clip(const std::vector<std::uint8_t>&bytes,const Asset&a,const std::string&name){
 Reader r{bytes};r.header("SKC1",2);Clip c;c.skeleton_hash=r.q();c.name=r.s();if(c.skeleton_hash!=a.skeleton_hash||c.name!=name)throw std::runtime_error("SKC identity mismatch");
 auto frames=r.count(100000),tracks=r.count(8192);if(!frames||tracks!=a.nodes.size()+1+a.mounts.size())throw std::runtime_error("SKC dimensions");c.scale=r.f();if(c.scale<=0||c.scale>100)throw std::runtime_error("SKC invalid scale");c.phases.resize(frames);phases(r,c.phases);c.tracks.resize(tracks);
 for(auto&t:c.tracks){auto count=r.u();if(count!=1&&count!=frames)throw std::runtime_error("SKC key count");if(std::uint64_t(count)*64>bytes.size()-r.at)throw std::runtime_error("SKC truncated track");t.keys.resize(count);
  for(unsigned component=0;component<16;++component){std::uint32_t previous=0;for(unsigned frame=0;frame<count;++frame){std::uint32_t delta=0;for(unsigned lane=0;lane<4;++lane)delta|=std::uint32_t(bytes[r.at+(component*4+lane)*count+frame])<<(lane*8);previous+=delta;float value;std::memcpy(&value,&previous,4);if(!std::isfinite(value))throw std::runtime_error("SKC nonfinite factor");t.keys[frame][component]=value;}}
  r.at+=std::size_t(count)*64;for(auto&key:t.keys){double norm=0;for(unsigned k=0;k<4;++k)norm+=key[k]*key[k];if(std::abs(norm-1)>1e-4)throw std::runtime_error("SKC invalid quaternion");}}
 r.end();return c;
}
struct Interval{unsigned first=0,second=0;double mix=0;};
inline Interval interval(const std::vector<float>&times,double phase){
 if(!std::isfinite(phase)||phase<0)throw std::runtime_error("SKM invalid phase");
 auto p=std::upper_bound(times.begin(),times.end(),phase);if(p==times.end())return {unsigned(times.size()-1),unsigned(times.size()-1),0};
 unsigned second=unsigned(p-times.begin());if(!second)return {};return {second-1,second,(phase-times[second-1])/(times[second]-times[second-1])};
}
inline Matrix sample(const Track&t,Interval i){
 const auto&a=t.keys[t.keys.size()==1?0:i.first];const auto&b=t.keys[t.keys.size()==1?0:i.second];double dot=0;for(unsigned k=0;k<4;++k)dot+=double(a[k])*b[k];double sign=dot<0?-1:1;dot=(std::min)(1.,std::abs(dot));double angle=std::acos(dot),sine=std::sin(angle),l=1-i.mix,r=i.mix;if(sine>1e-10){l=std::sin((1-i.mix)*angle)/sine;r=std::sin(i.mix*angle)/sine;}
 double q[4],length=0;for(unsigned k=0;k<4;++k){q[k]=a[k]*l+b[k]*sign*r;length+=q[k]*q[k];}for(auto&v:q)v/=std::sqrt(length);
 double w=q[0],x=q[1],y=q[2],z=q[3];double rotation[9]={1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w),2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w),2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)};
 Matrix out=identity();for(unsigned row=0;row<3;++row){for(unsigned col=0;col<3;++col){out[row*4+col]=0;for(unsigned k=0;k<3;++k)out[row*4+col]+=rotation[row*3+k]*(a[4+k*3+col]*(1-i.mix)+b[4+k*3+col]*i.mix);}out[row*4+3]=a[13+row]*(1-i.mix)+b[13+row]*i.mix;}return out;
}
struct Pose{Matrix root;std::vector<Matrix> raw,native,mounts;std::vector<std::array<double,6>> corrections;};
inline Pose evaluate(const Asset&a,const Clip&c,double phase){
 Pose p;auto time=interval(c.phases,phase);p.root=sample(c.tracks[a.nodes.size()],time);p.raw.resize(a.nodes.size());p.native.resize(a.nodes.size());
 for(auto n:a.order){auto local=sample(c.tracks[n],time);p.raw[n]=a.nodes[n].parent<0?local:mul(p.raw[a.nodes[n].parent],local);auto world=p.raw[n];for(unsigned j=0;j<3;++j)world[j*4+3]*=c.scale;p.native[n]=mul(p.root,mul(world,a.nodes[n].rebase));}
 for(unsigned m=0;m<a.mounts.size();++m){auto world=p.raw[a.mounts[m].anchor];for(unsigned j=0;j<3;++j)world[j*4+3]*=c.scale;p.mounts.push_back(mul(p.root,mul(world,sample(c.tracks[a.nodes.size()+1+m],time))));}
 if(c.name==a.correction_clip){auto t=interval(a.correction_phases,phase);p.corrections.resize(a.correction_count);for(unsigned v=0;v<a.correction_count;++v)for(unsigned k=0;k<6;++k)p.corrections[v][k]=a.corrections[(std::size_t(t.first)*a.correction_count+v)*6+k]*(1-t.mix)+a.corrections[(std::size_t(t.second)*a.correction_count+v)*6+k]*t.mix;}
 return p;
}
} // namespace owned::skm
