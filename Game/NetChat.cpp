#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "NetChat.h"
#include "ChatServer.h"
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// CChatConnection
///////////////////////////////////////////////////////////////////////////////////////////////////////////
bool CChatConnection::SendData(const char *pszData, int nLen)
{
    //	ASSERT( pLink->IsValid() );
    if (pLink->IsValid())
    {
        pLink->channelOutBuf.Seek(pLink->channelOutBuf.GetSize());
        pLink->channelOutBuf.Write(pszData, nLen);
    }
    return pLink->IsValid();
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
bool CChatConnection::CanSend()
{
    return pLink->IsValid();
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CChatConnection::Segment()
{
    if (pLink->IsValid())
    {
        CMemoryStream &mem = pLink->channelInBuf;
        NewDataIncome((const char *)mem.GetBuffer(), mem.GetSize());
        mem.Clear();
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CChatConnection::Disconnect()
{
    CMemorySave tmp;
    ReturnDataRead(tmp);
    ASSERT(tmp.GetSize() == 0);
    if (tmp.GetSize() != 0)
    {
        if (pLink->IsValid())
        {
            tmp.Seek(tmp.GetSize());
            tmp.Write(pLink->channelInBuf.GetBuffer(), pLink->channelInBuf.GetSize());
            pLink->channelInBuf.Seek(0);
            pLink->channelInBuf.Write(tmp.GetBuffer(), tmp.GetSize());
        }
        //		else
        //			ReportError( "Can not dump data back" );
    }
    pLink = 0;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// CChatNetDriver
///////////////////////////////////////////////////////////////////////////////////////////////////////////
CChatConnection *CChatNetDriver::NewLink(CCSLink *pLink)
{
    CChatConnection *pRes = dbgnew CChatConnection;
    pRes->pLink = pLink;
    return pRes;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
CChatConnection *CChatNetDriver::GetLink(CCSLink *pLink)
{
    for (auto i = links.begin(); i != links.end(); ++i)
    {
        CChatConnection *pTest = *i;
        if (pTest->pLink == pLink)
            return pTest;
    }
    return 0;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CChatNetDriver::Segment(CCSBase *pBase)
{
    ASSERT(pBase->IsValid());
    if (!pBase->IsValid())
        return;
    const CCSBase::CClientList &clients = pBase->GetClients();
    for (auto i = clients.begin(); i != clients.end(); ++i)
    {
        if (GetLink(*i) == 0 && (*i)->channelInBuf.GetSize() > 0 && pChatServer->IsValid() &&
            pChatServer->CanBeConnectingToServer(*i))
        {
            CChatConnection *pLink = NewLink(*i);
            links.push_back(MakeObj(pLink));
            newLinks.push_back(MakePtr(pLink));
        }
    }
    for (auto k = links.begin(); k != links.end();)
    {
        (*k)->Segment();
        if ((*k)->pLink->IsValid())
            ++k;
        else
            k = links.erase(k);
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
CChatConnection *CChatNetDriver::Connect(CCSLink *pLink)
{
    // pLink->channelInBuf.Clear();
    links.clear();
    newLinks.clear();
    CChatConnection *pRes = NewLink(pLink);
    links.push_back(MakeObj(pRes));
    pRes->Segment();
    while (pRes->HasPacket())
    {
        CPacket packed;
        if (!pRes->Recv(packed))
            ASSERT(0);
    }
    return pRes;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
bool CChatNetDriver::StartServer()
{
    return true;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CChatNetDriver::GetNewPlayers(CConnectionsList &newList)
{
    for (auto i = newLinks.begin(); i != newLinks.end(); ++i)
    {
        if ((*i)->IsValid())
            newList.AddTail(*i);
    }
    newLinks.clear();
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
