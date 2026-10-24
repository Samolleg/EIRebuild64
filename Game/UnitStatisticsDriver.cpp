#include "StdAfx.h"
#include "SaveSchema.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "UnitStatisticsDriver.h"

#include "SaveLoader.h"
#include "Unit.h"
#include "MapObjectServer.h"
#include "ItemDatabase.h"
/////////////////////////////////////////////////////////////////////////////////////
void SUnitWearsStatistics::Store(CSave &file) const
{
    SaveSchema::RequireDynamicSchema();
}
/////////////////////////////////////////////////////////////////////////////////////
void SUnitWearsStatistics::Load(CSave &file)
{
    SaveSchema::RequireDynamicSchema();
}
/////////////////////////////////////////////////////////////////////////////////////
void SUnitStatistics::Store(CSave &file) const
{
    SaveSchema::RequireDynamicSchema();
}
/////////////////////////////////////////////////////////////////////////////////////
void SUnitStatistics::Load(CSave &file)
{
    SaveSchema::RequireDynamicSchema();
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitStatisticsDriver::AddUnit(CUnit *pUnit)
{
    SUnitStatistics entry;
    SComplection comp;
    int i;
    // extract prototype and complection and form keys
    DWORD dwPrototype = static_cast<WORD>(pUnit->GetSubtypeID());
    pUnit->GetComplection(comp);
    DWORD dwKey = ((DWORD(comp.strength * 127.0f) & 0x3FF) << 20) |
                  ((DWORD(comp.grace * 127.0f) & 0x3FF) << 10) |
                  ((DWORD(comp.tall * 127.0f) & 0x3FF));
    // prototype & complection
    entry.wPrototype = static_cast<WORD>(pUnit->GetSubtypeID());
    entry.fStrength = comp.strength;
    entry.fGrace = comp.grace;
    entry.fTall = comp.tall;
    if (pUnit->GetTypeID() == MOTID_HUMAN)
    {
        // armors ...
        // 2nd layer (non-shaped)
        for (i = 0; i < RPG_NUM_ARMOR_TYPES; ++i)
        {
            CItem *pItem = pUnit->GetQArmor(i);
            if (pItem)
            {
                if ((pItem->GetShape() >= theItemDatabase.armors.GetNumEntries()) ||
                    (pItem->GetMaterial() >= theItemDatabase.materials.GetNumEntries()))
                {
                    entry.armors.push_back(-1);
                    ASSERT(0);
                }
                else
                    entry.armors.push_back(pItem->GetSubtypeID());
            }
            else
                entry.armors.push_back(-1);
        }
        // ... and weapons
        for (i = 0; i < QI_MAX_WEAPONS; ++i)
        {
            CItem *pItem = pUnit->GetQWeapon(i);
            if (pItem)
            {
                if ((pItem->GetShape() >= theItemDatabase.weapons.GetNumEntries()) ||
                    (pItem->GetMaterial() >= theItemDatabase.materials.GetNumEntries()))
                {
                    entry.weapons.push_back(-1);
                    ASSERT(0);
                }
                else
                    entry.weapons.push_back(pItem->GetSubtypeID());
            }
            else
                entry.weapons.push_back(-1);
        }
        entry.cCurrentWeaponIndex = pUnit->GetCurrentWeaponSlotIndex();
    }
    else
        entry.cCurrentWeaponIndex = -1;
    // check range for armors and weapons
    std::pair<CUnitStatisticsMap::iterator, CUnitStatisticsMap::iterator> range =
        units[dwPrototype].equal_range(dwKey);
    bool bNeedNewEntry = true;
    for (auto pos = range.first; pos != range.second; ++pos)
    {
        if ((pos->second.armors == entry.armors) && (pos->second.weapons == entry.weapons))
        {
            bNeedNewEntry = false;
            pos->second.nID = -1;
            break;
        }
    }
    // new entry...
    if (bNeedNewEntry)
    {
        // bodyparts (for non-humans only)
        if (pUnit->GetTypeID() != MOTID_HUMAN)
        {
            CStringArray &szBPs = ((CMapObjectServer *)pUnit->GetMapObjectSpecific())->szNames;
            for (i = 0; i < szBPs.GetSize(); ++i)
                entry.szBPs.push_back(LPCSTR(szBPs[i]));
        }
        // push new entry
        entry.nID = pUnit->GetID();
        units[dwPrototype].insert(CUnitStatisticsMap::value_type(dwKey, entry));
    }
    // find such in 'wears'
    bNeedNewEntry = true;
    std::pair<CUnitWearsMap::iterator, CUnitWearsMap::iterator> wear = wears.equal_range(
        dwPrototype);
    for (auto it = wear.first; it != wear.second; ++it)
    {
        if ((it->second.armors == entry.armors) && (it->second.weapons == entry.weapons))
        {
            bNeedNewEntry = false;
            it->second.nID = -1;
            break;
        }
    }
    if (bNeedNewEntry)
    {
        entry.nID = pUnit->GetID();
        wears.insert(CUnitWearsMap::value_type(dwPrototype, entry));
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitStatisticsDriver::Store(CSave &file) const
{
    SaveSchema::RequireDynamicSchema();
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitStatisticsDriver::Load(CSave &file, std::vector<SUnitStatistics> &statistics,
                                 std::vector<SUnitWearsStatistics> &wears)
{
    SaveSchema::RequireDynamicSchema();
}
/////////////////////////////////////////////////////////////////////////////////////
