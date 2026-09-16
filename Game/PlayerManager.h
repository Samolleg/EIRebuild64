#pragma once
/*==========================================================================================================

        Player manager class header

        Written by Ivanov Evgeny
        Nival Interactive, 1998

==========================================================================================================*/

#include "BaseManager.h"

class CPlayer;
// --------------------------- CPlayerManager
// --------------------------------------------------------------

class CPlayerManager : public CBaseManager
{
private:
    virtual LPGAMEBASE CreateInstant(ETypeIDs tidTypeID);
    // CPlayer *players[ N_MAX_PLAYERS ];        // табличка для быстрого поиска плеера по его
    // номеру void SetPlayer( CPlayer *pPlayer, int nPlayerNum );  // записать плеера pPlayer в
    // табличку под номером nPlayerNum
public:
    CPlayerManager();
    virtual ~CPlayerManager() {}
    // create/destroy
    virtual BOOL Create();
    virtual void Destroy();
    // player creating/removing for net driver
    CPlayer *CreatePlayer();
    void RemovePlayer(CPlayer *pPlayer);
    // Change ID
    void ChangeID(CPlayer *pPlayer, int nNewID);
    // searching existed player
    CPlayer *FindByID(int nID) { return (CPlayer *)Find(nID); }
    CPlayer *FindByNumber(int nPlayerNumber);
    void SendEvent(int nPlayerNumber, DWORD dwEventID);
    void SendStringEvent(int nPlayerNumber, const char *pszEvent);
    // class identification information
    virtual LPCSTR GetGameClassName() const { return "CPlayerManager"; }
    virtual EClassIDs WhoAmI() const { return HPC_PLAYERMANAGER; }
};

inline CPlayerManager::CPlayerManager()
{
#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
}

class CPlayerIterator : public CManagerIterator
{
public:
    CPlayer *GetFirst(CPlayerManager *lpManager)
    {
        return (CPlayer *)CManagerIterator::GetFirst(lpManager);
    }
    CPlayer *GetNext() { return (CPlayer *)CManagerIterator::GetNext(); }
};
