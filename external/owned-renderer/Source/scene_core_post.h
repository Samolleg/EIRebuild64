#pragma once
// Core mode (S3): Renderer::core_post_command, the x86 end of a typed scene
// command (included at the end of renderer.h). A command with a wire id is
// encoded with its frame envelope (when it declares frame_values) and sent;
// one without is refused (logged once by site, its visual omitted).
#if !defined(EID3D11_EMBEDDED) && !defined(EID3D11_SHADER_TOOL)
#include "scene_core_types.h"

namespace owned {

template <class C> void Renderer::core_post_command(const C& command, const char* site) {
    constexpr scene_core::Command id = scene_core::wire_command<C>::id;
    if constexpr (id == scene_core::Command::none) {
        (void)command;
        core_refuse(site);
    } else {
        try {
            // The combat UI state goes ahead of the commands that read it.
            if constexpr (std::is_same_v<C, scene_core::WorldScene> || std::is_same_v<C, scene_ui::Blits> ||
                          std::is_same_v<C, scene_ui::FlushUi>)
                core_sync_combat();
            std::vector<std::uint8_t> bytes;
            bytes.push_back(0);
            if constexpr (C::frame_values) {
                scene_core::FrameEnvelope envelope;
                if (!bridge_capture_frame_values(envelope.values)) { core_refuse(site); return; }
                // The atlas snapshot goes ahead of the first post that sees it.
                if (envelope.values.terrain_atlases != core_atlases_sent_) {
                    scene_core::TerrainAtlases atlases;
                    atlases.snapshot = envelope.values.terrain_atlases;
                    std::vector<std::uint8_t> atlas_bytes{0};
                    scene_wire::encode_into(atlas_bytes, atlases);
                    core_send(static_cast<std::uint16_t>(scene_core::Command::terrain_atlases), atlas_bytes);
                    core_atlases_sent_ = envelope.values.terrain_atlases;
                }
                envelope.hud = widescreen::hud_blit;
                std::vector<std::uint8_t> encoded = scene_wire::encode(envelope);
                // The host keeps the last envelope it was sent: only a change travels.
                if (encoded != core_envelope_) {
                    bytes[0] = 1;
                    bytes.insert(bytes.end(), encoded.begin(), encoded.end());
                    core_envelope_ = std::move(encoded);
                }
            }
            scene_wire::encode_into(bytes, command);
            core_send(static_cast<std::uint16_t>(id), bytes);
        } catch (...) {
            core_refuse(site);
        }
    }
}

}  // namespace owned
#else
namespace owned {
template <class C> void Renderer::core_post_command(const C&, const char* site) { core_refuse(site); }
}
#endif
