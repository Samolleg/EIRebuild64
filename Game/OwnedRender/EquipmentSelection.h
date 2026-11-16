#pragma once
#include <array>
#include <cstddef>

namespace OwnedRender {
// Values supplied by the host's item database and actor-specific combat rules.
struct EquipmentWeapon {
    bool present = false, twoHanded = false, shield = false, staff = false;
};
template<std::size_t N>
int EquipmentLeftHand(const std::array<EquipmentWeapon, N>& slots, int current)
{
    if (current < 0 || current >= int(N) || !slots[current].present || N < 2)
        return -1;
    int shield = -1;
    for (int i = 0; i < int(N); ++i)
        if (slots[i].present && slots[i].shield) { shield = i; break; }
    const auto &held = slots[current], &first = slots[0], &dual = slots[1];
    if (!first.present || !dual.present || held.twoHanded) return -1;
    if (held.staff || first.staff || dual.staff) {
        if (current == 1) {
            if (dual.shield) return shield;
            return !first.twoHanded && first.staff && dual.staff ? 0 : -1;
        }
        if (shield >= 0) return shield;
        return !dual.twoHanded && dual.staff && held.staff ? 1 : -1;
    }
    if (current == 1) return dual.shield ? shield : (!first.twoHanded ? 0 : -1);
    return shield >= 0 ? shield : (!dual.twoHanded ? 1 : -1);
}
} // namespace OwnedRender
