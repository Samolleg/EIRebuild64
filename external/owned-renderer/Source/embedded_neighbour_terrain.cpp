// Embedded candidate-56 neighbour terrain. These algorithms are the non-retail
// portion of engine_bridge.cpp (7f79f6e, source_terrain_view and neighbour
// prepare/upload). Native builds keep their existing engine_bridge owners.
// The host supplies only bridge_neighbour_world_transform; decoding stays here.
#include "neighbour_maps.h"
#include "owned_terrain_decode.h"
#include "native_instance_flags.h"
#include <cfloat>
#include <climits>

namespace owned {
namespace {
bool decode_neighbour_terrain_on_worker(void* opaque, NativeGeometryDecodeTarget& target) {
    const auto* context = static_cast<const OwnedTerrainSourceDecodeContext*>(opaque);
    return owned_terrain_decode_valid(context, target) && decode_owned_terrain_tiles(*context, target);
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
    view.decode_context = &context;
    view.decode = &decode_neighbour_terrain_on_worker;
    return true;
}


} // namespace
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

bool bridge_upload_neighbour_terrain(const NeighbourMapUpload& upload,
    const std::weak_ptr<const void>& owner,const std::array<float,16>& world_to_clip,
    NativeGeometrySubmitFn submit,void* opaque,std::uint32_t& part,const char*& failure) {
    failure="terrain.input";
    if(!upload.map || !upload.map->terrain || !submit || owner.expired())return false;
    const auto& source=*upload.map->terrain;
    // This neighbour's contact maps (its tile grid with the material blend
    // rows and its atlas copy) bind to its terrain draws, as to its
    // decorations: key first part + 1 (Renderer::add_ground_cover_neighbour).
    const std::uint32_t contact_map=part+1;
    OwnedTerrainFramePacket packet;packet.source=upload.map->terrain;
    packet.inputs.source_generation=upload.epoch;packet.inputs.source_revision=1;
    packet.inputs.object_to_clip=world_to_clip;
    const auto geometry=frozen_owned_terrain_geometry(source);
    OwnedTerrainDynamicSector dynamic;dynamic.captured=true;
    dynamic.animation.tiles_per_texture_side=source.tiles_per_texture_side;
    std::array<float,16> placement;
    failure="terrain.world_transform";
    if(!bridge_neighbour_world_transform(placement))return false;
    const std::array<float,16> offset{1,0,0,float(upload.selection.x),0,1,0,float(upload.selection.y),0,0,1,0,0,0,0,1};
    placement=retained_multiply_matrix(placement,offset);
    for(std::uint32_t sector=0;sector<source.sectors.size();++sector) {
        const auto& data=source.sectors[sector];
        for(std::uint32_t material=0;material<data.materials.size();++material) {
            OwnedTerrainCommand command;command.source_sector=sector;command.material=static_cast<std::uint16_t>(material);
            // Full detail: shared map edges agree only at every authored
            // vertex. The coarse level kept every second edge vertex and
            // opened cracks of up to 6 m against the adjoining map's detail.
            // Fog culling bounds the uploaded band, so the cost stays local.
            command.lod=0;command.subpart_mask=15;
            failure="terrain.descriptor_counts";
            if(!select_owned_terrain_descriptor_counts(data.materials[material],0,false,false,15,
                command.tile_count,command.index_count))return false;
            if(!command.tile_count)continue;
            command.vertex_count=command.tile_count*terrain_vertices_per_tile(0);
            const auto atlas=material%(source.texture_count+2u);
            failure="terrain.atlas_page_missing";
            if(atlas>=source.pages.size() || !source.pages[atlas].texture || source.pages[atlas].mips.empty())return false;
            OwnedTerrainSourceDecodeContext decode;
            NativeGeometryView view;
            failure="terrain.view";
            if(!source_terrain_view(packet,command,dynamic,decode,view))return false;
            decode.geometry=&geometry;
            view.world.enabled=true;view.world.identity=&data.identity;view.world.owner=&owner;
            view.world.record_revision=1;view.world.part=material;
            view.world.object_to_world=view.lighting.object_to_world=placement;
            view.world.world_to_clip=world_to_clip;
            NativeWorldBounds bounds;
            failure="terrain.bounds";
            if(!owned_terrain_command_bounds(source,command,geometry,bounds))return false;
            view.bounds_minimum.fill(FLT_MAX);view.bounds_maximum.fill(-FLT_MAX);
            for(unsigned corner=0;corner<8;++corner)for(unsigned axis=0;axis<3;++axis) {
                float value=placement[axis*4+3];
                for(unsigned n=0;n<3;++n)value+=placement[axis*4+n]*((corner&(1u<<n))?bounds.maximum[n]:bounds.minimum[n]);
                view.bounds_minimum[axis]=(std::min)(view.bounds_minimum[axis],value);
                view.bounds_maximum[axis]=(std::max)(view.bounds_maximum[axis],value);
            }
            // Wholly inside full neighbour fog: nothing would be visible.
            if(neighbour_fog_hides({view.bounds_minimum[0],view.bounds_minimum[1],
                view.bounds_maximum[0],view.bounds_maximum[1]},upload.fog_cull)) {++upload.fog_culled;continue;}
            NativePreparedMeshHandle mesh;NativePreparedMaterialHandle skin;
            view.prepared_mesh=&mesh;view.prepared_material=&skin;
            view.prebuilt_mesh=upload.prebuilt_terrain_mesh(sector,material);
            view.prepared_identity={INT_MIN,upload.epoch,upload.epoch,1,part++,0,1,true,true};
            const auto& page=source.pages[atlas];
            view.owned_texture.identity=page.texture->identity.container_incarnation;
            view.owned_texture.generation=1;view.owned_texture.resource=&page.texture->identity;
            view.owned_texture.value_source=1;
            view.owned_texture.format=static_cast<NativeOwnedTextureFormat>(page.texture->format);
            view.owned_texture.mips=page.mips.data();view.owned_texture.mip_count=static_cast<std::uint32_t>(page.mips.size());
            // Frozen decoration layers use their authored initial topology. The
            // current world light and atmosphere remain frame-owned GPU values.
            view.flags|=native_neighbour_flag;view.contribution={true,true,0};
            view.contact_map=contact_map;
            // The piece's raw edges (fog_edges); the shader insets them and
            // lets the band give way where other rendered pieces adjoin.
            view.terrain.fog_min[0]=placement[3]+float(upload.fog_edges.x0-upload.selection.x);
            view.terrain.fog_min[1]=placement[7]+float(upload.fog_edges.y0-upload.selection.y);
            view.terrain.fog_max[0]=placement[3]+float(upload.fog_edges.x1-upload.selection.x);
            view.terrain.fog_max[1]=placement[7]+float(upload.fog_edges.y1-upload.selection.y);
            view.ordering=material<source.texture_count+2u?NativeOrdering::reorderable:NativeOrdering::preserve;
            NativeGeometryResult result;
            failure="terrain.submit";
            if(!submit(opaque,view,result))return false;
        }
    }
    failure=nullptr;
    return true;
}


} // namespace owned
