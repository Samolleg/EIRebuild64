#pragma once
#include "../Navigation/Query.h"
#include "../Navigation/BodyTable.h"
#include "../Navigation/Profiles.h"
#include "../Navigation/SceneTransform.h"
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
class CMEBasicObject;
class NavigationWorld {
    std::array<std::unique_ptr<Navigation::Mesh>,Navigation::AgentCount> meshes;
    Navigation::BodyTable bodyTable;
    std::map<Navigation::BodyKey,Navigation::BodySize> bodySizes;
    std::unique_ptr<NavigationBake> baker; // Allocated only in the offline tool.
    std::string mapName,mobName,error;
    std::uint64_t generation=0;
    // Changes whenever a live mesh may have changed. No save data.
    std::uint64_t observerRevision=0;
    unsigned failures=0,stepFailures=0;
    std::uint64_t nextStepLog=0;
    // Independent, transient observers: one noisy NPC cannot hide another.
    enum class AirEvent { Query, RandomGoal, Step };
    struct AirJournal { std::uint64_t next=0; unsigned suppressed=0; };
    std::map<int,std::array<AirJournal,3>> airJournal;
    bool ObserveAir(CUnit* unit,AirEvent event,unsigned& suppressed);
    bool loaded=false;
    bool iceVariant=false;
    Navigation::Authoring authoring;
    std::map<int,std::vector<Navigation::Solid>> liveObjects;
    std::map<int,std::vector<Navigation::Solid>> appliedObjects;
    std::map<int,std::vector<Navigation::Solid>> liveFlightObjects,appliedFlightObjects;

    std::set<int> bakedStatic,dirtyObjects;
    unsigned updateProfile=0;
    std::shared_ptr<Navigation::Geometry> sources;
    std::shared_ptr<const Navigation::Geometry> originalSources;
    CWorldServer* owner=nullptr;
    std::map<int,Navigation::SceneTransform> sourceTransforms,liveTransforms,appliedTransforms;
    std::set<int> detachedObjects;
    std::map<std::tuple<int,int,int>,std::unique_ptr<Navigation::Mesh>> preparedTiles;
    std::set<int> absentStatic;
    std::set<std::tuple<int,int,int>> sourceDirty;
    std::future<std::unique_ptr<Navigation::Mesh>> sourceJob;
    std::tuple<int,int,int> sourceTask;
    bool sourceChanged=false;
    struct Retry {unsigned failures=0;std::uint64_t after=0;};
    std::map<std::tuple<int,int,int>,Retry> sourceRetries;
    std::set<int> flyingUnits;
    std::filesystem::path meshDirectory;
    bool flightDeferred=false;
    Navigation::Point flightLow,flightHigh;
    void FlushFlight();
    void QueueTile(int type,int x,int z);
    void QueueFlightBox(const Navigation::Solid&);
    void LoadFlying();
    std::map<int,int> liveDoors;
    struct Queue { Navigation::Point goal;int blocker=0; };
    std::map<int,Queue> queues;
    const std::vector<Navigation::Obstacle>* queryObstacles=nullptr;
    const Navigation::Route* queryReuse=nullptr;
    void UpdateSources(bool initial);
    bool Load(const std::filesystem::path& directory, bool synchronize=true);

    void QueueBox(const Navigation::Solid& box);
    bool Dynamic(int id) const { return !bakedStatic.count(id); }
    void SyncObjects(bool initial=false);
    bool Pending(int profile) const;
    bool Pending(int profile,Navigation::Point from,Navigation::Point to) const;
    void PublishSources();
    void SourceMob(const char* name,CAIMap& map);
    const Navigation::BodySize& Body(CUnit* unit);
    int Type(CUnit* unit,int aiClass);
    float Height(CUnit* unit,int aiClass=3);
    Navigation::Mesh* ForUnit(CUnit* unit,int aiClass=3);
public:
    NavigationWorld();
    ~NavigationWorld();
    NavigationWorld& operator=(NavigationWorld&&) = default;
    bool IsIceVariant() const { return iceVariant; }
    std::unique_ptr<NavigationWorld> PrepareVariant(bool ice);
    void CommitVariant(std::unique_ptr<NavigationWorld> candidate, CWorldServer& world);
    Navigation::Geometry* Terrain(); // Null in game sessions.
    void Scene(const char* mpr,const char* mob,CWorldServer& world);
    void SourceObject(const CMEBasicObject& object,CAIMap& map);
    // Service/MOB seam: z is terrain-relative, yaw is radians. Presence controls
    // AI/Nav participation (falling scenery remains a world/render object).
    void TransformObject(int id,Navigation::Point position,float yaw,bool present);
    bool ObjectPresent(int id) const { return !detachedObjects.count(id); }
    void Object(CMapObject* object,bool remove,CAIMap& map);
    std::uint64_t Bake(const std::filesystem::path& directory,Navigation::Bytes& table,CWorldServer& world);
    bool Load();
    void Advance();
    std::uint64_t Generation(CUnit* unit) { auto* mesh=ForUnit(unit);return mesh?mesh->generation:0; }
    void Stop(int id) { queues.erase(id); }
    void ForgetUnit(int id) { Stop(id);flyingUnits.erase(id);airJournal.erase(id); }
    bool Goal(CUnit* unit,Navigation::Point reference,Navigation::Location& result);
    float Radius(CUnit* unit) { return (std::max)(.05f,Body(unit).radius); }
    bool RandomGoal(CUnit* unit,Navigation::Point center,float radius,Navigation::Point& goal,Navigation::RandomShape shape=Navigation::RandomShape::Circle);
    bool Place(CUnit* unit,CAIMap& map);
    const std::string& Error() const { return error; }
    // Read-only: does not lazy-load the flying profile or touch unit queues.
    const Navigation::Mesh* ObserverMesh(int profile) const noexcept {
        return loaded && profile>=0 && profile<Navigation::AgentCount ? meshes[profile].get() : nullptr;
    }
    std::uint64_t ObserverRevision() const noexcept { return observerRevision; }
    bool ObserverUpdating(int profile) const { return Pending(profile); }
    Navigation::Point Position(CUnit* unit,CAIMap& map) const;
    Navigation::Route Find(CUnit* unit,Navigation::Point from,Navigation::Point goal,float maxLength,float goalRadius,const float* costs=nullptr,int aiClass=3,Navigation::GoalPolicy goalPolicy=Navigation::GoalPolicy::Exact,const Navigation::Location* goalSupport=nullptr);
    // Passage width and unit-to-unit separation have different footprints.
    float CollisionRadius(CUnit* unit);
    bool AttackPosition(CUnit* unit,CUnit* target,Navigation::Point goal,float reach,float maxLength,Navigation::Location& result);
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
