#pragma once
#include "RPGSystem.h"
#include "aiSettings.h"
#include "Tools.h"
#include <vector>

// Native UpdateStats / SetSkill display arithmetic. No RPG state is written.
namespace ModCampStats {
inline int Weight(int carried, bool baseStats) { return baseStats ? 0 : carried; }
inline int Capacity(float strength,float lift,float jewelry,float complex,float sets) {
    float base=strength*12.0f;
    base+=base*lift*0.01f;
    return int((base*jewelry*complex)*sets);
}
inline int Sight(const SRPGUnitStats& stats) {
    int sight=MINT(stats.senses[RPGS_SIGHT].fpEffectiveValue);
    sight+=stats.GetPerkModifier(11);
    return sight;
}
struct Primary { int healthPercent,staminaPercent,experiencePercent,manaPercent; };
inline Primary PrimaryValues(const SRPGUserUnitStats& shown,float hpDivisor,float mpDivisor,float experience) {
    return {int((shown.cStrength/hpDivisor)*100),int((shown.cDexterity/mpDivisor)*100),
            int(experience*100),int((25.0f/shown.cIntelligence)*100)};
}
inline int MagicSkill(const SRPGUnitStats& stats,int intelligence,int subtype) {
    float skill=stats.GetBaseMagicSkill(ERPGMagicSubtypes(subtype));
    skill+=stats.GetPerkModifier(RPGPRK_FIRE+subtype);
    return int(skill+(intelligence-25));
}
inline float MagicDamagePercent(int skill) { return (0.8f+(skill*30.0f/120.0f)/100.0f)*100.0f; }
inline int ScienceSkill(const SRPGUserUnitStats& shown) {
    return int(shown.cDexterity)-25+int(shown.cManualSkills[RPGMSKL_SCIENCE]);
}
struct PerkRow { const char* label;int value;const char* format; };
inline std::vector<PerkRow> PerkRows(int group,int modifier,bool jmv136,bool anatomy,float backstab) {
    std::vector<PerkRow> rows;
    auto add=[&](const char*label,int value,const char*format){rows.push_back({label,value,format});};
    switch(group) {
    case 0:case 10:case 17:add("ATTRIBPLUS",modifier,"%s %d");break;
    case 16:
        if(jmv136)add("DAMAGEMULT",modifier*3,"%s +%d%%");
        add("TOHITDEFENSE",modifier,"%s %d");break;
    case 24:
        if(jmv136){add("DAMAGEMULT",modifier,"%s +%d%%");add("ACTIONS",modifier,"%s +%d%%");}
        add("TOHITDEFENSE",modifier,"%s %d");break;
    case 6:
        if(jmv136)add("ACTIONS",modifier*2,"%s +%d%%");
        add("TOHITDEFENSE",modifier,"%s %d");break;
    case 7:case 8:case 9:add("SPELLBONUS",modifier,"%s %d");break;
    case 4:add("infoskill_27",modifier,"%s + %d%%");break;
    case 5:add("infoskill_28",modifier,"%s + %d%%");break;
    case 2:add("HEALTH",modifier+100,"%s %d%%");break;
    case 11:add("infoskill_29",modifier,"%s +%d");break;
    case 12:add("infoskill_30",modifier,"%s + %d%%");break;
    case 13:add("infoskill_31",modifier,"%s + %d%%");break;
    case 19:add("infoskill_32",modifier,"%s + %d%%");break;
    case 3:add("infoskill_26",modifier,"%s + %d%%");break;
    case 18:add("STAMINARGNR",modifier+100,"%s %d%%");break;
    case 14:add("ACTIONS",modifier,"%s + %d%%");break;
    case 1:add("ENCUMBRANCE",modifier+100,"%s %d%%");break;
    case 15:add("BSTABMULT",modifier+int(backstab*100-100)+100,"%s = %d%%");break;
    case 20:add(anatomy?"ANATOMYLIMBS":"infoskill_33",modifier,"%s + %d%%");break;
    case 21:add("infoskill_34",100-modifier,"%s %d%%");break;
    case 22:add("infoskill_35",modifier+100,"%s %d%%");break;
    case 23:add("STAMINA",modifier+100,"%s %d%%");break;
    }
    return rows;
}
}
