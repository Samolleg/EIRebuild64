#pragma once
#include "model_lighting.h"

namespace OwnedRender {
using owned::ModelLightingFacts;
using owned::ModelPointLightFacts;
using owned::NativeWorldLighting;
using owned::RetainedWorldPointLight;
using owned::produce_model_lighting;

inline bool admit_model_preview_point_light(const ModelPointLightFacts& input,
    const std::array<float,3>& object) noexcept {
    if (!owned::admit_model_point_light(input)) return false;
    float distance_squared = 0;
    for (unsigned i = 0; i < 3; ++i) {
        if (!std::isfinite(object[i])) return false;
        const float delta = object[i] - input.value.center[i];
        distance_squared += delta * delta;
    }
    const float reach = input.value.radius + 10.f;
    return distance_squared <= reach * reach;
}

struct ModelPreviewLighting {
    NativeWorldLighting world{};
    std::vector<RetainedWorldPointLight> points;
};
// Source preview selection uses the shared renderer ambient/sun and point admission.
template<class Next>
bool produce_model_preview_lighting(const ModelLightingFacts& world,
    const std::array<float,3>& object, Next&& next, ModelPreviewLighting& output) {
    output.points.clear();
    if (!produce_model_lighting(world, output.world)) return false;
    ModelPointLightFacts light;
    while (next(light)) {
        if (admit_model_preview_point_light(light, object)) {
            if (!light.value.source_id) light.value.source_id = int(output.points.size()) + 1;
            output.points.push_back(light.value);
        }
        light = {};
    }
    return true;
}
} // namespace OwnedRender
