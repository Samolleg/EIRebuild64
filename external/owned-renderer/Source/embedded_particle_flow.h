#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace owned {
// Dejavu 421604cc78b4 particle_flow.h. Values only: the host owns the
// source, simulation clock, pause, interpolation and normal particle draw.
struct ParticleFlowInput {
    std::array<float, 3> origin{};
    std::array<float, 2> direction{};
    float length = 0, width = 0;
};
struct ParticleFlowState {
    std::array<float, 3> position{};
    float size = 0, lateral = 0, angle = 0, distance = 0, speed = 0;
    int life = 0, phase = 0;
    std::uint32_t color = 0;
};
inline float particle_flow_random(std::uint32_t& state) noexcept {
    state = 1664525u * state + 1013904223u;
    return (state >> 8) * (1.f / 16777216.f);
}
inline void particle_flow_place(const ParticleFlowInput& input, ParticleFlowState& p) noexcept {
    const float lateral = p.lateral + std::sin(p.distance * 1.8f + p.angle) * .10f;
    p.position = {input.origin[0] + input.direction[0] * p.distance - input.direction[1] * lateral,
                  input.origin[1] + input.direction[1] * p.distance + input.direction[0] * lateral,
                  input.origin[2] + std::sin(p.distance * 2 + p.angle) * .10f};
    const float fade_in = (std::min)(1.f, p.distance / .7f);
    const float fade_out = (std::min)(1.f, (input.length - p.distance) / 1.4f);
    const auto alpha = static_cast<std::uint32_t>((std::max)(0.f, fade_in * fade_out) * 150);
    p.color = (alpha << 24) | 0x00bce9ff;
}
inline void particle_flow_create(const ParticleFlowInput& input, std::uint32_t& random,
                                 ParticleFlowState& p) noexcept {
    p = {};
    p.size = .45f + particle_flow_random(random) * .25f;
    p.lateral = (particle_flow_random(random) * 2 - 1) *
        (std::max)(0.f, input.width / 2 - p.size / 2 - .1f);
    p.angle = particle_flow_random(random) * 6.2831853f;
    p.speed = .65f + particle_flow_random(random) * .2f;
    p.life = 64;
    p.phase = static_cast<int>(particle_flow_random(random) * 15);
    particle_flow_place(input, p);
}
inline bool particle_flow_segment(const ParticleFlowInput& input, ParticleFlowState& p) noexcept {
    if (--p.life <= 0) return false;
    p.distance += p.speed;
    if (p.distance >= input.length) return false;
    p.phase = (p.phase + 1) % 15;
    particle_flow_place(input, p);
    return true;
}
} // namespace owned
