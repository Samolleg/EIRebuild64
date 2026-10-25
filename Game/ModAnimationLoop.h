#pragma once
#include "UnitServer.h"
#include "WorldServer.h"
#include "Player.h"
#include <cmath>
#include <cctype>
#include <string>
namespace ModAnimationLoop {
// Same GS identity as addon AD11. Native command/L0 state already serialize.
inline CGSVariableSet* Variables(CUnit* unit,std::string& key) {
    auto* player=theWorldServer.GetPlayer(0);
    if(!player||!unit||theWorldServer.szMobFileName.IsEmpty())return nullptr;
    std::string zone=theWorldServer.szMobFileName.GetString();
    const auto slash=zone.find_last_of("\\/");
    if(slash!=std::string::npos)zone.erase(0,slash+1);
    for(char& c:zone)c=char(std::tolower(static_cast<unsigned char>(c)));
    key="jmv.animloop.v1."+zone+"."+std::to_string(unit->GetID());
    return &player->party.vars;
}
inline bool Read(CUnitServer* server,CGSVariableSet*& vars,std::string& key,DWORD& id) {
    vars=Variables(server?server->GetUnit():nullptr,key);
    if(!vars)return false;
    const float value=vars->GetVar(key.c_str());
    if(!std::isfinite(value)||value<1||value>16777216.0f||value!=floorf(value))return false;
    id=DWORD(value)-1;return true;
}
inline bool Matches(const SUnitCommand& command,DWORD id) {
    return command.cmd==UC_PLAYANIMCYCLE&&command.dwParam==id;
}
inline bool Housekeeping(EUnitCommand command) {
    return command==UC_NONE||command==UC_IDLE||command==UC_REST;
}
}
