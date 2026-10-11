#include "StdAfx.h"
#include "ModSpellTextures.h"
#include "ModSpellOwner.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
/*==========================================================================================================

        Effects classes header

==========================================================================================================*/
/////////////////////////////////////////////////////////////////////////////////////
#include "Effect.h"
#include "ModRuntime.h"
#include "ModPackage.h"
#include "player.h"
#include "NetProtocol.h"
#include "Net.h"
#include "WorldClient.h"
#include "LightManager.h"
#include "Particle.h"
#include "ParticleManager.h"
#include "MapObjectManager.h"
#include "MapObject.h"
#include "WorldServer.h"
#include "lightning.h"
#include "Terrain.h"
#include "randomGen.h"
#include "UnitServer.h"
#include "Camera.h"
#include "Script.h"
// CRAP - this include is required for creation of map object ( is it actually required ?)
#include "Object3DClientSpecific.h"
/////////////////////////////////////////////////////////////////////////////////////
// CRAP - whole structure may be optimised: join light,sound&particles, replace move
// vector with destination and movespeed
/////////////////////////////////////////////////////////////////////////////////////
// ------------------------------------- General effect
// ----------------------------------------------------
void CEffect::UpdateInit()
{
    CUpdateHistory *pHist;
    pHist = update.StartUpdateTo(utStart);
    UpdateTo(*pHist);
    update.FinishUpdate();
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffect::UpdateFrame()
{
    CUpdateHistory *pHist;
    pHist = update.StartUpdateTo(utFrame);
    UpdateTo(*pHist);
    update.FinishUpdate();
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CEffect::UpdateFrom(EUpdateType tip, CPacket &pkt)
{
    BOOL bRv;
    CUpdateHistory *pHist;
    pHist = update.StartUpdateFrom(tip, pkt);
    bRv = UpdateFrom(tip, *pHist);
    update.FinishUpdate();
    return bRv;
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffect::Clear()
{
    SetCP(CPlot3D(0, 0, 0));
    update.Clear();
    nStartSegment = 0;
    vecSpeed = CPlot3D(0, 0, 0);
    bMoving = FALSE;
    nUpdatedSegment = -1;
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffect::SetUpdateMask()
{
    SetUpdated();
    nUpdatedSegment = theWorldServer.GetUpdateCounter();
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffect::SetMoving()
{
    bMoving = vecSpeed.x != 0 || vecSpeed.y != 0 || vecSpeed.z != 0;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CEffect::Create()
{
    Clear();
    UpdateInit();
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffect::Destroy()
{
    Clear();
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffect::SetSpeed(const CPlot3D &newSpeed)
{
    vecSpeed = newSpeed;
    SetMoving();
    SetUpdateMask();
}
/////////////////////////////////////////////////////////////////////////////////////
// ------------------------------------- Sound effect
// ------------------------------------------------------
CEffectSound::CEffectSound()
{
#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
    hSnd = SND_EMPTY;
    Clear();
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectSound::Clear()
{
    CEffect::Clear();
    if (hSnd != SND_EMPTY && bLooping)
        theWorld.GetSoundManager()->CancelFX(hSnd);
    hSnd = SND_EMPTY;
    bLooping = TRUE;
    szSoundName[0] = 0;
    fpMinRadius = 0;
    fpMaxRadius = 0;
    bHighPriority = false;
    bFresh = true;
}
/////////////////////////////////////////////////////////////////////////////////////
int CEffectSound::Segment()
{
    bool bWasFresh = bFresh;
    bFresh = false;
    if (bMoving)
    {
        SetCP(GetCP() + vecSpeed);
        if (hSnd)
            theWorld.GetSoundManager()->UpdateFX(hSnd, GetCP(), fpMinRadius, fpMaxRadius);
    }
    return bLooping || bWasFresh;
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectSound::ApplyParams(EUpdateType tip)
{
    if (tip == utStart)
        hSnd = theWorld.GetSoundManager()->BeginFX(szSoundName, bHighPriority ? 1000 : 0, GetCP(),
                                                   fpMinRadius, fpMaxRadius, bLooping);
    else
        theWorld.GetSoundManager()->UpdateFX(hSnd, GetCP(), fpMinRadius, fpMaxRadius);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CEffectSound::UpdateFrom(EUpdateType tip, CUpdateHistory &pkt)
{
    CPlot3D CP;
    pkt.ReadPos(CP);
    pkt >> bLooping;
    pkt.ReadString(szSoundName, 255);
    pkt.ReadFloat(fpMinRadius);
    pkt.ReadFloat(fpMaxRadius);
    pkt.ReadSpeed(vecSpeed);
    pkt.Read(&bHighPriority, sizeof(bHighPriority));
    SetMoving();
    if (!pkt.UpdateFromIsOk())
        return FALSE;

    SetCP(CP);
    ApplyParams(tip);
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectSound::UpdateTo(CUpdateHistory &pkt)
{
    CPlot3D CP = GetCP();
    pkt.WritePos(CP);
    pkt << bLooping;
    pkt.WriteString(szSoundName);
    pkt.WriteFloat(fpMinRadius);
    pkt.WriteFloat(fpMaxRadius);
    pkt.WriteSpeed(vecSpeed);
    pkt.Write(&bHighPriority, sizeof(bHighPriority));
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectSound::Load(CSave &file)
{
    file.Read(&cp, sizeof(cp));
    file.Read(szSoundName, sizeof(szSoundName));
    file.Read(fpMinRadius);
    file.Read(fpMaxRadius);
    file.Read(&vecSpeed, sizeof(vecSpeed));
    SetMoving();
    if (file.GetVersion() < 92)
        return;
    file.Read(&bLooping, sizeof(bLooping));
    if (file.GetVersion() < 109)
        return;
    file.Read(&bHighPriority, sizeof(bHighPriority));
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectSound::Store(CSave &file)
{
    file.Write(&cp, sizeof(cp));
    file.Write(szSoundName, sizeof(szSoundName));
    file.Write(fpMinRadius);
    file.Write(fpMaxRadius);
    file.Write(&vecSpeed, sizeof(vecSpeed));
    file.Write(&bLooping, sizeof(bLooping));
    file.Write(&bHighPriority, sizeof(bHighPriority));
}
/////////////////////////////////////////////////////////////////////////////////////
// ------------------------------ Sound effect ---------------------------------------
CEffectSoundSeq::CEffectSoundSeq()
{
#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
    hSndList = SND_EMPTY;
    Clear();
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectSoundSeq::Clear()
{
    CEffect::Clear();
    if (hSndList != SND_EMPTY)
        theWorld.GetSoundManager()->CancelFXList(hSndList);
    hSndList = SND_EMPTY;
    fpMinRadius = 0;
    fpMaxRadius = 0;
    szStartSound = "";
    szSounds.SetSize(0);
    bHighPriority = false;
}
/////////////////////////////////////////////////////////////////////////////////////
// FXList funcs for different cases
static int FXList1(int nPrevIndex, int nPlayingNumber)
{
    return 0 | SNDLIST_CYCLE_BIT;
}
static int FXList2(int nPrevIndex, int nPlayingNumber)
{
    return random.Get(0, 1);
}
static int FXList3(int nPrevIndex, int nPlayingNumber)
{
    return random.Get(0, 2);
}
static int FXList4(int nPrevIndex, int nPlayingNumber)
{
    return random.Get(0, 3);
}
static int FXList5(int nPrevIndex, int nPlayingNumber)
{
    return random.Get(0, 4);
}
static int FXListS1(int nPrevIndex, int nPlayingNumber)
{
    return nPlayingNumber ? 1 | SNDLIST_CYCLE_BIT : 0;
}
static int FXListS2(int nPrevIndex, int nPlayingNumber)
{
    return nPlayingNumber ? random.Get(1, 2) : 0;
}
static int FXListS3(int nPrevIndex, int nPlayingNumber)
{
    return nPlayingNumber ? random.Get(1, 3) : 0;
}
static int FXListS4(int nPrevIndex, int nPlayingNumber)
{
    return nPlayingNumber ? random.Get(1, 4) : 0;
}
static int FXListS5(int nPrevIndex, int nPlayingNumber)
{
    return nPlayingNumber ? random.Get(1, 5) : 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectSoundSeq::ApplyParams(EUpdateType tip)
{
    if (tip == utStart)
    {
        CStringArray szTempSounds;
        SMGetNextPlayIndexFunc func;
        if (szStartSound != "")
        {
            szTempSounds.Add(szStartSound);
            switch (szSounds.GetSize())
            {
            case 1:
                func = FXListS1;
                break;
            case 2:
                func = FXListS2;
                break;
            case 3:
                func = FXListS3;
                break;
            case 4:
                func = FXListS4;
                break;
            case 5:
                func = FXListS5;
                break;
            default:
                func = FXList1;
                break;
            }
        }
        else
        {
            switch (szSounds.GetSize())
            {
            case 1:
                func = FXList1;
                break;
            case 2:
                func = FXList2;
                break;
            case 3:
                func = FXList3;
                break;
            case 4:
                func = FXList4;
                break;
            case 5:
                func = FXList5;
                break;
            default:
                func = FXList1;
                break;
            }
        }
        for (int i = 0; i < szSounds.GetSize(); i++)
            szTempSounds.Add(szSounds[i]);
        hSndList = theWorld.GetSoundManager()->BeginFXList(szTempSounds, bHighPriority ? 1000 : 0,
                                                           GetCP(), fpMinRadius, fpMaxRadius, func);
    }
    else
        theWorld.GetSoundManager()->UpdateFX(hSndList, GetCP(), fpMinRadius, fpMaxRadius);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CEffectSoundSeq::UpdateFrom(EUpdateType tip, CUpdateHistory &pkt)
{
    CPlot3D CP;
    int nSounds;
    pkt.ReadPos(CP);
    szStartSound = pkt.ReadString();
    pkt >> nSounds;
    szSounds.SetSize(nSounds);
    for (int i = 0; i < nSounds; i++)
        szSounds[i] = pkt.ReadString();
    pkt.ReadFloat(fpMinRadius);
    pkt.ReadFloat(fpMaxRadius);
    pkt.ReadSpeed(vecSpeed);
    pkt.Read(&bHighPriority, sizeof(bHighPriority));
    SetMoving();
    if (!pkt.UpdateFromIsOk())
        return FALSE;

    SetCP(CP);
    ApplyParams(tip);
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectSoundSeq::UpdateTo(CUpdateHistory &pkt)
{
    CPlot3D CP = GetCP();
    pkt.WritePos(CP);
    pkt.WriteString(szStartSound);
    pkt << (int)szSounds.GetSize();
    for (int i = 0; i < szSounds.GetSize(); i++)
        pkt.WriteString(szSounds[i]);
    pkt.WriteFloat(fpMinRadius);
    pkt.WriteFloat(fpMaxRadius);
    pkt.WriteSpeed(vecSpeed);
    pkt.Write(&bHighPriority, sizeof(bHighPriority));
}
/////////////////////////////////////////////////////////////////////////////////////
int CEffectSoundSeq::Segment()
{
    if (bMoving)
    {
        SetCP(GetCP() + vecSpeed);
        if (hSndList)
            theWorld.GetSoundManager()->UpdateFXList(hSndList, GetCP(), fpMinRadius, fpMaxRadius);
    }
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectSoundSeq::Load(CSave &file)
{
    file.Read(&cp, sizeof(cp));
    file.Read(&vecSpeed, sizeof(vecSpeed));
    file.Read(szStartSound);
    LoadCStringArray(szSounds, file);
    file.Read(fpMinRadius);
    file.Read(fpMaxRadius);
    SetMoving();
    if (file.GetVersion() < 109)
        return;
    file.Read(&bHighPriority, sizeof(bHighPriority));
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectSoundSeq::Store(CSave &file)
{
    file.Write(&cp, sizeof(cp));
    file.Write(&vecSpeed, sizeof(vecSpeed));
    file.Write(szStartSound);
    StoreCStringArray(szSounds, file);
    file.Write(fpMinRadius);
    file.Write(fpMaxRadius);
    file.Write(&bHighPriority, sizeof(bHighPriority));
}
/////////////////////////////////////////////////////////////////////////////////////
// --------------------------- Ambient sound effect ----------------------------------
CEffectAmbientSound::CEffectAmbientSound()
{
#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
    hSndAmb = SND_EMPTY;
    Clear();
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectAmbientSound::Clear()
{
    CEffect::Clear();
    if (hSndAmb != SND_EMPTY)
        hSndAmb = theWorld.GetSoundManager()->CancelAmbient(hSndAmb);
    szSoundNames.RemoveAll();
    fpMinRadius = 0;
    fpMaxRadius = 0;
    nMsToPlayMin = 0;
    nMsToPlayMax = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectAmbientSound::ApplyParams(EUpdateType tip)
{
    if (tip == utStart)
        hSndAmb = theWorld.GetSoundManager()->BeginAmbient(szSoundNames, GetCP(), fpMinRadius,
                                                           fpMaxRadius, nMsToPlayMin, nMsToPlayMax);
    else
        theWorld.GetSoundManager()->UpdateAmbient(hSndAmb, GetCP(), fpMinRadius, fpMaxRadius,
                                                  nMsToPlayMin, nMsToPlayMax);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CEffectAmbientSound::UpdateFrom(EUpdateType tip, CUpdateHistory &pkt)
{
    BYTE cSize;
    CPlot3D CP;
    pkt.ReadPos(CP);
    pkt >> cSize;
    szSoundNames.SetSize(cSize);
    for (int i = 0; i < cSize; i++)
        szSoundNames[i] = pkt.ReadString();
    pkt.ReadFloat(fpMinRadius);
    pkt.ReadFloat(fpMaxRadius);
    pkt >> nMsToPlayMin;
    pkt >> nMsToPlayMax;
    if (!pkt.UpdateFromIsOk())
        return FALSE;

    SetCP(CP);
    ApplyParams(tip);
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectAmbientSound::UpdateTo(CUpdateHistory &pkt)
{
    CPlot3D CP = GetCP();
    pkt.WritePos(CP);
    pkt << (BYTE)szSoundNames.GetSize();
    for (int i = 0; i < szSoundNames.GetSize(); i++)
        pkt.WriteString(szSoundNames[i]);
    pkt.WriteFloat(fpMinRadius);
    pkt.WriteFloat(fpMaxRadius);
    pkt << nMsToPlayMin;
    pkt << nMsToPlayMax;
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectAmbientSound::Load(CSave &file)
{
    file.Read(&cp, sizeof(cp));
    LoadCStringArray(szSoundNames, file);
    file.Read(fpMinRadius);
    file.Read(fpMaxRadius);
    file.Read(nMsToPlayMin);
    file.Read(nMsToPlayMax);
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectAmbientSound::Store(CSave &file)
{
    file.Write(&cp, sizeof(cp));
    StoreCStringArray(szSoundNames, file);
    file.Write(fpMinRadius);
    file.Write(fpMaxRadius);
    file.Write(nMsToPlayMin);
    file.Write(nMsToPlayMax);
}

/////////////////////////////////////////////////////////////////////////////////////
// ------------------------------------ Point light effect
// -------------------------------------------------
CEffectPointLight::CEffectPointLight()
{
#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
    pLight = 0;
    Clear();
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectPointLight::Clear()
{
    CEffect::Clear();
    if (pLight)
    {
        theWorld.GetTerrain()->RemoveLight(pLight);
        pLight = 0;
    }
    color = CPlot3D(0, 0, 0);
    fpRange = 0;
    dwFlags = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
int CEffectPointLight::Segment()
{
    CPlot3D moveVector;
    if (bMoving)
    {
        SetCP(GetCP() + vecSpeed);
        if (pLight)
        {
            moveVector = vecSpeed;
            // CRAP
            if ((theWorldServer.GetSegmentTimeCounter() & 15) == 0)
            {
                moveVector = vecSpeed;
                theWorld.GetTerrain()->RemoveLight(pLight);
                pLight->SetCP(GetCP());
                theWorld.GetTerrain()->AddLight(pLight);
            }
            else
            {
                theWorld.GetTerrain()->MoveLight(pLight, &moveVector);
                pLight->SetCP(GetCP());
            }
        }
    }
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectPointLight::ApplyParams(EUpdateType tip)
{
    if (tip == utStart)
    {
        pLight = ((CLightManager *)theWorld.GetLightManager())->CreateLight(LTID_POINTLIGHT);
        pLight->SetPointLight(D3DVECTOR(color.x, color.y, color.z), D3DVECTOR(cp.x, cp.y, cp.z));
        pLight->SetRange(fabs(fpRange));
        pLight->SetFlag(dwFlags);
        theWorld.GetTerrain()->AddLight(pLight);
        if (bMoving)
            pLight->SetFlag(LIT_DYNAMIC);
    }
    else
    {
        pLight->SetDiffuse(color.x, color.y, color.z);
        if (fabs(pLight->GetRange() - fabs(fpRange)) < 1e-3f)
        {
            CPlot3D moveVector = GetCP() - pLight->GetCP();
            theWorld.GetTerrain()->MoveLight(pLight, &moveVector);
            pLight->SetCP(GetCP());
        }
        else
        {
            theWorld.GetTerrain()->RemoveLight(pLight);
            pLight->SetCP(GetCP());
            pLight->SetRange(fabs(fpRange));
            theWorld.GetTerrain()->AddLight(pLight);
        }
    }
    if (fpRange < 0)
        pLight->SetFlag(LIT_UNSHADOW);
    else
        pLight->ResetFlag(LIT_UNSHADOW);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CEffectPointLight::UpdateFrom(EUpdateType tip, CUpdateHistory &pkt)
{
    CPlot3D CP;
    pkt.ReadPos(CP);
    pkt.ReadSpeed(color);
    pkt.ReadFloat(fpRange);
    pkt.ReadSpeed(vecSpeed);
    pkt.Read(&dwFlags, sizeof(dwFlags));
    SetMoving();
    if (!pkt.UpdateFromIsOk())
        return FALSE;
    SetCP(CP);
    ApplyParams(tip);
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectPointLight::UpdateTo(CUpdateHistory &pkt)
{
    CPlot3D CP = GetCP();
    pkt.WritePos(CP);
    pkt.WriteSpeed(color);
    pkt.WriteFloat(fpRange);
    pkt.WriteSpeed(vecSpeed);
    pkt.Write(&dwFlags, sizeof(dwFlags));
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectPointLight::Load(CSave &file)
{
    file.Read(&cp, sizeof(cp));
    file.Read(&color, sizeof(color));
    file.Read(fpRange);
    file.Read(&vecSpeed, sizeof(vecSpeed));
    SetMoving();
    if (file.GetVersion() < 103)
        return;
    file.Read(dwFlags);
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectPointLight::Store(CSave &file)
{
    file.Write(&cp, sizeof(cp));
    file.Write(&color, sizeof(color));
    file.Write(fpRange);
    file.Write(&vecSpeed, sizeof(vecSpeed));
    file.Write(dwFlags);
}
/////////////////////////////////////////////////////////////////////////////////////
// ------------------------------------- Particle effect
// ---------------------------------------------------
CEffectParticle::CEffectParticle()
{
#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
    pParticle = 0;
    Clear();
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectParticle::Clear()
{
    CEffect::Clear();
    if (pParticle)
    {
        pParticle->FinishEffect();
        pParticle = 0;
    }
    nAttachedID = -1;
    fpScale = 1;
    fpPower = 1;
    fpPower2 = 1;
    typeID = TID_EMPTY;
    ptDest = CPlot3D(0, 0, 0);
    nStartID = -1;
}
/////////////////////////////////////////////////////////////////////////////////////
const CPlot3D &CEffectParticle::GetEffectPlace(CScriptObjectGroup &currentVisible)
{
    if (nAttachedID != -1)
    {
        CMapObject *pBind = ((CWorldBase *)GetWorld())->GetMapObject(nAttachedID);
        if (pBind)
        {
            if (!currentVisible.HasMapObject(pBind))
                ptTemp = CPlot3D(-1e6f, -1e6f, 0);
            else
                ptTemp = pBind->GetCP() + cp;
            return ptTemp;
        }
    }
    return cp;
}
/////////////////////////////////////////////////////////////////////////////////////
int CEffectParticle::GetParticleEffectLength()
{
    switch (typeID)
    {
    // Standalone variant effects have the same native lifetime as their base families.
    case PTID_FIREBLASTDARK:case PTID_FIREBLASTLIGHT:
    case PTID_LIGHTNINGBLASTDARK:case PTID_LIGHTNINGBLASTLIGHT:
    case PTID_NUKEDARK:case PTID_NUKELIGHT:
    case PTID_ACIDSTARDARK:case PTID_ACIDSTARLIGHT:
        return 60;
    case PTID_FIREBLAST:
    case PTID_LIGHTNINGBLAST:
    case PTID_VAMP:
    case PTID_HEALING:
    case PTID_CASTING:
    case PTID_NUKE:
    case PTID_BANSHEECASTING:
    case PTID_MUSHROOM:
    case PTID_BLOOD1:
    case PTID_BLOOD2:
    case PTID_BLOOD3:
    case PTID_BLOOD4:
    case PTID_CLAYRING:
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
    case PTID_CYLINDER1:
    case PTID_CYLINDER2:
    case PTID_FIRESTAR:
    case PTID_ACIDSTAR:
    case PTID_SPARKS:
        return 60;
    case PTID_TELEPORT:
        return 120;
    }
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectParticle::ApplyParams(EUpdateType tip)
{
    SpellTextures::ClientScope textureScope(this);
    if (tip == utStart && pParticle)
    {
        pParticle->FinishEffect();
        pParticle = 0;
    }
    UpdateParticleParams();
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CEffectParticle::UpdateFrom(EUpdateType tip, CUpdateHistory &pkt)
{
    CPlot3D CP;
    pkt.ReadPos(CP);
    pkt >> nStartSegment;
    pkt.Read(&typeID, sizeof(typeID));
    pkt.ReadFloat(fpScale);
    pkt >> nAttachedID;
    pkt.ReadSpeed(vecSpeed);
    pkt.ReadPos(ptDest);
    pkt >> nStartID;
    pkt.ReadFloat(fpPower);
    pkt.ReadFloat(fpPower2);
    SetMoving();
    if (WhoAmI() == HPC_EFFECTPARTICLE && !pkt.UpdateFromIsOk())
        return FALSE;
    if (nStartID != -1)
    {
        CUnit *pUnit = GetWorldBase()->GetUnit(nStartID);
        if (pUnit)
            CP = pUnit->GetCastPointPosition();
    }
    SetCP(CP);
    ApplyParams(tip);
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectParticle::UpdateParticleParams()
{
    int nTimePass;
    if (pParticle == 0)
    {
        pParticle = ((CParticleManager *)theWorld.GetParticleManager())
                        ->CreateParticleSystem(typeID);
        pParticle->SetCP(GetCP());
        pParticle->Scale(fpScale / pParticle->GetScale());
        pParticle->AttachCarrier(NULL);
        pParticle->SetPower(fpPower);
        pParticle->SetPower2(fpPower2);
        AttachCarrierOnClient();

        nTimePass = theWorld.GetSegmentTimeCounter() - nStartSegment;
        if (GetParticleEffectLength() == 0)
            nTimePass = Min(nTimePass, 30);
        // CRAP, have to check may be i = 0 ( depends on updates sending/receveing sequence )
        for (int i = 1; i < nTimePass; i++)
            pParticle->Segment();
        //
        if (typeID == PTID_FIREBLAST)
        {
            float _fpPower = Min(1.0f, 0.6f * fpPower);
            theWorld.AddFirePrints(GetCP().x, GetCP().y, fpPower2, fpPower2,
                                   random.GetFloat(0, 6.3f), _fpPower);
        }
        if (typeID == PTID_LIGHTNINGBLAST)
        {
            float _fpPower = Min(1.0f, 0.6f * fpPower);
            theWorld.AddFirePrints(GetCP().x, GetCP().y, fpScale, fpScale, random.GetFloat(0, 6.3f),
                                   _fpPower);
            theWorld.GetCamera()->BeginEarthquake(GetCP(), fpScale);
        }
    }
    else
    {
        pParticle->SetCP(GetCP());
        pParticle->Scale(fpScale / pParticle->GetScale());
        pParticle->SetPower(fpPower);
        pParticle->SetPower2(fpPower2);
        AttachCarrierOnClient();
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectParticle::UpdateTo(CUpdateHistory &pkt)
{
    CPlot3D CP = GetCP();
    pkt.WritePos(CP);
    pkt << nStartSegment;
    pkt.Write(&typeID, sizeof(typeID));
    pkt.WriteFloat(fpScale);
    pkt << nAttachedID;
    pkt.WriteSpeed(vecSpeed);
    pkt.WritePos(ptDest);
    pkt << nStartID;
    pkt.WriteFloat(fpPower);
    pkt.WriteFloat(fpPower2);
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectParticle::Attach(int nObjectID)
{
    nAttachedID = nObjectID;
    SetUpdateMask();
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectParticle::SetScale(float _fpScale)
{
    fpScale = _fpScale;
    SetUpdateMask();
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectParticle::SetPower(float _fpPower, float _fpPower2)
{
    fpPower = _fpPower;
    fpPower2 = _fpPower2;
    SetUpdateMask();
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectParticle::SetDestination(const CPlot3D &_ptDest)
{
    ptDest = _ptDest;
    SetUpdateMask();
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectParticle::AttachCarrierOnClient()
{
    CPlot3D prevCP;
    CMapObject *pMO;
    ASSERT(pParticle);
    if (pParticle)
    {
        if (pParticle->GetCarrier() && pParticle->GetCarrier()->GetID() == nAttachedID)
            return;
        if (pParticle->GetCarrier() == NULL && nAttachedID == -1)
            return;
    }
    pMO = (CMapObject *)((CMapObjectManager *)theWorld.GetMapObjectManager())->Find(nAttachedID);
    prevCP = pParticle->GetCP();
    pParticle->AttachCarrier(pMO); // to detach from object specify unexisting ObjID
    if (pMO == NULL)
        pParticle->SetCP(prevCP); // preserve CP when detaching from object with server command
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectParticle::Load(CSave &file)
{
    file.Read(&cp, sizeof(cp));
    file.Read(&typeID, sizeof(typeID));
    file.Read(fpScale);
    file.Read(nAttachedID);
    nAttachedID = GetWorld()->GetMapObjectManager()->SavedObjectID(nAttachedID);
    file.Read(&vecSpeed, sizeof(vecSpeed));
    SetMoving();
    file.Read(nStartSegment);
    file.Read(&ptDest, sizeof(ptDest));
    if (file.GetVersion() < 67)
        return;
    file.Read(nStartID);
    nStartID = GetWorld()->GetMapObjectManager()->SavedObjectID(nStartID);
    if (file.GetVersion() < 88)
        return;
    file.Read(fpPower);
    file.Read(fpPower2);
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectParticle::Store(CSave &file)
{
    file.Write(&cp, sizeof(cp));
    file.Write(&typeID, sizeof(typeID));
    file.Write(fpScale);
    file.Write(nAttachedID);
    file.Write(&vecSpeed, sizeof(vecSpeed));
    file.Write(nStartSegment);
    file.Write(&ptDest, sizeof(ptDest));
    file.Write(nStartID);
    file.Write(fpPower);
    file.Write(fpPower2);
}
/////////////////////////////////////////////////////////////////////////////////////
int CEffectParticle::Segment()
{
    // CPlot3D ptNewPos;
    // float fpZ;
    int nParticleEffectTime;
    nStartID = -1;
    if (bMoving)
    {
        CPlot3D ptNewPos;
        float fpZ;
        float fpSpeed = fabs(vecSpeed), fpDist;
        CPlot3D ptDif = ptDest - GetCP();
        // ptDif.z = 0;
        fpDist = sqrt(sqr(ptDif.x) + sqr(ptDif.y));
        if (fpDist > 0)
        {
            if (fpDist < fpSpeed)
                fpDist = fpSpeed;
            float fpAlpha = fpSpeed / fpDist;
            fpZ = ptDest.z - GetWorldBase()->GetSurfaceValue(ptDest.x, ptDest.y);
            ptNewPos = GetCP() + ptDif * fpAlpha;
            ptNewPos.z = Max(ptNewPos.z,
                             fpZ + GetWorldBase()->GetSurfaceValue(ptNewPos.x, ptNewPos.y));
            SetCP(ptNewPos);
            if (pParticle)
                pParticle->SetCP(ptNewPos);
        }
        /*		ptNewPos = GetCP() + vecSpeed;
                        fpZ = GetCP().z - GetWorldBase()->GetSurfaceValue( GetCP().x, GetCP().y );
                        ptNewPos.z = fpZ + GetWorldBase()->GetSurfaceValue( ptNewPos.x, ptNewPos.y
           ); SetCP( ptNewPos );*/
    }
    nParticleEffectTime = GetParticleEffectLength();
    // check up isnt it time to erase particle effect
    if (nParticleEffectTime > 0 &&
        GetWorldBase()->GetSegmentTimeCounter() > nStartSegment + nParticleEffectTime)
        return FALSE;
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// CEffectWall
/////////////////////////////////////////////////////////////////////////////////////
CEffectWall::CEffectWall()
{
#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
    pParticle = 0;
    Clear();
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectWall::Clear()
{
    CEffectParticle::Clear();
    ptWallDirection = CPlot3D(0, 0, 0);
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectWall::SetWallVector(const CPlot3D &vec)
{
    ptWallDirection = vec;
    SetUpdateMask();
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CEffectWall::UpdateFrom(EUpdateType tip, CUpdateHistory &pkt)
{
    if (!CEffectParticle::UpdateFrom(tip, pkt))
        return FALSE;
    pkt.ReadSpeed(ptWallDirection);
    if (!pkt.UpdateFromIsOk())
        return FALSE;

    if (pParticle)
        pParticle->SetWallVector(ptWallDirection);
    if (tip == utStart && typeID == PTID_FIREWALL)
    {
        float _fpPower = Min(1.0f, 0.6f * fpPower);
        float fpAngle = atan2(ptWallDirection.y, ptWallDirection.x);
        float fpDist = sqrt(sqr(ptWallDirection.y) + sqr(ptWallDirection.x));
        theWorld.AddFirePrints(GetCP().x, GetCP().y, fpScale * 0.5f + fpDist + 0.3f,
                               fpScale * 0.5f + 0.3f, fpAngle, _fpPower);
    }
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectWall::UpdateTo(CUpdateHistory &pkt)
{
    CEffectParticle::UpdateTo(pkt);
    pkt.WriteSpeed(ptWallDirection);
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectWall::Load(CSave &file)
{
    CEffectParticle::Load(file);
    file.Read(&ptWallDirection, sizeof(ptWallDirection));
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectWall::Store(CSave &file)
{
    CEffectParticle::Store(file);
    file.Write(&ptWallDirection, sizeof(ptWallDirection));
}
/////////////////////////////////////////////////////////////////////////////////////
// CEffectLightning
/////////////////////////////////////////////////////////////////////////////////////
CEffectLightning::CEffectLightning()
{
#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
    pLightning = 0;
    fpWidth = 0;
    bColor = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectLightning::Clear()
{
    CEffect::Clear();
    ptSrc = CPlot3D(0, 0, 0);
    ptDst = CPlot3D(0, 0, 0);
    fpWidth = 1;
    nAttachedID = -1;
    nCasterID = -1;
    pLightning = 0;
    bColor = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectLightning::RecalcPosition()
{
    CMapObject *pObj;
    pObj = GetWorldBase()->GetMapObject(nAttachedID);
    if (pObj)
    {
        ptDst = pObj->GetCP();
        ptDst.z += 1;
    }
    pObj = GetWorldBase()->GetMapObject(nCasterID);
    if (pObj)
    {
        if (pObj->WhoAmI() == HPC_UNIT && !GetWorld()->IsServer())
            ptSrc = ((CUnit *)pObj)->GetCastPointPosition();
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectLightning::ApplyParams(EUpdateType tip)
{
    SpellTextures::ClientScope textureScope(this);
    if (tip == utStart)
    {
        pLightning = (CLightning *)theWorld.GetLightningManager()->CreateData(TID_EMPTY);
        RecalcPosition();
        pLightning->CreateLightning(ptSrc, ptDst, fpWidth, bColor);
    }
    else
    {
        ASSERT(0);
        return;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CEffectLightning::UpdateFrom(EUpdateType tip, CUpdateHistory &pkt)
{
    CPlot3D CP;
    pkt.ReadPos(CP);
    pkt.ReadPos(ptSrc);
    pkt.ReadPos(ptDst);
    pkt.ReadFloat(fpWidth);
    pkt.Read(&bColor, sizeof(bColor));
    pkt >> nAttachedID >> nCasterID;
    if (!pkt.UpdateFromIsOk())
        return FALSE;
    RecalcPosition();
    SetCP(CP);
    ASSERT(tip == utStart);
    ApplyParams(tip);
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectLightning::UpdateTo(CUpdateHistory &pkt)
{
    CPlot3D CP = GetCP();
    pkt.WritePos(CP);
    pkt.WritePos(ptSrc);
    pkt.WritePos(ptDst);
    pkt.WriteFloat(fpWidth);
    pkt.Write(&bColor, sizeof(bColor));
    pkt << nAttachedID << nCasterID;
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectLightning::SetParams(const CPlot3D &ptNSrc, const CPlot3D &ptNDst, float fpNWidth)
{
    ptSrc = ptNSrc;
    ptDst = ptNDst;
    fpWidth = fpNWidth;
    SetCP((ptSrc + ptDst) / 2);
    SetUpdateMask();
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectLightning::Load(CSave &file)
{
    file.Read(&cp, sizeof(cp));
    file.Read(&ptSrc, sizeof(ptSrc));
    file.Read(&ptDst, sizeof(ptDst));
    file.Read(fpWidth);
    file.Read(nAttachedID);
    nAttachedID = GetWorld()->GetMapObjectManager()->SavedObjectID(nAttachedID);
    file.Read(nCasterID);
    nCasterID = GetWorld()->GetMapObjectManager()->SavedObjectID(nCasterID);
    if (file.GetVersion() < 100)
        return;
    file.Read(&bColor, sizeof(bColor));
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectLightning::Store(CSave &file)
{
    file.Write(&cp, sizeof(cp));
    file.Write(&ptSrc, sizeof(ptSrc));
    file.Write(&ptDst, sizeof(ptDst));
    file.Write(fpWidth);
    file.Write(nAttachedID);
    file.Write(nCasterID);
    file.Write(&bColor, sizeof(bColor));
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectLightning::Attach(int nSrcID, int nTargetID)
{
    nAttachedID = nTargetID;
    nCasterID = nSrcID;
    SetUpdateMask();
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectLightning::SetColor(BYTE _bColor)
{
    bColor = _bColor;
    SetUpdateMask();
}
/////////////////////////////////////////////////////////////////////////////////////
int CEffectLightning::Segment()
{
    if (pLightning && (nAttachedID != -1 || nCasterID != -1))
    {
        RecalcPosition();
        pLightning->MoveLightning(ptSrc, ptDst);
    }
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// CEffectArrowBase
/////////////////////////////////////////////////////////////////////////////////////
const float FP_ARROW_SPEED = 10.0f / 15;
const float FP_MIN_HEIGHT = 1;
/////////////////////////////////////////////////////////////////////////////////////
void CEffectArrowBase::Clear()
{
    CEffect::Clear();
    bDelivered = FALSE;
    nTargetID = -1;
    bNewArrow = false;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CEffectArrowBase::CalcSpeed()
{
    BOOL bRv = TRUE;
    CWorldBase *pWorld = GetWorldBase();
    CMapObject *pTarget = pWorld->GetMapObject(nTargetID);
    if (pTarget)
    {
        float fpLeng;
        CPlot3D ptTarget = pTarget->GetCP();
        ptTarget.z += pTarget->bv.ptCenter.z + pTarget->bv.ptMax.z * 0.5f;
        vecSpeed = ptTarget - GetCP();
        fpLeng = (float)sqrt(sqr(vecSpeed.x) + sqr(vecSpeed.y));
        float fpArrowSpeed = FP_ARROW_SPEED;
        if (WhoAmI() == HPC_EFFECTARROWFIGURE)
            fpArrowSpeed *= 2;
        if (fpLeng < fpArrowSpeed)
        {
            bRv = FALSE;
            bDelivered = TRUE;
        }
        else
            vecSpeed *= fpArrowSpeed / fpLeng;
    }
    else
    {
        vecSpeed = CPlot3D(0, 0, 0);
        bRv = FALSE;
        bDelivered = TRUE;
    }
    if (!pWorld->IsServer())
    {
        float fpH = pWorld->GetSurfaceValue(GetCP().x, GetCP().y);
        if (GetCP().z < fpH + FP_MIN_HEIGHT)
        {
            float fpDH = fpH + FP_MIN_HEIGHT - GetCP().z, fpDelta;
            fpDelta = Min(FP_MIN_HEIGHT, fpDH);
            fpDH -= fpDelta * 0.5f;
            vecSpeed.z = fpDH; // fpH + FP_MIN_HEIGHT - GetCP().z;
        }
    }
    return bRv;
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectArrowBase::SetTarget(int nID)
{
    nTargetID = nID;
    bDelivered = FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
int CEffectArrowBase::Segment()
{
    bNewArrow = false;
    BOOL bRv = CalcSpeed();
    SetCP(GetCP() + vecSpeed);
    if (!GetWorld()->IsServer())
        ApplyNewArrowPos();
    return bRv;
}
/////////////////////////////////////////////////////////////////////////////////////
// CEffectArrow
/////////////////////////////////////////////////////////////////////////////////////
void CEffectArrow::DiscardWeaponAttack()
{
    if(auto*mod=ModRuntime::Current())mod->ForgetAttack(weaponAttack);
    weaponAttackPending=false;weaponAttack.Clear();
}
void CEffectArrow::SetWeaponAttack(const SRPGAttackPortion&source)
{
    DiscardWeaponAttack();weaponAttack=source;weaponAttackPending=true;
    if(auto*mod=ModRuntime::Current())mod->MoveAttack(source,weaponAttack);
}
BOOL CEffectArrow::DeliverWeaponAttack(CUnit*target,CUnit*attacker)
{
    if(!weaponAttackPending)return FALSE;
    weaponAttackPending=false; // Consume before callbacks, including reentrant delivery.
    struct Cleanup{CEffectArrow*arrow;~Cleanup(){arrow->DiscardWeaponAttack();}} cleanup{this};
    if(!target||target->IsDead()||target->GetUserCommonStats().nHP<=0)return FALSE;
    if(target->GetOwner()&&target->GetOwner()->bGod>=2)return FALSE;
    CPlot3D delta=target->GetCP()-GetCP();
    if(sqr(delta.x)+sqr(delta.y)>=1)return FALSE;
    auto *server=dynamic_cast<CUnitServer*>(target->GetUnitSpecific());
    return server?server->ProcessCustomAttackPortion(&weaponAttack,attacker,attacker):
        target->ProcessAttackPortion(&weaponAttack,attacker,attacker);
}
int CEffectArrow::Segment()
{
    int result=CEffectArrowBase::Segment();
    auto*world=GetWorldBase();
    if(weaponAttackPending&&bDelivered&&world->IsServer())
        DeliverWeaponAttack(world->GetUnit(nTargetID),world->GetUnit(nAttackerID));
    return result;
}

void CEffectArrow::Clear()
{
    DiscardWeaponAttack();
    CEffectArrowBase::Clear();
    particle.Clear();
    pointLight.Clear();
    sound.Clear();

    color = CPlot3D(0, 0, 0);
    fpRange = 0;
    typeID = TID_EMPTY;
    fpScale = 1;
    nAttackerID = -1;
}
/////////////////////////////////////////////////////////////////////////////////////
CEffectArrow::CEffectArrow()
{
#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
    Clear();
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectArrow::ApplyNewArrowPos()
{
    particle.SetCP(GetCP());
    pointLight.SetCP(GetCP());
    sound.SetCP(GetCP());
    particle.ApplyParams(utFrame);
    pointLight.ApplyParams(utFrame);
    sound.ApplyParams(utFrame);
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectArrow::ApplyParams(EUpdateType tip)
{
    SpellTextures::ClientScope textureScope(this);
    particle.SetCP(GetCP());
    pointLight.SetCP(GetCP());
    sound.SetCP(GetCP());
    particle.typeID = typeID;
    particle.fpScale = typeID>=PTID_STAFFARROWFIRE&&typeID<=PTID_STAFFARROWACID?.5f:fpScale;
    pointLight.color = color;
    pointLight.fpRange = fpRange;
    pointLight.SetSpeed(CPlot3D(0.0001f, 0.0f, 0.0f));
    pointLight.dwFlags = LIT_MAGICSHADOW | LIT_SPECULAR;
    strcpy(sound.szSoundName, szSoundName);
    sound.fpMaxRadius = 5;
    sound.fpMaxRadius = 30;
    sound.bLooping = TRUE;

    particle.ApplyParams(tip);
    pointLight.ApplyParams(tip);
    sound.ApplyParams(tip);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CEffectArrow::UpdateFrom(EUpdateType tip, CUpdateHistory &pkt)
{
    CPlot3D CP;
    pkt >> nTargetID;
    pkt >> nAttackerID;
    szSoundName = pkt.ReadString();
    pkt.ReadSpeed(color);
    pkt.ReadFloat(fpRange);
    pkt.Read(&typeID, sizeof(typeID));
    pkt.ReadFloat(fpScale);
    pkt.ReadPos(CP);
    pkt.ReadSpeed(vecSpeed);
    pkt.Read(&bNewArrow, 1);
    if (!pkt.UpdateFromIsOk())
        return FALSE;
    if (bNewArrow)
    {
        CUnit *pUnit = GetWorldBase()->GetUnit(nAttackerID);
        if (pUnit)
            CP = pUnit->GetCastPointPosition();
    }
    SetCP(CP);
    ApplyParams(tip);
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectArrow::UpdateTo(CUpdateHistory &pkt)
{
    CalcSpeed();
    CPlot3D CP = GetCP();
    pkt << nTargetID;
    pkt << nAttackerID;
    pkt.WriteString(szSoundName);
    pkt.WriteSpeed(color);
    pkt.WriteFloat(fpRange);
    pkt.Write(&typeID, sizeof(typeID));
    pkt.WriteFloat(fpScale);
    pkt.WritePos(CP);
    pkt.WriteSpeed(vecSpeed);
    pkt.Write(&bNewArrow, 1);
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectArrow::Load(CSave &file)
{
    DiscardWeaponAttack(); // Legacy save format has no weapon payload.
    file.Read(&cp, sizeof(cp));
    file.Read(nTargetID);
    nTargetID = GetWorld()->GetMapObjectManager()->SavedObjectID(nTargetID);
    file.Read(&vecSpeed, sizeof(vecSpeed));
    file.Read(bDelivered);
    file.Read(szSoundName);
    file.Read(&color, sizeof(color));
    file.Read(fpRange);
    file.Read(&typeID, sizeof(typeID));
    file.Read(fpScale);
    if (file.GetVersion() < 67)
        return;
    file.Read(nAttackerID);
    nAttackerID = GetWorld()->GetMapObjectManager()->SavedObjectID(nAttackerID);
    file.Read(bNewArrow);
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectArrow::Store(CSave &file)
{
    file.Write(&cp, sizeof(cp));
    file.Write(nTargetID);
    file.Write(&vecSpeed, sizeof(vecSpeed));
    file.Write(bDelivered);
    file.Write(szSoundName);
    file.Write(&color, sizeof(color));
    file.Write(fpRange);
    file.Write(&typeID, sizeof(typeID));
    file.Write(fpScale);
    file.Write(nAttackerID);
    file.Write(bNewArrow);
}
/////////////////////////////////////////////////////////////////////////////////////
// CEffectArrowFigure
/////////////////////////////////////////////////////////////////////////////////////
void CEffectArrowFigure::Clear()
{
    attackProcessed=false;explosivePending=false;
    if(auto*mod=ModRuntime::Current())mod->ForgetAttack(attackPortion);
    CEffectArrowBase::Clear();
    if (!pMapObj.IsEmpty())
        theWorld.RemoveObject(pMapObj->GetID());
    attackPortion.Clear();
    pMapObj = 0;
    nAttackerID = -1;
}
/////////////////////////////////////////////////////////////////////////////////////
CEffectArrowFigure::CEffectArrowFigure()
{
#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
    Clear();
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CEffectArrowFigure::DeliverAttack(CMapObject*target,CUnit*attacker)
{
    if(attackProcessed)return FALSE;
    attackProcessed=true;
    auto*mod=ModRuntime::Current();
    struct Cleanup{CModPackage*mod;SRPGAttackPortion&ap;~Cleanup(){if(mod)mod->ForgetAttack(ap);}} cleanup{mod,attackPortion};
    return target?target->ProcessAttackPortion(&attackPortion,attacker,attacker):FALSE;
}
int CEffectArrowFigure::Segment()
{
    if(attackProcessed)return 0; // Arrival may already have been observed by UpdateTo.
    CEffectArrowBase::Segment();
    CWorldBase *pWorld = GetWorldBase();
    if (bDelivered && pWorld->IsServer())
    {
        CMapObject *pTarget = pWorld->GetMapObject(nTargetID);
        CUnit *pAttacker = pWorld->GetUnit(nAttackerID);
        DeliverAttack(pTarget,pAttacker);
    }
    return !bDelivered;
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectArrowFigure::ApplyNewArrowPos()
{
    ASSERT(!pMapObj.IsEmpty());
    if (pMapObj.IsEmpty())
        return;
    theWorld.MoveObject(pMapObj, GetCP());
    pMapObj->OrientInYDirection(vecSpeed);
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectArrowFigure::ApplyParams(EUpdateType tip)
{
    if (tip == utStart)
    {
        CString szTemplate = "efar1";
        CString szTexture = "arrow01";
        pMapObj = (LPUNIT)theWorld.GetMapObjectManager()->CreateData(
            MOTID_OBJECT, theWorld.GetTemporaryMapObjectID());

        CObject3DClientSpecific *lpObjectSpecific = (CObject3DClientSpecific *)
                                                        pMapObj->GetObjectSpecific();

        pMapObj->SetCP(GetCP());

        CStringArray szstrarr;
        pMapObj->LoadByTemplate(szTemplate, szstrarr);
        lpObjectSpecific->SetMaterials();
        pMapObj->LoadAnimation(szTemplate);
        pMapObj->InitAnimation();
        lpObjectSpecific->LoadTextures(szTexture, "");

        theWorld.AddObject(pMapObj);
    }
    else
        ApplyNewArrowPos();
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CEffectArrowFigure::UpdateFrom(EUpdateType tip, CUpdateHistory &pkt)
{
    CPlot3D CP;
    bNewArrow = FALSE;
    pkt >> nTargetID >> nAttackerID;
    pkt.ReadPos(CP);
    pkt.ReadSpeed(vecSpeed);
    pkt.Read(&bNewArrow, 1);
    if (!pkt.UpdateFromIsOk())
        return FALSE;
    if (bNewArrow)
    {
        CUnit *pUnit = GetWorldBase()->GetUnit(nAttackerID);
        if (pUnit)
            CP = pUnit->GetCastPointPosition();
    }
    SetCP(CP);
    ApplyParams(tip);
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectArrowFigure::UpdateTo(CUpdateHistory &pkt)
{
    CalcSpeed();
    CPlot3D CP = GetCP();
    pkt << nTargetID << nAttackerID;
    pkt.WritePos(CP);
    pkt.WriteSpeed(vecSpeed);
    pkt.Write(&bNewArrow, 1);
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectArrowFigure::Load(CSave &file)
{
    explosivePending=false;attackProcessed=false;
    if(auto*mod=ModRuntime::Current())mod->ForgetAttack(attackPortion);
    file.Read(&cp, sizeof(cp));
    file.Read(nTargetID);
    nTargetID = GetWorld()->GetMapObjectManager()->SavedObjectID(nTargetID);
    file.Read(nAttackerID);
    nAttackerID = GetWorld()->GetMapObjectManager()->SavedObjectID(nAttackerID);
    file.Read(&vecSpeed, sizeof(vecSpeed));
    file.Read(bDelivered);
    pMapObj.Load(file, *GetWorld());
    attackPortion.Load(file);
    file.Read(bNewArrow);
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectArrowFigure::Store(CSave &file)
{
    file.Write(&cp, sizeof(cp));
    file.Write(nTargetID);
    file.Write(nAttackerID);
    file.Write(&vecSpeed, sizeof(vecSpeed));
    file.Write(bDelivered);
    pMapObj.Store(file);
    attackPortion.Store(file);
    file.Write(bNewArrow);
}
/////////////////////////////////////////////////////////////////////////////////////
// CEffectShowVisibility
/////////////////////////////////////////////////////////////////////////////////////
CEffectShowVisibility::CEffectShowVisibility()
{
    nTargetID = -1;
    nWatcherID = -1;
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectShowVisibility::Clear()
{
    mask.Clear();
    if (!pFog.IsEmpty())
        pFog->AddNewVisibilityData(mask);
    nTargetID = -1;
    nWatcherID = -1;
    pFog = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectShowVisibility::SetNewParams(int nTarget, int nWatcher)
{
    nTargetID = nTarget;
    nWatcherID = nWatcher;
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectShowVisibility::ApplyParams(EUpdateType tip)
{
    if (tip == utStart)
        pFog = theWorld.GetVisFogManager()->CreateFog();
    if (pFog.IsEmpty())
    {
        ASSERT(0);
        return;
    }
    pFog->AddNewVisibilityData(mask);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CEffectShowVisibility::UpdateFrom(EUpdateType tip, CUpdateHistory &pkt)
{
    CMemorySave msk;
    if (!pkt.ReadDirect(msk))
        return FALSE;
    mask.Load(msk);
    ApplyParams(tip);
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectShowVisibility::UpdateTo(CUpdateHistory &pkt)
{
    CMemorySave msk;
    mask.Store(msk);
    pkt.WriteDirect(msk);
}
/////////////////////////////////////////////////////////////////////////////////////
int CEffectShowVisibility::Segment()
{
    if (GetWorld()->IsServer())
    {
        CUnit *pWatcher, *pTarget;
        pWatcher = GetWorldBase()->GetUnit(nWatcherID);
        pTarget = GetWorldBase()->GetUnit(nTargetID);
        if (pWatcher && pTarget)
        {
            SetCP(pWatcher->GetCP());
            BuildVisionMask(pWatcher, pTarget, mask);
            SetUpdateMask();
        }
        else
            return FALSE;
    }
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectShowVisibility::Load(CSave &file)
{
    file.Read(nTargetID);
    nTargetID = GetWorld()->GetMapObjectManager()->SavedObjectID(nTargetID);
    file.Read(nWatcherID);
    nWatcherID = GetWorld()->GetMapObjectManager()->SavedObjectID(nWatcherID);
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectShowVisibility::Store(CSave &file)
{
    file.Write(nTargetID);
    file.Write(nWatcherID);
}
/////////////////////////////////////////////////////////////////////////////////////
// CEffectMoshka
/////////////////////////////////////////////////////////////////////////////////////
const float FP_MOSHKA_RADIUS = 1.5f;
const float FP_MOSHKA_H = 3.2f;
/////////////////////////////////////////////////////////////////////////////////////
CEffectMoshka::CEffectMoshka()
{
#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
    Clear();
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectMoshka::Clear()
{
    CEffect::Clear();
    if (!pMapObj.IsEmpty())
        theWorld.RemoveObject(pMapObj->GetID());
    if (pParticle)
        pParticle->FinishEffect();
    pParticle = 0;
    pMapObj = 0;
    nUnitID = -1;
    fpPrevTime = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
int CEffectMoshka::Segment()
{
    SetTime(theWorld.GetSegmentTimeCounter());
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
CPlot3D CEffectMoshka::ChooseFarPoint(const CPlot3D &ptBase)
{
    CPlot3D ptRes, ptSrc;
    float fpDH = 30, fpR = 50;
    ptSrc = ptBase;
    ptSrc.z += 1.5f;
    for (;; fpDH += 3, fpR -= 5)
    {
        fpR = Max(fpR, 4.0f);
        float fpAngle = random.GetFloat(0, 6.28f);
        ptRes = ptBase;
        ptRes.z += fpDH;
        ptRes.x += cos(fpAngle) * fpR;
        ptRes.y += sin(fpAngle) * fpR;
        if (theWorldServer.aiMap.TraceRelief(ptSrc, ptRes) > 0.5f)
            break;
    }
    return ptRes;
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectMoshka::DoFinish(const CPlot3D &ptFinish)
{
    state = Finish;
    fpStateTime = 150;
    SetCP(ptFinish);
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectMoshka::SetTime(float fpTime)
{
    if (pMapObj.IsEmpty())
        return;
    CMapObject *pTarget = theWorld.GetMapObject(nUnitID);
    if (!pTarget)
        return;
    //
    float fpDTime = fpTime - fpPrevTime;
    fpPrevTime = fpTime;
    fpDTime = Max(0.0f, fpDTime);
    fpDTime = Min(5.0f, fpDTime);
    //
    CPlot3D ptTarget, ptVec;
    fpStateTime -= fpDTime;
    switch (state)
    {
    case Start:
        if (fpStateTime < 0)
            DoFinish(ChooseFarPoint(pTarget->GetCP()));
        ptTarget = pTarget->GetCP();
        ptVec = ptTarget - pMapObj->GetCP();
        if (distanceXY2(ptTarget, pMapObj->GetCP()) < 5 * 5)
        {
            state = Round;
            fpStateTime = 100;
            fpRoundAwayH = 30;
            fpBaseAngle = atan2(-ptVec.x, ptVec.y) - fpTime * 0.2f - 1.5f;
        }
        ptVec = CPlot3D(ptVec.y, -ptVec.x, 0);
        ptVec *= FP_MOSHKA_RADIUS / fabs(ptVec);
        ptVec.z = FP_MOSHKA_H;
        ptTarget += ptVec;
        break;

    case Round: {
        float fpAngle = fpBaseAngle + fpTime * 0.2f;
        if (fpStateTime < 0)
        {
            // time to get away;
            CPlot3D ptTest = pMapObj->GetCP();
            ptTest.z += fpRoundAwayH;
            ptTest.x += cos(fpAngle + FP_PI2) * 50;
            ptTest.y += sin(fpAngle + FP_PI2) * 50;
            if (theWorldServer.aiMap.TraceRelief(pMapObj->GetCP(), ptTest) > 0.5f)
                DoFinish(ptTest);
            else
                fpRoundAwayH += 0.5f;
        }
        ptTarget = pTarget->GetCP();
        ptTarget.z += FP_MOSHKA_H;
        ptTarget.x += FP_MOSHKA_RADIUS * cos(fpAngle);
        ptTarget.y += FP_MOSHKA_RADIUS * sin(fpAngle);
    }
    break;

    case Finish:
        if (fpStateTime < 0 || GetCP() == pMapObj->GetCP())
        {
            // int nTemp = nUnitID;
            Clear();
            // Create( nTemp ); // create new moshka, for testing purposes only
            return;
        }
        ptTarget = GetCP();
        break;
    }
    ptTarget.z = Max(ptTarget.z, theWorld.GetBridgeSurfaceZValue(ptTarget.x, ptTarget.y) + 0.5f);
    //
    ptVec = ptTarget - pMapObj->GetCP();
    float fpLeng = fabs(ptVec);
    if (fpLeng != 0)
    {
        float fpSpeed = 0.3f + 0.03f * sqrt(fabs(pMapObj->GetCP() - pTarget->GetCP()));
        if (fpLeng > fpDTime * fpSpeed)
            ptVec *= fpDTime * fpSpeed / fpLeng;
        theWorld.MoveObject(pMapObj, pMapObj->GetCP() + ptVec);
        pMapObj->OrientInYDirection(ptVec);
    }
    pMapObj->GetAnimationSystem2()->Step(fpDTime);
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectMoshka::Create(int nBindTo)
{
    nUnitID = nBindTo;
    CMapObject *pTarget = theWorld.GetMapObject(nUnitID);
    if (!pTarget)
        return;
    // create moshka
    CString szTemplate = "unmobi";
    CString szTexture = "bird00";
    pMapObj = (LPUNIT)theWorld.GetMapObjectManager()->CreateData(
        MOTID_OBJECT, theWorld.GetTemporaryMapObjectID());

    CObject3DClientSpecific *lpObjectSpecific = (CObject3DClientSpecific *)
                                                    pMapObj->GetObjectSpecific();

    CPlot3D ptStart = ChooseFarPoint(pTarget->GetCP());
    pMapObj->SetCP(ptStart);

    CStringArray szstrarr;
    pMapObj->LoadByTemplate(szTemplate, szstrarr);
    lpObjectSpecific->SetMaterials();
    pMapObj->LoadAnimation(szTemplate);
    pMapObj->InitAnimation();
    lpObjectSpecific->LoadTextures(szTexture, "");

    theWorld.AddObject(pMapObj);
    //
    lpObjectSpecific->Preselect(true);
    //
    // set object animation
    const SADEntry *pEntry = NULL;
    pEntry = pMapObj->GetAnimationEntryByIndex(0);
    ASSERT(pEntry);
    if (pEntry)
    {
        pMapObj->GetAnimationSystem2()->ClearAnimationsQueue();
        pMapObj->GetAnimationSystem2()->SetAnimation(pEntry);
    }
    // create particle effect
    pParticle = ((CParticleManager *)theWorld.GetParticleManager())
                    ->CreateParticleSystem(PTID_MOSHKA);
    // pParticle->SetCP( GetCP() );
    pParticle->Scale(0.5f); // fpScale / pParticle->GetScale() );
    pParticle->AttachCarrier((CMapObject *)pMapObj);
    //
    state = Start;
    fpStateTime = 200;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CEffectMoshka::UpdateFrom(EUpdateType tip, CUpdateHistory &pkt)
{
    // ASSERT(0);
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectMoshka::UpdateTo(CUpdateHistory &pkt)
{
    // ASSERT(0);
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectMoshka::ApplyParams(EUpdateType tip)
{
    // ASSERT(0);
}
/////////////////////////////////////////////////////////////////////////////////////
// CEffectTornado
/////////////////////////////////////////////////////////////////////////////////////
const float FP_TORNADO_SOUND_MIN = 10;
const float FP_TORNADO_SOUND_MAX = 40;
CEffectTornado::CEffectTornado()
{
#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
    hSnd = SND_EMPTY;
    Clear();
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectTornado::Clear()
{
    CEffect::Clear();
    if (pParticle)
        pParticle->DeleteEffect();
    if (hSnd != SND_EMPTY)
        theWorld.GetSoundManager()->CancelFX(hSnd);
    hSnd = SND_EMPTY;
    pParticle = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectTornado::SetParams(const CPlot3D &ptSrc, const CPlot3D &ptDst)
{
    float fpSpeed = 0.12f * random.GetFloat(0.8f, 1.2f), fpLeng;
    CPlot3D ptSpeed = ptDst - ptSrc;
    fpLeng = fabs(ptSpeed);
    nTimeLeft = 0;
    if (fpLeng > 0)
    {
        ptSpeed *= fpSpeed / fpLeng;
        nTimeLeft = Max(0, Float2Int(fpLeng / fpSpeed));
    }
    SetSpeed(ptSpeed);
    SetCP(ptSrc);
    nStartSegment = ((CWorldBase *)GetWorld())->GetSegmentTimeCounter();
}
/////////////////////////////////////////////////////////////////////////////////////
int CEffectTornado::Segment()
{
    if (bMoving)
    {
        SetCP(GetCP() + vecSpeed);
        if (pParticle)
            pParticle->SetCP(GetCP());
        if (hSnd != SND_EMPTY)
            theWorld.GetSoundManager()->UpdateFX(hSnd, GetCP(), FP_TORNADO_SOUND_MIN,
                                                 FP_TORNADO_SOUND_MAX);
    }
    if (nTimeLeft <= 110 && pParticle)
        pParticle->FinishEffect();
    // damage units with owners near subj
    if (GetWorld()->IsServer() && nTimeLeft > 20)
    {
        if ((nTimeLeft & 7) == 0)
        {
            POSITION pos;
            CUnitList units;
            SRPGAttackPortion attackPortion;
            CWorldServer *pWS = (CWorldServer *)GetWorld();
            pWS->aiMap.GetUnitsNear(GetCP(), units, 5);
            attackPortion.Clear();
            attackPortion.nSkill = 0;
            attackPortion.fpToHit = 100;
            attackPortion.fpAttacks[RPGAT_GENERAL] = 1;
            attackPortion.hl = RPGHL_BODY;
            const auto* tornadoMod = ModRuntime::Current();
            attackPortion.fpDamage = tornadoMod && tornadoMod->SpellAddon().Feature37() == 1
                ? tornadoMod->SpellAddon().Scalar(ModSpellAddon::ScalarKey::gameplay_tordmg) : 5.0f;
            for (pos = units.GetHeadPosition(); pos;)
            {
                CUnit *pUnit = units.GetNext(pos);
                if (!pUnit->GetOwner())
                    continue;
                ((CUnitServer *)pUnit->GetUnitSpecific())
                    ->ProcessAttackPortion(&attackPortion, 0, 0);
            }
        }
    }

    return --nTimeLeft >= 0;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CEffectTornado::UpdateFrom(EUpdateType tip, CUpdateHistory &pkt)
{
    CPlot3D CP;
    pkt.Read(&CP, sizeof(CP));
    pkt >> nTimeLeft;
    pkt.Read(&vecSpeed, sizeof(vecSpeed));
    SetMoving();
    if (!pkt.UpdateFromIsOk())
        return FALSE;
    SetCP(CP);
    ApplyParams(tip);
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectTornado::UpdateTo(CUpdateHistory &pkt)
{
    CPlot3D CP = GetCP();
    pkt.Write(&CP, sizeof(CP));
    pkt << nTimeLeft;
    pkt.Write(&vecSpeed, sizeof(vecSpeed));
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectTornado::ApplyParams(EUpdateType tip)
{
    if (tip == utStart && pParticle)
    {
        pParticle->FinishEffect();
        pParticle = 0;
    }
    if (pParticle == 0)
    {
        if (hSnd == SND_EMPTY)
        {
            hSnd = theWorld.GetSoundManager()->BeginFX("nature\\tornado\\circle.wav", 0, GetCP(),
                                                       FP_TORNADO_SOUND_MIN, FP_TORNADO_SOUND_MAX,
                                                       true);
        }
        //
        pParticle = ((CParticleManager *)theWorld.GetParticleManager())
                        ->CreateParticleSystem(ModRuntime::Current() && ModRuntime::Current()->CombatSetting("JMVParticles")
                            ? PTID_EYEBALLFIRE : PTID_TORNADO);
        pParticle->SetCP(GetCP());
        pParticle->Scale(1); // fpScale / pParticle->GetScale() );

        int nTimePass = theWorld.GetSegmentTimeCounter() - nStartSegment;
        nTimePass = Min(nTimePass, 30);
        for (int i = 1; i < nTimePass; i++)
            pParticle->Segment();
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectTornado::Load(CSave &file)
{
    file.Read(&cp, sizeof(cp));
    file.Read(&vecSpeed, sizeof(vecSpeed));
    file.Read(nTimeLeft);
    file.Read(nStartSegment);
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectTornado::Store(CSave &file)
{
    file.Write(&cp, sizeof(cp));
    file.Write(&vecSpeed, sizeof(vecSpeed));
    file.Write(nTimeLeft);
    file.Write(nStartSegment);
}
/////////////////////////////////////////////////////////////////////////////////////

BOOL CEffectArrowFigure::CalcSpeed(){
 if(nTargetID!=-2)return CEffectArrowBase::CalcSpeed();
 auto*base=GetWorldBase();if(base&&!base->IsServer())return TRUE;
 if(!base||!explosivePending){bDelivered=TRUE;vecSpeed=CPlot3D(0,0,0);return FALSE;}
 auto*world=static_cast<CWorldServer*>(base);CPlot3D next;
 bool arrived=ModSkills::ExplosiveStep(GetCP(),explosivePoint,world->GetSegmentTimeCounter()-explosiveStart,next,
  [world](const CPlot3D&a,const CPlot3D&b){return world->aiMap.TraceRelief(a,b);});
 vecSpeed=next-GetCP();if(!arrived)return TRUE;
 explosivePending=false;bDelivered=TRUE;attackProcessed=true; // Consume before nested damage/UpdateTo.
 Explode(next);return FALSE;
}
void CEffectArrowFigure::Explode(const CPlot3D& point){
 auto*base=GetWorldBase();if(base!=&theWorldServer)return;
 auto*world=static_cast<CWorldServer*>(base);auto*attacker=world->GetUnit(nAttackerID);
 CUnitList units;world->aiMap.GetUnitsNear(point,units,explosiveRadius);
 for(POSITION pos=units.GetHeadPosition();pos;){auto*target=units.GetNext(pos);
  if(!target||!target->CanBeDamagedByAreaSpell())continue;
  auto*server=dynamic_cast<CUnitServer*>(target->GetUnitSpecific());if(!server)continue;
  SRPGAttackPortion portion=attackPortion;server->ProcessCustomAttackPortion(&portion,attacker,attacker);
 }
 // One local client request after all targets, including an empty target list.
 ModSpellOwner::ExplosiveSound(world==&theWorldServer,theWorld.GetSoundManager(),
  [&](CSoundManager*sound,const ModSpellOwner::SoundRequest&request){sound->BeginFX(request.resource,request.priority,point,request.nearDistance,request.farDistance,request.loop);});
 auto*manager=world->GetEffectManager();
 auto*particle=static_cast<CEffectParticle*>(manager->CreateData(ETypeIDs(HPC_EFFECTPARTICLE)));
 if(particle){particle->typeID=PTID_FIREBLAST;particle->SetCP(point);particle->fpScale=(std::max)(.5f,explosiveRadius/3);particle->fpPower=1;particle->SetSpeed(CPlot3D(0,0,0));world->AddEffect(particle);}
}
