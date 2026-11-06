#pragma once
#include "ModInvincible.h"
#include "Script.h"

// Dejavu 421604cc78b4 encounter lifecycle. Facts are read at the request;
// no actor pointers, detection flags or access locks survive a world change.
namespace ModEncounter {
inline bool Named(const CWorldServer& world,const char* name) {
    const char* map=ModInvincible::Map(world);
    const size_t n=strlen(name);
    return !_strnicmp(map,name,n)&&(!map[n]||!_stricmp(map+n,".mob"));
}
inline bool Hard(const CWorldServer& world) { return Named(world,"gz19g_hard"); }
inline bool Arena(const CWorldServer& world) {
    return ModRuntime::Current()&&(Named(world,"gz19g")||Hard(world));
}
inline bool Curse(const CWorldServer& world) {
    return ModRuntime::Current()&&(Named(world,"zone20")||Named(world,"gz20g")||Named(world,"zone20_rework"));
}
inline float Read(CWorldServer& world,const char* key) {
    auto* player=world.GetPlayerManager()?world.GetPlayer(0):nullptr;return player?player->party.vars.GetVar(key):0;
}
inline void Set(CWorldServer& world,const char* key,float value) {
    if(auto* player=world.GetPlayer(0))player->party.vars.SetVar(key,value);
}
inline bool CurseActive(CWorldServer& world) {
    return Curse(world)&&Read(world,"ZC.Enabled")==1&&Read(world,"ZC.Active")==1;
}
inline bool Hero(CWorldServer& world,const CUnit* unit) {
    auto* player=world.GetPlayer(0);return player&&unit&&unit->GetOwner()==player;
}
inline bool AccessLocked(CWorldServer& world=theWorldServer) {
    if(Curse(world))return CurseActive(world);
    if(!Arena(world)||Read(world,"EFC.Enabled")!=1)return false;
    if(Read(world,"EF.Playing")==1)return true;
    const float stage=Read(world,"EFC.Stage");
    return stage>=1&&stage<8&&Read(world,"FGL.Won")!=1;
}
inline bool InBox(const CMapObject& object,float left,float right,float bottom,float top) {
    const auto p=object.GetCP();
    return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&
        p.x>=left&&p.x<=right&&p.y>=bottom&&p.y<=top&&p.z>-10;
}
inline bool CurseEnemy(CWorldServer& world,int id) {
    if(id<710001||id>=710013)return false;
    char key[64];std::snprintf(key,sizeof(key),"ZC.Enemy%d.On",id-710001);
    return Read(world,key)==1;
}
// Client render membership only. The same fact feeds the host draw list and
// the retained renderer snapshot; UNIT_VISIBLE on the live actor is untouched.
inline bool Draw(const CMapObject& object) {
    auto& world=theWorldServer;
    if(object.IsInvalid())return false;
    const int id=object.GetID();
    if(Curse(world)) {
        const bool declared=id==666666||id==640706||id==49435758||id==49435759||CurseEnemy(world,id);
        return declared&&InBox(object,54,106,46,102);
    }
    if(!Arena(world))return false;
    if(Hard(world)&&Read(world,"FGL.Active")==1&&
        (id==Read(world,"HG.ObeliskID")||id==Read(world,"HG.GuardID")))
        return InBox(object,28,100,26,62);
    if(Read(world,"EFC.Enabled")==1&&(id==1000105||id==990400))
        return InBox(object,20,108,20,108);
    return id==990400&&InBox(object,28,100,26,62);
}
inline void PinVisible(CWorldServer& world,CPlayer* player,CScriptObjectGroup& input) {
    if((!Arena(world)&&!Curse(world))||!player||player!=world.GetPlayer(0))return;
    // x64's input is a ref-counted unit group. Null/non-unit entries must not
    // reach SendVisibleUnitsList, which consumes GetUnit(). Static props already
    // travel through the normal object update stream and need no unit cast.
    CScriptObjectGroup valid;
    for(int i=0;i<input.GetSize();++i) {
        auto* unit=input.GetAt(i).GetUnit();
        if(ModInvincible::UnitValid(unit))valid.Add(unit);
    }
    input=valid;
    auto append=[&](int id,bool alive=false) {
        auto* unit=world.GetUnit(id);
        if(ModInvincible::UnitValid(unit,alive))input.Add(unit);
    };
    if(Curse(world)) {
        for(int id:{666666,640706,49435758,49435759,720101,720102})append(id);
        for(int i=0;i<12;++i)if(CurseEnemy(world,710001+i))append(710001+i,true);
        return;
    }
    if(Read(world,"EFC.Enabled")==1) {
        if(Hard(world))for(const char* key:{"HG.ObeliskID","HG.GuardID"}) {
            const float id=Read(world,key);
            if(std::isfinite(id)&&id>=1&&id<=16777215&&id==std::floor(id))append(int(id),true);
        }
        for(int id:{1000105,990400}) {
            auto* unit=world.GetUnit(id);
            if(ModInvincible::UnitValid(unit,true)&&InBox(*unit,20,108,20,108))input.Add(unit);
        }
    } else {
        auto* unit=world.GetUnit(990400);
        if(ModInvincible::UnitValid(unit,true)&&InBox(*unit,28,100,26,62))input.Add(unit);
    }
}
inline bool AckMuted(const CUnit* unit) {
    auto& world=theWorldServer;
    if(!Arena(world)||!ModInvincible::UnitValid(unit))return false;
    auto* server=world.GetUnit(unit->GetID());
    const float stage=Read(world,"EFC.Stage");
    return server&&Hero(world,server)&&Read(world,"EFC.Enabled")==1&&(stage==1||stage==2);
}
inline std::string PerformanceKey(const CUnit& unit) {
    return "EFC.Performance."+std::string(ModInvincible::Map(theWorldServer))+"."+std::to_string(unit.GetID());
}
inline void ClearPerformanceKeys(CWorldServer& world) {
    auto* player=world.GetPlayer(0);if(!player)return;
    const std::string prefix="EFC.Performance."+std::string(ModInvincible::Map(world))+".";
    std::vector<std::string> erase;
    for(const auto& var:player->party.vars)if(!var.first.compare(0,prefix.size(),prefix))erase.push_back(var.first);
    for(const auto& key:erase)player->party.vars.DelVar(key.c_str());
}
} // namespace ModEncounter
