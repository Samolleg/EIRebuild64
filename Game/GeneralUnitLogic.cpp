/**********************************************************************
 *<
        FILE: generalUnitLogic.cpp

        DESCRIPTION:  GeneralUnitLogic structures

 *>	Copyright (c) 1999, Nival Interactive
 **********************************************************************/

#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "generalUnitLogic.h"
#include "aiSensesConst.h"
/////////////////////////////////////////////////////////////////////////////////////
void SPatrolPoint::Load(CSave &file)
{
    file.Read(&ptPosition, sizeof(ptPosition));
    LoadCArray<SActionPoint, SActionPoint &>(actions, file);
}
/////////////////////////////////////////////////////////////////////////////////////
void SPatrolPoint::Store(CSave &file)
{
    file.Write(&ptPosition, sizeof(ptPosition));
    StoreCArray<SActionPoint, SActionPoint &>(actions, file);
}
/////////////////////////////////////////////////////////////////////////////////////
// SGeneralUnitLogic
/////////////////////////////////////////////////////////////////////////////////////
SGeneralUnitLogic::SGeneralUnitLogic()
{
    eLogicModel = UM_SENTRY;
    bCyclic = TRUE;
    dwLogicFlags = 0; // GUM_AGGRESSIVE | GUM_REVENGE;// | GUM_FEAR;
    fpGuardRadius = 10.0f;
    fpGuardStayIdleTime = -1;
    nAlarmNumber = 0;
    bEnabled = FALSE;
    fCallForHelpRadius = AI_DEFAULT_CALL_FOR_HELP_RADIUS;
    eAlarmCondition = AC_SEE_ENEMY;
    nAlarmDelay = 5 * 15; // 5 sec
    aggressionMode = UAM_AGGRESSIVE;
}
/////////////////////////////////////////////////////////////////////////////////////
void SGeneralUnitLogic::Load(CSave &file)
{
    file.Read(&eLogicModel, sizeof(eLogicModel));
    LoadCArray<SPatrolPoint, SPatrolPoint &>(patrolPoints, file);
    file.Read(&ptGuardPoint, sizeof(ptGuardPoint));
    file.Read(bCyclic);
    file.Read(dwLogicFlags);
    file.Read(fpGuardRadius);
    file.Read(nAlarmNumber);
    file.Read(bEnabled);
    file.Read(fCallForHelpRadius);
    file.Read(&eAlarmCondition, sizeof(eAlarmCondition));
    file.Read(nAlarmDelay);
    if (file.GetVersion() < 83)
    {
        aggressionMode = UAM_FEAR;
        if (HasMotivation(1))
            aggressionMode = UAM_AGGRESSIVE;
        else if (HasMotivation(2))
            aggressionMode = UAM_REVENGE;
        else if (HasMotivation(4))
            aggressionMode = UAM_FEAR;
        else if (HasMotivation(16))
            aggressionMode = UAM_FEARPLAYER;
        return;
    }
    file.Read(&aggressionMode, sizeof(aggressionMode));
}
/////////////////////////////////////////////////////////////////////////////////////
void SGeneralUnitLogic::Store(CSave &file)
{
    file.Write(&eLogicModel, sizeof(eLogicModel));
    StoreCArray<SPatrolPoint, SPatrolPoint &>(patrolPoints, file);
    file.Write(&ptGuardPoint, sizeof(ptGuardPoint));
    file.Write(bCyclic);
    file.Write(dwLogicFlags);
    file.Write(fpGuardRadius);
    file.Write(nAlarmNumber);
    file.Write(bEnabled);
    file.Write(fCallForHelpRadius);
    file.Write(&eAlarmCondition, sizeof(eAlarmCondition));
    file.Write(nAlarmDelay);
    file.Write(&aggressionMode, sizeof(aggressionMode));
}
/////////////////////////////////////////////////////////////////////////////////////
