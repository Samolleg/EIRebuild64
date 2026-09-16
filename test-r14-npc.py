from pathlib import Path
r=Path('work/eirebuild-local')
def edit(f,a,b):
 p=r/f;v=p.read_bytes();nl=b'\r\n' if b'\r\n' in v else b'\n';x=a.encode().replace(b'\n',nl);y=b.encode().replace(b'\n',nl);assert v.count(x)==1,(a[:40],v.count(x));p.write_bytes(v.replace(x,y))
edit('Game/ModRuntime.cpp','RPGAT_PIERCING','RPGAT_IMPALLING')
edit('Game/ModRuntime.cpp','int preparedCases=0;bool effectsRollback=false;', 'int preparedCases=0,npcCases=0;bool effectsRollback=false;')
a='''        // Actual native hero recalculation, with live perk DB and injured limb states.'''
b='''        // Pick installed NPC rows, preserving the real row values and native perk acquisition.
        run("__r14_npcs={}; local idx=require('table_indexes'); for name,rows in pairs(idx.UnitPerk) do local r=rows[#rows]; if r and type(name)=='string' and idx.UnitStat[name] then for col=13,44 do if type(r[col])=='number' and r[col]>0 then __r14_npcs[#__r14_npcs+1]=name; break end end end end; table.sort(__r14_npcs); assert(#__r14_npcs>0)");
        lua_getglobal(lua,"__r14_npcs");int count=(std::min)(8,int(lua_objlen(lua,-1)));
        for(int n=1;n<=count;n++){
            lua_rawgeti(lua,-1,n);std::string name=lua_tostring(lua,-1);lua_pop(lua,1);
            CUnitServer specific;EffectUnit npc;npc.Bind(&specific);npc.SetID(100+n);
            SRPGUnitStats prepared={};prepared.fpExperience=2000;prepared.psStrength={25,25};prepared.psDexterity={25,25};prepared.psIntelligence={25,25};prepared.move={10,10};
            for(auto&h:prepared.hls){h.state=RPGALS_ACTIVE;h.hlt=RPGHLT_BODY;h.fpHPWeight=1;h.fpVitality=1.f/RPG_NUM_HIT_LOCATIONS;h.fpHP=100;h.fpMaxHP=100;}
            for(int i=0;i<RPG_NUM_SENSES;i++){prepared.senses[i]={10,10};prepared.percepts[i]={1,1};}
            SRPGCommonStats commonNpc={};
            package.PrepareUnitStats(prepared,commonNpc,npc,name);
            for(int i=0;i<32;i++){auto value=package.Lookup("UnitPerk",name,i+13,0);int level=value.is_number()?int(value.get<double>()):0;require(prepared.cPerks[i]==level,"NPC perk did not reach staged native stats");}
            SRPGUnitStats once=prepared;SRPGCommonStats onceCommon=commonNpc;
            package.PrepareUnitStats(prepared,commonNpc,npc,name);
            require(memcmp(&prepared,&once,sizeof(prepared))==0 && memcmp(&commonNpc,&onceCommon,sizeof(commonNpc))==0,"NPC repeated preparation accumulates modifiers");npcCases++;
        }
        lua_pop(lua,1);lua_pushnil(lua);lua_setglobal(lua,"__r14_npcs");
'''+a
edit('Game/ModRuntime.cpp',a,b)
edit('Game/ModRuntime.cpp','return {{"preparedStatCases",preparedCases}', 'return {{"npcPreparedCases",npcCases},{"preparedStatCases",preparedCases}')
p=r/'check-stat-effects.py';v=p.read_text().replace('assert dispatch["preparedStatCases"]','assert dispatch["npcPreparedCases"] == 8\nassert dispatch["preparedStatCases"]');p.write_text(v)
