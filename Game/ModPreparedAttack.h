#pragma once

// Bound native callback: prepare on selected hand, deliver first, schedule second,
// then cancel a ready skill. This owns no timing, origin or serialized state.
namespace ModPreparedAttack {
inline int HitSlot(bool leftHand, int current, int left) { return leftHand ? left : current; }
template<class Cancel>
inline bool RejectPreparation(int prepared, Cancel cancel) {
    if (prepared >= 0) return false;
    cancel();
    return true;
}
template<class Retain, class Apply, class Schedule>
inline void DeliverProjectiles(int count, Retain retain, Apply apply, Schedule schedule) {
    if (count == 2) {
        auto saved = retain();
        apply(0, count, true);
        schedule(saved);
    } else {
        for (int i = 0; i < count; ++i) apply(i, count, i == 0);
    }
}
template<class Cancel>
inline void CancelReady(bool ready, Cancel cancel) { if (ready) cancel(); }
}
