#pragma once

#include <cstring>

namespace owned {

// Unit spell/eye attachment roles. BASE on scenery can be visible geometry.
// Keep these nodes in the skeleton for effects; exclude only their FIG draws.
inline bool unit_service_node(const char* name) noexcept {
    if (!name) return false;
    if (_strnicmp(name, "l.", 2) == 0) name += 2;
    if (_stricmp(name, "eyepoint1") == 0 || _stricmp(name, "eyepoint2") == 0 ||
        _stricmp(name, "baserh3") == 0 || _stricmp(name, "baser_hand3") == 0)
        return true;
    static const char* const cast_points[] = {
        "basesword", "baseaxe", "basedagger", "basepike", "baseclub", "basearrow",
        "basedsword", "basedaxe", "basedpike", "basedclub", "basestaff", "basedstaff"
    };
    for (const char* prefix : cast_points) {
        const auto length = std::strlen(prefix);
        if (_strnicmp(name, prefix, length) != 0) continue;
        const char* number = name + length;
        if (!*number) continue;
        while (*number >= '0' && *number <= '9') ++number;
        if (!*number) return true;
    }
    return false;
}

} // namespace owned
