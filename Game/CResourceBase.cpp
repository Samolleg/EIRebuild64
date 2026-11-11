#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "CResourceBase.h"
#include "Notification.h"
#include <array>
#include <string>
#include <stdexcept>
// --------------------------------------------------------------------------------------------
CResourceBase::CResourceBase()
{
    memset(&header, 0, sizeof(header));
    pHashTable = NULL;
    pNamePool = NULL;
    pdwAccelTable = NULL;
    nAccelTableSize = 0;
#ifdef TRY_OPEN_EXTERNAL_RESOURCES
    pAccelTable = NULL;
    nAccelNum = 0;
#endif
}
CResourceBase::~CResourceBase()
{
    memset(&header, 0, sizeof(header));
    pHashTable = NULL;
    pNamePool = NULL;
    FreeAccelTable();
}
// --------------------------------------------------------------------------------------------
bool CResourceBase::SetResourceLayout(std::uint64_t bytes, bool mapped)
{
    resourceBytes = namePoolBytes = 0;
    if (bytes < sizeof(header) || header.dwSignature != SRESHEADER_SIGNATURE)
        return false;
    const std::uint64_t tableBytes = std::uint64_t(header.dwEntryCount) * sizeof(SResHashEntry);
    if (header.dwHashTableOffset > bytes || tableBytes > bytes - header.dwHashTableOffset)
        return false;
    const auto available = bytes - header.dwHashTableOffset - tableBytes;
    // Nested/mapped archives historically use the remainder of the container
    // for names. Buffered archives own exactly the declared name pool.
    if (!mapped && header.dwNameDataSize > available)
        return false;
    resourceBytes = bytes;
    namePoolBytes = mapped ? available : header.dwNameDataSize;
    return true;
}
bool CResourceBase::BindResourceMemory(const void* address, std::size_t bytes)
{
    if (!address || bytes < sizeof(header)) return false;
    memcpy(&header, address, sizeof(header));
    if (!SetResourceLayout(bytes, true)) return false;
    auto* base = static_cast<const BYTE*>(address);
    pHashTable = reinterpret_cast<SResHashEntry*>(const_cast<BYTE*>(base + header.dwHashTableOffset));
    pNamePool = const_cast<BYTE*>(base + header.dwHashTableOffset +
        std::size_t(header.dwEntryCount) * sizeof(SResHashEntry));
    return true;
}
bool CResourceBase::EntryAvailable(DWORD index) const
{
    if (!pHashTable || !pNamePool || index >= header.dwEntryCount) return false;
    const auto& entry = pHashTable[index];
    if (!entry.wNameLen || entry.dwOffset > resourceBytes ||
        entry.dwSize > resourceBytes - entry.dwOffset ||
        entry.dwNameOffset > namePoolBytes || entry.wNameLen > namePoolBytes - entry.dwNameOffset)
        return false;
    return memchr(pNamePool + entry.dwNameOffset, 0, entry.wNameLen) == nullptr;
}
static DWORD GetHashValueByName(LPCSTR name, DWORD base, std::size_t& length)
{
    DWORD sum = 0;
    length = 0;
    for (auto* p = reinterpret_cast<const unsigned char*>(name); *p; ++p) {
        sum += tolower(*p);
        ++length;
    }
    return sum % base;
}
DWORD CResourceBase::GetIndexOfName(LPCSTR name)
{
    if (!name || !pHashTable || !pNamePool || !header.dwEntryCount) return DWORD(-1);
    std::size_t length = 0;
    DWORD index = GetHashValueByName(name, header.dwEntryCount, length);
    // Bad/dead records cannot match, but a valid successor remains usable.
    // The table-size bound also terminates cyclic collision chains.
    for (DWORD steps = 0; steps < header.dwEntryCount && index < header.dwEntryCount; ++steps) {
        const auto& entry = pHashTable[index];
        if (EntryAvailable(index) && length == entry.wNameLen &&
            _strnicmp(name, reinterpret_cast<const char*>(pNamePool + entry.dwNameOffset), length) == 0)
            return index;
        index = entry.dwNextIndex;
    }
    return DWORD(-1);
}
DWORD CResourceBase::GetIndexOfID(int id)
{
    if (id < 0 || id >= nAccelTableSize || !pdwAccelTable) return DWORD(-1);
    const DWORD index = pdwAccelTable[id];
    return EntryAvailable(index) ? index : DWORD(-1);
}

// --------------------------------------------------------------------------------------------
// Creates accel table, returns FALSE if index out of range
#ifdef TRY_OPEN_EXTERNAL_RESOURCES
BOOL CResourceBase::CreateAccelTable(SResToID *_pAccelTable, int _nAccelNum)
{
    FreeAccelTable();
    pAccelTable = _pAccelTable;
    nAccelNum = _nAccelNum;
#else
BOOL CResourceBase::CreateAccelTable(SResToID *pAccelTable, int nAccelNum)
{
    FreeAccelTable();
#endif
    if (!pAccelTable)
        return TRUE;
    // Get max index of accel table
    int nMaxID = 0, i;
    for (i = 0; i < nAccelNum; i++)
        if (pAccelTable[i].nResourceID > nMaxID)
            nMaxID = pAccelTable[i].nResourceID;
    if (DWORD(nMaxID) > header.dwEntryCount)
    {
        ReportWarning("CResourceBase::CreateAccelTable().\nIndex out of range.\n");
        return FALSE;
    }
    nAccelTableSize = nMaxID + 1;
    pdwAccelTable = dbgnew DWORD[nAccelTableSize];
    if (!pdwAccelTable)
    {
        ReportWarning("CResourceBase::CreateAccelTable().\nNo enough memory.\n");
        nAccelTableSize = 0;
        return FALSE;
    }
    memset(pdwAccelTable, 0xFF, nAccelTableSize * sizeof(DWORD));
    for (i = 0; i < nAccelNum; i++)
    {
        pdwAccelTable[pAccelTable[i].nResourceID] = GetIndexOfName(pAccelTable[i].szResourceName);
    }
    return TRUE;
}

void CResourceBase::FreeAccelTable()
{
    if (pdwAccelTable)
    {
        delete[] pdwAccelTable;
        pdwAccelTable = NULL;
        nAccelTableSize = 0;
    }
#ifdef TRY_OPEN_EXTERNAL_RESOURCES
    pAccelTable = NULL;
    nAccelNum = 0;
#endif
}

// ----------------------- Finds name of resource by it ID -----------------------------------
#ifdef TRY_OPEN_EXTERNAL_RESOURCES
LPCSTR CResourceBase::FindNameByID(int nResourceID)
{
    for (int i = 0; i < nAccelNum; i++)
        if (pAccelTable[i].nResourceID == nResourceID)
            return pAccelTable[i].szResourceName;
    return NULL;
}
#endif

// -------------------------------- File list ------------------------------------------------

static BOOL strsimilar(LPCSTR pszName, LPCSTR pszMask, BOOL bRetIfMaskEmpty)
{
    if (*pszMask == 0)
        if (*pszName == 0)
            return TRUE;
        else
            return bRetIfMaskEmpty;
    for (; *pszName && *pszMask; pszName++, pszMask++)
    {
        if (*pszMask == '*')
            return TRUE;
        if (*pszMask != '?' && toupper(*pszName) != toupper(*pszMask))
            return FALSE;
    }
    for (; *pszMask; pszMask++)
        if (*pszMask != '?' && *pszMask != '*')
            return FALSE;
    if (*pszName)
        return FALSE;
    return TRUE;
}

static BOOL FileNameSimilar(LPCSTR name, LPCSTR mask)
{
    auto split = [](const char* value) {
        std::array<std::string,4> parts;
        for (auto& part : parts) part.resize(strlen(value) + 1, '\0');
        if (_splitpath_s(value, parts[0].data(), parts[0].size(), parts[1].data(), parts[1].size(),
                         parts[2].data(), parts[2].size(), parts[3].data(), parts[3].size()) != 0)
            throw std::invalid_argument("Invalid resource name/mask");
        return parts;
    };
    const auto names = split(name), masks = split(mask);
    return strsimilar(names[0].c_str(), masks[0].c_str(), TRUE) &&
           strsimilar(names[1].c_str(), masks[1].c_str(), TRUE) &&
           strsimilar(names[2].c_str(), masks[2].c_str(), FALSE) &&
           strsimilar(names[3].c_str(), masks[3].c_str(), TRUE);
}

void CResourceBase::GetFileList(CStringArray &aNames, LPCSTR pszMask)
{
    aNames.RemoveAll();
    for (DWORD i = 0; i < header.dwEntryCount; ++i)
    {
        if (!EntryAvailable(i)) continue;
        const auto& entry = pHashTable[i];
        const CString name(reinterpret_cast<const char*>(pNamePool + entry.dwNameOffset), entry.wNameLen);
        if (pszMask == NULL || FileNameSimilar(name, pszMask))
            aNames.Add(name);
    }
}
