#pragma once
#include "../Navigation/Query.h"
#include <map>
#include "Plot.h"

class CMapObject;
class CUnit;
class CAIMap;
class CWorldServer;
class NavigationWorld {
    Navigation::Geometry terrain;
    std::map<int,std::vector<Navigation::Solid>> objects;
    std::map<std::string,Navigation::Bytes> sources;
    std::map<std::pair<int,int>,std::unique_ptr<Navigation::Mesh>> meshes;
    std::string scene, error;
    std::uint64_t generation = 1;
    unsigned failures = 0;
    void Changed();
    Navigation::Mesh* ForProfile(float radius,int aiClass,const char* phase);
    Navigation::Mesh* ForUnit(CUnit* unit,int aiClass = 3,const char* phase = "late");
public:
    void Source(const std::string& name,const void* data,size_t size);
    void MobSource(const char* path);
    void Scene(const char* mpr,const char* mob);
    Navigation::Geometry& Terrain() { return terrain; }
    void Object(CMapObject* object,bool remove,CAIMap& map);
    bool Prepare(CWorldServer& world,const char* phase = "prepare");
    const std::string& Error() const { return error; }
    Navigation::Point Position(CUnit* unit,CAIMap& map) const;
    Navigation::Route Find(CUnit* unit,Navigation::Point from,Navigation::Point goal,float maxLength,float goalRadius,const float* costs=nullptr,int aiClass=3);
    bool CanPlace(CUnit* unit,Navigation::Point point,int aiClass=3);
    bool Step(CUnit* unit,Navigation::Point from,Navigation::Point desired,Navigation::Location& result);
    bool Restore(CUnit* unit,CAIMap& map);
};
