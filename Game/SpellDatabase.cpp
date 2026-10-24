#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "SpellDatabase.h"

#include "CMappedResource.h"
#include "AssetCodec.h"
/////////////////////////////////////////////////////////////////////////////////////
SSpellDatabase theSpellDatabase;
/////////////////////////////////////////////////////////////////////////////////////
BOOL SSpellDatabase::Load(LPCSTR pszPath, LPCSTR pszFName)
{
    CString szPathName = CString(pszPath) + pszFName;
    CMappedResource res;
    if (res.OpenResource(szPathName) == FALSE)
        return ReportError("Can't open database file \"%s\"\n", szPathName);
    //
    DWORD num;
    LPBYTE pData = res.GetFile("spells.sdb", &num);
    if (!pData)
        return ReportError("Can't open spells database\n");
    //
    CMemorySave file;

    file.StartSaving();
    file.Write(pData, num);
    file.Seek(0);
    file.StartLoading();

    spells.Load(file, 1);
    runes.Load(file, 2);
    //
    CStructureSaver saver;
    saver.Start(file, true);
    AssetCodec::Archive(saver).AddT(3, cspells);
    AssetCodec::Archive(saver).AddT(4, carmors);
    AssetCodec::Archive(saver).AddT(5, cweapons);
    saver.Finish(file);
    //
    res.CloseFile(pData);
    res.CloseResource();
    //
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
