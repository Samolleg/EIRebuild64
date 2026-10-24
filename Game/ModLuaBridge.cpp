#include "StdAfx.h"
#include "GameJournal.h"
#include "../shared/Common.h"
#include "UnitClientSpecific.h"
#include "ModLuaBridge.h"
#include "SoundManager.h"
#include "Console.h"
#include "WorldClient.h"
#include "WorldServer.h"
#include "Player.h"
#include "ModStatRules.h"
#include <lua.hpp>
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
enum Kind {Stats,Common,Unit,Item,Primary,Secondary,Bytes,Floats,Perk,HitLocation,ArmorLayer,ReadOnlyFloats,ClientStats,ReadOnlyBytes,Spell,SpellEffect,Attack,DbRecord,DbAbsorbtion,DbArray};
struct Ref {
 void *object;Kind kind;int count;std::shared_ptr<Scope> scope;
 std::weak_ptr<const PerkHostLifetime> perkHost;
 std::weak_ptr<const CPerkDatabase::RecordIdentity> perkDatabase;
 std::weak_ptr<const CDatabaseRecordIdentity> itemDatabase;
 int itemTable=-1;
 int itemView=0;
};
const char *meta="EIRebuild.TypedObject";
const char *perkHostMeta="EIRebuild.PerkHost";
const char perkHostKey=0;
const char unitRefsKey=0;
constexpr ObjectId maxUnitCacheKey=0xffffffffULL;
static_assert(sizeof(lua_Integer)==8,"The host requires LuaJIT x64 public headers");
int PushUnitCacheKey(lua_State*s,const CUnit&unit){
 const auto id=unit.GetRTID();
 // The installed equipment cache accepts only uint32 numeric keys. Never wrap
 // or recycle ObjectId here: that would alias another live/cache entry.
 if(!id||id>maxUnitCacheKey)return luaL_error(s,"Unit ObjectId exceeds the Lua cache key range (1..4294967295)");
 lua_pushnumber(s,static_cast<lua_Number>(id));return 1;
}
struct PerkHostBinding {std::weak_ptr<const PerkHostLifetime> lifetime;};
int PerkHostGC(lua_State*s){((PerkHostBinding*)luaL_checkudata(s,1,perkHostMeta))->~PerkHostBinding();return 0;}
PerkHostBinding *FindPerkHost(lua_State*s){
 const int top=lua_gettop(s);lua_pushlightuserdata(s,const_cast<char*>(&perkHostKey));lua_rawget(s,LUA_REGISTRYINDEX);
 PerkHostBinding *result=nullptr;
 if(lua_type(s,-1)==LUA_TUSERDATA&&lua_objlen(s,-1)==sizeof(PerkHostBinding)&&lua_getmetatable(s,-1)){
  luaL_getmetatable(s,perkHostMeta);const bool matches=lua_rawequal(s,-1,-2)!=0;lua_pop(s,2);
  if(matches)result=(PerkHostBinding*)lua_touserdata(s,-1);
 }
 lua_settop(s,top);return result&&!result->lifetime.expired()?result:nullptr;
}
// Keep factory selectors4/5 and the four existing table IDs stable.
enum ItemTable {DbMaterial,DbWeapon,DbQuick,DbArmor,DbQuest=6,DbLoot};
enum ItemView {WeaponStats=1,WeaponManual,WeaponAuto,WeaponDamage,ArmorManual,ArmorAuto,
               ArmorMainDistribution,ArmorSecondDistribution,MaterialAbsorbtion,QuickManual,QuickAuto};
bool ItemRecordAlive(const Ref&r){
 switch(r.itemTable){
 case DbMaterial:return theItemDatabase.materials.OwnsRecordIdentity(r.itemDatabase)&&theItemDatabase.materials.GetEntry(r.count);
 case DbWeapon:return theItemDatabase.weapons.OwnsRecordIdentity(r.itemDatabase)&&theItemDatabase.weapons.GetEntry(r.count);
 case DbQuick:return theItemDatabase.quickItems.OwnsRecordIdentity(r.itemDatabase)&&theItemDatabase.quickItems.GetEntry(r.count);
 case DbArmor:return theItemDatabase.armors.OwnsRecordIdentity(r.itemDatabase)&&theItemDatabase.armors.GetEntry(r.count);
 case DbQuest:return theItemDatabase.questItems.OwnsRecordIdentity(r.itemDatabase)&&theItemDatabase.questItems.GetEntry(r.count);
 case DbLoot:return theItemDatabase.lootItems.OwnsRecordIdentity(r.itemDatabase)&&theItemDatabase.lootItems.GetEntry(r.count);
 default:return false;
 }
}
Ref &Get(lua_State*s,int n=1){
 if(lua_type(s,n)==LUA_TNUMBER){
  const double id=lua_tonumber(s,n);
  if(!std::isfinite(id)||floor(id)!=id||id<1||id>maxUnitCacheKey)
   luaL_error(s,"Expected an exact unit ObjectId cache key");
  // Only units supplied to this callback can be resolved. Numbers are never
  // converted to pointers, and a retained key grants no access after the call.
  lua_pushlightuserdata(s,const_cast<char*>(&unitRefsKey));lua_rawget(s,LUA_REGISTRYINDEX);
  if(!lua_istable(s,-1))luaL_error(s,"Unit key outside an active engine callback");
  lua_pushnumber(s,id);lua_rawget(s,-2);lua_remove(s,-2);
  if(lua_isnil(s,-1))luaL_error(s,"Unit key is not bound in this engine callback");
  lua_replace(s,n);
 }
 auto*p=(Ref*)luaL_checkudata(s,n,meta);
 if(p->scope&&!p->scope->active)luaL_error(s,"Expired engine callback object");
 if(p->kind==Perk){
  const auto*host=FindPerkHost(s);
  if(!host||p->perkHost.expired()||p->perkHost.owner_before(host->lifetime)||host->lifetime.owner_before(p->perkHost)||
     !thePerkDatabase.OwnsRecordIdentity(p->perkDatabase)||!thePerkDatabase.GetPerk(p->count))
   luaL_error(s,"Expired perk host or database record");
 }else if(p->kind==DbRecord||p->kind==DbAbsorbtion||p->kind==DbArray){
  const auto*host=FindPerkHost(s);
  if(!host||p->perkHost.expired()||p->perkHost.owner_before(host->lifetime)||host->lifetime.owner_before(p->perkHost)||!ItemRecordAlive(*p))
   luaL_error(s,"Expired item host or database record");
 }else if(!p->scope)luaL_error(s,"Missing engine callback scope");
 return *p;
}
void PushPerkRef(lua_State*s,int index,const std::shared_ptr<Scope>&scope){
 const auto*host=FindPerkHost(s);if(!host)throw std::runtime_error("Perk database requires a live Lua package owner");
 // No record or host address is retained. Resolve the real database entry on each access.
 new(lua_newuserdata(s,sizeof(Ref))) Ref{nullptr,Perk,index,scope,host->lifetime,thePerkDatabase.GetRecordIdentity()};
 luaL_getmetatable(s,meta);lua_setmetatable(s,-2);
}
void InstallPerkConstructor(lua_State*s);
void PushRef(lua_State*s,void*p,Kind k,const std::shared_ptr<Scope>&scope,int count=0){
 if(!p){lua_pushnil(s);return;}
 new(lua_newuserdata(s,sizeof(Ref))) Ref{p,k,count,scope};
 luaL_getmetatable(s,meta);lua_setmetatable(s,-2);
 if(k==Unit){
  const auto id=static_cast<CUnit*>(p)->GetRTID();const int ref=lua_gettop(s);
  lua_pushlightuserdata(s,const_cast<char*>(&unitRefsKey));lua_rawget(s,LUA_REGISTRYINDEX);
  if(lua_istable(s,-1)&&id&&id<=maxUnitCacheKey){
   lua_pushnumber(s,static_cast<lua_Number>(id));lua_pushvalue(s,ref);lua_rawset(s,-3);
  }
  lua_pop(s,1);
 }
}
int GC(lua_State*s){((Ref*)luaL_checkudata(s,1,meta))->~Ref();return 0;}
template<class T>int Field(lua_State*s,T&v,bool write){if constexpr(std::is_enum<T>::value){auto n=static_cast<typename std::underlying_type<T>::type>(v);int result=Field(s,n,write);if(write)v=static_cast<T>(n);return result;}else {if(write){double n=luaL_checknumber(s,3);if(!std::isfinite(n)||n<(std::numeric_limits<T>::lowest)()||n>(std::numeric_limits<T>::max)()||(std::is_integral<T>::value&&floor(n)!=n)){char error[192];snprintf(error,sizeof(error),"Engine field out of range: field=%s value=%.17g",lua_tostring(s,2)?lua_tostring(s,2):"<index>",n);return luaL_error(s,"%s",error);}v=static_cast<T>(n);return 0;}lua_pushnumber(s,v);return 1;}}
int CheckedInteger(lua_State*s,int index){double n=luaL_checknumber(s,index);if(!std::isfinite(n)||floor(n)!=n||n<(std::numeric_limits<int>::min)()||n>(std::numeric_limits<int>::max)())return luaL_error(s,"Expected exact engine integer");return int(n);}
// Slots use -1 for absence. Check before converting to the engine's uint32
// index, so a GC64 integer cannot wrap into a different valid slot.
DWORD CheckedSlot(lua_State*s,int index){
 const double n=luaL_checknumber(s,index);
 if(n==-1)return DWORD(-1);
 if(!std::isfinite(n)||floor(n)!=n||n<0||n>(std::numeric_limits<DWORD>::max)())
  luaL_error(s,"Expected a uint32 slot or -1");
 return static_cast<DWORD>(n);
}
struct ItemArraySpan {const void*data;int size;int memoryType;};
ItemArraySpan ItemArray(const Ref&r){
 if(r.itemTable==DbWeapon){const auto*p=theItemDatabase.weapons.GetEntry(r.count);
  switch(r.itemView){
  case WeaponStats:return {p->dwStatModifiers,_countof(p->dwStatModifiers),2};
  case WeaponManual:return {p->cManualSkillModifier,_countof(p->cManualSkillModifier),0};
  case WeaponAuto:return {p->cAutoSkillModifier,_countof(p->cAutoSkillModifier),0};
  case WeaponDamage:return {p->fpDamageDistributions,_countof(p->fpDamageDistributions),6};
  }
 }else if(r.itemTable==DbArmor){const auto*p=theItemDatabase.armors.GetEntry(r.count);
  switch(r.itemView){
  case ArmorManual:return {p->cManualSkillModifier,_countof(p->cManualSkillModifier),0};
  case ArmorAuto:return {p->cAutoSkillModifier,_countof(p->cAutoSkillModifier),0};
  case ArmorMainDistribution:return {p->main.fpDistributions,_countof(p->main.fpDistributions),6};
  case ArmorSecondDistribution:return {p->second.fpDistributions,_countof(p->second.fpDistributions),6};
  }
 }else if(r.itemTable==DbMaterial&&r.itemView==MaterialAbsorbtion){const auto*p=theItemDatabase.materials.GetEntry(r.count);
  return {p->fpAbsorbtionMultipliers,_countof(p->fpAbsorbtionMultipliers),6};
 }else if(r.itemTable==DbQuick){const auto*p=theItemDatabase.quickItems.GetEntry(r.count);
  if(r.itemView==QuickManual)return {p->cManualSkillModifier,_countof(p->cManualSkillModifier),0};
  if(r.itemView==QuickAuto)return {p->cAutoSkillModifier,_countof(p->cAutoSkillModifier),0};
 }
 return {nullptr,0,-1};
}
int ArrayValue(lua_State*s){auto&r=Get(s);
 if(r.kind==DbArray){const auto span=ItemArray(r);const int index=CheckedInteger(s,2);
  if(!span.data||index<0||index>=span.size)return luaL_error(s,"Item array index out of range");
  if(span.memoryType==0)lua_pushnumber(s,static_cast<const BYTE*>(span.data)[index]);
  else if(span.memoryType==2)lua_pushnumber(s,static_cast<const DWORD*>(span.data)[index]);
  else lua_pushnumber(s,static_cast<const float*>(span.data)[index]);return 1;
 }int i=CheckedInteger(s,2);if(i<0||i>=r.count)return luaL_error(s,"Engine array index out of range");if(r.kind==Bytes||r.kind==ReadOnlyBytes)lua_pushnumber(s,((BYTE*)r.object)[i]);else if(r.kind==Floats||r.kind==ReadOnlyFloats)lua_pushnumber(s,((float*)r.object)[i]);else return luaL_error(s,"Expected engine array");return 1;}
int ArraySet(lua_State*s){auto&r=Get(s);if(r.kind==DbArray)return luaL_error(s,"Item database arrays are read-only");int i=CheckedInteger(s,2);if(i<0||i>=r.count)return luaL_error(s,"Engine array index out of range");if(r.kind==Bytes)return Field(s,((BYTE*)r.object)[i],true);if(r.kind==Floats)return Field(s,((float*)r.object)[i],true);return luaL_error(s,"Expected engine array");}
int PerkModifier(lua_State*s){auto&r=Get(s);int i=CheckedInteger(s,2);if((r.kind!=Stats&&r.kind!=ClientStats)||i<0||i>=RPG_NUM_PERKS)return luaL_error(s,"Expected stats and valid perk");lua_pushnumber(s,r.kind==Stats?((SRPGUnitStats*)r.object)->GetPerkModifier(i):((const SRPGUserUnitStats*)r.object)->GetPerkModifier(i));return 1;}
int PrimaryAt(lua_State*s){auto&r=Get(s);int i=CheckedInteger(s,2);int mode=CheckedInteger(s,lua_upvalueindex(1));if(mode==2){if(r.kind!=Common||i<0||i>=RPG_NUM_ATTACK_TYPES)return luaL_error(s,"Invalid absorption index");PushRef(s,&((SRPGCommonStats*)r.object)->psAbsorbtions[i],Primary,r.scope);}else {if(r.kind!=Stats||i<0||i>=RPG_NUM_SENSES)return luaL_error(s,"Invalid sense index");auto*p=(SRPGUnitStats*)r.object;PushRef(s,mode?&p->percepts[i]:&p->senses[i],Primary,r.scope);}return 1;}

int StatAdjust(lua_State*s){auto&r=Get(s);int mode=CheckedInteger(s,lua_upvalueindex(1));float *value=nullptr;
 if(r.kind==Primary && mode/2<=1){auto*p=(SRPGPrimaryStat*)r.object;value=mode/2==0?&p->fpBaseValue:&p->fpEffectiveValue;}
 if(r.kind==Secondary && mode/2>=1){auto*p=(SRPGSecondaryStat*)r.object;value=mode/2==1?&p->fpCurrent:mode/2==2?&p->fpMax:&p->fpRegenerate;}
 if(!value)return luaL_error(s,"Invalid stat adjustment");double delta=luaL_checknumber(s,2);lua_settop(s,2);lua_pushnumber(s,*value+(mode%2?-delta:delta));return Field(s,*value,true);
}
int HitAt(lua_State*s){auto&r=Get(s);int i=CheckedInteger(s,2);if(r.kind!=Stats||i<0||i>=RPG_NUM_HIT_LOCATIONS)return luaL_error(s,"Invalid hit location index");PushRef(s,&((SRPGUnitStats*)r.object)->hls[i],HitLocation,r.scope);return 1;}
int LayerAt(lua_State*s){auto&r=Get(s);int i=CheckedInteger(s,2);if(r.kind!=HitLocation||i<0||i>=RPG_NUM_ARMOR_LAYERS)return luaL_error(s,"Invalid armor layer index");PushRef(s,&((SRPGHitLocation*)r.object)->layers[i],ArmorLayer,r.scope);return 1;}
int HitPoints(lua_State*s){auto&r=Get(s);if(r.kind!=Stats)return luaL_error(s,"Expected native stats");auto*p=(SRPGUnitStats*)r.object;double hp=luaL_checknumber(s,2);int op=CheckedInteger(s,lua_upvalueindex(1));
 if(!std::isfinite(hp)||fabs(hp)>(std::numeric_limits<float>::max)())return luaL_error(s,"Invalid hit points");
 // Native distribution writes only used hit locations; an empty skeleton is a no-op.
 if(op!=2){bool used=false;for(const auto&h:p->hls)used=used||h.state>RPGALS_NOTUSED;if(!used)return 0;}
 if(op!=2&&(!std::isfinite(p->hls[RPGHL_BODY].fpHPWeight)||p->hls[RPGHL_BODY].fpHPWeight<=0))return luaL_error(s,"Invalid body HP weight");
 for(auto&h:p->hls)if(h.state>RPGALS_NOTUSED){if(!std::isfinite(h.fpHPWeight)||!std::isfinite(h.fpHP)||!std::isfinite(h.fpMaxHP))return luaL_error(s,"Invalid hit location values");if(op!=0&&h.fpMaxHP<=0)return luaL_error(s,"Invalid hit location max HP");}
 if(op==2){lua_pushnumber(s,p->CollectHP(float(hp)));return 1;}
 if(op==1)p->DistributeHPProportional(float(hp));else p->DistributeHP(float(hp));return 0;
}


int EffectArray(lua_State*s){int arg=lua_istable(s,1)?2:1;auto&r=Get(s,arg);
 if(r.kind==DbArray){const auto span=ItemArray(r);
  if(!span.data||CheckedInteger(s,arg+1)!=span.memoryType||CheckedInteger(s,arg+2)!=span.size||!lua_toboolean(s,arg+3))
   return luaL_error(s,"Expected exact readonly item array type and size");
  lua_pushvalue(s,arg);return 1;
 }
 if((r.kind!=ReadOnlyFloats&&r.kind!=Floats) || CheckedInteger(s,arg+2)!=r.count || (lua_toboolean(s,arg+3)!=0)!=(r.kind==ReadOnlyFloats))return luaL_error(s,"Expected exact native array size and access mode");
 lua_getglobal(s,"Memory");lua_getfield(s,-1,"Type");lua_getfield(s,-1,"Float");bool type=lua_rawequal(s,arg+1,-1)!=0;lua_pop(s,3);
 if(!type)return luaL_error(s,"Native effects require Float element type");lua_pushvalue(s,arg);return 1;
}
int AddPerkRecord(lua_State*s){auto&r=Get(s);int index=CheckedInteger(s,2),level=CheckedInteger(s,3);
 if(r.kind!=Unit || r.object!=r.scope->unit || !r.scope->stagedStats)return luaL_error(s,"Perk mutation requires the active staged unit");
 if(index<0||index>=RPG_NUM_PERKS||level<0||level>=3||index*3+level>=thePerkDatabase.GetNumPerks())return luaL_error(s,"Invalid native perk record");
 try{r.scope->stagedStats->AddPerk(&thePerkDatabase,thePerkDatabase.GetPerk(index*3+level));}
 catch(const std::exception&e){ GameJournal::AllocationFailureInCatch("ModLuaBridge");lua_pushstring(s,e.what());return lua_error(s);}lua_pushinteger(s,level);return 1;
}
int LeftHand(lua_State*s){auto&r=Get(s);if(r.kind!=Unit)return luaL_error(s,"Expected typed unit for left hand");auto*p=(CUnit*)r.object;if(!p->GetUnitSpecific())return luaL_error(s,"Left hand requires native unit-specific component");lua_pushinteger(s,r.scope->LeftHand(*p));return 1;}
int UnitWeapon(lua_State*s){auto&r=Get(s);int i=CheckedInteger(s,2);if(r.kind!=Unit||i < -1||i>=QI_MAX_WEAPONS)return luaL_error(s,"Invalid weapon slot");if(i==-1){lua_pushnil(s);return 1;}auto*p=(CUnit*)r.object;if(!p->GetUnitSpecific())return luaL_error(s,"Weapons require native unit-specific component");PushRef(s,r.scope->Weapon(*p,i),Item,r.scope);return 1;}
int UnitEffect(lua_State*s){auto&r=Get(s);if(r.kind!=Unit)return luaL_error(s,"Expected unit");int id=CheckedInteger(s,2);if(id<0||id>=53)return luaL_error(s,"Invalid spell effect id");lua_pushnumber(s,((CUnit*)r.object)->GetEffectValue(id));return 1;}
int UnitArmor(lua_State*s){auto&r=Get(s);int index=CheckedInteger(s,2);if(r.kind!=Unit)return luaL_error(s,"Expected unit");if(index<0||index>=RPG_NUM_ARMOR_TYPES){lua_pushnil(s);return 1;}PushRef(s,((CUnit*)r.object)->GetQArmor(index),Item,r.scope);return 1;}
int UnitSpellValue(lua_State*s,bool addressResult){
 auto&r=Get(s);if(r.kind!=Unit)return luaL_error(s,"Expected typed unit for spell slot");
 const DWORD index=CheckedSlot(s,2);auto*unit=static_cast<CUnit*>(r.object);
 auto*specific=unit->GetUnitSpecific();
 const bool ownedClient=specific&&specific->WhoAmI()==HPC_UNITCLIENTSPECIFIC&&unit->GetOwner();
 const DWORD limit=ownedClient?QI_MAX_SPELLS:QI_NPC_SPELLS;
 if(ownedClient&&index<limit&&index>=unit->GetSpellSlotCount())
  return luaL_error(s,"Spell slot outside existing typed unit contract");
 CItem* item=index<limit?unit->GetQSpellItem(index):nullptr;
 // Native global returns address-or-zero; the BaseUnit method returns item-or-nil.
 if(!item&&addressResult)lua_pushinteger(s,0);else PushRef(s,item,Item,r.scope);
 return 1;
}
int UnitSpell(lua_State*s){return UnitSpellValue(s,false);}
int GetUnitSpell(lua_State*s){return UnitSpellValue(s,true);}
int UnitQuick(lua_State*s){auto&r=Get(s);int i=CheckedInteger(s,2);if(r.kind!=Unit||i<0)return luaL_error(s,"Invalid quick item slot");auto*p=(CUnit*)r.object;if(!p->GetUnitSpecific())return luaL_error(s,"Quick items require native unit-specific component");if(p->GetUnitSpecific()->WhoAmI()==HPC_UNITCLIENTSPECIFIC&&!p->GetOwner()){lua_pushnil(s);return 1;}PushRef(s,p->GetQItem(i),Item,r.scope);return 1;}
int ArmorMaterialName(lua_State*s){auto&r=Get(s);int index=CheckedInteger(s,2);if(r.kind!=Unit)return luaL_error(s,"Expected unit");if(index<0||index>=RPG_NUM_ARMOR_TYPES){lua_pushnil(s);return 1;}auto*armor=((CUnit*)r.object)->GetQArmor(index);if(!armor){lua_pushnil(s);return 1;}auto id=armor->GetMaterial();if(id>=unsigned(theItemDatabase.materials.GetNumEntries()))return luaL_error(s,"Equipped material missing from database");lua_pushstring(s,theItemDatabase.materials.GetEntry(int(id))->szName);return 1;}
void InstallItemConstructors(lua_State*s);
template<class T>void PushItemRecord(lua_State*s,const CDatabase<T>&table,int kind,int index,const PerkHostBinding&host,const std::shared_ptr<Scope>&scope={}){
 // count is the root record index; children retain no row/member addresses.
 const auto identity=table.GetRecordIdentity();
 new(lua_newuserdata(s,sizeof(Ref))) Ref{nullptr,DbRecord,index,scope,host.lifetime,{},identity,kind,0};
 luaL_getmetatable(s,meta);lua_setmetatable(s,-2);
}
int NativeItemRecord(lua_State*s,int table){
 const int index=CheckedInteger(s,1);
 const auto*host=FindPerkHost(s);if(!host)return luaL_error(s,"Item database requires a live Lua package owner");
 // Installed Lua wrappers resolve their constructor after this getter returns.
 InstallItemConstructors(s);
 int size=0;
 switch(table){
 case DbMaterial:size=theItemDatabase.materials.GetNumEntries();break;
 case DbWeapon:size=theItemDatabase.weapons.GetNumEntries();break;
 case DbQuick:size=theItemDatabase.quickItems.GetNumEntries();break;
 case DbArmor:size=theItemDatabase.armors.GetNumEntries();break;
 }
 if(size==0){lua_pushinteger(s,0);return 1;}
 // Actual native CDatabase aliases every invalid index to entry0 for nonempty tables.
 const int resolved=index>=0&&index<size?index:0;
 try{
  switch(table){
  case DbMaterial:PushItemRecord(s,theItemDatabase.materials,table,resolved,*host);break;
  case DbWeapon:PushItemRecord(s,theItemDatabase.weapons,table,resolved,*host);break;
  case DbQuick:PushItemRecord(s,theItemDatabase.quickItems,table,resolved,*host);break;
  case DbArmor:PushItemRecord(s,theItemDatabase.armors,table,resolved,*host);break;
  }
  return 1;
 }catch(const std::exception&error){ GameJournal::AllocationFailureInCatch("ModLuaBridge");lua_pushstring(s,error.what());}
 return lua_error(s);
}
int NativeGetMaterialRecord(lua_State*s){return NativeItemRecord(s,DbMaterial);}
int NativeGetWeaponRecord(lua_State*s){return NativeItemRecord(s,DbWeapon);}
int NativeGetQuickRecord(lua_State*s){return NativeItemRecord(s,DbQuick);}
int NativeGetArmorRecord(lua_State*s){return NativeItemRecord(s,DbArmor);}
template<class T>int ItemShapeRecord(lua_State*s,const Ref&item,const CDatabase<T>&table,int kind,
                                   int index,const SIDBItemGeneralShape*shape,const PerkHostBinding&host){
 const auto*entry=table.GetEntry(index);
 // The actual addon helper is independent of engine ReportError/AntiCrash interception.
 if(!entry){
  switch(kind){
  case DbWeapon:ErrorMsgBox(1,"GetShapeEntry() is failed because number %d is larger than weapons database size",index);
  case DbArmor:ErrorMsgBox(1,"GetShapeEntry() is failed because number %d is larger than armors database size",index);
  case DbQuick:ErrorMsgBox(1,"GetShapeEntry() is failed because number %d is larger than quickItems database size",index);
  case DbQuest:ErrorMsgBox(1,"GetShapeEntry() is failed because number %d is larger than questItems database size",index);
  case DbLoot:ErrorMsgBox(1,"GetShapeEntry() is failed because number %d is larger than lootItems database size",index);
  default:return luaL_error(s,"Item shape owner has no verified fatal database mapping");
  }
 }
 if(shape!=entry)return luaL_error(s,"Item shape is not the actual current database entry");
 // Convert only borrow-allocation failure to Luaerror; never wrap the native fatal helper.
 try{PushItemRecord(s,table,kind,index,host,item.scope);return 1;}
 catch(const std::exception&error){ GameJournal::AllocationFailureInCatch("ModLuaBridge");lua_pushstring(s,error.what());}
 return lua_error(s);
}
int PushItemShape(lua_State*s,const Ref&item){
 if(item.kind!=Item)return luaL_error(s,"Expected scoped item for shape entry");
 const auto*host=FindPerkHost(s);if(!host)return luaL_error(s,"Item shape requires a live Lua package owner");
 const auto*p=static_cast<const CItem*>(item.object);
 // The actual nonvirtual accessor resolves current DB ownership even for copied/stored items.
 const auto*shape=p->GetShapeEntry();const int index=static_cast<int>(p->GetShape());
 InstallItemConstructors(s);
 switch(p->GetTypeID()){
 case ITID_ITEM_WEAPON:return ItemShapeRecord(s,item,theItemDatabase.weapons,DbWeapon,index,shape,*host);
 case ITID_ITEM_ARMOR:return ItemShapeRecord(s,item,theItemDatabase.armors,DbArmor,index,shape,*host);
 case ITID_ITEM_QUICK:return ItemShapeRecord(s,item,theItemDatabase.quickItems,DbQuick,index,shape,*host);
 case ITID_ITEM_QUEST:return ItemShapeRecord(s,item,theItemDatabase.questItems,DbQuest,index,shape,*host);
 case ITID_ITEM_LOOT:return ItemShapeRecord(s,item,theItemDatabase.lootItems,DbLoot,index,shape,*host);
 default:
  if(shape)return luaL_error(s,"Item shape owner has no verified database mapping");
  lua_pushinteger(s,0);return 1;
 }
}
int NativeGetItemShapeEntry(lua_State*s){return PushItemShape(s,Get(s));}
void PushItemChild(lua_State*s,const Ref&root,Kind kind,int view){
 auto*child=new(lua_newuserdata(s,sizeof(Ref))) Ref(root);child->kind=kind;child->itemView=view;
 luaL_getmetatable(s,meta);lua_setmetatable(s,-2);
}
int ItemRecordAccess(lua_State*s,const Ref&r,const char*k,bool write){
 if(write)return luaL_error(s,"Item database records are read-only");
 if(r.kind==DbArray){const auto span=ItemArray(r);
  if(!span.data)return luaL_error(s,"Invalid item array view");
  if(strcmp(k,"size")==0){lua_pushinteger(s,span.size);return 1;}
  if(strcmp(k,"mem_type")==0){lua_pushinteger(s,span.memoryType);return 1;}
  if(strcmp(k,"read_only")==0){lua_pushboolean(s,true);return 1;}
  if(strcmp(k,"value")==0){lua_pushcfunction(s,ArrayValue);return 1;}
  if(strcmp(k,"set")==0){lua_pushcfunction(s,ArraySet);return 1;}
  return luaL_error(s,"Unknown item array property '%s'",k);
 }
#define DB_STRING(name,member) if(strcmp(k,name)==0){lua_pushstring(s,p->member);return 1;}
#define DB_NUMBER(name,member) if(strcmp(k,name)==0){lua_pushnumber(s,p->member);return 1;}
#define DB_ARRAY(name,view) if(strcmp(k,name)==0){PushItemChild(s,r,DbArray,view);return 1;}
 if(r.kind==DbAbsorbtion){
  if(r.itemTable!=DbArmor||(r.itemView!=1&&r.itemView!=2))return luaL_error(s,"Invalid armor absorbtion view");
  const auto*armor=theItemDatabase.armors.GetEntry(r.count);const auto*p=r.itemView==1?&armor->main:&armor->second;
  DB_NUMBER("absorbtion",fpAbsorbtion);DB_NUMBER("crippleMask",dwCrippleMask);
  DB_ARRAY("distributions",r.itemView==1?ArmorMainDistribution:ArmorSecondDistribution);
 }else if(r.itemTable==DbMaterial){const auto*p=theItemDatabase.materials.GetEntry(r.count);
  DB_STRING("name",szName);DB_STRING("type",szType);DB_STRING("codeName",szCodeName);
  DB_NUMBER("typeIndex",dwTypeIndex);DB_NUMBER("price",fpPrice);DB_NUMBER("weight",fpWeight);
  DB_NUMBER("manaCapacity",fpManaCapacity);DB_NUMBER("slots",dwSpellSlots);DB_NUMBER("durability",fpDurability);
  DB_NUMBER("skill",dwSkill);DB_NUMBER("damageMod",fpDamageModifier);DB_NUMBER("crippleMask",dwCrippleMask);
  DB_NUMBER("availableInShop",dwAvailableInShop);DB_ARRAY("absorbtionMultipliers",MaterialAbsorbtion);
 }else{
  const SIDBItemGeneralShape*p=nullptr;
  if(r.itemTable==DbWeapon)p=theItemDatabase.weapons.GetEntry(r.count);
  else if(r.itemTable==DbArmor)p=theItemDatabase.armors.GetEntry(r.count);
  else if(r.itemTable==DbQuick)p=theItemDatabase.quickItems.GetEntry(r.count);
  else if(r.itemTable==DbQuest)p=theItemDatabase.questItems.GetEntry(r.count);
  else if(r.itemTable==DbLoot)p=theItemDatabase.lootItems.GetEntry(r.count);
  if(!p)return luaL_error(s,"Invalid general item record");
  DB_STRING("name",szName);DB_STRING("type",szType);DB_STRING("materialType",szMaterialType);
  DB_NUMBER("typeId",dwTypeID);DB_NUMBER("materialTypeIndex",dwMaterialTypeIndex);
  DB_NUMBER("tti1",dwTextureTypeIndex);DB_NUMBER("tti2",dwTextureTypeIndex2);
  DB_NUMBER("price",fpPrice);DB_NUMBER("weight",fpWeight);DB_NUMBER("size",fpSize);
  DB_NUMBER("manaCapacity",fpManaCapacity);DB_NUMBER("spellSlots",dwSpellSlots);DB_NUMBER("durability",fpDurability);
  DB_NUMBER("components",dwComponents);DB_NUMBER("availableInShop",dwAvailableInShop);
  if(strcmp(k,"deconstructable")==0){lua_pushboolean(s,p->bDeconstructable);return 1;}
  if(r.itemTable==DbWeapon){const auto*p=theItemDatabase.weapons.GetEntry(r.count);
   DB_NUMBER("actions",dwActionPoints);DB_NUMBER("attackBonus",fAttackBonus);DB_NUMBER("defenceBonus",fDefenceBonus);
   DB_NUMBER("range",fpRange);DB_NUMBER("damageMin",fpDamageMin);DB_NUMBER("damageRandom",fpDamageRandom);
   if(strcmp(k,"damageMax")==0){lua_pushnumber(s,double(p->fpDamageMin)+double(p->fpDamageRandom));return 1;}
   DB_ARRAY("statMods",WeaponStats);DB_ARRAY("manualSkillMods",WeaponManual);DB_ARRAY("autoSkillMods",WeaponAuto);DB_ARRAY("damageDistributions",WeaponDamage);
  }else if(r.itemTable==DbArmor){const auto*p=theItemDatabase.armors.GetEntry(r.count);
   if(strcmp(k,"applyWounds")==0){lua_pushboolean(s,p->bApplyWounds);return 1;}
   DB_NUMBER("wearOrder",dwWearOrder);
   if(strcmp(k,"main")==0){PushItemChild(s,r,DbAbsorbtion,1);return 1;}
   if(strcmp(k,"secondary")==0){PushItemChild(s,r,DbAbsorbtion,2);return 1;}
   DB_ARRAY("manualSkillMod",ArmorManual);DB_ARRAY("autoSkillMod",ArmorAuto);
  }else if(r.itemTable==DbQuick){const auto*p=theItemDatabase.quickItems.GetEntry(r.count);
   DB_NUMBER("quickType",idbType);DB_NUMBER("skill",dwSkill);DB_NUMBER("damage",fDamage);DB_NUMBER("damageType",rpgDamageType);
   DB_STRING("spell",szSpell);DB_ARRAY("manualSkillMods",QuickManual);DB_ARRAY("autoSkillMods",QuickAuto);
  }
 }
#undef DB_STRING
#undef DB_NUMBER
#undef DB_ARRAY
 return luaL_error(s,"Unknown item database property '%s'",k);
}

int Heal(lua_State*s){
 auto&r=Get(s);auto&c=Get(s,2);double value=luaL_checknumber(s,3);
 if(r.kind!=Stats||c.kind!=Common||r.scope!=c.scope||!std::isfinite(value)||fabs(value)>(std::numeric_limits<float>::max)())return luaL_error(s,"Invalid native heal arguments");
 lua_pushboolean(s,((SRPGUnitStats*)r.object)->Heal((SRPGCommonStats*)c.object,float(value)));return 1;
}
int RegenMp(lua_State*s){auto&r=Get(s);double value=luaL_checknumber(s,2);if(r.kind!=Common||!std::isfinite(value)||fabs(value)>(std::numeric_limits<float>::max)())return luaL_error(s,"Invalid native MP regeneration");lua_pushboolean(s,((SRPGCommonStats*)r.object)->RegenerateMP(float(value)));return 1;}
int NativeGetPerkRecord(lua_State*s){
 const int index=CheckedInteger(s,1);
 if(!FindPerkHost(s))return luaL_error(s,"Perk database requires a live Lua package owner");
 // database.lua's getRecord looks up the constructor after this getter returns.
 // Install only that constructor when its module exists; do not change require order.
 InstallPerkConstructor(s);
 if(!thePerkDatabase.GetPerk(index)){lua_pushinteger(s,0);return 1;}
 try{PushPerkRef(s,index,{});return 1;}catch(const std::exception&e){ GameJournal::AllocationFailureInCatch("ModLuaBridge");lua_pushstring(s,e.what());}
 return lua_error(s);
}
int NativeGetPerkIndex(lua_State*s){
 auto& ref=Get(s);if(ref.kind!=Perk)return luaL_error(s,"Expected scoped perk record for database index");
 lua_pushinteger(s,thePerkDatabase.GetPerkIndex(thePerkDatabase.GetPerk(ref.count)));return 1;
}
int NativeItemsWeight(lua_State*s){
 auto& ref=Get(s);if(ref.kind!=Unit)return luaL_error(s,"Expected scoped unit for item weight");
 auto* unit=static_cast<CUnit*>(ref.object);float weight=unit->GetItemsWeight(QI_NPC_SPELLS);
 if(weight==0&&theWorldServer.GetMapObjectManager()){
  auto* serverUnit=theWorldServer.GetUnit(static_cast<int>(unit->GetID()));
  if(serverUnit)weight=serverUnit->GetItemsWeight(QI_NPC_SPELLS);
 }
 lua_pushnumber(s,weight);return 1;
}
int NativeEquipment(lua_State*s,int kind){
 auto& ref=Get(s);const DWORD index=CheckedSlot(s,2);
 if(ref.kind!=Unit)return luaL_error(s,"Expected scoped unit for equipment");
 auto* unit=static_cast<CUnit*>(ref.object);auto* specific=unit->GetUnitSpecific();
 if(!specific)return luaL_error(s,"Equipment requires an existing unit-specific owner");
 const bool client=specific->WhoAmI()==HPC_UNITCLIENTSPECIFIC;auto* owner=unit->GetOwner();CItem* item=nullptr;
 if(kind==0){
  if(client&&!owner){
   if(unit->GetCurrentWeaponSlotIndex()<QI_MAX_WEAPONS){
    if(index>=QI_MAX_WEAPONS)return luaL_error(s,"Ownerless-client weapon slot outside stored array");
    item=unit->GetStoredQWeapon(index);
   }
  }else item=unit->GetQWeapon(index);
 }else if(client&&owner){
  auto* personage=owner->party.GetPersonage(static_cast<int>(unit->GetID()));
  if(personage){
   if(kind==1){if(index<static_cast<DWORD>(personage->armors.GetSize()))item=personage->armors[index];}
   else if(index<static_cast<DWORD>(personage->items.GetSize()))item=personage->items[index];
  }
 }else if(kind==1){if(index<RPG_NUM_ARMOR_TYPES)item=unit->GetQArmor(index);}
 else item=unit->GetStoredQItem(index);
 if(item)PushRef(s,item,Item,ref.scope);else lua_pushinteger(s,0);return 1;
}
int NativeGetWeapon(lua_State*s){return NativeEquipment(s,0);}
int NativeGetArmor(lua_State*s){return NativeEquipment(s,1);}
int NativeGetQuick(lua_State*s){return NativeEquipment(s,2);}
int NativeGetEffectValue(lua_State*s){
 auto& ref=Get(s);const int id=CheckedInteger(s,2);
 if(ref.kind!=Unit)return luaL_error(s,"Expected scoped unit for effect value");
 lua_pushnumber(s,static_cast<CUnit*>(ref.object)->GetEffectValue(id));return 1;
}
int NativePerkModifier(lua_State*s,bool client){
 auto& ref=Get(s);const int id=CheckedInteger(s,2);
 if(ref.kind!=(client?ClientStats:Stats)||id<0||id>=RPG_NUM_PERKS)
  return luaL_error(s,"Expected matching scoped stats and valid perk ID");
 lua_pushnumber(s,client?static_cast<const SRPGUserUnitStats*>(ref.object)->GetPerkModifier(id):
                         static_cast<SRPGUnitStats*>(ref.object)->GetPerkModifier(id));return 1;
}
int NativeServerPerkModifier(lua_State*s){return NativePerkModifier(s,false);}
int NativeClientPerkModifier(lua_State*s){return NativePerkModifier(s,true);}
int NativeHeal(lua_State*s){
 auto& stats=Get(s,1);auto& common=Get(s,2);const float value=static_cast<float>(luaL_checknumber(s,3));
 if(stats.kind!=Stats||common.kind!=Common||stats.scope!=common.scope)
  return luaL_error(s,"Expected same-scope server stats and common stats");
 static_cast<SRPGUnitStats*>(stats.object)->Heal(static_cast<SRPGCommonStats*>(common.object),value);return 0;
}
int NativeRegenMp(lua_State*s){
 auto& ref=Get(s);const float value=static_cast<float>(luaL_checknumber(s,2));
 if(ref.kind!=Common)return luaL_error(s,"Expected scoped common stats for MP regeneration");
 auto* common=static_cast<SRPGCommonStats*>(ref.object);
 // Native guard is >=; the existing typed method only tests ==. Keep it unchanged.
 if(common->ssMP.fpCurrent>=common->ssMP.fpMax)return 0;
 common->RegenerateMP(value);return 0;
}
int NativeHitPoints(lua_State*s,int operation){
 auto& ref=Get(s);const float maxHp=static_cast<float>(luaL_checknumber(s,2));
 if(ref.kind!=Stats)return luaL_error(s,"Expected scoped server stats for hit points");
 auto* stats=static_cast<SRPGUnitStats*>(ref.object);
 if(operation==2){lua_pushnumber(s,stats->CollectHP(maxHp));return 1;}
 if(operation==1)stats->DistributeHPProportional(maxHp);else stats->DistributeHP(maxHp);return 0;
}
int NativeDistributeHp(lua_State*s){return NativeHitPoints(s,0);}
int NativeDistributeHpProportional(lua_State*s){return NativeHitPoints(s,1);}
int NativeCollectHp(lua_State*s){return NativeHitPoints(s,2);}
int NativeRound(lua_State*s){
 const double value=luaL_checknumber(s,1);double integer;
 const double fraction=modf(value,&integer);
 lua_pushnumber(s,fabs(fraction)<0.5?integer:integer>0.0?integer+1.0:integer-1.0);return 1;
}
int FixHitLocations(lua_State*s){
 auto& stats=Get(s,1);auto& unit=Get(s,2);const float maxHp=static_cast<float>(luaL_checknumber(s,3));
 if(stats.kind!=Stats||unit.kind!=Unit||stats.scope!=unit.scope)return luaL_error(s,"Expected same-scope server stats and unit");
 try{static_cast<SRPGUnitStats*>(stats.object)->FixHls(static_cast<CUnit*>(unit.object),maxHp);return 0;}
 catch(const std::exception& error){ GameJournal::AllocationFailureInCatch("ModLuaBridge");lua_pushstring(s,error.what());}
 return lua_error(s);
}
int EngineClassId(lua_State*s){
 auto& ref=Get(s);CGameBase* object=nullptr;
 if(ref.kind==Unit)object=static_cast<CUnit*>(ref.object);
 else if(ref.kind==Item)object=static_cast<CItem*>(ref.object);
 else if(ref.kind==Spell)object=static_cast<CSpell*>(ref.object);
 else return luaL_error(s,"Expected scoped engine class owner");
 // Explicit native IDs from matching ClassIDs.h enums, not a raw vtable/Kind value.
 switch(object->WhoAmI()){
 case HPC_UNIT:lua_pushinteger(s,80);return 1;
 case HPC_FAKEUNIT:lua_pushinteger(s,81);return 1;
 case HPC_ITEM:lua_pushinteger(s,16);return 1;
 case HPC_ITEM_WEAPON:lua_pushinteger(s,17);return 1;
 case HPC_ITEM_ARMOR:lua_pushinteger(s,18);return 1;
 case HPC_ITEM_QUICK:lua_pushinteger(s,19);return 1;
 case HPC_ITEM_QUEST:lua_pushinteger(s,20);return 1;
 case HPC_ITEM_LOOT:lua_pushinteger(s,21);return 1;
 case HPC_ITEM_SPELL:lua_pushinteger(s,22);return 1;
 case HPC_SPELL:lua_pushinteger(s,79);return 1;
 default:return luaL_error(s,"Engine class has no verified native Lua class ID");
 }
}
int Access(lua_State*s,bool write){auto&r=Get(s);const char*k=luaL_checkstring(s,2);
#define F(name,member) if(strcmp(k,name)==0)return Field(s,p->member,write)
#define CHILD(name,member,type) if(!write&&strcmp(k,name)==0){PushRef(s,&p->member,type,r.scope);return 1;}
#define ARRAY(name,member,type) if(!write&&strcmp(k,name)==0){PushRef(s,p->member,type,r.scope,_countof(p->member));return 1;}
 if(!write&&strcmp(k,"addr")==0){
  if(r.kind==Unit)return PushUnitCacheKey(s,*static_cast<CUnit*>(r.object));
  lua_pushvalue(s,1);return 1;
 }
 if(r.kind==DbRecord||r.kind==DbAbsorbtion||r.kind==DbArray)return ItemRecordAccess(s,r,k,write);
 if(r.kind==Perk&&!write){const auto*p=thePerkDatabase.GetPerk(r.count);
#define PERK_STRING(name,member) if(strcmp(k,name)==0){lua_pushstring(s,p->member);return 1;}
 PERK_STRING("name",szName);PERK_STRING("code",szCode);PERK_STRING("perkRequired",szPerkRequired);PERK_STRING("skillRequired",szSkillRequired);
#undef PERK_STRING
#define PERK_NUMBER(name,member) if(strcmp(k,name)==0){lua_pushnumber(s,p->member);return 1;}
 PERK_NUMBER("tti",dwTextureTypeIndex);PERK_NUMBER("perkExclusive",nPerkExclusive);PERK_NUMBER("autoSkill",rpgAutoSkill);PERK_NUMBER("manualSkill",rpgManualSkill);PERK_NUMBER("skillLevel",dwSkillLevel);
 PERK_NUMBER("strength",fStrength);PERK_NUMBER("dexterity",fDexterity);PERK_NUMBER("intelligence",fIntelligence);PERK_NUMBER("cost",dwCost);PERK_NUMBER("modifier",dwModifier);PERK_NUMBER("mult",dwMult);PERK_NUMBER("add",dwAdd);
#undef PERK_NUMBER
 if(strcmp(k,"active")==0){lua_pushboolean(s,p->bActive);return 1;}
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
 if(strcmp(k,"classId")==0)return EngineClassId(s);
 if(strcmp(k,"currWeapon")==0){if(!p->GetUnitSpecific())return luaL_error(s,"Unit equipment requires a native unit-specific component");PushRef(s,r.scope->Weapon(*p,int(p->GetCurrentWeaponSlotIndex())),Item,r.scope);return 1;}
 if(strcmp(k,"weapon")==0){lua_pushcfunction(s,UnitWeapon);return 1;}
 if(strcmp(k,"quickItem")==0){lua_pushcfunction(s,UnitQuick);return 1;}
 if(strcmp(k,"armor")==0){lua_pushcfunction(s,UnitArmor);return 1;}
 if(strcmp(k,"spellEffect")==0){lua_pushcfunction(s,UnitEffect);return 1;}
 if(strcmp(k,"spell")==0){lua_pushcfunction(s,UnitSpell);return 1;}

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
 if(!write&&strcmp(k,"prototypeAddr")==0)return PushItemShape(s,r);
 if(!write&&strcmp(k,"prototype")==0){
  PushItemShape(s,r);if(lua_isnumber(s,-1))return luaL_error(s,"Item prototype missing");return 1;
 }
 if(!write&&strcmp(k,"actions")==0&&p->GetTypeID()==ITID_ITEM_WEAPON){
  // Installed ItemWeapon.actions uses prototypeAddr+0x6C; read the real typed shape instead.
  PushItemShape(s,r);lua_getfield(s,-1,"actions");lua_remove(s,-2);return 1;
 }
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
int Factory(lua_State*s){int arg=lua_istable(s,1)?2:1;auto&r=Get(s,arg);int kind=CheckedInteger(s,lua_upvalueindex(1));if(int(r.kind)!=kind)return luaL_error(s,"Wrong engine object kind");lua_pushvalue(s,arg);return 1;}
void Constructor(lua_State*s,Kind kind){
 lua_newtable(s);lua_pushinteger(s,kind);lua_pushcclosure(s,Factory,1);lua_setfield(s,-2,"new");
 lua_newtable(s);lua_pushinteger(s,kind);lua_pushcclosure(s,Factory,1);lua_setfield(s,-2,"__call");lua_setmetatable(s,-2);
}
void InstallPerkConstructor(lua_State*s){
 const int top=lua_gettop(s);lua_getglobal(s,"PerksDatabase");
 if(lua_istable(s,-1)){
  lua_pushliteral(s,"PerksTableRecord");lua_rawget(s,-2);bool installed=false;
  if(lua_istable(s,-1)){
   lua_pushliteral(s,"new");lua_rawget(s,-2);
   if(lua_tocfunction(s,-1)==Factory&&lua_getupvalue(s,-1,1)){installed=lua_tointeger(s,-1)==Perk;lua_pop(s,1);}
   lua_pop(s,1);
  }
  lua_pop(s,1);
  if(!installed){Constructor(s,Perk);lua_setfield(s,-2,"PerksTableRecord");}
 }
 lua_settop(s,top);
}
enum ItemFactoryKind {GeneralItemFactory=4,ArmorAbsorbtionFactory=5};
int ItemFactory(lua_State*s){
 const int arg=lua_istable(s,1)?2:1;const auto&r=Get(s,arg);
 const int kind=CheckedInteger(s,lua_upvalueindex(1));bool matches=false;
 if(kind==GeneralItemFactory)matches=r.kind==DbRecord&&r.itemTable!=DbMaterial;
 else if(kind==ArmorAbsorbtionFactory)matches=r.kind==DbAbsorbtion&&r.itemTable==DbArmor;
 else matches=r.kind==DbRecord&&r.itemTable==kind;
 if(!matches)return luaL_error(s,"Wrong item database record kind");
 lua_pushvalue(s,arg);return 1;
}
void ItemConstructor(lua_State*s,int kind){
 lua_newtable(s);lua_pushinteger(s,kind);lua_pushcclosure(s,ItemFactory,1);lua_setfield(s,-2,"new");
 lua_newtable(s);lua_pushinteger(s,kind);lua_pushcclosure(s,ItemFactory,1);lua_setfield(s,-2,"__call");lua_setmetatable(s,-2);
}
void InstallItemConstructors(lua_State*s){
 const int top=lua_gettop(s);lua_getglobal(s,"ItemsDatabase");
 if(lua_istable(s,-1)){
  struct Entry{const char*name;int kind;};const Entry entries[]={
   {"MaterialsTableRecord",DbMaterial},{"WeaponsTableRecord",DbWeapon},{"QuicksTableRecord",DbQuick},{"ArmorsTableRecord",DbArmor},
   {"GeneralRecord",GeneralItemFactory},{"ArmorsTableAbsorbtion",ArmorAbsorbtionFactory}};
  for(const auto&e:entries){
   lua_pushstring(s,e.name);lua_rawget(s,-2);bool installed=false;
   if(lua_istable(s,-1)){
    lua_pushliteral(s,"new");lua_rawget(s,-2);
    if(lua_tocfunction(s,-1)==ItemFactory&&lua_getupvalue(s,-1,1)){installed=lua_tointeger(s,-1)==e.kind;lua_pop(s,1);}
    lua_pop(s,1);
   }
   lua_pop(s,1);
   if(!installed){ItemConstructor(s,e.kind);lua_setfield(s,-2,e.name);}
  }
 }
 lua_settop(s,top);
}
int PerkIndex(lua_State*s){return NativeGetPerkIndex(s);}

int IntegerMath(lua_State*s){
 const int a=CheckedInteger(s,1),b=CheckedInteger(s,2);
 if(b==0||(a==(std::numeric_limits<int>::lowest)()&&b==-1))return luaL_error(s,"Invalid integer division");
 lua_pushinteger(s,lua_toboolean(s,lua_upvalueindex(1))?a%b:a/b);return 1;
}
int UnitItemsWeight(lua_State*s){auto&r=Get(s);if(r.kind!=Unit)return luaL_error(s,"Expected unit");auto*u=(CUnit*)r.object;float weight=u->GetItemsWeight();if(u->GetUnitSpecific()&&(u->GetUnitSpecific()->WhoAmI()!=HPC_UNITCLIENTSPECIFIC||u->GetOwner()))for(DWORD i=0;i<u->GetSpellSlotCount();i++)if(u->GetQSpellItem(i))weight+=1;lua_pushnumber(s,weight);return 1;}
int ClientHlsPercent(lua_State*s){auto&r=Get(s);if(r.kind!=Unit)return luaL_error(s,"Expected unit");int part=CheckedInteger(s,2);float result=1;for(const auto&h:((CUnit*)r.object)->GetUserUnitStats()->hls)if(h.hlt==part&&h.wMaxHP>0)result=(std::min)(result,(std::max)(.0001f,float(h.wHP)+.0001f)/float(h.wMaxHP));lua_pushnumber(s,result);return 1;}
int Difficulty(lua_State*s){lua_pushinteger(s,theSettings.nDifficultyLevel);return 1;}
int PlayUnitSound(lua_State*s){
 auto& ref=Get(s);if(ref.kind!=Unit)return luaL_error(s,"Expected unit");
 const char* name=luaL_checklstring(s,2,nullptr);
 const int minimum=CheckedInteger(s,3);
 const int maximum=CheckedInteger(s,4);
 auto* manager=theWorld.GetSoundManager();
 if(!manager)return luaL_error(s,"Sound manager unavailable");
 manager->BeginFX(name,0,static_cast<CUnit*>(ref.object)->GetCP(),
                  float(minimum),float(maximum),FALSE);
 // Native Lua API ignores the sound handle and returns the difficulty level.
 return Difficulty(s);
}
int GetUnitGSVar(lua_State*s){
 auto&r=Get(s);if(r.kind!=Unit)return luaL_error(s,"Expected typed unit for GS variable");
 const char* name=luaL_checklstring(s,2,nullptr);auto* owner=static_cast<CUnit*>(r.object)->GetOwner();
 if(owner)for(const auto& entry:owner->party.vars)
  if(strcmp(entry.first.c_str(),name)==0){lua_pushnumber(s,entry.second);return 1;}
 lua_pushnil(s);return 1;
}
int SetUnitGSVar(lua_State*s){
 auto&r=Get(s);if(r.kind!=Unit)return luaL_error(s,"Expected typed unit for GS variable");
 const char* name=luaL_checklstring(s,2,nullptr);const float value=static_cast<float>(luaL_checknumber(s,3));
 auto* owner=static_cast<CUnit*>(r.object)->GetOwner();if(!owner)return 0;
 try{
  // Retail SetVar's x87 equal/unordered branch deletes zero and NaN (P07).
  owner->party.vars.SetVar(name,value==value?value:0.0f);return 0;
 }catch(const std::exception& error){ GameJournal::AllocationFailureInCatch("ModLuaBridge");lua_pushstring(s,error.what());}
 return lua_error(s);
}
int IsHostUnit(lua_State*s){
 auto&r=Get(s);if(r.kind!=Unit)return luaL_error(s,"Expected typed unit for host membership");
 auto* player=theWorldServer.GetPlayerManager()?theWorldServer.GetPlayer(0):nullptr;
 bool host=false;if(player)for(int i=0;i<player->party.people.GetSize();++i)
  if(static_cast<CUnit*>(r.object)->GetID()==player->party.people[i].nID){host=true;break;}
 lua_pushboolean(s,host);return 1;
}
void WriteConsoleLine(const char *value){
 NConsole::game.write(value,std::char_traits<char>::length(value));
 NConsole::game.put('\n');
 NConsole::game.flush();
}
int Console(lua_State*s){WriteConsoleLine(luaL_checkstring(s,1));return 0;}
int TempDumpVars(lua_State*s){
 auto&r=Get(s);if(r.kind!=Unit)return luaL_error(s,"Expected typed unit for GS variable dump");
 auto* owner=static_cast<CUnit*>(r.object)->GetOwner();if(!owner)return 0;
 for(const auto& entry:owner->party.vars){
  CString line;line.Format("Name: \"%s\" = %.2f",entry.first.c_str(),entry.second);
  WriteConsoleLine(line.GetString());
 }
 return 0;
}
// The installed complex-cache closures index their first argument directly.
// Normalize both host userdata and nested Lua calls to the same ObjectId key,
// retaining the actual Lua formulas/cache closures as the implementation.
int ComplexCallback(lua_State*s){
 const int args=lua_gettop(s);auto&r=Get(s);
 if(r.kind!=Unit)return luaL_error(s,"Complex cache requires a typed unit or callback-bound ObjectId");
 PushUnitCacheKey(s,*static_cast<CUnit*>(r.object));lua_replace(s,1);
 lua_pushvalue(s,lua_upvalueindex(1));lua_insert(s,1);lua_call(s,args,LUA_MULTRET);
 return lua_gettop(s);
}
int RawRejected(lua_State*s){return luaL_error(s,"Original-EXE memory accessor unavailable; use typed engine object");}
}
void Push(lua_State*s,const CSpell*p,const std::shared_ptr<Scope>&scope){PushRef(s,const_cast<CSpell*>(p),Spell,scope);}
void Push(lua_State*s,SAttackParams*p,const std::shared_ptr<Scope>&scope){PushRef(s,p,Attack,scope);}
void PushFloats(lua_State*s,float*p,int count,const std::shared_ptr<Scope>&scope){PushRef(s,p,Floats,scope,count);}
void Push(lua_State*s,const SRPGUserUnitStats*p,const std::shared_ptr<Scope>&scope){PushRef(s,const_cast<SRPGUserUnitStats*>(p),ClientStats,scope);}
void BindPerkDatabase(lua_State*s,const std::shared_ptr<PerkHostLifetime>&owner){
 luaL_newmetatable(s,perkHostMeta);lua_pushcfunction(s,PerkHostGC);lua_setfield(s,-2,"__gc");
 lua_pushliteral(s,"perk host");lua_setfield(s,-2,"__metatable");lua_pop(s,1);
 lua_pushlightuserdata(s,const_cast<char*>(&perkHostKey));
 new(lua_newuserdata(s,sizeof(PerkHostBinding))) PerkHostBinding{owner};
 luaL_getmetatable(s,perkHostMeta);lua_setmetatable(s,-2);lua_rawset(s,LUA_REGISTRYINDEX);
}
void Register(lua_State*s){
 lua_pushcfunction(s,NativeGetItemShapeEntry);lua_setglobal(s,"GetItemShapeEntry");
 lua_pushcfunction(s,NativeGetMaterialRecord);lua_setglobal(s,"GetMaterialRecord");
 lua_pushcfunction(s,NativeGetWeaponRecord);lua_setglobal(s,"GetWeaponRecord");
 lua_pushcfunction(s,NativeGetQuickRecord);lua_setglobal(s,"GetQuickRecord");
 lua_pushcfunction(s,NativeGetArmorRecord);lua_setglobal(s,"GetArmorRecord");
 lua_pushcfunction(s,ArmorMaterialName);lua_setglobal(s,"GetEquippedArmorMaterialName");for(int mod=0;mod<2;mod++){lua_pushboolean(s,mod);lua_pushcclosure(s,IntegerMath,1);lua_setglobal(s,mod?"IMod":"IDiv");}lua_pushcfunction(s,Difficulty);lua_setglobal(s,"GetDifficulty");lua_pushcfunction(s,PlayUnitSound);lua_setglobal(s,"PlaySound");lua_pushcfunction(s,GetUnitSpell);lua_setglobal(s,"GetSpell");lua_pushcfunction(s,GetUnitGSVar);lua_setglobal(s,"GetGSVar");lua_pushcfunction(s,SetUnitGSVar);lua_setglobal(s,"SetGSVar");lua_pushcfunction(s,IsHostUnit);lua_setglobal(s,"UnitIsHost");lua_pushcfunction(s,NativeRound);lua_setglobal(s,"Round");lua_pushcfunction(s,FixHitLocations);lua_setglobal(s,"FixHls");lua_pushcfunction(s,EngineClassId);lua_setglobal(s,"GetClassId");lua_pushcfunction(s,NativeGetEffectValue);lua_setglobal(s,"GetEffectValue");lua_pushcfunction(s,NativeServerPerkModifier);lua_setglobal(s,"GetPerkModifierForServer");lua_pushcfunction(s,NativeClientPerkModifier);lua_setglobal(s,"GetPerkModifierForClient");lua_pushcfunction(s,NativeHeal);lua_setglobal(s,"Heal");lua_pushcfunction(s,NativeRegenMp);lua_setglobal(s,"RegenMp");lua_pushcfunction(s,NativeDistributeHp);lua_setglobal(s,"DistributeHp");lua_pushcfunction(s,NativeDistributeHpProportional);lua_setglobal(s,"DistributeHpProportional");lua_pushcfunction(s,NativeCollectHp);lua_setglobal(s,"CollectHp");lua_pushcfunction(s,NativeItemsWeight);lua_setglobal(s,"GetItemsWeight");lua_pushcfunction(s,NativeGetWeapon);lua_setglobal(s,"GetWeapon");lua_pushcfunction(s,NativeGetArmor);lua_setglobal(s,"GetArmor");lua_pushcfunction(s,NativeGetQuick);lua_setglobal(s,"GetQuick");lua_pushcfunction(s,NativeGetPerkIndex);lua_setglobal(s,"GetPerkIndex");lua_pushcfunction(s,NativeGetPerkRecord);lua_setglobal(s,"GetPerkRecord");luaL_newmetatable(s,meta);lua_pushcfunction(s,Index);lua_setfield(s,-2,"__index");lua_pushcfunction(s,NewIndex);lua_setfield(s,-2,"__newindex");lua_pushcfunction(s,GC);lua_setfield(s,-2,"__gc");lua_pushliteral(s,"engine object");lua_setfield(s,-2,"__metatable");lua_pop(s,1);lua_pushcfunction(s,Console);lua_setglobal(s,"ConsoleWriteLine");lua_pushcfunction(s,TempDumpVars);lua_setglobal(s,"TempDumpVars");
 const char* types[]={"Str","Float","Double","UInt32","UInt16","UInt8","Int32","Int16","Int8"};for(auto t:types)for(auto suffix:{"FromMem","ToMem"}){std::string name=std::string(t)+suffix;lua_pushcfunction(s,RawRejected);lua_setglobal(s,name.c_str());}}
int BeginCallbackScope(lua_State*s){
 lua_pushlightuserdata(s,const_cast<char*>(&unitRefsKey));lua_rawget(s,LUA_REGISTRYINDEX);
 const int previous=luaL_ref(s,LUA_REGISTRYINDEX);
 lua_pushlightuserdata(s,const_cast<char*>(&unitRefsKey));lua_newtable(s);lua_rawset(s,LUA_REGISTRYINDEX);
 return previous;
}
void EndCallbackScope(lua_State*s,int previous){
 lua_pushlightuserdata(s,const_cast<char*>(&unitRefsKey));
 if(previous==LUA_REFNIL)lua_pushnil(s);else lua_rawgeti(s,LUA_REGISTRYINDEX,previous);
 lua_rawset(s,LUA_REGISTRYINDEX);luaL_unref(s,LUA_REGISTRYINDEX,previous);
}
void InstallFactories(lua_State*s){
  lua_pushcfunction(s,ClientHlsPercent);lua_setglobal(s,"GetHlsPercent");lua_pushcfunction(s,UnitItemsWeight);lua_setglobal(s,"GetUnitItemsWeight");
 lua_pushcfunction(s,AddPerkRecord);lua_setglobal(s,"AddPerkRecord");lua_pushcfunction(s,LeftHand);lua_setglobal(s,"GetLeftHand");
 lua_newtable(s);lua_pushcfunction(s,EffectArray);lua_setfield(s,-2,"new");lua_newtable(s);lua_pushcfunction(s,EffectArray);lua_setfield(s,-2,"__call");lua_setmetatable(s,-2);lua_setglobal(s,"Array");struct Entry{const char*name;Kind kind;};const Entry entries[]={{"Spell",Spell},{"SpellEffect",SpellEffect},{"AttackParams",Attack},{"ItemSpell",Item},{"UnitClientStats",ClientStats},{"UnitServerStats",Stats},{"UnitServerCommonStats",Common},{"UnitServer",Unit},{"UnitClient",Unit},{"BaseUnit",Unit},{"BaseItem",Item},{"ItemWeapon",Item},{"ItemArmor",Item},{"ItemQuick",Item},{"PrimaryStat",Primary},{"SecondaryStat",Secondary},{"HitLocation",HitLocation},{"ArmorLayer",ArmorLayer}};for(auto&e:entries){Constructor(s,e.kind);lua_setglobal(s,e.name);}
 InstallPerkConstructor(s);lua_getglobal(s,"PerksDatabase");lua_getfield(s,-1,"PerksTable");lua_pushcfunction(s,PerkIndex);lua_setfield(s,-2,"getRecordIndex");lua_pop(s,2);
 for(const char*name:{"GetComplexItemsStats","GetComplexStats","GetComplexSummStats"}){
  lua_getglobal(s,name);
  if(!lua_isfunction(s,-1))throw std::runtime_error(std::string("Missing complex stats callback: ")+name);
  lua_pushcclosure(s,ComplexCallback,1);lua_setglobal(s,name);
 }
 // Keep the installed Lua getRecord wrappers: empty0->nil and dynamic typed factory.
 InstallItemConstructors(s);

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
void Push(lua_State*s,const SPDBPerk*p,const std::shared_ptr<Scope>&scope){int index=thePerkDatabase.GetPerkIndex(p);if(index<0)throw std::runtime_error("Perk does not belong to the loaded engine database");PushPerkRef(s,index,scope);}
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
