/**********************************************************************
 *<
        FILE: NetUpdate.cpp

        DESCRIPTION:  Network updates, general mechanism for traffic optimization

        CREATED BY: Andy Gulin

 *>	Copyright (c) 1999, Nival Enterntainemnt
 **********************************************************************/

#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "NetUpdate.h"
#include "plot.h"
#include "Cruncher.h"

/////////////////////////////////////////////////////////////////////////////////////
// CUpdateHistory class
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPacket::Pack(CNetCompressor &pack, CPacket &dst)
{
    CMemoryStream fr;
    CFixedMemStream f(GetBuffer(), GetSize());
    pack.Pack(f, fr);
    dst.SetSize(fr.GetSize());
    dst.Seek(0);
    dst.Write(fr.GetBuffer(), fr.GetSize());
    dst.Seek(0);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPacket::Unpack(CNetCompressor &pack, CPacket &dst)
{
    /*	if ( GetSize() == 0 )
            {
                    dst.Clear();
                    return;
            }*/
    CMemoryStream fr;
    CFixedMemStream f(GetBuffer(), GetSize());
    pack.Unpack(f, fr);
    dst.SetSize(fr.GetSize());
    dst.Seek(0);
    dst.Write(fr.GetBuffer(), fr.GetSize());
    dst.Seek(0);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CPacket::WritePacket(CMemorySave &pkt)
{
    pkt.FlushBits();
    int nSendSize = pkt.GetSize() * 2;
    if (nSendSize < 256)
        Write(&nSendSize, 1);
    else
    {
        nSendSize |= 1;
        Write(&nSendSize, 4);
    }
    Write(pkt.GetBuffer(), pkt.GetSize());
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CPacket::ReadPacket(CMemorySave &pkt)
{
    UINT nSize, nReadSize;
    nSize = 0;
    Read(&nSize, 1);
    if (nSize & 1)
        Read(((char *)&nSize) + 1, 3);
    nSize >>= 1;
    if (nSize > 100000)
        return FALSE;
    pkt.SetSizeFast(nSize);
    nReadSize = Read(pkt.GetBufferForWrite(), nSize);
    if (nReadSize != nSize)
        return FALSE;
    pkt.Seek(0);
    pkt.SetOk();
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// CUpdateHistory class
///////////////////////////////////////////////////////////////////////////////////////////////////////////
CMemorySave *CUpdateHistory::pReference = NULL;
CPacket *CUpdateHistory::pSrcPacket = NULL;
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CUpdateHistory::bStartReplace;
BOOL CUpdateHistory::bFrameReplace;
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CUpdateHistory::StartUpdateTo(EUpdateType dest)
{
    ASSERT(!pSrcPacket);
    start.Seek(0);
    frame.Seek(0);
    startPkt.SetSize(0);
    framePkt.SetSize(0);
    if (dest == utStart)
    {
        // only one start filling is allowed, so be careful about place of calling UpdateTo( start,
        // ... )
        ASSERT(start.GetSize() == 0);
        bStartReplace = TRUE;
        bFrameReplace = TRUE;
    }
    else
    {
        bStartReplace = FALSE;
        bFrameReplace = TRUE;
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CUpdateHistory::StartUpdateFrom(EUpdateType tip, CPacket &pkt)
{
    start.Seek(0);
    frame.Seek(0);
    if (tip == utStart)
        pReference = &start;
    else
        pReference = &frame;
    pSrcPacket = &pkt;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CUpdateHistory::FinishUpdate()
{
    if (pReference)
    {
        pSrcPacket->FlushBits();
        pReference = NULL;
        pSrcPacket = NULL;
    }
    else
    {
        startPkt.FlushBits();
        framePkt.FlushBits();
        if (bStartReplace)
            start.SetSize(start.GetPosition());
        if (bFrameReplace)
            frame.SetSize(frame.GetPosition());
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// записать в dst разницу между pSrc и pData
inline void StoreDifference(const void *pSrc, const void *pData, int nSize, CPacket &dst)
{
    if (memcmp(pSrc, pData, nSize) == 0)
        dst.WriteBit(true);
    else
    {
        dst.WriteBit(false);
        const int *pISrc = (const int *)pSrc;
        const int *pIDst = (const int *)pData;
        for (; nSize > 3; pISrc++, pIDst++, nSize -= 4)
        {
            if (pISrc[0] == pIDst[0])
                dst.WriteBit(true);
            else
            {
                dst.WriteBit(false);
                dst.Write(pIDst, 4);
            }
        }
        dst.Write(pIDst, nSize);
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// тот же формат, что и StoreDifference, но целиком не совпадает
inline void StoreFirstDifference(const void *pData, int nSize, CPacket &dst)
{
    dst.WriteBit(false);
    const int *pIDst = (const int *)pData;
    for (; nSize > 3; pIDst++, nSize -= 4)
    {
        dst.WriteBit(false);
        dst.Write(pIDst, 4);
    }
    dst.Write(pIDst, nSize);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// считать то, что записано StoreDifference
// вернет true если данные изменились
inline bool LoadDifference(const void *pSrc, void *pData, int nSize, CPacket &src)
{
    if (src.ReadBit())
    {
        memcpy(pData, pSrc, nSize);
        return false;
    }
    const int *pISrc = (const int *)pSrc;
    int *pIDst = (int *)pData;
    for (; nSize > 3; pISrc++, pIDst++, nSize -= 4)
    {
        if (src.ReadBit())
            pIDst[0] = pISrc[0];
        else
            src.Read(pIDst, 4);
    }
    src.Read(pIDst, nSize);
    return true;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// записать разницу между предыдущим update и новым
inline void CheckAndReplace(CMemorySave &data, const void *pData, int nSize, BOOL bReplace,
                            CPacket &dst)
{
    ASSERT(nSize < 4096);
    if (data.GetSize() >= data.GetPosition() + nSize)
    {
        char *pSrc = data.GetBufferForWrite() + data.GetPosition();
        StoreDifference(pSrc, pData, nSize, dst);
        if (bReplace)
            memcpy(pSrc, pData, nSize);
        data.Seek(data.GetPosition() + nSize);
        return;
    }
    ASSERT(bReplace);
    StoreFirstDifference(pData, nSize, dst);
    data.Write(pData, nSize);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CUpdateHistory::Write(const void *pData, int nSize)
{
    // compare data with reference frame and store difference
    if (absoluteStart)
        StoreFirstDifference(pData, nSize, startPkt);
    else
        CheckAndReplace(start, pData, nSize, bStartReplace, startPkt);
    CheckAndReplace(frame, pData, nSize, bFrameReplace, framePkt);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
bool CUpdateHistory::CanRead(int nSize) const
{
    if (!pReference || !pSrcPacket || nSize < 0 ||
        pReference->GetPosition() > pReference->GetSize() ||
        pSrcPacket->GetPosition() > pSrcPacket->GetSize())
        return false;
    const unsigned long long remaining =
        static_cast<unsigned long long>(pReference->GetSize() - pReference->GetPosition()) +
        (pSrcPacket->GetSize() - pSrcPacket->GetPosition());
    return remaining >= static_cast<unsigned>(nSize);
}
void CUpdateHistory::Read(void *pData, int nSize)
{
    int nPos;
    ASSERT(pReference);
    nPos = pReference->GetPosition();
    const char *pSrc = pReference->GetBuffer() + nPos;
    if (LoadDifference(pSrc, pData, nSize, *pSrcPacket))
    {
        frame.Seek(nPos);
        frame.Write(pData, nSize);
    }
    pReference->Seek(nPos + nSize);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CUpdateHistory::WriteDirect(const void *pData, int nSize)
{
    startPkt.Write(pData, nSize);
    framePkt.Write(pData, nSize);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CUpdateHistory::ReadDirect(void *pData, int nSize)
{
    pSrcPacket->Read(pData, nSize);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CUpdateHistory::WriteDirect(CMemorySave &packet)
{
    startPkt.WritePacket(packet);
    framePkt.WritePacket(packet);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CUpdateHistory::ReadDirect(CMemorySave &packet)
{
    return pSrcPacket->ReadPacket(packet);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CUpdateHistory::WriteString(const char *pString)
{
    startPkt.Write(pString, strlen(pString) + 1);
    framePkt.Write(pString, strlen(pString) + 1);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
CString CUpdateHistory::ReadString()
{
    return pSrcPacket->ReadString();
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CUpdateHistory::ReadString(char *pDest, int nMaxLeng)
{
    CString szTemp = ReadString();
    if (szTemp.GetLength() < nMaxLeng - 1)
    {
        strcpy(pDest, szTemp);
    }
    else
        pDest[0] = 0;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CUpdateHistory::WritePos(const CPlot3D &ptPos)
{
    WriteFloat(ptPos.x, -16, 512 + 16);
    WriteFloat(ptPos.y, -16, 512 + 16);
    WriteFloat(ptPos.z, -32, 128 + 32);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CUpdateHistory::ReadPos(CPlot3D &ptPos)
{
    ReadFloat(ptPos.x, -16, 512 + 16);
    ReadFloat(ptPos.y, -16, 512 + 16);
    ReadFloat(ptPos.z, -32, 128 + 32);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CUpdateHistory::WritePos(const CPlot2D &ptPos)
{
    WriteFloat(ptPos.x, -32, 1024 + 32);
    WriteFloat(ptPos.y, -32, 1024 + 32);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CUpdateHistory::ReadPos(CPlot2D &ptPos)
{
    ReadFloat(ptPos.x, -32, 1024 + 32);
    ReadFloat(ptPos.y, -32, 1024 + 32);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CUpdateHistory::WriteFloat(float fpVal, float fpMin, float fpMax)
{
    const float fpMn = 65535 / (fpMax - fpMin);
    int nRes = Float2Int((fpVal - fpMin) * fpMn);
    ASSERT(nRes > 0 && nRes < 65536);
    Write(&nRes, 2);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CUpdateHistory::ReadFloat(float &fpVal, float fpMin, float fpMax)
{
    const float fpMn = (fpMax - fpMin) / 65535;
    unsigned short nRes;
    Read(&nRes, 2);
    fpVal = fpMin + fpMn * nRes;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// dark magic of float point numbers, reducing precision
union UFloat
{
    float f;
    struct
    {
        unsigned int m : 23, p : 8, s : 1;
    };
};
struct SFloat16
{
    unsigned short m : 8, p : 7, s : 1;
};
void CUpdateHistory::WriteFloat(float fpVal)
{
    UFloat u;
    SFloat16 res;
    u.f = fpVal;
    res.s = u.s;
    if (u.p < 0x40)
        res.p = 0;
    else
        res.p = u.p - 0x40;
    res.m = (u.m + 16384) >> 15;
    Write(&res, 2);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CUpdateHistory::ReadFloat(float &fpVal)
{
    UFloat u;
    SFloat16 res;
    Read(&res, 2);
    u.s = res.s;
    if (res.p != 0)
        u.p = res.p + 0x40;
    else
        u.p = 0;
    u.m = res.m << 15;
    fpVal = u.f;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CUpdateHistory::WriteSpeed(const CPlot3D &ptPos)
{
    WriteFloat(ptPos.x);
    WriteFloat(ptPos.y);
    WriteFloat(ptPos.z);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CUpdateHistory::ReadSpeed(CPlot3D &ptPos)
{
    ReadFloat(ptPos.x);
    ReadFloat(ptPos.y);
    ReadFloat(ptPos.z);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// CUpdateSingle class
///////////////////////////////////////////////////////////////////////////////////////////////////////////
CUpdateHistory *CUpdateSingle::StartUpdateTo(EUpdateType tip)
{
    history.StartUpdateTo(tip);
    return &history;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
CUpdateHistory *CUpdateSingle::StartUpdateFrom(EUpdateType tip, CPacket &from)
{
    history.StartUpdateFrom(tip, from);
    return &history;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CUpdateSingle::FinishUpdate()
{
    history.FinishUpdate();
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// CUpdateMultiple
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CUpdateMultiple::Clear()
{
    for (int i = 0; i < histories.GetSize(); i++)
        histories[i].Clear();
    wStartMask = 0;
    wFrameMask = 0;
    wCurrentMask = 0;
    pkt = NULL;
    startType = utNone;
    startPkt.SetSize(0);
    framePkt.SetSize(0);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
CUpdateHistory *CUpdateMultiple::GetHistory(int nNumber)
{
    if (!IsUpdateMask(nNumber))
        return NULL;
    switch (startType)
    {
    case utToStart:
        histories[nNumber].StartUpdateTo(utStart);
        break;
    case utToFrame:
        histories[nNumber].StartUpdateTo(utFrame);
        break;
    case utFromStart:
        histories[nNumber].StartUpdateFrom(utStart, *pkt);
        break;
    case utFromFrame:
        histories[nNumber].StartUpdateFrom(utFrame, *pkt);
        break;
    default:
        ASSERT(0);
        break;
    }
    return &histories[nNumber];
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CUpdateMultiple::StartUpdateTo(EUpdateType tip)
{
    if (tip == utStart)
    {
        wCurrentMask = 0xFFFF;
        startType = utToStart;
    }
    else
    {
        startType = utToFrame;
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CUpdateMultiple::StartUpdateFrom(EUpdateType tip, CPacket &from)
{
    pkt = &from;
    from.Read(&wCurrentMask, sizeof(wCurrentMask));
    if (tip == utStart)
        startType = utFromStart;
    else
        startType = utFromFrame;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CUpdateMultiple::BuildPacket(CPacket &dst, WORD wMask, EUpdateType tip)
{
    dst.SetSize(0);
    dst.Write(&wMask, sizeof(wMask));
    for (int i = 0; i < histories.GetSize(); i++)
    {
        if (wMask & (1 << i))
        {
            if (tip == utStart)
                dst.Append(histories[i].startPkt);
            else
                dst.Append(histories[i].framePkt);
        }
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CUpdateMultiple::FinishUpdate()
{
    if (startType == utToFrame)
    {
        wFrameMask = wCurrentMask;
        wStartMask |= wFrameMask;
        wCurrentMask = 0;
        BuildPacket(startPkt, wStartMask, utStart);
        BuildPacket(framePkt, wFrameMask, utFrame);
    }
    if (startType == utToStart)
    {
        startPkt.SetSize(0);
        framePkt.SetSize(0);
        wStartMask = 0;
        wFrameMask = 0;
        wCurrentMask = 0;
    }
    startType = utNone;
    pkt = NULL;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
