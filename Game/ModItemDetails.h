#pragma once

// Installed addon39454a3 FormTextInfo presentation. No combat state changes.
namespace ModItemDetails {
inline float Dps(float minimum, float random, unsigned actions, float criticalChance, float criticalDamage) {
    const float seconds = actions / 15.0f;
    const float critical = criticalDamage * 0.01f + 1;
    const float chance = criticalChance * 0.01f;
    const float damage = minimum + random * 0.5f;
    return damage * (critical * chance + 1 - chance) / seconds;
}
inline float EnergyRegen(float capacity, int flow, float bonus) {
    float rate = 0.001f;
    if (flow >= 1) rate *= 1 + bonus;
    return capacity * rate * 15.0f;
}
inline CString DpsText(float dps) {
    CString text;
    if (dps < 6) text.Format("%.2f", dps);
    else if (dps < 10) text.Format("%.1f", dps);
    else text.Format("%.0f", dps);
    return text;
}
}
