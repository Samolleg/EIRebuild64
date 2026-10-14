#pragma once

#include "owned_asset_loader.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace owned {

constexpr std::size_t kVisualRoutePointLimit = 4096;
constexpr std::size_t kVisualPartLimit = 4096;
constexpr std::size_t kVisualQueueLimit = 256;

// Select the current rotating object-pose sweep slice, including its wrap.
inline bool visual_pose_sweep_contains(std::size_t index, std::size_t count,
                                      std::size_t begin, std::size_t slice) noexcept {
    return index >= begin ? index - begin < slice
                          : count - begin + index < slice;
}

// Temporary publication diagnosis: preserve the first existing failed predicate.
// Every caller supplies a program literal; this never owns sampled game memory.
inline bool visual_failure(const char** first, const char* predicate) noexcept {
    if (first && !*first) *first = predicate;
    return false;
}

enum class VisualRouteKind : std::uint8_t {
    stand, rotate, walk, attack, attack_point
};

struct VisualWaypoint {
    std::array<float, 2> position{};
    std::array<float, 2> tangent{};
    float speed = 0;
};

struct VisualRouteValues {
    VisualRouteKind kind = VisualRouteKind::stand;
    double start_tick = 0;
    std::array<float, 2> start{};
    float start_angle = 0; // radians in the authoritative XY route plane
    float rotation_start_angle = 0;
    float rotation_delta = 0;
    float angular_speed = 0;
    std::int32_t target_id = 0;
    double hit_tick = 0;
    double turn_finish_tick = 0;
    std::vector<VisualWaypoint> points;
};

struct VisualRouteSegment {
    double begin = 0, end = 0;
    std::array<std::array<double, 4>, 2> polynomial{};
    float speed = 0;
    float chord_length = 0;
};

struct CompiledVisualRoute {
    VisualRouteValues values;
    std::uint64_t generation = 0;
    double rotation_ticks = 0;
    std::vector<VisualRouteSegment> segments;
};

struct VisualRouteSample {
    std::array<float, 2> position{};
    std::array<float, 2> direction{1, 0};
    float speed = 0.1f;
    std::size_t segment = 0;
};

bool compile_visual_route(VisualRouteValues values, std::uint64_t generation,
                          CompiledVisualRoute& route) noexcept;
bool sample_visual_route(const CompiledVisualRoute& route, double time,
                         VisualRouteSample& sample) noexcept;
// Speed in world units per game tick; direction (optional) is the unit XY
// route heading, zero while stationary.
float liquid_route_world_speed(const CompiledVisualRoute& route,double time,
    bool frozen,std::array<float,2>* direction=nullptr) noexcept;
bool face_visual_target(const CompiledVisualRoute& route, double time, double start_tick,
                        const std::array<float, 2>* target,
                        VisualRouteSample& sample) noexcept;

struct VisualClipSelection {
    std::shared_ptr<const AnimationDatabaseAsset> database;
    std::shared_ptr<const AnimationClipAsset> clip;
    std::uint32_t ordinal = 0;
    // A completed controller clear is a real pose, not a missing clip asset.
    bool neutral = false;
};

struct VisualPoseIntent {
    std::uint64_t revision = 0;
    std::uint64_t resource_source_generation = 0;
    std::vector<VisualClipSelection> selections;
    double anchor_time = 0;
    double phase = 0;
    float facing = 0;
};

struct VisualPoseState {
    std::uint64_t intent_revision = 0;
    double time = 0;
    double facing_time = 0;
    double phase = 0;
    std::size_t selection = 0;
    float facing = 0;
    std::array<float, 3> root{};
    std::array<float, 4> root_rotation{1, 0, 0, 0};
    bool root_rotation_valid = false;
    bool tilted_facing = false;
    // Unforced weapon visibility at the last owned tick. A neutral selection
    // preserves it until a semantic weapon replacement or another clip.
    bool weapon_hidden = false;
};

struct VisualRateValues {
    float coefficient = 1;
    float tall = 0;
    bool frozen = false;
    bool dont_move = false;
    bool movement_frozen = false;
    bool tilted_facing = false;
    bool scaled_animation = false;
};

struct VisualEffectState {
    double time = 0;
    double phase = 0;
    float opacity = 1;
    float stun_red = 0;
};

struct VisualEquipmentValues {
    bool has_weapon = false;
    bool weapon_is_bow = false;
    bool arrow_is_bow = false;
    bool human = false;
    bool speaking = false;
    bool forced_hidden = false;
};

struct VisualClipVisibility {
    bool hide_weapons = false;
    bool hide_arrow = false;
    bool casts_shadow = true;
};

// Visual timing policy: derive clip visibility at the owned sample, without
// inheriting the last camera-admitted controller's hidden-bit history.
VisualClipVisibility sample_visual_visibility(const AnimationEntryAsset& entry,
    double phase, bool dead,
    const VisualEquipmentValues& equipment) noexcept;
VisualClipVisibility sample_visual_visibility(const AnimationEntryAsset* entry,
    double phase, bool dead,
    const VisualEquipmentValues& equipment, bool previous_weapon_hidden) noexcept;
bool sample_visual_effects(const VisualEffectState& anchor, bool invisible,
                          bool stunned, float fade_alpha, double time,
                          VisualEffectState& result, const char** failure = nullptr) noexcept;

// Explicit intent is the only reset. Sampling is pure, so a second view or a
// second shadow slice cannot consume queue entries or advance the tick anchor.
bool anchor_visual_pose(const VisualPoseIntent& intent,
                        const std::array<float, 3>& root,
                        VisualPoseState& state, const char** failure = nullptr) noexcept;
bool anchor_visual_root_rotation(const std::array<float, 4>& rotation, double time,
                                 VisualPoseState& state, const char** failure = nullptr) noexcept;
bool sample_visual_pose(const VisualPoseIntent& intent,
                        const VisualPoseState& anchor,
                        const VisualRateValues& rates,
                        const VisualRouteSample& route, double time,
                        VisualPoseState& result, const char** failure = nullptr) noexcept;

// A UI view evaluates only the clip selected by the game. It never advances
// the gameplay queue or executes animation callbacks. Clock values use ANM
// phase units; the view owns the anchor while it is shown.
bool sample_visual_preview_phase(const VisualClipSelection& selection,
    double anchor_phase, double elapsed, double& phase) noexcept;

struct VisualNodeSample {
    std::array<float, 3> translation{};
    std::array<float, 4> rotation{1, 0, 0, 0};
    std::vector<std::array<float, 3>> morphs;
};

// Immutable model/clip linkage shared by pose consumers. Track pointers refer
// only to the pinned immutable clip; they never identify engine state.
struct VisualSkeletonBinding {
    std::shared_ptr<const ModelCpuAsset> model;
    std::shared_ptr<const AnimationDatabaseAsset> database;
    std::shared_ptr<const AnimationClipAsset> clip;
    std::vector<const AnimationTrackAsset*> tracks;
    std::vector<std::array<float,4>> constant_rotations;
    std::vector<bool> rotations_constant;
    std::vector<std::array<float,3>> translation_minimum, translation_maximum;
    std::vector<std::uint32_t> level_nodes;
    std::vector<std::uint32_t> level_offsets;
    std::uint32_t maximum_frame = 0;
    bool neutral = false;
    bool rigid = false;
};

struct VisualFramePhase {
    std::uint32_t frame = 0;
    float fraction = 0;
    bool operator==(const VisualFramePhase& other) const noexcept {
        return frame == other.frame && fraction == other.fraction;
    }
};
struct VisualNodeEnvelope {
    std::array<float,3> center{};
    float origin_radius = 0, rotation_scale = 1;
};
bool visual_skeleton_envelopes(const VisualSkeletonBinding& binding,
    const std::array<float,3>& shape,const std::array<float,3>& center_shift,
    std::vector<VisualNodeEnvelope>& result);

bool bind_visual_skeleton(std::shared_ptr<const ModelCpuAsset> model,
    const VisualClipSelection& selection, bool rigid, VisualSkeletonBinding& result,
    const char** failure = nullptr);
bool visual_skeleton_binding_matches(const VisualSkeletonBinding& binding,
    const ModelCpuAsset& model, const VisualClipSelection& selection, bool rigid) noexcept;
bool visual_frame_phase(double phase, std::uint32_t maximum_frame,
    VisualFramePhase& result) noexcept;

bool sample_visual_track(const AnimationTrackAsset* track, double phase,
                         VisualNodeSample& result, bool include_morphs = true);
bool sample_visual_skeleton(const ModelCpuAsset& model,
                            const VisualClipSelection& selection,
                            const std::array<float, 3>& shape,
                            const std::array<float, 3>& center_shift,
                            double phase,
                            std::vector<VisualNodeSample>& nodes,
                            bool include_morphs = true);
bool visual_morph_frames(const AnimationTrackAsset* track, double phase,
                         NativeMorphFramesView& result) noexcept;
// The ANM quaternion action preserves its supplied magnitude. This matrix is
// shared by both visual passes, including attachments and non-unit samples.
std::array<float, 3> visual_root_vector(const VisualPoseState& pose,
                                      const std::array<float, 3>& vector) noexcept;
bool visual_node_world_matrix(const VisualPoseState& pose,
                               const VisualNodeSample& node,
                               const std::array<float, 3>& scale,
                               std::array<float, 16>& result) noexcept;
// Compiled on asset/shape/topology change, never by walking triangles during
// visibility. Covers every phase, rotation and morph of the supplied clips.
bool visual_model_envelope(const ModelCpuAsset& model,
                           const std::vector<VisualClipSelection>& clips,
                           const std::array<float, 3>& shape,
                           const std::array<float, 3>& center_shift,
                           float& radius,
                           float* maximum_rotation_scale = nullptr) noexcept;

struct VisualBridgePlane {
    std::int32_t signed_id = 0;
    std::array<float, 3> center{};
    float radius = 0;
    std::array<float, 3> corner{};
    std::array<float, 3> first_axis{};
    std::array<float, 3> second_axis{};
    std::array<float, 3> normal{};
    float first_length = 0;
    float second_length = 0;
};

bool valid_visual_bridge(const VisualBridgePlane& bridge) noexcept;
bool sample_visual_bridge(const VisualBridgePlane& bridge, float x, float y,
                          float& height) noexcept;

} // namespace owned
