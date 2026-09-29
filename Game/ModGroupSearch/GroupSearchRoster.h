#pragma once
#include "GroupSearchMovement.h"
#include <set>

#pragma pack(push, 8)
namespace GroupSearch
{
// A complete snapshot of ACTIVE units only. No World::GetUnit fallback and no
// wake-up operation belong to this interface. Missing entries are not dead;
// they merely cannot receive expedition commands during this batch.
struct Candidate
{
    Identity unit;
    int side = -1;
    bool active = false;
    bool eligible = false;
    bool available = false; // combat/fear/script animation take priority
    Point position;
    int alarm = -1;
    int model = -1;
};
struct Home
{
    Point position;
    int alarm = -1;
    int model = -1;
};
struct RosterSnapshot
{
    std::map<Identity, Home> homes;
    // A sleeper is NOT woken to return. On a later active batch this ticket is
    // usable only if side/duty identity still matches; otherwise it is revoked.
    std::map<Identity, Home> deferredReturns;
};
inline bool SameDuty(const Candidate &c, const Home &h, int side)
{ return c.active && c.eligible && c.side == side && c.alarm == h.alarm && c.model == h.model; }
inline std::vector<Identity> SelectActiveParty(int side,
    const std::vector<Candidate> &candidates, const RosterSnapshot &roster)
{
    std::set<Identity> result;
    for (const auto &c : candidates)
        if (c.active && c.eligible && c.available && c.unit.id >= 0 &&
            c.side == side && Finite(c.position) &&
            roster.deferredReturns.count(c.unit) == 0)
            result.insert(c.unit);
    return {result.begin(), result.end()};
}
inline const Candidate *FindActive(const std::vector<Candidate> &candidates, Identity id)
{
    for (const auto &c : candidates)
        if (c.active && c.unit == id) return &c;
    return nullptr;
}
inline bool CaptureParty(RosterSnapshot &roster, int side,
    const std::vector<Identity> &party, const std::vector<Candidate> &candidates)
{
    std::map<Identity, Home> homes;
    for (const auto &id : party)
    {
        const auto *c = FindActive(candidates, id);
        if (!c || !c->eligible || !c->available || c->side != side ||
            !Finite(c->position) || roster.deferredReturns.count(id)) return false;
        if (!homes.emplace(id, Home{c->position, c->alarm, c->model}).second) return false;
    }
    if (homes.size() < 2) return false;
    roster.homes.swap(homes);
    return true;
}
struct RosterChanges
{
    std::vector<Identity> remaining;
    std::vector<Identity> suspended;
    std::vector<Identity> revoked;
    bool paused = false;
};
inline RosterChanges ReconcileParty(RosterSnapshot &roster, int side,
    const std::vector<Identity> &party, const std::vector<Candidate> &candidates)
{
    RosterChanges out;
    for (const auto &id : party)
    {
        auto home = roster.homes.find(id);
        const auto *c = FindActive(candidates, id);
        if (home == roster.homes.end()) { out.revoked.push_back(id); continue; }
        if (!c)
        {
            roster.deferredReturns[id] = home->second;
            out.suspended.push_back(id);
            roster.homes.erase(home);
        }
        else if (!SameDuty(*c, home->second, side))
        {
            out.revoked.push_back(id);
            roster.homes.erase(home); // never restore a superseded script duty
        }
        else
        {
            out.remaining.push_back(id);
            if (!c->available) out.paused = true;
        }
    }
    return out;
}
}
#pragma pack(pop)
