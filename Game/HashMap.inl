/////////////////////////////////////////////////////////////////////////////////////
// CHashMap<KEY, VAL> inline functions
//////////////////////////////////////////////////////////////////////////////////////
// simply constructor
template <class KEY, class VAL>
inline CHashMap<KEY, VAL>::CHashMap()
{
    kLastLargestKey = KEY(0);
}
/////////////////////////////////////////////////////////////////////////////////////
// 'new size' constructor
template <class KEY, class VAL>
inline CHashMap<KEY, VAL>::CHashMap(int nNewSize)
{
    SetHashTableSize(nNewSize);
}
/////////////////////////////////////////////////////////////////////////////////////
// 'cc' constructor
template <class KEY, class VAL>
inline CHashMap<KEY, VAL>::CHashMap(const CHashMap &m)
{
    Copy(m);
}
/////////////////////////////////////////////////////////////////////////////////////
// destructor
template <class KEY, class VAL>
inline CHashMap<KEY, VAL>::~CHashMap()
{
}
/////////////////////////////////////////////////////////////////////////////////////
// operator= calls 'Copy' and return reference to '*this'
template <class KEY, class VAL>
inline CHashMap<KEY, VAL> &CHashMap<KEY, VAL>::operator=(const CHashMap &m)
{
    this->Copy(m);
    return (*this);
}
/////////////////////////////////////////////////////////////////////////////////////
// clear hash table w/o shrinking size and destroying data structures
template <class KEY, class VAL>
inline void CHashMap<KEY, VAL>::ClearHashTable()
{
    kLastLargestKey = KEY(0);
    hashmap.clear();
}
/////////////////////////////////////////////////////////////////////////////////////
// remove all elements, shrink size to 0, destroy all data structures
template <class KEY, class VAL>
inline void CHashMap<KEY, VAL>::RemoveAll()
{
    hashmap.clear();
    kLastLargestKey = KEY(0);
}
/////////////////////////////////////////////////////////////////////////////////////
// CHashMap<KEY, VAL> out-of-line functions
//////////////////////////////////////////////////////////////////////////////////////
// set hash table size, initialize all data, etc.
template <class KEY, class VAL>
void CHashMap<KEY, VAL>::SetHashTableSize(int nNewSize)
{
    hashmap.rehash(nNewSize);
    //
    kLastLargestKey = KEY(0);
}
/////////////////////////////////////////////////////////////////////////////////////
// copy all data from hash table 'm'
template <class KEY, class VAL>
void CHashMap<KEY, VAL>::Copy(const CHashMap &m)
{
    hashmap = m.hashmap;
    kLastLargestKey = m.kLastLargestKey;
}
/////////////////////////////////////////////////////////////////////////////////////
// add new element 'v' with hash key 'k'
template <class KEY, class VAL>
void CHashMap<KEY, VAL>::SetAt(const KEY &k, const VAL &v)
{
    if (k > kLastLargestKey)
        kLastLargestKey = k;
    hashmap[DWORD(k)] = v;
}
/////////////////////////////////////////////////////////////////////////////////////
// remove element with hash key 'k' from this
template <class KEY, class VAL>
VAL CHashMap<KEY, VAL>::RemoveAt(const KEY &k)
{
    auto pos = hashmap.find(k);
    if (pos == hashmap.end())
        return VAL(0);
    VAL val = pos->second;
    hashmap.erase(pos);
    return val;
}
/////////////////////////////////////////////////////////////////////////////////////
// lookup for element with key 'key'
// return TRUE if success, otherwise - FALSE
template <class KEY, class VAL>
BOOL CHashMap<KEY, VAL>::Lookup(const KEY &key, VAL &rValue) const
{
    auto pos = hashmap.find(DWORD(key));
    if (pos == hashmap.end())
        return FALSE;
    else
    {
        rValue = pos->second;
        return TRUE;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// lookup for element with hash key 'k'
template <class KEY, class VAL>
VAL *CHashMap<KEY, VAL>::Seek(const KEY &key) const
{
    auto pos = hashmap.find(DWORD(key));
    if (pos == hashmap.end())
        return 0;
    else
        return const_cast<VAL *>(&(pos->second));
}

/////////////////////////////////////////////////////////////////////////////////////
// lookup for element with hash key 'k'. If no such element, create one
template <class KEY, class VAL>
VAL &CHashMap<KEY, VAL>::SeekNew(const KEY &key)
{
    if (key > kLastLargestKey)
        kLastLargestKey = key;
    return hashmap[DWORD(key)];
}
/////////////////////////////////////////////////////////////////////////////////////
// get pointer to value at key 'k' (use Seek)
template <class KEY, class VAL>
inline const VAL *CHashMap<KEY, VAL>::operator()(const KEY &k) const
{
    return Seek(k);
}
/////////////////////////////////////////////////////////////////////////////////////
// get value at key 'k' (use SeekNew)
template <class KEY, class VAL>
inline VAL &CHashMap<KEY, VAL>::operator[](const KEY &k)
{
    return SeekNew(k);
}
/////////////////////////////////////////////////////////////////////////////////////
// set last largest key, used in this table
template <class KEY, class VAL>
KEY CHashMap<KEY, VAL>::SetLastLargestKey(KEY key)
{
    KEY kOldLastLargestKey = kLastLargestKey;
    kLastLargestKey = key;
    return kOldLastLargestKey;
}

inline int CoolRandom(int nMax)
{
    return ((rand() << 15) + rand()) % nMax;
}

/////////////////////////////////////////////////////////////////////////////////////
// find first free key in range
template <class KEY, class VAL>
int CHashMap<KEY, VAL>::FindFirstFreeKeyInRange(KEY from, KEY to) const
{
    // first, trying to return 'last largest key + 1'
    if ((kLastLargestKey < to) && (kLastLargestKey >= from))
        return KEY(int(kLastLargestKey) + 1);
    // then, trying random keys 100000 times
    int i;

    for (i = 0; i < 100000; i++)
    {
        KEY key = KEY(from + CoolRandom(to - from));
        if (key == 0)
            continue;
        VAL *pValue = Seek(key);
        if (pValue == NULL)
            return key;
    }
    //
    return -1;
}
/////////////////////////////////////////////////////////////////////////////////////
// find first free key in entire map
template <class KEY, class VAL>
int CHashMap<KEY, VAL>::FindFirstFreeKey() const
{
    return FindFirstFreeKeyInRange(0, 2000000000);
}
/////////////////////////////////////////////////////////////////////////////////////
// add element under first free key
template <class KEY, class VAL>
int CHashMap<KEY, VAL>::Add(const VAL &val)
{
    int key = FindFirstFreeKey();
    if (key == -1)
        return key;
    SetAt(key, val);
    return key;
}
/////////////////////////////////////////////////////////////////////////////////////
//
//
//
// CHashMapIterator
//
//
//
template <class KEY, class VAL>
CHashMapIterator<KEY, VAL>::CHashMapIterator()
{
    lpHashMap = NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
template <class KEY, class VAL>
CHashMapIterator<KEY, VAL>::~CHashMapIterator()
{
    lpHashMap = NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
// get first element from hash map
template <class KEY, class VAL>
VAL CHashMapIterator<KEY, VAL>::GetFirst(CHashMap<KEY, VAL> *lpNewHashMap)
{
    lpHashMap = lpNewHashMap;
    pos = lpHashMap->begin();
    if (IsReachEnd())
        return VAL(0);
    else
        return pos->second;
}
/////////////////////////////////////////////////////////////////////////////////////
// get next element from hash map
template <class KEY, class VAL>
VAL CHashMapIterator<KEY, VAL>::GetNext()
{
    if (IsReachEnd())
        return 0;
    else
    {
        ++pos;
        if (IsReachEnd())
            return VAL(0);
        else
            return pos->second;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// get key value of current element
template <class KEY, class VAL>
KEY CHashMapIterator<KEY, VAL>::GetCurrentElementKey() const
{
    if (IsReachEnd())
        return KEY(-1);
    else
        return KEY(pos->first);
}
/////////////////////////////////////////////////////////////////////////////////////
