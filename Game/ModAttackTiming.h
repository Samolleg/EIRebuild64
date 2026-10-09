#pragma once
#include <cstring>

// Bound addon source: GetSpeeds changes movement only; rotation stays retail.
namespace ModAttackTiming {
struct MovementSpeeds { float move, rotate; };
inline MovementSpeeds ScaleMovement(MovementSpeeds speeds, float factor) {
    speeds.move *= factor;
    return speeds;
}

// Native live ordinary-animation branch in call_L1ExecAttack_00549160.
inline bool IsOrdinaryName(const char* animation) {
    return animation && (std::strncmp(animation, "uattack", 7) == 0 ||
        (std::strlen(animation) >= 10 && std::strncmp(animation + 3, "uattack", 7) == 0));
}
inline bool IsDatName(const char* animation) {
    return animation && std::strncmp(animation, "dat", 3) == 0;
}
// Legacy predicate retained for excluded planned-dat and disabled paths.
inline bool UsesImpactFrame(bool addonAttacks, bool attack, bool skillTagged, const char* animation) {
    return addonAttacks && attack && !skillTagged && IsOrdinaryName(animation);
}
// Native DisableRunaway changes cancellation, not the recheck call itself.
inline bool CancelFailedRecheck(bool disableRunaway) { return !disableRunaway; }
template<class Build, class Apply>
inline void ExecuteImpact(bool enabled, bool targetAlive, int now, int hitTime, Build build, Apply apply) {
    if (enabled && targetAlive && now == hitTime - 1) {
        build();
        apply();
    }
}
}
