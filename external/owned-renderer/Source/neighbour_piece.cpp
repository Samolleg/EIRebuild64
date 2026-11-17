// Condensing a decoded neighbour into its piece (neighbour_piece.h). Runs on
// the neighbour stream's worker after the weld and the prebuild, in the
// renderer DLL or the x64 render host; no renderer state, no game memory.
#include "neighbour_piece.h"
#include "environment_material.h"

#include "d3d12_frame.h"

#include "native_instance_flags.h"
#include "astral_rift.h"
#include "owned_terrain_views.h"

#include <cfloat>
#include <map>
#include <unordered_map>

namespace owned {

using Microsoft::WRL::ComPtr;

NativeOwnedTextureView neighbour_piece_texture_view(const NeighbourPieceTexture& texture,
    std::vector<NativeOwnedTextureMip>& mips) {
    NativeOwnedTextureView view{};
    view.identity = texture.view_identity; view.generation = 1;
    view.resource = &texture.identity; view.value_source = 1;
    view.format = texture.format; view.mip_count = texture.mip_count;
    view.opaque_mips = texture.opaque_mips;
    mips.clear();
    if (!texture.mips.empty()) {
        mips.reserve(texture.mips.size());
        for (const auto& mip : texture.mips)
            mips.push_back({texture.bytes.data() + mip.offset, mip.width, mip.height, mip.bytes});
        view.mips = mips.data();
    }
    return view;
}

namespace {

bool block_compressed(NativeOwnedTextureFormat format) noexcept {
    return format == NativeOwnedTextureFormat::dxt1 || format == NativeOwnedTextureFormat::dxt3 ||
        format == NativeOwnedTextureFormat::dxt5;
}

struct PieceBuilder {
    NeighbourPiece& piece;
    std::unordered_map<std::string, std::uint32_t> identity_index, texture_index;
    std::map<const FigureCpuAsset*, std::uint32_t> figure_index;
    std::map<const NativePrebuiltMesh*, std::uint32_t> mesh_index;
    std::unordered_map<std::string, std::uint32_t> impostor_index; // NeighbourPieceImpostor::key

    std::uint32_t identity(const ResourceIdentity& value) {
        auto key = value.key();
        const auto found = identity_index.find(key);
        if (found != identity_index.end()) return found->second;
        const auto index = static_cast<std::uint32_t>(piece.identities.size());
        piece.identities.push_back(value);
        identity_index.emplace(std::move(key), index);
        return index;
    }
    // A texture by its cache identity; mips are copied only for formats the
    // stream does not create (the render thread uploads those as before).
    std::uint32_t texture(const ResourceIdentity& value, std::uint64_t view_identity,
        NativeOwnedTextureFormat format, const NativeOwnedTextureMip* mips, std::uint32_t mip_count,
        std::uint32_t opaque_mips) {
        auto key = value.key() + "|" + std::to_string(view_identity);
        const auto found = texture_index.find(key);
        if (found != texture_index.end()) return found->second;
        NeighbourPieceTexture made;
        made.identity = value; made.view_identity = view_identity;
        made.format = format; made.mip_count = mip_count;
        // Only the levels the piece binds; anything past them is unknown.
        made.opaque_mips = mip_count >= 32 ? opaque_mips : opaque_mips & ((std::uint32_t{1} << mip_count) - 1u);
        if (!block_compressed(format) && mips) {
            std::size_t total = 0;
            for (std::uint32_t i = 0; i < mip_count; ++i) total += mips[i].bytes;
            made.bytes.reserve(total);
            for (std::uint32_t i = 0; i < mip_count; ++i) {
                made.mips.push_back({static_cast<std::uint32_t>(made.bytes.size()), mips[i].width, mips[i].height, mips[i].bytes});
                made.bytes.insert(made.bytes.end(), mips[i].data, mips[i].data + mips[i].bytes);
            }
        }
        const auto index = static_cast<std::uint32_t>(piece.textures.size());
        piece.textures.push_back(std::move(made));
        texture_index.emplace(std::move(key), index);
        return index;
    }
    std::uint32_t figure(const FigureCpuAsset& value) {
        const auto found = figure_index.find(&value);
        if (found != figure_index.end()) return found->second;
        NeighbourPieceFigure made;
        if(value.user_data.size()==20&&!std::memcmp(value.user_data.data(),"ENV8",4)){made.shape_count=8;made.shape_normals=1;}
        made.identity = identity(value.identity);
        made.position_groups = value.position_groups; made.normal_groups = value.normal_groups;
        made.texture_coordinate_count = static_cast<std::uint32_t>(value.texture_coordinates.size());
        made.component_count = static_cast<std::uint32_t>(value.components.size());
        made.index_count = static_cast<std::uint32_t>(value.indices.size());
        const auto index = static_cast<std::uint32_t>(piece.figures.size());
        piece.figures.push_back(made);
        figure_index.emplace(&value, index);
        return index;
    }
    // One entry per stream mesh: draws sharing a figure at one shape share it.
    std::uint32_t mesh(const NativePrebuiltMesh& value) {
        const auto found = mesh_index.find(&value);
        if (found != mesh_index.end()) return found->second;
        const auto index = static_cast<std::uint32_t>(piece.meshes.size());
        piece.meshes.push_back(value);
        mesh_index.emplace(&value, index);
        return index;
    }
};

// The terrain commands bridge-side upload submitted, in the same order and
// with the same rejections; placement, fog culling and frame values are the
// renderer's at adoption.
void piece_terrain(PieceBuilder& builder, const NeighbourMapUpload& upload) {
    auto& piece = builder.piece;
    const auto fail = [&](const char* step) { piece.terrain_failure = step; piece.terrain.clear(); };
    if (!upload.map || !upload.map->terrain) return fail("terrain.input");
    const auto& source = *upload.map->terrain;
    OwnedTerrainFramePacket packet; packet.source = upload.map->terrain;
    packet.inputs.source_generation = upload.epoch; packet.inputs.source_revision = 1;
    const auto geometry = frozen_owned_terrain_geometry(source);
    OwnedTerrainDynamicSector dynamic; dynamic.captured = true;
    dynamic.animation.tiles_per_texture_side = source.tiles_per_texture_side;
    for (std::uint32_t sector = 0; sector < source.sectors.size(); ++sector) {
        const auto& data = source.sectors[sector];
        for (std::uint32_t material = 0; material < data.materials.size(); ++material) {
            OwnedTerrainCommand command; command.source_sector = sector; command.material = static_cast<std::uint16_t>(material);
            // Full detail: shared map edges agree only at every authored
            // vertex; fog culling bounds the uploaded band.
            command.lod = 0; command.subpart_mask = 15;
            if (!select_owned_terrain_descriptor_counts(data.materials[material], 0, false, false, 15,
                    command.tile_count, command.index_count)) return fail("terrain.descriptor_counts");
            if (!command.tile_count) continue;
            command.vertex_count = command.tile_count * terrain_vertices_per_tile(0);
            const auto atlas = material % (source.texture_count + 2u);
            if (atlas >= source.pages.size() || !source.pages[atlas].texture || source.pages[atlas].mips.empty())
                return fail("terrain.atlas_page_missing");
            OwnedTerrainSourceDecodeContext decode;
            NativeGeometryView view;
            if (!source_terrain_view(packet, command, dynamic, decode, view)) return fail("terrain.view");
            NativeWorldBounds bounds;
            if (!owned_terrain_command_bounds(source, command, geometry, bounds)) return fail("terrain.bounds");
            const auto* mesh = upload.prebuilt_terrain_mesh(sector, material);
            if (!mesh || !mesh->bytes) return fail("terrain.prebuilt_missing");
            const auto& page = source.pages[atlas];
            NeighbourPieceTerrainDraw draw;
            draw.sector = sector; draw.material = material;
            draw.identity = builder.identity(data.identity);
            draw.texture = builder.texture(page.texture->identity, page.texture->identity.container_incarnation,
                static_cast<NativeOwnedTextureFormat>(page.texture->format), page.mips.data(),
                static_cast<std::uint32_t>(page.mips.size()), page.texture->opaque_mips);
            draw.mesh = builder.mesh(*mesh);
            draw.position_groups = view.position_groups; draw.normal_groups = view.normal_groups;
            draw.texture_coordinate_count = view.texture_coordinate_count;
            draw.component_count = view.component_count; draw.index_count = view.index_count;
            draw.terrain_page_count = view.terrain_page_count;
            draw.geometry_revision = view.geometry_revision;
            draw.terrain = view.terrain;
            draw.reorderable = view.ordering == NativeOrdering::reorderable ? 1u : 0u;
            draw.bounds_minimum = bounds.minimum; draw.bounds_maximum = bounds.maximum;
            piece.terrain.push_back(draw);
        }
    }
}

void piece_decorations(PieceBuilder& builder, const NeighbourMapUpload& upload) {
    auto& piece = builder.piece;
    for (std::size_t candidate_index = 0; candidate_index < upload.decorations.size(); ++candidate_index) {
        const auto& candidate = *upload.decorations[candidate_index];
        // Its impostor object, when baked (piece_impostors).
        const auto* plan = candidate_index < upload.impostor_plans.size() && !upload.impostor_plans[candidate_index].key.empty() ?
            &upload.impostor_plans[candidate_index] : nullptr;
        std::uint32_t impostor = 0;
        if (plan) {
            const auto found = builder.impostor_index.find(plan->key);
            if (found != builder.impostor_index.end()) impostor = found->second + 1;
        }
        const std::size_t first_draw = piece.decorations.size();
        bool lead_drawn = false;
        for (std::size_t node_index = 0; node_index < candidate.nodes.size(); ++node_index) {
            const auto& node = candidate.nodes[node_index];
            // A part without an archive texture (item 5: a live game surface)
            // keeps its mesh; its draw binds the posed record's texture.
            if (!node.figure || (!node.texture && !node.surface && candidate.impostor_inputs_complete)) { ++piece.unbuilt_decorations; continue; }
            const bool in_object = plan && plan->spheres[node_index][3] > 0;
            const auto found = upload.prebuilt_figures.find(neighbour_figure_shape(node.figure, candidate.source->shape,
                in_object ? &plan->spheres[node_index] : nullptr));
            if (found == upload.prebuilt_figures.end() || !found->second.bytes) { ++piece.unbuilt_decorations; continue; }
            NeighbourPieceDecoration draw;
            draw.candidate = static_cast<std::uint32_t>(candidate_index);
            draw.figure = builder.figure(*node.figure);
            draw.texture = node.texture ? builder.texture(node.texture->identity, node.texture_identity, node.texture_format,
                node.texture_mips.data(), static_cast<std::uint32_t>(node.texture_mips.size()), node.texture->opaque_mips) : UINT32_MAX;
            draw.mesh = builder.mesh(found->second);
            draw.shape = candidate.source->shape;
            draw.object = candidate.signed_id;
            draw.node = candidate.assembly && node_index < candidate.assembly->parts.size() ?
                candidate.assembly->parts[node_index].skeleton_node : UINT32_MAX;
            if (candidate.assembly && node_index < candidate.assembly->parts.size())
                draw.flags |= astral_record_flags(*candidate.assembly, candidate.assembly->parts[node_index]);
            if (candidate.source) retained_object_matrix(candidate.world_position, candidate.source->quaternion, draw.object_placement);
            draw.object_to_world = node.object_to_world;
            if(candidate.assembly&&node_index<candidate.assembly->parts.size()) {
                const auto& part=candidate.assembly->parts[node_index];
                if(part.environment_material) {
                    const auto& m=*part.environment_material;const auto native=m.native();
                    std::uint64_t key=1469598103934665603ull;for(char c:part.geometry->identity.key()){key^=std::uint8_t(c);key*=1099511628211ull;}
                    draw.environment_material=key?key:1;
                    std::copy_n(native.factors,4,draw.environment_factors.begin());std::copy_n(native.base,4,draw.environment_base.begin());std::copy_n(native.emission,4,draw.environment_emission.begin());
                    draw.environment_cutoff=m.cutoff;draw.environment_alpha=m.alpha;draw.environment_alpha_factor=m.alpha_factor;
                    draw.environment_address_u=m.address_u;draw.environment_address_v=m.address_v;
                    for(unsigned k=0;k<3;++k)if(m.textures[k+1]) {
                        const auto v=m.textures[k+1]->view();draw.environment_maps[k]=builder.texture(*v.resource,1,v.format,v.mips,v.mip_count,v.opaque_mips);
                    }
                }
            }
            draw.bounds_center = node.bounds.center;
            draw.bounds_minimum = node.bounds.minimum; draw.bounds_maximum = node.bounds.maximum;
            // Sway and foliage by the main map's rule (retained_visual_part_values).
            if (const auto* assembly = candidate.assembly.get();
                assembly && assembly->sways && assembly->parts.size() == candidate.nodes.size()) {
                const auto& part = assembly->parts[node_index];
                if (retained_visual_part_sway(part).foliage) draw.flags |= native_foliage_part_flag;
                const auto sway = candidate.root_valid ? retained_object_space_sway_flags(*assembly, part) : 0u;
                if (sway) { draw.flags |= sway; draw.sway = 1; draw.root_world = candidate.root_world; }
            }
            if (in_object && impostor) {
                draw.impostor = impostor;
                draw.impostor_lead = node_index == plan->lead;
                lead_drawn |= node_index == plan->lead;
            }
            piece.decorations.push_back(draw);
        }
        // Without its lead nothing would draw the object far away: its parts
        // keep their triangles.
        if (!lead_drawn)
            for (std::size_t i = first_draw; i < piece.decorations.size(); ++i) piece.decorations[i].impostor = 0;
    }
}

// One part's buffers (false: a creation failed, the object gets no bake).
bool piece_impostor_part(const NativeImpostorPart& source, NeighbourPieceImpostorPart& made, ID3D11Device* device,
    D3D12Preparer* d3d12) {
    made.index_count = static_cast<std::uint32_t>(source.indices.size());
    if (!device)
        // D3D12 backend: created and filled here; the bake records the copies.
        return SUCCEEDED(d3d12->prepare_buffer(source.vertices.size() * sizeof(float), source.vertices.data(), made.vertices12)) &&
            SUCCEEDED(d3d12->prepare_buffer(source.indices.size() * sizeof(std::uint32_t), source.indices.data(), made.indices12));
    D3D11_BUFFER_DESC vertices{};
    vertices.ByteWidth = static_cast<UINT>(source.vertices.size() * sizeof(float));
    vertices.Usage = D3D11_USAGE_IMMUTABLE;
    vertices.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    vertices.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    vertices.StructureByteStride = 8 * sizeof(float);
    const D3D11_SUBRESOURCE_DATA vertex_data{source.vertices.data(), 0, 0};
    D3D11_BUFFER_DESC indices{};
    indices.ByteWidth = static_cast<UINT>(source.indices.size() * sizeof(std::uint32_t));
    indices.Usage = D3D11_USAGE_IMMUTABLE;
    indices.BindFlags = D3D11_BIND_INDEX_BUFFER;
    const D3D11_SUBRESOURCE_DATA index_data{source.indices.data(), 0, 0};
    return SUCCEEDED(device->CreateBuffer(&vertices, &vertex_data, &made.vertices)) &&
        SUCCEEDED(device->CreateShaderResourceView(made.vertices.Get(), nullptr, &made.vertex_view)) &&
        SUCCEEDED(device->CreateBuffer(&indices, &index_data, &made.indices));
}

// Before the decorations: they name their object's entry.
void piece_impostors(PieceBuilder& builder, const NeighbourMapUpload& upload, ID3D11Device* device, D3D12Preparer* d3d12) {
    if (!device && !d3d12) return;
    for (const auto& [key, source] : upload.impostor_objects) {
        if (source.parts.empty() || source.parts.size() != source.nodes.size()) continue;
        NeighbourPieceImpostor made;
        made.key = key;
        made.sphere = source.sphere;
        bool ok = true;
        for (std::size_t i = 0; i < source.parts.size() && ok; ++i) {
            const auto& node = *source.nodes[i];
            NeighbourPieceImpostorPart part;
            if (node.surface) { part.texture = UINT32_MAX; part.surface = node.surface; }
            else part.texture = builder.texture(node.texture->identity, node.texture_identity, node.texture_format,
                node.texture_mips.data(), static_cast<std::uint32_t>(node.texture_mips.size()), node.texture->opaque_mips);
            ok = piece_impostor_part(source.parts[i], part, device, d3d12);
            made.parts.push_back(std::move(part));
        }
        if (!ok) continue; // its parts draw their own triangles
        builder.impostor_index.emplace(key, static_cast<std::uint32_t>(builder.piece.impostors.size()));
        builder.piece.impostors.push_back(std::move(made));
    }
}

void piece_ground_cover(PieceBuilder& builder, const NeighbourMapUpload& upload) {
    auto& piece = builder.piece;
    piece.ground_cover = upload.ground_cover_inputs;
    piece.liquid_surf = upload.liquid_surf;
    auto* inputs = piece.ground_cover.get();
    if (!inputs || !inputs->grids_valid || !upload.map || !upload.map->terrain) return;
    // Its terrain's own atlas pages, as its terrain draws bind them.
    const auto& terrain = *upload.map->terrain;
    inputs->atlas_textures.assign(inputs->used.size(), UINT32_MAX);
    for (std::size_t material = 0; material < inputs->used.size(); ++material) {
        if (!inputs->used[material]) continue;
        const auto page_index = material % (terrain.texture_count + 2u);
        if (page_index >= terrain.pages.size() || !terrain.pages[page_index].texture ||
            terrain.pages[page_index].mips.empty()) continue;
        const auto& page = terrain.pages[page_index];
        inputs->atlas_textures[material] = builder.texture(page.texture->identity,
            page.texture->identity.container_incarnation, static_cast<NativeOwnedTextureFormat>(page.texture->format),
            page.mips.data(), static_cast<std::uint32_t>(page.mips.size()), page.texture->opaque_mips);
    }
}

} // namespace

std::unique_ptr<NeighbourPiece> make_neighbour_piece(NeighbourMapUpload& upload, ID3D11Device* device, D3D12Preparer* d3d12) {
    auto piece = std::make_unique<NeighbourPiece>();
    piece->epoch = upload.epoch;
    piece->selection = upload.selection;
    piece->selected = upload.selected;
    piece->welded = upload.welded;
    piece->decoded_bytes = upload.decoded_bytes;
    piece->omitted_decorations = upload.omitted_decorations;
    piece->decoration_summary = upload.decoration_summary;
    piece->lod_stats.figures = upload.lod_stats.figures;
    piece->lod_stats.reduced_figures = upload.lod_stats.reduced_figures;
    piece->lod_stats.full_triangles = upload.lod_stats.full_triangles;
    piece->lod_stats.level_triangles = upload.lod_stats.level_triangles;
    piece->lod_stats.milliseconds = upload.lod_stats.milliseconds;
    piece->lod_stats.prebuild_milliseconds = upload.lod_stats.prebuild_milliseconds;
    PieceBuilder builder{*piece, {}, {}, {}, {}};
    if (upload.prebuilt_pages.empty()) piece->terrain_failure = "terrain.prebuild_failed";
    else {
        piece_terrain(builder, upload);
        piece_impostors(builder, upload, device, d3d12);
        piece_decorations(builder, upload);
    }
    piece_ground_cover(builder, upload);
    piece->pages = std::move(upload.prebuilt_pages);
    piece->prebuilt_textures = std::move(upload.prebuilt_textures);
    return piece;
}

} // namespace owned
