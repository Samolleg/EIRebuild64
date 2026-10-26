// Only included in the instrumented host; normal Release has no ASan imports.
#if defined(__SANITIZE_ADDRESS__)
#include <sanitizer/asan_interface.h>

namespace ASanReporting64 {
namespace {
wchar_t coePath[32768]{};

// ASan reads COE_LOG_FILE while initializing, before WinMain/CRT filesystem code.
// Use the same <game>/Engine64 layout as RuntimePaths64, only Win32 and BSS here.
__declspec(no_sanitize_address) void PrepareCOEPath() noexcept {
    DWORD n = GetModuleFileNameW(nullptr, coePath, _countof(coePath));
    if (!n || n >= _countof(coePath)) { coePath[0] = 0; return; }
    unsigned separators = 0;
    while (n && separators != 2) {
        --n;
        if (coePath[n] == L'\\' || coePath[n] == L'/') ++separators;
    }
    if (separators != 2 || n + 100 >= _countof(coePath)) { coePath[0] = 0; return; }
    const wchar_t* suffix = L"\\Logs64\\asan";
    while (*suffix) {
        if (*suffix == L'\\') {
            coePath[n] = 0;
            CreateDirectoryW(coePath, nullptr);
        }
        coePath[n++] = *suffix++;
    }
    coePath[n] = 0;
    CreateDirectoryW(coePath, nullptr);
    suffix = L"\\coe-";
    while (*suffix) coePath[n++] = *suffix++;
    const DWORD id[] = {GetCurrentProcessId(), GetTickCount()};
    for (DWORD value : id) {
        for (int shift = 28; shift >= 0; shift -= 4)
            coePath[n++] = L"0123456789abcdef"[(value >> shift) & 15];
        coePath[n++] = L'-';
    }
    --n;
    suffix = L".txt";
    while (*suffix) coePath[n++] = *suffix++;
    coePath[n] = 0;
    if (!SetEnvironmentVariableW(L"COE_LOG_FILE", coePath)) coePath[0] = 0;
}
}
}

extern "C" __declspec(no_sanitize_address)
const char* __cdecl __asan_default_options() {
    ASanReporting64::PrepareCOEPath();
    // COE owns duplicate-stack suppression and exact Raw HitCnt statistics.
    // MSVC disables suppress_equal_pcs in this mode; do not fight that policy.
    return "halt_on_error=0:continue_on_error=2:symbolize=1";
}

namespace ASanReporting64 {
namespace {
wchar_t reportPath[32768]{};
char runtimePath[32768]{};
volatile LONG reporting = 0;
struct SeenReport { void* pc; unsigned long long count; unsigned long long lastTotal; };
SeenReport seen[4096]{};
size_t seenCount = 0;

// No allocations or journal queue while ASan holds its reporting locks.
__declspec(no_sanitize_address) DWORD Append(const char* text) noexcept {
    if (!reportPath[0]) return ERROR_PATH_NOT_FOUND;
    HANDLE file = CreateFileW(reportPath, FILE_APPEND_DATA, FILE_SHARE_READ,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (file == INVALID_HANDLE_VALUE) return GetLastError();
    size_t remaining = 0;
    while (text[remaining]) ++remaining;
    DWORD error = ERROR_SUCCESS;
    while (remaining) {
        const DWORD chunk = remaining > MAXDWORD ? MAXDWORD : static_cast<DWORD>(remaining);
        DWORD written = 0;
        if (!WriteFile(file, text, chunk, &written, nullptr) || !written) {
            error = GetLastError();
            if (!error) error = ERROR_WRITE_FAULT;
            break;
        }
        text += written;
        remaining -= written;
    }
    if (!FlushFileBuffers(file) && !error) error = GetLastError();
    CloseHandle(file);
    return error;
}

__declspec(no_sanitize_address) void CountLine(void* pc, unsigned long long count,
                                              bool final) noexcept {
    char line[192];
    sprintf_s(line, "\r\nASAN_%s pc=%p callback_count=%llu (raw hits: COE transcript)\r\n",
        final ? "TOTAL" : "REPEAT", pc, count);
    const DWORD error = Append(line);
    GameJournal::ASanReport(reportPath, error, pc, count, true);
}

__declspec(no_sanitize_address) void Report(const char* text) {
    if (!text || InterlockedCompareExchange(&reporting, 1, 0)) return;
    void* pc = __asan_get_report_pc();
    SeenReport* entry = nullptr;
    // Unknown PCs are not collapsed: fatal reports can lack a generic-error PC.
    if (pc) {
        for (size_t i = 0; i < seenCount; ++i)
            if (seen[i].pc == pc) { entry = &seen[i]; break; }
    }
    if (entry) {
        const auto count = ++entry->count;
        // At most 64 tiny updates per PC, even for a hot recurring error.
        if ((count & (count - 1)) == 0) CountLine(pc, count, false);
    } else {
        if (pc && seenCount < _countof(seen)) seen[seenCount++] = {pc, 1, 0};
        // A full fixed table degrades to full reports rather than losing errors.
        DWORD error = Append("\r\n================ ASAN REPORT ================\r\n");
        const DWORD bodyError = Append(text);
        if (!error) error = bodyError;
        GameJournal::ASanReport(reportPath, error, pc, 1, false);
    }
    // COE returns to the application: a one-shot latch would discard later bugs.
    InterlockedExchange(&reporting, 0);
}
}

void Initialize() noexcept {
    // Independent runtime log_path; COE transcript additionally contains native
    // duplicate counts even when the runtime suppresses a callback/report.
    try {
        wchar_t local[32768];
        const DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", local, 32768);
        if (size && size < 32768) {
            const auto directory = std::filesystem::path(local) / L"EIRebuild64" / L"asan";
            std::filesystem::create_directories(directory);
            wchar_t shortPath[32768];
            const DWORD n = GetShortPathNameW(directory.c_str(), shortPath, 32768);
            const auto prefix = (n && n < 32768 ? std::filesystem::path(shortPath) : directory) / L"asan";
            BOOL lossy = FALSE;
            if (WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, prefix.c_str(), -1,
                    runtimePath, sizeof(runtimePath), nullptr, &lossy) && !lossy)
                __sanitizer_set_report_path(runtimePath);
            else runtimePath[0] = 0;
        }
    } catch (...) { runtimePath[0] = 0; }
    try {
        const auto directory = RuntimePaths64::Directory(L"Logs64") / L"asan";
        std::filesystem::create_directories(directory);
        SYSTEMTIME t; GetLocalTime(&t);
        wchar_t name[100];
        swprintf_s(name, L"asan-%04u%02u%02u-%02u%02u%02u-%lu.txt",
            t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, GetCurrentProcessId());
        const auto path = directory / name;
        if (path.native().size() < _countof(reportPath))
            wcscpy_s(reportPath, path.c_str());
    } catch (...) { reportPath[0] = 0; }
    // WinMain follows VCAsan's CRT initializer; replace its IDE exception callback
    // only after preparing the destination for reports.
    __asan_set_error_report_callback(Report);
}

__declspec(no_sanitize_address) void Finish() noexcept {
    if (InterlockedCompareExchange(&reporting, 1, 0)) return;
    for (size_t i = 0; i < seenCount; ++i)
        if (seen[i].count > 1 && seen[i].lastTotal != seen[i].count) {
            CountLine(seen[i].pc, seen[i].count, true);
            seen[i].lastTotal = seen[i].count;
        }
    InterlockedExchange(&reporting, 0);
}

void JournalReady() noexcept {
    GameJournal::Event("SESSION", "asan_reporting",
        "callback=true continue_on_error=2 report_path_ready=%u coe_path_ready=%u runtime_log_path=%s",
        reportPath[0] ? 1u : 0u, coePath[0] ? 1u : 0u, runtimePath[0] ? runtimePath : "unavailable");
}
}
#endif
