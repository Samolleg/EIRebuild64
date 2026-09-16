#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "TerrainPrintsDatabase.h"

#include "CMappedResource.h"
/////////////////////////////////////////////////////////////////////////////////////
STerrainPrintsDatabase theTPDatabase;
/////////////////////////////////////////////////////////////////////////////////////
BOOL STerrainPrintsDatabase::Load(LPCSTR pszPath)
{
    CString szPathName = pszPath;
    CMappedResource res;
    if (res.OpenResource(szPathName + "database.res") == FALSE)
        return ReportError("Can't open database file \"%s\"\n", szPathName + "databases.res");
    // open resource
    DWORD num;
    LPBYTE pData = res.GetFile("prints.db", &num);
    if (!pData)
        return ReportError("Can't open terrain prints database\n");
    // load data
    CMemorySave file;

    file.StartSaving();
    file.Write(pData, num);
    file.Seek(0);
    file.StartLoading();
    //
    blood.Load(file, 1);
    foot.Load(file, 2);
    fire.Load(file, 3);
    // close resource
    res.CloseFile(pData);
    res.CloseResource();

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
