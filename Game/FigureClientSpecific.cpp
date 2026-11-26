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
#include "FigureScreenBounds.h"
#include "native_placed_figure.h"
#include "model_draw.h"
#include "model_preview_material.h"
#include "OwnedRender/ModelPreviewLighting.h"
#include "GameJournal.h"
#include "CorePerf.h"
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
    scale = CPlot3D(1, 1, 1);pickingFrame=-1;pickingExact=false;rcPickBounds.SetRectEmpty();

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
bool CFigureClientSpecific::SubmitNativeFigure(CRenderer* renderer, CMaterial* material,
    BYTE fog, CSimplyArray<CLight*, CLight*>* lights)
{
    const auto reject = [](const char* step) {
        static unsigned reports = 0;
        if (reports++ < 8) GameJournal::Event("RENDER", "native_figure_rejected", "step=%s", step);
        return false;
    };
    auto* gpu = renderer->GetGPU();
    if (!gpu->IsHaveMatrix(MAT_STACK_PROJSPACE)) {
        renderer->GetD3D()->ReportC56FigureNoMatrix();
        return false;
    }
    owned::NativeFigureStreams streams;
    // Lit models use their current geometry container, including complection
    // edits. Placed/interface figures own the exact mapped streams themselves.
    auto* geometry = lights ? GetGeometry() : nullptr;
    if (geometry) {
        if (!geometry->GetNativeFigureStreams(pFigure->GetName(), streams)) return reject("streams");
    } else {
        streams = {reinterpret_cast<const owned::NativePackedGroup*>(pFigure->plots4),
            static_cast<std::uint32_t>(pFigure->nNumPlots),
            reinterpret_cast<const owned::NativePackedGroup*>(pFigure->norms4),
            static_cast<std::uint32_t>(pFigure->nNumNorms), nullptr, nullptr,
            reinterpret_cast<const owned::NativeTextureCoordinate*>(pFigure->maps),
            static_cast<std::uint32_t>(pFigure->nMaps),
            reinterpret_cast<const owned::NativeVertexComponent*>(pFigure->components),
            static_cast<std::uint32_t>(pFigure->nComponents), pFigure->indices,
            static_cast<std::uint32_t>(pFigure->nIndices)};
    }
    const bool defaults = pFigure->CheckFlag(FIG_USE_DEFAULT_COLORS) != 0;
    const bool studioPreview = owned::model_draw_is_character_preview();
    const bool prelit = !studioPreview && !lights && !defaults && pFigure->CheckFlag(FIG_LIGHTED);
    if (prelit) {
        streams.diffuse = reinterpret_cast<const std::uint32_t*>(pFigure->colors.GetData());
        streams.specular = reinterpret_cast<const std::uint32_t*>(pFigure->speculars.GetData());
    }
    owned::NativeGeometryView view;
    owned::apply_native_figure_streams(view, streams);
    view.semantic_kind = owned::SemanticKind::figure;
    memcpy(view.object_to_clip.data(), &gpu->GetMatrix(MAT_STACK_PROJSPACE), sizeof(SHMatrix));
    view.u_shift = pFigure->GetUShift(); view.v_shift = pFigure->GetVShift(); view.fog = fog;
    view.flags = material && material->CheckFlag(MAT_USE_SPECULAR) ? 8u : 0u;
    owned::NativeLighting response;
    owned::NativeWorldLighting illumination;
    std::vector<owned::RetainedWorldPointLight> points;
    if (studioPreview) {
        // Standalone head figures use a uniform UI scale. Compute the camera
        // frame in their local space, so ordinary item icons remain untouched.
        OwnedRender::ModelPreviewLighting studio;
        if(!OwnedRender::produce_model_preview_lighting(view.object_to_clip,{0,0,0},studio))
            return reject("preview.studio_lighting");
        illumination=studio.world;
        const std::array<float,16> local{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        response=OwnedRender::model_preview_tinted_lighting(local,defaults?pFigure->GetDefaultColor():0xffffffffu);
        view.interface_preview=true;
    } else if (lights && !defaults) {
        if (!material || !gpu->IsHaveMatrix(MAT_STACK_WORLDSPACE)) return reject("material_or_world_matrix");
        std::array<float,16> world;
        memcpy(world.data(), &gpu->GetMatrix(MAT_STACK_WORLDSPACE), sizeof(SHMatrix));
        const auto& m = material->GetMaterial();
        if (!owned::make_native_figure_lighting(&m.diffuse.r, &m.emissive.r,
                &m.specular.r, world, response)) return reject("material_values");
        const DWORD ambient = renderer->GetAmbientColor();
        illumination.ambient = {float((ambient >> 16) & 255) / 255.f,
            float((ambient >> 8) & 255) / 255.f, float(ambient & 255) / 255.f, 1.f};
        illumination.sunlight_diffuse = {};
        if (auto* sun = renderer->GetSunlight()) {
            const auto& color = sun->GetDiffuse(); const auto& direction = sun->GetDirection();
            illumination.sunlight_diffuse = {color.r, color.g, color.b, 1.f};
            illumination.sunlight_direction = {-direction.x, -direction.y, -direction.z, 0.f};
        }
        for (int i = 0; i < lights->GetSize(); ++i) {
            const auto* light = (*lights)[i];
            if (!light || light->CheckFlag(LIT_INACTIVE) || !(light->GetRange() > 0.f)) continue;
            const auto& center = light->GetCP(); const auto& color = light->GetDiffuse();
            owned::RetainedWorldPointLight point;
            point.source_id = i + 1; point.center = {center.x, center.y, center.z};
            point.color = {color.r, color.g, color.b}; point.radius = light->GetRange();
            points.push_back(point);
        }
    }
    owned::apply_native_placed_state(view,
        reinterpret_cast<const owned::NativeMorphOffset*>(pFigure->morphs.GetData()),
        pFigure->morphs.GetSize(), response, owned::NativeOrdering::preserve,
        !studioPreview && (defaults || (!lights && !prelit)),
        defaults ? pFigure->GetDefaultColor() : renderer->GetAmbientColor(),
        defaults ? pFigure->GetDefaultSpecular() : (std::uint32_t(fog) << 24));
    owned::apply_model_preview_material(view, {!studioPreview && (defaults || (!lights && !prelit)),
        material && material->CheckFlag(MAT_USE_SPECULAR), view.default_diffuse,
        view.default_specular, response, owned::NativeOrdering::preserve});
    return renderer->GetD3D()->SubmitC74Figure(view, response.enabled ? &illumination : nullptr, points);
}

BOOL CFigureClientSpecific::Draw(CRenderer *pRend, const CQuaternion &qObject,
    const CPlot3D &cpObject, LPGAMEBASE lpNewMaterial, BYTE cFog, CRect *pRect)
{
    if (pFigure->CheckFlag(FIG_HIDDEN)) return TRUE;
    pFigure->ResetFlag(FIG_VISIBLE);
    if (pRend->GetGeometryProcessor()->CheckVisibility(pFigure->bv) == TRV_INVISIBLE) return TRUE;
    if (pFigure->GetTextureTypeID() >= TTID_ANIMATED1 && pFigure->GetTextureTypeID() <= TTID_ANIMATED4)
        CalcAnimatedTextureFrame(theWorld.GetPreciseTime());
    SubmitNativeFigure(pRend, static_cast<CMaterial*>(lpNewMaterial), cFog, nullptr);
    return TRUE; // Rejected owned figures are diagnosed, with no alternate draw.
}
//
void CFigureClientSpecific::CalculateScreenBounds() const
{
    // Deferred until a selection candidate survives the figure BV rectangle.
    // The captured world matrix belongs to this frame's prepared host pose.
    CorePerf::Scope timing(CorePerf::Picking);
    pickingExact=true;rcClip.SetRectEmpty();
    IGeometry *pGeometry = GetGeometry();
    {
        const SXYZW4 *plots = pFigure->plots4;
        const SVertexComponents *components = pFigure->components;
        const WORD *indices = pFigure->indices;
        DWORD count = pFigure->nNumPlots, indexCount = pFigure->nIndices;
        const bool haveGeometry = !pGeometry || pGeometry->GetRangeGeometryData(
            pFigure->GetName(), plots, components, indices, count, indexCount);
        FigureScreenBounds::Bounds bounds;
        if (haveGeometry && plots && components && indices)
        {
            pickingVertices.resize(size_t(count)*4);
            pickingVisited.assign(size_t(count)*4,0);
            for (DWORD i = 0; i + 2 < indexCount; i += 3)
            {
                FigureScreenBounds::Point triangle[3];
                bool valid = true;
                for (int j = 0; j < 3; ++j)
                {
                    const DWORD vertex = components[indices[i + j]].gv;
                    if (vertex / 4 >= count) { valid = false; break; }
                    if(!pickingVisited[vertex]) {
                        const SXYZW4 &quad=plots[vertex/4];const int lane=vertex%4;
                        CPlot3D local(quad.x[lane],quad.y[lane],quad.z[lane]);
                        if(vertex<DWORD(pFigure->morphs.GetSize()))local+=pFigure->morphs[vertex];
                        pickingVertices[vertex]=pickingMatrix.Rotate4Vector(local);pickingVisited[vertex]=1;
                    }
                    const auto& projected=pickingVertices[vertex];
                    triangle[j]={projected.x,projected.y,projected.w};
                }
                if (valid) bounds.AddTriangle(triangle);
            }
        }
        if (bounds.valid)
            rcClip.SetRect(bounds.Left(), bounds.Top(), bounds.Right(), bounds.Bottom());
    }
}

void CFigureClientSpecific::PrepareOwnedPicking(CRenderer *pRend, CRect *pRect)
{
    if (pRect) pRect->SetRectEmpty();
    pFigure->ResetFlag(FIG_VISIBLE);
    if (pFigure->CheckFlag(FIG_HIDDEN) ||
        pRend->GetGeometryProcessor()->CheckVisibility(pFigure->bv) == TRV_INVISIBLE)
        return;
    pFigure->SetFlag(FIG_VISIBLE);
    if (pFigure->GetTextureTypeID() >= TTID_ANIMATED1 &&
        pFigure->GetTextureTypeID() <= TTID_ANIMATED4)
        CalcAnimatedTextureFrame(theWorld.GetPreciseTime());
    if(!pRect)return;
    auto* gpu=pRend->GetGeometryProcessor();
    const SHMatrix matrix=gpu->GetWorldToScreenMatrix()*gpu->GetMatrix(MAT_STACK_WORLDSPACE);
    const int frame=theWorld.GetFrameCounter();
    if(pickingFrame!=frame||memcmp(&pickingMatrix,&matrix,sizeof(matrix))!=0) {
        pickingFrame=frame;pickingMatrix=matrix;pickingExact=false;
        FigureScreenBounds::Point vertices[8];const auto& bv=pFigure->bv;
        for(unsigned i=0;i<8;++i) {
            const auto low=bv.IsSphere()?bv.ptCenter-CPlot3D(bv.fpRadius,bv.fpRadius,bv.fpRadius):bv.ptMin;
            const auto high=bv.IsSphere()?bv.ptCenter+CPlot3D(bv.fpRadius,bv.fpRadius,bv.fpRadius):bv.ptMax;
            const auto p=matrix.Rotate4Vector(CPlot3D((i&1)?high.x:low.x,(i&2)?high.y:low.y,(i&4)?high.z:low.z));
            vertices[i]={p.x,p.y,p.w};
        }
        FigureScreenBounds::Bounds bounds;
        bounds.AddBox(vertices);
        rcPickBounds.SetRectEmpty();rcClip.SetRectEmpty();
        if(bounds.valid)rcPickBounds.SetRect(bounds.Left(),bounds.Top(),bounds.Right(),bounds.Bottom());
    }
    *pRect=rcPickBounds;
}

BOOL CFigureClientSpecific::Draw(CRenderer *pRend, CSimplyArray<CLight *, CLight *> &lights,
    LPGAMEBASE lpNewMaterial, BYTE cFog, CRect *pRect, CTexture *pTexture0, CTexture *pTexture1)
{
    // Native picking still consumes source bounds. Rendering consumes packed
    // groups, current morphs and raw material/light facts through the c74 path.
    if (owned::model_draw_is_preview()) {
        // A second UI view must not replace world FIG_VISIBLE/rcClip used by
        // picking. Draw admission uses the current preview transform only.
        if (pFigure->CheckFlag(FIG_HIDDEN) ||
            pRend->GetGeometryProcessor()->CheckVisibility(pFigure->bv) == TRV_INVISIBLE)
            return TRUE;
        if (pFigure->GetTextureTypeID() >= TTID_ANIMATED1 &&
            pFigure->GetTextureTypeID() <= TTID_ANIMATED4)
            CalcAnimatedTextureFrame(theWorld.GetPreciseTime());
    } else {
        PrepareOwnedPicking(pRend, pRect);
        if (!pFigure->CheckFlag(FIG_VISIBLE)) return TRUE;
    }
    const bool submitted = SubmitNativeFigure(pRend, static_cast<CMaterial*>(lpNewMaterial), cFog, &lights);
    if (submitted && !owned::model_draw_is_preview()) pFigure->SetFlag(FIG_LIGHTED);
    DrawBBox(pRend);
    return TRUE;
}
//
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
