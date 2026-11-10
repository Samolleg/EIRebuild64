#pragma once
#include "../UnityCompleteData.h"
#include "EditorSchema.h"
// Model-authoring transforms. Rebind each renderer independently after changing
// the neutral skeleton; share this header with the future game preset reader.
namespace NalaEditor {
using namespace UnityBody;namespace E=EditorSchema;
inline bool Desc(const Data&d,unsigned node,unsigned root){int i=int(node);while(i>=0){if(unsigned(i)==root)return true;i=d.parent[unsigned(i)];}return false;}
inline bool Identity(V s){return s.x==1&&s.y==1&&s.z==1;}
inline M Diagonal(V s){M m;m.r[0][0]=s.x;m.r[1][1]=s.y;m.r[2][2]=s.z;return m;}
inline void Region(const Data&d,std::vector<M>&rest,unsigned pivot,std::initializer_list<unsigned>stops,V scale,bool canonical=false){
 if(Identity(scale))return;M frame=canonical?M{}:Proper(rest[pivot]);frame.t=rest[pivot].t;M affine=Product(Product(frame,Diagonal(scale)),Inverse(frame));
 std::vector<std::pair<unsigned,V>>terminal;for(unsigned stop:stops)terminal.push_back({stop,Sub(Point(affine,rest[stop].t),rest[stop].t)});
 for(unsigned i=0;i<rest.size();++i)if(Desc(d,i,pivot)){int stopped=-1;for(unsigned k=0;k<terminal.size();++k)if(Desc(d,i,terminal[k].first)){stopped=int(k);break;}
  if(stopped<0)rest[i]=Product(affine,rest[i]);else rest[i].t=Add(rest[i].t,terminal[unsigned(stopped)].second);}
}
inline void Shift(const Data&d,std::vector<M>&rest,unsigned pivot,V v){if(Dot(v,v)==0)return;for(unsigned i=0;i<rest.size();++i)if(Desc(d,i,pivot))rest[i].t=Add(rest[i].t,v);}
inline M NormalMatrix(const M&m){M inv=Inverse(m),r;for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)r.r[i][j]=inv.r[j][i];return r;}
inline M Zero(){M m;for(auto&row:m.r)for(double&x:row)x=0;return m;}
inline bool Apply(Data&d,const std::vector<double>&v,const std::vector<double>&bones,double globalScale){
 if(v.size()!=E::Count||bones.size()!=E::BoneCount*9)return false;auto original=d.rest,rest=d.rest;
 // Stature changes longitudinal segments, not the global scale. Terminal
 // subtrees translate with their attachment, retaining their own size/shape.
 // Breast render/physics roots must all stop here; otherwise torso elongation
 // stretches the breasts rather than merely moving their attachment upward.
 const double stature=v[E::stature];
 Region(d,rest,1,{2},{1,stature,1});
 Region(d,rest,2,{3},{1,stature,1});
 Region(d,rest,3,{73,4,34,66,68,139,140,141,148},{1,stature,1});
 for(unsigned side=0;side<2;++side){unsigned upper=side?5:35,forearm=side?8:38,hand=side?12:42,thigh=side?157:173,calf=side?160:176,foot=side?164:180;
  Region(d,rest,upper,{forearm},{1,stature,1});
  Region(d,rest,forearm,{hand},{1,stature,1});
  Region(d,rest,thigh,{calf},{1,stature,1});
  Region(d,rest,calf,{foot},{1,stature,1});
 }
 Region(d,rest,2,{3},{1,v[E::waistLength],1});
 Region(d,rest,3,{73,5,35},{v[E::chestWidth],v[E::chestLength],v[E::chestDepth]});
 Region(d,rest,156,{157,173},{v[E::hipWidth],v[E::hipDepth],1},true);
 for(unsigned side=0;side<2;++side){unsigned upper=side?5:35,forearm=side?8:38,hand=side?12:42,thigh=side?157:173,calf=side?160:176,foot=side?164:180;
  Region(d,rest,upper,{forearm},{v[E::upperArmWidth],v[E::upperArmLength],v[E::upperArmDepth]});
  Region(d,rest,forearm,{hand},{v[E::forearmWidth],v[E::forearmLength],v[E::forearmDepth]});
  Region(d,rest,hand,{},{v[E::handWidth],v[E::handLength],v[E::handDepth]});
  Region(d,rest,thigh,{calf},{v[E::thighWidth],v[E::thighLength],v[E::thighDepth]});
  Region(d,rest,calf,{foot},{v[E::calfWidth],v[E::calfLength],v[E::calfDepth]});
  Region(d,rest,foot,{},{v[E::footWidth],v[E::footLength],v[E::footHeight]});
  unsigned glute=side?350:344;Region(d,rest,glute,{},{v[E::gluteWidth],v[E::gluteDepth],v[E::gluteHeight]},true);Shift(d,rest,glute,{0,0,v[E::gluteRaise]*.001/globalScale});
  unsigned breast=side?141:148;Region(d,rest,breast,{},{v[E::breastWidth],v[E::breastDepth],v[E::breastHeight]},true);
  V move{(side?-1.:1.)*v[E::breastSpread]*.001/globalScale,0,v[E::breastRaise]*.001/globalScale};
  for(unsigned i=139;i<=154;++i)if((side&&(i==139||(i>=141&&i<=147)))||(!side&&(i==140||i>=148)))rest[i].t=Add(rest[i].t,move);
 }
 Region(d,rest,73,{76},{v[E::neckWidth],v[E::neckLength],v[E::neckWidth]});
 Region(d,rest,76,{},{v[E::headSize],v[E::headSize],v[E::headSize]});
 constexpr double radians=3.14159265358979323846/180.;
 for(unsigned bone=0;bone<E::BoneCount;++bone){const double*p=&bones[bone*9];bool active=false;for(unsigned j=0;j<9;++j)active|=p[j]!=0;if(!active)continue;
  M scale=Diagonal({1+p[6],1+p[7],1+p[8]});Q q{1,0,0,0};for(unsigned axis=0;axis<3;++axis){double a=p[3+axis]*radians/2;Q turn{std::cos(a),0,0,0};turn[axis+1]=std::sin(a);q=Mul(q,turn);}
  M change=Product(Rotation(q),scale);change.t={p[0]*.001/globalScale,p[1]*.001/globalScale,p[2]*.001/globalScale};M affine=Product(Product(rest[bone],change),Inverse(rest[bone]));
  for(unsigned i=0;i<rest.size();++i)if(Desc(d,i,bone))rest[i]=Product(affine,rest[i]);
 }
 bool changed=false;std::vector<M>delta(rest.size());
 for(unsigned i=0;i<rest.size();++i){delta[i]=Product(rest[i],Inverse(original[i]));for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)changed|=rest[i].r[r][c]!=original[i].r[r][c];changed|=Dot(Sub(rest[i].t,original[i].t),Sub(rest[i].t,original[i].t))!=0;}
 if(!changed)return true;
 std::vector<M>oldSkin(d.bindInverse.size()),newSkin(oldSkin.size()),oldNormal(oldSkin.size()),newNormal(oldSkin.size());
 for(unsigned i=0;i<oldSkin.size();++i){unsigned node=d.bindNode.empty()?i:d.bindNode[i];oldSkin[i]=Product(original[node],d.bindInverse[i]);newSkin[i]=Product(rest[node],d.bindInverse[i]);oldNormal[i]=NormalMatrix(oldSkin[i]);newNormal[i]=NormalMatrix(newSkin[i]);}
 for(unsigned i=0;i<d.vertices.size();++i){M neutral=Zero(),neutralNormal=Zero();V target{},normal{};
  for(unsigned k=0;k<4;++k){unsigned slot=d.skin[i].bone[k];double w=d.skin[i].weight[k];if(!w)continue;target=Add(target,Scale(Point(newSkin[slot],d.vertices[i]),w));normal=Add(normal,Scale(Rotate(newNormal[slot],d.normals[i]),w));
   for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c){neutral.r[r][c]+=oldSkin[slot].r[r][c]*w;neutralNormal.r[r][c]+=oldNormal[slot].r[r][c]*w;}neutral.t=Add(neutral.t,Scale(oldSkin[slot].t,w));}
  d.vertices[i]=Point(Inverse(neutral),target);d.normals[i]=Unit(Rotate(Inverse(neutralNormal),normal));if(!std::isfinite(Dot(d.vertices[i],d.vertices[i])))return false;
 }
 // Preserve the original neutral renderer mapping S0=oldRest*oldBind.
 // Eye/teeth/tongue renderers do not share the body's bind coordinate frame.
 for(unsigned i=0;i<d.bindInverse.size();++i){unsigned node=d.bindNode.empty()?i:d.bindNode[i];d.bindInverse[i]=Product(Inverse(rest[node]),Product(original[node],d.bindInverse[i]));}
 d.rest=rest;for(unsigned i=0;i<rest.size();++i){d.restQ[i]=Quaternion(rest[i]);d.stretch[i]=Product(Inverse(Rotation(d.restQ[i])),rest[i]);d.stretch[i].t={};if(d.parent[i]>=0)d.local[i]=Product(Inverse(rest[unsigned(d.parent[i])]),rest[i]);}
 for(auto&limit:d.limits)limit.frame=Product(Proper(delta[limit.parent]),limit.frame);
 return true;
}
}
