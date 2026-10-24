#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "Renderer.h"
#include "ModWorldBackground/Native.h"

#include "Camera.h"
#include "Material.h"
#include "Texture.h"
#include "WorldClient.h"
#include "TextureManager.h"
#include "IDebugInfoSystem.h"
#include "FigureClientSpecific.h"
#include "PolyLine.h"
#include "Light.h"
#include "GraphicsProcessorMMX.h"
#include "DXGeometryProcessor.h"

#include "GeneralSettings.h"
#include "LightManager.h"

#include "WorldServer.h"
#include "dx11/RendererDX11.h"
#include "OwnedRender/E1RendererFacade.h"
#include "dx11/vertex.h"
#include "dx11/Surface.h"
#include "dx11/ConstantBuffer.h"
#include "Particle.h"
#include <DirectXCollision.h>
#include "dx11/OwnedFigureInputs.h"
#include "native_placed_figure.h"
#include "model_draw.h"
#include "OwnedRender/E1VisualFrame.h"
#include "OwnedRender/E4Camp.h"
/////////////////////////////////////////////////////////////////////////////////////
namespace NDXDirect3D {
// buffers
CBYTEArray tempVertices;
}; // namespace NDXDirect3D
using namespace NDXDirect3D;

template <class VERTEX_TYPE>
inline VERTEX_TYPE *GetTempVertices(int nAmount, VERTEX_TYPE *pType)
{
    NDXDirect3D::tempVertices.SetSize(nAmount * sizeof(VERTEX_TYPE)); // ARRAY BOUNDS CHECK
    return reinterpret_cast<VERTEX_TYPE *>(NDXDirect3D::tempVertices.GetData());
}

/////////////////////////////////////////////////////////////////////////////////////
CRenderer theRenderer;

SRayPicker::SRayPicker(const CPoint &screenPoint, const CPlot3D &cameraPos)
{
    // calculate origin and direction
    SHMatrix world_to_screen = theRenderer.GetGPU()->GetWorldToScreenMatrix();
    SHMatrix screen_to_world;
    screen_to_world.HomogeneousInverse(world_to_screen);

    CPlot3D rayPoint = { float(screenPoint.x), float(screenPoint.y), 1.0 };
    CPlot3D rayDirectionWorld = screen_to_world.RotateVector(rayPoint);
    rayDirectionWorld.Normalize(); // intersects needs unit vector

    rayOrigin = { cameraPos.x, cameraPos.y, cameraPos.z, 1.0 };
    rayDirection = { rayDirectionWorld.x, rayDirectionWorld.y, rayDirectionWorld.z, 1.0 };
};

bool SRayPicker::CheckBV(const CPlot3D &pos, const SBoundingVolume &bv)
{
    float magnitude;
    if (bv.IsSphere())
    {
        DirectX::BoundingSphere bs(pos, bv.fpRadius);
        if (bs.Intersects(rayOrigin, rayDirection, magnitude))
        {
            return true;
        }
    }
    if (bv.IsBox())
    {
        DirectX::BoundingBox bb(pos, bv.ptMax);
        if (bb.Intersects(rayOrigin, rayDirection, magnitude))
        {
            return true;
        }
    }
};

/////////////////////////////////////////////////////////////////////////////////////
CRenderer *GetRenderer()
{
    return &theRenderer;
}
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
// **                   class CRenderer functions                         ** //
// ******************************************************************************** //
/////////////////////////////////////////////////////////////////////////////////////
void CRenderer::SetViewport(const RECT &rect)
{
}
/////////////////////////////////////////////////////////////////////////////////////
void CRenderer::Create(HWND hWnd, const SGeneralSettings &settings)
{
    pGeometryProcessor = std::make_unique<CDXGeometryProcessor>();
    // SSE2 is part of the x64 baseline; there is one graphics processor path.
    pGraphicsProcessor = std::make_unique<CGraphicsProcessorMMX>();

    dwDefaultAmbientColor = 0xFF000000;

    pDX11 = std::make_unique<CDX11>();
    pDX11->Initialize(hWnd, settings.dwWidth, settings.dwHeight);
    // The shared renderer has now applied the display policy. Synchronize
    // session dimensions before widescreen projection, UI/input and video.
    pDX11->GetCurrentMode(theSettings.dwWidth, theSettings.dwHeight);
    rcScreen = { 0, 0, LONG(theSettings.dwWidth), LONG(theSettings.dwHeight) };

    float fpFrontClip, fpBackClip, fpViewingAngle;
    SHMatrix matrix;
    // create and set projection matrix
    theCamera.GetViewingFrustum(fpFrontClip, fpBackClip, fpViewingAngle);
    float fAspect = 4.0f / 3.0f;
    //		float fAspect = static_cast<float>( GetWindowRect().right -
    // GetWindowRect().left ) /
    // static_cast<float>( GetWindowRect().bottom - GetWindowRect().top );
    GetGPU()->CreateProjectionMatrix(matrix, fpViewingAngle, fAspect, fpFrontClip, fpBackClip);
    GetGPU()->PushMatrix(matrix, MAT_STACK_PROJECTION);
    // create and set viewport scaling matrix
    GetGPU()->CreateViewportScaleMatrix(matrix, rcScreen);
    GetGPU()->PushMatrix(matrix, MAT_STACK_VIEWSCALE);
    SetViewport(rcScreen);
    // create and set clipping space scaling matrix
    GetGPU()->CreateClipScaleMatrix(matrix);
    GetGPU()->PushMatrix(matrix, MAT_STACK_CLIPSCALE);
    // form screen space matrix stack
    GetGPU()->PushMatrix(GetGPU()->GetMatrix(MAT_STACK_VIEWSCALE), MAT_STACK_SCRNSPACE);
    GetGPU()->PushMatrix(GetGPU()->GetMatrix(MAT_STACK_CLIPSCALE), MAT_STACK_SCRNSPACE);
    GetGPU()->PushMatrix(GetGPU()->GetMatrix(MAT_STACK_PROJECTION), MAT_STACK_SCRNSPACE);
    // form screen space2 matrix stack
    GetGPU()->PushMatrix(GetGPU()->GetMatrix(MAT_STACK_VIEWSCALE), MAT_STACK_SCRNSPACE2);
    GetGPU()->PushMatrix(GetGPU()->GetMatrix(MAT_STACK_CLIPSCALE), MAT_STACK_SCRNSPACE2);
    GetGPU()->PushMatrix(GetGPU()->GetMatrix(MAT_STACK_PROJECTION), MAT_STACK_SCRNSPACE2);
    // form projection (clip) space matrix stack
    GetGPU()->PushMatrix(GetGPU()->GetMatrix(MAT_STACK_CLIPSCALE), MAT_STACK_PROJSPACE);
    GetGPU()->PushMatrix(GetGPU()->GetMatrix(MAT_STACK_PROJECTION), MAT_STACK_PROJSPACE);
    // form 'view => clip' translation matrix
    GetGPU()->PushMatrix(GetGPU()->GetMatrix(MAT_STACK_CLIPSCALE), MAT_STACK_VIEW2CLIP);
    GetGPU()->PushMatrix(GetGPU()->GetMatrix(MAT_STACK_PROJECTION), MAT_STACK_VIEW2CLIP);
    // set projection matrix data to T&L pipeline (require transposed D3DMATRIX form)
    matrix = GetGPU()->GetMatrix(MAT_STACK_PROJECTION);
    Transpose(matrix);
    D3DMATRIX d3dmatrix = *(reinterpret_cast<D3DMATRIX *>(&matrix));
    GetGPU()->GetGeometryProcessor()->SetProjectionMatrix(d3dmatrix);
    GetGPU()->GetGeometryProcessor()->SetClipRect(
        static_cast<float>(GetWindowRect().right - GetWindowRect().left),
        static_cast<float>(GetWindowRect().bottom - GetWindowRect().top));
    GetGPU()->GetGeometryProcessor()->SetFogRange(settings.fpFogDayStartDistance,
                                                  settings.fpFarClipDistance);
    // z-buffering
    SetRenderState(D3DRENDERSTATE_ZENABLE, TRUE);
    SetRenderState(D3DRENDERSTATE_ZWRITEENABLE, TRUE);
    // filling and shading
    SetRenderState(D3DRENDERSTATE_FILLMODE, D3DFILL_SOLID);
    SetRenderState(D3DRENDERSTATE_SHADEMODE, D3DSHADE_GOURAUD);
    // alpha blend mode
    SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, TRUE);
    SetRenderState(D3DRENDERSTATE_ALPHATESTENABLE, TRUE);
    SetRenderState(D3DRENDERSTATE_ALPHAFUNC, D3DCMP_GREATER);
    SetRenderState(D3DRENDERSTATE_ALPHAREF, 200);
    SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_ONE);
    SetRenderState(D3DRENDERSTATE_DESTBLEND, D3DBLEND_ZERO);
    // set culling mode
    SetRenderState(D3DRENDERSTATE_CULLMODE, D3DCULL_CW);
    // specular, dithering and perspective correction
    SetRenderState(D3DRENDERSTATE_SPECULARENABLE, TRUE);
    SetRenderState(D3DRENDERSTATE_DITHERENABLE, settings.nDithering);
    // SetRenderState(D3DRENDERSTATE_TEXTUREPERSPECTIVE, settings.nPerspectiveCorrection);
    // antialiasing
    if (settings.nAntialiasing)
        SetRenderState(D3DRENDERSTATE_ANTIALIAS, D3DANTIALIAS_SORTINDEPENDENT);
    else
        SetRenderState(D3DRENDERSTATE_ANTIALIAS, D3DANTIALIAS_NONE);
    // enable fog
    SetRenderState(D3DRENDERSTATE_AMBIENT, 0);
    // initialization done - set flag...
    SetFlag(VIS_INITED);
    // statistics init
    dwLastFrameTime = 0;
    dwMinFrameTime = (1 << 31);
    dwMaxFrameTime = 0;
    dwTotalTime = 0;
    dwNumFrames = 0;
    dwAverageFrameTimeLastSec = 0;
    //
    dwOldTime = timeGetTime();
    dwSecPassedTime = 0;
    dwSecPassedFrames = 0;
    // setup default lighting
    D3DVECTOR dvAmbient = D3DVECTOR(50.0f / 255.0f, 50.0f / 255.0f, 50.0f / 255.0f);
    D3DVECTOR dvSunColor = D3DVECTOR(255.0f / 255.0f, 255.0f / 255.0, 255.0f / 255.0f);
    D3DVECTOR dvSunDirection = D3DVECTOR(5, 5, -8);
    // create sunlight with default params
    LPLIGHT lpLight = static_cast<LPLIGHT>(
        theWorld.GetLightManager()->CreateData(LTID_DIRECTIONAL));
    lpLight->SetDirectionalLight(dvSunColor, dvSunDirection);
    lpLight->SetRange(1000000.0);
    SetSunlight(lpLight);
    // setup default ambient
    CRenderer::SetAmbientColor(dvAmbient.x, dvAmbient.y, dvAmbient.z);
    // create shading manager
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CRenderer::Clear()
{
    return Clear(0, 0, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, GetBGColor(), 1, 0);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CRenderer::SetFogColor(DWORD dwColor)
{
    HRESULT dxrval = SetRenderState(D3DRENDERSTATE_FOGCOLOR, dwColor);
    if (dxrval != D3D_OK)
        return ReportError("Can't set fog color 0x%X.\n", dwColor);
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CRenderer::SetAlphaBlendMode(ERBLEND_MODE d3dSrcBlend, ERBLEND_MODE d3dDstBlend)
{
    SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND(d3dSrcBlend));
    SetRenderState(D3DRENDERSTATE_DESTBLEND, D3DBLEND(d3dDstBlend));
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CRenderer::DrawFigure(CFigure *pFigure, const CQuaternion &quat, const CPlot3D &cp,
                           CMaterial *pMaterial, BYTE cFog, CRect *pRect)
{
    if (pFigure->CheckFlag(FIG_HIDDEN))
        return TRUE;
    CFigureClientSpecific *pFigureSpec = static_cast<CFigureClientSpecific *>(
        pFigure->GetFigureSpecific());
    OwnedFigureInputs::Scope sourceScope(pDX11->GetOwnedRenderer(),pFigure,pMaterial,false,
        nullptr,nullptr,pDX11->GetBoundFigureTexture(0),pDX11->GetBoundFigureTexture(1));
    return pFigureSpec->Draw(this, quat, cp, pMaterial, cFog, pRect);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CRenderer::DrawFigure(CFigure *pFigure, CSimplyArray<CLight *, CLight *> &lights,
                           CMaterial *pMaterial, BYTE cFog, CRect *pRect, CTexture *pTexture0,
                           CTexture *pTexture1)
{
    if (pFigure->CheckFlag(FIG_HIDDEN))
        return TRUE;
    CFigureClientSpecific *pFigureSpec = static_cast<CFigureClientSpecific *>(
        pFigure->GetFigureSpecific());
    OwnedFigureInputs::Scope sourceScope(pDX11->GetOwnedRenderer(),pFigure,pMaterial,
        pDX11->IsOwnedWorldActive() && !owned::model_draw_is_preview(),pTexture0,pTexture1,
        pDX11->GetBoundFigureTexture(0),pDX11->GetBoundFigureTexture(1));
    return pFigureSpec->Draw(this, lights, pMaterial, cFog, pRect, pTexture0, pTexture1);
}
/////////////////////////////////////////////////////////////////////////////////////
// Typed c74 inputs only: GPU strip expansion and clipping belong to renderer.
BOOL CRenderer::Draw3DLine(CXYZWArray &points, const float fpLineWidth, const int nBVClipFlags,
                           float fpTextureLength, float fpTexVStart, float fpTexVWidth)
{
    if (nBVClipFlags == TRV_INVISIBLE || points.GetSize() < 2)
        return TRUE;
    std::vector<owned::GameRibbonPoint> values(points.GetSize());
    for (int i = 0; i < points.GetSize(); ++i)
        values[i] = {points[i].x, points[i].y, points[i].z};
    owned::EmbeddedRibbonInput input;
    input.points = values.data(); input.count = static_cast<std::uint32_t>(values.size());
    input.width = fpLineWidth; input.clip_flags = nBVClipFlags;
    input.texture_length = fpTextureLength; input.v_start = fpTexVStart; input.v_width = fpTexVWidth;
    input.interface_pass = !pDX11->IsOwnedWorldActive();
    const auto& projection = GetGPU()->GetMatrix(MAT_STACK_PROJSPACE);
    std::memcpy(input.projection.object_to_clip.data(), &projection, sizeof(input.projection.object_to_clip));
    const RECT& clip = GetWindowRect();
    input.projection.clip_half_width = float(clip.right - clip.left) * .5f;
    input.projection.clip_half_height = float(clip.bottom - clip.top) * .5f;
    pDX11->SubmitC74Ribbon(input);
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CRenderer::DrawParticles(bool bUseAdd, CXYZWArray &points, CParticleToDrawArray &particles)
{
    CDXGeometryProcessor *pGPU = GetGPU();
    SetProjectionMatrix();
    if (pDX11->IsOwnedWorldActive())
    {
        if (!points.GetSize())
            return TRUE;
        std::vector<owned::GameParticle> values(points.GetSize());
        for (int i = 0; i < points.GetSize(); ++i)
        {
            const auto& point = points[i];
            const auto& particle = particles[i];
            const auto& uv = *particle.pUV;
            values[i] = {point.x, point.y, point.z, point.w, particle.dwColor,
                         uv.fpU0, uv.fpV0, uv.fpU1, uv.fpV1};
        }
        owned::GameParticleView view;
        view.particles = values.data();
        view.count = static_cast<std::uint32_t>(values.size());
        // Same projection-space stack as SetProjectionMatrix(), in c56's
        // row-major M * p layout (the legacy shader uses its transpose).
        const SHMatrix& projection = pGPU->GetMatrix(MAT_STACK_PROJSPACE);
        std::memcpy(view.object_to_clip.data(), &projection, sizeof(view.object_to_clip));
        const RECT& clipRect = GetWindowRect();
        // Same dimensions passed to CTLPipelineFPU::SetClipRect at setup.
        view.clip_half_width = float(clipRect.right - clipRect.left) * 0.5f;
        view.clip_half_height = float(clipRect.bottom - clipRect.top) * 0.5f;
        view.additive = bUseAdd;
        pDX11->SubmitC56GameParticles(view);
        return TRUE;
    }
    // No OpenEI path: submission reports an unavailable renderer once.
    pDX11->SubmitC56GameParticles(owned::GameParticleView{});
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CRenderer::DrawSelection(const CPlot3D &cp, const float fSize, const CPlot3D &ptXAxis,
                              const CPlot3D &ptYAxis)
{
    owned::GameSelectionView view;
    view.marker = {cp.x, cp.y, cp.z, fSize, {ptXAxis.x, ptXAxis.y}, {ptYAxis.x, ptYAxis.y}};
    const auto& projection = GetGPU()->GetMatrix(MAT_STACK_PROJSPACE);
    std::memcpy(view.object_to_clip.data(), &projection, sizeof(view.object_to_clip));
    const RECT& clip = GetWindowRect();
    view.clip_half_width = float(clip.right - clip.left) * .5f;
    view.clip_half_height = float(clip.bottom - clip.top) * .5f;
    pDX11->SubmitC74Selection(view);
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CRenderer::BeginDrawFrame()
{
    if (!IsActive())
        return FALSE;

    dwCurrTime = timeGetTime();
    // collect statistics
    dwLastFrameTime = dwCurrTime - dwOldTime;
    if ((dwMinFrameTime > dwLastFrameTime) && (dwLastFrameTime != 0))
        dwMinFrameTime = dwLastFrameTime;
    if ((dwMaxFrameTime < dwLastFrameTime) && (dwLastFrameTime < 1000))
        dwMaxFrameTime = dwLastFrameTime;
    dwTotalTime += dwLastFrameTime;
    dwNumFrames++;
    dwSecPassedFrames++;
    dwSecPassedTime += dwLastFrameTime;
    if (dwSecPassedTime >= 1000)
    {
        if (dwSecPassedFrames)
            dwAverageFrameTimeLastSec = dwSecPassedTime / dwSecPassedFrames;
        else
            dwAverageFrameTimeLastSec = 0;
        dwSecPassedTime = 0;
        dwSecPassedFrames = 0;
    }
    //
    Clear();
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CRenderer::EndDrawFrame()
{
    CDebugInfoSystemInterface *pDIS = GetDebugInfoSystem();
#ifndef NODEBUGINFO
    CString szString;
    /*sztring.Format("%d", dwLastFrameTime);
    pDIS->Set("frm last", szString);
    szString.Format("%d", dwMinFrameTime);
    pDIS->Set("frm min", szString);
    szString.Format("%d", dwMaxFrameTime);
    pDIS->Set("frm max", szString);
    szString.Format("%d", dwAverageFrameTimeLastSec);
    pDIS->Set("frm avg. ls", szString);
    szString.Format("%d", ((dwMaxFrameTime + dwMinFrameTime) >> 1));
    pDIS->Set("frm avg.", szString);*/
    szString.Format("%g", 1000.0f / static_cast<float>(dwLastFrameTime));
    pDIS->Set("FPS", szString);
    szString.Format("%d", dwAverageFrameTimeLastSec);
    pDIS->Set("frm avg. ls", szString);

    szString.Format("%g", theWorld.GetWorldTime());
    pDIS->Set("world time", szString);

    szString.Format("%d : %d", theWorld.GetMapObjectManager()->GetNumElements(),
                    theWorld.GetMapObjectManager()->GetNumTrashElements());
    pDIS->Set("MOs(client)", szString);
    szString.Format("%d : %d", theWorldServer.GetMapObjectManager()->GetNumElements(),
                    theWorldServer.GetMapObjectManager()->GetNumTrashElements());
    pDIS->Set("MOs(server)", szString);
    szString.Format("%d : %d", theWorld.GetFigureManager()->GetNumElements(),
                    theWorld.GetFigureManager()->GetNumTrashElements());
    pDIS->Set("Figs(client)", szString);
    szString.Format("%d : %d", theWorldServer.GetFigureManager()->GetNumElements(),
                    theWorldServer.GetFigureManager()->GetNumTrashElements());
    pDIS->Set("Figs(server)", szString);
    szString.Format("%d", theWorld.GetGeometryManager()->GetNumElements());
    pDIS->Set("Geoms", szString);
    szString.Format("%d : %d : %d", theWorld.GetTextureManager()->GetNumElements(),
                    theWorld.GetTextureManager()->GetNumLRUElements(),
                    theWorld.GetTextureManager()->GetNumTrashElements());
    pDIS->Set("Textures", szString);
    szString.Format("%d : %d", theWorld.GetParticleManager()->GetNumElements(),
                    theWorld.GetParticleManager()->GetNumTrashElements());
    pDIS->Set("Particles", szString);
    szString.Format("%g", theCamera.GetDistance());
    pDIS->Set("Camera dist", szString);
    szString.Format("%g", ToDegree(theCamera.GetPitch()));
    pDIS->Set("Camera pitch", szString);
    szString.Format("%g", ToDegree(theCamera.GetRotateAngle()));
    pDIS->Set("Camera view", szString);
#endif // NODEBUGINFO
    dwOldTime = dwCurrTime;

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CRenderer::DrawFrameSelection(int x1, int y1, int x2, int y2, char cAlpha, bool bFrame)
{
    owned::ScreenRectangleRequest request{};
    if (!owned::make_screen_rectangle_request(x1, y1, x2, y2, cAlpha, bFrame, request))
        return FALSE;
    return pDX11->SubmitC56ScreenRectangle(request) ? TRUE : FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CRenderer::DrawPolyline(CPolyLine *pPolyline)
{
    return pPolyline->Draw(GetGPU(), this);
}
/////////////////////////////////////////////////////////////////////////////////////
void CRenderer::Destroy()
{
    ModWorldBackground::Unload();
    pGeometryProcessor = 0;
    pGraphicsProcessor = 0;
    pDX11 = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CRenderer::Restore()
{
    if (pDX11)
    {
        return theWorld.GetTextureManager()->ReloadAllTextures();
    }
    else
        return false;
}
/////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////
inline float Clamp(float fVal, float fMin, float fMax)
{
    return Max(fMin, Min(fVal, fMax));
}
/////////////////////////////////////////////////////////////////////////////////////
DXGI_FORMAT TextureFormatFromMMPFormat(DWORD mmpFormat)
{
    switch (mmpFormat)
    {
    case MMP_FORMAT_DXT1:
        return DXGI_FORMAT_BC1_UNORM;
    case MMP_FORMAT_DXT2:
        return DXGI_FORMAT_BC2_UNORM;
    case MMP_FORMAT_DXT3:
        return DXGI_FORMAT_BC2_UNORM;
    case MMP_FORMAT_5650:
    case MMP_FORMAT_5550:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    case MMP_FORMAT_5551:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    case MMP_FORMAT_4444:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    case MMP_FORMAT_8888:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    default:
        ReportError("Can't find texture format (MMP format %d).\n", mmpFormat);
        return DXGI_FORMAT_UNKNOWN;
    }
}
BOOL CRenderer::FindBestTextureFormat(const SMMPFileHeader *pHdr, DXGI_FORMAT &format) const
{
    const DWORD mmpFormat = pHdr->dwFormat;
    format = TextureFormatFromMMPFormat(mmpFormat);
    return format != DXGI_FORMAT_UNKNOWN;
}

/////////////////////////////////////////////////////////////////////////////////////
// BOOL CRenderer::FindTextureFormat( ETypeIDs tidTextureFormat, DDPIXELFORMAT& ddpf )
// find texture pixel format from hash map by type ID
BOOL CRenderer::FindTextureFormat(DWORD dwTextureFormat, DXGI_FORMAT &format) const
{
    format = TextureFormatFromMMPFormat(dwTextureFormat);
    return format != DXGI_FORMAT_UNKNOWN;
}
/////////////////////////////////////////////////////////////////////////////////////
__inline void ComposeVerticesMain(PNTVertex *vertices, SXYZW4 *geoms, SXYZW4 *norms, LPDWORD colors,
                                  LPDWORD speculars, CPlot2D *maps, SVertexComponents *components,
                                  int nComponents, float fU, float fV, DWORD dwRenderFlags)
{
    constexpr int s = sizeof(PNTVertex);
    for (int i = 0; i < nComponents; i++)
    {
        const SVertexComponents &component = components[i];
        const SXYZW4 &plot = geoms[component.gv >> 2];
        const int nLow = component.gv & 0x3;
        PNTVertex &vertex = vertices[i];
        vertex.pos.x = plot.x[nLow];
        vertex.pos.y = plot.y[nLow];
        vertex.pos.z = plot.z[nLow];

        const SXYZW4 &norm = norms[component.nv >> 2];
        int nLow2 = component.nv & 0x3;
        vertex.normal.x = norm.x[nLow2];
        vertex.normal.y = norm.y[nLow2];
        vertex.normal.z = norm.z[nLow2];

        const CPlot2D &uvs = maps[component.tv];
        vertex.tuv.x = uvs.x + fU;
        vertex.tuv.y = uvs.y + fV;

        int nv = component.nv;
        vertex.color = colors[nv];
        vertex.specular = speculars[nv];
        vertex.useVertexColor = dwRenderFlags & DXD3D_USE_VERTEX_COLOR;
    }
}

/////////////////////////////////////////////////////////////////////////////////////
bool CRenderer::TryOwnedFigure(const SXYZW4* plots,int nNumPlots,const SXYZW4* norms,int nNumNorms,
    const DWORD* colors,const DWORD* speculars,const CPlot2D* maps,int nNumMaps,
    const SVertexComponents* components,int nNumComponents,const WORD* pwIndices,int nNumIndices,
    float fpUShift,float fpVShift,BYTE cFog,DWORD dwRenderFlags,const CPlot3D* morphs,int morphCount)
{
    const bool campDraw=OwnedRender::E4::OwnsDraw();
    const bool hasFigure=pDX11->GetOwnedRenderer().FigureActive();
    // World objects belong to the committed frame, including rejected visuals.
    // Never submit a second figure draw or return to OpenEI for these IDs.
    const auto objectId = std::int32_t(pDX11->GetOwnedRenderer().FigureValues().object_id);
    if (!campDraw && hasFigure && owned::model_draw_uses_retained_world(
            pDX11->IsOwnedWorldActive(), objectId > 0, owned::model_draw_domain)) {
        OwnedRender::E1ReportOwnedVisual(objectId);
        return true;
    }
    const bool haveWorldMatrix=pGeometryProcessor->IsHaveMatrix(MAT_STACK_WORLDSPACE);
    const bool haveProjectionMatrix=pGeometryProcessor->IsHaveMatrix(MAT_STACK_PROJSPACE);
    if(!haveProjectionMatrix || (!haveWorldMatrix && objectId!=0)) {
        if(campDraw)pDX11->SubmitC56CampFigure({});
        else pDX11->ReportC56FigureNoMatrix();
        return true;
    }

    owned::NativeGeometryView view{};
    owned::apply_native_figure_streams(view, {
        reinterpret_cast<const owned::NativePackedGroup*>(plots), static_cast<std::uint32_t>(nNumPlots),
        reinterpret_cast<const owned::NativePackedGroup*>(norms), static_cast<std::uint32_t>(nNumNorms),
        reinterpret_cast<const std::uint32_t*>(colors), reinterpret_cast<const std::uint32_t*>(speculars),
        reinterpret_cast<const owned::NativeTextureCoordinate*>(maps), static_cast<std::uint32_t>(nNumMaps),
        reinterpret_cast<const owned::NativeVertexComponent*>(components), static_cast<std::uint32_t>(nNumComponents),
        pwIndices, static_cast<std::uint32_t>(nNumIndices)});
    view.semantic_kind=owned::SemanticKind::figure;view.ordering=owned::NativeOrdering::preserve;
    view.default_diffuse=dwDefaultAmbientColor;view.default_specular=RGBA_MAKE(0,0,0,cFog);
    if(campDraw&&(dwRenderFlags&(DXD3D_USE_VERTEX_COLOR|DXD3D_LIT))){
        // Only prelit UI figures have completed CPU colors. Lit unit previews
        // use the material/lighting path below, just like other native models.
        if(dwRenderFlags&DXD3D_LIT){view.diffuse=nullptr;view.specular=nullptr;view.flags|=4u;}
        if(dwRenderFlags&DXD3D_USE_SPECULAR)view.flags|=8u;
        const SHMatrix& clip=pGeometryProcessor->GetMatrix(MAT_STACK_PROJSPACE);
        memcpy(view.object_to_clip.data(),&clip,sizeof(clip));
        view.u_shift=fpUShift;view.v_shift=fpVShift;view.fog=cFog;
        view.morphs=reinterpret_cast<const owned::NativeMorphOffset*>(morphs);view.morph_count=morphCount;
        pDX11->SubmitC56CampFigure(view);
        return true; // This category never falls through to the OpenEI buffers/shaders.
    }
    if(dwRenderFlags&DXD3D_USE_SPECULAR)view.flags|=8u;
    view.u_shift=fpUShift;view.v_shift=fpVShift;view.fog=cFog;
    const EIRenderFigureV4 material=pDX11->GetOwnedRenderer().FigureValues();
    view.lighting.enabled=(dwRenderFlags&(DXD3D_USE_VERTEX_COLOR|DXD3D_LIT))?0u:1u;
    if(campDraw && view.lighting.enabled)view.diffuse=nullptr; // GPU supplies lit model colors.
    view.lighting.material_alpha=material.material_alpha;
    memcpy(view.lighting.material_diffuse.data(),material.material_diffuse,sizeof(material.material_diffuse));
    memcpy(view.lighting.emissive.data(),material.emissive,sizeof(material.emissive));
    memcpy(view.lighting.specular.data(),material.material_specular,sizeof(material.material_specular));
    const SHMatrix& clip=pGeometryProcessor->GetMatrix(MAT_STACK_PROJSPACE);
    memcpy(view.object_to_clip.data(),&clip,sizeof(clip));
    SHMatrix world;
    if(haveWorldMatrix)world=pGeometryProcessor->GetMatrix(MAT_STACK_WORLDSPACE);
    else Identity(world); // Interface figures use the legacy model-space lighting path.
    memcpy(view.lighting.object_to_world.data(),&world,sizeof(world));
    view.morphs=reinterpret_cast<const owned::NativeMorphOffset*>(morphs);
    view.morph_count=morphCount;
    if(dwRenderFlags&DXD3D_LIT){view.diffuse=nullptr;view.specular=nullptr;view.flags|=4u;}
    pDX11->SubmitC56Figure(view);
    return true; // Rejection is logged and skipped, never drawn by OpenEI.
}

int CRenderer::Render(SXYZW4 *plots, const int nNumPlots, SXYZW4 *norms, const int nNumNorms,
                      LPDWORD colors, LPDWORD speculars, CPlot2D *maps, const int nNumMaps,
                      SVertexComponents *components, const int nNumComponents,
                      SGvNvComponents *lightcomponents, const int nNumLightComponents,
                      WORD *pwIndices, const int nNumIndices, const float fpUShift,
                      const float fpVShift, const BYTE cFog, DWORD dwRenderFlags)
{
    if(TryOwnedFigure(plots,nNumPlots,norms,nNumNorms,colors,speculars,maps,nNumMaps,
        components,nNumComponents,pwIndices,nNumIndices,fpUShift,fpVShift,cFog,dwRenderFlags,nullptr,0))
        return TRV_VISIBLE_NOCLIP;
    return TRV_INVISIBLE;
}

/////////////////////////////////////////////////////////////////////////////////////
int CRenderer::Render(SXYZW4 *plots, CPlot2D *maps, const int nNumPlots, WORD *indices,
                      const int nNumIndices, DWORD dwColor, DWORD dwSpecular,
                      const SBoundingVolume *pBV, float fZCoeff)
{
    // CTerrainPrints.Draw(), DrawRealisticShadow2, DrawSelection()?
    DWORD dwColors[2] = { dwColor, dwSpecular };
    const int nNumDstPlots = nNumPlots * 4;
    STLVertex1 *pVertices = 0;
    pVertices = GetTempVertices(nNumDstPlots, (STLVertex1 *)0);
    for (int i = 0, j = 0; i < nNumDstPlots; j++)
    {
        for (int k = 0; k < 4; k++)
        {
            STLVertex1 &pVertix = pVertices[i];
            pVertix.sx = plots[j].x[k];
            pVertix.sy = plots[j].y[k];
            pVertix.sz = plots[j].z[k];
            pVertix.rhw = 1.0;
            Copy8Bytes(&(pVertix.color), dwColors);
            Copy8Bytes(&(pVertix.tu), &(maps[i].x));
            i++;
        }
    }
    // send to render
    if (fZCoeff != 1)
    {
        for (int i = 0; i < nNumDstPlots; ++i)
            pVertices[i].sz *= fZCoeff;
    }

    DrawIndexedPrimitive(D3DPT_TRIANGLELIST, pVertices, nNumDstPlots, indices, nNumIndices);

    return TRV_VISIBLE_NOCLIP;
}
/////////////////////////////////////////////////////////////////////////////////////
// render triangle list of TL vertices with 1 texture coords set
BOOL CRenderer::RenderTriangleSet(STLVertex1 *pVertices, DWORD dwVertexCount, LPWORD pIndices,
                                  DWORD dwIndexCount)
{

    return DrawIndexedPrimitive(D3DPT_TRIANGLELIST, pVertices, dwVertexCount, pIndices,
                                dwIndexCount, 0);
}
BOOL CRenderer::SetTexturingAlphaMode(ER_TEXTURING_ALPHA_MODE alphaMode)
{
    this->GetConstantBuffers()->cb_ps_pixelshader.data.alphaMode = alphaMode;
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
HRESULT CRenderer::SetRenderState(D3DRENDERSTATETYPE dwRenderStateType, DWORD dwRenderState)
{
    pDX11->SetFakeRenderState(dwRenderStateType, dwRenderState);
    return S_OK;
}
/////////////////////////////////////////////////////////////////////////////////////
HRESULT CRenderer::GetRenderState(D3DRENDERSTATETYPE dwRenderStateType, DWORD *pdwRenderState)
{
    pDX11->GetFakeRenderState(dwRenderStateType, pdwRenderState);
    return S_OK;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CRenderer::DrawIndexedPrimitive(D3DPRIMITIVETYPE primitiveType, LPVOID lpvVertices,
                                     DWORD dwVertexCount, LPWORD pIndices, DWORD dwIndexCount,
                                     DWORD dwFlags)
{
    if ((dwVertexCount == 0) || (dwIndexCount == 0))
        return TRUE;
    // Source arrays already contain completed homogeneous geometry. Keep its
    // W/colors/UVs; c74 shares the proxy translator, state and hardware clipping.
    static_assert(sizeof(STLVertex1) == sizeof(owned::FixedFunctionVertex), "fixed vertex layout");
    static_assert(offsetof(STLVertex1, color) == offsetof(owned::FixedFunctionVertex, diffuse), "color layout");
    static_assert(offsetof(STLVertex1, tu) == offsetof(owned::FixedFunctionVertex, u), "UV layout");
    SHMatrix projection;
    const auto& source = GetConstantBuffers()->cb_vs_vertexshader.data.ei_projspace;
    std::memcpy(&projection, &source, sizeof(projection));
    return pDX11->SubmitC74FixedFunction(primitiveType, reinterpret_cast<const owned::FixedFunctionVertex*>(lpvVertices), dwVertexCount,
        pIndices, dwIndexCount, projection) ? TRUE : FALSE;
}

void CRenderer::SetProjectionMatrix(const SHMatrix &matrix)
{
    this->GetConstantBuffers()->cb_vs_vertexshader.data.ei_projspace = sh_to_xm(matrix);
}

void CRenderer::SetProjectionMatrixOne()
{
    SHMatrix ident;
    Identity(ident);
    this->GetConstantBuffers()->cb_vs_vertexshader.data.ei_projspace = sh_to_xm(ident);
}

void CRenderer::SetProjectionMatrix()
{
    SHMatrix matrix = GetGPU()->GetMatrix(MAT_STACK_PROJSPACE);
    this->GetConstantBuffers()->cb_vs_vertexshader.data.ei_projspace = sh_to_xm(matrix);
}

BOOL CRenderer::Clear(DWORD dwCount, LPD3DRECT lpRects, DWORD dwFlags, DWORD dwColor, D3DVALUE dvZ,
                      DWORD dwStencil)
{
    if (OwnedRender::E4::OwnsDraw()) {
        const HRESULT hr = pDX11->GetRendererFacade().Visit([&](owned::Renderer& renderer) {
            const HRESULT flushed = renderer.flush_native();
            if (FAILED(flushed)) return flushed;
            return renderer.clear(dwCount, lpRects, dwFlags, dwColor, dvZ, dwStencil);
        });
        pDX11->RestoreHostBindings();
        return SUCCEEDED(hr);
    }
    if ((dwFlags & D3DCLEAR_ZBUFFER))
        pDX11->ClearZBuffer(dvZ);
    /*..
        dwFlags |= D3DCLEAR_STENCIL;*/
    return TRUE;
}

CConstantBuffers *CRenderer::GetConstantBuffers()
{
    return pDX11->GetConstantBuffers();
}

void CRenderer::SetFogBGColor(float r, float g, float b)
{
    dwBGColor = 0xFF000000 | (static_cast<DWORD>(MINT(r * 255.0f)) << 16) |
                (static_cast<DWORD>(MINT(g * 255.0f)) << 8) | static_cast<DWORD>(MINT(b * 255.0f));
    SetFogColor(dwBGColor);
    // pDX11
    pDX11->SetFogColor(dwBGColor);
    pDX11->SetBackgroundColor(dwBGColor);
}
void CRenderer::SetBGColor(DWORD dwColor)
{
    dwBGColor = dwColor;
    pDX11->SetBackgroundColor(dwColor);
}
/////////////////////////////////////////////////////////////////////////////////////
void CRenderer::SetAmbientColor(DWORD dwColor)
{
    dwAmbientColor = dwColor;
    float r = static_cast<float>((dwColor >> 16) & 0xFF) * (1.0f / 255.0f);
    float g = static_cast<float>((dwColor >> 8) & 0xFF) * (1.0f / 255.0f);
    float b = static_cast<float>(dwColor & 0xFF) * (1.0f / 255.0f);

    SetDefaultAmbientColor(dwColor);

    pDX11->GetConstantBuffers()->cb_vs_vertexshader.data.dcvAmbient = { r, g, b, 0 };
    pGeometryProcessor->GetGeometryProcessor()->SetAmbientLight(r, g, b);
}
/////////////////////////////////////////////////////////////////////////////////////
void CRenderer::SetSunlight(CLight *pLight)
{
    CLight *pOldLight = pSunLight;
    pSunLight = pLight;
    if (pSunLight)
        pSunLight->AddRef();
    if (pOldLight)
        pOldLight->Release();
    //
    if (pGeometryProcessor && pGeometryProcessor->GetGeometryProcessor())
        pGeometryProcessor->GetGeometryProcessor()->SetSunlight(pSunLight);
}
/////////////////////////////////////////////////////////////////////////////////////
void CRenderer::SetLighting(CLight *pNewSunLight, float fpAmbientR, float fpAmbientG,
                            float fpAmbientB)
{
    SetSunlight(pNewSunLight);
    SetAmbientColor(fpAmbientR, fpAmbientG, fpAmbientB);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CRenderer::BindTexture(DWORD dwStage, CTextureDescriptor &texture)
{
    texture.FinalDataLoading();
    return BindTexture(dwStage, texture.pTexture);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CRenderer::BindTexture(DWORD dwStage, CDXGISurface *pTexture)
{
    if (pTexture)
    {
        ID3D11ShaderResourceView *resView = pTexture->GetShaderResource().Get();
        return pDX11->BindTexture(dwStage, resView, pTexture->OwnedSurfaceId());
    }
    return pDX11->BindTexture(dwStage, nullptr);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CRenderer::BindTexture(DWORD dwStage, CGameBase *pTexture)
{
    if (!pTexture)
        return BindTexture(dwStage, (CDXGISurface *)0);

    ASSERT(pTexture->WhoAmI() == HPC_TEXTURE);
    CGameBase *pTex = pTexture;
    CDXGISurface *surface = static_cast<CTexture *>(pTex)->GetSurface();
    return BindTexture(dwStage, surface);
}
