#include "character_pane_owned_text.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>
#include <new>
#include <tuple>

namespace owned {
namespace {

constexpr std::size_t kMaximumPanePixels = 4u * 1024u * 1024u;
constexpr std::size_t kMaximumAtlasBytes = 8u * 1024u * 1024u;
constexpr std::size_t kMaximumGlyphs = 4096u;
constexpr std::size_t kMaximumFrameQuads = 16384u;
constexpr std::uint32_t kDefaultAtlasSide = 256u;
constexpr std::uint32_t kMaximumAtlasSide = 2048u;
constexpr std::uint32_t kAtlasGutter = 1u;
constexpr UINT kSupportedFlags = DT_LEFT | DT_CENTER | DT_RIGHT |
    DT_WORDBREAK | DT_SINGLELINE | DT_NOCLIP | DT_NOPREFIX;
constexpr std::uint64_t kAtlasIdentityBase = 0x5458544100000000ull;
constexpr std::uint64_t kSolidIdentity = 0x5458544c494e4501ull;
// A layout survives while it is drawn at least once per sweep interval.
constexpr std::uint64_t kLayoutSweepMs = 1000u;
// Raster scales are keyed in 1/65536 steps; this key is the stock size.
constexpr std::uint32_t kUnitRasterKey = 65536u;

std::uint8_t expand5(std::uint32_t value) noexcept {
    return static_cast<std::uint8_t>((value << 3u) | (value >> 2u));
}

std::uint32_t quantized_color(COLORREF color) noexcept {
    const std::uint32_t red = GetRValue(color) >> 3u;
    const std::uint32_t green = GetGValue(color) >> 3u;
    const std::uint32_t blue = GetBValue(color) >> 3u;
    if ((red | green | blue) == 0u) return 0u;
    return 0xff000000u |
        (static_cast<std::uint32_t>(expand5(red)) << 16u) |
        (static_cast<std::uint32_t>(expand5(green)) << 8u) |
        expand5(blue);
}

bool measured_draw_text(HDC dc, const char* text, RECT& rectangle,
                        UINT format, int& height) noexcept {
    height = 0;
    __try {
        height = DrawTextA(dc, text, -1, &rectangle, format);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        height = 0;
        return false;
    }
}

bool guarded_text_length(const char* text, std::size_t& length) noexcept {
    length = 0;
    if (!text) return false;
    __try {
        length = std::strlen(text);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        length = 0;
        return false;
    }
}

bool acp_size(const char* text, int& needed) noexcept {
    needed = 0;
    __try {
        needed = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
        return needed > 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        needed = 0;
        return false;
    }
}

bool acp_convert(const char* text, wchar_t* output, int count) noexcept {
    __try {
        return MultiByteToWideChar(
                   CP_ACP, 0, text, -1, output, count) > 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

} // namespace

const char* character_pane_text_failure_name(
    CharacterPaneTextFailure failure) noexcept {
    switch (failure) {
    case CharacterPaneTextFailure::none: return "none";
    case CharacterPaneTextFailure::dimensions: return "dimensions";
    case CharacterPaneTextFailure::allocation: return "allocation";
    case CharacterPaneTextFailure::font: return "font";
    case CharacterPaneTextFailure::encoding: return "encoding";
    case CharacterPaneTextFailure::flags: return "flags";
    case CharacterPaneTextFailure::layout: return "layout";
    case CharacterPaneTextFailure::glyph: return "glyph";
    case CharacterPaneTextFailure::dc_transaction: return "dc_transaction";
    case CharacterPaneTextFailure::submission: return "submission";
    }
    return "unknown";
}

bool CharacterPaneOwnedText::GlyphKey::operator<(
    const GlyphKey& other) const noexcept {
    return std::tie(font_generation, font_slot, raster_key, glyph) <
        std::tie(other.font_generation, other.font_slot, other.raster_key,
                 other.glyph);
}

bool CharacterPaneOwnedText::LayoutKey::operator<(
    const LayoutKey& other) const noexcept {
    return std::tie(font_generation, font_slot, format, width, height, text) <
        std::tie(other.font_generation, other.font_slot, other.format,
                 other.width, other.height, other.text);
}

CharacterPaneOwnedText::~CharacterPaneOwnedText() {
    release_raster_fonts();
    if (measure_dc_ && measure_old_bitmap_)
        SelectObject(measure_dc_, measure_old_bitmap_);
    if (measure_bitmap_) DeleteObject(measure_bitmap_);
    if (measure_dc_) DeleteDC(measure_dc_);
}

void CharacterPaneOwnedText::initialize_fonts(
    std::uint64_t generation) noexcept {
    if (!generation || generation == font_generation_) return;
    font_generation_ = generation;
    release_raster_fonts();
    glyphs_.clear();
    pages_.clear();
    layouts_.clear();
    next_page_identity_ = 1;
    glyph_bytes_ = 0;
    abort_frame();
}

void CharacterPaneOwnedText::retire_fonts() noexcept {
    font_generation_ = 0;
    release_raster_fonts();
    glyphs_.clear();
    pages_.clear();
    layouts_.clear();
    next_page_identity_ = 1;
    solid_asset_.reset();
    glyph_bytes_ = 0;
    abort_frame();
}

bool CharacterPaneOwnedText::ensure_measure_dc() noexcept {
    if (measure_dc_ && measure_bitmap_ && measure_bits_) return true;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = 1;
    info.bmiHeader.biHeight = -1;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return false;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(
        dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap || !bits) {
        if (bitmap) DeleteObject(bitmap);
        DeleteDC(dc);
        return false;
    }
    HGDIOBJ old = SelectObject(dc, bitmap);
    if (!old || old == HGDI_ERROR) {
        DeleteObject(bitmap);
        DeleteDC(dc);
        return false;
    }
    measure_dc_ = dc;
    measure_bitmap_ = bitmap;
    measure_old_bitmap_ = old;
    measure_bits_ = bits;
    return true;
}

void CharacterPaneOwnedText::sweep_layouts() noexcept {
    const std::uint64_t now = GetTickCount64();
    if (now - layout_sweep_ms_ < kLayoutSweepMs) return;
    for (auto entry = layouts_.begin(); entry != layouts_.end();)
        entry = entry->second.used_sweep == layout_sweep_
            ? std::next(entry) : layouts_.erase(entry);
    ++layout_sweep_;
    layout_sweep_ms_ = now;
}

void CharacterPaneOwnedText::release_raster_fonts() noexcept {
    for (const auto& entry : raster_fonts_) DeleteObject(entry.second);
    raster_fonts_.clear();
}

// A copy of the engine font at raster_scale_. Glyph indices come from the
// stock font shaping and are the same in the copy (same face).
HFONT CharacterPaneOwnedText::raster_font(
    HFONT font, std::uint32_t font_slot) noexcept {
    if (raster_key_ == kUnitRasterKey) return font;
    const auto key = std::make_pair(font_slot, raster_key_);
    const auto found = raster_fonts_.find(key);
    if (found != raster_fonts_.end()) return found->second;
    LOGFONTW description{};
    if (GetObjectW(font, sizeof(description), &description) !=
        sizeof(description))
        return nullptr;
    if (!description.lfHeight) {
        // Default size: scale the stock cell height (selected in the DC).
        TEXTMETRICW metrics{};
        if (!GetTextMetricsW(measure_dc_, &metrics)) return nullptr;
        description.lfHeight = metrics.tmHeight;
    }
    const auto scaled = [this](LONG value) {
        const LONG result = static_cast<LONG>(
            std::lround(static_cast<double>(value) * raster_scale_));
        return result ? result : (value < 0 ? -1 : 1);
    };
    description.lfHeight = scaled(description.lfHeight);
    if (description.lfWidth) description.lfWidth = scaled(description.lfWidth);
    HFONT created = CreateFontIndirectW(&description);
    if (!created) return nullptr;
    try {
        raster_fonts_.emplace(key, created);
    } catch (const std::bad_alloc&) {
        DeleteObject(created);
        return nullptr;
    }
    return created;
}

bool CharacterPaneOwnedText::begin_frame(
    std::uint32_t width, std::uint32_t height, float raster_scale) noexcept {
    abort_frame();
    sweep_layouts();
    // Bounded by the UI scale range and supported output/canvas ratios.
    if (!std::isfinite(raster_scale) || raster_scale < 0.25f ||
        raster_scale > 8.0f || std::fabs(raster_scale - 1.0f) < 1e-4f)
        raster_scale = 1.0f;
    raster_scale_ = raster_scale;
    raster_key_ = static_cast<std::uint32_t>(
        std::lround(static_cast<double>(raster_scale) * kUnitRasterKey));
    if (raster_key_ == kUnitRasterKey) raster_scale_ = 1.0f;
    if (!font_generation_ || !width || !height ||
        width > (std::numeric_limits<std::size_t>::max)() / height ||
        static_cast<std::size_t>(width) * height > kMaximumPanePixels) {
        failure_ = CharacterPaneTextFailure::dimensions;
        return false;
    }
    if (!ensure_measure_dc()) {
        failure_ = CharacterPaneTextFailure::font;
        return false;
    }
    width_ = width;
    height_ = height;
    if (++frame_token_ == 0) ++frame_token_;
    failure_ = CharacterPaneTextFailure::none;
    frame_claimed_ = true;
    dc_finished_ = false;
#ifdef EID3D11_TESTING
    frame_shape_calls_ = 0;
#endif
    return true;
}

bool CharacterPaneOwnedText::parse_text(
    const char* text, UINT format, DisplayText& output) noexcept {
    output = {};
    if (!text) return false;
    int needed = 0;
    if (!acp_size(text, needed)) return false;
    try {
        std::wstring source(static_cast<std::size_t>(needed), L'\0');
        if (!acp_convert(text, source.data(), needed)) return false;
        source.resize(static_cast<std::size_t>(needed - 1));
        if (format & DT_SINGLELINE)
            for (auto& character : source)
                if (character == L'\r' || character == L'\n')
                    character = L' ';
        if (format & DT_NOPREFIX) {
            output.text = std::move(source);
            return true;
        }
        output.text.reserve(source.size());
        for (std::size_t index = 0; index < source.size(); ++index) {
            if (source[index] != L'&') {
                output.text.push_back(source[index]);
                continue;
            }
            if (index + 1u >= source.size()) continue;
            if (source[index + 1u] == L'&') {
                output.text.push_back(L'&');
                ++index;
            } else {
                output.underlined.push_back(output.text.size());
            }
        }
        return true;
    } catch (const std::bad_alloc&) {
        return false;
    }
}

bool CharacterPaneOwnedText::shape_run(
    HDC dc, const wchar_t* text, std::size_t length,
    ShapedRun& output) noexcept {
    output = {};
    if (!length) return true;
    if (!dc || !text || length > static_cast<std::size_t>(INT_MAX) ||
        length > ((std::numeric_limits<std::size_t>::max)() - 8u) / 3u)
        return false;
    try {
        GCP_RESULTSW results{};
        results.lStructSize = sizeof(results);
        const std::size_t capacity = length * 3u + 8u;
        if (capacity > UINT_MAX) return false;
        results.nGlyphs = static_cast<UINT>(capacity);
        std::vector<wchar_t> glyphs(capacity);
        std::vector<int> advances(capacity);
        std::vector<UINT> order(capacity, UINT_MAX);
        std::vector<int> carets(capacity, INT_MIN);
        results.lpGlyphs = glyphs.data();
        results.lpDx = advances.data();
        results.lpOrder = order.data();
        results.lpCaretPos = carets.data();
#ifdef EID3D11_TESTING
        ++frame_shape_calls_;
#endif
        const DWORD placed = GetCharacterPlacementW(
            dc, text, static_cast<int>(length), 0, &results,
            GCP_GLYPHSHAPE | GCP_USEKERNING | GCP_REORDER);
        if (!placed || !results.nGlyphs || results.nGlyphs > capacity ||
            results.lpGlyphs != glyphs.data() ||
            results.lpDx != advances.data() ||
            results.lpOrder != order.data() ||
            results.lpCaretPos != carets.data())
            return false;
        std::int64_t width = 0;
        for (UINT index = 0; index < results.nGlyphs; ++index) {
            width += advances[index];
            if (width < INT_MIN || width > INT_MAX) return false;
        }
        if (width < 0 || width > INT_MAX) return false;
        for (std::size_t index = 0; index < length; ++index) {
            if (order[index] >= results.nGlyphs || carets[index] == INT_MIN ||
                carets[index] < 0 || carets[index] > width)
                return false;
        }
        output.glyphs.reserve(results.nGlyphs);
        for (UINT index = 0; index < results.nGlyphs; ++index)
            output.glyphs.push_back(
                static_cast<std::uint16_t>(glyphs[index]));
        output.advances.assign(
            advances.begin(), advances.begin() + results.nGlyphs);
        output.order.assign(order.begin(), order.begin() + length);
        output.carets.assign(carets.begin(), carets.begin() + length);
        output.width = static_cast<int>(width);
        return true;
    } catch (const std::bad_alloc&) {
        output = {};
        return false;
    }
}

#ifdef EID3D11_TESTING
bool CharacterPaneOwnedText::test_shape_wide(
    HFONT font, const wchar_t* text, std::size_t length,
    CharacterPaneShapeTestResult& result) noexcept {
    result = {};
    if (!font || font == HGDI_ERROR || !ensure_measure_dc()) return false;
    const HGDIOBJ old_font = SelectObject(measure_dc_, font);
    if (!old_font || old_font == HGDI_ERROR) return false;
    ShapedRun shaped{};
    const bool complete = shape_run(measure_dc_, text, length, shaped);
    SelectObject(measure_dc_, old_font);
    if (!complete) return false;
    result.source_characters = length;
    result.glyphs = shaped.glyphs.size();
    result.width = shaped.width;
    result.non_one_to_one = shaped.glyphs.size() != length;
    for (std::size_t index = 0; index < shaped.order.size(); ++index) {
        if (shaped.order[index] != index) result.reordered = true;
        for (std::size_t previous = 0; previous < index; ++previous)
            if (shaped.order[previous] == shaped.order[index])
                result.non_one_to_one = true;
    }
    return true;
}
#endif

bool CharacterPaneOwnedText::layout(
    HDC dc, const DisplayText& display, int maximum_width,
    bool word_break, bool single_line, std::vector<Line>& lines) noexcept {
    lines.clear();
    try {
        const auto shaped_line = [&](std::size_t start, std::size_t length,
                                     Line& line) {
            line = {};
            line.start = start;
            line.length = length;
            if (!shape_run(dc, display.text.data() + start,
                           length, line.shaped))
                return false;
            line.width = line.shaped.width;
            return true;
        };
        if (single_line) {
            Line line{};
            if (!shaped_line(0u, display.text.size(), line)) return false;
            lines.push_back(std::move(line));
            return true;
        }
        std::size_t paragraph_begin = 0;
        while (paragraph_begin <= display.text.size()) {
            std::size_t paragraph_end =
                display.text.find(L'\n', paragraph_begin);
            if (paragraph_end == std::wstring::npos)
                paragraph_end = display.text.size();
            if (paragraph_end > paragraph_begin &&
                display.text[paragraph_end - 1u] == L'\r')
                --paragraph_end;
            if (!word_break) {
                Line line{};
                if (!shaped_line(
                        paragraph_begin, paragraph_end - paragraph_begin,
                        line))
                    return false;
                lines.push_back(std::move(line));
            } else if (paragraph_begin == paragraph_end) {
                Line line{};
                line.start = paragraph_begin;
                lines.push_back(std::move(line));
            } else {
                const std::size_t first_line = lines.size();
                std::size_t cursor = paragraph_begin;
                while (cursor < paragraph_end) {
                    while (cursor < paragraph_end &&
                           display.text[cursor] == L' ')
                        ++cursor;
                    if (cursor == paragraph_end) break;
                    std::size_t best = cursor;
                    std::size_t last_space = std::wstring::npos;
                    ShapedRun best_run{};
                    for (std::size_t trial = cursor + 1u;
                         trial <= paragraph_end; ++trial) {
                        if (display.text[trial - 1u] == L' ')
                            last_space = trial - 1u;
                        ShapedRun trial_run{};
                        if (!shape_run(
                            dc, display.text.data() + cursor,
                            trial - cursor, trial_run))
                            return false;
                        if (trial_run.width <= std::max(maximum_width, 1) ||
                            best == cursor) {
                            best = trial;
                            best_run = std::move(trial_run);
                            continue;
                        }
                        break;
                    }
                    bool reshape = false;
                    if (best < paragraph_end &&
                        last_space != std::wstring::npos &&
                        last_space > cursor) {
                        best = last_space;
                        reshape = true;
                    }
                    if (best == cursor) {
                        ++best;
                        reshape = true;
                    }
                    const std::size_t untrimmed_best = best;
                    while (best > cursor &&
                           display.text[best - 1u] == L' ')
                        --best;
                    reshape = reshape || best != untrimmed_best;
                    if (reshape && !shape_run(
                            dc, display.text.data() + cursor,
                            best - cursor, best_run))
                        return false;
                    Line line{};
                    line.start = cursor;
                    line.length = best - cursor;
                    line.width = best_run.width;
                    line.shaped = std::move(best_run);
                    lines.push_back(std::move(line));
                    cursor = best;
                }
                if (lines.size() == first_line) {
                    Line line{};
                    line.start = paragraph_begin;
                    lines.push_back(std::move(line));
                }
            }
            if (paragraph_end == display.text.size()) break;
            paragraph_begin = paragraph_end + 1u;
        }
        return true;
    } catch (const std::bad_alloc&) {
        return false;
    }
}

bool CharacterPaneOwnedText::allocate_snapshot(
    std::uint64_t identity, std::uint64_t generation,
    std::uint64_t revision, std::uint32_t width,
    std::uint32_t height,
    std::shared_ptr<CharacterPaneGlyphAsset>& asset) noexcept {
    asset.reset();
    if (!identity || !generation || !width || !height ||
        width > (std::numeric_limits<std::size_t>::max)() / height)
        return false;
    const std::size_t bytes = static_cast<std::size_t>(width) * height;
    try {
        auto created = std::make_shared<CharacterPaneGlyphAsset>();
        created->identity = identity;
        created->generation = generation;
        created->revision = revision;
        created->width = width;
        created->height = height;
        if (bytes > created->coverage.max_size()) return false;
        created->coverage.resize(bytes);
        asset = std::move(created);
        return true;
    } catch (const std::bad_alloc&) {
        asset.reset();
        return false;
    }
}

bool CharacterPaneOwnedText::allocate_page(
    std::uint32_t font_slot, std::uint32_t minimum_width,
    std::uint32_t minimum_height, std::size_t& page_index) noexcept {
    page_index = static_cast<std::size_t>(-1);
    if (minimum_width > UINT_MAX - 2u * kAtlasGutter ||
        minimum_height > UINT_MAX - 2u * kAtlasGutter)
        return false;
    const std::uint32_t required = std::max(
        minimum_width + 2u * kAtlasGutter,
        minimum_height + 2u * kAtlasGutter);
    std::uint32_t side = kDefaultAtlasSide;
    while (side < required && side < kMaximumAtlasSide) side *= 2u;
    if (side < required || side > kMaximumAtlasSide ||
        side > (std::numeric_limits<std::size_t>::max)() / side)
        return false;
    const std::size_t bytes = static_cast<std::size_t>(side) * side;
    if (bytes > kMaximumAtlasBytes ||
        glyph_bytes_ > kMaximumAtlasBytes - bytes)
        return false;
    AtlasPage page{};
    page.font_slot = font_slot;
    const std::uint64_t identity = kAtlasIdentityBase + next_page_identity_;
    if (!identity || !allocate_snapshot(
            identity, font_generation_, 0u, side, side, page.asset))
        return false;
    try {
        pages_.push_back(std::move(page));
    } catch (const std::bad_alloc&) {
        return false;
    }
    ++next_page_identity_;
    glyph_bytes_ += bytes;
    page_index = pages_.size() - 1u;
    return true;
}

bool CharacterPaneOwnedText::make_page_mutable(
    std::size_t page_index) noexcept {
    if (page_index >= pages_.size() || !pages_[page_index].asset)
        return false;
    AtlasPage& page = pages_[page_index];
    if (page.modified_frame == frame_token_) return true;
    if (page.asset->revision ==
        (std::numeric_limits<std::uint64_t>::max)())
        return false;
    if (page.asset.use_count() > 1) {
        std::shared_ptr<CharacterPaneGlyphAsset> copy;
        if (!allocate_snapshot(
                page.asset->identity, page.asset->generation,
                page.asset->revision + 1u, page.asset->width,
                page.asset->height, copy))
            return false;
        std::copy(page.asset->coverage.begin(), page.asset->coverage.end(),
                  copy->coverage.begin());
        page.asset = std::move(copy);
    } else {
        ++page.asset->revision;
    }
    page.modified_frame = frame_token_;
    return true;
}

bool CharacterPaneOwnedText::place_glyph(
    std::uint32_t font_slot, std::uint32_t width,
    std::uint32_t height, const std::uint8_t* coverage,
    GlyphMask& mask) noexcept {
    if (!width || !height) return true;
    if (!coverage || width > kMaximumAtlasSide - 2u * kAtlasGutter ||
        height > kMaximumAtlasSide - 2u * kAtlasGutter)
        return false;
    const auto find_position = [=](const AtlasPage& page,
                                   std::uint32_t& x,
                                   std::uint32_t& y,
                                   std::uint32_t& row_height) {
        if (!page.asset || page.font_slot != font_slot) return false;
        x = page.next_x;
        y = page.next_y;
        row_height = page.row_height;
        if (x > page.asset->width ||
            width + kAtlasGutter > page.asset->width - x) {
            x = kAtlasGutter;
            if (row_height > UINT_MAX - y - kAtlasGutter) return false;
            y += row_height + kAtlasGutter;
            row_height = 0;
        }
        return x <= page.asset->width && y <= page.asset->height &&
            width + kAtlasGutter <= page.asset->width - x &&
            height + kAtlasGutter <= page.asset->height - y;
    };

    std::size_t page_index = static_cast<std::size_t>(-1);
    std::uint32_t x = 0, y = 0, row_height = 0;
    for (std::size_t index = 0; index < pages_.size(); ++index) {
        if (find_position(pages_[index], x, y, row_height)) {
            page_index = index;
            break;
        }
    }
    if (page_index == static_cast<std::size_t>(-1)) {
        if (!allocate_page(font_slot, width, height, page_index) ||
            !find_position(pages_[page_index], x, y, row_height))
            return false;
    }
    if (!make_page_mutable(page_index)) return false;
    AtlasPage& page = pages_[page_index];
    for (std::uint32_t row = 0; row < height; ++row)
        std::copy_n(
            coverage + static_cast<std::size_t>(row) * width, width,
            page.asset->coverage.begin() +
                static_cast<std::size_t>(y + row) * page.asset->width + x);
    page.next_x = x + width + kAtlasGutter;
    page.next_y = y;
    page.row_height = std::max(row_height, height);
    mask.page_index = page_index;
    mask.source = {
        static_cast<LONG>(x), static_cast<LONG>(y),
        static_cast<LONG>(x + width), static_cast<LONG>(y + height)};
    return true;
}

bool CharacterPaneOwnedText::realize_glyph(
    HDC dc, std::uint32_t font_slot, std::uint16_t glyph,
    const GlyphMask*& output) noexcept {
    output = nullptr;
    const GlyphKey key{font_generation_, font_slot, raster_key_, glyph};
    auto found = glyphs_.find(key);
    if (found != glyphs_.end()) {
        output = &found->second;
        return true;
    }
    if (glyphs_.size() >= kMaximumGlyphs) return false;
    MAT2 transform{};
    transform.eM11.value = 1;
    transform.eM22.value = 1;
    GLYPHMETRICS metrics{};
    const DWORD byte_count = GetGlyphOutlineW(
        dc, glyph, GGO_GRAY8_BITMAP | GGO_GLYPH_INDEX,
        &metrics, 0, nullptr, &transform);
    if (byte_count == GDI_ERROR || byte_count > kMaximumAtlasBytes)
        return false;
    const std::size_t width = metrics.gmBlackBoxX;
    const std::size_t height = metrics.gmBlackBoxY;
    if (height && width > (std::numeric_limits<std::size_t>::max)() / height)
        return false;
    const std::size_t output_bytes = width * height;
    if (output_bytes > kMaximumAtlasBytes || width > INT_MAX ||
        height > INT_MAX)
        return false;
    try {
        std::vector<std::uint8_t> encoded(byte_count);
        if (byte_count && GetGlyphOutlineW(
                dc, glyph, GGO_GRAY8_BITMAP | GGO_GLYPH_INDEX,
                &metrics, byte_count, encoded.data(), &transform) == GDI_ERROR)
            return false;
        std::vector<std::uint8_t> coverage(output_bytes);
        if (output_bytes && byte_count) {
            const std::size_t pitch = (width + 3u) & ~std::size_t(3u);
            if (pitch > byte_count || height > byte_count / pitch)
                return false;
            for (std::size_t y = 0; y < height; ++y)
                for (std::size_t x = 0; x < width; ++x)
                    coverage[y * width + x] = static_cast<std::uint8_t>(
                        (static_cast<unsigned>(encoded[y * pitch + x]) *
                             255u + 32u) /
                        64u);
        }
        GlyphMask mask{};
        mask.left = metrics.gmptGlyphOrigin.x;
        mask.top = metrics.gmptGlyphOrigin.y;
        mask.width = static_cast<int>(width);
        mask.height = static_cast<int>(height);
        if (!place_glyph(
                font_slot, static_cast<std::uint32_t>(width),
                static_cast<std::uint32_t>(height), coverage.data(), mask))
            return false;
        auto inserted = glyphs_.emplace(key, std::move(mask));
        if (!inserted.second) return false;
        output = &inserted.first->second;
        return true;
    } catch (const std::bad_alloc&) {
        return false;
    }
}

bool CharacterPaneOwnedText::append_glyph(
    const GlyphMask& glyph, int pen_x, int baseline_y, int shift,
    COLORREF color, const RECT& clip) noexcept {
    if (glyph.width <= 0 || glyph.height <= 0) return true;
    if (glyph.page_index >= pages_.size() || !pages_[glyph.page_index].asset)
        return false;
    if (raster_key_ != kUnitRasterKey) {
        // Output-size glyph: the renderer places it 1:1 at the snapped output
        // position of the stock pen, so its shape is the same everywhere.
        const double scale = raster_scale_;
        const double left = pen_x + shift + glyph.left / scale;
        const double top = baseline_y + shift - glyph.top / scale;
        const RECT bound{
            std::max<LONG>(clip.left, 0), std::max<LONG>(clip.top, 0),
            std::min<LONG>(clip.right, static_cast<LONG>(width_)),
            std::min<LONG>(clip.bottom, static_cast<LONG>(height_))};
        const RECT bounded{
            std::max<LONG>(static_cast<LONG>(std::floor(left)), bound.left),
            std::max<LONG>(static_cast<LONG>(std::floor(top)), bound.top),
            std::min<LONG>(static_cast<LONG>(
                std::ceil(left + glyph.width / scale)), bound.right),
            std::min<LONG>(static_cast<LONG>(
                std::ceil(top + glyph.height / scale)), bound.bottom)};
        if (bounded.right <= bounded.left || bounded.bottom <= bounded.top)
            return true;
        if (quads_.size() >= kMaximumFrameQuads) return false;
        CharacterPaneGlyphQuad quad{};
        quad.atlas_page_index = glyph.page_index;
        quad.source = glyph.source;
        quad.destination = bounded;
        quad.color = quantized_color(color);
        quad.raster_scale = raster_scale_;
        quad.anchor = {pen_x, baseline_y};
        quad.shift = {shift, shift};
        quad.offset = {glyph.left, -glyph.top};
        quad.clip = bound;
        try {
            quads_.push_back(std::move(quad));
        } catch (const std::bad_alloc&) {
            return false;
        }
        return true;
    }
    pen_x += shift;
    baseline_y += shift;
    const int glyph_left = pen_x + glyph.left;
    const int glyph_top = baseline_y - glyph.top;
    RECT destination{
        glyph_left, glyph_top,
        glyph_left + glyph.width, glyph_top + glyph.height};
    RECT bounded{
        std::max<LONG>({destination.left, clip.left, 0}),
        std::max<LONG>({destination.top, clip.top, 0}),
        std::min<LONG>({destination.right, clip.right,
                        static_cast<LONG>(width_)}),
        std::min<LONG>({destination.bottom, clip.bottom,
                        static_cast<LONG>(height_)})};
    if (bounded.right <= bounded.left || bounded.bottom <= bounded.top)
        return true;
    if (quads_.size() >= kMaximumFrameQuads) return false;
    CharacterPaneGlyphQuad quad{};
    quad.atlas_page_index = glyph.page_index;
    quad.source = {
        glyph.source.left + bounded.left - destination.left,
        glyph.source.top + bounded.top - destination.top,
        glyph.source.left + bounded.right - destination.left,
        glyph.source.top + bounded.bottom - destination.top};
    quad.destination = bounded;
    quad.color = quantized_color(color);
    try {
        quads_.push_back(std::move(quad));
    } catch (const std::bad_alloc&) {
        return false;
    }
    return true;
}

bool CharacterPaneOwnedText::ensure_solid_asset() noexcept {
    if (solid_asset_) return true;
    try {
        auto asset = std::make_shared<CharacterPaneGlyphAsset>();
        asset->identity = kSolidIdentity;
        asset->generation = 1u;
        asset->revision = 1u;
        asset->width = 1u;
        asset->height = 1u;
        asset->coverage.push_back(255u);
        solid_asset_ = std::move(asset);
        return true;
    } catch (const std::bad_alloc&) {
        return false;
    }
}

bool CharacterPaneOwnedText::append_line(
    int left, int right, int y, COLORREF color,
    const RECT& clip) noexcept {
    if (y < clip.top || y >= clip.bottom || y < 0 ||
        y >= static_cast<int>(height_))
        return true;
    left = std::max({left, static_cast<int>(clip.left), 0});
    right = std::min(
        {right, static_cast<int>(clip.right), static_cast<int>(width_)});
    if (right <= left) return true;
    if (!ensure_solid_asset() || quads_.size() >= kMaximumFrameQuads)
        return false;
    CharacterPaneGlyphQuad quad{};
    quad.asset = solid_asset_;
    quad.source = {0, 0, 1, 1};
    quad.destination = {left, y, right, y + 1};
    quad.color = quantized_color(color);
    try {
        quads_.push_back(std::move(quad));
    } catch (const std::bad_alloc&) {
        return false;
    }
    return true;
}

bool CharacterPaneOwnedText::draw_line(
    HDC dc, const DisplayText& display, const Line& line,
    int x, int baseline, int shift, COLORREF color, const RECT& clip,
    std::uint32_t font_slot, int underline_offset,
    int& underlines) noexcept {
    if (!line.length) return true;
    try {
        const ShapedRun& shaped = line.shaped;
        if (shaped.glyphs.size() != shaped.advances.size() ||
            shaped.order.size() != line.length ||
            shaped.carets.size() != line.length)
            return false;
        int pen = x;
        for (std::size_t index = 0; index < shaped.glyphs.size(); ++index) {
            const GlyphMask* glyph = nullptr;
            if (!realize_glyph(dc, font_slot,
                               shaped.glyphs[index],
                               glyph) || !glyph)
                return false;
            if (!append_glyph(*glyph, pen, baseline, shift, color, clip))
                return false;
            if ((shaped.advances[index] > 0 &&
                 pen > INT_MAX - shaped.advances[index]) ||
                (shaped.advances[index] < 0 &&
                 pen < INT_MIN - shaped.advances[index]))
                return false;
            pen += shaped.advances[index];
        }
        for (const std::size_t underline : display.underlined) {
            if (underline < line.start ||
                underline >= line.start + line.length)
                continue;
            const std::size_t local = underline - line.start;
            const std::size_t glyph_index = shaped.order[local];
            if (glyph_index >= shaped.advances.size()) return false;
            std::int64_t glyph_origin = 0;
            for (std::size_t index = 0; index < glyph_index; ++index)
                glyph_origin += shaped.advances[index];
            const std::int64_t glyph_end =
                glyph_origin + shaped.advances[glyph_index];
            const std::int64_t left =
                static_cast<std::int64_t>(x) + shift +
                std::min(glyph_origin, glyph_end);
            const std::int64_t right =
                static_cast<std::int64_t>(x) + shift +
                std::max(glyph_origin, glyph_end);
            if (left < INT_MIN || left > INT_MAX ||
                right < INT_MIN || right > INT_MAX)
                return false;
            const std::int64_t underline_right =
                right > left ? right : left + 1;
            if (underline_right > INT_MAX || !append_line(
                    static_cast<int>(left),
                    static_cast<int>(underline_right),
                    baseline + shift + underline_offset, color, clip))
                return false;
            ++underlines;
        }
        return true;
    } catch (const std::bad_alloc&) {
        return false;
    }
}

int CharacterPaneOwnedText::draw(
    HFONT font, std::uint32_t font_slot, const char* text,
    const RECT& rectangle, UINT format, COLORREF color,
    bool shadow, bool& height_valid) noexcept {
    height_valid = false;
    if (!frame_claimed_ || dc_finished_) return 0;
    if (!font || font == HGDI_ERROR) {
        fail(CharacterPaneTextFailure::font);
        return 0;
    }
    std::size_t length = 0;
    if (!guarded_text_length(text, length)) {
        fail(CharacterPaneTextFailure::encoding);
        return 0;
    }
    // The glyph cache already names a font by generation and slot. Only a
    // completed supported-format layout is reused; color and shadow are
    // applied when quads are emitted.
    LayoutKey key{};
    bool keyed = false;
    const TextLayout* shaped = nullptr;
    if (!(format & ~kSupportedFlags)) {
        try {
            key.font_generation = font_generation_;
            key.font_slot = font_slot;
            key.format = format;
            key.width = rectangle.right - rectangle.left;
            key.height = rectangle.bottom - rectangle.top;
            key.text.assign(text, length);
            keyed = true;
            const auto found = layouts_.find(key);
            if (found != layouts_.end()) {
                found->second.used_sweep = layout_sweep_;
                shaped = &found->second;
            }
        } catch (const std::bad_alloc&) {
            keyed = false;
        }
    }
    TextLayout fresh{};
    if (!shaped && !parse_text(text, format, fresh.display)) {
        fail(CharacterPaneTextFailure::encoding);
        return 0;
    }
    const HGDIOBJ old_font = SelectObject(measure_dc_, font);
    if (!old_font || old_font == HGDI_ERROR) {
        fail(CharacterPaneTextFailure::font);
        return 0;
    }
    SetBkMode(measure_dc_, TRANSPARENT);
    bool complete = true;
    if (shaped) {
        height_valid = true;
    } else {
        RECT measure_rectangle = rectangle;
        height_valid = measured_draw_text(
            measure_dc_, text, measure_rectangle, format, fresh.height);
        complete = height_valid;
        if (complete && (format & ~kSupportedFlags)) {
            fail(CharacterPaneTextFailure::flags);
            complete = false;
        }
        TEXTMETRICA metrics{};
        complete = complete && GetTextMetricsA(measure_dc_, &metrics) != FALSE;
        fresh.line_height = metrics.tmHeight;
        fresh.ascent = metrics.tmAscent;
        complete = complete && layout(
            measure_dc_, fresh.display, rectangle.right - rectangle.left,
            (format & DT_WORDBREAK) != 0 && !(format & DT_SINGLELINE),
            (format & DT_SINGLELINE) != 0, fresh.lines);
        shaped = &fresh;
    }
    const int exact_height = shaped->height;
    if (complete && raster_key_ != kUnitRasterKey) {
        // Layout above used the stock font; glyphs rasterize from its copy.
        const HFONT raster = raster_font(font, font_slot);
        const HGDIOBJ previous = raster ? SelectObject(measure_dc_, raster)
                                        : nullptr;
        if (!previous || previous == HGDI_ERROR) {
            fail(CharacterPaneTextFailure::font);
            complete = false;
        }
    }
    if (complete) {
        const RECT clip = (format & DT_NOCLIP)
            ? RECT{0, 0, static_cast<LONG>(width_),
                   static_cast<LONG>(height_)}
            : rectangle;
        for (std::size_t index = 0; index < shaped->lines.size(); ++index) {
            const Line& line = shaped->lines[index];
            int x = rectangle.left;
            if (format & DT_CENTER)
                x += ((rectangle.right - rectangle.left) - line.width) / 2;
            else if (format & DT_RIGHT)
                x = rectangle.right - line.width;
            const int y = rectangle.top +
                static_cast<int>(index) * shaped->line_height;
            const int baseline = y + shaped->ascent;
            int ignored_underlines = 0;
            if (shadow && !draw_line(
                    measure_dc_, shaped->display, line, x, baseline, 1,
                    RGB(8, 8, 8), clip, font_slot,
                    1, ignored_underlines)) {
                complete = false;
                break;
            }
            int underlines = 0;
            if (!draw_line(
                    measure_dc_, shaped->display, line, x, baseline, 0,
                    color, clip, font_slot, 1, underlines)) {
                complete = false;
                break;
            }
        }
    }
    SelectObject(measure_dc_, old_font);
    if (complete && keyed && shaped == &fresh) {
        fresh.used_sweep = layout_sweep_;
        try {
            layouts_.emplace(std::move(key), std::move(fresh));
        } catch (const std::bad_alloc&) {
        }
    }
    if (!complete && failure_ == CharacterPaneTextFailure::none)
        fail(CharacterPaneTextFailure::glyph);
    return height_valid ? exact_height : 0;
}

void CharacterPaneOwnedText::finish_dc(bool success) noexcept {
    if (!frame_claimed_) return;
    if (success && failure_ == CharacterPaneTextFailure::none) {
        for (auto& quad : quads_) {
            if (quad.atlas_page_index == static_cast<std::size_t>(-1))
                continue;
            if (quad.atlas_page_index >= pages_.size() ||
                !pages_[quad.atlas_page_index].asset) {
                fail(CharacterPaneTextFailure::submission);
                success = false;
                break;
            }
            quad.asset = pages_[quad.atlas_page_index].asset;
        }
    }
    dc_finished_ = true;
    if (!success) fail(CharacterPaneTextFailure::dc_transaction);
}

void CharacterPaneOwnedText::fail(
    CharacterPaneTextFailure failure) noexcept {
    if (failure_ == CharacterPaneTextFailure::none)
        failure_ = failure;
}

bool CharacterPaneOwnedText::take(
    CharacterPaneTextFrame& frame) noexcept {
    frame = {};
    if (!frame_complete()) return false;
    frame.width = width_;
    frame.height = height_;
    frame.quads = std::move(quads_);
    frame_claimed_ = false;
    dc_finished_ = false;
    width_ = 0;
    height_ = 0;
    failure_ = CharacterPaneTextFailure::none;
    return true;
}

void CharacterPaneOwnedText::abort_frame() noexcept {
    quads_.clear();
    width_ = 0;
    height_ = 0;
    failure_ = CharacterPaneTextFailure::none;
    frame_claimed_ = false;
    dc_finished_ = false;
}

bool RetainedOwnedTextFrames::publish(
    std::uint64_t surface_id, std::uint64_t generation,
    CharacterPaneTextFrame&& frame) noexcept {
    if (!surface_id) return false;
    try {
        Entry& entry = entries_[surface_id];
        entry.generation = generation;
        entry.frame = std::move(frame);
        return true;
    } catch (const std::bad_alloc&) {
        entries_.erase(surface_id);
        return false;
    }
}

RetainedOwnedTextFrames::Lookup RetainedOwnedTextFrames::find(
    std::uint64_t surface_id, std::uint64_t generation,
    const CharacterPaneTextFrame*& frame) noexcept {
    frame = nullptr;
    const auto found = entries_.find(surface_id);
    if (found == entries_.end()) return Lookup::absent;
    if (found->second.generation != generation) {
        entries_.erase(found);
        return Lookup::stale;
    }
    frame = &found->second.frame;
    return Lookup::ready;
}

void RetainedOwnedTextFrames::retire(std::uint64_t surface_id) noexcept {
    entries_.erase(surface_id);
}

} // namespace owned
