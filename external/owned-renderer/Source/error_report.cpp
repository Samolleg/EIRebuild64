// Error report package: see error_report.h.
//
// The process that survives builds it: after a device removal the game
// process is alive (its device-lost message runs on its own thread, the game
// thread waits), so it asks right there; after a crash nothing of ours may run
// reliably (the addon's own handler writes the dump and shows its dialog), so
// the next start asks once when that session left a dump. The files are
// copied into a staging folder, the user profile path in text files replaced
// by %USERPROFILE%, and the folder zipped with the tar.exe Windows ships
// (Windows 10 1803 and later); without it the folder stays and is named.
#include "error_report.h"

#include "build_version.h"
#include "debug_overlay_config.h"

#include <bcrypt.h>
#include <dxgi1_4.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <mutex>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace owned {
void bridge_async_flush(unsigned timeout_ms) noexcept;  // engine_bridge.cpp; a no-op stub in the x64 host
namespace {

using Microsoft::WRL::ComPtr;

std::mutex g_status_lock;
std::string g_status_line;  // the renderer's latest memory_status line

std::wstring folder_of(const std::wstring& path) { return path.substr(0, path.find_last_of(L"\\/") + 1); }

std::wstring module_path(HMODULE module) {
    std::vector<wchar_t> path(32768);
    const DWORD length = GetModuleFileNameW(module, path.data(), DWORD(path.size()));
    return length && length < path.size() ? std::wstring(path.data(), length) : std::wstring();
}

std::wstring dll_path() {
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&offer_error_report), &module);
    return module_path(module);
}

std::wstring environment(const wchar_t* name) {
    wchar_t value[32768];
    const DWORD length = GetEnvironmentVariableW(name, value, 32768);
    return length && length < 32768 ? std::wstring(value, length) : std::wstring();
}

// Reports go beside the game ("EID3D11 reports" next to Engine) so a tester
// finds them without digging (on a Steam Deck the AppData folder is deep in
// the Proton prefix). A game folder that cannot be written (Program Files)
// falls back to %LOCALAPPDATA%\EID3D11\reports.
std::wstring game_reports_folder() {
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&game_reports_folder), &self)) return {};
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(self, path, MAX_PATH);
    if (!length || length >= MAX_PATH) return {};
    std::wstring folder(path, length);
    for (int up = 0; up < 2; ++up) {  // the module's folder (Engine), then its parent
        const auto slash = folder.find_last_of(L"\\/");
        if (slash == std::wstring::npos) return {};
        folder.resize(slash);
    }
    folder += L"\\EID3D11 reports";
    if (!CreateDirectoryW(folder.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return {};
    const std::wstring probe = folder + L"\\.write-test";
    const HANDLE file = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    CloseHandle(file);
    return folder + L"\\";
}

std::wstring reports_folder() {
    if (std::wstring beside = game_reports_folder(); !beside.empty()) return beside;
    const std::wstring local = environment(L"LOCALAPPDATA");
    if (local.empty()) return {};
    CreateDirectoryW((local + L"\\EID3D11").c_str(), nullptr);
    CreateDirectoryW((local + L"\\EID3D11\\reports").c_str(), nullptr);
    return local + L"\\EID3D11\\reports\\";
}

bool newer(const FILETIME& a, const FILETIME& b) { return CompareFileTime(&a, &b) > 0; }

// Where F11 captures go (render_profiler: EI_RENDERER_DEBUG_DIR, else %LOCALAPPDATA%\EID3D11).
std::wstring captures_folder() {
    const std::wstring debug = environment(L"EI_RENDERER_DEBUG_DIR");
    return (debug.empty() ? environment(L"LOCALAPPDATA") + L"\\EID3D11" : debug) + L"\\captures\\";
}

// The newest file matching pattern in folder written after `after` (zero: any).
std::wstring newest_file(const std::wstring& folder, const wchar_t* pattern, FILETIME after, bool directories = false) {
    WIN32_FIND_DATAW found{};
    const HANDLE search = FindFirstFileW((folder + pattern).c_str(), &found);
    if (search == INVALID_HANDLE_VALUE) return {};
    std::wstring best;
    FILETIME best_time = after;
    do {
        const bool directory = (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (directory != directories || found.cFileName[0] == L'.') continue;
        if (newer(found.ftLastWriteTime, best_time)) { best_time = found.ftLastWriteTime; best = folder + found.cFileName; }
    } while (FindNextFileW(search, &found));
    FindClose(search);
    return best;
}

FILETIME seconds_ago(std::uint64_t seconds) {
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER value{};
    value.LowPart = now.dwLowDateTime;
    value.HighPart = now.dwHighDateTime;
    value.QuadPart -= seconds * 10000000ull;
    return {value.LowPart, value.HighPart};
}

// The newest crash dump written after `after`: the render host's beside the
// renderer, else the addon's ei-*.dmp (or only its .dmp.txt description) in
// Dumps\ of the game.exe folder or of the folder above it (the addon writes
// it beside the Engine folder).
std::wstring newest_dump(FILETIME after) {
    std::wstring dump = newest_file(folder_of(dll_path()), L"host_crash_*.dmp", after);
    if (!dump.empty()) return dump;
    std::wstring game = folder_of(module_path(nullptr));
    for (int level = 0; level < 2 && dump.empty(); ++level) {
        dump = newest_file(game + L"Dumps\\", L"ei-*.dmp*", after);
        if (game.size() > 1) game = folder_of(game.substr(0, game.size() - 1));
    }
    return dump;
}

// A capture image as PNG through WIC (Windows' own codecs): the 4K BMPs are
// 33 MB each, a PNG of the same frame a few MB. Literal GUIDs: no codec
// library to link.
bool write_png(const std::wstring& bmp, const std::wstring& png) {
    static const GUID factory_class = {0xcacaf262, 0x9370, 0x4615, {0xa1, 0x3b, 0x9f, 0x55, 0x39, 0xda, 0x4c, 0x0a}};  // CLSID_WICImagingFactory1
    static const GUID png_container = {0x1b7cfaf4, 0x713f, 0x473c, {0xbb, 0xcd, 0x61, 0x37, 0x42, 0x5f, 0xae, 0xaf}};  // GUID_ContainerFormatPng
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool ok = false;
    {
        ComPtr<IWICImagingFactory> factory;
        ComPtr<IWICBitmapDecoder> decoder;
        ComPtr<IWICBitmapFrameDecode> frame;
        ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapEncoder> encoder;
        ComPtr<IWICBitmapFrameEncode> out;
        ok = SUCCEEDED(CoCreateInstance(factory_class, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
             SUCCEEDED(factory->CreateDecoderFromFilename(bmp.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)) &&
             SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(factory->CreateStream(&stream)) &&
             SUCCEEDED(stream->InitializeFromFilename(png.c_str(), GENERIC_WRITE)) &&
             SUCCEEDED(factory->CreateEncoder(png_container, nullptr, &encoder)) &&
             SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
             SUCCEEDED(encoder->CreateNewFrame(&out, nullptr)) && SUCCEEDED(out->Initialize(nullptr)) &&
             SUCCEEDED(out->WriteSource(frame.Get(), nullptr)) && SUCCEEDED(out->Commit()) && SUCCEEDED(encoder->Commit());
    }
    if (SUCCEEDED(com)) CoUninitialize();
    if (!ok) DeleteFileW(png.c_str());
    return ok;
}

std::string read_file(const std::wstring& path, std::uint64_t tail_limit) {
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    LARGE_INTEGER size{};
    GetFileSizeEx(file, &size);
    std::uint64_t bytes = std::uint64_t(size.QuadPart);
    if (tail_limit && bytes > tail_limit) {
        LARGE_INTEGER start{};
        start.QuadPart = LONGLONG(bytes - tail_limit);
        SetFilePointerEx(file, start, nullptr, FILE_BEGIN);
        bytes = tail_limit;
    }
    std::string data;
    try { data.resize(std::size_t(bytes)); } catch (...) { CloseHandle(file); return {}; }
    DWORD read = 0;
    std::size_t done = 0;
    while (done < data.size() && ReadFile(file, &data[done], DWORD((std::min<std::size_t>)(data.size() - done, 1u << 24)), &read, nullptr) && read)
        done += read;
    data.resize(done);
    CloseHandle(file);
    return data;
}

bool write_file(const std::wstring& path, const std::string& data) {
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    std::size_t done = 0;
    while (done < data.size() && WriteFile(file, data.data() + done, DWORD((std::min<std::size_t>)(data.size() - done, 1u << 24)), &written, nullptr) && written)
        done += written;
    CloseHandle(file);
    return done == data.size();
}

std::string narrow(const std::wstring& text) {
    if (text.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(std::size_t(length > 0 ? length : 0), '\0');
    if (length > 0) WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()), &out[0], length, nullptr, nullptr);
    return out;
}

// A log or settings file into the package: its last 64 MB, the user
// profile path (and the account name inside it) replaced.
void copy_text(const std::wstring& source, const std::wstring& folder, std::string& listed, std::uint64_t tail = 64ull << 20) {
    if (GetFileAttributesW(source.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    std::string data = read_file(source, tail);
    for (const std::string& profile : {narrow(environment(L"USERPROFILE")), [] {
             char ansi[MAX_PATH]{};
             return GetEnvironmentVariableA("USERPROFILE", ansi, MAX_PATH) ? std::string(ansi) : std::string();
         }()}) {
        if (profile.size() < 4) continue;
        for (std::size_t at = data.find(profile); at != std::string::npos; at = data.find(profile, at + 13))
            data.replace(at, profile.size(), "%USERPROFILE%");
    }
    const std::wstring name = source.substr(source.find_last_of(L"\\/") + 1);
    if (write_file(folder + name, data)) listed += "  " + narrow(name) + "\n";
}

void copy_binary(const std::wstring& source, const std::wstring& target, std::string& listed) {
    if (source.empty()) return;
    if (CopyFileW(source.c_str(), target.c_str(), FALSE)) listed += "  " + narrow(target.substr(target.find_last_of(L"\\/") + 1)) + "\n";
}

std::string sha256(const std::wstring& path) {
    const std::string data = read_file(path, 0);
    if (data.empty()) return "unavailable";
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    unsigned char digest[32]{};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return "unavailable";
    const bool ok = BCryptHash(algorithm, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())), ULONG(data.size()),
                               digest, sizeof(digest)) == 0;
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!ok) return "unavailable";
    char text[65];
    for (int i = 0; i < 32; ++i) std::snprintf(text + i * 2, 3, "%02x", digest[i]);
    return text;
}

std::string system_summary() {
    std::string out;
    char line[512];
    // Windows version (RtlGetVersion is not shimmed like GetVersionEx).
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    OSVERSIONINFOW os{};
    os.dwOSVersionInfoSize = sizeof(os);
    if (const auto get = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"))) get(&os);
    std::snprintf(line, sizeof(line), "windows: %lu.%lu build %lu\n", os.dwMajorVersion, os.dwMinorVersion, os.dwBuildNumber);
    out += line;
    SYSTEM_INFO system{};
    GetNativeSystemInfo(&system);
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    GlobalMemoryStatusEx(&memory);
    std::snprintf(line, sizeof(line), "cpu threads: %lu, ram: %.1f GB (%.1f GB free)\n", system.dwNumberOfProcessors,
                  double(memory.ullTotalPhys) / 1073741824.0, double(memory.ullAvailPhys) / 1073741824.0);
    out += line;
    ComPtr<IDXGIFactory1> factory;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        ComPtr<IDXGIAdapter1> adapter;
        for (UINT index = 0; factory->EnumAdapters1(index, &adapter) != DXGI_ERROR_NOT_FOUND; ++index, adapter.Reset()) {
            DXGI_ADAPTER_DESC1 desc{};
            adapter->GetDesc1(&desc);
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
            LARGE_INTEGER umd{};
            const bool version = SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umd));
            // DedicatedVideoMemory is a SIZE_T: it wraps above 4 GB in this 32-bit process, so the budget below stands for it.
            std::snprintf(line, sizeof(line), "gpu %u: %s, vendor %04x device %04x, driver %u.%u.%u.%u\n",
                          index, narrow(desc.Description).c_str(), desc.VendorId, desc.DeviceId,
                          version ? unsigned(HIWORD(umd.HighPart)) : 0u, version ? unsigned(LOWORD(umd.HighPart)) : 0u,
                          version ? unsigned(HIWORD(umd.LowPart)) : 0u, version ? unsigned(LOWORD(umd.LowPart)) : 0u);
            // The same adapter can be enumerated more than once (one entry per LUID): list it once.
            if (out.find(line + std::strlen("gpu 0:")) != std::string::npos) continue;
            out += line;
            ComPtr<IDXGIAdapter3> adapter3;
            DXGI_QUERY_VIDEO_MEMORY_INFO local{}, non_local{};
            if (SUCCEEDED(adapter.As(&adapter3)) &&
                SUCCEEDED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local)) &&
                SUCCEEDED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &non_local))) {
                std::snprintf(line, sizeof(line), "  video memory budget: local %.0f MB, shared %.0f MB\n",
                              double(local.Budget) / 1048576.0, double(non_local.Budget) / 1048576.0);
                out += line;
            }
        }
    }
    out += "renderer_backend: Direct3D 12 embedded\n";
    {
        std::lock_guard<std::mutex> guard(g_status_lock);
        if (!g_status_line.empty()) out += "last memory status: " + g_status_line + "\n";
    }
    const std::wstring dll = dll_path();
    out += std::string("renderer commit: ") + kBuildCommit + "\n";
    out += "ddraw.dll sha256: " + sha256(folder_of(dll) + L"ddraw.dll") + "\n";  // dll is the host exe in core mode
    out += "eid3d11_x64_host.exe sha256: " + sha256(folder_of(dll) + L"eid3d11_x64_host.exe") + "\n";
    return out;
}

// Writes the package; returns the zip (or, when tar is unavailable, the
// folder) path, empty on failure.
// dump: the dump to include ("" none); capture: the capture folder ("" the newest,
// or none when older_capture is false: Shift+F11 must not send a stale frame).
std::wstring write_report(const char* reason, const std::wstring& chosen, const wchar_t* suffix = L"",
                          const std::wstring& capture_folder = {}, bool older_capture = true) {
    const std::wstring root = reports_folder();
    if (root.empty()) return {};
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t stamp[60];
    swprintf_s(stamp, L"%04u-%02u-%02u_%02u-%02u-%02u%s", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, suffix);
    const std::wstring folder = root + stamp + L"\\";
    if (!CreateDirectoryW(folder.c_str(), nullptr)) return {};
    std::string listed;
    const std::wstring dll_folder = folder_of(dll_path());
    for (const wchar_t* name : {L"host.log", L"host.prev.log", L"x86_remote.log", L"d3d12_core.log", L"owned_renderer.log", L"owned_exceptions.log"})
        copy_text(dll_folder + name, folder, listed);
    copy_text(renderer_config_path(), folder, listed);
    // The in-process renderer's events (memory_status lines among them): the last 8 MB.
    const std::wstring debug = environment(L"EI_RENDERER_DEBUG_DIR");
    copy_text((debug.empty() ? environment(L"LOCALAPPDATA") + L"\\EID3D11" : debug) + L"\\bridge.jsonl", folder, listed, 8ull << 20);
    if (!chosen.empty()) {
        // The addon writes ei-*.dmp with its ei-*.dmp.txt description, at times only the description.
        const bool description = chosen.size() > 4 && _wcsicmp(chosen.c_str() + chosen.size() - 4, L".txt") == 0;
        const std::wstring dump_file = description ? chosen.substr(0, chosen.size() - 4) : chosen;
        if (GetFileAttributesW(dump_file.c_str()) != INVALID_FILE_ATTRIBUTES)
            copy_binary(dump_file, folder + dump_file.substr(dump_file.find_last_of(L"\\/") + 1), listed);
        copy_text(dump_file + L".txt", folder, listed);
    }
    // The F11 capture: its json, and of its pass images only the final
    // composition and the overlay on top of it, as PNG (the zip goes through
    // messengers; the intermediate passes are developer material).
    const std::wstring capture = !capture_folder.empty() ? capture_folder
        : older_capture ? newest_file(captures_folder(), L"*", FILETIME{}, true) : std::wstring{};
    if (capture.empty() && !older_capture) listed += "  capture: none arrived for this package\n";
    if (!capture.empty()) {
        const std::wstring target = folder + L"capture\\";
        CreateDirectoryW(target.c_str(), nullptr);
        WIN32_FIND_DATAW found{};
        const HANDLE search = FindFirstFileW((capture + L"\\*").c_str(), &found);
        if (search != INVALID_HANDLE_VALUE) {
            do {
                if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                const std::wstring name = found.cFileName;
                const auto ends = [&](const wchar_t* tail) {
                    const std::size_t n = std::wcslen(tail);
                    return name.size() >= n && _wcsicmp(name.c_str() + name.size() - n, tail) == 0;
                };
                if (ends(L".json") || ends(L".jsonl")) CopyFileW((capture + L"\\" + name).c_str(), (target + name).c_str(), FALSE);
                else if (ends(L"ui_composition.bmp") || ends(L"debug_overlay.bmp"))  // "05_ui_composition.bmp" (F11), "ui_composition.bmp" (harness)
                    write_png(capture + L"\\" + name, target + name.substr(0, name.size() - 4) + L".png");
            } while (FindNextFileW(search, &found));
            FindClose(search);
            listed += "  capture\\ (" + narrow(capture.substr(capture.find_last_of(L"\\/") + 1)) + ": json, final images as PNG)\n";
        }
    }
    // Frame stalls (renderer_game.cpp frame_stall): their count in
    // owned_exceptions.log and the latest line.
    std::string stalls;
    {
        const std::string log = read_file(dll_folder + L"owned_exceptions.log", 4ull << 20);
        std::size_t count = 0, at = 0, latest = std::string::npos;
        while ((at = log.find("!!!! FRAME STALL", at)) != std::string::npos) { ++count; latest = at; at += 16; }
        stalls = "frame stalls in owned_exceptions.log (its last 4 MB): " + std::to_string(count);
        if (latest != std::string::npos) stalls += "; latest: " + log.substr(latest, log.find_first_of("\r\n", latest) - latest);
        stalls += "\n";
    }
    char head[256];
    std::snprintf(head, sizeof(head), "Evil Islands renderer error report, %04u-%02u-%02u %02u:%02u:%02u local time\nreason: ",
                  now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    write_file(folder + L"summary.txt", head + std::string(reason ? reason : "unknown") + "\n\n" + system_summary() + stalls + "\nfiles:\n" + listed);

    // tar.exe -a: the archive format from the .zip name (deflate).
    const std::wstring zip = root + stamp + L".zip";
    std::wstring tar = environment(L"SystemRoot") + L"\\System32\\tar.exe";
    std::wstring command = L"\"" + tar + L"\" -a -c -f \"" + zip + L"\" -C \"" + root + stamp + L"\" .";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    DWORD code = 1;
    if (CreateProcessW(tar.c_str(), &command[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        if (WaitForSingleObject(process.hProcess, 120000) == WAIT_OBJECT_0) GetExitCodeProcess(process.hProcess, &code);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
    if (code != 0 || GetFileAttributesW(zip.c_str()) == INVALID_FILE_ATTRIBUTES) return folder;  // the folder is the package
    // The staging copies (this function's own files only).
    WIN32_FIND_DATAW found{};
    for (const std::wstring& sub : {folder + L"capture\\", folder}) {
        const HANDLE search = FindFirstFileW((sub + L"*").c_str(), &found);
        if (search == INVALID_HANDLE_VALUE) continue;
        do {
            if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) DeleteFileW((sub + found.cFileName).c_str());
        } while (FindNextFileW(search, &found));
        FindClose(search);
        RemoveDirectoryW(sub.c_str());
    }
    return zip;
}

void report_and_show(const char* reason, const std::wstring& dump) {
    const std::wstring path = write_report(reason, dump);
    wchar_t text[1024];
    if (path.empty())
        swprintf_s(text, L"The error report could not be written (neither the game's \"EID3D11 reports\" folder nor %%LOCALAPPDATA%%\\EID3D11\\reports).");
    else
        swprintf_s(text, L"The error report was saved to:\n\n%s\n\nPlease send this file to the developers. "
                         L"It holds the renderer logs, settings, crash dump and a hardware summary (GPU, driver, Windows version).\n\n"
                         L"\x041E\x0442\x0447\x0451\x0442 \x0441\x043E\x0445\x0440\x0430\x043D\x0451\x043D (\x0441\x043C. \x043F\x0443\x0442\x044C \x0432\x044B\x0448\x0435). "
                         L"\x041F\x0435\x0440\x0435\x0434\x0430\x0439\x0442\x0435 \x044D\x0442\x043E\x0442 \x0444\x0430\x0439\x043B \x0440\x0430\x0437\x0440\x0430\x0431\x043E\x0442\x0447\x0438\x043A\x0430\x043C.",
                   path.c_str());
    MessageBoxW(nullptr, text, L"Evil Islands", MB_OK | MB_ICONINFORMATION | MB_TOPMOST | MB_SETFOREGROUND);
}

constexpr const wchar_t* kQuestion =
    L"\n\nCreate an error report for the developers? It collects the renderer logs, settings, the crash dump and a hardware "
    L"summary into one zip file.\n\n\x0421\x043E\x0437\x0434\x0430\x0442\x044C \x043E\x0442\x0447\x0451\x0442 \x043E\x0431 "
    L"\x043E\x0448\x0438\x0431\x043A\x0435 \x0434\x043B\x044F \x0440\x0430\x0437\x0440\x0430\x0431\x043E\x0442\x0447\x0438\x043A\x043E\x0432?";

bool harness_run() { return GetEnvironmentVariableW(L"EI_RENDERER_HARNESS_DIR", nullptr, 0) > 0; }

DWORD WINAPI startup_check(LPVOID) {
    const std::wstring root = reports_folder();
    if (root.empty()) return 0;
    // last_session.txt: this start's time (FILETIME as a number); a dump newer
    // than the previous start belongs to the previous session.
    const std::wstring marker = root + L"last_session.txt";
    const std::string previous = read_file(marker, 0);
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    write_file(marker, std::to_string((std::uint64_t(now.dwHighDateTime) << 32) | now.dwLowDateTime) + "\n");
    if (previous.empty()) return 0;
    const std::uint64_t start = std::strtoull(previous.c_str(), nullptr, 10);
    if (!start) return 0;
    const FILETIME after{DWORD(start), DWORD(start >> 32)};
    const std::wstring dump = newest_dump(after);
    if (dump.empty()) return 0;
    std::wstring text = L"The last game session ended with a crash (" + dump.substr(dump.find_last_of(L"\\/") + 1) + L").\n"
                        L"\x041F\x0440\x043E\x0448\x043B\x0430\x044F \x0438\x0433\x0440\x043E\x0432\x0430\x044F \x0441\x0435\x0441\x0441\x0438\x044F "
                        L"\x0437\x0430\x0432\x0435\x0440\x0448\x0438\x043B\x0430\x0441\x044C \x0441\x0431\x043E\x0435\x043C." + std::wstring(kQuestion);
    if (MessageBoxW(nullptr, text.c_str(), L"Evil Islands", MB_YESNO | MB_ICONQUESTION | MB_TOPMOST | MB_SETFOREGROUND) == IDYES)
        report_and_show(("previous session crashed; dump " + narrow(dump.substr(dump.find_last_of(L"\\/") + 1))).c_str(), dump);
    return 0;
}

} // namespace

void offer_error_report(const wchar_t* message, const char* reason) noexcept try {
    const std::wstring text = std::wstring(message) + kQuestion;
    if (harness_run()) {  // automated runs: no question; the package is written, the message shown as before
        write_report(reason, newest_dump(seconds_ago(600)));
        MessageBoxW(nullptr, message, L"Evil Islands", MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND);
        return;
    }
    if (MessageBoxW(nullptr, text.c_str(), L"Evil Islands", MB_YESNO | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND) == IDYES)
        report_and_show(reason, newest_dump(seconds_ago(600)));
} catch (...) {
}

void start_error_report_check() noexcept {
    if (harness_run()) return;
    if (const HANDLE thread = CreateThread(nullptr, 0, &startup_check, nullptr, 0, nullptr)) CloseHandle(thread);
}

namespace {
std::mutex g_package_lock;
std::string g_package_result;
bool g_package_ready = false;
volatile LONG g_package_running = 0;
volatile LONG g_host_memory_report_requested = 0;
volatile LONG g_host_memory_report_pending = 0;  // set with the request, cleared when the report is queued

// The capture folder the request makes (newer than its start), once nothing in
// it changed for 2 s; 60 s at most (a hidden overlay first runs its timing).
DWORD WINAPI diagnostics_package(LPVOID) {
    const FILETIME requested = seconds_ago(1);
    std::wstring capture;
    std::uint64_t last_bytes = ~0ull;
    unsigned quiet = 0;
    for (int tick = 0; tick < 240 && quiet < 8; ++tick) {
        Sleep(250);
        if (capture.empty()) capture = newest_file(captures_folder(), L"*", requested, true);
        if (capture.empty()) continue;
        std::uint64_t bytes = 0;
        WIN32_FIND_DATAW found{};
        const HANDLE search = FindFirstFileW((capture + L"\\*").c_str(), &found);
        if (search != INVALID_HANDLE_VALUE) {
            do bytes += (std::uint64_t(found.nFileSizeHigh) << 32) | found.nFileSizeLow; while (FindNextFileW(search, &found));
            FindClose(search);
        }
        quiet = bytes && bytes == last_bytes ? quiet + 1 : 0;
        last_bytes = bytes;
    }
    // The renderer's host memory report (Shift+F11) goes to bridge.jsonl through the
    // async event writer: wait for it (at most 5 s), then for the writer to write it.
    for (int tick = 0; tick < 100 && InterlockedCompareExchange(&g_host_memory_report_pending, 0, 0); ++tick) Sleep(50);
    owned::bridge_async_flush(1000);
    std::wstring path;
    try { path = write_report("manual diagnostics package (Shift+F11)", newest_dump(seconds_ago(24 * 3600)), L"-manual", capture, false); } catch (...) {}
    std::string text = path.empty() ? "Diagnostics package failed: neither the game's \"EID3D11 reports\" folder nor %LOCALAPPDATA%\\EID3D11\\reports could be written."
                                    : "Diagnostics package saved: " + narrow(path) + (capture.empty() ? " (no capture)" : "");
    {
        std::lock_guard<std::mutex> guard(g_package_lock);
        g_package_result = std::move(text);
        g_package_ready = true;
    }
    InterlockedExchange(&g_package_running, 0);
    return 0;
}
}  // namespace

void start_diagnostics_package() noexcept {
    if (InterlockedCompareExchange(&g_package_running, 1, 0) != 0) return;  // one at a time
    InterlockedExchange(&g_host_memory_report_pending, 1);
    InterlockedExchange(&g_host_memory_report_requested, 1);
    if (const HANDLE thread = CreateThread(nullptr, 0, &diagnostics_package, nullptr, 0, nullptr)) CloseHandle(thread);
    else InterlockedExchange(&g_package_running, 0);
}

void host_memory_report_done() noexcept {
    InterlockedExchange(&g_host_memory_report_pending, 0);
}

bool take_host_memory_report_request() noexcept {
    return InterlockedExchange(&g_host_memory_report_requested, 0) != 0;
}

void set_error_report_status(const char* line) noexcept try {
    std::lock_guard<std::mutex> guard(g_status_lock);
    g_status_line = line ? line : "";
} catch (...) {
}

bool take_diagnostics_package_result(std::string& text) noexcept try {
    std::lock_guard<std::mutex> guard(g_package_lock);
    if (!g_package_ready) return false;
    text = std::move(g_package_result);
    g_package_ready = false;
    return true;
} catch (...) {
    return false;
}

} // namespace owned
