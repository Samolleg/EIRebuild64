#pragma once

#include <array>
#include <cstdint>

namespace owned {

// Renderer highlight policy for one frame sample (VisualTickValues::highlight).
// Zero keeps the native emissive rules exactly; each bit is a renderer decision.
enum : std::uint8_t {
    kHighlightSelected = 1,         // selected units show the native white highlight
    kHighlightStripPreselect = 2,   // a preselected (briefing) unit drops its white push
    kHighlightHover = 4,            // the game's current cursor target gets the hover lift
};

// Hover lifts emissive this far toward white: brighter but still lit and
// shadowed, so it reads as softer than the flat native selection white.
constexpr float kHoverEmissiveLift = 0.35f;

// Object flags (supported game.exe, CObject3D): OBJ_SELECTED and OBJ_PRESELECTED.
constexpr std::uint32_t kSelectedObjectFlag = 0x10u;
constexpr std::uint32_t kPreselectedObjectFlag = 0x100u;

struct HighlightMaterial {
    std::array<float, 4> emissive{};
    std::array<float, 4> previous{};   // saved below the latest native Highlight(TRUE)
    std::array<float, 4> oldest{};     // bottom of the native highlight stack
    bool has_previous = false;
    bool has_oldest = false;
};

// Emissive for one material. Every native Highlight(TRUE) pushes the prior
// emissive and writes RGB 1.0 (game.exe 51C9F0). Selection pushes once through
// Preselect unless the object is already preselected; timed effects, briefing
// preselection and quest/light-on Preselect push independently.
inline std::array<float, 4> visual_highlight_emissive(std::uint32_t object_flags,
    bool unit, bool timed_highlight, bool quest_light, std::uint8_t policy,
    const HighlightMaterial& material) noexcept {
    const bool selected = unit && (object_flags & kSelectedObjectFlag) != 0;
    const bool preselected = unit && (object_flags & kPreselectedObjectFlag) != 0;
    const bool white = material.emissive[0] == 1.0f && material.emissive[1] == 1.0f &&
        material.emissive[2] == 1.0f && material.has_previous;
    std::array<float, 4> result = material.emissive;
    if (white) {
        if (quest_light && unit) {
            // The quest light's own Preselect(TRUE) can precede Select(TRUE).
            // Keep explicit timed and drag highlights; otherwise the oldest
            // saved material is the authored one beneath those white pushes.
            if (!timed_highlight && !preselected)
                result = selected && material.has_oldest ? material.oldest : material.previous;
        } else if (selected) {
            // More than one native white push can leave the previous value
            // white too. With selection as the only active highlight, use
            // the authored value below those pushes; keep explicit effects.
            result = !timed_highlight && !preselected && material.has_oldest
                ? material.oldest : material.previous;
        }
        if ((policy & kHighlightStripPreselect) && preselected && !selected && !timed_highlight)
            result = quest_light && material.has_oldest ? material.oldest : material.previous;
    }
    if ((policy & kHighlightSelected) && selected)
        result = {1.0f, 1.0f, 1.0f, material.emissive[3]};
    if (policy & kHighlightHover)
        for (unsigned channel = 0; channel < 3; ++channel)
            result[channel] += (1.0f - result[channel]) * kHoverEmissiveLift;
    return result;
}

// Cursor modes (supported game.exe CI3DCursor +0x18, A3Cursor ECursorType).
enum : std::int32_t {
    kCursorDefault = 0, kCursorAttack = 1, kCursorSpell = 2, kCursorUse = 4,
    kCursorTalk = 14, kCursorAttackHead = 16, kCursorAttackRightLeg = 21,
    kCursorSteal = 24,
};

enum class HoverTarget : std::uint8_t { none, unit, picked_object };

// Which target the mission's cursor decision (CI3DScreenMission::CheckAction,
// game.exe 639EF0) named. A visible unit is its UNIT_VISIBLE-filtered argument;
// without one, every CT_USE branch picked its object with PickFirstMapObject.
inline HoverTarget mission_hover_target(std::int32_t cursor_mode, bool visible_unit) noexcept {
    if (cursor_mode == kCursorUse)
        return visible_unit ? HoverTarget::unit : HoverTarget::picked_object;
    const bool unit_mode = cursor_mode == kCursorAttack || cursor_mode == kCursorSpell ||
        cursor_mode == kCursorSteal ||
        (cursor_mode >= kCursorAttackHead && cursor_mode <= kCursorAttackRightLeg);
    return unit_mode && visible_unit ? HoverTarget::unit : HoverTarget::none;
}

// A living unit is a hovered character; a corpse, lever, chest or trap is an
// interactable object.
inline bool hover_highlight_enabled(bool unit, bool dead, bool characters, bool objects) noexcept {
    return unit && !dead ? characters : objects;
}

} // namespace owned
