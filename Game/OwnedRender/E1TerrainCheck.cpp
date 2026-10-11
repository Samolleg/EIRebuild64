#include "E1Terrain.h"
#include "E1TerrainState.h"
#include "E1World.h"
#include "E1Surface.h"
#include "world_mist_grid.h"
#include <cstdio>
#include <filesystem>
#include <limits>
#include <cstring>
#include <cstdlib>

namespace {
int checks = 0;
struct ExitOwner {
    std::unique_ptr<OwnedRender::E1Surface> surface;
    ~ExitOwner() {
        if (!surface) return;
        const auto id = surface->Id();
        if (!OwnedRender::E1SurfaceValue(id)) std::_Exit(2);
        surface.reset();
        if (OwnedRender::E1SurfaceValue(id)) std::_Exit(3);
        std::puts("Surface global-owner teardown PASS");
    }
} exitOwner;
void checkAt(bool v, int line) { ++checks; if (!v) { std::printf("Failed source line: %d\n", line); throw checks; } }
#define check(...) checkAt((__VA_ARGS__), __LINE__)
bool decodeCommand(const owned::NativeGeometryView& view) {
    using namespace owned;
    std::vector<NativePackedGroup> positions(view.position_groups), normals(view.normal_groups);
    std::vector<NativeTextureCoordinate> uvs(view.texture_coordinate_count);
    std::vector<NativeVertexComponent> components(view.component_count);
    std::vector<std::uint16_t> indices(view.index_count);
    std::vector<NativeTerrainVertex> vertices(view.component_count);
    NativeGeometryDecodeTarget target;
    target.positions = positions.data(); target.position_groups = view.position_groups;
    target.normals = normals.data(); target.normal_groups = view.normal_groups;
    target.texture_coordinates = uvs.data(); target.texture_coordinate_count = view.texture_coordinate_count;
    target.components = components.data(); target.component_count = view.component_count;
    target.indices = indices.data(); target.index_count = view.index_count;
    target.terrain_vertices = vertices.data(); target.terrain_vertex_count = view.component_count;
    return view.decode && view.decode(view.decode_context, target) &&
        std::all_of(indices.begin(), indices.end(), [&](auto index){return index < components.size();});
}
struct Reader {
    owned::MemoryResourceArchive archive;
    bool fail = false, change = false, edited = false;
    unsigned reads = 0;
    static HRESULT __cdecl read(void* opaque, const char* name, void* destination,
        std::uint32_t capacity, std::uint32_t* size) {
        auto& self = *static_cast<Reader*>(opaque); ++self.reads;
        if (self.fail) return E_FAIL;
        owned::ResourceBlob blob; owned::ResourceFailure failure;
        if (!self.archive.resolve(name, blob, failure) || !blob.bytes) return E_FAIL;
        *size = static_cast<std::uint32_t>(blob.bytes->size());
        if (destination) {
            if (capacity < *size) return E_INVALIDARG;
            memcpy(destination, blob.bytes->data(), *size);
            if (self.edited && std::string(name).size() > 3 && std::string(name).substr(std::string(name).size()-3) == ".mp") {
                float z; memcpy(&z, static_cast<char*>(destination)+4, sizeof(z)); z *= 1.25f;
                memcpy(static_cast<char*>(destination)+4, &z, sizeof(z));
            }
            if (self.change) --*size;
        }
        return S_OK;
    }
};
}
int wmain(int argc, wchar_t** argv) try {
    check(argc == 2);
    using namespace owned; using namespace OwnedRender;
    Microsoft::WRL::ComPtr<ID3D11Device> device, foreignDevice;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL level;
    check(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &device, &level, &context)));
    check(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &foreignDevice, &level, nullptr)));
    D3D11_TEXTURE2D_DESC textureDesc{};
    textureDesc.Width = textureDesc.Height = 2; textureDesc.ArraySize = textureDesc.MipLevels = 1;
    textureDesc.SampleDesc.Count = 1; textureDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const std::uint32_t pixels[]{0xff123456,0xffabcdef,0x00111111,0x80555555};
    D3D11_SUBRESOURCE_DATA initial{pixels, 8, 16};
    Microsoft::WRL::ComPtr<ID3D11Texture2D> image, resolved;
    check(SUCCEEDED(device->CreateTexture2D(&textureDesc, &initial, &image)));
    exitOwner.surface = std::make_unique<E1Surface>();
    check(exitOwner.surface->Complete(image.Get()));
    E1Surface surface;
    check(surface.Id() && !E1SurfaceValue(surface.Id()));
    check(surface.Complete(image.Get()));
    auto value = E1SurfaceValue(surface.Id());
    check(value && value->id == surface.Id() && value->generation && value->width == 2 && value->height == 2);
    check(value->pitch == 8 && value->format.dwRBitMask == 0xff0000 && !value->alpha_fully_opaque && !value->alpha_cut_mask);
    check(SUCCEEDED(E1ResolveSurface(nullptr, device.Get(), value->id, value->generation, resolved)) && resolved == image);
    check(FAILED(E1ResolveSurface(nullptr, foreignDevice.Get(), value->id, value->generation, resolved)) && !resolved);
    check(FAILED(E1ResolveSurface(nullptr, device.Get(), value->id, value->generation+1, resolved)) && !resolved);
    surface.BeginWrite(); check(!E1SurfaceValue(surface.Id()));
    check(FAILED(E1ResolveSurface(nullptr, device.Get(), value->id, value->generation, resolved)));
    context->UpdateSubresource(image.Get(), 0, nullptr, pixels, 8, 16);
    check(surface.Complete(image.Get()));
    auto updated = E1SurfaceValue(surface.Id()); check(updated && updated->generation > value->generation);
    check(FAILED(E1ResolveSurface(nullptr, device.Get(), value->id, value->generation, resolved)));
    check(SUCCEEDED(E1ResolveSurface(nullptr, device.Get(), updated->id, updated->generation, resolved)));
    auto stagingDesc = textureDesc; stagingDesc.BindFlags = 0; stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    check(SUCCEEDED(device->CreateTexture2D(&stagingDesc, nullptr, &staging)));
    context->CopyResource(staging.Get(), resolved.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{}; check(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)));
    const bool correct = !memcmp(mapped.pData,pixels,8) && !memcmp(static_cast<char*>(mapped.pData)+mapped.RowPitch,pixels+2,8);
    context->Unmap(staging.Get(),0); check(correct);
    std::uint64_t retiredId = 0;
    { E1Surface transient; retiredId = transient.Id(); check(transient.Complete(image.Get())); }
    check(retiredId != surface.Id() && !E1SurfaceValue(retiredId));
    { E1Surface replacement; check(replacement.Id() != retiredId); check(!replacement.Complete(nullptr)); }
    textureDesc.Format = DXGI_FORMAT_R32_FLOAT;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> unsupported;
    check(SUCCEEDED(device->CreateTexture2D(&textureDesc,nullptr,&unsupported)));
    { E1Surface rejected; check(!rejected.Complete(unsupported.Get()) && !E1SurfaceValue(rejected.Id())); }
    ResourceStore store; ResourceStoreConfig config; ResourceFailure error;
    check(store.open(config, error));
    ResourceBlob blob; const std::filesystem::path path(argv[1]);
    check(store.read_loose_file(path.wstring(), path.filename().string(), blob, error));
    auto original = std::make_shared<TerrainCpuAsset>();
    check(parse_terrain_asset(blob, path.filename().string(), *original, error));
    Reader reader; check(reader.archive.open(blob, error));
    auto members = std::make_shared<TerrainCpuAsset>();
    check(parse_terrain_members(blob.identity, path.filename().string(), original->map_header_resource,
        [&](const std::string& name, ResourceBlob& part, ResourceFailure& why) {
            return reader.archive.resolve(name, part, why);
        }, *members, error));
    check(members->sectors.size() == original->sectors.size() && members->objects.size() == original->objects.size());
    check(members->tile_types == original->tile_types && members->texture_resources == original->texture_resources);
    std::array<float,16> matrix{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    RetainedTerrainSourceSnapshot a, b; a.source_generation = b.source_generation = 1;
    a.source_revision = b.source_revision = 1; a.terrain = original; b.terrain = members;
    WorldMistGridCache first, second;
    auto g1 = first.get(a, matrix), g2 = second.get(b, matrix);
    check(g1 && g2 && g1->texels == g2->texels);
    check(g1->width == g2->width && g1->height == g2->height && g1->terrain_low_z == g2->terrain_low_z);
    check(first.get(a, matrix) == g1);
    auto moved = matrix; moved[3] = 12; moved[7] = -8; moved[11] = 3;
    auto g3 = first.get(a, moved);
    check(g3 && g3 != g1 && g3->generation > g1->generation);
    check(g3->origin[0] == g1->origin[0]+12 && g3->origin[1] == g1->origin[1]-8);
    check(g3->terrain_low_z == g1->terrain_low_z+3);
    ++a.source_revision; auto g4 = first.get(a, moved);
    check(g4 && g4 != g3 && g4->generation > g3->generation);
    first.retire(); check(first.get(a, moved)->generation > g4->generation);
    auto invalid = matrix; invalid[0] = std::numeric_limits<float>::quiet_NaN();
    check(!first.get(a, invalid));
    check(!first.get({}, matrix));
    // Analytic one-cell liquid source: separable 5-tap blur keeps its weights.
    auto water = std::make_shared<TerrainCpuAsset>();
    water->objects.resize(1); water->objects[0].type = 2;
    water->tile_types.push_back(0); water->sectors.resize(1);
    auto& sector = water->sectors[0]; sector.bounds.minimum = {0,0,2};
    sector.bounds.maximum = {32,32,2}; sector.average_z = 2;
    sector.vertices.push_back({16,16,2}); sector.tiles.resize(1); sector.tiles[0].object_id = 0;
    RetainedTerrainSourceSnapshot liquid; liquid.source_generation = 1; liquid.source_revision = 1; liquid.terrain = water;
    WorldMistGridCache liquidCache;
    auto open = liquidCache.get(liquid, matrix); check(open && open->width == 9 && open->height == 9);
    const auto center = (4*open->width+4)*4;
    check(open->texels[center] == 0 && open->texels[center+1] == 36.f/256 &&
        open->texels[center+2] == 72.f/256 && open->texels[center+3] == 36.f/256);
    auto swamp = std::make_shared<TerrainCpuAsset>(*water); swamp->tile_types[0] = kTerrainTypeSwamp;
    liquid.terrain = swamp; // Identity reuse with a different asset must invalidate cache, too.
    auto swampGrid = liquidCache.get(liquid, matrix);
    check(swampGrid && swampGrid != open && swampGrid->texels[center] == 36.f/256 && swampGrid->texels[center+1] == 0);
    auto lava = std::make_shared<TerrainCpuAsset>(*water); lava->objects[0].self_illumination = 1;
    liquid.terrain = lava;
    auto lavaGrid = liquidCache.get(liquid, matrix);
    check(lavaGrid && std::all_of(lavaGrid->texels.begin(), lavaGrid->texels.end(), [](float v){return v == 0;}));
    int owner = 0, foreign = 0; std::string why;
    AmbientWorldSource ambient;
    check(!bridge_ambient_world_source(ambient) && !bridge_world_mist_grid());
    check(E1PublishTerrainSource(&owner, 10, 1, path.filename().string().c_str(),
        original->map_header_resource.c_str(), Reader::read, &reader, why));
    const unsigned reads = reader.reads;
    check(E1PublishTerrainSource(&owner, 10, 1, path.filename().string().c_str(),
        original->map_header_resource.c_str(), Reader::read, &reader, why) && reads == reader.reads);
    E1WorldFrame frame; frame.camera.valid = true; frame.camera.world_to_clip = matrix;
    E1BeginFrame();
    OwnedTerrainGeometryValues geometry;
    geometry.revision = 1; geometry.layer_shifts.assign(original->objects.size(), 0.f);
    geometry.animation.wind_direction[0] = .4f; geometry.animation.wind_direction[1] = -.2f;
    geometry.animation.wind_force_normalized = .7f;
    check(!E1PublishTerrainFrame(&owner, geometry, matrix)); // no world draw yet
    {
        E1WorldDraw draw(&owner, 10, frame);
        check(E1PublishTerrainFrame(&owner, geometry, matrix));
        check(bridge_ambient_world_source(ambient) && ambient.terrain && ambient.geometry && !ambient.map);
        check(ambient.wind_direction[0] == .4f && ambient.wind_direction[1] == -.2f && ambient.wind_force_normalized == .7f);
        auto grid = bridge_world_mist_grid(); check(grid && grid->texels == g1->texels);
        OwnedTerrainFrameInputs view;
        view.camera_position = {10000.f, 10000.f, 10000.f};
        view.lod_distances = {1.f, 2.f}; view.maximum_lod = 2;
        view.runtime_texture_count = original->texture_count;
        view.object_to_clip = {2.f/(original->sector_count_x*32.f),0,0,-1,
            0,2.f/(original->sector_count_y*32.f),0,-1,0,0,.0001f,.5f,0,0,0,1};
        view.game_visibility_domain_present = true;
        view.game_visibility_rows.assign(original->sector_count_y,
            {0,static_cast<int>(original->sector_count_x)-1});
        check(!E1TerrainView());
        check(!E1PublishTerrainView(&foreign, view) && !E1TerrainView());
        check(E1PublishTerrainView(&owner, view));
        const auto* packet = E1TerrainView();
        check(packet && packet->status == OwnedTerrainPacketStatus::ready && packet->spatial_ready);
        check(!packet->base_commands.empty() && !packet->spatial_parts.empty());
        auto spatialPacket = *packet;
        bool boundsReady = true;
        for (const auto* commands : {&spatialPacket.base_commands, &spatialPacket.transparent_commands})
            for (const auto& command : *commands) {
                NativeWorldBounds local, world;
                boundsReady = boundsReady && owned_terrain_spatial_bounds(spatialPacket, command, local, world) &&
                    local.valid && world.valid;
            }
        check(boundsReady);
        check(packet->inputs.source_generation && packet->inputs.source_revision == 1 && packet->inputs.world_frame_serial);
        check(std::all_of(packet->sectors.begin(),packet->sectors.end(),[](const auto& s){return s.lod == 2;}));
        bridge_set_terrain_lod_enabled(false);
        check(!E1TerrainView());
        check(E1PublishTerrainView(&owner, view));
        packet = E1TerrainView(); check(packet && packet->inputs.force_full_detail);
        check(std::all_of(packet->sectors.begin(),packet->sectors.end(),[](const auto& s){return s.lod == 0 && !s.lu_higher && !s.rd_higher;}));
        bridge_set_terrain_lod_enabled(false); check(E1TerrainView() == packet);
        bridge_set_terrain_lod_enabled(true); check(!E1TerrainView());
        check(E1PublishTerrainView(&owner, view) && !E1TerrainView()->inputs.force_full_detail);
        auto hidden = view; hidden.game_visibility_rows.clear();
        check(E1PublishTerrainView(&owner, hidden));
        // c56 selects render envelopes independently of the game's sector
        // admission. Keep the game-facing decisions, but render the source
        // terrain visible to this camera, as the c56 world path does.
        check(std::none_of(E1TerrainView()->sectors.begin(), E1TerrainView()->sectors.end(),
            [](const auto& sector) { return sector.visible; }));
        check(!E1TerrainView()->base_commands.empty());
        auto offscreen = view;
        offscreen.object_to_clip[3] = 100.f;
        check(E1PublishTerrainView(&owner, offscreen));
        check(E1TerrainView()->base_commands.empty() && E1TerrainView()->transparent_commands.empty());
        auto badView = view; badView.camera_position[0] = std::numeric_limits<float>::quiet_NaN();
        check(!E1PublishTerrainView(&owner, badView) && !E1TerrainView());
        check(E1PublishTerrainView(&owner, view));
        check(E1PublishTerrainFrame(&owner, geometry, matrix) && !E1TerrainView());
        check(E1PublishTerrainView(&owner, view));
        std::vector<std::uint64_t> atlases(original->texture_count+2u, 0); atlases[0] = surface.Id();
        std::vector<E1TerrainSectorValues> phaseRows;
        for (std::uint32_t i = 0; i < original->sectors.size(); ++i) {
            E1TerrainSectorValues row; row.sector = i;
            row.phases.assign(original->sectors[i].animation_count, 0);
            phaseRows.push_back(std::move(row));
        }
        check(!E1TerrainSector(0));
        check(E1PublishTerrainSectors(&owner, phaseRows));
        auto firstPhases = E1TerrainSector(0);
        check(firstPhases && firstPhases->captured && firstPhases->phase_values.revision);
        check(firstPhases->animation_phases == phaseRows[0].phases);
        check(firstPhases->animation.wind_force_normalized == .7f);
        check(!E1TerrainSector(static_cast<std::uint32_t>(phaseRows.size())));
        check(E1PublishTerrainSectors(&owner, phaseRows));
        check(E1TerrainSector(0)->phase_values.revision == firstPhases->phase_values.revision);
        auto duplicate = phaseRows; duplicate.push_back(phaseRows[0]);
        check(!E1PublishTerrainSectors(&owner, duplicate) && !E1TerrainSector(0));
        auto badPhases = phaseRows; badPhases[0].phases.push_back(0);
        check(!E1PublishTerrainSectors(&owner, badPhases) && !E1TerrainSector(0));
        badPhases = phaseRows; badPhases[0].sector = static_cast<std::uint32_t>(phaseRows.size());
        check(!E1PublishTerrainSectors(&owner, badPhases));
        check(E1PublishTerrainSectors(&owner, phaseRows));
        check(!E1PublishTerrainSectors(&foreign, phaseRows) && !E1TerrainSector(0));
        check(E1PublishTerrainSectors(&owner, {}) && !E1TerrainSector(0));
        check(E1PublishTerrainSectors(&owner, phaseRows));
        check(E1TerrainSector(0)->phase_values.revision != firstPhases->phase_values.revision);
        unsigned animatedChecks = 0;
        for (std::uint32_t i = 0; i < original->sectors.size() && !animatedChecks; ++i) {
            for (const auto& tile : original->sectors[i].tiles) {
                if (tile.animation_ordinal == UINT_MAX || tile.animation_phases < 2) continue;
                const auto old = E1TerrainSector(i);
                phaseRows[i].phases[tile.animation_ordinal] = 1;
                check(E1PublishTerrainSectors(&owner, phaseRows));
                const auto changed = E1TerrainSector(i);
                check(changed && changed->phase_values.revision != old->phase_values.revision);
                check(changed->animation_phases[tile.animation_ordinal] == 1 &&
                    old->animation_phases[tile.animation_ordinal] == 0);
                check(E1PublishTerrainSectors(&owner, phaseRows));
                check(E1TerrainSector(i)->phase_values.revision == changed->phase_values.revision);
                phaseRows[i].phases[tile.animation_ordinal] = tile.animation_phases;
                check(!E1PublishTerrainSectors(&owner, phaseRows) && !E1TerrainSector(i));
                phaseRows[i].phases[tile.animation_ordinal] = 0;
                check(E1PublishTerrainSectors(&owner, phaseRows));
                animatedChecks = 7; break;
            }
        }
        std::printf("Actual animated tile checks: %u\n", animatedChecks);
        check(E1PublishTerrainFrame(&owner, geometry, matrix) && !E1TerrainSector(0));
        check(E1PublishTerrainSectors(&owner, phaseRows));
        check(E1PublishTerrainView(&owner, view));
        std::shared_ptr<const OwnedVisualSurface> atlas;
        const char* atlasFailure = nullptr;
        check(!bridge_terrain_atlas(0, atlas, &atlasFailure) && !atlas && atlasFailure);
        E1TerrainCommandSource native;
        check(!E1PrepareTerrainCommand(false, 0, native) && !native.view.decode);
        // One real completed WARP surface per slot; this tests the material
        // resolver, not the map's texture art or an on-screen game frame.
        std::fill(atlases.begin(), atlases.end(), surface.Id());
        E1PublishTerrainAtlases(&owner, atlases);
        // A prepared mesh is not a renderable command until the actual pass
        // publishes its material. In particular vertex alpha must not become
        // texture alpha merely because a later UI pass changed the state.
        check(!E1PrepareTerrainCommand(false, 0, native));
        std::map<int,DWORD> renderStates{{D3DRENDERSTATE_ALPHABLENDENABLE, TRUE},
            {D3DRENDERSTATE_SRCBLEND, D3DBLEND_SRCALPHA},
            {D3DRENDERSTATE_DESTBLEND, D3DBLEND_INVSRCALPHA},
            {D3DRENDERSTATE_ALPHAREF, 173}, {D3DRENDERSTATE_SPECULARENABLE, TRUE},
            {D3DRENDERSTATE_CULLMODE, D3DCULL_CW}};
        const auto statesBefore = renderStates;
        auto baseMaterial = E1TerrainMaterialState(renderStates, 0);
        auto waterMaterial = E1TerrainMaterialState(renderStates, 2);
        check(renderStates == statesBefore);
        check(baseMaterial.alpha_op == D3DTOP_SELECTARG1 && waterMaterial.alpha_op == D3DTOP_SELECTARG2);
        check(E1TerrainMaterialState(renderStates, 1).alpha_op == D3DTOP_MODULATE);
        check(!E1TerrainMaterialState(renderStates, 3).enabled);
        check(baseMaterial.alpha_ref == 173 && baseMaterial.specular_enable && baseMaterial.cull_mode == D3DCULL_CW);
        check(!E1PublishTerrainMaterial(&owner, false, baseMaterial));
        for (auto* material : {&baseMaterial, &waterMaterial}) {
            material->replay_frame_state = true;
            material->captured_viewport = {13, 17, 640, 480};
            material->captured_depth_range = {.2f, .9f};
            material->captured_fog_enable = TRUE;
            material->captured_fog_color = 0x173951u;
        }
        check(E1PublishTerrainMaterial(&owner, false, baseMaterial));
        check(E1PublishTerrainMaterial(&owner, true, waterMaterial));
        check(E1PrepareTerrainCommand(false, 0, native));
        auto heldState = native.view.owned_fixed_state;
        check(heldState.enabled && heldState.replay_frame_state && !heldState.alpha_blend_enable &&
            heldState.source_blend == D3DBLEND_ONE && heldState.destination_blend == D3DBLEND_ZERO);
        check(heldState.captured_viewport == baseMaterial.captured_viewport &&
            heldState.captured_depth_range == baseMaterial.captured_depth_range &&
            heldState.captured_fog_color == baseMaterial.captured_fog_color && heldState.captured_fog_enable);
        E1TerrainCommandSource waterCommand;
        check(E1PrepareTerrainCommand(true, 0, waterCommand));
        check(waterCommand.view.owned_fixed_state.alpha_blend_enable &&
            waterCommand.view.owned_fixed_state.source_blend == D3DBLEND_SRCALPHA &&
            waterCommand.view.owned_fixed_state.alpha_op == D3DTOP_SELECTARG2);
        auto badMaterial = waterMaterial; badMaterial.captured_depth_range[1] = -1.f;
        check(!E1PublishTerrainMaterial(&owner, true, badMaterial));
        check(!E1PrepareTerrainCommand(true, 0, waterCommand));
        check(E1PrepareTerrainCommand(false, 0, native)); // independent base state
        check(!E1PublishTerrainMaterial(&foreign, false, baseMaterial));
        check(!E1PrepareTerrainCommand(false, 0, native));
        check(E1PublishTerrainMaterial(&owner, false, baseMaterial));
        check(E1PublishTerrainMaterial(&owner, true, waterMaterial));
        check(E1PublishTerrainFrame(&owner, geometry, matrix));
        check(E1PublishTerrainSectors(&owner, phaseRows) && E1PublishTerrainView(&owner, view));
        check(!E1PrepareTerrainCommand(false, 0, native) && !E1PrepareTerrainCommand(true, 0, waterCommand));
        check(E1PublishTerrainMaterial(&owner, false, baseMaterial));
        check(E1PublishTerrainMaterial(&owner, true, waterMaterial));
        check(E1PrepareTerrainCommand(false, 0, native));
        check(native.view.decode && native.view.decode_context == &native.context && native.atlas == updated);
        check(native.view.owned_texture.identity == updated->id && native.view.owned_texture.generation == updated->generation);
        check(native.view.owned_texture.surface == updated.get());
        check(SUCCEEDED(E1ResolveSurface(nullptr, device.Get(), native.view.owned_texture.identity,
            native.view.owned_texture.generation, resolved)) && resolved == image);
        check(native.context.geometry == native.geometry.get() && native.view.terrain_phase_values == &native.dynamic->phase_values);
        const auto& nv = native.view;
        std::vector<NativePackedGroup> positions(nv.position_groups), normals(nv.normal_groups);
        std::vector<NativeTextureCoordinate> uvs(nv.texture_coordinate_count);
        std::vector<NativeVertexComponent> components(nv.component_count);
        std::vector<std::uint16_t> indices(nv.index_count);
        std::vector<NativeTerrainVertex> vertices(nv.component_count);
        NativeGeometryDecodeTarget target;
        target.positions = positions.data(); target.position_groups = nv.position_groups;
        target.normals = normals.data(); target.normal_groups = nv.normal_groups;
        target.texture_coordinates = uvs.data(); target.texture_coordinate_count = nv.texture_coordinate_count;
        target.components = components.data(); target.component_count = nv.component_count;
        target.indices = indices.data(); target.index_count = nv.index_count;
        target.terrain_vertices = vertices.data(); target.terrain_vertex_count = nv.component_count;
        check(nv.decode(nv.decode_context, target));
        check(std::all_of(indices.begin(), indices.end(), [&](auto index){return index < components.size();}));
        --target.index_count; check(!nv.decode(nv.decode_context, target)); ++target.index_count;
        check(!E1PrepareTerrainCommand(false, E1TerrainView()->base_commands.size(), native) && !native.view.decode && !native.atlas);
        check(E1PrepareTerrainCommand(false, 0, native));
        check(bridge_terrain_atlas(0, atlas, &atlasFailure) && atlas == updated && !atlasFailure);
        surface.BeginWrite(); check(!bridge_terrain_atlas(0, atlas, &atlasFailure) && !atlas && atlasFailure);
        check(!E1PrepareTerrainCommand(false, 0, native) && !native.view.decode);
        check(surface.Complete(image.Get()));
        check(bridge_terrain_atlas(0, atlas, &atlasFailure) && atlas->generation > updated->generation);
        check(E1PrepareTerrainCommand(false, 0, native));
        check(native.view.owned_texture.identity == surface.Id() &&
            native.view.owned_texture.generation == atlas->generation &&
            native.view.owned_texture.generation != updated->generation);
        check(FAILED(E1ResolveSurface(nullptr, device.Get(), updated->id, updated->generation, resolved)) && !resolved);
        check(SUCCEEDED(E1ResolveSurface(nullptr, device.Get(), native.view.owned_texture.identity,
            native.view.owned_texture.generation, resolved)) && resolved == image);
        // Every material/sector command must carry the completed surface's
        // exact identity, including the ordered transparent range.
        bool allTextures = true;
        std::size_t baseCount = 0, transparentCount = 0;
        for (bool transparent : {false, true}) {
            const auto& commands = transparent ? E1TerrainView()->transparent_commands : E1TerrainView()->base_commands;
            for (std::size_t i = 0; i < commands.size(); ++i) {
                if (!E1PrepareTerrainCommand(transparent, i, native)) { allTextures = false; break; }
                const auto& texture = native.view.owned_texture;
                allTextures = allTextures && texture.surface && texture.identity == texture.surface->id &&
                    texture.generation == texture.surface->generation &&
                    SUCCEEDED(E1ResolveSurface(nullptr, device.Get(), texture.identity, texture.generation, resolved)) &&
                    resolved == image;
                if (transparent) ++transparentCount; else ++baseCount;
            }
        }
        check(allTextures && baseCount && transparentCount);
        check(E1PrepareTerrainCommand(false, 0, native));
        const auto heldMaterial = native.view.owned_fixed_state;
        baseMaterial.captured_fog_color = 0xabcdef;
        baseMaterial.captured_viewport = {0, 0, 1920, 1080};
        check(E1PublishTerrainMaterial(&owner, false, baseMaterial));
        check(native.view.owned_fixed_state.captured_fog_color == heldMaterial.captured_fog_color &&
            native.view.owned_fixed_state.captured_viewport == heldMaterial.captured_viewport);
        E1TerrainCommandSource nextState;
        check(E1PrepareTerrainCommand(false, 0, nextState));
        check(nextState.view.owned_fixed_state.captured_viewport == baseMaterial.captured_viewport);
        std::printf("Atlas descriptor/resolver: base=%zu transparent=%zu\n", baseCount, transparentCount);
        // Camera-visible nonresident sectors need phases only for animated materials.
        check(E1PublishTerrainSectors(&owner, {}));
        std::size_t geometryOnly = 0, missingPhases = 0;
        for (bool transparent : {false, true}) {
            const auto& commands = transparent ? E1TerrainView()->transparent_commands : E1TerrainView()->base_commands;
            for (std::size_t i = 0; i < commands.size(); ++i) {
                const auto& command = commands[i];
                const auto& material = original->sectors[command.source_sector].materials[command.material];
                bool needsPhases = false;
                float wave = 0;
                for (unsigned part = 0; part < 4; ++part) if (command.subpart_mask & (1u << part)) {
                    needsPhases |= material.subparts[part].required_animation_phases != 0;
                    for (const auto& envelope : material.subparts[part].envelopes)
                        wave = (std::max)(wave, envelope.maximum_wave_multiplier);
                }
                const bool ready = E1PrepareTerrainCommand(transparent, i, native);
                check(ready != needsPhases);
                if (needsPhases) {
                    ++missingPhases;
                    check(!native.view.decode && !native.view.owned_texture.surface);
                    continue;
                }
                ++geometryOnly;
                check(!native.dynamic && !native.view.terrain_phase_values &&
                    !native.view.terrain_animation_phases && !native.view.terrain.animation_phase_count &&
                    !native.view.terrain.animated_tiles_enabled);
                check(native.view.world.enabled && native.view.world.identity && native.view.world.owner &&
                    native.view.prepared_mesh && native.view.prepared_material &&
                    native.view.contribution.known && native.view.contribution.color);
                check(native.view.terrain.maximum_wind_multiplier == wave &&
                    (wave != 0 || !native.view.terrain.waves_enabled));
                check(native.view.world.object_to_world == matrix &&
                    native.view.world.world_to_clip == frame.camera.world_to_clip);
            }
        }
        check(geometryOnly != 0);
        std::printf("Nonresident terrain: ready=%zu deferred_phases=%zu\n", geometryOnly, missingPhases);
        // Decode both ranges without resident sector data, and keep a command
        // alive across a later publication. Its borrowed asset/cache owners stay pinned.
        for (bool transparent : {false, true}) {
            const auto& commands = transparent ? E1TerrainView()->transparent_commands : E1TerrainView()->base_commands;
            bool decoded = false;
            for (std::size_t i = 0; i < commands.size(); ++i) {
                if (!E1PrepareTerrainCommand(transparent, i, native)) continue;
                check(decodeCommand(native.view));
                const auto identity = native.view.prepared_identity;
                const auto mesh = native.view.prepared_mesh;
                const auto revision = native.view.world.record_revision;
                E1TerrainCommandSource again;
                check(E1PrepareTerrainCommand(transparent, i, again));
                check(again.view.prepared_mesh == mesh && again.view.world.record_revision == revision &&
                    again.view.prepared_identity.lifecycle_generation == identity.lifecycle_generation);
                check(decodeCommand(native.view) && decodeCommand(again.view));
                check(E1PublishTerrainSectors(&owner, phaseRows));
                check(decodeCommand(native.view));
                check(E1PublishTerrainSectors(&owner, {}));
                decoded = true; break;
            }
            check(decoded);
        }
        check(E1PublishTerrainSectors(&owner, phaseRows));
        check(!E1PrepareTerrainCommand(true, E1TerrainView()->transparent_commands.size(), native) &&
            !native.view.owned_texture.identity && !native.view.owned_texture.generation && !native.view.owned_texture.surface);
        check(!bridge_terrain_atlas(static_cast<std::uint32_t>(atlases.size()), atlas, &atlasFailure) && !atlas);
        E1PublishTerrainAtlases(&foreign, {}); check(bridge_terrain_atlas(0, atlas));
        E1PublishTerrainAtlases(&owner, {}); check(!bridge_terrain_atlas(0, atlas));
        E1RetireTerrain(&foreign); check(bridge_world_mist_grid() == grid);
        draw.Complete(); check(bridge_world_mist_grid() == grid);
    }
    E1BeginFrame(); check(!E1TerrainSector(0)); check(!E1TerrainView()); check(!bridge_ambient_world_source(ambient) && !ambient.terrain && !bridge_world_mist_grid());
    {
        E1WorldDraw draw(&owner, 10, frame);
        check(E1PublishTerrainFrame(&owner, geometry, matrix));
        // Abort must invalidate the terrain frame as well.
    }
    check(!bridge_world_mist_grid());
    {
        E1WorldDraw draw(&owner, 10, frame);
        check(E1PublishTerrainFrame(&owner, geometry, matrix));
        auto bad = geometry; bad.animation.wind_phase = std::numeric_limits<float>::infinity();
        check(!E1PublishTerrainFrame(&owner, bad, matrix) && !bridge_world_mist_grid());
        check(E1PublishTerrainFrame(&owner, geometry, matrix));
        reader.fail = true;
        check(!E1PublishTerrainSource(&owner, 10, 2, path.filename().string().c_str(),
            original->map_header_resource.c_str(), Reader::read, &reader, why));
        check(!why.empty() && !bridge_ambient_world_source(ambient) && !bridge_world_mist_grid());
        reader.fail = false; reader.change = true;
        check(!E1PublishTerrainSource(&owner, 10, 2, path.filename().string().c_str(),
            original->map_header_resource.c_str(), Reader::read, &reader, why));
        reader.change = false; reader.edited = true;
        check(E1PublishTerrainSource(&owner, 11, 2, path.filename().string().c_str(),
            original->map_header_resource.c_str(), Reader::read, &reader, why));
        check(!bridge_world_mist_grid());
        check(!E1PublishTerrainFrame(&owner, geometry, matrix)); // world 10 cannot publish world 11 terrain
        E1WorldDraw nextWorld(&owner, 11, frame);
        check(E1PublishTerrainFrame(&owner, geometry, matrix));
        check(bridge_ambient_world_source(ambient) && ambient.terrain->maximum_z == original->maximum_z*1.25f);
        check(ambient.terrain != original && ambient.terrain != members);
        E1RetireTerrain(&owner);
        check(!bridge_ambient_world_source(ambient) && !bridge_world_mist_grid());
        check(!E1PublishTerrainFrame(&owner, geometry, matrix));
    }
    std::printf("E1 terrain: %d checks PASS; sectors=%zu grid=%ux%u; WARP surface pixels/lifetime; no window/game\n", checks,
        original->sectors.size(), g1->width, g1->height);
    return 0;
} catch (int n) { std::printf("E1 terrain check %d FAILED\n", n); return 1; }
