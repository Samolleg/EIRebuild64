#pragma once
#include "SpellSystem.h"
#include "RPGSystem.h"
#include "ModSpellIdentity.h"
#include <algorithm>
#include <cmath>

namespace ModSpells {
inline float StrongestSignedEffect(float first,float second){return fabs(first)>=fabs(second)?first:second;}
// LS_ShieldsAndWeaponGroups: elemental identity only, not an FX alias.
inline int ElementalBase(int id){
 switch(id){
 case SPELL_ID_FIREBALLDARK: case SPELL_ID_FIREBALLLIGHT: return SPELL_ID_FIREBALL;
 case SPELL_ID_FIREARROWDARK: case SPELL_ID_FIREARROWLIGHT: return SPELL_ID_FIREARROW;
 case SPELL_ID_FIREWALLDARK: case SPELL_ID_FIREWALLLIGHT: return SPELL_ID_FIREWALL;
 case SPELL_ID_LIGHTNINGDARK: case SPELL_ID_LIGHTNINGLIGHT: return SPELL_ID_LIGHTNING;
 case SPELL_ID_INVOKE_LGHTNGDARK: case SPELL_ID_INVOKE_LGHTNGLIGHT: return SPELL_ID_INVOKE_LGHTNG;
 case SPELL_ID_WALL_LGHTNGDARK: case SPELL_ID_WALL_LGHTNGLIGHT: return SPELL_ID_WALL_LGHTNG;
 case SPELL_ID_ACIDRAYDARK: case SPELL_ID_ACIDRAYLIGHT: return SPELL_ID_ACIDRAY;
 case SPELL_ID_COLUMN_ACIDDARK: case SPELL_ID_COLUMN_ACIDLIGHT: return SPELL_ID_COLUMN_ACID;
 case SPELL_ID_POISONFOGDARK: case SPELL_ID_POISONFOGLIGHT: return SPELL_ID_POISONFOG;
 default:return id;
 }
}
inline bool ExtendedElementalPortion(int id,float power,int duration,SRPGAttackPortion&ap,int magicSubtype=0){
 if(id<SPELL_ID_VAMP)return false;
 ap.Clear();ap.hl=RPGHL_ALL;ap.fpToHit=FLT_MAX;ap.fBackStabMultiplier=1;ap.bCalledShot=false;
 int attack=-1;
 switch(ElementalBase(id)){
 case ModSpellIdentity::NativeTestArrow:case ModSpellIdentity::NativeTestWall:case SPELL_ID_FIREBALL:case SPELL_ID_FIREARROW:case SPELL_ID_FIREWALL:attack=RPGAT_TERMICAL;break;
 case SPELL_ID_LIGHTNING:case SPELL_ID_INVOKE_LGHTNG:case SPELL_ID_WALL_LGHTNG:attack=RPGAT_ELECTRICAL;break;
 case SPELL_ID_ACIDRAY:case SPELL_ID_COLUMN_ACID:case SPELL_ID_POISONFOG:attack=RPGAT_CHEMICAL;break;
 }
 if(id==ModSpellIdentity::NativeTest)attack=3+magicSubtype;
 if(attack>=0&&attack<int(RPG_NUM_ATTACK_TYPES)){ap.fpAttacks[attack]=1;ap.fpDamage=power;ap.bAreaEffect=id==ModSpellIdentity::NativeTest;}
 // Reference coPercent = 5 / 3 is integer division, hence exactly one.
 if(duration>1){ap.fpDamagePercentage=1;ap.fpDamage*=4.f/float(duration);}
 return true;
}
inline float ManaCost(float base,float bonus){return (std::max)(0.f,base*bonus);}
inline bool HasMana(float mana,float cost){return !(cost>mana+.0001f);}

// Balance.cpp ApplyRune: true means skip the original rune switch, not bookkeeping.
inline bool ApplyRune(const SSDBModifier&r,const SSDBSpellPrototype&p,DWORD&actions,float&power,long&duration,DWORD&resolve){
 const char*a=r.pszCode;
 if(a[0]=='x'){
  if(a[1]=='a'){float v=float(actions)-r.fpValue;actions=DWORD(v>22.5f?v:22.5f);return true;}
  if(a[1]=='b'){actions=DWORD(int(float(actions)*r.fpValue));return true;}
  if(a[1]=='d'){duration=long(float(duration)-p.dwDuration*.5f);duration=(std::max)(0L,duration);return true;}
 }
 if((a[0]=='x'||a[0]=='i')&&(a[1]=='t'||a[1]=='c')){
  resolve|=a[1]=='t'?SPELL_ITEM_TRIGGER:SPELL_ITEM_CONTINUOUS;
  power-=(std::max)(0.f,p.fpEffect*.75f);power=(std::max)(0.f,power);
  duration=long(float(duration)-p.dwDuration*(a[0]=='x'?.5f:.25f));duration=(std::max)(0L,duration);return true;
 }
 if(a[0]=='f'&&a[1]=='u')resolve|=SPELL_FILTER_UNDEAD;
 return false;
}
inline bool ExcludesUndead(DWORD filters,const char*mask){return (filters&SPELL_FILTER_UNDEAD)&&(!strcmp(mask,"unhusk")||!strcmp(mask,"unhuzf")||!strcmp(mask,"unhuzm"));}
inline int CastTime(const SRPGUnitStats&stats,const CSpell*spell,bool player,int difficulty){
 if(!spell)return 0;float action=stats.action.fpEffectiveValue;if(std::abs(action)>.00001f)action=15.f/action;
 float perk=1;for(int i=0;i<RPG_NUM_PERKS;++i){auto*p=stats.GetPerk(i);if(p&&p->nPerkExclusive==21)perk=1-p->dwModifier*.01f;}
 float head=1;if(stats.hls[0].state!=RPGALS_NOTUSED)head=.6f+.4f*(stats.hls[0].fpHP<0?0:stats.hls[0].fpHP/stats.hls[0].fpMaxHP);
 return Float2Int(float(spell->GetActionPoints())*action/head*perk*(player&&difficulty>0?.8f:1.f));
}
inline int KillExperience(float base,int multiplier){return (std::max)(3,int(base*multiplier));}
inline int DisplayExperience(float scaled){return int(scaled);}
inline bool ItemCanCast(float mana,float cost){return mana>=cost;}
}
