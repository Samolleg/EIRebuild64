#include "CrashEvidence.h"

#include <dbghelp.h>
#include <tlhelp32.h>
#include <wincrypt.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "advapi32.lib")

namespace CrashEvidence
{
namespace
{
typedef BOOL(WINAPI *MiniDumpWriteDumpFn)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
                                           PMINIDUMP_EXCEPTION_INFORMATION,
                                           PMINIDUMP_USER_STREAM_INFORMATION,
                                           PMINIDUMP_CALLBACK_INFORMATION);

wchar_t dumpPath[MAX_PATH] = {};
wchar_t reportPath[MAX_PATH] = {};
wchar_t markerPath[MAX_PATH] = {};
char version[96] = {};
char processHash[65] = {};
char addonHash[65] = {};
DWORD processHashError = ERROR_NOT_READY;
DWORD addonHashError = ERROR_NOT_READY;
HMODULE dbghelp = nullptr;
MiniDumpWriteDumpFn writeDump = nullptr;
PatchRangeCountFn patchRangeCount = nullptr;
PatchRangeAtFn patchRangeAt = nullptr;
char patchRangeText[32768] = {};
DWORD patchRangeTextSize = 0;
volatile LONG handling = 0;
HANDLE watchdogStop = nullptr;
HANDLE watchdogThread = nullptr;
DWORD watchdogLimit = 0;

#ifdef CRASH_EVIDENCE_TEST
bool failReportCreate = false;
bool failPrimaryDump = false;
bool failFallbackDump = false;
bool failReportWrite = false;
bool failPrimaryException = false;
DWORD testWatchdogLimit = 0;
DWORD testDumpHang = 0;
#endif

bool HashModule(HMODULE module, char (&hex)[65], DWORD &error)
{
    wchar_t path[MAX_PATH] = {};
    DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
    if (!length || length == MAX_PATH)
    {
        error = GetLastError() ? GetLastError() : ERROR_INSUFFICIENT_BUFFER;
        return false;
    }
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        error = GetLastError();
        return false;
    }
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    bool success = CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) &&
                   CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash);
    if (success)
    {
        unsigned char buffer[16384];
        for (;;)
        {
            DWORD read = 0;
            if (!ReadFile(file, buffer, sizeof(buffer), &read, nullptr))
            {
                success = false;
                break;
            }
            if (!read)
                break;
            if (!CryptHashData(hash, buffer, read, 0))
            {
                success = false;
                break;
            }
        }
        BYTE digest[32];
        DWORD digestLength = sizeof(digest);
        if (success && CryptGetHashParam(hash, HP_HASHVAL, digest, &digestLength, 0) &&
            digestLength == sizeof(digest))
        {
            const char digits[] = "0123456789ABCDEF";
            for (unsigned i = 0; i < sizeof(digest); ++i)
            {
                hex[i * 2] = digits[digest[i] >> 4];
                hex[i * 2 + 1] = digits[digest[i] & 15];
            }
            hex[64] = 0;
        }
        else
            success = false;
    }
    if (!success)
        error = GetLastError() ? GetLastError() : ERROR_GEN_FAILURE;
    if (hash)
        CryptDestroyHash(hash);
    if (provider)
        CryptReleaseContext(provider, 0);
    CloseHandle(file);
    if (success)
        error = 0;
    return success;
}

bool AppendExtension(wchar_t *target, size_t capacity, const wchar_t *base, const wchar_t *extension)
{
    return wcscpy_s(target, capacity, base) == 0 &&
           wcscat_s(target, capacity, extension) == 0;
}

DWORD WINAPI WatchdogProc(void *)
{
    if (WaitForSingleObject(watchdogStop, watchdogLimit) == WAIT_TIMEOUT)
        TerminateProcess(GetCurrentProcess(), ERROR_TIMEOUT);
    return 0;
}

void StartWatchdog(bool fullDump)
{
    // Disposable x86 probes finish in seconds. These are conservative hang limits,
    // not a claim that a full game dump can always complete within the limit.
    watchdogLimit = fullDump ? 15 * 60 * 1000 : 3 * 60 * 1000;
#ifdef CRASH_EVIDENCE_TEST
    if (testWatchdogLimit)
        watchdogLimit = testWatchdogLimit;
#endif
    watchdogStop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    watchdogThread = watchdogStop ? CreateThread(nullptr, 0, WatchdogProc, nullptr, 0, nullptr) : nullptr;
    if (!watchdogThread && watchdogStop)
    {
        CloseHandle(watchdogStop);
        watchdogStop = nullptr;
    }
}

void StopWatchdog()
{
    if (watchdogStop)
    {
        SetEvent(watchdogStop);
        if (watchdogThread)
        {
            WaitForSingleObject(watchdogThread, 1000);
            CloseHandle(watchdogThread);
            watchdogThread = nullptr;
        }
        CloseHandle(watchdogStop);
        watchdogStop = nullptr;
    }
}

struct ReportWriter
{
    HANDLE file;
    DWORD error;

    void Write(const char *data, size_t length)
    {
        if (error || file == INVALID_HANDLE_VALUE)
            return;
#ifdef CRASH_EVIDENCE_TEST
        if (failReportWrite)
        {
            error = ERROR_WRITE_FAULT;
            return;
        }
#endif
        DWORD written = 0;
        if (length > MAXDWORD || !WriteFile(file, data, static_cast<DWORD>(length), &written, nullptr) ||
            written != length)
            error = GetLastError() ? GetLastError() : ERROR_WRITE_FAULT;
    }

    void Print(const char *format, ...)
    {
        if (error)
            return;
        char line[1024];
        va_list args;
        va_start(args, format);
        int length = vsnprintf_s(line, sizeof(line), _TRUNCATE, format, args);
        va_end(args);
        if (length < 0)
            length = static_cast<int>(strlen(line));
        Write(line, static_cast<size_t>(length));
    }
};

void WriteContext(ReportWriter &out, EXCEPTION_POINTERS *info)
{
    if (!info || !info->ExceptionRecord)
    {
        out.Print("Exception: unavailable\r\n");
        return;
    }
    EXCEPTION_RECORD *record = info->ExceptionRecord;
    out.Print("Exception code: %08lX address: %p flags: %08lX\r\n",
              record->ExceptionCode, record->ExceptionAddress, record->ExceptionFlags);
    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2)
        out.Print("Access: %s address=%p\r\n",
                  record->ExceptionInformation[0] == 0 ? "read" :
                  record->ExceptionInformation[0] == 1 ? "write" : "execute",
                  reinterpret_cast<void *>(record->ExceptionInformation[1]));
    if (!info->ContextRecord)
    {
        out.Print("Registers: unavailable\r\n");
        return;
    }
#if defined(_M_IX86)
    CONTEXT *ctx = info->ContextRecord;
    out.Print("EAX=%08lX EBX=%08lX ECX=%08lX EDX=%08lX ESI=%08lX EDI=%08lX\r\n",
              ctx->Eax, ctx->Ebx, ctx->Ecx, ctx->Edx, ctx->Esi, ctx->Edi);
    out.Print("EIP=%08lX ESP=%08lX EBP=%08lX EFLAGS=%08lX\r\n",
              ctx->Eip, ctx->Esp, ctx->Ebp, ctx->EFlags);
    unsigned char stack[128] = {};
    SIZE_T bytesRead = 0;
    BOOL readable = ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void *>(ctx->Esp),
                                      stack, sizeof(stack), &bytesRead);
    if ((readable || bytesRead) && bytesRead)
    {
        out.Print("Stack at ESP (%lu bytes):\r\n", static_cast<unsigned long>(bytesRead));
        for (SIZE_T i = 0; i < bytesRead; i += 16)
        {
            out.Print("%08lX:", static_cast<unsigned long>(ctx->Esp + i));
            for (SIZE_T j = i; j < bytesRead && j < i + 16; ++j)
                out.Print(" %02X", stack[j]);
            out.Print("\r\n");
        }
    }
    else
        out.Print("Stack bytes: unavailable (error %lu)\r\n", GetLastError());
#else
    out.Print("Registers: unsupported architecture\r\n");
#endif
}

void WriteMemory(ReportWriter &out)
{
    MEMORYSTATUSEX status = {};
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status))
        out.Print("Memory: physical available=%llu, virtual available=%llu, pagefile available=%llu\r\n",
                  status.ullAvailPhys, status.ullAvailVirtual, status.ullAvailPageFile);
    else
        out.Print("Memory status: unavailable (error %lu)\r\n", GetLastError());

    SIZE_T committed = 0, reserved = 0, freeBytes = 0;
    unsigned int regions = 0;
    unsigned char *address = nullptr;
    MEMORY_BASIC_INFORMATION mbi = {};
    while (regions < 65536 && VirtualQuery(address, &mbi, sizeof(mbi)) == sizeof(mbi))
    {
        if (mbi.State == MEM_COMMIT)
            committed += mbi.RegionSize;
        else if (mbi.State == MEM_RESERVE)
            reserved += mbi.RegionSize;
        else if (mbi.State == MEM_FREE)
            freeBytes += mbi.RegionSize;
        ++regions;
        unsigned char *next = static_cast<unsigned char *>(mbi.BaseAddress) + mbi.RegionSize;
        if (next <= address)
            break;
        address = next;
    }
    out.Print("Address space: committed=%lu reserved=%lu free=%lu regions=%u%s\r\n",
              static_cast<unsigned long>(committed), static_cast<unsigned long>(reserved),
              static_cast<unsigned long>(freeBytes), regions,
              regions == 65536 ? " (truncated)" : "");
}

void WriteModules(ReportWriter &out)
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        out.Print("Modules: unavailable (error %lu)\r\n", GetLastError());
        return;
    }
    MODULEENTRY32W module = {};
    module.dwSize = sizeof(module);
    if (Module32FirstW(snapshot, &module))
    {
        unsigned count = 0;
        do
        {
            DWORD timestamp = 0, checksum = 0, pdbAge = 0;
            GUID pdbGuid = {};
            bool hasPe = false, hasPdb = false;
            __try
            {
                BYTE *base = module.modBaseAddr;
                IMAGE_DOS_HEADER *dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
                if (dos->e_magic == IMAGE_DOS_SIGNATURE && dos->e_lfanew > 0 && dos->e_lfanew < 4096)
                {
                    IMAGE_NT_HEADERS *nt = reinterpret_cast<IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
                    if (nt->Signature == IMAGE_NT_SIGNATURE)
                    {
                        hasPe = true;
                        timestamp = nt->FileHeader.TimeDateStamp;
                        checksum = nt->OptionalHeader.CheckSum;
                        IMAGE_DATA_DIRECTORY directory =
                            nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
                        if (directory.VirtualAddress && directory.Size >= sizeof(IMAGE_DEBUG_DIRECTORY))
                        {
                            IMAGE_DEBUG_DIRECTORY *entries = reinterpret_cast<IMAGE_DEBUG_DIRECTORY *>(
                                base + directory.VirtualAddress);
                            unsigned n = directory.Size / sizeof(IMAGE_DEBUG_DIRECTORY);
                            if (n > 32)
                                n = 32;
                            for (unsigned i = 0; i < n; ++i)
                            {
                                if (entries[i].Type != IMAGE_DEBUG_TYPE_CODEVIEW ||
                                    entries[i].SizeOfData < 24 || !entries[i].AddressOfRawData)
                                    continue;
                                BYTE *cv = base + entries[i].AddressOfRawData;
                                if (memcmp(cv, "RSDS", 4) == 0)
                                {
                                    memcpy(&pdbGuid, cv + 4, sizeof(pdbGuid));
                                    memcpy(&pdbAge, cv + 20, sizeof(pdbAge));
                                    hasPdb = true;
                                    break;
                                }
                            }
                        }
                    }
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                hasPe = hasPdb = false;
            }
            out.Print("Module: %ls base=%p size=%lu path=%ls", module.szModule,
                      module.modBaseAddr, module.modBaseSize, module.szExePath);
            if (hasPe)
                out.Print(" pe_timestamp=%08lX pe_checksum=%08lX", timestamp, checksum);
            if (hasPdb)
                out.Print(" pdb_rsds=%08lX-%04X-%04X-%02X%02X%02X%02X%02X%02X%02X%02X age=%lu",
                          pdbGuid.Data1, pdbGuid.Data2, pdbGuid.Data3,
                          pdbGuid.Data4[0], pdbGuid.Data4[1], pdbGuid.Data4[2], pdbGuid.Data4[3],
                          pdbGuid.Data4[4], pdbGuid.Data4[5], pdbGuid.Data4[6], pdbGuid.Data4[7], pdbAge);
            out.Print("\r\n");
            module.dwSize = sizeof(module);
        } while (++count < 256 && Module32NextW(snapshot, &module));
        if (count == 256)
            out.Print("Module list truncated at 256 entries\r\n");
    }
    else
        out.Print("Modules: unavailable (error %lu)\r\n", GetLastError());
    CloseHandle(snapshot);
}

void BuildPatchRangeText()
{
    patchRangeTextSize = 0;
    patchRangeText[0] = 0;
    if (!patchRangeCount || !patchRangeAt)
        return;
    size_t total = patchRangeCount();
    size_t count = total < 256 ? total : 256;
    for (size_t i = 0; i < count; ++i)
    {
        PatchRange range = {};
        if (!patchRangeAt(i, &range))
            break;
        int written = _snprintf_s(patchRangeText + patchRangeTextSize,
                                  sizeof(patchRangeText) - patchRangeTextSize,
                                  _TRUNCATE, "%s 0x%p+%Iu owner=%.80s\r\n",
                                  range.write ? "write" : "read", range.address,
                                  range.size, range.owner ? range.owner : "<unnamed>");
        if (written <= 0)
            break;
        patchRangeTextSize += (DWORD)written;
    }
    if (total > count && patchRangeTextSize + 48 < sizeof(patchRangeText))
    {
        int written = _snprintf_s(patchRangeText + patchRangeTextSize,
                                  sizeof(patchRangeText) - patchRangeTextSize,
                                  _TRUNCATE, "... %Iu more ranges omitted\r\n", total - count);
        if (written > 0)
            patchRangeTextSize += (DWORD)written;
    }
}

bool WriteReport(EXCEPTION_POINTERS *info, RecentCountFn countFn,
                 RecentLineFn lineFn, DWORD &error)
{
#ifdef CRASH_EVIDENCE_TEST
    if (failReportCreate)
    {
        error = ERROR_ACCESS_DENIED;
        return false;
    }
#endif
    HANDLE file = CreateFileW(reportPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                              CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        error = GetLastError();
        return false;
    }
    ReportWriter out = {file, 0};
    SYSTEMTIME now;
    GetLocalTime(&now);
    out.Print("EIRebuild crash report\r\nBuild: %s\r\nTime: %04u-%02u-%02u %02u:%02u:%02u.%03u local\r\n",
              version, now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
              now.wSecond, now.wMilliseconds);
    out.Print("Process=%lu thread=%lu\r\n", GetCurrentProcessId(), GetCurrentThreadId());
    out.Print("Process image file SHA256 at preparation: %s (error %lu)\r\n",
              processHash[0] ? processHash : "unavailable", processHashError);
    out.Print("Diagnostic host file SHA256 at preparation: %s (error %lu)\r\n",
              addonHash[0] ? addonHash : "unavailable", addonHashError);
    __try { WriteContext(out, info); }
    __except (EXCEPTION_EXECUTE_HANDLER) { out.Print("Exception context: unreadable\r\n"); }
    __try { WriteMemory(out); }
    __except (EXCEPTION_EXECUTE_HANDLER) { out.Print("Memory: unreadable\r\n"); }
    __try { WriteModules(out); }
    __except (EXCEPTION_EXECUTE_HANDLER) { out.Print("Modules: unreadable\r\n"); }
    out.Print("Patch ranges (also in minidump CommentStreamA):\r\n%s",
              patchRangeTextSize ? patchRangeText : "unavailable\r\n");
    out.Print("Recent engine diagnostics (bounded snapshot):\r\n");
    __try
    {
        if (countFn && lineFn)
        {
            size_t count = countFn();
            if (count > 24)
                count = 24;
            for (size_t i = 0; i < count; ++i)
            {
                const char *line = lineFn(i);
                if (line)
                    out.Print("%.*s\r\n", 199, line);
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { out.Print("Recent log: unreadable\r\n"); }
    if (!FlushFileBuffers(file) && !out.error)
        out.error = GetLastError();
    CloseHandle(file);
    error = out.error;
    if (out.error)
    {
        DeleteFileW(reportPath);
        return false;
    }
    return true;
}

BOOL CallDump(HANDLE file, MINIDUMP_TYPE kind, MINIDUMP_EXCEPTION_INFORMATION *info)
{
    BOOL result = FALSE;
    MINIDUMP_USER_STREAM stream = {};
    MINIDUMP_USER_STREAM_INFORMATION streams = {};
    if (patchRangeTextSize)
    {
        stream.Type = CommentStreamA;
        stream.Buffer = patchRangeText;
        stream.BufferSize = patchRangeTextSize + 1;
        streams.UserStreamCount = 1;
        streams.UserStreamArray = &stream;
    }
    __try
    {
#ifdef CRASH_EVIDENCE_TEST
        if (failPrimaryException && kind != MiniDumpNormal)
            RaiseException(0xE0000002, 0, 0, nullptr);
#endif
        result = writeDump(GetCurrentProcess(), GetCurrentProcessId(), file,
                           kind, info, patchRangeTextSize ? &streams : nullptr, nullptr);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        SetLastError(GetExceptionCode());
        result = FALSE;
    }
    return result;
}

bool WriteMinidump(EXCEPTION_POINTERS *exceptionInfo, bool fullDump,
                   bool &fallbackUsed, DWORD &error)
{
#ifdef CRASH_EVIDENCE_TEST
    if (testDumpHang)
        Sleep(testDumpHang);
#endif
    if (!writeDump)
    {
        error = ERROR_PROC_NOT_FOUND;
        return false;
    }
    HANDLE file = CreateFileW(dumpPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                              CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        error = GetLastError();
        return false;
    }
    MINIDUMP_EXCEPTION_INFORMATION info = {};
    info.ThreadId = GetCurrentThreadId();
    info.ExceptionPointers = exceptionInfo;
    info.ClientPointers = FALSE;
    MINIDUMP_TYPE extended = static_cast<MINIDUMP_TYPE>(
        (fullDump ? MiniDumpWithFullMemory : MiniDumpWithIndirectlyReferencedMemory) |
        MiniDumpWithDataSegs | MiniDumpWithFullMemoryInfo | MiniDumpWithThreadInfo |
        MiniDumpWithProcessThreadData | MiniDumpWithUnloadedModules |
        MiniDumpIgnoreInaccessibleMemory);
    BOOL success = FALSE;
#ifdef CRASH_EVIDENCE_TEST
    if (failPrimaryDump)
        SetLastError(ERROR_INVALID_DATA);
    else
#endif
        success = CallDump(file, extended, &info);
    if (!success)
    {
        error = GetLastError() ? GetLastError() : ERROR_GEN_FAILURE;
        fallbackUsed = true;
        if (SetFilePointer(file, 0, nullptr, FILE_BEGIN) != 0 || !SetEndOfFile(file))
        {
            error = GetLastError() ? GetLastError() : ERROR_WRITE_FAULT;
            CloseHandle(file);
            DeleteFileW(dumpPath);
            return false;
        }
#ifdef CRASH_EVIDENCE_TEST
        if (failFallbackDump)
            SetLastError(ERROR_INVALID_DATA);
        else
#endif
            success = CallDump(file, MiniDumpNormal, &info);
        if (!success)
            error = GetLastError() ? GetLastError() : ERROR_GEN_FAILURE;
    }
    if (success && !FlushFileBuffers(file))
    {
        error = GetLastError();
        success = FALSE;
    }
    CloseHandle(file);
    if (success)
        error = 0;
    if (!success)
        DeleteFileW(dumpPath);
    return success != FALSE;
}
}

void Configure(const wchar_t *path, const char *buildVersion)
{
    dumpPath[0] = reportPath[0] = markerPath[0] = 0;
    version[0] = 0;
    if (!path || wcslen(path) + 9 >= MAX_PATH ||
        !AppendExtension(dumpPath, MAX_PATH, path, L"") ||
        !AppendExtension(reportPath, MAX_PATH, path, L".txt") ||
        !AppendExtension(markerPath, MAX_PATH, path, L".writing"))
        return;
    if (buildVersion)
        strncpy_s(version, buildVersion, _TRUNCATE);
}

void Prepare(HMODULE addonModule)
{
    static volatile LONG prepared;
    if (InterlockedCompareExchange(&prepared, 1, 0))
        return;
    HashModule(nullptr, processHash, processHashError);
    HashModule(addonModule, addonHash, addonHashError);
    dbghelp = LoadLibraryW(L"dbghelp.dll");
    if (dbghelp)
        writeDump = reinterpret_cast<MiniDumpWriteDumpFn>(GetProcAddress(dbghelp, "MiniDumpWriteDump"));
}

void SetPatchRangeCallbacks(PatchRangeCountFn count, PatchRangeAtFn at)
{
    patchRangeCount = count;
    patchRangeAt = at;
}

void Shutdown()
{
    if (dbghelp)
        FreeLibrary(dbghelp);
    dbghelp = nullptr;
    writeDump = nullptr;
}

Result Capture(EXCEPTION_POINTERS *exceptionInfo, bool fullDump,
               RecentCountFn recentCount, RecentLineFn recentLine)
{
    Result result = {};
    if (InterlockedCompareExchange(&handling, 1, 0))
    {
        result.alreadyHandling = true;
        return result;
    }
    __try { BuildPatchRangeText(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { patchRangeTextSize = 0; }
    StartWatchdog(fullDump);
    result.watchdogStarted = watchdogThread != nullptr;
    if (!dumpPath[0])
    {
        result.reportError = result.dumpError = ERROR_INVALID_NAME;
        StopWatchdog();
        return result;
    }
    HANDLE marker = CreateFileW(markerPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (marker == INVALID_HANDLE_VALUE)
    {
        result.reportError = result.dumpError = GetLastError();
        StopWatchdog();
        return result;
    }
    CloseHandle(marker);
    result.reportWritten = WriteReport(exceptionInfo, recentCount, recentLine, result.reportError);
    result.dumpWritten = WriteMinidump(exceptionInfo, fullDump,
                                       result.fallbackUsed, result.dumpError);
    DeleteFileW(markerPath);
    return result;
}

void FinishHandling()
{
    // This is reached after optional save recovery, before the modal UI.
    StopWatchdog();
}

const wchar_t *ReportPath() { return reportPath; }
const wchar_t *DumpPath() { return dumpPath; }

#ifdef CRASH_EVIDENCE_TEST
void SetTestFailures(bool reportCreate, bool primaryDump, bool fallbackDump, bool reportWrite)
{
    failReportCreate = reportCreate;
    failPrimaryDump = primaryDump;
    failFallbackDump = fallbackDump;
    failReportWrite = reportWrite;
}
void SetTestDumpException(bool enabled) { failPrimaryException = enabled; }
void SetTestWatchdog(DWORD timeoutMs, DWORD hangMs)
{
    testWatchdogLimit = timeoutMs;
    testDumpHang = hangMs;
}
#endif
}
