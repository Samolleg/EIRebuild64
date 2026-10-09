// Included in aiUnitLogic.cpp after its existing corpse/death-fear helpers.
// Source: 39454a3 GroupSearchNative.inl / GroupSearchPath.inl; native types only.
namespace ModGroupSearch {
static void ResetCoordinator();
static bool OwnsPickupAnimation(CUnit*,CUnitServer*,int);
namespace {
struct Lease {
    GroupSearch::Movement movement;
    GroupSearch::Intent intent;
    void* duty = nullptr;
    int side = -1, alarm = -1, model = -1;
    GroupSearch::Action action = GroupSearch::Action::Native;
    int tick = -1;
    uint32_t serial = 0;
    int diagnosticTick=-1;
};
std::map<GroupSearch::Identity, Lease> leases;
std::set<GroupSearch::Identity> active, revoked;
std::string zone;
GroupSearch::Identity Id(CUnit* unit) { return {unit->GetID(), unit->GetSubtypeID()}; }
GroupSearch::Point Point(const CPlot3D& p) { return {p.x,p.y,p.z}; }
bool Context() {
    CPlayer* player = theWorldServer.GetPlayer(0);
    const char* name = player ? player->party.szCurrentZone.GetString() : nullptr;
    if (!name || !*name) { Reset(); return false; }
    if (zone != name) { Reset(); zone = name; }
    return true;
}
bool Eligible(CAIUnitLogic& logic) {
    CUnit* unit = logic.GetUnit();
    CUnitServer* server = logic.GetUnitServer();
    const SGeneralUnitLogic* general = logic.GetCurrentGeneralLogic();
    return unit && server && !unit->IsDead() && !unit->IsHero() &&
        !unit->GetOwner() && !server->IsTamed() && unit->GetPrototype() &&
        AddonRaceCanInvestigate(unit->GetPrototype()->szRace.GetString()) &&
        general && (general->eLogicModel == UM_GUARD ||
                    general->eLogicModel == UM_PATROL || general->eLogicModel == UM_SENTRY) &&
        (general->aggressionMode == UAM_AGGRESSIVE || general->aggressionMode == UAM_REVENGE);
}
bool PriorityBlocked(CAIUnitLogic& logic) {
    CUnitServer* s = logic.GetUnitServer();
    const EUnitState state = logic.GetScriptState();
    return ModDeathFearActive(logic.GetUnit()) || state == US_ATTACK ||
        state == US_CAST || state == US_SKILL || s->HasAttack() ||
        s->HasCast() || s->HasSkill() || s->IsAggressive();
}
bool Protected(CAIUnitLogic& logic) {
    const auto& current=logic.GetUnitServer()->GetScriptCommand();
    const auto& next=logic.GetUnitServer()->GetNextScriptCommand();
    return PriorityBlocked(logic) || current.cmd==UC_PLAYANIM || current.cmd==UC_PLAYANIMCYCLE ||
        next.cmd==UC_PLAYANIM || next.cmd==UC_PLAYANIMCYCLE;
}
struct Commands {
    CAIUnitLogic& logic;
    bool run;
    void Move(GroupSearch::Point p) { logic.StartMove(CPlot3D(p.x,p.y,p.z),run,false); }
    void Stop() { logic.StartIdle(); logic.GetUnitServer()->StopGroupSearchMotion(); }
    void Turn(float angle) { Stop(); logic.TurnGroupSearch(angle); }
};
}
void Reset() { leases.clear(); active.clear(); revoked.clear(); zone.clear(); ResetCoordinator(); }
void SetActive(const std::set<GroupSearch::Identity>& units) {
    if (!Context()) return;
    active = units;
    for (auto it=leases.begin(); it!=leases.end();)
        if (!active.count(it->first)) it=leases.erase(it); else ++it;
}
bool Request(CUnit* unit, const GroupSearch::Intent& intent) {
    if (!unit || unit->GetID()<0 || !GroupSearch::Finite(intent.target) || !Context()) return false;
    auto id=Id(unit); if (!active.count(id) || revoked.count(id)) return false;
    auto it=leases.find(id);
    if (it==leases.end()) {
        if (intent.order==GroupSearch::Order::None || intent.order==GroupSearch::Order::Release) return false;
        Lease lease; lease.side=unit->GetPlayerNum(); it=leases.emplace(id,lease).first;
    }
    it->second.intent=intent; return true;
}
void Forget(GroupSearch::Identity unit) { leases.erase(unit); revoked.erase(unit); }
std::set<GroupSearch::Identity> Revoked() { return revoked; }
std::vector<Feedback> ReadFeedback() {
    std::vector<Feedback> result;
    for (const auto& e:leases) result.push_back({e.first,e.second.serial,e.second.action,e.second.tick});
    return result;
}
bool CommandDuty(void* duty, CAIUnitLogic& logic) {
    if (!duty || !logic.GetUnitServer() || !logic.GetUnit() || !Context()) return false;
    CUnit* unit=logic.GetUnit(); auto id=Id(unit); auto it=leases.find(id);
    if (it==leases.end()) return false;
    auto& lease=it->second; const auto* general=logic.GetCurrentGeneralLogic();
    if (!active.count(id) || !Eligible(logic) || unit->GetPlayerNum()!=lease.side ||
        (lease.duty && (lease.duty!=duty || lease.alarm!=logic.GetGroupSearchAlarm() ||
                       lease.model!=int(general->eLogicModel)))) {
        leases.erase(it); revoked.insert(id); return false;
    }
    auto* server=logic.GetUnitServer(); GroupSearch::MotionInput input;
    input.tick=theWorldServer.GetSegmentTimeCounter(); input.dutySelected=true; input.eligible=true;
    const bool pickupOwned=!PriorityBlocked(logic) && lease.duty && lease.movement.OwnsCommand() &&
        lease.intent.order==GroupSearch::Order::Hold && OwnsPickupAnimation(unit,server,input.tick);
    input.protectedCommand=Protected(logic); input.position=Point(unit->GetCP());
    input.moving=server->HasMove(); input.turning=server->HasRotate(); input.failed=server->IsCmdFailed();
    if (lease.intent.requireNativeArrival)
        input.pathAtTarget=server->GroupSearchPathMatches(
            CPlot3D(lease.intent.target.x,lease.intent.target.y,lease.intent.target.z),
            lease.intent.nativeEndpointTolerance);
    Commands commands{logic,lease.intent.run};
    auto action=GroupSearch::Execute(lease.movement,lease.intent,input,commands);
    if (lease.movement.OwnsCommand() && !lease.duty) {
        lease.duty=duty; lease.alarm=logic.GetGroupSearchAlarm(); lease.model=int(general->eLogicModel);
    }
    if((lease.action!=action || lease.serial!=lease.intent.serial) &&
        (lease.diagnosticTick<0 || input.tick-lease.diagnosticTick>=182)){
        EngineDiagnostics::Event("GroupSearch event=COMMAND zone=%s side=%d unit=%d subtype=%u serial=%u order=%d action=%d blocked=%d pickup_owned=%d reason=%s",
            zone.c_str(),lease.side,id.id,id.subtype,lease.intent.serial,int(lease.intent.order),int(action),
            input.protectedCommand?1:0,pickupOwned?1:0,lease.movement.Reason());lease.diagnosticTick=input.tick;
    }
    lease.action=action; lease.tick=input.tick; lease.serial=lease.intent.serial;
    if (pickupOwned) return true; // exact owned clip suppresses only the selected duty
    if (action==GroupSearch::Action::Released || action==GroupSearch::Action::Native) {
        if (lease.intent.order==GroupSearch::Order::Release || lease.intent.order==GroupSearch::Order::None)
            leases.erase(it);
        return false;
    }
    return action!=GroupSearch::Action::Yield;
}
bool QueryPath(CUnitServer* server, GroupSearch::Point wanted,
               GroupSearch::Point& endpoint, bool& partial, int& cost) {
    if (!server || !server->GetUnit() || !GroupSearch::Finite(wanted)) return false;
    const auto current=Point(server->GetUnit()->GetCP());
    if (GroupSearch::Distance2(current,wanted)<0.000001f && std::fabs(current.z-wanted.z)<0.001f)
        { endpoint=wanted; partial=false; cost=0; return true; }
    SAIPath probe;
    const bool ok=theWorldServer.aiMap.NewPath(server->GetUnit(),CPlot3D(wanted.x,wanted.y,0),probe);
    endpoint=Point(probe.GetFinalPoint()); partial=probe.bRouteFailed; cost=probe.nPathCost;
    return ok;
}
}
