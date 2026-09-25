#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "Unit.h"
#include "ModPackage.h"
#include "ModRuntime.h"

#include "WinRegistry.h"
#include "FigureManager.h"

#include "World.h"
#include "Item.h"
#include "PacketIDs.h"
#include "NetProtocol.h"
#include "WorldClient.h"
#include "WorldServer.h"
#include "MapObjectServer.h"
#include "MapObjectManager.h"
#include "player.h"
#include "FofSystem.h"
#include "UnitServer.h"
#include "SpellManager.h"
#include "ItemManager.h"
#include "randomGen.h"
#include "UnitClientSpecific.h"
#include "MapObjectClientSpecific.h"
#include "aiSettings.h"
#include "PlayerClient.h"
#include "PlayerServer.h"
/////////////////////////////////////////////////////////////////////////////////////
#define _NOTIFY_FILE 0

#pragma pack(push, 1)
struct SAnimationIdPayload
{
    char magic[4];
    BYTE version, flags;
    WORD reserved;
    DWORD start, cycle;
};
#pragma pack(pop)
static_assert(sizeof(SAnimationIdPayload) == 16, "AIDX payload layout");

static DWORD NormalizeAnimationId(DWORD id)
{
    return id == 0xff ? 0xffffffffu : id;
}
static bool HasWideAnimationId(DWORD start, DWORD cycle)
{
    return (start > 253 && start != 0xffffffffu) ||
           (cycle > 253 && cycle != 0xffffffffu);
}
static BYTE EncodeAnimationId(DWORD id)
{
    return id == 0xff || id == 0xffffffffu ? 0xff :
        (id > 253 ? 0xfe : static_cast<BYTE>(id));
}

/////////////////////////////////////////////////////////////////////////////////////
void SAttackParams::ApplyDifficultyLevel(BOOL bHero)
{
    if (bHero)
        return;
    nAttack = int(float(nAttack) *
                  theAIGeneralSettings.fAttackLevels[theSettings.nDifficultyLevel]);
    nDefense = int(float(nDefense) *
                   theAIGeneralSettings.fDefenceLevels[theSettings.nDifficultyLevel]);
}
/////////////////////////////////////////////////////////////////////////////////////
// ***********************************************************************************
// class CUnit functions and operations
// ***********************************************************************************
/////////////////////////////////////////////////////////////////////////////////////
CUnit::CUnit()
{
    lpUnitSpecific = NULL;
    lpOwner = NULL;
    Clear();

#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
}
/////////////////////////////////////////////////////////////////////////////////////
CUnit::~CUnit()
{
    Destroy();
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CUnit::Create()
// create all internal data structures
BOOL CUnit::Create()
{
    CMapObject::Create();
    Clear();
    update.SetHistoriesNumber(VLD_MASK_UNITMAX + 1);
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// void CUnit::Destroy()
// destroy all internal data structures
void CUnit::Destroy()
{
    FreeReferences();
    CMapObject::Destroy();

    Clear();
}
/////////////////////////////////////////////////////////////////////////////////////
// void CUnit::Clear()
// clear data (not destroy)
void CUnit::Clear()
{
    szName.Empty();
    dwVoicePrototype = -1;
    //
    fpBaseZ = 0;
    nPlayerNum = 0;
    nAIClass = 3;
    fpAttackDistance = 0.75f;
    fpCollisionDistance = 0.5f;
    dwEnemyMask = 0;
    dwPossibleEnemies = 0xFFFFFFFF;
    nPlayerNum = 0;
    //
    SetUnitSpecific(NULL);
    lpOwner = 0;
    nStartAnimation = 0xff;
    nCycleAnimation = 0xff;
    fullStartAnimation = fullCycleAnimation = 0xffffffffu;
    bExtendedAnimationId = false;
    nAnimationSound = 0;
    nAnimationStartSegment = 0;
    animationSpeed = UAS_NORMAL_;
    locomotionSpeed = ULS_NORMAL;
    wishPose = currentPose = UP_STAND;
    dwCurrentWeapon = dwWishWeapon = -1;
    bDead = FALSE;
    bMoving = FALSE;
    //
    dwCurrentWeapon = DWORD(-1);
    rpgUserUnitStats.Clear();
    MemSetDWord((DWORD *)lpQWeapons, 0, QI_MAX_WEAPONS);
    MemSetDWord((DWORD *)lpQItems, 0, QI_MAX_ITEMS);
    MemSetDWord((DWORD *)lpArmors, 0, RPG_NUM_ARMOR_TYPES);
    MemSetDWord((DWORD *)lpQSpells, 0, QI_MAX_SPELLS);
    questItems.SetSize(0);
    //
    bNeedFastPath = FALSE;
    //
    update.Clear();
    aggression = PAS_AGGRESSIVE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnit::FreeReferences()
{
    int i;

    CMapObject::FreeReferences();

    SetOwner(NULL);
    if (lpUnitSpecific)
        lpUnitSpecific->FreeReferences();
    // release quick items
    for (i = 0; i < QI_MAX_ITEMS; i++)
    {
        if (lpQItems[i])
            lpQItems[i]->Release();
        lpQItems[i] = NULL;
    }
    // release quick weapons
    for (i = 0; i < QI_MAX_WEAPONS; i++)
    {
        if (lpQWeapons[i])
            lpQWeapons[i]->Release();
        lpQWeapons[i] = NULL;
    }
    // release quick spells
    for (i = 0; i < QI_MAX_SPELLS; i++)
    {
        if (lpQSpells[i])
            lpQSpells[i]->Release();
        lpQSpells[i] = NULL;
    }
    // release armors
    for (i = 0; i < RPG_NUM_ARMOR_TYPES; i++)
    {
        if (lpArmors[i])
            lpArmors[i]->Release();
        lpArmors[i] = NULL;
    }
    for (i = 0; i < questItems.GetSize(); i++)
        questItems[i]->Release();
    questItems.RemoveAll();
}

/////////////////////////////////////////////////////////////////////////////////////
void CUnit::SetSpecific(LPGAMEBASE lpNewSpecific)
{
    if (lpNewSpecific && (lpNewSpecific->GetTypeID() == SPTID_UNIT_SPECIFIC))
        SetUnitSpecific(lpNewSpecific);
    else
        CMapObject::SetSpecific(lpNewSpecific);
}
/////////////////////////////////////////////////////////////////////////////////////
// void CUnit::SetUnitSpecific( LPGAMEBASE pNewUnitSpecific )
// set new unit specific data
void CUnit::SetUnitSpecific(LPGAMEBASE lpNewUnitSpecific)
{
    if (lpUnitSpecific)
    {
        lpUnitSpecific->RegisterUnit(NULL);
        lpUnitSpecific->Release();
    }

    lpUnitSpecific = (CUnitSpecific *)lpNewUnitSpecific;

    if (lpUnitSpecific)
    {
        lpUnitSpecific->RegisterUnit(this);
        lpUnitSpecific->AddRef();
    }
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CUnit::FinalDataLoading()
{
    if (CheckFlag(OBJ_LOADED))
        return TRUE;
    BOOL bRv = CMapObject::FinalDataLoading();
    lpUnitSpecific->FinalDataLoading();
    return bRv;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnit::AddEnemyMask(DWORD dwNewEnemies)
{
    DWORD dwNewEn = dwEnemyMask | dwNewEnemies;
    if (dwEnemyMask != dwNewEn)
    {
        dwEnemyMask |= dwNewEnemies;
        SetFlag(UNIT_RECALCENEMY);
        SetUpdateMask(CMapObject::VLD_MASK_DIPLOMACY);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnit::SetEnemyMask(DWORD dwNewEnemies)
{
    dwEnemyMask = dwNewEnemies;
    lpUnitSpecific->RemovePendingDiplomacy();
    SetFlag(UNIT_RECALCENEMY);
    SetUpdateMask(CMapObject::VLD_MASK_DIPLOMACY);
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnit::SetAggression(EUnitAggression _aggression)
{
    aggression = _aggression;
    SetUpdateMask(CMapObject::VLD_MASK_AGGRESSION);
}
/////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////
// !!! Lowest level interface funtions
/////////////////////////////////////////////////////////////////////////////////////
// - route & walker can be easily found this function marks route for transmission on client
void CUnit::LowNewRoute()
{
    float fpMove, fpAng;
    CAIFullRoute &route = *lpUnitSpecific->GetRoute();
    route.DoNotTurnToAttack(GetID());
    GetSpeeds(fpMove, fpAng);
    walker.SetSpeeds(fpMove, fpAng);
    walker.CreateWalkerRoute(route);
    SetUpdateMask(CMapObject::VLD_MASK_ROUTE);
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnit::LowSetRouteSpeeds(float fpMove, float fpRotate)
{
    CAIFullRoute &route = *lpUnitSpecific->GetRoute();
    route.DoNotTurnToAttack(GetID());
    walker.SetSpeeds(fpMove, fpRotate);
    walker.CreateWalkerRoute(route);
}
/////////////////////////////////////////////////////////////////////////////////////
// set new animation - unique and cyclic parts
static void ShowAnimationName(SADEntry *pAD)
{
    CString szMsg;
    if (pAD)
    {
        szMsg.Format("Animation entry\n %s\n", AnimationFlagsToString(pAD->dwFlags));
        AfxMessageBox(szMsg);
    }
}
void CUnit::LowStartAnimation(BYTE start, BYTE cycle, int nStartSegment, BYTE nSoundSet)
{
    /*
            if ( GetID() > 1000000 )
            {
                    ShowAnimationName( GetAnimationByIndex( start ) );
                    ShowAnimationName( GetAnimationByIndex( cycle ) );
            }
            */
    nStartAnimation = start == 0xfe ? 0xff : start;
    nCycleAnimation = cycle;
    fullStartAnimation = NormalizeAnimationId(nStartAnimation);
    fullCycleAnimation = NormalizeAnimationId(cycle);
    bExtendedAnimationId = false;
    nAnimationSound = nSoundSet;
    nAnimationStartSegment = nStartSegment;
    SetUpdateMask(CMapObject::VLD_MASK_ANIM);
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnit::RestoreExtendedAnimationIds(DWORD start, DWORD cycle)
{
    fullStartAnimation = NormalizeAnimationId(start);
    fullCycleAnimation = NormalizeAnimationId(cycle);
    bExtendedAnimationId = HasWideAnimationId(fullStartAnimation, fullCycleAnimation);
}
void CUnit::LowStartAnimationExtended(DWORD start, DWORD cycle, int segment, BYTE sound)
{
    start = NormalizeAnimationId(start);
    cycle = NormalizeAnimationId(cycle);
    LowStartAnimation(EncodeAnimationId(start), EncodeAnimationId(cycle), segment, sound);
    if (HasWideAnimationId(start, cycle))
    {
        nStartAnimation = 0xfe; // AIDX packet marker, including cycle-only extensions.
        RestoreExtendedAnimationIds(start, cycle);
    }
}
// note - this function should be called before LowSegment() (or mess with Freeze() will arise)
void CUnit::LowAnimationSpeed(EUnitAnimationSpeed speed)
{
    if (animationSpeed == speed)
        return;
    animationSpeed = speed;
    SetUpdateMask(CMapObject::VLD_MASK_FFLAGS);
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnit::LowLocomotionSpeed(EUnitLocomotionSpeed speed)
{
    if (locomotionSpeed == speed)
        return;
    locomotionSpeed = speed;
    SetUpdateMask(CMapObject::VLD_MASK_FFLAGS);
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnit::LowSetPoses(EUnitPose cur, EUnitPose wish)
{
    currentPose = cur;
    wishPose = wish;
    SetUpdateMask(CMapObject::VLD_MASK_POSE);
}
/////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////
BOOL CUnit::RecalcRPGStats(bool bStrengthChanged)
{
    if (!lpUnitSpecific)
        return FALSE;
    return lpUnitSpecific->RecalcRPGStats(bStrengthChanged);
}
/////////////////////////////////////////////////////////////////////////////////////
// export all unit's stats to client format
BOOL CUnit::ExportStats()
{
    // export mapobject's stats
    CMapObject::ExportStats();
    // export unit's stats
    if (lpUnitSpecific)
        lpUnitSpecific->ExportStats();
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnit::LoadDefaultClientStats(const char *pszTemplate)
{
    CMapObjectManager *pM = (CMapObjectManager *)GetManager();
    SRPGCommonStats s1;
    SRPGUnitStats s2;
    s1.Clear();
    s2.Clear();
    pM->SetupStats(&s1, &s2, pszTemplate);
    s1.Export(&rpgUserCommonStats);
    s2.Export(0, &rpgUserUnitStats);
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnit::ShowHelmet(bool bShow)
{
    CModPackage *mod = ModRuntime::Current();
    auto apply = [mod, bShow](CUnit *unit) {
        if (!unit)
            return;
        const bool show = bShow && !(mod && mod->HidePartyHelmets() && mod->IsLocalPartyUnit(*unit));
        if (show)
            unit->ResetFlag(UNIT_HELMET_REMOVE);
        else
            unit->SetFlag(UNIT_HELMET_REMOVE);
        unit->SetUpdateMask(CMapObject::VLD_MASK_INVENTORY);
    };
    apply(this);
    if (mod && mod->PartyVisualOptionsEnabled())
    {
        if (theWorld.GetMapObjectManager())
        {
            CUnit *client = theWorld.GetUnit(GetID());
            if (client != this)
                apply(client);
        }
        if (theWorldServer.GetMapObjectManager())
        {
            CUnit *server = theWorldServer.GetUnit(GetID());
            if (server != this)
                apply(server);
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnit::ConvertItemsIDs(SItemIDs &res)
{
    int i;
    for (i = 0; i < QI_MAX_WEAPONS; i++)
    {
        if (!lpQWeapons[i])
            res.dwWeaponsID[i] = -1;
        else
            res.dwWeaponsID[i] = lpQWeapons[i]->GetSubtypeID();
    }
    for (i = 0; i < RPG_NUM_ARMOR_TYPES; i++)
    {
        if (lpArmors[i])
            res.dwArmorsID[i] = lpArmors[i]->GetSubtypeID();
        else
            res.dwArmorsID[i] = -1;
    }
    if (auto *mod = ModRuntime::Current())
        mod->FilterPartyVisualArmor(*this, res.dwArmorsID);
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnit::RefreshPartyVisualArmor()
{
    if (!lpUnitSpecific)
        return;
    SItemIDs visualItems;
    ConvertItemsIDs(visualItems);
    lpUnitSpecific->SetArmors(visualItems.dwArmorsID, RPG_NUM_ARMOR_TYPES);
    lpUnitSpecific->NotifyItemsUpdated();
}
/////////////////////////////////////////////////////////////////////////////////////
struct SUnitFFlags
{
    char cASpeed : 3;
    char cMSpeed : 3;
    char cDead : 1;
    char cMoving : 1;
};
/////////////////////////////////////////////////////////////////////////////////////
// compose packet with data and sent it via buffer manager
void CUnit::UpdateTo()
{
    CUpdateHistory *pPkt;
    DWORD i;
    // mapobject update
    CMapObject::UpdateTo();

    // unit RPG stats
    if (pPkt = update.GetHistory(VLD_MASK_UNIT_STATS))
    {
        ::UpdateTo(rpgUserUnitStats, *pPkt);
        pPkt->FinishUpdate();
    }
    // backpack update: quick weapons, items and spells + weared armor
    if (pPkt = update.GetHistory(VLD_MASK_INVENTORY))
    {
        {
            CUpdateHistory &pkt = *pPkt;
            SItemIDs items;
            ConvertItemsIDs(items);
            // quick weapon IDs
            for (i = 0; i < QI_MAX_WEAPONS; i++)
                pkt << items.dwWeaponsID[i];
            // helm
            if (!CheckFlag(UNIT_HELMET_REMOVE))
                pkt << items.dwArmorsID[0];
            else
                pkt << (DWORD)-1;
            //
            for (i = 1; i < RPG_NUM_ARMOR_TYPES; i++)
                pkt << items.dwArmorsID[i];
            /*
                            for ( i=0; i<QI_MAX_WEAPONS; i++ )
                            {
                                    if ( !lpQWeapons[i] )
                                            pkt << (int) 0;
                                    else
                                            pkt << (int) lpQWeapons[i]->GetID();
                            }
                            for ( i=0; i<QI_MAX_ITEMS; i++ )
                            {
                                    if ( !lpQItems[i] )
                                            pkt << (int) 0;
                                    else
                                            pkt << (int) lpQItems[i]->GetID();
                            }
                            // weared armor
                            for ( i=0; i<RPG_NUM_ARMOR_TYPES; i++ )
                            {
                                    if ( lpArmors[i] )
                                            pkt << (int) lpArmors[i]->GetID();
                                    else
                                            pkt << (int) 0;
                            }
                            // qspells armor
                            for ( i=0; i<QI_MAX_SPELLS; i++ )
                            {
                                    if ( lpQSpells[i] )
                                            pkt << (int) lpQSpells[i]->GetID();
                                    else
                                            pkt << (int) 0;
                            }*/
            pkt.FinishUpdate();
            if (lpUnitSpecific->WhoAmI() == HPC_UNITSERVERSPECIFIC)
                lpUnitSpecific->NotifyItemsUpdated();
        }
    }
    if (pPkt = update.GetHistory(VLD_MASK_POSE))
    {
        {
            CUpdateHistory &pkt = *pPkt; // current pose, current weapon
            pkt.Write(&currentPose, sizeof(currentPose));
            pkt.Write(&wishPose, sizeof(wishPose));
            pkt << dwCurrentWeapon;
            pkt.FinishUpdate();
        }
    }
    if (pPkt = update.GetHistory(VLD_MASK_FFLAGS))
    {
        {
            CUpdateHistory &pkt = *pPkt;
            SUnitFFlags fflags; // animation speed, movement speed, dead flag
            fflags.cASpeed = animationSpeed;
            fflags.cMSpeed = locomotionSpeed;
            fflags.cDead = bDead;
            fflags.cMoving = bMoving;
            pkt.Write(&fflags, sizeof(fflags));
            pkt.FinishUpdate();
        }
    }
    if (pPkt = update.GetHistory(VLD_MASK_ANIM))
    {
        {
            CUpdateHistory &pkt = *pPkt;
            // animations start time & animations itself
            pkt << nStartAnimation;
            pkt << nCycleAnimation;
            pkt << nAnimationStartSegment; // can be optimized to byte by storing difference
            pkt << nAnimationSound;
            if (auto* mod = ModRuntime::Current())
                if (mod->CombatSetting("AttackAnimSpeed"))
                    pkt.Write(&fpAnimationCoeff, sizeof(fpAnimationCoeff));
            if (nStartAnimation == 0xfe && bExtendedAnimationId)
            {
                SAnimationIdPayload payload = { { 'A', 'I', 'D', 'X' }, 1,
                    static_cast<BYTE>((fullStartAnimation != 0xffffffffu ? 1 : 0) |
                                      (fullCycleAnimation != 0xffffffffu ? 2 : 0)),
                    0, fullStartAnimation, fullCycleAnimation };
                pkt.Write(&payload, sizeof(payload));
            }
            pkt.FinishUpdate();
        }
    }
    // unit's name
    if (pPkt = update.GetHistory(VLD_MASK_NAME))
    {
        pPkt->WriteString(szName);
        pPkt->FinishUpdate();
    }
    // route
    if (pPkt = update.GetHistory(VLD_MASK_ROUTE))
    {
        lpUnitSpecific->GetRoute()->UpdateTo(*pPkt);
        pPkt->FinishUpdate();
    }
    // diplomacy
    if (pPkt = update.GetHistory(VLD_MASK_DIPLOMACY))
    {
        {
            CUpdateHistory &pkt = *pPkt;
            int nOwner = -1;
            if (lpOwner)
                nOwner = lpOwner->GetID();
            pkt << (dwEnemyMask & dwPossibleEnemies);
            pkt << (BYTE)nPlayerNum;
            pkt << nOwner;
            pkt.FinishUpdate();
        }
    }
    if (pPkt = update.GetHistory(VLD_MASK_DETECTIONS))
    {
        {
            CUpdateHistory &pkt = *pPkt;
            pkt.Write(&sensesDetection, sizeof(sensesDetection));
            pkt.FinishUpdate();
        }
    }
    if (pPkt = update.GetHistory(VLD_MASK_AGGRESSION))
    {
        {
            CUpdateHistory &pkt = *pPkt;
            pkt.Write(&aggression, sizeof(aggression));
            pkt.FinishUpdate();
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// update this unit from buffer manager packet
BOOL CUnit::UpdateFrom(EUpdateType tip)
{
    CUpdateHistory *pPkt;
    SItemIDs items;
    BOOL bRv = TRUE;
    BOOL bCallNotifyItems = FALSE, bCallNewItems = FALSE;
    float fOldHP = float(rpgUserCommonStats.nHP) / float(rpgUserCommonStats.nMaxHP);

    CPlot3D vDirection;
    int i;
    // mapobject update
    bRv = CMapObject::UpdateFrom(tip);
    // unit RPG stats
    if (pPkt = update.GetHistory(VLD_MASK_UNIT_STATS))
    {
        int hl;
        float fPrevHits[RPG_NUM_HIT_LOCATIONS];
        for (hl = 0; hl < RPG_NUM_HIT_LOCATIONS; hl++)
            fPrevHits[hl] = float(short(rpgUserUnitStats.hls[hl].wHP)) /
                            float(rpgUserUnitStats.hls[hl].wMaxHP);

        ::UpdateFrom(rpgUserUnitStats, *pPkt);

        if (!IsDead() && tip == utFrame)
        {
            bool bDamaged = false;
            for (hl = 0; hl < RPG_NUM_HIT_LOCATIONS; hl++)
            {
                float fHitsValue = float(short(rpgUserUnitStats.hls[hl].wHP)) /
                                   float(rpgUserUnitStats.hls[hl].wMaxHP);
                // check for damaging/healing for wounds processing
                if (fabs(fHitsValue - fPrevHits[hl]) > 1e-4f)
                    bDamaged = true;
                //
                if (fabs(fPrevHits[hl] - fHitsValue) > 1e-4f)
                    lpUnitSpecific->HLDamaged(ERPGHitLocations(hl), fPrevHits[hl] - fHitsValue,
                                              rpgUserUnitStats.bFlags);
            }
            if (bDamaged)
                lpUnitSpecific->NotifyHitPointsUpdated();
        }
        else if (tip == utStart) // update HitPoints-relative information for the unit on the start
            lpUnitSpecific->NotifyHitPointsUpdated();

        float fCurrHP = float(rpgUserCommonStats.nHP) / float(rpgUserCommonStats.nMaxHP);
        if (((fOldHP > fCurrHP) || (rpgUserUnitStats.bFlags & RPGSTAT_SHOWHPFLY)) && tip != utStart)
        {
            DWORD dwFlags = 0;
            if (rpgUserUnitStats.bFlags & RPGSTAT_CRITICAL)
                dwFlags |= HLDAMAGE_CRITICAL;
            if (rpgUserUnitStats.bFlags & RPGSTAT_BACKSTAB)
                dwFlags |= HLDAMAGE_BACKSTAB;
            if (rpgUserUnitStats.bFlags & RPGSTAT_ELECTRIC)
                dwFlags |= HLDAMAGE_ELECTRIC;
            GetMapObjectSpecific()->ReceiveDamage((fOldHP - fCurrHP) * rpgUserCommonStats.nMaxHP,
                                                  dwFlags);
            if (dwFlags & HLDAMAGE_ELECTRIC)
                lpUnitSpecific->ElectricDamage(fOldHP - fCurrHP);
        }

        bRv &= pPkt->UpdateFromIsOk();
        pPkt->FinishUpdate();
    }
    // backpack update: quick weapons, items and spells + weared armor
    if (pPkt = update.GetHistory(VLD_MASK_INVENTORY))
    {
        {
            CUpdateHistory &pkt = *pPkt;
            ASSERT("Tak dogovorilis 3aug99" && !GetWorld()->GetItemManager()->Find(0));
            RemoveAllItems();
            //
            for (i = 0; i < QI_MAX_WEAPONS; i++)
                pkt >> items.dwWeaponsID[i];
            for (i = 0; i < RPG_NUM_ARMOR_TYPES; i++)
                pkt >> items.dwArmorsID[i];
            /*
                            // set weapon by IDs
                            for ( i=0; i<QI_MAX_WEAPONS; i++ )
                            {{
                                    pkt >> nID;
                                    CItemWeapon *pWeapon =
               (CItemWeapon*)GetWorld()->GetItemManager()->Find( nID ); if ( pWeapon ) AddWeapon(
               pWeapon ); else if ( nID != 0 )
                                    {
                                            CString szError;
                                            szError.Format( "Can't find weapon ID = %d.\n", nID );
                                            ReportError( szError );
                                    }
                            }}
                            //
                            // set qitem by IDs
                            for ( i=0; i<QI_MAX_ITEMS; i++ )
                            {
                                    pkt >> nID;
                                    lpQItems[i] =  (CItemQuick*)GetWorld()->GetItemManager()->Find(
               nID ); if ( lpQItems[i] ) lpQItems[i]->AddRef(); if ( lpQItems[i] == NULL && nID != 0
               )
                                    {
                                            CString szError;
                                            szError.Format( "Can't find item ID = %d.\n", nID );
                                            ReportError( szError );
                                    }
                            }
                            //
                            // weared armor
                            // set armors by IDs
                            for ( i=0; i<RPG_NUM_ARMOR_TYPES; i++ )
                            {
                                    pkt >> nID;
                                    CItemArmor* lpArmor =
               (CItemArmor*)GetWorld()->GetItemManager()->Find( nID ); if ( lpArmor ) Wear( lpArmor
               ); else if ( nID != 0 )
                                    {
                                            CString szError;
                                            szError.Format( "Can't find armor ID = %d.\n", nID );
                                            ReportError( szError );
                                    }
                            }
                            //
                            // quick spells
                            // set spell by IDs
                            for ( i=0; i<QI_MAX_SPELLS; i++ )
                            {
                                    pkt >> nID;
                                    CItemSpell* lpItemSpell =
               (CItemSpell*)GetWorld()->GetItemManager()->Find( nID ); if ( lpItemSpell ) AddQSpell(
               lpItemSpell ); else if ( nID != 0 )
                                    {
                                            CString szError;
                                            szError.Format( "Can't find spell item ID = %d.\n", nID
               ); ReportError( szError );
                                    }
                            }*/
            bCallNewItems = TRUE;
            bCallNotifyItems = TRUE;

            bRv &= pkt.UpdateFromIsOk();
            pkt.FinishUpdate();
        }
    }
    //
    if (pPkt = update.GetHistory(VLD_MASK_POSE))
    {
        {
            DWORD dwNewWeapon;
            CUpdateHistory &pkt = *pPkt; // current pose, current weapon
            pkt.Read(&currentPose, sizeof(currentPose));
            pkt.Read(&wishPose, sizeof(wishPose));
            pkt >> dwNewWeapon;
            if (dwCurrentWeapon != dwNewWeapon)
            {
                bCallNotifyItems = TRUE;
                ChangeWeapon(dwNewWeapon);
            }
            dwWishWeapon = dwCurrentWeapon;
            pkt.FinishUpdate();
        }
    }
    if (pPkt = update.GetHistory(VLD_MASK_FFLAGS))
    {
        {
            CUpdateHistory &pkt = *pPkt;
            SUnitFFlags fflags; // animation speed, movement speed, dead flag
            pkt.Read(&fflags, sizeof(fflags));
            animationSpeed = (EUnitAnimationSpeed)fflags.cASpeed;
            locomotionSpeed = (EUnitLocomotionSpeed)fflags.cMSpeed;
            bDead = fflags.cDead;
            bMoving = fflags.cMoving;
            pkt.FinishUpdate();
        }
    }
    if (pPkt = update.GetHistory(VLD_MASK_ANIM))
    {
        {
            CUpdateHistory &pkt = *pPkt;
            // animations start time & animations itself
            pkt >> nStartAnimation;
            pkt >> nCycleAnimation;
            pkt >> nAnimationStartSegment; // can be optimized to byte by storing difference
            pkt >> nAnimationSound;
            if (auto* mod = ModRuntime::Current())
                if (mod->CombatSetting("AttackAnimSpeed"))
                    pkt.Read(&fpAnimationCoeff, sizeof(fpAnimationCoeff));
            fullStartAnimation = NormalizeAnimationId(nStartAnimation);
            fullCycleAnimation = NormalizeAnimationId(nCycleAnimation);
            bExtendedAnimationId = false;
            if (nStartAnimation == 0xfe)
            {
                SAnimationIdPayload payload = {};
                if (pkt.CanRead(sizeof(payload)))
                {
                    pkt.Read(&payload, sizeof(payload));
                    const BYTE flags = static_cast<BYTE>(
                        (payload.start != 0xffffffffu ? 1 : 0) |
                        (payload.cycle != 0xffffffffu ? 2 : 0));
                    if (memcmp(payload.magic, "AIDX", 4) == 0 &&
                        payload.version == 1 && payload.reserved == 0 &&
                        payload.flags == flags &&
                        HasWideAnimationId(payload.start, payload.cycle) &&
                        (payload.start == 0xffffffffu || GetAnimationByIndex(payload.start)) &&
                        (payload.cycle == 0xffffffffu || GetAnimationByIndex(payload.cycle)))
                        RestoreExtendedAnimationIds(payload.start, payload.cycle);
                }
                if (!bExtendedAnimationId)
                {
                    nStartAnimation = nCycleAnimation = 0xff;
                    fullStartAnimation = fullCycleAnimation = 0xffffffffu;
                }
            }
            if (animationSpeed == UAS_FROZEN)
                nAnimationStartSegment--;
            lpUnitSpecific->NewAnimationWasReceived(GetWorldBase()->GetSegmentTimeCounter());
            pkt.FinishUpdate();
        }
    }
    // name
    if (pPkt = update.GetHistory(VLD_MASK_NAME))
    {
        szName = pPkt->ReadString();

        bRv &= pPkt->UpdateFromIsOk();
        pPkt->FinishUpdate();
    }
    // route
    if (pPkt = update.GetHistory(VLD_MASK_ROUTE))
    {
        CAIFullRoute &route = *lpUnitSpecific->GetRoute();
        route.UpdateFrom(*pPkt);
        if (locomotionSpeed == ULS_FROZEN)
            route.Freeze(-1); // из-за особенностей синхронизации, на сервере уже прибавили
                              // StartSegment в этом случае

        LowNewRoute();
        // if ( route.GetMoveType() == CAIFullRoute::Stand )
        lpUnitSpecific->NewRouteReceived();

        bRv &= pPkt->UpdateFromIsOk();
        pPkt->FinishUpdate();
    }
    // diplomacy
    if (pPkt = update.GetHistory(VLD_MASK_DIPLOMACY))
    {
        {
            CUpdateHistory &pkt = *pPkt;
            int nOwner;
            pkt >> dwEnemyMask;
            nPlayerNum = 0;
            pkt >> (*(BYTE *)&nPlayerNum);
            pkt >> nOwner;
            if (nOwner != -1)
            {
                if (!lpOwner || lpOwner->GetID() != nOwner)
                {
                    CWorldClient *pW = (CWorldClient *)GetWorld();
                    CPlayerClient *pPC = pW->GetPlayer();
                    CPlayer *pPlayer = pPC->GetPlayer();
                    if (pPlayer->GetID() == nOwner || pPC->IsIgnoreInterfaceChange())
                        SetOwner(pPlayer);
                    // SetOwner( (CPlayer*)GetWorld()->GetPlayerManager()->Find( nOwner ) );
                }
            }
            else
                SetOwner(0);

            bRv &= pkt.UpdateFromIsOk();
            pkt.FinishUpdate();
        }
    }
    if (pPkt = update.GetHistory(VLD_MASK_DETECTIONS))
    {
        {
            CUpdateHistory &pkt = *pPkt;
            pkt.Read(&sensesDetection, sizeof(sensesDetection));
            pkt.FinishUpdate();
        }
    }
    if (pPkt = update.GetHistory(VLD_MASK_AGGRESSION))
    {
        {
            CUpdateHistory &pkt = *pPkt;
            pkt.Read(&aggression, sizeof(aggression));
            pkt.FinishUpdate();
        }
    }
    // refresh spell effects if needed
    if (pPkt = update.GetHistory(VLD_MASK_SPELLEFFECTS))
    {
        GetMapObjectSpecific()->UpdateSpellEffectVisualization(tip == utStart);
        pPkt->FinishUpdate();
    }
    // refresh spell effects if needed
    if (bCallNotifyItems)
    {
        if (bCallNewItems)
        {
            lpUnitSpecific->SetArmors(items.dwArmorsID, RPG_NUM_ARMOR_TYPES);
            lpUnitSpecific->SetWeapons(items.dwWeaponsID, QI_MAX_WEAPONS);
        }
        lpUnitSpecific->ChangeWeapon(dwCurrentWeapon);
        lpUnitSpecific->NotifyItemsUpdated();
    }
    return bRv;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnit::NewVerticalPos(float fpZ)
{
    fpBaseZ = fpZ;
    cp.z = CalcZ(cp.x, cp.y);
}
/////////////////////////////////////////////////////////////////////////////////////
// int CUnit::Segment()
int CUnit::Segment()
{
    lpUnitSpecific->Segment();
    CMapObject::Segment();
    /*	switch ( phase )
            {
                    case UF_MOVEIDLE:
                    case UF_MOVE:
                    case UF_ROTATE:
                    case UF_SUFFERMOVE:
                    case UF_STUN:
                            lpUnitSpecific->ExecMoveUnit( 1 );
                            break;
            }*/
    return 1;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnit::SetStunFlag(BOOL bStun)
{
    if (bStun)
        SetFlag(UNIT_STUN);
    else
        ResetFlag(UNIT_STUN);
}
/////////////////////////////////////////////////////////////////////////////////////
EUnitCastFailure CUnit::CheckIsCastPossible(DWORD dwQSpellIndex, CPlot3D &ptTarget)
{
    // when updating this code don`t forget to update CUnitServer::IsCastPossible
    CSpell *pSpell;
    if (dwQSpellIndex >= GetSpellSlotCount())
        return UCS_NOSPELL;
    pSpell = GetQSpell(dwQSpellIndex);
    if (!pSpell)
        return UCS_NOSPELL;
    if(auto*mod=ModRuntime::Current()){
        ModSkills::Config config;if((mod->TargetSkill(pSpell)||mod->PointSkill(pSpell))&&mod->SkillConfig(pSpell,config))
            return mod->SkillReady(*this,pSpell,config,false)&&config.Cost(pSpell->GetManaCost())<=GetMP()?UCS_OK:UCS_NOMANA;
    }
    if (HasSpellEffect(SPELL_ID_FEEBLEMIND))return UCS_FEEBLED;
    if (pSpell->GetManaCost() * rpgUserUnitStats.GetManaBonusMultiplier() > GetMP())
        return UCS_NOMANA;
    // if ( fabs2( ptTarget - GetCP() ) > sqr( pSpell->GetRange() * 1.02f ) )
    // return UCS_RANGE;
    return UCS_OK;
}
/////////////////////////////////////////////////////////////////////////////////////
EUnitCastFailure CUnit::CheckIsWandPossible(DWORD dwQItemIndex, CPlot3D &ptTarget)
{
    CItem *pItem;
    pItem = GetQItem(dwQItemIndex);
    if (!pItem || (pItem->WhoAmI() != HPC_ITEM_QUICK) ||
        !((((CItemQuick *)pItem)->GetQuickItemType() == IDB_WAND) ||
          (((CItemQuick *)pItem)->GetQuickItemType() == IDB_SCROLL)))
        return UCS_NOSPELL;
    CSpell *pSpell = pItem->GetSpell(0);
    // check mana capacity only for wands
    if (((CItemQuick *)pItem)->GetQuickItemType() == IDB_WAND)
    {
        if (pSpell->GetManaCost() > pItem->GetMP())
            return UCS_NOMANA;
    }
    // if ( fabs2( ptTarget - GetCP() ) > sqr( pSpell->GetRange() * 1.02f ) )
    // return UCS_RANGE;
    return UCS_OK;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnit::GetSpeeds(float &fpMoveSpeed, float &fpRotateSpeed)
{
    fpMoveSpeed = rpgUserUnitStats.cMove * 10.0f / 255.0f;
    fpRotateSpeed = fpMoveSpeed;
    switch (currentPose)
    {
    case UP_LAY: // ползти
        fpMoveSpeed *= rpgUserUnitStats.cSpeeds[3] * FP_RPG_MAX_SPEED / 255.0f;
        fpRotateSpeed = fpMoveSpeed;
        break;
    case UP_CRAWL: // перебежками
        fpMoveSpeed *= rpgUserUnitStats.cSpeeds[2] * FP_RPG_MAX_SPEED / 255.0f;
        fpRotateSpeed = fpMoveSpeed;
        break;
    case UP_REST:
    case UP_STAND: // идти стоя
        fpRotateSpeed = fpMoveSpeed * rpgUserUnitStats.cSpeeds[0] * FP_RPG_MAX_SPEED / 255.0f;
        fpMoveSpeed *= rpgUserUnitStats.cSpeeds[1] * FP_RPG_MAX_SPEED / 255.0f;
        break;
    case UP_RUN: // бежать
        fpMoveSpeed *= rpgUserUnitStats.cSpeeds[0] * FP_RPG_MAX_SPEED / 255.0f;
        fpRotateSpeed = fpMoveSpeed;
        break;
    }
    if(auto*mod=ModRuntime::Current()){float factor=mod->CrippledSpeed(*this);fpMoveSpeed*=factor;fpRotateSpeed*=factor;}
    fpRotateSpeed *= theAIGeneralSettings.fpRotateSpeedMult;
}
/////////////////////////////////////////////////////////////////////////////////////
float CUnit::GetShowSpeed()
{
    return GetShowSpeed(wishPose);
}
/////////////////////////////////////////////////////////////////////////////////////
float CUnit::GetShowSpeed(EUnitPose pose)
{
    float fpMoveSpeed = rpgUserUnitStats.cMove * 10.0f / 255.0f;
    switch (pose)
    {
    case UP_LAY: // ползти
        fpMoveSpeed *= rpgUserUnitStats.cSpeeds[3] * FP_RPG_MAX_SPEED / 255.0f;
        break;
    case UP_CRAWL: // перебежками
        fpMoveSpeed *= rpgUserUnitStats.cSpeeds[2] * FP_RPG_MAX_SPEED / 255.0f;
        break;
    case UP_REST:
    case UP_STAND: // идти стоя
        fpMoveSpeed *= rpgUserUnitStats.cSpeeds[1] * FP_RPG_MAX_SPEED / 255.0f;
        break;
    case UP_RUN: // бежать
        fpMoveSpeed *= rpgUserUnitStats.cSpeeds[0] * FP_RPG_MAX_SPEED / 255.0f;
        break;
    }
    if(auto*mod=ModRuntime::Current())fpMoveSpeed*=mod->CrippledSpeed(*this);
    return fpMoveSpeed;
}
/////////////////////////////////////////////////////////////////////////////////////
float CUnit::GetMaxAttackPathLength(float fpToTarget)
{
    if (IsHero())
        return CheckFlag(UNIT_DONTMOVE) ? Min(10.0f, fpToTarget) * 3 + 10 : 1e6f;
    return fpToTarget * 3 + 10;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnit::SetExactPosition(float fpTime)
{
    if (lpUnitSpecific)
        lpUnitSpecific->SetExactPosition(fpTime);
    // если это наш перс, то проявим следы. которые он увидел
    if (IsIDofPersonage(GetID()))
    {
        float fVision = rpgUserUnitStats.GetSightRange(theWorld.GetNightPowerSlow());
        if (currentPose == UP_LAY)
            fVision *= AI_LAY_SENSE_VISION;
        theWorld.VisualizePrints(GetCP(), fVision);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
CPlot3D CUnit::GetFuturePosition(int nSegment)
{
    CAIFullRoute &route = *lpUnitSpecific->GetRoute();
    if (IsDead())
        return GetTargetCP();
    if (!route.IsMoving() || CheckFlag(UNIT_GETFUTUREPOS))
        return GetCP();

    float fpSpeed;
    int nCheckTileX, nCheckTileY;
    bool bRotate;
    CPlot3D pos, vec;
    pos = GetCP();
    pos.x = Meter2AITile(pos.x);
    pos.y = Meter2AITile(pos.y);
    walker.GetPosition(GetWorldBase(), route, nSegment, bRotate, pos, vec, fpSpeed, nCheckTileX,
                       nCheckTileY);
    pos.x = AITile2Meter(pos.x);
    pos.y = AITile2Meter(pos.y);
    return pos;
}
/////////////////////////////////////////////////////////////////////////////////////
CPlot3D CUnit::GetTargetCP()
{
    if (IsDead())
    {
        CPlot3D ptAdd(0, 0, 0);
        const SADEntry *pAnim = GetAnimationByIndex(fullStartAnimation);
        if (pAnim)
        {
            if ((pAnim->dwFlags & ADB_ACTIONS) == ADB_ACTION_DEATH)
            {
                switch (pAnim->dwFlags2 & ADB_MODIFIER_DIRECTION2_MASK)
                {
                case ADB_MODIFIER_DIRECTION2_FM:
                    ptAdd = -GetYAxis() * bv.ptMax.y;
                    break;
                case ADB_MODIFIER_DIRECTION2_BM:
                    ptAdd = GetYAxis() * bv.ptMax.y;
                    break;
                case ADB_MODIFIER_DIRECTION2_ML:
                    ptAdd = GetXAxis() * bv.ptMax.x;
                    break;
                case ADB_MODIFIER_DIRECTION2_MR:
                    ptAdd = -GetXAxis() * bv.ptMax.x;
                    break;
                }
            }
        }
        CWorldServer *pWS = (CWorldServer *)GetWorldBase();
        if (pWS->IsServer())
        {
            if (!pWS->aiMap.CanPutUnit(3, GetCP() + ptAdd))
                return GetCP();
        }
        return GetCP() + ptAdd;
    }
    return GetCP();
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnit::SetOwner(CPlayer *lpNewOwner)
{
    if (lpOwner != lpNewOwner)
    {
        CPlayer *lpOldOwner = lpOwner;
        lpOwner = lpNewOwner;
        if (lpOldOwner)
            lpOldOwner->RemoveUnitFromControl(this);
        if (lpOwner)
            lpOwner->AddUnitUnderControl(this);
        SetUpdateMask(CMapObject::VLD_MASK_DIPLOMACY);

        if (lpUnitSpecific)
            lpUnitSpecific->SetOwner(lpNewOwner);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnit::SetPlayerNum(int nNewPlayer)
{
    nPlayerNum = nNewPlayer;
    SetFlag(UNIT_RECALCENEMY);
    if (lpUnitSpecific)
        SetUpdateMask(CMapObject::VLD_MASK_DIPLOMACY);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CUnit::IsEnemy(CUnit *pUnit)
{
    int nPlayerNum;

    ASSERT(pUnit);

    nPlayerNum = pUnit->GetPlayerNum();

    ASSERT(nPlayerNum >= 0 && nPlayerNum < N_MAX_PLAYERS);

    return dwEnemyMask & dwPossibleEnemies & (1 << nPlayerNum);
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnit::SetPossibleEnemiesMask(DWORD dwPosEnemies)
{
    ASSERT(dwPossibleEnemies == 0xFFFFFFFF || dwPosEnemies == 0xFFFFFFFF);
    dwPossibleEnemies = dwPosEnemies;
    SetUpdateMask(CMapObject::VLD_MASK_DIPLOMACY);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CUnit::IsHero() const
{
    return lpOwner != NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
const SADEntry *CUnit::GetAnimation(DWORD dwAnimationFlags, bool bWeapon)
{
    switch (GetTypeID())
    {
    case MOTID_HUMAN:
    case MOTID_MONSTER:
    case MOTID_ANIMAL:
        break;
    default:
        return nullptr;
    }

    const SADEntry *pEntry = GetRandomAnimationEntry(dwAnimationFlags);
    if (!pEntry)
    {
        // анимации с оружием найти не удалось - может есть без оружия ?
        dwAnimationFlags &= ~ADB_WEAPONS;
        pEntry = GetRandomAnimationEntry(dwAnimationFlags);
        // если не находится rest, то используем обычный neutral или attack
        if (!pEntry && (dwAnimationFlags & ADB_STATES) == ADB_STATE_REST)
        {
            pEntry = GetRandomAnimationEntry((dwAnimationFlags & (~ADB_STATES)) |
                                             ADB_STATE_NEUTRAL);
            if (!pEntry)
                pEntry = GetRandomAnimationEntry((dwAnimationFlags & (~ADB_STATES)) |
                                                 ADB_STATE_ATTACK);
        }
        // попробуем найти анимацию не взирая на оружие
        if (!pEntry && bWeapon)
            pEntry = GetRandomAnimationEntryNoWeapon(dwAnimationFlags);

#if __NOTIFY_FILE__
        if (pEntry == nullptr)
        {
            ReportWarning("Can't find animation entry %s for \"%s\"",
                          AnimationFlagsToString(dwAnimationFlags), szMaskName.GetString());
            // pEntry = GetAnimationEntryByIndex(0);
        }
#endif
    }
    // Redirect the addon's two-handed interaction clips before their indices
    // enter the byte-sized animation sequence.
    if (pEntry && GetTypeID() == MOTID_HUMAN &&
        ModRuntime::Current() && ModRuntime::Current()->CombatSetting("JMVAttacks"))
    {
        const char *name = pEntry->pszAnimationName;
        const char *suffix = nullptr;
        if (_strnicmp(name, "2hauspecial", 11) == 0 ||
            _strnicmp(name, "2hsuspecial", 11) == 0)
            suffix = name + 11;
        if (suffix && (_stricmp(suffix, "22") == 0 ||
                       _stricmp(suffix, "04") == 0 ||
                       _stricmp(suffix, "05") == 0 ||
                       _stricmp(suffix, "06") == 0 ||
                       _stricmp(suffix, "07") == 0 ||
                       _stricmp(suffix, "24") == 0))
        {
            CString ordinary = CString("uspecial") + suffix;
            if (const SADEntry *replacement = GetAnimationEntry(ordinary))
                pEntry = replacement;
        }
    }
    return pEntry;
}
/////////////////////////////////////////////////////////////////////////////////////
const SADEntry *CUnit::GetAnimationByIndex(DWORD nIndex)
{
    const SADEntry *pEntry = GetAnimationEntryByIndex(nIndex);
    if (pEntry == NULL)
        ReportWarning("Can't find animation entry index\n %i\n for \"%s\".\n", (int)nIndex,
                      szMaskName);

    return pEntry;
}
/////////////////////////////////////////////////////////////////////////////////////
float CUnit::CalcZ(float fpX, float fpY)
{
    CWorldBase *pWorld = GetWorldBase();
    if (rpgUserUnitStats.cLocomotion == 2)
    {
        if (lpUnitSpecific->WhoAmI() == HPC_UNITSERVERSPECIFIC)
            return pWorld->GetBridgeSurfaceZValue(fpX, fpY) + cp.z;
        else
            return pWorld->GetBridgeSurfaceZValue(fpX, fpY) + fpBaseZ;
    }
    else
        return pWorld->GetBridgeZValue(fpX, fpY);
}
/////////////////////////////////////////////////////////////////////////////////////
// get units head height above the ground
float CUnit::GetUnitHeadHeight() const
{
    float fpH;
    if (bv.IsBox())
        fpH = bv.ptMax.z * 1.8f;
    else
        fpH = bv.ptCenter.z + 0.7 * bv.fpRadius;
    if (IsLaying() || IsDead())
        fpH *= 0.2f;
    else if (IsCrawling())
        fpH *= 0.7f;
    return fpH;
}
/////////////////////////////////////////////////////////////////////////////////////
float CUnit::GetStandUnitHeadHeight() const
{
    float fpH;
    if (bv.IsBox())
        fpH = bv.ptMax.z * 1.8f;
    else
        fpH = bv.ptCenter.z + 0.7 * bv.fpRadius;
    return fpH;
}
/////////////////////////////////////////////////////////////////////////////////////
// obtain unit`s head position for shooting, looking etc
CPlot3D CUnit::GetUnitHeadPosition()
{
    CPlot3D ptView = GetCP();
    ptView.z = GetUnitHeadHeight();
    ptView.z += CalcZ(ptView.x, ptView.y);
    return ptView;
}
/////////////////////////////////////////////////////////////////////////////////////
float CUnit::GetUnitRadius()
{
    int nIdx = GetFigureIndexByName("bd");
    if (nIdx < 0)
    {
        nIdx = GetFigureIndexByName("chest");
        if (nIdx < 0)
            return 0;
    }
    float fpKoef = 1;
    if (GetTypeID() == MOTID_MONSTER && strcmp(GetMaskName(), "unmosp") == 0)
        fpKoef = 2;
    return fpKoef * (float)sqrt(sqr(fbvs[nIdx].ptMax.x) + sqr(fbvs[nIdx].ptMax.y));
}
/////////////////////////////////////////////////////////////////////////////////////
float CUnit::GetUnitHeight()
{
    return bv.ptMax.z * 2;
}
/////////////////////////////////////////////////////////////////////////////////////
// пересчитать различные параметры юнита после смены позы
void CUnit::RecalcAIParams()
{
    lpUnitSpecific->RecalcAIParams();
    if (lpUnitSpecific->WhoAmI() == HPC_UNITSERVERSPECIFIC)
    {
        switch (currentPose)
        {
        case UP_CRAWL:
            nAIClass = ((CUnitServer *)lpUnitSpecific)->rpgUnitStats.wAIClassCrawl;
            break;
        case UP_LAY:
            nAIClass = ((CUnitServer *)lpUnitSpecific)->rpgUnitStats.wAIClassLie;
            break;
        default:
            nAIClass = ((CUnitServer *)lpUnitSpecific)->rpgUnitStats.wAIClassStay;
            break;
        }
        // fpAttackDistance = ((CUnitServer*)lpUnitSpecific)->rpgUnitStats.fpAttackDistance;
        fpCollisionDistance = Max(bv.ptMax.x, bv.ptMax.y); //(bv.ptMax.x + bv.ptMax.y) / 2;//sqrt(
                                                           // sqr(bv.ptMax.x) + sqr(bv.ptMax.y) );
        fpCollisionDistance *= 0.9f;
        fpAttackDistance = fpCollisionDistance + 0.05f; // fpCollisionDistance + 0.366f;
        if (GetOwner())
            fpCollisionDistance += 0.2f;
        fpCollisionDistance = Min(fpCollisionDistance, FP_AI_MAX_LOCK_RADIUS);
        if (nAIClass == 0 && GetPrototype()->fAltitude > 1.5f)
            fpCollisionDistance = 0.01f;
    }
    else
    {
        // NOTE actually on client ai class and attack distance are not of much interest
        if (rpgUserUnitStats.cLocomotion == 2)
            nAIClass = 0;
        else
            nAIClass = 3;
        fpAttackDistance = 0.75f;
    }
#ifdef __FAST_PASSABILITIES__
    nAIClass = 3;
#endif
}
/////////////////////////////////////////////////////////////////////////////////////
// wear armor to appropriate slot
// NOTE: this function may cause returned item with nRefCount == 0
CItemArmor *CUnit::Wear(CItemArmor *lpArmor)
{
    if (!lpArmor || lpArmor->IsBroken())
        return 0;
    ASSERT(lpArmor->WhoAmI() == HPC_ITEM_ARMOR);
    CItemArmor *lpOldArmor = lpArmors[lpArmor->GetSlotIndex()];
    //
    lpArmors[lpArmor->GetSlotIndex()] = lpArmor;
    lpArmor->AddRef();
    //	lpUnitSpecific->Wear( lpArmor );
    lpUnitSpecific->Wear(lpArmor->GetSubtypeID());
    //
    if (lpOldArmor)
        lpOldArmor->Release();
    SetUpdateMask(CMapObject::VLD_MASK_INVENTORY);

    return lpOldArmor;
}
/////////////////////////////////////////////////////////////////////////////////////
// calculate weight of the all items, which this unit carry with
float CUnit::GetItemsWeight() const
{
    float fItemsWeight = 0;
    int i;
    // weapons
    for (i = 0; i < QI_MAX_WEAPONS; ++i)
    {
        if (lpQWeapons[i])
            fItemsWeight += lpQWeapons[i]->CItem::GetWeight();
    }
    // quick items
    for (i = 0; i < QI_MAX_ITEMS; ++i)
    {
        if (lpQItems[i])
            fItemsWeight += lpQItems[i]->GetWeight();
    }
    // spells
    for (i = 0; i < QI_MAX_SPELLS; ++i)
    {
        if (lpQSpells[i])
            fItemsWeight += lpQSpells[i]->GetWeight();
    }
    // armors
    for (i = 0; i < RPG_NUM_ARMOR_TYPES; ++i)
    {
        if (lpArmors[i])
            fItemsWeight += lpArmors[i]->GetWeight();
    }
    //
    return fItemsWeight;
}
/////////////////////////////////////////////////////////////////////////////////////
CItemArmor *CUnit::GetQArmor(DWORD dwSlotIndex)
{
    return lpArmors[dwSlotIndex];
}
/////////////////////////////////////////////////////////////////////////////////////
// remove armor by type
// NOTE: this function may cause returned item with nRefCount == 0
CItemArmor *CUnit::Remove(ERPGArmorTypes type)
{
    CItemArmor *lpArmor = lpArmors[type];
    lpArmors[type] = 0;
    if (lpArmor)
    {
        lpUnitSpecific->Remove(lpArmor->GetArmorType());
        //		lpUnitSpecific->Remove( type );
        lpArmor->Release();
    }
    SetUpdateMask(CMapObject::VLD_MASK_INVENTORY);
    SetUpdateMask(CMapObject::VLD_MASK_UNIT_STATS);

    return lpArmor;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnit::SetVoicePrototype(const SUDBUnitPrototype *pPrototype)
{
    SetVoicePrototype(theUnitDatabase.prototypes.GetEntryIndex(pPrototype));
}
void CUnit::SetVoicePrototype(const char *pszPrototype)
{
    SetVoicePrototype(theUnitDatabase.prototypes.GetEntryIndex(pszPrototype));
}
void CUnit::SetVoicePrototype(DWORD dwIndex)
{
    dwVoicePrototype = dwIndex;
}
/////////////////////////////////////////////////////////////////////////////////////
// get unit's prototype
const SUDBUnitPrototype *CUnit::GetPrototype() const
{
    return theUnitDatabase.prototypes[GetSubtypeID()];
}
const SUDBRaceModel *CUnit::GetRaceModel() const
{
    const SUDBUnitPrototype *pProt = GetPrototype();
    if (!pProt)
        return 0;
    return theUnitDatabase.races[pProt->dwRaceIndex];
}
const SUDBUnitPrototype *CUnit::GetVoicePrototype() const
{
    return theUnitDatabase.prototypes[dwVoicePrototype];
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnit::SetRPGStats(SRPGUnitStats *lpRPGNewUnitStats)
{
    if (lpUnitSpecific->WhoAmI() == HPC_UNITSERVERSPECIFIC)
        ((CUnitServer *)lpUnitSpecific)->SetRPGStats(lpRPGNewUnitStats);
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnit::RecalcAIClass()
{
    lpUnitSpecific->RecalcAIClass();
}
/////////////////////////////////////////////////////////////////////////////////////
bool CUnit::IsBackstab(CMapObject *pTarget) const
{
    if (!GetOwner() || !pTarget)
        return false;
    CPlot3D ptToTarget = pTarget->GetCP() - GetCP();
    float fpLeng = fabs(ptToTarget);
    if (fpLeng > 0)
        ptToTarget /= fpLeng;
    CModPackage *mod = ModRuntime::Current();
    if (!mod)
        return !IsCurrentWeaponABow() && pTarget->GetYAxis() * ptToTarget < -0.5f;
    float weaponRange = 0.5f;
    bool excluded = false;
    if (dwCurrentWeapon < 4)
    {
        const CItemWeapon *weapon = GetCurrentQWeapon();
        if (weapon)
        {
            const ERPGWeaponTypes type = weapon->GetWeaponType();
            excluded = type == RPGWST_HANDS3 || type == RPGWST_HANDS7 ||
                (!mod->CombatSetting("JMVRangedBackstab") &&
                 (type == RPGWST_BOW || type == RPGWST_CROSSBOW));
            weaponRange += weapon->GetRange();
        }
    }
    return !excluded && pTarget->GetYAxis() * ptToTarget < weaponRange;
}
/////////////////////////////////////////////////////////////////////////////////////
// return random to-hit value
float CUnit::GetToHit(CMapObject *pMO, bool bBackstab, bool bCalcDistance) const
{
    if (pMO->WhoAmI() != HPC_UNIT)
        return 1000;
    CUnit *pUnit = static_cast<CUnit *>(pMO);

    SAttackParams apAttacker, apDefender;
    GetAttackParams(apAttacker);
    pUnit->GetAttackParams(apDefender);
    float fWeightBonus = apAttacker.fWeaponWeight / (RPG_CONST_Y * apDefender.fWeaponWeight);
    float fToHit = static_cast<float>(apAttacker.nAttack);

    const auto* mod = ModRuntime::Current();
    const bool jmvAttacks = mod && mod->CombatSetting("JMVAttacks");
    if (bCalcDistance && (jmvAttacks || !mod || !mod->CombatSetting("DistanceModifier")))
    {
        float fDistanceModifier = fabs(GetCP() - pUnit->GetCP()) * 2.0f / (RPG_CONST_Z);
        fToHit -= fDistanceModifier;
    }

    if (!jmvAttacks && !bBackstab && (!mod || !mod->CombatSetting("WeightBonus")))
        fToHit += fWeightBonus;

    return Max(0.0f, fToHit);
}
/////////////////////////////////////////////////////////////////////////////////////
float CUnit::GetToHitProbability(CMapObject *pMO, ERPGHitLocations hl) const
{
    if (pMO->WhoAmI() != HPC_UNIT)
        return 100;

    CUnit *pTarget = static_cast<CUnit *>(pMO);

    bool bBackstab = IsBackstab(pTarget);
    float fToHit = GetToHit(pTarget, bBackstab, IsCurrentWeaponABow());
    // backstab or weapon == 'bow class' => tohit > 0
    // else tohit > defender.dwDefense
    float fDefense = 0;
    if (!(bBackstab || IsCurrentWeaponABow()))
    {
        SAttackParams apDefender;
        pTarget->GetAttackParams(apDefender);
        fDefense = apDefender.nDefense;
    }
    //
    if (hl == RPGHL_HEAD)
        fToHit -= 25.0f;
    else if ((hl < RPGHL_ALL) && (hl != RPGHL_BODY))
        fToHit -= 10.0f;
    //
    if (fToHit + RPG_CONST_X < fDefense)
        return 0;
    else if (fToHit - RPG_CONST_X > fDefense)
        return 100;
    else
    {
        float fDiff = (fToHit + RPG_CONST_X) - fDefense;
        return 100.0f * fDiff / (2.0f * RPG_CONST_X);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
float CUnit::GetDefense(CUnit *pUnit, bool bBackstab) const
{
    if (bBackstab)
        return 0;
    SAttackParams apDefender;
    pUnit->GetAttackParams(apDefender);
    return static_cast<float>(apDefender.nDefense);
}
/////////////////////////////////////////////////////////////////////////////////////
// add weapon to unit's quick weapons
int CUnit::AddWeapon(CItemWeapon *lpWeapon)
{
    if (!lpWeapon)
        return NULL;
    ASSERT(lpWeapon->WhoAmI() == HPC_ITEM_WEAPON);
    for (int i = 0; i < QI_MAX_WEAPONS; i++)
    {
        if (lpQWeapons[i] == lpWeapon) // prevent adding one weapon twice
            return TRUE;
        if (lpQWeapons[i] == NULL) // can add weapon only in empty slot
        {
            lpQWeapons[i] = lpWeapon;
            lpQWeapons[i]->AddRef();
            SetUpdateMask(CMapObject::VLD_MASK_INVENTORY);
            return i;
        }
    }

    return -1;
}
/////////////////////////////////////////////////////////////////////////////////////
// remove weapon from unit's quick weapons
void CUnit::RemoveWeapon(DWORD dwQWeaponIndex)
{
    if (dwQWeaponIndex >= QI_MAX_WEAPONS)
        return;

    CItemWeapon *lpOldWeapon = lpQWeapons[dwQWeaponIndex];
    lpQWeapons[dwQWeaponIndex] = NULL;
    if (lpOldWeapon)
        lpOldWeapon->Release();
    SetUpdateMask(CMapObject::VLD_MASK_INVENTORY);
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnit::ConvertWeaponsOnClient()
{
    SItemIDs items;
    ConvertItemsIDs(items);
    lpUnitSpecific->SetArmors(items.dwArmorsID, RPG_NUM_ARMOR_TYPES);
    lpUnitSpecific->SetWeapons(items.dwWeaponsID, QI_MAX_WEAPONS);
}
/////////////////////////////////////////////////////////////////////////////////////
ERPGWeaponTypes CUnit::GetCurrentWeaponType() const
{
    if ((dwCurrentWeapon >= QI_MAX_WEAPONS) || (lpQWeapons[dwCurrentWeapon] == 0))
        return ERPGWeaponTypes(rpgUserUnitStats.cNAWWeaponType);
    else
        return lpQWeapons[dwCurrentWeapon]->GetWeaponType();
}
/////////////////////////////////////////////////////////////////////////////////////
int CUnit::AddQItem(CItemQuick *lpItem)
{
    if (!lpItem)
        return NULL;
    ASSERT(lpItem->WhoAmI() == HPC_ITEM_QUICK);
    for (int i = 0; i < QI_MAX_ITEMS; i++)
    {
        if (lpQItems[i] == lpItem) // prevent adding one weapon twice
            return TRUE;
        if (lpQItems[i] == NULL) // can add weapon only in empty slot
        {
            lpQItems[i] = lpItem;
            lpQItems[i]->AddRef();
            SetUpdateMask(CMapObject::VLD_MASK_INVENTORY);
            return i;
        }
    }

    return -1;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnit::RemoveQItem(DWORD dwQItemIndex)
{
    if (dwQItemIndex >= QI_MAX_ITEMS)
        return;

    CItemQuick *lpOldItem = lpQItems[dwQItemIndex];
    // shift items
    /*for ( int i = dwQItemIndex; i < QI_MAX_ITEMS - 1; i++ )
            lpQItems[i] = lpQItems[i+1];
    lpQItems[ QI_MAX_ITEMS - 1 ] = NULL;*/
    lpQItems[dwQItemIndex] = NULL;
    if (lpOldItem)
        lpOldItem->Release();
    SetUpdateMask(CMapObject::VLD_MASK_INVENTORY);
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnit::AddQuestItem(CItemQuest *pItem)
{
    pItem->AddRef();
    questItems.Add(pItem);
    SetUpdateMask(CMapObject::VLD_MASK_INVENTORY);
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnit::RemoveQuestItem(int i)
{
    CItemQuest *pRes = questItems[i];
    pRes->Release();
    questItems.RemoveAt(i);
}
/////////////////////////////////////////////////////////////////////////////////////
CItem *CUnit::RemoveRandomItem()
{
    int nIdx;
    CArray<DWORD, DWORD> items;
    // quest items
    if (questItems.GetSize())
    {
        CItem *pRes = questItems[0];
        questItems.RemoveAt(0);
        SetUpdateMask(CMapObject::VLD_MASK_INVENTORY);
        return pRes;
    }
    // quick items
    for (DWORD i = 0; i < QI_MAX_ITEMS; i++)
    {
        if (lpQItems[i])
            items.Add(i);
    }
    //
    if (items.GetSize() == 0)
        return NULL;
    nIdx = items[random.Get(0, items.GetSize() - 1)];
    CItemQuick *pRes = lpQItems[nIdx];
    lpQItems[nIdx] = NULL;
    SetUpdateMask(CMapObject::VLD_MASK_INVENTORY);
    return pRes;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CUnit::HasQuickSlot()
{
    for (int i = 0; i < QI_MAX_ITEMS; i++)
    {
        if (lpQItems[i] == NULL)
            return TRUE;
    }
    return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnit::RemoveAllItems()
{
    DWORD i;
    for (i = 0; i < QI_MAX_WEAPONS; i++)
        RemoveWeapon(i);
    for (i = 0; i < RPG_NUM_ARMOR_TYPES; i++)
        Remove(ERPGArmorTypes(i));
    lpUnitSpecific->RemoveAllArmors();
    for (i = 0; i < QI_MAX_ITEMS; i++)
        RemoveQItem(i);
    for (i = 0; i < QI_MAX_SPELLS; i++)
        RemoveQSpell(i);
    for (i = 0; i < questItems.GetSize(); i++)
        questItems[i]->Release();
    questItems.RemoveAll();
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnit::GetAllItemsList(CGameBaseList &items, CGameBaseList &spells)
{
    items.RemoveAll();
    spells.RemoveAll();
    DWORD i;
    for (i = 0; i < QI_MAX_WEAPONS; i++)
        AddItemToLists(items, spells, lpQWeapons[i]);
    for (i = 0; i < RPG_NUM_ARMOR_TYPES; i++)
        AddItemToLists(items, spells, lpArmors[i]);
    for (i = 0; i < QI_MAX_ITEMS; i++)
        AddItemToLists(items, spells, lpQItems[i]);
    for (i = 0; i < QI_MAX_SPELLS; i++)
        AddItemToLists(items, spells, lpQSpells[i]);
    for (i = 0; i < questItems.GetSize(); i++)
        AddItemToLists(items, spells, questItems[i]);
}
/////////////////////////////////////////////////////////////////////////////////////
DWORD CUnit::GetSpellSlotCount() const
{
    return IsIDofPersonage(GetID()) ? QI_MAX_SPELLS : QI_NPC_SPELLS;
}

int CUnit::AddQSpell(CItemSpell *lpSpell)
{
    if (!lpSpell)
        return -1;
    // Search all occupied slots before selecting a hole: avoid duplicate ownership.
    for (DWORD i = 0; i < GetSpellSlotCount(); ++i)
        if (lpQSpells[i] == lpSpell)
            return int(i);
    for (DWORD i = 0; i < GetSpellSlotCount(); i++)
    {
        if (lpQSpells[i] == NULL) // can add spell only in empty slot
        {
            lpQSpells[i] = lpSpell;
            lpQSpells[i]->AddRef();
            SetUpdateMask(CMapObject::VLD_MASK_INVENTORY);
            return i;
        }
    }

    return -1;
}
/////////////////////////////////////////////////////////////////////////////////////
CItemSpell *CUnit::RemoveQSpell(DWORD dwQSpellIndex)
{
    if (dwQSpellIndex >= QI_MAX_SPELLS)
        return NULL;

    CItemSpell *lpOldSpell = lpQSpells[dwQSpellIndex];
    lpQSpells[dwQSpellIndex] = NULL;
    if (lpOldSpell)
        lpOldSpell->Release();
    SetUpdateMask(CMapObject::VLD_MASK_INVENTORY);

    return lpOldSpell;
}
/////////////////////////////////////////////////////////////////////////////////////
CItemWeapon *CUnit::GetQWeapon(DWORD dwQWeaponIndex) const
{
    if (lpUnitSpecific->WhoAmI() == HPC_UNITCLIENTSPECIFIC)
    {
        CPlayer *pPlayer = GetOwner();
        ASSERT(pPlayer);
        if (!pPlayer)
            return 0;
        CPersonage *pPers = pPlayer->party.GetPersonage(GetID());
        if (!pPers)
            return 0;
        if (dwQWeaponIndex >= pPers->weapons.GetSize())
            return 0;
        return pPers->weapons[dwQWeaponIndex];
    }
    if ((dwQWeaponIndex >= QI_MAX_WEAPONS))
        return NULL;
    else
        return lpQWeapons[dwQWeaponIndex];
}
/////////////////////////////////////////////////////////////////////////////////////
CItemQuick *CUnit::GetQItem(DWORD dwQItemIndex)
{
    if (lpUnitSpecific->WhoAmI() == HPC_UNITCLIENTSPECIFIC)
    {
        CPlayer *pPlayer = GetOwner();
        ASSERT(pPlayer);
        if (!pPlayer)
            return 0;
        CPersonage *pPers = pPlayer->party.GetPersonage(GetID());
        if (!pPers)
            return 0;
        if (dwQItemIndex >= pPers->items.GetSize())
            return 0;
        CItemQuick *pRes = pPers->items[dwQItemIndex];
        if (pRes && pRes->GetCount() > 0)
            return pRes;
        return 0;
    }
    if ((dwQItemIndex >= QI_MAX_ITEMS))
        return NULL;
    else
        return lpQItems[dwQItemIndex];
}
/////////////////////////////////////////////////////////////////////////////////////
CItemSpell *CUnit::GetQSpellItem(DWORD dwQSpellIndex)
{
    if (dwQSpellIndex >= GetSpellSlotCount())
        return nullptr;
    CItemSpell *pSpell;
    if (lpUnitSpecific->WhoAmI() == HPC_UNITCLIENTSPECIFIC)
    {
        CPlayer *pPlayer = GetOwner();
        ASSERT(pPlayer);
        if (!pPlayer)
            return 0;
        CPersonage *pPers = pPlayer->party.GetPersonage(GetID());
        if (!pPers)
            return 0;
        if (dwQSpellIndex >= pPers->spells.GetSize())
            return 0;
        pSpell = pPers->spells[dwQSpellIndex];
        return pSpell;
    }
    if ((dwQSpellIndex >= QI_MAX_SPELLS))
        return NULL;
    else
        return lpQSpells[dwQSpellIndex];
}
/////////////////////////////////////////////////////////////////////////////////////
CSpell *CUnit::GetQSpell(DWORD dwQSpellIndex)
{
    if (dwQSpellIndex >= GetSpellSlotCount())
        return nullptr;
    CSpell *pSpell;
    if (lpUnitSpecific->WhoAmI() == HPC_UNITCLIENTSPECIFIC)
    {
        CPlayer *pPlayer = GetOwner();
        ASSERT(pPlayer);
        if (!pPlayer)
            return 0;
        CPersonage *pPers = pPlayer->party.GetPersonage(GetID());
        if (!pPers)
            return 0;
        if (dwQSpellIndex >= pPers->spells.GetSize())
            return 0;
        if (!pPers->spells[dwQSpellIndex])
            return nullptr;
        pSpell = pPers->spells[dwQSpellIndex]->GetSpell();
        return pSpell;
    }
    if ((dwQSpellIndex >= QI_MAX_SPELLS))
        return NULL;
    else
    {
        if (lpQSpells[dwQSpellIndex])
            return lpQSpells[dwQSpellIndex]->GetSpell();
    }
    return NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnit::Load(CSave &file)
{
    int i, k, _nPlayer;
    CMapObject::Load(file);
    lpUnitSpecific->Load(file);
    ResetFlag(UNIT_BLOCKED);
    szName = file.ReadString();
    file.Read(dwEnemyMask); // маска, показывающая враждебных плееров для этого юнита
    file.Read(_nPlayer);
    CPlayer *pSavedOwner = NULL;
    if (GetWorld()->WhoAmI() == HPC_WORLD_SERVER)
        pSavedOwner = ((CWorldServer *)GetWorld())->GetPlayer(_nPlayer);
    file.Read(nPlayerNum);

    file.Read(nStartAnimation);
    file.Read(nCycleAnimation);
    fullStartAnimation = NormalizeAnimationId(nStartAnimation);
    fullCycleAnimation = NormalizeAnimationId(nCycleAnimation);
    bExtendedAnimationId = false;
    file.Read(nAnimationStartSegment);
    file.Read(&animationSpeed, sizeof(animationSpeed));
    file.Read(&locomotionSpeed, sizeof(locomotionSpeed));
    file.Read(&currentPose, sizeof(currentPose));
    file.Read(&wishPose, sizeof(wishPose));
    file.Read(dwCurrentWeapon);
    file.Read(dwWishWeapon);
    file.Read(bDead);
    file.Read(bMoving);

    walker.Load(file);
    file.Read(fpBaseZ);
    // combat targeting, information only
    file.Read(nAIClass);
    file.Read(fpAttackDistance);
    rpgUserUnitStats.Clear();
    file.Read(dwCurrentWeapon);
    for (i = 0; i < QI_MAX_WEAPONS; i++)
        lpQWeapons[i] = LoadGBPtr<CItemWeapon>(file, GetWorld()->GetItemManager());
    for (i = 0; i < QI_MAX_ITEMS; i++)
        lpQItems[i] = LoadGBPtr<CItemQuick>(file, GetWorld()->GetItemManager());
    const int nStoredSpellSlots = file.GetVersion() >= 117 ? QI_MAX_SPELLS : QI_LEGACY_SPELLS;
    for (i = 0; i < nStoredSpellSlots; i++)
        lpQSpells[i] = LoadGBPtr<CItemSpell>(file, GetWorld()->GetItemManager());
    for (; i < QI_MAX_SPELLS; i++)
        lpQSpells[i] = NULL;
    for (i = 0; i < RPG_NUM_ARMOR_TYPES; i++)
        lpArmors[i] = LoadGBPtr<CItemArmor>(file, GetWorld()->GetItemManager());

    file.Read(&sensesDetection, sizeof(sensesDetection));
    ExportStats();
    SetOwner(pSavedOwner);
    file.Read(dwPossibleEnemies);
    file.Read(i);
    questItems.SetSize(i);
    for (k = 0; k < i; k++)
        questItems[k] = LoadGBPtr<CItemQuest>(file, GetWorld()->GetItemManager());
    file.Read(fpCollisionDistance);
    if (file.GetVersion() < 73)
        return;
    file.Read(&aggression, sizeof(aggression));
    if (file.GetVersion() < 77)
        return;
    CString szProto;
    // unit prototype
    file.Read(szProto);
    SetSubtypeID(theUnitDatabase.prototypes.GetEntryIndex(szProto));
    if (file.GetVersion() < 106)
    {
        SetVoicePrototype(szProto);
        return;
    }
    // voice prototype
    file.Read(szProto);
    SetVoicePrototype(szProto);
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnit::Store(CSave &file)
{
    int i, k, _nPlayer;
    CMapObject::Store(file);
    lpUnitSpecific->Store(file);
    file.WriteString(szName);
    file.Write(dwEnemyMask); // маска, показывающая враждебных плееров для этого юнита
    _nPlayer = -1;
    if (lpOwner)
        _nPlayer = lpOwner->GetPlayerNumber();
    file.Write(_nPlayer);
    file.Write(nPlayerNum);

    file.Write(nStartAnimation);
    file.Write(nCycleAnimation);
    file.Write(nAnimationStartSegment);
    file.Write(&animationSpeed, sizeof(animationSpeed));
    file.Write(&locomotionSpeed, sizeof(locomotionSpeed));
    file.Write(&currentPose, sizeof(currentPose));
    file.Write(&wishPose, sizeof(wishPose));
    file.Write(dwCurrentWeapon);
    file.Write(dwWishWeapon);
    file.Write(bDead);
    file.Write(bMoving);

    walker.Store(file);
    file.Write(fpBaseZ);
    // combat targeting, information only
    file.Write(nAIClass);
    file.Write(fpAttackDistance);
    file.Write(dwCurrentWeapon);
    for (i = 0; i < QI_MAX_WEAPONS; i++)
        StoreGBPtr(file, lpQWeapons[i]);
    for (i = 0; i < QI_MAX_ITEMS; i++)
        StoreGBPtr(file, lpQItems[i]);
    for (i = 0; i < QI_MAX_SPELLS; i++)
        StoreGBPtr(file, lpQSpells[i]);
    for (i = 0; i < RPG_NUM_ARMOR_TYPES; i++)
        StoreGBPtr(file, lpArmors[i]);

    file.Write(&sensesDetection, sizeof(sensesDetection));
    file.Write(dwPossibleEnemies);
    file.Write(questItems.GetSize());
    for (k = 0; k < questItems.GetSize(); k++)
        StoreGBPtr(file, questItems[k]);
    file.Write(fpCollisionDistance);
    file.Write(&aggression, sizeof(aggression));
    // unit prototype
    CString szProto = GetPrototype()->szName;
    file.Write(szProto);
    // voice prototype
    const SUDBUnitPrototype *pVoicePrototype = GetVoicePrototype();
    if (pVoicePrototype == 0)
        szProto = "";
    else
        szProto = pVoicePrototype->szName;
    file.Write(szProto);
}
/////////////////////////////////////////////////////////////////////////////////////
const DWORD UNIT_NUM_SFX_RACES = 14;
const char *pszRaceSFXEmptyPath = "";
const char *pszRaceSFXPathes[] = { "heroes\\human\\male",
                                   "heroes\\human\\female",
                                   "heroes\\orc\\male",
                                   "heroes\\orc\\female",
                                   "monsters\\goblin",
                                   "monsters\\troll",
                                   "monsters\\ogre",
                                   "animals\\tiger",
                                   "monsters\\driad",
                                   "monsters\\banshee",
                                   "animals\\wolf",
                                   "animals\\boar",
                                   "animals\\cow"
                                   "animals\\pig",
                                   "" };
const int nRaceSFXNumHits[] = { 6, 9, 8, 8, 4, 4, 4, 4, 3, 8, 3, 3, 1, 4 };
const int nRaceSFXNumAttacks[] = { 2, 6, 7, 7, 2, 1, 6, 4, 4, 8, 2, 2, 0, 0 };
const int nRaceSFXNumDeathes[] = { 8, 9, 8, 8, 3, 1, 4, 4, 1, 8, 2, 1, 1, 4 };
//
LPCSTR CUnit::GetSFXPath() const
{
    const SUDBUnitPrototype *pVoicePrototype = GetVoicePrototype();
    if (pVoicePrototype == 0)
        return pszRaceSFXEmptyPath;
    DWORD dwRaceModel = pVoicePrototype->dwRaceIndex;
    if (dwRaceModel > UNIT_NUM_SFX_RACES)
        return pszRaceSFXEmptyPath;
    else
        return pszRaceSFXPathes[dwRaceModel];
}
int CUnit::GetNumSFXHits() const
{
    const SUDBUnitPrototype *pVoicePrototype = GetVoicePrototype();
    if (pVoicePrototype == 0)
        return 0;
    DWORD dwRaceModel = pVoicePrototype->dwRaceIndex;
    if (dwRaceModel > UNIT_NUM_SFX_RACES)
        return 0;
    else
        return nRaceSFXNumHits[dwRaceModel];
}
int CUnit::GetNumSFXAttacks() const
{
    const SUDBUnitPrototype *pVoicePrototype = GetVoicePrototype();
    if (pVoicePrototype == 0)
        return 0;
    DWORD dwRaceModel = pVoicePrototype->dwRaceIndex;
    if (dwRaceModel > UNIT_NUM_SFX_RACES)
        return 0;
    else
        return nRaceSFXNumAttacks[dwRaceModel];
}
int CUnit::GetNumSFXDeathes() const
{
    const SUDBUnitPrototype *pVoicePrototype = GetVoicePrototype();
    if (pVoicePrototype == 0)
        return 0;
    DWORD dwRaceModel = pVoicePrototype->dwRaceIndex;
    if (dwRaceModel > UNIT_NUM_SFX_RACES)
        return 0;
    else
        return nRaceSFXNumDeathes[dwRaceModel];
}
/////////////////////////////////////////////////////////////////////////////////////
float CUnit::GetAnimationCoeff(const SADEntry *pADEntry)
{
    // attack, cast, hit, death, cross, special
    if ((pADEntry->dwFlags & ADB_ACTIONS) == ADB_ACTION_ATTACK)
        return fAnimationCoeffs[0];
    else if ((pADEntry->dwFlags & ADB_ACTIONS) == ADB_ACTION_CAST)
        return fAnimationCoeffs[1];
    else if ((pADEntry->dwFlags & ADB_ACTIONS) == ADB_ACTION_SUFFER)
        return fAnimationCoeffs[2];
    else if ((pADEntry->dwFlags & ADB_ACTIONS) == ADB_ACTION_DEATH)
        return fAnimationCoeffs[3];
    else if ((pADEntry->dwFlags & ADB_ACTIONS) == ADB_ACTION_CROSS)
    {
        if (GetTypeID() == MOTID_HUMAN)
            return theAIGeneralSettings.fpHumanCrossAnimationSpeed;
        return fAnimationCoeffs[4];
    }
    else if ((pADEntry->dwFlags & ADB_ACTIONS) == ADB_ACTION_SPECIAL)
        return fAnimationCoeffs[5];
    else
        return 1;
    // return 10;
}
/////////////////////////////////////////////////////////////////////////////////////
CPlot3D CUnit::GetWeaponCastPoint(const CItemWeapon*weapon,bool left)
{
    // Use the selected hand on the animated client model; server models may have no bones.
    if(GetWorld()&&!GetWorld()->IsServer()&&GetTypeID()==MOTID_HUMAN)
        if(CFigure*hand=MatchFigure(left?"lh3":"rh3"))return GetFigureWorldPosition(hand);
    auto*animation=GetAnimationSystem2();
    if(!animation)return GetCP()+bv.ptCenter;
    CString name=left?"lh3":"baserh3";
    if(weapon){auto*shape=weapon->GetShapeEntry();
        if(shape->dwTypeID==RPGWST_HANDS3||shape->dwTypeID==RPGWST_HANDS7)
            name.Format("%s%s%.2d",left?"l.":"",shape->dwTypeID==RPGWST_HANDS3?"basestaff":"basedstaff",int(shape->dwTextureTypeIndex));
    }
    const CAnimationNode*node=animation->FindNode(name);
    if(!node)node=animation->FindNode("rh3");
    return node?GetCP()+rotation.Rotate(node->GetPosition()):GetCP()+bv.ptCenter;
}

CPlot3D CUnit::GetCastPointPosition()
{
    CPlot3D position;
    if (!GetWorld()->IsServer())
    {
        if (GetTypeID() == MOTID_HUMAN)
        {
            CFigure *pHand;
            pHand = MatchFigure("rh3");
            if (pHand)
            {
                CPlot3D ptHand = GetFigureWorldPosition(pHand);
                return ptHand;
            }
        }
        else if (static_cast<CUnitClientSpecific *>(lpUnitSpecific)
                     ->GetCastPointPosition(position) == false)
            return GetCP() + bv.ptCenter;
    }
    position = rotation.Rotate(position);
    return (GetCP() + position);
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnit::SetupCampUnitWithUnitStats(SRPGUnitStats *pUnitStats)
{
    pUnitStats->Export(this, &rpgUserUnitStats);
    float fMaxHP = pUnitStats->GetModifiedHealth(this, pUnitStats->RecalcHP());
    rpgUserCommonStats.nMaxHP = fMaxHP;
    rpgUserCommonStats.nHP = pUnitStats->CollectHP(fMaxHP);
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnit::ChangeComplectionWithSpell()
{
    CMapObject::ChangeComplectionWithSpell();
    lpUnitSpecific->ComplectionChanged();
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnit::RestoreComplection()
{
    SetComplection(baseComplection);
    /*
    SComplection compl = baseComplection;
    float fStrVal = 0, fDexVal = 0;
    if ( GetUnitSpecific()->WhoAmI() == HPC_UNITSERVERSPECIFIC )
    {
            CUnitServer *pUS = static_cast<CUnitServer*>( GetUnitSpecific() );
            fStrVal = pUS->GetRPGStats()->GetStrengthModifier();
            fDexVal = pUS->GetRPGStats()->GetDexterityModifier();
    }
    else
    {
            fStrVal = rpgUserUnitStats.GetStrengthModifier();
            fDexVal = rpgUserUnitStats.GetDexterityModifier();
    }
    compl.strength += fStrVal*0.2f;
    compl.grace = Max( 0.0f, compl.grace - fDexVal*0.2f );
    SetComplection( compl );
    */
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnit::ChangeSpeedWithSpell()
{
    lpUnitSpecific->ChangeSpeedWithSpell();
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnit::RemoveCrippledItems()
{
    int i;
    CList<CItem *, CItem *> items;
    //
    CPlayer *pPlayer = GetOwner();
    if (!pPlayer)
        return;
    ASSERT(GetWorld()->IsServer());
    CPlayerServer *pPS = (CPlayerServer *)pPlayer->GetPlayerSpecific();
    // armors
    for (i = 0; i < RPG_NUM_ARMOR_TYPES; ++i)
    {
        if (lpArmors[i] != 0)
        {
            if (lpArmors[i]->IsBroken())
            {
                CItemArmor *pBrokenArmor = lpArmors[i];
                pBrokenArmor->AddRef();
                items.AddTail(pBrokenArmor);
                Remove(ERPGArmorTypes(i));
            }
        }
    }
    //
    bool bChangeWeap = false;
    for (i = QI_MAX_WEAPONS - 1; i >= 0; --i)
    {
        if (lpQWeapons[i] != 0)
        {
            if (lpQWeapons[i]->IsBroken())
            {
                pPS->RemoveWeaponFromUnit(GetID(), i);
                CItemWeapon *pBrokenWeapon = lpQWeapons[i];
                pBrokenWeapon->AddRef();
                items.AddTail(pBrokenWeapon);
                RemoveWeapon(i);
                for (int k = i; k < QI_MAX_WEAPONS - 1; ++k)
                    lpQWeapons[k] = lpQWeapons[k + 1];
                lpQWeapons[QI_MAX_WEAPONS - 1] = 0;
                SetUpdateMask(CMapObject::VLD_MASK_POSE);
                bChangeWeap = true;
            }
        }
    }
    if (bChangeWeap)
        ChangeWeapon(-1);
    if (!items.IsEmpty())
    {
        if (pPlayer)
            pPlayer->AddItemsToHoover(items, true);
        // release this items because ref count was incremented in hoover
        for (POSITION pos = items.GetHeadPosition(); pos;)
            items.GetNext(pos)->Release();
        SetUpdateMask(CMapObject::VLD_MASK_INVENTORY);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
int CUnit::GetLevel() const
{
    if (IsIDofPersonage(GetID()))
        return lpUnitSpecific->GetHeroLevel();
    else
        return GetPrototype()->nLevel;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CUnit::CanPlayAnimation(const SADEntry *pEntry) const
{
    if (GetTypeID() == MOTID_HUMAN)
    {
        DWORD dwShapeFlags = 0;
        if (lpUnitSpecific->HaveLowShapes())
            dwShapeFlags |= ADB_FLAGS2_ALLOW_LOW_SHAPE;
        if (lpUnitSpecific->HaveHighShapes())
            dwShapeFlags |= ADB_FLAGS2_ALLOW_HIGH_SHAPE;
        //
        return (pEntry->dwFlags2 & dwShapeFlags) == dwShapeFlags;
    }
    else
        return true;
}
/////////////////////////////////////////////////////////////////////////////////////
float CUnit::GetHeightComplectionScale() const
{
    float fHCBase = GetHeightCorrection(baseComplection.tall);
    float fHCCurr = GetHeightCorrection(complection.tall);
    return fHCCurr / fHCBase;
}
/////////////////////////////////////////////////////////////////////////////////////
