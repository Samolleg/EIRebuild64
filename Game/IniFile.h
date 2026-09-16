#pragma once

#ifndef __SIMPLE_INIFILE__
#include "mm_malloc.h"
#include "SimplyArray.h"
#endif // __SIMPLE_INIFILE__

// --------------------------------------------------------------------------------------------
//
// --------------------------------------------------------------------------------------------
class CIniFile
{
private:
    CString szIniFileName; // name of ini-file to read from
public:
    CIniFile() = default;
    CIniFile(LPCSTR pszIniFileName);
    ~CIniFile() = default;
    // open/close to read/write
    BOOL Open(LPCSTR pszIniFileName); // open ini-file
    void Close();                     // close one
    // get sections and entries
    BOOL GetSections(CStringArray &sections);
    BOOL GetEntries(LPCSTR pszSection, CStringArray &entries);
    // Read functions
    // simply integer data read functions
    char GetChar(LPCSTR pszSection, LPCSTR pszEntry, char cDefault);
    BYTE GetByte(LPCSTR pszSection, LPCSTR pszEntry, BYTE cDefault);
    short GetShort(LPCSTR pszSection, LPCSTR pszEntry, short wDefault);
    WORD GetWord(LPCSTR pszSection, LPCSTR pszEntry, WORD wDefault);
    long GetLong(LPCSTR pszSection, LPCSTR pszEntry, long dwDefault);
    DWORD GetDWord(LPCSTR pszSection, LPCSTR pszEntry, DWORD dwDefault);
    int GetInt(LPCSTR pszSection, LPCSTR pszEntry, int nDefault);
    UINT GetUInt(LPCSTR pszSection, LPCSTR pszEntry, UINT nDefault);
    // simply fp data read functions
    float GetFloat(LPCSTR pszSection, LPCSTR pszEntry, float fpDefault);
    double GetDouble(LPCSTR pszSection, LPCSTR pszEntry, double fpDefault);
    // simply string data read functions
    CString GetString(LPCSTR pszSection, LPCSTR pszEntry, LPCSTR pszDefault);
    // array functions

#ifndef __SIMPLE_INIFILE__
    BOOL GetCharArray(LPCSTR pszSection, LPCSTR pszEntry, CCharArray &array);
    BOOL GetShortArray(LPCSTR pszSection, LPCSTR pszEntry, CShortArray &array);
    BOOL GetLongArray(LPCSTR pszSection, LPCSTR pszEntry, CLongArray &array);
    BOOL GetIntArray(LPCSTR pszSection, LPCSTR pszEntry, CIntArray &array);
#endif //__SIMPLE_INIFILE__

    BOOL GetByteArray(LPCSTR pszSection, LPCSTR pszEntry, CByteArray &array);
    BOOL GetWordArray(LPCSTR pszSection, LPCSTR pszEntry, CWordArray &array);
    BOOL GetDWordArray(LPCSTR pszSection, LPCSTR pszEntry, CDWordArray &array);
    BOOL GetUIntArray(LPCSTR pszSection, LPCSTR pszEntry, CUIntArray &array);
    BOOL GetStringArray(LPCSTR pszSection, LPCSTR pszEntry, CStringArray &szArray,
                        const char cSeperator = ',');
    // Write functions
    // simply integer data write functions
    BOOL SetChar(LPCSTR pszSection, LPCSTR pszEntry, char cData);
    BOOL SetByte(LPCSTR pszSection, LPCSTR pszEntry, BYTE cData);
    BOOL SetShort(LPCSTR pszSection, LPCSTR pszEntry, short wData);
    BOOL SetWord(LPCSTR pszSection, LPCSTR pszEntry, WORD wData);
    BOOL SetLong(LPCSTR pszSection, LPCSTR pszEntry, long dwData);
    BOOL SetDWord(LPCSTR pszSection, LPCSTR pszEntry, DWORD dwData);
    BOOL SetInt(LPCSTR pszSection, LPCSTR pszEntry, int nData);
    BOOL SetUInt(LPCSTR pszSection, LPCSTR pszEntry, UINT nData);
    // simply fp data write functions
    BOOL SetFloat(LPCSTR pszSection, LPCSTR pszEntry, float fpData);
    BOOL SetDouble(LPCSTR pszSection, LPCSTR pszEntry, double fpData);
    // simply string data write functions
    BOOL SetString(LPCSTR pszSection, LPCSTR pszEntry, CString szData);
    // array functions
    BOOL SetStringArray(LPCSTR pszSection, LPCSTR pszEntry, CStringArray &array,
                        const char cSeperator = ',');
};
