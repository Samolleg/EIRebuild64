#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "ItemDatabase.h"

#include "CMappedResource.h"
/////////////////////////////////////////////////////////////////////////////////////
SItemDatabase theItemDatabase;
/////////////////////////////////////////////////////////////////////////////////////
BOOL SItemDatabase::Load(LPCSTR pszPath, LPCSTR pszFName)
{
    CString szPathName = CString(pszPath) + pszFName;
    CMappedResource res;
    if (res.OpenResource(szPathName) == FALSE)
        return ReportError("Can't open database file \"%s\"\n", szPathName);
    //
    DWORD num;
    LPBYTE pData = res.GetFile("items.idb", &num);
    if (!pData)
        return ReportError("Can't open items database\n");
    //
    CMemorySave file;

    file.StartSaving();
    file.Write(pData, num);
    file.Seek(0);
    file.StartLoading();

    materials.Load(file, 1);
    weapons.Load(file, 2);
    armors.Load(file, 3);
    quickItems.Load(file, 4);
    questItems.Load(file, 5);
    lootItems.Load(file, 6);
    //
    res.CloseFile(pData);
    res.CloseResource();
    //
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
