#pragma once
///////////////////////////////////////////////////////////////////////////////////////////////////
#include "Streams.h"
#include "CSmain.h"
#include "SaveLoader.h"
#include "party.h"
///////////////////////////////////////////////////////////////////////////////////////////////////
/* Objects participating in chat server operation
    -server side created
     -person (with head info and other params)
     -phrase to be shown (text)
     -zone available/unavailable (with zone name)
    -client side created
     -say something (text) -its an object!
     -cmd something chat/kick/ban/lock/shutdown/camping*/
/////////////////////////////////////////////////////////////////////////////////////
class CChatServer;
class CChatClient;
extern CPtr<CChatServer> pChatServer;
extern CPtr<CChatClient> pChatClient;
//
extern void LMPTimePassed();
/////////////////////////////////////////////////////////////////////////////////////
template <class T, class A = std::allocator<std::pair<float, T>>>
class CTimedList : public std::list<std::pair<float, T>, A>
{
    typedef std::pair<float, T> CElem;

public:
    void TimePassed(float fpTime, CTimedList *pOut = 0)
    {
        for (auto i = this->begin(); i != this->end();)
        {
            auto k = i++;
            k->first -= fpTime;
            if (k->first < 0)
            {
                if (pOut)
                    pOut->splice(pOut->end(), *this, k);
                else
                    this->erase(k);
            }
        }
    }
    void Add(const T &data, float fpTime) { this->push_back(CElem(fpTime, data)); }
};
/////////////////////////////////////////////////////////////////////////////////////
const int N_CHAT_PASSWORD_LENGTH = 20;
enum EChatCmd
{
    CC_SELECT_ZONE,
    CC_COMPLETE_BRIEFING,
    CC_KICK,
    CC_BAN,
    CC_KICK_ID,
    CC_BAN_ID
};
/////////////////////////////////////////////////////////////////////////////////////
enum EChatServerCmd
{
    CS_GIVE_XP,
    CS_GIVE_MONEY,
    CS_GIVE_ITEM,
    CS_REMOVE_ITEM,
    CS_REMOVE_QUEST_ITEMS
};
/////////////////////////////////////////////////////////////////////////////////////
enum EChatPersonState
{
    CPS_ZONE,
    CPS_BASE,
    CPS_CAMP,
    CPS_CONNECTING,
    CPS_START
};
/////////////////////////////////////////////////////////////////////////////////////
enum ERejectReason
{
    CSR_TIMEOUT = 0,
    CSR_SERVER_FULL,
    CSR_IP_BANNED,
    CSR_WRONG_PASSWORD
};
/////////////////////////////////////////////////////////////////////////////////////
enum EChatMessages
{
    CMS_PLAYERENTER,
    CMS_PLAYERLEAVE,
    CMS_PLAYERKICKED,
    CMS_PLAYERBANNED,
    CMS_PLAYERDROPPED,
    CMS_QUESTTAKEN,
    CMS_QUESTCANCELED,
    CMS_QUESTCOMPLETED,
    CMS_ZONEENABLED,
    CMS_ZONEDISABLED,
    CMS_TRADEREQUEST,
    CMS_TRADECANCEL,
    CMS_PAUSEDBY,
};
/////////////////////////////////////////////////////////////////////////////////////
class CChatServerInfo : public CCSServerInfo
{
    OBJECT_BASIC_METHODS(CChatServerInfo);

public:
    std::string szName;
    ELMPAllod allod;
    std::string szQuestName;
    char nPlayers, nMaxPlayers;
    bool bUnderPassword;

    CChatServerInfo();
    virtual void UpdateTo(CBitStream &bits);
    virtual void UpdateFrom(CBitStream &bits);
};
/////////////////////////////////////////////////////////////////////////////////////
// SERVER created objects
/////////////////////////////////////////////////////////////////////////////////////
// один из персонажей на сервере
class CChatPerson : public CDObject
{
    OBJECT_BASIC_METHODS(CChatPerson);

public:
    std::string szName;
    std::string szPrototype;
    EChatPersonState state;
    bool bLeader;
    int nServerID;
    int nUniquePersonID;
    //
    int nChatColor; // server side only
    int nFirstChatMsg;
    //
    CChatPerson()
    {
        bLeader = false;
        state = CPS_BASE;
        nServerID = -1;
        nChatColor = -1;
        nUniquePersonID = -1;
    }
    //
    virtual void UpdateTo(CBitStream &bits)
    {
        bits.Write(szName);
        bits.Write(szPrototype);
        bits.Write(state);
        bits.Write(bLeader);
        bits.Write(nServerID);
        bits.Write(nUniquePersonID);
    }
    virtual void UpdateFrom(CBitStream &bits)
    {
        bits.Read(szName);
        bits.Read(szPrototype);
        bits.Read(state);
        bits.Read(bLeader);
        bits.Read(nServerID);
        bits.Read(nUniquePersonID);
    }
};
/////////////////////////////////////////////////////////////////////////////////////
// одна из строчек в чат окне, упорядочиваются по nOrder
class CChatPhrase : public CDObject
{
    OBJECT_BASIC_METHODS(CChatPhrase);

public:
    int nOrder;
    std::string szText; // how to make coloring?
    //
    CPtr<CCSLink> pWhom; // server side only
    //
    virtual void UpdateTo(CBitStream &bits)
    {
        bits.Write(nOrder);
        bits.Write(szText);
    }
    virtual void UpdateFrom(CBitStream &bits)
    {
        bits.Read(nOrder);
        bits.Read(szText);
    }
};
/////////////////////////////////////////////////////////////////////////////////////
// доступная зона (если таковая имеется)
class CChatZone : public CDObject
{
    OBJECT_BASIC_METHODS(CChatZone);

public:
    std::string szAddress;
    std::string szZoneName;
    //
    virtual void UpdateTo(CBitStream &bits)
    {
        bits.Write(szAddress);
        bits.Write(szZoneName);
    }
    virtual void UpdateFrom(CBitStream &bits)
    {
        bits.Read(szAddress);
        bits.Read(szZoneName);
    }
};
/////////////////////////////////////////////////////////////////////////////////////
// общая информация для клиента
class CChatClientState : public CDObject
{
    OBJECT_BASIC_METHODS(CChatClientState);

public:
    CPtr<CChatPerson> pPerson;
    CPtr<CCSLink> pClient; // server side link to client this state belongs

    virtual void UpdateTo(CBitStream &bits) { CS::pLink->WriteRef(bits, pPerson); }
    virtual void UpdateFrom(CBitStream &bits) { CS::pLink->ReadRef(bits, pPerson); }
};
/////////////////////////////////////////////////////////////////////////////////////
class CChatAvailableBriefing : public CDObject
{
    OBJECT_BASIC_METHODS(CChatAvailableBriefing);

public:
    std::string szBriefingName;

    virtual void UpdateTo(CBitStream &bits) { bits.Write(szBriefingName); }
    virtual void UpdateFrom(CBitStream &bits) { bits.Read(szBriefingName); }
};
/////////////////////////////////////////////////////////////////////////////////////
class CChatGlobalVar : public CDObject
{
    OBJECT_BASIC_METHODS(CChatGlobalVar);

public:
    std::string szVarName;
    float fpVal;
    unsigned int nOrder;

    virtual void UpdateTo(CBitStream &bits)
    {
        bits.Write(szVarName);
        bits.Write(fpVal);
        bits.Write(nOrder);
    }
    virtual void UpdateFrom(CBitStream &bits)
    {
        bits.Read(szVarName);
        bits.Read(fpVal);
        bits.Read(nOrder);
    }
};
/////////////////////////////////////////////////////////////////////////////////////
// текущий выбранный квест
class CChatCurrentQuest : public CDObject
{
    OBJECT_BASIC_METHODS(CChatCurrentQuest);

public:
    std::string szQuestName;

    virtual void UpdateTo(CBitStream &bits) { bits.Write(szQuestName); }
    virtual void UpdateFrom(CBitStream &bits) { bits.Read(szQuestName); }
};
/////////////////////////////////////////////////////////////////////////////////////
class CChatBLOBPart;
class CChatMQFile : public CDObject
{
    OBJECT_BASIC_METHODS(CChatMQFile);

public:
    unsigned short nTotalParts;
    int nFileLength;
    std::string szFileName;
    std::vector<CObj<CChatBLOBPart>> parts; // for use on server side

    virtual void UpdateTo(CBitStream &bits)
    {
        bits.Write(nTotalParts);
        bits.Write(nFileLength);
        bits.Write(szFileName);
    }
    virtual void UpdateFrom(CBitStream &bits)
    {
        bits.Read(nTotalParts);
        bits.Read(nFileLength);
        bits.Read(szFileName);
    }
};
/////////////////////////////////////////////////////////////////////////////////////
const int N_CHAT_BLOB_SIZE = 64;
class CChatBLOBPart : public CDObject
{
    OBJECT_BASIC_METHODS(CChatBLOBPart);

public:
    CPtr<CChatMQFile> pFile;
    unsigned short nPart;
    char cData[N_CHAT_BLOB_SIZE];

    virtual void UpdateTo(CBitStream &bits)
    {
        CS::pLink->WriteRef(bits, pFile);
        bits.Write(nPart);
        bits.Write(cData, sizeof(cData));
    }
    virtual void UpdateFrom(CBitStream &bits)
    {
        CS::pLink->ReadRef(bits, pFile);
        bits.Read(nPart);
        bits.Read(cData, sizeof(cData));
    }
};
/////////////////////////////////////////////////////////////////////////////////////
class CChatCommand : public CDObject
{
    OBJECT_BASIC_METHODS(CChatCommand);

public:
    CPtr<CCSLink> pClient; // to whom cmd
    EChatServerCmd cmd;
    float fpParam;
    std::string szParam;

    virtual void UpdateTo(CBitStream &bits)
    {
        bits.Write(cmd);
        bits.Write(fpParam);
        bits.Write(szParam);
    }
    virtual void UpdateFrom(CBitStream &bits)
    {
        bits.Read(cmd);
        bits.Read(fpParam);
        bits.Read(szParam);
    }
};
/////////////////////////////////////////////////////////////////////////////////////
class CChatItemProposal;
class CChatItemXCHG : public CDObject
{
    OBJECT_BASIC_METHODS(CChatItemXCHG);

public:
    CPtr<CChatPerson> pPersonWho, pPersonWhom; // server side ptrs for convenience
    CPtr<CChatItemProposal> pWho;
    CPtr<CChatItemProposal> pWhom;
    bool bDoIt;
    int nWhoSign, nWhomSign;

    CChatItemXCHG()
    {
        bDoIt = false;
        nWhoSign = 0;
        nWhomSign = 0;
    }
    virtual void UpdateTo(CBitStream &bits);
    virtual void UpdateFrom(CBitStream &bits);
};
/////////////////////////////////////////////////////////////////////////////////////
class CChatChannelData : public CDObject
{
    OBJECT_BASIC_METHODS(CChatChannelData);

public:
    unsigned int nOffset;
    unsigned char nLength;
    char cData[256];

    virtual void UpdateTo(CBitStream &bits) {}
    virtual void UpdateFrom(CBitStream &bits) {}
};
/////////////////////////////////////////////////////////////////////////////////////
// CLIENT created objects
/////////////////////////////////////////////////////////////////////////////////////
// когда новый клиент появляется в игре он создает такой объект
class CChatCAppear : public CDObject
{
    OBJECT_BASIC_METHODS(CChatCAppear);

public:
    std::string szName;
    std::string szPrototype;
    bool bLocalServer;
    EChatPersonState state;
    int nServerID;
    CPtr<CChatClientState> pState; // server side pointer (to remove person after disconnect)
    //
    CChatCAppear()
    {
        bLocalServer = false;
        state = CPS_START;
        nServerID = -1;
    }
    //
    virtual void UpdateTo(CBitStream &bits)
    {
        bits.Write(szName);
        bits.Write(szPrototype);
        bits.Write(state);
        bits.Write(bLocalServer);
        bits.Write(nServerID);
    }
    virtual void UpdateFrom(CBitStream &bits)
    {
        bits.Read(szName);
        bits.Read(szPrototype);
        bits.Read(state);
        bits.Read(bLocalServer);
        bits.Read(nServerID);
    }
};
/////////////////////////////////////////////////////////////////////////////////////
class CChatCSay : public CDObject
{
    OBJECT_BASIC_METHODS(CChatCSay);

public:
    std::string szText;
    //
    virtual void UpdateTo(CBitStream &bits) { bits.Write(szText); }
    virtual void UpdateFrom(CBitStream &bits) { bits.Read(szText); }
};
/////////////////////////////////////////////////////////////////////////////////////
class CChatCCmd : public CDObject
{
    OBJECT_BASIC_METHODS(CChatCCmd);

public:
    EChatCmd cmd;
    std::string szParam;
    //
    virtual void UpdateTo(CBitStream &bits)
    {
        bits.Write(cmd);
        bits.Write(szParam);
    }
    virtual void UpdateFrom(CBitStream &bits)
    {
        bits.Read(cmd);
        bits.Read(szParam);
    }
};
/////////////////////////////////////////////////////////////////////////////////////
// единичный элемент обмена
class CChatItem : public CDObject
{
    OBJECT_BASIC_METHODS(CChatItem);

public:
    CMemorySave item;

    virtual void UpdateTo(CBitStream &bits)
    {
        unsigned short nSize = item.GetSize();
        bits.Write(nSize);
        bits.Write(item.GetBuffer(), nSize);
    }
    virtual void UpdateFrom(CBitStream &bits)
    {
        unsigned short nSize;
        bits.Read(nSize);
        item.SetSize(nSize);
        bits.Read(item.GetBufferForWrite(), nSize);
    }
};
/////////////////////////////////////////////////////////////////////////////////////
// предложение об обмене
class CChatItemProposal : public CDObject
{
    OBJECT_BASIC_METHODS(CChatItemProposal);

public:
    std::vector<CPtr<CChatItem>> items;
    unsigned int nMoney;
    bool bOkTrade;
    int nOkTo, nSign;
    CPtr<CChatPerson> pWho;
    CPtr<CChatPerson> pWhom;
    //
    bool bWasTrade; // server side only
    //
    CChatItemProposal()
    {
        nOkTo = 0;
        nSign = 1;
        bOkTrade = false;
        nMoney = 0;
        bWasTrade = false;
    }
    virtual void UpdateTo(CBitStream &bits);
    virtual void UpdateFrom(CBitStream &bits);
};
/////////////////////////////////////////////////////////////////////////////////////
// запрос .mq файла
class CChatCMQRequest : public CDObject
{
    OBJECT_BASIC_METHODS(CChatCMQRequest);

public:
    CPtr<CChatMQFile> pFile;
    CPtr<CCSLink> pClient; // server side pointer (to distinguish different client`s requests)

    virtual void UpdateTo(CBitStream &bits) { CS::pLink->WriteRef(bits, pFile); }
    virtual void UpdateFrom(CBitStream &bits) { CS::pLink->ReadRef(bits, pFile); }
};
/////////////////////////////////////////////////////////////////////////////////////
// SERVER & CLIENT itself
/////////////////////////////////////////////////////////////////////////////////////
struct SQDBBriefing;
class CChatServer : public CCSServer
{
    typedef std::list<CObj<CChatPerson>> CChatPersonList;
    typedef std::list<CObj<CChatPhrase>> CChatPhraseList;
    typedef std::list<CObj<CChatClientState>> CChatClientList;
    typedef std::list<CObj<CChatAvailableBriefing>> CChatBriefings;
    typedef std::list<CObj<CChatGlobalVar>> CChatVars;
    typedef std::list<CObj<CChatMQFile>> CMQFiles;
    typedef std::list<CPtr<CChatCMQRequest>> CRequests;
    typedef CTimedList<CObj<CChatCommand>> CCmdList;
    typedef std::list<CPtr<CChatItemProposal>> CChatTrade;
    typedef std::list<CPtr<CChatItemXCHG>> CChatXCHGList;
    typedef CTimedList<CPtr<CChatItemXCHG>> CTimedXCHGList;

protected:
    float fpTimeToInformMaster;
    bool bInformMaster;
    unsigned int nMasterPwd;
    CCSNodeAddress masterAddr;
    //
    virtual void GetActiveObjects(SCSActiveSet &toUpdate);
    // triggers are called when peer sends update information on pObj
    friend void OnAddAppear(CChatServer *pThis, CChatCAppear *pObj);
    friend void OnUpdAppear(CChatServer *pThis, CChatCAppear *pObj);
    friend void OnDelAppear(CChatServer *pThis, CChatCAppear *pObj);
    friend void OnAddSay(CChatServer *pThis, CChatCSay *pObj);
    friend void OnAddCmd(CChatServer *pThis, CChatCCmd *pObj);
    friend void OnAddFileReq(CChatServer *pThis, CChatCMQRequest *pObj);
    friend void OnDelFileReq(CChatServer *pThis, CChatCMQRequest *pObj);
    friend void OnAddTrade(CChatServer *pThis, CChatItemProposal *pObj);
    friend void OnUpdTrade(CChatServer *pThis, CChatItemProposal *pObj);
    friend void OnDelTrade(CChatServer *pThis, CChatItemProposal *pObj);
    friend void OnUpdTItem(CChatServer *pThis, CChatItem *pObj);

    virtual void CreateServerInfo(CObj<CCSServerInfo> &pRes);
    virtual bool CheckLogin(const CCSNodeAddress &addr, CBitStream &file, char &bRejectReason);
    //
    bool IsLeader(CCSLink *pLink);
    CCSLink *GetLeader();
    bool IsBriefing(const std::string &szBrief);
    void AddBriefing(const std::string &szBrief);
    void GiveQuests();
    void BrSelectQuest(const std::string &szQuest);
    void BrRefuseQuest();
    void BrCompleteQuest(const std::string &szQuest);
    void CommandItems(const SQDBBriefing *pBrief, bool bEraseQuestItems);
    CChatClientState *GetClient1(CCSLink *pLink);
    CChatVars::iterator GetVar(const std::string &szVar);
    CChatVars::iterator GetVarNew(const std::string &szVar);
    CChatItemXCHG *GetItemXCHG(CChatItemProposal *pWho, CChatItemProposal *pWhom);
    CChatItemProposal *GetPeerTrade(CChatItemProposal *pWho);
    //
    CCSLink *GetClient1(const std::string &szName);
    CCSLink *GetClient1(CChatPerson *pPers);
    CCSLink *GetClient1(int nUniquePersonID);
    void Kick(CCSLink *pLink);
    void Ban(CCSLink *pLink);
    void SendMasterServerInfo();
    virtual void ProcessMaster(const CCSNodeAddress &addr, CBitStream &bits);
    //
    virtual void OnClientDrop(CCSLink *pLink);
    virtual void OnClientExit(CCSLink *pLink);

public:
    int nTalkOrder;
    CChatPersonList people;
    CChatPhraseList talk;
    CMQFiles mqFiles;
    CRequests requests; // current file requests
    CObj<CChatZone> pZone;
    CChatClientList clientStates;
    CObj<CChatCurrentQuest> pQuest;
    CChatBriefings briefings;
    CChatVars vars;
    CCmdList cmds;
    CChatTrade trades;
    CChatXCHGList xchgs;
    CTimedXCHGList timedXCHGs;
    std::list<CCSNodeAddress> banned;
    //
    std::string szServerName;
    std::string szLastCompletedQuest;
    int nMaxClients;
    ELMPAllod allod;
    char szPassword[N_CHAT_PASSWORD_LENGTH];
    int nUniqueClientID;
    //
    CChatServer(ELMPAllod _allod);
    virtual void TimePassed(double fpSeconds);
    bool CompleteQuest(const std::string &szQuest);
    void GSSetVar(const std::string &szVar, float fpVal);
    void GSSetVarMax(const std::string &szVar, float fpVal);
    void GSDelVar(const std::string &szVar);
    float GSGetVar(const std::string &szVar);
    void AddFile(const std::string &szFName, CMemoryStream &file); // like 1mpTest.mq
    void RemoveFiles();
    void Command(CCSLink *pClient, EChatServerCmd cmd, float fpParam, std::string szParam);
    int GetLeaderServerID();
    int GetUnusedChatColor();
    CChatPerson *GetPerson(int nServerID);
    void AddChatPhrase(const char *pszText, CChatPerson *pWhom = 0);
    void Inform(EChatMessages msg, const char *pszParam, CChatPerson *pWhom = 0);
    void InformAllButLeader(EChatMessages msg, const char *pszParam);
    void SetPassword(const char *pszPwd);
    bool CanBeConnectingToServer(CCSLink *pLink);
    void SetInformMaster(bool _bInform);
};
/////////////////////////////////////////////////////////////////////////////////////
class CChatClient : public CCSClient
{
    struct STradeOkTracking
    {
        int nOkTo, nSign;
        CParty offer, remote, temp;
    };
    //
    struct STradeOutdate
    {
        int nVal;
        STradeOutdate(int n)
            : nVal(n)
        {
        }
        bool operator()(const STradeOkTracking &a) const { return a.nOkTo < nVal; }
    };
    //
protected:
    virtual void GetActiveObjects(SCSActiveSet &toUpdate);
    // triggers are called when peer sends update information on pObj
    friend void OnAddState(CChatClient *pThis, CChatClientState *pObj);
    friend void OnDelState(CChatClient *pThis, CChatClientState *pObj);
    friend void OnAddPerson(CChatClient *pThis, CChatPerson *pObj);
    friend void OnUpdPerson(CChatClient *pThis, CChatPerson *pObj);
    friend void OnDelPerson(CChatClient *pThis, CChatPerson *pObj);
    friend void OnAddPhrase(CChatClient *pThis, CChatPhrase *pObj);
    friend void OnDelPhrase(CChatClient *pThis, CChatPhrase *pObj);
    friend void OnAddZone(CChatClient *pThis, CChatZone *pObj);
    friend void OnDelZone(CChatClient *pThis, CChatZone *pObj);
    friend void OnAddBrief(CChatClient *pThis, CChatAvailableBriefing *pObj);
    friend void OnDelBrief(CChatClient *pThis, CChatAvailableBriefing *pObj);
    friend void OnAddQuest(CChatClient *pThis, CChatCurrentQuest *pObj);
    friend void OnDelQuest(CChatClient *pThis, CChatCurrentQuest *pObj);
    friend void OnAddGSVar(CChatClient *pThis, CChatGlobalVar *pObj);
    friend void OnUpdGSVar(CChatClient *pThis, CChatGlobalVar *pObj);
    friend void OnDelGSVar(CChatClient *pThis, CChatGlobalVar *pObj);
    friend void OnAddMQFile(CChatClient *pThis, CChatMQFile *pObj);
    friend void OnDelMQFile(CChatClient *pThis, CChatMQFile *pObj);
    friend void OnAddBLOB(CChatClient *pThis, CChatBLOBPart *pObj);
    friend void OnDelBLOB(CChatClient *pThis, CChatBLOBPart *pObj);
    friend void OnAddSCmd(CChatClient *pThis, CChatCommand *pObj);
    friend void OnAddTradeC(CChatClient *pThis, CChatItemProposal *pObj);
    friend void OnUpdTradeC(CChatClient *pThis, CChatItemProposal *pObj);
    friend void OnDelTradeC(CChatClient *pThis, CChatItemProposal *pObj);
    friend void OnAddItem(CChatClient *pThis, CChatItem *pObj);
    friend void OnUpdItem(CChatClient *pThis, CChatItem *pObj);
    friend void OnDelItem(CChatClient *pThis, CChatItem *pObj);
    friend void OnAddItemXC(CChatClient *pThis, CChatItemXCHG *pObj);
    friend void OnUpdItemXC(CChatClient *pThis, CChatItemXCHG *pObj);
    friend void OnDelItemXC(CChatClient *pThis, CChatItemXCHG *pObj);

    virtual void OnConnected();
    virtual void NewServerInfo(CObj<CCSServerInfo> &dest);
    void ExecuteServerCommands();
    void UpdateRemoteItems();
    void RemoveOutdatedOks(int nForeignSign);
    STradeOkTracking *GetTrade(int nSign, int nOkTo);

public:
    typedef std::list<CPtr<CChatPerson>> CChatPersonList;
    typedef std::list<CPtr<CChatPhrase>> CChatPhraseList;
    typedef CTimedList<CObj<CDObject>> CCmdList;
    typedef std::list<CPtr<CChatAvailableBriefing>> CBriefings;
    typedef std::list<CPtr<CChatGlobalVar>> CGSVars;
    typedef std::list<CPtr<CChatMQFile>> CMQFiles;
    typedef std::list<CPtr<CChatBLOBPart>> CBLOBs;
    typedef std::list<CPtr<CChatItemProposal>> CTrades;
    typedef std::list<CPtr<CChatCommand>> CServerCmds;
    typedef std::list<CObj<CChatCMQRequest>> CMQRequests;
    typedef std::list<STradeOkTracking> CTradeOkList;
    //
    CChatPersonList people;
    CChatPhraseList talk;
    CMQFiles mqFiles;
    CBLOBs blobs;
    CMQRequests reqs;
    CPtr<CChatZone> pZone;
    CPtr<CChatClientState> pState;
    CPtr<CChatCurrentQuest> pQuest;
    CBriefings briefings;
    CGSVars vars;
    CObj<CChatCAppear> pSelf;
    CCmdList cmds;
    CServerCmds serverCmds;
    CObj<CChatItemProposal> pProposal;
    CTrades trades;
    CTradeOkList tradeOks;
    CPtr<CChatItemXCHG> pItemXCHG;
    //
    ELMPAllod allod;
    //
    CParty itemsOffer, itemsRemote;
    CChatItemProposal *GetPeerTrade();
    void UpdateOfferItems();
    void TradeOk(CParty &temp);
    void ResetTradeOk();
    void RequestTrade(CChatPerson *pDest);
    void CancelTradeRequest();
    //
    CChatClient();
    virtual void TimePassed(double fpSeconds);
    //
    void Command(CDObject *pObj, float fpTime);
    void Command(EChatCmd cmd, const char *pszParam);
    //
    void ProcessMasterServerReply(CMemoryStream &pkt);
    float GetMSReplyProgress(CMemoryStream &pkt); // return what part of whole reply is pkt, 0-1
};
///////////////////////////////////////////////////////////////////////////////////////////////////
void OnAddAppear(CChatServer *pThis, CChatCAppear *pObj);
void OnUpdAppear(CChatServer *pThis, CChatCAppear *pObj);
void OnDelAppear(CChatServer *pThis, CChatCAppear *pObj);
void OnAddSay(CChatServer *pThis, CChatCSay *pObj);
void OnAddCmd(CChatServer *pThis, CChatCCmd *pObj);
void OnAddFileReq(CChatServer *pThis, CChatCMQRequest *pObj);
void OnDelFileReq(CChatServer *pThis, CChatCMQRequest *pObj);
void OnAddTrade(CChatServer *pThis, CChatItemProposal *pObj);
void OnUpdTrade(CChatServer *pThis, CChatItemProposal *pObj);
void OnDelTrade(CChatServer *pThis, CChatItemProposal *pObj);
void OnUpdTItem(CChatServer *pThis, CChatItem *pObj);

void OnAddState(CChatClient *pThis, CChatClientState *pObj);
void OnDelState(CChatClient *pThis, CChatClientState *pObj);
void OnAddPerson(CChatClient *pThis, CChatPerson *pObj);
void OnUpdPerson(CChatClient *pThis, CChatPerson *pObj);
void OnDelPerson(CChatClient *pThis, CChatPerson *pObj);
void OnAddPhrase(CChatClient *pThis, CChatPhrase *pObj);
void OnDelPhrase(CChatClient *pThis, CChatPhrase *pObj);
void OnAddZone(CChatClient *pThis, CChatZone *pObj);
void OnDelZone(CChatClient *pThis, CChatZone *pObj);
void OnAddBrief(CChatClient *pThis, CChatAvailableBriefing *pObj);
void OnDelBrief(CChatClient *pThis, CChatAvailableBriefing *pObj);
void OnAddQuest(CChatClient *pThis, CChatCurrentQuest *pObj);
void OnDelQuest(CChatClient *pThis, CChatCurrentQuest *pObj);
void OnAddGSVar(CChatClient *pThis, CChatGlobalVar *pObj);
void OnUpdGSVar(CChatClient *pThis, CChatGlobalVar *pObj);
void OnDelGSVar(CChatClient *pThis, CChatGlobalVar *pObj);
void OnAddMQFile(CChatClient *pThis, CChatMQFile *pObj);
void OnDelMQFile(CChatClient *pThis, CChatMQFile *pObj);
void OnAddBLOB(CChatClient *pThis, CChatBLOBPart *pObj);
void OnDelBLOB(CChatClient *pThis, CChatBLOBPart *pObj);
void OnAddSCmd(CChatClient *pThis, CChatCommand *pObj);
void OnAddTradeC(CChatClient *pThis, CChatItemProposal *pObj);
void OnUpdTradeC(CChatClient *pThis, CChatItemProposal *pObj);
void OnDelTradeC(CChatClient *pThis, CChatItemProposal *pObj);
void OnAddItem(CChatClient *pThis, CChatItem *pObj);
void OnUpdItem(CChatClient *pThis, CChatItem *pObj);
void OnDelItem(CChatClient *pThis, CChatItem *pObj);
void OnAddItemXC(CChatClient *pThis, CChatItemXCHG *pObj);
void OnUpdItemXC(CChatClient *pThis, CChatItemXCHG *pObj);
void OnDelItemXC(CChatClient *pThis, CChatItemXCHG *pObj);
