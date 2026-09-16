#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "ServerSpecificManager.h"
#include "PlayerServer.h"
#include "Object3DServer.h"
#include "MapObjectServer.h"
#include "UnitServer.h"
#include "StuffMapObjects.h"
/////////////////////////////////////////////////////////////////////////////////////
CServerSpecificManager::CServerSpecificManager()
{
#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif
}
/////////////////////////////////////////////////////////////////////////////////////
CServerSpecificManager::~CServerSpecificManager()
{
}
/////////////////////////////////////////////////////////////////////////////////////
// LPGAMEBASE CServerSpecificManager::CreateInstant( ETypeIDs tidTypeID )
// creates client specific parts...
LPGAMEBASE CServerSpecificManager::CreateInstant(ETypeIDs tidTypeID)
{
    LPGAMEBASE lpBase = NULL;

    switch (tidTypeID)
    {
    case SPTID_PLAYER_SPECIFIC:
        lpBase = dbgnew CPlayerServer();
        return lpBase;
    case SPTID_OBJECT3D_SPECIFIC:
        lpBase = dbgnew CObject3DServer();
        return lpBase;
    case SPTID_MAPOBJECT_SPECIFIC:
        lpBase = dbgnew CMapObjectServer();
        return lpBase;
    case SPTID_UNIT_SPECIFIC:
        lpBase = dbgnew CUnitServer();
        return lpBase;
    case SPTID_TORCH_SPECIFIC:
        lpBase = dbgnew CTorchObjectSpecific();
        return lpBase;
    }
    return NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
