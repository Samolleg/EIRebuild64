#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "ResourceManager.h"
#include "ModRuntime.h"
#include "ModPackage.h"

#include "World.h"
#include "FileRegistry.h"
#include <io.h>
#include <fstream>
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
    if (_access(pszResourceFileName, 4) == -1) {
        std::vector<std::string> paths;bool ignore=false;
        if(!ModRuntime::Resources()||!ModRuntime::Resources()->ResourcePlan(pszResourceFileName,paths,ignore)||!ignore)return FALSE;
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
    if(ModRuntime::Resources())return TRUE; // The selected mod owns its ordered graphics archives.
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
