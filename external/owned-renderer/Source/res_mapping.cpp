#include "res_mapping.h"

#include <intrin.h>
#include <psapi.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace owned {
namespace {

using CreateFileMappingAFn = HANDLE(WINAPI*)(HANDLE, LPSECURITY_ATTRIBUTES, DWORD,
                                             DWORD, DWORD, LPCSTR);
using MapViewOfFileFn = LPVOID(WINAPI*)(HANDLE, DWORD, DWORD, DWORD, SIZE_T);
using UnmapViewOfFileFn = BOOL(WINAPI*)(LPCVOID);
using CloseHandleFn = BOOL(WINAPI*)(HANDLE);

constexpr std::uint32_t kExpectedImageSize = 0x003d7000;
constexpr std::uint32_t kCloseHandleIatRva = 0x0033b1e8;
constexpr std::uint32_t kUnmapViewOfFileIatRva = 0x0033b204;
constexpr std::uint32_t kMapViewOfFileIatRva = 0x0033b208;
constexpr std::uint32_t kCreateFileMappingAIatRva = 0x0033b414;
constexpr std::uint32_t kResourceCreateMappingCallRva = 0x00275959;
constexpr std::uint32_t kResourceCreateMappingReturnRva = 0x0027595f;
constexpr std::uint32_t kResourceMapViewCallRva = 0x002759c7;
constexpr std::uint32_t kResourceUnmapViewCallRva = 0x002756df;

constexpr std::array<std::uint8_t, 6> kCreateMappingCall{
    0xff, 0x15, 0x14, 0xb4, 0x73, 0x00};
constexpr std::array<std::uint8_t, 6> kMapViewCall{
    0xff, 0x15, 0x08, 0xb2, 0x73, 0x00};
constexpr std::array<std::uint8_t, 6> kUnmapViewCall{
    0xff, 0x15, 0x04, 0xb2, 0x73, 0x00};

constexpr std::size_t kMaximumTrackedMappings = 2048;
constexpr std::size_t kMaximumTrackedViews = 2048;

struct FileIdentity {
    DWORD volume_serial = 0;
    DWORD file_index_high = 0;
    DWORD file_index_low = 0;
    DWORD size_high = 0;
    DWORD size_low = 0;
    FILETIME last_write{};
};

bool operator==(const FileIdentity& left, const FileIdentity& right) noexcept {
    return left.volume_serial == right.volume_serial &&
        left.file_index_high == right.file_index_high &&
        left.file_index_low == right.file_index_low &&
        left.size_high == right.size_high && left.size_low == right.size_low &&
        left.last_write.dwHighDateTime == right.last_write.dwHighDateTime &&
        left.last_write.dwLowDateTime == right.last_write.dwLowDateTime;
}

std::uint64_t identity_size(const FileIdentity& identity) noexcept {
    return (static_cast<std::uint64_t>(identity.size_high) << 32u) |
        identity.size_low;
}

struct MappingRecord {
    HANDLE handle = nullptr;
    FileIdentity identity{};
};

struct ViewRecord {
    void* base = nullptr;
    FileIdentity identity{};
    std::uint64_t size = 0;
    std::uint32_t logical_references = 0;
    std::wstring physical; // GetMappedFileNameW of the view, once at map time
};

struct ImportSlot {
    const char* name = nullptr;
    std::uint32_t expected_rva = 0;
    void** cell = nullptr;
    void* expected = nullptr;
    void* replacement = nullptr;
};

struct MappingState {
    SRWLOCK lock = SRWLOCK_INIT;
    HMODULE image = nullptr;
    bool hooks_active = false;
    bool accepting = false;
    ResMappingStatus status = ResMappingStatus::not_attempted;
    CreateFileMappingAFn real_create_file_mapping = ::CreateFileMappingA;
    MapViewOfFileFn real_map_view_of_file = ::MapViewOfFile;
    UnmapViewOfFileFn real_unmap_view_of_file = ::UnmapViewOfFile;
    CloseHandleFn real_close_handle = ::CloseHandle;
    std::array<MappingRecord, kMaximumTrackedMappings> mappings{};
    std::array<ViewRecord, kMaximumTrackedViews> views{};
    // Read views the game maps through these import slots outside the shared
    // .res tracking (figure archives and others): their file name, taken once
    // when mapped and dropped before the game unmaps them. An offset-zero view
    // also keeps its extent so a resource reader needs no VirtualQuery.
    struct OtherView { const void* base = nullptr; std::uint64_t size = 0; std::wstring physical; };
    std::vector<OtherView> other_views;
    ResMappingSnapshot counters{};
};

MappingState g_state;

void saturating_increment(std::uint64_t& value) noexcept {
    if (value != std::numeric_limits<std::uint64_t>::max()) ++value;
}

void saturating_add(std::uint64_t& value, std::uint64_t amount) noexcept {
    if (amount > std::numeric_limits<std::uint64_t>::max() - value)
        value = std::numeric_limits<std::uint64_t>::max();
    else
        value += amount;
}

bool has_res_extension(const wchar_t* path, DWORD length) noexcept {
    if (!path || length < 4) return false;
    const wchar_t* extension = path + length - 4;
    return extension[0] == L'.' &&
        (extension[1] == L'r' || extension[1] == L'R') &&
        (extension[2] == L'e' || extension[2] == L'E') &&
        (extension[3] == L's' || extension[3] == L'S');
}

bool capture_file_identity(HANDLE file, FileIdentity& identity) noexcept {
    if (!file || file == INVALID_HANDLE_VALUE || GetFileType(file) != FILE_TYPE_DISK)
        return false;

    wchar_t final_path[MAX_PATH]{};
    const DWORD length = GetFinalPathNameByHandleW(
        file, final_path, static_cast<DWORD>(std::size(final_path)),
        FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!length || length >= std::size(final_path) ||
        !has_res_extension(final_path, length))
        return false;

    BY_HANDLE_FILE_INFORMATION information{};
    if (!GetFileInformationByHandle(file, &information) ||
        information.nFileSizeHigh != 0 || information.nFileSizeLow == 0)
        return false;

    identity.volume_serial = information.dwVolumeSerialNumber;
    identity.file_index_high = information.nFileIndexHigh;
    identity.file_index_low = information.nFileIndexLow;
    identity.size_high = information.nFileSizeHigh;
    identity.size_low = information.nFileSizeLow;
    identity.last_write = information.ftLastWriteTime;
    return identity.volume_serial != 0 &&
        (identity.file_index_high != 0 || identity.file_index_low != 0);
}

MappingRecord* find_mapping(HANDLE handle) noexcept {
    for (auto& mapping : g_state.mappings)
        if (mapping.handle == handle) return &mapping;
    return nullptr;
}

MappingRecord* empty_mapping() noexcept {
    for (auto& mapping : g_state.mappings)
        if (!mapping.handle) return &mapping;
    return nullptr;
}

ViewRecord* find_view(const FileIdentity& identity) noexcept {
    for (auto& view : g_state.views)
        if (view.base && view.identity == identity) return &view;
    return nullptr;
}

ViewRecord* find_view(const void* base) noexcept {
    for (auto& view : g_state.views)
        if (view.base == base) return &view;
    return nullptr;
}

ViewRecord* empty_view() noexcept {
    for (auto& view : g_state.views)
        if (!view.base) return &view;
    return nullptr;
}

bool range_in_image(const std::uint8_t* image, std::uint32_t image_size,
                    std::uint32_t rva, std::size_t bytes) noexcept {
    return image && rva <= image_size && bytes <= image_size - rva;
}

void** find_import_slot(std::uint8_t* image, std::uint32_t image_size,
                        const char* imported_dll, const char* function_name) noexcept {
    if (!range_in_image(image, image_size, 0, sizeof(IMAGE_DOS_HEADER))) return nullptr;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 ||
        !range_in_image(image, image_size, static_cast<std::uint32_t>(dos->e_lfanew),
                        sizeof(IMAGE_NT_HEADERS32)))
        return nullptr;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(image + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        nt->OptionalHeader.SizeOfImage != image_size)
        return nullptr;
    const auto& directory =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!directory.VirtualAddress ||
        !range_in_image(image, image_size, directory.VirtualAddress,
                        sizeof(IMAGE_IMPORT_DESCRIPTOR)))
        return nullptr;

    for (std::uint32_t descriptor_rva = directory.VirtualAddress;
         range_in_image(image, image_size, descriptor_rva,
                        sizeof(IMAGE_IMPORT_DESCRIPTOR));
         descriptor_rva += sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
        const auto* descriptor = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(
            image + descriptor_rva);
        if (!descriptor->Name) break;
        if (!range_in_image(image, image_size, descriptor->Name, 1)) return nullptr;
        const char* dll_name = reinterpret_cast<const char*>(image + descriptor->Name);
        if (_stricmp(dll_name, imported_dll) != 0) continue;
        if (!descriptor->OriginalFirstThunk || !descriptor->FirstThunk) return nullptr;

        for (std::uint32_t index = 0;; ++index) {
            const std::uint64_t name_rva = static_cast<std::uint64_t>(
                descriptor->OriginalFirstThunk) +
                static_cast<std::uint64_t>(index) * sizeof(IMAGE_THUNK_DATA32);
            const std::uint64_t iat_rva = static_cast<std::uint64_t>(
                descriptor->FirstThunk) +
                static_cast<std::uint64_t>(index) * sizeof(IMAGE_THUNK_DATA32);
            if (name_rva > std::numeric_limits<std::uint32_t>::max() ||
                iat_rva > std::numeric_limits<std::uint32_t>::max() ||
                !range_in_image(image, image_size,
                                static_cast<std::uint32_t>(name_rva),
                                sizeof(IMAGE_THUNK_DATA32)) ||
                !range_in_image(image, image_size,
                                static_cast<std::uint32_t>(iat_rva),
                                sizeof(IMAGE_THUNK_DATA32)))
                return nullptr;
            const auto* name_thunk = reinterpret_cast<const IMAGE_THUNK_DATA32*>(
                image + static_cast<std::uint32_t>(name_rva));
            if (!name_thunk->u1.AddressOfData) return nullptr;
            if (IMAGE_SNAP_BY_ORDINAL32(name_thunk->u1.Ordinal)) continue;
            const std::uint32_t import_name_rva = name_thunk->u1.AddressOfData;
            if (!range_in_image(image, image_size, import_name_rva,
                                sizeof(IMAGE_IMPORT_BY_NAME)))
                return nullptr;
            const auto* import_name = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(
                image + import_name_rva);
            if (std::strcmp(reinterpret_cast<const char*>(import_name->Name),
                            function_name) == 0)
                return reinterpret_cast<void**>(
                    image + static_cast<std::uint32_t>(iat_rva));
        }
    }
    return nullptr;
}

bool signature(const std::uint8_t* image, std::uint32_t rva,
               const std::array<std::uint8_t, 6>& expected) noexcept {
    return range_in_image(image, kExpectedImageSize, rva, expected.size()) &&
        std::memcmp(image + rva, expected.data(), expected.size()) == 0;
}

HANDLE create_file_mapping_impl(HANDLE file, LPSECURITY_ATTRIBUTES attributes,
                                DWORD protect, DWORD maximum_high,
                                DWORD maximum_low, LPCSTR name,
                                bool supported_resource_callsite) noexcept {
    const HANDLE mapping = g_state.real_create_file_mapping(
        file, attributes, protect, maximum_high, maximum_low, name);
    const DWORD result_error = GetLastError();
    const bool candidate = mapping && supported_resource_callsite &&
        !attributes && protect == PAGE_READONLY && maximum_high == 0 &&
        maximum_low == 0 && name == nullptr;
    if (candidate) {
        FileIdentity identity{};
        if (capture_file_identity(file, identity)) {
            AcquireSRWLockExclusive(&g_state.lock);
            MappingRecord* slot = find_mapping(mapping);
            if (slot) {
                *slot = {};
                --g_state.counters.tracked_mapping_handles;
                saturating_increment(g_state.counters.identity_rejections);
            } else if ((slot = empty_mapping()) != nullptr) {
                slot->handle = mapping;
                slot->identity = identity;
                ++g_state.counters.tracked_mapping_handles;
            } else {
                saturating_increment(g_state.counters.tracking_capacity_failures);
            }
            ReleaseSRWLockExclusive(&g_state.lock);
        } else {
            AcquireSRWLockExclusive(&g_state.lock);
            saturating_increment(g_state.counters.identity_rejections);
            ReleaseSRWLockExclusive(&g_state.lock);
        }
    }
    SetLastError(result_error);
    return mapping;
}

LPVOID map_view_of_file_impl(HANDLE mapping, DWORD access, DWORD offset_high,
                             DWORD offset_low, SIZE_T requested) noexcept {
    const DWORD entry_error = GetLastError();
    AcquireSRWLockExclusive(&g_state.lock);
    MappingRecord* tracked = g_state.accepting ? find_mapping(mapping) : nullptr;
    const bool eligible = tracked && access == FILE_MAP_READ && offset_high == 0 &&
        offset_low == 0 && requested == 0;
    if (eligible) {
        if (ViewRecord* existing = find_view(tracked->identity)) {
            if (existing->logical_references !=
                std::numeric_limits<std::uint32_t>::max()) {
                ++existing->logical_references;
                ++g_state.counters.logical_views;
                saturating_add(g_state.counters.active_saved_view_bytes,
                               existing->size);
                g_state.counters.peak_saved_view_bytes = std::max(
                    g_state.counters.peak_saved_view_bytes,
                    g_state.counters.active_saved_view_bytes);
                saturating_increment(g_state.counters.shared_reuses);
                void* const base = existing->base;
                ReleaseSRWLockExclusive(&g_state.lock);
                SetLastError(entry_error);
                return base;
            }
        }

        if (ViewRecord* slot = empty_view()) {
            saturating_increment(g_state.counters.physical_map_calls);
            void* const base = g_state.real_map_view_of_file(
                mapping, access, offset_high, offset_low, requested);
            const DWORD result_error = GetLastError();
            if (base) {
                MEMORY_BASIC_INFORMATION information{};
                const std::uint64_t mapped_size =
                    VirtualQuery(base, &information, sizeof(information)) ==
                            sizeof(information)
                        ? static_cast<std::uint64_t>(information.RegionSize)
                        : identity_size(tracked->identity);
                slot->base = base;
                slot->identity = tracked->identity;
                slot->size = mapped_size;
                slot->logical_references = 1;
                // Appearance resolution asks for this view's file per member;
                // the OS query costs a kernel call each time, so it is taken
                // once here, for exactly this view's lifetime.
                try {
                    std::wstring physical(32768, L'\0');
                    const DWORD count = GetMappedFileNameW(GetCurrentProcess(), base,
                        physical.data(), static_cast<DWORD>(physical.size()));
                    // An exact-size copy: the 64 KB query buffer stayed with
                    // every live view when it was resized and moved in.
                    if (count && count < physical.size())
                        slot->physical = L"\\\\?\\GLOBALROOT" + physical.substr(0, count);
                } catch (...) { slot->physical.clear(); }
                ++g_state.counters.logical_views;
                ++g_state.counters.physical_views;
                saturating_add(g_state.counters.physical_view_bytes, slot->size);
            }
            ReleaseSRWLockExclusive(&g_state.lock);
            SetLastError(result_error);
            return base;
        }
        saturating_increment(g_state.counters.tracking_capacity_failures);
    }

    saturating_increment(g_state.counters.passthrough_maps);
    saturating_increment(g_state.counters.physical_map_calls);
    ReleaseSRWLockExclusive(&g_state.lock);
    void* const base = g_state.real_map_view_of_file(
        mapping, access, offset_high, offset_low, requested);
    const DWORD result_error = GetLastError();
    if (base && access == FILE_MAP_READ) {
        try {
            MEMORY_BASIC_INFORMATION information{};
            const std::uint64_t extent = offset_high == 0 && offset_low == 0 &&
                VirtualQuery(base, &information, sizeof(information)) == sizeof(information) &&
                information.State == MEM_COMMIT && information.Type == MEM_MAPPED &&
                information.Protect == PAGE_READONLY && information.BaseAddress == base
                ? static_cast<std::uint64_t>(information.RegionSize) : 0;
            std::wstring physical(32768, L'\0');
            const DWORD count = GetMappedFileNameW(GetCurrentProcess(), base,
                physical.data(), static_cast<DWORD>(physical.size()));
            if (count && count < physical.size()) {
                physical = L"\\\\?\\GLOBALROOT" + physical.substr(0, count); // exact size, not 64 KB
                AcquireSRWLockExclusive(&g_state.lock);
                auto& views = g_state.other_views;
                const auto found = std::find_if(views.begin(), views.end(),
                    [base](const auto& view) { return view.base == base; });
                if (found != views.end()) { found->size = extent; found->physical = std::move(physical); }
                else if (views.size() < kMaximumTrackedViews)
                    views.push_back({base, extent, std::move(physical)});
                ReleaseSRWLockExclusive(&g_state.lock);
            }
        } catch (...) {}
    }
    SetLastError(result_error);
    return base;
}

BOOL unmap_view_of_file_impl(LPCVOID base) noexcept {
    const DWORD entry_error = GetLastError();
    AcquireSRWLockExclusive(&g_state.lock);
    ViewRecord* tracked = find_view(base);
    if (!tracked) {
        saturating_increment(g_state.counters.physical_unmap_calls);
        // Drop its provenance before the view goes, never after.
        auto& views = g_state.other_views;
        views.erase(std::remove_if(views.begin(), views.end(),
            [base](const auto& view) { return view.base == base; }), views.end());
        ReleaseSRWLockExclusive(&g_state.lock);
        const BOOL result = g_state.real_unmap_view_of_file(base);
        const DWORD result_error = GetLastError();
        SetLastError(result_error);
        return result;
    }
    if (tracked->logical_references > 1) {
        --tracked->logical_references;
        --g_state.counters.logical_views;
        g_state.counters.active_saved_view_bytes -= tracked->size;
        saturating_increment(g_state.counters.deferred_unmaps);
        ReleaseSRWLockExclusive(&g_state.lock);
        SetLastError(entry_error);
        return TRUE;
    }

    saturating_increment(g_state.counters.physical_unmap_calls);
    const BOOL result = g_state.real_unmap_view_of_file(base);
    const DWORD result_error = GetLastError();
    if (result) {
        --g_state.counters.logical_views;
        --g_state.counters.physical_views;
        g_state.counters.physical_view_bytes -= tracked->size;
        *tracked = {};
    }
    ReleaseSRWLockExclusive(&g_state.lock);
    SetLastError(result_error);
    return result;
}

BOOL close_handle_impl(HANDLE handle) noexcept {
    AcquireSRWLockExclusive(&g_state.lock);
    MappingRecord* tracked = find_mapping(handle);
    if (!tracked) {
        ReleaseSRWLockExclusive(&g_state.lock);
        return g_state.real_close_handle(handle);
    }
    const BOOL result = g_state.real_close_handle(handle);
    const DWORD result_error = GetLastError();
    if (result) {
        *tracked = {};
        --g_state.counters.tracked_mapping_handles;
    }
    ReleaseSRWLockExclusive(&g_state.lock);
    SetLastError(result_error);
    return result;
}

HANDLE WINAPI hook_create_file_mapping_a(HANDLE file,
                                         LPSECURITY_ATTRIBUTES attributes,
                                         DWORD protect, DWORD maximum_high,
                                         DWORD maximum_low, LPCSTR name) {
    const auto* caller = static_cast<const std::uint8_t*>(_ReturnAddress());
    const auto* image = reinterpret_cast<const std::uint8_t*>(g_state.image);
    const bool supported_callsite = image &&
        caller == image + kResourceCreateMappingReturnRva;
    return create_file_mapping_impl(file, attributes, protect, maximum_high,
                                    maximum_low, name, supported_callsite);
}

LPVOID WINAPI hook_map_view_of_file(HANDLE mapping, DWORD access,
                                    DWORD offset_high, DWORD offset_low,
                                    SIZE_T requested) {
    return map_view_of_file_impl(mapping, access, offset_high, offset_low,
                                 requested);
}

BOOL WINAPI hook_unmap_view_of_file(LPCVOID base) {
    return unmap_view_of_file_impl(base);
}

BOOL WINAPI hook_close_handle(HANDLE handle) {
    return close_handle_impl(handle);
}

std::array<ImportSlot, 4> import_slots(std::uint8_t* image) noexcept {
    HMODULE kernel = GetModuleHandleW(L"KERNEL32.dll");
    std::array<ImportSlot, 4> slots{{
        {"CreateFileMappingA", kCreateFileMappingAIatRva,
         find_import_slot(image, kExpectedImageSize, "KERNEL32.dll",
                          "CreateFileMappingA"),
         reinterpret_cast<void*>(GetProcAddress(kernel, "CreateFileMappingA")),
         reinterpret_cast<void*>(&hook_create_file_mapping_a)},
        {"MapViewOfFile", kMapViewOfFileIatRva,
         find_import_slot(image, kExpectedImageSize, "KERNEL32.dll",
                          "MapViewOfFile"),
         reinterpret_cast<void*>(GetProcAddress(kernel, "MapViewOfFile")),
         reinterpret_cast<void*>(&hook_map_view_of_file)},
        {"UnmapViewOfFile", kUnmapViewOfFileIatRva,
         find_import_slot(image, kExpectedImageSize, "KERNEL32.dll",
                          "UnmapViewOfFile"),
         reinterpret_cast<void*>(GetProcAddress(kernel, "UnmapViewOfFile")),
         reinterpret_cast<void*>(&hook_unmap_view_of_file)},
        {"CloseHandle", kCloseHandleIatRva,
         find_import_slot(image, kExpectedImageSize, "KERNEL32.dll",
                          "CloseHandle"),
         reinterpret_cast<void*>(GetProcAddress(kernel, "CloseHandle")),
         reinterpret_cast<void*>(&hook_close_handle)},
    }};
    return slots;
}

bool validate_import_slots(const std::uint8_t* image,
                           const std::array<ImportSlot, 4>& slots,
                           ResMappingStatus& failure) noexcept {
    if (!signature(image, kResourceCreateMappingCallRva, kCreateMappingCall) ||
        !signature(image, kResourceMapViewCallRva, kMapViewCall) ||
        !signature(image, kResourceUnmapViewCallRva, kUnmapViewCall)) {
        failure = ResMappingStatus::import_contract_mismatch;
        return false;
    }
    for (const auto& slot : slots) {
        if (!slot.cell || !slot.expected ||
            reinterpret_cast<const std::uint8_t*>(slot.cell) !=
                image + slot.expected_rva) {
            failure = ResMappingStatus::import_contract_mismatch;
            return false;
        }
        if (*slot.cell != slot.expected) {
            failure = ResMappingStatus::foreign_hook;
            return false;
        }
    }
    return true;
}

void set_status_locked(ResMappingStatus status) noexcept {
    g_state.status = status;
    g_state.counters.status = status;
    g_state.counters.hooks_active = g_state.hooks_active;
}

} // namespace

bool install_res_mapping_hooks(bool sites_verified) noexcept {
    AcquireSRWLockExclusive(&g_state.lock);
    if (g_state.hooks_active) {
        ReleaseSRWLockExclusive(&g_state.lock);
        return true;
    }
    if (!sites_verified) {
        saturating_increment(g_state.counters.hook_validation_failures);
        set_status_locked(ResMappingStatus::unsupported_image);
        ReleaseSRWLockExclusive(&g_state.lock);
        return false;
    }

    auto* image = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
    auto slots = import_slots(image);
    ResMappingStatus failure = ResMappingStatus::import_contract_mismatch;
    if (!validate_import_slots(image, slots, failure)) {
        saturating_increment(g_state.counters.hook_validation_failures);
        set_status_locked(failure);
        ReleaseSRWLockExclusive(&g_state.lock);
        return false;
    }

    auto* first = reinterpret_cast<std::uint8_t*>(slots.front().cell);
    auto* last = first;
    for (const auto& slot : slots) {
        first = std::min(first, reinterpret_cast<std::uint8_t*>(slot.cell));
        last = std::max(last, reinterpret_cast<std::uint8_t*>(slot.cell));
    }
    SYSTEM_INFO system_info{};
    GetSystemInfo(&system_info);
    const auto first_page = reinterpret_cast<std::uintptr_t>(first) &
        ~(static_cast<std::uintptr_t>(system_info.dwPageSize) - 1u);
    const auto last_page = reinterpret_cast<std::uintptr_t>(last) &
        ~(static_cast<std::uintptr_t>(system_info.dwPageSize) - 1u);
    if (!system_info.dwPageSize || first_page != last_page) {
        saturating_increment(g_state.counters.hook_validation_failures);
        set_status_locked(ResMappingStatus::import_contract_mismatch);
        ReleaseSRWLockExclusive(&g_state.lock);
        return false;
    }

    DWORD protection = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(first_page),
                        system_info.dwPageSize, PAGE_READWRITE, &protection)) {
        saturating_increment(g_state.counters.hook_validation_failures);
        set_status_locked(ResMappingStatus::protection_failure);
        ReleaseSRWLockExclusive(&g_state.lock);
        return false;
    }

    std::size_t patched = 0;
    for (; patched < slots.size(); ++patched) {
        const auto& slot = slots[patched];
        void* const previous = InterlockedCompareExchangePointer(
            slot.cell, slot.replacement, slot.expected);
        if (previous != slot.expected) break;
    }
    if (patched != slots.size()) {
        while (patched > 0) {
            --patched;
            const auto& slot = slots[patched];
            InterlockedCompareExchangePointer(slot.cell, slot.expected,
                                              slot.replacement);
        }
        DWORD ignored = 0;
        VirtualProtect(reinterpret_cast<void*>(first_page), system_info.dwPageSize,
                       protection, &ignored);
        saturating_increment(g_state.counters.hook_validation_failures);
        set_status_locked(ResMappingStatus::patch_race);
        ReleaseSRWLockExclusive(&g_state.lock);
        return false;
    }

    DWORD ignored = 0;
    const BOOL restored = VirtualProtect(reinterpret_cast<void*>(first_page),
                                         system_info.dwPageSize, protection,
                                         &ignored);
    FlushInstructionCache(GetCurrentProcess(), first,
                          static_cast<SIZE_T>(last - first) + sizeof(void*));
    if (!restored) {
        DWORD writable_protection = 0;
        VirtualProtect(reinterpret_cast<void*>(first_page),
                       system_info.dwPageSize, PAGE_READWRITE,
                       &writable_protection);
        for (const auto& slot : slots)
            InterlockedCompareExchangePointer(slot.cell, slot.expected,
                                              slot.replacement);
        DWORD rollback_ignored = 0;
        VirtualProtect(reinterpret_cast<void*>(first_page),
                       system_info.dwPageSize, protection,
                       &rollback_ignored);
        saturating_increment(g_state.counters.hook_validation_failures);
        set_status_locked(ResMappingStatus::protection_failure);
        ReleaseSRWLockExclusive(&g_state.lock);
        return false;
    }

    g_state.image = reinterpret_cast<HMODULE>(image);
    g_state.real_create_file_mapping =
        reinterpret_cast<CreateFileMappingAFn>(slots[0].expected);
    g_state.real_map_view_of_file =
        reinterpret_cast<MapViewOfFileFn>(slots[1].expected);
    g_state.real_unmap_view_of_file =
        reinterpret_cast<UnmapViewOfFileFn>(slots[2].expected);
    g_state.real_close_handle = reinterpret_cast<CloseHandleFn>(slots[3].expected);
    g_state.hooks_active = true;
    g_state.accepting = true;
    set_status_locked(ResMappingStatus::active);
    ReleaseSRWLockExclusive(&g_state.lock);
    return true;
}

void uninstall_res_mapping_hooks() noexcept {
    AcquireSRWLockExclusive(&g_state.lock);
    if (!g_state.hooks_active || !g_state.image) {
        set_status_locked(ResMappingStatus::inactive);
        ReleaseSRWLockExclusive(&g_state.lock);
        return;
    }
    g_state.accepting = false;

    for (auto& view : g_state.views) {
        if (!view.base) continue;
        saturating_increment(g_state.counters.physical_unmap_calls);
        g_state.real_unmap_view_of_file(view.base);
        view = {};
    }
    g_state.counters.logical_views = 0;
    g_state.counters.physical_views = 0;
    g_state.counters.physical_view_bytes = 0;
    g_state.counters.active_saved_view_bytes = 0;
    for (auto& mapping : g_state.mappings) mapping = {};
    g_state.counters.tracked_mapping_handles = 0;

    auto* image = reinterpret_cast<std::uint8_t*>(g_state.image);
    auto slots = import_slots(image);
    auto* first = reinterpret_cast<std::uint8_t*>(slots.front().cell);
    auto* last = first;
    bool valid = true;
    for (const auto& slot : slots) {
        if (!slot.cell || !slot.expected) {
            valid = false;
            break;
        }
        first = std::min(first, reinterpret_cast<std::uint8_t*>(slot.cell));
        last = std::max(last, reinterpret_cast<std::uint8_t*>(slot.cell));
    }
    SYSTEM_INFO system_info{};
    GetSystemInfo(&system_info);
    const auto first_page = reinterpret_cast<std::uintptr_t>(first) &
        ~(static_cast<std::uintptr_t>(system_info.dwPageSize) - 1u);
    const auto last_page = reinterpret_cast<std::uintptr_t>(last) &
        ~(static_cast<std::uintptr_t>(system_info.dwPageSize) - 1u);
    valid = valid && system_info.dwPageSize && first_page == last_page;
    bool foreign = false;
    DWORD protection = 0;
    if (!valid || !VirtualProtect(reinterpret_cast<void*>(first_page),
                                  system_info.dwPageSize, PAGE_READWRITE,
                                  &protection)) {
        set_status_locked(ResMappingStatus::protection_failure);
        ReleaseSRWLockExclusive(&g_state.lock);
        return;
    }
    for (const auto& slot : slots) {
        void* const previous = InterlockedCompareExchangePointer(
            slot.cell, slot.expected, slot.replacement);
        if (previous != slot.replacement) foreign = true;
    }
    DWORD ignored = 0;
    VirtualProtect(reinterpret_cast<void*>(first_page), system_info.dwPageSize,
                   protection, &ignored);
    FlushInstructionCache(GetCurrentProcess(), first,
                          static_cast<SIZE_T>(last - first) + sizeof(void*));
    g_state.hooks_active = false;
    g_state.image = nullptr;
    set_status_locked(foreign ? ResMappingStatus::teardown_foreign_hook
                              : ResMappingStatus::inactive);
    ReleaseSRWLockExclusive(&g_state.lock);
}

bool res_mapping_view_provenance(const void* view_base, std::wstring& physical) {
    if (!view_base) return false;
    AcquireSRWLockShared(&g_state.lock);
    bool found = false;
    try {
        for (const auto& view : g_state.views)
            if (view.base == view_base) {
                if (!view.physical.empty()) { physical = view.physical; found = true; }
                break;
            }
        if (!found)
            for (const auto& view : g_state.other_views)
                if (view.base == view_base) { physical = view.physical; found = true; break; }
    } catch (...) { found = false; }
    ReleaseSRWLockShared(&g_state.lock);
    return found;
}

bool res_mapping_view_containing(const void* address, std::uint64_t size,
    const void*& view_base, std::wstring& physical) {
    view_base = nullptr;
    const auto start = reinterpret_cast<std::uintptr_t>(address);
    if (!address || !size || size > UINTPTR_MAX - start) return false;
    const auto inside = [start, size](const void* base, std::uint64_t extent) {
        const auto origin = reinterpret_cast<std::uintptr_t>(base);
        return base && extent && start >= origin && start - origin <= extent &&
            size <= extent - (start - origin);
    };
    AcquireSRWLockShared(&g_state.lock);
    bool found = false;
    try {
        for (const auto& view : g_state.views)
            if (view.logical_references && !view.physical.empty() && inside(view.base, view.size)) {
                view_base = view.base; physical = view.physical; found = true; break;
            }
        if (!found)
            for (const auto& view : g_state.other_views)
                if (!view.physical.empty() && inside(view.base, view.size)) {
                    view_base = view.base; physical = view.physical; found = true; break;
                }
    } catch (...) { found = false; view_base = nullptr; }
    ReleaseSRWLockShared(&g_state.lock);
    return found;
}

ResMappingSnapshot res_mapping_snapshot() noexcept {
    AcquireSRWLockShared(&g_state.lock);
    ResMappingSnapshot snapshot = g_state.counters;
    snapshot.status = g_state.status;
    snapshot.hooks_active = g_state.hooks_active;
    ReleaseSRWLockShared(&g_state.lock);
    return snapshot;
}

const char* res_mapping_status_name(ResMappingStatus status) noexcept {
    switch (status) {
    case ResMappingStatus::not_attempted: return "not_attempted";
    case ResMappingStatus::active: return "active";
    case ResMappingStatus::unsupported_image: return "unsupported_image";
    case ResMappingStatus::import_contract_mismatch:
        return "import_contract_mismatch";
    case ResMappingStatus::foreign_hook: return "foreign_hook";
    case ResMappingStatus::protection_failure: return "protection_failure";
    case ResMappingStatus::patch_race: return "patch_race";
    case ResMappingStatus::inactive: return "inactive";
    case ResMappingStatus::teardown_foreign_hook:
        return "teardown_foreign_hook";
    }
    return "unknown";
}

#ifdef EID3D11_TESTING
namespace res_mapping_test {

void reset() noexcept {
    AcquireSRWLockExclusive(&g_state.lock);
    for (auto& view : g_state.views) {
        if (view.base) g_state.real_unmap_view_of_file(view.base);
        view = {};
    }
    for (auto& mapping : g_state.mappings) mapping = {};
    g_state.image = nullptr;
    g_state.hooks_active = false;
    g_state.accepting = true;
    g_state.status = ResMappingStatus::active;
    g_state.real_create_file_mapping = ::CreateFileMappingA;
    g_state.real_map_view_of_file = ::MapViewOfFile;
    g_state.real_unmap_view_of_file = ::UnmapViewOfFile;
    g_state.real_close_handle = ::CloseHandle;
    g_state.counters = {};
    g_state.counters.status = ResMappingStatus::active;
    ReleaseSRWLockExclusive(&g_state.lock);
}

HANDLE create_file_mapping(HANDLE file, LPSECURITY_ATTRIBUTES attributes,
                           DWORD protect, DWORD maximum_high,
                           DWORD maximum_low, LPCSTR name,
                           bool supported_resource_callsite) noexcept {
    return create_file_mapping_impl(file, attributes, protect, maximum_high,
                                    maximum_low, name,
                                    supported_resource_callsite);
}

LPVOID map_view_of_file(HANDLE mapping, DWORD access, DWORD offset_high,
                       DWORD offset_low, SIZE_T requested) noexcept {
    return map_view_of_file_impl(mapping, access, offset_high, offset_low,
                                 requested);
}

BOOL unmap_view_of_file(LPCVOID base) noexcept {
    return unmap_view_of_file_impl(base);
}

BOOL close_handle(HANDLE handle) noexcept {
    return close_handle_impl(handle);
}

} // namespace res_mapping_test
#endif

} // namespace owned
