//	Copyright (C) Free Software Foundation
//	Description:	Dynamic Hash Map
//	Author: Free Software Foundation
//  1st modification: Joseph Petviashvili
//  2nd modification: Yuri V. Blazhevich

#pragma once

#include <memory.h>

#include <unordered_map>
/////////////////////////////////////////////////////////////////////////////////////
const char HTS_EMPTY = 0;
const char HTS_VALID = 1;
const char HTS_DELETED = 2;
/////////////////////////////////////////////////////////////////////////////////////
// HashMap с автоматической инкрементацией ключа (обычно он int).
/////////////////////////////////////////////////////////////////////////////////////
// Следующий сниппет:
//
// CHashMap<int, int> map;
// map.Add(100);
// map.Add(200);
// map.SetAt(100, 300);
// map.Add(400);
// map.Add(500);
//
// Создаст вот такую вот unordered_map:
//
// { 1 -> 100 }, { 2 -> 200 }, { 100 -> 300 }, { 101 -> 400 }, { 102 -> 500 }
//
// TODO:
// Теоретически в поисках ключа может зависнуть или не смочь создать его вообще,
// смотри FindFirstFreeKeyInRange.
/////////////////////////////////////////////////////////////////////////////////////
template <class KEY, class VAL>
class CHashMap
{
public:
    typedef KEY *LPKEY;
    typedef VAL *LPVAL;
    typedef std::unordered_map<DWORD, VAL> hmap;

protected:
    std::unordered_map<DWORD, VAL> hashmap;
    KEY kLastLargestKey;

private:
    void Copy(const CHashMap &m);

public:
    CHashMap();
    CHashMap(int s);
    CHashMap(const CHashMap &m);
    ~CHashMap();
    //
    CHashMap &operator=(const CHashMap &m);
    // size changing and querying functions
    void SetHashTableSize(int nNewSize);
    int GetHashTableSize() const { return hashmap.bucket_count(); }
    int GetCount() const { return hashmap.size(); }
    // Observers use bucket iterators only during the current game frame.
    size_t ObserverBucketCount() const noexcept { return hashmap.bucket_count(); }
    auto ObserverBucketBegin(size_t bucket) const { return hashmap.cbegin(bucket); }
    auto ObserverBucketEnd(size_t bucket) const { return hashmap.cend(bucket); }
    BOOL IsEmpty() const { return hashmap.empty(); }
    // adding/removing element. clearing hash table, remove all elements
    void SetAt(const KEY &key, const VAL &val);
    VAL RemoveAt(const KEY &key);
    void RemoveAll();
    void ClearHashTable();
    int Add(const VAL &val);
    // lookup
    BOOL Lookup(const KEY &key, VAL &rValue) const;
    VAL *Seek(const KEY &key) const;
    VAL &SeekNew(const KEY &key);
    // data access
    const VAL *operator()(const KEY &k) const;
    VAL &operator[](const KEY &k);
    //
    typename std::unordered_map<DWORD, VAL>::iterator begin() { return hashmap.begin(); }
    typename std::unordered_map<DWORD, VAL>::iterator end() { return hashmap.end(); }
    typename std::unordered_map<DWORD, VAL>::const_iterator begin() const
    {
        return hashmap.begin();
    }
    typename std::unordered_map<DWORD, VAL>::const_iterator end() const { return hashmap.end(); }
    // key checking
    KEY SetLastLargestKey(KEY key);
    int FindFirstFreeKey() const;
    int FindFirstFreeKeyInRange(KEY from, KEY to) const;

    // for each по HashMap без возможности удаления элемента
    void ForEach(std::function<void(KEY, VAL)> fn) const
    {
        for (auto it = begin(); it != end(); ++it)
            fn(it->first, it->second);
    }
};
/////////////////////////////////////////////////////////////////////////////////////
template <class KEY, class VAL>
class CHashMapIterator
{
    typedef KEY *LPKEY;
    typedef VAL *LPVAL;

private:
    typename std::unordered_map<DWORD, VAL>::iterator pos;
    CHashMap<KEY, VAL> *lpHashMap;

public:
    CHashMapIterator();
    ~CHashMapIterator();

    VAL GetFirst(CHashMap<KEY, VAL> *lpHashMap);
    VAL GetNext();
    KEY GetCurrentElementKey() const;
    bool IsReachEnd() const { return pos == lpHashMap->end(); }
};

#include "HashMap.inl"
