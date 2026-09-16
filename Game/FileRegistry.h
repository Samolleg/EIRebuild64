#pragma once

#include "SimplyArray.h"
#include "Win32Classes.h"
/////////////////////////////////////////////////////////////////////////////////////
class CFileRegistry
{
public:
    CFileRegistry();  // default construcor
    ~CFileRegistry(); // destructor
    // open/close file/registr to read
    BOOL Open(LPCSTR pszFileName); // open 'reg'-file
    BOOL Open(LPVOID pFileData);   // open 'reg'-file already loaded to RAM
    void Close();                  // close one
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
    CString GetString(LPCSTR pszSection, LPCSTR pszEntry, CString szDefault);
    void GetString(LPCSTR pszSection, LPCSTR pszEntry, LPSTR pBuf, int &nBufLen, LPCSTR pszDefault);
    // complex array data read functions
    BOOL GetCharArray(LPCSTR pszSection, LPCSTR pszEntry, CCharArray &chars);
    BOOL GetByteArray(LPCSTR pszSection, LPCSTR pszEntry, CByteArray &bytes);
    BOOL GetShortArray(LPCSTR pszSection, LPCSTR pszEntry, CShortArray &shorts);
    BOOL GetWordArray(LPCSTR pszSection, LPCSTR pszEntry, CWordArray &words);
    BOOL GetLongArray(LPCSTR pszSection, LPCSTR pszEntry, CLongArray &longs);
    BOOL GetDWordArray(LPCSTR pszSection, LPCSTR pszEntry, CDWordArray &dwords);
    BOOL GetIntArray(LPCSTR pszSection, LPCSTR pszEntry, CIntArray &ints);
    BOOL GetUIntArray(LPCSTR pszSection, LPCSTR pszEntry, CUIntArray &uints);
    BOOL GetFloatArray(LPCSTR pszSection, LPCSTR pszEntry, CFloatArray &floats);
    BOOL GetDoubleArray(LPCSTR pszSection, LPCSTR pszEntry, CDoubleArray &doubles);
    BOOL GetStringArray(LPCSTR pszSection, LPCSTR pszEntry, CStringArray &strings);
    // complex data read functions
    CPoint GetCPoint(LPCSTR pszSection, LPCSTR pszEntry, CPoint ptDefault);
    CRect GetCRect(LPCSTR pszSection, LPCSTR pszEntry, CRect rcDefault);
    CSize GetCSize(LPCSTR pszSection, LPCSTR pszEntry, CSize siDefault);
    // get sections and entries
    BOOL GetSections(CStringArray &sections);
    BOOL GetEntries(LPCSTR pszSection, CStringArray &entries);

    // private :
    BOOL bIsInRam;           // TRUE if file loaded externally
    CMappedFile cMappedFile; // Object for file mapping
    LPBYTE pBaseAddress;     // Starting address of loaded file
    LPBYTE FindData(LPCSTR pszSection, LPCSTR pszEntry, BYTE cType, BOOL *pbIsSignle = NULL);
    BOOL IsValid();
};
