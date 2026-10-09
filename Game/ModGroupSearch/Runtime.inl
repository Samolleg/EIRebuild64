// Native 39454a3 coordinator mapped to typed EIRebuild owners; no retail ABI.
namespace ModGroupSearch {
namespace {
bool haulsRehydrated=false,configReady=false;
float trackRadius=17.0f,pickupDistance=2.5f;
const char* CheckEligibility(CUnit* unit,CUnitServer*& server) {
    server=nullptr;
    if(!unit||unit->IsHero()||unit->GetOwner()||!unit->GetPrototype()||
       !AddonRaceCanInvestigate(unit->GetPrototype()->szRace.GetString()))return "unit-filter";
    auto* specific=unit->GetUnitSpecific();
    if(!specific||specific->WhoAmI()!=HPC_UNITSERVERSPECIFIC)return "not-server";
    server=static_cast<CUnitServer*>(specific);
    return server->IsTamed()?"tamed":nullptr;
}
bool CurrentLogic(CUnitServer* server,const SGeneralUnitLogic*& logic,int& alarm) {
    if(!server)return false;
    logic=server->logic.GetCurrentGeneralLogic();alarm=server->logic.GetGroupSearchAlarm();
    return logic!=nullptr;
}
bool DutyModel(int model){return model==UM_GUARD||model==UM_PATROL||model==UM_SENTRY;}
bool CanSuspect(int mode){return mode==UAM_AGGRESSIVE||mode==UAM_REVENGE;}
bool HasAnimation(CUnitServer* s){const auto& a=s->GetScriptCommand();const auto& b=s->GetNextScriptCommand();
    return a.cmd==UC_PLAYANIM||a.cmd==UC_PLAYANIMCYCLE||b.cmd==UC_PLAYANIM||b.cmd==UC_PLAYANIMCYCLE;}
CGSVariableSet* PartyVars(){auto* p=AddonCorpsePlayer();return p?&p->party.vars:nullptr;}
std::string CurrentZone(){auto* p=AddonCorpsePlayer();return p?p->party.szCurrentZone.GetString():"";}
bool HasPrefix(const char* name,const std::string& prefix){return name&&std::strncmp(name,prefix.c_str(),prefix.size())==0;}
float DistanceXY2(const CPlot3D& a,const CPlot3D& b){return (a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y);}
bool AlliedCorpse(CUnit* unit,CUnit* body){return unit&&body&&unit!=body&&body->GetID()>=0&&body->IsDead()&&
    theWorldServer.fof.IsPlayerAlly(unit->GetPlayerNum(),body->GetPlayerNum());}
std::string HaulKey(const std::string& zone,int id,DWORD subtype){return "jmv.murderawareness.haul."+zone+"."+std::to_string(id)+"."+std::to_string(subtype);}
int HaulMarker(int id,DWORD subtype){return AddonCorpseReadInt(AddonCorpsePlayer(),HaulKey(CurrentZone(),id,subtype).c_str());}
void LoadConfig(){
    if(configReady)return;configReady=true;trackRadius=17.0f;pickupDistance=2.5f;
    auto* mod=ModRuntime::Current();if(!mod)return;
    const float r=mod->MurderAwarenessValue(9,17.0f),p=mod->MurderAwarenessValue(14,2.5f);
    if(std::isfinite(r)&&r>=1.0f&&r<=100.0f)trackRadius=r;
    if(std::isfinite(p)&&p>=0.5f&&p<=5.0f)pickupDistance=p;
}
SAddonCorpseSearch& PickupState(CUnit* unit,CUnitServer* server){
    auto& state=g_addonCorpseSearch[unit->GetID()];const auto zone=CurrentZone();
    if(state.observer!=unit||state.server!=server||state.observerSubtype!=unit->GetSubtypeID()||state.zone!=zone){
        state=SAddonCorpseSearch();state.observer=unit;state.server=server;state.observerSubtype=unit->GetSubtypeID();state.zone=zone;
    }
    return state;
}
void ReleasePickup(int observer,SAddonCorpseSearch& state,int tick){
    if(state.corpseId>=0){
        const auto key=HaulKey(state.zone,state.corpseId,state.corpseSubtype);
        auto* p=AddonCorpsePlayer();
        if(AddonCorpseReadInt(p,key.c_str())==observer+1)p->party.vars.SetVar(key.c_str(),0.0f);
        state.retry[{state.corpseId,state.corpseSubtype}]=tick+909;
    }
    state.corpseId=-1;state.haulAnimationCount=state.haulAnimationPhase=0;
}
void RehydrateHauls(int tick){
    if(haulsRehydrated)return;haulsRehydrated=true;
    auto* player=AddonCorpsePlayer();const auto zone=CurrentZone();
    CMapObjPtrHashIterator iterator;
    for(auto* object=iterator.GetFirst(&theWorldServer.dynamicObjs);object;object=iterator.GetNext()){
        if(object->WhoAmI()!=HPC_UNIT)continue;auto* body=static_cast<CUnit*>(object);
        if(!body->IsDead()||body->GetID()<0)continue;
        const int marker=HaulMarker(body->GetID(),body->GetSubtypeID());
        if(marker<0){if(!AddonApplyHauledCorpseState(body))haulsRehydrated=false;continue;}
        if(!marker)continue;
        auto* owner=theWorldServer.GetUnit(marker-1);CUnitServer* server=nullptr;
        if(!owner||owner->IsDead()||CheckEligibility(owner,server)||
            AddonCorpseReadInt(player,AddonCorpseMarker(owner,body,zone.c_str()).c_str())!=2){
            player->party.vars.SetVar(HaulKey(zone,body->GetID(),body->GetSubtypeID()).c_str(),0.0f);continue;
        }
        auto& state=PickupState(owner,server);
        if(state.corpseId>=0&&state.corpseId!=body->GetID()){
            player->party.vars.SetVar(HaulKey(zone,body->GetID(),body->GetSubtypeID()).c_str(),0.0f);continue;
        }
        state.corpseId=body->GetID();state.corpseSubtype=body->GetSubtypeID();state.haulClaimTick=tick;
    }
}
bool TryClaim(CUnit* unit,CUnitServer* server,CUnit* body,int tick){
    auto& state=PickupState(unit,server);auto* p=AddonCorpsePlayer();const auto zone=CurrentZone();
    if(state.corpseId>=0)return false;
    if(AddonCorpseProtected(body)){state.retry[Id(body)]=tick+909;return false;}
    std::vector<SAddonCarryEntry> entries;int destination=0,own=0;
    if(!AddonCarryPlan(p,body,unit,false,entries,destination,own)){state.retry[Id(body)]=tick+909;return false;}
    const auto key=HaulKey(zone,body->GetID(),body->GetSubtypeID());int claim=HaulMarker(body->GetID(),body->GetSubtypeID());
    if(claim<0)return false;
    if(claim>0&&claim!=unit->GetID()+1){
        auto* other=theWorldServer.GetUnit(claim-1);CUnitServer* otherServer=nullptr;
        if(other&&!other->IsDead()&&!CheckEligibility(other,otherServer)){state.retry[Id(body)]=tick+91;return false;}
        p->party.vars.SetVar(key.c_str(),0.0f);claim=0;
    }
    if(!claim){p->party.vars.SetVar(key.c_str(),float(unit->GetID()+1));if(HaulMarker(body->GetID(),body->GetSubtypeID())!=unit->GetID()+1)return false;}
    state.corpseId=body->GetID();state.corpseSubtype=body->GetSubtypeID();state.haulClaimTick=tick;
    state.haulAnimationPhase=state.haulAnimationCount=0;
    p->party.vars.SetVar(AddonCorpseMarker(unit,body,zone.c_str()).c_str(),2.0f);
    return true;
}
} // helpers
static bool OwnsPickupAnimation(CUnit* unit,CUnitServer* server,int tick){
    const auto found=g_addonCorpseSearch.find(unit->GetID());if(found==g_addonCorpseSearch.end())return false;
    const auto& s=found->second;const int clip=s.haulAnimationClip;
    if(s.observer!=unit||s.server!=server||s.observerSubtype!=unit->GetSubtypeID()||s.zone!=zone||
        s.corpseId<0||(s.haulAnimationPhase!=1&&s.haulAnimationPhase!=2)||
        clip<0||clip>=s.haulAnimationCount||s.haulAnimationCount>2||
        tick<s.haulAnimationQueuedTick||tick>=s.haulAnimationDeadline||tick<s.haulClaimTick||tick-s.haulClaimTick>=1091)return false;
    auto* body=theWorldServer.GetUnit(s.corpseId);
    if(!body||body->GetSubtypeID()!=s.corpseSubtype||!AlliedCorpse(unit,body)||HaulMarker(s.corpseId,s.corpseSubtype)!=unit->GetID()+1)return false;
    const auto& a=server->GetScriptCommand();const auto& b=server->GetNextScriptCommand();
    const bool current=AddonPickupAnimationRunning(a,s.haulAnimationEntry[clip]),next=AddonPickupAnimationRunning(b,s.haulAnimationEntry[clip]);
    return (current||next)&&(current||a.cmd==UC_IDLE||a.cmd==UC_NONE)&&(next||b.cmd==UC_IDLE||b.cmd==UC_NONE);
}
struct GGSRouteProbe
{
    GroupSearch::Identity unit;
    GroupSearch::Point wanted, endpoint;
    bool accepted;
    int probes;
};
struct GGSRuntimeSide
{
    GroupSearch::Expedition expedition;
    std::vector<std::string> keys;
    std::vector<std::string> bodyKeys;
    std::vector<std::string> witnessKeys;
    GroupSearch::SaveFormat::Words savedWords, savedBodyWords, savedWitnessWords;
    size_t formattedKeys=0;
    std::map<GroupSearch::Identity,int> loggedBodyBlocks;
    bool valid = true;
    int lastTick = -1;
    int lastSummary = -1;
    int lastBlocked = -1;
    GroupSearch::Phase plannedPhase = GroupSearch::Phase::Duty;
    GroupSearch::Identity plannedLeader;
    std::vector<GroupSearch::Identity> plannedParty;
    int routeReplans = 0;
    bool routeJob=false;
    bool routeFailed=false;
    int routeStartedTick=0;
    uint32_t routeSerial=0;
    std::vector<GGSRouteProbe> routeMemo;
    std::map<GroupSearch::Identity,GroupSearch::Point> routeOrigins;
    GGSRuntimeSide(const std::string &zone,int side):expedition(zone,side){}
};
static std::map<int,GGSRuntimeSide> G_groupSearchSides;
static std::string G_groupSearchZone;
static int G_groupSearchPollTick = -1;
static int G_groupSearchPathBudget = 8; // native calls across ALL sides per batch
static int G_groupSearchLastRouteSide = -1;
static void ResetCoordinator()
{
    G_groupSearchSides.clear();G_groupSearchZone.clear();G_groupSearchPollTick=-1;
    G_groupSearchLastRouteSide=-1;G_groupSearchPathBudget=8;
    g_addonCorpseSearch.clear();g_addonCorpseSearchZone.clear();
    haulsRehydrated=false;configReady=false;
}

static void GGS_ImportR4(GGSRuntimeSide &runtime,CGSVariableSet *vars)
{
    auto snapshot=runtime.expedition.Save();auto &s=snapshot.awareness;
    const std::string prefix="jmv.murderawareness.ally.v2."+s.zone+".";
    size_t imported=0,unresolved=0;
    struct Legacy {unsigned checkpoint;int phase;GroupSearch::Memory memory;};
    std::map<GroupSearch::Identity,Legacy> latest;
    for(const auto &entry:*vars)
    {
        const char *name=entry.first.c_str();if(!HasPrefix(name,prefix))continue;
        int observer=-1,ally=-1,phase=0;unsigned os=0,as=0,x=0,y=0,z=0,t=0;char trailing=0;
        if(std::sscanf(name+prefix.size(),"%d.%u.%d.%u.%d.%8x.%8x.%8x.%8x%c",
            &observer,&os,&ally,&as,&phase,&x,&y,&z,&t,&trailing)!=9 ||
            observer<0||ally<0||phase<1||phase>4||!std::isfinite(entry.second)||entry.second<1)continue;
        // Old schema has no side field. Resolve ID/subtype/side only, never
        // inspect hidden health/position to decide whether someone is missing.
        CUnit *o=theWorldServer.GetUnit(observer),*a=theWorldServer.GetUnit(ally);
        if(!o||!a||o->GetSubtypeID()!=os||a->GetSubtypeID()!=as){++unresolved;continue;}
        if(o->GetPlayerNum()!=s.side||a->GetPlayerNum()!=s.side)continue;
        GroupSearch::Identity id{ally,as};
        GroupSearch::Memory m;m.lastSeen={GroupSearch::SaveFormat::BitsFloat(x),GroupSearch::SaveFormat::BitsFloat(y),GroupSearch::SaveFormat::BitsFloat(z)};
        if(!GroupSearch::Finite(m.lastSeen))continue;
        if(phase==2)
            m.unseenTicks=GroupSearch::MissingTicks-int((std::min)(float(GroupSearch::MissingTicks),entry.second-1));
        if(phase==3||phase==4)
        {
            m.absent=true;m.unseenTicks=GroupSearch::MissingTicks;m.episode=1;
        }
        auto old=latest.find(id);
        const bool preferred=old==latest.end()||t>old->second.checkpoint||
            (t==old->second.checkpoint && (m.unseenTicks<old->second.memory.unseenTicks||
                (m.unseenTicks==old->second.memory.unseenTicks&&phase==4)));
        if(preferred)latest[id]={t,phase,m};
    }
    for(const auto &entry:latest)
    {
        if(!s.memories.emplace(entry.first,entry.second.memory).second)continue;
        if(entry.second.phase==3)
            s.pending.push_back({entry.first,1,entry.second.memory.lastSeen});
        else if(entry.second.phase==4)s.floor=1;
        ++imported;
    }
    runtime.valid=runtime.expedition.Restore(snapshot);
}

static GGSRuntimeSide &GGS_Side(int side,CGSVariableSet *vars)
{
    auto found=G_groupSearchSides.find(side);
    if(found!=G_groupSearchSides.end())return found->second;
    found=G_groupSearchSides.emplace(std::piecewise_construct,std::forward_as_tuple(side),
        std::forward_as_tuple(G_groupSearchZone,side)).first;
    auto &runtime=found->second;
    const auto prefix=GroupSearch::SaveFormat::Prefix(G_groupSearchZone,side);
    const auto bodyPrefix=GroupSearch::BodySave::Prefix(G_groupSearchZone,side);
    const auto witnessPrefix=GroupSearch::WitnessSave::Prefix(G_groupSearchZone,side);
    for(const auto &entry:*vars)
    {
        const char *name=entry.first.c_str();
        if(HasPrefix(name,witnessPrefix))
        {runtime.witnessKeys.emplace_back(name);if(entry.second!=1.0f)runtime.valid=false;continue;}
        if(HasPrefix(name,bodyPrefix))
        {runtime.bodyKeys.emplace_back(name);if(entry.second!=1.0f)runtime.valid=false;continue;}
        if(!HasPrefix(name,prefix))continue;
        runtime.keys.emplace_back(name);
        if(entry.second!=1.0f)runtime.valid=false;
    }
    std::sort(runtime.keys.begin(),runtime.keys.end());
    std::sort(runtime.bodyKeys.begin(),runtime.bodyKeys.end());
    std::sort(runtime.witnessKeys.begin(),runtime.witnessKeys.end());
    if(!runtime.keys.empty())
    {
        GroupSearch::SaveFormat::Words words;GroupSearch::ExpeditionSnapshot decoded;
        runtime.valid=runtime.valid && GroupSearch::SaveFormat::ParseKeys(runtime.keys,prefix,words) &&
            GroupSearch::SaveFormat::Decode(words,G_groupSearchZone,side,decoded);
        if(runtime.valid && !runtime.bodyKeys.empty())
        {
            GroupSearch::SaveFormat::Words metadata;
            auto result=GroupSearch::BodySave::Result::Invalid;
            if(GroupSearch::SaveFormat::ParseKeys(runtime.bodyKeys,bodyPrefix,metadata))
                result=GroupSearch::BodySave::Decode(metadata,words,decoded);
            if(result==GroupSearch::BodySave::Result::Invalid)runtime.valid=false;
        }
        if(runtime.valid&&!runtime.witnessKeys.empty())
        {
            GroupSearch::SaveFormat::Words metadata;auto result=GroupSearch::BodySave::Result::Invalid;
            if(GroupSearch::SaveFormat::ParseKeys(runtime.witnessKeys,witnessPrefix,metadata))
                result=GroupSearch::WitnessSave::Decode(metadata,words,decoded);
            if(result==GroupSearch::BodySave::Result::Invalid)runtime.valid=false;
        }
        runtime.valid=runtime.valid && runtime.expedition.Restore(decoded);
    }
    else if(runtime.bodyKeys.empty()&&runtime.witnessKeys.empty())GGS_ImportR4(runtime,vars);
    else runtime.valid=false; // orphan metadata is not an empty world
    EngineDiagnostics::Event("GroupSearch event=RESTORE zone=%s side=%d valid=%d base_keys=%u body_keys=%u witness_keys=%u",
        G_groupSearchZone.c_str(),side,runtime.valid?1:0,unsigned(runtime.keys.size()),unsigned(runtime.bodyKeys.size()),unsigned(runtime.witnessKeys.size()));
    return runtime;
}
static bool GGS_Save(GGSRuntimeSide &runtime,CGSVariableSet *vars)
{
    if(!runtime.valid)return false;
    const auto snapshot=runtime.expedition.View();
    const auto words=GroupSearch::SaveFormat::Encode(snapshot);
    if(words.empty())
    {
        runtime.valid=false;
        EngineDiagnostics::Event("GroupSearch event=SAVE_REJECTED zone=%s side=%d reason=invalid-state action=preserve-previous-gs",
            G_groupSearchZone.c_str(),snapshot.awareness.side);
        return false;
    }
    const auto metadata=GroupSearch::BodySave::Encode(snapshot,words);
    if(metadata.size()>GroupSearch::SaveFormat::MaxWords){runtime.valid=false;return false;}
    const auto witnessMetadata=GroupSearch::WitnessSave::Encode(snapshot,words);
    if(witnessMetadata.size()>GroupSearch::SaveFormat::MaxWords){runtime.valid=false;return false;}
    // World update/save is single-threaded. Write only changed chunks; no file
    // cache can leak future state into an older save or another map.
    auto write=[&](const std::string &key,const std::string &old) {
        if(!key.empty())vars->SetVar(key.c_str(),1.0f);
        if(!old.empty())vars->DelVar(old.c_str());
    };
    const auto side=snapshot.awareness.side;
    runtime.formattedKeys=GroupSearch::SaveFormat::SyncKeys(words,GroupSearch::SaveFormat::Prefix(G_groupSearchZone,side),runtime.savedWords,runtime.keys,write);
    runtime.formattedKeys+=GroupSearch::SaveFormat::SyncKeys(metadata,GroupSearch::BodySave::Prefix(G_groupSearchZone,side),runtime.savedBodyWords,runtime.bodyKeys,write);
    runtime.formattedKeys+=GroupSearch::SaveFormat::SyncKeys(witnessMetadata,GroupSearch::WitnessSave::Prefix(G_groupSearchZone,side),runtime.savedWitnessWords,runtime.witnessKeys,write);
    return true;
}

static bool GGS_IsAvailable(CUnit *unit,CUnitServer *server)
{
    return !PriorityBlocked(server->logic) &&
        !HasAnimation(server);
}
static bool GGS_InspectPhase(GroupSearch::Phase p)
{return p==GroupSearch::Phase::Inspect||p==GroupSearch::Phase::Inspect5||p==GroupSearch::Phase::Inspect10;}

static bool GGS_NativeSearchPoint(CUnitServer *server,GroupSearch::Point candidate,GroupSearch::Point &point,int &probes)
{
    // Query actual path output, never the guard generator's unchanged wish point.
    candidate.z=0;
    bool partial=false;int cost=0;
    if(probes>=64)return false;
    ++probes;
    const bool valid=QueryPath(server,candidate,point,partial,cost);
    const bool finite=GroupSearch::Finite(point);
    const float gap2=finite?GroupSearch::Distance2(point,candidate):-1;
    bool accepted=valid && !partial && finite && point.z==0 && gap2<=2.25f;
    bool verified=false;
    // A random point need not land exactly on the navigation geometry. Use
    // the actual full-path endpoint, but only nearby and stable on re-query.
    // This is target projection, NOT a larger arrival/partial-path tolerance.
    if(accepted && gap2>0.000001f)
    {
        accepted=false;
        if(probes<64)
        {
            GroupSearch::Point confirmed;bool secondPartial=false;int secondCost=0;
            ++probes;
            verified=QueryPath(server,point,confirmed,secondPartial,secondCost) &&
                !secondPartial && GroupSearch::Finite(confirmed) && confirmed.z==0 &&
                GroupSearch::Distance2(point,confirmed)<=0.0625f;
            accepted=verified;
        }
    }
    return accepted;
}

// Preflight Point5/Point10 before exposing a move. Restored targets are checked
// again, so legacy terrain-height targets cannot bypass the coordinate fix.
static bool GGS_PrepareSearchRoute(GGSRuntimeSide &runtime,
    const std::vector<GroupSearch::Candidate> &candidates,
    const std::map<GroupSearch::Identity,CUnit *> &active,
    const std::vector<GroupSearch::Feedback> &feedback)
{
    const auto s=runtime.expedition.View();const auto phase=s.awareness.phase;
    if(phase!=GroupSearch::Phase::Point5 && phase!=GroupSearch::Phase::Point10)
    {runtime.plannedPhase=GroupSearch::Phase::Duty;runtime.routeReplans=0;runtime.routeJob=false;runtime.routeMemo.clear();return false;}
    auto leader=active.find(s.awareness.leader);
    if(leader==active.end()){if(runtime.routeJob)runtime.plannedPhase=GroupSearch::Phase::Duty;runtime.routeJob=false;return true;} // roster reconciliation owns this
    for(auto id:s.awareness.party)
    {
        const auto *c=GroupSearch::FindActive(candidates,id);
        auto home=s.roster.homes.find(id);
        if(!c||home==s.roster.homes.end()||!GroupSearch::SameDuty(*c,home->second,s.awareness.side)||!c->available)
        {if(runtime.routeJob)runtime.plannedPhase=GroupSearch::Phase::Duty;runtime.routeJob=false;return true;}
    }
    bool failed=false;int failedMember=-1;
    for(const auto &f:feedback)if(f.fresh && f.serial==s.serial && f.action==GroupSearch::Action::Failed &&
        std::find(s.awareness.party.begin(),s.awareness.party.end(),f.unit)!=s.awareness.party.end())
    {failed=true;failedMember=f.unit.id;}
    const bool entering=runtime.plannedPhase!=phase;
    if(!runtime.routeJob && !entering && runtime.plannedLeader==s.awareness.leader && runtime.plannedParty==s.awareness.party && !failed)return false;
    bool moved=runtime.routeOrigins.size()!=s.awareness.party.size();
    for(auto id:s.awareness.party)
    {
        auto origin=runtime.routeOrigins.find(id);const auto now=GroupSearch::FindActive(candidates,id)->position;
        moved|=origin==runtime.routeOrigins.end() || GroupSearch::Distance2(origin->second,now)>0.0625f ||
            (origin!=runtime.routeOrigins.end() && std::fabs(origin->second.z-now.z)>0.25f);
    }
    if(!runtime.routeJob || runtime.routeSerial!=s.serial || moved)
    {
        if(!runtime.routeJob){if(entering)runtime.routeReplans=0;if(failed)++runtime.routeReplans;
            runtime.routeFailed=failed;runtime.routeStartedTick=theWorldServer.GetSegmentTimeCounter();}
        runtime.routeJob=true;runtime.routeSerial=s.serial;runtime.routeMemo.clear();runtime.routeOrigins.clear();
        for(auto id:s.awareness.party)runtime.routeOrigins[id]=GroupSearch::FindActive(candidates,id)->position;
    }
    failed=runtime.routeFailed;
    if(theWorldServer.GetSegmentTimeCounter()-runtime.routeStartedTick>=2182)
    {runtime.routeJob=false;runtime.routeMemo.clear();runtime.expedition.AbortSearchRoute();return false;}
    const auto target=phase==GroupSearch::Phase::Point5?s.point5:s.point10;
    const auto center=s.awareness.destination;const float radius=phase==GroupSearch::Phase::Point5?
        GroupSearch::Expedition::FirstSearchRadius:GroupSearch::Expedition::SecondSearchRadius;
    const float start=std::atan2(target.y-center.y,target.x-center.x);
    GroupSearch::Point selected;bool found=false;int tested=0,probes=0;
    std::map<GroupSearch::Identity,GroupSearch::Point> places;
    size_t cursor=0;bool pending=false;
    auto probe=[&](GroupSearch::Identity id,GroupSearch::Point wanted,GroupSearch::Point &end) {
        if(probes>=64)return false;
        if(cursor<runtime.routeMemo.size())
        {
            const auto &m=runtime.routeMemo[cursor];
            if(m.unit==id && GroupSearch::Distance2(m.wanted,wanted)<0.000001f && m.wanted.z==wanted.z)
            {++cursor;probes+=m.probes;end=m.endpoint;return m.accepted;}
            runtime.routeMemo.resize(cursor);
        }
        // A projected endpoint can require a second native query. Reserve
        // both calls before starting; a single engine query is indivisible.
        if(G_groupSearchPathBudget<2){pending=true;return false;}
        auto member=active.find(id);CUnitServer *server=nullptr;
        if(member==active.end()||CheckEligibility(member->second,server)||!server)return false;
        const int before=probes;const bool accepted=GGS_NativeSearchPoint(server,wanted,end,probes);
        G_groupSearchPathBudget-=probes-before;G_groupSearchLastRouteSide=s.awareness.side;
        runtime.routeMemo.push_back({id,wanted,end,accepted,probes-before});++cursor;return accepted;
    };
    if(runtime.routeReplans<=3)
        for(int n=0;n<12 && probes<64 && !pending;++n)
        {
            const float angle=start+float(n)*6.2831853f/12;
            GroupSearch::Point candidate{center.x+radius*std::cos(angle),center.y+radius*std::sin(angle),0};
            if(failed && GroupSearch::Distance2(candidate,target)<6.25f)continue;
            ++tested;
            places.clear();GroupSearch::Point end;
            if(!probe(s.awareness.leader,candidate,end))continue;
            candidate=end;
            if(failed && GroupSearch::Distance2(candidate,target)<6.25f)continue;
            places[s.awareness.leader]=candidate;
            bool reachable=true;size_t slot=1;
            for(auto id:s.awareness.party)
            {
                if(id==s.awareness.leader)continue;
                bool placed=false;
                for(int k=0;k<25 && probes<64 && !pending;++k)
                {
                    auto wanted=GroupSearch::Expedition::SearchSlot(candidate,slot);
                    if(k) {
                        const float a=float((k-1)%8)*6.2831853f/8;
                        const float r=1.5f*float(1+(k-1)/8);
                        wanted={candidate.x+r*std::cos(a),candidate.y+r*std::sin(a),0};
                    }
                    bool occupied=false;
                    for(const auto &p:places)if(GroupSearch::Distance2(p.second,wanted)<2.24f)occupied=true;
                    if(occupied)continue;
                    if(!probe(id,wanted,end))continue;
                    // Projection can move a slot onto a previously reserved
                    // endpoint. Check the actual place, not just the wish.
                    occupied=GroupSearch::Distance2(end,candidate)>36.0f;
                    for(const auto &p:places)if(GroupSearch::Distance2(p.second,end)<2.24f)occupied=true;
                    if(!occupied){places[id]=end;placed=true;break;}
                }
                ++slot;
                if(!placed){reachable=false;break;}
            }
            if(reachable){selected=candidate;found=true;break;}
        }
    if(pending)return true; // no unvalidated movement or investigation clock
    runtime.routeJob=false;runtime.routeMemo.clear();
    if(found)runtime.expedition.SetSearchPlaces(selected,places);
    else runtime.expedition.AbortSearchRoute(); // no false inspection; cases retained
    runtime.plannedPhase=phase;runtime.plannedLeader=s.awareness.leader;
    runtime.plannedParty=s.awareness.party;
    EngineDiagnostics::Event("GroupSearch event=ROUTE zone=%s side=%d leader=%d phase=%d accepted=%d probes=%d replans=%d batch_budget=%d",
        s.awareness.zone.c_str(),s.awareness.side,s.awareness.leader.id,int(phase),found?1:0,probes,runtime.routeReplans,G_groupSearchPathBudget);
    return false;
}

// Called only after a successful GS commit. Compare the actual transition,
// not the current phase: restore, blocked ticks and leader changes stay silent.
template<class Before,class After> static void GGS_PlaySignal(const Before &before,
    const After &after,bool started,
    const std::map<GroupSearch::Identity,CUnit *> &active)
{
    const auto phase=after.awareness.phase;
    const char *event=started && phase==GroupSearch::Phase::Gather ? "gather" :
        before.awareness.phase==GroupSearch::Phase::Gather && phase==GroupSearch::Phase::LastSeen ? "depart" : nullptr;
    if(!event)return;
    const char *status="no-active-leader";
    const auto leader=active.find(after.awareness.leader);
    if(leader!=active.end() && leader->second && !leader->second->IsDead())
    {
        auto *manager=theWorld.GetSoundManager();
        status="no-client-sound-manager";
        if(manager)
        {
            // Bind source by ID; the existing party audio owner updates gain.
            const auto sound=manager->BeginPartySignalFX("Tools\\sbor.wav",8000,
                leader->second->GetID(),20.0f,100.0f);
            status=sound==SND_EMPTY ? "start-failed" : "started";
        }
    }
    GameJournal::Event("AUDIO","group_signal","event=%s zone=%s side=%d leader=%d status=%s near=20 far=100",
        event,after.awareness.zone.c_str(),after.awareness.side,after.awareness.leader.id,status);
}

// Returns true while the leader is approaching/picking up a KNOWN body. The
// original virtual-loot transaction and animated pickup remain the sole owner.
static bool GGS_Pickup(GGSRuntimeSide& runtime,const std::map<GroupSearch::Identity,CUnit*>& activeUnits,
    const std::vector<CUnit*>& seenBodies,int tick,std::vector<GroupSearch::Assignment>& orders)
{
    const auto& s=runtime.expedition.Awareness().Save();auto leader=activeUnits.find(s.leader);
    if(!GGS_InspectPhase(s.phase)||leader==activeUnits.end())return false;
    for(auto id:s.party){
        auto member=activeUnits.find(id);CUnitServer* server=nullptr;
        if(member==activeUnits.end()||member->second->IsDead()||CheckEligibility(member->second,server)||!server)return false;
        if(!GGS_IsAvailable(member->second,server)){
            // Abort only transaction metadata on priority interruption; never
            // stop/replace a foreign animation, attack or script command.
            if(PriorityBlocked(server->logic)){
                auto st=g_addonCorpseSearch.find(s.leader.id);
                if(st!=g_addonCorpseSearch.end()){st->second.haulAnimationPhase=st->second.haulAnimationCount=0;st->second.nextHaulTick=tick+18;}
                return false;
            }
            const auto st=g_addonCorpseSearch.find(id.id);
            if(!(id==s.leader)||st==g_addonCorpseSearch.end()||st->second.observer!=member->second||
                st->second.server!=server||st->second.observerSubtype!=id.subtype||st->second.haulAnimationPhase==0)return false;
        }
    }
    auto* unit=leader->second;CUnitServer* server=nullptr;
    if(unit->IsDead()||CheckEligibility(unit,server)||!server)return false;
    const auto saved=runtime.expedition.View();auto home=saved.roster.homes.find(s.leader);
    const SGeneralUnitLogic* logic=nullptr;int alarm=-1;
    if(home==saved.roster.homes.end()||!CurrentLogic(server,logic,alarm)||
        alarm!=home->second.alarm||int(logic->eLogicModel)!=home->second.model||unit->GetPlayerNum()!=s.side||revoked.count(s.leader))return false;
    auto& state=PickupState(unit,server);
    if(state.corpseId<0&&GGS_IsAvailable(unit,server)){
        CUnit* chosen=nullptr;float best=0;
        for(auto* body:seenBodies){
            if(!body||body->GetPlayerNum()!=s.side||!AlliedCorpse(unit,body)||HaulMarker(body->GetID(),body->GetSubtypeID())<0||
                GroupSearch::Distance2(Point(body->GetCP()),s.destination)>225)continue;
            auto retry=state.retry.find(Id(body));if(retry!=state.retry.end()&&tick<retry->second)continue;
            const float distance=DistanceXY2(unit->GetCP(),body->GetCP());if(!chosen||distance<best){chosen=body;best=distance;}
        }
        if(chosen)TryClaim(unit,server,chosen,tick);
    }
    if(state.corpseId<0)return false;
    auto* body=theWorldServer.GetUnit(state.corpseId);
    if(!body||body->GetSubtypeID()!=state.corpseSubtype||!AlliedCorpse(unit,body)||
        HaulMarker(state.corpseId,state.corpseSubtype)!=unit->GetID()+1||tick-state.haulClaimTick>=1091){
        ReleasePickup(unit->GetID(),state,tick);return false;
    }
    if(!GGS_IsAvailable(unit,server)&&state.haulAnimationPhase==0)return true;
    GroupSearch::Intent intent;intent.serial=runtime.expedition.View().serial;
    if(DistanceXY2(unit->GetCP(),body->GetCP())>pickupDistance*pickupDistance&&state.haulAnimationPhase==0){
        auto lease=leases.find(s.leader);
        if(lease!=leases.end()&&lease->second.action==GroupSearch::Action::Failed){ReleasePickup(unit->GetID(),state,tick);return false;}
        intent.order=GroupSearch::Order::Move;intent.target=Point(body->GetCP());intent.tolerance=pickupDistance*0.8f;
        orders.push_back({s.leader,intent});return true;
    }
    intent.order=GroupSearch::Order::Hold;intent.target=Point(unit->GetCP());orders.push_back({s.leader,intent});
    auto lease=leases.find(s.leader);
    if(state.haulAnimationPhase==0&&(lease==leases.end()||!lease->second.duty||!lease->second.movement.OwnsCommand()))
        return true; // acquire selected-duty hold after restore before queuing a clip
    // The sole haul writer runs only in range, never its old PoI approach.
    AddonUpdateCorpseHaul(server->logic,state,unit,server,body,AddonCorpsePlayer(),CurrentZone(),tick,pickupDistance);
    if(HaulMarker(body->GetID(),body->GetSubtypeID())<0&&!body->CheckFlag(OBJ_HIDDEN))haulsRehydrated=false;
    return state.corpseId>=0;
}

void Update()
{
    if(!theWorldServer.IsMapLoaded())return;
    CGSVariableSet *vars=PartyVars();if(!vars)return;
    const std::string zone=CurrentZone();
    static bool waiting=false;
    if(zone.empty() || zone=="<unknown-zone>")
    {
        // bMapLoaded can precede assignment of the stable zone name. Do not
        // learn, restore GS or expose old-world commands during that interval.
        // This reset never dereferences cached objects or writes GS keys.
        waiting=true;Reset();return;
    }
    waiting=false;
    const int tick=theWorldServer.GetSegmentTimeCounter();
    if(zone!=G_groupSearchZone || (G_groupSearchPollTick>=0&&tick<G_groupSearchPollTick))
    {Reset();Context();G_groupSearchZone=zone;}
    if(G_groupSearchPollTick>=0 && tick-G_groupSearchPollTick<18)return;
    G_groupSearchPollTick=tick;Context();
    G_groupSearchPathBudget=8;
    LoadConfig();RehydrateHauls(tick);
    std::vector<GroupSearch::Candidate> candidates;
    std::map<GroupSearch::Identity,CUnit *> activeUnits;
    std::map<int,std::vector<GroupSearch::Observation>> visible;
    std::map<int,std::set<GroupSearch::Identity>> visibleIds,bodyIds;
    std::map<int,std::vector<CUnit *>> bodies;
    std::map<int,std::vector<GroupSearch::BodySighting>> sightings;
    std::set<GroupSearch::Identity> activeIds;
    {
    CMapObjPtrHashIterator iterator;
    for(CMapObject *object=iterator.GetFirst(theWorldServer.GetActiveSet());object;object=iterator.GetNext())
    {
        if(!object||object->WhoAmI()!=HPC_UNIT)continue;
        CUnit *unit=static_cast<CUnit *>(object);if(unit->GetID()<0)continue;
        const auto id=Id(unit);activeIds.insert(id);
        GroupSearch::Candidate c;c.unit=id;c.side=unit->GetPlayerNum();c.active=true;c.position=Point(unit->GetCP());
        CUnitServer *server=nullptr;const SGeneralUnitLogic *logic=nullptr;
        c.eligible=!unit->IsDead()&&!CheckEligibility(unit,server)&&server&&
            CurrentLogic(server,logic,c.alarm)&&logic&&DutyModel(c.model=int(logic->eLogicModel))&&
            CanSuspect(int(logic->aggressionMode))&&!revoked.count(id)&&c.side>=0;
        c.available=c.eligible&&GGS_IsAvailable(unit,server);
        candidates.push_back(c);activeUnits.emplace(id,unit);
        if(!c.eligible)continue;
        auto &side=GGS_Side(c.side,vars);(void)side;
        auto &observations=visible[c.side];auto &seen=bodies[c.side];
        CAIUnitEnvironment &environment=server->logic.environment;
        for(POSITION pos=environment.units.GetHeadPosition();pos;)
        {
            CUnit *other=static_cast<CUnit*>(environment.units.GetNext(pos));CUnitServer *otherServer=nullptr;
            if(!other||other==unit||other->IsDead()||other->GetPlayerNum()!=c.side||
                visibleIds[c.side].count(Id(other))||
                CheckEligibility(other,otherServer)||DistanceXY2(unit->GetCP(),other->GetCP())>trackRadius*trackRadius||
                !environment.IsUnitVisible(unit,other))continue;
            const auto otherId=Id(other);
            if(visibleIds[c.side].insert(otherId).second)
                observations.push_back({otherId,Point(other->GetCP())});
        }
        for(POSITION pos=environment.seenCorpses.GetHeadPosition();pos;)
        {
            CUnit *corpse=static_cast<CUnit*>(environment.seenCorpses.GetNext(pos));
            if(corpse&&corpse->GetPlayerNum()==c.side&&AlliedCorpse(unit,corpse)&&
                DistanceXY2(unit->GetCP(),corpse->GetCP())<=trackRadius*trackRadius)
            {
                if(bodyIds[c.side].insert(Id(corpse)).second)seen.push_back(corpse);
                if(HaulMarker(corpse->GetID(),corpse->GetSubtypeID())>=0)
                    sightings[c.side].push_back({id,Id(corpse),Point(corpse->GetCP())});
            }
        }
    }
    }
    SetActive(activeIds); // drop sleeping leases before consuming feedback
    std::vector<GroupSearch::Feedback> feedback;
    for(const auto &entry:leases)
    {
        const auto &lease=entry.second;
        feedback.push_back({entry.first,lease.serial,lease.action,
            lease.tick>=0&&tick>=lease.tick&&tick-lease.tick<=18});
    }
    // Rotate the first side so a difficult route cannot monopolize the budget.
    std::vector<int> sideOrder;for(const auto &entry:G_groupSearchSides)sideOrder.push_back(entry.first);
    auto nextSide=std::upper_bound(sideOrder.begin(),sideOrder.end(),G_groupSearchLastRouteSide);
    std::rotate(sideOrder.begin(),nextSide,sideOrder.end());
    for(auto side:sideOrder)
    {
        auto &runtime=G_groupSearchSides.at(side);
        if(!runtime.valid)continue;
        const auto before=runtime.expedition.Save();
        const int elapsed=runtime.lastTick>=0&&tick>=runtime.lastTick&&tick-runtime.lastTick<=36?tick-runtime.lastTick:0;
        runtime.lastTick=tick;
        // Active, already-known observers know their own presence. This does
        // not acquaint the side with unseen strangers or query sleeping units.
        for(const auto &c:candidates)if(c.eligible&&c.side==side&&before.awareness.memories.count(c.unit)&&visibleIds[side].insert(c.unit).second)
            visible[side].push_back({c.unit,c.position});
        const int observers=int(std::count_if(candidates.begin(),candidates.end(),[&](const GroupSearch::Candidate &c){return c.eligible&&c.side==side;}));
        const bool observing=observers>0;
        runtime.expedition.Awareness().Tick(elapsed,true,observing,visible[side]);
        std::vector<GroupSearch::Assignment> pickupOrders;
        const bool pickupBusy=GGS_Pickup(runtime,activeUnits,bodies[side],tick,pickupOrders);
        for(const auto &m:runtime.expedition.Awareness().Save().memories)
            if(m.second.bodyReported && HaulMarker(m.first.id,m.first.subtype)<0)
                runtime.expedition.Awareness().BodyHandled(m.first);
        const bool routeBusy=GGS_PrepareSearchRoute(runtime,candidates,activeUnits,feedback);
        GroupSearch::ExpeditionResult result;
        {
            result=runtime.expedition.Step(observing?elapsed:0,candidates,feedback,pickupBusy,&sightings[side],routeBusy);
        }
        for(auto id:result.forget){leases.erase(id);revoked.erase(id);}
        for(auto id:result.capped){leases.erase(id);revoked.erase(id);}
        // Save the decision and home/phase BEFORE exposing movement intents.
        if(!GGS_Save(runtime,vars))
        {
            for(const auto &c:candidates)if(c.side==side)leases.erase(c.unit);
            continue;
        }
        for(const auto &order:pickupOrders)
        {auto unit=activeUnits.find(order.unit);if(unit!=activeUnits.end())Request(unit->second,order.intent);}
        for(const auto &order:result.commands)
        {auto unit=activeUnits.find(order.unit);if(unit!=activeUnits.end())Request(unit->second,order.intent);}
        const auto after=runtime.expedition.View();
        GGS_PlaySignal(before,after,result.started,activeUnits);
        if(before.awareness.phase!=after.awareness.phase || result.started || result.ended || result.failed ||
            runtime.lastSummary<0 || tick-runtime.lastSummary>=1091)
        {
            EngineDiagnostics::Event("GroupSearch event=SIDE zone=%s side=%d tick=%d phase=%d->%d active=%u memories=%u party=%u leader=%d pickup=%d route_pending=%d budget=%d keys=%u",
                zone.c_str(),side,tick,int(before.awareness.phase),int(after.awareness.phase),unsigned(activeIds.size()),
                unsigned(after.awareness.memories.size()),unsigned(after.awareness.party.size()),after.awareness.leader.id,
                pickupBusy?1:0,routeBusy?1:0,G_groupSearchPathBudget,unsigned(runtime.formattedKeys));
            runtime.lastSummary=tick;
        }
    }
    // Release by numeric identity. No cached pointer from an abandoned map is
    // dereferenced, and no private individual expedition can resume beside G1.
    for(auto& entry:g_addonCorpseSearch){
        auto& state=entry.second;if(state.corpseId<0)continue;
        const GroupSearch::Identity id{entry.first,state.observerSubtype};auto live=activeUnits.find(id);
        if(live==activeUnits.end()||live->second!=state.observer){ReleasePickup(entry.first,state,tick);continue;}
        auto side=G_groupSearchSides.find(live->second->GetPlayerNum());
        if(side==G_groupSearchSides.end()||!side->second.valid||
            !(side->second.expedition.Awareness().Save().leader==id)||
            !GGS_InspectPhase(side->second.expedition.Awareness().Save().phase))ReleasePickup(entry.first,state,tick);
    }
    for(auto it=revoked.begin();it!=revoked.end();){
        bool owned=false;for(const auto& side:G_groupSearchSides){const auto s=side.second.expedition.View();
            owned|=s.roster.homes.count(*it)!=0||s.roster.deferredReturns.count(*it)!=0;}
        if(!owned)it=revoked.erase(it);else ++it;
    }

}
}
