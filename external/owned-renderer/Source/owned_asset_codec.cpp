// Asset wire codec (owned_asset_codec.h).
#include "owned_asset_codec.h"

#include <atomic>

#include "owned_terrain_decode.h"
#include "owned_terrain_front_end.h"
#include "retained_static_scene.h"

namespace owned {
namespace {
std::atomic<TerrainPageResolver> g_terrain_page_resolver{nullptr};
} // namespace
void set_terrain_page_resolver(TerrainPageResolver resolver) noexcept {
    g_terrain_page_resolver.store(resolver, std::memory_order_release);
}
namespace codec {

template<> struct raw<ResourceStoreStats> : std::true_type {};  // 64-bit fields only
template<> struct raw<OwnedLoaderMetrics> : std::true_type {};  // 64-bit fields only

// Field lists of the nested values. The container templates find them by
// argument-dependent lookup, so they live in this namespace, not an unnamed one.

OWNED_CODEC_FIELDS(FigureCpuAsset,
    io(s, v.identity); io(s, v.signature); io(s, v.shape_count); io(s, v.position_groups);
    io(s, v.normal_groups); io(s, v.material_group); io(s, v.texture_number); io(s, v.bounds);
    io(s, v.shape_position_radii); io(s, v.positions); io(s, v.normals); io(s, v.texture_coordinates);
    io(s, v.indices); io(s, v.components); io(s, v.morph_components); io(s, v.user_data);)
OWNED_CODEC_FIELDS(ModelLink, io(s, v.child); io(s, v.parent); io(s, v.parent_index);)
OWNED_CODEC_FIELDS(ModelNodeCpuAsset,
    io(s, v.name); io(s, v.figure); io(s, v.bone_positions); io(s, v.transform_available);)
OWNED_CODEC_FIELDS(AnimationEntryAsset,
    io(s, v.name); io(s, v.ordinal); io(s, v.flags); io(s, v.special_flags); io(s, v.length);
    io(s, v.speed); io(s, v.hide_frames);)
OWNED_CODEC_FIELDS(AnimationTrackAsset,
    io(s, v.identity); io(s, v.rotations); io(s, v.translations); io(s, v.morph_frames);
    io(s, v.morph_points); io(s, v.morphs); io(s, v.translation_radius); io(s, v.morph_radius);
    io(s, v.rotation_scale_bound);)
OWNED_CODEC_FIELDS(MaterialCpuAsset,
    io(s, v.source); io(s, v.identity); io(s, v.diffuse); io(s, v.ambient); io(s, v.specular);
    io(s, v.emissive); io(s, v.previous_highlight_emissive); io(s, v.has_previous_highlight_emissive);
    io(s, v.oldest_highlight_emissive); io(s, v.has_oldest_highlight_emissive); io(s, v.power);)
OWNED_CODEC_FIELDS(TerrainTileRangeDescriptor,
    io(s, v.tile_begin); io(s, v.tile_count); io(s, v.required_animation_phases); io(s, v.index_counts);
    io(s, v.envelopes); io(s, v.liquid_depth_objects);)
OWNED_CODEC_FIELDS(TerrainMaterialDescriptor, io(s, v.subparts);)
OWNED_CODEC_FIELDS(TerrainSectorCpuAsset,
    io(s, v.identity); io(s, v.map_x); io(s, v.map_y); io(s, v.has_layers); io(s, v.has_layer_tiles);
    io(s, v.average_z); io(s, v.bounds); io(s, v.subpart_bounds); io(s, v.vertices); io(s, v.tiles);
    io(s, v.base_tile_ids); io(s, v.materials); io(s, v.animation_count); io(s, v.base_slope_normals);
    io(s, v.base_facet_flatness);)
OWNED_CODEC_FIELDS(StaticObjectSource,
    io(s, v.nid); io(s, v.family); io(s, v.name); io(s, v.prototype_reference); io(s, v.prototype_nid);
    io(s, v.visual_reference); io(s, v.primary_texture); io(s, v.secondary_texture); io(s, v.position);
    io(s, v.quaternion); io(s, v.shape); io(s, v.owner); io(s, v.quest); io(s, v.shadow);
    io(s, v.part_names); io(s, v.unknown_field_ids); io(s, v.rejection);)
OWNED_CODEC_FIELDS(ResourceContainerInfo,
    io(s, v.mount_ordinal); io(s, v.loose_file); io(s, v.bytes); io(s, v.label); io(s, v.incarnation);
    io(s, v.file_key);)

// MobChunkRecord is recursive: forward-declared overloads.
void io(Out& s, const MobChunkRecord& v);
void io(In& s, MobChunkRecord& v);
void io(Out& s, const MobChunkRecord& v) {
    io(s, v.id); io(s, v.total_bytes); io(s, v.payload_offset); io(s, v.payload_bytes); io(s, v.children);
}
void io(In& s, MobChunkRecord& v) {
    io(s, v.id); io(s, v.total_bytes); io(s, v.payload_offset); io(s, v.payload_bytes); io(s, v.children);
}

const char* asset_kind_name(AssetKind kind) noexcept {
    static constexpr const char* names[] = {"?", "blob", "figure", "model", "texture", "material", "surface",
        "database", "clip", "assembly", "terrain", "map"};
    const auto index = static_cast<std::size_t>(kind);
    return index < sizeof(names) / sizeof(names[0]) ? names[index] : "?";
}

// ---- encoder --------------------------------------------------------------

std::uint64_t AssetEncoder::lookup(const void* address, AssetKind kind) {
    const auto found = objects_.find({address, kind});
    if (found == objects_.end()) return 0;
    // Same live object (an expired entry may share the address of a new one).
    if (!found->second.alive.expired()) return found->second.id;
    const auto old = found->second.id;
    objects_.erase(found);
    release(old);
    return 0;
}

std::uint64_t AssetEncoder::known(const void* address, AssetKind kind) const noexcept {
    const auto found = objects_.find({address, kind});
    return found != objects_.end() ? found->second.id : 0;
}

std::uint64_t AssetEncoder::key_of(const void* address) const noexcept {
    for (auto kind = static_cast<std::uint8_t>(AssetKind::blob); kind < static_cast<std::uint8_t>(AssetKind::count); ++kind) {
        const auto found = objects_.find({address, static_cast<AssetKind>(kind)});
        if (found != objects_.end() && !found->second.alive.expired()) return found->second.id;
    }
    return 0;
}

void AssetEncoder::release(std::uint64_t id) {
    Out out(sink_, context_, fragment_);
    io(out, kAssetRelease);
    io(out, id);
    out.finish();
}

std::size_t AssetEncoder::sweep() {
    std::size_t released = 0;
    for (auto it = objects_.begin(); it != objects_.end();) {
        if (!it->second.alive.expired()) { ++it; continue; }
        const auto id = it->second.id;
        it = objects_.erase(it);
        release(id);
        ++released;
    }
    return released;
}

void AssetEncoder::dependencies(const TextureCpuAsset& value) { id(value.source); }
void AssetEncoder::dependencies(const OwnedVisualSurface& value) { id(value.alpha_cut_mask); }
void AssetEncoder::dependencies(const VisualAssemblyAsset& value) {
    id(value.skeleton);
    for (const auto& part : value.parts) {
        id(part.geometry); id(part.texture); id(part.surface); id(part.material);
    }
}
void AssetEncoder::dependencies(const OwnedMapSnapshot& value) {
    if (value.mob) id(value.mob->bytes);
    id(value.terrain);
    for (const auto& item : value.models) id(item.second);
    for (const auto& item : value.textures) id(item.second);
}

void AssetEncoder::body(Out& out, const std::vector<std::uint8_t>& value) { io(out, value); }
void AssetEncoder::body(Out& out, const FigureCpuAsset& value) { io(out, value); }
void AssetEncoder::body(Out& out, const ModelCpuAsset& v) {
    io(out, v.visual_reference); io(out, v.model_container); io(out, v.bone_container); io(out, v.links);
    io(out, v.nodes); io(out, v.static_shape_parameterized); io(out, v.source_bytes);
}
void AssetEncoder::body(Out& out, const TextureCpuAsset& v) {
    io(out, v.value_source); io(out, v.value_serial); io(out, v.source_name); io(out, v.identity);
    io(out, v.width); io(out, v.height); io(out, v.bit_count); io(out, v.format_signature); io(out, v.format);
    io(out, v.masks); io(out, v.shifts); io(out, v.channel_bits); io(out, v.mips); io(out, v.opaque_mips);
    ref(out, v.source); io(out, v.texel_offset); io(out, v.user_data);
}
void AssetEncoder::body(Out& out, const MaterialCpuAsset& value) { io(out, value); }
void AssetEncoder::body(Out& out, const OwnedVisualSurface& v) {
    io(out, v.id); io(out, v.generation); io(out, v.width); io(out, v.height); io(out, v.pitch);
    io(out, v.format); io(out, v.has_color_key); io(out, v.color_key); ref(out, v.alpha_cut_mask);
    io(out, v.alpha_fully_opaque); io(out, v.static_mip_hint); io(out, v.block_content[0]); io(out, v.block_content[1]);
}
void AssetEncoder::body(Out& out, const AnimationDatabaseAsset& v) {
    io(out, v.identity); io(out, v.heights); io(out, v.entries);
    io(out, v.library_binding.database_identity); io(out, v.library_binding.source_generation);
}
void AssetEncoder::body(Out& out, const AnimationClipAsset& v) {
    io(out, v.identity);
    io_count(out, v.tracks.size());
    for (const auto& track : v.tracks) { io(out, track.first); io(out, track.second); }
}
void AssetEncoder::body(Out& out, const VisualAssemblyAsset& v) {
    io(out, v.revision); ref(out, v.skeleton);
    io_count(out, v.parts.size());
    for (const auto& p : v.parts) {
        io(out, p.name); ref(out, p.geometry); io(out, p.skeleton_node); ref(out, p.texture);
        io(out, p.texture_source_name); io(out, p.assembled_character_texture); io(out, p.material_name); ref(out, p.surface); io(out, p.source_available); ref(out, p.material);
        io(out, p.diffuse); io(out, p.specular); io(out, p.texture_type); io(out, p.material_type);
        io(out, p.figure_flags); io(out, p.material_flags); io(out, p.texture_flags); io(out, p.weapon);
        io(out, p.arrow); io(out, p.explicitly_hidden); io(out, p.casts_shadow);
    }
    io(out, v.shape); io(out, v.center_shift); io(out, v.scale); io(out, v.envelope_radius);
    io(out, v.sways); io(out, v.sway_shape); io(out, v.sway_root);
}
void AssetEncoder::body(Out& out, const TerrainCpuAsset& v) {
    io(out, v.identity); io(out, v.project_resource); io(out, v.map_header_resource); io(out, v.maximum_z);
    io(out, v.sector_count_x); io(out, v.sector_count_y); io(out, v.texture_count); io(out, v.texture_size);
    io(out, v.tile_count); io(out, v.tile_size); io(out, v.tiles_per_texture_side); io(out, v.tiles_per_texture);
    io(out, v.texture_resources); io(out, v.astral_slots); io(out, v.objects); io(out, v.tile_types); io(out, v.tile_animation_phases);
    io(out, v.sectors);
    io_count(out, v.pages.size()); // pages: count only (see the header)
    io(out, v.swamp_frame_cover); io(out, v.swamp_frame_texels); io(out, v.swamp_frame_side);
    io(out, v.immutable_descriptor_validated); io(out, v.immutable_descriptor_validation_count);
    const bool outflow = bool(v.liquid_outflow);
    io(out, outflow);
}
void AssetEncoder::body(Out& out, const OwnedMapSnapshot& v) {
    io(out, v.generation); io(out, v.map_resource); io(out, v.terrain_resource); io(out, v.request_serial);
    io(out, v.deterministic_fingerprint); io(out, v.cutover);
    const bool mob = bool(v.mob);
    io(out, mob);
    if (mob) { io(out, v.mob->identity); ref(out, v.mob->bytes); io(out, v.mob->roots); }
    ref(out, v.terrain); io(out, v.objects);
    io_count(out, v.models.size());
    for (const auto& item : v.models) { io(out, item.first); ref(out, item.second); }
    io_count(out, v.textures.size());
    for (const auto& item : v.textures) { io(out, item.first); ref(out, item.second); }
    io(out, v.containers); io(out, v.failures); io(out, v.store_stats); io(out, v.metrics);
}

// ---- decoder --------------------------------------------------------------

std::uint64_t AssetDecoder::key_of(const void* address) const noexcept {
    const auto found = keys_.find(address);
    return found == keys_.end() ? 0 : found->second;
}

template<class T> bool AssetDecoder::decode(In& in, std::shared_ptr<const void>& output) {
    auto value = std::make_shared<T>();
    if (!body(in, *value) || !in.done()) return false;
    output = std::move(value);
    return true;
}

bool AssetDecoder::define(In& in, std::string& error) {
    AssetKind kind{};
    std::uint64_t id = 0;
    io(in, kind); io(in, id);
    if (!in.ok() || !id || objects_.count(id)) { error = "asset definition header (id reused or malformed)"; return false; }
    std::shared_ptr<const void> object;
    bool ok = false;
    switch (kind) {
    case AssetKind::blob: ok = decode<std::vector<std::uint8_t>>(in, object); break;
    case AssetKind::figure: ok = decode<FigureCpuAsset>(in, object); break;
    case AssetKind::model: ok = decode<ModelCpuAsset>(in, object); break;
    case AssetKind::texture: ok = decode<TextureCpuAsset>(in, object); break;
    case AssetKind::material: ok = decode<MaterialCpuAsset>(in, object); break;
    case AssetKind::surface: ok = decode<OwnedVisualSurface>(in, object); break;
    case AssetKind::database: ok = decode<AnimationDatabaseAsset>(in, object); break;
    case AssetKind::clip: ok = decode<AnimationClipAsset>(in, object); break;
    case AssetKind::assembly: ok = decode<VisualAssemblyAsset>(in, object); break;
    case AssetKind::terrain: ok = decode<TerrainCpuAsset>(in, object); break;
    case AssetKind::map: ok = decode<OwnedMapSnapshot>(in, object); break;
    default: break;
    }
    if (!ok) {
        error = std::string("malformed ") + asset_kind_name(kind) + " definition " + std::to_string(id);
        return false;
    }
    keys_[object.get()] = id;
    objects_[id] = {kind, std::move(object)};
    return true;
}

bool AssetDecoder::release(In& in, std::string& error) {
    std::uint64_t id = 0;
    io(in, id);
    const auto found = in.done() ? objects_.find(id) : objects_.end();
    if (found == objects_.end()) { error = "release of an unknown asset " + std::to_string(id); return false; }
    keys_.erase(found->second.object.get());
    objects_.erase(found);
    return true;
}

bool AssetDecoder::body(In& in, std::vector<std::uint8_t>& value) { io(in, value); return in.ok(); }
bool AssetDecoder::body(In& in, FigureCpuAsset& value) { io(in, value); return in.ok(); }
bool AssetDecoder::body(In& in, ModelCpuAsset& v) {
    io(in, v.visual_reference); io(in, v.model_container); io(in, v.bone_container); io(in, v.links);
    io(in, v.nodes); io(in, v.static_shape_parameterized); io(in, v.source_bytes);
    return in.ok();
}
bool AssetDecoder::body(In& in, TextureCpuAsset& v) {
    io(in, v.value_source); io(in, v.value_serial); io(in, v.source_name); io(in, v.identity);
    io(in, v.width); io(in, v.height); io(in, v.bit_count); io(in, v.format_signature); io(in, v.format);
    io(in, v.masks); io(in, v.shifts); io(in, v.channel_bits); io(in, v.mips); io(in, v.opaque_mips);
    ref(in, v.source); io(in, v.texel_offset); io(in, v.user_data);
    return in.ok();
}
bool AssetDecoder::body(In& in, MaterialCpuAsset& value) { io(in, value); return in.ok(); }
bool AssetDecoder::body(In& in, OwnedVisualSurface& v) {
    io(in, v.id); io(in, v.generation); io(in, v.width); io(in, v.height); io(in, v.pitch);
    io(in, v.format); io(in, v.has_color_key); io(in, v.color_key); ref(in, v.alpha_cut_mask);
    io(in, v.alpha_fully_opaque); io(in, v.static_mip_hint); io(in, v.block_content[0]); io(in, v.block_content[1]);
    return in.ok();
}
bool AssetDecoder::body(In& in, AnimationDatabaseAsset& v) {
    io(in, v.identity); io(in, v.heights); io(in, v.entries);
    io(in, v.library_binding.database_identity); io(in, v.library_binding.source_generation);
    return in.ok();
}
bool AssetDecoder::body(In& in, AnimationClipAsset& v) {
    io(in, v.identity);
    const auto count = in.count(1);
    for (std::size_t i = 0; i < count && in.ok(); ++i) {
        std::string name;
        io(in, name);
        io(in, v.tracks[name]);
    }
    return in.ok();
}
bool AssetDecoder::body(In& in, VisualAssemblyAsset& v) {
    io(in, v.revision); ref(in, v.skeleton);
    v.parts.resize(in.count(1));
    for (auto& p : v.parts) {
        io(in, p.name); ref(in, p.geometry); io(in, p.skeleton_node); ref(in, p.texture);
        io(in, p.texture_source_name); io(in, p.assembled_character_texture); io(in, p.material_name); ref(in, p.surface); io(in, p.source_available); ref(in, p.material);
        io(in, p.diffuse); io(in, p.specular); io(in, p.texture_type); io(in, p.material_type);
        io(in, p.figure_flags); io(in, p.material_flags); io(in, p.texture_flags); io(in, p.weapon);
        io(in, p.arrow); io(in, p.explicitly_hidden); io(in, p.casts_shadow);
        if (!in.ok()) return false;
    }
    io(in, v.shape); io(in, v.center_shift); io(in, v.scale); io(in, v.envelope_radius);
    io(in, v.sways); io(in, v.sway_shape); io(in, v.sway_root);
    return in.ok();
}
bool AssetDecoder::body(In& in, TerrainCpuAsset& v) {
    io(in, v.identity); io(in, v.project_resource); io(in, v.map_header_resource); io(in, v.maximum_z);
    io(in, v.sector_count_x); io(in, v.sector_count_y); io(in, v.texture_count); io(in, v.texture_size);
    io(in, v.tile_count); io(in, v.tile_size); io(in, v.tiles_per_texture_side); io(in, v.tiles_per_texture);
    io(in, v.texture_resources); io(in, v.astral_slots); io(in, v.objects); io(in, v.tile_types); io(in, v.tile_animation_phases);
    io(in, v.sectors);
    const auto pages = in.count(0);
    if (pages > 65536) return false;
    v.pages.resize(pages);
    io(in, v.swamp_frame_cover); io(in, v.swamp_frame_texels); io(in, v.swamp_frame_side);
    io(in, v.immutable_descriptor_validated); io(in, v.immutable_descriptor_validation_count);
    bool outflow = false;
    io(in, outflow);
    if (outflow) owned_terrain_reset_liquid_outflow(v);
    if (in.ok()) {
        owned_terrain_report_astral_received(v);
        if (const auto resolver = g_terrain_page_resolver.load(std::memory_order_acquire)) resolver(v);
    }
    return in.ok();
}
bool AssetDecoder::body(In& in, OwnedMapSnapshot& v) {
    io(in, v.generation); io(in, v.map_resource); io(in, v.terrain_resource); io(in, v.request_serial);
    io(in, v.deterministic_fingerprint); io(in, v.cutover);
    bool mob = false;
    io(in, mob);
    if (mob) {
        auto document = std::make_shared<MobDocument>();
        io(in, document->identity); ref(in, document->bytes); io(in, document->roots);
        v.mob = std::move(document);
    }
    ref(in, v.terrain); io(in, v.objects);
    for (std::size_t i = 0, n = in.count(1); i < n && in.ok(); ++i) {
        std::string name; io(in, name); ref(in, v.models[name]);
    }
    for (std::size_t i = 0, n = in.count(1); i < n && in.ok(); ++i) {
        std::string name; io(in, name); ref(in, v.textures[name]);
    }
    io(in, v.containers); io(in, v.failures); io(in, v.store_stats); io(in, v.metrics);
    return in.ok();
}

} // namespace codec
} // namespace owned
