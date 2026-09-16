#pragma once

#include "CSbase.h"
#include "BasicChunk.h"
/////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////
class CCSServerInfo : public CObjectBase
{
public:
    enum EType
    {
        ST_Local,
        ST_AddressBook,
        ST_Master
    };
    CCSNodeAddress addr;
    float fpValidTimeLeft; // how much time left until server info expires//TimeSinceUpdate;
    float fpPing;          // ping to server in ms
    bool bWrongVersion;    // to catch different version
    EType type;

    CCSServerInfo();
    // version, number of players and other stuff...
    virtual void UpdateTo(CBitStream &bits) = 0;
    virtual void UpdateFrom(CBitStream &bits) = 0;
};
/////////////////////////////////////////////////////////////////////////////////////
class CCSAddressBook
{
public:
    struct SServer
    {
        CCSNodeAddress addr;
        CMemoryStream info;
        //
        void Serialize(CStructureSaver &saver);
    };
    std::vector<SServer> servers;
    unsigned int nUniqueClientID; // some identifier to separate clients
    //
    CCSAddressBook() { nUniqueClientID = 0; }
    std::vector<SServer>::iterator GetServer(const CCSNodeAddress &addr);
    bool HasAddr(const CCSNodeAddress &addr) { return GetServer(addr) != servers.end(); }
    void Serialize(CStructureSaver &saver);
};
/////////////////////////////////////////////////////////////////////////////////////
// client set of objects
class CCSClient : public CCSBase
{
    enum EState
    {
        S_NONE,
        S_LOGIN,
        S_ACTIVE
    };
    CCSNodeAddress server;
    CMemoryStream serverPassword;
    CS_LINKID clientLinkID;
    EState state;
    char nRejectReason;
    // увеличивающаяся задержка при посылке запроса на соединение, в S_LOGIN state only
    float fpLoginTimeLeft, fpLoginInterval;
    typedef std::list<CObj<CCSServerInfo>> CServerInfoList;
    CServerInfoList serverInfos; // contains currently available servers
    CCSAddressBook book;
    typedef std::list<CCSNodeAddress> CServerReqList;
    CServerReqList requestList;
    CCSNodeAddress activeServer; // server to which user is pointing right now
    unsigned nLastSentServerReq; // to whom from book last request was sent

    // server commands
    void CmdSGetInfo(const CCSNodeAddress &addr);
    void CmdSLogin(const CCSNodeAddress &addr, CMemoryStream &pwd);
    CServerInfoList::iterator GetServerInfo(const CCSNodeAddress &addr);

protected:
    virtual void RemoveClient(CClientList::iterator i);
    // server messages
    virtual bool ProcessNormal(const CCSNodeAddress &addr, CBitStream &bits);
    virtual void ProcessInfo(const CCSNodeAddress &addr, CBitStream &bits);
    virtual void ProcessAccepted(const CCSNodeAddress &addr, CBitStream &bits);
    virtual void ProcessRejected(const CCSNodeAddress &addr, CBitStream &bits);
    virtual void OnConnected() {}
    //
    virtual void NewServerInfo(CObj<CCSServerInfo> &dest) = 0;

public:
    //
    CCSClient(int nPort = -1)
    {
        state = S_NONE;
        clientLinkID = 0;
        StartNet(nPort);
    }
    // connecting and server requests
    void SendServerReqs();
    void SetAddressBook(CCSAddressBook &book);
    CCSAddressBook &GetAddressBook() { return book; }
    void AddAddressToBook(const CCSNodeAddress &addr);
    void RemoveAddressFromBook(const CCSNodeAddress &addr);
    void AddServerFromMaster(CCSServerInfo *pInfo);
    void SetActiveServer(const CCSNodeAddress &addr);
    void ConnectServer(const CCSNodeAddress &addr, const CMemoryStream &pwd);
    bool IsConnected() { return state == S_ACTIVE; }
    bool IsConnecting() { return state == S_LOGIN; }
    bool IsWeakConnection();
    const CCSServerInfo *GetServerInfo(int nIdx);
    char GetRejectReason() const { return nRejectReason; }
    //
    virtual void TimePassed(double fpSeconds);
};
/////////////////////////////////////////////////////////////////////////////////////
// master set of all objects
class CCSServer : public CCSBase
{
    struct SAcceptWaiter
    {
        CCSNodeAddress addr;
        CS_LINKID linkID;
        int nRate;
        float fpTime;

        SAcceptWaiter() {}
        SAcceptWaiter(const CCSNodeAddress &_addr, CS_LINKID _linkID, int _nRate, float _fpTime)
            : addr(_addr)
            , linkID(_linkID)
            , nRate(_nRate)
            , fpTime(_fpTime)
        {
        }
    };
    typedef std::list<SAcceptWaiter> CAcceptList;
    CAcceptList accepted;
    //
    CAcceptList::iterator GetAccepted(const CCSNodeAddress &addr);
    //
    // sending information to clients
    void SendServerInformation(const CCSNodeAddress &addr, float fpReqTime);
    void SendAccepted(const CCSNodeAddress &addr, CS_LINKID linkID);
    void SendRejected(const CCSNodeAddress &addr, CS_LINKID linkID, char bReason);
    // client messages
    virtual bool ProcessNormal(const CCSNodeAddress &addr, CBitStream &bits);
    virtual void ProcessGetInfo(const CCSNodeAddress &addr, CBitStream &bits);
    virtual void ProcessLogin(const CCSNodeAddress &addr, CBitStream &bits);

protected:
    int GetAcceptedCount() const { return accepted.size(); }
    virtual void CreateServerInfo(CObj<CCSServerInfo> &pRes) = 0;
    virtual bool CheckLogin(const CCSNodeAddress &addr, CBitStream &file, char &bRejectReason);

public:
    CCSServer(int nServerPort = -1)
    {
        StartNet(nServerPort > 0 ? nServerPort : NCSParams::nCSServerPort);
    }
    virtual void TimePassed(double fpSeconds);
};
/////////////////////////////////////////////////////////////////////////////////////
