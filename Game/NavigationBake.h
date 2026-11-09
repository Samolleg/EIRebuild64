#pragma once
#include "../Navigation/BodyTable.h"
#include <filesystem>
#include <set>
#include <functional>
#include <future>
#include "../Navigation/Authoring.h"

class CMapObject;
class CWorldServer;
class CFigureManager;
class CTextureManager;
class CMapObjectServer;
struct NavigationCanopy;
class CAIMap;
namespace owned {struct FigureCpuAsset;struct ModelCpuAsset;struct AnimationClipAsset;}
// Offline importer/measurement state. NavigationWorld allocates it exclusively
// under the NavBake tool scope; a game session never imports this geometry.
class NavigationBake {
    std::map<int,Navigation::Geometry> objects;
    using FigureJob=std::shared_future<std::shared_ptr<const owned::FigureCpuAsset>>;
    std::map<std::string,FigureJob> figures;
    std::shared_ptr<CTextureManager> canopyResources;
    std::map<std::string,std::shared_ptr<const NavigationCanopy>> canopyTextures;
    std::map<std::string,std::shared_ptr<const owned::ModelCpuAsset>> bodies;
    std::map<std::string,std::shared_ptr<const owned::AnimationClipAsset>> poses;
    Navigation::BodyTable bodyTable;
    Navigation::Authoring authored;
    CAIMap* sourceMap=nullptr;
    CWorldServer* sourceWorld=nullptr;
    std::set<int> staticIds;
    std::map<int,Navigation::Door> doors;
    std::set<std::string> normalizedBounds;
    FigureJob QueueFigure(CFigureManager& manager,const std::string& mask,const char* part);
    std::shared_ptr<const owned::FigureCpuAsset> Figure(CFigureManager& manager,const std::string& mask,const char* part);
    const Navigation::BodySize& Body(CWorldServer& world,const std::string& mask,float strength,float grace,float tall);
    Navigation::Geometry Geometry();
    float GroundHeight(Navigation::Point point);
    const NavigationCanopy& Canopy(const CMapObjectServer& object,const owned::FigureCpuAsset& figure);
public:
    // Yield to the owning-thread coordinator while this map's tiles build.
    static std::function<void()> overlap;
    static unsigned tileCells;
    static std::filesystem::path reportDirectory;
    static std::filesystem::path CacheDirectory() { return reportDirectory/"cache"; }
    static std::filesystem::path replayInput;
    Navigation::Geometry terrain;
    void Bind(CWorldServer& world,CAIMap& map) { sourceWorld=&world;sourceMap=&map; }
    void Configure(const Navigation::Authoring& policy) { authored=policy; }
    Navigation::Bytes Bodies(CWorldServer& world,const Navigation::Bytes& previous={});
    void Object(CMapObject* object,bool remove,CAIMap& map);
    std::uint64_t Bake(const std::string& map,const std::string& mob,const std::filesystem::path& directory,
        Navigation::Bytes& table,const Navigation::Bytes& authored);
};
