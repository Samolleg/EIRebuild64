#pragma once
#include "SaveSchema.h"

/**********************************************************************
 *<
        FILE: FofSystem.H

        DESCRIPTION:  Friend or Foe management

        CREATED BY: Andy Gulin

 *>	Copyright (c) 1999, Nival Enterntainemnt
 **********************************************************************/

#include "afxtempl.h"
#include "unit.h"
#include "SaveLoader.h"

// максимальное количество групп, относительно которых учитывается отношения дружественности
// ограничено 32
const int N_MAX_PLAYERS = 32;

// структура для контроля количества нанесенного повреждения
/*struct SFofDamageCount
{
        short nCurDamage, nMaxDamage; // если MaxDamage<0 то эти ребята не могут поссорится
        SFofDamageCount() { nCurDamage = 0; nMaxDamage = -1; }
};*/

enum EDiplomacy
{
    Ally,
    Neutral,
    War
};

class CFofTracker
{
public:
    void Load(SaveSchema::Reader &);
    void Store(SaveSchema::Writer &) const;
private:
    // терпимость к случайным нападениям
    // SFofDamageCount damages[32][32]; // [y][x], вред, полученный плеером y от плеера x,
    // характеризует

    DWORD allyMatrix[32]; // на каждого плеера массив из битиков, если битик == 1, то эти
                          // плеера шарят врагов - альянс ( на диагонали должны быть единички )

    DWORD enemyMatrix[32]; // на каждого плеера массив из битиков, если битик == 1, то эти
                           // плеера изначально находятся в состоянии войны

    // int CanSee( SFofUnit &a, SFofUnit &b ) const ; // 0 or 1
public:
    CFofTracker() { Clear(); }
    ~CFofTracker() { Clear(); }
    void Clear();
    // находится ли группа nPlayer1 в состоянии войны с группой nPlayer2
    BOOL IsPlayerWar(int nPlayer1, int nPlayer2) { return enemyMatrix[nPlayer1] & (1 << nPlayer2); }
    BOOL IsPlayerAlly(int nPlayer1, int nPlayer2) { return allyMatrix[nPlayer1] & (1 << nPlayer2); }
    DWORD GetEnemyMask(int nPlayer);

    // вызывается каждый сегмент для апдейтов состояния дружбы/вражды
    void Segment() {}
    // для объявления войны априори, например все орки ненавидят гоблинов, или player1 player`a2
    // после вызова этой функи должны(?) апдейтятся все отношения отдельных юнитов
    void SetDiplomacy(int nPlayer1, int nPlayer2, EDiplomacy eNewDiplomacy,
                      BOOL bSetUnitsDiplomacy = TRUE);
    // получить изначальную дипломатию для двух плееров
    EDiplomacy GetDiplomacy(int nPlayer1, int nPlayer2);
    // юнит pWho вероломно нанес nDamage юниту pWhom, не пора ли дать сдачи ?
    void MakeDamage(CUnit *pWho, CUnit *pWhom, int nDamage);
    void Load(CSave &file);
    void Store(CSave &file);
};
