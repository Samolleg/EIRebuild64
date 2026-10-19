#pragma once
#include "retained_static_scene.h"
#include <cmath>

namespace owned {

struct ModelLightingFacts {
    std::uint32_t ambient = 0;
    bool sunlight = false;
    std::array<float,3> diffuse{}, direction{};
};
inline bool produce_model_lighting(const ModelLightingFacts& input,
    NativeWorldLighting& output) noexcept {
    output = {};
    output.ambient = {float((input.ambient >> 16) & 255) / 255.f,
        float((input.ambient >> 8) & 255) / 255.f, float(input.ambient & 255) / 255.f, 1.f};
    output.sunlight_diffuse = {};
    if (!input.sunlight) return true;
    float length_squared = 0;
    for (unsigned i = 0; i < 3; ++i) {
        if (!std::isfinite(input.diffuse[i]) || !std::isfinite(input.direction[i])) return false;
        length_squared += input.direction[i] * input.direction[i];
    }
    if (!std::isfinite(length_squared) || length_squared <= 1.e-12f) return false;
    output.sunlight_diffuse = {input.diffuse[0], input.diffuse[1], input.diffuse[2], 1.f};
    output.sunlight_direction = {-input.direction[0], -input.direction[1], -input.direction[2], 0.f};
    return true;
}
struct ModelPointLightFacts {
    RetainedWorldPointLight value{};
    bool point = true, inactive = false, sunlight = false;
};
inline bool admit_model_point_light(const ModelPointLightFacts& input) noexcept {
    const auto& light = input.value;
    if (!input.point || input.inactive || input.sunlight ||
        !std::isfinite(light.radius) || !(light.radius > 0.f)) return false;
    for (unsigned i = 0; i < 3; ++i) {
        if (!std::isfinite(light.center[i]) || !std::isfinite(light.color[i])) return false;
    }
    return true;
}
} // namespace owned
