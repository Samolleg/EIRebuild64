#pragma once
#include "RPGSystem.h"
#include "SpellSystem.h"
#include "PerkDatabase.h"
#include "idbFormat.h"
#include <cmath>
#include <climits>

// Native 39454a3 FormToolTipW/FormToolTip: presentation only, borrowed inputs.
namespace ModIdleTooltips {
inline bool Attacking(const CSpell& spell){const int id=spell.GetSubtypeID()>>16;return id<=SPELL_ID_POISONFOG||id==SPELL_ID_VAMP;}
inline int DamageType(const CSpell& spell){return spell.GetMagicSubtype()==1?5:spell.GetMagicSubtype()==2?4:3;}
inline int Skill(const SRPGUserUnitStats& stats,const CSpell& spell){
 const int type=spell.GetMagicSubtype();float base=0;
 if(type>=RPGMST_FIRE&&type<=RPGMST_ACID)base=stats.cAutoSkills[RPGASKL_ELEMENTAL];
 else if(type>=RPGMST_ILLUSION&&type<=RPGMST_DIVINATION)base=stats.cAutoSkills[RPGASKL_SENSE];
 else if(type>=RPGMST_ENCHANTMENTS&&type<=RPGMST_DOMINATION)base=stats.cAutoSkills[RPGASKL_ASTRAL];
 if(type>=0&&type<RPG_NUM_MAGIC_SUBTYPES)base+=stats.GetPerkModifier(RPGPRK_FIRE+type);
 return int(base+int(stats.cIntelligence)-25);
}
inline float SkillModifier(const SRPGUserUnitStats& stats,const CSpell& spell){return .8f+(Skill(stats,spell)*30.0f/120.0f)/100.0f;}
enum QuickLabel { MANA,EFFECT,SPEED,RANGE,AREA,DURATION,TARGETS,METERS,SECONDS,INSTANT,AREAUNIT,FIXEDEFF,QUICK_LABELS };
inline CString Quick(const CString& name,int type,const CSpell* spell,const SRPGUserUnitStats& stats,
                     float mp,float maxMP,float additional,const CString* labels){
 CString text=name,row;if(!spell||(type!=IDB_WAND&&type!=IDB_SCROLL))return text;
 text+=" \n";const auto& s=*spell;
 if(type==IDB_WAND){
  row.Format("%s %d/%d (%d)\n",(LPCSTR)labels[MANA],int(mp),int(maxMP),int(s.GetManaCost()));text+=row;
  if(!s.GetPrototype()->bRuneAllowed[SDBAR_EFFECT])row.Format("%s %s  ",(LPCSTR)labels[EFFECT],(LPCSTR)labels[FIXEDEFF]);
  else row.Format("%s %.1f  ",(LPCSTR)labels[EFFECT],(s.GetPower()+additional)*(Attacking(s)?SkillModifier(stats,s):1.0f));text+=row;
  if(s.GetSpeed()<1.f)row.Format("%s %s \n",(LPCSTR)labels[SPEED],(LPCSTR)labels[INSTANT]);
  else row.Format("%s %d \n",(LPCSTR)labels[SPEED],int(s.GetSpeed()));text+=row;
  row.Format("%s %d%s  ",(LPCSTR)labels[RANGE],int(s.GetRange()),(LPCSTR)labels[METERS]);text+=row;
  if(s.IsOnUnit())row.Format("%s %s \n",(LPCSTR)labels[AREA],(LPCSTR)labels[AREAUNIT]);
  else row.Format("%s %.1f%s \n",(LPCSTR)labels[AREA],s.GetArea(),(LPCSTR)labels[METERS]);text+=row;
  if(s.GetDuration()<2)row.Format("%s %s  ",(LPCSTR)labels[DURATION],(LPCSTR)labels[INSTANT]);
  else row.Format("%s %.1f%s  ",(LPCSTR)labels[DURATION],s.GetDuration()/15.f,(LPCSTR)labels[SECONDS]);text+=row;
  row.Format("%s %d \n",(LPCSTR)labels[TARGETS],int(s.GetNumTargets()));text+=row;
 }else{
  if(!s.GetPrototype()->bRuneAllowed[SDBAR_EFFECT])row.Format("%s %s\n",(LPCSTR)labels[EFFECT],(LPCSTR)labels[FIXEDEFF]);
  else row.Format("%s %.1f  ",(LPCSTR)labels[EFFECT],s.GetPower()+additional);text+=row;
  if(s.GetDuration()<2)row.Format("%s %s\n",(LPCSTR)labels[DURATION],(LPCSTR)labels[INSTANT]);
  else row.Format("%s %.1f%s\n",(LPCSTR)labels[DURATION],s.GetDuration()/15.f,(LPCSTR)labels[SECONDS]);text+=row;
 }
 return text;
}
struct WeaponInput {
 CString name,damageType="Unknown",spellName="Unknown",spellDamageType;
 CString damage,actionsLabel,rangeLabel,spellLabel,blockPowerLabel,blockLabel,parryLabel,hpLabel,effectLabel,manaLabel;
 bool shield=false,broken=false;float minimum=0,maximum=0,range=0,hp=0,maxHP=0,mp=0,maxMP=0;
 int actions=0,difficulty=0;float blockPower=0,block=0,parry=0;const CSpell* spell=nullptr;const SRPGUserUnitStats* stats=nullptr;
};
inline bool Durability(float hp,float maximum,int& percent){
 const float value=hp/maximum*100.0f;
 if(maximum<=0||!std::isfinite(value)||double(value)<INT_MIN||double(value)>INT_MAX)return false;
 percent=int(value);return true;
}
inline CString Weapon(const WeaponInput& in){
 CString text=in.name+"\n",row;
 if(in.shield){
  row.Format("%s %.0f\n",(LPCSTR)in.blockPowerLabel,in.blockPower);text+=row;
  row.Format("%s %.0f%%\n",(LPCSTR)in.blockLabel,in.block);text+=row;
  row.Format("%s %.0f%%\n",(LPCSTR)in.parryLabel,in.parry);text+=row;
 }else{
  row.Format("%s %.0f-%.0f %s\n",(LPCSTR)in.damage,in.minimum,in.maximum,(LPCSTR)in.damageType);text+=row;
  row.Format("%s %d  %s %d\n",(LPCSTR)in.actionsLabel,in.actions,(LPCSTR)in.rangeLabel,int(in.range));text+=row;
 }
 int hp=0;if(!in.broken&&Durability(in.hp,in.maxHP,hp)){row.Format("%s %d%%\n",(LPCSTR)in.hpLabel,hp);text+=row;}
 if(in.spell&&in.stats){
  const auto& spell=*in.spell;row.Format("%s %s\n",(LPCSTR)in.spellLabel,(LPCSTR)in.spellName);text+=row;
  if(Attacking(spell))row.Format("%s %.0f %s\n",(LPCSTR)in.damage,spell.GetPower()*(1+in.difficulty*.25f)*SkillModifier(*in.stats,spell),(LPCSTR)in.spellDamageType);
  else row.Format("%s %.0f\n",(LPCSTR)in.effectLabel,spell.GetPower());text+=row;
  row.Format("%s %d/%d (%d)\n",(LPCSTR)in.manaLabel,int(in.mp),int(in.maxMP),int(spell.GetManaCost()));text+=row;
 }
 return text;
}
}
