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

// The frames after `shown` of the movie at `path` as GPU textures, decoded
// through the game's own binkw32 imports. Null on success, else why not.
// The game thread only: the game calls these imports from it.
const char* decode_progress_frames(ID3D11Device* device, const std::string& path, unsigned width, unsigned height,
    unsigned frames, unsigned shown, std::vector<SurfaceImage>& output) {
    output.clear();
    if (!device) return "no device";
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
    std::vector<SurfaceImage> after;
    after.reserve(frames - shown - 1);
    for (unsigned index = 0; index < frames; ++index) {
        do_frame(handle);
        copy(handle, pixels.data(), int(pitch), height, 0, 0, kBinkSurface32);
        if (index + 1 < frames) next(handle);
        if (index <= shown) continue;
        for (std::size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 255;
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width; desc.Height = height; desc.MipLevels = desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1; desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        const D3D11_SUBRESOURCE_DATA initial{pixels.data(), UINT(pitch), 0};
        SurfaceImage image;
        image.width = width; image.height = height; image.pitch = LONG(pitch);
        image.gpu_captured = true;
        if (FAILED(device->CreateTexture2D(&desc, &initial, &image.texture)) ||
            FAILED(device->CreateShaderResourceView(image.texture.Get(), nullptr, &image.srv)))
            return "frame texture creation failed";
        after.push_back(std::move(image));
    }
    output = std::move(after);
    return nullptr;
}

void report_progress_failure(const std::string& path, const char* why) noexcept {
    char text[512];
    std::snprintf(text, sizeof(text), "%s: %s; the held picture stays as it is", path.c_str(), why);
    bridge_report_event("load_progress", text);
}
} // namespace

// At a loading picture present (the game is inside 005DA400 while it shows a
// progress frame): which frame of which movie the picture shows. Scene
// thread: the game thread read it at the picture (present_image).
void Renderer::capture_load_progress_movie(bool fitted) noexcept try {
    auto& state = load_progress_movie_;
    ProgressMovie movie;
    const auto read_movie = [&] {
        if (!on_scene_thread()) return bridge_progress_movie(movie);
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
    if (!load_progress_movie_valid_ || state.path != movie.path || state.frames != movie.frames || state.shown != shown ||
        state.width != movie.width || state.height != movie.height) {
        state = {};
        state.path = movie.path; state.width = movie.width; state.height = movie.height;
        state.frames = movie.frames; state.shown = shown;
    }
    load_progress_movie_valid_ = shown + 1 < movie.frames;
} catch (...) { load_progress_movie_ = {}; load_progress_movie_valid_ = false; }

// Once per load, on the first held frame: the frames after the shown one as
// GPU textures, decoded on the game thread (the game's own movie calls).
// Scene thread: the request goes to queue_present; the frames arrive a frame
// or two later, until then the held picture shows as it is.
bool Renderer::decode_load_progress_movie() noexcept try {
    auto& state = load_progress_movie_;
    if (state.attempted) return !state.after.empty();
    if (on_scene_thread()) {
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

// Scene thread at the reveal: no decode result outlives its load.
void Renderer::cancel_load_progress_decode() noexcept {
    std::lock_guard<std::mutex> lock(load_progress_decode_lock_);
    load_progress_decode_ = {};
}

// A held frame, after the held picture was copied to the back buffer: the
// movie position the progress reaches (frames past the shown one, never
// back), drawn as the game's picture of that frame with the next one faded in.
void Renderer::draw_load_progress(float progress) noexcept try {
    auto& state = load_progress_movie_;
    // The frame's world and interface passes are done (present): the back
    // buffer is the target and nothing else is queued.
    if (!scene_composited_ || !pending_blits_.empty() || rtv_ != backbuffer_rtv_) return;
    if (!decode_load_progress_movie()) return;
    const float span = float(state.after.size());
    state.position = (std::max)(state.position, std::clamp(progress, 0.f, 1.f) * span);
    if (state.position <= 0.f) return;
    const unsigned base = (std::min)(unsigned(std::floor(state.position)), unsigned(state.after.size()));
    const float fraction = state.position - float(base);
    if (base > 0 && FAILED(present_image(state.after[base - 1], nullptr, nullptr))) { pending_blits_.clear(); return; }
    if (fraction > 0.f && base < state.after.size()) {
        const std::size_t before = pending_blits_.size();
        if (FAILED(present_image(state.after[base], nullptr, nullptr))) { pending_blits_.clear(); return; }
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
} catch (...) { pending_blits_.clear(); }

} // namespace owned
