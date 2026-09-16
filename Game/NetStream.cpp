#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "NetStream.h"
#include "tools.h"
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// CStreamConnection
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CStreamConnection::NewDataIncome(const char *pszData, int nLen)
{
    readPkt.Seek(readPkt.GetSize());
    readPkt.Write(pszData, nLen);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CStreamConnection::ReturnDataRead(CMemorySave &_alreadyRead)
{
    _alreadyRead.Clear();
    if (readPkt.GetSize() > 0)
    {
        _alreadyRead.Write(readPkt.GetBuffer(), readPkt.GetSize());
        readPkt.Clear();
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
bool CStreamConnection::HasPacket()
{
    int nSize;
    if (readPkt.GetSize() < 4)
        return false;
    readPkt.Seek(0);
    readPkt.Read(&nSize, 4);
    return readPkt.GetSize() >= nSize + 4;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
int CStreamConnection::GetInputPacketsNum()
{
    int nSize, nRes = 0;
    readPkt.Seek(0);
    for (;;)
    {
        if (readPkt.GetPosition() + 4 > readPkt.GetSize())
            return nRes;
        readPkt.Read(&nSize, 4);
        if (readPkt.GetPosition() + nSize > readPkt.GetSize() || nSize <= 0)
            return nRes;
        readPkt.Seek(readPkt.GetPosition() + nSize);
        nRes++;
    }
    return nRes;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
bool CStreamConnection::Send(CPacket &packet)
{
    char buf[N_MAX_PACKET_LENGTH];
    // make up a packet for sending
    ASSERT(packet.GetSize() < N_MAX_PACKET_LENGTH - 4);
    if (packet.GetSize() > N_MAX_PACKET_LENGTH - 4)
        return false;
    ((int *)buf)[0] = packet.GetSize();
    packet.Seek(0);
    packet.Read(buf + 4, packet.GetSize());
    if (!SendData(buf, packet.GetSize() + 4))
    {
        Disconnect();
        return false;
    }
    return true;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
bool CStreamConnection::Recv(CPacket &packet)
{
    ASSERT(HasPacket());
    int nSize = 0, nNewSize;
    readPkt.Seek(0);
    readPkt.Read(nSize);
    if (readPkt.GetSize() < nSize + 4)
        return false;
    packet.SetSize(nSize);
    readPkt.Read(packet.GetBufferForWrite(), nSize);
    nNewSize = readPkt.GetSize() - nSize - 4;
    memmove(readPkt.GetBufferForWrite(), readPkt.GetBuffer() + nSize + 4, nNewSize);
    readPkt.SetSize(nNewSize);
    return true;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// CUnreliableConnection
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// packets for large transmissions tesselation && transmitting data over unreliable media
DWORD CUnreliableConnection::dwCurrentTime;
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CUnreliableConnection::PacketReceived(const void *pszData, int nLen)
{
    DWORD dwPktNum, dwPing;
    dwCurrentTime = timeGetTime();
    // incorrect packet arrival
    if (nLen < 4)
        return;
    // if acknowledgement received - correct ping time with new value
    memcpy(&dwPktNum, pszData, 4);
    if (nLen == 4)
    {
        for (POSITION pos = outList.GetHeadPosition(); pos;)
        {
            POSITION oldPos = pos;
            SUNOutPacket &pkt = outList.GetNext(pos);
            if (pkt.data.dwPacketNum != dwPktNum)
                continue;
            // ack for the packet pkt arrived
            dwPing = dwCurrentTime - pkt.dwLastSentTime;
            // isn`t it a strange ping ?
            if (dwPing < dwCurrentPing / 2)
            {
                // this ack is real rocket so patch up ping time
                if (pkt.dwFirstTime == pkt.dwLastSentTime)
                    dwCurrentPing = dwPing;
            }
            else
                dwCurrentPing = (dwCurrentPing * 7 + dwPing) / 8;
            outList.RemoveAt(oldPos);
        }
        return;
    }
    // new data arrived - queue it into inList, process it to see if its possible
    // to combine some data to stream, send ack to the packet
    SendPacket(&dwPktNum, 4);
    // this packet has been received already?
    if (dwPktNum < dwInPktNum)
        return;
    for (POSITION pos = inList.GetHeadPosition(); pos;)
    {
        SUNPacket &pkt = inList.GetNext(pos);
        if (pkt.dwPacketNum == dwPktNum)
            return;
    }
    // isn`t this a packet we waited so long ?
    if (dwPktNum == dwInPktNum)
    {
        NewDataIncome(((const char *)pszData) + 4, nLen - 4);
        dwInPktNum++;
        for (;;)
        {
            bool bContinue = false;
            for (POSITION pos = inList.GetHeadPosition(); pos;)
            {
                POSITION oldPos = pos;
                SUNPacket &pkt = inList.GetNext(pos);
                if (pkt.dwPacketNum == dwInPktNum)
                {
                    NewDataIncome(pkt.cData, pkt.nRealPacketSize);
                    inList.RemoveAt(oldPos);
                    dwInPktNum++;
                    bContinue = true;
                    break;
                }
            }
            if (!bContinue)
                break;
        }
    }
    else
    {
        // arrived data is really something new, place it to queue
        SUNPacket &newPkt = inList.GetAt(inList.AddHead(SUNPacket()));
        memcpy(&newPkt, pszData, nLen);
        newPkt.nRealPacketSize = nLen - 4;
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
bool CUnreliableConnection::SendData(const char *pszData, int nLen)
{
    int nPktSize = GetMaxPacketSize();
    dwCurrentTime = timeGetTime();
    while (nLen > 0)
    {
        int nSendLen = Min(nPktSize, nLen);
        // allocate new packet in list
        SUNOutPacket &pktNew = outList.GetAt(outList.AddTail(SUNOutPacket()));
        pktNew.data.dwPacketNum = dwOutPktNum++;
        pktNew.data.nRealPacketSize = nSendLen;
        memcpy(pktNew.data.cData, pszData, nSendLen);

        pktNew.dwFirstTime = dwCurrentTime;
        pktNew.dwLastSentTime = dwCurrentTime;
        if (!SendPacket(&pktNew.data, pktNew.data.GetSendSize()))
            return false;

        nLen -= nSendLen;
        pszData += nSendLen;
    }
    return true;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CUnreliableConnection::ResendPackets()
{
    dwCurrentTime = timeGetTime();
    for (POSITION pos = outList.GetHeadPosition(); pos;)
    {
        SUNOutPacket &pkt = outList.GetNext(pos);
        if (dwCurrentTime - pkt.dwLastSentTime > N_KEEP_ALIVE_TIMEOUT)
        {
            Disconnect();
            return;
        }
        if (dwCurrentTime - pkt.dwLastSentTime > dwCurrentPing)
        {
            pkt.dwLastSentTime = dwCurrentTime;
            SendPacket(&pkt.data, pkt.data.GetSendSize());
        }
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CUnreliableMediaNetDriver::Segment()
{
    for (POSITION pos = connections.GetHeadPosition(); pos;)
    {
        CUnreliableConnection *pCon = connections.GetNext(pos);
        pCon->ResendPackets();
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CUnreliableMediaNetDriver::RemoveInvalidConnections()
{
    for (POSITION pos = connections.GetHeadPosition(); pos;)
    {
        POSITION delPos = pos;
        CUnreliableConnection *pCon = connections.GetNext(pos);
        if (!pCon->CanRecv() && !pCon->CanSend())
            connections.RemoveAt(delPos);
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
