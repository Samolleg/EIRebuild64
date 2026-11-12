#include "GamepadMission64.h"
#include "GamepadGestures64.h"

void GamepadMissionAccess::StepPose(CI3DScreenMission& mission, int direction) {
    if (!theWorld.GetPlayer()) return;
    for (int i = 0; i < mission.GetNumUnits(); ++i) {
        auto* unit = mission.GetUnit(i);
        if (!unit || unit->IsDead() || !unit->CheckFlag(OBJ_SELECTED)) continue;
        const int pose = Gamepad64::StepPose(int(unit->GetShowPose()), direction);
        if (pose != int(unit->GetShowPose()))
            theWorld.GetPlayer()->UnitCommandPose(unit, EUnitPose(pose));
    }
}
void GamepadMissionAccess::CycleHero(CI3DScreenMission& mission, int direction) {
    int count = 0, current = -1;
    mission.GetSelection(count, current);
    const int total = mission.GetNumUnits();
    for (int n = 0; n < total; ++n) {
        current = Gamepad64::NextSlot(current, total, direction);
        auto* unit = mission.GetUnit(current);
        if (unit && !unit->IsDead()) { mission.SelectionUpdate(current); return; }
    }
}
void GamepadMissionAccess::CycleWeapon(CI3DScreenMission& mission) {
    auto* bar = mission.pWeapons;
    if (!bar || !bar->pUnit) return;
    int slot = bar->iSelected;
    const int count = (std::min)(4, bar->nWeapons);
    for (int n = 0; n < count; ++n) {
        slot = Gamepad64::NextSlot(slot, count);
        if (bar->pUnit->GetQWeapon(slot)) { bar->ChangeWeapon(slot); return; }
    }
}
void GamepadMissionAccess::CycleItem(CI3DScreenMission& mission) {
    auto* bar = mission.pItems;
    if (!bar || !bar->pUnit) return;
    int slot = bar->iSelected;
    for (int n = 0; n < 4; ++n) {
        slot = Gamepad64::NextSlot(slot, 4);
        if (bar->pUnit->GetQItem(slot)) {
            if (slot != bar->iSelected) bar->ActivateQItem(slot);
            return;
        }
    }
}
int GamepadMissionAccess::Count(CI3DScreenMission& mission, bool right) {
    return right ? mission.pSpells->nSpells : mission.pSkills->nSkills;
}
int GamepadMissionAccess::Selected(CI3DScreenMission& mission, bool right) {
    return right ? mission.pSpells->iSelected : mission.pSkills->iSelected;
}
std::uint64_t GamepadMissionAccess::Token(CI3DScreenMission& mission, bool right) {
    std::uint64_t token = 1469598103934665603ull;
    const auto mix = [&](std::uint64_t value) { token = (token ^ value) * 1099511628211ull; };
    for (int i = 0; i < mission.GetNumUnits(); ++i) {
        auto* unit = mission.GetUnit(i);
        if (unit && unit->CheckFlag(OBJ_SELECTED)) { mix(unit->GetID()); mix(unit->IsDead()); }
    }
    const int count = Count(mission, right);
    mix(count);
    if (right) {
        auto* unit = mission.pSpells->pUnit;
        for (int i = 0; unit && i < count; ++i)
            mix(reinterpret_cast<std::uintptr_t>(unit->GetQSpellItem(i)));
    } else {
        for (int i = 0; i < count; ++i) mix(mission.pSkills->nTypes[i]);
    }
    return token;
}
void GamepadMissionAccess::Preview(CI3DScreenMission& mission, bool right, int slot) {
    // The legacy ShowSelect methods also write iSelected: restore that field
    // so highlighting cannot cast, select a target, or alter toggle semantics.
    const int actual = Selected(mission, right);
    const int highlighted = slot >= 0 ? slot : actual;
    for (int i = 0; i < Count(mission, right); ++i) {
        if (right) mission.pSpells->ShowSelectSpell(i, i == highlighted);
        else mission.pSkills->ShowSelectSkill(i, i == highlighted);
    }
    if (right) mission.pSpells->iSelected = actual;
    else mission.pSkills->iSelected = actual;
}
void GamepadMissionAccess::Commit(CI3DScreenMission& mission, bool right, int slot) {
    if (slot < 0 || slot >= Count(mission, right)) return;
    Preview(mission, right, -1);
    if (right) mission.pSpells->ChangeSpell(slot);
    else mission.pSkills->ChangeSkill(slot);
}
