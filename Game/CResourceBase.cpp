#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "CResourceBase.h"
#include "Notification.h"
// --------------------------------------------------------------------------------------------
CResourceBase::CResourceBase()
{
    memset(&header, 0, sizeof(header));
    pHashTable = NULL;
    pNamePool = NULL;
    pdwAccelTable = NULL;
    nAccelTableSize = 0;
#ifdef TRY_OPEN_EXTERNAL_RESOURCES
    pAccelTable = NULL;
    nAccelNum = 0;
#endif
}
CResourceBase::~CResourceBase()
{
    memset(&header, 0, sizeof(header));
    pHashTable = NULL;
    pNamePool = NULL;
    FreeAccelTable();
}
// --------------------------------------------------------------------------------------------
static int GetHashValueByName(LPBYTE pszBytes, DWORD dwHashBase, int *pnNameLen)
{
    DWORD dwSum = 0;
    int nNameLen = 0;
    while (*pszBytes)
    {
        dwSum += tolower(*pszBytes++);
        nNameLen++;
    }
    *pnNameLen = nNameLen;
    return dwSum % dwHashBase;
}
// --------------------------------------------------------------------------------------------
// returns index in hash table or -1 if not found
DWORD CResourceBase::GetIndexOfName(LPCSTR pszName)
{
    if (header.dwEntryCount == 0)
        return -1;
    int nNameLen;
    int nIndex = GetHashValueByName(LPBYTE(pszName), header.dwEntryCount, &nNameLen);
    while (nNameLen != pHashTable[nIndex].wNameLen ||
           strnicmp(pszName, LPCSTR(pNamePool + pHashTable[nIndex].dwNameOffset), nNameLen))
    {
        nIndex = pHashTable[nIndex].dwNextIndex;
        if (nIndex == -1)
            return -1;
    }
    return nIndex;
}
DWORD CResourceBase::GetIndexOfID(int nResourceID)
{
    if (nResourceID >= nAccelTableSize)
        return -1;
    return pdwAccelTable[nResourceID];
}

// --------------------------------------------------------------------------------------------
// Creates accel table, returns FALSE if index out of range
#ifdef TRY_OPEN_EXTERNAL_RESOURCES
BOOL CResourceBase::CreateAccelTable(SResToID *_pAccelTable, int _nAccelNum)
{
    FreeAccelTable();
    pAccelTable = _pAccelTable;
    nAccelNum = _nAccelNum;
#else
BOOL CResourceBase::CreateAccelTable(SResToID *pAccelTable, int nAccelNum)
{
    FreeAccelTable();
#endif
    if (!pAccelTable)
        return TRUE;
    // Get max index of accel table
    int nMaxID = 0, i;
    for (i = 0; i < nAccelNum; i++)
        if (pAccelTable[i].nResourceID > nMaxID)
            nMaxID = pAccelTable[i].nResourceID;
    if (DWORD(nMaxID) > header.dwEntryCount)
    {
        ReportWarning("CResourceBase::CreateAccelTable().\nIndex out of range.\n");
        return FALSE;
    }
    nAccelTableSize = nMaxID + 1;
    pdwAccelTable = dbgnew DWORD[nAccelTableSize];
    if (!pdwAccelTable)
    {
        ReportWarning("CResourceBase::CreateAccelTable().\nNo enough memory.\n");
        nAccelTableSize = 0;
        return FALSE;
    }
    memset(pdwAccelTable, 0xFF, nAccelTableSize * sizeof(DWORD));
    for (i = 0; i < nAccelNum; i++)
    {
        pdwAccelTable[pAccelTable[i].nResourceID] = GetIndexOfName(pAccelTable[i].szResourceName);
    }
    return TRUE;
}

void CResourceBase::FreeAccelTable()
{
    if (pdwAccelTable)
    {
        delete[] pdwAccelTable;
        pdwAccelTable = NULL;
        nAccelTableSize = 0;
    }
#ifdef TRY_OPEN_EXTERNAL_RESOURCES
    pAccelTable = NULL;
    nAccelNum = 0;
#endif
}

// ----------------------- Finds name of resource by it ID -----------------------------------
#ifdef TRY_OPEN_EXTERNAL_RESOURCES
LPCSTR CResourceBase::FindNameByID(int nResourceID)
{
    for (int i = 0; i < nAccelNum; i++)
        if (pAccelTable[i].nResourceID == nResourceID)
            return pAccelTable[i].szResourceName;
    return NULL;
}
#endif

// -------------------------------- File list ------------------------------------------------

static BOOL strsimilar(LPCSTR pszName, LPCSTR pszMask, BOOL bRetIfMaskEmpty)
{
    if (*pszMask == 0)
        if (*pszName == 0)
            return TRUE;
        else
            return bRetIfMaskEmpty;
    for (; *pszName && *pszMask; pszName++, pszMask++)
    {
        if (*pszMask == '*')
            return TRUE;
        if (*pszMask != '?' && toupper(*pszName) != toupper(*pszMask))
            return FALSE;
    }
    for (; *pszMask; pszMask++)
        if (*pszMask != '?' && *pszMask != '*')
            return FALSE;
    if (*pszName)
        return FALSE;
    return TRUE;
}

static BOOL FileNameSimilar(LPCSTR pszName, LPCSTR pszMask)
{
    char driveN[_MAX_DRIVE];
    char dirN[_MAX_DIR];
    char fnameN[_MAX_FNAME];
    char extN[_MAX_EXT];
    char driveM[_MAX_DRIVE];
    char dirM[_MAX_DIR];
    char fnameM[_MAX_FNAME];
    char extM[_MAX_EXT];

    _splitpath(pszName, driveN, dirN, fnameN, extN);
    _splitpath(pszMask, driveM, dirM, fnameM, extM);
    return strsimilar(driveN, driveM, TRUE) && strsimilar(dirN, dirM, TRUE) &&
           strsimilar(fnameN, fnameM, FALSE) && strsimilar(extN, extM, TRUE);
}

void CResourceBase::GetFileList(CStringArray &aNames, LPCSTR pszMask)
{
    char temp[1024];
    aNames.RemoveAll();
    SResHashEntry *pCurEntry = pHashTable;
    for (DWORD i = 0; i < header.dwEntryCount; i++, pCurEntry++)
    {
        BYTE *pName = pNamePool + pCurEntry->dwNameOffset;
        memcpy(temp, pName, pCurEntry->wNameLen);
        temp[pCurEntry->wNameLen] = 0;
        if (pszMask == NULL || FileNameSimilar(temp, pszMask) == TRUE)
            aNames.Add(temp);
    }
}
