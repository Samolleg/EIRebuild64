// MEUnitStats.cpp: implementation of the CMEUnitStats class.
//
////////////////////////////////////////////////////////////////////////////////////////////////
#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "MEUnitStats.h"
#include "RPGSystem.h"
#include "Item.h"
#include "ItemManager.h"
#include "SpellManager.h"
#include "Unit.h"
#include "MapObjectServer.h"
#include "UnitServer.h"
#include "WorldServer.h"

CString GetSpellCode(CSpell *pSpell)
{
    CString szCode;
    //	CStringArray aSpellRune;
    LPCSTR pRunes = pSpell->GetRunes();
    LPCSTR pName = pSpell->GetPrototype()->GetCodeName();
    szCode = pName;
    if (pSpell->GetRunesCount() > 0)
    {
        szCode += '{';
        int nCount = pSpell->GetRunesCount();
        for (int i = 0; i < nCount; i++)
        {
            szCode += theWorldServer.GetSpellManager()->GetModifier(pRunes[i] - 1)->GetCodeName();
            if (i != nCount - 1)
                szCode += ',';
        }
        szCode += '}';
    }
    return szCode;
}
////////////////////////////////////////////////////////////////////////////////////////////////
void CMEUnitSpecific::Import(CUnit *pUnit)
{
    CItemManager &itemManager = *theWorldServer.GetItemManager();
    int i;

    if ((stats.move == 0 && stats.nMaxHP == 0 && stats.action == 0) || !needImport)
    {
        for (i = 0; i < aQuickItems.GetSize(); i++)
            pUnit->AddQItem(static_cast<CItemQuick *>(itemManager.CreateItem(aQuickItems[i])));

        for (i = 0; i < aQuestItems.GetSize(); i++)
            pUnit->AddQuestItem(static_cast<CItemQuest *>(itemManager.CreateItem(aQuestItems[i])));
        return;
    }

    if (needImport)
    {
        pUnit->RemoveAllItems();
        SRPGCommonStats *pCommonStats =
            static_cast<CMapObjectServer *>(pUnit->GetMapObjectSpecific())->GetRPGStats();
        SRPGUnitStats *pUnitStats = static_cast<CUnitServer *>(pUnit->GetUnitSpecific())
                                        ->GetRPGStats();

        pCommonStats->ssHP.fpCurrent = stats.nHP;
        pCommonStats->ssHP.fpMax = stats.nMaxHP;
        pCommonStats->ssMP.fpCurrent = stats.nMP;
        pCommonStats->ssMP.fpMax = stats.nMaxMP;
        // redistribute new HP to the hit locations and completelly heal it
        pUnitStats->DistributeHP(pCommonStats->ssHP.fpMax);
        pUnitStats->HealAll();
        pUnitStats->fBaseHP = pCommonStats->ssHP.fpMax;
        // secondary attributes
        pUnitStats->move.fpEffectiveValue = stats.move;
        pUnitStats->action.fpEffectiveValue = stats.action;
        pUnitStats->fpSpeeds[0] = stats.fpSpeeds[0];
        pUnitStats->fpSpeeds[1] = stats.fpSpeeds[1];
        pUnitStats->fpSpeeds[2] = stats.fpSpeeds[2];
        pUnitStats->fpSpeeds[3] = stats.fpSpeeds[3];
        pUnitStats->fpVisionArc = stats.fpVisionArc;
        pUnitStats->fpPeripheral = stats.fpPeripheral;
        pUnitStats->fpPeripheralArc = stats.fpPeripheralArc;
        //
        pUnitStats->fpAttackDistance = stats.fpAttackDistance;
        pUnitStats->wAIClassStay = stats.dwAIClassStay;
        pUnitStats->wAIClassLie = stats.dwAIClassLie;
        pUnitStats->wAIClassCrawl = pUnitStats->wAIClassLie;
        // Native attack weapon (NAW) params // Monsters only
        pUnitStats->fpNAWRange = stats.fpNAWRange;
        pUnitStats->fpNAWToHit = stats.fpNAWToHit;
        pUnitStats->fpNAWParry = stats.fpNAWParry;
        pUnitStats->fpNAWWeight = stats.fpNAWWeight;
        pUnitStats->fpNAWDamageMin = stats.fpNAWDamageMin;
        pUnitStats->fpNAWDamageRnd = stats.fpNAWDamageRnd;
        memmove(pUnitStats->fpNAWAttacks, stats.fpNAWAttacks,
                ARRAY_SIZE(stats.fpNAWAttacks) * sizeof(float));

        for (i = 0; i < RPG_NUM_ATTACK_TYPES; ++i)
            pCommonStats->psAbsorbtions[i].fpEffectiveValue =
                pCommonStats->psAbsorbtions[i].fpBaseValue = pUnit->GetRaceModel()->defences[i] *
                                                             stats.nAbsorbtion;

        for (i = 0; i < RPG_NUM_SENSES; ++i)
            pUnitStats->senses[i].fpEffectiveValue = pUnitStats->senses[i]
                                                         .fpBaseValue = stats.senses[i];

        for (i = 0; i < RPG_NUM_SENSES; i++)
            pUnitStats->percepts[i].fpEffectiveValue = pUnitStats->percepts[i]
                                                           .fpBaseValue = stats.percepts[i];

        memmove(pUnitStats->cManualSkills, stats.cManualSkills, ARRAY_SIZE(stats.cManualSkills));

        for (i = 0; i < aSpells.GetSize(); i++)
            pUnit->AddQSpell(static_cast<CItemSpell *>(
                itemManager.CreateItemSpell(aSpells[i], theWorldServer.GetSpellManager())));

        for (i = 0; i < aQuickItems.GetSize(); i++)
            pUnit->AddQItem(static_cast<CItemQuick *>(itemManager.CreateItem(aQuickItems[i])));

        for (i = 0; i < aQuestItems.GetSize(); i++)
            pUnit->AddQuestItem(static_cast<CItemQuest *>(itemManager.CreateItem(aQuestItems[i])));

        for (i = 0; i < aArmors.GetSize(); i++)
            pUnit->Wear(static_cast<CItemArmor *>(itemManager.CreateItem(aArmors[i])));

        for (i = 0; i < aWeapons.GetSize(); i++)
            pUnit->AddWeapon(static_cast<CItemWeapon *>(itemManager.CreateItem(aWeapons[i])));
    }
}
////////////////////////////////////////////////////////////////////////////////////////////////
