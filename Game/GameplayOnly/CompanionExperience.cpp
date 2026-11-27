#include "StdAfx.h"
#include "../CompanionExperience.h"
#include "../Player.h"
#include "../UnitServer.h"
#include <cmath>

// Both entries use the existing server-owned save map. The original entry also
// accepts fractional XP from older saves. The remainder is in fifths of an XP:
// five one-point hero rewards then add up exactly, even for a veteran NPC.
void CreditCompanion(CPlayer &player, const BaseCompanion &companion, float heroShare,
                     CUnitServer *unit, SRPGUnitStats *fallback)
{
    auto &vars = player.party.vars;
    auto read = [&](const std::string &key) {
        const auto it = vars.find(key);
        return it == vars.end() ? 0.0f : it->second;
    };
    auto store = [&](const std::string &key, float oldValue, float value) {
        if (oldValue != value)
        {
            if (value == 0)
                vars.erase(key);
            else
                vars[key] = value;
        }
    };
    const float whole = read(companion.key);
    const float remainder = read(companion.remainderKey);
    double fifths = double(whole) * 5 + remainder + heroShare;
    auto *stats = unit ? unit->GetRPGStats() : fallback;
    if (stats && fifths > 0)
    {
        // Let exactly representable fractions through; batch smaller inexact
        // awards until a point is due, or settle the balance on arrival/hire.
        const float oldXP = stats->fpExperience, oldPenalty = stats->fExpPenalty;
        const double due = fifths / 5;
        const double target = double(oldXP) + oldPenalty + due;
        if (heroShare == 0 || due >= 1 || double(float(target)) == target)
        {
            fifths -= stats->GainExperience(float(due), false) * 5;
            if (unit && (oldXP != stats->fpExperience || oldPenalty != stats->fExpPenalty))
                unit->RecalcRPGStats();
        }
    }
    const float newWhole = float(std::trunc(fifths / 5));
    store(companion.key, whole, newWhole);
    store(companion.remainderKey, remainder, float(fifths - double(newWhole) * 5));
}
