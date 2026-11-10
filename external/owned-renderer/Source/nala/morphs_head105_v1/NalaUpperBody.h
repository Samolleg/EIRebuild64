#pragma once
#include "../UnityBodyKernel.h"
// New authoring controls, not an assertion that Unity's complete scale stack
// has been ported. Work in neutral/source space before retargeting and IK.
namespace NalaUpperBody {
using namespace UnityBody;
inline thread_local double Width=.30;
inline double Smooth(double a,double b,double x){double t=std::clamp((x-a)/(b-a),0.,1.);return t*t*(3-2*t);}
// Smoothly saturate lateral displacement, so the distal arm is translated
// rather than widened/stretched together with the torso.
inline double Capped(double x){double u=std::abs(x),a=.04,b=.08,v;
 if(u<=a)v=u;else if(u>=b)v=(a+b)*.5;else{double t=(u-a)/(b-a);v=a+(b-a)*(t-t*t*t+.5*t*t*t*t);}return std::copysign(v,x);}
inline V Map(V p){double central=1-Smooth(.035,.075,std::abs(p.x));double rise=Smooth(.16,.28,p.z);double upper=rise*(1-Smooth(.465,.525,p.z)*central)*(1-Smooth(.53,.56,p.z));
 double torso=(1-Smooth(.07,.10,std::abs(p.x)))*(1-Smooth(.37,.43,p.z));
 return {p.x+Width*Capped(p.x)*upper,p.y+.035*(p.y+.0304)*rise*torso,p.z};}
inline void Apply(Data&d){
 for(unsigned i=0;i<d.vertices.size();++i){V p=d.vertices[i];if(p.z<=.16||p.z>=.56)continue;V mapped=Map(p);
  M jac;const double h=1e-6;for(unsigned axis=0;axis<3;++axis){V a=p,b=p;double*ap=axis==0?&a.x:axis==1?&a.y:&a.z;double*bp=axis==0?&b.x:axis==1?&b.y:&b.z;*ap+=h;*bp-=h;V column=Scale(Sub(Map(a),Map(b)),.5/h);jac.r[0][axis]=column.x;jac.r[1][axis]=column.y;jac.r[2][axis]=column.z;}
  M inv=Inverse(jac),normal;for(unsigned row=0;row<3;++row)for(unsigned col=0;col<3;++col)normal.r[row][col]=inv.r[col][row];
  d.vertices[i]=mapped;d.normals[i]=Unit(Rotate(normal,d.normals[i]));
 }
 auto original=d.rest;std::vector<bool>changed(d.rest.size(),false);
 for(unsigned i=0;i<d.rest.size();++i){d.rest[i].t=Map(original[i].t);V shift=Sub(d.rest[i].t,original[i].t);changed[i]=Dot(shift,shift)>0;}
 for(unsigned i=0;i<d.rest.size();++i)if(d.parent[i]>=0&&(changed[i]||changed[unsigned(d.parent[i])]))d.local[i]=Product(Inverse(d.rest[unsigned(d.parent[i])]),d.rest[i]);
 // Each renderer has its OWN inverse bind slots; retain their rotations and
 // relative bind offsets, updating only the corresponding global translation.
 for(unsigned i=0;i<d.bindInverse.size();++i){unsigned node=d.bindNode.empty()?i:d.bindNode[i];if(!changed[node])continue;M inverseShift;inverseShift.t=Sub(original[node].t,d.rest[node].t);d.bindInverse[i]=Product(d.bindInverse[i],inverseShift);}
}
}
