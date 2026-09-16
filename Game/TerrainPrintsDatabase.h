#pragma once
#include "Database.h"
#include "tpFormat.h"
/////////////////////////////////////////////////////////////////////////////////////
struct STerrainPrintsDatabase
{
    CDatabase<STPDesc> blood;
    CDatabase<SFPDesc> foot;
    CDatabase<STPDesc> fire;
    //
    BOOL Load(LPCSTR pszPath);
};
extern STerrainPrintsDatabase theTPDatabase;
