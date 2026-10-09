#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Detached CPU data only. Never owns an engine terrain, MOB or renderer handle.
#pragma pack(push, 8)
namespace WorldBackground {
struct Point { double x = 0, y = 0; };
struct Identity { std::string profile, allod, zone, mpr; };
struct Placement { Identity identity; std::string component; Point origin; std::string mprPath,mobPath; double width=0,height=0; };
struct Travel { int exit = 0, destinationExit = 0; std::string destination; bool deploy = false; Point minimum, maximum; };
struct CatalogEntry { Identity identity; double width = 0, height = 0; std::vector<Travel> travel; std::string mob; };
std::vector<CatalogEntry> LayoutEntries(const std::vector<Placement>& layout);
struct Neighbor { CatalogEntry entry; Point offset; };
using Blob = std::vector<uint8_t>;
std::string Key(const std::string& value);
std::vector<Placement> ParseLayout(const std::string& json);
Identity ResolveActiveLayout(const std::vector<Placement>& layout,const Identity& observed);
// Reads only ASCII topology fields; CP1251 labels and unrelated sections are ignored.
std::vector<CatalogEntry> ParseCatalog(const std::string& mapText, const std::string& profile);
std::vector<Neighbor> Select(const std::vector<Placement>& layout,
    const std::vector<CatalogEntry>& catalog, const Identity& active);

struct Vertex { float x, y, z, nx, ny, nz; };
struct Material { uint32_t type; std::array<float, 10> values; };
struct Animation { uint16_t tile, phases; };
struct Sector {
    uint32_t x, y;
    std::vector<Vertex> land, overlay;
    std::array<uint8_t, 1089> overlayAlpha;
    std::array<uint16_t, 256> landTiles, overlayTiles, overlayMaterials;
};
struct Terrain {
    std::string name;
    uint32_t sectorsX = 0, sectorsY = 0, textures = 0, textureSize = 0, tileSize = 0;
    float maxZ = 0;
    std::vector<Material> materials;
    std::vector<uint32_t> terrainIds;
    std::vector<Animation> animations;
    std::vector<Sector> sectors;
    size_t Bytes() const;
};
Terrain DecodeMpr(const Blob& data);
// Read a member without changing mapped-resource globals. Returns false only if absent.
bool ReadMember(const Blob& archive, const std::string& name, Blob& result);

struct TexturePayload { std::string name; Blob mmp; };
struct Prepared { Neighbor placement; Terrain terrain; std::vector<TexturePayload> textures; };
struct Snapshot { std::vector<Prepared> neighbors; std::vector<std::string> diagnostics; size_t bytes = 0; };
// Provider must use the supplied explicit profile and exact catalog identity, never
// the current native map path. Missing/corrupt neighbors are isolated individually.
using ReadMpr = std::function<Blob(const Identity&)>;
using ReadTexture = std::function<Blob(const std::string& profile, const std::string& member)>;
class BackgroundData {
public:
    void Prepare(bool enabled, const std::string& json,
        const std::vector<CatalogEntry>& catalog, const Identity& active,
        const ReadMpr& read, size_t budgetBytes = 64u * 1024u * 1024u,
        const ReadTexture& textures = ReadTexture());
    void Clear();
    const Snapshot& Get() const { return snapshot_; }
private:
    Snapshot snapshot_;
};
}
#pragma pack(pop)
