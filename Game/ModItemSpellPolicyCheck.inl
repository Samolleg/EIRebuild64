#pragma once
#include "ModItemSpellPolicy.h"
#include "../SDKs/JSON/json.hpp"
#include <filesystem>
#include <fstream>
#include <shellapi.h>
#include <stdexcept>
namespace ModItemSpellPolicyCheck {
inline void Require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
inline nlohmann::json Run()
{
    using namespace ModItemSpells;
    nlohmann::json cases = nlohmann::json::array();
    Require(KeepContinuousWeapon(true,true,true,false),"Current weapon removed");
    Require(KeepContinuousWeapon(false,true,false,true),"Active left weapon removed");
    Require(!KeepContinuousWeapon(false,true,true,true) && !KeepContinuousWeapon(false,false,false,true) &&
            !KeepContinuousWeapon(false,true,false,false),"Inactive or blocked offhand retained");
    Require(IsActiveShield(true,2,2,RPGWST_HANDS1) && !IsActiveShield(true,2,1,RPGWST_HANDS1) &&
            !IsActiveShield(false,2,2,RPGWST_HANDS1) && !IsActiveShield(true,2,2,RPGWST_SWORD),"Shield selection mismatch");
    cases.push_back({{"name","equipment_current_left_shield_twohand"}});

    struct Effect { int nSpellID; float fpPower; };
    Effect effects[] = {{SPELL_ID_PROT_FIRE,5},{SPELL_ID_REGENERATION,3},{SPELL_ID_STUN,20}};
    auto at = [&](int i)->const Effect& { return effects[i]; };
    Require(Suppressed(SPELL_ID_PROT_FIRE,5,false,3,at),"Equal-power effect not suppressed");
    Require(Suppressed(SPELL_ID_PROT_FIRE,6,true,3,at),"Native <= direction lost");
    Require(!Suppressed(SPELL_ID_PROT_FIRE,4,false,3,at) && !Suppressed(SPELL_ID_PROT_ACID,6,true,3,at),"Wrong effect suppressed");
    Require(Suppressed(SPELL_ID_REGENERATION,3,true,3,at) && !Suppressed(SPELL_ID_REGENERATION,3,false,3,at),"Shield/armor regeneration sets merged");
    Require(!Suppressed(SPELL_ID_HEALING,100,true,3,at) && !Suppressed(SPELL_ID_PROT_FIRE,5,true,0,at),"Unfiltered/empty effects suppressed");
    Require(TriggerPhase(SPELL_ID_HEALING,false,true,1) && !TriggerPhase(SPELL_ID_HEALING,true,true,1),"Shield heal phase mismatch");
    Require(TriggerPhase(SPELL_ID_HEALING,true,false,1) && !TriggerPhase(SPELL_ID_HEALING,false,false,1) &&
            TriggerPhase(SPELL_ID_HEALING,false,false,0) && TriggerPhase(SPELL_ID_HEALING,false,false,2),"Armor heal flag must equal1");
    Require(TriggerPhase(SPELL_ID_PROT_FIRE,true,true,0) && !TriggerPhase(SPELL_ID_PROT_FIRE,false,false,1),"Nonheal phase mismatch");
    Require(CanTrigger(true,10,10,false) && !CanTrigger(true,9,10,false) && !CanTrigger(true,10,10,true) &&
            !CanTrigger(false,10,10,false),"Trigger/mana/filter gate mismatch");
    cases.push_back({{"name","existing_effect_phase_and_mana"}});

    Require(WeaponHealingTargetsSelf("Healing") && !WeaponHealingTargetsSelf("healing") &&
            !WeaponHealingTargetsSelf("Firearrow") && !WeaponHealingTargetsSelf(nullptr),"Healing exact-name target mismatch");
    Require(CastPointNode(true,RPGWST_SWORD,2,false)=="basesword02" &&
            CastPointNode(true,RPGWST_SWORD,2,true)=="l.basesword02" &&
            CastPointNode(true,RPGWST_HANDS7,3,true)=="l.basedstaff03", "Selected-hand node mismatch");
    Require(CastPointNode(true,RPGWST_BOW,7,true)=="basearrow00" &&
            CastPointNode(true,RPGWST_CROSSBOW,7,false)=="basearrow01" &&
            CastPointNode(false,0,0,true)=="lh3" && CastPointNode(false,0,0,false)=="baserh3" &&
            CastPointNode(true,RPGWST_HANDS1,0,true)=="rh3" && CastPointNode(true,RPGWST_HANDS1,0,false)=="lh3","Native node fallback mismatch");
    Require(!WandLacksMana(true,false,10,0) && WandLacksMana(true,true,10,9) &&
            !WandLacksMana(true,true,10,10) && !WandLacksMana(false,true,10,0),"Null wand/scroll/mana branch mismatch");
    cases.push_back({{"name","healing_hand_point_and_null_wand"}});
    return cases;
}
}
inline bool InspectItemSpellPolicyCommandLine(int& result)
{
    int count=0; auto* args=CommandLineToArgvW(GetCommandLineW(),&count); if(!args)return false;
    struct Free { LPWSTR* p; ~Free(){LocalFree(p);} } free{args};
    if(count<2 || wcscmp(args[1],L"--inspect-item-spell-policy"))return false;
    result=1; nlohmann::json report={{"success",false},{"actualProductionPolicy",true},{"rngExecuted",false}};
    for(const char* name:{"hostCreated","worldCreated","unitCreated","luaExecuted","deviceCreated","audioStarted","saveRead","uiExecuted"})report[name]=false;
    try { if(count!=3)throw std::runtime_error("Expected one report path");report["cases"]=ModItemSpellPolicyCheck::Run();report["success"]=true;result=0; }
    catch(const std::exception& error){report["error"]=error.what();}
    if(count>=3){std::ofstream file{std::filesystem::path(args[2])};file<<report.dump(2);if(!file)result=1;}
    return true;
}
