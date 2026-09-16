#pragma once
/**********************************************************************
 *<
        FILE: NetTcp.H

        DESCRIPTION:  TCP Network interface

        CREATED BY: Andy Gulin

 *>	Copyright (c) 1999, Nival Enterntainemnt
 **********************************************************************/

#include "NetStream.h"
#include "BasicM.h"
#include <winsock.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////
class CTcpSocket : public CFundament
{
    SOCKET s;

public:
    CTcpSocket(SOCKET _s);
    ~CTcpSocket();
    operator SOCKET() const { return s; }
};
///////////////////////////////////////////////////////////////////////////////////////////////////////////
class CTcpConnection : public CStreamConnection
{
    OBJECT_BASIC_METHODS(CTcpConnection);

private:
    CPtr<CTcpSocket> s;

    bool ReadData();

protected:
    virtual bool SendData(const char *pszData, int nLen);

public:
    virtual bool CanSend() { return s->IsValid() && *s != INVALID_SOCKET; }
    virtual bool CanRecv() { return HasPacket() || (s->IsValid() && *s != INVALID_SOCKET); }
    virtual void Disconnect();
    void CreateConnection(CTcpSocket *sckt)
    {
        Clear();
        s = sckt;
    }
    friend class CTcpNetDriver;
};
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// class is used by TcpNetDriver
///////////////////////////////////////////////////////////////////////////////////////////////////////////
class CTcpNetDriver : public CLowLevelNetDriver
{
    class CTcpConnector : public CMFundament
    {
    private:
        void GiveUp();

    public:
        CPtr<CTcpSocket> sResult;
        bool bInProgress;
        CString szAddr;
        CTcpConnector() { bInProgress = TRUE; }
        void RunConnect();
    };

    OBJECT_BASIC_METHODS(CTcpNetDriver);

protected:
private:
    typedef std::list<CObj<CTcpConnection>> CConList;
    typedef std::list<CPtr<CTcpConnection>> CPConList;
    CMPtr<CTcpConnector> pConnector;
    CConList connections;
    CPConList newConnections;
    CPtr<CTcpSocket> sAccept;

    void AcceptNewClient();

public:
    virtual bool StartServer();
    void StopServer();
    bool StartConnect(const char *pszServerHost);
    CTcpConnection *GetConnection(bool &bInProgress);
    void CancelConnect();
    virtual void Segment();
    virtual void GetNewPlayers(CConnectionsList &newList);
};
///////////////////////////////////////////////////////////////////////////////////////////////////////////
