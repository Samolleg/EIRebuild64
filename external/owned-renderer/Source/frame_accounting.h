#pragma once
#include "runtime_diagnostics.h"
// Per-present frame accounting (harness runs only: EI_RENDERER_HARNESS_DIR set,
// EI_RENDERER_FRAME_ACCOUNTING=0 turns it off). Written into the camera_path
// row; see RENDERER_ARCHITECTURE.md "Frame accounting".
//
// Each frame thread (the game thread, and the scene thread when render_thread
// is on) keeps an exclusive-time stack. Every QPC tick between two frame
// boundaries is charged to exactly one part: the innermost open scope, or the
// thread's current root part (set by the game's loop hooks). The parts of one
// frame therefore sum to that thread's wall interval; the residual is the time
// spent in the generic root (game_other / scene_other), i.e. outside every
// named segment.
//
// D3D11 creation calls (device) and Map/GetData (immediate context) are
// counted through a vtable patch of our own device and context (installed
// only while accounting is on, restored at release).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d11.h>
#include <intrin.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>

namespace frame_acct {

enum Part : std::uint8_t {
    // Game thread roots (set by the game-loop hooks).
    game_other,        // before any loop mark / outside every named segment (residual)
    game_sim,          // CountTime body from its clock read: pre/fixed/post ticks (simulation, AI)
    game_loop,         // the main loop between OnIdle/EndFrame and the next segment
    game_draw,         // DrawFrame body from BeginFrame to EndFrame outside the world draw (UI)
    // Game thread scopes (game code).
    game_ui_idle,      // InterfaceOnIdle
    game_msg,          // window messages through the game's WndProc
    game_world,        // the game's own world draw traversal (our hooks inside excluded)
    game_end_frame,    // EndFrame, game part
    // Our code on the game thread.
    ours_frame_begin,  // the begin-frame sink (finish queued output, restore targets)
    ours_world_setup,  // hook_world_draw's own setup and completion
    ours_world_scene,  // retained seam: render_thread=1 capture+copy+post, =0 full prepare
    ours_terrain,      // owned terrain range hook (front end, visibility, binds)
    ours_objects,      // owned object interaction (extents, flags)
    ours_native_geom,  // the native geometry sink (legacy figure packets)
    ours_renderer,     // public Renderer entries on the game thread (SCENE_ENTRY, renderer.h)
    ours_surface_upload, // surface upload recording / flush on the game thread
    wait_scene_sync,   // game thread waits for the scene thread to drain
    wait_present_slot, // game thread waits for the previous frame's present
    present_entry,     // Renderer::present in scene mode (copy, queue)
    // present_frame, on whichever thread presents.
    pf_other,          // present_frame outside the named steps below
    pf_flush_native,
    pf_composite,
    pf_ui,
    pf_overlay,        // debug overlay / telemetry
    pf_harness,        // harness step and its row output
    pf_limiter,        // frame limiter sleep (0 in harness runs)
    pf_present_call,   // IDXGISwapChain::Present (includes the DXGI queue wait)
    pf_trim,           // mesh cache trim
    pf_shader_prep,    // prepare_shaders + specialisation pump
    // Scene thread.
    scene_other,       // scene loop outside commands (residual)
    scene_idle,        // waiting for the game thread to queue work
    scene_command,     // running a queued command not otherwise named
    scene_world,       // run_owned_visual_scene (shadows, culling, LOD, binding, recording)
    // Retained-world owners (CityRetainedOwnerGuard), on whichever thread
    // runs them; nested inside scene_world / ours_world_scene.
    ret_receiver_fitting, ret_caster_values, ret_depth_registration, ret_nonunit_pose,
    ret_visual_frame_values, ret_shadow_input_decision, ret_domain_snapshot,
    ret_color_submission, ret_point_shadow, ret_terrain_base, ret_lighting_publish,
    // Our hooks that wrap a game original (the original itself is charged
    // to the part that was current when the hook was entered).
    ours_object_draw,  // object draw hook outside the game's original and the interaction
    ours_figure_hooks, // placed/lit figure hooks outside the game's original
    // Mesh creation on first use (nested wherever it happens).
    mesh_build,        // get_native_mesh miss: topology build and allocation
    mesh_upload,       // geometry page UpdateSubresource calls
    terrain_record,    // terrain sector view decode (source_terrain_world_record)
    geometry_growth,   // geometry page growth (new buffer plus copy of the live arena)
    point_pool_growth, // point-shadow atlas growth (new array plus slice copies)
    // Renderer::flush_native_static (opaque world after terrain), split so
    // none of it stays unattributed (#164). ours_static_flush is its own
    // remainder (pending flush, clip, state guards).
    ours_static_flush,
    ours_static_hiz,   // occlusion pyramid build
    ours_ground_cover, // render_ground_cover outside the two parts below
    ours_gc_units,     // unit, footprint and corpse reads for cover bending/tracks
    ours_gc_objects,   // object contact store: journal update, window query, builds
    ours_critters,     // render_critters
    ours_static_set,   // flush_native_static_set: expiry scan, set rebuild, enqueue
    ours_static_submit, // that set's flush_native (ordering, layout, uploads, draws)
    count,
    none = 0xff
};

inline const char* part_name(unsigned part) noexcept {
    static const char* const names[count] = {
        "game_other", "game_sim", "game_loop", "game_draw", "game_ui_idle", "game_msg",
        "game_world", "game_end_frame", "ours_frame_begin", "ours_world_setup",
        "ours_world_scene", "ours_terrain", "ours_objects", "ours_native_geom",
        "ours_renderer", "ours_surface_upload", "wait_scene_sync", "wait_present_slot",
        "present_entry", "pf_other", "pf_flush_native", "pf_composite", "pf_ui",
        "pf_overlay", "pf_harness", "pf_limiter", "pf_present_call", "pf_trim",
        "pf_shader_prep", "scene_other", "scene_idle", "scene_command", "scene_world",
        "ret_receiver_fitting", "ret_caster_values", "ret_depth_registration", "ret_nonunit_pose",
        "ret_visual_frame_values", "ret_shadow_input_decision", "ret_domain_snapshot",
        "ret_color_submission", "ret_point_shadow", "ret_terrain_base", "ret_lighting_publish",
        "ours_object_draw", "ours_figure_hooks", "mesh_build", "mesh_upload", "terrain_record",
        "geometry_growth", "point_pool_growth", "ours_static_flush", "ours_static_hiz",
        "ours_ground_cover", "ours_gc_units", "ours_gc_objects", "ours_critters",
        "ours_static_set", "ours_static_submit"};
    return part < count ? names[part] : "?";
}

inline bool enabled() noexcept {
#ifdef EID3D11_EMBEDDED
    if (!RuntimeDiagnostics::Enabled()) return false;
#endif
    static const bool on = [] {
        char value[8]{};
        if (GetEnvironmentVariableA("EI_RENDERER_FRAME_ACCOUNTING", value, sizeof(value)) && value[0] == '0')
            return false;
        char dir[8]{};
        return GetEnvironmentVariableA("EI_RENDERER_HARNESS_DIR", dir, sizeof(dir)) > 0;
    }();
    return on;
}

inline std::int64_t now() noexcept {
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return value.QuadPart;
}
inline double ms_per_tick() noexcept {
    static const double scale = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return 1000.0 / static_cast<double>(f.QuadPart);
    }();
    return scale;
}

// One closed frame of one thread.
struct Record {
    bool valid = false;
    std::uint64_t serial = 0;
    std::int64_t wall = 0;                       // QPC ticks, boundary to boundary
    std::array<std::int64_t, count> ticks{};     // sums to wall
    double oncpu_ms = -1.0;                      // thread cycles in the interval (TSC estimate)
    unsigned overflow = 0;                       // scopes dropped (stack full)
};

struct ThreadState {
    std::array<std::uint8_t, 32> stack{};        // [0] is the root part
    unsigned depth = 0;                          // index of the innermost part
    std::int64_t since = 0, begin = 0;
    std::uint64_t cycles = 0, tsc = 0;
    std::uint64_t serial = 0;
    unsigned overflow = 0;
    std::array<std::int64_t, count> acc{};
};
inline thread_local ThreadState t_state{};

inline void charge(ThreadState& s, std::int64_t at) noexcept {
    if (s.since) s.acc[s.stack[s.depth]] += at - s.since;
    s.since = at;
}

inline bool push(Part part) noexcept {
    if (part == none || !enabled()) return false;
    ThreadState& s = t_state;
    if (s.depth + 1 >= s.stack.size()) { ++s.overflow; return false; }
    charge(s, now());
    s.stack[++s.depth] = part;
    return true;
}
inline void pop() noexcept {
    ThreadState& s = t_state;
    if (!s.depth) return;
    charge(s, now());
    --s.depth;
}
inline void set_root(Part part) noexcept {
    if (!enabled()) return;
    ThreadState& s = t_state;
    charge(s, now());
    s.stack[0] = part;
}

// The part being charged on this thread now (for re-entering a game
// original from inside one of our hooks).
inline Part current() noexcept {
    if (!enabled()) return none;
    const ThreadState& s = t_state;
    return static_cast<Part>(s.stack[s.depth]);
}

// A Renderer entry on the game thread: charged as ours_renderer unless it
// runs inside present_frame (already named) or another entry.
inline Part entry_part() noexcept {
    if (!enabled()) return none;
    const ThreadState& s = t_state;
    const std::uint8_t top = s.stack[s.depth];
    return (top >= pf_other && top <= pf_shader_prep) || top == ours_renderer ? none : ours_renderer;
}

struct Scope {
    bool active;
    explicit Scope(Part part) noexcept : active(push(part)) {}
    ~Scope() { if (active) pop(); }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};

// TSC rate for converting thread cycle counts (calibrated over the run).
struct TscCalibration { std::atomic<std::uint64_t> tsc0{0}; std::atomic<std::int64_t> qpc0{0}; };
inline TscCalibration g_tsc{};
inline double tsc_per_ms(std::uint64_t tsc, std::int64_t qpc) noexcept {
    std::uint64_t t0 = g_tsc.tsc0.load(std::memory_order_relaxed);
    if (!t0) {
        g_tsc.qpc0.store(qpc, std::memory_order_relaxed);
        g_tsc.tsc0.store(tsc, std::memory_order_relaxed);
        return 0.0;
    }
    const std::int64_t q0 = g_tsc.qpc0.load(std::memory_order_relaxed);
    const double elapsed = static_cast<double>(qpc - q0) * ms_per_tick();
    return elapsed > 100.0 ? static_cast<double>(tsc - t0) / elapsed : 0.0;
}

// Ends this thread's frame at this instant; the open scopes stay open and
// keep charging into the next frame.
inline Record close_frame() noexcept {
    Record record{};
    if (!enabled()) return record;
    ThreadState& s = t_state;
    const std::int64_t at = now();
    const std::uint64_t tsc = __rdtsc();
    ULONG64 cycles = 0;
    QueryThreadCycleTime(GetCurrentThread(), &cycles);
    charge(s, at);
    if (s.begin) {
        record.valid = true;
        record.serial = ++s.serial;
        record.wall = at - s.begin;
        record.ticks = s.acc;
        record.overflow = s.overflow;
        const double rate = tsc_per_ms(tsc, at);
        if (rate > 0.0 && s.cycles) record.oncpu_ms = static_cast<double>(cycles - s.cycles) / rate;
    } else {
        (void)tsc_per_ms(tsc, at);
    }
    s.acc = {};
    s.overflow = 0;
    s.begin = at;
    s.cycles = cycles;
    s.tsc = tsc;
    return record;
}

// ---- D3D11 call counters ---------------------------------------------------
enum Counter : unsigned {
    buffers, buffer_bytes, buffer_ticks,
    textures, texture_bytes, texture_ticks,
    views, view_ticks,
    shaders, shader_ticks,
    states, state_ticks,
    maps, map_ticks,
    get_data, get_data_ticks,
    mesh_uploads, mesh_upload_bytes,
    mesh_world_terrain, mesh_world_figure, mesh_world_depth_only, mesh_world_prebuilt,
    mesh_terrain_lod0, mesh_terrain_lod1, mesh_terrain_lod2, mesh_terrain_lod3, mesh_terrain_stitched,
    sun_dirty_slices, sun_slice0_dirty, sun_slice3_dirty, sun_slice4_dirty, sun_depth_parts,
    map_stalls, map_stall_ticks,
    map_discards,
    counter_count
};
inline const char* counter_name(unsigned counter) noexcept {
    static const char* const names[counter_count] = {
        "buffers", "buffer_bytes", "buffer_ms", "textures", "texture_bytes", "texture_ms",
        "views", "view_ms", "shaders", "shader_ms", "states", "state_ms", "maps", "map_ms",
        "get_data", "get_data_ms", "mesh_uploads", "mesh_upload_bytes",
        "mesh_world_terrain", "mesh_world_figure", "mesh_world_depth_only", "mesh_world_prebuilt",
        "mesh_terrain_lod0", "mesh_terrain_lod1", "mesh_terrain_lod2", "mesh_terrain_lod3", "mesh_terrain_stitched",
        "sun_dirty_slices", "sun_slice0_dirty", "sun_slice3_dirty", "sun_slice4_dirty", "sun_depth_parts",
        "map_stalls", "map_stall_ms", "map_discards"};
    return counter < counter_count ? names[counter] : "?";
}
inline bool counter_is_ticks(unsigned counter) noexcept {
    switch (counter) {
    case buffer_ticks: case texture_ticks: case view_ticks: case shader_ticks: case state_ticks:
    case map_ticks: case get_data_ticks: case map_stall_ticks: return true;
    default: return false;
    }
}
inline std::array<std::atomic<std::int64_t>, counter_count> g_counters{};
inline void add(Counter counter, std::int64_t value) noexcept {
    g_counters[counter].fetch_add(value, std::memory_order_relaxed);
}
// A world-keyed mesh created on first use: what it is and who asked first.
// For terrain, part carries (material << 8) | (subpart << 4) | variant with
// variant = lod | lu_higher << 2 | rd_higher << 3.
inline void count_world_mesh(bool terrain, bool depth_only, bool prebuilt, std::uint32_t part) noexcept {
    if (!enabled()) return;
    add(terrain ? mesh_world_terrain : mesh_world_figure, 1);
    if (depth_only) add(mesh_world_depth_only, 1);
    if (prebuilt) add(mesh_world_prebuilt, 1);
    if (terrain) {
        add(static_cast<Counter>(mesh_terrain_lod0 + (part & 3u)), 1);
        if (part & 12u) add(mesh_terrain_stitched, 1);
    }
}
inline std::array<std::int64_t, counter_count> counters_snapshot() noexcept {
    std::array<std::int64_t, counter_count> out{};
    for (unsigned i = 0; i < counter_count; ++i) out[i] = g_counters[i].load(std::memory_order_relaxed);
    return out;
}

inline double format_bytes_per_texel(DXGI_FORMAT format) noexcept {
    switch (format) {
    case DXGI_FORMAT_BC1_TYPELESS: case DXGI_FORMAT_BC1_UNORM: case DXGI_FORMAT_BC1_UNORM_SRGB:
    case DXGI_FORMAT_BC4_TYPELESS: case DXGI_FORMAT_BC4_UNORM: case DXGI_FORMAT_BC4_SNORM: return 0.5;
    case DXGI_FORMAT_BC2_TYPELESS: case DXGI_FORMAT_BC2_UNORM: case DXGI_FORMAT_BC2_UNORM_SRGB:
    case DXGI_FORMAT_BC3_TYPELESS: case DXGI_FORMAT_BC3_UNORM: case DXGI_FORMAT_BC3_UNORM_SRGB:
    case DXGI_FORMAT_BC5_TYPELESS: case DXGI_FORMAT_BC5_UNORM: case DXGI_FORMAT_BC5_SNORM:
    case DXGI_FORMAT_BC6H_TYPELESS: case DXGI_FORMAT_BC6H_UF16: case DXGI_FORMAT_BC6H_SF16:
    case DXGI_FORMAT_BC7_TYPELESS: case DXGI_FORMAT_BC7_UNORM: case DXGI_FORMAT_BC7_UNORM_SRGB:
    case DXGI_FORMAT_R8_UNORM: case DXGI_FORMAT_R8_UINT: case DXGI_FORMAT_R8_TYPELESS: case DXGI_FORMAT_A8_UNORM: return 1.0;
    case DXGI_FORMAT_R16_FLOAT: case DXGI_FORMAT_R16_UNORM: case DXGI_FORMAT_R16_TYPELESS: case DXGI_FORMAT_D16_UNORM:
    case DXGI_FORMAT_R8G8_UNORM: case DXGI_FORMAT_R16_UINT: case DXGI_FORMAT_B5G6R5_UNORM: case DXGI_FORMAT_B5G5R5A1_UNORM: return 2.0;
    case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R16G16B16A16_UNORM: case DXGI_FORMAT_R16G16B16A16_TYPELESS:
    case DXGI_FORMAT_R32G32_FLOAT: case DXGI_FORMAT_R32G32_TYPELESS: case DXGI_FORMAT_R32G8X24_TYPELESS: return 8.0;
    case DXGI_FORMAT_R32G32B32A32_FLOAT: case DXGI_FORMAT_R32G32B32A32_TYPELESS: case DXGI_FORMAT_R32G32B32A32_UINT: return 16.0;
    default: return 4.0;
    }
}
inline bool format_is_block(DXGI_FORMAT format) noexcept {
    return format >= DXGI_FORMAT_BC1_TYPELESS && format <= DXGI_FORMAT_BC5_SNORM ||
        format >= DXGI_FORMAT_BC6H_TYPELESS && format <= DXGI_FORMAT_BC7_UNORM_SRGB;
}

namespace vt {
using CreateBufferFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, const D3D11_BUFFER_DESC*, const D3D11_SUBRESOURCE_DATA*, ID3D11Buffer**);
using CreateTexture2DFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, const D3D11_TEXTURE2D_DESC*, const D3D11_SUBRESOURCE_DATA*, ID3D11Texture2D**);
using CreateTexture3DFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, const D3D11_TEXTURE3D_DESC*, const D3D11_SUBRESOURCE_DATA*, ID3D11Texture3D**);
using CreateViewFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, ID3D11Resource*, const void*, void**);
using CreateInputLayoutFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, const D3D11_INPUT_ELEMENT_DESC*, UINT, const void*, SIZE_T, ID3D11InputLayout**);
using CreateShaderFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*, void**);
using CreateStateFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, const void*, void**);
using MapFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, D3D11_MAP, UINT, D3D11_MAPPED_SUBRESOURCE*);
using GetDataFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Asynchronous*, void*, UINT, UINT);

// Vtable slots (d3d11.h ID3D11DeviceVtbl / ID3D11DeviceContextVtbl order).
enum DeviceSlot : unsigned { d_buffer = 3, d_tex2d = 5, d_tex3d = 6, d_srv = 7, d_uav = 8, d_rtv = 9, d_dsv = 10,
    d_layout = 11, d_vs = 12, d_gs = 13, d_ps = 15, d_hs = 16, d_ds = 17, d_cs = 18,
    d_blend = 20, d_depth = 21, d_raster = 22, d_sampler = 23, d_slots = 24 };
enum ContextSlot : unsigned { c_map = 14, c_get_data = 29, c_slots = 30 };

inline void** g_device_table = nullptr;
inline void** g_context_table = nullptr;
inline std::array<void*, d_slots> g_device_original{};
inline std::array<void*, c_slots> g_context_original{};

template <class F> F device_original(unsigned slot) noexcept { return reinterpret_cast<F>(g_device_original[slot]); }
template <class F> F context_original(unsigned slot) noexcept { return reinterpret_cast<F>(g_context_original[slot]); }

inline HRESULT STDMETHODCALLTYPE create_buffer(ID3D11Device* self, const D3D11_BUFFER_DESC* desc, const D3D11_SUBRESOURCE_DATA* data, ID3D11Buffer** out) {
    const std::int64_t begin = now();
    const HRESULT hr = device_original<CreateBufferFn>(d_buffer)(self, desc, data, out);
    add(buffer_ticks, now() - begin); add(buffers, 1); if (desc) add(buffer_bytes, desc->ByteWidth);
    return hr;
}
inline HRESULT STDMETHODCALLTYPE create_texture2d(ID3D11Device* self, const D3D11_TEXTURE2D_DESC* desc, const D3D11_SUBRESOURCE_DATA* data, ID3D11Texture2D** out) {
    const std::int64_t begin = now();
    const HRESULT hr = device_original<CreateTexture2DFn>(d_tex2d)(self, desc, data, out);
    add(texture_ticks, now() - begin); add(textures, 1);
    if (desc) add(texture_bytes, static_cast<std::int64_t>(double(desc->Width) * desc->Height * (desc->ArraySize ? desc->ArraySize : 1) *
        format_bytes_per_texel(desc->Format) * (desc->MipLevels == 1 ? 1.0 : 4.0 / 3.0)));
    return hr;
}
inline HRESULT STDMETHODCALLTYPE create_texture3d(ID3D11Device* self, const D3D11_TEXTURE3D_DESC* desc, const D3D11_SUBRESOURCE_DATA* data, ID3D11Texture3D** out) {
    const std::int64_t begin = now();
    const HRESULT hr = device_original<CreateTexture3DFn>(d_tex3d)(self, desc, data, out);
    add(texture_ticks, now() - begin); add(textures, 1);
    if (desc) add(texture_bytes, static_cast<std::int64_t>(double(desc->Width) * desc->Height * desc->Depth * format_bytes_per_texel(desc->Format)));
    return hr;
}
template <unsigned Slot> HRESULT STDMETHODCALLTYPE create_view(ID3D11Device* self, ID3D11Resource* resource, const void* desc, void** out) {
    const std::int64_t begin = now();
    const HRESULT hr = device_original<CreateViewFn>(Slot)(self, resource, desc, out);
    add(view_ticks, now() - begin); add(views, 1);
    return hr;
}
inline HRESULT STDMETHODCALLTYPE create_layout(ID3D11Device* self, const D3D11_INPUT_ELEMENT_DESC* elements, UINT count_, const void* code, SIZE_T length, ID3D11InputLayout** out) {
    const std::int64_t begin = now();
    const HRESULT hr = device_original<CreateInputLayoutFn>(d_layout)(self, elements, count_, code, length, out);
    add(state_ticks, now() - begin); add(states, 1);
    return hr;
}
template <unsigned Slot> HRESULT STDMETHODCALLTYPE create_shader(ID3D11Device* self, const void* code, SIZE_T length, ID3D11ClassLinkage* linkage, void** out) {
    const std::int64_t begin = now();
    const HRESULT hr = device_original<CreateShaderFn>(Slot)(self, code, length, linkage, out);
    add(shader_ticks, now() - begin); add(shaders, 1);
    return hr;
}
template <unsigned Slot> HRESULT STDMETHODCALLTYPE create_state(ID3D11Device* self, const void* desc, void** out) {
    const std::int64_t begin = now();
    const HRESULT hr = device_original<CreateStateFn>(Slot)(self, desc, out);
    add(state_ticks, now() - begin); add(states, 1);
    return hr;
}
// The longest Map since the previous row: what was mapped, how, and the
// part it ran in (a Map over 1 ms waited for the GPU or the driver).
struct MapStall { std::atomic<std::int64_t> ticks{0}; std::atomic<int> type{0}, usage{0}, part{0}; std::atomic<std::uint32_t> bytes{0}; };
inline MapStall g_map_stall{};
inline HRESULT STDMETHODCALLTYPE map(ID3D11DeviceContext* self, ID3D11Resource* resource, UINT sub, D3D11_MAP type, UINT flags, D3D11_MAPPED_SUBRESOURCE* mapped) {
    const std::int64_t begin = now();
    const HRESULT hr = context_original<MapFn>(c_map)(self, resource, sub, type, flags, mapped);
    const std::int64_t elapsed = now() - begin;
    add(map_ticks, elapsed); add(maps, 1);
    if (type == D3D11_MAP_WRITE_DISCARD) add(map_discards, 1);
    if (elapsed * ms_per_tick() > 1.0) {
        add(map_stalls, 1); add(map_stall_ticks, elapsed);
        if (elapsed > g_map_stall.ticks.load(std::memory_order_relaxed) && resource) {
            D3D11_RESOURCE_DIMENSION dimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
            resource->GetType(&dimension);
            std::uint32_t bytes = 0; int usage = -1;
            if (dimension == D3D11_RESOURCE_DIMENSION_BUFFER) {
                D3D11_BUFFER_DESC desc{}; static_cast<ID3D11Buffer*>(resource)->GetDesc(&desc);
                bytes = desc.ByteWidth; usage = int(desc.Usage);
            } else if (dimension == D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
                D3D11_TEXTURE2D_DESC desc{}; static_cast<ID3D11Texture2D*>(resource)->GetDesc(&desc);
                bytes = desc.Width * desc.Height; usage = 10 + int(desc.Usage);
            }
            g_map_stall.ticks.store(elapsed, std::memory_order_relaxed);
            g_map_stall.type.store(int(type) | (flags ? 0x100 : 0), std::memory_order_relaxed);
            g_map_stall.usage.store(usage, std::memory_order_relaxed);
            g_map_stall.bytes.store(bytes, std::memory_order_relaxed);
            g_map_stall.part.store(int(current()), std::memory_order_relaxed);
        }
    }
    return hr;
}
inline HRESULT STDMETHODCALLTYPE get_data_call(ID3D11DeviceContext* self, ID3D11Asynchronous* query, void* data, UINT size, UINT flags) {
    const std::int64_t begin = now();
    const HRESULT hr = context_original<GetDataFn>(c_get_data)(self, query, data, size, flags);
    add(get_data_ticks, now() - begin); add(get_data, 1);
    return hr;
}
// The d3d11.dll vtables sit in its PAGE_EXECUTE_READ section next to code
// (crash-005D58EA dump: immediate-context table at d3d11+0x7138). The page
// must stay executable while a slot is written, or another thread running
// d3d11 code on that page faults.
inline bool patch(void** table, unsigned slot, void* replacement, void*& original) noexcept {
    DWORD previous = 0;
    if (!VirtualProtect(&table[slot], sizeof(void*), PAGE_EXECUTE_READWRITE, &previous)) return false;
    original = table[slot];
    table[slot] = replacement;
    DWORD ignored = 0;
    VirtualProtect(&table[slot], sizeof(void*), previous, &ignored);
    return true;
}
inline void restore(void** table, unsigned slot, void* original) noexcept {
    if (!original) return;
    DWORD previous = 0;
    if (!VirtualProtect(&table[slot], sizeof(void*), PAGE_EXECUTE_READWRITE, &previous)) return;
    table[slot] = original;
    DWORD ignored = 0;
    VirtualProtect(&table[slot], sizeof(void*), previous, &ignored);
}
} // namespace vt

// Our device and immediate context only (the game never sees them); installed
// at device creation.
inline void install_device_counters(ID3D11Device* device, ID3D11DeviceContext* context) noexcept {
    using namespace vt;
    if (!enabled() || !device || !context || g_device_table) return;
    g_device_table = *reinterpret_cast<void***>(device);
    g_context_table = *reinterpret_cast<void***>(context);
    auto& d = g_device_original;
    auto& c = g_context_original;
    patch(g_device_table, d_buffer, reinterpret_cast<void*>(&create_buffer), d[d_buffer]);
    patch(g_device_table, d_tex2d, reinterpret_cast<void*>(&create_texture2d), d[d_tex2d]);
    patch(g_device_table, d_tex3d, reinterpret_cast<void*>(&create_texture3d), d[d_tex3d]);
    patch(g_device_table, d_srv, reinterpret_cast<void*>(&create_view<d_srv>), d[d_srv]);
    patch(g_device_table, d_uav, reinterpret_cast<void*>(&create_view<d_uav>), d[d_uav]);
    patch(g_device_table, d_rtv, reinterpret_cast<void*>(&create_view<d_rtv>), d[d_rtv]);
    patch(g_device_table, d_dsv, reinterpret_cast<void*>(&create_view<d_dsv>), d[d_dsv]);
    patch(g_device_table, d_layout, reinterpret_cast<void*>(&create_layout), d[d_layout]);
    patch(g_device_table, d_vs, reinterpret_cast<void*>(&create_shader<d_vs>), d[d_vs]);
    patch(g_device_table, d_gs, reinterpret_cast<void*>(&create_shader<d_gs>), d[d_gs]);
    patch(g_device_table, d_ps, reinterpret_cast<void*>(&create_shader<d_ps>), d[d_ps]);
    patch(g_device_table, d_hs, reinterpret_cast<void*>(&create_shader<d_hs>), d[d_hs]);
    patch(g_device_table, d_ds, reinterpret_cast<void*>(&create_shader<d_ds>), d[d_ds]);
    patch(g_device_table, d_cs, reinterpret_cast<void*>(&create_shader<d_cs>), d[d_cs]);
    patch(g_device_table, d_blend, reinterpret_cast<void*>(&create_state<d_blend>), d[d_blend]);
    patch(g_device_table, d_depth, reinterpret_cast<void*>(&create_state<d_depth>), d[d_depth]);
    patch(g_device_table, d_raster, reinterpret_cast<void*>(&create_state<d_raster>), d[d_raster]);
    patch(g_device_table, d_sampler, reinterpret_cast<void*>(&create_state<d_sampler>), d[d_sampler]);
    // Only Map and GetData: the runtime rewrites its own draw, dispatch, copy
    // and update slots (patched thunks there never ran in eb0af081 runs).
    patch(g_context_table, c_map, reinterpret_cast<void*>(&map), c[c_map]);
    patch(g_context_table, c_get_data, reinterpret_cast<void*>(&get_data_call), c[c_get_data]);
}

inline void uninstall_device_counters() noexcept {
    using namespace vt;
    if (!g_device_table) return;
    for (unsigned slot = 0; slot < d_slots; ++slot) restore(g_device_table, slot, g_device_original[slot]);
    for (unsigned slot = 0; slot < c_slots; ++slot) restore(g_context_table, slot, g_context_original[slot]);
    g_device_original = {};
    g_context_original = {};
    g_device_table = g_context_table = nullptr;
}

} // namespace frame_acct
