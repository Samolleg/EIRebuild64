// Background checkpoints. Ordinary scripts, gates, save restrictions and
// encounter algorithms are unchanged. Entry requires a freshly entered arena.
json ArenaStages(const std::string& name) {
    if(name=="gz19g")return {"entry","dialogue","erfar","flash","escape","golem_entry","golem_rise","golem","aftermath","finale"};
    if(name=="gz19g_hard")return {"entry","collapse","golem_entry","golem_rise","golem","aftermath","exit"};
    if(name=="gz20g")return {"entry","battle","phase1","phase2","phase3","wave1","wave2","wave3","wave4","wave5"};
    throw std::runtime_error("arena name must be gz19g, gz19g_hard or gz20g");
}
void ArenaFragment(const std::string& source) {
    float result=0;CString error;
    if(!theWorldServer.script.RunFragment(source.c_str(),result,error))throw std::runtime_error(Text(error).c_str());
}
std::string ArenaActor(int id){return "GetObjectByID(\""+std::to_string(id)+"\")";}
void ArenaSet(const char* key,float value) {
    auto* server=static_cast<CPlayerServer*>(theWorldServer.GetPlayer(0)->GetPlayerSpecific());
    server->SetVar(key,value);
}
bool ArenaStage(Job& job,Clock::time_point deadline) {
    static std::uint64_t preparedEpoch=0;
    if(!AgentBackground::Enabled())throw std::runtime_error("arena_stage requires --background");
    const auto& params=job.request.at("params");
    const auto name=params.at("name").get<std::string>(),stage=params.at("stage").get<std::string>();
    const auto stages=ArenaStages(name);
    if(std::find(stages.begin(),stages.end(),stage)==stages.end()) {
        job.response=Error("unsupported arena stage");job.response["stages"]=stages;return true;
    }
    const bool hard=name=="gz19g_hard",curse=name=="gz20g";
    auto& world=theWorldServer;auto& script=world.script;
    if(!job.phase) {
        CommandClient();
        const bool named=world.GetCurrentZoneName().CompareNoCase(name.c_str())==0||
            (curse&&world.GetCurrentZoneName().CompareNoCase("zone20")==0);
        if(!named||
           (curse?!ModEncounter::Curse(world):!ModEncounter::Arena(world)))
            throw std::runtime_error("enter the requested arena first, then wait for a ready local mission");
        if(theInterface.GetScreensNum()!=1||!dynamic_cast<CI3DScreenMission*>(theInterface.GetCurrentScreen())||IWorldConnector.HasReq())
            throw std::runtime_error("arena checkpoint requires the mission screen without a pending transition");
        auto* player=world.GetPlayer(0);
        if(!player||player->party.people.GetSize()<1||player->party.people.GetSize()>16)
            throw std::runtime_error("arena checkpoint requires a party of 1..16");
        const auto read=[&](const char* key){return ModEncounter::Read(world,key);};
        if(curse?(read("ZC.Enabled")!=1||read("ZC.Active")!=0||read("ZC.Started")!=0||read("ZC.Failed")!=0||read("ZC.VictoryReady")!=0):
           (read("EFC.Enabled")!=1||read("EFC.Stage")!=0||read("FGL.Active")!=0||read("FGL.Won")!=0||read("A62.Active")!=0))
            throw std::runtime_error("checkpoint requires a fresh arena; re-enter it before choosing another stage");
        const auto actor=[&](int id){auto* unit=world.GetUnit(id);if(!unit||unit->IsDead()||!unit->GetUnitSpecific())throw std::runtime_error("required arena actor is absent or dead");};
        actor(curse?666666:990400);
        if(curse){actor(640706);actor(49435758);actor(49435759);for(int i=0;i<12;++i)actor(710001+i);}
        else if(!hard)actor(1000105);
        for(int i=0;i<player->party.people.GetSize();++i)actor(player->party.people[i].nID);
        if(stage=="entry") {
            job.response={{"ok",true},{"status","done"},{"name",name},{"stage",stage},{"stages",stages},{"truncated",false}};return true;
        }
        if(preparedEpoch==owned::bridge_current_world_epoch())
            throw std::runtime_error("this arena already has a checkpoint; re-enter after cancellation or before choosing another");
        if(!curse) {
            if(script.GetGlobalVars().gVars.GetVariableIndex("EFCParty")<0)
                throw std::runtime_error("loaded arena has no EFCParty group");
            if(hard)script.McpArenaThread("#HGLights",nullptr,0,false,true);
            const char* thread=nullptr;const char* marker=nullptr;float value=0;bool tail=false;
            if(stage=="dialogue"||stage=="erfar"){thread="#EFCDialogue";marker="EFC.Stage";value=3;tail=stage=="erfar";}
            if(stage=="flash"||stage=="escape"){thread="#EFCFlash";marker="EFC.Stage";value=5;tail=stage=="escape";}
            if(stage=="golem_rise"||stage=="golem"||stage=="aftermath"||stage=="exit") {
                thread=hard?"#HGRise":"#EFCRise";marker="EFC.Stage";value=7;tail=stage!="golem_rise";
            }
            if(stage=="finale"){thread="#EFCRise";marker="EFC.Stage";value=7;tail=true;}
            if(thread)script.McpArenaThread(thread,marker,value,tail,true);
            job.filterName=thread?thread:"";job.filterRadius=value;
            job.renderDetails=tail;job.captureName=marker?marker:"";
        }
        job.world=&world;job.worldId=world.GetRTID();job.orderEpoch=owned::bridge_current_world_epoch();
        preparedEpoch=job.orderEpoch;
        job.phase=1;job.item=0;
    }
    if(!SameWorld(job,world)||job.orderEpoch!=owned::bridge_current_world_epoch())
        throw std::runtime_error("arena world changed during checkpoint preparation");
    auto* player=world.GetPlayer(0);
    if(job.phase==3) {
        if(ModEncounter::Read(world,"FGL.Active")!=1) {
            if(Clock::now()>job.lodExpires)throw std::runtime_error("golem did not initialize; unpause and re-enter the arena");
            return false;
        }
        const auto boss=ArenaActor(990400);
        ArenaFragment("SetInvincible("+boss+",\"all\",0) ApplyDirectDamage("+boss+",1000000)");
        job.item=0;job.phase=4;
    }
    if(job.phase==4) {
        // Death is real; FGLPoll/EFCPoll/HGPoll now own victory and cleanup.
        // Final trigger positions are applied after FGLPoll's entry teleport.
        if(stage=="finale"||stage=="exit")while(job.item<size_t(player->party.people.GetSize())&&Clock::now()<deadline) {
            const auto actor=ArenaActor(player->party.people[int(job.item)].nID);
            const auto x=stage=="finale"?85.0+double(job.item)*1.5:62.0+double(job.item)*1.5;
            const auto y=stage=="finale"?80:12;
            ArenaFragment("UMClear("+actor+") Idle("+actor+") SetCP("+actor+","+std::to_string(x)+","+std::to_string(y)+",0)");
            ++job.item;
        }
        if((stage=="finale"||stage=="exit")&&job.item<size_t(player->party.people.GetSize()))return false;
        job.response={{"ok",true},{"status","accepted"},{"service",true},{"name",name},{"stage",stage},
            {"stages",stages},{"completion","real death; ordinary scenario owns aftermath/finale/exit"},{"truncated",false}};return true;
    }
    const bool erfar=stage=="dialogue"||stage=="erfar"||stage=="flash"||stage=="escape";
    if(job.phase==1) {
        // One party member at a time; no unbounded loop in the command frame.
        while(job.item<size_t(player->party.people.GetSize())&&Clock::now()<deadline) {
            const auto id=player->party.people[int(job.item)].nID;
            const auto actor=ArenaActor(id);const double x=(curse?80.0:62.0)+(double(job.item)-double(player->party.people.GetSize()-1)*.5)*2.5;
            const double y=curse?78.0:hard?31.5:erfar?54.0:33.0;
            const auto blocked=(!curse&&(stage=="dialogue"||stage=="flash"||stage=="golem_rise"||stage=="collapse"))?"1":"0";
            ArenaFragment("UMClear("+actor+") Idle("+actor+") UMPlayer("+actor+") SetCP("+actor+","+std::to_string(x)+","+std::to_string(y)+",0) BlockUnit("+actor+","+blocked+")");
            ++job.item;
        }
        if(job.item<size_t(player->party.people.GetSize()))return false;
        job.phase=2;return false;
    }
    if(job.phase==5) {
        // HGPoll performs the real collapse settlement and Nav publication.
        // That work stays in its ordinary server tick, outside the MCP budget.
        if(ModEncounter::Read(world,"HG.Stage")<4) {
            if(Clock::now()>job.lodExpires)throw std::runtime_error("hard collapse did not settle; unpause and re-enter");
            return false;
        }
    }
    if(!curse&&!hard&&stage!="dialogue"&&stage!="erfar") {
        // Exact authored tower-break placements (A62Poll), one object per slice.
        static const struct {int id;float x,y,z;} broken[]={
            {990200,9.4f,6,-32},{990202,9.64f,6,-32},{990201,49,56,0},
            {990300,7,6,-32},{990302,7.24f,6,-32},{990301,58.107695f,26.741670f,0},
            {990310,8.2f,6,-32},{990312,8.44f,6,-32},{990311,78.892305f,49.258330f,0},
            {990207,7,6,-40},{990307,7,6,-40},{990317,7,6,-40},{990203,7,6,-40},{990205,7,6,-40}};
        const size_t count=stage=="flash"?9:std::size(broken);
        while(job.bucket<count&&Clock::now()<deadline) {
            const auto& object=broken[job.bucket++];
            ArenaFragment("SetCP("+ArenaActor(object.id)+","+std::to_string(object.x)+","+std::to_string(object.y)+","+std::to_string(object.z)+")");
        }
        if(job.bucket<count)return false;
        ArenaSet("A67.Broken0",1);ArenaSet("A67.Broken1",1);ArenaSet("A67.Broken2",1);
        ArenaSet("A67.Destroyed",3);ArenaSet("A62.SealBroken",1);
    }
    if(curse) {
        // The existing CurseLoop captures the party and starts the encounter
        // through its real location predicate on the next server tick.
        if(stage.rfind("phase",0)==0) {
            const int phase=stage.back()-'0';auto* boss=world.GetUnit(666666);
            auto* server=static_cast<CUnitServer*>(boss->GetUnitSpecific());
            const float maximum=server->GetMapObjectServer()->GetMaxHP();
            const float target=maximum*(phase==1?1.f:phase==2?.7f:.35f);
            const float damage=server->GetMapObjectServer()->GetCurrentHP()-target;
            if(damage>0)server->ApplyDirectDamage(damage);
        }
        if(stage.rfind("wave",0)==0) {
            const int wave=stage.back()-'0';const int previous=wave==1?0:wave==2?1:3+(wave-3)*3;
            // Previous finite actors are consumed through native death, just as
            // after clearing the preceding waves. Current wave still spawns normally.
            for(int i=0;i<previous;++i) {
                auto* unit=world.GetUnit(710001+i);auto* server=static_cast<CUnitServer*>(unit->GetUnitSpecific());
                server->ApplyDirectDamage(server->GetMapObjectServer()->GetCurrentHP()+1);
                const auto key="ZC.Enemy"+std::to_string(i)+".Used";ArenaSet(key.c_str(),1);
            }
            ArenaSet("ZC.Waves",float(wave-1));
        }
    } else {
        CScriptObjectGroup group;
        for(int i=0;i<player->party.people.GetSize();++i)group.Add(world.GetUnit(player->party.people[i].nID));
        script.GetGlobalVars().gVars[script.GetGlobalVars().gVars.GetVariableIndex("EFCParty")]=group;
        const auto boss=ArenaActor(990400);
        if(hard&&job.phase!=5) {
            script.McpArenaThread("#HGLights",nullptr,0,false,false);
            if(stage!="collapse") {
                ArenaSet("HG.Stage",3);ArenaSet("HG.Timer",59);ArenaSet("EFC.Stage",1);
                job.phase=5;job.lodExpires=Clock::now()+std::chrono::seconds(3);return false;
            }
        }
        if(!erfar&&stage!="collapse") {
            ArenaFragment("UMClear("+boss+") Idle("+boss+") SetCP("+boss+","+(hard?"39":"37.25561142")+",55,0) SwitchLeverState(GetObjectByID(\"991022\"),0)");
            if(!hard)ArenaFragment("SetCP(GetObjectByID(\"1000105\"),85.42383575,85.17633972,0) SwitchLeverState(GetObjectByID(\"991054\"),1)");
            ArenaSet("A62.Test",0);ArenaSet("A62.Active",0);ArenaSet("EFC.Stage",6.5f);
            if(hard){ArenaSet("HG.Stage",5);ArenaSet("HG.EntryLeg",1);}
        }
        if(stage=="collapse") {ArenaSet("HG.Stage",3);ArenaSet("HG.Timer",0);ArenaSet("HG.ImpactBorn",0);ArenaSet("EFC.Stage",1);}
        else if(stage=="golem_entry") {
            ArenaFragment("UMClear("+boss+") Idle("+boss+") SetCP("+boss+",37.25561142,83.55864716,0)");
            ArenaSet("EFC.Stage",6);ArenaSet("EFC.EntryWait",0);
            if(hard){ArenaSet("HG.Stage",4);ArenaSet("HG.EntryLeg",0);ArenaFragment("Run("+boss+") UMSentry("+boss+",37.2556,64)");}
        } else if(erfar) {
            ArenaSet("EFC.Stage",stage=="dialogue"?2:stage=="erfar"?2:4);
        }
        if(!job.filterName.empty())script.McpArenaThread(job.filterName.c_str(),job.captureName.empty()?nullptr:job.captureName.c_str(),float(job.filterRadius),job.renderDetails,false);
        if(stage=="aftermath"||stage=="exit"||stage=="finale") {
            job.phase=3;job.lodExpires=Clock::now()+std::chrono::seconds(3);return false;
        }
    }
    job.response={{"ok",true},{"status","accepted"},{"service",true},{"name",name},{"stage",stage},
        {"stages",stages},{"completion","observe ordinary arena globals on subsequent ticks"},{"truncated",false}};return true;
}
