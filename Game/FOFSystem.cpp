#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "FofSystem.h"
#include <stdlib.h>
#include "PlayerManager.h"
#include "WorldServer.h"
#include "MapObjectManager.h"
#include "Unit.h"
#include "Player.h"

void CFofTracker::Clear()
{
    memset(enemyMatrix, 0, sizeof(allyMatrix));
    for (int i = 0; i < 32; i++)
        allyMatrix[i] = 1 << i;
}

void CFofTracker::SetDiplomacy(int nPlayer1, int nPlayer2, EDiplomacy eNewDiplomacy,
                               BOOL bSetUnitsDiplomacy)
{
    CMapObjectManager *pManager;
    CMapObjectIterator iter;
    CMapObject *pMO;
    CUnit *pUnit;
    DWORD dwMask;
    switch (eNewDiplomacy)
    {
    case Ally:
        enemyMatrix[nPlayer1] &= ~(1 << nPlayer2);
        allyMatrix[nPlayer1] |= (1 << nPlayer2);
        break;
    case Neutral:
        enemyMatrix[nPlayer1] &= ~(1 << nPlayer2);
        allyMatrix[nPlayer1] &= ~(1 << nPlayer2);
        break;
    case War:
        enemyMatrix[nPlayer1] |= (1 << nPlayer2);
        allyMatrix[nPlayer1] &= ~(1 << nPlayer2);
        break;
    }
    if (bSetUnitsDiplomacy)
    {
        pManager = (CMapObjectManager *)theWorldServer.GetMapObjectManager();
        pMO = iter.GetFirst(pManager);
        while (pMO)
        {
            if (pMO->WhoAmI() == HPC_UNIT)
            {
                pUnit = (CUnit *)pMO;
                if (pUnit->GetPlayerNum() == nPlayer1)
                {
                    dwMask = pUnit->GetEnemyMask();
                    if (eNewDiplomacy == War)
                        dwMask |= (1 << nPlayer2);
                    else
                        dwMask &= ~(1 << nPlayer2);
                    pUnit->SetEnemyMask(dwMask);
                }
            }
            pMO = iter.GetNext();
        }
    }
}

EDiplomacy CFofTracker::GetDiplomacy(int nPlayer1, int nPlayer2)
{
    if (allyMatrix[nPlayer1] & (1 << nPlayer2))
        return Ally;
    if (enemyMatrix[nPlayer1] & (1 << nPlayer2))
        return War;
    return Neutral;
}

DWORD CFofTracker::GetEnemyMask(int nPlayer)
{
    if (nPlayer < 0 || nPlayer >= N_MAX_PLAYERS)
        return 0xFFFFFFFF;
    return enemyMatrix[nPlayer];
}

/*
void CFofTracker::DeclareWar( int nPlayer1, int nPlayer2 )
{
        if( IsPlayerWar( nPlayer1, nPlayer2 ) )
                return;
        PlayerWars.Add( SFofPlayerWar( nPlayer1, nPlayer2 ) );
// сбросим флажки дружбы nPlayer2 для всех юнитов из nPlayer1
        POSITION pos;
        pos = units.GetHeadPosition();
        while( pos )
        {
                SFofUnit &unit = units.GetNext( pos );
                if( unit.nPlayer == nPlayer1 )
                        unit.dwFriend &= ~( 1 << nPlayer2 );
        }
}
*/

void CFofTracker::MakeDamage(CUnit *pWho, CUnit *pWhom, int nDamage)
{
    // DWORD dwSaveFriend;
    if (!pWho || !pWhom)
        return;
    if (IsPlayerAlly(pWhom->GetPlayerNum(), pWho->GetPlayerNum()))
        return;
    pWhom->AddEnemy(pWho->GetPlayerNum());
    pWhom->SetFlag(UNIT_RECALCENEMY);
    if (CPlayer *pPlayer = pWhom->GetOwner())
    {
        CUnitPtrHashIterator unitIter;
        for (CUnit *pUnit = unitIter.GetFirst(&pPlayer->unitsUnderControl); pUnit;
             pUnit = unitIter.GetNext())
            pUnit->AddEnemy(pWho->GetPlayerNum());
    }
    /*	SFofDamageCount &damage = pFofWhom->damages[ pFofWho->nPlayer ];
            if( damage.nMaxDamage < 0 )
                    return;
            damage.nCurDamage += nDamage;
            if( damage.nCurDamage > damage.nMaxDamage )
            {
                    dwSaveFriend = pFofWhom->dwFriend;
                    pFofWhom->dwFriend &= ~( 1 << pFofWho->nPlayer );
                    if( pFofWhom->dwFriend != dwSaveFriend )
                            pFofWhom->nMoved = 1;
                    damage.nMaxDamage = -1;
            }*/
}

void CFofTracker::Load(CSave &file)
{
    file.Read(allyMatrix, sizeof(allyMatrix));
    file.Read(enemyMatrix, sizeof(enemyMatrix));
}

void CFofTracker::Store(CSave &file)
{
    file.Write(allyMatrix, sizeof(allyMatrix));
    file.Write(enemyMatrix, sizeof(enemyMatrix));
}
