#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "NetTcp.h"
#include "Notification.h"
#include <process.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////
// TCP порт на котором висит драйвер
#ifndef SET_TCP_PORT
const unsigned short LLD_DEFAULT_TCP_PORT = 8000;
#else
const unsigned short LLD_DEFAULT_TCP_PORT = SET_TCP_PORT;
#endif

const int N_MAX_TCP_PACKET_LENGTH = 32768;
const int TCP_BUFFER_SIZE = 50000;

///////////////////////////////////////////////////////////////////////////////////////////////////////////
struct STcpInit
{
    volatile static int nCloseCount;
    STcpInit()
    {
        BOOL bRv;
        WORD wVersionRequested = MAKEWORD(1, 1);
        WSADATA wsaData;

        bRv = WSAStartup(wVersionRequested, &wsaData) == 0;
        ASSERT(bRv);
    }
    ~STcpInit()
    {
        while (nCloseCount)
            Sleep(1000);
        WSACleanup();
    }
};
volatile int STcpInit::nCloseCount = 0;
///////////////////////////////////////////////////////////////////////////////////////////////////////////
static void __cdecl CloseSocketThread(void *pData)
{
    SOCKET s = (SOCKET)pData;
    struct SLinger
    {
        unsigned short l_onoff, l_linger;
    };
    SLinger linger;
    linger.l_onoff = 1;
    linger.l_linger = 60;
    setsockopt(s, SOL_SOCKET, SO_LINGER, (const char *)&linger, sizeof(linger));
    closesocket(s);
    STcpInit::nCloseCount--;
}
static STcpInit tcpInit;
///////////////////////////////////////////////////////////////////////////////////////////////////////////
static void DoCloseSocket(SOCKET s)
{
    STcpInit::nCloseCount++;
    _beginthread(CloseSocketThread, 102400, (void *)s);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// CTcpSocket
///////////////////////////////////////////////////////////////////////////////////////////////////////////
CTcpSocket::CTcpSocket(SOCKET _s)
{
    ASSERT(_s != INVALID_SOCKET);
    s = _s;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
CTcpSocket::~CTcpSocket()
{
    if (s != INVALID_SOCKET)
        DoCloseSocket(s);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// CTcpConnection
///////////////////////////////////////////////////////////////////////////////////////////////////////////
static timeval timeout = { 0, 0 };
///////////////////////////////////////////////////////////////////////////////////////////////////////////
bool CTcpConnection::SendData(const char *pszData, int nLen)
{
    int rv;
    fd_set checkwritable;
    if (!s->IsValid())
        return false;
    // check state of socket and send if ok
    FD_ZERO(&checkwritable);
    FD_SET(*s, &checkwritable);
    if (select(0, 0, &checkwritable, 0, &timeout) == 1)
    { // сокет готов к записи
        rv = send(*s, pszData, nLen, 0);
        if (rv != nLen)
            return false;
    }
    else
        return false;
    return true;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
bool CTcpConnection::ReadData()
{
    int nRv;
    if (!s->IsValid())
        return false;
    char szBuf[60000];
    nRv = recv(*s, szBuf, 60000, 0);
    if (nRv == 0 || nRv == SOCKET_ERROR)
    {
        Disconnect();
        return false;
    }
    NewDataIncome(szBuf, nRv);
    return true;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CTcpConnection::Disconnect()
{
    s = 0;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// CTcpNetDriver
///////////////////////////////////////////////////////////////////////////////////////////////////////////
bool CTcpNetDriver::StartServer()
{
    ASSERT(!sAccept->IsValid());
    if (sAccept->IsValid())
        return false;
    sAccept = dbgnew CTcpSocket(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (*sAccept == INVALID_SOCKET)
    {
        sAccept = 0;
        return false;
    }
    sockaddr_in name;
    memset(&name, 0, sizeof(name));
    name.sin_family = AF_INET;
    name.sin_addr.S_un.S_addr = INADDR_ANY;
    name.sin_port = htons(LLD_DEFAULT_TCP_PORT);
    if (bind(*sAccept, (sockaddr *)&name, sizeof(name)) != 0)
    {
        sAccept = 0;
        return false;
    }
    if (listen(*sAccept, 16) != 0)
    {
        sAccept = 0;
        return false;
    }
    return true;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// CTcpConnector
// thread for async connection to remote server
void CTcpNetDriver::CTcpConnector::GiveUp()
{
    sResult = 0;
    bInProgress = false;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CTcpNetDriver::CTcpConnector::RunConnect()
{
    int nIdx, nPort = LLD_DEFAULT_TCP_PORT;
    sResult = dbgnew CTcpSocket(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (*sResult == INVALID_SOCKET)
    {
        GiveUp();
        return;
    }

    sockaddr_in nameRemote, nameLocal;
    memset(&nameRemote, 0, sizeof(nameRemote));
    nameRemote.sin_family = AF_INET;
    // extract port number from address
    nIdx = szAddr.Find(':');
    if (nIdx != -1)
    {
        nPort = atoi(szAddr.Right(szAddr.GetLength() - nIdx - 1));
        szAddr = szAddr.Left(nIdx);
    }
    // determine host
    nameRemote.sin_addr.S_un.S_addr = inet_addr(szAddr);
    if (nameRemote.sin_addr.S_un.S_addr == INADDR_NONE) // not resolved?
    {
        hostent *he;
        he = gethostbyname(szAddr); // m.b. it is string comp.domain
        if (he == NULL)
        {
            GiveUp();
            return;
        }
        nameRemote.sin_addr.S_un.S_addr = *(unsigned long *)(he->h_addr_list[0]);
    }
    nameRemote.sin_port = htons(nPort);

    //
    memset(&nameLocal, 0, sizeof(nameLocal));
    nameLocal.sin_family = AF_INET;
    nameLocal.sin_addr.S_un.S_addr = INADDR_ANY;
    nameLocal.sin_port = 0;
    if (bind(*sResult, (sockaddr *)&nameLocal, sizeof(nameLocal)) != 0)
    {
        GiveUp();
        return;
    }
    if (connect(*sResult, (sockaddr *)&nameRemote, sizeof(nameRemote)) != 0)
    {
        GiveUp();
        return;
    }
    // set connection`s params
    int nSndBufSize = TCP_BUFFER_SIZE;
    setsockopt(*sResult, SOL_SOCKET, SO_SNDBUF, (char *)&nSndBufSize, sizeof(int));
    setsockopt(*sResult, SOL_SOCKET, SO_RCVBUF, (char *)&nSndBufSize, sizeof(int));

    DWORD dwNoBlock = 1;
    bool bTemp = true;
    // ioctlsocket( sResult, FIONBIO, &dwNoBlock );
    // setsockopt( sResult, IPPROTO_TCP, TCP_NODELAY, ( char* )&bTemp, sizeof( bool ) );

    // connection is complete
    bInProgress = false;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// start process of async connecting to pszServerHost
bool CTcpNetDriver::StartConnect(const char *pszServerHost)
{
    CancelConnect();
    ASSERT(!pConnector->IsValid());
    pConnector = dbgnew CTcpConnector;
    pConnector->szAddr = pszServerHost;
    pConnector->RunFunctionAsThread(&CTcpConnector::RunConnect);
    return true;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// get state of connecting process, if its finished connection will be created and returned
CTcpConnection *CTcpNetDriver::GetConnection(bool &bInProgress)
{
    CTcpConnection *pRes;
    if (pConnector->IsValid() && !pConnector->bInProgress)
    {
        if (!pConnector->sResult->IsValid())
            pRes = 0;
        else
        {
            pRes = dbgnew CTcpConnection;
            pRes->CreateConnection(pConnector->sResult);
            connections.push_back(MakeObj(pRes));
        }
        pConnector = 0;
        bInProgress = false;
        return pRes;
    }
    else
    {
        if (!pConnector->IsValid())
            pConnector = 0;
    }
    if (pConnector)
        bInProgress = true;
    return NULL;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// cancel process of async connection
void CTcpNetDriver::CancelConnect()
{
    pConnector = 0;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CTcpNetDriver::AcceptNewClient()
{
    sockaddr_in incomingAddr;
    int nIncomingAddrLen = sizeof(incomingAddr);

    SOCKET newSocket = accept(*sAccept, (sockaddr *)&incomingAddr, &nIncomingAddrLen);
    if (newSocket == INVALID_SOCKET)
    {
        ReportWarning("CTcpNetDriver::AcceptNewClient()\nUnable to accept new connection.\n");
        return;
    }
    /*	if (iMaxConnection - iNumConnection <= 0)
            {
                    closesocket( newSocket );
                    ReportWarning( "CLlDriverAcceptThreadTcp().\nClient connection refused.\n");
                    return;
            }*/
    // Disable buffering
    // bool bTemp = true;
    // setsockopt( newSocket, IPPROTO_TCP, TCP_NODELAY, ( char* )&bTemp, sizeof( bool ) );
    // Set 32k buffer for send and for receive
    int nSndBufSize = TCP_BUFFER_SIZE;
    setsockopt(newSocket, SOL_SOCKET, SO_SNDBUF, (char *)&nSndBufSize, sizeof(int));
    setsockopt(newSocket, SOL_SOCKET, SO_RCVBUF, (char *)&nSndBufSize, sizeof(int));
    // DWORD	dwNoBlock = 1;
    // ioctlsocket( newSocket, FIONBIO, &dwNoBlock );
    //	int nRetBufSize = sizeof( int );
    //	getsockopt( newSocket, SOL_SOCKET, SO_SNDBUF, ( char* )&nSndBufSize, &nRetBufSize );

    CTcpConnection *pRes = dbgnew CTcpConnection;
    pRes->CreateConnection(dbgnew CTcpSocket(newSocket));
    connections.push_back(MakeObj(pRes));
    newConnections.push_back(pRes);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// receive data for all tcp connections also look for new connections if in server mode
void CTcpNetDriver::Segment()
{
    CConList::iterator pos;
    // wipe out dead connections
    for (pos = connections.begin(); pos != connections.end();)
    {
        CTcpConnection *pCon = *pos;
        if (!pCon->CanSend() && !pCon->CanRecv())
            pos = connections.erase(pos);
        else
            ++pos;
    }

    for (;;)
    {
        int iRv;
        fd_set checkReadable;

        FD_ZERO(&checkReadable);
        if (sAccept->IsValid())
            FD_SET(*sAccept, &checkReadable);
        for (pos = connections.begin(); pos != connections.end(); ++pos)
        {
            if ((*pos)->CanSend()) // can send means socket is not erased
                FD_SET(*(*pos)->s, &checkReadable);
        }

        iRv = select(0, &checkReadable, 0, 0, &timeout);
        if (iRv == 0 || iRv == SOCKET_ERROR)
            break; // timeout или ошибка

        if (sAccept->IsValid() && FD_ISSET(*sAccept, &checkReadable))
            AcceptNewClient();
        for (pos = connections.begin(); pos != connections.end();)
        {
            CTcpConnection *pCon = *pos;
            if (pCon->CanSend())
            {
                if (FD_ISSET(*pCon->s, &checkReadable))
                {
                    if (!pCon->ReadData())
                    {
                        if (!pCon->CanSend() && !pCon->CanRecv())
                        {
                            pos = connections.erase(pos);
                            continue;
                        }
                    }
                }
            }
            ++pos;
        }
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CTcpNetDriver::StopServer()
{
    sAccept = 0;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// copy connections create for newplayers from temprorary array
void CTcpNetDriver::GetNewPlayers(CConnectionsList &newList)
{
    newList.RemoveAll();
    for (auto i = newConnections.begin(); i != newConnections.end(); ++i)
        newList.AddHead(*i);
    newConnections.clear();
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
