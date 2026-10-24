/*==========================================================================================================

        Network protocol classes implementation

        Written by Ivanov Evgeny
        Nival Interactive, 1998

==========================================================================================================*/
#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "GameBase.h"
#include "NetProtocol.h"
#include "PacketIDs.h"
#include <ctype.h>

// Returns false if string is non-printable or have invalid length
BOOL CheckSymbString(LPCSTR pszName, int nLen, int nMinLen)
{
    LPCSTR pStr = pszName;
    for (int i = 0; i < nLen; i++, pStr++)
    {
        if (*pStr == 0)
            return i >= nMinLen;
        if (!isprint(*pStr))
            return FALSE;
    }
    return TRUE;
}
/*
// -------------------------- Get/Set data functions
-------------------------------------------------------

BOOL GetBASCII( LPSTR pszDstBuf, int nDstBufLen, CBufferManager* pBufMng, int nMinLen )
{
        BYTE cStrSize;
        if ( !pBufMng->ReadData(&cStrSize, sizeof(cStrSize)) )
                return FALSE;
        int nStrSize = int( cStrSize );
        if ( nStrSize > nDstBufLen-1 )
                return FALSE;
        if ( !pBufMng->ReadData(pszDstBuf, nStrSize) )
                return FALSE;
        pszDstBuf[ nStrSize ] = 0;
        if ( nMinLen >= 0 )
                return CheckSymbString( pszDstBuf, nStrSize, nMinLen );
        return TRUE;
}

BOOL GetBASCII( CString& szDstBuf, CBufferManager* pBufMng, int nMinLen )
{
        BYTE buffer[1024];
        BYTE cStrSize;

        if ( !pBufMng->ReadData(&cStrSize, sizeof(cStrSize)) )
                return FALSE;
        int nStrSize = int( cStrSize );
        if ( nStrSize > 1024 )
                return FALSE;
        if ( !pBufMng->ReadData(buffer, nStrSize) )
                return FALSE;
        szDstBuf = buffer;
        if ( nMinLen >= 0 )
                return CheckSymbString( LPCSTR(buffer), nStrSize, nMinLen );

        return TRUE;
}

void SetBASCII( LPVOID& pDstBuf, LPCSTR pszBuf )
{
        LPBYTE pDst = LPBYTE( pDstBuf ) + 1;
        for ( int i = 0; *pszBuf; i++ )
                *pDst++ = *pszBuf++;
        assert( i < 0x100 );
        *( (BYTE*)pDstBuf ) = i;
        pDstBuf = LPBYTE( pDstBuf ) + 1 + i;
}
*/

/////////////////////////////////////////////////////////////////////////////////////
// ------------------------ RADIX sort of LPGAMEBASE array by ID
// -------------------------------------------

inline ObjectId GetID(const CGameBase *object)
{
    return object ? object->GetRTID() : 0;
}
inline void SortOne(int shift, CGameBasePtrArray &src, CGameBasePtrArray &dst)
{
    int counts[256];
    LPGAMEBASE *index[256];
    LPGAMEBASE *addr;
    int i;

    memset(counts, 0, sizeof(counts));
    for (i = 0; i < src.GetSize(); i++)
        counts[(GetID(src[i]) >> shift) & 0xFF]++;
    for (i = 0, addr = dst.GetData(); i < 256; i++)
    {
        index[i] = addr;
        addr += counts[i];
    }
    for (i = 0; i < src.GetSize(); i++)
        *(index[(GetID(src[i]) >> shift) & 0xFF]++) = src[i];
}
/////////////////////////////////////////////////////////////////////////////////////
void SortGameBaseArrayByID(CGameBasePtrArray &toSort)
{
    if (toSort.GetSize() < 2)
        return;
    static CGameBasePtrArray tempSort;
    tempSort.SetSize(toSort.GetSize());

    SortOne(0, toSort, tempSort);
    SortOne(8, tempSort, toSort);
    SortOne(16, toSort, tempSort);
    SortOne(24, tempSort, toSort);
    SortOne(32, toSort, tempSort);
    SortOne(40, tempSort, toSort);
    SortOne(48, toSort, tempSort);
    SortOne(56, tempSort, toSort);
    // erase duplicates
    CGameBase **pCur, **pFinish, **pDst;
    pCur = toSort.GetData();
    pDst = pCur;
    pFinish = pCur + toSort.GetSize();
    *pDst++ = *pCur++;
    while (pCur < pFinish)
    {
        if (GetID(pCur[0]) == GetID(pCur[-1]))
            pCur++;
        else
            *pDst++ = *pCur++;
    }
    toSort.SetSize(pDst - toSort.GetData()); // ARRAY BOUNDS CHECK
}
/////////////////////////////////////////////////////////////////////////////////////
// -------- Select object ID's to add/remove/update, keeping previousIDs updated and sorted
// ----------------
void SelectIDs(CGameBasePtrArray &previousIDs, CGameBasePtrArray &newIDs, CGameBasePtrArray &toAdd,
               CGameBasePtrArray &toRemove, CGameBasePtrArray &toUpdate)
{
    // sort newIDs
    SortGameBaseArrayByID(newIDs);

    // init arrays
    int nMaxSize = Max(newIDs.GetSize(), previousIDs.GetSize());
    toAdd.SetSize(nMaxSize);
    toRemove.SetSize(nMaxSize);
    toUpdate.SetSize(nMaxSize);
    int nAdded = 0, nRemoved = 0, nUpdated = 0;

    LPGAMEBASE *pPrev = previousIDs.GetData();
    LPGAMEBASE *pNew = newIDs.GetData();

    int nPrevLeft = previousIDs.GetSize(), nNewLeft = newIDs.GetSize();
    BOOL bAddOrRem = FALSE;

    for (int i = 0; nPrevLeft && nNewLeft; i++)
    {
        ASSERT((*pNew)->IsValid());
        if (GetID(*pPrev) != GetID(*pNew))
        {
            bAddOrRem = TRUE;
            if (GetID(*pPrev) < GetID(*pNew))
            {
                toRemove[nRemoved++] = *pPrev;
                (*pPrev++)->Release();
                nPrevLeft--;
            }
            else
            {
                toAdd[nAdded++] = *pNew;
                (*pNew++)->AddRef();
                nNewLeft--;
            }
        }
        else
        {
            /*			if ( (*pNew)->IsJustCreated() )
                                    {
                                            toRemove[ nRemoved++ ] = *pPrev;
                                            (*pPrev)->Release();
                                            toAdd[ nAdded++ ] = *pNew;
                                            (*pNew)->AddRef();
                                            bAddOrRem = TRUE;
                                    }
                                    else
                                            if ( (*pNew)->IsUpdated() )*/
            toUpdate[nUpdated++] = *pNew;
            pPrev++;
            nPrevLeft--;
            pNew++;
            nNewLeft--;
        }
    }

    if (bAddOrRem || nPrevLeft || nNewLeft)
    {
        while (nPrevLeft--)
        {
            toRemove[nRemoved++] = *pPrev;
            (*pPrev++)->Release();
        }
        while (nNewLeft--)
        {
            toAdd[nAdded++] = *pNew;
            (*pNew++)->AddRef();
        }
        previousIDs.SetSize(newIDs.GetSize());
        if (newIDs.GetSize())
            memcpy(previousIDs.GetData(), newIDs.GetData(),
                   sizeof(LPGAMEBASE *) * newIDs.GetSize());
    }
    toAdd.SetSize(nAdded);
    toRemove.SetSize(nRemoved);
    toUpdate.SetSize(nUpdated);
}
/////////////////////////////////////////////////////////////////////////////////////
void ReleaseIDs(CGameBasePtrArray &previousIDs)
{
    LPGAMEBASE *pPrev = previousIDs.GetData();
    for (int i = 0; i < previousIDs.GetSize(); i++)
        (*pPrev++)->Release();
    previousIDs.SetSize(0);
}
/////////////////////////////////////////////////////////////////////////////////////
void ReleaseHashMap(GameBaseHashMap &hash)
{
    CHashMapIterator<int, class CGameBase *> iter;
    CGameBase *pThing = iter.GetFirst(&hash);
    while (pThing)
    {
        pThing->Release();
        pThing = iter.GetNext();
    }
    hash.RemoveAll();
}
/////////////////////////////////////////////////////////////////////////////////////
