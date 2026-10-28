#pragma once
#include "../Navigation/Query.h"
#include "../Navigation/BodyTable.h"
#include "../Navigation/Profiles.h"
#include <filesystem>
#include <future>
#include <deque>
#include "../Navigation/Authoring.h"

class CMapObject;
class CWorldServer;
class CUnit;
class CAIMap;
class CAIRoute;
class CAIFullRoute;
struct SAIPath;
class NavigationBake;
class NavigationWorld {
    std::array<std::unique_ptr<Navigation::Mesh>,Navigation::AgentCount> meshes;
    Navigation::BodyTable bodyTable;
    std::map<Navigation::BodyKey,Navigation::BodySize> bodySizes;
    std::unique_ptr<NavigationBake> baker; // Allocated only in the offline tool.
    std::string mapName,mobName,error;
    std::uint64_t generation=0;
    unsigned failures=0,stepFailures=0;
    std::uint64_t nextStepLog=0;
    bool loaded=false;
    Navigation::Authoring authoring;
    std::map<int,std::vector<Navigation::Solid>> liveObjects;
    std::map<int,std::vector<Navigation::Solid>> appliedObjects;

    std::set<int> bakedStatic,dirtyObjects;
    unsigned updateProfile=0;
    std::shared_ptr<Navigation::Geometry> sources;
    std::set<int> absentStatic;
    std::set<std::tuple<int,int,int>> sourceDirty;
    std::future<std::unique_ptr<Navigation::Mesh>> sourceJob;
    std::tuple<int,int,int> sourceTask;
    std::map<int,int> liveDoors;
    struct Queue { Navigation::Point goal;int blocker=0; };
    std::map<int,Queue> queues;
    const std::vector<Navigation::Obstacle>* queryObstacles=nullptr;
    void UpdateSources(bool initial);
    bool Load(const std::filesystem::path& directory);

    void QueueBox(const Navigation::Solid& box);
    bool Dynamic(int id) const { return !bakedStatic.count(id); }
    void SyncObjects(bool initial=false);
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
    std::uint64_t Bake(const std::filesystem::path& directory,Navigation::Bytes& table,CWorldServer& world);
    bool Load();
    void Advance();
    float Radius(CUnit* unit) { return (std::max)(.05f,Body(unit).radius); }
    bool RandomGoal(CUnit* unit,Navigation::Point center,float radius,Navigation::Point& goal);
    bool Place(CUnit* unit,CAIMap& map);
    const std::string& Error() const { return error; }
    Navigation::Point Position(CUnit* unit,CAIMap& map) const;
    Navigation::Route Find(CUnit* unit,Navigation::Point from,Navigation::Point goal,float maxLength,float goalRadius,const float* costs=nullptr,int aiClass=3);
    bool CanPlace(CUnit* unit,Navigation::Point point,int aiClass=3);
    bool Support(CUnit* unit,Navigation::Point point,Navigation::Location& result);
    bool Step(CUnit* unit,Navigation::Point from,Navigation::Point desired,Navigation::Location& result,const CAIRoute* route=nullptr);
    bool Restore(CUnit* unit,CAIMap& map);
    struct Movement {
        Navigation::Location support;
        Navigation::Point desired,direction;
        float speed=0;
        bool rotating=false,finished=false,waiting=false,replanned=false;
        double detourMs=0;
        int blocker=-1;
        unsigned avoidanceAttempts=0;
    };
    // Shared by L1ExecMove and the offline release walker. Time is in game ticks.
    bool Move(CUnit*,SAIPath&,float time,unsigned& wait,Movement&);

};
