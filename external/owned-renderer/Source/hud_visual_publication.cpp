#include "hud_visual_publication.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <tuple>

namespace owned {
namespace {

constexpr std::size_t kMaximumCachedAssets = 512;
constexpr std::size_t kMaximumCachedBytes = 64u * 1024u * 1024u;
constexpr std::uint32_t kPnt3 = 0x33544e50u;
constexpr std::uint32_t kArgb8888 = 0x00008888u;

std::uint64_t hash_bytes(std::uint64_t value, const void* bytes,
                         std::size_t size) noexcept {
    const auto* data = static_cast<const std::uint8_t*>(bytes);
    for (std::size_t index = 0; index < size; ++index) {
        value ^= data[index];
        value *= 1099511628211ull;
    }
    return value;
}

std::uint64_t asset_identity(HudResourceRole role, std::uint64_t role_generation,
                             std::uint64_t overlay_generation,
                             std::uint32_t source_width,
                             std::uint32_t source_height,
                             std::uint32_t variant,
                             std::uint32_t copy_width,
                             std::uint32_t copy_height,
                             std::uint32_t source_x,
                             std::uint32_t source_y,
                             bool native_defined_output,
                             const std::string& canonical_name) noexcept {
    std::uint64_t result = 1469598103934665603ull;
    result = hash_bytes(result, &role, sizeof(role));
    result = hash_bytes(result, &role_generation, sizeof(role_generation));
    result = hash_bytes(result, &overlay_generation, sizeof(overlay_generation));
    result = hash_bytes(result, &source_width, sizeof(source_width));
    result = hash_bytes(result, &source_height, sizeof(source_height));
    result = hash_bytes(result, &variant, sizeof(variant));
    result = hash_bytes(result, &copy_width, sizeof(copy_width));
    result = hash_bytes(result, &copy_height, sizeof(copy_height));
    result = hash_bytes(result, &source_x, sizeof(source_x));
    result = hash_bytes(result, &source_y, sizeof(source_y));
    result = hash_bytes(result, &native_defined_output,
                        sizeof(native_defined_output));
    result = hash_bytes(result, canonical_name.data(), canonical_name.size());
    return result ? result : 1u;
}

std::size_t figure_bytes(const FigureCpuAsset& value) noexcept {
    return value.positions.size() * sizeof(NativePackedGroup) +
        value.normals.size() * sizeof(NativePackedGroup) +
        value.texture_coordinates.size() * sizeof(NativeTextureCoordinate) +
        value.indices.size() * sizeof(std::uint16_t) +
        value.components.size() * sizeof(NativeVertexComponent) +
        value.morph_components.size() * sizeof(FigureMorphComponent) +
        value.bounds.size() * sizeof(FigureBounds) + value.user_data.size();
}

std::size_t texture_bytes(const TextureCpuAsset& value) noexcept {
    return value.source ? value.source->size() : 0u;
}

bool convert_hud_icon_to_argb1555(const TextureCpuAsset& source,
                                  TextureCpuAsset& destination) noexcept {
    destination = {};
    if (source.format != MmpFormat::argb8888 || source.bit_count != 32u ||
        source.mips.empty() || !source.source || !source.width ||
        !source.height || source.mips[0].width != source.width ||
        source.mips[0].height != source.height)
        return false;
    const std::uint64_t pixels = static_cast<std::uint64_t>(source.width) *
        source.height;
    const std::uint64_t source_bytes = pixels * 4u;
    const std::uint64_t output_bytes = pixels * 2u;
    if ((pixels & 3u) != 0u || source_bytes != source.mips[0].bytes ||
        output_bytes > (std::numeric_limits<std::uint32_t>::max)() ||
        output_bytes > (std::numeric_limits<std::size_t>::max)())
        return false;
    std::uint64_t source_offset = 0u;
    if (source.format_signature == kPnt3) {
        if (source.mips.size() != 1u || source.texel_offset != 0u ||
            source.mips[0].byte_offset != 0u ||
            source_bytes != source.source->size())
            return false;
    } else if (source.format_signature == kArgb8888) {
        // PutOneLayer(false) consumes the base-level DWORD stream beginning at
        // the exact 76-byte MMP header boundary. It is not a DXT decode path.
        source_offset = source.texel_offset;
        if (source_offset != 76u ||
            source.mips[0].byte_offset != source_offset ||
            source_offset > source.source->size() ||
            source_bytes > source.source->size() - source_offset)
            return false;
    } else {
        return false;
    }
    try {
        auto pixels1555 = std::make_shared<std::vector<std::uint8_t>>(
            static_cast<std::size_t>(output_bytes));
        for (std::uint64_t index = 0; index < pixels; ++index) {
            std::uint32_t pixel = 0;
            std::memcpy(&pixel,
                        source.source->data() + source_offset + index * 4u,
                        sizeof(pixel));
            const std::uint16_t converted = static_cast<std::uint16_t>(
                ((pixel >> 16u) & 0x8000u) |
                ((pixel >> 9u) & 0x7c00u) |
                ((pixel >> 6u) & 0x03e0u) |
                ((pixel >> 3u) & 0x001fu));
            std::memcpy(pixels1555->data() + index * 2u, &converted,
                        sizeof(converted));
        }
        destination.identity = source.identity;
        destination.width = source.width;
        destination.height = source.height;
        destination.bit_count = 16u;
        destination.format_signature = source.format_signature;
        destination.format = MmpFormat::argb1555;
        destination.masks = {0x7c00u, 0x03e0u, 0x001fu, 0x8000u};
        destination.shifts = {10u, 5u, 0u, 15u};
        destination.channel_bits = {5u, 5u, 5u, 1u};
        destination.mips.push_back({source.width, source.height, 0u,
                                    static_cast<std::uint32_t>(output_bytes)});
        destination.source = std::move(pixels1555);
        destination.texel_offset = 0u;
        return true;
    } catch (...) {
        destination = {};
        return false;
    }
}

bool make_native_white_icon(const HudIconPlacement& placement,
                            TextureCpuAsset& destination) noexcept {
    destination = {};
    const std::uint32_t width = placement.source_width;
    const std::uint32_t height = placement.source_height;
    const std::uint64_t source_right =
        static_cast<std::uint64_t>(placement.source_x) + placement.copy_width;
    const std::uint64_t source_bottom =
        static_cast<std::uint64_t>(placement.source_y) + placement.copy_height;
    const std::uint64_t pixels = static_cast<std::uint64_t>(width) * height;
    const std::uint64_t output_bytes = pixels * sizeof(std::uint16_t);
    if (!width || !height || !placement.copy_width || !placement.copy_height ||
        source_right > width || source_bottom > height ||
        output_bytes > (std::numeric_limits<std::uint32_t>::max)() ||
        output_bytes > (std::numeric_limits<std::size_t>::max)() ||
        output_bytes > kMaximumCachedBytes)
        return false;
    try {
        auto white = std::make_shared<std::vector<std::uint8_t>>(
            static_cast<std::size_t>(output_bytes),
            static_cast<std::uint8_t>(0u));
        for (std::uint32_t row = 0; row < placement.copy_height; ++row) {
            const std::size_t offset =
                (static_cast<std::size_t>(placement.source_y + row) * width +
                 placement.source_x) * sizeof(std::uint16_t);
            std::fill_n(white->data() + offset,
                        static_cast<std::size_t>(placement.copy_width) *
                            sizeof(std::uint16_t),
                        static_cast<std::uint8_t>(0xffu));
        }
        destination.width = width;
        destination.height = height;
        destination.bit_count = 16u;
        destination.format_signature = kHudNativeWhiteFormatSignature;
        destination.format = MmpFormat::argb1555;
        destination.masks = {0x7c00u, 0x03e0u, 0x001fu, 0x8000u};
        destination.shifts = {10u, 5u, 0u, 15u};
        destination.channel_bits = {5u, 5u, 5u, 1u};
        destination.mips.push_back({width, height, 0u,
                                    static_cast<std::uint32_t>(output_bytes)});
        destination.source = std::move(white);
        destination.texel_offset = 0u;
        return true;
    } catch (...) {
        destination = {};
        return false;
    }
}

HudPublicationFailure make_publication_failure(
    HudPublicationFailureStage stage, HudVisualLane lane,
    std::uint32_t ordinal, const char* logical_member,
    ResourceFailureCode resource_failure) noexcept {
    HudPublicationFailure failure{};
    failure.stage = stage;
    failure.lane = lane;
    failure.ordinal = ordinal;
    failure.resource_failure = resource_failure;
    if (logical_member) {
        while (failure.logical_member_bytes < kHudFailureMemberBytes &&
               logical_member[failure.logical_member_bytes]) {
            failure.logical_member[failure.logical_member_bytes] =
                logical_member[failure.logical_member_bytes];
            ++failure.logical_member_bytes;
        }
    }
    return failure;
}

} // namespace

const char* hud_publication_failure_stage_name(
    HudPublicationFailureStage stage) noexcept {
    switch (stage) {
    case HudPublicationFailureStage::none: return "none";
    case HudPublicationFailureStage::resolver_unavailable:
        return "resolverunavailable";
    case HudPublicationFailureStage::resolver: return "resolver";
    case HudPublicationFailureStage::size: return "size";
    case HudPublicationFailureStage::copy: return "copy";
    case HudPublicationFailureStage::texture_parse: return "textureparse";
    case HudPublicationFailureStage::texture_1555: return "1555";
    case HudPublicationFailureStage::dimension: return "dim";
    case HudPublicationFailureStage::figure_parse: return "figureparse";
    case HudPublicationFailureStage::shape_count: return "shapecount";
    case HudPublicationFailureStage::budget: return "budget";
    case HudPublicationFailureStage::lane_invalid: return "laneinvalid";
    case HudPublicationFailureStage::generation: return "generation";
    case HudPublicationFailureStage::current_receiver: return "currentreceiver";
    case HudPublicationFailureStage::ordinal: return "ordinal";
    }
    return "unknown";
}

bool hud_interface_figure_is_supported(
    const FigureCpuAsset& figure) noexcept {
    constexpr std::uint32_t kFigureSignature1 = 0x31474946u;
    const bool exact_pair =
        (figure.signature == kFigureSignature1 && figure.shape_count == 1u) ||
        (figure.signature == kFigureSignature8 && figure.shape_count == 8u);
    if (!exact_pair || !figure.position_groups ||
        figure.bounds.size() != figure.shape_count)
        return false;
    if (figure.position_groups >
        (std::numeric_limits<std::size_t>::max)() / figure.shape_count)
        return false;
    return figure.positions.size() ==
        static_cast<std::size_t>(figure.position_groups) * figure.shape_count;
}

HudVisualPublication::HudVisualPublication(void* resolve_context,
                                           HudResolveResourceFn resolve)
    : resolve_context_(resolve_context), resolve_(resolve) {}

void HudVisualPublication::set_resolver(void* context,
                                        HudResolveResourceFn resolve) noexcept {
    resolve_context_ = context;
    resolve_ = resolve;
}

bool HudVisualPublication::AssetKey::operator<(
    const AssetKey& other) const noexcept {
    return std::tie(role, role_generation, overlay_generation, source_width,
                    source_height, variant, copy_width, copy_height, source_x,
                    source_y, native_defined_output, canonical_name) <
        std::tie(other.role, other.role_generation, other.overlay_generation,
                 other.source_width, other.source_height, other.variant,
                 other.copy_width, other.copy_height, other.source_x,
                 other.source_y, other.native_defined_output,
                 other.canonical_name);
}

std::size_t HudVisualPublication::lane_index(HudVisualLane lane) noexcept {
    const auto value = static_cast<std::uint8_t>(lane);
    return value >= static_cast<std::uint8_t>(HudVisualLane::weapon) &&
            value <= static_cast<std::uint8_t>(HudVisualLane::skill)
        ? static_cast<std::size_t>(value - 1u) : 4u;
}

std::uint64_t HudVisualPublication::role_generation(
    HudResourceRole role) const noexcept {
    return role == HudResourceRole::texture ? texture_generation_ :
                                              figure_generation_;
}

bool HudVisualPublication::role_ready(HudResourceRole role) const noexcept {
    return role == HudResourceRole::texture ? texture_ready_ : figure_ready_;
}

void HudVisualPublication::clear_publications() noexcept {
    for (auto& lane : active_lanes_) lane = {};
    for (auto& lane : provisional_lanes_) lane = {};
    pending_icons_.clear();
    pending_slots_.clear();
    open_failure_ = {};
    lane_ = HudVisualLane::none;
    lane_open_ = false;
    lane_failed_ = false;
    active_screen_generation_ = 0;
    provisional_screen_generation_ = 0;
    activation_staging_ = false;
    activation_failed_ = false;
}

void HudVisualPublication::clear_role_assets(HudResourceRole role) noexcept {
    for (auto item = assets_.begin(); item != assets_.end();) {
        if (item->first.role == role) {
            cached_asset_bytes_ -= item->second.owned_bytes;
            item = assets_.erase(item);
        } else {
            ++item;
        }
    }
}

void HudVisualPublication::invalidate_publications() noexcept {
    if (activation_staging_) abort_activation();
    for (std::size_t index = 0; index < active_lanes_.size(); ++index) {
        auto& lane = active_lanes_[index];
        const HudPublicationFailure first_failure = lane.first_failure;
        lane.slots.clear();
        lane.claimed = active_screen_generation_ != 0;
        lane.valid = false;
        lane.refreshed = false;
        lane.first_failure = first_failure;
        if (lane.claimed && !lane.first_failure)
            lane.first_failure = make_publication_failure(
                HudPublicationFailureStage::generation,
                static_cast<HudVisualLane>(index + 1u), 0u, nullptr,
                ResourceFailureCode::none);
    }
    pending_icons_.clear();
    pending_slots_.clear();
    open_failure_ = {};
    lane_ = HudVisualLane::none;
    lane_open_ = false;
    lane_failed_ = false;
}

void HudVisualPublication::begin_role_recreation(HudResourceRole role) noexcept {
    invalidate_role(role);
    if (role == HudResourceRole::texture) texture_ready_ = false;
    else figure_ready_ = false;
}

void HudVisualPublication::finish_role_recreation(HudResourceRole role,
                                                   bool success) noexcept {
    if (role == HudResourceRole::texture) texture_ready_ = success;
    else figure_ready_ = success;
}

void HudVisualPublication::invalidate_role(HudResourceRole role) noexcept {
    std::uint64_t& generation = role == HudResourceRole::texture ?
        texture_generation_ : figure_generation_;
    if (++generation == 0u) ++generation;
    invalidate_publications();
    clear_role_assets(role);
}

void HudVisualPublication::initialize_overlay(std::uint32_t variant) noexcept {
    if (overlay_ready_ && overlay_variant_ == variant) return;
    overlay_variant_ = variant;
    overlay_ready_ = true;
    if (++overlay_generation_ == 0u) ++overlay_generation_;
    invalidate_publications();
    assets_.clear();
    cached_asset_bytes_ = 0;
}

void HudVisualPublication::retire_overlay() noexcept {
    overlay_ready_ = false;
    if (++overlay_generation_ == 0u) ++overlay_generation_;
    clear_publications();
    assets_.clear();
    cached_asset_bytes_ = 0;
}

void HudVisualPublication::clear_contract() noexcept {
    clear_publications();
    assets_.clear();
    cached_asset_bytes_ = 0;
    if (++texture_generation_ == 0u) ++texture_generation_;
    if (++figure_generation_ == 0u) ++figure_generation_;
    if (++overlay_generation_ == 0u) ++overlay_generation_;
    texture_ready_ = false;
    figure_ready_ = false;
    overlay_ready_ = false;
    overlay_variant_ = 0;
}

bool HudVisualPublication::begin_activation() noexcept {
    if (activation_staging_) return false;
    retire_screen();
    if (!texture_ready_ || !figure_ready_ || !overlay_ready_) return false;
    activation_staging_ = true;
    staged_texture_generation_ = texture_generation_;
    staged_figure_generation_ = figure_generation_;
    staged_overlay_generation_ = overlay_generation_;
    provisional_screen_generation_ = next_screen_generation_++;
    if (!provisional_screen_generation_)
        provisional_screen_generation_ = next_screen_generation_++;
    return provisional_screen_generation_ != 0;
}

bool HudVisualPublication::commit_activation(
    const std::array<std::uint32_t, 4>& final_lane_counts) noexcept {
    if (!activation_staging_ || lane_open_ || activation_failed_ ||
        !provisional_screen_generation_ || !texture_ready_ || !figure_ready_ ||
        !overlay_ready_ || staged_texture_generation_ != texture_generation_ ||
        staged_figure_generation_ != figure_generation_ ||
        staged_overlay_generation_ != overlay_generation_)
        return false;
    for (std::size_t index = 0; index < provisional_lanes_.size(); ++index) {
        const auto& lane = provisional_lanes_[index];
        if (lane.refreshed) {
            if (!lane.claimed || !lane.valid) return false;
        } else if (lane.claimed || lane.valid || final_lane_counts[index] != 0u) {
            // Only a never-entered lane can inherit the proved freshly
            // constructed empty state, and its exact carrier count must still
            // be zero after the optional activation tail. A partial, failed,
            // or externally populated rebuild may not become empty success.
            return false;
        }
    }
    for (auto& lane : provisional_lanes_) {
        if (!lane.refreshed) {
            lane.claimed = true;
            lane.valid = true;
            lane.refreshed = true;
        }
    }
    active_lanes_ = std::move(provisional_lanes_);
    for (auto& lane : provisional_lanes_) lane = {};
    active_screen_generation_ = provisional_screen_generation_;
    provisional_screen_generation_ = 0;
    activation_staging_ = false;
    return true;
}

void HudVisualPublication::abort_activation() noexcept {
    for (auto& lane : provisional_lanes_) lane = {};
    pending_icons_.clear();
    pending_slots_.clear();
    open_failure_ = {};
    lane_ = HudVisualLane::none;
    lane_open_ = false;
    lane_failed_ = false;
    provisional_screen_generation_ = 0;
    activation_staging_ = false;
    activation_failed_ = false;
}

void HudVisualPublication::retire_screen() noexcept {
    abort_activation();
    for (auto& lane : active_lanes_) lane = {};
    active_screen_generation_ = 0;
    // Screen-owned slot references and their manager-selected immutable assets
    // share one lifetime. A later activation resolves its manager generations
    // afresh rather than retaining assets across a reset/recreated screen.
    assets_.clear();
    cached_asset_bytes_ = 0;
}

bool HudVisualPublication::begin_lane(HudVisualLane lane) noexcept {
    if (lane_open_ || lane_index(lane) >= active_lanes_.size() ||
        (!activation_staging_ && !active_screen_generation_))
        return false;
    auto& target = activation_staging_ ?
        provisional_lanes_[lane_index(lane)] :
        active_lanes_[lane_index(lane)];
    // Remove the prior visual pairing before the semantic owner starts
    // RemoveAll/Add*. A reentrant Draw during that call must see an owned
    // omission, never old descriptors beside newly rebuilt empty carriers.
    target.slots.clear();
    target.first_failure = {};
    target.claimed = true;
    target.valid = false;
    target.refreshed = false;
    lane_open_ = true;
    lane_failed_ = false;
    lane_ = lane;
    pending_icons_.clear();
    pending_slots_.clear();
    open_failure_ = {};
    if (++rebuild_serial_ == 0u) ++rebuild_serial_;
    return true;
}

bool HudVisualPublication::resolve_texture(const char* name,
    const HudIconPlacement& placement, HudPublishedVisual& output, bool& source_found) {
    output = {}; source_found = false;
    if (!owns_active_screen() || !name) return false;
    CachedAsset asset; AssetLoadFailure failure;
    if (!load(HudResourceRole::texture, name, &placement, asset, failure)) return false;
    output.screen_generation = active_screen_generation_;
    output.texture_generation = texture_generation_;
    output.overlay_generation = overlay_generation_;
    output.texture = asset.texture; output.texture_identity = asset.identity;
    output.placement = placement; output.complete = true;
    source_found = asset.native_source_found;
    return true;
}

bool HudVisualPublication::load(HudResourceRole role,
                                const std::string& canonical_name,
                                const HudIconPlacement* placement,
                                CachedAsset& output,
                                AssetLoadFailure& failure) {
    output = {};
    failure = {};
    if (!resolve_ || !role_ready(role)) {
        failure.stage = HudPublicationFailureStage::resolver_unavailable;
        failure.resource_failure = ResourceFailureCode::not_found;
        return false;
    }
    AssetKey key{};
    AssetKey native_defined_key{};
    try {
        key.role = role;
        key.role_generation = role_generation(role);
        key.overlay_generation = overlay_generation_;
        if (role == HudResourceRole::texture && placement) {
            key.source_width = placement->source_width;
            key.source_height = placement->source_height;
            key.variant = placement->variant;
        }
        key.canonical_name = canonical_name;
        auto found = assets_.find(key);
        if (found != assets_.end()) {
            found->second.last_use = ++use_serial_;
            output = found->second;
            return output.resolved;
        }
        if (role == HudResourceRole::texture && placement) {
            native_defined_key = key;
            native_defined_key.copy_width = placement->copy_width;
            native_defined_key.copy_height = placement->copy_height;
            native_defined_key.source_x = placement->source_x;
            native_defined_key.source_y = placement->source_y;
            native_defined_key.native_defined_output = true;
            found = assets_.find(native_defined_key);
            if (found != assets_.end()) {
                found->second.last_use = ++use_serial_;
                output = found->second;
                return output.resolved;
            }
        }
    } catch (...) {
        failure.stage = HudPublicationFailureStage::budget;
        return false;
    }

    CachedAsset value{};
    value.identity = asset_identity(
        role, key.role_generation, key.overlay_generation, key.source_width,
        key.source_height, key.variant, key.copy_width, key.copy_height,
        key.source_x, key.source_y, key.native_defined_output,
        canonical_name);
    value.last_use = ++use_serial_;
    ResourceBlob blob{};
    HudPublicationFailureStage resolve_failure =
        HudPublicationFailureStage::none;
    ResourceFailureCode resolve_failure_code = ResourceFailureCode::none;
    const bool resolved = resolve_(resolve_context_, role,
                                   canonical_name.c_str(), blob,
                                   resolve_failure, resolve_failure_code);
    const bool source_found = resolved && blob.bytes;
    if (!source_found) {
        const bool native_defined_missing = role == HudResourceRole::texture &&
            placement && !resolved && !blob.bytes &&
            resolve_failure == HudPublicationFailureStage::resolver &&
            resolve_failure_code == ResourceFailureCode::not_found;
        if (native_defined_missing) {
            try {
                key = std::move(native_defined_key);
                value.identity = asset_identity(
                    role, key.role_generation, key.overlay_generation,
                    key.source_width, key.source_height, key.variant,
                    key.copy_width, key.copy_height, key.source_x, key.source_y,
                    key.native_defined_output, canonical_name);
                auto asset = std::make_shared<TextureCpuAsset>();
                if (!make_native_white_icon(*placement, *asset)) {
                    failure.stage = HudPublicationFailureStage::budget;
                    return false;
                }
                value.owned_bytes = texture_bytes(*asset);
                value.texture = std::move(asset);
                value.resolved = true;
                value.native_source_found = false;
            } catch (...) {
                failure.stage = HudPublicationFailureStage::budget;
                return false;
            }
        } else {
            failure.stage = resolve_failure != HudPublicationFailureStage::none ?
                resolve_failure : HudPublicationFailureStage::resolver;
            failure.resource_failure =
                resolve_failure_code != ResourceFailureCode::none ?
                    resolve_failure_code : ResourceFailureCode::not_found;
            return false;
        }
    }
    try {
        ResourceFailure parse_failure{};
        if (source_found && role == HudResourceRole::texture) {
            TextureCpuAsset decoded{};
            auto asset = std::make_shared<TextureCpuAsset>();
            if (!parse_texture_asset(blob, decoded, parse_failure)) {
                failure.stage = HudPublicationFailureStage::texture_parse;
                failure.resource_failure = parse_failure.code;
                return false;
            }
            if (!convert_hud_icon_to_argb1555(decoded, *asset)) {
                failure.stage = HudPublicationFailureStage::texture_1555;
                failure.resource_failure = ResourceFailureCode::parse;
                return false;
            }
            value.owned_bytes = texture_bytes(*asset);
            value.texture = std::move(asset);
            value.resolved = true;
        } else if (source_found) {
            auto asset = std::make_shared<FigureCpuAsset>();
            if (!parse_figure_asset(blob, *asset, parse_failure)) {
                failure.stage = HudPublicationFailureStage::figure_parse;
                failure.resource_failure = parse_failure.code;
                return false;
            }
            if (!hud_interface_figure_is_supported(*asset)) {
                failure.stage = HudPublicationFailureStage::shape_count;
                failure.resource_failure = ResourceFailureCode::parse;
                return false;
            }
            value.owned_bytes = figure_bytes(*asset);
            value.figure = std::move(asset);
            value.resolved = true;
        }
    } catch (...) {
        failure.stage = HudPublicationFailureStage::budget;
        return false;
    }
    if (!value.resolved || value.owned_bytes > kMaximumCachedBytes) {
        failure.stage = HudPublicationFailureStage::budget;
        return false;
    }
    if (!make_cache_room(value.owned_bytes)) {
        failure.stage = HudPublicationFailureStage::budget;
        return false;
    }
    try {
        const AssetKey lookup = key;
        auto inserted = assets_.emplace(std::move(key), std::move(value));
        cached_asset_bytes_ += inserted.first->second.owned_bytes;
        auto found = assets_.find(lookup);
        if (found == assets_.end()) return false;
        output = found->second;
        return output.resolved;
    } catch (...) {
        failure.stage = HudPublicationFailureStage::budget;
        return false;
    }
}

bool HudVisualPublication::observe_icon(
    const char* canonical_name, const HudIconPlacement& placement,
    bool* native_source_found) {
    if (native_source_found) *native_source_found = true;
    if (!lane_open_) return false;
    const std::uint32_t ordinal = static_cast<std::uint32_t>(
        pending_icons_.size());
    PendingIcon pending{};
    pending.placement = placement;
    const std::uint64_t source_right =
        static_cast<std::uint64_t>(placement.source_x) + placement.copy_width;
    const std::uint64_t source_bottom =
        static_cast<std::uint64_t>(placement.source_y) + placement.copy_height;
    const std::uint64_t destination_right =
        static_cast<std::uint64_t>(placement.destination_x) + placement.copy_width;
    const std::uint64_t destination_bottom =
        static_cast<std::uint64_t>(placement.destination_y) + placement.copy_height;
    const bool bounds = canonical_name && *canonical_name &&
        placement.atlas_width && placement.source_width &&
        placement.source_height && placement.copy_width &&
        placement.copy_height && source_right <= placement.source_width &&
        source_bottom <= placement.source_height &&
        destination_right <= placement.atlas_width &&
        destination_bottom <= placement.atlas_width;
    if (bounds) {
        CachedAsset asset{};
        AssetLoadFailure load_failure{};
        if (load(HudResourceRole::texture, canonical_name, &placement, asset,
                 load_failure)) {
            if (native_source_found)
                *native_source_found = asset.native_source_found;
            pending.texture = asset.texture;
            pending.identity = asset.identity;
            pending.resolved = asset.texture &&
                asset.texture->width == placement.source_width &&
                asset.texture->height == placement.source_height;
            if (!pending.resolved)
                record_open_failure(HudPublicationFailureStage::dimension,
                                    ordinal, canonical_name,
                                    ResourceFailureCode::parse);
        } else {
            record_open_failure(load_failure.stage, ordinal, canonical_name,
                                load_failure.resource_failure);
        }
    } else {
        record_open_failure(HudPublicationFailureStage::dimension, ordinal,
                            canonical_name, ResourceFailureCode::parse);
    }
    try {
        pending_icons_.push_back(std::move(pending));
    } catch (...) {
        lane_failed_ = true;
        record_open_failure(HudPublicationFailureStage::budget, ordinal,
                            canonical_name, ResourceFailureCode::none);
        return false;
    }
    if (!pending_icons_.back().resolved) lane_failed_ = true;
    return pending_icons_.back().resolved;
}

void HudVisualPublication::reject_icon_attempt() noexcept {
    if (!lane_open_) return;
    lane_failed_ = true;
    record_open_failure(HudPublicationFailureStage::lane_invalid,
                        static_cast<std::uint32_t>(pending_icons_.size()),
                        nullptr, ResourceFailureCode::none);
    try {
        pending_icons_.push_back({});
    } catch (...) {
    }
}

bool HudVisualPublication::attach_figure(const char* base_name,
                                         bool visible) {
    if (!lane_open_) return false;
    HudPublishedVisual slot{};
    slot.lane = lane_;
    slot.screen_generation = activation_staging_ ?
        provisional_screen_generation_ : active_screen_generation_;
    slot.rebuild_serial = rebuild_serial_;
    slot.ordinal = static_cast<std::uint32_t>(pending_slots_.size());
    slot.figure_generation = figure_generation_;
    slot.texture_generation = texture_generation_;
    slot.overlay_generation = overlay_generation_;
    slot.visible = visible;
    if (slot.ordinal < pending_icons_.size()) {
        const auto& icon = pending_icons_[slot.ordinal];
        slot.texture = icon.texture;
        slot.texture_identity = icon.identity;
        slot.placement = icon.placement;
        slot.complete = icon.resolved;
    }
    if (base_name && *base_name) {
        std::string name;
        try {
            name = base_name;
            name += ".fig";
        } catch (...) {
            lane_failed_ = true;
            record_open_failure(HudPublicationFailureStage::budget,
                                slot.ordinal, base_name,
                                ResourceFailureCode::none);
            return false;
        }
        CachedAsset asset{};
        AssetLoadFailure load_failure{};
        if (load(HudResourceRole::figure, name, nullptr, asset,
                 load_failure)) {
            slot.figure = asset.figure;
            slot.geometry_identity = asset.identity;
            slot.complete = slot.complete && asset.figure;
        } else {
            slot.complete = false;
            record_open_failure(load_failure.stage, slot.ordinal,
                                name.c_str(), load_failure.resource_failure);
        }
    } else {
        slot.complete = false;
        record_open_failure(HudPublicationFailureStage::figure_parse,
                            slot.ordinal, base_name,
                            ResourceFailureCode::invalid_name);
    }
    try {
        pending_slots_.push_back(std::move(slot));
    } catch (...) {
        lane_failed_ = true;
        record_open_failure(HudPublicationFailureStage::budget, slot.ordinal,
                            base_name, ResourceFailureCode::none);
        return false;
    }
    if (!pending_slots_.back().complete) lane_failed_ = true;
    return pending_slots_.back().complete;
}

void HudVisualPublication::record_open_failure(
    HudPublicationFailureStage stage, std::uint32_t ordinal,
    const char* logical_member,
    ResourceFailureCode resource_failure) noexcept {
    if (open_failure_ || stage == HudPublicationFailureStage::none) return;
    open_failure_ = make_publication_failure(
        stage, lane_, ordinal, logical_member, resource_failure);
}

void HudVisualPublication::replace_lane(LanePublication& target,
                                        bool success) noexcept {
    target = {};
    target.claimed = true;
    target.refreshed = true;
    success = success && !lane_failed_ &&
        pending_icons_.size() == pending_slots_.size();
    if (!success) {
        if (!open_failure_)
            record_open_failure(HudPublicationFailureStage::lane_invalid,
                                static_cast<std::uint32_t>(
                                    pending_slots_.size()),
                                nullptr, ResourceFailureCode::none);
        target.first_failure = open_failure_;
        return;
    }
    try {
        target.slots = std::move(pending_slots_);
        target.valid = std::all_of(
            target.slots.begin(), target.slots.end(),
            [](const HudPublishedVisual& slot) { return slot.complete; });
    } catch (...) {
        target = {};
        target.claimed = true;
        target.refreshed = true;
        target.first_failure = open_failure_ ? open_failure_ :
            make_publication_failure(HudPublicationFailureStage::budget,
                                     lane_, 0u, nullptr,
                                     ResourceFailureCode::none);
    }
}

void HudVisualPublication::finish_lane(bool success) noexcept {
    if (!lane_open_) return;
    const auto index = lane_index(lane_);
    if (index < active_lanes_.size()) {
        auto& target = activation_staging_ ? provisional_lanes_[index] :
                                             active_lanes_[index];
        replace_lane(target, success);
        if (activation_staging_ && !target.valid) activation_failed_ = true;
    }
    pending_icons_.clear();
    pending_slots_.clear();
    open_failure_ = {};
    lane_ = HudVisualLane::none;
    lane_open_ = false;
    lane_failed_ = false;
}

bool HudVisualPublication::repaint_icon(HudVisualLane lane,
                                       const char* canonical_name,
                                       const HudIconPlacement& placement,
                                       HudPublicationFailure* output_failure) {
    if (output_failure) *output_failure = {};
    const auto reject = [&](HudPublicationFailureStage stage,
                            ResourceFailureCode code, std::uint32_t ordinal = 0u) {
        if (output_failure) *output_failure = make_publication_failure(
            stage, lane, ordinal, canonical_name, code);
        return false;
    };
    const auto index = lane_index(lane);
    if (!canonical_name || !*canonical_name)
        return reject(HudPublicationFailureStage::resolver, ResourceFailureCode::invalid_name);
    if (!active_screen_generation_)
        return reject(HudPublicationFailureStage::generation, ResourceFailureCode::none);
    if (lane_open_ || index >= active_lanes_.size())
        return reject(HudPublicationFailureStage::lane_invalid, ResourceFailureCode::none);
    auto& publication = active_lanes_[index];
    if (!publication.claimed || !publication.valid) {
        if (output_failure) *output_failure = classify_missing(
            lane, 0u, active_screen_generation_, true);
        return false;
    }
    for (auto& slot : publication.slots) {
        const auto& cell = slot.placement;
        if (cell.atlas_width != placement.atlas_width ||
            cell.destination_x != placement.destination_x ||
            cell.destination_y != placement.destination_y ||
            cell.copy_width != placement.copy_width ||
            cell.copy_height != placement.copy_height ||
            cell.variant != placement.variant)
            continue;
        CachedAsset asset{};
        AssetLoadFailure failure{};
        if (!load(HudResourceRole::texture, canonical_name, &placement, asset, failure))
            return reject(failure.stage, failure.resource_failure, slot.ordinal);
        if (!asset.texture || asset.texture->width != placement.source_width ||
            asset.texture->height != placement.source_height)
            return reject(HudPublicationFailureStage::dimension, ResourceFailureCode::parse, slot.ordinal);
        slot.texture = asset.texture;
        slot.texture_identity = asset.identity;
        slot.placement = placement;
        slot.texture_generation = texture_generation_;
        return true;
    }
    return reject(HudPublicationFailureStage::ordinal, ResourceFailureCode::none);
}

void HudVisualPublication::invalidate_open_lane() noexcept {
    if (!lane_open_) return;
    lane_failed_ = true;
    record_open_failure(HudPublicationFailureStage::lane_invalid,
                        static_cast<std::uint32_t>(pending_slots_.size()),
                        nullptr, ResourceFailureCode::none);
    if (activation_staging_) activation_failed_ = true;
}

const HudPublishedVisual* HudVisualPublication::find(
    HudVisualLane lane, std::uint32_t ordinal) const noexcept {
    const auto index = lane_index(lane);
    if (!active_screen_generation_ || index >= active_lanes_.size())
        return nullptr;
    const auto& publication = active_lanes_[index];
    if (!publication.claimed || !publication.valid ||
        ordinal >= publication.slots.size())
        return nullptr;
    const auto& slot = publication.slots[ordinal];
    if (slot.screen_generation != active_screen_generation_ ||
        slot.texture_generation != texture_generation_ ||
        slot.figure_generation != figure_generation_ ||
        slot.overlay_generation != overlay_generation_)
        return nullptr;
    return &slot;
}

HudPublicationFailure HudVisualPublication::classify_missing(
    HudVisualLane lane, std::uint32_t ordinal,
    std::uint64_t expected_screen_generation,
    bool current_receiver) const noexcept {
    const auto index = lane_index(lane);
    if (lane_open_ && lane_ == lane && open_failure_)
        return open_failure_;
    if (index < active_lanes_.size() &&
        active_lanes_[index].first_failure)
        return active_lanes_[index].first_failure;
    if (!current_receiver)
        return make_publication_failure(
            HudPublicationFailureStage::current_receiver, lane, ordinal,
            nullptr, ResourceFailureCode::none);
    if (!active_screen_generation_ ||
        expected_screen_generation != active_screen_generation_)
        return make_publication_failure(
            HudPublicationFailureStage::generation, lane, ordinal, nullptr,
            ResourceFailureCode::none);
    if (index >= active_lanes_.size() ||
        !active_lanes_[index].claimed || !active_lanes_[index].valid)
        return make_publication_failure(
            HudPublicationFailureStage::lane_invalid, lane, ordinal, nullptr,
            ResourceFailureCode::none);
    const auto& publication = active_lanes_[index];
    if (ordinal >= publication.slots.size())
        return make_publication_failure(
            HudPublicationFailureStage::ordinal, lane, ordinal, nullptr,
            ResourceFailureCode::none);
    const auto& slot = publication.slots[ordinal];
    if (slot.screen_generation != active_screen_generation_ ||
        slot.texture_generation != texture_generation_ ||
        slot.figure_generation != figure_generation_ ||
        slot.overlay_generation != overlay_generation_)
        return make_publication_failure(
            HudPublicationFailureStage::generation, lane, ordinal, nullptr,
            ResourceFailureCode::none);
    return make_publication_failure(
        HudPublicationFailureStage::lane_invalid, lane, ordinal, nullptr,
        ResourceFailureCode::none);
}

bool HudVisualPublication::claims_lane(HudVisualLane lane) const noexcept {
    const auto index = lane_index(lane);
    return active_screen_generation_ && index < active_lanes_.size() &&
        active_lanes_[index].claimed;
}

bool HudVisualPublication::capturing_lane() const noexcept { return lane_open_; }

HudVisualLane HudVisualPublication::capturing_lane_id() const noexcept {
    return lane_;
}

bool HudVisualPublication::owns_active_screen() const noexcept {
    return active_screen_generation_ != 0;
}

std::uint64_t HudVisualPublication::active_screen_generation() const noexcept {
    return active_screen_generation_;
}

std::size_t HudVisualPublication::cached_asset_count() const noexcept {
    return assets_.size();
}

std::size_t HudVisualPublication::cached_asset_bytes() const noexcept {
    return cached_asset_bytes_;
}

bool HudVisualPublication::make_cache_room(std::size_t bytes) noexcept {
    if (bytes > kMaximumCachedBytes) return false;
    while (assets_.size() >= kMaximumCachedAssets ||
           cached_asset_bytes_ > kMaximumCachedBytes - bytes) {
        auto victim = assets_.end();
        for (auto item = assets_.begin(); item != assets_.end(); ++item) {
            const auto& value = item->second;
            const bool unpinned = (!value.figure || value.figure.use_count() == 1u) &&
                (!value.texture || value.texture.use_count() == 1u);
            if (unpinned && (victim == assets_.end() ||
                             value.last_use < victim->second.last_use))
                victim = item;
        }
        if (victim == assets_.end()) return false;
        cached_asset_bytes_ -= victim->second.owned_bytes;
        assets_.erase(victim);
    }
    return true;
}

} // namespace owned
