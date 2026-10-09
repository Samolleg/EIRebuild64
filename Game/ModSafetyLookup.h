#pragma once

namespace ModSafetyLookup {
// One borrowed server-player snapshot; the predicate never receives null.
template<class Lookup, class ZonePredicate>
bool FinalBattle(bool lookupReady, Lookup lookup, ZonePredicate isFinalZone)
{
    if (!lookupReady) return false;
    auto* player = lookup();
    return player && isFinalZone(player);
}
inline float NightPower(float base, bool cave, bool finalBattle)
{
    if (cave) base = 1.f;
    if (finalBattle) base = 0.f;
    return base;
}
}
