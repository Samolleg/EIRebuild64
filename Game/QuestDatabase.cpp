#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "QuestDatabase.h"

#include "CMappedResource.h"
#include "FileRegistry.h"
#include "StringProcessing.h"
/////////////////////////////////////////////////////////////////////////////////////
SQuestDatabase theQuestDatabase;
/////////////////////////////////////////////////////////////////////////////////////
BOOL SQuestDatabase::Load(LPCSTR pszPath)
{
    CString szPathName = pszPath;
    CMappedResource res;
    if (res.OpenResource(szPathName + "database.res") == FALSE)
        return ReportError("Can't open database file \"%s\"\n", szPathName + "databases.res");
    //
    DWORD num;
    LPBYTE pData = res.GetFile("quests.qdb", &num);
    if (!pData)
        return ReportError("Can't open quests database\n");
    //
    CMemorySave file;

    file.StartSaving();
    file.Write(pData, num);
    file.Seek(0);
    file.StartLoading();

    quests.Load(file, 1);
    briefings.Load(file, 2);
    //
    res.CloseFile(pData);
    res.CloseResource();

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void ReadStringArray(CFileRegistry &reg, LPCSTR pszSection, LPCSTR pszEntry,
                     std::vector<CString> &szArray)
{
    CStringArray szStrings;
    reg.GetStringArray(pszSection, pszEntry, szStrings);
    szArray.clear();
    szArray.reserve(szStrings.GetSize());
    for (int i = 0; i < szStrings.GetSize(); ++i)
    {
        if (!szStrings[i].IsEmpty())
            szArray.push_back(szStrings[i]);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
bool SQuestDatabase::AddInfo(BYTE *pData)
{
    CFileRegistry reg;
    if (reg.Open(pData) == FALSE)
        return false;
    // read quest info
    SQDBQuest quest;
    CStringArray szStrings;

    quest.fExpTotal = 0;
    quest.szName = reg.GetString("quest", "name", "");
    quest.fExp = reg.GetFloat("quest", "exp", 0);
    quest.fMoney = reg.GetFloat("quest", "money", 0);
    quest.dwZone = reg.GetDWord("quest", "zone index", 0);
    quest.dwNumber = reg.GetDWord("quest", "order", 0);
    reg.GetStringArray("quest", "questplaces", szStrings);
    for (int i = 0; i < szStrings.GetSize(); ++i)
    {
        CStringArray szVals;
        szStrings[i].TrimLeft('{');
        szStrings[i].TrimRight('}');
        NStrProc::SplitString(szStrings[i], szVals, ';');
        if (szVals.GetSize() == 2)
        {
            quest.places.push_back(atof(szVals[0]));
            quest.places.push_back(atof(szVals[1]));
        }
    }

    quests.AddEntry(&quest);
    // read briefings info
    SQDBBriefing briefing;
    briefing.nSpecial = 0;
    // 'receive quest' briefing
    briefing.szName = reg.GetString("briefing receive", "name", "");
    briefing.fExp = reg.GetFloat("briefing receive", "exp", 0);
    briefing.fMoney = reg.GetFloat("briefing receive", "money", 0);
    ReadStringArray(reg, "briefing receive", "give items", briefing.szItems);
    ReadStringArray(reg, "briefing receive", "remove items", briefing.szRemoveItems);
    ReadStringArray(reg, "briefing receive", "give quests", briefing.szGiveQuests);
    ReadStringArray(reg, "briefing receive", "complete quests", briefing.szCompleteQuests);
    ReadStringArray(reg, "briefing receive", "available zones", briefing.szAvailableZones);
    ReadStringArray(reg, "briefing receive", "complete zones", briefing.szCompleteZones);
    briefings.AddEntry(&briefing);
    // 'reject quest' briefing
    briefing.szName = reg.GetString("briefing reject", "name", "");
    briefing.fExp = reg.GetFloat("briefing reject", "exp", 0);
    briefing.fMoney = reg.GetFloat("briefing reject", "money", 0);
    ReadStringArray(reg, "briefing reject", "give items", briefing.szItems);
    ReadStringArray(reg, "briefing reject", "remove items", briefing.szRemoveItems);
    ReadStringArray(reg, "briefing reject", "give quests", briefing.szGiveQuests);
    ReadStringArray(reg, "briefing reject", "complete quests", briefing.szCompleteQuests);
    ReadStringArray(reg, "briefing reject", "available zones", briefing.szAvailableZones);
    ReadStringArray(reg, "briefing reject", "complete zones", briefing.szCompleteZones);
    briefings.AddEntry(&briefing);
    // 'complete quest' briefing
    briefing.szName = reg.GetString("briefing complete", "name", "");
    briefing.fExp = reg.GetFloat("briefing complete", "exp", 0);
    briefing.fMoney = reg.GetFloat("briefing complete", "money", 0);
    ReadStringArray(reg, "briefing complete", "give items", briefing.szItems);
    ReadStringArray(reg, "briefing complete", "remove items", briefing.szRemoveItems);
    ReadStringArray(reg, "briefing complete", "give quests", briefing.szGiveQuests);
    ReadStringArray(reg, "briefing complete", "complete quests", briefing.szCompleteQuests);
    ReadStringArray(reg, "briefing complete", "available zones", briefing.szAvailableZones);
    ReadStringArray(reg, "briefing complete", "complete zones", briefing.szCompleteZones);
    briefings.AddEntry(&briefing);

    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
