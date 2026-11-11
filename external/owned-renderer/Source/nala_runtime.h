#pragma once
#include <array>
#include <memory>
#include <vector>
#include "skn_runtime.h"
namespace owned { struct RetainedVisualSample; }
namespace owned::nala {
struct Input {
 bool selected=false, valid=false;
 std::array<double,5> shape_inputs{}; // build, fullness, height, strength, dexterity
 std::array<unsigned,4> clothing{}; // native slots 3,4,6,5
 std::array<unsigned char,6> wounds{};
 bool operator==(const Input& b)const noexcept {
  return selected==b.selected&&valid==b.valid&&shape_inputs==b.shape_inputs&&clothing==b.clothing&&wounds==b.wounds;
 }
};
}
