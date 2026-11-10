#pragma once
#include <array>
#include <cstdint>

// Wound display states; inactive-but-damaged hit locations stay critical.
namespace NalaWoundsStudy {
struct Location { std::uint8_t state; std::uint16_t hp, maximum; };
using Levels = std::array<std::uint8_t, 6>;
inline bool Classify(const std::array<Location, 6>& locations, Levels& out) {
 Levels result{};
 for (unsigned i = 0; i < result.size(); ++i) {
  const auto& v = locations[i];
  if (v.state == 0) continue;
  if (v.state == 1 || v.state == 2) { result[i] = 3; continue; }
  if (v.state != 3) return false;
  if (v.maximum == 0 || v.hp > v.maximum) return false;
  if (v.hp == v.maximum) result[i] = 0;
  else if (v.hp >= v.maximum * .66) result[i] = 1;
  else if (v.hp >= v.maximum * .33) result[i] = 2;
  else result[i] = 3;
 }
 out = result;
 return true;
}
}
