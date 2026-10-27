#pragma once
#include "../Navigation/BodyTable.h"
#include <filesystem>
#include <set>

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
    std::set<std::string> normalizedBounds;
    std::shared_ptr<const owned::FigureCpuAsset> Figure(CFigureManager& manager,const std::string& mask,const char* part);
    const Navigation::BodySize& Body(CWorldServer& world,const std::string& mask,float strength,float grace,float tall);
    Navigation::Geometry Geometry();
public:
    Navigation::Geometry terrain;
    Navigation::Bytes Bodies(CWorldServer& world);
    void Object(CMapObject* object,bool remove,CAIMap& map);
    std::string Fingerprint(const std::string& map,const std::string& mob,const Navigation::Bytes& table,const Navigation::Bytes& authored);
    std::uint64_t Bake(const std::string& map,const std::string& mob,const std::filesystem::path& directory,
        const Navigation::Bytes& table,const Navigation::Bytes& authored);
};
