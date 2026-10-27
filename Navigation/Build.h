#pragma once
#include "Geometry.h"
#include "Cache.h"
#include <memory>
#include <cstddef>
#include "../external/recastnavigation/Detour/Include/DetourNavMesh.h"
#include "../external/recastnavigation/Detour/Include/DetourNavMeshQuery.h"

namespace Navigation {
using Bytes = std::vector<std::uint8_t>;
struct MeshDeleter { void operator()(dtNavMesh* mesh) const { dtFreeNavMesh(mesh); } };
struct QueryDeleter { void operator()(dtNavMeshQuery* query) const { dtFreeNavMeshQuery(query); } };
struct Mesh {
    std::unique_ptr<dtNavMesh,MeshDeleter> data;
    mutable std::unique_ptr<dtNavMeshQuery,QueryDeleter> query;
    Profile profile;
    std::vector<Bytes> layers;
    std::unique_ptr<Cache> cache;
    std::string mapName, mobName, profileId; // Self-describing offline artifact identity.
    std::uint64_t generation = 0;
    std::uint32_t tiles = 0;
    // Build telemetry only; no change to the container or serialized addresses.
    std::uint32_t workers = 0;
    std::uint64_t rasterMs = 0, polygonMs = 0, buildMs = 0;
};
struct BuildRequest { Profile profile; };
std::vector<std::unique_ptr<Mesh>> BuildProfiles(const Geometry& geometry,const std::vector<BuildRequest>& requests);
std::unique_ptr<Mesh> Build(const Geometry& geometry,const Profile& profile,const std::string& key);
Bytes Store(const Mesh& mesh);
std::unique_ptr<Mesh> Load(Bytes bytes,const Profile& expected,
    const std::string& mapName,const std::string& mobName,const std::string& profileId);
}
