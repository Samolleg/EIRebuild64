#pragma once

#include "owned_asset_loader.h"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace owned {

enum class HudVisualLane : std::uint8_t {
    none,
    weapon,
    item,
    spell,
    skill,
};

enum class HudResourceRole : std::uint8_t {
    texture,
    figure,
};

enum class HudPublicationFailureStage : std::uint8_t {
    none,
    resolver_unavailable,
    resolver,
    size,
    copy,
    texture_parse,
    texture_1555,
    dimension,
    figure_parse,
    shape_count,
    budget,
    lane_invalid,
    generation,
    current_receiver,
    ordinal,
};

constexpr std::size_t kHudFailureMemberBytes = 96u;

struct HudPublicationFailure {
    HudPublicationFailureStage stage = HudPublicationFailureStage::none;
    HudVisualLane lane = HudVisualLane::none;
    std::uint32_t ordinal = 0;
    ResourceFailureCode resource_failure = ResourceFailureCode::none;
    std::array<char, kHudFailureMemberBytes + 1u> logical_member{};
    std::uint8_t logical_member_bytes = 0;

    explicit operator bool() const noexcept {
        return stage != HudPublicationFailureStage::none;
    }
};

const char* hud_publication_failure_stage_name(
    HudPublicationFailureStage stage) noexcept;

// Interface figures are materialized by the supported native loader with a
// fixed zero complection. FIG8 therefore contributes its first shape-major
// position block and first bound; all remaining channels are shared.
bool hud_interface_figure_is_supported(
    const FigureCpuAsset& figure) noexcept;

struct HudIconPlacement {
    std::uint32_t atlas_width = 0;
    std::uint32_t source_width = 0;
    std::uint32_t source_height = 0;
    std::uint32_t copy_width = 0;
    std::uint32_t copy_height = 0;
    std::uint32_t destination_x = 0;
    std::uint32_t destination_y = 0;
    std::uint32_t source_x = 0;
    std::uint32_t source_y = 0;
    std::uint32_t variant = 0;
};

// Renderer-native identity for the producer's defined 0xffff output when the
// authoritative texture manager returns no selected member. This is an owned
// upload representation, not a fabricated resolved ResourceBlob.
constexpr std::uint32_t kHudNativeWhiteFormatSignature = 0x57445548u;

// Pure value publication. Engine screen/object/figure addresses are deliberately
// absent: synchronous bridge admission supplies lane and ordinal at draw time.
struct HudPublishedVisual {
    HudVisualLane lane = HudVisualLane::none;
    std::uint64_t screen_generation = 0;
    std::uint64_t rebuild_serial = 0;
    std::uint32_t ordinal = 0;
    std::uint64_t geometry_identity = 0;
    std::uint64_t texture_identity = 0;
    std::uint64_t figure_generation = 0;
    std::uint64_t texture_generation = 0;
    std::uint64_t overlay_generation = 0;
    HudIconPlacement placement{};
    std::shared_ptr<const FigureCpuAsset> figure;
    std::shared_ptr<const TextureCpuAsset> texture;
    bool visible = true;
    bool complete = false;
};

using HudResolveResourceFn = bool (*)(
    void* context, HudResourceRole role, const char* canonical_name,
    ResourceBlob& blob, HudPublicationFailureStage& failure_stage,
    ResourceFailureCode& failure_code);

class HudVisualPublication {
public:
    explicit HudVisualPublication(void* resolve_context = nullptr,
                                  HudResolveResourceFn resolve = nullptr);

    void set_resolver(void* context, HudResolveResourceFn resolve) noexcept;
    void begin_role_recreation(HudResourceRole role) noexcept;
    void finish_role_recreation(HudResourceRole role, bool success) noexcept;
    void invalidate_role(HudResourceRole role) noexcept;
    std::uint64_t role_generation(HudResourceRole role) const noexcept;
    bool role_ready(HudResourceRole role) const noexcept;
    void initialize_overlay(std::uint32_t variant) noexcept;
    void retire_overlay() noexcept;
    void clear_contract() noexcept;

    bool begin_activation() noexcept;
    bool commit_activation(
        const std::array<std::uint32_t, 4>& final_lane_counts) noexcept;
    void abort_activation() noexcept;
    void retire_screen() noexcept;

    bool begin_lane(HudVisualLane lane) noexcept;
    bool observe_icon(const char* canonical_name,
                      const HudIconPlacement& placement,
                      bool* native_source_found = nullptr);
    void reject_icon_attempt() noexcept;
    bool attach_figure(const char* base_name, bool visible = true);
    void finish_lane(bool success) noexcept;
    void invalidate_open_lane() noexcept;
    // A published lane icon repainted later by its semantic owner (the same
    // producer, same atlas cell) outside a lane rebuild: replace that slot's
    // texture. False when no published slot occupies that cell.
    bool repaint_icon(HudVisualLane lane, const char* canonical_name,
                      const HudIconPlacement& placement,
                      HudPublicationFailure* failure = nullptr);

    const HudPublishedVisual* find(HudVisualLane lane,
                                   std::uint32_t ordinal) const noexcept;
    HudPublicationFailure classify_missing(
        HudVisualLane lane, std::uint32_t ordinal,
        std::uint64_t expected_screen_generation,
        bool current_receiver) const noexcept;
    // Read-only selected texture for non-lane placed figures (portraits).
    // Uses the same manager generations, cache, 1555 conversion and missing
    // source output as observe_icon. No physical atlas or lock is required.
    bool resolve_texture(const char* name, const HudIconPlacement& placement,
                         HudPublishedVisual& output, bool& source_found);
    bool claims_lane(HudVisualLane lane) const noexcept;
    bool capturing_lane() const noexcept;
    HudVisualLane capturing_lane_id() const noexcept;
    bool owns_active_screen() const noexcept;
    std::uint64_t active_screen_generation() const noexcept;
    std::size_t cached_asset_count() const noexcept;
    std::size_t cached_asset_bytes() const noexcept;

private:
    struct AssetKey {
        HudResourceRole role = HudResourceRole::texture;
        std::uint64_t role_generation = 0;
        std::uint64_t overlay_generation = 0;
        std::uint32_t source_width = 0;
        std::uint32_t source_height = 0;
        std::uint32_t variant = 0;
        std::uint32_t copy_width = 0;
        std::uint32_t copy_height = 0;
        std::uint32_t source_x = 0;
        std::uint32_t source_y = 0;
        bool native_defined_output = false;
        std::string canonical_name;

        bool operator<(const AssetKey& other) const noexcept;
    };
    struct CachedAsset {
        std::shared_ptr<const FigureCpuAsset> figure;
        std::shared_ptr<const TextureCpuAsset> texture;
        std::uint64_t identity = 0;
        std::uint64_t last_use = 0;
        std::size_t owned_bytes = 0;
        bool resolved = false;
        bool native_source_found = true;
        // The archive member a decoded texture came from (container file
        // key, absolute offset, bytes); file key 0: no stable source.
        std::array<std::uint64_t, 3> source{};
    };
    struct PendingIcon {
        std::shared_ptr<const TextureCpuAsset> texture;
        std::uint64_t identity = 0;
        HudIconPlacement placement{};
        bool resolved = false;
    };
    struct LanePublication {
        std::vector<HudPublishedVisual> slots;
        HudPublicationFailure first_failure{};
        bool claimed = false;
        bool valid = false;
        bool refreshed = false;
    };

    struct AssetLoadFailure {
        HudPublicationFailureStage stage = HudPublicationFailureStage::none;
        ResourceFailureCode resource_failure = ResourceFailureCode::none;
    };

    bool load(HudResourceRole role, const std::string& canonical_name,
              const HudIconPlacement* placement, CachedAsset& output,
              AssetLoadFailure& failure);
    void record_open_failure(HudPublicationFailureStage stage,
                             std::uint32_t ordinal,
                             const char* logical_member,
                             ResourceFailureCode resource_failure) noexcept;
    bool make_cache_room(std::size_t bytes) noexcept;
    void clear_role_assets(HudResourceRole role) noexcept;
    void clear_publications() noexcept;
    void invalidate_publications() noexcept;
    void replace_lane(LanePublication& target, bool success) noexcept;
    static std::size_t lane_index(HudVisualLane lane) noexcept;

    void* resolve_context_ = nullptr;
    HudResolveResourceFn resolve_ = nullptr;
    std::uint64_t texture_generation_ = 0;
    std::uint64_t figure_generation_ = 0;
    std::uint64_t overlay_generation_ = 0;
    std::uint64_t next_screen_generation_ = 1;
    std::uint64_t active_screen_generation_ = 0;
    std::uint64_t provisional_screen_generation_ = 0;
    std::uint64_t rebuild_serial_ = 0;
    std::uint64_t use_serial_ = 0;
    std::uint64_t staged_texture_generation_ = 0;
    std::uint64_t staged_figure_generation_ = 0;
    std::uint64_t staged_overlay_generation_ = 0;
    std::uint32_t overlay_variant_ = 0;
    bool texture_ready_ = false;
    bool figure_ready_ = false;
    bool overlay_ready_ = false;
    bool activation_staging_ = false;
    bool activation_failed_ = false;
    bool lane_open_ = false;
    bool lane_failed_ = false;
    HudVisualLane lane_ = HudVisualLane::none;
    std::vector<PendingIcon> pending_icons_;
    std::vector<HudPublishedVisual> pending_slots_;
    HudPublicationFailure open_failure_{};
    std::array<LanePublication, 4> active_lanes_{};
    std::array<LanePublication, 4> provisional_lanes_{};
    std::map<AssetKey, CachedAsset> assets_;
    std::size_t cached_asset_bytes_ = 0;
    // Decoded icon textures whose cache entries a generation change dropped
    // (a resource archive added or closed, an overlay change: the camp screen
    // does both). A later load that resolves to the same archive member
    // takes the decoded texture instead of parsing and converting it again.
    std::map<std::array<std::uint64_t, 3>, std::shared_ptr<const TextureCpuAsset>> retired_textures_;
    std::size_t retired_texture_bytes_ = 0;
    void retire_texture_asset(const CachedAsset& value) noexcept;
};

} // namespace owned
