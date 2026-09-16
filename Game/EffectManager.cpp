#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
/*==========================================================================================================

        Effect manager class implementation

==========================================================================================================*/

#include "EffectManager.h"
#include "Effect.h"

/////////////////////////////////////////////////////////////////////////////////////
// ---------------------------------- Create/destroy
// -------------------------------------------------------

BOOL CEffectManager::Create()
{
    if (!CBaseManager::Create())
        return FALSE;
    return TRUE;
}

void CEffectManager::Destroy()
{
    CBaseManager::Destroy();
}

// --------------------------------- Create/remove player
// --------------------------------------------------

LPGAMEBASE CEffectManager::CreateInstant(ETypeIDs tidTypeID)
{
    CEffect *pEffect = NULL;
    switch (tidTypeID)
    {
    case HPC_EFFECTSOUND:
        pEffect = dbgnew CEffectSound();
        break;
    case HPC_EFFECTAMBIENTSOUND:
        pEffect = dbgnew CEffectAmbientSound();
        break;
    case HPC_EFFECTPARTICLE:
        pEffect = dbgnew CEffectParticle();
        break;
    case HPC_EFFECTPOINTLIGHT:
        pEffect = dbgnew CEffectPointLight();
        break;
    case HPC_EFFECTLIGHTNING:
        pEffect = dbgnew CEffectLightning;
        break;
    case HPC_EFFECTWALL:
        pEffect = dbgnew CEffectWall;
        break;
    case HPC_EFFECTARROW:
        pEffect = dbgnew CEffectArrow;
        break;
    case HPC_EFFECTARROWFIGURE:
        pEffect = dbgnew CEffectArrowFigure;
        break;
    case HPC_EFFECTSHOWVISIBILITY:
        pEffect = dbgnew CEffectShowVisibility;
        break;
    case HPC_EFFECTSOUNDSEQ:
        pEffect = dbgnew CEffectSoundSeq;
        break;
    case HPC_EFFECTMOSHKA:
        pEffect = dbgnew CEffectMoshka;
        break;
    case HPC_EFFECTTORNADO:
        pEffect = dbgnew CEffectTornado;
        break;
    }

    if (pEffect == NULL)
        return NULL;
    pEffect->SetTypeID(tidTypeID);

    return pEffect;
}