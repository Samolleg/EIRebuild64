#pragma once
// Immutable observations of nativeBuildCS, not a second LOD selector.
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace owned {
constexpr std::uint32_t kNativeLodObservationCapacity = 32768;
// Records also include terrain and shadow passes. Their sparse indices are
// independent of the bounded number of observed figure rows (48 MiB max,
// including the three readbacks; allocated only on an observation request).
constexpr std::uint32_t kNativeLodObservationRecordLimit = 262144;
struct NativeLodIdentity {
    std::int32_t id = 0;
    std::uint64_t generation = 0;
    std::uint32_t part = 0;
    std::string model, zone, map, mob;
    bool neighbour = false;
    bool candidate_id = false; // neighbour stream index, not an engine object ID
};
struct NativeLodDecisionGpu {
    std::uint32_t state = 0; // 0 not observed, 1 culled, 2 selection completed
    std::uint32_t first_level = UINT32_MAX, second_level = UINT32_MAX;
    std::uint32_t first_triangles = 0, second_triangles = 0;
    std::uint32_t level_count = 0, full_triangles = 0, fade = 0;
    std::array<float,3> position{}; // actual GPU node origin in world coordinates
    std::uint32_t merged = 0;
};
static_assert(sizeof(NativeLodDecisionGpu)==48,"native LOD UAV layout");
struct NativeLodRow {
    std::uint32_t record = 0;
    std::shared_ptr<const NativeLodIdentity> identity;
    NativeLodDecisionGpu decision;
};
struct NativeLodSnapshot {
    std::uint64_t frame = 0, world_epoch = 0, request = 0;
    std::uint64_t camera_frame = 0;
    double main_camera_max_delta = 0; // largest absolute matrix element difference on rejection
    std::uint32_t record_capacity = 0, observed_rows = 0;
    bool truncated = false, unavailable = false;
    const char* unavailable_reason = nullptr; // static reason, owned by the producer
    std::vector<NativeLodRow> rows;
};
// A try-lock copies only a shared pointer. Neither producer nor consumer waits.
std::shared_ptr<const NativeLodSnapshot> native_lod_snapshot() noexcept;
// Game thread: reuse an outstanding request, otherwise request one future full frame.
std::uint64_t native_lod_request(std::uint64_t world_epoch) noexcept;
void native_lod_acknowledge(std::uint64_t request) noexcept;
} // namespace owned
