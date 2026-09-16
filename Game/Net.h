#pragma once
/**********************************************************************
 *<
        FILE: Net.H

        DESCRIPTION:  Network basic interface

        CREATED BY: Andy Gulin

 *>	Copyright (c) 1999, Nival Enterntainemnt
 **********************************************************************/

#include "Basic.h"
#include "NetUpdate.h"
#include <list>

// --------------------------------------------------------------------------------------------
// соединение с удаленным объектом, позволяет проверять наличие соединения, проверять
// наличие пакета, передавать и принимать данные
// --------------------------------------------------------------------------------------------
class CConnection : public CObjectBase
{
private:
    bool bStoreTraffic, bSkipSegment;
    CFileSave trafficSaver;

public:
    CConnection();
    void StartTrafficSaving(const char *fname);
    void StopTrafficSaving();
    bool IsStoringTraffic() const { return bStoreTraffic; }
    virtual bool CanSend() = 0;
    virtual bool CanRecv() = 0;
    virtual bool HasPacket() = 0; // is there a packet after receivedPacket
    virtual int GetInputPacketsNum() { return HasPacket() ? 1 : 0; }
    virtual bool Send(CPacket &packet);
    virtual bool Recv(CPacket &packet);
    virtual void Segment() {} // to help sending data through non guaranteeing media
    virtual void Disconnect() = 0;
    virtual DWORD GetPing() { return 0; }
};
typedef CList<CPtr<CConnection>, CConnection *> CConnectionsList;

// --------------------------------------------------------------------------------------------
// управление связью с удаленным объектом
// --------------------------------------------------------------------------------------------
class CLowLevelNetDriver : public CObjectBase
{
public:
    virtual bool StartServer() = 0;
    virtual void Segment() {}
    virtual void GetNewPlayers(CConnectionsList &newList) = 0;
};

// --------------------------------------------------------------------------------------------
// случай с клиентом и сервером на одной машине
// --------------------------------------------------------------------------------------------
class CLocalConnection : public CConnection
{
    OBJECT_BASIC_METHODS(CLocalConnection);

private:
    std::list<CPacket> nextPacket;

public:
    CPtr<CLocalConnection> pPeer;

    CLocalConnection() {}
    virtual bool CanSend() { return pPeer->IsValid(); }
    virtual bool CanRecv() { return !nextPacket.empty() || pPeer->IsValid(); }
    virtual bool HasPacket() { return !nextPacket.empty(); }
    virtual int GetInputPacketsNum() { return nextPacket.size(); }
    virtual bool Send(CPacket &packet);
    virtual bool Recv(CPacket &packet);
    virtual void Disconnect();
    void Connect(CLocalConnection *pCon);
};

// --------------------------------------------------------------------------------------------
class CLocalNetDriver : public CLowLevelNetDriver
{
    OBJECT_BASIC_METHODS(CLocalNetDriver);

private:
    CPtr<CLocalConnection> pNewConnection;

public:
    virtual bool StartServer() { return false; }
    CLocalConnection *Connect(CLocalNetDriver *pServerNetDriver);
    virtual void GetNewPlayers(CConnectionsList &newList);
};
// --------------------------------------------------------------------------------------------
/*
   Improvement plan
6)-ability for connection to pack/crypt data
7)-initializing of encrypted/packed session
*/
/*
class CPlayer
{
  CConnection *pConnection;
};

class CWorldServer
{
        CLowLevelDriver *lld;
};

class CWorldClient
{
  CPlayerClient *player;

};
*/
