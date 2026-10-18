#pragma once

#include "lifecycle_probe.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace owned {

enum class MobSourceRole : std::uint8_t;
class RetainedStaticBindingView;
struct RetainedSceneIdentitySnapshot;
struct RetainedTerrainSourceSnapshot;
struct RetainedTerrainSourceIdentity;
class RetainedStaticScene;
struct ResourceBlob;
struct ResourceFailure;
struct ResourceStoreStats;
struct ResourceStoreConfig;
struct ModelCpuAsset;
struct FigureCpuAsset;
struct TextureCpuAsset;
struct AnimationDatabaseAsset;
struct AnimationClipAsset;
struct OwnedVisualSurface;

struct VisualAssetLookupDiagnostic {
    std::int64_t decode_ticks = 0;
    bool cache_hit = false;
    bool cache_miss = false;
    bool decode_attempted = false;
};

// The same retained owner used by world membership and immutable map sources.
RetainedStaticScene& retained_visual_scene() noexcept;
bool retained_visual_resolve_selected(const std::wstring& physical_file,
    const std::string& name, const void* selected, std::uint32_t size, std::uint64_t file_offset,
    ResourceBlob& blob, std::uint64_t& source_generation, ResourceFailure& failure,
    struct SelectedResourceReadDiagnostic* diagnostic = nullptr,
    bool verified_read_only_mapping = false);
bool retained_visual_resolve_native_anm(const std::string& name,
    const void* selected, std::uint32_t size, ResourceBlob& blob,
    std::uint64_t& source_generation, ResourceFailure& failure);
bool retained_visual_copy_selected_memory(const std::string& name,
    const void* selected, std::uint32_t size, ResourceBlob& blob,
    std::uint64_t& source_generation, ResourceFailure& failure);
std::shared_ptr<const ModelCpuAsset> retained_visual_skeleton_asset(
    const ResourceBlob& model, const ResourceBlob& bones, const std::string& name,
    ResourceFailure& failure, VisualAssetLookupDiagnostic* diagnostic = nullptr);
std::shared_ptr<const FigureCpuAsset> retained_visual_figure_asset(
    const ResourceBlob& blob, ResourceFailure& failure,
    VisualAssetLookupDiagnostic* diagnostic = nullptr);
std::shared_ptr<const TextureCpuAsset> retained_visual_texture_asset(
    const ResourceBlob& blob, ResourceFailure& failure);
// Synchronous read of a completed generated-MMP source. Applies count/byte
// limits before the immutable copy; no borrowed engine address is retained.
// Only the existing completed writer wrappers publish completed/failed events.
// Other semantic acquisitions reuse the current generation without rescanning.
enum class GeneratedTextureEvent : std::uint8_t { current, completed, failed };
std::shared_ptr<const TextureCpuAsset> retained_visual_generated_texture_asset(
    const std::string& name, const void* bytes, std::uint32_t size,
    ResourceFailure& failure, GeneratedTextureEvent event = GeneratedTextureEvent::current);
std::shared_ptr<const AnimationDatabaseAsset> retained_visual_database_asset(
    const ResourceBlob& blob, ResourceFailure& failure);
std::shared_ptr<const AnimationDatabaseAsset> retained_visual_selected_database_asset(
    const std::string& name, const void* selected, std::uint32_t size,
    std::uint64_t& source_generation, ResourceFailure& failure);
std::shared_ptr<const AnimationClipAsset> retained_visual_clip_asset(
    const ResourceBlob& outer, const std::string& name, ResourceFailure& failure,
    const std::shared_ptr<const AnimationDatabaseAsset>& database = nullptr,
    std::uint64_t source_generation = 0);
enum class RetainedVisualSiblingResult : std::uint8_t {
    unavailable,
    resolved,
    rejected,
};
RetainedVisualSiblingResult retained_visual_sibling_clip_asset(
    const std::shared_ptr<const AnimationDatabaseAsset>& database,
    std::uint64_t source_generation, const std::string& outer_name,
    const std::string& clip_name, std::shared_ptr<const AnimationClipAsset>& clip,
    ResourceFailure& failure);
std::shared_ptr<const OwnedVisualSurface> retained_visual_surface_value(
    std::uint64_t id, std::uint64_t generation) noexcept;
// pixels/bytes are the completed generation's CPU bytes, read only to
// establish alpha facts; the published value keeps no reference to them.
// An owned loader may instead provide the already-computed immutable alpha facts.
bool retained_visual_complete_surface(const OwnedVisualSurface& description,
    const std::uint8_t* pixels, std::size_t bytes,
    std::shared_ptr<const OwnedVisualSurface>& value, bool alpha_prepared = false) noexcept;
void retained_visual_retire_surface(std::uint64_t id) noexcept;

// Active game configuration, with optional developer mount overrides. Initialization and
// shutdown occur through normal DirectDraw object lifetime, outside DllMain.
void owned_resource_runtime_acquire(const wchar_t* game_registry_key = nullptr) noexcept;
void owned_resource_runtime_poll() noexcept;
bool owned_visual_resource_mounts(ResourceStoreConfig& mounts);
bool owned_text_resource_mounts(ResourceStoreConfig& mounts);
void owned_resource_runtime_release() noexcept;

bool retained_static_runtime_requested() noexcept;
// The configured first-wins resource mounts (game archives), for renderer
// assets read by name such as the critters' creature models and skins.
struct ResourceStoreConfig;
// True only for the EI Starter registry root, whose startup loads the addon
// named by Starter's AddonPath; the stock root never loads one.
bool registry_root_loads_starter_addon(const wchar_t* game_registry_key) noexcept;
// Decode the compiled mod configuration's visual archive arrays in first-wins
// order. Paths supplied here are the already resolved game/addon directories.
bool derive_owned_resource_mounts(const std::wstring& work_path,
    const std::wstring& resource_path, const std::wstring& addon_path,
    const std::vector<std::uint8_t>& config, ResourceStoreConfig& mounts,
    ResourceFailure& failure, ResourceStoreConfig* text_mounts = nullptr);
bool owned_resource_runtime_mounts(ResourceStoreConfig& mounts) noexcept;
std::uint64_t retained_static_begin_source(MobSourceRole role,
                                           const char* name,
                                           const char* selected_mob = nullptr) noexcept;
bool retained_static_record_parser_source(std::uint64_t transaction,
                                          const char* name) noexcept;
void retained_static_record_admission(std::uint64_t transaction,
                                      std::uint32_t nid,
                                      std::uint32_t family,
                                      std::int32_t signed_id) noexcept;
void retained_static_finish_source(std::uint64_t transaction,
                                   bool owner_success,
                                   bool common_parser_ran) noexcept;
// Load timing for the load reveal log: QPC of the latest main source begin
// (the game's shared map/save loader entry) and the QPC ticks its finish spent
// waiting for the owned map decode. Zero before the first main load.
struct MainLoadTiming { std::int64_t begin_qpc = 0, owned_wait_qpc = 0; };
MainLoadTiming retained_static_main_load_timing() noexcept;
void retained_static_lifecycle_added(const LifecycleIdentity& identity) noexcept;
void retained_static_lifecycle_removed(std::int32_t signed_id) noexcept;
void retained_static_positive_reset(std::uint64_t new_epoch) noexcept;
// World change (renderer release_world_state): expired identity-index entries
// and the retained scene's per-frame scratch.
void owned_resource_runtime_release_world_bookkeeping() noexcept;
RetainedStaticBindingView retained_static_binding_view() noexcept;
RetainedSceneIdentitySnapshot retained_static_scene_identity_snapshot();
RetainedTerrainSourceSnapshot retained_static_terrain_source_snapshot() noexcept;
RetainedTerrainSourceIdentity retained_static_terrain_source_identity() noexcept;
ResourceStoreStats owned_visual_resource_stats() noexcept;   // temporary: memory attribution
// Temporary (#142): CPU texture bytes the live visual textures still hold.
std::uint64_t owned_visual_texture_source_bytes() noexcept;

#ifdef EID3D11_TESTING
struct OwnedVisualClipReuseTestStats {
    std::uint64_t outer_opens = 0;
    std::uint64_t member_copies = 0;
    std::uint64_t clip_decodes = 0;
};
OwnedVisualClipReuseTestStats owned_visual_clip_reuse_for_test() noexcept;
ResourceStoreStats owned_visual_byte_budget_for_test() noexcept;
void owned_visual_set_byte_limit_for_test(std::uint64_t bytes) noexcept;
// End the synthetic resolver epoch while the real proxy fixture remains alive.
void owned_resource_runtime_close_selected_for_test() noexcept;
struct OwnedResourceRuntimeTestState {
    std::uint64_t active_transaction = 0;
    std::size_t pending_loads = 0;
    bool polling = false;
    bool owner_finished = false;
};
bool owned_resource_runtime_wait_active_ready_for_test() noexcept;
bool owned_resource_runtime_hold_active_completion_for_test() noexcept;
bool owned_resource_runtime_wait_completion_held_for_test() noexcept;
bool owned_resource_runtime_wait_waiter_entered_for_test() noexcept;
void owned_resource_runtime_release_completion_for_test() noexcept;
OwnedResourceRuntimeTestState owned_resource_runtime_state_for_test() noexcept;
#endif

} // namespace owned
