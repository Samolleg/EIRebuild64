#pragma once
#include "ModEncounter.h"
#include "UnitServer.h"
#include "WorldClient.h"
#include "MapObjectServer.h"
#include "AnimationSystem2.h"
#include <cfloat>
#include <cstdlib>

namespace ModArena {
inline float Status(const CUnit& unit,const char* field) {
    char key[100];std::snprintf(key,sizeof(key),"A70.H%u.%s",unsigned(unit.GetID()),field);
    return ModEncounter::Read(theWorldServer,key);
}
inline void Status(const CUnit& unit,const char* field,float value) {
    char key[100];std::snprintf(key,sizeof(key),"A70.H%u.%s",unsigned(unit.GetID()),field);
    ModEncounter::Set(theWorldServer,key,value);
}
inline bool Active() {
    return ModEncounter::Arena(theWorldServer)&&ModEncounter::Read(theWorldServer,"A62.Active")==1;
}
inline void SpeedChanged(CUnit& unit) {
    if(auto* server=dynamic_cast<CUnitServer*>(unit.GetUnitSpecific()))server->ScriptSpeedChanged();
}
inline float Slow(const CUnit& unit) {
    return !unit.IsDead()&&Active()&&Status(unit,"IceEnd")>theWorldServer.GetSegmentTimeCounter()?.25f:1.f;
}
inline void Clear(CUnit& unit) {
    if(Status(unit,"IceLease")==1)SpeedChanged(unit);
    for(const char* field:{"ShockEnd","BurnEnd","BurnNext","IceEnd","IceLease"}) {
        if(Status(unit,field)!=0)Status(unit,field,0);
    }
}
inline void ApplyStatus(CUnit& unit,int kind) {
    if(ModInvincible::Mask(&unit)&ModInvincible::Bad)return;
    const float now=float(theWorldServer.GetSegmentTimeCounter());
    if(kind==1)Status(unit,"ShockEnd",now+90);
    if(kind==2){Status(unit,"BurnEnd",now+90);Status(unit,"BurnNext",now+18);}
    if(kind==3){Status(unit,"IceEnd",now+54);Status(unit,"IceLease",1);SpeedChanged(unit);}
}
inline void Tick(CUnit* unit) {
    if(!ModEncounter::Arena(theWorldServer)||!ModInvincible::UnitValid(unit,true))return;
    const int now=theWorldServer.GetSegmentTimeCounter();const bool active=Active();
    if(Status(*unit,"IceLease")==1&&(!active||now>=Status(*unit,"IceEnd"))) {
        Status(*unit,"IceLease",0);Status(*unit,"IceEnd",0);SpeedChanged(*unit);
    }
    if(!active){Clear(*unit);return;}
    auto* server=dynamic_cast<CUnitServer*>(unit->GetUnitSpecific());if(!server)return;
    float next=Status(*unit,"BurnNext"),end=Status(*unit,"BurnEnd");
    for(int i=0;i<5&&next>0&&next<=now&&next<=end&&ModInvincible::UnitValid(unit,true);++i) {
        server->ApplyDirectDamage(server->GetMapObjectServer()->GetMaxHP()*.02f);next+=18;
    }
    if(next>end)next=0;
    if(Status(*unit,"BurnNext")!=next)Status(*unit,"BurnNext",next);
}
// Wrap the original regeneration, including its cadence/consumable/awareness
// observers. Only positive HP, MP and six limb deltas are rolled back.
struct Regeneration {
    bool blocked=false;
    float hp=0,mp=0,limbs[6]{};
    Regeneration(const SRPGCommonStats& common,const SRPGUnitStats& stats,CUnit* unit) {
        blocked=Active()&&ModInvincible::UnitValid(unit,true)&&Status(*unit,"ShockEnd")>theWorldServer.GetSegmentTimeCounter();
        if(blocked){hp=common.ssHP.fpCurrent;mp=common.ssMP.fpCurrent;for(int i=0;i<6;++i)limbs[i]=stats.hls[i].fpHP;}
    }
    bool Finish(SRPGCommonStats& common,SRPGUnitStats& stats,CUnit* unit) const {
        if(!blocked)return false;bool changed=false;
        if(common.ssHP.fpCurrent>hp){common.ssHP.fpCurrent=hp;changed=true;}
        if(common.ssMP.fpCurrent>mp){common.ssMP.fpCurrent=mp;changed=true;}
        for(int i=0;i<6;++i)if(stats.hls[i].fpHP>limbs[i]){stats.hls[i].fpHP=limbs[i];changed=true;}
        if(changed){unit->SetUpdateMask(CMapObject::VLD_MASK_STATS);unit->SetUpdateMask(CMapObject::VLD_MASK_UNIT_STATS);}
        return changed;
    }
};
inline int Motion(CUnit* unit) {
    if(!ModEncounter::Arena(theWorldServer)||!ModInvincible::UnitValid(unit)||!unit->IsMoving())return 0;
    CUnit* visual=unit;
    auto* client=theWorld.GetUnit(unit->GetID());
    if(client&&!client->IsInvalid()&&client->GetSubtypeID()==unit->GetSubtypeID())visual=client;
    const auto* system=visual->GetAnimationSystem2();
    const auto* entry=system?system->GetCurrentAnimation():nullptr;
    if(unit->GetCurrentPose()!=UP_RUN||!entry)return 1;
    return (entry->dwFlags&0x003c0000)==0x00100000&&
        (entry->dwFlags&0xc0000000)!=0xc0000000?2:1;
}
inline bool Damage(CUnit* unit,float power,float element) {
    if(!Active()||!ModInvincible::UnitValid(unit,true)||!std::isfinite(power)||power<=0||power>100000||
        !std::isfinite(element)||element<1||element>3||element!=std::floor(element))return false;
    auto* server=dynamic_cast<CUnitServer*>(unit->GetUnitSpecific());
    if(!server||ModInvincible::World(unit)!=&theWorldServer)return false;
    SRPGAttackPortion portion;portion.Clear();
    portion.fpAttacks[element==1?5:element==2?3:4]=1;
    portion.fpToHit=FLT_MAX;portion.fpDamage=power;portion.hl=RPGHL_ALL;
    portion.bAreaEffect=TRUE;portion.fBackStabMultiplier=1;
    server->ProcessAttackPortion(&portion,nullptr,nullptr);
    if(ModInvincible::UnitValid(unit,true))server->ApplyDirectDamage(50.f);
    if(ModInvincible::UnitValid(unit,true))ApplyStatus(*unit,int(element));
    return true;
}
inline void ClearSavedStatuses(CWorldServer& world) {
    auto* player=world.GetPlayer(0);if(!player)return;
    std::vector<std::string> erase;
    for(const auto& var:player->party.vars) {
        if(var.first.compare(0,5,"A70.H"))continue;
        const auto dot=var.first.find_last_of('.');if(dot==std::string::npos)continue;
        const auto field=var.first.substr(dot+1);
        if(field=="IceLease"&&var.second==1){
            char* end=nullptr;const unsigned long id=std::strtoul(var.first.c_str()+5,&end,10);
            if(end==var.first.c_str()+dot&&id<=2147483647ul)if(auto* unit=world.GetUnit(int(id)))SpeedChanged(*unit);
        }
        if(field=="ShockEnd"||field=="BurnEnd"||field=="BurnNext"||field=="IceEnd"||field=="IceLease")erase.push_back(var.first);
    }
    for(const auto& key:erase)player->party.vars.DelVar(key.c_str());
}
} // namespace ModArena
