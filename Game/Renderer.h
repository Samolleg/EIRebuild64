#pragma once
/////////////////////////////////////////////////////////////////////////////////////
#include "mmpFormat.h"
#include "Plot.h"
#include "GameBase.h"
#include "TLPipeline.h"
#include "DXGeometryProcessor.h"
#include "TextureDescriptor.h"
#include "Particle.h"
/////////////////////////////////////////////////////////////////////////////////////
const DWORD DXD3D_UPDATE_EXTENTS = 0x00000001;
const DWORD DXD3D_CLIP = 0x00000002;
const DWORD DXD3D_LIT = 0x00000004;
const DWORD DXD3D_USE_SPECULAR = 0x00000008;
// USE_COLOR
// float4 diffuse_color = input.useVertexColor ? input.color : calculateLighting(input.model_pos,
// input.normal); when flag is set, assume vertices are prelit and use their colors in vertex shader
// when flag is omit, calculate lighting in vertex shader
const DWORD DXD3D_USE_VERTEX_COLOR = 0x000000010;
// --------------------------------------------------------------------------------------------
namespace NDXDirect3D {
// state blocks
extern CBYTEArray tempVertices;
}; // namespace NDXDirect3D

/////////////////////////////////////////////////////////////////////////////////////
const DWORD VIS_ACTIVE = 0x00000001;
const DWORD VIS_INITED = 0x00000002;

const DWORD VIS_CAN_RENDER = VIS_ACTIVE | VIS_INITED;

class CConstantBuffers;
class CDX11;
class CDXGISurface;
struct SGeneralSettings;
struct SBoundingVolume;
class CConstantBuffers;
class CParticleArray;
class CGraphicsProcessor;
class CQuaternion;
class CPlot3D;
class CMaterial;
class CFigure;
class CPolyLine;
class CTexture;
class CLight;
class CWeather;

typedef enum _ERBLEND_MODE
{
    BM_UNKNOWN = 0,
    BM_ZERO = 1,
    BM_ONE = 2,
    BM_SRCCOLOR = 3,
    BM_INVSRCCOLOR = 4,
    BM_SRCALPHA = 5,
    BM_INVSRCALPHA = 6,
    BM_DESTALPHA = 7,
    BM_INVDESTALPHA = 8,
    BM_DESTCOLOR = 9,
    BM_INVDESTCOLOR = 10,
    BM_SRCALPHASAT = 11,
    BM_BOTHSRCALPHA = 12,
    BM_BOTHINVSRCALPHA = 13,
} ERBLEND_MODE;

typedef enum _ER_TEXTURING_ALPHA_MODE
{
    AM_DEFAULT = 0,
    AM_TEXTURE = 0,
    AM_MULTIPLY = 1,
    AM_VERTEX = 2
} ER_TEXTURING_ALPHA_MODE;

class SParticleToDraw;
typedef CArray<SParticleToDraw, SParticleToDraw &> CParticleToDrawArray;

/////////////////////////////////////////////////////////////////////////////////////
class CRenderer
{
private:
    std::unique_ptr<CDX11> pDX11;
    std::unique_ptr<CDXGeometryProcessor> pGeometryProcessor;
    std::unique_ptr<CGraphicsProcessor> pGraphicsProcessor;
    DWORD dwFlags;                // any flags
    DWORD dwBGColor;              // background color
    DWORD dwAmbientColor;         // ambient color
    CLight *pSunLight;            // current sunlight
    DWORD dwDefaultAmbientColor;  //
    DWORD dwDefaultSpecularColor; //
    BOOL bAddFog;                 // add fog or no ?
    bool bRenderFor2DRectInfoOnly;
    RECT rcScreen;

    // frame and session statistics
    DWORD dwLastFrameTime;           // time, passed from last frame to this. in milliseconds
    DWORD dwMinFrameTime;            // minimal frame time
    DWORD dwMaxFrameTime;            // maximal frame time
    DWORD dwAverageFrameTimeLastSec; // average frame time of the last second
    DWORD dwTotalTime;               // sum of the all 'dwLastFrameTime'
    DWORD dwNumFrames;               // number of passed frames
    DWORD dwCurrTime;

    DWORD dwOldTime;
    DWORD dwSecPassedTime;
    DWORD dwSecPassedFrames;

private:
    virtual BOOL SetFogColor(DWORD dwColor);

public:
    void Create(HWND hWnd, const SGeneralSettings &settings);
    void Destroy();

    BOOL BeginDrawFrame();
    BOOL EndDrawFrame();

    CDX11 *GetD3D() const { return pDX11.get(); }
    CDXGeometryProcessor *GetGPU() const { return pGeometryProcessor.get(); }
    CGraphicsProcessor *GetGrPU() const { return pGraphicsProcessor.get(); }
    CGraphicsProcessor *GetGraphicsProcessor() const { return pGraphicsProcessor.get(); }
    CDXGeometryProcessor *GetGeometryProcessor() const { return pGeometryProcessor.get(); }
    CTLPipeline *GetTransformPipeline() const { return pGeometryProcessor->GetGeometryProcessor(); }

    void SetFlag(DWORD dwFlag) { dwFlags |= dwFlag; }
    void ResetFlag(DWORD dwFlag) { dwFlags &= ~dwFlag; }
    DWORD CheckFlag(DWORD dwFlag) const { return (dwFlags & dwFlag); }
    DWORD IsActive() const { return CheckFlag(VIS_ACTIVE); }
    DWORD IsInited() const { return CheckFlag(VIS_INITED); }
    BOOL IsCanRender() const { return (CheckFlag(VIS_CAN_RENDER) == VIS_CAN_RENDER); }

    void SetFogBGColor(float r, float g, float b);
    void SetBGColor(DWORD dwColor);
    DWORD GetBGColor() const { return dwBGColor; }
    void SetAmbientColor(float r, float g, float b)
    {
        SetAmbientColor(0xFF000000 | (static_cast<DWORD>(MINT(r * 255.0f)) << 16) |
                        (static_cast<DWORD>(MINT(g * 255.0f)) << 8) |
                        static_cast<DWORD>(MINT(b * 255.0f)));
    }
    void SetAmbientColor(DWORD dwColor);
    DWORD GetAmbientColor() const { return dwAmbientColor; }
    void SetSunlight(CLight *pLight);
    CLight *GetSunlight() const { return pSunLight; }
    void SetLighting(CLight *pNewSunLight, float fpAmbientR, float fpAmbientG, float fpAmbientB);
    //
    int CheckVisibility(const SBoundingVolume &bv)
    {
        return pGeometryProcessor->CheckVisibility(bv);
    }
    int TransformVertices(SXYZW4 *pSrc, SXYZW *pDst, BYTE *pClips, const int nNumSrc,
                          const SBoundingVolume *pBV)
    {
        return pGeometryProcessor->TransformVertices(pSrc, pDst, pClips, nNumSrc, pBV);
    }
    //

    BOOL Draw3DLine(CXYZWArray &points, const float fpLineWidth, const int nBVClipFlags)
    {
        return Draw3DLine(points, fpLineWidth, nBVClipFlags, 0, 0, 0.25f);
    }
    DWORD GetLastFrameTime() { return dwLastFrameTime; };
    BOOL Clear();
    BOOL SetAlphaBlendMode(ERBLEND_MODE d3dSrcBlend, ERBLEND_MODE d3dDstBlend);
    BOOL DrawFigure(CFigure *pFigure, const CQuaternion &quat, const CPlot3D &cp,
                    CMaterial *pMaterial, BYTE cFog, CRect *pRect);
    BOOL DrawFigure(CFigure *pFigure, CSimplyArray<CLight *, CLight *> &lights,
                    CMaterial *pMaterial, BYTE cFog, CRect *pRect, CTexture *pTexture0 = 0,
                    CTexture *pTexture1 = 0);
    BOOL DrawPolyline(CPolyLine *pPolyline);
    BOOL Draw3DLine(CXYZWArray &points, const float fpLineWidth, const int nBVClipFlags,
                    float fpTextureLength, float fpTexVStart, float fpTexVWidth);
    BOOL DrawParticles(bool bUseAdd, CXYZWArray &points, CParticleToDrawArray &particles);
    BOOL DrawSelection(const CPlot3D &cp, const float fSize, const CPlot3D &ptXAxis,
                       const CPlot3D &ptYAxis);
    BOOL DrawFrameSelection(int x1, int y1, int x2, int y2, char cAlpha = 64, bool bFrame = true);
    BOOL DrawPolyLine(const CSimplyArray<POINT, POINT &> &plots, DWORD dwColor);
    BOOL DrawTextureDebug(CGameBase *pTexture, float x, float y, float fScale = 1.0f);
    bool Restore();
    void SetViewport(const RECT &rect);
    // D3D
    void Move(int dx, int dy)
    {
        rcScreen.left += dx;
        rcScreen.right += dx;
        rcScreen.top += dy;
        rcScreen.bottom += dy;
    };
    void MoveTo(int x, int y)
    {
        SetRect(&rcScreen, x, y, x + (rcScreen.right - rcScreen.left),
                y + (rcScreen.bottom - rcScreen.top));
    };
    const RECT &GetWindowRect() const { return rcScreen; };

    void SetAddFogOn() { bAddFog = TRUE; };
    void SetAddFogOff() { bAddFog = FALSE; };
    BOOL IsAddFog() const { return bAddFog; };
    //
    void SetDefaultAmbientColor(DWORD dwColor) { dwDefaultAmbientColor = dwColor; };
    void SetDefaultSpecularColor(DWORD dwColor) { dwDefaultSpecularColor = dwColor; };

    BOOL FindBestTextureFormat(const SMMPFileHeader *pHdr, DXGI_FORMAT &format) const;
    BOOL FindTextureFormat(DWORD dwTextureFormat, DXGI_FORMAT &format) const;
    // render triangle set functions
    BOOL RenderTriangleSet(STLVertex1 *pVertices, DWORD dwVertexCount, LPWORD pIndices,
                           DWORD dwIndexCount);
    void FlushBuffers(){};
    // bind texture for rendering
    BOOL BindTexture(DWORD dwStage, CGameBase *pTexture);
    // bind texture for rendering
    BOOL BindTexture(DWORD dwStage, CDXGISurface *pTexture);
    // bind texture for rendering
    BOOL BindTexture(DWORD dwStage, CTextureDescriptor &texture);
    BOOL Clear(DWORD dwCount, LPD3DRECT lpRects, DWORD dwFlags, DWORD dwColor, D3DVALUE dvZ,
               DWORD dwStencil);
    CConstantBuffers *GetConstantBuffers();
    void GetCurrentMode(DWORD &widthOut, DWORD &heightOut)
    {
        widthOut = rcScreen.right - rcScreen.left;
        heightOut = rcScreen.bottom - rcScreen.top;
    };
    BOOL SetTexturingAlphaMode(ER_TEXTURING_ALPHA_MODE alphaMode);
    HRESULT SetRenderState(D3DRENDERSTATETYPE dwRenderStateType, DWORD dwRenderState);
    HRESULT GetRenderState(D3DRENDERSTATETYPE dwRenderStateType, DWORD *pdwRenderState);
    bool TryOwnedFigure(const SXYZW4* plots,int nNumPlots,const SXYZW4* norms,int nNumNorms,
        const DWORD* colors,const DWORD* speculars,const CPlot2D* maps,int nNumMaps,
        const SVertexComponents* components,int nNumComponents,const WORD* indices,int nNumIndices,
        float u,float v,BYTE fog,DWORD flags,const CPlot3D* morphs,int morphCount);
    int Render(SXYZW4 *plots, const int nNumPlots, SXYZW4 *norms, const int nNumNorms,
               LPDWORD colors, LPDWORD speculars, CPlot2D *maps, const int nNumMaps,
               SVertexComponents *components, const int nNumComponents,
               SGvNvComponents *lightcomponents, const int nNumLightComponents, WORD *pwIndices,
               const int nNumIndices, const float fpUShift, const float fpVShift, const BYTE cFog,
               DWORD dwRenderFlags);
    int Render(SXYZW4 *plots, CPlot2D *maps, const int nNumPlots, WORD *indices,
               const int nNumIndices, DWORD dwColor = 0x80808080, DWORD dwSpecular = 0xFF000000,
               const SBoundingVolume *pBV = 0, float fZCoeff = 1);
    BOOL DrawIndexedPrimitive(D3DPRIMITIVETYPE primitiveType, LPVOID lpvVertices,
                              DWORD dwVertexCount, LPWORD pIndices, DWORD dwIndexCount,
                              DWORD dwFlags = 0);
    void SetProjectionMatrix(const SHMatrix &matrix);
    void SetProjectionMatrixOne();
    void SetProjectionMatrix();
    void SetWorldMatrix(const SHMatrix &matrix);
    void SetCameraPosition(const CPlot3D &pos);
};
/////////////////////////////////////////////////////////////////////////////////////
extern CRenderer theRenderer;

struct SRayPicker
{
    DirectX::XMVECTOR rayOrigin, rayDirection;

    SRayPicker(const CPoint &screenPoint, const CPlot3D &cameraPos);
    bool CheckBV(const CPlot3D &pos, const SBoundingVolume &bv);
};

CRenderer *GetRenderer();