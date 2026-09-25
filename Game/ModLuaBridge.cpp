#include "StdAfx.h"
#include "UnitClientSpecific.h"
#include "ModLuaBridge.h"
#include "ModStatRules.h"
#include "luajit-x86/lua.hpp"
#include "RpgSystem.h"
#include "Unit.h"
#include "Item.h"
#include "SpellSystem.h"
#include "ItemDatabase.h"
#include "PerkDatabase.h"
#include "GeneralSettings.h"
#include <limits>
#include <type_traits>
#include <stdexcept>
namespace ModLua {
namespace {
enum Kind {Stats,Common,Unit,Item,Primary,Secondary,Bytes,Floats,Perk,HitLocation,ArmorLayer,ReadOnlyFloats,ClientStats,ReadOnlyBytes,Spell,SpellEffect,Attack};
struct Ref {void *object;Kind kind;int count;std::shared_ptr<Scope> scope;};
const char *meta="EIRebuild.TypedObject";
Ref &Get(lua_State*s,int n=1){auto*p=(Ref*)luaL_checkudata(s,n,meta);if(!p->scope->active)luaL_error(s,"Expired engine callback object");return *p;}
void PushRef(lua_State*s,void*p,Kind k,const std::shared_ptr<Scope>&scope,int count=0){if(!p){lua_pushnil(s);return;}auto*r=new(lua_newuserdata(s,sizeof(Ref))) Ref{p,k,count,scope};luaL_getmetatable(s,meta);lua_setmetatable(s,-2);}
int GC(lua_State*s){((Ref*)luaL_checkudata(s,1,meta))->~Ref();return 0;}
template<class T>int Field(lua_State*s,T&v,bool write){if constexpr(std::is_enum<T>::value){auto n=static_cast<typename std::underlying_type<T>::type>(v);int result=Field(s,n,write);if(write)v=static_cast<T>(n);return result;}else {if(write){double n=luaL_checknumber(s,3);if(!std::isfinite(n)||n<(std::numeric_limits<T>::lowest)()||n>(std::numeric_limits<T>::max)()||(std::is_integral<T>::value&&floor(n)!=n))return luaL_error(s,"Engine field out of range");v=static_cast<T>(n);return 0;}lua_pushnumber(s,v);return 1;}}
int CheckedInteger(lua_State*s,int index){double n=luaL_checknumber(s,index);if(!std::isfinite(n)||floor(n)!=n||n<(std::numeric_limits<int>::min)()||n>(std::numeric_limits<int>::max)())return luaL_error(s,"Expected exact engine integer");return int(n);}
int ArrayValue(lua_State*s){auto&r=Get(s);int i=CheckedInteger(s,2);if(i<0||i>=r.count)return luaL_error(s,"Engine array index out of range");if(r.kind==Bytes||r.kind==ReadOnlyBytes)lua_pushnumber(s,((BYTE*)r.object)[i]);else if(r.kind==Floats||r.kind==ReadOnlyFloats)lua_pushnumber(s,((float*)r.object)[i]);else return luaL_error(s,"Expected engine array");return 1;}
int ArraySet(lua_State*s){auto&r=Get(s);int i=CheckedInteger(s,2);if(i<0||i>=r.count)return luaL_error(s,"Engine array index out of range");if(r.kind==Bytes)return Field(s,((BYTE*)r.object)[i],true);if(r.kind==Floats)return Field(s,((float*)r.object)[i],true);return luaL_error(s,"Expected engine array");}
int PerkModifier(lua_State*s){auto&r=Get(s);int i=CheckedInteger(s,2);if((r.kind!=Stats&&r.kind!=ClientStats)||i<0||i>=RPG_NUM_PERKS)return luaL_error(s,"Expected stats and valid perk");lua_pushnumber(s,r.kind==Stats?((SRPGUnitStats*)r.object)->GetPerkModifier(i):((const SRPGUserUnitStats*)r.object)->GetPerkModifier(i));return 1;}
int PrimaryAt(lua_State*s){auto&r=Get(s);int i=CheckedInteger(s,2);int mode=lua_tointeger(s,lua_upvalueindex(1));if(mode==2){if(r.kind!=Common||i<0||i>=RPG_NUM_ATTACK_TYPES)return luaL_error(s,"Invalid absorption index");PushRef(s,&((SRPGCommonStats*)r.object)->psAbsorbtions[i],Primary,r.scope);}else {if(r.kind!=Stats||i<0||i>=RPG_NUM_SENSES)return luaL_error(s,"Invalid sense index");auto*p=(SRPGUnitStats*)r.object;PushRef(s,mode?&p->percepts[i]:&p->senses[i],Primary,r.scope);}return 1;}

int StatAdjust(lua_State*s){auto&r=Get(s);int mode=lua_tointeger(s,lua_upvalueindex(1));float *value=nullptr;
 if(r.kind==Primary && mode/2<=1){auto*p=(SRPGPrimaryStat*)r.object;value=mode/2==0?&p->fpBaseValue:&p->fpEffectiveValue;}
 if(r.kind==Secondary && mode/2>=1){auto*p=(SRPGSecondaryStat*)r.object;value=mode/2==1?&p->fpCurrent:mode/2==2?&p->fpMax:&p->fpRegenerate;}
 if(!value)return luaL_error(s,"Invalid stat adjustment");double delta=luaL_checknumber(s,2);lua_settop(s,2);lua_pushnumber(s,*value+(mode%2?-delta:delta));return Field(s,*value,true);
}
int HitAt(lua_State*s){auto&r=Get(s);int i=CheckedInteger(s,2);if(r.kind!=Stats||i<0||i>=RPG_NUM_HIT_LOCATIONS)return luaL_error(s,"Invalid hit location index");PushRef(s,&((SRPGUnitStats*)r.object)->hls[i],HitLocation,r.scope);return 1;}
int LayerAt(lua_State*s){auto&r=Get(s);int i=CheckedInteger(s,2);if(r.kind!=HitLocation||i<0||i>=RPG_NUM_ARMOR_LAYERS)return luaL_error(s,"Invalid armor layer index");PushRef(s,&((SRPGHitLocation*)r.object)->layers[i],ArmorLayer,r.scope);return 1;}
int HitPoints(lua_State*s){auto&r=Get(s);if(r.kind!=Stats)return luaL_error(s,"Expected native stats");auto*p=(SRPGUnitStats*)r.object;double hp=luaL_checknumber(s,2);int op=lua_tointeger(s,lua_upvalueindex(1));
 if(!std::isfinite(hp)||fabs(hp)>(std::numeric_limits<float>::max)())return luaL_error(s,"Invalid hit points");
 // Native distribution writes only used hit locations; an empty skeleton is a no-op.
 if(op!=2){bool used=false;for(const auto&h:p->hls)used=used||h.state>RPGALS_NOTUSED;if(!used)return 0;}
 if(op!=2&&(!std::isfinite(p->hls[RPGHL_BODY].fpHPWeight)||p->hls[RPGHL_BODY].fpHPWeight<=0))return luaL_error(s,"Invalid body HP weight");
 for(auto&h:p->hls)if(h.state>RPGALS_NOTUSED){if(!std::isfinite(h.fpHPWeight)||!std::isfinite(h.fpHP)||!std::isfinite(h.fpMaxHP))return luaL_error(s,"Invalid hit location values");if(op!=0&&h.fpMaxHP<=0)return luaL_error(s,"Invalid hit location max HP");}
 if(op==2){lua_pushnumber(s,p->CollectHP(float(hp)));return 1;}
 if(op==1)p->DistributeHPProportional(float(hp));else p->DistributeHP(float(hp));return 0;
}


int EffectArray(lua_State*s){int arg=lua_istable(s,1)?2:1;auto&r=Get(s,arg);
 if((r.kind!=ReadOnlyFloats&&r.kind!=Floats) || CheckedInteger(s,arg+2)!=r.count || (lua_toboolean(s,arg+3)!=0)!=(r.kind==ReadOnlyFloats))return luaL_error(s,"Expected exact native array size and access mode");
 lua_getglobal(s,"Memory");lua_getfield(s,-1,"Type");lua_getfield(s,-1,"Float");bool type=lua_rawequal(s,arg+1,-1)!=0;lua_pop(s,3);
 if(!type)return luaL_error(s,"Native effects require Float element type");lua_pushvalue(s,arg);return 1;
}
int AddPerkRecord(lua_State*s){auto&r=Get(s);int index=CheckedInteger(s,2),level=CheckedInteger(s,3);
 if(r.kind!=Unit || r.object!=r.scope->unit || !r.scope->stagedStats)return luaL_error(s,"Perk mutation requires the active staged unit");
 if(index<0||index>=RPG_NUM_PERKS||level<0||level>=3||index*3+level>=thePerkDatabase.GetNumPerks())return luaL_error(s,"Invalid native perk record");
 try{r.scope->stagedStats->AddPerk(&thePerkDatabase,thePerkDatabase.GetPerk(index*3+level));}
 catch(const std::exception&e){lua_pushstring(s,e.what());return lua_error(s);}return 0;
}
int LeftHand(lua_State*s){auto&r=Get(s);if(r.kind!=Unit)return luaL_error(s,"Expected typed unit for left hand");auto*p=(CUnit*)r.object;if(!p->GetUnitSpecific())return luaL_error(s,"Left hand requires native unit-specific component");lua_pushinteger(s,r.scope->LeftHand(*p));return 1;}
int UnitWeapon(lua_State*s){auto&r=Get(s);int i=CheckedInteger(s,2);if(r.kind!=Unit||i<0||i>=QI_MAX_WEAPONS)return luaL_error(s,"Invalid weapon slot");auto*p=(CUnit*)r.object;if(!p->GetUnitSpecific())return luaL_error(s,"Weapons require native unit-specific component");PushRef(s,r.scope->Weapon(*p,i),Item,r.scope);return 1;}
int UnitEffect(lua_State*s){auto&r=Get(s);if(r.kind!=Unit)return luaL_error(s,"Expected unit");int id=CheckedInteger(s,2);if(id<0||id>=53)return luaL_error(s,"Invalid spell effect id");lua_pushnumber(s,((CUnit*)r.object)->GetEffectValue(id));return 1;}
int UnitArmor(lua_State*s){auto&r=Get(s);int index=CheckedInteger(s,2);if(r.kind!=Unit)return luaL_error(s,"Expected unit");if(index<0||index>=RPG_NUM_ARMOR_TYPES){lua_pushnil(s);return 1;}PushRef(s,((CUnit*)r.object)->GetQArmor(index),Item,r.scope);return 1;}
int UnitQuick(lua_State*s){auto&r=Get(s);int i=CheckedInteger(s,2);if(r.kind!=Unit||i<0||i>=QI_MAX_ITEMS)return luaL_error(s,"Invalid quick item slot");auto*p=(CUnit*)r.object;if(!p->GetUnitSpecific())return luaL_error(s,"Quick items require native unit-specific component");if(p->GetUnitSpecific()->WhoAmI()==HPC_UNITCLIENTSPECIFIC&&!p->GetOwner()){lua_pushnil(s);return 1;}PushRef(s,p->GetQItem(i),Item,r.scope);return 1;}
int ArmorMaterialName(lua_State*s){auto&r=Get(s);int index=CheckedInteger(s,2);if(r.kind!=Unit)return luaL_error(s,"Expected unit");if(index<0||index>=RPG_NUM_ARMOR_TYPES){lua_pushnil(s);return 1;}auto*armor=((CUnit*)r.object)->GetQArmor(index);if(!armor){lua_pushnil(s);return 1;}auto id=armor->GetMaterial();if(id>=unsigned(theItemDatabase.materials.GetNumEntries()))return luaL_error(s,"Equipped material missing from database");lua_pushstring(s,theItemDatabase.materials.GetEntry(int(id))->szName);return 1;}
int DatabaseName(lua_State*s){int index=CheckedInteger(s,1);int kind=lua_tointeger(s,lua_upvalueindex(1));if(index<0)return luaL_error(s,"Invalid item database index");const char*name=nullptr;
 if(kind==0){if(index>=theItemDatabase.materials.GetNumEntries())return luaL_error(s,"Material database index out of range");name=theItemDatabase.materials.GetEntry(index)->szName;}
 else if(kind==1){if(index>=theItemDatabase.weapons.GetNumEntries())return luaL_error(s,"Weapon database index out of range");name=theItemDatabase.weapons.GetEntry(index)->szName;}
 else if(kind==3){if(index>=theItemDatabase.armors.GetNumEntries())return luaL_error(s,"Armor database index out of range");name=theItemDatabase.armors.GetEntry(index)->szName;}
 else{if(index>=theItemDatabase.quickItems.GetNumEntries())return luaL_error(s,"Quick item database index out of range");name=theItemDatabase.quickItems.GetEntry(index)->szName;}
 // Value record for the name-only lookup: no engine address escapes to Lua.
 lua_newtable(s);lua_pushstring(s,name);lua_setfield(s,-2,"name");return 1;
}

int Heal(lua_State*s){
 auto&r=Get(s);auto&c=Get(s,2);double value=luaL_checknumber(s,3);
 if(r.kind!=Stats||c.kind!=Common||r.scope!=c.scope||!std::isfinite(value)||fabs(value)>(std::numeric_limits<float>::max)())return luaL_error(s,"Invalid native heal arguments");
 lua_pushboolean(s,((SRPGUnitStats*)r.object)->Heal((SRPGCommonStats*)c.object,float(value)));return 1;
}
int RegenMp(lua_State*s){auto&r=Get(s);double value=luaL_checknumber(s,2);if(r.kind!=Common||!std::isfinite(value)||fabs(value)>(std::numeric_limits<float>::max)())return luaL_error(s,"Invalid native MP regeneration");lua_pushboolean(s,((SRPGCommonStats*)r.object)->RegenerateMP(float(value)));return 1;}
int Access(lua_State*s,bool write){auto&r=Get(s);const char*k=luaL_checkstring(s,2);
#define F(name,member) if(strcmp(k,name)==0)return Field(s,p->member,write)
#define CHILD(name,member,type) if(!write&&strcmp(k,name)==0){PushRef(s,&p->member,type,r.scope);return 1;}
#define ARRAY(name,member,type) if(!write&&strcmp(k,name)==0){PushRef(s,p->member,type,r.scope,_countof(p->member));return 1;}
 if(!write&&strcmp(k,"addr")==0){lua_pushvalue(s,1);return 1;}
 if(r.kind==Perk&&!write){auto*p=(const SPDBPerk*)r.object;
 if(strcmp(k,"code")==0){lua_pushstring(s,p->szCode);return 1;}
 if(strcmp(k,"name")==0){lua_pushstring(s,p->szName);return 1;}
 if(strcmp(k,"cost")==0){lua_pushnumber(s,p->dwCost);return 1;}
 if(strcmp(k,"modifier")==0){lua_pushnumber(s,p->dwModifier);return 1;}
 }

 if((r.kind==Primary||r.kind==Secondary)&&!write){const char*names[]={"addBase","subBase","addCurr","subCurr","addMax","subMax","addRegen","subRegen"};for(int i=0;i<8;i++)if(strcmp(k,names[i])==0){lua_pushinteger(s,i);lua_pushcclosure(s,StatAdjust,1);return 1;}}
 if(r.kind==HitLocation){auto*p=(SRPGHitLocation*)r.object;F("type",hlt);F("crippleMask",dwCrippleMask);F("state",state);F("hp",fpHP);F("maxHp",fpMaxHP);F("hpWeight",fpHPWeight);F("vitality",fpVitality);if(!write&&strcmp(k,"layer")==0){lua_pushcfunction(s,LayerAt);return 1;}}
 if(r.kind==ArmorLayer){auto*p=(SRPGArmorLayer*)r.object;F("crippleMask",dwCrippleMask);F("state",state);ARRAY("absorbtion",fpAbsorbtions,Floats);ARRAY("resistance",fpResistances,Floats);}
 if(r.kind==Primary){auto*p=(SRPGPrimaryStat*)r.object;F("base",fpBaseValue);F("curr",fpEffectiveValue);}
 if(r.kind==Secondary){auto*p=(SRPGSecondaryStat*)r.object;F("curr",fpCurrent);F("max",fpMax);F("regen",fpRegenerate);}
 if(r.kind==Stats){auto*p=(SRPGUnitStats*)r.object;
 if(!write&&strcmp(k,"hitLocation")==0){lua_pushcfunction(s,HitAt);return 1;}
 if(!write){const char*names[]={"distributeHp","distributeHpProportional","collectHp"};for(int i=0;i<3;i++)if(strcmp(k,names[i])==0){lua_pushinteger(s,i);lua_pushcclosure(s,HitPoints,1);return 1;}}

 F("flags1",dwFlags);F("flags2",bFlags);F("experience",fpExperience);F("spentExperience",fSpentExp);F("visionArc",fpVisionArc);F("currEncumbrance",fpCurrEncumbrance);F("maxEncumbrance",fpMaxEncumbrance);F("locomotion",dwLocomotion);F("baseHp",fBaseHP);F("expPenalty",fExpPenalty);F("moveDamageModifier",fpMoveDamageModifier);F("actionsDamageModifier",fpActionDamageModifier);
 F("nawRange",fpNAWRange);F("nawToHit",fpNAWToHit);F("nawToHitRandom",fpNAWToHitRandom);F("nawParry",fpNAWParry);F("nawWeight",fpNAWWeight);F("nawDamageMin",fpNAWDamageMin);F("nawDamageRandom",fpNAWDamageRnd);F("nawActions",fpNAWActionPoints);
 CHILD("strength",psStrength,Primary);CHILD("dexterity",psDexterity,Primary);CHILD("intelligence",psIntelligence,Primary);CHILD("move",move,Primary);CHILD("actions",action,Primary);
 ARRAY("speeds",fpSpeeds,Floats);ARRAY("manualSkills",cManualSkills,Bytes);ARRAY("autoSkills",cAutoSkills,Bytes);ARRAY("perks",cPerks,Bytes);ARRAY("nawAttacks",fpNAWAttacks,Floats);
 if(!write&&strcmp(k,"isServer")==0){lua_pushboolean(s,1);return 1;}
 if(!write&&strcmp(k,"nawType")==0){lua_pushnumber(s,p->rpgNAWWeaponType);return 1;}
 if(!write&&strcmp(k,"nawDamageMax")==0){lua_pushnumber(s,p->fpNAWDamageMin+p->fpNAWDamageRnd);return 1;}
 if(!write&&strcmp(k,"perkModifier")==0){lua_pushcfunction(s,PerkModifier);return 1;}
 if(!write&&(strcmp(k,"sense")==0||strcmp(k,"percept")==0)){lua_pushinteger(s,strcmp(k,"sense")!=0);lua_pushcclosure(s,PrimaryAt,1);return 1;}
 }
 if(!write&&r.kind==Stats&&strcmp(k,"heal")==0){lua_pushcfunction(s,Heal);return 1;}
 if(!write&&r.kind==Common&&strcmp(k,"regenMp")==0){lua_pushcfunction(s,RegenMp);return 1;}
 if(r.kind==Common){auto*p=(SRPGCommonStats*)r.object;F("flags",dwFlags);F("regenCounter",nRegenerateCounter);CHILD("hp",ssHP,Secondary);CHILD("mp",ssMP,Secondary);if(!write&&strcmp(k,"absorbtion")==0){lua_pushinteger(s,2);lua_pushcclosure(s,PrimaryAt,1);return 1;}}
 if((r.kind==Bytes||r.kind==ReadOnlyBytes||r.kind==Floats||r.kind==ReadOnlyFloats)&&!write){if(strcmp(k,"value")==0){lua_pushcfunction(s,ArrayValue);return 1;}if(strcmp(k,"set")==0){lua_pushcfunction(s,ArraySet);return 1;}if(strcmp(k,"size")==0){lua_pushinteger(s,r.count);return 1;}}
 if(r.kind==ClientStats&&!write){auto*p=(const SRPGUserUnitStats*)r.object;
#define R(name,member) if(strcmp(k,name)==0){lua_pushnumber(s,p->member);return 1;}
 R("experience",dwExperience);R("freeExperience",nFreeExp);R("strength",cStrength);R("dexterity",cDexterity);R("intelligence",cIntelligence);R("locomotion",cLocomotion);R("movePoints",cMove);R("actions",cAction);R("encumbrance",wEncumbrance);R("attackerProto",wAttackerPrototype);
#define RO_ARRAY(name,member) if(strcmp(k,name)==0){PushRef(s,const_cast<BYTE*>(p->member),ReadOnlyBytes,r.scope,_countof(p->member));return 1;}
 RO_ARRAY("speeds",cSpeeds);RO_ARRAY("senses",cSenses);RO_ARRAY("percepts",cPercepts);RO_ARRAY("manualSkills",cManualSkills);RO_ARRAY("autoSkills",cAutoSkills);RO_ARRAY("perks",cPerks);
#undef RO_ARRAY
 if(strcmp(k,"isServer")==0){lua_pushboolean(s,0);return 1;}
 if(strcmp(k,"perkModifier")==0){lua_pushcfunction(s,PerkModifier);return 1;}
#undef R
 }
 if(r.kind==Attack){auto*p=(SAttackParams*)r.object;F("weaponType",rpgWeaponType);F("damageType",rpgAttackType);F("weaponWeight",fWeaponWeight);F("damageMin",fDamageMin);F("damageMax",fDamageMax);F("toHitRandom",fToHitRandom);if(strcmp(k,"attack")==0||strcmp(k,"defense")==0){int&v=strcmp(k,"attack")==0?p->nAttack:p->nDefense;if(write){double n=luaL_checknumber(s,3);if(!std::isfinite(n)||n<INT_MIN||n>INT_MAX)return luaL_error(s,"Attack integer out of range");v=int(n);return 0;}lua_pushinteger(s,v);return 1;}}
 if(r.kind==Spell&&!write){auto*p=(const CSpell*)r.object;
#define S(name,expr) if(strcmp(k,name)==0){lua_pushnumber(s,expr);return 1;}
 S("id",p->GetID());S("typeId",p->GetTypeID());S("subtypeId",p->GetSubtypeID()>>16);S("speed",p->GetSpeed());S("range",p->GetRange());S("numTargets",p->GetNumTargets());S("resolve",p->GetResolveFlags());S("manaCost",p->GetManaCost());S("slots",p->GetNumSlots());S("price",p->GetPrice());S("actions",p->GetActionPoints());S("complexity",p->GetComplexity());
#undef S
 if(strcmp(k,"runeIds")==0){PushRef(s,const_cast<char*>(p->GetRunes()),ReadOnlyBytes,r.scope,8);return 1;}
 if(strcmp(k,"effect")==0){PushRef(s,const_cast<CSpellEffect*>(&p->GetEffect()),SpellEffect,r.scope);return 1;}
 }
 if(r.kind==SpellEffect&&!write){auto*p=(const CSpellEffect*)r.object;
#define E(name,member) if(strcmp(k,name)==0){lua_pushnumber(s,p->member);return 1;}
 E("id",nSpellID);E("strength",nVisStrength);E("duration",nDuration);E("power",fpPower);E("radius",fpRadius);
#undef E
 if(strcmp(k,"isQuiet")==0){lua_pushboolean(s,p->bQuiet);return 1;}
 }
 if(r.kind==Unit&&!write){auto*p=(CUnit*)r.object;
 if(strcmp(k,"currWeapon")==0){if(!p->GetUnitSpecific())return luaL_error(s,"Unit equipment requires a native unit-specific component");PushRef(s,r.scope->Weapon(*p,int(p->GetCurrentWeaponSlotIndex())),Item,r.scope);return 1;}
 if(strcmp(k,"weapon")==0){lua_pushcfunction(s,UnitWeapon);return 1;}
 if(strcmp(k,"quickItem")==0){lua_pushcfunction(s,UnitQuick);return 1;}
 if(strcmp(k,"armor")==0){lua_pushcfunction(s,UnitArmor);return 1;}
 if(strcmp(k,"spellEffect")==0){lua_pushcfunction(s,UnitEffect);return 1;}

#define U(name,expr) if(strcmp(k,name)==0){lua_pushnumber(s,expr);return 1;}
 U("itemsWeight",p->GetItemsWeight());U("id",p->GetID());U("playerNum",p->GetPlayerNum());U("pose",p->GetCurrentPose());U("currWeaponIndex",p->GetCurrentWeaponSlotIndex());
 if(strcmp(k,"name")==0){lua_pushstring(s,p->GetName());return 1;}
 if(strcmp(k,"isPlayer")==0){lua_pushboolean(s,p->GetID()>=1000000000u&&p->GetID()<2000000000u);return 1;}
 if(strcmp(k,"isDead")==0){lua_pushboolean(s,p->IsDead());return 1;}if(strcmp(k,"isMoving")==0){lua_pushboolean(s,p->IsMoving());return 1;}
#undef U
 }
 if(r.kind==Item){auto*p=(CItem*)r.object;
 if(strcmp(k,"mp")==0){auto it=r.scope->itemMana.find(p);float value=it==r.scope->itemMana.end()?p->GetMP():it->second;int result=Field(s,value,write);if(write)r.scope->itemMana[p]=value;return result;}
 if(!write&&strcmp(k,"spell")==0){Push(s,p->GetSpell(0),r.scope);return 1;}
 if(strcmp(k,"regenRate")==0){auto it=r.scope->itemRegeneration.find(p);float value=it==r.scope->itemRegeneration.end()?p->GetManaRegenRate():it->second;int result=Field(s,value,write);if(write)r.scope->itemRegeneration[p]=value;return result;}
 if(!write&&strcmp(k,"prototype")==0){const auto*shape=p->GetShapeEntry();if(!shape)return luaL_error(s,"Item prototype missing");lua_newtable(s);lua_pushstring(s,shape->szName);lua_setfield(s,-2,"name");lua_pushinteger(s,shape->dwTypeID);lua_setfield(s,-2,"typeId");if(p->GetTypeID()==ITID_ITEM_WEAPON){const auto*w=static_cast<const SIDBWeaponShape*>(shape);lua_pushnumber(s,w->fAttackBonus);lua_setfield(s,-2,"attackBonus");lua_pushnumber(s,w->fDefenceBonus);lua_setfield(s,-2,"defenceBonus");}return 1;}
 if(strcmp(k,"count")==0){auto it=r.scope->itemCounts.find(p);int v=it==r.scope->itemCounts.end()?p->GetCount():it->second;int result=Field(s,v,write);if(write){if(v<0)return luaL_error(s,"Negative item count");r.scope->itemCounts[p]=v;}return result;}
 if(write){}
 else {
#define I(name,expr) if(strcmp(k,name)==0){lua_pushnumber(s,expr);return 1;}
 I("id",p->GetID());I("materialId",p->GetMaterial());I("prototypeId",p->GetShape());I("durability",p->GetHP());I("durabilityMax",p->GetMaxHP());I("mpMax",p->GetMaxMP());I("weight",p->GetWeight());I("price",p->GetPrice());I("size",p->GetSize());I("numRunes",p->GetSpellSlots());I("count",p->GetCount());
#undef I
 }}
#undef F
#undef CHILD
#undef ARRAY
 return luaL_error(s,"Unported engine property '%s' (kind %d, write %d)",k,int(r.kind),int(write));
}
int Index(lua_State*s){return Access(s,false);}int NewIndex(lua_State*s){return Access(s,true);}
int Factory(lua_State*s){int arg=lua_istable(s,1)?2:1;auto&r=Get(s,arg);int kind=lua_tointeger(s,lua_upvalueindex(1));if(int(r.kind)!=kind)return luaL_error(s,"Wrong engine object kind");lua_pushvalue(s,arg);return 1;}
void Constructor(lua_State*s,Kind kind){
 lua_newtable(s);lua_pushinteger(s,kind);lua_pushcclosure(s,Factory,1);lua_setfield(s,-2,"new");
 lua_newtable(s);lua_pushinteger(s,kind);lua_pushcclosure(s,Factory,1);lua_setfield(s,-2,"__call");lua_setmetatable(s,-2);
}
int PerkIndex(lua_State*s){auto&r=Get(s);if(r.kind!=Perk)return luaL_error(s,"Expected native perk record");lua_pushinteger(s,r.count);return 1;}

int IntegerMath(lua_State*s){
 auto a=luaL_checkinteger(s,1),b=luaL_checkinteger(s,2);
 if(b==0||(a==(std::numeric_limits<lua_Integer>::lowest)()&&b==-1))return luaL_error(s,"Invalid integer division");
 lua_pushinteger(s,lua_toboolean(s,lua_upvalueindex(1))?a%b:a/b);return 1;
}
int UnitItemsWeight(lua_State*s){auto&r=Get(s);if(r.kind!=Unit)return luaL_error(s,"Expected unit");auto*u=(CUnit*)r.object;float weight=u->GetItemsWeight();if(u->GetUnitSpecific()&&(u->GetUnitSpecific()->WhoAmI()!=HPC_UNITCLIENTSPECIFIC||u->GetOwner()))for(DWORD i=0;i<u->GetSpellSlotCount();i++)if(u->GetQSpellItem(i))weight+=1;lua_pushnumber(s,weight);return 1;}
int ClientHlsPercent(lua_State*s){auto&r=Get(s);if(r.kind!=Unit)return luaL_error(s,"Expected unit");int part=CheckedInteger(s,2);float result=1;for(const auto&h:((CUnit*)r.object)->GetUserUnitStats()->hls)if(h.hlt==part&&h.wMaxHP>0)result=(std::min)(result,(std::max)(.0001f,float(h.wHP)+.0001f)/float(h.wMaxHP));lua_pushnumber(s,result);return 1;}
int Difficulty(lua_State*s){lua_pushinteger(s,theSettings.nDifficultyLevel);return 1;}
int Console(lua_State*s){if(lua_isstring(s,1))OutputDebugStringA(lua_tostring(s,1));return 0;}
int RawRejected(lua_State*s){return luaL_error(s,"Original-EXE memory accessor unavailable; use typed engine object");}
}
void Push(lua_State*s,const CSpell*p,const std::shared_ptr<Scope>&scope){PushRef(s,const_cast<CSpell*>(p),Spell,scope);}
void Push(lua_State*s,SAttackParams*p,const std::shared_ptr<Scope>&scope){PushRef(s,p,Attack,scope);}
void PushFloats(lua_State*s,float*p,int count,const std::shared_ptr<Scope>&scope){PushRef(s,p,Floats,scope,count);}
void Push(lua_State*s,const SRPGUserUnitStats*p,const std::shared_ptr<Scope>&scope){PushRef(s,const_cast<SRPGUserUnitStats*>(p),ClientStats,scope);}
void Register(lua_State*s){lua_pushcfunction(s,ArmorMaterialName);lua_setglobal(s,"GetEquippedArmorMaterialName");for(int mod=0;mod<2;mod++){lua_pushboolean(s,mod);lua_pushcclosure(s,IntegerMath,1);lua_setglobal(s,mod?"IMod":"IDiv");}lua_pushcfunction(s,Difficulty);lua_setglobal(s,"GetDifficulty");luaL_newmetatable(s,meta);lua_pushcfunction(s,Index);lua_setfield(s,-2,"__index");lua_pushcfunction(s,NewIndex);lua_setfield(s,-2,"__newindex");lua_pushcfunction(s,GC);lua_setfield(s,-2,"__gc");lua_pushliteral(s,"engine object");lua_setfield(s,-2,"__metatable");lua_pop(s,1);lua_pushcfunction(s,Console);lua_setglobal(s,"ConsoleWriteLine");
 const char* types[]={"Str","Float","Double","UInt32","UInt16","UInt8","Int32","Int16","Int8"};for(auto t:types)for(auto suffix:{"FromMem","ToMem"}){std::string name=std::string(t)+suffix;lua_pushcfunction(s,RawRejected);lua_setglobal(s,name.c_str());}}
void InstallFactories(lua_State*s){
  lua_pushcfunction(s,ClientHlsPercent);lua_setglobal(s,"GetHlsPercent");lua_pushcfunction(s,UnitItemsWeight);lua_setglobal(s,"GetUnitItemsWeight");
 lua_pushcfunction(s,AddPerkRecord);lua_setglobal(s,"AddPerkRecord");lua_pushcfunction(s,LeftHand);lua_setglobal(s,"GetLeftHand");
 lua_newtable(s);lua_pushcfunction(s,EffectArray);lua_setfield(s,-2,"new");lua_newtable(s);lua_pushcfunction(s,EffectArray);lua_setfield(s,-2,"__call");lua_setmetatable(s,-2);lua_setglobal(s,"Array");struct Entry{const char*name;Kind kind;};const Entry entries[]={{"Spell",Spell},{"SpellEffect",SpellEffect},{"AttackParams",Attack},{"ItemSpell",Item},{"UnitClientStats",ClientStats},{"UnitServerStats",Stats},{"UnitServerCommonStats",Common},{"UnitServer",Unit},{"UnitClient",Unit},{"BaseUnit",Unit},{"BaseItem",Item},{"ItemWeapon",Item},{"ItemArmor",Item},{"ItemQuick",Item},{"PrimaryStat",Primary},{"SecondaryStat",Secondary},{"HitLocation",HitLocation},{"ArmorLayer",ArmorLayer}};for(auto&e:entries){Constructor(s,e.kind);lua_setglobal(s,e.name);}
 lua_getglobal(s,"PerksDatabase");Constructor(s,Perk);lua_setfield(s,-2,"PerksTableRecord");lua_getfield(s,-1,"PerksTable");lua_pushcfunction(s,PerkIndex);lua_setfield(s,-2,"getRecordIndex");lua_pop(s,2);
 lua_getglobal(s,"ItemsDatabase");const char*names[]={"MaterialsTable","WeaponsTable","QuicksTable","ArmorsTable"};for(int i=0;i<4;i++){lua_getfield(s,-1,names[i]);lua_pushinteger(s,i);lua_pushcclosure(s,DatabaseName,1);lua_setfield(s,-2,"getRecord");lua_pop(s,1);}lua_pop(s,1);

}
CItemWeapon* Scope::Weapon(const CUnit&unit,int slot){
 if(slot<0||slot>=int(QI_MAX_WEAPONS))return nullptr;auto*specific=const_cast<CUnit&>(unit).GetUnitSpecific();if(!specific)throw std::runtime_error("Weapon view needs unit-specific owner");
 if(specific->WhoAmI()!=HPC_UNITCLIENTSPECIFIC||unit.GetOwner())return unit.GetQWeapon(slot);
 auto key=std::make_pair(&unit,slot);auto it=weaponViews.find(key);if(it!=weaponViews.end())return it->second.get();
 DWORD subtype=static_cast<CUnitClientSpecific*>(specific)->GetWeaponSubtype(slot);if(subtype==DWORD(-1))return nullptr;
 if(CItem::GetShape(subtype)>=unsigned(theItemDatabase.weapons.GetNumEntries())||CItem::GetMaterial(subtype)>=unsigned(theItemDatabase.materials.GetNumEntries()))throw std::runtime_error("Client weapon subtype outside database");
 auto view=std::make_shared<CItemWeapon>();view->SetTypeID(ITID_ITEM_WEAPON);view->SetSubtypeID(subtype);auto*result=view.get();weaponViews.emplace(key,std::move(view));return result;
}
int Scope::LeftHand(const CUnit&unit){return ModStats::LeftHand(unit,[&](int slot){return Weapon(unit,slot);});}
void Scope::CommitItems(){for(const auto&item:itemRegeneration)item.first->SetManaRegenRate(item.second);for(const auto&item:itemMana)item.first->SetMP(item.second);for(const auto&item:itemCounts)item.first->SetCount(item.second);itemRegeneration.clear();itemMana.clear();itemCounts.clear();}
void PushEffects(lua_State*s,float*p,int count,const std::shared_ptr<Scope>&scope){PushRef(s,p,ReadOnlyFloats,scope,count);}
void Push(lua_State*s,const SPDBPerk*p,const std::shared_ptr<Scope>&scope){int index=thePerkDatabase.GetPerkIndex(p);if(index<0)throw std::runtime_error("Perk does not belong to the loaded engine database");PushRef(s,const_cast<SPDBPerk*>(p),Perk,scope,index);}
void Push(lua_State*s,SRPGUnitStats*p,const std::shared_ptr<Scope>&a){PushRef(s,p,Stats,a);}void Push(lua_State*s,SRPGCommonStats*p,const std::shared_ptr<Scope>&a){PushRef(s,p,Common,a);}void Push(lua_State*s,const CUnit*p,const std::shared_ptr<Scope>&a){/* Unit bindings are read-only; retain const input through the opaque storage. */PushRef(s,const_cast<CUnit*>(p),Unit,a);}void Push(lua_State*s,CItem*p,const std::shared_ptr<Scope>&a){PushRef(s,p,Item,a);}
}
namespace ModLua {
nlohmann::json CheckNativeBridge(lua_State*s){
 int top=lua_gettop(s);auto scope=std::make_shared<Scope>();
 struct End {lua_State*s;int top;std::shared_ptr<Scope>scope;~End(){scope->active=false;lua_settop(s,top);}} end{s,top,scope};
 SRPGUnitStats stats={};stats.fpExperience=2000;stats.psStrength={25,50};stats.psDexterity={25,25};stats.fpMaxEncumbrance=100;stats.fpCurrEncumbrance=50;stats.move={10,10};stats.fpMoveDamageModifier=.5f;
 SRPGCommonStats common={};common.ssMP={.1f,100,0};CUnit unit;CItemWeapon item;item.SetCount(3);
 Push(s,&stats,scope);lua_setglobal(s,"__native_stats");Push(s,&common,scope);lua_setglobal(s,"__native_common");Push(s,&unit,scope);lua_setglobal(s,"__native_unit");Push(s,&item,scope);lua_setglobal(s,"__native_item");
 auto run=[&](const char*code,int results){if(luaL_loadstring(s,code)||lua_pcall(s,0,results,0)){std::string error=lua_tostring(s,-1);throw std::runtime_error(error);}};
 run("return CalcBaseHealth(__native_stats)",1);double hp=lua_tonumber(s,-1);lua_pop(s,1);double expected=(30*pow(1.10,(log(2000.0/500)/log(1.61)))-6)*2;if(fabs(hp-expected)>0.0001)throw std::runtime_error("Typed stats HP callback mismatch: actual="+std::to_string(hp)+" expected="+std::to_string(expected));
 run("return CalcBaseMana(__native_stats)",1);double mp=lua_tonumber(s,-1);lua_pop(s,1);double expectedMp=((30*pow(1.10,(log(4.0)/log(1.61)))-42.9)*1.2+47);if(fabs(mp-expectedMp)>.0001)throw std::runtime_error("Typed stats MP callback mismatch");
 run("CalcRunningManaCost(__native_stats,__native_unit,__native_common)",0);
 if(fabs(common.ssMP.fpCurrent-(.1-pow(100,.7)*.00715))>.0001||fabs(stats.move.fpEffectiveValue-1)>.0001)throw std::runtime_error("Native mana/movement writeback failed");
 run("assert(IDiv(-7,3)==-2 and IMod(-7,3)==-1); assert(not pcall(IDiv,1,0)); assert(UnitServerStats:new(__native_stats)==__native_stats); assert(UnitServerStats.new(__native_stats)==__native_stats); assert(not pcall(function() UnitServer:new(__native_stats) end)); __native_stats.manualSkills:set(0,7); __native_item.count=4; __native_item.mp=12; assert(__native_item.count==4); assert(not pcall(function() __native_stats.perks:set(9999,1) end)); assert(not pcall(function() return UInt32FromMem(0x791ee8) end)); assert(not pcall(function() return __native_unit.addr+4 end)); assert(not pcall(function() UnitServer(__native_stats) end))",0);
 if(item.GetMP()==12||item.GetCount()!=3)throw std::runtime_error("Item mutation leaked before commit");scope->CommitItems();
 if(stats.cManualSkills[0]!=7||item.GetCount()!=4||item.GetMP()!=12)throw std::runtime_error("Typed field writeback failed");
 float effects[53]={};effects[52]=7;PushEffects(s,effects,53,scope);lua_setglobal(s,"__native_effects");
 run("assert(Array(__native_effects,Memory.Type.Float,53,true):value(52)==7); assert(not pcall(function() Array(__native_effects,Memory.Type.Float,52,true) end)); assert(not pcall(function() Array(__native_effects,Memory.Type.UInt8,53,true) end)); assert(not pcall(function() __native_effects:set(0,9) end)); assert(not pcall(function() return __native_effects:value(53) end))",0);
 SRPGUserUnitStats client={};client.dwExperience=1234;client.nFreeExp=25;client.cStrength=26;client.cDexterity=27;client.cIntelligence=28;client.cAutoSkills[7]=77;client.cManualSkills[2]=12;
 CSpell spell;spell.SetID(99);SAttackParams attack={};attack.fDamageMin=2;attack.fDamageMax=4;
 Push(s,&client,scope);lua_setglobal(s,"__native_client");Push(s,&spell,scope);lua_setglobal(s,"__native_spell");Push(s,&attack,scope);lua_setglobal(s,"__native_attack");
 run("assert(not __native_client.isServer and __native_client.experience==1234 and __native_client.freeExperience==25 and __native_client.strength==26 and __native_client.dexterity==27 and __native_client.intelligence==28); assert(__native_client.autoSkills:value(7)==77 and __native_client.manualSkills:value(2)==12); assert(not pcall(function() __native_client.strength=99 end)); assert(not pcall(function() __native_client.autoSkills:set(0,99) end)); assert(Spell(__native_spell)==__native_spell and SpellEffect(__native_spell.effect).power==0); assert(__native_spell.id==99 and __native_spell.complexity==1 and __native_spell.runeIds:value(7)==0); assert(not pcall(function() __native_spell.runeIds:set(0,1) end)); assert(not pcall(function() __native_spell.effect.power=9 end)); assert(not pcall(function() return __native_spell.runeIds:value(8) end)); assert(not pcall(function() return __native_client.autoSkills:value(0.5) end)); assert(not pcall(function() __native_stats.manualSkills:set(0,0.5) end)); assert(not pcall(function() __native_item.count=-1 end)); assert(not pcall(function() Spell(__native_stats) end)); AttackParams(__native_attack).damageMax=8; __native_attack.attack=17; __native_attack.attack=1.5; assert(__native_attack.attack==1); __native_attack.attack=17; assert(not pcall(function() __native_attack.attack=0/0 end)); __retained_effect=__native_spell.effect; __retained_array=__native_client.perks",0);
 if(attack.fDamageMax!=8||attack.nAttack!=17||client.cStrength!=26||client.cAutoSkills[0]!=0)throw std::runtime_error("Foundation typed object contract failed");
 // Failed callbacks discard staged inventory writes.
 {auto failed=std::make_shared<Scope>();Push(s,&item,failed);lua_setglobal(s,"__failed_item");run("assert(not pcall(function() __failed_item.count=9; __failed_item.mp=99; error('rollback') end))",0);failed->active=false;lua_pushnil(s);lua_setglobal(s,"__failed_item");}
 if(item.GetCount()!=4||item.GetMP()!=12)throw std::runtime_error("Failed callback changed inventory");
 scope->active=false;run("assert(not pcall(function() return __native_stats.experience end)); assert(not pcall(function() return __native_effects:value(0) end)); __native_effects=nil; assert(not pcall(function() return __retained_effect.power end)); assert(not pcall(function() return __retained_array:value(0) end)); __retained_effect=nil; __retained_array=nil",0);
 const char*globals[]={"__native_stats","__native_common","__native_unit","__native_item","__native_client","__native_spell","__native_attack"};for(auto g:globals){lua_pushnil(s);lua_setglobal(s,g);}
 return {{"foundationTypedContracts",true},{"nativeStatsHealth",hp},{"nativeStatsMana",mp},{"manaAndMoveWriteback",true},{"typedItemWriteback",true},{"arrayBoundsRejected",true},{"rawAddressRejected",true},{"wrongKindRejected",true},{"constructorForms",true},{"expiredObjectRejected",true}};
}
}
