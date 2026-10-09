#pragma once
#include "ModStealingPolicy.h"
#include "../SDKs/JSON/json.hpp"
#include <filesystem>
#include <fstream>
#include <shellapi.h>
#include <stdexcept>
#include <vector>
namespace ModStealingPolicyCheck {
inline void Require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
inline nlohmann::json Run()
{
    nlohmann::json cases=nlohmann::json::array();
    Require(ModStealing::CanSteal(true,false),"Noncombat stealing blocked");
    Require(!ModStealing::CanSteal(true,true) && !ModStealing::CanSteal(false,false) &&
            !ModStealing::CanSteal(false,true),"Combat/missing player permitted");
    cases.push_back({{"name","stealing_combat_and_player_gate"}});
    Require(ModStealing::ActorSkill(25,10)==10 && ModStealing::ActorSkill(40,20)==35 &&
            ModStealing::ActorSkill(0,0)==1 && ModStealing::ActorSkill(24,1)==1 &&
            ModStealing::ActorSkill(255,255)==485,"Native user-byte skill/floor mismatch");
    cases.push_back({{"name","native_user_stat_skill_and_floor"}});

    struct Ref { int refs=1,adds=0,releases=0; void AddRef(){Require(refs>0,"Retain freed item");++refs;++adds;} void Release(){Require(refs>0,"Double release");--refs;++releases;} };
    Ref first,second,quick;
    std::vector<Ref*> inventory{&first,&second},borrowed,player;
    auto collect = [&] {
        const int expectedCollected=int(inventory.size());
        ModStealing::CollectQuestItems([&]{return int(inventory.size());},[&](int i){return inventory[i];},
            [&](Ref* item){Require(item->refs==2,"Temporary ref not retained before append");borrowed.push_back(item);},
            [&](int i){Require(int(borrowed.size())==expectedCollected,"Detach occurred before all quests retained");inventory[i]->Release();inventory.erase(inventory.begin()+i);});
    };
    collect();
    Require(inventory.empty() && borrowed.size()==2 && first.refs==1 && second.refs==1,"Wrong post-detach ownership");
    for(auto* item:borrowed){item->AddRef();player.push_back(item);}
    for(auto* item:borrowed){Require(item->refs==2,"Player ref missing before temporary release");item->Release();}
    Require(player.size()==2 && player[0]==&first && player[1]==&second && first.refs==1 && second.refs==1 &&
            first.adds==2 && first.releases==2 && second.adds==2 && second.releases==2,"Quest not owned exactly once after transfer");
    borrowed.clear();Ref single;inventory.push_back(&single);collect();
    Require(inventory.empty() && borrowed.size()==1 && single.refs==1,"Single quest detach mismatch");
    single.AddRef();borrowed.front()->Release();Require(single.refs==1 && single.adds==2 && single.releases==2,"Single quest final owner mismatch");
    borrowed.clear();collect();Require(borrowed.empty() && quick.refs==1 && quick.releases==0,"Empty quest collector removed unrelated item");
    int calls=0;Ref untouched;
    if(ModStealing::CanSteal(true,true))ModStealing::CollectQuestItems([&]{++calls;return 0;},[&](int){return &untouched;},[&](Ref*){++calls;},[&](int){++calls;});
    Require(calls==0 && untouched.refs==1 && untouched.adds==0 && untouched.releases==0,"Failed gate changed ownership");
    cases.push_back({{"name","quest_ref_hold_detach_player_release"},{"initialRefs",1},{"afterHold",2},{"afterDetach",1},{"afterPlayer",2},{"finalRefs",1}});
    return cases;
}
}
inline bool InspectStealingPolicyCommandLine(int& result)
{
    int count=0;auto* args=CommandLineToArgvW(GetCommandLineW(),&count);if(!args)return false;
    struct Free {LPWSTR* p;~Free(){LocalFree(p);}} free{args};
    if(count<2||wcscmp(args[1],L"--inspect-stealing-policy"))return false;
    result=1;nlohmann::json report={{"success",false},{"actualProductionPolicy",true},{"rngExecuted",false}};
    for(const char* name:{"hostCreated","worldCreated","unitCreated","luaExecuted","deviceCreated","audioStarted","saveRead","uiExecuted"})report[name]=false;
    try{if(count!=3)throw std::runtime_error("Expected one report path");report["cases"]=ModStealingPolicyCheck::Run();report["success"]=true;result=0;}
    catch(const std::exception& error){report["error"]=error.what();}
    if(count>=3){std::ofstream file{std::filesystem::path(args[2])};file<<report.dump(2);if(!file)result=1;}
    return true;
}
