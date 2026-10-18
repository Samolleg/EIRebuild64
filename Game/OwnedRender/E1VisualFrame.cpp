#include "E1VisualFrame.h"
#include "E1PerfDiagnostics.h"
#include "E1TerrainSubmission.h"
#include "E1Surface.h"
#include "../GameJournal.h"
#include "renderer.h"
#include "engine_bridge.h"
#include "retained_static_scene.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>
#include <map>
#include <set>
#include <string>

namespace {
// Native sinks of the embedded host. They mirror the DirectDraw proxy's
// Device adapters (proxy.cpp) without its legacy draw queue: the embedded
// host has no pending legacy draws or UI blits to drain before native work.
struct Sink {
    owned::Renderer* renderer = nullptr;
    owned::FixedState state{};
    owned::Renderer::NativeQueueCheckpoint checkpoint{};
    owned::NativeGeometryResult batchFailure{};
    bool object = false;
    bool batch = false;
    bool staticsFlushed = false;
    bool shadowsEnabled = true;
};
Sink* self(void* context) noexcept { return static_cast<Sink*>(context); }
bool lastFlushed = false;
std::vector<std::int32_t> currentDrawn;   // sorted, this world draw only
std::map<std::int32_t, const char*> currentOmissions;
std::uint64_t batchRollbackReportEpoch = 0;
void reportBatchFailure(const owned::NativeGeometryResult& result, const char* fallback) noexcept {
    const auto epoch = owned::bridge_current_world_epoch();
    if (!epoch || batchRollbackReportEpoch == epoch) return;
    batchRollbackReportEpoch = epoch;
    GameJournal::Event("RENDER", "world_visual_batch_rollback",
        "operation=%s hr=%08lx", result.failure_operation ? result.failure_operation : fallback,
        static_cast<unsigned long>(result.failure_hresult));
}
struct PoseFailureSummary {
    std::set<std::int32_t> ids;
    bool reported = false;
};
std::map<std::string, PoseFailureSummary> poseFailures;
std::uint64_t poseFailureEpoch = 0;
bool ranThisDraw = false;

void reportPoseFailures(std::uint64_t epoch) noexcept try {
    if (!epoch || epoch != poseFailureEpoch) return;
    for (auto& entry : poseFailures) {
        auto& summary = entry.second;
        if (summary.reported || summary.ids.empty()) continue;
        std::string ids;
        unsigned shown = 0;
        for (const auto id : summary.ids) {
            if (shown == 5) break;
            if (!ids.empty()) ids += ",";
            ids += std::to_string(id);
            ++shown;
        }
        GameJournal::Event("RENDER", "world_visual_pose_reject",
            "epoch=%llu step=%s objects=%u ids=%s",
            static_cast<unsigned long long>(epoch), entry.first.c_str(),
            static_cast<unsigned>(summary.ids.size()), ids.c_str());
        summary.reported = true;
    }
} catch (...) {}
std::uint64_t reportEpoch = 0;
std::uint64_t omissionReportEpoch = 0;
std::map<std::int32_t, bool> reportedVisuals;
struct SecondaryVisual { std::uint64_t primary=0; std::shared_ptr<const owned::OwnedVisualSurface> surface; };
std::map<std::int32_t,SecondaryVisual> secondaryVisuals;

bool submitImpl(void* context, const owned::NativeGeometryView& view, owned::NativeGeometryResult& result) noexcept try {
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
        // Match the proxy's bulk prepared-record route unless a wound layer
        // is actually published. Unit entries are also published with an
        // empty secondary surface, which is not a reason to split the pose
        // GPU submission into per-part queue_native_owned calls.
        if(secondary!=secondaryVisuals.end() && secondary->second.surface)
            return SUCCEEDED(r.queue_embedded_records(submission,sink.state,
                secondary->second.primary,secondary->second.surface.get(),&result));
        return SUCCEEDED(r.queue_native_records(submission,sink.state,nullptr,&result));
    }
    if (view.wound_only) {
        const auto secondary=secondaryVisuals.find(view.prepared_identity.signed_id);
        if (secondary==secondaryVisuals.end() || !secondary->second.surface ||
            secondary->second.surface->id==0) {
            result.failure_hresult = E_INVALIDARG;
            result.failure_operation = "embedded.wound_surface_unpublished";
            return false;
        }
        return SUCCEEDED(r.queue_embedded_wound(view,sink.state,
            *secondary->second.surface,&result));
    }
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
bool submit(void* context, const owned::NativeGeometryView& view, owned::NativeGeometryResult& result) noexcept {
    const bool accepted = submitImpl(context, view, result);
    if (!accepted) self(context)->batchFailure = result;
    return accepted;
}
bool beginBatch(void* context, owned::SemanticKind) noexcept {
    Sink& sink = *self(context);
    if (sink.batch) return false;
    sink.batchFailure = {};
    const HRESULT hr = sink.renderer->flush_native();
    if (FAILED(hr)) {
        sink.batchFailure.failure_hresult = hr;
        sink.batchFailure.failure_operation = "embedded.batch_begin_flush";
        reportBatchFailure(sink.batchFailure, "embedded.batch_begin_rejected");
        return false;
    }
    sink.batch = true;
    return true;
}
void endBatch(void* context, bool commit) noexcept {
    Sink& sink = *self(context);
    if (!sink.batch) return;
    if (!commit) {
        sink.renderer->discard_native();
        reportBatchFailure(sink.batchFailure, "embedded.batch_rejected");
    }
    sink.batch = false;
    sink.batchFailure = {};
}
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
    output.shadows_enabled = sink.shadowsEnabled && sink.renderer->native_shadows_enabled();
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
                 std::uint64_t revision, std::vector<owned::RetainedShadowCasterValue>& missing) noexcept {
    return self(context)->renderer->queue_native_static_shadow_depth(casters, count, revision, missing);
}
bool terrainBake(void* context, const owned::NativeTerrainShadowEntry* entries,
                 std::uint32_t count, std::vector<std::uint32_t>& absent) noexcept {
    return self(context)->renderer->queue_native_terrain_shadow_bake(entries, count, absent);
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
    static E1PerfCounter counter{"scene_total"};
    E1PerfScope timing(counter);
    owned::EmbeddedVisualFrameInputs inputs;
    inputs.complete_visuals = &E1CompleteVisualFrame;
    inputs.time = host.time;
    inputs.scene_flags = host.sceneFlags;
    inputs.draw_undetected = host.drawUndetected;
    inputs.briefing_screen = host.briefingScreen;
    inputs.hover_id = host.hoverId;
    inputs.hover_serial = host.hoverSerial;
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
    sink.shadowsEnabled = renderer.native_shadows_enabled() || renderer.native_point_shadows_enabled();
    inputs.scene_flags = (inputs.scene_flags & ~0x200u) | (sink.shadowsEnabled ? 0x200u : 0u);
    struct TerrainContext {
        owned::Renderer& renderer;
        const owned::FixedState& state;
        const std::array<float, 16>& worldToClip;
    } terrainContext{renderer, state, host.worldToClip};
    owned::RetainedTerrainSourceSnapshot terrain;
    if (host.sceneFlags & 0x100u) {
        E1ShadowTerrain(inputs, terrain);
        inputs.draw_terrain = [](void* context) -> bool {
            auto& terrain = *static_cast<TerrainContext*>(context);
            E1TerrainSubmission result;
            return SUCCEEDED(E1SubmitTerrainPass(terrain.renderer, false, result)) &&
                result.completed &&
                // Adopt streamed neighbours and draw their opaque terrain/objects
                // before world figures. Their water stays in composite_world.
                SUCCEEDED(terrain.renderer.flush_native_static_terrain(
                    terrain.state, terrain.worldToClip));
        };
        inputs.terrain_context = &terrainContext;
    }
    renderer.set_surface_texture_resolver(&E1ResolveSurface, nullptr);
    owned::bridge_set_native_geometry_sink(&sink, &submit, &beginBatch, &endBatch, &beginObject, &endObject,
        &frameState, &worldLighting, &shadowBegin, &shadowEnd, &shadowDiagnostics, &pointLights,
        &pointShadowBegin, &pointShadowEnd, &staticDepth, &pointShadowBatch, &pointShadowPublish,
        &pointShadowAllocate, &pointShadowRelease, &shadowCull, &terrainBake);
    owned::bridge_set_rain_occlusion_sink(
        [](void* context, const owned::NativeShadowSlice& slice, std::uint32_t side) {
            return self(context)->renderer->begin_native_rain_occlusion(slice, side);
        },
        [](void* context, bool commit) {
            return self(context)->renderer->end_native_rain_occlusion(commit);
        });
    std::vector<std::int32_t> drawn;
    std::vector<owned::EmbeddedVisualOmission> omitted;
    const bool ok = owned::bridge_embedded_owned_visual_frame(inputs, &drawn, &omitted);
    std::sort(drawn.begin(), drawn.end());
    currentDrawn.swap(drawn);
    currentOmissions.clear();
    for (const auto& item : omitted)
        if (item.id > 0 && item.reason) currentOmissions.emplace(item.id, item.reason);
    const auto epoch = owned::bridge_current_world_epoch();
    if (!omitted.empty() && epoch && omissionReportEpoch != epoch) {
        omissionReportEpoch = epoch;
        std::map<std::string, std::pair<std::size_t, std::vector<std::int32_t>>> summary;
        for (const auto& item : omitted) {
            if (item.id <= 0 || !item.reason) continue;
            auto& reason = summary[item.reason];
            ++reason.first;
            if (reason.second.size() < 5) reason.second.push_back(item.id);
        }
        char details[384]{};
        std::size_t detailsUsed = 0;
        for (const auto& entry : summary) {
            char ids[64]{};
            std::size_t used = 0;
            for (const auto id : entry.second.second) {
                const int written = std::snprintf(ids + used, sizeof(ids) - used,
                    "%s%d", used ? "," : "", id);
                if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(ids) - used) break;
                used += static_cast<std::size_t>(written);
            }
            const int written = std::snprintf(details + detailsUsed, sizeof(details) - detailsUsed,
                "%s%s=%llu ids=%s", detailsUsed ? ";" : "", entry.first.c_str(),
                static_cast<unsigned long long>(entry.second.first), ids);
            if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(details) - detailsUsed) break;
            detailsUsed += static_cast<std::size_t>(written);
        }
        GameJournal::Event("RENDER", "world_visual_omission",
            "owner=c56 %s", detailsUsed ? details : "count=0");
    }
    reportPoseFailures(epoch);
    ranThisDraw = true;
    if (sink.object) renderer.rollback_native(sink.checkpoint);
    // The sinks point at this stack frame: never leave them installed.
    owned::bridge_set_rain_occlusion_sink(nullptr, nullptr);
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
    currentOmissions.clear();
    lastFlushed = false;
    ranThisDraw = false;
}
void E1ReportOwnedPoseFailure(std::int32_t id, const char* step) noexcept try {
    const auto epoch = owned::bridge_current_world_epoch();
    if (!epoch || id <= 0 || !step || !*step) return;
    if (poseFailureEpoch != epoch) {
        poseFailures.clear();
        poseFailureEpoch = epoch;
    }
    auto& summary = poseFailures[step];
    if (summary.ids.insert(id).second) summary.reported = false;
} catch (...) {}
void E1ReportOwnedVisual(std::int32_t id) noexcept try {
    const auto epoch = owned::bridge_current_world_epoch();
    if (reportEpoch != epoch) {
        reportedVisuals.clear();
        reportEpoch = epoch;
    }
    const bool drawn = E1OwnedVisualDrawn(id);
    if (!drawn && epoch && omissionReportEpoch != epoch) {
        omissionReportEpoch = epoch;
        const char* reason = E1OwnedVisualOmission(id);
        GameJournal::Event("RENDER", "world_visual_omission",
            "id=%d owner=c56 reason=%s", id, reason ? reason : "entity_missing_from_frame");
    }
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
const char* E1OwnedVisualOmission(std::int32_t id) noexcept {
    if (!ranThisDraw || id <= 0) return nullptr;
    const auto found = currentOmissions.find(id);
    return found == currentOmissions.end() ? nullptr : found->second;
}
}
