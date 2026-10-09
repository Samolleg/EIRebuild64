#pragma once
namespace ModSpellIdentity {
// Raw subtype high words in native39454a3. Never reinterpret stored enum IDs.
constexpr int NativeTest=54,NativeTestArrow=55,NativeTestWall=56;
inline int AttackOverride(int id){
 if(id==NativeTest||(id>=57&&id<=62))return 3;
 if(id>=63&&id<=68)return 5;
 if(id>=69&&id<=74)return 4;
 return -1;
}
inline bool ExtendedLifecycle(int id){return id==NativeTestArrow||id==NativeTestWall||(id>=57&&id<=74);}
inline bool Wall(int id){return id==NativeTestWall||id==61||id==62;}
inline bool PointEligible(bool release,bool ready,bool owner,float mana,float cost,float range,float distance2,float radius){
 return ready&&owner&&cost<=mana&&range>0&&(!release||(radius>0&&distance2<=range*range));
}
template<class Send> void ActiveSelfCast(Send send){send();}
}
