#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "CSlink.h"
#include "Streams.h"
/////////////////////////////////////////////////////////////////////////////////////
// CCSNodeAddress
/////////////////////////////////////////////////////////////////////////////////////
bool CCSNodeAddress::SetInetName(const char *pszHost, int nDefaultPort)
{
    int nIdx, nPort = nDefaultPort;
    memset(&addr, 0, sizeof(addr));
    ASSERT(sizeof(addr) >= sizeof(sockaddr_in));
    sockaddr_in &nameRemote = *(sockaddr_in *)&addr;

    nameRemote.sin_family = AF_INET;
    // extract port number from address
    string szAddr = pszHost;
    nIdx = szAddr.find(':');
    if (nIdx != -1)
    {
        nPort = atoi(string(szAddr, nIdx + 1).c_str());
        szAddr = string(szAddr, 0, nIdx);
    }
    // determine host
    nameRemote.sin_addr.S_un.S_addr = inet_addr(szAddr.c_str());
    if (nameRemote.sin_addr.S_un.S_addr == INADDR_NONE) // not resolved?
    {
        hostent *he;
        he = gethostbyname(szAddr.c_str()); // m.b. it is string comp.domain
        if (he == NULL)
            return false;
        nameRemote.sin_addr.S_un.S_addr = *(unsigned long *)(he->h_addr_list[0]);
    }
    nameRemote.sin_port = htons(nPort);
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
string CCSNodeAddress::GetName(bool bResolve) const
{
    sockaddr_in &nameRemote = *(sockaddr_in *)&addr;

    hostent *he = 0;
    if (bResolve)
        he = gethostbyaddr((const char *)&addr, sizeof(addr),
                           AF_INET); // m.b. it is string comp.domain
    char szBuf[1024];
    if (he == 0 || he->h_name == 0)
    {
        in_addr &ia = nameRemote.sin_addr;
        sprintf(szBuf, "%i.%i.%i.%i:%i", (int)ia.S_un.S_un_b.s_b1, (int)ia.S_un.S_un_b.s_b2,
                (int)ia.S_un.S_un_b.s_b3, (int)ia.S_un.S_un_b.s_b4,
                (int)ntohs(nameRemote.sin_port));
    }
    else
    {
        sprintf(szBuf, "%s:%i", (const char *)he->h_name, (int)ntohs(nameRemote.sin_port));
    }
    return szBuf;
}
/////////////////////////////////////////////////////////////////////////////////////
// CCSLinksManager
/////////////////////////////////////////////////////////////////////////////////////
CCSLinksManager::CCSLinksManager()
{
    WORD wVersionRequested = MAKEWORD(1, 1);
    WSADATA wsaData;

    int bRv = WSAStartup(wVersionRequested, &wsaData) == 0;
    ASSERT(bRv);

    s = INVALID_SOCKET;
    // get host for broadcast addresses formation
    char szHost[1024];
    if (gethostname(szHost, 1000))
    {
        ASSERT(0);
        return;
    }
    hostent *he;
    he = gethostbyname(szHost); // m.b. it is string comp.domain
    if (he == NULL)
    {
        ASSERT(0);
        return;
    }
    // form addresses
    CCSNodeAddress addr;
    sockaddr_in &name = *(sockaddr_in *)&addr;
    name.sin_family = AF_INET;
    unsigned long *pAddr = (unsigned long *)(he->h_addr_list[0]);
    for (; *pAddr; pAddr++)
    {
        name.sin_addr.S_un.S_addr = pAddr[0];
        unsigned char bClass = name.sin_addr.S_un.S_un_b.s_b1;
        if (bClass >= 1 && bClass <= 126)
        {
            name.sin_addr.S_un.S_un_b.s_b2 = 255;
            name.sin_addr.S_un.S_un_b.s_b3 = 255;
            name.sin_addr.S_un.S_un_b.s_b4 = 255;
        }
        if (bClass >= 128 && bClass <= 191)
        {
            name.sin_addr.S_un.S_un_b.s_b3 = 255;
            name.sin_addr.S_un.S_un_b.s_b4 = 255;
        }
        if (bClass >= 192 && bClass <= 223)
            name.sin_addr.S_un.S_un_b.s_b4 = 255;
        broadcastAddr.push_back(addr);
        break; // hostent are broken for some unknown reason
    }
}
/////////////////////////////////////////////////////////////////////////////////////
CCSLinksManager::~CCSLinksManager()
{
    Finish();
    WSACleanup();
}
/////////////////////////////////////////////////////////////////////////////////////
bool CCSLinksManager::Start(int nPort)
{
    Finish();
    s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET)
        return false;
    sockaddr_in name;
    memset(&name, 0, sizeof(name));
    name.sin_family = AF_INET;
    name.sin_addr.S_un.S_addr = INADDR_ANY;
    name.sin_port = htons(nPort);
    if (nPort > 0)
    {
        if (bind(s, (sockaddr *)&name, sizeof(name)) != 0)
        {
            closesocket(s);
            s = INVALID_SOCKET;
            return false;
        }
    }
    DWORD dwOpt = 1;
    ioctlsocket(s, FIONBIO, &dwOpt); // no block
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char *)&dwOpt, 4);
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSLinksManager::Finish()
{
    if (s != INVALID_SOCKET)
        closesocket(s);
    s = INVALID_SOCKET;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CCSLinksManager::MakeBroadcastAddr(CCSNodeAddress &res, unsigned int nNAddr, int nPort)
{
    if (nNAddr >= broadcastAddr.size())
        return false;
    res = broadcastAddr[nNAddr];
    sockaddr_in &name = *(sockaddr_in *)&res.addr;
    name.sin_port = htons(nPort);
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CCSLinksManager::IsLocalAddr(const CCSNodeAddress &test)
{
    const sockaddr_in &nt = *(sockaddr_in *)&test.addr;
    if (nt.sin_addr.S_un.S_addr == 0x0100007f)
        return true;
    for (int i = 0; i < broadcastAddr.size(); ++i)
    {
        CCSNodeAddress &broad = broadcastAddr[i];
        const sockaddr_in &nb = *(sockaddr_in *)&broad.addr;
        DWORD dwB = nb.sin_addr.S_un.S_addr;
        DWORD dwT = nt.sin_addr.S_un.S_addr;
        DWORD dwMask = 0;
        for (int k = 3; k >= 0; k--)
        {
            DWORD dwTestMask = 0xFF << k * 8;
            if ((dwB & dwTestMask) == dwTestMask)
                dwMask |= dwTestMask;
            else
                break;
        }
        dwMask = ~dwMask;
        bool bTest = (dwB & dwMask) == (dwT & dwMask);
        if (bTest)
            return true;
    }
    return false;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CCSLinksManager::Send(const CCSNodeAddress &dst, CMemoryStream &pkt)
{
    int nSize = pkt.GetSize();
    int nRv = sendto(s, (const char *)pkt.GetBuffer(), nSize, 0, &dst.addr, sizeof(dst.addr));
    return nRv == nSize;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CCSLinksManager::Recv(CCSNodeAddress &src, CMemoryStream &pkt)
{
    int nAddrSize;
    pkt.Seek(2048);
    nAddrSize = sizeof(src.addr);
    int nRes = recvfrom(s, (char *)pkt.GetBufferForWrite(), 2048, 0, &src.addr, &nAddrSize);
    if (nRes >= 0)
    {
        src.addr.sa_family = AF_INET; // somehow this gets spoiled on win2k
        memset(src.addr.sa_data + 6, 0, 8);
        pkt.SetSize(nRes);
    }
    return nRes >= 0;
}
/////////////////////////////////////////////////////////////////////////////////////
// CTCPRequest
/////////////////////////////////////////////////////////////////////////////////////
CTCPRequest::CTCPRequest(const CCSNodeAddress &addr)
{
    s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET)
    {
        state = FAILURE;
        return;
    }

    sockaddr_in nameLocal;
    //
    memset(&nameLocal, 0, sizeof(nameLocal));
    nameLocal.sin_family = AF_INET;
    nameLocal.sin_addr.S_un.S_addr = INADDR_ANY;
    nameLocal.sin_port = 0;
    if (bind(s, (sockaddr *)&nameLocal, sizeof(nameLocal)) != 0)
    {
        Finish();
        return;
    }
    DWORD dwNoBlock = 1;
    ioctlsocket(s, FIONBIO, &dwNoBlock);
    connect(s, &addr.addr, sizeof(addr.addr));
    state = CONNECTING;
    // set connection`s params
    // int nSndBufSize = TCP_BUFFER_SIZE;
    // setsockopt( sResult, SOL_SOCKET, SO_SNDBUF, ( char* )&nSndBufSize, sizeof( int ) );
    // setsockopt( sResult, SOL_SOCKET, SO_RCVBUF, ( char* )&nSndBufSize, sizeof( int ) );

    // bool bTemp = true;
    // setsockopt( sResult, IPPROTO_TCP, TCP_NODELAY, ( char* )&bTemp, sizeof( bool ) );
}
/////////////////////////////////////////////////////////////////////////////////////
CTCPRequest::~CTCPRequest()
{
    if (s != INVALID_SOCKET)
        closesocket(s);
}
/////////////////////////////////////////////////////////////////////////////////////
void CTCPRequest::Finish(EState newState)
{
    if (s != INVALID_SOCKET)
        closesocket(s);
    s = INVALID_SOCKET;
    state = newState;
}
/////////////////////////////////////////////////////////////////////////////////////
static timeval timeout = { 0, 0 };
/////////////////////////////////////////////////////////////////////////////////////
CTCPRequest::EState CTCPRequest::GetState()
{
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(s, &fds);
    switch (state)
    {
    case CONNECTING:
        if (select(0, 0, &fds, 0, &timeout) == 1)
            state = WAITINGWRITABLE;
        else if (select(0, 0, 0, &fds, &timeout) == 1)
            Finish();
        break;
    case WAITINGWRITABLE:
        if (select(0, 0, &fds, 0, &timeout) == 1)
        {
            state = TRANSFER;
            DWORD dwNoBlock = 0;
            ioctlsocket(s, FIONBIO, &dwNoBlock);
            int nRv = send(s, (const char *)request.GetBuffer(), request.GetSize(), 0);
            dwNoBlock = 1;
            ioctlsocket(s, FIONBIO, &dwNoBlock);
            if (nRv != request.GetSize())
                Finish();
        }
        else if (select(0, 0, 0, &fds, &timeout) == 1)
            Finish();
        break;
    case TRANSFER:
        if (select(0, &fds, 0, 0, &timeout) == 1)
        {
            char szBuf[2048];
            int nRv = recv(s, szBuf, 2048, 0);
            switch (nRv)
            {
            case SOCKET_ERROR:
                Finish();
                break;
            case 0:
                Finish(COMPLETE);
                break;
            default:
                res.Seek(res.GetSize());
                res.Write(szBuf, nRv);
                break;
            }
        }
        else if (select(0, 0, 0, &fds, &timeout) == 1)
            Finish();
        break;
    }
    return state;
}
/////////////////////////////////////////////////////////////////////////////////////
