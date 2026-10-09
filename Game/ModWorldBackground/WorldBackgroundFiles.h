#pragma once
#include "WorldBackgroundData.h"

namespace WorldBackground {
// Explicit resolved profile. The runtime adapter supplies the actual configured
// archive order; no guesses from MOD.Title, addon hash, or the first JSON profile.
class ProfileFiles {
public:
    std::string profile;
    std::wstring layoutRoot;
    Blob LayoutMap(const std::string& requestedProfile,const std::string& path,bool mob=false) const;
    std::vector<std::wstring> mapRoots;
    std::vector<std::wstring> textureArchives,figureArchives,materialArchives;
    Blob Mpr(const Identity& identity) const;
    Blob Mob(const std::string& requestedProfile,const std::string& member) const;
    Blob MaterialFile(const std::string& requestedProfile,const std::string& member) const;
    Blob Figure(const std::string& requestedProfile,const std::string& member) const;
    Blob Texture(const std::string& requestedProfile,const std::string& member) const;
};
Blob ReadFileBounded(const std::wstring& path,size_t limit);
bool ReadArchiveMember(const std::wstring& archive,const std::string& member,Blob& output,size_t limit);
}
