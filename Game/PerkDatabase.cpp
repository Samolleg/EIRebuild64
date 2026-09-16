#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "PerkDatabase.h"

#include "StringProcessing.h"
#include "BasicChunk.h"
#include "CMappedResource.h"
/////////////////////////////////////////////////////////////////////////////////////
CPerkDatabase thePerkDatabase;
/////////////////////////////////////////////////////////////////////////////////////
// get skill by codename
const SPDBSkill *CPerkDatabase::GetSkill(LPCSTR pszCode) const
{
    for (int i = 0; i < skills.size(); i++)
    {
        if (skills[i].szCode.CompareNoCase(pszCode) == 0)
            return &skills[i];
    }
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
// get skill's index by codename
int CPerkDatabase::GetSkillIndex(LPCSTR pszCode) const
{
    for (int i = 0; i < skills.size(); i++)
    {
        if (skills[i].szCode.CompareNoCase(pszCode) == 0)
            return i;
    }
    return -1;
}
/////////////////////////////////////////////////////////////////////////////////////
// get skill's index by skill entry
int CPerkDatabase::GetSkillIndex(const SPDBSkill *pSkill) const
{
    for (auto it = skills.begin(); it != skills.end(); ++it)
    {
        if (&*it == pSkill)
            return it - skills.begin();
    }
    return -1;
}
/////////////////////////////////////////////////////////////////////////////////////
// get perk by codename
const SPDBPerk *CPerkDatabase::GetPerk(LPCSTR pszCode) const
{
    for (int i = 0; i < perks.size(); i++)
    {
        if (perks[i].szCode.CompareNoCase(pszCode) == 0)
            return &perks[i];
    }
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
// get perk's index by codename
int CPerkDatabase::GetPerkIndex(LPCSTR pszCode) const
{
    for (int i = 0; i < perks.size(); i++)
    {
        if (perks[i].szCode.CompareNoCase(pszCode) == 0)
            return i;
    }
    return -1;
}
/////////////////////////////////////////////////////////////////////////////////////
// get perk's index by perk entry
int CPerkDatabase::GetPerkIndex(const SPDBPerk *pPerk) const
{
    for (auto it = perks.begin(); it != perks.end(); ++it)
    {
        if (&*it == pPerk)
            return it - perks.begin();
    }
    return -1;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CPerkDatabase::SaveDatabase(LPCSTR pszPath)
{
    CString szPath = pszPath;
    CStructureSaver saver;
    CFileSave file;

    printf("Saving database.\n");

    file.OpenWrite(szPath + "perks.pdb");
    saver.Start(file, false);
    saver.AddT(1, skills);
    saver.AddT(2, perks);
    saver.Finish(file);
    file.CloseFile();

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CPerkDatabase::LoadDatabase(LPCSTR pszPath, LPCSTR pszFName)
{
    CString szPathName = CString(pszPath) + pszFName;
    CMappedResource res;
    if (res.OpenResource(szPathName) == FALSE)
        return ReportError("Can't open database file \"%s\"\n", szPathName);
    //
    DWORD num;
    LPBYTE pData = res.GetFile("perks.pdb", &num);
    if (!pData)
        return ReportError("Can't open perks database\n");
    //
    CStructureSaver saver;
    CMemorySave file;

    file.StartSaving();
    file.Write(pData, num);
    file.Seek(0);
    file.StartLoading();
    saver.Start(file, true);
    saver.AddT(1, skills);
    saver.AddT(2, perks);
    saver.Finish(file);
    //
    res.CloseFile(pData);
    res.CloseResource();

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
