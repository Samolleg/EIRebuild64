#pragma once
#include <algorithm>
namespace ModCampItemPrice {
// Native Perks internal quote, distinct from retail virtual UI display.
// Preserve both truncation steps; do not apply repair's minimum fee here.
inline int WeaponArmor(float price,float coefficient,float repairCoefficient,float hp,float maxHP){
 const float damage=1.f-((std::max)(0.f,hp)+0.00001f)/maxHP;
 const float repair=price*repairCoefficient*damage;
 return int(price*coefficient-int(repair));
}
}
