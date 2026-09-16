#pragma once

#include "Basic.h"
#include "Streams.h"
#include <unordered_map>
#include <map>
#include <list>
#include <vector>
#include <ostream>
#include "CSlink.h"
#include "BasicFactory.h"
/////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////
class CCSBase;
class CCSLink;
extern ostream *pCSLog;
// CRAP - only for current version
inline ostream &operator<<(ostream &a, std::string &str)
{
    a << str.c_str();
    return a;
}
/////////////////////////////////////////////////////////////////////////////////////
enum ECSBaseOption
{
    CS_LINK_TIMEOUT, // after this number of seconds connection will be dropped
    CS_MAX_RTT       // influence on ID reuse
};
/////////////////////////////////////////////////////////////////////////////////////
// номер порта, который будет использоваться для поиска серверов или при создании
// своего сервера
namespace NCSParams {
extern int nCSServerPort;
extern int nCSRate;      // description in .cpp
extern int nCSLocalRate; //
}; // namespace NCSParams
/////////////////////////////////////////////////////////////////////////////////////
namespace NCSCommands {
// commands to server and to client, sent as first byte of update
typedef char ECSCmd;
const char CMD_NORMAL = 0;
const char CMD_LOGOUT = 1;
const char CMD_LOGGEDOUT = 2;
// to server only
const char CMDS_GET_INFO = 3;
const char CMDS_LOGIN = 4;
// to client only
const char CMDC_INFO = 5;
const char CMDC_ACCEPTED = 6;
const char CMDC_REJECTED = 7;
//
const char CMD_MASTER = (char)0xff;
}; // namespace NCSCommands
/////////////////////////////////////////////////////////////////////////////////////
// cpp lacks support for frameworks so create our own
namespace CS // Framework
{
extern CCSBase *pWorld;
extern CCSLink *pLink;
}; // namespace CS
/////////////////////////////////////////////////////////////////////////////////////
// class for convinient handling of framework`s contexts
class CFWContext
{
    void *&pFrameworkPtr;

public:
    template <class T>
    CFWContext(T *&pF, T *pData)
        : pFrameworkPtr((void *&)pF)
    {
        pF = pData;
    }
    ~CFWContext() { pFrameworkPtr = 0; }
};
/////////////////////////////////////////////////////////////////////////////////////
typedef unsigned short CS_OBJECTID;
typedef unsigned char CS_TYPEID;
typedef unsigned int CS_UPDATETIME;
typedef unsigned int CS_APPLICATION_ID;
typedef unsigned int CS_LINKID;
// number of different types allowed for CS interactions
const int N_CS_TYPES = 256;
// number of possible different packet numbers
const int CS_PACKET_ID_RANGE = 0x10000;
// включительно
const int CS_MAX_PRIORITY = 10;
/////////////////////////////////////////////////////////////////////////////////////
// elementary part of world, represent objects and events, D stands for distributed
class CDObject : public CObjectBase
{
    CS_UPDATETIME nUpdateTime; // when it was last updated
public:
    CDObject() { nUpdateTime = 1; }
    // unsigned char GetType() const { return 0; }
    // virtual  TimePassed( int nSegments ) { return true; } // objects may be dynamic
    // virtual bool IsDynamic() { return false; }
    // these functions can use CS framework
    virtual void UpdateTo(CBitStream &bits) = 0;
    virtual void UpdateFrom(CBitStream &bits) = 0;
    void SetUpdated() { nUpdateTime++; }

    friend class CCSBase;
    friend class CCSLink;
};
/////////////////////////////////////////////////////////////////////////////////////
class CCSFreeIDPool
{
    std::vector<CS_OBJECTID> freeIDs;
    CS_OBJECTID nLastUsedID;

public:
    CCSFreeIDPool() { nLastUsedID = 0; }
    void FreeID(CS_OBJECTID nID) { freeIDs.push_back(nID); }
    CS_OBJECTID GetID()
    {
        if (freeIDs.empty())
            return ++nLastUsedID;
        else
        {
            CS_OBJECTID nRes = freeIDs.back();
            freeIDs.pop_back();
            return nRes;
        }
    }
    bool HasReached(CS_OBJECTID nLimit) { return nLastUsedID >= nLimit; }
};
/////////////////////////////////////////////////////////////////////////////////////
// ID объекта для последующего повторного использования
struct SCSObjectID
{
    CS_OBJECTID nID;
    CS_TYPEID tID;

    SCSObjectID();
    SCSObjectID(CS_TYPEID _tID, CS_OBJECTID _nID)
        : nID(_nID)
        , tID(_tID)
    {
    }
};
/////////////////////////////////////////////////////////////////////////////////////
class CCSFreeIDBuffer
{
    std::vector<SCSObjectID> ids;

public:
    void AddFreeID(CS_TYPEID _tID, CS_OBJECTID _nID)
    {
        ASSERT(_tID < N_CS_TYPES);
        ids.emplace_back(SCSObjectID(_tID, _nID));
    }
    void ReclaimIDs(CCSFreeIDPool (&idPools)[N_CS_TYPES]);
};
/////////////////////////////////////////////////////////////////////////////////////
// ссылка на объект, подлежащий отсылке на клиент
class CCSShareInfo
{
public:
    CDObject *pObj;
    char nPriority; // 0 - minimal priority, CS_MAX_PRIORITY - highest priority

    CCSShareInfo() = default;
    CCSShareInfo(CDObject *_pObj, char _nPriority)
        : pObj(_pObj)
        , nPriority(_nPriority)
    {
    }
};
/////////////////////////////////////////////////////////////////////////////////////
// представление клиента
// server`ом в контексте cslink называется сторона с которой существует этот экземляр объекта
// client`ом - тот CSLink с которым он связан
class CCSLink : public CObjectBase
{
    OBJECT_BASIC_METHODS(CCSLink);

public:
    typedef unsigned short PACKET_ID;
    typedef unsigned short CHANNEL_DATA_OFFSET;

private:
    struct SChannelBlock
    {
        CHANNEL_DATA_OFFSET nOffset;
        unsigned char nLength;
        char cData[256];

        int GetSize() const { return sizeof(nOffset) + 1 + (int)nLength; }
        int GetHeaderSize() const { return sizeof(nOffset) + 1; }
    };
    //
    enum EUpdateType
    {
        UT_BANDW,       // object is marked as non updated if no ack received after rtt
        UT_DELAY,       // object is marked as updated after ack is received
        UT_REMOVEBANDW, // same as above but for object removal
        UT_REMOVEDELAY,
    };
    static CMemoryStream updatePkt;
    // link information
    PACKET_ID nPktSent;             // is used to order update packets
    CS_UPDATETIME nPktLastReceived; // number of last received pkt
    CS_UPDATETIME nPktHighCounter;  // high part of packet counter
    //
    // info on received pkts (acks on them should be sent)
    std::vector<CS_UPDATETIME> receivedPkts;
    //
    CS_LINKID linkID;            // linkID to check logout requests for authority
    float fpAvrgRTT, fpAvrgRTT2; // RTT statistics
    CCSNodeAddress peer;         // address of peer in communication
    float fpUpdateTimeDelay;     // recommended delay between updates (in seconds)
    float fpUpdateTimeElapsed;   // time elapsed since last update
    float fpTimeSinceLastRecv;   // time elapsed since last receive
    double fpCurrentTime;
    int nUpdateSize; // recommended update size

    //
    // information about objects to be sent to peer
    class CActiveObject;
    // элементарный апдейт одного объекта
    class CUpdateRequest
    {
    public:
        EUpdateType type;
        CS_UPDATETIME nUpdateTime; // update mark of sent information
        CPtr<CActiveObject> pSubj;
    };
    // пакет, сформированный из CUpdateRequest
    class CUpdate
    {
    public:
        double fpTimeCreated;
        PACKET_ID nPktNumber;
        std::list<CUpdateRequest> updates;
        SChannelBlock outBlock;
        bool bRolledBack;
        //
        CUpdate() { bRolledBack = false; }
    };
    // объект, подлежащий отсылке на клиент
    class CActiveObject : public CObjectBase
    {
        OBJECT_BASIC_METHODS(CActiveObject);

    public:
        CS_UPDATETIME nUpdateTime; // update that is known on client, 0 if nothing is known
        CS_OBJECTID nID; // ID by which this object is known on client 0 if has not been updated
        CS_TYPEID tID;
        unsigned char bDelay : 1;    // optimize for delay
        unsigned char nPriority : 7; // priority of this update
        CPtr<CDObject> pObject;      // object itself

        CActiveObject(CDObject *pObj = 0)
            : pObject(pObj)
        {
            nUpdateTime = 0;
            nID = 0;
        }
    };

    //
    // IDs tracking for objects sent on client
    // используется буферизация освободившихся ID для предотвращения ссылок на удаленные
    // объекты из запоздавших пакетов
    CCSFreeIDPool idPools[N_CS_TYPES];
    std::list<CCSFreeIDBuffer> idReuse;
    float fpIDReuseElapsed;

    //
    // objects to be sent
    typedef std::map<int, CPtr<CActiveObject>> CActiveObjectsIDMap;
    typedef std::map<CDObject *, CPtr<CActiveObject>> CActiveObjectsMap;
    typedef std::map<CActiveObject *, CPtr<CActiveObject>> CActiveSet;
    //
    CActiveObjectsIDMap activeObjectIDs; // mapping of objects with ID`s for peer referencing
    CActiveObjectsMap activeObjects;     // all objects to be known on client
    CActiveSet activeObjsSorted[CS_MAX_PRIORITY + 1];
    // info on sent update packets
    std::list<CUpdate> sentUpdates, rolledUpdates; // unacknowledged updates

    //
    // information about objects from client
    // maps from ids to object owning struct and from pointer to ID for peer referencing
    class CClientCreatedObj
    {
    public:
        CObj<CDObject> pObj;
        CS_UPDATETIME nUpdateTime;
    };
    struct SClientObjID
    {
        CS_OBJECTID nID; // ID by which this object is known on client 0 if has not been updated
        CS_TYPEID tID;
    };
    typedef std::map<int, CClientCreatedObj> CCreatedSet;
    typedef std::map<CDObject *, SClientObjID> CCreatedIDSet;
    CCreatedSet createdObjs;    // objects created by this client
    CCreatedIDSet createObjIDs; // IDs of created objects

    // streaming data control structures
    // текущее смещение отсылаемых и принимаемых данных
    CHANNEL_DATA_OFFSET nChannelOutputOffset, nChannelInputOffset;
    // списки блоков данных нуждающихся в отсылке и находящихся на очереди на прием
    typedef std::list<SChannelBlock> SChannelBlockList;
    SChannelBlockList channelOutList, channelInList;

    // acknowledgements on packets cache
    DWORD dwAckedBits;
    PACKET_ID nAckedLast;

private:
    void RollbackUpdate(CUpdate &update);
    void CommitUpdate(CUpdate &update);
    //
    void SetServerID(CActiveObject *pAO);
    void FreeServerID(CActiveObject *pAO);
    void AddActiveObject(CCSShareInfo &info);
    void RemoveActiveObject(CActiveObjectsMap::iterator &k);
    void RemoveActiveObject(CActiveSet::iterator &k);
    void FormNewActiveSet();
    //
    void SendPktAcks(CBitStream &bits);
    void ReceiveAck(PACKET_ID nPkt);
    void ReceivePktAcks(CBitStream &bits);
    bool CheckRecvPacketNumber(CS_UPDATETIME nPkt);
    //
    void SendRefID(CBitStream &bits, unsigned int bExternal, CS_TYPEID tID, CS_OBJECTID nID);
    void RecvRefID(CBitStream &bits, unsigned int *pbExternal, CS_TYPEID *ptID, CS_OBJECTID *pnID);
    void SendID(CBitStream &bits, CS_TYPEID tID, CS_OBJECTID nID);
    void RecvID(CBitStream &bits, CS_TYPEID *ptID, CS_OBJECTID *pnID);
    //
    bool CreateUpdate(int nMaxSize);
    //
    void SetClientID(CDObject *pObj, CS_TYPEID tID, CS_OBJECTID nID);
    void FreeClientID(CDObject *pObj);
    CDObject *NewClientObj(CClientCreatedObj &cc, CS_TYPEID tID, CS_OBJECTID nID);
    bool ReceiveObject(CBitStream &bits, CS_UPDATETIME nUpdateTime);
    //
    // funcs that should be available from CCSBase (other private should not)
    // is called before link destroy to inform via triggers about
    void SetAddress(const CCSNodeAddress &addr) { peer = addr; }
    bool TimePassed(double fpSeconds);
    bool ProcessUpdate(CBitStream &bits);
    void FreeCreatedObjects();

    void CreateOutChannelBlock(int nMaxSize);
    void GatherInChannelBlocks();
    void NewChannelInput(const SChannelBlock &block);

    void RegisterRTT(float fpRTT);

public:
    // channel data
    CMemoryStream channelOutBuf, channelInBuf;
    //
    CCSLink(CS_LINKID linkID = 0);
    const CCSNodeAddress &GetPeerAddress() const { return peer; }
    CS_LINKID GetLinkID() const { return linkID; }
    bool IsWeakConnect() const;
    void SetRate(int nBytePerSec);
    // refs store/load
    void WriteRef(CBitStream &bits, CDObject *pObj);
    CDObject *ReadRef(CBitStream &bits);
    template <class T>
    void ReadRef(CBitStream &bits, CPtr<T> &ref)
    {
        ref = (T *)ReadRef(bits);
    }
    // ?????????? some means to access created by peer objects
    friend class CCSBase; // for ProcessUpdate(), FreeCreatedObjects(), SetAddress(), TimePassed()
};
/////////////////////////////////////////////////////////////////////////////////////
// множество объектов, подлежащих отсылке
struct SCSActiveSet
{
    std::vector<CCSShareInfo> &toUpdate;
    //
    SCSActiveSet(std::vector<CCSShareInfo> &_toUpdate)
        : toUpdate(_toUpdate)
    {
    }
    void AddObject(CDObject *pObj, char nPriority)
    {
        ASSERT(pObj->IsValid());
        toUpdate.emplace_back();
        CCSShareInfo &n = toUpdate.back();
        n.pObj = pObj;
        n.nPriority = nPriority;
    }
};
/////////////////////////////////////////////////////////////////////////////////////
// два возможных типа пересылки - экономия трафика или задержки
enum ECSUpdateType
{
    CS_DELAY,
    CS_BANDW
};
/////////////////////////////////////////////////////////////////////////////////////
// something common for client & server
class CCSBase : public CFundament
{
public:
    //
    typedef std::list<CObj<CCSLink>> CClientList;

protected:
    //
    struct CClientAddrFunc
    {
        const CCSNodeAddress &addr;
        CClientAddrFunc(const CCSNodeAddress &a)
            : addr(a)
        {
        }
        bool operator()(const CObj<CCSLink> &client)
        {
            ASSERT(client.IsValid());
            return addr == client->GetPeerAddress();
        }
    };

    static CMemoryStream recvPkt;
    // data
    CS_APPLICATION_ID applicationID;
    // int nServerPort;
    int nLinkTimeout;
    int nMaxRTT;
    int nIDReuseInterval;
    //
    int nUpdateCounter; // counter of update cycles
    double fpTime;      // world time
    //
    CClientList clients;             // current client set
    CClassFactory<CDObject> factory; // class factory
    CCSLinksManager links;           // network manager
    char nDelay[N_CS_TYPES];         // флажки выбора типа передачи для разных видов объектов

    // updates sending
    void StartNet(int nPort) { links.Start(nPort); }
    bool SendUpdate(const CCSNodeAddress &addr, CMemoryStream &pkt);

    // clients manipulations
    CClientList::iterator GetClient(const CCSNodeAddress &addr);
    virtual void RemoveClient(CClientList::iterator i);
    CCSLink &AddClient(const CCSNodeAddress &addr, CS_LINKID linkID, int nRate);
    //
    CDObject *CreateObject(CS_TYPEID nType) { return factory.CreateObject(nType); }

    //
    int GetRate(const CCSNodeAddress &addr);
    // cmds
    void CmdLogout(const CCSNodeAddress &addr, CS_LINKID linkID);

    // packets processing
    virtual bool ProcessNormal(const CCSNodeAddress &addr, CBitStream &bits);
    virtual void ProcessGetInfo(const CCSNodeAddress &addr, CBitStream &bits);
    virtual void ProcessLogin(const CCSNodeAddress &addr, CBitStream &bits);
    virtual void ProcessLogout(const CCSNodeAddress &addr, CBitStream &bits);
    virtual void ProcessInfo(const CCSNodeAddress &addr, CBitStream &bits);
    virtual void ProcessAccepted(const CCSNodeAddress &addr, CBitStream &bits);
    virtual void ProcessRejected(const CCSNodeAddress &addr, CBitStream &bits);
    virtual void ProcessMaster(const CCSNodeAddress &addr, CBitStream &bits);

    //
    virtual void OnClientDrop(CCSLink *pLink) {}
    virtual void OnClientExit(CCSLink *pLink) {}
    //
    // functions for application specific logic (pLink in framework is set to calling client)
    // determines what objects should be visible on client
    virtual void GetActiveObjects(SCSActiveSet &toUpdate);

    //
    // triggers on different operations with objects created by peer
    enum ETriggerType
    {
        TT_ADD,
        TT_UPD,
        TT_DEL
    };
    // typedef void (CCSBase::*TriggerFunc)( CDObject* );
    typedef void (*TriggerFunc)(CCSBase *, CDObject *);
    typedef std::vector<TriggerFunc> CTriggerSet;
    CTriggerSet addTriggers[N_CS_TYPES];
    CTriggerSet updateTriggers[N_CS_TYPES];
    CTriggerSet removeTriggers[N_CS_TYPES];
    // triggers are called when peer sends update information on pObj
    void TriggerOnAdd(CDObject *pObj, CS_TYPEID tID);
    void TriggerOnUpdate(CDObject *pObj, CS_TYPEID tID);
    void TriggerOnRemove(CDObject *pObj, CS_TYPEID tID);
    // register some special handler on event
    void RegisterTrigger(ETriggerType tt, CS_TYPEID tID, TriggerFunc func);
    // lack for more powerful template (like void (W::*func)(T*)) make this neccessary
    // but this template lacks for type mismatch check in func argument and T
    template <class W, class T>
    void RegisterTrigger(ETriggerType tt, void (*func)(W *, T *))
    {
        CObj<T> pObj = dbgnew T;
        RegisterTrigger(tt, factory.GetObjectTypeID(pObj), (TriggerFunc)func);
    }

public:
    CCSBase();
    ~CCSBase();
    virtual void TimePassed(double fpSeconds);
    template <class TT>
    void RegisterType(int nTypeID, TT *(*func)(), ECSUpdateType ut)
    {
        ASSERT(nTypeID >= 0 && nTypeID < N_CS_TYPES);
        nDelay[nTypeID] = (ut == CS_DELAY);
        // FIXME: There was RegisterType here, but it has different arguments number
		factory.RegisterTypeSafe(nTypeID, (CClassFactory<CDObject>::newFunc)func);
    }
    void SetOpt(ECSBaseOption option, int nVal);
    int GetOpt(ECSBaseOption option);
    const CClientList &GetClients() const { return clients; }

    friend class CCSLink;
};
/////////////////////////////////////////////////////////////////////////////////////
// macro for registering CFundament derivatives
#define REGISTER_CS_CLASS(world, N, name, ut) world.RegisterType(N, &name## ::New##name, ut);
/////////////////////////////////////////////////////////////////////////////////////
