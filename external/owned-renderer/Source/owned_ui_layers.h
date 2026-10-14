#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace owned {

// Owned interface layers (M3 step 1a).
//
// A CI3DSurface DC session (CI3DSurface::GetDC .. ReleaseDC) is answered with
// a pooled recording DC: a real memory DC with a 1x1 bitmap, so font
// selection, state and metrics still run in real GDI. Drawing calls the game
// makes through its own import table are recorded into a byte recipe
// together with the DC state they depend on (font LOGFONT, colours, modes,
// pen, brush) read from the DC at draw time; nothing is drawn. GDI bounds
// accumulation on the recording DC catches any drawing that did not pass
// through a recorded call, which fails the layer.
//
// The owner keeps the last recipe whose application left the surface
// unchanged. An identical recipe on an unchanged surface generation is a
// no-op: no DIB conversion, no GDI rasterization, no upload. Otherwise the
// recipe is replayed with real GDI onto the surface's DIB, so the result is
// the shipped GDI output by construction (exact edges, overlaps, shadows).
//
// A RichEdit render (EM_FORMATRANGE) into a recording DC promotes that
// session to a labelled legacy layer: the owner supplies its legacy DIB DC
// with the recipe so far applied and the rest of the session draws there.

enum class UiLayerFailure : std::uint8_t {
    none,
    unhooked_drawing,   // drawing on the recording DC outside the recorded set
    unsupported_call,   // a recorded entry the recipe cannot represent
    unsupported_state,  // mapping, clipping or update-CP state on the DC
    unsupported_object, // pen/brush/font that cannot be captured by value
    allocation,
    count,
};
const char* ui_layer_failure_name(UiLayerFailure failure) noexcept;

// Returns the DC holding the surface pixels with `recipe` applied, or null.
using UiLayerPromote = HDC (*)(void* context, const std::uint8_t* recipe,
                               std::size_t size) noexcept;

// Verifies the supported game image (CI3DSurface GetDC/ReleaseDC call sites
// and the import slots) and patches the import slots. Returns null when
// active, otherwise a short reason.
const char* ui_layers_install(std::uint8_t* game_image) noexcept;
void ui_layers_uninstall() noexcept;
bool ui_layers_active() noexcept;
// True for the return address of the DirectDraw GetDC call inside
// CI3DSurface::GetDC of the verified image.
bool ui_layer_getdc_caller(const void* return_address) noexcept;

HDC ui_layer_begin(void* context, UiLayerPromote promote) noexcept;
struct UiLayerEnd {
    UiLayerFailure failure = UiLayerFailure::none;
    bool promoted = false;
};
// Finishes the session of `dc`; the recipe is swapped into `recipe`.
UiLayerEnd ui_layer_end(HDC dc, std::vector<std::uint8_t>& recipe) noexcept;
void ui_layer_abort(HDC dc) noexcept;
// Replays onto `target`. `touched`, when given, receives GDI's accumulated
// bounds of everything the replay drew (empty when nothing).
bool ui_layer_replay(const std::uint8_t* recipe, std::size_t size,
                     HDC target, RECT* touched = nullptr) noexcept;

// Per-surface record: the recipe whose last application changed no pixel and
// the surface generation it left.
struct UiLayerState {
    std::vector<std::uint8_t> recipe;
    std::uint64_t generation = 0;
    bool stable = false;
};
bool ui_layer_unchanged(const UiLayerState& state,
                        const std::vector<std::uint8_t>& recipe,
                        std::uint64_t generation) noexcept;
// Records an application of `recipe` (swapped into the state) that left
// `generation`; `changed` reports whether it changed any pixel.
void ui_layer_commit(UiLayerState& state, std::vector<std::uint8_t>& recipe,
                     std::uint64_t generation, bool changed) noexcept;

// Recording entry points behind the game's import slots. A call on any DC
// that is not an active recording DC goes to the real function.
int WINAPI ui_layer_draw_text_a(HDC dc, LPCSTR text, int count, LPRECT rect,
                                UINT format);
BOOL WINAPI ui_layer_ext_text_out_a(HDC dc, int x, int y, UINT options,
                                    const RECT* rect, LPCSTR text, UINT count,
                                    const INT* dx);
BOOL WINAPI ui_layer_text_out_a(HDC dc, int x, int y, LPCSTR text, int count);
BOOL WINAPI ui_layer_line_to(HDC dc, int x, int y);
BOOL WINAPI ui_layer_polyline_to(HDC dc, const POINT* points, DWORD count);
COLORREF WINAPI ui_layer_set_pixel(HDC dc, int x, int y, COLORREF color);
BOOL WINAPI ui_layer_pat_blt(HDC dc, int x, int y, int width, int height,
                             DWORD rop);
int WINAPI ui_layer_frame_rect(HDC dc, const RECT* rect, HBRUSH brush);
LONG WINAPI ui_layer_tabbed_text_out_a(HDC dc, int x, int y, LPCSTR text,
                                       int count, int tab_count,
                                       const INT* tabs, int tab_origin);
BOOL WINAPI ui_layer_gray_string_a(HDC dc, HBRUSH brush, GRAYSTRINGPROC proc,
                                   LPARAM data, int count, int x, int y,
                                   int width, int height);
LRESULT WINAPI ui_layer_send_message_a(HWND window, UINT message,
                                       WPARAM wparam, LPARAM lparam);

} // namespace owned
