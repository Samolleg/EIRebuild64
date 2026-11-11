#pragma once
// NVIDIA Reflex low latency on the host's D3D12 device (option reflex; NVAPI,
// MIT, static nvapi64.lib) and the game-frame latency measurement.
//
// Two processes: the game simulates, the host renders. The game thread waits
// after each present until the host published presents_done, so the host's
// release is the start of the game's next simulation:
// - mode 1: low latency mode (no frame interval: never a cap) and latency
//   markers; the driver sleeps at its own spot.
// - mode 2: also NvAPI_D3D_Sleep once per game present, right before the host
//   publishes presents_done (the cross-process form of sleeping before input
//   sampling; Remix sleeps inside the app's Present the same way).
// Markers are host-side times (NVAPI stamps the call): SIMULATION_START when
// the host releases the game frame, SIMULATION_END when the host takes that
// frame's present record, RENDERSUBMIT around the host's frame, PRESENT
// around the swap-chain Present, or the rendered-frame handover to the
// DLSS-G pacer. Rendering and final presentation share the normal direct
// queue. Each id's markers arrive in NVAPI's order before the next id's.
// Measurement (every mode, also FG off): the game's frame start (its QPC when
// the previous present wait returned, sent in the present record) to the
// real image's Present returning on the host, median and p95 every 5 s in
// host.log; with Reflex on, NVAPI's own simulation-start-to-present report.
#include <cstdint>

struct ID3D12Device;

namespace reflex {

enum class Marker { simulation_start, simulation_end, render_start, render_end, present_start, present_end };

// Replay thread, once per game present: the option's mode (0 off, 1 markers
// and low latency mode, 2 also Sleep). Initialises NVAPI on first use; a
// device without it stays off with one log line.
void configure(ID3D12Device* device, int mode) noexcept;
int mode() noexcept;
#ifdef EID3D11_EMBEDDED
// After joining presentation callers, before releasing the executor device.
void retire_device(ID3D12Device* device) noexcept;
#endif
// The game present being rendered (replay thread) and its game frame start.
void set_frame(std::uint64_t id, std::int64_t game_start_qpc) noexcept;
std::uint64_t frame() noexcept;
std::int64_t frame_start() noexcept;
std::int64_t frame_taken() noexcept;  // QPC when set_frame ran (the host took the record)
void marker(Marker type, std::uint64_t frame) noexcept;
void sleep() noexcept;
// A real image's Present returned (any thread): one latency sample.
void real_presented(std::int64_t game_start_qpc) noexcept;
// latency_pacing (core_host.cpp), once per game present: the release delay
// the game is told, the ring wait (host takes the present record minus
// the game's present call), the queue (host free minus present call), the
// host frame interval and the unpaced one; means go into the 5 s line.
void pacing_sample(bool on, double delay_ms, double ring_ms, double queue_ms, double interval_ms, double unpaced_ms) noexcept;

} // namespace reflex
