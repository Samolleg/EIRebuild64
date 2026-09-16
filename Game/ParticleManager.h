#pragma once
/**********************************************************************
 *<
        FILE: ParticleManager.H

        DESCRIPTION:  Particle systems manager header

        CREATED BY: Ivanov Evgeny

 *>	Copyright (c) 1998, Nival Enterntainemnt
 **********************************************************************/
#include "BaseManager.h"

class CParticle;
struct SSingleParticle;
// ------------------------------ ParticleManager --------------------------------------------

class CParticleManager : public CBaseManager
{
private:
    CList<SSingleParticle *, SSingleParticle *> singleParticlesPool;
    CHashMap<int, CParticle *> referencedParticles;
    BOOL bHoldFires;

public:
    // Constr/desctr
    CParticleManager();
    ~CParticleManager() { Destroy(); }
    virtual BOOL Create();
    virtual void Destroy();

    // Create particle system object
    CParticle *CreateParticleSystem(ETypeIDs tidTypeID);
    // Draw
    BOOL Draw(CRenderer *pTarget);
    int Segment(bool bCombatPause);
    void EraseNotReferencedParticleEffects();
    // Memory management
    void deleteParticle(SSingleParticle *pSPart);
    SSingleParticle *newParticle();
    // CRAP for demo about fires
    void HoldFires();
    void ResumeFires();

    // class identification information
    virtual LPCSTR GetGameClassName() const { return "CParticleManager"; };
    virtual EClassIDs WhoAmI() const { return HPC_PARTICLEMANAGER; }

protected:
    LPGAMEBASE CreateInstant(ETypeIDs tidTypeID);

private:
    // Initialization
    void Clear();
};

inline CParticleManager::CParticleManager()
{
    Clear();
#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
}
