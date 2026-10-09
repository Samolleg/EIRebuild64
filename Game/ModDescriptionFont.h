#pragma once
#include <Windows.h>

// Same height policy as addon ScopedDescriptionFont, using real MFC ownership.
class ModDescriptionFont {
    CFont font;
    CFont **slot = nullptr;
    CFont *original = nullptr;
public:
    ModDescriptionFont(CFont **target, int screenHeight) {
        if (!target || !*target || screenHeight <= 0) return;
        LOGFONTA data{};
        HFONT handle = (HFONT)(*target)->GetSafeHandle();
        if (!handle || GetObjectA(handle, sizeof(data), &data) != sizeof(data)) return;
        const int magnitude = data.lfHeight < 0 ? -data.lfHeight : data.lfHeight;
        const int delta = (std::max)(1, MulDiv(2, screenHeight, 600));
        if (magnitude <= delta) return;
        data.lfHeight = data.lfHeight < 0 ? -(magnitude - delta) : magnitude - delta;
        data.lfWidth = 0;
        HFONT created = CreateFontIndirectA(&data);
        if (!created) return;
        if (!font.Attach(created)) { DeleteObject(created); return; }
        slot = target; original = *target; *slot = &font;
    }
    ~ModDescriptionFont() { if (slot) *slot = original; }
    ModDescriptionFont(const ModDescriptionFont&) = delete;
    ModDescriptionFont& operator=(const ModDescriptionFont&) = delete;
};
