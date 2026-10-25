#pragma once

#include <windows.h>
#include <cstddef>

namespace CrashEvidence
{
// Fixed storage; the fatal path must not depend on allocating a C++ container.
struct Frame
{
    ULONG_PTR address;
    DWORD rva, line;
    bool processImage;
    char module[128], function[256], file[384];
};
struct Details
{
    DWORD code;
    char kind[64], types[2048], message[1024], at[768];
    char access[16];
    ULONG_PTR accessAddress;
    unsigned frameCount;
    bool exactPdb;
    Frame frames[64];
};
const Details &LastDetails();
struct Result
{
    bool reportWritten;
    bool dumpWritten;
    bool fallbackUsed;
    bool alreadyHandling;
    bool watchdogStarted;
    DWORD reportError;
    DWORD dumpError;
};

typedef size_t (*RecentCountFn)();
typedef const char *(*RecentLineFn)(size_t);
struct PatchRange
{
    const void *address;
    size_t size;
    const char *owner;
    bool write;
};
typedef size_t (*PatchRangeCountFn)();
typedef bool (*PatchRangeAtFn)(size_t, PatchRange *);

// Configuration is safe under the loader lock; library loading and hashing are deferred.
void Configure(const wchar_t *dumpPath, const char *buildVersion);
void Prepare(HMODULE addonModule);
void SetPatchRangeCallbacks(PatchRangeCountFn count, PatchRangeAtFn at);
void Shutdown();
Result Capture(EXCEPTION_POINTERS *exceptionInfo, bool fullDump,
               RecentCountFn recentCount, RecentLineFn recentLine);
// Stop the processing watchdog before the intentional modal error message.
void FinishHandling();
const wchar_t *ReportPath();
const wchar_t *DumpPath();

#ifdef CRASH_EVIDENCE_TEST
void SetTestFailures(bool reportCreate, bool primaryDump, bool fallbackDump, bool reportWrite);
void SetTestDumpException(bool enabled);
void SetTestWatchdog(DWORD timeoutMs, DWORD hangMs);
#endif
}
