#pragma once

#include "NetStream.h"

class CCSLink;
class CCSBase;

// --------------------------------------------------------------------------------------------
// connection через chat server
// --------------------------------------------------------------------------------------------
class CChatConnection : public CStreamConnection
{
public:
    static CChatConnection *NewCChatConnection() { return new CChatConnection(); }

protected:
    virtual CObjectBase *Duplicate() const
    {
        CObjectBase *pRes = new CChatConnection(*this);
        return pRes;
    }
    virtual void DestroyContents()
    {
        CChatConnection::~CChatConnection();
        ::new (this) CChatConnection;
    }
    virtual ~CChatConnection() { Disconnect(); }

protected:
    virtual bool SendData(const char *pszData, int nLen);

public:
    CPtr<CCSLink> pLink;

    CChatConnection() {}
    virtual bool CanSend();
    virtual bool CanRecv() { return true; }
    virtual void Segment();
    virtual void Disconnect();
};

// --------------------------------------------------------------------------------------------
// управление связью с удаленным объектом
// --------------------------------------------------------------------------------------------
class CChatNetDriver : public CLowLevelNetDriver
{
    OBJECT_BASIC_METHODS(CChatNetDriver);

private:
    typedef std::list<CObj<CChatConnection>> CLinkList;
    typedef std::list<CPtr<CChatConnection>> CLinkPtrList;
    CLinkList links;
    CLinkPtrList newLinks;
    //
    CChatConnection *NewLink(CCSLink *pLink);
    CChatConnection *GetLink(CCSLink *pLink);

public:
    virtual bool StartServer();
    void Segment(CCSBase *pBase);
    CChatConnection *Connect(CCSLink *pLink);
    virtual void GetNewPlayers(CConnectionsList &newList);
};