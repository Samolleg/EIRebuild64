#include "StdAfx.h"
#include "WideScreen.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "I3DMain.h"
#include "I3DToolTip.h"
#include "OwnedRender/E3Hud.h"
#include "StringProcessing.h"
#include "I3DCursor.h"
#include "KeyMap.h"
#include "GeneralSettings.h"

#include "Console.h"
#include "dx11/RendererDX11.h"
/////////////////////////////////////////////////////////////////////////////////////
const CPoint ptIncTip(24, 24);
const int N_TOOLTIP_FONT = 0;

const float TOOLTIP_ACTIVATE_TIME = 0; //.4f;

const int N_TEXT_R_FROM = 43;
const int N_TEXT_G_FROM = 28;
const int N_TEXT_B_FROM = 0;

const int N_TEXT_R = 228;
const int N_TEXT_G = 215;
const int N_TEXT_B = 167;

const int N_TEXT_HDR_R = 255;
const int N_TEXT_HDR_G = 179;
const int N_TEXT_HDR_B = 49;

const int TOOLTIP_ALPHA = 255;
/////////////////////////////////////////////////////////////////////////////////////
CI3DToolTip::CI3DToolTip(CInterface3D *pI, CI3DPolyArea *pArea, LPCTSTR pszText, int nHeader,
                         int nAction)
    : CI3DObject(pI)
{
    m_fIdleTime = 0;
    m_eMode = TT_HIDE;

    m_pArea = pArea;

    if (!pszText || pszText[0] == 0)
        return;

    szStrings.RemoveAll();

    NStrProc::SplitString(pszText, szStrings, '\n');

    m_sizeTip.cx = 0;
    m_sizeTip.cy = 0;
    nStringHeight = 0;
    nHeaderLines = nHeader;

    nKbdAction = nAction;
    pszCurrentKeyName = 0;
    nFirstLineWidth = 0;

    for (int i = 0; i < szStrings.GetSize(); i++)
    {
        CSize size;
        if (szStrings[i].IsEmpty() && i < szStrings.GetSize() - 1)
        {
            size = pI3D->TextSize("w", N_TOOLTIP_FONT);
            size.cx = 0;
        }
        else
            size = pI3D->TextSize(szStrings[i], N_TOOLTIP_FONT);
        if (size.cx > m_sizeTip.cx)
            m_sizeTip.cx = size.cx;
        if (size.cy > 0)
        {
            m_sizeTip.cy += size.cy;
            nStringHeight = size.cy;
        }
    }

    m_sizeTip.cx += 12;
    m_sizeTip.cy += 8;
    /*
            CRect rect( CPoint(0,0), m_sizeTip );
            CI3DFigure *pFigure = AddFigure();
            pFigure->MakeBar( 2, 100, (float)rect.left, (float)rect.top, (float)rect.right,
       (float)rect.bottom ); pFigure->SetDefaultColor( RGBA_MAKE(0,0,200,0) );
            pFigure->UseDefaultColors( TRUE );

            AddSurface( dbgnew CI3DSurface( pI3D, rect ) );
            */
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DToolTip::UpdateText(LPCTSTR pszText)
{
    if (!pszText || pszText[0] == 0)
        return;
    szStrings.RemoveAll();
    NStrProc::SplitString(pszText, szStrings, '\n');
    Update();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DToolTip::Process(CPoint point)
{
    switch (m_eMode)
    {
    case TT_HIDE:
        break;

    case TT_SHOW:
        break;

    case TT_WAIT:
        if (szStrings.GetSize() == 0 || theSettings.fToolTipTime == 0)
        {
            m_eMode = TT_HIDE;
            break;
        }
        if (m_fIdleTime >= theSettings.fToolTipTime)
        {
            nFirstLineWidth = 0;
            if (nKbdAction)
            {
                pszCurrentKeyName = NKeyMap::GetKeyNameByAction((EKbdActions)nKbdAction);
                if (pszCurrentKeyName)
                    nFirstLineWidth = pI3D->TextSize(szStrings[0] + "  ( " + pszCurrentKeyName +
                                                         " )",
                                                     N_TOOLTIP_FONT)
                                          .cx +
                                      12;
            }

            m_eMode = TT_INC;
            m_fIdleTime = 0;

            int nWidth = (m_sizeTip.cx > nFirstLineWidth) ? m_sizeTip.cx : nFirstLineWidth;
            CRect rect(0, 0, nWidth, m_sizeTip.cy);
            CPoint shift(ptIncTip);
            pI3D->ScreenToVirtualLocal(shift);
            rect += shift;
            rect += point;
            if (rect.right > 800 + WideScreen::VirtualShift())
            {
                rect -= CPoint(rect.right - 800 - WideScreen::VirtualShift(), 0);
                if (rect.left < -WideScreen::VirtualShift())
                    rect += CPoint(-WideScreen::VirtualShift() - rect.left, 0);
            }
            if (rect.bottom > 600)
            {
                rect -= CPoint(0, rect.bottom - point.y);
                if (rect.top < 0)
                    rect += CPoint(0, -rect.top);
            }

            CI3DSurface *pSurf;
            AddSurface(pSurf = dbgnew CI3DSurface(pI3D, rect));
            // TT_INC is drawable this frame; finish GDI writes and publish now.
            Update();
            /*
                                            CI3DFigure *pFig;
                                            pFig = AddFigure();
                                            pFig->MakeBar( 2.2f, 100, (float)rect.left,
               (float)rect.top, (float)rect.right, (float)rect.bottom ); pFig->UseDefaultColors(
               TRUE ); pFig->SetDefaultColor( RGBA_MAKE( 0, 0, 0, 0 ) );
            */
        }
        else
            m_fIdleTime += GetFrameTime();
        break;

    case TT_INC:
        if (m_fIdleTime >= TOOLTIP_ACTIVATE_TIME)
        {
            m_eMode = TT_SHOW;
            m_fIdleTime = TOOLTIP_ACTIVATE_TIME;
            Update();
        }
        else
        {
            Update();
            m_fIdleTime += GetFrameTime();
        }
        break;

    case TT_DEC:
        if (m_fIdleTime <= 0)
        {
            m_eMode = TT_HIDE;
            RemoveAllFigures();
            RemoveAllSurfaces();
        }
        else
        {
            Update();
            m_fIdleTime -= GetFrameTime();
        }
        break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DToolTip::Update()
{
    ++ownedTextRevision;
    /*
            float fPart = m_fIdleTime / TOOLTIP_ACTIVATE_TIME;
            if ( fPart < 0 )
                    fPart = 0;
            if ( fPart > 1 )
                    fPart = 1;
    */
    float fPart = 1;
    /*
            if ( figures.GetSize() )
                    GetFigure(0)->SetDefaultColor( RGBA_MAKE( 16, 16, 16, static_cast<int>(fPart *
       TOOLTIP_ALPHA) ) );
    */

    CI3DSurface *pSurface = GetSurface(0);
    if (pSurface)
    {
#ifndef NOGETDC
        CDC *pDC = pSurface->GetDC();
        if (!pDC)
            return;
        pSurface->Fill(RGB(N_TEXT_R_FROM, N_TEXT_G_FROM, N_TEXT_B_FROM));

        pSurface->DrawRect(CRect(0, 0, 0, 0), RGB(N_TEXT_HDR_R, N_TEXT_HDR_G, N_TEXT_HDR_B));

        int nWidth = (m_sizeTip.cx > nFirstLineWidth) ? m_sizeTip.cx : nFirstLineWidth;
        CRect rc(6, 4, nWidth - 6, 4 + nStringHeight);
        CPoint shift(0, nStringHeight);
        for (int i = 0; i < szStrings.GetSize(); i++)
        {
            /*
            int r, g, b;
            if ( i < nHeaderLines )
            {
                    r = Min( 255, static_cast<int>(fPart * N_TEXT_HDR_R + (1 - fPart) *
            N_TEXT_R_FROM) ); g = Min( 255, static_cast<int>(fPart * N_TEXT_HDR_G + (1 - fPart) *
            N_TEXT_G_FROM) ); b = Min( 255, static_cast<int>(fPart * N_TEXT_HDR_B + (1 - fPart) *
            N_TEXT_B_FROM) );
            }
            else
            {
                    r = Min( 255, static_cast<int>(fPart * N_TEXT_R + (1 - fPart) * N_TEXT_R_FROM)
            ); g = Min( 255, static_cast<int>(fPart * N_TEXT_G + (1 - fPart) * N_TEXT_G_FROM) ); b =
            Min( 255, static_cast<int>(fPart * N_TEXT_B + (1 - fPart) * N_TEXT_B_FROM) );
            }
            COLORREF cr = RGB( r, g, b );
            */
            COLORREF cr;
            if (i < nHeaderLines)
                cr = RGB(N_TEXT_HDR_R, N_TEXT_HDR_G, N_TEXT_HDR_B);
            else
                cr = RGB(N_TEXT_R, N_TEXT_G, N_TEXT_B);
            if (i == 0 && pszCurrentKeyName)
                pSurface->DrawText(szStrings[0] + "  ( " + pszCurrentKeyName + " )", rc,
                                   N_TOOLTIP_FONT, cr, DT_LEFT, false);
            else
                pSurface->DrawText(szStrings[i], rc, N_TOOLTIP_FONT, cr, DT_LEFT, false);
            rc += shift;
        }
        pSurface->ReleaseDC();
#endif // NOGETDC
    }
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CI3DToolTip::Draw(CRenderer *pRend)
{
    if (m_eMode != TT_WAIT && m_eMode != TT_HIDE)
        return CI3DObject::Draw(pRend);
    else
        return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DToolTip::Draw2D()
{
    if (m_eMode != TT_WAIT && m_eMode != TT_HIDE) {
        // GDI writes RGB and leaves alpha zero. A tooltip is a filled rectangle,
        // not a premultiplied sprite: otherwise its background adds to the scene.
        auto *dx = pI3D->GetD3D();
        dx->GetSpriteBatch()->End();
        dx->GetSpriteBatch()->Begin(DirectX::SpriteSortMode_Deferred, dx->GetCommonStates()->Opaque());
        if (!OwnedRender::E3ClockTooltip(this)) CI3DObject::Draw2D();
        dx->GetSpriteBatch()->End();
        dx->GetSpriteBatch()->Begin();
    }
}
/////////////////////////////////////////////////////////////////////////////////////
