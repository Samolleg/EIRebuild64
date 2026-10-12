#include "E1VisualFrame.h"
#include "../GameJournal.h"
#include "renderer.h"
#include "engine_bridge.h"
#include "retained_static_scene.h"
#include <algorithm>
#include <cstring>
#include <vector>
#include <map>

namespace {
// Native sinks of the embedded host. They mirror the DirectDraw proxy's
// Device adapters (proxy.cpp) without its legacy draw queue: the embedded
// host has no pending legacy draws or UI blits to drain before native work.
struct Sink {
    owned::Renderer* renderer = nullptr;
    owned::FixedState state{};
    owned::Renderer::NativeQueueCheckpoint checkpoint{};
    bool object = false;
    bool staticsFlushed = false;
};
Sink* self(void* context) noexcept { return static_cast<Sink*>(context); }
bool lastFlushed = false;
std::vector<std::int32_t> currentDrawn;   // sorted, this world draw only
bool ranThisDraw = false;
std::uint64_t reportEpoch = 0;
std::map<std::int32_t, bool> reportedVisuals;
struct SecondaryVisual { std::uint64_t primary=0; std::shared_ptr<const owned::OwnedVisualSurface> surface; };
std::map<std::int32_t,SecondaryVisual> secondaryVisuals;

bool submit(void* context, const owned::NativeGeometryView& view, owned::NativeGeometryResult& result) noexcept try {
    using namespace owned;
    Sink& sink = *self(context);
    Renderer& r = *sink.renderer;
    result = {};
    if (view.static_submission) {
        const auto& request = *view.static_submission;
        using Operation = NativeStaticSubmission::Operation;
        request.generation = r.native_static_generation();
        if (request.operation == Operation::generation) return true;
        if (request.operation == Operation::shadow_static || request.operation == Operation::shadow_dynamic) {
            request.generation = r.set_native_shadow_phase(request.operation == Operation::shadow_static ? 1u : 2u);
            return true;
        }
        if (request.operation == Operation::terrain_frame) {
            if (!request.terrain_state) { result.failure_operation = "embedded.terrain_frame"; return false; }
            return SUCCEEDED(r.set_native_terrain_frame(*request.terrain_state, request.full_sun, request.layer_shifts));
        }
        if (request.operation == Operation::remove) { r.remove_native_static_object(request.key, request.terrain); return true; }
        if (request.operation == Operation::flush) {
            if (!request.world_to_clip) { result.failure_operation = "embedded.static_clip"; return false; }
            const HRESULT hr = request.terrain ? r.flush_native_static_terrain(sink.state, *request.world_to_clip)
                                               : r.flush_native_static(sink.state, *request.world_to_clip);
            if (SUCCEEDED(hr) && !request.terrain) sink.staticsFlushed = true;
            if (FAILED(hr)) { result.failure_hresult = hr; result.failure_operation = "embedded.static_flush"; }
            return SUCCEEDED(hr);
        }
        bool registered = false;
        HRESULT hr = S_OK;
        if (request.terrain) {
            if (!request.terrain_view) { result.failure_operation = "embedded.static_terrain_view"; return false; }
            const auto& terrain = *request.terrain_view;
            const FixedState terrainState = terrain.owned_fixed_state.enabled
                ? native_owned_submission_state(sink.state, terrain.owned_fixed_state) : sink.state;
            hr = r.set_native_static_terrain(request.key, terrain, terrainState, nullptr, registered, &result);
        } else {
            if (!request.records) { result.failure_operation = "embedded.static_records"; return false; }
            hr = r.set_native_static_object(request.key, *request.records, sink.state, registered, &result);
        }
        request.registered = registered;
        return SUCCEEDED(hr);
    }
    if (view.depth_submission)
        return SUCCEEDED(r.queue_native_depth(*view.depth_submission, sink.state, &result));
    if (view.prepared_submission) {
        const auto& submission=*view.prepared_submission;
        const auto secondary=secondaryVisuals.find(submission.identity.signed_id);
        if(secondary!=secondaryVisuals.end())
            return SUCCEEDED(r.queue_embedded_records(submission,sink.state,
                secondary->second.primary,secondary->second.surface.get(),&result));
        return SUCCEEDED(r.queue_native_records(submission,sink.state,nullptr,&result));
    }
    // No DirectDraw wound surfaces exist in the embedded host.
    if (view.wound_only) return false;
    const FixedState state = view.owned_fixed_state.enabled
        ? native_owned_submission_state(sink.state, view.owned_fixed_state) : sink.state;
    if (view.owned_texture.identity) return SUCCEEDED(r.queue_native_owned(view, state, &result));
    const bool prepared = view.prepared_mesh && view.prepared_material && view.prepared_identity.valid;
    return SUCCEEDED(prepared ? r.queue_native_prepared(view, state, nullptr, &result)
                              : r.queue_native(view, state, nullptr, &result));
} catch (...) {
    if (!result.failure_operation) result.failure_operation = "embedded.exception";
    return false;
}
bool beginBatch(void*, owned::SemanticKind) noexcept { return true; }
void endBatch(void* context, bool commit) noexcept { if (!commit) self(context)->renderer->discard_native(); }
bool beginObject(void* context) noexcept {
    Sink& sink = *self(context);
    if (sink.object || !sink.renderer->checkpoint_native(sink.checkpoint)) return false;
    sink.object = true;
    return true;
}
void endObject(void* context, bool commit) noexcept {
    Sink& sink = *self(context);
    if (!sink.object) return;
    if (!commit) sink.renderer->rollback_native(sink.checkpoint);
    sink.object = false;
}
bool frameState(void* context, owned::NativeFrameState& output) noexcept {
    const Sink& sink = *self(context);
    const auto& vp = sink.state.viewport;
    output.viewport = {vp.dwX, vp.dwY, vp.dwWidth, vp.dwHeight};
    std::memcpy(output.view.data(), &sink.state.transform[D3DTRANSFORMSTATE_VIEW], sizeof(output.view));
    std::memcpy(output.projection.data(), &sink.state.transform[D3DTRANSFORMSTATE_PROJECTION], sizeof(output.projection));
    output.material = owned::native_owned_fixed_state(sink.state);
    output.shadows_enabled = sink.renderer->native_shadows_enabled();
    return true;
}
bool worldLighting(void* context, const owned::NativeWorldLighting& lighting) noexcept {
    return self(context)->renderer->set_native_world_lighting(lighting);
}
bool shadowBegin(void* context, const owned::NativeShadowFrame& frame, std::uint8_t& dirty) noexcept {
    return self(context)->renderer->begin_native_shadows(frame, dirty);
}
bool shadowEnd(void* context, bool commit) noexcept { return self(context)->renderer->end_native_shadows(commit); }
void shadowDiagnostics(void* context, std::uint64_t serial, const owned::NativeShadowSourceDiagnostics& values) noexcept {
    self(context)->renderer->record_native_shadow_source_diagnostics(serial, values);
}
bool pointLights(void* context, const owned::RetainedVisualFrame& frame) noexcept {
    return self(context)->renderer->set_native_point_lights(frame.world_point_lights, frame.time);
}
bool pointShadowBegin(void* context, std::uint32_t light, std::uint32_t firstFace,
                      const owned::NativePointShadowFrame& faces) noexcept {
    return self(context)->renderer->begin_native_point_shadow_faces(light, firstFace, faces);
}
bool pointShadowEnd(void* context, bool commit) noexcept { return self(context)->renderer->end_native_point_shadow_faces(commit); }
bool staticDepth(void* context, const owned::RetainedShadowCasterValue* casters, std::uint32_t count,
                 std::vector<owned::RetainedShadowCasterValue>& missing) noexcept {
    return self(context)->renderer->queue_native_static_shadow_depth(casters, count, missing);
}
bool pointShadowBatch(void* context, std::uint64_t& generation, bool& pressure) noexcept {
    return self(context)->renderer->begin_native_point_shadow_batch(generation, &pressure);
}
void pointShadowPublish(void* context, const std::vector<owned::NativePointShadowPublication>& lights,
                        const owned::NativePointShadowStats& stats) noexcept {
    try { self(context)->renderer->publish_native_point_shadows(lights, stats); } catch (...) {}
}
bool pointShadowAllocate(void* context, bool overlay, int& slot) noexcept {
    return self(context)->renderer->allocate_native_point_shadow(overlay, slot);
}
void pointShadowRelease(void* context, std::uint64_t generation, int slot) noexcept {
    self(context)->renderer->release_native_point_shadow(generation, slot);
}
void shadowCull(void* context, owned::NativeShadowCullProjection& projection) noexcept {
    self(context)->renderer->native_shadow_cull_projection(projection);
}
}

namespace OwnedRender {
bool E1RunOwnedVisualFrame(owned::Renderer& renderer, const owned::FixedState& state,
                           const E1VisualInputs& host) noexcept {
    owned::EmbeddedVisualFrameInputs inputs;
    inputs.time = host.time;
    inputs.scene_flags = host.sceneFlags;
    inputs.draw_undetected = host.drawUndetected;
    inputs.world_to_clip = host.worldToClip;
    inputs.lighting.ambient = host.ambient;
    inputs.lighting.sunlight_diffuse = host.sunDiffuse;
    inputs.lighting.sunlight_direction = host.sunDirection;
    inputs.lighting_valid = host.lightingValid;
    inputs.fog = host.fog;
    inputs.fog_context = host.fogContext;
    Sink sink;
    sink.renderer = &renderer;
    sink.state = state;
    owned::bridge_set_native_geometry_sink(&sink, &submit, &beginBatch, &endBatch, &beginObject, &endObject,
        &frameState, &worldLighting, &shadowBegin, &shadowEnd, &shadowDiagnostics, &pointLights,
        &pointShadowBegin, &pointShadowEnd, &staticDepth, &pointShadowBatch, &pointShadowPublish,
        &pointShadowAllocate, &pointShadowRelease, &shadowCull);
    std::vector<std::int32_t> drawn;
    const bool ok = owned::bridge_embedded_owned_visual_frame(inputs, &drawn);
    std::sort(drawn.begin(), drawn.end());
    currentDrawn.swap(drawn);
    ranThisDraw = true;
    if (sink.object) renderer.rollback_native(sink.checkpoint);
    // The sinks point at this stack frame: never leave them installed.
    owned::bridge_set_native_geometry_sink(nullptr, nullptr);
    lastFlushed = sink.staticsFlushed;
    return ok;
}
bool E1OwnedVisualFrameFlushedStatics() noexcept { return lastFlushed; }
void E1PublishSecondaryVisual(std::int32_t id, std::uint64_t primary,
    std::shared_ptr<const owned::OwnedVisualSurface> secondary) noexcept try {
    if(id>0 && primary)secondaryVisuals[id]={primary,std::move(secondary)};
} catch(...) {}
void E1OwnedVisualFrameNewDraw() noexcept {
    secondaryVisuals.clear();
    currentDrawn.clear();
    lastFlushed = false;
    ranThisDraw = false;
}
void E1ReportOwnedVisual(std::int32_t id) noexcept try {
    const auto epoch = owned::bridge_current_world_epoch();
    if (reportEpoch != epoch) {
        reportedVisuals.clear();
        reportEpoch = epoch;
    }
    const bool drawn = E1OwnedVisualDrawn(id);
    const auto previous = reportedVisuals.find(id);
    if (previous == reportedVisuals.end() || previous->second != drawn) {
        reportedVisuals[id] = drawn;
        GameJournal::Event("RENDER", "world_visual",
            "id=%d owner=c56 status=%s", id, drawn ? "drawn" : "skipped_unclaimed");
    }
} catch (...) {}
bool E1OwnedVisualDrawn(std::int32_t id) noexcept {
    return ranThisDraw && id > 0 && std::binary_search(currentDrawn.begin(), currentDrawn.end(), id);
}
}
