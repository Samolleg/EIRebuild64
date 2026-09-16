#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "LightManager.h"

// #include "WorldClient.h"
// #include "Terrain.h"
/////////////////////////////////////////////////////////////////////////////////////
CLightManager::CLightManager()
{
    dcvAmbientLight.r = 0;
    dcvAmbientLight.g = 0;
    dcvAmbientLight.b = 0;
    dcvAmbientLight.a = 0;

    bLightsActive = TRUE;

#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
}
/////////////////////////////////////////////////////////////////////////////////////
CLightManager::~CLightManager()
{
}
/////////////////////////////////////////////////////////////////////////////////////
// LPGAMEBASE CLightManager::CreateInstant( ETypeIDs tidTypeID )
// create data instant with desired type ID
LPGAMEBASE CLightManager::CreateInstant(ETypeIDs tidTypeID)
{
    LPLIGHT pLight = NULL;

    if ((tidTypeID == LTID_DIRECTIONAL) || (tidTypeID == LTID_POINTLIGHT))
    {
        pLight = dbgnew CLight();
        if (pLight == NULL)
            return NULL;
        pLight->SetTypeID(tidTypeID);
    }
    else
        ReportWarning("CLightManager::CreateInstant.\n Request for light with unknown type %d.\n",
                      tidTypeID);

    return pLight;
}
/////////////////////////////////////////////////////////////////////////////////////
LPGAMEBASE CLightManager::CreateData(ETypeIDs tidTypeID, int id)
{
    LPGAMEBASE lpLight = CBaseManager::CreateData(tidTypeID, id);

    lpLight->ResetFlag(LIT_INACTIVE);

    return lpLight;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CLightManager::SetAmbientLight( const D3DCOLORVALUE& dcvNewAmbientLight )
// set ambient light
BOOL CLightManager::SetAmbientLight(const D3DCOLORVALUE &dcvNewAmbientLight)
{
    // store new ambient light params
    dcvAmbientLight = dcvNewAmbientLight;

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CLightManager::SetSunLight( LPLIGHT lpNewSunLight )
// set sunlight ...
BOOL CLightManager::SetSunLight(LPLIGHT lpNewSunLight)
{
    // set new sunlight
    lpSunLight = lpNewSunLight;

    /*
    if ( lpSunLight )
    {
            // set sun light to d3d scene
            ((CWorldClient*)lpWorld)->GetTerrain()->SetSun( lpSunLight, dcvAmbientLight );
            //
            return ((CWorldClient*)lpWorld)->lpD3DScene->SetSunLight( lpSunLight );
    }
    else
            return TRUE;
     */
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CLightManager::SetLight(LPLIGHT lpNewSunLight, const D3DCOLORVALUE &dcvNewAmbientLight)
{
    lpSunLight = lpNewSunLight;
    // store new ambient light params
    dcvAmbientLight = dcvNewAmbientLight;

    /*
    if ( lpSunLight )
    {
            // set sun light to d3d scene
            ((CWorldClient*)lpWorld)->GetTerrain()->SetSun( lpSunLight, dcvAmbientLight );
            //
            ((CWorldClient*)lpWorld)->lpD3DScene->SetSunLight( lpSunLight );
            ((CWorldClient*)lpWorld)->lpD3DScene->SetAmbientLight( dcvAmbientLight );

            return TRUE;
    }
    else
            return TRUE;
     */
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
