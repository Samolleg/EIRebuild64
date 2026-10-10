#pragma once
/////////////////////////////////////////////////////////////////////////////////////
class CWinRegistry
{
public:
    CWinRegistry();  // default construcor
    ~CWinRegistry(); // destructor
    // open/close registry to read/write
    BOOL Open(LPCSTR pszKeyName, HKEY hkRootKey = HKEY_LOCAL_MACHINE); // open Win95 registry
    void Close();                                                      // close one

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
    CString GetString(LPCSTR pszSection, LPCSTR pszEntry, CString szDefault);
    // complex data read functions
    CPoint GetCPoint(LPCSTR pszSection, LPCSTR pszEntry, CPoint ptDefault);
    CRect GetCRect(LPCSTR pszSection, LPCSTR pszEntry, CRect rcDefault);
    CSize GetCSize(LPCSTR pszSection, LPCSTR pszEntry, CSize siDefault);
    void GetBinInt(LPCSTR pszSection, LPCSTR pszEntry, LPVOID pDefault, int nDataLen,
                   LPVOID pBuffer);

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
    // complex data write functions
    BOOL SetCPoint(LPCSTR pszSection, LPCSTR pszEntry, CPoint ptData);
    BOOL SetCRect(LPCSTR pszSection, LPCSTR pszEntry, CRect rcData);
    BOOL SetCSize(LPCSTR pszSection, LPCSTR pszEntry, CSize siData);
    // get sections and entries
    BOOL GetSections(CStringArray &sections);
    BOOL GetEntries(LPCSTR pszSection, CStringArray &entries);
    // delete sections and entries
    BOOL DeleteSection(LPCSTR pszSection);
    BOOL DeleteEntry(LPCSTR pszSection, LPCSTR pszEntry);
    BOOL SetBinInt(LPCSTR pszSection, LPCSTR pszEntry, LPVOID pData, int nDataLen);

private:
    CString baseSection;
    bool opened = false;
    std::wstring ProfileSection(LPCSTR section) const;
    BOOL SetDWordInt(LPCSTR pszSection, LPCSTR pszEntry, DWORD dwData);
    BOOL SetStringInt(LPCSTR pszSection, LPCSTR pszEntry, CString szData);
    DWORD GetDWordInt(LPCSTR pszSection, LPCSTR pszEntry, DWORD dwDefault);
    CString GetStringInt(LPCSTR pszSection, LPCSTR pszEntry, CString szDefault);
};
/////////////////////////////////////////////////////////////////////////////////////
