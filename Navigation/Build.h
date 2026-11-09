#pragma once
#include "Geometry.h"

#include <memory>
#include <cstddef>
#include <tuple>
#include <set>
#include "../external/recastnavigation/Detour/Include/DetourNavMesh.h"
#include "../external/recastnavigation/Detour/Include/DetourNavMeshQuery.h"

namespace Navigation {
using Bytes = std::vector<std::uint8_t>;
struct MeshDeleter { void operator()(dtNavMesh* mesh) const { dtFreeNavMesh(mesh); } };
struct QueryDeleter { void operator()(dtNavMeshQuery* query) const { dtFreeNavMeshQuery(query); } };
struct Mesh {
    std::unique_ptr<dtNavMesh,MeshDeleter> data;
    mutable std::unique_ptr<dtNavMeshQuery,QueryDeleter> query;
    struct Scratch {
        std::vector<dtPolyRef> corridor,visible,refs;
        std::vector<float> points,cost,length;
        std::vector<unsigned char> flags,closed;
        std::vector<int> previous;
        std::vector<Location> nodes;
        std::vector<Route> incoming;
        std::vector<Obstacle> obstacles;
    };
    mutable Scratch scratch; // Queries on a mesh are serialized by the owner.
    Profile profile;
    std::shared_ptr<const Geometry> sources;
    std::set<int> excludedObjects;

    // Original support identity, in tile polygon order. Never inferred from
    // Detour's polygon fan height during queries.
    std::map<std::pair<int,int>,std::vector<std::vector<std::uint64_t>>> polygonSurfaces;
    std::map<int,bool> doorOpen;
    std::map<std::tuple<int,int,int,int>,unsigned short> doorFlags;
    unsigned char* replacementData=nullptr;
    int replacementSize=0;
    ~Mesh(){dtFree(replacementData);}
    std::string mapName, mobName, profileId; // Self-describing offline artifact identity.
    std::uint64_t generation = 0;
    std::uint32_t tiles = 0;
    // Build telemetry only; no change to the container or serialized addresses.
    std::uint32_t workers = 0, rebuilt = 0;
    Bytes tileKeys; // Baker-only signatures, never part of the game container.
    std::uint64_t rasterMs = 0, polygonMs = 0, buildMs = 0;
    std::array<std::uint64_t,5> stagesMs{}; // Recast raster, filters/compact, erosion, regions/contours/polys, detail; summed CPU time.
};
struct BuildRequest {
    Profile profile; std::vector<Point> columns;
    std::unique_ptr<Mesh> previous; // Consumed when the complete profile is reusable.
    Bytes tileKeys, previousBytes;
};
Bytes Digest(const Bytes& bytes);
struct SourceReuse {Bytes previous, keys;};
Bytes StoreSources(const Geometry& geometry,SourceReuse* reuse=nullptr);
std::shared_ptr<Geometry> LoadSources(Bytes bytes,std::array<double,3>* stages=nullptr);
void IndexSurfaces(Mesh& mesh);
void IndexSources(Geometry& geometry,float tileWidth);
std::unique_ptr<Mesh> RebuildTile(const Geometry& geometry,const Profile& profile,int x,int z);
void ReplaceTile(Mesh& mesh,Mesh& replacement,int x,int z);
void DoorState(Mesh& mesh,int object,bool open);
std::vector<std::unique_ptr<Mesh>> BuildProfiles(const Geometry& geometry,std::vector<BuildRequest>& requests);
std::unique_ptr<Mesh> Build(const Geometry& geometry,const Profile& profile,const std::string& key);
Bytes Store(const Mesh& mesh);
std::unique_ptr<Mesh> Load(Bytes bytes,const Profile& expected,
    const std::string& mapName,const std::string& mobName,const std::string& profileId);
}
