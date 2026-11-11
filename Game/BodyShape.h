#pragma once
#include <array>
#include <cmath>

// Capture CUnit's five visual inputs. The SKN file owns their shape resolver.
namespace BodyShape {
struct Input {double strength=25,dexterity=25;std::array<double,3> db{.3,.3,.85};};
struct State {
    Input input{};
    bool valid=false;
    void Update(const Input& nextInput) {
        input=nextInput;
        valid=std::isfinite(input.strength) && std::isfinite(input.dexterity);
        for(double value:input.db)valid=valid && std::isfinite(value);
    }
};
}