#pragma once
#include <algorithm>
#include <functional>

namespace ModConditionalCombat {
enum class Owner { Inherited, DirectDefender };
inline bool Fallback(bool shit,bool attacks){return shit&&!attacks;}
struct Backend {
 std::function<void()> retailSeed,initialStats,difficulty;
 std::function<float()> roll;
 std::function<void(bool,float)> retailDamage;
 std::function<void(Owner)> raw;
 std::function<void(float)> damagePair;
 std::function<void(int)> attackCallback;
};
inline void Produce(Owner owner,Backend& b){
 if(owner==Owner::Inherited){
  b.retailSeed();
  float maximumRoll=b.roll();b.retailDamage(true,maximumRoll);
  float minimumRoll=b.roll();b.retailDamage(false,minimumRoll);
  b.initialStats();
 }
 b.raw(owner);
 float pairRoll=b.roll();b.damagePair(pairRoll);
 b.difficulty();b.attackCallback(owner==Owner::Inherited?40:53);
}
inline int Unarmed(float naw,float skill,bool direct){return int(naw+(direct?skill:0.f));}
inline float Base(float attack,float jitter,float distance){return (std::max)(0.f,attack+jitter-distance);}
inline float Location(float score,float modifier){return (std::max)(0.f,score+modifier);}
template<class Roll> bool Avoidance(bool avoid,float power,float backstab,bool stun,Roll roll){
 float value=roll(); // Native consumes this draw even when no avoidance effect exists.
 return avoid&&power>value&&backstab<=1&&!stun;
}

inline bool OrdinaryCreation(bool shit,bool attacks,bool taggedSkill,bool left){return Fallback(shit,attacks)&&!taggedSkill&&!left;}
inline bool RetailWeapon(bool personage,int slot,bool exists){return personage&&slot>=0&&slot<4&&exists;}
inline bool RetailRear(bool owner,bool bow,float dot){return owner&&!bow&&dot<-.5f;}
inline float WeaponToHit(int dice,int maximum){return float(2*dice-(maximum+1));}
struct CreationBackend {std::function<void()> clear,weapon,native,modified,metadata;};
inline void CreateRetail(bool weapon,CreationBackend& b){b.clear();if(weapon)b.weapon();else b.native();b.modified();b.metadata();}

enum class DamageCall { RecipientVirtual, DirectCustom };
inline bool DownstreamFallback(bool shit,bool attacks,DamageCall call){return Fallback(shit,attacks)&&call==DamageCall::RecipientVirtual;}
inline bool RejectOtherExperienceOwner(bool inherited,bool otherOwner){return !inherited&&otherOwner;}
inline bool CustomEquipmentPolicy(bool inherited){return !inherited;}
template<class Inherited,class Existing> void QueryDefense(bool inherited,Inherited first,Existing second){if(inherited)first();else second();}
}
