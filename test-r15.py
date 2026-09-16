from pathlib import Path
r=Path('work/eirebuild-local')
def edit(f,a,b):
 p=r/f;v=p.read_bytes();nl=b'\r\n' if b'\r\n' in v else b'\n';x=a.encode().replace(b'\n',nl);y=b.encode().replace(b'\n',nl);assert v.count(x)==1,(f,a[:45],v.count(x));p.write_bytes(v.replace(x,y))
edit('Game/ModRuntime.cpp','int preparedCases=0,npcCases=0,fullCases=0;','int preparedCases=0,npcCases=0,fullCases=0,fullHeroCases=0,leftHandCases=0;bool stunRollback=false;')
# add direct known-policy fixture before existing initial hero tests
a='''        // Actual native RecalcStats callsite, with native ownership and real UDB/race records.'''
b='''        // Left-hand semantics: current client StrongMan perk, shield priority, dual staves.
        {
            auto shape=[&](int type){for(int i=0;i<theItemDatabase.weapons.GetNumEntries();i++)if(theItemDatabase.weapons.GetEntry(i)->dwTypeID==type)return i;throw std::runtime_error("Missing weapon type fixture");};
            CItemWeapon weapons[3];int ids[]={shape(RPGWST_HANDS4),shape(RPGWST_HANDS1),shape(RPGWST_HANDS1)};
            CUnitServer specific;EffectUnit actor;actor.Bind(&specific);actor.StrongMan(0);actor.SelectWeapon(0);
            for(int i=0;i<2;i++){weapons[i].SetTypeID(ITID_ITEM_WEAPON);weapons[i].SetSubtypeID(DWORD(ids[i])<<16);weapons[i].AddRef();actor.AddWeapon(&weapons[i]);}
            struct DetachWeapons {EffectUnit&u;CItemWeapon*w;int count=2;~DetachWeapons(){for(int i=0;i<count;i++){u.RemoveWeapon(i);w[i].DecRefCount();}}} detachWeapons{actor,weapons};
            require(ModStats::LeftHand(actor)==-1,"Two-handed sword unexpectedly permits shield");leftHandCases++;
            actor.StrongMan(1);require(ModStats::LeftHand(actor)==1,"Client StrongMan does not enable shield");leftHandCases++;
            int staff=shape(RPGWST_HANDS3);weapons[0].SetSubtypeID(DWORD(staff)<<16);weapons[1].SetSubtypeID(DWORD(staff)<<16);actor.StrongMan(0);
            require(ModStats::LeftHand(actor)==1,"Dual staves first slot mismatch");leftHandCases++;
            actor.SelectWeapon(1);require(ModStats::LeftHand(actor)==0,"Dual staves second slot mismatch");leftHandCases++;
            weapons[2].SetTypeID(ITID_ITEM_WEAPON);weapons[2].SetSubtypeID(DWORD(ids[2])<<16);weapons[2].AddRef();actor.AddWeapon(&weapons[2]);detachWeapons.count=3;
            actor.SelectWeapon(0);require(ModStats::LeftHand(actor)==2,"Shield priority from primary slot mismatch");leftHandCases++;
            actor.SelectWeapon(1);require(ModStats::LeftHand(actor)==0,"Dual slot must retain first staff despite shield");leftHandCases++;
        }
'''+a
edit('Game/ModRuntime.cpp',a,b)
edit('Game/ModRuntime.cpp','''            auto exp=package.Lookup("UnitStat",name,2,0); // exact eligibility is checked via the live Lua table below
            lua_getglobal(lua,"GetUnitStats");
            // Use direct table lookup for positive XP and strength to select meaningful real NPC fixtures.
            lua_pop(lua,1);lua_pushstring''','''            // Use direct table lookup for positive XP and strength to select meaningful real NPC fixtures.
            lua_pushstring''')
a='''            require(memcmp(&live,&once,sizeof(live))==0 && memcmp(&result,&commonOnce,sizeof(result))==0,"Full NPC recalc accumulates modifiers");fullCases++;'''
b=a+'''
            // Failure occurs after the original callback has written its shared stunSpell upvalue.
            if(fullCases==1){
                lua_getglobal(lua,"GetLeftHand");int original=luaL_ref(lua,LUA_REGISTRYINDEX);
                struct RestoreLeft {lua_State*s;int ref;~RestoreLeft(){lua_rawgeti(s,LUA_REGISTRYINDEX,ref);lua_setglobal(s,"GetLeftHand");luaL_unref(s,LUA_REGISTRYINDEX,ref);}} restoreLeft{lua,original};
                lua_pushinteger(lua,actor.GetID());lua_setglobal(lua,"__r15_id");
                run("for i=1,20 do local name,t=debug.getupvalue(RecalcUnitStatsJmv,i); if name=='stunSpell' then __r15_stun=t; break end end; assert(__r15_stun); __r15_stun[__r15_id]=17; GetLeftHand=function() error('intentional late recalc failure') end");
                auto oldStats=live;auto oldCommon=result;bool rejected=false;try{live.RecalcStats(&result,&actor);}catch(const std::runtime_error&){rejected=true;}
                require(rejected && memcmp(&live,&oldStats,sizeof(live))==0 && memcmp(&result,&oldCommon,sizeof(result))==0,"Full stats failure partially commits");
                run("assert(__r15_stun[__r15_id]==17); __r15_stun[__r15_id]=nil; __r15_stun=nil; __r15_id=nil");stunRollback=true;
            }
            actor.SetID(1000000000+fullCases);live.RecalcStats(&result,&actor);
            float expectedHero=live.RecalcHP()*1.05f*(1+theSettings.nDifficultyLevel*.25f);
            require(fabs(result.ssHP.fpMax-expectedHero)<.001f && result.ssHP.fpCurrent>0 && live.fpVisionArc==-2 && live.fpSpeeds[0]>0,"Full hero HP/proportions/vision/speed mismatch");fullHeroCases++;
'''
edit('Game/ModRuntime.cpp',a,b)
edit('Game/ModRuntime.cpp','#include "UnitDatabase.h"','#include "UnitDatabase.h"\n#include "GeneralSettings.h"')
edit('Game/ModRuntime.cpp','return {{"fullNativeRecalcCases",fullCases}', 'return {{"fullHeroRecalcCases",fullHeroCases},{"leftHandCases",leftHandCases},{"stunUpvalueRollback",stunRollback},{"fullNativeRecalcCases",fullCases}')
p=r/'check-full-stats.py';v=p.read_text().replace('assert dispatch["fullNativeRecalcCases"] == 8','assert dispatch["fullNativeRecalcCases"] == 8\nassert dispatch["fullHeroRecalcCases"] == 8\nassert dispatch["leftHandCases"] == 6 and dispatch["stunUpvalueRollback"]');p.write_text(v)
