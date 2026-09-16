#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "ClientSpecificManager.h"

#include "Object3DClientSpecific.h"
#include "MapObjectClientSpecific.h"
#include "FigureClientSpecific.h"
#include "UnitClientSpecific.h"
#include "PlayerClient.h"
#include "StuffMOClientSpec.h"
/////////////////////////////////////////////////////////////////////////////////////
CClientSpecificManager::CClientSpecificManager()
{
#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
}
/////////////////////////////////////////////////////////////////////////////////////
CClientSpecificManager::~CClientSpecificManager()
{
}
/////////////////////////////////////////////////////////////////////////////////////
// LPGAMEBASE CClientSpecificManager::CreateInstant( ETypeIDs tidTypeID )
// creates client specific parts...
LPGAMEBASE CClientSpecificManager::CreateInstant(ETypeIDs tidTypeID)
{
    LPGAMEBASE lpBase = NULL;

    switch (tidTypeID)
    {
    case SPTID_OBJECT3D_SPECIFIC:
        lpBase = dbgnew CObject3DClientSpecific();
        break;
    case SPTID_MAPOBJECT_SPECIFIC:
        lpBase = dbgnew CMapObjectClientSpecific();
        break;
    case SPTID_FIGURE_SPECIFIC:
        lpBase = dbgnew CFigureClientSpecific();
        break;
    case SPTID_PLAYER_SPECIFIC:
        lpBase = dbgnew CPlayerClient();
        break;
    case SPTID_UNIT_SPECIFIC:
        lpBase = dbgnew CUnitClientSpecific();
        break;
    case SPTID_TORCH_SPECIFIC:
        lpBase = dbgnew CTorchObjectClient();
        return lpBase;
    }

    return lpBase;
}
/////////////////////////////////////////////////////////////////////////////////////
