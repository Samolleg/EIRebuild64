#pragma once

#include "Database.h"
#include "qdbFormat.h"
/////////////////////////////////////////////////////////////////////////////////////
struct SQuestDatabase
{
public:
    CDatabase<SQDBQuest> quests;
    CDatabase<SQDBBriefing> briefings;

public:
    BOOL Load(LPCSTR pszPath);
    //
    bool AddInfo(BYTE *pData);
    //
};
/////////////////////////////////////////////////////////////////////////////////////
extern SQuestDatabase theQuestDatabase;