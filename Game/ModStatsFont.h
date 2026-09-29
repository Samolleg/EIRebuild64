#pragma once
#include <Windows.h>
#include <cstring>

// Use a 14-unit GDI line box (+2); DrawStats retains a 15-unit clip rectangle.
// Row origins remain 12 units apart; GDI leading is not visible glyph ink.
// Cache one font; changing selection or redrawing does not create GDI objects.
class StatsFontCache
{
    LOGFONTA key_{};
    int limit_ = 0;
    HFONT font_ = nullptr;
    int height_ = 0;
public:
    ~StatsFontCache() { if (font_) DeleteObject(font_); }
    HFONT Get(HFONT original, int screenHeight)
    {
        LOGFONTA key{};
        if (!original || screenHeight <= 0 ||
            GetObjectA(original, sizeof(key), &key) != sizeof(key)) return nullptr;
        const int limit = (14 * screenHeight) / 600;
        if (limit < 2) return nullptr;
        if (limit == limit_ && std::memcmp(&key, &key_, sizeof(key)) == 0)
            return font_;
        if (font_) { DeleteObject(font_); font_ = nullptr; }
        key_ = key; limit_ = limit; height_ = 0;
        HDC dc = GetDC(nullptr);
        if (!dc) return nullptr;
        int size = key.lfHeight < 0 ? -key.lfHeight : key.lfHeight;
        if (size > limit) size = limit;
        for (; size > 0; --size) {
            LOGFONTA fitted = key;
            fitted.lfHeight = -size;
            fitted.lfWidth = 0;
            HFONT candidate = CreateFontIndirectA(&fitted);
            if (!candidate) break;
            HGDIOBJ previous = SelectObject(dc, candidate);
            TEXTMETRICA metrics{};
            const bool valid = previous && previous != HGDI_ERROR && GetTextMetricsA(dc, &metrics);
            if (previous && previous != HGDI_ERROR) SelectObject(dc, previous);
            if (valid && metrics.tmHeight < limit) {
                font_ = candidate; height_ = metrics.tmHeight; break;
            }
            DeleteObject(candidate);
        }
        ReleaseDC(nullptr, dc);
        return font_;
    }
    int Height() const { return height_; }
    StatsFontCache() = default;
    StatsFontCache(const StatsFontCache&) = delete;
    StatsFontCache& operator=(const StatsFontCache&) = delete;
};

// Native CFont ownership: attach cached HFONT only for the DrawStats scope.
class ModStatsFont {
 CFont font;CFont**slot=nullptr;CFont*original=nullptr;
public:
 ModStatsFont(CFont**target,int height,StatsFontCache&cache){
  if(!target||!*target)return;HFONT fitted=cache.Get((HFONT)(*target)->GetSafeHandle(),height);
  if(fitted&&font.Attach(fitted)){slot=target;original=*target;*slot=&font;}
 }
 ~ModStatsFont(){if(slot){*slot=original;font.Detach();}}
};
