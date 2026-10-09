#pragma once

#include "BasicChunk.h"

#include <vector>
#include <unordered_map>
#include <string>
#include <functional>
#include <memory>
#include <utility>

// Borrowed Lua records hold only a weak identity, never a vector element.
struct CDatabaseRecordIdentity {};
/////////////////////////////////////////////////////////////////////////////////////
template <class TYPE>
class CCodeNameEqualFunctional
{
private:
    const std::string szCodeName;

public:
    explicit CCodeNameEqualFunctional(const std::string &szNewCodeName)
        : szCodeName(szNewCodeName)
    {
    }
    explicit CCodeNameEqualFunctional(const char *pszNewCodeName)
        : szCodeName(pszNewCodeName)
    {
    }
    bool operator()(const TYPE &entry) { return szCodeName == entry.GetCode(); }
};
/////////////////////////////////////////////////////////////////////////////////////
template <class TYPE>
class CDatabase
{
    typedef std::vector<TYPE> CDataContainer;
    typedef std::unordered_map<std::string, TYPE *> CDataMap;

private:
    CDataContainer data;
    mutable CDataMap datamap;
    mutable std::shared_ptr<const CDatabaseRecordIdentity> recordIdentity;

public:
    CDatabase() = default;
    // Preserve the existing record/map copy semantics, but never share borrow identity.
    CDatabase(const CDatabase &other) : data(other.data), datamap(other.datamap) {}
    CDatabase &operator=(const CDatabase &other)
    {
        if (this != &other)
        {
            recordIdentity.reset();
            data = other.data;
            datamap = other.datamap;
        }
        return *this;
    }
    CDatabase(CDatabase &&) noexcept = default;
    CDatabase &operator=(CDatabase &&other) noexcept
    {
        if (this != &other)
        {
            recordIdentity.reset(); // revoke old borrows before destroying/moving records
            data = std::move(other.data);
            datamap = std::move(other.datamap);
            recordIdentity = std::move(other.recordIdentity);
        }
        return *this;
    }
    std::weak_ptr<const CDatabaseRecordIdentity> GetRecordIdentity() const
    {
        // Allocate only when a real record is borrowed, not during DB construction/Clear.
        if (!recordIdentity) recordIdentity = std::make_shared<const CDatabaseRecordIdentity>();
        return recordIdentity;
    }
    bool OwnsRecordIdentity(const std::weak_ptr<const CDatabaseRecordIdentity> &identity) const
    {
        return recordIdentity && !identity.expired() &&
               !identity.owner_before(recordIdentity) && !recordIdentity.owner_before(identity);
    }

    bool AddEntry(TYPE *pEntry);
    bool RemoveEntry(TYPE *pEntry);
    // access
    const TYPE *operator[](int nIndex) const
    {
        if (nIndex < data.size())
            return &data[nIndex];
        else
            return 0;
    }
    const TYPE *operator[](const std::string &szCode) const;
    const TYPE *operator[](const char *pszCode) const;
    const TYPE *GetEntry(int nIndex) const
    {
        if (nIndex < data.size())
            return &data[nIndex];
        else
            return 0;
    }
    const TYPE *GetEntry(const std::string &szCode) const;
    const TYPE *GetEntry(const char *pszCode) const;
    // relative information
    int GetEntryIndex(const TYPE *pEntry) const { return std::distance(&data[0], pEntry); };
    int GetEntryIndex(const std::string &szCode) const;
    int GetEntryIndex(const char *pszCode) const;
    int GetNumEntries() const { return data.size(); }
    //
    void Load(CSave &file, int nID);
};
/////////////////////////////////////////////////////////////////////////////////////
template <class TYPE>
inline bool CDatabase<TYPE>::AddEntry(TYPE *pEntry)
{
    if (pEntry == 0)
        return false; // can't add '0' entry
    auto pos = datamap.find(pEntry->GetCode());
    if ((pos != datamap.end()) && (pos->second != 0))
        return false; // such entry already exist
    //
    recordIdentity.reset(); // invalidate before a possible vector reallocation
    data.push_back(*pEntry);
    // re-map all values to preserve pointers
    datamap.clear();
    for (auto pos1 = data.begin(); pos1 != data.end(); ++pos1)
        datamap[pos1->GetCode()] = &(*pos1);
    //
    return true;
}
template <class TYPE>
inline bool CDatabase<TYPE>::RemoveEntry(TYPE *pEntry)
{
    if (pEntry == 0)
        return false; // can't remove '0' entry
    auto pos = datamap.find(pEntry->GetCode());
    if (pos == datamap.end())
        return false; // no such entry
    //
    recordIdentity.reset(); // erase moves subsequent vector entries
    data.erase(pos->second);
    datamap.erase(pos);
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
template <class TYPE>
inline const TYPE *CDatabase<TYPE>::operator[](const std::string &szCode) const
{
    return datamap[szCode];
}
template <class TYPE>
inline const TYPE *CDatabase<TYPE>::GetEntry(const std::string &szCode) const
{
    return datamap[szCode];
}
/////////////////////////////////////////////////////////////////////////////////////
template <class TYPE>
inline const TYPE *CDatabase<TYPE>::operator[](const char *pszCode) const
{
    return datamap[pszCode];
}
template <class TYPE>
inline const TYPE *CDatabase<TYPE>::GetEntry(const char *pszCode) const
{
    return datamap[pszCode];
}
/////////////////////////////////////////////////////////////////////////////////////
template <class TYPE>
inline int CDatabase<TYPE>::GetEntryIndex(const std::string &szCode) const
{
    const TYPE *pEntry = GetEntry(szCode);
    if (pEntry)
        return GetEntryIndex(pEntry);
    return -1;
}
/////////////////////////////////////////////////////////////////////////////////////
template <class TYPE>
inline int CDatabase<TYPE>::GetEntryIndex(const char *pszCode) const
{
    const TYPE *pEntry = GetEntry(pszCode);
    if (pEntry)
        return GetEntryIndex(pEntry);
    return -1;
}
/////////////////////////////////////////////////////////////////////////////////////
template <class TYPE>
void CDatabase<TYPE>::Load(CSave &file, int nID)
{
    CStructureSaver saver;

    saver.Start(file, true);
    recordIdentity.reset(); // AddT may clear/resize before deserializing records
    saver.AddT(nID, data);
    saver.Finish(file);
    // store in map for the fast search
    for (auto pos = data.begin(); pos != data.end(); ++pos)
        datamap[pos->GetCode()] = &(*pos);
}
