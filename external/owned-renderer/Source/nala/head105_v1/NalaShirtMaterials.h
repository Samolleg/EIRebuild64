#pragma once
namespace NalaShirtMaterials {
// Exact client subtype: database prototype ordinal in high16, material in low16.
// Only this reviewed sleeveless blueprint is supported; other cuts stay separate.
struct Material {unsigned subtype,ordinal,resource;const char*key;};
inline constexpr Material Materials[]={
 {0x0001001cu,28,212,"wool"},{0x0001001du,29,213,"cotton"},
 {0x0001001eu,30,214,"silk red"},{0x0001001fu,31,215,"silk blue"},
 {0x0001002cu,44,216,"astral thread"}};
inline const Material*ForSubtype(unsigned value){for(const auto&m:Materials)if(m.subtype==value)return &m;return nullptr;}
inline const Material*ForOrdinal(unsigned value){for(const auto&m:Materials)if(m.ordinal==value)return &m;return nullptr;}
}
