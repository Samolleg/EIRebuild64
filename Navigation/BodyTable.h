#pragma once
#include "Build.h"
#include <map>
#include <tuple>

namespace Navigation {
inline constexpr const char* BodyFilename="bodies.navbody";
struct BodySize { float radius=0; float height[3]{}; };
using BodyKey=std::tuple<std::string,float,float,float>;
// Each model has a measured 0/1/2 morph grid and exact database constitutions.
// Runtime interpolates these values and never opens model resources.
class BodyTable {
public:
    std::map<BodyKey,BodySize> samples;
    BodySize Lookup(const std::string& model,float strength,float grace,float tall) const;
};
Bytes StoreBodies(const BodyTable& table);
BodyTable LoadBodies(const Bytes& bytes);
}
