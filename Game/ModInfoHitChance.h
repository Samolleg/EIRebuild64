#include "ModPhysicalHitChance.h"
// Included by the existing stats-window owner; no additional hooks.
#ifndef JMV_INFO_HIT_CHANCE_DIAGNOSTICS
#define JMV_INFO_HIT_CHANCE_DIAGNOSTICS 0
#endif

static bool LS_InfoPhysicalHitChance(CUnit* attacker, CUnit* defender,
    const SAttackParams& attack, const SAttackParams& defense,
    float distance, unsigned direction, float& percent)
{
    if (!attacker || !defender || attacker->IsDead() || defender->IsDead())
        return false;
    const float range = defender->GetAttackDistance(); // GetToHitNew uses defender!
    if (!_finite(range) || range <= 0.0f || !_finite(distance) ||
        !_finite(attack.fToHitRandom) || attack.fToHitRandom >= 2147483520.0f)
        return false;
    const int roundedSpread = attack.fToHitRandom > 0.0f ? MINT(attack.fToHitRandom) : 0;
    if (roundedSpread < 0)
        return false;
    const float penalty = distance * 2.0f / (RPG_CONST_Z * range * 2.0f);
    if (!_finite(penalty))
        return false;
    float avoidPower = 0.0f;
    bool stunned = false;
    const CSpellEffectArray& effects = defender->GetSpellEffects();
    for (int i = 0; i < effects.GetSize(); ++i) {
        if (effects[i].nSpellID == SPELL_ID_STUN)
            stunned = true;
        if (effects[i].nSpellID == SPELL_ID_SHRINK)
            avoidPower = (std::max)(avoidPower, effects[i].fpPower);
    }
    percent = static_cast<float>(PhysicalHitChance::Percent(attack.nAttack, defense.nDefense,
        attack.fToHitRandom, roundedSpread, penalty, avoidPower, stunned));

    return true;
}
