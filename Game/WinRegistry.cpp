#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "Notification.h"
#include "WinRegistry.h"
#include "SettingsProfile.h"
/////////////////////////////////////////////////////////////////////////////////////

// Legacy typed interface backed exclusively by the ReStarter XML provider.
CWinRegistry::CWinRegistry() = default;
CWinRegistry::~CWinRegistry() { Close(); }
BOOL CWinRegistry::Open(LPCSTR key, HKEY) {
    SettingsProfile::Initialize();
    baseSection = SettingsProfile::Ansi(SettingsProfile::Section(SettingsProfile::Wide(key))).c_str();
    opened = true; return TRUE;
}
void CWinRegistry::Close() { opened = false; baseSection.Empty(); }
std::wstring CWinRegistry::ProfileSection(LPCSTR section) const {
    auto base = SettingsProfile::Wide(baseSection);
    auto child = SettingsProfile::Wide(section);
    return base.empty() ? child : child.empty() ? base : base + L"\\" + child;
}
BOOL CWinRegistry::SetDWordInt(LPCSTR section,LPCSTR name,DWORD value) {
    return opened && SettingsProfile::SetDword(ProfileSection(section),SettingsProfile::Wide(name),value);
}
BOOL CWinRegistry::SetStringInt(LPCSTR section,LPCSTR name,CString value) {
    auto text=SettingsProfile::Wide(value); std::vector<char> bytes((text.size()+1)*2);
    memcpy(bytes.data(),text.c_str(),bytes.size());
    return opened && SettingsProfile::SetRaw(ProfileSection(section),SettingsProfile::Wide(name),bytes,REG_SZ);
}
BOOL CWinRegistry::SetBinInt(LPCSTR section,LPCSTR name,LPVOID data,int length) {
    if(!opened || length<0 || (length && !data)) return FALSE;
    std::vector<char> bytes(length); if(length) memcpy(bytes.data(),data,length);
    return SettingsProfile::SetRaw(ProfileSection(section),SettingsProfile::Wide(name),bytes,REG_BINARY);
}
DWORD CWinRegistry::GetDWordInt(LPCSTR section,LPCSTR name,DWORD fallback) {
    return opened ? SettingsProfile::Dword(ProfileSection(section),SettingsProfile::Wide(name),fallback) : fallback;
}
CString CWinRegistry::GetStringInt(LPCSTR section,LPCSTR name,CString fallback) {
    if(!opened) return fallback;
    std::vector<char> data; uint32_t type=0;
    if(!SettingsProfile::GetRaw(ProfileSection(section),SettingsProfile::Wide(name),data,type) || type!=REG_SZ) return fallback;
    return SettingsProfile::Ansi(SettingsProfile::String(ProfileSection(section),SettingsProfile::Wide(name))).c_str();
}
void CWinRegistry::GetBinInt(LPCSTR section,LPCSTR name,LPVOID fallback,int length,LPVOID buffer) {
    std::vector<char> data; uint32_t type=0;
    if(opened && SettingsProfile::GetRaw(ProfileSection(section),SettingsProfile::Wide(name),data,type) &&
       type==REG_BINARY && data.size()==static_cast<size_t>(length)) memcpy(buffer,data.data(),length);
    else memcpy(buffer,fallback,length);
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
BOOL CWinRegistry::GetSections(CStringArray& sections) {
    sections.RemoveAll(); if(!opened) return FALSE;
    for(const auto& value:SettingsProfile::Sections(SettingsProfile::Wide(baseSection))) sections.Add(SettingsProfile::Ansi(value).c_str());
    return TRUE;
}
BOOL CWinRegistry::GetEntries(LPCSTR section,CStringArray& entries) {
    entries.RemoveAll(); if(!opened) return FALSE;
    const auto values=SettingsProfile::Entries(ProfileSection(section));
    if(values.empty()) return FALSE;
    for(const auto& value:values) entries.Add(SettingsProfile::Ansi(value).c_str());
    return TRUE;
}
BOOL CWinRegistry::DeleteSection(LPCSTR section) {
    return opened && SettingsProfile::Delete(ProfileSection(section),nullptr);
}
BOOL CWinRegistry::DeleteEntry(LPCSTR section,LPCSTR name) {
    auto entry=SettingsProfile::Wide(name);
    return opened && SettingsProfile::Delete(ProfileSection(section),&entry);
}
