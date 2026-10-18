#pragma once
#include "E3Input.h"
#include <cursor_image.h>
#include <memory>
#include <vector>

namespace OwnedRender {
struct E3CursorStrip {
    std::uint64_t id = 0, epoch = 0;
    unsigned width = 0, height = 0, frame_width = 0, frames = 0;
    std::vector<std::uint32_t> pixels;
};
struct E3CursorState {
    std::shared_ptr<const E3CursorStrip> strip;
    POINT position{}, hotspot{};
    unsigned frame = 0;
    DWORD phase_time = 0;
    bool visible = false, locked = false;
};
void E3CursorStart();
void E3CursorStop() noexcept;
void E3CursorPublish(E3CursorState state);
void E3CursorCanvas(HWND window, unsigned width, unsigned height, bool active) noexcept;
HRESULT E3QueueCursor(owned::Renderer& renderer);
}