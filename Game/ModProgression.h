#pragma once
#include "RPGSystemConsts.h"

// Native LS progression rules. Values are borrowed; no actor/UI ownership here.
namespace ModProgression {
inline bool Index(int index,int count){return index>=0 && index<count;}
inline bool PerkPending(bool native,bool valid,int price,int points,float spent){
    return valid && (native ? !(spent>1000000000.f) : price<=points);
}
inline bool SkillPending(bool native,bool automatic,int index,bool initial,
                         int value,int price,int points,float spent,int repeatLimit){
    if(!Index(index,automatic?int(RPG_NUM_ACTUAL_AUTO_SKILLS):int(RPG_NUM_ACTUAL_MANUAL_SKILLS)))return false;
    const int limit=initial?(native?120:repeatLimit):repeatLimit;
    if(value>=limit || price>points)return false;
    if(native && initial && (spent>1000000000.f || (automatic && (index==4 || index==5))))return false;
    return true;
}
inline int CurrentRepeatLimit(bool mod,bool automatic,int index){
    // Current installed five configured maxima120; original Science/Backstab100.
    return mod && automatic && index!=RPGASKL_BACKSTAB ? 120 : 100;
}
inline bool CampAffordable(int price,int total){return price<=total;}
template<class Stats> bool Minimum(const Stats& stats,int index){
    return Index(index,int(RPG_NUM_PERKS)*3) && stats.cPerks[index/3]>=index%3+1;
}
template<class Stats,class Reader,class Lookup>
bool Additional(const Stats& stats,Reader read,Lookup lookup){
    bool valid=true;
    for(int field=3;field<=7;++field){
        const auto code=read(field);const int index=lookup(code.c_str());
        // Failed/sentinel lookup terminates the native sequence, retaining earlier failures.
        if(index<0)break;
        if(!Minimum(stats,index))valid=false;
    }
    return valid;
}
template<class Stats,class Backup> void Copy(const Stats& stats,Backup& backup){
    for(int i=0;i<RPG_NUM_ACTUAL_AUTO_SKILLS;++i)backup.cAutoSkillsCopy[i]=stats.cAutoSkills[i];
    for(int i=0;i<RPG_NUM_ACTUAL_MANUAL_SKILLS;++i)backup.cManualSkillsCopy[i]=stats.cManualSkills[i];
    for(int i=0;i<RPG_NUM_PERKS;++i)backup.cPerksCopy[i]=stats.cPerks[i];
    backup.fExperienceCopy=stats.fpExperience;backup.fSpentExpCopy=stats.fSpentExp;
    backup.fExpPenaltyCopy=stats.fExpPenalty;backup.bModified=false;
}
template<class Stats,class Backup> void Restore(Stats& stats,Backup& backup){
    for(int i=0;i<RPG_NUM_ACTUAL_AUTO_SKILLS;++i)stats.cAutoSkills[i]=backup.cAutoSkillsCopy[i];
    for(int i=0;i<RPG_NUM_ACTUAL_MANUAL_SKILLS;++i)stats.cManualSkills[i]=backup.cManualSkillsCopy[i];
    for(int i=0;i<RPG_NUM_PERKS;++i)stats.cPerks[i]=backup.cPerksCopy[i];
    stats.fpExperience=backup.fExperienceCopy;stats.fSpentExp=backup.fSpentExpCopy;
    stats.fExpPenalty=backup.fExpPenaltyCopy;backup.bModified=false;
}
}
