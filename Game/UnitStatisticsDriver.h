#pragma once
namespace SaveSchema { class Writer; class Reader; }
/////////////////////////////////////////////////////////////////////////////////////
#include <unordered_map>
#include <vector>
/////////////////////////////////////////////////////////////////////////////////////
class CUnit;
class CSave;
/////////////////////////////////////////////////////////////////////////////////////
// statistics for unit wears to create shared textures
struct SUnitWearsStatistics
{
public:
    WORD wPrototype;
    std::vector<DWORD> armors, weapons;
    char cCurrentWeaponIndex;
    int nID;

public:
    void Store(CSave &file) const;
    void Load(CSave &file);
};
// statistics for unit complections and bodyparts
struct SUnitStatistics : public SUnitWearsStatistics
{
public:
    float fStrength, fGrace, fTall;
    std::vector<CString> szBPs;

public:
    void Store(CSave &file) const;
    void Load(CSave &file);
};
/////////////////////////////////////////////////////////////////////////////////////
class CUnitStatisticsDriver
{
    typedef std::unordered_multimap<DWORD, SUnitWearsStatistics> CUnitWearsMap;
    typedef std::unordered_multimap<DWORD, SUnitStatistics> CUnitStatisticsMap;
    typedef std::unordered_map<DWORD, CUnitStatisticsMap> CUnitsMap;

private:
    CUnitsMap units;
    CUnitWearsMap wears;

public:
    void AddUnit(CUnit *pUnit);
    //
    void Store(CSave &file) const;
    //
    static void Load(CSave &file, std::vector<SUnitStatistics> &statistics,
                     std::vector<SUnitWearsStatistics> &wears);
public:
    void Store(SaveSchema::Writer &) const;
    static void Load(SaveSchema::Reader &, std::vector<SUnitStatistics> &, std::vector<SUnitWearsStatistics> &);
};
/////////////////////////////////////////////////////////////////////////////////////
