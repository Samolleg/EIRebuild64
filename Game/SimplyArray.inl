/////////////////////////////////////////////////////////////////////////////////////
#include "mm_malloc.h"
/////////////////////////////////////////////////////////////////////////////////////
// CSimplyArray<TYPE, ARG_TYPE> inline functions
template <class TYPE, class ARG_TYPE>
inline int CSimplyArray<TYPE, ARG_TYPE>::GetSize() const
{
    return m_nSize;
}
/////////////////////////////////////////////////////////////////////////////////////
template <class TYPE, class ARG_TYPE>
inline int CSimplyArray<TYPE, ARG_TYPE>::GetUpperBound() const
{
    return m_nSize - 1;
}
/////////////////////////////////////////////////////////////////////////////////////
template <class TYPE, class ARG_TYPE>
inline void CSimplyArray<TYPE, ARG_TYPE>::RemoveAll()
{
    SetSize(0, -1);
}
/////////////////////////////////////////////////////////////////////////////////////
template <class TYPE, class ARG_TYPE>
inline const TYPE &CSimplyArray<TYPE, ARG_TYPE>::GetAt(int nIndex) const
{
#if defined(__NOMFC__)
    assert((nIndex >= 0) && (nIndex < m_nSize));
#else
    ASSERT((nIndex >= 0) && (nIndex < m_nSize));
#endif // #if defined(__NOMFC__)
    return m_pData[nIndex];
}
/////////////////////////////////////////////////////////////////////////////////////
template <class TYPE, class ARG_TYPE>
inline void CSimplyArray<TYPE, ARG_TYPE>::SetAt(int nIndex, ARG_TYPE newElement)
{
#if defined(__NOMFC__)
    assert((nIndex >= 0) && (nIndex < m_nSize));
#else
    ASSERT((nIndex >= 0) && (nIndex < m_nSize));
#endif // #if defined(__NOMFC__)
    m_pData[nIndex] = newElement;
}
/////////////////////////////////////////////////////////////////////////////////////
template <class TYPE, class ARG_TYPE>
inline TYPE &CSimplyArray<TYPE, ARG_TYPE>::ElementAt(int nIndex)
{
#if defined(__NOMFC__)
    assert((nIndex >= 0) && (nIndex < m_nSize));
#else
    ASSERT((nIndex >= 0) && (nIndex < m_nSize));
#endif // #if defined(__NOMFC__)
    return m_pData[nIndex];
}
/////////////////////////////////////////////////////////////////////////////////////
template <class TYPE, class ARG_TYPE>
inline const TYPE *CSimplyArray<TYPE, ARG_TYPE>::GetData() const
{
    return (const TYPE *)m_pData;
}
/////////////////////////////////////////////////////////////////////////////////////
template <class TYPE, class ARG_TYPE>
inline TYPE *CSimplyArray<TYPE, ARG_TYPE>::GetData()
{
    return (TYPE *)m_pData;
}
/////////////////////////////////////////////////////////////////////////////////////
template <class TYPE, class ARG_TYPE>
inline int CSimplyArray<TYPE, ARG_TYPE>::Add(ARG_TYPE newElement)
{
    int nIndex = m_nSize;
    SetAtGrow(nIndex, newElement);
    return nIndex;
}
/////////////////////////////////////////////////////////////////////////////////////
template <class TYPE, class ARG_TYPE>
inline const TYPE &CSimplyArray<TYPE, ARG_TYPE>::operator[](int nIndex) const
{
    return GetAt(nIndex);
}
/////////////////////////////////////////////////////////////////////////////////////
template <class TYPE, class ARG_TYPE>
inline TYPE &CSimplyArray<TYPE, ARG_TYPE>::operator[](int nIndex)
{
    return ElementAt(nIndex);
}
/////////////////////////////////////////////////////////////////////////////////////
// CSimplyArray<TYPE, ARG_TYPE> out-of-line functions
template <class TYPE, class ARG_TYPE>
CSimplyArray<TYPE, ARG_TYPE>::CSimplyArray()
{
    m_pData = NULL;
    m_nSize = m_nMaxSize = m_nGrowBy = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
template <class TYPE, class ARG_TYPE>
CSimplyArray<TYPE, ARG_TYPE>::~CSimplyArray()
{
    if (m_pData != NULL)
        mm_free(m_pData); // delete []m_pData;
}
/////////////////////////////////////////////////////////////////////////////////////
template <class TYPE, class ARG_TYPE>
void CSimplyArray<TYPE, ARG_TYPE>::SetSize(int nNewSize, int nGrowBy)
{
#if defined(__NOMFC__)
    assert(nNewSize >= 0);
#else
    ASSERT(nNewSize >= 0);
#endif // #if defined(__NOMFC__)

    if (nGrowBy != -1)
        m_nGrowBy = nGrowBy; // set new size

    if (nNewSize == 0)
    {
        // shrink to nothing
        if (m_pData != NULL)
        {
            mm_free(m_pData); // delete []m_pData;
            m_pData = NULL;
        }
        m_nSize = m_nMaxSize = 0;
    }
    else if (m_pData == NULL)
    {
        // create one with exact size
        m_pData = mm_malloc<TYPE>(nNewSize); // new TYPE[nNewSize];
        //		memset( m_pData, 0, nNewSize*sizeof(TYPE) );
        m_nSize = m_nMaxSize = nNewSize;
    }
    else if (nNewSize <= m_nMaxSize)
    {
        // it fits
        // #ifdef _DEBUG
        //		if ( nNewSize > m_nSize )
        //			memset( &m_pData[m_nSize], 0, nNewSize - m_nSize );
        // #endif // _DEBUG
        m_nSize = nNewSize;
    }
    else
    {
        // otherwise, grow array
        int nGrowBy = m_nGrowBy;
        if (nGrowBy == 0)
        {
            // heuristically determine growth when nGrowBy == 0
            //  (this avoids heap fragmentation in many situations)
            nGrowBy = m_nSize / 8;
            nGrowBy = (nGrowBy < 4) ? 4 : ((nGrowBy > 1024) ? 1024 : nGrowBy);
        }
        int nNewMax;
        if (nNewSize < m_nMaxSize + nGrowBy)
            nNewMax = m_nMaxSize + nGrowBy; // granularity
        else
            nNewMax = nNewSize; // no slush

#if defined(__NOMFC__)
        assert(nNewMax >= m_nMaxSize); // no wrap around
#else
        ASSERT(nNewMax >= m_nMaxSize); // no wrap around
#endif                                             // #if defined(__NOMFC__)
        TYPE *pNewData = mm_malloc<TYPE>(nNewMax); // new TYPE[nNewMax];
        // copy new data from old
        memcpy(pNewData, m_pData, m_nSize * sizeof(TYPE));
        // construct remaining elements
#if defined(__NOMFC__)
        assert(nNewSize > m_nSize);
#else
        ASSERT(nNewSize > m_nSize);
#endif                    // #if defined(__NOMFC__)
                          //		memset( &pNewData[m_nSize], 0, nNewSize - m_nSize );
                          // get rid of old stuff
        mm_free(m_pData); // delete []m_pData;
        m_pData = pNewData;
        m_nSize = nNewSize;
        m_nMaxSize = nNewMax;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
template <class TYPE, class ARG_TYPE>
void CSimplyArray<TYPE, ARG_TYPE>::SetSizeNS(int nNewSize, int nGrowBy)
{
#if defined(__NOMFC__)
    assert(nNewSize >= 0);
#else
    ASSERT(nNewSize >= 0);
#endif // #if defined(__NOMFC__)

    if (nGrowBy != -1)
        m_nGrowBy = nGrowBy; // set new size

    if (m_pData == NULL)
    {
        // create one with exact size
        m_pData = mm_malloc<TYPE>(nNewSize); // new TYPE[nNewSize];
        //		memset( m_pData, 0, nNewSize*sizeof(TYPE) );
        m_nSize = m_nMaxSize = nNewSize;
    }
    else if (nNewSize <= m_nMaxSize)
    {
        // it fits
        // #ifdef _DEBUG
        //		if ( nNewSize > m_nSize )
        //			memset( &m_pData[m_nSize], 0, nNewSize - m_nSize );
        // #endif // _DEBUG
        m_nSize = nNewSize;
    }
    else
    {
        // otherwise, grow array
        int nGrowBy = m_nGrowBy;
        if (nGrowBy == 0)
        {
            // heuristically determine growth when nGrowBy == 0
            //  (this avoids heap fragmentation in many situations)
            nGrowBy = m_nSize / 8;
            nGrowBy = (nGrowBy < 4) ? 4 : ((nGrowBy > 1024) ? 1024 : nGrowBy);
        }
        int nNewMax;
        if (nNewSize < m_nMaxSize + nGrowBy)
            nNewMax = m_nMaxSize + nGrowBy; // granularity
        else
            nNewMax = nNewSize; // no slush

#if defined(__NOMFC__)
        assert(nNewMax >= m_nMaxSize); // no wrap around
#else
        ASSERT(nNewMax >= m_nMaxSize); // no wrap around
#endif                                             // #if defined(__NOMFC__)
        TYPE *pNewData = mm_malloc<TYPE>(nNewMax); // new TYPE[nNewMax];
                                                   // construct remaining elements
#if defined(__NOMFC__)
        assert(nNewSize > m_nSize);
#else
        ASSERT(nNewSize > m_nSize);
#endif                    // #if defined(__NOMFC__)
                          //		memset( &pNewData[m_nSize], 0, nNewSize - m_nSize );
                          // get rid of old stuff
        mm_free(m_pData); // delete []m_pData;
        m_pData = pNewData;
        m_nSize = nNewSize;
        m_nMaxSize = nNewMax;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
template <class TYPE, class ARG_TYPE>
int CSimplyArray<TYPE, ARG_TYPE>::Append(const CSimplyArray &src)
{
#if defined(__NOMFC__)
    assert(this != &src); // cannot append to itself
#else
    ASSERT(this != &src); // cannot append to itself
#endif // #if defined(__NOMFC__)

    int nOldSize = m_nSize;
    SetSize(m_nSize + src.m_nSize);
    memcpy(m_pData + nOldSize, src.m_pData, src.m_nSize * sizeof(TYPE));
    return nOldSize;
}
/////////////////////////////////////////////////////////////////////////////////////
template <class TYPE, class ARG_TYPE>
void CSimplyArray<TYPE, ARG_TYPE>::Copy(const CSimplyArray &src)
{
#if defined(__NOMFC__)
    assert(this != &src); // cannot append to itself
#else
    ASSERT(this != &src); // cannot append to itself
#endif // #if defined(__NOMFC__)

    SetSize(src.m_nSize);
    memcpy(m_pData, src.m_pData, src.m_nSize * sizeof(TYPE));
}
/////////////////////////////////////////////////////////////////////////////////////
template <class TYPE, class ARG_TYPE>
void CSimplyArray<TYPE, ARG_TYPE>::FreeExtra()
{
    if (m_nSize != m_nMaxSize)
    {
        // shrink to desired size
        TYPE *pNewData = NULL;
        if (m_nSize != 0)
        {
            pNewData = mm_malloc<TYPE>(m_nSize); // new TYPE[m_nSize];
            // copy new data from old
            memcpy(pNewData, m_pData, m_nSize * sizeof(TYPE));
        }

        // get rid of old stuff (note: no destructors called)
        mm_free(m_pData); // delete []m_pData;
        m_pData = pNewData;
        m_nMaxSize = m_nSize;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
template <class TYPE, class ARG_TYPE>
void CSimplyArray<TYPE, ARG_TYPE>::SetAtGrow(int nIndex, ARG_TYPE newElement)
{
#if defined(__NOMFC__)
    assert(nIndex >= 0);
#else
    ASSERT(nIndex >= 0);
#endif // #if defined(__NOMFC__)

    if (nIndex >= m_nSize)
        SetSize(nIndex + 1, -1);
    m_pData[nIndex] = newElement;
}
/////////////////////////////////////////////////////////////////////////////////////
template <class TYPE, class ARG_TYPE>
void CSimplyArray<TYPE, ARG_TYPE>::InsertAt(int nIndex, ARG_TYPE newElement, int nCount /*=1*/)
{
#if defined(__NOMFC__)
    assert(nIndex >= 0); // will expand to meet need
    assert(nCount > 0);  // zero or negative size not allowed
#else
    ASSERT(nIndex >= 0); // will expand to meet need
    ASSERT(nCount > 0);  // zero or negative size not allowed
#endif // #if defined(__NOMFC__)

    if (nIndex >= m_nSize)
    {
        // adding after the end of the array
        SetSize(nIndex + nCount, -1); // grow so nIndex is valid
    }
    else
    {
        // inserting in the middle of the array
        int nOldSize = m_nSize;
        SetSize(m_nSize + nCount, -1); // grow it to new size
        // shift old data up to fill gap
        memmove(&m_pData[nIndex + nCount], &m_pData[nIndex], (nOldSize - nIndex) * sizeof(TYPE));
        // re-init slots we copied from
        //		memset( &m_pData[nIndex], 0, nCount*sizeof(TYPE) );
    }

    // insert new value in the gap
#if defined(__NOMFC__)
    assert(nIndex + nCount <= m_nSize);
#else
    ASSERT(nIndex + nCount <= m_nSize);
#endif // #if defined(__NOMFC__)

    while (nCount--)
        m_pData[nIndex++] = newElement;
}
/////////////////////////////////////////////////////////////////////////////////////
template <class TYPE, class ARG_TYPE>
void CSimplyArray<TYPE, ARG_TYPE>::RemoveAt(int nIndex, int nCount)
{
#if defined(__NOMFC__)
    assert(nIndex >= 0);
    assert(nCount >= 0);
    assert(nIndex + nCount <= m_nSize);
#else
    ASSERT(nIndex >= 0);
    ASSERT(nCount >= 0);
    ASSERT(nIndex + nCount <= m_nSize);
#endif // #if defined(__NOMFC__)

    // just remove a range
    int nMoveCount = m_nSize - (nIndex + nCount);

    if (nMoveCount)
        memcpy(&m_pData[nIndex], &m_pData[nIndex + nCount], nMoveCount * sizeof(TYPE));
    m_nSize -= nCount;
}
/////////////////////////////////////////////////////////////////////////////////////
template <class TYPE, class ARG_TYPE>
void CSimplyArray<TYPE, ARG_TYPE>::InsertAt(int nStartIndex, CSimplyArray *pNewArray)
{
#if defined(__NOMFC__)
    assert(pNewArray != NULL);
    assert(nStartIndex >= 0);
#else
    ASSERT(pNewArray != NULL);
    ASSERT(nStartIndex >= 0);
#endif // #if defined(__NOMFC__)

    if (pNewArray->GetSize() > 0)
    {
        InsertAt(nStartIndex, pNewArray->GetAt(0), pNewArray->GetSize());
        for (int i = 0; i < pNewArray->GetSize(); i++)
            SetAt(nStartIndex + i, pNewArray->GetAt(i));
    }
}
/////////////////////////////////////////////////////////////////////////////////////
