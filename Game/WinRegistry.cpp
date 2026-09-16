#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "Notification.h"
#include "WinRegistry.h"
/////////////////////////////////////////////////////////////////////////////////////

// construcor
CWinRegistry::CWinRegistry()
{
    hBaseKey = NULL;
}
// destructor
CWinRegistry::~CWinRegistry()
{
    Close();
}

BOOL CWinRegistry::CreateKey(HKEY hParent, LPCSTR pszSubKey, PHKEY phResult)
{
    DWORD dwDisposition;
    char ts[2] = "";
    return ERROR_SUCCESS == RegCreateKeyEx(hParent, pszSubKey, 0, ts, REG_OPTION_NON_VOLATILE,
                                           KEY_ALL_ACCESS, 0, phResult, &dwDisposition);
}

BOOL CWinRegistry::OpenKey(HKEY hParent, LPCSTR pszSubKey, PHKEY phResult)
{
    return ERROR_SUCCESS == RegOpenKeyEx(hParent, pszSubKey, 0, KEY_ALL_ACCESS, phResult);
}
// open registry to read/write
BOOL CWinRegistry::Open(LPCSTR pszKeyName, HKEY hkRootKey)
{
    Close();
    if (!CreateKey(hkRootKey, pszKeyName, &hBaseKey))
    {
        hBaseKey = NULL;
        return FALSE;
    }
    return TRUE;
}
// close registry to read/write
void CWinRegistry::Close()
{
    if (hBaseKey != NULL)
    {
        RegCloseKey(hBaseKey);
        hBaseKey = NULL;
    }
}

//-------------- Internal read/write functions --------------------------------
BOOL CWinRegistry::SetDWordInt(LPCSTR pszSection, LPCSTR pszEntry, DWORD dwData)
{
    HKEY hSection;
    if (!CreateKey(hBaseKey, pszSection, &hSection))
    {
        ReportWarning("CWinRegistry::SetDWordInt().\nUnable to open key.\n");
        return FALSE;
    }
    if (ERROR_SUCCESS !=
        RegSetValueEx(hSection, pszEntry, 0, REG_DWORD, (BYTE *)&dwData, sizeof(DWORD)))
    {
        ReportWarning("CWinRegistry::SetDWordInt().\nUnable to access registry.\n");
        RegCloseKey(hSection);
        return FALSE;
    }
    RegCloseKey(hSection);
    return TRUE;
}

BOOL CWinRegistry::SetStringInt(LPCSTR pszSection, LPCSTR pszEntry, CString szData)
{
    HKEY hSection;
    if (!CreateKey(hBaseKey, pszSection, &hSection))
    {
        ReportWarning("CWinRegistry::SetStringInt().\nUnable to open key.\n");
        return FALSE;
    }
    if (ERROR_SUCCESS != RegSetValueEx(hSection, pszEntry, 0, REG_SZ, (LPBYTE)(LPCTSTR)szData,
                                       szData.GetLength() + 1))
    {
        ReportWarning("CWinRegistry::SetStringInt().\nUnable to access registry.\n");
        RegCloseKey(hSection);
        return FALSE;
    }
    RegCloseKey(hSection);
    return TRUE;
}

BOOL CWinRegistry::SetBinInt(LPCSTR pszSection, LPCSTR pszEntry, LPVOID pData, int nDataLen)
{
    HKEY hSection;
    if (!CreateKey(hBaseKey, pszSection, &hSection))
    {
        ReportWarning("CWinRegistry::SetBinInt().\nUnable to open key.\n");
        return FALSE;
    }
    if (ERROR_SUCCESS != RegSetValueEx(hSection, pszEntry, 0, REG_BINARY, (LPBYTE)pData, nDataLen))
    {
        ReportWarning("CWinRegistry::SetBinInt().\nUnable to access registry.\n");
        RegCloseKey(hSection);
        return FALSE;
    }
    RegCloseKey(hSection);
    return TRUE;
}

DWORD CWinRegistry::GetDWordInt(LPCSTR pszSection, LPCSTR pszEntry, DWORD dwDefault)
{
    HKEY hSection;
    if (!OpenKey(hBaseKey, pszSection, &hSection))
        return dwDefault;
    DWORD dwRet, dwSize = sizeof(DWORD), dwType;
    if (ERROR_SUCCESS != RegQueryValueEx(hSection, pszEntry, 0, &dwType, (LPBYTE)&dwRet, &dwSize))
    {
        RegCloseKey(hSection);
        return dwDefault;
    }
    RegCloseKey(hSection);
    if (dwType != REG_DWORD)
    {
        ReportWarning("CWinRegistry::GetDWordInt().\nRegistry data type is not DWORD.\n");
        return dwDefault;
    }
    return dwRet;
}

CString CWinRegistry::GetStringInt(LPCSTR pszSection, LPCSTR pszEntry, CString szDefault)
{
    HKEY hSection;
    if (!OpenKey(hBaseKey, pszSection, &hSection))
        return szDefault;
    DWORD dwSize = 256, dwType;
    BYTE szBuf[256], *pBuf = 0;
    LONG rv = RegQueryValueEx(hSection, pszEntry, 0, &dwType, szBuf, &dwSize);
    if (ERROR_MORE_DATA == rv)
    {
        pBuf = (LPBYTE)malloc(dwSize);
        rv = RegQueryValueEx(hSection, pszEntry, 0, &dwType, pBuf, &dwSize);
    }
    RegCloseKey(hSection);
    if (ERROR_SUCCESS != rv)
    {
        if (pBuf)
            free(pBuf);
        return szDefault;
    }
    if (dwType != REG_SZ)
    {
        ReportWarning("CWinRegistry::GetStringInt().\nRegistry data type is not SZ.\n");
        if (pBuf)
            free(pBuf);
        return szDefault;
    }
    if (pBuf)
    {
        CString szRet(pBuf);
        free(pBuf);
        return szRet;
    }
    return CString(szBuf);
}

void CWinRegistry::GetBinInt(LPCSTR pszSection, LPCSTR pszEntry, LPVOID pDefault, int nDataLen,
                             LPVOID pBuffer)
{
    HKEY hSection;
    if (!OpenKey(hBaseKey, pszSection, &hSection))
    {
        memcpy(pBuffer, pDefault, nDataLen);
        return;
    }
    DWORD dwSize = (DWORD)nDataLen, dwType;
    if (ERROR_SUCCESS != RegQueryValueEx(hSection, pszEntry, 0, &dwType, (LPBYTE)pBuffer, &dwSize))
    {
        RegCloseKey(hSection);
        memcpy(pBuffer, pDefault, nDataLen);
        return;
    }
    RegCloseKey(hSection);
    if (dwType != REG_BINARY)
    {
        ReportWarning("CWinRegistry::GetBinInt().\nRegistry data type is not BINARY.\n");
        memcpy(pBuffer, pDefault, nDataLen);
        return;
    }
}
// ------------------ Write functions --------------------------------------

// simply integer data write functions
BOOL CWinRegistry::SetChar(LPCSTR pszSection, LPCSTR pszEntry, char cData)
{
    return SetDWordInt(pszSection, pszEntry, cData);
}
BOOL CWinRegistry::SetByte(LPCSTR pszSection, LPCSTR pszEntry, BYTE cData)
{
    return SetDWordInt(pszSection, pszEntry, cData);
}
BOOL CWinRegistry::SetShort(LPCSTR pszSection, LPCSTR pszEntry, short wData)
{
    return SetDWordInt(pszSection, pszEntry, wData);
}
BOOL CWinRegistry::SetWord(LPCSTR pszSection, LPCSTR pszEntry, WORD wData)
{
    return SetDWordInt(pszSection, pszEntry, wData);
}
BOOL CWinRegistry::SetLong(LPCSTR pszSection, LPCSTR pszEntry, long dwData)
{
    return SetDWordInt(pszSection, pszEntry, dwData);
}
BOOL CWinRegistry::SetDWord(LPCSTR pszSection, LPCSTR pszEntry, DWORD dwData)
{
    return SetDWordInt(pszSection, pszEntry, dwData);
}
BOOL CWinRegistry::SetInt(LPCSTR pszSection, LPCSTR pszEntry, int nData)
{
    return SetDWordInt(pszSection, pszEntry, nData);
}
BOOL CWinRegistry::SetUInt(LPCSTR pszSection, LPCSTR pszEntry, UINT nData)
{
    return SetDWordInt(pszSection, pszEntry, nData);
}
// simply fp data write functions
BOOL CWinRegistry::SetFloat(LPCSTR pszSection, LPCSTR pszEntry, float fpData)
{
    return SetBinInt(pszSection, pszEntry, &fpData, sizeof(float));
};
BOOL CWinRegistry::SetDouble(LPCSTR pszSection, LPCSTR pszEntry, double fpData)
{
    return SetBinInt(pszSection, pszEntry, &fpData, sizeof(double));
}
// simply string data write functions
BOOL CWinRegistry::SetString(LPCSTR pszSection, LPCSTR pszEntry, CString szData)
{
    return SetStringInt(pszSection, pszEntry, szData);
}
// complex data write functions
BOOL CWinRegistry::SetCPoint(LPCSTR pszSection, LPCSTR pszEntry, CPoint ptData)
{
    return SetBinInt(pszSection, pszEntry, &ptData, sizeof(CPoint));
}
BOOL CWinRegistry::SetCRect(LPCSTR pszSection, LPCSTR pszEntry, CRect rcData)
{
    return SetBinInt(pszSection, pszEntry, &rcData, sizeof(CRect));
}
BOOL CWinRegistry::SetCSize(LPCSTR pszSection, LPCSTR pszEntry, CSize siData)
{
    return SetBinInt(pszSection, pszEntry, &siData, sizeof(CSize));
}

// ------------------ Read functions --------------------------------------
// simply integer data read functions
char CWinRegistry::GetChar(LPCSTR pszSection, LPCSTR pszEntry, char cDefault)
{
    return (char)GetDWordInt(pszSection, pszEntry, cDefault);
}
BYTE CWinRegistry::GetByte(LPCSTR pszSection, LPCSTR pszEntry, BYTE cDefault)
{
    return (BYTE)GetDWordInt(pszSection, pszEntry, cDefault);
}
short CWinRegistry::GetShort(LPCSTR pszSection, LPCSTR pszEntry, short wDefault)
{
    return (short)GetDWordInt(pszSection, pszEntry, wDefault);
}
WORD CWinRegistry::GetWord(LPCSTR pszSection, LPCSTR pszEntry, WORD wDefault)
{
    return (WORD)GetDWordInt(pszSection, pszEntry, wDefault);
}
long CWinRegistry::GetLong(LPCSTR pszSection, LPCSTR pszEntry, long dwDefault)
{
    return (long)GetDWordInt(pszSection, pszEntry, dwDefault);
}
DWORD CWinRegistry::GetDWord(LPCSTR pszSection, LPCSTR pszEntry, DWORD dwDefault)
{
    return GetDWordInt(pszSection, pszEntry, dwDefault);
}
int CWinRegistry::GetInt(LPCSTR pszSection, LPCSTR pszEntry, int nDefault)
{
    return (int)GetDWordInt(pszSection, pszEntry, nDefault);
}
UINT CWinRegistry::GetUInt(LPCSTR pszSection, LPCSTR pszEntry, UINT nDefault)
{
    return (UINT)GetDWordInt(pszSection, pszEntry, nDefault);
}
// simply fp data read functions
float CWinRegistry::GetFloat(LPCSTR pszSection, LPCSTR pszEntry, float fpDefault)
{
    float fpRet;
    GetBinInt(pszSection, pszEntry, &fpDefault, sizeof(float), &fpRet);
    return fpRet;
}
double CWinRegistry::GetDouble(LPCSTR pszSection, LPCSTR pszEntry, double fpDefault)
{
    double fpRet;
    GetBinInt(pszSection, pszEntry, &fpDefault, sizeof(double), &fpRet);
    return fpRet;
}
// simply string data read functions
CString CWinRegistry::GetString(LPCSTR pszSection, LPCSTR pszEntry, CString szDefault)
{
    return GetStringInt(pszSection, pszEntry, szDefault);
}
// complex data read functions
CPoint CWinRegistry::GetCPoint(LPCSTR pszSection, LPCSTR pszEntry, CPoint ptDefault)
{
    CPoint ptRet;
    GetBinInt(pszSection, pszEntry, &ptDefault, sizeof(CPoint), &ptRet);
    return ptRet;
}
CRect CWinRegistry::GetCRect(LPCSTR pszSection, LPCSTR pszEntry, CRect rcDefault)
{
    CRect rcRet;
    GetBinInt(pszSection, pszEntry, &rcDefault, sizeof(CRect), &rcRet);
    return rcRet;
}
CSize CWinRegistry::GetCSize(LPCSTR pszSection, LPCSTR pszEntry, CSize siDefault)
{
    CSize siRet;
    GetBinInt(pszSection, pszEntry, &siDefault, sizeof(CSize), &siRet);
    return siRet;
}

//------------------------------ get sections and entries
//--------------------------------------------------
BOOL CWinRegistry::GetSections(CStringArray &sections)
{
    sections.SetSize(0);
    if (hBaseKey == NULL)
        return FALSE;

    char buffer[256];
    DWORD dwBufSize = 256;
    FILETIME ftLastWriteTime;
    for (DWORD dwIndex = 0; ERROR_SUCCESS == RegEnumKeyEx(hBaseKey, dwIndex, buffer, &dwBufSize,
                                                          NULL, NULL, NULL, &ftLastWriteTime);
         dwIndex++)
    {
        dwBufSize = 256;
        sections.Add(CString(LPCTSTR(buffer), dwBufSize));
    }

    return TRUE;
}

BOOL CWinRegistry::GetEntries(LPCSTR pszSection, CStringArray &entries)
{
    entries.SetSize(0);
    if (hBaseKey == NULL)
        return FALSE;

    HKEY hSection;
    if (!OpenKey(hBaseKey, pszSection, &hSection))
        return FALSE;

    char buffer[256];
    DWORD dwBufSize = 256;
    for (DWORD dwIndex = 0; ERROR_SUCCESS == RegEnumValue(hSection, dwIndex, buffer, &dwBufSize,
                                                          NULL, NULL, NULL, NULL);
         dwIndex++)
    {
        dwBufSize = 256;
        entries.Add(CString(LPCTSTR(buffer), dwBufSize));
    }

    RegCloseKey(hSection);
    return TRUE;
}

//---------------------------- delete sections and entries
//-------------------------------------------------

BOOL CWinRegistry::DeleteSection(LPCSTR pszSection)
{
    if (hBaseKey == NULL)
        return FALSE;
    return ERROR_SUCCESS == RegDeleteKey(hBaseKey, pszSection);
}

BOOL CWinRegistry::DeleteEntry(LPCSTR pszSection, LPCSTR pszEntry)
{
    if (hBaseKey == NULL)
        return FALSE;

    HKEY hSection;
    if (!OpenKey(hBaseKey, pszSection, &hSection))
        return FALSE;

    BOOL bRv = (ERROR_SUCCESS == RegDeleteValue(hSection, pszEntry));

    RegCloseKey(hSection);

    return bRv;
}
