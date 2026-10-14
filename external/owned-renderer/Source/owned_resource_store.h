#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace owned {

enum class ResourceFailureCode : std::uint8_t {
    none,
    invalid_name,
    unsupported_encoding,
    not_found,
    file_open,
    file_read,
    selection_mismatch,
    archive_header,
    archive_signature,
    archive_bounds,
    archive_chain,
    archive_name,
    unsupported_compression,
    cancelled,
    parse,
};

const char* resource_failure_code_name(ResourceFailureCode code) noexcept;

struct ResourceFailure {
    ResourceFailureCode code = ResourceFailureCode::none;
    std::string message;

    explicit operator bool() const noexcept {
        return code != ResourceFailureCode::none;
    }
};

struct ResourceContainerInfo {
    std::uint32_t mount_ordinal = 0;
    bool loose_file = false;
    std::uint64_t bytes = 0;
    std::string label;
    std::uint64_t incarnation = 0;
    std::uint64_t file_key = 0;
};

struct ResourceIdentity {
    std::string canonical_name;
    // Unique for an immutable open scope, including simultaneous stores.
    // This is an owned lifetime token, never a content hash or file pointer.
    std::uint64_t container_incarnation = 0;
    // Same physical file and version across opens (volume, file ID, size,
    // write/change times), or 0 for memory-only containers. Only the GPU
    // texture pool uses it, so maps that reopen one archive share uploads.
    std::uint64_t container_file_key = 0;
    std::uint64_t absolute_offset = 0;
    std::uint64_t bytes = 0;
    std::uint32_t mount_ordinal = 0;
    std::uint32_t entry_index = 0;
    bool loose_file = false;

    std::string key() const;
};

struct ResourceBlob {
    ResourceIdentity identity;
    std::shared_ptr<const std::vector<std::uint8_t>> bytes;
};

class ResourceLocation {
public:
    ResourceLocation() = default;

    const ResourceIdentity& identity() const noexcept { return identity_; }
    explicit operator bool() const noexcept { return state_ != nullptr; }

private:
    struct State;
    ResourceIdentity identity_;
    std::shared_ptr<const State> state_;

    friend class ResourceStore;
};

struct ResourceStoreConfig {
    // The shipped streaming path checks loose resources before its archive
    // lookup. Roots and archives are each searched in the exact order supplied;
    // callers supply the complete active mod priority plus base fallback.
    std::vector<std::wstring> loose_roots;
    std::vector<std::wstring> archives;
};

struct ResourceStoreStats {
    std::uint64_t live_immutable_bytes = 0;
    std::uint64_t immutable_byte_limit = UINT64_MAX;
    std::uint64_t archive_owner_count = 0;
    std::uint64_t mapped_view_count = 0;
    std::uint64_t read_calls = 0;
    std::uint64_t read_bytes = 0;
    std::uint64_t resolved_loose = 0;
    std::uint64_t resolved_archive = 0;
    std::uint64_t cache_hits = 0;
#ifdef EID3D11_TESTING
    std::uint64_t selected_comparison_calls = 0;
    // Requested memcmp span; a mismatch can stop before reading every byte.
    std::uint64_t selected_comparison_span_bytes = 0;
#endif
};

// Optional, per-selection diagnostics for the existing outer-frame recorder.
// The returned values describe this call, not deltas from shared store counters.
struct SelectedResourceReadDiagnostic {
    std::int64_t provenance_ticks = 0;
    std::int64_t resolve_ticks = 0;
    std::int64_t file_read_ticks = 0;
    std::int64_t comparison_ticks = 0;
    std::uint64_t file_read_calls = 0;
    std::uint64_t file_read_bytes = 0;
    std::uint64_t comparison_span_bytes = 0;
    bool backing_cache_hit = false;
};

class ResourceStore {
public:
    explicit ResourceStore(std::uint64_t immutable_byte_limit = UINT64_MAX);
    ~ResourceStore();
    ResourceStore(ResourceStore&&) noexcept;
    ResourceStore& operator=(ResourceStore&&) noexcept;
    ResourceStore(const ResourceStore&) = delete;
    ResourceStore& operator=(const ResourceStore&) = delete;

    bool open(const ResourceStoreConfig& config, ResourceFailure& failure);
    void close() noexcept;
    // The backing reservation follows the last immutable shared owner, even
    // after a source key is replaced or this store closes its file scopes.
    std::shared_ptr<std::vector<std::uint8_t>> allocate_immutable_bytes(
        std::size_t bytes, ResourceFailure& failure, std::size_t retained_extra = 0) const;
    // Snapshot a completed native-selected member. Its new immutable memory
    // source owns range [0,size); this identity makes no filesystem claim.
    bool copy_selected_memory(const std::string& resource_name,
                              const void* selected_bytes, std::uint32_t size,
                              ResourceBlob& blob, ResourceFailure& failure) const;
#ifdef EID3D11_TESTING
    void set_immutable_byte_limit_for_test(std::uint64_t bytes) noexcept;
#endif

    bool locate(const std::string& resource_name, ResourceLocation& location,
                ResourceFailure& failure) const;
    bool read(const ResourceLocation& location, ResourceBlob& blob,
              ResourceFailure& failure, bool* backing_cache_hit = nullptr,
              SelectedResourceReadDiagnostic* diagnostic = nullptr) const;
    bool resolve(const std::string& resource_name, ResourceBlob& blob,
                 ResourceFailure& failure) const;

    // A native resolver has already selected this physical file/member. This
    // synchronous bridge boundary verifies the returned bytes and actual
    // absolute file offset against that file's normal index. The selected file
    // stays read-only for this resolver epoch; borrowed memory is never retained.
    // Only a complete original read-only mapped-span proof permits reusing the
    // first byte comparison; a weaker view still compares on every selection.
    // It does not apply the store's configured mount priority.
    bool resolve_selected_file(const std::wstring& physical_file,
                               const std::string& resource_name,
                               const void* selected_bytes,
                               std::uint64_t selected_size,
                               std::uint64_t selected_file_offset,
                               ResourceBlob& blob, ResourceFailure& failure,
                               SelectedResourceReadDiagnostic* diagnostic = nullptr,
                               bool verified_read_only_mapping = false);
    // Only the supported native ANM getter, whose validated mapper creates
    // whole-file read views. Derives fresh mapping provenance from the borrowed
    // span; an immutable member proof belongs to the held file incarnation.
    bool resolve_native_mapped_anm(const std::string& resource_name,
                                   const void* selected_bytes, std::uint64_t selected_size,
                                   ResourceBlob& blob, ResourceFailure& failure);
    // One completed generated file, selected by the native writer's path.
    // Its handle denies concurrent writes and is released with this read; it
    // must not enter the process-lifetime archive selection cache.
    bool read_completed_loose_file(const std::wstring& physical_file,
                                  const std::string& resource_name,
                                  ResourceBlob& blob, ResourceFailure& failure) const;

    // Explicit completed non-texture assets; format validation belongs to the decoder.
    bool read_loose_file(const std::wstring& physical_file,
                         const std::string& resource_name,
                         ResourceBlob& blob, ResourceFailure& failure) const;

    ResourceStoreStats stats() const noexcept;
    std::vector<ResourceContainerInfo> containers() const;
    // Lower-case ASCII member names of every mounted archive (sorted, unique).
    // A listed name may still be unreachable through its bucket chain; resolve
    // decides.
    std::vector<std::string> archive_entry_names() const;

private:
    bool resolve_selected_file_impl(const std::wstring& physical_file,
        const std::string& canonical_name, const void* selected_bytes,
        std::uint64_t selected_size, std::uint64_t selected_file_offset,
        bool verified_read_only_mapping, ResourceBlob& blob, ResourceFailure& failure,
        SelectedResourceReadDiagnostic* diagnostic = nullptr);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// A nested RES member is indexed over immutable owned bytes. It never creates
// another file handle or mapped view; its identity remains rooted in the outer
// container incarnation and byte range.
class MemoryResourceArchive {
public:
    MemoryResourceArchive();
    ~MemoryResourceArchive();
    MemoryResourceArchive(MemoryResourceArchive&&) noexcept;
    MemoryResourceArchive& operator=(MemoryResourceArchive&&) noexcept;
    MemoryResourceArchive(const MemoryResourceArchive&) = delete;
    MemoryResourceArchive& operator=(const MemoryResourceArchive&) = delete;

    bool open(const ResourceBlob& container, ResourceFailure& failure,
              const ResourceStore* budget = nullptr);
    bool locate(const std::string& resource_name, ResourceIdentity& identity,
                ResourceFailure& failure) const;
    bool resolve(const std::string& resource_name, ResourceBlob& blob,
                 ResourceFailure& failure, const ResourceStore* budget = nullptr) const;
    std::size_t entry_count() const noexcept;
    std::vector<std::string> entry_names() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace owned
