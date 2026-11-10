#pragma once
#include "GamePoseReader.h"
#include "CrouchRelaxReference.h"
#include "TransitionFreeHand.h"
#include "NalaProneVisibility.h"
#include "ArmedTransitionFrames.h"
#include "UnityHandControl.h"
#include "ArmedEndpointData.h"
#include "ShoulderFamilyData.h"
#include "NativeWeaponGeometry.h"
#include "UnitySittingReference.h"
#include "UnityBowEnvelopeData.h"
namespace UnityHands {
inline double SittingWeight(const std::array<M,15>&game){double maximum=0;for(unsigned side=0;side<2;++side){M relative=Product(Inverse(game[0]),game[9+side]);Q q=Quaternion(Product(Inverse(UnityHandsData::SittingReference[side]),relative));maximum=std::max(maximum,2*std::acos(std::clamp(std::abs(q[0]),0.,1.)));}double t=std::clamp((.55-maximum)/.35,0.,1.);return t*t*(3-2*t);}
inline int ExpectedDriver(const UnityWeaponCatalog::Spec&s){return s.role==Role::Shield?5:int(7+s.side);}
inline double CrouchRelaxation(const std::array<M,15>&game){double maximum=0;for(unsigned side=0;side<2;++side){M relative=Product(Inverse(game[0]),game[9+side]);Q q=Quaternion(Product(Inverse(CrouchRelaxReference::Legs[side]),relative));maximum=std::max(maximum,2*std::acos(std::clamp(std::abs(q[0]),0.,1.)));}double t=std::clamp((.9-maximum)/.6,0.,1.);return t*t*(3-2*t);}
inline double Fade(double value,double inner,double outer){double t=std::clamp((outer-value)/(outer-inner),0.,1.);return t*t*(3-2*t);}
inline void Resolve(const UnityGame::Actor&actor,Input&input,std::string&weapon){input={};input.transitionActor=&actor;input.generic=true;input.carryPose=actor.animation.rfind("cidle",0)==0&&actor.animation!="cidle03"&&actor.animation!="cidle06";input.poleDownPose=actor.animation=="uspecial10";input.freeSupportPose=actor.animation.find("crawl")!=std::string::npos;input.relaxedRightCrouch=(actor.animation=="cidle04"||actor.animation=="cidle05"||actor.animation=="cwalk03"||actor.animation=="ucross07"||actor.animation=="ucross08"||actor.animation=="ucross09"||actor.animation=="ucross10")?CrouchRelaxation(actor.poses):0;TransitionFreeHand::Resolve(actor.animation,actor.poses,input.transitionRightWrist,input.transitionRightWeight);weapon.clear();if(actor.animation=="uspecial19")input.sitting=SittingWeight(actor.poses);
 UnityGame::WeaponGeometry geometry;geometry.Load(actor.geometry);
 for(const auto&a:actor.attachments){if(NalaProneVisibility::Hidden(actor,a))continue;const auto*s=UnityWeaponCatalog::Find(a.name);if(!s||a.driver!=ExpectedDriver(*s))continue;
  auto&binding=input.bindings[s->side];if(binding.spec){if(binding.spec->role==Role::Arrow&&s->role!=Role::Arrow){}else continue;}
  binding.spec=s;binding.relative=Product(Inverse(actor.poses[unsigned(a.driver)]),a.pose);binding.driver=a.driver;binding.axis=s->axis;binding.point=s->point;binding.radius=s->radius;
  // New female pike-down stance: choose a grip on the actual current shaft.
  // Ordinary carry and all other animations keep their existing point.
  bool pikeDown=actor.animation=="uspecial10"&&std::string(s->name).rfind("rh3.dpike",0)==0&&Rotate(a.pose,s->axis).z>.4;
  if(pikeDown){
   double low=1e9;for(const auto&r:geometry.ranges)if(r.first==a.name)for(unsigned i=0;i<r.second.second*4;++i){V point;if(geometry.Point(a.name,i,point))low=std::min(low,point.y);}
   if(low<0&&low>-4)binding.point.y=std::min(-.10,low+.90);
  }
  input.shapes[s->side].radius=binding.radius;
  if(UnityHandleGeometry::Describe(geometry,s->name,binding.point,binding.radius,input.shapes[s->side],pikeDown))input.shapes[s->side].radius=binding.radius;
 {std::string name=s->name;
  if(name=="rh3.staff03"||name=="rh3.dstaff03"||name=="rh3.daxe00"){
   double high=0;for(const auto&range:geometry.ranges)if(range.first==name)for(unsigned i=0;i<range.second.second*4;++i){V point;if(geometry.Point(name,i,point))high=std::max(high,point.y);}
   V point=binding.point;point.y=high*(name=="rh3.daxe00"?.35:.65);double radius=binding.radius;UnityHandleGeometry::Shape shape;
   if(UnityHandleGeometry::Describe(geometry,name,point,radius,shape,true)){
    input.modelSizePoint[s->side]=binding.point;input.modelSizeShape[s->side]=input.shapes[s->side];input.hasModelSizeShape[s->side]=true;
    binding.point=point;binding.radius=radius;input.shapes[s->side]=shape;
   }
  }
 }

  if(s->role==Role::Shield&&!input.shapes[s->side].known)continue;
  input.grip[s->side]=1;input.gestures[s->side]=s->role==Role::Crossbow?Gesture::Trigger:s->role==Role::Arrow?Gesture::Hook:s->role==Role::Shield?Gesture::Shield:Gesture::Cylinder;
 }
 if(actor.animation=="cidle19"&&input.bindings[1].spec&&((input.bindings[1].spec->role==Role::Rod&&ShoulderFamilyData::Known(input.bindings[1].spec->name))||input.bindings[1].spec->role==Role::Crossbow)){input.carryPose=false;input.staffShoulderPose=true;}
 if((actor.animation=="cidle04"||actor.animation=="cidle05")&&input.bindings[1].spec&&input.bindings[1].spec->role==Role::Rod&&ShoulderFamilyData::Known(input.bindings[1].spec->name)){std::string name=input.bindings[1].spec->name;if(name.find(".pike")==std::string::npos&&name.find(".dpike")==std::string::npos)input.crouchCarryPose=true;}
 // Include the full currently owned bow assembly, not just its grip section.
 for(unsigned side=0;side<2;++side){const auto&b=input.bindings[side];if(!b.spec||b.spec->role!=Role::Bow)continue;M holder=Product(actor.poses[unsigned(b.driver)],b.relative);for(const auto&a:actor.attachments){if(NalaProneVisibility::Hidden(actor,a))continue;const auto*member=UnityWeaponCatalog::Assembly(a.name);const auto*edges=UnityBowEnvelopeData::Find(a.name);if(!member||!edges||member->role!=Role::Bow||member->side!=side||a.driver!=b.driver)continue;M relative=Product(Inverse(holder),a.pose);for(unsigned index=0;index<edges->count;++index){const auto&e=UnityBowEnvelopeData::Edges[edges->first+index];V first,last;if(!geometry.Point(a.name,e[0],first)||!geometry.Point(a.name,e[1],last))continue;V delta=Sub(last,first);unsigned steps=unsigned(std::clamp(std::ceil(std::sqrt(Dot(delta,delta))/.04),1.,64.));for(unsigned step=0;step<=steps;++step)input.bowEnvelope[side].push_back(Point(relative,Add(first,Scale(delta,double(step)/steps))));}}}
 for(unsigned side=0;side<2;++side){const auto&b=input.bindings[side];if(!b.spec||b.spec->role!=Role::Bow)continue;M holder=Product(actor.poses[unsigned(b.driver)],b.relative);unsigned parts=0;for(const auto&a:actor.attachments)if(!NalaProneVisibility::Hidden(actor,a)&&a.driver==b.driver&&a.name.rfind("bwtetiv",0)==0){V centre{};bool valid=true;for(unsigned i=0;i<6;++i){V v;if(!geometry.Point(a.name,i,v)){valid=false;break;}centre=Add(centre,v);}if(valid){input.bowString[side]=Add(input.bowString[side],Point(Product(Inverse(holder),a.pose),Scale(centre,1./6)));++parts;}}if(parts){input.bowString[side]=Scale(input.bowString[side],1./parts);input.hasBowString[side]=true;}}
 // Select a support hand from the native pose; actual grip geometry and all
 // finger poses are rebuilt below, including the previously rejected axe.
 const bool idleGesture=actor.animation=="cidle13"||actor.animation=="cidle14"||actor.animation=="cidle21";
 for(unsigned side=0;side<2;++side){const auto&b=input.bindings[side];if(idleGesture||!b.spec||!b.spec->secondary||std::string(b.spec->name)=="rh3.dstaff05")continue;unsigned other=1-side;if(input.bindings[other].spec)continue;
  M oldMount=Product(actor.poses[7+side],b.relative);V point=Point(oldMount,b.point),axis=Unit(Rotate(oldMount,b.axis)),own=actor.poses[7+other].t;V delta=Sub(own,point);double along=Dot(delta,axis);V offset=Sub(delta,Scale(axis,along));
  double weight=Fade(std::sqrt(Dot(offset,offset)),.10,.16)*Fade(std::max(b.spec->low-along,along-b.spec->high),.04,.10);
  bool firstAxe=std::string(b.spec->name)==UnityHandsData::VerifiedWeapon&&actor.animation.rfind("2ha",0)==0;
  bool releaseAxe=actor.animation=="2haucross07"||actor.animation=="2haucross09"||actor.animation=="2haucross10";
  if(firstAxe&&!releaseAxe)weight=1;
  if(weight>0){input.primary=int(side);input.support=int(other);input.supportWeight=weight;input.grip[other]=weight;input.gestures[other]=Gesture::Cylinder;input.bindings[other].radius=b.radius;input.weapon=true;input.twoHand=true;break;}
 }
 const auto&bow=input.bindings[0];if(!idleGesture&&bow.spec&&bow.spec->role==Role::Bow){
  V oldHand=actor.poses[8].t;double closest=1e9;std::vector<std::pair<V,double>>contacts;
  for(const auto&a:actor.attachments)if(!NalaProneVisibility::Hidden(actor,a)&&a.driver==7&&a.name.rfind("bwtetiv",0)==0){
   std::array<V,6>vertices;bool valid=true;for(unsigned i=0;i<6;++i)if(!geometry.Point(a.name,i,vertices[i]))valid=false;if(!valid)continue;
   double low=vertices[0].y,high=low;for(V p:vertices){low=std::min(low,p.y);high=std::max(high,p.y);}V first{},last{};unsigned nf=0,nl=0;
   for(V p:vertices){if(p.y<(low+high)*.5){first=Add(first,p);++nf;}else{last=Add(last,p);++nl;}}if(!nf||!nl)continue;
   first=Point(a.pose,Scale(first,1./nf));last=Point(a.pose,Scale(last,1./nl));V segment=Sub(last,first);double length2=Dot(segment,segment);if(length2<1e-12)continue;
   V contact=Add(first,Scale(segment,std::clamp(Dot(Sub(oldHand,first),segment)/length2,0.,1.)));double distance=Dot(Sub(contact,oldHand),Sub(contact,oldHand));closest=std::min(closest,distance);contacts.push_back({contact,distance});
  }
  double weight=Fade(std::sqrt(closest),.10,.16);
  if(!contacts.empty()&&weight>0){V chosen{};double total=0;for(const auto&contact:contacts){double influence=std::exp(-(contact.second-closest)/.0001);chosen=Add(chosen,Scale(contact.first,influence));total+=influence;}input.primary=0;input.support=1;input.supportWeight=weight;input.supportPoint=true;input.oldSupportPoint=Point(Inverse(actor.poses[7]),Scale(chosen,1./total));input.grip[1]=weight;input.gestures[1]=Gesture::Hook;input.weapon=true;input.twoHand=true;}
 }
 if(input.bindings[0].spec&&input.bindings[0].spec->role==Role::Bow)weapon=input.bindings[0].spec->name;else if(input.bindings[1].spec)weapon=input.bindings[1].spec->name;else if(input.bindings[0].spec)weapon=input.bindings[0].spec->name;
 if(input.carryPose&&input.bindings[1].spec&&input.bindings[1].spec->role==Role::Crossbow)input.crouchCarryPose=true;
 if(input.crouchCarryPose&&!HandFirstCarry(input))input.crouchCarryPose=false;
 const bool meleeClip=actor.animation.rfind("uattack",0)==0||actor.animation.rfind("2hauattack",0)==0||actor.animation.rfind("2hsuattack",0)==0||actor.animation.rfind("uat01",0)==0||actor.animation.rfind("dat01",0)==0;
 if(meleeClip)for(unsigned side=0;side<2;++side){const auto*spec=input.bindings[side].spec;if(!spec||spec->role!=Role::Rod)continue;std::string name=spec->name;if(side==0){input.meleeAction[side]=MeleeGripFrames::LeftKnown(spec->name);continue;}bool family=name.rfind("rh3.sword",0)==0||name.rfind("rh3.dagger",0)==0||name.rfind("rh3.axe",0)==0||name.rfind("rh3.dsword",0)==0||name.rfind("rh3.daxe",0)==0;input.meleeAction[side]=family&&ShoulderFamilyData::Known(name);}
 MeleePoseCurves::Resolve(actor.animation,actor.poses,input.bindings,input.meleeCurve,input.meleePhase,input.meleeConfidence,input.meleeWeight);

 if((actor.animation=="ucross07"||actor.animation=="ucross08"||actor.animation=="ucross09"||actor.animation=="ucross10")&&input.bindings[0].spec&&!input.bindings[1].spec){std::string bowName=input.bindings[0].spec->name;if(bowName=="lh3.bwpartb01"||bowName=="lh3.bwpartb02"||bowName=="lh3.bwpartb03"||bowName=="lh3.bwpartb04"||bowName=="lh3.bwpartb05"||bowName=="lh3.bwpartb07"){input.primary=input.support=-1;input.supportWeight=0;input.grip[1]=0;input.weapon=input.twoHand=false;}}
 if(actor.animationFrame>=0&&(input.grip[0]>0||input.grip[1]>0)){ArmedTransitionFrames::Resolve(actor.animation,actor.animationFrame,input.transitionFrom,input.transitionTo,input.transitionWeight);if(input.transitionFrom>=0)input.transitionActor=&actor;}

}

inline const char* TransitionNeighbour(const std::string&clip,unsigned edge){
 const struct{const char*clip;const char*from;const char*to;}routes[]={
 {"ucross01","cidle09","cidle01"},{"ucross02","cidle01","cidle09"},{"ucross03","cidle01","cidle03"},{"ucross04","cidle09","cidle03"},{"ucross05","cidle03","cidle01"},{"ucross06","cidle03","cidle09"},{"ucross07","cidle01","cidle04"},{"ucross08","cidle04","cidle01"},{"ucross09","cidle09","cidle04"},{"ucross10","cidle04","cidle09"},{"ucross11","cidle04","cidle03"},{"ucross12","cidle03","cidle04"},
 {"scross13","cidle01","crun04"},{"scross15","cidle01","cwalk05"},{"scross17","cidle09","crun01"},{"scross20","cidle09","cwalk02"},{"scross23","cidle04","cwalk03"},{"scross24","cidle04","crun04"},{"scross25","cidle03","ccrawl01"}};
 for(const auto&r:routes)if(clip==r.clip)return edge?r.to:r.from;return "";
}
inline void TransitionTarget(const UnityGame::Actor&ref,unsigned edge,Input&out){auto neighbour=ref;neighbour.animation=TransitionNeighbour(ref.animation,edge);neighbour.animationFrame=-1;std::string weapon;Resolve(neighbour,out,weapon);}
inline M TransitionLocal(const Data&d,const std::vector<M>&w,unsigned bone){int parent=d.parent[bone];return parent<0?RigidTransition(w[bone]):Product(Inverse(RigidTransition(w[unsigned(parent)])),RigidTransition(w[bone]));}
inline bool ComputeArmedBeforeBowLock(const Data&d,const UnityBodyAppearance::Profile&p,const std::array<M,15>&game,const Input&input,std::vector<V>&v,std::vector<V>&n,std::vector<M>&world){
 if(input.transitionFrom<0||input.transitionTo<0||!input.transitionActor)return ComputeBase(d,p,game,input,v,n,world);
 const auto&actor=*input.transitionActor;int clip=ArmedEndpointData::Clip(actor.animation);if(clip<0)return ComputeBase(d,p,game,input,v,n,world);
 double t=std::clamp(input.transitionWeight,0.,1.);
 if(t<=0||t>=1){Input exact;TransitionTarget(actor,t<=0?0:1,exact);bool ok=ComputeBase(d,p,game,exact,v,n,world);TransitionResult(exact,exact,0,input);return ok;}
 Input base=input;base.transitionFrom=base.transitionTo=-1;base.transitionActor=nullptr;base.bonesOnly=true;
 if(!ComputeBase(d,p,game,base,v,n,world))return false;
 auto original=world;std::array<std::vector<M>,2>reference,accepted;std::array<Input,2>nativeInputs,acceptedInputs;
 for(unsigned edge=0;edge<2;++edge){UnityGame::Actor ref;if(!ArmedEndpointData::Reference(actor,clip,edge,ref))return false;
  std::string weapon;Resolve(ref,nativeInputs[edge],weapon);nativeInputs[edge].bonesOnly=true;auto&target=acceptedInputs[edge];TransitionTarget(ref,edge,target);target.bonesOnly=true;
  std::vector<V>rv,rn;if(!ComputeBase(d,p,ref.poses,nativeInputs[edge],rv,rn,reference[edge])||!ComputeBase(d,p,ref.poses,target,rv,rn,accepted[edge]))return false;
 }
 std::vector<unsigned char>done(world.size(),0);
 auto apply=[&](auto&&self,unsigned bone)->void{if(done[bone])return;int parent=d.parent[bone];if(parent>=0)self(self,unsigned(parent));
  M current=TransitionLocal(d,original,bone);M delta[2];for(unsigned edge=0;edge<2;++edge){M a=TransitionLocal(d,reference[edge],bone),b=TransitionLocal(d,accepted[edge],bone);delta[edge]=Product(b,Inverse(a));delta[edge].t=Sub(b.t,a.t);}
  // Reference differences below numerical noise retain the exact original local pose.
  Q qa=Quaternion(delta[0]),qb=Quaternion(delta[1]);bool changed=std::abs(qa[0])<1-1e-13||std::abs(qb[0])<1-1e-13||Dot(delta[0].t,delta[0].t)>1e-20||Dot(delta[1].t,delta[1].t)>1e-20;
  bool inherited=parent>=0&&!SameMatrix(world[unsigned(parent)],original[unsigned(parent)]);
  if(!changed&&!inherited){world[bone]=original[bone];done[bone]=1;return;}
  M corrected=current;if(changed){corrected=Product(Rotation(Nlerp(qa,qb,t)),current);corrected.t=Add(current.t,InterpolatePoint(delta[0].t,delta[1].t,t));double length=std::sqrt(Dot(corrected.t,corrected.t)),wanted=std::sqrt(Dot(current.t,current.t));if(parent>=0&&length>1e-12)corrected.t=Scale(corrected.t,wanted/length);}
  M out=parent<0?corrected:Product(RigidTransition(world[unsigned(parent)]),corrected);V size=MatrixScale(original[bone]);for(unsigned r=0;r<3;++r){out.r[r][0]*=size.x;out.r[r][1]*=size.y;out.r[r][2]*=size.z;}world[bone]=out;done[bone]=1;
 };for(unsigned bone=0;bone<world.size();++bone)apply(apply,bone);
 if((actor.animation=="scross23"||actor.animation=="scross24")&&!input.bindings[1].spec&&input.support!=1){const auto&h=UnityHandsData::Sides[1];Q local[2];for(unsigned edge=0;edge<2;++edge)local[edge]=Mul(Inv(Quaternion(accepted[edge][h.forearm])),Quaternion(accepted[edge][h.hand]));FreeTurn(d,world,h.hand,Mul(Quaternion(world[h.forearm]),Nlerp(local[0],local[1],t)));}
 for(unsigned side=0;side<2;++side){const auto&binding=input.bindings[side];if(!binding.spec||!base.solved[side])continue;
  if(!nativeInputs[0].solved[side]||!nativeInputs[1].solved[side]||!acceptedInputs[0].solved[side]||!acceptedInputs[1].solved[side])return false;
  Q delta[2],localGrip[2];V contact[2];const auto&h=UnityHandsData::Sides[side];unsigned bone=binding.spec->role==Role::Shield?h.forearm:h.hand;
  for(unsigned edge=0;edge<2;++edge){delta[edge]=Mul(Inv(Quaternion(nativeInputs[edge].mounts[side])),Quaternion(acceptedInputs[edge].mounts[side]));M hand=RigidTransition(accepted[edge][bone]);localGrip[edge]=Mul(Inv(Quaternion(hand)),Quaternion(acceptedInputs[edge].mounts[side]));hand.t=Scale(hand.t,p.scale);contact[edge]=Point(Inverse(hand),Point(acceptedInputs[edge].mounts[side],binding.point));}
  M mount=Product(base.mounts[side],Rotation(Nlerp(delta[0],delta[1],t)));
  if(actor.animation=="ucross04"&&std::string(binding.spec->name)=="lh3.bwpartb02"){
   M motion=Product(Proper(world[3]),Inverse(Proper(d.rest[3])));V front=Unit(Rotate(motion,{0,-1,0}));double bump=std::pow(std::sin(3.14159265358979323846*t),4),fat=std::clamp((p.fat-.25)/.65,0.,1.);
   double early=std::clamp(actor.animationFrame/(.4*42),0.,1.);early=std::pow(std::sin(3.14159265358979323846*early),2);
   double degrees=(40+15*fat)*bump+30*fat*early;mount=Product(Rotation(UnityHandsV6::AxisAngle(front,-degrees*3.14159265358979323846/180)),mount);
  }
  input.objectScale[side]=(1-t)*acceptedInputs[0].objectScale[side]+t*acceptedInputs[1].objectScale[side];double ratio=input.objectScale[side]/base.objectScale[side];for(auto&row:mount.r)for(double&x:row)x*=ratio;
  // Keep the occupied hand on the continuously chosen grip face. The native
  // generic wrist solver can select another face in a single intermediate frame.
  Q target=Mul(Quaternion(mount),Inv(Nlerp(localGrip[0],localGrip[1],t)));FreeTurn(d,world,bone,target);
  M hand=RigidTransition(world[bone]);hand.t=Scale(hand.t,p.scale);V centre=Point(hand,InterpolatePoint(contact[0],contact[1],t));mount.t=Sub(centre,Rotate(mount,binding.point));input.mounts[side]=mount;input.solved[side]=true;
 }
 return UnityHandsV6::SkinWorld(d,world,v,n);
}

#include "BowCrouchReference.h"
#include "GestureWristPhase.h"
inline bool UprightBowGripClip(const std::string&name){
 if(name.rfind("cidle",0)==0)return name!="cidle03"&&name!="cidle06";
 if(name.rfind("cwalk",0)==0||name.rfind("crun",0)==0)return true;
 for(const char*clip:{"ucross01","ucross02","ucross07","ucross08","ucross09","ucross10","scross13","scross15","scross17","scross20","scross23","scross24"})if(name==clip)return true;
 return false;
}
inline bool BowGripLockTarget(const Input&input){const auto*spec=input.bindings[0].spec;if(!spec||!input.solved[0]||!input.transitionActor)return false;std::string name=spec->name;return (name=="lh3.bwpartb02"||name=="lh3.bwpartb07")&&UprightBowGripClip(input.transitionActor->animation);}
inline bool ComputeBeforeBowCrouch(const Data&d,const UnityBodyAppearance::Profile&p,const std::array<M,15>&game,const Input&input,std::vector<V>&v,std::vector<V>&n,std::vector<M>&world){
 if(!ComputeArmedBeforeBowLock(d,p,game,input,v,n,world))return false;
 if(!BowGripLockTarget(input))return true;
 // Reference the already accepted neutral face, with this actor's native BON
 // complection and this shaped body's contact geometry. No body is reposed.
 UnityGame::Actor neutral;if(!ArmedEndpointData::Reference(*input.transitionActor,ArmedEndpointData::Clip("ucross09"),0,neutral))return false;
 neutral.animation="cidle09";Input accepted;std::string weapon;Resolve(neutral,accepted,weapon);accepted.bonesOnly=true;
 std::vector<V>unusedV,unusedN;std::vector<M>neutralWorld;if(!ComputeBase(d,p,neutral.poses,accepted,unusedV,unusedN,neutralWorld)||!accepted.solved[0])return false;
 const auto&binding=input.bindings[0];const auto&h=UnityHandsData::Sides[0];V localNormal={0,0,1};if(std::abs(Dot(Unit(binding.axis),localNormal))>.9)localNormal={1,0,0};
 V faceInHand=Rotate(Inverse(Proper(neutralWorld[h.hand])),Rotate(accepted.mounts[0],localNormal));
 M&mount=input.mounts[0];V pivot=Point(mount,binding.point),axis=Unit(Rotate(mount,binding.axis));
 V before=Rotate(mount,localNormal),wanted=Rotate(world[h.hand],faceInHand);
 before=Sub(before,Scale(axis,Dot(before,axis)));wanted=Sub(wanted,Scale(axis,Dot(wanted,axis)));
 if(Dot(before,before)<1e-14||Dot(wanted,wanted)<1e-14)return false;before=Unit(before);wanted=Unit(wanted);
 double angle=std::atan2(Dot(axis,Cross(before,wanted)),std::clamp(Dot(before,wanted),-1.,1.));
 if(std::abs(angle)<1e-9)return true;
 M turn=Rotation(UnityHandsV6::AxisAngle(axis,angle));turn.t=Sub(pivot,Rotate(turn,pivot));mount=Product(turn,mount);
 // No SkinWorld/Reach/FreeTurn: body, arms, wrists and fingers remain byte exact.
 return true;
}

inline bool SimpleBowCrouch(const Input&input){
 if(!input.transitionActor||input.bindings[1].spec||input.support>=0)return false;const auto*spec=input.bindings[0].spec;if(!spec)return false;std::string name=spec->name;if(name!="lh3.bwpartb01"&&name!="lh3.bwpartb02"&&name!="lh3.bwpartb03"&&name!="lh3.bwpartb04"&&name!="lh3.bwpartb05"&&name!="lh3.bwpartb07")return false;
 const auto&clip=input.transitionActor->animation;return clip=="ucross07"||clip=="ucross08"||clip=="ucross09"||clip=="ucross10";
}
inline bool IdleBowHeadReach(const Data&d,const std::array<M,15>&game,const Input&input,std::vector<M>&world){
 const auto&binding=input.bindings[0];if(!binding.spec||binding.spec->role!=Role::Bow||!input.solved[0])return false;
 double distance=std::sqrt(Dot(Sub(game[7].t,game[2].t),Sub(game[7].t,game[2].t)));
 double weight=std::clamp((.43-distance)/.16,0.,1.);weight=weight*weight*(3-2*weight);if(weight<1e-8)return false;
 const auto&s=UnityHandsData::Sides[0];V shoulder=world[s.upper].t,elbow=world[s.forearm].t,wrist=world[s.hand].t;
 M torso=Product(Proper(world[3]),Inverse(Proper(d.rest[3])));V outward=Unit(Rotate(torso,{.35,-1,0}));V target=Add(wrist,Scale(outward,.12*weight));
 double a=std::sqrt(Dot(Sub(elbow,shoulder),Sub(elbow,shoulder))),b=std::sqrt(Dot(Sub(wrist,elbow),Sub(wrist,elbow)));V ray=Sub(target,shoulder);double length=std::sqrt(Dot(ray,ray));if(a<.05||b<.05||length<1e-8)return false;
 V direction=Scale(ray,1/length);length=std::clamp(length,std::abs(a-b)+1e-8,a+b-1e-8);target=Add(shoulder,Scale(direction,length));
 V pole=Sub(Sub(elbow,shoulder),Scale(direction,Dot(Sub(elbow,shoulder),direction)));if(Dot(pole,pole)<1e-10)return false;pole=Unit(pole);
 double x=(a*a+length*length-b*b)/(2*length),height=std::sqrt(std::max(0.,a*a-x*x));V wantedElbow=Add(shoulder,Add(Scale(direction,x),Scale(pole,height)));Q hand=Quaternion(Proper(world[s.hand]));
 FreeTurn(d,world,s.upper,Mul(UnityHandsV6::Between(Sub(elbow,shoulder),Sub(wantedElbow,shoulder)),Quaternion(Proper(world[s.upper]))));
 FreeTurn(d,world,s.forearm,Mul(UnityHandsV6::Between(Sub(world[s.hand].t,world[s.forearm].t),Sub(target,world[s.forearm].t)),Quaternion(Proper(world[s.forearm]))));
 FreeTurn(d,world,s.hand,hand);return true;
}
inline bool Compute(const Data&d,const UnityBodyAppearance::Profile&p,const std::array<M,15>&game,const Input&input,std::vector<V>&v,std::vector<V>&n,std::vector<M>&world){
 if(input.transitionActor&&(input.transitionActor->animation=="cidle13"||input.transitionActor->animation=="cidle14"||input.transitionActor->animation=="cidle21")){
  if(!ComputeBeforeBowCrouch(d,p,game,input,v,n,world))return false;
  UnityGame::Actor neutral;
  if(!ArmedEndpointData::Reference(*input.transitionActor,ArmedEndpointData::Clip("ucross09"),0,neutral))return true;
  neutral.animation="cidle09";Input accepted;std::string weapon;Resolve(neutral,accepted,weapon);accepted.bonesOnly=true;
  std::vector<V>tmpV,tmpN;std::vector<M>referenceWorld;
  if(!ComputeBeforeBowCrouch(d,p,neutral.poses,accepted,tmpV,tmpN,referenceWorld))return true;
  bool armMoved=input.transitionActor->animation=="cidle13"&&IdleBowHeadReach(d,game,input,world);
  for(unsigned side=0;side<2;++side){const auto&binding=input.bindings[side];
   if(!binding.spec||!input.solved[side]||!accepted.solved[side]||binding.spec->role==Role::Shield||binding.spec->role==Role::Arrow)continue;
   unsigned bone=UnityHandsData::Sides[side].hand;M oldHand=RigidTransition(referenceWorld[bone]);oldHand.t=Scale(oldHand.t,p.scale);
   M currentHand=RigidTransition(world[bone]);currentHand.t=Scale(currentHand.t,p.scale);
   input.mounts[side]=Product(currentHand,Product(Inverse(oldHand),accepted.mounts[side]));input.objectScale[side]=accepted.objectScale[side];
  }
  // Preserve the original hand in the healthy parts of the gesture; only
  // blend away the excessive wrist motion during the identified intervals.
  unsigned side=input.transitionActor->animation=="cidle13"?0u:1u;const auto&binding=input.bindings[side];double weight=GestureWristPhase::Weight(input.transitionActor->animation,game);
  if(weight>1e-8&&binding.spec&&binding.spec->role!=Role::Shield&&binding.spec->role!=Role::Arrow&&input.solved[side]&&accepted.solved[side]){
   const auto&arm=UnityHandsData::Sides[side];M before=RigidTransition(world[arm.hand]);before.t=Scale(before.t,p.scale);M grip=Product(Inverse(before),input.mounts[side]);
   Q forearm=Quaternion(Proper(world[arm.forearm]));Q local=Mul(Inv(forearm),Quaternion(Proper(world[arm.hand])));
   Q wanted=Mul(Inv(Quaternion(Proper(referenceWorld[arm.forearm]))),Quaternion(Proper(referenceWorld[arm.hand])));
   FreeTurn(d,world,arm.hand,Mul(forearm,Nlerp(local,wanted,weight)));
   M after=RigidTransition(world[arm.hand]);after.t=Scale(after.t,p.scale);input.mounts[side]=Product(after,grip);armMoved=true;
  }
  return !armMoved||input.bonesOnly||UnityHandsV6::SkinWorld(d,world,v,n);
 }
 if(!SimpleBowCrouch(input))return ComputeBeforeBowCrouch(d,p,game,input,v,n,world);
 const auto&actor=*input.transitionActor;bool neutral=actor.animation=="ucross09"||actor.animation=="ucross10",rising=actor.animation=="ucross08"||actor.animation=="ucross10";
 double t=BowCrouchReference::Phase(actor.animation,game);if(rising)t=1-t;t=t*t*(3-2*t);
 std::array<std::vector<M>,2>accepted;std::array<Input,2>grips;
 for(unsigned edge=0;edge<2;++edge){UnityGame::Actor ref;if(!BowCrouchReference::Reference(actor,edge==1?2:neutral?1:0,ref))return false;std::string weapon;Resolve(ref,grips[edge],weapon);grips[edge].bonesOnly=true;std::vector<V>rv,rn;
  if(!ComputeBeforeBowCrouch(d,p,ref.poses,grips[edge],rv,rn,accepted[edge]))return false;
 }
 // Retain the existing body and free hand. Author only the occupied arm's
 // calm carry curve between the actual adjacent accepted static poses.
 Input retained=input;retained.bonesOnly=true;if(!ComputeBeforeBowCrouch(d,p,game,retained,v,n,world))return false;const auto&hand=UnityHandsData::Sides[0];
 auto descendant=[&](unsigned bone,unsigned root){int a=int(bone);while(a>=0){if(unsigned(a)==root)return true;a=d.parent[unsigned(a)];}return false;};
 std::vector<unsigned char>done(world.size(),0);
 auto apply=[&](auto&&self,unsigned bone)->void{if(done[bone])return;int parent=d.parent[bone];if(parent>=0&&descendant(unsigned(parent),hand.upper))self(self,unsigned(parent));
  M a=TransitionLocal(d,accepted[0],bone),b=TransitionLocal(d,accepted[1],bone);
  M corrected=Rotation(Nlerp(Quaternion(a),Quaternion(b),t));corrected.t=InterpolatePoint(a.t,b.t,t);
  double length=std::sqrt(Dot(corrected.t,corrected.t)),wanted=std::sqrt(Dot(a.t,a.t));if(parent>=0&&length>1e-12)corrected.t=Scale(corrected.t,wanted/length);
  M out=parent<0?corrected:Product(RigidTransition(world[unsigned(parent)]),corrected);V size=MatrixScale(accepted[0][bone]);for(unsigned r=0;r<3;++r){out.r[r][0]*=size.x;out.r[r][1]*=size.y;out.r[r][2]*=size.z;}world[bone]=out;done[bone]=1;
 };for(unsigned bone=0;bone<world.size();++bone)if(descendant(bone,hand.upper))apply(apply,bone);
 // One fixed grip transform. The old weapon animation has no say in either
 // this wrist curve or the weapon's direction relative to the occupied hand.
 M refHand=RigidTransition(accepted[0][hand.hand]);refHand.t=Scale(refHand.t,p.scale);M fixedGrip=Product(Inverse(refHand),grips[0].mounts[0]);M actualHand=RigidTransition(world[hand.hand]);actualHand.t=Scale(actualHand.t,p.scale);input.mounts[0]=Product(actualHand,fixedGrip);input.solved[0]=true;input.objectScale[0]=grips[0].objectScale[0];
 return input.bonesOnly||UnityHandsV6::SkinWorld(d,world,v,n);
}
inline bool AttachmentPose(const UnityGame::Actor&actor,const Input&input,const M&root,const UnityGame::Attachment&attachment,M&result){const auto*member=UnityWeaponCatalog::Assembly(attachment.name);if(!member)return false;unsigned side=member->side;const auto&binding=input.bindings[side];if(!binding.spec||!input.solved[side]||binding.spec->role!=member->role||attachment.driver!=binding.driver)return false;M holder=Product(actor.poses[unsigned(binding.driver)],binding.relative);result=Product(Product(root,input.mounts[side]),Product(Inverse(holder),attachment.pose));return true;}
}
