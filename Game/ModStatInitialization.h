#pragma once
#include "RpgSystem.h"
#include "Unit.h"
#include "Item.h"
#include <array>
#include <map>
#include <vector>
#include <cstring>
#include <cmath>

// Adapted from the verified addon's StatInitialization/RegenerationInvalidation.
// Owned by the package; no original-address layouts and no serialized additions.
namespace ModInitialization {
struct Identity {
 const void *unit,*specific,*common,*stats;unsigned subtype;
 bool operator==(const Identity& b)const{return unit==b.unit&&specific==b.specific&&common==b.common&&stats==b.stats&&subtype==b.subtype;}
};
inline Identity Identify(CUnit&u,const SRPGCommonStats&c,const SRPGUnitStats&s){return {&u,u.GetUnitSpecific(),&c,&s,u.GetSubtypeID()};}
inline bool Same(const SRPGCommonStats&a,const void*stats,const SRPGCommonStats&b,const SRPGUnitStats&s){
 SRPGCommonStats normalized;memcpy(&normalized,&a,sizeof(a));normalized.ssMP.fpCurrent=b.ssMP.fpCurrent;normalized.nRegenerateCounter=b.nRegenerateCounter;
 return !memcmp(&normalized,&b,sizeof(b))&&!memcmp(stats,&s,sizeof(s));
}
struct Ledger {
 struct Completed {Identity identity;SRPGCommonStats common;std::array<unsigned char,sizeof(SRPGUnitStats)> stats;};
 std::map<int,Completed> completed;unsigned epoch=0;
 void Reset(){completed.clear();++epoch;}
 void Invalidate(int id){completed.erase(id);}
 void Finish(CUnit&u,const SRPGCommonStats&c,const SRPGUnitStats&s){
  Invalidate(u.GetID());if(!u.GetUnitSpecific()||completed.size()>=8192||!std::isfinite(c.ssHP.fpCurrent)||!std::isfinite(c.ssHP.fpMax)||!std::isfinite(c.ssMP.fpCurrent)||!std::isfinite(c.ssMP.fpMax))return;
  auto&v=completed[u.GetID()];v.identity=Identify(u,c,s);memcpy(&v.common,&c,sizeof(c));memcpy(v.stats.data(),&s,sizeof(s));
 }
 bool Ready(CUnit&u,const SRPGCommonStats&c,const SRPGUnitStats&s)const{
  auto it=completed.find(u.GetID());return it!=completed.end()&&it->second.identity==Identify(u,c,s)&&Same(it->second.common,it->second.stats.data(),c,s);
 }
};
struct Inputs {
 SRPGCommonStats common;std::array<unsigned char,sizeof(SRPGUnitStats)> stats;
 std::vector<CSpellEffect> effects;unsigned weapon;
 struct Item {CItem*pointer;float hp,maxHp;unsigned subtype;};std::vector<Item> items;
 static Item Read(CItem*p){return {p,p?p->GetHP():0,p?p->GetMaxHP():0,p?p->GetSubtypeID():0};}
 Inputs(const SRPGCommonStats&c,const SRPGUnitStats&s,CUnit&u){
  memcpy(&common,&c,sizeof(c));memcpy(stats.data(),&s,sizeof(s));weapon=u.GetCurrentWeaponSlotIndex();
  for(int i=0;i<u.GetSpellEffects().GetSize();++i)effects.push_back(u.GetSpellEffects()[i]);
  for(int i=0;i<QI_MAX_WEAPONS;i++)items.push_back(Read(u.GetQWeapon(i)));
  for(int i=0;i<RPG_NUM_ARMOR_TYPES;i++)items.push_back(Read(u.GetQArmor(i)));
  for(int i=0;i<QI_MAX_ITEMS;i++)items.push_back(Read(u.GetQItem(i)));
 }
 bool MpOnly(const SRPGCommonStats&c,const SRPGUnitStats&s,CUnit&u)const{
  if(!std::isfinite(common.ssMP.fpCurrent)||!std::isfinite(c.ssMP.fpCurrent)||!std::isfinite(c.ssHP.fpCurrent)||!std::isfinite(c.ssHP.fpMax)||common.ssMP.fpCurrent==c.ssMP.fpCurrent||!Same(common,stats.data(),c,s))return false;
  if(u.GetCurrentWeaponSlotIndex()!=weapon||u.GetSpellEffects().GetSize()!=int(effects.size()))return false;
  for(int i=0;i<int(effects.size());++i){const auto&a=effects[i];const auto&b=u.GetSpellEffects()[i];if(a.nSpellID!=b.nSpellID||a.nVisStrength!=b.nVisStrength||a.nDuration!=b.nDuration||a.fpPower!=b.fpPower||a.fpRadius!=b.fpRadius||a.bQuiet!=b.bQuiet)return false;}
  size_t index=0;auto same=[&](CItem*p){auto b=Read(p);const auto&a=items[index++];return a.pointer==b.pointer&&a.hp==b.hp&&a.maxHp==b.maxHp&&a.subtype==b.subtype;};
  for(int i=0;i<QI_MAX_WEAPONS;i++)if(!same(u.GetQWeapon(i)))return false;
  for(int i=0;i<RPG_NUM_ARMOR_TYPES;i++)if(!same(u.GetQArmor(i)))return false;
  for(int i=0;i<QI_MAX_ITEMS;i++)if(!same(u.GetQItem(i)))return false;
  return true;
 }
}
;
}
