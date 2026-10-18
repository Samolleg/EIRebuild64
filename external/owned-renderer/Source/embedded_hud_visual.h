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
using EmbeddedHudVisualSink = bool (*)(void*, const NativeGeometryView&);
bool hud_embedded_submit_visual(const HudPublishedVisual& visual,
    const EmbeddedHudVisualInput& input, void* context, EmbeddedHudVisualSink sink) noexcept;
}
#endif
