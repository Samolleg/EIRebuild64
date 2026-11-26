#pragma once
// Hosted plants (issue #251 3c): ground-cover meshes grown on one host model
// (Tools/export-hosted-plants.py -> Data/eid3d_hosted/hosted_plants.bin,
// staged as eid3d_hosted\hosted_plants.bin beside ddraw.dll and the x64 host,
// each of which reads its own ranges) and placed on that model's map objects. The
// scene half finds the hosts in the retained visual frame and answers one
// record per host object; the renderer uploads the models those hosts use
// (per map, on demand), appends the visible hosts to the cover pool
// (gcHostedCS) and draws each hosted kind with the plant cards' programs by
// vertex pulling (GC_HOSTED_VS).
//
// Contract per host object (checked once per model and once per placement):
// the installed model must be the one the plants were grown on (fingerprint:
// parts, triangle counts, mean-shape bounds); its placement must be a
// rotation with positive up (the cover frame's tilt) times a per-axis scale.
// A host whose complection is a per-axis scale of the mean shape (fit within
// 2 cm) is instanced with that scale; a host that is not (the arch stwa3)
// takes its own copy of the plant's vertices, rebound from the exported
// triangle binding onto its posed rest mesh (once per placement, off the
// frame thread). Anything else omits that host's plants with one log line.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace owned::ground_cover {

// ---- The exported data (little-endian, packed as written by the tool) ----
struct HostedFileHeader {
    char magic[4];               // "HPL2"
    std::uint32_t version;       // kHostedFileVersion
    std::uint32_t hosts, parts, models, vertex_words, bind_words, indices, textures, texture_bytes;
};
struct HostedHostRecord {        // a host model: its parts' fingerprint and mean-shape bounds
    char model[16], texture[24];
    std::uint32_t first_part, parts;
    float lo[3], hi[3];
};
struct HostedPartRecord { char name[32]; std::uint32_t triangles, flip; };
// One grown plant model: its hosted kind (kHostedKinds index), host, the metres
// a mesh unit (vertex positions are int16 / 16384 mesh units), 6 vertex words a
// vertex (the desert plants' layout; art 5 samples its own texture), its
// triangle list (local vertex indices), for a host that is no pure per-axis
// scale 5 binding words a vertex, and 1 + its texture (0: none).
struct HostedModelRecord {
    std::uint32_t kind, host;
    float scale;
    std::uint32_t first_vertex, vertices, first_index, indices, first_bind, binds, texture;
};
// A model's own art (the lichen cards' colony atlas): DXT1 with 1-bit alpha,
// its mip chain (coverage-preserving alpha) packed level after level.
struct HostedTextureRecord { std::uint32_t width, height, mips, first_byte, bytes; };
constexpr std::uint32_t kHostedFileVersion = 2;
static_assert(sizeof(HostedFileHeader) == 40 && sizeof(HostedHostRecord) == 72 && sizeof(HostedPartRecord) == 40 &&
              sizeof(HostedModelRecord) == 40 && sizeof(HostedTextureRecord) == 20, "the export tool's layout");

// The file's tables, read once (a few KB); the bulk ranges (vertex, binding
// and index words, texture levels) stay in the file and are read when a map
// uploads its models or a placement is rebound, so no address space holds
// them between uploads. Null when the file is absent or damaged (named once).
// A model id of kCaveColony kind is a terrain-placed cave lichen colony
// (ground cover meshes gm_cave_lichen_*), not a hosted model.
constexpr std::uint32_t kCaveColonyKind = 0xffu, kNoHost = 0xffffffffu;
constexpr std::uint32_t kHostedPadIndex = 0xffffffffu; // padding in a kind's index run (gcHostedPad)
struct HostedPlantData {
    std::wstring path;
    std::vector<HostedHostRecord> hosts;
    std::vector<HostedPartRecord> parts;
    std::vector<HostedModelRecord> models;
    std::vector<HostedTextureRecord> textures;
    std::uint64_t vertex_at = 0, bind_at = 0, index_at = 0, texture_at = 0; // file offsets of the sections
};
const HostedPlantData* hosted_plant_data() noexcept;
// One range of a model or texture from the file (false: unreadable, named once).
bool hosted_read_vertices(const HostedPlantData& data, const HostedModelRecord& model, std::vector<std::uint32_t>& words) noexcept;
bool hosted_read_binds(const HostedPlantData& data, const HostedModelRecord& model, std::vector<std::uint32_t>& words) noexcept;
bool hosted_read_indices(const HostedPlantData& data, const HostedModelRecord& model, std::vector<std::uint32_t>& indices) noexcept;
bool hosted_read_texture(const HostedPlantData& data, const HostedTextureRecord& texture, std::vector<std::uint8_t>& bytes) noexcept;

// ---- Per frame ----
// One host object's plants (bridge_hosted_plants): the model, the world
// origin of the host's model space and the metres a mesh unit (y axis), yaw
// inside the tilted frame and stretch x/z (relative to y), the model's z axis
// in the world (unit, z > 0), world bounds (centre, radius), a hash, and for a
// rebound host 1 + the first of its own vertices in HostedPlantFrame::rebound.
struct HostedPlantRecord {
    std::uint32_t model = 0;
    float root[4]{}, frame[4]{}, up[3]{}, bounds[4]{};
    std::uint32_t hash = 0, rebound = 0;
};
// records: one per host object of the world's entity list (order stable while
// the hosts are); rebound: per rebound placement its own positions (mesh units)
// and normals, two float4 a vertex, as long as its model; revision changes
// whenever records or rebound change.
struct HostedPlantFrame {
    std::vector<HostedPlantRecord> records;
    std::vector<float> rebound;
    std::uint64_t revision = 0;
};

// One host as gcHostedCS reads it (GcHosted, 112 bytes).
struct HostedPlantGpu {
    float root[4];
    float frame[4];
    float up[3];
    std::uint32_t index_base;    // 1 + the model's first index in the map's hosted index buffer
    float bounds[4];
    std::uint32_t kind_mesh, hash, mid_mesh, far_mesh; // the kind's pool and its tier pools (#275)
    std::uint32_t vertex_delta, pad[3]; // a rebound copy's offset from its model's vertices (mod 2^32)
    // LOD tiers (issue #275): 1 + the mid and far levels' first index (0: none)
    // and their largest deviation in mesh units (float bits).
    std::uint32_t mid_base, far_base, mid_error, far_error;
};
static_assert(sizeof(HostedPlantGpu) == 112, "HostedPlantGpu mirrors GcHosted");

} // namespace owned::ground_cover
