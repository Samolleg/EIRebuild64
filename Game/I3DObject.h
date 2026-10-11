#pragma once

#include "Object.h"
#include "I3DFigure.h"
#include "I3DHandler.h"
#include "Material.h"
#include "Texture.h"
#include <memory>

class CInterface3D;
class CI3DToolTip;
class CDXGISurface;
/////////////////////////////////////////////////////////////////////////////////////
class CI3DPolyArea
{
    friend struct E4CampAccess;
    friend struct E3HudAccess;
private:
    CInterface3D *pI3D;
    CSimplyArray<CPoint, CPoint> points;
    CRect rect;
    bool bIsRect;
    bool bIsValid;
    CPoint shift;
    CI3DToolTip *pTip;

public:
    CI3DPolyArea(CInterface3D *pI);
    ~CI3DPolyArea();

    void Clear();
    void AddPoint(CPoint point);
    void AddPoint(int x, int y) { AddPoint(CPoint(x, y)); }
    void MakeRect(int x1, int y1, int x2, int y2);
    void MakeRect(CRect rect) { MakeRect(rect.left, rect.top, rect.right, rect.bottom); }
    void SetShift(CPoint sh) { shift = sh; }
    void SetShift(int x, int y) { SetShift(CPoint(x, y)); }

    BOOL PtInArea(CPoint point);

    const char* JournalTip() const noexcept;
    bool IsToolTipActive();
    void UpdateToolTip(LPCTSTR pszText);
    void SetToolTip(LPCTSTR pszText, int nHeaderLines = 1, int nKbdAction = 0);
    void SetToolTip(int nTipID, int nHeaderLines = 1, int nKbdAction = 0);
    void ProcessIdle(CPoint point);

    void Draw(CRenderer *pRend);
    void Draw2D();
};
/////////////////////////////////////////////////////////////////////////////////////
enum EBlitStyleFlag
{
    BS_COLOR_KEY = 1,
    BS_STRETCH = 2,
    BS_VISIBLE = 4
};
/////////////////////////////////////////////////////////////////////////////////////
class CI3DSurface
{
    friend struct E4CampAccess;
private:
    CInterface3D *pI3D;
    HDC hDC; // used when locked
    DWORD dwBlitFlags;
    std::unique_ptr<CDXGISurface> pSurface;
    std::unique_ptr<CDXGISurface> pCopy;
    std::unique_ptr<DWORD[]> pixelBuffer;
    bool pixelBufferValid = false;
    HRESULT pixelReadbackStatus = E_PENDING;
    int nRealWidth, nRealHeight;
    int nCopyBytes;

protected:
    CRect rect; // rect in real coords
public:
    COLORREF colorKey;

    CI3DSurface(CInterface3D *pI3D, CRect rc,
                EBlitStyleFlag eBlitFlags = EBlitStyleFlag(BS_VISIBLE | BS_COLOR_KEY));
    CI3DSurface(CInterface3D *pI3D, int x1, int y1, int x2, int y2,
                EBlitStyleFlag eBlitFlags = EBlitStyleFlag(BS_VISIBLE | BS_COLOR_KEY),
                bool bVirtual = true);
    CI3DSurface(CInterface3D *pI3D, CString szName);

    void SetRect(CRect rect);
    void Move(CPoint ptLeftTop);

    void Show(BOOL bShow, BOOL bNeedStore = FALSE);
    void MemoryBackup(BOOL bShow);
    BOOL IsVisible() { return HasFlag(BS_VISIBLE); }

    CDXGISurface *GetSurface() { return pSurface.get(); }

    // Blit style flag operation
    BOOL HasFlag(DWORD dwStyle) { return (dwBlitFlags & dwStyle) != 0; }
    void AddFlag(DWORD dwStyle) { dwBlitFlags |= dwStyle; }
    void SetFlag(DWORD dwStyle, BOOL bSet)
    {
        if (bSet)
            dwBlitFlags |= dwStyle;
        else
            dwBlitFlags &= ~dwStyle;
    }
    void ClearFlags() { dwBlitFlags = 0; }

    DWORD GetPixel(int nX, int nY); // in surface pixel coordinates
    HRESULT GetPixelReadbackStatus() const { return pixelReadbackStatus; }

    void StretchTo(CDXGISurface *result, const RECT &rect);

    void GetFromScreen();
    virtual void Draw();
    CDC *GetDC();
    void ReleaseDC();
    void Clear();
    void Fade();
    void Fill(COLORREF crColor);
    void DrawRect(const CRect &rc, COLORREF crColor);
    int DrawText(LPCTSTR pszStr, const CRect &rc, int nFont, COLORREF crColor, int nFormat,
                 BOOL bShadow = TRUE);
    void Text(LPCTSTR pszStr, COLORREF crColor = RGB(200, 200, 200), int nFont = 0,
              CRect rc = CRect(0, 0, 0, 0), int nFormat = DT_LEFT)
    {
        DrawText(pszStr, rc, nFont, crColor, nFormat);
    }
    int DrawMultiText(LPCTSTR pszStr, CRect rc, int nLines, int nFont, COLORREF crColor,
                      int nFormat, BOOL bShadow = TRUE, int nLineStep = 0);
};
/////////////////////////////////////////////////////////////////////////////////////
class CI3DMapObjTranslation;
class CI3DObject : public CBase3D, public CI3DHandler
{
    friend struct E4CampAccess;
    friend struct E3HudAccess;
private:
    SBoundingVolume bv;
    CRect rect;
    BOOL bVisible;

protected:
    LPMATERIAL pMat1, pMat2, pMat3;

    CSimplyArray<LPTEXTURE, LPTEXTURE> textures;
    CSimplyArray<CI3DFigure *, CI3DFigure *> figures;    // figures of this object
    CSimplyArray<CI3DSurface *, CI3DSurface *> surfaces; // 2D blitting surfaces
    CSimplyArray<CI3DPolyArea *, CI3DPolyArea *> areas;  // areas to click
    CSimplyArray<CI3DMapObjTranslation *, CI3DMapObjTranslation *> m_aMapObjTaranslation;

public:
    CI3DObject(CInterface3D *pI);
    virtual ~CI3DObject();

    virtual void Init() {}

    void Show(BOOL bShow, BOOL bNeedStore = TRUE);
    virtual void MemoryBackup(BOOL bShow);
    BOOL IsVisible() { return bVisible; }

    CInterface3D *GetI3D() { return pI3D; }

    // textures managing
    void SetTexture(LPTEXTURE pTex);
    LPTEXTURE AddTexture(LPCTSTR pszTexName, ETypeIDs eTexTypeID = TTID_TREE_MAIN,
                         LPCTSTR pszDir = 0);
    LPTEXTURE AddTexture(int nSizeX, int nSizeY, ETypeIDs eTextureTypeID = TFTID_5551);
    LPTEXTURE GetTexture(int num) { return textures[num]; }
    void RemoveTexture(LPTEXTURE pTex);
    void RemoveTexture(int num);
    void RemoveAllTextures();

    // Map obj managing
    CI3DMapObjTranslation *AddMapObjTranslation();
    CI3DMapObjTranslation *GetMapObjTranslation(int nNum) { return m_aMapObjTaranslation[nNum]; }
    void RemoveAllMapObjTranslations();

    // figures managing
    BOOL AddFigure(CI3DFigure *lpFigure);
    CI3DFigure *AddFigure(LPCTSTR pszFigName = 0);
    CI3DFigure *GetFigure(int num) { return figures[num]; }
    CI3DFigure *ChangeFigure(CI3DFigure *lpFigure, LPCTSTR pszFigName);
    BOOL RemoveFigure(CI3DFigure *lpFigure);
    void RemoveFigure(int num);
    void RemoveAllFigures();

    float GetFrameTime();

    // surfaces managing
    CI3DSurface *AddSurface(CI3DSurface *pSurface);
    CI3DSurface *GetSurface(int num)
    {
        if (surfaces.GetSize() > 0)
            return surfaces[num];
        else
            return NULL;
    }
    void RemoveSurface(int num);
    void RemoveAllSurfaces();

    // areas managing
    CI3DPolyArea *AddArea();
    CI3DPolyArea *GetArea(int num) { return areas[num]; }
    void RemoveArea(int num);
    void RemoveAllAreas();
    int HitTest(CPoint pt);
    void JournalHit(int hit) noexcept;

    void SetRect(CRect rc) { rect = rc; }
    void SetRect(int x1, int y1, int x2, int y2)
    {
        rect.left = x1;
        rect.top = y1;
        rect.right = x2;
        rect.bottom = y2;
    }
    BOOL IsInside(CPoint point);

    virtual BOOL Draw(CRenderer *pRend);
    virtual void DrawTips(CRenderer *pRend);
    virtual void Draw2D();
    virtual void Draw2DTips();

    // messages
    virtual void OnIdle() {}

    virtual BOOL Create() { return TRUE; }
    virtual void Destroy() {}
    virtual LPCSTR GetGameClassName() const { return "Sorry, no other way."; }
    virtual EClassIDs WhoAmI() const { return HPC_INTERFACEOBJECT; }
};

bool PasteTexture(LPTEXTURE pDstTex, LPCTSTR pszTexName, int nSrcSizeX, int nSrcSizeY, int nSizeX,
                  int nSizeY, int nDstX, int nDstY, int nSrcX = 0, int nSrcY = 0,
                  std::unique_ptr<WORD[]> *uploadedPixels = nullptr);
