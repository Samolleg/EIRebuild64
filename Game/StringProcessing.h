#pragma once
/////////////////////////////////////////////////////////////////////////////////////
// #if defined( __NOMFC__ )
#include <string>
#include <vector>
/////////////////////////////////////////////////////////////////////////////////////
void SaveString(const std::string &szString, FILE *file);
void LoadString(std::string &szString, LPBYTE &pData);
void SaveStringArray(const std::vector<std::string> &szStrings, FILE *file);
void LoadStringArray(std::vector<std::string> &szStrings, LPBYTE &pData);
// #else
void SaveString(CString &szString, CFile &file);
void LoadString(CString &szString, LPBYTE &lpData);
void SaveStringArray(CStringArray &szStrings, CFile &file);
void LoadStringArray(CStringArray &szStrings, LPBYTE &lpData);
// #endif // defined( __NOMFC__ )
/////////////////////////////////////////////////////////////////////////////////////
namespace NStrProc {
// each separator means new string
extern BOOL SplitString(LPCSTR pszString, CStringArray &szStrings, const BYTE cSeparator);
// several separators in a row are treated as one
extern BOOL SplitStringMulti(LPCSTR pszString, CStringArray &szStrings, const BYTE cSeparator);
extern BOOL SplitStringBrackets(LPCSTR pszString, CStringArray &szStrings, const BYTE cSeparator,
                                const BYTE cOpenBrckt, const BYTE cCloseBrckt);
extern BOOL SplitStringQuotes(LPCSTR pszString, CStringArray &szStrings);
extern BOOL ComposeString(CString &szString, CStringArray &szStrings, const BYTE cSeparator);
extern BOOL ReplaceChars(CString &szString, const BYTE cOld, const BYTE cNew);
extern void Init();
extern void SplitStringWithBrackets(const std::string &szString, std::vector<std::string> &szVector,
                                    const char cSeparator);
}; // namespace NStrProc
/////////////////////////////////////////////////////////////////////////////////////
