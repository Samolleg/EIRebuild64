#pragma once
#include <Windows.h>

// Retail MFC (game.exe 0x70BC60): integer division, not modern MFC's MulDiv.
// Keep the same default LOGFONT and screen DC mapping for all interface fonts.
namespace InterfaceFont {
inline BOOL CreatePointFont(CFont& font, int decipoints, const char* face)
{
    HDC dc = ::GetDC(nullptr);
    if (!dc) return FALSE;
    LOGFONTA data{};
    data.lfCharSet = DEFAULT_CHARSET;
    lstrcpynA(data.lfFaceName, face, LF_FACESIZE);
    POINT point{0, GetDeviceCaps(dc, LOGPIXELSY) * decipoints / 720};
    POINT origin{0, 0};
    const BOOL mapped = DPtoLP(dc, &point, 1) && DPtoLP(dc, &origin, 1);
    ::ReleaseDC(nullptr, dc);
    if (!mapped) return FALSE;
    data.lfHeight = -abs(point.y - origin.y);
    HFONT created = CreateFontIndirectA(&data);
    if (!created) return FALSE;
    if (!font.Attach(created)) { DeleteObject(created); return FALSE; }
    return TRUE;
}
}
