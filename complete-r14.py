from pathlib import Path
r=Path('work/eirebuild-local')
def edit(f,a,b):
 p=r/f;v=p.read_bytes();nl=b'\r\n' if b'\r\n' in v else b'\n';x=a.encode().replace(b'\n',nl);y=b.encode().replace(b'\n',nl);assert v.count(x)==1,(f,a[:50],v.count(x));p.write_bytes(v.replace(x,y))
edit('Game/ModPackage.cpp','BeginCallback("RecalcInitialJmvStatsExt");ModLua::Push(state,&staged,frame.scope);ModLua::Push(state,&stagedCommon,frame.scope);ModLua::Push(state,&unit,frame.scope);lua_pushstring(state,prototype.c_str());Call(4,0);','''// Match the arguments used inside the current full Lua callback, including its nil armorRecalc/forceRecalc globals.
 BeginCallback("RecalcInitialJmvStats");ModLua::Push(state,&staged,frame.scope);ModLua::Push(state,&stagedCommon,frame.scope);lua_getglobal(state,"armorRecalc");ModLua::Push(state,&unit,frame.scope);lua_pushstring(state,prototype.c_str());ModLua::Push(state,&unit,frame.scope);lua_getglobal(state,"forceRecalc");Call(7,0);''')
edit('Game/ModRuntime.cpp','bool nonzeroJewelryFixture=false;', 'bool nonzeroJewelryFixture=false;int preparedCases=0;bool effectsRollback=false;')
a='''            require(fabs(changed.psStrength.fpEffectiveValue-(50+bonus))<.0001,"Equipment primary stat mutation was lost");'''
b=a+'''
            // Complete initial/spell/custom stage on real equipped native objects.
            equippedUnit.SetID(1000000001);
            equippedUnit.Power(SPELL_ID_ANTIMAGIC,20);equippedUnit.Power(SPELL_ID_PROT_FIRE,30);
            equippedUnit.Power(SPELL_ID_PROT_FIRE,10); // strongest instance wins, never sum duplicates
            equippedUnit.Power(SPELL_ID_INVISIBILITY,50);equippedUnit.Power(SPELL_ID_SILENCE,25);
            auto luaNumber=[&](const char*code){if(luaL_loadstring(lua,code)||lua_pcall(lua,0,1,0))throw std::runtime_error(lua_tostring(lua,-1));double n=lua_tonumber(lua,-1);lua_pop(lua,1);return n;};
            int flow=int(luaNumber("return PerkType.Flow")),armory=int(luaNumber("return PerkType.Armory"));
            for(int level=0;level<=3;level++){
                SRPGUnitStats prepared=stats;prepared.psIntelligence={25,999};prepared.cPerks[flow]=BYTE(level);prepared.cPerks[armory]=BYTE(level);
                for(int i=0;i<RPG_NUM_SENSES;i++){prepared.senses[i]={10,99};prepared.percepts[i]={1,99};}
                SRPGCommonStats result={};for(auto&a:result.psAbsorbtions)a={5,999};
                package.PrepareUnitStats(prepared,result,equippedUnit,"unused-hero-prototype");
                float flowMod=prepared.GetPerkModifier(flow)*.01f,armorMod=prepared.GetPerkModifier(armory)*.01f;
                require(fabs(result.psAbsorbtions[RPGAT_TERMICAL].fpEffectiveValue-35*(1+flowMod))<.0001,"Fire/antimagic/Flow stage mismatch");
                require(fabs(result.psAbsorbtions[RPGAT_ELECTRICAL].fpEffectiveValue-25*(1+flowMod))<.0001,"Electric protection stage mismatch");
                require(fabs(result.psAbsorbtions[RPGAT_PIERCING].fpEffectiveValue-5*(1+armorMod))<.0001,"Armory stage mismatch");
                require(fabs(prepared.percepts[RPGS_SIGHT].fpEffectiveValue-.5f)<.0001 && fabs(prepared.percepts[RPGS_HEARING].fpEffectiveValue-.75f)<.0001,"Stealth effect scaling mismatch");
                const float quickRates[]={.001f,.0012f,.0014f,.00175f};
                require(fabs(jewelry.GetManaRegenRate()-quickRates[level])<.000001,"Quick item regeneration did not commit");
                preparedCases++;
            }
            // Original callbacks first mutate stats and item regeneration, then fail: nothing publishes.
            lua_getglobal(lua,"ApplyCustomEffects");int original=luaL_ref(lua,LUA_REGISTRYINDEX);
            struct RestoreCustom {lua_State*s;int ref;~RestoreCustom(){lua_rawgeti(s,LUA_REGISTRYINDEX,ref);lua_setglobal(s,"ApplyCustomEffects");luaL_unref(s,LUA_REGISTRYINDEX,ref);lua_pushnil(s);lua_setglobal(s,"__r14_original");}} restoreCustom{lua,original};
            lua_rawgeti(lua,LUA_REGISTRYINDEX,original);lua_setglobal(lua,"__r14_original");
            run("ApplyCustomEffects=function(...) __r14_original(...); error('intentional after item mutations') end");
            SRPGUnitStats before=stats;SRPGCommonStats beforeCommon=common;float rate=jewelry.GetManaRegenRate();
            bool rejected=false;try{package.PrepareUnitStats(stats,common,equippedUnit,"unused");}catch(const std::runtime_error&){rejected=true;}
            require(rejected && memcmp(&stats,&before,sizeof(stats))==0 && memcmp(&common,&beforeCommon,sizeof(common))==0 && jewelry.GetManaRegenRate()==rate,"Stat/equipment stage partially committed");effectsRollback=true;
'''
edit('Game/ModRuntime.cpp',a,b)
edit('Game/ModRuntime.cpp','return {{"nativeHpMpCallsites",true}', 'return {{"preparedStatCases",preparedCases},{"effectsEquipmentRollback",effectsRollback},{"nativeHpMpCallsites",true}')
# Targeted extra bridge check proves exact effect-array semantics, lifetime and bounds.
a=''' scope->active=false;run("assert(not pcall(function() return __native_stats.experience end))",0);'''
b=''' float effects[53]={};effects[52]=7;PushEffects(s,effects,53,scope);lua_setglobal(s,"__native_effects");
 run("assert(Array(__native_effects,Memory.Type.Float,53,true):value(52)==7); assert(not pcall(function() Array(__native_effects,Memory.Type.Float,52,true) end)); assert(not pcall(function() Array(__native_effects,Memory.Type.UInt8,53,true) end)); assert(not pcall(function() __native_effects:set(0,9) end)); assert(not pcall(function() return __native_effects:value(53) end))",0);
 scope->active=false;run("assert(not pcall(function() return __native_stats.experience end)); assert(not pcall(function() return __native_effects:value(0) end)); __native_effects=nil",0);'''
edit('Game/ModLuaBridge.cpp',a,b)
p=r/'check-stat-effects.py';v=(r/'check-equipment-health.py').read_text().replace('r13','r14');v=v.replace('assert dispatch["modifiedHealthMutationSentinels"]','assert dispatch["modifiedHealthMutationSentinels"]\nassert dispatch["preparedStatCases"] == 4 and dispatch["effectsEquipmentRollback"]');p.write_text(v)
