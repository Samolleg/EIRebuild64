#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include <ctype.h>
#include "Notification.h"
#include "FileRegistry.h"
#include "ModRuntime.h"
#include "regFormat.h"
#ifdef _A4
#include "WorldClient.h"
#endif //_A4

/////////////////////////////////////////////////////////////////////////////////////
// Вычисляет хэш-значение для ASCIIZ строки
WORD GetHashValue(const char *pszSource, WORD wHashBase)
{
    DWORD dwSum;
    for (dwSum = 0; *pszSource; dwSum += tolower(*pszSource++))
        ;
    return WORD(dwSum % wHashBase);
}

//--------------- Конструктор/деструктор ------------------------------------------------------
CFileRegistry::CFileRegistry()
{
    bIsInRam = FALSE;
    pBaseAddress = NULL;
}

CFileRegistry::~CFileRegistry()
{
    Close();
}

//--------------- Open/Close ------------------------------------------------------------------
BOOL CFileRegistry::Open(LPCSTR pszFileName)
{
    std::string resolved=ModRuntime::ConfigPath(pszFileName);
    if (!cMappedFile.Open(resolved.c_str()))
        return FALSE;
    bIsInRam = FALSE;
    pBaseAddress = (BYTE *)cMappedFile.GetAddress();
    return IsValid();
}

BOOL CFileRegistry::Open(LPVOID pFileData)
{
    bIsInRam = TRUE;
    pBaseAddress = (BYTE *)pFileData;
    return IsValid();
}

BOOL CFileRegistry::IsValid()
{
    if (((SRegHeader *)pBaseAddress)->dwSignature != SREGHEADER_SIGNATURE)
    {
        ReportWarning("CFileRegistry::IsValid().\nThat is not registry file.\n");
        Close();
        return FALSE;
    }
    return TRUE;
}

void CFileRegistry::Close()
{
    if (!bIsInRam)
        cMappedFile.Close();
#ifdef _A4
    else
        theWorld.CloseReg(pBaseAddress);
#endif //_A4
    bIsInRam = FALSE;
    pBaseAddress = NULL;
}
//----------------- Find general data from registry file -------------------------------------

LPBYTE CFileRegistry::FindData(LPCSTR pszSection, LPCSTR pszEntry, BYTE cType, BOOL *pbIsSignle,
                               BOOL bReportTypeMismatch)
{
    if (pBaseAddress == NULL)
        return NULL;
    SRegHashEntry *pHash;
    WORD wNextIndex;
    WORD wNameLen;
    // Lookup for required section
    WORD wSectNumber = ((SRegHeader *)pBaseAddress)->wSectionsNumber;
    pHash = (SRegHashEntry *)(pBaseAddress + sizeof(SRegHeader));
    wNextIndex = GetHashValue(pszSection, wSectNumber);
    SSectionHeader *pSectHeader;
    wNameLen = WORD(strlen(pszSection));
    while (1)
    {
        pSectHeader = (SSectionHeader *)(pBaseAddress + pHash[wNextIndex].dwOffset);
        if (pSectHeader->wNameLen == wNameLen &&
            memicmp(LPBYTE(pSectHeader) + sizeof(SSectionHeader), pszSection, wNameLen) == 0)
            // required section finded
            break;
        if (pHash[wNextIndex].wNextIndex != (WORD)-1)
            wNextIndex = pHash[wNextIndex].wNextIndex;
        else
        {
#ifdef _DEBUG
            char buf[512];
            sprintf(buf, "CFileRegistry::FindData().\nUnable to find section \"%s\".\n",
                    pszSection);
            ReportWarning(buf);
#endif
            return NULL;
        }
    }
    if (NULL == pszEntry)
        return (LPBYTE)pSectHeader;
    WORD wValNumber = ((SSectionHeader *)pSectHeader)->wValuesNumber;
    pHash = (SRegHashEntry *)(LPBYTE(pSectHeader) + sizeof(SSectionHeader) + wNameLen);
    wNextIndex = GetHashValue(pszEntry, wValNumber);
    SValueHeader *pValHeader = NULL;
    wNameLen = WORD(strlen(pszEntry));
    while (1)
    {
        pValHeader = (SValueHeader *)(LPBYTE(pSectHeader) + pHash[wNextIndex].dwOffset);
        if (pValHeader->wNameLen == wNameLen &&
            memicmp(LPBYTE(pValHeader) + sizeof(SValueHeader), pszEntry, wNameLen) == 0)
            // required value finded
            break;
        if (pHash[wNextIndex].wNextIndex != (WORD)-1)
            wNextIndex = pHash[wNextIndex].wNextIndex;
        else
        {
#ifdef _DEBUG
            char buf[512];
            sprintf(buf,
                    "CFileRegistry::FindData().\nUnable to find entry \"%s\" of section \"%s\".\n",
                    pszEntry, pszSection);
            ReportWarning(buf);
#endif
            return NULL;
        }
    }
    if (pbIsSignle)
        *pbIsSignle = !(cType & ERT_ARRAY);
    if (pValHeader->cType != cType)
    {
        if (pValHeader->cType == (cType & ~ERT_ARRAY))
        {
            if (pbIsSignle)
                *pbIsSignle = TRUE;
        }
        else
        {
            if (bReportTypeMismatch)
                ReportWarning("CFileRegistry::FindData(). Wrong type of entry. Section: %s, Entry: %s",
                              pszSection, pszEntry);
            return NULL;
        }
    }
    return LPBYTE(pValHeader) + sizeof(SValueHeader) + wNameLen;
}

//-------------------------- get sections/entries --------------------------------------------
BOOL CFileRegistry::GetSections(CStringArray &sections)
{
    WORD wSectNumber = ((SRegHeader *)pBaseAddress)->wSectionsNumber;
    SRegHashEntry *pHash = (SRegHashEntry *)(pBaseAddress + sizeof(SRegHeader));
    sections.SetSize(wSectNumber);
    for (int i = 0; i < wSectNumber; i++)
    {
        SSectionHeader *pSectHeader = (SSectionHeader *)(pBaseAddress + pHash[i].dwOffset);
        sections[i] = CString(LPCTSTR(pSectHeader) + sizeof(SSectionHeader), pSectHeader->wNameLen);
    }
    return TRUE;
}
BOOL CFileRegistry::GetEntries(LPCSTR pszSection, CStringArray &entries)
{
    SSectionHeader *pSectHeader = (SSectionHeader *)FindData(pszSection, 0, 0);
    if (!pSectHeader)
    {
        ReportWarning("CFileRegistry::GetEntries().\nUnable to find section.\n");
        return FALSE;
    }
    WORD wEntryNumber = pSectHeader->wValuesNumber;
    SRegHashEntry *pHash = (SRegHashEntry *)(LPBYTE(pSectHeader) + sizeof(SSectionHeader) +
                                             pSectHeader->wNameLen);
    entries.SetSize(wEntryNumber);
    for (int i = 0; i < wEntryNumber; i++)
    {
        SValueHeader *pValHeader = (SValueHeader *)(LPBYTE(pSectHeader) + pHash[i].dwOffset);
        entries[i] = CString(LPCTSTR(pValHeader) + sizeof(SValueHeader), pValHeader->wNameLen);
    }
    return TRUE;
}
//------------------ simply integer data read functions --------------------------------------
char CFileRegistry::GetChar(LPCSTR pszSection, LPCSTR pszEntry, char cDefault)
{
    LPDWORD pRet = (LPDWORD)FindData(pszSection, pszEntry, ERT_DWORD);
    if (pRet)
        return char(*pRet);
    else
    {
        LPDWORD pRet = (LPDWORD)FindData(pszSection, pszEntry, ERT_FLOAT);
        if (pRet)
            return char(*((float *)pRet));
    }
    return cDefault;
}
BYTE CFileRegistry::GetByte(LPCSTR pszSection, LPCSTR pszEntry, BYTE cDefault)
{
    LPDWORD pRet = (LPDWORD)FindData(pszSection, pszEntry, ERT_DWORD);
    if (pRet)
        return BYTE(*pRet);
    else
    {
        LPDWORD pRet = (LPDWORD)FindData(pszSection, pszEntry, ERT_FLOAT);
        if (pRet)
            return BYTE(*((float *)pRet));
    }
    return cDefault;
}
short CFileRegistry::GetShort(LPCSTR pszSection, LPCSTR pszEntry, short wDefault)
{
    LPDWORD pRet = (LPDWORD)FindData(pszSection, pszEntry, ERT_DWORD);
    if (pRet)
        return short(*pRet);
    else
    {
        LPDWORD pRet = (LPDWORD)FindData(pszSection, pszEntry, ERT_FLOAT);
        if (pRet)
            return short(*((float *)pRet));
    }
    return wDefault;
}
WORD CFileRegistry::GetWord(LPCSTR pszSection, LPCSTR pszEntry, WORD wDefault)
{
    LPDWORD pRet = (LPDWORD)FindData(pszSection, pszEntry, ERT_DWORD);
    if (pRet)
        return WORD(*pRet);
    else
    {
        LPDWORD pRet = (LPDWORD)FindData(pszSection, pszEntry, ERT_FLOAT);
        if (pRet)
            return WORD(*((float *)pRet));
    }
    return wDefault;
}
long CFileRegistry::GetLong(LPCSTR pszSection, LPCSTR pszEntry, long dwDefault)
{
    LPDWORD pRet = (LPDWORD)FindData(pszSection, pszEntry, ERT_DWORD);
    if (pRet)
        return long(*pRet);
    else
    {
        LPDWORD pRet = (LPDWORD)FindData(pszSection, pszEntry, ERT_FLOAT);
        if (pRet)
            return long(*((float *)pRet));
    }
    return dwDefault;
}
DWORD CFileRegistry::GetDWord(LPCSTR pszSection, LPCSTR pszEntry, DWORD dwDefault)
{
    LPDWORD pRet = (LPDWORD)FindData(pszSection, pszEntry, ERT_DWORD);
    if (pRet)
        return DWORD(*pRet);
    else
    {
        LPDWORD pRet = (LPDWORD)FindData(pszSection, pszEntry, ERT_FLOAT);
        if (pRet)
            return DWORD(*((float *)pRet));
    }
    return dwDefault;
}
int CFileRegistry::GetInt(LPCSTR pszSection, LPCSTR pszEntry, int nDefault)
{
    LPDWORD pRet = (LPDWORD)FindData(pszSection, pszEntry, ERT_DWORD, NULL, FALSE);
    if (pRet)
        return int(*pRet);
    else
    {
        LPDWORD pRet = (LPDWORD)FindData(pszSection, pszEntry, ERT_FLOAT);
        if (pRet)
            return int(*((float *)pRet));
    }
    return nDefault;
}
UINT CFileRegistry::GetUInt(LPCSTR pszSection, LPCSTR pszEntry, UINT nDefault)
{
    LPDWORD pRet = (LPDWORD)FindData(pszSection, pszEntry, ERT_DWORD, NULL, FALSE);
    if (pRet)
        return UINT(*pRet);
    else
    {
        LPDWORD pRet = (LPDWORD)FindData(pszSection, pszEntry, ERT_FLOAT);
        if (pRet)
            return UINT(*((float *)pRet));
    }
    return nDefault;
}

// ---------------------- simply fp data read functions ---------------------------------------
float CFileRegistry::GetFloat(LPCSTR pszSection, LPCSTR pszEntry, float fpDefault)
{
    float *pRet = (float *)FindData(pszSection, pszEntry, ERT_FLOAT, NULL, FALSE);
    if (pRet)
        return *pRet;
    else
    {
        pRet = (float *)FindData(pszSection, pszEntry, ERT_DWORD);
        if (pRet)
            return float(*(long *)(pRet));
    }
    return fpDefault;
}

double CFileRegistry::GetDouble(LPCSTR pszSection, LPCSTR pszEntry, double fpDefault)
{
    float *pRet = (float *)FindData(pszSection, pszEntry, ERT_FLOAT, NULL, FALSE);
    if (pRet)
        return double(*pRet);
    else
    {
        pRet = (float *)FindData(pszSection, pszEntry, ERT_DWORD);
        if (pRet)
            return double(*(long *)(pRet));
    }
    return fpDefault;
}
// --------------------- simply string data read functions ------------------------------------
CString CFileRegistry::GetString(LPCSTR pszSection, LPCSTR pszEntry, CString szDefault)
{
    const LPWORD pRet = (LPWORD)FindData(pszSection, pszEntry, ERT_STRING);
    if (pRet)
        return CString(LPCTSTR(pRet) + sizeof(WORD), *pRet);
    else
        return szDefault;
}

void CFileRegistry::GetString(LPCSTR pszSection, LPCSTR pszEntry, LPSTR pBuf, int &nBufLen,
                              LPCSTR pszDefault)
{
    LPWORD pRet = (LPWORD)FindData(pszSection, pszEntry, ERT_STRING);
    if (pRet)
    {
        WORD wLen = *pRet++;
        if (wLen >= nBufLen)
            wLen = nBufLen - 1;
        strncpy(pBuf, LPCTSTR(pRet), wLen);
        pBuf[wLen] = 0;
        nBufLen = wLen;
    }
    else
    {
        strncpy(pBuf, pszDefault, nBufLen);
        nBufLen = strlen(pBuf);
    }
}
// -------------------- complex array data read functions -------------------------------------
BOOL CFileRegistry::GetCharArray(LPCSTR pszSection, LPCSTR pszEntry, CCharArray &chars)
{
    BOOL bIsSignle;
    LPWORD pRet = (LPWORD)FindData(pszSection, pszEntry, ERT_DWORD | ERT_ARRAY, &bIsSignle);
    if (!pRet)
    {
        chars.SetSize(0);
        return FALSE;
    }
    int nArraySize;
    DWORD *pArrayData;
    if (bIsSignle)
    {
        nArraySize = 1;
        pArrayData = (LPDWORD)pRet;
    }
    else
    {
        nArraySize = *pRet;
        pArrayData = (LPDWORD)(pRet + 1);
    }
    chars.SetSize(nArraySize);
    for (int i = 0; i < nArraySize; i++, pArrayData++)
        chars[i] = char(*pArrayData);
    return TRUE;
}
BOOL CFileRegistry::GetByteArray(LPCSTR pszSection, LPCSTR pszEntry, CByteArray &bytes)
{
    BOOL bIsSignle;
    LPWORD pRet = (LPWORD)FindData(pszSection, pszEntry, ERT_DWORD | ERT_ARRAY, &bIsSignle);
    if (!pRet)
    {
        bytes.SetSize(0);
        return FALSE;
    }
    int nArraySize;
    DWORD *pArrayData;
    if (bIsSignle)
    {
        nArraySize = 1;
        pArrayData = (LPDWORD)pRet;
    }
    else
    {
        nArraySize = *pRet;
        pArrayData = (LPDWORD)(pRet + 1);
    }
    bytes.SetSize(nArraySize);
    for (int i = 0; i < nArraySize; i++, pArrayData++)
        bytes[i] = BYTE(*pArrayData);
    return TRUE;
}
BOOL CFileRegistry::GetShortArray(LPCSTR pszSection, LPCSTR pszEntry, CShortArray &shorts)
{
    BOOL bIsSignle;
    LPWORD pRet = (LPWORD)FindData(pszSection, pszEntry, ERT_DWORD | ERT_ARRAY, &bIsSignle);
    if (!pRet)
    {
        shorts.SetSize(0);
        return FALSE;
    }
    int nArraySize;
    DWORD *pArrayData;
    if (bIsSignle)
    {
        nArraySize = 1;
        pArrayData = (LPDWORD)pRet;
    }
    else
    {
        nArraySize = *pRet;
        pArrayData = (LPDWORD)(pRet + 1);
    }
    shorts.SetSize(nArraySize);
    for (int i = 0; i < nArraySize; i++, pArrayData++)
        shorts[i] = short(*pArrayData);
    return TRUE;
}
BOOL CFileRegistry::GetWordArray(LPCSTR pszSection, LPCSTR pszEntry, CWordArray &words)
{
    BOOL bIsSignle;
    LPWORD pRet = (LPWORD)FindData(pszSection, pszEntry, ERT_DWORD | ERT_ARRAY, &bIsSignle);
    if (!pRet)
    {
        words.SetSize(0);
        return FALSE;
    }
    int nArraySize;
    DWORD *pArrayData;
    if (bIsSignle)
    {
        nArraySize = 1;
        pArrayData = (LPDWORD)pRet;
    }
    else
    {
        nArraySize = *pRet;
        pArrayData = (LPDWORD)(pRet + 1);
    }
    words.SetSize(nArraySize);
    for (int i = 0; i < nArraySize; i++, pArrayData++)
        words[i] = WORD(*pArrayData);
    return TRUE;
}
BOOL CFileRegistry::GetLongArray(LPCSTR pszSection, LPCSTR pszEntry, CLongArray &longs)
{
    BOOL bIsSignle;
    LPWORD pRet = (LPWORD)FindData(pszSection, pszEntry, ERT_DWORD | ERT_ARRAY, &bIsSignle);
    if (!pRet)
    {
        longs.SetSize(0);
        return FALSE;
    }
    int nArraySize;
    DWORD *pArrayData;
    if (bIsSignle)
    {
        nArraySize = 1;
        pArrayData = (LPDWORD)pRet;
    }
    else
    {
        nArraySize = *pRet;
        pArrayData = (LPDWORD)(pRet + 1);
    }
    longs.SetSize(nArraySize);
    for (int i = 0; i < nArraySize; i++, pArrayData++)
        longs[i] = long(*pArrayData);
    return TRUE;
}
BOOL CFileRegistry::GetDWordArray(LPCSTR pszSection, LPCSTR pszEntry, CDWordArray &dwords)
{
    BOOL bIsSignle;
    LPWORD pRet = (LPWORD)FindData(pszSection, pszEntry, ERT_DWORD | ERT_ARRAY, &bIsSignle);
    if (!pRet)
    {
        dwords.SetSize(0);
        return FALSE;
    }
    int nArraySize;
    DWORD *pArrayData;
    if (bIsSignle)
    {
        nArraySize = 1;
        pArrayData = (LPDWORD)pRet;
    }
    else
    {
        nArraySize = *pRet;
        pArrayData = (LPDWORD)(pRet + 1);
    }
    dwords.SetSize(nArraySize);
    for (int i = 0; i < nArraySize; i++, pArrayData++)
        dwords[i] = *pArrayData;
    return TRUE;
}
BOOL CFileRegistry::GetIntArray(LPCSTR pszSection, LPCSTR pszEntry, CIntArray &ints)
{
    BOOL bIsSignle;
    LPWORD pRet = (LPWORD)FindData(pszSection, pszEntry, ERT_DWORD | ERT_ARRAY, &bIsSignle);
    if (!pRet)
    {
        ints.SetSize(0);
        return FALSE;
    }
    int nArraySize;
    DWORD *pArrayData;
    if (bIsSignle)
    {
        nArraySize = 1;
        pArrayData = (LPDWORD)pRet;
    }
    else
    {
        nArraySize = *pRet;
        pArrayData = (LPDWORD)(pRet + 1);
    }
    ints.SetSize(nArraySize);
    for (int i = 0; i < nArraySize; i++, pArrayData++)
        ints[i] = int(*pArrayData);
    return TRUE;
}
BOOL CFileRegistry::GetUIntArray(LPCSTR pszSection, LPCSTR pszEntry, CUIntArray &uints)
{
    BOOL bIsSignle;
    LPWORD pRet = (LPWORD)FindData(pszSection, pszEntry, ERT_DWORD | ERT_ARRAY, &bIsSignle);
    if (!pRet)
    {
        uints.SetSize(0);
        return FALSE;
    }
    int nArraySize;
    DWORD *pArrayData;
    if (bIsSignle)
    {
        nArraySize = 1;
        pArrayData = (LPDWORD)pRet;
    }
    else
    {
        nArraySize = *pRet;
        pArrayData = (LPDWORD)(pRet + 1);
    }
    uints.SetSize(nArraySize);
    for (int i = 0; i < nArraySize; i++, pArrayData++)
        uints[i] = UINT(*pArrayData);
    return TRUE;
}
BOOL CFileRegistry::GetFloatArray(LPCSTR pszSection, LPCSTR pszEntry, CFloatArray &floats)
{
    BOOL bIsSignle;
    LPWORD pRet = (LPWORD)FindData(pszSection, pszEntry, ERT_FLOAT | ERT_ARRAY, &bIsSignle);
    if (!pRet)
    {
        floats.SetSize(0);
        return FALSE;
    }
    int nArraySize;
    float *pArrayData;
    if (bIsSignle)
    {
        nArraySize = 1;
        pArrayData = (float *)pRet;
    }
    else
    {
        nArraySize = *pRet;
        pArrayData = (float *)(pRet + 1);
    }
    floats.SetSize(nArraySize);
    for (int i = 0; i < nArraySize; i++, pArrayData++)
        floats[i] = *pArrayData;
    return TRUE;
}
BOOL CFileRegistry::GetDoubleArray(LPCSTR pszSection, LPCSTR pszEntry, CDoubleArray &doubles)
{
    BOOL bIsSignle;
    LPWORD pRet = (LPWORD)FindData(pszSection, pszEntry, ERT_FLOAT | ERT_ARRAY, &bIsSignle);
    if (!pRet)
    {
        doubles.SetSize(0);
        return FALSE;
    }
    int nArraySize;
    float *pArrayData;
    if (bIsSignle)
    {
        nArraySize = 1;
        pArrayData = (float *)pRet;
    }
    else
    {
        nArraySize = *pRet;
        pArrayData = (float *)(pRet + 1);
    }
    doubles.SetSize(nArraySize);
    for (int i = 0; i < nArraySize; i++, pArrayData++)
        doubles[i] = double(*pArrayData);
    return TRUE;
}
BOOL CFileRegistry::GetStringArray(LPCSTR pszSection, LPCSTR pszEntry, CStringArray &strings)
{
    BOOL bIsSignle;
    LPWORD pRet = (LPWORD)FindData(pszSection, pszEntry, ERT_STRING | ERT_ARRAY, &bIsSignle);
    if (!pRet)
    {
        strings.SetSize(0);
        return FALSE;
    }
    int nArraySize;
    LPBYTE pArrayData;
    if (bIsSignle)
    {
        nArraySize = 1;
        pArrayData = (LPBYTE)pRet;
    }
    else
    {
        nArraySize = *pRet;
        pArrayData = (LPBYTE)(pRet + 1);
    }
    strings.SetSize(nArraySize);
    for (int i = 0; i < nArraySize; i++, pArrayData += *LPWORD(pArrayData) + sizeof(WORD))
        strings[i] = CString(LPCTSTR(pArrayData) + sizeof(WORD), *LPWORD(pArrayData));
    return TRUE;
}
// --------------------- complex data read functions ------------------------------------------
CPoint CFileRegistry::GetCPoint(LPCSTR pszSection, LPCSTR pszEntry, CPoint ptDefault)
{
    LPWORD pRet = (LPWORD)FindData(pszSection, pszEntry, ERT_DWORD | ERT_ARRAY);
    if (!pRet)
        return ptDefault;
    if (*pRet != 2)
    {
        ReportWarning("CFileRegistry::GetCPoint().\nWrong type of entry.\n");
        return ptDefault;
    }
    DWORD *pArrayData = (LPDWORD)(pRet + 1);
    return CPoint(*((int *)pArrayData), ((int *)pArrayData)[1]);
}
CRect CFileRegistry::GetCRect(LPCSTR pszSection, LPCSTR pszEntry, CRect rcDefault)
{
    LPWORD pRet = (LPWORD)FindData(pszSection, pszEntry, ERT_DWORD | ERT_ARRAY);
    if (!pRet)
        return rcDefault;
    if (*pRet != 4)
    {
        ReportWarning("CFileRegistry::GetCRect().\nWrong type of entry.\n");
        return rcDefault;
    }
    DWORD *pArrayData = (LPDWORD)(pRet + 1);
    return CRect(*((int *)pArrayData), ((int *)pArrayData)[1], ((int *)pArrayData)[2],
                 ((int *)pArrayData)[3]);
}
CSize CFileRegistry::GetCSize(LPCSTR pszSection, LPCSTR pszEntry, CSize siDefault)
{
    LPWORD pRet = (LPWORD)FindData(pszSection, pszEntry, ERT_DWORD | ERT_ARRAY);
    if (!pRet)
        return siDefault;
    if (*pRet != 2)
    {
        ReportWarning("CFileRegistry::GetCSize().\nWrong type of entry.\n");
        return siDefault;
    }
    DWORD *pArrayData = (LPDWORD)(pRet + 1);
    return CSize(*((int *)pArrayData), ((int *)pArrayData)[1]);
}
