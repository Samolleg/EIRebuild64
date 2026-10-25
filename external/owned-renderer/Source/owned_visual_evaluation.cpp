#include "owned_visual_evaluation.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace owned {
namespace {
constexpr double kPi = 3.14159265358979323846;

template<std::size_t N> bool finite(const std::array<float, N>& values) noexcept {
    for (float value : values) if (!std::isfinite(value)) return false;
    return true;
}
template<std::size_t N> double length(const std::array<float, N>& values) noexcept {
    double square = 0;
    for (float value : values) square += static_cast<double>(value) * value;
    return std::sqrt(square);
}
double angle_delta(double angle) noexcept {
    return std::remainder(angle, 2 * kPi);
}
std::array<float, 2> direction(double angle) noexcept {
    return {static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle))};
}
const AnimationEntryAsset* entry(const VisualClipSelection& selection) noexcept {
    if (selection.neutral || !selection.database || !selection.clip ||
        selection.ordinal >= selection.database->entries.size()) return nullptr;
    return &selection.database->entries[selection.ordinal];
}
bool neutral_selection(const VisualClipSelection& selection) noexcept {
    return selection.neutral && !selection.database && !selection.clip && selection.ordinal == 0;
}
double height_scale(const AnimationDatabaseAsset& database, float tall) noexcept {
    const auto& h = database.heights;
    return h[1] != 0 ? (h[0] + static_cast<double>(tall) * (h[2] - h[0])) / h[1] : 1;
}
bool advance_queue(const VisualPoseIntent& intent, VisualPoseState& state, const char** failure) noexcept {
    if (!std::isfinite(state.phase) || state.phase < 0) return visual_failure(failure, "queue.phase");
    if (state.selection >= intent.selections.size()) return visual_failure(failure, "queue.selection");
    for (;;) {
        if (neutral_selection(intent.selections[state.selection])) {
            state.phase = 0;
            return true;
        }
        const auto* current = entry(intent.selections[state.selection]);
        if (!current) return visual_failure(failure, "queue.entry");
        if (!current->length) return visual_failure(failure, "queue.zero_length");
        if (state.phase <= current->length) return true;
        if (state.selection + 1 < intent.selections.size()) {
            state.phase -= current->length;
            ++state.selection;
        } else {
            if ((current->flags & 0xc0000000u) == 0x80000000u) {
                state.phase = std::fmod(state.phase, current->length);
                // The native transition condition is strict: a positive exact
                // multiple finishes at the last frame, not at frame zero.
                if (state.phase == 0) state.phase = current->length;
            } else state.phase = current->length;
            return true;
        }
    }
}
std::array<double, 8> shape_weights(const std::array<float, 3>& shape) noexcept {
    std::array<double, 8> weights{};
    for (std::size_t i = 0; i < weights.size(); ++i) {
        weights[i] = 1;
        for (std::size_t axis = 0; axis < 3; ++axis)
            weights[i] *= (i & (1u << axis)) ? shape[axis] : 1.0 - shape[axis];
    }
    return weights;
}
bool bone_position(const ModelNodeCpuAsset& node,
                   const std::array<double, 8>& weights,
                   std::array<float, 3>& bone) noexcept {
    const auto shapes = node.bone_positions.size()/3;
    if ((shapes != 1 && shapes != 8) || node.bone_positions.size() != shapes*3) return false;
    bone = {};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        double value = 0;
        for (std::size_t i = 0; i < shapes; ++i)
            value += node.bone_positions[i * 3u + axis] *
                (shapes == 1 ? 1 : weights[i]);
        bone[axis] = static_cast<float>(value);
    }
    return finite(bone);
}
std::array<float, 4> multiply(const std::array<float, 4>& a,
                             const std::array<float, 4>& b) noexcept {
    return {a[0]*b[0]-a[1]*b[1]-a[2]*b[2]-a[3]*b[3],
        a[0]*b[1]+a[1]*b[0]+a[2]*b[3]-a[3]*b[2],
        a[0]*b[2]-a[1]*b[3]+a[2]*b[0]+a[3]*b[1],
        a[0]*b[3]+a[1]*b[2]-a[2]*b[1]+a[3]*b[0]};
}
std::array<float, 3> rotate(const std::array<float, 4>& q,
                          const std::array<float, 3>& p) noexcept {
    const auto value = multiply(multiply(q, {0, p[0], p[1], p[2]}),
                                {q[0], -q[1], -q[2], -q[3]});
    return {value[1], value[2], value[3]};
}
void turn_tilted_root(VisualPoseState& state, const std::array<float, 2>& direction,
                      double elapsed) noexcept {
    if (elapsed <= 0) return;
    const auto up = rotate(state.root_rotation, {0, 0, 1});
    const auto forward = rotate(state.root_rotation, {0, -1, 0});
    const double up_length = length(up), forward_length = length(forward);
    if (up_length == 0 || forward_length == 0) return;
    std::array<double, 3> normal{}, target{}, axis{};
    for (unsigned i = 0; i < 3; ++i) normal[i] = up[i]/up_length;
    const double along_up = direction[0]*normal[0] + direction[1]*normal[1];
    double target_square = 0;
    for (unsigned i = 0; i < 3; ++i) {
        target[i] = (i < 2 ? direction[i] : 0) - normal[i]*along_up;
        target_square += target[i]*target[i];
    }
    // No projected heading supplies no turn; retain the last owned orientation.
    if (target_square == 0) return;
    const double target_length = std::sqrt(target_square);
    for (double& value : target) value /= target_length;
    double cosine = 0, axis_square = 0;
    for (unsigned i = 0; i < 3; ++i) {
        const auto j = (i+1)%3, k = (i+2)%3;
        axis[i] = (forward[j]*target[k] - forward[k]*target[j])/forward_length;
        axis_square += axis[i]*axis[i];
        cosine += forward[i]*target[i]/forward_length;
    }
    if (std::sqrt(axis_square) < 1.0e-5) return;
    // The supported mode turns 40% per update. The owned visual clock uses
    // one update per tick and a continuous fractional sample, never per view.
    const double angle = std::acos((std::clamp)(cosine, -1.0, 1.0)) *
        (1 - std::pow(0.6, elapsed));
    const double sine = std::sin(angle/2)/std::sqrt(axis_square);
    const std::array<float, 4> delta{static_cast<float>(std::cos(angle/2)),
        static_cast<float>(axis[0]*sine), static_cast<float>(axis[1]*sine),
        static_cast<float>(axis[2]*sine)};
    state.root_rotation = multiply(delta, state.root_rotation);
}
template<std::size_t N> std::array<float, N> interpolate(
    const std::vector<std::array<float, N>>& frames, double phase,
    std::array<float, N> empty = {}) noexcept {
    if (frames.empty()) return empty;
    if (phase >= frames.size() - 1) return frames.back();
    const auto index = static_cast<std::size_t>(phase);
    const double fraction = phase - index;
    std::array<float, N> result{};
    for (std::size_t axis = 0; axis < N; ++axis)
        result[axis] = static_cast<float>(frames[index][axis] +
            fraction * (frames[index + 1][axis] - frames[index][axis]));
    return result;
}
} // namespace

bool compile_visual_route(VisualRouteValues values, std::uint64_t generation,
                          CompiledVisualRoute& route) noexcept {
    route = {};
    if (!generation || values.kind > VisualRouteKind::attack_point ||
        !std::isfinite(values.start_tick) || !finite(values.start) ||
        !std::isfinite(values.start_angle) ||
        !std::isfinite(values.rotation_start_angle) ||
        !std::isfinite(values.rotation_delta) || !std::isfinite(values.angular_speed) ||
        !std::isfinite(values.hit_tick) || !std::isfinite(values.turn_finish_tick) ||
        values.points.size() > kVisualRoutePointLimit) return false;
    if (values.kind == VisualRouteKind::attack && values.turn_finish_tick < values.start_tick)
        return false;
    try {
        CompiledVisualRoute result{};
        result.generation = generation;
        if (values.rotation_delta != 0) {
            if (values.angular_speed <= 0) return false;
            result.rotation_ticks = std::fabs(
                static_cast<double>(values.rotation_delta) / values.angular_speed);
        }
        if (values.kind == VisualRouteKind::walk) {
            if (values.points.empty()) return false;
            double end = result.rotation_ticks;
            for (std::size_t i = 0; i < values.points.size(); ++i) {
                const auto& point = values.points[i];
                if (!finite(point.position) || !finite(point.tangent) ||
                    !std::isfinite(point.speed)) return false;
                if (i + 1 == values.points.size()) break;
                const auto& next = values.points[i + 1];
                const double distance = std::hypot(
                    static_cast<double>(next.position[0]) - point.position[0],
                    static_cast<double>(next.position[1]) - point.position[1]);
                if (!(distance > 0) || !(point.speed > 0)) return false;
                VisualRouteSegment segment{};
                segment.begin = end;
                end += distance / point.speed;
                if (!std::isfinite(end) || end <= segment.begin) return false;
                segment.end = end;
                segment.speed = point.speed;
                segment.chord_length = static_cast<float>(distance);
                for (std::size_t axis = 0; axis < 2; ++axis) {
                    const double a = point.position[axis], b = next.position[axis];
                    const double u = point.tangent[axis] * distance;
                    const double v = next.tangent[axis] * distance;
                    segment.polynomial[axis] = {a, u, 3*(b-a)-2*u-v, 2*(a-b)+u+v};
                }
                result.segments.push_back(segment);
            }
        }
        result.values = std::move(values);
        route = std::move(result);
        return true;
    } catch (...) { return false; }
}

bool sample_visual_route(const CompiledVisualRoute& route, double time,
                         VisualRouteSample& sample) noexcept {
    sample = {};
    if (!route.generation || !std::isfinite(time)) return false;
    const auto& value = route.values;
    const double elapsed = (std::max)(0.0, time - value.start_tick);
    sample.position = value.start;
    sample.direction = direction(value.start_angle);
    if (value.kind == VisualRouteKind::rotate ||
        value.kind == VisualRouteKind::attack_point ||
        (value.kind == VisualRouteKind::walk && elapsed < route.rotation_ticks)) {
        const double fraction = route.rotation_ticks > 0 ?
            (std::min)(1.0, elapsed / route.rotation_ticks) : 1;
        sample.direction = direction(value.rotation_start_angle + value.rotation_delta * fraction);
    } else if (value.kind == VisualRouteKind::walk) {
        if (value.points.empty()) return false;
        const auto segment = std::lower_bound(route.segments.begin(), route.segments.end(),
            elapsed, [](const VisualRouteSegment& item, double t) { return item.end < t; });
        if (segment == route.segments.end()) {
            sample.position = value.points.back().position;
            sample.direction = value.points.back().tangent;
            sample.speed = 0;
            sample.segment = route.segments.size();
        } else {
            sample.segment = static_cast<std::size_t>(segment - route.segments.begin());
            const double u = (elapsed - segment->begin) / (segment->end - segment->begin);
            for (std::size_t axis = 0; axis < 2; ++axis) {
                const auto& p = segment->polynomial[axis];
                sample.position[axis] = static_cast<float>(((p[3]*u+p[2])*u+p[1])*u+p[0]);
                sample.direction[axis] = static_cast<float>(
                    ((3*p[3]*u+2*p[2])*u+p[1]) / segment->chord_length);
            }
            sample.speed = segment->speed;
        }
    }
    return finite(sample.position) && finite(sample.direction) && std::isfinite(sample.speed);
}

float liquid_route_world_speed(const CompiledVisualRoute& route,double time,bool frozen,std::array<float,2>* direction) noexcept {
    if(direction)*direction={0,0};
    if(frozen||route.values.kind!=VisualRouteKind::walk)return 0;
    const double elapsed=time-route.values.start_tick;
    if(elapsed<route.rotation_ticks)return 0;
    const auto segment=std::lower_bound(route.segments.begin(),route.segments.end(),elapsed,
        [](const VisualRouteSegment& value,double t){return value.end<t;});
    if(segment==route.segments.end())return 0;
    const double duration=segment->end-segment->begin;
    if(!(duration>0))return 0;
    const double u=(elapsed-segment->begin)/duration;
    std::array<double,2> velocity{};
    for(unsigned axis=0;axis<2;++axis) {
        const auto& p=segment->polynomial[axis];
        velocity[axis]=((3*p[3]*u+2*p[2])*u+p[1])*.5/duration;
    }
    const double speed=std::hypot(velocity[0],velocity[1]);
    if(direction&&speed>1e-9)*direction={static_cast<float>(velocity[0]/speed),static_cast<float>(velocity[1]/speed)};
    return static_cast<float>(speed);
}

bool face_visual_target(const CompiledVisualRoute& route, double time, double start_tick,
                        const std::array<float, 2>* target,
                        VisualRouteSample& sample) noexcept {
    if (route.values.kind != VisualRouteKind::attack) return true;
    if (!std::isfinite(route.values.turn_finish_tick) ||
        route.values.turn_finish_tick < start_tick ||
        route.values.turn_finish_tick < route.values.start_tick ||
        !std::isfinite(time) || !std::isfinite(start_tick)) return false;
    time = (std::max)(time,start_tick);
    if (!target) { sample.direction = direction(route.values.start_angle); return true; }
    if (!finite(*target)) return false;
    const double dx = (*target)[0] - route.values.start[0];
    const double dy = (*target)[1] - route.values.start[1];
    const double finish = route.values.turn_finish_tick;
    const double begin = start_tick;
    if (time < finish && finish != begin) {
        const double delta = angle_delta(std::atan2(dy, dx) - route.values.start_angle);
        sample.direction = direction(route.values.start_angle + delta *
            (std::max)(0.0, time - begin) / (finish - begin));
    } else {
        const double distance = std::hypot(dx, dy);
        sample.direction = distance == 0 ? std::array<float, 2>{0, 0} :
            std::array<float, 2>{static_cast<float>(dx/distance), static_cast<float>(dy/distance)};
    }
    return true;
}

bool anchor_visual_pose(const VisualPoseIntent& intent,
                        const std::array<float, 3>& root,
                        VisualPoseState& state, const char** failure) noexcept {
    state = {};
    if (!intent.revision) return visual_failure(failure, "anchor.revision");
    if (intent.selections.empty() || intent.selections.size() > kVisualQueueLimit)
        return visual_failure(failure, "anchor.queue_size");
    if (!std::isfinite(intent.anchor_time)) return visual_failure(failure, "anchor.time");
    if (!std::isfinite(intent.facing)) return visual_failure(failure, "anchor.facing");
    if (!finite(root)) return visual_failure(failure, "anchor.root");
    for (const auto& selection : intent.selections) {
        if (neutral_selection(selection)) continue;
        if (!entry(selection)) return visual_failure(failure, "anchor.entry");
        if (!intent.resource_source_generation) return visual_failure(failure, "anchor.source_generation");
    }
    state.intent_revision = intent.revision;
    state.time = intent.anchor_time;
    state.facing_time = intent.anchor_time;
    state.phase = intent.phase;
    state.facing = intent.facing;
    state.root = root;
    return advance_queue(intent, state, failure);
}

bool anchor_visual_root_rotation(const std::array<float, 4>& rotation, double time,
                                 VisualPoseState& state, const char** failure) noexcept {
    const double magnitude = length(rotation);
    if (!finite(rotation) || !std::isfinite(magnitude) || magnitude == 0 ||
        !std::isfinite(time)) return visual_failure(failure, "placement.rotation");
    state.root_rotation = rotation;
    state.root_rotation_valid = true;
    // Explicit placement starts a new facing interval, including while paused.
    // Planar units use the same authored forward axis as tilted units.
    if (!state.tilted_facing) {
        const auto forward = rotate(rotation, {0, -1, 0});
        state.facing = std::atan2(forward[1], forward[0]);
    }
    state.facing_time = time;
    return true;
}

bool sample_visual_pose(const VisualPoseIntent& intent,
                        const VisualPoseState& anchor,
                        const VisualRateValues& rates,
                        const VisualRouteSample& route, double time,
                        VisualPoseState& result, const char** failure) noexcept {
    result = {};
    if (anchor.intent_revision != intent.revision) return visual_failure(failure, "pose.intent_revision");
    if (!std::isfinite(time)) return visual_failure(failure, "pose.time");
    if (!std::isfinite(anchor.time)) return visual_failure(failure, "pose.anchor_time");
    if (anchor.selection >= intent.selections.size()) return visual_failure(failure, "pose.selection");
    if (!std::isfinite(rates.coefficient) || rates.coefficient < 0)
        return visual_failure(failure, "pose.coefficient");
    if (!std::isfinite(rates.tall)) return visual_failure(failure, "pose.tall");
    if (!finite(route.position)) return visual_failure(failure, "pose.route_position");
    if (!finite(route.direction)) return visual_failure(failure, "pose.route_direction");
    if (!std::isfinite(route.speed) || route.speed < 0) return visual_failure(failure, "pose.route_speed");
    const auto& selection = intent.selections[anchor.selection];
    const auto* current = entry(selection);
    const bool neutral = neutral_selection(selection);
    if (!current && !neutral) return visual_failure(failure, "pose.entry");
    result = anchor;
    result.tilted_facing = rates.tilted_facing;
    if (rates.tilted_facing && !anchor.root_rotation_valid)
        return visual_failure(failure, "pose.root_rotation");
    // A newly received selection may begin after this tick. Its initial phase
    // is held until the authoritative anchor, rather than stepped a second
    // time or misclassified as a clock rewind. The scene owns rewind detection.
    const double elapsed = (std::max)(0.0, time - anchor.time);
    if (!std::isfinite(anchor.facing_time)) return visual_failure(failure, "pose.facing_time");
    const double facing_elapsed = (std::max)(0.0, time - anchor.facing_time);
    double direction_length = length(route.direction);
    if (!rates.dont_move && !rates.movement_frozen) {
        // Route positions are AI tiles; the supported placement conversion is
        // one half metre per tile. Z comes from the owned height publication.
        result.root[0] = route.position[0] * 0.5f;
        result.root[1] = route.position[1] * 0.5f;
        const double target = std::atan2(route.direction[1], route.direction[0]);
        if (rates.tilted_facing) turn_tilted_root(result, route.direction, facing_elapsed);
        else if (facing_elapsed > 0) {
            result.facing = static_cast<float>(anchor.facing +
                angle_delta(target - anchor.facing) * (1 - std::exp(-0.30000001192092896 * facing_elapsed)));
            direction_length = 1;
        }
    }
    if (rates.tilted_facing) {
        if (!finite(result.root_rotation)) return visual_failure(failure, "pose.result_rotation");
        const auto forward = rotate(result.root_rotation, {0, -1, 0});
        result.facing = std::atan2(forward[1], forward[0]);
    }
    double coefficient = rates.scaled_animation ? rates.coefficient : 1;
    if (rates.movement_frozen) coefficient = 1;
    else if (rates.scaled_animation && !rates.dont_move && current && current->speed != 0) {
        const double clip_speed = current->speed * height_scale(*selection.database, rates.tall) * 2;
        if (!std::isfinite(clip_speed) || clip_speed <= 0) return visual_failure(failure, "pose.clip_speed");
        coefficient = 15 * route.speed * direction_length / clip_speed;
    }
    if (!rates.frozen && elapsed > 0) {
        if (neutral) {
            // SetTime with no current entry admits the queued head at zero,
            // discarding that step's elapsed phase. The append producer anchors
            // its time, so earlier/repeated samples cannot activate it.
            result.phase = 0;
            if (result.selection + 1 < intent.selections.size()) ++result.selection;
        } else result.phase += elapsed * coefficient;
    }
    result.time = (std::max)(time, anchor.time);
    result.facing_time = (std::max)(time, anchor.facing_time);
    if (!finite(result.root)) return visual_failure(failure, "pose.result_root");
    if (!std::isfinite(result.facing)) return visual_failure(failure, "pose.result_facing");
    return advance_queue(intent, result, failure);
}

bool sample_visual_preview_phase(const VisualClipSelection& selection,
    double anchor_phase, double elapsed, double& phase) noexcept {
    phase = 0;
    if (!std::isfinite(anchor_phase) || anchor_phase < 0 ||
        !std::isfinite(elapsed) || elapsed < 0) return false;
    if (neutral_selection(selection)) return true;
    const auto* clip = entry(selection);
    if (!clip || !clip->length) return false;
    phase = anchor_phase + elapsed;
    if (!std::isfinite(phase)) return false;
    if (phase <= clip->length) return true;
    if ((clip->flags & 0xc0000000u) == 0x80000000u) {
        phase = std::fmod(phase, clip->length);
        if (phase == 0) phase = clip->length;
    } else phase = clip->length;
    return true;
}

VisualClipVisibility sample_visual_visibility(const AnimationEntryAsset& entry,
    double phase, bool dead,
    const VisualEquipmentValues& equipment) noexcept {
    return sample_visual_visibility(&entry,phase,dead,equipment,false);
}

VisualClipVisibility sample_visual_visibility(const AnimationEntryAsset* entry,
    double phase, bool dead,
    const VisualEquipmentValues& equipment, bool previous_weapon_hidden) noexcept {
    VisualClipVisibility result{};
    const bool clip_hides_weapons = entry && (entry->flags & 0x7fffu) == 0;
    result.hide_weapons = equipment.forced_hidden ||
        (entry ? ((!equipment.has_weapon || equipment.weapon_is_bow || equipment.speaking) &&
         clip_hides_weapons) : previous_weapon_hidden);
    if (equipment.human && equipment.arrow_is_bow) {
        result.hide_arrow = true;
        if (entry && (entry->special_flags & 1u) && std::isfinite(phase) && phase >= 0) {
            const double show = entry->hide_frames[0], hide = entry->hide_frames[1];
            // Equal events retain the original unhide-first precedence. A
            // wrapped window is evaluated from phase, without a camera history.
            const bool in_window = show <= hide ? phase >= show && phase < hide :
                phase >= show || phase < hide;
            result.hide_arrow = !(in_window || (show == hide && phase >= show));
        }
        result.hide_arrow = result.hide_arrow || result.hide_weapons;
    }
    // Current posed units cast without the legacy lying-animation veto.
    // Visible corpses keep casting for their authoritative body lifetime;
    // part visibility, material alpha and shadow eligibility still gate submission.
    result.casts_shadow = dead || entry != nullptr;
    return result;
}

bool sample_visual_effects(const VisualEffectState& anchor, bool invisible,
                          bool stunned, float fade_alpha, double time,
                          VisualEffectState& result, const char** failure) noexcept {
    result = {};
    if (!std::isfinite(anchor.phase) || anchor.phase < 0) return visual_failure(failure, "effects.phase");
    if (!std::isfinite(anchor.time)) return visual_failure(failure, "effects.anchor_time");
    if (!std::isfinite(time)) return visual_failure(failure, "effects.time");
    if (time < anchor.time) return visual_failure(failure, "effects.before_anchor");
    if (!std::isfinite(fade_alpha) || fade_alpha < 0 || fade_alpha > 1)
        return visual_failure(failure, "effects.fade");
    result = anchor;
    result.opacity = fade_alpha;
    result.stun_red = 0;
    const double elapsed = time-anchor.time;
    const auto pulse = [](double phase) noexcept {
        const double part = std::fmod(phase,30);
        return static_cast<float>(0.2+0.5*(part > 15 ? 30-part : part)/15);
    };
    // Both effects advance the same native phase, in this order. The owned
    // state commits once per world tick; fractional reads never accumulate.
    if (invisible) { result.phase += elapsed; result.opacity = pulse(result.phase); }
    if (stunned) { result.phase += elapsed; result.stun_red = pulse(result.phase); }
    result.time = time;
    return std::isfinite(result.phase) || visual_failure(failure, "effects.phase_overflow");
}

bool sample_visual_track(const AnimationTrackAsset* track, double phase,
                         VisualNodeSample& result, bool include_morphs) {
    result = {};
    if (!std::isfinite(phase) || phase < 0) return false;
    if (!track) return true; // A missing named track has the supported cleared pose.
    result.translation = interpolate(track->translations, phase);
    result.rotation = interpolate(track->rotations, phase, {1, 0, 0, 0});
    if (track->rotations.size() > 1 && phase < track->rotations.size() - 1) {
        const auto index = static_cast<std::size_t>(phase);
        const auto& a = track->rotations[index];
        auto b = track->rotations[index + 1];
        const double fraction = phase - index;
        double dot = 0;
        for (std::size_t i = 0; i < 4; ++i) dot += static_cast<double>(a[i]) * b[i];
        if (dot < 0) { for (float& value : b) value = -value; dot = -dot; }
        double left = 1-fraction, right = fraction;
        if (1.0-dot > 1e-4) {
            const double angle = std::acos(dot);
            const double sine = std::sin(angle);
            left = std::sin((1-fraction)*angle)/sine;
            right = std::sin(fraction*angle)/sine;
        }
        for (std::size_t i = 0; i < 4; ++i)
            result.rotation[i] = static_cast<float>(a[i]*left + b[i]*right);
    }
    if (include_morphs && track->morph_frames) {
        if (static_cast<std::uint64_t>(track->morph_frames) * track->morph_points != track->morphs.size())
            return false;
        const double t = (std::min)(phase, static_cast<double>(track->morph_frames - 1));
        const auto first = static_cast<std::size_t>(t);
        const auto last = (std::min)(first + 1, static_cast<std::size_t>(track->morph_frames - 1));
        result.morphs.resize(track->morph_points);
        for (std::size_t i = 0; i < result.morphs.size(); ++i)
            for (std::size_t axis = 0; axis < 3; ++axis) {
                const float a = track->morphs[first * track->morph_points + i][axis];
                const float b = track->morphs[last * track->morph_points + i][axis];
                result.morphs[i][axis] = static_cast<float>(a + (t-first)*(b-a));
            }
    }
    return finite(result.translation) && finite(result.rotation);
}

bool visual_skeleton_binding_matches(const VisualSkeletonBinding& binding,
    const ModelCpuAsset& model,const VisualClipSelection& selection,bool rigid) noexcept {
    const auto same=[](const ResourceIdentity& a,const ResourceIdentity& b) {
        return a.container_incarnation==b.container_incarnation && a.absolute_offset==b.absolute_offset &&
            a.bytes==b.bytes && a.canonical_name==b.canonical_name;
    };
    if (!binding.model || binding.rigid!=rigid ||
        !same(binding.model->model_container,model.model_container) ||
        !same(binding.model->bone_container,model.bone_container)) return false;
    if (rigid) return true;
    if (binding.neutral!=neutral_selection(selection)) return false;
    if (binding.neutral) return true;
    return entry(selection) && binding.clip && binding.database &&
        same(binding.clip->identity,selection.clip->identity) &&
        same(binding.database->identity,selection.database->identity);
}

bool bind_visual_skeleton(std::shared_ptr<const ModelCpuAsset> model,
    const VisualClipSelection& selection,bool rigid,VisualSkeletonBinding& result,const char** failure) {
    result={};
    if (!model || model->nodes.size()!=model->links.size() || model->nodes.empty() ||
        model->nodes.size()>kVisualPartLimit) return visual_failure(failure,"skeleton.model");
    const bool neutral=!rigid && neutral_selection(selection);
    if (!rigid && !neutral && !entry(selection)) return visual_failure(failure,"skeleton.selection");
    VisualSkeletonBinding next;
    next.model=std::move(model);next.rigid=rigid;next.neutral=neutral;
    if (!rigid && !neutral) {next.clip=selection.clip;next.database=selection.database;}
    const auto count=next.model->nodes.size();
    std::vector<std::uint32_t> levels(count);
    next.tracks.resize(count);
    next.constant_rotations.assign(count,{1,0,0,0});next.rotations_constant.assign(count,true);
    next.translation_minimum.resize(count);next.translation_maximum.resize(count);
    std::uint32_t level_count=1;
    for (std::size_t i=0;i<count;++i) {
        const auto& node=next.model->nodes[i];
        if (!node.transform_available) continue;
        if (node.bone_positions.size()!=3 && node.bone_positions.size()!=24)
            return visual_failure(failure,"skeleton.bones");
        const auto parent=next.model->links[i].parent_index;
        if (parent!=UINT_MAX) {
            if (parent>=i) return visual_failure(failure,"skeleton.parent");
            levels[i]=levels[parent]+1;
            level_count=(std::max)(level_count,levels[i]+1);
        }
        if (next.clip) {
            const auto found=next.clip->tracks.find(node.name);
            if (found==next.clip->tracks.end()) continue;
            const auto& track=found->second;next.tracks[i]=&track;
            if (!track.rotations.empty()) {
                next.constant_rotations[i]=track.rotations.front();
                double norm=0;for (float value:track.rotations.front()) norm+=double(value)*value;
                // Equal raw quaternions below unit magnitude still vary in
                // length under the supported slerp, so they are not fixed.
                next.rotations_constant[i]=track.rotations.size()==1 || (norm>=1 &&
                    std::all_of(track.rotations.begin(),track.rotations.end(),[&](const auto& value){return value==track.rotations.front();}));
            }
            if (!track.translations.empty()) {
                next.translation_minimum[i]=next.translation_maximum[i]=track.translations.front();
                for (const auto& translation:track.translations) for (unsigned axis=0;axis<3;++axis) {
                    next.translation_minimum[i][axis]=(std::min)(next.translation_minimum[i][axis],translation[axis]);
                    next.translation_maximum[i][axis]=(std::max)(next.translation_maximum[i][axis],translation[axis]);
                }
            }
            const std::uint64_t frames=(std::max)({std::uint64_t(track.rotations.size()),
                std::uint64_t(track.translations.size()),std::uint64_t(track.morph_frames)});
            if (frames>UINT_MAX) return visual_failure(failure,"skeleton.frames");
            if (frames) next.maximum_frame=(std::max)(next.maximum_frame,static_cast<std::uint32_t>(frames-1));
        }
    }
    next.level_offsets.assign(level_count+1,0);
    for (std::size_t i=0;i<count;++i)
        if (next.model->nodes[i].transform_available) ++next.level_offsets[levels[i]+1];
    for (std::uint32_t level=1;level<=level_count;++level)
        next.level_offsets[level]+=next.level_offsets[level-1];
    auto cursors=next.level_offsets;next.level_nodes.resize(next.level_offsets.back());
    for (std::uint32_t i=0;i<count;++i)
        if (next.model->nodes[i].transform_available) next.level_nodes[cursors[levels[i]]++]=i;
    result=std::move(next);
    return true;
}

bool visual_skeleton_envelopes(const VisualSkeletonBinding& binding,
    const std::array<float,3>& shape,const std::array<float,3>& center_shift,
    std::vector<VisualNodeEnvelope>& result) {
    result.clear();
    if (!binding.model || binding.tracks.size()!=binding.model->nodes.size() ||
        !finite(shape) || !finite(center_shift)) return false;
    const auto weights=shape_weights(shape);
    const double height=binding.database?height_scale(*binding.database,shape[2]):1;
    result.resize(binding.model->nodes.size());
    std::vector<std::array<float,4>> rotations(result.size(),{1,0,0,0});
    std::vector<bool> constant(result.size(),true);
    for (std::size_t i=0;i<result.size();++i) {
        if (!binding.model->nodes[i].transform_available) continue;
        const auto* track=binding.tracks[i];const auto parent=binding.model->links[i].parent_index;
        std::array<float,3> bone{};
        if (!bone_position(binding.model->nodes[i],weights,bone)) return false;
        auto& output=result[i];
        if (binding.rigid) {
            output.center=bone;
            if (parent!=UINT_MAX) for (unsigned axis=0;axis<3;++axis) output.center[axis]+=result[parent].center[axis];
        } else {
            double origin=0;
            if (parent==UINT_MAX) {
                double squared=0;
                for (unsigned axis=0;axis<3;++axis) {
                    const double lower=binding.translation_minimum[i][axis],upper=binding.translation_maximum[i][axis];
                    output.center[axis]=static_cast<float>((lower+upper)*.5*height-center_shift[axis]);
                    const double half=(upper-lower)*.5*height;squared+=half*half;
                }
                origin=std::sqrt(squared);
            } else {
                output.center=result[parent].center;origin=result[parent].origin_radius;
                if (constant[parent]) {
                    const auto offset=rotate(rotations[parent],bone);
                    for (unsigned axis=0;axis<3;++axis) output.center[axis]+=offset[axis];
                } else origin+=result[parent].rotation_scale*length(bone);
            }
            const double rotation=(track?track->rotation_scale_bound:1)*(parent==UINT_MAX?1:result[parent].rotation_scale);
            output.origin_radius=std::nextafter(static_cast<float>(origin),INFINITY);
            output.rotation_scale=std::nextafter(static_cast<float>(rotation),INFINITY);
            constant[i]=binding.rotations_constant[i]&&(parent==UINT_MAX||constant[parent]);
            if (constant[i]) rotations[i]=parent==UINT_MAX?binding.constant_rotations[i]:multiply(binding.constant_rotations[i],rotations[parent]);
        }
        if (!finite(output.center)||!std::isfinite(output.origin_radius)||!std::isfinite(output.rotation_scale)) return false;
    }
    return true;
}

bool visual_frame_phase(double phase,std::uint32_t maximum_frame,VisualFramePhase& result) noexcept {
    result={};
    if (!std::isfinite(phase) || phase<0) return false;
    const double bounded=(std::min)(phase,double(maximum_frame));
    result.frame=static_cast<std::uint32_t>(bounded);
    result.fraction=static_cast<float>(bounded-result.frame);
    return true;
}

bool sample_visual_skeleton(const ModelCpuAsset& model,
                            const VisualClipSelection& selection,
                            const std::array<float, 3>& shape,
                            const std::array<float, 3>& center_shift,
                            double phase, std::vector<VisualNodeSample>& nodes,
                            bool include_morphs) {
    nodes.clear();
    const bool neutral = neutral_selection(selection);
    if ((!entry(selection) && !neutral) || !finite(shape) || !finite(center_shift) ||
        model.nodes.size() != model.links.size() || model.nodes.size() > kVisualPartLimit)
        return false;
    const auto weights = shape_weights(shape);
    const double scale = neutral ? 1 : height_scale(*selection.database, shape[2]);
    // Preserve LNK indices; unavailable slots are never sampled or admitted
    // as parts, and supported children cannot depend on them.
    nodes.resize(model.nodes.size());
    for (std::size_t i = 0; i < model.nodes.size(); ++i) {
        const auto& node = model.nodes[i];
        if (!node.transform_available) continue;
        const AnimationTrackAsset* track = nullptr;
        if (!neutral) {
            const auto found = selection.clip->tracks.find(node.name);
            if (found != selection.clip->tracks.end()) track = &found->second;
        }
        VisualNodeSample value{};
        if (!sample_visual_track(track,phase,value,include_morphs)) return false;
        const auto parent = model.links[i].parent_index;
        if (parent == UINT_MAX) {
            for (std::size_t axis = 0; axis < 3; ++axis)
                value.translation[axis] = static_cast<float>(value.translation[axis]*scale - center_shift[axis]);
        } else {
            if (parent >= i) return false;
            std::array<float, 3> bone{};
            if (!bone_position(node, weights, bone)) return false;
            const auto offset = rotate(nodes[parent].rotation, bone);
            for (std::size_t axis = 0; axis < 3; ++axis)
                value.translation[axis] = nodes[parent].translation[axis] + offset[axis];
            // The supported node hierarchy composes local rotation on the left.
            value.rotation = multiply(value.rotation, nodes[parent].rotation);
        }
        if (!finite(value.translation) || !finite(value.rotation)) return false;
        nodes[i] = std::move(value);
    }
    return true;
}

std::array<float, 3> visual_root_vector(const VisualPoseState& pose,
                                      const std::array<float, 3>& vector) noexcept {
    if (pose.tilted_facing) return rotate(pose.root_rotation, vector);
    const float c = -std::sin(pose.facing), s = std::cos(pose.facing);
    return {c*vector[0]-s*vector[1], s*vector[0]+c*vector[1], vector[2]};
}

bool visual_node_world_matrix(const VisualPoseState& pose,
                               const VisualNodeSample& node,
                               const std::array<float, 3>& scale,
                               std::array<float, 16>& result) noexcept {
    result = {};
    if (!finite(pose.root) || !std::isfinite(pose.facing) ||
        !finite(node.translation) || !finite(node.rotation) || !finite(scale) ||
        (pose.tilted_facing && (!pose.root_rotation_valid || !finite(pose.root_rotation)))) return false;
    // Facing names forward (-model Y), as established by the supported
    // orientation setter. The matrix rotates the model's X/Y axes instead.
    for (std::size_t column = 0; column < 3; ++column) {
        std::array<float, 3> axis{};
        axis[column] = 1;
        auto rotated = rotate(node.rotation, axis);
        for (std::size_t i = 0; i < 3; ++i) rotated[i] *= scale[i];
        rotated = visual_root_vector(pose, rotated);
        for (unsigned row = 0; row < 3; ++row) result[row*4+column] = rotated[row];
    }
    const auto offset = visual_root_vector(pose, {node.translation[0]*scale[0],
        node.translation[1]*scale[1], node.translation[2]*scale[2]});
    for (unsigned row = 0; row < 3; ++row) result[row*4+3] = pose.root[row]+offset[row];
    result[15] = 1;
    return finite(result);
}

bool visual_model_envelope(const ModelCpuAsset& model,
                           const std::vector<VisualClipSelection>& clips,
                           const std::array<float, 3>& shape,
                           const std::array<float, 3>& center_shift,
                           float& radius, float* maximum_rotation_scale) noexcept {
    radius = 0;
    if (maximum_rotation_scale) *maximum_rotation_scale = 1;
    if (!finite(shape) || !finite(center_shift) || model.nodes.size() != model.links.size() ||
        model.nodes.size() > kVisualPartLimit || clips.size() > kVisualQueueLimit) return false;
    try {
        const auto weights = shape_weights(shape);
        std::vector<double> origin_radii(model.nodes.size());
        std::vector<double> rotation_scales(model.nodes.size());
        for (std::size_t i = 0; i < model.nodes.size(); ++i) {
            const auto& node = model.nodes[i];
            if (!node.transform_available) continue;
            const auto& figure = node.figure;
            std::array<float, 3> bone{};
            if (!bone_position(node, weights, bone) ||
                figure.positions.size() != static_cast<std::uint64_t>(figure.position_groups)*figure.shape_count)
                return false;
            double translation_radius = 0, morph_radius = 0, rotation_scale = 1;
            for (const auto& clip : clips) {
                if (neutral_selection(clip)) continue;
                if (!entry(clip)) return false;
                const auto found = clip.clip->tracks.find(node.name);
                if (found == clip.clip->tracks.end()) continue;
                const auto& track = found->second;
                translation_radius = (std::max)(translation_radius,
                    track.translation_radius * std::fabs(height_scale(*clip.database, shape[2])));
                morph_radius = (std::max)(morph_radius, static_cast<double>(track.morph_radius));
                rotation_scale = (std::max)(rotation_scale,
                    static_cast<double>(track.rotation_scale_bound));
            }
            const auto parent = model.links[i].parent_index;
            if (parent != UINT_MAX && parent >= i) return false;
            origin_radii[i] = parent == UINT_MAX ? translation_radius + length(center_shift) :
                origin_radii[parent] + rotation_scales[parent]*length(bone);
            rotation_scales[i] = rotation_scale * (parent == UINT_MAX ? 1 : rotation_scales[parent]);
            double geometry_radius = 0;
            for (std::size_t s = 0; s < figure.shape_count; ++s)
                geometry_radius += figure.shape_position_radii[s] *
                    (figure.shape_count == 1 ? 1 : std::fabs(weights[s]));
            const double complete = origin_radii[i] + rotation_scales[i]*(geometry_radius + morph_radius);
            if (!std::isfinite(complete) || complete > (std::numeric_limits<float>::max)()) return false;
            radius = (std::max)(radius, static_cast<float>(complete));
            if (maximum_rotation_scale) *maximum_rotation_scale = (std::max)(
                *maximum_rotation_scale,
                std::nextafter(static_cast<float>(rotation_scales[i]), INFINITY));
        }
        radius = std::nextafter(radius, (std::numeric_limits<float>::infinity)());
        return std::isfinite(radius);
    } catch (...) { return false; }
}

bool valid_visual_bridge(const VisualBridgePlane& bridge) noexcept {
    return finite(bridge.center) && finite(bridge.corner) && finite(bridge.first_axis) &&
        finite(bridge.second_axis) && finite(bridge.normal) && std::isfinite(bridge.radius) &&
        bridge.radius >= 0 && std::isfinite(bridge.first_length) && bridge.first_length >= 0 &&
        std::isfinite(bridge.second_length) && bridge.second_length >= 0 && bridge.normal[2] != 0;
}

bool sample_visual_bridge(const VisualBridgePlane& bridge, float x, float y,
                          float& height) noexcept {
    if (!std::isfinite(x) || !std::isfinite(y)) return false;
    const double dx = static_cast<double>(x) - bridge.center[0];
    const double dy = static_cast<double>(y) - bridge.center[1];
    if (dx*dx+dy*dy > static_cast<double>(bridge.radius)*bridge.radius) return false;
    const double px = x - bridge.corner[0], py = y - bridge.corner[1];
    const double pz = -(px*bridge.normal[0]+py*bridge.normal[1])/bridge.normal[2];
    const double u = px*bridge.first_axis[0]+py*bridge.first_axis[1]+pz*bridge.first_axis[2];
    const double v = px*bridge.second_axis[0]+py*bridge.second_axis[1]+pz*bridge.second_axis[2];
    // Supported height publication admits the half-unit border independently
    // of the tighter gameplay bridge-presence predicate.
    if (u < -0.5 || v < -0.5 || u > bridge.first_length+0.5 || v > bridge.second_length+0.5)
        return false;
    height = static_cast<float>(bridge.corner[2]+u*bridge.first_axis[2]+v*bridge.second_axis[2]);
    return std::isfinite(height);
}
} // namespace owned
