#pragma once
// EString.h: interface for the EString class.
//
//////////////////////////////////////////////////////////////////////

// class EStringConstIterator;

class ESubString
{
    int m_nFirst, m_nLast;
    CString &m_szString;
    CString m_szSubString;

public:
    ESubString(CString &szString, int nFirst, int nLast)
        : m_szString(szString)
        , m_nFirst(nFirst)
        , m_nLast(nLast)
    {
    }

    ESubString &operator=(const char *pszStr)
    {
        m_szString = m_szString.Left(m_nFirst) + pszStr + m_szString.Mid(m_nLast);
        return *this;
    }
    operator LPCSTR() { return m_szSubString = m_szString.Mid(m_nFirst, m_nLast - m_nFirst); }
};

class EString : public CString
{
public:
    ESubString GetFindSubString(const char *pszFirstFind, const char *pszSecondFind,
                                int nStartPos = 0);
    ESubString operator()(const int nFirst, const int nLast)
    {
        return ESubString(*this, nFirst, nLast);
    }

    EString &operator=(const char *pszStr)
    {
        CString::operator=(pszStr);
        return *this;
    }
    EString() = default;
    EString(const char *pszStr) { *this = pszStr; }

    void LoadFromFile(CFile &file);
    void SaveToFile(CFile &file);
    void DeleteSlashR();

    bool StartFrom(const char *pszText) const { return Find(pszText) == 0; }
    void ReplaceAll(const char *pszEpxr, const char *pszReplace);

    EString &operator<<(const char *pszText)
    {
        operator+=(pszText);
        return *this;
    }
    EString &operator<<(const float fValue);
    EString &operator<<(const int nValue);
    EString &operator<<(const char cValue)
    {
        operator+=(cValue);
        return *this;
    }
};

class EStringIterator
{
    int m_nPos;
    const CString &m_szString;
    const char *m_pszSeparator;
    CString m_szLexem;

public:
    EStringIterator(const CString &szSource, const char *pszSeparator, const int nPos = 0)
        : m_szString(szSource)
        , m_pszSeparator(pszSeparator)
        , m_nPos(nPos)
    {
        FirstLexem();
    }

    LPCSTR NextLexem();
    LPCSTR FirstLexem();
    void ReplaceLexem(const char *pszReplace);

    float Value() { return atof(m_szLexem); }
    int IntValue() { return atoi(m_szLexem); }
    operator LPCSTR() const
    {
        if (m_nPos == -1)
            return 0;
        else
            return m_szLexem;
    }
};

typedef EStringIterator EStringConstIterator;
