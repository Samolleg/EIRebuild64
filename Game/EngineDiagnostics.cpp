#include "EngineDiagnostics.h"
#include "CrashEvidence.h"
#include <cstdio>
#include <cstdarg>
#include <exception>
#include <cstdlib>

namespace EngineDiagnostics {
namespace {
SRWLOCK lock = SRWLOCK_INIT;
char recent[24][200] = {}, snapshot[24][200] = {};
unsigned next = 0, count = 0, snapshotCount = 0;
bool silent = false;
HANDLE runtimeLog = INVALID_HANDLE_VALUE;
volatile LONG eventCount = 0;
size_t Count() { return snapshotCount; }
const char *Line(size_t i) { return i < snapshotCount ? snapshot[i] : nullptr; }
void Freeze() {
    if (!TryAcquireSRWLockShared(&lock)) return;
    snapshotCount = count;
    for (unsigned i = 0; i < count; ++i)
        memcpy(snapshot[i], recent[(next + 24 - count + i) % 24], 200);
    ReleaseSRWLockShared(&lock);
}
void FatalTerminate() {
    Caught("std::terminate: unhandled C++ exception or fatal exception during unwind");
    TerminateProcess(GetCurrentProcess(), 0xE0000135);
}
void InvalidParameter(const wchar_t *, const wchar_t *, const wchar_t *, unsigned, uintptr_t) {
    Caught("CRT invalid parameter");
    TerminateProcess(GetCurrentProcess(), 0xE0000136);
}
}
void Note(const char *text) {
    if (!text || !TryAcquireSRWLockExclusive(&lock)) return;
    _snprintf_s(recent[next], 200, _TRUNCATE, "[%lu] %s", GetTickCount(), text);
    next = (next + 1) % 24;
    if (count < 24) ++count;
    ReleaseSRWLockExclusive(&lock);
}
void Initialize(bool quiet) {
    silent = quiet;
    wchar_t directory[MAX_PATH] = {}, path[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", directory, MAX_PATH);
    if (!n || n >= MAX_PATH - 80) GetTempPathW(MAX_PATH, directory);
    wcscat_s(directory, L"\\EIRebuild"); CreateDirectoryW(directory, nullptr);
    wcscat_s(directory, L"\\Crashes"); CreateDirectoryW(directory, nullptr);
    SYSTEMTIME t; GetLocalTime(&t);
    _snwprintf_s(path, MAX_PATH, _TRUNCATE,
        L"%s\\EIRebuild-%04u%02u%02u-%02u%02u%02u-%03u-%lu.dmp",
        directory, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond,
        t.wMilliseconds, GetCurrentProcessId());
    wchar_t logPath[MAX_PATH];
    _snwprintf_s(logPath, MAX_PATH, _TRUNCATE, L"%s.runtime.txt", path);
    runtimeLog = CreateFileW(logPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    CrashEvidence::Configure(path, "EIRebuild R135 " __DATE__ " " __TIME__);
    CrashEvidence::Prepare(GetModuleHandleW(nullptr));
    SetUnhandledExceptionFilter(Filter);
    std::set_terminate(FatalTerminate);
    _set_invalid_parameter_handler(InvalidParameter);
    ULONG stack = 32768; SetThreadStackGuarantee(&stack);
    Note("Diagnostics initialized");
}
LONG WINAPI Filter(EXCEPTION_POINTERS *info) {
    Freeze();
    CrashEvidence::Result r = CrashEvidence::Capture(info, false, Count, Line);
    CrashEvidence::FinishHandling();
    if (!r.alreadyHandling && !silent) {
        wchar_t text[1200];
        _snwprintf_s(text, 1200, _TRUNCATE,
            L"EIRebuild stopped after an error.\nReport: %s (error %lu)\nDump: %s (error %lu)",
            CrashEvidence::ReportPath(), r.reportError, CrashEvidence::DumpPath(), r.dumpError);
        MessageBoxW(nullptr, text, L"EIRebuild crash diagnostics", MB_OK | MB_ICONERROR);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
void Caught(const char *text) {
    Note(text);
    Note("Caught exception: dump context is the reporting point, not the original throw");
    CONTEXT context = {}; RtlCaptureContext(&context);
    EXCEPTION_RECORD record = {}; record.ExceptionCode = 0xE0000135;
#ifdef _M_IX86
    record.ExceptionAddress = reinterpret_cast<void *>(context.Eip);
#endif
    EXCEPTION_POINTERS info = { &record, &context };
    Filter(&info);
}
}

namespace EngineDiagnostics {
void Event(const char *format, ...) {
    if (InterlockedIncrement(&eventCount) > 1024) return;
    char text[768], line[820]; va_list args; va_start(args, format);
    _vsnprintf_s(text, sizeof(text), _TRUNCATE, format, args); va_end(args);
    Note(text);
    int n = _snprintf_s(line, sizeof(line), _TRUNCATE, "[%lu] %s\r\n", GetTickCount(), text);
    if (runtimeLog != INVALID_HANDLE_VALUE && n > 0) {
        DWORD written; WriteFile(runtimeLog, line, n, &written, nullptr); FlushFileBuffers(runtimeLog);
    }
}
}
