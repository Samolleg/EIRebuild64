#pragma once
#include "UnityHandControlLegacy.h"
#include "UnityWeaponCatalog.h"
#include "UnityShieldCorrection.h"
namespace UnityHandsV7 {
using namespace UnityBody;using Role=UnityWeaponCatalog::Role;
enum class Gesture {Open,Cylinder,Trigger,Hook,Shield};
struct Binding {const UnityWeaponCatalog::Spec*spec=nullptr;M relative{};V point{},axis{0,1,0};double radius=.016;int driver=-1;bool shieldCorrection=false;};
struct Input {double sitting=0;std::array<double,2>grip{};bool weapon=false,twoHand=false;M oldRelative{};bool generic=false;std::array<Binding,2>bindings{};std::array<Gesture,2>gestures{Gesture::Open,Gesture::Open};int primary=-1,support=-1;double supportWeight=1;bool supportPoint=false;V oldSupportPoint{};};
inline V GripCentre(unsigned side,Gesture gesture=Gesture::Cylinder){const auto&s=UnityHandsData::Sides[side];if(gesture==Gesture::Hook)return Add(s.palm,Add(Scale(s.forward,.025),Scale(s.normal,.022)));return UnityHandsV6::GripCentre(side);}
inline M Hand(const std::vector<M>&world,unsigned side,double scale,bool sized=false){unsigned bone=UnityHandsData::Sides[side].hand;M m=Proper(world[bone]);m.t=sized?world[bone].t:Scale(world[bone].t,scale);return m;}
inline M OldHandToNew(const Data&d,unsigned side){unsigned bone=UnityHandsData::Sides[side].hand;return Product(Inverse(Proper(d.rest[bone])),Proper(Product(Inverse(d.gameBind[0]),d.gameBind[7+side])));}
inline V BindingShift(const Data&d,const UnityBodyAppearance::Profile&p,const Input&input,const std::vector<M>&world,unsigned side,bool sized=false){
 const auto&binding=input.bindings[side];if(!binding.spec)return {};
 if(binding.spec->role==Role::Shield){if(!binding.shieldCorrection)return {};unsigned bone=UnityHandsData::Sides[side].forearm;M map=Product(Inverse(Proper(d.rest[bone])),Proper(Product(Inverse(d.gameBind[0]),d.gameBind[unsigned(binding.driver)])));V delta=Sub(UnityShieldCorrection::Reference,Point(binding.relative,binding.point));return Rotate(Product(Proper(world[bone]),map),delta);}
 M local=Product(OldHandToNew(d,side),binding.relative);V point=Point(local,binding.point),axis=Unit(Rotate(local,binding.axis));V centre=Scale(GripCentre(side,input.gestures[side]),p.scale);
 V shift=Sub(centre,Add(point,Scale(axis,Dot(Sub(centre,point),axis))));return Rotate(Hand(world,side,p.scale,sized),shift);
}
inline M BindingPose(const Data&d,const UnityBodyAppearance::Profile&p,const Input&input,const std::vector<M>&world,unsigned side,bool adjust=true){const auto&b=input.bindings[side];M m;
 if(b.spec&&b.spec->role==Role::Shield){unsigned bone=UnityHandsData::Sides[side].forearm;M forearm=Proper(world[bone]);forearm.t=Scale(world[bone].t,p.scale);M map=Product(Inverse(Proper(d.rest[bone])),Proper(Product(Inverse(d.gameBind[0]),d.gameBind[unsigned(b.driver)])));m=Product(forearm,Product(map,b.relative));}
 else m=Product(Hand(world,side,p.scale),Product(OldHandToNew(d,side),b.relative));if(adjust)m.t=Add(m.t,BindingShift(d,p,input,world,side));return m;}
inline V AttachmentShift(const Data&d,const UnityBodyAppearance::Profile&p,const Input&input,const std::vector<M>&world,int driver,const std::string&name,bool sized){
 if(!input.generic)return {};const auto*member=UnityWeaponCatalog::Assembly(name);if(!member)return {};
 unsigned side=member->side;const auto&binding=input.bindings[side];if(!binding.spec||driver!=(binding.spec->role==Role::Shield?binding.driver:int(7+side)))return {};
 if(member->role!=binding.spec->role)return {};return BindingShift(d,p,input,world,side,sized);
}
inline void Fingers(const Data&d,std::vector<M>&world,unsigned side,Gesture gesture,double grip,double relaxed,double radius){
 if(gesture==Gesture::Cylinder&&std::abs(radius-.016)<1e-12){UnityHandsV6::Fingers(d,world,side,grip,relaxed);return;}
 const auto&s=UnityHandsData::Sides[side];grip=std::clamp(grip,0.,1.);relaxed=std::clamp(relaxed,0.,1.);if(grip==0&&relaxed==0)return;
 double size=std::clamp(1-(radius-.016)*12,.83,1.08);constexpr double pi=3.14159265358979323846;
 for(unsigned finger=0;finger<5;++finger)for(unsigned joint=0;joint<3;++joint){const double closed[]={finger?62.:28.,finger?88.:38.,finger?48.:24.};const double loose[]={8.,12.,6.};double degrees=closed[joint]*size;
  if(gesture==Gesture::Trigger&&finger==1){const double trigger[]={18,24,10};degrees=trigger[joint];}
  if(gesture==Gesture::Hook){const double hooked[]={55,70,35},free[]={12,18,8};degrees=finger==1||finger==2?hooked[joint]:finger==0?closed[joint]*.7:free[joint];}
  if(gesture==Gesture::Shield)degrees*=.8;
  double angle=(degrees*grip+loose[joint]*relaxed*(1-grip))*pi/180;unsigned bone=s.fingers[finger][joint];Replace(d,world,bone,Mul(Quaternion(world[bone]),UnityHandsV6::AxisAngle(s.axes[finger][joint],angle)));
 }
}
inline bool Compute(const Data&d,const UnityBodyAppearance::Profile&p,const std::array<M,15>&game,const Input&input,std::vector<V>&v,std::vector<V>&n,std::vector<M>&world){
 if(!input.generic){UnityHandsV6::Input old;old.sitting=input.sitting;old.grip=input.grip;old.weapon=input.weapon;old.twoHand=input.twoHand;old.oldRelative=input.oldRelative;return UnityHandsV6::Compute(d,p,game,old,v,n,world);}
 if(!std::isfinite(input.sitting)||!std::isfinite(input.grip[0])||!std::isfinite(input.grip[1]))return false;
 if(input.sitting<=0&&input.grip[0]<=0&&input.grip[1]<=0)return UnityBodyAppearance::Compute(d,p,game,v,n,world);
 world=Retarget(d,game);LimitRetarget(d,world);
 if(input.primary>=0&&input.support>=0){unsigned primary=unsigned(input.primary),side=unsigned(input.support);const auto&s=UnityHandsData::Sides[side];M mount=BindingPose(d,p,input,world,primary);
  V contact;
  if(input.supportPoint){M hand=Hand(world,primary,p.scale);contact=Add(Point(Product(hand,OldHandToNew(d,primary)),input.oldSupportPoint),BindingShift(d,p,input,world,primary));}
  else{const auto&b=input.bindings[primary];V axis=Unit(Rotate(mount,b.axis)),point=Point(mount,b.point);V own=Add(Scale(world[s.hand].t,p.scale),Rotate(world[s.hand],Scale(GripCentre(side,input.gestures[side]),p.scale)));contact=Add(point,Scale(axis,Dot(Sub(own,point),axis)));}
  V target=Scale(Sub(contact,Rotate(world[s.hand],Scale(GripCentre(side,input.gestures[side]),p.scale))),1/p.scale);if(!UnityHandsV6::Reach(d,world,s,target,Quaternion(world[s.hand]),input.supportWeight))return false;
 }
 std::vector<M>preview;if(input.sitting>0){preview=world;Control(d,preview);UnityBodyAppearance::ApplyScales(p,preview);}
 if(input.sitting>0)for(unsigned side=0;side<2;++side){const auto&s=UnityHandsData::Sides[side];const auto&t=UnityHandsData::SittingTriangles[side];V a=UnityHandsV6::Vertex(d,preview,t[0]),b=UnityHandsV6::Vertex(d,preview,t[1]),c=UnityHandsV6::Vertex(d,preview,t[2]),normal=Unit(Cross(Sub(b,a),Sub(c,a)));
  V point=Add(Scale(Add(Add(a,b),c),1./3),Scale(normal,.007));
  // Wider shoulders need a correspondingly wider contact on the same thigh
  // plane; otherwise the wrist is forced into a sharper bend by the old target.
  double wider=std::max(0.,std::abs(d.rest[s.upper].t.x)-.12722583444638522);if(wider>1e-5){M torso=Product(Proper(preview[3]),Inverse(Proper(d.rest[3])));V outward=Rotate(torso,{side==0?1.:-1.,0,0});outward=Unit(Sub(outward,Scale(normal,Dot(outward,normal))));point=Add(point,Scale(outward,1.5*wider));}
  M desired=Product(UnityHandsV6::Basis(Sub(preview[s.calf].t,preview[s.thigh].t),Scale(normal,-1)),Inverse(UnityHandsV6::Basis(s.forward,s.normal)));V wrist=Sub(point,Rotate(desired,s.palm));if(!UnityHandsV6::Reach(d,world,s,wrist,Quaternion(desired),input.sitting*(1-std::clamp(input.grip[side],0.,1.))))return false;}
 Control(d,world);for(unsigned side=0;side<2;++side)Fingers(d,world,side,input.gestures[side],input.grip[side],input.sitting,input.bindings[side].radius);
 UnityBodyAppearance::ApplyScales(p,world);return UnityHandsV6::SkinWorld(d,world,v,n);
}
}
