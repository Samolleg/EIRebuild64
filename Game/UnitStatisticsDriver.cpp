#include "StdAfx.h"
#include "SaveSchema.h"
#include "SaveLoader.h"
#include "UnitStatisticsDriver.h"
static const char LOCAL_FILE[] = __FILE__;

namespace {
using namespace SaveSchema;
template<class T> struct LocalPrecacheCodec { static_assert(!std::is_same_v<T, T>, "Save64: missing local precache codec for T"); };
template<> struct LocalPrecacheCodec<SUnitWearsStatistics>
{
    static constexpr std::uint32_t Id = 0x17c1;
    static ValueType Get() { return {
        [](Writer &w,const void *p){const auto &v=*static_cast<const SUnitWearsStatistics *>(p);
            w.UInt16(v.wPrototype);w.Count(static_cast<UINT>(v.armors.size()));for(auto id:v.armors)w.UInt32(id);
            w.Count(static_cast<UINT>(v.weapons.size()));for(auto id:v.weapons)w.UInt32(id);
            w.UInt8(static_cast<unsigned char>(v.cCurrentWeaponIndex));w.Int32(v.nID);},
        [](Reader &r,void *p){auto &v=*static_cast<SUnitWearsStatistics *>(p);
            v.wPrototype=r.UInt16();v.armors.resize(r.Count());for(auto &id:v.armors)id=r.UInt32();
            v.weapons.resize(r.Count());for(auto &id:v.weapons)id=r.UInt32();
            v.cCurrentWeaponIndex=static_cast<char>(r.UInt8());v.nID=r.Int32();}}; }
};
template<> struct LocalPrecacheCodec<SUnitStatistics>
{
    static constexpr std::uint32_t Id = 0x17c2;
    static ValueType Get() { return {
        [](Writer &w,const void *p){const auto &v=*static_cast<const SUnitStatistics *>(p);
            w.Value<SUnitWearsStatistics, LocalPrecacheCodec<SUnitWearsStatistics>>(static_cast<const SUnitWearsStatistics &>(v));w.Float32(v.fStrength);w.Float32(v.fGrace);w.Float32(v.fTall);
            w.Count(static_cast<UINT>(v.szBPs.size()));for(const auto &name:v.szBPs)w.String((LPCSTR)name);},
        [](Reader &r,void *p){auto &v=*static_cast<SUnitStatistics *>(p);
            r.Value<SUnitWearsStatistics, LocalPrecacheCodec<SUnitWearsStatistics>>(static_cast<SUnitWearsStatistics &>(v));v.fStrength=r.Float32();v.fGrace=r.Float32();v.fTall=r.Float32();
            v.szBPs.resize(r.Count());for(auto &name:v.szBPs)name=r.String().c_str();}}; }
};
template<class F> void WriteLocal(CSave &file,F root)
{
    CMemorySave f;SaveSchema::Registry types;SaveSchema::Store(f,types,root);file.Write(f);
}
template<class F> void ReadLocal(CSave &file,F root)
{
    CMemorySave f;file.Read(f);f.Seek(0);SaveSchema::Registry types;auto graph=SaveSchema::Load(f,types,root);
}
}
/////////////////////////////////////////////////////////////////////////////////////
#include "UnitStatisticsDriver.h"

#include "SaveLoader.h"
#include "Unit.h"
#include "MapObjectServer.h"
#include "ItemDatabase.h"
/////////////////////////////////////////////////////////////////////////////////////
void SUnitWearsStatistics::Store(CSave &file) const
{
    WriteLocal(file,[&](SaveSchema::Writer &w){w.Value<SUnitWearsStatistics, LocalPrecacheCodec<SUnitWearsStatistics>>(*this);});
}
/////////////////////////////////////////////////////////////////////////////////////
void SUnitWearsStatistics::Load(CSave &file)
{
    ReadLocal(file,[&](SaveSchema::Reader &r){r.Value<SUnitWearsStatistics, LocalPrecacheCodec<SUnitWearsStatistics>>(*this);});
}
/////////////////////////////////////////////////////////////////////////////////////
void SUnitStatistics::Store(CSave &file) const
{
    WriteLocal(file,[&](SaveSchema::Writer &w){w.Value<SUnitStatistics, LocalPrecacheCodec<SUnitStatistics>>(*this);});
}
/////////////////////////////////////////////////////////////////////////////////////
void SUnitStatistics::Load(CSave &file)
{
    ReadLocal(file,[&](SaveSchema::Reader &r){r.Value<SUnitStatistics, LocalPrecacheCodec<SUnitStatistics>>(*this);});
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
    WriteLocal(file,[&](SaveSchema::Writer &w){Store(w);});
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitStatisticsDriver::Load(CSave &file, std::vector<SUnitStatistics> &statistics,
                                 std::vector<SUnitWearsStatistics> &wears)
{
    ReadLocal(file,[&](SaveSchema::Reader &r){Load(r,statistics,wears);});
}
/////////////////////////////////////////////////////////////////////////////////////
