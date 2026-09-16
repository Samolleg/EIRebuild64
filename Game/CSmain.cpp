#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "CSmain.h"

using namespace NCSCommands;
using namespace std;
/////////////////////////////////////////////////////////////////////////////////////
// if info about server did not arrive in specified amount of time it is removed from list
const float FP_SERVER_LIST_TIMEOUT = 20;
const float FP_ACCEPTED_TIMEOUT = 60; // related to maximal rtt
const float FP_INVALID_PING = 9.999f;
/////////////////////////////////////////////////////////////////////////////////////
// CCSServerInfo
/////////////////////////////////////////////////////////////////////////////////////
CCSServerInfo::CCSServerInfo()
{
    fpValidTimeLeft = 0;
    fpPing = FP_INVALID_PING;
    bWrongVersion = false;
    type = ST_Local;
}
/////////////////////////////////////////////////////////////////////////////////////
// CCSAddressBook
/////////////////////////////////////////////////////////////////////////////////////
std::vector<CCSAddressBook::SServer>::iterator CCSAddressBook::GetServer(const CCSNodeAddress &addr)
{
    std::vector<SServer>::iterator i;
    for (i = servers.begin(); i != servers.end(); ++i)
    {
        if (i->addr == addr)
            return i;
    }
    return i;
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSAddressBook::SServer::Serialize(CStructureSaver &saver)
{
    saver.AddRaw(1, &addr, sizeof(addr));
    int nSize;
    if (saver.IsReading())
    {
        saver.Add(2, nSize);
        info.SetSize(nSize);
    }
    else
    {
        nSize = info.GetSize();
        saver.Add(2, nSize);
    }
    saver.AddRaw(3, info.GetBufferForWrite(), nSize);
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSAddressBook::Serialize(CStructureSaver &file)
{
    file.AddT(1, servers);
    file.Add(2, nUniqueClientID);
}
/////////////////////////////////////////////////////////////////////////////////////
// CCSClient
/////////////////////////////////////////////////////////////////////////////////////
void CCSClient::CmdSGetInfo(const CCSNodeAddress &addr)
{
    CMemoryStream pkt;
    pkt << CMDS_GET_INFO;
    pkt << (float)fpTime;
    SendUpdate(addr, pkt);
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSClient::CmdSLogin(const CCSNodeAddress &addr, CMemoryStream &pwd)
{
    int nRate = GetRate(addr);
    CMemoryStream pkt;
    pkt << CMDS_LOGIN;
    pkt << applicationID;
    pkt << clientLinkID;
    pkt << nRate;
    pkt.Write(pwd.GetBuffer(), pwd.GetSize());
    SendUpdate(addr, pkt);
}
/////////////////////////////////////////////////////////////////////////////////////
bool CCSClient::ProcessNormal(const CCSNodeAddress &addr, CBitStream &bits)
{
    // if we are not connected then
    if (state != S_ACTIVE)
    {
        if (pCSLog)
            (*pCSLog) << "ERROR: Normal update when not in active state" << endl;
        return false;
    }
    return CCSBase::ProcessNormal(addr, bits);
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSClient::ProcessAccepted(const CCSNodeAddress &addr, CBitStream &bits)
{
    CS_LINKID linkID;
    bits.Read(linkID);
    if (server != addr)
    {
        if (pCSLog)
            (*pCSLog) << "ERROR: Accepted msg received from unknown host " << addr.GetFastName()
                      << endl;
    }
    else
    {
        if (linkID != clientLinkID)
        {
            if (pCSLog)
                (*pCSLog) << "ERROR: Accepted msg received with wrong linkID" << endl;
            return;
        }
        if (state == S_LOGIN)
        {
            state = S_ACTIVE;
            AddClient(server, linkID, GetRate(server));
            if (pCSLog)
                (*pCSLog) << "Start connection" << endl;
            OnConnected();
        }
        else
        {
            if (pCSLog)
                (*pCSLog) << "ERROR: Accept for already started session from " << addr.GetFastName()
                          << endl;
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSClient::ProcessRejected(const CCSNodeAddress &addr, CBitStream &bits)
{
    CS_LINKID linkID;
    bits.Read(linkID);
    if (server != addr)
    {
        if (pCSLog)
            (*pCSLog) << "ERROR: Rejected msg received from unknown host " << addr.GetFastName()
                      << endl;
    }
    else
    {
        if (linkID != clientLinkID)
        {
            if (pCSLog)
                (*pCSLog) << "ERROR: Rejected msg received with wrong linkID from "
                          << addr.GetFastName() << endl;
            return;
        }
        if (state == S_LOGIN)
        {
            bits.Read(nRejectReason);
            state = S_NONE; // connection failed
        }
        else
        {
            if (pCSLog)
                (*pCSLog) << "ERROR: Reject not during connecting from " << addr.GetFastName()
                          << endl;
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSClient::ProcessInfo(const CCSNodeAddress &addr, CBitStream &bits)
{
    ASSERT(applicationID != 0); // specify unique applicationID
    CS_APPLICATION_ID appID;
    CObj<CCSServerInfo> pInfo;
    float fpReqSent;
    //
    bits.Read(fpReqSent);
    bits.Read(appID);
    // check that application is correct
    if ((appID & 0xFFFFFF00) != (applicationID & 0xFFFFFF00))
        return;
    auto i = GetServerInfo(addr);
    //
    NewServerInfo(pInfo);
    pInfo->UpdateFrom(bits);
    if (!pInfo.IsValid())
        return;
    pInfo->addr = addr;
    pInfo->bWrongVersion = appID != applicationID;
    if (links.IsLocalAddr(addr))
    {
        if (book.HasAddr(addr))
            pInfo->type = CCSServerInfo::ST_AddressBook;
        else
            pInfo->type = CCSServerInfo::ST_Local;
        pInfo->fpValidTimeLeft = FP_SERVER_LIST_TIMEOUT;
    }
    else
    {
        if (book.HasAddr(addr))
        {
            pInfo->type = CCSServerInfo::ST_AddressBook;
            pInfo->fpValidTimeLeft = book.servers.size() * 0.5f + FP_SERVER_LIST_TIMEOUT;
        }
        else
        {
            if (i != serverInfos.end())
            {
                pInfo->type = CCSServerInfo::ST_Master;
                pInfo->fpValidTimeLeft = 1e38f;
            }
            else
                return;
        }
    }
    pInfo->fpPing = fpTime - fpReqSent;
    //
    if (i != serverInfos.end())
    {
        *i = pInfo;
        // update book
        auto k = book.GetServer(addr);
        if (k != book.servers.end())
        {
            CBitLocker bits;
            k->info.Clear();
            bits.LockWrite(k->info, 1024);
            pInfo->UpdateTo(bits);
        }
    }
    else
        serverInfos.push_back(MakeObj(pInfo.Extract()));
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSClient::RemoveClient(CClientList::iterator i)
{
    if (i == clients.end())
        return;
    CCSBase::RemoveClient(i);
    if (clients.empty())
    {
        if (pCSLog)
            (*pCSLog) << "CONNECTION LOST server link is lost, entering idle" << endl;
        state = S_NONE;
        // CRAP - everything should be erased here
    }
}
/////////////////////////////////////////////////////////////////////////////////////
CCSClient::CServerInfoList::iterator CCSClient::GetServerInfo(const CCSNodeAddress &addr)
{
    for (auto i = serverInfos.begin(); i != serverInfos.end(); ++i)
    {
        if ((*i)->addr == addr)
            break;
    }
    return serverInfos.end();
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSClient::SendServerReqs()
{
    // send broadcast request(s)
    CCSNodeAddress bc;
    for (int nCounter = 0; 1; nCounter++)
    {
        if (links.MakeBroadcastAddr(bc, nCounter, NCSParams::nCSServerPort))
            CmdSGetInfo(bc);
        else
            break;
    }
    if (!requestList.empty())
    {
        CCSNodeAddress addr = requestList.front();
        requestList.pop_front();
        CmdSGetInfo(addr);
        return;
    }
    // send requests for servers in list
    if (++nLastSentServerReq >= book.servers.size())
        nLastSentServerReq = 0;
    if (nLastSentServerReq < book.servers.size())
        CmdSGetInfo(book.servers[nLastSentServerReq].addr);
    if (activeServer.GetIP() != 0)
        requestList.push_back(activeServer);
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSClient::SetActiveServer(const CCSNodeAddress &addr)
{
    activeServer = addr;
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSClient::SetAddressBook(CCSAddressBook &_book)
{
    book = _book;
    for (int k = 0; k < book.servers.size(); k++)
    {
        CCSAddressBook::SServer &server = book.servers[k];
        auto i = GetServerInfo(server.addr);
        if (i == serverInfos.end())
        {
            CObj<CCSServerInfo> pInfo;
            if (server.info.GetSize())
            {
                CBitLocker bits;
                server.info.Seek(0);
                bits.LockRead(server.info, server.info.GetSize());
                NewServerInfo(pInfo);
                pInfo->UpdateFrom(bits);
            }
            else
            {
                NewServerInfo(pInfo);
            }
            if (!pInfo.IsValid())
                continue;
            pInfo->type = CCSServerInfo::ST_AddressBook;
            pInfo->addr = server.addr;
            serverInfos.push_back(MakeObj(pInfo.Extract()));
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSClient::AddAddressToBook(const CCSNodeAddress &addr)
{
    auto k = book.GetServer(addr);
    if (k == book.servers.end())
    {
        book.servers.emplace_back();
        book.servers.back().addr = addr;
        CmdSGetInfo(addr);
    }
    auto i = GetServerInfo(addr);
    if (i == serverInfos.end())
    {
        CObj<CCSServerInfo> pInfo;
        NewServerInfo(pInfo);
        if (pInfo.IsValid())
        {
            pInfo->addr = addr;
            pInfo->type = CCSServerInfo::ST_AddressBook;
            serverInfos.push_back(MakeObj(pInfo.Extract()));
        }
    }
    else
    {
        (*i)->type = CCSServerInfo::ST_AddressBook;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSClient::RemoveAddressFromBook(const CCSNodeAddress &addr)
{
    auto k = book.GetServer(addr);
    if (k != book.servers.end())
        book.servers.erase(k);
    for (auto i = serverInfos.begin(); i != serverInfos.end();)
    {
        if ((*i)->addr == addr)
            i = serverInfos.erase(i);
        else
            ++i;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSClient::AddServerFromMaster(CCSServerInfo *pInfo)
{
    auto i = GetServerInfo(pInfo->addr);
    if (i == serverInfos.end())
    {
        pInfo->type = CCSServerInfo::ST_Master;
        pInfo->fpValidTimeLeft = 1e38f;
        serverInfos.push_back(MakeObj(pInfo));
    }
    requestList.push_back(pInfo->addr);
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSClient::ConnectServer(const CCSNodeAddress &addr, const CMemoryStream &pwd)
{
    if (state == S_LOGIN && addr == server)
        return;
    ASSERT(state == S_NONE);
    ASSERT(clients.empty());
    server = addr;
    clientLinkID = (GetTickCount() & 0xffff) | (rand() << 16);
    state = S_LOGIN;
    fpLoginTimeLeft = 0;
    fpLoginInterval = 0.1f;
    nRejectReason = 0;
    serverPassword = pwd;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CCSClient::IsWeakConnection()
{
    if (state != S_ACTIVE)
        return false;
    if (clients.empty())
        return false;
    return clients.front()->IsWeakConnect();
}
/////////////////////////////////////////////////////////////////////////////////////
const CCSServerInfo *CCSClient::GetServerInfo(int nIdx)
{
    auto k = serverInfos.begin();
    for (int i = 0; i < nIdx; i++)
    {
        if (k == serverInfos.end())
            return 0;
        ++k;
    }
    // *serferInfos[k] is CObj<CCSServerInfo *>, not CCSServerInfo *
    return k == serverInfos.end() ? 0 : &(**k);
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSClient::TimePassed(double fpSeconds)
{
    // remove outdated information about servers
    for (auto i = serverInfos.begin(); i != serverInfos.end();)
    {
        if ((*i)->fpValidTimeLeft > 0)
        {
            (*i)->fpValidTimeLeft -= fpSeconds;
            if ((*i)->fpValidTimeLeft <= 0)
            {
                if (book.HasAddr((*i)->addr))
                {
                    (*i)->fpPing = FP_INVALID_PING;
                    ++i;
                }
                else
                    i = serverInfos.erase(i);
            }
            else
                ++i;
        }
        else
            ++i;
    }
    //
    CCSBase::TimePassed(fpSeconds);
    // send updates
    switch (state)
    {
    case S_LOGIN:
        fpLoginTimeLeft -= fpSeconds;
        if (fpLoginTimeLeft <= 0)
        {
            if (fpLoginInterval > 3)
            {
                if (pCSLog)
                    (*pCSLog) << "Connection failed" << endl;
                state = S_NONE; // connection failed
            }
            else
            {
                CmdSLogin(server, serverPassword);
                fpLoginInterval *= 2;
                fpLoginTimeLeft = fpLoginInterval;
            }
        }
        break;
    default:
        break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// CCSServer
/////////////////////////////////////////////////////////////////////////////////////
void CCSServer::SendServerInformation(const CCSNodeAddress &addr, float fpReqTime)
{
    ASSERT(applicationID != 0); // specify unique applicationID
    CMemoryStream pkt;
    CObj<CCSServerInfo> pInfo;
    //
    CreateServerInfo(pInfo);
    if (!pInfo.IsValid())
        return;
    // send it
    pkt << CMDC_INFO;
    pkt << fpReqTime;
    {
        CBitLocker bits;
        bits.LockWrite(pkt, 1024);
        bits.Write(applicationID);
        pInfo->UpdateTo(bits);
    }
    SendUpdate(addr, pkt);
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSServer::SendAccepted(const CCSNodeAddress &addr, CS_LINKID linkID)
{
    CMemoryStream pkt;
    pkt << CMDC_ACCEPTED;
    pkt << linkID;
    SendUpdate(addr, pkt);
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSServer::SendRejected(const CCSNodeAddress &addr, CS_LINKID linkID, char bReason)
{
    CMemoryStream pkt;
    pkt << CMDC_REJECTED;
    pkt << linkID;
    pkt << bReason;
    SendUpdate(addr, pkt);
}
/////////////////////////////////////////////////////////////////////////////////////
CCSServer::CAcceptList::iterator CCSServer::GetAccepted(const CCSNodeAddress &addr)
{
    CAcceptList::iterator res;
    for (res = accepted.begin(); res != accepted.end(); ++res)
        if (res->addr == addr)
            return res;
    return res;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CCSServer::CheckLogin(const CCSNodeAddress &addr, CBitStream &file, char &nRejectReason)
{
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSServer::ProcessGetInfo(const CCSNodeAddress &addr, CBitStream &bits)
{
    float fpTime;
    bits.Read(fpTime);
    SendServerInformation(addr, fpTime);
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSServer::ProcessLogin(const CCSNodeAddress &addr, CBitStream &bits)
{
    char nRejectReason;
    CS_APPLICATION_ID appID;
    bits.Read(appID);
    if (appID != applicationID)
        return;
    int nReqRate;
    CS_LINKID linkID;
    bits.Read(linkID);
    bits.Read(nReqRate);
    nReqRate = Min(nReqRate, GetRate(addr));
    // test if session already active
    auto pClient = GetClient(addr);
    if (pClient != clients.end())
    {
        if (pCSLog)
            (*pCSLog) << "ERROR: Ignoring login request for active session from "
                      << addr.GetFastName() << endl;
        return;
    }
    // if this one already in list
    auto pAccepted = GetAccepted(addr);
    if (pAccepted != accepted.end())
    {
        if (linkID != pAccepted->linkID)
        {
            pAccepted->linkID = linkID;
            if (pCSLog)
                (*pCSLog) << "WARNING: LinkID mismatch from " << addr.GetFastName() << endl;
        }
        pAccepted->nRate = nReqRate;
        if (pCSLog)
            (*pCSLog) << "Reaccepting client from " << addr.GetFastName() << endl;
        SendAccepted(addr, linkID);
    }
    else
    {
        if (CheckLogin(addr, bits, nRejectReason))
        {
            if (pCSLog)
                (*pCSLog) << "Accepting client from " << addr.GetFastName() << endl;
            accepted.emplace_back(SAcceptWaiter(addr, linkID, nReqRate, 0.0f));
            SendAccepted(addr, linkID);
        }
        else
        {
            SendRejected(addr, linkID, nRejectReason);
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
bool CCSServer::ProcessNormal(const CCSNodeAddress &addr, CBitStream &bits)
{
    if (!CCSBase::ProcessNormal(addr, bits))
    {
        auto pAccepted = GetAccepted(addr);
        if (pAccepted == accepted.end())
            return false;
        AddClient(pAccepted->addr, pAccepted->linkID, pAccepted->nRate);
        accepted.erase(pAccepted);
        return CCSBase::ProcessNormal(addr, bits);
    }
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSServer::TimePassed(double fpSeconds)
{
    // check for timeouts for accepted but not created clients
    for (auto i = accepted.begin(); i != accepted.end();)
    {
        i->fpTime += fpSeconds;
        if (i->fpTime > FP_ACCEPTED_TIMEOUT)
        {
            if (pCSLog)
                (*pCSLog) << "Accept period elapsed for " << i->addr.GetFastName() << endl;
            i = accepted.erase(i);
        }
        else
            ++i;
    }
    //
    CCSBase::TimePassed(fpSeconds);
}
/////////////////////////////////////////////////////////////////////////////////////
