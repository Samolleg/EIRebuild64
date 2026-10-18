#pragma once

#include <cstdint>
#include <vector>
#include <memory>

namespace owned {

// One cursor frame as straight-alpha BGRA. The game's cursor rule is a binary
// black key (RGB 0 is transparent), so an unscaled frame has alpha 0 or 255.
struct CursorFrameSource {
    const std::uint8_t* pixels = nullptr;
    std::uint32_t pitch = 0;
    std::uint32_t bits = 32;  // 16 or 32
    std::uint32_t red_mask = 0, green_mask = 0, blue_mask = 0;
    std::uint32_t left = 0, top = 0, width = 0, height = 0;
};

// Extracts one frame; false for an unsupported format or empty frame.
bool extract_cursor_frame(const CursorFrameSource& source, std::vector<std::uint32_t>& output);

// Upscales a keyed frame for display: repeated EPX doubling rounds the pixel
// steps on diagonals, then a premultiplied area resample reaches the exact size,
// giving anti-aliased edges. scale <= 1 returns the frame unchanged.
void upscale_cursor_frame(const std::vector<std::uint32_t>& input, std::uint32_t width,
    std::uint32_t height, float scale, std::vector<std::uint32_t>& output,
    std::uint32_t& output_width, std::uint32_t& output_height);

// Load cursor. While the renderer holds the loading picture, presents come
// only as fast as the held frames (a few per second while the pipeline
// warm-up compiles), so the game's current cursor frame, exactly as the
// owned cursor would draw it (same pixels, size and hotspot), is the system
// cursor instead: Windows moves it at the display rate whatever the game
// thread does. Game thread only (presents and the window procedure).
// show: the frame to use (rebuilt when key changes; false when no cursor
// could be made from it, with the load cursor hidden); hide: back to the game's
// hidden system cursor and the owned one; apply: for WM_SETCURSOR over the
// client area, true when the load cursor was set (the game would hide it).
bool show_load_cursor(std::uint64_t key, const std::vector<std::uint32_t>& pixels, std::uint32_t width,
    std::uint32_t height, std::uint32_t hot_x, std::uint32_t hot_y) noexcept;
void hide_load_cursor() noexcept;
bool apply_load_cursor() noexcept;
// Presents that showed the load cursor since the last reset (load_reveal line).
std::uint32_t take_load_cursor_presents() noexcept;


// The proxy and embedded host publish the same immutable cursor inputs. Pixel
// storage stays with the caller until prepare returns; prepared output owns it.
struct CursorInput {
    std::uint64_t image_id = 0, epoch = 0;
    CursorFrameSource source;
    long x = 0, y = 0, hot_x = 0, hot_y = 0;
    float canvas_width = 0, canvas_height = 0;
    void* window = nullptr;
    int scale_percent = 100;
    bool visible = false;
};
struct CursorImage {
    std::uint64_t key = 0;
    std::vector<std::uint32_t> pixels;
    std::uint32_t width = 0, height = 0;
};
struct CursorPresentation {
    std::shared_ptr<const CursorImage> image;
    long left = 0, top = 0, hot_x = 0, hot_y = 0;
};
std::uint64_t cursor_frame_key(const CursorInput& input) noexcept;
class CursorProducer {
    std::shared_ptr<const CursorImage> cached_;
public:
    bool prepare(const CursorInput& input, CursorPresentation& result);
    void reset() noexcept { cached_.reset(); }
    std::uint64_t key() const noexcept { return cached_ ? cached_->key : 0; }
};
class Renderer;
// Clears the previous publication, uses the load cursor only on held pictures,
// otherwise queues exactly the same image after the renderer's other UI.
long queue_owned_cursor(Renderer& renderer, const CursorPresentation& frame,
                        bool load_cursor, bool* load_shown = nullptr);
} // namespace owned
