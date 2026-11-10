#include "StdAfx.h"
#include "embedded_particle_flow.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "Particle.h"
#include "LiveObserver.h"
#include "Effect.h"
#include "EffectManager.h"
#include "UnitServer.h"
#include "WorldServer.h"
#include <map>

#include "Object.h"
#include "tools.h"
#include "ParticleManager.h"
#include "GameJournal.h"
#include "Notification.h"
#include "WorldClient.h"
#include "RandomGen.h"
#include "Renderer.h"
#include "light.h"
#ifndef _A4
#include "ServerConsts.h"
#include "Unit.h"
#include "UnitClientSpecific.h"
#include "Camera.h"
#endif
/**********************************************************************
 *<
        FILE: Particle.CPP

        DESCRIPTION:  Particle system object implementation

        CREATED BY: Ivanov Evgeny

 *>	Copyright (c) 1998, Nival Enterntainemnt
 **********************************************************************/

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#define new DEBUG_NEW
#endif

const int N_TEXTURE_SIZE = 256;
// Tornado consts
const float TORNADO_HEIGHT = 12;
const int TORNADO_NUM_SOURCES = 6;
const int N_BLOOD_SOURCES = 3;
const int N_PATHDEST_ROUND_PARTS = 128;
const int N_PATHDEST_CROSS_PARTS = 32; // must be power of 2
const int N_MOSHKA_SOURCES = 80;
//
const int N_FRAMES_TO_PAUSE = 56;

static SXYZW &operator+=(SXYZW &res, const CPlot3D &arg)
{
    res.x += arg.x;
    res.y += arg.y;
    res.z += arg.z;
    return res;
}

static SXYZW &operator-=(SXYZW &res, const CPlot3D &arg)
{
    res.x -= arg.x;
    res.y -= arg.y;
    res.z -= arg.z;
    return res;
}

static CPlot3D GetRandomBallPoint()
{
    float fpX, fpY, fpZ;
    for (int nSafe = 10; nSafe > 0; nSafe--)
    {
        fpX = random.GetFloat(-1, 1);
        fpY = random.GetFloat(-1, 1);
        fpZ = random.GetFloat(-1, 1);
        if (sqr(fpX) + sqr(fpY) + sqr(fpZ) < 1)
            break;
    }
    return CPlot3D(fpX, fpY, fpZ);
}

static CPlot3D GetRandomSpherePoint()
{
    float fpX, fpY, fpZ, fpLeng;
    for (int nSafe = 10; nSafe > 0; nSafe--)
    {
        fpX = random.GetFloat(-1, 1);
        fpY = random.GetFloat(-1, 1);
        fpZ = random.GetFloat(-1, 1);
        fpLeng = sqr(fpX) + sqr(fpY) + sqr(fpZ);
        if (fpLeng < 1)
            break;
    }
    return CPlot3D(fpX, fpY, fpZ) / sqrt(fpLeng);
}

static CPlot3D GetRandomSpherePoint(const CPlot3D &ptShift)
{
    float fpX, fpY, fpZ, fpLeng;
    for (int nSafe = 10; nSafe > 0; nSafe--)
    {
        fpX = random.GetFloat(-1, 1) + ptShift.x;
        fpY = random.GetFloat(-1, 1) + ptShift.y;
        fpZ = random.GetFloat(-1, 1) + ptShift.z;
        fpLeng = sqr(fpX) + sqr(fpY) + sqr(fpZ);
        if (fpLeng < 1)
            break;
    }
    return CPlot3D(fpX, fpY, fpZ) / sqrt(fpLeng);
}

static CPlot3D GetRandomRoundPoint()
{
    float fpX, fpY;
    for (int nSafe = 10; nSafe > 0; nSafe--)
    {
        fpX = random.GetFloat(-1, 1);
        fpY = random.GetFloat(-1, 1);
        if (sqr(fpX) + sqr(fpY) < 1)
            break;
    }
    return CPlot3D(fpX, fpY, 0);
}

static CPlot3D GetCenteredRandomRoundPoint()
{
    float fpR, fpA;
    fpR = random.GetFloat(0, 1);
    fpA = random.GetFloat(0, 2 * 3.14159f);
    return CPlot3D(fpR * sin(fpA), fpR * cos(fpA), 0);
}
// --------------------------------- Initialization
// --------------------------------------------------------

namespace {
std::map<CParticle*,CSmartPtr<CEffectPointLight>> powerHitLights;
bool PowerHitCarrierActive(CObject3D*carrier,const void*world,int hit,bool delivered,std::uintptr_t weapon){
 if(!carrier||!world||world!=theWorldServer.GetSpecificManager()||theWorld.GetUnit(carrier->GetID())!=carrier)return false;
 auto*unit=theWorldServer.GetUnit(carrier->GetID());auto*server=unit?dynamic_cast<CUnitServer*>(unit->GetUnitSpecific()):nullptr;
 return server&&server->PowerHitTrailActive(hit,delivered,weapon);
}
void UpdatePowerHitLight(CParticle*particle,const CPlot3D&point,float strength){
 if(strength<=0||!theWorld.GetEffectManager()){powerHitLights.erase(particle);return;}
 auto found=powerHitLights.find(particle);bool first=found==powerHitLights.end();
 if(first){auto*effect=static_cast<CEffectPointLight*>(theWorld.GetEffectManager()->CreateData(ETypeIDs(HPC_EFFECTPOINTLIGHT)));if(!effect)return;found=powerHitLights.emplace(particle,CSmartPtr<CEffectPointLight>(effect)).first;}
 auto*effect=static_cast<CEffectPointLight*>(found->second);effect->SetCP(point);effect->color=CPlot3D(1,.65f,.22f)*strength;effect->fpRange=2;effect->dwFlags=0x840;effect->ApplyParams(first?utStart:utFrame);
}
}
void CParticle::StopPowerHitLights(){for(auto&entry:powerHitLights)entry.first->SetParam2(-1);powerHitLights.clear();}
void CParticle::Destroy()
{
    powerHitLights.erase(this);
    while (!particles.IsEmpty())
        ((CParticleManager *)lpManager)->deleteParticle(particles.RemoveHead());
    pTexture.Clear();
    if (pCarrier)
        pCarrier->DetachParticleSource(this);
    Clear();
}

void CParticle::Clear()
{
    SetCP(CPlot3D(0, 0, 0));
    SetOrientation(QNULL);
    pCarrier = NULL;
    carrierCP = GetCP();
    scriptFlowRandom = 0x13579bdu;
    arenaWave = {};
    golemWall = {};
    nBornProbability = 0;
    nMinTrys = 0;
    nLifeSpan = -1;
    nAnimPhases = 1;
    nAnimPerTexSize = 1;
    nLifeOfPhase = 10000;
    pTexture.Clear();
    createFunc = NULL;
    segmentFunc = NULL;
    locSourceSegmentFunc = NULL;
    windDir = CPlot3D(0, 0, 0);
    fpWindForce = 0;
    ptWallDirection = CPlot3D(0, 0, 0);
    bUseAddEffect = FALSE;
    locSources.SetSize(0);
    fpPower = 1;
    fpPower2 = 1;
    fpSize = 1;
    fpCurrentScale = 1;
    fpLenToSingleDice = 1000;
    posRespectMinRnd = CPlot3D(0, 0, 0);
    posRespectMaxRnd = CPlot3D(0, 0, 0);
    dwPFlags = 0;
    fpSpeedFactor = 0;
    fpSizeFactor = 0;
    bindPlace = bpCast;
    nInvisibleCounter = 0;
    fpParam1 = 0;
    fpParam2 = 0;
}

void CParticle::AttachCarrier(CObject3D *pNewCarrier)
{
    CObject3D *pOldCarrier = pCarrier;
    // avoid recursion
    if (pCarrier == pNewCarrier)
    {
        if (pCarrier == NULL)
            carrierCP = GetCP();
        return;
    }
    pCarrier = NULL;
    // передвинем эффект, как будо он остался привязан к старому объекту
    if (pOldCarrier && pNewCarrier == NULL)
    {
        // The old carrier may already have retired its unit/weapon state.
        // carrierCP is the last resolved world point, including our offset.
        SetCP(carrierCP);
    }

    if (pOldCarrier)
        pOldCarrier->DetachParticleSource(this);
    pCarrier = pNewCarrier;
    if (pCarrier)
    {
        pCarrier->RecalcBoundingVolumeFast();
        pCarrier->AttachParticleSource(this);
        carrierCP = GetCarrierCP(pCarrier) + GetCP();
    }
    else
        carrierCP = GetCP();
}

CPlot3D CParticle::GetPosition()
{
    if (pCarrier)
        return GetCarrierCP(pCarrier) + GetCP();
    return GetCP();
}

#ifndef _A4
CPlot3D CParticle::GetBodyPartCP(CUnit *pUnit, const char *pszName)
{
    CFigure *pHead;
    pHead = pUnit->MatchFigure(pszName);
    if (pHead)
    {
        CPlot3D ptHead = pUnit->GetFigureWorldPosition(pHead);
        return ptHead;
    }
    // this should never happen but don`t let things mess up in the case
    pHead = pUnit->MatchFigure("bd");
    if (pHead)
    {
        CPlot3D ptHead = pUnit->GetFigureWorldPosition(pHead);
        return ptHead;
    }
    // life is getting harder
    CPlot3D ptRes = pUnit->GetCP();
    ptRes.z += pUnit->bv.ptMax.z;
    return ptRes;
}
#endif

CPlot3D CParticle::GetCarrierCP(CObject3D *pOldCarrier)
{
    if (!pOldCarrier)
        return CPlot3D(0, 0, 0);
#ifndef _A4
    if (CheckFlag(PS_BIND_TO_BODYPART))
    {
        if (pOldCarrier->WhoAmI() == HPC_UNIT)
        {
            CUnit *pUnit = (CUnit *)pOldCarrier;
            switch (bindPlace)
            {
            case bpCast:
                return pUnit->GetCastPointPosition();
            case bpAboveHead: {
                CPlot3D ptRes = GetBodyPartCP(pUnit, "hd");
                ptRes.z += 1;
                return ptRes;
            }
            break;
            case bpHead:
                return GetBodyPartCP(pUnit, "hd");
            case bpLHand:
                return GetBodyPartCP(pUnit, "lh2");
            case bpRHand:
                return GetBodyPartCP(pUnit, "rh2");
            case bpLLeg:
                return GetBodyPartCP(pUnit, "ll2");
            case bpRLeg:
                return GetBodyPartCP(pUnit, "rl2");
            case bpBody:
                return GetBodyPartCP(pUnit, "bd");
            case bpBody00:
                return GetBodyPartCP(pUnit, "body00");
            case bpBody01:
                return GetBodyPartCP(pUnit, "body01");
            case bpBody02:
                return GetBodyPartCP(pUnit, "body02");
            case bpBody03:
                return GetBodyPartCP(pUnit, "body03");
            case bpBody04:
                return GetBodyPartCP(pUnit, "body04");
            case bpRStaff:
            case bpLStaff:
                if (pUnit->GetUnitSpecific()->WhoAmI() == HPC_UNITCLIENTSPECIFIC) {
                    auto *specific = static_cast<CUnitClientSpecific *>(pUnit->GetUnitSpecific());
                    const auto node = specific->GetWeaponParticleNode(bindPlace == bpLStaff);
                    return GetBodyPartCP(pUnit, node.c_str());
                }
                return GetBodyPartCP(pUnit, bindPlace == bpLStaff ? "lh3" : "rh3");
            }
        }

        SBoundingVolume &cbv = pOldCarrier->bv;
        float fpHeight; // fpRadius,
        if (cbv.IsSphere())
        {
            // fpRadius = cbv.fpRadius;
            fpHeight = cbv.fpRadius * 2 * 0.8f;
        }
        else
        {
            // fpRadius = Max( cbv.ptMax.x - cbv.ptMin.x, cbv.ptMax.y - cbv.ptMin.y ) * 0.5f;
            fpHeight = (cbv.ptMax.z - cbv.ptMin.z);
        }
        return CPlot3D(pOldCarrier->GetCP().x, pOldCarrier->GetCP().y,
                       pOldCarrier->GetCP().z + fpHeight);
    }
#else
#ifndef __A4_EDITOR__
    if (CheckFlag(PS_BIND_TO_BODYPART) && pOldCarrier->WhoAmI() == HPC_UNIT)
    {
        CMobileObject3D *pUnit = (CMobileObject3D *)pOldCarrier;
        switch (bindPlace)
        {
        case bpBody:
            return pUnit->GetSoulPoint();
        }
    }
#endif //__A4_EDITOR__
#endif //_A4
    return pOldCarrier->GetCP();
}

void CParticle::GetObjectBV(CPlot3D &ptCenter, float &fpRadius)
{
    if (!pCarrier)
    {
        ptCenter = CPlot3D(0, 0, 0);
        fpRadius = 0.1f;
        return;
    }
    SBoundingVolume &cbv = ((CObject3D *)pCarrier)->bv;
    ((CObject3D *)pCarrier)->RecalcBoundingVolumeFast();
    if (cbv.IsSphere())
    {
        fpRadius = cbv.fpRadius;
        // ptStart.z -= cbv.fpRadius;
    }
    else
    {
        fpRadius = sqrt(sqr(cbv.ptMax.z - cbv.ptMin.z) + sqr(cbv.ptMax.x - cbv.ptMin.x) +
                        sqr(cbv.ptMax.y - cbv.ptMin.y)) *
                   0.5f;
        // ptStart.z = cbv.ptCenter.z + cbv.ptMin.z * 1.5f;
    }
    ptCenter = CPlot3D(0, 0, cbv.ptCenter.z);
}

void CParticle::FinishEffect()
{
    ResetFlag(PS_GENERATE_PARTICLES);
    ResetFlag(PS_KEEP_ALIVE);
    // nGenerateParticles = 0;
    AttachCarrier(NULL);
}

void CParticle::DeleteEffect()
{
    while (!particles.IsEmpty())
        ((CParticleManager *)lpManager)->deleteParticle(particles.RemoveHead());
    ResetFlag(PS_GENERATE_PARTICLES);
    ResetFlag(PS_KEEP_ALIVE);
    // nGenerateParticles = 0;
    AttachCarrier(NULL);
}

void CParticle::HoldEffect()
{
    ResetFlag(PS_GENERATE_PARTICLES);
    SetFlag(PS_KEEP_ALIVE);
}

void CParticle::RunEffect()
{
    SetFlag(PS_GENERATE_PARTICLES);
    SetFlag(PS_KEEP_ALIVE);
}

void CParticle::SetClipState(bool bVisible)
{
    if (bVisible)
    {
        ResetFlag(PS_PAUSE_INVISIBLE);
        ResetFlag(PS_DONT_DRAW);
    }
    else
    {
        SetFlag(PS_PAUSE_INVISIBLE);
        SetFlag(PS_DONT_DRAW);
    }
}

inline void CParticle::UpdateBV(SSingleParticle *pSPart)
{
    bvMin.x = Min(bvMin.x, pSPart->coord.x);
    bvMin.y = Min(bvMin.y, pSPart->coord.y);
    bvMin.z = Min(bvMin.z, pSPart->coord.z);
    bvMax.x = Max(bvMax.x, pSPart->coord.x);
    bvMax.y = Max(bvMax.y, pSPart->coord.y);
    bvMax.z = Max(bvMax.z, pSPart->coord.z);
}

inline void CParticle::FinishBV()
{
    CPlot3D ptCenter((bvMax.x + bvMin.x) * 0.5f, (bvMax.y + bvMin.y) * 0.5f,
                     (bvMax.z + bvMin.z) * 0.5f);
    bv.SetCenter(ptCenter);
    bv.SetBox(bvMin - ptCenter, bvMax - ptCenter);
}

// ------------------------------------ Scale effect
// -------------------------------------------------------

void CParticle::Scale(float fpScaleFactor)
{
    posRespectMinRnd *= fpScaleFactor;
    posRespectMaxRnd *= fpScaleFactor;
    fpSize *= fpScaleFactor;
    fpCurrentScale *= fpScaleFactor;
    fpSpeedFactor *= fpScaleFactor;
    fpLenToSingleDice *= fpScaleFactor;

    // That is crooked piece, but i have no time to get it right
    /*if ( GetTypeID() != PTID_FIREBLAST )
            return;
    for ( int i = 0; i < locSources.GetSize(); i++ )
    {
            locSources[i].respectCP = CPlot3D( 0, 0, 0 );

            CPlot3D vec;
            vec.x = random.GetFloat( posRespectMinRnd.x, posRespectMaxRnd.x );
            vec.y = random.GetFloat( posRespectMinRnd.y, posRespectMaxRnd.y );
            vec.z = random.GetFloat( posRespectMinRnd.z, posRespectMaxRnd.z );

            float fpMod = ( 1.f / FSqrt( vec.x*vec.x + vec.y*vec.y + vec.z*vec.z ) ) * fpSpeedFactor
    * 0.5f;

            locSources[i].vSpeed = vec * fpMod;
            locSources[i].nPhase = 0;
            locSources[i].reserved = CPlot3D( 0.95f, 0, 0 );
    }
    */
}

void CParticle::EmptyLocSourceSegmentFunc(SLocalSource *)
{
}

// ------------------------------------ Create
// -------------------------------------------------------------

const char* CParticle::ConfigureArrow(ETypeIDs type)
{
    const char* texture;
    switch(type){
    case PTID_RICKARROW: texture="RikArrowSmoke";break;
    case PTID_FIREARROW: case PTID_STAFFARROWFIRE: texture="FireArrowSmoke";break;
    case PTID_STAFFARROWLIGHT: texture="lightArrowSmoke";break;
    case PTID_STAFFARROWWATER: texture="acidray";break;
    case PTID_STAFFARROWACID: texture="acidrayorig";break;
    default:return nullptr;
    }
    nMinTrys=1;fpLenToSingleDice=.06f;nLifeSpan=15;nAnimPhases=15;nLifeOfPhase=1;
    posRespectMinRnd=CPlot3D(-.2f,-.2f,-.2f);posRespectMaxRnd=CPlot3D(.2f,.2f,.2f);
    fpSize=.3f;fpSizeFactor=1.03f;dwPFlags=0;
    createFunc=&CParticle::FireArrowSingleCreate;segmentFunc=&CParticle::FireArrowSingleSegment;
    if(type>=PTID_STAFFARROWFIRE&&type<=PTID_STAFFARROWACID)fpSpeedFactor=0;
    return texture;
}

#include "ModExtendedParticle.inl"

BOOL CParticle::Create()
{
    int i;
    Clear();
    ClearBV();
    char pszTexName[256];
    SetFlag(PS_GENERATE_PARTICLES); // nGenerateParticles = 1;
    ResetFlag(PS_UNPAUSABLE);
    ResetFlag(PS_BIND_TO_BODYPART);
    nAnimPerTexSize = 4;
    nBornProbability = 100;
    nParticlesMax = 500;
    nAnimPhases = 16;
    if(ConfigureExtendedSpell(GetTypeID(),pszTexName))InitializeExtendedSpellSources();
    else switch (GetTypeID())
    {
    case PTID_SPHEREACID:
    case PTID_SPHEREELECTRICITY:
    case PTID_SPHEREFIRE:
    case PTID_ANTIMAGIC:
        bUseAddEffect = TRUE;
        nBornProbability = 90;
        SetFlag(PS_NEED_CARRIER_TO_GENERATE);
        nMinTrys = 1;
        if (GetTypeID() == PTID_ANTIMAGIC)
            nLifeSpan = 4; // 15;
        else
            nLifeSpan = 15;

        nAnimPhases = 15;
        nLifeOfPhase = 1;
        posRespectMinRnd = CPlot3D(-0.2f, -0.2f, -1.f);
        posRespectMaxRnd = CPlot3D(0.2f, 0.2f, -0.f);
        fpSize = 1;
        // fpSizeFactor = 0.94f;
        // ptWallDirection = CPlot3D( 0.5f, 0, 0 );

        createFunc = &CParticle::SphereSingleCreate;
        segmentFunc = &CParticle::SphereSingleSegment;
        locSourceSegmentFunc = &CParticle::EmptyLocSourceSegmentFunc;
        if (GetTypeID() == PTID_SPHEREFIRE)
            strcpy(pszTexName, "ProtectionF");
        else if (GetTypeID() == PTID_SPHEREELECTRICITY)
            strcpy(pszTexName, "ProtectionE");
        else if (GetTypeID() == PTID_ANTIMAGIC)
            strcpy(pszTexName, "AntiMagic");
        else
            strcpy(pszTexName, "ProtectionA");
        break;
    case PTID_FIREBALL:
        nBornProbability = 88;
        nMinTrys = 1;
        fpLenToSingleDice = 0.09f;
        nLifeSpan = 15;
        nAnimPhases = 15;
        nLifeOfPhase = 1;
        posRespectMinRnd = CPlot3D(-0.2f, -0.2f, -1.f);
        posRespectMaxRnd = CPlot3D(0.2f, 0.2f, -0.f);
        fpSize = 0.8f;
        fpSizeFactor = 0.94f;

        createFunc = &CParticle::FireballSingleCreate;
        segmentFunc = &CParticle::FireballSingleSegment;
        strcpy(pszTexName, "FireSmoke");
        break;
    case PTID_RICKARROW:
    case PTID_FIREARROW:
    case PTID_STAFFARROWFIRE:
    case PTID_STAFFARROWLIGHT:
    case PTID_STAFFARROWWATER:
    case PTID_STAFFARROWACID:
        strcpy(pszTexName,ConfigureArrow(GetTypeID()));
        break;
    case PTID_ACIDRAY:
        nMinTrys = 1;
        fpLenToSingleDice = 0.06f;
        nLifeSpan = 15;
        nAnimPhases = 15;
        nLifeOfPhase = 1;
        posRespectMinRnd = CPlot3D(-1, -1, -1);
        posRespectMaxRnd = CPlot3D(1, 1, 1);
        fpSize = 0.3f;

        createFunc = &CParticle::AcidRaySingleCreate;
        segmentFunc = &CParticle::AcidRaySingleSegment;
        strcpy(pszTexName, "AcidRay");
        break;
    case PTID_LINK:
        nBornProbability = 80;
        nMinTrys = 10;
        nLifeSpan = 45;
        nAnimPhases = 15;
        nLifeOfPhase = 3;
        posRespectMinRnd = CPlot3D(-0.2f, -0.2f, -0.2f);
        posRespectMaxRnd = CPlot3D(0.2f, 0.2f, 0.2f);
        fpSize = 1;
        // some default value
        ptWallDirection = CPlot3D(-10, 0, 0);

        createFunc = &CParticle::LinkSingleCreate;
        segmentFunc = &CParticle::LinkSingleSegment;
        locSourceSegmentFunc = &CParticle::LinkLocSourceSegmentFunc;
        strcpy(pszTexName, "Link");
        break;
    case PTID_BLUEGAS:
        nBornProbability = 80;
        SetFlag(PS_NEED_CARRIER_TO_GENERATE);
        nMinTrys = 5;
        nLifeSpan = 45;
        nAnimPhases = 15;
        nLifeOfPhase = 3;
        posRespectMinRnd = CPlot3D(-1, -1, -1);
        posRespectMaxRnd = CPlot3D(1, 1, 1);
        fpSize = 1;

        createFunc = &CParticle::BlueGasSingleCreate;
        segmentFunc = &CParticle::BlueGasSingleSegment;
        locSourceSegmentFunc = &CParticle::EmptyLocSourceSegmentFunc;
        strcpy(pszTexName, "Stench");
        break;
    case PTID_CAMPFIRE:
    case PTID_SMOKE:
    case PTID_FIRE:
    case PTID_FIREWALL:
        nBornProbability = 88;
        nMinTrys = 6;
        nLifeSpan = 32; // 13;
        nAnimPhases = 15;
        nLifeOfPhase = 1;
        posRespectMinRnd = CPlot3D(-0.2f, -0.2f, 0);
        posRespectMaxRnd = CPlot3D(0.2f, 0.2f, 0);
        fpSize = 0.8f;
        fpSpeedFactor = 0.30f; // Particles Z movement speed
        fpSizeFactor = 0.85f;

        createFunc = &CParticle::CampfireSingleCreate;
        segmentFunc = &CParticle::CampfireSingleSegment;
        if (GetTypeID() == PTID_FIREWALL)
        {
            nMinTrys = 20;
            nParticlesMax = 1500;
            locSources.SetSize(1);
            createFunc = &CParticle::FireWallSingleCreate;
            locSourceSegmentFunc = &CParticle::FireLocSourceSegmentFunc;
        }
        strcpy(pszTexName, "TendriSmoke");
        break;
    case PTID_FIREBLAST: {
        nBornProbability = 100;
        nMinTrys = 60; // 300;
        nParticlesMax = 600;
        nLifeSpan = 15;
        nLifeOfPhase = 1;
        posRespectMinRnd = CPlot3D(-0.02f, -0.02f, -0.02f);
        posRespectMaxRnd = CPlot3D(0.02f, 0.02f, 0.02f);
        fpSize = 0.6f;
        fpSpeedFactor = 1.0f; // Particles radial movement speed
        fpSizeFactor = 0.8f;

        createFunc = &CParticle::FireBlastSingleCreate;
        segmentFunc = &CParticle::FireBlastSingleSegment;
        locSourceSegmentFunc = &CParticle::FireBlastLocSourceSegmentFunc;
        strcpy(pszTexName, "Explosion");

        // Create local sources
        locSources.SetSize(15);
        for (i = 0; i < locSources.GetSize(); i++)
        {
            locSources[i].respectCP = CPlot3D(0, 0, 0);

            CPlot3D vec;
            vec = GetRandomSpherePoint(CPlot3D(0, 0, 0.7f));
            // vec.x = random.GetFloat( posRespectMinRnd.x, posRespectMaxRnd.x );
            // vec.y = random.GetFloat( posRespectMinRnd.y, posRespectMaxRnd.y );
            // vec.z = random.GetFloat( posRespectMinRnd.z, posRespectMaxRnd.z );

            // float fpMod = ( 1.f / FSqrt( vec.x*vec.x + vec.y*vec.y + vec.z*vec.z ) ) *
            // fpSpeedFactor * 0.5f;
            locSources[i].vSpeed = vec * random.GetFloat(0.8f, 1.2f) * 0.8f; // * fpMod;

            locSources[i].nPhase = 0;
            locSources[i].reserved.x = 0; // = //CPlot3D( 0.95f, 0, 0 );
            locSources[i].reserved.y = 0.4f;
            locSources[i].reserved.z = 0.7f;
            locSources[i].reserved2.x = random.Get(0, 5);
            locSources[i].reserved2.y = random.Get(0.7f, 1.4f);
        }
        // bUseAddEffect = TRUE;
    }
    break;
    case PTID_VULCANSMOKE:
        nBornProbability = 88;
        nMinTrys = 2;
        nParticlesMax = 400;
        nLifeSpan = 16;
        nLifeOfPhase = 1;
        posRespectMinRnd = CPlot3D(0.2f, 0.5f, 0); // radial speed, height of cloud
        posRespectMaxRnd = CPlot3D(0.3f, 4.0f, 0); // radius of pillar, height of pillar
        fpSize = 2.5f;                             // size of cloud particle
        fpSpeedFactor = posRespectMaxRnd.y / float(nLifeSpan * 4); // speed of pillar particle
        fpSizeFactor = 0.99f;                                      // scaling of cloud partcle

        createFunc = &CParticle::VulcanSmokeSingleCreate;
        segmentFunc = &CParticle::VulcanSmokeSingleSegment;
        strcpy(pszTexName, "Vulkan");
        locSources.SetSize(0);
        break;
    case PTID_HEALING:
        bUseAddEffect = TRUE;
        SetFlag(PS_NEED_CARRIER_TO_GENERATE);
        nMinTrys = 0x7FFFFFFF;
        nLifeSpan = 8;
        nLifeOfPhase = 1;
        fpSize = 0.5f;

        createFunc = &CParticle::HealingSingleCreate;
        segmentFunc = &CParticle::HealingSingleSegment;
        locSourceSegmentFunc = &CParticle::HealingLocSourceSegmentFunc;
        strcpy(pszTexName, "Healing");
        locSources.SetSize(0);
        break;
    case PTID_POISONFOG:
    case PTID_AGGRESSIONFOG:
        nBornProbability = 50;
        nMinTrys = 1; // 0x7FFFFFFF;
        nParticlesMax = 0x7FFFFFFF;

        nLifeSpan = 3236;
        nAnimPhases = 15;
        nLifeOfPhase = 1;

        createFunc = &CParticle::PoisonFogSingleCreate;
        segmentFunc = &CParticle::PoisonFogSingleSegment;
        strcpy(pszTexName, "PoisonCloud");
        break;
    case PTID_GEYSER:
        nBornProbability = 88;
        nMinTrys = 20;

        nLifeOfPhase = 100;
        fpSpeedFactor = 0.3f; // movement speed

        posRespectMinRnd = CPlot3D(-0.1f, -0.1f, -0.02f);
        posRespectMaxRnd = CPlot3D(0.1f, 0.1f, 0.02f);

        locSources.SetSize(3);
        for (i = 0; i < locSources.GetSize(); i++)
        {
            locSources[i].nLifeTime = 0; // Remaining function time
            locSources[i].nPhase = 0;    // Remaining relax time
        }
        createFunc = &CParticle::GeyserSingleCreate;
        segmentFunc = &CParticle::GeyserSingleSegment;
        locSourceSegmentFunc = &CParticle::GeyserLocSourceSegmentFunc;
        strcpy(pszTexName, "Fireworks");
        bUseAddEffect = TRUE;
        break;
    case PTID_TORNADO:
        nBornProbability = 100;
        nMinTrys = 4;

        nParticlesMax = 800;
        nLifeOfPhase = 1;
        fpSpeedFactor = 0.2f; // movement speed
        nLifeOfPhase = D3DRGBA(theRenderer.GetSunlight()->GetDiffuse().r,
                               theRenderer.GetSunlight()->GetDiffuse().g,
                               theRenderer.GetSunlight()->GetDiffuse().b,
                               0.1f // float(0x30) / 0xff
        );

        locSources.SetSize(TORNADO_NUM_SOURCES);
        for (i = 0; i < locSources.GetSize(); i++)
        {
            SLocalSource &src = locSources[i];
            src.respectCP = CPlot3D(0, 0, i * TORNADO_HEIGHT / (TORNADO_NUM_SOURCES - 1));
            locSources[i].nLifeTime = 0; // Remaining function time
            locSources[i].nPhase = 0;    // Remaining relax time
        }
        locSources[0].reserved2.x = TORNADO_HEIGHT;
        locSources[0].reserved2.y = 0.5f;
        locSources[0].reserved2.z = 0.99f;
        createFunc = &CParticle::TornadoSingleCreate;
        segmentFunc = &CParticle::TornadoSingleSegment;
        locSourceSegmentFunc = &CParticle::TornadoLocSourceSegmentFunc;
        strcpy(pszTexName, "Tornado"); // Vulkan" );
        //			bUseAddEffect = TRUE;
        break;
    case PTID_CURSEHOLDER:
        nBornProbability = 100;
        nMinTrys = 4;
        SetFlag(PS_NEED_CARRIER_TO_GENERATE);
        SetFlag(PS_BIND_TO_BODYPART);
        bindPlace = bpBody;

        nParticlesMax = 800;
        nLifeOfPhase = 1;
        fpSpeedFactor = 0.2f;                // movement speed
        nLifeOfPhase = D3DRGBA(0, 0, 0, 0.1f // float(0x30) / 0xff
        );

        locSources.SetSize(TORNADO_NUM_SOURCES);
        for (i = 0; i < locSources.GetSize(); i++)
        {
            SLocalSource &src = locSources[i];
            src.respectCP = CPlot3D(0, 0, i * TORNADO_HEIGHT / (TORNADO_NUM_SOURCES - 1));
            locSources[i].nLifeTime = 0; // Remaining function time
            locSources[i].nPhase = 0;    // Remaining relax time
        }
        locSources[0].reserved2.x = TORNADO_HEIGHT * 0.4f;
        locSources[0].reserved2.y = 0;
        locSources[0].reserved2.z = 0.997f;
        createFunc = &CParticle::TornadoSingleCreate;
        segmentFunc = &CParticle::TornadoSingleSegment;
        locSourceSegmentFunc = &CParticle::TornadoLocSourceSegmentFunc;
        strcpy(pszTexName, "Tornado"); // Vulkan" );
        //			bUseAddEffect = TRUE;
        break;
    case PTID_CASTING:
        SetFlag(PS_NEED_CARRIER_TO_GENERATE);
        nMinTrys = 0x7FFFFFFF;
        nLifeSpan = -1;
        nLifeOfPhase = 1;

        createFunc = &CParticle::CastingSingleCreate;
        segmentFunc = &CParticle::CastingSingleSegment;
        locSourceSegmentFunc = &CParticle::CastingLocSourceSegmentFunc;
        strcpy(pszTexName, "FireSmoke");
        locSources.SetSize(0);
        break;

    case PTID_BANSHEECASTING:
        SetFlag(PS_NEED_CARRIER_TO_GENERATE);
        nMinTrys = 0x7FFFFFFF;
        nLifeSpan = -1;
        nLifeOfPhase = 1;

        createFunc = &CParticle::BansheeCastingSingleCreate;
        segmentFunc = &CParticle::BansheeCastingSingleSegment;
        locSourceSegmentFunc = &CParticle::BansheeCastingLocSourceSegmentFunc;
        strcpy(pszTexName, "Banshee");
        locSources.SetSize(0);
        break;

    case PTID_NUKE:
        nMinTrys = 128; // 0x7FFFFFFF;
        nParticlesMax = 400;
        nLifeSpan = -1;
        nLifeOfPhase = 1;
        fpSpeedFactor = 1; // following variables are calculated further

        createFunc = &CParticle::NukeSingleCreate;
        segmentFunc = &CParticle::NukeSingleSegment;
        locSourceSegmentFunc = &CParticle::NukeLocSourceSegmentFunc;
        strcpy(pszTexName, "AcidRay"); // Explosion" );//"FireSmoke" );
        locSources.SetSize(1);
        locSources[0].nLifeTime = 0; // Remaining function time
        break;
    case PTID_TRANSFORM:
    case PTID_MUSHROOM:
        // nMinTrys = 20;//0x7FFFFFFF;
        nMinTrys = 10;
        // nParticlesMax = 400;
        nParticlesMax = 4000;
        nLifeSpan = -1;
        nLifeOfPhase = 1;
        fpSpeedFactor = 1; // following variables are calculated further

        createFunc = &CParticle::MushroomSingleCreate;
        segmentFunc = &CParticle::MushroomSingleSegment;
        locSourceSegmentFunc = &CParticle::MushroomLocSourceSegmentFunc;
        if (GetTypeID() == PTID_FIREBLAST)
            strcpy(pszTexName, "Explosion"); //"FireSmoke" );
        else
            strcpy(pszTexName, "ChangeHero"); //"FireSmoke" );
        break;
    case PTID_BLOOD1:
    case PTID_BLOOD2:
    case PTID_BLOOD3:
    case PTID_BLOOD4:
        SetFlag(PS_NEED_CARRIER_TO_GENERATE);
        SetFlag(PS_BIND_TO_BODYPART);
        bindPlace = bpBody;

        nMinTrys = 10; // 15;
        nParticlesMax = 300;
        nLifeSpan = 6;
        nLifeOfPhase = 15;
        posRespectMinRnd = CPlot3D(-0.2f, -0.2f, -0.2f);
        posRespectMaxRnd = CPlot3D(0.2f, 0.2f, 0.2f);

        createFunc = &CParticle::BloodSingleCreate;
        segmentFunc = &CParticle::BloodSingleSegment;
        locSourceSegmentFunc = &CParticle::BloodLocSourceSegmentFunc;
        strcpy(pszTexName, "RainSnow");
        locSources.SetSize(1);
        locSources[0].nPhase = -100; // mark startup mode
        switch (GetTypeID())
        {
        case PTID_BLOOD1:
            locSources[0].nLifeTime = 12;
            break;
        case PTID_BLOOD2:
            locSources[0].nLifeTime = 13;
            break;
        case PTID_BLOOD3:
            locSources[0].nLifeTime = 14;
            break;
        case PTID_BLOOD4:
            locSources[0].nLifeTime = 15;
            break;
        }
        break;
    case PTID_A4BLOODRED:
    case PTID_A4BLOODGREEN:
    case PTID_A4BLOODBLUE:
    case PTID_A4BLOODBLACK:
        SetFlag(PS_NEED_CARRIER_TO_GENERATE);
        SetFlag(PS_BIND_TO_BODYPART);
        bindPlace = bpBody;

        nMinTrys = 45;
        nParticlesMax = 300;
        nLifeSpan = 6;
        nLifeOfPhase = 15;
        posRespectMinRnd = CPlot3D(-0.2f, -0.2f, -0.2f);
        posRespectMaxRnd = CPlot3D(0.2f, 0.2f, 0.2f);

        createFunc = &CParticle::A4BloodSingleCreate;
        segmentFunc = &CParticle::A4BloodSingleSegment;
        locSourceSegmentFunc = &CParticle::A4BloodLocSourceSegmentFunc;

        locSources.SetSize(random.Get(4, 8));
        for (i = 0; i < locSources.GetSize(); i++)
            locSources[i].nPhase = 0;

        strcpy(pszTexName, "RainSnow");
        break;
    case PTID_CLAYRING:
        bUseAddEffect = TRUE;
        nMinTrys = 80;
        nLifeSpan = 15;
        nLifeOfPhase = 2;

        createFunc = &CParticle::ClayRingSingleCreate;
        segmentFunc = &CParticle::ClayRingSingleSegment;
        locSourceSegmentFunc = &CParticle::ClayRingLocSourceSegmentFunc;
        strcpy(pszTexName, "ClayRing"); //"FireSmoke" );
        locSources.SetSize(10);
        for (i = 0; i < locSources.GetSize(); i++)
        {
            float fpAngle = 2 * 3.1416 * i / 10;
            locSources[i].reserved = CPlot3D((float)sin(fpAngle), (float)cos(fpAngle), 0);
            // locSources[i].reserved *= 1 / 15.0f;
            locSources[i].vSpeed = locSources[i].respectCP = GetCP();
            locSources[i].nLifeTime = 14;
        }
        break;
    case PTID_TELEPORT:
        nMinTrys = 20;
        nParticlesMax = 320;
        nLifeSpan = 14;
        nLifeOfPhase = 1;

        createFunc = &CParticle::TeleportSingleCreate;
        segmentFunc = &CParticle::TeleportSingleSegment;
        locSourceSegmentFunc = &CParticle::TeleportLocSourceSegmentFunc;
        strcpy(pszTexName, "Teleport"); // Fireworks" );
        // strcpy( pszTexName, "Link" );
        locSources.SetSize(1);
        locSources[0].nLifeTime = 0;
        break;
    case PTID_MODIFIER1:
    case PTID_MODIFIER2:
    case PTID_MODIFIER3:
    case PTID_MODIFIER4:
    case PTID_MODIFIER5:
    case PTID_MODIFIER6:
    case PTID_MODIFIER7:
    case PTID_MODIFIER8:
    case PTID_MODIFIER9:
    case PTID_MODIFIER10:
    case PTID_MODIFIER11:
    case PTID_MODIFIER12:
        SetFlag(PS_NEED_CARRIER_TO_GENERATE);
        nMinTrys = 80;
        nLifeSpan = 14;
        nLifeOfPhase = 2;

        createFunc = &CParticle::ModifierSingleCreate;
        segmentFunc = &CParticle::ModifierSingleSegment;
        locSourceSegmentFunc = &CParticle::ModifierLocSourceSegmentFunc;
        strcpy(pszTexName, "Stun"); // FireSmoke" );
        break;
    case PTID_CASTINGILLUSION:
    case PTID_CASTINGENCHANTMENT:
    case PTID_CASTINGDOMINATION:
    case PTID_CASTINGHEALING:
        SetFlag(PS_NEED_CARRIER_TO_GENERATE);
        nMinTrys = 10; // 20;
        nLifeOfPhase = 1;
        nParticlesMax = 40 * 15;

        posRespectMinRnd = CPlot3D(-0.04f, -0.04f, -0.04f);
        posRespectMaxRnd = CPlot3D(0.04f, 0.04f, 0.04f);
        createFunc = &CParticle::CastingStarRoundSingleCreate;
        segmentFunc = &CParticle::CastingStarRoundSingleSegment;
        locSourceSegmentFunc = &CParticle::CastingStarRoundLocSourceSegmentFunc;
        {
            switch (GetTypeID())
            {
            case PTID_CASTINGILLUSION:
                nLifeSpan = 2;
                strcpy(pszTexName, "Casting2");
                break;
            case PTID_CASTINGENCHANTMENT:
                nLifeSpan = 2;
                strcpy(pszTexName, "Casting6");
                break;
            case PTID_CASTINGDOMINATION:
                nLifeSpan = 1;
                strcpy(pszTexName, "Casting3");
                break;
            case PTID_CASTINGHEALING:
                nLifeSpan = 1;
                strcpy(pszTexName, "Casting1");
                break;
            }
        }
        break;
    case PTID_CASTINGFAILED:
        SetFlag(PS_NEED_CARRIER_TO_GENERATE);
        nMinTrys = 25;
        nLifeSpan = 14;
        nLifeOfPhase = 1;

        createFunc = &CParticle::CastingFailedSingleCreate;
        segmentFunc = &CParticle::CastingFailedSingleSegment;
        locSourceSegmentFunc = &CParticle::CastingFailedLocSourceSegmentFunc;
        strcpy(pszTexName, "Teleport");
        break;
    case PTID_CASTINGFIRE:
    case PTID_CASTINGELECTRICITY:
    case PTID_CASTINGACID:
    case PTID_CASTINGDIVINATION:
        SetFlag(PS_NEED_CARRIER_TO_GENERATE);
        SetFlag(PS_BIND_TO_BODYPART);
        bindPlace = bpCast;

        createFunc = &CParticle::CastingGatherSingleCreate;
        segmentFunc = &CParticle::CastingGatherSingleSegment;
        locSourceSegmentFunc = &CParticle::CastingGatherLocSourceSegmentFunc;
        {
            switch (GetTypeID())
            {
            case PTID_CASTINGFIRE:
                nLifeSpan = 5;
                nMinTrys = 20;
                nLifeOfPhase = 5;
                strcpy(pszTexName, "FireSmoke");
                break;
            case PTID_CASTINGELECTRICITY:
                nLifeSpan = 5;
                nMinTrys = 20;
                nLifeOfPhase = 1;
                strcpy(pszTexName, "Casting8");
                break;
            case PTID_CASTINGACID:
                nLifeSpan = 5;
                nMinTrys = 20;
                nLifeOfPhase = 5;
                strcpy(pszTexName, "Casting7");
                break;
            case PTID_CASTINGDIVINATION:
                nLifeSpan = 5;
                nMinTrys = 6;
                nLifeOfPhase = 1;
                strcpy(pszTexName, "Casting4");
                bindPlace = bpAboveHead;
                bUseAddEffect = TRUE;
                break;
            }
        }
        break;
    case PTID_LIGHTNINGBLAST:
        nBornProbability = 88;
        nMinTrys = 250; // 100;
        nParticlesMax = 600;
        nLifeSpan = 15;
        nAnimPhases = 15;
        nLifeOfPhase = 1;
        posRespectMinRnd = CPlot3D(-0.2f, -0.2f, -0.2f);
        posRespectMaxRnd = CPlot3D(0.2f, 0.2f, 0.2f);
        fpSize = 1;
        // fpSpeedFactor = 1.0f; // Particles radial movement speed
        // fpSizeFactor = 0.8f;

        createFunc = &CParticle::LightningBlastSingleCreate;
        segmentFunc = &CParticle::LightningBlastSingleSegment;
        locSourceSegmentFunc = &CParticle::LightningBlastLocSourceSegmentFunc;
        strcpy(pszTexName, "TendriLight");
        bUseAddEffect = TRUE;
        break;
    case PTID_ZONEEXIT:
        nMinTrys = 10;
        nParticlesMax = 40 * 15;
        fpPower = 5;
        fpPower2 = 5;
        SetFlag(PS_PAUSE_INVISIBLE);

        createFunc = &CParticle::ZoneExitSingleCreate;
        segmentFunc = &CParticle::ZoneExitSingleSegment;
        locSourceSegmentFunc = &CParticle::ZoneExitLocSourceSegmentFunc;
        strcpy(pszTexName, "ZoneExit");
        break;
    case PTID_PATH:
        SetFlag(PS_UNPAUSABLE);
        nMinTrys = 2500;
        nParticlesMax = 2500;
        // fpPower = 5;
        // fpPower2 = 5;

        createFunc = &CParticle::PathSingleCreate;
        segmentFunc = &CParticle::PathSingleSegment;
        locSourceSegmentFunc = &CParticle::PathLocSourceSegmentFunc;
        strcpy(pszTexName, "ShapeChange");
        break;
    case PTID_PATHFAILED:
    case PTID_PATHDESTINATION: {
        SetFlag(PS_UNPAUSABLE);
        // fpPower = 5;
        // fpPower2 = 5;
        locSources.SetSize(1);
        locSources[0].nLifeTime = 0;
        switch (GetTypeID())
        {
        case PTID_PATHFAILED:
            nMinTrys = N_PATHDEST_ROUND_PARTS + 4 * N_PATHDEST_CROSS_PARTS;
            locSources[0].nPhase = RGBA_MAKE(0xFF, 0x40, 0x40, 0);
            break;
        case PTID_PATHDESTINATION:
            nMinTrys = N_PATHDEST_ROUND_PARTS;
            locSources[0].nPhase = RGBA_MAKE(0x40, 0xFF, 0x40, 0);
            break;
        }
        nParticlesMax = nMinTrys;

        createFunc = &CParticle::PathDestSingleCreate;
        segmentFunc = &CParticle::PathDestSingleSegment;
        locSourceSegmentFunc = &CParticle::PathDestLocSourceSegmentFunc;
        strcpy(pszTexName, "ShapeChange");
    }
    break;
    case PTID_MOSHKA:
        SetFlag(PS_NEED_CARRIER_TO_GENERATE);
        nMinTrys = 100; // 71;//20;
        // nLifeOfPhase = 1;
        nParticlesMax = 800; // 40 * 15;

        // posRespectMinRnd = CPlot3D( -0.04f, -0.04f, -0.04f );
        // posRespectMaxRnd = CPlot3D( 0.04f, 0.04f, 0.04f );
        createFunc = &CParticle::MoshkaSingleCreate;
        segmentFunc = &CParticle::MoshkaSingleSegment;
        locSourceSegmentFunc = &CParticle::MoshkaLocSourceSegmentFunc;
        locSources.SetSize(N_MOSHKA_SOURCES);
        for (i = 0; i < N_MOSHKA_SOURCES; i++)
            locSources[i].nLifeTime = 0;
        strcpy(pszTexName, "Bag");
        break;
    case PTID_PORTAL:
        nMinTrys = 50;
        nParticlesMax = 800;
        nLifeSpan = 10;
        nLifeOfPhase = 3;
        createFunc = &CParticle::PortalSingleCreate;
        segmentFunc = &CParticle::PortalSingleSegment;
        locSourceSegmentFunc = &CParticle::PortalLocSourceSegmentFunc;
        strcpy(pszTexName, "Teleport");
        locSources.SetSize(1);
        bUseAddEffect = TRUE;
        break;
    case PTID_PORTALSTAR:
        nMinTrys = 1;
        nParticlesMax = 1;
        createFunc = &CParticle::PortalStarSingleCreate;
        segmentFunc = &CParticle::PortalStarSingleSegment;
        locSourceSegmentFunc = &CParticle::PortalStarLocSourceSegmentFunc;
        strcpy(pszTexName, "Casting6");
        // bUseAddEffect = TRUE;
        break;
    case PTID_CYLINDER1:
    case PTID_CYLINDER2:
        SetFlag(PS_NEED_CARRIER_TO_GENERATE);
        nMinTrys = 30; // 20;
        nLifeOfPhase = 1;
        nParticlesMax = 40 * 15;

        // posRespectMinRnd = CPlot3D( -0.04f, -0.04f, -0.04f );
        // posRespectMaxRnd = CPlot3D( 0.04f, 0.04f, 0.04f );
        createFunc = &CParticle::CylinderSingleCreate;
        segmentFunc = &CParticle::CylinderSingleSegment;
        locSourceSegmentFunc = &CParticle::CylinderLocSourceSegmentFunc;
        switch (GetTypeID())
        {
        case PTID_CYLINDER1:
            strcpy(pszTexName, "Casting2");
            break;
        case PTID_CYLINDER2:
            strcpy(pszTexName, "Casting1");
            break;
        }
        locSources.SetSize(2);
        for (i = 0; i < locSources.GetSize(); i++)
            locSources[i].nLifeTime = 100;
        break;
    case PTID_FIRESTAR:
    case PTID_ACIDSTAR:
        SetFlag(PS_NEED_CARRIER_TO_GENERATE);
        SetFlag(PS_BIND_TO_BODYPART);
        bindPlace = bpBody;
        nMinTrys = 1;

        createFunc = &CParticle::StarSingleCreate;
        segmentFunc = &CParticle::StarSingleSegment;
        locSourceSegmentFunc = &CParticle::StarLocSourceSegmentFunc;
        switch (GetTypeID())
        {
        case PTID_FIRESTAR:
            strcpy(pszTexName, "FireArrowSmoke");
            nLifeSpan = 15;
            break;
        case PTID_ACIDSTAR:
            strcpy(pszTexName, "AcidRay");
            nLifeSpan = 15;
            break;
        }
        break;
    case PTID_SPARKS:
        bUseAddEffect = TRUE;
        SetFlag(PS_NEED_CARRIER_TO_GENERATE);
        SetFlag(PS_BIND_TO_BODYPART);
        bindPlace = bpBody;
        nMinTrys = 10;
        nParticlesMax = 300;
        createFunc = &CParticle::SparksSingleCreate;
        segmentFunc = &CParticle::SparksSingleSegment;
        locSourceSegmentFunc = &CParticle::SparksLocSourceSegmentFunc;
        strcpy(pszTexName, "Casting6");
        break;
    case PTID_VISIONSTAR1:
    case PTID_VISIONSTAR2:
    case PTID_VISIONSTAR3:
    case PTID_VISIONSTAR4: {
        bUseAddEffect = TRUE;
        SetFlag(PS_NEED_CARRIER_TO_GENERATE);
        SetFlag(PS_BIND_TO_BODYPART);
        bindPlace = bpAboveHead;
        nMinTrys = 3;
        createFunc = &CParticle::VStarSingleCreate;
        segmentFunc = &CParticle::VStarSingleSegment;
        locSourceSegmentFunc = &CParticle::VStarLocSourceSegmentFunc;
        switch (GetTypeID())
        {
        case PTID_VISIONSTAR1:
            nLifeSpan = 0;
            break;
        case PTID_VISIONSTAR2:
            nLifeSpan = 1;
            break;
        case PTID_VISIONSTAR3:
            nLifeSpan = 2;
            break;
        case PTID_VISIONSTAR4:
            nLifeSpan = 3;
            break;
        }
        strcpy(pszTexName, "Stun");
        break;
    }
    case PTID_REGENERATION:
        bUseAddEffect = TRUE;
        SetFlag(PS_NEED_CARRIER_TO_GENERATE);
        SetFlag(PS_BIND_TO_BODYPART);
        bindPlace = bpBody;
        nMinTrys = 2;
        createFunc = &CParticle::RegenerationSingleCreate;
        segmentFunc = &CParticle::RegenerationSingleSegment;
        locSourceSegmentFunc = &CParticle::RegenerationLocSourceSegmentFunc;
        strcpy(pszTexName, "Stun");
        break;
    case PTID_SILENCE:
        bUseAddEffect = TRUE;
        SetFlag(PS_NEED_CARRIER_TO_GENERATE);
        SetFlag(PS_BIND_TO_BODYPART);
        bindPlace = bpBody;
        nMinTrys = 4;
        nParticlesMax = 300;
        createFunc = &CParticle::SilenceSingleCreate;
        segmentFunc = &CParticle::SilenceSingleSegment;
        locSourceSegmentFunc = &CParticle::SilenceLocSourceSegmentFunc;
        strcpy(pszTexName, "Stun");
        break;
    case PTID_FEEBLEMIND:
        bUseAddEffect = TRUE;
        SetFlag(PS_NEED_CARRIER_TO_GENERATE);
        SetFlag(PS_BIND_TO_BODYPART);
        bindPlace = bpHead;
        nMinTrys = 5;
        nParticlesMax = 300;
        createFunc = &CParticle::FeebleMindSingleCreate;
        segmentFunc = &CParticle::FeebleMindSingleSegment;
        locSourceSegmentFunc = &CParticle::FeebleMindLocSourceSegmentFunc;
        strcpy(pszTexName, "Link");
        break;
    case PTID_FEETCLOUD1:
    case PTID_FEETCLOUD2: {
        bUseAddEffect = TRUE;
        SetFlag(PS_NEED_CARRIER_TO_GENERATE);
        nMinTrys = 8;
        nParticlesMax = 300;
        locSources.SetSize(2);
        for (int i = 0; i < 2; i++)
        {
            locSources[i].nPhase = -1;
            locSources[i].nLifeTime = i;
            locSources[i].respectCP = CPlot3D(0, 0, 0);
            locSources[i].reserved.x = 0;
        }
        createFunc = &CParticle::FeetCloudSingleCreate;
        segmentFunc = &CParticle::FeetCloudSingleSegment;
        locSourceSegmentFunc = &CParticle::FeetCloudLocSourceSegmentFunc;
        strcpy(pszTexName, "VisionFog");
        switch (GetTypeID())
        {
        case PTID_FEETCLOUD1:
            nLifeSpan = RGBA_MAKE(0, 0, 0xFF, 0);
            break;
        case PTID_FEETCLOUD2:
            nLifeSpan = RGBA_MAKE(0xFF, 0, 0, 0);
            break;
        }
    }
    break;
    case PTID_BALLOFSTARS:
        bUseAddEffect = TRUE;
        SetFlag(PS_NEED_CARRIER_TO_GENERATE);
        SetFlag(PS_BIND_TO_BODYPART);
        bindPlace = bpCast;
        nMinTrys = 1;
        nParticlesMax = 300;
        createFunc = &CParticle::BallStarsSingleCreate;
        segmentFunc = &CParticle::BallStarsSingleSegment;
        locSourceSegmentFunc = &CParticle::BallStarsLocSourceSegmentFunc;
        strcpy(pszTexName, "Stun");
        break;
    case PTID_CURSESTARS:
        bUseAddEffect = TRUE;
        SetFlag(PS_NEED_CARRIER_TO_GENERATE);
        SetFlag(PS_BIND_TO_BODYPART);
        bindPlace = bpBody;
        nMinTrys = 8;
        nParticlesMax = 300;
        createFunc = &CParticle::CurseStarsSingleCreate;
        segmentFunc = &CParticle::CurseStarsSingleSegment;
        locSourceSegmentFunc = &CParticle::CurseStarsLocSourceSegmentFunc;
        strcpy(pszTexName, "Healing");
        break;
    case PTID_STARTTRANS:
        SetFlag(PS_NEED_CARRIER_TO_GENERATE);
        SetFlag(PS_BIND_TO_BODYPART);
        bindPlace = bpBody;
        nMinTrys = 1;
        nParticlesMax = 300;
        createFunc = &CParticle::StartTransSingleCreate;
        segmentFunc = &CParticle::StartTransSingleSegment;
        locSourceSegmentFunc = &CParticle::StartTransLocSourceSegmentFunc;
        locSources.SetSize(1);
        locSources[0].nLifeTime = 30;
        locSources[0].reserved.x = 0;
        locSources[0].reserved.y = 1;
        strcpy(pszTexName, "ZoneExit");
        break;
    default:
    {
        const int typeID = static_cast<int>(GetTypeID());
        GameJournal::Event("ERROR", "unknown_particle_type", "type_id=%d", typeID);
        // CreateData keeps the object even when Create reports failure. Leave it
        // inert so callers that attach/segment it can safely continue.
        ResetFlag(PS_GENERATE_PARTICLES);
        nMinTrys = 0;
        locSources.SetSize(0);
        return ReportRecoverableError("CParticle::Create().\nUnknown ETypeID %d.\n", typeID);
    }
    }
    // Load texture
    pTexture.SetDesc(TTID_SPELL_MAIN, pszTexName);
    // Generate UV coords
    float fpAnimSize = 1.f / float(nAnimPerTexSize);
    float fpUVMargin = 0.5f / N_TEXTURE_SIZE;
    uvOfPhases.SetSize(nAnimPhases);
    SParticleUV *pUV = uvOfPhases.GetData();
    for (i = 0; i < nAnimPhases; i++, pUV++)
    {
        float nPhX = float(i % nAnimPerTexSize);
        float nPhY = float(i / nAnimPerTexSize);
        pUV->fpU0 = nPhX * fpAnimSize + fpUVMargin;
        pUV->fpV0 = 1 - (nPhY * fpAnimSize + fpUVMargin);
        pUV->fpU1 = (nPhX + 1) * fpAnimSize - fpUVMargin;
        pUV->fpV1 = 1 - ((nPhY + 1) * fpAnimSize - fpUVMargin);
    }
    return TRUE;
}

//------------------------------------ Mushroom -----------------------------------------------
// const float F_MUSHROOM_TOR_SPEED = 0.23f;
// const float F_MUSHROOM_HOR_SPEED  = 0.4f;
// const float F_MUSHROOM_VERT_SPEED = 0.2f;

BOOL CParticle::MushroomSingleCreate(SSingleParticle *pSPart, int nTry)
{
    /*	if ( locSources.GetSize() == 0 )
            {
                    locSources.SetSize(1);
                    locSources[0].nLifeTime = 0;			 // Remaining function time
                    locSources[0].respectCP.x = 1 * fpSize;  // радиус тора, определяющего движение
       частиц locSources[0].respectCP.z = 2 * fpSize;  // высота тора, определяющего движение частиц
                    locSources[0].reserved = carrierCP;
            }
            float fpRadius, fpAngle;
            fpRadius = random.GetFloat( 0.1f * fpSize, 1 * fpSize );//0.2f + locSources[0].nLifeTime
       * 0.2f ); fpAngle = random.GetFloat( 0, float(2*pi) ); pSPart->vSpeed.x = (float) sin(
       fpAngle ); pSPart->vSpeed.y = (float) cos( fpAngle ); pSPart->coord.x = carrierCP.x + float(
       fpRadius * pSPart->vSpeed.x ); pSPart->coord.y = carrierCP.y + float( fpRadius *
       pSPart->vSpeed.y ); pSPart->coord.z = carrierCP.z + random.GetFloat( 0, fpSize * 1.0f );
            pSPart->coord.w = fpSize * random.GetFloat( 0.2f, 0.6f );
            pSPart->nLifeTime = ( 35 - locSources[0].nLifeTime );
            pSPart->nPhase = 0;
            pSPart->nSpecial = random.Get( 0, 5 );
            pSPart->dwColor = RGBA_MAKE( 0xFF, 0xFF, 0xFF, 0xFF ); //dwAlpha );
            return TRUE;*/
    if (locSources.GetSize() == 0)
    {
        locSources.SetSize(1);
        locSources[0].nLifeTime = 0; // Remaining function time
        locSources[0].respectCP.x = float(0.2f * log(1)) * fpSize;
        locSources[0].respectCP.z = 0;
        locSources[0].reserved = carrierCP;
    }
    float fpRadius, vx, vy, vz, fpLen;
    fpRadius = locSources[0].respectCP.x * random.GetFloat(0.8f, 1.2f);
    vx = random.GetFloat(-1, 1);
    vy = random.GetFloat(-1, 1);
    vz = random.GetFloat(0.1f, 1);
    fpLen = float(1 / sqrt(vx * vx + vy * vy + vz * vz));
    // fpRadius = random.GetFloat( fpRadius * fpSize, 1 * fpSize );//0.2f + locSources[0].nLifeTime
    // * 0.2f ); fpAngle = random.GetFloat( 0, float(2*pi) );
    //	pSPart->vSpeed.x = (float) sin( fpAngle );
    //	pSPart->vSpeed.y = (float) cos( fpAngle );
    pSPart->vSpeed.x = vx * fpLen;
    pSPart->vSpeed.y = vy * fpLen;
    pSPart->vSpeed.z = vz * fpLen + 0.12f;
    pSPart->coord.x = carrierCP.x + float(fpRadius * pSPart->vSpeed.x);
    pSPart->coord.y = carrierCP.y + float(fpRadius * pSPart->vSpeed.y);
    pSPart->coord.z = carrierCP.z + float(fpRadius * pSPart->vSpeed.z);
    ; // + random.GetFloat( 0, fpSize * 1.0f );
    pSPart->coord.z += locSources[0].respectCP.z;
    pSPart->coord.w = 0.10f +
                      (locSources[0].nLifeTime) * 0.008f; // fpSize * random.GetFloat( 0.2f, 0.6f );
    pSPart->vSpeed.x *= 0.06f * fpSize;
    pSPart->vSpeed.y *= 0.06f * fpSize;
    pSPart->vSpeed.z *= 0.06f * fpSize;
    pSPart->coord.w *= fpSize;
    pSPart->nLifeTime = 16 + random.Get(0, 3);
    pSPart->nPhase = Max(0, (int)(locSources[0].nLifeTime / 10 + random.Get(-1, 1)));
    pSPart->nPhaseLifeTime = 2;
    pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0x40);
    return TRUE;
}

BOOL CParticle::MushroomSingleSegment(SSingleParticle *pSPart)
{
    DWORD dwAlpha;
    float fx, fy, fz;
    SXYZW &point = pSPart->coord;
    if (--(pSPart->nLifeTime) == 0)
    {
        return FALSE;
    }
    if (pSPart->nPhase < 14)
    {
        if (--pSPart->nPhaseLifeTime == 0)
        {
            pSPart->nPhase++;
            pSPart->nPhaseLifeTime = 1;
        }
        if (pSPart->nPhase < 8)
            pSPart->coord.w *= 1.1f;
    }

    if (pSPart->nLifeTime > 4)
    {
        dwAlpha = Min((DWORD)0xFF, (DWORD)RGBA_GETALPHA(pSPart->dwColor) + 0x40);
        pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, dwAlpha);
    }
    else
    {
        dwAlpha = Max((DWORD)0, (DWORD)RGBA_GETALPHA(pSPart->dwColor) - 0x40);
        pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, dwAlpha);
    }

    point.x += pSPart->vSpeed.x;
    point.y += pSPart->vSpeed.y;
    point.z += pSPart->vSpeed.z;
    pSPart->vSpeed.x *= 0.7f;
    pSPart->vSpeed.y *= 0.7f;
    pSPart->vSpeed.z *= 0.7f;
    // ускорение частиц к вершине гриба в зависимости от их высоты и расстояния до оси гриба
    float fpCenterZ = locSources[0].respectCP.z, fpRadius;
    fx = point.x - locSources[0].reserved.x;
    fy = point.y - locSources[0].reserved.y;
    fz = point.z - locSources[0].reserved.z;
    fpRadius = float(sqrt(fx * fx + fy * fy));
    if (fz < fpCenterZ)
    {
        // pSPart->vSpeed.z += 0.2f * ( fpCenterZ - fz ) / ( 1 + fpRadius );
        pSPart->vSpeed.x -= fx * fpSize * 0.02f / fpRadius;
        pSPart->vSpeed.y -= fy * fpSize * 0.02f / fpRadius;
    }
    return TRUE;

    /*	DWORD dwAlpha;
            float fpRadius, x, z, mn, xx, zz;
            CPlot3D vSpeed;
            SXYZW point = pSPart->coord;
            CPlot3D &torcenter = locSources[0].respectCP;
            CPlot3D &oldCarrier = locSources[0].reserved;

            point.x -= oldCarrier.x;
            point.y -= oldCarrier.y;
            point.z -= oldCarrier.z;

            if ( --( pSPart->nLifeTime ) == 0 )
            {
                    return FALSE;
            }
            if ( pSPart->nLifeTime < 8 )
            {
                    dwAlpha = RGBA_GETALPHA( pSPart->dwColor ) - 0x20;
                    pSPart->dwColor = RGBA_MAKE( 0xFF, 0xFF, 0xFF, dwAlpha );
            }
    //	pSPart->nPhase = max( 0, 8 - ( pSPart->nLifeTime + pSPart->nSpecial ) / 4 );
            if ( pSPart->nPhase < 15 && random.Get( 0, 16 ) < 10 ) pSPart->nPhase++;

            fpRadius = (float) sqrt( point.x*point.x + point.y*point.y );
    // составляющая скорости за счет тора
            xx = point.z - torcenter.z;
            zz = torcenter.x - fpRadius;
            x = xx + zz * 0.2f;  // для того, чтобы точки стремились к центру тора
            z = zz - xx * 0.2f;
        mn = float( F_MUSHROOM_TOR_SPEED / sqrt( 0.001 + x*x + z*z ) );
            x *= mn;
            z *= mn;
    // ускорение частиц к вершине гриба в зависимости от их высоты и расстояния до оси гриба
            z += F_MUSHROOM_VERT_SPEED * ( ( torcenter.z+torcenter.x ) - point.z ) / ( 1 + fpRadius
    );
    // ускорение частиц в горизонтальной плоскости к оси гриба
            x += F_MUSHROOM_HOR_SPEED  * ( -fpRadius ) / ( 1 + fabs(point.z) );
    // keep particles from mushroom axis
            if ( point.z < torcenter.z )
                    x *= fpRadius / ( fpRadius + torcenter.x );
            else
                    x *= fpRadius / ( fpRadius + 0.5f );
    // special check for convergence
            if ( fpRadius + x < 0 )
                    x = -fpRadius * 0.6f;

            pSPart->coord.w = ( fpRadius + 1 ) * 0.3f;
    //	if ( pSPart->nLifeTime < 15 )
    //	{
    //		pSPart->coord.w *= exp ( log( 1.08f ) * ( 15 - pSPart->nLifeTime ) );
    //	}
    // подвинем точку
            pSPart->coord.x += pSPart->vSpeed.x * x;
            pSPart->coord.y += pSPart->vSpeed.y * x;
            pSPart->coord.z += z;//vSpeed.z;
            return TRUE;*/
}

void CParticle::MushroomLocSourceSegmentFunc(SLocalSource *pLSrc)
{
#ifndef _A4
    if (GetTypeID() == PTID_MUSHROOM)
    {
        if (pLSrc->nLifeTime == 0 && pLSrc == &locSources[0])
            theCamera.BeginEarthquake(carrierCP, 5, fpSize);
    }
#endif
    /*	pLSrc->nLifeTime++;
    //	if ( nMinTrys > 22 ) nMinTrys /= 2; //-= 4;
            if ( pLSrc->nLifeTime == 10 )
                    AttachCarrier( NULL );
            pLSrc->respectCP.z += 0.06f;
    //	pLSrc->respectCP.x *= 1.03f;*/
    pLSrc->nLifeTime++;
    pLSrc->respectCP.x = float(
        0.2 * fpSize * log(pLSrc->nLifeTime + 1)); // 2 - ( 2 - pLSrc->respectCP.x ) * 0.95f;
    pLSrc->respectCP.z += 0.12f * fpSize;
    if (pLSrc->nLifeTime == 30)
        ResetFlag(PS_GENERATE_PARTICLES); // nGenerateParticles = 0;
}

//-------------------------------------- Nuke -------------------------------------------------
const float F_NUKE_AIR_COEF = 0.85f;
const float F_NUKE_G_FORCE = 1.0f * DW_DEFAULT_SEGMENT_TIME / 1000;
const float F_NUKE_ALPHA_GROW = 4.f;

BOOL CParticle::NukeSingleCreate(SSingleParticle *pSPart, int nTry)
{
    DWORD dwAlpha;
    int nGenPhasesLeft = 11 - locSources[0].nLifeTime;
    float fpRadius, fpAngle;
    fpRadius = random.GetFloat(0, fpSize * 0.2f);
    fpAngle = random.GetFloat(0, float(2 * pi));
    pSPart->coord.x = carrierCP.x + float(fpRadius * sin(fpAngle));
    pSPart->coord.y = carrierCP.y + float(fpRadius * cos(fpAngle));
    pSPart->coord.z = carrierCP.z + random.GetFloat(0, fpSize * 2);
    pSPart->coord.w = 0.2f * fpSize;
    pSPart->vSpeed.x = float(sin(fpAngle));
    pSPart->vSpeed.y = float(cos(fpAngle));
    pSPart->vSpeed.z = fpSize * 0.1f *
                       nGenPhasesLeft; // random.GetFloat( fpSize*0.1f*nGenPhasesLeft,
                                       // fpSize*0.15f*nGenPhasesLeft );
    pSPart->nPhase = locSources[0].nLifeTime / 3;
    //	pSPart->nPhase = 0;
    pSPart->nLifeTime = 30 + nGenPhasesLeft;
    dwAlpha = (DWORD)(0x4 * exp(locSources[0].nLifeTime *
                                log(F_NUKE_ALPHA_GROW))); // 0x0 + locSources[0].nLifeTime * 0x30;
    if (dwAlpha > 0xFF)
        dwAlpha = 0xFF;
    pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, dwAlpha);
    pSPart->nSpecial = 0;
    return TRUE;
}

BOOL CParticle::NukeSingleSegment(SSingleParticle *pSPart)
{
    float fpSpeed;
    DWORD dwNewAlpha;
    if (--(pSPart->nLifeTime) == 0)
    {
        return FALSE;
    }
    if (pSPart->nSpecial)
    {
        if (pSPart->nPhase < 14)
            pSPart->nPhase++;
        else
            return FALSE;
        pSPart->coord.x = pSPart->coord.x + pSPart->vSpeed.x;
        pSPart->coord.y = pSPart->coord.y + pSPart->vSpeed.y;
        pSPart->vSpeed.x *= F_NUKE_AIR_COEF;
        pSPart->vSpeed.y *= F_NUKE_AIR_COEF;
        pSPart->coord.w *= 1.13f;
        dwNewAlpha = (DWORD)(RGBA_GETALPHA(pSPart->dwColor) -
                             0x10); //+ 0x30;// / N_CASTING_FADE_LIFETIME;
        if (dwNewAlpha > 0xff)
            dwNewAlpha = 0;
        pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, dwNewAlpha);
    }
    else
    {
        pSPart->vSpeed.z = pSPart->vSpeed.z * F_NUKE_AIR_COEF - F_NUKE_G_FORCE;
        pSPart->coord.z = pSPart->coord.z + pSPart->vSpeed.z;
        dwNewAlpha = (DWORD)(RGBA_GETALPHA(pSPart->dwColor) *
                             F_NUKE_ALPHA_GROW); //+ 0x30;// / N_CASTING_FADE_LIFETIME;
        if (dwNewAlpha > 0xff)
            dwNewAlpha = 0xff;
        pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, dwNewAlpha);
        if (pSPart->vSpeed.z < 0 && pSPart->coord.z < carrierCP.z + 0.5 * fpSize)
        {
            fpSpeed = fabs(pSPart->vSpeed.z);
            pSPart->vSpeed.x *= fpSpeed;
            pSPart->vSpeed.y *= fpSpeed;
            pSPart->nSpecial = 1;
        }
    }
    return TRUE;
}

void CParticle::NukeLocSourceSegmentFunc(SLocalSource *pLSrc)
{
    pLSrc->nLifeTime++;
    if (nMinTrys > 8)
        nMinTrys /= 2;
    if (pLSrc->nLifeTime == 13)
        ResetFlag(PS_GENERATE_PARTICLES); // nGenerateParticles = 0;
}

// ------------------------------- Spell casting ----------------------------------------------
const int N_CASTING_FADE_LIFETIME = 8;
const int N_CASTING_SOURCE_LIFETIME = 36;
const int N_CASTING_RING_LIFETIME = 9;
const int N_CASTING_PT_PER_TAIL = 9;
const int N_CASTING_RING_SIZE = 20;
const float F_CASTING_RING_KOEF = 0.5f; // влияет на скорость с которой кольцо расходится из центра
BOOL CParticle::CastingSingleCreate(SSingleParticle *pSPart, int nTry)
{
    int nBaseLifeTime;
    float fpRadius, fpHeight, fpAngle, fpRndBase, fpWishRadius, fpNextRadius;
    // Generate local sources at first time
    if (locSources.GetSize() == 0)
    {
        if (pCarrier == NULL)
        {
            ReportWarning("Particle effect Casting without carrier\n");
            return FALSE;
        }
        nMinTrys = 3; // Float2Int( fpSize * 6.f + 2.f );
        locSources.SetSize(nMinTrys);
        float fpAngleInc = ToRadian(360.f / float(nMinTrys));
        CPlot3D ptStart;
        GetObjectBV(ptStart, fpRadius);
        fpHeight = fpRadius;
        ptStart.z -= fpRadius;
        fpRadius *= 1.1f;
        fpHeight *= 2;
        fpSpeedFactor = (fpRadius / float(N_CASTING_SOURCE_LIFETIME)) * 2.f;
        fpSizeFactor =
            0; // float( ( 0.5f*pi*fpRadius/nMinTrys ) / float( N_CASTING_SOURCE_LIFETIME ) );

        float fpAngle = 0;
        // ptStart -= carrierCP;
        for (int i = 0; i < nMinTrys; i++, fpAngle += fpAngleInc)
        {
            locSources[i].nPhase = 0;
            locSources[i].nLifeTime = N_CASTING_SOURCE_LIFETIME +
                                      N_CASTING_RING_LIFETIME; // * 3 / 2;
            float fpZRnd = random.GetFloat(-fpHeight / 10, fpHeight / 10);
            float fpZInc = (fpHeight - fpZRnd) / float(N_CASTING_SOURCE_LIFETIME);

            locSources[i].reserved = ptStart;
            locSources[i].reserved.z += fpZRnd;

            locSources[i].respectCP = locSources[i].reserved;

            locSources[i].vSpeed.x = 0.0f;
            locSources[i].vSpeed.y = fpAngle;
            locSources[i].vSpeed.z = fpZInc;
        }
        dwPFlags = 0;
        nMinTrys *= N_CASTING_PT_PER_TAIL;
        // nMinTrys = 1;
    }
    if (int(dwPFlags) <= locSources.GetSize())
    {
        if (int(dwPFlags) == locSources.GetSize())
        {
            dwPFlags++;
            return FALSE;
        }
        pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0xFF);
        pSPart->nPhase = 0;
        pSPart->nLifeTime = N_CASTING_SOURCE_LIFETIME; // * 3 / 2;
        pSPart->nPhaseLifeTime = 1;
        pSPart->nSpecial = int(dwPFlags);
        pSPart->coord.x = locSources[dwPFlags].respectCP.x + carrierCP.x;
        pSPart->coord.y = locSources[dwPFlags].respectCP.y + carrierCP.y;
        pSPart->coord.z = locSources[dwPFlags].respectCP.z + carrierCP.z;
        pSPart->coord.w = (fpSize) * 0.0015f; // 025f;
        dwPFlags++;
        return TRUE;
    }

    // Last phase of effect
    if (locSources[0].nLifeTime < N_CASTING_RING_LIFETIME)
    {
        nMinTrys = 8 * N_CASTING_PT_PER_TAIL; // change number of particles for last phase

        fpRadius = locSources[0].vSpeed.x;
        fpAngle = random.GetFloat(0, float(2 * pi));
        pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0xFF);
        pSPart->nPhase = 1;
        nBaseLifeTime = Min(locSources[0].nLifeTime, 5);
        pSPart->nLifeTime = random.Get(nBaseLifeTime + 1, nBaseLifeTime + 4);
        pSPart->nPhaseLifeTime = 1;
        pSPart->nSpecial = -1;
        fpWishRadius = fpSpeedFactor * N_CASTING_RING_SIZE;
        fpNextRadius = fpRadius + (fpRadius - fpWishRadius) * F_CASTING_RING_KOEF;
        fpRadius = random.GetFloat(fpNextRadius, fpRadius);
        // fpRndBase = locSources[0].vSpeed.x * 0.10f;
        pSPart->coord.x = float(locSources[0].respectCP.x + carrierCP.x +
                                fpRadius *
                                    sin(fpAngle)); // + random.GetFloat( -fpRndBase, fpRndBase );
        pSPart->coord.y = float(locSources[0].respectCP.y + carrierCP.y +
                                fpRadius *
                                    cos(fpAngle)); // + random.GetFloat( -fpRndBase, fpRndBase );
        pSPart->coord.z = locSources[0].respectCP.z +
                          carrierCP.z; // + random.GetFloat( -fpRndBase, 0.f );
        pSPart->coord.w = (fpSize) * 0.1f;

        return TRUE;
    }
    int nLocSrcIndex = random.Get(0, locSources.GetSize() - 1); // nTry / N_CASTING_PT_PER_TAIL;
    ///	int nInnerIndex = nTry % N_CASTING_PT_PER_TAIL;

    pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0xFF);
    pSPart->nPhase = 1;
    pSPart->nLifeTime = 7;
    pSPart->nPhaseLifeTime = 1;
    pSPart->nSpecial = -1;
    fpRndBase = locSources[nLocSrcIndex].vSpeed.x * 0.10f + 1e-3f;
    pSPart->coord.x = locSources[nLocSrcIndex].respectCP.x + carrierCP.x +
                      random.GetFloat(-fpRndBase, fpRndBase);
    pSPart->coord.y = locSources[nLocSrcIndex].respectCP.y + carrierCP.y +
                      random.GetFloat(-fpRndBase, fpRndBase);
    pSPart->coord.z = locSources[nLocSrcIndex].respectCP.z + carrierCP.z +
                      random.GetFloat(-fpRndBase, 0.f);
    pSPart->coord.w = (fpSize) * 0.1f;

    return TRUE;
}

BOOL CParticle::CastingSingleSegment(SSingleParticle *pSPart)
{
    if (--(pSPart->nLifeTime) == 0)
    {
        if (pSPart->nSpecial >= 0 && pSPart->nSpecial != 0xFFFF)
        {
            pSPart->nSpecial = 0xFFFF;
            pSPart->nLifeTime = N_CASTING_FADE_LIFETIME;
            return TRUE;
        }
        return FALSE;
    }
    if (pSPart->nSpecial == 0xFFFF)
    {
        pSPart->coord.w /= 1.3f;
        DWORD dwNewAlpha = RGBA_GETALPHA(pSPart->dwColor) - 0xFF / N_CASTING_FADE_LIFETIME;
        pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, dwNewAlpha);
        return TRUE;
    }
    if (pSPart->nSpecial >= 0)
    {
        //		if ( ++( pSPart->nPhase ) >= 8 )
        //			pSPart->nPhase = 0;
        pSPart->nPhaseLifeTime = 1;
        if (pSPart->nLifeTime >= N_CASTING_SOURCE_LIFETIME)
        {
            pSPart->coord.w += fpSizeFactor;
        }
        else
        {
            if (pSPart->nLifeTime >= N_CASTING_SOURCE_LIFETIME / 2)
                pSPart->coord.w -= fpSizeFactor;
            else
                pSPart->coord.w += fpSizeFactor;
        }
        SLocalSource *pLSrc = &locSources[pSPart->nSpecial];
        pSPart->coord.x = pLSrc->respectCP.x + carrierCP.x;
        pSPart->coord.y = pLSrc->respectCP.y + carrierCP.y;
        pSPart->coord.z = pLSrc->respectCP.z + carrierCP.z;
    }
    else
        pSPart->nPhase++;
    return TRUE;
}

void CParticle::CastingLocSourceSegmentFunc(SLocalSource *pLSrc)
{
    float fpWishRadius;
    if (--pLSrc->nLifeTime == 0)
    {
        ResetFlag(PS_GENERATE_PARTICLES); // nGenerateParticles = 0;
        return;
    }
    if (pLSrc->nLifeTime >= N_CASTING_SOURCE_LIFETIME / 2 + N_CASTING_RING_LIFETIME)
    {
        pLSrc->vSpeed.x += fpSpeedFactor;
    }
    else
    {
        if (pLSrc->nLifeTime >= N_CASTING_RING_LIFETIME)
            pLSrc->vSpeed.x -= fpSpeedFactor;
        else
        {
            fpWishRadius = fpSpeedFactor * N_CASTING_RING_SIZE;
            pLSrc->vSpeed.x = fpWishRadius + (pLSrc->vSpeed.x - fpWishRadius) * F_CASTING_RING_KOEF;
        }
    }
    if (pLSrc->nLifeTime >= N_CASTING_RING_LIFETIME)
    {
        pLSrc->vSpeed.y += ToRadian(10.f);

        pLSrc->respectCP.x = pLSrc->reserved.x + pLSrc->vSpeed.x * float(sin(pLSrc->vSpeed.y));
        pLSrc->respectCP.y = pLSrc->reserved.y + pLSrc->vSpeed.x * float(cos(pLSrc->vSpeed.y));
        pLSrc->respectCP.z += pLSrc->vSpeed.z;
    }
}

//-------------------------------------- Banshee casting
//-------------------------------------------------

const int N_BANSHEECASTING_PHASE_LIFETIME = 1;
const int N_BANSHEECASTING_RING_LIFETIME = 8 * N_BANSHEECASTING_PHASE_LIFETIME;
const int N_BANSHEECASTING_RING_SIZE = 30;
const float F_BANSHEECASTING_RING_KOEF = 0.5f;

BOOL CParticle::BansheeCastingSingleCreate(SSingleParticle *pSPart, int nTry)
{
    int nBaseLifeTime;
    float fpRadius, fpHeight, fpAngle, fpWishRadius, fpNextRadius;
    // Generate local sources at first time
    if (locSources.GetSize() == 0)
    {
        assert(pCarrier);
        locSources.SetSize(1);

        CPlot3D ptStart;
        GetObjectBV(ptStart, fpRadius);
        fpHeight = fpRadius;
        // fpRadius *= 1.5f;
        fpHeight *= 3;
        fpSpeedFactor = (fpRadius / float(N_CASTING_SOURCE_LIFETIME)) * 2.0f;

        locSources[0].nLifeTime = N_BANSHEECASTING_RING_LIFETIME;
        locSources[0].respectCP.z = fpHeight * 0.5f;
        locSources[0].vSpeed.x = 0;

        nMinTrys = 8 * N_CASTING_PT_PER_TAIL;
        return FALSE;
    }

    fpRadius = locSources[0].vSpeed.x;

    fpAngle = random.GetFloat(0, float(2 * pi));
    pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0xFF);
    pSPart->nPhase = 0;
    pSPart->nPhaseLifeTime = N_BANSHEECASTING_PHASE_LIFETIME;

    nBaseLifeTime = Min(locSources[0].nLifeTime, 5);
    pSPart->nLifeTime = random.Get(nBaseLifeTime + 1, nBaseLifeTime + 3);

    fpWishRadius = fpSpeedFactor * N_BANSHEECASTING_RING_SIZE;
    fpNextRadius = fpRadius + (fpRadius - fpWishRadius) * F_BANSHEECASTING_RING_KOEF;
    if (fpRadius > fpNextRadius + 1e-3f)
        fpRadius = random.GetFloat(fpNextRadius, fpRadius);
    else
        fpRadius = fpNextRadius;

    pSPart->coord.x = float(carrierCP.x + fpRadius * sin(fpAngle));
    pSPart->coord.y = float(carrierCP.y + fpRadius * cos(fpAngle));
    pSPart->coord.z = locSources[0].respectCP.z + carrierCP.z;
    pSPart->coord.w = (fpSize) * 0.15f;

    return TRUE;
}

BOOL CParticle::BansheeCastingSingleSegment(SSingleParticle *pSPart)
{
    if (--(pSPart->nLifeTime) == 0)
        return FALSE;
    if (--(pSPart->nPhaseLifeTime) == 0)
    {
        pSPart->nPhase++;
        pSPart->nPhaseLifeTime = N_BANSHEECASTING_PHASE_LIFETIME;
    }
    return TRUE;
}

void CParticle::BansheeCastingLocSourceSegmentFunc(SLocalSource *pLSrc)
{
    if (--pLSrc->nLifeTime == 0)
    {
        ResetFlag(PS_GENERATE_PARTICLES);
        return;
    }
    float fpWishRadius = fpSpeedFactor * N_BANSHEECASTING_RING_SIZE;
    pLSrc->vSpeed.x = fpWishRadius + (pLSrc->vSpeed.x - fpWishRadius) * F_BANSHEECASTING_RING_KOEF;
}

// --------------------------------- Tornado --------------------------------------------------
BOOL CParticle::TornadoSingleCreate(SSingleParticle *pSPart, int)
{
    float x, y, angle, radius, fRadiusMult;
    float h = random.GetFloat(0, 1); // TORNADO_HEIGHT );
    // float h = random.GetFloat( 0, float( locSources.GetSize()-1.1 ) );
    // int nSrc = (int) h; // номер нижнего источника
    // fraction = h - nSrc;

    // h *= TORNADO_HEIGHT / ( TORNADO_NUM_SOURCES - 1 );  // относительная высота партикла
    pSPart->nSpecial = random.Get(0, 2);

    pSPart->dwColor = nLifeOfPhase; // RGBA_MAKE( 0xFF, 0xFF, 0xFF, 0x30 );
    // pSPart->dwColor &= 0xFFFFFF; pSPart->dwColor |=
    pSPart->coord.x = carrierCP.x;
    pSPart->coord.y = carrierCP.y;
    pSPart->coord.z = carrierCP.z + locSources[0].respectCP.z;

    // float fpBaseW = fpSize;// * 0.10f;

    pSPart->nLifeTime = 100 + random.Get(0, 8);

    float fpSH = h * (TORNADO_NUM_SOURCES - 1);
    int nSrc = static_cast<int>(Min<INT_PTR>(Float2Int(fpSH), locSources.GetSize() - 2));
    float fpFraction = Min(1.0f, fpSH - nSrc);
    x = locSources[nSrc].respectCP.x * (1 - fpFraction) +
        locSources[nSrc + 1].respectCP.x * fpFraction;
    y = locSources[nSrc].respectCP.y * (1 - fpFraction) +
        locSources[nSrc + 1].respectCP.y * fpFraction;

    angle = random.GetFloat(float(-pi), float(pi));
    pSPart->reserved2.x = h; // relative height
    pSPart->reserved2.y = 0.1f;
    // pSPart->camera.y = sqrt( pSPart->camera.x );//Min( 1, pSPart->camera.x  );
    fRadiusMult = sqr(sqr(1 - pSPart->reserved2.x)); // + h*h*h*h / ( 10*10*10*10 );
    radius = random.GetFloat(0.8f, 2) * (fRadiusMult + 0.2f) * 2;
    pSPart->reserved.x = angle;
    pSPart->reserved.y = radius;

    pSPart->vSpeed.x = random.GetFloat(float(pi * 0.06), float(pi * 0.07)) * (1.1f - sqr(1 - h)) *
                       1.5f;
    pSPart->vSpeed.y = locSources[0].reserved2.z;
    pSPart->vSpeed.z = 0.0f;

    pSPart->coord.x += x + float(sin(angle) * radius);
    pSPart->coord.y += y + float(cos(angle) * radius);
    pSPart->coord.z += h * locSources[0].reserved2.x;
    if (pSPart->nSpecial)
    {
        pSPart->coord.w = fpSize * (radius + 0.3f); // * (1 + fRadiusMult) / 4;
        pSPart->nPhase = random.Get(0, 11);
    }
    else
    {
        pSPart->nPhase = random.Get(12, 15);
        pSPart->coord.w = fpSize * 0.15f; // * (1 + fRadiusMult) / 4;
    }
    return TRUE;
}

BOOL CParticle::TornadoSingleSegment(SSingleParticle *pSPart)
{
    float x, y, angle, radius;
    if (--(pSPart->nLifeTime) == 0)
        return FALSE;
    pSPart->reserved2.x += pSPart->vSpeed.z;
    float &h = pSPart->reserved2.x;
    pSPart->vSpeed.z += 0.0001f;
    if (h > 1)
        return FALSE;

    float fpSH = h * (TORNADO_NUM_SOURCES - 1);
    int nSrc = static_cast<int>(Min<INT_PTR>(static_cast<int>(fpSH), locSources.GetSize() - 2));
    float fpFraction = fpSH - nSrc;
    fpFraction = Min(1.0f, fpFraction);
    x = locSources[nSrc].respectCP.x * (1 - fpFraction) +
        locSources[nSrc + 1].respectCP.x * fpFraction;
    y = locSources[nSrc].respectCP.y * (1 - fpFraction) +
        locSources[nSrc + 1].respectCP.y * fpFraction;

    pSPart->reserved.x += pSPart->vSpeed.x;
    pSPart->vSpeed.x *= 1.01f;
    pSPart->reserved.y *= pSPart->vSpeed.y;
    angle = pSPart->reserved.x;
    radius = pSPart->reserved.y;
    pSPart->coord.x = carrierCP.x + x + float(sin(angle) * radius);
    pSPart->coord.y = carrierCP.y + y + float(cos(angle) * radius);
    pSPart->coord.z = carrierCP.z + locSources[0].respectCP.z;
    pSPart->coord.z += pSPart->reserved2.x * locSources[0].reserved2.x;
    // уменьшаем прозрачность к центру
    if (pSPart->nLifeTime > 8)
        pSPart->reserved2.y = Min(1.0f, pSPart->reserved2.y + 0.1f);
    else
        pSPart->reserved2.y = Max(0.1f, pSPart->reserved2.y);
    int nAlpha = Float2Int(pSPart->reserved2.y * sqrt(1 - h) * 0xFF);
    pSPart->dwColor = (pSPart->dwColor & 0xFFFFFF) | RGBA_MAKE(0, 0, 0, nAlpha);
    if (pSPart->nSpecial)
    {
        pSPart->coord.w = fpSize * (radius + 0.3f);
        pSPart->nPhase = (pSPart->nPhase + 1) % 12;
    }
    else
    {
        pSPart->nPhase = 12 + ((pSPart->nPhase + 1) & 3);
    }
    return TRUE;
}

void CParticle::TornadoLocSourceSegmentFunc(SLocalSource *pLSrc)
{
    int i;
    float vMod;
    if (pLSrc != &locSources[0])
        return;
    if (locSources[0].reserved2.y == 0)
    {
        CPlot3D ptMoveTo = locSources[0].respectCP - vCarrierSpeed;
        ptMoveTo *= 0.95f;
        ptMoveTo.z = 0;
        for (i = 0; i < locSources.GetSize(); i++)
        {
            float x = ((float)i) / (locSources.GetSize() - 1), mn;
            mn = (float)cos(pi * x / 2); //( (1 + cos(pi*x) ) / 2 );
            locSources[i].respectCP = ptMoveTo * mn;
        }
        locSources[0].respectCP.z = -carrierCP.z +
                                    theWorld.GetSurfaceValue(carrierCP.x, carrierCP.y) + 1;
    }
    else
    {
        locSources[0].respectCP.z = -carrierCP.z +
                                    theWorld.GetSurfaceValue(carrierCP.x, carrierCP.y) + 1;
        CPlot3D vMoveTo = (locSources[0].respectCP + vCarrierSpeed * 20) * (-1), vMoveDir;
        vMoveDir = vMoveTo;
        vMod = fabs(vMoveTo);
        if (vMod != 0)
            vMoveDir = vMoveTo * (1 / (vMod));
        vMod = vMod * 0.5f;
        if (vMod > 0.01f)
            vMod = 0.01f; // ограничение по скорости
        CPlot3D npos = locSources[0].respectCP + vMoveDir * vMod;
        for (i = 0; i < locSources.GetSize(); i++)
        {
            float x = ((float)i) / (locSources.GetSize() - 1), mn;
            mn = (float)((1 + cos(pi * x) + (1 - x) * locSources[0].reserved2.y) / 2.5f);
            locSources[i].respectCP = npos * mn;
        }
    }
    if (!GetCarrier() && locSources[0].reserved2.y == 0 && !particles.IsEmpty())
        FinishEffect();
}

// --------------------------------- Geyser
// --------------------------------------------------------------

BOOL CParticle::GeyserSingleCreate(SSingleParticle *pSPart, int)
{
    int nSrc;
    /*
    // Check for active sources
    BOOL bIsHaveActiveSources = FALSE;
    for ( int nSrc = 0; nSrc < locSources.GetSize(); nSrc++ )
            if ( locSources[nSrc].nLifeTime > 0 )
                    bIsHaveActiveSources = TRUE;
    if ( bIsHaveActiveSources == FALSE )
            return FALSE;
    // Select source
    */
    // do
    nSrc = random.Get(0, locSources.GetSize() - 1);
    // while( locSources[nSrc].nLifeTime <= 0 );

    // Set start values of color and lifetime
    pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0xFF);
    pSPart->coord.x = carrierCP.x;
    pSPart->coord.y = carrierCP.y;
    pSPart->coord.z = carrierCP.z;
    if (particles.GetCount() == 0)
    {
        pSPart->coord.w = 0;
        pSPart->nSpecial = 1;
        pSPart->nPhase = 15;
        return TRUE;
    }

    pSPart->nSpecial = 0;
    pSPart->nPhase = random.Get(0, 11);
    float fpBaseW = fpSize * 0.20f;
    pSPart->coord.w = fpBaseW; // + random.GetFloat( -fpBaseW * 0.2f, fpBaseW * 0.5f );

    pSPart->nLifeTime = 20;

    // Generate start coordinates
    if (locSources[nSrc].nLifeTime > 0)
    {
        pSPart->nLifeTime = 0x7FFFFFFF;
        float fpAngle = locSources[nSrc].respectCP.x + ToRadian(random.GetFloat(-20, 20));
        float fpPhi = locSources[nSrc].respectCP.y + ToRadian(random.GetFloat(-10, 10));
        float fpRadius = locSources[nSrc].respectCP.z + random.GetFloat(-0.1f, 0.1f);
        float fpSinPhi = float(sin(fpPhi));
        pSPart->vSpeed.x = fpRadius * float(sin(fpAngle)) * fpSinPhi * fpSpeedFactor;
        pSPart->vSpeed.y = fpRadius * float(cos(fpAngle)) * fpSinPhi * fpSpeedFactor;
        pSPart->vSpeed.z = fpRadius * float(cos(fpPhi)) * fpSpeedFactor;
        pSPart->coord.x += random.GetFloat(posRespectMinRnd.x, posRespectMaxRnd.x);
        pSPart->coord.y += random.GetFloat(posRespectMinRnd.y, posRespectMaxRnd.y);
        pSPart->coord.z += random.GetFloat(posRespectMinRnd.z, posRespectMaxRnd.z);
        pSPart->coord.w = 0;
        pSPart->reserved.x = fpBaseW;
    }
    else
    {
        pSPart->nLifeTime = 1;
        pSPart->vSpeed.x = 0.f;
        pSPart->vSpeed.y = 0.f;
        pSPart->vSpeed.z = 0.f;
        pSPart->coord.x += random.GetFloat(posRespectMinRnd.x * 0.5f, posRespectMaxRnd.x * 0.5f);
        pSPart->coord.y += random.GetFloat(posRespectMinRnd.y * 0.5f, posRespectMaxRnd.y * 0.5f);
        pSPart->coord.w = 0.01f * fpSize;
    }
    return TRUE;
}

const DWORD DW_GEYSER_ALPHA_DEC = 9;
BOOL CParticle::GeyserSingleSegment(SSingleParticle *pSPart)
{
    if (pSPart->nSpecial)
    {
        if (!CheckFlag(PS_GENERATE_PARTICLES))
        {
            pSPart->coord.w = 0;
            return FALSE;
        }
        pSPart->coord.w = random.GetFloat(0.03f, 0.3f) * fpSize;
        return TRUE;
    }
    DWORD dwAlpha = RGBA_GETALPHA(pSPart->dwColor);
    if (dwAlpha <= DW_GEYSER_ALPHA_DEC)
        return FALSE;
    if (--pSPart->nLifeTime <= 0)
        return FALSE;
    pSPart->nPhase = ((pSPart->nPhase + 1) & 3) + (pSPart->nPhase & ~3);
    // if ( pSPart->nPhase < 3 )
    // pSPart->nPhase++;
    // Update coordinates
    pSPart->reserved.x *= 0.9f;
    pSPart->coord.x += pSPart->vSpeed.x;
    pSPart->coord.y += pSPart->vSpeed.y;
    pSPart->coord.z += pSPart->vSpeed.z;
    pSPart->coord.w = pSPart->reserved.x; // 0.9f;//1.05f;
    // Update speed
    pSPart->vSpeed.z -= fpSpeedFactor * 0.06f;
    // Update color
    pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, (dwAlpha - DW_GEYSER_ALPHA_DEC));

    return TRUE;
}

void CParticle::GeyserLocSourceSegmentFunc(SLocalSource *pLSrc)
{
    if (pLSrc->nLifeTime > 0)
    {
        if (--pLSrc->nLifeTime == 0)
            pLSrc->nPhase = random.Get(3, 36); // set relax time
        return;
    }
    if (--pLSrc->nPhase <= 0)
    {
        pLSrc->nLifeTime = random.Dice(1, 7); // set time to function
        float fpAngle = ToRadian(random.GetFloat(0, 360));
        float fpPhi = ToRadian(random.GetFloat(0, 10));
        float fpRadius = random.GetFloat(0.4f, 1.0f);
        pLSrc->respectCP = CPlot3D(fpAngle, fpPhi, fpRadius); // set stream vector
    }
}

// ---------------------------------- Poison fog
// -----------------------------------------------------------
const float FP_POISON_PARTICLE_SIZE = 1.f;
const DWORD DW_POISON_ALPHA_DEC = 9;
const float FP_POISON_SQ_SIZE = 1;

BOOL CParticle::PoisonFogSingleCreate(SSingleParticle * pSPart, int)
{
	int nDX, nDY, nSize;
	if (particles.GetCount() == 0)
	{
		
		const float fpRad = fpSize;
		nSize = (int)((fpSize + 1) / FP_POISON_SQ_SIZE);
		for (nDY = -nSize; nDY <= nSize; nDY++)
			for (nDX = -nSize; nDX <= nSize; nDX++)
			{
				float fpX = nDX * FP_POISON_SQ_SIZE;
				float fpY = nDY * FP_POISON_SQ_SIZE;
				fpX += random.GetFloat(-FP_POISON_SQ_SIZE * 0.25f, FP_POISON_SQ_SIZE * 0.25f);
				fpY += random.GetFloat(-FP_POISON_SQ_SIZE * 0.25f, FP_POISON_SQ_SIZE * 0.25f);
				if ((fpX * fpX) + (fpY * fpY) < fpRad * fpRad)
				{
					SSingleParticle* pPart = ((CParticleManager*)lpManager)->newParticle();
					if ( pPart )
					{
						fpX += cp.x;
						fpY += cp.y;
						pPart->coord.x = fpX;
						pPart->coord.y = fpY;
						pPart->coord.w = FP_POISON_PARTICLE_SIZE + random.GetFloat( -0.25f, 0.25f );
						pPart->coord.z = theWorld.GetSurfaceValue( fpX, fpY ) + pPart->coord.w;
						
						if ( GetTypeID() == PTID_AGGRESSIONFOG )
							pPart->dwColor = RGBA_MAKE( 0xFF, 0xAF, 0x7F, 0xFF );
						else
							pPart->dwColor = RGBA_MAKE( 0xFF, 0xFF, 0xFF, Min( 0xFF, Float2Int( fpPower * 0xA0 ) ) );
						pPart->nLifeTime = nLifeSpan;
						pPart->nPhase = random.Get( 0, nAnimPhases-1 );
						pPart->nPhaseLifeTime = nLifeOfPhase;							
											
						AddNewParticle( pPart );
					}
				}
			}
	}
	return FALSE;
}

BOOL CParticle::PoisonFogSingleSegment(SSingleParticle *pSPart)
{
    // Update phase
    if (--(pSPart->nPhaseLifeTime) == 0)
    {
        if (++(pSPart->nPhase) >= nAnimPhases)
            pSPart->nPhase = 0;
        pSPart->nPhaseLifeTime = nLifeOfPhase;
    }
    if (CheckFlag(PS_GENERATE_PARTICLES) == 0)
    {
        DWORD dwAlpha = RGBA_GETALPHA(pSPart->dwColor);
        if (dwAlpha <= DW_POISON_ALPHA_DEC)
            return FALSE;
        int nADec = Max(1, Float2Int(fpPower * DW_POISON_ALPHA_DEC * 0.5f));
        if (GetTypeID() == PTID_AGGRESSIONFOG)
            pSPart->dwColor = RGBA_MAKE(0xFF, 0xAF, 0x7F, dwAlpha - nADec);
        else
            pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, dwAlpha - nADec);
        return TRUE;
    }
    return TRUE;
}

// ---------------------------------- Healing
// ----------------------------------------------------------
const int N_HEALING_FADE_LIFETIME = 8;
const int N_HEALING_SOURCE_LIFETIME = 36;
const int N_PT_PER_TAIL = 3;

BOOL CParticle::HealingSingleCreate(SSingleParticle *pSPart, int nTry)
{
    // Generate local sources at first time
    if (locSources.GetSize() == 0)
    {
        if (pCarrier == NULL)
        {
            ReportWarning("Particle effect Healing without carrier\n");
            return FALSE;
        }
        nMinTrys = Float2Int(fpSize * 6.f + 2.f);
        locSources.SetSize(nMinTrys);
        float fpAngleInc = ToRadian(360.f / float(nMinTrys));
        float fpRadius, fpHeight;
        CPlot3D ptStart;
        GetObjectBV(ptStart, fpRadius);
        fpHeight = fpRadius;
        ptStart.z -= fpRadius;

        fpRadius *= 1.1f;
        fpHeight *= 2;
        fpSpeedFactor = (fpRadius / float(N_HEALING_SOURCE_LIFETIME)) * 2.f;
        fpSizeFactor = (fpSize + 1.0f) * 0.25f / float(N_HEALING_SOURCE_LIFETIME);

        float fpAngle = 0;
        // ptStart -= carrierCP;
        for (int i = 0; i < nMinTrys; i++, fpAngle += fpAngleInc)
        {
            locSources[i].nPhase = 0;
            locSources[i].nLifeTime = N_HEALING_SOURCE_LIFETIME;
            float fpZRnd = random.GetFloat(-fpHeight / 10, fpHeight / 10);
            float fpZInc = (fpHeight - fpZRnd) / float(N_HEALING_SOURCE_LIFETIME);

            locSources[i].reserved = ptStart;
            locSources[i].reserved.z += fpZRnd;

            locSources[i].respectCP = locSources[i].reserved;

            locSources[i].vSpeed.x = 0.0f;
            locSources[i].vSpeed.y = fpAngle;
            locSources[i].vSpeed.z = fpZInc;
        }
        dwPFlags = 0;
        nMinTrys *= N_PT_PER_TAIL;
    }
    if (int(dwPFlags) <= locSources.GetSize())
    {
        if (int(dwPFlags) == locSources.GetSize())
        {
            dwPFlags++;
            return FALSE;
        }
        pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0xFF);
        pSPart->nPhase = 0;
        pSPart->nLifeTime = N_HEALING_SOURCE_LIFETIME;
        pSPart->nPhaseLifeTime = 1;
        pSPart->nSpecial = int(dwPFlags);
        pSPart->coord.x = locSources[dwPFlags].respectCP.x + carrierCP.x;
        pSPart->coord.y = locSources[dwPFlags].respectCP.y + carrierCP.y;
        pSPart->coord.z = locSources[dwPFlags].respectCP.z + carrierCP.z;
        pSPart->coord.w = (fpSize + 1.0f) * 0.10f;
        dwPFlags++;
        return TRUE;
    }

    int nLocSrcIndex = random.Get(0, locSources.GetSize() - 1); // nTry / N_PT_PER_TAIL;
    // int nInnerIndex = nTry % N_PT_PER_TAIL;

    pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0xFF);
    pSPart->nPhase = 8;
    pSPart->nLifeTime = 8;
    pSPart->nPhaseLifeTime = 1;
    pSPart->nSpecial = -1;
    float fpRndBase = locSources[nLocSrcIndex].vSpeed.x * 0.15f;
    pSPart->coord.x = locSources[nLocSrcIndex].respectCP.x + carrierCP.x +
                      random.GetFloat(-fpRndBase, fpRndBase);
    pSPart->coord.y = locSources[nLocSrcIndex].respectCP.y + carrierCP.y +
                      random.GetFloat(-fpRndBase, fpRndBase);
    pSPart->coord.z = locSources[nLocSrcIndex].respectCP.z + carrierCP.z +
                      random.GetFloat(-fpRndBase, 0.f);
    pSPart->coord.w = (fpSize + 1.0f) * 0.07f;

    return TRUE;
}

BOOL CParticle::HealingSingleSegment(SSingleParticle *pSPart)
{
    if (--(pSPart->nLifeTime) == 0)
    {
        if (pSPart->nSpecial >= 0 && pSPart->nSpecial != 0xFFFF)
        {
            pSPart->nSpecial = 0xFFFF;
            pSPart->nLifeTime = N_HEALING_FADE_LIFETIME;
            return TRUE;
        }
        return FALSE;
    }
    if (pSPart->nSpecial == 0xFFFF)
    {
        DWORD dwNewAlpha = RGBA_GETALPHA(pSPart->dwColor) - 0xFF / N_HEALING_FADE_LIFETIME;
        pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, dwNewAlpha);
        return TRUE;
    }
    if (pSPart->nSpecial >= 0)
    {
        if (++(pSPart->nPhase) >= 8)
            pSPart->nPhase = 0;
        pSPart->nPhaseLifeTime = 1;
        if (pSPart->nLifeTime >= N_HEALING_SOURCE_LIFETIME / 2)
            pSPart->coord.w += fpSizeFactor;
        else
            pSPart->coord.w -= fpSizeFactor;
        SLocalSource *pLSrc = &locSources[pSPart->nSpecial];
        pSPart->coord.x = pLSrc->respectCP.x + carrierCP.x;
        pSPart->coord.y = pLSrc->respectCP.y + carrierCP.y;
        pSPart->coord.z = pLSrc->respectCP.z + carrierCP.z;
    }
    else
        pSPart->nPhase++;
    return TRUE;
}

void CParticle::HealingLocSourceSegmentFunc(SLocalSource *pLSrc)
{
    if (--pLSrc->nLifeTime == 0)
    {
        ResetFlag(PS_GENERATE_PARTICLES); // nGenerateParticles = 0;
        return;
    }
    if (pLSrc->nLifeTime >= N_HEALING_SOURCE_LIFETIME / 2)
        pLSrc->vSpeed.x += fpSpeedFactor;
    else
        pLSrc->vSpeed.x -= fpSpeedFactor;
    pLSrc->vSpeed.y += ToRadian(10.f);

    pLSrc->respectCP.x = pLSrc->reserved.x + pLSrc->vSpeed.x * float(sin(pLSrc->vSpeed.y));
    pLSrc->respectCP.y = pLSrc->reserved.y + pLSrc->vSpeed.x * float(cos(pLSrc->vSpeed.y));
    pLSrc->respectCP.z += pLSrc->vSpeed.z;
}

// ---------------------------------- VulcanSmoke
// ----------------------------------------------------------

BOOL CParticle::VulcanSmokeSingleCreate(SSingleParticle *pSPart, int)
{
    // Set start values of color and lifetime
    pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0xFF);
    pSPart->nPhase = 0;
    // Generate start coordinates, speed and size
    if (int(random.Get(1, 100) <= 60))
    {
        pSPart->nSpecial = 8;
        pSPart->nLifeTime = nLifeSpan * 8;
        pSPart->nPhaseLifeTime = 8;
        // vulcan cloud particle
        pSPart->coord.x = carrierCP.x;
        pSPart->coord.y = carrierCP.y;
        pSPart->coord.z = carrierCP.z + posRespectMaxRnd.y +
                          random.GetFloat(-posRespectMinRnd.y * 0.5f, posRespectMinRnd.y * 0.5f);
        pSPart->coord.w = fpSize;

        pSPart->reserved.x = fpSizeFactor;
        pSPart->reserved.y = 0.965f;

        float fpRadius = posRespectMinRnd.x;
        float fpAngle = random.GetFloat(0, ToRadian(360.f));
        pSPart->vSpeed.x = fpRadius * float(sin(fpAngle));
        pSPart->vSpeed.y = fpRadius * float(cos(fpAngle));
        pSPart->vSpeed.z = 0.f;
    }
    else
    {
        pSPart->nSpecial = 4;
        pSPart->nLifeTime = nLifeSpan * 4;
        pSPart->nPhaseLifeTime = 4;
        // vulcan pillar particle
        float fpRadius = random.GetFloat(0, posRespectMaxRnd.x);
        float fpAngle = random.GetFloat(0, ToRadian(360.f));

        pSPart->coord.x = carrierCP.x + fpRadius * float(sin(fpAngle));
        pSPart->coord.y = carrierCP.y + fpRadius * float(cos(fpAngle));
        pSPart->coord.z = carrierCP.z;
        pSPart->coord.w = fpSize * 0.3f;

        pSPart->reserved.x = 1.02f;
        pSPart->reserved.y = 1;

        pSPart->vSpeed.x = 0;
        pSPart->vSpeed.y = 0;
        pSPart->vSpeed.z = fpSpeedFactor;
    }
    return TRUE;
}

BOOL CParticle::VulcanSmokeSingleSegment(SSingleParticle *pSPart)
{
    // Update lifetime
    if (--(pSPart->nLifeTime) == 0)
        return FALSE;
    // Update phase
    if (--(pSPart->nPhaseLifeTime) == 0)
    {
        if (++(pSPart->nPhase) >= nAnimPhases)
            return FALSE;
        pSPart->nPhaseLifeTime = pSPart->nSpecial;
    }
    // Update coordinates and size
    pSPart->coord.x += pSPart->vSpeed.x;
    pSPart->coord.y += pSPart->vSpeed.y;
    pSPart->coord.z += pSPart->vSpeed.z;
    pSPart->coord.w *= pSPart->reserved.x;

    pSPart->vSpeed *= pSPart->reserved.y;
    return TRUE;
}

// ----------------------------------
// FireBlast-------------------------------------------------------------
BOOL CParticle::FireBlastSingleCreate(SSingleParticle * pSPart, int)
{
	SLocalSource* pLSrc = locSources.GetData();
	int nSrc = random.Get(-14, 14);
	pSPart->nSpecial = nSrc;
	pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0xFF);
	if (particles.GetCount() == 0 && pLSrc->nPhase == 1)
	{
		pSPart->nSpecial = -1222;
		pSPart->coord = carrierCP;
		pSPart->nPhase = 15;
		pSPart->coord.w = 0;
		return TRUE;
	}
	if (nSrc < 0)
	{
		
		pSPart->nLifeTime = 0;

		CPlot3D vec = GetRandomBallPoint();
		float fpPSize = 1 - fabs2(vec);
		vec *= fpSize;
		pSPart->coord = carrierCP + vec * locSources[0].reserved.x;
		pSPart->reserved = vec * locSources[0].reserved.y;
		pSPart->coord.w = 0;
		float fpBorder = 0.2f + pLSrc->nPhase / 7;
		if (fpPSize < fpBorder)
		{
			pSPart->nPhase = 8;
			pSPart->reserved2.x = fpSize * 0.6f * pLSrc->reserved.x * random.GetFloat(0.8f, 1.2f);
			pSPart->reserved2.y = pSPart->nPhase;
			pSPart->reserved2.z = random.GetFloat(0.35f, 0.7f);
		}
		else
		{
			pSPart->nPhase = random.Get(0, 2);
			pSPart->reserved2.x = fpSize * 0.6f * pLSrc->reserved.x * random.GetFloat(0.8f, 1.2f);
			pSPart->reserved2.y = pSPart->nPhase;
			pSPart->reserved2.z = random.GetFloat(0.35f, 0.7f) * 0.5f + 0.35f * fpPSize / fpBorder; 
		}
	}
	else
	{
		pSPart->nPhase = random.Get(0, 2);
		pLSrc += nSrc;
		if (pLSrc->reserved2.x == 0)
			pSPart->nPhase = 8;
		float fpAlpha = random.GetFloat(0, 1);
		pSPart->coord = carrierCP + pLSrc->respectCP - pLSrc->vSpeed * fpAlpha * fpSize;
		pSPart->coord.w = (0.3f - 0.15f * fpAlpha) * random.GetFloat(0.7f, 1.5f) * (pLSrc->reserved.x / 7 + 0.3f);
		pSPart->coord.w *= fpSize * pLSrc->reserved2.y;
		pSPart->reserved = fpSize * pLSrc->vSpeed;
	}
	
	return TRUE;
}

DWORD dwFireBlastAlpha[16] = { 0xFF, 0xFF, 0xcF, 0xaF, 0x80, 0x80, 0x80, 0x80,
                               0x80, 0xaF, 0xcF, 0xc0, 0x80, 0x30, 0x00, 0 };
BOOL CParticle::FireBlastSingleSegment(SSingleParticle *pSPart)
{
    SLocalSource *pLSrc = locSources.GetData();
    // Update lifetime
    if (pSPart->nSpecial < 0)
    {
        if (pSPart->nPhase >= 14)
        {
            if (pSPart->nSpecial == -1222)
            {
                if (pSPart->coord.w == 0)
                    pSPart->coord.w = 2.3f * fpSize;
                else
                    pSPart->coord.w = 0;
                return pLSrc->nPhase < 3;
            }
            return FALSE;
        }
        pSPart->nLifeTime++;
        pSPart->coord.x = pSPart->coord.x + pSPart->reserved.x;
        pSPart->coord.y = pSPart->coord.y + pSPart->reserved.y;
        pSPart->coord.z = pSPart->coord.z + pSPart->reserved.z;
        pSPart->coord.w = pSPart->reserved2.x;
        if (pSPart->nPhase > 7)
        {
            pSPart->coord.w *= 1.03f;
            pSPart->reserved2.y += pSPart->reserved2.z;
            pSPart->reserved2.z *= 1.02f;
            pSPart->nPhase = Float2Int(pSPart->reserved2.y);
        }
        else
        {
            pSPart->coord.w *= 1.01f; // locSources[0].reserved.z;//pSPart->camera.x;
            pSPart->reserved2.y += pSPart->reserved2.z;
            pSPart->reserved2.z *= 1.02f;
            pSPart->nPhase = Float2Int(pSPart->reserved2.y);
        }
        pSPart->reserved2.x = pSPart->coord.w;
        pSPart->reserved *= (locSources[0].reserved.z - 0.03f);
        // The accelerating phase can skip past the last table entry in one step.
        if (static_cast<unsigned>(pSPart->nPhase) >= _countof(dwFireBlastAlpha))
            return FALSE;
        pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, dwFireBlastAlpha[pSPart->nPhase]);
    }
    else
    {
        pSPart->nPhase++;
        if ((pSPart->nPhase & 7) == 7)
            return FALSE;
        pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0xFF);
        pLSrc += pSPart->nSpecial;
        pSPart->coord += pSPart->reserved;
        pSPart->reserved *= 0.7f;
        pSPart->coord.w *= 0.8f;
    }
    return TRUE;
}

void CParticle::FireBlastLocSourceSegmentFunc(SLocalSource *pLSrc)
{
    pLSrc->reserved.x += pLSrc->reserved.y;
    pLSrc->reserved.y *= pLSrc->reserved.z;
    pLSrc->nPhase++;
    if (pLSrc->nPhase > 6)
        ResetFlag(PS_GENERATE_PARTICLES); // nGenerateParticles = 0;
    pLSrc->respectCP += fpSize * pLSrc->vSpeed;
    pLSrc->vSpeed *= 0.75f;
    pLSrc->vSpeed.z -= 0.03f;
}

// --------------------------------- Fireball
// --------------------------------------------------------------

BOOL CParticle::FireballSingleCreate(SSingleParticle *pSPart, int nNumber)
{
    // Set start values of color and lifetime
    pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0xFF);
    pSPart->nLifeTime = nLifeSpan;
    pSPart->nPhase = 0;
    pSPart->nPhaseLifeTime = nLifeOfPhase;
    // Generate start coordinates
    pSPart->coord = carrierCP;

    float fpAdd, fpDir = 0;
    fpAdd = fpSize * 0.5f;
    if (nNumber == 0)
    {
        pSPart->coord.w = fpSize * 1.2f;
        fpDir = 0;
        pSPart->nSpecial = 1;
    }
    else
    {
        pSPart->coord.w = fpSize * 0.5f;
        fpDir = random.GetFloat(-1, 0);
        pSPart->nSpecial = 0;
    }

    if (fpSpeedMod > 1e-5f)
    {
        float fpRandom = fpDir; // random.GetFloat( posRespectMinRnd.z, posRespectMaxRnd.z ) +
                                // fpDir;
        pSPart->coord += vCarrierSpeed * fpRandom;

        fpRandom = random.GetFloat(posRespectMinRnd.x - fpAdd, posRespectMaxRnd.x + fpAdd);
        pSPart->reserved = vSpeedX1 * fpRandom;

        fpRandom = random.GetFloat(posRespectMinRnd.y - fpAdd, posRespectMaxRnd.y + fpAdd);
        pSPart->reserved += vSpeedX2 * fpRandom;
        if (!pSPart->nSpecial)
            pSPart->coord += pSPart->reserved;
    }
    else
        pSPart->reserved = CPlot3D(0, 0, 0);
    return TRUE;
}

BOOL CParticle::FireballSingleSegment(SSingleParticle *pSPart)
{
    // Update lifetime
    if (--(pSPart->nLifeTime) == 0)
        return FALSE;
    // Update phase
    if (--(pSPart->nPhaseLifeTime) == 0)
    {
        if (++(pSPart->nPhase) >= nAnimPhases)
            return FALSE;
        pSPart->nPhaseLifeTime = nLifeOfPhase;
    }
    // Update coordinates and size
    pSPart->coord.w *= fpSizeFactor;

    if (pSPart->nSpecial)
    {
        pSPart->nSpecial = 0;
        pSPart->coord.w *= 0.5f;
    }
    else
        pSPart->coord -= pSPart->reserved;

    pSPart->reserved *= 0.95f;
    pSPart->coord += pSPart->reserved;
    /*	if ( pSPart->nPhase < 2 )
                    pSPart->coord += pSPart->reserved;
            */
    return TRUE;
}

// ---------------------------------- FireArrow
// -------------------------------------------------------------

BOOL CParticle::FireArrowSingleCreate(SSingleParticle *pSPart, int nNumber)
{
    // Set start values of color and lifetime
    pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0xFF);
    pSPart->nLifeTime = nLifeSpan;
    pSPart->nPhaseLifeTime = nLifeOfPhase;
    // Generate start coordinates
    pSPart->coord = carrierCP;

    float fpAdd, fpDir = 0;
    fpAdd = 0; // fpSize * 0.5f;
    if (nNumber == 0 && dwPFlags == 0)
    {
        dwPFlags = 1;
        pSPart->coord.w = fpSize * 0.4f;
        fpDir = 0;
        pSPart->nSpecial = 2;
        pSPart->nPhase = 0;
    }
    else
    {
        pSPart->coord.w = fpSize * 0.2f;
        fpDir = random.GetFloat(-1, 0);
        pSPart->nSpecial = 0;
        if (random.Get(0, 99) > 70)
            pSPart->nSpecial = 1;
        pSPart->nPhase = 2 + random.Get(0, 1);
    }

    if (fpSpeedMod > 1e-5f)
    {
        float fpRandom = fpDir; // random.GetFloat( posRespectMinRnd.z, posRespectMaxRnd.z ) +
                                // fpDir;
        pSPart->coord += vCarrierSpeed * fpRandom;

        fpRandom = random.GetFloat(posRespectMinRnd.x - fpAdd, posRespectMaxRnd.x + fpAdd);
        pSPart->reserved = vSpeedX1 * fpRandom;

        fpRandom = random.GetFloat(posRespectMinRnd.y - fpAdd, posRespectMaxRnd.y + fpAdd);
        pSPart->reserved += vSpeedX2 * fpRandom;
        if (!pSPart->nSpecial)
        {
            pSPart->coord += pSPart->reserved;
        }
    }
    else
        pSPart->reserved = CPlot3D(0, 0, 0);
    return TRUE;
}

BOOL CParticle::FireArrowSingleSegment(SSingleParticle * pSPart)
{
	AttachCarrier(pCarrier);
	
	BindTo(bindPlace);
	if (pSPart->nSpecial == 2)
	{
		pSPart->coord.x = carrierCP.x + vCarrierSpeed.x;
		pSPart->coord.y = carrierCP.y + vCarrierSpeed.y;
		pSPart->coord.z = carrierCP.z + vCarrierSpeed.z;
		
		pSPart->nPhase = random.Get(0, 2);
		return CheckFlag(PS_GENERATE_PARTICLES) != 0;
		
		
	}
	if (--(pSPart->nLifeTime) == 0)
		return FALSE;
	if (--(pSPart->nPhaseLifeTime) == 0)
	{
		if (++(pSPart->nPhase) >= nAnimPhases)
			return FALSE;
		pSPart->nPhaseLifeTime = nLifeOfPhase;
	}
	

	if (pSPart->nSpecial == 0)
	{
		if (pSPart->nPhase < 6)
			pSPart->coord.w *= 1.4f;
		else
			pSPart->coord.w *= fpSizeFactor;

		pSPart->coord.x -= pSPart->reserved.x;
		pSPart->coord.y -= pSPart->reserved.y;
		pSPart->coord.z -= pSPart->reserved.z;

		pSPart->reserved *= 1.1f;

		pSPart->coord.x += pSPart->reserved.x;
		pSPart->coord.y += pSPart->reserved.y;
		pSPart->coord.z += pSPart->reserved.z;
	}
	else
	{
		if (pSPart->nPhase < 6)
			pSPart->coord.w *= 1.4f;
		
		pSPart->reserved *= 0.95f * random.Get(0, 2);
	}
	return TRUE;
}

// ---------------------------------- AcidRay
// -------------------------------------------------------------

static float fpAcidWidth[] = {
    0.00f, 0.85f, 0.93f, 1.00f, 0.97f, 0.93f, 0.87f, 0.80f, 0.73f, 0.65f,
    0.55f, 0.43f, 0.30f, 0.20f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f,
};
static float fpAcidDropSize[] = {
    1.10f, 0.85f, 0.93f, 1.00f, 0.97f, 0.93f, 0.87f, 0.80f, 0.73f, 0.65f,
    0.55f, 0.43f, 0.30f, 0.20f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f,
};

BOOL CParticle::AcidRaySingleCreate(SSingleParticle *pSPart, int nNumber)
{
    // Set start values of color and lifetime
    pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0xFF);
    pSPart->nLifeTime = nLifeSpan;
    pSPart->nPhaseLifeTime = nLifeOfPhase;
    // Generate start coordinates
    pSPart->coord = carrierCP;

    float fpDir = random.GetFloat(-1, 0);
    pSPart->nSpecial = 0;
    // if ( random.Get( 0, 99 ) > 70 )
    // pSPart->nSpecial = 1;
    pSPart->nPhase = random.Get(0, 1);

    if (fpSpeedMod > 1e-5f)
    {
        float fpRandom = fpDir;
        float fpAdd = 0;
        pSPart->coord += vCarrierSpeed * fpRandom;

        pSPart->vSpeed.x = pSPart->coord.x;
        pSPart->vSpeed.y = pSPart->coord.y;
        pSPart->vSpeed.z = pSPart->coord.z;

        fpRandom = random.GetFloat(-1, 1); // posRespectMinRnd.x-fpAdd, posRespectMaxRnd.x+fpAdd );
        pSPart->reserved = vSpeedX1 * fpRandom;

        fpRandom = random.GetFloat(-1, 1); // posRespectMinRnd.y-fpAdd, posRespectMaxRnd.y+fpAdd );
        pSPart->reserved += vSpeedX2 * fpRandom;
    }
    else
    {
        pSPart->reserved = CPlot3D(0, 0, 0);
        pSPart->vSpeed = carrierCP;
    }
    pSPart->coord = pSPart->vSpeed +
                    pSPart->reserved * fpSize * fpAcidWidth[nLifeSpan - pSPart->nLifeTime];
    pSPart->coord.w = fpSize * fpAcidDropSize[nLifeSpan - pSPart->nLifeTime];
    return TRUE;
}

BOOL CParticle::AcidRaySingleSegment(SSingleParticle *pSPart)
{
    if (--(pSPart->nLifeTime) == 0)
        return FALSE;

    pSPart->coord = pSPart->vSpeed +
                    pSPart->reserved * fpSize * fpAcidWidth[nLifeSpan - pSPart->nLifeTime];
    pSPart->coord.w = fpSize * fpAcidDropSize[nLifeSpan - pSPart->nLifeTime];
    if (--(pSPart->nPhaseLifeTime) == 0)
    {
        if (++(pSPart->nPhase) >= nAnimPhases)
            return FALSE;
        pSPart->nPhaseLifeTime = nLifeOfPhase;
    }
    return TRUE;
}

// ---------------------------------- Link
// -------------------------------------------------------------

BOOL CParticle::BlueGasSingleCreate(SSingleParticle *pSPart, int)
{
    // determine size of effect if this is first particle to be created
    if (locSources.GetSize() == 0)
    {
        if (pCarrier == NULL)
        {
            ReportWarning("Particle effect Link without carrier\n");
            return FALSE;
        }

        float fpRadius, fpHeight;
        CPlot3D ptStart;
        GetObjectBV(ptStart, fpRadius);
        fpHeight = fpRadius;
        fpRadius *= 0.3f;
        fpHeight *= 2;

        locSources.SetSize(1);
        locSources[0].respectCP = ptStart;
        locSources[0].reserved.x = fpRadius;
        locSources[0].reserved.z = fpHeight;
        nMinTrys = Max(1, int(5 * fpSize + .5f));
    }

    const float fpRadius = locSources[0].reserved.x;
    const float fpHeight = locSources[0].reserved.z;
    float fpAngle, fpR, fpDAngle;
    fpR = random.GetFloat(0, fpRadius);
    fpAngle = random.GetFloat(0, float(2 * pi));
    fpDAngle = 0.3f * fpR / fpRadius;

    pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0xFF);
    pSPart->nPhase = 0;
    pSPart->nLifeTime = nLifeSpan;
    pSPart->nPhaseLifeTime = nLifeOfPhase;

    pSPart->reserved = CPlot3D((float)sin(fpAngle) * fpR, (float)cos(fpAngle) * fpR,
                               random.GetFloat(-0.3f * fpRadius, 0.3f * fpRadius));
    pSPart->vSpeed = CPlot3D((float)sin(fpDAngle), (float)cos(fpDAngle),
                             random.GetFloat(0.02f * fpHeight, 0.03f * fpHeight));
    pSPart->coord = locSources[0].respectCP + carrierCP + pSPart->reserved;
    pSPart->coord.w = fpRadius * 0.7f;

    return TRUE;
}

BOOL CParticle::BlueGasSingleSegment(SSingleParticle *pSPart)
{
    if (--(pSPart->nLifeTime) == 0)
        return FALSE;
    if (--(pSPart->nPhaseLifeTime) == 0)
    {
        if (++(pSPart->nPhase) >= nAnimPhases)
            return FALSE;
        pSPart->nPhaseLifeTime = nLifeOfPhase;
    }
    pSPart->coord -= pSPart->reserved;

    float fpX, fpY;
    fpX = pSPart->reserved.x * pSPart->vSpeed.y - pSPart->reserved.y * pSPart->vSpeed.x;
    fpY = pSPart->reserved.x * pSPart->vSpeed.x + pSPart->reserved.y * pSPart->vSpeed.y;
    pSPart->reserved.x = fpX;
    pSPart->reserved.y = fpY;
    pSPart->reserved.z += pSPart->vSpeed.z;
    pSPart->vSpeed.z *= 1.02f;

    pSPart->coord += pSPart->reserved;

    pSPart->coord.w *= 1.01f;
    if (pSPart->nLifeTime < 8)
    {
        DWORD dwAlpha = Max((DWORD)0, (DWORD)RGBA_GETALPHA(pSPart->dwColor) - 0x20);
        pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, dwAlpha);
    }
    return TRUE;
}

// ---------------------------------- Link
// -------------------------------------------------------------

BOOL CParticle::LinkSingleCreate(SSingleParticle * pSPart, int)
{
	
	if (locSources.GetSize() == 0)
	{
		locSources.SetSize(4);
		locSources[0].reserved.x = fabs(ptWallDirection);
		locSources[0].reserved.y = 1 / locSources[0].reserved.x;

		CPlot3D vec1 = ptWallDirection ^ CPlot3D(0, 0, 1);
		float fpLeng = fabs(vec1);
		if (fpLeng < 1e-10)
		{
			vec1 = ptWallDirection ^ CPlot3D(0, 1, 1);
			fpLeng = fabs(vec1);
		}
		vec1 *= 1 / fpLeng;
		CPlot3D vec2 = ptWallDirection ^ vec1;
		fpLeng = fabs(vec2);
		vec2 *= 1 / fpLeng;
		locSources[1].reserved = ptWallDirection / fabs(ptWallDirection);
		locSources[2].reserved = vec1;
		locSources[3].reserved = vec2;
		locSources[0].nLifeTime = 0xFF; 
	}

	pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0);

	float fpRandomX = random.GetFloat(-1, 1);
	float fpRandomY = random.GetFloat(-1, 1);
	pSPart->reserved = CPlot3D(
		fpRandomX * posRespectMinRnd.x,
		fpRandomY * posRespectMinRnd.y,
		random.GetFloat(posRespectMinRnd.z, posRespectMaxRnd.z));

	float fpRadius = float(sqrt((fpRandomX * fpRandomX) + (fpRandomY * fpRandomY)));
	float fpDAngle = 0.2f * fpRadius;
	pSPart->reserved2.x = (float)cos(fpDAngle);
	pSPart->reserved2.y = (float)sin(fpDAngle);

	pSPart->coord = carrierCP + pSPart->reserved;
	if (random.Get(0, 3) == 0 && fpRadius < 0.5f)
	{
		pSPart->vSpeed.y = random.GetFloat(0.10f, 0.12f);
		pSPart->vSpeed.z = fpSize * random.GetFloat(0.13f, 0.16f);
	}
	else
	{
		pSPart->vSpeed.y = random.GetFloat(0.17f, 0.25f);
		pSPart->vSpeed.z = fpSize * random.GetFloat(0.03f, 0.05f);
	}
	
	pSPart->coord.w = pSPart->vSpeed.z;

	pSPart->vSpeed.x = 0;
	pSPart->nPhase = random.Get(0, nAnimPhases);
	return TRUE;
}

BOOL CParticle::LinkSingleSegment(SSingleParticle * pSPart)
{
	pSPart->nPhase = (pSPart->nPhase + 1) % nAnimPhases;
	pSPart->vSpeed.x += pSPart->vSpeed.y;
	float fpPart = pSPart->vSpeed.x * locSources[0].reserved.y;
	if (pSPart->vSpeed.x > locSources[0].reserved.x)
		return FALSE;

	float fpX, fpY;
	fpX = pSPart->reserved.x * pSPart->reserved2.x - pSPart->reserved.y * pSPart->reserved2.y;
	fpY = pSPart->reserved.x * pSPart->reserved2.y + pSPart->reserved.y * pSPart->reserved2.x;
	pSPart->reserved.x = fpX;
	pSPart->reserved.y = fpY;

	pSPart->coord = carrierCP +
		locSources[2].reserved * pSPart->reserved.x +
		locSources[3].reserved * pSPart->reserved.y +
		locSources[1].reserved * pSPart->reserved.z +
		ptWallDirection * fpPart;
	pSPart->coord.z += (0.25f - (fpPart - 0.5f) * (fpPart - 0.5f)) * locSources[0].reserved.x * 0.9f;
	pSPart->coord.w = pSPart->vSpeed.z;

	DWORD dwAlpha = (DWORD)((0.5f - fabs(fpPart - 0.5f)) * 2 * 500);
	dwAlpha = Min((DWORD)locSources[0].nLifeTime, dwAlpha);
	pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, dwAlpha);

	return TRUE;
}

void CParticle::LinkLocSourceSegmentFunc(SLocalSource *pSrc)
{
    if (!CheckFlag(PS_GENERATE_PARTICLES))
        pSrc->nLifeTime -= 0x10;
    pSrc->nLifeTime = Max(0, pSrc->nLifeTime);
}
// ---------------------------------- SphereXXX
// -------------------------------------------------------------
static const float fpSphereParticleSize[] = { 0.010f, 0.020f, 0.030f, 0.035f, 0.035f, 0.035f,
                                              0.035f, 0.035f, 0.035f, 0.035f, 0.035f, 0.035f,
                                              0.035f, 0.030f, 0.020f, 0.010f };

static const float fpSphereParticleSizeFire[] = { 0.010f, 0.015f, 0.02f, 0.03f, 0.015f, 0.010f };

static CPlot3D CalcSpherePointPosition(const CPlot3D &ptSrc, const CPlot3D &ptDir,
                                       const float fpDirLeng)
{
    if (fpDirLeng > 1e-2)
    {
        float fpPart;
        fpPart = (ptDir * ptSrc) / fpDirLeng;
        fpPart = (-fpPart);
        fpPart = Min(1.0f, fpPart);
        fpPart = Max(fpPart, 0.0f);
        return ptSrc - ptDir * fpPart;
    }
    return ptSrc;
}

BOOL CParticle::SphereSingleCreate(SSingleParticle *pSPart, int nNumber)
{
    // determine size of effect if this is first particle to be created
    if (nNumber == 0) // locSources.GetSize() == 0 )
    {
        if (pCarrier == NULL)
        {
            ReportWarning("Particle effect Link without carrier\n");
            return FALSE;
        }

        float fpRadius;
        CPlot3D ptCenter;
        GetObjectBV(ptCenter, fpRadius);
        float fpStartZ = ptCenter.z;
        fpRadius *= 1.2f;

        if (locSources.GetSize() == 0)
        {
            locSources.SetSize(2);
            const float fpDAngle = 0.01f;
            locSources[1].reserved.x = (float)cos(fpDAngle);
            locSources[1].reserved.y = (float)sin(fpDAngle);
            locSources[1].respectCP = CPlot3D(0, 0, 0);
            locSources[0].respectCP = CPlot3D(0, 0, 0);
            locSources[0].respectCP.z = fpStartZ;
            locSources[0].reserved.x = fpRadius;
        }
        locSources[0].respectCP.z = fpStartZ * 0.1f + locSources[0].respectCP.z * 0.9f;
        locSources[0].reserved.x = fpRadius * 0.1f + locSources[0].reserved.x * 0.9f;
        // locSources[0].reserved.z = fpHeight;
    }

    if (nNumber == 0)
    {
        if (GetTypeID() == PTID_ANTIMAGIC)
            nMinTrys = Float2Int(fpSize * 24); // 16 );
        else
            nMinTrys = Float2Int(fpSize * 6); // 4 );
        locSources[1].respectCP = locSources[1].respectCP * 0.7f + vCarrierSpeed;
        locSources[0].reserved.z = fabs(locSources[1].respectCP);
        if (locSources[0].reserved.z < 1e-2)
            locSources[0].reserved.z = 0;
    }

    const float fpRadius = locSources[0].reserved.x;
    // const float fpHeight = locSources[0].reserved.z;
    if (random.Get(0, 10) == 0)
    {
        if (GetTypeID() == PTID_ANTIMAGIC)
            pSPart->vSpeed.x = random.GetFloat(2.5f, 3.5f);
        else if (GetTypeID() == PTID_SPHEREELECTRICITY)
            pSPart->vSpeed.x = random.GetFloat(3.5f, 4.5f);
        else
            pSPart->vSpeed.x = random.GetFloat(1.5f, 2.5f);
    }
    else
        pSPart->vSpeed.x = random.GetFloat(0.7f, 1.3f);

    pSPart->reserved = GetRandomBallPoint();
    // pSPart->reserved += ptWallDirection;
    pSPart->reserved *= 0.65f * fpRadius / fabs(pSPart->reserved);
    pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0xFF);
    pSPart->nPhase = random.Get(0, 10);

    pSPart->nLifeTime = nLifeSpan;

    pSPart->nPhaseLifeTime = 1;
    pSPart->coord = locSources[0].respectCP + carrierCP +
                    CalcSpherePointPosition(pSPart->reserved, locSources[1].respectCP,
                                            locSources[0].reserved.z);

    if (GetTypeID() == PTID_SPHEREFIRE)
        pSPart->coord.w = pSPart->vSpeed.x * fpRadius * fpSphereParticleSize[pSPart->nLifeTime] *
                          2; // 1.6f;
    else if (GetTypeID() == PTID_ANTIMAGIC)
        pSPart->coord.w = pSPart->vSpeed.x * fpRadius * fpSphereParticleSizeFire[pSPart->nLifeTime];
    else if (GetTypeID() == PTID_SPHEREELECTRICITY)
        pSPart->coord.w = pSPart->vSpeed.x * fpRadius * fpSphereParticleSize[pSPart->nLifeTime];
    else
        pSPart->coord.w = pSPart->vSpeed.x * fpRadius * fpSphereParticleSize[pSPart->nLifeTime] *
                          1.6f;

    return TRUE;
}

BOOL CParticle::SphereSingleSegment(SSingleParticle * pSPart)
{
	if (--(pSPart->nLifeTime) == 0)
		return FALSE;
	pSPart->nPhase = (pSPart->nPhase + 1) % nAnimPhases;
	const float fpRadius = locSources[0].reserved.x;

	float fpX, fpY;
	fpX = pSPart->reserved.x * locSources[1].reserved.x - pSPart->reserved.y * locSources[1].reserved.y;
	fpY = pSPart->reserved.x * locSources[1].reserved.y + pSPart->reserved.y * locSources[1].reserved.x;
	pSPart->reserved.x = fpX;
	pSPart->reserved.y = fpY;

	pSPart->coord = locSources[0].respectCP + carrierCP +
		CalcSpherePointPosition(pSPart->reserved, locSources[1].respectCP, locSources[0].reserved.z);

	if (GetTypeID() == PTID_SPHEREFIRE)
		pSPart->coord.w = pSPart->vSpeed.x * fpRadius * 0.5f * fpSphereParticleSize[pSPart->nLifeTime] * 2;
	else if (GetTypeID() == PTID_ANTIMAGIC)
		pSPart->coord.w = pSPart->vSpeed.x * fpRadius * 0.5f * fpSphereParticleSizeFire[pSPart->nLifeTime];
	else if (GetTypeID() == PTID_SPHEREELECTRICITY)
		pSPart->coord.w = pSPart->vSpeed.x * fpRadius * 0.5f * fpSphereParticleSize[pSPart->nLifeTime];
	else
		pSPart->coord.w = pSPart->vSpeed.x * fpRadius * 0.5f * fpSphereParticleSize[pSPart->nLifeTime] * 1.6f;

	return TRUE;
}

// ---------------------------------- Campfire
// -------------------------------------------------------------

BOOL CParticle::CampfireSingleCreate(SSingleParticle *pSPart, int)
{
    // Set start values of color and lifetime
    pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0xFF);
    pSPart->nLifeTime = nLifeSpan;
    pSPart->nPhase = (GetTypeID() == PTID_SMOKE ? 8 : 0);
    pSPart->nPhaseLifeTime = nLifeOfPhase;
    pSPart->coord.w = fpSize;
    // Generate start coordinates
    float fpAdd = 0, fpZAdd = 0;
    if (int(random.Get(1, 100)) <= 40)
    {
        pSPart->coord.w = fpSize * 0.5f;
        fpAdd = fpSize * 0.3f;
    }

    pSPart->reserved.x = random.GetFloat(posRespectMinRnd.x - fpAdd, posRespectMaxRnd.x + fpAdd);
    pSPart->reserved.y = random.GetFloat(posRespectMinRnd.y - fpAdd, posRespectMaxRnd.y + fpAdd);
    pSPart->reserved.z = 0;

    pSPart->reserved *= 0.5f;

    pSPart->coord.x = carrierCP.x + pSPart->reserved.x;
    pSPart->coord.y = carrierCP.y + pSPart->reserved.y;
    pSPart->coord.z = carrierCP.z + random.GetFloat(0, fpSpeedFactor) + fpZAdd;

    pSPart->vSpeed.x = 0;
    pSPart->vSpeed.y = 0;
    pSPart->vSpeed.z = fpSpeedFactor;

    return TRUE;
}

BOOL CParticle::CampfireSingleSegment(SSingleParticle * pSPart)
{
	
	if (--(pSPart->nLifeTime) == 0)
		return FALSE;
	
	if (--(pSPart->nPhaseLifeTime) == 0)
	{
		if (++(pSPart->nPhase) >= nAnimPhases)
			return FALSE;
		pSPart->nPhaseLifeTime = nLifeOfPhase;
		if (pSPart->nPhase > 7)
		{
			if (GetTypeID() == PTID_FIRE || GetTypeID() == PTID_FIREWALL || GetTypeID() == PTID_TORNADOFIRE)
				return FALSE;
			pSPart->nPhaseLifeTime = nLifeOfPhase * 4;
			if (random.Get(3) == 0)
				return FALSE;
		}
		if (pSPart->nPhase == 8)
			pSPart->vSpeed.z *= 0.5;
	}
	
	pSPart->coord.z += pSPart->vSpeed.z;
	if (pSPart->nPhase < 2)
	{
		pSPart->coord.x += pSPart->reserved.x;
		pSPart->coord.y += pSPart->reserved.y;
		pSPart->coord.z += pSPart->reserved.z;
	}
	if (pSPart->nPhase < 7)
		pSPart->coord.w *= fpSizeFactor;
	else
	{
		pSPart->coord.w *= 1.06f;
		float fpMult = fpWindForce * pSPart->coord.w * 0.15f;
		pSPart->coord.x += windDir.x * fpMult;
		pSPart->coord.y += windDir.y * fpMult;
		pSPart->coord.z += windDir.z * fpMult;
	}
	return TRUE;
}

// ---------------------------------- Firewall
// -------------------------------------------------------------
BOOL CParticle::FireWallSingleCreate(SSingleParticle *pSPart, int nTry)
{
    if (!CampfireSingleCreate(pSPart, nTry))
        return FALSE;
    float fpWallSide = random.GetFloat(-1, 1);
    pSPart->coord.x += fpWallSide * ptWallDirection.x;
    pSPart->coord.y += fpWallSide * ptWallDirection.y;
    pSPart->coord.z += -carrierCP.z + theWorld.GetSurfaceValue(pSPart->coord.x, pSPart->coord.y);
    return TRUE;
}

void CParticle::FireLocSourceSegmentFunc(SLocalSource *)
{
    nMinTrys = 20 * fabs(ptWallDirection) / 1.4f;
    nMinTrys = Max(6, nMinTrys);
    nMinTrys = Min(nMinTrys, nParticlesMax / 16);
}

// --------------------------------- Blood --------------------------------------------------
BOOL CParticle::BloodSingleCreate(SSingleParticle *pSPart, int)
{
    if (locSources[0].nPhase < 0)
    {
        nLifeOfPhase = Float2Int(fpPower * 8);
        if (nLifeOfPhase < 1)
            nLifeOfPhase = 1;
        nMinTrys = nLifeOfPhase * 10; // Float2Int( fpPower * 50 );
        locSources.SetSize(nLifeOfPhase);
        for (int i = 0; i < nLifeOfPhase; i++)
        {
            locSources[i].respectCP = GetRandomSpherePoint(); // CPlot3D( 0, 0, 0 );
            locSources[i].nPhase = 0;
        }
    }
    // Set start values of color and lifetime
    pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0xFF);
    pSPart->nLifeTime = nLifeSpan;
    pSPart->nPhase = locSources[0].nLifeTime;

    // Set start coordinates for base point
    CPlot3D vec;
    vec.x = random.GetFloat(-0.03f, 0.03f);
    vec.y = random.GetFloat(-0.03f, 0.03f);
    vec.z = random.GetFloat(-0.03f, 0.03f);
    vec += locSources[random.Get(0, nLifeOfPhase - 1)].respectCP;
    vec *= fpSize;

    pSPart->coord.x = carrierCP.x;
    pSPart->coord.y = carrierCP.y;
    pSPart->coord.z = carrierCP.z;
    pSPart->coord.w = fpPower2;

    // vec += locSources[0].vSpeed * fpSize;
    // Set start speed
    float fpMod = (1.f / FSqrt(vec.x * vec.x + vec.y * vec.y + vec.z * vec.z)) * fpSize / 6;
    if (random.Get(0, 7) == 0)
        fpMod *= 1.5f;
    pSPart->vSpeed = vec * fpMod;
    // Set speed and size factors
    pSPart->reserved.x = 0.9f;
    pSPart->reserved.y = 0.80f;
    pSPart->coord += pSPart->vSpeed * random.GetFloat(0, 1);
    return TRUE;
}

static DWORD dwBloodAlpha[16] = { 0,    0x40, 0x80, 0xc0, 0xFF, 0xFF, 0xFF, 0xFF,
                                  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
BOOL CParticle::BloodSingleSegment(SSingleParticle *pSPart)
{
    // Update lifetime
    if (--(pSPart->nLifeTime) == 0)
        return FALSE;
    pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, dwBloodAlpha[pSPart->nLifeTime]); // 0 );
    //
    // pSPart->nPhase = (( pSPart->nPhase + 1 ) & 3) + 12;
    // Update coordinates
    pSPart->coord.x += pSPart->vSpeed.x;
    pSPart->coord.y += pSPart->vSpeed.y;
    pSPart->coord.z += pSPart->vSpeed.z;
    // Update size
    pSPart->coord.w *= pSPart->reserved.y;
    // Update speed
    pSPart->vSpeed *= pSPart->reserved.x;
    pSPart->vSpeed.z -= fpSize * 0.05f;

    return TRUE;
}

void CParticle::BloodLocSourceSegmentFunc(SLocalSource *pLSrc)
{
    pLSrc->nPhase++;
    // Check for end generation
    if (pLSrc->nPhase > 5)
    {
        ResetFlag(PS_GENERATE_PARTICLES); // nGenerateParticles = 0;
        AttachCarrier(0);
    }
}

// --------------------------------- A4 Blood --------------------------------------------------
BOOL CParticle::A4BloodSingleCreate(SSingleParticle *pSPart, int)
{
    // Set start values of color and lifetime
    pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0xFF);
    pSPart->nPhase = 12;

    // Set start coordinates
    pSPart->coord.x = carrierCP.x;
    pSPart->coord.y = carrierCP.y;
    pSPart->coord.z = carrierCP.z;
    pSPart->coord.w = random.GetFloat(0.1f, 0.2f);

    // Get parent local source
    int nLoc = random.Get(locSources.GetSize() - 1);

    // Set start speed
    float fpDR = locSources[nLoc].vSpeed.x * 0.5f;
    float fpDA = locSources[nLoc].vSpeed.y * ToRadian(10);
    float fpR = locSources[nLoc].vSpeed.x + random.GetFloat(-fpDR, fpDR);
    float fpA = locSources[nLoc].vSpeed.y + random.GetFloat(-fpDA, fpDA);
    pSPart->vSpeed = CPlot3D(fpR * sin(fpA), fpR * cos(fpA), 0.0f);

    // Set speed and size factors
    pSPart->reserved.x = 1.0f;
    pSPart->reserved.z = -0.12f;
    pSPart->reserved.z += random.GetFloat(pSPart->reserved.z * 0.5f, -pSPart->reserved.z * 0.5f);
    pSPart->reserved.y = random.GetFloat(-0.6f, -0.2f);

    return TRUE;
}

BOOL CParticle::A4BloodSingleSegment(SSingleParticle *pSPart)
{
    // Update lifetime
    if (pSPart->coord.z < 0)
        return FALSE;
    // Update coordinates
    pSPart->coord.x += pSPart->vSpeed.x;
    pSPart->coord.y += pSPart->vSpeed.y;
    pSPart->coord.z += pSPart->vSpeed.z;
    // Update size
    pSPart->coord.w *= pSPart->reserved.x;
    // Update speed
    pSPart->vSpeed.z = Max(pSPart->vSpeed.z + pSPart->reserved.z, pSPart->reserved.y);

    return TRUE;
}

void CParticle::A4BloodLocSourceSegmentFunc(SLocalSource *pLSrc)
{
    if (pLSrc->nPhase == 0)
    {
        pLSrc->vSpeed.x = random.GetFloat(0.1f, 0.3f);       // radius
        pLSrc->vSpeed.y = random.GetFloat(0, ToRadian(360)); // angle
    }
    if (pLSrc->nPhase > 0)
    {
        ResetFlag(PS_GENERATE_PARTICLES);
        AttachCarrier(0);
    }
    pLSrc->nPhase++;
}

// --------------------------------- ClayRing ------------------------------------------------
static float fpClayRadius[16] = {
    0.000f, 0.025f, 0.056f, 0.092f, 0.136f, 0.188f, 0.251f, 0.326f,
    0.417f, 0.525f, 0.656f, 0.812f, 1.000f, 1.000f, 1.000f, 1.000f,
};
static DWORD dwClayAlpha[16] = { 0,    0x80, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                 0xFF, 0xA0, 0x50, 0x20, 0,    0,    0,    0 };
BOOL CParticle::ClayRingSingleCreate(SSingleParticle *pSPart, int nNumber)
{
    pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, dwClayAlpha[locSources[0].nLifeTime]);
    pSPart->nLifeTime = nLifeSpan;
    pSPart->nPhase = 1;
    pSPart->nPhaseLifeTime = nLifeOfPhase;
    if (locSources[0].nLifeTime == 13 && nNumber < locSources.GetSize())
    {
        pSPart->reserved = CPlot3D(0, 0, 0);
        pSPart->coord = locSources[nNumber].respectCP;
        pSPart->coord.w = 0.2f;
        pSPart->nPhase = 0;
        pSPart->nSpecial = nNumber;
        return TRUE;
    }

    int n = random.Get(0, locSources.GetSize() - 1);
    float fpPart = random.GetFloat(0, 1);

    pSPart->nLifeTime = 5;
    pSPart->reserved = locSources[n].respectCP * fpPart + locSources[n].vSpeed * (1 - fpPart);
    pSPart->reserved2 = CPlot3D(random.GetFloat(-0.1f, 0.1f), random.GetFloat(-0.1f, 0.1f),
                                random.GetFloat(-0.1f, 0.1f));
    pSPart->coord = locSources[n].vSpeed; // pSPart->reserved + pSPart->camera;
    pSPart->coord.w = 0.1f;
    return TRUE;
}

BOOL CParticle::ClayRingSingleSegment(SSingleParticle *pSPart)
{
    if (--(pSPart->nLifeTime) == 0)
        return FALSE;
    if (pSPart->nPhase == 0)
    {
        // The source can expire before this particle's own lifetime ends.
        if (static_cast<unsigned>(locSources[0].nLifeTime) >= _countof(dwClayAlpha))
            return FALSE;
        pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, dwClayAlpha[locSources[0].nLifeTime]);
        pSPart->coord = locSources[pSPart->nSpecial].respectCP;
        pSPart->coord.w = 0.2f;
        return TRUE;
    }
    // Update phase
    if (--(pSPart->nPhaseLifeTime) == 0)
    {
        if (++(pSPart->nPhase) >= nAnimPhases)
            return FALSE;
        pSPart->nPhaseLifeTime = nLifeOfPhase;
    }
    pSPart->reserved2 *= 0.9f;
    float fpW = pSPart->coord.w;
    pSPart->coord = pSPart->reserved + pSPart->reserved2;
    pSPart->coord.w = fpW * 0.9f;
    int dwAlpha = Max(0, (int)RGBA_GETALPHA(pSPart->dwColor) - 0x30);
    pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, dwAlpha);
    return TRUE;
}

void CParticle::ClayRingLocSourceSegmentFunc(SLocalSource *pLSrc)
{
    if (--pLSrc->nLifeTime < 0)
    {
        ResetFlag(PS_GENERATE_PARTICLES);
        return;
    }
    pLSrc->vSpeed = pLSrc->respectCP;
    if (fpSize < 0)
        pLSrc->respectCP = carrierCP +
                           pLSrc->reserved * fpSize * fpClayRadius[pLSrc->nLifeTime]; //  * -15;
    else
        pLSrc->respectCP = carrierCP +
                           pLSrc->reserved * fpSize * fpClayRadius[14 - pLSrc->nLifeTime];
    float fpX, fpY;
    pLSrc->respectCP.z = theWorld.GetSurfaceValue(pLSrc->respectCP.x, pLSrc->respectCP.y) + 1;
    fpX = pLSrc->reserved.x * 0.95f + pLSrc->reserved.y * 0.31225f;
    fpY = -pLSrc->reserved.x * 0.31225f + pLSrc->reserved.y * 0.95f;
    pLSrc->reserved.x = fpX; // * fpMn;
    pLSrc->reserved.y = fpY; // * fpMn;
    if (pLSrc->vSpeed.x == 0 && pLSrc->vSpeed.y == 0)
        pLSrc->vSpeed = pLSrc->respectCP;
}

// --------------------------------- Teleport ------------------------------------------------
BOOL CParticle::TeleportSingleCreate(SSingleParticle * pSPart, int)
{
	pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0xFF);
	pSPart->nLifeTime = nLifeSpan;
	pSPart->nPhase = 0;
	pSPart->nPhaseLifeTime = nLifeOfPhase;

	float fpX, fpY, fpR = fabs(fpSize), fpR2 = (fpR * fpR), fpXYR;

	fpX = random.GetFloat(0, 1); fpX = (fpX * fpX + fpX) / 2;
	float fpA = random.GetFloat(0, 2 * 3.1416f);
	fpXYR = fpX;
	fpX = fpR * fpXYR * sin(fpA);
	fpY = fpR * fpXYR * cos(fpA);

	pSPart->coord = carrierCP + CPlot3D(fpX, fpY, 0);
	pSPart->coord.z += random.GetFloat(-0.4f, -0.2f) * fpR;
	pSPart->coord.w = 0;
	pSPart->reserved2.x = 0.2f * (2 - fpXYR);
	if (random.GetFloat(0, 1) > fpXYR)
		pSPart->nPhase = random.Get(4, 15);
	else
		pSPart->nPhase = random.Get(0, 3);
	if (fpSize > 0)
	{
		pSPart->reserved.z = fpR * 3 * 0.2f * (1 - fpXYR + 3) / 4;
		pSPart->reserved.x = 1.2f;
		pSPart->nSpecial = -0x13;
	}
	else
	{
		pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0x00);
		pSPart->reserved.z = fpR * 3 * 3 * (1 - fpXYR + 3) / 4;
		pSPart->reserved.x = 1 / 1.2f;
		pSPart->nSpecial = 0x13;
	}
	pSPart->coord.z += pSPart->reserved.z;
	return TRUE;
}

BOOL CParticle::TeleportSingleSegment(SSingleParticle *pSPart)
{
    if (--(pSPart->nLifeTime) == 0)
        return FALSE;
    // Update phase
    if (--(pSPart->nPhaseLifeTime) == 0)
    {
        pSPart->nPhase++;
        if (pSPart->nPhase == 4)
            pSPart->nPhase = 0;
        if (pSPart->nPhase == 16)
            pSPart->nPhase = 4;
        pSPart->nPhaseLifeTime = nLifeOfPhase;
    }
    pSPart->coord.z -= pSPart->reserved.z;
    pSPart->reserved.z *= pSPart->reserved.x;
    pSPart->coord.z += pSPart->reserved.z;
    if (pSPart->nLifeTime == 1)
        pSPart->coord.w = 0;
    else
        pSPart->coord.w = pSPart->reserved2.x;
    DWORD dwAlpha = Max((DWORD)0, (DWORD)RGBA_GETALPHA(pSPart->dwColor) + pSPart->nSpecial);
    dwAlpha = Min(dwAlpha, (DWORD)0xFF);
    pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, dwAlpha);
    return TRUE;
}

void CParticle::TeleportLocSourceSegmentFunc(SLocalSource *pLSrc)
{
    if (++(pLSrc->nLifeTime) > 35)
        ResetFlag(PS_GENERATE_PARTICLES); // nGenerateParticles = 0;
}

// --------------------------------- Modifier ------------------------------------------------
static DWORD dwModifierAlpha[35] = {
    /*	0x20, 0x40, 0x80, 0xFF,  0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xB0,  0x70, 0x40, 0x20, 0*/
    0x0, 0x20, 0x40, 0x80, 0xFF, /*0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF,*/ 0xCF, 0x9F, 0x6F, 0x3F, 0x0F, 0
};
static DWORD dwModifierAlphaLarge[35] = {
    /*	0x0,  0x20, 0x40, 0x80,  0xFF,  0xFF, 0xFF, 0xFF,
            0xC0, 0x80, 0x40, 0x20,  0*/
    0x0, 0x80, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xC0, 0x80, 0x40, 0x20, 0
};
static DWORD dwModifierAlphaLarge1[35] = { 0x0,  0x20, 0x40, 0x80, 0xFF, 0xFF, 0xFF,
                                           0xFF, 0xFF, 0xFF, 0xFF, 0x80, 0 };
BOOL CParticle::ModifierSingleCreate(SSingleParticle *pSPart, int nNumber)
{
    pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0xFF);
    pSPart->nLifeTime = nLifeSpan;
    pSPart->nPhaseLifeTime = nLifeOfPhase;
    if (locSources.GetSize() == 0)
    {
        if (pCarrier == NULL)
        {
            ReportWarning("Particle effect Modifier without carrier\n");
            return FALSE;
        }
        locSources.SetSize(Max(1, (int)(fabs(fpSize) * 10)));
        float fpRadius;
        CPlot3D ptCenter;
        GetObjectBV(ptCenter, fpRadius);

        posRespectMinRnd
            .x = fpRadius; // nobody is perfect :), sorry, got no storage for global data here
        fpRadius *= 2;

        CPlot3D ptProect(0, 0, 0);
        switch (GetTypeID())
        {
        case PTID_MODIFIER1:
            ptProect = CPlot3D(0, 0, 1);
            break;
        case PTID_MODIFIER2:
        case PTID_MODIFIER3:
            ptProect = CPlot3D(0, 0, 1);
            posRespectMinRnd.x += fpRadius * 0.4f; // head
            break;
        case PTID_MODIFIER4:
            ptProect = CPlot3D(0, 0, 1);
            posRespectMinRnd.x -= fpRadius * 0.4f; // legs
            break;
        case PTID_MODIFIER5:
        case PTID_MODIFIER6:
        case PTID_MODIFIER7:
        case PTID_MODIFIER8:
            ptProect = pCarrier->GetYAxis();
            break;
        }
        for (int i = 0; i < locSources.GetSize(); i++)
        {
            locSources[i].nLifeTime = 0;
            CPlot3D ptDir;
            float fpLeng;
            do
            {
                ptDir.x = random.GetFloat(-1, 1);
                ptDir.y = random.GetFloat(-1, 1);
                ptDir.z = random.GetFloat(-1, 1);
                ptDir -= ptProect * (ptDir * ptProect);
                fpLeng = fabs2(ptDir);
            } while (fpLeng == 0);
            fpLeng = fpRadius / sqrt(fpLeng);
            if (fpSize < 0)
                fpLeng /= 23;
            ptDir *= fpLeng;
            locSources[i].reserved = ptDir;
            locSources[i].respectCP = carrierCP + ptDir;
            locSources[i].respectCP.z += posRespectMinRnd.x;
            locSources[i].vSpeed = locSources[i].respectCP;
        }
    }
    pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, dwModifierAlpha[locSources[0].nLifeTime]);
    if (locSources[0].nLifeTime == 0 && nNumber < locSources.GetSize())
    {
        float fpPSize = 0.5f; // large particle size
        // large particles creation
        switch (GetTypeID())
        {
        case PTID_MODIFIER1:
            pSPart->nPhase = 0;
            break;
        case PTID_MODIFIER2:
            pSPart->nPhase = 1;
            break;
        case PTID_MODIFIER3:
            pSPart->nPhase = 2;
            break;
        case PTID_MODIFIER4:
            pSPart->nPhase = 3;
            break;
        case PTID_MODIFIER5:
            pSPart->nPhase = 4;
            break;
        case PTID_MODIFIER6:
            pSPart->nPhase = 5;
            break;
        case PTID_MODIFIER7:
            pSPart->nPhase = 6;
            break;
        case PTID_MODIFIER8:
            pSPart->nPhase = 7;
            break;
        case PTID_MODIFIER9:
            pSPart->nPhase = 8;
            break;
        case PTID_MODIFIER10:
            pSPart->nPhase = 9;
            break;
        case PTID_MODIFIER11:
            pSPart->nPhase = 10;
            break;
        case PTID_MODIFIER12:
            pSPart->nPhase = 11;
            break;
        }
        pSPart->nSpecial = nNumber;
        pSPart->coord = locSources[pSPart->nSpecial].respectCP;
        if (fpSize < 0)
            pSPart->coord.w = fpPSize * 0.4f; // 2f;////0.07f;
        else
            pSPart->coord.w = fpPSize * 0.25f; // 0.15f//085f;//0.15f;
        pSPart->nLifeTime = 15 + 10;
        return TRUE;
    }
    int n = random.Get(0, locSources.GetSize() - 1);
    float fpPart = random.GetFloat(0, 1);

    pSPart->nPhase = random.Get(12, 15);
    pSPart->nLifeTime = 5;
    pSPart->reserved = locSources[n].respectCP * fpPart + locSources[n].vSpeed * (1 - fpPart);
    pSPart->reserved2 = CPlot3D(random.GetFloat(-0.04f, 0.04f), random.GetFloat(-0.04f, 0.04f),
                                random.GetFloat(-0.04f, 0.04f));
    // CPlot3D ( random.GetFloat( -0.1f, 0.1f ), random.GetFloat( -0.1f, 0.1f ),random.GetFloat(
    // -0.1f, 0.1f ) );
    pSPart->coord = locSources[n].vSpeed; // pSPart->reserved + pSPart->camera;
    pSPart->coord.w = 0.06f;              // 2f;////0.07f;

    return TRUE;
}

BOOL CParticle::ModifierSingleSegment(SSingleParticle *pSPart)
{
    if (--(pSPart->nLifeTime) == 0)
        return FALSE;
    if (pSPart->nPhase < 12)
    {
        float fpW = pSPart->coord.w;
        pSPart->coord = locSources[pSPart->nSpecial].respectCP;
        if (fpSize < 0)
        {
            pSPart->coord.w = fpW * (1 / 1.07f); // 12f );//0.2f;
            pSPart->dwColor = RGBA_MAKE(
                0xFF, 0xFF, 0xFF, dwModifierAlphaLarge[locSources[pSPart->nSpecial].nLifeTime]);
        }
        else
        {
            pSPart->coord.w = fpW * 1.07f; // 12f;//0.2f;
            pSPart->dwColor = RGBA_MAKE(
                0xFF, 0xFF, 0xFF, dwModifierAlphaLarge1[locSources[pSPart->nSpecial].nLifeTime]);
        }
        return TRUE;
    }
    // Update phase
    if (--(pSPart->nPhaseLifeTime) == 0)
    {
        if (++(pSPart->nPhase) >= nAnimPhases)
            pSPart->nPhase = 12;
        pSPart->nPhaseLifeTime = nLifeOfPhase;
    }
    pSPart->reserved2 *= 0.9f;
    float fpW = pSPart->coord.w;
    pSPart->coord = pSPart->reserved + pSPart->reserved2;
    pSPart->coord.w = fpW * 0.9f;
    int dwAlpha = Max((int)0, (int)RGBA_GETALPHA(pSPart->dwColor) - 0x30);
    pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, dwAlpha);
    return TRUE;
}

void CParticle::ModifierLocSourceSegmentFunc(SLocalSource *pLSrc)
{
    if (pLSrc->nLifeTime++ >= 10)
    {
        ResetFlag(PS_GENERATE_PARTICLES);
        return;
    }
    // pLSrc->nLifeTime++;
    pLSrc->vSpeed = pLSrc->respectCP;
    float fpMn;
    if (fpSize < 0)
    {
        pLSrc->respectCP = carrierCP + pLSrc->reserved;
        pLSrc->respectCP.z += posRespectMinRnd.x;
        fpMn = 1.4f; // 17f;//1.2f;
    }
    else
    {
        pLSrc->respectCP = carrierCP + pLSrc->reserved;
        pLSrc->respectCP.z += posRespectMinRnd.x;
        fpMn = 1 / 1.4f; // 17f;
    }
    pLSrc->reserved *= fpMn;
}

// ----------------------------- CastingStarRound --------------------------------------------
const int CASTING_STAR_LIFETIME = 28;
const float CASTING_ROTATION_SPEED1 = 0.5f; // rad/segment
const float CASTING_ROTATION_SPEED2 = 0.3f; // rad/segment
BOOL CParticle::CastingStarRoundSingleCreate(SSingleParticle * pSPart, int)
{
	
	if (locSources.GetSize() < nLifeSpan)
	{
		if (pCarrier == NULL)
		{
			ReportWarning( "Particle effect CastingElemental without carrier\n");
			return FALSE;
		}
		float fpRadius, fpHeight;
		if (locSources.GetSize() == 0)
		{
			CPlot3D ptStart;
			GetObjectBV(ptStart, fpRadius);
			fpHeight = fpRadius;
			ptStart.z -= fpRadius;
			fpRadius *= 0.5f;
		}
		else
		{
			fpRadius = locSources[0].reserved.x;
			fpHeight = locSources[0].vSpeed.z;
		}
		

		int nSrcIdx = locSources.GetSize();
		locSources.SetSize(nSrcIdx + 1);
		SLocalSource & src = locSources[nSrcIdx];
		float fpSrcAngle = 2 * FP_PI * nSrcIdx / nLifeSpan;

		float fpRotationSpeed, fpEffectHeight, fpEffectDir;
		fpEffectHeight = fpHeight * 0.4f;
		fpEffectDir = 1;
		switch (GetTypeID())
		{
		case PTID_CASTINGILLUSION:     fpRotationSpeed = CASTING_ROTATION_SPEED2; break;
		case PTID_CASTINGENCHANTMENT:  fpRotationSpeed = CASTING_ROTATION_SPEED2; break;
		case PTID_CASTINGDOMINATION:
			fpRotationSpeed = CASTING_ROTATION_SPEED1;
			fpEffectHeight = fpHeight * 1.6f;
			fpEffectDir = -1;
			break;
		case PTID_CASTINGHEALING:      fpRotationSpeed = CASTING_ROTATION_SPEED1; break;
		}

		src.nPhase = 0;
		src.nLifeTime = 1;
		src.reserved.x = fpRadius * cos(fpSrcAngle);
		src.reserved.y = fpRadius * sin(fpSrcAngle);
		src.reserved.z = fpEffectHeight;
		src.respectCP = carrierCP + src.reserved;
		src.reserved2 = src.respectCP;
		src.vSpeed.x = (float)cos(fpRotationSpeed);
		src.vSpeed.y = (float)sin(fpRotationSpeed);
		src.vSpeed.z = fpHeight * fpEffectDir;

		
		pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0xFF);
		pSPart->nPhase = 0;
		pSPart->nLifeTime = 6;
		pSPart->nPhaseLifeTime = 1;
		pSPart->nSpecial = -1 - nSrcIdx;
		pSPart->coord = src.respectCP;
		pSPart->coord.w = 0;
		return TRUE;
	}

	int nSrc = random.Get(0, nLifeSpan - 1);
	SLocalSource & src = locSources[nSrc];
	float fpAlpha = random.GetFloat(0, 1);

	pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0xFF);
	pSPart->nPhase = 1;
	pSPart->nLifeTime = CASTING_STAR_LIFETIME;
	pSPart->nPhaseLifeTime = 1;
	pSPart->nSpecial = nSrc;
	pSPart->reserved2.z = 0.03f * src.vSpeed.z * random.GetFloat(0.85f, 1.2f);
	
		
	
	pSPart->reserved2.y = 1.03f;
	pSPart->reserved.x = random.GetFloat(posRespectMinRnd.x, posRespectMaxRnd.x);
	pSPart->reserved.y = random.GetFloat(posRespectMinRnd.y, posRespectMaxRnd.y);
	pSPart->reserved.z = random.GetFloat(posRespectMinRnd.z, posRespectMaxRnd.z);

	pSPart->coord = pSPart->reserved + src.reserved2;
	pSPart->reserved += src.reserved2 * fpAlpha + src.respectCP * (1 - fpAlpha);
	pSPart->coord.w = fpSize * 0.20f * random.GetFloat(0.6f, 1.4f);
	return TRUE;
}

static DWORD dwCastElemAlpha[] = { 0, 0x20, 0x40, 0x70, 0xa0, 0xd0, 0xff };
BOOL CParticle::CastingStarRoundSingleSegment(SSingleParticle *pSPart)
{
    if (pSPart->nSpecial < 0)
    {
        pSPart->coord = locSources[-pSPart->nSpecial - 1].respectCP;
        pSPart->coord.w = fpSize * 0.4f * random.GetFloat(0.8f, 1.2f);
        if (!CheckFlag(PS_GENERATE_PARTICLES) || !pCarrier)
        {
            if (pSPart->nLifeTime < 0)
                return FALSE;
            pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, dwCastElemAlpha[pSPart->nLifeTime--]);
        }
        return TRUE;
    }
    if (--(pSPart->nLifeTime) == 0)
        return FALSE;
    if (pSPart->nLifeTime < 7)
        pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, dwCastElemAlpha[pSPart->nLifeTime - 1]);
    pSPart->nPhase++;
    if (pSPart->nPhase == 16)
        pSPart->nPhase = 12;
    float fpW = pSPart->coord.w;
    pSPart->coord = pSPart->reserved;
    pSPart->coord.w = fpW * 0.97f;
    pSPart->reserved.z += pSPart->reserved2.z;
    pSPart->reserved2.z *= pSPart->reserved2.y;
    return TRUE;
}

void CParticle::CastingStarRoundLocSourceSegmentFunc(SLocalSource *pLSrc)
{
    pLSrc->reserved2 = pLSrc->respectCP;
    float fpX, fpY;
    fpX = pLSrc->reserved.x * pLSrc->vSpeed.x + pLSrc->reserved.y * pLSrc->vSpeed.y;
    fpY = -pLSrc->reserved.x * pLSrc->vSpeed.y + pLSrc->reserved.y * pLSrc->vSpeed.x;
    pLSrc->reserved.x = fpX;
    pLSrc->reserved.y = fpY;
    pLSrc->respectCP = carrierCP + pLSrc->reserved;
}

// ------------------------------ CastingFailed ----------------------------------------------
BOOL CParticle::CastingFailedSingleCreate(SSingleParticle *pSPart, int)
{
    // Generate local sources at first time
    if (locSources.GetSize() == 0)
    {
        if (pCarrier == NULL)
        {
            ReportWarning("Particle effect CastingElemental without carrier\n");
            return FALSE;
        }
        float fpRadius, fpHeight;
        CPlot3D ptCenter;
        GetObjectBV(ptCenter, fpRadius);
        fpHeight = fpRadius * 2;

        CPlot3D ptStart(0, 0, 0);
        ptStart.z = -ptCenter.z - fpHeight * 0.3f;
        fpRadius *= 0.8f; // 1.1f;
#ifndef _A4
        if (pCarrier->WhoAmI() == HPC_UNIT)
            fpRadius = ((CMapObject *)pCarrier)->GetAttackDistance();
#endif //_A4
        fpRadius *= 2;

        locSources.SetSize(1);
        locSources[0].nPhase = 0;
        locSources[0].reserved.x = fpRadius;
        locSources[0].reserved.y = 0;
        locSources[0].reserved.z = fpHeight;
        locSources[0].reserved2 = ptStart;
        locSources[0].nLifeTime = 1;
    }

    pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0xFF);
    pSPart->nPhase = 1;
    pSPart->nLifeTime = 13;
    pSPart->nPhaseLifeTime = 1;
    pSPart->reserved2.z = 0.10f * locSources[0].reserved.z * random.GetFloat(0.7f, 1.3f);
    // float fpAlpha = random.GetFloat( 0, 1 );

    float fpR = random.GetFloat(0.9f, 1.1f);
    float fpA = random.GetFloat(0, 2 * 3.14159f);
    pSPart->reserved.x = (float)(sin(fpA) * fpR);
    pSPart->reserved.y = (float)(cos(fpA) * fpR);
    pSPart->reserved.z = random.GetFloat(posRespectMinRnd.z, posRespectMaxRnd.z);
    pSPart->reserved.z += locSources[0].reserved2.z;

    pSPart->coord = carrierCP + pSPart->reserved;
    pSPart->coord.w = fpSize * 0.25f * random.GetFloat(0.6f, 1.4f);
    return TRUE;
}

static DWORD dwCastFailedAlpha[12] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                       0xCF, 0x9F, 0x6F, 0x3F, 0x0F, 0 };
BOOL CParticle::CastingFailedSingleSegment(SSingleParticle *pSPart)
{
    if (--(pSPart->nLifeTime) == 0)
        return FALSE;
    pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, dwCastFailedAlpha[Max(0, 11 - pSPart->nLifeTime)]);
    pSPart->nPhase++;
    float fpW = pSPart->coord.w;
    pSPart->coord = carrierCP + pSPart->reserved;
    pSPart->coord.w = fpW * 0.97f;
    pSPart->reserved.z += pSPart->reserved2.z;
    pSPart->reserved2.z *= 1.05f;
    return TRUE;
}

void CParticle::CastingFailedLocSourceSegmentFunc(SLocalSource *pLSrc)
{
    pLSrc->nLifeTime++;
}

// ------------------------------ CastingGather ----------------------------------------------
const float FP_CASTING_S_SRC_LENG = 10;
const float FP_CASTING_S_SRC_RND_LENG = 0.1f;
const float FP_CASTING_S_SRC_RND_STEP = 0.1f;
BOOL CParticle::CastingGatherSingleCreate(SSingleParticle * pSPart, int)
{
	
	if (locSources.GetSize() == 0)
	{
		if (pCarrier == NULL)
		{
			ReportWarning( "Particle effect CastingElemental without carrier\n");
			return FALSE;
		}
		float fpRadius, fpHeight;
		CPlot3D ptCenter;
		GetObjectBV(ptCenter, fpRadius);
		fpHeight = fpRadius;
		fpRadius *= 0.8f;
#ifndef _A4
		if (pCarrier->WhoAmI() == HPC_UNIT)
			fpRadius = ((CMapObject*)pCarrier)->GetAttackDistance();
#endif 
		if (GetTypeID() == PTID_CASTINGDIVINATION)
			fpRadius *= 2;
		else
			fpRadius *= 3;

		locSources.SetSize(nLifeSpan + 1);
		locSources[0].nPhase = 0xFF;  
		locSources[0].nLifeTime = 6;  
		locSources[0].reserved.x = fpRadius;
		if (GetTypeID() == PTID_CASTINGDIVINATION)
		{
			locSources[0].reserved.y = 0.07f;   
			locSources[0].vSpeed.x = 1.05f;     
		}
		else
		{
			locSources[0].reserved.y = 0.08f;
			locSources[0].vSpeed.x = 1.07f;
		}
		locSources[0].respectCP.x = 1;  
		for (int i = 1; i <= nLifeSpan; i++)
		{
			locSources[i].reserved = GetRandomBallPoint() * FP_CASTING_S_SRC_LENG;
			locSources[i].reserved2 = GetRandomBallPoint() * FP_CASTING_S_SRC_RND_LENG;
			locSources[i].respectCP.x = 0; 
		}

		
		if (GetTypeID() == PTID_CASTINGDIVINATION)
		{
			pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0);
			pSPart->nPhase = 0;
			pSPart->nSpecial = 1;
			pSPart->coord = carrierCP;
			pSPart->coord.w = 0;
			return TRUE;
		}
	}

	pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0);
	pSPart->nPhase = 14;
	pSPart->nLifeTime = 13;
	pSPart->nSpecial = 0;
	pSPart->nPhaseLifeTime = 1;
	float fpX, fpY, fpZ, fpLeng;
	for (;;)
	{
		fpX = random.GetFloat(-1, 1);
		fpY = random.GetFloat(-1, 1);
		fpZ = random.GetFloat(-1, 1);
		fpLeng = fpX * fpX + fpY * fpY + fpZ * fpZ;
		if (fpLeng <= 1 && fpLeng > 0)
			break;
	}
	int nSrc = random.Get(0, nLifeSpan * nLifeOfPhase);
	if (nSrc > 0)
	{
		nSrc = (nSrc - 1) % nLifeSpan;
		fpX += locSources[nSrc].reserved.x;
		fpY += locSources[nSrc].reserved.y;
		fpZ += locSources[nSrc].reserved.z;
		fpLeng = fpX * fpX + fpY * fpY + fpZ * fpZ;
	}
	fpLeng = 1 / sqrt(fpLeng);
	pSPart->reserved.x = fpX * fpLeng;
	pSPart->reserved.y = fpY * fpLeng;
	pSPart->reserved.z = fpZ * fpLeng;

	if (GetTypeID() == PTID_CASTINGDIVINATION)
	{
		pSPart->reserved2.x = locSources[0].reserved.x * 0.6f;  
		pSPart->reserved2.y = pSPart->reserved2.x / 26.0f;      
	}
	else
	{
		pSPart->reserved2.x = locSources[0].reserved.x * 0.6f;
		pSPart->reserved2.y = pSPart->reserved2.x / 13.0f;
	}
	pSPart->reserved2.z = locSources[0].vSpeed.x;      

	pSPart->coord = carrierCP + pSPart->reserved * pSPart->reserved2.x;
	
	pSPart->coord.w = pSPart->reserved2.x * locSources[0].reserved.y * random.GetFloat(0.6f, 1.4f);
	return TRUE;
}

static DWORD dwCastGatherFade[] = { 0x00, 0x20, 0x40, 0x70, 0xa0, 0xd0, 0xff };
static DWORD dwCastGatherAlpha[] = { 0x00, 0x20, 0x40, 0x70, 0xa0, 0xd0, 0xff, 0xff,
                                     0xe0, 0xc0, 0x80, 0x40, 0x20, 0x10, 0x00 };
static DWORD dwCastGatherAlpha1[] = { 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                      0xe0, 0xc0, 0x80, 0x40, 0x20, 0x10, 0x00 };
BOOL CParticle::CastingGatherSingleSegment(SSingleParticle *pSPart)
{
    if (locSources[0].nPhase == 0)
        return FALSE;
    if (pSPart->nSpecial)
    {
        pSPart->coord = carrierCP;
        pSPart->coord.w = locSources[0].reserved.x * 0.3f * random.GetFloat(0.85f, 1.15f);
        pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, locSources[0].nPhase);
        return TRUE;
    }
    if (--(pSPart->nLifeTime) == 0)
        return FALSE;
    if (GetTypeID() == PTID_CASTINGDIVINATION)
        pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF,
                                    Min(locSources[0].nPhase,
                                        (int)dwCastGatherAlpha[pSPart->nLifeTime]));
    else
        pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF,
                                    Min(locSources[0].nPhase,
                                        (int)dwCastGatherAlpha1[pSPart->nLifeTime]));
    if (GetTypeID() == PTID_CASTINGFIRE && (pSPart->nLifeTime & 3) == 0)
        pSPart->nPhase++;
    pSPart->nPhase--;
    float fpW = pSPart->coord.w;
    pSPart->reserved2.x -= pSPart->reserved2.y;
    pSPart->coord = carrierCP + pSPart->reserved * pSPart->reserved2.x;
    // pSPart->coord.z += locSources[0].camera.z;
    pSPart->coord.w = fpW * pSPart->reserved2.z;
    return TRUE;
}

void CParticle::CastingGatherLocSourceSegmentFunc(SLocalSource *pLSrc)
{
    if (pLSrc->respectCP.x != 0)
    {
        // this is first source
        if (!CheckFlag(PS_GENERATE_PARTICLES) || !pCarrier)
        {
            if (pLSrc->nLifeTime > 0)
                pLSrc->nLifeTime--;
            pLSrc->nPhase = dwCastGatherFade[locSources[0].nLifeTime];
        }
    }
    else
    {
        pLSrc->reserved += pLSrc->reserved2;
        float fpS = FP_CASTING_S_SRC_RND_STEP;
        pLSrc->reserved2.x = pLSrc->reserved2.x * 0.95f + random.GetFloat(-fpS, fpS) -
                             pLSrc->reserved.x * 0.0005f;
        pLSrc->reserved2.y = pLSrc->reserved2.y * 0.95f + random.GetFloat(-fpS, fpS) -
                             pLSrc->reserved.y * 0.0005f;
        pLSrc->reserved2.z = pLSrc->reserved2.z * 0.95f + random.GetFloat(-fpS, fpS) -
                             pLSrc->reserved.z * 0.0005f;
    }
}

// ------------------------------ LightningBlast ----------------------------------------------
BOOL CParticle::LightningBlastSingleCreate(SSingleParticle * pSPart, int)
{
	
	if (locSources.GetSize() == 0)
	{
		
		locSources.SetSize(10);
		for (int i = 0; i < locSources.GetSize(); i++)
		{
			CPlot3D vec = GetCenteredRandomRoundPoint(), newCP;
			if (i == 0)
				vec = CPlot3D(0, 0, 0);
			newCP = vec * fpSize * 0.3f + carrierCP;
			newCP.z = theWorld.GetSurfaceValue(newCP.x, newCP.y) + 0.3f;
			locSources[i].respectCP = newCP;
			locSources[i].reserved = newCP;
			vec.z = 0.5f;
			vec *= 1 / fabs(vec);
			locSources[i].vSpeed = vec * 0.5f;
			locSources[i].nPhase = 0;
			locSources[i].nLifeTime = 0;
			
		}
	}
	if (locSources[0].nPhase < locSources.GetSize() * 2)
	{
		pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0xFF);
		int nSrc = locSources[0].nPhase / 2;
		
		{
			pSPart->nPhase = 0;
			pSPart->nLifeTime = 10;
		}
		
		pSPart->nLifeTime = 10;
		pSPart->nPhaseLifeTime = locSources[0].nPhase & 1;
		pSPart->nSpecial = nSrc + 1;
		pSPart->coord = locSources[nSrc].respectCP;
		pSPart->coord.w = 0;
		pSPart->reserved2.x = 0.05f * 2; 
		locSources[0].nPhase++;
		if (pSPart->nPhaseLifeTime)
		{
			pSPart->reserved2.x = fpSize * 0.2f * random.GetFloat(0.5f, 1.4f);
			pSPart->nPhase = random.Get(12, 15);
			if (nSrc == 0)
				pSPart->reserved2.x = fpSize * 0.4f;
			pSPart->coord.w = pSPart->reserved2.x;
		}
		return TRUE;
	}
	pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0xFF);
	pSPart->nSpecial = 0;
	if (particles.GetCount() < 200 && locSources[0].nLifeTime < 5)
	{
		pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0x40);
		pSPart->nPhase = 8;
		int nSrc = random.Get(0, locSources.GetSize() - 1);
		float fpR = 1 - (random.GetFloat(0, 1) * random.GetFloat(0, 1));
		float fpAng = random.GetFloat(0, 6.28f);
		pSPart->vSpeed = GetRandomRoundPoint();
		pSPart->vSpeed.x = fpR * cos(fpAng);
		pSPart->vSpeed.y = fpR * sin(fpAng);
		pSPart->vSpeed *= fpSize * 0.3f;
		pSPart->vSpeed.z = 0;
		pSPart->coord.x = locSources[0].respectCP.x + pSPart->vSpeed.x;
		pSPart->coord.y = locSources[0].respectCP.y + pSPart->vSpeed.y;
		pSPart->coord.z = locSources[0].respectCP.z + (1 - fpR) * fpSize * 0.1f;
		pSPart->coord.w = 0;
		pSPart->nLifeTime = Float2Int(9 - 3 * fpR);
		pSPart->nPhaseLifeTime = 0;
		pSPart->reserved2.x = random.GetFloat(0.1f, 0.14f) * fpSize;
		pSPart->reserved2.x *= 2 - fpR;
		return TRUE;
	}
	int nSrc;
	
	{
		nSrc = random.Get(0, locSources.GetSize() - 1);
		
			
	}

	
	{
		pSPart->nPhase = 1;
		pSPart->reserved2.x = random.GetFloat(0.02f, 0.03f) * 3; 
	}
	
	pSPart->nLifeTime = 6;
	pSPart->nPhaseLifeTime = 1;
	float fpPart = random.GetFloat(0, 1);
	pSPart->coord = locSources[nSrc].respectCP;
	pSPart->coord.w = 0;
	pSPart->reserved = locSources[nSrc].respectCP * fpPart + locSources[nSrc].reserved * (1 - fpPart);
	return TRUE;
}

static DWORD dwLBAlpha[] = { 0,    0x50, 0xb0, 0xff, 0xff, 0xff, 0xff, 0xff,
                             0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
BOOL CParticle::LightningBlastSingleSegment(SSingleParticle *pSPart)
{
    if (--pSPart->nLifeTime < 0)
        return FALSE;
    if (pSPart->nSpecial)
    {
        if (pSPart->nPhaseLifeTime)
        {
            if (pSPart->coord.w == 0)
            {
                nMinTrys = 50;
                // pSPart->nPhase = random.Get(12,15);
                pSPart->coord.w = pSPart->reserved2
                                      .x; // fpSize * 0.2f * random.GetFloat(0.2f, 1.2f);
                return TRUE;
            }
            pSPart->coord.w = 0;
            return FALSE;
        }
        pSPart->coord = locSources[pSPart->nSpecial - 1].respectCP;
        pSPart->coord.w = pSPart->reserved2.x;
        return TRUE;
    }
    pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, dwLBAlpha[pSPart->nLifeTime] / 4);

    if (pSPart->nPhaseLifeTime)
    {
        pSPart->nPhase++;
        // if ( pSPart->nPhase == 12 )
        // pSPart->nPhase = 8;
        pSPart->coord = pSPart->reserved;
        pSPart->coord.w = pSPart->reserved2.x;
        pSPart->reserved2.x *= 0.9f;
    }
    else
    {
        pSPart->nPhase = (pSPart->nPhase & ~3) + ((pSPart->nPhase + 1) & 3);
        pSPart->coord += pSPart->vSpeed;
        pSPart->coord.w = pSPart->reserved2.x;
        pSPart->reserved2.x *= 0.9f; // 1.05f;//1.2f;
        pSPart->vSpeed *= 0.6f;
    }
    return TRUE;
}

void CParticle::LightningBlastLocSourceSegmentFunc(SLocalSource *pLSrc)
{
    nMinTrys = 50;
    pLSrc->nLifeTime++;
    pLSrc->reserved = pLSrc->respectCP;
    pLSrc->respectCP += pLSrc->vSpeed;
    pLSrc->vSpeed.z -= 0.075f;
    if (pLSrc->nLifeTime >= 10)
        ResetFlag(PS_GENERATE_PARTICLES);
}

// --------------------------------- ZoneExit ------------------------------------------------
BOOL CParticle::ZoneExitSingleCreate(SSingleParticle *pSPart, int)
{
    nMinTrys = (int)Min(40, Float2Int(fpPower * fpPower2));

    // int nSrc = random.Get( 0, locSources.GetSize() - 1 );
    if (ptWallDirection.x)
        pSPart->dwColor = RGBA_MAKE(0xFF, 0x60, 0x60, 0);
    else
        pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0);
    pSPart->nPhase = random.Get(0, 15);
    pSPart->nLifeTime = 15;
    pSPart->nPhaseLifeTime = 1;
    pSPart->nSpecial = 0;
    // float fpPart = random.GetFloat( 0, 1 );
    float fpX, r1 = random.GetFloat(-1, 1);
    float fpY, r2 = random.GetFloat(-1, 1);
    float fpSize = 1.9f - sqr(r1) - sqr(r2);
    fpX = carrierCP.x + r1 * fpPower * fpParam1 - r2 * fpPower2 * fpParam2;
    fpY = carrierCP.y + r1 * fpPower * fpParam2 + r2 * fpPower2 * fpParam1;
    float fpZ = theWorld.GetSurfaceValue(fpX, fpY) + 0.1f;
    pSPart->coord.x = fpX;
    pSPart->coord.y = fpY;
    pSPart->coord.z = fpZ;
    pSPart->coord.w = 0;
    pSPart->reserved.x = fpSize * 0.05f * random.GetFloat(0.7f, 1.3f);
    // pSPart->reserved = locSources[nSrc].respectCP * fpPart + locSources[nSrc].reserved * ( 1 -
    // fpPart ); pSPart->camera.x = random.GetFloat( fpSize * 0.02f, fpSize * 0.03f );
    return TRUE;
}

static DWORD dwZEAlpha[] = { 0,    0x50, 0xb0, 0xff, 0xff, 0xff, 0xff, 0xff,
                             0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x80, 0x40 };
/*static DWORD dwZEAlpha[] = { 0,   0x50,0xb0,0xff,   0xff,0xff,0xff,0xff,
                             0xff,0xff,0xff,0xff,   0xff,0xff,0x80,0 };*/
BOOL CParticle::ZoneExitSingleSegment(SSingleParticle *pSPart)
{
    if (static_cast<unsigned>(pSPart->nLifeTime) >= _countof(dwZEAlpha))
        return FALSE;
    pSPart->dwColor &= RGBA_MAKE(0xFF, 0xFF, 0xFF, 0);
    pSPart->dwColor |= RGBA_MAKE(0, 0, 0, dwZEAlpha[pSPart->nLifeTime]);
    if (--pSPart->nLifeTime < 0)
        return FALSE;
    pSPart->nPhase = ((pSPart->nPhase + 1) & 3) + (pSPart->nPhase & (~3));
    // pSPart->coord = pSPart->reserved;
    pSPart->coord.w = pSPart->reserved.x; // 0.1f;//pSPart->camera.x;
    pSPart->coord.z += 0.05f;
    pSPart->reserved2.x *= 0.9f;
    return TRUE;
}

void CParticle::ZoneExitLocSourceSegmentFunc(SLocalSource *)
{
}

// --------------------------------- Path ------------------------------------------------
BOOL CParticle::PathSingleCreate(SSingleParticle *pSPart, int nNumber)
{
#ifndef _A4
    CPlot3D ptPos;
    if (locSources.GetSize() == 0)
    {
        locSources.SetSize(1);
        locSources[0].reserved = CPlot3D(-1, -1, -1);
        if (fpPower == 0)
            locSources[0].nPhase = RGBA_MAKE(0xb0, 0xb0, 0xb0, 0);
        else if (fpPower == 1)
            locSources[0].nPhase = RGBA_MAKE(0x40, 0xFF, 0x40, 0);
        else if (fpPower == 2)
            locSources[0].nPhase = RGBA_MAKE(0xFF, 0x80, 0x28, 0);
    }
    if(nNumber<0||size_t(nNumber)>=pathMarkers.size())return FALSE;
    ptPos=pathMarkers[nNumber];
    if (locSources[0].reserved == ptPos)
        return FALSE;
    locSources[0].reserved = ptPos;
    // Navigation points follow the bottom; the marker follows the visible
    // water surface. Keep the physical route and renderer unchanged.
    ptPos.z = Max(ptPos.z,theWorld.GetSurfaceValue(ptPos.x,ptPos.y)) + 0.15f;
#else
    CPlot3D ptPos(-1, -1, -1);
#endif
    pSPart->coord = ptPos;
    pSPart->coord.w = 0.07f;
    pSPart->nPhase = 15;
    pSPart->nLifeTime = -10; // 6 + nNumber * 8 / ;
    pSPart->nPhaseLifeTime = nNumber;
    pSPart->dwColor = locSources[0].nPhase;
    return TRUE;
}

float fpPathBrightness[] = { 0, 0.15f, 0.3f, 0.45f, 0.6f, 0.75f, 0.9f, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
BOOL CParticle::PathSingleSegment(SSingleParticle *pSPart)
{
    if (pSPart->nLifeTime == -10)
    {
        // Creation attempts can outnumber particles when duplicate positions are rejected.
        pSPart->nLifeTime = static_cast<int>(Min<INT_PTR>(16, 7 + 8 * pSPart->nPhaseLifeTime / particles.GetCount()));
    }
    if (--pSPart->nLifeTime < 0)
        return FALSE;
    float fpPower = 1; //( 1 - sqr( pSPart->nPhaseLifeTime / float( nParticlesMax ) ) );
    fpPower *= fpPathBrightness[pSPart->nLifeTime];
    pSPart->dwColor = (pSPart->dwColor & 0xFFFFFF) + RGBA_MAKE(0, 0, 0, Float2Int(fpPower * 255));
    return TRUE;
}

void CParticle::PathLocSourceSegmentFunc(SLocalSource *)
{
    ResetFlag(PS_GENERATE_PARTICLES);
}

// --------------------------------- PathDest ------------------------------------------------
static float fpPathDestDx[4] = { 0.707f, -0.707f, -0.707f, 0.707f };
static float fpPathDestDy[4] = { 0.707f, -0.707f, 0.707f, -0.707f };
BOOL CParticle::PathDestSingleCreate(SSingleParticle *pSPart, int nNumber)
{
    float fpAngle, fpX, fpY, fpZ = 0;
    if (nNumber >= N_PATHDEST_ROUND_PARTS)
    {
        // cross
        int nPart = (nNumber - N_PATHDEST_ROUND_PARTS);
        int nSpring = nPart / N_PATHDEST_CROSS_PARTS;
        int nLeng = nPart & (N_PATHDEST_CROSS_PARTS - 1);
        float fpLeng = nLeng * (0.10f / (N_PATHDEST_CROSS_PARTS - 1));
        fpX = fpLeng * fpPathDestDx[nSpring];
        fpY = fpLeng * fpPathDestDy[nSpring];
    }
    else
    {
        // round
        fpAngle = nNumber * 6.29f / N_PATHDEST_ROUND_PARTS;
        fpX = 0.10f * sin(fpAngle);
        fpY = 0.10f * cos(fpAngle);
    }
    pSPart->coord = carrierCP;
    pSPart->coord.w = 0;
    pSPart->reserved = CPlot3D(fpX, fpY, 0);
    pSPart->nLifeTime = 6;
    pSPart->nPhase = 15;
    pSPart->dwColor = locSources[0].nPhase;
    pSPart->reserved2.x = 0.07f;
    pSPart->reserved2.y = 1.0f;
    return TRUE;
}

static DWORD dwPDAlpha[] = { 0, 0x40, 0x90, 0xff, 0xff, 0xff, 0xff, 0xff,
                             // 0,   0x20,0x50,0x90,   0xdf,0xff,0xff,0xff,
                             0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x80, 0x40 };
BOOL CParticle::PathDestSingleSegment(SSingleParticle *pSPart)
{
    if (--pSPart->nLifeTime < 0)
        return FALSE;
    pSPart->dwColor = (pSPart->dwColor & 0xFFFFFF) +
                      RGBA_MAKE(0, 0, 0, dwPDAlpha[pSPart->nLifeTime]);
    pSPart->coord += pSPart->reserved;
    pSPart->coord.z = theWorld.GetSurfaceValue(pSPart->coord.x, pSPart->coord.y) + 0.1f;
    float fpH = locSources[0].vSpeed.x -
                locSources[0].reserved.z *
                    ((pSPart->coord.x - carrierCP.x) * locSources[0].reserved.x +
                     (pSPart->coord.y - carrierCP.y) * locSources[0].reserved.y) +
                0.1f;
    pSPart->coord.z = Max(pSPart->coord.z, fpH);
    pSPart->coord.w = pSPart->reserved2.x; // 0.05f;
    pSPart->reserved *= 1.05f;
    pSPart->reserved2.x *= pSPart->reserved2.y;
    return TRUE;
}

void CParticle::PathDestLocSourceSegmentFunc(SLocalSource *pSLoc)
{
    if (pSLoc->nLifeTime == 0)
    {
        theWorld.GetBridgeSurface(carrierCP.x, carrierCP.y, pSLoc->reserved, pSLoc->vSpeed.x);
        if (pSLoc->reserved.z != 0)
            pSLoc->reserved.z = 1 / pSLoc->reserved.z;
    }
    pSLoc->nLifeTime++;
    if (pSLoc->nLifeTime > 1)
        ResetFlag(PS_GENERATE_PARTICLES);
}

// --------------------------------- Moshka ------------------------------------------------
const DWORD dwMoshkaColors[4] = { 0xFF6666, 0x66FF66, 0x6666FF, 0xaaaaaa };
const int nMoshkaColors[12][3] = {
    //{250,255,155},
    { 251, 254, 143 }, { 252, 227, 149 }, { 252, 201, 154 }, { 255, 176, 154 }, { 255, 155, 163 },
    { 252, 152, 176 }, { 250, 150, 195 }, { 247, 149, 212 }, { 243, 147, 237 }, { 239, 144, 249 },
    { 239, 144, 249 }, { 239, 144, 249 }
    /*
            {255,180, 50},
            {246,169, 62},
            {236,157, 75},
            {217,135,101},
            {198,112,125},
            {178, 90,152},
            {159, 67,177},
            {139, 45,203},
            {119, 22,227},
            {100,  0,255}*/
    /*
            {255,255,255},
            {255,255,174},
            {255,254, 86},
            {255,227, 47},
            {255,178, 47},
            {255,140, 47},
            {255, 97, 47},
            {255, 37, 47},
            {255, 10, 47},
            {255,  1,144},
            {255,  0,249},
            {193,  0,255},
            {193,  0,255},*/
};
static float fpMoshkaSize[12];
BOOL CParticle::MoshkaSingleCreate(SSingleParticle *pSPart, int nNumber)
{
    if (particles.GetCount() < N_MOSHKA_SOURCES)
    {
        if (nNumber == 0)
        {
            float fpSize = 1;
            for (int k = 0; k < 12; k++)
            {
                fpMoshkaSize[k] = fpSize;
                fpSize *= 0.93f;
            }
        }
        int nKind = random.Get(0, 3);
        pSPart->nSpecial = particles.GetCount();
        pSPart->dwColor = RGBA_MAKE(
            0xFF, 0xFF, 0xFF, 0); // dwMoshkaColors[nKind]; //RGBA_MAKE( 0xFF, 0xFF, 0xFF, 0 );
        pSPart->nLifeTime = 15;
        pSPart->nPhase = nKind; //*4; //12;
        pSPart->coord = carrierCP;
        pSPart->coord.w = 0;
        return TRUE;
    }
    int nFather = random.Get(0, N_MOSHKA_SOURCES - 1);
    SLocalSource &src = locSources[nFather];
    // Set start values of color and lifetime
    // random color
    pSPart->dwColor = RGBA_MAKE(nMoshkaColors[src.nPhase][0], nMoshkaColors[src.nPhase][1],
                                nMoshkaColors[src.nPhase][2], 0);
    pSPart->nLifeTime = Min(5, src.nLifeTime + 1);
    pSPart->nPhase = 4; // nKind*4; //12;
    pSPart->nSpecial = -1;
    // Generate start coordinates
    pSPart->coord = src.respectCP +
                    random.GetFloat(-1, 0) * locSources[nFather].vSpeed; // carrierCP;
    pSPart->coord.w = fpMoshkaSize[src.nPhase] * 0.1f * fpSize;

    return TRUE;
}

static const DWORD dwMoshkaAlpha[16] = { 0x00, 0x40, 0x70, 0xff, 0xff, 0xff, 0xff, 0xff,
                                         0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
BOOL CParticle::MoshkaSingleSegment(SSingleParticle *pSPart)
{
    if (pSPart->nSpecial < 0)
    {
        // Update lifetime
        if (--(pSPart->nLifeTime) <= 0)
            return FALSE;
        pSPart->nPhase = pSPart->nPhase + 1; // ) & 3 ) + ( pSPart->nPhase & ~3 );
        // Update coordinates and size
        pSPart->dwColor = (pSPart->dwColor & 0xFFFFFF) +
                          RGBA_MAKE(0, 0, 0, dwMoshkaAlpha[pSPart->nLifeTime]);
        return TRUE;
    }
    SLocalSource &src = locSources[pSPart->nSpecial];
    if (src.nLifeTime == -13)
        return FALSE;
    pSPart->nPhase = (pSPart->nPhase + 1) & 3; // ) + ( pSPart->nPhase & ~3 );
    pSPart->coord = src.respectCP;
    pSPart->coord.w = fpMoshkaSize[src.nPhase] * fpSize * 0.15f;
    int nColor = Min(9, pSPart->nLifeTime);
    pSPart->dwColor = RGBA_MAKE(nMoshkaColors[src.nPhase][0], nMoshkaColors[src.nPhase][1],
                                nMoshkaColors[src.nPhase][2], dwMoshkaAlpha[nColor]);
    // prevent interpolation
    if (src.vSpeed.x == 0 && src.vSpeed.y == 0 && src.vSpeed.z == 0)
    {
        // pSPart->dwColor = RGBA_MAKE( 0xFF, 0xFF, 0xFF, 0xFF );
        // pSPart->coord.w = 0;
        pSPart->prevcoord = pSPart->coord;
        pSPart->dwPrevColor = pSPart->dwColor;
        pSPart->coord.w = fpSize * 0.15f;
    }
    return TRUE;
}

void CParticle::MoshkaLocSourceSegmentFunc(SLocalSource *pSLoc)
{
    if (--pSLoc->nLifeTime < 0)
    {
        if (!GetCarrier())
        {
            pSLoc->nLifeTime = -13;
            return;
        }
        pSLoc->nPhase = random.Get(0, 2);
        pSLoc->respectCP = carrierCP;
        pSLoc->respectCP.x += random.GetFloat(-fpSize * 0.2f, fpSize * 0.2f);
        pSLoc->respectCP.y += random.GetFloat(-fpSize * 0.2f, fpSize * 0.2f);
        pSLoc->respectCP.z += random.GetFloat(-fpSize * 0.2f, fpSize * 0.2f);
        pSLoc->reserved = GetRandomBallPoint() * fpSize * 0.2f;
        pSLoc->nLifeTime = random.Get(7, 9);
        pSLoc->vSpeed = CPlot3D(0, 0, 0);
        return;
    }
    pSLoc->nPhase++;
    if (random.Get(0, 5) == 0)
    {
        pSLoc->reserved += GetRandomBallPoint() * fpSize * 0.05f;
    }
    pSLoc->respectCP += pSLoc->reserved;
    pSLoc->vSpeed = pSLoc->reserved;
}

// ------------------------------- PortalStar ------------------------------------------------
BOOL CParticle::PortalStarSingleCreate(SSingleParticle *pSPart, int)
{
    pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0);
    pSPart->nLifeTime = 15;
    pSPart->nPhase = 0;
    pSPart->coord = carrierCP;
    pSPart->coord.w = 0;
    return TRUE;
}

BOOL CParticle::PortalStarSingleSegment(SSingleParticle *pSPart)
{
    if (!CheckFlag(PS_GENERATE_PARTICLES))
        return FALSE;
    pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0xFF);
    pSPart->coord.w = random.GetFloat(0.8f, 1.2f) * fpSize;
    return TRUE;
}

void CParticle::PortalStarLocSourceSegmentFunc(SLocalSource *)
{
}

// ------------------------------- Cylinder ------------------------------------------------
const int N_CYLINDER_TIME = 18;
const float FP_CYLINDER_ROTS = 0.3f; // 2 оборота в секунду
static DWORD dwCylinderAlpha[] = {
    0x00, 0x20, 0x40, 0x80, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};
BOOL CParticle::CylinderSingleCreate(SSingleParticle *pSPart, int)
{
    pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0);
    int nParticlesCount = particles.GetCount();
    if (nParticlesCount < locSources.GetSize())
    {
        if (nParticlesCount == 0)
        {
            float fpRadius, fpDCos, fpDSin;
            CPlot3D ptStart;
            GetObjectBV(ptStart, fpRadius);
            float fpDAngle = 2 * FP_PI / 15 * FP_CYLINDER_ROTS;
            if (GetTypeID() == PTID_CYLINDER2)
                fpDAngle = -fpDAngle;
            fpDCos = cos(fpDAngle);
            fpDSin = sin(fpDAngle);
            for (int i = 0; i < locSources.GetSize(); i++)
            {
                float fpCircle = 0.7f * fpRadius;
                locSources[i].respectCP.x = fpCircle * cos(i * 2 * FP_PI / locSources.GetSize());
                locSources[i].respectCP.y = fpCircle * sin(i * 2 * FP_PI / locSources.GetSize());
                locSources[i].respectCP.z = 0.2f * fpRadius;
                locSources[i].vSpeed = CPlot3D(0, 0, 0);
                locSources[i].reserved.x = fpDCos;
                locSources[i].reserved.y = fpDSin;
                locSources[i].nLifeTime = N_CYLINDER_TIME;
                locSources[i].reserved2.z = fpRadius * (2.0f / locSources[i].nLifeTime) * 0.04f;
                locSources[i].reserved2.x = 1 / fpCircle;
                locSources[i].reserved2.y = fpCircle;
            }
        }
        // pSPart->nLifeTime = 15;
        pSPart->nPhase = 0;
        pSPart->nSpecial = nParticlesCount;
        pSPart->coord = carrierCP + locSources[nParticlesCount].respectCP;
        pSPart->coord.w = 0;
        return TRUE;
    }
    int nSrc = random.Get(0, locSources.GetSize() - 1);
    SLocalSource &src = locSources[nSrc];
    pSPart->nLifeTime = Min(14, src.nLifeTime + 6);
    if (pSPart->nLifeTime < 0)
        return FALSE;
    if (src.vSpeed.x == 0 && src.vSpeed.y == 0)
        return FALSE;
    pSPart->nPhase = 1;
    pSPart->nSpecial = -1;
    pSPart->reserved = src.respectCP + src.vSpeed * random.GetFloat(-1, 0);
    pSPart->reserved2 = src.vSpeed;
    pSPart->coord = carrierCP + pSPart->reserved;
    pSPart->coord.w = 0.1f; // 0;
    return TRUE;
}

BOOL CParticle::CylinderSingleSegment(SSingleParticle *pSPart)
{
    if (pSPart->nSpecial >= 0)
    {
        if (!CheckFlag(PS_GENERATE_PARTICLES))
        {
            pSPart->coord.w = 0;
            pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0);
            return FALSE;
        }
        SLocalSource &src = locSources[pSPart->nSpecial];
        pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, dwCylinderAlpha[src.nLifeTime]);
        pSPart->coord = carrierCP + src.respectCP;
        pSPart->coord.w = 0.3f;
        return TRUE;
    }
    pSPart->nPhase++;
    pSPart->nLifeTime--;
    if (pSPart->nLifeTime <= 0) // pSPart->nPhase == 15 )
        return FALSE;
    pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, dwCylinderAlpha[pSPart->nLifeTime]); // 0xFF );

    pSPart->reserved += pSPart->reserved2;
    float fpKoef = locSources[0].reserved2.y /
                   sqrt(sqr(pSPart->reserved.x) + sqr(pSPart->reserved.y));
    pSPart->reserved.x *= fpKoef;
    pSPart->reserved.y *= fpKoef;
    CPlot3D ptNewDir;
    float fpMn = fabs(pSPart->reserved2) * locSources[0].reserved2.x;
    pSPart->reserved2.x = 0.6f * fpMn * pSPart->reserved.y;
    pSPart->reserved2.y = 0.6f * fpMn * (-pSPart->reserved.x);
    pSPart->reserved2.z = 0.6f * pSPart->reserved2.z;
    float fpW = pSPart->coord.w;
    pSPart->coord = carrierCP + pSPart->reserved;
    pSPart->coord.w = fpW * 0.9f; // 0.1f;
    return TRUE;
}

void CParticle::CylinderLocSourceSegmentFunc(SLocalSource *pSLoc)
{
    if (pSLoc == &locSources[0])
    {
        if (pSLoc->nLifeTime % 2 != 0)
            nMinTrys--;
        nMinTrys--;
    }
    if (pSLoc->nLifeTime-- <= 0)
    {
        ResetFlag(PS_GENERATE_PARTICLES);
        return;
    }
    CPlot3D ptNew;
    ptNew.x = pSLoc->respectCP.x * pSLoc->reserved.x + pSLoc->respectCP.y * pSLoc->reserved.y;
    ptNew.y = -pSLoc->respectCP.x * pSLoc->reserved.y + pSLoc->respectCP.y * pSLoc->reserved.x;
    ptNew.z = pSLoc->respectCP.z + pSLoc->reserved2.z;
    pSLoc->vSpeed = ptNew - pSLoc->respectCP;
    pSLoc->respectCP = ptNew;
    pSLoc->reserved2.z *= 1.25f;

    float fpDAngle = -2 * FP_PI / 15 * 0.1f;
    float fpDCos, fpDSin;
    if (GetTypeID() == PTID_CYLINDER2)
        fpDAngle = -fpDAngle;
    fpDCos = cos(fpDAngle);
    fpDSin = sin(fpDAngle);
    ptNew.x = pSLoc->reserved.x * fpDCos + pSLoc->reserved.y * fpDSin;
    ptNew.y = -pSLoc->reserved.x * fpDSin + pSLoc->reserved.y * fpDCos;
    pSLoc->reserved.x = ptNew.x;
    pSLoc->reserved.y = ptNew.y;
}

// --------------------------------- Portal --------------------------------------------------
const float FP_PORTAL_RADIUS = 2.5f;
const float FP_PORTAL_H = 8;
BOOL CParticle::PortalSingleCreate(SSingleParticle *pSPart, int)
{
    if (particles.GetCount() == 0)
    {
        pSPart->coord = carrierCP;
        pSPart->coord.z += FP_PORTAL_H * 0.8f;
        pSPart->coord.w = 0;
        pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0x60); // FF );
        pSPart->nSpecial = 1;
        pSPart->nPhase = 15;
        return TRUE;
    }
    pSPart->nSpecial = 0;
    pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0x40); // 0xFF );
    pSPart->nLifeTime = nLifeSpan;
    pSPart->nPhaseLifeTime = nLifeOfPhase;

    float fpX, fpY, fpR = FP_PORTAL_RADIUS, fpR2 = sqr(fpR), fpXYR;

    fpX = random.GetFloat(0, 1);
    fpX = (fpX * fpX + fpX) / 2;
    float fpA = random.GetFloat(0, 2 * 3.1416f);
    fpXYR = fpX;
    fpX = fpR * fpXYR * sin(fpA);
    fpY = fpR * fpXYR * cos(fpA);

    pSPart->coord = carrierCP + CPlot3D(fpX, fpY, 0);
    // pSPart->coord.z += random.GetFloat( -0.4f, -0.2f ) * fpR;
    pSPart->coord.w = 0;
    pSPart->reserved2.z = 0.3f * (2 - fpXYR);
    pSPart->reserved2.x = fpX;
    pSPart->reserved2.y = fpY;
    fpA = fpR / FP_PORTAL_RADIUS * 0.1f; // * fpSize;
    if (random.GetFloat(0, 1) > fpXYR)
        pSPart->nPhase = random.Get(4, 14);
    else
    {
        pSPart->nPhase = random.Get(0, 3);
        fpA = -fpA;
    }
    pSPart->reserved.x = cos(fpA);
    pSPart->reserved.y = sin(fpA);
    pSPart->reserved.z = fpR * 3 * 0.02f * (1 - fpXYR + 3) / 4;
    pSPart->coord.z += random.GetFloat(-0.5f, 0.5f) * pSPart->reserved.z;
    return TRUE;
}

BOOL CParticle::PortalSingleSegment(SSingleParticle *pSPart)
{
    if (pSPart->nSpecial)
    {
        if (!CheckFlag(PS_GENERATE_PARTICLES))
            return FALSE;
        // pSPart->nPhase = ( pSPart->nPhase + 1 ) & 3;
        if (fpSize >= 1)
            pSPart->coord.w = random.GetFloat(0.8f, 1.2f) * 1.5f; // * fpSize;
        else
            pSPart->coord.w = 0;
        return TRUE;
    }
    if (pSPart->nLifeTime == 0)
        return FALSE;
    // pSPart->nLifeTime--;
    // if ( --( pSPart->nLifeTime ) == 0 )
    // return FALSE;
    // Update phase
    // pSPart->coord.z -= pSPart->reserved.z;
    float fpX, fpY;
    fpX = pSPart->reserved.x * pSPart->reserved2.x + pSPart->reserved.y * pSPart->reserved2.y;
    fpY = -pSPart->reserved.y * pSPart->reserved2.x + pSPart->reserved.x * pSPart->reserved2.y;
    pSPart->reserved2.x = fpX;
    pSPart->reserved2.y = fpY;
    pSPart->coord.x = carrierCP.x + fpX;
    pSPart->coord.y = carrierCP.y + fpY;
    pSPart->coord.z += pSPart->reserved.z * fpSize;
    pSPart->reserved.z *= 1.03f;
    if (--(pSPart->nPhaseLifeTime) == 0)
    {
        pSPart->nPhase++;
        if (pSPart->nPhase == 4)
            pSPart->nPhase = 0;
        if (pSPart->nPhase == 15)
            pSPart->nPhase = 4;
        pSPart->nPhaseLifeTime = nLifeOfPhase;
    }
    float fpH = pSPart->coord.z - carrierCP.z;
    if (fpH > FP_PORTAL_H)
    {
        pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0);
        pSPart->coord.w = 0;
        pSPart->nLifeTime = 0;
        return TRUE;
    }
    pSPart->coord.w = pSPart->reserved2.z;
    int nAlpha = (55 + Float2Int((FP_PORTAL_H - fpH) * (200 / FP_PORTAL_H))) >> 2;
    nAlpha = Min(nAlpha, 64); // 255 );
    pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, nAlpha);
    return TRUE;
}

void CParticle::PortalLocSourceSegmentFunc(SLocalSource *)
{
    nMinTrys = 10 * fpSize; // * sqrt( fpSize );
}

// ---------------------------------- Star ---------------------------------------------------
BOOL CParticle::StarSingleCreate(SSingleParticle *pSPart, int)
{
    ResetFlag(PS_GENERATE_PARTICLES);
    pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0);
    pSPart->nLifeTime = 3;
    pSPart->nPhase = nLifeSpan;
    pSPart->coord = carrierCP;
    pSPart->coord.w = 0;
    pSPart->reserved.x = fpSize;
    return TRUE;
}

BOOL CParticle::StarSingleSegment(SSingleParticle *pSPart)
{
    if (--pSPart->nLifeTime <= 0)
    {
        pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0);
        pSPart->coord.w = 0;
        return FALSE;
    }
    pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0xFF);
    pSPart->coord.w = pSPart->reserved.x;
    pSPart->reserved.x *= 0.9f;
    return TRUE;
}

void CParticle::StarLocSourceSegmentFunc(SLocalSource *pLSrc)
{
}

// ---------------------------------- Sparks --------------------------------------------------
BOOL CParticle::SparksSingleCreate(SSingleParticle *pSPart, int)
{
    if (particles.GetCount() == 0)
    {
        pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0xFF);
        pSPart->coord = carrierCP;
        pSPart->coord.w = 0;
        pSPart->nPhase = 0;
        pSPart->nSpecial = 1;
        return TRUE;
    }
    ResetFlag(PS_GENERATE_PARTICLES);
    pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0xFF);
    pSPart->vSpeed = GetRandomSpherePoint() * (sqr(random.GetFloat(0.8f, 1.4f)) * fpSize * 0.2f);
    pSPart->coord = carrierCP + pSPart->vSpeed * random.GetFloat(0, 1);
    pSPart->coord.w = 0.2f * fpSize * random.GetFloat(0.8f, 1.2f);
    pSPart->nPhase = random.Get(1, 3);
    pSPart->nLifeTime = 6;
    pSPart->nSpecial = 0;
    return TRUE;
}

static DWORD dwSparksAlpha[] = {
    0x00, 0x40, 0x80, 0xff, 0xff, 0xff, 0xff, 0xff,
};
BOOL CParticle::SparksSingleSegment(SSingleParticle *pSPart)
{
    if (pSPart->nSpecial)
    {
        if (pSPart->coord.w == 0)
        {
            pSPart->coord.w = 0.55f * fpSize * random.GetFloat(0.8f, 1.2f);
            return TRUE;
        }
        else
        {
            pSPart->coord.w = 0;
            return FALSE;
        }
    }
    if (--pSPart->nLifeTime < 0)
        return FALSE;
    pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, dwSparksAlpha[pSPart->nLifeTime]);
    float fpW = pSPart->coord.w;
    pSPart->coord += pSPart->vSpeed;
    pSPart->coord.w = fpW * 0.9f;
    pSPart->vSpeed *= 0.6f;
    pSPart->nPhase++;
    return TRUE;
}

void CParticle::SparksLocSourceSegmentFunc(SLocalSource *)
{
}

// ------------------------------- VisionStar -------------------------------------------------
static void CalcStarPos(CPlot3D &ptRes, float fpAngle)
{
    ptRes.x = cos(fpAngle);
    ptRes.y = sin(fpAngle);
    ptRes.z = 0;
}
BOOL CParticle::VStarSingleCreate(SSingleParticle *pSPart, int)
{
    if (particles.GetCount() > 4 || (particles.GetCount() > 0 && nLifeSpan != 1))
        return FALSE;
    pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0);
    pSPart->nPhase = nLifeSpan;
    pSPart->coord = carrierCP;
    pSPart->coord.w = 0;
    if (nLifeSpan == 1)
    {
        switch (particles.GetCount())
        {
        case 0:
            CalcStarPos(pSPart->reserved, 0);
            break;
        case 1:
            CalcStarPos(pSPart->reserved, 1 * FP_2PI / 5);
            break;
        case 2:
            CalcStarPos(pSPart->reserved, 2 * FP_2PI / 5);
            break;
        case 3:
            CalcStarPos(pSPart->reserved, 3 * FP_2PI / 5);
            break;
        case 4:
            CalcStarPos(pSPart->reserved, 4 * FP_2PI / 5);
            break;
        }
        pSPart->reserved *= 0.5f;
        pSPart->coord += pSPart->reserved;
    }
    return TRUE;
}

BOOL CParticle::VStarSingleSegment(SSingleParticle *pSPart)
{
    float fpPSize = 0.3f;
    pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0xFF);
    float fpW = pSPart->coord.w;
    pSPart->coord = carrierCP;
    if (nLifeSpan == 1)
    {
        float fpX, fpY;
        const float fpDAngle = 0.07f;
        fpX = cos(fpDAngle) * pSPart->reserved.x + sin(fpDAngle) * pSPart->reserved.y;
        fpY = -sin(fpDAngle) * pSPart->reserved.x + cos(fpDAngle) * pSPart->reserved.y;
        pSPart->reserved.x = fpX;
        pSPart->reserved.y = fpY;
        pSPart->coord += pSPart->reserved;
        fpPSize = 0.17f; // pSPart->nPhase = (( pSPart->nPhase + 1 ) & 3) + 12;
    }
    if (GetTypeID() == PTID_VISIONSTAR4)
        pSPart->coord.z += 0.7f;
    pSPart->coord.w = fpW * 0.95f + (fpSize * fpPSize * random.GetFloat(0.1f, 1.9f)) * 0.05f;
    if (!CheckFlag(PS_GENERATE_PARTICLES) || !GetCarrier())
    {
        pSPart->coord.w *= 0.95f;
        fpSize = 0;
        if (pSPart->coord.w < 0.03f)
        {
            pSPart->coord.w = 0;
            pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0);
            return FALSE;
        }
    }
    return TRUE;
}

void CParticle::VStarLocSourceSegmentFunc(SLocalSource *pLSrc)
{
}

// ------------------------------ Regeneration -----------------------------------------------
BOOL CParticle::RegenerationSingleCreate(SSingleParticle * pSPart, int)
{
	if (particles.GetCount() > 1)
		return FALSE;
	pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0);
	pSPart->nPhase = 3;
	pSPart->coord = carrierCP;
	pSPart->coord.w = 0;
	pSPart->reserved.x = particles.GetCount() ? 1.0f : -1.0f;
	return TRUE;
}

BOOL CParticle::RegenerationSingleSegment(SSingleParticle *pSPart)
{
    if (!CheckFlag(PS_GENERATE_PARTICLES) || !GetCarrier())
    {
        fpSize *= 0.75f;
        if (fpSize < 0.02f)
        {
            pSPart->coord.w = 0;
            pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0);
            return FALSE;
        }
    }
    pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0xFF);
    CPlot3D ptWishPos;
    ptWishPos = carrierCP;
    CObject3D *pCarrier = GetCarrier();
    if (pCarrier)
        ptWishPos += pCarrier->GetXAxis() * (pSPart->reserved.x * pCarrier->bv.fpRadius * 0.65f);
    pSPart->coord.w = fpSize * 0.25f * random.GetFloat(0.8f, 1.2f);
    pSPart->coord.x = pSPart->coord.x * 0.7f + ptWishPos.x * 0.3f;
    pSPart->coord.y = pSPart->coord.y * 0.7f + ptWishPos.y * 0.3f;
    pSPart->coord.z = pSPart->coord.z * 0.7f + ptWishPos.z * 0.3f + fpSize * 0.4f;
    return TRUE;
}

void CParticle::RegenerationLocSourceSegmentFunc(SLocalSource *pLSrc)
{
}

// --------------------------------- Silence -------------------------------------------------
BOOL CParticle::SilenceSingleCreate(SSingleParticle *pSPart, int)
{
    if (particles.GetCount() > 1)
    {
        int nPos = random.Get(0, 1);
        CPlot3D ptPos;
        POSITION pos = particles.GetHeadPosition();
        if (nPos)
            particles.GetNext(pos);
        SSingleParticle *pSrc = particles.GetAt(pos);
        float fpAlpha = random.GetFloat(0, 1);
        ptPos.x = pSrc->coord.x - fpAlpha * pSrc->vSpeed.x;
        ptPos.y = pSrc->coord.y - fpAlpha * pSrc->vSpeed.y;
        ptPos.z = pSrc->coord.z - fpAlpha * pSrc->vSpeed.z;
        ptPos += GetRandomBallPoint() * pSrc->coord.w * 0.8f;
        pSPart->coord = ptPos;
        pSPart->coord.w = 0;
        pSPart->reserved.x = fpSize * 0.08f * random.GetFloat(0.8f, 1.2f);
        pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0xFF);
        pSPart->nPhase = 6;
        pSPart->nSpecial = 0;
        pSPart->nLifeTime = 7;
        return TRUE;
    }
    pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0);
    pSPart->nPhase = 6;
    pSPart->coord = carrierCP;
    pSPart->coord.w = 0;
    pSPart->reserved.x = particles.GetCount() ? 0 : 3.1416f;
    pSPart->reserved.y = 0.1f;
    pSPart->reserved2.x = pSPart->reserved.x;
    pSPart->reserved2.y = 0.12f;
    pSPart->reserved2.z = 1;
    pSPart->nSpecial = 1;
    // pSPart->reserved.x = particles.GetCount() ? 1 : -1;
    return TRUE;
}

BOOL CParticle::SilenceSingleSegment(SSingleParticle *pSPart)
{
    if (!CheckFlag(PS_GENERATE_PARTICLES) || !GetCarrier())
    {
        if (pSPart->nSpecial)
            fpSize *= 0.85f;
        if (fpSize < 0.02f)
        {
            pSPart->coord.w = 0;
            pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0);
            return FALSE;
        }
    }
    if (pSPart->nSpecial)
    {
        float fpDist = 1;
        CObject3D *pCarrier = GetCarrier();
        if (pCarrier)
        {
            fpDist = pCarrier->bv.fpRadius * 0.5f;
            pSPart->reserved2.z = fpDist;
        }
        else
            fpDist = pSPart->reserved2.z;
        pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0xFF);
        CPlot3D ptWishPos;
        ptWishPos.x = cos(pSPart->reserved.x);
        ptWishPos.y = sin(pSPart->reserved.x);
        ptWishPos.z = sin(pSPart->reserved2.x) * 0.5f;
        ptWishPos *= fpDist / fabs(ptWishPos);
        ptWishPos += carrierCP;
        ptWishPos.z += fpDist * 0.5f;
        pSPart->vSpeed.x = ptWishPos.x - pSPart->coord.x;
        pSPart->vSpeed.y = ptWishPos.y - pSPart->coord.y;
        pSPart->vSpeed.z = ptWishPos.z - pSPart->coord.z;
        pSPart->coord = ptWishPos;
        pSPart->reserved.x += pSPart->reserved.y;
        pSPart->reserved2.x += pSPart->reserved2.y;
        pSPart->coord.w = fpSize * 0.22f * random.GetFloat(0.9f, 1.1f);
    }
    else
    {
        if (--pSPart->nLifeTime <= 0)
        {
            pSPart->coord.w = 0;
            return FALSE;
        }
        pSPart->coord.w = pSPart->reserved.x;
        pSPart->reserved.x *= 0.8f;
    }
    return TRUE;
}

void CParticle::SilenceLocSourceSegmentFunc(SLocalSource *pLSrc)
{
}

// ----------------------------- FeebleMind ------------------------------------------------
BOOL CParticle::FeebleMindSingleCreate(SSingleParticle *pSPart, int)
{
    nMinTrys = Float2Int(fpSize * 2);
    pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0);
    pSPart->nPhase = random.Get(0, 15);
    pSPart->coord = carrierCP;
    pSPart->coord.w = 0;
    pSPart->nLifeTime = 20;
    float fpAngle = random.GetFloat(0, 2 * FP_PI);

    pSPart->reserved.x = cos(fpAngle) * 0.05f;
    pSPart->reserved.y = sin(fpAngle) * 0.05f;
    pSPart->reserved.z = 0;
    pSPart->coord += pSPart->reserved;
    // pSPart->camera.x = 0;
    // pSPart->camera.y = 0.1f;
    pSPart->reserved2.z = 0.035f * fpSize * random.GetFloat(0.9f, 1.1f);
    return TRUE;
}

static DWORD dwFeebleMindAlpha[] = {
    0x00, 0x20, 0x40, 0x80, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};
BOOL CParticle::FeebleMindSingleSegment(SSingleParticle *pSPart)
{
    if (--pSPart->nLifeTime < 0)
        return FALSE;
    pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, dwFeebleMindAlpha[pSPart->nLifeTime]);
    pSPart->coord -= pSPart->reserved;
    float fpX, fpY;
    const float fpDAngle = 0.07f;
    fpX = cos(fpDAngle) * pSPart->reserved.x + sin(fpDAngle) * pSPart->reserved.y;
    fpY = -sin(fpDAngle) * pSPart->reserved.x + cos(fpDAngle) * pSPart->reserved.y;
    pSPart->reserved.x = fpX * 1.10f; // pSPart->camera.x;
    pSPart->reserved.y = fpY * 1.10f; // pSPart->camera.x;
    pSPart->coord += pSPart->reserved;
    pSPart->coord.z += 0.05f;
    pSPart->coord.w = pSPart->reserved2.z;
    // pSPart->camera.x += pSPart->camera.y;
    // pSPart->camera.y += 0.03f;
    pSPart->reserved2.z *= 1.04f;
    pSPart->nPhase = (pSPart->nPhase + 1) & 15;
    return TRUE;
}

void CParticle::FeebleMindLocSourceSegmentFunc(SLocalSource *pLSrc)
{
}

// ------------------------------- FeetCloud -----------------------------------------------
BOOL CParticle::FeetCloudSingleCreate(SSingleParticle *pSPart, int)
{
    nMinTrys = Float2Int(fpSize * 8);
    pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0);
    pSPart->nPhase = random.Get(0, 15);
    pSPart->nSpecial = random.Get(0, 1);
    SLocalSource &src = locSources[pSPart->nSpecial];
    if (src.nPhase >= 0)
        pSPart->coord = src.respectCP;
    else
        pSPart->coord = carrierCP;
    pSPart->coord.w = 0;
    pSPart->vSpeed = GetRandomRoundPoint() * 0.1f * src.reserved.x * fpSize;
    pSPart->reserved.x = fpSize * 0.4f * src.reserved.x;
    pSPart->reserved.y = 1.0f;
    pSPart->reserved2 = CPlot3D(0, 0, 0);
    pSPart->coord.z += pSPart->reserved.x * 1.2f;
    pSPart->nLifeTime = 10;
    return TRUE;
}

static DWORD dwFeetCloudAlpha[] = {
    0x00, 0x20, 0x40, 0x80, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};
static DWORD dwFeetCloudAlpha1[] = {
    0x00, 0x10, 0x20, 0x40, 0x60, 0x80, 0xc0, 0xff,
};
BOOL CParticle::FeetCloudSingleSegment(SSingleParticle *pSPart)
{
    if (--pSPart->nLifeTime < 0)
        return FALSE;
    if (!CheckFlag(PS_GENERATE_PARTICLES) || !GetCarrier())
    {
        pSPart->nLifeTime = Min(8, pSPart->nLifeTime);
        pSPart->dwColor = nLifeSpan + RGBA_MAKE(0, 0, 0, dwFeetCloudAlpha[pSPart->nLifeTime]);
    }
    else
        pSPart->dwColor = nLifeSpan + RGBA_MAKE(0, 0, 0, dwFeetCloudAlpha[pSPart->nLifeTime]);
    pSPart->reserved2 += pSPart->vSpeed;
    CPlot3D ptSrc;
    ptSrc.x = pSPart->coord.x;
    ptSrc.y = pSPart->coord.y;
    ptSrc.z = pSPart->coord.z;
    pSPart->coord = ptSrc * (1 - pSPart->reserved.y) +
                    (locSources[pSPart->nSpecial].respectCP + pSPart->reserved2) *
                        pSPart->reserved.y;
    pSPart->coord.w = pSPart->reserved.x;
    pSPart->reserved.x *= 0.8f;
    pSPart->reserved.y *= 0.98f;
    pSPart->vSpeed *= 0.9f;
    return TRUE;
}

void CParticle::FeetCloudLocSourceSegmentFunc(SLocalSource * pLSrc)
{
#ifndef _A4
	if (GetCarrier() && GetCarrier()->WhoAmI() == HPC_UNIT)
	{
		CUnit* pUnit = (CUnit*)GetCarrier();
		if (pLSrc->nLifeTime)
			pLSrc->respectCP = GetBodyPartCP(pUnit, "ll3");
		else
			pLSrc->respectCP = GetBodyPartCP(pUnit, "rl3");
	}
	else
#endif
		pLSrc->respectCP = carrierCP;
	CPlot3D ptTemp;
	GetObjectBV(ptTemp, pLSrc->reserved.x);
	pLSrc->nPhase = 0;
}

// ------------------------------- BallStars -----------------------------------------------
BOOL CParticle::BallStarsSingleCreate(SSingleParticle *pSPart, int)
{
    CPlot3D ptCenter;
    float fpRadius;
    GetObjectBV(ptCenter, fpRadius);
    pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0);
    pSPart->nPhase = 3;
    pSPart->coord = carrierCP + GetRandomBallPoint() * (fpRadius * 0.8f); // carrierCP;
    pSPart->coord.w = 0;
    pSPart->reserved.x = random.GetFloat(0.7f, 1.3f);
    pSPart->nLifeTime = 20;
#ifndef _A4
    CUnit *pUnit = (CUnit *)GetCarrier();
    if (pUnit)
    {
        if (pUnit->GetCurrentPose() == UP_RUN)
        {
            nMinTrys = 4;
            pSPart->nLifeTime = 5;
        }
        else
            nMinTrys = 1;
        if (pUnit->IsDead())
            FinishEffect();
    }
    else
    {
        FinishEffect();
        return FALSE;
    }
#endif
    return TRUE;
}

BOOL CParticle::BallStarsSingleSegment(SSingleParticle *pSPart)
{
    if (--pSPart->nLifeTime < 0)
    {
        pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0);
        pSPart->coord.w = 0;
        return FALSE;
    }
    pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0xFF);
    pSPart->coord.w = random.GetFloat(0.8f, 1.2f) * fpSize * pSPart->reserved.x;
    if (!GetCarrier())
        FinishEffect();
#ifndef _A4
#endif
    return TRUE;
}

void CParticle::BallStarsLocSourceSegmentFunc(SLocalSource *pLSrc)
{
}

// ------------------------------- CurseStars -----------------------------------------------
BOOL CParticle::CurseStarsSingleCreate(SSingleParticle *pSPart, int)
{
    pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0);
    pSPart->nPhase = random.Get(0, 15);
    CPlot3D ptDir = GetRandomBallPoint() * fpSize;
    pSPart->coord = carrierCP + ptDir;
    pSPart->coord.w = 0;
    float fpLeng = fabs(ptDir);
    if (fpLeng > 0)
        pSPart->vSpeed = 0.1f * fpSize * ptDir / fpLeng;
    else
        pSPart->vSpeed = CPlot3D(0, 0, 0);
    pSPart->nLifeTime = 20;
    return TRUE;
}

static DWORD dwCurseStarAlpha[] = {
    0x00, 0x20, 0x40, 0x80, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};
BOOL CParticle::CurseStarsSingleSegment(SSingleParticle *pSPart)
{
    if (--pSPart->nLifeTime < 0)
    {
        pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0);
        pSPart->coord.w = 0;
        return FALSE;
    }
    pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, dwCurseStarAlpha[pSPart->nLifeTime]);
    if (pSPart->coord.w == 0)
        pSPart->coord.w = 0.04f * random.GetFloat(0.8f, 1.2f) * fpSize;
    else
        pSPart->coord.w *= 0.95f;
    pSPart->coord += pSPart->vSpeed;
    pSPart->vSpeed *= 0.9f;
    if (!GetCarrier())
        FinishEffect();
    return TRUE;
}

void CParticle::CurseStarsLocSourceSegmentFunc(SLocalSource *pLSrc)
{
}

// ------------------------------- StartTrans -----------------------------------------------
BOOL CParticle::StartTransSingleCreate(SSingleParticle *pSPart, int)
{
    if (particles.GetCount() == 0)
    {
        pSPart->nLifeTime = 4;
        pSPart->nSpecial = 1;
        pSPart->nPhase = 12;
        pSPart->coord = carrierCP;
    }
    else
    {
        pSPart->nLifeTime = 15;
        pSPart->nSpecial = 0;
        pSPart->nPhase = random.Get(0, 11);
        pSPart->reserved = GetRandomSpherePoint() * fpSize;
        pSPart->coord = carrierCP + pSPart->reserved;
        pSPart->vSpeed = -pSPart->reserved / (pSPart->nLifeTime + 3);
        pSPart->reserved2.x = fpSize * 0.05f;
    }
    pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0);
    pSPart->coord.w = 0;
    return TRUE;
}

static DWORD dwStartTransAlpha[] = { 0,    0x50, 0xb0, 0xff, 0xff, 0xff, 0xff, 0xff,
                                     0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x80, 0x40 };
BOOL CParticle::StartTransSingleSegment(SSingleParticle *pSPart)
{
    if (pSPart->nSpecial)
    {
        if (!GetCarrier())
        {
            if (--pSPart->nLifeTime < 0)
                return FALSE;
            pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, dwStartTransAlpha[pSPart->nLifeTime]);
            pSPart->coord.w *= 0.6f;
            return TRUE;
        }
        pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0xFF);
        pSPart->coord.w = locSources[0].reserved.x * random.GetFloat(0.8f, 1.2f);
        return TRUE;
    }
    if (--pSPart->nLifeTime < 0)
        return FALSE;
    if (!GetCarrier())
        pSPart->nLifeTime = Min(pSPart->nLifeTime, 4);
    pSPart->nPhase = ((pSPart->nPhase + 1) & 3) + (pSPart->nPhase & (~3));
    pSPart->coord += pSPart->vSpeed;
    pSPart->coord.w = pSPart->reserved2.x; // 0.1f;//pSPart->camera.x;
    pSPart->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, dwStartTransAlpha[pSPart->nLifeTime]);
    return TRUE;
}

void CParticle::StartTransLocSourceSegmentFunc(SLocalSource *pLSrc)
{
    if (pLSrc->nLifeTime-- < 0)
    {
        FinishEffect();
    }
    pLSrc->reserved.x += 0.02f * fpSize;
    locSources[0].reserved.y += 0.4f;
    nMinTrys = Float2Int(locSources[0].reserved.y);
    nMinTrys = Min(nMinTrys, 8);
}

// ------------------------- Segment processing ----------------------------------------------
namespace {
owned::ParticleFlowInput FlowInput(const CPlot3D& cp,const CPlot3D& direction,float length,float width){
    return {{cp.x,cp.y,cp.z},{direction.x,direction.y},length,width};
}
void FlowToHost(const owned::ParticleFlowState& p,SSingleParticle& out){
    out.coord.x=p.position[0];out.coord.y=p.position[1];out.coord.z=p.position[2];out.coord.w=p.size;
    out.reserved=CPlot3D(p.lateral,p.angle,p.distance);out.reserved2=CPlot3D(p.size,0,0);
    out.vSpeed=CPlot3D(p.speed,0,0);out.nLifeTime=p.life;out.nPhase=p.phase;out.dwColor=p.color;
}
}
void CParticle::ConfigureScriptFlow(const CPlot3D& direction,float length,float width)
{
    if(GetTypeID()!=PTID_SMOKE)return;
    if(createFunc!=&CParticle::ScriptFlowSingleCreate){
        pTexture.SetDesc(TTID_SPELL_MAIN,"VisionFog");locSources.SetSize(0);
        createFunc=&CParticle::ScriptFlowSingleCreate;segmentFunc=&CParticle::ScriptFlowSingleSegment;locSourceSegmentFunc=nullptr;
        nBornProbability=100;nMinTrys=7;nParticlesMax=180;fpLenToSingleDice=1000;
        nAnimPhases=15;nAnimPerTexSize=4;nLifeOfPhase=1;bUseAddEffect=FALSE;
        ResetFlag(PS_NEED_CARRIER_TO_GENERATE|PS_BIND_TO_BODYPART);
    }
    ptWallDirection=direction;fpPower=length;fpPower2=width;
}
BOOL CParticle::ScriptFlowSingleCreate(SSingleParticle* out,int)
{
    *out={};out->nSpecial=0x1ce;out->nPhaseLifeTime=1;
    owned::ParticleFlowState p;
    owned::particle_flow_create(FlowInput(carrierCP,ptWallDirection,fpPower,fpPower2),scriptFlowRandom,p);
    FlowToHost(p,*out);return TRUE;
}
BOOL CParticle::ScriptFlowSingleSegment(SSingleParticle* out)
{
    if(out->nSpecial!=0x1ce)return FALSE;
    owned::ParticleFlowState p;
    p.size=out->reserved2.x;p.lateral=out->reserved.x;p.angle=out->reserved.y;p.distance=out->reserved.z;
    p.speed=out->vSpeed.x;p.life=out->nLifeTime;p.phase=out->nPhase;
    if(!owned::particle_flow_segment(FlowInput(carrierCP,ptWallDirection,fpPower,fpPower2),p))return FALSE;
    FlowToHost(p,*out);return TRUE;
}


namespace {
void ArenaToHost(const owned::ArenaWaveParticle& p,SSingleParticle& out) {
    out.coord=SXYZW(p.x,p.y,p.z,p.size);
    out.reserved=CPlot3D(p.reserved.x,p.reserved.y,p.reserved.z);
    out.reserved2=CPlot3D(p.reserved2.x,p.reserved2.y,p.reserved2.z);
    out.vSpeed=CPlot3D(p.speed.x,p.speed.y,p.speed.z);
    out.nLifeTime=p.life;out.nSpecial=p.special;out.nPhase=p.phase;
    out.nPhaseLifeTime=p.phaseTime;out.dwColor=p.color;
}
owned::ArenaWaveParticle ArenaFromHost(const SSingleParticle& in) {
    owned::ArenaWaveParticle p;
    p.x=in.coord.x;p.y=in.coord.y;p.z=in.coord.z;p.size=in.coord.w;
    p.reserved={in.reserved.x,in.reserved.y,in.reserved.z};
    p.reserved2={in.reserved2.x,in.reserved2.y,in.reserved2.z};
    p.speed={in.vSpeed.x,in.vSpeed.y,in.vSpeed.z};
    p.life=in.nLifeTime;p.special=in.nSpecial;p.phase=in.nPhase;
    p.phaseTime=in.nPhaseLifeTime;p.color=in.dwColor;return p;
}
}
void CParticle::ConfigureArenaWave(int kind,float radius,bool charge,int tick)
{
    if(GetTypeID()!=PTID_SMOKE||kind<1||kind>3||!std::isfinite(radius)||radius>124)return;
    if(createFunc!=&CParticle::ArenaWaveSingleCreate) {
        arenaWave={};
        pTexture.SetDesc(TTID_SPELL_MAIN,kind==2?"TendriSmoke":kind==1?"TendriLight":"VisionFog");
        locSources.SetSize(0);createFunc=&CParticle::ArenaWaveSingleCreate;
        segmentFunc=&CParticle::ArenaWaveSingleSegment;locSourceSegmentFunc=nullptr;
        nBornProbability=100;fpLenToSingleDice=1000;
        nAnimPhases=15;nAnimPerTexSize=4;nLifeOfPhase=1;bUseAddEffect=kind==1?TRUE:FALSE;
        ResetFlag(PS_NEED_CARRIER_TO_GENERATE|PS_UNPAUSABLE|PS_BIND_TO_BODYPART|PS_PAUSE_INVISIBLE|PS_DONT_DRAW);
        SetFlag(PS_GENERATE_PARTICLES|PS_KEEP_ALIVE);
    }
    arenaWave.kind=kind;arenaWave.radius=radius;arenaWave.charge=charge;arenaWave.lease_tick=tick;
    nMinTrys=owned::arena_wave_tries(arenaWave);nParticlesMax=owned::arena_wave_maximum(kind);
}
BOOL CParticle::ArenaWaveSingleCreate(SSingleParticle* out,int)
{
    arenaWave.origin={carrierCP.x,carrierCP.y,carrierCP.z};
    owned::ArenaWaveParticle p;
    if(!owned::arena_wave_create(&arenaWave,&p,theWorldServer.GetSegmentTimeCounter()))return FALSE;
    *out={};ArenaToHost(p,*out);return TRUE;
}
BOOL CParticle::ArenaWaveSingleSegment(SSingleParticle* out)
{
    auto p=ArenaFromHost(*out);
    if(!owned::arena_wave_age(&p))return FALSE;
    ArenaToHost(p,*out);return TRUE;
}

void CParticle::AddNewParticle(SSingleParticle *pPart)
{
    pPart->nDelFlag = 0;

    pPart->prevcoord = pPart->coord;
    pPart->dwPrevColor = pPart->dwColor;
    pPart->nPrevPhase = pPart->nPhase;
    UpdateBV(pPart);
    particles.AddTail(pPart);
}

void CParticle::CreateParticle(int nParticle)
{
    SSingleParticle *pPart = ((CParticleManager *)lpManager)->newParticle();
    if (pPart)
    {
        if (!(this->*createFunc)(pPart, nParticle))
        {
            ((CParticleManager *)lpManager)->deleteParticle(pPart);
            return;
        }
        AddNewParticle(pPart);
    }
}

int CParticle::Segment()
{
    if (nInvisibleCounter > N_FRAMES_TO_PAUSE && CheckFlag(PS_PAUSE_INVISIBLE))
        return TRUE;
    CPlot3D newEffectPos;
#ifndef _A4
    // update wind
    theWorld.GetWind(windDir, fpWindForce);
#endif
    // clear bounding volume
    ClearBV();
    // Update carrier speed
    if (pCarrier && pCarrier->IsInvalid())
        AttachCarrier(0);
    if (pCarrier)
    {
        newEffectPos = GetCarrierCP(pCarrier) + GetCP(); // если есть связанный объект координаты
        // add some inertia in case of binding to cast point
        // if ( bindPlace == bpCast )
        // newEffectPos = 0.1f * newEffectPos + 0.9f * carrierCP;
        // самого эффекта являются относительными
        vCarrierSpeed = newEffectPos - carrierCP;
        carrierCP = newEffectPos;
    }
    else
    {
        vCarrierSpeed = GetCP() - carrierCP;
        carrierCP = GetCP();
    }
    fpSpeedMod = FSqrt(vCarrierSpeed.x * vCarrierSpeed.x + vCarrierSpeed.y * vCarrierSpeed.y +
                       vCarrierSpeed.z * vCarrierSpeed.z);
    if (fpSpeedMod > 1e-3)
    {
        vSpeedX1 = vCarrierSpeed ^ CPlot3D(0, 0, 1);
        float fpLeng = fabs(vSpeedX1);
        if (fpLeng < 1e-10)
        {
            vSpeedX1 = vCarrierSpeed ^ CPlot3D(0, 1, 1);
            fpLeng = fabs(vSpeedX1);
        }
        vSpeedX1 *= 1 / fpLeng;
        vSpeedX2 = vCarrierSpeed ^ vSpeedX1;
        fpLeng = fabs(vSpeedX2);
        vSpeedX2 *= 1 / fpLeng;
    }
    else
    {
        vSpeedX1 = CPlot3D(0, 0, 0);
        vSpeedX2 = CPlot3D(0, 0, 0);
    }
    // Update local sources
    for (int i = 0; i < locSources.GetSize(); i++)
        (this->*locSourceSegmentFunc)(&locSources[i]);

    // Call segment for all existing particles
    for (POSITION pos = particles.GetHeadPosition(); pos;)
    {
        POSITION posOld = pos;
        SSingleParticle *pPart = particles.GetNext(pos);
        if (pPart->nDelFlag)
        {
            particles.RemoveAt(posOld);
            ((CParticleManager *)lpManager)->deleteParticle(pPart);
            // Deferred deletion keeps the last interpolation frame, not another update.
            continue;
        }
        pPart->prevcoord = pPart->coord;
        pPart->dwPrevColor = pPart->dwColor;
        pPart->nPrevPhase = pPart->nPhase;
        if (!(this->*segmentFunc)(pPart))
        {
            pPart->nDelFlag = 1;
            pPart->coord = pPart->prevcoord;
            pPart->dwColor = pPart->dwPrevColor;
            pPart->nPhase = pPart->nPrevPhase;
        }
        UpdateBV(pPart);
    }

    // Arena generation is leased in simulation ticks; pause never advances it.
    if(createFunc==&CParticle::ArenaWaveSingleCreate) {
        const int now=theWorldServer.GetSegmentTimeCounter();
        if(now<arenaWave.lease_tick||now-arenaWave.lease_tick>=4)nMinTrys=0;
    }
    // Create new particles
    int nTrys = Float2Int(fpSpeedMod / fpLenToSingleDice);
    nTrys = Max(nTrys, nMinTrys);
    if (CheckFlag(PS_GENERATE_PARTICLES) &&
        (CheckFlag(PS_NEED_CARRIER_TO_GENERATE) == 0 || pCarrier))
    {
        int nParticles = 0;
        for (int n = 0; n < nTrys && particles.GetCount() < nParticlesMax; n++)
        {
            // Throw dices
            // Flow is always born (100%) and uses only its visual RNG.
            int nDice = (createFunc==&CParticle::ScriptFlowSingleCreate||createFunc==&CParticle::ArenaWaveSingleCreate)?0:random.Get(0, 99);
            // Check dices for create new particles
            if (nDice < nBornProbability)
                CreateParticle(nParticles++);
        }
        if (nParticles == 0 && nBornProbability > 50 && nTrys > 0 &&
            particles.GetCount() < nParticlesMax)
            CreateParticle(nParticles++);
    }

    // Calculate final values of bounding volume
    FinishBV();

    if (!pCarrier) // Check for carrier present
    {
        if (CheckFlag(PS_KEEP_ALIVE))
            return TRUE;
        return !particles.IsEmpty();
    }
    return CheckFlag(PS_GENERATE_PARTICLES) || !particles.IsEmpty();
}

// ------------------------------------ Draw
// ---------------------------------------------------------------

BOOL CParticle::Draw(CRenderer *pTarget)
{
    if (particles.GetCount() == 0)
    {
        LiveObserver::ObserveParticle(*this,false);
        return TRUE;
    }

    int nClipFlag = pTarget->CheckVisibility(bv);
    const int logicClip = LiveObserver::ParticleLogicVisibility(bv,nClipFlag);
    if (logicClip == TRV_INVISIBLE) ++nInvisibleCounter;
    else nInvisibleCounter = 0;
    if (nClipFlag == TRV_INVISIBLE)
    {
        LiveObserver::ObserveParticle(*this,false);
        return TRUE;
    }
    // Define temporary arrays
    static CParticleToDrawArray resParts;
    static CXYZWArray inCoords;
    // Степень перехода из состояния "prev" в состояние "current" [0,1)
    int nSegment = theWorld.GetSegmentTimeCounter();
    float fpCurrentStep = Min(theWorld.GetPreciseTime() - nSegment, 1.0f);
    fpCurrentStep = Max(fpCurrentStep, 0.0f);

    resParts.SetSize(4096);
    inCoords.SetSize(4096);
    resParts.SetSize(particles.GetCount());
    inCoords.SetSize(particles.GetCount());

    // Set texture
    if (!pTarget->BindTexture(0, pTexture))
    {
        LiveObserver::ObserveParticle(*this,false);
        return FALSE;
    }

    // Fill inCoords and set colors and texture coordinates
    SXYZW *pXYZW = inCoords.GetData();
    SParticleToDraw *pParticle = resParts.GetData();
    // Sequently fill and render arrays
    for (POSITION pos = particles.GetHeadPosition(); pos; pXYZW++, pParticle++)
    {
        SSingleParticle *pPart = particles.GetNext(pos);
        pXYZW->x = pPart->prevcoord.x * (1 - fpCurrentStep) + pPart->coord.x * fpCurrentStep;
        pXYZW->y = pPart->prevcoord.y * (1 - fpCurrentStep) + pPart->coord.y * fpCurrentStep;
        pXYZW->z = pPart->prevcoord.z * (1 - fpCurrentStep) + pPart->coord.z * fpCurrentStep;
        pXYZW->w = pPart->prevcoord.w * (1 - fpCurrentStep) + pPart->coord.w * fpCurrentStep;
        pParticle->dwColor = RGBA_MAKE(int(RGBA_GETRED(pPart->dwPrevColor) * (1 - fpCurrentStep) +
                                           RGBA_GETRED(pPart->dwColor) * fpCurrentStep),
                                       int(RGBA_GETGREEN(pPart->dwPrevColor) * (1 - fpCurrentStep) +
                                           RGBA_GETGREEN(pPart->dwColor) * fpCurrentStep),
                                       int(RGBA_GETBLUE(pPart->dwPrevColor) * (1 - fpCurrentStep) +
                                           RGBA_GETBLUE(pPart->dwColor) * fpCurrentStep),
                                       int(RGBA_GETALPHA(pPart->dwPrevColor) * (1 - fpCurrentStep) +
                                           RGBA_GETALPHA(pPart->dwColor) * fpCurrentStep), );
        pParticle->pUV = uvOfPhases.GetData() + pPart->nPrevPhase;
    }
    // Alpha-test only fully transparent exit texels. Without this, the DX11
    // pass writes zero into the target alpha even when RGB blending leaves
    // the background untouched. Restore the caller's state after this draw.
    const bool clipTransparentExit = GetTypeID() == PTID_ZONEEXIT ||
                                     GetTypeID() == PTID_STARTTRANS;
    DWORD oldAlphaTest = 0;
    DWORD oldAlphaRef = 0;
    if (clipTransparentExit)
    {
        pTarget->GetRenderState(D3DRENDERSTATE_ALPHATESTENABLE, &oldAlphaTest);
        pTarget->GetRenderState(D3DRENDERSTATE_ALPHAREF, &oldAlphaRef);
        pTarget->SetRenderState(D3DRENDERSTATE_ALPHATESTENABLE, TRUE);
        pTarget->SetRenderState(D3DRENDERSTATE_ALPHAREF, 1);
    }
    const BOOL drawn = pTarget->DrawParticles(bUseAddEffect != FALSE, inCoords, resParts);
    LiveObserver::ObserveParticle(*this,drawn!=FALSE);
    if (clipTransparentExit)
    {
        pTarget->SetRenderState(D3DRENDERSTATE_ALPHAREF, oldAlphaRef);
        pTarget->SetRenderState(D3DRENDERSTATE_ALPHATESTENABLE, oldAlphaTest);
    }
    return drawn;
}

void CParticle::ConfigurePowerHit(CUnit* carrier, const CPlot3D& point,
    const CPlot3D& direction, int hitTime)
{
    SetCP(point);
    powerHitWorld=theWorldServer.GetSpecificManager();
    carrierCP = point;
    // A two-layer additive weapon glow; contact dust stays alpha blended.
    bUseAddEffect = carrier ? TRUE : FALSE;
    dwPFlags = PS_GENERATE_PARTICLES;
    nBornProbability = 100;
    nParticlesMax = carrier ? 2 : 36;
    nMinTrys = carrier ? 2 : 14;
    fpLenToSingleDice = 1.0f;
    nLifeSpan = carrier ? 128 : 9;
    powerHitWeaponToken = 1;
    fpSize = carrier ? 0.55f : 0.26f;
    fpParam2 = 0.0f;
    fpPower2 = 0.0f;
    fpParam1 = carrier ? 0.0f : 1.0f;
    createFunc = &CParticle::PowerHitSingleCreate;
    segmentFunc = &CParticle::PowerHitSingleSegment;
    locSourceSegmentFunc = &CParticle::PowerHitSourceSegment;
    locSources.SetSize(1);
    SLocalSource source = {};
    source.nLifeTime = carrier ? 128 : 1;
    source.reserved.x = 3.0f;
    source.reserved.y = 6.0f;
    source.nPhase = hitTime;
    source.vSpeed = direction;
    locSources[0] = source;
    if (carrier)
    {
        BindTo(bpRHand);
        SetFlag(PS_BIND_TO_BODYPART);
        SetFlag(PS_NEED_CARRIER_TO_GENERATE);
        AttachCarrier(carrier);
    }
}

void CParticle::PowerHitSourceSegment(SLocalSource* source)
{
    if (fpParam1 == 0.0f)
    {
        if (fpParam2 < 0.0f || source->nLifeTime-- <= 0 ||
            (fpPower2 > 0.0f && source->reserved.y <= 0.0f) ||
            !PowerHitCarrierActive(pCarrier,powerHitWorld,source->nPhase,fpPower2>0,powerHitWeaponToken) ||
            !static_cast<CUnit*>(pCarrier)->MatchFigure("rh3"))
        {
            fpParam2 = -1.0f;
            powerHitLights.erase(this);
            ResetFlag(PS_GENERATE_PARTICLES);
            return;
        }
        carrierCP=GetBodyPartCP(static_cast<CUnit*>(pCarrier),"rh3") + GetCP();
        fpParam2 += 1.0f;
        if (fpPower2 > 0.0f) source->reserved.y -= 1.0f;
        const float lightStrength = fpPower2 > 0.0f ?
            max(0.0f, source->reserved.y / 6.0f) :
            min(1.0f, 0.35f + 0.65f * fpParam2 / max(1.0f, source->reserved.x - 1.0f));
        UpdatePowerHitLight(this,carrierCP,lightStrength);
        // Create exactly one core and one halo, then only update them.
        if (fpParam2 > 1.0f)
            ResetFlag(PS_GENERATE_PARTICLES);
    }
    else if (source->nLifeTime-- <= 0)
        ResetFlag(PS_GENERATE_PARTICLES);
}

BOOL CParticle::PowerHitSingleCreate(SSingleParticle* particle, int index)
{
    particle->coord = carrierCP;
    if (fpParam1 == 0.0f)
    {
        particle->nSpecial = index;
        particle->nLifeTime = nLifeSpan;
        particle->nPhase = 0; // white light cell; no blue smoke/arc frames
        particle->nPhaseLifeTime = 1;
        particle->vSpeed = CPlot3D(0, 0, 0);
        particle->coord.w = fpSize * (index == 0 ? 0.45f : 1.0f) * 0.65f;
        particle->dwColor = index == 0 ? RGBA_MAKE(110, 105, 90, 255) :
            RGBA_MAKE(75, 47, 12, 255);
        return TRUE;
    }
    else
    {
        particle->coord.x += random.GetFloat(-0.06f, 0.06f);
        particle->coord.y += random.GetFloat(-0.06f, 0.06f);
        particle->coord.z += random.GetFloat(-0.06f, 0.06f);
    }
    particle->coord.w = fpSize;
    particle->nLifeTime = nLifeSpan;
    // The shared atlas uses phases 0-7 for fire and 8-14 for smoke.
    particle->nPhase = fpParam1 == 0.0f ? 12 : 8;
    particle->nPhaseLifeTime = 1;
    particle->nSpecial = 0;
    particle->vSpeed = fpParam1 == 0.0f ? CPlot3D(0, 0, 0) :
        locSources[0].vSpeed * random.GetFloat(0.03f, 0.08f) +
        CPlot3D(random.GetFloat(-0.02f, 0.02f),
                random.GetFloat(-0.02f, 0.02f), random.GetFloat(0.01f, 0.03f));
    particle->dwColor = RGBA_MAKE(225, 215, 195, 215);
    return TRUE;
}

BOOL CParticle::PowerHitSingleSegment(SSingleParticle* particle)
{
    if (fpParam1 == 0.0f)
    {
        if (fpParam2 < 0.0f || --particle->nLifeTime <= 0)
        {
            // Segment restores previous color on deletion; clear both to avoid
            // one extra visible interpolation frame after a cancelled swing.
            particle->dwColor = particle->dwPrevColor = 0;
            return FALSE;
        }
        const float strength = fpPower2 > 0.0f ?
            max(0.0f, locSources[0].reserved.y / 6.0f) :
            min(1.0f, 0.35f + 0.65f * fpParam2 / max(1.0f, locSources[0].reserved.x - 1.0f));
        particle->coord = carrierCP;
        particle->coord.w = fpSize * (particle->nSpecial == 0 ? 0.45f : 1.0f) *
            (0.5f + 0.5f * strength);
        // Additive rendering uses RGB intensity, not alpha, for fading.
        particle->dwColor = particle->nSpecial == 0 ?
            RGBA_MAKE(int(255 * strength), int(245 * strength), int(215 * strength), 255) :
            RGBA_MAKE(int(180 * strength), int(115 * strength), int(30 * strength), 255);
        return TRUE;
    }
    if (--particle->nLifeTime <= 0)
        return FALSE;
    particle->coord += particle->vSpeed;
    particle->vSpeed *= 0.82f;
    particle->coord.w *= fpParam1 == 0.0f ? 1.04f : 1.12f;
    const int age = nLifeSpan - particle->nLifeTime;
    const int firstPhase = fpParam1 == 0.0f ? 12 : 8;
    particle->nPhase = min(nAnimPhases - 1, firstPhase +
        (15 - firstPhase) * age / nLifeSpan);
    particle->dwColor = RGBA_MAKE(225, 215, 195,
        215 * particle->nLifeTime / nLifeSpan);
    return TRUE;
}
void CParticle::ConfigureGolemWall(int side,int tick) {
    if(GetTypeID()!=PTID_SMOKE||side<0||side>3)return;
    if(createFunc!=&CParticle::GolemWallSingleCreate) {
        golemWall={};pTexture.SetDesc(TTID_SPELL_MAIN,"TendriSmoke");locSources.SetSize(0);
        createFunc=&CParticle::GolemWallSingleCreate;segmentFunc=&CParticle::GolemWallSingleSegment;locSourceSegmentFunc=nullptr;
        nBornProbability=100;fpLenToSingleDice=1000;nAnimPhases=15;nAnimPerTexSize=4;nLifeOfPhase=1;bUseAddEffect=FALSE;
        ResetFlag(PS_NEED_CARRIER_TO_GENERATE|PS_UNPAUSABLE|PS_BIND_TO_BODYPART|PS_PAUSE_INVISIBLE|PS_DONT_DRAW);
        SetFlag(PS_GENERATE_PARTICLES|PS_KEEP_ALIVE);
    }
    golemWall.side=side;golemWall.lease_tick=tick;nMinTrys=side%2?40:80;nParticlesMax=1800;
}
BOOL CParticle::GolemWallSingleCreate(SSingleParticle* out,int) {
    golemWall.ground=carrierCP.z;owned::ArenaWaveParticle p;
    if(!owned::golem_wall_create(golemWall,p,theWorldServer.GetSegmentTimeCounter()))return FALSE;
    *out={};ArenaToHost(p,*out);return TRUE;
}
BOOL CParticle::GolemWallSingleSegment(SSingleParticle* out) {
    auto p=ArenaFromHost(*out);if(!owned::golem_wall_age(p))return FALSE;ArenaToHost(p,*out);return TRUE;
}
