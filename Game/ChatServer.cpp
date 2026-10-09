#include "StdAfx.h"
#include "ModPackage.h"
#include "ModRuntime.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "ChatServer.h"
#include "WorldInterface.h"
#include "I3DMain.h"
#include "Console.h"
#include "QuestDatabase.h"
#include <algorithm>
#include "SpellSystem.h"
#include "SpellManager.h"
#include "Item.h"
#include "ItemManager.h"
#include "WorldServer.h"
#include "PlayerServer.h"
#include "A3ServerDesc.h"
///////////////////////////////////////////////////////////////////////////////////////////////////
const float FP_MASTER_SERVER_INFO_INTERVAL = 20;
const int N_MASTER_SERVER_PORT = 28004;
///////////////////////////////////////////////////////////////////////////////////////////////////
// General
///////////////////////////////////////////////////////////////////////////////////////////////////
CPtr<CChatServer> pChatServer;
CPtr<CChatClient> pChatClient;
const int N_A3_APPLICATION_ID = 0xDEC0AD07; // just to be different
///////////////////////////////////////////////////////////////////////////////////////////////////
void LMPTimePassed()
{
    static DWORD dwPrevTime = 0;
    DWORD dwCurTime = timeGetTime(), dwTimeStep;
    dwTimeStep = dwCurTime - dwPrevTime;
    if (dwTimeStep > 5000)
        dwTimeStep = 1000;
    dwPrevTime = dwCurTime;
    //
    if (pChatServer->IsValid())
        pChatServer->TimePassed(dwTimeStep / 1024.0f);
    if (pChatClient->IsValid())
        pChatClient->TimePassed(dwTimeStep / 1024.0f);
    IWorldConnector.LMPRecalc();
}
///////////////////////////////////////////////////////////////////////////////////////////////////
// CChatServerInfo
///////////////////////////////////////////////////////////////////////////////////////////////////
CChatServerInfo::CChatServerInfo()
{
    szName = "?";
    allod = LMPA_NONE;
    szQuestName = "?";
    nPlayers = 0;
    nMaxPlayers = 0;
    bUnderPassword = false;
}
void CChatServerInfo::UpdateTo(CBitStream &bits)
{
    bits.WriteCString(szName.c_str());
    bits.Write(allod);
    bits.WriteCString(szQuestName.c_str());
    bits.Write(nPlayers);
    bits.Write(nMaxPlayers);
    bits.Write(bUnderPassword);
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CChatServerInfo::UpdateFrom(CBitStream &bits)
{
    bits.ReadCString(szName);
    bits.Read(allod);
    bits.ReadCString(szQuestName);
    bits.Read(nPlayers);
    bits.Read(nMaxPlayers);
    bits.Read(bUnderPassword);
}
///////////////////////////////////////////////////////////////////////////////////////////////////
// classes in use
///////////////////////////////////////////////////////////////////////////////////////////////////
static void RegisterChatClasses(CCSBase &base)
{
    REGISTER_CS_CLASS(base, 1, CChatPerson, CS_BANDW); // CS_DELAY );//CS_BANDW );
    REGISTER_CS_CLASS(base, 2, CChatPhrase, CS_BANDW);
    REGISTER_CS_CLASS(base, 3, CChatZone, CS_BANDW);
    REGISTER_CS_CLASS(base, 4, CChatClientState, CS_BANDW);
    REGISTER_CS_CLASS(base, 5, CChatCAppear, CS_BANDW);
    REGISTER_CS_CLASS(base, 6, CChatCSay, CS_BANDW);
    REGISTER_CS_CLASS(base, 7, CChatCCmd, CS_BANDW);
    REGISTER_CS_CLASS(base, 8, CChatAvailableBriefing, CS_BANDW);
    REGISTER_CS_CLASS(base, 9, CChatCurrentQuest, CS_BANDW);
    REGISTER_CS_CLASS(base, 10, CChatGlobalVar, CS_BANDW);
    REGISTER_CS_CLASS(base, 11, CChatMQFile, CS_BANDW);
    REGISTER_CS_CLASS(base, 12, CChatBLOBPart, CS_BANDW);
    REGISTER_CS_CLASS(base, 13, CChatCMQRequest, CS_BANDW);
    REGISTER_CS_CLASS(base, 14, CChatCommand, CS_BANDW);
    REGISTER_CS_CLASS(base, 15, CChatItemProposal, CS_BANDW);
    REGISTER_CS_CLASS(base, 16, CChatItem, CS_BANDW);
    REGISTER_CS_CLASS(base, 17, CChatItemXCHG, CS_BANDW);
}
///////////////////////////////////////////////////////////////////////////////////////////////////
// CChatItemProposal
///////////////////////////////////////////////////////////////////////////////////////////////////
void CChatItemProposal::UpdateTo(CBitStream &bits)
{
    int nSize;
    nSize = items.size();
    bits.Write(nSize);
    for (int i = 0; i < nSize; i++)
        CS::pLink->WriteRef(bits, items[i]);
    bits.Write(nMoney);
    bits.Write(bOkTrade);
    bits.Write(nOkTo);
    bits.Write(nSign);
    CS::pLink->WriteRef(bits, pWho);
    CS::pLink->WriteRef(bits, pWhom);
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CChatItemProposal::UpdateFrom(CBitStream &bits)
{
    int nSize;
    bits.Read(nSize);
    items.resize(nSize);
    for (int i = 0; i < nSize; i++)
        CS::pLink->ReadRef(bits, items[i]);
    bits.Read(nMoney);
    bits.Read(bOkTrade);
    bits.Read(nOkTo);
    bits.Read(nSign);
    CS::pLink->ReadRef(bits, pWho);
    CS::pLink->ReadRef(bits, pWhom);
}
///////////////////////////////////////////////////////////////////////////////////////////////////
// CChatItemXCHG
///////////////////////////////////////////////////////////////////////////////////////////////////
void CChatItemXCHG::UpdateTo(CBitStream &bits)
{
    CS::pLink->WriteRef(bits, pWho);
    CS::pLink->WriteRef(bits, pWhom);
    bits.Write(bDoIt);
    bits.Write(nWhoSign);
    bits.Write(nWhomSign);
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CChatItemXCHG::UpdateFrom(CBitStream &bits)
{
    CS::pLink->ReadRef(bits, pWho);
    CS::pLink->ReadRef(bits, pWhom);
    bits.Read(bDoIt);
    bits.Read(nWhoSign);
    bits.Read(nWhomSign);
}
///////////////////////////////////////////////////////////////////////////////////////////////////
// CChatServer
///////////////////////////////////////////////////////////////////////////////////////////////////
CChatServer::CChatServer(ELMPAllod _allod)
{
    applicationID = N_A3_APPLICATION_ID;
    RegisterChatClasses(*this);
    RegisterTrigger(TT_ADD, OnAddAppear);
    RegisterTrigger(TT_UPD, OnUpdAppear);
    RegisterTrigger(TT_DEL, OnDelAppear);
    RegisterTrigger(TT_ADD, OnAddSay);
    RegisterTrigger(TT_ADD, OnAddCmd);
    RegisterTrigger(TT_ADD, OnAddFileReq);
    RegisterTrigger(TT_DEL, OnDelFileReq);
    RegisterTrigger(TT_ADD, OnAddTrade);
    RegisterTrigger(TT_UPD, OnUpdTrade);
    RegisterTrigger(TT_DEL, OnDelTrade);
    RegisterTrigger(TT_ADD, OnUpdTItem);
    RegisterTrigger(TT_UPD, OnUpdTItem);
    nTalkOrder = 0;
    nMaxClients = 8;
    // roll quests at start
    allod = _allod;
    GiveQuests();
    fpTimeToInformMaster = 0;
    bInformMaster = false;
    nMasterPwd = 0;
    memset(szPassword, 0, sizeof(szPassword));
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CChatServer::SetInformMaster(bool _bInform)
{
    bInformMaster = _bInform;
    if (bInformMaster)
        masterAddr.SetInetName(theSettings.szMasterServerName, N_MASTER_SERVER_PORT);
}
///////////////////////////////////////////////////////////////////////////////////////////////////
bool CChatServer::CanBeConnectingToServer(CCSLink *pLink)
{
    CChatClientState *pState = GetClient1(pLink);
    if (!pState->IsValid() || !pState->pPerson->IsValid())
        return false;
    return pState->pPerson->state == CPS_CONNECTING;
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CChatServer::GetActiveObjects(SCSActiveSet &toUpdate)
{
    CChatPerson *pPerson = 0;
    bool bAddBriefings = false;
    for (auto i = people.begin(); i != people.end(); ++i)
        toUpdate.AddObject(*i, 2);
    for (auto n = vars.begin(); n != vars.end(); ++n)
        toUpdate.AddObject(*n, 2);
    for (auto m = clientStates.begin(); m != clientStates.end(); ++m)
    {
        if ((*m)->pClient == CS::pLink)
        {
            pPerson = (*m)->pPerson;
            if ((*m)->pPerson->bLeader)
                bAddBriefings = true;
            toUpdate.AddObject(*m, 2);
        }
    }
    if (pPerson->IsValid())
    {
        for (auto k = talk.begin(); k != talk.end(); ++k)
        {
            if ((*k)->pWhom == 0 || (*k)->pWhom == CS::pLink)
            {
                if ((*k)->nOrder > pPerson->nFirstChatMsg)
                    toUpdate.AddObject(*k, 2);
            }
        }
    }
    for (auto c = mqFiles.begin(); c != mqFiles.end(); ++c)
    {
        toUpdate.AddObject(*c, 2);
        // test if current client has request on this file
        for (auto z = requests.begin(); z != requests.end(); ++z)
        {
            if ((CChatMQFile *)(*z)->pFile == (CChatMQFile *)*c && (*z)->pClient == CS::pLink)
            {
                // this client has request - send him parts of file
                for (auto x = (*c)->parts.begin(); x != (*c)->parts.end(); ++x)
                    toUpdate.AddObject(*x, 1);
                break;
            }
        }
    }
    if (bAddBriefings)
    {
        for (auto f = briefings.begin(); f != briefings.end(); ++f)
            toUpdate.AddObject(*f, 2);
    }
    if (pZone.IsValid())
        toUpdate.AddObject(pZone, 2);
    if (pQuest.IsValid())
        toUpdate.AddObject(pQuest, 2);
    for (auto b = cmds.begin(); b != cmds.end(); ++b)
    {
        if (b->second->pClient == CS::pLink)
            toUpdate.AddObject(b->second, 2);
    }
    for (auto s = trades.begin(); s != trades.end(); ++s)
    {
        if ((*s)->pWhom == pPerson)
        {
            bool bOk = true;
            for (int z = 0; z < (*s)->items.size(); ++z)
            {
                if (!(*s)->items[z]->IsValid())
                    bOk = false;
            }
            if (bOk)
            {
                toUpdate.AddObject(*s, 2);
                for (int z = 0; z < (*s)->items.size(); ++z)
                    toUpdate.AddObject((*s)->items[z], 2);
            }
        }
    }
    {
        for (auto i = xchgs.begin(); i != xchgs.end(); ++i)
        {
            if ((*i)->pPersonWho == pPerson || (*i)->pPersonWhom == pPerson)
                toUpdate.AddObject((*i), 2);
        }
        for (auto k = timedXCHGs.begin(); k != timedXCHGs.end(); ++k)
        {
            if (k->second->pPersonWho == pPerson || k->second->pPersonWhom == pPerson)
                toUpdate.AddObject(k->second, 2);
        }
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CChatServer::TimePassed(double fpSeconds)
{
    CCSServer::TimePassed(fpSeconds);
    timedXCHGs.TimePassed(fpSeconds);
    cmds.TimePassed(fpSeconds);
    if (bInformMaster)
    {
        fpTimeToInformMaster -= fpSeconds;
        if (fpTimeToInformMaster < 0)
        {
            fpTimeToInformMaster = FP_MASTER_SERVER_INFO_INTERVAL;
            if (theSettings.szMasterServerName != "")
                SendMasterServerInfo();
        }
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////////
CChatClientState *CChatServer::GetClient1(CCSLink *pLink)
{
    if (!pLink->IsValid())
        return 0;
    for (auto m = clientStates.begin(); m != clientStates.end(); ++m)
    {
        if ((*m)->pClient == pLink)
            return *m;
    }
    return 0;
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CChatServer::AddBriefing(const std::string &szBrief)
{
    CChatAvailableBriefing *pBrief = dbgnew CChatAvailableBriefing;
    pBrief->szBriefingName = szBrief;
    briefings.push_back(MakeObj(pBrief));
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CChatServer::GiveQuests()
{
    IWorldConnector.LMPFreeTextResources();
    std::vector<std::string> szQuests;
    IWorldConnector.LMPGetQuests(szQuests, allod, szLastCompletedQuest.c_str());
    for (auto i = szQuests.begin(); i != szQuests.end(); ++i)
    {
        AddBriefing(*i + "_1");
        IWorldConnector.LMPAddTextResource(i->c_str());
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CChatServer::BrSelectQuest(const std::string &szQuest)
{
    briefings.clear();
    vars.clear();
    AddBriefing(szQuest + "_2");
    pQuest = dbgnew CChatCurrentQuest;
    pQuest->szQuestName = szQuest;
    //
    szLastCompletedQuest = szQuest;
    // load info into map.txt, quest database
    IWorldConnector.LMPLoadQuestResources(szQuest.c_str());
    IWorldConnector.SelectLMPZone(szQuest.c_str());
    //
    Inform(CMS_QUESTTAKEN, IWorldConnector.LMPGetQuestNameLocalised());
    // set variables
    const SQDBBriefing *pBrief = theQuestDatabase.briefings.GetEntry(szQuest + "_1");
    if (pBrief)
    {
        for (auto i = pBrief->szGiveQuests.begin(); i != pBrief->szGiveQuests.end(); ++i)
            GSSetVar((const char *)*i, 1);
        GSSetVar(std::string("q.") + szQuest + "." + szQuest, 1);
        GSSetVar(std::string("q.") + szQuest + "." + szQuest + ".1", 1);
        CommandItems(pBrief, false);
    }
    else
        ASSERT(0);
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CChatServer::BrRefuseQuest()
{
    if (pQuest->IsValid())
    {
        Inform(CMS_QUESTCANCELED, IWorldConnector.LMPGetQuestNameLocalised());
        const SQDBBriefing *pBrief = theQuestDatabase.briefings.GetEntry(
            (pQuest->szQuestName + "_1").c_str());
        if (pBrief)
            CommandItems(pBrief, true);
        else
            ASSERT(0);
    }
    pQuest = 0;
    briefings.clear();
    vars.clear();
    RemoveFiles();
    GiveQuests();
    IWorldConnector.SelectLMPZone(0);
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CChatServer::BrCompleteQuest(const std::string &szQuest)
{
    if (pQuest->IsValid())
        Inform(CMS_QUESTCOMPLETED, IWorldConnector.LMPGetQuestNameLocalised());
    // give out experience
    const SQDBBriefing *pBrief = theQuestDatabase.briefings.GetEntry(szQuest + "_3");
    if (pBrief)
    {
        int nClients = Max(1, (int)clients.size());
        float fpExp = pBrief->fExp / nClients;
        float fpMoney = pBrief->fMoney / nClients;
        for (auto i = clients.begin(); i != clients.end(); ++i)
        {
            Command(*i, CS_GIVE_XP, fpExp, "");
            Command(*i, CS_GIVE_MONEY, fpMoney, "");
        }
        CommandItems(pBrief, true);
    }
    pQuest = 0;
    briefings.clear();
    vars.clear();
    RemoveFiles();
    GiveQuests();
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CChatServer::CommandItems(const SQDBBriefing *pBrief, bool bEraseQuestItems)
{
    int i;
    CCSLink *pLeader = GetLeader();
    if (!pLeader->IsValid())
        return;
    if (pBrief->szItems.size() > 0)
    {
        for (i = 0; i < pBrief->szItems.size(); i++)
            Command(pLeader, CS_GIVE_ITEM, 0, (const char *)pBrief->szItems[i]);
    }
    if (bEraseQuestItems)
        Command(pLeader, CS_REMOVE_QUEST_ITEMS, 0, "");
    //	for ( i = 0; i < pBrief->szRemoveItems.size(); i++ )
    //		Command( pLeader, CS_REMOVE_ITEM, 0, (const char*)pBrief->szRemoveItems[i] );
}
///////////////////////////////////////////////////////////////////////////////////////////////////
bool CChatServer::CompleteQuest(const std::string &szQuest)
{
    if (pQuest->IsValid())
    {
        if (pQuest->szQuestName == szQuest)
        {
            pQuest = 0;
            briefings.clear();
            AddBriefing(szQuest + "_3");
            return true;
        }
        else
            ASSERT(0);
    }
    return false;
}
///////////////////////////////////////////////////////////////////////////////////////////////////
CChatServer::CChatVars::iterator CChatServer::GetVar(const std::string &szVar)
{
    for (auto i = vars.begin(); i != vars.end(); ++i)
    {
        if ((*i)->szVarName == szVar)
            return i;
    }
    return vars.end(); // TODO: is it OK? Was return i;
}
///////////////////////////////////////////////////////////////////////////////////////////////////
CChatServer::CChatVars::iterator CChatServer::GetVarNew(const std::string &szVar)
{
    auto i = GetVar(szVar);
    if (i == vars.end())
    {
        vars.push_front(MakeObj(dbgnew CChatGlobalVar));
        i = vars.begin();
        (*i)->szVarName = szVar;
    }
    return i;
}
///////////////////////////////////////////////////////////////////////////////////////////////////
static int nVarOrder = 0;
void CChatServer::GSSetVar(const std::string &szVar, float fpVal)
{
    if (fpVal == 0)
        GSDelVar(szVar);
    else
    {
        auto i = GetVarNew(szVar);
        (*i)->fpVal = fpVal;
        (*i)->nOrder = nVarOrder++;
        (*i)->SetUpdated();
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CChatServer::GSSetVarMax(const std::string &szVar, float fpVal)
{
    auto i = GetVarNew(szVar);
    (*i)->fpVal = Max((*i)->fpVal, fpVal);
    (*i)->nOrder = nVarOrder++;
    (*i)->SetUpdated();
    if ((*i)->fpVal == 0)
        GSDelVar(szVar);
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CChatServer::GSDelVar(const std::string &szVar)
{
    auto i = GetVar(szVar);
    if (i != vars.end())
        vars.erase(i);
}
///////////////////////////////////////////////////////////////////////////////////////////////////
float CChatServer::GSGetVar(const std::string &szVar)
{
    auto i = GetVar(szVar);
    if (i != vars.end())
        return (*i)->fpVal;
    return 0;
}
///////////////////////////////////////////////////////////////////////////////////////////////////
bool CChatServer::IsBriefing(const std::string &szBrief)
{
    for (auto i = briefings.begin(); i != briefings.end(); ++i)
    {
        if ((*i)->szBriefingName == szBrief)
            return true;
    }
    return false;
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CChatServer::AddFile(const std::string &szFName, CMemoryStream &file)
{
    // test if file is already here
    for (auto i = mqFiles.begin(); i != mqFiles.end(); ++i)
    {
        if ((*i)->szFileName == szFName)
            return;
    }
    // add file with its parts
    CChatMQFile *pFile = dbgnew CChatMQFile;
    int nSize = file.GetSize(), nParts = (nSize + N_CHAT_BLOB_SIZE - 1) / N_CHAT_BLOB_SIZE;
    pFile->szFileName = szFName;
    pFile->nFileLength = nSize;
    pFile->nTotalParts = nParts;
    pFile->parts.resize(nParts, dbgnew CChatBLOBPart);
    file.Seek(0);
    for (int k = 0; k < nParts; ++k)
    {
        pFile->parts[k]->pFile = pFile;
        pFile->parts[k]->nPart = k;
        int nRead = Min(N_CHAT_BLOB_SIZE, nSize - k * N_CHAT_BLOB_SIZE);
        file.Read(pFile->parts[k]->cData, nRead);
    }
    mqFiles.push_back(MakeObj(pFile));
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CChatServer::RemoveFiles()
{
    mqFiles.clear();
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CChatServer::Command(CCSLink *pClient, EChatServerCmd cmd, float fpParam, std::string szParam)
{
    CChatCommand *pCmd = dbgnew CChatCommand;
    pCmd->pClient = pClient;
    pCmd->cmd = cmd;
    pCmd->fpParam = fpParam;
    pCmd->szParam = szParam;
    cmds.Add(pCmd, 60);
}
///////////////////////////////////////////////////////////////////////////////////////////////////
bool CChatServer::IsLeader(CCSLink *pLink)
{
    for (auto m = clientStates.begin(); m != clientStates.end(); ++m)
    {
        if ((*m)->pClient == pLink && (*m)->pPerson->IsValid() && (*m)->pPerson->bLeader)
            return true;
    }
    return false;
}
///////////////////////////////////////////////////////////////////////////////////////////////////
CCSLink *CChatServer::GetLeader()
{
    for (auto m = clientStates.begin(); m != clientStates.end(); ++m)
    {
        if ((*m)->pPerson->IsValid() && (*m)->pPerson->bLeader)
            return (*m)->pClient;
    }
    return 0;
}
///////////////////////////////////////////////////////////////////////////////////////////////////
int CChatServer::GetLeaderServerID()
{
    for (auto i = people.begin(); i != people.end(); ++i)
    {
        if ((*i)->bLeader)
            return (*i)->nServerID;
    }
    ASSERT(0); // no leader on server?
    return -1;
}
///////////////////////////////////////////////////////////////////////////////////////////////////
struct SIPFunc
{
    const CCSNodeAddress &tst;
    SIPFunc(const CCSNodeAddress &_tst)
        : tst(_tst)
    {
    }
    bool operator()(const CCSNodeAddress &_tst) const { return tst.SameIP(_tst); }
};
bool CChatServer::CheckLogin(const CCSNodeAddress &addr, CBitStream &file, char &bRejectReason)
{
    bRejectReason = CSR_IP_BANNED;
    if (std::find_if(banned.begin(), banned.end(), SIPFunc(addr)) != banned.end())
        return false;
    if (szPassword[0])
    {
        bRejectReason = CSR_WRONG_PASSWORD;
        char szTest[sizeof(szPassword)];
        memset(szTest, 0, sizeof(szTest));
        file.Read(szTest);
        if (memcmp(szTest, szPassword, sizeof(szPassword)) != 0)
            return false;
    }
    bRejectReason = CSR_SERVER_FULL;
    return clients.size() + GetAcceptedCount() < nMaxClients;
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CChatServer::SetPassword(const char *pszPwd)
{
    memset(szPassword, 0, sizeof(szPassword));
    strncpy(szPassword, pszPwd, sizeof(szPassword));
}
///////////////////////////////////////////////////////////////////////////////////////////////////
CCSLink *CChatServer::GetClient1(const std::string &szName)
{
    int nLeng = szName.size();
    for (auto i = people.begin(); i != people.end(); ++i)
    {
        if (strnicmp((*i)->szName.c_str(), szName.c_str(), nLeng) == 0)
        {
            for (auto k = clientStates.begin(); k != clientStates.end(); ++k)
            {
                if ((*k)->pPerson == (CChatPerson *)(*i))
                {
                    if (!IsLeader((*k)->pClient))
                        return (*k)->pClient;
                    else
                        break;
                }
            }
        }
    }
    return 0;
}
///////////////////////////////////////////////////////////////////////////////////////////////////
CCSLink *CChatServer::GetClient1(CChatPerson *pPers)
{
    if (!pPers->IsValid())
        return 0;
    for (auto k = clientStates.begin(); k != clientStates.end(); ++k)
    {
        if ((*k)->pPerson == pPers)
            return (*k)->pClient;
    }
    return 0;
}
///////////////////////////////////////////////////////////////////////////////////////////////////
CCSLink *CChatServer::GetClient1(int nUniquePersonID)
{
    for (auto k = clientStates.begin(); k != clientStates.end(); ++k)
    {
        if ((*k)->pPerson->nUniquePersonID == nUniquePersonID)
            return (*k)->pClient;
    }
    return 0;
}
///////////////////////////////////////////////////////////////////////////////////////////////////
int CChatServer::GetUnusedChatColor()
{
    for (int nTry = 1; 1; nTry++)
    {
        for (auto i = people.begin(); i != people.end(); ++i)
        {
            if ((*i)->nChatColor == nTry)
                goto Failed;
        }
        return nTry;
    Failed:;
    }
    return 0;
}
///////////////////////////////////////////////////////////////////////////////////////////////////
CChatPerson *CChatServer::GetPerson(int nServerID)
{
    for (auto i = people.begin(); i != people.end(); ++i)
    {
        if ((*i)->nServerID == nServerID)
            return *i;
    }
    return 0;
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CChatServer::AddChatPhrase(const char *pszText, CChatPerson *pWhom)
{
    talk.push_back(MakeObj(dbgnew CChatPhrase));
    CChatPhrase *pPhrase = talk.back();
    pPhrase->szText = pszText;
    pPhrase->nOrder = nTalkOrder++;
    pPhrase->pWhom = GetClient1(pWhom);
    if (talk.size() > 50)
        talk.erase(talk.begin());
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CChatServer::Inform(EChatMessages msg, const char *pszParam, CChatPerson *pWhom)
{
    AddChatPhrase(theInterface.FormChatMessageString(msg, pszParam), pWhom);
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CChatServer::InformAllButLeader(EChatMessages msg, const char *pszParam)
{
    for (auto i = people.begin(); i != people.end(); ++i)
    {
        if (!(*i)->bLeader)
            Inform(msg, pszParam, *i);
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CChatServer::Kick(CCSLink *pLink)
{
    if (pLink->IsValid() && pLink != GetLeader())
    {
        CChatClientState *pState = GetClient1(pLink);
        if (pState && pState->pPerson->IsValid())
            Inform(CMS_PLAYERKICKED, pState->pPerson->szName.c_str());
        CmdLogout(pLink->GetPeerAddress(), pLink->GetLinkID());
        RemoveClient(GetClient(pLink->GetPeerAddress()));
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CChatServer::Ban(CCSLink *pLink)
{
    if (pLink->IsValid() && pLink != GetLeader())
    {
        CChatClientState *pState = GetClient1(pLink);
        if (pState && pState->pPerson->IsValid())
            Inform(CMS_PLAYERBANNED, pState->pPerson->szName.c_str());
        banned.push_back(pLink->GetPeerAddress());
        CmdLogout(pLink->GetPeerAddress(), pLink->GetLinkID());
        RemoveClient(GetClient(pLink->GetPeerAddress()));
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CChatServer::OnClientDrop(CCSLink *pLink)
{
    CChatClientState *pState = GetClient1(pLink);
    if (!pState->IsValid() || !pState->pPerson->IsValid())
        return;
    Inform(CMS_PLAYERDROPPED, pState->pPerson->szName.c_str());
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CChatServer::OnClientExit(CCSLink *pLink)
{
    CChatClientState *pState = GetClient1(pLink);
    if (!pState->IsValid() || !pState->pPerson->IsValid())
        return;
    Inform(CMS_PLAYERLEAVE, pState->pPerson->szName.c_str());
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CChatServer::CreateServerInfo(CObj<CCSServerInfo> &pRes)
{
    CChatServerInfo *pInfo = dbgnew CChatServerInfo;
    pInfo->szName = szServerName;
    pInfo->allod = allod;
    pInfo->szQuestName = "";
    if (pQuest->IsValid())
        pInfo->szQuestName = IWorldConnector.LMPGetQuestNameLocalised();
    pInfo->nPlayers = char(clients.size());
    pInfo->nMaxPlayers = nMaxClients;
    pInfo->bUnderPassword = szPassword[0] != 0;
    pRes = pInfo;
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CChatServer::SendMasterServerInfo()
{
    CMemoryStream pkt;
    SA3GameServer info;
    info.szName = szServerName;
    info.bSecret = szPassword[0] != 0;
    info.nClientID = nUniqueClientID;
    info.nAllod = (unsigned char)allod;
    info.szQuestName = "";
    if (pQuest->IsValid())
        info.szQuestName = IWorldConnector.LMPGetQuestNameLocalised();
    info.nPlayers = (unsigned char)clients.size();
    info.nMaxPlayers = nMaxClients;
    info.nPwd = nMasterPwd;
    info.nProtocolID = N_A3_APPLICATION_ID;
    info.UpdateTo(pkt);
    links.Send(masterAddr, pkt);
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CChatServer::ProcessMaster(const CCSNodeAddress &addr, CBitStream &bits)
{
    unsigned int nClientIDTest;
    if (addr != masterAddr)
    {
        ASSERT(0); // password from wrong master server
        return;
    }
    bits.Read(nClientIDTest);
    if (nUniqueClientID == nClientIDTest)
        bits.Read(nMasterPwd);
}
///////////////////////////////////////////////////////////////////////////////////////////////////
CChatItemXCHG *CChatServer::GetItemXCHG(CChatItemProposal *pWho, CChatItemProposal *pWhom)
{
    for (auto i = xchgs.begin(); i != xchgs.end(); ++i)
    {
        if (((*i)->pWho == pWho && (*i)->pWhom == pWhom) ||
            ((*i)->pWho == pWhom && (*i)->pWhom == pWho))
            return *i;
    }
    return 0;
}
///////////////////////////////////////////////////////////////////////////////////////////////////
CChatItemProposal *CChatServer::GetPeerTrade(CChatItemProposal *pWho)
{
    for (auto i = trades.begin(); i != trades.end(); ++i)
    {
        if ((*i)->pWho == pWho->pWhom && (*i)->pWhom == pWho->pWho)
            return *i;
    }
    return 0;
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void OnUpdAppear(CChatServer *pThis, CChatCAppear *pObj)
{
    pObj->pState->pPerson->state = pObj->state;
    pObj->pState->pPerson->nServerID = pObj->nServerID;
    pObj->pState->pPerson->SetUpdated();
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void OnAddAppear(CChatServer *pThis, CChatCAppear *pObj)
{
    static int nUnique = 0;
    pThis->people.push_back(MakeObj(dbgnew CChatPerson));
    CChatPerson *pPerson = pThis->people.back();
    pThis->clientStates.push_back(MakeObj(dbgnew CChatClientState));
    CChatClientState *pState = pThis->clientStates.back();
    pPerson->szName = pObj->szName;
    pPerson->state = pObj->state;
    pPerson->szPrototype = pObj->szPrototype;
    pPerson->nServerID = pObj->nServerID;
    pPerson->nChatColor = pThis->GetUnusedChatColor();
    pPerson->nUniquePersonID = nUnique++;
    pPerson->nFirstChatMsg = pThis->nTalkOrder;
    pState->pPerson = pPerson;
    pState->pClient = CS::pLink;
    pObj->pState = pState;
    if (pChatClient->IsValid())
    {
        if (pObj->bLocalServer)
            pPerson->bLeader = true;
    }
    else
    {
        if (pThis->clientStates.size() == 1)
            pPerson->bLeader = true;
    }
    //
    pThis->Inform(CMS_PLAYERENTER, pPerson->szName.c_str());
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void OnDelAppear(CChatServer *pThis, CChatCAppear *pObj)
{
    if (pObj->pState->IsValid())
    {
        if (pObj->pState->pPerson->IsValid())
        {
            bool bWasLeader = pObj->pState->pPerson->bLeader;
            pThis->people.remove_if(SObjTest(pObj->pState->pPerson));
            if (bWasLeader && pThis->people.size() > 0)
            {
                pThis->people.front()->bLeader = true;
                pThis->people.front()->SetUpdated();
            }
        }
        pThis->clientStates.remove_if(SObjTest(pObj->pState));
    }
    else
        ASSERT(0);
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void OnAddSay(CChatServer *pThis, CChatCSay *pObj)
{
    CChatClientState *pState = pThis->GetClient1(CS::pLink);
    ASSERT(pState->IsValid());
    if (!pState->IsValid())
        return;
    CString szText;
    if(auto* mod=ModRuntime::Resources())mod->LogChat(pState->pPerson->nChatColor,pState->pPerson->szName.c_str(),pObj->szText.c_str());
    szText.Format("^%i%s^7: %s", pState->pPerson->nChatColor, pState->pPerson->szName.c_str(),
                  pObj->szText.c_str());
    pThis->AddChatPhrase(szText);
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void OnAddCmd(CChatServer *pThis, CChatCCmd *pObj)
{
    switch (pObj->cmd)
    {
    case CC_SELECT_ZONE:
        if (pThis->IsLeader(CS::pLink))
            IWorldConnector.SelectLMPZone(pObj->szParam.c_str());
        else
            ASSERT(0); // command select zone not from leader
        break;
    case CC_COMPLETE_BRIEFING:
        if (pThis->IsLeader(CS::pLink) && pThis->IsBriefing(pObj->szParam))
        {
            std::string szFinish = pObj->szParam.substr(pObj->szParam.length() - 2);
            std::string szQuest = pObj->szParam.substr(0, pObj->szParam.length() - 2);
            if (szFinish == "_1")
            {
                // get quest briefing is completed
                pThis->BrSelectQuest(szQuest);
            }
            if (szFinish == "_2")
            {
                // refuse quest briefing is completed
                pThis->BrRefuseQuest();
            }
            if (szFinish == "_3")
            {
                // quest was completed and we can get some reward
                pThis->BrCompleteQuest(szQuest);
            }
        }
        else
            ASSERT(0); // command select zone not from leader
        break;
    case CC_KICK:
        if (pThis->IsLeader(CS::pLink))
        {
            for (;;)
            {
                CCSLink *pLink = pThis->GetClient1(pObj->szParam);
                if (pLink->IsValid())
                    pThis->Kick(pLink);
                else
                    break;
            }
        }
        else
            ASSERT(0); // Сосите кеглю
        break;
    case CC_BAN:
        if (pThis->IsLeader(CS::pLink))
        {
            for (;;)
            {
                CCSLink *pLink = pThis->GetClient1(pObj->szParam);
                if (pLink->IsValid())
                    pThis->Ban(pLink);
                else
                    break;
            }
        }
        else
            ASSERT(0); // Сосите кеглю
        break;
    case CC_KICK_ID:
        if (pThis->IsLeader(CS::pLink))
        {
            CCSLink *pLink = pThis->GetClient1(atoi(pObj->szParam.c_str()));
            pThis->Kick(pLink);
        }
        else
            ASSERT(0); // Сосите кеглю
        break;
    case CC_BAN_ID:
        if (pThis->IsLeader(CS::pLink))
        {
            CCSLink *pLink = pThis->GetClient1(atoi(pObj->szParam.c_str()));
            pThis->Ban(pLink);
        }
        else
            ASSERT(0); // Сосите кеглю
        break;
    default:
        ASSERT(0);
        break;
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void OnAddFileReq(CChatServer *pThis, CChatCMQRequest *pObj)
{
    pObj->pClient = CS::pLink;
    pThis->requests.push_back(MakePtr(pObj));
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void OnDelFileReq(CChatServer *pThis, CChatCMQRequest *pObj)
{
    pThis->requests.remove(MakePtr(pObj));
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void OnAddTrade(CChatServer *pThis, CChatItemProposal *pObj)
{
    pThis->trades.push_back(MakePtr(pObj));
    // see if there is a peer trade and if so create xchange object
    CChatItemProposal *pPeer = pThis->GetPeerTrade(pObj);
    if (pPeer)
    {
        ASSERT(pThis->GetItemXCHG(pObj, pPeer) == 0);
        CChatItemXCHG *pX = dbgnew CChatItemXCHG;
        pX->pWho = pObj;
        pX->pWhom = pPeer;
        pX->pPersonWho = pObj->pWho;
        pX->pPersonWhom = pObj->pWhom;
        pThis->xchgs.push_back(MakePtr(pX));
        pObj->bWasTrade = true;
        pPeer->bWasTrade = true;
    }
    if (!pPeer)
    {
        pThis->Inform(CMS_TRADEREQUEST, pObj->pWho->szName.c_str(), pObj->pWhom);
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void OnUpdTrade(CChatServer *pThis, CChatItemProposal *pObj)
{
    pObj->SetUpdated();
    // look is not it Ok to trade
    CChatItemProposal *pPeer = pThis->GetPeerTrade(pObj);
    CChatItemXCHG *pX = pThis->GetItemXCHG(pObj, pPeer);
    if (pX->IsValid() && pX->pWho->IsValid() && pX->pWhom->IsValid())
    {
        if (pX->pWho->bOkTrade && pX->pWho->nOkTo == pX->pWhom->nSign && pX->pWhom->bOkTrade &&
            pX->pWhom->nOkTo == pX->pWho->nSign)
        {
            pX->bDoIt = true;
            pX->nWhoSign = pX->pWho->nSign;
            pX->nWhomSign = pX->pWhom->nSign;
            pX->SetUpdated();
        }
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void OnUpdTItem(CChatServer *pThis, CChatItem *pObj)
{
    pObj->SetUpdated();
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void OnDelTrade(CChatServer *pThis, CChatItemProposal *pObj)
{
    pThis->trades.remove(MakePtr(pObj));
    //
    CChatItemProposal *pPeer = pThis->GetPeerTrade(pObj);
    CChatItemXCHG *pX = pThis->GetItemXCHG(pObj, pPeer);
    if (!pObj->bWasTrade)
    {
        pThis->Inform(CMS_TRADECANCEL, pObj->pWho->szName.c_str(), pObj->pWhom);
    }
    if (pX->IsValid())
    {
        if (pX->bDoIt)
            pThis->timedXCHGs.Add(pX, 60);
        pThis->xchgs.remove(pX);
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////////
// CChatClient
///////////////////////////////////////////////////////////////////////////////////////////////////
CChatClient::CChatClient()
{
    pSelf = dbgnew CChatCAppear;
    applicationID = N_A3_APPLICATION_ID;
    RegisterChatClasses(*this);
    RegisterTrigger(TT_ADD, OnAddState);
    RegisterTrigger(TT_DEL, OnDelState);
    RegisterTrigger(TT_ADD, OnAddPerson);
    RegisterTrigger(TT_UPD, OnUpdPerson);
    RegisterTrigger(TT_DEL, OnDelPerson);
    RegisterTrigger(TT_ADD, OnAddPhrase);
    RegisterTrigger(TT_DEL, OnDelPhrase);
    RegisterTrigger(TT_ADD, OnAddZone);
    RegisterTrigger(TT_DEL, OnDelZone);
    RegisterTrigger(TT_ADD, OnAddBrief);
    RegisterTrigger(TT_DEL, OnDelBrief);
    RegisterTrigger(TT_ADD, OnAddQuest);
    RegisterTrigger(TT_DEL, OnDelQuest);
    RegisterTrigger(TT_ADD, OnAddGSVar);
    RegisterTrigger(TT_UPD, OnUpdGSVar);
    RegisterTrigger(TT_DEL, OnDelGSVar);
    RegisterTrigger(TT_ADD, OnAddMQFile);
    RegisterTrigger(TT_DEL, OnDelMQFile);
    RegisterTrigger(TT_ADD, OnAddBLOB);
    RegisterTrigger(TT_DEL, OnDelBLOB);
    RegisterTrigger(TT_ADD, OnAddSCmd);
    RegisterTrigger(TT_ADD, OnAddTradeC);
    RegisterTrigger(TT_UPD, OnUpdTradeC);
    RegisterTrigger(TT_DEL, OnDelTradeC);
    RegisterTrigger(TT_ADD, OnAddItem);
    RegisterTrigger(TT_UPD, OnUpdItem);
    RegisterTrigger(TT_DEL, OnDelItem);
    RegisterTrigger(TT_ADD, OnAddItemXC);
    RegisterTrigger(TT_UPD, OnUpdItemXC);
    RegisterTrigger(TT_DEL, OnDelItemXC);
    itemsOffer.Create();
    itemsRemote.Create();
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CChatClient::TimePassed(double fpSeconds)
{
    CCSClient::TimePassed(fpSeconds);
    cmds.TimePassed(fpSeconds);
    if (!serverCmds.empty())
    {
        // try to execute them
        if (theWorldServer.curInterface == IFACE_BASE)
        {
            CPlayer *pPlayer = theWorldServer.GetPlayer(0);
            if (pPlayer)
            {
                CPlayerServer *pPS = (CPlayerServer *)pPlayer->GetPlayerSpecific();
                if (pPS->GetStatus() == CPlayerServer::PSS_ACTIVE && theWorldServer.IsMapLoaded())
                    ExecuteServerCommands();
            }
        }
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CChatClient::ExecuteServerCommands()
{
    CPlayer *pPlayer = theWorldServer.GetPlayer(0);
    ASSERT(pPlayer);
    if (!pPlayer)
        return;
    for (auto i = serverCmds.begin(); i != serverCmds.end(); ++i)
    {
        CChatCommand *pObj = *i;
        switch (pObj->cmd)
        {
        case CS_GIVE_XP:
            theWorldServer.GainExperience(0, pObj->fpParam);
            break;
        case CS_GIVE_MONEY: {
            CList<CItem *, CItem *> items;
            pPlayer->party.nMoney += (int)pObj->fpParam;
            pPlayer->AddItemsToHoover(items);
        }
        break;
        case CS_GIVE_ITEM: {
            typedef CList<CItem *, CItem *> CItemPtrList;
            CList<CSmartPtr<CItem>, CItem *> items;
            items.AddTail(pPlayer->party.GetItemManager()->CreateItem(pObj->szParam.c_str()));
            pPlayer->AddItemsToHoover((CItemPtrList &)items);
        }
        break;
        case CS_REMOVE_ITEM: {
            int nID = -1;
            CSmartPtr<CItem> pItem = pPlayer->party.GetItemManager()->CreateItem(
                pObj->szParam.c_str());
            for (POSITION pos = pPlayer->items.GetHeadPosition(); pos;)
            {
                CItem *pTItem = pPlayer->items.GetNext(pos);
                if (pTItem->Compare(pItem) == 0)
                {
                    nID = pTItem->GetID();
                    break;
                }
            }
            if (nID >= 0)
                pPlayer->RemoveItemByID(nID);
            else
                ASSERT(0); // no such item found
        }
        break;
        case CS_REMOVE_QUEST_ITEMS: {
            pPlayer->party.RemoveAllQuestItems();
            for (POSITION pos = pPlayer->items.GetHeadPosition(); pos;)
            {
                CItem *pItem = pPlayer->items.GetNext(pos);
                if (pItem->WhoAmI() == HPC_ITEM_QUEST)
                    pPlayer->RemoveItemByID(pItem->GetID());
            }
        }
        break;
        default:
            ASSERT(0); // unknown command
            break;
        }
    }
    serverCmds.clear();
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CChatClient::GetActiveObjects(SCSActiveSet &toUpdate)
{
    toUpdate.AddObject(pSelf, 1);
    for (auto i = cmds.begin(); i != cmds.end(); ++i)
        toUpdate.AddObject(i->second, 1);
    for (auto k = reqs.begin(); k != reqs.end(); ++k)
        toUpdate.AddObject(*k, 1);
    if (pProposal.IsValid())
    {
        toUpdate.AddObject(pProposal, 1);
        for (int i = 0; i < pProposal->items.size(); ++i)
            toUpdate.AddObject(pProposal->items[i], 1);
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CChatClient::Command(CDObject *pObj, float fpTime)
{
    cmds.Add(MakeObj(pObj), fpTime);
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CChatClient::Command(EChatCmd cmd, const char *pszParam)
{
    CChatCCmd *pCmd = dbgnew CChatCCmd;
    pCmd->cmd = cmd;
    pCmd->szParam = pszParam;
    Command(pCmd, 60);
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CChatClient::OnConnected()
{
    IWorldConnector.LMPConnected();
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CChatClient::NewServerInfo(CObj<CCSServerInfo> &dest)
{
    CChatServerInfo *pInfo = dbgnew CChatServerInfo;
    dest = pInfo;
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CChatClient::ProcessMasterServerReply(CMemoryStream &pkt)
{
    SA3GameServer a3s;
    CMemoryStream buf;
    CNetCompressor pack;
    pkt.Seek(0);
    int nSize;
    pkt.Read(&nSize, 4);
    if (nSize != pkt.GetSize())
    {
        ASSERT(0);
        return;
    }
    pack.Unpack(pkt, buf);
    buf.Seek(0);
    while (buf.GetPosition() < buf.GetSize() /* && buf.IsOk() */)
    {
        CChatServerInfo *pInfo = dbgnew CChatServerInfo;
        buf.Read(&pInfo->addr, sizeof(pInfo->addr));
        if (!a3s.UpdateFrom(buf))
        {
            ASSERT(0);
            return;
        }
        pInfo->allod = (ELMPAllod)a3s.nAllod;
        pInfo->bUnderPassword = a3s.bSecret;
        pInfo->nMaxPlayers = a3s.nMaxPlayers;
        pInfo->nPlayers = a3s.nPlayers;
        pInfo->szName = a3s.szName;
        pInfo->szQuestName = a3s.szQuestName;
        AddServerFromMaster(pInfo);
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////////
float CChatClient::GetMSReplyProgress(CMemoryStream &pkt)
{
    if (pkt.GetSize() < 4)
        return 0;
    int nSize;
    pkt.Seek(0);
    pkt.Read(&nSize, 4);
    return float(pkt.GetSize()) / (nSize == 0 ? 1 : nSize);
}
///////////////////////////////////////////////////////////////////////////////////////////////////
// Trade related funcs
static void StoreItem(CItem *pItem, CMemorySave &file)
{
    file.Clear();
    CSpell *pSpell = pItem->GetSpell(0);
    if (pSpell)
    {
        file.Write(pSpell->GetID());
        file.Write(pSpell->GetTypeID());
        file.Write(pSpell->GetSubtypeID());
        pSpell->Store(file);
    }
    else
        file.Write((int)-1);
    file.Write(pItem->GetTypeID());
    file.Write(pItem->GetSubtypeID());
    pItem->Store(file);
}
///////////////////////////////////////////////////////////////////////////////////////////////////
static CItem *LoadItem(CWorld &world, CMemorySave &file)
{
    file.Seek(0);
    CItem *pItem;
    CSpell *pSpell = 0;
    int nID;
    ETypeIDs tid;
    DWORD dwSub;
    file.Read(nID);
    if (nID > 0)
    {
        file.Read(&tid, sizeof(tid));
        file.Read(dwSub);
        pSpell = (CSpell *)world.GetSpellManager()->CreateData(tid, nID);
        pSpell->SetSubtypeID(dwSub);
        pSpell->Load(file);
    }
    file.Read(&tid, sizeof(tid));
    file.Read(dwSub);
    pItem = (CItem *)world.GetItemManager()->CreateData(tid);
    pItem->SetSubtypeID(dwSub);
    pItem->Load(file);
    return pItem;
}
///////////////////////////////////////////////////////////////////////////////////////////////////
static bool IsEqualItems(CChatItem *p1, CChatItem *p2)
{
    return p1->item.GetSize() == p2->item.GetSize() &&
           memcmp(p1->item.GetBuffer(), p2->item.GetBuffer(), p1->item.GetSize()) == 0;
}
///////////////////////////////////////////////////////////////////////////////////////////////////
struct SItemSearch
{
    CChatItem *p1;
    SItemSearch(CChatItem *_p1)
        : p1(_p1)
    {
    }
    bool operator()(CChatItem *p2) const { return IsEqualItems(p1, p2); }
};
///////////////////////////////////////////////////////////////////////////////////////////////////
void CChatClient::UpdateOfferItems()
{
    ASSERT(pProposal.IsValid());
    if (!pProposal.IsValid())
        return;
    // remember old items to use them if item is reused
    typedef std::vector<CPtr<CChatItem>> CItemVec;
    CItemVec items = pProposal->items;
    //
    pProposal->items.clear();
    pProposal->nMoney = itemsOffer.nMoney;
    for (int i = 0; i < itemsOffer.allWagonItems.GetSize(); i++)
    {
        CPtr<CChatItem> pItem = dbgnew CChatItem;
        StoreItem(itemsOffer.allWagonItems[i], pItem->item);
        auto k = std::find_if(items.begin(), items.end(), SItemSearch(pItem));
        if (k != items.end())
        {
            pProposal->items.push_back(*k);
            items.erase(k);
        }
        else
            pProposal->items.push_back(pItem);
    }
    //
    pProposal->nSign++;
    pProposal->bOkTrade = false;
    pProposal->SetUpdated();
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CChatClient::UpdateRemoteItems()
{
    int i;
    CChatItemProposal *pPeer = GetPeerTrade();
    if (!pPeer)
        return;
    // test if everything has arrived, if not - do not update anything
    for (i = 0; i < pPeer->items.size(); i++)
    {
        if (!pPeer->items[i]->IsValid())
            return;
        if (pPeer->items[i]->item.GetSize() == 0)
            return;
    }
    itemsRemote.Clear();
    itemsRemote.nMoney = pPeer->nMoney;
    std::vector<CSmartPtr<CItem>> items;
    for (i = 0; i < pPeer->items.size(); i++)
        items.push_back(LoadItem(itemsRemote, pPeer->items[i]->item));

    for (i = 0; i < pPeer->items.size(); i++)
        itemsRemote.AddItemToWagon(items[i], true);
    //
    RemoveOutdatedOks(pPeer->nSign);
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CChatClient::TradeOk(CParty &temp)
{
    ASSERT(pProposal.IsValid());
    CChatItemProposal *pPeer = GetPeerTrade();
    if (!pPeer || !pProposal.IsValid())
        return;
    pProposal->bOkTrade = true;
    pProposal->nOkTo = pPeer->nSign;
    pProposal->SetUpdated();
    //
    if (GetTrade(pProposal->nSign, pProposal->nOkTo))
        return;
    tradeOks.emplace_back();
    STradeOkTracking &t = tradeOks.back();
    t.nOkTo = pProposal->nOkTo;
    t.nSign = pProposal->nSign;
    t.offer = itemsOffer;
    t.remote = itemsRemote;
    t.temp = temp;
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CChatClient::ResetTradeOk()
{
    if (!pProposal.IsValid() || !pProposal->bOkTrade)
        return;
    pProposal->bOkTrade = false;
    pProposal->SetUpdated();
}
///////////////////////////////////////////////////////////////////////////////////////////////////
CChatClient::STradeOkTracking *CChatClient::GetTrade(int nSign, int nOkTo)
{
    for (auto i = tradeOks.begin(); i != tradeOks.end(); ++i)
    {
        if (i->nSign == nSign && i->nOkTo == nOkTo)
            return &(*i);
    }
    return 0;
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CChatClient::RemoveOutdatedOks(int nForeignSign)
{
    tradeOks.remove_if(STradeOutdate(nForeignSign));
}
///////////////////////////////////////////////////////////////////////////////////////////////////
CChatItemProposal *CChatClient::GetPeerTrade()
{
    if (!pProposal.IsValid())
        return 0;
    for (auto i = trades.begin(); i != trades.end(); ++i)
    {
        if ((*i)->pWhom == pProposal->pWho && (*i)->pWho == pProposal->pWhom && (*i)->IsValid())
            return *i;
    }
    return 0;
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CChatClient::RequestTrade(CChatPerson *pDest)
{
    ASSERT(pState->IsValid() && pState->pPerson->IsValid());
    if (!pState->IsValid() || !pState->pPerson->IsValid())
        return;
    pProposal = dbgnew CChatItemProposal;
    pProposal->pWho = pState->pPerson;
    pProposal->pWhom = pDest;
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void CChatClient::CancelTradeRequest()
{
    pProposal = 0;
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void OnAddState(CChatClient *pThis, CChatClientState *pObj)
{
    pThis->pState = pObj;
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void OnDelState(CChatClient *pThis, CChatClientState *pObj)
{
    pThis->pState = 0;
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void OnAddPerson(CChatClient *pThis, CChatPerson *pObj)
{
    pThis->people.push_back(MakePtr(pObj));
    theInterface.LMPPersonsUpdate();
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void OnUpdPerson(CChatClient *pThis, CChatPerson *pObj)
{
    theInterface.LMPPersonsUpdate(false);
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void OnDelPerson(CChatClient *pThis, CChatPerson *pObj)
{
    pThis->people.remove(pObj);
    theInterface.LMPPersonsUpdate();
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void OnAddPhrase(CChatClient *pThis, CChatPhrase *pObj)
{
    pThis->talk.push_back(pObj);
    theInterface.LMPPhrase(pObj->szText.c_str(), pObj->nOrder);
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void OnDelPhrase(CChatClient *pThis, CChatPhrase *pObj)
{
    pThis->talk.remove(pObj);
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void OnAddZone(CChatClient *pThis, CChatZone *pObj)
{
    pThis->pZone = pObj;
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void OnDelZone(CChatClient *pThis, CChatZone *pObj)
{
    pThis->pZone = 0;
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void OnAddBrief(CChatClient *pThis, CChatAvailableBriefing *pObj)
{
    pThis->briefings.push_back(pObj);
    IWorldConnector.LMPAddBriefing(pObj->szBriefingName.c_str());
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void OnDelBrief(CChatClient *pThis, CChatAvailableBriefing *pObj)
{
    pThis->briefings.remove(pObj);
    IWorldConnector.LMPDelBriefing(pObj->szBriefingName.c_str());
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void OnAddQuest(CChatClient *pThis, CChatCurrentQuest *pObj)
{
    pThis->pQuest = pObj;
    if (!pChatServer->IsValid())
    {
        IWorldConnector.LMPAddTextResource(pObj->szQuestName.c_str());
        IWorldConnector.LMPLoadQuestResources(pObj->szQuestName.c_str());
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void OnDelQuest(CChatClient *pThis, CChatCurrentQuest *pObj)
{
    pThis->pQuest = 0;
    // if ( !pChatServer->IsValid() )
    //	IWorldConnector.LMPFreeTextResources();
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void OnAddGSVar(CChatClient *pThis, CChatGlobalVar *pObj)
{
    pThis->vars.push_back(MakePtr(pObj));
    IWorldConnector.LMPSyncBriefings();
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void OnUpdGSVar(CChatClient *pThis, CChatGlobalVar *pObj)
{
    IWorldConnector.LMPSyncBriefings();
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void OnDelGSVar(CChatClient *pThis, CChatGlobalVar *pObj)
{
    pThis->vars.remove(MakePtr(pObj));
    IWorldConnector.LMPSyncBriefings();
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void OnAddMQFile(CChatClient *pThis, CChatMQFile *pObj)
{
    pThis->mqFiles.push_back(MakePtr(pObj));
    // test if we had the file if not add request
    if (IWorldConnector.HasLMPFile(pObj->szFileName.c_str()))
        return;
    // if not request the file
    CChatCMQRequest *pReq = dbgnew CChatCMQRequest;
    pReq->pFile = pObj;
    pThis->reqs.push_back(MakeObj(pReq));
}
///////////////////////////////////////////////////////////////////////////////////////////////////
struct STestMQRequest
{
    CChatMQFile *pTest;
    STestMQRequest(CChatMQFile *_pTest)
        : pTest(_pTest)
    {
    }
    bool operator()(const CChatCMQRequest *_pTest) const { return _pTest->pFile == pTest; }
};
///////////////////////////////////////////////////////////////////////////////////////////////////
void OnDelMQFile(CChatClient *pThis, CChatMQFile *pObj)
{
    pThis->mqFiles.remove(MakePtr(pObj));
    pThis->reqs.remove_if(STestMQRequest(pObj));
}
///////////////////////////////////////////////////////////////////////////////////////////////////
struct SCompareBLOBs
{
    bool operator()(const CChatBLOBPart *pA, const CChatBLOBPart *pB) const
    {
        return pA->nPart < pB->nPart;
    }
};
void OnAddBLOB(CChatClient *pThis, CChatBLOBPart *pObj)
{
    pThis->blobs.push_back(MakePtr(pObj));
    if (NConsole::RejectServerMQ())
        return;
    // test if every blob of some file has arrived if so file can be composed an request canceled
    CChatMQFile *pFile = pObj->pFile;
    // if pFile has not arrived yet
    if (pFile->szFileName == "")
    {
        ASSERT(0); // request is issued before object arrived - strange
        return;
    }
    //
    int nPartCount = 0;
    typedef std::vector<const CChatBLOBPart *> CBlobVector;
    CBlobVector parts;
    for (auto i = pThis->blobs.begin(); i != pThis->blobs.end(); ++i)
    {
        if ((*i)->pFile == pFile)
        {
            nPartCount++;
            parts.push_back(*i);
        }
    }
    //
    if (nPartCount == pFile->nTotalParts)
    {
        CMemoryStream res;
        std::sort(parts.begin(), parts.end(), SCompareBLOBs());
        for (auto k = parts.begin(); k != parts.end(); ++k)
            res.Write((*k)->cData, N_CHAT_BLOB_SIZE);
        // ASSERT( res.GetSize() <= pFile->nFileLength );
        res.SetSize(pFile->nFileLength);
        IWorldConnector.LMPAddFile(pFile->szFileName.c_str(), res);
        // cancel request
        pThis->reqs.remove_if(STestMQRequest(pFile));
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void OnDelBLOB(CChatClient *pThis, CChatBLOBPart *pObj)
{
    pThis->blobs.remove(MakePtr(pObj));
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void OnAddSCmd(CChatClient *pThis, CChatCommand *pObj)
{
    pThis->serverCmds.push_back(MakePtr(pObj));
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void OnAddTradeC(CChatClient *pThis, CChatItemProposal *pObj)
{
    pThis->trades.push_back(MakePtr(pObj));
    if (pThis->GetPeerTrade() == pObj)
    {
        pThis->ResetTradeOk();
        pThis->UpdateRemoteItems();
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void OnUpdTradeC(CChatClient *pThis, CChatItemProposal *pObj)
{
    if (pThis->GetPeerTrade() == pObj)
    {
        pThis->ResetTradeOk();
        pThis->UpdateRemoteItems();
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void OnDelTradeC(CChatClient *pThis, CChatItemProposal *pObj)
{
    pThis->trades.remove(MakePtr(pObj));
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void OnAddItem(CChatClient *pThis, CChatItem *pObj)
{
    pThis->ResetTradeOk();
    pThis->UpdateRemoteItems();
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void OnUpdItem(CChatClient *pThis, CChatItem *pObj)
{
    pThis->ResetTradeOk();
    pThis->UpdateRemoteItems();
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void OnDelItem(CChatClient *pThis, CChatItem *pObj)
{
    pThis->ResetTradeOk();
    pThis->UpdateRemoteItems();
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void OnAddItemXC(CChatClient *pThis, CChatItemXCHG *pObj)
{
    // xchange screen can be created
    ASSERT(!pObj->bDoIt);
    ASSERT(!pThis->pItemXCHG->IsValid());
    pThis->pItemXCHG = pObj;
    pThis->tradeOks.clear();
    pThis->itemsOffer.Clear();
    pThis->itemsRemote.Clear();
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void OnUpdItemXC(CChatClient *pThis, CChatItemXCHG *pObj)
{
    if (pObj->bDoIt && pObj == pThis->pItemXCHG)
    {
        // do xchange & close
        int nSign, nOkTo;
        if (pObj->pWho == (CChatItemProposal *)pThis->pProposal)
        {
            nSign = pObj->nWhoSign;
            nOkTo = pObj->nWhomSign;
        }
        else
        {
            nSign = pObj->nWhomSign;
            nOkTo = pObj->nWhoSign;
        }
        CChatClient::STradeOkTracking *pTrade = pThis->GetTrade(nSign, nOkTo);
        ASSERT(pTrade);
        if (pTrade)
            theInterface.LMPItemXCHGComplete(pTrade->remote, pTrade->offer, pTrade->temp);
        pThis->pItemXCHG = 0;
        pThis->pProposal = 0;
        pThis->tradeOks.clear();
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////////
void OnDelItemXC(CChatClient *pThis, CChatItemXCHG *pObj)
{
    if (pThis->pItemXCHG == pObj)
    {
        pThis->pItemXCHG = 0;
        pThis->pProposal = 0;
        pThis->tradeOks.clear();
    }
    // remove xchange screen
}
///////////////////////////////////////////////////////////////////////////////////////////////////
