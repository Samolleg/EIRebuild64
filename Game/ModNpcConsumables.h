#pragma once
#include <string>
#include "WorldLoadOrigin.h"
class CUnit;struct SRPGCommonStats;
struct ModConsumableConfig {std::string spellFormula;int charges=0;bool automatic=false;bool prototypeFallback=false;};
bool JMV_RegisterNpcCombatConsumable(CUnit*,const char*,const char*,int);
void JMV_NpcConsumablesUpdate(SRPGCommonStats*,CUnit*);
void JMV_NpcConsumablesLoaded(WorldLoadOrigin origin);
bool JMV_CheckNpcVisitLifecycle();
void JMV_NpcConsumablesRestart();
void JMV_NpcConsumablesReset();
bool JMV_CheckNpcConsumablePacking();
