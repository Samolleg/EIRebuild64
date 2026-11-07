#include "E3Cursor.h"
#include "../AgentBackground.h"
#include "../LiveObserver.h"
#include "../CorePerf.h"
#include "cursor_confinement.h"
#include "../ErrorDialog.h"
#include <engine_bridge.h>
#include <mmsystem.h>
#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <chrono>

namespace {
// Owned by the host. Neither the renderer nor this worker follows game object
// pointers: images and mode/visibility/lock facts are immutable publications.
struct CursorOwner {
    std::mutex mutex;
    std::condition_variable wake;
    std::thread thread;
    bool stop = false, active = false, loadCursor = false;
    HWND window = nullptr;
    unsigned width = 0, height = 0;
    int scale = 100;
    std::uint64_t revision = 0;
    OwnedRender::E3CursorState state;
    owned::CursorPresentation frame;
    ~CursorOwner() { shutdown(); }
    void shutdown() noexcept {
        { std::lock_guard<std::mutex> lock(mutex); stop = true; wake.notify_all(); }
        if (thread.joinable()) thread.join();
        std::lock_guard<std::mutex> lock(mutex);
        state = {}; frame = {};
        active = loadCursor = false; window = nullptr; width = height = 0; ++revision;
    }
    void run() noexcept {
        owned::CursorProducer producer;
        DWORD attached = 0;
        bool reported = false;
        MSG message{}; PeekMessage(&message, nullptr, 0, 0, PM_NOREMOVE); // create the worker input queue
        const DWORD workerThread = GetCurrentThreadId();
        std::unique_lock<std::mutex> lock(mutex);
        while (!stop) {
            const auto version = revision;
            const auto snapshot = state;
            const HWND target = window;
            const unsigned cw = width, ch = height;
            const int percent = scale;
            const bool enabled = active;
            const bool loading = loadCursor;
            lock.unlock();
            owned::CursorPresentation prepared;
            try {
                owned::CursorInput input;
                input.window = target; input.canvas_width = cw; input.canvas_height = ch;
                input.scale_percent = percent;
                input.visible = enabled && snapshot.visible && snapshot.strip &&
                    target && GetForegroundWindow() == target && !IsIconic(target) && IsWindowVisible(target);
                if (input.visible) {
                    const auto& strip = *snapshot.strip;
                    input.image_id = strip.id; input.epoch = strip.epoch;
                    input.source.pixels = reinterpret_cast<const std::uint8_t*>(strip.pixels.data());
                    input.source.pitch = strip.width * 4; input.source.bits = 32;
                    input.source.red_mask = 0x00ff0000; input.source.green_mask = 0x0000ff00;
                    input.source.blue_mask = 0x000000ff;
                    input.source.width = strip.frame_width; input.source.height = strip.height;
                    const auto phase = ((timeGetTime() - snapshot.phase_time) / 125 + snapshot.frame) % strip.frames;
                    input.source.left = phase * strip.frame_width;
                    POINT point = snapshot.position;
                    if (!snapshot.locked && GetCursorPos(&point)) {
                        RECT client{};
                        if (ScreenToClient(target, &point) && GetClientRect(target, &client))
                            point = owned::cursor_output_to_native(point, cw, ch, client.right, client.bottom);
                        else point = snapshot.position;
                    }
                    point.x = std::clamp(point.x, 0L, LONG(cw));
                    point.y = std::clamp(point.y, 0L, LONG(ch));
                    input.hot_x = snapshot.hotspot.x; input.hot_y = snapshot.hotspot.y;
                    input.x = point.x - input.hot_x; input.y = point.y - input.hot_y;
                }
                if (!producer.prepare(input, prepared) && !reported) {
                    reported = true;
                    owned::bridge_report_event("owned_cursor_omitted", "host strip format unsupported");
                }
            } catch (...) {
                // Publish absence rather than reuse another mode/epoch's image.
                if (!reported) {
                    reported = true;
                    owned::bridge_report_event("owned_cursor_omitted", "host snapshot preparation failed");
                }
            }
            lock.lock();
            if (version == revision && !stop) {
                frame = std::move(prepared);
                const std::lock_guard<std::recursive_mutex> modalLock(ErrorDialog::cursorMutex);
                if (ErrorDialog::cursorPauses) frame = {};
                // The held loading picture uses the renderer's system cursor,
                // exactly as the proxy does. Share only the OS input queue so
                // SetCursor can update it while the game thread is loading.
                const DWORD wanted = loading && frame.image ? GetWindowThreadProcessId(target, nullptr) : 0;
                if (attached != wanted) {
                    if (attached) {
                        if (!ErrorDialog::cursorPauses) owned::hide_load_cursor();
                        AttachThreadInput(workerThread, attached, FALSE);
                    }
                    attached = wanted && AttachThreadInput(workerThread, wanted, TRUE) ? wanted : 0;
                }
                if (loading && frame.image && attached) {
                    const auto& image = *frame.image;
                    owned::show_load_cursor(image.key, image.pixels, image.width, image.height,
                        unsigned((std::max)(0L, frame.hot_x)), unsigned((std::max)(0L, frame.hot_y)));
                } else if (loading && !ErrorDialog::cursorPauses) owned::hide_load_cursor();
            }
            wake.wait_for(lock, std::chrono::milliseconds(41), [&] { return stop || revision != version; });
        }
        if (attached) {
            owned::hide_load_cursor();
            AttachThreadInput(workerThread, attached, FALSE);
        }
    }
} cursor;
}

namespace OwnedRender {
void E3CursorStart() {
    if (AgentBackground::Enabled()) return;
    std::lock_guard<std::mutex> lock(cursor.mutex);
    if (cursor.thread.joinable()) return;
    cursor.stop = false;
    cursor.thread = std::thread([] { cursor.run(); });
}
void E3CursorStop() noexcept { cursor.shutdown(); if (!AgentBackground::Enabled()) owned::hide_load_cursor(); }
void E3CursorPublish(E3CursorState state) {
    std::lock_guard<std::mutex> lock(cursor.mutex);
    // Keep an already prepared frame only for the identical mode/visibility.
    if (cursor.state.strip != state.strip || cursor.state.visible != state.visible ||
        cursor.state.locked != state.locked) cursor.frame = {};
    cursor.state = std::move(state); ++cursor.revision; cursor.wake.notify_all();
}
void E3CursorCanvas(HWND window, unsigned width, unsigned height, bool active) noexcept {
    CorePerf::Scope timing(CorePerf::UiCursorCanvas);
    std::lock_guard<std::mutex> lock(cursor.mutex);
    if (cursor.window != window || cursor.width != width || cursor.height != height || cursor.active != active) {
        cursor.frame = {}; ++cursor.revision;
    }
    if (!active || cursor.window != window) cursor.state = {};
    cursor.window = window; cursor.width = width; cursor.height = height; cursor.active = active;
    cursor.wake.notify_all();
}
HRESULT E3QueueCursor(owned::Renderer& renderer, int percent, bool loading) {
    if (AgentBackground::Enabled()) return S_OK;
    owned::CursorPresentation frame;
    {
        std::lock_guard<std::mutex> lock(cursor.mutex);
        if (cursor.scale != percent) { cursor.scale = percent; cursor.frame = {}; ++cursor.revision; cursor.wake.notify_all(); }
        cursor.loadCursor = loading;
        if (cursor.active && cursor.window && GetForegroundWindow() == cursor.window && !ErrorDialog::Active())
            frame = cursor.frame;
        const std::lock_guard<std::recursive_mutex> modalLock(ErrorDialog::cursorMutex);
        if (ErrorDialog::cursorPauses) return S_OK;
        return owned::queue_owned_cursor(renderer, frame, cursor.loadCursor);
    }
}
BOOL E3ReadCursor(POINT* point) noexcept {
    if (LiveObserver::ReadUiCursor(point)) return TRUE;
    if (AgentBackground::Enabled()) {
        if (!point) return FALSE;
        unsigned w=0,h=0;E3Canvas(w,h);*point=POINT{LONG(w/2),LONG(h/2)};return TRUE;
    }
    if (!point || !GetCursorPos(point)) return FALSE;
    const HWND window = E3Window();
    if (!window) return TRUE;
    if (!ScreenToClient(window, point)) return FALSE;
    static HWND lastWindow = nullptr;
    static POINT last{};
    RECT client{};
    if (GetForegroundWindow() == window) { last = *point; lastWindow = window; }
    else if (GetClientRect(window, &client)) {
        *point = lastWindow == window ? last : POINT{client.right / 2, client.bottom / 2};
        *point = owned::harness_cursor_point(*point, client.right, client.bottom);
    }
    *point = owned::game_output_to_native(window, *point);
    return TRUE;
}
BOOL E3WriteCursor(int x, int y) noexcept {
    if (LiveObserver::WriteUiCursor(x,y)) return TRUE;
    if (AgentBackground::Enabled()) return FALSE;
    HWND window = E3Window();
    if (!window) return SetCursorPos(x, y);
    if (GetForegroundWindow() != window || ErrorDialog::Active()) return FALSE;
    POINT point = owned::game_native_to_output(window, POINT{x, y});
    if (!ClientToScreen(window, &point)) return FALSE;
    return SetCursorPos(point.x, point.y);
}
}