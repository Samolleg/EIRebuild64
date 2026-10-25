#include "world_mist_grid.h"
#include "model_preview_producer.h"
#ifdef EID3D11_EMBEDDED
#include "embedded_host.h"
#endif
// The bridge's scene half: its state and game-free helpers. Moved from
// engine_bridge.cpp so the x64 host links the scene half
// (owned_world_prepare/shadows/consumers.cpp) too: the native sinks and their
// setters, the scene command's installed inputs, shadow/point-shadow/rain/
// load-reveal/part-merge settings, failure reporting, and the base terrain
// submission (records, per-frame commands, the persistent terrain set). None
// of it reads game memory or calls the game; the bridge diagnostics it raises
// go through append_bridge_event (bridge.jsonl in the DLL, host.log in the
// host). Reads only the frame copies (owned_world_frame.h); host-buildable.
#include "owned_world_frame.h"
#include "neighbour_maps.h"
#include "owned_terrain_views.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace owned {

thread_local BridgeFrameSample g_bridge_sample{};

OwnedShadowSettings g_owned_shadow_settings{};
std::uint64_t g_owned_shadow_settings_revision = 1;
float g_point_shadow_merge_distance=1.5f; // world units; 0 disables shadow merging
// Moving-caster overlays: this many lamps nearest the view focus refresh every
// frame, the others every third frame (staggered); 0 refreshes all every frame.
int g_point_shadow_fast_overlays=4;

std::int64_t bridge_now() {
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return value.QuadPart;
}

std::int64_t bridge_frequency() {
    static const std::int64_t frequency = [] {
        LARGE_INTEGER value{};
        QueryPerformanceFrequency(&value);
        return std::max<std::int64_t>(1, value.QuadPart);
    }();
    return frequency;
}

thread_local void* g_native_sink_context = nullptr;
thread_local NativeGeometrySubmitFn g_native_sink_submit = nullptr;
thread_local NativeGeometryBatchBeginFn g_native_sink_begin_batch = nullptr;
thread_local NativeGeometryBatchEndFn g_native_sink_end_batch = nullptr;
thread_local NativeGeometryObjectBeginFn g_native_sink_begin_object = nullptr;
thread_local NativeGeometryObjectEndFn g_native_sink_end_object = nullptr;
thread_local NativeGeometryFrameStateFn g_native_sink_frame_state = nullptr;
thread_local NativeWorldLightingSetFn g_native_sink_world_lighting = nullptr;
thread_local NativeShadowBeginFn g_native_sink_shadow_begin = nullptr;
thread_local NativeShadowCullProjectionFn g_native_sink_shadow_cull_projection = nullptr;
thread_local NativeShadowTerrainBakeFn g_native_sink_shadow_terrain_bake = nullptr;
thread_local NativeShadowStaticDepthFn g_native_sink_shadow_static_depth = nullptr;
thread_local NativeShadowEndFn g_native_sink_shadow_end = nullptr;
thread_local NativeShadowSourceDiagnosticsFn g_native_sink_shadow_diagnostics = nullptr;
thread_local NativePointLightsSetFn g_native_sink_point_lights = nullptr;
thread_local NativePointShadowBeginFn g_native_sink_point_shadow_begin = nullptr;
thread_local NativeRainOcclusionBeginFn g_native_sink_rain_begin = nullptr;
thread_local NativeRainOcclusionEndFn g_native_sink_rain_end = nullptr;
// Owned precipitation shelter map: the renderer's request and what the
// bridge last baked (or attempted) for it.
RainOcclusionRequest g_rain_request{};
thread_local NativePointShadowEndFn g_native_sink_point_shadow_end = nullptr;
thread_local NativePointShadowBatchFn g_native_sink_point_shadow_batch = nullptr;
thread_local NativePointShadowPublishFn g_native_sink_point_shadow_publish = nullptr;
thread_local NativePointShadowAllocateFn g_native_sink_point_shadow_allocate = nullptr;
thread_local NativePointShadowReleaseFn g_native_sink_point_shadow_release = nullptr;

void bridge_set_native_geometry_sink(void* context, NativeGeometrySubmitFn submit,
                                     NativeGeometryBatchBeginFn begin_batch,
                                     NativeGeometryBatchEndFn end_batch,
                                     NativeGeometryObjectBeginFn begin_object,
                                     NativeGeometryObjectEndFn end_object,
                                     NativeGeometryFrameStateFn frame_state,
                                     NativeWorldLightingSetFn world_lighting,
                                     NativeShadowBeginFn shadow_begin,
                                     NativeShadowEndFn shadow_end,
                                     NativeShadowSourceDiagnosticsFn shadow_diagnostics,
                                     NativePointLightsSetFn point_lights,
                                     NativePointShadowBeginFn point_shadow_begin,
                                     NativePointShadowEndFn point_shadow_end,
                                     NativeShadowStaticDepthFn static_depth,
                                     NativePointShadowBatchFn point_shadow_batch,
                                     NativePointShadowPublishFn point_shadow_publish,
                                     NativePointShadowAllocateFn point_shadow_allocate,
                                     NativePointShadowReleaseFn point_shadow_release,
                                     NativeShadowCullProjectionFn shadow_cull_projection,
                                     NativeShadowTerrainBakeFn terrain_bake) {
    if (!context && g_native_sink_context && g_native_sink_shadow_end)
        g_native_sink_shadow_end(g_native_sink_context,false);
    if (!context && g_native_sink_context && g_native_sink_point_shadow_end)
        g_native_sink_point_shadow_end(g_native_sink_context,false);
    g_native_sink_context = context;
    g_native_sink_submit = submit;
    g_native_sink_begin_batch = begin_batch;
    g_native_sink_end_batch = end_batch;
    g_native_sink_begin_object = begin_object;
    g_native_sink_end_object = end_object;
    g_native_sink_frame_state = frame_state;
    g_native_sink_world_lighting = world_lighting;
    g_native_sink_shadow_begin = shadow_begin;
    g_native_sink_shadow_cull_projection = shadow_cull_projection;
    g_native_sink_shadow_terrain_bake = terrain_bake;
    g_native_sink_shadow_static_depth = static_depth;
    g_native_sink_shadow_end = shadow_end;
    g_native_sink_shadow_diagnostics = shadow_diagnostics;
    g_native_sink_point_lights = point_lights;
    g_native_sink_point_shadow_begin = point_shadow_begin;
    g_native_sink_point_shadow_end = point_shadow_end;
    g_native_sink_point_shadow_batch = point_shadow_batch;
    g_native_sink_point_shadow_publish = point_shadow_publish;
    g_native_sink_point_shadow_allocate = point_shadow_allocate;
    g_native_sink_point_shadow_release = point_shadow_release;
    g_native_world_lighting_frame = {};
}

void bridge_set_rain_occlusion_sink(NativeRainOcclusionBeginFn begin,NativeRainOcclusionEndFn end) noexcept {
    g_native_sink_rain_begin = begin;
    g_native_sink_rain_end = end;
}

void bridge_set_rain_occlusion_request(const RainOcclusionRequest& request) noexcept {
    g_rain_request = request;
}
bool same_lifecycle_identity(const LifecycleIdentity& left,
                             const LifecycleIdentity& right) noexcept {
    return left.id == right.id && left.generation == right.generation &&
        left.positive_world_epoch == right.positive_world_epoch;
}

bool submit_owned_visual_records(const RetainedVisualSample& sample,
    const NativePreparedPartSubmission* const* parts,std::uint32_t count,
    const std::array<float,16>& clip,std::uint64_t serial,std::uintptr_t wound,
    NativeGeometryResult& result,bool retained_color_diagnostic) {
    NativePreparedSubmission submission;
    submission.parts=parts;submission.count=count;submission.pose=&sample.prepared->gpu_pose;
    submission.identity={sample.lifecycle.id,sample.lifecycle.generation,sample.lifecycle.positive_world_epoch,
        sample.resource.serial,0,0,serial,true};
    submission.world_to_clip=&clip;submission.wound_surface_interface=wound;
    submission.retained_color_diagnostic=retained_color_diagnostic;
    NativeGeometryView dispatch;dispatch.prepared_submission=&submission;
    return g_native_sink_submit&&g_native_sink_submit(g_native_sink_context,dispatch,result);
}

thread_local std::uint64_t g_object_render_presentation_serial = 0;

// Scene thread: the last installed frame values. While installed, the
// bridge's game-reading pulls answer from them (game memory is read only on
// the game thread, when the values are captured).
thread_local bool g_scene_frame_values_installed = false;
// Scene thread: inputs of the queued world command being run (null outside).
thread_local const std::vector<std::int16_t>* g_scene_fog_table = nullptr;
// The neighbour border fog revision the scene command's fog table was computed
// at (scene thread); see current_border_fog_revision.
thread_local std::uint64_t g_scene_fog_revision = 0;
// The border fog revision the fog bytes of this preparation belong to. On the
// scene thread the bytes come from the command's table, computed on the game
// thread before the scene thread runs; stamping a packet with the live
// revision instead let a neighbour publication between the two mark stale
// bytes current, and those objects kept the old fog until their own state
// changed (zone3obr: trees inside the zone7 seam stayed at 255 while a rock
// beside them, re-registered for a flag change, took the seam fog).
std::uint64_t current_border_fog_revision() noexcept {
    return g_scene_fog_table ? g_scene_fog_revision : neighbour_border_fog_revision();
}

thread_local const std::vector<std::shared_ptr<SurfaceImage>>* g_scene_terrain_textures = nullptr;
thread_local const std::vector<std::shared_ptr<const OwnedVisualSurface>>* g_scene_terrain_shadow_textures = nullptr;
thread_local void (*g_scene_bind_texture)(void*, const std::shared_ptr<SurfaceImage>&) = nullptr;
thread_local void* g_scene_sinks = nullptr;
thread_local const GroundCoverFootprintSnapshot* g_scene_footprints = nullptr;
// The retained scene's shadow revision when the command's values were
// copied: journal consumers acknowledge only through it.
thread_local std::uint64_t g_scene_capture_revision = 0;
std::uint64_t owned_scene_ack_revision(std::uint64_t revision) noexcept {
    return g_scene_capture_revision ? (std::min)(revision, g_scene_capture_revision) : revision;
}


// Temporary corpse-shadow question only: disabled unless explicitly enabled
// for the next focused player F11. No native reads or visual decisions added.
#ifndef EID3D11_EMBEDDED
bool hovered_shadow_observation_enabled() noexcept {
    static const bool enabled=[](){char value[8]{};return GetEnvironmentVariableA("EI_RENDERER_CORPSE_SHADOW_DIAGNOSTIC",value,sizeof(value))>0 && value[0]=='1';}();
    return enabled;
}
#else
bool hovered_shadow_observation_enabled() noexcept { return false; }
#endif

// frame={} for everything else (values and prepared included).
void clear_owned_visual_world_frame(OwnedVisualWorldFrame& frame) noexcept {
    OwnedVisualWorldFrame kept{};   // value-initialised, as frame={}
    visit_owned_visual_frame_scratch(frame,[](auto& vector) noexcept {vector.clear();});
    kept.shadow_casters.swap(frame.shadow_casters);kept.shadow_receivers.swap(frame.shadow_receivers);
    kept.static_receivers.swap(frame.static_receivers);kept.static_drawn.swap(frame.static_drawn);
    kept.candidates.swap(frame.candidates);kept.color_order.swap(frame.color_order);
    kept.light_only_order.swap(frame.light_only_order);kept.submission_order.swap(frame.submission_order);
    kept.selected.swap(frame.selected);kept.depth_selected.swap(frame.depth_selected);
    kept.shadow_time_active_visuals.swap(frame.shadow_time_active_visuals);
    frame=std::move(kept);
}
namespace {
volatile LONG g_owned_visual_failure_reported = 0;
volatile LONG g_owned_shadow_failure_reported = 0;

bool take_owned_shadow_failure_detail(const char* stage,const NativePreparedIdentity& identity,
    const NativeGeometryResult* result,const NativeOwnedTextureView* texture,char (&detail)[512]) noexcept {
    // Independent of the earlier construction/appearance failure slot. No reset
    // on map, frame or hook reinstall: the diagnostic writes once per process.
    if(InterlockedCompareExchange(&g_owned_shadow_failure_reported,1,0)!=0)return false;
    if(identity.terrain_source)
        std::snprintf(detail,sizeof(detail),"source=terrain stage=%s sector=%d source_generation=%llu source_revision=%llu material=%u subpart_mask=%u",
            stage,identity.signed_id,identity.lifecycle_generation,identity.plan_generation,
            identity.part_index>>4u,identity.part_index&15u);
    else
        std::snprintf(detail,sizeof(detail),"source=visual stage=%s id=%d resource=%llu membership=%llu world=%llu part=%u",
            stage,identity.signed_id,identity.plan_generation,identity.lifecycle_generation,
            identity.world_epoch,identity.part_index);
    auto append=[&](const char* format,auto... values) {
        const auto length=std::strlen(detail);
        std::snprintf(detail+length,sizeof(detail)-length,format,values...);
    };
    if(result && result->failure_operation)append(" operation=%s",result->failure_operation);
    if(result && FAILED(result->failure_hresult))
        append(" hr=0x%08lx",static_cast<unsigned long>(result->failure_hresult));
    // Borrow only the immutable input already handed to the failed operation.
    // No texture lookup, residency request, pixel read or diagnostic scan.
    if(result && result->failure_texture && texture) {
        if(texture->surface) {
            const auto& source=*texture->surface;const auto& format=source.format;
            append(" texture=surface width=%u height=%u pitch=%d flags=0x%08lx fourcc=0x%08lx bpp=%lu r=0x%08lx g=0x%08lx b=0x%08lx a=0x%08lx",
                source.width,source.height,source.pitch,format.dwFlags,format.dwFourCC,
                format.dwRGBBitCount,format.dwRBitMask,format.dwGBitMask,format.dwBBitMask,format.dwRGBAlphaBitMask);
        } else if(texture->mips && texture->mip_count) {
            append(" texture=owned width=%u height=%u native_format=%u mips=%u",
                texture->mips[0].width,texture->mips[0].height,
                static_cast<unsigned>(texture->format),texture->mip_count);
        } else append(" texture=descriptor_unavailable");
    }
    return true;
}

} // namespace
bool reject_owned_shadow_part(const char* stage,const NativePreparedIdentity& identity,
    const NativeGeometryResult* result,const NativeOwnedTextureView* texture) noexcept {
    char detail[512]{};
    if(take_owned_shadow_failure_detail(stage,identity,result,texture,detail))
        append_bridge_event("owned_shadow_first_failure",detail,true);
    return false;
}

void log_owned_visual_failure(const OwnedVisualRejection& rejection) noexcept {
    if (!rejection.stage || InterlockedCompareExchange(&g_owned_visual_failure_reported,1,0) != 0) return;
    char detail[512]{};
    format_owned_visual_failure_detail(rejection,detail,sizeof(detail));
    append_bridge_event("owned_scene_first_failure",detail,true);
}

void reject_owned_visual(const char* stage, const char* reason,
    const RetainedVisualSample* sample, bool failure, std::uint32_t class_flags) noexcept {
    auto& frame = g_owned_visual_world_frame;
    if (!frame.diagnostic.profile_frame) return;
    OwnedVisualRejection rejection{};
    rejection.stage=stage; rejection.reason=reason; rejection.class_flags=class_flags;
    rejection.draw_undetected=frame.draw_undetected_units;
    if (sample) {
        if (std::strcmp(stage,"values")==0) rejection.reason_scope=sample->failure_scope;
        rejection.signed_id=sample->resource.signed_id;
        rejection.resource_serial=sample->resource.serial;
        rejection.membership_generation=sample->lifecycle.generation;
        rejection.status=static_cast<std::uint32_t>(sample->status);
        rejection.object_flags=sample->values.object_flags; rejection.unit=sample->values.unit;
        rejection.hidden=sample->values.hidden;
    }
    if (!frame.diagnostic.first_rejection.stage) frame.diagnostic.first_rejection=rejection;
    if (failure) log_owned_visual_failure(rejection);
}

namespace {
volatile LONG g_object_plan_failure_reported = 0;
} // namespace
void report_object_plan_failure(const char* stage) noexcept {
#ifdef EID3D11_EMBEDDED
    embedded_report_render_failure("object_render_plan_failure", stage);
#else
    if (InterlockedCompareExchange(&g_object_plan_failure_reported, 1, 0) == 0)
        append_bridge_event("object_render_plan_first_failure",
                            stage ? stage : "unknown", true);
#endif
}

void report_world_point_light_omission(const char* reason) noexcept {
    static volatile LONG reported = 0;
    if (InterlockedCompareExchange(&reported, 1, 0) == 0)
        append_bridge_event("owned_world_point_light_omitted",
            reason ? reason : "unknown", true);
}

std::atomic<bool> g_load_reveal_enabled{false};
std::atomic<std::uint64_t> g_load_reveal_revealed_epoch{0};
bool load_reveal_held() noexcept {
    const std::uint64_t epoch = bridge_current_world_epoch();
    return g_load_reveal_enabled.load(std::memory_order_relaxed) && epoch &&
        epoch != g_load_reveal_revealed_epoch.load(std::memory_order_relaxed);
}

#if !defined(EID3D11_EMBEDDED)
void bridge_set_load_reveal(bool enabled, std::uint64_t revealed_epoch) noexcept {
    g_load_reveal_enabled.store(enabled, std::memory_order_relaxed);
    g_load_reveal_revealed_epoch.store(revealed_epoch, std::memory_order_relaxed);
}

#endif // !EID3D11_EMBEDDED
// Far lamp memory and the mobile-light rule live in world_lamp_memory.h.
WorldLampMemory<RetainedWorldPointLight> g_world_lamp_memory;

// Character part merge (#65): units draw one colour record per merged group.
bool g_merge_character_parts = true;

bool owned_unit_merged(const RetainedVisualSample& sample) {
    return g_merge_character_parts && sample.values.unit && retained_visual_merged_groups(sample) &&
        !sample.prepared->merged_groups.empty();
}
bool owned_merged_member(const RetainedVisualSample& sample,std::size_t part) noexcept {
    const auto& map=sample.prepared->merged_part_group;
    return part<map.size() && map[part]>=0;
}
void bridge_set_merge_character_parts(bool enabled) noexcept { g_merge_character_parts = enabled; }


void bridge_set_shadow_settings(std::uint32_t map_side,
    std::uint8_t close_cascades,bool far_shadows,bool point_shadows,bool sun_shadows) noexcept {
    if (g_owned_shadow_settings.map_side == map_side &&
        g_owned_shadow_settings.close_cascades == close_cascades &&
        g_owned_shadow_settings.far_shadows == far_shadows &&
        g_owned_shadow_settings.point_shadows == point_shadows &&
        g_owned_shadow_settings.sun_shadows == sun_shadows) return;
    g_owned_shadow_settings={map_side,close_cascades,far_shadows,point_shadows,sun_shadows};
    if (++g_owned_shadow_settings_revision == 0) ++g_owned_shadow_settings_revision;
}

void bridge_set_point_shadow_fast_overlays(int count) noexcept { g_point_shadow_fast_overlays=count<0?0:count; }
void bridge_set_point_shadow_merge_distance(float distance) noexcept {
    g_point_shadow_merge_distance=std::isfinite(distance)&&distance>0?distance:0;
}

bool current_base_terrain_quality_reference(const TerrainCpuAsset*& source,
    const std::array<float,16>*& transform,std::uint64_t& generation) noexcept {
    source=nullptr;transform=nullptr;generation=0;
    const auto& terrain=g_owned_terrain_frame;
    if(!terrain.world_serial || !terrain.world_transform_valid ||
       terrain.front_end.status!=OwnedTerrainPacketStatus::ready ||
       !terrain.source_values.terrain || !terrain.front_end.spatial_ready ||
       terrain.front_end.source!=terrain.source_values.terrain ||
       terrain.front_end.inputs.source_generation!=terrain.source_values.source_generation ||
       terrain.front_end.inputs.source_revision!=terrain.source_values.source_revision ||
       terrain.front_end.inputs.world_frame_serial!=terrain.world_serial ||
       terrain.front_end.spatial_object_to_world!=terrain.object_to_world)return false;
    source=terrain.source_values.terrain.get();
    transform=&terrain.object_to_world;
    generation=terrain.source_values.source_generation;
    return true;
}
namespace {

bool decode_owned_terrain_source(void* opaque,
                                 NativeGeometryDecodeTarget& target) {
    const auto* context = static_cast<const OwnedTerrainSourceDecodeContext*>(opaque);
    if (!owned_terrain_decode_valid(context, target)) return false;
    if (g_bridge_sample.active) {
        ++g_bridge_sample.terrain_source_topology_decodes;
        g_bridge_sample.terrain_source_topology_vertices +=
            context->command.vertex_count;
    }
    return decode_owned_terrain_tiles(*context, target);
}

} // namespace
bool source_terrain_world_record(const RetainedTerrainSourceSnapshot& source,
    OwnedTerrainFramePacket& packet, const OwnedTerrainCommand& command,
    const OwnedTerrainDynamicSector* color, const NativeContribution& contribution,
    OwnedTerrainSourceDecodeContext& context, NativeGeometryView*& output,
    bool alpha_coverage) {
    const auto before=packet.spatial_record_reuses;
    const bool result=prepare_owned_terrain_world_record(source,packet,command,color,
        contribution,context,output,alpha_coverage);
    if(g_bridge_sample.active)
        g_bridge_sample.terrain_spatial_bounds_reused+=packet.spatial_record_reuses-before;
    if(output)output->decode=&decode_owned_terrain_source; // Preserve retail profiling.
    return result;
}

bool apply_owned_terrain_world_transform(NativeGeometryView& view) noexcept {
    const auto& frame=g_owned_terrain_frame;
    if(!frame.world_transform_valid || !frame.front_end.spatial_ready ||
       frame.front_end.spatial_object_to_world!=frame.object_to_world)return false;
    view.world.object_to_world=view.lighting.object_to_world=frame.object_to_world;
    view.world.world_to_clip=frame.world_to_clip;
    return true;
}

namespace {

const char* owned_terrain_omission_reason(
    OwnedTerrainRangeAction action) noexcept {
    switch (action) {
    case OwnedTerrainRangeAction::omit_pending_source: return "source_pending";
    case OwnedTerrainRangeAction::omit_invalid: return "invalid_or_submit_failure";
    case OwnedTerrainRangeAction::omit_stale: return "stale_frame_packet";
    case OwnedTerrainRangeAction::omit_wrong_order: return "unexpected_pass_order";
    default: return "unknown";
    }
}

} // namespace
thread_local const char* g_owned_terrain_failure_stage="range.packet_invalid";
thread_local HRESULT g_owned_terrain_failure_hresult=S_OK;

void report_owned_terrain_omission(OwnedTerrainRangeAction action) noexcept {
    if (g_bridge_sample.active) ++g_bridge_sample.terrain_owned_omitted_ranges;
    static thread_local OwnedTerrainRangeAction last =
        OwnedTerrainRangeAction::submit_base;
    static thread_local std::uint64_t last_generation = UINT64_MAX;
    const std::uint64_t generation =
        g_owned_terrain_frame.front_end.inputs.source_generation;
    if (last == action && last_generation == generation) return;
    last = action;
    last_generation = generation;
    char detail[240];
    if(action==OwnedTerrainRangeAction::omit_invalid) {
        std::snprintf(detail,sizeof(detail),"invalid_or_submit_failure stage=%s hr=%08lx",
            g_owned_terrain_failure_stage,static_cast<unsigned long>(g_owned_terrain_failure_hresult));
        append_bridge_event("owned_terrain_omitted",detail,true);
    } else append_bridge_event("owned_terrain_omitted",owned_terrain_omission_reason(action),true);
}

#if !defined(EID3D11_EMBEDDED)
bool submit_source_owned_terrain_commands(
    void* renderer, const std::vector<OwnedTerrainCommand>& commands,
    const NativeOwnedFixedState* captured_state) {
    g_owned_terrain_failure_stage="submit.world_lighting";
    g_owned_terrain_failure_hresult=S_OK;
    const auto lighting=publish_native_world_lighting(renderer);
    if(lighting!=FigureFallbackReason::none) {
        g_owned_terrain_failure_stage=figure_fallback_reason_name(lighting);return false;
    }
    g_owned_terrain_failure_stage="submit.begin_batch";
    if (!g_native_sink_begin_batch(g_native_sink_context,
                                   SemanticKind::terrain))
        return false;
    bool batch_active = true;
    try {
        std::uint16_t bound_material = UINT16_MAX;
        for (const auto& command : commands) {
            g_owned_terrain_failure_stage="submit.source_sector_or_resources";
            const auto& source=g_owned_terrain_frame.source_values;
            if (!source.terrain || !source.geometry || !source.prepared ||
                command.source_sector>=source.terrain->sectors.size()) throw false;
            const OwnedTerrainDynamicSector* color=nullptr;
            if (command.source_sector<g_owned_terrain_frame.dynamic.size() &&
                g_owned_terrain_frame.dynamic[command.source_sector].captured)
                color=&g_owned_terrain_frame.dynamic[command.source_sector];
            const auto& material=source.terrain->sectors[command.source_sector].materials[command.material];
            bool phase_available=true;
            for (unsigned part=0;part<4;++part) if (command.subpart_mask&(1u<<part)) {
                const auto required=material.subparts[part].required_animation_phases;
                if (required && (!color || required>color->animation_phases.size())) phase_available=false;
            }
            if (!phase_available) {
                static bool reported=false;
                if (!reported) { reported=true;append_bridge_event("owned_terrain_color_part_missing",
                    "completed_atlas_phase_unavailable",true); }
                continue;
            }
            OwnedTerrainSourceDecodeContext decode{};
            NativeGeometryView* source_view=nullptr;
            g_owned_terrain_failure_stage="submit.source_world_record";
            if (!source_terrain_world_record(source,g_owned_terrain_frame.front_end,command,
                    color,{true,true,0},decode,source_view)||!source_view) throw false;
            auto& view=*source_view;
            if (captured_state) {
                view.owned_fixed_state=*captured_state;
                view.owned_fixed_state.replay_frame_state=true;
            }
            g_owned_terrain_failure_stage="submit.world_transform";
            if(!apply_owned_terrain_world_transform(view))throw false;
            if (bound_material != command.material) {
                g_owned_terrain_failure_stage="submit.texture_handles_empty";
                if (g_owned_terrain_frame.texture_handles.empty()) throw false;
                const std::size_t texture_index = command.material %
                    g_owned_terrain_frame.texture_handles.size();
                // The wrapper is stable only for this uninterrupted outer
                // World::Draw. Binding remains immediate; no texture pointer
                // enters persistent source state or survives frame teardown.
                void* texture =
                    g_owned_terrain_frame.texture_handles[texture_index];
                g_owned_terrain_failure_stage="submit.bind_texture";
                if (!bind_owned_terrain_texture(renderer, texture_index, texture)) throw false;
                bound_material = command.material;
            }
            NativeGeometryResult ignored{};
            if (!g_native_sink_submit(g_native_sink_context, view, ignored)) {
                g_owned_terrain_failure_stage=ignored.failure_operation?ignored.failure_operation:"submit.renderer_rejected";
                g_owned_terrain_failure_hresult=ignored.failure_hresult;throw false;
            }
            if (g_bridge_sample.active) {
                ++g_bridge_sample.terrain_calls;
                ++g_bridge_sample.terrain_gpu_owned_calls;
            }
        }
        g_native_sink_end_batch(g_native_sink_context, true);
        batch_active = false;
        // A later omission must not report this completed submit's last stage.
        g_owned_terrain_failure_stage="none";
        g_owned_terrain_failure_hresult=S_OK;
        return true;
    } catch (...) {
        if (batch_active) g_native_sink_end_batch(g_native_sink_context, false);
        return false;
    }
}

#endif // !EID3D11_EMBEDDED
namespace {

// Base terrain as a persistent renderer set (#62). Every opaque base piece
// (sector x material, all sub-parts, full detail) is registered once and
// GPU-culled; a frame sends only its terrain globals and one flush. Pieces are
// re-registered when their content inputs change: source identity, the
// terrain transform, the bound texture wrappers, the base render state (fog
// colour/enable are frame values the set flush takes from the current frame),
// or a renderer set reset. Live layer shifts (game water levels) are never an
// input: the shader applies them to liquid depth (shift_objects).
// Base pieces decode from the published geometry; the game's per-sector
// capture follows its own camera visibility and carries only atlas phases. Pieces with animated atlas phases and
// any piece the set declines stay on the per-frame path.
struct OwnedTerrainSetState {
    bool valid = false;
    std::uint64_t renderer_generation = 0, source_generation = 0, source_revision = 0;
    const void* terrain = nullptr;
    std::array<float,16> object_to_world{};
    std::vector<void*> textures;
    std::array<std::uint32_t,29> material{};
    std::vector<std::uint64_t> keys;
    std::vector<OwnedTerrainCommand> per_frame;
    std::vector<OwnedTerrainCommand> deferred;   // texture wrapper not yet available
    unsigned failures_logged = 0;
};
OwnedTerrainSetState g_owned_terrain_set;
std::uint64_t g_owned_terrain_set_rebuilds=0;   // full re-registrations (temporary telemetry)

} // namespace
bool owned_terrain_static_op(NativeStaticSubmission& request,const char** failure) {
    NativeGeometryView view;view.static_submission=&request;NativeGeometryResult result{};
    const bool ok=g_native_sink_submit && g_native_sink_submit(g_native_sink_context,view,result);
    if(failure)*failure=result.failure_operation;
    return ok;
}

void report_owned_terrain_set_failure(const char* stage,std::uint32_t sector,std::uint32_t material,const char* detail) {
    g_owned_terrain_failure_stage=detail?detail:stage;
    auto& state=g_owned_terrain_set;
    if(state.failures_logged>=8)return;
    ++state.failures_logged;char text[160];
    std::snprintf(text,sizeof(text),"stage=%s sector=%u material=%u detail=%s",stage,sector,material,detail?detail:"-");
    append_bridge_event("terrain_set_failed",text,false);
}
namespace {

// The frame's handle list names only materials the game draws this frame;
// the map's full wrapper list (valid for the same outer World::Draw) names
// every atlas. A null entry is an atlas the game has not loaded yet.
// Field values only (bool members leave indeterminate padding bytes); the
// frame's fog colour/enable are excluded, the set flush reads them per frame.
std::array<std::uint32_t,29> owned_terrain_state_key(const NativeOwnedFixedState& v) noexcept {
    std::uint32_t near_bits=0,far_bits=0;
    std::memcpy(&near_bits,&v.captured_depth_range[0],4);std::memcpy(&far_bits,&v.captured_depth_range[1],4);
    return {v.enabled?1u:0u,v.cull_mode,v.fill_mode,v.z_enable,v.z_write_enable,v.z_func,v.alpha_blend_enable,
        v.source_blend,v.destination_blend,v.alpha_test_enable,v.alpha_func,v.alpha_ref,v.texture_factor,
        v.specular_enable,v.color_op,v.alpha_op,v.color_arg1,v.color_arg2,v.alpha_arg1,v.alpha_arg2,
        v.address_u,v.address_v,v.min_filter,v.mag_filter,v.mip_filter,v.captured_viewport[2],v.captured_viewport[3],
        near_bits,far_bits};
}

} // namespace
void* owned_terrain_texture_wrapper(std::size_t material) noexcept {
    auto& frame=g_owned_terrain_frame;
    if(frame.texture_handles.empty())return nullptr;
    const auto index=material%frame.texture_handles.size();
    void* texture=frame.texture_handles[index];
    return texture;
}
namespace {

#if !defined(EID3D11_EMBEDDED)
bool register_owned_terrain_piece(void* renderer,const OwnedTerrainCommand& command,bool& deferred) {
    auto& frame=g_owned_terrain_frame;auto& state=g_owned_terrain_set;
    deferred=false;
    const auto sector=command.source_sector;const auto material=command.material;
    void* texture=owned_terrain_texture_wrapper(material);
    if(!texture){deferred=true;return true;}
    OwnedTerrainSourceDecodeContext decode{};NativeGeometryView* view=nullptr;
    if(!source_terrain_world_record(frame.source_values,frame.front_end,command,nullptr,{true,true,0},decode,view) || !view) {
        report_owned_terrain_set_failure("record",sector,material,nullptr);return false;
    }
    view->owned_fixed_state=frame.shadow_material;view->owned_fixed_state.replay_frame_state=true;
    if(!apply_owned_terrain_world_transform(*view)) {
        report_owned_terrain_set_failure("transform",sector,material,nullptr);return false;
    }
    if(!bind_owned_terrain_texture(renderer,frame.texture_handles.empty()?0:material%frame.texture_handles.size(),texture)) {
        report_owned_terrain_set_failure("texture",sector,material,nullptr);return false;
    }
    NativeStaticSubmission request;request.terrain=true;
    request.key=(std::uint64_t(sector)<<16)|material;request.terrain_view=view;
    const char* failure=nullptr;
    if(!owned_terrain_static_op(request,&failure)) {
        report_owned_terrain_set_failure("set",sector,material,failure);return false;
    }
    if(request.registered)state.keys.push_back(request.key);
    else state.per_frame.push_back(command);
    return true;
}

bool submit_owned_terrain_set(void* renderer) {
    auto& frame=g_owned_terrain_frame;auto& state=g_owned_terrain_set;
    const auto& source=frame.source_values;auto& packet=frame.front_end;
    if(!source.terrain || !source.geometry || !source.prepared || !packet.spatial_ready ||
       packet.source!=source.terrain || !frame.world_transform_valid)return false;
    if(publish_native_world_lighting(renderer)!=FigureFallbackReason::none)return false;
    NativeStaticSubmission query;query.operation=NativeStaticSubmission::Operation::generation;
    if(!owned_terrain_static_op(query))return false;
    const auto& terrain=*source.terrain;
    const auto material=owned_terrain_state_key(frame.shadow_material);
    std::vector<void*> textures(frame.texture_handles.size());
    for(std::size_t index=0;index<textures.size();++index)textures[index]=owned_terrain_texture_wrapper(index);
    // An atlas loading later is not a change: only a replaced wrapper is.
    bool textures_same=state.textures.size()==textures.size();
    for(std::size_t index=0;textures_same && index<textures.size();++index)
        textures_same=!state.textures[index] || state.textures[index]==textures[index];
    const bool current=state.valid && state.renderer_generation==query.generation &&
        state.terrain==source.terrain.get() && state.source_generation==source.source_generation &&
        state.source_revision==source.source_revision &&
        state.object_to_world==frame.object_to_world &&
        textures_same && state.material==material;
    if(!current) {
        ++g_owned_terrain_set_rebuilds;
        for(const auto key:state.keys) {
            NativeStaticSubmission remove;remove.operation=NativeStaticSubmission::Operation::remove;
            remove.terrain=true;remove.key=key;owned_terrain_static_op(remove);
        }
        state=OwnedTerrainSetState{false,0,0,0,nullptr,{},{},{},{},{},{},state.failures_logged};
        if(frame.texture_handles.empty())return false;
        const auto threshold=terrain.texture_count+2u;
        for(std::size_t begin=0;begin<packet.spatial_parts.size();) {
            const auto sector=packet.spatial_parts[begin].source_sector;
            const auto material=packet.spatial_parts[begin].material;
            std::uint8_t mask=0;std::size_t end=begin;
            while(end<packet.spatial_parts.size() && packet.spatial_parts[end].source_sector==sector &&
                  packet.spatial_parts[end].material==material)
                mask=static_cast<std::uint8_t>(mask|packet.spatial_parts[end++].subpart_mask);
            const auto first=begin;begin=end;
            if(material>=threshold || !mask)continue;
            OwnedTerrainCommand command{};
            if(!owned_terrain_render_command(packet,sector,material,mask,command)) {
                report_owned_terrain_set_failure("command",sector,material,nullptr);return false;
            }
            command.spatial_index=static_cast<std::uint32_t>(first);
            bool animated=false;
            for(unsigned part=0;part<4;++part)
                if((mask&(1u<<part)) && terrain.sectors[sector].materials[material].subparts[part].required_animation_phases)
                    animated=true;
            if(animated){state.per_frame.push_back(command);continue;}
            bool deferred=false;
            if(!register_owned_terrain_piece(renderer,command,deferred))continue;   // omitted, logged
            if(deferred)state.deferred.push_back(command);
        }
        state.valid=true;state.renderer_generation=query.generation;state.terrain=source.terrain.get();
        state.source_generation=source.source_generation;state.source_revision=source.source_revision;
        state.object_to_world=frame.object_to_world;state.textures=textures;
        state.material=material;
    }
    state.textures=textures;
    if(!state.deferred.empty()) {
        std::vector<OwnedTerrainCommand> still;
        for(const auto& command:state.deferred) {
            bool deferred=false;
            if(register_owned_terrain_piece(renderer,command,deferred) && deferred)still.push_back(command);
        }
        state.deferred.swap(still);
    }
    NativeStaticSubmission flush;flush.operation=NativeStaticSubmission::Operation::flush;
    flush.terrain=true;flush.world_to_clip=&frame.world_to_clip;
    const char* failure=nullptr;
    if(!owned_terrain_static_op(flush,&failure)){report_owned_terrain_set_failure("flush",0,0,failure);return false;}
    return state.per_frame.empty() ||
        submit_source_owned_terrain_commands(renderer,state.per_frame,&frame.shadow_material);
}
#endif // !EID3D11_EMBEDDED

// Terrain LOD draws the base terrain per frame, but the neighbouring maps are
// drawn by the terrain set flush. Flush an empty set on that path too, after
// retiring a full-detail set left by a live switch so it is not drawn twice.
bool flush_owned_terrain_lod_set() {
    auto& state=g_owned_terrain_set;
    if(state.valid) {
        for(const auto key:state.keys) {
            NativeStaticSubmission remove;remove.operation=NativeStaticSubmission::Operation::remove;
            remove.terrain=true;remove.key=key;owned_terrain_static_op(remove);
        }
        state=OwnedTerrainSetState{false,0,0,0,nullptr,{},{},{},{},{},{},state.failures_logged};
    }
    NativeStaticSubmission flush;flush.operation=NativeStaticSubmission::Operation::flush;
    flush.terrain=true;flush.world_to_clip=&g_owned_terrain_frame.world_to_clip;
    const char* failure=nullptr;
    if(!owned_terrain_static_op(flush,&failure)){report_owned_terrain_set_failure("flush",0,0,failure);return false;}
    return true;
}

} // namespace
bool submit_pending_owned_terrain_base() noexcept {
    auto& frame=g_owned_terrain_frame;
    if (!frame.base_pending) return true;
    frame.base_pending=false;
    auto* renderer=frame.base_renderer;
    frame.base_renderer=nullptr;
    bool success=false;
    g_owned_terrain_failure_stage="base.renderer_world_shadow_or_geometry_missing";
    g_owned_terrain_failure_hresult=S_OK;
    try {
        success=renderer && frame.world_serial && frame.shadow_material.enabled &&
            g_owned_visual_world_frame.shadow_ready && frame.source_values.geometry;
        if(success) {
            // The completed terrain owner supplies wind, weather and frame
            // lifetime to every consumer, independently of geometry LOD.
            NativeStaticSubmission globals;
            globals.operation=NativeStaticSubmission::Operation::terrain_frame;
            globals.terrain_state=&frame.source_values.geometry->animation;
            globals.layer_shifts=&frame.source_values.geometry->layer_shifts;
            const char* failure=nullptr;
            success=owned_terrain_static_op(globals,&failure);
            if(!success)report_owned_terrain_set_failure("globals",0,0,failure);
        }
        if(success)success=frame.front_end.inputs.force_full_detail ? submit_owned_terrain_set(renderer) :
            submit_source_owned_terrain_commands(renderer,frame.front_end.base_commands,&frame.shadow_material) &&
            flush_owned_terrain_lod_set();
    } catch (...) { g_owned_terrain_failure_stage="base.exception_or_allocation_failure";success=false; }
    if (!complete_owned_terrain_base(frame.front_end,success)) {
        frame.front_end.failed_after_claim=true;
        report_owned_terrain_omission(OwnedTerrainRangeAction::omit_invalid);
        return false;
    }
    return true;
}


// The outer world draw's frame bracket (the game thread inline, the scene
// thread or the core-mode host from its queued begin/end commands).
thread_local std::uint64_t g_owned_terrain_world_serial = 0;


void begin_owned_terrain_world_frame() noexcept {
    g_native_world_lighting_frame = {};
    g_owned_terrain_frame.front_end.source.reset();
    g_owned_terrain_frame.source_values = {};
    g_owned_terrain_frame.world_transform_valid=false;
    g_owned_terrain_frame.front_end.status =
        OwnedTerrainPacketStatus::pending_source;
    g_owned_terrain_frame.front_end.inputs = {};
    g_owned_terrain_frame.front_end.sectors.clear();
    g_owned_terrain_frame.front_end.spatial_parts.clear();
    g_owned_terrain_frame.front_end.base_commands.clear();
    g_owned_terrain_frame.front_end.transparent_commands.clear();
    g_owned_terrain_frame.front_end.base_requested = false;
    g_owned_terrain_frame.front_end.base_submitted = false;
    g_owned_terrain_frame.front_end.failed_after_claim = false;
    g_owned_terrain_frame.front_end.spatial_ready = false;
    g_owned_terrain_frame.front_end.preparation_count = 0;
    g_owned_terrain_frame.front_end.base_submission_count = 0;
    g_owned_terrain_frame.front_end.transparent_reuse_count = 0;
    for (auto& dynamic : g_owned_terrain_frame.dynamic)
        dynamic.captured = false;
    g_owned_terrain_frame.publication_seen.clear();
    g_owned_terrain_frame.texture_handles.clear();
    g_owned_terrain_frame.shadow_texture_observed.clear();
    g_owned_terrain_frame.shadow_material={};
    g_owned_terrain_frame.base_attempted = false;
    g_owned_terrain_frame.base_publication_ran = false;
    g_owned_terrain_frame.base_pending = false;
    g_owned_terrain_frame.base_renderer = nullptr;
    if (++g_owned_terrain_world_serial == 0) ++g_owned_terrain_world_serial;
    g_owned_terrain_frame.world_serial = g_owned_terrain_world_serial;
}

void end_owned_terrain_world_frame() noexcept {
    if (g_owned_terrain_frame.base_pending) {
        complete_owned_terrain_base(g_owned_terrain_frame.front_end,false);
        report_object_plan_failure("owned_terrain_base_seal_missing");
    }
    g_owned_terrain_frame.base_pending = false;
    g_owned_terrain_frame.base_renderer = nullptr;
    g_native_world_lighting_frame = {};
    // Releases the source snapshot and borrowed texture-wrapper values before
    // a map can be freed or another outer world draw begins. Reusable dynamic
    // value storage keeps its capacity but cannot be consumed once captured is
    // cleared and the world serial becomes zero.
    g_owned_terrain_frame.front_end.source.reset();
    g_owned_terrain_frame.source_values = {};
    g_owned_terrain_frame.world_transform_valid=false;
    g_owned_terrain_frame.front_end.status =
        OwnedTerrainPacketStatus::pending_source;
    g_owned_terrain_frame.front_end.spatial_parts.clear();
    g_owned_terrain_frame.front_end.spatial_ready=false;
    for (auto& dynamic : g_owned_terrain_frame.dynamic)
        dynamic.captured = false;
    g_owned_terrain_frame.publication_seen.clear();
    g_owned_terrain_frame.texture_handles.clear();
    g_owned_terrain_frame.shadow_texture_observed.clear();
    g_owned_terrain_frame.shadow_material={};
    g_owned_terrain_frame.base_attempted = false;
    g_owned_terrain_frame.base_publication_ran = false;
    g_owned_terrain_frame.world_serial = 0;
}

void reset_owned_visual_world_frame() noexcept {
    auto& frame=g_owned_visual_world_frame;
    if(!frame.prepared.empty())g_owned_visual_prepared_spare=std::move(frame.prepared);
    clear_owned_visual_world_frame(frame);
}


// One object's wound layer behind its world frame: each admitted part is
// submitted again with the wound surface, inside one object transaction.
// same_world: the world frame on this thread was prepared from the frame
// the parts were chosen from; otherwise the layer is omitted.
void run_owned_wound_layer(const std::vector<OwnedWoundRequest>& requests,
    const std::vector<std::shared_ptr<SurfaceImage>>& wounds,void* sinks,const BridgeSceneHandoff& handoff,
    bool same_world) noexcept {
    install_owned_scene_inputs(sinks,handoff,nullptr,nullptr,nullptr,0);
    const auto& world=g_owned_visual_world_frame;
    bool transaction=false,ok=same_world;
    try {
        for(std::size_t i=0;ok && i<requests.size();++i) {
            const auto& request=requests[i];
            if(request.sample>=world.values->entities.size() || request.sample>=world.prepared.size()) { ok=false; break; }
            const auto& sample=world.values->entities[request.sample];
            const auto& prepared=world.prepared[request.sample];
            if(!prepared.submitted || request.part>=prepared.parts.size() ||
               !prepared.parts[request.part].color_admitted)continue;
            if(!transaction && !(transaction=g_native_sink_begin_object && g_native_sink_begin_object(g_native_sink_context))) { ok=false; break; }
            handoff.set_wound(sinks,request.wound,wounds[i]);
            NativePreparedPartSubmission* record=nullptr;
            NativeWorldBounds part_bounds{};
            NativeGeometryResult ignored{};
            ok=retained_visual_part_bounds(sample,request.part,part_bounds) &&
                retained_visual_part_submission(sample,request.part,part_bounds,{true,true,0},
                    world.values->time,request.fog,record);
            if(ok) {
                const NativePreparedPartSubmission* selected=record;
                ok=submit_owned_visual_records(sample,&selected,1,world.coverage.world_to_clip,
                    world.serial,request.wound,ignored);
            }
        }
    } catch(...) { ok=false; }
    if(!ok)report_object_plan_failure("owned_wound_layer");
    if(transaction && g_native_sink_end_object)g_native_sink_end_object(g_native_sink_context,ok);
    clear_owned_scene_inputs(handoff);
}


// The active map's swamp mist source (game-free: the terrain frame and the
// retained terrain source), for the game thread, the scene thread and the host.
#if !defined(EID3D11_EMBEDDED)
std::shared_ptr<const WorldMistGrid> bridge_world_mist_grid() noexcept {
    static thread_local WorldMistGridCache cache;
    const auto& frame = g_owned_terrain_frame;
    if (!frame.world_serial || !frame.world_transform_valid) { cache.retire(); return {}; }
    return cache.get(retained_visual_scene().terrain_source_snapshot(), frame.object_to_world);
}

// The active terrain placement the neighbour stream places its maps by.
bool bridge_neighbour_world_transform(std::array<float,16>& placement) noexcept {
    if(!g_owned_terrain_frame.world_transform_valid)return false;
    placement=g_owned_terrain_frame.object_to_world;return true;
}

#endif // !EID3D11_EMBEDDED
// One object preview through its captured device-state sets.
void run_owned_preview_command(OwnedPreviewCommand& command) noexcept {
    if (command.sets.empty()) return;
    const auto& handoff = command.handoff;
    void* sinks = command.sets.front().get();
    install_owned_scene_inputs(sinks, handoff, nullptr, nullptr, nullptr, 0);
    bool transaction = false, ok = false;
    try {
        transaction = g_native_sink_begin_object && g_native_sink_begin_object(g_native_sink_context);
        const ModelPreviewInput input{command.sample, command.base_world, command.world_to_clip,
            command.texture_time, command.fog, command.presentation_serial};
        ModelPartResult result;
        ok = transaction && produce_model_preview(input, command.parts.size(),
            [&](std::size_t i, ModelPartFacts& facts) {
                facts.texture_set = static_cast<std::uint32_t>(command.parts[i].set);
                return true;
            },
            [&](std::uint32_t set) {
                if (set >= command.sets.size()) return false;
                return true;
            },
            [&](std::size_t i, ModelPreviewPart& output) {
                const auto& part = command.parts[i];
                // Restore this captured state for every part, including after a wound draw.
                if (handoff.adopt_sinks) handoff.adopt_sinks(sinks, command.sets[part.set].get());
                output.ordinal = part.ordinal;
                output.fixed_state = part.material;
                output.fixed_state_supplied = true;
                output.material = {part.uses_default_colors, (part.material_flags & 0x10u) != 0,
                    part.default_diffuse, part.default_specular, part.lighting,
                    part.wound ? NativeOrdering::preserve : NativeOrdering::derive_from_state};
                output.wound = part.wound;
                return true;
            },
            [&](const NativeGeometryView& view, const ModelPreviewPart&) {
                NativeGeometryResult result{};
                return g_native_sink_submit(g_native_sink_context, view, result);
            },
            [&](std::size_t i, const ModelPreviewPart& part) {
                if (handoff.set_wound) handoff.set_wound(sinks, part.wound, command.parts[i].wound_texture);
                return true;
            }, result);
    } catch (...) { ok = false; }
    if (!ok) {
        static volatile LONG reported = 0;
        if (InterlockedCompareExchange(&reported,1,0) == 0)
            append_bridge_event("owned_preview_omitted","scene thread: preview submission failed; the preview is omitted",true);
    }
    if (transaction && g_native_sink_end_object) g_native_sink_end_object(g_native_sink_context, ok);
    clear_owned_scene_inputs(handoff);
}

// The footprint snapshot of the world command being prepared (scene thread,
// or the x64 host in core mode, where it is the only source).
bool scene_ground_cover_footprints(std::vector<ground_cover::FootprintSplat>& output,
    std::vector<FootprintTrailPoint>* trail_points) noexcept {
    // Scene thread: the game thread's read for this world command.
    output.clear();
    if(trail_points)trail_points->clear();
    const auto* snapshot=g_scene_footprints;
    if(!snapshot) {
        static bool reported=false;
        if(!reported){reported=true;append_bridge_event("ground_cover_footprints_rejected",
            "scene thread: no game-thread footprint snapshot with this command; ground cover omitted",true);}
        return false;
    }
    try {
        output=snapshot->splats;
        if(trail_points)*trail_points=snapshot->trail_points;
    } catch(...) {
        output.clear();
        if(trail_points)trail_points->clear();
        return false;
    }
    return snapshot->ok;
}

} // namespace owned
