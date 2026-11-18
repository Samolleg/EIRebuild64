#pragma once
#ifdef EID3D11_EMBEDDED
#include "hud_visual_publication.h"
#include "engine_bridge.h"

namespace owned {
HudVisualPublication& hud_embedded_publication() noexcept;
// Completed carrier values, borrowed only for the synchronous submission.
// Assets, atlas remapping and mesh construction stay in the proxy producer.
struct EmbeddedHudVisualInput {
    std::array<float,16> object_to_clip{};
    float u_shift = 0, v_shift = 0;
    std::uint32_t diffuse = 0xffffffffu, specular = 0xff000000u;
    bool default_colors = true, color_sum = false;
};
// Completed current model groups, pose/morph, unit/slot and texture cell.
// Source ownership stays with the host for this synchronous call only.
struct EmbeddedHudPortraitInput {
    NativeGeometryView model{};
    EmbeddedHudVisualInput state{};
    const char* texture_name = nullptr;
    const char* base_texture_name = nullptr;
    bool whole_atlas = false;
    HudIconPlacement placement{};
    std::int32_t unit_id = 0;
    std::uint32_t slot = 0;
};
using EmbeddedHudVisualSink = bool (*)(void*, const NativeGeometryView&);
bool hud_embedded_submit_portrait(const EmbeddedHudPortraitInput& input,
    void* context, EmbeddedHudVisualSink sink) noexcept;
bool hud_embedded_submit_visual(const HudPublishedVisual& visual,
    const EmbeddedHudVisualInput& input, void* context, EmbeddedHudVisualSink sink) noexcept;
}
#endif
