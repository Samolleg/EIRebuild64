#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "E1TerrainSubmission.h"
#include "E1Surface.h"
#include "renderer.h"

namespace OwnedRender {
namespace {
struct TerrainSink {
    owned::Renderer& renderer;
    HRESULT Begin(const E1TerrainPassSource& pass) {
        renderer.set_surface_texture_resolver(&E1ResolveSurface, nullptr);
        if (!renderer.set_native_world_lighting(pass.lighting)) return E_INVALIDARG;
        return renderer.set_native_terrain_frame(pass.geometry->animation, false,
            &pass.geometry->layer_shifts);
    }
    HRESULT Queue(const E1TerrainCommandSource& command) {
        const auto state = owned::native_owned_submission_state({}, command.view.owned_fixed_state);
        return renderer.queue_native_owned(command.view, state);
    }
    HRESULT Finish() { return renderer.flush_native(); }
    void Abort() noexcept { renderer.discard_native(); }
};
}
HRESULT E1SubmitTerrainPass(owned::Renderer& renderer, bool transparent,
    E1TerrainSubmission& result) noexcept {
    TerrainSink sink{renderer};
    return E1SubmitTerrainPassTo(sink, transparent, result);
}
}
