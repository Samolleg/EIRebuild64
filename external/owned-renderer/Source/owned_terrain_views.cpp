// Terrain command views over an immutable decoded terrain: the geometry view a
// terrain command submits, and the neighbour terrain's commands for the
// stream's prebuilt pages. No game memory, no renderer state; shared by the
// renderer DLL and the x64 render host.
#include "owned_terrain_views.h"

#include <cfloat>

namespace owned {

namespace {
// The terrain decode without the bridge sample counters (render-thread state).
bool decode_neighbour_terrain_on_worker(void* opaque,NativeGeometryDecodeTarget& target) {
    const auto* context=static_cast<const OwnedTerrainSourceDecodeContext*>(opaque);
    return owned_terrain_decode_valid(context,target) && decode_owned_terrain_tiles(*context,target);
}
}

bool source_terrain_view(const OwnedTerrainFramePacket& packet,
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
    view.decode = &decode_neighbour_terrain_on_worker;
    return true;
}


// The commands bridge_upload_neighbour_terrain submits (full detail, every
// subpart, atlas page present), in sector then material order, without its
// placement, fog culling or frame values: those belong to the instance, not to
// the mesh. Runs on the neighbour stream's worker.
void bridge_prepare_neighbour_terrain(const NeighbourMapUpload& upload,
    void (*emit)(void*,std::uint32_t,std::uint32_t,const NativeGeometryView&),void* context) {
    if(!upload.map || !upload.map->terrain || !emit)return;
    const auto& source=*upload.map->terrain;
    OwnedTerrainFramePacket packet;packet.source=upload.map->terrain;
    packet.inputs.source_generation=upload.epoch;packet.inputs.source_revision=1;
    const auto geometry=frozen_owned_terrain_geometry(source);
    OwnedTerrainDynamicSector dynamic;dynamic.captured=true;
    dynamic.animation.tiles_per_texture_side=source.tiles_per_texture_side;
    for(std::uint32_t sector=0;sector<source.sectors.size();++sector) {
        const auto& data=source.sectors[sector];
        for(std::uint32_t material=0;material<data.materials.size();++material) {
            OwnedTerrainCommand command;command.source_sector=sector;command.material=static_cast<std::uint16_t>(material);
            command.lod=0;command.subpart_mask=15;
            if(!select_owned_terrain_descriptor_counts(data.materials[material],0,false,false,15,
                command.tile_count,command.index_count) || !command.tile_count)continue;
            command.vertex_count=command.tile_count*terrain_vertices_per_tile(0);
            const auto atlas=material%(source.texture_count+2u);
            if(atlas>=source.pages.size() || !source.pages[atlas].texture || source.pages[atlas].mips.empty())continue;
            OwnedTerrainSourceDecodeContext decode;
            NativeGeometryView view;
            if(!source_terrain_view(packet,command,dynamic,decode,view))continue;
            decode.geometry=&geometry;
            view.decode=&decode_neighbour_terrain_on_worker;
            view.world.enabled=true;view.world.identity=&data.identity;view.world.part=material;
            emit(context,sector,material,view);
        }
    }
}

} // namespace owned
