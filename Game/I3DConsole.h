#pragma once

class CI3DSurface;
class CInterface3D;
/////////////////////////////////////////////////////////////////////////////////////
class CI3DConsole
{
private:
    CInterface3D *pI3D;
    CI3DSurface *pSurface; // 2d surface
    CFont *pConsoleFont;   // console font

    CStringArray szCommandStack; // for fast prev commands
    int nCurStackCommand;        // if 0 - last command
    CString szCommand;           // current command

    int nCursorPos; // cursor position
    int nSelStart;  // where shift was down

    int nWidth;
    int nHeight; // in real pixels
    int nLineHeight;
    int nFontPixels=0;
    int nBorder=8,nScrollWidth=10,nMinTextWidth=50;
    CRect rcRequested;
    CRect rcReal;

    CStringArray szLines; // all lines in output pool
    int nTotalLines;      // max number of lines
    int nVisibleLines;    // number of visible lines
    int nCurrentLine;     // last visible line: if 0 - end of output at the bottom

    bool bCtrlDown;  // is CTRL down
    bool bShiftDown; // is SHIFT down

    bool bVisible; // is visible?

private:
    void RedrawLines();   // redraw output
    void RedrawCommand(); // redraw current command and cursor

    void RemoveExcess(); // remove excess lines in output

public:
    CI3DConsole(CInterface3D *pI3D);
    ~CI3DConsole();

    void SetRect(CRect rc);
    void SetHistory(int nLines);

    void Draw2D();

    void Show(bool bShow);
    void Toggle() { Show(!bVisible); }
    bool IsVisible() { return bVisible; }

    void Output(const char *pszText);

    void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
    void OnChar(UINT nChar, UINT nRepCnt, UINT nFlags);
    void OnKeyUp(UINT nChar, UINT nRepCnt, UINT nFlags);
};
