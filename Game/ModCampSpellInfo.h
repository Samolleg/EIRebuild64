#pragma once
#include "ModSpellTooltip.h"
#include <vector>
#include <utility>

// Active addon39454a3 FormSpellInfo. Camp UI deliberately differs from mission UI.
namespace ModCampSpellInfo {
struct Values { float manaBonus, basePower, powerBonus, castBonus; };
inline Values Calculate(const SRPGUserUnitStats &stats, const CSpell &spell,
                        float additional, int difficulty, bool ctrl)
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
    const float skill = baseSkill + int(stats.cIntelligence) - 25;
    const float skillMod = 0.8f + (skill * 30.0f / 120.0f) / 100.0f;
    const int id = spell.GetSubtypeID() >> 16;
    const bool attacking = id <= SPELL_ID_POISONFOG || id == SPELL_ID_VAMP;
    const float intelligenceMod = float((stats.cIntelligence * 0.01 - 0.25) * 2 * stats.cPerks[31]);
    const float attribute = (std::max)(0.0f, 1 + (attacking ? intelligenceMod : 0));
    const float effectDifficulty = attacking ? 1 + difficulty * 0.25f : 1.0f;
    const float power = spell.GetPower() + additional;
    const float bonus = attacking ? power * skillMod * attribute * effectDifficulty - power : 0;
    float action = stats.cAction;
    if (std::abs(action) > 0.00001f) action = 15.0f / action;
    float caster = 1;
    for (int i = 0; i < RPG_NUM_PERKS; ++i)
        if (const SPDBPerk *perk = stats.GetPerk(i))
            if (perk->nPerkExclusive == 21) caster = float(1 - perk->dwModifier * 0.01);
    const float points = float(spell.GetActionPoints());
    const float castBonus = (points * action * caster * (difficulty > 0 ? 0.8f : 1.0f) - points) / 15.0f;
    const float manaBonus = ctrl ? 0 : spell.GetManaCost() * (25.0f / stats.cIntelligence) - spell.GetManaCost();
    if (!std::isfinite(manaBonus) || !std::isfinite(power) || !std::isfinite(bonus) || !std::isfinite(castBonus))
        throw std::runtime_error("Invalid camp spell info inputs");
    return {manaBonus, power, bonus, castBonus};
}
inline CString Mana(const CSpell &spell, const Values &v)
{
    CString text, rate;
    if (spell.GetDuration() / 15.f >= 2)
        rate.Format("(%.1f)", spell.GetManaCost() / (spell.GetDuration() / 15.f));
    if (v.manaBonus < -1) text.Format("%d%d", int(spell.GetManaCost()), int(v.manaBonus));
    else if (v.manaBonus > -1 && v.manaBonus < 1) text.Format("%d", int(spell.GetManaCost()));
    else text.Format("%d+%d", int(spell.GetManaCost()), int(v.manaBonus));
    return text + rate;
}
inline CString Effect(const Values &v)
{
    CString text;
    if (v.powerBonus > 0) text.Format(v.basePower > 10 ? "%.0f+%.0f" : "%.1f+%.1f", v.basePower, v.powerBonus);
    else if (v.powerBonus < 0) text.Format(v.basePower > 10 ? "%.0f%.0f" : "%.1f%.1f", v.basePower, v.powerBonus);
    else text.Format(v.basePower > 10 ? "%.0f" : "%.1f", v.basePower);
    return text;
}
inline CString Cast(const CSpell &spell, const Values &v)
{
    CString text;
    if (v.castBonus != 0) text.Format("%.1f%.1f", spell.GetActionPoints() / 15.f, v.castBonus);
    else text.Format("%.1f", spell.GetActionPoints() / 15.f);
    return text; // Retain the source's concatenation, including positive corrections.
}
struct Extra { const char *label; CString value; bool perSecond = false; };
inline std::vector<Extra> Extras(const CSpell &spell, const Values &v)
{
    std::vector<Extra> rows;
    const int id = spell.GetSubtypeID() >> 16;
    const float power = v.basePower + v.powerBonus;
    auto add = [&](const char *key, const CString &value, bool perSecond = false) { rows.push_back({key,value,perSecond}); };
    CString text;
    if (id == SPELL_ID_STRENGTHEN || id == SPELL_ID_WEAKEN) {
        const bool plus = id == SPELL_ID_STRENGTHEN;
        // Weaken's source uses double literals, strengthen uses float for damage.
        const float damage = plus ? power * 0.3f : float(power * 0.3);
        text.Format(plus ? "+%.1f%%" : "-%.1f%%", damage); add("infoitem_84", text);
        text.Format(plus ? "+%d%%" : "-%d%%", int(power * 0.5)); add("infoitem_85", text);
        text.Format(plus ? "+%d" : "-%d", int(power)); add("infoskill_1", text);
    }
    if (id == SPELL_ID_ENLARGE) { text.Format("+%.1f", power); add("infoitem_86", text); }
    if (id == SPELL_ID_SPEED || id == SPELL_ID_SLOW) {
        const bool plus = id == SPELL_ID_SPEED;
        text.Format(plus ? "+%.1f" : "-%.1f", power); add("infoitem_87", text);
        text.Format(plus ? "+%.1f%%" : "-%.1f%%", (power * 100) / 23); add("infounit_2", text);
    }
    if (id == SPELL_ID_REGENERATION) {
        text.Format("+%.1f", power / 30); add("infoskill_0", text, true);
        text.Format("(%.0f)", (power / 30) * (spell.GetDuration() / 15.f)); add("", text);
    }
    if (id == SPELL_ID_FIREWALL || id == SPELL_ID_WALL_LGHTNG || id == SPELL_ID_POISONFOG) {
        const float baseDuration = spell.GetPrototype()->dwDuration / 15.f;
        const float durationMod = (spell.GetDuration() / 15.f) / baseDuration;
        const float damage = (power / baseDuration) * durationMod;
        if (!std::isfinite(damage) || !std::isfinite(power * durationMod))
            throw std::runtime_error("Invalid camp spell duration");
        text.Format("%.1f", damage); add("infoitem_15", text, true);
        text.Format("(%.0f)", power * durationMod); add("", text);
    }
    return rows;
}
}
