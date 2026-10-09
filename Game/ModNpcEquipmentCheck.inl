#pragma once
#include "ModNpcEquipment.h"
#include "../SDKs/JSON/json.hpp"
#include <filesystem>
#include <fstream>
#include <shellapi.h>
#include <vector>
namespace ModNpcEquipmentCheck {
inline void Require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
inline nlohmann::json Run()
{
    nlohmann::json cases=nlohmann::json::array();std::vector<int> order;
    auto hp=[&]{order.push_back(1);return 24.f;};auto mp=[&]{order.push_back(3);return 50.f;};
    auto npcHP=[&]{order.push_back(4);return 30.f;};
    auto npcMP=[&]{order.push_back(6);float overload=.75f;order.push_back(7);return ModNpcEquipment::Mana(80,overload);};
    auto modify=[&](float value){order.push_back(2);return value*.5f;};
    auto hero=ModNpcEquipment::BaseResources(true,hp,mp,npcHP,npcMP,modify);
    Require(hero.hp==12 && hero.mp==50 && order==std::vector<int>({1,2,3}),"Hero selected NPC callback or overload");
    order.clear();auto npc=ModNpcEquipment::BaseResources(false,hp,mp,npcHP,npcMP,modify);
    Require(npc.hp==15 && npc.mp==60 && order==std::vector<int>({4,2,6,7}),"NPC callback/modifier/overload order mismatch");
    cases.push_back({{"name","npc_hero_callback_order"},{"luaFormulaExecuted",false}});
    Require(ModNpcEquipment::Capacity(100.75f,.2f+.05f)==125 && ModNpcEquipment::Truncate(126.9f)==126 &&
            ModNpcEquipment::Capacity(100.75f,0)==100 && ModNpcEquipment::Capacity(100.75f,-.1f)==90 &&
            ModNpcEquipment::Truncate(-1.9f)==-1 && ModNpcEquipment::Mana(80,1)==80 &&
            ModNpcEquipment::Mana(80,.75f)==60,"Native float/truncation/jewelry/overload mismatch");
    cases.push_back({{"name","jewelry_capacity_weight_overload"}});

    // Scalar callback trace: no CUnit, world, inventory manager or fabricated save.
    struct Token {int id,refs=0;};Token tokens[]={{1},{2},{3},{4}};
    struct Recipient {std::vector<Token*> items;void Add(Token* t){for(auto* old:items)if(old==t)return;items.push_back(t);++t->refs;}};
    Recipient local,remote;bool same=true,present=true;int creates=0,notifications=0;std::vector<int> properties;
    auto grant=[&]{ModNpcEquipment::GrantQuicks([&](int p){properties.push_back(p);return p==2?std::string("ring"):p==4?std::string("missing"):std::string();},
        [&](const char* name)->Token*{if(std::string(name)=="missing")return nullptr;return &tokens[creates++];},
        [&](Token*t){local.Add(t);},[&]()->Recipient*{return !present?nullptr:same?&local:&remote;},
        [&](Recipient*r,Token*t){Require(!local.items.empty()&&local.items.back()==t,"Server add preceded local add");r->Add(t);},[&]{++notifications;});};
    grant();Require(properties==std::vector<int>({2,3,4,5})&&creates==1&&notifications==1&&tokens[0].refs==1&&local.items.size()==1,"First quick grant/same pointer duplicate mismatch");
    properties.clear();grant();Require(creates==2&&notifications==2&&local.items.size()==2&&tokens[1].refs==1,"Repeat WearAll differs from native new-item creation");
    same=false;grant();Require(creates==3&&notifications==3&&remote.items.size()==1&&tokens[2].refs==2,"Distinct server recipient lost item/ref or update");
    present=false;grant();Require(creates==4&&notifications==3&&tokens[3].refs==1,"Missing server issued notify or lost local grant");
    cases.push_back({{"name","quick_grant_order_empty_missing_repeated"},{"repeatedCreates",2},{"nameIdempotence",false},{"actualInventoryExecuted",false}});
    return cases;
}
}
inline bool InspectNpcEquipmentCommandLine(int& result)
{
    int count=0;auto* args=CommandLineToArgvW(GetCommandLineW(),&count);if(!args)return false;
    struct Free {LPWSTR* p;~Free(){LocalFree(p);}} free{args};
    if(count<2||wcscmp(args[1],L"--inspect-npc-equipment"))return false;
    result=1;nlohmann::json report={{"success",false},{"actualProductionPolicy",true},{"rngExecuted",false}};
    for(const char* name:{"hostCreated","worldCreated","unitCreated","luaExecuted","deviceCreated","audioStarted","saveRead","uiExecuted"})report[name]=false;
    try{if(count!=3)throw std::runtime_error("Expected one report path");report["cases"]=ModNpcEquipmentCheck::Run();report["success"]=true;result=0;}
    catch(const std::exception& error){report["error"]=error.what();}
    if(count>=3){std::ofstream file{std::filesystem::path(args[2])};file<<report.dump(2);if(!file)result=1;}
    return true;
}
