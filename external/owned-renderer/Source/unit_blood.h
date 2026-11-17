#pragma once

#include <cstddef>
#include <cstdint>

namespace owned::critters {

struct UnitBloodIdentity {
    std::uint32_t prototypes = 0x003af358;
    std::uint32_t races = 0x003af398;
    bool classify_race_name = false;
};

// Supported game.exe: 530970 resolves subtype +18 through 7AF358/7AF35C
// (288-byte prototypes), then prototype +8 through 7AF398/7AF39C
// (360-byte races). 5378EC reads race +108; 52DE0D serializes field 40.
// Borrow only during authoritative publication; retain the value, never pointers.
template<class Read>
bool read_unit_blood(const void* image, const void* unit, std::uint32_t& blood,
                     bool& undead_prototype, Read read,
                     const UnitBloodIdentity& identity = {}) noexcept {
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
    if (!get(unit, 0x18, subtype) || !row(identity.prototypes, subtype, 288, prototype) ||
        !get(prototype, 8, race) || !row(identity.races, race, 360, model) || !get(model, 0x108, value)) return false;
    // Base UndeadF13/15/17 use the same red-blood human race/model as living
    // humans. There is no database undead flag. Prototype field 0 (52D749)
    // supplies the distinguishing code; model-family exclusion still handles
    // the remaining undead, including Dejavu's red-blood zombie soldiers.
    // LiA's zone/quest-prefixed prototype names do not carry the base prefix.
    // Its authoritative race name distinguishes undead sharing a human model.
    const void* names[] = {prototype, identity.classify_race_name ? model : nullptr};
    bool named_undead = false;
    constexpr char prefix[] = "Undead";
    for (const void* record : names) {
        if (!record) continue;
        const char* code = nullptr;
        if (!get(record, 0, code) || !code) return false;
        bool matches = true;
        for (std::size_t i = 0; i < sizeof(prefix) - 1; ++i) {
            char c = 0;
            if (!get(code, i, c)) return false;
            if (c != prefix[i]) { matches = false; break; }
        }
        named_undead |= matches;
    }
    blood = value; undead_prototype = named_undead;
    return true;
}

// The unit database skin of a unit the game gave no texture name (script
// AddUnitToServer: CreatePersonage fills no texture for non-humans). Same
// verified prototype/race rows as above; prototype +0Ch is the skin index,
// race +E8h the model mask (CString) and +ECh the primary texture list
// (vector<CString> begin/end). The mask must equal the unit's own figure
// mask, which confirms the record layout on every read.
template<class Read>
bool read_unit_race_skin(const void* image, const void* unit, const char* figure_mask,
                         char (&texture)[64], Read read,
                         const UnitBloodIdentity& identity = {}) noexcept {
    static_assert(sizeof(void*) == 4, "supported game ABI is x86");
    texture[0] = 0;
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
    const auto text = [&](const char* source, char* out, std::size_t size) {
        if (!source) return false;
        for (std::size_t i = 0; i < size; ++i) {
            if (!get(source, i, out[i])) return false;
            if (!out[i]) return true;
        }
        return false;
    };
    std::uint32_t subtype = 0, race = 0, skin = 0;
    const void* prototype = nullptr;
    const void* model = nullptr;
    const char* mask = nullptr;
    std::uintptr_t first = 0, last = 0;
    char mask_text[64]{};
    if (!figure_mask || !get(unit, 0x18, subtype) || !row(identity.prototypes, subtype, 288, prototype) ||
        !get(prototype, 8, race) || !get(prototype, 0xc, skin) ||
        !row(identity.races, race, 360, model) || !get(model, 0xe8, mask) ||
        !text(mask, mask_text, sizeof(mask_text))) return false;
    for (std::size_t i = 0;; ++i) {
        const auto fold = [](char c) { return c >= 'A' && c <= 'Z' ? char(c + 32) : c; };
        const char a = fold(mask_text[i]), b = fold(figure_mask[i]);
        if (a != b) return false;
        if (!a) break;
    }
    const char* name = nullptr;
    if (!get(model, 0xec, first) || !get(model, 0xf0, last) || !first || last < first ||
        (last - first) % 4 || skin >= (last - first) / 4 ||
        !get(reinterpret_cast<const void*>(first), skin * 4, name) ||
        !text(name, texture, sizeof(texture)) || !texture[0]) {
        texture[0] = 0;
        return false;
    }
    return true;
}

} // namespace owned::critters
