#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "ResourceManager.h"
#include "OwnedRender/E3HudVisual.h"
#include "ModRuntime.h"
#include "ModPackage.h"
#include "OwnedRender/StartupPaths.h"

#include "World.h"
#include "FileRegistry.h"
#include <io.h>
#include <fstream>
#include <set>
#include "Notification.h"
/////////////////////////////////////////////////////////////////////////////////////
CResourceManager::CResourceManager()
{
}
CResourceManager::~CResourceManager()
{
    Destroy();
}
/////////////////////////////////////////////////////////////////////////////////////
// close all managed resources
void CResourceManager::Destroy()
{
    OwnedRender::E3HudResourceChanged(this,true);
    // close all opened resources
    for (auto pos = resources.begin(); pos != resources.end(); ++pos)
    {
        pos->second->CloseResource();
        delete pos->second;
    }
    // clear resources array
    resources.clear();
    // destroy base structures
    CBaseManager::Destroy();
}
/////////////////////////////////////////////////////////////////////////////////////
// add new resource file and open it
BOOL CResourceManager::AddNewResource(LPCSTR pszResourceFileName)
{
    OwnedRender::E3HudResourceChanged(this);
    if (_access(pszResourceFileName, 4) == -1) {
        std::vector<std::string> paths;bool ignore=false;
        if(!ModRuntime::Resources()||!ModRuntime::Resources()->ResourcePlan(pszResourceFileName,paths,ignore)||(!ignore&&paths.empty()))return FALSE;
    }
    // first, check for such resource
    for (auto pos = resources.begin(); pos != resources.end(); ++pos)
    {
        if (pos->first == pszResourceFileName)
            return TRUE;
    }
    // then, add it and open
    resources.push_back(CResourcePair(pszResourceFileName, dbgnew CMappedResource()));
    if (!resources.back().second->OpenResource(pszResourceFileName))
    {
        delete resources.back().second;
        resources.pop_back();
        return FALSE;
    }

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// searching for resource and close it
BOOL CResourceManager::CloseResource(LPCSTR pszResourceFileName)
{
    OwnedRender::E3HudResourceChanged(this);
    for (auto pos = resources.begin(); pos != resources.end(); ++pos)
    {
        if (pos->first == pszResourceFileName)
        {
            pos->second->CloseResource();
            delete pos->second;
            resources.erase(pos);
            return TRUE;
        }
    }

    return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
// close all opened resources
BOOL CResourceManager::CloseResources()
{
    OwnedRender::E3HudResourceChanged(this);
    for (auto pos = resources.begin(); pos != resources.end(); ++pos)
    {
        pos->second->CloseResource();
        delete pos->second;
    }
    resources.clear();

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// try to open file by name in all managed resources
LPBYTE CResourceManager::GetFile(LPCSTR pszFileName, LPDWORD pnDataSize)
{
    LPBYTE lpData = NULL;

    for (auto pos = resources.begin(); pos != resources.end(); ++pos)
    {
        if(pos->second->IsDeleted(pszFileName))return nullptr;
        lpData = pos->second->GetFile(pszFileName, pnDataSize);
        if (lpData)
            return lpData;
    }

    return lpData;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CResourceManager::CloseFile(LPBYTE pFileImage)
{
    for (auto pos = resources.begin(); pos != resources.end(); ++pos)
    {
        if (pos->second->CloseFile(pFileImage) == true)
            return true;
    }
    return false;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CResourceManager::CheckFileExist(LPCSTR pszFileName)
{
    for (auto pos = resources.begin(); pos != resources.end(); ++pos)
    {
        if(pos->second->IsDeleted(pszFileName))return FALSE;
        BOOL bRetVal = pos->second->CheckFileExist(pszFileName);
        if (bRetVal)
            return bRetVal;
    }
    return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CResourceManager::CloseAllFiles()
{
    for (auto pos = resources.begin(); pos != resources.end(); ++pos)
        pos->second->CloseAllFiles();
}
/////////////////////////////////////////////////////////////////////////////////////

// Ordered visual overlays: first archive containing an entry wins.
BOOL CResourceManager::AddGraphicsResources(LPCSTR category)
{
    if (auto *mod = ModRuntime::Resources())
    {
        const bool textures = _stricmp(category, "textures.res") == 0;
        const bool figures = _stricmp(category, "figures.res") == 0;
        if (!textures && !figures) return TRUE;
        owned::ResourceStoreConfig mounts;
        const CString resourcePath = ((CWorld*)GetWorld())->GetResourcePath();
        OwnedRender::CaptureGraphicsMounts(mounts, std::filesystem::path((LPCSTR)resourcePath), mod,
            [](const std::string& path) { return std::filesystem::path(path).wstring(); });
        const auto kind = figures ? owned::kArchiveFigures : owned::kArchiveTextures;
        std::vector<std::wstring> orderedPaths = mounts.loose_roots;
        for (size_t i = 0; i != mounts.archives.size(); ++i)
            if (mounts.archive_kinds[i] & kind) orderedPaths.push_back(mounts.archives[i]);
        std::set<std::wstring> seen;
        for (const auto& mountedPath : orderedPaths)
        {
            std::wstring key = mountedPath;
            for (auto& ch : key) if (ch >= L'A' && ch <= L'Z') ch += L'a' - L'A';
            if (!seen.insert(key).second) continue;
            const std::string path = std::filesystem::path(mountedPath).string();
            CMappedResource* archive = figures ? static_cast<CMappedResource*>(dbgnew CWin32MappedResource(true))
                                              : dbgnew CMappedResource(true);
            if (!archive->OpenResource(path.c_str()))
            {
                delete archive;
                return ReportError("Cannot open graphics archive %s", path.c_str());
            }
            OwnedRender::E3HudResourceChanged(this);
            resources.push_back(CResourcePair(path, archive));
        }
        return TRUE;
    }
    CString path = CWorld::GetCurrentPath() + "config\\graphics-resources.ini";
    if (_access(path, 0) != 0)
        return TRUE;
    std::ifstream input((LPCSTR)path);
    if (!input)
        return ReportError("Cannot read graphics resource list");
    std::string line;
    while (std::getline(input, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto equals = line.find('=');
        if (equals == std::string::npos || line.substr(0, equals) != category) continue;
        std::string relative = line.substr(equals + 1);
        if (relative.empty() || relative.find("..") != std::string::npos ||
            relative.find(':') != std::string::npos || relative[0] == '\\' || relative[0] == '/')
            return ReportError("Invalid graphics resource path");
        CString archive = CWorld::GetCurrentPath() + relative.c_str();
        if (!AddNewResource(archive))
            return ReportError("Cannot open graphics archive %s", (LPCSTR)archive);
    }
    return TRUE;
}

void CResourceManager::GetFileList(CStringArray& names,LPCSTR mask) {
    std::set<std::string> unique;
    for(const auto& resource:resources) {
        CStringArray entries;resource.second->GetFileList(entries,mask);
        for(INT_PTR i=0;i<entries.GetSize();++i)unique.emplace(entries[i].GetString());
    }
    for(const auto& name:unique)names.Add(name.c_str());
}
