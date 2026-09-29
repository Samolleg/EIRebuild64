#pragma once

// Engine-independent shared suspicion policy. Runtime sensing, serialization
// and selected-duty command ownership are separate, testable adapters.
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

#pragma pack(push, 8)
namespace GroupSearch
{
constexpr int MissingTicks = 2182; // 120 game seconds at 55 ms
constexpr int LookPeriodTicks = 1091; // Legacy save bound and search-retry delay; not look cadence.
constexpr int LookDurationTicks = 36; // about two game seconds
inline int LevelLookPeriodTicks(int level)
{ return level>=3?364:level==2?546:level==1?728:LookPeriodTicks; } // 20/30/40s at 55ms, rounded up

struct Identity
{
    int id = -1;
    uint32_t subtype = 0;
    bool operator<(const Identity &b) const
    { return id != b.id ? id < b.id : subtype < b.subtype; }
    bool operator==(const Identity &b) const
    { return id == b.id && subtype == b.subtype; }
};
struct Point { float x = 0, y = 0, z = 0; };
struct Observation { Identity unit; Point position; };
struct Memory
{
    Point lastSeen;
    int unseenTicks = 0;
    bool absent = false;
    uint64_t episode = 0; // new absence or newly confirmed body evidence
    bool bodyReported = false; // persisted by the optional body GS extension
};
struct Case
{
    Identity unit;
    uint64_t episode = 0;
    Point lastSeen;
    bool body = false;
};
enum class Phase { Duty, Gather, LastSeen, Inspect, Point5, Inspect5,
                   Point10, Inspect10, Return };
struct Snapshot
{
    unsigned version = 1;
    std::string zone;
    int side = -1;
    int floor = 0;
    int lookCountdown = LookPeriodTicks;
    int lookRemaining = 0;
    std::map<Identity, Memory> memories;
    std::vector<Case> pending; // oldest-to-newest; retry preserves this order
    std::vector<Case> active;
    std::vector<Identity> party;
    Identity leader;
    Phase phase = Phase::Duty;
    Point destination;
};

inline bool ValidSnapshot(const Snapshot &s)
{
    auto point = [](Point p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); };
    if (s.version != 1 || s.zone.empty() || s.side < 0 || s.floor < 0 || s.floor > 1 ||
        s.lookCountdown < 1 || s.lookCountdown > LookPeriodTicks ||
        s.lookRemaining < 0 || s.lookRemaining > LookDurationTicks ||
        static_cast<unsigned>(s.phase) > static_cast<unsigned>(Phase::Return) ||
        !point(s.destination)) return false;
    for (const auto &e : s.memories)
        if (e.first.id < 0 || !point(e.second.lastSeen) || e.second.unseenTicks < 0 ||
            e.second.unseenTicks > MissingTicks || (e.second.absent &&
                (e.second.episode == 0 || e.second.unseenTicks != MissingTicks))) return false;
    std::set<std::pair<Identity, uint64_t>> cases;
    for (const auto *list : {&s.pending, &s.active}) for (const auto &c : *list)
    {
        auto m = s.memories.find(c.unit);
        if (m == s.memories.end() || c.episode == 0 || c.episode > m->second.episode ||
            !point(c.lastSeen) || !cases.emplace(c.unit,c.episode).second) return false;
    }
    std::set<Identity> party;
    for (auto id : s.party) if (id.id < 0 || !party.insert(id).second) return false;
    if (s.phase == Phase::Duty) return s.party.empty() && s.active.empty() && s.leader.id == -1;
    if (s.active.empty()) return false;
    if (s.party.empty()) return s.phase == Phase::Return && s.leader.id == -1;
    return party.count(s.leader) != 0 && (s.party.size() >= 2 || s.phase == Phase::Return);
}

class Coordinator
{
    Snapshot state;
    static void RemoveCase(std::vector<Case> &cases, const Identity &id, uint64_t episode)
    {
        cases.erase(std::remove_if(cases.begin(), cases.end(), [&](const Case &c) {
            return c.unit == id && c.episode == episode;
        }), cases.end());
    }
public:
    Coordinator(std::string zone, int side) { state.zone = zone; state.side = side; }
    const Snapshot &Save() const { return state; }
    // Restore replaces all state, including on same-map rollback. No merging
    // with future memories or globals. Encoding/GS binding belongs to adapter.
    bool Restore(const Snapshot &saved)
    {
        if (saved.zone != state.zone || saved.side != state.side || !ValidSnapshot(saved)) return false;
        state = saved;
        // Keep legacy GS words readable; never wait beyond the new interval.
        state.lookCountdown = (std::min)(state.lookCountdown, LevelLookPeriodTicks(Level()));
        return true;
    }
    int Level() const
    {
        if (state.phase != Phase::Duty) return 3;
        if (std::any_of(state.pending.begin(),state.pending.end(),[](const Case &c){return c.body;})) return 3;
        return (std::min)(3, state.floor + static_cast<int>(state.pending.size()));
    }
    bool ShouldLook() const { return Level() > 0 && state.lookRemaining > 0; }
    void ResetLookClock()
    { state.lookCountdown=LevelLookPeriodTicks(Level());state.lookRemaining=0; }

    // Only the runtime's observed same-side corpse list may call this. The
    // body report is distinct from inferred absence, including a body found
    // after a previous search. Repeated sightings cannot mint more cases.
    bool ReportBody(Identity id,Point position)
    {
        if(id.id<0 || !std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))return false;
        auto &m=state.memories[id];if(m.bodyReported)return false;
        const int oldLevel=Level();
        RemoveCase(state.pending,id,m.episode);
        m.bodyReported=true;m.absent=true;m.unseenTicks=MissingTicks;m.lastSeen=position;++m.episode;
        state.pending.push_back({id,m.episode,position,true});
        if(oldLevel!=Level())ResetLookClock();
        return true;
    }
    void BodyHandled(Identity id)
    {
        const int oldLevel=Level();
        // Pickup during an older expedition also resolves its newly queued
        // direct-body report. Keep bodyReported, so sight/load stays silent.
        state.pending.erase(std::remove_if(state.pending.begin(),state.pending.end(),
            [&](const Case &c){return c.unit==id && c.body;}),state.pending.end());
        if(oldLevel!=Level())ResetLookClock();
    }

    // One complete sensing batch per side, not one Tick per observer. Visible
    // union must be collected before absence is evaluated. Membership/sight are
    // supplied by the engine; no global dead/alive query is used here.
    void Tick(int elapsedTicks, bool mapActive, bool observationAvailable,
              const std::vector<Observation> &visible)
    {
        if (!mapActive || !observationAvailable || elapsedTicks < 0) return;
        const int oldLevel = Level();
        std::set<Identity> seen;
        for (const auto &o : visible)
        {
            if (o.unit.id < 0 || !std::isfinite(o.position.x) ||
                !std::isfinite(o.position.y) || !std::isfinite(o.position.z)) continue;
            auto &m = state.memories[o.unit]; // immediate, never evicted by age
            if (m.absent)
            {
                RemoveCase(state.pending, o.unit, m.episode);
                // Active cases remain owned by this expedition until return;
                // no individual member peels off after a fleeting reunion.
            }
            m.lastSeen = o.position;
            m.unseenTicks = 0;
            m.absent = false;
            m.bodyReported = false; // genuinely observed alive again
            seen.insert(o.unit);
        }
        for (auto &entry : state.memories)
        {
            if (seen.count(entry.first)) continue;
            auto &m = entry.second;
            m.unseenTicks += (std::min)(elapsedTicks, MissingTicks - m.unseenTicks);
            if (!m.absent && m.unseenTicks >= MissingTicks)
            {
                m.absent = true;
                ++m.episode;
                state.pending.push_back({entry.first, m.episode, m.lastSeen});
            }
        }
        const int level=Level();
        if (level == 0 || oldLevel != level)
        {
            ResetLookClock();
        }
        else
        {
            state.lookRemaining = (std::max)(0, state.lookRemaining - elapsedTicks);
            state.lookCountdown -= elapsedTicks;
            if (state.lookCountdown <= 0)
            {
                state.lookRemaining = LookDurationTicks;
                state.lookCountdown = LevelLookPeriodTicks(level);
            }
        }
    }
    bool Begin(std::vector<Identity> participants, Identity leader, Identity body = {})
    {
        if (Level() < 3 || state.phase != Phase::Duty) return false;
        std::sort(participants.begin(), participants.end());
        participants.erase(std::unique(participants.begin(), participants.end()), participants.end());
        // No lone searcher. The adapter captures eligible active homes before
        // Begin; Gather advances only after actual selected-duty acknowledgments.
        if (participants.size() < 2 ||
            std::find(participants.begin(), participants.end(), leader) == participants.end()) return false;
        state.party = participants;
        // The witness may have a higher ID than its reinforcements. Slot zero
        // must still belong to the leader at the body, not the smallest ID.
        auto first=std::find(state.party.begin(),state.party.end(),leader);
        std::rotate(state.party.begin(),first,first+1);
        state.leader = leader;
        // Direct evidence outranks inferred absence. One expedition owns one
        // body destination; other observed bodies must NOT be silently marked
        // investigated when it returns from somewhere else.
        size_t bodyIndex=state.pending.size();
        for(size_t n=state.pending.size();n>0;--n)
            if(state.pending[n-1].body && (body.id<0 || state.pending[n-1].unit==body)){bodyIndex=n-1;break;}
        if(bodyIndex<state.pending.size())
        {
            state.active.push_back(state.pending[bodyIndex]);
            state.pending.erase(state.pending.begin()+bodyIndex);
        }
        else state.active.swap(state.pending);
        state.destination = state.active.back().lastSeen;
        state.phase = Phase::Gather;
        return true;
    }
    bool ReplaceLeader(Identity replacement)
    {
        if (state.phase == Phase::Duty ||
            std::find(state.party.begin(), state.party.end(), replacement) == state.party.end()) return false;
        state.leader = replacement;
        return true;
    }
    // Activity loss changes only expedition membership, NEVER side memory or
    // loss counters. A sole survivor returns; there is no lone search mission.
    bool RetainParty(const std::vector<Identity> &remaining)
    {
        if (state.phase == Phase::Duty) return false;
        std::set<Identity> unique;
        for (auto id : remaining)
            if (!unique.insert(id).second ||
                std::find(state.party.begin(), state.party.end(), id) == state.party.end()) return false;
        state.party.assign(unique.begin(), unique.end());
        if (unique.count(state.leader) == 0)
            state.leader = state.party.empty() ? Identity() : state.party.front();
        auto first=std::find(state.party.begin(),state.party.end(),state.leader);
        if(first!=state.party.end())std::rotate(state.party.begin(),first,first+1);
        if (state.party.size() < 2) state.phase = Phase::Return;
        return true;
    }
    // Advance only on verified arrival/inspection/return events. No timeout
    // or pose flag is equivalent to success. Blocked paths require explicit
    // AbortToReturn; combat adapter pauses/relinquishes commands.
    bool Arrived(bool wholeParty)
    {
        if (!wholeParty) return false;
        switch (state.phase)
        {
        case Phase::Gather: state.phase = Phase::LastSeen; return true;
        case Phase::LastSeen: state.phase = Phase::Inspect; return true;
        case Phase::Point5: state.phase = Phase::Inspect5; return true;
        case Phase::Point10: state.phase = Phase::Inspect10; return true;
        case Phase::Return:
            state.active.clear(); state.party.clear(); state.leader = Identity();
            state.floor = 1; state.phase = Phase::Duty;
            ResetLookClock();
            return true;
        default: return false;
        }
    }
    bool InspectionFinished(bool leaderCarryingBody = false)
    {
        if (state.phase != Phase::Inspect && state.phase != Phase::Inspect5 &&
            state.phase != Phase::Inspect10) return false;
        if (leaderCarryingBody || state.phase == Phase::Inspect10) state.phase = Phase::Return;
        else state.phase = state.phase == Phase::Inspect ? Phase::Point5 : Phase::Point10;
        return true;
    }
    bool AbortToReturn()
    {
        if (state.phase == Phase::Duty) return false;
        state.phase = Phase::Return;
        return true;
    }
};
}
#pragma pack(pop)
