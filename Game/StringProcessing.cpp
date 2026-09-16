#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "StringProcessing.h"

#include <unordered_map>
/////////////////////////////////////////////////////////////////////////////////////
namespace NStrProc {
std::unordered_map<char, char> brackets;
char szBrackets[8];
const DWORD NUM_BRACKETS = 5;
//
void Init();
void SplitStringWithBrackets(const std::string &szString, std::vector<std::string> &szVector,
                             const char cSeparator);
}; // namespace NStrProc
/////////////////////////////////////////////////////////////////////////////////////
void SaveString(const std::string &szString, FILE *file)
{
    DWORD num = szString.size() + 1;
    fwrite(&num, sizeof(num), 1, file);
    fwrite(szString.c_str(), 1, num, file);
}
/////////////////////////////////////////////////////////////////////////////////////
static char buffer[4096];
void LoadString(std::string &szString, LPBYTE &pData)
{
    DWORD num = *reinterpret_cast<DWORD *>(pData);
    pData += sizeof(DWORD);
    if (num)
    {
        memcpy(buffer, pData, num);
        buffer[num] = 0;
        szString = buffer;
    }
    pData += num;
}
/////////////////////////////////////////////////////////////////////////////////////
void SaveStringArray(const std::vector<std::string> &szStrings, FILE *file)
{
    DWORD num = szStrings.size();
    fwrite(&num, sizeof(num), 1, file);
    for (auto iter = szStrings.begin(); iter != szStrings.end(); ++iter)
        SaveString(*iter, file);
}
/////////////////////////////////////////////////////////////////////////////////////
void LoadStringArray(std::vector<std::string> &szStrings, LPBYTE &pData)
{
    DWORD num = *reinterpret_cast<DWORD *>(pData);
    pData += sizeof(DWORD);
    szStrings.resize(num);
    for (auto iter = szStrings.begin(); iter != szStrings.end(); ++iter)
        LoadString(*iter, pData);
}
/////////////////////////////////////////////////////////////////////////////////////

void SaveString(CString &szString, CFile &file)
{
    DWORD num;
    num = szString.GetLength() + 1;
    file.Write(&num, sizeof(num));
    file.Write((LPCSTR)szString, num);
}
/////////////////////////////////////////////////////////////////////////////////////
void LoadString(CString &szString, LPBYTE &lpData)
{
    DWORD num = *((DWORD *)lpData);
    lpData += sizeof(DWORD);
    if (num)
    {
        char *buffer = dbgnew char[num + 2];
        memcpy(buffer, lpData, num);
        buffer[num] = 0;
        szString = buffer;
        delete[] buffer;
    }
    else
        szString.Empty();
    lpData += num;
}
/////////////////////////////////////////////////////////////////////////////////////
void SaveStringArray(CStringArray &szStrings, CFile &file)
{
    DWORD num = szStrings.GetSize();
    file.Write(&num, sizeof(num));
    for (int i = 0; i < szStrings.GetSize(); i++)
        SaveString(szStrings[i], file);
}
/////////////////////////////////////////////////////////////////////////////////////
void LoadStringArray(CStringArray &szStrings, LPBYTE &lpData)
{
    DWORD num = *((DWORD *)lpData);
    lpData += sizeof(DWORD);
    szStrings.SetSize(num);
    for (int i = 0; i < szStrings.GetSize(); i++)
        LoadString(szStrings[i], lpData);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL NStrProc::SplitString(LPCSTR pszString, CStringArray &szStrings, const BYTE cSeparator)
{
    int pos1, pos2;
    CString szValue;
    CString szStr = pszString;
    szStrings.RemoveAll();
    // split string
    szStr += cSeparator;
    pos1 = 0;
    pos2 = szStr.Find(cSeparator);
    while (pos2 != -1)
    {
        szValue = szStr.Mid(pos1, pos2 - pos1);
        szValue.TrimLeft();
        szValue.TrimRight();
        szStrings.Add(szValue);

        pos1 = pos2 + 1;
        pos2 = szStr.Find(cSeparator, pos1);
    }

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL NStrProc::SplitStringMulti(LPCSTR pszString, CStringArray &szStrings, const BYTE cSeparator)
{
    int pos1, pos2;
    CString szValue;
    CString szStr = pszString;
    szStrings.RemoveAll();
    // split string
    szStr += cSeparator;
    pos1 = 0;
    pos2 = szStr.Find(cSeparator);
    while (pos2 != -1)
    {
        if (pos2 != pos1)
        {
            szValue = szStr.Mid(pos1, pos2 - pos1);
            szValue.TrimLeft();
            szValue.TrimRight();
            szStrings.Add(szValue);
        }
        pos1 = pos2 + 1;
        pos2 = szStr.Find(cSeparator, pos1);
    }

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL NStrProc::SplitStringBrackets(LPCSTR pszString, CStringArray &szStrings, const BYTE cSeparator,
                                   const BYTE cOpenBrckt, const BYTE cCloseBrckt)
{
    int pos1, pos2;
    CString szValue;
    CString szStr = pszString;
    szStrings.RemoveAll();
    // split string
    szStr += cSeparator;
    pos1 = 0;
    pos2 = 0;
    unsigned int iChar;
    int nCount = 0;
    LPCTSTR ptr = (LPCTSTR)szStr;
    while ((iChar = _tcsnextc(ptr)) != 0)
    {
        if (iChar == cSeparator && nCount == 0)
        {
            pos2 = ptr - (LPCTSTR)szStr;
            szValue = szStr.Mid(pos1, pos2 - pos1);
            szValue.TrimLeft();
            szValue.TrimRight();
            szStrings.Add(szValue);
            pos1 = pos2 + 1;
            ptr = _tcsinc(ptr);
            continue;
        }
        if (iChar == cOpenBrckt)
        {
            nCount++;
            ptr = _tcsinc(ptr);
            continue;
        }
        if (iChar == cCloseBrckt)
        {
            nCount--;
            ptr = _tcsinc(ptr);
            continue;
        }

        ptr = _tcsinc(ptr);
    }
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL NStrProc::SplitStringQuotes(LPCSTR pszString, CStringArray &szWords)
{
    CString szTmp, szChar;

    szWords.RemoveAll();
    unsigned int iChar;
    int nMode = 0; // 0 - skipping spaces, 1 - getting word, 2 - inside quotes
    LPCTSTR ptr = pszString;

    while ((iChar = _tcsnextc(ptr)) != 0)
    {
        if (iChar == ' ')
        {
            if (nMode == 1)
            {
                nMode = 0;
                if (!szTmp.IsEmpty())
                {
                    szWords.Add(szTmp);
                    szTmp.Empty();
                }
            }
            if (nMode != 2)
            {
                ptr = _tcsinc(ptr);
                continue;
            }
        }
        if (iChar == '"')
            nMode = (nMode == 2 ? 1 : 2);
        else if (nMode == 0)
            nMode = 1;
        if (isleadbyte(*ptr))
        {
            szChar = *ptr;
            szChar += *(ptr + 1);
        }
        else
            szChar = *ptr;
        szTmp += szChar;
        ptr = _tcsinc(ptr);
    }
    if (!szTmp.IsEmpty())
    {
        szWords.Add(szTmp);
        szTmp.Empty();
    }
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL NStrProc::ComposeString(CString &szString, CStringArray &szStrings, const BYTE cSeparator)
{
    szString = "";
    if (szStrings.GetSize() == 0)
        return TRUE;
    szString += szStrings[0];
    for (int i = 1; i < szStrings.GetSize(); i++)
    {
        szString += cSeparator;
        szString += szStrings[i];
    }
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL NStrProc::ReplaceChars(CString &szString, const BYTE cOld, const BYTE cNew)
{
    int pos1, pos2;
    CString szRet;
    pos1 = 0;
    pos2 = szString.Find(cOld);
    while (pos2 != -1)
    {
        szRet += szString.Mid(pos1, pos2 - pos1);
        szRet += cNew;
        pos1 = pos2 + 1;
        pos2 = szString.Find(cOld, pos1);
    }
    szRet += szString.Mid(pos1);
    szString = szRet;
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void NStrProc::Init()
{
    brackets['('] = ')';
    brackets['['] = ']';
    brackets['{'] = '}';
    brackets['\''] = '\'';
    brackets['\"'] = '\"';
    strcpy(szBrackets, "({[\'\" ");
}
/////////////////////////////////////////////////////////////////////////////////////
void NStrProc::SplitStringWithBrackets(const std::string &szString,
                                       std::vector<std::string> &szVector, const char cSeparator)
{
    int nPos = 0, nLastPos = 0;
    szBrackets[NUM_BRACKETS] = cSeparator;
    //
    do
    {
        // find open bracket or separator
        nPos = szString.find_first_of(szBrackets, nPos);
        // check for open bracket
        if (nPos != std::string::npos)
        {
            // if this is an open bracket, then search for close one
            if (szString[nPos] != cSeparator)
            {
                nPos = szString.find(brackets[szString[nPos]], nPos + 1);
                continue;
            }
        }
        // add string
        szVector.push_back(szString.substr(nLastPos, nPos - nLastPos));
        nPos = nLastPos = nPos + 1;
        //		nPos = nLastPos = szString.find_first_not_of( cSeparator, nPos );
        //
    } while (nPos != std::string::npos);
}
/////////////////////////////////////////////////////////////////////////////////////
