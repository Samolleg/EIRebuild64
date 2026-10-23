#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "CMappedResource.h"
#include "ModRuntime.h"
#include "ModPackage.h"
#include <set>
#include <filesystem>
#include "Notification.h"
#include <io.h>
#include <fcntl.h>
#include <atomic>
namespace { std::atomic<std::uint64_t> resourcePayloadBytes{0}; }
std::uint64_t CMappedResource::LivePayloadBytes() noexcept { return resourcePayloadBytes.load(std::memory_order_relaxed); }

/////////////////////////////////////////////////////////////////////////////////////

struct SResourceFileRecord
{
    std::string szName;
    LPBYTE pAllocatedMemory;
    DWORD dwSize;
    int nRefs;
    DWORD dwFilePos;
};

/////////////////////////////////////////////////////////////////////////////////////

CMappedResource::CMappedResource(bool raw):rawResource(raw)
{
    pBaseAddress = NULL;
    hFile = -1;
    bWriteAccess = false;
}

CMappedResource::~CMappedResource()
{
    CloseResource();
}
/////////////////////////////////////////////////////////////////////////////////////

void CMappedResource::OpenOverlays(LPCSTR name,bool write,bool mapped){
 overlays.clear();ignoreOriginal=false;if(rawResource||write||!ModRuntime::Resources())return;
 std::vector<std::string> paths;ModRuntime::Resources()->ResourcePlan(name,paths,ignoreOriginal);
 for(const auto&path:paths){if(!std::filesystem::is_regular_file(path))throw std::runtime_error("Missing mod resource: "+path);std::unique_ptr<CMappedResource> child = mapped ? std::unique_ptr<CMappedResource>(new CWin32MappedResource(true)) : std::make_unique<CMappedResource>(true);if(!child->OpenResource(path.c_str()))throw std::runtime_error("Cannot open mod resource: "+path);overlays.push_back(std::move(child));}
}
namespace {
std::vector<std::string> BriefingCandidates(LPCSTR path)
{
    if (!path)
        return {};
    // Briefing speech uses only the canonical s variant, including legacy
    // callers asking for t or an unprefixed name. Missing s speech stays
    // missing: never substitute another recording.
    if (_strnicmp(path, "briefing\\", 9) == 0)
        return {std::string("s\\") + path};
    if (_strnicmp(path, "s\\briefing\\", 11) == 0 ||
        _strnicmp(path, "t\\briefing\\", 11) == 0)
        return {std::string("s\\") + (path + 2)};
    return {path};
}
}
std::string CMappedResource::FileProvider(LPCSTR name)
{
    for (auto& child : overlays)
        if (child->IsFileExistExact(name)) return child->sourcePath;
    return !ignoreOriginal && IsFileExistExact(name) ? sourcePath : std::string();
}
LPBYTE CMappedResource::OverlayFile(LPCSTR name,LPDWORD size)
{
    for (auto& child : overlays)
        if (auto* data = child->GetFileExact(name, size))
            return data;
    return nullptr;
}
void CMappedResource::GetFileList(CStringArray&names,LPCSTR mask){
 names.RemoveAll();std::set<std::string> seen;CStringArray local;
 auto append=[&](){for(int i=0;i<local.GetSize();i++){std::string key=(LPCSTR)local[i];for(char&c:key)if(c>='A'&&c<='Z')c+=32;if(seen.insert(key).second)names.Add(local[i]);}local.RemoveAll();};
 for(auto&child:overlays){child->GetFileList(local,mask);append();}if(!ignoreOriginal){CResourceBase::GetFileList(local,mask);append();}
}
BOOL CMappedResource::OpenResource(LPCSTR pszFileName, BOOL _bWriteAccess)
{
    CloseResource();
    bWriteAccess = _bWriteAccess;
    const std::string resolvedName = (!rawResource && !_bWriteAccess)
        ? ModRuntime::MapPath(pszFileName) : std::string(pszFileName);
    sourcePath = resolvedName;
    pszFileName = resolvedName.c_str();
    OpenOverlays(pszFileName,_bWriteAccess);if(ignoreOriginal)return TRUE;

    hFile = _open(pszFileName, _O_BINARY | _O_RANDOM | (bWriteAccess ? _O_RDWR : _O_RDONLY));
    if (hFile == -1)
        return ReportError("CMappedResource::OpenResource()\nUnable to open file \"" +
                           CString(pszFileName) + "\"");
    if (_lseek(hFile, 0, SEEK_SET) == -1 || _read(hFile, &header, sizeof(header)) != sizeof(header))
    {
        CloseResource();
        return ReportError("CMappedResource::OpenResource()\nUnable to read file \"" +
                           CString(pszFileName) + "\"");
    }
    if (header.dwSignature != SRESHEADER_SIGNATURE)
    {
        ReportError("Wrong format resource file \"" + CString(pszFileName) + "\"");
        CloseResource();
        return FALSE;
    }
    int nHashTblSize = header.dwEntryCount * sizeof(SResHashEntry);
    pHashTable = dbgnew SResHashEntry[header.dwEntryCount];
    pNamePool = dbgnew BYTE[header.dwNameDataSize];
    if (_lseek(hFile, header.dwHashTableOffset, SEEK_SET) == -1 ||
        _read(hFile, pHashTable, nHashTblSize) != nHashTblSize ||
        _read(hFile, pNamePool, header.dwNameDataSize) != header.dwNameDataSize)
    {
        CloseResource();
        return ReportError("CMappedResource::OpenResource()\nUnable to read file \"" +
                           CString(pszFileName) + "\"");
    }
    return TRUE;
}

BOOL CMappedResource::OpenResourceMem(LPVOID pAddress)
{
    CloseResource();
    pBaseAddress = LPBYTE(pAddress);
    memcpy(&header, pBaseAddress, sizeof(header));
    if (header.dwSignature != SRESHEADER_SIGNATURE)
    {
        ReportError("Wrong format resource\n");
        CloseResource();
        return FALSE;
    }
    pHashTable = (SResHashEntry *)(pBaseAddress + header.dwHashTableOffset);
    pNamePool = pBaseAddress + header.dwHashTableOffset +
                sizeof(SResHashEntry) * header.dwEntryCount;
    return TRUE;
}

void CMappedResource::CloseResource()
{
    overlays.clear();ignoreOriginal=false;
    CloseAllFiles();
    if (hFile >= 0)
    {
        _close(hFile);
        hFile = -1;
    }
    if (pBaseAddress == NULL && pHashTable)
        delete[] pHashTable;
    if (pBaseAddress == NULL && pNamePool)
        delete[] pNamePool;
    pHashTable = NULL;
    pNamePool = NULL;
    pBaseAddress = NULL;
    memset(&header, 0, sizeof(header));
}

//--------------------------------------------------------------------------------------------

bool CMappedResource::IsFileExist(LPCSTR pszFileName)
{
    for (const auto& candidate : BriefingCandidates(pszFileName))
    {
        for (auto& child : overlays)
            if (child->IsFileExistExact(candidate.c_str()))
                return true;
        if (!ignoreOriginal && IsFileExistExact(candidate.c_str()))
            return true;
    }
    return false;
}
bool CMappedResource::IsFileExistExact(LPCSTR pszFileName)
{
    return pHashTable && pNamePool && GetIndexOfName(pszFileName) != DWORD(-1);
}
LPBYTE CMappedResource::GetFile(LPCSTR pszFileName, LPDWORD pnDataSize)
{
    for (const auto& candidate : BriefingCandidates(pszFileName))
    {
        if (auto* data = OverlayFile(candidate.c_str(), pnDataSize))
            return data;
        if (!ignoreOriginal)
            if (auto* data = GetFileExact(candidate.c_str(), pnDataSize))
                return data;
    }
    return nullptr;
}
LPBYTE CMappedResource::GetFileExact(LPCSTR pszFileName, LPDWORD pnDataSize)
{
    if (!pHashTable || !pNamePool)
        return NULL;

    auto pos = recByName.find(std::string(pszFileName));
    if (pos != recByName.end())
    {
        SResourceFileRecord *pRec = pos->second;
        pRec->nRefs++;
        if (pnDataSize)
            *pnDataSize = pRec->dwSize;
        return pRec->pAllocatedMemory;
    }

    DWORD dwIndex = GetIndexOfName(pszFileName);
    if (dwIndex == -1)
        return NULL;

    if (pBaseAddress)
    {
        if(pnDataSize)*pnDataSize=pHashTable[dwIndex].dwSize;
        return pBaseAddress + pHashTable[dwIndex].dwOffset;
    }

    SResourceFileRecord *pRec = dbgnew SResourceFileRecord;
    pRec->szName = std::string(pszFileName);
    pRec->nRefs = 1;
    pRec->dwSize = pHashTable[dwIndex].dwSize;
    pRec->pAllocatedMemory = dbgnew BYTE[pRec->dwSize];
    pRec->dwFilePos = pHashTable[dwIndex].dwOffset;

    if (_lseek(hFile, pRec->dwFilePos, SEEK_SET) == -1 ||
        _read(hFile, pRec->pAllocatedMemory, pRec->dwSize) != pRec->dwSize)
    {
        delete[] pRec->pAllocatedMemory;
        delete pRec;
        return NULL;
    }
    if (pnDataSize)
        *pnDataSize = pRec->dwSize;

    recByName[pRec->szName] = pRec;
    recByData[DWORD(pRec->pAllocatedMemory)] = pRec;
    resourcePayloadBytes.fetch_add(pRec->dwSize, std::memory_order_relaxed);
    return pRec->pAllocatedMemory;
}

bool CMappedResource::CloseFile(LPBYTE pFileImage)
{
    for(auto&child:overlays)if(child->CloseFile(pFileImage))return true;
    if (pBaseAddress)
        return true;

    if (!pFileImage)
        return false;

    auto pos = recByData.find(DWORD(pFileImage));
    if (pos == recByData.end())
    {
        // ReportWarning("CMappedResource::CloseFile()\nFile already closed\n");
        return false;
    }
    SResourceFileRecord *pRec = pos->second;
    if (--pRec->nRefs == 0)
    {
        if (bWriteAccess && (_lseek(hFile, pRec->dwFilePos, SEEK_SET) == -1 ||
                             _write(hFile, pRec->pAllocatedMemory, pRec->dwSize) != pRec->dwSize))
            ReportError("CMappedResource::CloseFile()\nUnable to write file \"" +
                        CString(pFileImage) + "\"");
        recByData.erase(pos);
        recByName.erase(recByName.find(pRec->szName));
        resourcePayloadBytes.fetch_sub(pRec->dwSize, std::memory_order_relaxed);
        delete[] pRec->pAllocatedMemory;
        delete pRec;
    }
    return true;
}

void CMappedResource::CloseAllFiles()
{
    for(auto&child:overlays)child->CloseAllFiles();
    if (pBaseAddress)
        return;

    for (auto pos = recByData.begin(); pos != recByData.end(); ++pos)
    {
        resourcePayloadBytes.fetch_sub(pos->second->dwSize, std::memory_order_relaxed);
        delete[] pos->second->pAllocatedMemory;
        delete pos->second;
    }
    recByData.clear();
    recByName.clear();
}
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
// **                          CWin32MappedResource
// ******************************************************************************** //
/////////////////////////////////////////////////////////////////////////////////////
//--------------------------------------------------------------------------------------------
CWin32MappedResource::CWin32MappedResource(bool raw) : CMappedResource(raw)
{
    pBaseAddress = NULL;
}
CWin32MappedResource::~CWin32MappedResource()
{
    CloseResource();
}
//--------------------------------------------------------------------------------------------
BOOL CWin32MappedResource::OpenResource(LPCSTR pszFileName, BOOL bWriteAccess)
{
    CloseResource();sourcePath=pszFileName;OpenOverlays(pszFileName,bWriteAccess,true);if(ignoreOriginal)return TRUE;
    if (!cMappedFile.Open(pszFileName, bWriteAccess))
        return FALSE;
    if (OpenResourceMem((LPBYTE)cMappedFile.GetAddress()))
        return TRUE;
    CloseResource();
    return FALSE;
}

BOOL CWin32MappedResource::OpenResourceMem(LPVOID pAddress)
{
    pBaseAddress = (LPBYTE)pAddress;
    memcpy(&header, pBaseAddress, sizeof(header));
    if (header.dwSignature != SRESHEADER_SIGNATURE)
    {
        ReportError("Wrong format resource\n");
        CloseResource();
        return FALSE;
    }
    pHashTable = (SResHashEntry *)(pBaseAddress + header.dwHashTableOffset);
    pNamePool = pBaseAddress + header.dwHashTableOffset +
                sizeof(SResHashEntry) * header.dwEntryCount;
    return TRUE;
}

void CWin32MappedResource::CloseResource()
{
    overlays.clear();ignoreOriginal=false;
    cMappedFile.Close();
    pHashTable = NULL;
    pNamePool = NULL;
    pBaseAddress = NULL;
    memset(&header, 0, sizeof(header));
}

void CWin32MappedResource::Flush()
{
    cMappedFile.Flush();
}

//--------------------------------------------------------------------------------------------
// Ищет требуемый ресурс и возвращает его координаты
// NULL если не найден, если размер ресурса не требуется,
// можно оставить равным NULL
LPBYTE CWin32MappedResource::GetFile(LPCSTR pszFileName, LPDWORD pnDataSize)
{
    for (const auto& candidate : BriefingCandidates(pszFileName))
    {
        if (auto* data = OverlayFile(candidate.c_str(), pnDataSize))
            return data;
        if (!ignoreOriginal)
            if (auto* data = GetFileExact(candidate.c_str(), pnDataSize))
                return data;
    }
    return nullptr;
}
LPBYTE CWin32MappedResource::GetFileExact(LPCSTR pszFileName, LPDWORD pnDataSize)
{
    if (!pHashTable || !pNamePool)
        return NULL;
    DWORD dwIndex = GetIndexOfName(pszFileName);
    if (dwIndex == -1)
        return NULL;
    if (pnDataSize)
        *pnDataSize = pHashTable[dwIndex].dwSize;
    return pBaseAddress + pHashTable[dwIndex].dwOffset;
}

bool CWin32MappedResource::CloseFile(LPBYTE pFileImage)
{
    for(auto&child:overlays)if(child->CloseFile(pFileImage))return true;
    return true;
}
void CWin32MappedResource::CloseAllFiles()
{
    for(auto&child:overlays)child->CloseAllFiles();
}
