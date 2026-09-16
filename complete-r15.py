from pathlib import Path
r=Path('work/eirebuild-local')
def edit(f,a,b):
 p=r/f;v=p.read_bytes();nl=b'\r\n' if b'\r\n' in v else b'\n';x=a.encode().replace(b'\n',nl);y=b.encode().replace(b'\n',nl);assert v.count(x)==1,(f,a[:40],v.count(x));p.write_bytes(v.replace(x,y))
edit('Game/ModStatRules.h','#include "Unit.h"','#include "Unit.h"\n#include "GSVars.h"')
p=r/'Game/ModPackage.cpp';v=p.read_bytes().replace(b'theWorldClient.',b'theWorld.');p.write_bytes(v)
edit('Game/ModPackage.h','void RecalcUnitStats(SRPGUnitStats&','bool RecalcUnitStats(SRPGUnitStats&')
edit('Game/ModPackage.cpp','void CModPackage::RecalcUnitStats(SRPGUnitStats&','bool CModPackage::RecalcUnitStats(SRPGUnitStats&')
edit('Game/ModPackage.cpp','frame.scope->CommitItems();stats=staged;common=stagedCommon;stun.committed=true;','frame.scope->CommitItems();stats=staged;common=stagedCommon;stun.committed=true;return npc&&npcTrickster&&staged.cPerks[11]>0;')
edit('Game/RPGSystem.cpp','mod->RecalcUnitStats(*this,*lpCommonStats,*pUnit);\n        if(!pUnit->GetOwner() && cPerks[11]>0) pUnit->RecalcAIParams();','bool refreshAI=mod->RecalcUnitStats(*this,*lpCommonStats,*pUnit);\n        if(refreshAI) pUnit->RecalcAIParams();')
edit('Game/ModRuntime.h','const std::string &itemArchive);','const std::string &itemArchive,const std::string &unitArchive);')
edit('Game/ModRuntime.cpp','const std::string &itemArchive) {','const std::string &itemArchive,const std::string &unitArchive) {')
edit('Game/ModRuntime.cpp','#include "UnitServer.h"','#include "UnitServer.h"\n#include "UnitDatabase.h"\n#include "ModStatRules.h"\n#include "WorldServer.h"\n#include "ServerSpecificManager.h"')
edit('Game/ModRuntime.cpp','CPerkDatabase previous;SItemDatabase previousItems;','CPerkDatabase previous;SItemDatabase previousItems;SUnitDatabase previousUnits;')
# both constructor and destructor swaps
p=r/'Game/ModRuntime.cpp';v=p.read_bytes().replace(b'std::swap(previousItems,theItemDatabase);',b'std::swap(previousItems,theItemDatabase);std::swap(previousUnits,theUnitDatabase);');p.write_bytes(v)
edit('Game/ModRuntime.cpp','    SRPGUnitStats stats = {};','''    const fs::path unitPath(unitArchive);
    if(!theUnitDatabase.Load((unitPath.parent_path().string()+"\\\\").c_str(),unitPath.filename().string().c_str()))throw std::runtime_error("Native unit DB failed to load");
    SRPGUnitStats stats = {};''')
edit('Game/ModPackage.cpp','report["engineDispatch"]=ModRuntime::CheckEngineDispatch(*this,state,perkOwner,itemOwner);','std::string unitOwner;ReadResource("database.res","units.udb","",unitOwner);if(unitOwner.empty())throw std::runtime_error("Package has no units.udb owner");\n report["engineDispatch"]=ModRuntime::CheckEngineDispatch(*this,state,perkOwner,itemOwner,unitOwner);')
edit('Game/ModRuntime.cpp','int preparedCases=0,npcCases=0;','int preparedCases=0,npcCases=0,fullCases=0;')
edit('Game/ModRuntime.cpp','void SelectWeapon(int slot){dwCurrentWeapon=slot;}','void SelectWeapon(int slot){dwCurrentWeapon=slot;}\n            void StrongMan(int value){rpgUserUnitStats.cPerks[29]=BYTE(value);}')
a='''        // Actual native hero recalculation, with live perk DB and injured limb states.'''
b='''        // Actual native RecalcStats callsite, with native ownership and real UDB/race records.
        CServerSpecificManager specificManager;specificManager.SetWorld(&theWorldServer);
        for(int i=0;i<theUnitDatabase.prototypes.GetNumEntries() && fullCases<8;i++){
            auto*prototype=theUnitDatabase.prototypes.GetEntry(i);std::string name=(LPCSTR)prototype->szName;
            auto exp=package.Lookup("UnitStat",name,2,0); // exact eligibility is checked via the live Lua table below
            lua_getglobal(lua,"GetUnitStats");
            // Use direct table lookup for positive XP and strength to select meaningful real NPC fixtures.
            lua_pop(lua,1);lua_pushstring(lua,name.c_str());lua_setglobal(lua,"__r15_name");
            if(luaL_loadstring(lua,"local idx=require('table_indexes'); local rows=idx.UnitStat[__r15_name]; if not rows then return false end; local r=rows[#rows]; return (r[UnitStatType.Exp] or 0)>0 and (r[UnitStatType.StatStrength] or 0)>0")||lua_pcall(lua,0,1,0))throw std::runtime_error(lua_tostring(lua,-1));
            bool use=lua_toboolean(lua,-1)!=0;lua_pop(lua,1);if(!use)continue;
            CUnitServer specific;EffectUnit actor;actor.Bind(&specific);specific.RegisterUnit(&actor);specific.SetManager(&specificManager);actor.SetSubtypeID(i);actor.SetID(9000+fullCases);
            struct DetachOwner {CUnitServer&s;~DetachOwner(){s.SetManager(nullptr);s.RegisterUnit(nullptr);}} detachOwner{specific};
            auto&live=*specific.GetRPGStats();live={};live.fpExperience=2000;live.fBaseHP=100;live.psStrength={25,25};live.psDexterity={25,25};live.psIntelligence={25,25};live.move={10,10};live.fpMaxEncumbrance=300;
            for(auto&h:live.hls){h.state=RPGALS_ACTIVE;h.hlt=RPGHLT_BODY;h.fpHPWeight=1;h.fpVitality=1.f/RPG_NUM_HIT_LOCATIONS;h.fpHP=100;h.fpMaxHP=100;}
            for(int j=0;j<RPG_NUM_SENSES;j++){live.senses[j]={10,10};live.percepts[j]={1,1};}
            SRPGCommonStats result={};result.ssHP={100,100,1};result.ssMP={50,50,1};
            actor.Power(SPELL_ID_STRENGTHEN,20);actor.Power(SPELL_ID_WEAKEN,10);
            live.RecalcStats(&result,&actor);
            require(fabs(result.ssHP.fpMax-105)<.0001 && std::isfinite(result.ssMP.fpMax) && live.move.fpEffectiveValue>0,"Full native NPC HP/MP/movement mismatch");
            auto once=live;auto commonOnce=result;live.RecalcStats(&result,&actor);
            require(memcmp(&live,&once,sizeof(live))==0 && memcmp(&result,&commonOnce,sizeof(result))==0,"Full NPC recalc accumulates modifiers");fullCases++;
        }
        run("__r15_name=nil");require(fullCases==8,"Not enough full NPC fixtures");
'''+a
edit('Game/ModRuntime.cpp',a,b)
edit('Game/ModRuntime.cpp','return {{"npcPreparedCases",npcCases}', 'return {{"fullNativeRecalcCases",fullCases},{"npcPreparedCases",npcCases}')
p=r/'check-full-stats.py';v=(r/'check-stat-effects.py').read_text().replace('r14','r15').replace('assert dispatch["npcPreparedCases"] == 8','assert dispatch["fullNativeRecalcCases"] == 8\nassert dispatch["npcPreparedCases"] == 8');p.write_text(v)
