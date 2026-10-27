#pragma once
#include "../Navigation/Query.h"
#include <map>
#include <tuple>
#include <set>
#include <filesystem>
#include "Plot.h"

class CMapObject;
class CUnit;
class CAIMap;
class CWorldServer;
namespace owned { struct FigureCpuAsset; struct ModelCpuAsset; struct AnimationClipAsset; }
class NavigationWorld {
    Navigation::Geometry terrain;
    std::shared_ptr<Navigation::Geometry> geometry;
    std::map<int,Navigation::Geometry> objects, pendingObjects;
    std::map<std::string,std::shared_ptr<const owned::FigureCpuAsset>> figures;
    std::map<std::string,std::shared_ptr<const owned::ModelCpuAsset>> bodies;
    std::map<std::string,std::shared_ptr<const owned::AnimationClipAsset>> poses;
    struct BodySize { float radius=0; float height[3]{}; };
    std::map<std::tuple<std::string,float,float,float>,BodySize> bodySizes;
    std::map<std::string,Navigation::Bytes> sources;
    std::map<int,std::unique_ptr<Navigation::Mesh>> meshes;
    std::string scene, mapName, mobName, inputKey, error;
    std::set<std::string> modelInputs;
    std::set<std::string> normalizedBounds; // Offline release diagnostics, one entry per object/part.
    std::uint64_t generation = 1;
    unsigned failures = 0;
    void Changed();
    void SyncObjects();
    void ModelInputs(CMapObject* object);
    void Snapshot();
    std::string Fingerprint(int type);
    std::string Filename(int type) const;
    std::shared_ptr<const owned::FigureCpuAsset> Figure(CMapObject* object,const char* part);
    const BodySize& Body(CUnit* unit);
    int Type(CUnit* unit,int aiClass);
    float Height(CUnit* unit,int aiClass=3);
    Navigation::Mesh* ForProfile(int type,const char* phase);
    Navigation::Mesh* ForUnit(CUnit* unit,int aiClass = 3,const char* phase = "late");
public:
    void Source(const std::string& name,const void* data,size_t size);
    void Scene(const char* mpr,const char* mob);
    Navigation::Geometry& Terrain() { return terrain; }
    void Object(CMapObject* object,bool remove,CAIMap& map);
    std::uint64_t Bake(CWorldServer& world,const std::filesystem::path& directory);
    bool Prepare(CWorldServer& world,const char* phase = "prepare");
    const std::string& Error() const { return error; }
    Navigation::Point Position(CUnit* unit,CAIMap& map) const;
    Navigation::Route Find(CUnit* unit,Navigation::Point from,Navigation::Point goal,float maxLength,float goalRadius,const float* costs=nullptr,int aiClass=3);
    bool CanPlace(CUnit* unit,Navigation::Point point,int aiClass=3);
    bool Step(CUnit* unit,Navigation::Point from,Navigation::Point desired,Navigation::Location& result);
    bool Restore(CUnit* unit,CAIMap& map);
};
