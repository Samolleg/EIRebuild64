#pragma once
namespace ModCampSale {
enum class Role {Other,Weapon,Armor,Quick,Instruction,Treasure,Material};
enum class Event {Restore,Shape,Material,TreasureErase};
struct Info {Role role;int components,count;};
// Lazy description follows restoration. Forbidden(true) asks about shape;
// Forbidden(false) asks about raw-subtype material. Native brace nesting differs.
template<class Describe,class Forbidden,class Emit>
inline void Process(int shop,bool metal,Describe describe,Forbidden forbidden,Emit emit){
 if(shop==2)return;
 if(shop!=1||!metal)emit(Event::Restore,0,0);
 const Info info=describe();
 switch(info.role){
 case Role::Weapon:
  if(!forbidden(true)){emit(Event::Shape,0,0);if(!forbidden(false))emit(Event::Material,1,info.components);}
  break;
 case Role::Armor:case Role::Quick:
  if(!forbidden(true))emit(Event::Shape,0,0);
  if(!forbidden(false))emit(Event::Material,1,info.components);
  break;
 case Role::Instruction:
  if(!forbidden(true))emit(Event::Shape,0,0);
  break;
 case Role::Treasure:
  emit(Event::TreasureErase,0,0);return; // caller may release the final item owner
 case Role::Material:
  if(!forbidden(false))emit(Event::Material,info.count,info.count);
  break;
 default:break;
 }
}
}
