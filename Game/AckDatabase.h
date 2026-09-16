#pragma once

#include "Database.h"
#include "ackFormat.h"

class CUnit;

// --------------------------------------------------------------------------------------------
struct SAckDatabase
{
public:
    CDatabase<SAckHeroAck> ackHeroAcks;
    CDatabase<SAckUnitGeneral> ackUnitAcks;
    CDatabase<SAckNPC> ackNPCAcks;

public:
    BOOL Load(LPCSTR pszPath);
};
extern SAckDatabase theAckDatabase;
// --------------------------------------------------------------------------------------------
const SAckEntry *PlayAck(CUnit *pUnit, DWORD dwType);
const SAckEntry *PlayScenarioAck(CUnit *pUnit, const char *pszKey, bool bBlockUnit = false);
