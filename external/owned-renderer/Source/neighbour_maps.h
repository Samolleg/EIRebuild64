#pragma once
#include "neighbour_fog.h"
#include "retained_static_submission.h"
#include "static_lod.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <cmath>
#include <memory>
#include <array>
#include <cstring>
#include <map>

namespace owned {
// One dedicated geometry page a worker created with its final contents.
struct NativePrebuiltPage {
    Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
    std::uint32_t bytes = 0;
};
// Packs decoded meshes into pages of at most 64 MB of CPU staging, creating
// each page with its initial data (the device is free-threaded) and freeing
// the staging copy. Renderer::build_native_prebuilt_mesh writes the exact
// layout get_native_mesh would upload.
// One block-compressed archive texture a worker created with its mips, keyed
// as get_native_owned_texture keys it; the render thread adds it to its
// texture cache at upload unless the key is resident already.
struct NativePrebuiltTexture {
    std::string key;
    std::uint64_t generation = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    std::uint32_t width = 0, height = 0, mip_count = 0;
    std::uint64_t bytes = 0;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
};
// The texture cache key of an archive member (get_native_owned_texture).
std::string native_owned_archive_texture_key(const ResourceIdentity& resource,
    std::uint64_t generation, bool full_mips);
// Creates one prebuilt texture (block-compressed formats with authored mips
// only); false when the source does not qualify or creation failed.
bool build_native_prebuilt_texture(ID3D11Device* device, const NativeOwnedTextureView& source,
    NativePrebuiltTexture& output) noexcept;
// Keys resident in the renderer's owned texture cache, so the stream does
// not create textures the cache already holds (a stale answer only costs a
// duplicate creation or a render-thread upload).
void neighbour_texture_resident(const std::string& key, bool resident) noexcept;
void neighbour_textures_clear() noexcept;
bool neighbour_texture_is_resident(const std::string& key) noexcept;
struct NativePrebuiltBuilder {
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    std::vector<std::uint8_t> blob;
    std::vector<NativePrebuiltPage> pages;
    bool failed = false;
};
bool build_native_prebuilt_mesh(NativePrebuiltBuilder& builder, const NativeGeometryView& view,
    NativePrebuiltMesh& output) noexcept;
bool finish_native_prebuilt_pages(NativePrebuiltBuilder& builder) noexcept;
// The shared mesh of a neighbour decoration figure at one exact shape: the
// CPU's shape selection (positions), keyed by the figure and that shape
// (get_native_mesh's shape_bits), so every node of every neighbour drawing the
// figure at that shape shares one mesh. The node transform is per record
// (NativeWorldGeometry::object_space).
struct NeighbourFigureShape {
    const FigureCpuAsset* figure = nullptr;
    std::array<std::uint32_t, 3> shape{};
    bool operator<(const NeighbourFigureShape& other) const noexcept {
        return figure != other.figure ? figure < other.figure : shape < other.shape;
    }
};
inline NeighbourFigureShape neighbour_figure_shape(const FigureCpuAsset* figure, const std::array<float, 3>& shape) noexcept {
    NeighbourFigureShape key{figure, {}};
    std::memcpy(key.shape.data(), shape.data(), sizeof(key.shape));
    return key;
}
inline NativeGeometryView neighbour_figure_view(const FigureCpuAsset& figure, const NativePackedGroup* positions,
    const std::array<float, 3>& shape) noexcept {
    NativeGeometryView view;
    view.positions=positions;view.position_groups=figure.position_groups;
    view.normals=figure.normals.data();view.normal_groups=figure.normal_groups;
    view.texture_coordinates=figure.texture_coordinates.data();
    view.texture_coordinate_count=static_cast<std::uint32_t>(figure.texture_coordinates.size());
    view.components=figure.components.data();view.component_count=static_cast<std::uint32_t>(figure.components.size());
    view.indices=figure.indices.data();view.index_count=static_cast<std::uint32_t>(figure.indices.size());
    view.world.enabled=true;view.world.identity=&figure.identity;view.world.shape=shape;
    view.world.object_space=true;
    return view;
}
// The CPU inputs of a neighbour's ground cover and contact maps, computed by
// the stream after the weld (renderer_ground_cover.cpp).
struct GroundCoverNeighbourInputs;
struct NeighbourMapUpload;
std::shared_ptr<GroundCoverNeighbourInputs> prepare_ground_cover_neighbour_inputs(
    const NeighbourMapUpload& upload) noexcept;
// Its sea surf field (liquid_surf.h) from those inputs' area grid; null
// without water or inputs.
struct LiquidSurfField;
std::shared_ptr<const LiquidSurfField> prepare_liquid_surf_neighbour(const NeighbourMapUpload& upload,const std::string& no_sea_maps) noexcept;
struct NeighbourMapUpload {
    std::uint64_t epoch = 0;
    std::shared_ptr<GroundCoverNeighbourInputs> ground_cover_inputs;
    std::shared_ptr<const LiquidSurfField> liquid_surf;
    NeighbourSelection selection;
    std::shared_ptr<const OwnedMapSnapshot> map;
    std::vector<std::shared_ptr<RetainedStaticSourceCandidate>> decorations;
    std::uint64_t decoded_bytes = 0;
    std::size_t omitted_decorations = 0;
    // Drawn decorations per family and omitted ones per reason, for the log.
    std::string decoration_summary;
    // Every selected neighbour (active-map coordinates).
    std::vector<NeighbourBounds> selected;
    // This piece's raw rectangle for its border-fog band (the renderer fills
    // it; the shaders let the band give way where other pieces adjoin).
    NeighbourBounds fog_edges;
    // World-space neighbour fog for culling: terrain batches and decorations
    // wholly in full fog are not uploaded (the renderer fills it from the
    // selected coverage and the current range).
    NeighbourFogField fog_cull;
    mutable std::uint32_t fog_culled = 0;
    // Geometry the stream decoded into dedicated pages after the weld: terrain
    // commands by (sector << 16 | material), ascending, and one shared mesh per
    // decoration figure and exact shape (every node of that figure at that
    // shape draws it in object space). Empty when no device was available or
    // a page failed.
    std::vector<NativePrebuiltPage> prebuilt_pages;
    std::vector<NativePrebuiltTexture> prebuilt_textures;
    std::vector<std::uint32_t> prebuilt_terrain_keys;
    std::vector<NativePrebuiltMesh> prebuilt_terrain;
    std::map<NeighbourFigureShape, NativePrebuiltMesh> prebuilt_figures;
    // Crown impostor bake inputs (static_lod.h), per cut-out figure and
    // texture: the renderer bakes them into its atlas before the decorations
    // register (Renderer::bake_neighbour_impostors).
    struct ImpostorSource {
        NativeImpostorSource source;
        const RetainedPreparedNode* node = nullptr; // its texture
    };
    std::map<std::pair<const FigureCpuAsset*, std::uint64_t>, ImpostorSource> impostor_sources;
    // Static LOD levels built for this neighbour's figures (log line).
    struct {
        std::uint32_t figures = 0, reduced_figures = 0;
        std::uint64_t full_triangles = 0;
        std::array<std::uint64_t, kStaticLodMaxLevels> level_triangles{};
        double milliseconds = 0, prebuild_milliseconds = 0;
    } lod_stats;
    const NativePrebuiltMesh* prebuilt_terrain_mesh(std::uint32_t sector, std::uint32_t material) const noexcept {
        const std::uint32_t key = (sector << 16) | material;
        const auto found = std::lower_bound(prebuilt_terrain_keys.begin(), prebuilt_terrain_keys.end(), key);
        return found != prebuilt_terrain_keys.end() && *found == key ?
            &prebuilt_terrain[std::size_t(found - prebuilt_terrain_keys.begin())] : nullptr;
    }
    // The stream welded this terrain's shared edges (heights, normals) and set
    // its outside liquid against the active map and the earlier neighbours,
    // in layout order, before handing it over.
    bool welded = false;
};
// Visual families a neighbour backdrop draws as render-only meshes: flora
// (53), structures such as buildings, walls and bridges (54), stones (55) and
// levers (60: doors, gates, chests, panels) in their authored state. Nothing
// there is interactive. Units, torches and traps are not drawn.
inline bool neighbour_decoration_family(std::uint32_t family) noexcept {
    return (family>=53 && family<=55) || family==60;
}
// Why one neighbour MOB object is not drawn as a decoration; nullptr when it
// is structurally drawable (immutable preparation may still reject it).
const char* neighbour_decoration_omission(const StaticObjectSource& object,
    const OwnedMapSnapshot& map) noexcept;
// The four side strips of a terrain placed at (x,y) in active-map coordinates.
std::vector<NeighbourEdgeStrip> neighbour_edge_strips(const TerrainCpuAsset& terrain,
    double x, double y);
// Adjoining maps share their edge heights (shipped data: within 0.1 m at every
// authored vertex) but not their authored normals, which shade a visible line.
// Gives each base vertex on a side of `terrain` (placed at x,y) that touches an
// opposite strip the strip's normal, interpolated along it, so the rendered
// piece adopts the shading of the piece it adjoins. Returns welded vertices.
// Adjoining maps can disagree on their shared edge (Dejavu zone2 against
// zone3obr: heights one row apart, up to 3 m, and zone3obr's edge vertices
// jittered 0.5 m along it), which opens a crack showing the sky. Moves each
// base vertex on a side of `terrain` that touches an opposite strip onto the
// strip's vertex (XY and height), and the vertices behind it by a share that
// fades over kNeighbourHeightWeldBand vertices, so both maps' edges are one
// polyline. Only grid-aligned strips (whole-metre offsets). Render only.
constexpr int kNeighbourHeightWeldBand = 4;
void neighbour_weld_edge_heights(TerrainCpuAsset& terrain, double x, double y,
    const std::vector<NeighbourEdgeStrip>& strips);
std::size_t neighbour_weld_edge_normals(TerrainCpuAsset& terrain, double x, double y,
    const std::vector<NeighbourEdgeStrip>& strips);
void neighbour_maps_enable(bool enabled) noexcept;
// The map's origin in the saved world layout (its component's frame, the
// same numbers the neighbour placement subtracts), read from the layout file
// for the verified profile; false and 0,0 when the map is not in it. A map
// therefore has the same frame as the active map and as a neighbour.
bool neighbour_layout_origin(const std::string& mpr, const ResourceStoreConfig& mounts, double& x, double& y) noexcept;
// A layout origin snapped to whole 2 m terrain tiles: the frame of the
// terrain material layouts (noise, hex cells, world-space tile reads), which
// must keep each tile's world-space read aligned with its own art.
inline float neighbour_layout_tile_origin(double value) noexcept {return float(2.0*std::round(value*.5));}
// The device the stream creates prebuilt geometry pages on (null: none).
void neighbour_maps_set_device(ID3D11Device* device) noexcept;
// The neighbour terrain's commands as upload would submit them, with a decode
// that is safe on a worker (no live game reads, no frame state).
void bridge_prepare_neighbour_terrain(const NeighbourMapUpload& upload,
    void (*emit)(void* context, std::uint32_t sector, std::uint32_t material, const NativeGeometryView& view),
    void* context);
// Active map rectangle, its border-fog edges over the accepted neighbours
// (neighbour_fog_edges), where they touch it (neighbour_fog_contacts) and the
// F10 range scale, in active-map coordinates.
// Edges equal the active rectangle when no neighbour is rendered. The renderer
// publishes it; terrain, liquids, ground cover, critters, particles and the
// game's object fog byte all derive the band shift and seam fog from it.
struct NeighbourBorderFog { NeighbourBounds active, edges; NeighbourContacts contacts; double range = 1; };
void neighbour_border_fog_publish(const NeighbourBorderFog& fog) noexcept;
NeighbourBorderFog neighbour_border_fog() noexcept;
// Publication follows accepted-piece membership, range restreams and teardown.
// Retained object fog must be recomputed after any of these render-only changes.
std::uint64_t neighbour_border_fog_revision() noexcept;
// Sun-shadow receivers of the rendered neighbours: world-space boxes, one per
// 32 m cell of each accepted neighbour's terrain and decorations. The renderer
// publishes them on accepted-piece changes and teardown; the bridge adds the
// visible ones to the active map's terrain receivers, so the sun maps' bands,
// squares and depth ranges are fitted to neighbour ground as well (without
// them, neighbour ground beyond the active map's receivers selected no map).
void neighbour_receivers_publish(std::vector<NativeWorldBounds> cells) noexcept;
void neighbour_receivers_append_visible(const std::array<float,16>& world_to_clip,
    std::vector<NativeWorldBounds>& receivers) noexcept;
void neighbour_maps_begin(const std::string& mpr, std::uint64_t epoch,
    const ResourceStoreConfig& mounts) noexcept;
// The water_surf_no_sea_maps list neighbour sea fields are built with (the
// renderer publishes it; a stream takes it when it starts).
void neighbour_maps_set_no_sea_maps(const std::string& maps) noexcept;
void neighbour_maps_stop() noexcept;
// Stops the stream only while it serves this map generation. A world change
// releases the uploaded neighbours; a reload of the same map keeps its
// generation, so its drained stream must go or begin() would reuse it.
void neighbour_maps_stop_epoch(std::uint64_t epoch) noexcept;
std::unique_ptr<NeighbourMapUpload> neighbour_maps_take(std::uint64_t epoch) noexcept;
void neighbour_maps_uploaded(std::uint64_t epoch) noexcept;
std::string neighbour_maps_error(std::uint64_t epoch);
// True once no stream runs for this epoch (disabled, no profile, never begun)
// or its worker has finished and its last piece was uploaded.
bool neighbour_maps_settled(std::uint64_t epoch) noexcept;
// Neighbours of this epoch's stream not yet uploaded (0 once settled); false
// while the stream has not read its layout yet (count unknown).
bool neighbour_maps_remaining(std::uint64_t epoch,std::size_t& remaining) noexcept;
bool bridge_neighbour_world_transform(std::array<float,16>& placement) noexcept;
void bridge_neighbour_diagnostic(const char* reason) noexcept;
// Synchronous preparation into the shipped owned terrain decoder; callback
// uploads each piece before the borrowed context dies. No live game reads.
// On failure, failure names the rejected step.
bool bridge_upload_neighbour_terrain(const NeighbourMapUpload& upload,
    const std::weak_ptr<const void>& owner, const std::array<float,16>& world_to_clip,
    NativeGeometrySubmitFn submit, void* context, std::uint32_t& part, const char*& failure);
}
