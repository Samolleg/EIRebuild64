#pragma once

#include "Plot.h"
#include "sArray.h"
#include "SaveLoader.h"

// --------------------------------------------------------------------------------------------
const int N_NUMBER_OF_ALARM = 5;

// --------------------------------------------------------------------------------------------
// информация об аларме - место поднятия, время поднятия и признак активированности
// --------------------------------------------------------------------------------------------
struct SAlarmInfo
{
    bool bActivated;
    int nTime;
    float fpX, fpY;
    void Clear()
    {
        bActivated = false;
        nTime = 0;
        fpX = fpY = 0;
    }
    void Load(CSave &file) { file.Read(this, sizeof(*this)); }
    void Store(CSave &file) { file.Write(this, sizeof(*this)); }
};
/////////////////////////////////////////////////////////////////////////////////////
// General Unit Logic Type
enum EUnitModel
{
    UM_IDLE = 0,
    UM_GUARD,
    UM_PATROL,
    UM_SENTRY,
    UM_PLAYER,
    UM_GUARD_ALARM
};
enum EAlarmCondition
{
    AC_SEE_ENEMY = 0,
    AC_IS_ATTACKED,
    AC_SUSPECTION
};
/////////////////////////////////////////////////////////////////////////////////////
enum EUnitAggressionMode
{
    UAM_AGGRESSIVE =
        0, // Злой ли unit ( по умолчанию злой ), т.е. валит всех кого увидет с плохой дипломатией
    UAM_REVENGE = 1,    // Будет ли юнит мстить
    UAM_FEAR = 2,       // Боится ли юнит
    UAM_FEARPLAYER = 3, // Боится ли юнит (обычная боязнь + боязнь всех плееров)
    UAM_LAST
};
/////////////////////////////////////////////////////////////////////////////////////
const int GUM_ALWAYS_ACTIVE = 8; // Всегда активен = сегменты зовутся всегда
/////////////////////////////////////////////////////////////////////////////////////
const short GUAP_REST = 1; // при выполнении этого ActionPoint юнит должен находится в rest
/////////////////////////////////////////////////////////////////////////////////////
struct SActionPoint
{
public:
    CPlot3D ptLook;    // точка в которую unit сейчас будет смотреть
    float fpTurnSpeed; // скорость разворота в рад/сегмент, если 0, то разворот максимально быстрый
    short
        nWaitSeg; // сколько сегментов unit будет пялиться в эту точку (converted from int to short)
    short nFlags; // different action point flags (GUAP_*)

public:
    SActionPoint()
    {
        fpTurnSpeed = 0;
        nWaitSeg = 0;
        nFlags = 0;
    }
    void Load(CSave &file) { file.Read(this, sizeof(*this)); }
    void Store(CSave &file) { file.Write(this, sizeof(*this)); }
};
typedef CAArray<SActionPoint, SActionPoint &> CActionArray;
/////////////////////////////////////////////////////////////////////////////////////
struct SPatrolPoint
{
public:
    CPlot3D ptPosition;   // координаты очередной точки патруля
    CActionArray actions; // последовательность действий unit-a, если отсутствует, то действия
                          // определяются CUMPatrol
public:
    int AddActionPoint(CPlot3D point);
    void Load(CSave &file);
    void Store(CSave &file);
};
typedef CAArray<SPatrolPoint, SPatrolPoint &> CPatrolPointArray;
/////////////////////////////////////////////////////////////////////////////////////
struct SGeneralUnitLogic
{
public:
    EUnitModel eLogicModel;
    CPatrolPointArray patrolPoints;
    CPlot3D ptGuardPoint;      // что охраняем
    BOOL bCyclic;              // зациклены ли точки патрулирования
    DWORD dwLogicFlags;        // флаги для различных мотиваций поведения
    float fpGuardRadius;       // радиус в метрах для GUARD
    float fpGuardStayIdleTime; // время в секундах в течение которого юнит стоит и ничего не делает
    int nAlarmNumber;          //  номер аларма, поднимаемого юнитом
    BOOL bEnabled;             // используется ли данная логика
    float fCallForHelpRadius;  // радиус в котором юнит зовёт на помощь
    EAlarmCondition eAlarmCondition;
    int nAlarmDelay; // задержка начала аларма от события, вызвавшего его в сегментах
    EUnitAggressionMode aggressionMode; // агрессивность

public:
    inline int AddPatrolPoint(CPlot3D point);
    SGeneralUnitLogic();
    // function to operate with motivations defined as GUM_xxx
    BOOL HasMotivation(DWORD dwMotiv) { return (dwLogicFlags & dwMotiv) != 0; }
    void AddMotivation(DWORD dwMotiv) { dwLogicFlags |= dwMotiv; }
    void SetMotivation(DWORD dwMotiv, BOOL bSet)
    {
        if (bSet)
            dwLogicFlags |= dwMotiv;
        else
            dwLogicFlags &= ~dwMotiv;
    }
    void SetAggressionMode(EUnitAggressionMode mode) { aggressionMode = mode; }
    void ClearMotivation() { dwLogicFlags = 0; }
    void Load(CSave &file);
    void Store(CSave &file);
};
/////////////////////////////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------------------------------
inline int SGeneralUnitLogic::AddPatrolPoint(CPlot3D point)
{
    patrolPoints.SetSize(patrolPoints.GetSize() + 1);
    patrolPoints[patrolPoints.GetUpperBound()].ptPosition = point;
    return patrolPoints.GetUpperBound();
}
/////////////////////////////////////////////////////////////////////////////////////
inline int SPatrolPoint::AddActionPoint(CPlot3D point)
{
    actions.SetSize(actions.GetSize() + 1);
    actions[actions.GetUpperBound()].ptLook = point;
    return actions.GetUpperBound();
}
