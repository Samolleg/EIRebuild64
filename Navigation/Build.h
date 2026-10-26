#pragma once
#include "Geometry.h"
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
    std::shared_ptr<const Geometry> geometry; // Current inputs, shared by profiles; never serialized.
    std::string fingerprint;
    std::uint64_t generation = 0;
    std::uint32_t tiles = 0;
    // Build telemetry only; no change to the container or serialized addresses.
    std::uint32_t workers = 0;
    std::uint64_t rasterMs = 0, polygonMs = 0;
};
std::unique_ptr<Mesh> Build(const Geometry& geometry,const Profile& profile,const std::string& key);
Bytes Store(const Mesh& mesh);
std::unique_ptr<Mesh> Load(const Bytes& bytes,const Profile& expected,const std::string& key);
// Canonical LE input bytes, also used by the host's SHA-256 fingerprint.
Bytes Inputs(const Geometry& geometry,const Profile& profile,const std::string& scene,const Bytes& source);
}
