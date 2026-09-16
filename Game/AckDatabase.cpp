#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "AckDatabase.h"

#include "Unit.h"
#include "UnitDatabase.h"
#include "WorldClient.h"
#include "SoundManager.h"
#include "GSVars.h"
/////////////////////////////////////////////////////////////////////////////////////
SAckDatabase theAckDatabase;
/////////////////////////////////////////////////////////////////////////////////////
static const SAckEntry *GetAckEntry(const SAckEntryVector *pAcks, DWORD dwAckType, int nProbability)
{
    if (pAcks == 0)
        return 0;
    //
    std::vector<const SAckEntry *> result;
    // select all entries with appropriate probability
    for (auto pos = pAcks->begin(); pos != pAcks->end(); ++pos)
    {
        if (pos->nProbability >= nProbability)
            result.push_back(&*pos);
    }
    // get random entry from the selected vector
    return result.size() ? result[Random(result.size())] : 0;
}
/////////////////////////////////////////////////////////////////////////////////////
static const SAckEntry *GetAckEntryBored(const SAckEntryBoredVector *pAcks, DWORD dwAckType,
                                         DWORD dwFlags, int nProbability)
{
    if (pAcks == 0)
        return 0;
    //
    std::vector<const SAckEntry *> result;
    // select all entries with appropriate probability
    for (auto pos = pAcks->begin(); pos != pAcks->end(); ++pos)
    {
        if (pos->IsFlags(dwFlags) && (pos->nProbability >= nProbability))
            result.push_back(&*pos);
    }
    // get random entry from the selected vector
    return result.size() ? result[Random(result.size())] : 0;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL SAckDatabase::Load(LPCSTR pszPath)
{
    CString szPathName = pszPath;
    CMappedResource res;
    if (res.OpenResource(szPathName + "database.res") == FALSE)
        return ReportError("Can't open database file \"%s\"\n", szPathName + "databases.res");
    // open resource
    DWORD num;
    LPBYTE pData = res.GetFile("acks.db", &num);
    if (!pData)
        return ReportError("Can't open acks database\n");
    // load data
    CMemorySave file;

    file.StartSaving();
    file.Write(pData, num);
    file.Seek(0);
    file.StartLoading();
    //
    ackHeroAcks.Load(file, 1);
    ackUnitAcks.Load(file, 2);
    ackNPCAcks.Load(file, 3);
    // close resource
    res.CloseFile(pData);
    res.CloseResource();
    // assign pointers to use for fast ack access
    for (int i = 0; i < ackHeroAcks.GetNumEntries(); ++i)
    {
        SAckHeroAck *pAcks = const_cast<SAckHeroAck *>(ackHeroAcks[i]);
        // hero notifies
        pAcks->acks[ACK_HERO_ACK_SELECTION] = &pAcks->ackSelection;
        pAcks->acks[ACK_HERO_ACK_MOVE] = &pAcks->ackMove;
        pAcks->acks[ACK_HERO_ACK_ATTACK] = &pAcks->ackAttack;
        pAcks->acks[ACK_HERO_ACK_CAST] = &pAcks->ackCast;
        pAcks->acks[ACK_HERO_ACK_LOOT] = &pAcks->ackLoot;
        pAcks->acks[ACK_HERO_ACK_USE_OBJECT] = &pAcks->ackUseObject;
        pAcks->acks[ACK_HERO_ACK_STEAL_FROM_UNIT] = &pAcks->ackStealFromUnit;
        pAcks->acks[ACK_HERO_ACK_FOLLOW] = &pAcks->ackFollow;
        pAcks->acks[ACK_HERO_ACK_USE_POTION] = &pAcks->ackUsePotion;
        pAcks->acks[ACK_HERO_ACK_CHANGE_POSE] = &pAcks->ackChangePosition;
        pAcks->acks[ACK_HERO_ACK_ATTACK_TOO_BIG] = &pAcks->ackAttackTooBig;
        // hero refuses
        pAcks->acks[ACK_HERO_REFUSE_NO_WAY_TO_ATTACK] = &pAcks->ackNoWayToAttack;
        pAcks->acks[ACK_HERO_REFUSE_TARGET_TOO_HIGH] = &pAcks->ackTargetTooHigh;
        pAcks->acks[ACK_HERO_REFUSE_CANT_CHANGE_POSE] = &pAcks->ackCantChangePosition;
        pAcks->acks[ACK_HERO_REFUSE_PATH_NOT_FOUND] = &pAcks->ackPathNotFound;
        pAcks->acks[ACK_HERO_REFUSE_CANT_CAST] = &pAcks->ackCantCast;
        pAcks->acks[ACK_HERO_REFUSE_CANT_TELEPORT] = &pAcks->ackCantTeleport;
        pAcks->acks[ACK_HERO_REFUSE_SCIENCE_FAILED] = &pAcks->ackScienceFailed;
        pAcks->acks[ACK_HERO_REFUSE_CANT_SEE_TARGET] = &pAcks->ackCantSeeTarget;
        pAcks->acks[ACK_HERO_REFUSE_TOO_COMPLEX_SPELL_TO_CAST] = &pAcks->ackTooComplexSpellToCast;
        pAcks->acks[ACK_HERO_REFUSE_CANT_RUN_OVERLOADED] = &pAcks->ackCantRunOverloaded;
        pAcks->acks[ACK_HERO_REFUSE_CANT_RUN_INJURED] = &pAcks->ackCantRunInjured;
        // hero refuses не проигрываемые, но передаваемые
        pAcks->acks[ACK_HERO_REFUSE_TAME] = 0;
        pAcks->acks[ACK_HERO_REFUSE_CHANGE_WEAPON] = 0;
        pAcks->acks[ACK_HERO_REFUSE_SPELL_LINK] = 0;
        pAcks->acks[ACK_HERO_REFUSE_SPELL_LINK_ABORTED] = 0;
        pAcks->acks[ACK_HERO_REFUSE_SPELL_CHARM] = 0;
        pAcks->acks[ACK_HERO_REFUSE_CANT_START_REST] = 0;
        // hero notifies
        pAcks->acks[ACK_HERO_NOTIFY_DECIDED_TO_ATTACK] = &pAcks->ackDeciedToAttackUnit;
        pAcks->acks[ACK_HERO_NOTIFY_OUT_OF_STAMINA] = &pAcks->ackOutOfStamina;
        pAcks->acks[ACK_HERO_NOTIFY_ARM_CRIPPLED] = &pAcks->ackArmCrippled;
        pAcks->acks[ACK_HERO_NOTIFY_LEG_CRIPPLED] = &pAcks->ackLegCrippled;
        pAcks->acks[ACK_HERO_NOTIFY_ATTACKED_IN_DEFENCE] = &pAcks->ackAttackedInDefence;
        pAcks->acks[ACK_HERO_NOTIFY_WAITING_FOLLOWERS] = &pAcks->ackWaitingFollowers;
        pAcks->acks[ACK_HERO_NOTIFY_STEAL_EMPTY] = &pAcks->ackStealEmpty;
        pAcks->acks[ACK_HERO_NOTIFY_ARMOR_CRIPPLED] = &pAcks->ackArmorCrippled;
        pAcks->acks[ACK_HERO_NOTIFY_WEAPON_CRIPPLED] = &pAcks->ackWeaponCrippled;
        // whisper
        pAcks->acks[ACK_HERO_WHISPER_YES] = &pAcks->ackWhisperYes;
        pAcks->acks[ACK_HERO_WHISPER_NO] = &pAcks->ackWhisperNo;
        // scenario
        pAcks->acks[ACK_HERO_SCENARIO] = &pAcks->ackScenario;
    }
    //
    for (int i = 0; i < ackUnitAcks.GetNumEntries(); ++i)
    {
        SAckUnitGeneral *pAcks = const_cast<SAckUnitGeneral *>(ackUnitAcks[i]);
        //
        pAcks->acks[ACK_UNIT_BECOME_AGGRESSIVE] = &pAcks->ackBecomeAggressive;
        pAcks->acks[ACK_UNIT_BECOME_SUSPECTIOUS] = &pAcks->ackBecameSuspectious;
        pAcks->acks[ACK_UNIT_BECOME_KILLER] = &pAcks->ackBecomeKiller;
        pAcks->acks[ACK_UNIT_WHILE_IN_REST] = &pAcks->ackWhileInRestState;
        pAcks->acks[ACK_UNIT_WHILE_IN_AGGRESSIVE] = &pAcks->ackWhileInAggressiveState;
    }
    //
    for (int i = 0; i < ackNPCAcks.GetNumEntries(); ++i)
    {
        SAckNPC *pAcks = const_cast<SAckNPC *>(ackNPCAcks[i]);
        //
        pAcks->acks[ACK_NPC_SELECTED_FOR_TALKING] = &pAcks->ackSelectedForTalking;
        pAcks->acks[ACK_NPC_WHILE_IN_REST] = &pAcks->ackWhileInRestState;
    }
    //
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
DWORD GetWhisperType(DWORD dwType)
{
    switch (dwType)
    {
    case ACK_HERO_ACK_SELECTION:
    case ACK_HERO_ACK_MOVE:
    case ACK_HERO_ACK_ATTACK:
    case ACK_HERO_ACK_CAST:
    case ACK_HERO_ACK_LOOT:
    case ACK_HERO_ACK_USE_OBJECT:
    case ACK_HERO_ACK_FOLLOW:
    case ACK_HERO_ACK_USE_POTION:
    case ACK_HERO_ACK_CHANGE_POSE:
    case ACK_HERO_ACK_ATTACK_TOO_BIG:
        return ACK_HERO_WHISPER_YES; // YES
    case ACK_HERO_REFUSE_NO_WAY_TO_ATTACK:
    case ACK_HERO_REFUSE_TARGET_TOO_HIGH:
    case ACK_HERO_REFUSE_CANT_CHANGE_POSE:
    case ACK_HERO_REFUSE_PATH_NOT_FOUND:
    case ACK_HERO_REFUSE_CANT_CAST:
    case ACK_HERO_REFUSE_CANT_TELEPORT:
    case ACK_HERO_REFUSE_SCIENCE_FAILED:
    case ACK_HERO_REFUSE_CANT_SEE_TARGET:
    case ACK_HERO_REFUSE_TOO_COMPLEX_SPELL_TO_CAST:
    case ACK_HERO_NOTIFY_OUT_OF_STAMINA:
        return ACK_HERO_WHISPER_NO; // NO
    }
    // default value
    return dwType;
}
/////////////////////////////////////////////////////////////////////////////////////
const SAckEntry *PlayAck(CUnit *pUnit, DWORD dwType)
{
    if (pUnit == 0)
        return 0;
    if (dwType == ACK_CLEAR)
    {
        pUnit->GetUnitSpecific()->ClearAcks();
        return 0;
    }
    //
    /*
  #ifdef __DEMO__
    if ( (dwType == ACK_HERO_NOTIFY_ATTACKED_IN_DEFENCE) || (dwType ==
  ACK_HERO_NOTIFY_WAITING_FOLLOWERS) ) return 0; #endif // __DEMO__
    */
    //
    if (IsIDofPersonage(pUnit->GetID()) && (pUnit->GetCurrentPose() < UP_STAND))
        dwType = GetWhisperType(dwType);
    //
    const SUDBUnitPrototype *pPrototype = pUnit->GetVoicePrototype();
    if (!pPrototype)
        return 0;
    //
    const SAckEntryVector *pAcks = 0;
    const SAckEntryBoredVector *pBoredAcks = 0;
    //
    if ((dwType == ACK_HERO_NOTIFY_ARMOR_CRITICAL) || (dwType == ACK_HERO_NOTIFY_WEAPON_CRITICAL))
#ifndef __DEMO__
        theWorld.GetSoundManager()->BeginFX("weapons\\timecrash.wav", 0, pUnit->GetCP(), 8, 20);
#else
        theWorld.GetSoundManager()->BeginFX("magic\\end1.wav", 0, pUnit->GetCP(), 8, 20);
#endif                                 // __DEMO__
    else if (dwType <= ACK_HERO_BORED) // Hero Acks
    {
        // play 'armor/weapon crippled' directly in addition to main ack
        if ((dwType == ACK_HERO_NOTIFY_ARMOR_CRIPPLED) ||
            (dwType == ACK_HERO_NOTIFY_WEAPON_CRIPPLED))
            theWorld.GetSoundManager()->BeginFX("weapons\\crash.wav", 0, pUnit->GetCP(), 8, 20);
        const SAckHeroAck *pHeroAcks = theAckDatabase.ackHeroAcks[pPrototype->szName];
        if (pHeroAcks)
        {
            if (dwType == ACK_HERO_BORED)
                pBoredAcks = &(pHeroAcks->ackBored);
            else
            {
                auto pos = pHeroAcks->acks.find(dwType);
                if (pos != pHeroAcks->acks.end())
                    pAcks = pos->second;
            }
        }
    }
    else if (dwType <= ACK_NPC_WHILE_IN_REST) // NPC Acks
    {
        const SAckNPC *pNPCAcks = theAckDatabase.ackNPCAcks[pPrototype->szName];
        if (pNPCAcks)
        {
            auto pos = pNPCAcks->acks.find(dwType);
            if (pos != pNPCAcks->acks.end())
                pAcks = pos->second;
        }
    }
    else if (dwType <= ACK_UNIT_WHILE_IN_AGGRESSIVE) // Unit Acks
    {
        // find prototype's acks
        const SAckUnitGeneral *pUnitAcks = theAckDatabase.ackUnitAcks[pPrototype->szName];
        if (pUnitAcks == 0)
        {
            // if no prototype's acks, then try to find race's acks...
            const SUDBRaceModel *pRace = theUnitDatabase.races[pPrototype->dwRaceIndex];
            pUnitAcks = theAckDatabase.ackUnitAcks[pRace->szRace];
        }
        if (pUnitAcks)
        {
            auto pos = pUnitAcks->acks.find(dwType);
            if (pos != pUnitAcks->acks.end())
                pAcks = pos->second;
        }
    }
    //
    if ((pAcks == 0) && (pBoredAcks == 0))
        return 0;
    //
    const SAckEntry *pAck = 0;
    if (pBoredAcks != 0)
    {
        // bored acks we can play only in the case of moving/idling
        const SADEntry *pADEntry = pUnit->GetAnimationSystem2()->GetCurrentAnimation();
        if (pADEntry)
        {
            if (((pADEntry->dwFlags & ADB_ACTIONS) == ADB_ACTION_RUN) ||
                ((pADEntry->dwFlags & ADB_ACTIONS) == ADB_ACTION_WALK) ||
                ((pADEntry->dwFlags & ADB_ACTIONS) == ADB_ACTION_IDLE))
            {
                DWORD dwAckFlags = theWorld.FormBoredAckFlagsForUnit(pUnit);
                pAck = GetAckEntryBored(pBoredAcks, dwType, dwAckFlags, Random(100));
            }
        }
    }
    else
        pAck = GetAckEntry(pAcks, dwType, Random(100));
    //
    if (pAck)
        pUnit->GetUnitSpecific()->AddAck(pAck, dwType);

    return pAck;
}
/////////////////////////////////////////////////////////////////////////////////////
const SAckEntry *PlayScenarioAck(CUnit *pUnit, const char *pszKey, bool bBlockUnit)
{
    if (pUnit == 0)
        return 0;

    const SUDBUnitPrototype *pPrototype = pUnit->GetVoicePrototype();
    if (!pPrototype)
        return 0;
    const SAckHeroAck *pHeroAcks = theAckDatabase.ackHeroAcks[pPrototype->szName];
    if (pHeroAcks == 0)
        return 0;
    const SAckEntry *pAck = 0;
    // select ack
    for (auto pos = pHeroAcks->ackScenario.begin(); pos != pHeroAcks->ackScenario.end(); ++pos)
    {
        if (pos->szKey == pszKey)
        {
            pAck = &(*pos);
            break;
        }
    }
    //
    DWORD dwAckType = bBlockUnit ? ACK_HERO_SCENARIO_BLOCK : ACK_HERO_SCENARIO;
    if (pAck)
        pUnit->GetUnitSpecific()->AddAck(pAck, dwAckType);
    //
    return pAck;
}
/////////////////////////////////////////////////////////////////////////////////////
