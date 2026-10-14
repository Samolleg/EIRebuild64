#pragma once

#include <cstddef>
#include <cstdint>

namespace owned::critters {

// Supported game.exe: 530970 resolves subtype +18 through 7AF358/7AF35C
// (288-byte prototypes), then prototype +8 through 7AF398/7AF39C
// (360-byte races). 5378EC reads race +108; 52DE0D serializes field 40.
// Borrow only during authoritative publication; retain the value, never pointers.
template<class Read>
bool read_unit_blood(const void* image, const void* unit, std::uint32_t& blood,
                     bool& undead_prototype, Read read) noexcept {
    static_assert(sizeof(void*) == 4, "supported game ABI is x86");
    blood = 0; undead_prototype = false;
    const auto get = [&](const void* base, std::size_t offset, auto& value) {
        return read(base, offset, &value, sizeof(value));
    };
    const auto row = [&](std::size_t rva, std::uint32_t index, std::size_t stride, const void*& out) {
        std::uintptr_t first = 0, last = 0;
        if (!get(image, rva, first) || !get(image, rva + 4, last) || !first || last < first ||
            (last - first) % stride || index >= (last - first) / stride) return false;
        out = reinterpret_cast<const void*>(first + index * stride);
        return true;
    };
    std::uint32_t subtype = 0, race = 0;
    const void* prototype = nullptr;
    const void* model = nullptr;
    std::uint32_t value = 0;
    if (!get(unit, 0x18, subtype) || !row(0x3af358, subtype, 288, prototype) ||
        !get(prototype, 8, race) || !row(0x3af398, race, 360, model) || !get(model, 0x108, value)) return false;
    // Base UndeadF13/15/17 use the same red-blood human race/model as living
    // humans. There is no database undead flag. Prototype field 0 (52D749)
    // supplies the distinguishing code; model-family exclusion still handles
    // the remaining undead, including Dejavu's red-blood zombie soldiers.
    const char* code = nullptr;
    if (!get(prototype, 0, code) || !code) return false;
    bool named_undead = true;
    constexpr char prefix[] = "Undead";
    for (std::size_t i = 0; i < sizeof(prefix) - 1; ++i) {
        char c = 0;
        if (!get(code, i, c)) return false;
        if (c != prefix[i]) { named_undead = false; break; }
    }
    blood = value; undead_prototype = named_undead;
    return true;
}

} // namespace owned::critters
