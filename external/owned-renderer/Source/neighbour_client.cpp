#if defined(EID3D11_EMBEDDED) || defined(EID3D11_SHADER_TOOL)
#include "neighbour_client.h"
#include "neighbour_maps.h"
#include "neighbour_piece.h"
#include "owned_resource_runtime.h"
namespace owned {
void neighbour_client_begin(const std::string& mpr, std::uint64_t epoch, const ResourceStoreConfig& mounts) noexcept {
        NeighbourStreamSource source;
        try {source.layout_file = neighbour_layout_file();} catch (...) {} // the stream reports the missing addon
        source.active_terrain = [epoch]() -> std::shared_ptr<const TerrainCpuAsset> {
            const auto published = retained_static_terrain_source_snapshot();
            return published.map && published.terrain && published.map->generation == epoch ? published.terrain : nullptr;
        };
        neighbour_maps_begin(mpr, epoch, mounts, std::move(source));
}
std::unique_ptr<NeighbourPiece> neighbour_client_take(std::uint64_t epoch) noexcept { return neighbour_maps_take(epoch); }
void neighbour_client_uploaded(std::uint64_t epoch) noexcept { neighbour_maps_uploaded(epoch); }
void neighbour_client_stop() noexcept { neighbour_maps_stop(); }
void neighbour_client_stop_epoch(std::uint64_t epoch) noexcept { neighbour_maps_stop_epoch(epoch); }
bool neighbour_client_settled(std::uint64_t epoch) noexcept { return neighbour_maps_settled(epoch); }
bool neighbour_client_remaining(std::uint64_t epoch, std::size_t& remaining) noexcept { return neighbour_maps_remaining(epoch,remaining); }
std::string neighbour_client_error(std::uint64_t epoch) { return neighbour_maps_error(epoch); }
}
#else
// The renderer DLL's neighbour stream (neighbour_client.h).
#include "neighbour_client.h"

#include "neighbour_maps.h"
#include "neighbour_piece.h"
#include "owned_resource_runtime.h"
#include "neighbour_remote.h"
#include "owned_resource_runtime.h"
#include "x64_remote_link.h"
#include "scene_core_types.h"

#include <atomic>
#include <mutex>

namespace owned {

namespace {
// Core mode: the epoch the host stream serves (0: none), as the local
// stream tracks its own.
std::atomic<std::uint64_t> g_remote_epoch{0};
// Begin and stop come from the game thread (map load) and the render thread;
// one at a time, so a begin never interleaves with another stop.
std::mutex g_remote_lock;
// Core mode: the host runs the stream for its own Renderer; this process
// only sends the inputs it would read (neighbour_client_begin) and the stops.
bool core() noexcept { return x64remote::core_active(); }
template <class C> void core_send(scene_core::Command id, const C& command) noexcept try {
    std::vector<std::uint8_t> bytes{0};
    scene_wire::encode_into(bytes, command);
    x64remote::scene_message(static_cast<std::uint16_t>(id), bytes.data(), bytes.size());
} catch (...) {
    x64remote::lose_device("core mode: a neighbour stream message could not be built (allocation)");
}
} // namespace

void neighbour_client_begin(const std::string& mpr, std::uint64_t epoch, const ResourceStoreConfig& mounts) noexcept {
    if (core()) {
        std::lock_guard<std::mutex> guard(g_remote_lock);
        if (g_remote_epoch.load() == epoch || !neighbour_maps_enabled()) return;
        try {
            NeighbourRemoteBegin begin;
            begin.mpr = mpr;
            try {begin.layout_file = neighbour_layout_file();} catch (...) {} // the host stream reports it
            begin.no_sea_maps = neighbour_maps_no_sea_maps();
            begin.mounts = mounts;
            scene_core::NeighbourBegin command;
            command.epoch = epoch;
            command.inputs = encode_neighbour_begin(begin);
            core_send(scene_core::Command::neighbour_begin, command);
            g_remote_epoch.store(epoch);
        } catch (...) {
            x64remote::lose_device("core mode: neighbour stream begin allocation failed");
        }
        return;
    }
    // This process's stream: the layout next to its addon.dll and the
    // active terrain the retained scene published for this generation.
    NeighbourStreamSource source;
    try {source.layout_file = neighbour_layout_file();} catch (...) {} // the stream reports the missing addon
    source.active_terrain = [epoch]() -> std::shared_ptr<const TerrainCpuAsset> {
        const auto published = retained_static_terrain_source_snapshot();
        return published.map && published.terrain && published.map->generation == epoch ? published.terrain : nullptr;
    };
    neighbour_maps_begin(mpr, epoch, mounts, std::move(source));
}

std::unique_ptr<NeighbourPiece> neighbour_client_take(std::uint64_t epoch) noexcept {
    if (core()) return nullptr;  // the host Renderer adopts the pieces
    return neighbour_maps_take(epoch);
}

void neighbour_client_uploaded(std::uint64_t epoch) noexcept {
    if (core()) return;
    neighbour_maps_uploaded(epoch);
}

void neighbour_client_stop() noexcept {
    if (core()) {
        std::lock_guard<std::mutex> guard(g_remote_lock);
        if (!g_remote_epoch.exchange(0)) return;
        core_send(scene_core::Command::neighbour_stop, scene_core::NeighbourStop{});
        return;
    }
    neighbour_maps_stop();
}

void neighbour_client_stop_epoch(std::uint64_t epoch) noexcept {
    if (core()) { if (epoch && g_remote_epoch.load() == epoch) neighbour_client_stop(); return; }
    neighbour_maps_stop_epoch(epoch);
}

bool neighbour_client_settled(std::uint64_t epoch) noexcept {
    if (core()) return true;  // the host Renderer's load reveal waits for its own stream
    return neighbour_maps_settled(epoch);
}

bool neighbour_client_remaining(std::uint64_t epoch, std::size_t& remaining) noexcept {
    if (core()) { remaining = 0; return true; }
    return neighbour_maps_remaining(epoch, remaining);
}

std::string neighbour_client_error(std::uint64_t epoch) {
    if (core()) return {};
    return neighbour_maps_error(epoch);
}

} // namespace owned

#endif
