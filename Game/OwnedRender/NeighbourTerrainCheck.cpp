#include "NeighbourTerrain.h"
#include "../../external/owned-renderer/Source/neighbour_maps.h"
#include "../../external/owned-renderer/Source/owned_terrain_decode.h"
#include <cstdio>
#include <filesystem>
#include <stdexcept>

static void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
static void check(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}
struct Decoded {
    std::uint64_t hash = 14695981039346656037ull;
    std::uint64_t commands = 0, vertices = 0, indices = 0;
    template<class T> void append(const std::vector<T>& data) {
        const auto* p = reinterpret_cast<const unsigned char*>(data.data());
        for (std::size_t i = 0; i < data.size() * sizeof(T); ++i) { hash ^= p[i]; hash *= 1099511628211ull; }
    }
    void decode(const owned::NativeGeometryView& view) {
        check(view.decode != nullptr && view.decode_context != nullptr, "real decoder callback");
        std::vector<owned::NativePackedGroup> p(view.position_groups), n(view.normal_groups);
        std::vector<owned::NativeTextureCoordinate> uv(view.texture_coordinate_count);
        std::vector<owned::NativeVertexComponent> c(view.component_count);
        std::vector<std::uint16_t> idx(view.index_count);
        std::vector<owned::NativeTerrainVertex> terrain(view.component_count);
        owned::NativeGeometryDecodeTarget target{};
        target.positions=p.data(); target.position_groups=view.position_groups;
        target.normals=n.data(); target.normal_groups=view.normal_groups;
        target.texture_coordinates=uv.data(); target.texture_coordinate_count=view.texture_coordinate_count;
        target.components=c.data(); target.component_count=view.component_count;
        target.indices=idx.data(); target.index_count=view.index_count;
        target.terrain_vertices=terrain.data(); target.terrain_vertex_count=view.component_count;
        check(view.decode(view.decode_context, target), "decode terrain command");
        for (auto index : idx) check(index < c.size(), "index in component range");
        for (const auto& group : p) {
            const float* values = reinterpret_cast<const float*>(&group);
            for (std::size_t i=0; i<sizeof(group)/sizeof(float); ++i) check(std::isfinite(values[i]), "finite positions");
        }
        append(p); append(n); append(uv); append(c); append(idx); append(terrain);
        ++commands; vertices += c.size(); indices += idx.size();
    }
};
static void prepare(void* opaque, std::uint32_t, std::uint32_t, const owned::NativeGeometryView& view) {
    static_cast<Decoded*>(opaque)->decode(view);
}
static bool submit(void* opaque, const owned::NativeGeometryView& view, owned::NativeGeometryResult&) {
    check(view.world.owner && !view.world.owner->expired(), "live world owner");
    check(view.world.object_to_world[3] == 41 && view.world.object_to_world[7] == 62, "host placement plus neighbour offset");
    check(view.owned_texture.resource && view.owned_texture.mips && view.owned_texture.mip_count, "real texture provenance");
    static_cast<Decoded*>(opaque)->decode(view);
    return true;
}
int wmain(int argc, wchar_t** argv) try {
    check(argc >= 2, "pass MPR file followed by ordered texture archives");
    auto store = std::make_shared<owned::ResourceStore>();
    owned::ResourceStoreConfig config;
    for (int i=2; i<argc; ++i) config.archives.emplace_back(argv[i]);
    owned::ResourceFailure failure;
    check(store->open(config, failure), failure.message);
    const auto path = std::filesystem::path(argv[1]);
    owned::ResourceBlob blob;
    check(store->read_loose_file(path.wstring(), path.filename().string(), blob, failure), failure.message);
    auto terrain = std::make_shared<owned::TerrainCpuAsset>();
    check(owned::parse_terrain_asset(blob, path.filename().string(), *terrain, failure), failure.message);
    check(owned::validate_terrain_asset_descriptor_once(*terrain), "terrain descriptor validation");
    terrain->pages.resize(terrain->texture_resources.size());
    for (std::size_t i=0; i<terrain->pages.size(); ++i) {
        auto& page = terrain->pages[i];
        page.store = store;
        const auto name = terrain->texture_resources[i] + ".mmp";
        if (!store->locate(name, page.location, failure) || !page.ensure_upload_bytes(failure, false))
            page = {}; // Same optional-page contract as OwnedResourceLoader.
    }
    auto map = std::make_shared<owned::OwnedMapSnapshot>();
    map->terrain = terrain;
    owned::NeighbourMapUpload upload;
    upload.epoch=7; upload.map=map; upload.selection.x=40; upload.selection.y=60;
    std::shared_ptr<const void> lifetime=map;
    std::weak_ptr<const void> owner=lifetime;
    const std::array<float,16> matrix{1,0,0,1, 0,1,0,2, 0,0,1,3, 0,0,0,1};
    std::array<float,16> actual;
    std::uint32_t part=0;
    const char* why=nullptr;
    Decoded prepared, submitted;
    check(!owned::bridge_neighbour_world_transform(actual), "no placement before publication");
    check(!owned::bridge_upload_neighbour_terrain(upload, owner, matrix, submit, &submitted, part, why), "unpublished world rejected");
    OwnedRender::PublishTerrainPlacement(terrain.get(), matrix);
    check(owned::bridge_neighbour_world_transform(actual) && actual == matrix, "published placement values");
    OwnedRender::RetireTerrainPlacement(map.get());
    check(owned::bridge_neighbour_world_transform(actual), "unrelated owner does not retire active world");
    owned::bridge_prepare_neighbour_terrain(upload, prepare, &prepared);
    check(prepared.commands > 0 && prepared.vertices > 0 && prepared.indices > 0, "nonempty authored terrain");
    check(owned::bridge_upload_neighbour_terrain(upload, owner, matrix, submit, &submitted, part, why), why ? why : "upload");
    check(!why && prepared.commands == submitted.commands && prepared.hash == submitted.hash, "worker and submit decode identical geometry");
    OwnedRender::RetireTerrainPlacement(terrain.get());
    check(!owned::bridge_neighbour_world_transform(actual), "terrain Free retires placement");
    check(!owned::bridge_upload_neighbour_terrain(upload, owner, matrix, submit, &submitted, part, why), "retired placement rejected");
    std::printf("PASS: %s sectors=%zu textures=%zu commands=%llu vertices=%llu indices=%llu geometry_hash=%016llx; no GPU/window\n",
        path.filename().string().c_str(), terrain->sectors.size(), terrain->pages.size(),
        prepared.commands, prepared.vertices, prepared.indices, prepared.hash);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
}
