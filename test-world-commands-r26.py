from pathlib import Path
r=Path(__file__).resolve().parent
p=r/'Game/ModMobCommands.h';b=p.read_bytes().replace(b'theWorldClient.',b'theWorld.');p.write_bytes(b)
p=r/'Game/ModRuntime.cpp';b=p.read_bytes();dest=r/'evidence/addon-integration/pre-r26/Game/ModRuntime.cpp';assert not dest.exists();dest.write_bytes(b)
s=b.decode('cp1251').replace('pending.command=302','pending.command=386')
a='            SSCCmd pending;'
block='''            {
                CScript commands;CString code="GlobalVars(d:float,r:float,f:float,g:group,o:object) WorldScript(d=DistancePointPoint(0,0,3,4) r=Random(7,7) f=IsFileExists(\\"main.lua\\") o=RandObj(g))";
                require(commands.LoadText(code),"World command parser failed");CString exported;commands.StoreText(exported);CScript again;require(again.LoadText(exported),"World command export failed");commands.Segment();again.Segment();for(auto*p:{&commands,&again}){float v=0;require(p->GetGlobalVars().fVars.GetVar("d",v)&&v==5,"Distance contract mismatch");require(p->GetGlobalVars().fVars.GetVar("r",v)&&v==7,"Random interval mismatch");require(p->GetGlobalVars().fVars.GetVar("f",v)&&v==1,"Package file lookup mismatch");}
                SSCCmd escape;escape.command=356;escape.szParams.Add(CString("../main.lua"));float v=0;bool rejected=false;try{ModMob::Float(escape,v);}catch(const std::runtime_error&){rejected=true;}require(rejected,"File lookup traversal accepted");
            }
'''
assert s.count(a)==1;s=s.replace(a,block.replace('\n','\r\n')+a)
a='                require(string(377,-1).IsEmpty(),"Missing weapon query mismatch");'
block='''                require(string(303,0)==std::to_string(actor.GetID()).c_str(),"Object string ID mismatch");
                auto query=[&](int id){SSCCmd cmd;cmd.command=id;cmd.oParams.Add(&object);float v=-1;cmd.CalcFunction(&context,vars,v);return v;};
                auto pose=specific.GetScriptPose();actor.LowSetPoses(pose==UP_STAND?UP_CRAWL:UP_STAND,UP_STAND);require(actor.GetCurrentPose()!=pose&&query(361)==pose,"Server pose authority mismatch");actor.LowSetPoses(pose,pose);
                require(query(366)==float(specific.logic.GetScriptState()),"Logic state query mismatch");
                actor.SetFlag(UNIT_STUN);require(query(369)==1,"Stun flag query mismatch");actor.RemoveFlag(UNIT_STUN);require(query(369)==0,"Cleared stun flag mismatch");
                SSCCmd flag;flag.command=367;flag.oParams.Add(&object);flag.params.Add(31);actor.SetFlag(0x80000000u);float v=0;flag.CalcFunction(&context,vars,v);require(v==1&&query(358)==1,"Flag bit/high marker mismatch");actor.RemoveFlag(0x80000000u);
                auto aggression=actor.GetAggression();SSCCmd change;change.command=382;change.oParams.Add(&object);change.params.Add(1);require(ModMob::Void(change)&&int(actor.GetAggression())==1,"Aggression action failed");actor.SetAggression(aggression);
                SSCCmd armor;armor.command=302;armor.oParams.Add(&object);armor.params.Add(0);armor.params.Add(0);live.hls[0].layers[0].fpAbsorbtions[0]=2;live.hls[0].layers[1].fpAbsorbtions[0]=3;live.hls[0].layers[2].fpAbsorbtions[0]=4;armor.CalcFunction(&context,vars,v);require(v==9,"Armor layers query mismatch");
'''
assert s.count(a)==1;s=s.replace(a,a+'\r\n'+block.replace('\n','\r\n'))
s=s.replace('return {{"mobUnitAndStringQueries"','return {{"mobWorldCommands",true},{"mobUnitAndStringQueries"');p.write_bytes(s.encode('cp1251'))
(r/'check-world-commands-r26.py').write_text((r/'check-unit-commands-r25.py').read_text().replace('r25-runtime','r26-runtime'),encoding='utf-8')
