#pragma once
#include <unordered_map>

namespace ModEquipmentSwitch {
struct State { bool changed=false; int time=0; };
using Cache=std::unordered_map<int,State>;
inline Cache& RuntimeCache() { static Cache cache; return cache; }
// Native loaded IDs are inserted only when absent; no save data or actor pointer.
inline void AfterLoad(Cache& cache,int id,bool attacksActive)
{
    if (attacksActive) cache.emplace(id,State{});
}
// Only the registered StartAttack hook has this delay. Stock L1 paths stay stock.
inline bool BeforeAttack(Cache& cache,int id,unsigned current,unsigned wish,
                         bool attacksActive,bool noDelay)
{
    if (current==wish) return false;
    if (!attacksActive || noDelay) return true;
    auto found=cache.find(id);
    if (found==cache.end()) { cache.emplace(id,State{}); return true; }
    State& state=found->second;
    if (!state.changed) { state.changed=true; state.time=0; return false; }
    if (state.time<45) { ++state.time; return false; }
    state.changed=false; state.time=0; return true;
}
}
