#pragma once
namespace NalaPantsMaterials {
// Blueprint 2 (simple long johns), with shared Cloth material ordinals.
struct Material {unsigned subtype,ordinal,resource;const char*key;};
inline constexpr Material Materials[]={
 {0x0002001cu,28,217,"wool"},{0x0002001du,29,218,"cotton"},
 {0x0002001eu,30,219,"silk red"},{0x0002001fu,31,220,"silk blue"},
 {0x0002002cu,44,221,"astral thread"}};
inline const Material*ForSubtype(unsigned value){for(const auto&m:Materials)if(m.subtype==value)return &m;return nullptr;}
}
