#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
/*==========================================================================================================

        Player manager class implementation

        Written by Ivanov Evgeny
        Nival Interactive, 1998

==========================================================================================================*/

#include "World.h"
#include "Item.h"
#include "PlayerManager.h"
#include "Player.h"
#include "PlayerServer.h"

// ---------------------------------- Create/destroy
// -------------------------------------------------------

BOOL CPlayerManager::Create()
{
    if (!CBaseManager::Create())
        return FALSE;
    return TRUE;
}

void CPlayerManager::Destroy()
{
    CBaseManager::Destroy();
}

// --------------------------------- Create/remove player
// --------------------------------------------------

LPGAMEBASE CPlayerManager::CreateInstant(ETypeIDs tidTypeID)
{
    CPlayer *pPlayer = dbgnew CPlayer();

    if (pPlayer == NULL)
        return NULL;
    pPlayer->SetTypeID(tidTypeID);

    return pPlayer;
}

CPlayer *CPlayerManager::CreatePlayer()
{
    CPlayer *pPlayer = (CPlayer *)CBaseManager::CreateData(STID_PLAYER);
    if (!pPlayer)
        return NULL;
    LPGAMEBASE pSpecific =
        ((CWorld *)GetWorld())->GetSpecificManager()->CreateData(SPTID_PLAYER_SPECIFIC);
    pPlayer->SetPlayerSpecific(pSpecific);
    pPlayer->AddRef();
    pPlayer->SetPlayerNumber(0);

    return pPlayer;
}

void CPlayerManager::RemovePlayer(CPlayer *pPlayer)
{
    // pPlayer->SetPlayerNumber( -1 );
    pPlayer->Release();
}

// ------------------------- Change player ID
// --------------------------------------------------------------
void CPlayerManager::ChangeID(CPlayer *pPlayer, int nNewID)
{
    assert(hash.Seek(pPlayer->GetID()));
    hash.RemoveAt(pPlayer->GetID());

    pPlayer->SetID(nNewID);

    assert(hash.Seek(nNewID) == NULL);
    hash.SetAt(nNewID, pPlayer);
}

CPlayer *CPlayerManager::FindByNumber(int nPlayerNumber)
{
    CManagerIterator iter;
    CPlayer *pPlayer = (CPlayer *)iter.GetFirst(this);
    for (; pPlayer; pPlayer = (CPlayer *)iter.GetNext())
    {
        if (pPlayer->GetPlayerNumber() == nPlayerNumber)
            return pPlayer;
    }
    return NULL;
}

void CPlayerManager::SendEvent(int nPlayerNumber, DWORD dwEventID)
{
    CManagerIterator iter;
    CPlayer *pPlayer = (CPlayer *)iter.GetFirst(this);
    ASSERT(((CWorld *)GetWorld())->IsServer());
    for (; pPlayer; pPlayer = (CPlayer *)iter.GetNext())
        ((CPlayerServer *)pPlayer->GetPlayerSpecific())->SendEvent((char)nPlayerNumber, dwEventID);
}

void CPlayerManager::SendStringEvent(int nPlayerNumber, const char *pszEvent)
{
    CManagerIterator iter;
    CPlayer *pPlayer = (CPlayer *)iter.GetFirst(this);
    ASSERT(((CWorld *)GetWorld())->IsServer());
    for (; pPlayer; pPlayer = (CPlayer *)iter.GetNext())
        ((CPlayerServer *)pPlayer->GetPlayerSpecific())
            ->SendStringEvent((char)nPlayerNumber, pszEvent);
}

/*
void CPlayerManager::SetPlayer( CPlayer *pPlayer, int nPlayerNum )
{
        if( nPlayerNum < 0 || nPlayerNum >= N_MAX_PLAYERS )
                return;
        if( pPlayer && players[ nPlayerNum ] )
                ReportError( "CPlayerManager::SetPlayer, player %i already registered", nPlayerNum
); players[ nPlayerNum ] = pPlayer;
}*/
