#include "owned_resource_store.h"
#include <charconv>
#include <filesystem>


#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <new>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <cstdio>

namespace owned {
void bridge_append_probe_event(const char* event, const char* detail) noexcept;
namespace {

constexpr std::uint32_t kResSignature = 0x019ce23cu;
constexpr std::uint64_t kResHeaderBytes = 16;
constexpr std::uint64_t kResEntryBytes = 22;
std::atomic<std::uint64_t> next_container_incarnation{1};
std::atomic<std::uint64_t> all_immutable_bytes{0};

// Only the existing selected-member recorder supplies a destination.
struct SelectedDiagnosticTicks {
    std::int64_t* destination = nullptr;
    LARGE_INTEGER begin{};
    explicit SelectedDiagnosticTicks(std::int64_t* value) noexcept : destination(value) {
        if (destination) QueryPerformanceCounter(&begin);
    }
    ~SelectedDiagnosticTicks() noexcept {
        if (!destination || begin.QuadPart <= 0) return;
        LARGE_INTEGER end{};
        if (QueryPerformanceCounter(&end) && end.QuadPart >= begin.QuadPart)
            *destination += end.QuadPart - begin.QuadPart;
    }
};

void fail(ResourceFailure& failure, ResourceFailureCode code,
          const std::string& message) {
    failure.code = code;
    failure.message = message;
}

std::uint64_t allocate_container_incarnation(ResourceFailure& failure) {
    auto incarnation = next_container_incarnation.load(std::memory_order_relaxed);
    do {
        if (incarnation == UINT64_MAX) {
            fail(failure, ResourceFailureCode::file_open, "resource incarnation limit");
            return 0;
        }
    } while (!next_container_incarnation.compare_exchange_weak(
        incarnation, incarnation+1, std::memory_order_relaxed));
    return incarnation;
}

// The same physical file version across opens: volume, file ID, size and
// last write/change times. Any rewrite or replacement changes it. 0 if the
// file system cannot say, which keeps the per-open incarnation identity.
std::uint64_t physical_file_key(HANDLE handle, std::uint64_t size) noexcept {
    BY_HANDLE_FILE_INFORMATION file{};
    FILE_BASIC_INFO basic{};
    if (!GetFileInformationByHandle(handle, &file) ||
        !GetFileInformationByHandleEx(handle, FileBasicInfo, &basic, sizeof(basic)))
        return 0;
    const std::uint64_t values[] = {
        file.dwVolumeSerialNumber,
        (std::uint64_t{file.nFileIndexHigh} << 32) | file.nFileIndexLow,
        size,
        (std::uint64_t{file.ftLastWriteTime.dwHighDateTime} << 32) | file.ftLastWriteTime.dwLowDateTime,
        static_cast<std::uint64_t>(basic.ChangeTime.QuadPart)};
    std::uint64_t hash = 14695981039346656037ull;
    for (const auto value : values)
        for (unsigned byte = 0; byte < 8; ++byte) {
            hash ^= (value >> (byte * 8)) & 0xffu;
            hash *= 1099511628211ull;
        }
    return hash ? hash : 1;
}

bool copy_selected_memory_bytes(void* owned, const void* selected, std::size_t size) noexcept {
    __try { std::memcpy(owned, selected, size); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool checked_add(std::uint64_t left, std::uint64_t right,
                 std::uint64_t& result) noexcept {
    if (right > (std::numeric_limits<std::uint64_t>::max)() - left) return false;
    result = left + right;
    return true;
}

bool checked_multiply(std::uint64_t left, std::uint64_t right,
                      std::uint64_t& result) noexcept {
    if (left && right > (std::numeric_limits<std::uint64_t>::max)() / left)
        return false;
    result = left * right;
    return true;
}

std::uint16_t read_u16(const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint16_t>(bytes[0]) |
        static_cast<std::uint16_t>(bytes[1]) << 8;
}

std::uint32_t read_u32(const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint32_t>(bytes[0]) |
        static_cast<std::uint32_t>(bytes[1]) << 8 |
        static_cast<std::uint32_t>(bytes[2]) << 16 |
        static_cast<std::uint32_t>(bytes[3]) << 24;
}

std::int32_t read_i32(const std::uint8_t* bytes) noexcept {
    return static_cast<std::int32_t>(read_u32(bytes));
}

// The game folds resource names with the CRT tolower of its "Font CharSet"
// code page (1251 or 1252) for the hash and the compare. Bytes 0xE0-0xFF are
// lowercase letters in both, which tolower keeps; the other high bytes
// depend on the code page and stay unsupported.
bool high_byte_folds_to_itself(unsigned char value) noexcept { return value >= 0xE0u; }

bool canonical_resource_name(const std::string& input, std::string& canonical,
                             ResourceFailure& failure) {
    canonical.clear();
    if (input.empty() || input.size() > 1024) {
        fail(failure, ResourceFailureCode::invalid_name,
             "resource name is empty or exceeds 1024 bytes");
        return false;
    }
    canonical.reserve(input.size());
    std::size_t segment_start = 0;
    for (std::size_t index = 0; index < input.size(); ++index) {
        const auto value = static_cast<unsigned char>(input[index]);
        if (value >= 0x80u && !high_byte_folds_to_itself(value)) {
            fail(failure, ResourceFailureCode::unsupported_encoding,
                 "high-bit resource lookup requires an unproved legacy locale rule");
            return false;
        }
        char output = static_cast<char>(value);
        if (output == '/') output = '\\';
        if (output == ':' || output == '\0') {
            fail(failure, ResourceFailureCode::invalid_name,
                 "resource name contains an invalid byte");
            return false;
        }
        if (output >= 'A' && output <= 'Z')
            output = static_cast<char>(output + ('a' - 'A'));
        canonical.push_back(output);
        if (output == '\\') {
            const std::string segment = canonical.substr(segment_start,
                                                          index - segment_start);
            if (segment.empty() || segment == "..") {
                fail(failure, ResourceFailureCode::invalid_name,
                     "resource name escapes or contains an empty path segment");
                return false;
            }
            segment_start = index + 1;
        }
    }
    if (canonical.front() == '\\' || canonical.back() == '\\' ||
        canonical.substr(segment_start) == "..") {
        fail(failure, ResourceFailureCode::invalid_name,
             "resource name is absolute or escapes its loose root");
        return false;
    }
    failure = {};
    return true;
}

// Names whose game folding is known: ASCII plus high_byte_folds_to_itself.
bool ascii_name(const std::string& value) noexcept {
    return std::all_of(value.begin(), value.end(), [](char byte) {
        const auto code = static_cast<unsigned char>(byte);
        return (code < 0x80u || high_byte_folds_to_itself(code)) && byte != '\0';
    });
}

std::string fold_ascii(const std::string& input) {
    std::string output = input;
    for (char& value : output)
        if (value >= 'A' && value <= 'Z') value = static_cast<char>(value + 32);
    return output;
}

std::uint32_t legacy_ascii_hash(const std::string& name,
                                std::uint32_t entry_count) noexcept {
    std::uint32_t sum = 0;
    for (unsigned char value : name) {
        if (value >= 'A' && value <= 'Z') value += 'a' - 'A';
        sum += value;
    }
    return entry_count ? sum % entry_count : 0;
}

std::string base_name_utf8(const std::wstring& path) {
    const std::size_t slash = path.find_last_of(L"\\/");
    const std::wstring name = slash == std::wstring::npos ? path : path.substr(slash + 1);
    if (name.empty()) return "container";
    const int count = WideCharToMultiByte(CP_UTF8, 0, name.data(),
                                          static_cast<int>(name.size()), nullptr,
                                          0, nullptr, nullptr);
    if (count <= 0) return "container";
    std::string output(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, name.data(), static_cast<int>(name.size()),
                        output.data(), count, nullptr, nullptr);
    return output;
}

struct StoreCounters {
    std::atomic<std::uint64_t> live_immutable_bytes{0};
    std::atomic<std::uint64_t> immutable_byte_limit{UINT64_MAX};
    std::atomic<std::uint64_t> archive_owner_count{0};
    std::atomic<std::uint64_t> read_calls{0};
    std::atomic<std::uint64_t> read_bytes{0};
    std::atomic<std::uint64_t> resolved_loose{0};
    std::atomic<std::uint64_t> resolved_archive{0};
    std::atomic<std::uint64_t> cache_hits{0};
#ifdef EID3D11_TESTING
    std::atomic<std::uint64_t> selected_comparison_calls{0};
    std::atomic<std::uint64_t> selected_comparison_span_bytes{0};
#endif
};

class FileOwner {
public:
    FileOwner(std::wstring path, std::shared_ptr<StoreCounters> counters,
              bool archive)
        : path_(std::move(path)), counters_(std::move(counters)), archive_(archive) {}

    ~FileOwner() {
        if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
        if (archive_ && opened_)
            counters_->archive_owner_count.fetch_sub(1, std::memory_order_relaxed);
    }

    bool open(ResourceFailure& failure) {
        handle_ = CreateFileW(path_.c_str(), GENERIC_READ,
                              FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS,
                              nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) {
            const auto error = GetLastError();
            fail(failure, error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND
                    ? ResourceFailureCode::not_found : ResourceFailureCode::file_open,
                 "resource container open failed path=\"" + std::filesystem::path(path_).u8string() +
                 "\" win32=" + std::to_string(error));
            bridge_append_probe_event("resource_open_failed", failure.message.c_str());
            return false;
        }
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(handle_, &size) || size.QuadPart < 0) {
            fail(failure, ResourceFailureCode::file_open,
                 "resource container size could not be read");
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
            return false;
        }
        incarnation_ = allocate_container_incarnation(failure);
        if (!incarnation_) {
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
            return false;
        }
        size_ = static_cast<std::uint64_t>(size.QuadPart);
        file_key_ = physical_file_key(handle_, size_);
        opened_ = true;
        if (archive_)
            counters_->archive_owner_count.fetch_add(1, std::memory_order_relaxed);
        failure = {};
        return true;
    }

    bool read(std::uint64_t offset, std::uint64_t size,
              std::vector<std::uint8_t>& output,
              ResourceFailure& failure) const {
        std::uint64_t end = 0;
        if (!checked_add(offset, size, end) || end > size_ ||
            size > (std::numeric_limits<std::size_t>::max)()) {
            fail(failure, ResourceFailureCode::archive_bounds,
                 "resource read is outside its owning file");
            return false;
        }
        try {
            output.resize(static_cast<std::size_t>(size));
        } catch (...) {
            fail(failure, ResourceFailureCode::file_read,
                 "resource read allocation failed");
            return false;
        }
        std::lock_guard<std::mutex> guard(lock_);
        LARGE_INTEGER position{};
        position.QuadPart = static_cast<LONGLONG>(offset);
        if (!SetFilePointerEx(handle_, position, nullptr, FILE_BEGIN)) {
            fail(failure, ResourceFailureCode::file_read,
                 "resource seek failed");
            return false;
        }
        std::uint64_t cursor = 0;
        while (cursor < size) {
            const DWORD amount = static_cast<DWORD>((std::min)(
                size - cursor,
                static_cast<std::uint64_t>((std::numeric_limits<DWORD>::max)())));
            DWORD read_bytes = 0;
            if (!ReadFile(handle_, output.data() + static_cast<std::size_t>(cursor),
                          amount, &read_bytes, nullptr) || read_bytes != amount) {
                fail(failure, ResourceFailureCode::file_read,
                     "resource read was short or failed");
                return false;
            }
            cursor += amount;
        }
        counters_->read_calls.fetch_add(1, std::memory_order_relaxed);
        counters_->read_bytes.fetch_add(size, std::memory_order_relaxed);
        failure = {};
        return true;
    }

    const std::wstring& path() const noexcept { return path_; }
    std::uint64_t size() const noexcept { return size_; }
    std::uint64_t incarnation() const noexcept { return incarnation_; }
    std::uint64_t file_key() const noexcept { return file_key_; }

private:
    std::wstring path_;
    std::shared_ptr<StoreCounters> counters_;
    bool archive_ = false;
    bool opened_ = false;
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    std::uint64_t size_ = 0;
    std::uint64_t incarnation_ = 0;
    std::uint64_t file_key_ = 0;
    mutable std::mutex lock_;
};

struct ArchiveEntry {
    std::int32_t next = -1;
    std::uint32_t size = 0;
    std::uint32_t offset = 0;
    std::string raw_name;
    std::string folded_name;
    bool ascii = false;
};

bool parse_archive_records(const std::uint8_t* table, std::uint32_t count,
                           const std::uint8_t* names,
                           std::uint32_t names_bytes,
                           std::uint64_t container_bytes,
                           std::vector<ArchiveEntry>& entries,
                           ResourceFailure& failure,
                           const ResourceStore* budget = nullptr,
                           std::shared_ptr<const std::vector<std::uint8_t>>* reservation = nullptr) {
    // The game opens an empty archive and every lookup in it misses.
    if (!count) {
        entries.clear();
        failure = {};
        return true;
    }
    if (budget) {
        std::uint64_t bytes = static_cast<std::uint64_t>(count)*sizeof(ArchiveEntry);
        for (std::uint32_t index = 0; index < count; ++index)
            bytes += 2ull*(read_u16(table+index*kResEntryBytes+16)+1u);
        if (bytes > SIZE_MAX) {
            fail(failure, ResourceFailureCode::archive_bounds, "RES index reservation exceeds address space");
            return false;
        }
        *reservation = budget->allocate_immutable_bytes(0, failure, static_cast<std::size_t>(bytes));
        if (!*reservation) return false;
    }
    try { entries.resize(count); } catch (...) {
        fail(failure, ResourceFailureCode::archive_header,
             "RES index allocation failed");
        return false;
    }
    std::uint32_t unreachable = 0;
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto* source = table + index * kResEntryBytes;
        ArchiveEntry& entry = entries[index];
        entry.next = read_i32(source);
        entry.size = read_u32(source + 4);
        entry.offset = read_u32(source + 8);
        const std::uint16_t name_length = read_u16(source + 16);
        const std::uint32_t name_offset = read_u32(source + 18);
        std::uint64_t data_end = 0;
        std::uint64_t name_end = 0;
        // The game validates no record: a bad one only matters when a lookup
        // walks onto it, where the game reads outside the archive. Keep the
        // archive and make that record unreachable (a chain index out of
        // range ends the chain; a data or name range outside the archive, or
        // a name with an embedded NUL, never matches - as in the game).
        if (entry.next < -1 || entry.next >= static_cast<std::int32_t>(count)) {
            entry.next = -1;
            ++unreachable;
        }
        if (!checked_add(entry.offset, entry.size, data_end) ||
            data_end > container_bytes ||
            !checked_add(name_offset, name_length, name_end) ||
            name_end > names_bytes) {
            ++unreachable;
            continue;
        }
        entry.raw_name.assign(
            reinterpret_cast<const char*>(names + name_offset),
            name_length);
        if (entry.raw_name.find('\0') != std::string::npos) {
            ++unreachable;
            continue;
        }
        entry.ascii = ascii_name(entry.raw_name);
        if (entry.ascii) entry.folded_name = fold_ascii(entry.raw_name);
    }
    if (unreachable) {
        char detail[160];
        std::snprintf(detail, sizeof(detail), "%u of %u RES records unreachable (bad range, chain or name)",
                      unreachable, count);
        bridge_append_probe_event("res_records_unreachable", detail);
    }
    // The shipped lookup accepts the table as mounted and follows only the
    // requested name's bucket chain. Some active archives contain dead records
    // that are not reachable from their nominal bucket; those records remain
    // invisible, but do not invalidate otherwise reachable archive contents.
    failure = {};
    return true;
}

bool parse_archive_bytes(const std::uint8_t* data, std::uint64_t bytes,
                         std::vector<ArchiveEntry>& entries,
                         ResourceFailure& failure, const ResourceStore* budget = nullptr,
                         std::shared_ptr<const std::vector<std::uint8_t>>* reservation = nullptr) {
    if (!data || bytes < kResHeaderBytes) {
        fail(failure, ResourceFailureCode::archive_header,
             "RES header is truncated");
        return false;
    }
    const std::uint32_t signature = read_u32(data);
    if (signature != kResSignature) {
        fail(failure, ResourceFailureCode::archive_signature,
             "RES signature or archive compression variant is unsupported");
        return false;
    }
    const std::uint32_t count = read_u32(data + 4);
    const std::uint32_t table_offset = read_u32(data + 8);
    std::uint64_t table_bytes = 0;
    std::uint64_t names_offset = 0;
    if (!checked_multiply(count, kResEntryBytes, table_bytes) ||
        !checked_add(table_offset, table_bytes, names_offset) ||
        names_offset > bytes) {
        fail(failure, ResourceFailureCode::archive_bounds,
             "RES table is outside the container");
        return false;
    }
    // The game's mapped reader (used for every nested archive) places the
    // name pool right after the table and ignores the stored pool size;
    // names are bounded by the container instead.
    const std::uint32_t names_bytes = static_cast<std::uint32_t>(
        (std::min)(bytes - names_offset, std::uint64_t{UINT32_MAX}));
    return parse_archive_records(data + table_offset, count,
                                 data + names_offset, names_bytes, bytes,
                                 entries, failure, budget, reservation);
}

bool find_archive_entry(const std::vector<ArchiveEntry>& entries,
                        const std::string& canonical,
                        std::uint32_t& index) noexcept {
    if (entries.empty()) return false;
    std::int32_t cursor = static_cast<std::int32_t>(
        legacy_ascii_hash(canonical, static_cast<std::uint32_t>(entries.size())));
    std::size_t steps = 0;
    while (cursor >= 0 && steps++ < entries.size()) {
        const ArchiveEntry& entry = entries[static_cast<std::size_t>(cursor)];
        if (entry.ascii && entry.folded_name == canonical) {
            index = static_cast<std::uint32_t>(cursor);
            return true;
        }
        cursor = entry.next;
    }
    return false;
}

struct ArchiveMount {
    std::shared_ptr<FileOwner> file;
    std::vector<ArchiveEntry> entries;
    ResourceContainerInfo container;
    std::uint8_t kinds = kArchiveAllKinds;
};

struct ImmutableByteStorage {
    std::shared_ptr<StoreCounters> counters;
    std::uint64_t reserved;
    std::vector<std::uint8_t> bytes;
    ImmutableByteStorage(std::shared_ptr<StoreCounters> owner, std::uint64_t reservation, std::size_t size)
        : counters(std::move(owner)), reserved(reservation), bytes(size) {}
    ~ImmutableByteStorage() {
        counters->live_immutable_bytes.fetch_sub(reserved,std::memory_order_relaxed);
        all_immutable_bytes.fetch_sub(reserved,std::memory_order_relaxed);
    }
};

bool selected_bytes_equal(const void* selected, const std::uint8_t* owned,
                           std::size_t bytes, [[maybe_unused]] StoreCounters& counters) noexcept {
    if (!selected || !owned) return false;
#ifdef EID3D11_TESTING
    counters.selected_comparison_calls.fetch_add(1, std::memory_order_relaxed);
    counters.selected_comparison_span_bytes.fetch_add(bytes, std::memory_order_relaxed);
#endif
    __try { return std::memcmp(selected, owned, bytes) == 0; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

} // namespace

struct ResourceLocation::State {
    std::shared_ptr<FileOwner> file;
    std::uint64_t offset = 0;
    std::uint64_t bytes = 0;
    mutable std::mutex read_lock;
    mutable std::weak_ptr<const std::vector<std::uint8_t>> backing;
    // The held file denies writers; its exact member proof lasts for this incarnation.
    mutable bool read_only_mapping_verified = false; // Protected by read_lock.
};

struct FileSlice final : ResourceSlice {
    std::shared_ptr<FileOwner> file;
    std::uint64_t base = 0;
    bool read(std::uint64_t offset, std::uint64_t bytes, std::vector<std::uint8_t>& output,
              ResourceFailure& failure) const override {
        std::uint64_t end = 0;
        if (!checked_add(offset, bytes, end) || end > size) {
            fail(failure, ResourceFailureCode::archive_bounds, "container slice read is outside its member");
            return false;
        }
        return file->read(base + offset, bytes, output, failure);
    }
};

struct ResourceStore::Impl {
    std::shared_ptr<StoreCounters> counters = std::make_shared<StoreCounters>();
    std::vector<std::wstring> loose_roots;
    std::vector<ArchiveMount> archives;
    std::map<std::wstring, ArchiveMount> selected_files;
    mutable std::mutex cache_lock;
    mutable std::unordered_map<std::string, ResourceLocation> cache;
    struct MemoryCopy {
        ResourceIdentity identity;
        std::weak_ptr<const std::vector<std::uint8_t>> bytes;
    };
    mutable std::unordered_multimap<std::string, MemoryCopy> memory_copies;
    mutable unsigned memory_copy_insertions = 0;
    mutable std::mutex container_lock;
    mutable std::vector<ResourceContainerInfo> loose_containers;
};

struct MemoryResourceArchive::Impl {
    ResourceBlob container;
    std::shared_ptr<const std::vector<std::uint8_t>> index_reservation;
    std::vector<ArchiveEntry> entries;
};

const char* resource_failure_code_name(ResourceFailureCode code) noexcept {
    switch (code) {
    case ResourceFailureCode::none: return "none";
    case ResourceFailureCode::invalid_name: return "invalid_name";
    case ResourceFailureCode::unsupported_encoding: return "unsupported_encoding";
    case ResourceFailureCode::not_found: return "not_found";
    case ResourceFailureCode::file_open: return "file_open";
    case ResourceFailureCode::file_read: return "file_read";
    case ResourceFailureCode::selection_mismatch: return "selection_mismatch";
    case ResourceFailureCode::archive_header: return "archive_header";
    case ResourceFailureCode::archive_signature: return "archive_signature";
    case ResourceFailureCode::archive_bounds: return "archive_bounds";
    case ResourceFailureCode::archive_chain: return "archive_chain";
    case ResourceFailureCode::archive_name: return "archive_name";
    case ResourceFailureCode::unsupported_compression: return "unsupported_compression";
    case ResourceFailureCode::cancelled: return "cancelled";
    case ResourceFailureCode::parse: return "parse";
    }
    return "unknown";
}

bool resource_canonical_name(const std::string& input, std::string& canonical,
                             ResourceFailure& failure) {
    return canonical_resource_name(input, canonical, failure);
}

void resource_store_set_incarnation_base(std::uint64_t base) noexcept {
    auto current = next_container_incarnation.load(std::memory_order_relaxed);
    while (current < base && !next_container_incarnation.compare_exchange_weak(
        current, base, std::memory_order_relaxed)) {}
}

// "incarnation:offset:bytes:name". The mesh cache builds this per queued
// world draw; a stream formatter cost about 90 ms of the first terrain frame.
std::string ResourceIdentity::key() const {
    char numbers[64];
    char* cursor = numbers;
    char* const end = numbers + sizeof(numbers);
    for (const std::uint64_t value : {container_incarnation, absolute_offset, bytes}) {
        cursor = std::to_chars(cursor, end, value).ptr;
        *cursor++ = ':';
    }
    std::string output;
    output.reserve(static_cast<std::size_t>(cursor - numbers) + canonical_name.size());
    output.append(numbers, cursor);
    output += canonical_name;
    return output;
}

ResourceStore::ResourceStore(std::uint64_t immutable_byte_limit) : impl_(std::make_unique<Impl>()) {
    impl_->counters->immutable_byte_limit.store(immutable_byte_limit,std::memory_order_relaxed);
}
ResourceStore::~ResourceStore() = default;
std::uint64_t owned_resource_live_bytes() noexcept { return all_immutable_bytes.load(std::memory_order_relaxed); }
ResourceStore::ResourceStore(ResourceStore&&) noexcept = default;
ResourceStore& ResourceStore::operator=(ResourceStore&&) noexcept = default;

std::shared_ptr<std::vector<std::uint8_t>> ResourceStore::allocate_immutable_bytes(
    std::size_t bytes, ResourceFailure& failure, std::size_t retained_extra) const {
    const auto owner = impl_->counters;
    if (retained_extra > UINT64_MAX-static_cast<std::uint64_t>(bytes)) {
        fail(failure,ResourceFailureCode::archive_bounds,"immutable byte reservation overflow");
        return nullptr;
    }
    const auto reserved = static_cast<std::uint64_t>(bytes)+retained_extra;
    auto used = owner->live_immutable_bytes.load(std::memory_order_relaxed);
    const auto limit = owner->immutable_byte_limit.load(std::memory_order_relaxed);
    do {
        if (used > limit || reserved > limit-used) {
            fail(failure,ResourceFailureCode::archive_bounds,"live immutable visual byte limit");
            return nullptr;
        }
    } while (!owner->live_immutable_bytes.compare_exchange_weak(used,used+reserved,std::memory_order_relaxed));
    try {
        auto storage = std::make_shared<ImmutableByteStorage>(owner,reserved,bytes);
        all_immutable_bytes.fetch_add(reserved,std::memory_order_relaxed);
        auto* value = &storage->bytes;
        failure = {};
        return std::shared_ptr<std::vector<std::uint8_t>>(std::move(storage),value);
    } catch (...) {
        owner->live_immutable_bytes.fetch_sub(reserved,std::memory_order_relaxed);
        fail(failure,ResourceFailureCode::file_read,"immutable visual byte allocation failed");
        return nullptr;
    }
}

bool ResourceStore::copy_selected_memory(const std::string& resource_name,
    const void* selected_bytes, std::uint32_t size, ResourceBlob& blob,
    ResourceFailure& failure) const {
    blob = {};
    std::string canonical;
    if (!canonical_resource_name(resource_name, canonical, failure)) return false;
    const auto address = reinterpret_cast<std::uintptr_t>(selected_bytes);
    if (!selected_bytes || !size || size > UINTPTR_MAX-address) {
        fail(failure, ResourceFailureCode::archive_bounds, "native-selected memory range");
        return false;
    }
    // Memory selections have no file provenance. Reuse only after comparing
    // every byte of this completed selection, never by its address or name.
    std::lock_guard<std::mutex> guard(impl_->cache_lock);
    const auto range = impl_->memory_copies.equal_range(canonical);
    for (auto entry = range.first; entry != range.second;) {
        auto backing = entry->second.bytes.lock();
        if (!backing) { entry = impl_->memory_copies.erase(entry); continue; }
        if (backing->size() == size &&
            selected_bytes_equal(selected_bytes, backing->data(), size, *impl_->counters)) {
            blob = {entry->second.identity, std::move(backing)};
            impl_->counters->cache_hits.fetch_add(1, std::memory_order_relaxed);
            failure = {};
            return true;
        }
        ++entry;
    }
    if (++impl_->memory_copy_insertions % 64 == 0)
        for (auto entry = impl_->memory_copies.begin(); entry != impl_->memory_copies.end();)
            if (entry->second.bytes.expired()) entry = impl_->memory_copies.erase(entry); else ++entry;
    auto bytes = allocate_immutable_bytes(size, failure);
    if (!bytes) return false;
    if (!copy_selected_memory_bytes(bytes->data(), selected_bytes, size)) {
        fail(failure, ResourceFailureCode::file_read, "native-selected memory unavailable");
        return false;
    }
    const auto incarnation = allocate_container_incarnation(failure);
    if (!incarnation) return false;
    blob.identity.container_incarnation = incarnation;
    blob.identity.canonical_name = std::move(canonical);
    blob.identity.bytes = size;
    blob.bytes = std::move(bytes);
    impl_->memory_copies.emplace(blob.identity.canonical_name, Impl::MemoryCopy{blob.identity, blob.bytes});
    failure = {};
    return true;
}

#ifdef EID3D11_TESTING
void ResourceStore::set_immutable_byte_limit_for_test(std::uint64_t bytes) noexcept {
    impl_->counters->immutable_byte_limit.store(bytes,std::memory_order_relaxed);
}
#endif

bool ResourceStore::open(const ResourceStoreConfig& config,
                         ResourceFailure& failure) {
    close();
    if (!config.archive_kinds.empty() && config.archive_kinds.size() != config.archives.size()) {
        fail(failure, ResourceFailureCode::parse, "archive kinds do not match the archive list");
        return false;
    }
    impl_->loose_roots = config.loose_roots;
    for (std::size_t index = 0; index < config.archives.size(); ++index) {
        auto file = std::make_shared<FileOwner>(config.archives[index],
                                                impl_->counters, true);
        if (!file->open(failure)) {
            bridge_append_probe_event("resource_store_open_failed", failure.message.c_str());
            close();
            return false;
        }
        ArchiveMount mount{};
        mount.file = file;
        if (!config.archive_kinds.empty()) mount.kinds = config.archive_kinds[index];
        mount.container.mount_ordinal = static_cast<std::uint32_t>(
            impl_->loose_roots.size() + index);
        mount.container.bytes = file->size();
        mount.container.label = base_name_utf8(file->path());
        mount.container.incarnation = file->incarnation();
        mount.container.file_key = file->file_key();
        std::vector<std::uint8_t> header;
        if (!file->read(0, kResHeaderBytes, header, failure)) {
            close();
            return false;
        }
        if (read_u32(header.data()) != kResSignature) {
            fail(failure, ResourceFailureCode::archive_signature,
                 "mounted RES uses an unsupported signature or compression variant");
            close();
            return false;
        }
        const std::uint32_t count = read_u32(header.data() + 4);
        const std::uint32_t table_offset = read_u32(header.data() + 8);
        const std::uint32_t names_bytes = read_u32(header.data() + 12);
        std::uint64_t table_bytes = 0;
        std::uint64_t metadata_bytes = 0;
        if (!checked_multiply(count, kResEntryBytes, table_bytes) ||
            !checked_add(table_bytes, names_bytes, metadata_bytes)) {
            fail(failure, ResourceFailureCode::archive_header,
                 "mounted RES index size is invalid");
            close();
            return false;
        }
        std::uint64_t table_end = 0;
        if (!checked_add(table_offset, metadata_bytes, table_end) ||
            table_end > file->size() ||
            metadata_bytes > (std::numeric_limits<std::size_t>::max)()) {
            fail(failure, ResourceFailureCode::archive_bounds,
                 "mounted RES table is outside its file");
            close();
            return false;
        }
        std::vector<std::uint8_t> metadata;
        if (!file->read(table_offset, metadata_bytes, metadata, failure)) {
            close();
            return false;
        }
        if (!parse_archive_records(metadata.data(), count,
                                   metadata.data() + table_bytes, names_bytes,
                                   file->size(), mount.entries, failure)) {
            close();
            return false;
        }
        impl_->archives.push_back(std::move(mount));
    }
    failure = {};
    return true;
}

void ResourceStore::close() noexcept {
    if (!impl_) return;
    std::lock_guard<std::mutex> cache_guard(impl_->cache_lock);
    impl_->cache.clear();
    impl_->memory_copies.clear();
    impl_->archives.clear();
    impl_->selected_files.clear();
    impl_->loose_roots.clear();
    std::lock_guard<std::mutex> container_guard(impl_->container_lock);
    impl_->loose_containers.clear();
}

bool ResourceStore::locate(const std::string& resource_name,
                           ResourceLocation& location,
                           ResourceFailure& failure) const {
    location = {};
    std::string canonical;
    if (!canonical_resource_name(resource_name, canonical, failure)) return false;
    // Serialize selection, not payload reads: concurrent requests for one
    // loose member must observe one open incarnation within this store.
    std::lock_guard<std::mutex> cache_guard(impl_->cache_lock);
    {
        const auto found = impl_->cache.find(canonical);
        if (found != impl_->cache.end()) {
            location = found->second;
            impl_->counters->cache_hits.fetch_add(1, std::memory_order_relaxed);
            failure = {};
            return true;
        }
    }
    const std::wstring relative(canonical.begin(), canonical.end());
    for (std::size_t index = 0; index < impl_->loose_roots.size(); ++index) {
        std::wstring path = impl_->loose_roots[index];
        if (!path.empty() && path.back() != L'\\' && path.back() != L'/')
            path.push_back(L'\\');
        path += relative;
        auto file = std::make_shared<FileOwner>(path, impl_->counters, false);
        ResourceFailure open_failure{};
        if (!file->open(open_failure)) {
            if (open_failure.code == ResourceFailureCode::not_found) continue;
            failure = std::move(open_failure);
            return false; // An unavailable override cannot select a lower mount.
        }
        location.identity_.canonical_name = canonical;
        location.identity_.container_incarnation = file->incarnation();
        location.identity_.container_file_key = file->file_key();
        location.identity_.bytes = file->size();
        location.identity_.mount_ordinal = static_cast<std::uint32_t>(index);
        location.identity_.loose_file = true;
        auto state = std::make_shared<ResourceLocation::State>();
        state->file = std::move(file);
        state->bytes = location.identity_.bytes;
        location.state_ = std::move(state);
        ResourceContainerInfo container{};
        container.mount_ordinal = static_cast<std::uint32_t>(index);
        container.loose_file = true;
        container.bytes = location.identity_.bytes;
        container.label = canonical;
        container.incarnation = location.identity_.container_incarnation;
        container.file_key = location.identity_.container_file_key;
        {
            std::lock_guard<std::mutex> guard(impl_->container_lock);
            const auto duplicate = std::find_if(
                impl_->loose_containers.begin(), impl_->loose_containers.end(),
                [&](const ResourceContainerInfo& item) {
                    return item.mount_ordinal == container.mount_ordinal &&
                           item.label == container.label && item.incarnation == container.incarnation;
                });
            if (duplicate == impl_->loose_containers.end())
                impl_->loose_containers.push_back(std::move(container));
        }
        impl_->counters->resolved_loose.fetch_add(1, std::memory_order_relaxed);
        impl_->cache.emplace(canonical, location);
        failure = {};
        return true;
    }
    // The texture manager's chain serves .mmp textures and the map editor's
    // terrain data (.json atlas profiles and semantic fields), which the
    // editor ships beside the MMP pages in a texture archive (Dejavu:
    // HDPack_Terrain.res, a textures.res entry). Everything else is a figure
    // manager lookup.
    const auto ends_with = [&](const char* suffix) {
        const std::size_t n = std::strlen(suffix);
        return canonical.size() >= n && canonical.compare(canonical.size() - n, n, suffix) == 0;
    };
    const std::uint8_t kind = ends_with(".mmp") || ends_with(".dds") || ends_with(".json") ? kArchiveTextures : kArchiveFigures;
    for (const ArchiveMount& archive : impl_->archives) {
        if (!(archive.kinds & kind)) continue;
        std::uint32_t entry_index = 0;
        if (!find_archive_entry(archive.entries, canonical, entry_index)) continue;
        const ArchiveEntry& entry = archive.entries[entry_index];
        location.identity_.canonical_name = canonical;
        location.identity_.container_incarnation = archive.container.incarnation;
        location.identity_.container_file_key = archive.container.file_key;
        location.identity_.absolute_offset = entry.offset;
        location.identity_.bytes = entry.size;
        location.identity_.mount_ordinal = archive.container.mount_ordinal;
        location.identity_.entry_index = entry_index;
        auto state = std::make_shared<ResourceLocation::State>();
        state->file = archive.file;
        state->offset = entry.offset;
        state->bytes = entry.size;
        location.state_ = std::move(state);
        impl_->counters->resolved_archive.fetch_add(1, std::memory_order_relaxed);
        impl_->cache.emplace(canonical, location);
        failure = {};
        return true;
    }
    fail(failure, ResourceFailureCode::not_found,
         "resource identity was not present in the configured mount set");
    return false;
}

bool ResourceStore::read(const ResourceLocation& location, ResourceBlob& blob,
                         ResourceFailure& failure, bool* backing_cache_hit,
                         SelectedResourceReadDiagnostic* diagnostic) const {
    blob = {};
    if (backing_cache_hit) *backing_cache_hit = false;
    if (!location.state_) {
        fail(failure, ResourceFailureCode::file_read,
             "resource location is empty");
        return false;
    }
    std::lock_guard<std::mutex> guard(location.state_->read_lock);
    if (auto backing = location.state_->backing.lock()) {
        if (backing_cache_hit) *backing_cache_hit = true;
        blob = {location.identity_,std::move(backing)};
        impl_->counters->cache_hits.fetch_add(1,std::memory_order_relaxed);
        failure = {};
        return true;
    }
    if (location.state_->bytes > SIZE_MAX) {
        fail(failure,ResourceFailureCode::archive_bounds,"immutable member exceeds address space");
        return false;
    }
    auto bytes = allocate_immutable_bytes(static_cast<std::size_t>(location.state_->bytes),failure);
    if (!bytes) return false;
    {
        SelectedDiagnosticTicks timing(diagnostic ? &diagnostic->file_read_ticks : nullptr);
        if (diagnostic) ++diagnostic->file_read_calls;
        if (!location.state_->file->read(location.state_->offset,
                                         location.state_->bytes, *bytes, failure))
            return false;
    }
    blob.identity = location.identity_;
    blob.bytes = std::move(bytes);
    location.state_->backing = blob.bytes;
    failure = {};
    return true;
}

bool ResourceStore::resolve(const std::string& resource_name, ResourceBlob& blob,
                            ResourceFailure& failure) const {
    ResourceLocation location;
    return locate(resource_name, location, failure) && read(location, blob, failure);
}

bool ResourceStore::read_completed_loose_file(const std::wstring& physical_file,
                                              const std::string& resource_name,
                                              ResourceBlob& blob,
                                              ResourceFailure& failure) const {
    if (!read_loose_file(physical_file, resource_name, blob, failure)) return false;
    if (blob.identity.bytes < 76u) {
        blob = {};
        fail(failure, ResourceFailureCode::archive_bounds, "completed texture file size limit");
        return false;
    }
    return true;
}

bool ResourceStore::read_loose_file(const std::wstring& physical_file,
                                  const std::string& resource_name,
                                  ResourceBlob& blob, ResourceFailure& failure) const {
    blob = {};
    std::string canonical;
    if (physical_file.empty() || !canonical_resource_name(resource_name, canonical, failure))
        return false;
    FileOwner file(physical_file, impl_->counters, false);
    if (!file.open(failure)) return false;
    if (file.size() > SIZE_MAX) {
        fail(failure, ResourceFailureCode::archive_bounds, "completed texture file size limit");
        return false;
    }
    ResourceBlob value;
    value.identity.canonical_name = std::move(canonical);
    value.identity.bytes = file.size();
    value.identity.loose_file = true;
    value.identity.container_incarnation = file.incarnation();
    value.identity.container_file_key = file.file_key();
    auto bytes = allocate_immutable_bytes(static_cast<std::size_t>(file.size()),failure);
    if (!bytes) return false;
    if (!file.read(0, file.size(), *bytes, failure)) return false;
    value.bytes = std::move(bytes);
    blob = std::move(value);
    failure = {};
    return true;
}

bool ResourceStore::resolve_selected_file(const std::wstring& physical_file,
                                          const std::string& resource_name,
                                          const void* selected_bytes,
                                          std::uint64_t selected_size,
                                          std::uint64_t selected_file_offset,
                                          ResourceBlob& blob,
                                          ResourceFailure& failure,
                                          SelectedResourceReadDiagnostic* diagnostic,
                                          bool verified_read_only_mapping,
                                          bool container) {
    blob = {};
    if (diagnostic) *diagnostic = {};
    std::string canonical;
    if (physical_file.empty() || !selected_bytes ||
        !canonical_resource_name(resource_name, canonical, failure)) return false;
    return resolve_selected_file_impl(physical_file, canonical, selected_bytes,
        selected_size, selected_file_offset, verified_read_only_mapping, blob, failure, diagnostic,
        container);
}

bool ResourceStore::resolve_selected_file_impl(const std::wstring& physical_file,
    const std::string& canonical, const void* selected_bytes,
    std::uint64_t selected_size, std::uint64_t selected_file_offset,
    bool verified_read_only_mapping, ResourceBlob& blob, ResourceFailure& failure,
    SelectedResourceReadDiagnostic* diagnostic, bool container) {
    SelectedDiagnosticTicks resolve_timing(diagnostic ? &diagnostic->resolve_ticks : nullptr);
    try {
    auto found = impl_->selected_files.find(physical_file);
    if (found == impl_->selected_files.end()) {
        auto file = std::make_shared<FileOwner>(physical_file, impl_->counters, true);
        if (!file->open(failure)) return false;
        std::vector<std::uint8_t> header;
        if (file->size() < 4u || !file->read(0, 4u, header, failure)) return false;
        ArchiveMount mount{};
        if (read_u32(header.data()) == kResSignature) {
            ResourceStore indexed;
            indexed.impl_->counters = impl_->counters;
            ResourceStoreConfig config{};
            config.archives.push_back(physical_file);
            if (!indexed.open(config, failure)) return false;
            mount = std::move(indexed.impl_->archives.front());
            // The initial immutable scope spans native selection and indexing.
            mount.file = std::move(file);
            mount.container.incarnation = mount.file->incarnation();
            mount.container.file_key = mount.file->file_key();
        } else {
            mount.file = std::move(file);
            mount.container.loose_file = true;
            mount.container.bytes = mount.file->size();
            mount.container.label = base_name_utf8(physical_file);
            mount.container.incarnation = mount.file->incarnation();
            mount.container.file_key = mount.file->file_key();
        }
        found = impl_->selected_files.emplace(physical_file, std::move(mount)).first;
    }
    const auto& mount = found->second;
    ResourceLocation location{};
    auto state = std::make_shared<ResourceLocation::State>();
    state->file = mount.file;
    location.identity_.canonical_name = canonical;
    location.identity_.container_incarnation = mount.container.incarnation;
    location.identity_.container_file_key = mount.container.file_key;
    location.identity_.loose_file = mount.container.loose_file;
    if (mount.container.loose_file) {
        state->bytes = mount.file->size();
    } else {
        std::uint32_t index = 0;
        if (!find_archive_entry(mount.entries, canonical, index)) {
            fail(failure, ResourceFailureCode::not_found, "native-selected member missing from its file");
            return false;
        }
        state->offset = mount.entries[index].offset;
        state->bytes = mount.entries[index].size;
        location.identity_.entry_index = index;
    }
    // Zero size is allowed only for an already-selected cached member whose
    // getter has no size output; the independently parsed index bounds it.
    if (!state->bytes || state->offset != selected_file_offset ||
        state->bytes > SIZE_MAX ||
        (selected_size && selected_size != state->bytes)) {
        fail(failure, ResourceFailureCode::archive_bounds, "native-selected member range mismatch");
        return false;
    }
    location.identity_.absolute_offset = state->offset;
    location.identity_.bytes = state->bytes;
    location.state_ = std::move(state);
    {
        // The ':' namespace cannot be a configured resource name. Native
        // selection still establishes the exact member on every acquisition.
        std::lock_guard<std::mutex> guard(impl_->cache_lock);
        const auto key = std::string(":selected:")+location.identity_.key();
        location = impl_->cache.emplace(key,location).first->second;
    }
    // A container whose exact member was already proven needs no bytes: its
    // nested archive reads index and members from the held file.
    const auto as_slice = [&] {
        auto slice = std::make_shared<FileSlice>();
        slice->file = location.state_->file;
        slice->base = location.state_->offset;
        slice->size = location.state_->bytes;
        blob = {};
        blob.identity = location.identity_;
        blob.slice = std::move(slice);
        failure = {};
        return true;
    };
    if (container && verified_read_only_mapping) {
        std::lock_guard<std::mutex> guard(location.state_->read_lock);
        if (location.state_->read_only_mapping_verified) return as_slice();
    }
    ResourceBlob result{};
    bool backing_cache_hit = false;
    if (!read(location, result, failure,
              diagnostic ? &backing_cache_hit : nullptr, diagnostic)) return false;
    if (diagnostic) {
        diagnostic->backing_cache_hit = backing_cache_hit;
        if (!backing_cache_hit) diagnostic->file_read_bytes = result.bytes->size();
    }
    {
        // read() releases this same lock before returning the pinned bytes.
        // Generic borrowed buffers always compare, even after a native proof.
        std::lock_guard<std::mutex> guard(location.state_->read_lock);
        if (!verified_read_only_mapping || !location.state_->read_only_mapping_verified) {
            if (diagnostic) diagnostic->comparison_span_bytes = result.bytes->size();
            SelectedDiagnosticTicks comparison_timing(
                diagnostic ? &diagnostic->comparison_ticks : nullptr);
            if (!selected_bytes_equal(selected_bytes, result.bytes->data(), result.bytes->size(), *impl_->counters)) {
                fail(failure, ResourceFailureCode::selection_mismatch, "native-selected member content mismatch");
                return false;
            }
            if (verified_read_only_mapping) location.state_->read_only_mapping_verified = true;
        }
    }
    if (container) return as_slice();
    blob = std::move(result);
    failure = {};
    return true;
    } catch (const std::bad_alloc&) {
        fail(failure, ResourceFailureCode::file_read,
            "selected file ownership allocation failed");
    } catch (const std::length_error&) {
        fail(failure, ResourceFailureCode::file_read,
            "selected file ownership allocation failed");
    }
    blob = {};
    return false;
}

ResourceStoreStats ResourceStore::stats() const noexcept {
    ResourceStoreStats output{};
    output.live_immutable_bytes = impl_->counters->live_immutable_bytes.load(std::memory_order_relaxed);
    output.immutable_byte_limit = impl_->counters->immutable_byte_limit.load(std::memory_order_relaxed);
    output.archive_owner_count = impl_->counters->archive_owner_count.load(std::memory_order_relaxed);
    output.read_calls = impl_->counters->read_calls.load(std::memory_order_relaxed);
    output.read_bytes = impl_->counters->read_bytes.load(std::memory_order_relaxed);
    output.resolved_loose = impl_->counters->resolved_loose.load(std::memory_order_relaxed);
    output.resolved_archive = impl_->counters->resolved_archive.load(std::memory_order_relaxed);
    output.cache_hits = impl_->counters->cache_hits.load(std::memory_order_relaxed);
#ifdef EID3D11_TESTING
    output.selected_comparison_calls = impl_->counters->selected_comparison_calls.load(std::memory_order_relaxed);
    output.selected_comparison_span_bytes = impl_->counters->selected_comparison_span_bytes.load(std::memory_order_relaxed);
#endif
    return output;
}

std::vector<ResourceContainerInfo> ResourceStore::containers() const {
    std::vector<ResourceContainerInfo> output;
    output.reserve(impl_->archives.size());
    for (const ArchiveMount& archive : impl_->archives)
        output.push_back(archive.container);
    std::lock_guard<std::mutex> guard(impl_->container_lock);
    output.insert(output.end(), impl_->loose_containers.begin(),
                  impl_->loose_containers.end());
    std::sort(output.begin(), output.end(), [](const auto& left, const auto& right) {
        if (left.mount_ordinal != right.mount_ordinal)
            return left.mount_ordinal < right.mount_ordinal;
        return left.label < right.label;
    });
    return output;
}

std::vector<std::string> ResourceStore::archive_entry_names() const {
    std::vector<std::string> names;
    for (const ArchiveMount& archive : impl_->archives)
        for (const ArchiveEntry& entry : archive.entries)
            if (entry.ascii) names.push_back(entry.folded_name);
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}

MemoryResourceArchive::MemoryResourceArchive() : impl_(std::make_unique<Impl>()) {}
MemoryResourceArchive::~MemoryResourceArchive() = default;
MemoryResourceArchive::MemoryResourceArchive(MemoryResourceArchive&&) noexcept = default;
MemoryResourceArchive& MemoryResourceArchive::operator=(MemoryResourceArchive&&) noexcept = default;

bool MemoryResourceArchive::open(const ResourceBlob& container,
                                 ResourceFailure& failure, const ResourceStore* budget) {
    impl_->container = {};
    std::vector<ArchiveEntry>{}.swap(impl_->entries);
    impl_->index_reservation.reset();
    if (!container.bytes && container.slice) {
        // Header, then the record table and the name pool after it (to the end).
        const auto& slice = *container.slice;
        std::vector<std::uint8_t> header, tail;
        std::uint64_t names_offset = 0, table_bytes = 0;
        if (slice.size < kResHeaderBytes || !slice.read(0, kResHeaderBytes, header, failure)) return false;
        const std::uint32_t count = read_u32(header.data() + 4), table_offset = read_u32(header.data() + 8);
        if (read_u32(header.data()) != kResSignature) {
            fail(failure, ResourceFailureCode::archive_signature, "RES signature or archive compression variant is unsupported");
            return false;
        }
        if (!checked_multiply(count, kResEntryBytes, table_bytes) ||
            !checked_add(table_offset, table_bytes, names_offset) || names_offset > slice.size) {
            fail(failure, ResourceFailureCode::archive_bounds, "RES table is outside the container");
            return false;
        }
        if (!slice.read(table_offset, slice.size - table_offset, tail, failure)) return false;
        const auto names_bytes = static_cast<std::uint32_t>((std::min)(slice.size - names_offset, std::uint64_t{UINT32_MAX}));
        if (!parse_archive_records(tail.data(), count, tail.data() + table_bytes, names_bytes, slice.size,
                                   impl_->entries, failure, budget, &impl_->index_reservation)) {
            std::vector<ArchiveEntry>{}.swap(impl_->entries);
            impl_->index_reservation.reset();
            return false;
        }
        impl_->container = container;
        failure = {};
        return true;
    }
    if (!container.bytes ||
        !parse_archive_bytes(container.bytes->data(), container.bytes->size(),
                             impl_->entries, failure, budget, &impl_->index_reservation)) {
        std::vector<ArchiveEntry>{}.swap(impl_->entries);
        impl_->index_reservation.reset();
        return false;
    }
    impl_->container = container;
    failure = {};
    return true;
}

bool MemoryResourceArchive::locate(const std::string& resource_name,
                                   ResourceIdentity& identity, ResourceFailure& failure) const {
    identity = {};
    std::string canonical;
    if (!canonical_resource_name(resource_name, canonical, failure)) return false;
    std::uint32_t index = 0;
    if (!find_archive_entry(impl_->entries, canonical, index)) {
        fail(failure, ResourceFailureCode::not_found,
             "nested resource identity was not present");
        return false;
    }
    const ArchiveEntry& entry = impl_->entries[index];
    identity = impl_->container.identity;
    identity.canonical_name += "/" + canonical;
    identity.absolute_offset += entry.offset;
    identity.bytes = entry.size;
    identity.entry_index = index;
    failure = {};
    return true;
}

bool MemoryResourceArchive::resolve(const std::string& resource_name,
                                    ResourceBlob& blob, ResourceFailure& failure,
                                    const ResourceStore* budget) const {
    blob = {};
    ResourceIdentity identity;
    if (!locate(resource_name,identity,failure)) return false;
    const auto& entry = impl_->entries[identity.entry_index];
    auto bytes = budget ? budget->allocate_immutable_bytes(entry.size,failure) :
        std::make_shared<std::vector<std::uint8_t>>();
    if (!bytes) return false;
    if (!impl_->container.bytes) {
        if (!impl_->container.slice->read(entry.offset, entry.size, *bytes, failure)) return false;
        blob.identity = std::move(identity);
        blob.bytes = std::move(bytes);
        failure = {};
        return true;
    }
    try {
        bytes->assign(impl_->container.bytes->begin() + entry.offset,
                      impl_->container.bytes->begin() + entry.offset + entry.size);
    } catch (...) {
        fail(failure, ResourceFailureCode::file_read,
             "nested resource allocation failed");
        return false;
    }
    blob.identity = std::move(identity);
    blob.bytes = std::move(bytes);
    failure = {};
    return true;
}

std::size_t MemoryResourceArchive::entry_count() const noexcept {
    return impl_->entries.size();
}

std::vector<std::string> MemoryResourceArchive::entry_names() const {
    std::vector<std::string> names;
    names.reserve(impl_->entries.size());
    for (const ArchiveEntry& entry : impl_->entries)
        if (entry.ascii) names.push_back(entry.folded_name);
    return names;
}

} // namespace owned
