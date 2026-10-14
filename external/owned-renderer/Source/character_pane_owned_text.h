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
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace owned {

struct CharacterPaneGlyphAsset {
    std::uint64_t identity = 0;
    std::uint64_t generation = 0;
    std::uint64_t revision = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> coverage;
};

struct CharacterPaneGlyphQuad {
    std::shared_ptr<const CharacterPaneGlyphAsset> asset;
    std::size_t atlas_page_index = static_cast<std::size_t>(-1);
    RECT source{};
    RECT destination{};
    std::uint32_t color = 0;
};

struct CharacterPaneTextFrame {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<CharacterPaneGlyphQuad> quads;
};

enum class CharacterPaneTextFailure : std::uint8_t {
    none,
    dimensions,
    allocation,
    font,
    encoding,
    flags,
    layout,
    glyph,
    dc_transaction,
    submission,
};

const char* character_pane_text_failure_name(
    CharacterPaneTextFailure failure) noexcept;

#ifdef EID3D11_TESTING
struct CharacterPaneShapeTestResult {
    std::size_t source_characters = 0;
    std::size_t glyphs = 0;
    int width = 0;
    bool non_one_to_one = false;
    bool reordered = false;
};
#endif

// Owns bounded append-only glyph atlas pages, immutable published page
// snapshots, and one frame-local list of ordered glyph quads. Engine handles
// are never retained. Completed layouts of recently drawn strings are kept,
// keyed by font generation/slot, format, rectangle size and text bytes, so an
// unchanged string is not measured and reshaped every frame; a layout unused
// for a full sweep interval, or of a retired font generation, is dropped. A
// font generation is published by the exact CInterface3D Init/Fini hooks.
class CharacterPaneOwnedText {
public:
    CharacterPaneOwnedText() = default;
    ~CharacterPaneOwnedText();
    CharacterPaneOwnedText(const CharacterPaneOwnedText&) = delete;
    CharacterPaneOwnedText& operator=(const CharacterPaneOwnedText&) = delete;

    void initialize_fonts(std::uint64_t generation) noexcept;
    void retire_fonts() noexcept;
    bool begin_frame(std::uint32_t width, std::uint32_t height) noexcept;
    int draw(HFONT font, std::uint32_t font_slot, const char* text,
             const RECT& rectangle, UINT format, COLORREF color,
             bool shadow, bool& height_valid) noexcept;
    void finish_dc(bool success) noexcept;
    void fail(CharacterPaneTextFailure failure) noexcept;
    bool take(CharacterPaneTextFrame& frame) noexcept;
    void abort_frame() noexcept;

    bool frame_claimed() const noexcept { return frame_claimed_; }
    bool frame_complete() const noexcept {
        return frame_claimed_ && dc_finished_ &&
            failure_ == CharacterPaneTextFailure::none;
    }
    CharacterPaneTextFailure failure() const noexcept { return failure_; }
    std::uint64_t font_generation() const noexcept { return font_generation_; }
    std::size_t cached_glyphs() const noexcept { return glyphs_.size(); }
    std::size_t cached_glyph_bytes() const noexcept { return glyph_bytes_; }
    std::size_t frame_quads() const noexcept { return quads_.size(); }
#ifdef EID3D11_TESTING
    std::size_t test_frame_shape_calls() const noexcept {
        return frame_shape_calls_;
    }
    bool test_shape_wide(HFONT font, const wchar_t* text,
                         std::size_t length,
                         CharacterPaneShapeTestResult& result) noexcept;
#endif

private:
    struct GlyphKey {
        std::uint64_t font_generation = 0;
        std::uint32_t font_slot = 0;
        std::uint16_t glyph = 0;
        bool operator<(const GlyphKey& other) const noexcept;
    };
    struct GlyphMask {
        int left = 0;
        int top = 0;
        int width = 0;
        int height = 0;
        std::size_t page_index = static_cast<std::size_t>(-1);
        RECT source{};
    };
    struct ShapedRun {
        std::vector<std::uint16_t> glyphs;
        std::vector<int> advances;
        std::vector<UINT> order;
        std::vector<int> carets;
        int width = 0;
    };
    struct DisplayText {
        std::wstring text;
        std::vector<std::size_t> underlined;
    };
    struct Line {
        std::size_t start = 0;
        std::size_t length = 0;
        int width = 0;
        ShapedRun shaped;
    };
    struct LayoutKey {
        std::uint64_t font_generation = 0;
        std::uint32_t font_slot = 0;
        UINT format = 0;
        LONG width = 0;
        LONG height = 0;
        std::string text;
        bool operator<(const LayoutKey& other) const noexcept;
    };
    struct TextLayout {
        DisplayText display;
        std::vector<Line> lines;
        int height = 0;
        LONG line_height = 0;
        LONG ascent = 0;
        std::uint64_t used_sweep = 0;
    };
    struct AtlasPage {
        std::uint32_t font_slot = 0;
        std::uint32_t next_x = 1;
        std::uint32_t next_y = 1;
        std::uint32_t row_height = 0;
        std::uint64_t modified_frame = 0;
        std::shared_ptr<CharacterPaneGlyphAsset> asset;
    };
    bool parse_text(const char* text, UINT format,
                    DisplayText& output) noexcept;
    bool shape_run(HDC dc, const wchar_t* text, std::size_t length,
                   ShapedRun& output) noexcept;
    bool layout(HDC dc, const DisplayText& text, int maximum_width,
                bool word_break, bool single_line,
                std::vector<Line>& lines) noexcept;
    bool draw_line(HDC dc, const DisplayText& text, const Line& line,
                   int x, int baseline, COLORREF color, const RECT& clip,
                   std::uint32_t font_slot, int underline_offset,
                   int& underlines) noexcept;
    bool realize_glyph(HDC dc, std::uint32_t font_slot,
                       std::uint16_t glyph, const GlyphMask*& output) noexcept;
    bool append_glyph(const GlyphMask& glyph, int pen_x, int baseline_y,
                      COLORREF color, const RECT& clip) noexcept;
    bool append_line(int left, int right, int y, COLORREF color,
                     const RECT& clip) noexcept;
    bool place_glyph(std::uint32_t font_slot, std::uint32_t width,
                     std::uint32_t height, const std::uint8_t* coverage,
                     GlyphMask& mask) noexcept;
    bool make_page_mutable(std::size_t page_index) noexcept;
    bool allocate_page(std::uint32_t font_slot, std::uint32_t minimum_width,
                       std::uint32_t minimum_height,
                       std::size_t& page_index) noexcept;
    bool allocate_snapshot(std::uint64_t identity, std::uint64_t generation,
                           std::uint64_t revision, std::uint32_t width,
                           std::uint32_t height,
                           std::shared_ptr<CharacterPaneGlyphAsset>& asset) noexcept;
    bool ensure_measure_dc() noexcept;
    void sweep_layouts() noexcept;
    bool ensure_solid_asset() noexcept;

    std::uint64_t font_generation_ = 0;
    std::uint64_t next_page_identity_ = 1;
    std::uint64_t frame_token_ = 0;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::vector<CharacterPaneGlyphQuad> quads_;
    std::map<GlyphKey, GlyphMask> glyphs_;
    std::vector<AtlasPage> pages_;
    std::map<LayoutKey, TextLayout> layouts_;
    std::uint64_t layout_sweep_ = 1;
    std::uint64_t layout_sweep_ms_ = 0;
    std::shared_ptr<const CharacterPaneGlyphAsset> solid_asset_;
    std::size_t glyph_bytes_ = 0;
    HDC measure_dc_ = nullptr;
    HBITMAP measure_bitmap_ = nullptr;
    HGDIOBJ measure_old_bitmap_ = nullptr;
    void* measure_bits_ = nullptr;
    CharacterPaneTextFailure failure_ = CharacterPaneTextFailure::none;
    bool frame_claimed_ = false;
    bool dc_finished_ = false;
#ifdef EID3D11_TESTING
    std::size_t frame_shape_calls_ = 0;
#endif
};

// Frames of text surfaces drawn once and presented on later frames (chat
// lines). Keyed by the proxy surface identity; each entry also records the
// surface content generation at publication so any later non-owned write to
// that surface makes the frame stale. Publishing the same surface again
// replaces the whole frame, which is the producer's clear-then-redraw. The
// surface's own destruction and font retirement retire entries.
class RetainedOwnedTextFrames {
public:
    enum class Lookup : std::uint8_t { absent, stale, ready };

    bool publish(std::uint64_t surface_id, std::uint64_t generation,
                 CharacterPaneTextFrame&& frame) noexcept;
    Lookup find(std::uint64_t surface_id, std::uint64_t generation,
                const CharacterPaneTextFrame*& frame) noexcept;
    void retire(std::uint64_t surface_id) noexcept;
    void clear() noexcept { entries_.clear(); }
    std::size_t size() const noexcept { return entries_.size(); }

private:
    struct Entry {
        std::uint64_t generation = 0;
        CharacterPaneTextFrame frame;
    };
    std::map<std::uint64_t, Entry> entries_;
};

} // namespace owned
