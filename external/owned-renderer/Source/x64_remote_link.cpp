#include "x64_remote_link.h"
#include <psapi.h>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

namespace owned {
void bridge_report_event(const char* event, const char* detail) noexcept;
std::wstring renderer_config_path();
}

namespace x64remote {
namespace {

constexpr std::int64_t kPublishBytes = 256 * 1024;
constexpr DWORD kHostStallMs = 20000;

struct Link {
    bool started = false;
    bool connected = false;     // the host runs (core mode: scene records, no device here)
    std::mutex scene_mutex;     // one scene message at a time (its fragments stay contiguous)
    bool attempt_cleared = false; // the start marker was removed after the first present
    std::wstring log_path;
    HANDLE mapping = nullptr;
    HANDLE work = nullptr, space = nullptr, reply = nullptr, present = nullptr;
    HANDLE host_process = nullptr;
    Control* control = nullptr;
    std::uint8_t* ring = nullptr;
    std::uint32_t ring_bytes = 0;
    std::uint8_t* readback = nullptr;   // readback area (synchronous replies)
    std::uint32_t readback_bytes = 0;
    std::uint32_t frames_ahead = 1;
    CRITICAL_SECTION ring_lock;
    std::int64_t write_pos = 0, published = 0; // ring lock
    std::atomic<long> removed{0};
    char lost_reason[160]{}; // written once, before removed is visible to readers of it
    std::mutex reply_mutex;
    std::uint64_t reply_serial = 0; // reply_mutex
    // Statistics (records/bytes under the ring lock, the rest atomic).
    std::uint64_t records = 0, bytes = 0, space_waits = 0;
    double space_wait_ms = 0;
    std::atomic<std::uint64_t> present_waits{0};
    double present_wait_ms = 0, readback_ms = 0; // present thread / reply mutex
    std::uint64_t frames = 0;
    std::int64_t report_qpc = 0;
    std::uint64_t report_records = 0, report_bytes = 0, report_host_frames = 0;
    double qpc_to_ms = 0;
};

Link& link() noexcept {
    static Link* value = [] {
        auto* l = new Link;
        InitializeCriticalSectionAndSpinCount(&l->ring_lock, 4000);
        LARGE_INTEGER frequency{};
        QueryPerformanceFrequency(&frequency);
        l->qpc_to_ms = 1000.0 / static_cast<double>(frequency.QuadPart);
        return l;
    }();
    return *value;
}

std::int64_t now_qpc() noexcept {
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return value.QuadPart;
}

void lose(const char* reason) noexcept {
    auto& l = link();
    long expected = 0;
    if (!l.removed.compare_exchange_strong(expected, static_cast<long>(DXGI_ERROR_DEVICE_REMOVED))) return;
    strncpy_s(l.lost_reason, reason, _TRUNCATE);
    log("render host lost: %s", reason);
    owned::bridge_report_event("x64_host_lost", reason);
}

bool host_alive() noexcept {
    auto& l = link();
    if (l.removed.load(std::memory_order_relaxed)) return false;
    if (!l.host_process || WaitForSingleObject(l.host_process, 0) == WAIT_OBJECT_0) { lose("the x64 host process exited"); return false; }
    if (const long hr = l.control->device_removed) {
        MemoryBarrier();
        char text[160];
        if (l.control->host_reason[0])
            std::snprintf(text, sizeof(text), "%.*s (%08lx)", int(sizeof(l.control->host_reason)), l.control->host_reason,
                          static_cast<unsigned long>(hr));
        else
            std::snprintf(text, sizeof(text), "the host reported device removal (%08lx)", static_cast<unsigned long>(hr));
        lose(text);
        return false;
    }
    return true;
}

// Waits until ready() holds, raising `flag` so the host signals `event`.
// timeout_ms 0: until the host dies.
template <class Ready> bool wait_until(Ready ready, volatile long& flag, HANDLE event, DWORD timeout_ms = 0) noexcept {
    const ULONGLONG deadline = timeout_ms ? GetTickCount64() + timeout_ms : 0;
    for (int spin = 0; spin < 64; ++spin) {
        if (ready()) return true;
        YieldProcessor();
    }
    for (;;) {
        if (ready()) return true;
        if (!host_alive()) return false;
        if (deadline && GetTickCount64() > deadline) return false;
        InterlockedExchange(&flag, 1);
        if (ready()) { InterlockedExchange(&flag, 0); return true; }
        WaitForSingleObject(event, 50);
        InterlockedExchange(&flag, 0);
    }
}

void publish_locked() noexcept {
    auto& l = link();
    if (l.write_pos == l.published) return;
    InterlockedExchange64(&l.control->write_pos, l.write_pos);
    l.published = l.write_pos;
    if (l.control->host_sleeping) SetEvent(l.work);
}

// False when the space never came (host stalled or gone): the caller must
// not write, because the bytes ahead are records the host has not consumed
// (writing them corrupted the record the host was executing: host crash of
// 2026-10-24 after a 20 s stall).
bool ensure_space(std::uint32_t bytes) noexcept {
    auto& l = link();
    const auto fits = [&] { return l.write_pos + bytes - l.control->read_pos <= std::int64_t(l.ring_bytes); };
    if (fits()) return true;
    publish_locked();
    const std::int64_t start = now_qpc();
    // A host that stops consuming (hung, not exited) fails the device
    // visibly instead of hanging the game.
    if (!wait_until(fits, l.control->space_waiting, l.space, kHostStallMs) && !l.removed.load())
        lose("the host stopped consuming commands for 20 s");
    ++l.space_waits;
    l.space_wait_ms += (now_qpc() - start) * l.qpc_to_ms;
    return fits();
}

thread_local std::vector<std::uint8_t> t_scratch; // records written after the link died
thread_local bool t_in_writer = false;            // a record is being written on this thread

bool env_flag(const wchar_t* name) noexcept {
    wchar_t value[8]{};
    return GetEnvironmentVariableW(name, value, 8) && value[0] == L'1';
}

unsigned env_number(const wchar_t* name, unsigned fallback, unsigned low, unsigned high) noexcept {
    wchar_t value[16]{};
    if (!GetEnvironmentVariableW(name, value, 16)) return fallback;
    const unsigned parsed = static_cast<unsigned>(wcstoul(value, nullptr, 10));
    return parsed < low ? low : parsed > high ? high : parsed;
}

} // namespace

std::wstring host_path() noexcept try {
    wchar_t host[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(L"EI_RENDERER_X64_HOST", host, MAX_PATH);
    if (length && length < MAX_PATH) return host;
    HMODULE module = nullptr;
    wchar_t self[MAX_PATH]{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&host_path), &module) ||
        !GetModuleFileNameW(module, self, MAX_PATH))
        return {};
    const std::wstring dll = self;
    return dll.substr(0, dll.find_last_of(L"\\/") + 1) + L"eid3d11_x64_host.exe";
} catch (...) {
    return {};
}

namespace {
// Start marker next to eid3d11.ini: written before the host is launched,
// removed once it presented its first frame. Still there at the next start:
// that attempt crashed or hung during init.
std::wstring attempt_marker() {
    const std::wstring ini = owned::renderer_config_path();
    return ini.substr(0, ini.find_last_of(L"\\/") + 1) + L"eid3d11_x64_host.pending";
}

void set_option_off() noexcept {
    WritePrivateProfileStringW(L"graphics", L"x64_render_host", L"false", owned::renderer_config_path().c_str());
}

char g_notice[320] = {};
ULONGLONG g_notice_until = 0; // set at the first query: the notice shows for 90 s

void leave_notice(const char* text) noexcept {
    strncpy_s(g_notice, text, _TRUNCATE);
    g_notice_until = 0;
}
} // namespace

const char* host_unavailable_reason() noexcept {
    static const char* reason = [] () -> const char* {
        // A 64-bit host needs a 64-bit (x64 or ARM64) Windows.
        USHORT process = 0, native = 0;
        using IsWow64Process2Fn = BOOL(WINAPI*)(HANDLE, USHORT*, USHORT*);
        const auto is_wow64 = reinterpret_cast<IsWow64Process2Fn>(
            GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "IsWow64Process2"));
        bool x64_capable = false;
        if (is_wow64 && is_wow64(GetCurrentProcess(), &process, &native)) {
            x64_capable = native == IMAGE_FILE_MACHINE_AMD64 || native == IMAGE_FILE_MACHINE_ARM64;
        } else {
            SYSTEM_INFO info{};
            GetNativeSystemInfo(&info);
            x64_capable = info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ||
                          info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64;
        }
        if (!x64_capable) return "needs 64-bit Windows";
        const std::wstring host = host_path();
        const DWORD attributes = host.empty() ? INVALID_FILE_ATTRIBUTES : GetFileAttributesW(host.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) return "64-bit host file missing";
        return nullptr;
    }();
    return reason;
}

bool core_wanted() noexcept try {
    const bool forced = core_requested();
    wchar_t value[16]{};
    GetPrivateProfileStringW(L"graphics", L"x64_render_host", L"false", value, 16, owned::renderer_config_path().c_str());
    const bool option = _wcsicmp(value, L"true") == 0 || _wcsicmp(value, L"1") == 0;
    if (!forced && !option) return false;
    const std::wstring marker = attempt_marker();
    if (GetFileAttributesW(marker.c_str()) != INVALID_FILE_ATTRIBUTES) {
        // Rollback: the last start never reached its first frame.
        DeleteFileW(marker.c_str());
        set_option_off();
        leave_notice("64-bit render host failed to start last time and was turned off (F10 > Display). "
                     "Using the standard renderer.");
        owned::bridge_report_event("x64_host_rolled_back", "the previous start left its marker; option turned off");
        return false;
    }
    if (const char* reason = host_unavailable_reason()) {
        set_option_off();
        char text[200];
        std::snprintf(text, sizeof(text), "64-bit render host unavailable (%s) and turned off. Using the standard renderer.", reason);
        leave_notice(text);
        owned::bridge_report_event("x64_host_unavailable", reason);
        return false;
    }
    // The marker goes down before the host is launched (core_ensure); the
    // first host frame removes it (note_first_present).
    const HANDLE file = CreateFileW(marker.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    return true;
} catch (...) {
    return false;
}

const char* remote_notice() noexcept {
    if (!g_notice[0]) return nullptr;
    const ULONGLONG now = GetTickCount64();
    if (!g_notice_until) g_notice_until = now + 90000;
    if (now > g_notice_until) { g_notice[0] = 0; return nullptr; }
    return g_notice;
}

// Called at every Present: the host reached its first frame, the start
// attempt succeeded.
void note_first_present() noexcept {
    auto& l = link();
    if (l.attempt_cleared || !l.control || l.control->presents_done < 1) return;
    l.attempt_cleared = true;
    DeleteFileW(attempt_marker().c_str());
    log("first host frame presented; start marker cleared");
}

void log(const char* format, ...) noexcept {
    auto& l = link();
    char text[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    SYSTEMTIME now{};
    GetLocalTime(&now);
    FILE* file = nullptr;
    if (!l.log_path.empty() && _wfopen_s(&file, l.log_path.c_str(), L"ab") == 0 && file) {
        std::fprintf(file, "%02u:%02u:%02u.%03u %s\n", now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, text);
        std::fclose(file);
    }
    OutputDebugStringA("EID3D11 x64 host link: ");
    OutputDebugStringA(text);
    OutputDebugStringA("\n");
}

// A named failure: the device is lost and the game closes at the next
// Present naming it.
void lose_device(const char* reason) noexcept { lose(reason); }

bool link_start(HWND game_window, UINT device_flags, char* error, std::size_t error_bytes) noexcept try {
    auto& l = link();
    const auto fail = [&](const char* text) {
        std::snprintf(error, error_bytes, "%s (%lu)", text, GetLastError());
        log("start failed: %s", error);
        // A host we launched that did not come up must not linger.
        if (l.host_process) { TerminateProcess(l.host_process, 1); CloseHandle(l.host_process); l.host_process = nullptr; }
        return false;
    };
    if (l.started) return fail("the render host was already started once");
    l.started = true;
    const std::wstring host_path = x64remote::host_path();
    if (host_path.empty()) return fail("the host executable path is unknown");
    l.log_path = host_path.substr(0, host_path.find_last_of(L"\\/") + 1) + L"x86_remote.log";
    l.ring_bytes = env_number(L"EI_RENDERER_X64_RING_MB", 32, 8, 256) << 20;
    l.frames_ahead = env_number(L"EI_RENDERER_X64_FRAMES_AHEAD", 1, 1, 3);
    l.readback_bytes = env_number(L"EI_RENDERER_X64_READBACK_MB", 4, 1, 64) << 20;
    const DWORD pid = GetCurrentProcessId();
    log("---- render host start: pid=%lu ring=%u MB readback=%u MB frames_ahead=%u control=%u KB", pid,
        l.ring_bytes >> 20, l.readback_bytes >> 20, l.frames_ahead, static_cast<unsigned>(sizeof(Control) >> 10));
    wchar_t name[96];
    const std::uint64_t total = sizeof(Control) + std::uint64_t(l.ring_bytes) + l.readback_bytes;
    swprintf_s(name, L"Local\\eid3d11_x64r_%lu", pid);
    l.mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, static_cast<DWORD>(total), name);
    if (!l.mapping) return fail("shared memory creation failed");
    auto* view = static_cast<std::uint8_t*>(MapViewOfFile(l.mapping, FILE_MAP_WRITE, 0, 0, static_cast<SIZE_T>(total)));
    if (!view) return fail("shared memory mapping failed");
    l.control = reinterpret_cast<Control*>(view);
    l.ring = view + sizeof(Control);
    l.readback = l.ring + l.ring_bytes;
    std::memset(l.control, 0, sizeof(Control));
    l.control->magic = kMagic;
    l.control->version = kVersion;
    l.control->ring_bytes = l.ring_bytes;
    l.control->control_bytes = sizeof(Control);
    l.control->requested_flags = device_flags;
    l.control->frames_ahead = l.frames_ahead;
    l.control->readback_bytes = l.readback_bytes;
    l.control->core_mode = 1u;
    const auto event = [&](const wchar_t* kind) {
        swprintf_s(name, L"Local\\eid3d11_x64r_%s_%lu", kind, pid);
        return CreateEventW(nullptr, FALSE, FALSE, name);
    };
    l.work = event(L"work");
    l.space = event(L"space");
    l.reply = event(L"reply");
    l.present = event(L"present");
    if (!l.work || !l.space || !l.reply || !l.present) return fail("event creation failed");
    wchar_t command[MAX_PATH + 96];
    swprintf_s(command, L"\"%s\" --remote --pid %lu --hwnd %llu", host_path.c_str(), pid,
               static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(game_window)));
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
        return fail("host launch failed");
    CloseHandle(process.hThread);
    l.host_process = process.hProcess;
    log("host launched pid=%lu; waiting for its device", process.dwProcessId);
    const ULONGLONG deadline = GetTickCount64() + 30000;
    while (!l.control->host_ready) {
        if (WaitForSingleObject(l.host_process, 10) == WAIT_OBJECT_0) {
            DWORD code = 0;
            GetExitCodeProcess(l.host_process, &code);
            SetLastError(code); // fail() prints it: the host's exit code here
            return fail("the host exited during startup (exit code in parentheses; see host.log)");
        }
        if (GetTickCount64() > deadline) return fail("the host did not report a device within 30 s (see host.log)");
    }
    if (l.control->host_ready != 1) {
        MemoryBarrier();
        char reason[sizeof(l.control->host_reason) + 1]{};
        std::memcpy(reason, l.control->host_reason, sizeof(l.control->host_reason));
        return fail(reason[0] ? reason : "the host could not create its D3D11 device (see host.log)");
    }
    char adapter[128]{};
    WideCharToMultiByte(CP_UTF8, 0, l.control->caps.adapter.description, -1, adapter, sizeof(adapter) - 1, nullptr, nullptr);
    char detail[256];
    std::snprintf(detail, sizeof(detail), "host pid=%lu adapter=%s feature_level=%x ring_mb=%u frames_ahead=%u",
                  process.dwProcessId, adapter, l.control->caps.feature_level, l.ring_bytes >> 20, l.frames_ahead);
    log("connected: %s", detail);
    owned::bridge_report_event("x64_host_connected", detail);
    l.connected = true;
    return true;
} catch (...) {
    std::snprintf(error, error_bytes, "allocation failed");
    return false;
}

Control& control() noexcept { return *link().control; }
const std::uint8_t* readback_area() noexcept { return link().readback; }
std::uint32_t readback_bytes() noexcept { return link().readback_bytes; }
std::uint32_t chunk_bytes() noexcept { return link().ring_bytes / 4; }
HRESULT link_removed() noexcept { return link().removed.load(std::memory_order_relaxed); }
bool core_requested() noexcept { return env_flag(L"EI_RENDERER_X64_CORE"); }
bool core_active() noexcept { return link().connected; }
void signal_clean_exit() noexcept {
    if (!core_active() || !link().control) return;
    MemoryBarrier();
    InterlockedExchange(&link().control->game_clean_exit, 1);
}
std::uint64_t host_private_bytes() noexcept {
    auto& l = link();
    if (!l.connected || !l.host_process) return 0;
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(l.host_process, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters)))
        return 0;
    return counters.PrivateUsage;
}

void scene_message(std::uint16_t command, const std::uint8_t* data, std::size_t bytes) noexcept try {
    auto& l = link();
    if (!l.connected) return;
    // Fragments far below a record's limit (a quarter of the ring).
    const std::size_t fragment = (std::min<std::size_t>)(std::size_t(1) << 20, chunk_bytes() - sizeof(SceneFragment));
    std::lock_guard<std::mutex> lock(l.scene_mutex);
    std::size_t at = 0;
    do {
        const std::size_t part = (std::min)(fragment, bytes - at);
        const SceneFragment header{static_cast<std::uint32_t>(part), command, at + part == bytes ? std::uint16_t(1) : std::uint16_t(0)};
        Writer w(Op::scene, static_cast<std::uint32_t>(sizeof(header) + part));
        w.put(header);
        if (part) w.put(data + at, static_cast<std::uint32_t>(part));
        at += part;
    } while (at < bytes);
} catch (...) {
    lose("a scene message could not be written (lock failure)");
}
void check_host() noexcept {
    if (!link().connected) return;
    host_alive();
    note_first_present();
}
const char* remote_lost_reason() noexcept {
    auto& l = link();
    return l.removed.load() && l.lost_reason[0] ? l.lost_reason : nullptr;
}
// DLSS runs in the host Renderer (host_bridge.cpp); the game process's
// Renderer never upscales with it (its Upscaling option names why).
bool dlss_caps(DlssCaps& out) noexcept {
    out = {};
    strncpy_s(out.reason, "needs the 64-bit render host", _TRUNCATE);
    return false;
}
bool dlss_render_size(UINT, UINT, int, UINT&, UINT&) noexcept { return false; }
HRESULT dlss_evaluate(ID3D11DeviceContext*, const DlssEvaluate&) noexcept { return E_NOTIMPL; }
void dlss_release() noexcept {}
bool host_presented() noexcept { return false; }

Writer::Writer(Op op, std::uint32_t payload_bytes) noexcept {
    auto& l = link();
    const std::uint32_t total = (static_cast<std::uint32_t>(sizeof(Record)) + payload_bytes + 7u) & ~7u;
    EnterCriticalSection(&l.ring_lock);
    // The lock is recursive: a record started inside another would
    // interleave the two. Never expected.
    if (t_in_writer) lose("a record was started inside another record (internal error)");
    t_in_writer = true;
    // Dropped records go to a scratch buffer: the device is gone, a chunking
    // bug (reported once), or the space never came (the device is lost).
    const auto drop = [&] {
        if (t_scratch.size() < total) t_scratch.resize(total);
        cursor_ = t_scratch.data() + sizeof(Record);
        end_ = l.write_pos;
    };
    if (l.removed.load(std::memory_order_relaxed) || total > l.ring_bytes / 2) {
        if (total > l.ring_bytes / 2) lose("a record exceeded half the ring (internal chunking error)");
        drop();
        return;
    }
    std::uint32_t offset = static_cast<std::uint32_t>(l.write_pos % l.ring_bytes);
    if (offset + total > l.ring_bytes) {
        const std::uint32_t pad = l.ring_bytes - offset;
        if (!ensure_space(pad)) { drop(); return; }
        auto* record = reinterpret_cast<Record*>(l.ring + offset);
        record->size = pad;
        record->op = static_cast<std::uint16_t>(Op::pad);
        record->reserved = 0;
        l.write_pos += pad;
        offset = 0;
    }
    if (!ensure_space(total)) { drop(); return; }
    auto* record = reinterpret_cast<Record*>(l.ring + offset);
    record->size = total;
    record->op = static_cast<std::uint16_t>(op);
    record->reserved = 0;
    cursor_ = l.ring + offset + sizeof(Record);
    l.write_pos += total;
    end_ = l.write_pos;
    ++l.records;
    l.bytes += total;
}

Writer::~Writer() {
    auto& l = link();
    t_in_writer = false;
    if (l.write_pos - l.published >= kPublishBytes) publish_locked();
    LeaveCriticalSection(&l.ring_lock);
}

void publish() noexcept {
    auto& l = link();
    if (!l.control) return;
    EnterCriticalSection(&l.ring_lock);
    publish_locked();
    LeaveCriticalSection(&l.ring_lock);
}

std::uint64_t next_reply_serial() noexcept {
    auto& l = link();
    l.reply_mutex.lock();
    return ++l.reply_serial;
}

HRESULT wait_reply(std::uint64_t serial) noexcept {
    auto& l = link();
    publish();
    const std::int64_t start = now_qpc();
    const bool ok = wait_until([&] { return l.control->reply_serial >= serial; }, l.control->reply_waiting, l.reply, kHostStallMs);
    if (!ok && !l.removed.load()) lose("the host did not answer a request for 20 s");
    const HRESULT hr = ok ? static_cast<HRESULT>(l.control->reply_hr) : static_cast<HRESULT>(DXGI_ERROR_DEVICE_REMOVED);
    l.readback_ms += (now_qpc() - start) * l.qpc_to_ms;
    return hr;
}

void end_reply() noexcept { link().reply_mutex.unlock(); }

void wait_presents(std::uint64_t serial) noexcept {
    auto& l = link();
    const auto ready = [&] { return l.control->presents_done + l.frames_ahead >= serial; };
    if (ready()) return;
    const std::int64_t start = now_qpc();
    // Bounded: a host present stuck behind this thread must not hang the game.
    if (!wait_until(ready, l.control->present_waiting, l.present, 2000) && !l.removed.load())
        log("waited 2 s for host present %llu (host at %llu); continuing", static_cast<unsigned long long>(serial),
            static_cast<unsigned long long>(l.control->presents_done));
    ++l.present_waits;
    l.present_wait_ms += (now_qpc() - start) * l.qpc_to_ms;
}

void report_frame() noexcept {
    auto& l = link();
    ++l.frames;
    const std::int64_t now = now_qpc();
    if (!l.report_qpc) { l.report_qpc = now; return; }
    const double elapsed = (now - l.report_qpc) * l.qpc_to_ms;
    if (elapsed < 2000.0) return;
    std::uint64_t records = 0, bytes = 0, space_waits = 0;
    double space_ms = 0;
    EnterCriticalSection(&l.ring_lock);
    records = l.records; bytes = l.bytes; space_waits = l.space_waits; space_ms = l.space_wait_ms;
    l.space_waits = 0; l.space_wait_ms = 0;
    LeaveCriticalSection(&l.ring_lock);
    const double frames = static_cast<double>(l.frames);
    const std::uint64_t host_frames = l.control->host_frames;
    log("per frame (%llu frames, %.1f/s): records=%.0f KB=%.1f | waits: ring=%llu (%.1f ms) ahead=%llu (%.2f ms/frame) "
        "readback=%.2f ms/frame | host_frames=%llu removed=%08lx",
        static_cast<unsigned long long>(l.frames), frames * 1000.0 / elapsed, (records - l.report_records) / frames,
        (bytes - l.report_bytes) / frames / 1024.0, static_cast<unsigned long long>(space_waits), space_ms,
        static_cast<unsigned long long>(l.present_waits.exchange(0)), l.present_wait_ms / frames, l.readback_ms / frames,
        static_cast<unsigned long long>(host_frames - l.report_host_frames), static_cast<unsigned long>(l.removed.load()));
    l.report_records = records;
    l.report_bytes = bytes;
    l.report_host_frames = host_frames;
    l.present_wait_ms = l.readback_ms = 0;
    l.frames = 0;
    l.report_qpc = now;
}

} // namespace x64remote
