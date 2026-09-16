#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "LeverDatabase.h"

#include "CMappedResource.h"
/////////////////////////////////////////////////////////////////////////////////////
SLeverDatabase theLeverDatabase;
/////////////////////////////////////////////////////////////////////////////////////
BOOL SLeverDatabase::Load(LPCSTR pszPath)
{
    CString szPathName = pszPath;
    CMappedResource res;
    if (res.OpenResource(szPathName + "database.res") == FALSE)
        return ReportError("Can't open database file \"%s\"\n", szPathName + "databases.res");
    //
    DWORD num;
    LPBYTE pData = res.GetFile("Levers.ldb", &num);
    if (!pData)
        return ReportError("Can't open Levers database\n");
    //
    CMemorySave file;

    file.StartSaving();
    file.Write(pData, num);
    file.Seek(0);
    file.StartLoading();

    levers.Load(file, 1);
    //
    res.CloseFile(pData);
    res.CloseResource();
    //
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
