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
#include <functional>
#include <map>
#include <string>
#include <unordered_set>
#include <vector>

namespace owned {
struct D3D12PreparedUpload;  // d3d12_frame.h
class D3D12Preparer;
// One dedicated geometry page a worker created with its final contents.
struct NativePrebuiltPage {
    Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
    // D3D12 backend: the page created and its bytes in upload memory on the
    // worker; the render thread only records the copy (finish_buffer).
    std::shared_ptr<D3D12PreparedUpload> d3d12;
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
    // D3D12 backend: the texture created and every level in upload memory on
    // the worker; the render thread only records the copies (finish_texture).
    std::shared_ptr<D3D12PreparedUpload> d3d12;
};
// The texture cache key of an archive member (get_native_owned_texture).
std::string native_owned_archive_texture_key(const ResourceIdentity& resource,
    std::uint64_t generation, bool full_mips);
// The texture cache key of an owned source with CPU levels
// (get_native_owned_texture_d3d12): generated is its box-filtered BGRA8
// chain, else its authored levels as they are.
std::string native_owned_mip_texture_key(const NativeOwnedTextureView& source, bool generated);
// The DXGI format of an uncompressed owned format (UNKNOWN for the rest) and
// its bytes per pixel.
DXGI_FORMAT native_owned_uncompressed_format(NativeOwnedTextureFormat format, UINT& pixel_bytes) noexcept;
// The generated chain of a one-level uncompressed source: level 0 expanded
// to BGRA8, then each level the 2x2 box of the one above (clamped at odd
// edges, rounded), down to 1x1. False when the source does not qualify.
bool native_owned_box_mips(const NativeOwnedTextureView& source, std::vector<std::vector<std::uint32_t>>& levels);
// Creates one prebuilt texture (block-compressed formats with authored mips;
// D3D12 backend also uncompressed ones, as the render thread would create
// them: the generated chain or the authored levels, under its key); false
// when the source does not qualify or creation failed.
// D3D12 backend: d3d12 prepares it instead (device null).
bool build_native_prebuilt_texture(ID3D11Device* device, D3D12Preparer* d3d12, const NativeOwnedTextureView& source,
    NativePrebuiltTexture& output) noexcept;
// Keys resident in the renderer's owned texture cache, so the stream does
// not create textures the cache already holds (a stale answer only costs a
// duplicate creation or a render-thread upload).
void neighbour_texture_resident(const std::string& key, bool resident) noexcept;
void neighbour_textures_clear() noexcept;
bool neighbour_texture_is_resident(const std::string& key) noexcept;
// The [RES] mounts of the current world (set when its stream starts); null
// before the first world.
std::shared_ptr<const ResourceStoreConfig> neighbour_world_mounts() noexcept;
struct NativePrebuiltBuilder {
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    std::shared_ptr<D3D12Preparer> d3d12;  // D3D12 backend (no device): pages are prepared uploads
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
// (NativeWorldGeometry::object_space). A part of an impostor object also
// carries the object's sphere in its own frame (its mesh's sphere block), so
// the key holds it too (zero: the part's own sphere).
struct NeighbourFigureShape {
    const FigureCpuAsset* figure = nullptr;
    std::array<std::uint32_t, 3> shape{};
    std::array<std::uint32_t, 4> sphere{};
    bool operator<(const NeighbourFigureShape& other) const noexcept {
        if (figure != other.figure) return figure < other.figure;
        return shape != other.shape ? shape < other.shape : sphere < other.sphere;
    }
};
inline NeighbourFigureShape neighbour_figure_shape(const FigureCpuAsset* figure, const std::array<float, 3>& shape,
    const std::array<float, 4>* sphere = nullptr) noexcept {
    NeighbourFigureShape key{figure, {}, {}};
    std::memcpy(key.shape.data(), shape.data(), sizeof(key.shape));
    if (sphere) std::memcpy(key.sphere.data(), sphere->data(), sizeof(key.sphere));
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
// the stream after the weld (ground_cover_neighbour.h).
struct GroundCoverNeighbourInputs;
struct LiquidSurfField;
struct NeighbourPiece; // neighbour_piece.h
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
    // Crown impostor bake inputs (static_lod.h), one per object model (its
    // drawn parts and their textures), from the first decoration drawing it:
    // the renderer bakes them into its atlas before the decorations register
    // (Renderer::bake_neighbour_impostors). Empty parts: not bakeable.
    struct ImpostorObject {
        std::vector<NativeImpostorPart> parts;
        std::vector<const RetainedPreparedNode*> nodes; // per part, its texture
        std::array<float, 4> sphere{};                  // in the lead part's frame
    };
    std::map<std::string, ImpostorObject> impostor_objects;
    // Per decoration (index as decorations): its impostor object's key (empty:
    // none), its lead node and the object's sphere at its own shape in each
    // node's frame (per node; radius 0 for a node outside the object).
    struct ImpostorPlan {
        std::string key;
        std::uint32_t lead = 0;
        std::vector<std::array<float, 4>> spheres;
        // Morphing parts (eight shapes): the shape group's bake is its
        // representative's (decoration index); unit_points, every part
        // vertex in the sphere's unit frame, only until the grouping.
        bool morphs = false;
        std::size_t representative = 0;
        std::vector<float> unit_points;
    };
    std::vector<ImpostorPlan> impostor_plans;
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
// The terrain shape values (slope normal, facet flatness) drive the cliff
// side projection and the blend's steepness. Each map computes them from its
// own grid, one-sided at its edge, so two maps disagreed on their shared edge
// vertices and the cliff texturing broke along the seam as a hard line
// (#197, zone3obr against its neighbour). Prepares `terrain`'s values from its
// welded grid, then gives each base vertex on a side that touches an opposite
// strip the strip's values (whole-metre alignment only), so both maps
// interpolate from the same seam values. Returns welded vertices.
std::size_t neighbour_weld_edge_shading(TerrainCpuAsset& terrain, double x, double y,
    const std::vector<NeighbourEdgeStrip>& strips);
void neighbour_maps_enable(bool enabled) noexcept;
bool neighbour_maps_enabled() noexcept;
// The map's origin in the saved world layout (its component's frame, the
// same numbers the neighbour placement subtracts), as the neighbour stream
// read it for this active map from the verified profile's layout file;
// false and 0,0 until the stream has read it or when the map is not in it.
// A map therefore has the same frame as the active map and as a neighbour.
bool neighbour_layout_origin(const std::string& mpr, double& x, double& y) noexcept;
// A map's name as the layout lists it (file name, lower case, no ".mpr").
std::string neighbour_map_name(const std::string& mpr);
// A layout origin snapped to whole 2 m terrain tiles: the frame of the
// terrain material layouts (noise, hex cells, world-space tile reads), which
// must keep each tile's world-space read aligned with its own art.
inline float neighbour_layout_tile_origin(double value) noexcept {return float(2.0*std::round(value*.5));}
// The device the stream creates prebuilt geometry pages on (null: none).
void neighbour_maps_set_device(ID3D11Device* device) noexcept;
// D3D12 backend: the stream creates its pages and textures through the
// executor's preparer (null: none), so the render thread only records copies.
void neighbour_maps_set_d3d12(std::shared_ptr<D3D12Preparer> preparer) noexcept;
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
// Typed render-only inputs from CTerrain or the verified retail terrain reader.
// Keep the object byte correction shared with the addon; no game addresses here.
struct ObjectBorderFogInputs {
    std::array<float, 2> extent{};
    float distance = 0;
    bool enabled = false;
};
inline std::uint8_t neighbour_continued_border_fog(const ObjectBorderFogInputs& inputs,
    const float* position, std::uint8_t fog) noexcept {
    const auto published=neighbour_border_fog();
    const NeighbourBounds shift{published.edges.x0-published.active.x0,published.edges.y0-published.active.y0,
        published.edges.x1-published.active.x1,published.edges.y1-published.active.y1};
    const auto field=neighbour_fog_field(published.active,published.contacts,published.range);
    if(!field.span.any(0) && !field.span.any(1) && !field.span.any(2) && !field.span.any(3))return fog;
    const float distance=inputs.distance;
    const auto& extent=inputs.extent;
    if(!inputs.enabled || !position || !std::isfinite(distance) || distance<=0.0f ||
       !std::isfinite(extent[0]) || !std::isfinite(extent[1]) ||
       !std::isfinite(position[0]) || !std::isfinite(position[1]))return fog;
    const double inset=double(distance)+1.0;
    const double x0=inset,y0=inset,x1=extent[0]-inset,y1=extent[1]-inset;
    const double original=border_fog_density(position[0],position[1],{x0,y0,x1,y1},distance,{});
    const double moved=border_fog_density(position[0],position[1],
        {x0+shift.x0,y0+shift.y0,x1+shift.x1,y1+shift.y1},distance,field);
    const double value=std::round(double(fog)+255.0*(original-moved));
    return static_cast<std::uint8_t>((std::max)(0.0,(std::min)(255.0,value)));
}
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
// What a stream reads besides the archives: the world layout file next to the
// loaded addon, and the active map's decoded terrain for the edge weld (null
// until it is published; the stream waits up to 60 s). The renderer DLL takes
// both from its own process (renderer_neighbour_maps.cpp); the x64 render
// host gets the layout path in its begin record and decodes the active MPR.
struct NeighbourStreamSource {
    std::wstring layout_file;
    std::function<std::shared_ptr<const TerrainCpuAsset>()> active_terrain;
};
// The layout file next to addon.dll in this process; throws without the addon.
std::wstring neighbour_layout_file();
// The stream of this process (the renderer DLL's own in standard mode, the
// host's in remote mode; the DLL reaches either through neighbour_client.h).
void neighbour_maps_begin(const std::string& mpr, std::uint64_t epoch,
    const ResourceStoreConfig& mounts, NeighbourStreamSource source) noexcept;
// The water_surf_no_sea_maps list neighbour sea fields are built with (the
// renderer publishes it; a stream takes it when it starts).
void neighbour_maps_set_no_sea_maps(const std::string& maps) noexcept;
std::string neighbour_maps_no_sea_maps();
void neighbour_maps_stop() noexcept;
// Stops the stream only while it serves this map generation. A world change
// releases the uploaded neighbours; a reload of the same map keeps its
// generation, so its drained stream must go or begin() would reuse it.
void neighbour_maps_stop_epoch(std::uint64_t epoch) noexcept;
// A reload of the same map, where the game's main load starts (it keeps its
// generation, so no begin): the stream restarts for the coming world (one
// nothing was taken from yet is kept as it is) and is held, delivering
// nothing to the world being left, until the world change releases it. Its
// neighbours then prepare during the game's load instead of after it.
void neighbour_maps_restart(const std::string& mpr, std::uint64_t epoch,
    const ResourceStoreConfig& mounts, NeighbourStreamSource source) noexcept;
// The renderer's world change (release_world_state): a held stream is
// released to the new world; otherwise neighbour_maps_stop_epoch.
void neighbour_maps_world_changed(std::uint64_t epoch) noexcept;
// The next neighbour's piece in layout order (neighbour_piece.h).
std::unique_ptr<NeighbourPiece> neighbour_maps_take(std::uint64_t epoch) noexcept;
void neighbour_maps_uploaded(std::uint64_t epoch) noexcept;
std::string neighbour_maps_error(std::uint64_t epoch);
// True once no stream runs for this epoch (disabled, no profile, never begun)
// or its worker has finished and its last piece was uploaded.
bool neighbour_maps_settled(std::uint64_t epoch) noexcept;
// Neighbours of this epoch's stream not yet uploaded (0 once settled); false
// while the stream has not read its layout yet (count unknown).
bool neighbour_maps_remaining(std::uint64_t epoch,std::size_t& remaining) noexcept;
// Stream steps shared with the active map's object-space statics: every
// decoration's meshes (static LOD levels once per figure, one mesh per figure
// and shape) and impostor plans, one prebuilt texture per cache key, and the
// decorations' textures.
// shape_texels: the impostor shape groups' allowed error, in frame texels.
void prebuild_decorations(NeighbourMapUpload& upload, NativePrebuiltBuilder& builder, float shape_texels);
void prebuild_texture(NativePrebuiltBuilder& builder, NeighbourMapUpload& upload,
    std::unordered_set<std::string>& seen, const NativeOwnedTextureView& view);
void prebuild_decoration_textures(NeighbourMapUpload& upload, NativePrebuiltBuilder& builder,
    std::unordered_set<std::string>& seen);
// The active map's object-space statics (Renderer::update_active_statics):
// the decoration piece a neighbour gets (static LOD meshes, impostor bake
// inputs; no textures, no terrain), built on a worker for one batch of
// catalog models, each from the live assembly the game resolved for an
// eligible object (prepare_live_static_candidate) in the object's own frame.
// take returns the finished piece of that generation once (null while
// building, after a failure or for another generation) with the model key of
// each piece object and the models that could not be prepared.
void active_statics_begin(std::uint64_t generation, std::vector<ActiveStaticsModelRequest> requests) noexcept;
std::unique_ptr<NeighbourPiece> active_statics_take(std::uint64_t generation, bool& failed, std::string& failure,
    std::vector<std::uint64_t>& models, std::vector<std::uint64_t>& failed_models) noexcept;
void active_statics_stop() noexcept;
bool bridge_neighbour_world_transform(std::array<float,16>& placement) noexcept;
void bridge_neighbour_diagnostic(const char* reason) noexcept;
}
