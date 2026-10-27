#pragma once
#include "Geometry.h"
#include <set>
#include <string>
#include <map>

namespace Navigation {
struct Authoring {
    std::set<int> dynamic, fixed;
    std::map<int,Door> doors;
    std::map<int,std::set<std::string>> walkable;
};
}
