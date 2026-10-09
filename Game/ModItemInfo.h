#pragma once
#include "RPGSystemConsts.h"
#include "Tools.h"
#include <cmath>

// Installed addon39454a3 inventory presentation rules; never alter item/spell stats.
namespace ModItemInfo {
inline CString PrototypeEffect(float base, float additional) {
    CString text;
    if (additional > 0) text.Format("%.1f+%.1f", base, additional);
    else text.Format("%.1f", base);
    return text;
}
inline CString PotionEffect(float base, float additional, bool runeAllowed, LPCTSTR fixed) {
    CString text;
    if (runeAllowed) text.Format("%.1f", base + additional);
    else text = fixed;
    return text;
}
inline double PrototypeCast(DWORD actions) { return actions / 15.0; }
inline float PrototypeRadius(float area) { return float(std::sqrt(area / FP_PI)); }
inline const char *WeaponTypeKey(int type) {
    if (type == 9) return "infoitem_97";
    if (type == RPGWST_HANDS2 || type == RPGWST_HANDS4 || type == RPGWST_HANDS5 ||
        type == RPGWST_HANDS6 || type == RPGWST_HANDS7 || type == RPGWST_BOW || type == RPGWST_CROSSBOW)
        return "infoitem_96";
    return nullptr;
}
inline const char *WeaponWeightKey(int type, int weight) {
    if (type == 9) return weight == 1 ? "infoitem_70" : weight == 2 ? "infoitem_71" : weight == 3 ? "infoitem_72" : nullptr;
    return weight == 1 ? "infoitem_67" : weight == 2 ? "infoitem_68" : "infoitem_69";
}
inline const char *ArmorWeightKey(const CString &shape) {
    if (shape.Left(2) == "vl") return "infoitem_74";
    if (shape.Left(1) == "l") return "infoitem_75";
    if (shape.Left(1) == "m") return "infoitem_76";
    if (shape.Left(1) == "h") return "infoitem_77";
    if (shape.Left(2) == "vh") return "infoitem_78";
    return nullptr;
}
inline void Absorptions(const float *values, const CString *labels, CStringArray &rows, bool round = false) {
    rows.RemoveAll();
    for (int i = 0; i < RPG_NUM_ATTACK_TYPES - 1; ++i) {
        CString value;
        if (round) value.Format("%d", int(values[i]));
        else value.Format("%.1f", values[i]);
        rows.Add(labels[i] + value + ((i < RPGAT_TERMICAL ? i < 2 : i < 5) ? " " : ""));
    }
}
}
