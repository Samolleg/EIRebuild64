// Load progress during the load reveal hold. The game's loading screen is its
// progress movie (game.exe 005DA400, RENDERER_ARCHITECTURE.md "Load reveal"):
// every frame is the whole picture with the loading text lit up to that
// progress, the game steps it one frame per progress point and never shows
// its last frame (the wanted frame is clamped to frames - 1 and the frame
// shown is the one before the wanted one). While the renderer holds the last
// loading picture, the frames after the one shown follow the hold's real
// progress (load_reveal_progress): decoded once from the same movie through
// the game's own binkw32 imports, composed exactly as the game's picture
// (present_image: aspect fit and fill), the next frame faded in over the
// current one by the fraction of its step that is done. The last frame is
// reached when every awaited component has finished, i.e. as the hold ends.
#include "renderer.h"
#include "engine_bridge.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace owned {
namespace {
#ifndef EID3D11_EMBEDDED
// The function an import slot of game.exe holds now (what the game calls,
// an addon's replacement included), found by the import's name.
void* game_import(const char* dll, const char* name) noexcept {
    const auto* base = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
    if (!base) return nullptr;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;
    const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!directory.VirtualAddress) return nullptr;
    for (auto* entry = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress); entry->Name; ++entry) {
        if (_stricmp(reinterpret_cast<const char*>(base + entry->Name), dll) != 0 || !entry->OriginalFirstThunk) continue;
        const auto* names = reinterpret_cast<const IMAGE_THUNK_DATA32*>(base + entry->OriginalFirstThunk);
        const auto* slots = reinterpret_cast<const IMAGE_THUNK_DATA32*>(base + entry->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL32(names->u1.Ordinal)) continue;
            const auto* by_name = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(by_name->Name), name) == 0)
                return reinterpret_cast<void*>(static_cast<std::uintptr_t>(slots->u1.Function));
        }
    }
    return nullptr;
}

constexpr unsigned kBinkSurface32 = 3; // BGRA bytes

// The frames after `shown` of the movie at `path` as BGRA pixels (opaque
// alpha), decoded through the game's own binkw32 imports. Null on success,
// else why not. The game thread only: the game calls these imports from it.
const char* decode_progress_pixels(const std::string& path, unsigned width, unsigned height, unsigned frames,
    unsigned shown, std::vector<std::vector<std::uint8_t>>& output) {
    output.clear();
    const auto open = reinterpret_cast<void*(__stdcall*)(const char*, unsigned)>(game_import("binkw32.dll", "_BinkOpen@8"));
    const auto do_frame = reinterpret_cast<int(__stdcall*)(void*)>(game_import("binkw32.dll", "_BinkDoFrame@4"));
    const auto copy = reinterpret_cast<int(__stdcall*)(void*, void*, int, unsigned, unsigned, unsigned, unsigned)>(
        game_import("binkw32.dll", "_BinkCopyToBuffer@28"));
    const auto next = reinterpret_cast<void(__stdcall*)(void*)>(game_import("binkw32.dll", "_BinkNextFrame@4"));
    const auto close = reinterpret_cast<void(__stdcall*)(void*)>(game_import("binkw32.dll", "_BinkClose@4"));
    const auto volume = reinterpret_cast<void(__stdcall*)(void*, int)>(game_import("binkw32.dll", "_BinkSetVolume@8"));
    if (!open || !do_frame || !copy || !next || !close) return "game.exe binkw32 imports not found";
    void* handle = open(path.c_str(), 0);
    if (!handle) return "BinkOpen failed";
    struct Closer { void* handle; void(__stdcall* close)(void*); ~Closer() { close(handle); } } closer{handle, close};
    if (volume) volume(handle, 0); // the game's own handle owns any sound
    std::uint32_t header[3]{};
    std::memcpy(header, handle, sizeof(header)); // width, height, frames: as the game reads its handle
    if (header[0] != width || header[1] != height || header[2] != frames)
        return "the opened movie differs from the game's";
    if (width > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || height > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION)
        return "movie larger than a texture";
    // Bink copies only the blocks a frame changed: every frame decodes in
    // order into one persistent buffer (DoFrame, CopyToBuffer, NextFrame), as
    // the game decodes into its one surface.
    const std::size_t pitch = std::size_t(width) * 4;
    std::vector<std::uint8_t> pixels(pitch * height, 0);
    std::vector<std::vector<std::uint8_t>> after;
    after.reserve(frames - shown - 1);
    for (unsigned index = 0; index < frames; ++index) {
        do_frame(handle);
        copy(handle, pixels.data(), int(pitch), height, 0, 0, kBinkSurface32);
        if (index + 1 < frames) next(handle);
        if (index <= shown) continue;
        for (std::size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 255;
        after.push_back(pixels);
    }
    output = std::move(after);
    return nullptr;
}

// Decoded progress frames as GPU textures (the held picture's GPU sources).
const char* create_progress_textures(ID3D11Device* device, unsigned width, unsigned height,
    const std::vector<std::vector<std::uint8_t>>& frames, std::vector<SurfaceImage>& output, D3D12Frame* d3d12 = nullptr) {
    output.clear();
    const std::size_t pitch = std::size_t(width) * 4;
    std::vector<SurfaceImage> after;
    after.reserve(frames.size());
    for (const auto& pixels : frames) {
        if (pixels.size() != pitch * height) return "frame size differs from the movie";
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width; desc.Height = height; desc.MipLevels = desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1; desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        const D3D11_SUBRESOURCE_DATA initial{pixels.data(), UINT(pitch), 0};
        SurfaceImage image;
        image.width = width; image.height = height; image.pitch = LONG(pitch);
        image.gpu_captured = true;
        if (d3d12) {
            if (FAILED(d3d12->create_texture(width, height, desc.Format, image.d3d12)) ||
                FAILED(d3d12->write_texture(*image.d3d12, RECT{0, 0, LONG(width), LONG(height)}, pixels.data(), UINT(pitch))))
                return "frame texture creation failed (Direct3D 12)";
        } else if (!device || FAILED(device->CreateTexture2D(&desc, &initial, &image.texture)) ||
                   FAILED(device->CreateShaderResourceView(image.texture.Get(), nullptr, &image.srv)))
            return "frame texture creation failed";
        after.push_back(std::move(image));
    }
    output = std::move(after);
    return nullptr;
}

// The frames after `shown` as GPU textures (game thread; standard and scene mode).
const char* decode_progress_frames(ID3D11Device* device, const std::string& path, unsigned width, unsigned height,
    unsigned frames, unsigned shown, std::vector<SurfaceImage>& output) {
    output.clear();
    if (!device) return "no device";
    std::vector<std::vector<std::uint8_t>> pixels;
    if (const char* why = decode_progress_pixels(path, width, height, frames, shown, pixels)) return why;
    return create_progress_textures(device, width, height, pixels, output);
}
#endif // retail Bink imports and PE32 parsing

void report_progress_failure(const std::string& path, const char* why) noexcept {
    char text[512];
    std::snprintf(text, sizeof(text), "%s: %s; the held picture stays as it is", path.c_str(), why);
    bridge_report_event("load_progress", text);
}
} // namespace

#ifdef EID3D11_EMBEDDED
void Renderer::set_embedded_movie_image(std::shared_ptr<const SurfaceImage> image) {
    if (scene_queue()) {
        scene_post([this, image = std::move(image)] { set_embedded_movie_image(image); });
        return;
    }
    embedded_movie_image_ = std::move(image);
    load_progress_movie_valid_ = bool(embedded_movie_image_);
    if (!embedded_movie_image_) {
        load_picture_.Reset();
        load_picture_this_frame_ = load_picture_fitted_ = false;
        load_progress_movie_ = {};
    }
}
#endif

// At a loading picture present (the game is inside 005DA400 while it shows a
// progress frame): which frame of which movie the picture shows. Scene
// thread: the game thread read it at the picture (present_image).
void Renderer::capture_load_progress_movie(bool fitted) noexcept try {
#ifdef EID3D11_EMBEDDED
    load_progress_movie_valid_ = fitted && bool(embedded_movie_image_);
#else
    auto& state = load_progress_movie_;
    ProgressMovie movie;
    const auto read_movie = [&] {
        if (!on_scene_thread() && !core_host_) return bridge_progress_movie(movie);
        movie = progress_movie_input_;
        return progress_movie_input_valid_;
    };
    // Only a full-screen picture composed by present_image is redrawn the same way.
    if (!fitted || !read_movie() || movie.current < 2 || movie.frames < 2 ||
        !movie.width || !movie.height) {
        state = {}; load_progress_movie_valid_ = false;
        return;
    }
    const unsigned shown = (std::min)(movie.current - 2, movie.frames - 1);
    if (!load_progress_movie_valid_ || state.source != movie.source || state.path != movie.path || state.frames != movie.frames || state.shown != shown ||
        state.width != movie.width || state.height != movie.height) {
        state = {};
        state.path = movie.path; state.width = movie.width; state.height = movie.height;
        state.frames = movie.frames; state.shown = shown;
        state.source = movie.source;
    }
    load_progress_movie_valid_ = shown + 1 < movie.frames;
#endif
} catch (...) { load_progress_movie_ = {}; load_progress_movie_valid_ = false; }

// Once per load, on the first held frame: the frames after the shown one as
// GPU textures, decoded on the game thread (the game's own movie calls).
// Scene thread: the request goes to queue_present; the frames arrive a frame
// or two later, until then the held picture shows as it is.
#ifndef EID3D11_EMBEDDED
bool Renderer::decode_load_progress_movie() noexcept try {
    auto& state = load_progress_movie_;
    if (state.attempted) return !state.after.empty();
    // Scene thread, or the core-mode host: the game process decodes (its binkw32).
    if (on_scene_thread() || core_host_) {
        std::lock_guard<std::mutex> lock(load_progress_decode_lock_);
        auto& decode = load_progress_decode_;
        if (!decode.serial || decode.path != state.path || decode.width != state.width || decode.height != state.height ||
            decode.frames != state.frames || decode.shown != state.shown) {
            decode = {};
            decode.path = state.path; decode.width = state.width; decode.height = state.height;
            decode.frames = state.frames; decode.shown = state.shown;
            decode.serial = ++load_progress_decode_serials_;
            return false;
        }
        if (!decode.done) return false;
        state.attempted = true;
        state.after = std::move(decode.after); // a failure was reported by the decode
        decode = {};
        return !state.after.empty();
    }
    state.attempted = true;
    if (const char* why = decode_progress_frames(device_.Get(), state.path, state.width, state.height,
            state.frames, state.shown, state.after)) {
        report_progress_failure(state.path, why);
        state.after.clear();
        return false;
    }
    return !state.after.empty();
} catch (...) {
    load_progress_movie_.after.clear();
    bridge_report_event("load_progress", "decode failed (allocation); the held picture stays as it is");
    return false;
}

// Game thread, scene mode (queue_present): a decode the scene thread asked for.
void Renderer::service_load_progress_decode() noexcept try {
    LoadProgressDecode request;
    {
        std::lock_guard<std::mutex> lock(load_progress_decode_lock_);
        const auto& pending = load_progress_decode_;
        if (!pending.serial || pending.taken) return;
        request.path = pending.path; request.width = pending.width; request.height = pending.height;
        request.frames = pending.frames; request.shown = pending.shown; request.serial = pending.serial;
        load_progress_decode_.taken = true;
    }
    const char* why = nullptr;
    try {
        why = decode_progress_frames(device_.Get(), request.path, request.width, request.height,
            request.frames, request.shown, request.after);
    } catch (...) { request.after.clear(); why = "decode failed (allocation)"; }
    if (why) { request.after.clear(); report_progress_failure(request.path, why); }
    std::lock_guard<std::mutex> lock(load_progress_decode_lock_);
    auto& decode = load_progress_decode_;
    // A cancelled or replaced request drops its frames here.
    if (decode.serial == request.serial) { decode.done = true; decode.after = std::move(request.after); }
} catch (...) {}

#endif // retail decoder service

// Scene thread at the reveal: no decode result outlives its load.
void Renderer::cancel_load_progress_decode() noexcept {
    std::lock_guard<std::mutex> lock(load_progress_decode_lock_);
    load_progress_decode_ = {};
}

// A held frame, after the held picture was copied to the back buffer: the
// movie position the progress reaches (frames past the shown one, never
// back), drawn as the game's picture of that frame with the next one faded in.
void Renderer::draw_load_progress(float progress) noexcept try {
#ifdef EID3D11_EMBEDDED
    (void)progress; // Playback time belongs to the host FFmpeg player.
    if (!embedded_movie_image_ || !scene_composited_ ||
        !pending_blits_.empty() || rtv_ != backbuffer_rtv_) return;
    const auto image = embedded_movie_image_;
    if (FAILED(present_image(*image, nullptr, nullptr))) {
        pending_blits_.clear();
        bridge_report_event("load_progress", "host movie composition failed");
        return;
    }
    load_picture_this_frame_ = load_picture_fitted_ = false;
    if (FAILED(render_pending_blits())) {
        pending_blits_.clear();
        bridge_report_event("load_progress", "host movie draw failed");
    }
#else
    auto& state = load_progress_movie_;
    // The frame's world and interface passes are done (present): the back
    // buffer is the target and nothing else is queued.
    if (!scene_composited_ || !pending_blits_.empty() || (!d3d12_ && rtv_ != backbuffer_rtv_)) return;
    const bool typed = bool(state.source);
    if (typed && state.source_failed) return;
    if (!typed && !decode_load_progress_movie()) return;
    const float span = typed ? float(state.frames - state.shown - 1) : float(state.after.size());
    state.position = (std::max)(state.position, std::clamp(progress, 0.f, 1.f) * span);
    if (state.position <= 0.f) return;
    const unsigned base = (std::min)(unsigned(std::floor(state.position)), unsigned(span));
    const float fraction = state.position - float(base);
    const auto image_at = [&](unsigned offset) -> SurfaceImage* {
        if (!typed) return &state.after[offset - 1];
        const unsigned frame = state.shown + offset;
        const unsigned slot = frame % 2;
        if (state.image_frames[slot] != frame) {
            std::vector<std::uint8_t> bgra;
            if (!state.source->read(frame, bgra) ||
                bgra.size() != std::size_t(state.width) * state.height * 4) {
                state.source_failed = true;
                report_progress_failure(state.path, "host frame decode failed");
                return nullptr;
            }
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = state.width; desc.Height = state.height;
            desc.MipLevels = desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_IMMUTABLE; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            const D3D11_SUBRESOURCE_DATA initial{bgra.data(), state.width * 4, 0};
            auto& image = state.images[slot]; image = {};
            image.width = state.width; image.height = state.height;
            image.pitch = LONG(state.width * 4); image.gpu_captured = true;
            if (FAILED(device_->CreateTexture2D(&desc, &initial, &image.texture)) ||
                FAILED(device_->CreateShaderResourceView(image.texture.Get(), nullptr, &image.srv))) {
                state.source_failed = true;
                report_progress_failure(state.path, "host frame upload failed");
                return nullptr;
            }
            state.image_frames[slot] = frame;
        }
        return &state.images[slot];
    };
    if (base > 0) {
        auto* image = image_at(base);
        if (!image || FAILED(present_image(*image, nullptr, nullptr))) { pending_blits_.clear(); return; }
    }
    if (fraction > 0.f && base < unsigned(span)) {
        const std::size_t before = pending_blits_.size();
        auto* image = image_at(base + 1);
        if (!image || FAILED(present_image(*image, nullptr, nullptr))) { pending_blits_.clear(); return; }
        // Only the next frame's picture quad (not its fill), at the fraction's opacity.
        if (pending_blits_.size() == before + 2 && pending_blits_[before].presentation_fill)
            pending_blits_.erase(pending_blits_.begin() + std::ptrdiff_t(before));
        if (pending_blits_.size() == before + 1) {
            auto& top = pending_blits_.back();
            top.force_opaque_alpha = false;
            top.diffuse = (std::uint32_t(std::lround(fraction * 255.f)) << 24) | 0x00ffffffu;
        } else pending_blits_.resize(before);
    }
    load_picture_this_frame_ = load_picture_fitted_ = false; // our own picture, not a loading picture of the game's
    if (!pending_blits_.empty() && FAILED(render_pending_blits())) {
        pending_blits_.clear();
        bridge_report_event("load_progress", "composition failed; the held picture shows instead");
        state.after.clear();
    }
#endif
} catch (...) { pending_blits_.clear(); }

// Core mode, host: the decode request the game process has not taken yet.
#if defined(EID3D11_EMBEDDED) || defined(EID3D11_SHADER_TOOL)
bool Renderer::core_take_progress_request(std::string& path, unsigned& width, unsigned& height, unsigned& frames,
    unsigned& shown, std::uint64_t& serial) noexcept { return false; }
#else
bool Renderer::core_take_progress_request(std::string& path, unsigned& width, unsigned& height, unsigned& frames,
    unsigned& shown, std::uint64_t& serial) noexcept try {
    std::lock_guard<std::mutex> lock(load_progress_decode_lock_);
    auto& pending = load_progress_decode_;
    if (!pending.serial || pending.taken) return false;
    path = pending.path; width = pending.width; height = pending.height; frames = pending.frames; shown = pending.shown;
    serial = pending.serial; pending.taken = true;
    return true;
} catch (...) { return false; }
#endif

// Core mode, host: the game process's decoded frames for request `serial`
// (empty with `failure`: reported, the held picture stays as it is).
#if defined(EID3D11_EMBEDDED) || defined(EID3D11_SHADER_TOOL)
void Renderer::core_progress_frames(std::uint64_t serial, unsigned width, unsigned height,
    const std::vector<std::vector<std::uint8_t>>& frames, const char* failure) noexcept {  }
#else
void Renderer::core_progress_frames(std::uint64_t serial, unsigned width, unsigned height,
    const std::vector<std::vector<std::uint8_t>>& frames, const char* failure) noexcept try {
    std::vector<SurfaceImage> after;
    const char* why = failure;
    if (!why) why = create_progress_textures(device_.Get(), width, height, frames, after, d3d12_);
    std::string path;
    {
        std::lock_guard<std::mutex> lock(load_progress_decode_lock_);
        auto& decode = load_progress_decode_;
        if (decode.serial != serial) return; // cancelled or replaced
        path = decode.path;
        decode.done = true;
        if (!why) decode.after = std::move(after);
    }
    if (why) report_progress_failure(path, why);
} catch (...) {}
#endif

// Core mode, game process: decode a host request with the game's binkw32.
#ifndef EID3D11_EMBEDDED
const char* core_decode_progress_pixels(const std::string& path, unsigned width, unsigned height, unsigned frames,
    unsigned shown, std::vector<std::vector<std::uint8_t>>& output) {
    try { return decode_progress_pixels(path, width, height, frames, shown, output); }
    catch (...) { output.clear(); return "decode failed (allocation)"; }
}

#endif
} // namespace owned

