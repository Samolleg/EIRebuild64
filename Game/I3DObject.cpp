#include "StdAfx.h"
#include "WideScreen.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "I3DObject.h"
#include "I3DMapObjTranslation.h"
#include "I3DMain.h"
#include "I3DToolTip.h"

#include "Renderer.h"

#include "MaterialManager.h"
#include "FigureManager.h"
#include "TextureManager.h"
#include "TextManager.h"

#include "WorldClient.h"
#include "dx11/RendererDX11.h"
#include "dx11/Surface.h"
/////////////////////////////////////////////////////////////////////////////////////
CI3DPolyArea::CI3DPolyArea(CInterface3D *pI)
    : pI3D(pI)
{
    pTip = 0;
    bIsValid = false;
    bIsRect = false;
    shift.x = 0;
    shift.y = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
CI3DPolyArea::~CI3DPolyArea()
{
    if (pTip)
        delete pTip;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CI3DPolyArea::IsToolTipActive()
{
    if (pTip)
        return pTip->IsActive();
    else
        return false;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DPolyArea::UpdateToolTip(LPCTSTR pszText)
{
    if (!pTip)
        return;
    pTip->UpdateText(pszText);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DPolyArea::SetToolTip(LPCTSTR pszText, int nHeaderLines, int nKbdAction)
{
    if (pTip)
        delete pTip;
    pTip = dbgnew CI3DToolTip(pI3D, this, pszText, nHeaderLines, nKbdAction);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DPolyArea::SetToolTip(int nTipID, int nHeaderLines, int nKbdAction)
{
    if (!nTipID)
    {
        SetToolTip("");
        return;
    }
    CString szTipName;
    szTipName.Format("tip %d", nTipID);
    if (theWorld.GetTextManager()->GetText(szTipName))
        theWorld.GetTextManager()->GetUntilNextCommand(szTipName);
    SetToolTip(szTipName, nHeaderLines, nKbdAction);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DPolyArea::ProcessIdle(CPoint point)
{
    if (pTip)
    {
        if (PtInArea(point))
        {
            if (!pTip->IsActive())
                pTip->Activate();
        }
        else
        {
            if (pTip->IsActive())
                pTip->Deactivate();
        }
        pTip->Process(point);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CI3DPolyArea::PtInArea(CPoint point)
{
    point -= shift;
    if (!bIsValid || !rect.PtInRect(point))
        return FALSE;
    if (bIsRect)
        return TRUE;
    CPoint p1, p2;
    int size = points.GetSize();
    for (int i = 0; i < size; i++)
    {
        p1 = points[i];
        p2 = points[(i + 1) % size];
        if (point.x * p1.y + p1.x * p2.y + p2.x * point.y <
            point.x * p2.y + p2.x * p1.y + p1.x * point.y)
            return FALSE;
    }
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DPolyArea::MakeRect(int x1, int y1, int x2, int y2)
{
    x1 += WideScreen::currentShift;
    x2 += WideScreen::currentShift;
    points.RemoveAll();
    rect.left = x1;
    rect.top = y1;
    rect.right = x2;
    rect.bottom = y2;
    bIsValid = true;
    bIsRect = true;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DPolyArea::Clear()
{
    points.RemoveAll();
    bIsValid = false;
    bIsRect = false;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DPolyArea::AddPoint(CPoint point)
{
    point.x += WideScreen::currentShift;
    if (!points.GetSize())
    {
        rect.left = point.x;
        rect.top = point.y;
        rect.right = point.x;
        rect.bottom = point.y;
    }
    else
    {
        rect.left = Min(point.x, rect.left);
        rect.top = Min(point.y, rect.top);
        rect.right = Max(point.x, rect.right);
        rect.bottom = Max(point.y, rect.bottom);
    }
    points.Add(point);
    if (points.GetSize() > 2)
        bIsValid = true;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DPolyArea::Draw(CRenderer *pRend)
{
    if (pTip)
        pTip->Draw(pRend);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DPolyArea::Draw2D()
{
    if (pTip)
        pTip->Draw2D();

    if (!bIsValid)
        return;

    // AlexK: this is debug tool for button bounding boxes - outlines them with red lines

    // HPEN hPen, hOldPen;
    // HDC hDC;
    // CPoint pt;
    // DWORD w, h, bpp;
    // int i;

    // theRenderer.GetD3D()->GetCurrentMode( w, h, bpp );

    //// create red pen
    // hPen = CreatePen( PS_SOLID, 1, RGB(255,0,0) );
    //// get dc and select red pen

    // auto swapchain = dx11->GetSwapChain();
    // Microsoft::WRL::ComPtr<ID3D11Texture2D> backbuffer;
    // IfFailedThrowHR(swapchain->GetBuffer(0, IID_PPV_ARGS(&backbuffer)));
    // IDXGISurface1 *surf;
    // backbuffer->QueryInterface(IID_PPV_ARGS(&surf));
    // surf->AcquireDC(FALSE, &hDC);

    // hOldPen = (HPEN)SelectObject( hDC, hPen );

    // if ( bIsRect )
    //{
    //        rect += shift;
    //        MoveToEx( hDC, rect.left*w/800, rect.top*h/600, NULL );
    //        LineTo( hDC, rect.right*w/800, rect.top*h/600 );
    //        LineTo( hDC, rect.right*w/800, rect.bottom*h/600 );
    //        LineTo( hDC, rect.left*w/800, rect.bottom*h/600 );
    //        LineTo( hDC, rect.left*w/800, rect.top*h/600 );
    //        rect -= shift;
    //}
    // else if ( points.GetSize() > 0 )
    //{
    //        pt = points[ points.GetSize() - 1 ];
    //        pt += shift;
    //        MoveToEx( hDC, pt.x*w/800, pt.y*h/600, NULL );
    //        for ( i=0; i<points.GetSize(); i++ )
    //        {
    //                pt = points[i];
    //                pt += shift;
    //                LineTo( hDC, pt.x*w/800, pt.y*h/600 );
    //        }
    //}

    //// select old pen and release dc
    // SelectObject( hDC, hOldPen );
    // surf->ReleaseDC(nullptr);
    //// delete red pen
    // DeleteObject( hPen );
}
/////////////////////////////////////////////////////////////////////////////////////
CI3DSurface::CI3DSurface(CInterface3D *pI, int x1, int y1, int x2, int y2,
                         EBlitStyleFlag eBlitFlags, bool bVirtual)
{
    colorKey = 0;
    pI3D = pI;
    dwBlitFlags = eBlitFlags;
    rect.left = x1;
    rect.top = y1;
    rect.right = x2;
    rect.bottom = y2;
    if (bVirtual)
        pI3D->VirtualToScreen(rect);
    nRealWidth = rect.Width();
    nRealHeight = rect.Height();
    pSurface = std::make_unique<CDXGISurface>(pI3D->GetD3D(), nRealWidth, nRealHeight);
    nCopyBytes = 0;
    Show(TRUE);
}
/////////////////////////////////////////////////////////////////////////////////////
CI3DSurface::CI3DSurface(CInterface3D *pI, CRect rc, EBlitStyleFlag eBlitFlags)
{
    colorKey = 0;
    pI3D = pI;
    dwBlitFlags = eBlitFlags;
    rect = rc;
    pI3D->VirtualToScreen(rect);
    nRealWidth = rect.Width();
    nRealHeight = rect.Height();
    pSurface = std::make_unique<CDXGISurface>(pI3D->GetD3D(), nRealWidth, nRealHeight);
    nCopyBytes = 0;
    Show(TRUE);
}
/////////////////////////////////////////////////////////////////////////////////////
CI3DSurface::CI3DSurface(CInterface3D *pI, CString szName)
{
    colorKey = 0;
    pI3D = pI;
    SMMPFileHeader *pHdr = (SMMPFileHeader *)(theWorld.GetTextureManager()->GetFile(szName +
                                                                                    ".mmp"));
    pSurface = std::make_unique<CDXGISurface>(pI3D->GetD3D(), pHdr);
    theWorld.GetTextureManager()->CloseFile((BYTE *)pHdr);
    nRealWidth = pSurface->GetWidth();
    nRealHeight = pSurface->GetHeight();
    rect.SetRect(0, 0, pSurface->GetWidth(), pSurface->GetHeight());
    nCopyBytes = 0;
    SetFlag(BS_COLOR_KEY, TRUE);
}
std::unique_ptr<DWORD[]> pixelBuffer;
/////////////////////////////////////////////////////////////////////////////////////
DWORD CI3DSurface::GetPixel(int nX, int nY)
{
    if (!pSurface || nX < 0 || nY < 0 || nX >= pSurface->GetWidth() || nY >= pSurface->GetHeight())
        return 0;
    int width = pSurface->GetWidth();
    if (!pixelBuffer)
    pixelBuffer = pSurface->GetPixelBuffer();
    if (!pixelBuffer) return 0;
    int pixelIdx = nY * width + nX;
    return pixelBuffer[pixelIdx];

    //return pSurface->GetPixel(nX, nY);
    /*HDC hDc;
    pSurface->AcquireDC(&hDc);
    GetPixel(hDc, 0, 0);
    pSurface->ReleaseDC(hDc);*/

    //GetPixel(hDc, nX, nY);
#ifndef NOGETDC
    return 0;
#else
    return 0;
#endif // NOGETDC
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DSurface::SetRect(CRect rectr)
{
    rect = rectr;
    pI3D->VirtualToScreen(rect);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DSurface::GetFromScreen()
{
    if (!pSurface)
        return;
    // must match screen's sampling
    pSurface->BltFromRenderSurface(0, rect, 0);
    pixelBuffer = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DSurface::Fade()
{       
    pSurface->Fade();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DSurface::StretchTo(CDXGISurface *result, const RECT &rect)
{
    if (!pSurface)
        return;

    pSurface->StretchTo(result, rect);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DSurface::Show(BOOL bShow, BOOL bNeedStore)
{
    SetFlag(BS_VISIBLE, bShow);
    if (bShow && pCopy)
        MemoryBackup(TRUE);
    if (!bShow && bNeedStore)
        MemoryBackup(FALSE);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DSurface::MemoryBackup(BOOL bShow)
{
    if (bShow)
    {
        if (pSurface == 0)
        {
            pSurface = std::make_unique<CDXGISurface>(pI3D->GetD3D(), nRealWidth,
                                                      nRealHeight);
        }
        if (pCopy)
        {
            // restore from copy and delete it
            pSurface->CopyResource(pCopy.get());
            pCopy.reset();
        }
    }
    else if (!bShow && pSurface != 0)
    {
        // make surface copy
        pCopy.reset();

        D3D11_TEXTURE2D_DESC tex2d;
        pSurface->GetTexture()->GetDesc(&tex2d);
        pCopy = std::make_unique<CDXGISurface>(pI3D->GetD3D(), &tex2d);
        pCopy->CopyResource(pSurface.get());
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DSurface::Draw()
{
#ifndef NOBLIT
    if (!pSurface || !HasFlag(BS_VISIBLE))
        return;

    CRect rectScreen(0, 0, pI3D->GetWidth(), pI3D->GetHeight());
    rectScreen.IntersectRect(rectScreen, rect);

    if (rectScreen.IsRectEmpty())
        return;

    // Warning: Stretch-енные картинки не clip-аются !!!
    CRect rectSrc(CPoint(rectScreen.TopLeft() - rect.TopLeft()), rectScreen.Size());
    LPRECT pSrcRect = NULL;
    if (!HasFlag(BS_STRETCH))
        pSrcRect = rectSrc;

    if (HasFlag(BS_COLOR_KEY))
    {
        // DDBLTFX ddbltfx;
        // ddbltfx.ddckSrcColorkey.dwColorSpaceHighValue = colorKey;
        // ddbltfx.ddckSrcColorkey.dwColorSpaceLowValue = colorKey;
        pSurface->BltToRenderSurface(rectScreen, pSrcRect,
                                     DDBLT_KEYSRCOVERRIDE | DDBLT_ASYNC | DDBLT_WAIT, 0);
    }
    else
    {
        HRESULT dxrval = pSurface->BltToRenderSurface(rectScreen, pSrcRect,
                                                      DDBLT_ASYNC | DDBLT_WAIT, 0);
        if (dxrval != DD_OK)
            ReportWarning(dxrval, "Can't blit I3D surface to screen");
    }
#endif // NOBLIT
}
/////////////////////////////////////////////////////////////////////////////////////
CDC *CI3DSurface::GetDC()
{
#ifdef DXGI_ENABLED
    if (pSurface)
    {
        ASSERT(pSurface->GetBPP() == 32);
        hDC = 0;
        if (!pSurface->AcquireDC(&hDC))
            return NULL;
        return CDC::FromHandle(hDC);
    }
#endif // NOGETDC
    return NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DSurface::ReleaseDC()
{
#ifdef DXGI_ENABLED
    if (pSurface)
        pSurface->ReleaseDC(hDC);
#endif // NOGETDC
    hDC = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DSurface::DrawRect(const CRect &rc, COLORREF crColor)
{
    if (!pSurface)
        return;
#ifndef NOGETDC
    CRect rcDraw;
    CBrush brush;
    CDC *pDC = CDC::FromHandle(hDC);
    if (!pDC)
        return;
    if (rc.IsRectEmpty())
        rcDraw = CRect(0, 0, rect.Width(), rect.Height());
    else
    {
        rcDraw = rc;
        pI3D->VirtualToScreenLocal(rcDraw);
    }

    brush.CreateSolidBrush(crColor);
    pDC->FrameRect(rcDraw, &brush);
#endif // NOGETDC
}

int CI3DSurface::DrawText(LPCTSTR pszStr, const CRect &rc, int nFont, COLORREF crColor, int nFormat,
                          BOOL bShadow)
{
    if (!pSurface)
        return 0;
#ifdef DXGI_ENABLED
    CPoint shift(1, 1);
    CRect rcDraw;
    CDC *pDC = CDC::FromHandle(hDC);
    if (!pDC)
        return 0;
    if (rc.IsRectEmpty())
        rcDraw = CRect(0, 0, rect.Width(), rect.Height());
    else
    {
        rcDraw = rc;
        pI3D->VirtualToScreenLocal(rcDraw);
        rcDraw.bottom += 2;
        rcDraw.right += 2;
    }
    CFont *pOldFont = pDC->SelectObject(pI3D->GetFont(nFont));
    pDC->SetBkMode(TRANSPARENT);
    const char *mytext = pszStr;
    if (bShadow)
    {
        pDC->SetTextColor(RGB(8, 8, 8));
        pDC->DrawText(pszStr, -1, rcDraw + shift, nFormat);
    }
    pDC->SetTextColor(crColor);
    CSize size;
    size.cy = pDC->DrawText(pszStr, -1, rcDraw, nFormat);
    pDC->SelectObject(pOldFont);
    pI3D->ScreenToVirtual(size);
    return size.cy;
#else
    return 0;
#endif // NOGETDC
}
/////////////////////////////////////////////////////////////////////////////////////
int CI3DSurface::DrawMultiText(LPCTSTR pszStr, CRect rc, int nLines, int nFont, COLORREF crColor,
                               int nFormat, BOOL bShadow, int nLineStep)
{
    if (!pSurface)
        return 0;
#ifdef DXGI_ENABLED
    int nWidth = rc.Width();

    CString szChar;
    CString szWord;
    CString szLine;
    CString szOrigin = pszStr;
    szOrigin += "\n";
    CStringArray szFText;
    unsigned int iChar;
    LPCTSTR ptr = (LPCTSTR)szOrigin;
    int nLine = 0;

    while (_tcsnextc(ptr) != 0)
    {
        if (isleadbyte(*ptr))
        {
            szChar = *ptr;
            szChar += *(ptr + 1);
        }
        else
            szChar = *ptr;

        switch (iChar = _tcsnextc(ptr))
        {
        case ' ':
            if (pI3D->TextSize(szLine + szWord, nFont).cx > nWidth)
            {
                szFText.Add(szLine);
                szLine = szWord + " ";
            }
            else
            {
                szLine += szWord;
                szLine += " ";
            }
            szWord.Empty();
            break;

        case '\n':
            if (pI3D->TextSize(szLine + szWord, nFont).cx > nWidth)
            {
                szFText.Add(szLine);
                szFText.Add(szWord);
            }
            else
            {
                szLine += szWord;
                szFText.Add(szLine);
            }
            szLine.Empty();
            szWord.Empty();
            break;

        default:
            szWord += szChar;
        }
        ptr = _tcsinc(ptr);
    }

    CRect rcTmp = rc;
    CPoint shift(0, nLineStep > 0 ? nLineStep : rc.Height());
    for (int i = 0; i < nLines && i < szFText.GetSize(); i++)
    {
        DrawText(szFText[i], rcTmp, nFont, crColor, nFormat, bShadow);
        rcTmp += shift;
    }
    return szFText.GetSize();
#else
    return 0;
#endif // NOGETDC
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DSurface::Clear()
{
    if (!pSurface)
        return;
#ifdef DXGI_ENABLED
    if (!hDC)
        return;
    // pSurface->Clear(0);
    CDC *pDC = CDC::FromHandle(hDC);
    pDC->FillSolidRect(rect - rect.TopLeft(), colorKey);
#endif // NOGETDC
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DSurface::Fill(COLORREF crColor)
{
    if (!pSurface)
        return;
#ifdef DXGI_ENABLED
    CDC *pDC = CDC::FromHandle(hDC);
    if (!pDC)
        return;
    pDC->FillSolidRect(rect - rect.TopLeft(), crColor);
#endif // NOGETDC
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DSurface::Move(CPoint ptLeftTop)
{
    pI3D->VirtualToScreen(ptLeftTop);
    rect = CRect(ptLeftTop, CSize(rect.Width(), rect.Height()));
}
/////////////////////////////////////////////////////////////////////////////////////
CI3DObject::CI3DObject(CInterface3D *pI)
    : CI3DHandler(pI)
{
    ResetAllFlags();
    rect = CRect(0, 0, 0, 0);
    SetOrientation(QNULL);
    SetCP(CPlot3D(0, 0, 0));

    pMat3 = NULL;
    CMaterialManager *pMatMng = (CMaterialManager *)theWorld.GetMaterialManager();
    pMat2 = pMatMng->CreateMaterial(MTID_STONE);
    pMatMng->SetMaterial(pMat2, MTID_STONE);
    pMat2->SetEmissive(D3DVECTOR(1, 1, 1));
    pMat2->AddRef();
    pMat1 = pMatMng->CreateMaterial(MTID_STONE);
    pMatMng->SetMaterial(pMat1, MTID_STONE);
    pMat1->SetEmissive(D3DVECTOR(0, 0, 0));
    pMat1->AddRef();

    Show(TRUE);
}
/////////////////////////////////////////////////////////////////////////////////////
CI3DObject::~CI3DObject()
{
    if (pMat1)
        pMat1->Release();
    pMat1 = NULL;
    if (pMat2)
        pMat2->Release();
    pMat2 = NULL;
    if (pMat3)
        pMat3->Release();
    pMat3 = NULL;

    RemoveAllFigures();
    RemoveAllSurfaces();
    RemoveAllAreas();
    RemoveAllTextures();
    RemoveAllMapObjTranslations();
    ResetAllFlags();
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CI3DObject::Draw(CRenderer *pRend)
{
    int k;

    for (k = 0; k < figures.GetSize(); k++)
        figures[k]->Draw(pRend);

    for (k = 0; k < m_aMapObjTaranslation.GetSize(); k++)
        m_aMapObjTaranslation[k]->Draw(pRend);

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObject::DrawTips(CRenderer *pRend)
{
    CPoint pt = pI3D->GetMousePos();
    pI3D->ScreenToVirtual(pt);
    for (int k = 0; k < areas.GetSize(); k++)
    {
        areas[k]->ProcessIdle(pt);
        // areas[k]->Draw( pRend );
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObject::Draw2D()
{
    for (int k = 0; k < surfaces.GetSize(); k++)
        if (surfaces[k]->IsVisible())
            surfaces[k]->Draw();

    // for (int k = 0; k < areas.GetSize(); k++ )
    // areas[k]->Draw2D();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObject::Draw2DTips()
{
    CPoint pt = pI3D->GetMousePos();
    pI3D->ScreenToVirtual(pt);
    for (int k = 0; k < areas.GetSize(); k++)
    {
        areas[k]->ProcessIdle(pt);
        areas[k]->Draw2D();
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObject::SetTexture(LPTEXTURE pTex)
{
    if (!pTex)
        return;
    textures.Add(pTex);
    pTex->AddRef();
}
/////////////////////////////////////////////////////////////////////////////////////
LPTEXTURE CI3DObject::AddTexture(LPCTSTR pszTexName, ETypeIDs eTextureTypeID, LPCTSTR pszDir)
{
    CTextureManager *pTexMng = (CTextureManager *)theWorld.GetTextureManager();
    LPTEXTURE pTex = pTexMng->CreateTexture(eTextureTypeID, pszTexName);
    if (!pTex)
        return NULL;
    BOOL bLoaded = FALSE;
    if (!pszDir)
        bLoaded = pTexMng->LoadTexture(pTex);
    else
        bLoaded = pTexMng->LoadTexture(pTex, pszDir);
    if (bLoaded)
    {
        textures.Add(pTex);
        pTex->AddRef();
        return pTex;
    }
    else
        return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
LPTEXTURE CI3DObject::AddTexture(int nSizeX, int nSizeY, ETypeIDs eTextureTypeID)
{
    CTextureManager *pTexMng = (CTextureManager *)theWorld.GetTextureManager();
    LPTEXTURE pTex = pTexMng->CreateInterfaceTexture(nSizeX, nSizeY, eTextureTypeID);
    if (!pTex)
        return NULL;
    textures.Add(pTex);
    pTex->AddRef();
    pTex->SetFlag(TEX_CANT_UNLOAD);
    return pTex;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObject::RemoveTexture(LPTEXTURE pTex)
{
    if (!pTex)
        return;
    for (int i = 0; i < textures.GetSize(); i++)
        if (textures[i] == pTex)
        {
            textures[i]->Release();
            textures.RemoveAt(i);
            return;
        }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObject::RemoveTexture(int num)
{
    textures[num]->Release();
    textures.RemoveAt(num);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObject::RemoveAllTextures()
{
    for (int i = 0; i < textures.GetSize(); i++)
        textures[i]->Release();
    textures.RemoveAll();
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CI3DObject::AddFigure(CI3DFigure *lpFigure)
{
    // scan for such figure
    for (int i = 0; i < figures.GetSize(); i++)
    {
        if (figures[i] == lpFigure)
            return TRUE;
    }
    figures.Add(lpFigure);
    lpFigure->AddRef();
    lpFigure->SetObject3D(this);
    return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
CI3DFigure *CI3DObject::AddFigure(LPCTSTR pszFigName)
{
    CI3DFigure *pFig;
    CFigureManager *pFigMng = (CFigureManager *)theWorld.GetFigureManager();
    pFig = (CI3DFigure *)pFigMng->CreateI3DFigure(pszFigName);
    if (!pFig)
        return NULL;
    AddFigure(pFig);
    pFig->SetMaterial(pMat1);
    if (textures.GetSize() > 0)
        pFig->SetTexture(textures[0]);
    else
        pFig->SetTexture(NULL);
    return pFig;
}
/////////////////////////////////////////////////////////////////////////////////////
CI3DFigure *CI3DObject::ChangeFigure(CI3DFigure *lpFigure, LPCTSTR pszFigName)
{
    for (int i = 0; i < figures.GetSize(); i++)
        if (figures[i] == lpFigure)
        {
            CI3DFigure *pFig;
            CFigureManager *pFigMng = (CFigureManager *)theWorld.GetFigureManager();
            pFig = (CI3DFigure *)pFigMng->CreateI3DFigure(pszFigName);
            if (!pFig)
                return 0;
            pFig->AddRef();
            pFig->SetObject3D(this);
            pFig->SetMaterial(pMat1);
            if (textures.GetSize() > 0)
                pFig->SetTexture(textures[0]);
            else
                pFig->SetTexture(NULL);

            pFig->SetCP(lpFigure->GetCP());
            pFig->SetUShift(lpFigure->GetUShift());
            pFig->SetVShift(lpFigure->GetVShift());
            pFig->SetOrientation(lpFigure->GetOrientation());
            pFig->SetTexture(lpFigure->GetTexture());
            pFig->SetScale(lpFigure->GetXScale());

            figures.RemoveAt(i);
            lpFigure->Release();
            figures.InsertAt(i, pFig);

            return pFig;
        }
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CI3DObject::RemoveFigure(CI3DFigure *lpFigure)
{
    int j;
    // search in figures
    for (j = 0; j < figures.GetSize(); j++)
    {
        if (figures[j] == lpFigure)
        {
            figures.RemoveAt(j);
            lpFigure->Release();
            return TRUE;
        }
    }
    return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObject::RemoveFigure(int num)
{
    figures[num]->Release();
    figures.RemoveAt(num);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObject::RemoveAllFigures()
{
    // remove figures
    for (int i = 0; i < figures.GetSize(); i++)
        figures[i]->Release();
    figures.RemoveAll();
}
/////////////////////////////////////////////////////////////////////////////////////
CI3DPolyArea *CI3DObject::AddArea()
{
    CI3DPolyArea *pArea = dbgnew CI3DPolyArea(pI3D);
    areas.Add(pArea);
    return pArea;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObject::RemoveArea(int num)
{
    delete areas[num];
    areas.RemoveAt(num);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObject::RemoveAllAreas()
{
    for (int i = 0; i < areas.GetSize(); i++)
        delete areas[i];
    areas.RemoveAll();
}
/////////////////////////////////////////////////////////////////////////////////////
CI3DSurface *CI3DObject::AddSurface(CI3DSurface *pSurface)
{
    if (pSurface)
        surfaces.Add(pSurface);
    return pSurface;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObject::RemoveSurface(int num)
{
    delete surfaces[num];
    surfaces.RemoveAt(num);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObject::RemoveAllSurfaces()
{
    for (int i = 0; i < surfaces.GetSize(); i++)
        delete surfaces[i];
    surfaces.RemoveAll();
}
/////////////////////////////////////////////////////////////////////////////////////
int CI3DObject::HitTest(CPoint pt)
{
    CPoint point(pt);
    pI3D->ScreenToVirtual(point);
    for (int i = 0; i < areas.GetSize(); i++)
        if (areas[i]->PtInArea(point))
            return i;
    return -1;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CI3DObject::IsInside(CPoint pt)
{
    CPoint point(pt);
    pI3D->ScreenToVirtual(point);
    return rect.PtInRect(point);
}
/////////////////////////////////////////////////////////////////////////////////////
CI3DMapObjTranslation *CI3DObject::AddMapObjTranslation()
{
    CI3DMapObjTranslation *pMapObj = dbgnew CI3DMapObjTranslation();
    if (!pMapObj)
        return NULL;
    m_aMapObjTaranslation.Add(pMapObj);
    return pMapObj;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObject::RemoveAllMapObjTranslations()
{
    for (int i = 0; i < m_aMapObjTaranslation.GetSize(); i++)
        delete m_aMapObjTaranslation[i];
    m_aMapObjTaranslation.RemoveAll();
}
/////////////////////////////////////////////////////////////////////////////////////
bool PasteTexture(LPTEXTURE pDstTex, LPCTSTR pszTexName, int nSrcSizeX, int nSrcSizeY, int nSizeX,
                  int nSizeY, int nDstX, int nDstY, int nSrcX, int nSrcY)
{
    CDXGISurface *pSurface = pDstTex->GetSurface();
    CString resourceName(pszTexName);
    if (resourceName.Right(4).CompareNoCase(".mmp") != 0)
        resourceName += ".mmp";
    LPBYTE pTexData = ((CTextureManager *)(theWorld.GetTextureManager()))
                          ->GetMMPData(resourceName);

    SMMPFileHeader *pHdr = (SMMPFileHeader *)pTexData;
    std::unique_ptr<DWORD[]> pRGBA = 0;
    std::unique_ptr<WORD[]> pSixteenBit = 0;
    if (!pTexData)
        return FALSE;

    // read from MMP. better hope it's RGBA.
    pRGBA = std::make_unique<DWORD[]>(nSrcSizeX * nSrcSizeY);
    memset(pRGBA.get(), 0, nSrcSizeX * nSrcSizeY * 4);
    if (((SMMPFileHeader *)pTexData)->dwFormat == MMP_FORMAT_PNT3)
    {
        theRenderer.GetGraphicsProcessor()->PutOneLayer(
            pRGBA.get(), (LPDWORD)(pTexData + sizeof(SMMPFileHeader)), nSrcSizeX * nSrcSizeY, true);
    }
    else
    {
        theRenderer.GetGraphicsProcessor()->PutOneLayer(pRGBA.get(),
                                                        (LPDWORD)(pTexData +
                                                                  sizeof(SMMPFileHeader)),
                                                        nSrcSizeX * nSrcSizeY, false);
    }
    // convert to 16 bit...5551?
    pSixteenBit = std::make_unique<WORD[]>(nSrcSizeX * nSrcSizeY);
    DWORD pitch = 16 * nSrcSizeX / 8;
    LPWORD pDst = pSixteenBit.get();
    LPDWORD pSrc = pRGBA.get();
    DWORD dwColor, a, r, g, b;
    for (int j = 0; j < nSrcSizeX; j++)
    {
        for (int i = 0; i < nSrcSizeY; i++)
        {
            dwColor = *pSrc++;
            a = (dwColor >> 16) & 0x8000;
            r = (dwColor >> 9) & 0x7C00;
            g = (dwColor >> 6) & 0x03E0;
            b = (dwColor >> 3) & 0x001F;
            *pDst++ = WORD(a | r | g | b);
        }
    }

    D3D11_BOX region = { 0 };
    region.left = nDstX;
    region.right = nDstX + nSizeX;
    region.top = nDstY;
    region.bottom = nDstY + nSizeY;
    region.front = 0;
    region.back = 1;

    D3D11_SUBRESOURCE_DATA srd = { 0 };
    srd.pSysMem = pSixteenBit.get();
    int bpp = 16;
    srd.SysMemPitch = bpp * nSrcSizeX / 8;

    pSurface->UpdateTexture(0, &region, &srd);
    ((CTextureManager *)(theWorld.GetTextureManager()))->CloseAllFiles();

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
float CI3DObject::GetFrameTime()
{
    return static_cast<float>(GetRenderer()->GetLastFrameTime()) / 1000.0f;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObject::Show(BOOL bShow, BOOL bNeedStore)
{
    bVisible = bShow;
    if (bNeedStore)
        MemoryBackup(bShow);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObject::MemoryBackup(BOOL bShow)
{
    if (bShow)
    {
        for (int i = 0; i < surfaces.GetSize(); i++)
            if (surfaces[i]->IsVisible())
                surfaces[i]->MemoryBackup(bShow);
    }
    else
    {
        for (int i = 0; i < surfaces.GetSize(); i++)
            surfaces[i]->MemoryBackup(bShow);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
