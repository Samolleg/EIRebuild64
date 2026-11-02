#include "render_profiler.h"
#include "debug_output_paths.h"
#include "debug_overlay.h"
#include "camp_inventory.h"
#include "engine_bridge.h"
#include "owned_resource_runtime.h"
#include "retained_static_scene.h"
#include "retained_static_submission.h"
#include "stall_watchdog.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <tuple>

namespace owned {
namespace {

bool environment_flag(const char* name) {
    char value[16]{};
    return GetEnvironmentVariableA(name, value, static_cast<DWORD>(sizeof(value))) != 0 && value[0] == '1';
}

constexpr bool profiling_requested(bool environment_enabled, bool attribution_enabled,
                                   bool caller_enabled, bool capture_requested) noexcept {
    return environment_enabled || attribution_enabled || caller_enabled || capture_requested;
}

static_assert(!profiling_requested(false, false, false, false));
static_assert(profiling_requested(true, false, false, false));
static_assert(profiling_requested(false, true, false, false));
static_assert(profiling_requested(false, false, true, false));
static_assert(profiling_requested(false, false, false, true));

std::int64_t qpc_now() {
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return value.QuadPart;
}

std::string sanitize(const char* name) {
    std::string result = name ? name : "unnamed";
    for (auto& c : result) {
        const bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                          (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (!keep) c = '_';
    }
    return result;
}

void write_json_string(std::ostringstream& output,
                       const std::string& value) {
    static constexpr char digits[] = "0123456789abcdef";
    output << '"';
    for (const unsigned char byte : value) {
        switch (byte) {
        case '"': output << "\\\""; break;
        case '\\': output << "\\\\"; break;
        case '\b': output << "\\b"; break;
        case '\f': output << "\\f"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (byte < 0x20u || byte >= 0x7fu) {
                output << "\\u00" << digits[byte >> 4u]
                       << digits[byte & 15u];
            } else {
                output << static_cast<char>(byte);
            }
        }
    }
    output << '"';
}

std::string utc_capture_timestamp() {
    SYSTEMTIME value{};
    GetSystemTime(&value);
    char formatted[32]{};
    std::snprintf(formatted, sizeof(formatted),
                  "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
                  value.wYear, value.wMonth, value.wDay, value.wHour,
                  value.wMinute, value.wSecond, value.wMilliseconds);
    return formatted;
}

} // namespace

CutoutShadowScopeSnapshot summarize_cutout_shadow_scope(
    std::uint64_t presentation_frame,
    const std::vector<CutoutShadowScopeRecord>& records) noexcept {
    CutoutShadowScopeSnapshot snapshot{};
    snapshot.presentation_frame=presentation_frame;
    try {
        using Instance=std::tuple<bool,std::int32_t,std::uint64_t,std::uint64_t>;
        struct SourceCount {std::uint64_t parts=0;std::set<Instance> instances;};
        std::map<std::string,SourceCount> sources;
        std::set<Instance> instances,instances_without_resource_key;
        snapshot.parts=records.size();
        for(const auto& record:records){
            const Instance identity{record.terrain_source,record.signed_id,
                record.lifecycle_generation,record.world_epoch};
            instances.insert(identity);
            if(record.resource_key.empty()){
                ++snapshot.parts_without_resource_key;
                instances_without_resource_key.insert(identity);
            }else{
                auto& source=sources[record.resource_key];
                ++source.parts;
                source.instances.insert(identity);
            }
        }
        snapshot.sources=sources.size();
        snapshot.instances=instances.size();
        snapshot.instances_without_resource_key=instances_without_resource_key.size();
        for(const auto& entry:sources)
            if(snapshot.listed_sources.size()<64)
                snapshot.listed_sources.push_back({entry.first,entry.second.instances.size(),entry.second.parts});
        snapshot.available=true;
    }catch(...){snapshot={};snapshot.presentation_frame=presentation_frame;}
    return snapshot;
}

std::string format_scene_identity_json(
    const RetainedSceneIdentitySnapshot& identity,
    const SceneWorldToClipSnapshot& camera_context,
    const char* capture_requested_at_utc,
    bool presentation_frame_valid,
    std::uint64_t presentation_frame) {
    const bool matrix_valid = camera_context.valid &&
        std::all_of(camera_context.world_to_clip.begin(),
                    camera_context.world_to_clip.end(),
                    [](float value) { return std::isfinite(value); });
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(9);
    output << "{\"capture_requested_at_utc\":";
    write_json_string(output, capture_requested_at_utc
        ? std::string(capture_requested_at_utc) : std::string());
    output << ",\"status\":\""
           << retained_scene_identity_status_name(identity.status) << '"';
    output << ",\"main_outer_resource\":";
    if (identity.main_outer_resource.empty())
        output << "null";
    else
        write_json_string(output, identity.main_outer_resource);
    output << ",\"main_mob_resource\":";
    if (identity.main_mob_resource.empty())
        output << "null";
    else
        write_json_string(output, identity.main_mob_resource);
    output << ",\"published_source_membership\":[";
    for (std::size_t index = 0;
         index < identity.published_source_membership.size(); ++index) {
        if (index) output << ',';
        write_json_string(
            output, identity.published_source_membership[index]);
    }
    output << "],\"published_source_membership_truncated\":"
           << (identity.published_source_membership_truncated
                   ? "true" : "false")
           << ",\"source_generation\":" << identity.source_generation
           << ",\"source_revision\":" << identity.source_revision
           << ",\"binding_revision\":" << identity.binding_revision
           << ",\"pending_transactions\":"
           << identity.pending_transactions
           << ",\"positive_world_epoch\":"
           << identity.positive_world_epoch
           << ",\"state_presentation_frame\":";
    if (presentation_frame_valid)
        output << presentation_frame;
    else
        output << "null";
    output << ",\"camera_context\":{\"valid\":"
           << (matrix_valid ? "true" : "false")
           << ",\"scene_frame_serial\":"
           << camera_context.scene_frame_serial
           << ",\"world_to_clip\":";
    if (matrix_valid) {
        output << '[';
        for (std::size_t index = 0;
             index < camera_context.world_to_clip.size(); ++index) {
            if (index) output << ',';
            output << camera_context.world_to_clip[index];
        }
        output << ']';
    } else {
        output << "null";
    }
    output << "},\"time_of_day\":null}";
    return output.str();
}

void format_owned_visual_failure_detail(const OwnedVisualRejection& rejection,
    char* detail, std::size_t size) noexcept {
    std::snprintf(detail,size,"stage=%s reason=%s%s%s id=%d resource=%llu membership=%llu status=%s",
        rejection.stage,rejection.reason_scope ? rejection.reason_scope : "",
        rejection.reason_scope ? "/" : "",rejection.reason ? rejection.reason : "unavailable",rejection.signed_id,
        static_cast<unsigned long long>(rejection.resource_serial),
        static_cast<unsigned long long>(rejection.membership_generation),retained_visual_status_name(rejection.status));
}

std::string format_retained_static_submission_json(
    const BridgeProfileSnapshot* bridge) {
    if (!bridge) return "null";
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << "{\"claimed_instances\":"
           << bridge->retained_static_claimed_instances
           << ",\"visible_instances\":"
           << bridge->retained_static_visible_instances
           << ",\"submitted_instances\":"
           << bridge->retained_static_submitted_instances
           << ",\"submitted_nodes\":"
           << bridge->retained_static_submitted_nodes
           << ",\"post_cull_nodes\":"
           << bridge->retained_static_post_cull_nodes
           << ",\"local_failures\":"
           << bridge->retained_static_local_failures
           << ",\"unique_geometry_variants\":"
           << bridge->retained_static_unique_geometry_variants
           << ",\"suppressed_legacy_draws\":"
           << bridge->retained_static_suppressed_draws
           << ",\"legacy_prepare_removed\":"
           << bridge->retained_static_legacy_prepare_removed
           << ",\"legacy_prepare_remaining\":"
           << bridge->retained_static_legacy_prepare_remaining
           << ",\"owned_visibility_candidates\":"
           << bridge->retained_static_owned_visibility_candidates
           << ",\"owned_visibility_culled\":"
           << bridge->retained_static_owned_visibility_culled
           << ",\"hidden_instances\":"
           << bridge->retained_static_hidden_instances
           << ",\"legacy_shadow_publications\":"
           << bridge->retained_static_shadow_publications
           << ",\"shadow_input\":{\"bridge_sample_frame\":" << bridge->frame
           << ",\"pairing\":\"bridge_sample_only\""
           << ",\"active_visuals\":" << bridge->shadow_active_visuals
           << ",\"active_terrain\":" << bridge->shadow_active_terrain
           << ",\"changed_visuals\":" << bridge->shadow_changed_visuals
           << ",\"changed_terrain\":" << bridge->shadow_changed_terrain
           << ",\"static_casters_reused\":" << bridge->shadow_static_casters_reused
           << ",\"part_bounds_rebuilt\":" << bridge->shadow_bounds_rebuilt
           << ",\"receiver_refits\":" << bridge->shadow_receiver_refits
           << ",\"caster_refits\":" << bridge->shadow_caster_refits
           << ",\"map_reuses\":" << bridge->shadow_map_reuses
           << ",\"dirty_mask\":" << static_cast<unsigned>(bridge->shadow_dirty_mask) << '}'
           << ",\"prepare\":{\"result\":\""
           << retained_static_prepare_result_name(
                  static_cast<RetainedStaticPrepareResult>(
                      bridge->retained_static_prepare_result))
           << "\",\"binding_revision\":"
           << bridge->retained_static_prepare_binding_revision
           << ",\"source_bindings\":"
           << bridge->retained_static_prepare_source_bindings
           << ",\"binding_index\":";
    if (bridge->retained_static_prepare_binding_index == UINT64_MAX)
        output << "null";
    else
        output << bridge->retained_static_prepare_binding_index;
    output << ",\"node_index\":";
    if (bridge->retained_static_prepare_node_index == UINT64_MAX)
        output << "null";
    else
        output << bridge->retained_static_prepare_node_index;
    output << ",\"signed_id\":";
    if (bridge->retained_static_prepare_binding_index == UINT64_MAX)
        output << "null";
    else
        output << bridge->retained_static_prepare_signed_id;
    output << "},\"owned_visual\":";
    const auto& visual=bridge->owned_visual;
    if (!visual.completed || !visual.profile_frame || visual.profile_frame!=bridge->frame) output << "null";
    else {
        output << "{\"profile_frame\":" << visual.profile_frame << ",\"world_serial\":" << visual.world_serial
            << ",\"sampled\":" << (visual.sampled ? "true" : "false")
            << ",\"active\":" << (visual.active ? "true" : "false")
            << ",\"shadow_ready\":" << (visual.shadow_ready ? "true" : "false")
            << ",\"color_submitted\":" << (visual.color_submitted ? "true" : "false")
            << ",\"live_members\":" << visual.live_members << ",\"detached_members\":" << visual.detached_members << ",\"missing_resource\":" << visual.missing_resource
            << ",\"unfinished_resource\":" << visual.unfinished_resource << ",\"excluded_generation\":" << visual.excluded_generation
            << ",\"samples\":" << visual.samples << ",\"considered\":" << visual.considered
            << ",\"unavailable_values\":" << visual.unavailable_values << ",\"class_rejected\":" << visual.class_rejected
            << ",\"no_visible_part\":" << visual.no_visible_part << ",\"invalid_envelope\":" << visual.invalid_envelope
            << ",\"bounds_ready\":" << visual.bounds_ready << ",\"contribution_rejected\":" << visual.contribution_rejected
            << ",\"eligible\":" << visual.eligible << ",\"submitted_parts\":" << visual.submitted_parts
            << ",\"shadow_considered_including_terrain\":" << visual.shadow_considered
            << ",\"shadow_eligible_including_terrain\":" << visual.shadow_eligible
            << ",\"shadow_submitted_including_terrain\":" << visual.shadow_submitted << ",\"first_rejection\":";
        const auto& first=visual.first_rejection;
        if (!first.stage) output << "null";
        else {
            output << "{\"stage\":"; write_json_string(output,first.stage);
            std::string reason;
            if (first.reason_scope) { reason=first.reason_scope; reason+='/'; }
            reason+=first.reason ? first.reason : "unavailable";
            output << ",\"reason\":"; write_json_string(output,reason.c_str());
            output << ",\"status\":"; write_json_string(output,retained_visual_status_name(first.status));
            output << ",\"signed_id\":" << first.signed_id << ",\"resource_serial\":" << first.resource_serial
                << ",\"membership_generation\":" << first.membership_generation << ",\"object_flags\":" << first.object_flags
                << ",\"class_flags\":" << first.class_flags << ",\"unit\":" << (first.unit ? "true" : "false")
                << ",\"hidden\":" << (first.hidden ? "true" : "false")
                << ",\"draw_undetected\":" << (first.draw_undetected ? "true" : "false") << '}';
        }
        output << '}';
    }
    output << '}';
    return output.str();
}

std::string format_terrain_owned_front_end_json(
    const BridgeProfileSnapshot* bridge) {
    if (!bridge) return "null";
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << "{\"frame_preparations\":"
           << bridge->terrain_owned_frame_preparations
           << ",\"transparent_reuses\":"
           << bridge->terrain_owned_transparent_reuses
           << ",\"spatial_bounds_prepared\":"
           << bridge->terrain_spatial_bounds_prepared
           << ",\"spatial_bounds_reused\":"
           << bridge->terrain_spatial_bounds_reused
           << ",\"game_publication_sectors\":"
           << bridge->terrain_game_publication_sectors
           << ",\"dynamic_color_vertices_copied\":"
           << bridge->terrain_dynamic_color_vertices_copied
           << ",\"structural_vertices_copied\":"
           << bridge->terrain_structural_vertices_copied
           << ",\"source_topology_decodes\":"
           << bridge->terrain_source_topology_decodes
           << ",\"source_topology_vertices\":"
           << bridge->terrain_source_topology_vertices
           << ",\"source_color_decodes\":"
           << bridge->terrain_source_color_decodes
           << ",\"source_color_vertices\":"
           << bridge->terrain_source_color_vertices
           << ",\"original_render_front_end_calls\":"
           << bridge->terrain_original_render_front_end_calls
           << ",\"omitted_ranges\":"
           << bridge->terrain_owned_omitted_ranges << '}';
    return output.str();
}

void RenderProfiler::refresh_enabled() noexcept {
    enabled_ = profiling_requested(environment_enabled_, attribution_.enabled(),
                                   requested_enabled_, capture_requested_);
}

HRESULT RenderProfiler::initialize(ID3D11Device* device, ID3D11DeviceContext* context) {
    shutdown();
    if (!device || !context) return E_INVALIDARG;
    device_ = device;
    context_ = context;
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    qpc_frequency_ = std::max<std::int64_t>(1, frequency.QuadPart);
    debug_root_ = make_debug_root();
    attribution_.initialize();

    char capture_value[32]{};
    if (GetEnvironmentVariableA("EI_RENDERER_CAPTURE_FRAME", capture_value,
                                static_cast<DWORD>(sizeof(capture_value))) != 0) {
        requested_capture_frame_ = _strtoui64(capture_value, nullptr, 10);
    }
    environment_enabled_ = environment_flag("EI_RENDERER_PROFILE") ||
                           environment_flag("EI_RENDERER_CAPTURE_NEXT") ||
                           requested_capture_frame_ != ~std::uint64_t{0};
    {
        // Sub-scopes are on with profiling; =0 measures their own overhead.
        char value[16]{};
        sub_scopes_enabled_ = !(GetEnvironmentVariableA("EI_RENDERER_PROFILE_SUBSCOPES", value,
                                                         static_cast<DWORD>(sizeof(value))) != 0 &&
                                value[0] == '0');
    }
    refresh_enabled();

    // D3D12 backend: stamps instead of queries; the adopted D3D11 device
    // creates nothing.
    if (timestamps_) return S_OK;
    D3D11_QUERY_DESC disjoint_desc{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
    D3D11_QUERY_DESC timestamp_desc{D3D11_QUERY_TIMESTAMP, 0};
    for (auto& slot : slots_) {
        HRESULT hr = device_->CreateQuery(&disjoint_desc, &slot.disjoint);
        if (FAILED(hr)) {
            attribution_.record_timestamp_query_failure("disjoint", hr);
            return hr;
        }
        if (attribution_.enabled()) {
            hr = device_->CreateQuery(&timestamp_desc, &slot.device_span.begin);
            if (FAILED(hr)) {
                attribution_.record_timestamp_query_failure("device_span_begin", hr);
                return hr;
            }
            hr = device_->CreateQuery(&timestamp_desc, &slot.device_span.end);
            if (FAILED(hr)) {
                attribution_.record_timestamp_query_failure("device_span_end", hr);
                return hr;
            }
        }
        for (auto& pair : slot.queries) {
            hr = device_->CreateQuery(&timestamp_desc, &pair.begin);
            if (FAILED(hr)) {
                attribution_.record_timestamp_query_failure("pass_begin", hr);
                return hr;
            }
            hr = device_->CreateQuery(&timestamp_desc, &pair.end);
            if (FAILED(hr)) {
                attribution_.record_timestamp_query_failure("pass_end", hr);
                return hr;
            }
        }
    }
    return S_OK;
}

void RenderProfiler::shutdown() {
    if (pass_active_) end_pass();
    if (frame_active_) end_frame();
    for (auto& slot : slots_) {
        if (slot.pending) resolve(slot, true);
    }
    attribution_.shutdown(debug_root_.c_str());
    slots_ = {};
    for (auto& subs : sub_slots_) {
        subs.queries.clear();
        subs.records.clear();
        subs.count = subs.dropped = 0;
    }
    current_subs_ = nullptr;
    current_ = nullptr;
    present_slot_ = nullptr;
    device_.Reset();
    context_.Reset();
    enabled_ = false;
    environment_enabled_ = false;
    requested_enabled_ = false;
    frame_active_ = false;
    pass_active_ = false;
    sub_active_ = false;
    capture_active_ = false;
    capture_requested_ = false;
    capture_directory_.clear();
    pending_normal_frame_metadata_.clear();
    clear_shadow_temporal_pair();
    latest_snapshot_ = {};
    has_latest_snapshot_ = false;
    attribution_selected_pending_ = 0;
}

void RenderProfiler::request_capture_next(const BridgeProfileSnapshot* bridge,
                                          const RendererResourceSnapshot* resources,
                                          const CutoutShadowScopeSnapshot* cutout_scope) noexcept {
    capture_requested_ = true;
    enabled_ = true;
    try {
        std::string metadata;
        metadata.reserve(8192);
        char line[4096]{};
        if (has_latest_snapshot_) {
            const auto& render = latest_snapshot_;
            std::snprintf(line, sizeof(line),
                          "{\n  \"description\":\"Resolved normal frame preceding the diagnostic pass capture. The captured frame stalls for per-pass readback and is not a performance baseline.\",\n"
                          "  \"render\":{\"frame\":%llu,\"width\":%u,\"height\":%u,\"cpu_ms\":%.6f,\"gpu_ms\":%.6f,\"gpu_valid\":%s,\"passes\":[",
                          static_cast<unsigned long long>(render.frame), render.width, render.height,
                          render.frame_cpu_ms, render.frame_gpu_ms,
                          render.gpu_valid ? "true" : "false");
            metadata += line;
            for (UINT index = 0; index < render.pass_count; ++index) {
                const auto& pass = render.passes[index];
                std::snprintf(line, sizeof(line),
                              "%s{\"name\":\"%s\",\"cpu_ms\":%.6f,\"gpu_ms\":%.6f,\"gpu_valid\":%s,\"draws\":%llu,\"triangles\":%llu,\"batches\":%llu,\"uploads\":%llu,\"upload_bytes\":%llu",
                              index ? "," : "", sanitize(pass.name.data()).c_str(),
                              pass.cpu_ms, pass.gpu_ms, pass.gpu_ms >= 0.0 ? "true" : "false",
                              static_cast<unsigned long long>(pass.counters.draws),
                              static_cast<unsigned long long>(pass.counters.triangles),
                              static_cast<unsigned long long>(pass.counters.batches),
                              static_cast<unsigned long long>(pass.counters.uploads),
                              static_cast<unsigned long long>(pass.counters.upload_bytes));
                metadata += line;
                // Additive: passes with GPU sub-scopes carry "children".
                bool first_child = true;
                for (UINT child = 0; child < render.child_count; ++child) {
                    const auto& sub = render.children[child];
                    if (sub.parent != index) continue;
                    std::snprintf(line, sizeof(line),
                                  "%s{\"name\":\"%s\",\"gpu_ms\":%.6f,\"gpu_valid\":%s,\"segments\":%u,\"draws\":%llu,\"triangles\":%llu,\"batches\":%llu}",
                                  first_child ? ",\"children\":[" : ",",
                                  sanitize(sub.name.data()).c_str(),
                                  sub.gpu_ms, sub.gpu_ms >= 0.0 ? "true" : "false", sub.segments,
                                  static_cast<unsigned long long>(sub.draws),
                                  static_cast<unsigned long long>(sub.triangles),
                                  static_cast<unsigned long long>(sub.batches));
                    metadata += line;
                    first_child = false;
                }
                metadata += first_child ? "}" : "]}";
            }
            std::snprintf(line, sizeof(line), "],\"children_dropped\":%u}", render.children_dropped);
            metadata += line;
        } else {
            metadata = "{\n  \"description\":\"No resolved normal render frame was available before this diagnostic capture.\",\n  \"render\":null";
        }
        if (resources) {
            std::snprintf(line, sizeof(line),
                          ",\n  \"resources\":{\"presentation_frame\":%llu,\"present_interval_ms\":%.6f,\"present_smoothed_ms\":%.6f,\"present_fps\":%.3f,\"native_logical_draws\":%llu,\"native_object_generic_ingress_calls\":%llu,\"native_object_prepared_ingress_calls\":%llu,\"native_physical_batches\":%llu,\"native_reorderable_draws\":%llu,\"native_ordered_draws\":%llu,\"native_material_buckets\":%llu,\"native_depth_selection\":{\"selected\":%llu,\"warm\":%llu,\"cold\":%llu,\"registration_callbacks\":%llu},\"native_shadows\":{\"enabled\":%s,\"draws\":%llu,\"triangles\":%llu,\"two_sided_cutout_physical_draws\":%llu,\"two_sided_cutout_cpu_selected_records\":%llu,\"value_reuses\":%llu,\"validity_scans\":%llu,\"frame_copies\":%llu,\"dirty_mask\":%u},\"native_one_draw_batches\":%llu,\"native_max_batch_draws\":%llu,\"texture_array_attempts\":%llu,\"texture_array_successes\":%llu,\"texture_array_fallback_unavailable\":%llu,\"texture_array_fallback_format\":%llu,\"texture_array_invalid_layout\":%llu,\"texture_array_allocation_failures\":%llu,\"texture_array_classes\":%llu,\"texture_array_pages\":%llu,\"texture_array_slices\":%llu}",
                          static_cast<unsigned long long>(resources->presentation_frame),
                          resources->present_interval_ms,
                          resources->present_smoothed_ms,
                          resources->present_smoothed_ms > 0.0
                              ? 1000.0 / resources->present_smoothed_ms : 0.0,
                          static_cast<unsigned long long>(resources->native_logical_draws),
                          static_cast<unsigned long long>(resources->native_object_generic_ingress_calls),
                          static_cast<unsigned long long>(resources->native_object_prepared_ingress_calls),
                          static_cast<unsigned long long>(resources->native_physical_batches),
                          static_cast<unsigned long long>(resources->native_reorderable_draws),
                          static_cast<unsigned long long>(resources->native_ordered_draws),
                          static_cast<unsigned long long>(resources->native_material_buckets),
                          static_cast<unsigned long long>(resources->native_depth_selected),
                          static_cast<unsigned long long>(resources->native_depth_warm),
                          static_cast<unsigned long long>(resources->native_depth_cold),
                          static_cast<unsigned long long>(resources->native_depth_registration_callbacks),
                          resources->native_shadows_enabled ? "true" : "false",
                          static_cast<unsigned long long>(resources->native_shadow_draws),
                          static_cast<unsigned long long>(resources->native_shadow_triangles),
                          static_cast<unsigned long long>(resources->native_shadow_two_sided_cutout_draws),
                          static_cast<unsigned long long>(resources->native_shadow_two_sided_cutout_selected_records),
                          static_cast<unsigned long long>(resources->native_shadow_value_reuses),
                          static_cast<unsigned long long>(resources->native_shadow_validity_scans),
                          static_cast<unsigned long long>(resources->native_shadow_frame_copies),
                          static_cast<unsigned>(resources->native_shadow_dirty_mask),
                          static_cast<unsigned long long>(resources->native_one_draw_batches),
                          static_cast<unsigned long long>(resources->native_max_batch_draws),
                          static_cast<unsigned long long>(resources->native_texture_array_attempts),
                          static_cast<unsigned long long>(resources->native_texture_array_successes),
                          static_cast<unsigned long long>(resources->native_texture_array_fallback_unavailable),
                          static_cast<unsigned long long>(resources->native_texture_array_fallback_format),
                          static_cast<unsigned long long>(resources->native_texture_array_invalid_layout),
                          static_cast<unsigned long long>(resources->native_texture_array_allocation_failures),
                          static_cast<unsigned long long>(resources->native_texture_array_classes),
                          static_cast<unsigned long long>(resources->native_texture_array_pages),
                          static_cast<unsigned long long>(resources->native_texture_array_slices));
            metadata += line;
            const auto& point=resources->point_shadows;
            std::snprintf(line,sizeof(line),
                ",\n  \"point_shadow_tiers\":{\"static_baked\":%llu,\"static_rebaked\":%llu,\"overlays\":%llu,\"budget_skipped\":%llu,\"radius_dirty\":%llu,\"terrain\":%llu,\"light_change\":%llu,\"list_change\":%llu,\"reset\":%llu,\"allocation_failures\":%llu,\"pressure_drops\":%llu}",
                static_cast<unsigned long long>(point.baked),static_cast<unsigned long long>(point.rebaked),
                static_cast<unsigned long long>(point.overlays),static_cast<unsigned long long>(point.budget),
                static_cast<unsigned long long>(point.radius_dirty),static_cast<unsigned long long>(point.terrain),
                static_cast<unsigned long long>(point.light),static_cast<unsigned long long>(point.list),
                static_cast<unsigned long long>(point.reset),static_cast<unsigned long long>(point.allocation_failures),static_cast<unsigned long long>(point.pressure_drops));
            metadata += line;
            const auto& grid=resources->point_light_grid;
            std::snprintf(line,sizeof(line),
                ",\n  \"point_light_grid\":{\"lights\":%llu,\"width\":%llu,\"height\":%llu,\"references\":%llu,\"max_per_cell\":%llu,\"emissive\":%llu}",
                static_cast<unsigned long long>(grid.lights),static_cast<unsigned long long>(grid.width),
                static_cast<unsigned long long>(grid.height),static_cast<unsigned long long>(grid.references),
                static_cast<unsigned long long>(grid.max_per_cell),static_cast<unsigned long long>(grid.emissive));
            metadata += line;
            append_critter_summary_json(metadata,resources->critters);
            if(resources->native_shadow_source_publications) {
                std::ostringstream source;
                source.imbue(std::locale::classic());
                source << ",\n  \"shadow_source_same_present\":{\"presentation_frame\":"
                    << resources->presentation_frame
                    << ",\"last_world_serial\":" << resources->native_shadow_source_world_serial
                    << ",\"publications\":" << resources->native_shadow_source_publications
                    << ",\"active_visuals\":" << resources->native_shadow_source_active_visuals
                    << ",\"active_terrain\":" << resources->native_shadow_source_active_terrain
                    << ",\"changed_visuals\":" << resources->native_shadow_source_changed_visuals
                    << ",\"changed_terrain\":" << resources->native_shadow_source_changed_terrain
                    << ",\"static_casters_reused\":" << resources->native_shadow_source_static_casters_reused
                    << ",\"part_bounds_rebuilt\":" << resources->native_shadow_source_part_bounds_rebuilt
                    << ",\"receiver_refits\":" << resources->native_shadow_source_receiver_refits
                    << ",\"caster_refits\":" << resources->native_shadow_source_caster_refits
                    << ",\"map_reuses\":" << resources->native_shadow_source_map_reuses
                    << ",\"dirty_mask\":" << static_cast<unsigned>(resources->native_shadow_source_dirty_mask)
                    << '}';
                metadata += source.str();
            } else metadata += ",\n  \"shadow_source_same_present\":null";
        } else {
            metadata += ",\n  \"resources\":null";
            append_critter_summary_json(metadata,CritterSummary{});
            metadata += ",\n  \"shadow_source_same_present\":null";
        }
        if(cutout_scope&&cutout_scope->available){
            std::ostringstream scope;
            scope.imbue(std::locale::classic());
            scope << ",\n  \"shadow_cutout_policy_scope\":{\"scope\":\"live_registered_policy_covered_cutout_shadow_records\",\"presentation_frame\":"
                  << cutout_scope->presentation_frame
                  << ",\"source_assets\":" << cutout_scope->sources
                  << ",\"instances\":" << cutout_scope->instances
                  << ",\"parts\":" << cutout_scope->parts
                  << ",\"instances_without_resource_key\":" << cutout_scope->instances_without_resource_key
                  << ",\"parts_without_resource_key\":" << cutout_scope->parts_without_resource_key
                  << ",\"omitted_source_assets\":" << cutout_scope->sources-cutout_scope->listed_sources.size()
                  << ",\"sources\":[";
            bool first=true;
            for(const auto& source:cutout_scope->listed_sources){
                if(!first)scope << ',';
                first=false;
                scope << "{\"resource_key\":";write_json_string(scope,source.resource_key);
                scope << ",\"instances\":" << source.instances
                      << ",\"parts\":" << source.parts << '}';
            }
            scope << "]}";
            metadata += scope.str();
        }else metadata += ",\n  \"shadow_cutout_policy_scope\":null";
        if (resources) {
            std::snprintf(
                line, sizeof(line),
                ",\n  \"performance_statistics\":{\"rolling_window_seconds\":5,\"rolling_samples\":%llu,\"rolling_covered_seconds\":%.6f,\"rolling_average_fps\":%.6f,\"rolling_lowest_fps\":%.6f,\"rolling_warming_up\":%s,\"rolling_complete\":%s,\"rolling_capacity_truncated\":%s,\"telemetry_stale\":%s,\"telemetry_age_ms\":%llu,\"process_cpu_percent\":%.6f,\"process_cpu_valid\":%s,\"process_private_bytes\":%llu,\"process_private_valid\":%s,\"gpu_3d_percent\":%.6f,\"gpu_3d_valid\":%s,\"vram_local_usage_bytes\":%llu,\"vram_local_budget_bytes\":%llu,\"vram_local_valid\":%s}",
                static_cast<unsigned long long>(resources->rolling_frame_samples),
                resources->rolling_frame_seconds,
                resources->rolling_average_fps,
                resources->rolling_lowest_fps,
                resources->rolling_frame_warming_up ? "true" : "false",
                resources->rolling_frame_complete ? "true" : "false",
                resources->rolling_frame_capacity_truncated ? "true" : "false",
                resources->system_telemetry_stale ? "true" : "false",
                static_cast<unsigned long long>(resources->system_telemetry_age_ms),
                resources->process_cpu_percent,
                resources->process_cpu_valid ? "true" : "false",
                static_cast<unsigned long long>(resources->process_private_bytes),
                resources->process_private_bytes_valid ? "true" : "false",
                resources->gpu_3d_percent,
                resources->gpu_3d_percent_valid ? "true" : "false",
                static_cast<unsigned long long>(resources->gpu_usage_bytes),
                static_cast<unsigned long long>(resources->gpu_budget_bytes),
                resources->gpu_memory_valid ? "true" : "false");
            metadata += line;
        } else {
            metadata += ",\n  \"performance_statistics\":null";
        }
        if (resources) {
            std::snprintf(
                line, sizeof(line),
                ",\n  \"resource_mappings\":{\"status\":%lu,\"hooks_active\":%s,\"tracked_mapping_handles\":%llu,\"logical_views\":%llu,\"physical_views\":%llu,\"physical_view_bytes\":%llu,\"active_saved_view_bytes\":%llu,\"peak_saved_view_bytes\":%llu,\"shared_reuses\":%llu,\"deferred_unmaps\":%llu,\"passthrough_maps\":%llu,\"identity_rejections\":%llu,\"tracking_capacity_failures\":%llu,\"hook_validation_failures\":%llu}",
                static_cast<unsigned long>(resources->res_mapping_status),
                resources->res_mapping_hooks_active ? "true" : "false",
                static_cast<unsigned long long>(resources->res_mapping_tracked_handles),
                static_cast<unsigned long long>(resources->res_mapping_logical_views),
                static_cast<unsigned long long>(resources->res_mapping_physical_views),
                static_cast<unsigned long long>(resources->res_mapping_physical_bytes),
                static_cast<unsigned long long>(resources->res_mapping_active_saved_bytes),
                static_cast<unsigned long long>(resources->res_mapping_peak_saved_bytes),
                static_cast<unsigned long long>(resources->res_mapping_shared_reuses),
                static_cast<unsigned long long>(resources->res_mapping_deferred_unmaps),
                static_cast<unsigned long long>(resources->res_mapping_passthrough_maps),
                static_cast<unsigned long long>(resources->res_mapping_identity_rejections),
                static_cast<unsigned long long>(resources->res_mapping_capacity_failures),
                static_cast<unsigned long long>(resources->res_mapping_validation_failures));
            metadata += line;
            std::snprintf(
                line, sizeof(line),
                ",\n  \"resource_cache_release\":{\"status\":%lu,\"hooks_active\":%s,\"override_gets\":%llu,\"override_releases\":%llu,\"icon_releases\":%llu}",
                static_cast<unsigned long>(resources->res_cache_status),
                resources->res_cache_hooks_active ? "true" : "false",
                static_cast<unsigned long long>(resources->res_cache_override_gets),
                static_cast<unsigned long long>(resources->res_cache_override_releases),
                static_cast<unsigned long long>(resources->res_cache_icon_releases));
            metadata += line;
        } else {
            metadata += ",\n  \"resource_mappings\":null";
        }
        if (resources) {   // temporary: memory attribution
            const auto store = owned_visual_resource_stats();
            std::snprintf(line, sizeof(line),
                ",\n  \"memory\":{\"process_private_bytes\":%llu,\"proxy_surface_cpu_bytes\":%llu,\"surface_gpu_bytes\":%llu,\"surface_double_resident_bytes\":%llu,\"surface_released_bytes\":%llu,\"surface_released_count\":%llu,\"surface_pinned_count\":%llu,\"surface_restore_readbacks\":%llu,\"surface_restore_readback_ms\":%.3f,\"owned_immutable_bytes\":%llu,\"owned_archive_owners\":%llu,\"owned_mapped_views\":%llu,\"owned_texture_source_bytes\":%llu,\"transient_upload_bytes\":%llu,\"owned_texture_entries\":%llu,\"owned_texture_bytes\":%llu,\"texture_array_bytes\":%llu,\"geometry_page_bytes\":%llu,\"geometry_live_bytes\":%llu}",
                static_cast<unsigned long long>(resources->process_private_bytes),
                static_cast<unsigned long long>(resources->cpu_surface_bytes),
                static_cast<unsigned long long>(resources->gpu_texture_bytes),
                static_cast<unsigned long long>(resources->double_resident_bytes),
                static_cast<unsigned long long>(resources->released_surface_bytes),
                static_cast<unsigned long long>(resources->released_surfaces),
                static_cast<unsigned long long>(resources->pinned_surfaces),
                static_cast<unsigned long long>(resources->surface_restore_readbacks),
                resources->surface_restore_readback_ms,
                static_cast<unsigned long long>(store.live_immutable_bytes),
                static_cast<unsigned long long>(store.archive_owner_count),
                static_cast<unsigned long long>(store.mapped_view_count),
                static_cast<unsigned long long>(owned_visual_texture_source_bytes()),
                static_cast<unsigned long long>(resources->transient_upload_bytes),
                static_cast<unsigned long long>(resources->owned_texture_entries),
                static_cast<unsigned long long>(resources->owned_texture_bytes),
                static_cast<unsigned long long>(resources->texture_array_bytes),
                static_cast<unsigned long long>(resources->geometry_page_bytes),
                static_cast<unsigned long long>(resources->geometry_live_bytes));
            metadata += line;
            // D3D12: surface homes are executor textures the surface accounting
            // does not count; those two fields read 0 and are not GPU totals.
            if (resources->d3d12_backend && !metadata.empty() && metadata.back() == '}') {
                metadata.pop_back();
                metadata += ",\"unavailable_on_d3d12\":{\"surface_gpu_bytes\":\"surface accounting counts D3D11 textures; D3D12 surface textures are not counted (reads 0)\","
                            "\"surface_double_resident_bytes\":\"derived from surface_gpu_bytes (reads 0)\"}}";
            }
        }
        char lifecycle_json[3072]{};
        if (bridge &&
            format_lifecycle_probe_json(lifecycle_json, sizeof(lifecycle_json),
                                        bridge->lifecycle)) {
            metadata += ",\n  \"lifecycle_probe\":";
            metadata += lifecycle_json;
        } else if (!bridge) {
            metadata += ",\n  \"lifecycle_probe\":null";
        } else {
            metadata += ",\n  \"lifecycle_probe\":{\"validation\":\"format_failure\"}";
        }
        if (resources) {
            std::snprintf(line, sizeof(line),
                          ",\n  \"terrain_submission\":{\"reorderable_contracts\":%llu,\"preserve_contracts\":%llu,\"textured_draws\":%llu,\"untextured_draws\":%llu,\"topology_uploads\":%llu,\"topology_upload_vertices\":%llu,\"color_only_uploads\":%llu,\"color_only_upload_vertices\":%llu}",
                          static_cast<unsigned long long>(resources->native_terrain_reorderable_contracts),
                          static_cast<unsigned long long>(resources->native_terrain_preserve_contracts),
                          static_cast<unsigned long long>(resources->native_terrain_textured_draws),
                          static_cast<unsigned long long>(resources->native_terrain_untextured_draws),
                          static_cast<unsigned long long>(resources->native_terrain_topology_uploads),
                          static_cast<unsigned long long>(resources->native_terrain_topology_upload_vertices),
                          static_cast<unsigned long long>(resources->native_terrain_color_only_uploads),
                          static_cast<unsigned long long>(resources->native_terrain_color_only_upload_vertices));
            metadata += line;
        } else {
            metadata += ",\n  \"terrain_submission\":null";
        }
        metadata += ",\n  \"terrain_owned_front_end\":";
        metadata += format_terrain_owned_front_end_json(bridge);
        metadata += ",\n  \"retained_static_submission\":";
        metadata += format_retained_static_submission_json(bridge);
        const RetainedSceneIdentitySnapshot scene_identity =
            retained_static_scene_identity_snapshot();
        const SceneWorldToClipSnapshot scene_camera =
            bridge_scene_world_to_clip_snapshot();
        const std::string capture_timestamp = utc_capture_timestamp();
        metadata += ",\n  \"scene_identity\":";
        metadata += format_scene_identity_json(
            scene_identity, scene_camera, capture_timestamp.c_str(),
            resources != nullptr,
            resources ? resources->presentation_frame : 0);
        if (bridge) {
            std::snprintf(line, sizeof(line),
                          ",\n  \"engine\":{\"frame\":%llu,\"frame_cpu_ms\":%.6f,\"outside_scene_ms\":%.6f,\"outside_entry_ms\":%.6f,\"world_draw_ms\":%.6f,\"world_unattributed_ms\":%.6f,\"world_draw_calls\":%llu,\"scene_3d_ms\":%.6f,\"scene_3d_unattributed_ms\":%.6f,\"scene_3d_calls\":%llu,\"outside_scene_3d_ms\":%.6f,\"object_draw_ms\":%.6f,\"object_draw_unattributed_ms\":%.6f,\"object_draw_calls\":%llu,\"object_plan_original_calls\":%llu,\"object_plan_owned_calls\":%llu,\"object_plan_hidden_calls\":%llu,\"object_plan_diagnostic_calls\":%llu,\"object_plan_bootstrap_calls\":%llu,\"object_plan_submitted_figures\":%llu,\"object_plan_early_suffix_removed_calls\":%llu,\"object_plan_early_owned_calls\":%llu,\"object_plan_early_omitted_calls\":%llu,\"object_plan_bootstrap_ms\":%.6f,\"object_plan_submit_ms\":%.6f,\"simple_ms\":%.6f,\"simple_calls\":%llu,\"figure_placed_ms\":%.6f,\"figure_placed_calls\":%llu,\"figure_lit_ms\":%.6f,\"figure_lit_calls\":%llu,\"figure_lit_morphed_ms\":%.6f,\"figure_lit_morphed_calls\":%llu,\"figure_lit_rigid_ms\":%.6f,\"figure_lit_rigid_calls\":%llu,\"figure_owned\":%llu,\"figure_fallbacks\":%llu,\"terrain_outer_ms\":%.6f,\"terrain_outer_calls\":%llu,\"terrain_ingress_ms\":%.6f,\"terrain_calls\":%llu,\"terrain_capture_ms\":%.6f,\"terrain_submit_ms\":%.6f,\"terrain_owned\":%llu,\"terrain_fallbacks\":%llu,\"terrain_candidate_sectors\":%llu,\"terrain_visible_sectors\":%llu,\"terrain_frustum_rejected_sectors\":%llu,\"terrain_animation_owned_updates\":%llu,\"terrain_animation_fallback_updates\":%llu,\"terrain_wave_sectors\":%llu,\"terrain_animated_tile_updates\":%llu}\n}\n",
                          static_cast<unsigned long long>(bridge->frame), bridge->frame_cpu_ms,
                          bridge->outside_scene_interval_ms, bridge->outside_entry_cpu_ms,
                          bridge->world_draw_cpu_ms, bridge->world_unattributed_cpu_ms,
                          static_cast<unsigned long long>(bridge->world_draw_calls),
                          bridge->scene_3d_cpu_ms, bridge->scene_3d_unattributed_cpu_ms,
                          static_cast<unsigned long long>(bridge->scene_3d_calls),
                          bridge->outside_scene_3d_cpu_ms,
                          bridge->object_draw_cpu_ms,
                          bridge->object_draw_unattributed_cpu_ms,
                          static_cast<unsigned long long>(bridge->object_draw_calls),
                          static_cast<unsigned long long>(bridge->object_plan_original_calls),
                          static_cast<unsigned long long>(bridge->object_plan_owned_calls),
                          static_cast<unsigned long long>(bridge->object_plan_hidden_calls),
                          static_cast<unsigned long long>(bridge->object_plan_diagnostic_calls),
                          static_cast<unsigned long long>(bridge->object_plan_bootstrap_calls),
                          static_cast<unsigned long long>(bridge->object_plan_submitted_figures),
                          static_cast<unsigned long long>(bridge->object_plan_early_suffix_removed_calls),
                          static_cast<unsigned long long>(bridge->object_plan_early_owned_calls),
                          static_cast<unsigned long long>(bridge->object_plan_early_omitted_calls),
                          bridge->object_plan_bootstrap_cpu_ms,
                          bridge->object_plan_submit_cpu_ms,
                          bridge->simple_cpu_ms, static_cast<unsigned long long>(bridge->simple_calls),
                          bridge->figure_placed_cpu_ms,
                          static_cast<unsigned long long>(bridge->figure_placed_calls),
                          bridge->figure_lit_cpu_ms,
                          static_cast<unsigned long long>(bridge->figure_lit_calls),
                          bridge->figure_lit_morphed_cpu_ms,
                          static_cast<unsigned long long>(bridge->figure_lit_morphed_calls),
                          bridge->figure_lit_rigid_cpu_ms,
                          static_cast<unsigned long long>(bridge->figure_lit_rigid_calls),
                          static_cast<unsigned long long>(bridge->figure_gpu_owned_calls),
                          static_cast<unsigned long long>(bridge->figure_cpu_fallback_calls),
                          bridge->terrain_outer_cpu_ms,
                          static_cast<unsigned long long>(bridge->terrain_outer_calls),
                          bridge->terrain_cpu_ms,
                          static_cast<unsigned long long>(bridge->terrain_calls),
                          bridge->terrain_capture_cpu_ms, bridge->terrain_submit_cpu_ms,
                          static_cast<unsigned long long>(bridge->terrain_gpu_owned_calls),
                          static_cast<unsigned long long>(bridge->terrain_cpu_fallback_calls),
                          static_cast<unsigned long long>(bridge->terrain_candidate_sectors),
                          static_cast<unsigned long long>(bridge->terrain_visible_sectors),
                          static_cast<unsigned long long>(bridge->terrain_frustum_rejected_sectors),
                          static_cast<unsigned long long>(bridge->terrain_animation_owned_updates),
                          static_cast<unsigned long long>(bridge->terrain_animation_fallback_updates),
                          static_cast<unsigned long long>(bridge->terrain_wave_sectors),
                          static_cast<unsigned long long>(bridge->terrain_animated_tile_updates));
            metadata += line;
        } else {
            metadata += ",\n  \"engine\":null\n}\n";
        }
        pending_normal_frame_metadata_ = std::move(metadata);
    } catch (...) {
        pending_normal_frame_metadata_.clear();
    }
}

void RenderProfiler::mark(QueryPair& pair, bool end) noexcept {
    if (timestamps_) (end ? pair.end_stamp : pair.begin_stamp) = timestamps_->stamp();
    else context_->End(end ? pair.end.Get() : pair.begin.Get());
}

bool RenderProfiler::read(const QueryPair& pair, bool end, UINT64& ticks) noexcept {
    if (timestamps_) {
        const std::uint64_t stamp = end ? pair.end_stamp : pair.begin_stamp;
        std::uint64_t value = 0;
        if (!stamp || !timestamps_->value(stamp, value)) return false;
        ticks = value;
        return true;
    }
    return context_->GetData(end ? pair.end.Get() : pair.begin.Get(), &ticks, sizeof(ticks),
                             D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK;
}

void RenderProfiler::begin_frame(UINT width, UINT height) {
    if (!enabled_ || frame_active_ || !context_) return;
    FrameSlot& slot = slots_[next_frame_ % kBufferedFrames];
    if (slot.pending && !resolve(slot, false)) {
        // Do not recycle a live query slot.  Missing one diagnostic frame is
        // preferable to stalling rendering or corrupting later measurements.
        if (attribution_.enabled()) attribution_.mark_dropped();
        ++next_frame_;
        return;
    }
    // Resolution may finalize an attribution-only capture. Do not open one
    // extra general profiler slot after its last authoritative reason ended.
    if (!enabled_) return;
    slot.frame = next_frame_;
    slot.pass_count = 0;
    slot.cpu_scope_count = 0;
    slot.width = width;
    slot.height = height;
    slot.pending = false;
    slot.attribution_present_returned = false;
    slot.attribution = {};
    current_ = &slot;
    current_pass_ = 0;
    SubScopeSlot& subs = sub_slots_[next_frame_ % kBufferedFrames];
    subs.count = subs.dropped = 0;
    current_subs_ = sub_scopes_enabled_ ? &subs : nullptr;

    const bool capture_next = environment_flag("EI_RENDERER_CAPTURE_NEXT") && !capture_next_consumed_;
    capture_active_ = capture_requested_ || capture_next || requested_capture_frame_ == next_frame_;
    capture_requested_ = false;
    if (capture_next) capture_next_consumed_ = true;
    if (capture_active_) {
        char directory[MAX_PATH]{};
        std::snprintf(directory, sizeof(directory), "%s\\captures\\frame_%06llu",
                      debug_root_.c_str(), static_cast<unsigned long long>(next_frame_));
        capture_directory_ = directory;
        CreateDirectoryA(debug_root_.c_str(), nullptr);
        const std::string captures = debug_root_ + "\\captures";
        CreateDirectoryA(captures.c_str(), nullptr);
        CreateDirectoryA(capture_directory_.c_str(), nullptr);
        if (!pending_normal_frame_metadata_.empty()) {
            const std::string metadata_path = capture_directory_ + "\\normal_frame.json";
            FILE* file = nullptr;
            fopen_s(&file, metadata_path.c_str(), "wb");
            if (file) {
                std::fwrite(pending_normal_frame_metadata_.data(), 1,
                            pending_normal_frame_metadata_.size(), file);
                std::fclose(file);
            }
        }
        pending_normal_frame_metadata_.clear();
    } else {
        capture_directory_.clear();
    }

    if (!timestamps_) context_->Begin(slot.disjoint.Get());
    if (attribution_.enabled() && attribution_.begin_interval(next_frame_)) {
        attribution_.record_device_begin();
        mark(slot.device_span, false);
    }
    frame_active_ = true;
}

void RenderProfiler::add_cpu_time(const char* name, std::int64_t qpc_ticks) noexcept {
    if (!frame_active_ || !current_ || !name) return;
    auto& slot = *current_;
    for (UINT index = 0; index < slot.cpu_scope_count; ++index)
        if (slot.cpu_scopes[index].name == name) {
            slot.cpu_scopes[index].ticks += qpc_ticks; ++slot.cpu_scopes[index].calls; return;
        }
    if (slot.cpu_scope_count < slot.cpu_scopes.size())
        slot.cpu_scopes[slot.cpu_scope_count++] = {name, qpc_ticks, 1};
}

void RenderProfiler::begin_pass(const char* name) {
    if (!frame_active_ || !current_) return;
    if (pass_active_ && std::strncmp(current_->passes[current_pass_].name.data(),
                                    name ? name : "unnamed",
                                    current_->passes[current_pass_].name.size()) == 0) return;
    if (pass_active_) end_pass();
    if (current_pass_ >= current_->queries.size()) {
        QueryPair pair;
        const D3D11_QUERY_DESC timestamp_desc{D3D11_QUERY_TIMESTAMP, 0};
        if (!timestamps_ && (FAILED(device_->CreateQuery(&timestamp_desc, &pair.begin)) ||
            FAILED(device_->CreateQuery(&timestamp_desc, &pair.end)))) {
            if (attribution_.enabled()) attribution_.mark_invalid(attribution_scope_overflow);
            return;
        }
        current_->queries.push_back(std::move(pair));
        current_->passes.emplace_back();
    }
    PassRecord& pass = current_->passes[current_pass_];
    pass = {};
    strncpy_s(pass.name.data(), pass.name.size(), name ? name : "unnamed", _TRUNCATE);
    pass.cpu_begin = qpc_now();
    if (attribution_.enabled()) pass.attribution_begin = attribution_.sample_point();
    mark(current_->queries[current_pass_], false);
    pass_active_ = true;
}

bool RenderProfiler::current_pass_is(const char* name) const {
    if (!pass_active_ || !current_) return false;
    const char* expected = name ? name : "unnamed";
    return std::strncmp(current_->passes[current_pass_].name.data(), expected,
                        current_->passes[current_pass_].name.size()) == 0;
}

void RenderProfiler::add_draw(std::uint64_t triangles, std::uint64_t legacy_draws) {
    if (!pass_active_ || !current_) return;
    auto& counters = current_->passes[current_pass_].counters;
    counters.draws += legacy_draws;
    counters.triangles += triangles;
}

void RenderProfiler::add_batch(std::uint64_t count) {
    if (pass_active_ && current_) current_->passes[current_pass_].counters.batches += count;
}

void RenderProfiler::add_upload(std::uint64_t bytes) {
    if (!pass_active_ || !current_) return;
    auto& counters = current_->passes[current_pass_].counters;
    ++counters.uploads;
    counters.upload_bytes += bytes;
}

void RenderProfiler::add_capture(std::uint64_t cpu_ticks, std::uint64_t bytes, bool appended) {
    if (!pass_active_ || !current_) return;
    auto& counters = current_->passes[current_pass_].counters;
    counters.capture_cpu_ticks += cpu_ticks;
    ++counters.capture_calls;
    counters.capture_bytes += bytes;
    if (appended) ++counters.packets_appended;
    else ++counters.packets_created;
}

void RenderProfiler::sub_scope(const char* name) {
    if (!pass_active_ || !current_subs_) return;
    SubScopeSlot& subs = *current_subs_;
    const char* label = name ? name : "unnamed";
    if (sub_active_) {
        const auto& open = subs.records[subs.count];
        if (std::strncmp(open.name.data(), label, open.name.size()) == 0) return;
        end_sub_scope();
    }
    if (subs.count >= subs.queries.size()) {
        QueryPair pair;
        const D3D11_QUERY_DESC timestamp_desc{D3D11_QUERY_TIMESTAMP, 0};
        // A failed creation only loses the diagnostic split, never a pass total.
        if (!timestamps_ && (FAILED(device_->CreateQuery(&timestamp_desc, &pair.begin)) ||
            FAILED(device_->CreateQuery(&timestamp_desc, &pair.end)))) {
            ++subs.dropped;
            return;
        }
        subs.queries.push_back(std::move(pair));
        subs.records.emplace_back();
    }
    SubScopeRecord& record = subs.records[subs.count];
    record = {};
    strncpy_s(record.name.data(), record.name.size(), label, _TRUNCATE);
    record.parent = current_pass_;
    mark(subs.queries[subs.count], false);
    sub_active_ = true;
}

void RenderProfiler::add_sub_draw(std::uint64_t draws, std::uint64_t triangles) {
    if (!sub_active_ || !current_subs_) return;
    auto& record = current_subs_->records[current_subs_->count];
    record.draws += draws;
    record.triangles += triangles;
    ++record.batches;
}

void RenderProfiler::end_sub_scope() {
    if (!sub_active_ || !current_subs_) return;
    mark(current_subs_->queries[current_subs_->count], true);
    ++current_subs_->count;
    sub_active_ = false;
}

void RenderProfiler::end_pass(ID3D11Texture2D* capture_source) {
    if (!pass_active_ || !current_) return;
    end_sub_scope();
    PassRecord& pass = current_->passes[current_pass_];
    mark(current_->queries[current_pass_], true);
    pass.cpu_end = qpc_now();
    if (attribution_.enabled()) {
        pass.attribution_end = attribution_.sample_point();
        attribution_.add_scope(pass.name.data(), pass.attribution_begin,
                               pass.attribution_end, current_->frame,
                               pass.counters.uploads, pass.counters.upload_bytes,
                               pass.counters.draws, pass.counters.triangles,
                               pass.counters.batches, pass.counters.capture_cpu_ticks,
                               pass.counters.capture_calls, pass.counters.capture_bytes,
                               pass.counters.packets_created, pass.counters.packets_appended);
    }
    // D3D12: no D3D11 back buffer to copy; the pass manifest is still written.
    if (capture_active_ && (capture_source || timestamps_)) capture_pass(capture_source, pass, current_pass_);
    ++current_pass_;
    current_->pass_count = current_pass_;
    pass_active_ = false;
}

void RenderProfiler::record_attribution_workload(
    const BridgeProfileSnapshot* bridge,
    const RendererResourceSnapshot& resources) noexcept {
    // Every Present consumes/reset, including closed/unmarked recorder intervals.
    const auto camp=camp_inventory_take_scroll_cost();
    if (!attribution_.enabled() || !attribution_.interval_open()) return;
    LARGE_INTEGER begin{}, end{};
    if (!QueryPerformanceCounter(&begin)) {
        attribution_.mark_invalid(attribution_query_failed);
        return;
    }
    RendererWorkloadSnapshot workload{};
    if(camp.observed){
        workload.set(RendererWorkloadField::camp_scroll_valid,camp.valid?1:0);
        if(camp.valid){
            workload.set(RendererWorkloadField::camp_scroll_mode,camp.mode);
            workload.set(RendererWorkloadField::camp_scroll_generation,camp.generation);
            if(camp.item_calls){
                workload.set(RendererWorkloadField::camp_scroll_first_qpc,camp.first_qpc);
                workload.set(RendererWorkloadField::camp_scroll_last_qpc,camp.last_qpc);
            }
            workload.set(RendererWorkloadField::camp_scroll_item_calls,camp.item_calls);
            workload.set(RendererWorkloadField::camp_scroll_item_wall_qpc,camp.item_wall_qpc);
            workload.set(RendererWorkloadField::camp_scroll_item_max_wall_qpc,camp.item_max_wall_qpc);
            workload.set(RendererWorkloadField::camp_scroll_miss_calls,camp.miss_calls);
            workload.set(RendererWorkloadField::camp_scroll_miss_wall_qpc,camp.miss_wall_qpc);
            workload.set(RendererWorkloadField::camp_scroll_miss_max_wall_qpc,camp.miss_max_wall_qpc);
            workload.set(RendererWorkloadField::camp_scroll_figure_misses,camp.figure_misses);
            const auto& stash=camp.grids[0];
            if(stash.observed){
                workload.set(RendererWorkloadField::camp_scroll_stash_first,stash.first);
                workload.set(RendererWorkloadField::camp_scroll_stash_count,stash.count);
                workload.set(RendererWorkloadField::camp_scroll_stash_category,stash.category);
                workload.set(RendererWorkloadField::camp_scroll_stash_columns,stash.columns);
                workload.set(RendererWorkloadField::camp_scroll_stash_rows,stash.rows);
            }
            const auto& vendor=camp.grids[1];
            if(vendor.observed){
                workload.set(RendererWorkloadField::camp_scroll_vendor_first,vendor.first);
                workload.set(RendererWorkloadField::camp_scroll_vendor_count,vendor.count);
                workload.set(RendererWorkloadField::camp_scroll_vendor_category,vendor.category);
                workload.set(RendererWorkloadField::camp_scroll_vendor_columns,vendor.columns);
                workload.set(RendererWorkloadField::camp_scroll_vendor_rows,vendor.rows);
            }
        }
    }
    workload.set(RendererWorkloadField::native_opaque_draws_partial,
                 resources.native_opaque_draws);
    workload.set(RendererWorkloadField::native_cutout_draws_partial,
                 resources.native_cutout_draws);
    workload.set(RendererWorkloadField::ui_blit_queue_cpu_qpc_partial,
                 resources.ui_blit_queue_cpu_qpc);
    workload.set(RendererWorkloadField::ui_blit_queue_calls_partial,
                 resources.ui_blit_queue_calls);
    workload.set(RendererWorkloadField::ui_blit_conversion_cpu_qpc_partial,
                 resources.ui_blit_conversion_cpu_qpc);
    workload.set(RendererWorkloadField::ui_blit_conversion_calls_partial,
                 resources.ui_blit_conversion_calls);
    workload.set(RendererWorkloadField::ui_blit_conversion_pixels_partial,
                 resources.ui_blit_conversion_pixels);
    if (bridge) {
        workload.set(RendererWorkloadField::world_draw_calls_partial, bridge->world_draw_calls);
        workload.set(RendererWorkloadField::object_draw_calls_partial, bridge->object_draw_calls);
        workload.set(RendererWorkloadField::simple_calls_partial, bridge->simple_calls);
        workload.set(RendererWorkloadField::figure_placed_calls_partial,
                     bridge->figure_placed_calls);
        workload.set(RendererWorkloadField::figure_lit_calls_partial, bridge->figure_lit_calls);
        workload.set(RendererWorkloadField::figure_gpu_owned_calls_partial,
                     bridge->figure_gpu_owned_calls);
        workload.set(RendererWorkloadField::figure_cpu_fallback_calls_partial,
                     bridge->figure_cpu_fallback_calls);
        workload.set(RendererWorkloadField::terrain_calls_partial, bridge->terrain_calls);
        workload.set(RendererWorkloadField::terrain_outer_calls_partial,
                     bridge->terrain_outer_calls);
        workload.set(RendererWorkloadField::terrain_gpu_owned_calls_partial,
                     bridge->terrain_gpu_owned_calls);
        workload.set(RendererWorkloadField::terrain_cpu_fallback_calls_partial,
                     bridge->terrain_cpu_fallback_calls);
        workload.set(RendererWorkloadField::terrain_candidate_sectors_partial,
                     bridge->terrain_candidate_sectors);
        workload.set(RendererWorkloadField::terrain_visible_sectors_partial,
                     bridge->terrain_visible_sectors);
        workload.set(RendererWorkloadField::terrain_frustum_rejected_sectors_partial,
                     bridge->terrain_frustum_rejected_sectors);
    }
    if (!QueryPerformanceCounter(&end) || end.QuadPart < begin.QuadPart) {
        attribution_.mark_invalid(attribution_query_failed);
        return;
    }
    workload.set(RendererWorkloadField::profiler_observer_cpu_qpc,
                 static_cast<std::uint64_t>(end.QuadPart - begin.QuadPart));
    attribution_.set_renderer_workload(workload);
}

void RenderProfiler::end_frame(UINT present_interval, UINT present_flags) {
    if (!frame_active_ || !current_) return;
    if (pass_active_) end_pass();
    if (attribution_.enabled() && attribution_.interval_open()) {
        mark(current_->device_span, true);
        attribution_.record_device_end();
    }
    if (timestamps_) current_->frame_stamp = timestamps_->stamp();
    else context_->End(current_->disjoint.Get());
    if (attribution_.enabled()) attribution_.record_present_enter(present_interval, present_flags);
    current_->pending = true;
    present_slot_ = current_;
    current_ = nullptr;
    current_subs_ = nullptr;
    frame_active_ = false;
    capture_active_ = false;
    ++next_frame_;
}

void RenderProfiler::present_return(UINT width, UINT height, UINT present_interval,
                                    UINT present_flags, HRESULT result) {
    if (attribution_.enabled() && present_slot_ && attribution_.interval_open()) {
        attribution_.record_present_return(result);
        present_slot_->attribution = attribution_.take_current();
        present_slot_->attribution_present_returned = true;
        if (present_slot_->attribution.capture_selected) ++attribution_selected_pending_;
    }
    else if (attribution_.enabled()) {
        attribution_.submit_first_partial(present_slot_ ? present_slot_->frame : next_frame_,
                                          present_interval, present_flags, result);
    }
    present_slot_ = nullptr;
    if (SUCCEEDED(result)) begin_frame(width, height);
    (void)present_interval;
    (void)present_flags;
}

HRESULT gpu_map_read_bounded(ID3D11DeviceContext* context, ID3D11Device* device, ID3D11Resource* resource,
                             UINT subresource, D3D11_MAPPED_SUBRESOURCE& mapped, const char* what,
                             std::string* reason, DWORD timeout_ms) noexcept {
    if (!context || !resource) return E_INVALIDARG;
    const ULONGLONG start = GetTickCount64();
    bool flushed = false;
    for (;;) {
        const HRESULT hr = context->Map(resource, subresource, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
        if (hr != DXGI_ERROR_WAS_STILL_DRAWING) {
            if (FAILED(hr) && reason) {
                char text[96];std::snprintf(text, sizeof(text), "map failed hr=%08lx", static_cast<unsigned long>(hr));
                *reason = text;
            }
            return hr;
        }
        if (!flushed) { context->Flush(); flushed = true; }
        if (GetTickCount64() - start >= timeout_ms) break;
        SwitchToThread();
    }
    const HRESULT removed = device ? device->GetDeviceRemovedReason() : E_POINTER;
    char text[192];
    std::snprintf(text, sizeof(text), "%s: GPU readback not complete after %lu ms; device removed reason=%08lx",
                  what ? what : "readback", static_cast<unsigned long>(timeout_ms), static_cast<unsigned long>(removed));
    bridge_report_event("gpu_readback_timeout", text);
    if (reason) *reason = text;
    return E_PENDING;
}

bool RenderProfiler::resolve(FrameSlot& slot, bool wait) {
    if (!slot.pending || !context_) return true;
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{};
    HRESULT ready = S_FALSE;
    // A waiting resolve (F11 capture, shutdown) gives up after 2 s like the
    // readbacks: a GPU that stops completing work must not hang the caller.
    const ULONGLONG wait_start = GetTickCount64();
    do {
        if (timestamps_) {
            // D3D12: the frame's last stamp done means all of its stamps are.
            std::uint64_t ticks = 0;
            disjoint.Frequency = timestamps_->frequency();
            disjoint.Disjoint = FALSE;
            ready = !slot.frame_stamp || timestamps_->value(slot.frame_stamp, ticks) ? S_OK : S_FALSE;
        } else
        ready = context_->GetData(slot.disjoint.Get(), &disjoint, sizeof(disjoint),
                                  wait ? 0 : D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (ready == S_FALSE && wait) {
            if (GetTickCount64() - wait_start >= 2000) {
                char text[160];
                std::snprintf(text, sizeof(text), "profiler timestamps: not complete after 2000 ms; device removed reason=%08lx",
                              static_cast<unsigned long>(device_ ? device_->GetDeviceRemovedReason() : E_POINTER));
                bridge_report_event("gpu_readback_timeout", text);
                break;
            }
            SwitchToThread();
        }
    } while (ready == S_FALSE && wait);
    if (ready != S_OK) {
        // Continuous profiling is diagnostic and must never stall rendering.
        return false;
    }

    // Resolved in place (the snapshot is too large for the game thread's
    // stack); every field read below pass_count/child_count is rewritten.
    RenderProfileSnapshot& snapshot = latest_snapshot_;
    snapshot.frame = slot.frame;
    snapshot.width = slot.width;
    snapshot.height = slot.height;
    snapshot.gpu_valid = !disjoint.Disjoint;
    snapshot.frame_cpu_ms = 0.0;
    snapshot.frame_gpu_ms = 0.0;
    snapshot.pass_count = slot.pass_count;
    snapshot.passes.assign(slot.pass_count, ProfilePassSnapshot{});
    snapshot.child_count = 0;
    snapshot.children.clear();
    snapshot.children_dropped = 0;
    snapshot.cpu_scope_count = slot.cpu_scope_count;
    for (UINT index = 0; index < slot.cpu_scope_count; ++index) {
        auto& scope = snapshot.cpu_scopes[index];
        strncpy_s(scope.name.data(), scope.name.size(), slot.cpu_scopes[index].name, _TRUNCATE);
        scope.cpu_ms = 1000.0 * static_cast<double>(slot.cpu_scopes[index].ticks) / static_cast<double>(qpc_frequency_);
        scope.calls = slot.cpu_scopes[index].calls;
    }
    struct GpuInterval { UINT64 begin; UINT64 end; };
    std::vector<GpuInterval> gpu_intervals(slot.pass_count);
    UINT gpu_interval_count = 0;
    for (UINT index = 0; index < slot.pass_count; ++index) {
        const auto& pass = slot.passes[index];
        auto& resolved_pass = snapshot.passes[index];
        resolved_pass.name = pass.name;
        resolved_pass.counters = pass.counters;
        UINT64 gpu_begin = 0;
        UINT64 gpu_end = 0;
        const bool gpu_ready = !disjoint.Disjoint &&
            read(slot.queries[index], false, gpu_begin) && read(slot.queries[index], true, gpu_end);
        const double cpu_ms = 1000.0 * static_cast<double>(pass.cpu_end - pass.cpu_begin) /
                              static_cast<double>(qpc_frequency_);
        const double gpu_ms = gpu_ready && gpu_end >= gpu_begin && disjoint.Frequency
            ? 1000.0 * static_cast<double>(gpu_end - gpu_begin) / static_cast<double>(disjoint.Frequency)
            : -1.0;
        resolved_pass.cpu_ms = cpu_ms;
        resolved_pass.gpu_ms = gpu_ms;
        snapshot.frame_cpu_ms += cpu_ms;
        if (gpu_ms >= 0.0) snapshot.frame_gpu_ms += gpu_ms;
        else snapshot.gpu_valid = false;
        if (attribution_.enabled() && slot.attribution_present_returned && index < slot.attribution.scope_count) {
            auto& scope = slot.attribution.scopes[index];
            scope.gpu_resolved = gpu_ready && gpu_end >= gpu_begin;
            scope.gpu_ticks = scope.gpu_resolved ? gpu_end - gpu_begin : 0;
        }
        if (gpu_ready && gpu_end >= gpu_begin)
            gpu_intervals[gpu_interval_count++] = { gpu_begin, gpu_end };
    }
    {
        // Sub-scopes: same-named segments of one pass instance merge.
        const SubScopeSlot& subs = sub_slots_[static_cast<std::size_t>(&slot - slots_.data())];
        snapshot.children_dropped = subs.dropped;
        for (UINT index = 0; index < subs.count; ++index) {
            const auto& record = subs.records[index];
            if (record.parent >= snapshot.pass_count) continue;
            UINT64 gpu_begin = 0, gpu_end = 0;
            const bool gpu_ready = !disjoint.Disjoint && disjoint.Frequency &&
                read(subs.queries[index], false, gpu_begin) && read(subs.queries[index], true, gpu_end) &&
                gpu_end >= gpu_begin;
            const double gpu_ms = gpu_ready
                ? 1000.0 * static_cast<double>(gpu_end - gpu_begin) / static_cast<double>(disjoint.Frequency)
                : -1.0;
            UINT child = 0;
            while (child < snapshot.child_count &&
                   (snapshot.children[child].parent != record.parent ||
                    std::strncmp(snapshot.children[child].name.data(), record.name.data(),
                                 record.name.size()) != 0)) ++child;
            if (child == snapshot.child_count) snapshot.children.emplace_back();
            auto& merged = snapshot.children[child];
            if (child == snapshot.child_count) {
                ++snapshot.child_count;
                merged.name = record.name;
                merged.parent = record.parent;
                merged.segments = 0;
                merged.gpu_ms = 0.0;
                merged.draws = merged.triangles = merged.batches = 0;
            }
            ++merged.segments;
            merged.gpu_ms = merged.gpu_ms >= 0.0 && gpu_ms >= 0.0 ? merged.gpu_ms + gpu_ms : -1.0;
            merged.draws += record.draws;
            merged.triangles += record.triangles;
            merged.batches += record.batches;
        }
    }
    if (attribution_.enabled() && slot.attribution_present_returned) {
        UINT64 span_begin = 0, span_end = 0;
        const bool span_ready = !disjoint.Disjoint &&
            read(slot.device_span, false, span_begin) && read(slot.device_span, true, span_end);
        std::sort(gpu_intervals.begin(), gpu_intervals.begin() + gpu_interval_count,
                  [](const GpuInterval& left, const GpuInterval& right) { return left.begin < right.begin; });
        std::uint64_t named_union = 0;
        if (gpu_interval_count) {
            UINT64 union_begin = gpu_intervals[0].begin, union_end = gpu_intervals[0].end;
            for (UINT index = 1; index < gpu_interval_count; ++index) {
                if (gpu_intervals[index].begin > union_end) {
                    named_union += union_end - union_begin;
                    union_begin = gpu_intervals[index].begin;
                    union_end = gpu_intervals[index].end;
                } else union_end = std::max(union_end, gpu_intervals[index].end);
            }
            named_union += union_end - union_begin;
        }
        const bool all_scopes_resolved = gpu_interval_count == slot.attribution.scope_count;
        reconcile_gpu_attribution(slot.attribution, span_ready && all_scopes_resolved,
                                  disjoint.Disjoint, span_begin, span_end,
                                  named_union, disjoint.Frequency);
        if (slot.attribution.capture_selected) {
            if (attribution_.submit(slot.attribution)) {
                if (attribution_.finalize_if_full(debug_root_.c_str())) refresh_enabled();
            }
            if (attribution_selected_pending_) --attribution_selected_pending_;
            if (attribution_selected_pending_ == 0 && attribution_.marker_window_flush_pending()) {
                (void)attribution_.flush_completed_window(debug_root_.c_str());
                refresh_enabled();
            }
        }
    }
    has_latest_snapshot_ = true;
    slot.pending = false;
    return true;
}

void RenderProfiler::clear_shadow_temporal_pair() noexcept {
    shadow_temporal_slots_ = {};
    shadow_temporal_first_frame_ = 0;
    shadow_temporal_last_world_completion_ = 0;
    shadow_temporal_queued_ = 0;
    shadow_temporal_armed_ = false;
}

bool RenderProfiler::request_shadow_temporal_pair(ID3D11Texture2D* color,
    ID3D11Texture2D* depth, ID3D11Texture2D* shadows,
    std::uint64_t first_presentation_frame,
    std::uint64_t completed_world_serial) noexcept {
    if (!device_ || !context_ || shadow_temporal_armed_ || !color || !depth || !shadows)
        return false;
    D3D11_TEXTURE2D_DESC descriptions[3]{};
    color->GetDesc(&descriptions[0]);
    depth->GetDesc(&descriptions[1]);
    shadows->GetDesc(&descriptions[2]);
    const auto& c = descriptions[0];
    const auto& d = descriptions[1];
    const auto& s = descriptions[2];
    if (c.Width != d.Width || c.Height != d.Height || c.ArraySize != 1 ||
        c.SampleDesc.Count != 1 ||
        d.Format != DXGI_FORMAT_R24G8_TYPELESS || d.SampleDesc.Count != 1 ||
        s.Format != DXGI_FORMAT_R32_TYPELESS || s.ArraySize != 5 ||
        s.Width != s.Height || (s.Width != 1024 && s.Width != 2048 && s.Width != 4096) ||
        s.SampleDesc.Count != 1 ||
        c.MipLevels != 1 || d.MipLevels != 1 || s.MipLevels != 1) return false;
    clear_shadow_temporal_pair();
    for (auto& slot : shadow_temporal_slots_) {
        ID3D11Texture2D* targets[3]{};
        for (unsigned index = 0; index < 3; ++index) {
            auto desc = descriptions[index];
            desc.Usage = D3D11_USAGE_STAGING;
            desc.BindFlags = desc.MiscFlags = 0;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            if (FAILED(device_->CreateTexture2D(&desc, nullptr, &targets[index]))) {
                for (auto* target : targets) if (target) target->Release();
                clear_shadow_temporal_pair();
                return false;
            }
        }
        slot.color.Attach(targets[0]);
        slot.depth.Attach(targets[1]);
        slot.shadows.Attach(targets[2]);
    }
    shadow_temporal_first_frame_ = first_presentation_frame;
    shadow_temporal_last_world_completion_ = completed_world_serial;
    shadow_temporal_armed_ = true;
    return true;
}

bool RenderProfiler::queue_shadow_temporal_frame(ID3D11Texture2D* color,
    ID3D11Texture2D* depth, ID3D11Texture2D* shadows,
    const ShadowTemporalFrameSnapshot& snapshot) noexcept {
    if (!shadow_temporal_due(snapshot.presentation_frame,
            snapshot.world_completion_serial) || !color || !depth || !shadows)
        return false;
    auto& slot = shadow_temporal_slots_[shadow_temporal_queued_];
    ID3D11Texture2D* sources[3]{color, depth, shadows};
    ID3D11Texture2D* targets[3]{slot.color.Get(), slot.depth.Get(), slot.shadows.Get()};
    for (unsigned index = 0; index < 3; ++index) {
        D3D11_TEXTURE2D_DESC source{}, target{};
        sources[index]->GetDesc(&source);
        targets[index]->GetDesc(&target);
        if (source.Width != target.Width || source.Height != target.Height ||
            source.Format != target.Format || source.ArraySize != target.ArraySize) {
            clear_shadow_temporal_pair();
            return false;
        }
    }
    slot.snapshot = snapshot;
    LARGE_INTEGER copy_begin{}, copy_end{};
    QueryPerformanceCounter(&copy_begin);
    context_->CopyResource(slot.color.Get(), color);
    context_->CopyResource(slot.depth.Get(), depth);
    context_->CopyResource(slot.shadows.Get(), shadows);
    QueryPerformanceCounter(&copy_end);
    slot.snapshot.copy_enqueue_qpc_ticks = copy_end.QuadPart - copy_begin.QuadPart;
    ++shadow_temporal_queued_;
    shadow_temporal_last_world_completion_ = snapshot.world_completion_serial;
    return true;
}

ShadowTemporalPresentOutcome RenderProfiler::shadow_temporal_present(
    std::uint64_t presentation_frame) noexcept {
    if (!shadow_temporal_armed_ || presentation_frame < shadow_temporal_first_frame_)
        return ShadowTemporalPresentOutcome::none;
    const auto expected = presentation_frame - shadow_temporal_first_frame_ + 1;
    if (expected > 2 || shadow_temporal_queued_ < expected) {
        clear_shadow_temporal_pair();
        return ShadowTemporalPresentOutcome::unavailable;
    }
    if (expected == 1) return ShadowTemporalPresentOutcome::waiting;
    const bool written = export_shadow_temporal_pair();
    clear_shadow_temporal_pair();
    return written ? ShadowTemporalPresentOutcome::complete
                   : ShadowTemporalPresentOutcome::export_failed;
}

bool RenderProfiler::export_shadow_temporal_pair() {
    const stall_watch::Exempt disk_export;  // diagnostic readback and disk output
    try {
        const auto& first = shadow_temporal_slots_[0].snapshot;
        const auto& second = shadow_temporal_slots_[1].snapshot;
        SYSTEMTIME started{};
        GetSystemTime(&started);
        char name[96]{};
        std::snprintf(name, sizeof(name),
            "shadow_temporal_%06llu_%04u%02u%02uT%02u%02u%02u%03uZ_%lu",
            static_cast<unsigned long long>(first.presentation_frame),
            started.wYear, started.wMonth, started.wDay, started.wHour,
            started.wMinute, started.wSecond, started.wMilliseconds,
            static_cast<unsigned long>(GetCurrentProcessId()));
        const std::string captures = debug_root_ + "\\captures";
        const std::string directory = captures + "\\" + name;
        if (directory.size() >= MAX_PATH ||
            (!CreateDirectoryA(debug_root_.c_str(), nullptr) &&
                GetLastError() != ERROR_ALREADY_EXISTS) ||
            (!CreateDirectoryA(captures.c_str(), nullptr) &&
                GetLastError() != ERROR_ALREADY_EXISTS) ||
            !CreateDirectoryA(directory.c_str(), nullptr)) return false;
        const double interval_ms = second.sample_qpc > first.sample_qpc
            ? 1000.0 * static_cast<double>(second.sample_qpc - first.sample_qpc) /
                static_cast<double>(qpc_frequency_) : 0.0;
        bool complete = true;
        for (unsigned ordinal = 0; ordinal < 2; ++ordinal) {
            const auto& slot = shadow_temporal_slots_[ordinal];
            const auto& snapshot = slot.snapshot;
            const std::string stem = "frame_" + std::to_string(ordinal);
            const std::string color_name = stem + "_world.bmp";
            const std::string depth_name = stem + "_world_depth.d24s8";
            const std::string shadow_name = stem + "_shadow.r32farray";
            const bool color_written = write_bmp(directory + "\\" + color_name,
                slot.color.Get(), true);
            const bool depth_written = write_shadow_temporal_depth(directory + "\\" + depth_name,
                slot.depth.Get());
            const bool shadow_written = write_shadow_temporal_maps(directory + "\\" + shadow_name,
                slot.shadows.Get());
            D3D11_TEXTURE2D_DESC color_desc{}, shadow_desc{};
            slot.color->GetDesc(&color_desc);
            slot.shadows->GetDesc(&shadow_desc);
            std::ostringstream output;
            output.imbue(std::locale::classic());
            output << std::setprecision(12);
            const auto write_array = [&](const auto& values) {
                output << '[';
                for (std::size_t index = 0; index < values.size(); ++index)
                    output << (index ? "," : "") << values[index];
                output << ']';
            };
            output << "{\"schema\":2,\"capture\":\"f11_shadow_temporal_pair\",\"ordinal\":"
                << ordinal << ",\"requested_frames\":2,\"sample_boundary\":\"first_depth_clear_after_completed_world\""
                << ",\"presentation_frame\":" << snapshot.presentation_frame
                << ",\"world_bridge_frame\":" << snapshot.world_bridge_frame
                << ",\"world_completion_serial\":" << snapshot.world_completion_serial
                << ",\"sample_qpc\":" << snapshot.sample_qpc
                << ",\"qpc_frequency\":" << qpc_frequency_
                << ",\"copy_enqueue_ms\":"
                << (1000.0 * static_cast<double>(snapshot.copy_enqueue_qpc_ticks) /
                    static_cast<double>(qpc_frequency_))
                << ",\"capture_perturbation\":\"three GPU copies per sample, including one complete sun-shadow array; no CPU readback or file I/O between samples\""
                << ",\"pair_sample_interval_ms\":" << interval_ms
                << ",\"world_time_valid\":" << (snapshot.world_time_valid ? "true" : "false")
                << ",\"world_time\":" << snapshot.world_time
                << ",\"pair_world_time_delta\":"
                << (first.world_time_valid && second.world_time_valid
                    ? second.world_time - first.world_time : 0.0)
                << ",\"clear_flags\":" << snapshot.clear_flags
                << ",\"clear_rectangles\":" << snapshot.clear_rectangles
                << ",\"clear_full_surface\":" << (snapshot.clear_full_surface ? "true" : "false")
                << ",\"width\":" << color_desc.Width << ",\"height\":" << color_desc.Height
                << ",\"world_image\":\"" << color_name << "\",\"world_depth_image\":\""
                << depth_name << "\",\"shadow_depth_image\":\"" << shadow_name
                << "\",\"color_written\":" << (color_written ? "true" : "false")
                << ",\"depth_written\":" << (depth_written ? "true" : "false")
                << ",\"shadow_written\":" << (shadow_written ? "true" : "false")
                << ",\"world_depth_encoding\":\"top-down little-endian D24_UNORM_S8_UINT, tightly packed four bytes per pixel\""
                << ",\"shadow_width\":" << shadow_desc.Width
                << ",\"shadow_height\":" << shadow_desc.Height
                << ",\"shadow_slices\":" << shadow_desc.ArraySize
                << ",\"shadow_depth_encoding\":\"five top-down " << shadow_desc.Width
                << 'x' << shadow_desc.Height << " R32_FLOAT slices, tightly packed in slice order\""
                << ",\"selector_rule\":\"first camera-containing slice; slices[4].bias[2]>=0.5 also requires guarded light containment\""
                << ",\"shadow_projection_change_scope\":\"first numeric difference only; world_to_light includes XY and Z\""
                << ",\"authored_sun_valid\":" << (snapshot.authored_sun_valid ? "true" : "false")
                << ",\"authored_sun_direction\":";
            write_array(snapshot.authored_sun_direction);
            output << ",\"authored_sun_diffuse\":";
            write_array(snapshot.authored_sun_diffuse);
            output << ",\"scene_frame_serial\":" << snapshot.scene_frame_serial
                << ",\"world_to_clip_valid\":"
                << (snapshot.world_to_clip_valid ? "true" : "false")
                << ",\"world_to_clip\":";
            write_array(snapshot.world_to_clip);
            output << ",\"shadows_enabled\":" << (snapshot.shadows_enabled ? "true" : "false")
                << ",\"shadow_publication_serial\":" << snapshot.publication_serial
                << ",\"shadow_begin_calls\":" << snapshot.shadow_begin_calls
                << ",\"previous_valid_mask\":" << unsigned(snapshot.previous_valid_mask)
                << ",\"current_enabled_mask\":" << unsigned(snapshot.current_enabled_mask)
                << ",\"world_to_light_changed_mask\":" << unsigned(snapshot.world_to_light_changed_mask)
                << ",\"camera_slice_changed_mask\":" << unsigned(snapshot.camera_slice_changed_mask)
                << ",\"bias_changed_mask\":" << unsigned(snapshot.bias_changed_mask)
                << ",\"receiver_bounds_changed_mask\":" << unsigned(snapshot.receiver_bounds_changed_mask)
                << ",\"validity_changed_mask\":" << unsigned(snapshot.validity_changed_mask)
                << ",\"dirty_mask\":" << unsigned(snapshot.dirty_mask)
                << ",\"source_dirty_mask\":" << unsigned(snapshot.source_dirty_mask)
                << ",\"source_changed_visuals\":" << snapshot.source_changed_visuals
                << ",\"source_changed_terrain\":" << snapshot.source_changed_terrain
                << ",\"slices\":[";
            for (std::size_t index = 0; index < snapshot.slices.size(); ++index) {
                const auto& slice = snapshot.slices[index];
                const float radius = slice.bias[0] * (float(shadow_desc.Width) * .5f);
                const float depth_span = slice.bias[1] > 0 ? 1.f / slice.bias[1] : 0.f;
                output << (index ? "," : "") << "{\"index\":" << index
                    << ",\"enabled\":" << (slice.enabled ? "true" : "false")
                    << ",\"valid\":" << (slice.valid ? "true" : "false")
                    << ",\"caster_revisions\":" << slice.caster_revisions
                    << ",\"light_center\":[" << -slice.world_to_light[3] * radius << ','
                    << -slice.world_to_light[7] * radius << "]"
                    << ",\"light_radius\":" << radius
                    << ",\"light_depth\":[" << -slice.world_to_light[11] * depth_span << ','
                    << (1.f - slice.world_to_light[11]) * depth_span << "]"
                    << ",\"bias\":";
                write_array(slice.bias);
                output << ",\"world_to_light\":";
                write_array(slice.world_to_light);
                output << ",\"camera_slice\":";
                write_array(slice.camera_slice);
                output << ",\"receiver_minimum\":";
                write_array(slice.receiver_minimum);
                output << ",\"receiver_maximum\":";
                write_array(slice.receiver_maximum);
                output << '}';
            }
            output << "]}\n";
            const std::string metadata = output.str();
            FILE* file = nullptr;
            fopen_s(&file, (directory + "\\" + stem + ".json").c_str(), "wb");
            if (!file) return false;
            const bool wrote = std::fwrite(metadata.data(), 1, metadata.size(), file) ==
                metadata.size();
            const bool metadata_written = std::fclose(file) == 0 && wrote;
            complete = color_written && depth_written && shadow_written &&
                metadata_written && complete;
        }
        return complete;
    } catch (...) { return false; }
}

void RenderProfiler::capture_pass(ID3D11Texture2D* source, const PassRecord& pass, UINT ordinal) {
    const stall_watch::Exempt disk_export;  // F11 capture readback and disk output
    char image_name[128]{};
    std::snprintf(image_name, sizeof(image_name), "%02u_%s.bmp", ordinal, sanitize(pass.name.data()).c_str());
    const std::string image_path = capture_directory_ + "\\" + image_name;
    // Every pass is an uncompressed 4K image (~25-125 per capture, up to ~3 GB),
    // so a capture writes only the composed frame; per-pass images are opt-in
    // with EI_RENDERER_CAPTURE_ALL_PASSES=1. Pass timings are always recorded.
    static const bool all_passes = [] { char value[4]{};
        return GetEnvironmentVariableA("EI_RENDERER_CAPTURE_ALL_PASSES", value, sizeof(value)) && value[0] == '1'; }();
    const bool image = all_passes || std::strcmp(sanitize(pass.name.data()).c_str(), "ui_composition") == 0;
    if (image && source && !write_bmp(image_path, source)) return;
    const bool image_written = image && source;

    const std::string manifest_path = capture_directory_ + "\\passes.jsonl";
    FILE* file = nullptr;
    fopen_s(&file, manifest_path.c_str(), "ab");
    if (!file) return;
    const double cpu_ms = 1000.0 * static_cast<double>(pass.cpu_end - pass.cpu_begin) /
                          static_cast<double>(qpc_frequency_);
    std::fprintf(file,
                 "{\"ordinal\":%u,\"name\":\"%s\",\"image\":\"%s\",\"cpu_ms\":%.4f,"
                 "\"draws\":%llu,\"triangles\":%llu,\"batches\":%llu,\"uploads\":%llu,"
                 "\"upload_bytes\":%llu}\n",
                 ordinal, sanitize(pass.name.data()).c_str(), image_written ? image_name : "", cpu_ms,
                 static_cast<unsigned long long>(pass.counters.draws),
                 static_cast<unsigned long long>(pass.counters.triangles),
                 static_cast<unsigned long long>(pass.counters.batches),
                 static_cast<unsigned long long>(pass.counters.uploads),
                 static_cast<unsigned long long>(pass.counters.upload_bytes));
    std::fclose(file);
}

bool RenderProfiler::write_bmp(const std::string& path, ID3D11Texture2D* source,
    bool source_is_staging) {
    if (!source || !device_ || !context_) return false;
    D3D11_TEXTURE2D_DESC desc{};
    source->GetDesc(&desc);
    if (desc.SampleDesc.Count != 1 ||
        (desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM && desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB &&
         desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM && desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)) return false;
    ComPtr<ID3D11Texture2D> staging;
    if (source_is_staging) {
        if (desc.Usage != D3D11_USAGE_STAGING ||
            !(desc.CPUAccessFlags & D3D11_CPU_ACCESS_READ)) return false;
        staging = source;
    } else {
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;
        if (FAILED(device_->CreateTexture2D(&desc, nullptr, &staging))) return false;
        context_->CopyResource(staging.Get(), source);
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(gpu_map_read_bounded(context_.Get(), device_.Get(), staging.Get(), 0, mapped, path.c_str()))) return false;

    BITMAPFILEHEADER file_header{};
    BITMAPINFOHEADER info_header{};
    const DWORD pixel_bytes = desc.Width * desc.Height * 4;
    file_header.bfType = 0x4d42;
    file_header.bfOffBits = sizeof(file_header) + sizeof(info_header);
    file_header.bfSize = file_header.bfOffBits + pixel_bytes;
    info_header.biSize = sizeof(info_header);
    info_header.biWidth = static_cast<LONG>(desc.Width);
    info_header.biHeight = -static_cast<LONG>(desc.Height);
    info_header.biPlanes = 1;
    info_header.biBitCount = 32;
    info_header.biCompression = BI_RGB;
    info_header.biSizeImage = pixel_bytes;

    FILE* file = nullptr;
    fopen_s(&file, path.c_str(), "wb");
    if (!file) {
        context_->Unmap(staging.Get(), 0);
        return false;
    }
    bool complete = std::fwrite(&file_header, sizeof(file_header), 1, file) == 1 &&
                    std::fwrite(&info_header, sizeof(info_header), 1, file) == 1;
    std::array<std::uint8_t, 4> converted{};
    const bool rgba = desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM || desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    for (UINT y = 0; y < desc.Height; ++y) {
        const auto* row = static_cast<const std::uint8_t*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch;
        if (!rgba) {
            complete = std::fwrite(row, 4, desc.Width, file) == desc.Width && complete;
        } else {
            for (UINT x = 0; x < desc.Width; ++x) {
                converted = {row[x * 4 + 2], row[x * 4 + 1], row[x * 4], row[x * 4 + 3]};
                complete = std::fwrite(converted.data(), 1, converted.size(), file) ==
                    converted.size() && complete;
            }
        }
    }
    complete = std::fclose(file) == 0 && complete;
    context_->Unmap(staging.Get(), 0);
    return complete;
}

bool RenderProfiler::write_shadow_temporal_depth(const std::string& path,
    ID3D11Texture2D* staging) {
    if (!staging || !context_) return false;
    D3D11_TEXTURE2D_DESC desc{};
    staging->GetDesc(&desc);
    if (desc.Format != DXGI_FORMAT_R24G8_TYPELESS || desc.SampleDesc.Count != 1 ||
        desc.MipLevels != 1 || desc.ArraySize != 1 ||
        desc.Usage != D3D11_USAGE_STAGING ||
        !(desc.CPUAccessFlags & D3D11_CPU_ACCESS_READ)) return false;
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(gpu_map_read_bounded(context_.Get(), device_.Get(), staging, 0, mapped, path.c_str()))) return false;
    FILE* file = nullptr;
    fopen_s(&file, path.c_str(), "wb");
    if (!file) { context_->Unmap(staging, 0); return false; }
    const std::size_t row_bytes = std::size_t(desc.Width) * 4;
    bool complete = true;
    for (UINT y = 0; y < desc.Height; ++y) {
        const auto* row = static_cast<const std::uint8_t*>(mapped.pData) +
            std::size_t(y) * mapped.RowPitch;
        if (std::fwrite(row, 1, row_bytes, file) != row_bytes) {
            complete = false;
            break;
        }
    }
    complete = std::fclose(file) == 0 && complete;
    context_->Unmap(staging, 0);
    return complete;
}

bool RenderProfiler::write_shadow_temporal_maps(const std::string& path,
    ID3D11Texture2D* staging) {
    if (!staging || !context_) return false;
    D3D11_TEXTURE2D_DESC desc{};
    staging->GetDesc(&desc);
    if (desc.Format != DXGI_FORMAT_R32_TYPELESS || !desc.Width ||
        desc.Width != desc.Height || desc.ArraySize != 5 || desc.MipLevels != 1 ||
        desc.Usage != D3D11_USAGE_STAGING ||
        !(desc.CPUAccessFlags & D3D11_CPU_ACCESS_READ)) return false;
    FILE* file = nullptr;
    fopen_s(&file, path.c_str(), "wb");
    if (!file) return false;
    bool complete = true;
    for (UINT slice = 0; slice < 5 && complete; ++slice) {
        const UINT subresource = D3D11CalcSubresource(0, slice, 1);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(gpu_map_read_bounded(context_.Get(), device_.Get(), staging, subresource, mapped, path.c_str()))) {
            complete = false;
            break;
        }
        for (UINT row = 0; row < desc.Height; ++row) {
            const auto* pixels = static_cast<const std::uint8_t*>(mapped.pData) +
                static_cast<std::size_t>(row) * mapped.RowPitch;
            const auto bytes = static_cast<std::size_t>(desc.Width) * 4;
            if (std::fwrite(pixels, 1, bytes, file) != bytes) {
                complete = false;
                break;
            }
        }
        context_->Unmap(staging, subresource);
    }
    complete = std::fclose(file) == 0 && complete;
    return complete;
}

std::string RenderProfiler::make_debug_root() const {
    return debug_output_root();
}

} // namespace owned
