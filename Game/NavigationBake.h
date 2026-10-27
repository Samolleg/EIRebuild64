#pragma once
#include "../Navigation/BodyTable.h"
#include <filesystem>
#include <set>
#include "../Navigation/Authoring.h"

class CMapObject;
class CWorldServer;
class CFigureManager;
class CAIMap;
namespace owned {struct FigureCpuAsset;struct ModelCpuAsset;struct AnimationClipAsset;}
// Offline importer/measurement state. NavigationWorld allocates it exclusively
// under the NavBake tool scope; a game session never imports this geometry.
class NavigationBake {
    std::map<int,Navigation::Geometry> objects;
    std::map<std::string,std::shared_ptr<const owned::FigureCpuAsset>> figures;
    std::map<std::string,std::shared_ptr<const owned::ModelCpuAsset>> bodies;
    std::map<std::string,std::shared_ptr<const owned::AnimationClipAsset>> poses;
    Navigation::BodyTable bodyTable;
    Navigation::Authoring authored;
    CAIMap* sourceMap=nullptr;
    CWorldServer* sourceWorld=nullptr;
    std::set<int> staticIds;
    std::set<std::string> normalizedBounds;
    std::shared_ptr<const owned::FigureCpuAsset> Figure(CFigureManager& manager,const std::string& mask,const char* part);
    const Navigation::BodySize& Body(CWorldServer& world,const std::string& mask,float strength,float grace,float tall);
    Navigation::Geometry Geometry();
public:
    Navigation::Geometry terrain;
    void Bind(CWorldServer& world,CAIMap& map) { sourceWorld=&world;sourceMap=&map; }
    void Configure(const Navigation::Authoring& policy) { authored=policy; }
    Navigation::Bytes Bodies(CWorldServer& world);
    void Object(CMapObject* object,bool remove,CAIMap& map);
    std::uint64_t Bake(const std::string& map,const std::string& mob,const std::filesystem::path& directory,
        const Navigation::Bytes& table,const Navigation::Bytes& authored);
};
