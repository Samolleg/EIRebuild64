#pragma once
#include "RPGSystem.h"
#include <algorithm>
#include <array>
#include <string>
#include <vector>

// Read-only camp presentation; does not call combat AttackParams or write RPG state.
namespace ModCampDescriptions {
struct Weapon {
    bool present=false;
    int type=0,weightClass=0,attack=0,defense=0;
    float shapeSkill=0;
    float minimum=0,maximum=0,criticalChance=0,criticalPower=0,parry=0;
    std::array<float,RPG_NUM_ATTACK_TYPES> distribution={};
};
struct Input {
    const SRPGUnitStats* stats=nullptr;
    const SRPGUserUnitStats* shown=nullptr;
    Weapon right,left;
    int carried=0,capacity=1,armorAttack=0,armorDefense=0,difficulty=0;
    float complexDamage=0,complexBackstab=0,jewelryRestore=0,armorRestore=0,raceMP=0;
    float overloadParryRight=0,overloadParryLeft=0;
    float block=0,blockPower=0,armorHearing=0,armorSight=0,armorCritChance=0,armorCritMaterial=0,jewelryCritChance=0,jewelryCritPower=0;
};
struct Row {
    std::string label;float value=0,second=0;
    const char* format="%.0f";
    int damageType=-1;
    bool heading=false;
};
struct Damage {
    int type=0;bool left=false;
    float rawMinimum=0,rawMaximum=0,minimum=0,maximum=0,skill=0,attribute=0,perk=0,overload=0,complex=0,sum=0,shownSum=0;
};
struct Result {
    int attack=0,defense=0;
    float mpRate=0,mpBase=0,mpPerSecond=0,mpSeconds=0,backstab=0;
    std::vector<Row> attackRows,defenseRows,overloadRows,mpRows,damageRows;
    std::vector<Damage> damages;
};
inline bool Ranged(int type){return type==RPGWST_BOW||type==RPGWST_CROSSBOW||type==RPGWST_HANDS3||type==RPGWST_HANDS7;}
inline bool TwoHanded(int type){return type==RPGWST_HANDS2||type==RPGWST_HANDS4||type==RPGWST_HANDS5||type==RPGWST_HANDS6||type==RPGWST_HANDS7;}
inline int Armor(int raw,int relief){return raw+(std::max)(0,(std::min)(-raw,relief));}
inline float Absorption(float raw,int type,float armory,float flow){return raw+raw*(type<3?armory:flow)*.01f;}
inline float MpOverload(int weight,int capacity){float ratio=weight==0?0:float(weight)/float(capacity);return ratio>=1.5f?.01f:ratio>=1.25f?.3f:ratio>=1?.5f:ratio>=.8f?.8f:ratio>=.65f?.9f:1;}
// Native overload tooltip deliberately differs from ordinary parry display.
inline float OverloadParry(float right,float left,float factor,bool dual){
    if(!dual)return right*.01f;
    float a=right*.01f-right*factor*.01f,b=left*.01f-left*factor*.01f;
    if(a<.0001f){if(b<.0001f)b=a*100;a=b*100;}
    else if(b<.0001f)b=a*100;
    else a=(a+b-2*a*b)*100;
    return a;
}
// Same rows in render and hover, independent of damage mixture/actor changes.
inline int WeaponHover(int row,bool weapon,int damageRows){
    if(row>=1&&row<=4)return 4;
    if(weapon){if(row==7||row==8)return 5;if(row>=9&&row<9+damageRows)return 7;}
    int weight=weapon?10+damageRows:11;
    if(row==weight)return 0;if(row==weight+2)return 6;return -1;
}
}
