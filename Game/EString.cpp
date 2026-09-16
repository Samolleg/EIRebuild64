// EString.cpp: implementation of the EString class.
//
/////////////////////////////////////////////////////////////////////////////////////////////////
#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "EString.h"
/////////////////////////////////////////////////////////////////////////////////////////////////
// Construction/Destruction
/////////////////////////////////////////////////////////////////////////////////////////////////
LPCSTR EStringIterator::FirstLexem()
{
    m_nPos = 0;
    return NextLexem();
}
/////////////////////////////////////////////////////////////////////////////////////////////////
LPCSTR EStringIterator::NextLexem()
{
    if (m_nPos == m_szString.GetLength())
        m_nPos = -1;

    if (m_nPos == -1)
        return 0;

    int nPos = m_szString.Find(m_pszSeparator, m_nPos);
    if (nPos == -1)
    {
        m_szLexem = m_szString.Mid(m_nPos);
        m_nPos = m_szString.GetLength();
    }
    else
    {
        m_szLexem = m_szString.Mid(m_nPos, nPos - m_nPos);
        m_nPos = nPos + strlen(m_pszSeparator);
    }
    return *this;
}
/////////////////////////////////////////////////////////////////////////////////////////////////
void EStringIterator::ReplaceLexem(const char *pszReplace)
{
    //	m_szString = m_szString.Left( ;
}
/////////////////////////////////////////////////////////////////////////////////////////////////
void EString::ReplaceAll(const char *pszEpxr, const char *pszReplace)
{
    for (int nPos = 0; nPos != -1;)
    {
        nPos = Find(pszEpxr, nPos);
        if (nPos != -1)
        {
            operator()(nPos, nPos + strlen(pszEpxr)) = pszReplace;
            nPos += strlen(pszReplace);
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////////////////
EString &EString::operator<<(const float fValue)
{
    CString szTemp;
    szTemp.Format("%f", fValue);
    operator+=(szTemp);
    return *this;
}
/////////////////////////////////////////////////////////////////////////////////////////////////
EString &EString::operator<<(const int nValue)
{
    CString szTemp;
    szTemp.Format("%i", nValue);
    operator+=(szTemp);
    return *this;
}
/////////////////////////////////////////////////////////////////////////////////////////////////
ESubString EString::GetFindSubString(const char *pszFirstFind, const char *pszSecondFind,
                                     int nStartPos)
{
    int nFirst = -1, nLast = -1;

    if (pszFirstFind)
    {
        nFirst = Find(pszFirstFind, nStartPos);
        if (nFirst == -1)
            return ESubString(*this, nStartPos, nStartPos);
        else
            nFirst += strlen(pszFirstFind);
    }
    else
        nFirst = nStartPos;

    if (pszSecondFind)
    {
        nLast = Find(pszSecondFind, nFirst);
        if (nLast == -1)
            return ESubString(*this, GetLength(), GetLength());
    }
    else
        nLast = GetLength();

    return ESubString(*this, nFirst, nLast);
}
/////////////////////////////////////////////////////////////////////////////////////////////////
void EString::LoadFromFile(CFile &file)
{
    file.SeekToBegin();
    file.Read(GetBufferSetLength(file.GetLength()), file.GetLength());
    ReleaseBuffer();
}
/////////////////////////////////////////////////////////////////////////////////////////////////
void EString::SaveToFile(CFile &file)
{
    file.Write(GetBuffer(0), GetLength());
    ReleaseBuffer();
}
/////////////////////////////////////////////////////////////////////////////////////////////////
void EString::DeleteSlashR()
{
    Remove('\r');
    /*	for( int i = 0; i < GetLength(); )
                    if ( GetAt(i) == '\r' )
                            Delete(i);
                    else
                            ++i;
    */
}
/////////////////////////////////////////////////////////////////////////////////////////////////
