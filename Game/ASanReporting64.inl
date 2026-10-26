// Only included in the instrumented host; normal Release has no ASan imports.
#if defined(__SANITIZE_ADDRESS__)
#include <sanitizer/asan_interface.h>

extern "C" __declspec(no_sanitize_address)
const char* __cdecl __asan_default_options() {
    // MSVC uses continue_on_error; halt_on_error alone is not sufficient.
    return "halt_on_error=1:continue_on_error=0:symbolize=1";
}

namespace ASanReporting64 {
namespace {
wchar_t reportPath[32768]{};
char runtimePath[32768]{};
volatile LONG reporting = 0;

// No filesystem/STL allocations, journal queue, or instrumented memory accesses
// while ASan holds its reporting locks. Preserve the entire supplied string.
__declspec(no_sanitize_address) void Report(const char* text) {
    if (!text || InterlockedCompareExchange(&reporting, 1, 0)) return;
    DWORD error = ERROR_PATH_NOT_FOUND;
    if (reportPath[0]) {
        HANDLE file = CreateFileW(reportPath, GENERIC_WRITE, FILE_SHARE_READ,
            nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            size_t remaining = 0;
            while (text[remaining]) ++remaining;
            error = ERROR_SUCCESS;
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
        } else error = GetLastError();
    }
    // The report is already flushed before touching the journal.
    GameJournal::ASanReport(reportPath, error);
}
}

void Initialize() noexcept {
    // Set the runtime's log_path directly, before registering the callback.
    // Bypass flag parsing, including the drive colon, spaces and quotes.
    // ASan writes runtimePath.<pid> before invoking Report, independently of it.
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
    // WinMain follows VCAsan's CRT initializer, so our callback owns this slot.
    __asan_set_error_report_callback(Report);
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
}

void JournalReady() noexcept {
    GameJournal::Event("SESSION", "asan_reporting",
        "callback=true report_path_ready=%u runtime_log_path=%s",
        reportPath[0] ? 1u : 0u, runtimePath[0] ? runtimePath : "unavailable");
}
}
#endif
