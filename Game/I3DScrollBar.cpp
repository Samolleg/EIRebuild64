// I3DScrollBar.cpp: implementation of the CI3DScrollBar class.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "I3DScrollBar.h"
#include "OwnedRender/E3Hud.h"
#include "I3DMain.h"
#include "A3Cursor.h"
//////////////////////////////////////////////////////////////////////
CI3DScrollBar::CI3DScrollBar(CInterface3D *pI, CRect rectMake, float fDepth, int nRange,
                             float fSpeed)
    : CI3DObject(pI)
    , m_fDepth(fDepth)
    , m_nRange(nRange)
    , m_fSpeed(fSpeed)
{
    m_isUpdated = true;
    m_eMode = SBM_FREE;
    m_nCurPos = -1;

    AddTexture("Scrollbar");

    AddFigure();
    AddFigure();
    AddFigure();
    AddFigure();

    AddArea();
    AddArea();
    AddArea();

    SetRect(rectMake);
    SetPos(0);
    fToScroll = 0;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
void CI3DScrollBar::SetRange(int nRange)
{
    if (nRange < 0)
        nRange = 0;

    m_nRange = nRange;
    m_isUpdated = true;
    if (m_nRange == 0)
    {
        GetFigure(0)->Show(FALSE);
        GetFigure(1)->Show(FALSE);
        GetFigure(2)->Show(FALSE);
        GetFigure(3)->Show(FALSE);
    }
    else
    {
        GetFigure(0)->Show(TRUE);
        GetFigure(1)->Show(TRUE);
        GetFigure(2)->Show(TRUE);
        GetFigure(3)->Show(TRUE);
    }
    ForcePointerUpdate();
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CI3DScrollBar::GetPos(int &nPos)
{
    nPos = m_nCurPos;
    if (m_isUpdated)
    {
        m_isUpdated = false;
        return TRUE;
    }
    return FALSE;
}
//////////////////////////////////////////////////////////////////////
void CI3DScrollBar::SetPos(int nPos)
{
    if (nPos < 0)
        nPos = 0;
    if (nPos > m_nRange)
        nPos = m_nRange;

    m_isUpdated = true;

    if (m_nCurPos != nPos)
    {
        m_nCurPos = nPos;

        CI3DFigure *pFig = GetFigure(1);
        int nViewPos = int(float(m_nCurPos * m_rectView.Height()) / float(m_nRange));
        pFig->SetRelativeToScreen((float)m_rectView.left + 5, (float)m_rectView.top + nViewPos,
                                  m_fDepth - 0.1f);

        m_rectPointer.left = m_rectView.left;
        m_rectPointer.right = m_rectView.left + 10;
        m_rectPointer.top = m_rectView.top + nViewPos - 14;
        m_rectPointer.bottom = m_rectView.top + nViewPos + 14;
    }
}
//////////////////////////////////////////////////////////////////////
void CI3DScrollBar::OnMouseMove(UINT nFlags, CPoint point)
{
    if (m_eMode == SBM_TRACK)
    {
        POINT native{};bool valid=false;
        if(OwnedRender::E3MessagePoint(this,point,native,true,valid)) {
            if(!valid)return;
            point=CPoint(native);
        } else pI3D->ScreenToVirtual(point);
        point -= m_ptInc;
        int nPos = int(float(point.y - m_rectView.top) * float(m_nRange) /
                       float(m_rectView.Height()));
        SetPos(nPos);
    }
    SetProcessed();
}
//////////////////////////////////////////////////////////////////////
void CI3DScrollBar::OnLButtonDown(UINT nFlags, CPoint point)
{
    CPoint ptView(point);
    POINT native{};bool valid=false;
    if(OwnedRender::E3MessagePoint(this,point,native,false,valid)) {
        if(!valid)return;
        point=CPoint(native);
    } else pI3D->ScreenToVirtual(point);
    if (m_rectPointer.PtInRect(point))
    {
        pI3D->SetCapture(this);
        m_eMode = SBM_TRACK;
        m_ptInc = point - m_rectPointer.CenterPoint();
        SetProcessed();
    }
    else
        switch (HitTest(ptView))
        {
        case 0:
            pI3D->SetCapture(this);
            m_eMode = SBM_SCROLL_UP;
            fToScroll = 0;
            SetProcessed();
            break;
        case 1:
            pI3D->SetCapture(this);
            m_eMode = SBM_SCROLL_DOWN;
            fToScroll = 0;
            SetProcessed();
            break;
        }
}
//////////////////////////////////////////////////////////////////////
void CI3DScrollBar::OnLButtonUp(UINT nFlags, CPoint point)
{
    m_eMode = SBM_FREE;
    pI3D->ReleaseCapture();
    fToScroll = 0;
    SetProcessed();
}
//////////////////////////////////////////////////////////////////////
void CI3DScrollBar::OnIdle()
{
    if (m_eMode == SBM_SCROLL_UP || m_eMode == SBM_SCROLL_DOWN)
    {
        float fStep = GetFrameTime() * m_fSpeed * 16;
        if (m_eMode == SBM_SCROLL_UP)
            fStep = -fStep;
        fToScroll += fStep;
    }

    int nToScroll = 0;
    if (fToScroll > 0)
        while (fToScroll >= 1)
        {
            nToScroll++;
            fToScroll -= 1;
        }
    else
        while (fToScroll <= -1)
        {
            nToScroll--;
            fToScroll += 1;
        }

    if (nToScroll != 0)
        SetPos(m_nCurPos + nToScroll);
}
//////////////////////////////////////////////////////////////////////
BOOL CI3DScrollBar::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
    if (HitTest(pt) == 2)
    {
        fToScroll -= m_fSpeed * zDelta / WHEEL_DELTA;
        SetProcessed();
    }
    return TRUE;
}
//////////////////////////////////////////////////////////////////////
void CI3DScrollBar::SetRect(CRect rectMake)
{
    CI3DObject::SetRect(rectMake);
    // m_rectView = rectMake;
    m_rectView.left = rectMake.right - 10;
    m_rectView.right = rectMake.right;
    m_rectView.top = rectMake.top + 26;
    m_rectView.bottom = rectMake.bottom - 26;
    // m_rectView.left = m_rectView.right - N_SCROLL_WIDTH;

    CI3DFigure *pFig = GetFigure(0);
    pFig->MakeBar(m_fDepth, 100, rectMake.right - 8, rectMake.top + 4, rectMake.right - 2,
                  rectMake.bottom - 4, 8, 4, 32, 252);

    pFig = GetFigure(1);
    pFig->MakeBar(m_fDepth - 0.1f, 100, rectMake.right - 10, rectMake.top, rectMake.right,
                  rectMake.top + 28, 40, 8, 80, 120);

    pFig = GetFigure(2);
    pFig->MakeBar(m_fDepth, 100, rectMake.right - 10, rectMake.top, rectMake.right,
                  rectMake.top + 12, 40, 176, 80, 128);

    pFig = GetFigure(3);
    pFig->MakeBar(m_fDepth, 100, rectMake.right - 10, rectMake.bottom - 12, rectMake.right,
                  rectMake.bottom, 40, 128, 80, 176);

    GetArea(0)->MakeRect(rectMake.right - 10, rectMake.top, rectMake.right, rectMake.top + 12);
    GetArea(1)->MakeRect(rectMake.right - 10, rectMake.bottom - 12, rectMake.right,
                         rectMake.bottom);
    GetArea(2)->MakeRect(rectMake.left, rectMake.top, rectMake.right - 10, rectMake.bottom);

    ForcePointerUpdate();
}
/////////////////////////////////////////////////////////////////////////////////////////////////////
void CI3DScrollBar::ForcePointerUpdate()
{
    int nPos = m_nCurPos;
    m_nCurPos--;
    SetPos(nPos);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////
int CI3DScrollBar::GetRange()
{
    return m_nRange;
}
//////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////
void CI3DHScrollBar::SetPos(int nPos)
{
    if (nPos < 0)
        nPos = 0;
    if (nPos > m_nRange)
        nPos = m_nRange;

    m_isUpdated = true;

    if (m_nCurPos != nPos)
    {
        m_nCurPos = nPos;

        CI3DFigure *pFig = GetFigure(1);
        int nViewPos = int(float(m_nCurPos * m_rectView.Width()) / float(m_nRange));
        pFig->SetRelativeToScreen((float)m_rectView.left + nViewPos, (float)m_rectView.top + 5,
                                  m_fDepth - 0.1f);

        m_rectPointer.top = m_rectView.top;
        m_rectPointer.bottom = m_rectView.top + 10;
        m_rectPointer.left = m_rectView.left + nViewPos - 14;
        m_rectPointer.right = m_rectView.left + nViewPos + 14;
    }
}
//////////////////////////////////////////////////////////////////////
void CI3DHScrollBar::OnMouseMove(UINT nFlags, CPoint point)
{
    if (m_eMode == SBM_TRACK)
    {
        pI3D->ScreenToVirtual(point);
        point -= m_ptInc;
        int nPos = int(float(point.x - m_rectView.left) * float(m_nRange) /
                       float(m_rectView.Width()));
        SetPos(nPos);
    }
    SetProcessed();
}
//////////////////////////////////////////////////////////////////////
void CI3DHScrollBar::SetRect(CRect rectMake)
{
    CI3DObject::SetRect(rectMake);
    m_rectView.top = rectMake.bottom - 10;
    m_rectView.bottom = rectMake.bottom;
    m_rectView.left = rectMake.left + 26;
    m_rectView.right = rectMake.right - 26;

    CI3DFigure *pFig = GetFigure(0);
    pFig->MakeBar(m_fDepth, 100, 0, 0, 6, rectMake.Width() - 8, 8, 4, 32, 252);
    pFig->SetRelativeToScreen((rectMake.left + rectMake.right) / 2.f, rectMake.bottom - 5,
                              m_fDepth);
    pFig->SetOrientation(CQuaternion(CPlot3D(0, 0, 1), FP_PI2));

    pFig = GetFigure(1);
    pFig->MakeBar(m_fDepth - 0.1f, 100, 0, 0, 10, 28, 40, 8, 80, 120);
    pFig->SetOrientation(CQuaternion(CPlot3D(0, 0, 1), FP_PI2));

    pFig = GetFigure(2);
    pFig->MakeBar(m_fDepth, 100, 0, 0, 10, 12, 40, 176, 80, 128);
    pFig->SetRelativeToScreen(rectMake.right - 6, rectMake.bottom - 5, m_fDepth);
    pFig->SetOrientation(CQuaternion(CPlot3D(0, 0, 1), FP_PI2));

    pFig = GetFigure(3);
    pFig->MakeBar(m_fDepth, 100, 0, 0, 10, 12, 40, 128, 80, 176);
    pFig->SetRelativeToScreen(rectMake.left + 6, rectMake.bottom - 5, m_fDepth);
    pFig->SetOrientation(CQuaternion(CPlot3D(0, 0, 1), FP_PI2));

    GetArea(0)->MakeRect(rectMake.left, rectMake.bottom - 10, rectMake.left + 12, rectMake.bottom);
    GetArea(1)->MakeRect(rectMake.right - 12, rectMake.bottom - 10, rectMake.right,
                         rectMake.bottom);
    GetArea(2)->MakeRect(rectMake.left, rectMake.top, rectMake.right, rectMake.bottom - 10);

    ForcePointerUpdate();
}
//////////////////////////////////////////////////////////////////////
CI3DHScrollBar::CI3DHScrollBar(CInterface3D *pI, CRect rectMake, float fDepth, int nRange,
                               float fSpeed)
    : CI3DScrollBar(pI, rectMake, fDepth, nRange, fSpeed)
{
}
//////////////////////////////////////////////////////////////////////
CI3DSwitcher::CI3DSwitcher(CInterface3D *pI, CRect rectMake, LPCTSTR pszDefaultState)
    : CI3DObject(pI)
{
    SetRect(rectMake);
    AddSurface(dbgnew CI3DSurface(pI, rectMake));
    if (pszDefaultState)
        szStates.Add(pszDefaultState);
    else
        szStates.Add("");
    mbUpdate = false;
    rcClick = rectMake;
    SetState(0);
}
//////////////////////////////////////////////////////////////////////
void CI3DSwitcher::AddState(LPCTSTR pszState)
{
    if (pszState)
        szStates.Add(pszState);
    else
        szStates.Add("");
}
//////////////////////////////////////////////////////////////////////
bool CI3DSwitcher::GetState(int &nState)
{
    nState = mnCurState;
    if (mbUpdate)
    {
        mbUpdate = false;
        return true;
    }
    return false;
}
//////////////////////////////////////////////////////////////////////
void CI3DSwitcher::SetState(int nState)
{
    if (nState >= szStates.GetSize())
        mnCurState = 0;
    else
        mnCurState = nState;
#ifndef NOGETDC
    surfaces[0]->GetDC();
    surfaces[0]->Clear();
    surfaces[0]->DrawText(szStates[mnCurState], rcClick - rcClick.TopLeft(), 1, RGB(228, 215, 167),
                          DT_LEFT);
    surfaces[0]->ReleaseDC();
#endif // NOGETDC
    mbUpdate = true;
}
//////////////////////////////////////////////////////////////////////
void CI3DSwitcher::OnLButtonDown(UINT nFlags, CPoint point)
{
    pI3D->BeginFX("buttons\\messbox\\ok.wav");
    SetState(mnCurState + 1);
}
//////////////////////////////////////////////////////////////////////
void CI3DSwitcher::OnLButtonDblClk(UINT nFlags, CPoint point)
{
    // for not changing second time when dblclk arrives
}
//////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////
CI3DEditBox::CI3DEditBox(CInterface3D *pI, CRect rcNewBox)
    : CI3DObject(pI)
{
    flags = EBF_ALL;
    bCentered = false;
    nFont = 1;
    nCharLimit = 50;
    crText = RGB(255, 255, 255);
    crCursor = RGB(255, 0, 0);

    SetBox(rcNewBox);
    bActive = true;
}
//////////////////////////////////////////////////////////////////////
void CI3DEditBox::SetParams(int _nFlags, bool _bCentered, int _nFont, int _nCharLimit,
                            COLORREF _crText, COLORREF _crCursor)
{
    flags = EEditBoxFlags(_nFlags);
    bCentered = _bCentered;
    nFont = _nFont;
    nCharLimit = _nCharLimit;
    crText = _crText;
    crCursor = _crCursor;
    Redraw();
}
//////////////////////////////////////////////////////////////////////
void CI3DEditBox::SetBox(CRect rcNewBox)
{
    RemoveAllSurfaces();
    rcBox = rcNewBox;
    AddSurface(dbgnew CI3DSurface(pI3D, rcBox));
    Redraw();
}
//////////////////////////////////////////////////////////////////////
void CI3DEditBox::SetString(LPCTSTR pszString)
{
    bEdited = false;
    szString = pszString;
    bUpdated = true;
    nCursorPos = szString.GetLength();
    Redraw();
}
//////////////////////////////////////////////////////////////////////
void CI3DEditBox::Activate(bool _bActive)
{
    bActive = _bActive;
}
//////////////////////////////////////////////////////////////////////
void CI3DEditBox::Redraw()
{
    if (nCursorPos > szString.GetLength())
        nCursorPos = szString.GetLength();
    if (nCursorPos < 0)
        nCursorPos = 0;

#ifndef NOGETDC
    CI3DSurface *pSurf = surfaces[0];
    CDC *pDC = pSurf->GetDC();
    if (!pDC)
        return;
    pSurf->Clear();

    if (bCentered)
        pSurf->DrawText(szString, rcBox - rcBox.TopLeft(), nFont, crText, DT_CENTER | DT_NOPREFIX);
    else
        pSurf->DrawText(szString, rcBox - rcBox.TopLeft(), nFont, crText, DT_LEFT | DT_NOPREFIX);

    CSize sizeCursor = pI3D->TextSize(szString.Left(nCursorPos), nFont);
    int nX;
    if (bCentered)
    {
        CSize sizeLine = pI3D->TextSize(szString, nFont);
        nX = rcBox.Width() / 2 - sizeLine.cx / 2 + sizeCursor.cx;
    }
    else
        nX = sizeCursor.cx;
    if (nX >= 0 && nX < rcBox.Width())
    {
        CPen pen(PS_SOLID, 1, crCursor);
        CPen *pOldPen = pDC->SelectObject(&pen);
        CPoint p1(nX, 0);
        CPoint p2(nX, rcBox.Height());
        pI3D->VirtualToScreenLocal(p1);
        pI3D->VirtualToScreenLocal(p2);
        pDC->MoveTo(p1.x, p1.y);
        pDC->LineTo(p2.x, p2.y);
        pDC->SelectObject(pOldPen);
    }

    pSurf->ReleaseDC();
#endif // NOGETDC
}
//////////////////////////////////////////////////////////////////////
void CI3DEditBox::BeginEdit(bool bClear)
{
    if (!bEdited)
    {
        bEdited = true;
        if (bClear)
        {
            szString.Empty();
            nCursorPos = 0;
            bUpdated = true;
        }
        else
            nCursorPos = szString.GetLength();
    }
}
//////////////////////////////////////////////////////////////////////
void CI3DEditBox::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    if (!bActive)
        return;
    switch (nChar)
    {
    case VK_BACK:
        BeginEdit();
        if (nCursorPos > 0)
        {
            szString = szString.Left(nCursorPos - 1) + szString.Mid(nCursorPos);
            --nCursorPos;
            bUpdated = true;
        }
        Redraw();
        SetProcessed();
        break;
    case VK_DELETE:
        BeginEdit();
        if (nCursorPos < szString.GetLength())
        {
            szString = szString.Left(nCursorPos) + szString.Mid(nCursorPos + 1);
            bUpdated = true;
        }
        Redraw();
        SetProcessed();
        break;
    case VK_LEFT:
        BeginEdit(false);
        --nCursorPos;
        Redraw();
        SetProcessed();
        break;
    case VK_RIGHT:
        BeginEdit(false);
        ++nCursorPos;
        Redraw();
        SetProcessed();
        break;
    case VK_HOME:
        BeginEdit(false);
        nCursorPos = 0;
        Redraw();
        SetProcessed();
        break;
    case VK_END:
        BeginEdit(false);
        nCursorPos = szString.GetLength();
        Redraw();
        SetProcessed();
        break;
    }
}
//////////////////////////////////////////////////////////////////////
void CI3DEditBox::OnChar(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    if (!bActive)
        return;
    switch (nChar)
    {
    case VK_BACK:
    case VK_TAB:
    case VK_ESCAPE:
    case 13:
        break;

    default: {
        if (pI3D->IsCtrlDown())
            break;
        char ch = (char)nChar;
        EEditBoxFlags fl;
        if (isdigit(nChar))
            fl = EBF_DIGITS;
        else if (isalpha(nChar) || ch == ' ')
            fl = EBF_LETTERS;
        else if (ch == '.' || ch == ':')
            fl = EBF_DOTS;
        else
            fl = EBF_SYMBOLS;
        if (!(flags & fl))
            break;
        BeginEdit();
        CString szNew = szString.Left(nCursorPos) + ch + szString.Mid(nCursorPos);
        if (szString.GetLength() < nCharLimit && pI3D->TextSize(szNew, nFont).cx < rcBox.Width())
        {
            szString = szNew;
            bUpdated = true;
            nCursorPos++;
        }
        Redraw();
        SetProcessed();
    }
    break;
    }
}
//////////////////////////////////////////////////////////////////////
