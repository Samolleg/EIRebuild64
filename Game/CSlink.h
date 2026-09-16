#pragma once

#include <winsock.h>
#include <vector>
#include "streams.h"
/////////////////////////////////////////////////////////////////////////////////////
class CMemoryStream;
/////////////////////////////////////////////////////////////////////////////////////
// адрес хоста для передачи данных
class CCSNodeAddress
{
    sockaddr addr;

public:
    CCSNodeAddress() { memset(&addr, 0, sizeof(addr)); }
    bool SetInetName(const char *pszHost, int nDefaultPort);
    std::string GetName(bool bResolve = true) const;
    std::string GetFastName() const { return GetName(false); }
    bool operator==(const CCSNodeAddress &a) const
    {
        return memcmp(&addr, &a.addr, sizeof(addr)) == 0;
    }
    bool operator!=(const CCSNodeAddress &a) const
    {
        return memcmp(&addr, &a.addr, sizeof(addr)) != 0;
    }
    void Clear() { memset(&addr, 0, sizeof(addr)); }
    bool SameIP(const CCSNodeAddress &a) const
    {
        return memcmp(((char *)&a.addr) + 4, ((char *)&addr) + 4, 4) == 0;
    }
    unsigned int GetIP() const { return ((unsigned int *)(&addr))[1]; }
    friend class CCSLinksManager;
    friend class CTCPRequest;
};
/////////////////////////////////////////////////////////////////////////////////////
// abstraction from messaging level
class CCSLinksManager
{
    SOCKET s;
    std::vector<CCSNodeAddress> broadcastAddr;

public:
    CCSLinksManager();
    ~CCSLinksManager();
    bool Start(int nPort);
    void Finish();
    bool MakeBroadcastAddr(CCSNodeAddress &res, unsigned int nNAddr, int nPort);
    bool IsLocalAddr(const CCSNodeAddress &test);
    bool Send(const CCSNodeAddress &dst, CMemoryStream &pkt);
    bool Recv(CCSNodeAddress &src, CMemoryStream &pkt);
};
///////////////////////////////////////////////////////////////////////////////////////////////////
class CTCPRequest
{
public:
    enum EState
    {
        FAILURE,
        CONNECTING,
        TRANSFER,
        COMPLETE,
        WAITINGWRITABLE
    };
    CMemoryStream request, res; // CRAP - requests are expected to fit buffer or GetState will block
    //
    CTCPRequest(const CCSNodeAddress &addr);
    ~CTCPRequest();
    EState GetState();
    bool IsComplete() { return GetState() == COMPLETE; }

private:
    SOCKET s;
    EState state;
    CTCPRequest(const CTCPRequest &a) { ASSERT(0); }
    CTCPRequest &operator=(const CTCPRequest &a)
    {
        ASSERT(0);
        return *this;
    }
    void Finish(EState newState = FAILURE);
};
/////////////////////////////////////////////////////////////////////////////////////
