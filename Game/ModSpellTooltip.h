#pragma once
#include "RPGSystem.h"
#include "SpellSystem.h"
#include "PerkDatabase.h"
#include <algorithm>
#include <cmath>
#include <climits>
#include <stdexcept>

// Active addon CI3DObjectSpells::SetUnit (source 39454a3), UI calculations only.
namespace ModSpellTooltip {
struct Values { float mana, effect, castSeconds; };
inline Values Calculate(const SRPGUserUnitStats &stats, const CSpell &spell,
                        float additionalEffect, float manaIntelligence, int difficulty)
{
    const int subtype = spell.GetMagicSubtype();
    float baseSkill = 0;
    if (subtype >= RPGMST_FIRE && subtype <= RPGMST_ACID)
        baseSkill = stats.cAutoSkills[RPGASKL_ELEMENTAL];
    else if (subtype >= RPGMST_ILLUSION && subtype <= RPGMST_DIVINATION)
        baseSkill = stats.cAutoSkills[RPGASKL_SENSE];
    else if (subtype >= RPGMST_ENCHANTMENTS && subtype <= RPGMST_DOMINATION)
        baseSkill = stats.cAutoSkills[RPGASKL_ASTRAL];
    baseSkill += stats.GetPerkModifier(RPGPRK_FIRE + subtype);
    const int skill = int(baseSkill + int(stats.cIntelligence) - 25);
    const int id = spell.GetSubtypeID() >> 16;
    const bool attacking = id <= SPELL_ID_POISONFOG || id == SPELL_ID_VAMP;
    const float skillMod = 0.8f + (skill * 30.0f / 120.0f) / 100.0f;
    const bool wise = stats.cPerks[31] == 1;
    const float intelligenceMod = float((stats.cIntelligence * 0.01 - 0.25) * 2 * wise);
    const float attributeMod = (std::max)(0.0f, attacking ? 1 + intelligenceMod : 1.0f);
    const float effectDifficulty = attacking ? 1 + difficulty * 0.25f : 1.0f;

    float action = stats.cAction;
    if (std::abs(action) > 0.00001f) action = 15.0f / action;
    float caster = 1;
    for (int i = 0; i < RPG_NUM_PERKS; ++i)
        if (const SPDBPerk *perk = stats.GetPerk(i))
            if (perk->nPerkExclusive == 21) caster = float(1 - perk->dwModifier * 0.01);
    float head = 1;
    if (stats.hls[0].state != RPGALS_NOTUSED && !stats.hls[0].wMaxHP)
        throw std::runtime_error("Invalid spell tooltip head HP");
    if (stats.hls[0].state != RPGALS_NOTUSED)
        head = 0.6f + 0.4f * (stats.hls[0].wHP / stats.hls[0].wMaxHP);
    const float ticks = (spell.GetActionPoints() * action / head) * caster *
                        (difficulty > 0 ? 0.8f : 1.0f);
    const float mana = spell.GetManaCost() * (25.0f / manaIntelligence);
    const float effect = (spell.GetPower() + additionalEffect) * skillMod * attributeMod * effectDifficulty;
    if (!std::isfinite(mana) || !std::isfinite(effect) || !std::isfinite(ticks) ||
        double(ticks) < INT_MIN || double(ticks) > INT_MAX)
        throw std::runtime_error("Invalid spell tooltip inputs");
    return {mana, effect, int(ticks) / 15.0f}; // Source truncates segments before conversion.
}
}
