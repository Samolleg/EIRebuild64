#pragma once
#include "UnitServer.h"
#include "Unit.h"
#include "WorldServer.h"
#include "MapObjectServer.h"
#include "ModRuntime.h"
#include "ModPackage.h"
#include "ModSpellRules.h"
#include "ModNpcActionUtility.h"
#include <unordered_map>
namespace ModTactical {
enum Type { Weapon, Direct, Area, Continuous, Debuff, CombatBuff, PeaceBuff, Heal, Useless };
inline Type CasterTableType(int rawID,const ModSpellAddon::Profile& profile){
 // ASI copies only 54 entries. ID 54 used padding as a jump-table index;
 // the agreed typed contract rejects it and every higher ID safely.
 if(rawID<0||rawID>=54)return Useless;
 const auto key=static_cast<ModSpellAddon::ByteKey>(
  static_cast<size_t>(ModSpellAddon::ByteKey::aicastertable_0)+rawID);
 switch(profile.Byte(key)){
 case 0:return Direct;case 1:return Area;case 2:return Continuous;
 case 3:return CombatBuff;case 4:return PeaceBuff;case 5:return Useless;
 case 6:return Heal;case 7:return Debuff;default:return Useless;
 }
}
inline Type SpellType(CSpell*spell){
 if(!spell)return Weapon;
 if(auto*mod=ModRuntime::Current())if(mod->SpellAddon().Feature48()==1)
  return CasterTableType(int(spell->GetSubtypeID()>>16),mod->SpellAddon());
 switch(ModSpells::ElementalBase(spell->GetSubtypeID()>>16)){
 case SPELL_ID_ACIDRAY:case SPELL_ID_FIREARROW:case SPELL_ID_LIGHTNING:case SPELL_ID_RICKMAGIC:case SPELL_ID_CURSEMAGIC:case SPELL_ID_VAMP:return Direct;
 case SPELL_ID_FIREBALL:case SPELL_ID_INVOKE_LGHTNG:case SPELL_ID_COLUMN_ACID:return Area;
 case SPELL_ID_FIREWALL:case SPELL_ID_WALL_LGHTNG:case SPELL_ID_POISONFOG:return Continuous;
 case SPELL_ID_WEAKEN:case SPELL_ID_FEEBLEMIND:case SPELL_ID_STUN:case SPELL_ID_STENCH:case SPELL_ID_SLOW:case SPELL_ID_SHRINK:return Debuff;
 case SPELL_ID_PROT_FIRE:case SPELL_ID_PROT_ELECTRIC:case SPELL_ID_PROT_ACID:case SPELL_ID_STRENGTHEN:case SPELL_ID_ANTIMAGIC:case SPELL_ID_ENLARGE:return CombatBuff;
 case SPELL_ID_EAGLE_SIGHT:case SPELL_ID_INFRAVISION:case SPELL_ID_DETECT_LIFE:case SPELL_ID_INVISIBILITY:case SPELL_ID_SILENCE:case SPELL_ID_LICHDOM:case SPELL_ID_REGENERATION:case SPELL_ID_SPEED:return PeaceBuff;
 case SPELL_ID_HEALING:return Heal;default:return Useless;
 }
}
inline bool Enabled(CUnit*unit){auto*mod=ModRuntime::Current();return mod&&unit&&(!unit->IsHero()||mod->CompanionSkillAI(*unit));}
inline CUnitServer* Server(CUnit*unit){return unit?dynamic_cast<CUnitServer*>(unit->GetUnitSpecific()):nullptr;}
inline CSpell* Spell(CUnit*unit,int slot){return unit&&slot>=0&&slot<8&&slot<int(unit->GetSpellSlotCount())?unit->GetQSpell(slot):nullptr;}
inline bool LiveEnemy(CUnit*unit,CUnit*enemy){if(!unit||unit->IsDead()||!enemy||enemy==unit||enemy->IsDead())return false;int player=enemy->GetPlayerNum();return player>=0&&player<32&&unit->IsEnemy(player);}
inline CUnit* CommandEnemy(CUnit*unit,const SUnitCommand&cmd){if(cmd.pTarget.IsEmpty()||(!cmd.IsAttack()&&cmd.cmd!=UC_CASTUNIT))return nullptr;auto*enemy=theWorldServer.GetUnit(cmd.pTarget->GetID());return enemy==static_cast<CUnit*>(cmd.pTarget)&&LiveEnemy(unit,enemy)?enemy:nullptr;}
inline CUnit* FightingEnemy(CUnit*unit){auto*server=Server(unit);if(!server||unit->IsDead())return nullptr;auto*enemy=CommandEnemy(unit,server->GetScriptCommand());return enemy?enemy:CommandEnemy(unit,server->GetNextScriptCommand());}
inline bool CommandMatches(const SUnitCommand&cmd,int slot,CUnit*target){return cmd.IsCast()&&cmd.nQSpellNum==slot&&((cmd.cmd==UC_CASTUNIT&&cmd.pTarget==target)||cmd.cmd==UC_CASTPOINT);}
inline bool HasCommand(CUnitServer*server,int slot,CUnit*target){return server&&(CommandMatches(server->GetScriptCommand(),slot,target)||CommandMatches(server->GetNextScriptCommand(),slot,target));}
inline bool ObsoleteHeal(CUnitServer*server,CSpell*spell,CMapObject*object){
 auto*unit=server?server->GetUnit():nullptr;if(!unit||unit->IsHero()||unit->GetOwner()||!spell||(spell->GetSubtypeID()>>16)!=SPELL_ID_HEALING||!object||object->WhoAmI()!=HPC_UNIT)return false;
 auto*target=static_cast<CUnit*>(object);auto*mod=ModRuntime::Current();if(!mod||mod->VampSpellValue(target->GetPrototype()->szName,2)==1)return false;
 if(target->IsDead())return true;auto*ts=Server(target);auto*stats=ts?ts->GetMapObjectServer():nullptr;return stats&&stats->GetCurrentHP()>=stats->GetMaxHP();
}
inline bool KeepCommand(CUnitServer*server,const SUnitCommand&cmd){if(!cmd.IsCast())return false;auto*spell=Spell(server->GetUnit(),cmd.nQSpellNum);if(!spell)return false;if(!cmd.pTarget.IsEmpty()&&cmd.pTarget->WhoAmI()==HPC_UNIT&&static_cast<CUnit*>(cmd.pTarget)->IsDead())return false;return !ObsoleteHeal(server,spell,cmd.pTarget);}
struct Recent {int slot;DWORD subtype,started;};
struct Reservation {int caster,slot;DWORD subtype,expires;};
inline std::unordered_map<int,Recent> recent;
inline std::unordered_map<unsigned long long,Reservation> reservations;
inline const void* world=nullptr;
inline std::string zone;
inline DWORD lastTick=0;
inline CUnit* selectionCaster=nullptr;
inline CUnit* selectionEnemy=nullptr;
inline void Reset(){recent.clear();reservations.clear();world=nullptr;zone.clear();lastTick=0;}
inline void Sync(){const void*nowWorld=theWorldServer.GetSpecificManager();std::string nowZone=(LPCSTR)theWorldServer.szMobFileName;DWORD tick=DWORD(theWorldServer.GetSegmentTimeCounter());if(world!=nowWorld||zone!=nowZone||tick<lastTick){Reset();world=nowWorld;zone=nowZone;}lastTick=tick;}
struct Selection {CUnit*oldCaster;CUnit*oldEnemy;Selection(CUnit*caster,CUnitList&units,bool heal):oldCaster(selectionCaster),oldEnemy(selectionEnemy){selectionCaster=nullptr;selectionEnemy=nullptr;if(!heal)for(POSITION pos=units.GetHeadPosition();pos;){auto*enemy=units.GetNext(pos);if(LiveEnemy(caster,enemy)){selectionCaster=caster;selectionEnemy=enemy;break;}}}~Selection(){selectionCaster=oldCaster;selectionEnemy=oldEnemy;}};
inline bool EffectType(int type){return type==Debuff||type==CombatBuff||type==PeaceBuff;}
inline unsigned long long Key(CUnit*target,int effect){return (static_cast<unsigned long long>(static_cast<unsigned>(target->GetID()))<<32)|static_cast<unsigned>(effect);}
inline bool Reserved(CUnit*target,int effect,Reservation&result){
 auto it=reservations.find(Key(target,effect));if(it==reservations.end())return false;
 auto*owner=theWorldServer.GetUnit(it->second.caster);auto*server=owner&&!owner->IsDead()&&Enabled(owner)?Server(owner):nullptr;auto*spell=server?Spell(owner,it->second.slot):nullptr;
 bool valid=spell&&spell->GetSubtypeID()==it->second.subtype;DWORD now=DWORD(theWorldServer.GetSegmentTimeCounter());
 if(valid&&HasCommand(server,it->second.slot,target))it->second.expires=now+146;
 if(!valid||LONG(it->second.expires-now)<=0){reservations.erase(it);return false;}result=it->second;return true;
}
inline void Record(CUnit*caster,CSpell*spell,int slot,CUnit*target){Sync();DWORD now=DWORD(theWorldServer.GetSegmentTimeCounter());recent[caster->GetID()]={slot,caster->GetSubtypeID(),now};int effect=spell->GetSubtypeID()>>16;if(target&&effect>0&&EffectType(SpellType(spell)))reservations[Key(target,effect)]={caster->GetID(),slot,spell->GetSubtypeID(),now+146};}
inline float Health(CUnit*target){return target&&target->GetMaxHP()>0?(std::max)(0.f,(std::min)(1.f,float(target->GetHP())/target->GetMaxHP())):1.f;}
inline float Score(CUnit*caster,CUnit*target,CSpell*spell,int slot,int type,float average,float native){
 Sync();if(!target||target->IsDead()||!spell->IsValidTarget(caster,target))return -1e30f;
 if((spell->GetSubtypeID()>>16)==SPELL_ID_VAMP&&3+spell->GetMagicSubtype()>=int(RPG_NUM_ATTACK_TYPES))return -1e30f;
 auto*known=selectionCaster==caster&&LiveEnemy(caster,selectionEnemy)?selectionEnemy:FightingEnemy(caster);
 if(type==CombatBuff||type==PeaceBuff){auto*allyEnemy=target!=caster&&theWorldServer.fof.IsPlayerAlly(caster->GetPlayerNum(),target->GetPlayerNum())?FightingEnemy(target):nullptr;if(!known&&!LiveEnemy(caster,allyEnemy))return -1e30f;}
 int effect=spell->GetSubtypeID()>>16;bool effectType=EffectType(type);auto key=Key(target,effect);
 if(effectType&&effect>0&&target->HasSpellEffect(effect)){reservations.erase(key);return -1e30f;}
 Reservation reservation={};bool held=effectType&&effect>0&&Reserved(target,effect,reservation);bool same=HasCommand(Server(caster),slot,target);
 if(held&&(reservation.caster!=caster->GetID()||!same))return -1e30f;
 if(type==PeaceBuff&&effect==SPELL_ID_REGENERATION&&target->GetHP()>=target->GetMaxHP())return -1e30f;
 if(native<=-1e20f)return native;
 float baseline=average>0&&average<1000000?(std::max)(8.f,average):8.f;
 auto*opponent=FightingEnemy(target);NpcUtility::Context context={};context.health=Health(target);context.moving=target->IsMoving();context.engaged=opponent!=nullptr;
 context.threatensAlly=LiveEnemy(caster,target)&&opponent&&(opponent==caster||theWorldServer.fof.IsPlayerAlly(caster->GetPlayerNum(),opponent->GetPlayerNum()));auto*ts=Server(target);context.castCommand=ts&&context.threatensAlly&&ts->GetScriptCommand().IsCast();
 context.threatened=(opponent&&FightingEnemy(opponent)==target)||(known&&FightingEnemy(known)==target);
 switch(type){
 case Direct:case Area:case Continuous:{auto it=recent.find(caster->GetID());if(!same&&it!=recent.end()&&it->second.subtype==caster->GetSubtypeID()&&it->second.slot==slot){DWORD elapsed=DWORD(theWorldServer.GetSegmentTimeCounter())-it->second.started;if(elapsed<91)native-=baseline*.45f*(1-float(elapsed)/91);}break;}
 case Debuff:native+=baseline*NpcUtility::Bonus(effect==SPELL_ID_STUN?NpcUtility::Action::Stun:effect==SPELL_ID_SLOW?NpcUtility::Action::Slow:NpcUtility::Action::Debuff,context);break;
 case CombatBuff:native+=baseline*NpcUtility::Bonus(NpcUtility::Action::CombatBuff,context);break;
 case PeaceBuff:native+=baseline*(effect==SPELL_ID_REGENERATION?.65f+2*(1-context.health):effect==SPELL_ID_SPEED?(target->IsMoving()?1.1f:.7f):.7f);break;
 case Heal:native+=baseline*NpcUtility::Bonus(NpcUtility::Action::Heal,context);break;
 }return native;
}
}
