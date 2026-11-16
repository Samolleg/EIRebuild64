#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace owned {
// Cold immutable selection. Preview clients publish their own local arrays;
// the renderer never looks up a world unit or a mutable inventory.
struct VisualEquipmentItem {
    enum class Kind { weapon, armor };
    Kind kind = Kind::weapon;
    std::string shape, material;
    // hand_r, hand_l, or armor_<host slot>. Registry entries select sockets.
    std::string slot;
};
} // namespace owned
