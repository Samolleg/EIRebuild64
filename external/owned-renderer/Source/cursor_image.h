#pragma once

#include <cstdint>
#include <vector>

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

} // namespace owned
