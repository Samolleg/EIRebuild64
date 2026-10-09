#pragma once

#include "pdbFormat.h"

#include <vector>
#include <memory>
#include <utility>
/////////////////////////////////////////////////////////////////////////////////////
class CPerkDatabase
{
public:
    // Identity follows the actual vector owner, never a Lua callback or cached record.
    struct RecordIdentity {};

private:
    std::vector<SPDBSkill> skills;
    std::vector<SPDBPerk> perks;
    std::shared_ptr<const RecordIdentity> recordIdentity = std::make_shared<RecordIdentity>();

public:
    CPerkDatabase() = default;
    CPerkDatabase(const CPerkDatabase &other) : skills(other.skills), perks(other.perks) {}
    CPerkDatabase &operator=(const CPerkDatabase &other)
    {
        if (this != &other)
        {
            // A copy is a different record owner even when the contents are equal.
            recordIdentity = std::make_shared<RecordIdentity>();
            skills = other.skills;
            perks = other.perks;
        }
        return *this;
    }
    CPerkDatabase(CPerkDatabase &&) noexcept = default;
    CPerkDatabase &operator=(CPerkDatabase &&) noexcept = default;
    std::weak_ptr<const RecordIdentity> GetRecordIdentity() const { return recordIdentity; }
    bool OwnsRecordIdentity(const std::weak_ptr<const RecordIdentity> &identity) const
    {
        return !identity.expired() && !identity.owner_before(recordIdentity) &&
               !recordIdentity.owner_before(identity);
    }
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
        // Prepare the empty identity before changing this live database.
        CPerkDatabase empty;
        *this = std::move(empty); // Existing noexcept move transfers vectors and identity.
    }
};
/////////////////////////////////////////////////////////////////////////////////////
extern CPerkDatabase thePerkDatabase;
