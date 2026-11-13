#pragma once
#include <cstdint>
class CI3DScreenMission;

// Native mission commands, independent of keyboard remapping and modifier keys.
// Preview only changes icon appearance; Commit invokes the same action as a click.
struct GamepadMissionAccess {
    static void TrackHero(CI3DScreenMission& mission);
    static void North(CI3DScreenMission& mission);
    static void StepPose(CI3DScreenMission& mission, int direction);
    static void CycleHero(CI3DScreenMission& mission, int direction);
    static void CycleWeapon(CI3DScreenMission& mission);
    static void CycleItem(CI3DScreenMission& mission);
    static int Count(CI3DScreenMission& mission, bool right);
    static int Selected(CI3DScreenMission& mission, bool right);
    static std::uint64_t Token(CI3DScreenMission& mission, bool right);
    static void Preview(CI3DScreenMission& mission, bool right, int slot);
    static void Commit(CI3DScreenMission& mission, bool right, int slot);
};
