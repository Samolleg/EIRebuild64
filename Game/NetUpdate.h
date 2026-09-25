#pragma once
/**********************************************************************
 *<
        FILE: NetUpdate.H

        DESCRIPTION:  Network updates, general mechanism for traffic optimization

        CREATED BY: Andy Gulin

 *>	Copyright (c) 1999, Nival Enterntainemnt
 **********************************************************************/

#include "SaveLoader.h"

class CPlot3D;
class CPlot2D;
class CNetCompressor;
///////////////////////////////////////////////////////////////////////////////////////////////////////////
enum EUpdateType
{
    utStart,
    utFrame
};
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// Crap realization of this basic class for network updates, perfomance of this class is very
// important for network updates perfomance
class CPacket : public CMemorySave
{
public:
    void Pack(CNetCompressor &pack, CPacket &dst);
    void Unpack(CNetCompressor &pack, CPacket &dst);
    void Append(CPacket &pkt)
    {
        FlushBits();
        pkt.Seek(0);
        Write(pkt.GetBuffer(), pkt.GetSize());
    }
    void WritePacket(CMemorySave &pkt);
    BOOL ReadPacket(CMemorySave &pkt);
};
///////////////////////////////////////////////////////////////////////////////////////////////////////////
class CUpdateHistory
{
    static CPacket *pSrcPacket;
    static BOOL bStartReplace, bFrameReplace;
    static CMemorySave *pReference;
    CMemorySave start, frame; // states of object on starting moment and on previous frame
public:
    CPacket startPkt,
        framePkt; // пакеты с информацией об изменениях от создания объекта и от предыдущего update

    void Clear()
    {
        start.SetSize(0);
        frame.SetSize(0);
        startPkt.SetSize(0);
        framePkt.SetSize(0);
    }
    void StartUpdateTo(EUpdateType dest);
    void StartUpdateFrom(EUpdateType tip, CPacket &pkt);
    void FinishUpdate();
    void Write(const void *pData, int nSize);
    void Read(void *pData, int nSize);
    bool CanRead(int nSize) const;
    void WriteDirect(const void *pData, int nSize);
    void ReadDirect(void *pData, int nSize);
    void WriteDirect(CMemorySave &packet);
    BOOL ReadDirect(CMemorySave &packet);
    BOOL UpdateFromIsOk()
    {
        ASSERT(pReference);
        return pReference->GetPosition() == pReference->GetSize();
    }
    // smart updates for strings are not implemented yet ( list of possible strings is to come )
    // names of units should be transferred as a fixed length data - this allow utilization of
    // standard traffic compression
    void WriteString(const char *pString);
    CString ReadString();
    void ReadString(char *pDest, int nMaxLeng);
    // several specialized functions
    void WritePos(const CPlot3D &ptPos);
    void ReadPos(CPlot3D &ptPos);
    void WritePos(const CPlot2D &ptPos);
    void ReadPos(CPlot2D &ptPos);
    void WriteFloat(float fpVal, float fpMin, float fpMax);
    void ReadFloat(float &fpVal, float fpMin, float fpMax);
    void WriteFloat(float fpVal);
    void ReadFloat(float &fpVal);
    void WriteSpeed(const CPlot3D &ptPos);
    void ReadSpeed(CPlot3D &ptPos);

    CUpdateHistory &operator>>(long &res)
    {
        Read(&res, sizeof(res));
        return *this;
    }
    CUpdateHistory &operator>>(unsigned long &res)
    {
        Read(&res, sizeof(res));
        return *this;
    }
    CUpdateHistory &operator>>(int &res)
    {
        Read(&res, sizeof(res));
        return *this;
    }
    CUpdateHistory &operator>>(unsigned int &res)
    {
        Read(&res, sizeof(res));
        return *this;
    }
    CUpdateHistory &operator>>(short &res)
    {
        Read(&res, sizeof(res));
        return *this;
    }
    CUpdateHistory &operator>>(unsigned short &res)
    {
        Read(&res, sizeof(res));
        return *this;
    }
    CUpdateHistory &operator>>(char &res)
    {
        Read(&res, sizeof(res));
        return *this;
    }
    CUpdateHistory &operator>>(signed char &res)
    {
        Read(&res, sizeof(res));
        return *this;
    }
    CUpdateHistory &operator>>(unsigned char &res)
    {
        Read(&res, sizeof(res));
        return *this;
    }
    CUpdateHistory &operator>>(float &res)
    {
        Read(&res, sizeof(res));
        return *this;
    }
    CUpdateHistory &operator>>(double &res)
    {
        Read(&res, sizeof(res));
        return *this;
    }

    CUpdateHistory &operator<<(long res)
    {
        Write(&res, sizeof(res));
        return *this;
    }
    CUpdateHistory &operator<<(unsigned long res)
    {
        Write(&res, sizeof(res));
        return *this;
    }
    CUpdateHistory &operator<<(int res)
    {
        Write(&res, sizeof(res));
        return *this;
    }
    CUpdateHistory &operator<<(unsigned int res)
    {
        Write(&res, sizeof(res));
        return *this;
    }
    CUpdateHistory &operator<<(short res)
    {
        Write(&res, sizeof(res));
        return *this;
    }
    CUpdateHistory &operator<<(unsigned short res)
    {
        Write(&res, sizeof(res));
        return *this;
    }
    CUpdateHistory &operator<<(char res)
    {
        Write(&res, sizeof(res));
        return *this;
    }
    CUpdateHistory &operator<<(signed char res)
    {
        Write(&res, sizeof(res));
        return *this;
    }
    CUpdateHistory &operator<<(unsigned char res)
    {
        Write(&res, sizeof(res));
        return *this;
    }
    CUpdateHistory &operator<<(float res)
    {
        Write(&res, sizeof(res));
        return *this;
    }
    CUpdateHistory &operator<<(double res)
    {
        Write(&res, sizeof(res));
        return *this;
    }
};
///////////////////////////////////////////////////////////////////////////////////////////////////////////
class CUpdateSingle
{
    CUpdateHistory history;

public:
    void AppendFrame(CPacket &dest) { dest.Append(history.framePkt); }
    void AppendStart(CPacket &dest) { dest.Append(history.startPkt); }
    void Clear() { history.Clear(); }
    CUpdateHistory *StartUpdateTo(EUpdateType tip);
    CUpdateHistory *StartUpdateFrom(EUpdateType tip, CPacket &from);
    void FinishUpdate();
};
///////////////////////////////////////////////////////////////////////////////////////////////////////////
enum EUpdateStartType
{
    utToStart,
    utToFrame,
    utFromStart,
    utFromFrame,
    utNone
};
///////////////////////////////////////////////////////////////////////////////////////////////////////////
class CUpdateMultiple
{
    CArray<CUpdateHistory, CUpdateHistory &> histories;
    WORD wStartMask, wFrameMask;
    WORD wCurrentMask; // current mask in use
    CPacket *pkt;
    EUpdateStartType startType;

    void BuildPacket(CPacket &dst, WORD wMask, EUpdateType tip);

public:
    CPacket startPkt,
        framePkt; // пакеты с информацией об изменениях от создания объекта и от предыдущего update

    void Clear();
    CUpdateMultiple() { Clear(); }
    void SetHistoriesNumber(int nNum) { histories.SetSize(nNum); }
    int GetHistoriesNumber() const { return histories.GetSize(); }
    void SetUpdateMask(int nBit)
    {
        WORD wMask = 1 << nBit;
        wCurrentMask |= wMask;
    }
    int IsUpdateMask(int nBit) { return ((int)wCurrentMask) & (1 << nBit); }
    WORD GetStartUpdateMask() { return wStartMask | wCurrentMask; }

    CUpdateHistory *GetHistory(int nNumber);
    void StartUpdateTo(EUpdateType tip);
    void StartUpdateFrom(EUpdateType tip, CPacket &from);
    void FinishUpdate();
};
///////////////////////////////////////////////////////////////////////////////////////////////////////////
