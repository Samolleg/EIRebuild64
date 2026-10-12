#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "I3DMain.h"
#include "I3DConsole.h"
#include "I3DObject.h"
#include "Console.h"
#include "StringProcessing.h"
#include "shared/Common.h"
/////////////////////////////////////////////////////////////////////////////////////
const int CONSOLE_STACK_SIZE = 30;
const int CONSOLE_BORDER = 8;
const int CONSOLE_LINE_HEIGHT = 15;
const int CONSOLE_SCROLL_WIDTH = 10;
const int CONSOLE_MIN_TEXT_WIDTH = 50;
const int CONSOLE_MIN_HISTORY = 8;
const int CONSOLE_MAX_HISTORY = 512;

const COLORREF CONSOLE_COLOR_BG = RGB(40, 40, 40);
const COLORREF CONSOLE_COLOR_TEXT = RGB(180, 180, 180);
const COLORREF CONSOLE_COLOR_CMD = RGB(255, 255, 255);
const COLORREF CONSOLE_COLOR_CURSOR = RGB(255, 255, 255);
const COLORREF CONSOLE_COLOR_SEL = RGB(80, 80, 80);
// GDI writes RGB with an undefined/zero alpha byte. Retail blits ignored it;
// the source renderer blends premultiplied BGRA. Own this conversion here so
// console colors remain opaque without changing other UI surfaces.
class CConsolePaint
{
    CI3DSurface *surface;
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previous = nullptr;
    DWORD *pixels = nullptr;
    int width, top, height;
public:
    CConsolePaint(CI3DSurface *target, int w, int y, int h)
        : surface(target), width(w), top(y), height(h)
    {
        BITMAPINFO info = {};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        dc = CreateCompatibleDC(nullptr);
        if (!dc) return;
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS,
                                  reinterpret_cast<void **>(&pixels), nullptr, 0);
        if (!bitmap) return;
        previous = SelectObject(dc, bitmap);
        SetViewportOrgEx(dc, 0, -top, nullptr);
    }
    CDC *GetDC() { return pixels ? CDC::FromHandle(dc) : nullptr; }
    ~CConsolePaint()
    {
        if (pixels) {
            GdiFlush();
            for (size_t i = 0; i < size_t(width) * height; ++i)
                pixels[i] |= 0xff000000u;
            D3D11_BOX box = {0, UINT(top), 0, UINT(width), UINT(top + height), 1};
            D3D11_SUBRESOURCE_DATA data = {pixels, UINT(width * sizeof(DWORD)), 0};
            surface->GetSurface()->UpdateTexture(0, &box, &data);
        }
        if (previous) SelectObject(dc, previous);
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
    }
    CConsolePaint(const CConsolePaint &) = delete;
    CConsolePaint &operator=(const CConsolePaint &) = delete;
};
/////////////////////////////////////////////////////////////////////////////////////
CI3DConsole::CI3DConsole(CInterface3D *pI)
{
    pI3D = pI;
    pSurface = 0;
    nCurStackCommand = -1;
    nCursorPos = 0;
    nSelStart = 0;
    nCurrentLine = 0;
    bVisible = false;
    bCtrlDown = false;
    bShiftDown = false;

    pConsoleFont = new CFont();
    if (!pConsoleFont->CreatePointFont(90, _T("Courier New")))
    {
        delete pConsoleFont;
        pConsoleFont = 0;
    }

    SetRect(CRect(0, 0, 800, 150));
    SetHistory(256);
}
/////////////////////////////////////////////////////////////////////////////////////
CI3DConsole::~CI3DConsole()
{
    if (pSurface)
        delete pSurface;
    pSurface = 0;
    if (pConsoleFont)
        delete pConsoleFont;
    pConsoleFont = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DConsole::SetRect(CRect rc)
{
    int w = pI3D->GetWidth(), h = pI3D->GetHeight();

    rcReal = rc;
    pI3D->VirtualToScreen(rcReal);

    nWidth = rcReal.Width();
    nHeight = rcReal.Height();

    if (nWidth < 2 * CONSOLE_BORDER + CONSOLE_SCROLL_WIDTH + CONSOLE_MIN_TEXT_WIDTH)
        nWidth = 2 * CONSOLE_BORDER + CONSOLE_SCROLL_WIDTH + CONSOLE_MIN_TEXT_WIDTH;
    if (nWidth > w)
        nWidth = w;

    if (nHeight < 3 * CONSOLE_BORDER + 3 * CONSOLE_LINE_HEIGHT)
        nHeight = 3 * CONSOLE_BORDER + 3 * CONSOLE_LINE_HEIGHT;
    if (nHeight > h)
        nHeight = h;

    nVisibleLines = (nHeight - 3 * CONSOLE_BORDER) / CONSOLE_LINE_HEIGHT - 1;
    // nHeight = (nVisibleLines + 1) * CONSOLE_LINE_HEIGHT + 3 * CONSOLE_BORDER;

    rcReal.right = rcReal.left + nWidth;
    rcReal.bottom = rcReal.top + nHeight;

    rcVirtual = rcReal;
    pI3D->ScreenToVirtual(rcVirtual);

    if (IsVisible())
    {
        Show(false);
        Show(true);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DConsole::SetHistory(int nLines)
{
    nTotalLines = nLines;
    if (nLines < CONSOLE_MIN_HISTORY)
        nLines = CONSOLE_MIN_HISTORY;
    if (nLines > CONSOLE_MAX_HISTORY)
        nLines = CONSOLE_MAX_HISTORY;
    RemoveExcess();
    RedrawLines();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DConsole::Draw2D()
{
    if (pSurface)
        pSurface->Draw();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DConsole::Show(bool bShow)
{
    bVisible = bShow;
    if (bShow)
    {
        if (!pSurface)
            pSurface = dbgnew CI3DSurface(pI3D, rcVirtual, BS_VISIBLE);
        RedrawLines();
        RedrawCommand();
    }
    else
    {
        if (pSurface)
            delete pSurface;
        pSurface = 0;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DConsole::Output(const char *pszText)
{
    CStringArray szNewLines;
    NStrProc::SplitString(pszText, szNewLines, '\n');
    if (szLines.GetSize())
        szLines[szLines.GetSize() - 1] += szNewLines[0];
    else
        szLines.Add(szNewLines[0]);
    for (int i = 1; i < szNewLines.GetSize(); i++)
        szLines.Add(szNewLines[i]);
    if (nCurrentLine)
        nCurrentLine += szNewLines.GetSize();
    RemoveExcess();
    RedrawLines();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DConsole::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    switch (nChar)
    {
    case VK_CONTROL:
        bCtrlDown = true;
        break;

    case VK_SHIFT:
        bShiftDown = true;
        break;

    case VK_DELETE:
        if (nSelStart != nCursorPos)
        {
            int i = Min(nSelStart, nCursorPos);
            int j = Max(nSelStart, nCursorPos);
            szCommand = szCommand.Left(i) + szCommand.Mid(j);
            nSelStart = nCursorPos = i;
            RedrawCommand();
        }
        else if (nCursorPos < szCommand.GetLength())
        {
            szCommand = szCommand.Left(nCursorPos) + szCommand.Mid(nCursorPos + 1);
            RedrawCommand();
        }
        break;

    case VK_LEFT:
        --nCursorPos;
        if (!bShiftDown)
            nSelStart = nCursorPos;
        RedrawCommand();
        break;

    case VK_RIGHT:
        ++nCursorPos;
        if (!bShiftDown)
            nSelStart = nCursorPos;
        RedrawCommand();
        break;

    case VK_HOME:
        if (bCtrlDown)
        {
            nCurrentLine = Max(0, szLines.GetSize() - nVisibleLines);
            RedrawLines();
        }
        else
        {
            nCursorPos = 0;
            if (!bShiftDown)
                nSelStart = nCursorPos;
            RedrawCommand();
        }
        break;

    case VK_END:
        if (bCtrlDown)
        {
            nCurrentLine = 0;
            RedrawLines();
        }
        else
        {
            nCursorPos = szCommand.GetLength();
            if (!bShiftDown)
                nSelStart = nCursorPos;
            RedrawCommand();
        }
        break;

    case VK_UP:
        if (nCurStackCommand < szCommandStack.GetSize())
            nCurStackCommand++;
        if (nCurStackCommand < 0 || nCurStackCommand >= szCommandStack.GetSize())
            szCommand.Empty();
        else
            szCommand = szCommandStack[nCurStackCommand];
        nSelStart = nCursorPos = szCommand.GetLength();
        RedrawCommand();
        break;

    case VK_DOWN:
        if (nCurStackCommand >= 0)
            nCurStackCommand--;
        if (nCurStackCommand < 0 || nCurStackCommand >= szCommandStack.GetSize())
            szCommand.Empty();
        else
            szCommand = szCommandStack[nCurStackCommand];
        nSelStart = nCursorPos = szCommand.GetLength();
        RedrawCommand();
        break;

    case VK_PRIOR:
        if (bCtrlDown)
            nCurrentLine += nVisibleLines;
        else
            nCurrentLine++;
        RedrawLines();
        break;

    case VK_NEXT:
        if (bCtrlDown)
            nCurrentLine -= nVisibleLines;
        else
            nCurrentLine--;
        RedrawLines();
        break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
constexpr int GetCharIndex(UINT nChar)
{
    return nChar - 'a' + 1;
};
void CI3DConsole::OnChar(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    int i = min(nSelStart, nCursorPos);
    int j = max(nSelStart, nCursorPos);

    switch (nChar)
    {
    case GetCharIndex('a'):
        if (bCtrlDown)
        {
            nSelStart = 0;
            nCursorPos = szCommand.GetLength();
            RedrawCommand();
            break;
        }
    case GetCharIndex('c'):
    case GetCharIndex('x'):
        if (bCtrlDown && nSelStart != nCursorPos)
        {
            std::string selected(szCommand.GetString() + i, szCommand.GetString() + j);
            ClipboardSetText(AnsiToWide(selected));
            if (nChar == GetCharIndex('x'))
            {
                szCommand = szCommand.Left(i) + szCommand.Left(j);
                nSelStart = nCursorPos = i;
                RedrawCommand();
            }
            break;
        }
    case GetCharIndex('v'):
        if (bCtrlDown)
        {
            CString clipboard(WideToAnsi(ClipboardGetText()).c_str());
            auto newLinePos = clipboard.FindOneOf("\r\n");
            if (newLinePos >= 0)
                clipboard = clipboard.Left(newLinePos);
            szCommand = szCommand.Left(i) + clipboard + szCommand.Mid(j);
            nSelStart = nCursorPos = j + clipboard.GetLength() - (nSelStart != nCursorPos ? 1 : 0);
            RedrawCommand();
            break;
        }
    case 13: // Enter
        if (szCommandStack.IsEmpty() || szCommand != szCommandStack[0])
        {
            szCommandStack.InsertAt(0, szCommand);
            if (szCommandStack.GetSize() > CONSOLE_STACK_SIZE)
                szCommandStack.RemoveAt(szCommandStack.GetSize() - 1);     
        } 
        nCurStackCommand = -1;
        szCommand.Empty();
        nSelStart = nCursorPos = 0;
        RedrawCommand();

        Output(CString("> ") + szCommandStack[0] + "\n");
        NConsole::Process(szCommandStack[0]);
        break;

    case VK_BACK:
        if (nSelStart != nCursorPos)
        {
            int i = Min(nSelStart, nCursorPos);
            int j = Max(nSelStart, nCursorPos);
            szCommand = szCommand.Left(i) + szCommand.Mid(j);
            nSelStart = nCursorPos = i;
            RedrawCommand();
        }
        else if (nCursorPos)
        {
            szCommand = szCommand.Left(nCursorPos - 1) + szCommand.Mid(nCursorPos);
            nSelStart = --nCursorPos;
            RedrawCommand();
        }
        break;

    case VK_ESCAPE:
        szCommand.Empty();
        nSelStart = nCursorPos = 0;
        RedrawCommand();
        break;

    case VK_TAB:
        break;

    default:
        if (!bCtrlDown)
        {
            int i = Min(nSelStart, nCursorPos);
            int j = Max(nSelStart, nCursorPos);
            szCommand = szCommand.Left(i) + (char)nChar + szCommand.Mid(j);
            nSelStart = nCursorPos = i + 1;
            RedrawCommand();
        }
        break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DConsole::OnKeyUp(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    switch (nChar)
    {
    case VK_CONTROL:
        bCtrlDown = false;
        break;

    case VK_SHIFT:
        bShiftDown = false;
        break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DConsole::RedrawLines()
{
    if (nCurrentLine > szLines.GetSize() - nVisibleLines)
        nCurrentLine = szLines.GetSize() - nVisibleLines;
    if (nCurrentLine < 0)
        nCurrentLine = 0;

    if (!pSurface || !pConsoleFont)
        return;

#ifndef NOGETDC
    CConsolePaint paint(pSurface, nWidth, 0,
                        nHeight - 2 * CONSOLE_BORDER - CONSOLE_LINE_HEIGHT);
    CDC *pDC = paint.GetDC();
    if (!pDC)
        return;

    pDC->FillSolidRect(0, 0, nWidth, nHeight - 2 * CONSOLE_BORDER - CONSOLE_LINE_HEIGHT,
                       CONSOLE_COLOR_BG);

    CFont *pOldFont = pDC->SelectObject(pConsoleFont);
    pDC->SetTextColor(CONSOLE_COLOR_TEXT);
    pDC->SetBkMode(TRANSPARENT);

    int i, j;
    for (i = 0, j = szLines.GetSize() - nCurrentLine - 1; i < nVisibleLines && j >= 0; i++, j--)
    {
        pDC->DrawText(
            szLines[j],
            CRect(CONSOLE_BORDER,
                  nHeight - 2 * (CONSOLE_BORDER + CONSOLE_LINE_HEIGHT) - i * CONSOLE_LINE_HEIGHT,
                  nWidth - CONSOLE_BORDER - CONSOLE_SCROLL_WIDTH,
                  nHeight - 2 * CONSOLE_BORDER - CONSOLE_LINE_HEIGHT - i * CONSOLE_LINE_HEIGHT + 5),
            DT_LEFT | DT_NOPREFIX);
    }

    if (nCurrentLine > 0)
        pDC->DrawText("+",
                      CRect(nWidth - CONSOLE_BORDER - CONSOLE_SCROLL_WIDTH,
                            nHeight - 2 * (CONSOLE_BORDER + CONSOLE_LINE_HEIGHT),
                            nWidth - CONSOLE_BORDER,
                            nHeight - 2 * CONSOLE_BORDER - CONSOLE_LINE_HEIGHT),
                      DT_RIGHT | DT_NOPREFIX);

    if (nCurrentLine < szLines.GetSize() - nVisibleLines)
        pDC->DrawText("+",
                      CRect(nWidth - CONSOLE_BORDER - CONSOLE_SCROLL_WIDTH, CONSOLE_BORDER,
                            nWidth - CONSOLE_BORDER, CONSOLE_BORDER + CONSOLE_LINE_HEIGHT),
                      DT_RIGHT | DT_NOPREFIX);

    pDC->SelectObject(pOldFont);

    // paint uploads the opaque console rectangle after GDI drawing.
#endif // NOGETDC
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DConsole::RedrawCommand()
{
    if (nCursorPos > szCommand.GetLength())
        nCursorPos = szCommand.GetLength();
    if (nCursorPos < 0)
        nCursorPos = 0;
    if (nSelStart > szCommand.GetLength())
        nSelStart = szCommand.GetLength();
    if (nSelStart < 0)
        nSelStart = 0;

    if (!pSurface || !pConsoleFont)
        return;

    CSize sizeCursor, sizeSel;

#ifndef NOGETDC
    CConsolePaint paint(pSurface, nWidth,
                        nHeight - 2 * CONSOLE_BORDER - CONSOLE_LINE_HEIGHT,
                        2 * CONSOLE_BORDER + CONSOLE_LINE_HEIGHT);
    CDC *pDC = paint.GetDC();
    if (!pDC)
        return;
    CFont *pOldFont = pDC->SelectObject(pConsoleFont);

    // clear
    pDC->FillSolidRect(0, nHeight - 2 * CONSOLE_BORDER - CONSOLE_LINE_HEIGHT, nWidth,
                       2 * CONSOLE_BORDER + CONSOLE_LINE_HEIGHT, CONSOLE_COLOR_BG);

    // selection
    sizeCursor = pDC->GetOutputTextExtent(CString("> ") + szCommand.Left(nCursorPos));
    if (nSelStart != nCursorPos)
    {
        sizeSel = pDC->GetOutputTextExtent(CString("> ") + szCommand.Left(nSelStart));
        int begin = Min(sizeCursor.cx, sizeSel.cx);
        int end = Max(sizeCursor.cx, sizeSel.cx);
        if (end > nWidth - 2 * CONSOLE_BORDER)
            end = nWidth - 2 * CONSOLE_BORDER;
        if (begin < end)
            pDC->FillSolidRect(CONSOLE_BORDER + begin,
                               nHeight - CONSOLE_LINE_HEIGHT - CONSOLE_BORDER, end - begin,
                               CONSOLE_LINE_HEIGHT, CONSOLE_COLOR_SEL);
    }

    // command
    pDC->SetTextColor(CONSOLE_COLOR_CMD);
    pDC->SetBkMode(TRANSPARENT);

    pDC->DrawText(CString("> ") + szCommand,
                  CRect(CONSOLE_BORDER, nHeight - CONSOLE_LINE_HEIGHT - CONSOLE_BORDER,
                        nWidth - CONSOLE_BORDER, nHeight - CONSOLE_BORDER + 5),
                  DT_LEFT | DT_NOPREFIX);

    // cursor
    if (sizeCursor.cx < nWidth - 2 * CONSOLE_BORDER)
    {
        CPen pen(PS_SOLID, 2, CONSOLE_COLOR_CURSOR);
        CPen *pOldPen = pDC->SelectObject(&pen);
        pDC->MoveTo(CONSOLE_BORDER + sizeCursor.cx, nHeight - CONSOLE_LINE_HEIGHT - CONSOLE_BORDER);
        pDC->LineTo(CONSOLE_BORDER + sizeCursor.cx, nHeight - CONSOLE_BORDER);
        pDC->SelectObject(pOldPen);
    }

    pDC->SelectObject(pOldFont);

    // paint uploads the opaque console rectangle after GDI drawing.
#endif // NOGETDC
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DConsole::RemoveExcess()
{
    if (szLines.GetSize() > nTotalLines)
        szLines.RemoveAt(0, szLines.GetSize() - nTotalLines);
}
/////////////////////////////////////////////////////////////////////////////////////
