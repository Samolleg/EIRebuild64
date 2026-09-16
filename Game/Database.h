#pragma once

#include "BasicChunk.h"

#include <vector>
#include <unordered_map>
#include <string>
#include <functional>
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

public:
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
    saver.AddT(nID, data);
    saver.Finish(file);
    // store in map for the fast search
    for (auto pos = data.begin(); pos != data.end(); ++pos)
        datamap[pos->GetCode()] = &(*pos);
}
