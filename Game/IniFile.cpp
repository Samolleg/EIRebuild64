#include "StdAfx.h"
#include "RuntimePaths64.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "IniFile.h"
#include "ModRuntime.h"
/////////////////////////////////////////////////////////////////////////////////////
CIniFile::CIniFile(LPCSTR pszIniFileName)
{
    Open(pszIniFileName);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CIniFile::Open(LPCSTR pszIniFileName)
{
    const auto requested = std::filesystem::absolute(pszIniFileName).lexically_normal();
    const auto config = RuntimePaths64::Root() / L"Config64";
    szIniFileName = _wcsicmp(requested.parent_path().c_str(), config.c_str()) == 0
        ? pszIniFileName : ModRuntime::ConfigPath(pszIniFileName).c_str();

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CIniFile::Close()
{
    szIniFileName.Empty();
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CIniFile::GetSections( CStringArray& sections )
// get all sections from currently open ini-file
BOOL CIniFile::GetSections(CStringArray &sections)
{
    static const int nBufferSize = 1024 * 20;
    LPTSTR pszPos1 = NULL;
    char buffer[nBufferSize];
    CString szString;
    int i, nRetLength;
    //
    pszPos1 = buffer;
    // get section names
    nRetLength = GetPrivateProfileSectionNames(buffer, nBufferSize, szIniFileName);
    //
    sections.RemoveAll();
    //
    if (buffer[0] == 0)
        return FALSE;
    // parse returned data
    for (i = 0; i < nBufferSize; i++)
    {
        if (buffer[i] == 0)
        {
            szString = pszPos1;
            sections.Add(szString);
            pszPos1 = &buffer[i + 1];
            if (buffer[i + 1] == 0)
                return TRUE;
        }
    }

    return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CIniFile::GetEntries( LPCSTR pszSection, CStringArray& entries )
// get all entries from desired section of currently open ini-file
BOOL CIniFile::GetEntries(LPCSTR pszSection, CStringArray &entries)
{
    static const int nBufferSize = 1024 * 20;
    char buffer[nBufferSize];
    int i, nRetLength, pos;
    LPTSTR pszPos1 = NULL;
    CString szString;
    //
    pszPos1 = buffer;
    // get section names
    nRetLength = GetPrivateProfileSection(pszSection, buffer, nBufferSize, szIniFileName);
    //
    entries.RemoveAll();
    //
    if (buffer[0] == 0)
        return FALSE;
    // parse returned data
    for (i = 0; i < nBufferSize; i++)
    {
        if (buffer[i] == 0)
        {
            szString = pszPos1;
            pos = szString.Find('=');
            if (pos != -1)
            {
                szString = szString.Left(pos);
                szString.TrimLeft();
                szString.TrimRight();
            }
            entries.Add(szString);
            pszPos1 = &buffer[i + 1];
            if (buffer[i + 1] == 0)
                return TRUE;
        }
    }

    return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
//                          'Get' function serie                                    //
// ******************************************************************************** //
char CIniFile::GetChar(LPCSTR pszSection, LPCSTR pszEntry, char cDefault)
{
    return (char)GetInt(pszSection, pszEntry, cDefault);
}
/////////////////////////////////////////////////////////////////////////////////////
BYTE CIniFile::GetByte(LPCSTR pszSection, LPCSTR pszEntry, BYTE cDefault)
{
    return (BYTE)GetInt(pszSection, pszEntry, cDefault);
}
/////////////////////////////////////////////////////////////////////////////////////
short CIniFile::GetShort(LPCSTR pszSection, LPCSTR pszEntry, short wDefault)
{
    return (short)GetInt(pszSection, pszEntry, wDefault);
}
/////////////////////////////////////////////////////////////////////////////////////
WORD CIniFile::GetWord(LPCSTR pszSection, LPCSTR pszEntry, WORD wDefault)
{
    return (WORD)GetInt(pszSection, pszEntry, wDefault);
}
/////////////////////////////////////////////////////////////////////////////////////
long CIniFile::GetLong(LPCSTR pszSection, LPCSTR pszEntry, long dwDefault)
{
    return (long)GetInt(pszSection, pszEntry, dwDefault);
}
/////////////////////////////////////////////////////////////////////////////////////
DWORD CIniFile::GetDWord(LPCSTR pszSection, LPCSTR pszEntry, DWORD dwDefault)
{
    return (DWORD)GetInt(pszSection, pszEntry, dwDefault);
}
/////////////////////////////////////////////////////////////////////////////////////
int CIniFile::GetInt(LPCSTR pszSection, LPCSTR pszEntry, int nDefault)
{
    return GetPrivateProfileInt(pszSection, pszEntry, nDefault, szIniFileName);
}
/////////////////////////////////////////////////////////////////////////////////////
UINT CIniFile::GetUInt(LPCSTR pszSection, LPCSTR pszEntry, UINT nDefault)
{
    return (UINT)GetInt(pszSection, pszEntry, nDefault);
}
/////////////////////////////////////////////////////////////////////////////////////
float CIniFile::GetFloat(LPCSTR pszSection, LPCSTR pszEntry, float fpDefault)
{
    return (float)GetDouble(pszSection, pszEntry, fpDefault);
}
/////////////////////////////////////////////////////////////////////////////////////
double CIniFile::GetDouble(LPCSTR pszSection, LPCSTR pszEntry, double fpDefault)
{
    CString szDefault;
    szDefault.Format("%g", fpDefault);
    CString szString = GetString(pszSection, pszEntry, szDefault);
    return atof(szString);
}
/////////////////////////////////////////////////////////////////////////////////////
CString CIniFile::GetString(LPCSTR pszSection, LPCSTR pszEntry, LPCSTR pszDefault)
{
    char buff[1024];
    GetPrivateProfileString(pszSection, pszEntry, pszDefault, buff, 1023, szIniFileName);
    return CString(buff);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CIniFile::GetStringArray(LPCSTR pszSection, LPCSTR pszEntry, CStringArray &szArray,
                              const char cSeperator)
{
    CString szString = GetString(pszSection, pszEntry, "");
    CString szValue;
    int pos1, pos2;

    if (szString.IsEmpty())
        return FALSE;
    // split string
    szString += cSeperator;
    pos1 = 0;
    pos2 = szString.Find(cSeperator);
    while (pos2 != -1)
    {
        szValue = szString.Mid(pos1, pos2 - pos1);
        szValue.TrimLeft();
        szValue.TrimRight();
        szArray.Add(szValue);
        //
        pos1 = pos2 + 1;
        pos2 = szString.Find(cSeperator, pos1);
    }
    //
    return TRUE;
}

#ifndef __SIMPLE_INIFILE__

/////////////////////////////////////////////////////////////////////////////////////
BOOL CIniFile::GetLongArray(LPCSTR pszSection, LPCSTR pszEntry, CLongArray &array)
{
    CIntArray nArray;
    if (GetIntArray(pszSection, pszEntry, nArray) == FALSE)
        return FALSE;
    array.SetSize(nArray.GetSize());
    for (int i = 0; i < nArray.GetSize(); i++)
        array[i] = long(nArray[i]);

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CIniFile::GetCharArray(LPCSTR pszSection, LPCSTR pszEntry, CCharArray &array)
{
    CIntArray nArray;
    if (GetIntArray(pszSection, pszEntry, nArray) == FALSE)
        return FALSE;
    array.SetSize(nArray.GetSize());
    for (int i = 0; i < nArray.GetSize(); i++)
        array[i] = char(nArray[i]);

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CIniFile::GetShortArray(LPCSTR pszSection, LPCSTR pszEntry, CShortArray &array)
{
    CIntArray nArray;
    if (GetIntArray(pszSection, pszEntry, nArray) == FALSE)
        return FALSE;
    array.SetSize(nArray.GetSize());
    for (int i = 0; i < nArray.GetSize(); i++)
        array[i] = short(nArray[i]);

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CIniFile::GetIntArray(LPCSTR pszSection, LPCSTR pszEntry, CIntArray &array)
{
    CStringArray szArray;
    if (GetStringArray(pszSection, pszEntry, szArray) == FALSE)
        return FALSE;
    array.SetSize(szArray.GetSize());
    for (int i = 0; i < szArray.GetSize(); i++)
        array[i] = int(atoi(szArray[i]));

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////

#endif //__SIMPLE_INIFILE__

/////////////////////////////////////////////////////////////////////////////////////
BOOL CIniFile::GetByteArray(LPCSTR pszSection, LPCSTR pszEntry, CByteArray &array)
{
    CUIntArray nArray;
    if (GetUIntArray(pszSection, pszEntry, nArray) == FALSE)
        return FALSE;
    array.SetSize(nArray.GetSize());
    for (int i = 0; i < nArray.GetSize(); i++)
        array[i] = BYTE(nArray[i]);

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CIniFile::GetWordArray(LPCSTR pszSection, LPCSTR pszEntry, CWordArray &array)
{
    CUIntArray nArray;
    if (GetUIntArray(pszSection, pszEntry, nArray) == FALSE)
        return FALSE;
    array.SetSize(nArray.GetSize());
    for (int i = 0; i < nArray.GetSize(); i++)
        array[i] = WORD(nArray[i]);

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CIniFile::GetDWordArray(LPCSTR pszSection, LPCSTR pszEntry, CDWordArray &array)
{
    CUIntArray nArray;
    if (GetUIntArray(pszSection, pszEntry, nArray) == FALSE)
        return FALSE;
    array.SetSize(nArray.GetSize());
    for (int i = 0; i < nArray.GetSize(); i++)
        array[i] = DWORD(nArray[i]);

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CIniFile::GetUIntArray(LPCSTR pszSection, LPCSTR pszEntry, CUIntArray &array)
{
    CStringArray szArray;
    if (GetStringArray(pszSection, pszEntry, szArray) == FALSE)
        return FALSE;
    array.SetSize(szArray.GetSize());
    for (int i = 0; i < szArray.GetSize(); i++)
        array[i] = UINT(atoi(szArray[i]));

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
//                          'Set' function serie                                    //
// ******************************************************************************** //
BOOL CIniFile::SetString(LPCSTR pszSection, LPCSTR pszEntry, CString szData)
{
    return WritePrivateProfileString(pszSection, pszEntry, szData, RuntimePaths64::Config(std::filesystem::path((LPCSTR)szIniFileName).filename().string().c_str()).string().c_str());
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CIniFile::SetChar(LPCSTR pszSection, LPCSTR pszEntry, char cData)
{
    return SetInt(pszSection, pszEntry, cData);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CIniFile::SetByte(LPCSTR pszSection, LPCSTR pszEntry, BYTE cData)
{
    return SetUInt(pszSection, pszEntry, cData);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CIniFile::SetShort(LPCSTR pszSection, LPCSTR pszEntry, short wData)
{
    return SetInt(pszSection, pszEntry, wData);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CIniFile::SetWord(LPCSTR pszSection, LPCSTR pszEntry, WORD wData)
{
    return SetUInt(pszSection, pszEntry, wData);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CIniFile::SetLong(LPCSTR pszSection, LPCSTR pszEntry, long dwData)
{
    return SetInt(pszSection, pszEntry, dwData);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CIniFile::SetDWord(LPCSTR pszSection, LPCSTR pszEntry, DWORD dwData)
{
    return SetInt(pszSection, pszEntry, dwData);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CIniFile::SetInt(LPCSTR pszSection, LPCSTR pszEntry, int nData)
{
    CString szData;
    szData.Format("%d", nData);
    return SetString(pszSection, pszEntry, szData);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CIniFile::SetUInt(LPCSTR pszSection, LPCSTR pszEntry, UINT nData)
{
    CString szData;
    szData.Format("%u", nData);
    return SetString(pszSection, pszEntry, szData);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CIniFile::SetFloat(LPCSTR pszSection, LPCSTR pszEntry, float fpData)
{
    CString szData;
    szData.Format("%f", fpData);
    return SetString(pszSection, pszEntry, szData);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CIniFile::SetDouble(LPCSTR pszSection, LPCSTR pszEntry, double fpData)
{
    CString szData;
    szData.Format("%lf", fpData);
    return SetString(pszSection, pszEntry, szData);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CIniFile::SetStringArray(LPCSTR pszSection, LPCSTR pszEntry, CStringArray &szArray,
                              const char cSeperator)
{
    CString szString;
    int i;

    if (szArray.GetSize() == 0)
        return FALSE;

    szString = szArray[0];
    for (i = 1; i < szArray.GetSize(); i++)
        szString += cSeperator + szArray[i];

    return SetString(pszSection, pszEntry, szString);
}
/////////////////////////////////////////////////////////////////////////////////////
