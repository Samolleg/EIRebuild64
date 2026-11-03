// The bridge's scene half: per-entity preparation of the retained visual
// frame (admission, bounds, poses, part contributions), colour submission and
// the world command the scene thread runs (run_owned_visual_scene). Reads
// only the captured frame copies (owned_world_frame.h); host-buildable.
#include "owned_world_frame.h"
#include "world_scene_algorithms.h"
#include "embedded_perf06.h"
#include "combat_ui.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace owned {

thread_local NativeWorldLightingFrame g_native_world_lighting_frame{};
thread_local OwnedVisualWorldFrame g_owned_visual_world_frame{};
thread_local std::vector<OwnedVisualFramePartState> g_owned_visual_prepared_spare;
thread_local OwnedTerrainRuntimeFrame g_owned_terrain_frame{};



bool same_visual_resource(const VisualResourceIncarnation& left,
    const VisualResourceIncarnation& right) noexcept {
    return left.signed_id==right.signed_id && left.serial==right.serial;
}
namespace {

bool same_visual_selection_key(const VisualClipSelection& left,
    const VisualClipSelection& right) noexcept {
    if(left.neutral||right.neutral)return left.neutral==right.neutral;
    return left.ordinal==right.ordinal && left.database==right.database && left.clip==right.clip;
}

bool same_visual_admission_key(const VisualPreparedResources::AdmissionKey& left,
    const VisualPreparedResources::AdmissionKey& right) noexcept {
    return same_lifecycle_identity(left.lifecycle,right.lifecycle) &&
        same_visual_resource(left.resource,right.resource) && left.assembly==right.assembly &&
        left.assembly_revision==right.assembly_revision && left.object_flags==right.object_flags &&
        left.opacity==right.opacity &&
        left.shadows_enabled==right.shadows_enabled &&
        left.draw_undetected==right.draw_undetected && left.unit==right.unit &&
        left.hidden==right.hidden && left.transparent_override==right.transparent_override &&
        left.hide_weapons==right.hide_weapons && left.hide_arrow==right.hide_arrow &&
        left.casts_shadow==right.casts_shadow;
}

VisualPreparedResources::AdmissionKey owned_visual_admission_key(
    const RetainedVisualSample& sample,bool draw_undetected,
    bool shadows_enabled) noexcept {
    VisualPreparedResources::AdmissionKey key{};
    key.lifecycle=sample.lifecycle;key.resource=sample.resource;key.assembly=sample.assembly;
    key.assembly_revision=sample.assembly?sample.assembly->revision:0;
    key.shadows_enabled=shadows_enabled;key.unit=sample.values.unit;
    key.object_flags=sample.values.object_flags&(
        0x40u|(key.unit?0x00400000u:(shadows_enabled?8u:0u)));
    key.draw_undetected=key.unit&&draw_undetected;
    key.hidden=sample.values.hidden;
    key.hide_weapons=sample.visibility.hide_weapons;key.hide_arrow=sample.visibility.hide_arrow;
    if(shadows_enabled) {
        if(key.unit)key.casts_shadow=sample.visibility.casts_shadow;
        if(key.unit) {
            key.transparent_override=sample.values.invisible||sample.values.fade_alpha<1;
            if(key.transparent_override)key.opacity=sample.effects.opacity;
        }
    }
    return key;
}

#if defined(EID3D11_EMBEDDED)
// Temporary r6 observation at the renderer's actual admission seam; source
// lists alone cannot establish which service geometry reaches depth/colour.
void report_weapon_admission(const RetainedVisualSample& sample,
    const std::vector<VisualPreparedResources::PartAdmission>& parts) noexcept try {
    static std::uint64_t epoch = 0;
    static std::array<std::uint64_t, 16> reported{};
    const auto current = bridge_current_world_epoch();
    if (epoch != current) { epoch = current; reported = {}; }
    if (!sample.values.unit || !sample.resource.serial ||
        std::find(reported.begin(), reported.end(), sample.resource.serial) != reported.end()) return;
    auto slot = std::find(reported.begin(), reported.end(), 0);
    if (slot == reported.end() || !std::any_of(sample.assembly->parts.begin(), sample.assembly->parts.end(),
        [](const VisualPartAsset& part) {
            return part.name.find(".pike") != std::string::npos || part.name.find(".dpike") != std::string::npos;
        })) return;
    const std::string header = "scope=weapon_parts_admission epoch=" + std::to_string(epoch) + " id=" +
        std::to_string(sample.resource.signed_id) + " serial=" + std::to_string(sample.resource.serial) +
        " revision=" + std::to_string(sample.assembly->revision) + " name_visible_casts=";
    std::string detail = header;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (detail.size() > 500) {
            embedded_perf06_report(detail.c_str());
            detail = header;
        }
        detail += sample.assembly->parts[i].name + ":" + std::to_string(parts[i].visible) +
            ":" + std::to_string(parts[i].casts) + ",";
    }
    *slot = sample.resource.serial;
    embedded_perf06_report(detail.c_str());
} catch (...) {}
#endif

} // namespace
bool prepare_owned_visual_admission(const RetainedVisualSample& sample,
    bool draw_undetected,bool shadows_enabled) {
    auto& prepared=*sample.prepared;
    const auto key=owned_visual_admission_key(sample,draw_undetected,shadows_enabled);
    if(prepared.derived_admission_valid &&
        same_visual_admission_key(prepared.derived_admission_key,key) &&
        prepared.derived_admission_parts.size()==sample.assembly->parts.size()) {
        return true;
    }
    std::vector<VisualPreparedResources::PartAdmission> parts(sample.assembly->parts.size());
    for(std::size_t index=0;index<parts.size();++index) {
        const auto& asset=sample.assembly->parts[index];auto& part=parts[index];
        part.visible=retained_visual_part_visible(sample,asset,draw_undetected);
        // The packed shader receives only for its cutout/opaque material bucket.
        // This fixed alpha-tested figure state reaches that bucket only when
        // authored packed alpha is effectively one; lit figures keep their
        // established receiver policy regardless of color ordering.
        part.receives=shadows_enabled && part.visible &&
            (!(asset.figure_flags&0x100u) ||
             retained_visual_effective_alpha(sample,asset)>=254.5f/255.f);
        part.casts=shadows_enabled && owned_visual_part_casts(sample,asset,draw_undetected);
    }
#if defined(EID3D11_EMBEDDED)
    report_weapon_admission(sample, parts);
#endif
    prepared.derived_admission_key=key;
    prepared.derived_admission_parts=std::move(parts);
    prepared.derived_admission_valid=true;
    return true;
}
namespace {

} // namespace
bool prepare_owned_visual_world_bounds(const RetainedVisualSample& sample,
    NativeWorldBounds& bounds) {
    return prepare_world_visual_bounds(sample,bounds);
}
bool prepare_owned_visual_pose(const RetainedVisualSample& sample) {
#ifdef EID3D11_TESTING
    return prepare_world_visual_pose(sample, &g_owned_visual_world_frame.pose_preparations);
#else
    return prepare_world_visual_pose(sample);
#endif
}

// Per-frame scratch owns camera/light contribution only. Semantic admission,
// world bounds, pose and part bounds stay with the retained resource
// incarnation and are rejected by their authoritative input tuples.
bool prepare_owned_visual_part(std::size_t entity,std::size_t part,
    const OwnedVisualPartPreparation*& output) {
    auto& frame=g_owned_visual_world_frame;
    const auto& sample=frame.values->entities[entity];
    auto& parts=frame.prepared[entity].parts;
    if(parts.size()!=sample.assembly->parts.size())parts.resize(sample.assembly->parts.size());
    // The retained per-part values are sized lazily (also by
    // owned_interaction_shape_bounds); an entity drawn before anything asked
    // for its interaction bounds still had none (null read, 2026-10-06 dump).
    if(sample.prepared->parts.size()!=sample.assembly->parts.size())
        sample.prepared->parts.resize(sample.assembly->parts.size());
    auto& value=parts[part];output=nullptr;
    bool ready=value.ready_serial==frame.serial;
    if(!ready) {
        auto& retained=sample.prepared->parts[part];
        bool reuse=retained.derived_bounds_valid && retained.derived_bounds_pose_generation &&
            retained.derived_bounds_pose_generation==sample.prepared->derived_pose_generation;
        if(reuse) {
            value.bounds=retained.derived_bounds;
        } else {
            ++frame.shadow_source_diagnostic.part_bounds_rebuilt;
            if(g_bridge_sample.active)++g_bridge_sample.shadow_bounds_rebuilt;
            value.ready_serial=0;
            if(!retained_visual_part_bounds(sample,part,value.bounds))return false;
            retained.derived_bounds=value.bounds;
            retained.derived_bounds_pose_generation=sample.prepared->derived_pose_generation;
            retained.derived_bounds_valid=true;
        }
        if(frame.shadows_enabled)
            value.contribution=retained_world_contribution(value.bounds,value.casts,frame.coverage,
                &value.light_bounds);
        else
            value.contribution={true,retained_world_bounds_visible(
                value.bounds,frame.coverage.world_to_clip),0};
        value.ready_serial=frame.serial;
    }
    output=&value;return true;
}


bool owned_point_shadow_influence(const NativeWorldBounds& bounds,
    const RetainedWorldPointLight& light) noexcept {
    if(!bounds.valid || !(light.radius>0))return false;
    const auto shadow_center=retained_point_shadow_center(light);
    double distance_squared=0;
    for(unsigned axis=0;axis<3;++axis) {
        const double center=shadow_center[axis];
        const double delta=center<bounds.minimum[axis] ? bounds.minimum[axis]-center :
            center>bounds.maximum[axis] ? center-bounds.maximum[axis] : 0;
        distance_squared+=delta*delta;
    }
    return distance_squared<=double(light.radius)*light.radius;
}

bool owned_point_shadow_influence(const NativeWorldBounds& bounds,
    const std::vector<RetainedWorldPointLight>& lights) noexcept {
    for(const auto& light:lights)
        if(light.casts_shadow && owned_point_shadow_influence(bounds,light))return true;
    return false;
}
namespace {

// Temporary (static pop-in evidence): static objects appearing/disappearing
// between frames, with camera distance. Remove once the mechanism is settled.
struct StaticMembershipProbe {
    std::map<std::int32_t,std::array<float,3>> members;unsigned member_logged=0;
};
StaticMembershipProbe g_static_membership_probe;

void retire_owned_static_color(const RetainedVisualSample& sample) noexcept;
std::uint64_t owned_static_key(const RetainedVisualSample& sample) noexcept;
// Keys registered in the renderer's static set -> last frame serial whose
// admission saw the entity. Entries unseen in a frame are removed, so an
// entity that silently leaves the frame's list never lingers as a ghost.
std::unordered_map<std::uint64_t,std::uint64_t> g_owned_static_registry;

// The persistent static record was built from other values or another pose
// than the sample now carries (a script moved, rotated or reshaped it).
bool owned_static_registration_stale(const RetainedVisualSample& sample) noexcept {
    if(!sample.prepared || !sample.prepared->color_packet.static_registered)return false;
    const auto& prepared=*sample.prepared;
    const auto& packet=prepared.color_packet;
    return !sample.assembly || packet.assembly!=sample.assembly.get() ||
        packet.assembly_revision!=sample.assembly->revision ||
        !prepared.derived_pose_valid || packet.pose_generation!=prepared.derived_pose_generation ||
        !owned_visual_pose_key_matches(prepared.derived_pose_key,sample) ||
        std::memcmp(&packet.values,&sample.values,sizeof(packet.values))!=0;
}

} // namespace
thread_local OwnedAdmissionCache g_owned_admission_cache;
namespace {
bool owned_admission_finite(const NativeWorldBounds& b) noexcept {
    if(!b.valid)return false;
    for(unsigned axis=0;axis<3;++axis)
        if(!std::isfinite(b.minimum[axis]) || !std::isfinite(b.maximum[axis]) || b.minimum[axis]>b.maximum[axis])return false;
    return true;
}
} // namespace
bool prepare_owned_visual_values(std::shared_ptr<const RetainedVisualFrame> values,
    const std::array<float,16>& world_to_clip, const NativeWorldLighting& lighting,
    bool draw_undetected, std::uint32_t class_flags,
    const std::vector<NativeWorldBounds>& terrain_receivers,bool shadows_enabled,
    const RetainedDirectionalCoverage* reused_coverage,
    const std::vector<std::int32_t>* static_dirty,
    std::uint64_t dirty_since,std::uint64_t dirty_through) {
    CityRetainedOwnerGuard receiver_timing(CityRetainedOwner::receiver_fitting);
    auto& cache=g_owned_admission_cache;
    const bool cache_was_valid=cache.valid;
    cache.valid=false;   // valid again only when this preparation completes
    auto& frame = g_owned_visual_world_frame;
    auto prepared_storage=std::move(frame.prepared);
    if(prepared_storage.empty())prepared_storage.swap(g_owned_visual_prepared_spare);
    const auto shadow_source_diagnostic=frame.shadow_source_diagnostic;
    auto shadow_storage=std::move(frame.shadows);
    clear_owned_visual_world_frame(frame);   // the scratch vectors keep their capacity
    if(!values)return false;
    frame.prepared=std::move(prepared_storage);
    frame.shadow_source_diagnostic=shadow_source_diagnostic;
    frame.shadows=std::move(shadow_storage);
    frame.active = true;
    frame.shadows_enabled = shadows_enabled;
    frame.values = std::move(values);
    if (g_bridge_sample.active && bridge_profile_enabled()) {
        frame.diagnostic=frame.values->diagnostic;
        frame.diagnostic.profile_frame=g_bridge_sample.frame;
        frame.diagnostic.samples=frame.values->entities.size();
        if (frame.values->first_failure.stage) log_owned_visual_failure(frame.values->first_failure);
        if (frame.diagnostic.first_rejection.stage) log_owned_visual_failure(frame.diagnostic.first_rejection);
    }
    frame.draw_undetected_units = draw_undetected;
    frame.serial = ++g_object_render_presentation_serial;
    if (!frame.serial) frame.serial = ++g_object_render_presentation_serial;
#ifdef EID3D11_EMBEDDED
    const auto* g_embedded_visual_inputs=bridge_embedded_visual_inputs();
    if(hovered_shadow_observation_enabled() && g_embedded_visual_inputs)
        frame.observed_hover={g_embedded_visual_inputs->hover_id,g_embedded_visual_inputs->hover_serial};
#else
    if(hovered_shadow_observation_enabled() && !g_scene_frame_values_installed)frame.observed_hover=hovered_shadow_observation_target();
#endif
    const auto entity_count=frame.values->entities.size();
    auto receivers = shadows_enabled ? terrain_receivers : std::vector<NativeWorldBounds>{};
    // Static receivers are unioned per terrain-sector-sized cell, as terrain
    // receivers are per sector: coverage clips every receiver box against the
    // camera and each map, and ~2,000 object boxes dominated that work while
    // their per-cell union changes the fitted envelopes only within a cell.
    float receiver_cell=0;
    for(const auto& sector:terrain_receivers)
        receiver_cell=(std::max)(receiver_cell,(std::max)(sector.maximum[0]-sector.minimum[0],sector.maximum[1]-sector.minimum[1]));
    auto& static_receivers=frame.static_receivers;static_receivers.clear();
    static const bool membership_probe=[]{char value[4]{};   // temporary: static object pop-in evidence
        return GetEnvironmentVariableA("EI_RENDERER_STATIC_MEMBERSHIP_PROBE",value,sizeof(value))!=0;}();
    if(membership_probe&&g_world_lamp_memory.camera_valid) try {
        auto& probe=g_static_membership_probe;
        std::map<std::int32_t,std::array<float,3>> now;
        for(const auto& sample:frame.values->entities)if(!sample.values.unit)now.emplace(sample.lifecycle.id,sample.object_pose.position);
        const auto log=[&](bool added,const std::array<float,3>& p){
            if(probe.member_logged>=400)return;++probe.member_logged;double d=0;
            for(unsigned axis=0;axis<3;++axis){const double c=p[axis]-g_world_lamp_memory.camera[axis];d+=c*c;}
            char detail[80];std::snprintf(detail,sizeof(detail),"added=%d distance=%.1f",added?1:0,std::sqrt(d));
            append_bridge_event("static_membership_probe",detail,false);
        };
        if(!probe.members.empty()&&now.size()+64>probe.members.size()&&probe.members.size()+64>now.size()){
            for(const auto& [id,p]:probe.members)if(!now.count(id))log(false,p);
            for(const auto& [id,p]:now)if(!probe.members.count(id))log(true,p);
        }
        probe.members=std::move(now);
    } catch(...) {}
    // An unchanged object in the persistent static sets needs no admission,
    // contribution, pose or candidate work: its colour and shadow records are
    // already drawn, and its admission flags and bounds are cached with it.
    // Change detection is the journal (static_dirty), admission settings and
    // the renderer's neighbour fog publication (which never dirties objects).
    static struct { bool valid=false,draw_undetected=false,shadows=false; std::uint32_t flags=0; } admission_settings;
    bool static_skip=static_dirty && admission_settings.valid && admission_settings.draw_undetected==draw_undetected &&
        admission_settings.shadows==shadows_enabled && admission_settings.flags==class_flags;
    admission_settings={true,draw_undetected,shadows_enabled,class_flags};
    std::uint64_t static_generation=0;
    if(static_skip) {
        NativeStaticSubmission query;query.operation=NativeStaticSubmission::Operation::generation;
        NativeGeometryView view;view.static_submission=&query;NativeGeometryResult ignored{};
        static_skip=g_native_sink_submit && g_native_sink_submit(g_native_sink_context,view,ignored);
        static_generation=query.generation;
    }
    const auto border_fog_revision=current_border_fog_revision();
    // Persistent admission (OwnedAdmissionCache): the frame-level conditions,
    // then the identity at every index.
    bool incremental=cache_was_valid && static_skip && !frame.diagnostic.profile_frame && !membership_probe &&
        cache.draw_undetected==draw_undetected && cache.shadows==shadows_enabled && cache.class_flags==class_flags &&
        cache.static_generation==static_generation && cache.border_fog_revision==border_fog_revision &&
        dirty_since<=cache.through && receiver_cell>0 &&
        cache.entries.size()==entity_count && frame.prepared.size()==entity_count;
    for(std::size_t i=0;incremental && i<entity_count;++i) {
        const auto& sample=frame.values->entities[i];const auto& entry=cache.entries[i];
        incremental=same_lifecycle_identity(entry.lifecycle,sample.lifecycle) &&
            entry.resource_serial==sample.resource.serial && entry.claimed==sample.mesh_declared &&
            (entry.state==OwnedAdmissionCache::State::process ||
             (sample.status==RetainedVisualStatus::ready && sample.assembly && sample.prepared));
    }
    auto& admit=cache.admit;admit.clear();
    auto& contribute=cache.contribute;contribute.clear();
    std::size_t kept_claimed=0;
    if(incremental && cache.receiver_cell!=receiver_cell) {
        // The receiver cell follows the largest visible terrain sector; a
        // new size only re-keys the kept receivers (append_cell_merged_receivers
        // unions by this cell).
        std::vector<OwnedAdmissionCache::Member> kept;
        for(const auto& [key,cell]:cache.cells)kept.insert(kept.end(),cell.members.begin(),cell.members.end());
        cache.cells.clear();cache.receiver_cell=receiver_cell;
        for(const auto& member:kept){cache.entries[member.index].in_cell=false;cache.join_cell(member.index,member.bounds);}
    }
    if(incremental) {
        for(const auto id:*static_dirty) {
            auto it=std::lower_bound(cache.entries.begin(),cache.entries.end(),id,
                [](const OwnedAdmissionCache::Entry& entry,std::int32_t key){return entry.lifecycle.id<key;});
            for(;it!=cache.entries.end() && it->lifecycle.id==id;++it)it->dirty=true;
        }
        for(std::size_t i=0;i<entity_count;++i) {
            auto& entry=cache.entries[i];
            if(entry.state==OwnedAdmissionCache::State::process || entry.dirty) {
                entry.dirty=false;entry.state=OwnedAdmissionCache::State::process;
                cache.leave_cell(static_cast<std::uint32_t>(i));
                admit.push_back(static_cast<std::uint32_t>(i));
                contribute.push_back(static_cast<std::uint32_t>(i));
            } else {
                if(entry.state==OwnedAdmissionCache::State::admitted)contribute.push_back(static_cast<std::uint32_t>(i));
                kept_claimed+=entry.claimed;
            }
        }
        for(const auto i:admit) {
            auto& part=frame.prepared[i];
            auto storage=std::move(part.parts);part={};part.parts=std::move(storage);part.parts.clear();
        }
    } else {
        cache.clear();
        frame.prepared.resize(entity_count);
        for(auto& part:frame.prepared){
            auto storage=std::move(part.parts);part={};part.parts=std::move(storage);part.parts.clear();
        }
    }
    frame.admission_incremental=incremental;
    frame.considered+=kept_claimed;
    const auto admit_entity=[&](std::size_t i) {
        const auto& sample = frame.values->entities[i];
        auto& prepared = frame.prepared[i];
        prepared.claimed = sample.mesh_declared;
        if (!prepared.claimed) {
            reject_owned_visual("classification","unclaimed",&sample,false,class_flags);
            return;
        }
        ++frame.considered;
        if (sample.status != RetainedVisualStatus::ready || !sample.assembly || !sample.prepared) {
            retire_owned_static_color(sample);
            if (frame.diagnostic.profile_frame) ++frame.diagnostic.unavailable_values;
            reject_owned_visual("values",sample.status!=RetainedVisualStatus::ready ?
                (sample.readiness_reason ? sample.readiness_reason : retained_visual_status_name(static_cast<std::uint32_t>(sample.status))) :
                !sample.assembly ? "assembly" : "prepared_resources",&sample);
            report_object_plan_failure("owned_scene_value_unavailable");
            return;
        }
        // Temporary (bind-pose report, 2026-10-02): a unit whose game controller
        // has no current entry is drawn in the bind pose, exactly as the game's
        // cleared tracks are. Name such units so a capture shows whether a
        // reported T-pose is that game state. Remove once that report is settled.
        if (sample.values.unit && sample.selection.neutral && sample.assembly->skeleton) {
            static std::set<std::int32_t> reported;  // world submission thread only
            try {
                if (reported.size() < 32 && reported.insert(sample.resource.signed_id).second) {
                    char detail[256]{};
                    std::snprintf(detail, sizeof(detail), "id=%d model=%s",
                        static_cast<int>(sample.resource.signed_id),
                        sample.assembly->skeleton->model_container.canonical_name.c_str());
                    append_bridge_event("owned_unit_no_current_animation", detail, true);
                }
            } catch (...) {}
        }
        if (!(class_flags & (sample.values.unit ? 0x80u : 0x40u))) {
            retire_owned_static_color(sample);
            if (frame.diagnostic.profile_frame) ++frame.diagnostic.class_rejected;
            reject_owned_visual("classification","class_flags",&sample,false,class_flags);
            return;
        }
        if (static_skip && !sample.values.unit && sample.prepared->derived_admission_valid &&
            sample.prepared->derived_world_bounds_valid) {
            const auto& packet=sample.prepared->color_packet;
            if (packet.static_current(static_generation,border_fog_revision) &&
                !std::binary_search(static_dirty->begin(),static_dirty->end(),sample.lifecycle.id)) {
                const auto found=g_owned_static_registry.find(owned_static_key(sample));
                if (found!=g_owned_static_registry.end()) {
                    const auto& admitted=sample.prepared->derived_admission_parts;
                    prepared.parts.resize(admitted.size());
                    bool receives=false;
                    for(std::size_t part_index=0;part_index<admitted.size();++part_index){
                        auto& part=prepared.parts[part_index];
                        part.visible=admitted[part_index].visible;part.receives=admitted[part_index].receives;
                        part.casts=admitted[part_index].casts;prepared.casting_parts+=part.casts;receives|=part.receives;
                    }
                    prepared.bounds=sample.prepared->derived_world_bounds;
                    prepared.point_membership_bounds=prepared.bounds;
                    prepared.static_reused=true;
                    found->second=frame.serial;
                    if (frame.diagnostic.profile_frame) ++frame.diagnostic.bounds_ready;
                    if (receives && retained_world_bounds_visible(prepared.bounds,world_to_clip))
                        static_receivers.push_back(prepared.bounds);
                    return;
                }
            }
        }
        if (!prepare_owned_visual_admission(sample,draw_undetected,shadows_enabled)) {
            retire_owned_static_color(sample);
            reject_owned_visual("classification","admission",&sample,false,class_flags);
            return;
        }
        const auto& admitted=sample.prepared->derived_admission_parts;
        prepared.parts.resize(admitted.size());
        bool visible_part=false,receives=false,casts=false;
        for(std::size_t part_index=0;part_index<admitted.size();++part_index){
            auto& part=prepared.parts[part_index];
            part.visible=admitted[part_index].visible;
            part.receives=admitted[part_index].receives;
            part.casts=admitted[part_index].casts;
            prepared.casting_parts+=part.casts;
            visible_part|=part.visible;receives|=part.receives;casts|=part.casts;
        }
        if (!visible_part) {
            retire_owned_static_color(sample);
            if (frame.diagnostic.profile_frame) ++frame.diagnostic.no_visible_part;
            reject_owned_visual("visibility","no_visible_part",&sample,false,class_flags);
            return;
        }
        if (!prepare_owned_visual_world_bounds(sample,prepared.bounds)) {
            retire_owned_static_color(sample);
            if (frame.diagnostic.profile_frame) ++frame.diagnostic.invalid_envelope;
            reject_owned_visual("envelope","world_bounds",&sample);
            report_object_plan_failure("owned_scene_envelope");
            return;
        }
        // Preserve the camera-independent admission envelope before pose/coverage work.
        prepared.point_membership_bounds=prepared.bounds;
        if (sample.prepared->color_packet.static_registered) {
            const auto found=g_owned_static_registry.find(owned_static_key(sample));
            if (found!=g_owned_static_registry.end()) found->second=frame.serial;
        }
        if (frame.diagnostic.profile_frame) ++frame.diagnostic.bounds_ready;
        if (receives && retained_world_bounds_visible(prepared.bounds,world_to_clip)) {
            if (sample.values.unit) receivers.push_back(prepared.bounds);
            else static_receivers.push_back(prepared.bounds);
        }
    };
    if(incremental)for(const auto i:admit)admit_entity(i);
    else for(std::size_t i=0;i<entity_count;++i)admit_entity(i);
    if(incremental) {
        // Kept entities' receivers: one decision per cell where the camera
        // allows it, else the exact per-box test of each member.
        auto& unions=cache.cell_unions;unions.clear();
        for(auto& [key,cell]:cache.cells) {
            if(!cell.all_current) {
                cell.all=cell.members.front().bounds;
                for(const auto& member:cell.members)
                    for(unsigned axis=0;axis<3;++axis) {
                        cell.all.minimum[axis]=(std::min)(cell.all.minimum[axis],member.bounds.minimum[axis]);
                        cell.all.maximum[axis]=(std::max)(cell.all.maximum[axis],member.bounds.maximum[axis]);
                    }
                cell.all_current=true;
            }
            const auto kind=classify_receiver_cell(cell.all,world_to_clip);
            if(kind==ReceiverCellClass::outside)continue;
            if(kind==ReceiverCellClass::inside){unions.emplace_back(key,cell.all);continue;}
            NativeWorldBounds merged{};
            for(const auto& member:cell.members) {
                if(!retained_world_bounds_visible(member.bounds,world_to_clip))continue;
                if(!merged.valid){merged=member.bounds;continue;}
                for(unsigned axis=0;axis<3;++axis) {
                    merged.minimum[axis]=(std::min)(merged.minimum[axis],member.bounds.minimum[axis]);
                    merged.maximum[axis]=(std::max)(merged.maximum[axis],member.bounds.maximum[axis]);
                }
            }
            if(merged.valid)unions.emplace_back(key,merged);
        }
        append_cell_merged_receivers(receiver_cell,static_receivers,unions,receivers);
    } else append_cell_merged_receivers(receiver_cell,static_receivers,receivers);

    if (shadows_enabled) {
        const TerrainCpuAsset* base_source=nullptr;
        const std::array<float,16>* base_transform=nullptr;std::uint64_t base_generation=0;
        current_base_terrain_quality_reference(base_source,base_transform,base_generation);
        if(reused_coverage)frame.coverage=*reused_coverage;
        else if (!build_retained_directional_coverage(world_to_clip,lighting,receivers,
            frame.coverage,base_source,base_transform,g_owned_shadow_settings.map_side,
            g_owned_shadow_settings.close_cascades,base_generation,g_owned_shadow_settings.close_range,
            frame.camera_anchor_z)) {
            reject_owned_visual("coverage","receiver_coverage");
            report_object_plan_failure("owned_scene_receiver_coverage");
            return false;
        }
        const unsigned first_close=3u-g_owned_shadow_settings.close_cascades;
        for(unsigned index=0;index<5;++index)
            if((index<3 && index<first_close) ||
               (index>=3 && !g_owned_shadow_settings.far_shadows))
                frame.coverage.slices[index].receivers_in_light={};
    } else {
        frame.coverage={};frame.coverage.world_to_clip=world_to_clip;frame.coverage.valid=true;
    }
    if(shadows_enabled)frame.shadow_receivers=std::move(receivers);
    receiver_timing.finish();
    CityRetainedOwnerGuard caster_timing(CityRetainedOwner::caster_values);
    // Published entity envelopes depend only on authoritative scalar values and
    // immutable asset metadata. They can select both consumers before binding
    // animation tracks or constructing backend part envelopes.
    cache.rejected.assign(entity_count,0);
    const auto contribute_entity=[&](std::size_t i) {
        auto& prepared=frame.prepared[i];
        prepared.candidate_begin=frame.candidates.size();
        prepared.candidate_end=prepared.candidate_begin;
        if (!prepared.bounds.valid) return;
        // Admission already proved it unchanged and registered this frame.
        if (prepared.static_reused) { prepared.submitted=true; return; }
        const auto& sample=frame.values->entities[i];
        const bool casts=shadows_enabled && prepared.casting_parts!=0;
        prepared.contribution=shadows_enabled ?
            retained_world_contribution(prepared.bounds,casts,frame.coverage) :
            NativeContribution{true,retained_world_bounds_visible(prepared.bounds,world_to_clip),0};
        const bool point_caster=shadows_enabled && g_owned_shadow_settings.point_shadows &&
            prepared.casting_parts &&
            owned_point_shadow_influence(prepared.bounds,frame.values->world_point_lights);

        // A registered static set record keeps drawing (GPU-culled) at the
        // pose it was built with. Only colour-contributing entities are
        // resubmitted to the set, so an object that changed since and now
        // lies outside the camera would keep its old record: retire it; it
        // registers again when it is in view.
        if (!prepared.contribution.color && owned_static_registration_stale(sample))
            retire_owned_static_color(sample);
        if (!prepared.contribution.color && !prepared.contribution.light_mask && !point_caster) {
            frame.shadow_considered+=prepared.casting_parts;
            if(frame.diagnostic.profile_frame)++frame.diagnostic.contribution_rejected;
            reject_owned_visual("contribution","outside_receiver_coverage",&sample,false);
            cache.rejected[i]=1;
            return;
        }
        // One preparation for this value serves color, shadow revision checks,
        // dirty depth slices and the later admitted wound overlay.
        const bool posed=prepare_owned_visual_pose(sample);
        if (!posed) {
            retire_owned_static_color(sample);
            prepared.bounds={};prepared.contribution={};
            reject_owned_visual("values","pose_binding",&sample);
            report_object_plan_failure("owned_scene_pose_binding");
            return;
        }
        frame.shadow_considered+=prepared.casting_parts;
        if(prepared.contribution.color) {
            double z=world_to_clip[11],w=world_to_clip[15];
            for(unsigned axis=0;axis<3;++axis) {
                const double center=(double(prepared.bounds.minimum[axis])+prepared.bounds.maximum[axis])*.5;
                z+=world_to_clip[8+axis]*center;w+=world_to_clip[12+axis]*center;
            }
            frame.color_order.emplace_back(w>0?z/w:1.0,i);
        } else frame.light_only_order.push_back(i);
        const bool merged=owned_unit_merged(sample);
        for(std::size_t part=0;part<prepared.parts.size();++part) {
            const auto& value=prepared.parts[part];
            if(value.casts && prepared.contribution.light_mask)++frame.shadow_eligible;
            if(merged && owned_merged_member(sample,part))continue;
            if((value.visible && prepared.contribution.color) ||
               (value.casts && (prepared.contribution.light_mask || point_caster)))
                frame.candidates.push_back({i,part});
        }
        if(merged)for(std::size_t group=0;group<sample.prepared->merged_groups.size();++group) {
            bool visible=false,casts=false;
            for(const auto part:sample.prepared->merged_groups[group].parts)
                if(part<prepared.parts.size()){visible|=prepared.parts[part].visible;casts|=prepared.parts[part].casts;}
            if((visible && prepared.contribution.color) || (casts && (prepared.contribution.light_mask || point_caster)))
                frame.candidates.push_back({i,kMergedPartBase+group});
        }
        prepared.candidate_end=frame.candidates.size();
    };
    if(incremental)for(const auto i:contribute)contribute_entity(i);
    else for(std::size_t i=0;i<entity_count;++i)contribute_entity(i);
    // Keep what the next frame may skip: the static colour set's members, and
    // static objects outside every consumer whose admission stands.
    const auto settle=[&](std::size_t i) {
        auto& entry=cache.entries[i];
        const auto& prepared=frame.prepared[i];
        const auto& sample=frame.values->entities[i];
        auto state=OwnedAdmissionCache::State::process;
        if(prepared.claimed && prepared.bounds.valid) {
            if(prepared.static_reused)state=OwnedAdmissionCache::State::reused;
            else if(cache.rejected[i] && !sample.values.unit && sample.prepared &&
                    !sample.prepared->color_packet.static_registered)
                state=OwnedAdmissionCache::State::admitted;
        }
        entry.state=state;
        bool receives=false;
        if(state!=OwnedAdmissionCache::State::process)
            for(const auto& part:prepared.parts)receives|=part.receives;
        receives=receives && owned_admission_finite(prepared.bounds);
        if(!receives)cache.leave_cell(static_cast<std::uint32_t>(i));
        else if(!entry.in_cell)cache.join_cell(static_cast<std::uint32_t>(i),prepared.bounds);
    };
    // Dirty marks find entities by binary search: the list must be in
    // ascending ID order (it is built so); a kept list keeps its order.
    bool ordered=true;
    if(incremental)for(const auto i:contribute)settle(i);
    else {
        for(std::size_t i=1;i<entity_count && ordered;++i)
            ordered=frame.values->entities[i-1].lifecycle.id<=frame.values->entities[i].lifecycle.id;
        cache.receiver_cell=receiver_cell;
        cache.entries.resize(entity_count);
        for(std::size_t i=0;i<entity_count;++i) {
            const auto& sample=frame.values->entities[i];auto& entry=cache.entries[i];
            entry=OwnedAdmissionCache::Entry{};
            entry.lifecycle=sample.lifecycle;entry.resource_serial=sample.resource.serial;
            entry.claimed=sample.mesh_declared;
            if(receiver_cell>0)settle(i);
        }
    }
    // Persistent admission needs the receiver cells (receiver_cell > 0).
    if(receiver_cell>0 && static_dirty && ordered) {
        cache.draw_undetected=draw_undetected;cache.shadows=shadows_enabled;cache.class_flags=class_flags;
        cache.static_generation=static_generation;cache.border_fog_revision=border_fog_revision;
        cache.through=dirty_through;cache.valid=static_skip;
    }
    return true;
}
namespace {


std::uint64_t owned_static_key(const RetainedVisualSample& sample) noexcept {
    return (std::uint64_t(std::uint32_t(sample.lifecycle.id))<<32)|std::uint32_t(sample.lifecycle.generation);
}

// Leave the renderer's static set (hidden, rejected or no longer eligible).
void retire_owned_static_color(const RetainedVisualSample& sample) noexcept {
    if(!sample.prepared || !sample.prepared->color_packet.static_registered || !g_native_sink_submit)return;
    sample.prepared->color_packet.static_registered=false;
    NativeStaticSubmission request;request.operation=NativeStaticSubmission::Operation::remove;
    request.key=owned_static_key(sample);g_owned_static_registry.erase(request.key);
    NativeGeometryView view;view.static_submission=&request;NativeGeometryResult ignored{};
    g_native_sink_submit(g_native_sink_context,view,ignored);
}

// A static object belongs in the persistent set when nothing about its color
// records varies per frame: not a unit, no time-scrolled texture and no part
// that must keep per-frame transparency order.
bool owned_static_color_eligible(const RetainedVisualSample& sample,
    const OwnedVisualFramePartState& prepared) noexcept {
    if(sample.values.unit || !sample.assembly || prepared.parts.size()!=sample.assembly->parts.size())return false;
    bool any=false;
    for(std::size_t index=0;index<prepared.parts.size();++index) {
        if(!prepared.parts[index].visible)continue;
        const auto& part=sample.assembly->parts[index];
        if(part.texture_type==13 || part.texture_type==14 || part.texture_type==16 ||
           retained_visual_preserve_color_order(sample,part))return false;
        any=true;
    }
    return any;
}

// The game's distance fog for one entity of the frame: its fog function is a
// game call, so on the scene thread the value comes from the table the game
// thread filled when it queued the command.
bool owned_visual_fog(void* fog_controller,std::size_t index,const RetainedVisualSample& sample,BYTE& fog) noexcept {
    if (g_scene_fog_table) {
        if (index >= g_scene_fog_table->size() || (*g_scene_fog_table)[index] < 0) return false;
        fog = static_cast<BYTE>((*g_scene_fog_table)[index]);
        return true;
    }
    return retained_world_fog(fog_controller,sample.pose.root,fog);
}

bool owned_static_color_current(const VisualPreparedResources::ColorPacket& packet,
    const RetainedVisualSample& sample,const OwnedVisualFramePartState& prepared,
    std::uint8_t fog,std::uint64_t generation,std::uint64_t border_fog_revision) noexcept {
    if(!packet.static_current(generation,border_fog_revision) ||
       packet.assembly!=sample.assembly.get() || packet.assembly_revision!=sample.assembly->revision ||
       packet.pose_generation!=sample.prepared->derived_pose_generation || packet.fog!=fog ||
       packet.quest_light!=sample.quest_light_active || packet.visible.size()!=prepared.parts.size() ||
       std::memcmp(&packet.values,&sample.values,sizeof(packet.values))!=0)return false;
    for(std::size_t index=0;index<prepared.parts.size();++index)
        if(packet.visible[index]!=std::uint8_t(prepared.parts[index].visible))return false;
    return true;
}

} // namespace
bool submit_owned_visual_color(void* fog_controller, bool prepare_light_only,
    bool static_set) {
 PERF06_SCOPE("scene_color_submission");
    auto& frame=g_owned_visual_world_frame;
    const auto& world_to_clip=frame.coverage.world_to_clip;
    if (!g_native_sink_context || !g_native_sink_submit || !g_native_sink_begin_batch ||
        !g_native_sink_end_batch) {
        reject_owned_visual("submission","sink_unavailable");
        return false;
    }
    bool batch = false;
    bool success = true;
    auto& color_order=frame.color_order;
    auto& light_only_order=frame.light_only_order;
    if(!prepare_light_only)for(const auto i:light_only_order) {
        if(frame.diagnostic.profile_frame)++frame.diagnostic.contribution_rejected;
        reject_owned_visual("contribution","outside_receiver_coverage",
            &frame.values->entities[i],false);
    }
    // Renderer-owned color order replaces the old mutable texture/geometry
    // pointer sort. Depth increases away from the camera in this LH clip space.
    // Equal depths retain lifecycle order. Shadow grouping ignores this order.
    std::stable_sort(color_order.begin(),color_order.end(),
        [](const auto& a,const auto& b) { return a.first>b.first; });
    const auto color_units=std::stable_partition(color_order.begin(),color_order.end(),
        [&](const auto& item){return !frame.values->entities[item.second].values.unit;});
    const auto light_units=prepare_light_only ?
        std::stable_partition(light_only_order.begin(),light_only_order.end(),
            [&](std::size_t item){return !frame.values->entities[item].values.unit;}) :
        light_only_order.begin();
    auto& submission_order=frame.submission_order;
    submission_order.clear();
    submission_order.reserve(color_order.size()+(prepare_light_only?light_only_order.size():0));
    for(auto it=color_order.begin();it!=color_units;++it)submission_order.push_back(it->second);
    if(prepare_light_only)
        submission_order.insert(submission_order.end(),light_only_order.begin(),light_units);
    for(auto it=color_units;it!=color_order.end();++it)submission_order.push_back(it->second);
    if(prepare_light_only)
        submission_order.insert(submission_order.end(),light_units,light_only_order.end());
    // Persistent static color set: an unchanged static object stays registered
    // in the renderer and is drawn GPU-culled with the whole set; only new or
    // changed ones are (re)registered here. The set draws before per-frame
    // objects, preserving static-before-unit order.
    auto& static_drawn=frame.static_drawn;
    try { static_drawn.assign(frame.prepared.size(),0); } catch(...) { return false; }
    if(static_set && !prepare_light_only) try {
        NativeStaticSubmission query;query.operation=NativeStaticSubmission::Operation::generation;
        NativeGeometryView query_view;query_view.static_submission=&query;NativeGeometryResult ignored{};
        if(!g_native_sink_submit(g_native_sink_context,query_view,ignored))throw "owned_static_generation";
        const auto generation=query.generation;
        const auto border_fog_revision=current_border_fog_revision();
        for (const auto i:submission_order) {
            const auto& sample = frame.values->entities[i];
            auto& prepared = frame.prepared[i];
            if (!prepared.claimed || !prepared.bounds.valid || !sample.prepared) continue;
            if (!owned_static_color_eligible(sample,prepared)) { retire_owned_static_color(sample); continue; }
            BYTE fog = 255;
            if (!(sample.values.object_flags & 0x800u) &&
                !owned_visual_fog(fog_controller,i,sample,fog)) continue;   // per-frame path reports
            auto& packet=sample.prepared->color_packet;
            std::size_t parts_drawn=0;
            if (owned_static_color_current(packet,sample,prepared,fog,generation,border_fog_revision) &&
                g_owned_static_registry.count(owned_static_key(sample))) {
                for(std::size_t index=0;index<prepared.parts.size();++index)
                    if(prepared.parts[index].visible){prepared.parts[index].color_admitted=true;++parts_drawn;}
            } else {
                // Every visible part, whatever the current frustum: the GPU culls.
                frame.selected.clear();
                bool built=true;
                for(std::size_t index=0;index<prepared.parts.size() && built;++index) {
                    if(!prepared.parts[index].visible)continue;
                    const OwnedVisualPartPreparation* part_value=nullptr;
                    NativePreparedPartSubmission* record=nullptr;
                    built=prepare_owned_visual_part(i,index,part_value) &&
                        retained_visual_part_submission(sample,index,part_value->bounds,{true,true,0},
                            frame.values->time,fog,record);
                    if(built)frame.selected.push_back(record);
                }
                packet.static_registered=false;
                if(!built || frame.selected.empty())continue;
                NativePreparedSubmission submission;
                submission.parts=frame.selected.data();submission.count=static_cast<UINT>(frame.selected.size());
                submission.pose=&sample.prepared->gpu_pose;
                submission.identity={sample.lifecycle.id,sample.lifecycle.generation,sample.lifecycle.positive_world_epoch,
                    sample.resource.serial,0,0,frame.serial,true};
                submission.world_to_clip=&world_to_clip;submission.retained_color_diagnostic=true;
                NativeStaticSubmission request;request.key=owned_static_key(sample);request.records=&submission;
                NativeGeometryView view;view.static_submission=&request;NativeGeometryResult result{};
                if(!g_native_sink_submit(g_native_sink_context,view,result) || !request.registered)continue;
                packet.visible.resize(prepared.parts.size());
                for(std::size_t index=0;index<prepared.parts.size();++index)packet.visible[index]=prepared.parts[index].visible;
                packet.assembly=sample.assembly.get();packet.assembly_revision=sample.assembly->revision;
                packet.pose_generation=sample.prepared->derived_pose_generation;
                packet.values=sample.values;packet.fog=fog;packet.quest_light=sample.quest_light_active;
                packet.static_registered=true;packet.static_generation=request.generation;
                packet.border_fog_revision=border_fog_revision;
                g_owned_static_registry[request.key]=frame.serial;
                for(std::size_t index=0;index<prepared.parts.size();++index)
                    if(prepared.parts[index].visible){prepared.parts[index].color_admitted=true;++parts_drawn;}
            }
            static_drawn[i]=1;prepared.submitted=true;
            ++frame.eligible;frame.submitted_parts+=parts_drawn;
        }
        const auto remove_unseen=[&](std::unordered_map<std::uint64_t,std::uint64_t>::iterator it) {
            NativeStaticSubmission request;request.operation=NativeStaticSubmission::Operation::remove;
            request.key=it->first;NativeGeometryView view;view.static_submission=&request;
            NativeGeometryResult removed{};g_native_sink_submit(g_native_sink_context,view,removed);
            return g_owned_static_registry.erase(it);
        };
        if(frame.admission_incremental) {
            // Only visited entities can be unseen: every kept one is a current
            // member of the set (OwnedAdmissionCache), and no entity left.
            for(const auto i:g_owned_admission_cache.contribute) {
                const auto it=g_owned_static_registry.find(owned_static_key(frame.values->entities[i]));
                if(it!=g_owned_static_registry.end() && it->second!=frame.serial)remove_unseen(it);
            }
        } else for(auto it=g_owned_static_registry.begin();it!=g_owned_static_registry.end();) {
            if(it->second==frame.serial){++it;continue;}
            it=remove_unseen(it);
        }
        NativeStaticSubmission flush;flush.operation=NativeStaticSubmission::Operation::flush;
        flush.world_to_clip=&world_to_clip;
        NativeGeometryView flush_view;flush_view.static_submission=&flush;NativeGeometryResult flush_result{};
        if(!g_native_sink_submit(g_native_sink_context,flush_view,flush_result))throw "owned_static_flush";
    } catch(const char* reason) {
        reject_owned_visual("submission",reason);report_object_plan_failure(reason);
        return false;
    } catch(...) {
        report_object_plan_failure("owned_static_allocation");return false;
    }
    // Preserve static-before-unit while visiting the completed order once.
    // Within each span, color stays depth-stable and light-only stays lifecycle-stable.
    NativeFrameState combat_pass{};
    bool combat_pass_checked=false, combat_pass_ready=false;
    try {
        for (const auto i:submission_order) {
            const auto& sample = frame.values->entities[i];
            auto& prepared = frame.prepared[i];
            if (!prepared.claimed || !prepared.bounds.valid || static_drawn[i]) continue;
            ++frame.eligible;
            BYTE fog = 255;
            if (!(sample.values.object_flags & 0x800u) &&
                !owned_visual_fog(fog_controller,i,sample,fog)) {
                reject_owned_visual("submission","fog",&sample);
                report_object_plan_failure("owned_scene_fog"); success = false; continue;
            }
            if (!batch) {
                if (!g_native_sink_begin_batch(g_native_sink_context,SemanticKind::figure)) {
                    reject_owned_visual("submission","begin_batch",&sample); return false;
                }
                batch = true;
            }
            frame.selected.clear();
            bool commit = true;
            std::size_t submitted = 0;
            // A fully visible static object whose record inputs are unchanged
            // resubmits its previous selection without per-part preparation.
            auto& packet=sample.prepared->color_packet;
            const bool packet_eligible=!prepare_light_only && !sample.values.unit;
            bool reused=false;
            if(packet_eligible && packet.valid && packet.assembly==sample.assembly.get() &&
               packet.assembly_revision==sample.assembly->revision &&
               packet.pose_generation==sample.prepared->derived_pose_generation &&
               packet.fog==fog && packet.quest_light==sample.quest_light_active &&
               packet.visible.size()==prepared.parts.size() &&
               std::memcmp(&packet.values,&sample.values,sizeof(packet.values))==0 &&
               retained_world_bounds_inside(packet.bounds,world_to_clip)) {
                reused=true;
                for(std::size_t p=0;p<prepared.parts.size() && reused;++p)
                    reused=packet.visible[p]==std::uint8_t(prepared.parts[p].visible);
                for(std::size_t k=0;k<packet.parts.size() && reused;++k)
                    reused=sample.prepared->parts[packet.parts[k]].submission.values_revision==packet.values_revisions[k] &&
                        sample.prepared->parts[packet.parts[k]].record.world.geometry_radius==
                            sample.prepared->parts[packet.parts[k]].geometry_radius;
                if(reused)for(std::size_t k=0;k<packet.parts.size();++k) {
                    auto& record=sample.prepared->parts[packet.parts[k]].submission;
                    record.bounds=packet.part_bounds[k];record.contribution={true,true,0};
                    frame.selected.push_back(&record);
                    prepared.parts[packet.parts[k]].color_admitted=true;
                    ++submitted;
                }
            }
            // Merged units (#65): each group is one colour record; its member
            // parts are admitted with it and skip the per-part record below.
            const bool merged=g_merge_character_parts && !prepare_light_only && sample.values.unit &&
                retained_visual_merged_groups(sample) && !sample.prepared->merged_groups.empty() &&
                prepared.contribution.color;
            if(merged && !reused) {
                auto& groups=sample.prepared->merged_groups;
                for(std::size_t group=0;group<groups.size() && commit;++group) {
                    bool visible=false;
                    for(const auto part:groups[group].parts)visible|=prepared.parts[part].visible;
                    if(!visible)continue;
                    NativePreparedPartSubmission* record=nullptr;
                    if(!retained_visual_group_submission(sample,group,prepared.bounds,{true,true,0},
                        frame.values->time,fog,record)) {
                        reject_owned_visual("submission","merged_record",&sample);commit=false;break;
                    }
                    frame.selected.push_back(record);++submitted;
                    for(const auto part:groups[group].parts)
                        if(prepared.parts[part].visible)prepared.parts[part].color_admitted=true;
                }
            }
            if(!reused && commit)for(std::size_t candidate=prepared.candidate_begin;
                candidate<prepared.candidate_end;++candidate) {
                const auto part_index=frame.candidates[candidate].part;
                if (part_index>=kMergedPartBase) continue;
                if (!prepared.parts[part_index].visible) continue;
                if (merged && owned_merged_member(sample,part_index)) continue;
                const bool casts=prepare_light_only && prepared.parts[part_index].casts;
                auto contribution = prepared.contribution;
                if (!casts) contribution.light_mask = 0;
                if (!contribution.color && !contribution.light_mask) continue;
                const OwnedVisualPartPreparation* part_value=nullptr;
                if (!prepare_owned_visual_part(i,part_index,part_value)) {
                    reject_owned_visual("submission","part_envelope",&sample); commit=false; break;
                }
                const auto& part_bounds=part_value->bounds;
                contribution=part_value->contribution;
                if(!casts)contribution.light_mask=0;
                if (!contribution.color && !contribution.light_mask) continue;
                NativePreparedPartSubmission* record=nullptr;
                const bool recorded=retained_visual_part_submission(sample,part_index,part_bounds,contribution,
                    frame.values->time,fog,record);
                if (!recorded) {
                    reject_owned_visual("submission","part_record",&sample);commit=false;break;
                }
                frame.selected.push_back(record);
                prepared.parts[part_index].color_admitted = contribution.color;
                ++submitted;
            }
            if(!reused && commit && packet_eligible) {
                // Store only an exact selection: every visible part selected
                // for color, none time-scrolled, and all inside the frustum.
                packet.valid=false;
                std::size_t visible_parts=0;
                for(const auto& part:prepared.parts)visible_parts+=part.visible;
                bool storable=visible_parts==frame.selected.size();
                NativeWorldBounds bounds{};
                try {
                    packet.parts.clear();packet.values_revisions.clear();packet.part_bounds.clear();
                    for(std::size_t k=0;k<frame.selected.size() && storable;++k) {
                        const auto* record=frame.selected[k];
                        std::size_t index=0;
                        while(index<sample.prepared->parts.size() && &sample.prepared->parts[index].submission!=record)++index;
                        const auto type=index<sample.assembly->parts.size()?sample.assembly->parts[index].texture_type:0u;
                        if(index>=sample.prepared->parts.size() || !record->contribution.color ||
                           type==13 || type==14 || type==16){storable=false;break;}
                        packet.parts.push_back(static_cast<std::uint32_t>(index));
                        packet.values_revisions.push_back(record->values_revision);
                        packet.part_bounds.push_back(record->bounds);
                        if(!bounds.valid)bounds=record->bounds;
                        else for(unsigned axis=0;axis<3;++axis){
                            bounds.minimum[axis]=(std::min)(bounds.minimum[axis],record->bounds.minimum[axis]);
                            bounds.maximum[axis]=(std::max)(bounds.maximum[axis],record->bounds.maximum[axis]);
                        }
                    }
                    if(storable && bounds.valid && retained_world_bounds_inside(bounds,world_to_clip)) {
                        packet.visible.resize(prepared.parts.size());
                        for(std::size_t p=0;p<prepared.parts.size();++p)packet.visible[p]=prepared.parts[p].visible;
                        packet.assembly=sample.assembly.get();packet.assembly_revision=sample.assembly->revision;
                        packet.pose_generation=sample.prepared->derived_pose_generation;
                        packet.values=sample.values;packet.fog=fog;packet.quest_light=sample.quest_light_active;
                        packet.bounds=bounds;packet.valid=true;
                    }
                } catch(...) {packet.valid=false;}
            }
            if(commit&&!frame.selected.empty()){
                NativeGeometryResult result{};
                commit=submit_owned_visual_records(sample,frame.selected.data(),static_cast<UINT>(frame.selected.size()),
                    world_to_clip,frame.serial,0,result,true);
                if(!commit)reject_owned_visual("submission","sink_rejected",&sample);
            }
            prepared.submitted = commit;
            if (commit && submitted && !prepare_light_only && sample.values.unit &&
                !sample.values.dead && !sample.values.hidden && !sample.values.invisible && prepared.contribution.color &&
                combat_ui_needs_plate(sample.resource.signed_id)) {
                auto& retained=*sample.prepared;
                const bool new_appearance=retained.combat_anchor_assembly.lock()!=sample.assembly;
                std::array<float,3> p{};
                if (retained_visual_combat_anchor(sample,p)) {
                    const auto& m = world_to_clip;
                    const float x = m[0]*p[0] + m[1]*p[1] + m[2]*p[2] + m[3];
                    const float y = m[4]*p[0] + m[5]*p[1] + m[6]*p[2] + m[7];
                    const float z = m[8]*p[0] + m[9]*p[1] + m[10]*p[2] + m[11];
                    const float w = m[12]*p[0] + m[13]*p[1] + m[14]*p[2] + m[15];
                    if (!combat_pass_checked) {
                        combat_pass_checked=true;
                        combat_pass_ready=g_native_sink_frame_state && g_native_sink_frame_state(g_native_sink_context,combat_pass);
                    }
                    if (w > 0 && z >= 0 && z <= w && std::abs(x) <= w && std::abs(y) <= w && combat_pass_ready)
                        combat_ui_publish_plate(sample.resource.signed_id,
                            combat_pass.viewport[0] + (x/w + 1) * .5f * combat_pass.viewport[2],
                            combat_pass.viewport[1] + (1 - y/w) * .5f * combat_pass.viewport[3]);
                } else if (new_appearance) combat_ui_set_status("Combat plate omitted: reference body bounds unavailable.");
            }
            if (commit) frame.submitted_parts += submitted;
            else { report_object_plan_failure("owned_scene_part_submission"); success = false; }
        }
    } catch (...) {
        if (batch) g_native_sink_end_batch(g_native_sink_context,true);
        report_object_plan_failure("owned_scene_submission_exception");
        reject_owned_visual("submission","exception");
        return false;
    }
    if (batch) g_native_sink_end_batch(g_native_sink_context,true);
    return success;
}

// Temporary automation aid: when the named event is set, write every unit's
// id, model, world position and NDC position (current camera) next to the
// game executable, so test routes can click objects instead of pixels.
namespace {
void dump_owned_units_on_request(const std::array<float,16>& world_to_clip) noexcept {
    static HANDLE request=CreateEventA(nullptr,FALSE,FALSE,"Local\\EID3D11_DumpUnits");
    if(!request || WaitForSingleObject(request,0)!=WAIT_OBJECT_0)return;
    const auto& frame=g_owned_visual_world_frame;
    if(!frame.values)return;
    char path[MAX_PATH]{};
    if(!GetModuleFileNameA(nullptr,path,MAX_PATH))return;
    char* slash=std::strrchr(path,'\\');if(!slash)return;
    strcpy_s(slash+1,MAX_PATH-static_cast<std::size_t>(slash+1-path),"eid3d11_units.txt");
    FILE* file=nullptr;if(fopen_s(&file,path,"w") || !file)return;
    for(const auto& sample:frame.values->entities) {
        if(!sample.values.unit)continue;
        const auto& p=sample.pose.root;
        const auto& m=world_to_clip;
        const float x=m[0]*p[0]+m[1]*p[1]+m[2]*p[2]+m[3],y=m[4]*p[0]+m[5]*p[1]+m[6]*p[2]+m[7];
        const float w=m[12]*p[0]+m[13]*p[1]+m[14]*p[2]+m[15];
        const char* name=sample.assembly && sample.assembly->skeleton ?
            sample.assembly->skeleton->visual_reference.c_str() : "-";
        // Part/texture/material mix (character part-merge sizing).
        std::size_t parts=0;std::vector<std::uint64_t> surfaces,materials;
        if(sample.assembly)for(const auto& part:sample.assembly->parts){
            if(part.explicitly_hidden)continue;++parts;
            surfaces.push_back(part.surface?part.surface->id:0);
            materials.push_back((std::uint64_t(part.material_type)<<48)^(std::uint64_t(part.texture_type)<<40)^
                (std::uint64_t(part.material_flags)<<20)^part.figure_flags);
        }
        const auto distinct=[](std::vector<std::uint64_t>& v){
            std::sort(v.begin(),v.end());return static_cast<std::size_t>(std::unique(v.begin(),v.end())-v.begin());};
        std::fprintf(file,"%d %s %.2f %.2f %.2f %.5f %.5f %d parts=%zu textures=%zu materials=%zu\n",
            sample.lifecycle.id,name[0]?name:"-",p[0],p[1],p[2],w>1e-5f?x/w:0.f,w>1e-5f?y/w:0.f,w>1e-5f?1:0,
            parts,distinct(surfaces),distinct(materials));
    }
    std::fclose(file);
}

} // namespace
// The seam's submissions once its shadow frame is prepared (both modes).
#if !defined(EID3D11_EMBEDDED)
bool finish_owned_visual_scene(void* fog_controller,const std::array<float,16>& world_to_clip) {
    auto& frame=g_owned_visual_world_frame;
    if(!frame.shadow_ready || frame.coverage.world_to_clip!=world_to_clip) {
        reject_owned_visual("submission","shadow_frame_unavailable");
        report_object_plan_failure("owned_shadow_frame_unavailable");return false;
    }
    CityRetainedOwnerGuard terrain_timing(CityRetainedOwner::terrain_base);
    const bool terrain_submitted = submit_pending_owned_terrain_base();
    terrain_timing.finish();
    if (!terrain_submitted) {
        reject_owned_visual("submission","terrain_base_unavailable");
    }
    if(frame.color_submitted)return terrain_submitted;
    frame.color_submitted=true;
    CityRetainedOwnerGuard color_timing(CityRetainedOwner::color_submission);
    const bool color_submitted = submit_owned_visual_color(fog_controller,false,true);
    color_timing.finish();
    dump_owned_units_on_request(world_to_clip);
    return terrain_submitted && color_submitted;
}
#endif // !EID3D11_EMBEDDED
namespace {

// Scene-thread presentation serials stay apart from the game thread's (both
// reach the renderer: game-thread previews and HUD figures, scene-thread world).
constexpr std::uint64_t kScenePresentationSerialBase = 1ull << 62;

} // namespace
// Scene thread: install a queued world command's inputs; clear afterwards.
void install_owned_scene_inputs(void* sinks,const BridgeSceneHandoff& handoff,
    const std::vector<std::shared_ptr<SurfaceImage>>* textures,
    const std::vector<std::shared_ptr<const OwnedVisualSurface>>* shadow_textures,
    const std::vector<std::int16_t>* fog,std::uint64_t revision) noexcept {
    if(handoff.install_sinks)handoff.install_sinks(sinks);
    g_scene_sinks=sinks;g_scene_bind_texture=handoff.bind_texture;
    g_scene_terrain_textures=textures;g_scene_terrain_shadow_textures=shadow_textures;
    g_scene_fog_table=fog;g_scene_capture_revision=revision;
    if(g_object_render_presentation_serial<kScenePresentationSerialBase)
        g_object_render_presentation_serial=kScenePresentationSerialBase;
}
void clear_owned_scene_inputs(const BridgeSceneHandoff& handoff) noexcept {
    g_scene_sinks=nullptr;g_scene_bind_texture=nullptr;
    g_scene_terrain_textures=nullptr;g_scene_terrain_shadow_textures=nullptr;
    g_scene_fog_table=nullptr;g_scene_capture_revision=0;g_scene_fog_revision=0;
    if(handoff.install_sinks)handoff.install_sinks(nullptr);
}

#if !defined(EID3D11_EMBEDDED)
void run_owned_visual_scene(OwnedSceneCommand& command) noexcept {
    const frame_acct::Scope accounted(frame_acct::scene_world);
    install_owned_scene_inputs(command.sinks.get(),command.handoff,&command.terrain_textures,
        &command.terrain_shadow_textures,&command.fog,command.inputs.shadow_source_revision);
    g_scene_fog_revision=command.fog_revision;
    g_owned_terrain_frame=std::move(command.terrain);
    g_native_world_lighting_frame=command.lighting;
    // The captured pass is the game thread's view of the shadow switch; the
    // renderer that draws applied the switch at its own BeginScene and has the
    // last word (installed sinks' frame state). Shadows off there means no sun
    // pass at all, so the world never waits for one it refuses (#204).
    {
        NativeFrameState executor{};
        if(command.inputs.pass.shadows_enabled && g_native_sink_frame_state &&
           g_native_sink_frame_state(g_native_sink_context,executor) && !executor.shadows_enabled)
            command.inputs.pass.shadows_enabled=false;
    }
    auto& frame=g_owned_visual_world_frame;
    frame.active=true;frame.shadow_attempted=true;frame.shadow_source_diagnostic={};
    frame.camera_anchor_z=command.camera_anchor_z;
    bool ready=false;
    try {
        ready=prepare_owned_world_shadows_from(std::move(command.inputs),command.world_to_clip);
    } catch(const char* reason) {
        startup_failure(StartupStage::shadow_prepare,reason);
        reject_owned_visual("shadow_prepare",reason);
        report_object_plan_failure(reason);
    } catch(...) {
        startup_failure(StartupStage::shadow_prepare,"owned_shadow_allocation");
        report_object_plan_failure("owned_shadow_allocation");
    }
    if(!ready) {
        frame.active=true;frame.shadow_attempted=true;frame.shadow_ready=false;
        retire_owned_shadow_maps();
    } else {
        g_scene_footprints=&command.footprints;
        try { (void)finish_owned_visual_scene(nullptr,command.world_to_clip); }
        catch(...) { report_object_plan_failure("owned_scene_frame_allocation"); }
        g_scene_footprints=nullptr;
    }
    clear_owned_scene_inputs(command.handoff);
}



#endif // !EID3D11_EMBEDDED
} // namespace owned
