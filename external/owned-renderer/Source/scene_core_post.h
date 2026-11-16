#pragma once
// Core mode (S3): Renderer::core_post_command, the x86 end of a typed scene
// command (included at the end of renderer.h). A command with a wire id is
// encoded with its frame envelope (when it declares frame_values) and sent;
// one without is refused (logged once by site, its visual omitted).
#if !defined(EID3D11_EMBEDDED) && !defined(EID3D11_SHADER_TOOL)
#include "scene_core_types.h"

namespace owned {

// Core stream deltas (engine_bridge.cpp; the host's is a no-op): forget what
// the host was sent, so every delta slot travels again.
void core_stream_delta_reset() noexcept;

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
            // The flag byte and a changed envelope (small); the command is
            // encoded straight into its ring record below.
            static thread_local std::vector<std::uint8_t> bytes, encoded;
            bytes.assign(1, 0);
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
                encoded.clear();
                scene_wire::encode_into(encoded, envelope);
                // The host keeps the last envelope it was sent: only a change travels.
                if (encoded != core_envelope_) {
                    bytes[0] = 1;
                    bytes.insert(bytes.end(), encoded.begin(), encoded.end());
                    core_envelope_ = encoded;
                }
            }
            scene_wire::Count count;
            scene_wire::field(count, const_cast<C&>(command));  // Count never writes the value
            struct Fill { const std::vector<std::uint8_t>* head; const C* command; } fill{&bytes, &command};
            core_send_fill(static_cast<std::uint16_t>(id), bytes.size() + count.bytes,
                [](void* context, std::uint8_t* at) {
                    const auto& f = *static_cast<const Fill*>(context);
                    std::memcpy(at, f.head->data(), f.head->size());
                    scene_wire::OutAt out{at + f.head->size()};
                    scene_wire::field(out, const_cast<C&>(*f.command));  // OutAt never writes the value
                }, &fill);
        } catch (...) {
            // The failed post may have been a store's keep or a delta's base:
            // the stores start over at the next present.
            core_stream_reset();
            core_stream_delta_reset();
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
