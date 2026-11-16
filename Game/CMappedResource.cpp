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
#include "GameJournal.h"
#include <shlwapi.h>
#include <fstream>
#pragma comment(lib,"shlwapi.lib")
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
 for (const auto& path : paths) {
     auto child = std::make_unique<CMappedResource>(true);
     if (child->OpenResource(path.c_str())) overlays.push_back(std::move(child));
 }
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
bool CMappedResource::IsDeleted(LPCSTR name) const
{
    for(const auto& child:overlays) {
        if(child->IsDeleted(name))return true;
        if(child->IsFileExistExact(name))return false;
    }
    if(ignoreOriginal||!pak)return false;
    try{auto entry=pak->Find(name);return entry&&entry->method==2;}catch(const std::exception&){return false;}
}
std::string CMappedResource::FileProvider(LPCSTR name)
{
    if(IsDeleted(name))return {};
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
 for(auto&child:overlays){child->GetFileList(local,mask);append();}
 if (!ignoreOriginal && pak) {
     for (const auto& e : pak->Entries()) if (!mask || PathMatchSpecA(e.name.c_str(),mask)) local.Add(e.name.c_str());
     append();
 } else if (!ignoreOriginal && looseDirectory) {
     for (const auto& f : std::filesystem::recursive_directory_iterator(sourcePath)) {
         if (!f.is_regular_file()) continue;
         auto name = f.path().lexically_relative(sourcePath).string();
         if (!mask || PathMatchSpecA(name.c_str(),mask)) local.Add(name.c_str());
     }
     append();
 } else if(!ignoreOriginal){CResourceBase::GetFileList(local,mask);append();}
 for(int i=names.GetSize()-1;i>=0;--i)if(IsDeleted(names[i]))names.RemoveAt(i);
}
namespace {
bool ReadResourceBytes(int file, void* buffer, std::size_t bytes)
{
    auto* out = static_cast<BYTE*>(buffer);
    while (bytes) {
        const unsigned count = static_cast<unsigned>((std::min)(bytes, std::size_t(1u << 20)));
        const int read = _read(file, out, count);
        if (read <= 0) return false;
        out += read; bytes -= read;
    }
    return true;
}
}
BOOL CMappedResource::OpenResource(LPCSTR pszFileName, BOOL _bWriteAccess)
{
    CloseResource();
    bWriteAccess = _bWriteAccess;
    const std::string mappedName = (!rawResource && !_bWriteAccess)
        ? ModRuntime::MapPath(pszFileName) : std::string(pszFileName);
    const std::string resolvedName = _bWriteAccess ? mappedName
        : ei::pak::ResolveArchivePath(mappedName).string();
    sourcePath = resolvedName;
    pszFileName = resolvedName.c_str();
    OpenOverlays(pszFileName, _bWriteAccess);
    if (ignoreOriginal) return TRUE;
    if (!overlays.empty() && !std::filesystem::exists(sourcePath)) { ignoreOriginal = true; return TRUE; }
    if (!_bWriteAccess && std::filesystem::is_directory(sourcePath)) {
        looseDirectory = true; return TRUE;
    }
    if (_stricmp(std::filesystem::path(sourcePath).extension().string().c_str(),".pak") == 0) {
        if (_bWriteAccess) { GameJournal::Event("PAK","read_only","path=%s",sourcePath.c_str()); return FALSE; }
        try {
            for(const auto& f:std::filesystem::directory_iterator(std::filesystem::absolute(sourcePath).parent_path()))
                if(f.path().filename().string().rfind("pak-txn-",0)==0&&f.path().extension()==L".json")GameJournal::Event("PAK","pending_transaction","manifest=%s",f.path().string().c_str());
            pak = std::make_unique<ei::pak::Archive>(sourcePath); return TRUE;
        }
        catch (const std::exception& e) { GameJournal::Event("PAK","open_failed","%s",e.what()); return FALSE; }
    }
    hFile = _open(pszFileName, _O_BINARY | _O_RANDOM | (bWriteAccess ? _O_RDWR : _O_RDONLY));
    if (hFile == -1) return ReportError("Cannot open resource: %s", pszFileName);
    const auto size = _filelengthi64(hFile);
    if (size < 0 || !ReadResourceBytes(hFile, &header, sizeof(header)) ||
        !SetResourceLayout(static_cast<std::uint64_t>(size), false)) {
        CloseResource();
        return ReportError("Invalid resource header/ranges: %s", pszFileName);
    }
    try {
        const auto tableBytes = std::size_t(header.dwEntryCount) * sizeof(SResHashEntry);
        pHashTable = dbgnew SResHashEntry[header.dwEntryCount];
        pNamePool = dbgnew BYTE[static_cast<std::size_t>(namePoolBytes)];
        if (_lseeki64(hFile, header.dwHashTableOffset, SEEK_SET) == -1 ||
            !ReadResourceBytes(hFile, pHashTable, tableBytes) ||
            !ReadResourceBytes(hFile, pNamePool, static_cast<std::size_t>(namePoolBytes))) {
            CloseResource();
            return ReportError("Incomplete resource table/names: %s", pszFileName);
        }
    } catch (...) { CloseResource(); throw; }
    return TRUE;
}

BOOL CMappedResource::OpenResourceMem(LPVOID address, std::size_t bytes)
{
    CloseResource();
    if (!BindResourceMemory(address, bytes)) {
        CloseResource();
        return ReportError("Invalid memory resource header/ranges");
    }
    pBaseAddress = static_cast<BYTE*>(address);
    return TRUE;
}

void CMappedResource::CloseResource()
{
    overlays.clear();ignoreOriginal=false;
    CloseAllFiles();
    pak.reset(); looseDirectory = false;
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
    resourceBytes = namePoolBytes = 0;
}

//--------------------------------------------------------------------------------------------

bool CMappedResource::IsFileExist(LPCSTR pszFileName)
{
    for (const auto& candidate : BriefingCandidates(pszFileName))
    {
        if(IsDeleted(candidate.c_str()))return {};
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
    try {
        if (pak) {auto e=pak->Find(pszFileName);return e&&e->method!=2;}
        if (looseDirectory) return std::filesystem::is_regular_file(std::filesystem::path(sourcePath)/ei::pak::Normalize(pszFileName));
    } catch (const std::exception&) { return false; }
    return pHashTable && pNamePool && GetIndexOfName(pszFileName) != DWORD(-1);
}
LPBYTE CMappedResource::GetFile(LPCSTR pszFileName, LPDWORD pnDataSize)
{
    for (const auto& candidate : BriefingCandidates(pszFileName))
    {
        if(IsDeleted(candidate.c_str()))return {};
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
    if (!pszFileName || (!pak && !looseDirectory && (!pHashTable || !pNamePool)))
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

    if (pak || looseDirectory) {
        try {
            ei::pak::Bytes bytes;
            if (pak) {
                auto* entry = pak->Find(pszFileName); if (!entry||entry->method==2) return nullptr;
                bytes = pak->Read(*entry,RuntimeDiagnostics::Enabled());
            } else {
                const auto path = std::filesystem::path(sourcePath)/ei::pak::Normalize(pszFileName);
                if (!std::filesystem::is_regular_file(path)) return nullptr;
                const auto size = std::filesystem::file_size(path);
                if (!size || size > MAXDWORD) return nullptr;
                bytes.resize(static_cast<size_t>(size)); std::ifstream in(path,std::ios::binary);
                if (!in.read(reinterpret_cast<char*>(bytes.data()),bytes.size())) return nullptr;
            }
            auto memory = std::make_unique<BYTE[]>(bytes.size());
            memcpy(memory.get(),bytes.data(),bytes.size());
            auto record = std::make_unique<SResourceFileRecord>();
            record->szName = pszFileName; record->dwSize = static_cast<DWORD>(bytes.size());
            record->dwFilePos = 0; record->nRefs = 1; record->pAllocatedMemory = memory.get();
            auto inserted = recByName.emplace(record->szName,record.get());
            try { recByData.emplace(memory.get(),record.get()); }
            catch (...) { recByName.erase(inserted.first); throw; }
            resourcePayloadBytes.fetch_add(record->dwSize,std::memory_order_relaxed);
            if (pnDataSize) *pnDataSize = record->dwSize;
            record.release(); return memory.release();
        } catch (const std::exception& e) { GameJournal::Event("PAK","read_failed","path=%s entry=%s error=%s",sourcePath.c_str(),pszFileName,e.what()); return nullptr; }
    }
    DWORD dwIndex = GetIndexOfName(pszFileName);
    if (dwIndex == -1) return NULL;
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

    if (_lseeki64(hFile, pRec->dwFilePos, SEEK_SET) == -1 ||
        !ReadResourceBytes(hFile, pRec->pAllocatedMemory, pRec->dwSize))
    {
        delete[] pRec->pAllocatedMemory;
        delete pRec;
        return NULL;
    }
    if (pnDataSize)
        *pnDataSize = pRec->dwSize;

    recByName[pRec->szName] = pRec;
    recByData[pRec->pAllocatedMemory] = pRec;
    resourcePayloadBytes.fetch_add(pRec->dwSize, std::memory_order_relaxed);
    return pRec->pAllocatedMemory;
}

bool CMappedResource::CloseFile(LPBYTE pFileImage)
{
    for(auto&child:overlays)if(child->CloseFile(pFileImage))return true;
    if (pBaseAddress)
        return reinterpret_cast<std::uintptr_t>(pFileImage) >= reinterpret_cast<std::uintptr_t>(pBaseAddress) &&
            reinterpret_cast<std::uintptr_t>(pFileImage) - reinterpret_cast<std::uintptr_t>(pBaseAddress) < resourceBytes;

    if (!pFileImage)
        return false;

    auto pos = recByData.find(pFileImage);
    if (pos == recByData.end())
    {
        // ReportWarning("CMappedResource::CloseFile()\nFile already closed\n");
        return false;
    }
    SResourceFileRecord *pRec = pos->second;
    if (--pRec->nRefs == 0)
    {
        if (bWriteAccess && (_lseeki64(hFile, pRec->dwFilePos, SEEK_SET) == -1 ||
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
BOOL CWin32MappedResource::OpenResource(LPCSTR name, BOOL write)
{
    const auto resolved = write ? std::string(name) : ei::pak::ResolveArchivePath(name).string();
    name = resolved.c_str();
    if (_stricmp(std::filesystem::path(name).extension().string().c_str(),".pak") == 0 || std::filesystem::is_directory(name))
        return CMappedResource::OpenResource(name,write);
    CloseResource(); sourcePath = name; OpenOverlays(name, write, true);
    if (ignoreOriginal) return TRUE;
    if (!overlays.empty() && !std::filesystem::exists(name)) { ignoreOriginal = true; return TRUE; }
    if (!cMappedFile.Open(name, write)) { CloseResource(); return FALSE; }
    if (!BindResourceMemory(cMappedFile.GetAddress(), cMappedFile.GetSize())) {
        CloseResource();
        return ReportError("Invalid mapped resource header/ranges: %s", name);
    }
    pBaseAddress = static_cast<BYTE*>(cMappedFile.GetAddress());
    return TRUE;
}

BOOL CWin32MappedResource::OpenResourceMem(LPVOID address, std::size_t bytes)
{
    CloseResource();
    if (!BindResourceMemory(address, bytes)) {
        CloseResource();
        return ReportError("Invalid memory resource header/ranges");
    }
    pBaseAddress = static_cast<BYTE*>(address);
    return TRUE;
}

void CWin32MappedResource::CloseResource()
{
    overlays.clear();ignoreOriginal=false;
    CMappedResource::CloseAllFiles();
    cMappedFile.Close();
    pHashTable = NULL;
    pNamePool = NULL;
    pBaseAddress = NULL;
    CMappedResource::CloseResource();
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
        if(IsDeleted(candidate.c_str()))return {};
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
    if (!pBaseAddress) return CMappedResource::GetFileExact(pszFileName,pnDataSize);
    if (!pszFileName || !pHashTable || !pNamePool)
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
    if (!pBaseAddress) return CMappedResource::CloseFile(pFileImage);
    for(auto&child:overlays)if(child->CloseFile(pFileImage))return true;
    return reinterpret_cast<std::uintptr_t>(pFileImage) >= reinterpret_cast<std::uintptr_t>(pBaseAddress) &&
        reinterpret_cast<std::uintptr_t>(pFileImage) - reinterpret_cast<std::uintptr_t>(pBaseAddress) < resourceBytes;
}
void CWin32MappedResource::CloseAllFiles()
{
    if (!pBaseAddress) { CMappedResource::CloseAllFiles(); return; }
    for(auto&child:overlays)child->CloseAllFiles();
}
