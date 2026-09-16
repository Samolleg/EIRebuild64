#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "Particle.h"
#include "ParticleManager.h"
#include "camera.h"
#include "WorldClient.h"
#include <algorithm>
#ifndef _A4
// for unit visibility check
#include "Unit.h"
#endif //_A4
/**********************************************************************
 *<
        FILE: ParticleManager.CPP

        DESCRIPTION:  Paticle systems manager implementation

        CREATED BY: Ivanov Evgeny

 *>	Copyright (c) 1998, Nival Enterntainemnt
 **********************************************************************/

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#define new DEBUG_NEW
#endif

// ------------------------- Initialization -------------------------------------------------

BOOL CParticleManager::Create()
{
    Clear();
    if (!CBaseManager::Create())
        return FALSE;
    return TRUE;
}

void CParticleManager::Destroy()
{
    CHashMapIterator<int, CParticle *> iterator;
    CParticle *pParticle = iterator.GetFirst(&referencedParticles);
    while (pParticle)
    {
        pParticle->Release();
        pParticle = iterator.GetNext();
    }
    referencedParticles.RemoveAll();
    while (!singleParticlesPool.IsEmpty())
        delete singleParticlesPool.RemoveHead();
    CBaseManager::Destroy();
    Clear();
}

void CParticleManager::Clear()
{
    bHoldFires = FALSE;
}

// ------------------ Create particle system object -----------------------------------------

LPGAMEBASE CParticleManager::CreateInstant(ETypeIDs tidTypeID)
{
    CParticle *pParticle = dbgnew CParticle();
    if (pParticle == NULL)
        return NULL;
    pParticle->SetTypeID(tidTypeID);

    return pParticle;
}

CParticle *CParticleManager::CreateParticleSystem(ETypeIDs tidTypeID)
{
    CParticle *rv = (CParticle *)CreateData(tidTypeID);
    rv->AddRef();
    referencedParticles.SetAt(rv->GetID(), rv);

    if (bHoldFires && (rv->GetTypeID() == PTID_CAMPFIRE || rv->GetTypeID() == PTID_SMOKE ||
                       rv->GetTypeID() == PTID_FIRE))
        rv->HoldEffect();
    return rv;
}

// ---------------------------------- Step ----------------------------------------------------
int CParticleManager::Segment(bool bCombatPause)
{
    CManagerIterator iterator;
    CParticle *pParticle = (CParticle *)iterator.GetFirst(this);
    std::list<int> ids;
    for (; pParticle; pParticle = (CParticle *)iterator.GetNext())
    {
        if (bCombatPause && !pParticle->CheckFlag(PS_UNPAUSABLE))
            continue;
        if (pParticle->Segment() == FALSE)
        {
            if (referencedParticles.Seek(pParticle->GetID()))
            {
                ids.push_back(pParticle->GetID());
                pParticle->Release();
            }
        }
    }
    // actually remove
    for (auto pos = ids.begin(); pos != ids.end(); ++pos)
        referencedParticles.RemoveAt(*pos);
    //
    return 0;
}

void CParticleManager::EraseNotReferencedParticleEffects()
{
    CManagerIterator iterator;
    CParticle *pParticle = (CParticle *)iterator.GetFirst(this);
    std::list<int> ids;
    while (pParticle)
    {
        if (pParticle->GetRefCount() == 1)
        {
            if (referencedParticles.Seek(pParticle->GetID()))
            {
                ids.push_back(pParticle->GetID());
                pParticle->Release();
            }
        }
        pParticle = (CParticle *)iterator.GetNext();
    }
    // actually remove
    for (auto pos = ids.begin(); pos != ids.end(); ++pos)
        referencedParticles.RemoveAt(*pos);
    //
}
// ---------------------------- Memory management --------------------------------------------

void CParticleManager::deleteParticle(SSingleParticle *pSPart)
{
    singleParticlesPool.AddTail(pSPart);
}

SSingleParticle *CParticleManager::newParticle()
{
    if (!singleParticlesPool.IsEmpty())
        return singleParticlesPool.RemoveHead();
    else
        return dbgnew SSingleParticle;
}
// ---------------------------------- Draw ---------------------------------------------------
struct SParticleSort
{
    float fpZ;
    CParticle *pParticle;
    SParticleSort() = default;
    SParticleSort(float z, CParticle *p)
        : fpZ(z)
        , pParticle(p)
    {
    }
};

static CArray<SParticleSort> sources;

/*static int __cdecl CmpParticleDepth( const void *a, const void *b )
{
        const float *fpA = (const float*)a, *fpB = (const float*)b;
        return fpA[0] < fpB[0];
}*/
struct CmpParticleDepth
{
    bool operator()(const SParticleSort &a, const SParticleSort &b) { return a.fpZ > b.fpZ; }
};

BOOL CParticleManager::Draw(CRenderer *pTarget)
{
    int i;
    BOOL bUseAddEffect = FALSE;

    CManagerIterator iterator;
    CParticle *pParticle = (CParticle *)iterator.GetFirst(this);
    // отсортируем источники по глубине
    sources.SetSize(0);
    CPlot3D vecLook;
    vecLook = theCamera.GetZAxis();
    while (pParticle)
    {
        sources.Add(SParticleSort(pParticle->GetPosition() * vecLook, pParticle));
        pParticle = (CParticle *)iterator.GetNext();
    }
    std::sort(sources.GetData(), sources.GetData() + sources.GetSize(), CmpParticleDepth());
    // нарисуем каждый particle source
    for (i = 0; i < sources.GetSize(); i++)
    {
        pParticle = sources[i].pParticle;
        if (pParticle->CheckFlag(PS_DONT_DRAW))
            continue;
        // particle sources bound to invisible units are not rendered
        if (pParticle->GetCarrier() && pParticle->GetCarrier()->WhoAmI() == HPC_UNIT &&
            !pParticle->GetCarrier()->CheckFlag(UNIT_VISIBLE))
            continue;
        if (pParticle->IsUseAddEffect() == TRUE && bUseAddEffect == FALSE)
        {
            pTarget->SetAlphaBlendMode(BM_SRCALPHA, BM_ONE); // 'add' effect
            bUseAddEffect = TRUE;
        }
        if (pParticle->IsUseAddEffect() == FALSE && bUseAddEffect == TRUE)
        {
            pTarget->SetAlphaBlendMode(BM_SRCALPHA, BM_INVSRCALPHA);
            bUseAddEffect = FALSE;
        }

        if (!pParticle->Draw(pTarget))
            return FALSE;
    }

    return TRUE;
}

// -------------------------------- Fires stuff ----------------------------------------------
void CParticleManager::HoldFires()
{
    CManagerIterator iterator;
    CParticle *pParticle = (CParticle *)iterator.GetFirst(this);
    while (pParticle)
    {
        if (pParticle->GetTypeID() == PTID_CAMPFIRE || pParticle->GetTypeID() == PTID_SMOKE ||
            pParticle->GetTypeID() == PTID_FIRE)
            pParticle->HoldEffect();

        pParticle = (CParticle *)iterator.GetNext();
    }
    bHoldFires = TRUE;
}

void CParticleManager::ResumeFires()
{
    CManagerIterator iterator;
    CParticle *pParticle = (CParticle *)iterator.GetFirst(this);
    while (pParticle)
    {
        if (pParticle->GetTypeID() == PTID_CAMPFIRE || pParticle->GetTypeID() == PTID_SMOKE ||
            pParticle->GetTypeID() == PTID_FIRE)
            pParticle->RunEffect();

        pParticle = (CParticle *)iterator.GetNext();
    }
    bHoldFires = FALSE;
}
