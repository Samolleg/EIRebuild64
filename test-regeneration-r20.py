exec(open(__file__.replace('test-regeneration-r20.py','implement-regeneration-r20.py'),encoding='utf-8').read().split("edit('Game/ModPackage.h'")[0])
edit('Game/ModLuaBridge.cpp',' if(stats.cManualSkills[0]!=7', ' if(item.GetMP()==12)throw std::runtime_error("Item MP leaked before commit");scope->CommitItems();\n if(stats.cManualSkills[0]!=7')
edit('Game/ModRuntime.cpp','            auto once=live;auto commonOnce=result;', '''            // R20: native callsite executes unchanged installed HP/MP Lua on real equipment.
            if(fullCases==0){
                auto savedStats=live;auto savedCommon=result;
                result.ssMP.fpCurrent=0;result.ssMP.fpRegenerate=.01f;result.nRegenerateCounter=0;
                for(int tick=1;tick<15;tick++)require(!result.Regenerate(live,&actor,false)&&result.nRegenerateCounter==tick&&result.ssMP.fpCurrent==0,"Regeneration waiting cadence mismatch");
                require(result.Regenerate(live,&actor,false)&&result.nRegenerateCounter==0&&result.ssMP.fpCurrent>0,"Due regeneration did not restore MP");
                result.ssHP.fpCurrent=0;result.nRegenerateCounter=14;
                require(!result.Regenerate(live,&actor,false)&&result.nRegenerateCounter==14,"Dead unit advanced regeneration");
                result=savedCommon;live=savedStats;result.ssHP.fpCurrent*=.5f;for(auto&h:live.hls)h.fpHP*=.5f;result.nRegenerateCounter=14;float beforeHp=result.ssHP.fpCurrent;
                require(result.Regenerate(live,&actor,false)&&result.ssHP.fpCurrent>beforeHp,"Installed HP regeneration failed");
                for(int counter: {-1,14,15,2147483647}){result=savedCommon;live=savedStats;result.ssMP.fpCurrent=0;result.nRegenerateCounter=counter;require(result.Regenerate(live,&actor,true)&&result.nRegenerateCounter==0,"Serialized counter boundary mismatch");}
                // Failure after both resource and equipment writes must publish nothing.
                lua_getglobal(lua,"RegenerateHpAndMpJmvDue");int original=luaL_ref(lua,LUA_REGISTRYINDEX);
                run("RegenerateHpAndMpJmvDue=function(c,s,u) c.hp.curr=1; u:armor(0).mp=19; error('intentional regen failure') end");
                result=savedCommon;live=savedStats;result.nRegenerateCounter=14;auto before=result;float itemMp=armorPieces[0].GetMP();bool rejected=false;
                try{result.Regenerate(live,&actor,false);}catch(const std::runtime_error&){rejected=true;}
                lua_rawgeti(lua,LUA_REGISTRYINDEX,original);lua_setglobal(lua,"RegenerateHpAndMpJmvDue");luaL_unref(lua,LUA_REGISTRYINDEX,original);
                require(rejected&&memcmp(&result,&before,sizeof(result))==0&&memcmp(&live,&savedStats,sizeof(live))==0&&armorPieces[0].GetMP()==itemMp,"Regeneration failure partially committed");
                result=savedCommon;live=savedStats;
            }
            auto once=live;auto commonOnce=result;''')
edit('Game/ModRuntime.cpp','return {{"armorLocationCases",armorCases}', 'return {{"regenerationCadenceHpMpRollback",true},{"armorLocationCases",armorCases}')
print('R20 native regeneration fixtures written')
