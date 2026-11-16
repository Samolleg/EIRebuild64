#include "owned_ui_layers.h"
#include "game_sites.h"
#include "ui_language.h"

#include <richedit.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>

namespace owned {
// Defined in engine_bridge.cpp (bridge.jsonl).
void append_bridge_event(const char* event, const char* detail,
                         bool sites_verified, const char* stage,
                         const char* edge, unsigned sequence) noexcept;
namespace {

// CI3DSurface::GetDC / ReleaseDC call the surface's DirectDraw GetDC
// (vtable +0x44) and ReleaseDC (+0x68); the return address of the first is
// the claim site. Shipped bytes of the supported image (BE0FC330...).
constexpr std::uint32_t kGetDcCallRva = 0x001fdc24u;
constexpr std::uint32_t kGetDcReturnRva = 0x001fdc27u;
constexpr std::uint32_t kReleaseDcCallRva = 0x001fdc81u;
constexpr std::uint8_t kGetDcCall[3] = {0xff, 0x51, 0x44};
constexpr std::uint8_t kReleaseDcCall[3] = {0xff, 0x51, 0x68};

using DrawTextAFn = int(WINAPI*)(HDC, LPCSTR, int, LPRECT, UINT);
using ExtTextOutAFn = BOOL(WINAPI*)(HDC, int, int, UINT, const RECT*, LPCSTR,
                                    UINT, const INT*);
using TextOutAFn = BOOL(WINAPI*)(HDC, int, int, LPCSTR, int);
using LineToFn = BOOL(WINAPI*)(HDC, int, int);
using PolylineToFn = BOOL(WINAPI*)(HDC, const POINT*, DWORD);
using SetPixelFn = COLORREF(WINAPI*)(HDC, int, int, COLORREF);
using PatBltFn = BOOL(WINAPI*)(HDC, int, int, int, int, DWORD);
using FrameRectFn = int(WINAPI*)(HDC, const RECT*, HBRUSH);
using TabbedTextOutAFn = LONG(WINAPI*)(HDC, int, int, LPCSTR, int, int,
                                       const INT*, int);
using GrayStringAFn = BOOL(WINAPI*)(HDC, HBRUSH, GRAYSTRINGPROC, LPARAM, int,
                                    int, int, int, int);
using SendMessageAFn = LRESULT(WINAPI*)(HWND, UINT, WPARAM, LPARAM);

struct RealFunctions {
    DrawTextAFn draw_text = &::DrawTextA;
    ExtTextOutAFn ext_text_out = &::ExtTextOutA;
    TextOutAFn text_out = &::TextOutA;
    LineToFn line_to = &::LineTo;
    PolylineToFn polyline_to = &::PolylineTo;
    SetPixelFn set_pixel = &::SetPixel;
    PatBltFn pat_blt = &::PatBlt;
    FrameRectFn frame_rect = &::FrameRect;
    TabbedTextOutAFn tabbed_text_out = &::TabbedTextOutA;
    GrayStringAFn gray_string = &::GrayStringA;
    SendMessageAFn send_message = &::SendMessageA;
} g_real;

// Import slots of the supported game.exe (shipped import table).
struct ImportSlot {
    std::uint32_t rva;
    const char* module;
    const char* name;
    void* replacement;
    void** real;
};

std::array<ImportSlot, 11> import_slots() noexcept {
    std::array<ImportSlot, 11> slots{{
        {0x0033b4f0u, "user32.dll", "DrawTextA",
         reinterpret_cast<void*>(&ui_layer_draw_text_a),
         reinterpret_cast<void**>(&g_real.draw_text)},
        {0x0033b074u, "gdi32.dll", "ExtTextOutA",
         reinterpret_cast<void*>(&ui_layer_ext_text_out_a),
         reinterpret_cast<void**>(&g_real.ext_text_out)},
        {0x0033b078u, "gdi32.dll", "TextOutA",
         reinterpret_cast<void*>(&ui_layer_text_out_a),
         reinterpret_cast<void**>(&g_real.text_out)},
        {0x0033b0e8u, "gdi32.dll", "LineTo",
         reinterpret_cast<void*>(&ui_layer_line_to),
         reinterpret_cast<void**>(&g_real.line_to)},
        {0x0033b0c8u, "gdi32.dll", "PolylineTo",
         reinterpret_cast<void*>(&ui_layer_polyline_to),
         reinterpret_cast<void**>(&g_real.polyline_to)},
        {0x0033b188u, "gdi32.dll", "SetPixel",
         reinterpret_cast<void*>(&ui_layer_set_pixel),
         reinterpret_cast<void**>(&g_real.set_pixel)},
        {0x0033b060u, "gdi32.dll", "PatBlt",
         reinterpret_cast<void*>(&ui_layer_pat_blt),
         reinterpret_cast<void**>(&g_real.pat_blt)},
        {0x0033b4bcu, "user32.dll", "FrameRect",
         reinterpret_cast<void*>(&ui_layer_frame_rect),
         reinterpret_cast<void**>(&g_real.frame_rect)},
        {0x0033b4ecu, "user32.dll", "TabbedTextOutA",
         reinterpret_cast<void*>(&ui_layer_tabbed_text_out_a),
         reinterpret_cast<void**>(&g_real.tabbed_text_out)},
        {0x0033b4f4u, "user32.dll", "GrayStringA",
         reinterpret_cast<void*>(&ui_layer_gray_string_a),
         reinterpret_cast<void**>(&g_real.gray_string)},
        {0x0033b4c8u, "user32.dll", "SendMessageA",
         reinterpret_cast<void*>(&ui_layer_send_message_a),
         reinterpret_cast<void**>(&g_real.send_message)},
    }};
    if (running_game_sites().report.engine == GameEngine::lia) {
        static_assert(std::size(kLiaUiImportRvas) == std::tuple_size<decltype(slots)>::value);
        for (std::size_t index = 0; index < slots.size(); ++index)
            slots[index].rva = kLiaUiImportRvas[index];
    }
    return slots;
}

std::uint8_t* g_image = nullptr;
std::uint32_t g_get_dc_return_rva = 0;

enum class Op : std::uint8_t {
    draw_text = 1,
    ext_text_out,
    text_out,
    lines,
    set_pixel,
    pat_blt,
    frame_rect,
    tabbed_text_out,
};

// Every recorded struct is made of 4-byte fields (no padding), so recipes
// compare byte for byte.
struct TextState {
    LOGFONTA font;
    COLORREF color;
    COLORREF background;
    int background_mode;
    UINT align;
    int extra;
};
struct DrawTextRecord {
    TextState state;
    RECT rect;
    UINT format;
    int length;
};
struct ExtTextOutRecord {
    TextState state;
    int x, y;
    UINT options;
    int has_rect;
    RECT rect;
    UINT count;
    UINT dx_values;
};
struct TextOutRecord {
    TextState state;
    int x, y;
    int length;
};
struct LinesRecord {
    LOGPEN pen;
    int rop2;
    POINT from;
    DWORD count;
};
struct SetPixelRecord {
    int x, y;
    COLORREF color;
};
struct PatBltRecord {
    LOGBRUSH brush;
    int x, y, width, height;
    DWORD rop;
};
struct FrameRectRecord {
    LOGBRUSH brush;
    RECT rect;
};
struct TabbedRecord {
    TextState state;
    int x, y;
    int length;
    int tab_count;
    int origin;
};

struct Session {
    std::atomic<HDC> active{nullptr};
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ old_bitmap = nullptr;
    int saved = 0;
    void* context = nullptr;
    UiLayerPromote promote = nullptr;
    HDC legacy = nullptr;
    LONG width = 0;  // The surface the recipe replays onto.
    UiLayerFailure failure = UiLayerFailure::none;
    std::vector<std::uint8_t> recipe;
};

// Concurrent CI3DSurface DC sessions (nested GetDC in the inventory code);
// a session beyond this fails to begin and the owner reports it.
std::array<Session, 16> g_sessions;
HRGN g_clip_probe = nullptr;

Session* find_session(HDC dc) noexcept {
    if (!dc) return nullptr;
    for (auto& session : g_sessions)
        if (session.active.load(std::memory_order_acquire) == dc)
            return &session;
    return nullptr;
}

void fail(Session& session, UiLayerFailure failure) noexcept {
    if (session.failure == UiLayerFailure::none) session.failure = failure;
}

bool default_mapping(Session& session, HDC dc) noexcept {
    POINT viewport{}, window{};
    if (!g_clip_probe) g_clip_probe = CreateRectRgn(0, 0, 0, 0);
    const bool ok = g_clip_probe && GetMapMode(dc) == MM_TEXT &&
        GetGraphicsMode(dc) == GM_COMPATIBLE && GetLayout(dc) == 0 &&
        GetViewportOrgEx(dc, &viewport) && GetWindowOrgEx(dc, &window) &&
        !viewport.x && !viewport.y && !window.x && !window.y &&
        GetClipRgn(dc, g_clip_probe) == 0;
    if (!ok) fail(session, UiLayerFailure::unsupported_state);
    return ok;
}

bool capture_text(Session& session, HDC dc, TextState& state) noexcept {
    state = {};
    if (!default_mapping(session, dc)) return false;
    const HGDIOBJ font = GetCurrentObject(dc, OBJ_FONT);
    if (!font || GetObjectA(font, sizeof(state.font), &state.font) !=
                     static_cast<int>(sizeof(state.font))) {
        fail(session, UiLayerFailure::unsupported_object);
        return false;
    }
    // Bytes after the face name terminator are not part of the font.
    const std::size_t name = strnlen(state.font.lfFaceName, LF_FACESIZE);
    if (name < LF_FACESIZE)
        std::memset(state.font.lfFaceName + name, 0, LF_FACESIZE - name);
    state.color = GetTextColor(dc);
    state.background = GetBkColor(dc);
    state.background_mode = GetBkMode(dc);
    state.align = GetTextAlign(dc);
    state.extra = GetTextCharacterExtra(dc);
    if (state.align == GDI_ERROR || (state.align & TA_UPDATECP)) {
        fail(session, UiLayerFailure::unsupported_state);
        return false;
    }
    return true;
}

bool capture_brush(Session& session, HGDIOBJ brush, LOGBRUSH& output) noexcept {
    output = {};
    if (!brush || GetObjectType(brush) != OBJ_BRUSH ||
        GetObjectA(brush, sizeof(output), &output) !=
            static_cast<int>(sizeof(output)) ||
        (output.lbStyle != BS_SOLID && output.lbStyle != BS_NULL)) {
        fail(session, UiLayerFailure::unsupported_object);
        return false;
    }
    output.lbHatch = 0;
    if (output.lbStyle == BS_NULL) output.lbColor = 0;
    return true;
}

bool capture_lines(Session& session, HDC dc, LinesRecord& record) noexcept {
    record = {};
    if (!default_mapping(session, dc)) return false;
    const HGDIOBJ pen = GetCurrentObject(dc, OBJ_PEN);
    if (!pen || GetObjectType(pen) != OBJ_PEN ||
        GetObjectA(pen, sizeof(record.pen), &record.pen) !=
            static_cast<int>(sizeof(record.pen))) {
        fail(session, UiLayerFailure::unsupported_object);
        return false;
    }
    record.rop2 = GetROP2(dc);
    if (!GetCurrentPositionEx(dc, &record.from)) {
        fail(session, UiLayerFailure::unsupported_state);
        return false;
    }
    return true;
}

template <class T>
void put(std::vector<std::uint8_t>& output, const T& value) {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(&value);
    output.insert(output.end(), bytes, bytes + sizeof(T));
}

void put_bytes(std::vector<std::uint8_t>& output, const void* data,
               std::size_t size) {
    if (!size) return;
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    output.insert(output.end(), bytes, bytes + size);
}

// Appends one operation; after promotion it is also drawn on the legacy DC.
template <class Record, class Tail>
void record_op(Session& session, Op op, const Record& record, Tail&& tail) noexcept {
    if (session.failure != UiLayerFailure::none) return;
    const std::size_t start = session.recipe.size();
    try {
        session.recipe.push_back(static_cast<std::uint8_t>(op));
        put(session.recipe, record);
        tail(session.recipe);
    } catch (...) {
        session.recipe.resize(start);
        fail(session, UiLayerFailure::allocation);
        return;
    }
    if (session.legacy &&
        !ui_layer_replay(session.recipe.data() + start,
                         session.recipe.size() - start, session.legacy))
        fail(session, UiLayerFailure::allocation);
}

void no_tail(std::vector<std::uint8_t>&) {}

std::vector<std::pair<LOGFONTA, HFONT>> g_fonts;

// The game's text bytes are in its "Font CharSet" code page. When that is not
// the Windows ANSI code page (Russian data on a ja-JP system), the A
// functions would decode them wrongly, so replay converts with the game's
// code page and draws with the W functions; measurement then uses the same
// decoded text and the same replay font, so fit and draw agree.
bool replay_wide() noexcept { return game_text_code_page() != CP_ACP; }

std::wstring g_wide;

bool widen(const std::uint8_t* text, int length) noexcept {
    try {
        g_wide.clear();
        if (length <= 0) return true;
        const UINT code_page = game_text_code_page();
        const auto* bytes = reinterpret_cast<LPCSTR>(text);
        const int count = MultiByteToWideChar(code_page, 0, bytes, length, nullptr, 0);
        if (count <= 0) return false;
        g_wide.resize(static_cast<std::size_t>(count));
        return MultiByteToWideChar(code_page, 0, bytes, length, g_wide.data(), count) == count;
    } catch (...) {
        return false;
    }
}

HFONT create_replay_font(const LOGFONTA& font) noexcept {
    if (!replay_wide()) return CreateFontIndirectA(&font);
    LOGFONTW wide{};
    static_assert(offsetof(LOGFONTA, lfFaceName) == offsetof(LOGFONTW, lfFaceName));
    std::memcpy(&wide, &font, offsetof(LOGFONTA, lfFaceName));
    // GetObjectA returned the face name in the Windows ANSI code page.
    const int face = static_cast<int>(strnlen(font.lfFaceName, LF_FACESIZE - 1));
    if (face && MultiByteToWideChar(CP_ACP, 0, font.lfFaceName, face,
                                    wide.lfFaceName, LF_FACESIZE - 1) <= 0)
        return nullptr;
    return CreateFontIndirectW(&wide);
}

HFONT replay_font(const LOGFONTA& font) noexcept {
    for (const auto& entry : g_fonts)
        if (std::memcmp(&entry.first, &font, sizeof(font)) == 0)
            return entry.second;
    const HFONT created = create_replay_font(font);
    if (!created) return nullptr;
    try {
        g_fonts.emplace_back(font, created);
    } catch (...) {
        DeleteObject(created);
        return nullptr;
    }
    return created;
}

// DrawText results for the recording DC: the height (and CALCRECT rectangle)
// is a pure function of font, DC text state, format, rectangle and string,
// so it is measured once on the recording DC (bounds accumulation paused)
// and reused while the string keeps being drawn.
struct MeasuredText {
    int result = 0;
    RECT rect{};
    ULONGLONG used = 0;
};
std::unordered_map<std::string, MeasuredText> g_measured;
ULONGLONG g_measured_sweep = 0;
constexpr ULONGLONG kMeasuredRetentionMs = 10000;

int measure_draw_text(HDC dc, const TextState& state, LPCSTR text, int length,
                      LPRECT rect, UINT format) noexcept {
    const ULONGLONG now = GetTickCount64();
    std::string key;
    try {
        key.reserve(sizeof(state) + sizeof(RECT) + sizeof(format) +
                    static_cast<std::size_t>(length));
        key.append(reinterpret_cast<const char*>(&state), sizeof(state));
        key.append(reinterpret_cast<const char*>(rect), sizeof(RECT));
        key.append(reinterpret_cast<const char*>(&format), sizeof(format));
        key.append(text, static_cast<std::size_t>(length));
        const auto found = g_measured.find(key);
        if (found != g_measured.end()) {
            found->second.used = now;
            *rect = found->second.rect;
            return found->second.result;
        }
    } catch (...) {
        key.clear();
    }
    RECT measured = *rect;
    int result = 0;
    // The recording DC is a 1x1 bitmap: a drawing call here rasterizes every
    // glyph only to discard it (laptop j214: ~10 samples, ~15 ms of the
    // outfitting open, before the replay rasterized them again). Where the
    // return value is the text height (no vertical alignment), DT_CALCRECT
    // returns the same height without drawing; the caller's rectangle is
    // left as a drawing call leaves it.
    constexpr UINT kDrawnOnly = DT_CALCRECT | DT_VCENTER | DT_BOTTOM | DT_MODIFYSTRING |
        DT_EDITCONTROL | DT_END_ELLIPSIS | DT_WORD_ELLIPSIS | DT_PATH_ELLIPSIS;
    const bool height_only = length > 0 && !(format & kDrawnOnly);
    RECT calculated = *rect;
    RECT* const target = height_only ? &calculated : &measured;
    const UINT call_format = height_only ? format | DT_CALCRECT : format;
    SetBoundsRect(dc, nullptr, DCB_DISABLE);
    if (!replay_wide()) {
        result = g_real.draw_text(dc, text, length, target, call_format);
    } else {
        const HFONT font = replay_font(state.font);
        const HGDIOBJ old = font ? SelectObject(dc, font) : nullptr;
        if (old && old != HGDI_ERROR &&
            widen(reinterpret_cast<const std::uint8_t*>(text), length))
            result = DrawTextW(dc, g_wide.c_str(), static_cast<int>(g_wide.size()),
                               target, call_format);
        if (old && old != HGDI_ERROR) SelectObject(dc, old);
    }
    SetBoundsRect(dc, nullptr, DCB_ENABLE);
    *rect = measured;
    if (!key.empty() && result) {
        try {
            if (now - g_measured_sweep > kMeasuredRetentionMs) {
                for (auto it = g_measured.begin(); it != g_measured.end();)
                    it = it->second.used < g_measured_sweep
                        ? g_measured.erase(it) : std::next(it);
                g_measured_sweep = now;
            }
            g_measured.emplace(std::move(key), MeasuredText{result, measured, now});
        } catch (...) {
        }
    }
    return result;
}

// ---- Replay ----

// ---- Whole descriptions ----
// User rule: descriptions are never shortened. GDI ellipses replace the end
// of a line with "..."; the addon's skill-card description lines (each a
// one-line DrawMultiText rectangle) are the producer that asks for them, and
// only those calls change here. They are drawn without ellipsis or clip. When
// word wrapping would put text below the producer's rectangle (over its next
// line) the line is drawn unwrapped; a line wider than its rectangle gets a
// smaller font for this call down to kMinimumWholeFontPercent, then may use
// the surface margin, and shrinks further only if the surface cannot hold it.
constexpr UINT kEllipsisFlags = DT_END_ELLIPSIS | DT_WORD_ELLIPSIS | DT_PATH_ELLIPSIS;
// The owned card lines' floor (character_pane_owned_text.cpp).
constexpr LONG kMinimumWholeFontPercent = 80;

// DrawText measurement with `state`'s font selected on dc, restored after;
// on a recording DC it is cached like the other measurements.
int measure_with_font(HDC dc, const TextState& state, bool recording,
                      LPCSTR text, int length, RECT& rect, UINT format) noexcept {
    const HFONT font = replay_font(state.font);
    const HGDIOBJ previous = font ? SelectObject(dc, font) : nullptr;
    if (!previous || previous == HGDI_ERROR) return 0;
    int result = 0;
    if (recording)
        result = measure_draw_text(dc, state, text, length, &rect, format);
    else if (!replay_wide())
        result = g_real.draw_text(dc, text, length, &rect, format);
    else if (widen(reinterpret_cast<const std::uint8_t*>(text), length))
        result = DrawTextW(dc, g_wide.c_str(), static_cast<int>(g_wide.size()), &rect, format);
    SelectObject(dc, previous);
    return result;
}

// Rewrites an ellipsis call's format and, when needed, its font size: the
// largest size (binary search, a few measurements) whose measured line fits
// its rectangle, or above the floor the surface room. DT_CALCRECT widens the
// rectangle for a word too wide to break, so width is checked even when the
// wrapped text fits the rectangle's height.
void fit_whole_text(HDC dc, TextState& state, bool recording, LPCSTR text,
                    int length, const RECT& rect, LONG surface_width,
                    UINT& format) noexcept {
    format = (format & ~kEllipsisFlags) | DT_NOCLIP;
    if (format & DT_CALCRECT) return;
    const LONG width = rect.right - rect.left;
    const LONG height = rect.bottom - rect.top;
    if ((format & DT_WORDBREAK) && !(format & DT_SINGLELINE)) {
        RECT wrapped = rect;
        if (measure_with_font(dc, state, recording, text, length, wrapped,
                              format | DT_CALCRECT) > height)
            format &= ~DT_WORDBREAK;  // Wrapping would print over the next line.
    }
    const bool wraps = (format & DT_WORDBREAK) && !(format & DT_SINGLELINE);
    // Room for the aligned line inside the surface (DrawText centres an
    // over-wide line on its rectangle).
    const LONG room = surface_width <= 0 ? width
        : (format & DT_CENTER)
            ? width + 2 * (std::min)(rect.left, surface_width - rect.right)
        : (format & DT_RIGHT) ? rect.right : surface_width - rect.left;
    const LOGFONTA original = state.font;
    const LONG size = original.lfHeight < 0 ? -original.lfHeight : original.lfHeight;
    const LONG minimum =
        (std::max)(1L, (size * kMinimumWholeFontPercent + 99) / 100);
    const auto use = [&](LONG trial) {
        if (trial == size || !size) {
            state.font = original;
            return;
        }
        state.font.lfHeight = original.lfHeight < 0 ? -trial : trial;
        state.font.lfWidth = static_cast<LONG>(
            (original.lfWidth * trial + size / 2) / size);
    };
    const auto fits = [&](LONG trial) {
        use(trial);
        RECT line = rect;
        const int measured = measure_with_font(dc, state, recording, text,
                                               length, line, format | DT_CALCRECT);
        return measured > 0 && (!wraps || measured <= height) &&
            line.right - line.left <= (trial > minimum ? width : room);
    };
    if (fits(size)) return;
    LONG best = 0;
    for (LONG low = 1, high = size - 1; low <= high;) {
        const LONG middle = low + (high - low) / 2;
        if (fits(middle)) {
            best = middle;
            low = middle + 1;
        } else {
            high = middle - 1;
        }
    }
    use(best ? best : 1);
    if (!best) {
        static bool reported = false;
        if (!reported) {
            reported = true;
            append_bridge_event("ui_layer_text_overflow",
                "a description line does not fit its surface even at 1 px; "
                "its letters past the surface edge are cut", true,
                nullptr, nullptr, 0);
        }
    }
}

LONG dc_bitmap_width(HDC dc) noexcept {
    BITMAP bitmap{};
    const HGDIOBJ selected = GetCurrentObject(dc, OBJ_BITMAP);
    return selected && GetObjectA(selected, sizeof(bitmap), &bitmap) ==
        static_cast<int>(sizeof(bitmap)) ? bitmap.bmWidth : 0;
}

bool apply_text(HDC target, const TextState& state) noexcept {
    const HFONT font = replay_font(state.font);
    if (!font || !SelectObject(target, font)) return false;
    SetTextColor(target, state.color);
    SetBkColor(target, state.background);
    SetBkMode(target, state.background_mode);
    SetTextAlign(target, state.align);
    SetTextCharacterExtra(target, state.extra);
    return true;
}

class Reader {
public:
    Reader(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}
    bool done() const noexcept { return offset_ == size_; }
    template <class T> bool get(T& value) noexcept {
        if (size_ - offset_ < sizeof(T)) return false;
        std::memcpy(&value, data_ + offset_, sizeof(T));
        offset_ += sizeof(T);
        return true;
    }
    const std::uint8_t* take(std::size_t size) noexcept {
        if (size_ - offset_ < size) return nullptr;
        const std::uint8_t* result = data_ + offset_;
        offset_ += size;
        return result;
    }
private:
    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t offset_ = 0;
};

bool replay_one(Reader& reader, HDC target) noexcept {
    std::uint8_t kind = 0;
    if (!reader.get(kind)) return false;
    switch (static_cast<Op>(kind)) {
    case Op::draw_text: {
        DrawTextRecord record{};
        if (!reader.get(record) || record.length < 0) return false;
        const auto* text = reader.take(static_cast<std::size_t>(record.length));
        if (!text || !apply_text(target, record.state)) return false;
        RECT rect = record.rect;
        if (!replay_wide()) {
            DrawTextA(target, reinterpret_cast<LPCSTR>(text), record.length, &rect,
                      record.format);
            return true;
        }
        if (!widen(text, record.length)) return false;
        DrawTextW(target, g_wide.c_str(), static_cast<int>(g_wide.size()), &rect,
                  record.format);
        return true;
    }
    case Op::ext_text_out: {
        ExtTextOutRecord record{};
        if (!reader.get(record)) return false;
        const auto* dx = reader.take(record.dx_values * sizeof(INT));
        const auto* text = reader.take(record.count);
        if (!dx || !text || !apply_text(target, record.state)) return false;
        // Glyph indices are not text. The game's advances were measured on
        // differently decoded characters, so the W text uses its own font
        // advances (no dx).
        if (replay_wide() && record.count && !(record.options & ETO_GLYPH_INDEX)) {
            if (!widen(text, static_cast<int>(record.count))) return false;
            ExtTextOutW(target, record.x, record.y, record.options & ~ETO_PDY,
                        record.has_rect ? &record.rect : nullptr,
                        g_wide.c_str(), static_cast<UINT>(g_wide.size()), nullptr);
            return true;
        }
        ExtTextOutA(target, record.x, record.y, record.options,
                    record.has_rect ? &record.rect : nullptr,
                    record.count ? reinterpret_cast<LPCSTR>(text) : nullptr,
                    record.count,
                    record.dx_values ? reinterpret_cast<const INT*>(dx) : nullptr);
        return true;
    }
    case Op::text_out: {
        TextOutRecord record{};
        if (!reader.get(record) || record.length < 0) return false;
        const auto* text = reader.take(static_cast<std::size_t>(record.length));
        if (!text || !apply_text(target, record.state)) return false;
        if (!replay_wide()) {
            TextOutA(target, record.x, record.y, reinterpret_cast<LPCSTR>(text),
                     record.length);
            return true;
        }
        if (!widen(text, record.length)) return false;
        TextOutW(target, record.x, record.y, g_wide.c_str(),
                 static_cast<int>(g_wide.size()));
        return true;
    }
    case Op::lines: {
        LinesRecord record{};
        if (!reader.get(record) || !record.count) return false;
        const auto* points = reader.take(record.count * sizeof(POINT));
        if (!points) return false;
        const HPEN pen = CreatePenIndirect(&record.pen);
        if (!pen) return false;
        const HGDIOBJ old = SelectObject(target, pen);
        SetROP2(target, record.rop2);
        MoveToEx(target, record.from.x, record.from.y, nullptr);
        PolylineTo(target, reinterpret_cast<const POINT*>(points), record.count);
        SelectObject(target, old);
        DeleteObject(pen);
        return true;
    }
    case Op::set_pixel: {
        SetPixelRecord record{};
        if (!reader.get(record)) return false;
        SetPixel(target, record.x, record.y, record.color);
        return true;
    }
    case Op::pat_blt: {
        PatBltRecord record{};
        if (!reader.get(record)) return false;
        const HBRUSH brush = CreateBrushIndirect(&record.brush);
        if (!brush) return false;
        const HGDIOBJ old = SelectObject(target, brush);
        PatBlt(target, record.x, record.y, record.width, record.height,
               record.rop);
        SelectObject(target, old);
        DeleteObject(brush);
        return true;
    }
    case Op::frame_rect: {
        FrameRectRecord record{};
        if (!reader.get(record)) return false;
        const HBRUSH brush = CreateBrushIndirect(&record.brush);
        if (!brush) return false;
        FrameRect(target, &record.rect, brush);
        DeleteObject(brush);
        return true;
    }
    case Op::tabbed_text_out: {
        TabbedRecord record{};
        if (!reader.get(record) || record.length < 0 || record.tab_count < 0)
            return false;
        const auto* tabs = reader.take(
            static_cast<std::size_t>(record.tab_count) * sizeof(INT));
        const auto* text = reader.take(static_cast<std::size_t>(record.length));
        if (!tabs || !text || !apply_text(target, record.state)) return false;
        const INT* tab_stops =
            record.tab_count ? reinterpret_cast<const INT*>(tabs) : nullptr;
        if (!replay_wide()) {
            TabbedTextOutA(target, record.x, record.y, reinterpret_cast<LPCSTR>(text),
                           record.length, record.tab_count, tab_stops, record.origin);
            return true;
        }
        if (!widen(text, record.length)) return false;
        TabbedTextOutW(target, record.x, record.y, g_wide.c_str(),
                       static_cast<int>(g_wide.size()), record.tab_count, tab_stops,
                       record.origin);
        return true;
    }
    }
    return false;
}

void release_session_dc(Session& session) noexcept {
    if (session.dc) {
        if (session.old_bitmap) SelectObject(session.dc, session.old_bitmap);
        if (session.bitmap) DeleteObject(session.bitmap);
        DeleteDC(session.dc);
    }
    session.dc = nullptr;
    session.bitmap = nullptr;
    session.old_bitmap = nullptr;
}

bool ensure_session_dc(Session& session) noexcept {
    if (session.dc) return true;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = 1;
    info.bmiHeader.biHeight = -1;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    session.dc = CreateCompatibleDC(nullptr);
    if (session.dc)
        session.bitmap = CreateDIBSection(session.dc, &info, DIB_RGB_COLORS,
                                          &bits, nullptr, 0);
    if (session.bitmap) session.old_bitmap = SelectObject(session.dc, session.bitmap);
    if (!session.dc || !session.bitmap || !session.old_bitmap ||
        session.old_bitmap == HGDI_ERROR) {
        session.old_bitmap = nullptr;
        release_session_dc(session);
        return false;
    }
    return true;
}

void close_session(Session& session) noexcept {
    SetBoundsRect(session.dc, nullptr, DCB_DISABLE);
    RestoreDC(session.dc, session.saved);
    session.saved = 0;
    session.context = nullptr;
    session.promote = nullptr;
    session.legacy = nullptr;
    session.active.store(nullptr, std::memory_order_release);
}

} // namespace

const char* ui_layer_failure_name(UiLayerFailure failure) noexcept {
    switch (failure) {
    case UiLayerFailure::none: return "none";
    case UiLayerFailure::unhooked_drawing: return "unhooked_drawing";
    case UiLayerFailure::unsupported_call: return "unsupported_call";
    case UiLayerFailure::unsupported_state: return "unsupported_state";
    case UiLayerFailure::unsupported_object: return "unsupported_object";
    case UiLayerFailure::allocation: return "allocation";
    case UiLayerFailure::count: break;
    }
    return "unknown";
}

const char* ui_layers_install(std::uint8_t* image) noexcept {
    if (g_image) return nullptr;
    if (!image) return "unsupported_image";
    const bool lia = running_game_sites().report.engine == GameEngine::lia;
    const auto get_dc_call = lia ? kLiaUiGetDcCallRva : kGetDcCallRva;
    const auto release_dc_call = lia ? kLiaUiReleaseDcCallRva : kReleaseDcCallRva;
    if (std::memcmp(image + get_dc_call, kGetDcCall, sizeof(kGetDcCall)) ||
        std::memcmp(image + release_dc_call, kReleaseDcCall,
                    sizeof(kReleaseDcCall)))
        return "surface_dc_call_mismatch";
    auto slots = import_slots();
    std::uintptr_t page = 0;
    for (const auto& slot : slots) {
        if (!slot.rva) continue; // LiA does not import PatBlt or PolylineTo.
        const HMODULE module = GetModuleHandleA(slot.module);
        void* const expected = module
            ? reinterpret_cast<void*>(GetProcAddress(module, slot.name)) : nullptr;
        auto* const cell = reinterpret_cast<void**>(image + slot.rva);
        // Another module patched this slot: its replacement must keep drawing.
        if (!expected || *cell != expected) return "import_slot_not_stock";
        const std::uintptr_t cell_page =
            reinterpret_cast<std::uintptr_t>(cell) & ~std::uintptr_t{0xfff};
        if (page && cell_page != page) return "import_slots_span_pages";
        page = cell_page;
    }
    DWORD protection = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(page), 0x1000, PAGE_READWRITE,
                        &protection))
        return "import_page_protection";
    std::size_t patched = 0;
    for (; patched < slots.size(); ++patched) {
        if (!slots[patched].rva) continue;
        auto* const cell = reinterpret_cast<void**>(image + slots[patched].rva);
        void* const stock = *cell;
        *slots[patched].real = stock;
        if (InterlockedCompareExchangePointer(cell, slots[patched].replacement,
                                              stock) != stock)
            break;
    }
    if (patched != slots.size()) {
        while (patched--) {
            if (!slots[patched].rva) continue;
            auto* const cell = reinterpret_cast<void**>(image + slots[patched].rva);
            InterlockedCompareExchangePointer(cell, *slots[patched].real,
                                              slots[patched].replacement);
        }
    }
    DWORD ignored = 0;
    VirtualProtect(reinterpret_cast<void*>(page), 0x1000, protection, &ignored);
    if (patched != slots.size()) return "import_patch_race";
    g_get_dc_return_rva = lia ? kLiaUiGetDcReturnRva : kGetDcReturnRva;
    g_image = image;
    return nullptr;
}

void ui_layers_uninstall() noexcept {
    if (g_image) {
        auto slots = import_slots();
        const auto page = reinterpret_cast<std::uintptr_t>(g_image + slots[0].rva) &
            ~std::uintptr_t{0xfff};
        DWORD protection = 0;
        if (VirtualProtect(reinterpret_cast<void*>(page), 0x1000,
                           PAGE_READWRITE, &protection)) {
            for (const auto& slot : slots) {
                if (!slot.rva) continue;
                InterlockedCompareExchangePointer(
                    reinterpret_cast<void**>(g_image + slot.rva), *slot.real,
                    slot.replacement);
            }
            DWORD ignored = 0;
            VirtualProtect(reinterpret_cast<void*>(page), 0x1000, protection,
                           &ignored);
        }
        g_image = nullptr;
        g_get_dc_return_rva = 0;
    }
    for (auto& session : g_sessions) {
        if (session.active.load(std::memory_order_acquire)) close_session(session);
        release_session_dc(session);
    }
    for (const auto& entry : g_fonts) DeleteObject(entry.second);
    g_fonts.clear();
    g_measured.clear();
    if (g_clip_probe) DeleteObject(g_clip_probe);
    g_clip_probe = nullptr;
}

bool ui_layers_active() noexcept { return g_image != nullptr; }

bool ui_layer_getdc_caller(const void* return_address) noexcept {
    return g_image && return_address == g_image + g_get_dc_return_rva;
}

HDC ui_layer_begin(void* context, UiLayerPromote promote,
                   std::uint32_t width) noexcept {
    for (auto& session : g_sessions) {
        if (session.active.load(std::memory_order_acquire)) continue;
        if (!ensure_session_dc(session)) return nullptr;
        session.saved = SaveDC(session.dc);
        if (!session.saved) return nullptr;
        SetBoundsRect(session.dc, nullptr, DCB_RESET | DCB_ENABLE);
        session.recipe.clear();
        session.failure = UiLayerFailure::none;
        session.context = context;
        session.promote = promote;
        session.legacy = nullptr;
        session.width = static_cast<LONG>(width);
        session.active.store(session.dc, std::memory_order_release);
        return session.dc;
    }
    return nullptr;
}

UiLayerEnd ui_layer_end(HDC dc, std::vector<std::uint8_t>& recipe) noexcept {
    UiLayerEnd result{};
    Session* session = find_session(dc);
    if (!session) {
        result.failure = UiLayerFailure::unsupported_call;
        return result;
    }
    RECT bounds{};
    if ((GetBoundsRect(dc, &bounds, DCB_RESET) & DCB_SET) == DCB_SET)
        fail(*session, UiLayerFailure::unhooked_drawing);
    result.failure = session->failure;
    result.promoted = session->legacy != nullptr;
    recipe.swap(session->recipe);
    close_session(*session);
    return result;
}

void ui_layer_abort(HDC dc) noexcept {
    if (Session* session = find_session(dc)) close_session(*session);
}

bool ui_layer_replay(const std::uint8_t* recipe, std::size_t size,
                     HDC target, RECT* touched) noexcept {
    if (touched) *touched = {};
    if (!target) return false;
    if (!size) return true;
    const int saved = SaveDC(target);
    if (!saved) return false;
    if (touched) SetBoundsRect(target, nullptr, DCB_RESET | DCB_ENABLE);
    Reader reader(recipe, size);
    bool ok = true;
    while (ok && !reader.done()) ok = replay_one(reader, target);
    if (touched) {
        RECT bounds{};
        if ((GetBoundsRect(target, &bounds, DCB_RESET) & DCB_SET) == DCB_SET)
            *touched = bounds;
        SetBoundsRect(target, nullptr, DCB_DISABLE);
    }
    RestoreDC(target, saved);
    return ok;
}

bool ui_layer_unchanged(const UiLayerState& state,
                        const std::vector<std::uint8_t>& recipe,
                        std::uint64_t generation) noexcept {
    return state.stable && state.generation == generation &&
        state.recipe == recipe;
}

void ui_layer_commit(UiLayerState& state, std::vector<std::uint8_t>& recipe,
                     std::uint64_t generation, bool changed) noexcept {
    // A replay that changed nothing reached the recipe's fixed point; a
    // replay that changed pixels is confirmed by the next identical one.
    state.recipe.swap(recipe);
    state.generation = generation;
    state.stable = !changed;
}

// ---- Import hooks ----

int WINAPI ui_layer_draw_text_a(HDC dc, LPCSTR text, int count, LPRECT rect,
                                UINT format) {
    Session* session = find_session(dc);
    const bool whole = text && rect && (format & kEllipsisFlags);
    if (!session || !text || !rect) {
        if (!whole) return g_real.draw_text(dc, text, count, rect, format);
        // A DC outside the recorded layers: fitted and drawn here.
        TextState state{};
        const HGDIOBJ current = GetCurrentObject(dc, OBJ_FONT);
        if (!current || GetObjectA(current, sizeof(state.font), &state.font) !=
                static_cast<int>(sizeof(state.font)))
            return g_real.draw_text(dc, text, count, rect,
                                    (format & ~kEllipsisFlags) | DT_NOCLIP);
        fit_whole_text(dc, state, false, text, count, *rect,
                       dc_bitmap_width(dc), format);
        const HFONT fitted = replay_font(state.font);
        const HGDIOBJ previous = fitted ? SelectObject(dc, fitted) : nullptr;
        const int result = g_real.draw_text(dc, text, count, rect, format);
        if (previous && previous != HGDI_ERROR) SelectObject(dc, previous);
        return result;
    }
    const int length = count < 0 ? static_cast<int>(std::strlen(text)) : count;
    TextState state{};
    if (format & DT_MODIFYSTRING) fail(*session, UiLayerFailure::unsupported_call);
    const UINT whole_format =
        whole ? (format & ~kEllipsisFlags) | DT_NOCLIP : format;
    if (!capture_text(*session, dc, state) ||
        session->failure != UiLayerFailure::none) {
        RECT ignored = *rect;
        SetBoundsRect(dc, nullptr, DCB_DISABLE);
        const int result = g_real.draw_text(dc, text, length, &ignored, whole_format);
        SetBoundsRect(dc, nullptr, DCB_ENABLE);
        if (format & DT_CALCRECT) *rect = ignored;
        return result;
    }
    if (!(format & DT_CALCRECT)) {
        // Recorded already rewritten, so replay_one draws it unchanged.
        DrawTextRecord record{state, *rect, format, length};
        if (whole)
            fit_whole_text(dc, record.state, true, text, length, *rect,
                           session->width, record.format);
        record_op(*session, Op::draw_text, record,
                  [&](std::vector<std::uint8_t>& out) {
                      put_bytes(out, text, static_cast<std::size_t>(length));
                  });
    }
    return measure_draw_text(dc, state, text, length, rect, whole_format);
}

BOOL WINAPI ui_layer_ext_text_out_a(HDC dc, int x, int y, UINT options,
                                    const RECT* rect, LPCSTR text, UINT count,
                                    const INT* dx) {
    Session* session = find_session(dc);
    if (!session) return g_real.ext_text_out(dc, x, y, options, rect, text, count, dx);
    TextState state{};
    if (!capture_text(*session, dc, state)) return TRUE;
    if (count && !text) {
        fail(*session, UiLayerFailure::unsupported_call);
        return TRUE;
    }
    ExtTextOutRecord record{};
    record.state = state;
    record.x = x;
    record.y = y;
    record.options = options;
    record.has_rect = rect != nullptr;
    if (rect) record.rect = *rect;
    record.count = count;
    record.dx_values = dx ? count * ((options & ETO_PDY) ? 2u : 1u) : 0u;
    record_op(*session, Op::ext_text_out, record,
              [&](std::vector<std::uint8_t>& out) {
                  put_bytes(out, dx, record.dx_values * sizeof(INT));
                  put_bytes(out, text, count);
              });
    return TRUE;
}

BOOL WINAPI ui_layer_text_out_a(HDC dc, int x, int y, LPCSTR text, int count) {
    Session* session = find_session(dc);
    if (!session) return g_real.text_out(dc, x, y, text, count);
    TextState state{};
    if (!capture_text(*session, dc, state)) return TRUE;
    if (count < 0 || (count && !text)) {
        fail(*session, UiLayerFailure::unsupported_call);
        return FALSE;
    }
    TextOutRecord record{state, x, y, count};
    record_op(*session, Op::text_out, record,
              [&](std::vector<std::uint8_t>& out) {
                  put_bytes(out, text, static_cast<std::size_t>(count));
              });
    return TRUE;
}

BOOL WINAPI ui_layer_line_to(HDC dc, int x, int y) {
    Session* session = find_session(dc);
    if (!session) return g_real.line_to(dc, x, y);
    LinesRecord record{};
    if (capture_lines(*session, dc, record)) {
        record.count = 1;
        const POINT point{x, y};
        record_op(*session, Op::lines, record,
                  [&](std::vector<std::uint8_t>& out) { put(out, point); });
    }
    // LineTo leaves the current position at its end point.
    MoveToEx(dc, x, y, nullptr);
    return TRUE;
}

BOOL WINAPI ui_layer_polyline_to(HDC dc, const POINT* points, DWORD count) {
    Session* session = find_session(dc);
    if (!session) return g_real.polyline_to(dc, points, count);
    if (!points || !count) return FALSE;
    LinesRecord record{};
    if (capture_lines(*session, dc, record)) {
        record.count = count;
        record_op(*session, Op::lines, record,
                  [&](std::vector<std::uint8_t>& out) {
                      put_bytes(out, points, count * sizeof(POINT));
                  });
    }
    MoveToEx(dc, points[count - 1].x, points[count - 1].y, nullptr);
    return TRUE;
}

COLORREF WINAPI ui_layer_set_pixel(HDC dc, int x, int y, COLORREF color) {
    Session* session = find_session(dc);
    if (!session) return g_real.set_pixel(dc, x, y, color);
    // Explicit RGB only: palette-relative values depend on the device.
    if (!default_mapping(*session, dc) || (color >> 24)) {
        fail(*session, UiLayerFailure::unsupported_call);
        return static_cast<COLORREF>(-1);
    }
    record_op(*session, Op::set_pixel, SetPixelRecord{x, y, color}, no_tail);
    return color;
}

BOOL WINAPI ui_layer_pat_blt(HDC dc, int x, int y, int width, int height,
                             DWORD rop) {
    Session* session = find_session(dc);
    if (!session) return g_real.pat_blt(dc, x, y, width, height, rop);
    PatBltRecord record{};
    if (default_mapping(*session, dc) &&
        capture_brush(*session, GetCurrentObject(dc, OBJ_BRUSH), record.brush)) {
        record.x = x;
        record.y = y;
        record.width = width;
        record.height = height;
        record.rop = rop;
        record_op(*session, Op::pat_blt, record, no_tail);
    }
    return TRUE;
}

int WINAPI ui_layer_frame_rect(HDC dc, const RECT* rect, HBRUSH brush) {
    Session* session = find_session(dc);
    if (!session) return g_real.frame_rect(dc, rect, brush);
    if (!rect) return 0;
    FrameRectRecord record{};
    if (default_mapping(*session, dc) &&
        capture_brush(*session, brush, record.brush)) {
        record.rect = *rect;
        record_op(*session, Op::frame_rect, record, no_tail);
    }
    return 1;
}

LONG WINAPI ui_layer_tabbed_text_out_a(HDC dc, int x, int y, LPCSTR text,
                                       int count, int tab_count,
                                       const INT* tabs, int tab_origin) {
    Session* session = find_session(dc);
    if (!session)
        return g_real.tabbed_text_out(dc, x, y, text, count, tab_count, tabs,
                                      tab_origin);
    // The returned extent depends on the tab origin: measure on the DC itself,
    // with the decoded text and replay font when replay draws W text.
    TextState state{};
    const bool captured = capture_text(*session, dc, state);
    LONG result = 0;
    SetBoundsRect(dc, nullptr, DCB_DISABLE);
    if (!captured || !replay_wide() || count < 0 || (count && !text)) {
        result = g_real.tabbed_text_out(dc, x, y, text, count, tab_count, tabs,
                                        tab_origin);
    } else {
        const HFONT font = replay_font(state.font);
        const HGDIOBJ old = font ? SelectObject(dc, font) : nullptr;
        if (old && old != HGDI_ERROR &&
            widen(reinterpret_cast<const std::uint8_t*>(text), count))
            result = TabbedTextOutW(dc, x, y, g_wide.c_str(),
                                    static_cast<int>(g_wide.size()), tab_count,
                                    tabs, tab_origin);
        if (old && old != HGDI_ERROR) SelectObject(dc, old);
    }
    SetBoundsRect(dc, nullptr, DCB_ENABLE);
    if (!captured) return result;
    if (count < 0 || (count && !text) || tab_count < 0 || (tab_count && !tabs)) {
        fail(*session, UiLayerFailure::unsupported_call);
        return result;
    }
    TabbedRecord record{state, x, y, count, tab_count, tab_origin};
    record_op(*session, Op::tabbed_text_out, record,
              [&](std::vector<std::uint8_t>& out) {
                  put_bytes(out, tabs, static_cast<std::size_t>(tab_count) * sizeof(INT));
                  put_bytes(out, text, static_cast<std::size_t>(count));
              });
    return result;
}

BOOL WINAPI ui_layer_gray_string_a(HDC dc, HBRUSH brush, GRAYSTRINGPROC proc,
                                   LPARAM data, int count, int x, int y,
                                   int width, int height) {
    Session* session = find_session(dc);
    if (!session)
        return g_real.gray_string(dc, brush, proc, data, count, x, y, width,
                                  height);
    // No shipped interface producer uses it; its callback draws elsewhere.
    fail(*session, UiLayerFailure::unsupported_call);
    return TRUE;
}

LRESULT WINAPI ui_layer_send_message_a(HWND window, UINT message,
                                       WPARAM wparam, LPARAM lparam) {
    if (message != EM_FORMATRANGE || !wparam || !lparam)
        return g_real.send_message(window, message, wparam, lparam);
    FORMATRANGE range = *reinterpret_cast<const FORMATRANGE*>(lparam);
    Session* session = find_session(range.hdc);
    if (!session) session = find_session(range.hdcTarget);
    if (!session) return g_real.send_message(window, message, wparam, lparam);
    if (!session->legacy && session->promote &&
        session->failure == UiLayerFailure::none)
        session->legacy = session->promote(
            session->context, session->recipe.data(), session->recipe.size());
    if (!session->legacy) {
        fail(*session, UiLayerFailure::allocation);
        return 0;
    }
    if (range.hdc == session->dc) range.hdc = session->legacy;
    if (range.hdcTarget == session->dc) range.hdcTarget = session->legacy;
    return g_real.send_message(window, message, wparam,
                               reinterpret_cast<LPARAM>(&range));
}

} // namespace owned
