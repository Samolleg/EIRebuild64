#include "ModProgression.h"
#include "I3DScreenConstructor.h"

Json CheckProgressionG3(const std::string& directory){
    // Resource/Lua and plain stats/backup only; no screens or live actors are instantiated.
    ModRuntime::Host host(directory);auto* package=ModRuntime::Current();std::string owner;
    const auto base=(fs::path(directory).parent_path().parent_path()/"Base/res/database.res").string();
    package->ReadResource("database.res","perks.pdb",base,owner);
    if(!thePerkDatabase.LoadDatabase("",owner.c_str()))throw std::runtime_error("Progression perk DB unavailable");
    std::ifstream stream("oracle.json");Json oracle;stream>>oracle;
    Json report={{"success",true},{"luaMainExecuted",true},{"worldCreated",false},
        {"unitCreated",false},{"screenCreated",false},{"deviceCreated",false},
        {"audioStarted",false},{"gameplayActivated",false},{"native",package->CombatSetting("JMVPerks")!=0},
        {"skills",Json::array()},{"pending",Json::array()},{"camp",Json::array()},
        {"perks",Json::array()},{"fixtures",Json::array()},{"eligibility",Json::array()}};
    int checks=0;auto require=[&](bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);};
    for(auto row:oracle["skill_cases"]){
        row["allowed"]=ModProgression::SkillPending(true,row["automatic"].get<bool>(),row["index"].get<int>(),
            row["initial"].get<bool>(),row["value"].get<int>(),row["price"].get<int>(),row["points"].get<int>(),
            row["spent"].get<float>(),row["repeatLimit"].get<int>());report["skills"].push_back(row);
    }
    for(auto row:oracle["perk_cases"]){
        row["allowed"]=ModProgression::PerkPending(true,row["valid"].get<bool>(),row["price"].get<int>(),
            row["points"].get<int>(),row["spent"].get<float>());report["pending"].push_back(row);
    }
    for(auto row:oracle["camp_cases"]){row["allowed"]=ModProgression::CampAffordable(row["price"].get<int>(),row["total"].get<int>());report["camp"].push_back(row);}
    const int count=thePerkDatabase.GetNumPerks();
    for(int i=0;i<count;++i){const auto* perk=thePerkDatabase.GetPerk(i);Json codes=Json::array(),indices=Json::array();
        for(int field=3;field<=7;++field){auto code=package->ProgressionRequirementCode(perk->szCode,field);codes.push_back(code?Json(*code):Json(nullptr));indices.push_back(code?thePerkDatabase.GetPerkIndex(code->c_str()):-1);}
        report["perks"].push_back({{"id",i},{"code",std::string((LPCSTR)perk->szCode)},{"active",perk->bActive!=0},
            {"attributes",{perk->fpAttributes[0],perk->fpAttributes[1],perk->fpAttributes[2]}},
            {"skillRequired",std::string((LPCSTR)perk->szSkillRequired)},{"autoSkill",int(perk->rpgAutoSkill)},
            {"skillLevel",perk->dwSkillLevel},{"required",std::string((LPCSTR)perk->szPerkRequired)},
            {"requiredIndex",thePerkDatabase.GetPerkIndex(perk->szPerkRequired)},
            {"exclusive",perk->nPerkExclusive},{"additional",codes},{"additionalIndices",indices}});
    }
    std::vector<SRPGUnitStats> fixtures;
    auto fixture=[&](int group,int level,int low){SRPGUnitStats stats={};
        stats.psStrength.fpEffectiveValue=stats.psDexterity.fpEffectiveValue=stats.psIntelligence.fpEffectiveValue=255;
        for(int i=0;i<RPG_NUM_ACTUAL_AUTO_SKILLS;++i)stats.cAutoSkills[i]=120;
        if(group>=0)stats.cPerks[group]=BYTE(level);
        if(low==0)stats.psStrength.fpEffectiveValue=0;if(low==1)stats.psDexterity.fpEffectiveValue=0;
        if(low==2)stats.psIntelligence.fpEffectiveValue=0;
        if(low==3)for(int i=0;i<RPG_NUM_ACTUAL_AUTO_SKILLS;++i)stats.cAutoSkills[i]=0;
        fixtures.push_back(stats);Json owned=Json::array(),skills=Json::array();
        for(int i=0;i<RPG_NUM_PERKS;++i)owned.push_back(stats.cPerks[i]);
        for(int i=0;i<RPG_NUM_ACTUAL_AUTO_SKILLS;++i)skills.push_back(stats.cAutoSkills[i]);
        report["fixtures"].push_back({{"owned",owned},{"skills",skills},{"attributes",
            {stats.psStrength.fpEffectiveValue,stats.psDexterity.fpEffectiveValue,stats.psIntelligence.fpEffectiveValue}}});};
    fixture(-1,0,-1);for(int group=0;group<RPG_NUM_PERKS;++group)for(int level=0;level<=3;++level)fixture(group,level,-1);
    for(int low=0;low<4;++low)fixture(-1,0,low);
    // Also satisfy every extra minimum while leaving each candidate group empty.
    for(int candidate=0;candidate<RPG_NUM_PERKS;++candidate){fixture(-1,0,-1);auto& stats=fixtures.back();
        for(int i=0;i<RPG_NUM_PERKS;++i){stats.cPerks[i]=BYTE(i==candidate?0:3);report["fixtures"].back()["owned"][i]=stats.cPerks[i];}}
    for(int f=0;f<int(fixtures.size());++f){Json available=Json::array(),upgrade=Json::array();const auto& stats=fixtures[f];
        for(int i=0;i<count;++i){const auto* perk=thePerkDatabase.GetPerk(i);
            available.push_back(stats.CanAddPerk(&thePerkDatabase,perk)!=FALSE);
            upgrade.push_back(ModProgression::Additional(stats,
                [&](int field){return package->ProgressionRequirementCode(perk->szCode,field);},
                [&](const char* code){return thePerkDatabase.GetPerkIndex(code); }));}
        report["eligibility"].push_back({{"fixture",f},{"available",available},{"additional",upgrade}});
    }
    // Independently checked native backup layout, full36-perk tail and unaffected primary cells.
    static_assert(sizeof(SPerksBackup)==0x3c && offsetof(SPerksBackup,fExperienceCopy)==0x2c &&
        offsetof(SPerksBackup,fSpentExpCopy)==0x30 && offsetof(SPerksBackup,bModified)==0x38,"Native perk backup layout");
    SRPGUnitStats original={};for(int i=0;i<RPG_NUM_ACTUAL_AUTO_SKILLS;++i)original.cAutoSkills[i]=BYTE(20+i);
    original.cManualSkills[0]=111;for(int i=0;i<RPG_NUM_PERKS;++i)original.cPerks[i]=BYTE(i%4);
    original.fpExperience=4096.5f;original.fSpentExp=64.5f;original.fExpPenalty=-10.25f;
    original.psStrength.fpBaseValue=25;original.psDexterity.fpBaseValue=24;original.psIntelligence.fpBaseValue=26;
    SRPGUnitStats changed=original;SPerksBackup backup={};backup.bModified=true;ModProgression::Copy(original,backup);
    require(!backup.bModified && backup.cPerksCopy[35]==3 && backup.cAutoSkillsCopy[5]==25 && backup.cManualSkillsCopy[0]==111,"Backup copy/tail");
    for(int i=0;i<RPG_NUM_ACTUAL_AUTO_SKILLS;++i)changed.cAutoSkills[i]=0;changed.cManualSkills[0]=0;
    for(int i=0;i<RPG_NUM_PERKS;++i)changed.cPerks[i]=0;changed.fpExperience=1;changed.fSpentExp=2;changed.fExpPenalty=3;
    backup.bModified=true;ModProgression::Restore(changed,backup);
    require(memcmp(&original,&changed,sizeof(original))==0 && !backup.bModified,"Backup restore changed unrelated stats");
    changed.psStrength.fpBaseValue=31;ModProgression::Restore(changed,backup);
    require(changed.psStrength.fpBaseValue==31,"Backup restored primary outside contract");
    int reads=0;SRPGUnitStats plain={};
    bool stopped=ModProgression::Additional(plain,[&](int field){++reads;return std::string(field==3?"known":field==4?"unknown":"later");},
        [&](const char* code){return std::string(code)=="known"?0:std::string(code)=="later"?3:-1;});
    require(!stopped && reads==2,"Unknown lookup lost earlier failure or did not stop");
    require(!ModProgression::Minimum(plain,-1) && !ModProgression::Minimum(plain,108),"Invalid perk index accepted");
    require(!ModProgression::Index(-1,1) && !ModProgression::Index(1,1),"Pending array bounds guard");
    require(!theWorldServer.IsMapLoaded(),"Progression inspection loaded world");
    report["backup"]={{"size",sizeof(backup)},{"exp",offsetof(SPerksBackup,fExperienceCopy)},
        {"spent",offsetof(SPerksBackup,fSpentExpCopy)},{"penalty",offsetof(SPerksBackup,fExpPenaltyCopy)},
        {"modified",offsetof(SPerksBackup,bModified)},{"roundTrip",true},{"primaryPreserved",true}};
    report["assertions"]=checks;return report;
}
