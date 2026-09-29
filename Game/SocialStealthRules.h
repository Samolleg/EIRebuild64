#pragma once
#include <array>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace SocialStealth {
const int ArmorSlots = 7;
struct Config {
    int minimum = 4;
    std::vector<std::string> armorNames;
};
struct Result {
    bool matches = false;
    int worn = 0;
    int foreignSlot = -1;
};
struct Profile {
    int minimum = 4;
    bool valid = false;
    std::string error;
    std::unordered_set<int> allowed;
    std::unordered_map<int, Result> members;
};
inline Result Evaluate(const Profile& profile, const std::array<int, ArmorSlots>& armor) {
    Result result;
    if (!profile.valid) return result;
    for (int slot = 0; slot < ArmorSlots; ++slot) {
        if (armor[slot] == -1) continue; // Empty slot; -2 is an invalid item.
        ++result.worn;
        if (!profile.allowed.count(armor[slot]) && result.foreignSlot == -1)
            result.foreignSlot = slot;
    }
    result.matches = result.foreignSlot == -1 && result.worn >= profile.minimum;
    return result;
}
inline const Result& Member(Profile& profile, int id,
    const std::array<int, ArmorSlots>& armor) {
    const auto found = profile.members.find(id);
    if (found != profile.members.end()) return found->second;
    return profile.members.emplace(id, Evaluate(profile, armor)).first->second;
}
inline void Compile(Profile& profile, const Config& config,
    const std::unordered_map<std::string, int>& armorIndex) {
    profile = Profile();
    profile.minimum = config.minimum;
    if (config.minimum < 0 || config.minimum > ArmorSlots) {
        profile.error = "invalid-minimum";
        return;
    }
    for (const auto& name : config.armorNames) {
        const auto found = armorIndex.find(name);
        if (found == armorIndex.end() || found->second < 0) {
            profile.error = "unknown-or-ambiguous-armor: " + name;
            return;
        }
        profile.allowed.insert(found->second);
    }
    profile.valid = true;
}
}
