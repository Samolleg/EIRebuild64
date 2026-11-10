#pragma once
#include <array>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdint>
namespace UnityBody {
struct V {double x=0,y=0,z=0;};
struct M {double r[3][3]={{1,0,0},{0,1,0},{0,0,1}};V t;};
using Q=std::array<double,4>;
inline V Add(V a,V b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline V Sub(V a,V b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline V Scale(V a,double s){return {a.x*s,a.y*s,a.z*s};}
inline double Dot(V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline V Cross(V a,V b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline V Unit(V v){return Scale(v,1/std::max(std::sqrt(Dot(v,v)),1e-15));}
inline V Rotate(const M&m,V v){return {m.r[0][0]*v.x+m.r[0][1]*v.y+m.r[0][2]*v.z,m.r[1][0]*v.x+m.r[1][1]*v.y+m.r[1][2]*v.z,m.r[2][0]*v.x+m.r[2][1]*v.y+m.r[2][2]*v.z};}
inline V Point(const M&m,V v){return Add(Rotate(m,v),m.t);}
inline M Product(const M&a,const M&b){M m;for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j){m.r[i][j]=0;for(unsigned k=0;k<3;++k)m.r[i][j]+=a.r[i][k]*b.r[k][j];}m.t=Point(a,b.t);return m;}
inline M Inverse(const M&m){M out;const auto&a=m.r;double d=a[0][0]*(a[1][1]*a[2][2]-a[1][2]*a[2][1])-a[0][1]*(a[1][0]*a[2][2]-a[1][2]*a[2][0])+a[0][2]*(a[1][0]*a[2][1]-a[1][1]*a[2][0]);
 out.r[0][0]=(a[1][1]*a[2][2]-a[1][2]*a[2][1])/d;out.r[0][1]=(a[0][2]*a[2][1]-a[0][1]*a[2][2])/d;out.r[0][2]=(a[0][1]*a[1][2]-a[0][2]*a[1][1])/d;
 out.r[1][0]=(a[1][2]*a[2][0]-a[1][0]*a[2][2])/d;out.r[1][1]=(a[0][0]*a[2][2]-a[0][2]*a[2][0])/d;out.r[1][2]=(a[0][2]*a[1][0]-a[0][0]*a[1][2])/d;
 out.r[2][0]=(a[1][0]*a[2][1]-a[1][1]*a[2][0])/d;out.r[2][1]=(a[0][1]*a[2][0]-a[0][0]*a[2][1])/d;out.r[2][2]=(a[0][0]*a[1][1]-a[0][1]*a[1][0])/d;out.t=Scale(Rotate(out,m.t),-1);return out;
}
inline M Proper(M m){m.t={};for(unsigned pass=0;pass<5;++pass){M inv=Inverse(m),n=m;for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)n.r[i][j]=.5*(m.r[i][j]+inv.r[j][i]);m=n;}return m;}
inline Q Normalize(Q q){double n=0;for(double v:q)n+=v*v;n=std::sqrt(n);for(double&v:q)v/=n;return q;}
inline Q Mul(Q a,Q b){V av{a[1],a[2],a[3]},bv{b[1],b[2],b[3]};V v=Add(Add(Scale(av,b[0]),Scale(bv,a[0])),Cross(av,bv));return {a[0]*b[0]-Dot(av,bv),v.x,v.y,v.z};}
inline Q Inv(Q q){double n=0;for(double v:q)n+=v*v;return {q[0]/n,-q[1]/n,-q[2]/n,-q[3]/n};}
inline Q Quaternion(M m){m=Proper(m);const auto&r=m.r;Q q{};double tr=r[0][0]+r[1][1]+r[2][2];if(tr>0){double s=2*std::sqrt(1+tr);q={s/4,(r[2][1]-r[1][2])/s,(r[0][2]-r[2][0])/s,(r[1][0]-r[0][1])/s};}else{unsigned k=0;if(r[1][1]>r[k][k])k=1;if(r[2][2]>r[k][k])k=2;unsigned i=(k+1)%3,j=(k+2)%3;double s=2*std::sqrt(1+r[k][k]-r[i][i]-r[j][j]);q[k+1]=s/4;q[0]=(r[j][i]-r[i][j])/s;q[i+1]=(r[i][k]+r[k][i])/s;q[j+1]=(r[j][k]+r[k][j])/s;}return Normalize(q);}
inline M Rotation(Q q){q=Normalize(q);double w=q[0],x=q[1],y=q[2],z=q[3];M m;m.r[0][0]=1-2*(y*y+z*z);m.r[0][1]=2*(x*y-z*w);m.r[0][2]=2*(x*z+y*w);m.r[1][0]=2*(x*y+z*w);m.r[1][1]=1-2*(x*x+z*z);m.r[1][2]=2*(y*z-x*w);m.r[2][0]=2*(x*z-y*w);m.r[2][1]=2*(y*z+x*w);m.r[2][2]=1-2*(x*x+y*y);return m;}
inline Q Nlerp(Q a,Q b,double t){double d=0;for(unsigned i=0;i<4;++i)d+=a[i]*b[i];Q q;for(unsigned i=0;i<4;++i)q[i]=(1-t)*a[i]+t*b[i]*(d<0?-1:1);return Normalize(q);}
inline Q Damped(V own,V target,Q initial,Q parent,V axis){Q base=Mul(parent,initial);V f=Unit(Rotate(Rotation(base),axis)),to=Sub(target,own);to=Dot(to,to)>1e-30?Unit(to):f;V cross=Cross(to,f);double angle=std::acos(std::clamp(Dot(to,f),-1.,1.));if(Dot(cross,cross)<1e-24){if(angle<1e-8)return base;V a=std::abs(f.x)<=std::abs(f.y)&&std::abs(f.x)<=std::abs(f.z)?V{1,0,0}:(std::abs(f.y)<=std::abs(f.z)?V{0,1,0}:V{0,0,1});cross=Cross(f,a);}cross=Unit(cross);double s=std::sin(angle/2);return Mul(Inv({std::cos(angle/2),cross.x*s,cross.y*s,cross.z*s}),base);}
struct Skin {std::array<std::uint16_t,4>bone;std::array<double,4>weight;};
struct Dual {unsigned a,b,i1,i2;};
struct DampedSpec {unsigned own,target;};
struct Semi {unsigned own,target;double weight;};
struct JointLimit {unsigned own,parent;M frame;double low,high,y,z;};
struct Data {
 std::vector<M>rest,local,bindInverse,stretch;std::vector<Q>restQ;std::vector<int>parent,driver;std::vector<unsigned>order,bindNode;
 std::array<M,15>gameBind;std::array<std::vector<Dual>,2>dual;std::array<std::vector<DampedSpec>,2>damped;std::array<std::vector<Semi>,2>semi;
 std::vector<V>vertices,normals;std::vector<Skin>skin;V forward;std::vector<std::vector<unsigned>>children;
 std::vector<JointLimit>limits;
};
inline void Replace(const Data&d,std::vector<M>&world,unsigned i,Q q){M old=world[i],m=Product(Rotation(q),d.stretch[i]);m.t=old.t;world[i]=m;M delta=Product(m,Inverse(old));std::vector<unsigned>queue=d.children[i];for(unsigned k=0;k<queue.size();++k){unsigned j=queue[k];world[j]=Product(delta,world[j]);queue.insert(queue.end(),d.children[j].begin(),d.children[j].end());}}
inline Q LimitSwingTwist(Q q,const JointLimit&s,bool&changed){
 q=Normalize(q);if(q[0]<0)for(double&v:q)v=-v;
 Q twist{q[0],q[1],0,0};double magnitude=twist[0]*twist[0]+twist[1]*twist[1];twist=magnitude>1e-24?Normalize(twist):Q{1,0,0,0};
 double angle=2*std::atan2(twist[1],twist[0]),bound=std::clamp(angle,s.low,s.high);
 Q swing=Mul(q,Inv(twist));double ty=swing[2]/(1+swing[0]),tz=swing[3]/(1+swing[0]);
 double yLimit=std::tan(s.y/4),zLimit=std::tan(s.z/4),ellipse=(ty/yLimit)*(ty/yLimit)+(tz/zLimit)*(tz/zLimit);
 changed=std::abs(bound-angle)>1e-12||ellipse>1+1e-12;if(!changed)return q;
 if(ellipse>1){double factor=1/std::sqrt(ellipse);ty*=factor;tz*=factor;}
 double radius=ty*ty+tz*tz;Q limitedSwing{(1-radius)/(1+radius),0,2*ty/(1+radius),2*tz/(1+radius)};
 return Normalize(Mul(limitedSwing,{std::cos(bound/2),std::sin(bound/2),0,0}));
}
inline unsigned LimitRetarget(const Data&d,std::vector<M>&w){unsigned changed=0;
 for(const auto&s:d.limits){M parentDelta=Product(Proper(w[s.parent]),Inverse(Rotation(d.restQ[s.parent])));
  M ownDelta=Product(Proper(w[s.own]),Inverse(Rotation(d.restQ[s.own])));
  M relative=Product(Inverse(parentDelta),ownDelta),joint=Product(Product(Inverse(s.frame),relative),s.frame);
  bool limited=false;Q q=LimitSwingTwist(Quaternion(joint),s,limited);if(!limited)continue;
  M motion=Product(Product(Product(parentDelta,s.frame),Rotation(q)),Inverse(s.frame));
  Replace(d,w,s.own,Quaternion(Product(motion,Rotation(d.restQ[s.own]))));++changed;
 }return changed;
}
inline std::vector<M> Retarget(const Data&d,const std::array<M,15>&game){std::vector<M>world=d.rest;M root=Inverse(game[0]),restRoot=Inverse(d.gameBind[0]);std::array<M,15>delta;for(unsigned i=0;i<15;++i)delta[i]=Proper(Product(Product(root,game[i]),Inverse(Product(restRoot,d.gameBind[i]))));for(unsigned i:d.order){int p=d.parent[i];if(p>=0)world[i]=Product(world[unsigned(p)],d.local[i]);int driver=d.driver[i];if(driver>=0){M m=Product(delta[unsigned(driver)],d.rest[i]);m.t=world[i].t;world[i]=m;}}return world;}
inline void Control(const Data&d,std::vector<M>&w){auto q=[&](unsigned i){return Quaternion(w[i]);};auto initial=[&](unsigned i){return Mul(Inv(d.restQ[unsigned(d.parent[i])]),d.restQ[i]);};for(unsigned side=0;side<2;++side){for(const auto&s:d.dual[side]){Q qa=q(s.a),qb=q(s.b);V pa=w[s.a].t,pb=w[s.b].t;Q da=Damped(pa,Add(pa,Rotate(Rotation(qa),d.forward)),initial(s.a),q(unsigned(d.parent[s.a])),d.forward),db=Damped(pb,Add(pb,Rotate(Rotation(qb),d.forward)),initial(s.b),q(unsigned(d.parent[s.b])),d.forward);Q rolled=Mul(Mul(qa,Inv(db)),qb),o1=Mul(Inv(d.restQ[s.a]),d.restQ[s.i1]),o2=Mul(Inv(d.restQ[s.a]),d.restQ[s.i2]);Replace(d,w,s.i1,Mul(Nlerp(da,rolled,.3330000042915344),o1));Replace(d,w,s.i2,Mul(Nlerp(rolled,da,.3330000042915344),o2));}
 for(const auto&s:d.damped[side])Replace(d,w,s.own,Damped(w[s.own].t,w[s.target].t,initial(s.own),q(unsigned(d.parent[s.own])),d.forward));
 for(const auto&s:d.semi[side])Replace(d,w,s.own,Mul(Nlerp(q(unsigned(d.parent[s.own])),q(s.target),s.weight),initial(s.own)));
}}
inline bool Compute(const Data&d,const std::array<M,15>&game,std::vector<V>&out,std::vector<V>*normals=nullptr,bool controllers=true,std::vector<M>*finalWorld=nullptr,bool limits=false){auto world=Retarget(d,game);if(limits)LimitRetarget(d,world);if(controllers)Control(d,world);std::vector<M>skin(d.bindInverse.size());if(!d.bindNode.empty()&&d.bindNode.size()!=skin.size())return false;for(unsigned i=0;i<skin.size();++i){unsigned node=d.bindNode.empty()?i:d.bindNode[i];if(node>=world.size())return false;skin[i]=Product(world[node],d.bindInverse[i]);}if(d.skin.size()!=d.vertices.size()||d.normals.size()!=d.vertices.size())return false;out.assign(d.vertices.size(),{});if(normals)normals->assign(out.size(),{});for(unsigned i=0;i<out.size();++i){V n{};for(unsigned k=0;k<4;++k){const auto&s=d.skin[i];unsigned bone=s.bone[k];if(bone>=skin.size())return false;out[i]=Add(out[i],Scale(Point(skin[bone],d.vertices[i]),s.weight[k]));if(normals)n=Add(n,Scale(Rotate(skin[bone],d.normals[i]),s.weight[k]));}if(!std::isfinite(out[i].x)||!std::isfinite(out[i].y)||!std::isfinite(out[i].z))return false;if(normals)(*normals)[i]=Unit(n);}if(finalWorld)*finalWorld=std::move(world);return true;}
}
