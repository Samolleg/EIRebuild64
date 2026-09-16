#pragma once
/**********************************************************************
 *<
        FILE: NetTcp.H

        DESCRIPTION:  TCP Network interface

        CREATED BY: Andy Gulin

 *>	Copyright (c) 1999, Nival Enterntainemnt
 **********************************************************************/

#include "Net.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////
const int N_MAX_PACKET_LENGTH = 32768;
const DWORD N_KEEP_ALIVE_TIMEOUT = 30000; // 30 sec
///////////////////////////////////////////////////////////////////////////////////////////////////////////
class CStreamConnection : public CConnection
{
    // contains current incoming data
    CMemorySave readPkt;

protected:
    void NewDataIncome(const char *pszData, int nLen);
    void ReturnDataRead(CMemorySave &_alreadyRead);
    virtual bool SendData(const char *pszData, int nLen) = 0;

public:
    virtual bool HasPacket();
    virtual int GetInputPacketsNum();
    virtual bool Send(CPacket &packet);
    virtual bool Recv(CPacket &packet);
};

// --------------------------------------------------------------------------------------------
// unreliable media network interface
// --------------------------------------------------------------------------------------------

const int N_MAX_UNREL_PACKET_LENGTH = 8192;
// --------------------------------------------------------------------------------------------
struct SUNPacket
{
    DWORD dwPacketNum;
    char cData[N_MAX_UNREL_PACKET_LENGTH];
    int nRealPacketSize;
    int GetSendSize() const { return nRealPacketSize + sizeof(dwPacketNum); }
};
// --------------------------------------------------------------------------------------------
struct SUNOutPacket
{
    DWORD dwLastSentTime, dwFirstTime;
    SUNPacket data;
};
// --------------------------------------------------------------------------------------------
class CUnreliableConnection : public CStreamConnection
{
    CList<SUNOutPacket> outList;
    CList<SUNPacket> inList;
    DWORD dwOutPktNum, dwInPktNum;
    DWORD dwCurrentPing;
    static DWORD dwCurrentTime;

protected:
    virtual int GetMaxPacketSize() = 0;
    virtual bool SendPacket(void *pszData, int nLen) = 0;
    void PacketReceived(const void *pszData, int nLen);

    virtual bool SendData(const char *pszData, int nLen);

public:
    void ResendPackets();
    CUnreliableConnection()
    {
        dwOutPktNum = 1;
        dwInPktNum = 1;
        dwCurrentPing = 1000;
    }
    DWORD GetPing() { return dwCurrentPing; }
};
// --------------------------------------------------------------------------------------------
// Driver
// --------------------------------------------------------------------------------------------
class CUnreliableMediaNetDriver : public CLowLevelNetDriver
{
protected:
    CList<CObj<CUnreliableConnection>, CUnreliableConnection *> connections;

    void RemoveInvalidConnections();

public:
    virtual void Segment();
};