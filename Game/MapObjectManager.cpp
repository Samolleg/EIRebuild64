#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "MapObjectManager.h"

#include "World.h"
#include "Unit.h"
#include "StuffMapObjects.h"
#include "ItemManager.h"
#include "SpellManager.h"
#include "CResourceBase.h"
#include "StringProcessing.h"
#include "BasicChunk.h"
#include "PerkDatabase.h"
/////////////////////////////////////////////////////////////////////////////////////
CMapObjectManager::CMapObjectManager()
{
#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
}
/////////////////////////////////////////////////////////////////////////////////////
CMapObjectManager::~CMapObjectManager()
{
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CMapObjectManager::Create()
// open animation database and create all internal data structures
BOOL CMapObjectManager::Create()
{
    CString szResourcesPath = ((CWorld *)GetWorld())->GetResourcePath();

    if (CResourceManager::Create() == FALSE)
        return FALSE;
    // add database resource: animations, units, items, etc.
    AddNewResource(szResourcesPath + "database.res");

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// void CMapObjectManager::Destroy()
// close animation database and destroy all internal data structures
void CMapObjectManager::Destroy()
{
    CResourceManager::Destroy();
}
/////////////////////////////////////////////////////////////////////////////////////
// LPGAMEBASE CMapObjectManager::CreateInstant( ETypeIDs tidTypeID )
// create data instant with desired type ID
LPGAMEBASE CMapObjectManager::CreateInstant(ETypeIDs tidTypeID)
{
    LPGAMEBASE pBase = NULL;

    switch (tidTypeID)
    {
    case MOTID_HUMAN:
    case MOTID_ANIMAL:
    case MOTID_MONSTER:
        pBase = dbgnew CUnit();
        break;
    case MOTID_TREE:
        pBase = dbgnew CTree();
        break;
    case MOTID_TORCH:
        pBase = dbgnew CTorchObject();
        break;
    case MOTID_STONE:
        pBase = dbgnew CStone();
        break;
    case MOTID_BUILDING:
        pBase = dbgnew CBuilding();
        break;
    case MOTID_OBJECT:
        pBase = dbgnew CStuffObject();
        break;
    case MOTID_UNIT:
        pBase = dbgnew CUnit();
        break;
    case ASTID_ANIMATION_SYSTEM2:
        pBase = dbgnew CAnimationSystem2();
        break;
    case MOTID_LEVER:
        pBase = dbgnew CLeverObject;
        break;
    case MOTID_TRAP:
        pBase = dbgnew CMagicTrapObject;
        break;
    case MOTID_KAPKAN:
        pBase = dbgnew CTrapObject;
        break;
    }

    return pBase;
}
/////////////////////////////////////////////////////////////////////////////////////
// LPGAMEBASE CMapObjectManager::CreateData( ETypeIDs tidTypeID, int nNewID )
// create data and specific parts
LPGAMEBASE CMapObjectManager::CreateData(ETypeIDs tidTypeID, int nNewID)
{
    LPMAPOBJECT lpMapObject = LPMAPOBJECT(CResourceManager::CreateData(tidTypeID, nNewID));
    LPGAMEBASE lpSpecific = NULL;

    if (lpMapObject)
    {
        // create and set animation system for object
#if !(defined(_A4) || defined(__OBJECT_CONVERTOR_A4__))
        CAnimationSystem2 *lpAnimationSystem = (CAnimationSystem2 *)GetWorld()
                                                   ->GetFigureManager()
                                                   ->CreateData(ASTID_ANIMATION_SYSTEM2);
#else
        CAnimationSystem3 *lpAnimationSystem = (CAnimationSystem3 *)GetWorld()
                                                   ->GetFigureManager()
                                                   ->CreateData(ASTID_ANIMATION_SYSTEM3);
#endif // !( defined(_A4) || defined(__OBJECT_CONVERTOR_A4__) )
        lpMapObject->SetAnimationSystem2(lpAnimationSystem);
        // create and set common specifics for all CMapObject derived classes: CObjectSpecific and
        // CMapObjectSpecific
        lpSpecific =
            ((CWorld *)GetWorld())->GetSpecificManager()->CreateData(SPTID_OBJECT3D_SPECIFIC);
        lpMapObject->SetSpecific(lpSpecific);
        lpSpecific =
            ((CWorld *)GetWorld())->GetSpecificManager()->CreateData(SPTID_MAPOBJECT_SPECIFIC);
        lpMapObject->SetSpecific(lpSpecific);
        // create and set special specifics (unit/tree/building/etc.)
        switch (tidTypeID)
        {
        case MOTID_UNIT:
        case MOTID_HUMAN:
        case MOTID_MONSTER:
        case MOTID_ANIMAL:
            lpSpecific =
                ((CWorld *)GetWorld())->GetSpecificManager()->CreateData(SPTID_UNIT_SPECIFIC);
            lpMapObject->SetSpecific(lpSpecific);
            break;
        case MOTID_TORCH:
            lpSpecific =
                ((CWorld *)GetWorld())->GetSpecificManager()->CreateData(SPTID_TORCH_SPECIFIC);
            lpMapObject->SetSpecific(lpSpecific);
        }
    }

    return lpMapObject;
}
/////////////////////////////////////////////////////////////////////////////////////
// SADEntry* CMapObjectManager::GetAnimationEntry( CString& szMaskName, DWORD dwCaps )
// get animation entry, which exactly match specified caps
SADEntry *CMapObjectManager::GetAnimationEntry(CString &szMaskName, DWORD dwCaps)
{
    LPBYTE lpData = GetFile(szMaskName + ".adb");
    SADBFileHeader *pHdr = (SADBFileHeader *)lpData;
    SADEntry *entries = NULL;
    // check for file existance
    if (lpData == NULL)
    {
        ReportWarning("Can't find animation database file for \"%s\".\n", szMaskName);
        return NULL;
    }
    // assign pointer to entries
    entries = (SADEntry *)(lpData + pHdr->ShiftToEntry(0));
    //
    for (int i = 0; i < int(pHdr->dwCount); i++)
    {
        if (entries[i].Match(dwCaps))
            return &entries[i];
    }

    return NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
SADEntry *CMapObjectManager::GetAnimationEntryByIndex(CString &szMaskName, DWORD cIndex)
{
    LPBYTE lpData = GetFile(szMaskName + ".adb");
    SADBFileHeader *pHdr = (SADBFileHeader *)lpData;
    // check for file existance
    if (lpData == NULL)
    {
        ReportWarning("Can't find animation database file for \"%s\".\n", szMaskName);
        return 0;
    }
    //
    if (cIndex >= pHdr->dwCount)
        return 0;

    return reinterpret_cast<SADEntry *>(lpData + pHdr->ShiftToEntry(cIndex));
}
/////////////////////////////////////////////////////////////////////////////////////
SADEntry *CMapObjectManager::GetSpecialAnimationEntry(CString &szMaskName, DWORD dwSpecialIndex)
{
    LPBYTE lpData = GetFile(szMaskName + ".adb");
    SADBFileHeader *pHdr = (SADBFileHeader *)lpData;
    SADEntry *entries = NULL;
    // check for file existance
    if (lpData == NULL)
    {
        ReportWarning("Can't find animation database file for \"%s\".\n", szMaskName);
        return NULL;
    }
    // assign pointer to entries
    entries = (SADEntry *)(lpData + pHdr->ShiftToEntry(0));
    //
    DWORD dwModifierSpecial = ADB_MODIFIER_SPECIAL_1 * dwSpecialIndex;
    for (int i = 0; i < int(pHdr->dwCount); i++)
    {
        if (((entries[i].dwFlags & ADB_ACTIONS) == ADB_ACTION_SPECIAL) &&
            ((entries[i].dwFlags & ADB_MODIFIERS) == dwModifierSpecial))
            return &entries[i];
    }

    return NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
// SADEntry* CMapObjectManager::GetAnimationEntry( CString& szMaskName, DWORD dwCaps )
// get animation entry, which exactly match specified caps
void CMapObjectManager::GetAllAnimationEntries(CString &szMaskName, CADEntryPtrArray &szArray)
{
    LPBYTE lpData = GetFile(szMaskName + ".adb");
    SADBFileHeader *pHdr = (SADBFileHeader *)lpData;
    SADEntry *entries = NULL;
    // check for file existance
    if (lpData == NULL)
    {
        ReportWarning("Can't find animation database file for \"%s\".\n", szMaskName);
        return;
    }
    // assign pointer to entries
    entries = (SADEntry *)(lpData + pHdr->ShiftToEntry(0));
    //
    szArray.RemoveAll();
    for (int i = 0; i < int(pHdr->dwCount); i++)
    {
        szArray.Add(&entries[i]);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
SADEntry *CMapObjectManager::GetAnimationEntry(const CString &szMaskName, LPCSTR pszAnimationName)
{
    LPBYTE lpData = GetFile(szMaskName + ".adb");
    SADBFileHeader *pHdr = (SADBFileHeader *)lpData;
    SADEntry *entries = NULL;
    // check for file existance
    if (lpData == NULL)
    {
        ReportWarning("Can't find animation database file for \"%s\".\n", szMaskName);
        return 0;
    }
    // assign pointer to entries
    entries = (SADEntry *)(lpData + pHdr->ShiftToEntry(0));
    //
    for (int i = 0; i < int(pHdr->dwCount); i++)
    {
        if (stricmp(entries[i].pszAnimationName, pszAnimationName) == 0)
            return &(entries[i]);
    }

    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
// SADEntry* CMapObjectManager::GetRandomAnimationEntry( CString& szMaskName, DWORD dwCaps )
// get one of animation entry, which exactly match specified caps
SADEntry *CMapObjectManager::GetRandomAnimationEntry(CString &szMaskName, DWORD dwCaps)
{
    LPBYTE lpData = GetFile(szMaskName + ".adb");
    SADBFileHeader *pHdr = (SADBFileHeader *)lpData;
    CSimplyArray<SADEntry *, SADEntry *> entryArray;
    SADEntry *entries = NULL;

    // check for file existance
    if (lpData == NULL)
    {
        ReportWarning("Can't find animation database file for \"%s\".\n", szMaskName);
        return NULL;
    }
    // assign pointer to entries
    entries = (SADEntry *)(lpData + pHdr->ShiftToEntry(0));
    //
    for (int i = 0; i < int(pHdr->dwCount); i++)
    {
        if (entries[i].Match(dwCaps))
            entryArray.Add(&entries[i]);
    }
    //
    if (entryArray.GetSize() > 1)
    {
        DWORD dwProbability = Random(100);
        SADEntry *pReservedADEntry = entryArray[0];
        // remove all entries with probability less, then random value
        for (int i = 0; i < entryArray.GetSize(); ++i)
        {
            if (entryArray[i]->dwProbability < dwProbability)
            {
                entryArray.RemoveAt(i);
                --i;
            }
        }
        if (entryArray.GetSize() > 0)
            return entryArray[Random(entryArray.GetSize())];
        else
            return pReservedADEntry;
    }
    else if (entryArray.GetSize() == 1)
        return entryArray[0];
    else
        return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
ETypeIDs CMapObjectManager::GetTypeIDFromPrototype(LPCSTR pszPrototypeName)
{
    const SUDBUnitPrototype *pPrototype = GetPrototype(pszPrototypeName);
    if (pPrototype == 0)
        return MOTID_MONSTER;
    const SUDBRaceModel *pRace = GetRace(pPrototype->dwRaceIndex);
    if (pRace == 0)
        return MOTID_MONSTER;

    return pRace->tidTypeID;
}
/////////////////////////////////////////////////////////////////////////////////////
// collect names of available hit location types
void CMapObjectManager::GetHitLocationTypeNames(CStringArray &szNames) const
{
    szNames.SetSize(theUnitDatabase.hlts.GetNumEntries());
    for (int i = 0; i < szNames.GetSize(); i++)
        szNames[i] = theUnitDatabase.hlts[i]->szName;
}
/////////////////////////////////////////////////////////////////////////////////////
// collect names of available races
void CMapObjectManager::GetRaceNames(CStringArray &szNames) const
{
    szNames.SetSize(theUnitDatabase.races.GetNumEntries());
    for (int i = 0; i < szNames.GetSize(); i++)
        szNames[i] = theUnitDatabase.races[i]->szRace;
}
/////////////////////////////////////////////////////////////////////////////////////
// collect names of available prototypes
void CMapObjectManager::GetPrototypeNames(CStringArray &szNames) const
{
    szNames.SetSize(theUnitDatabase.prototypes.GetNumEntries());
    for (int i = 0; i < szNames.GetSize(); i++)
        szNames[i] = theUnitDatabase.prototypes[i]->szName;
}
/////////////////////////////////////////////////////////////////////////////////////
// collect names of available NPC prototypes
void CMapObjectManager::GetNPCPrototypeNames(CStringArray &szNames) const
{
    szNames.SetSize(theUnitDatabase.npcs.GetNumEntries());
    for (int i = 0; i < szNames.GetSize(); i++)
        szNames[i] = theUnitDatabase.npcs[i]->szName;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CMapObjectManager::SetupDefaultStats(SRPGCommonStats *pCommonStats, SRPGUnitStats *pUnitStats)
{
    int i, j;

    if (pUnitStats != 0)
    {
        pUnitStats->fpExperience = 0;
        pUnitStats->fSpentExp = 0;
        pUnitStats->psStrength.fpBaseValue = 50;
        pUnitStats->psDexterity.fpBaseValue = 50;
        pUnitStats->psIntelligence.fpBaseValue = 50;
        pUnitStats->move.fpBaseValue = 0.2f;
        pUnitStats->action.fpBaseValue = 1;
        pUnitStats->fpSpeeds[0] = 2.0f / 15.0f;
        pUnitStats->fpSpeeds[1] = 2.0f / 15.0f;
        pUnitStats->fpSpeeds[2] = 2.0f / 15.0f;
        pUnitStats->fpSpeeds[3] = 2.0f / 15.0f;
        pUnitStats->fpMaxEncumbrance = 0;
        pUnitStats->dwLocomotion = 0;
        for (i = 0; i < RPG_NUM_SENSES; i++)
        {
            pUnitStats->senses[i].fpEffectiveValue = pUnitStats->senses[i].fpBaseValue = 100;
            pUnitStats->percepts[i].fpEffectiveValue = pUnitStats->percepts[i].fpBaseValue = 100;
        }
        for (i = 0; i < RPG_NUM_HIT_LOCATIONS; i++)
        {
            pUnitStats->hls[i].hlt = RPGHLT_BODY;
            pUnitStats->hls[i].dwCrippleMask = 0;
            pUnitStats->hls[i].state = RPGALS_ACTIVE;
            pUnitStats->hls[i].fpHP = 100;
            pUnitStats->hls[i].fpMaxHP = 100;
            // Default locations have equal HP; use equal nonzero distribution weights.
            pUnitStats->hls[i].fpHPWeight = 1;
            for (j = 0; j < RPG_NUM_ARMOR_LAYERS; j++)
            {
                pUnitStats->hls[i].layers[j].dwCrippleMask = 0;
                pUnitStats->hls[i].layers[j].state = RPGALS_NOTUSED;
                pUnitStats->hls[i].layers[j].lpItems[0] = NULL;
                pUnitStats->hls[i].layers[j].lpItems[1] = NULL;
                MemSetDWord((DWORD *)pUnitStats->hls[i].layers[j].fpAbsorbtions, 0,
                            RPG_NUM_ATTACK_TYPES);
            }
        }
    }
    else
    {
        // hp
        pCommonStats->ssHP.fpCurrent = 1000;
        pCommonStats->ssHP.fpMax = 1000;
        pCommonStats->ssHP.fpRegenerate = 10;
        // mp
        pCommonStats->ssMP.fpCurrent = 1000;
        pCommonStats->ssMP.fpMax = 1000;
        pCommonStats->ssMP.fpRegenerate = 10;
        // resistances and absorbtions
        for (i = 0; i < RPG_NUM_ATTACK_TYPES; i++)
            pCommonStats->psAbsorbtions[i].fpBaseValue = pCommonStats->psAbsorbtions[i]
                                                             .fpEffectiveValue = 100;
    }

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// setup default stats for mapobject by their subtypeID
BOOL CMapObjectManager::SetupDefaultStats(LPGAMEBASE lpMapObject)
{
    SRPGCommonStats rpgCommonStats;
    SRPGUnitStats rpgUnitStats;

    rpgCommonStats.Clear();

    switch (lpMapObject->WhoAmI())
    {
    case HPC_UNIT:
        rpgUnitStats.Clear();
        SetupDefaultStats(&rpgCommonStats, &rpgUnitStats);
        ((CMapObject *)lpMapObject)->SetRPGStats(&rpgUnitStats);
        break;
    default:
        SetupDefaultStats(&rpgCommonStats, 0);
        ((CMapObject *)lpMapObject)->SetRPGStats(&rpgCommonStats);
    }

    ((CMapObject *)lpMapObject)->RecalcRPGStats();
    lpMapObject->SetSubtypeID(0);

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CMapObjectManager::SetupStats(SRPGCommonStats *pCommonStats, SRPGUnitStats *pUnitStats,
                                   LPCSTR pszPrototype)
{
    const SUDBUnitPrototype *pPrototype = theUnitDatabase.prototypes[pszPrototype];
    // check for prototype
    if (pPrototype == 0)
    {
        SetupDefaultStats(pCommonStats, pUnitStats);
        //		ReportWarning( "Can't find prototype \"%s\" to setup stats.\n", pszPrototype
        //);
        return TRUE;
    }
    const SUDBRaceModel *pRace = theUnitDatabase.races[pPrototype->dwRaceIndex];
    const SUDBNPCPrototype *pNPC = theUnitDatabase.npcs[pszPrototype];
    int i;
    //
    if ((pCommonStats == 0) || (pUnitStats == 0))
        return FALSE;
    //
    pCommonStats->Clear();
    pUnitStats->Clear();
    // set values for common stats
    // health and mana params
    pCommonStats->ssHP.fpCurrent = pCommonStats->ssHP.fpMax = pPrototype->fpMaxHP;
    pCommonStats->ssHP.fpRegenerate = pRace->fpRegenerateHP;
    pCommonStats->ssMP.fpCurrent = pCommonStats->ssMP.fpMax = pPrototype->fpMaxMP;
    pCommonStats->ssMP.fpRegenerate = pRace->fpRegenerateMP;
    pUnitStats->fBaseHP = pCommonStats->ssHP.fpMax;
    // set values for unit stats
    if (pNPC)
    {
        pUnitStats->psStrength.fpEffectiveValue = pUnitStats->psStrength
                                                      .fpBaseValue = pNPC->fStrength;
        pUnitStats->psDexterity.fpEffectiveValue = pUnitStats->psDexterity
                                                       .fpBaseValue = pNPC->fDexterity;
        pUnitStats->psIntelligence.fpEffectiveValue = pUnitStats->psIntelligence
                                                          .fpBaseValue = pNPC->fIntelligence;
    }
    else
    {
        pUnitStats->psStrength.fpEffectiveValue = pUnitStats->psStrength
                                                      .fpBaseValue = pPrototype->fpMaxHP *
                                                                     (1.0f / 3.0f);
        pUnitStats->psDexterity.fpEffectiveValue = pUnitStats->psDexterity.fpBaseValue =
            pPrototype->fpMaxMP * (1.0f / 3.0f); // pPrototype->fpMovePoints * 15.0f;
        pUnitStats->psIntelligence.fpEffectiveValue = pUnitStats->psIntelligence
                                                          .fpBaseValue = 25.0f;
    }
    // action and move points
    if (pNPC)
    {
        pUnitStats->move.fpBaseValue = pPrototype->fpMovePoints;
        pUnitStats->action.fpBaseValue = pUnitStats->psDexterity.fpEffectiveValue * 0.5f;
    }
    else
    {
        pUnitStats->move.fpBaseValue = pPrototype->fpMovePoints;
        pUnitStats->action.fpBaseValue = 15;
    }
    pUnitStats->fpNAWActionPoints = pPrototype->fpActionPoints;

    memcpy(pUnitStats->fpSpeeds, pRace->fpSpeeds, sizeof(pUnitStats->fpSpeeds));
    // senses and percepts
    for (i = 0; i < RPG_NUM_SENSES; i++)
    {
        pUnitStats->senses[i].fpBaseValue = pPrototype->senses[i];
        pUnitStats->percepts[i].fpBaseValue = pPrototype->percepts[i];
    }
    //
    pUnitStats->fpVisionArc = (float)cos(ToRadian(pRace->fpVisionArc * 0.5f));
    pUnitStats->fpPeripheral = pPrototype->fpPeripheral;
    pUnitStats->fpPeripheralArc = (float)cos(ToRadian(180.0f * 0.5f));
    pUnitStats->dwLocomotion = pRace->dwLocomotion;
    // setup default skill values
    if (pNPC)
    {
        memcpy(pUnitStats->cManualSkills, pNPC->cManualSkills, sizeof(pUnitStats->cManualSkills));
        memcpy(pUnitStats->cAutoSkills, pNPC->cAutoSkills, sizeof(pUnitStats->cAutoSkills));
    }
    else
    {
        for (i = 0; i < RPG_NUM_MANUAL_SKILLS; i++)
            pUnitStats->cManualSkills[i] = (BYTE)MINT(pPrototype->fpDefaultSkill);
        for (i = 0; i < RPG_NUM_AUTO_SKILLS; i++)
            pUnitStats->cAutoSkills[i] = (BYTE)MINT(pPrototype->fpDefaultSkill);
        // setup special default skill values
        pUnitStats->cManualSkills[RPGMSKL_SCIENCE] = (BYTE)MINT(pPrototype->fpSkillSteal);
        pUnitStats->cManualSkills[RPGMSKL_STEALING] = (BYTE)MINT(pPrototype->fpSkillSteal);
        pUnitStats->cManualSkills[RPGMSKL_TAME] = (BYTE)MINT(pPrototype->fpSkillTame);
        pUnitStats->cAutoSkills[RPGASKL_ELEMENTAL] = BYTE(pPrototype->magics[0]);
        pUnitStats->cAutoSkills[RPGASKL_SENSE] = BYTE(pPrototype->magics[1]);
        pUnitStats->cAutoSkills[RPGASKL_ASTRAL] = BYTE(pPrototype->magics[2]);
    }
    //
    pUnitStats->fpAttackDistance = pRace->fpAttackDistance;
    pUnitStats->wAIClassStay = pRace->dwAIClassStay;
    pUnitStats->wAIClassLie = pRace->dwAIClassLie;
    pUnitStats->wAIClassCrawl = pUnitStats->wAIClassLie;
    // common absorptions
    for (i = 0; i < RPG_NUM_ATTACK_TYPES; ++i)
        pCommonStats->psAbsorbtions[i].fpEffectiveValue =
            pCommonStats->psAbsorbtions[i].fpBaseValue = pRace->defences[i] *
                                                         pPrototype->fpAbsorbtionMultiplier;
    // hit locations
    for (i = 0; i < RPG_NUM_HIT_LOCATIONS; i++)
    {
        if (pRace->hls[i].fpWeight == 0)
        {
            pUnitStats->hls[i].state = RPGALS_NOTUSED;
            continue;
        }
        const SUDBHitLocationType *pHLT = theUnitDatabase.hlts[pRace->hls[i].dwHLTypeIndex];

        pUnitStats->hls[i].hlt = pHLT->hlt;
        pUnitStats->hls[i].fpHPWeight = pRace->hls[i].fpWeight;
        pUnitStats->hls[i].fpVitality = pRace->hls[i].fpVitality;
        pUnitStats->hls[i].dwCrippleMask = pHLT->dwCrippleMask;
        /*
        for ( j=0; j<RPG_NUM_ATTACK_TYPES; ++j )
        {
                pUnitStats->hls[i].layers[0].fpAbsorbtions[j] =
                        pHLT->fpAbsorbtions[j] + pPrototype->fpAbsorbtionMultiplier *
        pHLT->fpResistances[j];
        }
        */
        pUnitStats->hls[i].layers[0].dwCrippleMask = 0;
        pUnitStats->hls[i].layers[0].state = RPGALS_NOTUSED;
        pUnitStats->hls[i].layers[1].state = RPGALS_NOTUSED;
        pUnitStats->hls[i].layers[2].state = RPGALS_NOTUSED;
        pUnitStats->hls[i].state = RPGALS_ACTIVE;
    }
    // native attack weapon params
    pUnitStats->fpNAWRange = pPrototype->fpAttackRange;
    pUnitStats->fpNAWToHit = pPrototype->fpAttackToHit;
    pUnitStats->fpNAWToHitRandom = pPrototype->fpAttackToHitRandom;
    pUnitStats->fpNAWParry = pPrototype->fpAttackParry;
    pUnitStats->fpNAWWeight = pPrototype->fpAttackWeight;
    pUnitStats->fpNAWDamageMin = pPrototype->fpDamageMin;
    pUnitStats->fpNAWDamageRnd = pPrototype->fpDamageRandom;
    memcpy(pUnitStats->fpNAWAttacks, pRace->attacks, sizeof(float) * RPG_NUM_ATTACK_TYPES);
    pUnitStats->rpgNAWWeaponType = pPrototype->rpgWeaponType;
    //
    // expirience and perks
    pUnitStats->fSpentExp = 0;
    if (pNPC)
    {
        pUnitStats->GainExperience(pNPC->fExpirience);
        for (auto pos = pNPC->szPerks.begin(); pos != pNPC->szPerks.end(); ++pos)
        {
            const SPDBPerk *pPerk = thePerkDatabase.GetPerk(*pos);
            if (pPerk)
                pUnitStats->AddPerk(&thePerkDatabase, pPerk);
        }
        pUnitStats->fSpentExp = pUnitStats->fpExperience;
    }

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CMapObjectManager::SetupStats(LPGAMEBASE lpMapObject, LPCSTR pszPrototype)
{
    const SUDBUnitPrototype *pPrototype = theUnitDatabase.prototypes[pszPrototype];
    // check for prototype
    if (pPrototype == 0)
    {
        SetupDefaultStats(lpMapObject);
        ReportWarning("Can't find prototype \"%s\" for object %d.\n", pszPrototype,
                      lpMapObject->GetID());
        return TRUE;
    }
    const SUDBRaceModel *pRace = theUnitDatabase.races[pPrototype->dwRaceIndex];
    //
    switch (lpMapObject->WhoAmI())
    {
    case HPC_UNIT: {
        CUnit *lpUnit = (CUnit *)lpMapObject;
        SRPGCommonStats rpgCommonStats;
        SRPGUnitStats rpgUnitStats;
        //
        SetupStats(&rpgCommonStats, &rpgUnitStats, pszPrototype);
        // setup stats
        rpgCommonStats.dwFlags |= RPGCS_PRESET_VALUES;
        rpgUnitStats.dwFlags |= RPGUS_PRESET_VALUES;
        //
        lpUnit->CMapObject::SetRPGStats(&rpgCommonStats);
        lpUnit->SetRPGStats(&rpgUnitStats);
        //
        lpUnit->SetCenterShift(CPlot3D(0, -(pRace->fpCenterShift), 0));
        //
        SComplection comp = { pPrototype->fStrength, pPrototype->fGrace, pPrototype->fTall };
        lpUnit->SetComplection(comp);
        lpUnit->SetBaseComplection(comp);
        lpUnit->RestoreComplection();
        // voice prototype
        lpUnit->SetVoicePrototype(pPrototype);
        break;
    }
    }

    lpMapObject->SetSubtypeID(theUnitDatabase.prototypes.GetEntryIndex(pszPrototype));

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CMapObjectManager::CreateNewHero(SRPGUnitStats *lpStats, LPCSTR pszPrototype)
{
    int i;
    const SUDBUnitPrototype *pPrototype = theUnitDatabase.prototypes[pszPrototype];
    //
    if (pPrototype == 0)
        return ReportError("Can't find prototype \"%s\" for new HERO.\n", pszPrototype);

    const SUDBRaceModel *pRace = theUnitDatabase.races[pPrototype->dwRaceIndex];
    const SUDBNPCPrototype *pNPC = theUnitDatabase.npcs[pszPrototype];
    if (pNPC == 0)
        ReportWarning("Can't find NPC prototype \"%s\" for new HERO.\n", pszPrototype);
    //
    lpStats->Clear();
    // set values for unit stats
    if (pNPC)
    {
        lpStats->psStrength.fpEffectiveValue = lpStats->psStrength.fpBaseValue = pNPC->fStrength;
        lpStats->psDexterity.fpEffectiveValue = lpStats->psDexterity.fpBaseValue = pNPC->fDexterity;
        lpStats->psIntelligence.fpEffectiveValue = lpStats->psIntelligence
                                                       .fpBaseValue = pNPC->fIntelligence;
    }
    else
    {
        lpStats->psStrength.fpEffectiveValue = lpStats->psStrength.fpBaseValue = pPrototype
                                                                                     ->fpMaxHP *
                                                                                 (1.0f / 3.0f);
        lpStats->psDexterity.fpEffectiveValue = lpStats->psDexterity.fpBaseValue =
            pPrototype->fpMaxMP * (1.0f / 3.0f); // pPrototype->fpMovePoints * 15.0f;
        lpStats->psIntelligence.fpEffectiveValue = lpStats->psIntelligence.fpBaseValue = 25.0f;
    }
    // convert to tiles per segment
    if (pNPC)
    {
        lpStats->move.fpEffectiveValue = lpStats->move.fpBaseValue = pPrototype->fpMovePoints;
        lpStats->action.fpEffectiveValue = lpStats->action.fpBaseValue = lpStats->psDexterity
                                                                             .fpEffectiveValue *
                                                                         0.5f;
    }
    else
    {
        lpStats->move.fpEffectiveValue = lpStats->move.fpBaseValue = pPrototype->fpMovePoints;
        lpStats->action.fpEffectiveValue = lpStats->action.fpBaseValue = 15;
    }
    lpStats->fpNAWActionPoints = pPrototype->fpActionPoints;
    //
    memcpy(lpStats->fpSpeeds, pRace->fpSpeeds, sizeof(lpStats->fpSpeeds));
    // senses and percepts
    for (i = 0; i < RPG_NUM_SENSES; i++)
    {
        lpStats->senses[i].fpEffectiveValue = lpStats->senses[i].fpBaseValue = pPrototype
                                                                                   ->senses[i];
        lpStats->percepts[i].fpEffectiveValue = lpStats->percepts[i]
                                                    .fpBaseValue = pPrototype->percepts[i];
    }
    //
    lpStats->fpVisionArc = (float)cos(ToRadian(pRace->fpVisionArc * 0.5f));
    lpStats->fpPeripheral = pPrototype->fpPeripheral;
    lpStats->fpPeripheralArc = (float)cos(ToRadian(180.0f * 0.5f));
    lpStats->dwLocomotion = pRace->dwLocomotion;
    // setup default skill values
    if (pNPC)
    {
        memcpy(lpStats->cManualSkills, pNPC->cManualSkills, sizeof(lpStats->cManualSkills));
        memcpy(lpStats->cAutoSkills, pNPC->cAutoSkills, sizeof(lpStats->cAutoSkills));
    }
    else
    {
        for (i = 0; i < RPG_NUM_MANUAL_SKILLS; i++)
            lpStats->cManualSkills[i] = (BYTE)MINT(pPrototype->fpDefaultSkill);
        for (i = 0; i < RPG_NUM_AUTO_SKILLS; i++)
            lpStats->cAutoSkills[i] = (BYTE)MINT(pPrototype->fpDefaultSkill);
        // setup special default skill values
        lpStats->cManualSkills[RPGMSKL_SCIENCE] = (BYTE)MINT(pPrototype->fpSkillSteal);
        lpStats->cManualSkills[RPGMSKL_STEALING] = (BYTE)MINT(pPrototype->fpSkillSteal);
        lpStats->cManualSkills[RPGMSKL_TAME] = (BYTE)MINT(pPrototype->fpSkillTame);
        lpStats->cAutoSkills[RPGASKL_ELEMENTAL] = BYTE(pPrototype->magics[0]);
        lpStats->cAutoSkills[RPGASKL_SENSE] = BYTE(pPrototype->magics[1]);
        lpStats->cAutoSkills[RPGASKL_ASTRAL] = BYTE(pPrototype->magics[2]);
    }
    //
    lpStats->fpAttackDistance = pRace->fpAttackDistance;
    lpStats->wAIClassStay = pRace->dwAIClassStay;
    lpStats->wAIClassLie = pRace->dwAIClassLie;
    lpStats->wAIClassCrawl = lpStats->wAIClassLie;

    // hit locations
    for (i = 0; i < RPG_NUM_HIT_LOCATIONS; i++)
    {
        if (pRace->hls[i].fpWeight == 0)
        {
            lpStats->hls[i].state = RPGALS_NOTUSED;
            continue;
        }
        const SUDBHitLocationType *pHLT = theUnitDatabase.hlts[pRace->hls[i].dwHLTypeIndex];

        lpStats->hls[i].hlt = pHLT->hlt;
        lpStats->hls[i].fpHPWeight = pRace->hls[i].fpWeight;
        lpStats->hls[i].fpVitality = pRace->hls[i].fpVitality;
        lpStats->hls[i].dwCrippleMask = pHLT->dwCrippleMask;
        /*
        for ( j=0; j<RPG_NUM_ATTACK_TYPES; ++j )
        {
                lpStats->hls[i].layers[0].fpAbsorbtions[j] =
                        pHLT->fpAbsorbtions[j] + pPrototype->fpAbsorbtionMultiplier *
        pHLT->fpResistances[j];
        }
        */
        lpStats->hls[i].layers[0].dwCrippleMask = 0;
        lpStats->hls[i].layers[0].state = RPGALS_NOTUSED;
        lpStats->hls[i].layers[1].state = RPGALS_NOTUSED;
        lpStats->hls[i].layers[2].state = RPGALS_NOTUSED;
        lpStats->hls[i].state = RPGALS_ACTIVE;
    }
    // native attack weapon params
    lpStats->fpNAWRange = pPrototype->fpAttackRange;
    lpStats->fpNAWToHit = pPrototype->fpAttackToHit;
    lpStats->fpNAWToHitRandom = pPrototype->fpAttackToHitRandom;
    lpStats->fpNAWParry = pPrototype->fpAttackParry;
    lpStats->fpNAWWeight = pPrototype->fpAttackWeight;
    lpStats->fpNAWDamageMin = pPrototype->fpDamageMin;
    lpStats->fpNAWDamageRnd = pPrototype->fpDamageRandom;
    memcpy(lpStats->fpNAWAttacks, pRace->attacks, sizeof(float) * RPG_NUM_ATTACK_TYPES);
    lpStats->rpgNAWWeaponType = pPrototype->rpgWeaponType;
    //
    // expirience and perks
    lpStats->fSpentExp = 0;
    if (pNPC)
    {
        lpStats->GainExperience(pNPC->fExpirience);
        for (auto pos = pNPC->szPerks.begin(); pos != pNPC->szPerks.end(); ++pos)
        {
            const SPDBPerk *pPerk = thePerkDatabase.GetPerk(*pos);
            if (pPerk)
                lpStats->AddPerk(&thePerkDatabase, pPerk);
        }
        lpStats->fSpentExp = lpStats->fpExperience;
    }

    lpStats->RecalcInternalHeroStats(1);
    lpStats->HealAll();
    lpStats->RecalcInternalHeroStats(1);

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// CRAP
BOOL CMapObjectManager::WearAll(LPGAMEBASE lpMapObject, LPCSTR pszPrototype)
{
    CSpellManager *lpSpellManager = (CSpellManager *)((CWorld *)GetWorld())->GetSpellManager();
    CItemManager *lpItemManager = (CItemManager *)((CWorld *)GetWorld())->GetItemManager();
    const SUDBUnitPrototype *pPrototype = theUnitDatabase.prototypes[pszPrototype];
    int i;
    //
    if (pPrototype == 0)
    {
        ReportError("Can't find prototype \"%s\".\n", pszPrototype);
        return FALSE;
    }
    // weapons and armors can wear/wield only HUMANS!
    if (lpMapObject->GetTypeID() == MOTID_HUMAN)
    {
        // wear armors
        for (i = 0; i < pPrototype->szWears.size(); i++)
        {
            CItem *lpItem = lpItemManager->CreateItem(pPrototype->szWears[i]);
            if (lpItem)
                ((CUnit *)lpMapObject)->Wear((CItemArmor *)lpItem);
            else
                ReportError("Can't create armor \"%s\" for unit \"%s\".\n", pPrototype->szWears[i],
                            pszPrototype);
        }
        // wield weapon
        if ((pPrototype->szWeapon != "none") && !pPrototype->szWeapon.IsEmpty())
        {
            CItem *lpItem = lpItemManager->CreateItem(pPrototype->szWeapon);
            if (lpItem == 0)
                ReportError("Can't create weapon \"%s\" for unit \"%s\".\n", pPrototype->szWeapon,
                            pszPrototype);
            int nIndex = ((CUnit *)lpMapObject)->AddWeapon((CItemWeapon *)lpItem);
            if (nIndex != -1)
                ((CUnit *)lpMapObject)->WantWeapon(nIndex);
        }
        // add secondary weapon
        if ((pPrototype->szWeapon2 != "none") && !pPrototype->szWeapon2.IsEmpty())
        {
            CItem *lpItem = lpItemManager->CreateItem(pPrototype->szWeapon2);
            if (lpItem == 0)
                ReportError("Can't create weapon \"%s\" for unit \"%s\".\n", pPrototype->szWeapon2,
                            pszPrototype);
            int nIndex = ((CUnit *)lpMapObject)->AddWeapon((CItemWeapon *)lpItem);
        }
    }
    // add qspells
    for (i = 0; i < pPrototype->szSpells.size(); i++)
    {
        CItemSpell *lpItem = lpItemManager->CreateItemSpell(pPrototype->szSpells[i],
                                                            lpSpellManager);
        if (lpItem)
            ((CUnit *)lpMapObject)->AddQSpell(lpItem);
        else
            ReportError("Can't create spell \"%s\" for unit \"%s\".\n", pPrototype->szSpells[i],
                        pszPrototype);
    }

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CMapObjectManager::GetItemsByPrototype(LPCSTR pszPrototype, CStringArray &szWears,
                                            CString &szWeapon, CString &szWeapon2,
                                            CStringArray &szSpells)
{
    const SUDBUnitPrototype *pPrototype = theUnitDatabase.prototypes[pszPrototype];
    int i;

    if (pPrototype == 0)
        return ReportError("Can't find prototype \"%s\".\n", pszPrototype);

    szWears.SetSize(pPrototype->szWears.size());
    for (i = 0; i < szWears.GetSize(); ++i)
        szWears[i] = pPrototype->szWears[i];
    szSpells.SetSize(pPrototype->szSpells.size());
    for (i = 0; i < szSpells.GetSize(); ++i)
        szSpells[i] = pPrototype->szSpells[i];
    szWeapon = pPrototype->szWeapon;
    szWeapon2 = pPrototype->szWeapon2;

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
