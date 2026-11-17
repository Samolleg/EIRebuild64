// Retained-scene publication stream (retained_publication.h): delta value
// codecs and the recorder's message emission. Built into the DLL and the
// x64 host.
#include "retained_publication.h"

#include "owned_terrain_front_end.h"
#include "retained_static_scene.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>

namespace owned {

using codec::io;

namespace {

void io_resource(RetainedDeltaOut& s, const VisualResourceIncarnation& v) { io(s.out, v.signed_id); io(s.out, v.serial); }
void io_resource(RetainedDeltaIn& s, VisualResourceIncarnation& v) { io(s.in, v.signed_id); io(s.in, v.serial); }

template<class S, class R, class E> void tick_fields(S& s, R& rate, E& equipment) {
    io(s, rate.coefficient); io(s, rate.tall); io(s, rate.frozen); io(s, rate.dont_move);
    io(s, rate.movement_frozen); io(s, rate.tilted_facing); io(s, rate.scaled_animation);
    io(s, rate.action_coefficient);
    io(s, equipment.has_weapon); io(s, equipment.weapon_is_bow); io(s, equipment.arrow_is_bow);
    io(s, equipment.human); io(s, equipment.speaking); io(s, equipment.forced_hidden);
}
template<class S, class T> void tick_values(S& s, T& v) {
    io(s, v.object_flags); io(s, v.unit); io(s, v.timed_highlight_active); io(s, v.highlight);
    io(s, v.target_half_extent); tick_fields(s, v.rate, v.equipment); io(s, v.route_start_tick);
    io(s, v.base_z); io(s, v.fade_alpha); io(s, v.locomotion); io(s, v.start_animation); io(s, v.visibility);
    io(s, v.loudness); io(s, v.pose); io(s, v.moving); io(s, v.blood); io(s, v.undead_prototype);
    io(s, v.vision); io(s, v.controlled); io(s, v.dead); io(s, v.hidden); io(s, v.invisible); io(s, v.stunned);
}
template<class S, class T> void route_fields(S& s, T& v) {
    auto& r = v.values;
    io(s, r.kind); io(s, r.start_tick); io(s, r.start); io(s, r.start_angle); io(s, r.rotation_start_angle);
    io(s, r.rotation_delta); io(s, r.angular_speed); io(s, r.target_id); io(s, r.hit_tick);
    io(s, r.turn_finish_tick); io(s, r.points); io(s, v.generation); io(s, v.rotation_ticks);
}
template<class S, class T> void segment_fields(S& s, T& v) {
    io(s, v.begin); io(s, v.end); io(s, v.polynomial); io(s, v.speed); io(s, v.chord_length);
}
template<class S, class T> void pose_scalars(S& s, T& v) {
    io(s, v.position); io(s, v.quaternion); io(s, v.shape); io(s, v.object_flags);
}
template<class S, class T> void pose_tail(S& s, T& v) {
    io(s, v.phase); io(s, v.source_generation); io(s, v.appearance_revision);
}
template<class S, class T> void light_fields(S& s, T& v) {
    io(s, v.source_id); io(s, v.center); io(s, v.color); io(s, v.radius); io(s, v.casts_shadow);
    io(s, v.shadow_fade); io(s, v.attached); io(s, v.hero); io(s, v.spell);
}
template<class S, class T> void geometry_fields(S& s, T& v) {
    io(s, v.revision); io(s, v.structural_revision); io(s, v.noise_revision); io(s, v.layer_shifts);
    io(s, v.layer_shift_range); io(s, v.wind_noise); io(s, v.animation);
}
} // namespace

void delta_io(RetainedDeltaOut& s, const VisualClipSelection& v) {
    s.ref(v.database); s.ref(v.clip); io(s.out, v.ordinal); io(s.out, v.neutral);
}
void delta_io(RetainedDeltaIn& s, VisualClipSelection& v) {
    s.ref(v.database); s.ref(v.clip); io(s.in, v.ordinal); io(s.in, v.neutral);
}
void delta_io(RetainedDeltaOut& s, const VisualPoseIntent& v) {
    io(s.out, v.revision); io(s.out, v.resource_source_generation);
    codec::io_count(s.out, v.selections.size());
    for (const auto& selection : v.selections) delta_io(s, selection);
    io(s.out, v.anchor_time); io(s.out, v.phase); io(s.out, v.facing);
}
void delta_io(RetainedDeltaIn& s, VisualPoseIntent& v) {
    io(s.in, v.revision); io(s.in, v.resource_source_generation);
    v.selections.resize(s.in.count(1));
    for (auto& selection : v.selections) { delta_io(s, selection); if (!s.in.ok()) return; }
    io(s.in, v.anchor_time); io(s.in, v.phase); io(s.in, v.facing);
}
void delta_io(RetainedDeltaOut& s, const VisualObjectPose& v) {
    pose_scalars(s.out, v); delta_io(s, v.selection); pose_tail(s.out, v);
}
void delta_io(RetainedDeltaIn& s, VisualObjectPose& v) {
    pose_scalars(s.in, v); delta_io(s, v.selection); pose_tail(s.in, v);
}
void delta_io(RetainedDeltaOut& s, const VisualObjectPublicationSnapshot& v) {
    io_resource(s, v.resource); io(s.out, v.resolver_generation); io(s.out, v.assembly_revision);
    s.ref(v.database); delta_io(s, v.prior_pose); io(s.out, v.has_prior_pose);
}
void delta_io(RetainedDeltaIn& s, VisualObjectPublicationSnapshot& v) {
    io_resource(s, v.resource); io(s.in, v.resolver_generation); io(s.in, v.assembly_revision);
    s.ref(v.database); delta_io(s, v.prior_pose); io(s.in, v.has_prior_pose);
}
void delta_io(RetainedDeltaOut& s, const CompiledVisualRoute& v) {
    route_fields(s.out, v);
    codec::io_count(s.out, v.segments.size());
    for (const auto& segment : v.segments) segment_fields(s.out, segment);
}
void delta_io(RetainedDeltaIn& s, CompiledVisualRoute& v) {
    route_fields(s.in, v);
    v.segments.resize(s.in.count(1));
    for (auto& segment : v.segments) segment_fields(s.in, segment);
}
void delta_io(RetainedDeltaOut& s, const VisualTickValues& v) { tick_values(s.out, v); }
void delta_io(RetainedDeltaIn& s, VisualTickValues& v) { tick_values(s.in, v); }
void delta_io(RetainedDeltaOut& s, const RetainedHighlightState& v) {
    io(s.out, v.show_selected); io(s.out, v.strip_preselected); io(s.out, v.hover_characters);
    io(s.out, v.hover_objects); io_resource(s, v.hover);
}
void delta_io(RetainedDeltaIn& s, RetainedHighlightState& v) {
    io(s.in, v.show_selected); io(s.in, v.strip_preselected); io(s.in, v.hover_characters);
    io(s.in, v.hover_objects); io_resource(s, v.hover);
}
void delta_io(RetainedDeltaOut& s, const std::vector<RetainedWorldPointLight>& v) {
    codec::io_count(s.out, v.size());
    for (const auto& light : v) light_fields(s.out, light);
}
void delta_io(RetainedDeltaIn& s, std::vector<RetainedWorldPointLight>& v) {
    v.resize(s.in.count(1));
    for (auto& light : v) light_fields(s.in, light);
}
void delta_io(RetainedDeltaOut& s, const std::vector<RetainedFireSpell>& v) {
    codec::io_count(s.out, v.size());
    for (const auto& f : v) { io(s.out, f.kind); io(s.out, f.center); io(s.out, f.wall); io(s.out, f.radius); }
}
void delta_io(RetainedDeltaIn& s, std::vector<RetainedFireSpell>& v) {
    v.resize(s.in.count(1));
    for (auto& f : v) { io(s.in, f.kind); io(s.in, f.center); io(s.in, f.wall); io(s.in, f.radius); }
}
void delta_io(RetainedDeltaOut& s, const std::vector<RetainedQuestLight>& v) {
    codec::io_count(s.out, v.size());
    for (const auto& light : v) io(s.out, light.source);
}
void delta_io(RetainedDeltaIn& s, std::vector<RetainedQuestLight>& v) {
    v.resize(s.in.count(1));
    for (auto& light : v) io(s.in, light.source);
}
void delta_io(RetainedDeltaOut& s, const OwnedTerrainGeometryValues& v) { geometry_fields(s.out, v); }
void delta_io(RetainedDeltaIn& s, OwnedTerrainGeometryValues& v) { geometry_fields(s.in, v); }

void delta_text(RetainedDeltaOut& s, const char* text) {
    const bool present = text != nullptr;
    io(s.out, present);
    if (present) io(s.out, std::string(text));
}

void RetainedPublicationRecorder::emit(const std::vector<std::uint8_t>& message) noexcept {
    std::size_t at = 0;
    do {
        const std::size_t part = (std::min)(fragment_, message.size() - at);
        sink_(context_, message.data() + at, part, at + part == message.size());
        at += part;
    } while (at < message.size());
}

void RetainedPublicationRecorder::fail(const char* what) noexcept {
    // The stream is incomplete from here on: stop it and say so once (core
    // mode then loses the device: retained_publication_failed()).
    if (failed_.exchange(true)) return;
    bridge_append_probe_event("retained_publication_failed", what);
}

} // namespace owned
