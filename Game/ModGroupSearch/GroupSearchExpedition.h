#pragma once
#include "GroupSearchRoster.h"

#pragma pack(push, 8)
namespace GroupSearch
{
struct BodySighting { Identity observer, body; Point position; };
struct BodyWitness
{
    Identity observer, body;
    Point target;
    int quietTicks=0, travelTicks=0;
    bool looking=false;
};
struct ExpeditionSnapshot
{
    Snapshot awareness;
    RosterSnapshot roster;
    Point gather, point5, point10, trail;
    int phaseTicks = 0;
    int inspectTicks = 0;
    uint32_t serial = 1;
    uint32_t random = 1;
    bool waiting = false;
    bool interrupted = false;
    int startingFloor = 0;
    int retryTicks = 0;
    BodyWitness witness; // optional GS extension; base v1 stays unchanged
};
struct Feedback
{
    Identity unit;
    uint32_t serial = 0;
    Action action = Action::Native;
    bool fresh = false;
};
// Read-only, non-owning view valid only until the next mutation. Runtime
// queries/encoding do not need to copy the complete permanent ally memory.
struct ExpeditionView
{
    const Snapshot &awareness;
    const RosterSnapshot &roster;
    const Point &gather, &point5, &point10, &trail;
    int phaseTicks, inspectTicks;
    uint32_t serial, random;
    bool waiting, interrupted;
    int startingFloor, retryTicks;
    const BodyWitness &witness;
};
struct Assignment { Identity unit; Intent intent; };
struct ExpeditionResult
{
    std::vector<Assignment> commands;
    std::vector<Identity> forget; // sleeping/reassigned: no command or wake-up
    std::vector<Identity> pathFailed; // detached locally, not a group arrival
    std::vector<Identity> returnFailed; // explicit native fallback, not arrival
    std::vector<Identity> capped; // legacy oversized party: separate home return
    bool started = false;
    bool ended = false;
    bool failed = false;
    bool phaseChanged = false;
};
class Expedition
{
    Coordinator awareness;
    ExpeditionSnapshot saved;
    // Native-validated placements are transient, reconstructed before moving
    // after load. Existing GS schema remains unchanged.
    std::map<Identity,Point> searchPlaces;
    static constexpr int PhaseLimit = 2182; // two active minutes per travel leg
    static constexpr int InspectLimit = 91; // five active seconds, two turns
    static bool Near(Point a, Point b, float radius)
    { return Distance2(a,b) <= radius*radius && std::fabs(a.z-b.z) <= 2; }
    static Point Slot(Point center, size_t index)
    {
        if (!index) return center;
        const float angle = float(index)*2.39996323f;
        const float radius = 1.5f*std::sqrt(float(index));
        center.x += radius*std::cos(angle); center.y += radius*std::sin(angle);
        return center;
    }
    float RandomAngle()
    {
        saved.random = saved.random*1664525u+1013904223u;
        return float(saved.random & 0xFFFFu) * (6.2831853f/65536.0f);
    }
    void NextPhase()
    { saved.phaseTicks = saved.inspectTicks = 0; saved.waiting = false; searchPlaces.clear(); ++saved.serial; }
    Intent OrderFor(Order order, Point target, float angle = 0) const
    {
        Intent i; i.order=order; i.serial=saved.serial; i.target=target;
        i.angle=angle; return i;
    }
    Intent DutyLook(const Candidate &c) const
    {
        return OrderFor(awareness.ShouldLook()?Order::Look:Order::Release,c.position,
            float(c.unit.id%17)*0.37f+(awareness.Save().lookRemaining>LookDurationTicks/2?1.4f:-1.4f));
    }
    bool BodyNeedsWitness(Identity body) const
    {
        const auto &s=awareness.Save();const auto m=s.memories.find(body);
        if(m==s.memories.end()||!m->second.bodyReported)return true;
        return std::any_of(s.pending.begin(),s.pending.end(),[&](const Case &c){return c.body&&c.unit==body;});
    }
    bool PrepareWitness(int elapsed,const std::vector<Candidate> &active,
        const std::vector<Feedback> &feedback,const std::vector<BodySighting> &sightings,ExpeditionResult &out)
    {
        auto &w=saved.witness;const int side=awareness.Save().side;
        const Candidate *c=FindActive(active,w.observer);
        const BodySighting *seen=nullptr;
        for(const auto &s:sightings)if(s.observer==w.observer&&s.body==w.body){seen=&s;break;}
        if(w.observer.id>=0)
        {
            auto home=saved.roster.deferredReturns.find(w.observer);
            if(!c||home==saved.roster.deferredReturns.end()||!SameDuty(*c,home->second,side)||!seen||!BodyNeedsWitness(w.body))
            {w=BodyWitness{};++saved.serial;return false;}
            // No elapsed peaceful time survives combat, fear or animation.
            if(!c->available){w.quietTicks=0;w.looking=false;return false;}
        }
        else
        {
            if(saved.retryTicks)return false;
            float best=0;
            for(const auto &s:sightings)
            {
                const auto *candidate=FindActive(active,s.observer);
                if(!candidate||!candidate->eligible||!candidate->available||candidate->side!=side||
                    saved.roster.deferredReturns.count(s.observer)||!BodyNeedsWitness(s.body)||!Finite(s.position))continue;
                const float distance=Distance2(candidate->position,s.position);
                if(!seen||distance<best){seen=&s;c=candidate;best=distance;}
            }
            if(!seen)return false;
            w.observer=seen->observer;w.body=seen->body;w.target=seen->position;
            saved.roster.deferredReturns[w.observer]={c->position,c->alarm,c->model};
            ++saved.serial;
        }
        if(Distance2(w.target,seen->position)>0.01f)
        {w.target=seen->position;w.looking=false;w.quietTicks=0;++saved.serial;}
        bool arrived=false,ownedLook=false,failed=false;
        for(const auto &f:feedback)if(f.unit==w.observer&&f.serial==saved.serial&&f.fresh)
        {arrived|=f.action==Action::Arrived;ownedLook|=f.action==Action::Turn||f.action==Action::Keep;failed|=f.action==Action::Failed;}
        w.travelTicks=(std::min)(PhaseLimit,w.travelTicks+elapsed);
        if(failed||w.travelTicks>=PhaseLimit)
        {w=BodyWitness{};saved.retryTicks=LookPeriodTicks;++saved.serial;out.failed=true;return false;}
        const bool atBody=Near(c->position,w.target,2.5f);
        if(!atBody && w.looking){w.looking=false;w.quietTicks=0;++saved.serial;}
        if(!w.looking&&atBody&&arrived){w.looking=true;w.quietTicks=0;++saved.serial;ownedLook=false;}
        if(w.looking && ownedLook)w.quietTicks=(std::min)(LookDurationTicks,w.quietTicks+elapsed);
        Intent intent=OrderFor(w.looking?Order::Look:Order::Move,w.looking?c->position:w.target,
            w.quietTicks<LookDurationTicks/2?1.4f:-1.4f);
        intent.tolerance=2.5f;
        out.commands.push_back({w.observer,intent});
        return w.looking&&ownedLook&&w.quietTicks>=LookDurationTicks;
    }
    void CompleteReturn()
    {
        const auto old=awareness.Save();
        awareness.Arrived(true);
        if(saved.interrupted)
        {
            // An abandoned expedition did NOT investigate its cases. Keep
            // them pending, including when everyone slept, rather than silently
            // dropping suspicion to one. Retry after one active minute.
            auto restored=awareness.Save();restored.floor=saved.startingFloor;
            std::vector<Case> unresolved;
            for(const auto &c:old.active)
            {
                const auto m=restored.memories.find(c.unit);
                if(m!=restored.memories.end()&&m->second.absent&&m->second.episode==c.episode)
                    unresolved.push_back(c);
            }
            // Pending is oldest-to-newest; Begin chooses the newest case.
            // Cases discovered DURING this expedition must remain after its
            // older unresolved cases, including across save/load and retries.
            unresolved.insert(unresolved.end(),restored.pending.begin(),restored.pending.end());
            restored.pending.swap(unresolved);
            const bool otherBody=std::any_of(old.pending.begin(),old.pending.end(),[](const Case &c){return c.body;});
            awareness.Restore(restored);saved.retryTicks=otherBody?0:LookPeriodTicks;
        }
        saved.roster.homes.clear();NextPhase();
    }
public:
    static constexpr size_t MaxPartySize = 4;
    static constexpr float GatherTolerance = 5.0f;
    // Keep legacy point5/point10 fields and phase IDs for GS compatibility.
    static constexpr float FirstSearchRadius = 10.0f;
    static constexpr float SecondSearchRadius = 20.0f;
    static Point SearchSlot(Point center,size_t index) { return Slot(center,index); }
    Expedition(std::string zone, int side) : awareness(zone,side)
    { saved.awareness=awareness.Save(); saved.random=uint32_t(side)+1; }
    Coordinator &Awareness() { return awareness; }
    bool ReportBody(Identity id,Point position)
    {
        if(!awareness.ReportBody(id,position))return false;
        saved.retryTicks=0; // genuinely NEW evidence bypasses an old retry wait
        return true;
    }
    const Coordinator &Awareness() const { return awareness; }
    ExpeditionSnapshot Save() const
    { const auto s=View();return {s.awareness,s.roster,s.gather,s.point5,s.point10,s.trail,
        s.phaseTicks,s.inspectTicks,s.serial,s.random,s.waiting,s.interrupted,
        s.startingFloor,s.retryTicks,s.witness}; }
    ExpeditionView View() const
    { return {awareness.Save(),saved.roster,saved.gather,saved.point5,saved.point10,saved.trail,
        saved.phaseTicks,saved.inspectTicks,saved.serial,saved.random,saved.waiting,
        saved.interrupted,saved.startingFloor,saved.retryTicks,saved.witness}; }
    template<class State> static bool Valid(const State &s)
    {
        if (!Finite(s.gather) || !Finite(s.point5) || !Finite(s.point10) || !Finite(s.trail) ||
            s.phaseTicks < 0 || s.phaseTicks > PhaseLimit || s.inspectTicks < 0 ||
            s.inspectTicks > InspectLimit || s.serial == 0 || s.startingFloor<0 ||
            s.startingFloor>1 || s.retryTicks<0 || s.retryTicks>LookPeriodTicks) return false;
        if (s.roster.homes.size() != s.awareness.party.size()) return false;
        for (auto id : s.awareness.party) if (!s.roster.homes.count(id)) return false;
        for (const auto *list : {&s.roster.homes,&s.roster.deferredReturns})
            for (const auto &e : *list)
                if (e.first.id < 0 || !Finite(e.second.position) || e.second.alarm < 0 ||
                    e.second.model < 0) return false;
        for (const auto &e : s.roster.homes) if (s.roster.deferredReturns.count(e.first)) return false;
        if(s.witness.observer.id>=0 && (s.awareness.phase!=Phase::Duty || s.witness.body.id<0 ||
            !Finite(s.witness.target)||!s.roster.deferredReturns.count(s.witness.observer)||
            s.witness.quietTicks<0||s.witness.quietTicks>LookDurationTicks||
            s.witness.travelTicks<0||s.witness.travelTicks>PhaseLimit||
            (!s.witness.looking&&s.witness.quietTicks)))return false;
        return ValidSnapshot(s.awareness);
    }
    bool Restore(const ExpeditionSnapshot &s)
    {
        if(!Valid(s)||!awareness.Restore(s.awareness))return false;
        saved=s;saved.awareness=Snapshot{};searchPlaces.clear();return true;
    }
    // Replace a shared target only after native route validation. Do not reset
    // the travel deadline: repeated replans cannot extend a search forever.
    bool RetargetSearchPoint(Point point)
    {
        const auto phase=awareness.Save().phase;
        if(!Finite(point)||(phase!=Phase::Point5&&phase!=Phase::Point10))return false;
        (phase==Phase::Point5?saved.point5:saved.point10)=point;
        saved.waiting=false;++saved.serial;return true;
    }
    bool SetSearchPlaces(Point point,const std::map<Identity,Point> &places)
    {
        for(auto id:awareness.Save().party)
            if(!places.count(id)||!Finite(places.at(id)))return false;
        if(!RetargetSearchPoint(point))return false;
        searchPlaces=places;return true;
    }
    void AbortSearchRoute()
    { saved.interrupted=true;awareness.AbortToReturn();NextPhase(); }
    ExpeditionResult Step(int elapsed, const std::vector<Candidate> &active,
        const std::vector<Feedback> &feedback, bool pickupBusy = false,
        const std::vector<BodySighting> *sightings = nullptr, bool routeBusy = false)
    {
        ExpeditionResult out;
        elapsed=(std::max)(0,(std::min)(elapsed,PhaseLimit));
        saved.retryTicks=(std::max)(0,saved.retryTicks-elapsed);
        const int side=awareness.Save().side;
        bool witnessReady=false;
        if(sightings && awareness.Save().phase==Phase::Duty)
            witnessReady=PrepareWitness(elapsed,active,feedback,*sightings,out);
        for (auto it=saved.roster.deferredReturns.begin(); it!=saved.roster.deferredReturns.end();)
        {
            if(it->first==saved.witness.observer){++it;continue;}
            const auto *c=FindActive(active,it->first);
            if (!c) {++it; continue;} // no query/wake of an inactive unit
            if (!SameDuty(*c,it->second,side))
            {out.forget.push_back(it->first);it=saved.roster.deferredReturns.erase(it);continue;}
            if (!c->available) {++it;continue;}
            bool failed=false;
            for(const auto &f:feedback) if(f.unit==it->first && f.fresh &&
                f.serial==saved.serial && f.action==Action::Failed) failed=true;
            const bool done=Near(c->position,it->second.position,1.5f);
            out.commands.push_back({it->first,OrderFor(done||failed?Order::Release:Order::Move,it->second.position)});
            if(done||failed)
            {
                if(failed)out.returnFailed.push_back(it->first);
                out.failed|=failed;it=saved.roster.deferredReturns.erase(it);
            }
            else ++it;
        }
        if (awareness.Save().phase==Phase::Duty)
        {
            auto party=SelectActiveParty(side,active,saved.roster);
            const auto witness=saved.witness;
            if(witnessReady)party.insert(party.begin(),witness.observer);
            // Do not immediately re-enlist a unit which was released this tick.
            for(const auto &a:out.commands)if(!(witnessReady&&a.unit==witness.observer))
                party.erase(std::remove(party.begin(),party.end(),a.unit),party.end());
            if(party.size()>MaxPartySize)party.resize(MaxPartySize);
            const bool bodyPending=std::any_of(awareness.Save().pending.begin(),awareness.Save().pending.end(),[](const Case &c){return c.body;});
            const bool newBodySight=sightings&&std::any_of(sightings->begin(),sightings->end(),[&](const BodySighting &s){return BodyNeedsWitness(s.body);});
            const bool bodyGate=!sightings || witnessReady || (!bodyPending&&!newBodySight&&witness.observer.id<0);
            Home witnessHome;
            const bool startWitness=witnessReady&&party.size()>=2;
            if(startWitness)
            {
                witnessHome=saved.roster.deferredReturns.at(witness.observer);
                saved.roster.deferredReturns.erase(witness.observer);
                ReportBody(witness.body,witness.target);
            }
            if (bodyGate && awareness.Level()>=3 && saved.retryTicks==0 && party.size()>=2 && CaptureParty(saved.roster,side,party,active))
            {
                saved.startingFloor=awareness.Save().floor;
                if (!awareness.Begin(party,party.front(),startWitness?witness.body:Identity{})) {saved.roster.homes.clear();return out;}
                if(startWitness)
                {
                    saved.roster.homes[witness.observer]=witnessHome;
                    saved.witness=BodyWitness{};
                    out.commands.erase(std::remove_if(out.commands.begin(),out.commands.end(),[&](const Assignment &a){return a.unit==witness.observer;}),out.commands.end());
                }
                saved.gather=FindActive(active,party.front())->position;
                saved.trail=saved.gather;
                const Point center=awareness.Save().destination;
                const float a=RandomAngle(), b=RandomAngle();
                saved.point5={center.x+FirstSearchRadius*std::cos(a),center.y+FirstSearchRadius*std::sin(a),center.z};
                saved.point10={center.x+SecondSearchRadius*std::cos(b),center.y+SecondSearchRadius*std::sin(b),center.z};
                saved.interrupted=false;NextPhase();out.started=true;
            }
            else
            {
                if(startWitness)saved.roster.deferredReturns[witness.observer]=witnessHome;
                for(const auto &c:active)
                {
                    if (!c.active || !c.eligible || c.side!=side || !c.available ||
                        saved.roster.deferredReturns.count(c.unit)) continue;
                    const bool justReturned=std::any_of(out.commands.begin(),out.commands.end(),[&](const Assignment &a){return a.unit==c.unit;});
                    if(justReturned)continue;
                    out.commands.push_back({c.unit,DutyLook(c)});
                }
                return out;
            }
        }
        const Phase before=awareness.Save().phase;
        auto changes=ReconcileParty(saved.roster,side,awareness.Save().party,active);
        // Old saves may contain a larger expedition. Keep their memories and
        // original posts; surplus members return without joining the search.
        if(changes.remaining.size()>MaxPartySize)
        {
            for(size_t n=MaxPartySize;n<changes.remaining.size();++n)
            {
                const auto id=changes.remaining[n];
                saved.roster.deferredReturns[id]=saved.roster.homes.at(id);
                saved.roster.homes.erase(id);out.capped.push_back(id);
            }
            changes.remaining.resize(MaxPartySize);
            changes.paused=false;
            for(auto id:changes.remaining)
                if(!FindActive(active,id)->available)changes.paused=true;
        }
        out.forget.insert(out.forget.end(),changes.suspended.begin(),changes.suspended.end());
        out.forget.insert(out.forget.end(),changes.revoked.begin(),changes.revoked.end());
        const bool rosterChanged=!changes.suspended.empty()||!changes.revoked.empty()||!out.capped.empty();
        if(rosterChanged)
        {awareness.RetainParty(changes.remaining);if(before!=Phase::Return && changes.remaining.size()<2)saved.interrupted=true;++saved.serial;}
        Phase phase=awareness.Save().phase;
        if (phase!=before) {NextPhase();out.phaseChanged=true;}
        auto party=awareness.Save().party;
        // Bystanders use the side's level3 cadence. Searchers and deferred
        // returns keep their phase-owned commands, never periodic turns.
        for(const auto &c:active)
        {
            if(!c.active||!c.eligible||!c.available||c.side!=side||
                saved.roster.homes.count(c.unit)||saved.roster.deferredReturns.count(c.unit))continue;
            if(std::find(out.forget.begin(),out.forget.end(),c.unit)!=out.forget.end())continue;
            if(std::none_of(out.commands.begin(),out.commands.end(),[&](const Assignment &a){return a.unit==c.unit;}))
                out.commands.push_back({c.unit,DutyLook(c)});
        }
        if(changes.paused || pickupBusy || routeBusy)
        {
            // Peaceful peers wait; fighting members receive no new command.
            // No timeout is spent while a battle/animation owns the group.
            for(auto id:party)
            {
                const auto *c=FindActive(active,id);
                if(c && c->available && (!pickupBusy || !(id==awareness.Save().leader)))
                    out.commands.push_back({id,OrderFor(Order::Hold,c->position)});
            }
            return out;
        }
        saved.phaseTicks=(std::min)(PhaseLimit,saved.phaseTicks+elapsed);
        std::set<Identity> failedMembers;
        for(const auto &f:feedback)
            if(f.fresh && f.serial==saved.serial && f.action==Action::Failed &&
                std::find(party.begin(),party.end(),f.unit)!=party.end()) failedMembers.insert(f.unit);
        if(!failedMembers.empty())
        {
            // A failed path belongs to this member, not to every participant.
            // Preserve its original post in the existing serialized return
            // ticket; exclude it from regrouping until that return is resolved.
            const bool returning=phase==Phase::Return;
            for(auto id:failedMembers)
            {
                auto home=saved.roster.homes.find(id);
                if(!returning)saved.roster.deferredReturns.emplace(id,home->second);
                saved.roster.homes.erase(home);
                (returning?out.returnFailed:out.pathFailed).push_back(id);
                party.erase(std::remove(party.begin(),party.end(),id),party.end());
            }
            awareness.RetainParty(party);
            if(!returning && party.size()<2)saved.interrupted=true;
            const Phase after=awareness.Save().phase;
            if(after!=phase){NextPhase();out.phaseChanged=true;phase=after;}
            else ++saved.serial; // stale approach feedback cannot fail the return
            out.failed=true;
            for(auto id:failedMembers)
                out.commands.push_back({id,OrderFor(returning?Order::Release:Order::Move,
                    returning?Point{}:saved.roster.deferredReturns.at(id).position)});
        }
        if(saved.phaseTicks>=PhaseLimit)
        {
            // A group-wide deadline is still a real interruption. Failure of
            // the final return itself does not undo completed investigation.
            if(phase!=Phase::Return)saved.interrupted=true;
            out.failed=true;
            if(phase==Phase::Return)
            {
                // An impossible return is explicitly NOT reported as arrival.
                // Release to native duty (whose route has never been replaced).
                for(auto id:party) out.commands.push_back({id,OrderFor(Order::Release,{})});
                saved.roster.homes.clear();awareness.RetainParty({});
                CompleteReturn();out.ended=true;return out;
            }
            awareness.AbortToReturn();NextPhase();out.phaseChanged=true;phase=Phase::Return;
        }
        bool allArrived=true, allOwned=true;
        const float arrivalTolerance=(phase==Phase::Gather||phase==Phase::LastSeen||
            phase==Phase::Point5||phase==Phase::Point10)?GatherTolerance:1.5f;
        const bool inspect=phase==Phase::Inspect || phase==Phase::Inspect5 || phase==Phase::Inspect10;
        Point destination=phase==Phase::Gather?saved.gather:
            (phase==Phase::Point5||phase==Phase::Inspect5)?saved.point5:
            (phase==Phase::Point10||phase==Phase::Inspect10)?saved.point10:awareness.Save().destination;
        const auto *leader=FindActive(active,awareness.Save().leader);
        const bool travel=phase==Phase::LastSeen||phase==Phase::Point5||phase==Phase::Point10;
        if(travel && leader)
        {
            float furthest=0;
            for(auto id:party)
            {
                const auto *c=FindActive(active,id);if(!c)continue;
                // Already waiting at a validated destination is not lagging
                // behind the leader. Otherwise independent paths can deadlock.
                if(searchPlaces.count(id)&&Near(c->position,searchPlaces.at(id),arrivalTolerance))continue;
                furthest=(std::max)(furthest,Distance2(c->position,leader->position));
            }
            const float radius=1.5f*std::sqrt(float(party.size()));
            if(furthest>(radius+8)*(radius+8))saved.waiting=true;
            if(furthest<(radius+4)*(radius+4))saved.waiting=false;
            if(Distance2(saved.trail,leader->position)>=9) saved.trail=leader->position;
        }
        size_t slot=0;
        for(auto id:party)
        {
            const auto *c=FindActive(active,id);
            if(!c) {allArrived=false;allOwned=false;continue;}
            Point target=phase==Phase::Return?saved.roster.homes.at(id).position:Slot(destination,slot);
            const bool plannedSearch=(phase==Phase::Point5||phase==Phase::Point10)&&searchPlaces.count(id);
            if(plannedSearch)target=searchPlaces.at(id);
            bool owned=false;
            bool nativeArrived=false;
            for(const auto &f:feedback) if(f.unit==id && f.fresh && f.serial==saved.serial &&
                f.action!=Action::Native && f.action!=Action::Yield && f.action!=Action::Released)
            {owned=true;nativeArrived|=f.action==Action::Arrived;}
            allOwned&=owned;
            allArrived&=Near(c->position,target,arrivalTolerance)&&owned&&
                (!(plannedSearch||phase==Phase::LastSeen)||nativeArrived);
            Intent i=OrderFor(Order::Move,target);
            i.tolerance=arrivalTolerance;
            i.requireNativeArrival=plannedSearch||phase==Phase::LastSeen;
            if(phase==Phase::LastSeen)i.nativeEndpointTolerance=arrivalTolerance;
            if(inspect) i=OrderFor(Order::Look,c->position,float(slot)*0.8f+(saved.inspectTicks<InspectLimit/2?1.5f:-1.5f));
            else if(plannedSearch)
            {
                // Each member follows a validated path to its own nearby place;
                // no unchecked formation offsets along a cave wall. Leader still
                // waits for lagging peers and the phase waits for every arrival.
                if(id==awareness.Save().leader && saved.waiting)i=OrderFor(Order::Hold,c->position);
            }
            else if(travel && leader && !Near(leader->position,destination,arrivalTolerance))
            {
                if(id==awareness.Save().leader)
                {
                    i=OrderFor(saved.waiting?Order::Hold:Order::Move,destination);
                    i.tolerance=arrivalTolerance;
                    i.requireNativeArrival=true;
                    i.nativeEndpointTolerance=arrivalTolerance;
                }
                else i=OrderFor(Order::Move,Slot(saved.trail,slot));
            }
            i.run=phase==Phase::Gather && i.order==Order::Move;
            out.commands.push_back({id,i});++slot;
        }
        if(inspect)
        {
            if(allOwned)saved.inspectTicks=(std::min)(InspectLimit,saved.inspectTicks+elapsed);
            if(saved.inspectTicks>=InspectLimit) {awareness.InspectionFinished();NextPhase();out.phaseChanged=true;}
        }
        else if(allArrived)
        {
            if(phase!=Phase::Return){awareness.Arrived(true);NextPhase();}
            out.phaseChanged=true;
            if(phase==Phase::Return)
            {
                for(auto &a:out.commands) if(std::find(party.begin(),party.end(),a.unit)!=party.end())a.intent.order=Order::Release;
                CompleteReturn();out.ended=true;out.failed|=saved.interrupted;
            }
        }
        return out;
    }
};
}
#pragma pack(pop)
