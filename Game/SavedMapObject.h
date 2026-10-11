#pragma once
#include "SaveLoader.h"
#include "TypeIDs.h"

// Read-only grammar of MapObject/UnitServer/Unit and StuffMapObjects saves.
// A discarded record must never construct a world object, resolve a reference,
// run a setter, or write into the input. Sizes are checked against native types
// at the call site. Keep version branches paired with the corresponding Load.
namespace SavedMapObject {
inline void Bytes(CSave& f, unsigned n) {
    if (f.Require(n, "truncated_discarded_object")) f.Seek(f.GetPosition() + n);
}
inline int Int(CSave& f) { int n = 0; f.Read(n); return n; }
inline void Text(CSave& f) { char c = 1; while (c && f.IsOk()) f.Read(c); }
inline int Count(CSave& f, unsigned minimum = 1) {
    int n = Int(f);
    if (n < 0 || static_cast<unsigned>(n) > f.Remaining() / minimum) {
        f.Fail("invalid_discarded_array"); return 0;
    }
    return n;
}
inline void Array(CSave& f, unsigned size) { Bytes(f, Count(f, size) * size); }
inline void Names(CSave& f) { for (int n = Count(f); n-- > 0;) Text(f); }
inline void Animation(CSave& f) { Array(f, 12); Bytes(f, 4); }
inline void Rally(CSave& f) {
    Bytes(f, 12); Array(f, f.GetVersion() < 68 ? 12 : 13);
}
inline void GeneralLogic(CSave& f) {
    Bytes(f, 4);
    for (int n = Count(f, 16); n-- > 0;) { Bytes(f, 12); Array(f, 20); }
    Bytes(f, 12 + 4 + 4 + 4 + 4 + 4 + 4 + 4 + 4);
    if (f.GetVersion() >= 83) Bytes(f, 4);
}
inline void Logic(CSave& f) {
    Bytes(f, 4); Rally(f); Bytes(f, 56);
    int n = Count(f, 4); Bytes(f, 4); // active motivation index
    while (n-- > 0) {
        switch (Int(f)) {
        case 0: Bytes(f, f.GetVersion() < 102 ? 20 : 24); break; // guard
        case 1: for (int k = Count(f, 16); k-- > 0;) Rally(f); Bytes(f, 4); break;
        case 2: Bytes(f, 12); break; // sentry
        case 3: Bytes(f, 16); break; // suspection
        case 4: Bytes(f, 5); break; // aggression
        case 5: break; // corpse watcher
        case 6: case 8: // player/tamed
            Bytes(f, 24);
            if (f.GetVersion() >= 70 && f.GetVersion() < 94) Array(f, 4);
            break;
        case 7: // fear
            Bytes(f, 20);
            if (f.GetVersion() >= 83) Bytes(f, 1);
            if (f.GetVersion() >= 112) Bytes(f, 12);
            break;
        case 10: Bytes(f, 4); break; // follow
        default: f.Fail("unknown_discarded_motivation"); return;
        }
    }
    // CAIUnitEnvironment: nullable object lists, interest, ignored targets, sounds.
    for (int k = 0; k < 4; ++k) Array(f, 4);
    Bytes(f, 16); Array(f, 8);
    for (int k = Count(f, 21); k-- > 0;) { Bytes(f, 16); Text(f); Bytes(f, 4); }
    Bytes(f, 6); // int interest decrease, two char alarm fields
    for (int k = Count(f, 44); k-- > 0;) GeneralLogic(f);
    Bytes(f, 5); // alarm delay + always on the run
    if (f.GetVersion() >= 75) {
        Array(f, f.GetVersion() < 90 ? 24 : 40);
        if (f.GetVersion() >= 90) { Array(f, 40); Bytes(f, 4); }
    }
    if (f.GetVersion() >= 113) Bytes(f, 1);
}
inline void Command(CSave& f) { Bytes(f, f.GetVersion() < 72 ? 29 : 30); }
inline void UnitServer(CSave& f) {
    Bytes(f, 2); Animation(f);
    // SAIPath: toGo route, then the CAIFullRoute current segment.
    Array(f, 8); Bytes(f, 32); Array(f, 8); Bytes(f, 24); Array(f, 8); Bytes(f, 4);
    Animation(f); Bytes(f, 26);
    if (f.GetVersion() >= 72) Bytes(f, 12);
    Bytes(f, 4); Animation(f); Bytes(f, 8 + 16 + 12);
    Command(f); Command(f);
    Bytes(f, 1796 - (f.GetVersion() < 69 ? 12 : f.GetVersion() < 80 ? 8 : f.GetVersion() < 99 ? 4 : 0));
    Bytes(f, 36); Logic(f); Bytes(f, 16 + 2 + 4 + 4);
    Array(f, 8); Bytes(f, 5); // continuous spells and train
    if (f.GetVersion() < 68) return;
    Bytes(f, 4);
    if (f.GetVersion() < 72) return;
    Bytes(f, 4);
    if (f.GetVersion() < 74) return;
    Bytes(f, 1);
    if (f.GetVersion() < 76) return;
    Bytes(f, 16);
    if (f.GetVersion() < 84) return;
    Bytes(f, 4);
    if (f.GetVersion() < 105) return;
    Bytes(f, 60);
    if (f.GetVersion() >= 115) Bytes(f, 4); // disk field, no legacy Load's Write
}
inline void Common(CSave& f) {
    Bytes(f, 88); Text(f); Text(f); Names(f); // MapObjectServer
    Bytes(f, 32); Text(f); Bytes(f, 12); Array(f, 14);
    Bytes(f, 4); Text(f);
    if (f.GetVersion() > 78) Bytes(f, 12);
}
inline void Skip(CSave& f, int type) {
    Common(f);
    switch (type) {
    case MOTID_HUMAN: case MOTID_MONSTER: case MOTID_ANIMAL: case MOTID_UNIT:
        UnitServer(f); Text(f); Bytes(f, 50); // two byte-sized animation IDs
        Array(f, 28); Array(f, 12); Array(f, 4); Bytes(f, 20 + 16);
        Bytes(f, (4 + 4 + (f.GetVersion() >= 117 ? 16 : 8) + 7) * 4);
        Bytes(f, 20 + 4); Array(f, 4); Bytes(f, 4);
        if (f.GetVersion() < 73) return;
        Bytes(f, 4);
        if (f.GetVersion() < 77) return;
        Text(f);
        if (f.GetVersion() >= 106) Text(f);
        break;
    case MOTID_TREE: case MOTID_BUILDING: case MOTID_STONE: case MOTID_OBJECT: break;
    case MOTID_KAPKAN: Bytes(f, 4); break;
    case MOTID_LEVER: Bytes(f, 12 + 12 + 2); break;
    case MOTID_TORCH: Bytes(f, 4); Text(f); Bytes(f, 8 + 16 + 24); break;
    case MOTID_TRAP: // spell, areas, trapped persons, target points, casting fields
        Bytes(f, 4); Array(f, 12); Array(f, 4); Array(f, 8); Bytes(f, 16); break;
    default: f.Fail("unknown_discarded_dynamic_type"); break;
    }
}
}
