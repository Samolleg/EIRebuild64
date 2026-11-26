#pragma once
// Live D3D12 memory of the host by kind and by creation site (issue #289,
// where zone3obr's ~9 GB goes). Every committed resource, heap and
// descriptor heap the backend creates is tagged once at creation with its
// allocation size and the call stack that created it; a private-data object
// the runtime releases together with the object takes the bytes off again,
// so no release path needs to know about it. memory_status prints the
// totals every 30 s (Renderer::log_memory_status).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>

#include <cstdint>
#include <string>
#include <vector>

namespace d3d12 {

enum class MemoryKind : unsigned char {
    texture, texture_array, storage_texture, target, depth, buffer, rt_structure, rt_scratch,
    texture_heap, upload, readback, descriptors, swap_chain, other, count
};

// Tags a freshly created object (a resource, ID3D12Heap or descriptor heap).
// A resource on an upload or readback heap counts as upload/readback
// (system memory on a discrete adapter) whatever kind is passed.
void track_memory(ID3D12Device* device, ID3D12Object* object, MemoryKind kind) noexcept;

// Report lines: the kind totals against the process's local use, then the
// largest creation sites (live bytes, peak, count, kind, host-exe RVAs of
// the creating stack; resolve with build\x64-host\eid3d11_x64_host.map).
std::vector<std::string> memory_report(std::uint64_t process_local_bytes);

// The host's resident memory by owner (modules, write-combined, mapped,
// allocation sites of 64 KiB or more, other private) and the largest sites.
std::vector<std::string> host_memory_report();

} // namespace d3d12
