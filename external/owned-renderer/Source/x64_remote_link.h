#pragma once
// Transport of the remote D3D11 device (x86 side): launches the x64 host,
// owns the shared control block and the command ring, and writes records in
// call order from any thread. See x64_remote_protocol.h.

#include <windows.h>
#include <cstdint>
#include <cstring>
#include <intrin.h>
#include <string>
#include "x64_remote_protocol.h"

namespace x64remote {

// EI_RENDERER_X64_REMOTE=1: the test override that forces the remote device.
bool remote_requested() noexcept;
// The host executable: EI_RENDERER_X64_HOST, else eid3d11_x64_host.exe next
// to this DLL (the shipped layout).
std::wstring host_path() noexcept;

// Launches the host and waits for its capability snapshot. On failure
// `error` names the reason; nothing falls back to a local device.
bool link_start(HWND game_window, UINT device_flags, char* error, std::size_t error_bytes, bool core = false) noexcept;
Control& control() noexcept;
// Largest payload one record may carry (a quarter of the ring).
std::uint32_t chunk_bytes() noexcept;
// Shared readback area (written by the host under a reply) and its size.
const std::uint8_t* readback_area() noexcept;
std::uint32_t readback_bytes() noexcept;
// Prefetch slots: kPrefetchSlots when none is free.
std::uint32_t allocate_prefetch_slot() noexcept;
void free_prefetch_slot(std::uint32_t slot) noexcept;
const PrefetchSlot& prefetch_slot(std::uint32_t slot) noexcept;
const std::uint8_t* prefetch_data(std::uint32_t slot) noexcept;
// True once the host died or reported device removal.
HRESULT link_removed() noexcept;
// True while the remote call-stream device is in use (the link started
// without core mode).
bool remote_active() noexcept;
// EI_RENDERER_X64_CORE=1: core mode wanted (typed scene records to the
// host's Renderer). Env-only until S6 brings the overlay back.
bool core_requested() noexcept;
// True while core mode runs: the link started for scene records and the
// game process has no D3D11 device.
bool core_active() noexcept;
// Core mode: one scene message (scene_core.h command id, scene_wire bytes)
// as Op::scene fragments, with no other scene record between them.
void scene_message(std::uint16_t command, const std::uint8_t* data, std::size_t bytes) noexcept;
// Picks up a host failure (exit, removal, failed creation) into the lost
// state; called once per Present.
void check_host() noexcept;

// Client cost on the calling thread (rdtsc ticks), reported per frame for
// the presenting thread: context calls (all), records written (lock and
// copy included) and dynamic Map/Unmap. Only with
// EI_RENDERER_X64_CLIENT_TIMING=1 (the timers cost ~20 ns per call).
extern bool g_client_timing;
struct ClientTicks {
    std::uint64_t calls = 0, call_ticks = 0, writer_ticks = 0, map_ticks = 0;
};
ClientTicks& client_ticks() noexcept;
class TimedCall {
public:
    TimedCall() noexcept : start_(g_client_timing ? __rdtsc() : 0) {}
    ~TimedCall() {
        if (!start_) return;
        auto& t = client_ticks();
        ++t.calls;
        t.call_ticks += __rdtsc() - start_;
    }
private:
    std::uint64_t start_;
};
// Map/Unmap time without the records they write (counted as writer time).
class TimedMap {
public:
    TimedMap() noexcept : start_(g_client_timing ? __rdtsc() : 0), writer_(start_ ? client_ticks().writer_ticks : 0) {}
    ~TimedMap() {
        if (!start_) return;
        auto& t = client_ticks();
        t.map_ticks += (__rdtsc() - start_) - (t.writer_ticks - writer_);
    }
private:
    std::uint64_t start_, writer_;
};

// One record: the constructor takes the ring lock and reserves the whole
// record, put() fills it in order, the destructor completes it. Never
// release a proxy (it may write a destroy record) while a Writer is alive.
class Writer {
public:
    Writer(Op op, std::uint16_t context, std::uint32_t payload_bytes) noexcept;
    ~Writer();
    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;
    void put(const void* data, std::uint32_t bytes) noexcept {
        std::memcpy(cursor_, data, bytes);
        cursor_ += bytes;
    }
    template <class T> void put(const T& value) noexcept { put(&value, sizeof(T)); }
    void u32(std::uint32_t value) noexcept { put(value); }
    // Space for `bytes` the caller fills directly.
    std::uint8_t* take(std::uint32_t bytes) noexcept {
        std::uint8_t* at = cursor_;
        cursor_ += bytes;
        return at;
    }
    // Ring position just past this record (deferred frees).
    std::int64_t end_position() const noexcept { return end_; }

private:
    std::uint8_t* cursor_ = nullptr;
    std::int64_t end_ = 0;
    std::uint64_t start_ticks_ = 0;
};

// Makes every written record visible to the host.
void publish() noexcept;
// Synchronous request: next_reply_serial() takes the reply lock and returns
// the serial to put in the request record; wait_reply() publishes and
// waits for the host's reply (its HRESULT); end_reply() releases the lock
// once the caller has read the readback area. One request in flight.
std::uint64_t next_reply_serial() noexcept;
HRESULT wait_reply(std::uint64_t serial) noexcept;
void end_reply() noexcept;
// Present bookkeeping: waits until at most frames_ahead presents are
// queued ahead of the host.
void wait_presents(std::uint64_t serial) noexcept;
// Ring position the host has consumed.
std::int64_t consumed_position() noexcept;

// Handles and query result slots, reused after their destroy record.
std::uint32_t allocate_handle() noexcept;
void free_handle(std::uint32_t handle) noexcept;
// kNoQuerySlot when the result storage could not grow (its mapping failed;
// the device is lost with that reason). Slots are demand-sized: see
// kQuerySegments in x64_remote_protocol.h.
std::uint32_t allocate_query_slot() noexcept;
void free_query_slot(std::uint32_t slot) noexcept;
// The result slot of an allocated slot index (any thread).
const QuerySlot& query_slot(std::uint32_t slot) noexcept;
std::uint64_t next_query_serial() noexcept;

// Statistics for the periodic report.
struct Counters {
    std::uint64_t records, bytes, map_bytes, update_bytes, init_bytes;
    std::uint64_t readbacks, prefetches, space_waits, present_waits;
    double space_wait_ms, present_wait_ms, readback_ms;
};
void count_map(std::uint32_t bytes) noexcept;
void count_update(std::uint32_t bytes) noexcept;
void count_init(std::uint32_t bytes) noexcept;
void count_readback(double ms) noexcept;
void count_prefetch() noexcept;
void count_shadow(std::int64_t bytes) noexcept;
void count_handles(int delta) noexcept;
// Once per present: writes the two-second report line.
void report_frame() noexcept;

// One line in x86_remote.log (next to the host exe) and the debugger.
void log(const char* format, ...) noexcept;
// A call the remote device does not support, or cannot execute exactly:
// logged once per name (x64_remote_unsupported event) and the device is
// lost with "unsupported call in remote mode: <what>", so the next Present
// fails and the game closes through the device-removed message naming it.
// Never a silent skip.
void unsupported(const char* what) noexcept;
// A named failure outside the D3D11 call stream: the device is lost with
// `reason` and the game closes at the next Present through the named box.
void lose_device(const char* reason) noexcept;
// A remote-only deviation whose result is provably identical to the local
// device (only slower): logged once per name, the game continues.
void remote_warning(const char* what) noexcept;

} // namespace x64remote
