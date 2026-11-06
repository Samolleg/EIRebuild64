#pragma once
#include "WorldServer.h"
#include "UnitServer.h"
#include "UnitDatabase.h"
#include "MapEditObject.h"
#include "ModInvincible.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

// Dejavu 421604cc78b4 unit_spawn.h: real NPCs, not party deployment.
// Saved GS owns allocation/status and the identity of each managed instance.
namespace ModUnitSpawns {
inline CPlayer* Player() { return theWorldServer.GetPlayer(0); }
inline float Read(const char* key) { auto* p=Player();return p?p->party.vars.GetVar(key):0; }
inline void Set(const char* key,float value) { if(auto* p=Player())p->party.vars.SetVar(key,value); }
inline std::string Key(unsigned id,const char* field) {
    char key[160];
    if(id)std::snprintf(key,sizeof(key),"UnitSpawn.%.80s.%u.%s",ModInvincible::Map(theWorldServer),id,field);
    else std::snprintf(key,sizeof(key),"UnitSpawn.%.80s.%s",ModInvincible::Map(theWorldServer),field);
    return key;
}
inline void Status(unsigned id,int error) {
    Set("UnitSpawn.LastID",float(id));Set("UnitSpawn.LastError",float(error));
}
inline bool Integer(float value,unsigned minimum,unsigned maximum) {
    return std::isfinite(value)&&value>=float(minimum)&&value<=float(maximum)&&value==std::floor(value);
}
inline const SUDBUnitPrototype* Prototype(const char* name) {
    if(!name||!*name)return nullptr;
    for(int i=0;i<theUnitDatabase.prototypes.GetNumEntries();++i){
        const auto* p=theUnitDatabase.prototypes[i];if(p&&!_stricmp(p->szName,name))return p;
    }
    return nullptr;
}
inline bool Managed(const CUnit& unit) {
    if(Read(Key(unsigned(unit.GetID()),"Managed").c_str())!=1)return false;
    const auto identity=std::uint64_t(unit.GetRTID());
    for(unsigned i=0;i<4;++i){
        const auto field="Owner"+std::to_string(i);
        if(Read(Key(unsigned(unit.GetID()),field.c_str()).c_str())!=float((identity>>(16*i))&0xffffu))return false;
    }
    return true;
}
inline void Remember(const CUnit& unit) {
    const auto identity=std::uint64_t(unit.GetRTID());
    for(unsigned i=0;i<4;++i){
        const auto field="Owner"+std::to_string(i);
        Set(Key(unsigned(unit.GetID()),field.c_str()).c_str(),float((identity>>(16*i))&0xffffu));
    }
    Set(Key(unsigned(unit.GetID()),"Managed").c_str(),1);
}
inline void Clear(unsigned id) {
    Set(Key(id,"Managed").c_str(),0);
    for(unsigned i=0;i<4;++i){const auto field="Owner"+std::to_string(i);Set(Key(id,field.c_str()).c_str(),0);}
}
inline unsigned Allocate() {
    const auto key=Key(0,"NextID");const float saved=Read(key.c_str());
    unsigned next=Integer(saved,9800000,16000000)?unsigned(saved):9800000;
    for(unsigned i=0;i<65536;++i){
        if(next>=16000000)next=9800000;
        const unsigned id=next++;
        if(theWorldServer.GetMapObject(int(id)))continue;
        Set(key.c_str(),float(next));return id;
    }
    return 0;
}
inline unsigned Spawn(float requested,const char* name,float side,float x,float y,float z) {
    Status(0,1);
    if(!Player()||!theWorldServer.IsMapLoaded()||!Integer(requested,0,16777215)||!Integer(side,0,31))return 0;
    const auto* prototype=Prototype(name);
    if(!prototype||prototype->dwRaceIndex>=unsigned(theUnitDatabase.races.GetNumEntries())){Status(0,3);return 0;}
    const auto* race=theUnitDatabase.races[int(prototype->dwRaceIndex)];
    if(!race||(race->tidTypeID!=MOTID_HUMAN&&race->tidTypeID!=MOTID_MONSTER&&race->tidTypeID!=MOTID_ANIMAL)){Status(0,3);return 0;}
    auto& map=theWorldServer.aiMap;
    if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z)||std::abs(z)>10000||x<0||y<0||x>=AITile2Meter(float(map.XSize()))||y>=AITile2Meter(float(map.YSize()))){Status(0,4);return 0;}
    unsigned id=unsigned(requested);
    if(id&&theWorldServer.GetMapObject(int(id))){Status(0,2);return 0;}
    if(!id)id=Allocate();if(!id){Status(0,5);return 0;}

    CMEUnit description;
    description.p_nID=int(id);description.p_tidTypeID=race->tidTypeID;
    description.p_ptPosition=CPlot3D(x,y,z);description.p_qRotation=QNULL;
    description.p_szName.Format("SpawnedUnit_%u",id);
    description.p_szTemplate=race->szMaskName;
    description.p_szParentTemplate=description.p_szUnitPrototype=prototype->szName;
    description.p_Complection={prototype->fStrength,prototype->fGrace,prototype->fTall};
    description.p_isUseInScript=true;description.p_isShadow=true;description.p_nPlayer=BYTE(side);
    const auto skin=prototype->dwSkinIndex;
    if(!race->szPrimaryTextures.empty())description.p_szPrimTexture=race->szPrimaryTextures[(std::min)(size_t(skin),race->szPrimaryTextures.size()-1)];
    if(!race->szSecondaryTextures.empty())description.p_szSecTexture=race->szSecondaryTextures[(std::min)(size_t(skin),race->szSecondaryTextures.size()-1)];
    for(auto& logic:description.p_sLogic)logic.ptGuardPoint=description.p_ptPosition;
    // Validate body, floor and occupants before publishing/indexing the NPC.
    const int created=theWorldServer.AddMapObject(&description,true);
    if(created<0){Status(0,created==-2?4:5);return 0;}
    if(created!=int(id)){theWorldServer.RemoveMapObject(created);Status(0,5);return 0;}
    auto* unit=theWorldServer.GetUnit(created);
    if(!unit||unit->IsInvalid()){
        theWorldServer.RemoveMapObject(created);Status(0,5);return 0;
    }
    auto* server=dynamic_cast<CUnitServer*>(unit->GetUnitSpecific());
    if(!server){theWorldServer.RemoveMapObject(created);Status(0,5);return 0;}
    server->logic.SetLogics(description.p_sLogic,N_NUMBER_OF_ALARM);
    Remember(*unit);Status(id,0);return id;
}
inline bool Despawn(float requested) {
    Status(0,1);
    if(!Player()||!theWorldServer.IsMapLoaded()||!Integer(requested,1,16777215))return false;
    const auto id=unsigned(requested);auto* object=theWorldServer.GetMapObject(int(id));
    if(!object){Clear(id);Status(id,0);return true;}
    auto* unit=dynamic_cast<CUnit*>(object);
    if(!unit||unit->IsInvalid()||unit->GetOwner()||!Managed(*unit)){Status(0,6);return false;}
    // Native final removal forgets attacks, routes and script/client references.
    // A corpse is a valid managed instance; GS ownership clears on success.
    theWorldServer.RemoveMapObject(int(id));
    const bool gone=theWorldServer.GetMapObject(int(id))==nullptr;
    if(gone)Clear(id);Status(gone?id:0,gone?0:5);return gone;
}
} // namespace ModUnitSpawns
