#pragma once
#include "ShoulderGripFrames.h"
#include "ShoulderFamilyData.h"
#include "MeleeGripFrames.h"
#include "MeleePoseCurves.h"
#include "UnityWeaponsLegacy.h"
#include "UnityContactGraspData.h"
#include "UnityHandleGeometry.h"
namespace UnityGame {struct Actor;}
namespace UnityHands {
using namespace UnityBody;using Role=UnityWeaponCatalog::Role;using Gesture=UnityHandsV7::Gesture;
struct Input:UnityHandsV7::Input {bool carryPose=false,poleDownPose=false,freeSupportPose=false;std::array<UnityHandleGeometry::Shape,2>shapes;std::array<std::vector<V>,2>bowEnvelope;std::array<V,2>bowString{};std::array<bool,2>hasBowString{};mutable std::array<M,2>mounts{};mutable std::array<bool,2>solved{};mutable std::array<double,2>objectScale{1,1};bool staffShoulderPose=false;std::array<bool,2>hasModelSizeShape{};std::array<V,2>modelSizePoint{};std::array<UnityHandleGeometry::Shape,2>modelSizeShape;bool crouchCarryPose=false;double relaxedRightCrouch=0;double transitionRightWeight=-1;Q transitionRightWrist{1,0,0,0};std::array<bool,2>meleeAction{};mutable std::array<double,2>meleeClearanceShift{};int meleeCurve=-1;double meleePhase=0,meleeConfidence=0,meleeWeight=0;int transitionFrom=-1,transitionTo=-1;double transitionWeight=0;const UnityGame::Actor* transitionActor=nullptr;bool bonesOnly=false;};
inline void ArmControl(const Data&d,std::vector<M>&w,unsigned first=0,unsigned last=2){auto q=[&](unsigned i){return Quaternion(w[i]);};
 for(unsigned side=first;side<last;++side){for(unsigned index=0;index<2;++index){const auto&s=d.dual[side][index];Q qa=q(s.a),qb=q(s.b);V pa=w[s.a].t,pb=w[s.b].t;auto initial=[&](unsigned i){return Mul(Inv(d.restQ[unsigned(d.parent[i])]),d.restQ[i]);};Q da=Damped(pa,Add(pa,Rotate(Rotation(qa),d.forward)),initial(s.a),q(unsigned(d.parent[s.a])),d.forward),db=Damped(pb,Add(pb,Rotate(Rotation(qb),d.forward)),initial(s.b),q(unsigned(d.parent[s.b])),d.forward);Q rolled=Mul(Mul(qa,Inv(db)),qb),o1=Mul(Inv(d.restQ[s.a]),d.restQ[s.i1]),o2=Mul(Inv(d.restQ[s.a]),d.restQ[s.i2]);Replace(d,w,s.i1,Mul(Nlerp(da,rolled,.3330000042915344),o1));Replace(d,w,s.i2,Mul(Nlerp(rolled,da,.3330000042915344),o2));}
  const auto&s=d.semi[side][1];Q initial=Mul(Inv(d.restQ[unsigned(d.parent[s.own])]),d.restQ[s.own]);Replace(d,w,s.own,Mul(Nlerp(q(unsigned(d.parent[s.own])),q(s.target),s.weight),initial));
 }
}
struct Bracket {unsigned a,b;double t;};
template<unsigned N>inline Bracket Range(const double(&values)[N],double x){x=std::clamp(x,values[0],values[N-1]);for(unsigned i=1;i<N;++i)if(x<=values[i])return {i-1,i,(x-values[i-1])/(values[i]-values[i-1])};return {N-1,N-1,0};}
struct Grasp {UnityContactData::Pose pose;V axis,normal;};
inline Grasp Lookup(unsigned side,const UnityBodyAppearance::Profile&p,double radius,bool round,double aspect){
 Grasp out{};const auto&r=UnityContactData::Sides[side];out.axis=r.axis;out.normal=r.normal;auto R=Range(UnityContactData::Radii,radius),A=Range(UnityContactData::Arms,p.arms),F=Range(UnityContactData::Fat,std::max(p.fat,0.)),S=Range(UnityContactData::Aspects,aspect);
 for(unsigned s=0;s<(round?1u:2u);++s)for(unsigned f=0;f<2;++f)for(unsigned a=0;a<2;++a)for(unsigned ri=0;ri<2;++ri){unsigned shape=round?0:1+(s?S.b:S.a);double weight=(round?1:(s?S.t:1-S.t))*(f?F.t:1-F.t)*(a?A.t:1-A.t)*(ri?R.t:1-R.t);const auto&v=UnityContactData::Palette[shape][f?F.b:F.a][a?A.b:A.a][ri?R.b:R.a];for(unsigned k=0;k<18;++k)out.pose.angles[k]+=v.angles[k]*weight;out.pose.centre=Add(out.pose.centre,Scale(v.centre,weight));}
 if(side==0)out.pose.centre.x=-out.pose.centre.x;out.axis=Unit({out.axis.x/p.arms,out.axis.y,out.axis.z/p.arms});return out;
}
inline void ApplyFingers(const Data&d,std::vector<M>&w,unsigned side,const Grasp&grasp,double weight,Gesture gesture){const auto&s=UnityHandsData::Sides[side];const auto&m=UnityContactData::Sides[side];weight=std::clamp(weight,0.,1.);
 for(unsigned finger=0;finger<5;++finger)for(unsigned joint=0;joint<3;++joint){unsigned bone=s.fingers[finger][joint];double angle=grasp.pose.angles[finger*3+joint];if(gesture==Gesture::Trigger&&finger==1)angle*=.3;if(gesture==Gesture::Hook&&finger>=3)angle*=.2;
  // Trigger contact is not yet geometrically identified. Keep the fitted
  // cylinder pose instead of extending the index finger through the stock.
  if(gesture==Gesture::Trigger&&finger==1)angle=grasp.pose.angles[finger*3+joint];
  Q turn=UnityHandsV6::AxisAngle(m.axes[finger][joint],angle*weight);if(finger==0&&joint==0)turn=Mul(Mul(Mul(UnityHandsV6::AxisAngle(m.opposition,grasp.pose.angles[15]*weight),UnityHandsV6::AxisAngle(m.pronation,grasp.pose.angles[16]*weight)),UnityHandsV6::AxisAngle(m.abduction,grasp.pose.angles[17]*weight)),turn);
  Replace(d,w,bone,Mul(Quaternion(w[bone]),turn));
 }
}
inline M Affine(const M&raw,const M&scaled){M result=Product(scaled,Inverse(raw));result.t={};return result;}
inline Q Orient(const M&hand,const M&affine,const Grasp&grasp,V axis,V normal){V current=Unit(Rotate(hand,grasp.axis));if(Dot(current,axis)<0)axis=Scale(axis,-1);M inverse=Inverse(affine);V a=Unit(Rotate(inverse,axis)),n=Unit(Rotate(inverse,normal));return Quaternion(Product(UnityHandsV6::Basis(a,n),Inverse(UnityHandsV6::Basis(grasp.axis,grasp.normal))));}
inline V Contact(const M&scaled,const Grasp&grasp,double scale){return Scale(Point(scaled,grasp.pose.centre),scale);}
inline bool CarryWrist(const Input&input,unsigned side){if(!input.carryPose)return false;const auto&b=input.bindings[side];if(!b.spec)return false;std::string name=b.spec->name;return b.spec->role==Role::Bow||name.rfind("rh3.pike",0)==0||name.rfind("lh3.pike",0)==0;}
inline Q NeutralWrist(const Data&d,const std::vector<M>&w,unsigned side){const auto&s=UnityHandsData::Sides[side];return Mul(Quaternion(w[s.forearm]),Mul(Inv(d.restQ[s.forearm]),d.restQ[s.hand]));}
inline Q LimitCarryWrist(const Data&d,const std::vector<M>&w,unsigned side,Q desired,double limit){const auto&s=UnityHandsData::Sides[side];Q neutral=NeutralWrist(d,w,side),error=Normalize(Mul(Inv(neutral),desired));if(error[0]<0)for(double&v:error)v=-v;V axis=Unit(Rotate(Inverse(Rotation(neutral)),Sub(w[s.hand].t,w[s.forearm].t)));double projection=error[1]*axis.x+error[2]*axis.y+error[3]*axis.z;Q twist=Normalize({error[0],axis.x*projection,axis.y*projection,axis.z*projection});Q swing=Normalize(Mul(error,Inv(twist)));double angle=2*std::acos(std::clamp(std::abs(swing[0]),0.,1.));if(angle>limit)swing=Nlerp({1,0,0,0},swing,limit/angle);return Mul(neutral,Mul(swing,twist));}
}
#include "UnityThumbContact.h"
namespace UnityHands {
inline bool BowClearance(const Data&d,const UnityBodyAppearance::Profile&p,const Input&input,std::vector<M>&w){
 for(unsigned side=0;side<2;++side){const auto&b=input.bindings[side];if(!b.spec||b.spec->role!=Role::Bow||input.grip[side]<=0)continue;const auto&s=UnityHandsData::Sides[side];
  V thigh=w[s.thigh].t,leg=Sub(w[s.calf].t,thigh),own=w[s.hand].t;double projection=std::clamp(Dot(Sub(own,thigh),leg)/std::max(Dot(leg,leg),1e-12),0.,1.);V nearest=Add(thigh,Scale(leg,projection));double distance=std::sqrt(Dot(Sub(own,nearest),Sub(own,nearest)));double weight=std::clamp((.38-distance)/.16,0.,1.);weight=weight*weight*(3-2*weight)*input.grip[side];
  // Only hands near the thigh are moved. Aiming poses keep their native reach.
  unsigned pelvis=0;for(unsigned i=0;i<d.driver.size();++i)if(d.driver[i]==0){pelvis=i;break;}M motion=Product(Proper(w[pelvis]),Inverse(Proper(d.rest[pelvis])));V outward=Unit(Rotate(motion,{side==0?1.:-1.,0,0}));double amount=.075*(1+.25*std::max(p.fat,0.)+.5*std::max(p.legs-1.,0.));V target=Add(own,Scale(outward,amount));if(!UnityHandsV6::Reach(d,w,s,target,Quaternion(w[s.hand]),weight))return false;
 }return true;
}
inline bool BowEnvelopeClearance(const Data&d,const UnityBodyAppearance::Profile&p,const Input&input,std::vector<M>&w){
 for(unsigned side=0;side<2;++side){const auto&b=input.bindings[side];if(!b.spec||b.spec->role!=Role::Bow||!input.solved[side]||input.bowEnvelope[side].empty())continue;const auto&s=UnityHandsData::Sides[side];unsigned pelvis=0;for(unsigned i=0;i<d.driver.size();++i)if(d.driver[i]==0){pelvis=i;break;}M motion=Product(Proper(w[pelvis]),Inverse(Proper(d.rest[pelvis])));V outward=Unit(Rotate(motion,{side==0?1.:-1.,0,0}));
  double spent=0;for(unsigned pass=0;pass<3;++pass){V thigh=Scale(w[s.thigh].t,p.scale),leg=Scale(Sub(w[s.calf].t,w[s.thigh].t),p.scale),direction=Unit(leg);double length2=Dot(leg,leg);V lateral=Unit(Sub(outward,Scale(direction,Dot(outward,direction)))),front=Unit(Cross(direction,lateral));double radius=p.scale*p.legs*(.115+.025*std::max(p.fat,0.)),required=0;
   for(V local:input.bowEnvelope[side]){V point=Point(input.mounts[side],local),delta=Sub(point,thigh);double along=Dot(delta,leg)/std::max(length2,1e-12);if(along<-.12||along>1.12)continue;V radial=Sub(point,Add(thigh,Scale(leg,std::clamp(along,0.,1.))));double y=Dot(radial,front),x=Dot(radial,lateral);if(std::abs(y)>=radius||x< -radius)continue;required=std::max(required,std::sqrt(std::max(radius*radius-y*y,0.))+.012*p.scale-x);}
   if(required<.001*p.scale)break;required=std::min(required,input.carryPose?std::max(0.,.03*p.scale-spent):.18*p.scale);if(required<.001*p.scale)break;V before=w[s.hand].t,target=Add(before,Scale(outward,required/p.scale));Q orientation=Quaternion(w[s.hand]);if(!UnityHandsV6::Reach(d,w,s,target,orientation,1))return false;V shift=Scale(Sub(w[s.hand].t,before),p.scale);input.mounts[side].t=Add(input.mounts[side].t,shift);spent+=std::max(0.,Dot(shift,outward));if(Dot(shift,shift)<1e-10)break;
  }
 }return true;
}
inline void BowCarryTurn(const Data&d,const UnityBodyAppearance::Profile&p,const Input&input,const std::vector<M>&w,const std::array<V,2>&centres){
 if(!input.carryPose)return;unsigned pelvis=0;for(unsigned i=0;i<d.driver.size();++i)if(d.driver[i]==0){pelvis=i;break;}
 // Reverse the rejected inward roll. Only the bow mount changes; the exact
 // reviewed wrist/fingers and the grip pivot stay fixed.
 for(unsigned side=0;side<2;++side){const auto&b=input.bindings[side];if(!b.spec||b.spec->role!=Role::Bow||!input.solved[side]||!input.hasBowString[side])continue;M&mount=input.mounts[side];V pivot=Point(mount,centres[side]),axis=Unit(Rotate(mount,b.axis)),string=Sub(Point(mount,input.bowString[side]),pivot),towardBody=Sub(Scale(w[pelvis].t,p.scale),pivot);std::string bowName=b.spec->name;double fatBlend=std::clamp((p.fat-.25)/.65,0.,1.);fatBlend=fatBlend*fatBlend*(3-2*fatBlend);double adjustment=bowName=="lh3.bwpartb01"?20.:bowName=="lh3.bwpartb03"||bowName=="lh3.bwpartb05"?15.:0.;double angle=((bowName=="lh3.bwpartb04"?62.5:25.)+fatBlend*adjustment)*3.14159265358979323846/180.;M positive=Rotation(UnityHandsV6::AxisAngle(axis,angle)),negative=Rotation(UnityHandsV6::AxisAngle(axis,-angle));bool positiveDirection=Dot(Rotate(positive,string),towardBody)<=Dot(Rotate(negative,string),towardBody);if(std::string(b.spec->name)=="lh3.bwpartb04")positiveDirection=!positiveDirection;if(bowName=="lh3.bwpartb01"||bowName=="lh3.bwpartb03"||bowName=="lh3.bwpartb05"||bowName=="lh3.bwpartb07")positiveDirection=true;M turn=positiveDirection?positive:negative;turn.t=Sub(pivot,Rotate(turn,pivot));mount=Product(turn,mount);
#ifndef NALA_BOW02_STRING_DEGREES
#define NALA_BOW02_STRING_DEGREES -35
#endif
 if(bowName=="lh3.bwpartb02"){M clearance=Rotation(UnityHandsV6::AxisAngle(axis,double(NALA_BOW02_STRING_DEGREES)*3.14159265358979323846/180.));clearance.t=Sub(pivot,Rotate(clearance,pivot));mount=Product(clearance,mount);}

 }
}
inline void DualCarryTurn(const Data&d,const UnityBodyAppearance::Profile&p,const Input&input,std::vector<M>&world,const Grasp&grasp,V centre){
 const auto&left=input.bindings[0];const auto&right=input.bindings[1];if(!input.carryPose||!left.spec||!right.spec||left.spec->role!=Role::Rod||right.spec->role!=Role::Rod||!input.solved[0]||!input.solved[1])return;
 const auto&hand=UnityHandsData::Sides[0];auto old=world;UnityBodyAppearance::ApplyScales(p,old);V axis=Unit(Sub(world[hand.hand].t,world[hand.forearm].t));Q turn=UnityHandsV6::AxisAngle(axis,-17*3.14159265358979323846/180.);Replace(d,world,hand.hand,Mul(turn,Quaternion(world[hand.hand])));
 unsigned pelvis=0;for(unsigned i=0;i<d.driver.size();++i)if(d.driver[i]==0){pelvis=i;break;}M motion=Product(Proper(world[pelvis]),Inverse(Proper(d.rest[pelvis])));V outward=Unit(Rotate(motion,{1,0,0})),target=Add(world[hand.hand].t,Scale(outward,.001/p.scale));if(!UnityHandsV6::Reach(d,world,hand,target,Quaternion(world[hand.hand]),1))return;ArmControl(d,world,0,1);
 auto now=world;UnityBodyAppearance::ApplyScales(p,now);M rotation=Product(UnityHandsV6::Basis(Rotate(now[hand.hand],grasp.axis),Rotate(now[hand.hand],grasp.normal)),Inverse(UnityHandsV6::Basis(Rotate(old[hand.hand],grasp.axis),Rotate(old[hand.hand],grasp.normal))));input.mounts[0]=Product(rotation,input.mounts[0]);input.mounts[0].t=Add(input.mounts[0].t,Sub(Contact(now[hand.hand],grasp,p.scale),Point(input.mounts[0],centre)));
}

#ifndef NALA_POLE_ELBOW_DEGREES
#define NALA_POLE_ELBOW_DEGREES 30
#endif
inline void PoleElbow(const Data&d,const UnityBodyAppearance::Profile&p,const Input&input,std::vector<M>&world,const Grasp&grasp){
 if(!input.poleDownPose||!input.solved[1]||!input.bindings[1].spec||std::string(input.bindings[1].spec->name).rfind("rh3.dpike",0)!=0)return;
 const auto&h=UnityHandsData::Sides[1];auto before=world;UnityBodyAppearance::ApplyScales(p,before);
 Q handPose=Quaternion(world[h.hand]);M torso=Product(Proper(world[3]),Inverse(Proper(d.rest[3])));
 V axis=Unit(Rotate(torso,{1,0,0}));Q flex=UnityHandsV6::AxisAngle(axis,-double(NALA_POLE_ELBOW_DEGREES)*3.14159265358979323846/180.);
 Replace(d,world,h.forearm,Mul(flex,Quaternion(world[h.forearm])));
 Replace(d,world,h.hand,handPose);ArmControl(d,world,1,2);
 auto after=world;UnityBodyAppearance::ApplyScales(p,after);
 input.mounts[1].t=Add(input.mounts[1].t,Sub(Contact(after[h.hand],grasp,p.scale),Contact(before[h.hand],grasp,p.scale)));
}


#ifndef NALA_POLE_CLEARANCE_DEGREES
#define NALA_POLE_CLEARANCE_DEGREES 6
#endif
inline void PoleBladeClearance(const Data&d,const UnityBodyAppearance::Profile&p,const Input&input,std::vector<M>&world,const Grasp&grasp,V centre){
 if(NALA_POLE_CLEARANCE_DEGREES==0||!input.poleDownPose||!input.solved[1]||!input.bindings[1].spec||std::string(input.bindings[1].spec->name).rfind("rh3.dpike",0)!=0)return;
 const auto&h=UnityHandsData::Sides[1];auto before=world;UnityBodyAppearance::ApplyScales(p,before);
 M torso=Product(Proper(world[3]),Inverse(Proper(d.rest[3])));
 V axis=Unit(Rotate(torso,{0,-1,0})),outward=Unit(Rotate(torso,{-1,0,0}));
 V down=Scale(Unit(Rotate(input.mounts[1],input.bindings[1].axis)),-1);
 if(Dot(Cross(axis,down),outward)<0)axis=Scale(axis,-1);
 Q turn=UnityHandsV6::AxisAngle(axis,(std::string(input.bindings[1].spec->name)=="rh3.dpike03"?double(NALA_POLE_CLEARANCE_DEGREES):std::string(input.bindings[1].spec->name)=="rh3.dpike02"?12.:std::string(input.bindings[1].spec->name)=="rh3.dpike00"?10.:8.)*3.14159265358979323846/180.);
 if(std::string(input.bindings[1].spec->name)=="rh3.dpike02")turn=Mul(UnityHandsV6::AxisAngle(Unit(Rotate(torso,{1,0,0})),3.*3.14159265358979323846/180.),turn);
 Replace(d,world,h.hand,Mul(turn,Quaternion(world[h.hand])));ArmControl(d,world,1,2);
 auto after=world;UnityBodyAppearance::ApplyScales(p,after);
 M rotation=Product(UnityHandsV6::Basis(Rotate(after[h.hand],grasp.axis),Rotate(after[h.hand],grasp.normal)),Inverse(UnityHandsV6::Basis(Rotate(before[h.hand],grasp.axis),Rotate(before[h.hand],grasp.normal))));
 input.mounts[1]=Product(rotation,input.mounts[1]);
 input.mounts[1].t=Add(input.mounts[1].t,Sub(Contact(after[h.hand],grasp,p.scale),Point(input.mounts[1],centre)));
}
inline void PoleBladeRoll(const Input&input,const std::vector<M>&world,V centre){
 if(!input.poleDownPose||!input.solved[1]||!input.bindings[1].spec||std::string(input.bindings[1].spec->name).rfind("rh3.dpike",0)!=0||world.size()<=165)return;
 // The inspected dpike family has its broad transverse direction near source X.
 // dpike00/01 tips are faceted; dpike02/03 are flat in this plane. Roll around the shaft, never tilt the shaft itself.
 M&mount=input.mounts[1];V axis=Unit(Rotate(mount,input.bindings[1].axis));
 auto project=[&](V v){return Unit(Sub(v,Scale(axis,Dot(v,axis))));};
 V width=project(Rotate(mount,{1,0,0})),foot=project(Sub(world[165].t,world[164].t));
 if(Dot(width,foot)<0)foot=Scale(foot,-1);
 double angle=std::atan2(Dot(axis,Cross(width,foot)),Dot(width,foot));
 V pivot=Point(mount,centre);M turn=Rotation(UnityHandsV6::AxisAngle(axis,angle));
 turn.t=Sub(pivot,Rotate(turn,pivot));mount=Product(turn,mount);
}


inline Q RelaxedCarry(const Data&,const std::vector<M>&,unsigned);
inline double StaffShoulderBlend(const Data&d,const std::vector<M>&w){const auto&h=UnityHandsData::Sides[1];M torso=Product(Proper(w[3]),Inverse(Proper(d.rest[3])));V up=Unit(Rotate(torso,{0,0,1}));double t=std::clamp((Dot(Sub(w[h.hand].t,w[h.upper].t),up)+.24)/.20,0.,1.);return t*t*(3-2*t);}
inline bool StaffShoulderContact(const Data&d,const UnityBodyAppearance::Profile&p,const std::array<M,15>&game,const Input&input,std::vector<M>&w,const Grasp&grasp,V centre){
 if(!input.staffShoulderPose||!input.solved[1])return true;
 const auto&h=UnityHandsData::Sides[1];M torso=Product(Proper(w[3]),Inverse(Proper(d.rest[3])));
 V up=Unit(Rotate(torso,{0,0,1})),front=Unit(Rotate(torso,{0,-1,0})),right=Unit(Rotate(torso,{-1,0,0}));
 double t=StaffShoulderBlend(d,w);if(t<1e-9)return true;
 auto before=w;UnityBodyAppearance::ApplyScales(p,before);
 V target=Add(w[h.upper].t,Add(Scale(right,.075*p.arms),Add(Scale(front,.20),Scale(up,-.03))));
 Q orientation=Quaternion(w[h.hand]);if(!UnityHandsV6::Reach(d,w,h,target,orientation,t))return false;
 // Author the shoulder grasp along the character's front/back plane.
 // The lower tip is in front and slightly below the grip; the head is behind.
 auto reached=w;UnityBodyAppearance::ApplyScales(p,reached);
 // Preserve the native lift/settle pitch instead of snapping straight into
 // the final shoulder angle while the hand is still rising.
 const auto&binding=input.bindings[1];
 V native=Rotate(Product(Inverse(game[0]),Product(game[unsigned(binding.driver)],binding.relative)),binding.axis);
 if(std::string(binding.spec->name)!="rh3.dstaff05")native=Rotate(Product(Inverse(game[0]),game[unsigned(binding.driver)]),V{2.6274373904099671e-17,0.99999999999999989,1.1959166717799544e-18});
 V shaft=Unit(Add(Scale(front,-native.y),Scale(up,native.z)));
 double sign=Dot(Rotate(before[h.hand],grasp.axis),Rotate(input.mounts[1],input.bindings[1].axis))<0?-1.:1.;
 M affine=Affine(w[h.hand],reached[h.hand]),inverse=Inverse(affine);
 V palm=Scale(right,-1);palm=Unit(Sub(palm,Scale(shaft,Dot(palm,shaft))));
 Q authored=Quaternion(Product(UnityHandsV6::Basis(Rotate(inverse,Scale(shaft,sign)),Rotate(inverse,palm)),Inverse(UnityHandsV6::Basis(grasp.axis,grasp.normal))));
 Replace(d,w,h.hand,Nlerp(Quaternion(w[h.hand]),authored,t));ArmControl(d,w,1,2);
 auto after=w;UnityBodyAppearance::ApplyScales(p,after);
 M turn=Product(UnityHandsV6::Basis(Rotate(after[h.hand],grasp.axis),Rotate(after[h.hand],grasp.normal)),Inverse(UnityHandsV6::Basis(Rotate(before[h.hand],grasp.axis),Rotate(before[h.hand],grasp.normal))));
 input.mounts[1]=Product(turn,input.mounts[1]);input.mounts[1].t=Add(input.mounts[1].t,Sub(Contact(after[h.hand],grasp,p.scale),Point(input.mounts[1],centre)));
 return true;
}


// The chosen source facet and contact centre belong to the handle, not to an
// animation frame.  Re-evaluating the nearest facet lets a weapon roll in a fist.
inline bool FixedHandle(const Input&input,unsigned side){const auto&b=input.bindings[side];return b.spec&&(input.meleeAction[side]||(side==1&&(input.crouchCarryPose||(std::string(b.spec->name)!="rh3.dstaff05"&&(input.staffShoulderPose||input.hasModelSizeShape[side])))));}
inline UnityHandleGeometry::Section HandleSection(const Input&input,unsigned side,const UnityHandleGeometry::Shape&shape,const M&mount,V point,V normal,V axis){
 if(!FixedHandle(input,side))return UnityHandleGeometry::Facing(shape,mount,point,normal,axis);
 const char*name=input.bindings[side].spec->name;V local=side==0?MeleeGripFrames::LeftNormal(name):ShoulderGripFrames::Normal(name);
 auto section=UnityHandleGeometry::Facing(shape,M{},point,local,input.bindings[side].axis);
 section.palmDirection=Unit(Rotate(mount,section.palmDirection));return section;
}
inline void PinHandle(const UnityBodyAppearance::Profile&p,const Input&input,const std::vector<M>&world){
 if(!FixedHandle(input,1)||!input.solved[1])return;
 const auto&b=input.bindings[1];auto section=HandleSection(input,1,input.shapes[1],M{},b.point,{},b.axis);
 auto grasp=Lookup(1,p,section.radius*input.objectScale[1]/p.scale,section.round,section.aspect);
 auto scaled=world;UnityBodyAppearance::ApplyScales(p,scaled);const auto&hand=scaled[UnityHandsData::Sides[1].hand];
 // Preserve the existing action's handle direction outside the reviewed calm
 // carry/shoulder paths. Their authored fixed sign is positive throughout.
 double sign=(input.carryPose||input.staffShoulderPose)?1.:(Dot(Rotate(hand,grasp.axis),Rotate(input.mounts[1],b.axis))<0?-1.:1.);
 M mount=Product(UnityHandsV6::Basis(Scale(Rotate(hand,grasp.axis),sign),Rotate(hand,grasp.normal)),Inverse(UnityHandsV6::Basis(b.axis,section.palmDirection)));
 for(auto&row:mount.r)for(double&v:row)v*=input.objectScale[1];
 mount.t=Sub(Contact(hand,grasp,p.scale),Rotate(mount,section.point));input.mounts[1]=mount;
}
// A pose-matched C1 arm arc, authored over the native attack. No reactive
// nearest-surface decision can change its direction between frames.
inline bool MeleePoseReach(const Data&d,std::vector<M>&w,const UnityHandsData::Side&s,V target,Q orientation){
 V shoulder=w[s.upper].t,elbow=w[s.forearm].t,wrist=w[s.hand].t;double a=std::sqrt(Dot(Sub(elbow,shoulder),Sub(elbow,shoulder))),b=std::sqrt(Dot(Sub(wrist,elbow),Sub(wrist,elbow)));V ray=Sub(target,shoulder);double distance=std::sqrt(Dot(ray,ray));if(a<.05||b<.05||distance<1e-8)return false;
 // The legacy 1 mm shortening creates a visible elbow bend even for an
 // infinitesimal correction of a straight arm, then flips that artificial
 // bend when its pole changes side. Keep a numerical epsilon instead.
 V direction=Scale(ray,1/distance);double limit=a+b-1e-8,offset=std::sqrt(Dot(Sub(target,wrist),Sub(target,wrist))),fade=std::clamp(offset/.02,0.,1.);fade=fade*fade*(3-2*fade);double softness=.006*fade;if(softness>1e-12){double x=(limit-distance)/softness;if(x< -40)distance=limit;else if(x<40)distance=limit-softness*std::log1p(std::exp(x));}distance=std::clamp(distance,std::abs(a-b)+1e-8,limit);target=Add(shoulder,Scale(direction,distance));V pole=Sub(Sub(elbow,shoulder),Scale(direction,Dot(Sub(elbow,shoulder),direction)));if(Dot(pole,pole)<1e-10){pole=Cross(direction,Rotate(w[s.upper],{0,0,1}));if(Dot(pole,pole)<1e-10)pole=Cross(direction,{1,0,0});}pole=Unit(pole);double x=(a*a+distance*distance-b*b)/(2*distance),height=std::sqrt(std::max(0.,a*a-x*x));V desiredElbow=Add(shoulder,Add(Scale(direction,x),Scale(pole,height)));
 Replace(d,w,s.upper,Mul(UnityHandsV6::Between(Sub(elbow,shoulder),Sub(desiredElbow,shoulder)),Quaternion(w[s.upper])));Replace(d,w,s.forearm,Mul(UnityHandsV6::Between(Sub(w[s.hand].t,w[s.forearm].t),Sub(target,w[s.forearm].t)),Quaternion(w[s.forearm])));Replace(d,w,s.hand,orientation);return std::sqrt(Dot(Sub(w[s.hand].t,target),Sub(w[s.hand].t,target)))<1e-6;
}
inline bool MeleeHeadClearance(const Data&d,const UnityBodyAppearance::Profile&p,const Input&input,std::vector<M>&w,const std::array<Grasp,2>&grasp){
 if(input.meleeCurve<0)return true;const auto&curve=MeleePoseCurves::Clips[unsigned(input.meleeCurve)];unsigned side=curve.side;if(!input.meleeAction[side]||!input.solved[side])return true;
 double fat=std::clamp((p.fat-.25)/.65,0.,1.),amount=(curve.base+fat*curve.fat)*input.meleeWeight*input.meleeConfidence;if(amount<1e-10)return true;
 const auto&h=UnityHandsData::Sides[side];M torso=Product(Proper(w[3]),Inverse(Proper(d.rest[3])));V offset=Scale(Rotate(torso,curve.direction),amount);auto before=w;UnityBodyAppearance::ApplyScales(p,before);V prior=Contact(before[h.hand],grasp[side],p.scale),target=Add(w[h.hand].t,offset),oldWrist=w[h.hand].t;Q orientation=Quaternion(w[h.hand]);
 if(!MeleePoseReach(d,w,h,target,orientation))return false;V shift=Sub(w[h.hand].t,oldWrist);input.meleeClearanceShift[side]=std::sqrt(Dot(shift,shift));ArmControl(d,w);auto reached=w;UnityBodyAppearance::ApplyScales(p,reached);
 const auto&binding=input.bindings[side];M&mount=input.mounts[side];V axis=Unit(Rotate(mount,binding.axis));M unit=mount;for(auto&row:unit.r)for(double&v:row)v/=input.objectScale[side];auto section=HandleSection(input,side,input.shapes[side],unit,binding.point,{},axis);Q desired=Orient(w[h.hand],Affine(w[h.hand],reached[h.hand]),grasp[side],axis,section.palmDirection);Replace(d,w,h.hand,desired);ArmControl(d,w);auto after=w;UnityBodyAppearance::ApplyScales(p,after);mount.t=Add(mount.t,Sub(Contact(after[h.hand],grasp[side],p.scale),prior));return true;
}

inline bool ComputeReviewed(const Data&d,const UnityBodyAppearance::Profile&p,const std::array<M,15>&game,const Input&input,std::vector<V>&v,std::vector<V>&n,std::vector<M>&world){
 input.meleeClearanceShift={0,0};input.solved={false,false};input.objectScale={1,1};if(!input.generic)return UnityHandsV7::Compute(d,p,game,input,v,n,world);
 bool held=input.grip[0]>0||input.grip[1]>0;if(!held)return UnityHandsV7::Compute(d,p,game,input,v,n,world);
 if(!std::isfinite(input.sitting)||!std::isfinite(input.grip[0])||!std::isfinite(input.grip[1]))return false;
 world=Retarget(d,game);LimitRetarget(d,world);Control(d,world);if(!input.carryPose&&!BowClearance(d,p,input,world))return false;ArmControl(d,world);std::vector<M>initial=world;UnityBodyAppearance::ApplyScales(p,initial);
 std::array<Grasp,2>grasp{};std::array<V,2>axis{},normal{},centres{};std::array<bool,2>direct{};
 for(unsigned side=0;side<2;++side){const auto&b=input.bindings[side];if(!b.spec||input.grip[side]<=0)continue;if(b.spec->role==Role::Shield&&!input.shapes[side].known)continue;
  M mount;
  const auto&s=UnityHandsData::Sides[side];unsigned bone=b.spec->role==Role::Shield?s.forearm:s.hand;
  M base=Proper(initial[bone]);base.t=Scale(initial[bone].t,p.scale);M rest=Product(Inverse(Proper(d.rest[bone])),Proper(Product(Inverse(d.gameBind[0]),d.gameBind[unsigned(b.driver)])));mount=Product(base,Product(rest,b.relative));
  axis[side]=Unit(Rotate(mount,b.axis));normal[side]=Unit(Rotate(initial[s.hand],UnityContactData::Sides[side].normal));auto section=HandleSection(input,side,input.shapes[side],mount,b.point,normal[side],axis[side]);normal[side]=section.palmDirection;centres[side]=section.point;
  auto sizeSection=input.hasModelSizeShape[side]?HandleSection(input,side,input.modelSizeShape[side],mount,input.modelSizePoint[side],Unit(Rotate(initial[s.hand],UnityContactData::Sides[side].normal)),axis[side]):section;double objectScale=std::clamp(p.scale*.026/(std::max(sizeSection.radius,.001)*std::max(sizeSection.aspect,1.)),.55,1.);input.objectScale[side]=objectScale;for(auto&row:mount.r)for(double&value:row)value*=objectScale;
  grasp[side]=Lookup(side,p,section.radius*objectScale/p.scale,section.round,section.aspect);M a=Affine(world[s.hand],initial[s.hand]);Q desired=Orient(world[s.hand],a,grasp[side],axis[side],normal[side]);
  if(input.staffShoulderPose&&side==1){
   // Use the animated character wrist, blending from the accepted low carry.
   // Keep the source-to-hand axial sign fixed; a per-frame nearest-end test
   // creates a half-turn when the forearm crosses the shoulder.
   desired=RelaxedCarry(d,world,side);
   M hand=Product(a,Rotation(desired));V wantedAxis=Unit(Rotate(hand,grasp[side].axis)),wantedNormal=Unit(Rotate(hand,grasp[side].normal));
   M restMount=Product(Proper(Product(Inverse(d.gameBind[0]),d.gameBind[unsigned(b.driver)])),b.relative);
   if(!FixedHandle(input,side)&&Dot(Rotate(d.rest[s.hand],grasp[side].axis),Rotate(restMount,b.axis))<0)wantedAxis=Scale(wantedAxis,-1);
   M turn=Product(UnityHandsV6::Basis(wantedAxis,wantedNormal),Inverse(UnityHandsV6::Basis(axis[side],normal[side])));mount=Product(turn,mount);axis[side]=wantedAxis;normal[side]=wantedNormal;
  }

  if(CarryWrist(input,side)){Q limited=LimitCarryWrist(d,world,side,desired,(b.spec->role==Role::Bow?5.:12.)*3.14159265358979323846/180.);M hand=Product(a,Rotation(limited));V wantedAxis=Unit(Rotate(hand,grasp[side].axis)),wantedNormal=Unit(Rotate(hand,grasp[side].normal));if(Dot(wantedAxis,axis[side])<0)wantedAxis=Scale(wantedAxis,-1);M turn=Product(UnityHandsV6::Basis(wantedAxis,wantedNormal),Inverse(UnityHandsV6::Basis(axis[side],normal[side])));mount=Product(turn,mount);axis[side]=wantedAxis;normal[side]=wantedNormal;desired=limited;}
  Replace(d,world,s.hand,Nlerp(Quaternion(world[s.hand]),desired,input.grip[side]));
  input.mounts[side]=mount;input.solved[side]=true;direct[side]=true;
 }
 // Wrist rotations are reflected through the original forearm twist controls.
 ArmControl(d,world);std::vector<M>preview=world;UnityBodyAppearance::ApplyScales(p,preview);
 for(unsigned side=0;side<2;++side)if(direct[side]){M&mount=input.mounts[side];V point=Point(mount,centres[side]),contact=Contact(preview[UnityHandsData::Sides[side].hand],grasp[side],p.scale),shift=Sub(contact,point);mount.t=Add(mount.t,shift);}
 if(!MeleeHeadClearance(d,p,input,world,grasp))return false;
 if(!BowEnvelopeClearance(d,p,input,world))return false;ArmControl(d,world);preview=world;UnityBodyAppearance::ApplyScales(p,preview);
 if(input.primary>=0&&input.support>=0&&input.solved[unsigned(input.primary)]){unsigned primary=unsigned(input.primary),side=unsigned(input.support);const auto&s=UnityHandsData::Sides[side];const auto&b=input.bindings[primary];M mount=input.mounts[primary];V target;
  axis[side]=Unit(Rotate(mount,b.axis));normal[side]=Unit(Rotate(preview[s.hand],UnityContactData::Sides[side].normal));M unitMount=mount;for(auto&row:unitMount.r)for(double&value:row)value/=input.objectScale[primary];auto section=UnityHandleGeometry::Facing(input.shapes[primary],unitMount,b.point,normal[side],axis[side]);double radius=section.radius*input.objectScale[primary]/p.scale;grasp[side]=Lookup(side,p,radius,section.round,section.aspect);normal[side]=section.palmDirection;M a=Affine(world[s.hand],preview[s.hand]);Q desired=Orient(world[s.hand],a,grasp[side],axis[side],normal[side]);
  if(input.supportPoint){M oldMount=Product(game[7+primary],b.relative),relative=Product(Inverse(oldMount),game[7+primary]);target=Point(Product(mount,relative),input.oldSupportPoint);}
  else{V point=Point(mount,section.point),own=Contact(preview[s.hand],grasp[side],p.scale);target=Add(point,Scale(axis[side],Dot(Sub(own,point),axis[side])));}
  M desiredHand=Product(a,Rotation(desired));V offset=Scale(Rotate(desiredHand,grasp[side].pose.centre),p.scale);V wrist=Scale(Sub(target,offset),1/p.scale);if(!UnityHandsV6::Reach(d,world,s,wrist,desired,input.supportWeight))return false;
 }
 ArmControl(d,world);
 // Preserve the reviewed straight wrist and carry clearance; only the separate
 // separate bow adjustment below changes its angle without moving the hand.
 for(unsigned side=0;side<2;++side)if(CarryWrist(input,side)&&input.bindings[side].spec->role==Role::Bow&&direct[side]){const auto&s=UnityHandsData::Sides[side];auto old=world;UnityBodyAppearance::ApplyScales(p,old);Q limited=LimitCarryWrist(d,world,side,Quaternion(world[s.hand]),0);Replace(d,world,s.hand,limited);auto now=world;UnityBodyAppearance::ApplyScales(p,now);M turn=Product(UnityHandsV6::Basis(Rotate(now[s.hand],grasp[side].axis),Rotate(now[s.hand],grasp[side].normal)),Inverse(UnityHandsV6::Basis(Rotate(old[s.hand],grasp[side].axis),Rotate(old[s.hand],grasp[side].normal))));input.mounts[side]=Product(turn,input.mounts[side]);V shift=Sub(Contact(now[s.hand],grasp[side],p.scale),Point(input.mounts[side],centres[side]));input.mounts[side].t=Add(input.mounts[side].t,shift);}
 for(unsigned side=0;side<2;++side){if(input.grip[side]>0)ApplyFingers(d,world,side,grasp[side],input.grip[side],input.gestures[side]);else if(input.sitting>0)UnityHandsV6::Fingers(d,world,side,0,input.sitting);}
 ThumbContact(d,p,input,world);DualCarryTurn(d,p,input,world,grasp[0],centres[0]);BowCarryTurn(d,p,input,world,centres);PoleElbow(d,p,input,world,grasp[1]);PoleBladeClearance(d,p,input,world,grasp[1],centres[1]);if(!StaffShoulderContact(d,p,game,input,world,grasp[1],centres[1]))return false;PinHandle(p,input,world);UnityBodyAppearance::ApplyScales(p,world);PoleBladeRoll(input,world,centres[1]);return (input.bonesOnly||UnityHandsV6::SkinWorld(d,world,v,n));
}

// A character-authored relaxed carry. No weapon orientation is used here.
inline Q RelaxedCarry(const Data&d,const std::vector<M>&w,unsigned side){
 const auto&s=UnityHandsData::Sides[side];M torso=Product(Proper(w[3]),Inverse(Proper(d.rest[3])));
 V fingers=Unit(Sub(w[s.hand].t,w[s.forearm].t));
 V palm=Rotate(torso,{side==0?-1.:1.,0,0});palm=Unit(Sub(palm,Scale(fingers,Dot(palm,fingers))));
 Q straight=Quaternion(Product(UnityHandsV6::Basis(fingers,palm),Inverse(UnityHandsV6::Basis(s.forward,s.normal))));
 // Fifteen degrees of relaxed ulnar deviation; shoulder/elbow/wrist positions
 // stay exactly at the animation's targets. Final swing is bounded to 20deg.
 Q tilt=UnityHandsV6::AxisAngle(palm,(side==0?-15.:15.)*3.14159265358979323846/180.);
 return LimitCarryWrist(d,w,side,Mul(tilt,straight),20*3.14159265358979323846/180.);
}
inline bool HandFirstCarry(const Input&input){
 if(!input.generic||!input.carryPose||input.support>=0)return false;
 bool held=false;
 for(unsigned side=0;side<2;++side){const auto&b=input.bindings[side];if(!b.spec||input.grip[side]<=0)continue;held=true;
  if(b.spec->role==Role::Bow||b.spec->role==Role::Arrow)return false;
  std::string name=b.spec->name;
  if(name.find(".pike")!=std::string::npos||name.find(".dpike")!=std::string::npos)return false;
 }
 return held;
}
inline bool ComputeBeforeFree(const Data&d,const UnityBodyAppearance::Profile&p,const std::array<M,15>&game,const Input&input,std::vector<V>&v,std::vector<V>&n,std::vector<M>&world){
 if(!HandFirstCarry(input))return ComputeReviewed(d,p,game,input,v,n,world);
 input.solved={false,false};input.objectScale={1,1};
 if(!std::isfinite(input.sitting)||!std::isfinite(input.grip[0])||!std::isfinite(input.grip[1]))return false;
 world=Retarget(d,game);LimitRetarget(d,world);Control(d,world);ArmControl(d,world);
 auto scaled=world;UnityBodyAppearance::ApplyScales(p,scaled);
 std::array<Grasp,2>grasp{};std::array<V,2>centres{};
 for(unsigned side=0;side<2;++side){const auto&b=input.bindings[side];if(!b.spec||input.grip[side]<=0)continue;
  if(b.spec->role==Role::Shield&&!input.shapes[side].known)continue;
  const auto&s=UnityHandsData::Sides[side];unsigned bone=b.spec->role==Role::Shield?s.forearm:s.hand;
  M base=Proper(scaled[bone]);base.t=Scale(scaled[bone].t,p.scale);
  M rest=Product(Inverse(Proper(d.rest[bone])),Proper(Product(Inverse(d.gameBind[0]),d.gameBind[unsigned(b.driver)])));
  M mount=Product(base,Product(rest,b.relative));
  V axis=Unit(Rotate(mount,b.axis)),normal=Unit(Rotate(scaled[s.hand],UnityContactData::Sides[side].normal));
  auto section=HandleSection(input,side,input.shapes[side],mount,b.point,normal,axis);centres[side]=section.point;
  auto sizeSection=input.hasModelSizeShape[side]?HandleSection(input,side,input.modelSizeShape[side],mount,input.modelSizePoint[side],normal,axis):section;double size=std::clamp(p.scale*.026/(std::max(sizeSection.radius,.001)*std::max(sizeSection.aspect,1.)),.55,1.);
  input.objectScale[side]=size;for(auto&row:mount.r)for(double&x:row)x*=size;
  grasp[side]=Lookup(side,p,section.radius*size/p.scale,section.round,section.aspect);
  Q pose=(b.spec->role==Role::Rod||b.spec->role==Role::Crossbow)?RelaxedCarry(d,world,side):LimitCarryWrist(d,world,side,Quaternion(world[s.hand]),20*3.14159265358979323846/180.);
  Replace(d,world,s.hand,Nlerp(Quaternion(world[s.hand]),pose,input.grip[side]));
  auto handWorld=world;UnityBodyAppearance::ApplyScales(p,handWorld);M actual=handWorld[s.hand];
  V wantedAxis=Unit(Rotate(actual,grasp[side].axis)),wantedNormal=Unit(Rotate(actual,grasp[side].normal));
  if(b.spec->role==Role::Rod||b.spec->role==Role::Crossbow){M torso=Product(Proper(world[3]),Inverse(Proper(d.rest[3])));V front=Rotate(torso,{0,-1,0});std::string itemName=b.spec->name;double headSign=(itemName=="rh3.dstaff00"||itemName=="rh3.dstaff01"||itemName=="lh3.dstaff00"||itemName=="lh3.dstaff01")?-1.:(std::abs(b.spec->low)>std::abs(b.spec->high)?-1.:1.);if(!(input.crouchCarryPose&&side==1)&&Dot(Scale(wantedAxis,headSign),front)<0)wantedAxis=Scale(wantedAxis,-1);}
  else if(Dot(wantedAxis,axis)<0)wantedAxis=Scale(wantedAxis,-1);
  M turn=Product(UnityHandsV6::Basis(wantedAxis,wantedNormal),Inverse(UnityHandsV6::Basis(axis,section.palmDirection)));
  mount=Product(turn,mount);input.mounts[side]=mount;input.solved[side]=true;
 }
 ArmControl(d,world);scaled=world;UnityBodyAppearance::ApplyScales(p,scaled);
 for(unsigned side=0;side<2;++side)if(input.solved[side]){M&mount=input.mounts[side];mount.t=Add(mount.t,Sub(Contact(scaled[UnityHandsData::Sides[side].hand],grasp[side],p.scale),Point(mount,centres[side])));}
 for(unsigned side=0;side<2;++side){if(input.grip[side]>0)ApplyFingers(d,world,side,grasp[side],input.grip[side],input.gestures[side]);else if(input.sitting>0)UnityHandsV6::Fingers(d,world,side,0,input.sitting);}
 ThumbContact(d,p,input,world);DualCarryTurn(d,p,input,world,grasp[0],centres[0]);PinHandle(p,input,world);UnityBodyAppearance::ApplyScales(p,world);
 return (input.bonesOnly||UnityHandsV6::SkinWorld(d,world,v,n));
}

// Rigidly turn the scaled affine subtree. This preserves authored bone lengths
// and nonuniform shape scales; no source geometry or rest bind is changed.
inline void FreeTurn(const Data&d,std::vector<M>&w,unsigned bone,Q desired){
 Q current=Quaternion(w[bone]);M turn=Rotation(Mul(desired,Inv(current)));V pivot=w[bone].t;
 turn.t=Sub(pivot,Rotate(turn,pivot));
 for(unsigned i=0;i<w.size();++i){int ancestor=int(i);while(ancestor>=0&&unsigned(ancestor)!=bone)ancestor=d.parent[unsigned(ancestor)];if(ancestor>=0)w[i]=Product(turn,w[i]);}
}
inline double FreeWeight(const Input&input,const std::vector<M>&w,unsigned side){
 if(input.sitting>.001||input.poleDownPose||input.freeSupportPose)return 0;
 if(input.bindings[side].spec||input.grip[side]>1e-6)return 0;
 const auto&h=UnityHandsData::Sides[side];V ray=Unit(Sub(w[h.hand].t,w[h.forearm].t));
 double t=std::clamp((-ray.z-.60)/.25,0.,1.);t=t*t*(3-2*t);if(side==1)t=std::max(t,input.relaxedRightCrouch);if(side==1&&input.transitionRightWeight>=0)t=input.transitionRightWeight;
 return t*(1-std::clamp(input.grip[side],0.,1.));
}
inline bool ComputeBase(const Data&d,const UnityBodyAppearance::Profile&p,const std::array<M,15>&game,const Input&input,std::vector<V>&v,std::vector<V>&n,std::vector<M>&world){
 if(!ComputeBeforeFree(d,p,game,input,v,n,world))return false;
 bool changed=false;constexpr double radians=3.14159265358979323846/180.;
 for(unsigned side=0;side<2;++side){double weight=FreeWeight(input,world,side);if(weight<=0)continue;
  const auto&h=UnityHandsData::Sides[side];V forward=Unit(Sub(world[h.hand].t,world[h.forearm].t));
  V palm=Rotate(Proper(world[h.hand]),h.normal);palm=Sub(palm,Scale(forward,Dot(palm,forward)));
  if(Dot(palm,palm)<1e-8)continue;palm=Unit(palm);
  Q straight=Quaternion(Product(UnityHandsV6::Basis(forward,palm),Inverse(UnityHandsV6::Basis(h.forward,h.normal))));
  Q flex=UnityHandsV6::AxisAngle(Cross(forward,palm),10*radians);
  Q deviation=UnityHandsV6::AxisAngle(palm,(side==0?-5.:5.)*radians);
  Q target=Mul(deviation,Mul(flex,straight));
  if(side==1&&input.transitionRightWeight>=0)target=Mul(Quaternion(Proper(world[h.forearm])),input.transitionRightWrist);
  FreeTurn(d,world,h.hand,(side==1&&input.transitionRightWeight>=0)?target:Nlerp(Quaternion(world[h.hand]),target,weight));
  const double loose[5][3]={{6,10,5},{8,12,6},{11,17,8},{15,21,10},{18,25,12}};
  for(unsigned finger=0;finger<5;++finger)for(unsigned joint=0;joint<3;++joint){unsigned bone=h.fingers[finger][joint];
   Q turn=UnityHandsV6::AxisAngle(h.axes[finger][joint],loose[finger][joint]*weight*radians);
   FreeTurn(d,world,bone,Mul(Quaternion(world[bone]),turn));
  }
  changed=true;
 }
 return !changed||(input.bonesOnly||UnityHandsV6::SkinWorld(d,world,v,n));
}
inline M RigidTransition(const M&m){M out=Proper(m);out.t=m.t;return out;}

inline Input TransitionRecipe(const Input&source,int mode){
 Input out=source;out.transitionFrom=out.transitionTo=-1;out.transitionWeight=0;
 out.carryPose=mode==0||mode==1;out.crouchCarryPose=false;out.staffShoulderPose=false;out.poleDownPose=false;out.freeSupportPose=mode==4;
 const auto*right=out.bindings[1].spec;
 if(out.carryPose&&right){std::string name=right->name;
  if(right->role==Role::Crossbow||(mode==1&&right->role==Role::Rod&&ShoulderFamilyData::Known(name)&&name.find(".pike")==std::string::npos&&name.find(".dpike")==std::string::npos))out.crouchCarryPose=true;
 }
 if(out.crouchCarryPose&&!HandFirstCarry(out))out.crouchCarryPose=false;
 return out;
}
inline bool SameMatrix(const M&a,const M&b){if(a.t.x!=b.t.x||a.t.y!=b.t.y||a.t.z!=b.t.z)return false;for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)if(a.r[i][j]!=b.r[i][j])return false;return true;}
inline V MatrixScale(const M&m){return {std::sqrt(m.r[0][0]*m.r[0][0]+m.r[1][0]*m.r[1][0]+m.r[2][0]*m.r[2][0]),std::sqrt(m.r[0][1]*m.r[0][1]+m.r[1][1]*m.r[1][1]+m.r[2][1]*m.r[2][1]),std::sqrt(m.r[0][2]*m.r[0][2]+m.r[1][2]*m.r[1][2]+m.r[2][2]*m.r[2][2])};}
inline V InterpolatePoint(V a,V b,double t){return Add(Scale(a,1-t),Scale(b,t));}
inline void TransitionResult(const Input&from,const Input&to,double t,const Input&output){for(unsigned side=0;side<2;++side){output.solved[side]=from.solved[side]||to.solved[side];output.objectScale[side]=(1-t)*from.objectScale[side]+t*to.objectScale[side];output.mounts[side]=t>=1?to.mounts[side]:from.mounts[side];}}
inline bool Compute(const Data&d,const UnityBodyAppearance::Profile&p,const std::array<M,15>&game,const Input&input,std::vector<V>&v,std::vector<V>&n,std::vector<M>&world);
}
