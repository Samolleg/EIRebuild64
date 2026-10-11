#pragma once
#include "engine_bridge.h"
#include "owned_terrain_front_end.h"

namespace OwnedRender {
using E1TerrainRead = HRESULT (__cdecl *)(void*, const char*, void*, std::uint32_t, std::uint32_t*);
// Synchronously copies completed source members. No owner/callback enters renderer assets.
bool E1PublishTerrainSource(const void* owner, std::uint64_t world, std::uint64_t revision,
    const char* project, const char* header, E1TerrainRead read, void* context,
    std::string& failure) noexcept;
void E1PublishTerrainAtlases(const void* owner, const std::vector<std::uint64_t>& surfaces) noexcept;
void E1RetireTerrain(const void* owner) noexcept;
bool E1PublishTerrainFrame(const void* owner, const owned::OwnedTerrainGeometryValues& values,
    const std::array<float,16>& placement) noexcept;
}
