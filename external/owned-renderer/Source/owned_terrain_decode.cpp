// Terrain decode shared by the renderer and Prototypes/shader-lab: the
// vertex streams of one terrain command (decode_owned_terrain_tiles), the
// per-tile contact grid and the terrain material blend rows of a map, and its
// tile families. Moved out of engine_bridge.cpp and renderer.cpp unchanged.
#include "owned_terrain_decode.h"
#include "retained_static_scene.h"

#include "contact_blend.h"
#include "terrain_tile_presets.h"
#include "terrain_tile_map_families.h"

#include <bcrypt.h>
#include <cctype>
#include <unordered_map>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <chrono>
#include <cmath>
#include <limits>
#include <map>
#include <memory>

namespace owned {
namespace {
constexpr std::uint32_t kTerrainLayerVertexOffset = 33u * 33u;
constexpr std::uint32_t kTerrainGrassObjectType = 4;
// Steep tile class: a base vertex whose slope normal z is below the walk
// limit (terrain_cliff.hlsl terrainWalkLimitZ, cos 40 = .76604444) plus a
// margin. Flat tiles' interpolated normals stay inside that cone, so their
// side projection share is exactly 0 (terrainCliffPlaneWeights); their edges
// all have both corners at or above terrainFlatEdgeZ (below this value), so
// a steep neighbour keeps every shared edge whole (nativeTessFactorsOf).
constexpr float kTerrainSteepClassZ = .76604444f + .002f;
} // namespace

bool prepare_owned_terrain_source_view(const OwnedTerrainFramePacket& packet,
                         const OwnedTerrainCommand& command,
                         const OwnedTerrainDynamicSector& dynamic,
                         OwnedTerrainSourceDecodeContext& context,
                         NativeGeometryView& view) noexcept {
    if (!packet.source || command.source_sector >= packet.source->sectors.size() ||
        !dynamic.captured || !command.vertex_count || !command.index_count)
        return false;
    const auto& sector = packet.source->sectors[command.source_sector];
    if (command.material >= sector.materials.size())
        return false;
    const auto& material = sector.materials[command.material];
    context = {};
    context.source = packet.source.get();
    context.sector = &sector;
    context.material = &material;
    context.dynamic = &dynamic;
    context.command = command;
    view = {};
    view.object_token = reinterpret_cast<std::uintptr_t>(&sector);
    view.figure_token = view.object_token;
    view.geometry_token = reinterpret_cast<std::uintptr_t>(&material);
    view.cache_identity = view.geometry_token;
    // Owned addresses are only lookup tokens. The full revision binds an
    // allocator-reused address to generation+coordinate+material+variant.
    view.geometry_revision = owned_terrain_geometry_revision(
        packet, command, dynamic.structural_generation);
    view.geometry_revision_valid = true;
    view.position_groups = (command.vertex_count + 3u) / 4u;
    view.normal_groups = view.position_groups;
    view.texture_coordinate_count = command.vertex_count;
    view.component_count = command.vertex_count;
    view.index_count = command.index_count;
    view.object_to_clip = packet.inputs.object_to_clip;
    view.lighting.enabled = 1;
    view.lighting.object_to_world = {
        1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    view.semantic_kind = SemanticKind::terrain;
    view.ordering = command.material < packet.source->texture_count + 2u
        ? NativeOrdering::reorderable : NativeOrdering::preserve;
    view.flags = 8u;
    view.decoded_channels = native_decoded_terrain;
    view.terrain = dynamic.animation;
    view.terrain.liquid_detail_time = dynamic.animation.precise_time;
    view.terrain.animation_phase_count =
        static_cast<std::uint32_t>(dynamic.animation_phases.size());
    view.terrain_animation_phases = dynamic.animation_phases.empty()
        ? nullptr : dynamic.animation_phases.data();
    if (command.material < packet.source->pages.size() &&
        packet.source->pages[command.material].mip_count > 1) {
        view.terrain_page = &packet.source->pages[command.material];
    }
    view.terrain_page_count = packet.source->texture_count + 2u;
    view.decode_context = &context;
    view.decode = [](void* opaque, NativeGeometryDecodeTarget& target) { const auto* input = static_cast<const OwnedTerrainSourceDecodeContext*>(opaque); return owned_terrain_decode_valid(input, target) && decode_owned_terrain_tiles(*input, target); };
    return true;
}

bool prepare_owned_terrain_world_record(const RetainedTerrainSourceSnapshot& source,
    OwnedTerrainFramePacket& packet, const OwnedTerrainCommand& command,
    const OwnedTerrainDynamicSector* color, const NativeContribution& contribution,
    OwnedTerrainSourceDecodeContext& context, NativeGeometryView*& output,
    bool alpha_coverage) {
    output=nullptr;
    if (!contribution.known) return false;
    if (!contribution.color && !contribution.light_mask) return true;
    const auto& inputs=packet.inputs;
    if (!source.terrain || !source.geometry || !source.prepared ||
        inputs.source_generation!=source.source_generation || inputs.source_revision!=source.source_revision ||
        command.source_sector>=source.terrain->sectors.size()) return false;
    const auto& sector=source.terrain->sectors[command.source_sector];
    if (command.material>=sector.materials.size()) return false;
    NativeWorldBounds bounds{},world_bounds{};
    if (!owned_terrain_spatial_bounds(packet,command,bounds,world_bounds) || !bounds.valid)
        return false;
    ++packet.spatial_record_reuses;
    OwnedTerrainDynamicSector geometry_only{};
    geometry_only.captured=true;
    geometry_only.animation=source.geometry->animation;
    const std::uint32_t variant=command.lod|(command.lu_higher?4u:0u)|(command.rd_higher?8u:0u);
    const std::array<std::uint32_t,4> key{command.source_sector,command.material,command.subpart_mask,variant};
    if(source.prepared->parts.size()>=65536 && source.prepared->parts.find(key)==source.prepared->parts.end())return false;
    auto& handles=source.prepared->parts[key];
    const auto structure=owned_terrain_command_structure(*source.terrain,command,*source.geometry);
    if(!structure)return false;
    const auto revision=owned_terrain_geometry_revision(packet,command,structure);
    if(!handles.source_revision || handles.source.geometry_revision!=revision) {
        NativeGeometryView next;
        if(!prepare_owned_terrain_source_view(packet,command,color?*color:geometry_only,context,next))return false;
        next.geometry_revision=revision;next.decode_context=nullptr;next.terrain_animation_phases=nullptr;
        handles.source=next;handles.owner=source.prepared;
        if(++handles.source_revision==0)++handles.source_revision;
    }
    auto& view=handles.source;
    view.owned_fixed_state={};view.owned_texture={};view.terrain_phase_values=nullptr;
    view.object_to_clip=inputs.object_to_clip;
    context={};context.source=source.terrain.get();context.sector=&sector;
    context.material=&sector.materials[command.material];context.command=command;
    context.dynamic=color;context.geometry=source.geometry.get();view.decode_context=&context;
    context.borrowed_view=&view;output=&view;
    view.terrain=source.geometry->animation;
    view.terrain.liquid_detail_time=view.terrain.precise_time;
    if ((contribution.color || alpha_coverage) && color) {
        view.terrain.liquid_detail_time=color->animation.precise_time;
        view.terrain.animated_tiles_enabled=color->animation.animated_tiles_enabled;
        view.terrain.animation_phase_count=static_cast<std::uint32_t>(color->animation_phases.size());
        view.terrain_animation_phases=color->animation_phases.empty()?nullptr:color->animation_phases.data();
        view.terrain_phase_values=&color->phase_values;
    } else {
        view.terrain_animation_phases=nullptr;view.terrain.animation_phase_count=0;
        view.terrain.animated_tiles_enabled=0;
    }
    // Opaque depth needs no atlas phase. Cutout depth and color consume the
    // same completed phase packet, never an independently advanced animation.
    float maximum_multiplier=0;
    for (unsigned part=0;part<4;++part) if (command.subpart_mask&(1u<<part)) {
        const auto& range=sector.materials[command.material].subparts[part];
        if ((contribution.color || alpha_coverage) && range.required_animation_phases &&
            (!color || !color->captured || range.required_animation_phases>color->animation_phases.size())) return false;
        for (const auto& group:range.envelopes)
            maximum_multiplier=(std::max)(maximum_multiplier,group.maximum_wave_multiplier);
    }
    view.terrain.maximum_wind_multiplier=maximum_multiplier;
    if (maximum_multiplier==0) view.terrain.waves_enabled=0;
    view.prepared_mesh=&handles.mesh;view.prepared_material=&handles.material;
    view.prepared_identity={static_cast<std::int32_t>(command.source_sector),source.source_generation,
        source.source_generation,source.source_revision,
        (std::uint32_t(command.material)<<4u)|command.subpart_mask,0,inputs.world_frame_serial,true,true};
    view.world.record_revision=handles.source_revision;view.world.owner=&handles.owner;
    view.world.enabled=true;view.world.identity=&sector.identity;
    view.world.part=(std::uint32_t(command.material)<<8u)|(std::uint32_t(command.subpart_mask)<<4u)|variant;
    view.world.object_to_world={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    view.world.world_to_clip=inputs.object_to_clip;
    view.bounds_minimum=world_bounds.minimum;view.bounds_maximum=world_bounds.maximum;
    view.contribution=contribution;
    return true;
}

TerrainIndexTemplate terrain_index_template(std::uint32_t lod, std::uint8_t mask,
                                             bool lu_higher, bool rd_higher) {
    static constexpr std::uint8_t lod0_regular[]{
        0,1,4, 0,4,3, 1,2,5, 1,5,4, 3,4,7, 3,7,6, 4,5,8, 4,8,7};
    static constexpr std::uint8_t lod0_up[]{
        0,2,4, 0,4,3, 2,5,4, 3,4,7, 3,7,6, 4,5,8, 4,8,7};
    static constexpr std::uint8_t lod0_down[]{
        0,1,4, 0,4,3, 1,2,5, 1,5,4, 3,4,6, 4,8,6, 4,5,8};
    static constexpr std::uint8_t lod1_regular[]{0,1,3, 0,3,2, 2,3,5, 2,5,4};
    static constexpr std::uint8_t lod1_left[]{0,1,3, 0,3,4, 3,5,4};
    static constexpr std::uint8_t lod1_right[]{0,1,2, 1,5,2, 2,5,4};
    static constexpr std::uint8_t lod2_regular[]{0,1,3, 0,3,2};
    if (lod == 0) {
        if (lu_higher && (mask & 1u)) return {lod0_up, static_cast<std::uint32_t>(std::size(lod0_up))};
        if (rd_higher && (mask & 2u)) return {lod0_down, static_cast<std::uint32_t>(std::size(lod0_down))};
        return {lod0_regular, static_cast<std::uint32_t>(std::size(lod0_regular))};
    }
    if (lod == 1) {
        if (lu_higher && (mask & 4u)) return {lod1_left, static_cast<std::uint32_t>(std::size(lod1_left))};
        if (rd_higher && (mask & 8u)) return {lod1_right, static_cast<std::uint32_t>(std::size(lod1_right))};
        return {lod1_regular, static_cast<std::uint32_t>(std::size(lod1_regular))};
    }
    return {lod2_regular, static_cast<std::uint32_t>(std::size(lod2_regular))};
}

std::uint32_t terrain_vertices_per_tile(std::uint32_t lod) {
    static constexpr std::uint32_t counts[]{9, 6, 4};
    return lod < std::size(counts) ? counts[lod] : 0;
}

bool set_owned_terrain_material(const TerrainObjectDescriptor& object,
                                NativeTerrainVertex& vertex) noexcept {
    // The game turns opacity into a byte without a range check; an
    // out-of-range value is drawn clamped instead of dropping the sector.
    if (!std::isfinite(object.opacity)) return false;
    const float opacity = (std::min)((std::max)(object.opacity, 0.0f), 1.0f);
    vertex.opacity = static_cast<std::uint32_t>(opacity * 255.0f) / 255.0f;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const float emissive = object.color[axis] * object.self_illumination;
        if (!std::isfinite(emissive)) return false;
        vertex.emissive[axis] = std::min(emissive, 1.0f);
    }
    return true;
}

bool owned_terrain_decode_valid(const OwnedTerrainSourceDecodeContext* context,
                                const NativeGeometryDecodeTarget& target) noexcept {
    return !(!context || !context->source || !context->sector || !context->material ||
        (!context->geometry && (!context->dynamic || !context->dynamic->captured)) ||
        target.position_groups != (context->command.vertex_count + 3u) / 4u ||
        target.normal_groups != target.position_groups ||
        target.texture_coordinate_count != context->command.vertex_count ||
        target.component_count != context->command.vertex_count ||
        target.index_count != context->command.index_count ||
        target.terrain_vertex_count != context->command.vertex_count ||
        !target.positions || !target.normals ||
        !target.texture_coordinates || !target.components ||
        !target.indices || !target.terrain_vertices);
}

bool decode_owned_terrain_tiles(const OwnedTerrainSourceDecodeContext& decode,
                                NativeGeometryDecodeTarget& target) {
    const auto* context = &decode;
    static constexpr std::array<std::array<std::uint8_t, 9>, 3> lod_vertex_map{{
        {{0,1,2,3,4,5,6,7,8}}, {{0,2,3,5,6,8,0,0,0}},
        {{0,2,6,8,0,0,0,0,0}}
    }};
    const std::uint32_t vertices_per_tile =
        terrain_vertices_per_tile(context->command.lod);
    if (!vertices_per_tile)
        return false;
    try {
        std::fill_n(target.positions, target.position_groups, NativePackedGroup{});
        std::fill_n(target.normals, target.normal_groups, NativePackedGroup{});
        std::fill_n(target.terrain_vertices, target.terrain_vertex_count,
                    NativeTerrainVertex{});
        OwnedTerrainGeometryValues local_geometry;
        // Published geometry decodes layer z and depths at the authored rest
        // heights; the shader adds the live layer shifts (shift_objects). The
        // flow field reads the shifted surface the game shows when this range
        // decodes: a map can carry a constant nonzero shift, and the flow of
        // the rest surface there (its buried test) broke rivers into
        // faceted chevron bands. A later shift change does not re-decode.
        const OwnedTerrainGeometryValues* direction_geometry=context->geometry;
        if(!direction_geometry) {
            // Compatibility decode has completed sector heights. Recover only
            // shifts actually observed here; unknown neighbours stay unavailable.
            local_geometry.layer_shifts.assign(context->source->objects.size(),
                std::numeric_limits<float>::quiet_NaN());
            for(std::size_t n=0;n<context->dynamic->layer_z.size()&&n+1089<context->sector->vertices.size();++n) {
                const auto& layer=context->sector->vertices[n+1089];
                if(layer.lighting_object<local_geometry.layer_shifts.size())
                    local_geometry.layer_shifts[layer.lighting_object]=context->dynamic->layer_z[n]-layer.z;
            }
            direction_geometry=&local_geometry;
        }
        std::array<TerrainObjectDescriptor,1089> liquid_materials{};
        std::array<std::uint16_t,256> liquid_masks{};
        std::array<float,1089> liquid_swamp{};
        std::unique_ptr<OwnedLiquidFlowSector> liquid_flow; // first water tile prepares it
        // A tile's frame bounds in its atlas (min/max of its lane UVs).
        const auto tile_uv_bounds=[&](const TerrainTileDescriptor& tile,float bounds[4]) {
            const auto uvs=terrain_tile_uvs(*context->source,tile);
            bounds[0]=bounds[2]=uvs[0].u;bounds[1]=bounds[3]=uvs[0].v;
            for(const auto& uv:uvs) {
                bounds[0]=(std::min)(bounds[0],uv.u);bounds[1]=(std::min)(bounds[1],uv.v);
                bounds[2]=(std::max)(bounds[2],uv.u);bounds[3]=(std::max)(bounds[3],uv.v);
            }
        };
        // Pure swamp tiles (frame cover 1, swamp_frame_cover) of this sector
        // and its ring (local tile coordinates -3..18): their atlas page and
        // frame bounds. The liquid shader reads the algae art from the bounds
        // its triangle carries, so a water tile inside the soft swamp edge,
        // and a swamp transition tile whose frame has open water painted in,
        // borrow the nearest pure swamp tile's frame: their own frames drawn
        // as algae cut hard tile-shaped water patches into the mat.
        struct SwampArtSource{bool valid=false;std::uint16_t page=0;float bounds[4]{};};
        std::array<SwampArtSource,22*22> swamp_art{};
        if(context->command.material>=context->source->texture_count+2){
            owned_terrain_liquid_materials(*context->source,context->command.source_sector,liquid_materials,&liquid_swamp);
            owned_terrain_liquid_neighbour_masks(*context->source,context->command.source_sector,liquid_masks);
            const auto& source=*context->source;const auto& origin=*context->sector;
            for(int sy=(std::max)(0,int(origin.map_y)-1);sy<=(std::min)(int(source.sector_count_y)-1,int(origin.map_y)+1);++sy)
            for(int sx=(std::max)(0,int(origin.map_x)-1);sx<=(std::min)(int(source.sector_count_x)-1,int(origin.map_x)+1);++sx)
            for(const auto& tile:source.sectors[std::size_t(sy)*source.sector_count_x+std::size_t(sx)].tiles) {
                if(!tile.transparent||tile.object_id>=source.objects.size()||
                   !terrain_liquid_object(source.objects[tile.object_id].type)||!terrain_swamp_tile(source,tile)||
                   terrain_swamp_frame_cover(source,tile)<1.f)continue;
                unsigned low_x=33,low_y=33;
                for(auto index:tile.vertex_indices){const unsigned grid=index>=kTerrainLayerVertexOffset?index-kTerrainLayerVertexOffset:index;
                    low_x=(std::min)(low_x,grid%33u);low_y=(std::min)(low_y,grid/33u);}
                const int tx=(sx-int(origin.map_x))*16+int(low_x/2),ty=(sy-int(origin.map_y))*16+int(low_y/2);
                if(tx<-3||tx>18||ty<-3||ty>18)continue;
                auto& art=swamp_art[std::size_t(ty+3)*22+std::size_t(tx+3)];
                art.valid=true;art.page=tile.material_index;tile_uv_bounds(tile,art.bounds);
            }
        }
        std::uint32_t vertex_cursor = 0;
        std::uint32_t index_cursor = 0;
        // Page-material commands emit their flat tiles first, then their steep
        // tiles (some base vertex's slope normal past the walk limit, decoded
        // as the shader decodes it, with a margin): the steep class alone can
        // show side projections, so the renderer draws the two index ranges
        // with different pixel variants (steep_index_begin).
        const bool split_classes = context->command.material < context->source->texture_count;
        // Every tile is classed by its base vertex normals (transparent and
        // animated tiles too: they never carry the material or long-tile
        // flags, so the flat and full pixel variants draw them alike), since
        // the flat class also draws untessellated: a tile is flat only when
        // all its edges are flat-class edges (nativeTessFactorsOf).
        const auto steep_tile = [&](const TerrainTileDescriptor& tile) {
            for (const auto index : tile.vertex_indices) {
                if (index >= kTerrainLayerVertexOffset || index >= context->sector->vertices.size()) continue;
                const std::uint32_t packed = owned_terrain_slope_normal(*context->source, *context->sector, index);
                const float x = float(std::int16_t(packed & 0xffffu)) / 32767.0f, y = float(std::int16_t(packed >> 16)) / 32767.0f;
                const float z = std::sqrt((std::max)(0.0f, 1.0f - x * x - y * y));
                if (z < kTerrainSteepClassZ) return true;
            }
            return false;
        };
        for (std::uint32_t pass = 0; pass < (split_classes ? 2u : 1u); ++pass) {
        if (pass == 1) target.steep_index_begin = index_cursor;
        for (std::uint32_t part = 0; part < 4u; ++part) {
            if (!(context->command.subpart_mask & (1u << part))) continue;
            const auto& range = context->material->subparts[part];
            const std::uint32_t range_end =
                static_cast<std::uint32_t>(range.tile_begin) + range.tile_count;
            if (range_end > context->sector->tiles.size()) return false;
            for (std::uint32_t tile_index = range.tile_begin;
                 tile_index < range_end; ++tile_index) {
                const auto& tile = context->sector->tiles[tile_index];
                if (split_classes && steep_tile(tile) != (pass == 1)) continue;
                bool water_tile = false;
                if (tile.transparent) {
                    if (tile.object_id >= context->source->objects.size())
                        return false;
                    const auto type = context->source->objects[tile.object_id].type;
                    water_tile = terrain_liquid_object(type);
                }
                const auto tile_uvs = terrain_tile_uvs(*context->source, tile);
                float liquid_uv_bounds[4]{};
                std::uint32_t swamp_art_flags=0;
                unsigned tile_low_x=33,tile_low_y=33;
                if (water_tile) {
                    for (auto index:tile.vertex_indices) {
                        const unsigned grid=index>=kTerrainLayerVertexOffset?index-kTerrainLayerVertexOffset:index;
                        tile_low_x=(std::min)(tile_low_x,grid%33u);tile_low_y=(std::min)(tile_low_y,grid/33u);
                    }
                    tile_uv_bounds(tile,liquid_uv_bounds);
                    // Open water and swamp transition frames: the nearest pure
                    // swamp tile of the same page within three tiles lends its
                    // frame (static: the tile's own flipbook phase must not
                    // move it). Without one (a page with no pure swamp frame,
                    // zone3obr's transition page) the tile takes the map swamp
                    // frame (TerrainCpuAsset::swamp_frame_texels, its own
                    // texture): it never draws water art as algae.
                    const bool swamp_type=terrain_swamp_tile(*context->source,tile);
                    if ((!swamp_type||terrain_swamp_frame_cover(*context->source,tile)<1.f) && tile_low_x<32u && tile_low_y<32u) {
                        const int tx=int(tile_low_x/2),ty=int(tile_low_y/2);
                        int best=INT_MAX;const SwampArtSource* lender=nullptr;
                        for(int dy=-3;dy<=3;++dy)for(int dx=-3;dx<=3;++dx) {
                            const auto& art=swamp_art[std::size_t(ty+dy+3)*22+std::size_t(tx+dx+3)];
                            if(!art.valid||art.page!=tile.material_index||dx*dx+dy*dy>=best)continue;
                            best=dx*dx+dy*dy;lender=&art;
                        }
                        if(lender){std::copy_n(lender->bounds,4,liquid_uv_bounds);swamp_art_flags=native_terrain_swamp_art_borrowed;}
                        else swamp_art_flags=native_terrain_swamp_art_map;
                    }
                }
                const std::uint32_t tile_base = vertex_cursor;
                const bool material_tile = !tile.transparent &&
                    tile.animation_ordinal == UINT_MAX &&
                    tile.material_index < context->source->texture_count;
                const bool long_tile = !tile.transparent &&
                    tile.animation_ordinal == UINT_MAX &&
                    tile.material_index >= context->source->texture_count;
                const std::uint32_t fill_flags = material_tile
                    ? owned_terrain_fill_flags(*context->source, *context->sector, tile, tile_uvs) : 0u;
                for (std::uint32_t item = 0; item < vertices_per_tile; ++item) {
                    const std::uint32_t source_lane =
                        lod_vertex_map[context->command.lod][item];
                    const std::uint32_t source_index =
                        tile.vertex_indices[source_lane];
                    if (source_index >= context->sector->vertices.size()) return false;
                    const auto& source_vertex =
                        context->sector->vertices[source_index];
                    float z = source_vertex.z;
                    auto& terrain_vertex = target.terrain_vertices[vertex_cursor];
                    const auto bed_liquid=terrain_base_liquid_object(*context->source,*context->sector,source_index);
                    if (bed_liquid!=UINT16_MAX) {
                        const auto& object=context->source->objects[bed_liquid];
                        terrain_vertex.liquid_bed_emission=object.self_illumination;
                        if (context->geometry) {
                            // Signed rest depth; the shader adds the live shift and clamps.
                            terrain_vertex.liquid_depth=
                                context->sector->vertices[source_index+kTerrainLayerVertexOffset].z-source_vertex.z;
                            terrain_vertex.shift_objects=(terrain_vertex.shift_objects&0xffffu)|(std::uint32_t(bed_liquid)<<16);
                        } else {
                            if (source_index>=context->dynamic->layer_z.size()) return false;
                            terrain_vertex.liquid_depth=(std::max)(0.0f,
                                context->dynamic->layer_z[source_index]-source_vertex.z);
                        }
                    }
                    if (water_tile) {
                        terrain_vertex.flags |= native_terrain_water_vertex;
                        const unsigned grid=source_index>=kTerrainLayerVertexOffset?
                            source_index-kTerrainLayerVertexOffset:source_index;
                        const unsigned local_x=grid%33u-tile_low_x,local_y=grid/33u-tile_low_y;
                        if (local_x<=2u && local_y<=2u && tile_low_x<32u && tile_low_y<32u)
                            terrain_vertex.flags |= native_terrain_liquid_edge|
                                (std::uint32_t(liquid_masks[(tile_low_y/2u)*16u+tile_low_x/2u]&511u)<<16)|
                                (local_x<<25)|(local_y<<27);
                    }
                    if (source_index >= kTerrainLayerVertexOffset) {
                        const std::uint32_t layer_index =
                            source_index - kTerrainLayerVertexOffset;
                        terrain_vertex.grid_index = layer_index;
                        // The layer object, the same for both copies of a
                        // sector-edge vertex (owned_terrain_layer_object).
                        const std::uint16_t layer_object=
                            owned_terrain_layer_object(*context->source,*context->sector,layer_index);
                        if (context->geometry) {
                            if (layer_index>=context->geometry->wind_noise.size() || !std::isfinite(z)) return false;
                            terrain_vertex.noise=context->geometry->wind_noise[layer_index];
                            if (layer_object != UINT16_MAX)
                                terrain_vertex.shift_objects=(terrain_vertex.shift_objects&0xffff0000u)|layer_object;
                        } else {
                            if (layer_index>=context->dynamic->layer_z.size() ||
                                layer_index>=context->dynamic->wind_noise.size()) return false;
                            z=context->dynamic->layer_z[layer_index];
                            terrain_vertex.noise=context->dynamic->wind_noise[layer_index];
                        }
                        terrain_vertex.depth = z -
                            context->sector->vertices[layer_index].z;
                        if (layer_object != UINT16_MAX) {
                            if (layer_object >=
                                context->source->objects.size())
                                return false;
                            const auto& object = context->source->objects[
                                layer_object];
                            terrain_vertex.object_color[0] = object.color[0];
                            terrain_vertex.object_color[1] = object.color[1];
                            terrain_vertex.object_color[2] = object.color[2];
                            terrain_vertex.wind_multiplier = object.wave_multiplier;
                            if (!set_owned_terrain_material(object, terrain_vertex))
                                return false;
                            terrain_vertex.flags |= native_terrain_wave_vertex;
                            if (object.type == kTerrainGrassObjectType)
                                terrain_vertex.flags |= native_terrain_grass_vertex;
                        }
                    }
                    if (water_tile) {
                        const auto& object=liquid_materials[source_index-kTerrainLayerVertexOffset];
                        std::copy(object.color.begin(),object.color.end(),terrain_vertex.object_color);
                        const float swamp=liquid_swamp[source_index-kTerrainLayerVertexOffset];
                        terrain_vertex.flags|=std::uint32_t(std::lround((std::clamp)(swamp,0.f,1.f)*7.f))<<native_terrain_swamp_shift;
                        if(!liquid_flow) {
                            liquid_flow=std::make_unique<OwnedLiquidFlowSector>();
                            if(!liquid_flow->prepare(*context->source,context->command.source_sector,direction_geometry))
                                return false;
                        }
                        OwnedLiquidFlow flow;
                        if(!liquid_flow->flow(source_index-kTerrainLayerVertexOffset,flow))return false;
                        {const unsigned grid=source_index-kTerrainLayerVertexOffset;
                         owned_terrain_weld_edge_flow(*context->source,int(context->sector->map_x*32+grid%33u),
                             int(context->sector->map_y*32+grid/33u),flow);}
                        terrain_vertex.liquid_bend=flow.bend;terrain_vertex.liquid_cascade=flow.cascade;terrain_vertex.liquid_drop=flow.drop;terrain_vertex.liquid_fill=flow.fill;
                        std::copy(flow.current.begin(),flow.current.end(),terrain_vertex.liquid_current);
                        terrain_vertex.liquid_multiplier=object.wave_multiplier;
                        terrain_vertex.liquid_self_illumination=object.self_illumination;
                        std::copy_n(liquid_uv_bounds,4,terrain_vertex.liquid_uv_bounds);
                        terrain_vertex.flags|=swamp_art_flags;
                        if (!set_owned_terrain_material(object,terrain_vertex)) return false;
                        terrain_vertex.opacity=object.opacity;
                        for(unsigned axis=0;axis<3;++axis)
                            terrain_vertex.emissive[axis]=object.color[axis]*object.self_illumination;
                    }
                    if (tile.animation_ordinal != UINT_MAX) {
                        if (!context->geometry && tile.animation_ordinal >=
                            context->dynamic->animation_phases.size())
                            return false;
                        terrain_vertex.flags |= native_terrain_animated_uv;
                        terrain_vertex.animation_index = tile.animation_ordinal;
                        const std::uint32_t first_long =
                            context->source->texture_count *
                            context->source->tiles_per_texture;
                        terrain_vertex.animation_base_tile =
                            tile.tile_index < first_long
                                ? tile.tile_index % context->source->tiles_per_texture
                                : (tile.tile_index - first_long) % 8u;
                    }
                    if (source_index < kTerrainLayerVertexOffset) {
                        terrain_vertex.slope_normal = owned_terrain_slope_normal(
                            *context->source, *context->sector, source_index);
                        // Base vertices carry no grid index (layer vertices
                        // only); bits 16-31 hold the facet flatness.
                        terrain_vertex.grid_index = owned_terrain_facet_flatness(
                            *context->source, *context->sector, source_index) << 16;
                        // Dry base vertices: liquid_fill holds the authored
                        // pit depth the shadow casters fill (L-085).
                        if (!water_tile)
                            terrain_vertex.liquid_fill = owned_terrain_pit_fill(
                                *context->source, *context->sector, source_index);
                        // Dry material vertices: liquid_drop holds the rise
                        // over the local floor (the blend's wall-foot band).
                        if (!water_tile && material_tile)
                            terrain_vertex.liquid_drop = owned_terrain_foot_rise(
                                *context->source, *context->sector, source_index);
                        // ... and liquid_bend the wall rising beside it
                        // (wall-foot contact darkening).
                        if (!water_tile && material_tile)
                            terrain_vertex.liquid_bend = owned_terrain_wall_above(
                                *context->source, *context->sector, source_index);
                        if (material_tile)
                            terrain_vertex.flags |= native_terrain_material_vertex | fill_flags;
                        else if (long_tile)
                            terrain_vertex.flags |= native_terrain_long_tile;
                    }
                    const std::uint32_t group = vertex_cursor >> 2u;
                    const std::uint32_t lane = vertex_cursor & 3u;
                    target.positions[group].x[lane] = source_vertex.x;
                    target.positions[group].y[lane] = source_vertex.y;
                    target.positions[group].z[lane] = z;
                    target.positions[group].w[lane] = 1.0f;
                    target.normals[group].x[lane] = source_vertex.normal_x;
                    target.normals[group].y[lane] = source_vertex.normal_y;
                    target.normals[group].z[lane] = source_vertex.normal_z;
                    target.texture_coordinates[vertex_cursor] = tile_uvs[source_lane];
                    const auto component =
                        static_cast<std::uint16_t>(vertex_cursor);
                    target.components[vertex_cursor] =
                        {component, component, component};
                    ++vertex_cursor;
                }
                const auto topology = terrain_index_template(
                    context->command.lod, tile.edge_mask,
                    context->command.lu_higher, context->command.rd_higher);
                if (index_cursor + topology.count > target.index_count) return false;
                for (std::uint32_t index = 0; index < topology.count; ++index)
                    target.indices[index_cursor++] = static_cast<std::uint16_t>(
                        tile_base + topology.indices[index]);
            }
        }
        }
        // Other materials (long, layer and liquid tiles) show no side
        // projection: every tile counts as flat.
        if (!split_classes) target.steep_index_begin = index_cursor;
        return vertex_cursor == target.component_count &&
            index_cursor == target.index_count;
    } catch (...) {
        return false;
    }
}


std::vector<std::array<std::uint32_t,4>> owned_terrain_contact_tiles(const TerrainCpuAsset& terrain,
    std::uint32_t tiles_x,std::uint32_t tiles_y,std::uint64_t& used){
  std::vector<std::array<std::uint32_t,4>> tiles(std::size_t(tiles_x)*tiles_y,std::array<std::uint32_t,4>{0,0,0,0xffffffffu});
  constexpr std::uint32_t layer=33u*33u;
  const auto unorm=[](float u,float v){
   const auto q=[](float x){return std::uint32_t(std::lround(std::clamp(x,0.f,1.f)*65535.f));};
   return q(u)|(q(v)<<16);
  };
  used=0;
  for(const auto& sector:terrain.sectors){
   if(sector.map_x>=terrain.sector_count_x||sector.map_y>=terrain.sector_count_y)continue;
   for(const auto& tile:sector.tiles){
    if(tile.transparent||tile.vertex_indices[0]>=layer||tile.material_index>=contact_blend_max_materials)continue;
    std::uint32_t lx=33,ly=33;
    for(const unsigned k:{0u,2u,6u,8u}){
     lx=std::min(lx,std::uint32_t(tile.vertex_indices[k]%33u));ly=std::min(ly,std::uint32_t(tile.vertex_indices[k]/33u));
    }
    const std::uint32_t gx=sector.map_x*16u+lx/2u,gy=sector.map_y*16u+ly/2u;
    if(gx>=tiles_x||gy>=tiles_y||lx>30||ly>30)continue;
    // World corners (cx,cy) of the tile's four corner lanes.
    const auto uvs=terrain_tile_uvs(terrain,tile);
    std::array<std::uint32_t,4> corner{};bool complete=true;
    for(const unsigned k:{0u,2u,6u,8u}){
     const std::uint32_t cx=(tile.vertex_indices[k]%33u-lx)/2u,cy=(tile.vertex_indices[k]/33u-ly)/2u;
     if(cx>1||cy>1){complete=false;break;}
     corner[cy*2+cx]=unorm(uvs[k].u,uvs[k].v);
    }
    if(!complete)continue;
    const std::size_t g=std::size_t(gy)*tiles_x+gx;
    tiles[g]={corner[0],corner[1],corner[2],tile.material_index};
    used|=std::uint64_t{1}<<tile.material_index;
   }
  }
  return tiles;
}

TerrainTileBlendInput owned_terrain_tile_blend_input(const TerrainCpuAsset& terrain,std::uint32_t tiles_x,std::uint32_t tiles_y){
 TerrainTileBlendInput in;
 in.tiles_x=tiles_x;in.tiles_y=tiles_y;
 const std::uint32_t slots=in.slots=terrain.texture_count*terrain.tiles_per_texture;
 constexpr std::uint32_t layer=33u*33u;
 const std::size_t count=std::size_t(tiles_x)*tiles_y;
 std::vector<std::uint32_t> usage(slots,0u);
 in.slot.assign(count,0xffffu);in.lanes.assign(count,0u);in.wet.assign(count,0u);
 // Steep tiles: the blend owns their side projection. Mean corner height:
 // the cliff's up/down direction for its top and bottom materials. Tiles
 // with a corner under a liquid surface (rest layer height): never a
 // cliff's top or bottom material (no river bed on a face).
 in.steep.assign(count,false);in.heights.assign(count,0.f);in.underwater.assign(count,false);
 for(const auto& sector:terrain.sectors){
  if(sector.map_x>=terrain.sector_count_x||sector.map_y>=terrain.sector_count_y)continue;
  for(const auto& tile:sector.tiles){
   // Long and animated tiles keep their art (unknown corners).
   if(tile.transparent||tile.vertex_indices[0]>=layer||tile.tile_index>=slots||tile.animation_phases)continue;
   std::uint32_t lx=33,ly=33;
   for(const unsigned k:{0u,2u,6u,8u}){
    lx=std::min(lx,std::uint32_t(tile.vertex_indices[k]%33u));ly=std::min(ly,std::uint32_t(tile.vertex_indices[k]/33u));
   }
   const std::uint32_t gx=sector.map_x*16u+lx/2u,gy=sector.map_y*16u+ly/2u;
   if(gx>=tiles_x||gy>=tiles_y||lx>30||ly>30)continue;
   const std::size_t t=std::size_t(gy)*tiles_x+gx;
   ++usage[tile.tile_index];
   // Steep from the same per-vertex slope normals the shader's plane weights
   // use (shared vertices give both tiles the same answer): any lane vertex
   // past 38 degrees. A lane-rise test missed tiles whose shared vertices
   // lean past the walk limit, and the face treatment then stopped at a
   // straight tile edge.
   for(unsigned k=0;k<9;++k){
    const std::uint32_t packed=owned_terrain_slope_normal(terrain,sector,tile.vertex_indices[k]);
    const float nx=float(std::int16_t(packed&65535u))/32767.f,ny=float(std::int16_t(packed>>16))/32767.f;
    if(packed&&nx*nx+ny*ny>.3790f)in.steep[t]=true; // 1 - cos^2(38 degrees)
   }
   for(const unsigned k:{0u,2u,6u,8u}){
    const std::uint32_t base=tile.vertex_indices[k];
    if(terrain_base_liquid_object(terrain,sector,base)!=UINT16_MAX&&sector.vertices[base+layer].z>sector.vertices[base].z){
     in.underwater[t]=true;
     const std::uint32_t cx=(base%33u-lx)/2u,cy=(base/33u-ly)/2u;
     if(cx<2&&cy<2)in.wet[t]=std::uint8_t(in.wet[t]|(1u<<(cy*2+cx)));
    }
   }
   in.heights[t]=.25f*(sector.vertices[tile.vertex_indices[0]].z+sector.vertices[tile.vertex_indices[2]].z+
    sector.vertices[tile.vertex_indices[6]].z+sector.vertices[tile.vertex_indices[8]].z);
   const auto uvs=terrain_tile_uvs(terrain,tile);
   std::array<std::array<float,2>,4> uv{};bool complete=true;
   for(const unsigned k:{0u,2u,6u,8u}){
    const std::uint32_t cx=(tile.vertex_indices[k]%33u-lx)/2u,cy=(tile.vertex_indices[k]/33u-ly)/2u;
    if(cx>1||cy>1){complete=false;break;}
    uv[cy*2+cx]={uvs[k].u,uvs[k].v};
   }
   if(complete){
    // Signature corner of each world corner (terrain_tile_world_corners).
    const auto lane=terrain_tile_world_corners({0u,1u,2u,3u},uv);
    in.slot[t]=std::uint16_t(tile.tile_index);
    in.lanes[t]=std::uint8_t(lane[0]|(lane[1]<<2)|(lane[2]<<4)|(lane[3]<<6));
   }
  }
 }
 // Plain slots must be drawn on this map, so their page is in the contact atlas.
 in.usage=std::move(usage);in.usable.assign(slots,false);
 for(std::uint32_t s=0;s<slots;++s)
  in.usable[s]=in.usage[s]>0&&s/64u<contact_blend_max_materials&&!terrain.tile_animation_phases[s];
 return in;
}

namespace {
// Every tile's world-corner families from its slot and corner lanes.
std::vector<std::array<std::uint32_t,4>> tile_blend_corners(const TerrainTileBlendInput& in,const TerrainTileFamilies& families){
 std::vector<std::array<std::uint32_t,4>> corners(in.slot.size(),std::array<std::uint32_t,4>{});
 for(std::size_t t=0;t<corners.size();++t){
  if(in.slot[t]>=in.slots||in.slot[t]>=families.signatures.size())continue;
  const auto& g=families.signatures[in.slot[t]];
  for(unsigned k=0;k<4;++k)corners[t][k]=g[(in.lanes[t]>>(2*k))&3u];
 }
 return corners;
}

// Each family's material cell (its plain slot, else a borrowed one) and the
// liquid-bed families.
std::map<std::uint32_t,std::uint32_t> tile_blend_cells(const TerrainTileBlendInput& in,const TerrainTileFamilies& families,
    const std::vector<std::array<std::uint32_t,4>>& corners,std::set<std::uint32_t>& bed){
 const std::uint32_t slots=in.slots;
 // Placed tile corners per family, and those under a liquid at rest.
 std::map<std::uint32_t,std::uint32_t> placed,placed_wet;
 for(std::size_t t=0;t<corners.size();++t)
  for(unsigned k=0;k<4;++k){
   const std::uint32_t f=corners[t][k];
   if(f){++placed[f];if(in.wet[t]&(1u<<k))++placed_wet[f];}
  }
 // Liquid-bed families (terrain_tile_bed_name, kTerrainTileBedShare).
 for(const auto& [family,count]:placed){
  const auto name=families.names.find(family);
  if((name!=families.names.end()&&terrain_tile_bed_name(name->second))||float(placed_wet[family])>=kTerrainTileBedShare*float(count))
   bed.insert(family);
 }
 std::map<std::uint32_t,std::uint32_t> cells;
 // Each family's plain cell carries its material class (edge character).
 for(const auto& [family,slot]:terrain_tile_plain_slots(families.signatures,in.usage,in.usable)){
  const auto name=families.names.find(family);
  cells[family]=terrain_tile_blend_cell(slot,terrain_tile_material_class(name!=families.names.end()?name->second:std::string()),bed.count(family)!=0);
 }
 // Families without a drawn plain slot borrow a same-word, same-bed family's cell
 // (most drawn first) instead of leaving their tiles as hard-edged art.
 {
  std::map<std::uint32_t,std::uint32_t> drawn;
  for(std::uint32_t s=0;s<slots&&s<families.signatures.size();++s)for(const auto f:families.signatures[s])if(f)drawn[f]+=in.usage[s];
  std::vector<std::uint32_t> order;for(const auto& [f,n]:drawn)order.push_back(f);
  std::sort(order.begin(),order.end(),[&](std::uint32_t a,std::uint32_t b){return drawn[a]>drawn[b]||(drawn[a]==drawn[b]&&a<b);});
  terrain_tile_borrow_cells(families,order,cells,bed);
 }
 return cells;
}
}

std::vector<std::uint32_t> owned_terrain_tile_vertex_families(const TerrainTileBlendInput& in,const TerrainTileFamilies& families){
 return terrain_tile_vertex_families(in.tiles_x,in.tiles_y,tile_blend_corners(in,families));
}

std::set<std::string> owned_terrain_tile_blend_drawable(const TerrainTileBlendInput& in,const TerrainTileFamilies& families){
 std::set<std::uint32_t> bed;
 std::set<std::string> names;
 for(const auto& [family,cell]:tile_blend_cells(in,families,tile_blend_corners(in,families),bed)){
  const auto name=families.names.find(family);
  if(name!=families.names.end())names.insert(name->second);
 }
 return names;
}

std::vector<std::array<std::uint32_t,4>> owned_terrain_tile_blend_rows(const TerrainTileBlendInput& in,
    const TerrainTileFamilies& families,const std::vector<std::uint32_t>& edge_families,
    std::size_t& blended,std::size_t& plain_materials,std::size_t& bed_families,std::set<std::uint32_t>& unmatched){
 const auto corners=tile_blend_corners(in,families);
 std::set<std::uint32_t> bed;
 const auto cells=tile_blend_cells(in,families,corners,bed);
 // Shared map edge vertices take the family the edge gives them when this
 // map has a cell for it; otherwise they keep their own (reported).
 auto family=terrain_tile_vertex_families(in.tiles_x,in.tiles_y,corners);
 if(edge_families.size()==family.size())
  for(std::size_t i=0;i<family.size();++i){
   const std::uint32_t f=edge_families[i];
   if(!f||f==family[i])continue;
   if(cells.count(f))family[i]=f;else unmatched.insert(f);
  }
 auto rows=terrain_tile_blend_rows(in.tiles_x,in.tiles_y,corners,family,cells,in.steep,in.heights,in.underwater,families.rock_hard_only);
 blended=0;plain_materials=cells.size();bed_families=bed.size();
 for(const auto& row:rows)blended+=(row[2]&5u)==5u; // known and near a transition
 return rows;
}

// Map file atlas bindings (namespace, slot count), cached by path, size and
// write time (a changed file gets a new key). Only these small headers are
// kept; base tiles are re-read per load (32-bit address space).
namespace {
bool load_cancelled(const std::atomic<bool>* cancel) noexcept {return cancel&&cancel->load(std::memory_order_relaxed);}
struct MapFileCache {
 std::mutex lock;
 std::map<std::wstring,TerrainTileMapFile> headers;
};
MapFileCache& map_file_cache(){static MapFileCache cache;return cache;}

// Adds to `runs` the base tiles of every *.mpr in the loose map roots (first
// root wins per file name, as the game resolves maps) bound to `space` with
// `slots` atlas slots, except the map itself (`own`, already in memory).
// Returns the number of maps added.
std::size_t pool_map_files(const ResourceStoreConfig& mounts,const std::string& space,unsigned slots,
    const std::string& own,const std::vector<bool>& skip,TerrainTileVertexRuns& runs,const std::atomic<bool>* cancel){
 std::size_t added=0;
 std::vector<TerrainTilePlacement> tiles;
 std::map<std::wstring,bool> taken;
 auto& cache=map_file_cache();
 for(const auto& root:mounts.loose_roots){
  WIN32_FIND_DATAW found{};
  const HANDLE find=FindFirstFileW((root+L"\\*.mpr").c_str(),&found);
  if(find==INVALID_HANDLE_VALUE)continue;
  do{
   if(load_cancelled(cancel))break;
   if(found.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)continue;
   std::wstring name=found.cFileName;
   for(auto& c:name)if(c>=L'A'&&c<=L'Z')c=wchar_t(c+32);
   if(!taken.emplace(name,true).second)continue;
   std::string narrow;for(const auto c:name)narrow+=c<128?char(c):'?';
   if(narrow==own)continue;
   const std::wstring path=root+L"\\"+found.cFileName;
   const std::wstring key=path+L"|"+std::to_wstring((std::uint64_t(found.nFileSizeHigh)<<32)|found.nFileSizeLow)+L"|"+
    std::to_wstring((std::uint64_t(found.ftLastWriteTime.dwHighDateTime)<<32)|found.ftLastWriteTime.dwLowDateTime);
   TerrainTileMapFile header;bool have=false;
   {std::lock_guard<std::mutex> guard(cache.lock);const auto h=cache.headers.find(key);if(h!=cache.headers.end()){header=h->second;have=true;}}
   if(!have){
    if(!terrain_tile_read_map_file(path,header,nullptr))header={};
    std::lock_guard<std::mutex> guard(cache.lock);cache.headers[key]=header;
   }
   if(header.space!=space||header.slots!=slots)continue;
   TerrainTileMapFile check;
   if(!terrain_tile_read_map_file(path,check,&tiles)||check.space!=space||check.slots!=slots||tiles.empty())continue;
   runs.add_map(tiles,slots,skip);++added;
  }while(FindNextFileW(find,&found));
  FindClose(find);
 }
 return added;
}

// Content key of every atlas slot: a hash of the level-0 bytes of its cell
// (exact block or pixel rows, with the format and cell size), so a slot
// keeps its key when a repacked atlas moves it. Keys are compared only
// within one load, so their values never need to persist. Two 32-bit lanes
// over 4-byte words (FNV-1a and a MurmurHash3 body): the per-byte 64-bit
// FNV-1a called _allmul for every byte on x86. Slot pos of a page is cell
// column pos%side, row side-1-pos/side from the top of the image (the blend
// shader's cell rule). Evidence: the zone19 atlases of 2026-09-28 and
// 2026-10-08 match slot for slot under this rule (every repacked cliff slot
// pair byte-identical), not under the top-down row order. False when a page
// is missing or does not divide into cells.
bool terrain_atlas_slot_keys(const ResourceStore& store,const std::string& space,unsigned pages,unsigned side,
    std::vector<std::uint64_t>& keys,const std::atomic<bool>* cancel){
 keys.assign(std::size_t(pages)*side*side,0);
 if(!pages||!side)return false;
 struct Key{
  std::uint32_t a=2166136261u,b=0x9747b28cu;std::size_t length=0;
  static std::uint32_t rotl(std::uint32_t v,int r){return (v<<r)|(v>>(32-r));}
  void word(std::uint32_t w){
   a=(a^w)*16777619u;
   std::uint32_t k=rotl(w*0xcc9e2d51u,15)*0x1b873593u;
   b=rotl(b^k,13)*5u+0xe6546b64u;length+=4;
  }
  void bytes(const std::uint8_t* p,std::size_t n){
   for(;n>=4;p+=4,n-=4){std::uint32_t w;std::memcpy(&w,p,4);word(w);}
   if(n){std::uint32_t w=0;std::memcpy(&w,p,n);word(w^(std::uint32_t(n)<<24));}
  }
  std::uint64_t finish() const{
   std::uint32_t h=b^std::uint32_t(length);
   h^=h>>16;h*=0x85ebca6bu;h^=h>>13;h*=0xc2b2ae35u;h^=h>>16;
   return (std::uint64_t(h)<<32)|a;
  }
 };
 for(unsigned page=0;page<pages;++page){
  if(load_cancelled(cancel))return false;
  char suffix[16];std::snprintf(suffix,sizeof(suffix),"%03u.mmp",page);
  ResourceBlob blob;ResourceFailure failure;TextureCpuAsset texture;
  if(!store.resolve(space+suffix,blob,failure)||!parse_texture_asset(blob,texture,failure)||texture.mips.empty()||!texture.source)return false;
  const auto& mip=texture.mips[0];
  const bool blocks=texture.format==MmpFormat::dxt1||texture.format==MmpFormat::dxt3||texture.format==MmpFormat::dxt5;
  const unsigned unit=blocks?4u:1u;
  if(!mip.width||!mip.height||mip.width%side||mip.height%side||(mip.width/side)%unit||(mip.height/side)%unit)return false;
  const std::size_t rows=mip.height/unit,row_bytes=mip.bytes/rows,cell_rows=(mip.height/side)/unit,cell_bytes=row_bytes/side;
  if(rows*row_bytes!=mip.bytes||cell_bytes*side!=row_bytes)return false;
  const std::uint8_t* base=texture.source->data()+mip.byte_offset;
  for(unsigned pos=0;pos<side*side;++pos){
   Key h;
   const std::uint64_t head[2]={std::uint64_t(texture.format_signature),std::uint64_t(mip.width/side)};
   h.bytes(reinterpret_cast<const std::uint8_t*>(head),sizeof(head));
   const std::size_t r=side-1-pos/side,c=pos%side;
   for(std::size_t y=0;y<cell_rows;++y)h.bytes(base+(r*cell_rows+y)*row_bytes+c*cell_bytes,cell_bytes);
   keys[std::size_t(page)*side*side+pos]=h.finish();
  }
 }
 return true;
}

// The map's atlas without a profile of its own name: adopt, slot by slot,
// the families of shipped profiles whose atlas slots carry byte-identical
// level-0 art (a re-authored map with a repacked or partly changed atlas
// keeps the editor's families for every unchanged slot). Candidates are the
// profiles of the same map name (namespace before its 12-digit atlas key);
// the one matching most open slots is taken first, then the next fills only
// slots still open. adopted[s] marks the slots taken; text names each
// profile used.
std::size_t adopt_profile_families(const ResourceStore& store,const TerrainTileMapInput& input,
    TerrainTileFamilies& out,std::vector<bool>& adopted,std::string& text,const std::atomic<bool>* cancel){
 const unsigned slots=input.slots;
 out.signatures.assign(slots,{});out.names.clear();adopted.assign(slots,false);
 const auto map_name=[](const std::string& space){
  const auto cut=space.find_last_of('_');
  if(cut==std::string::npos||space.size()-cut-1!=12)return space;
  for(std::size_t i=cut+1;i<space.size();++i)if(!std::isxdigit(static_cast<unsigned char>(space[i])))return space;
  return space.substr(0,cut);
 };
 std::vector<std::uint64_t> own;
 if(!input.tiles_per_side||input.pages*input.tiles_per_side*input.tiles_per_side!=slots||
    !terrain_atlas_slot_keys(store,input.space,input.pages,input.tiles_per_side,own,cancel))return 0;
 struct Candidate{std::string space;unsigned slots,pages;std::string json;};
 std::vector<Candidate> candidates;
 const std::string prefix="ei_atlas_profile_",own_name=map_name(input.space);
 for(const auto& member:store.archive_entry_names()){
  if(load_cancelled(cancel))return 0;
  if(member.compare(0,prefix.size(),prefix)!=0||member.size()<prefix.size()+5||member.compare(member.size()-5,5,".json")!=0)continue;
  ResourceBlob blob;ResourceFailure failure;
  if(!store.resolve(member,blob,failure)||!blob.bytes)continue;
  try{
   Candidate c{{},0,0,std::string(blob.bytes->begin(),blob.bytes->end())};
   const auto root=small_json::Reader(c.json).root();
   c.space=root.field("mprNamespace").str();
   for(char& ch:c.space)if(ch>='A'&&ch<='Z')ch=char(ch+32);
   c.slots=unsigned(root.field("atlasSlotCount").num());c.pages=unsigned(root.field("textureCount").num());
   if(c.space==input.space||map_name(c.space)!=own_name||!c.pages||c.slots%c.pages)continue;
   candidates.push_back(std::move(c));
  }catch(...){}
 }
 std::sort(candidates.begin(),candidates.end(),[](const Candidate& a,const Candidate& b){return a.space<b.space;});
 // Each candidate's assigned slots by content key.
 struct Source{const Candidate* c;TerrainTileSignatures signatures;std::map<std::uint32_t,std::string> names;
  std::unordered_map<std::uint64_t,unsigned> by_key;};
 std::vector<Source> sources;
 for(const auto& c:candidates){
  const unsigned per_page=c.slots/c.pages;unsigned side=0;
  while((side+1)*(side+1)<=per_page)++side;
  Source source{&c,{},{},{}};std::vector<std::uint64_t> keys;
  if(side*side!=per_page||!terrain_tile_signatures_from_profile(c.json,c.space,c.slots,source.signatures,&source.names)||
     !terrain_atlas_slot_keys(store,c.space,c.pages,side,keys,cancel))continue;
  for(unsigned s=0;s<c.slots&&s<keys.size();++s){
   const auto& g=source.signatures[s];
   if(g[0]|g[1]|g[2]|g[3])source.by_key.emplace(keys[s],s);
  }
  sources.push_back(std::move(source));
 }
 std::size_t total=0;
 std::vector<bool> used(sources.size(),false);
 while(total<slots){
  std::size_t best=sources.size(),best_count=0;
  for(std::size_t i=0;i<sources.size();++i){
   if(used[i])continue;
   std::size_t count=0;
   for(unsigned s=0;s<slots;++s)count+=!adopted[s]&&sources[i].by_key.count(own[s]);
   if(count>best_count){best=i;best_count=count;}
  }
  if(best==sources.size())break;
  used[best]=true;
  const auto& source=sources[best];
  for(unsigned s=0;s<slots;++s){
   if(adopted[s])continue;
   const auto hit=source.by_key.find(own[s]);
   if(hit==source.by_key.end())continue;
   out.signatures[s]=source.signatures[hit->second];adopted[s]=true;
  }
  for(const auto& [id,name]:source.names)out.names.emplace(id,name);
  total+=best_count;
  text+=(text.empty()?"":", ")+source.c->space+" "+std::to_string(best_count);
 }
 return total;
}
}

TerrainTileMapInput terrain_tile_map_input(const TerrainCpuAsset& terrain){
 TerrainTileMapInput input;
 input.space=terrain_tile_family_space(terrain);
 input.slots=terrain.texture_count*terrain.tiles_per_texture;
 input.pages=terrain.texture_count;input.tiles_per_side=terrain.tiles_per_texture_side;
 std::string own=terrain.project_resource;
 const auto cut=own.find_last_of("\\/");if(cut!=std::string::npos)own=own.substr(cut+1);
 for(char& c:own)if(c>='A'&&c<='Z')c=char(c+32);
 input.own_file=own;
 input.types=terrain.tile_types;
 input.skip.assign(input.slots,false);
 for(std::uint32_t s=0;s<input.slots&&s<terrain.tile_animation_phases.size();++s)input.skip[s]=terrain.tile_animation_phases[s]!=0;
 constexpr std::uint32_t layer=33u*33u;
 for(const auto& sector:terrain.sectors)
  for(const auto& tile:sector.tiles){
   const std::uint32_t first=tile.vertex_indices[0];
   if(first>=layer||tile.tile_index>=input.slots)continue;
   input.own.push_back({std::int32_t(sector.map_x*16u+(first%33u)/2u),std::int32_t(sector.map_y*16u+(first/33u)/2u),
    tile.tile_index,std::uint8_t(tile.rotation&3u)});
  }
 return input;
}

// Terrain material blend: the map's tile families, off the render thread.
// 1. The editor profile shipped with the map's resources (its member name is
//    the editor's: the lower-case namespace and the first 16 hex digits of its
//    SHA-256).
// 2. The editor's built-in preset of the namespace and slot count, when the
//    maps of that atlas fit it (vertex consistency: vanilla and repainted
//    packs keep the shipped slot layout; a different layout does not fit).
// 3. Families from the placements of every map of the atlas
//    (terrain_tile_map_families.h); unnamed groups stay unassigned.
// One log line per atlas names the source and its corner counts.
TerrainTileFamilies load_terrain_tile_families(ResourceStoreConfig mounts,TerrainTileMapInput input,
    const std::atomic<bool>* cancel) noexcept try{
 TerrainTileFamilies result;
 const std::string space=input.space;const unsigned slots=input.slots;
 const std::string atlas=space+"/"+std::to_string(slots);
 const auto assigned=[](const TerrainTileFamilies& f){std::size_t n=0;for(const auto& g:f.signatures)for(const auto c:g)n+=c!=0;return n;};
 const auto counts=[&](int source,std::size_t count){
  char text[160];std::snprintf(text,sizeof(text)," | corners profile=%zu preset=%zu maps=%zu unassigned=%zu",
   source==0?count:0,source==1?count:0,source==2?count:0,std::size_t(slots)*4u-count);
  return std::string(text);
 };
 std::array<unsigned char,32> digest{};
 BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
 std::string hashed_space=space;
 bool hashed=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)==0&&
  BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)==0&&
  BCryptHashData(hash,reinterpret_cast<PUCHAR>(hashed_space.data()),ULONG(hashed_space.size()),0)==0&&
  BCryptFinishHash(hash,digest.data(),ULONG(digest.size()),0)==0;
 if(hash)BCryptDestroyHash(hash);
 if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);
 if(hashed){
  std::string member="ei_atlas_profile_"+space+"_";
  for(unsigned i=0;i<8;++i){member+="0123456789abcdef"[digest[i]>>4];member+="0123456789abcdef"[digest[i]&15];}
  member+=".json";
  ResourceStore store;ResourceFailure failure;ResourceBlob blob;
  if(store.open(mounts,failure)&&store.resolve(member,blob,failure)&&blob.bytes&&
     terrain_tile_signatures_from_profile(std::string(blob.bytes->begin(),blob.bytes->end()),space,slots,result.signatures,&result.names)){
   result.source="tile families "+atlas+": profile "+member+counts(0,assigned(result));return result;
  }
 }
 // Corner nodes are 16-bit (slot*4+corner); tile ids are 14-bit anyway.
 if(!slots||slots>16384u){result.signatures.clear();result.source="no tile families for "+atlas;return result;}
 const auto start=std::chrono::steady_clock::now();
 // Families adopted by slot art from other shipped profiles of this map.
 TerrainTileFamilies adopted_families;std::vector<bool> adopted;std::string adopted_text;std::size_t adopted_slots=0;
 {ResourceStore store;ResourceFailure failure;
  if(store.open(mounts,failure))adopted_slots=adopt_profile_families(store,input,adopted_families,adopted,adopted_text,cancel);}
 const auto cancelled=[&]{TerrainTileFamilies none;none.source="tile family load cancelled";return none;};
 if(load_cancelled(cancel))return cancelled();
 if(adopted_slots==slots){
  const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
  char text[96];std::snprintf(text,sizeof(text),"adopted %zu of %u slots by art (%.0f ms): ",adopted_slots,slots,ms);
  result=std::move(adopted_families);
  result.source="tile families "+atlas+": "+text+adopted_text+counts(0,assigned(result));return result;
 }
 TerrainTileVertexRuns runs;
 runs.add_map(input.own,slots,input.skip);
 const std::size_t maps=pool_map_files(mounts,space,slots,input.own_file,input.skip,runs,cancel)+1;
 if(load_cancelled(cancel))return cancelled();
 std::string rejected;
 for(const auto& preset:kTerrainTilePresets){
  std::string name=preset.name;
  for(char& c:name)if(c>='A'&&c<='Z')c=char(c+32);
  if(name!=space||preset.slots!=slots||!terrain_tile_signatures_from_preset(preset.signatures,slots,result.signatures))continue;
  std::size_t tested=0;
  const double fit=terrain_tile_vertex_consistency(runs,result.signatures,tested);
  char text[160];std::snprintf(text,sizeof(text),"(vertex fit %.1f%% of %zu vertices, %zu maps)",100.*fit,tested,maps);
  if(fit>=kTerrainTilePresetFit||tested<64){
   terrain_tile_names_from_preset(preset.families,result.names);
   result.source="tile families "+atlas+": editor preset "+preset.name+" "+text+counts(1,assigned(result));return result;
  }
  rejected=std::string("preset ")+preset.name+" rejected "+text+"; ";
  break;
 }
 auto inferred=terrain_tile_map_families(runs,slots,input.types,input.skip);
 const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
 char text[200];std::snprintf(text,sizeof(text),"map placements of %zu maps (%zu corners placed, %zu named families, %.0f ms)",
  maps,inferred.placed,inferred.families.names.size(),ms);
 result=std::move(inferred.families);
 result.source="tile families "+atlas+": "+rejected+text+counts(2,inferred.named);
 if(adopted_slots){
  // Adopted slots keep the editor's families; placements fill the rest
  // (inferred ids count from 1, editor ids are hashes).
  for(unsigned s=0;s<slots;++s)if(adopted[s])result.signatures[s]=adopted_families.signatures[s];
  for(const auto& [id,name]:adopted_families.names)result.names.emplace(id,name);
  result.source+=" | adopted "+std::to_string(adopted_slots)+" of "+std::to_string(slots)+" slots by art: "+adopted_text;
 }
 return result;
}catch(...){TerrainTileFamilies failed;failed.source="tile family load failed";return failed;}

std::string terrain_tile_family_space(const TerrainCpuAsset& terrain){
 const auto& name=terrain.map_header_resource;
 std::string space=name.size()>3?name.substr(0,name.size()-3):name;
 for(char& c:space)if(c>='A'&&c<='Z')c=char(c+32);
 return space;
}

} // namespace owned
