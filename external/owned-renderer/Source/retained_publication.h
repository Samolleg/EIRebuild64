#pragma once
// Retained-scene publication stream (phase 2, S2d). In the remote device mode
// every mutating RetainedStaticScene call is recorded as one delta message at
// its commit point, inside the exclusive scene lock, so the stream order is
// the order the scene applied them, whichever thread called (game thread,
// loader worker, render thread). Assets the deltas reference are defined
// first (owned_asset_codec.h). The x64 host replays the stream into its own
// RetainedStaticScene (Host/retained_replica.cpp).
//
// Standard mode: no recorder is attached; each mutator pays one null check.
//
// Not recorded, by design: calls that publish renderer output or consume
// the scene (publish_shadow_input_domain, shadow/visual change reads and
// acknowledgements, frame scratch release, the surface texture retainer
// wiring). Those belong to the consumer that moves to the host at S3.
//
// The temporary replica digest of S2d was removed at S3 (core mode prepares
// the world from the replica itself; a stream failure loses the device).
#include "owned_asset_codec.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace owned {

struct VisualAssemblyAsset;
struct VisualTickValues;
struct VisualObjectPose;
struct VisualObjectPublicationSnapshot;
struct RetainedHighlightState;
struct RetainedWorldPointLight;
struct RetainedQuestLight;

// First byte of a message. 1 and 2 are the asset tags (owned_asset_codec.h).
enum class RetainedOp : std::uint8_t {
    begin_source = 16, record_parser_source, record_admission, finish_source, abandon_source, publish_source,
    lifecycle_added, lifecycle_removed, positive_lifecycle_reset, reset,
    visual_detach, visual_retire_detached, retainer_cleared, visual_seed, visual_reconcile_seeds,
    visual_begin_resource, visual_finish_resource, visual_destroy_resource, visual_declare_mesh,
    visual_bind_database, visual_publish_assembly, visual_replace_surface, visual_replace_generated_texture,
    visual_publish_route, visual_publish_intent, visual_publish_root_rotation, visual_append_idle,
    visual_publish_tick_values, visual_publish_detection, visual_reject_detection,
    visual_reject_detected_unit_detection, visual_publish_object_pose, visual_reject_object_pose_if_current,
    visual_publish_forced_weapon_ids, visual_publish_bridges, visual_publish_quest_lights,
    visual_publish_highlight, visual_publish_world_point_lights, visual_reject, visual_reject_all,
    visual_commit_tick, publish_terrain_geometry, publish_terrain_texture,
    visual_publish_preview, material_publish_composition,
    end
};


class RetainedPublicationRecorder;

// A delta under construction: the local message buffer plus asset references
// (defined on first use, before this message is sent).
class RetainedDeltaOut {
public:
    codec::Out out;
    template<class T> void ref(const std::shared_ptr<const T>& value) { const auto id = assets_.id(value); codec::io(out, id); }
    template<class T> void ref(const std::shared_ptr<T>& value) { ref(std::shared_ptr<const T>(value)); }
    std::uint64_t key_of(const void* address) const noexcept { return assets_.key_of(address); }
    std::size_t asset_count() const noexcept { return assets_.live(); }
private:
    friend class RetainedPublicationRecorder;
    explicit RetainedDeltaOut(codec::AssetEncoder& assets) noexcept : assets_(assets) {}
    codec::AssetEncoder& assets_;
};

// The replica's view of a delta: input plus decoded asset lookups.
class RetainedDeltaIn {
public:
    RetainedDeltaIn(codec::In& input, const codec::AssetDecoder& assets) noexcept : in(input), assets_(assets) {}
    codec::In& in;
    template<class T> void ref(std::shared_ptr<const T>& value) {
        std::uint64_t id = 0;
        codec::io(in, id);
        if (!in.ok() || !assets_.get(id, value)) in.fail();
    }
private:
    const codec::AssetDecoder& assets_;
};

// Value codecs of the delta arguments (both directions).
void delta_io(RetainedDeltaOut& s, const VisualClipSelection& v);
void delta_io(RetainedDeltaIn& s, VisualClipSelection& v);
void delta_io(RetainedDeltaOut& s, const VisualPoseIntent& v);
void delta_io(RetainedDeltaIn& s, VisualPoseIntent& v);
void delta_io(RetainedDeltaOut& s, const VisualObjectPose& v);
void delta_io(RetainedDeltaIn& s, VisualObjectPose& v);
void delta_io(RetainedDeltaOut& s, const VisualObjectPublicationSnapshot& v);
void delta_io(RetainedDeltaIn& s, VisualObjectPublicationSnapshot& v);
void delta_io(RetainedDeltaOut& s, const CompiledVisualRoute& v);
void delta_io(RetainedDeltaIn& s, CompiledVisualRoute& v);
void delta_io(RetainedDeltaOut& s, const VisualTickValues& v);
void delta_io(RetainedDeltaIn& s, VisualTickValues& v);
void delta_io(RetainedDeltaOut& s, const RetainedHighlightState& v);
void delta_io(RetainedDeltaIn& s, RetainedHighlightState& v);
void delta_io(RetainedDeltaOut& s, const std::vector<RetainedWorldPointLight>& v);
void delta_io(RetainedDeltaIn& s, std::vector<RetainedWorldPointLight>& v);
void delta_io(RetainedDeltaOut& s, const std::vector<RetainedQuestLight>& v);
void delta_io(RetainedDeltaIn& s, std::vector<RetainedQuestLight>& v);
void delta_io(RetainedDeltaOut& s, const OwnedTerrainGeometryValues& v);
void delta_io(RetainedDeltaIn& s, OwnedTerrainGeometryValues& v);
// Program-literal predicates (a null pointer is kept apart from "").
void delta_text(RetainedDeltaOut& s, const char* text);

// Game-process side. Thread safe: one lock serializes ids, definitions and
// messages, so asset definitions never interleave with another message.
class RetainedPublicationRecorder {
public:
    // sink receives every message in fragments of at most `fragment` bytes.
    RetainedPublicationRecorder(codec::Out::Sink sink, void* context, std::size_t fragment) noexcept
        : sink_(sink), context_(context), fragment_(fragment), assets_(sink, context, fragment) {}

    // Defines an asset ahead of the delta that names it (outside the scene
    // lock for large ones: snapshots, assemblies).
#if defined(EID3D11_EMBEDDED) || defined(EID3D11_SHADER_TOOL)
    template<class T> void asset(const std::shared_ptr<T>&) noexcept {}
    template<class F> void delta(RetainedOp, F&&) noexcept {}
#else
    template<class T> void asset(const std::shared_ptr<T>& value) noexcept {
        if (!value || failed_.load(std::memory_order_relaxed)) return;
        std::lock_guard<std::mutex> guard(lock_);
        try { assets_.id(std::shared_ptr<const T>(value)); } catch (...) { fail("asset allocation"); }
    }
    // Defines `full` now and lets `replacement` stand for it from here on
    // (AssetEncoder::alias). False: the stream has failed.
    template<class T> bool asset_alias(const std::shared_ptr<const T>& full,
                                       const std::shared_ptr<const T>& replacement) noexcept {
        if (!full || failed_.load(std::memory_order_relaxed)) return false;
        std::lock_guard<std::mutex> guard(lock_);
        try { assets_.alias(full, replacement); } catch (...) { fail("asset allocation"); return false; }
        return true;
    }
    // One delta message: the op, then what `write` adds.
    template<class F> void delta(RetainedOp op, F&& write) noexcept {
        if (failed_.load(std::memory_order_relaxed)) return;
        std::lock_guard<std::mutex> guard(lock_);
        try {
            if (++deltas_ % 1024 == 0) assets_.sweep();
            RetainedDeltaOut message(assets_);
            message.out.buffer.swap(scratch_); // reuse one allocation per stream
            message.out.buffer.clear();
            codec::io(message.out, op);
            write(message);
            emit(message.out.buffer);
            message.out.buffer.swap(scratch_);
        } catch (...) { fail("delta allocation"); }
    }
#endif
    // The stream stopped (an allocation failed): the host's replica is
    // incomplete from here on.
    bool failed() const noexcept { return failed_.load(std::memory_order_relaxed); }

private:
    void emit(const std::vector<std::uint8_t>& message) noexcept;
    void fail(const char* what) noexcept;

    codec::Out::Sink sink_;
    void* context_;
    std::size_t fragment_;
    std::mutex lock_;
    codec::AssetEncoder assets_;
    std::vector<std::uint8_t> scratch_;
    std::uint64_t deltas_ = 0;
    std::atomic<bool> failed_{false};
};

// x86 DLL only (retained_publication_client.cpp): attaches the recorder to
// the runtime's retained scene once the remote device link is up. Called
// before any map is loaded; idempotent.
void retained_publication_attach() noexcept;

} // namespace owned
