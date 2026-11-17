#pragma once
// One adopted neighbour as the renderer receives it (neighbour_piece.cpp).
//
// The neighbour stream decodes, welds and prepares a neighbour map, then
// condenses it into this piece: the GPU objects it created (geometry pages,
// textures, impostor buffers), small identity tables, one record per terrain
// and decoration draw, and the ground-cover/surf inputs the renderer's own
// effects still build from. The decoded map, its figures, textures and
// decoration candidates stay behind on the stream side and die there.
//
// The renderer DLL builds pieces on its own stream (standard mode); the x64
// render host builds the same pieces and the DLL receives them through the
// remote link (remote mode), so the game process never holds a decoded
// neighbour. Records hold values and indices only, never CPU pointers.
#include "ground_cover_neighbour.h"
#include "native_prebuilt.h"
#include "neighbour_maps.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace owned {

struct LiquidSurfField;
struct TerrainFaceGrid; // terrain_face_atlas.h

// A texture a draw binds: the archive member whose key the owned texture
// cache uses. Block-compressed members arrive created (NeighbourPiece::
// prebuilt_textures); other formats carry their mips here, freed at adoption,
// and take the render thread's own upload path.
struct NeighbourPieceTexture {
    ResourceIdentity identity;
    std::uint64_t view_identity = 0;     // NativeOwnedTextureView::identity
    NativeOwnedTextureFormat format = NativeOwnedTextureFormat::dxt1;
    std::uint32_t mip_count = 0;
    // Bit i: mip i proven alpha1 (TextureCpuAsset::opaque_mips); unknown and
    // transparent mips stay 0. The material class reads it (static LOD).
    std::uint32_t opaque_mips = 0;
    struct Mip { std::uint32_t offset = 0, width = 0, height = 0, bytes = 0; };
    std::vector<Mip> mips;               // into bytes; empty when created by the stream
    std::vector<std::uint8_t> bytes;
};

// One terrain command (full detail, every subpart) of the neighbour.
struct NeighbourPieceTerrainDraw {
    std::uint32_t sector = 0, material = 0;
    std::uint32_t identity = 0;          // NeighbourPiece::identities (the sector's)
    std::uint32_t texture = 0;           // NeighbourPiece::textures (its atlas page)
    std::uint32_t mesh = 0;              // NeighbourPiece::meshes
    std::uint32_t position_groups = 0, normal_groups = 0, texture_coordinate_count = 0,
        component_count = 0, index_count = 0, terrain_page_count = 0;
    std::array<std::uint32_t, 5> geometry_revision{};
    NativeTerrainState terrain{};
    std::uint32_t reorderable = 1;
    std::array<float, 3> bounds_minimum{}, bounds_maximum{}; // map-local command bounds
};

// A decoration figure's mesh-cache identity and stream counts.
struct NeighbourPieceFigure {
    std::uint32_t shape_count=1,shape_normals=0;
    std::uint32_t identity = 0;          // NeighbourPiece::identities
    std::uint32_t position_groups = 0, normal_groups = 0, texture_coordinate_count = 0,
        component_count = 0, index_count = 0;
};

// One drawn decoration node.
struct NeighbourPieceDecoration {
    std::uint64_t environment_material = 0;
    std::array<std::uint32_t,3> environment_maps{UINT32_MAX,UINT32_MAX,UINT32_MAX};
    std::array<float,4> environment_factors{},environment_base{},environment_emission{};
    float environment_cutoff=.5f,environment_alpha_factor=1;
    std::uint32_t environment_alpha=0;
    std::uint32_t environment_address_u=1,environment_address_v=1;
    std::uint32_t candidate = 0;         // nodes of one MOB object share it (contact span)
    std::uint32_t figure = 0;            // NeighbourPiece::figures
    std::uint32_t texture = 0;           // NeighbourPiece::textures (UINT32_MAX: none, item 5 surface part)
    std::uint32_t mesh = 0;              // NeighbourPiece::meshes (its figure at its shape)
    std::array<float, 3> shape{};
    std::array<float, 16> object_to_world{}; // map-local node placement
    std::array<float, 3> bounds_center{}, bounds_minimum{}, bounds_maximum{};
    std::uint32_t flags = 0;             // foliage and sway bits for the record
    std::uint32_t sway = 0;              // nonzero: root_world is the sway anchor
    std::array<float, 3> root_world{};   // map-local
    // Its object's crown impostor: NeighbourPiece::impostors index + 1 (0:
    // none), and whether this node is the part that draws it (the lead).
    std::uint32_t impostor = 0;
    std::uint32_t impostor_lead = 0;
    std::int32_t object = 0;             // its MOB object id (impostor debug labels)
    std::uint32_t node = 0;              // its model node (the active map's statics match live parts by it)
    std::array<float, 16> object_placement{}; // its object's map-local placement (no node offset)
};

// One part of a crown impostor object: its texture and its triangles already
// on the GPU (8 floats per vertex in the lead part's frame, 32-bit indices).
struct NeighbourPieceImpostorPart {
    std::uint32_t texture = 0;           // NeighbourPiece::textures (UINT32_MAX: surface)
    // Item 5 live-sourced models only (never a neighbour piece, never sent
    // across the remote link): the part's game surface, resolved by the
    // renderer's surface cache.
    std::shared_ptr<const OwnedVisualSurface> surface;
    Microsoft::WRL::ComPtr<ID3D11Buffer> vertices, indices;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> vertex_view;
    // D3D12 backend: both buffers created with their bytes in upload memory
    // on the stream worker; the bake records their copies (finish_buffer).
    std::shared_ptr<D3D12PreparedUpload> vertices12, indices12;
    std::uint32_t index_count = 0;
};
// Crown impostor bake input of one object model: all its drawn parts, baked
// together into one atlas slice named by key.
struct NeighbourPieceImpostor {
    std::string key;
    std::vector<NeighbourPieceImpostorPart> parts;
    std::array<float, 4> sphere{};       // the lead part's frame
};

struct NeighbourPiece {
    std::uint64_t epoch = 0;
    NeighbourSelection selection;
    std::vector<NeighbourBounds> selected;   // every selected neighbour (active-map coordinates)
    bool welded = false;
    // A terrain step that rejected the whole piece (the names the renderer
    // logs as failed_step); empty when the terrain is complete.
    std::string terrain_failure;
    std::uint64_t decoded_bytes = 0;
    std::size_t omitted_decorations = 0;
    std::uint32_t unbuilt_decorations = 0;   // nodes whose mesh the stream could not build
    std::string decoration_summary;
    struct {
        std::uint32_t figures = 0, reduced_figures = 0;
        std::uint64_t full_triangles = 0;
        std::array<std::uint64_t, kStaticLodMaxLevels> level_triangles{};
        double milliseconds = 0, prebuild_milliseconds = 0;
        double load_milliseconds = 0; // decode through piece preparation, before renderer adoption
    } lod_stats;
    std::vector<ResourceIdentity> identities;
    std::vector<NeighbourPieceTexture> textures;
    std::vector<NativePrebuiltPage> pages;
    std::vector<NativePrebuiltMesh> meshes;
    std::vector<NativePrebuiltTexture> prebuilt_textures;
    std::vector<NeighbourPieceTerrainDraw> terrain;
    std::vector<NeighbourPieceFigure> figures;
    std::vector<NeighbourPieceDecoration> decorations;
    std::vector<NeighbourPieceImpostor> impostors;
    std::shared_ptr<GroundCoverNeighbourInputs> ground_cover;
    std::shared_ptr<const LiquidSurfField> liquid_surf;
    // Its terrain face grid (terrain_face_atlas.h) from the welded terrain:
    // the renderer builds the face atlas from it whenever the face cache is
    // on. Not on the remote link (remote mode keeps neighbour faces per pixel).
    std::shared_ptr<const TerrainFaceGrid> face_grid;
    // Filled by the renderer at adoption.
    NeighbourBounds fog_edges;
    NeighbourFogField fog_cull;
    mutable std::uint32_t fog_culled = 0;
};

// The texture view of a piece texture (mips only when it carries them).
NativeOwnedTextureView neighbour_piece_texture_view(const NeighbourPieceTexture& texture,
    std::vector<NativeOwnedTextureMip>& mips);

// Condenses a decoded, welded and prebuilt neighbour into its piece, creating
// its impostor buffers on `device` (D3D12 backend, no device: preparing them
// through `d3d12`). The upload keeps its decoded map; the caller drops it
// afterwards.
std::unique_ptr<NeighbourPiece> make_neighbour_piece(NeighbourMapUpload& upload, ID3D11Device* device,
    D3D12Preparer* d3d12 = nullptr);

} // namespace owned
