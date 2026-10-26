#include "StdAfx.h"
#include "ModNativeCrtRandom.h"
#include "NumericPolicy.h"
#include "ModRuntime.h"
#include "ModPackage.h"
#include "ModAudioPolicy.h"
#include "WorldInterface.h"
#include "ModStatRules.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "UnitClientSpecific.h"

#include "Object3DClientSpecific.h"
#include "MapObjectClientSpecific.h"
#include "MapObjectManager.h"
#include "ParticleManager.h"
#include "TextureManager.h"
#include "FigureManager.h"
#include "SoundManager.h"
#include "Particle.h"
#include "adFormat.h"
#include "WorldClient.h"
#include "WorldServer.h"
#include "adFormat.h"
#include "udbFormat.h"
#include "Terrain.h"
#include "randomGen.h"
#include "UnitDatabase.h"
#include "ItemDatabase.h"
#include "AckDatabase.h"
#include "GSVars.h"
#include "Renderer.h"
#include "Camera.h"
#include "PlayerClient.h"
#include "AISettings.h"
#include "ModItemSpellPolicy.h"
#include "dx11/Surface.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
/////////////////////////////////////////////////////////////////////////////////////
static const float FP_MIN_SOUND_RADIUS = 8.0f;
static const float FP_MAX_SOUND_RADIUS = 40.0f;
template<class WeaponAt> static int ResolveVisualLeftHand(const CUnit& unit, WeaponAt weaponAt);
/////////////////////////////////////////////////////////////////////////////////////
// helper function to load rect from one texture to the point in the another one
static bool CopyMouthTexture(ITexture *pDst, const POINT *pPoint, ITexture *pSrc, const RECT *pRect)
{
    if (!pDst || !pSrc || !pDst->GetSurface() || !pSrc->GetSurface()) return false;
    auto sDst = pDst->GetSurface();
    auto sSrc = pSrc->GetSurface();
    D3D11_TEXTURE2D_DESC dst = {}, src = {};
    sDst->GetSurfaceDesc(&dst);
    sSrc->GetSurfaceDesc(&src);
    if (dst.Format != src.Format || dst.SampleDesc.Count != 1 || src.SampleDesc.Count != 1 ||
        pPoint->x < 0 || pPoint->y < 0 || pRect->left < 0 || pRect->top < 0 ||
        pRect->right <= pRect->left || pRect->bottom <= pRect->top) return false;
    // Mouth atlases are uncompressed; compressed rectangles need block alignment.
    if (dst.Format != DXGI_FORMAT_B8G8R8A8_UNORM && dst.Format != DXGI_FORMAT_R8G8B8A8_UNORM &&
        dst.Format != DXGI_FORMAT_B5G5R5A1_UNORM && dst.Format != DXGI_FORMAT_B5G6R5_UNORM)
        return false;
    D3D11_BOX box = { UINT(pRect->left), UINT(pRect->top), 0,
                       UINT(pRect->right), UINT(pRect->bottom), 1 };
    UINT x = UINT(pPoint->x), y = UINT(pPoint->y);
    DWORD mips = min(dst.MipLevels, src.MipLevels);
    bool copied = false;
    for (DWORD i = 0; i < mips; ++i)
    {
        if (box.right <= box.left || box.bottom <= box.top) break;
        if (box.right > src.Width || box.bottom > src.Height ||
            x > dst.Width || y > dst.Height ||
            box.right - box.left > dst.Width - x || box.bottom - box.top > dst.Height - y)
            return copied;
        sDst->CopySubresourceRegion(i, x, y, 0, sSrc, i, &box);
        copied = true;
        box.left >>= 1; box.top >>= 1; box.right >>= 1; box.bottom >>= 1;
        x >>= 1; y >>= 1;
        src.Width = max(1U, src.Width >> 1); src.Height = max(1U, src.Height >> 1);
        dst.Width = max(1U, dst.Width >> 1); dst.Height = max(1U, dst.Height >> 1);
    }
    return copied;
}
static bool UseHDCharacterTextures()
{
    return ModRuntime::Current() && ModRuntime::Current()->CombatSetting("JMVGraphics");
}
/////////////////////////////////////////////////////////////////////////////////////
typedef CParticle *CParticlePtr;
static void FreeEffect(CParticlePtr &pParticle)
{
    if (pParticle)
    {
        pParticle->FinishEffect();
        pParticle->Release();
        pParticle = NULL;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
static void DeleteEffect(CParticlePtr &pParticle)
{
    if (pParticle)
    {
        pParticle->DeleteEffect();
        pParticle->Release();
        pParticle = NULL;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
CUnitClientSpecific::CUnitClientSpecific()
{
    // bNeedUpdateRoute = FALSE;
    fFadeOutAlpha = 1;
    bWoundTextureDataCached = false;
}
/////////////////////////////////////////////////////////////////////////////////////
CUnitClientSpecific::~CUnitClientSpecific()
{
    Destroy();
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CUnitClientSpecific::Create()
// create internal data structures
BOOL CUnitClientSpecific::Create()
{
    pCastingParticle = NULL;
    pPathParticle[0] = 0;
    pPathParticle[1] = 0;
    fpPrevSetPosTime = 0;
    route.Clear();
    fLastTime = 0;
    pLastADEntry = 0;
    bWeaponHidden = false;
    bBriefingWeaponForcedHidden = false;
    bWeaponHiddenBeforeBriefing = false;
    bArrowHidden = false;
    bFirstSetpos = true;
    fFadeOutAlpha = 1;
    hLastStoredIdleSFX = -1;
    hLastStoredAttackSFX = -1;
    hLastAttackSound = -1;
    pLastAck = 0;
    dwLastAckTime = 0;

    nTemporalHighlightSegmentsCounter = 0;
    nBloodyPrintsCounter = -1;
    nBloodyPrintsType = -1;
    bWoundTextureDataCached = false;
    //
    bMouthSpeakingMode = false;
    nMouthSwitchTicks = 0;
    nCurrentMouthState = 0;
    //
    MemSetDWord(armors, -1, RPG_NUM_ARMOR_TYPES);
    MemSetDWord(weapons, -1, QI_MAX_WEAPONS);
    //
    ReleaseMouthMimicTexture();
    //
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// void CUnitClientSpecific::Destroy()
// destroy internal data structures
void CUnitClientSpecific::Destroy()
{
    DeleteEffect(pWeaponParticles[0]);
    DeleteEffect(pWeaponParticles[1]);
    FreeEffect(pCastingParticle);
    FreeEffect(pPathParticle[0]);
    FreeEffect(pPathParticle[1]);
    //
    ReleaseMouthMimicTexture();
    bWoundTextureDataCached = false;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitClientSpecific::InventoryUpdated()
{
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitClientSpecific::SetOwner(CPlayer *pOwner)
{
    if (pOwner && !(ModRuntime::Current() && ModRuntime::Current()->CombatSetting("JMVGraphics2")))
    {
        //			D3DCOLORVALUE dcvColor = { 144.0f/255.0f, 191.0f/255.0f,
        // 255.0f/255.0f, 1.0f };
        const D3DCOLORVALUE dcvColor = { theSettings.fHeroLight[0], theSettings.fHeroLight[1],
                                         theSettings.fHeroLight[2], 1.0f };
        //			D3DCOLORVALUE dcvColor = { 255.0f/255.0f, 120.0f/255.0f,
        // 255.0f/255.0f, 1.0f };
        static_cast<CMapObjectClientSpecific *>(lpUnit->GetMapObjectSpecific())
            ->SetLight(dcvColor, theSettings.fHeroLightRadius);
    }
}
// segment processing
int CUnitClientSpecific::Segment()
{
    // blood test
    // if ( random.Get( 0, 10 ) == 0 )
    // HLDamaged( (ERPGHitLocations)random.Get( 0, RPG_NUM_HIT_LOCATIONS ), random.GetFloat( 0.1f, 1
    // ) );
    //
    if (lpUnit->locomotionSpeed == ULS_FROZEN)
        route.Freeze(1);
    if (lpUnit->animationSpeed == UAS_FROZEN)
        lpUnit->nAnimationStartSegment++;
    // SetExactPosition( GetWorldBase()->GetSegmentTimeCounter() );
    // hide/show weapon
    const SADEntry *pADEntry = lpUnit->GetCurrentADEntry();
    if (pADEntry)
        HideWeapon((pADEntry->dwFlags & ADB_WEAPONS) == 0);

    // hide/show shadow
    if (lpUnit->bDead && lpUnit->nStartAnimation + 50 < GetWorldBase()->GetSegmentTimeCounter())
        lpUnit->SetFlag(OBJ_NOSHADOW);
    else
    {
        if (!pADEntry || ((pADEntry->dwFlags & ADB_STATES) == ADB_STATE_LIE &&
                          (pADEntry->dwFlags & ADB_ACTIONS) != ADB_ACTION_CROSS))
            lpUnit->SetFlag(OBJ_NOSHADOW);
        else
            lpUnit->ResetFlag(OBJ_NOSHADOW);
    }
    // fade out
    if (fFadeOutAlpha < 1)
    {
        CObject3DClientSpecific *pObjSpec = static_cast<CObject3DClientSpecific *>(
            lpUnit->GetObjectSpecific());
        CMaterial *pMaterial = pObjSpec->GetFirstMaterial();
        while (pMaterial)
        {
            pMaterial->SetAlpha(fFadeOutAlpha);
            pMaterial = pObjSpec->GetNextMaterial();
        }
        fFadeOutAlpha = Min(1.0f, fFadeOutAlpha + 1.0f / 15.0f);
    }
    // temporal highlight processing
    if (nTemporalHighlightSegmentsCounter > 0)
    {
        nTemporalHighlightSegmentsCounter--;
        if (nTemporalHighlightSegmentsCounter == 0)
            static_cast<CObject3DClientSpecific *>(lpUnit->GetObjectSpecific())->Highlight(FALSE);
    }
    // mouth speech mode
    if (bMouthSpeakingMode)
    {
        if (nMouthSwitchTicks > 0)
            --nMouthSwitchTicks;
        else
        {
            nMouthSwitchTicks = 2 + Random(8);
            if (nCurrentMouthState == 0)
                MouthOpen();
            else
                MouthClose();
        }
    }
    //
    ProcessHideUnhide();
    StepsSound();
    // play acknowledgements
    if (!acks.empty())
    {
        SAckDescriptor &ack = acks.front();
        if (--ack.nBackCounter == 0) // play new ack
        {
            if (ack.bBlockUnit)
            {
                CUnit *pUnit = theWorldServer.GetUnit(lpUnit->GetID());
                if (pUnit)
                    pUnit->SetFlag(UNIT_BLOCKED);
            }
            if ((ack.dwType == ACK_HERO_ACK_ATTACK) ||
                (ack.dwType == ACK_HERO_NOTIFY_DECIDED_TO_ATTACK))
            {
                if (!theWorld.GetSoundManager()->IsFXValid(hLastAttackSound))
                {
                    if (BeginPlayAck(ack))
                        hLastAttackSound = ack.hSound;
                }
            }
            else
                BeginPlayAck(ack);
        }
        else if ((ack.nBackCounter < 0) && !theWorld.GetSoundManager()->IsFXValid(
                                               ack.hSound)) // remove ack, which have already played
        {
            // remove blocking...
            if (acks.front().bBlockUnit)
            {
                CUnit *pUnit = theWorldServer.GetUnit(lpUnit->GetID());
                if (pUnit)
                    pUnit->ResetFlag(UNIT_BLOCKED);
            }
            acks.pop_front();
        }
    }
    // special code for certain animations, now its idle in stand
    if (lpUnit->nCycleAnimation == 0xfe && !lpUnit->HasExtendedAnimationId() &&
        lpUnit->GetAnimationSystem2()->GetAnimationQueueSize() < 3)
        PumpAnimation(false);

    return 1;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitClientSpecific::StartFadeOut()
{
    fFadeOutAlpha = 0;
    CObject3DClientSpecific *pObjSpec = static_cast<CObject3DClientSpecific *>(
        lpUnit->GetObjectSpecific());
    CMaterial *pMaterial = pObjSpec->GetFirstMaterial();
    while (pMaterial)
    {
        pMaterial->SetAlpha(fFadeOutAlpha);
        pMaterial = pObjSpec->GetNextMaterial();
    }
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CUnitClientSpecific::FinalDataLoading()
{
    CObject3DClientSpecific *pObjSpec = static_cast<CObject3DClientSpecific *>(
        lpUnit->GetObjectSpecific());
    CMaterial *pMaterial = pObjSpec->GetFirstMaterial();
    while (pMaterial)
    {
        pMaterial->SetAlpha(fFadeOutAlpha);
        pMaterial = pObjSpec->GetNextMaterial();
    }
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
const SUDBRaceModel *CUnitClientSpecific::GetRace() const
{
    const SUDBUnitPrototype *pPrototype = theUnitDatabase.prototypes[lpUnit->GetSubtypeID()];
    if (!pPrototype)
        return 0;
    return theUnitDatabase.races[pPrototype->dwRaceIndex];
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CUnitClientSpecific::GetStepSound(int nTerrainType, CString &szSFXName) const
{
    const SUDBRaceModel *pRace = GetRace();
    if (!pRace || !pRace->dwNumSteps[nTerrainType])
        return FALSE;
    // don't play water sounds, if on the bridge!
    float fVal1 = theWorld.GetZValue(lpUnit->GetCPX(), lpUnit->GetCPY());
    float fVal2 = theWorld.GetBridgeZValue(lpUnit->GetCPX(), lpUnit->GetCPY());
    bool bOnBridge = fabs(fVal1 - fVal2) > 0.01f;
    if (bOnBridge)
        nTerrainType = TERRTYPE_STONE;

    if (lpUnit->IsLaying() &&
        ((nTerrainType != TERRTYPE_SWAMP) && (nTerrainType != TERRTYPE_WATER)))
        szSFXName.Format("steps\\human\\crawl\\%d.wav", Dice(6));
    else
        szSFXName.Format("%s\\%d.wav", static_cast<LPCSTR>(pRace->szSteps[nTerrainType]),
                         Dice(pRace->dwNumSteps[nTerrainType]));

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CUnitClientSpecific::GetActionSound(const SADEntry *pADEntry, float fTime1, float fTime2,
                                         CString &szSFXName, BYTE cSoundSet) const
{
    const SUDBUnitPrototype *pPrototype = lpUnit->GetVoicePrototype();
    if (pPrototype == 0)
        return FALSE;
    const SUDBRaceModel *pRace = theUnitDatabase.races[pPrototype->dwRaceIndex];
    int nStepNum = pADEntry->dwSFXFrame;
    //
    szSFXName.Empty();
    if (nStepNum == 0)
        return FALSE;
    if (!IsValInRangeLeft<BOOL>(float(nStepNum), fTime1, fTime2))
        return FALSE; // don't play

    if (cSoundSet != 0)
    {
        szSFXName = "weapons\\miss.wav";
        return TRUE;
    }
    //
    const DWORD *iter = std::find(pADEntry->dwSFXIndices, pADEntry->dwSFXIndices + 4, 0);
    if (iter == pADEntry->dwSFXIndices)
        return FALSE;

    DWORD dwIndex = pADEntry->dwSFXIndices[Random(iter - pADEntry->dwSFXIndices)];
    //
    CString szSFXPath(pRace->szSFXPath);
    if (pPrototype->szSFXPath.GetLength())
        szSFXPath = pPrototype->szSFXPath;
    //
    switch (pADEntry->dwFlags & ADB_ACTIONS)
    {
    case ADB_ACTION_ATTACK:
        // special case - attack with ranged weapon (bow/crossbow)
        if ((lpUnit->GetTypeID() == MOTID_HUMAN) && IsCurrentWeaponABow())
        {
            szSFXName.Format("weapons\\%d.wav", dwIndex);
            return TRUE;
        }
        else if (Random(100) < pRace->dwAttackProbability)
            szSFXName.Format("%s\\attack\\%d.wav", static_cast<LPCSTR>(szSFXPath), dwIndex);
        break;
    case ADB_ACTION_SUFFER:
        szSFXName.Format("%s\\hit\\%d.wav", static_cast<LPCSTR>(szSFXPath), dwIndex);
        break;
    case ADB_ACTION_DEATH:
        szSFXName.Format("%s\\death\\%d.wav", static_cast<LPCSTR>(szSFXPath), dwIndex);
        break;
    case ADB_ACTION_IDLE:
        if (Random(100) < pRace->dwIdleProbability)
            szSFXName.Format("%s\\idle\\%d.wav", static_cast<LPCSTR>(szSFXPath), dwIndex);
        break;
    case ADB_ACTION_SPECIAL: // tame, loot, steal and science animations
        switch ((pADEntry->dwFlags & ADB_MODIFIERS) / ADB_MODIFIER_SPECIAL_1)
        {
        case 19: // tame
            szSFXName = "heroes\\human\\skills\\tame.wav";
            break;
        case 20: // steal
            szSFXName = "heroes\\human\\skills\\steal.wav";
            break;
        case 21: // science
            szSFXName = "heroes\\human\\skills\\science.wav";
            break;
        case 22: // loot
        case 48:
        case 52:
        case 53:
            szSFXName = "heroes\\human\\skills\\loot.wav";
            break;
        }
        break;
    default:
        return FALSE;
    }
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitClientSpecific::StepsSound()
{
    const SADEntry *pADEntry = lpUnit->GetCurrentADEntry();
    float fTime = lpUnit->lpAnimationSystem->GetTime();
    int nSoundPriority = 0;
    CPlot3D unitCP = lpUnit->GetCP();
    CString szSFXName;
    int nStepNum;
    BOOL bPlay;
    int i;
    //
    if (pLastADEntry != pADEntry) // check for animation change
    {
        fLastTime = 0;
        pLastADEntry = pADEntry;
    }
    //
    if (fTime == fLastTime)
        return; // no animation time passed since last check
    //
    if (!pADEntry)
        return;

    switch (pADEntry->dwFlags & ADB_ACTIONS)
    {
    case ADB_ACTION_WALK:
    case ADB_ACTION_RUN:
        bPlay = FALSE;
        for (i = 0; i < 4; i++)
        {
            nStepNum = pADEntry->dwSteps[i] - 1;
            if (nStepNum == 0)
                break;
            if (IsValInRangeRight<BOOL>(float(nStepNum), fLastTime, fTime))
            {
                bPlay = TRUE;
                break;
            }
        }
        //
        if (!bPlay)
            break;
        //
        if (!lpUnit->HasSpellEffect(SPELL_ID_SILENCE))
            GetStepSound(
                ((CWorldClient *)GetWorld())->GetTerrain()->GetTerrainType(unitCP.x, unitCP.y),
                szSFXName);
        // add terrain prints
        if (!lpUnit->IsLaying())
        {
            const SUDBUnitPrototype *pPrototype = lpUnit->GetPrototype();
            if (pPrototype)
            {
                // потрясём землю (читай камеру) от крутых юнитов
                float fDetonation = pPrototype->fDetonation;
                if (fDetonation > 0)
                    theCamera.BeginEarthquake(lpUnit->GetCP(), 5, fDetonation);
            }
            //
            const SUDBRaceModel *pRace = lpUnit->GetRaceModel();
            if (pRace && (pRace->nFootprintType > -1) && (pRace->dwNumLegSegments > 0) &&
                (pRace->dwNumLegSegments < 9))
            {
                char pszLegName[4];
                DWORD dwFootType = DWORD(pRace->nFootprintType);
                bool bLeft = (i & 1) == 0;
                if (pRace->bFirstStepRight)
                    bLeft = !bLeft;
                sprintf(pszLegName, bLeft ? "ll%d" : "rl%d", pRace->dwNumLegSegments);
                CFigure *pLeg = lpUnit->MatchFigure(pszLegName);
                if (pLeg)
                {
                    CPlot3D plot = lpUnit->GetFigureWorldPosition(pLeg);
                    CPlot3D axis = lpUnit->GetYAxis();
                    float fLen = Max(0.0001f, sqrt(square(axis.x) + square(axis.y)));
                    float fAngle = axis.y / fLen;
                    fAngle = FP_PI - acos(CorrectCosAngle(fAngle));
                    if (axis.x < 0)
                        fAngle = -fAngle;
                    float fSizeX = pLeg->GetBoundingVolume()->ptMax.x;
                    float fSizeY = pLeg->GetBoundingVolume()->ptMax.y;
                    bool bPrintsVisible = (theSettings.nDrawVisibleOnly == 0) ||
                                          (lpUnit->CheckFlag(UNIT_VISIBLE) != 0);
                    //
                    float fDetection = pPrototype->percepts[RPGS_SIGHT];
                    switch (lpUnit->currentPose)
                    {
                    case UP_LAY:
                        fDetection *= AI_LAY_VISION;
                        break;
                    case UP_CRAWL:
                        fDetection *= AI_CRAWL_VISION;
                        break;
                    }
                    //
                    int nBloodType = theWorld.AddFootPrints(lpUnit, plot.x - axis.x * fSizeX / 2,
                                                            plot.y - axis.y * fSizeY / 2, fSizeX,
                                                            fSizeY, fAngle,
                                                            (bLeft << 16) | dwFootType, fDetection,
                                                            bPrintsVisible, nBloodyPrintsType);
                    if (nBloodyPrintsCounter > 0)
                        --nBloodyPrintsCounter;
                    else
                        nBloodyPrintsType = -1;
                    if (nBloodType >= 0)
                    {
                        nBloodyPrintsCounter = 4 + Random(4);
                        nBloodyPrintsType = nBloodType;
                    }
                }
            }
        }
        //
        break;
    case ADB_ACTION_ATTACK:
        // CRAP - запрет звуков атаки на базе
        if (theWorld.GetPlayer()->GetPlayer()->curInterface == IFACE_BASE)
            break;
        // CRAP ends
        theWorld.GetSoundManager()->CancelFX(hLastStoredIdleSFX);
        // потрясём землю (читай камеру) от ударов крутых юнитов
        if (IsValInRangeRight<BOOL>(float(pADEntry->dwHit), fLastTime, fTime))
        {
            float fDetonation = lpUnit->GetPrototype()->fDetonation;
            if (fDetonation > 0)
                theCamera.BeginEarthquake(lpUnit->GetCP(), 5, fDetonation);
        }
        // special 'miss' sound for HUMANs only
        if ((lpUnit->GetTypeID() == MOTID_HUMAN) && (lpUnit->nAnimationSound != 0))
        {
            if (GetActionSound(pADEntry, fLastTime, fTime, szSFXName, lpUnit->nAnimationSound) !=
                FALSE)
                theWorld.GetSoundManager()->BeginFX(szSFXName, 1000, unitCP, FP_MIN_SOUND_RADIUS,
                                                    FP_MAX_SOUND_RADIUS);
        }
        // break здесь не нужен, т.к. это только дополнение для атаки к выбору звука.
    case ADB_ACTION_DEATH:
        if ((pADEntry->dwFlags & ADB_ACTIONS) == ADB_ACTION_DEATH)
        {
            theWorld.GetSoundManager()->CancelFX(hLastStoredIdleSFX);
            theWorld.GetSoundManager()->CancelFX(hLastAttackSound);
        }
        // break здесь не нужен, т.к. это только дополнение для смерти к выбору звука.
    case ADB_ACTION_SPECIAL:
        nSoundPriority = 1000;
        // break здесь не нужен, т.к. это только дополнение для всех предыдущих звуков
    case ADB_ACTION_SUFFER:
    case ADB_ACTION_IDLE:
        if (GetActionSound(pADEntry, fLastTime, fTime, szSFXName, 0) == FALSE)
            szSFXName.Empty();
        break;
    }

    if (!szSFXName.IsEmpty())
    {
        HSOUND hSound = -1;
        if ((pADEntry->dwFlags & ADB_ACTIONS) == ADB_ACTION_ATTACK)
        {
            if (!theWorld.GetSoundManager()->IsFXValid(hLastAttackSound))
                hSound = hLastAttackSound = theWorld.GetSoundManager()->BeginFX(
                    szSFXName, nSoundPriority, unitCP, FP_MIN_SOUND_RADIUS, FP_MAX_SOUND_RADIUS);
        }
        else if (ModRuntime::Current() && (pADEntry->dwFlags & ADB_ACTIONS) == ADB_ACTION_IDLE &&
                 ModAudioPolicy::ReuseIdle(theWorld.GetSoundManager(), hLastStoredIdleSFX, szSFXName))
            hSound = hLastStoredIdleSFX;
        else
            hSound = theWorld.GetSoundManager()->BeginFX(szSFXName, nSoundPriority, unitCP,
                                                         FP_MIN_SOUND_RADIUS, FP_MAX_SOUND_RADIUS);
        // запомним звук последнего idle для того, чтобы убить его при проигрывании атаки
        if ((pADEntry->dwFlags & ADB_ACTIONS) == ADB_ACTION_IDLE)
            hLastStoredIdleSFX = hSound;
    }

    fLastTime = fTime;
    pLastADEntry = pADEntry;
}
/////////////////////////////////////////////////////////////////////////////////////
// определение флагов для анимаций различных действий юнита
/////////////////////////////////////////////////////////////////////////////////////
void CUnitClientSpecific::DeleteCastingParticle()
{
    DeleteEffect(pCastingParticle);
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitClientSpecific::SetPathParticles(CParticle *pP1, CParticle *pP2)
{
    DeleteEffect(pPathParticle[0]);
    DeleteEffect(pPathParticle[1]);
    if (pP1)
    {
        pPathParticle[0] = pP1;
        pP1->AddRef();
    }
    if (pP2)
    {
        pPathParticle[1] = pP2;
        pP2->AddRef();
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitClientSpecific::NewAnimationWasReceived(float fSegmentTimeCounter)
{
    const SADEntry *pEntry = NULL, *pEntryCycle = NULL;
    if (lpUnit->CheckFlag(UNIT_OLDROTATE))
        return;
    lpUnit->GetAnimationSystem2()->ClearAnimationsQueue();
    if (lpUnit->nCycleAnimation == 0xfe && !lpUnit->HasExtendedAnimationId())
        PumpAnimation(true);
    else
    {
        if (lpUnit->GetStartAnimationId() != 0xffffffffu)
            pEntry = lpUnit->GetAnimationByIndex(lpUnit->GetStartAnimationId());
        if (lpUnit->GetCycleAnimationId() != 0xffffffffu)
            pEntryCycle = lpUnit->GetAnimationByIndex(lpUnit->GetCycleAnimationId());
        ASSERT(pEntry || pEntryCycle);
        if (pEntry)
            lpUnit->GetAnimationSystem2()->SetAnimation(pEntry);
        if (pEntryCycle)
        {
            if (!pEntry)
                lpUnit->GetAnimationSystem2()->SetAnimation(pEntryCycle);
            else
                lpUnit->GetAnimationSystem2()->AddNextAnimation(pEntryCycle);
        }
    }
    // +1 нужен так как на клиенте время увеличивается после всех апдейтов
    float fpStep = 1 + fSegmentTimeCounter - lpUnit->nAnimationStartSegment;
    if (fpStep > 0 && lpUnit->lpAnimationSystem)
    {
        fpPrevSetPosTime = lpUnit->nAnimationStartSegment + fpStep;
        lpUnit->lpAnimationSystem->Step(fpStep);
        // нет необходимости играть звук для прошедшей части анимации ещё раз
        fLastTime = lpUnit->lpAnimationSystem->GetTime();
        pLastADEntry = lpUnit->lpAnimationSystem->GetCurrentAnimation();
    }
    else
        fpPrevSetPosTime = lpUnit->nAnimationStartSegment;
}
/////////////////////////////////////////////////////////////////////////////////////
DWORD CUnitClientSpecific::GetAnimationWeaponMask()
{
    const auto *mod = ModRuntime::Current();
    const bool addonAttacks = mod && mod->CombatSetting("JMVAttacks");
    DWORD dwWeapon = (!addonAttacks || lpUnit->dwCurrentWeapon < QI_MAX_WEAPONS)
        ? GetWeapon(lpUnit->dwCurrentWeapon) : DWORD(-1);
    if (dwWeapon != -1)
    {
        DWORD dwShape = CItem::GetShape(dwWeapon);
        if (dwShape != 0xffff)
        {
            if (!theItemDatabase.weapons[dwShape])
                return 0;
            dwWeapon = theItemDatabase.weapons[dwShape]->dwTypeID;
            // The addon maps newly added weapon groups to its base animation mask.
            if (addonAttacks && dwWeapon > 15)
                dwWeapon = RPGWST_SWORD;
            return dwWeapon < 32 ? (1u << dwWeapon) : 0;
        }
        ASSERT(0);
        return 0;
    }
    else if (lpUnit->rpgUserUnitStats.cNAWWeaponType < RPGWST_UNKNOWN ||
             (addonAttacks && lpUnit->rpgUserUnitStats.cNAWWeaponType > RPGWST_UNKNOWN &&
              lpUnit->rpgUserUnitStats.cNAWWeaponType <= 15))
        return (1u << lpUnit->rpgUserUnitStats.cNAWWeaponType);
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
// добавить в очередь анимации, соответствующие текущему состоянию юнита, используется
// когда одному состоянию могут соответствовать несколько анимаций
void CUnitClientSpecific::PumpAnimation(bool bSet)
{
    DWORD dwFlags;
    if (lpUnit->CheckFlag(UNIT_OLDROTATE))
        return;
    dwFlags = GetAnimationWeaponMask();
    if (lpUnit->GetTypeID() != MOTID_HUMAN)
        dwFlags |= ADB_STATE_ATTACK;
    else
        dwFlags |= ADB_STATE_NEUTRAL;
    switch (lpUnit->nCycleAnimation)
    {
    case 0xfe:
        dwFlags |= ADB_ACTION_IDLE | ADB_STAGE_CYCLE | ADB_MODIFIER_IDLE_STAY;
        break;
    default:
        ASSERT(0);
        break;
    }
    const SADEntry *pEntry;
    pEntry = lpUnit->GetAnimation(dwFlags);
    ASSERT(pEntry);
    if (pEntry)
    {
        if (bSet)
            lpUnit->GetAnimationSystem2()->SetAnimation(pEntry);
        else
            lpUnit->GetAnimationSystem2()->AddNextAnimation(pEntry);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitClientSpecific::PoseChanged()
{
}
/////////////////////////////////////////////////////////////////////////////////////
// change weapon - change models and notify to change texture
std::string CUnitClientSpecific::GetWeaponParticleNode(bool left) const
{
    const int slot = left ? ResolveVisualLeftHand(*lpUnit,
        [this](int i) { return GetWeapon(DWORD(i)); }) : int(lpUnit->dwCurrentWeapon);
    const DWORD weapon = slot >= 0 ? GetWeapon(DWORD(slot)) : DWORD(-1);
    if (weapon == DWORD(-1)) return left ? "lh3" : "rh3";
    const SIDBWeaponShape *shape = theItemDatabase.weapons[CItem::GetShape(weapon)];
    if (!shape) return left ? "lh3" : "rh3";
    return ModItemSpells::CastPointNode(true, shape->dwTypeID, int(shape->dwTextureTypeIndex), left);
}
void CUnitClientSpecific::RefreshWeaponParticles()
{
    DeleteEffect(pWeaponParticles[0]);
    DeleteEffect(pWeaponParticles[1]);
    auto *mod = ModRuntime::Current();
    if (!mod || !mod->CombatSetting("JMVParticles")) return;
    const DWORD right = GetCurrentWeapon();
    const SIDBWeaponShape *shape = right != DWORD(-1) ? theItemDatabase.weapons[CItem::GetShape(right)] : nullptr;
    // The source owns a pair only when the right hand holds a staff.
    if (!shape || (shape->dwTypeID != RPGWST_HANDS3 && shape->dwTypeID != RPGWST_HANDS7)) return;
    const int leftSlot = ResolveVisualLeftHand(*lpUnit,
        [this](int i) { return GetWeapon(DWORD(i)); });
    const DWORD weapons[2] = { right, leftSlot >= 0 ? GetWeapon(DWORD(leftSlot)) : DWORD(-1) };
    auto *manager = static_cast<CParticleManager *>(theWorld.GetParticleManager());
    for (int hand = 0; hand < 2; ++hand) {
        if (weapons[hand] == DWORD(-1)) continue;
        shape = theItemDatabase.weapons[CItem::GetShape(weapons[hand])];
        if (!shape || (shape->dwTypeID != RPGWST_HANDS3 && shape->dwTypeID != RPGWST_HANDS7)) continue;
        ETypeIDs type = PTID_STAFFBALLFIRE;
        if (shape->fpDamageDistributions[4] > 0) type = PTID_STAFFBALLWATER;
        else if (shape->fpDamageDistributions[5] > 0) type = PTID_STAFFBALLLIGHT;
        CParticle *particle = manager->CreateParticleSystem(type);
        pWeaponParticles[hand] = particle;
        particle->SetCP(CPlot3D(0, 0, 0));
        particle->Scale(0.125f);
        particle->SetPower(1);
        particle->AttachCarrier(lpUnit);
        particle->BindTo(hand ? CParticle::bpLStaff : CParticle::bpRStaff);
        particle->SetFlag(PS_NEED_CARRIER_TO_GENERATE);
        particle->SetFlag(PS_BIND_TO_BODYPART);
    }
}
BOOL CUnitClientSpecific::ChangeWeapon(DWORD dwToWeapon)
{
    const DWORD dwNewWeapon = GetWeapon(dwToWeapon);
    CStringArray szNames;
    CModelResourceEntry *pModel = ((CFigureManager *)theWorld.GetFigureManager())
                                      ->GetModel(lpUnit->GetMaskName());

    // The addon clears both hand variants from every quick slot before reloading the active pair.
    for (int slot = 0; slot < int(QI_MAX_WEAPONS); ++slot)
    {
        const DWORD item = GetWeapon(DWORD(slot));
        if (item == DWORD(-1)) continue;
        for (int hand = 0; hand < 2; ++hand)
        {
            CItemWeapon::GetWeaponModelNames(CItem::GetShape(item), szNames, hand == 0);
            for (int i = 0; i < szNames.GetSize(); ++i)
                lpUnit->RemoveFigure(szNames[i]);
        }
    }

    lpUnit->dwCurrentWeapon = dwToWeapon;
    bWeaponHidden = false;
    if (dwNewWeapon != DWORD(-1))
    {
        CItemWeapon::GetWeaponModelNames(CItem::GetShape(dwNewWeapon), szNames, true);
        for (int i = 0; i < szNames.GetSize(); ++i)
        {
            CFigure *pFigure = lpUnit->LoadFigure(szNames[i], pModel,
                                                  lpUnit->CheckFlag(OBJ_LOADED));
            if (pFigure) lpUnit->LinkFigure(pFigure);
        }

        const int leftSlot = ResolveVisualLeftHand(*lpUnit,
            [this](int slot) { return GetWeapon(DWORD(slot)); });
        const DWORD leftWeapon = leftSlot >= 0 ? GetWeapon(DWORD(leftSlot)) : DWORD(-1);
        if (leftWeapon != DWORD(-1) && leftSlot != int(dwToWeapon))
        {
            CItemWeapon::GetWeaponModelNames(CItem::GetShape(leftWeapon), szNames, false);
            for (int i = 0; i < szNames.GetSize(); ++i)
            {
                CFigure *pFigure = lpUnit->LoadFigure(szNames[i], pModel,
                                                      lpUnit->CheckFlag(OBJ_LOADED));
                if (pFigure) lpUnit->LinkFigure(pFigure);
            }
        }
        bArrowHidden = false;
        HideArrow(true);
    }

    RefreshWeaponParticles();
    if (bBriefingWeaponForcedHidden) HideWeapon(true);
    NotifyWeaponUpdated();
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CUnitClientSpecific::GetCastPointPosition(CPlot3D &position)
{
#if !(defined(_A4) || defined(__OBJECT_CONVERTOR_A4__))
    CString szCastPointName;
    if (CItemWeapon::GetCastPointName(GetCurrentWeapon(), szCastPointName) == false)
        szCastPointName = "baserh3";
    //
    const CAnimationNode *pNode = lpUnit->GetAnimationSystem2()->FindNode(szCastPointName);
    if (pNode == 0)
        return false;
    position = pNode->GetPosition();
#endif // !( defined(_A4) || defined(__OBJECT_CONVERTOR_A4__) )
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitClientSpecific::SetUnitPosition(float fpTime, float fpStep)
{
    int nX, nY;
    CPlot3D pos, vec;
    float fpSpeed;
    bool bMoved, bRotating;
    if (lpUnit->CheckFlag(UNIT_DONTMOVE))
        return;

    pos = lpUnit->GetCP();
    pos.x = Meter2AITile(pos.x);
    pos.y = Meter2AITile(pos.y);
    bMoved = lpUnit->walker.GetPosition(GetWorldBase(), route, fpTime, bRotating, pos, vec, fpSpeed,
                                        nX, nY);
    // ASSERT( bMoved );
    pos.x = AITile2Meter(pos.x);
    pos.y = AITile2Meter(pos.y);
    pos.z = lpUnit->CalcZ(pos.x, pos.y) + lpUnit->fpBaseZ;
    GetWorldBase()->MoveObject(lpUnit, pos);
    // we don't need orientation in the object viewer
    if (bFirstSetpos)
    {
        bFirstSetpos = false;
        lpUnit->OrientInYDirection(vec, FALSE);
    }
    if (lpUnit->CheckFlag(UNIT_OLDROTATE))
        lpUnit->OrientInYDirection(vec, TRUE);
    else if (fpStep > 0)
    {
        CPlot3D ptYDir = lpUnit->GetYAxis();
        float fpSrcAngle = atan2(-ptYDir.y, -ptYDir.x);
        float fpDstAngle = atan2(vec.y, vec.x);
        while (fabs(fpDstAngle - fpSrcAngle) > 3.1416f)
        {
            if (fpDstAngle > fpSrcAngle)
                fpSrcAngle += FP_2PI;
            else
                fpDstAngle += FP_2PI;
        }
        float fpKoef = exp(-fpStep * 0.3f);
        float fpAngle = fpSrcAngle * fpKoef + fpDstAngle * (1 - fpKoef);
        vec = CPlot3D(cos(fpAngle), sin(fpAngle), 0);

        lpUnit->SetOrientationXY(vec);
    }

    // 15 = количество кадров анимации в секунду
    const SADEntry *pAD = lpUnit->GetCurrentAnimation();
    if (pAD && pAD->fpSpeed != 0)
    {
        float fpAnimationSpeed = pAD->GetMovementSpeed(lpUnit->complection.tall);
        lpUnit->SetAnimationCoeff(15 * fpSpeed * fabs(vec) / (Meter2AITile(fpAnimationSpeed)));
    }
    else
    {
        if (pAD)
            lpUnit->SetAnimationCoeff(lpUnit->GetAnimationCoeff(pAD));
        else
            lpUnit->SetAnimationCoeff(1.0f);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitClientSpecific::NewRouteReceived()
{
    SetUnitPosition(fpPrevSetPosTime, 0);
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitClientSpecific::SetExactPosition(float fpTime)
{
    // calc time step
    float fpStep = fpTime - fpPrevSetPosTime;
    if (fpStep < 0)
    {
        CPlot3D pos = lpUnit->GetCP();
        pos.z = lpUnit->CalcZ(pos.x, pos.y) + lpUnit->fpBaseZ;
        GetWorldBase()->MoveObject(lpUnit, pos);
        // ASSERT(0&&"Wrong time counting");
        return;
    }
    if (fpStep > 500)
        fpStep = 500 + random.Get(0, 100);
    fpPrevSetPosTime = fpTime;
    //
    if (lpUnit->IsInvisible())
        ((CObject3DClientSpecific *)(lpUnit)->GetObjectSpecific())->StepInvisibility(fpStep);
    if (lpUnit->IsStuned())
        ((CObject3DClientSpecific *)(lpUnit)->GetObjectSpecific())->StepStun(fpStep);
    if (lpUnit->locomotionSpeed == ULS_NORMAL)
        SetUnitPosition(fpTime, fpStep);
    else
        lpUnit->SetAnimationCoeff(1.0f);
    // change animation
    if (lpUnit->animationSpeed == UAS_MOVEMENT || (lpUnit->animationSpeed != UAS_FROZEN && ModRuntime::Current() && ModRuntime::Current()->CombatSetting("AttackAnimSpeed")))
        fpStep *= lpUnit->fpAnimationCoeff;
    if (lpUnit->lpAnimationSystem && lpUnit->animationSpeed != UAS_FROZEN)
    {
        lpUnit->lpAnimationSystem->Step(fpStep);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// bodypart lists for wear/remove functions
// plate   : bd, rh1, rh2, rh3, lh1, lh2, lh3
// leggings: hp, ll1, ll2, ll3, rl1, rl2, rl3
// helm    : hd, hr
static const char pszModelBodyparts[3][7][4] = { { "bd", "rh1", "rh2", "rh3", "lh1", "lh2", "lh3" },
                                                 { "hp", "rl1", "rl2", "rl3", "ll1", "ll2", "ll3" },
                                                 { "hr", "hd", "", "", "", "", "" } };
void FormBodypartNames(const SUDBUnitPrototype *pPrototype, const std::vector<DWORD> &armors,
                       const std::vector<DWORD> &weapons, std::vector<std::string> &szNames,
                       std::vector<std::string> &szBPNames)
{
    const SUDBRaceModel *pRace = theUnitDatabase.races[pPrototype->dwRaceIndex];
    int i, j;
    // armors
    std::vector<DWORD>::const_iterator pos;
    for (pos = armors.begin(); pos != armors.end(); ++pos)
    {
        if ((((*pos) >> 16) & 0xFFFF) >= theItemDatabase.armors.GetNumEntries())
            continue;
        const SIDBItemGeneralShape *pShape = theItemDatabase.armors[((*pos) >> 16) & 0xFFFF];
        char buff[1024];
        int nModelShapeIndex = -1;
        // select model shape index
        switch (pShape->dwTypeID)
        {
        case RPGAST_PLATE:
            nModelShapeIndex = 0;
            break;
        case RPGAST_LEGGINGS:
            nModelShapeIndex = 1;
            break;
        case RPGAST_HELM:
            nModelShapeIndex = 2;
            break;
        }
        // form bodypart names
        if (nModelShapeIndex != -1)
        {
            for (i = 0; i < 7; i++)
            {
                if (pszModelBodyparts[nModelShapeIndex][i][0] == 0)
                    break; // no more bodyparts
                if (strcmp(pszModelBodyparts[nModelShapeIndex][i], "hr") ==
                    0) // hair bodypart name correction
                {
                    if (pPrototype->dwHair == -1)
                        continue;
                    sprintf(buff, "hr.%.2d.armor%.2d", pPrototype->dwHair,
                            pShape->dwTextureTypeIndex);
                }
                else
                    sprintf(buff, "%s.armor%.2d", pszModelBodyparts[nModelShapeIndex][i],
                            pShape->dwTextureTypeIndex);
                szNames.push_back(buff);
            }
        }
    }
    // weapons
    for (pos = weapons.begin(); pos != weapons.end(); ++pos)
        CItemWeapon::GetWeaponModelNames(*pos, szNames);
    // for native (w/o dressing) bodypart names
    char pszBodypartName[8];
    for (i = 0; i < 3; ++i)
    {
        for (j = 0; j < 7; ++j)
        {
            if (pszModelBodyparts[i][j] == 0)
                break;
            //
            if (strcmp(pszModelBodyparts[i][j], "hr") == 0) // hair bodypart name correction
            {
                if (pPrototype->dwHair == -1)
                    continue;
                sprintf(pszBodypartName, "hr.%.2d", pPrototype->dwHair);
                szBPNames.push_back(pszBodypartName);
            }
            else
                szBPNames.push_back(pszModelBodyparts[i][j]);
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CUnitClientSpecific::NotifyItemsUpdated()
{
    CString szString;
    //
    if (lpUnit->GetTypeID() == MOTID_HUMAN)
    {
        // primary texture
        CObject3DClientSpecific *lpSpec = (CObject3DClientSpecific *)lpUnit->GetObjectSpecific();
        CTexture *pTexture = lpSpec->GetPrimaryTexture();
        auto* primaryTextureManager=static_cast<CTextureManager*>(theWorld.GetTextureManager());
        if(primaryTextureManager->SharePrimaryCharacterAtlas()) {
            if(AssemblePrimaryTexture(szString))lpSpec->RebindPrimaryTexture(szString);
        }
        else if (AssemblePrimaryTexture(szString) || (szString != pTexture->GetName()))
        {
            pTexture->SetName(szString);
            pTexture->ResetFlag(TEX_LOADED);
            ((CTextureManager *)pTexture->GetManager())->LoadTexture(pTexture);
        }
        // secondary texture
        pTexture = lpSpec->GetSecondaryTexture();
        auto* textureManager=static_cast<CTextureManager*>(theWorld.GetTextureManager());
        if(textureManager->ShareSecondaryCharacterAtlas()) {
            if(AssembleSecondaryTexture(szString))lpSpec->RebindSecondaryTexture(szString);
        }
        else if (AssembleSecondaryTexture(szString) || (pTexture && (szString != pTexture->GetName())))
        {
            if (pTexture)
            {
                pTexture->SetName(szString);
                pTexture->ResetFlag(TEX_LOADED);
                ((CTextureManager *)pTexture->GetManager())->LoadTexture(pTexture);
            }
        }
    }

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CUnitClientSpecific::NotifyWeaponUpdated()
{
    if (lpUnit->GetTypeID() == MOTID_HUMAN)
    {
        CString szString;
        CObject3DClientSpecific *lpSpec = (CObject3DClientSpecific *)lpUnit->GetObjectSpecific();
        CTexture *pTexture = lpSpec->GetSecondaryTexture();
        auto* textureManager=static_cast<CTextureManager*>(theWorld.GetTextureManager());
        if(textureManager->ShareSecondaryCharacterAtlas()) {
            if(AssembleSecondaryTexture(szString))lpSpec->RebindSecondaryTexture(szString);
        }
        else if (AssembleSecondaryTexture(szString) || (pTexture && (szString != pTexture->GetName())))
        {
            if (pTexture)
            {
                pTexture->SetName(szString);
                pTexture->ResetFlag(TEX_LOADED);
                ((CTextureManager *)pTexture->GetManager())->LoadTexture(pTexture);
            }
        }
    }

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
template<class WeaponAt> static int ResolveVisualLeftHand(const CUnit& unit, WeaponAt weaponAt);
void CUnitClientSpecific::FormItemMMPData(BOOL bPrimary,
                                          CArray<SItemMMPData, SItemMMPData &> &userdata)
{
    if (bPrimary)
    {
        userdata.RemoveAll();
        userdata.SetSize(RPG_NUM_ARMOR_TYPES + 1); // skin + all armors w/o helm
        // fill skin user data
        userdata[0].dwSubtypeID = lpUnit->GetSubtypeID();
        userdata[0].tidTypeID = MOTID_HUMAN;
        userdata[0].dwType = -1;
        // fill user data from current armors
        for (int i = 0; i < RPG_NUM_ARMOR_TYPES; ++i)
        {
            DWORD dwItem = armors[i];
            if (dwItem != -1)
            {
                const SIDBItemGeneralShape *pShape = theItemDatabase
                                                         .armors[CItem::GetShape(dwItem)];
                if (pShape != 0)
                {
                    userdata[i + 1].dwSubtypeID = dwItem;
                    userdata[i + 1].tidTypeID = ITID_ITEM_ARMOR;
                    userdata[i + 1].dwType = pShape->dwTypeID;
                }
            }
        }
    }
    else
    {
        // fill user data from current helm and weapon
        userdata.RemoveAll();
        userdata.SetSize(3); // helm + right-hand weapon + resolved left-hand item
        for (int i = 0; i < 3; ++i) {
            userdata[i].dwSubtypeID = DWORD(-1);
            userdata[i].tidTypeID = ITID_ITEM_WEAPON;
            userdata[i].dwType = DWORD(-1);
        }
        // current weapon
        DWORD dwItem = armors[RPGAST_HELM];
        if (dwItem != -1) // helm
        {
            const SIDBItemGeneralShape *pShape = theItemDatabase.armors[CItem::GetShape(dwItem)];
            if (pShape != 0)
            {
                userdata[0].dwSubtypeID = dwItem;
                userdata[0].tidTypeID = ITID_ITEM_ARMOR;
                userdata[0].dwType = pShape->dwTypeID;
            }
        }
        dwItem = GetCurrentWeapon();
        if (dwItem != -1) // weapon
        {
            const SIDBItemGeneralShape *pShape = theItemDatabase.weapons[CItem::GetShape(dwItem)];
            if (pShape != 0)
            {
                userdata[1].dwSubtypeID = dwItem;
                userdata[1].tidTypeID = ITID_ITEM_WEAPON;
                userdata[1].dwType = pShape->dwTypeID;
            }
        }
        const int leftSlot = ResolveVisualLeftHand(*lpUnit, [this](int slot) { return GetWeapon(DWORD(slot)); });
        dwItem = leftSlot >= 0 ? GetWeapon(DWORD(leftSlot)) : DWORD(-1);
        if (dwItem != DWORD(-1)) {
            const SIDBItemGeneralShape *pShape = theItemDatabase.weapons[CItem::GetShape(dwItem)];
            if (pShape) {
                userdata[2].dwSubtypeID = dwItem;
                userdata[2].tidTypeID = ITID_ITEM_WEAPON;
                userdata[2].dwType = pShape->dwTypeID;
            }
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// check, is item (armor of weapon) belong to this armor layer ?
bool IsItemInArmorLayer(DWORD dwShape, ETypeIDs tidTypeID, DWORD dwALIndex)
{
    switch (tidTypeID)
    {
    case ITID_ITEM_ARMOR: {
        const SIDBItemGeneralShape *pShape = theItemDatabase.armors[dwShape];
        if (pShape == 0)
            return false;
        switch (pShape->dwTypeID)
        {
        case RPGAST_SHIRT:
        case RPGAST_PANTS:
        case RPGAST_BOOTS:
        case RPGAST_GLOVES:
            return dwALIndex == 1;
        case RPGAST_PLATE:
        case RPGAST_LEGGINGS:
            return dwALIndex == 2;
        case RPGAST_HELM:
            return dwALIndex == 3;
        }
    }
        return false;
    case ITID_ITEM_WEAPON:
        return dwALIndex == 4;
    }

    return false;
}
/////////////////////////////////////////////////////////////////////////////////////
// Keep offhand atlas selection aligned with the installed addon's currentWeapons::GetWeapons.
template<class WeaponAt> static int ResolveVisualLeftHand(const CUnit& unit, WeaponAt weaponAt)
{
    const int currentSlot = int(unit.GetCurrentWeaponSlotIndex());
    if (currentSlot < 0 || currentSlot >= int(QI_MAX_WEAPONS) || weaponAt(currentSlot) == DWORD(-1)) return -1;
    int shieldSlot = -1;
    for (int i = 0; i < int(QI_MAX_WEAPONS); ++i) {
        DWORD item = weaponAt(i);
        if (item == DWORD(-1)) continue;
        const auto shape = theItemDatabase.weapons[CItem::GetShape(item)];
        if (shape && shape->dwTypeID == RPGWST_HANDS1) { shieldSlot = i; break; }
    }
    const int firstSlot = 0, dualSlot = 1;
    const DWORD currentItem = weaponAt(currentSlot), firstItem = weaponAt(firstSlot), dualItem = weaponAt(dualSlot);
    if (firstItem == DWORD(-1) || dualItem == DWORD(-1)) return -1;
    const auto current = theItemDatabase.weapons[CItem::GetShape(currentItem)];
    const auto first = theItemDatabase.weapons[CItem::GetShape(firstItem)];
    const auto dual = theItemDatabase.weapons[CItem::GetShape(dualItem)];
    if (!current || !first || !dual) return -1;
    const int currentType = int(current->dwTypeID), firstType = int(first->dwTypeID), dualType = int(dual->dwTypeID);
    const bool currentTwoHanded = ModStats::IsTwohandedWeapon(currentType, &unit);
    const bool firstTwoHanded = ModStats::IsTwohandedWeapon(firstType, &unit);
    const bool dualTwoHanded = ModStats::IsTwohandedWeapon(dualType, &unit);
    const bool dualIsShield = dualType == RPGWST_HANDS1;
    const bool currentIsStaff = currentType == RPGWST_HANDS3 || currentType == RPGWST_HANDS7;
    const bool firstIsStaff = firstType == RPGWST_HANDS3 || firstType == RPGWST_HANDS7;
    const bool dualIsStaff = dualType == RPGWST_HANDS3 || dualType == RPGWST_HANDS7;
    if (currentTwoHanded) return -1;
    if (currentIsStaff || dualIsStaff || firstIsStaff) {
        if (currentSlot == dualSlot) {
            if (dualIsShield) return shieldSlot;
            return !firstTwoHanded && firstIsStaff && dualIsStaff ? firstSlot : -1;
        }
        if (shieldSlot >= 0) return shieldSlot;
        return !dualTwoHanded && dualIsStaff && currentIsStaff ? dualSlot : -1;
    }
    if (currentSlot == dualSlot) {
        if (dualIsShield) return shieldSlot;
        if (!firstTwoHanded) return firstSlot;
        return -1;
    }
    if (shieldSlot >= 0) return shieldSlot;
    return !dualTwoHanded ? dualSlot : -1;
}

static const DWORD UCS_PRIMARY_TEXTURE_SIZE = 256;
static const DWORD UCS_SECONDARY_TEXTURE_SIZE = 128;
/////////////////////////////////////////////////////////////////////////////////////
// assemble whole texture (primary or secondary)
BOOL CUnitClientSpecific::AssembleTexture(BOOL bPrimaryTexture, CString &szTextureName)
{
    LPTEXTUREMANAGER pTextureManager = (LPTEXTUREMANAGER)theWorld.GetTextureManager();
    CArray<SItemMMPData, SItemMMPData &> userdata;
    CString szResultFileName1, szResultFileName2, szFileName;
    STextureSetDescription set;
    set.characterAtlas = bPrimaryTexture ? 1 : 2;
    int i;
    // fill user data from current armors and weapon
    FormItemMMPData(bPrimaryTexture, userdata);
    //
    set.data.SetSize(userdata.GetSize() * sizeof(SItemMMPData));
    memcpy(set.data.GetData(), userdata.GetData(), set.data.GetSize());
    //
    if (bPrimaryTexture) // assemble primary texture
    {
        szResultFileName1.Format("mo%dp", lpUnit->GetID());
        szResultFileName2.Format("prot%dp", lpUnit->GetSubtypeID());
        // Baseline private fallback; canonical sharing detaches before mouth writes.
        if (UseHDCharacterTextures()) szResultFileName2 = szResultFileName1;
        set.dwTextureDimention = UCS_PRIMARY_TEXTURE_SIZE;
        // skin, non-shaped armors, shaped armos w/o helm
        // skin
        if (theUnitDatabase.prototypes[lpUnit->GetSubtypeID()])
        {
            DWORD dwSkinIndex = theUnitDatabase.prototypes[lpUnit->GetSubtypeID()]->dwSkinIndex;
            szFileName.Format("%sskin_%.2d.mmp", lpUnit->GetMaskName(), dwSkinIndex);
        }
        else
            szFileName.Format("%sskin_00.mmp", lpUnit->GetMaskName());
        set.szTextures.Add(szFileName);
        // non-shaped armors
        for (i = RPGAST_SHIRT; i <= RPGAST_GLOVES; ++i)
        {
            DWORD dwItem = armors[i];
            if (dwItem != -1)
            {
                CItemArmor::FormTextureNameMask(szFileName, CItem::GetShape(dwItem),
                                                CItem::GetMaterial(dwItem));
                szFileName = lpUnit->GetMaskName() + szFileName + ".mmp";
                set.szTextures.Add(szFileName);
            }
        }
        // shaped armors w/o helm
        {
            // wear plate and leggings with the 'wear order' taken in account
            DWORD dwPlate = armors[RPGAST_PLATE];
            DWORD dwLeggings = armors[RPGAST_LEGGINGS];
            if ((dwPlate != -1) && (dwLeggings != -1))
            {
                if (CItemArmor::GetWearOrder(CItem::GetShape(dwLeggings)) <
                    CItemArmor::GetWearOrder(CItem::GetShape(dwPlate)))
                {
                    CItemArmor::FormTextureNameMask(szFileName, CItem::GetShape(dwLeggings),
                                                    CItem::GetMaterial(dwLeggings));
                    szFileName = lpUnit->GetMaskName() + szFileName + ".mmp";
                    set.szTextures.Add(szFileName);
                    CItemArmor::FormTextureNameMask(szFileName, CItem::GetShape(dwPlate),
                                                    CItem::GetMaterial(dwPlate));
                    szFileName = lpUnit->GetMaskName() + szFileName + ".mmp";
                    set.szTextures.Add(szFileName);
                }
                else
                {
                    CItemArmor::FormTextureNameMask(szFileName, CItem::GetShape(dwPlate),
                                                    CItem::GetMaterial(dwPlate));
                    szFileName = lpUnit->GetMaskName() + szFileName + ".mmp";
                    set.szTextures.Add(szFileName);
                    CItemArmor::FormTextureNameMask(szFileName, CItem::GetShape(dwLeggings),
                                                    CItem::GetMaterial(dwLeggings));
                    szFileName = lpUnit->GetMaskName() + szFileName + ".mmp";
                    set.szTextures.Add(szFileName);
                }
            }
            else
            {
                DWORD dwItem = dwPlate != -1 ? dwPlate : dwLeggings;
                if (dwItem != -1)
                {
                    CItemArmor::FormTextureNameMask(szFileName, CItem::GetShape(dwItem),
                                                    CItem::GetMaterial(dwItem));
                    szFileName = lpUnit->GetMaskName() + szFileName + ".mmp";
                    set.szTextures.Add(szFileName);
                }
            }
        }
    }
    else // assemble secondary texture
    {
        szResultFileName1.Format("mo%ds", lpUnit->GetID());
        szResultFileName2.Format("prot%ds", lpUnit->GetSubtypeID());
        set.dwTextureDimention = UCS_SECONDARY_TEXTURE_SIZE;
        // helm, weapon
        // helm
        DWORD dwItem = armors[RPGAST_HELM];
        if (dwItem != -1)
        {
            CItemArmor::FormTextureNameMask(szFileName, CItem::GetShape(dwItem),
                                            CItem::GetMaterial(dwItem));
            szFileName = lpUnit->GetMaskName() + szFileName + ".mmp";
            set.equipmentSlots[2] = int(set.szTextures.GetSize());
            set.szTextures.Add(szFileName);
        }
        // weapon
        dwItem = GetCurrentWeapon();
        if (dwItem != -1)
        {
            CItemWeapon::FormTextureNameMask(szFileName, CItem::GetShape(dwItem),
                                             CItem::GetMaterial(dwItem));
            szFileName = lpUnit->GetMaskName() + szFileName + ".mmp";
            set.equipmentSlots[0] = int(set.szTextures.GetSize());
            set.szTextures.Add(szFileName);
        }
        const int leftSlot = ResolveVisualLeftHand(*lpUnit, [this](int slot) { return GetWeapon(DWORD(slot)); });
        dwItem = leftSlot >= 0 ? GetWeapon(DWORD(leftSlot)) : DWORD(-1);
        if (dwItem != DWORD(-1)) {
            const SIDBItemGeneralShape *pShape = theItemDatabase.weapons[CItem::GetShape(dwItem)];
            if (pShape) {
                CItemWeapon::FormTextureNameMask(szFileName, CItem::GetShape(dwItem), CItem::GetMaterial(dwItem));
                szFileName = lpUnit->GetMaskName() + szFileName + ".mmp";
                const int atlasSlot = pShape->dwTypeID == RPGWST_HANDS1 ? 3 : 1;
                set.equipmentSlots[atlasSlot] = int(set.szTextures.GetSize());
                set.szTextures.Add(szFileName);
            }
        }
    }
    //
    return pTextureManager->ComposeTexture(szTextureName, set, szResultFileName1, szResultFileName2,
                                           3, 0, MMP_FORMAT_5551);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CUnitClientSpecific::AssemblePrimaryTexture(CString &szTextureName)
{
    return AssembleTexture(TRUE, szTextureName);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CUnitClientSpecific::AssembleSecondaryTexture(CString &szTextureName)
{
    return AssembleTexture(FALSE, szTextureName);
}
/////////////////////////////////////////////////////////////////////////////////////
bool CUnitClientSpecific::FormWoundsMMPData(CBYTEArray &userdata)
{
    bool bRetVal = false;
    // form by hit location
    for (int i = 0; i < RPG_NUM_HIT_LOCATIONS; ++i)
    {
        if ((lpUnit->rpgUserUnitStats.hls[i].state == RPGALS_NOTUSED) ||
            (lpUnit->rpgUserUnitStats.hls[i].wMaxHP == 0))
            userdata[i] = 0;
        else
        {
            float fDamageLevel = float(short(lpUnit->rpgUserUnitStats.hls[i].wHP)) /
                                 float(lpUnit->rpgUserUnitStats.hls[i].wMaxHP);
            if (fDamageLevel == 1)
                userdata[i] = 0;
            else if (fDamageLevel <= 1.0f / float(lpUnit->rpgUserUnitStats.hls[i].wMaxHP))
                userdata[i] = 3, bRetVal = true;
            else if (fDamageLevel <= 0.33f)
                userdata[i] = 2, bRetVal = true;
            else if (fDamageLevel < 0.75f)
                userdata[i] = 1, bRetVal = true;
            else
                userdata[i] = 0;
        }
    }
    // correct paired hit locations for non-human units
    if (lpUnit->GetTypeID() != MOTID_HUMAN)
    {
        userdata[RPGHL_RLEG] = userdata[RPGHL_LLEG] = Max(userdata[RPGHL_LLEG],
                                                          userdata[RPGHL_RLEG]);
        userdata[RPGHL_RHAND] = userdata[RPGHL_LHAND] = Max(userdata[RPGHL_LHAND],
                                                            userdata[RPGHL_RHAND]);
    }
    else // check 'shaped' armors for human units
    {
        // leggings: left leg, right leg
        DWORD dwArmor = armors[RPGAST_LEGGINGS];
        if (dwArmor != -1)
        {
            const SIDBArmorShape *pShape = theItemDatabase.armors[CItem::GetShape(dwArmor)];
            if (!pShape->bApplyWounds)
                userdata[RPGHL_LLEG] = userdata[RPGHL_RLEG] = 0;
        }
        // plate: body, left hand, right hand
        dwArmor = armors[RPGAST_PLATE];
        if (dwArmor != -1)
        {
            const SIDBArmorShape *pShape = theItemDatabase.armors[CItem::GetShape(dwArmor)];
            if (!pShape->bApplyWounds)
                userdata[RPGHL_BODY] = userdata[RPGHL_LHAND] = userdata[RPGHL_RHAND] = 0;
        }
        // helm: head
        dwArmor = armors[RPGAST_HELM];
        if (dwArmor != -1)
        {
            const SIDBArmorShape *pShape = theItemDatabase.armors[CItem::GetShape(dwArmor)];
            if (!pShape->bApplyWounds)
                userdata[RPGHL_HEAD] = 0;
        }
    }

    return bRetVal;
}
/////////////////////////////////////////////////////////////////////////////////////
static const char *pszWoundBPNames[] = { "hd", // head
                                         "bd", // body
                                         "lh", // left hand
                                         "rh", // right hand
                                         "ll", // left leg
                                         "rl", // right leg
                                         "" };
static const char *pszWoundMonsterBPNames[] = { "hd", // head
                                                "bd", // body
                                                "h",  // left hand
                                                "h",  // right hand
                                                "l",  // left leg
                                                "l",  // right leg
                                                "" };

BOOL CUnitClientSpecific::AssembleWoundsTexture(const CBYTEArray *requiredData)
{
    STextureSetDescription set;
    CString szFileName, szTextureName;
    // form user data
    set.data.SetSize(RPG_NUM_HIT_LOCATIONS); // ARRAY BOUNDS CHECK
    if (FormWoundsMMPData(set.data) == false)
        return FALSE;
    if (requiredData && (requiredData->GetSize() != set.data.GetSize() ||
        std::memcmp(requiredData->GetData(), set.data.GetData(), set.data.GetSize()) != 0))
        return FALSE;
    // setup dimention
    set.dwTextureDimention = 64;
    if (lpUnit->GetTypeID() == MOTID_HUMAN)
        set.dwTextureDimention = 128;
    // compose wound layer names, based on the user data
    for (int i = 0; i < RPG_NUM_HIT_LOCATIONS; ++i)
    {
        if (set.data[i] == 0)
            continue;
        //
        if (lpUnit->GetTypeID() == MOTID_HUMAN)
            szFileName.Format("%s%sw%d.mmp", lpUnit->GetMaskName(), pszWoundBPNames[i],
                              int(set.data[i]));
        else
            szFileName.Format("%s%sw%d.mmp", lpUnit->GetMaskName(), pszWoundMonsterBPNames[i],
                              int(set.data[i]));
        set.szTextures.Add(szFileName);
    }
    //
    szFileName.Format("mo%dw", lpUnit->GetID());
    LPTEXTUREMANAGER pTextureManager = (LPTEXTUREMANAGER)theWorld.GetTextureManager();
    if (requiredData) {
        // Recovery must not replace a missing original layer with zero pixels.
        for(int i=0;i<set.szTextures.GetSize();++i) {
            auto* header=reinterpret_cast<SMMPFileHeader*>(pTextureManager->GetFile(set.szTextures[i]));
            if(!header)return FALSE;
            struct CloseLayer {CTextureManager* manager;BYTE* bytes;~CloseLayer(){manager->CloseFile(bytes);}} close{pTextureManager,reinterpret_cast<BYTE*>(header)};
            if(header->dwDimentionX!=set.dwTextureDimention ||
               header->dwDimentionY!=set.dwTextureDimention)return FALSE;
        }
    }
    return pTextureManager->ComposeTexture(szTextureName, set, szFileName, szFileName, 3,
                                           0 /*0x00FFFFFF*/, MMP_FORMAT_8888);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CUnitClientSpecific::EnsureWoundsTextureReady()
{
    if(!bWoundTextureDataCached)return FALSE;
    CBYTEArray current;
    current.SetSize(RPG_NUM_HIT_LOCATIONS);
    if(!FormWoundsMMPData(current))return FALSE;
    for(int i=0;i<RPG_NUM_HIT_LOCATIONS;++i)
        if(current[i]!=woundTextureData[i])return FALSE;
    auto* specific=static_cast<CObject3DClientSpecific*>(lpUnit->GetObjectSpecific());
    if(!specific)return FALSE;
    char name[128];std::sprintf(name,"mo%dw",lpUnit->GetID());
    // Try existing RES/disk/CPU data first; actual surface+userdata are required.
    if(specific->LoadWounds(name,&current))return TRUE;
    // Existing owner forms exactly the same userdata before rebuilding its MMP.
    // No HP/cache-state change is used to force this local recovery.
    if(!AssembleWoundsTexture(&current))return FALSE;
    return specific->LoadWounds(name,&current);
}
BOOL CUnitClientSpecific::NotifyHitPointsUpdated()
{
    CBYTEArray currentWoundData;
    currentWoundData.SetSize(RPG_NUM_HIT_LOCATIONS);
    if (!FormWoundsMMPData(currentWoundData))
    {
        bWoundTextureDataCached = false;
        ((CObject3DClientSpecific *)lpUnit->GetObjectSpecific())->ReleaseWounds();
        return TRUE;
    }

    bool unchanged = bWoundTextureDataCached;
    for (int i = 0; unchanged && i < RPG_NUM_HIT_LOCATIONS; ++i)
        unchanged = woundTextureData[i] == currentWoundData[i];

    char textureName[128];
    sprintf(textureName, "mo%dw", lpUnit->GetID());
    CObject3DClientSpecific *objectSpecific =
        (CObject3DClientSpecific *)lpUnit->GetObjectSpecific();
    if (unchanged)
    {
        // A missing CPU payload is rebuilt only for matching retained wound data.
        EnsureWoundsTextureReady();
        return TRUE;
    }

    if (AssembleWoundsTexture() && objectSpecific->LoadWounds(textureName, &currentWoundData))
    {
        for (int i = 0; i < RPG_NUM_HIT_LOCATIONS; ++i)
            woundTextureData[i] = currentWoundData[i];
        bWoundTextureDataCached = true;
    }
    else
    {
        bWoundTextureDataCached = false;
        objectSpecific->ReleaseWounds();
    }
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitClientSpecific::Dead()
{
    static_cast<CMapObjectClientSpecific *>(lpUnit->GetMapObjectSpecific())->LightOff();

    DWORD dwBloodType = lpUnit->GetRaceModel()->dwBloodType;
    if ((dwBloodType > 0) && (dwBloodType <= 4))
    {
        CPlot3D plot;
        float fBloodSize = 0.1f;

        CFigure *pBP = lpUnit->MatchFigure("bd");
        if (pBP)
        {
            const CPlot3D &ptMax = pBP->GetBoundingVolume()->ptMax;
            fBloodSize = (ptMax.x + ptMax.y) * 0.5f;
            plot = lpUnit->GetFigureWorldPosition(pBP);
        }
        else
        {
            const CPlot3D &ptMax = lpUnit->GetBoundingVolume().ptMax;
            fBloodSize = Min(ptMax.x, ptMax.y) * 0.75;
            plot = lpUnit->GetCP();
        }
        dwBloodType = ((dwBloodType - 1) << 16) | (Random(2) + 2);
        theWorld.AddBloodPrints(plot.x, plot.y, fBloodSize, fBloodSize * 3, dwBloodType, false);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
static const ERPGArmorTypes hlArmors[][2][2] = {
    { { RPGAST_UNKNOWN, RPGAST_UNKNOWN }, { RPGAST_HELM, RPGAST_UNKNOWN } }, // head
    { { RPGAST_SHIRT, RPGAST_UNKNOWN }, { RPGAST_PLATE, RPGAST_UNKNOWN } },  // body
    { { RPGAST_SHIRT, RPGAST_GLOVES }, { RPGAST_PLATE, RPGAST_UNKNOWN } },   // left hand
    { { RPGAST_SHIRT, RPGAST_GLOVES }, { RPGAST_PLATE, RPGAST_UNKNOWN } },   // right hand
    { { RPGAST_PANTS, RPGAST_BOOTS }, { RPGAST_LEGGINGS, RPGAST_UNKNOWN } }, // left leg
    { { RPGAST_PANTS, RPGAST_BOOTS }, { RPGAST_LEGGINGS, RPGAST_UNKNOWN } }  // right leg
};
void CUnitClientSpecific::HLDamaged(ERPGHitLocations hl, float fpDamage, BYTE cFlags)
{
    if (((cFlags & RPGSTAT_SHRINK) != 0) || (fpDamage <= 1e-4f))
        return; // don't need any processing
    // play 'weapon hit' sound
    //
    CString szMaterialName = lpUnit->GetRaceModel()->szNativeSkinType;
    if (lpUnit->GetTypeID() == MOTID_HUMAN)
    {
        ERPGArmorTypes armortype = hlArmors[hl][1][0];
        const SIDBMaterial *pMaterial = 0;
        if (armortype != RPGAST_UNKNOWN) // основной на верхнем слое
        {
            DWORD dwMaterial = CItem::GetMaterial(armors[armortype]);
            pMaterial = dwMaterial != 0xffff ? theItemDatabase.materials[dwMaterial] : 0;
            if (pMaterial)
                szMaterialName = pMaterial->szType;
        }
        if ((armortype = hlArmors[hl][0][0]) != RPGAST_UNKNOWN &&
            !pMaterial) // основной на нижнем слое
        {
            DWORD dwMaterial = CItem::GetMaterial(armors[armortype]);
            pMaterial = dwMaterial != 0xffff ? theItemDatabase.materials[dwMaterial] : 0;
            if (pMaterial)
                szMaterialName = pMaterial->szType;
        }
        if ((armortype = hlArmors[hl][0][1]) != RPGAST_UNKNOWN &&
            !pMaterial) // вспомогательный на нижнем слое
        {
            DWORD dwMaterial = CItem::GetMaterial(armors[armortype]);
            pMaterial = dwMaterial != 0xffff ? theItemDatabase.materials[dwMaterial] : 0;
            if (pMaterial)
                szMaterialName = pMaterial->szType;
        }
        // leather, metal, stone
        if ((szMaterialName == "Crystal") || (szMaterialName == "Bone") ||
            (szMaterialName == "Stone"))
            szMaterialName = "stone";
        else if (szMaterialName != "Metal")
            szMaterialName = "leather";
    }
    //
    CString szSFXName;
    BYTE cWeaponType = RPGWST_UNKNOWN;
    if (lpUnit->rpgUserUnitStats.wAttackerPrototype < RPGWST_MAGIC)
        cWeaponType = lpUnit->rpgUserUnitStats.wAttackerPrototype;
    else
    {
        DWORD dwPrototype = lpUnit->rpgUserUnitStats.wAttackerPrototype - RPGWST_MAGIC;
        const SUDBUnitPrototype *pPrototype = theUnitDatabase.prototypes[dwPrototype];
        if (pPrototype)
            cWeaponType = pPrototype->rpgAttackWeaponType;
    }
    if (lpUnit->GetTypeID() != MOTID_HUMAN)
    {
        switch (cWeaponType)
        {
        case RPGWST_SWORD:
        case RPGWST_AXE:
            szSFXName.Format("weapons\\slash\\%s\\1.wav", static_cast<LPCSTR>(szMaterialName));
            break;
        case RPGWST_DAGGER:
        case RPGWST_SPEAR:
        case RPGWST_BOW:
        case RPGWST_CROSSBOW:
        case RPGWST_HANDS2:
            szSFXName.Format("weapons\\pierce\\%s\\1.wav", static_cast<LPCSTR>(szMaterialName));
            break;
        case RPGWST_HAMMER:
            szSFXName.Format("weapons\\crash\\%s\\1.wav", static_cast<LPCSTR>(szMaterialName));
            break;
        case RPGWST_HANDS1:
            szSFXName.Format("weapons\\hands1\\%s\\1.wav", static_cast<LPCSTR>(szMaterialName));
            break;
        case RPGWST_HANDS3:
            szSFXName.Format("weapons\\crash\\%s\\1.wav", static_cast<LPCSTR>(szMaterialName));
            break;
        case RPGWST_HANDS4:
            szSFXName.Format("weapons\\hands4\\%s\\1.wav", static_cast<LPCSTR>(szMaterialName));
            break;
        case RPGWST_HANDS5:
            szSFXName.Format("weapons\\slash\\%s\\1.wav", static_cast<LPCSTR>(szMaterialName));
            break;
        case RPGWST_HANDS6:
            szSFXName.Format("weapons\\hands6\\%s\\1.wav", static_cast<LPCSTR>(szMaterialName));
            break;
        case RPGWST_HANDS7:
            szSFXName.Format("weapons\\hands7\\%s\\1.wav", static_cast<LPCSTR>(szMaterialName));
            break;
        }
    }
    else
    {
        switch (cWeaponType)
        {
        case RPGWST_SWORD:
        case RPGWST_HANDS4:
        case RPGWST_HANDS5:
        case RPGWST_AXE:
            szSFXName.Format("weapons\\slash\\%s\\1.wav", static_cast<LPCSTR>(szMaterialName));
            break;
        case RPGWST_DAGGER:
        case RPGWST_SPEAR:
        case RPGWST_BOW:
        case RPGWST_CROSSBOW:
        case RPGWST_HANDS2:
            szSFXName.Format("weapons\\pierce\\%s\\1.wav", static_cast<LPCSTR>(szMaterialName));
            break;
        case RPGWST_HAMMER:
        case RPGWST_HANDS1:
        case RPGWST_HANDS6:
            szSFXName.Format("weapons\\hands6\\%s\\1.wav", static_cast<LPCSTR>(szMaterialName));
            break;
        case RPGWST_HANDS3:
        case RPGWST_HANDS7:
            szSFXName.Format("weapons\\hands7\\%s\\1.wav", static_cast<LPCSTR>(szMaterialName));
            break;
        }
    }
    if (!szSFXName.IsEmpty())
        ((CWorldClient *)GetWorld())
            ->GetSoundManager()
            ->BeginFX(szSFXName, 1000, lpUnit->GetCP(), 8, 40);
    // draw 'blood' particles
    CPlot3D ptHL;
    float fpScale;
    CFigure *pBPDamaged = NULL;
    CParticle *pParticle = 0;
    CParticle::EBindPlace bindPlace;
    switch (hl)
    {
    case RPGHL_HEAD:
        pBPDamaged = lpUnit->MatchFigure("hd");
        bindPlace = CParticle::bpHead;
        break;
    case RPGHL_LHAND:
        pBPDamaged = lpUnit->MatchFigure("lh2");
        bindPlace = CParticle::bpLHand;
        break;
    case RPGHL_RHAND:
        pBPDamaged = lpUnit->MatchFigure("rh2");
        bindPlace = CParticle::bpRHand;
        break;
    case RPGHL_LLEG:
        pBPDamaged = lpUnit->MatchFigure("ll2");
        bindPlace = CParticle::bpLLeg;
        break;
    case RPGHL_RLEG:
        pBPDamaged = lpUnit->MatchFigure("rl2");
        bindPlace = CParticle::bpRLeg;
        break;
    case RPGHL_BODY:
        pBPDamaged = lpUnit->MatchFigure("bd");
        bindPlace = CParticle::bpBody;
        break;
    }
    //
    if (!pBPDamaged)
        return;
    if (!lpUnit->CheckFlag(OBJ_LOADED))
        return;
    DWORD dwBlood = lpUnit->GetRaceModel()->dwBloodType;
    ETypeIDs tidBlood;
    switch (dwBlood)
    {
    case 1:
    case 5:
        tidBlood = PTID_BLOOD1;
        break;
    case 2:
    case 6:
        tidBlood = PTID_BLOOD2;
        break;
    case 3:
    case 7:
        tidBlood = PTID_BLOOD3;
        break;
    case 4:
    case 8:
        tidBlood = PTID_BLOOD4;
        break;
    default:
        tidBlood = TID_EMPTY;
        break;
    }
    // 'flying' blood
    if (tidBlood != TID_EMPTY)
    {
        pParticle = ((CParticleManager *)theWorld.GetParticleManager())
                        ->CreateParticleSystem(tidBlood);
        fpScale = pBPDamaged->GetBV().fpRadius;
        pParticle->SetCP(CPlot3D(0, 0, 0.1f));
        pParticle->Scale(fpScale * 2.5f);
        pParticle->SetPower2(0.05f / 1.5f); // lpUnit->GetBoundingVolume().fpRadius * 0.02f );
        if (fpDamage < 0.1f)
            fpDamage = 0.1f;
        if (fpDamage > 1)
            fpDamage = 1;
        pParticle->SetPower(fpDamage);
        pParticle->AttachCarrier(lpUnit);
        pParticle->BindTo(bindPlace);
    }
    // blood on the ground
    if ((dwBlood > 0) && (dwBlood <= 4))
    {
        float fBVSize = Min(lpUnit->GetBoundingVolume().ptMax.x,
                            lpUnit->GetBoundingVolume().ptMax.y);
        float fBloodPrintsRandom = Max(1.0f, fBVSize);
        const CPlot3D &ptMax = pBPDamaged->GetBoundingVolume()->ptMax;
        float fBloodSize = 0.3f * ((ptMax.x + ptMax.y) * 0.5f);
        CPlot3D plot = lpUnit->GetFigureWorldPosition(pBPDamaged);
        auto draw = [&](int maximum) { auto* mod=ModRuntime::Current(); return mod&&mod->CombatSetting("JMVAttacks") ? ModNativeCrtRandom::Random(maximum) : Random(maximum); };
        plot.x += 2.0f * float(draw(MINT(fBloodPrintsRandom * 100.0f))) * 0.01f -
                  fBloodPrintsRandom;
        plot.y += 2.0f * float(draw(MINT(fBloodPrintsRandom * 100.0f))) * 0.01f -
                  fBloodPrintsRandom;
        DWORD dwBloodType = ((dwBlood - 1) << 16) | draw(2);
        theWorld.AddBloodPrints(plot.x, plot.y, fBloodSize, fBloodSize * 3, dwBloodType, true);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitClientSpecific::ElectricDamage(float fpStrength)
{
    CParticle *pParticle = ((CParticleManager *)theWorld.GetParticleManager())
                               ->CreateParticleSystem(PTID_SPARKS);
    float fpScale = lpUnit->bv.fpRadius;
    pParticle->SetCP(CPlot3D(0, 0, 0));
    pParticle->Scale(fpScale);
    pParticle->SetPower(10);
    pParticle->AttachCarrier(lpUnit);
    pParticle->BindTo(CParticle::bpBody);
    TemporalHighlight(4);
}
/////////////////////////////////////////////////////////////////////////////////////
// hide weapon model
namespace { thread_local bool restoringBriefingWeapon = false; }
void CUnitClientSpecific::SetBriefingWeaponHidden(bool hidden)
{
    if (bBriefingWeaponForcedHidden == hidden)
    {
        if (hidden) HideWeapon(true);
        return;
    }
    if (hidden) bWeaponHiddenBeforeBriefing = bWeaponHidden;
    bBriefingWeaponForcedHidden = hidden;
    struct RestoreBriefingContext {
        bool previous;
        RestoreBriefingContext(bool restoring) : previous(restoringBriefingWeapon) { restoringBriefingWeapon = restoring; }
        ~RestoreBriefingContext() { restoringBriefingWeapon = previous; }
    } context(!hidden);
    HideWeapon(hidden ? true : bWeaponHiddenBeforeBriefing);
}

void CUnitClientSpecific::HideWeapon(bool bHide)
{
    if (bBriefingWeaponForcedHidden) bHide = true;
    if (bWeaponHidden == bHide && !bBriefingWeaponForcedHidden)
        return;

    DWORD dwWeapon = GetCurrentWeapon();
    const auto *mod = ModRuntime::Current();
    if (mod && mod->CombatSetting("JMVAttacks") && dwWeapon != DWORD(-1) &&
        CItemWeapon::GetWeaponType(dwWeapon) != RPGWST_BOW && !bMouthSpeakingMode &&
        (bHide ? !bBriefingWeaponForcedHidden : !restoringBriefingWeapon)) return;

    CStringArray szNames;

    if (dwWeapon != -1)
        CItemWeapon::GetWeaponModelNames(CItem::GetShape(dwWeapon), szNames);
    //
    for (int i = 0; i < szNames.GetSize(); ++i)
    {
        CFigure *pFigure = lpUnit->FindFigure(szNames[i]);
        ASSERT(pFigure);
        if (!pFigure)
            continue;
        if (bHide)
            pFigure->SetFlag(FIG_HIDDEN);
        else
            pFigure->ResetFlag(FIG_HIDDEN);
    }

    const int leftSlot = ResolveVisualLeftHand(*lpUnit,
        [this](int slot) { return GetWeapon(DWORD(slot)); });
    const DWORD leftWeapon = leftSlot >= 0 ? GetWeapon(DWORD(leftSlot)) : DWORD(-1);
    if (leftWeapon != DWORD(-1) && leftSlot != int(lpUnit->dwCurrentWeapon)) {
        CItemWeapon::GetWeaponModelNames(CItem::GetShape(leftWeapon), szNames, false);
        for (int i = 0; i < szNames.GetSize(); ++i) {
            CFigure *figure = lpUnit->FindFigure(szNames[i]);
            if (!figure) continue;
            if (bHide) figure->SetFlag(FIG_HIDDEN);
            else figure->ResetFlag(FIG_HIDDEN);
        }
    }

    for (auto *particle : pWeaponParticles) {
        if (!particle || particle->GetCarrier() != lpUnit) continue;
        if (bHide) particle->HoldEffect();
        else particle->RunEffect();
    }
    bWeaponHidden = bHide;
    bArrowHidden = bHide;
}
/////////////////////////////////////////////////////////////////////////////////////
class CRemoveExclusiveAckFunctional
{
private:
    DWORD dwCategory;
    DWORD dwType;

public:
    explicit CRemoveExclusiveAckFunctional(DWORD _dwType)
        : dwType(_dwType)
    {
    }
    bool operator()(const SAckDescriptor &ack)
    {
        // only HERO_REFUSE can exclude HERO_ACK
        return (((dwType == ACK_HERO_REFUSE_CANT_CHANGE_POSE) &&
                 (ack.dwType == ACK_HERO_ACK_CHANGE_POSE)) ||
                ((dwType == ACK_HERO_REFUSE_PATH_NOT_FOUND) && (ack.dwType == ACK_HERO_ACK_MOVE)) ||
                ((dwType == ACK_HERO_REFUSE_CANT_CAST) && (ack.dwType == ACK_HERO_ACK_CAST)) ||
                ((dwType == ACK_HERO_REFUSE_CANT_TELEPORT) && (ack.dwType == ACK_HERO_ACK_CAST)) ||
                ((dwType == ACK_HERO_REFUSE_SCIENCE_FAILED) &&
                 (ack.dwType == ACK_HERO_ACK_USE_OBJECT)) ||
                ((dwType == ACK_HERO_REFUSE_SCIENCE_FAILED) &&
                 (ack.dwType == ACK_HERO_ACK_STEAL_FROM_UNIT)) ||
                ((dwType == ACK_HERO_REFUSE_TOO_COMPLEX_SPELL_TO_CAST) &&
                 (ack.dwType == ACK_HERO_ACK_CAST)));
    }
};
class CRemoveFinishedAckFunctional
{
private:
    int nUnitID;

public:
    explicit CRemoveFinishedAckFunctional(int nID)
        : nUnitID(nID)
    {
    }
    bool operator()(const SAckDescriptor &ack)
    {
        if ((ack.hSound != -1) && !theWorld.GetSoundManager()->IsFXValid(ack.hSound))
        {
            if (ack.bBlockUnit)
            {
                CUnit *pUnit = theWorldServer.GetUnit(nUnitID);
                if (pUnit)
                    pUnit->ResetFlag(UNIT_BLOCKED);
            }
            return true;
        }
        else
            return false;
    }
};
void CUnitClientSpecific::AddAck(const SAckEntry *pAck, DWORD dwType)
{
    if (pAck == 0)
        return;
    // remove mutualy exclusive acks
    acks.remove_if(CRemoveExclusiveAckFunctional(dwType));
    // remove acks, which have already finished
    acks.remove_if(CRemoveFinishedAckFunctional(lpUnit->GetID()));
    //
    bool bGuaratee = IsAckGuarantie(dwType);
    //
    if (acks.empty() || bGuaratee)
    {
        // form new ack
        SAckDescriptor ack;
        ack.pAck = pAck;
        ack.dwType = dwType;
        ack.nBackCounter = 3;
        ack.hSound = -1;
        ack.bBlockUnit = (dwType == ACK_HERO_SCENARIO_BLOCK);
        // play it, if no other acks playing now (ack list is empty)
        if (acks.empty())
        {
            if (ack.bBlockUnit)
            {
                CUnit *pUnit = theWorldServer.GetUnit(lpUnit->GetID());
                if (pUnit)
                    pUnit->SetFlag(UNIT_BLOCKED);
            }
            ack.nBackCounter = 0;

            if ((ack.dwType == ACK_HERO_ACK_ATTACK) ||
                (ack.dwType == ACK_HERO_NOTIFY_DECIDED_TO_ATTACK))
            {
                if (!theWorld.GetSoundManager()->IsFXValid(hLastAttackSound))
                {
                    if (BeginPlayAck(ack))
                        hLastAttackSound = ack.hSound;
                }
            }
            else
                BeginPlayAck(ack);
        }
        acks.push_back(ack);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitClientSpecific::ClearAcks()
{
    // search for 'blocking' ack and unblock unit, if it is
    for (auto pos = acks.begin(); pos != acks.end(); ++pos)
    {
        if (pos->bBlockUnit)
        {
            CUnit *pUnit = theWorldServer.GetUnit(lpUnit->GetID());
            if (pUnit)
                pUnit->ResetFlag(UNIT_BLOCKED);
        }
        theWorld.GetSoundManager()->CancelFX(pos->hSound);
    }
    //
    acks.clear();
}
/////////////////////////////////////////////////////////////////////////////////////
bool CUnitClientSpecific::BeginPlayAck(SAckDescriptor &ack)
{
    ModAudioPolicy::AckScope route(lpUnit ? unsigned(lpUnit->GetID()) : 0, ack.dwType,
        ModRuntime::Current() && lpUnit);
    DWORD dwTime = timeGetTime();
    if ((pLastAck == ack.pAck) && (dwTime - dwLastAckTime < 5000))
        return false;
    if (IsAckOutOfCharacter(ack.dwType))
        ack.hSound = theWorld.GetSoundManager()->BeginSpeech(ack.pAck->szFileName.c_str(), 1, 100);
    else
        ack.hSound = theWorld.GetSoundManager()->BeginSpeech(ack.pAck->szFileName.c_str(), 1,
                                                             lpUnit->GetCP(), FP_MIN_SOUND_RADIUS,
                                                             FP_MAX_SOUND_RADIUS);

    pLastAck = ack.pAck;
    dwLastAckTime = dwTime;
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitClientSpecific::SetArmors(DWORD *pArmors, DWORD dwNumArmors)
{
    int i;
    // remove all the old armors
    for (i = 0; i < RPG_NUM_ARMOR_TYPES; ++i)
        Remove(ERPGArmorTypes(i));
    MemSetDWord(armors, -1, RPG_NUM_ARMOR_TYPES);
    // fill with new armors
    int num = Min(dwNumArmors, RPG_NUM_ARMOR_TYPES);
    for (i = 0; i < num; ++i)
        Wear(pArmors[i]);
}
/////////////////////////////////////////////////////////////////////////////////////
bool CUnitClientSpecific::Wear(DWORD dwArmor)
{
    if (dwArmor == -1)
        return false;
    DWORD dwShape = CItem::GetShape(dwArmor);
    DWORD dwMaterial = CItem::GetMaterial(dwArmor);
    if ((dwShape == 0xFFFF) || (dwMaterial == 0xFFFF))
        return false;
    ERPGArmorTypes eArmorType = (ERPGArmorTypes)theItemDatabase.armors[dwShape]->dwTypeID;
    if (eArmorType >= RPG_NUM_ARMOR_TYPES)
        return false;
    if (armors[eArmorType] == dwArmor)
        return true;
    armors[eArmorType] = dwArmor;
    // change (or add) figure
    CFigureManager *pFigureManager = (CFigureManager *)theWorld.GetFigureManager();
    int nModelShapeIndex = -1;

    switch (eArmorType)
    {
    case RPGAST_PLATE:
        nModelShapeIndex = 0;
        break;
    case RPGAST_LEGGINGS:
        nModelShapeIndex = 1;
        break;
    case RPGAST_HELM:
        nModelShapeIndex = 2;
        break;
    }
    if (nModelShapeIndex != -1)
    {
        CModelResourceEntry *pModel = pFigureManager->GetModel(lpUnit->GetMaskName());
        if (pModel == 0)
            return false;
        const SIDBArmorShape *pShape = theItemDatabase.armors[dwShape];
        CString szString;
        //
        for (int i = 0; i < 7; i++)
        {
            if (pszModelBodyparts[nModelShapeIndex][i][0] == 0)
                break; // no more bodyparts
            CString szBodypartName(pszModelBodyparts[nModelShapeIndex][i]);
            if (szBodypartName == "hr") // hair bodypart name correction
            {
                DWORD dwHair = theUnitDatabase.prototypes[lpUnit->GetSubtypeID()]->dwHair;
                if (dwHair == -1)
                    continue;
                szBodypartName.Format("hr.%.2d", dwHair);
            }
            CFigure *pFigure = lpUnit->MatchFigure(szBodypartName);
            szString.Format("%s.armor%.2d", static_cast<LPCSTR>(szBodypartName), pShape->dwTextureTypeIndex);
            if (pModel->GetModel()->CheckFileExist(szString))
            {
                lpUnit->LoadFigure(szString, pModel, lpUnit->CheckFlag(OBJ_LOADED));
                if (strcmp(pszModelBodyparts[nModelShapeIndex][i], "hd") != 0)
                    lpUnit->RemoveFigure(pFigure); // remove everything but the hair
            }
            else
            {
                if (pFigure)
                {
                    if (strcmp(pszModelBodyparts[nModelShapeIndex][i], "hr") == 0)
                        lpUnit->RemoveFigure(pFigure);
                    else if (szBodypartName != pFigure->GetName())
                    {
                        lpUnit->LoadFigure(szBodypartName, pModel, lpUnit->CheckFlag(OBJ_LOADED));
                        lpUnit->RemoveFigure(pFigure);
                    }
                }
                else
                    lpUnit->LoadFigure(szBodypartName, pModel, lpUnit->CheckFlag(OBJ_LOADED));
            }
        }
    }

    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CUnitClientSpecific::Remove(ERPGArmorTypes eArmorType)
{
    if (eArmorType >= RPG_NUM_ARMOR_TYPES)
        return false;
    DWORD dwArmor = armors[eArmorType];
    if (dwArmor == -1)
        return false;
    DWORD dwShape = CItem::GetShape(dwArmor);
    DWORD dwMaterial = CItem::GetMaterial(dwArmor);
    if ((dwShape == 0xFFFF) || (dwMaterial == 0xFFFF))
        return false;
    armors[eArmorType] = -1;
    // remove model bodyparts
    CFigureManager *pFigureManager = (CFigureManager *)theWorld.GetFigureManager();
    int nModelShapeIndex = -1;

    switch (eArmorType)
    {
    case RPGAST_PLATE:
        nModelShapeIndex = 0;
        break;
    case RPGAST_LEGGINGS:
        nModelShapeIndex = 1;
        break;
    case RPGAST_HELM:
        nModelShapeIndex = 2;
        break;
    }
    if (nModelShapeIndex != -1)
    {
        CModelResourceEntry *pModel = ((CFigureManager *)theWorld.GetFigureManager())
                                          ->GetModel(lpUnit->GetMaskName());
        if (pModel == 0)
            return false;
        for (int i = 0; i < 7; i++)
        {
            if (pszModelBodyparts[nModelShapeIndex][i][0] == 0)
                break; // no more bodyparts
            CString szBodypartName(pszModelBodyparts[nModelShapeIndex][i]);
            if (szBodypartName == "hr") // hair bodypart name correction
            {
                DWORD dwHair = theUnitDatabase.prototypes[lpUnit->GetSubtypeID()]->dwHair;
                if (dwHair == -1)
                    continue;
                szBodypartName.Format("hr.%.2d", dwHair);
            }

            CSimplyArray<CFigure *, CFigure *> figs;
            lpUnit->MatchFigures(szBodypartName, figs);
            if (figs.GetSize() == 1)
            {
                if (szBodypartName != figs[0]->GetName())
                {
                    lpUnit->RemoveFigure(figs[0]);
                    lpUnit->LoadFigure(szBodypartName, pModel, lpUnit->CheckFlag(OBJ_LOADED));
                }
            }
            else
            {
                for (int j = 0; j < figs.GetSize(); ++j)
                    lpUnit->RemoveFigure(figs[j]);
                lpUnit->LoadFigure(szBodypartName, pModel, lpUnit->CheckFlag(OBJ_LOADED));
            }
        }
    }
    //
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitClientSpecific::SetWeapons(DWORD *pWeapons, DWORD dwNumWeapons)
{
    // clear all the old weapons
    MemSetDWord(weapons, -1, QI_MAX_WEAPONS);
    // fill with new weapons
    int num = Min(dwNumWeapons, QI_MAX_WEAPONS);
    for (int i = 0; i < num; ++i)
        AddWeapon(pWeapons[i]);
}
/////////////////////////////////////////////////////////////////////////////////////
bool CUnitClientSpecific::AddWeapon(DWORD dwWeapon)
{
    if (dwWeapon == -1)
        return false;
    // find first empty place
    int num = 0;
    for (; num < QI_MAX_WEAPONS; ++num)
    {
        if (weapons[num] == -1)
            break;
    }
    // return with false, if no empty places
    if (num == QI_MAX_WEAPONS)
        return false;
    //
    DWORD dwShape = CItem::GetShape(dwWeapon);
    DWORD dwMaterial = CItem::GetMaterial(dwWeapon);
    if ((dwShape == 0xFFFF) || (dwMaterial == 0xFFFF))
        return false;
    weapons[num] = dwWeapon;

    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CUnitClientSpecific::RemoveWeapon(DWORD dwWeapon)
{
    for (int i = 0; i < QI_MAX_WEAPONS; ++i)
    {
        if (weapons[i] == dwWeapon)
        {
            weapons[i] = -1;
            return true;
        }
    }
    return false;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitClientSpecific::GetAttackParams(SAttackParams &params) const
{
    if(auto*mod=ModRuntime::Current()){mod->AttackParams(lpUnit->rpgUserUnitStats,*lpUnit,params);return;}
    bool bHero = IsIDofPersonage(lpUnit->GetID());
    int i;

    if (!bHero || (lpUnit->dwCurrentWeapon >= QI_MAX_WEAPONS) ||
        (weapons[lpUnit->dwCurrentWeapon] == -1))
    {
        params.fDamageMin = lpUnit->rpgUserUnitStats.cNAWDamageMin;
        params.fDamageMax = lpUnit->rpgUserUnitStats.cNAWDamageMin +
                            lpUnit->rpgUserUnitStats.cNAWDamageRnd;
        params.nAttack = lpUnit->rpgUserUnitStats.cNAWToHit;
        params.nDefense = lpUnit->rpgUserUnitStats.cNAWParry;
        params.fWeaponWeight = lpUnit->rpgUserUnitStats.cNAWWeight;
        //
        if ((lpUnit->dwCurrentWeapon < QI_MAX_WEAPONS) && (weapons[lpUnit->dwCurrentWeapon] != -1))
        {
            DWORD dwWeapon = weapons[lpUnit->dwCurrentWeapon];
            params.rpgWeaponType = CItemWeapon::GetWeaponType(dwWeapon);
            params.rpgAttackType = CItemWeapon::GetAttackType(dwWeapon);
        }
        else
        {
            params.rpgWeaponType = RPGWST_HANDS;
            const SUDBRaceModel *pRaceModel = lpUnit->GetRaceModel();
            for (i = 0; i < RPG_NUM_ATTACK_TYPES; ++i)
            {
                if (pRaceModel->attacks[i] != 0)
                {
                    params.rpgAttackType = static_cast<ERPGAttackTypes>(i);
                    break;
                }
            }
        }
    }
    else
    {
        DWORD dwWeapon = weapons[lpUnit->dwCurrentWeapon];
        params.fDamageMin = CItemWeapon::GetDamageMin(dwWeapon);
        params.fDamageMax = CItemWeapon::GetDamageMax(dwWeapon);
        params.rpgWeaponType = CItemWeapon::GetWeaponType(dwWeapon);
        params.rpgAttackType = CItemWeapon::GetAttackType(dwWeapon);
        params.fWeaponWeight = CItemWeapon::GetWeight(dwWeapon);
        const SIDBWeaponShape *pShape = theItemDatabase.weapons[CItem::GetShape(dwWeapon)];
        params.nAttack = lpUnit->rpgUserUnitStats.GetWeaponSkillLevel(params.rpgWeaponType,
                                                                      pShape) +
                         CItemWeapon::GetAtackBonus(dwWeapon);
        params.nDefense = lpUnit->rpgUserUnitStats.GetWeaponSkillLevel(params.rpgWeaponType,
                                                                       pShape) +
                          CItemWeapon::GetDefenceBonus(dwWeapon);
    }
    // we can't defend with the bow class weapons
    if ((params.rpgWeaponType == RPGWST_BOW) || (params.rpgWeaponType == RPGWST_CROSSBOW))
        params.nDefense = 0;
    // correct damage params with spells
    params.fDamageMax = lpUnit->rpgUserUnitStats.GetModifiedDamage(lpUnit, params.fDamageMax);
    params.fDamageMin = lpUnit->rpgUserUnitStats.GetModifiedDamage(lpUnit, params.fDamageMin);
    //
    params.fToHitRandom = lpUnit->rpgUserUnitStats.cNAWToHitRandom;
    //
    params.ApplyDifficultyLevel(bHero);
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitClientSpecific::HideArrow(bool bHide)
{
    if (bArrowHidden == bHide)
        return;
    DWORD dwWeapon = GetCurrentWeapon();
    if (dwWeapon == -1)
        return;
    if (CItemWeapon::GetWeaponType(dwWeapon) == RPGWST_BOW)
    {
        CFigure *pFigure = lpUnit->FindFigure("rh3.arrow00");
        if (pFigure)
        {
            if (bHide)
                pFigure->SetFlag(FIG_HIDDEN);
            else
                pFigure->ResetFlag(FIG_HIDDEN);
            bArrowHidden = bHide;
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitClientSpecific::ProcessHideUnhide()
{
    // for humans ONLY
    if (lpUnit->GetTypeID() != MOTID_HUMAN)
        return;
    const SADEntry *pADEntry = lpUnit->GetCurrentADEntry();
    // if no animation or this animation have no hide/unhide entries, hide arrow
    if (!pADEntry || (pADEntry && !pADEntry->IsHaveUnhide()))
    {
        HideArrow();
        return;
    }
    //
    float fTime = lpUnit->lpAnimationSystem->GetTime();
    if (fTime == fLastTime)
        return; // no animation time passed since last check
    //
    if (IsValInRangeRight<BOOL>(float(pADEntry->dwHides[0]), fLastTime, fTime))
        HideArrow(false);
    else if (IsValInRangeRight<BOOL>(float(pADEntry->dwHides[1]), fLastTime, fTime))
        HideArrow(true);
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitClientSpecific::TemporalHighlight(int nNumSegments)
{
    if ((nTemporalHighlightSegmentsCounter == 0) && (nNumSegments > 0))
        static_cast<CObject3DClientSpecific *>(lpUnit->GetObjectSpecific())->Highlight(TRUE);
    nTemporalHighlightSegmentsCounter = Max(nTemporalHighlightSegmentsCounter, nNumSegments);
}
/////////////////////////////////////////////////////////////////////////////////////
bool CUnitClientSpecific::LoadMouthMimicTexture()
{
    if (pMouthMimicTexture != 0)
        return true;
    const SUDBUnitPrototype *pPrototype = lpUnit->GetPrototype();
    if (pPrototype == 0)
        return false;
    char buff[1024];
    if (lpUnit->GetTypeID() == MOTID_HUMAN)
        sprintf(buff, UseHDCharacterTextures() ? "%smouth_%.2d.x4" : "%smouth_%.2d",
                lpUnit->GetMaskName(), pPrototype->dwSkinIndex);
    else
        sprintf(buff, UseHDCharacterTextures() ? "%smouth.x4" : "%smouth", lpUnit->GetMaskName());
    pMouthMimicTexture = theWorld.GetTextureManager()->CreateTexture(TCTID_STATIC_TEXTURE, buff);
    theWorld.GetTextureManager()->LoadTexture(pMouthMimicTexture);
    if (pMouthMimicTexture->CheckFlag(TEX_LOADED) == 0)
    {
        ReleaseMouthMimicTexture();
        return false;
    }
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitClientSpecific::ReleaseMouthMimicTexture()
{
    pMouthMimicTexture = 0;
    bMouthSpeakingMode = false;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitClientSpecific::MouthOpen(bool bForce)
{
    if (pMouthMimicTexture == 0)
        return;
    if (!bForce && (nCurrentMouthState == 1))
        return;
    auto* objectSpec=static_cast<CObject3DClientSpecific*>(lpUnit->GetObjectSpecific());
    if(!objectSpec || !objectSpec->EnsurePrivatePrimaryAtlas(lpUnit->GetID()))return;
    // 32x32 texture
    // mouth open are in (0,16) coords with size 32x16
    // mouth close are in (0,0) coords with size 32x16
    ITexture *pPrimaryTexture = static_cast<CObject3DClientSpecific *>(lpUnit->GetObjectSpecific())
                                    ->GetPrimaryTexture();
    if (pPrimaryTexture == 0)
        return;
    if (UseHDCharacterTextures())
    {
        const SUDBRaceModel *race = lpUnit->GetRaceModel();
        if (!race) return;
        const LONG width = race->nMouthRectSizeX * 4;
        const LONG height = race->nMouthRectSizeY * 4;
        const LONG top = LONG(pMouthMimicTexture->GetSizeY()) - 2 * height;
        POINT point = { race->nMouthPointX * 4,
                        LONG(pPrimaryTexture->GetSizeX()) - race->nMouthPointY * 4 - height };
        RECT rect = { 0, top, width, top + height };
        if (CopyMouthTexture(pPrimaryTexture, &point, pMouthMimicTexture, &rect))
            nCurrentMouthState = 1;
        return;
    }
    // calc coeff for non high-quality textures
    float fCoeff = pPrimaryTexture->GetSizeX() / 256.0f;
    // текстура перевёрнута - не забывай
    const SUDBRaceModel *pRace = lpUnit->GetRaceModel();
    float fYAdd = (pMouthMimicTexture->GetSizeY() / fCoeff - pRace->nMouthRectSizeY * 2.0f) /
                  fCoeff;
    POINT point = { long(pRace->nMouthPointX * fCoeff),
                    long((256 - pRace->nMouthPointY - pRace->nMouthRectSizeY) * fCoeff) };
    RECT rect = { 0, long(fYAdd), long(pRace->nMouthRectSizeX * fCoeff),
                  long((pRace->nMouthRectSizeY + fYAdd) * fCoeff) };
    if (CopyMouthTexture(pPrimaryTexture, &point, pMouthMimicTexture, &rect))
        nCurrentMouthState = 1;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitClientSpecific::MouthClose(bool bForce)
{
    if (pMouthMimicTexture == 0)
        return;
    if (!bForce && (nCurrentMouthState == 0))
        return;
    auto* objectSpec=static_cast<CObject3DClientSpecific*>(lpUnit->GetObjectSpecific());
    if(!objectSpec || !objectSpec->EnsurePrivatePrimaryAtlas(lpUnit->GetID()))return;
    // 32x32 texture
    // mouth open are in (0,16) coords with size 32x16
    // mouth close are in (0,0) coords with size 32x16
    ITexture *pPrimaryTexture = static_cast<CObject3DClientSpecific *>(lpUnit->GetObjectSpecific())
                                    ->GetPrimaryTexture();
    if (pPrimaryTexture == 0)
        return;
    if (UseHDCharacterTextures())
    {
        const SUDBRaceModel *race = lpUnit->GetRaceModel();
        if (!race) return;
        const LONG width = race->nMouthRectSizeX * 4;
        const LONG height = race->nMouthRectSizeY * 4;
        const LONG top = LONG(pMouthMimicTexture->GetSizeY()) - 1 * height;
        POINT point = { race->nMouthPointX * 4,
                        LONG(pPrimaryTexture->GetSizeX()) - race->nMouthPointY * 4 - height };
        RECT rect = { 0, top, width, top + height };
        if (CopyMouthTexture(pPrimaryTexture, &point, pMouthMimicTexture, &rect))
            nCurrentMouthState = 0;
        return;
    }
    // calc coeff for non high-quality textures
    float fCoeff = pPrimaryTexture->GetSizeX() / 256.0f;
    // текстура перевёрнута - не забывай
    const SUDBRaceModel *pRace = lpUnit->GetRaceModel();
    float fYAdd = (pMouthMimicTexture->GetSizeY() / fCoeff - pRace->nMouthRectSizeY * 2.0f) /
                  fCoeff;
    POINT point = { long(pRace->nMouthPointX * fCoeff),
                    long((256 - pRace->nMouthPointY - pRace->nMouthRectSizeY) * fCoeff) };
    RECT rect = { 0, long((pRace->nMouthRectSizeY + fYAdd) * fCoeff),
                  long(pRace->nMouthRectSizeX * fCoeff),
                  long((2.0f * pRace->nMouthRectSizeY + fYAdd) * fCoeff) };
    if (CopyMouthTexture(pPrimaryTexture, &point, pMouthMimicTexture, &rect))
        nCurrentMouthState = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
int CUnitClientSpecific::GetHeroLevel() const
{
    const CModPackage *mod = ModRuntime::Current();
    if (mod && mod->SpellAddon().Feature47() == 1)
    {
        const auto &profile = mod->SpellAddon();
        const float divisor = profile.Scalar(ModSpellAddon::ScalarKey::rebalanceengine_divlvla);
        const float multiplier = profile.Scalar(ModSpellAddon::ScalarKey::rebalanceengine_mullvla);
        const float addOutside = profile.Scalar(ModSpellAddon::ScalarKey::rebalanceengine_addlvla);
        const float addInside = profile.Scalar(ModSpellAddon::ScalarKey::rebalanceengine_addlvlb);
        const int32_t experience = NumericPolicy::SignedExperience(lpUnit->rpgUserUnitStats.dwExperience);
        return NumericPolicy::RebalancedHeroLevel(double(experience), divisor, multiplier,
                                                   addInside, addOutside);
    }
    float fPower = float(lpUnit->rpgUserUnitStats.dwExperience) /
                   theAIGeneralSettings.fConsiderModifier;
    return NumericPolicy::HeroLevelFromPower(fPower, theAIGeneralSettings.fConsiderLogBase);
}
/////////////////////////////////////////////////////////////////////////////////////
bool CUnitClientSpecific::HaveLowShapes() const
{
    return ((armors[RPGAST_SHIRT] != -1) || (armors[RPGAST_PANTS] != -1) ||
            (armors[RPGAST_BOOTS] != -1) || (armors[RPGAST_GLOVES] != -1));
}
/////////////////////////////////////////////////////////////////////////////////////
bool CUnitClientSpecific::HaveHighShapes() const
{
    return ((armors[RPGAST_HELM] != -1) || (armors[RPGAST_PLATE] != -1) ||
            (armors[RPGAST_LEGGINGS] != -1));
}
/////////////////////////////////////////////////////////////////////////////////////
