#include "StdAfx.h"
#include "SaveSchema.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "MapObjectServer.h"
#include "World.h"
#include "FigureManager.h"
#include "SpellSystem.h"
#include "MapObjectManager.h"
#include "ModRuntime.h"
#include "ModPackage.h"
#include "ModInvincible.h"

namespace {
bool SpellEffectPowerEnabled()
{
    auto *mod = ModRuntime::Current();
    return mod && mod->CombatSetting("SendSpellEffectValueToPlayers") != 0;
}
}
/////////////////////////////////////////////////////////////////////////////////////
// MapObjectPtr
/////////////////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////
CMapObject::CMapObject()
{
    FreeReferences();
    lpMapObjectSpecific = NULL;
    rpgUserCommonStats.Clear();
    fpAnimationCoeff = 1;

    nTileMapX = -1;
    nTileMapY = -1;
}
/////////////////////////////////////////////////////////////////////////////////////
CMapObject::~CMapObject()
{
    FreeReferences();
}
/////////////////////////////////////////////////////////////////////////////////////
void CMapObject::FreeReferences()
{
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CMapObject::Create()
// create
BOOL CMapObject::Create()
{
    CObject3D::Create();
    SetMapObjectSpecific(NULL);

    rpgUserCommonStats.Clear();
    fpAnimationCoeff = 1;
    rpgUserCommonStats.Clear();
    update.SetHistoriesNumber(VLD_MASK_MAPOBJMAX + 1);
    update.Clear();
    nUpdatedSegment = -1;
    spellEffects.RemoveAll();
    nActiveObjectCounter = 5;
    szAttachedQuest = "";
    memset(&baseComplection, 0, sizeof(baseComplection));

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// void CMapObject::Destroy()
// destroy
void CMapObject::Destroy()
{
    spellEffects.RemoveAll();
    SetMapObjectSpecific(NULL);
    CObject3D::Destroy();
}
/////////////////////////////////////////////////////////////////////////////////////
// void CMapObject::SetSpecific( LPGAMEBASE lpNewSpecific )
// set specific to location, depend on type id
void CMapObject::SetSpecific(LPGAMEBASE lpNewSpecific)
{
    if (lpNewSpecific && (lpNewSpecific->GetTypeID() == SPTID_MAPOBJECT_SPECIFIC))
        SetMapObjectSpecific(lpNewSpecific);
    else
        CObject3D::SetSpecific(lpNewSpecific);
}
/////////////////////////////////////////////////////////////////////////////////////
// void CMapObject::SetMapObjectSpecific( LPGAMEBASE pNewSpecific )
// set new unit specific data
void CMapObject::SetMapObjectSpecific(LPGAMEBASE lpNewSpecific)
{
    if (lpMapObjectSpecific)
    {
        lpMapObjectSpecific->RegisterMapObject(NULL);
        lpMapObjectSpecific->Release();
    }

    lpMapObjectSpecific = (CMapObjectSpecific *)lpNewSpecific;

    if (lpMapObjectSpecific)
    {
        lpMapObjectSpecific->RegisterMapObject(this);
        lpMapObjectSpecific->AddRef();
    }
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CMapObject::LeaveMap()
{
    lpMapObjectSpecific->LeaveMap();
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CMapObject::ReturnOnMap()
{
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CMapObject::RecalcRPGStats(bool bStrengthChanged)
{
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// export new stats from server specific (ONLY if this is a server object)
BOOL CMapObject::ExportStats()
{
    CMapObjectServer *pMOSpec = (CMapObjectServer *)GetMapObjectSpecific();
    if (pMOSpec->WhoAmI() == HPC_MAPOBJECTSERVERSPECIFIC)
    {
        pMOSpec->rpgCommonStats.Export(&rpgUserCommonStats);
        return TRUE;
    }
    else
        return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CMapObject::LoadDefaultClientStats(const char *pszTemplate)
{
    CMapObjectManager *pM = (CMapObjectManager *)GetManager();
    SRPGCommonStats s1;
    pM->SetupStats(&s1, 0, pszTemplate);
    s1.Export(&rpgUserCommonStats);
}
/////////////////////////////////////////////////////////////////////////////////////
void CMapObject::SetUpdateMask(int nBit)
{
    CWorldBase *pWB;
    if (nBit >= 0)
        update.SetUpdateMask(nBit);
    SetUpdated();
    if (IsReferenced() && (pWB = GetWorldBase()))
        nUpdatedSegment = pWB->GetUpdateCounter();
}
/////////////////////////////////////////////////////////////////////////////////////
void CMapObject::UpdateInit()
{
    update.StartUpdateTo(utStart);
    UpdateTo();
    update.FinishUpdate();
    ResetUpdated();
}
/////////////////////////////////////////////////////////////////////////////////////
void CMapObject::UpdateFrame()
{
    if (update.IsUpdateMask(VLD_MASK_STATS) || update.IsUpdateMask(VLD_MASK_UNIT_STATS))
        ExportStats();
    update.StartUpdateTo(utFrame);
    UpdateTo();
    update.FinishUpdate();
    ResetUpdated();
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CMapObject::UpdateFrom(EUpdateType tip, CPacket &pkt)
{
    BOOL bRv;
    update.StartUpdateFrom(tip, pkt);
    bRv = UpdateFrom(tip);
    update.FinishUpdate();
    return bRv;
}
/////////////////////////////////////////////////////////////////////////////////////
void CMapObject::UpdateTo()
{
    CUpdateHistory *pPkt;
    // common RPG stats
    if (pPkt = update.GetHistory(VLD_MASK_STATS))
    {
        ::UpdateTo(rpgUserCommonStats, *pPkt);
        pPkt->FinishUpdate();
    }
    // complection
    if (pPkt = update.GetHistory(VLD_MASK_COMPLECTION))
    {
        pPkt->Write(&complection, sizeof(complection));
        pPkt->FinishUpdate();
    }
    // placement: position and rotation
    if (pPkt = update.GetHistory(VLD_MASK_POSROT))
    {
        BYTE bIsShadow = CheckFlag(OBJ_NOSHADOW) != 0;
        (*pPkt) << bIsShadow;
        pPkt->Write(&cp, sizeof(float) * 3);
        pPkt->Write(&rotation, sizeof(float) * 4);
        pPkt->FinishUpdate();
    }
    // spell effects
    if (pPkt = update.GetHistory(VLD_MASK_SPELLEFFECTS))
    {
        const bool sendSpellEffectPower = SpellEffectPowerEnabled();
        BYTE bNumber = spellEffects.GetSize();
        (*pPkt) << bNumber;
        for (int k = 0; k < spellEffects.GetSize(); k++)
        {
            CSpellEffect &eff = spellEffects[k];
            pPkt->WriteDirect(&eff.nSpellID, sizeof(eff.nSpellID));
            pPkt->WriteDirect(&eff.nVisStrength, sizeof(eff.nVisStrength));
            pPkt->WriteDirect(&eff.nDuration, 2);
            pPkt->WriteDirect(&eff.bQuiet, 1);
            if (sendSpellEffectPower)
                pPkt->WriteDirect(&eff.fpPower, 4);
        }
        pPkt->FinishUpdate();
    }
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CMapObject::UpdateFrom(EUpdateType tip)
{
    CUpdateHistory *pPkt;
    BOOL bRv = TRUE;
    // RPG common stats
    if (pPkt = update.GetHistory(VLD_MASK_STATS))
    {
        int nOldHP = rpgUserCommonStats.nHP;
        ::UpdateFrom(rpgUserCommonStats, *pPkt);
        bRv &= pPkt->UpdateFromIsOk();
        pPkt->FinishUpdate();
        if ((nOldHP != rpgUserCommonStats.nHP) || (tip == utFrame))
            NotifyHitPointsUpdated();
    }
    // complection
    if (pPkt = update.GetHistory(VLD_MASK_COMPLECTION))
    {
        SComplection complection;
        pPkt->Read(&complection, sizeof(complection));
        SetComplection(complection);

        bRv &= pPkt->UpdateFromIsOk();
        pPkt->FinishUpdate();
    }
    // placement: position and rotation
    if (pPkt = update.GetHistory(VLD_MASK_POSROT))
    {
        BYTE bIsShadow;
        (*pPkt) >> bIsShadow;
        if (bIsShadow)
            SetFlag(OBJ_NOSHADOW);
        else
            ResetFlag(OBJ_NOSHADOW);
        CPlot3D newCP;
        pPkt->Read(&newCP, sizeof(float) * 3);
        pPkt->Read(&rotation, sizeof(float) * 4);
        GetWorldBase()->MoveObject(this, newCP);
        NewVerticalPos(cp.z);

        bRv &= pPkt->UpdateFromIsOk();
        pPkt->FinishUpdate();
    }
    // spell effects
    if (pPkt = update.GetHistory(VLD_MASK_SPELLEFFECTS))
    {
        const bool sendSpellEffectPower = SpellEffectPowerEnabled();
        BYTE bNumber;
        (*pPkt) >> bNumber;
        spellEffects.SetSize(bNumber);
        for (int k = 0; k < spellEffects.GetSize(); k++)
        {
            CSpellEffect &eff = spellEffects[k];
            eff.nDuration = 0;
            pPkt->ReadDirect(&eff.nSpellID, sizeof(eff.nSpellID));
            pPkt->ReadDirect(&eff.nVisStrength, sizeof(eff.nVisStrength));
            pPkt->ReadDirect(&eff.nDuration, 2);
            pPkt->ReadDirect(&eff.bQuiet, 1);
            if (sendSpellEffectPower)
            {
                eff.fpPower = 0;
                pPkt->ReadDirect(&eff.fpPower, 4);
            }
        }
        if (WhoAmI() != HPC_UNIT)
            lpMapObjectSpecific->UpdateSpellEffectVisualization(tip == utStart);

        bRv &= pPkt->UpdateFromIsOk();
        pPkt->FinishUpdate();
    }
    return bRv;
}
/////////////////////////////////////////////////////////////////////////////////////
void CMapObject::NewVerticalPos(float fpZ)
{
    SetCP(CPlot3D(cp.x, cp.y, fpZ + GetWorldBase()->GetZValue(cp.x, cp.y)));
}
/////////////////////////////////////////////////////////////////////////////////////
// void CMapObject::SetRPGStats( SRPGCommonStats* lpRPGNewCommonStats )
// setup new common RPG stats
void CMapObject::SetRPGStats(SRPGCommonStats *lpRPGNewCommonStats)
{
    if (lpMapObjectSpecific->WhoAmI() == HPC_MAPOBJECTSERVERSPECIFIC)
        ((CMapObjectServer *)lpMapObjectSpecific)->SetRPGStats(lpRPGNewCommonStats);
}
/////////////////////////////////////////////////////////////////////////////////////
void CMapObject::ClearRPGAbsorbtions()
{
    if (lpMapObjectSpecific->WhoAmI() == HPC_MAPOBJECTSERVERSPECIFIC)
    {
        for (int i = 0; i < RPG_NUM_ATTACK_TYPES; ++i)
            ((CMapObjectServer *)lpMapObjectSpecific)->rpgCommonStats.psAbsorbtions[i].fpBaseValue =
                ((CMapObjectServer *)lpMapObjectSpecific)
                    ->rpgCommonStats.psAbsorbtions[i]
                    .fpBaseValue = 0;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CMapObject::IsScientific()
{
    return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CMapObject::IsScientificDoor()
{
    return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
DWORD CMapObject::GetScienceKeyNumber()
{
    return -123;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CMapObject::ApplyScience(const SRPGScienceStats &tool, CUnit *pWho)
{
    ReportWarning("Science is applied on wrong object");
    return false;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CMapObject::CanApplyScience(const SRPGScienceStats &tool)
{
    return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CMapObject::GetUsePoints(CArray<CPlot3D, CPlot3D &> &points, float fpUnitRadius)
{
    points.SetSize(0);
}
/////////////////////////////////////////////////////////////////////////////////////
void CMapObject::SetAttachedQuest(const char *pszQuest)
{
    szAttachedQuest = pszQuest;
}
/////////////////////////////////////////////////////////////////////////////////////
void CMapObject::SetQuestVarValue(float fpVal)
{
    if (fpVal == 0)
        SetQuestState(QS_NOT_AVAILABLE);
    else if (fpVal == 1)
        SetQuestState(QS_ACTIVE);
    else
        SetQuestState(QS_COMPLETED);
}
/////////////////////////////////////////////////////////////////////////////////////
// step one segment
int CMapObject::Segment()
{
    // first, segment in specific
    if (lpMapObjectSpecific)
        lpMapObjectSpecific->Segment();
    return TRUE;
}
// Advance effects on an object excluded from normal active-set segments.
bool CMapObject::TickInactiveSpellEffects()
{
    if (nActiveObjectCounter > 0 || !lpMapObjectSpecific || spellEffects.IsEmpty())
        return false;
    bool expired = false;
    for (int i = 0; i < spellEffects.GetSize(); ++i)
    {
        --spellEffects[i].nDuration;
        expired = expired || spellEffects[i].nDuration <= 0;
    }
    return expired;
}
/////////////////////////////////////////////////////////////////////////////////////
// add spell effect to mapobject's spell effects list
void CMapObject::AddSpellEffect(CSpellEffect &effect)
{
    if (!ModInvincible::AllowsEffect(*this, effect.nSpellID)) return;
    // ADD: FixSpellEffectsOverflow
    // With optimization below 20 is too much actually
    if (spellEffects.GetSize() >= 20)
    {
        static int warnsCount = 0;
        // if (warnsCount++ < 10)
        //     LOGWARN("Too many spell effects on unit %d", target->id);
        return;
    }

    spellEffects.Add(effect);
    SetUpdateMask(VLD_MASK_SPELLEFFECTS);
    bool bStrengthChanged = false;
    switch (effect.nSpellID)
    {
    case SPELL_ID_STRENGTHEN:
    case SPELL_ID_WEAKEN:
        bStrengthChanged = true;
        break;
    case SPELL_ID_ENLARGE:
    case SPELL_ID_SHRINK:
        {
            auto *mod = ModRuntime::Current();
            const int mode = mod ? mod->SpellAddon().Feature24() : 0;
            bStrengthChanged = mode != 1 && mode != 2;
        }
        break;
    }
    RecalcRPGStats(bStrengthChanged);
    if (effect.nSpellID == SPELL_ID_STUN || effect.nSpellID == SPELL_ID_TELEPORT)
        SetStunFlag(TRUE);
    //
    ChangeSpeedWithSpell();
    // change complection
    if (bStrengthChanged)
        ChangeComplectionWithSpell();

    // ADD: FixSpellEffectsOverflow
    if (effect.nDuration > 1) // Avoid optimizing instant effects
    {
        assert(!spellEffects.IsEmpty());
        assert(effect.nSpellID == spellEffects[spellEffects.GetSize() - 1].nSpellID);
        for (int i = 0; i < spellEffects.GetSize() - 1; i++)
        {
            if (spellEffects[i].nSpellID == effect.nSpellID &&
                spellEffects[i].nDuration <= effect.nDuration &&
                spellEffects[i].fpPower <= effect.fpPower &&
                // Not sure does it make sense to check radius and quiet but just in case...
                spellEffects[i].bQuiet == effect.bQuiet &&
                abs(spellEffects[i].fpRadius - effect.fpRadius) < 0.001
                // Don't check visStrength as it doesn't really matter
            )
            {
                spellEffects.RemoveAt(i);
            }
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CMapObject::ChangeComplectionWithSpell()
{
    RestoreComplection();
    float fChange = HasSpellEffect(SPELL_ID_STRENGTHEN) - HasSpellEffect(SPELL_ID_WEAKEN);
    auto *mod = ModRuntime::Current();
    const int mode = mod ? mod->SpellAddon().Feature24() : 0;
    // The native JMVParticles entry hook bypasses the retail body patched by f24.
    // Preserve f24's result when that entry hook is absent.
    const bool nativeParticles = mod && mod->CombatSetting("JMVParticles");
    const float strengthCoefficient = !nativeParticles && mode == 1 ? 0.1f : 0.4f;
    float fChange2 = nativeParticles || mode == 1 || mode == 2 ? 0.0f :
        float(HasSpellEffect(SPELL_ID_ENLARGE) - HasSpellEffect(SPELL_ID_SHRINK));
    float fTotalChange = fChange + fChange2;
    // 'shrink' and 'enlarge' will affect all complection params
    // while 'strengthen' and 'weaken' only 'strength' and 'tall' params in different cases
    if (fChange2 != 0)
    {
        if (fTotalChange != 0)
        {
            complection.strength = Max(0.0f, complection.strength + 0.4f * fTotalChange);
            complection.grace = Max(0.0f, complection.grace + 0.4f * fTotalChange);
            complection.tall = Max(0.0f, complection.tall + 0.4f * fTotalChange);
        }
    }
    else if (fChange != 0)
    {
        complection.strength = Max(0.0f, complection.strength + strengthCoefficient * fChange);
        if (GetTypeID() != MOTID_HUMAN)
            complection.tall = Max(0.0f, complection.tall + strengthCoefficient * fChange);
    }
    SetUpdateMask(VLD_MASK_COMPLECTION);
}
/////////////////////////////////////////////////////////////////////////////////////
void CMapObject::RemoveAllSpellEffects()
{
    spellEffects.SetSize(0);
    SetUpdateMask(VLD_MASK_SPELLEFFECTS);
    RecalcRPGStats(true);
    SetStunFlag(FALSE);
    ChangeComplectionWithSpell();
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CMapObject::HasSpellEffect(int nSpellID) const
{
    for (int i = 0; i < spellEffects.GetSize(); i++)
        if (spellEffects[i].nSpellID == nSpellID)
            return TRUE;
    return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
float CMapObject::GetEffectValue(int nSpellID) const
{
    float fEffectValue = 0;
    for (int i = 0; i < spellEffects.GetSize(); ++i)
    {
        if (spellEffects[i].nSpellID == nSpellID)
            fEffectValue = Max(fEffectValue, spellEffects[i].fpPower);
    }
    return fEffectValue;
}
/////////////////////////////////////////////////////////////////////////////////////
CPlot3D CMapObject::GetUnitHeadPosition()
{
    CPlot3D ptRes = GetCP() + bv.ptCenter;
    ptRes.z += GetWorldBase()->GetZValue(ptRes.x, ptRes.y);
    return ptRes;
}
/////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////
bool CMapObject::IsInvisible() const
{
    return CheckFlag(MAPOBJ_TRANSPARENT) != 0;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CMapObject::IsStuned() const
{
    return CheckFlag(MAPOBJ_STUNED) != 0;
}
/////////////////////////////////////////////////////////////////////////////////////

bool CMapObject::RefreshWeaponStatus(CSpellEffect& effect){
 for(int i=0;i<spellEffects.GetSize();++i)if(spellEffects[i].nSpellID==effect.nSpellID&&spellEffects[i].nDuration>0){
  spellEffects[i]=effect;SetUpdateMask(VLD_MASK_SPELLEFFECTS);ChangeSpeedWithSpell();return true;
 }
 if(spellEffects.GetSize()>=20)return false;
 AddSpellEffect(effect);return true;
}
