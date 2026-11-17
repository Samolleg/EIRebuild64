#pragma once
#ifdef EID3D11_EMBEDDED
#include "game_particles.h"
#include <array>
#include <cmath>
#include <cstdint>

namespace owned {
// Synchronous, pointer-bounded host values. No game object or retail address
// is retained. GPU expansion/layout use the same producers as the proxy.
struct EmbeddedEffectProjection {
    std::array<float, 16> object_to_clip{};
    float clip_half_width = 0, clip_half_height = 0;
};
struct EmbeddedRibbonInput {
    EmbeddedEffectProjection projection{};
    const GameRibbonPoint* points = nullptr;
    std::uint32_t count = 0;
    float width = 0, texture_length = 0, v_start = 0, v_width = 0;
    int clip_flags = 0;
    bool interface_pass = false;
};
struct EmbeddedPrintInput {
    EmbeddedEffectProjection projection{};
    // Original base-terrain control triangles, with slope normal and atlas cell.
    const GameParticleGpu* terrain_vertices = nullptr;
    // One SXYZW4 group: xxxx yyyy zzzz wwww; UVs in vertex order.
    const std::array<float, 16>* groups = nullptr;
    std::uint32_t group_count = 0;
    const std::array<float, 2>* maps = nullptr;
    std::uint32_t map_count = 0;
    const std::uint16_t* indices = nullptr;
    std::uint32_t index_count = 0, color = 0;
};
struct EmbeddedCombatTextUnit {
    bool present = false, visible = false, hidden = false, unit_visible = false;
    bool owned_unit = false, has_figure = false, figure_visible = false;
    std::array<std::int32_t, 4> figure_rect{}, unit_rect{};
};
struct EmbeddedCombatTextEvent {
    std::int32_t unit_id = 0, amount = 0;
    std::uint32_t flags = 0, time_ms = 0;
    EmbeddedCombatTextUnit unit{};
};
struct EmbeddedCombatTextInput {
    const EmbeddedCombatTextEvent* events = nullptr;
    std::uint32_t count = 0;
    double game_time_segments = 0;
    float screen_width = 0, screen_height = 0;
    bool addon_layout = false, enabled = true;
};
// Same clock conversion as combat_text_clock_ms in the proxy bridge.
inline std::uint32_t embedded_combat_text_clock_ms(double segments) noexcept {
    if (!std::isfinite(segments) || segments < 0) return 0;
    return static_cast<std::uint32_t>(static_cast<std::uint64_t>(segments * .055 * 1000.0));
}
inline bool embedded_combat_text_expired(std::uint32_t now, std::uint32_t time) noexcept {
    const auto age = static_cast<std::int32_t>(now - time);
    return age > 3000 || age < -6000;
}
} // namespace owned
#endif
