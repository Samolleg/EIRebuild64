#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "CSbase.h"
#include <algorithm>

extern ostream *pCSLog = 0;
/////////////////////////////////////////////////////////////////////////////////////
using namespace std;
using namespace NCSCommands;
using namespace NCSParams;
/////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////
int NCSParams::nCSServerPort = 8888; // Default TCP port
int NCSParams::nCSRate = 2500;       // default rates for remote an local connections
int NCSParams::nCSLocalRate = 12500; //
const int N_MAX_UPDATE_SIZE = 1024;  // maximal update size of one object
const float FP_RTT_KEEP = 0.93f;     // RTT recalc rate
const int MAX_CHANNEL_BUFFER_LENGTH = 32000;

/////////////////////////////////////////////////////////////////////////////////////
// CS framework
CCSBase *CS::pWorld = 0;
CCSLink *CS::pLink = 0;
CMemoryStream CCSLink::updatePkt;
CMemoryStream CCSBase::recvPkt;
/////////////////////////////////////////////////////////////////////////////////////
// CCSFreeIDBuffer
/////////////////////////////////////////////////////////////////////////////////////
void CCSFreeIDBuffer::ReclaimIDs(CCSFreeIDPool (&idPools)[N_CS_TYPES])
{
    for (auto i = ids.begin(); i != ids.end(); ++i)
        idPools[i->tID].FreeID(i->nID);
    ids.clear();
}
/////////////////////////////////////////////////////////////////////////////////////
// CCSLink
/////////////////////////////////////////////////////////////////////////////////////
CCSLink::CCSLink(CS_LINKID _linkID)
{
    DWORD dwTick = GetTickCount();
    linkID = 0;
    // state = S_NONE;
    float fpRTT;
    if (CS::pWorld)
        fpRTT = CS::pWorld->nMaxRTT;
    else
        fpRTT = 5;
    fpAvrgRTT = fpRTT;
    fpAvrgRTT2 = sqr(fpAvrgRTT);
    //
    fpUpdateTimeDelay = 0.04f; // * ( 0.5f + (dwTick & 0xff ) / 255.0 );
    fpUpdateTimeElapsed = 0;
    fpTimeSinceLastRecv = 0;
    nUpdateSize = 220; // 512;
    nPktSent = (PACKET_ID)dwTick;
    nPktLastReceived = 0;
    nPktHighCounter = CS_PACKET_ID_RANGE;
    idReuse.emplace_back();
    idReuse.emplace_back();
    fpIDReuseElapsed = 0;
    linkID = _linkID;
    nChannelOutputOffset = 0;
    nChannelInputOffset = 0;
    fpCurrentTime = 0;
    dwAckedBits = 0;
    nAckedLast = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CCSLink::IsWeakConnect() const
{
    return fpTimeSinceLastRecv > fpAvrgRTT * 3;
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSLink::SetRate(int nBytesPerSec)
{
    nBytesPerSec = Max(nBytesPerSec, 500);
    nBytesPerSec = Min(nBytesPerSec, 50000);
    fpUpdateTimeDelay = 250.0f / nBytesPerSec;
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSLink::RegisterRTT(float fpRTT)
{
    ASSERT(fpRTT >= 0);
    fpAvrgRTT *= FP_RTT_KEEP;
    fpAvrgRTT += (1 - FP_RTT_KEEP) * fpRTT;
    fpAvrgRTT2 *= FP_RTT_KEEP;
    fpAvrgRTT2 += (1 - FP_RTT_KEEP) * sqr(fpRTT);
}
/////////////////////////////////////////////////////////////////////////////////////
bool CCSLink::TimePassed(double fpSeconds)
{
    CFWContext ctx(CS::pLink, this);
    fpTimeSinceLastRecv += fpSeconds;
    fpCurrentTime += fpSeconds;
    //
    float fpRTTDisperse = sqrt(fabs(sqr(fpAvrgRTT) - fpAvrgRTT2));
    float fpRTTDiscard = fpAvrgRTT + fpRTTDisperse * 3; // 99% probability if normal distribution
    fpRTTDiscard = Max(fpRTTDiscard, 0.1f);
    fpRTTDiscard = Min(fpRTTDiscard, (float)CS::pWorld->nMaxRTT);
    float fpMaxRTT = (float)CS::pWorld->nMaxRTT;
    // roll back updates that are out of estimated rtt
    list<CUpdate>::iterator it, itmp;
    for (it = sentUpdates.begin(); it != sentUpdates.end();)
    {
        double fpCrap = it->fpTimeCreated;
        if (fpCurrentTime - it->fpTimeCreated > fpRTTDiscard)
        {
            RollbackUpdate(*it);
            itmp = it;
            ++it;
            rolledUpdates.splice(rolledUpdates.end(), sentUpdates, itmp);
        }
        else
            break;
    }
    for (it = rolledUpdates.begin(); it != rolledUpdates.end();)
    {
        if (fpCurrentTime - it->fpTimeCreated > fpMaxRTT)
            it = rolledUpdates.erase(it);
        else
            ++it;
    }
    // form new update (if enough time passed etc)
    fpUpdateTimeElapsed += fpSeconds;
    while (fpUpdateTimeElapsed > fpUpdateTimeDelay)
    {
        fpUpdateTimeElapsed -= fpUpdateTimeDelay;
        if (CreateUpdate(nUpdateSize) && channelOutList.empty() && channelOutBuf.GetSize() == 0)
            break;
    }
    while (fpUpdateTimeElapsed > fpUpdateTimeDelay)
        fpUpdateTimeElapsed -= fpUpdateTimeDelay;
    // reuse ID`s if time has come
    fpIDReuseElapsed += fpSeconds;
    if (fpIDReuseElapsed > CS::pWorld->nIDReuseInterval)
    {
        fpIDReuseElapsed = 0;
        idReuse.front().ReclaimIDs(idPools);
        idReuse.pop_front();
        idReuse.emplace_back();
    }
    return fpTimeSinceLastRecv < CS::pWorld->nLinkTimeout &&
           channelOutBuf.GetSize() < MAX_CHANNEL_BUFFER_LENGTH;
}
/////////////////////////////////////////////////////////////////////////////////////
// COMMIT/ROLLBACK updates
/////////////////////////////////////////////////////////////////////////////////////
void CCSLink::RollbackUpdate(CUpdate &update)
{
    ASSERT(!update.bRolledBack);
    if (update.bRolledBack)
        return;
    // cout << "ROLLBACK update " << update.nPktNumber << endl;
    for (auto k = update.updates.begin(); k != update.updates.end(); ++k)
    {
        CUpdateRequest &upd = *k;
        switch (upd.type)
        {
        case UT_REMOVEBANDW:
            upd.pSubj->nUpdateTime = 0;
            break;
        case UT_BANDW:
            // if this update is in effect cancel it
            if (upd.pSubj->nUpdateTime == upd.nUpdateTime)
                upd.pSubj->nUpdateTime = 0; // update is required
            break;
        case UT_REMOVEDELAY:
        case UT_DELAY:
            break;
        default:
            ASSERT("Unknown update type" && 0);
            break;
        }
    }
    if (update.outBlock.nLength)
        channelOutList.push_back(update.outBlock);
    update.bRolledBack = true;
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSLink::CommitUpdate(CUpdate &update)
{
    // cout << "COMMIT update " << update.nPktNumber << endl;
    RegisterRTT(fpCurrentTime - update.fpTimeCreated);
    //
    for (auto k = update.updates.begin(); k != update.updates.end(); ++k)
    {
        CUpdateRequest &upd = *k;
        switch (upd.type)
        {
        case UT_REMOVEBANDW:
        case UT_REMOVEDELAY: {
            // this assert does not neccessarily holds true - if object get erased
            // from active state then get back to active this assertion can fail
            // but for upd.pSubj->pObject new CActiveObject should be allocated
            ASSERT(activeObjects.find(upd.pSubj->pObject) == activeObjects.end());
            CActiveSet &s = activeObjsSorted[upd.pSubj->nPriority];
            auto i = s.find(upd.pSubj);
            if (i != s.end())
            {
                FreeServerID(upd.pSubj);
                s.erase(i);
            }
        }
        break;
        case UT_BANDW:
            break;
        case UT_DELAY:
            upd.pSubj->nUpdateTime = Max(upd.pSubj->nUpdateTime, upd.nUpdateTime);
            break;
        default:
            ASSERT("Unknown update type" && 0);
            break;
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// FORM active set
/////////////////////////////////////////////////////////////////////////////////////
struct SCSShareInfoObj
{
    bool operator()(const CCSShareInfo &a, const CCSShareInfo &b) const { return a.pObj < b.pObj; }
};
/////////////////////////////////////////////////////////////////////////////////////
void CCSLink::AddActiveObject(CCSShareInfo &info)
{
    // only valid objects are accepted
    if (!info.pObj || !info.pObj->IsValid())
    {
        ASSERT(0);
        return;
    }
    int nTypeID = CS::pWorld->factory.GetObjectTypeID(info.pObj);
    ASSERT(nTypeID >= 0);
    ASSERT(info.nPriority >= 0 && info.nPriority <= CS_MAX_PRIORITY);
    // add info on object into structures
    CActiveObject *pAO = new CActiveObject;
    activeObjects[info.pObj] = pAO;
    activeObjsSorted[info.nPriority][pAO] = pAO;
    pAO->tID = nTypeID;
    pAO->pObject = info.pObj;
    pAO->nPriority = info.nPriority;
    pAO->bDelay = CS::pWorld->nDelay[nTypeID];
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSLink::RemoveActiveObject(CActiveObjectsMap::iterator &k)
{
    CActiveObject *pAO = k->second;
    if (pAO->nID == 0)
    {
        // has not been updated yet
        activeObjsSorted[pAO->nPriority].erase(pAO);
        activeObjects.erase(k);
    }
    else
    {
        // object has been updated at least once - mark it as needing update
        pAO->pObject = 0;
        pAO->nUpdateTime = 0; // force update
        activeObjects.erase(k);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSLink::RemoveActiveObject(CActiveSet::iterator &k)
{
    CActiveObject *pAO = k->second;
    ASSERT(pAO->nID != 0);
    activeObjects.erase(pAO->pObject);
    pAO->pObject = 0;
    pAO->nUpdateTime = 0; // force update
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSLink::FormNewActiveSet()
{
    static std::vector<CCSShareInfo> toUpdate;
    toUpdate.clear();
    SCSActiveSet updateSet(toUpdate);
    CS::pWorld->GetActiveObjects(updateSet);

    sort(toUpdate.begin(), toUpdate.end(), SCSShareInfoObj());
    auto pUpdate = toUpdate.begin(), pUpdateEnd = toUpdate.end();
    CCSLink::CActiveObjectsMap::iterator k = activeObjects.begin(), kend = activeObjects.end(), t;
    // update sets pass
    for (;;)
    {
        if (pUpdate == pUpdateEnd)
        {
            for (; k != kend;)
            {
                t = k++;
                RemoveActiveObject(t);
            }
            return;
        }
        if (k == kend)
        {
            for (; pUpdate < pUpdateEnd; pUpdate++)
                AddActiveObject(*pUpdate);
            return;
        }
        // determine what list is ahead and advance pointers
        if (pUpdate->pObj == (CDObject *)k->second->pObject)
        {
            // most frequent situation
            ++k;
            pUpdate++;
            continue;
        }
        if (pUpdate->pObj > (CDObject *)k->second->pObject)
        {
            t = k++;
            RemoveActiveObject(t);
            continue;
        }
        // this condition is complementary to previous so no need for if
        if (1) // if ( pUpdate->pObj < (CDObject*)k->second->pObject )
        {
            AddActiveObject(*pUpdate);
            pUpdate++;
            continue;
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// CHANNEL DATA
/////////////////////////////////////////////////////////////////////////////////////
// return true if test offset is before border
static bool IsBefore(CCSLink::CHANNEL_DATA_OFFSET border, CCSLink::CHANNEL_DATA_OFFSET test)
{
    return ((CCSLink::CHANNEL_DATA_OFFSET)(test - border)) > 0x8000;
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSLink::CreateOutChannelBlock(int nMaxSize)
{
    if (channelOutList.empty())
    {
        nMaxSize = Min(nMaxSize, 255);
        channelOutList.emplace_back();
        SChannelBlock &block = channelOutList.back();
        int nSize = Min(nMaxSize, channelOutBuf.GetSize());
        block.nOffset = nChannelOutputOffset;
        block.nLength = nSize;
        memcpy(block.cData, channelOutBuf.GetBuffer(), nSize);
        int nNewOutputSize = channelOutBuf.GetSize() - nSize;
        memmove(channelOutBuf.GetBufferForWrite(), channelOutBuf.GetBuffer() + nSize,
                nNewOutputSize);
        channelOutBuf.SetSize(nNewOutputSize);
        nChannelOutputOffset += nSize;
    }
    else
    {
        // find first block
        CCSLink::SChannelBlockList::iterator i = channelOutList.begin(), ibest = i, ibestNext;
        CHANNEL_DATA_OFFSET bestOffset = i->nOffset;
        for (; i != channelOutList.end(); ++i)
        {
            if (IsBefore(bestOffset, i->nOffset))
            {
                bestOffset = i->nOffset;
                ibest = i;
            }
        }
        if (channelOutList.begin() != ibest)
        {
            ibestNext = ibest;
            ++ibestNext;
            channelOutList.splice(channelOutList.begin(), channelOutList, ibest, ibestNext);
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSLink::GatherInChannelBlocks()
{
    for (;;)
    {
        bool bCont = false;
        for (auto i = channelInList.begin(); i != channelInList.end();)
        {
            if (i->nOffset == nChannelInputOffset)
            {
                channelInBuf.Seek(channelInBuf.GetSize());
                channelInBuf.Write(i->cData, i->nLength);
                nChannelInputOffset += i->nLength;
                i = channelInList.erase(i);
                bCont = true;
            }
            else
                ++i;
        }
        if (!bCont)
            break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSLink::NewChannelInput(const SChannelBlock &block)
{
    // is not this data already received?
    if (IsBefore(nChannelInputOffset, block.nOffset) || block.nLength == 0)
        return;
    channelInList.push_back(block);
    GatherInChannelBlocks();
}
/////////////////////////////////////////////////////////////////////////////////////
// PACKET ACKS
/////////////////////////////////////////////////////////////////////////////////////
// use recevedPkts
void CCSLink::SendPktAcks(CBitStream &bits)
{
    int i;
    DWORD dwBits = 0;
    PACKET_ID nLast = (PACKET_ID)nPktLastReceived;
    CS_UPDATETIME nTest = nPktLastReceived;
    for (i = 1; i <= 32; i++)
    {
        nTest--;
        vector<CS_UPDATETIME>::iterator k;
        k = find(receivedPkts.begin(), receivedPkts.end(), nTest);
        if (k != receivedPkts.end())
            dwBits |= 1 << (i - 1);
    }
    bits.Write(&nLast, sizeof(nLast));
    bits.Write(&dwBits, 4);
    // removing too old ack Pkts
    for (i = receivedPkts.size() - 1; i >= 0; i--)
    {
        if (receivedPkts[i] + 32 < nPktLastReceived)
            receivedPkts.erase(receivedPkts.begin() + i);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// receive single ack, search for this packet and ack it
void CCSLink::ReceiveAck(PACKET_ID nPkt)
{
    list<CUpdate>::iterator i;
    for (i = sentUpdates.begin(); i != sentUpdates.end();)
    {
        if (i->nPktNumber == nPkt)
        {
            CommitUpdate(*i);
            i = sentUpdates.erase(i);
            return;
        }
        else
            ++i;
    }
    // this should be very rare case so perfomance is not critical
    for (i = rolledUpdates.begin(); i != rolledUpdates.end();)
    {
        if (i->nPktNumber == nPkt)
        {
            CommitUpdate(*i);
            i = rolledUpdates.erase(i);
            return;
        }
        else
            ++i;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// pair to SendPktAcks()
void CCSLink::ReceivePktAcks(CBitStream &bits)
{
    DWORD dwBits, dwNewAckBits;
    PACKET_ID nLast;
    bits.Read(&nLast, sizeof(nLast));
    bits.Read(&dwBits, sizeof(dwBits));
    //
    int nShift = (nLast - nAckedLast) & (CS_PACKET_ID_RANGE - 1);
    if (nShift > CS_PACKET_ID_RANGE / 2)
        nShift -= CS_PACKET_ID_RANGE;
    if (nShift <= 0)
    {
        if (nShift < -32)
            dwAckedBits = 0;
        else
            dwAckedBits >>= -nShift;
    }
    else
    {
        dwAckedBits <<= 1;
        dwAckedBits |= 1;
        nShift--;
        if (nShift < 32)
            dwAckedBits <<= nShift;
        else
            dwAckedBits = 0;
    }
    dwNewAckBits = dwBits & ~dwAckedBits;
    dwAckedBits |= dwNewAckBits;
    nAckedLast = nLast;
    //
    ReceiveAck(nLast);
    for (int i = 1; i <= 32; i++)
    {
        nLast--;
        if (dwNewAckBits & (1 << (i - 1)))
            ReceiveAck(nLast);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// check if nPkt can be acked and is not too obsolete
// add it to received list and update pktlastreceived value if needed
bool CCSLink::CheckRecvPacketNumber(CS_UPDATETIME nPkt)
{
    // is not update too old?
    if (nPkt + 32 < nPktLastReceived)
    {
        if (pCSLog)
            (*pCSLog) << "SKIP too old update, dif=" << nPktLastReceived - nPkt << endl;
        return false;
    }
    nPktLastReceived = Max(nPktLastReceived, nPkt);
    // check for duplicate (for economy on rcvList mostly, because ack maybe already be sent
    // and duplication will not be detected)
    for (auto k = receivedPkts.begin(); k != receivedPkts.end(); ++k)
    {
        if (*k == nPkt)
        {
            if (pCSLog)
                (*pCSLog) << "DUPLICATE update received from " << peer.GetFastName() << endl;
            return false;
        }
    }
    // add to receivedPkts set
    receivedPkts.push_back(nPkt);
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
// ID`s send/receive
/////////////////////////////////////////////////////////////////////////////////////
static int FormObjectKey(CS_TYPEID tID, CS_OBJECTID oID)
{
    return (tID << 16) + (unsigned short)oID;
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSLink::SendRefID(CBitStream &bits, unsigned int bExternal, CS_TYPEID tID, CS_OBJECTID nID)
{
    bits.WriteBit(bExternal);
    bits.Write(&tID, sizeof(tID));
    bits.Write(&nID, sizeof(nID));
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSLink::RecvRefID(CBitStream &bits, unsigned int *pbExternal, CS_TYPEID *ptID,
                        CS_OBJECTID *pnID)
{
    pbExternal[0] = bits.ReadBit();
    bits.Read(ptID, sizeof(*ptID));
    bits.Read(pnID, sizeof(*pnID));
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSLink::SendID(CBitStream &bits, CS_TYPEID tID, CS_OBJECTID nID)
{
    bits.Write(&tID, sizeof(tID));
    bits.Write(&nID, sizeof(nID));
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSLink::RecvID(CBitStream &bits, CS_TYPEID *ptID, CS_OBJECTID *pnID)
{
    bits.Read(ptID, sizeof(*ptID));
    bits.Read(pnID, sizeof(*pnID));
}
/////////////////////////////////////////////////////////////////////////////////////
// ID alloc/free and storing in appropriate structures for server side objects
/////////////////////////////////////////////////////////////////////////////////////
void CCSLink::SetServerID(CActiveObject *pAO)
{
    pAO->nID = idPools[pAO->tID].GetID(); // CRAP - does not check possible failure
    int nID = FormObjectKey(pAO->tID, pAO->nID);
    ASSERT(activeObjectIDs.find(nID) == activeObjectIDs.end());
    activeObjectIDs[nID] = pAO;
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSLink::FreeServerID(CActiveObject *pAO)
{
    int nID = FormObjectKey(pAO->tID, pAO->nID);
    activeObjectIDs.erase(nID);
    idReuse.back().AddFreeID(pAO->tID, pAO->nID);
}
/////////////////////////////////////////////////////////////////////////////////////
// ID tracking from client side objects
/////////////////////////////////////////////////////////////////////////////////////
void CCSLink::SetClientID(CDObject *pObj, CS_TYPEID tID, CS_OBJECTID nID)
{
    ASSERT(createObjIDs.find(pObj) == createObjIDs.end());
    SClientObjID &co = createObjIDs[pObj];
    co.nID = nID;
    co.tID = tID;
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSLink::FreeClientID(CDObject *pObj)
{
    ASSERT(createObjIDs.find(pObj) != createObjIDs.end());
    createObjIDs.erase(pObj);
}
/////////////////////////////////////////////////////////////////////////////////////
CDObject *CCSLink::NewClientObj(CClientCreatedObj &cc, CS_TYPEID tID, CS_OBJECTID nID)
{
    CDObject *pObj = CS::pWorld->CreateObject(tID);
    if (!pObj)
    {
        ASSERT(0);
        return 0;
    }
    cc.pObj = pObj;
    cc.nUpdateTime = 0; // mark object as just created
    SetClientID(pObj, tID, nID);
    return pObj;
}
/////////////////////////////////////////////////////////////////////////////////////
// REFERENCES storing
/////////////////////////////////////////////////////////////////////////////////////
// CRAP - type should be specified as parameter and should not be stored
void CCSLink::WriteRef(CBitStream &bits, CDObject *pObj)
{
    if (!pObj || !pObj->IsValid())
    {
        SendRefID(bits, false, 0, 0);
        return;
    }
    auto i = activeObjects.find(pObj);
    if (i == activeObjects.end())
    {
        auto k = createObjIDs.find(pObj);
        if (k == createObjIDs.end())
        {
            ASSERT(0); // referencing of not participating object
            SendRefID(bits, false, 0, 0);
            return;
        }
        // client created object
        const SClientObjID &co = k->second;
        SendRefID(bits, true, co.tID, co.nID);
        return;
    }
    // normal server side object
    CActiveObject *pAO = i->second;
    if (pAO->nID == 0)
        SetServerID(pAO);
    SendRefID(bits, false, pAO->tID, pAO->nID);
}
/////////////////////////////////////////////////////////////////////////////////////
CDObject *CCSLink::ReadRef(CBitStream &bits)
{
    int nID;
    CS_TYPEID tID;
    CS_OBJECTID oID;
    unsigned int bExternal;

    RecvRefID(bits, &bExternal, &tID, &oID);
    nID = FormObjectKey(tID, oID);
    if (oID == 0)
        return 0;
    if (bExternal)
    {
        // this is reference to server side object
        auto pOP = activeObjectIDs.find(nID);
        if (pOP == activeObjectIDs.end())
        {
            // referencing on already removed object
            return 0;
        }
        return pOP->second->pObject;
    }
    else
    {
        // reference on another object from client side
        auto pOP = createdObjs.find(nID);
        // here we got to create referenced object (with default values)
        // trigger on its addition will be called later
        if (pOP == createdObjs.end())
            return NewClientObj(createdObjs[nID], tID, nID);
        if (!pOP->second.pObj->IsValid())
            return NewClientObj(pOP->second, tID, nID);
        return pOP->second.pObj;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// SEND world update
/////////////////////////////////////////////////////////////////////////////////////
bool CCSLink::CreateUpdate(int nMaxSize)
{
    bool bRv = true;
    ASSERT(nMaxSize > 10);
    CMemoryStream &pkt = updatePkt;
    {
        // determine objects to be updated
        FormNewActiveSet();

        //
        // update formation itself
        CBitLocker bits;
        pkt.Seek(0);
        bits.LockWrite(pkt, nMaxSize + N_MAX_UPDATE_SIZE);

        // write command
        ECSCmd cmd = CMD_NORMAL;
        bits.Write(&cmd, sizeof(cmd));

        // current packet number
        const unsigned char *pStart = bits.GetCurrentPtr();
        PACKET_ID nPktCurrent = nPktSent++;
        bits.Write(&nPktCurrent, sizeof(PACKET_ID));

        // acks on received packets
        SendPktAcks(bits);

        // send channel data
        CreateOutChannelBlock(nMaxSize - 16);
        const SChannelBlock &block = channelOutList.front();
        bits.Write(&block, block.GetSize());
        ASSERT(nMaxSize > bits.GetCurrentPtr() - pStart);

        //
        // send information about every changed object including deleted ones
        sentUpdates.emplace_back();
        CUpdate &update = sentUpdates.back();
        update.fpTimeCreated = fpCurrentTime;
        update.nPktNumber = nPktCurrent;
        update.outBlock = block;

        // free channel block
        channelOutList.pop_front();
        //
        unsigned char *pObjNum = bits.WriteDelayed(1);
        pObjNum[0] = 0;
        for (int nPriority = CS_MAX_PRIORITY; nPriority >= 0; nPriority--)
        {
            for (auto k = activeObjsSorted[nPriority].begin();
                 k != activeObjsSorted[nPriority].end(); ++k)
            {
                CActiveObject *pAO = k->second;
                CPtr<CDObject> &pObj = pAO->pObject;
                bool bModify = pObj->IsValid();
                if (bModify)
                {
                    ASSERT(pAO->nUpdateTime <= pObj->nUpdateTime);
                    if (pAO->nUpdateTime == pObj->nUpdateTime)
                        continue;
                    // designate ID if has not already do so
                    if (pAO->nID == 0)
                        SetServerID(pAO);
                    //
                    bits.WriteBit(1);
                    SendID(bits, pAO->tID, pAO->nID);
                    pObj->UpdateTo(bits);
                }
                else
                {
                    // if this object has never been updated skip it
                    if (pAO->nID == 0)
                        continue;
                    // if this object just got spoiled remove it from lists
                    if (pAO->pObject)
                        RemoveActiveObject(k);
                    //
                    if (pAO->nUpdateTime == 0)
                    {
                        bits.WriteBit(0);
                        SendID(bits, pAO->tID, pAO->nID);
                    }
                    else
                        continue;
                }
                // check for available size (if it is exhausted do not add last object into udpate)
                if (nMaxSize < bits.GetCurrentPtr() - pStart)
                {
                    bRv = false;
                    break;
                }
                pObjNum[0]++; // count updated objects
                // fill update record (using bandwidth setting for every object right now)
                update.updates.emplace_back();
                CUpdateRequest &req = update.updates.back();
                req.pSubj = pAO;
                // choose bandwidth saving/delay optimized and fill update record appropriately
                if (pAO->bDelay)
                {
                    if (bModify)
                    {
                        req.nUpdateTime = pObj->nUpdateTime;
                        req.type = UT_DELAY;
                    }
                    else
                        req.type = UT_REMOVEDELAY;
                }
                else
                {
                    // mark as already updated to prevent additional updates until this one iis
                    // commited/rollbacked
                    if (bModify)
                    {
                        pAO->nUpdateTime = pObj->nUpdateTime;
                        req.nUpdateTime = pObj->nUpdateTime;
                        req.type = UT_BANDW;
                    }
                    else
                    {
                        pAO->nUpdateTime = 1;
                        req.type = UT_REMOVEBANDW;
                    }
                }
            }
        }
    }
    if (!CS::pWorld->SendUpdate(peer, pkt))
    {
        RollbackUpdate(sentUpdates.back());
        sentUpdates.pop_back();
        bRv = false;
    }
    return bRv;
}
/////////////////////////////////////////////////////////////////////////////////////
// RECEIVE world update
/////////////////////////////////////////////////////////////////////////////////////
bool CCSLink::ReceiveObject(CBitStream &bits, CS_UPDATETIME nUpdateTime)
{
    int nID;
    unsigned int bUpdate = bits.ReadBit();
    CS_TYPEID tID = 0;
    CS_OBJECTID oID = 0;
    RecvID(bits, &tID, &oID);
    nID = FormObjectKey(tID, oID);
    auto pOP = createdObjs.find(nID);

    if (bUpdate)
    {
        // update object
        if (pOP == createdObjs.end())
        {
            // new object is created
            CClientCreatedObj &cc = createdObjs[nID];
            CDObject *pObj = NewClientObj(cc, tID, nID);
            if (!pObj)
                return false;
            pObj->UpdateFrom(bits);
            cc.nUpdateTime = nUpdateTime;
            CS::pWorld->TriggerOnAdd(pObj, tID);
        }
        else
        {
            CObj<CDObject> &pObj = pOP->second.pObj;
            if (pOP->second.nUpdateTime < nUpdateTime)
            {
                if (!pObj.IsValid())
                {
                    pObj = NewClientObj(pOP->second, tID, nID);
                    if (!pObj)
                        return false;
                }
                // update existing object
                pObj->UpdateFrom(bits);
                if (pOP->second.nUpdateTime == 0)
                    CS::pWorld->TriggerOnAdd(pObj, tID);
                else
                    CS::pWorld->TriggerOnUpdate(pObj, tID);
                pOP->second.nUpdateTime = nUpdateTime;
            }
            else
            {
                if (pCSLog)
                    (*pCSLog) << "SKIP update, newer version of object is already here" << endl;
                // got to skip this update
                CObj<CDObject> pObj = CS::pWorld->CreateObject(tID);
                pObj->UpdateFrom(bits);
            }
        }
    }
    else
    {
        // remove object
        if (pOP == createdObjs.end())
        {
            // new empty slot for ID is created
            CClientCreatedObj &cc = createdObjs[nID];
            cc.nUpdateTime = nUpdateTime;
        }
        else
        {
            if (pOP->second.nUpdateTime < nUpdateTime)
            {
                // erase object
                CObj<CDObject> &pObj = pOP->second.pObj;
                if (pObj.IsValid())
                {
                    FreeClientID(pObj);
                    CS::pWorld->TriggerOnRemove(pObj, tID);
                }
                pObj = 0;
                pOP->second.nUpdateTime = nUpdateTime;
            }
            else
            {
                if (pCSLog)
                    (*pCSLog) << "SKIP remove, newer version of object is already here" << endl;
            }
        }
    }
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CCSLink::ProcessUpdate(CBitStream &bits)
{
    CFWContext ctx(CS::pLink, this);
    PACKET_ID nReceived;
    CS_UPDATETIME nCurUpdate;
    unsigned char nObjNum, nCounter;

    fpTimeSinceLastRecv = 0;
    //
    bits.Read(&nReceived, sizeof(PACKET_ID));
    // calcing sequential number of arrived update (accounting for wrapping of pkt numbers)
    nCurUpdate = nPktHighCounter + nReceived;
    if (nPktLastReceived == 0)
        nPktLastReceived = nCurUpdate;
    int nDif = nCurUpdate - nPktLastReceived;
    if (nDif > CS_PACKET_ID_RANGE / 2)
        nCurUpdate -= CS_PACKET_ID_RANGE;
    else if (nDif < -CS_PACKET_ID_RANGE / 2)
    {
        nCurUpdate += CS_PACKET_ID_RANGE;
        nPktHighCounter += CS_PACKET_ID_RANGE;
    }
    // check is not packet too obsolete
    if (!CheckRecvPacketNumber(nCurUpdate))
        return true;

    // receiving acks
    ReceivePktAcks(bits);

    // receive channel data
    SChannelBlock block;
    bits.Read(&block, block.GetHeaderSize());
    bits.Read(block.cData, block.nLength);
    NewChannelInput(block);

    // receive new objects info
    bits.Read(&nObjNum, 1);
    for (nCounter = 0; nCounter < nObjNum; nCounter++)
    {
        if (!ReceiveObject(bits, nCurUpdate))
            return false;
    }
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSLink::FreeCreatedObjects()
{
    CFWContext ctx(CS::pLink, this);
    for (auto i = createdObjs.begin(); i != createdObjs.end(); ++i)
    {
        CObj<CDObject> &pObj = i->second.pObj;
        if (pObj.IsValid())
            CS::pWorld->TriggerOnRemove(pObj, (CS_TYPEID)CS::pWorld->factory.GetObjectTypeID(pObj));
        pObj = 0;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// CCSBase
/////////////////////////////////////////////////////////////////////////////////////
CCSBase::CCSBase()
{
    fpTime = 0;
    nUpdateCounter = 1;
    applicationID = 0;
    SetOpt(CS_LINK_TIMEOUT, 60);
    SetOpt(CS_MAX_RTT, 5);
}
/////////////////////////////////////////////////////////////////////////////////////
CCSBase::~CCSBase()
{
    for (auto i = clients.begin(); i != clients.end(); ++i)
        CmdLogout((*i)->GetPeerAddress(), (*i)->GetLinkID());
}
/////////////////////////////////////////////////////////////////////////////////////
CCSBase::CClientList::iterator CCSBase::GetClient(const CCSNodeAddress &addr)
{
    return find_if(clients.begin(), clients.end(), CClientAddrFunc(addr));
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSBase::RemoveClient(CClientList::iterator i)
{
    if (i != clients.end())
    {
        if (pCSLog)
            (*pCSLog) << "REMOVE client " << (*i)->GetPeerAddress().GetFastName() << endl;
        (*i)->FreeCreatedObjects();
        clients.erase(i);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
CCSLink &CCSBase::AddClient(const CCSNodeAddress &addr, CS_LINKID linkID, int nRate)
{
    if (pCSLog)
        (*pCSLog) << "ADD client " << addr.GetFastName() << endl;
    RemoveClient(GetClient(addr));
    clients.push_back(MakeObj(new CCSLink(linkID)));
    CCSLink *pLink = clients.back();
    pLink->SetAddress(addr);
    pLink->SetRate(nRate);
    return *pLink;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CCSBase::SendUpdate(const CCSNodeAddress &addr, CMemoryStream &pkt)
{
    return links.Send(addr, pkt);
}
/////////////////////////////////////////////////////////////////////////////////////
int CCSBase::GetRate(const CCSNodeAddress &addr)
{
    if (links.IsLocalAddr(addr))
        return nCSLocalRate;
    return nCSRate;
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSBase::CmdLogout(const CCSNodeAddress &addr, CS_LINKID linkID)
{
    CMemoryStream pkt;
    pkt << CMD_LOGOUT;
    pkt << linkID;
    SendUpdate(addr, pkt);
}
/////////////////////////////////////////////////////////////////////////////////////
bool CCSBase::ProcessNormal(const CCSNodeAddress &addr, CBitStream &bits)
{
    auto i = GetClient(addr);
    if (i == clients.end())
    {
        if (pCSLog)
            (*pCSLog) << "ERROR: Update from unknown host" << endl;
        // CmdLogout( addr );
        return false;
    }
    else
    {
        if (!(*i)->ProcessUpdate(bits))
        {
            if (pCSLog)
                (*pCSLog) << "ERROR: client " << (*i)->GetPeerAddress().GetFastName()
                          << " sent incorrect update" << endl;
            OnClientDrop(*i);
            RemoveClient(i);
        }
    }
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSBase::ProcessGetInfo(const CCSNodeAddress &addr, CBitStream &bits)
{
    if (pCSLog)
        (*pCSLog) << "ERROR: Can`t handle get info" << endl;
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSBase::ProcessLogin(const CCSNodeAddress &addr, CBitStream &bits)
{
    if (pCSLog)
        (*pCSLog) << "ERROR: Can`t handle login" << endl;
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSBase::ProcessLogout(const CCSNodeAddress &addr, CBitStream &bits)
{
    CS_LINKID linkID = 0;
    bits.Read(linkID);
    auto i = GetClient(addr);
    if (i == clients.end() || (*i)->GetLinkID() != linkID)
        return;
    OnClientExit(*i);
    RemoveClient(i);
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSBase::ProcessInfo(const CCSNodeAddress &addr, CBitStream &bits)
{
    if (pCSLog)
        (*pCSLog) << "ERROR: Can`t handle info" << endl;
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSBase::ProcessAccepted(const CCSNodeAddress &addr, CBitStream &bits)
{
    if (pCSLog)
        (*pCSLog) << "ERROR: Can`t handle accepted" << endl;
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSBase::ProcessRejected(const CCSNodeAddress &addr, CBitStream &bits)
{
    if (pCSLog)
        (*pCSLog) << "ERROR: Can`t handle rejected" << endl;
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSBase::ProcessMaster(const CCSNodeAddress &addr, CBitStream &bits)
{
    if (pCSLog)
        (*pCSLog) << "ERROR: Can`t handle master packet" << endl;
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSBase::TimePassed(double fpSeconds)
{
    CFWContext ctx(CS::pWorld, this);
    //
    fpTime += fpSeconds;
    // process incoming packets
    CCSNodeAddress addr;
    CMemoryStream &pkt = recvPkt;
    while (links.Recv(addr, pkt))
    {
        if (pkt.GetSize() == 0)
        {
            if (pCSLog)
                (*pCSLog) << "ZERO length packet received from " << addr.GetFastName() << endl;
            continue;
        }
        ECSCmd cmd;
        pkt.Read(&cmd, sizeof(cmd));
        CBitStream bits(pkt.GetBufferForWrite() + 1, CBitStream::read, pkt.GetSize() - 1);
        switch (cmd)
        {
        case CMD_NORMAL:
            ProcessNormal(addr, bits);
            break;
        case CMD_LOGOUT:
            ProcessLogout(addr, bits);
            break;
        case CMDS_GET_INFO:
            ProcessGetInfo(addr, bits);
            break;
        case CMDS_LOGIN:
            ProcessLogin(addr, bits);
            break;
        case CMDC_INFO:
            ProcessInfo(addr, bits);
            break;
        case CMDC_ACCEPTED:
            ProcessAccepted(addr, bits);
            break;
        case CMDC_REJECTED:
            ProcessRejected(addr, bits);
            break;
        case CMD_MASTER:
            ProcessMaster(addr, bits);
            break;
        default:
            ASSERT(0 && "Unknown command");
            break;
        }
    }
    //
    for (auto i = clients.begin(); i != clients.end();)
    {
        if (!(*i)->TimePassed(fpSeconds))
        {
            auto t = i++;
            if (pCSLog)
                (*pCSLog) << "TIMEOUT client, drop " << (*t)->GetPeerAddress().GetFastName()
                          << endl;
            OnClientDrop(*t);
            RemoveClient(t);
        }
        else
            ++i;
    }
    nUpdateCounter++;
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSBase::GetActiveObjects(SCSActiveSet &toUpdate)
{
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSBase::TriggerOnAdd(CDObject *pObj, CS_TYPEID tID)
{
    for (auto i = addTriggers[tID].begin(); i != addTriggers[tID].end(); ++i)
        (*i)(this, pObj);
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSBase::TriggerOnUpdate(CDObject *pObj, CS_TYPEID tID)
{
    for (auto i = updateTriggers[tID].begin(); i != updateTriggers[tID].end(); ++i)
        (*i)(this, pObj);
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSBase::TriggerOnRemove(CDObject *pObj, CS_TYPEID tID)
{
    for (auto i = removeTriggers[tID].begin(); i != removeTriggers[tID].end(); ++i)
        (*i)(this, pObj);
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSBase::RegisterTrigger(ETriggerType tt, CS_TYPEID tID, TriggerFunc func)
{
    switch (tt)
    {
    case TT_ADD:
        addTriggers[tID].push_back(func);
        break;
    case TT_UPD:
        updateTriggers[tID].push_back(func);
        break;
    case TT_DEL:
        removeTriggers[tID].push_back(func);
        break;
    default:
        ASSERT(0);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CCSBase::SetOpt(ECSBaseOption option, int nVal)
{
    switch (option)
    {
    case CS_LINK_TIMEOUT:
        nLinkTimeout = nVal;
        break;
    case CS_MAX_RTT:
        nMaxRTT = nVal;
        break;
    }
    nIDReuseInterval = nLinkTimeout + nMaxRTT + 10;
}
/////////////////////////////////////////////////////////////////////////////////////
int CCSBase::GetOpt(ECSBaseOption option)
{
    switch (option)
    {
    case CS_LINK_TIMEOUT:
        return nLinkTimeout;
    case CS_MAX_RTT:
        return nMaxRTT;
    }
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
