#pragma once

#include "ResourceManager.h"
#include "Material.h"

#include <unordered_map>

class CMaterialManager : public CResourceManager
{
private:
    std::unordered_map<DWORD, D3DMATERIAL7> materials;

private:
    virtual BOOL Create();
    virtual LPGAMEBASE CreateInstant(ETypeIDs tidTypeID);

public:
    CMaterialManager();
    virtual ~CMaterialManager();
    // create/destroy functions
    virtual void Destroy();
    // material create/set
    LPMATERIAL CreateMaterial(LPCSTR pszName);     // create material from file
    LPMATERIAL CreateMaterial(ETypeIDs tidTypeID); // create material with specified type
    BOOL SetMaterial(LPMATERIAL lpMaterial, ETypeIDs tidTypeID);
    BOOL SetupMaterial(LPMATERIAL lpMaterial);
    // resource description string
    virtual LPCSTR GetResourceDescription() const { return "materials"; };
    // class identification information
    virtual LPCSTR GetGameClassName() const { return "CMaterialManager"; };
    virtual EClassIDs WhoAmI() const { return HPC_MATERIALMANAGER; }
};
