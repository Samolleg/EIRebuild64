#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "Renderer.h"

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
#include "dx11/vertex.h"
#include "dx11/Surface.h"
#include "dx11/ConstantBuffer.h"
#include "dx11/EffectProcessor.h" //for delete CDX11
#include "dx11/DXShader.h"
#include "dx11/Batchers.h"
#include "Particle.h"
#include <DirectXCollision.h>
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

// Maximum number of vertexes for particles
const int N_HARDWARE_V_MAX_PARTICLES = 2048; // 4096;
/////////////////////////////////////////////////////////////////////////////////////
CRenderer theRenderer;
typedef CSimplyArray<ParticleVertex, ParticleVertex> CPVertexArray;
typedef ParticleVertex *SortVertixPtr;
typedef CPVertexArray ParticleVertexArray;

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
    pGraphicsProcessor = std::make_unique<CGraphicsProcessorMMX>();

    rcScreen = { 0, 0, LONG(settings.dwWidth), LONG(settings.dwHeight) };
    dwDefaultAmbientColor = 0xFF000000;

    pDX11 = std::make_unique<CDX11>();
    pDX11->Initialize(hWnd, settings.dwWidth, settings.dwHeight);

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
    SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND(d3dDstBlend));
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
    return pFigureSpec->Draw(this, lights, pMaterial, cFog, pRect, pTexture0, pTexture1);
}
/////////////////////////////////////////////////////////////////////////////////////
// works in projection space, center - center of lighning route to create new segment, vector -
// direction of new pair of points, res - array where result is stored, nCount - number of vertexes
// made so far
static float fp3DLineU, fp3DLineV1, fp3DLineV2;
static void GeneratePointsPair(const D3DTLVERTEX &center, const CPlot2D &ptVector,
                               CTLVertexArray &res, int &nCount, float fpW)
{
    D3DTLVERTEX &v = res[nCount++];
    float fpStep = fpW * 15 * center.rhw;
    fpStep /= fabs(ptVector);

    v = center;
    v.sx += fpStep * ptVector.y;
    v.sy -= fpStep * ptVector.x;
    v.tu = fp3DLineU;
    v.tv = fp3DLineV1;

    D3DTLVERTEX &v1 = res[nCount++];

    v1 = center;
    v1.sx -= fpStep * ptVector.y;
    v1.sy += fpStep * ptVector.x;
    v1.tu = fp3DLineU;
    v1.tv = fp3DLineV2;
}

/////////////////////////////////////////////////////////////////////////////////////
inline CPlot2D GetDirection(int nIdx, int nNum, CTLVertexArray &src)
{
    switch (nNum)
    {
    case 1:
        return CPlot2D(src[nIdx].sx - src[nIdx - 1].sx, src[nIdx].sy - src[nIdx - 1].sy);
    case 2:
        if (nIdx < src.GetSize() - 1)
            return CPlot2D(src[nIdx + 1].sx - src[nIdx - 1].sx,
                           src[nIdx + 1].sy - src[nIdx - 1].sy);
        else
            return CPlot2D(src[nIdx].sx - src[nIdx - 1].sx, src[nIdx].sy - src[nIdx - 1].sy);
        break;
    }
    ASSERT(0 && "Wrong use of CLightning::GetDirection");
    return CPlot2D(1, 0);
}

/////////////////////////////////////////////////////////////////////////////////////
BOOL CRenderer::Draw3DLine(CXYZWArray &points, const float fpLineWidth, const int nBVClipFlags,
                           float fpTextureLength, float fpTexVStart, float fpTexVWidth)
{
    CDXGeometryProcessor *pGPU = GetGPU();
    CTLPipeline *pGeometryProcessor = pGPU->GetGeometryProcessor();
    int nClipFlag = nBVClipFlags;
    int i;
    // Define temporary arrays
    static CBYTEArray clips;
    static CWORDArray tFaces;
    static CXYZWArray projVertexes;
    static CTLVertexArray srcVertexes, transVertexes;
    // set sizes of arrays
    int nVertexNum = points.GetSize();
    projVertexes.SetSize(nVertexNum);
    transVertexes.SetSize(nVertexNum);
    srcVertexes.SetSize(nVertexNum * 2 + 100);
    clips.SetSize(nVertexNum);
    tFaces.SetSize(srcVertexes.GetSize() * 3 + 150);
    tFaces.SetSize(0);
    // first, check BV agains current matrix stack
    if (nClipFlag == TRV_INVISIBLE)
        return TRUE;
    if (points.GetSize() <= 1)
        return TRUE;
    float fpDU = fpTextureLength / (points.GetSize() - 1);
    fp3DLineU = fpTextureLength != 0 ? 0 : 0.875f;
    fp3DLineV1 = fpTexVStart;
    fp3DLineV2 = fp3DLineV1 + fpTexVWidth;
    // project this points
    if (nClipFlag == TRV_VISIBLE_NOCLIP)
        pGeometryProcessor->TransformVertices(pGPU->GetMatrix(MAT_STACK_SCRNSPACE),
                                              points.GetData(), projVertexes.GetData(),
                                              points.GetSize());
    else
        nClipFlag = pGeometryProcessor->TransformVertices(pGPU->GetMatrix(MAT_STACK_PROJSPACE),
                                                          points.GetData(), projVertexes.GetData(),
                                                          clips.GetData(), points.GetSize());
    if (nClipFlag == TRV_INVISIBLE)
        return TRUE;
    // compose T&L vertices
    for (i = 0; i < projVertexes.GetSize(); i++)
    {
        Copy128Bits(&transVertexes[i].sx, &projVertexes[i].x);
        transVertexes[i].color = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0xFF);
        transVertexes[i].specular = RGBA_MAKE(0, 0, 0, 0xFF);
    }
    transVertexes[0].color = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0);
    transVertexes[projVertexes.GetSize() - 1].color = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0);
    // create points list & mesh
    BOOL bPrevPointsReady = FALSE;
    int nCurSrcVertex = 0;

    if (nClipFlag == TRV_VISIBLE_NOCLIP)
    {
        for (i = 1; i < transVertexes.GetSize(); i++)
        {
            if (!bPrevPointsReady)
            {
                GeneratePointsPair(transVertexes[i - 1], GetDirection(i, 1, transVertexes),
                                   srcVertexes, nCurSrcVertex, fpLineWidth);
            }
            fp3DLineU += fpDU;
            if (i == transVertexes.GetSize() - 1)
                GeneratePointsPair(transVertexes[i], GetDirection(i, 1, transVertexes), srcVertexes,
                                   nCurSrcVertex, fpLineWidth);
            else
                GeneratePointsPair(transVertexes[i], GetDirection(i, 2, transVertexes), srcVertexes,
                                   nCurSrcVertex, fpLineWidth);
            bPrevPointsReady = TRUE;
            // add 2 triangles
            tFaces.Add(nCurSrcVertex - 4);
            tFaces.Add(nCurSrcVertex - 3);
            tFaces.Add(nCurSrcVertex - 2);
            tFaces.Add(nCurSrcVertex - 2);
            tFaces.Add(nCurSrcVertex - 3);
            tFaces.Add(nCurSrcVertex - 1);
        }
    }
    else
    {
        pGeometryProcessor->CastProjectionToScreenC(transVertexes.GetData(),
                                                    transVertexes.GetSize());
        for (i = 1; i < transVertexes.GetSize(); i++)
        {
            // fake compare, must develop more advanced ( possible situation when both have nonzero
            // clips but segment is visible )
            if (clips[i - 1] == 0 || clips[i] == 0)
            {
                if (!bPrevPointsReady)
                {
                    GeneratePointsPair(transVertexes[i - 1], GetDirection(i, 1, transVertexes),
                                       srcVertexes, nCurSrcVertex, fpLineWidth);
                }
                fp3DLineU += fpDU;
                if (i == transVertexes.GetSize() - 1)
                    GeneratePointsPair(transVertexes[i], GetDirection(i, 1, transVertexes),
                                       srcVertexes, nCurSrcVertex, fpLineWidth);
                else
                    GeneratePointsPair(transVertexes[i], GetDirection(i, 2, transVertexes),
                                       srcVertexes, nCurSrcVertex, fpLineWidth);
                bPrevPointsReady = TRUE;
                // add 2 triangles
                tFaces.Add(nCurSrcVertex - 4);
                tFaces.Add(nCurSrcVertex - 3);
                tFaces.Add(nCurSrcVertex - 2);
                tFaces.Add(nCurSrcVertex - 2);
                tFaces.Add(nCurSrcVertex - 3);
                tFaces.Add(nCurSrcVertex - 1);
            }
            else
            {
                bPrevPointsReady = FALSE;
                fp3DLineU += fpDU;
            }
        }
    }
    if (tFaces.GetSize() == 0)
        return TRUE;
    // set 'ADD' mode
    DWORD dwOldSrcBlend, dwOldDstBlend;
    GetRenderState(D3DRENDERSTATE_SRCBLEND, &dwOldSrcBlend);
    GetRenderState(D3DRENDERSTATE_DESTBLEND, &dwOldDstBlend);
    SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_SRCALPHA);
    SetRenderState(D3DRENDERSTATE_DESTBLEND, D3DBLEND_ONE);

    DWORD screenWidth, screenHeight;
    pDX11->GetCurrentMode(screenWidth, screenHeight);
    for (i = 0; i < srcVertexes.GetSize(); i++)
    {
        STLVertex1 *pVertex = (STLVertex1 *)&srcVertexes[i];
        STLVertex1 &vertex = *pVertex;

        // convert to NDC space -1..1
        CPlot3D screen = { (2 * vertex.sx / screenWidth) - 1.f,
                           -(2 * vertex.sy / screenHeight) + 1.f, 0 };

        vertex.sx = screen.x;
        vertex.sy = screen.y;
        vertex.sz = 0.0;
        vertex.rhw = 1.0;
    }
    this->SetProjectionMatrixOne();
    // render it
    BOOL bRetVal = RenderTriangleSet(reinterpret_cast<STLVertex1 *>(srcVertexes.GetData()),
                                     srcVertexes.GetSize(), tFaces.GetData(), tFaces.GetSize());
    // restore blending mode
    SetRenderState(D3DRENDERSTATE_SRCBLEND, dwOldSrcBlend);
    SetRenderState(D3DRENDERSTATE_DESTBLEND, dwOldDstBlend);

    return bRetVal;
}
/////////////////////////////////////////////////////////////////////////////////////
// particle rendering
// ------------------------------- Sort TLVertexes and clips by z
// ------------------------------------------
struct SSortZElem
{
    union
    {
        int nZ;
        float fZ;
    };
    SSortZElem *pNext;
    SortVertixPtr lpVertex;
    union
    {
        BYTE bClip[4];
        DWORD dwClip;
    };
};

/////////////////////////////////////////////////////////////////////////////////////
static void RadixSortStage(int shift, SSortZElem **pSrc, SSortZElem **pDst, int nCount,
                           int nNormalDir)
{
    int i;
    SSortZElem *lists[256] = {};
    MemSetDWord((DWORD *)lists, 0, 256);
    for (i = 0; i < nCount; i++)
    {
        int n = (pSrc[i]->nZ >> shift) &
                0xff; // determine pile to which this element is to be stored
        pSrc[i]->pNext = lists[n];
        lists[n] = pSrc[i];
    }
    if (nNormalDir)
    {
        for (i = 0; i < 256; i++)
        {
            SSortZElem *pElem = lists[i];
            while (pElem)
            {
                *pDst++ = pElem;
                pElem = pElem->pNext;
            }
        }
    }
    else
    {
        for (i = 255; i >= 0; i--)
        {
            SSortZElem *pElem = lists[i];
            while (pElem)
            {
                *pDst++ = pElem;
                pElem = pElem->pNext;
            }
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// сортировка слияниями, эта функция - один проход слияни
static void JoinSortStage(int step, SSortZElem **pSrc, SSortZElem **pDst, int nCount)
{
    int i, n1, n2, step2, dest, numSteps;
    step2 = step * 2;
    if (step * 2 > nCount)
    {
        numSteps = nCount - step;
        memcpy(pDst + nCount - step, pSrc + nCount - step, step * sizeof(pSrc[0]));
    }
    else
    {
        numSteps = step;
    }
    numSteps = Min(step, nCount - step);
    for (i = 0; i < numSteps; i++)
    {
        n1 = i;
        n2 = i + step;
        dest = i;
        while (1)
        {
            if (pSrc[n1]->nZ < pSrc[n2]->nZ)
            {
                pDst[dest] = pSrc[n2];
                n2 += step2;
                dest += step;
                if (n2 >= nCount)
                {
                    while (n1 < nCount)
                    {
                        pDst[dest] = pSrc[n1];
                        n1 += step2;
                        dest += step;
                    }
                    break;
                }
            }
            else
            {
                pDst[dest] = pSrc[n1];
                n1 += step2;
                dest += step;
                if (n1 >= nCount)
                {
                    while (n2 < nCount)
                    {
                        pDst[dest] = pSrc[n2];
                        n2 += step2;
                        dest += step;
                    }
                    break;
                }
            }
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////
static void SortByZ(ParticleVertexArray &tlVertexes, CByteArray &clips)
{
    int i, iAdd, nStep;
    static ParticleVertexArray tmpVertexes;
    static CSimplyArray<SSortZElem, SSortZElem &> sortElements;
    static CSimplyArray<SSortZElem *, SSortZElem *> sort, sort1;
    SSortZElem **pSort, **pSort1, **pSortTemp;

    tmpVertexes.SetSize(tlVertexes.GetSize());
    memcpy(tmpVertexes.GetData(), tlVertexes.GetData(),
           sizeof(tlVertexes[0]) * tlVertexes.GetSize());

    SortVertixPtr tlBase = tmpVertexes.GetData();

    BYTE *pClips = clips.GetData();
    int nElem = tmpVertexes.GetSize() / 4;

    sortElements.SetSize(nElem);
    sort.SetSize(nElem);
    iAdd = 0;
    for (i = 0; i < nElem; i++)
    {
        SSortZElem &elem = sortElements[iAdd];
        elem.fZ = tlBase[i * 4].pos.z;
        if (elem.nZ & 0x80000000) // points with negative Z values are discarded
            continue;
        elem.dwClip = ((DWORD *)pClips)[i]; // copy 4 clip bytes
        elem.lpVertex = tlBase + i * 4;     // pointer to 4 d3dvertexes
        sort[iAdd] = &elem;
        iAdd++;
    }
    //	sortElements.SetSize( iAdd );
    nElem = iAdd; // количество элементов, которое будет сортироваться (элементы с отрицательным z
                  // отброшены)
    sort1.SetSize(nElem);

    pSort = sort.GetData();
    pSort1 = sort1.GetData();
    if (nElem > 128)
    { // radix sort is efficient on this number of particles
        RadixSortStage(0, pSort, pSort1, nElem, 1);
        RadixSortStage(8, pSort1, pSort, nElem, 0);
        RadixSortStage(16, pSort, pSort1, nElem, 1);
        RadixSortStage(24, pSort1, pSort, nElem, 0);
    }
    else if (nElem > 1)
    { // use join sort with less number of particles
        nStep = 0x40000000;
        while (nStep >= nElem)
            nStep >>= 1; // determine initial step for join sort

        while (nStep > 0)
        {
            JoinSortStage(nStep, pSort, pSort1, nElem);
            nStep >>= 1;
            pSortTemp = pSort;
            pSort = pSort1;
            pSort1 = pSortTemp; // swap pSort & pSort1
        }
    }
    // move from pSort[] to tlVertexes & clips
    tlVertexes.SetSize(nElem * 4); // ARRAY BOUNDS CHECK
    // clips.SetSize( nElem*4 );
    tlBase = tlVertexes.GetData();
    pClips = clips.GetData();
    for (i = 0; i < nElem; i++)
    {
        memcpy(tlBase + i * 4, pSort[i]->lpVertex, sizeof(tlVertexes[i]) * 4);
        ((DWORD *)pClips)[i] = pSort[i]->dwClip; // copy 4 clip bytes
    }
}

/////////////////////////////////////////////////////////////////////////////////////
BOOL CRenderer::DrawParticles(bool bUseAdd, CXYZWArray &points, CParticleToDrawArray &particles)
{
    CDXGeometryProcessor *pGPU = GetGPU();
    CTLPipeline *pGeometryProcessor = pGPU->GetGeometryProcessor();
    // Define temporary arrays
    static CXYZWArray inCoords;
    static CPVertexArray tlVertexes;
    static CByteArray clips;
    static CWordArray tFaces;

    // Set draft sizes of arrays
    inCoords.SetSize(N_HARDWARE_V_MAX_PARTICLES / 4);                         // ARRAY BOUNDS CHECK
    tlVertexes.SetSize(N_HARDWARE_V_MAX_PARTICLES * 4 /* *4 for clipping */); // ARRAY BOUNDS CHECK
    clips.SetSize(N_HARDWARE_V_MAX_PARTICLES);
    tFaces.SetSize((N_HARDWARE_V_MAX_PARTICLES / 4) * 6 * 4 /* *4 for clipping */);
    // prepare matrix
    {
        this->SetProjectionMatrix();
    }
    const SXYZW *pSrcPoint = points.GetData();
    SParticleToDraw *pParticle = particles.GetData();
    int nSrcPointsLeft = points.GetSize();
    // additive blending
    DWORD dwOldSrcBlend, dwOldDstBlend;
    if (bUseAdd)
    {
        GetRenderState(D3DRENDERSTATE_SRCBLEND, &dwOldSrcBlend);
        GetRenderState(D3DRENDERSTATE_DESTBLEND, &dwOldDstBlend);

        SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_SRCALPHA);
        SetRenderState(D3DRENDERSTATE_DESTBLEND, D3DBLEND_ONE);
    }
    // Sequently fill and render arrays
    while (nSrcPointsLeft > 0)
    {
        // form current tlvertex array
        ParticleVertex *pTL = tlVertexes.GetData();
        int nNumPart = 0;
        for (; nNumPart < (N_HARDWARE_V_MAX_PARTICLES / 4 - 1) && nSrcPointsLeft > 0;
             nNumPart++, nSrcPointsLeft--, pParticle++)
        {
            const DWORD dwParticleColor = pParticle->dwColor;
            const SParticleUV *pUV = pParticle->pUV;
            const float radius = pSrcPoint->w;

            const float xx[4] = { -radius, +radius, +radius, -radius };
            const float yy[4] = { -radius, -radius, +radius, +radius };
            const float uu[4] = { pUV->fpU0, pUV->fpU1, pUV->fpU1, pUV->fpU0 };
            const float vv[4] = { pUV->fpV0, pUV->fpV0, pUV->fpV1, pUV->fpV1 };

            for (int i = 0; i < 4; ++i)
            {
                auto &vertex = *pTL;
                vertex.pos.x = pSrcPoint->x;
                vertex.pos.y = pSrcPoint->y;
                vertex.pos.z = pSrcPoint->z;
                vertex.pos.w = 1.0;

                vertex.tuv.x = uu[i];
                vertex.tuv.y = vv[i];
                vertex.screen_dx.x = xx[i];
                vertex.screen_dx.y = yy[i];
                vertex.color = dwParticleColor;
                vertex.specular = RGBA_MAKE(0, 0, 0, 0xFF);
                pTL++;
            }
            pSrcPoint++;
        }
        assert(nNumPart > 0);

        tlVertexes.SetSize(nNumPart * 4); // ARRAY BOUNDS CHECK
        int nClipFlag = TRV_VISIBLE_NOCLIP;
        // pSrcPoint += nNumPart;
        // Sort particles by z

        // qsort(tlVertexes.GetData(), tlVertexes.GetSize(), sizeof(ParticleVertex), CmpGridPoints);
        // TODOALEX: doesn't seem to have any effect?
        // if (!bUseAdd)
        // SortByZ(tlVertexes, clips);
        nNumPart = tlVertexes.GetSize() / 4;
        // Draw particles
        WORD *pFace = tFaces.GetData();
        // compose quads
        for (int i = 0; i < nNumPart * 4; i += 4, pFace += 6)
        {
            pFace[0] = i + 0;
            pFace[1] = i + 1;
            pFace[2] = i + 2;
            pFace[3] = i + 0;
            pFace[4] = i + 2;
            pFace[5] = i + 3;
        }
        int nVertices = nNumPart * 4;
        int nIndexes = nNumPart * 2 * 3; // 6 per quad
        if (nNumPart)
        {
            pDX11->DrawParticles(reinterpret_cast<ParticleVertex *>(tlVertexes.GetData()),
                                 nVertices, tFaces.GetData(), nIndexes, 0);
            return TRUE;
        }
    }
    // restore blending after ADD
    if (bUseAdd)
    {
        SetRenderState(D3DRENDERSTATE_SRCBLEND, dwOldSrcBlend);
        SetRenderState(D3DRENDERSTATE_DESTBLEND, dwOldDstBlend);
    }

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CRenderer::DrawSelection(const CPlot3D &cp, const float fSize, const CPlot3D &ptXAxis,
                              const CPlot3D &ptYAxis)
{
    // triangle around character
    float fRadius = fSize / 3.0f;
    WORD indices[18] = { 0, 4, 3, 0, 1, 4, 1, 5, 4, 1, 2, 5, 2, 3, 5, 2, 0, 3 };
    SXYZW4 selectionPlots[2] = {};
    float fAddHeight = 0.3f;
    CPlot2D maps[8];
    //
    memset(maps, 0, sizeof(maps));
    // form vertices for selection
    CPlot3D ptPlot(cp + 3.0f * fRadius * ptXAxis + 2.0f * fRadius * ptYAxis);
    selectionPlots[0].x[0] = ptPlot.x;
    selectionPlots[0].y[0] = ptPlot.y;
    selectionPlots[0].z[0] = cp.z + fAddHeight;
    selectionPlots[0].w[0] = 1.0f;
    //
    ptPlot.SetPlot(cp - 3.0f * fRadius * ptXAxis + 2.0f * fRadius * ptYAxis);
    selectionPlots[0].x[1] = ptPlot.x;
    selectionPlots[0].y[1] = ptPlot.y;
    selectionPlots[0].z[1] = cp.z + fAddHeight;
    selectionPlots[0].w[1] = 1.0f;
    //
    ptPlot.SetPlot(cp - 5.0f * fRadius * ptYAxis);
    selectionPlots[0].x[2] = ptPlot.x;
    selectionPlots[0].y[2] = ptPlot.y;
    selectionPlots[0].z[2] = cp.z + fAddHeight;
    selectionPlots[0].w[2] = 1.0f;
    //
    ptPlot.SetPlot(cp + 2.0f * fRadius * ptXAxis + 1.4f * fRadius * ptYAxis);
    selectionPlots[0].x[3] = ptPlot.x;
    selectionPlots[0].y[3] = ptPlot.y;
    selectionPlots[0].z[3] = cp.z + fAddHeight;
    selectionPlots[0].w[3] = 1.0f;
    //
    ptPlot.SetPlot(cp - 2.0f * fRadius * ptXAxis + 1.4f * fRadius * ptYAxis);
    selectionPlots[1].x[0] = ptPlot.x;
    selectionPlots[1].y[0] = ptPlot.y;
    selectionPlots[1].z[0] = cp.z + fAddHeight;
    selectionPlots[1].w[0] = 1.0f;
    //
    ptPlot.SetPlot(cp - 4.0f * fRadius * ptYAxis);
    selectionPlots[1].x[1] = ptPlot.x;
    selectionPlots[1].y[1] = ptPlot.y;
    selectionPlots[1].z[1] = cp.z + fAddHeight;
    selectionPlots[1].w[1] = 1.0f;
    //
    selectionPlots[1].x[2] = cp.x;
    selectionPlots[1].y[2] = cp.y;
    selectionPlots[1].z[2] = cp.z + fAddHeight;
    selectionPlots[1].w[2] = 1.0f;
    //
    selectionPlots[1].x[3] = cp.x;
    selectionPlots[1].y[3] = cp.y;
    selectionPlots[1].z[3] = cp.z + fAddHeight;
    selectionPlots[1].w[3] = 1.0f;

    this->SetProjectionMatrix();

    this->Render(selectionPlots, maps, 2, indices, 18, RGBA_MAKE(0, 255, 0, 128),
                 RGBA_MAKE(0, 0, 0, 255), 0);

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
    // green rect when selecting characters /w mice
    // check rect for singularity :)
    if ((x2 < x1) || (y2 < y1))
        return FALSE;
    //
    DWORD dwColor = RGBA_MAKE(0, 0, 0, cAlpha);
    DWORD dwSpecular = RGBA_MAKE(0, 0, 0, 255);
    WORD indices[6] = { 0, 1, 2, 0, 2, 3 };
    // SLineVertex vertices[5] = { };
    STLVertex1 vertices[5] = {};
    float fPlus = 1, fMinus = -1;
    //
    if (!bFrame)
        fPlus = fMinus = 0;
    //
    vertices[0].sx = x1 + fPlus;
    vertices[0].sy = y1 + fPlus;
    vertices[0].sz = 0;
    vertices[0].rhw = 1;
    vertices[0].color = dwColor;
    vertices[0].specular = dwSpecular;

    vertices[1].sx = x1 + fPlus;
    vertices[1].sy = y2 + fMinus;
    vertices[1].sz = 0;
    vertices[1].rhw = 1;
    vertices[1].color = dwColor;
    vertices[1].specular = dwSpecular;

    vertices[2].sx = x2 + fMinus;
    vertices[2].sy = y2 + fMinus;
    vertices[2].sz = 0;
    vertices[2].rhw = 1;
    vertices[2].color = dwColor;
    vertices[2].specular = dwSpecular;

    vertices[3].sx = x2 + fMinus;
    vertices[3].sy = y1 + fPlus;
    vertices[3].sz = 0;
    vertices[3].rhw = 1;
    vertices[3].color = dwColor;
    vertices[3].specular = dwSpecular;

    vertices[4].sx = x1 + fPlus;
    vertices[4].sy = y1 + fPlus;
    vertices[4].sz = 0;
    vertices[4].rhw = 1;
    vertices[4].color = dwColor;
    vertices[4].specular = dwSpecular;

    BindTexture(0, (CDXGISurface *)0);

    SetRenderState(D3DRENDERSTATE_ZBIAS, 0);
    DrawIndexedPrimitive(D3DPT_TRIANGLELIST, vertices, 4, indices, 6);

    if (bFrame)
    {
        dwColor = RGBA_MAKE(255, 255, 255, 255);
        for (int i = 0; i < 5; ++i)
            vertices[i].color = dwColor;

        vertices[0].sx = x1;
        vertices[0].sy = y1;

        vertices[1].sx = x1;
        vertices[1].sy = y2;

        vertices[2].sx = x2;
        vertices[2].sy = y2;

        vertices[3].sx = x2;
        vertices[3].sy = y1;

        vertices[4].sx = x1;
        vertices[4].sy = y1;

        // SetRenderState(D3DRENDERSTATE_SHADEMODE, D3DSHADE_FLAT);
        // SetRenderState(D3DRENDERSTATE_ZBIAS, 1);
        // SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_DISABLE);
        // SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

        // pDX11->DrawIndexedPrimitive(, vertices, 5, 0);

        // SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
        // SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
        // SetRenderState(D3DRENDERSTATE_SHADEMODE, D3DSHADE_GOURAUD);
        // SetRenderState(D3DRENDERSTATE_ZBIAS, 0);
    }

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CRenderer::DrawPolyline(CPolyLine *pPolyline)
{
    return pPolyline->Draw(GetGPU(), this);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CRenderer::DrawPolyLine(const CSimplyArray<POINT, POINT &> &plots, DWORD dwColor)
{
    if (plots.GetSize() < 2)
        return FALSE;
    //
    CSimplyArray<SLineVertex, SLineVertex &> verts;
    verts.SetSize(plots.GetSize()); // ARRAY BOUNDS CHECK
                                    //
    for (int i = 0; i < plots.GetSize(); ++i)
    {
        verts[i].x = plots[i].x;
        verts[i].y = plots[i].y;
        verts[i].z = 0;
        verts[i].rhw = 1;
        verts[i].color = dwColor | 0xff000000;
        verts[i].specular = 0xff000000;
    }
    //
    // SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_DISABLE);
    // SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

    // DrawPrimitive(D3DPT_LINESTRIP, SLineVertex::fvfFlags, verts.GetData(), verts.GetSize(),
    //                    0);

    // SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    // SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);

    return TRUE;
}
void CRenderer::Destroy()
{
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
BOOL CRenderer::DrawTextureDebug(CGameBase *pTex, float x, float y, float fScale)
{
    CTexture *pTexture = static_cast<CTexture *>(pTex);
    if (pTexture)
    {
        float fSizeX = float(pTexture->GetSizeX()) * fScale;
        float fSizeY = float(pTexture->GetSizeY()) * fScale;
        STLVertex1 vertices[4] = { { x + 0, y + 0, 0.9f, 1, 0xffffffff, 0xff000000, 0, 0 },
                                   { x + 0, y + fSizeY, 0.9f, 1, 0xffffffff, 0xff000000, 0, 1 },
                                   { x + fSizeX, y + 0, 0.9f, 1, 0xffffffff, 0xff000000, 1, 0 },
                                   { x + fSizeX, y + fSizeY, 0.9f, 1, 0xffffffff, 0xff000000, 1,
                                     1 } };
        BindTexture(0, pTexture);
        // return GetD3D()->DrawPrimitive(D3DPT_TRIANGLESTRIP, D3DFVF_TLVERTEX, vertices, 4, 0);
    }
    return FALSE;
}
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
        return DXGI_FORMAT_B5G6R5_UNORM;
    case MMP_FORMAT_5551:
        return DXGI_FORMAT_B5G5R5A1_UNORM;
    case MMP_FORMAT_4444:
        return DXGI_FORMAT_B4G4R4A4_UNORM;
    default:
        ReportError("Can't find texture format (MMP format %d).\n", mmpFormat);
        return DXGI_FORMAT_UNKNOWN;
    }
}
BOOL CRenderer::FindBestTextureFormat(const SMMPFileHeader *pHdr, DXGI_FORMAT &format) const
{
    const DWORD mmpFormat = pHdr->dwFormat;
    format = TextureFormatFromMMPFormat(mmpFormat);
    return TRUE;
}

/////////////////////////////////////////////////////////////////////////////////////
// BOOL CRenderer::FindTextureFormat( ETypeIDs tidTextureFormat, DDPIXELFORMAT& ddpf )
// find texture pixel format from hash map by type ID
BOOL CRenderer::FindTextureFormat(DWORD dwTextureFormat, DXGI_FORMAT &format) const
{
    format = TextureFormatFromMMPFormat(dwTextureFormat);
    return TRUE;
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
int CRenderer::Render(SXYZW4 *plots, const int nNumPlots, SXYZW4 *norms, const int nNumNorms,
                      LPDWORD colors, LPDWORD speculars, CPlot2D *maps, const int nNumMaps,
                      SVertexComponents *components, const int nNumComponents,
                      SGvNvComponents *lightcomponents, const int nNumLightComponents,
                      WORD *pwIndices, const int nNumIndices, const float fpUShift,
                      const float fpVShift, const BYTE cFog, DWORD dwRenderFlags)
{
    VertexBuffer<PNTVertex> vertexBuf;
    PNTVertex *pVertices = GetTempVertices(nNumComponents, (PNTVertex *)0);

    if (dwRenderFlags & DXD3D_LIT)
    {
        DWORD dwDefaultColor = dwDefaultAmbientColor;       // RGBA_MAKE( 200, 200, 200, 255 );
        DWORD dwDefaultSpecular = RGBA_MAKE(0, 0, 0, cFog); // dwDefaultSpecularColor
        MemSetDWord(colors, dwDefaultColor, nNumNorms * 4);
        MemSetDWord(speculars, dwDefaultSpecular, nNumNorms * 4);
    }

    LPTLPIPELINE pGPU = pGeometryProcessor->GetGeometryProcessor();
    if (IsAddFog())
        pGPU->AddFog(reinterpret_cast<D3DTLVERTEX *>(pVertices), nNumComponents);

    ComposeVerticesMain(pVertices, plots, norms, colors, speculars, maps, components,
                        nNumComponents, fpUShift, fpVShift, dwRenderFlags);

    return pDX11->DrawFigurePrimitive(pVertices, nNumComponents, pwIndices, nNumIndices, dwRenderFlags);
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
    if (bAddFog)
        pGeometryProcessor->GetGeometryProcessor()->AddFog(reinterpret_cast<D3DTLVERTEX *>(
                                                               pVertices),
                                                           dwVertexCount);

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
    pDX11->DrawSimplePrimitive(lpvVertices, dwVertexCount, pIndices, dwIndexCount, dwFlags);
    return TRUE;
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

void CRenderer::SetWorldMatrix(const SHMatrix &matrix)
{
    this->GetConstantBuffers()->cb_vs_vertexshader.data.model_to_world = sh_to_xm(matrix);
}

void CRenderer::SetCameraPosition(const CPlot3D &pos)
{
    this->GetConstantBuffers()->cb_vs_vertexshader.data.cameraPosition = pos;
}

BOOL CRenderer::Clear(DWORD dwCount, LPD3DRECT lpRects, DWORD dwFlags, DWORD dwColor, D3DVALUE dvZ,
                      DWORD dwStencil)
{
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
        return pDX11->BindTexture(dwStage, resView);
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