#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "FigureClientSpecific.h"

#include "WorldClient.h"

#include "Texture.h"
#include "Material.h"
#include "PolyLine.h"
#include "dx11/RendererDX11.h"
#include "dx11/ConstantBuffer.h"

#include "Light.h"
/////////////////////////////////////////////////////////////////////////////////////
CFigureClientSpecific::CFigureClientSpecific()
{
#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
}
/////////////////////////////////////////////////////////////////////////////////////
CFigureClientSpecific::~CFigureClientSpecific()
{
    Destroy();
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CFigureClientSpecific::Create()
{
    ResetAllFlags();
    scale = CPlot3D(1, 1, 1);

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CFigureClientSpecific::Destroy()
{
    // unregister figure
    RegisterFigure(0);

    ResetAllFlags();
}
/////////////////////////////////////////////////////////////////////////////////////
// calc current animation frame for animated texture
void CFigureClientSpecific::CalcAnimatedTextureFrame(float fpTime)
{
    SetFlag(FCS_ANIMATED_TEXTURE);
    RecalcXYAdd(fpTime);
}
/////////////////////////////////////////////////////////////////////////////////////
// void CFigureClientSpecific::RecalcXYAdd()
// recalculates xadd and yadd from texture animation step
void CFigureClientSpecific::RecalcXYAdd(float fpTimeStep)
{
    if (pFigure->GetTextureTypeID() == TTID_ANIMATED1)
    {
        int nTextureAnimationStep = ((int)fpTimeStep) & 7;
        if (nTextureAnimationStep < 4)
        {
            pFigure->SetUShift(0.25f * static_cast<float>(nTextureAnimationStep));
            pFigure->SetVShift(0.0f);
        }
        else
        {
            pFigure->SetUShift(0.25f * static_cast<float>(nTextureAnimationStep - 4));
            pFigure->SetVShift(0.5f);
        }
    }
    else if (pFigure->GetTextureTypeID() == TTID_ANIMATED2)
    {
        int nTextureAnimationStep = ((int)fpTimeStep) & 15;
        pFigure->SetUShift(0.25f * static_cast<float>(nTextureAnimationStep % 4));
        pFigure->SetVShift(1.0f - 0.25f * static_cast<float>(nTextureAnimationStep / 4));
    }
    else if (pFigure->GetTextureTypeID() == TTID_ANIMATED4)
    {
        int nTextureAnimationStep = ((int)fpTimeStep) & 31;
        pFigure->SetUShift(0.125f * static_cast<float>(nTextureAnimationStep % 8));
        pFigure->SetVShift(1.0f - 0.25f * static_cast<float>(nTextureAnimationStep / 8));
    }
}
/////////////////////////////////////////////////////////////////////////////////////
__inline void ComposeD3DTLVertices(D3DTLVERTEX *vertices, SXYZW *geoms, DWORD *colors,
                                   DWORD *speculars, CPlot2D *maps, SVertexComponents *components,
                                   int nComponents)
{
    int nv;

    for (int i = 0; i < nComponents; i++)
    {
        nv = components[i].nv;
        Copy128Bits(&vertices[i].sx, &geoms[components[i].gv].x);
        vertices[i].color = colors[nv];
        vertices[i].specular = speculars[nv];
        Copy64Bits(&vertices[i].tu, &maps[components[i].tv].x);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
__inline void ComposeD3DTLVertices(D3DTLVERTEX *vertices, SXYZW *geoms, DWORD *colors,
                                   DWORD *speculars, CPlot2D *maps, SVertexComponents *components,
                                   int nComponents, float fpAddX, float fpAddY)
{
    int nv;

    for (int i = 0; i < nComponents; i++)
    {
        nv = components[i].nv;
        Copy128Bits(&vertices[i].sx, &geoms[components[i].gv].x);
        vertices[i].color = colors[nv];
        vertices[i].specular = speculars[nv];
        vertices[i].tu = maps[components[i].tv].x + fpAddX;
        vertices[i].tv = maps[components[i].tv].y + fpAddY;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
__inline void ComposeShadowD3DTLVertices(D3DTLVERTEX *vertices, SXYZW *geoms, CPlot2D *maps,
                                         SVertexComponents *components, int nComponents,
                                         int nAlpha = 255 / 2)
{
    int nv;

    for (int i = 0; i < nComponents; i++)
    {
        nv = components[i].nv;
        Copy128Bits(&vertices[i].sx, &geoms[components[i].gv].x);
        vertices[i].color = RGBA_MAKE(0, 0, 0, nAlpha);
        vertices[i].specular = RGBA_MAKE(0, 0, 0, 255);
        Copy64Bits(&vertices[i].tu, &maps[components[i].tv].x);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CFigureClientSpecific::DrawBBox(CRenderer *pRend)
{
    if (GetWorld()->CheckFlag(WC_RENDER_BOUNDBOXES))
    {
        CPlot3D aaa, aai, aia, aii, iaa, iai, iia, iii;
        CPolyLine polyline;
        // AAA
        aaa.x = pFigure->bv.ptCenter.x + pFigure->bv.ptMax.x;
        aaa.y = pFigure->bv.ptCenter.y + pFigure->bv.ptMax.y;
        aaa.z = pFigure->bv.ptCenter.z + pFigure->bv.ptMax.z;
        // AAI
        aai.x = pFigure->bv.ptCenter.x + pFigure->bv.ptMax.x;
        aai.y = pFigure->bv.ptCenter.y + pFigure->bv.ptMax.y;
        aai.z = pFigure->bv.ptCenter.z + pFigure->bv.ptMin.z;
        // AIA
        aia.x = pFigure->bv.ptCenter.x + pFigure->bv.ptMax.x;
        aia.y = pFigure->bv.ptCenter.y + pFigure->bv.ptMin.y;
        aia.z = pFigure->bv.ptCenter.z + pFigure->bv.ptMax.z;
        // AII
        aii.x = pFigure->bv.ptCenter.x + pFigure->bv.ptMax.x;
        aii.y = pFigure->bv.ptCenter.y + pFigure->bv.ptMin.y;
        aii.z = pFigure->bv.ptCenter.z + pFigure->bv.ptMin.z;
        // IAA
        iaa.x = pFigure->bv.ptCenter.x + pFigure->bv.ptMin.x;
        iaa.y = pFigure->bv.ptCenter.y + pFigure->bv.ptMax.y;
        iaa.z = pFigure->bv.ptCenter.z + pFigure->bv.ptMax.z;
        // IAI
        iai.x = pFigure->bv.ptCenter.x + pFigure->bv.ptMin.x;
        iai.y = pFigure->bv.ptCenter.y + pFigure->bv.ptMax.y;
        iai.z = pFigure->bv.ptCenter.z + pFigure->bv.ptMin.z;
        // IIA
        iia.x = pFigure->bv.ptCenter.x + pFigure->bv.ptMin.x;
        iia.y = pFigure->bv.ptCenter.y + pFigure->bv.ptMin.y;
        iia.z = pFigure->bv.ptCenter.z + pFigure->bv.ptMax.z;
        // III
        iii.x = pFigure->bv.ptCenter.x + pFigure->bv.ptMin.x;
        iii.y = pFigure->bv.ptCenter.y + pFigure->bv.ptMin.y;
        iii.z = pFigure->bv.ptCenter.z + pFigure->bv.ptMin.z;
        //
        polyline.AddPlot(aii);
        polyline.AddPlot(aai);
        polyline.AddPlot(aaa);
        polyline.AddPlot(aia);
        polyline.AddPlot(iia);
        polyline.AddPlot(iaa);
        polyline.AddPlot(aaa);
        polyline.AddPlot(aai);
        polyline.AddPlot(iai);
        polyline.AddPlot(iaa);
        polyline.AddPlot(iai);
        polyline.AddPlot(iii);
        polyline.AddPlot(iia);
        polyline.AddPlot(iii);
        polyline.AddPlot(aii);
        polyline.AddPlot(aia);
        //
        pRend->DrawPolyline(&polyline);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CFigureClientSpecific::Draw(CRenderer *pRend, const CQuaternion &qObject,
                                 const CPlot3D &cpObject, LPGAMEBASE lpNewMaterial, BYTE cFog,
                                 CRect *pRect)
{
    if (pFigure->CheckFlag(FIG_HIDDEN))
        return TRUE;
    // (interface / ci3dfigure / sky) draw without lighting
    // check visibility, move texture, account for morphs and render
    LPMATERIAL lpMaterial = static_cast<LPMATERIAL>(lpNewMaterial);
    static CXYZW4Array plots41;
    DWORD dwRenderFlags = 0;
    int i;
    //
    pFigure->ResetFlag(FIG_VISIBLE);
    //
    int nClipFlag = pRend->GetGeometryProcessor()->CheckVisibility(pFigure->bv);
    if (nClipFlag == TRV_INVISIBLE)
        return TRUE;
    //
    if (pFigure->GetTextureTypeID() >= TTID_ANIMATED1 &&
        pFigure->GetTextureTypeID() <= TTID_ANIMATED4)
        CalcAnimatedTextureFrame(theWorld.GetPreciseTime());
    // must we use default colors ?

    // never lit in shader, that is - use vertex colors
    dwRenderFlags |= DXD3D_USE_VERTEX_COLOR;
    if (pFigure->CheckFlag(FIG_USE_DEFAULT_COLORS))
    {
        // interface figure has it's own default colors
        MemSetDWord(pFigure->colors.GetData(), pFigure->dwDefaultColor, pFigure->colors.GetSize());
        MemSetDWord(pFigure->speculars.GetData(), pFigure->dwDefaultSpecular,
                    pFigure->speculars.GetSize());
    }
    else if (!pFigure->CheckFlag(FIG_LIGHTED))
    {
        // use default color from CD3D object down the line
        dwRenderFlags |= DXD3D_LIT;
    }
    //
    int nNumPlots = pFigure->nNumPlots;
    SXYZW4 *plots = pFigure->plots4;

    if (pFigure->morphs.GetSize() != 0)
    {
        int hindex, lindex;

        plots41.SetSize(pFigure->nNumPlots);
        memcpy(plots41.GetData(), pFigure->plots4, sizeof(SXYZW4) * pFigure->nNumPlots);
        for (i = 0; i < pFigure->morphs.GetSize(); i++)
        {
            hindex = i >> 2;
            lindex = i & 0x3;
            plots41[hindex].x[lindex] += pFigure->morphs[i].x;
            plots41[hindex].y[lindex] += pFigure->morphs[i].y;
            plots41[hindex].z[lindex] += pFigure->morphs[i].z;
        }

        plots = plots41.GetData();
    }
    // rendering
    pRend->Render(plots, nNumPlots, pFigure->norms4, pFigure->nNumNorms, pFigure->colors.GetData(),
                  pFigure->speculars.GetData(), pFigure->maps, pFigure->nMaps, pFigure->components,
                  pFigure->nComponents, pFigure->lightcomponents, pFigure->nLightComponents,
                  pFigure->indices, pFigure->nIndices, pFigure->GetUShift(), pFigure->GetVShift(),
                  cFog, dwRenderFlags);

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CFigureClientSpecific::Draw(CRenderer *pRend, CSimplyArray<CLight *, CLight *> &lights,
                                 LPGAMEBASE lpNewMaterial, BYTE cFog, CRect *pRect,
                                 CTexture *pTexture0, CTexture *pTexture1)
{
    // draw with lighting
    // check visibility, move texture, account for morphs
    // setup lights positions in world space
    pFigure->ResetFlag(FIG_VISIBLE);
    if (pFigure->CheckFlag(FIG_HIDDEN))
        return TRUE;
    LPMATERIAL lpMaterial = static_cast<LPMATERIAL>(lpNewMaterial);
    IGeometry *pGeometry = GetGeometry();
    static CXYZW4Array plots41;
    DWORD dwRenderFlags = 0;
    //
    int nClipFlag = pRend->GetGeometryProcessor()->CheckVisibility(pFigure->bv);
    if (nClipFlag == TRV_INVISIBLE)
        return TRUE;
    pFigure->SetFlag(FIG_VISIBLE);
    if (pFigure->GetTextureTypeID() >= TTID_ANIMATED1 &&
        pFigure->GetTextureTypeID() <= TTID_ANIMATED4)
        CalcAnimatedTextureFrame(theWorld.GetPreciseTime());

    if (!pGeometry)
        ASSERT(1);

    if (pFigure->CheckFlag(FIG_USE_DEFAULT_COLORS))
    {
        if (pGeometry)
        {
            pGeometry->ProcessLighting(pFigure->GetName(), pFigure->dwDefaultColor,
                                       pFigure->dwDefaultSpecular, pFigure->colors,
                                       pFigure->speculars);
        }
        else
        {
            pFigure->colors.SetSize(pFigure->nNumNorms * 4);    // ARRAY BOUNDS CHECK
            pFigure->speculars.SetSize(pFigure->nNumNorms * 4); // ARRAY BOUNDS CHECK
            MemSetDWord(pFigure->colors.GetData(), pFigure->dwDefaultColor,
                        pFigure->colors.GetSize());
            MemSetDWord(pFigure->speculars.GetData(), pFigure->dwDefaultSpecular,
                        pFigure->speculars.GetSize());
        }

        dwRenderFlags |= DXD3D_USE_VERTEX_COLOR;
    }

    {
        CTLPipeline *pGeometryProcessor = pRend->GetGeometryProcessor()->GetGeometryProcessor();
        // lit figure
        // setup lights with figure placement
        SHMatrix matrix;
        matrix.HomogeneousInverse(pRend->GetGeometryProcessor()->GetMatrix(MAT_STACK_WORLDSPACE));
        float fObjectsLightingCoeff;
        fObjectsLightingCoeff = theSettings.fObjectsLightingCoeff;
        for (int i = 0; i < lights.GetSize(); ++i)
        {
            lights[i]->SetRespectCP(matrix.RotateHVector(lights[i]->GetCP()));
            lights[i]->SetupWithMaterial(lpMaterial, fObjectsLightingCoeff);
        }
        // sunlight
        CLight *pSunlight = theRenderer.GetSunlight();
        if (pSunlight)
        {
            D3DVECTOR d3dDirection = pSunlight->GetDirection();
            CPlot3D nl(-d3dDirection.x, -d3dDirection.y, -d3dDirection.z);
            nl = matrix.RotateVector(nl);
            pSunlight->SetRespectDirection(nl);
            pSunlight->SetupWithMaterial(lpMaterial, fObjectsLightingCoeff);

            pRend->GetConstantBuffers()->cb_vs_vertexlights.data.sunlight = CLight11Packed(
                *pSunlight);
            pGeometryProcessor->SetSunlight(pSunlight);
        }
        // perform lighting
        for (int i = 0; i < min(16, lights.GetSize()); ++i)
        {
            pRend->GetConstantBuffers()->cb_vs_vertexlights.data.lights[i] = CLight11Packed(
                *lights[i]);
        }
        pRend->GetConstantBuffers()->cb_vs_vertexshader.data.nlights = lights.GetSize();
        pRend->GetConstantBuffers()->cb_vs_vertexshader.data.dvEmissive = lpMaterial->GetEmissive();

        if (pGeometry) // light {unhuma} unit
            pGeometry->ProcessLighting(pFigure->GetName(), lights, lpMaterial, cFog,
                                       pFigure->colors, pFigure->speculars);
        //
        if (!lpMaterial->CheckFlag(MAT_USE_SPECULAR))
            MemSetDWord(pFigure->speculars.GetData(), RGBA_MAKE(0, 0, 0, cFog),
                        pFigure->speculars.GetSize());

        pFigure->SetFlag(FIG_LIGHTED);
    }

    if (lpMaterial->CheckFlag(MAT_USE_SPECULAR))
        dwRenderFlags |= DXD3D_USE_SPECULAR;

    if (pTexture0 && pTexture1)
    {
        // huh?
        ASSERT(0);
    }

    if (pGeometry)
    {
        pGeometry->Render(pFigure->GetName(), dwRenderFlags, pFigure->colors, pFigure->speculars,
                          pFigure->morphs, pFigure->GetUShift(), pFigure->GetVShift());
    }
    else
    {
        ASSERT(0);
        // wtf s that branch?
        int nNumPlots = pFigure->nNumPlots;
        SXYZW4 *plots = pFigure->plots4;
        if (pFigure->morphs.GetSize() != 0)
        {
            int hindex, lindex;

            plots41.SetSize(pFigure->nNumPlots);
            memcpy(plots41.GetData(), pFigure->plots4, sizeof(SXYZW4) * pFigure->nNumPlots);
            for (int i = 0; i < pFigure->morphs.GetSize(); i++)
            {
                hindex = i >> 2;
                lindex = i & 0x3;
                plots41[hindex].x[lindex] += pFigure->morphs[i].x;
                plots41[hindex].y[lindex] += pFigure->morphs[i].y;
                plots41[hindex].z[lindex] += pFigure->morphs[i].z;
            }

            plots = plots41.GetData();
        }
        nClipFlag = pRend->Render(plots, nNumPlots, pFigure->norms4, pFigure->nNumNorms,
                                  pFigure->colors.GetData(), pFigure->speculars.GetData(),
                                  pFigure->maps, pFigure->nMaps, pFigure->components,
                                  pFigure->nComponents, pFigure->lightcomponents,
                                  pFigure->nLightComponents, pFigure->indices, pFigure->nIndices,
                                  pFigure->GetUShift(), pFigure->GetVShift(), cFog, dwRenderFlags);
    }
    //
    DrawBBox(&theRenderer);

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// void CFigureClientSpecific::Calc2DClipRect( CXYZW4Array& homoscreen )
// calculate clip rect
void CFigureClientSpecific::Calc2DClipRect(CXYZW4Array &homoscreen)
{
    float minx = FLT_MAX, miny = FLT_MAX, minz = FLT_MAX;
    float maxx = -FLT_MAX, maxy = -FLT_MAX, maxz = -FLT_MAX;

    for (int i = 0; i < homoscreen.GetSize(); i++)
    {
        // min/max X
        minx = Min(homoscreen[i].x[0], minx);
        maxx = Max(homoscreen[i].x[0], maxx);
        minx = Min(homoscreen[i].x[1], minx);
        maxx = Max(homoscreen[i].x[1], maxx);
        minx = Min(homoscreen[i].x[2], minx);
        maxx = Max(homoscreen[i].x[2], maxx);
        minx = Min(homoscreen[i].x[3], minx);
        maxx = Max(homoscreen[i].x[3], maxx);
        // min/max Y
        miny = Min(homoscreen[i].y[0], miny);
        maxy = Max(homoscreen[i].y[0], maxy);
        miny = Min(homoscreen[i].y[1], miny);
        maxy = Max(homoscreen[i].y[1], maxy);
        miny = Min(homoscreen[i].y[2], miny);
        maxy = Max(homoscreen[i].y[2], maxy);
        miny = Min(homoscreen[i].y[3], miny);
        maxy = Max(homoscreen[i].y[3], maxy);
    }

    rcClip.SetRect(int(minx), int(miny), int(maxx), int(maxy));
}
/////////////////////////////////////////////////////////////////////////////////////
// void CFigureClientSpecific::Calc2DClipRect( CTLVertexArray& homoscreen )
// calculate clip rect
void CFigureClientSpecific::Calc2DClipRect(CTLVertexArray &tlscreen)
{
    float minx = FLT_MAX, miny = FLT_MAX, minz = FLT_MAX;
    float maxx = -FLT_MAX, maxy = -FLT_MAX, maxz = -FLT_MAX;

    for (int i = 0; i < tlscreen.GetSize(); i++)
    {
        // min/max X
        minx = Min(tlscreen[i].sx, minx);
        maxx = Max(tlscreen[i].sx, maxx);
        // min/max Y
        miny = Min(tlscreen[i].sy, miny);
        maxy = Max(tlscreen[i].sy, maxy);
    }

    rcClip.SetRect(int(minx), int(miny), int(maxx), int(maxy));
}
/////////////////////////////////////////////////////////////////////////////////////
// void CFigureClientSpecific::Calc2DClipRect( CXYZWArray& homoscreen )
// calculate clip rect
void CFigureClientSpecific::Calc2DClipRect(CXYZWArray &homoscreen)
{
    float minx = FLT_MAX, miny = FLT_MAX, minz = FLT_MAX;
    float maxx = -FLT_MAX, maxy = -FLT_MAX, maxz = -FLT_MAX;

    for (int i = 0; i < homoscreen.GetSize(); i++)
    {
        // min/max X/Y
        minx = Min(homoscreen[i].x, minx);
        maxx = Max(homoscreen[i].x, maxx);
        miny = Min(homoscreen[i].y, miny);
        maxy = Max(homoscreen[i].y, maxy);
        i++;

        minx = Min(homoscreen[i].x, minx);
        maxx = Max(homoscreen[i].x, maxx);
        miny = Min(homoscreen[i].y, miny);
        maxy = Max(homoscreen[i].y, maxy);
        i++;

        minx = Min(homoscreen[i].x, minx);
        maxx = Max(homoscreen[i].x, maxx);
        miny = Min(homoscreen[i].y, miny);
        maxy = Max(homoscreen[i].y, maxy);
        i++;

        minx = Min(homoscreen[i].x, minx);
        maxx = Max(homoscreen[i].x, maxx);
        miny = Min(homoscreen[i].y, miny);
        maxy = Max(homoscreen[i].y, maxy);
    }

    rcClip.SetRect(int(minx), int(miny), int(maxx), int(maxy));
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CFigureClientSpecific::DrawRealisticShadow2(SHMatrix &matShadow, SHMatrix &matScale,
                                                 LPVOID lpRenderParam)
{
    return FALSE;
    CObject3D *lpObject = (CObject3D *)pFigure->GetObject3D();
    LPGRAPHICSPROCESSOR pGrPU = GetRenderer()->GetGrPU();
    CSimplyArray<CPoint, CPoint &> dst;
    SHMatrix matrix, matSet;
    int i;

    // prepare matrix set
    pFigure->PrepareForDraw(lpObject->GetOrientation(), VNULL);
    memcpy(&matrix, &(pFigure->d3dmatrix), sizeof(SHMatrix));
    matrix._14 = matrix._24 = matrix._34 = 0;
    matSet = matrix * matShadow * matScale;
    // transform
    dst.SetSize(pFigure->nNumPlots * 4);
    GetRenderer()->GetGPU()->GetGeometryProcessor()->TransformShadow(matrix * matShadow * matScale,
                                                                     pFigure->plots4, dst.GetData(),
                                                                     pFigure->nNumPlots);
    // _DEBUG check for bounds
#ifdef _DEBUG
    // CRAP !!!
    for (i = 0; i < dst.GetSize(); i++)
    {
        if ((dst[i].x > 63) || (dst[i].y > 63) || (dst[i].x < 0) || (dst[i].y < 0))
        {
            dst[i].x = Min((short)63, Max((short)0, (short)dst[i].x));
            dst[i].y = Min((short)63, Max((short)0, (short)dst[i].y));
            TRACE1("SHADOW BOUNDS VIOLATION in figure \"%s\"\n", pFigure->GetName());
        }
    }
    // end of CRAP !!!
#endif // _DEBUG
    // rasterize
    for (i = 0; i < pFigure->nIndices; i += 3)
    {
        int index11 = pFigure->components[pFigure->indices[i + 0]].gv;
        int index12 = pFigure->components[pFigure->indices[i + 0]].tv;
        int index21 = pFigure->components[pFigure->indices[i + 1]].gv;
        int index22 = pFigure->components[pFigure->indices[i + 1]].tv;
        int index31 = pFigure->components[pFigure->indices[i + 2]].gv;
        int index32 = pFigure->components[pFigure->indices[i + 2]].tv;
        ((SShadowTextureRenderParam *)lpRenderParam)->x1 = dst[index11].x;
        ((SShadowTextureRenderParam *)lpRenderParam)->y1 = dst[index11].y;
        ((SShadowTextureRenderParam *)lpRenderParam)->tu1 = MINT(pFigure->maps[index12].x * 255);
        ((SShadowTextureRenderParam *)lpRenderParam)->tv1 = MINT(pFigure->maps[index12].y * 255);
        ((SShadowTextureRenderParam *)lpRenderParam)->x2 = dst[index21].x;
        ((SShadowTextureRenderParam *)lpRenderParam)->y2 = dst[index21].y;
        ((SShadowTextureRenderParam *)lpRenderParam)->tu2 = MINT(pFigure->maps[index22].x * 255);
        ((SShadowTextureRenderParam *)lpRenderParam)->tv2 = MINT(pFigure->maps[index22].y * 255);
        ((SShadowTextureRenderParam *)lpRenderParam)->x3 = dst[index31].x;
        ((SShadowTextureRenderParam *)lpRenderParam)->y3 = dst[index31].y;
        ((SShadowTextureRenderParam *)lpRenderParam)->tu3 = MINT(pFigure->maps[index32].x * 255);
        ((SShadowTextureRenderParam *)lpRenderParam)->tv3 = MINT(pFigure->maps[index32].y * 255);
        if (((SShadowTextureRenderParam *)lpRenderParam)->lpSrcTexture)
            pGrPU->RasterizeTriangleWithAlpha(*((SShadowTextureRenderParam *)lpRenderParam));
        else
            pGrPU->RasterizeTriangle(*((SShadowTextureRenderParam *)lpRenderParam));
    }

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
