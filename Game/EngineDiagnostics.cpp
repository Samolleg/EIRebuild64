#include "StdAfx.h"
#include "RuntimePaths64.h"
#include "GameJournal.h"
#include "ErrorDialog.h"
#include "EngineDiagnostics.h"
#include "SettingsProfile.h"
#include "../shared/Common.h"
#include "ModDiagnosticsPolicy.h"
#include "ModRuntime.h"
#include "ModPackage.h"
#include "ModConfiguration.h"
#include "Console.h"
#include "WorldServer.h"
#include "WorldInterface.h"
#include "Player.h"
#include "Party.h"
#include "FrameDiagnostics.h"
#include "CrashEvidence.h"
#include <cstdio>
#include <cstdarg>
#include <exception>
#include <cstdlib>
#include <filesystem>

namespace EngineDiagnostics {
namespace {
SRWLOCK lock = SRWLOCK_INIT;
char recent[24][200] = {}, snapshot[24][200] = {};
unsigned next = 0, count = 0, snapshotCount = 0;
bool silent = false;
bool fullDump = false;
LPTOP_LEVEL_EXCEPTION_FILTER previousHandler = nullptr;
volatile LONG handling = 0;
CFileLogger fileLog;
class ConsoleLogger : public CLogger {
public:
    bool Start(ELogLevel level) {
        WithTimestamp(); WithLevel(); SetShortTimestamp(true); SetShortLevel(true);
        SetSeparator(" "); return CLogger::Start(level);
    }
private:
    bool StartLocked() override { return true; }
    void StopLocked() override {}
    void FlushLocked() override {}
    void WriteLineLocked(const char *line, size_t) override { NConsole::game << line << std::endl; }
} consoleLog;
DWORD Setting(const wchar_t *name, DWORD fallback) {
    return SettingsProfile::Dword(L"Settings",name,fallback);
}

HANDLE runtimeLog = INVALID_HANDLE_VALUE;
volatile LONG eventCount = 0;
// Retain the existing typed reporting-point context beside the native logger ring.
size_t Count() { return fileLog.RecentLineCount() + snapshotCount; }
const char *Line(size_t i) {
    const size_t logged = fileLog.RecentLineCount();
    if (i < logged) return fileLog.RecentLine(i);
    i -= logged; return i < snapshotCount ? snapshot[i] : nullptr;
}
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
    Caught("CRT invalid parameter", 0xE0000136);
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
    fullDump = Setting(L"FullDump", 0) != 0;
    std::wstring starter = RuntimePaths64::Directory(L"Logs64").wstring();
    if (starter.empty()) starter = GetDirectoryName(GetCurrentModulePath());
    AddTrailingSlash(starter);
    fileLog.Start(starter + L"eirebuild64.log", L"a", CLogger::LevelFromInt(int(Setting(L"LogLevel", 1))));
    consoleLog.Start(CLogger::LevelFromInt(int(Setting(L"ConsoleLogLevel", 3))));
    std::wstring directory = starter + L"Dumps\\", path;
    if (CreateDirectoryW(directory.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS)
        path = GetUniqPath(directory, L"ei-", L".dmp");
    wchar_t logPath[MAX_PATH];
    _snwprintf_s(logPath, MAX_PATH, _TRUNCATE, L"%s.runtime.txt", path.c_str());
    runtimeLog = path.empty() ? INVALID_HANDLE_VALUE : CreateFileW(logPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    FrameDiagnostics::Configure(logPath);
    CrashEvidence::Configure(path.c_str(), "EIRebuild R135 " __DATE__ " " __TIME__);
    CrashEvidence::Prepare(GetModuleHandleW(nullptr));
    previousHandler = SetUnhandledExceptionFilter(Filter);
    std::set_terminate(FatalTerminate);
    _set_invalid_parameter_handler(InvalidParameter);
    ULONG stack = 32768; SetThreadStackGuarantee(&stack);
    Note("Diagnostics initialized");
    fileLog.Info("Main", "If game crashes, dump will be here: %s", WideToAnsi(path).c_str());
}
LONG WINAPI Filter(EXCEPTION_POINTERS *info) {
    if (InterlockedCompareExchange(&handling, 1, 0))
        return previousHandler ? previousHandler(info) : EXCEPTION_CONTINUE_SEARCH;
    const DWORD code = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0;
    fileLog.EnterCrashMode(); consoleLog.EnterCrashMode();
    FrameDiagnostics::Flush();
    Freeze();
    CrashEvidence::Result r = CrashEvidence::Capture(info, fullDump, Count, Line);
    if (r.alreadyHandling)
        return previousHandler ? previousHandler(info) : EXCEPTION_CONTINUE_SEARCH;
    GameJournal::Crash(CrashEvidence::DumpPath(), code, &CrashEvidence::LastDetails());
    GameJournal::FlushCritical();
    CrashEvidence::FinishHandling();
    if (!silent) {
        wchar_t text[1600];
        _snwprintf_s(text, 1600, _TRUNCATE,
            L"EIRebuild stopped after an error. Code: 0x%08lX\nReport: %s (error %lu)\nDump: %s (error %lu)",
            code, CrashEvidence::ReportPath(), r.reportError, CrashEvidence::DumpPath(), r.dumpError);
        ErrorDialog::ShowW(GetActiveWindow(), text, L"EIRebuild crash diagnostics", MB_OK | MB_ICONERROR, false);
    }
    ExitProcess(code);
}
void Caught(const char *text, DWORD code) {
    Note(text);
    Note("Caught exception: dump context is the reporting point, not the original throw");
    CONTEXT context = {}; RtlCaptureContext(&context);
    EXCEPTION_RECORD record = {}; record.ExceptionCode = code;
#ifdef _M_IX86
    record.ExceptionAddress = reinterpret_cast<void *>(context.Eip);
#endif
    EXCEPTION_POINTERS info = { &record, &context };
    Filter(&info);
}
}

namespace EngineDiagnostics {
void Event(const char *format, ...) {
    FrameDiagnostics::Scope timing(FrameDiagnostics::DiagnosticEvent);
    char text[768], line[820]; va_list args; va_start(args, format);
    _vsnprintf_s(text, sizeof(text), _TRUNCATE, format, args); va_end(args);
    GameJournal::Event("DIAG", "diagnostic_event", "text=%s", GameJournal::Quote(text).c_str());
    if (InterlockedIncrement(&eventCount) > 1024) return;
    Note(text);
    int n = _snprintf_s(line, sizeof(line), _TRUNCATE, "[%lu] %s\r\n", GetTickCount(), text);
    if (runtimeLog != INVALID_HANDLE_VALUE && n > 0) {
        DWORD written; WriteFile(runtimeLog, line, n, &written, nullptr); FlushFileBuffers(runtimeLog);
    }
}
}

namespace EngineDiagnostics {
bool AntiCrashEnabled() {
    auto *mod = ModRuntime::Resources();
    return mod && mod->CombatSetting("AntiCrash") != 0;
}
void Report(const char *text, bool error) {
    GameJournal::Event("ERROR", "report", "is_error=%d text=%s", error, GameJournal::Quote(text).c_str());
    if (error) {
        fileLog.Error("Patch", "%s", text); fileLog.Flush();
        consoleLog.Error("", "%s", text);
    } else {
        fileLog.Warning("Notification", "%s", text);
        consoleLog.Warning("", "%s", text);
    }
}
void ItemCreationError(const char *reason, const char *name) {
    GameJournal::Event("ERROR", "item_creation", "reason=%s name=%s", GameJournal::Quote(reason).c_str(), GameJournal::Quote(name).c_str());
    consoleLog.Error("", "GiveItem(): %s (\"%s\")", reason, name);
}
int GetLogLevel(bool console) {
    return static_cast<int>(console ? consoleLog.GetLevel() : fileLog.GetLevel());
}
void SetLogLevel(bool console, int level) {
    const auto value = CLogger::LevelFromInt(level);
    if (console) consoleLog.SetLevel(value);
    else fileLog.SetLevel(value);
}
}
