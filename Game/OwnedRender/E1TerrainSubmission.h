#pragma once
#include "E1Terrain.h"

namespace owned { class Renderer; }
namespace OwnedRender {
struct E1TerrainSubmission {
    std::size_t queued = 0;
    bool completed = false;
    const char* failure = nullptr;
};
// Synchronous pass boundary. Failure aborts the frame; it is never permission
// to replay an already partially submitted pass through a second renderer.
// The sink consumes each borrowed decoder context before Queue returns.
template<class Sink>
HRESULT E1SubmitTerrainPassTo(Sink& sink, bool transparent, E1TerrainSubmission& result) noexcept {
    result = {};
    E1TerrainPassSource pass;
    if (!E1PrepareTerrainPass(transparent, pass)) {
        result.failure = "terrain.pass_source";
        return E_PENDING;
    }
    bool began = false;
    const auto fail = [&](HRESULT hr, const char* reason) {
        result.failure = reason;
        if (began) sink.Abort();
        return hr;
    };
    try {
        // Begin may have changed renderer state before reporting a failure.
        began = true;
        HRESULT hr = sink.Begin(pass);
        if (FAILED(hr)) return fail(hr, "terrain.begin");
        for (std::size_t index = 0; index < pass.commands; ++index) {
            E1TerrainCommandSource command;
            if (!E1PrepareTerrainCommand(transparent, index, command) ||
                command.geometry != pass.geometry) return fail(E_PENDING, "terrain.command_source");
            hr = sink.Queue(command);
            if (FAILED(hr)) return fail(hr, "terrain.queue");
            ++result.queued;
        }
        hr = sink.Finish();
        if (FAILED(hr)) return fail(hr, "terrain.flush");
        result.completed = true;
        return S_OK;
    } catch (const std::bad_alloc&) { return fail(E_OUTOFMEMORY, "terrain.allocation"); }
      catch (...) { return fail(E_FAIL, "terrain.exception"); }
}
HRESULT E1SubmitTerrainPass(owned::Renderer& renderer, bool transparent,
    E1TerrainSubmission& result) noexcept;
}
