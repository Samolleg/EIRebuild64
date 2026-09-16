#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "World.h"

#include "MaterialManager.h"
/////////////////////////////////////////////////////////////////////////////////////
CMaterialManager::CMaterialManager()
{
#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
}
/////////////////////////////////////////////////////////////////////////////////////
CMaterialManager::~CMaterialManager()
{
}
/////////////////////////////////////////////////////////////////////////////////////
const DWORD NUM_MATERIALS = 8;
struct SMaterialDesc
{
    ETypeIDs tid;
    const char *pszName;
};
const SMaterialDesc materialDescs[] = {
    { MTID_LEATHER, "leather.mat" }, { MTID_WOOD, "wood.mat" },    { MTID_METAL, "metal.mat" },
    { MTID_CLOTH, "cloth.mat" },     { MTID_SKIN, "skin.mat" },    { MTID_STONE, "stone.mat" },
    { MTID_MAGIC, "magic.mat" },     { MTID_CRYSTAL, "metal.mat" }
};
/////////////////////////////////////////////////////////////////////////////////////
// create internal data and open resource
BOOL CMaterialManager::Create()
{
    if (CResourceManager::Create() == FALSE)
        return FALSE;

    CString szResourcesPath = ((CWorld *)GetWorld())->GetResourcePath();
    AddNewResource(szResourcesPath + "materials.res");
    // create material map
    for (int i = 0; i < NUM_MATERIALS; ++i)
    {
        D3DMATERIAL7 material;
        memset(&material, 0, sizeof(material));
        // open resource file
        BYTE *lpData = GetFile(materialDescs[i].pszName);
        BYTE *pBaseData = lpData;
        if (lpData == 0)
        {
            material.diffuse.r = material.diffuse.g = material.diffuse.b = 0.3f;
            material.diffuse.a = 1.0f;
            material.ambient.r = material.ambient.g = material.ambient.b = 0.7f;
            material.specular.r = material.specular.g = material.specular.b = 0.2f;
            material.emissive.r = material.emissive.g = material.emissive.b = 0.0f;
            material.power = 5;
        }
        else
        {
            // skip material name
            WORD num = *(reinterpret_cast<WORD *>(lpData));
            lpData += sizeof(num);
            lpData += num;
            // read diffuse components
            material.diffuse = *(reinterpret_cast<D3DCOLORVALUE *>(lpData));
            lpData += sizeof(D3DCOLORVALUE);
            // ...ambient
            material.ambient = *(reinterpret_cast<D3DCOLORVALUE *>(lpData));
            lpData += sizeof(D3DCOLORVALUE);
            // ...specular
            material.specular = *(reinterpret_cast<D3DCOLORVALUE *>(lpData));
            lpData += sizeof(D3DCOLORVALUE);
            // ...emissive
            material.emissive = *(reinterpret_cast<D3DCOLORVALUE *>(lpData));
            lpData += sizeof(D3DCOLORVALUE);
            // ...and power
            material.power = *(reinterpret_cast<float *>(lpData));
            lpData += sizeof(float);
        }
        //
        CloseFile(pBaseData);
        //
        if (materialDescs[i].tid == MTID_MAGIC)
            material.emissive.r = material.emissive.g = material.emissive.b = 1.0f;
        // add to the hash map
        materials[materialDescs[i].tid] = material;
    }

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// void CMaterialManager::Destroy()
// destroy internal data and close resource
void CMaterialManager::Destroy()
{
    CResourceManager::Destroy();
}
/////////////////////////////////////////////////////////////////////////////////////
// LPGAMEBASE CMaterialManager::CreateInstant( ETypeIDs tidTypeID )
// create data instant with desired type ID
LPGAMEBASE CMaterialManager::CreateInstant(ETypeIDs tidTypeID)
{
    LPMATERIAL pMaterial = dbgnew CMaterial();
    if (pMaterial == NULL)
        return NULL;
    pMaterial->SetTypeID(tidTypeID);

    return pMaterial;
}
/////////////////////////////////////////////////////////////////////////////////////
// LPMATERIAL CMaterialManager::CreateMaterial( int nTypeID )
// create material and set data from tables by typeID
LPMATERIAL CMaterialManager::CreateMaterial(ETypeIDs tidTypeID)
{
    LPMATERIAL pMaterial = (LPMATERIAL)CreateData(tidTypeID);
    pMaterial->SetFlag(MAT_CHANGED);
    SetupMaterial(pMaterial);

    return pMaterial;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CMaterialManager::SetupMaterial(LPMATERIAL lpMaterial)
{
    return SetMaterial(lpMaterial, lpMaterial->GetTypeID());
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CMaterialManager::SetMaterial( LPMATERIAL lpMaterial, ETypeIDs tidTypeID )
// set material components: ambient, diffuse, specular, emissive, alpha, power, ramp.
BOOL CMaterialManager::SetMaterial(LPMATERIAL lpMaterial, ETypeIDs tidTypeID)
{
    // check param
    if (lpMaterial == NULL)
        return FALSE;
    //
    lpMaterial->Setup(materials[tidTypeID]);
    //
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
