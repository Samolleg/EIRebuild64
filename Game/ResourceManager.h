#pragma once
#include "CMappedResource.h"
#include "BaseManager.h"
/////////////////////////////////////////////////////////////////////////////////////
// struct SResourceEntry
// resource system and resource file name, which handled by this resource system
struct SResourceEntry
{
public:
    CMappedResource resource; // mapped resource system
    CString szResourceName;   // resource file name, which handled by resource system
    BOOL bOpen;               // is resource opened ?
};
/////////////////////////////////////////////////////////////////////////////////////
// class CResourceManager : public CBaseManager
// data manager, which handles external resources via file mapping
class CResourceManager : public CBaseManager
{
public:
    typedef std::pair<std::string, CMappedResource *> CResourcePair;
    typedef std::vector<CResourcePair> CResourceList;

protected:
    CResourceList resources;

public:
    CResourceManager();
    virtual ~CResourceManager();
    // create/destroy functions
    virtual void Destroy();
    // work with resources
    // single resource file
    virtual BOOL AddNewResource(LPCSTR pszResourceFileName);
    virtual BOOL CloseResource(LPCSTR pszResourceFileName);
    // multiple resource operations
    virtual BOOL CloseResources();
    BOOL AddGraphicsResources(LPCSTR category);
    // file open from resource
    virtual LPBYTE GetFile(LPCSTR pszFileName, LPDWORD pnDataSize = NULL);
    virtual bool CloseFile(LPBYTE pFileImage);
    virtual BOOL CheckFileExist(LPCSTR pszFileName);
    virtual void CloseAllFiles();
    // resource description string
    virtual LPCSTR GetResourceDescription() const = 0;
};
/////////////////////////////////////////////////////////////////////////////////////
