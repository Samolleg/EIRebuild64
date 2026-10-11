#include "E1Terrain.h"
#include "E1World.h"
#include "world_mist_grid.h"
#include <cstdio>
#include <filesystem>
#include <limits>
#include <cstring>

namespace {
int checks = 0;
void check(bool v) { ++checks; if (!v) throw checks; }
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
        E1RetireTerrain(&foreign); check(bridge_world_mist_grid() == grid);
        draw.Complete(); check(bridge_world_mist_grid() == grid);
    }
    E1BeginFrame(); check(!bridge_ambient_world_source(ambient) && !ambient.terrain && !bridge_world_mist_grid());
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
    std::printf("E1 terrain: %d checks PASS; sectors=%zu grid=%ux%u; no window/game\n", checks,
        original->sectors.size(), g1->width, g1->height);
    return 0;
} catch (int n) { std::printf("E1 terrain check %d FAILED\n", n); return 1; }
