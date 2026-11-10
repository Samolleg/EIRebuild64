#pragma once
#include "BodyShapeResolver.h"

// Visual CPU data only: never use for FIG/BV/navigation/gameplay.
namespace BodyShape {
using Input = NalaMorphs::Input;
using Values = NalaMorphs::Values;
using Status = NalaMorphs::Status;
inline bool Resolve(const Input& input, Values& values, Status& status)
{
    return NalaMorphs::Resolve(input, values, status);
}
inline double Scale(const Values& values)
{
    return .9309906028352594 * values[EditorSchema::height];
}
struct State {
    Values controls{};
    Status status{};
    double scale = 0;
    bool valid = false;
    void Update(const Input& input)
    {
        Values next{};
        valid = BodyShape::Resolve(input, next, status);
        controls = valid ? next : Values{};
        scale = valid ? Scale(controls) : 0;
    }
};
}
