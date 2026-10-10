#include "CrashEvidence.h"

#include <dbghelp.h>
#include <tlhelp32.h>
#include <wincrypt.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <exception>

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
Details details = {};
DWORD crashThread = 0;
HANDLE crashThreadHandle = nullptr;
#define DBG_PROC(name) decltype(&::name) p##name = nullptr
DBG_PROC(StackWalk64);
DBG_PROC(SymInitializeW);
DBG_PROC(SymCleanup);
DBG_PROC(SymSetOptions);
DBG_PROC(SymLoadModuleExW);
DBG_PROC(SymGetModuleInfoW64);
DBG_PROC(SymFromAddr);
DBG_PROC(SymGetLineFromAddr64);
DBG_PROC(SymFunctionTableAccess64);
DBG_PROC(UnDecorateSymbolName);
#undef DBG_PROC
bool symbolsInitialized = false;
DWORD64 symbolBase = 0;

bool ReadMemory(ULONG_PTR address, void *buffer, size_t size)
{
    SIZE_T read = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void *>(address),
                                        buffer, size, &read) && read == size;
}

bool ReadString(ULONG_PTR address, char *buffer, size_t size)
{
    if (!size) return false;
    buffer[0] = 0;
    for (size_t i = 0; i + 1 < size; ++i)
    {
        char c = 0;
        if (address + i < address || !ReadMemory(address + i, &c, 1)) return false;
        buffer[i] = c;
        buffer[i + 1] = 0;
        if (!c) return true;
    }
    return true; // Explicitly bounded, possibly truncated text.
}

const char *ExceptionKind(DWORD code)
{
    switch (code)
    {
    case EXCEPTION_ACCESS_VIOLATION: return "access_violation";
    case 0xE06D7363: return "cpp_exception";
    case EXCEPTION_STACK_OVERFLOW: return "stack_overflow";
    case EXCEPTION_IN_PAGE_ERROR: return "in_page_error";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal_instruction";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "integer_divide_by_zero";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "float_divide_by_zero";
    case EXCEPTION_BREAKPOINT: return "breakpoint";
    case EXCEPTION_INT_OVERFLOW: return "integer_overflow";
    case 0xC0000409: return "fail_fast";
    case 0xE0000135: return "cpp_terminate_reporting_point";
    case 0xE0000136: return "invalid_parameter";
    default: return "unknown_exception";
    }
}

// MSVC x86 EH metadata contains absolute pointers, unlike x64 image-relative EH.
struct ThrowInfo32 { DWORD attributes, unwind, forward, catchables; };
struct Catchable32 { DWORD properties, type; int mdisp, pdisp, vdisp, size; DWORD copy; };

bool AdjustObject(ULONG_PTR object, const Catchable32 &type, ULONG_PTR &adjusted)
{
    __int64 offset = type.mdisp;
    if (type.pdisp >= 0)
    {
        DWORD table = 0;
        int displacement = 0;
        if (!ReadMemory(object + type.pdisp, &table, sizeof(table)) ||
            !ReadMemory(table + type.vdisp, &displacement, sizeof(displacement))) return false;
        offset += type.pdisp + displacement;
    }
    __int64 value = static_cast<__int64>(object) + offset;
    if (value <= 0 || value > MAXDWORD) return false;
    adjusted = static_cast<ULONG_PTR>(value);
    return true;
}

bool ReadWhat(ULONG_PTR object, char *message, size_t capacity)
{
    // Call only the recognized std::exception base, with SEH isolation. Unknown
    // exception vtables are never invoked; pointers and returned text are guarded.
    DWORD vtable = 0, method = 0;
    MEMORY_BASIC_INFORMATION region = {};
    if (!ReadMemory(object, &vtable, 4) || !ReadMemory(vtable + 4, &method, 4) ||
        !VirtualQuery(reinterpret_cast<void *>(method), &region, sizeof(region)) ||
        region.State != MEM_COMMIT || !(region.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
            PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) || (region.Protect & PAGE_GUARD)) return false;
    __try
    {
        const char *value = reinterpret_cast<const std::exception *>(object)->what();
        return ReadString(reinterpret_cast<ULONG_PTR>(value), message, capacity);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void DecodeCpp(const EXCEPTION_RECORD &record)
{
    if (record.NumberParameters < 3 || record.ExceptionInformation[0] < 0x19930520 ||
        record.ExceptionInformation[0] > 0x19930522) return;
    ThrowInfo32 info = {};
    int count = 0;
    if (!ReadMemory(record.ExceptionInformation[2], &info, sizeof(info)) ||
        !ReadMemory(info.catchables, &count, sizeof(count)) || count <= 0 || count > 64) return;
    const ULONG_PTR object = record.ExceptionInformation[1];
    for (int i = 0; i < count; ++i)
    {
        DWORD pointer = 0;
        Catchable32 type = {};
        char decorated[384] = {}, name[384] = {};
        if (!ReadMemory(info.catchables + 4 + i * 4, &pointer, 4) ||
            !ReadMemory(pointer, &type, sizeof(type)) ||
            !ReadString(type.type + 8, decorated, sizeof(decorated))) continue;
        const char *input = decorated[0] == '.' ? decorated + 1 : decorated;
        if (!pUnDecorateSymbolName || !pUnDecorateSymbolName(input, name, sizeof(name), UNDNAME_NO_ARGUMENTS))
            strncpy_s(name, decorated, _TRUNCATE);
        if (details.types[0]) strncat_s(details.types, " -> ", _TRUNCATE);
        strncat_s(details.types, name, _TRUNCATE);
        ULONG_PTR base = 0;
        if (!AdjustObject(object, type, base)) continue;
        if (!strcmp(decorated, ".?AVexception@std@@") && !details.message[0])
        {
            if (!ReadWhat(base, details.message, sizeof(details.message)))
                strcpy_s(details.message, "std::exception::what() unavailable");
        }
        if (count == 1 && (!strcmp(decorated, ".J") || !strcmp(name, "long")) && type.size == 4)
        {
            LONG value = 0;
            if (ReadMemory(base, &value, sizeof(value)))
            {
                char system[768] = {};
                DWORD id = static_cast<DWORD>(value);
                if ((id & 0xffff0000) == 0x80070000) id &= 0xffff;
                FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                               nullptr, id, 0, system, sizeof(system), nullptr);
                _snprintf_s(details.message, _TRUNCATE, "HRESULT 0x%08lX (%ld): %s", value, value,
                            system[0] ? system : "system message unavailable");
            }
        }
    }
    if (!details.message[0])
    {
        // Unknown/CException-like objects: raw bounded bytes, no guessed layout,
        // CString decoding or virtual method call in a potentially corrupt process.
        BYTE bytes[16] = {};
        if (ReadMemory(object, bytes, sizeof(bytes)))
            _snprintf_s(details.message, _TRUNCATE,
                "object bytes: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                bytes[0],bytes[1],bytes[2],bytes[3],bytes[4],bytes[5],bytes[6],bytes[7],
                bytes[8],bytes[9],bytes[10],bytes[11],bytes[12],bytes[13],bytes[14],bytes[15]);
        else strcpy_s(details.message, "exception object unreadable");
    }
}

DWORD64 CALLBACK ModuleBase(HANDLE, DWORD64 address)
{
    if (address > MAXDWORD) return 0;
    MEMORY_BASIC_INFORMATION region = {};
    if (!VirtualQuery(reinterpret_cast<void *>(static_cast<ULONG_PTR>(address)), &region, sizeof(region)) ||
        region.Type != MEM_IMAGE) return 0;
    return reinterpret_cast<ULONG_PTR>(region.AllocationBase);
}

BOOL CALLBACK StackRead(HANDLE, DWORD64 address, PVOID buffer, DWORD size, LPDWORD read)
{
    SIZE_T amount = 0;
    BOOL ok = address <= MAXDWORD && ReadProcessMemory(GetCurrentProcess(),
        reinterpret_cast<void *>(static_cast<ULONG_PTR>(address)), buffer, size, &amount);
    *read = static_cast<DWORD>(amount);
    return ok;
}

PVOID CALLBACK FunctionTable(HANDLE process, DWORD64 address)
{
    return details.exactPdb && ModuleBase(process, address) == symbolBase && pSymFunctionTableAccess64
        ? pSymFunctionTableAccess64(process, address) : nullptr;
}

void AddFrame(DWORD address)
{
    if (!address || details.frameCount == 64) return;
    Frame &frame = details.frames[details.frameCount++];
    frame.address = address;
    DWORD base = static_cast<DWORD>(ModuleBase(GetCurrentProcess(), address));
    frame.rva = base ? address - base : address;
    frame.processImage = base == reinterpret_cast<ULONG_PTR>(GetModuleHandleW(nullptr));
    char path[MAX_PATH] = {};
    if (base && GetModuleFileNameA(reinterpret_cast<HMODULE>(base), path, MAX_PATH))
    {
        const char *name = strrchr(path, '\\');
        strncpy_s(frame.module, name ? name + 1 : path, _TRUNCATE);
    }
    else strcpy_s(frame.module, "unknown");
    if (!details.exactPdb || !frame.processImage) return;
    alignas(SYMBOL_INFO) char storage[sizeof(SYMBOL_INFO) + 512] = {};
    SYMBOL_INFO *symbol = reinterpret_cast<SYMBOL_INFO *>(storage);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO); symbol->MaxNameLen = 512;
    DWORD64 displacement = 0;
    if (pSymFromAddr && pSymFromAddr(GetCurrentProcess(), address, &displacement, symbol))
        strncpy_s(frame.function, symbol->Name, _TRUNCATE);
    IMAGEHLP_LINE64 line = {}; line.SizeOfStruct = sizeof(line);
    DWORD offset = 0;
    if (pSymGetLineFromAddr64 && pSymGetLineFromAddr64(GetCurrentProcess(), address, &offset, &line))
    {
        frame.line = line.LineNumber;
        ReadString(reinterpret_cast<ULONG_PTR>(line.FileName), frame.file, sizeof(frame.file));
    }
}

void CollectDetails(EXCEPTION_POINTERS *pointers)
{
    bool pdb = details.exactPdb;
    ZeroMemory(&details, sizeof(details)); details.exactPdb = pdb;
    EXCEPTION_POINTERS info = {};
    EXCEPTION_RECORD record = {};
    if (!ReadMemory(reinterpret_cast<ULONG_PTR>(pointers), &info, sizeof(info)) ||
        !ReadMemory(reinterpret_cast<ULONG_PTR>(info.ExceptionRecord), &record, sizeof(record)))
    { strcpy_s(details.kind, "unreadable_exception"); return; }
    details.code = record.ExceptionCode;
    strcpy_s(details.kind, ExceptionKind(record.ExceptionCode));
    if ((record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION || record.ExceptionCode == EXCEPTION_IN_PAGE_ERROR) &&
        record.NumberParameters >= 2)
    {
        strcpy_s(details.access, record.ExceptionInformation[0] == 0 ? "read" :
            record.ExceptionInformation[0] == 1 ? "write" : record.ExceptionInformation[0] == 8 ? "execute" : "unknown");
        details.accessAddress = record.ExceptionInformation[1];
        _snprintf_s(details.message, _TRUNCATE, "%s address=0x%08lX", details.access, details.accessAddress);
    }
    if (record.ExceptionCode == 0xE06D7363) DecodeCpp(record);
#if defined(_M_IX86)
    CONTEXT context = {};
    if (!ReadMemory(reinterpret_cast<ULONG_PTR>(info.ContextRecord), &context, sizeof(context))) return;
    AddFrame(context.Eip);
    STACKFRAME64 walk = {};
    walk.AddrPC.Offset = context.Eip; walk.AddrStack.Offset = context.Esp; walk.AddrFrame.Offset = context.Ebp;
    walk.AddrPC.Mode = walk.AddrStack.Mode = walk.AddrFrame.Mode = AddrModeFlat;
    DWORD64 lastPc = context.Eip, lastSp = context.Esp;
    for (unsigned attempt = 0; attempt < 64 && details.frameCount < 64 && pStackWalk64; ++attempt)
    {
        if (!pStackWalk64(IMAGE_FILE_MACHINE_I386, GetCurrentProcess(), crashThreadHandle,
                &walk, &context, StackRead, FunctionTable, ModuleBase, nullptr) || !walk.AddrPC.Offset) break;
        if (attempt && walk.AddrPC.Offset == lastPc && walk.AddrStack.Offset == lastSp) break;
        // Only the initial result can duplicate the supplied PC. Recursive calls
        // legitimately have identical return PCs but distinct stack pointers.
        if (attempt || walk.AddrPC.Offset != details.frames[0].address)
            AddFrame(static_cast<DWORD>(walk.AddrPC.Offset));
        lastPc = walk.AddrPC.Offset; lastSp = walk.AddrStack.Offset;
    }
#endif
    for (unsigned i = 0; i < details.frameCount; ++i)
    {
        const Frame &frame = details.frames[i];
        if (!frame.processImage) continue;
        if (frame.function[0])
            _snprintf_s(details.at, _TRUNCATE, "%s %s:%lu", frame.function, frame.file, frame.line);
        else _snprintf_s(details.at, _TRUNCATE, "%s+0x%08lX", frame.module, frame.rva);
        break;
    }
}

void PrepareSymbols()
{
    if (!pSymInitializeW || !pSymSetOptions || !pSymLoadModuleExW || !pSymGetModuleInfoW64) return;
    wchar_t image[MAX_PATH] = {}, folder[MAX_PATH] = {}, pdb[MAX_PATH] = {};
    DWORD length = GetModuleFileNameW(nullptr, image, MAX_PATH);
    if (!length || length == MAX_PATH) return;
    wcscpy_s(folder, image);
    wchar_t *slash = wcsrchr(folder, L'\\'); if (!slash) return; *slash = 0;
    _snwprintf_s(pdb, _TRUNCATE, L"%s\\EIRebuild.pdb", folder);
    // No invasion, no env paths, no deferred/automatic module loading or network.
    pSymSetOptions(SYMOPT_EXACT_SYMBOLS | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME |
        SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS | SYMOPT_IGNORE_NT_SYMPATH |
        SYMOPT_DISABLE_SYMSRV_AUTODETECT | SYMOPT_NO_UNQUALIFIED_LOADS);
    symbolsInitialized = pSymInitializeW(GetCurrentProcess(), folder, FALSE) != FALSE;
    if (!symbolsInitialized || GetFileAttributesW(pdb) == INVALID_FILE_ATTRIBUTES) return;
    // Supply the PDB itself, so an embedded absolute build path cannot select a
    // different file. DbgHelp validates its GUID/age against the loaded image.
    symbolBase = reinterpret_cast<ULONG_PTR>(GetModuleHandleW(nullptr));
    IMAGE_DOS_HEADER dos = {}; IMAGE_NT_HEADERS nt = {};
    if (!ReadMemory(static_cast<ULONG_PTR>(symbolBase), &dos, sizeof(dos)) ||
        dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 || dos.e_lfanew > 4096 ||
        !ReadMemory(static_cast<ULONG_PTR>(symbolBase) + dos.e_lfanew, &nt, sizeof(nt)) ||
        nt.Signature != IMAGE_NT_SIGNATURE) return;
    // GUID/age comes from the EXE's RSDS record, checked again after symbol load.
    GUID guid = {}; DWORD age = 0; bool rsds = false;
    IMAGE_DATA_DIRECTORY debug = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
    for (unsigned i = 0; i < debug.Size / sizeof(IMAGE_DEBUG_DIRECTORY) && i < 32; ++i)
    {
        IMAGE_DEBUG_DIRECTORY entry = {}; BYTE cv[24] = {};
        if (!ReadMemory(static_cast<ULONG_PTR>(symbolBase) + debug.VirtualAddress + i * sizeof(entry), &entry, sizeof(entry)) ||
            entry.Type != IMAGE_DEBUG_TYPE_CODEVIEW || entry.SizeOfData < sizeof(cv) ||
            !ReadMemory(static_cast<ULONG_PTR>(symbolBase) + entry.AddressOfRawData, cv, sizeof(cv)) || memcmp(cv, "RSDS", 4)) continue;
        memcpy(&guid, cv + 4, sizeof(guid)); memcpy(&age, cv + 20, sizeof(age)); rsds = true; break;
    }
    if (!rsds) return;
    DWORD64 loaded = pSymLoadModuleExW(GetCurrentProcess(), nullptr, pdb, nullptr, symbolBase, nt.OptionalHeader.SizeOfImage, nullptr, 0);
    IMAGEHLP_MODULEW64 module = {}; module.SizeOfStruct = sizeof(module);
    details.exactPdb = loaded == symbolBase && pSymGetModuleInfoW64(GetCurrentProcess(), symbolBase, &module) &&
        module.SymType == SymPdb && !memcmp(&guid, &module.PdbSig70, sizeof(guid)) && age == module.PdbAge &&
        !_wcsicmp(pdb, module.LoadedPdbName);
}

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

    void Text(const char *ansi)
    {
        wchar_t wide[1024] = {}; char utf8[3072] = {};
        int n = MultiByteToWideChar(1251, 0, ansi, -1, wide, 1024);
        if (n && WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, sizeof(utf8), nullptr, nullptr))
            Write(utf8, strlen(utf8));
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
    // UTF-8 title written as bytes, independent of the source-file code page.
    out.Print("\xD0\xA7\xD1\x82\xD0\xBE \xD1\x83\xD0\xBF\xD0\xB0\xD0\xBB\xD0\xBE / What crashed\r\n");
    out.Print("kind=%s code=0x%08lX\r\ntype=", details.kind, details.code);
    // Types may exceed one formatter line; emit them separately without truncation.
    out.Write(details.types, strlen(details.types));
    out.Print("\r\nwhat="); out.Text(details.message);
    out.Print("\r\nat="); out.Text(details.at); out.Print("\r\n");
    out.Print("Call stack (exception context, %u frames; exact adjacent PDB=%s):\r\n",
              details.frameCount, details.exactPdb ? "yes" : "no");
    for (unsigned i = 0; i < details.frameCount; ++i)
    {
        const Frame &frame = details.frames[i];
        out.Print("#%02u %s+0x%08lX pc=0x%08lX", i, frame.module, frame.rva, frame.address);
        if (frame.function[0]) { out.Print(" "); out.Text(frame.function); }
        if (frame.file[0]) { out.Print(" "); out.Text(frame.file); out.Print(":%lu", frame.line); }
        out.Print("\r\n");
    }
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
    info.ThreadId = crashThread;
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
    {
        writeDump = reinterpret_cast<MiniDumpWriteDumpFn>(GetProcAddress(dbghelp, "MiniDumpWriteDump"));
#define LOAD_DBG(name) p##name = reinterpret_cast<decltype(p##name)>(GetProcAddress(dbghelp, #name))
        LOAD_DBG(StackWalk64); LOAD_DBG(SymInitializeW); LOAD_DBG(SymCleanup);
        LOAD_DBG(SymSetOptions); LOAD_DBG(SymLoadModuleExW); LOAD_DBG(SymGetModuleInfoW64);
        LOAD_DBG(SymFromAddr); LOAD_DBG(SymGetLineFromAddr64);
        LOAD_DBG(SymFunctionTableAccess64); LOAD_DBG(UnDecorateSymbolName);
#undef LOAD_DBG
        __try { PrepareSymbols(); }
        __except (EXCEPTION_EXECUTE_HANDLER) { details.exactPdb = false; }
    }
}

void SetPatchRangeCallbacks(PatchRangeCountFn count, PatchRangeAtFn at)
{
    patchRangeCount = count;
    patchRangeAt = at;
}

void Shutdown()
{
    if (symbolsInitialized && pSymCleanup) pSymCleanup(GetCurrentProcess());
    symbolsInitialized = false;
    if (dbghelp)
        FreeLibrary(dbghelp);
    dbghelp = nullptr;
    writeDump = nullptr;
}

struct CaptureWork
{
    EXCEPTION_POINTERS *info;
    bool fullDump;
    RecentCountFn count;
    RecentLineFn line;
    Result *result;
};

DWORD WINAPI CaptureArtifacts(void *parameter)
{
    CaptureWork &work = *static_cast<CaptureWork *>(parameter);
    // Every stage is isolated. Preserve a usable dump before symbol/message work.
    __try { work.result->dumpWritten = WriteMinidump(work.info, work.fullDump,
        work.result->fallbackUsed, work.result->dumpError); }
    __except (EXCEPTION_EXECUTE_HANDLER) { work.result->dumpError = GetExceptionCode(); }
    __try { CollectDetails(work.info); }
    __except (EXCEPTION_EXECUTE_HANDLER) { strcpy_s(details.message, "extended diagnosis interrupted"); }
    __try { work.result->reportWritten = WriteReport(work.info, work.count, work.line, work.result->reportError); }
    __except (EXCEPTION_EXECUTE_HANDLER) { work.result->reportError = GetExceptionCode(); }
    return 0;
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
    crashThread = GetCurrentThreadId();
    crashThreadHandle = OpenThread(THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, crashThread);
    CaptureWork work = {exceptionInfo, fullDump, recentCount, recentLine, &result};
    DWORD code = 0;
    __try { code = exceptionInfo->ExceptionRecord->ExceptionCode; }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    // An exhausted stack cannot safely run DbgHelp. Keep the original context and
    // faulting thread ID while writing evidence on a fresh stack. Watchdog bounds it.
    HANDLE worker = code == EXCEPTION_STACK_OVERFLOW
        ? CreateThread(nullptr, 0, CaptureArtifacts, &work, 0, nullptr) : nullptr;
    if (worker)
    {
        WaitForSingleObject(worker, INFINITE);
        CloseHandle(worker);
    }
    else CaptureArtifacts(&work);
    if (crashThreadHandle) CloseHandle(crashThreadHandle);
    crashThreadHandle = nullptr;
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
const Details &LastDetails() { return details; }

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
