#pragma once
#include "engine_bridge.h"
#include "owned_terrain_front_end.h"
#include "owned_terrain_decode.h"

namespace OwnedRender {
using E1TerrainRead = HRESULT (__cdecl *)(void*, const char*, void*, std::uint32_t, std::uint32_t*);
// Synchronously copies completed source members. No owner/callback enters renderer assets.
bool E1PublishTerrainSource(const void* owner, std::uint64_t world, std::uint64_t revision,
    const char* project, const char* header, E1TerrainRead read, void* context,
    std::string& failure) noexcept;
void E1PublishTerrainAtlases(const void* owner, const std::vector<std::uint64_t>& surfaces) noexcept;
void E1RetireTerrain(const void* owner) noexcept;
struct E1TerrainSectorValues {
    std::uint32_t sector = 0;
    std::vector<std::uint32_t> phases;
};
// Completed resident-sector values, copied without advancing game animation.
// An omitted sector is unavailable, never an invitation to reuse old phases.
bool E1PublishTerrainSectors(const void* owner,
    const std::vector<E1TerrainSectorValues>& values) noexcept;
std::shared_ptr<const owned::OwnedTerrainDynamicSector> E1TerrainSector(std::uint32_t sector) noexcept;
// Owns the borrowed decoder inputs for one synchronous native submission.
// Noncopyable because view.decode_context points to this object's context.
struct E1TerrainCommandSource {
    owned::NativeGeometryView view{};
    owned::OwnedTerrainSourceDecodeContext context;
    std::shared_ptr<const owned::TerrainCpuAsset> terrain;
    std::shared_ptr<const owned::OwnedTerrainGeometryValues> geometry;
    std::shared_ptr<const owned::OwnedTerrainDynamicSector> dynamic;
    std::shared_ptr<const owned::OwnedVisualSurface> atlas;
    std::shared_ptr<owned::OwnedTerrainPreparedResources> prepared;
    E1TerrainCommandSource() = default;
    E1TerrainCommandSource(const E1TerrainCommandSource&) = delete;
    E1TerrainCommandSource& operator=(const E1TerrainCommandSource&) = delete;
};
bool E1PrepareTerrainCommand(bool transparent, std::size_t index, E1TerrainCommandSource& output) noexcept;
// The host publishes camera/visibility facts; c56 chooses LOD and commands.
bool E1PublishTerrainView(const void* owner, const owned::OwnedTerrainFrameInputs& values) noexcept;
// Borrowed only during this world frame; invalidated by a new/failed publication.
const owned::OwnedTerrainFramePacket* E1TerrainView() noexcept;
bool E1PublishTerrainFrame(const void* owner, const owned::OwnedTerrainGeometryValues& values,
    const std::array<float,16>& placement) noexcept;
}
