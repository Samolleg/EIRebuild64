#pragma once
#include "ModInvincible.h"
#include "UnitServer.h"
#include "WorldClient.h"
#include "NavigationObject.h"
#include <climits>

namespace ModScriptMotion {
inline std::string Key(const CUnit& unit,const char* prefix,const char* field) {
    char key[160];std::snprintf(key,sizeof(key),"%s.%.80s.%u.%s",prefix,ModInvincible::Map(theWorldServer),unsigned(unit.GetID()),field);return key;
}
inline float Read(const CUnit& unit,const char* prefix,const char* field) {
    auto* player=theWorldServer.GetPlayer(0);return player?player->party.vars.GetVar(Key(unit,prefix,field).c_str()):0;
}
inline void Set(const CUnit& unit,const char* prefix,const char* field,float value) {
    if(auto* player=theWorldServer.GetPlayer(0))player->party.vars.SetVar(Key(unit,prefix,field).c_str(),value);
}
inline void RestoreMovement(CUnit& unit) {
    unit.scriptMovementEnabled=false;unit.scriptMovementOffset=0;unit.scriptMovementFactor=1;
    if(!ModRuntime::Current()||!theWorldServer.GetPlayer(0)||Read(unit,"MovementModifier","enabled")!=1)return;
    const float offset=Read(unit,"MovementModifier","offset"),factor=Read(unit,"MovementModifier","factor");
    if(!std::isfinite(offset)||!std::isfinite(factor)||factor<=0)return;
    unit.scriptMovementEnabled=true;unit.scriptMovementOffset=offset;unit.scriptMovementFactor=factor;
}
inline bool SetMovement(CUnit* unit,float offset,float factor) {
    if(!ModInvincible::UnitValid(unit)||!std::isfinite(offset)||!std::isfinite(factor)||offset<-10||offset>10||factor<=0||factor>10)return false;
    const bool enabled=offset!=0||factor!=1;if(enabled&&unit->IsDead())return false;
    auto* server=dynamic_cast<CUnitServer*>(unit->GetUnitSpecific());if(!server||!theWorldServer.GetPlayer(0))return false;
    const unsigned protection=ModInvincible::Mask(unit);
    if(((offset<0||factor<1)&&(protection&ModInvincible::Bad))||((offset>0||factor>1)&&(protection&ModInvincible::Buffs)))return false;
    const bool changed=Read(*unit,"MovementModifier","enabled")!=(enabled?1.f:0.f)||Read(*unit,"MovementModifier","offset")!=offset||Read(*unit,"MovementModifier","factor")!=factor;
    Set(*unit,"MovementModifier","offset",offset);Set(*unit,"MovementModifier","factor",factor);Set(*unit,"MovementModifier","enabled",enabled?1.f:0.f);
    RestoreMovement(*unit);if(changed)server->ScriptSpeedChanged();return true;
}

inline bool SetRotate(const char* text,float degrees) {
    if(!text||!*text||!std::isfinite(degrees))return false;
    unsigned id=0;
    for(const char* p=text;*p;++p) {
        if(*p<'0'||*p>'9'||id>(unsigned(INT_MAX)-unsigned(*p-'0'))/10)return false;
        id=id*10+unsigned(*p-'0');
    }
    auto* object=theWorldServer.GetMapObject(int(id));if(!object||object->IsInvalid())return false;
    float w,x,y,z;object->GetOrientation().GetComponents(w,x,y,z);
    const float norm=w*w+x*x+y*y+z*z;if(!std::isfinite(norm)||norm<.0001f)return false;
    const float yaw=std::remainder(degrees,360.f)*FP_PI/180.f;
    const float current=std::atan2(-2*(x*y-w*z),norm-2*(x*x+z*z));
    const float delta=std::remainder(yaw-current,FP_2PI),c=std::cos(delta/2),s=std::sin(delta/2),inv=1/std::sqrt(norm);
    const CQuaternion rotation((c*w-s*z)*inv,(c*x-s*y)*inv,(c*y+s*x)*inv,(c*z+s*w)*inv);
    auto* unit=dynamic_cast<CUnit*>(object);
    auto* server=unit?dynamic_cast<CUnitServer*>(unit->GetUnitSpecific()):nullptr;
    if(server&&server->IsL1Moving()&&std::abs(delta)<.000001f)return true;
    const bool turned=server&&server->TurnScriptPath(delta);
    if(!unit&&NavigationObject(object)&&object->WhoAmI()!=HPC_MAGICTRAP) {
        // The existing scene owner publishes the rotated physical placement.
        const auto axis=rotation.RotateX();const auto p=object->GetCP();
        theWorldServer.aiMap.Navigation().TransformObject(int(id),{p.x,p.y,p.z},std::atan2(axis.y,axis.x),theWorldServer.aiMap.Navigation().ObjectPresent(int(id)));
    } else object->SetOrientation(rotation);
    if(server&&!turned)server->FaceScriptRoute(yaw-FP_PI2);
    object->SetUpdateMask(CMapObject::VLD_MASK_POSROT);
    if(turned)return true; // the normal unit segment publishes the pending route
    object->UpdateFrame();
    auto* client=theWorld.GetMapObject(int(id));
    if(client&&client!=object&&!client->IsInvalid()&&client->GetSubtypeID()==object->GetSubtypeID()) {
        client->SetOrientation(rotation);
        if(auto* peer=dynamic_cast<CUnit*>(client)) {
            if(auto* specific=peer->GetUnitSpecific())if(auto* route=specific->GetRoute()){route->Face(yaw-FP_PI2);peer->LowNewRoute();}
        } else client->ResetFlag(OBJ_STATIC|OBJ_LIGHTED);
    }
    return true;
}
} // namespace ModScriptMotion
