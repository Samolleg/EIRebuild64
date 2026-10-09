#pragma once
#include "RPGSystem.h"
#include "AssortmentGenerator.h"
#include <array>
#include <vector>
#include <algorithm>

// Native 39454a3 CShop::SetAssortmentParameters. No world or RNG state ownership.
namespace ModAssortment {
struct Bounds {std::array<float,RPG_NUM_MAGIC_SUBTYPES> minimum,maximum;};
template<class Draw>
Bounds Prepare(const std::vector<const SRPGUnitStats*>& party,int shopLimit,Draw&& draw){
 Bounds result;const float limit=float(shopLimit);
 for(int subtype=0;subtype<RPG_NUM_MAGIC_SUBTYPES;++subtype){
  float& minimum=result.minimum[subtype];float& maximum=result.maximum[subtype];minimum=1e20f;maximum=-1e20f;
  for(const auto* stats:party){
   const float skill=stats->GetMagicSkill(ERPGMagicSubtypes(subtype));
   minimum=(std::max)((std::min)(minimum,skill),1.0f);
   maximum=(std::max)((std::max)(maximum,skill),limit);
   // Native Random(int): truncate the float bound; ONE CRT draw per person/subtype.
   maximum=maximum*.4f+draw(int(maximum*.6f));
  }
 }
 return result;
}
template<class Draw>
Bounds Apply(const std::vector<const SRPGUnitStats*>& party,int shopLimit,Draw&& draw,
             CAssortmentGenerator& items,CAssortmentGenerator& spells){
 auto result=Prepare(party,shopLimit,draw);
 items.SetParameters(result.minimum.data(),result.maximum.data());
 spells.SetParameters(result.minimum.data(),result.maximum.data());return result;
}
}
