#pragma once
#include "../Navigation/Query.h"
#include "../Navigation/BodyTable.h"
#include "../Navigation/Profiles.h"
#include <filesystem>

class CMapObject;
class CUnit;
class CAIMap;
class NavigationBake;
class NavigationWorld {
    std::array<std::unique_ptr<Navigation::Mesh>,Navigation::AgentCount> meshes;
    Navigation::BodyTable bodyTable;
    std::map<Navigation::BodyKey,Navigation::BodySize> bodySizes;
    std::unique_ptr<NavigationBake> baker; // Allocated only in the offline tool.
    std::string mapName,mobName,error;
    std::uint64_t generation=0;
    unsigned failures=0;
    bool loaded=false;
    const Navigation::BodySize& Body(CUnit* unit);
    int Type(CUnit* unit,int aiClass);
    float Height(CUnit* unit,int aiClass=3);
    Navigation::Mesh* ForUnit(CUnit* unit,int aiClass=3);
public:
    NavigationWorld();
    ~NavigationWorld();
    Navigation::Geometry* Terrain(); // Null in game sessions.
    void Scene(const char* mpr,const char* mob);
    void Object(CMapObject* object,bool remove,CAIMap& map);
    std::uint64_t Bake(const std::filesystem::path& directory,const Navigation::Bytes& table,bool fingerprintOnly=false);
    bool Load();
    const std::string& Error() const { return error; }
    Navigation::Point Position(CUnit* unit,CAIMap& map) const;
    Navigation::Route Find(CUnit* unit,Navigation::Point from,Navigation::Point goal,float maxLength,float goalRadius,const float* costs=nullptr,int aiClass=3);
    bool CanPlace(CUnit* unit,Navigation::Point point,int aiClass=3);
    bool Step(CUnit* unit,Navigation::Point from,Navigation::Point desired,Navigation::Location& result);
    bool Restore(CUnit* unit,CAIMap& map);
};
