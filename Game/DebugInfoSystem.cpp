#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "DebugInfoSystem.h"
/////////////////////////////////////////////////////////////////////////////////////
const int DEBUGINFO_LINE_HEIGHT = 15;
const int DEBUGINFO_MIN_WIDTH = 100;
const int DEBUGINFO_MIN_HEIGHT = 15;
const DWORD DEBUGINFO_DELAY = 500;
const COLORREF DEBUGINFO_COLOR_TEXT = RGB(255, 255, 255);
/////////////////////////////////////////////////////////////////////////////////////
CDebugInfoSystem theDebugInfoSystem;
CDebugInfoSystemInterface *GetDebugInfoSystem()
{
    return static_cast<CDebugInfoSystemInterface *>(&theDebugInfoSystem);
}
/////////////////////////////////////////////////////////////////////////////////////
void CDebugInfoSystem::Set(LPCSTR key, LPCSTR val)
{
    datamap[key] = val;
}
/////////////////////////////////////////////////////////////////////////////////////
void CDebugInfoSystem::Remove(LPCSTR key)
{
    auto ptr = datamap.find(key);
    if (ptr != datamap.end())
        datamap.erase(ptr);
}
/////////////////////////////////////////////////////////////////////////////////////
void CDebugInfoSystem::Init(CInterface3D *pI)
{
    pI3D = pI;
    pSurface = 0;
    bVisible = false;

    pDebugFont = new CFont();
    if (!pDebugFont->CreatePointFont(90, _T("Courier New")))
    {
        delete pDebugFont;
        pDebugFont = 0;
    }

    SetRect(CRect(10, 10, 290, 290));
}
/////////////////////////////////////////////////////////////////////////////////////
void CDebugInfoSystem::Fini()
{
    if (pSurface)
        delete pSurface;
    pSurface = 0;
    if (pDebugFont)
        delete pDebugFont;
    pDebugFont = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CDebugInfoSystem::SetRect(CRect rc)
{
    int w = pI3D->GetWidth(), h = pI3D->GetHeight();

    rcReal = rc;
    pI3D->VirtualToScreen(rcReal);

    int nWidth = rcReal.Width();
    int nHeight = rcReal.Height();

    if (nWidth < DEBUGINFO_MIN_WIDTH)
        nWidth = DEBUGINFO_MIN_WIDTH;
    if (nWidth > w)
        nWidth = w;

    if (nHeight < DEBUGINFO_MIN_HEIGHT)
        nHeight = DEBUGINFO_MIN_HEIGHT;
    if (nHeight > h)
        nHeight = h;

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
void CDebugInfoSystem::Draw2D()
{
    DWORD dwCurrentTime = timeGetTime();
    if (dwCurrentTime - dwLastRedrawTime > DEBUGINFO_DELAY)
    {
        dwLastRedrawTime = dwCurrentTime;
        RedrawSurface();
    }
    if (pSurface)
        pSurface->Draw();
}
/////////////////////////////////////////////////////////////////////////////////////
void CDebugInfoSystem::RedrawSurface()
{
    if (!pSurface)
        return;

#ifndef NOGETDC
    CDC *pDC = pSurface->GetDC();
    if (!pDC)
        return;
    pDC->FillSolidRect(0, 0, rcReal.Width(), rcReal.Height(), RGB(0, 0, 0));
    pDC->SetBkMode(TRANSPARENT);
    CFont *pOldFont = pDC->SelectObject(pDebugFont);

    CRect rc1(0, 0, rcReal.Width() / 2, DEBUGINFO_LINE_HEIGHT + 5);
    CRect rc2(rcReal.Width() / 2, 0, rcReal.Width(), DEBUGINFO_LINE_HEIGHT + 5);

    std::map<std::string, std::string>::iterator it;
    for (it = datamap.begin(); it != datamap.end(); ++it)
    {
        pDC->SetTextColor(RGB(8, 8, 8));
        pDC->DrawText(it->first.c_str(), it->first.length(), rc1 + CPoint(1, 1),
                      DT_LEFT | DT_NOPREFIX);
        pDC->DrawText(it->second.c_str(), it->second.length(), rc2 + CPoint(1, 1),
                      DT_LEFT | DT_NOPREFIX);

        pDC->SetTextColor(DEBUGINFO_COLOR_TEXT);
        pDC->DrawText(it->first.c_str(), it->first.length(), rc1, DT_LEFT | DT_NOPREFIX);
        pDC->DrawText(it->second.c_str(), it->second.length(), rc2, DT_LEFT | DT_NOPREFIX);

        rc1 += CPoint(0, DEBUGINFO_LINE_HEIGHT);
        rc2 += CPoint(0, DEBUGINFO_LINE_HEIGHT);
        if (rc1.bottom > rcReal.Height())
            break;
    }
    pDC->SelectObject(pOldFont);

    pSurface->ReleaseDC();
#endif // NOGETDC
}
/////////////////////////////////////////////////////////////////////////////////////
void CDebugInfoSystem::Show(bool bShow)
{
    bVisible = bShow;
    if (bShow)
    {
        if (!pSurface)
            pSurface = dbgnew CI3DSurface(pI3D, rcVirtual);
        dwLastRedrawTime = timeGetTime();
        RedrawSurface();
    }
    else
    {
        if (pSurface)
            delete pSurface;
        pSurface = 0;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
#if 0
void CDebugInfoSystem::Draw( int x, int y, int xstep, int ystep, HDC hDC )
{
	std::map <std::string, std::string>::iterator it;
	int i;

	for ( i=0, it = datamap.begin(); it != datamap.end(); it++, i++ )
	{
		TextOut( hDC, x        , y + ystep*i, it->first.c_str() , it->first.length() );
		TextOut( hDC, x + xstep, y + ystep*i, it->second.c_str(), it->second.length() );
	}
}
#endif
/////////////////////////////////////////////////////////////////////////////////////
