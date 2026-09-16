/**********************************************************************
 *<
        FILE: SaveLoader.cpp

        DESCRIPTION:  implementation of save load basic classes

        CREATED BY: Andy Gulin

 *>	Copyright (c) 1999, Nival Enterntainemnt
 **********************************************************************/

#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "SaveLoader.h"

/////////////////////////////////////////////////////////////////////////////////////
// CSave implementation
/////////////////////////////////////////////////////////////////////////////////////
void CSave::ReadString(char *pDst, int nMaxSize)
{
    while (nMaxSize > 0)
    {
        char ch;
        Read(ch);
        *pDst++ = ch;
        nMaxSize--;
        if (ch == 0)
            break;
    }
    pDst[nMaxSize - 1] = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CSave::ReadChunk(DWORD &dwID, CMemorySave &chunk)
{
    DWORD dwLeng = 0, dwRead;
    if (!Read(&dwID, sizeof(dwID)))
        return false;
    if (!Read(&dwLeng, sizeof(dwLeng)))
        return false;
    dwLeng -= 8;
    chunk.Clear();
    chunk.StartSaving();
    char *buf = dbgnew char[dwLeng];
    dwRead = Read(buf, dwLeng);
    if (dwRead != dwLeng)
    {
        delete[] buf;
        return false;
    }
    chunk.Write(buf, dwLeng);
    chunk.Seek(0);
    delete[] buf;
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CSave::WriteChunk(DWORD dwID, CMemorySave &chunk)
{
    DWORD dwLeng;
    Write(&dwID, sizeof(dwID));
    dwLeng = chunk.GetSize() + 8;
    Write(&dwLeng, sizeof(dwLeng));
    dwLeng -= 8;
    char *buf = dbgnew char[dwLeng];
    chunk.Seek(0);
    chunk.StartLoading();
    chunk.Read(buf, dwLeng);
    Write(buf, dwLeng);
    delete[] buf;
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CSave::GetChunk(DWORD dwID, CMemorySave &chunk)
{
    DWORD dwRid;
    Seek(0);
    StartLoading();
    while (ReadChunk(dwRid, chunk))
    {
        if (dwRid == dwID)
            return true;
    }
    chunk.Clear();
    return false;
}
/////////////////////////////////////////////////////////////////////////////////////
void CSave::ReplaceChunk(DWORD dwID, CMemorySave &chunk, CSave &result)
{
    bool bReplaced = false;
    DWORD dwRid;
    CMemorySave original;
    result.StartSaving();
    while (ReadChunk(dwRid, original))
    {
        if (dwRid == dwID)
        {
            bReplaced = true;
            result.WriteChunk(dwRid, chunk);
        }
        else
            result.WriteChunk(dwRid, original);
    }
    if (!bReplaced)
        result.WriteChunk(dwID, chunk);
}
/////////////////////////////////////////////////////////////////////////////////////
void CSave::CopyFrom(CSave &file)
{
    file.Seek(0);
    file.StartLoading();
    char buf[1024];
    StartSaving();
    while (1)
    {
        DWORD dwRead = file.Read(buf, 1024);
        if (dwRead == 0)
            break;
        Write(buf, dwRead);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
static void StoreLength(CSave &file, int &nStorePosition, unsigned char nNum)
{
    int nPos = file.GetPosition();
    file.Seek(nStorePosition);
    file.Write(nNum);
    file.Seek(nPos);
    nStorePosition = file.GetPosition();
    file.Write((char)0);
}
/////////////////////////////////////////////////////////////////////////////////////
void CSave::StoreDifference(CSave &original, CSave &subj)
{
    int i, nLen;
    ASSERT(original.GetSize() == subj.GetSize());
    if (original.GetSize() != subj.GetSize())
        return;
    original.Seek(0);
    subj.Seek(0);
    nLen = original.GetSize();

    bool bSame = true;
    int nCurCount = 0, nStorePos = GetPosition();
    Write((char)0);
    for (i = 0; i < nLen; i++)
    {
        char ch1, ch2;
        original.Read(ch1);
        subj.Read(ch2);
        if (ch1 != ch2)
        {
            if (!bSame)
            {
                nCurCount++;
                if (nCurCount == 256)
                {
                    StoreLength(*this, nStorePos, 255);
                    StoreLength(*this, nStorePos, 0);
                    nCurCount = 1;
                }
            }
            else
            {
                StoreLength(*this, nStorePos, (unsigned char)nCurCount);
                bSame = false;
                nCurCount = 1;
            }
            Write(ch2);
        }
        else
        {
            if (bSame)
            {
                nCurCount++;
                if (nCurCount == 256)
                {
                    StoreLength(*this, nStorePos, 255);
                    StoreLength(*this, nStorePos, 0);
                    nCurCount = 1;
                }
            }
            else
            {
                StoreLength(*this, nStorePos, (unsigned char)nCurCount);
                bSame = true;
                nCurCount = 1;
            }
        }
    }
    StoreLength(*this, nStorePos, (unsigned char)nCurCount);
}
/////////////////////////////////////////////////////////////////////////////////////
void CSave::LoadFromDifference(CSave &original, CSave &dif)
{
    int nLeng = 0, nCurPos = 0, nTotalLeng = original.GetSize();
    char buf[256];
    original.Seek(0);
    Seek(0);
    while (nCurPos < nTotalLeng)
    {
        dif.Read(&nLeng, 1);
        original.Read(buf, nLeng);
        Write(buf, nLeng);
        nCurPos += nLeng;

        if (dif.Read(&nLeng, 1) == 0)
        {
            ASSERT(0);
            break;
        }
        original.Read(buf, nLeng);
        dif.Read(buf, nLeng);
        Write(buf, nLeng);
        nCurPos += nLeng;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CSave::Read(CSave &res)
{
    unsigned int nSize;
    char *pData;
    nSize = 0;
    Read(&nSize, 2);
    if (nSize & 1)
        Read(((char *)&nSize) + 2, 2);
    nSize >>= 1;
    res.Clear();
    res.Seek(0);
    ASSERT(nSize < 10000000);
    if (nSize == 0)
        return;
    pData = dbgnew char[nSize];
    Read(pData, nSize);
    res.Write(pData, nSize);
    delete[] pData;
}
/////////////////////////////////////////////////////////////////////////////////////
void CSave::Write(CSave &res)
{
    int nSize, nSz;
    nSz = nSize = res.GetSize();
    if (nSize > 30000)
    {
        nSz = (nSz << 1) + 1;
        Write(&nSz, 4);
    }
    else
    {
        nSz = (nSz << 1);
        Write(&nSz, 2);
    }
    char *pData = dbgnew char[nSize];
    res.Seek(0);
    res.Read(pData, nSize);
    Write(pData, nSize);
    delete[] pData;
}
/////////////////////////////////////////////////////////////////////////////////////
// universal functions to load/store headers of save files
bool CSave::LoadSaveHeader(const int nSignature, const int nCurrentVersion,
                           const int nMinimalVersion)
{
    int nID = 0;
    Read(nID);
    if (nID != nSignature)
        return false;
    Read(nID);
    ASSERT(nID <= nCurrentVersion);
    if (nID > nCurrentVersion)
        return false;
    if (nID < nMinimalVersion)
        return false;
    SetVersion(nID);
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
void CSave::StoreSaveHeader(const int nSignature, const int nCurrentVersion)
{
    Write(nSignature);
    Write(nCurrentVersion);
    SetVersion(nCurrentVersion);
}
/////////////////////////////////////////////////////////////////////////////////////
// CMemorySave implementation
/////////////////////////////////////////////////////////////////////////////////////
UINT CMemorySave::VRead(void *lpBuf, UINT nCount)
{
    int rv;
    if (nPos + nCount > nFileSize)
    {
        bIsOk = false;
        rv = nFileSize - nPos;
        memset(lpBuf, 0, nCount);
        if (rv < 0)
            return 0;
        nCount = (UINT)rv;
    }
    memcpy(lpBuf, pszData + nPos, nCount);
    nPos += nCount;
    return nCount;
}
/////////////////////////////////////////////////////////////////////////////////////
void CMemorySave::VWrite(const void *lpBuf, UINT nCount)
{
    int nOldSize;
    char *pszNewBuf;
    if (nPos + nCount > nBufSize)
    {
        nOldSize = nBufSize;
        nBufSize = (nBufSize + nCount) * 2;
        pszNewBuf = dbgnew char[nBufSize];
        memcpy(pszNewBuf, pszData, nOldSize);
        delete[] pszData;
        pszData = pszNewBuf;
    }
    memcpy(pszData + nPos, lpBuf, nCount);
    nPos += nCount;
    nFileSize = nFileSize > nPos ? nFileSize : nPos; // max( nFileSize, nPos )
}
/////////////////////////////////////////////////////////////////////////////////////
void CMemorySave::Seek(unsigned int nNPos)
{
    ASSERT(nNPos >= 0 && nNPos <= nBufSize);
    nPos = nNPos;
    FlushBits();
    nBits = 0x80;
}
/////////////////////////////////////////////////////////////////////////////////////
void CMemorySave::SetSize(UINT nLeng)
{
    ASSERT(nFileSize <= nBufSize);
    if (nLeng <= nBufSize)
    {
        nFileSize = nLeng;
        nPos = 0;
        return;
    }
    char *pPrevData = pszData;
    int nNewBuf = nLeng + (nLeng >> 1) + 16;
    pszData = dbgnew char[nNewBuf];
    if (pPrevData)
        memcpy(pszData, pPrevData, nFileSize);
    delete[] pPrevData;
    nBufSize = nNewBuf;
    nPos = 0;
    FlushBits();
    nBits = 0x80;
    nFileSize = nLeng;
}
/////////////////////////////////////////////////////////////////////////////////////
void CMemorySave::SetSizeFast(UINT nLeng)
{
    if (nLeng <= nBufSize)
    {
        nFileSize = nLeng;
        nPos = 0;
        return;
    }
    if (pszData)
        delete[] pszData;
    pszData = dbgnew char[nLeng];
    nBufSize = nLeng;
    nPos = 0;
    nBits = 0x80;
    nFileSize = nLeng;
}
/////////////////////////////////////////////////////////////////////////////////////
void CMemorySave::CopyFrom(CSave &file)
{
    file.Seek(0);
    SetSize(file.GetSize());
    file.Read(pszData, nFileSize);
    Seek(0);
}
/////////////////////////////////////////////////////////////////////////////////////
void CMemorySave::AllocNewBitsByte()
{
    if (nBitsWritePos >= 0)
        pszData[nBitsWritePos] = cBits;
    nBitsWritePos = nPos;
    cBits = 0;
    Write(&cBits, 1);
    nBits = 1;
}
/////////////////////////////////////////////////////////////////////////////////////
void CMemorySave::FlushBits()
{
    if (nBitsWritePos >= 0)
        pszData[nBitsWritePos] = cBits;
    nBitsWritePos = -1;
    nBits = 0x80;
}
/////////////////////////////////////////////////////////////////////////////////////
