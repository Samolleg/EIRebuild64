#pragma once

// The game.exe the renderer was loaded into is accepted feature by feature:
// each feature runs only if every game.exe byte it reads or patches is what
// it expects at its known address (user decision 2026-10-04: no whole-file
// hash gate). The site list is generated from the sources by
// Tools/generate-game-sites.py. Installers keep their own in-memory entry
// checks, which also see addon hooks; this pass checks the exe file itself.
// The file hash is logged for diagnostics only.

#include <windows.h>
#include <bcrypt.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace owned {

enum class GameSiteGroup : std::uint8_t {
    bridge,              // engine bridge: semantic slots and every bridge feature
    cursor_coordinates,  // proxy cursor call sites
    res_mapping,         // resource file-mapping import slots
    res_cache,           // addon resource-cache release routing
    ui_layers,           // owned interface text DC sites
    harness,             // test harness verbs
    hud_native,          // stock HUD panel entries (no addon HUD contract)
    visual_native,       // stock unit-appearance sites (no addon visual contract)
    widescreen,          // owned projection and interface layout
    focus,               // background mode: WinMain loop calls, activation state
    count
};

struct GameSiteSection {
    std::uint32_t virtual_address;
    std::uint32_t raw_offset;
    std::uint32_t raw_size;
};

struct GameSite {
    std::uint32_t rva;
    std::uint32_t file_offset;
    std::uint8_t length;
    GameSiteGroup group;
    std::uint8_t bytes[6];
};

} // namespace owned

#include "game_sites_table.h"

namespace owned {

constexpr std::size_t kGameSiteGroupCount = static_cast<std::size_t>(GameSiteGroup::count);

inline const char* game_site_group_name(GameSiteGroup group) noexcept {
    static const char* const names[kGameSiteGroupCount] = {
        "bridge", "cursor_coordinates", "res_mapping", "res_cache", "ui_layers", "harness", "hud_native", "visual_native", "widescreen", "focus"};
    const auto index = static_cast<std::size_t>(group);
    return index < kGameSiteGroupCount ? names[index] : "unknown";
}

struct GameSiteReport {
    bool layout = false;  // i386 PE with the reference image size and section layout
    std::array<bool, kGameSiteGroupCount> group{};
    std::size_t failed_count = 0;
    std::array<std::uint32_t, 16> failed_rvas{};  // first failures, for the log
    bool ok(GameSiteGroup g) const noexcept { return layout && group[static_cast<std::size_t>(g)]; }
};

// File offsets of the PE header fields 4 GB mode changes; false unless the
// file is an i386 PE with the reference image size and section layout.
struct GameImageHeader {
    std::size_t characteristics = 0;
    std::size_t checksum = 0;
};

inline bool game_image_layout(const std::vector<std::uint8_t>& file, GameImageHeader& header) {
    const auto read32 = [&](std::size_t at, std::uint32_t& value) {
        if (at > file.size() || file.size() - at < 4) return false;
        std::memcpy(&value, file.data() + at, 4);
        return true;
    };
    const auto read16 = [&](std::size_t at, std::uint16_t& value) {
        if (at > file.size() || file.size() - at < 2) return false;
        std::memcpy(&value, file.data() + at, 2);
        return true;
    };
    std::uint16_t magic = 0, machine = 0, sections = 0, optional_size = 0, optional_magic = 0;
    std::uint32_t nt = 0, signature = 0, image_size = 0;
    if (!read16(0, magic) || magic != IMAGE_DOS_SIGNATURE || !read32(0x3c, nt) ||
        !read32(nt, signature) || signature != IMAGE_NT_SIGNATURE ||
        !read16(nt + 4, machine) || machine != IMAGE_FILE_MACHINE_I386 ||
        !read16(nt + 6, sections) || !read16(nt + 20, optional_size) ||
        !read16(nt + 24, optional_magic) || optional_magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        !read32(nt + 24 + 56, image_size) || image_size != kGameSiteImageSize ||
        sections != std::size(kGameSiteSections))
        return false;
    const std::size_t table = nt + 24 + optional_size;
    for (std::uint16_t index = 0; index < sections; ++index) {
        std::uint32_t virtual_address = 0, raw_size = 0, raw_offset = 0;
        const std::size_t entry = table + index * 40u;
        if (!read32(entry + 12, virtual_address) || !read32(entry + 16, raw_size) ||
            !read32(entry + 20, raw_offset) ||
            virtual_address != kGameSiteSections[index].virtual_address ||
            raw_offset != kGameSiteSections[index].raw_offset ||
            raw_size != kGameSiteSections[index].raw_size)
            return false;
    }
    header.characteristics = nt + 22;
    header.checksum = nt + 24 + 64;
    return true;
}

inline GameSiteReport verify_game_sites(const std::vector<std::uint8_t>& file) {
    GameSiteReport report;
    GameImageHeader header;
    report.layout = game_image_layout(file, header);
    report.group.fill(true);
    if (!report.layout) return report;
    for (const auto& site : kGameSites) {
        if (site.file_offset <= file.size() && file.size() - site.file_offset >= site.length &&
            std::memcmp(file.data() + site.file_offset, site.bytes, site.length) == 0)
            continue;
        report.group[static_cast<std::size_t>(site.group)] = false;
        if (report.failed_count < report.failed_rvas.size())
            report.failed_rvas[report.failed_count] = site.rva;
        ++report.failed_count;
    }
    return report;
}

inline bool read_whole_file(const wchar_t* path, std::vector<std::uint8_t>& bytes) {
    const HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart < (64ll << 20);
    if (ok) {
        bytes.resize(static_cast<std::size_t>(size.QuadPart));
        DWORD read = 0;
        ok = ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) &&
             read == bytes.size();
    }
    CloseHandle(file);
    return ok;
}

inline bool sha256_bytes(const std::uint8_t* data, std::size_t size, std::array<std::uint8_t, 32>& digest) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD object_size = 0, result_size = 0;
    std::vector<std::uint8_t> object;
    bool ok = false;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0 &&
        BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&object_size),
                          sizeof(object_size), &result_size, 0) >= 0) {
        object.resize(object_size);
        ok = BCryptCreateHash(algorithm, &hash, object.data(), object_size, nullptr, 0, 0) >= 0 &&
             BCryptHashData(hash, const_cast<PUCHAR>(data), static_cast<ULONG>(size), 0) >= 0 &&
             BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0;
    }
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    return ok;
}

inline std::wstring running_image_path() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (!length) return {};
        if (length < path.size()) { path.resize(length); return path; }
        path.resize(path.size() * 2);
    }
}

struct RunningGameSites {
    GameSiteReport report;
    char sha256[65] = "unavailable";  // diagnostics only, never a gate
};

// The running exe's site verification, computed once.
inline const RunningGameSites& running_game_sites() {
    static const RunningGameSites result = [] {
        RunningGameSites value;
        std::vector<std::uint8_t> file;
        const std::wstring path = running_image_path();
        if (path.empty() || !read_whole_file(path.c_str(), file)) return value;
        value.report = verify_game_sites(file);
        std::array<std::uint8_t, 32> digest{};
        if (sha256_bytes(file.data(), file.size(), digest))
            for (std::size_t index = 0; index < digest.size(); ++index)
                std::snprintf(value.sha256 + index * 2, 3, "%02X", digest[index]);
        return value;
    }();
    return result;
}

inline bool game_sites_ok(GameSiteGroup group) { return running_game_sites().report.ok(group); }

// The PE image checksum (the CheckSumMappedFile algorithm): 16-bit
// one's-complement sum of the file with the checksum field as zero, plus the
// file length.
inline std::uint32_t pe_file_checksum(const std::vector<std::uint8_t>& file, std::size_t checksum_offset) {
    std::uint32_t sum = 0;
    for (std::size_t at = 0; at < file.size(); at += 2) {
        std::uint32_t word = file[at];
        if (at + 1 < file.size()) word |= std::uint32_t(file[at + 1]) << 8;
        if (at >= checksum_offset && at < checksum_offset + 4) word = 0;
        sum += word;
        sum = (sum & 0xffff) + (sum >> 16);
    }
    sum = (sum & 0xffff) + (sum >> 16);
    return sum + static_cast<std::uint32_t>(file.size());
}

// Sets the large-address-aware bit and recomputes the PE checksum.
inline bool set_large_address_aware(std::vector<std::uint8_t>& file) {
    GameImageHeader header;
    if (!game_image_layout(file, header)) return false;
    file[header.characteristics] |= static_cast<std::uint8_t>(IMAGE_FILE_LARGE_ADDRESS_AWARE);
    const std::uint32_t checksum = pe_file_checksum(file, header.checksum);
    std::memcpy(file.data() + header.checksum, &checksum, 4);
    return true;
}

// The large-address-aware bit of the running process image, as loaded.
inline bool running_image_large_address_aware() noexcept {
    const auto* base = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    return (nt->FileHeader.Characteristics & IMAGE_FILE_LARGE_ADDRESS_AWARE) != 0;
}

// Patches the executable this process was started from so its next start
// gets the 4 GB address space. The running image cannot be overwritten but
// can be renamed: the original moves to <exe>.bak when no backup exists
// (else to <exe>.old, removed on a later start), and the patched copy takes
// the original name. Only an exe with the game's image layout is touched.
// Returns a short player-facing result.
inline const char* enable_large_address_mode() {
    const std::wstring path = running_image_path();
    if (path.empty()) return "Could not locate game.exe.";
    std::vector<std::uint8_t> bytes;
    if (!read_whole_file(path.c_str(), bytes)) return "Could not read game.exe.";
    GameImageHeader header;
    if (!game_image_layout(bytes, header)) return "This game.exe does not have the Evil Islands layout; not changed.";
    if (bytes[header.characteristics] & IMAGE_FILE_LARGE_ADDRESS_AWARE)
        return "4 GB mode is already enabled in game.exe. Restart the game to use it.";
    if (!set_large_address_aware(bytes)) return "Could not patch game.exe.";
    const std::wstring patched = path + L".new", backup = path + L".bak", old = path + L".old";
    const HANDLE file = CreateFileW(patched.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return "Could not write next to game.exe (folder not writable?).";
    DWORD written = 0;
    const bool wrote = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
                       written == bytes.size() && FlushFileBuffers(file);
    CloseHandle(file);
    std::vector<std::uint8_t> check;
    if (!wrote || !read_whole_file(patched.c_str(), check) || check != bytes) {
        DeleteFileW(patched.c_str());
        return "Could not write the patched game.exe.";
    }
    const bool have_backup = GetFileAttributesW(backup.c_str()) != INVALID_FILE_ATTRIBUTES;
    const std::wstring& aside = have_backup ? old : backup;
    if (have_backup) DeleteFileW(old.c_str());
    if (!MoveFileExW(path.c_str(), aside.c_str(), 0)) {
        DeleteFileW(patched.c_str());
        return "Could not rename the running game.exe; not changed.";
    }
    if (!MoveFileExW(patched.c_str(), path.c_str(), 0)) {
        MoveFileExW(aside.c_str(), path.c_str(), 0);
        DeleteFileW(patched.c_str());
        return "Could not place the patched game.exe; not changed.";
    }
    return have_backup ? "4 GB mode enabled. Restart the game to use it."
                       : "4 GB mode enabled (original saved as game.exe.bak). Restart the game to use it.";
}

// Removes the <exe>.old left by an earlier patch once its process is gone.
inline void remove_previous_patched_image() {
    const std::wstring path = running_image_path();
    if (!path.empty()) DeleteFileW((path + L".old").c_str());
}

} // namespace owned
