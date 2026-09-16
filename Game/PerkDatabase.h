#pragma once

#include "pdbFormat.h"

#include <vector>
/////////////////////////////////////////////////////////////////////////////////////
class CPerkDatabase
{
private:
    std::vector<SPDBSkill> skills;
    std::vector<SPDBPerk> perks;

public:
    const SPDBSkill *GetSkill(int nIndex) const
    {
        if (nIndex < skills.size())
            return &skills[nIndex];
        else
            return 0;
    }
    const SPDBSkill *GetSkill(LPCSTR pszCode) const;
    int GetSkillIndex(LPCSTR pszCode) const;
    int GetSkillIndex(const SPDBSkill *pSkill) const;
    int GetNumSkills() const { return skills.size(); }

    const SPDBPerk *GetPerk(int nIndex) const
    {
        if (nIndex < perks.size())
            return &perks[nIndex];
        else
            return 0;
    }
    const SPDBPerk *GetPerk(LPCSTR pszCode) const;
    int GetPerkIndex(LPCSTR pszCode) const;
    int GetPerkIndex(const SPDBPerk *pPerk) const;
    int GetNumPerks() const { return perks.size(); }
    // save/load service
    BOOL SaveDatabase(LPCSTR pszPath);
    BOOL LoadDatabase(LPCSTR pszPath, LPCSTR pszFName);
    void Clear()
    {
        CPerkDatabase::~CPerkDatabase();
        ::new (this) CPerkDatabase;
    }
};
/////////////////////////////////////////////////////////////////////////////////////
extern CPerkDatabase thePerkDatabase;
