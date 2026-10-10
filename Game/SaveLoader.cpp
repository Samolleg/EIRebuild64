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
#include "TypeIDs.h"

/////////////////////////////////////////////////////////////////////////////////////
// CSave implementation
/////////////////////////////////////////////////////////////////////////////////////
void CSave::ReadString(char *pDst, int nMaxSize)
{
    if (nMaxSize <= 0) { Fail("invalid_string_buffer"); return; }
    int written = 0;
    for (;;)
    {
        char ch = 0;
        if (Read(&ch, 1) != 1) { pDst[written] = 0; return; }
        if (!ch) { pDst[written] = 0; return; }
        if (written < nMaxSize - 1) pDst[written++] = ch;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
bool CSave::ReadChunk(DWORD &dwID, CMemorySave &chunk)
{
    DWORD dwLeng = 0, dwRead;
    if (Eof()) return false;
    if (!Require(8, "truncated_chunk_header")) return false;
    if (!Read(&dwID, sizeof(dwID)))
        return false;
    if (!Read(&dwLeng, sizeof(dwLeng)))
        return false;
    if (dwLeng < 8) { Fail("invalid_chunk_length"); return false; }
    dwLeng -= 8;
    if (!Require(dwLeng, "truncated_chunk")) return false;
    chunk.Clear();
    chunk.InheritReadPolicy(*this);
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
// A save delta alternates equal-byte and changed-byte runs. Keep the final
// marker aligned with the following dynamic-object registry.
static void StoreDeltaLength(CSave &file, unsigned int &lengthPosition, unsigned char length, bool appendNextLength)
{
    unsigned int endPosition = file.GetPosition();
    file.Seek(lengthPosition);
    file.Write(length);
    file.Seek(endPosition);
    if (appendNextLength)
    {
        lengthPosition = endPosition;
        file.Write((unsigned char)0);
    }
}

static bool IsDynamicObjectType(unsigned int typeId)
{
    return (typeId >= MOTID_HUMAN && typeId <= MOTID_LEVER) || typeId == MOTID_KAPKAN;
}

static bool IsDynamicRegistry(CSave &file, unsigned int position)
{
    const unsigned int savedPosition = file.GetPosition();
    const unsigned int size = file.GetSize();
    unsigned int rows = 0;
    bool valid = false;
    file.Seek(position);
    while (position <= size && size - position >= sizeof(unsigned int))
    {
        unsigned int id = 0;
        if (file.Read(&id, sizeof(id)) != sizeof(id))
            break;
        position += sizeof(id);
        if (id == 0xFFFFFFFF)
        {
            valid = true;
            break;
        }
        if (size - position < sizeof(unsigned int) * 2)
            break;
        unsigned int typeId = 0, subtype = 0;
        if (file.Read(&typeId, sizeof(typeId)) != sizeof(typeId) ||
            file.Read(&subtype, sizeof(subtype)) != sizeof(subtype))
            break;
        position += sizeof(unsigned int) * 2;
        if (!IsDynamicObjectType(typeId) || ++rows > 10000)
            break;
    }
    file.Seek(savedPosition);
    return valid;
}

void CSave::StoreDifference(CSave &original, CSave &subj)
{
    const unsigned int length = original.GetSize();
    ASSERT(length == subj.GetSize());
    if (length != subj.GetSize() || length == 0)
        return;
    original.Seek(0);
    subj.Seek(0);
    bool sameRun = true;
    unsigned int runLength = 0;
    unsigned int lengthPosition = GetPosition();
    Write((unsigned char)0);
    for (unsigned int i = 0; i < length; ++i)
    {
        unsigned char originalByte = 0, subjectByte = 0;
        original.Read(originalByte);
        subj.Read(subjectByte);
        const bool bytesSame = originalByte == subjectByte;
        if (bytesSame == sameRun)
        {
            if (++runLength == 256)
            {
                StoreDeltaLength(*this, lengthPosition, 255, true);
                StoreDeltaLength(*this, lengthPosition, 0, true);
                runLength = 1;
            }
        }
        else
        {
            StoreDeltaLength(*this, lengthPosition, static_cast<unsigned char>(runLength), true);
            sameRun = bytesSame;
            runLength = 1;
        }
        if (!bytesSame)
            Write(subjectByte);
    }
    StoreDeltaLength(*this, lengthPosition, static_cast<unsigned char>(runLength), sameRun);
}

void CSave::LoadFromDifference(CSave &original, CSave &dif)
{
    unsigned int current = 0;
    const unsigned int total = original.GetSize();
    unsigned char buffer[256];
    bool endedAfterChangedRun = false;
    original.Seek(0);
    Seek(0);
    while (current < total)
    {
        unsigned char sameLength = 0;
        if (dif.Read(&sameLength, sizeof(sameLength)) != sizeof(sameLength))
            break;
        if (sameLength > total - current) { dif.Fail("delta_equal_overflow"); return; }
        original.Read(buffer, sameLength);
        Write(buffer, sameLength);
        current += sameLength;
        unsigned char changedLength = 0;
        if (dif.Read(&changedLength, sizeof(changedLength)) != sizeof(changedLength))
            break;
        if (changedLength > total - current) { dif.Fail("delta_changed_overflow"); return; }
        if (!dif.Require(changedLength, "truncated_delta")) return;
        original.Read(buffer, changedLength);
        dif.Read(buffer, changedLength);
        Write(buffer, changedLength);
        current += changedLength;
        endedAfterChangedRun = current == total && changedLength != 0;
    }
    if (current != total) { dif.Fail("incomplete_delta"); return; }
    if (!endedAfterChangedRun)
        return;
    const unsigned int registryPosition = dif.GetPosition();
    if (registryPosition >= dif.GetSize())
        return;
    unsigned char marker = 0xFF;
    dif.Read(&marker, sizeof(marker));
    dif.Seek(registryPosition);
    if (marker == 0 &&
        !IsDynamicRegistry(dif, registryPosition) &&
        IsDynamicRegistry(dif, registryPosition + 1))
        dif.Seek(registryPosition + 1);
}
//\f///////////////////////////////////////////////////////////////////////////////////
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
    res.InheritReadPolicy(*this);
    res.Seek(0);
    if (!Require(nSize, "truncated_embedded_save")) return;
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
    if (Read(&nID, sizeof(nID)) != sizeof(nID)) return false;
    if (nID != nSignature)
        return false;
    if (Read(&nID, sizeof(nID)) != sizeof(nID)) return false;
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
    if (nPos > nFileSize || nCount > nFileSize - nPos)
    {
        bIsOk = false;
        rv = nPos <= nFileSize ? nFileSize - nPos : 0;
        memset(lpBuf, 0, nCount);
        if (rv < 0)
            return 0;
        nCount = (UINT)rv;
    }
    if (nCount) memcpy(lpBuf, pszData + nPos, nCount);
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
    if (IsStrictRead() && nNPos > nFileSize) { Fail("seek_out_of_bounds"); return; }
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
    InheritReadPolicy(file);
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
