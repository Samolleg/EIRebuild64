/**********************************************************************
 *<
        FILE: Net.cpp

        DESCRIPTION:  Network basic interface

        CREATED BY: Andy Gulin

 *>	Copyright (c) 1999, Nival Enterntainemnt
 **********************************************************************/

#include "StdAfx.h"
#include "RuntimePaths64.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "Net.h"

/////////////////////////////////////////////////////////////////////////////////////
// CConnection
///////////////////////////////////////////////////////////////////////////////////////////////////////////
CConnection::CConnection()
{
    bStoreTraffic = FALSE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CConnection::StartTrafficSaving(const char *fname)
{
    ASSERT(!bStoreTraffic);
    trafficSaver.OpenWrite(RuntimePaths64::Log(std::filesystem::path(fname).filename().string().c_str()).string().c_str());
    bSkipSegment = TRUE;
    bStoreTraffic = TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CConnection::StopTrafficSaving()
{
    ASSERT(bStoreTraffic);
    trafficSaver.CloseFile();
    bStoreTraffic = FALSE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
bool CConnection::Send(CPacket &packet)
{
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
bool CConnection::Recv(CPacket &packet)
{
    if (bStoreTraffic)
    {
        if (bSkipSegment)
            bSkipSegment = FALSE;
        else
            trafficSaver.Write(packet);
    }
    return TRUE;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// CLocalConnection
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CLocalConnection::Disconnect()
{
    CPtr<CLocalConnection> pTempPeer = pPeer;
    pPeer = 0;
    if (pTempPeer)
        pTempPeer->Disconnect();
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CLocalConnection::Connect(CLocalConnection *pCon)
{
    ASSERT(!pPeer->IsValid());
    pPeer = pCon;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
bool CLocalConnection::Send(CPacket &packet)
{
    CConnection::Send(packet);
    ASSERT(pPeer->IsValid());
    if (!pPeer->IsValid())
        return false;
    pPeer->nextPacket.emplace_back();
    pPeer->nextPacket.back().CopyFrom(packet);
    return true;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
bool CLocalConnection::Recv(CPacket &packet)
{
    if (nextPacket.empty())
        return false;
    packet.CopyFrom(nextPacket.front());
    nextPacket.pop_front();
    CConnection::Recv(packet);
    return true;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
CLocalConnection *CLocalNetDriver::Connect(CLocalNetDriver *pServerNetDriver)
{
    CLocalConnection *pConnect;
    ASSERT(pServerNetDriver);
    if (!pServerNetDriver)
        return NULL;
    ASSERT(!pServerNetDriver->pNewConnection->IsValid());
    pServerNetDriver->pNewConnection = dbgnew CLocalConnection;
    pConnect = dbgnew CLocalConnection;
    pServerNetDriver->pNewConnection->Connect(pConnect);
    pConnect->Connect(pServerNetDriver->pNewConnection);
    return pConnect;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CLocalNetDriver::GetNewPlayers(CConnectionsList &newList)
{
    if (pNewConnection)
        newList.AddTail(pNewConnection);
    pNewConnection = 0;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
