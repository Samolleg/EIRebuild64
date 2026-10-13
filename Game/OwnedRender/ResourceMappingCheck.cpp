#include "ResourceMapping.h"
#include "../../external/owned-renderer/Source/res_mapping.h"
#include <cstdio>
#include <string>
#include <stdexcept>

static void check(bool value, const char* name) {
    if (!value) throw std::runtime_error(name);
}
int wmain(int argc, wchar_t** argv) try {
    check(argc == 2, "pass a new fixture file path");
    HANDLE file = CreateFileW(argv[1], GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    check(file != INVALID_HANDLE_VALUE, "create isolated fixture");
    char bytes[8193]{};
    bytes[0] = 'R'; bytes[8192] = 'Z';
    DWORD written = 0;
    check(WriteFile(file, bytes, sizeof(bytes), &written, nullptr) && written == sizeof(bytes), "fixture bytes");
    HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    check(mapping != nullptr, "mapping handle");
    auto* first = static_cast<char*>(OwnedRender::MapResourceView(mapping, FILE_MAP_READ, sizeof(bytes)));
    auto* second = static_cast<char*>(OwnedRender::MapResourceView(mapping, FILE_MAP_READ, sizeof(bytes)));
    check(first && second && first != second, "independent simultaneous views");
    check(first[0] == 'R' && first[8192] == 'Z', "actual mapped bytes");
    CloseHandle(mapping); // Views, not mapping handles, determine lifetime.
    std::wstring physical;
    const void* base = nullptr;
    check(owned::res_mapping_view_provenance(first, physical), "live provenance");
    HANDLE provenance = CreateFileW(physical.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    check(provenance != INVALID_HANDLE_VALUE, "provenance opens actual mapped file");
    BY_HANDLE_FILE_INFORMATION a{}, b{};
    check(GetFileInformationByHandle(file, &a) && GetFileInformationByHandle(provenance, &b), "file identities");
    check(a.dwVolumeSerialNumber == b.dwVolumeSerialNumber && a.nFileIndexHigh == b.nFileIndexHigh &&
        a.nFileIndexLow == b.nFileIndexLow, "provenance identity");
    CloseHandle(provenance);
    check(owned::res_mapping_view_containing(first + 17, sizeof(bytes) - 17, base, physical) && base == first, "full tail range");
    check(!owned::res_mapping_view_containing(first + 8192, 2, base, physical) && !base && physical.empty(), "reject page padding");
    check(!owned::res_mapping_view_containing(first, 0, base, physical), "reject empty range");
    check(!owned::res_mapping_view_containing(reinterpret_cast<void*>(UINTPTR_MAX - 2), 4, base, physical), "reject overflow");
    check(!owned::res_mapping_view_provenance(first + 1, physical), "exact base only");
    check(!owned::res_mapping_view_containing(bytes, 1, base, physical), "reject heap/stack bytes");
    auto snapshot = owned::res_mapping_snapshot();
    check(snapshot.physical_views == 2 && snapshot.physical_view_bytes == 2 * sizeof(bytes) &&
        !snapshot.hooks_active && snapshot.shared_reuses == 0, "truthful live counters");
    check(OwnedRender::UnmapResourceView(first) != FALSE, "close first");
    check(!owned::res_mapping_view_provenance(first, physical), "retired view unavailable");
    check(owned::res_mapping_view_provenance(second, physical), "second survives first close");
    check(OwnedRender::UnmapResourceView(second) != FALSE, "close second");
    mapping = CreateFileMappingW(file, nullptr, PAGE_READWRITE, 0, 0, nullptr);
    check(mapping != nullptr, "write mapping");
    auto* writable = OwnedRender::MapResourceView(mapping, FILE_MAP_WRITE, sizeof(bytes));
    check(writable != nullptr, "write view");
    check(!owned::res_mapping_view_provenance(writable, physical) &&
        !owned::res_mapping_view_containing(writable, 1, base, physical), "write view never accepted as immutable");
    check(OwnedRender::UnmapResourceView(writable) != FALSE, "close write view");
    CloseHandle(mapping);
    mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    check(mapping != nullptr, "reopen mapping");
    for (int i = 0; i < 64; ++i) {
        auto* view = OwnedRender::MapResourceView(mapping, FILE_MAP_READ, sizeof(bytes));
        check(view && owned::res_mapping_view_containing(view, sizeof(bytes), base, physical), "reopen/reuse");
        check(OwnedRender::UnmapResourceView(view) && !owned::res_mapping_view_provenance(view, physical), "repeated retirement");
    }
    auto* untracked = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    check(untracked && !owned::res_mapping_view_provenance(untracked, physical), "foreign mapping not claimed");
    UnmapViewOfFile(untracked);
    CloseHandle(mapping);
    CloseHandle(file);
    snapshot = owned::res_mapping_snapshot();
    check(snapshot.physical_views == 0 && snapshot.physical_view_bytes == 0 &&
        snapshot.physical_map_calls == 67 && snapshot.physical_unmap_calls == 67 &&
        snapshot.identity_rejections == 1 && snapshot.tracking_capacity_failures == 0, "final lifetime balance");
    std::puts("PASS: real mappings, file identity, range bounds, immutable provenance, independent views, retirement and reuse (67 views)");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
}
