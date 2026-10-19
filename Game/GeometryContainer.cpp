#include "StdAfx.h"
#include "ModShadowRaster.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "GeometryContainer.h"
#include "GeometryManager.h"

#include "Material.h"
#include "native_placed_figure.h"
#include "dx11/RendererDX11.h"

/////////////////////////////////////////////////////////////////////////////////////
static CXYZW4Array plots41;
// Geometry is created/mutated on the existing source render thread. No RNG or
// save state is touched; an exhausted ID space refuses native submission.
static uint64_t NextOwnedGeometryIdentity() {
    static uint64_t next = 1;
    if(next==UINT64_MAX)return 0;
    return next++;
}
/////////////////////////////////////////////////////////////////////////////////////
DWORD GetFVFVertexSize(DWORD dwFVFFlags)
{
    DWORD dwSize = 0;
    //
    if (dwFVFFlags & D3DFVF_XYZ)
        dwSize += 3 * sizeof(float);
    else if (dwFVFFlags & D3DFVF_XYZRHW)
        dwSize += 4 * sizeof(float);
    else if (dwFVFFlags & D3DFVF_XYZB1)
        dwSize += 4 * sizeof(float);
    else if (dwFVFFlags & D3DFVF_XYZB2)
        dwSize += 5 * sizeof(float);
    else if (dwFVFFlags & D3DFVF_XYZB3)
        dwSize += 6 * sizeof(float);
    else if (dwFVFFlags & D3DFVF_XYZB4)
        dwSize += 7 * sizeof(float);
    else if (dwFVFFlags & D3DFVF_XYZB5)
        dwSize += 8 * sizeof(float);
    //
    if (dwFVFFlags & D3DFVF_NORMAL)
        dwSize += 3 * sizeof(float);
    //
    if (dwFVFFlags & D3DFVF_DIFFUSE)
        dwSize += sizeof(DWORD);
    //
    if (dwFVFFlags & D3DFVF_SPECULAR)
        dwSize += sizeof(DWORD);
    //
    if (dwFVFFlags & D3DFVF_TEX0)
        dwSize += 0;
    else if (dwFVFFlags & D3DFVF_TEX1)
        dwSize += 2 * sizeof(float);
    else if (dwFVFFlags & D3DFVF_TEX2)
        dwSize += 4 * sizeof(float);
    else if (dwFVFFlags & D3DFVF_TEX3)
        dwSize += 6 * sizeof(float);
    else if (dwFVFFlags & D3DFVF_TEX4)
        dwSize += 8 * sizeof(float);
    else if (dwFVFFlags & D3DFVF_TEX5)
        dwSize += 10 * sizeof(float);
    else if (dwFVFFlags & D3DFVF_TEX6)
        dwSize += 12 * sizeof(float);
    else if (dwFVFFlags & D3DFVF_TEX7)
        dwSize += 14 * sizeof(float);
    else if (dwFVFFlags & D3DFVF_TEX8)
        dwSize += 16 * sizeof(float);
    //
    return dwSize;
}
/////////////////////////////////////////////////////////////////////////////////////
CGeometryContainer::CGeometryContainer(CGeometryManager *pManager, const std::string &szNewName,
                                       float fNewStrength, float fNewGrace, float fNewTall,
                                       CModelResourceEntry *_pModel)
    : IGeometry(szNewName)
    , pGeometryManager(pManager)
    , pModel(_pModel)
    , fStrength(fNewStrength)
    , fGrace(fNewGrace)
    , fTall(fNewTall)
{
    pTL = pGeometryManager->GetD3D()->GetGeometryProcessor()->GetGeometryProcessor();
}
/////////////////////////////////////////////////////////////////////////////////////
void CGeometryContainer::SetNewComplection(float fNewStrength, float fNewGrace, float fNewTall)
{
    fStrength = fNewStrength;
    fGrace = fNewGrace;
    fTall = fNewTall;
    ++ownedRevision;
}
/////////////////////////////////////////////////////////////////////////////////////
void CGeometryContainer::GetRangeNames(std::vector<std::string> &szNames) const
{
    for (auto iter = ranges.begin(); iter != ranges.end(); ++iter)
        szNames.push_back(iter->first);
}
/////////////////////////////////////////////////////////////////////////////////////
bool CGeometryContainer::IsHaveRange(const std::string &szRangeName) const
{
    return (ranges.find(szRangeName) != ranges.end());
}
/////////////////////////////////////////////////////////////////////////////////////
bool CGeometryContainer::AddRange(const std::string &szRangeName)
{
    return pGeometryManager->AddGeometryRange(this, szRangeName);
}
/////////////////////////////////////////////////////////////////////////////////////
bool CGeometryContainer::AddRange(const std::string &szRangeName, SXYZW4 *rangePlots4,
                                  DWORD dwNumPlots, SXYZW4 *rangeNorms4, DWORD dwNumNorms,
                                  CPlot2D *rangeMaps, DWORD dwNumMaps,
                                  SVertexComponents *rangeComponents, DWORD dwNumComponents,
                                  SGvNvComponents *rangeLightComponents, DWORD dwNumLightComponents,
                                  WORD *rangeIndices, DWORD dwNumIndices)
{
    auto iter = ranges.find(szRangeName);
    if (iter != ranges.end())
        return false; // such range already exist
    SGeometryRangeSW range;
    range.ownedIdentity = NextOwnedGeometryIdentity();
    // fill data and range structure
    range.dwPlotsStart = plots4.GetSize();
    range.dwNumPlots = dwNumPlots;
    plots4.SetSize(range.dwPlotsStart + dwNumPlots);
    memcpy(plots4.GetData() + range.dwPlotsStart, rangePlots4, dwNumPlots * sizeof(SXYZW4));

    range.dwNormsStart = norms4.GetSize();
    range.dwNumNorms = dwNumNorms;
    norms4.SetSize(range.dwNormsStart + dwNumNorms);
    memcpy(norms4.GetData() + range.dwNormsStart, rangeNorms4, dwNumNorms * sizeof(SXYZW4));

    range.dwMapsStart = maps.GetSize();
    range.dwNumMaps = dwNumMaps;
    maps.SetSize(range.dwMapsStart + dwNumMaps);
    memcpy(maps.GetData() + range.dwMapsStart, rangeMaps, dwNumMaps * sizeof(CPlot2D));

    range.dwComponentsStart = components.GetSize();
    range.dwNumComponents = dwNumComponents;
    components.SetSize(range.dwComponentsStart + dwNumComponents);
    memcpy(components.GetData() + range.dwComponentsStart, rangeComponents,
           dwNumComponents * sizeof(SVertexComponents));

    range.dwLightComponentsStart = lightcomponents.GetSize();
    range.dwNumLightComponents = dwNumLightComponents;
    lightcomponents.SetSize(range.dwLightComponentsStart + dwNumLightComponents);
    memcpy(lightcomponents.GetData() + range.dwLightComponentsStart, rangeLightComponents,
           dwNumLightComponents * sizeof(SGvNvComponents));

    range.dwIndicesStart = indices.GetSize();
    range.dwNumIndices = dwNumIndices;
    indices.SetSize(range.dwIndicesStart + dwNumIndices);
    memcpy(indices.GetData() + range.dwIndicesStart, rangeIndices, dwNumIndices * sizeof(WORD));
    // store range structure
    ranges[szRangeName] = range;
    ++ownedRevision;

    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CGeometryContainer::RemoveRange(const std::string &szRangeName)
{
    auto iter = ranges.find(szRangeName);
    if (iter == ranges.end())
        return false; // no such range
    //
    SGeometryRangeSW range = iter->second;
    // remove range
    ranges.erase(iter);
    ownedFigureSources.Remove(szRangeName);
    ++ownedRevision;
    // remove range data
    plots4.RemoveAt(range.dwPlotsStart, range.dwNumPlots);
    norms4.RemoveAt(range.dwNormsStart, range.dwNumNorms);
    maps.RemoveAt(range.dwMapsStart, range.dwNumMaps);
    components.RemoveAt(range.dwComponentsStart, range.dwNumComponents);
    lightcomponents.RemoveAt(range.dwLightComponentsStart, range.dwNumLightComponents);
    indices.RemoveAt(range.dwIndicesStart, range.dwNumIndices);
    // correct another ranges
    for (iter = ranges.begin(); iter != ranges.end(); ++iter)
    {
        // if range lies beyond the target range to remove, then subtract all 'start' entries of it
        if (iter->second.dwIndicesStart > range.dwIndicesStart)
        {
            iter->second.dwPlotsStart -= range.dwPlotsStart;
            iter->second.dwNormsStart -= range.dwNormsStart;
            iter->second.dwMapsStart -= range.dwMapsStart;
            iter->second.dwComponentsStart -= range.dwComponentsStart;
            iter->second.dwLightComponentsStart -= range.dwLightComponentsStart;
            iter->second.dwIndicesStart -= range.dwIndicesStart;
        }
    }
    //
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CGeometryContainer::GetRangeGeometryData(const std::string &szRangeName, const SXYZW4 *&plots,
                                              const SVertexComponents *&comps, const WORD *&inds,
                                              DWORD &dwNumPlots, DWORD &dwNumIndices) const
{
    auto iter = ranges.find(szRangeName);
    if (iter == ranges.end())
        return false; // no such range
    //
    plots = plots4.GetData() + iter->second.dwPlotsStart;
    comps = components.GetData() + iter->second.dwComponentsStart;
    inds = indices.GetData() + iter->second.dwIndicesStart;
    dwNumIndices = iter->second.dwNumIndices;
    dwNumPlots = iter->second.dwNumPlots;

    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CGeometryContainer::GetNativeFigureStreams(const std::string& name,
    owned::NativeFigureStreams& output) const
{
    const auto found = ranges.find(name);
    if (found == ranges.end()) return false;
    const auto& r = found->second;
    output = {reinterpret_cast<const owned::NativePackedGroup*>(plots4.GetData() + r.dwPlotsStart), r.dwNumPlots,
        reinterpret_cast<const owned::NativePackedGroup*>(norms4.GetData() + r.dwNormsStart), r.dwNumNorms,
        nullptr, nullptr,
        reinterpret_cast<const owned::NativeTextureCoordinate*>(maps.GetData() + r.dwMapsStart), r.dwNumMaps,
        reinterpret_cast<const owned::NativeVertexComponent*>(components.GetData() + r.dwComponentsStart), r.dwNumComponents,
        indices.GetData() + r.dwIndicesStart, r.dwNumIndices};
    return true;
}

bool CGeometryContainer::ChangeRangeGeometry(const std::string &szRangeName, SXYZW4 *rangePlots4,
                                             DWORD dwNumPlots)
{
    auto iter = ranges.find(szRangeName);
    if (iter == ranges.end())
        return false; // no such range
    //
    const SGeometryRangeSW &range = iter->second;
    memcpy(plots4.GetData() + range.dwPlotsStart, rangePlots4,
           Min(range.dwNumPlots, dwNumPlots) * sizeof(SXYZW4));
    ++ownedRevision;

    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CGeometryContainer::ProcessLighting(const std::string &szRangeName,
                                         CSimplyArray<CLight *, CLight *> &lights,
                                         CMaterial *pMaterial, BYTE cFog, CDWORDArray &colors,
                                         CDWORDArray &speculars)
{
    auto iter = ranges.find(szRangeName);
    if (iter == ranges.end())
        return false; // no such range
    const SGeometryRangeSW &range = iter->second;
    //
    colors.SetSize(range.dwNumNorms * 4);    // ARRAY BOUNDS CHECK
    speculars.SetSize(range.dwNumNorms * 4); // ARRAY BOUNDS CHECK
                                             // perform lighting
    if (pMaterial->CheckFlag(MAT_USE_SPECULAR))
    {
        DWORD dwSpecularColor = D3DRGBA(pMaterial->GetCompleteSpecular().r,
                                        pMaterial->GetCompleteSpecular().g,
                                        pMaterial->GetCompleteSpecular().b, 0) |
                                (cFog << 24);
        MemSetDWord(speculars.GetData(), dwSpecularColor, speculars.GetSize());
    }
    else
        MemSetDWord(speculars.GetData(), RGBA_MAKE(0, 0, 0, cFog), speculars.GetSize());
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CGeometryContainer::ProcessLighting(const std::string &szRangeName, DWORD dwColor,
                                         DWORD dwSpecular, CDWORDArray &colors,
                                         CDWORDArray &speculars)
{
    auto iter = ranges.find(szRangeName);
    if (iter == ranges.end())
        return false; // no such range
    const SGeometryRangeSW &range = iter->second;

    colors.SetSize(range.dwNumNorms * 4);    // ARRAY BOUNDS CHECK
    speculars.SetSize(range.dwNumNorms * 4); // ARRAY BOUNDS CHECK

    MemSetDWord(colors.GetData(), dwColor, colors.GetSize());
    MemSetDWord(speculars.GetData(), dwSpecular, speculars.GetSize());

    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
int CGeometryContainer::Render(const std::string &szRangeName, DWORD dwRenderFlags,
                               CDWORDArray &colors, CDWORDArray &speculars, CPlot3DArray &morphs,
                               float fUShift, float fVShift)
{
    auto iter = ranges.find(szRangeName);
    if (iter == ranges.end())
        return TRV_INVISIBLE; // no such range
    const SGeometryRangeSW &range = iter->second;
    SXYZW4 *plots = plots4.GetData() + range.dwPlotsStart;
    SXYZW4 *renderPlots = plots;
    CRenderer* sourceRenderer=pGeometryManager->GetD3D();
    if(dwRenderFlags & DXD3D_USE_SPECULAR)sourceRenderer->SetRenderState(D3DRENDERSTATE_SPECULARENABLE,TRUE);
    sourceRenderer->GetD3D()->GetOwnedRenderer().SelectFigureGeometry(range.ownedIdentity,ownedRevision);
    sourceRenderer->TryOwnedFigure(plots,range.dwNumPlots,
        norms4.GetData()+range.dwNormsStart,range.dwNumNorms,colors.GetData(),speculars.GetData(),
        maps.GetData()+range.dwMapsStart,range.dwNumMaps,components.GetData()+range.dwComponentsStart,
        range.dwNumComponents,indices.GetData()+range.dwIndicesStart,range.dwNumIndices,
        fUShift,fVShift,255,dwRenderFlags,morphs.GetData(),morphs.GetSize());
    if(dwRenderFlags & DXD3D_USE_SPECULAR)sourceRenderer->SetRenderState(D3DRENDERSTATE_SPECULARENABLE,FALSE);
    return TRV_VISIBLE_NOCLIP;
}
/////////////////////////////////////////////////////////////////////////////////////
void CGeometryContainer::RecalcScreenExtents()
{
    SetRect(&rcExtents, 2000000000, 2000000000, -2000000000, -2000000000);
    for (auto iter = extents.begin(); iter != extents.end(); ++iter)
    {
        if (!IsRectEmpty(&iter->second.rcScreen))
        {
            if (rcExtents.left > iter->second.rcScreen.left)
                rcExtents.left = iter->second.rcScreen.left;
            if (rcExtents.right < iter->second.rcScreen.right)
                rcExtents.right = iter->second.rcScreen.right;
            if (rcExtents.top > iter->second.rcScreen.top)
                rcExtents.top = iter->second.rcScreen.top;
            if (rcExtents.bottom < iter->second.rcScreen.bottom)
                rcExtents.bottom = iter->second.rcScreen.bottom;
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
DWORD CGeometryContainer::ProjectShadow(const std::string &szRangeName, const CPlot3DArray &morphs,
                                        const SHMatrix &matrix, CPlot2D *buffer,
                                        CTRect<float> *pRect)
{
    auto iter = ranges.find(szRangeName);
    if (iter == ranges.end())
        return 0; // no such range
    const SGeometryRangeSW &range = iter->second;
    //
    SXYZW4 *plots = plots4.GetData() + range.dwPlotsStart;
    SXYZW4 *renderPlots = plots;
    if (morphs.GetSize() != 0)
    {
        int hindex, lindex;

        plots41.SetSize(range.dwNumPlots); // ARRAY BOUNDS CHECK
        for (int i = 0; i < plots41.GetSize() * 4; ++i)
        {
            hindex = i >> 2;
            lindex = i & 0x3;
            // CRAP - wrong work with morphs - 'morphs.GetSize()' are not multiple to 4
            if (i >= morphs.GetSize())
            {
                plots41[hindex].x[lindex] = plots[hindex].x[lindex];
                plots41[hindex].y[lindex] = plots[hindex].y[lindex];
                plots41[hindex].z[lindex] = plots[hindex].z[lindex];
            }
            else
            {
                plots41[hindex].x[lindex] = plots[hindex].x[lindex] + morphs[i].x;
                plots41[hindex].y[lindex] = plots[hindex].y[lindex] + morphs[i].y;
                plots41[hindex].z[lindex] = plots[hindex].z[lindex] + morphs[i].z;
            }
        }
        renderPlots = plots41.GetData();
    }
    //
    pTL->TransformShadow(matrix, renderPlots, buffer, range.dwNumPlots, pRect);

    return range.dwNumPlots * 4;
}
/////////////////////////////////////////////////////////////////////////////////////
DWORD CGeometryContainer::RenderShadow(const std::string &szRangeName, CPoint *shadowpoints,
                                       SShadowTextureRenderParam &rp)
{
    auto iter = ranges.find(szRangeName);
    if (iter == ranges.end())
        return 0; // no such range
    const SGeometryRangeSW &range = iter->second;

    static CSimplyArray<CPoint, CPoint &> maps255;
    CGraphicsProcessor *pGrPU = pGeometryManager->GetD3D()->GetGraphicsProcessor();
    int i;

    maps255.SetSize(range.dwNumMaps); // ARRAY BOUNDS CHECK
    // convert maps to range [0...255]
    for (i = 0; i < range.dwNumMaps; ++i)
    {
        maps255[i].x = MINT(maps[range.dwMapsStart + i].x * 255);
        maps255[i].y = MINT(maps[range.dwMapsStart + i].y * 255);
    }
    // rasterize
    SVertexComponents *pComponents = components.GetData() + range.dwComponentsStart;
    WORD *pIndices = indices.GetData() + range.dwIndicesStart;
    for (i = 0; i < range.dwNumIndices; i += 3)
    {
        Copy8Bytes(&(rp.x1), &(shadowpoints[pComponents[pIndices[i + 0]].gv].x));
        Copy8Bytes(&(rp.tu1), &(maps255[pComponents[pIndices[i + 0]].tv].x));
        Copy8Bytes(&(rp.x2), &(shadowpoints[pComponents[pIndices[i + 1]].gv].x));
        Copy8Bytes(&(rp.tu2), &(maps255[pComponents[pIndices[i + 1]].tv].x));
        Copy8Bytes(&(rp.x3), &(shadowpoints[pComponents[pIndices[i + 2]].gv].x));
        Copy8Bytes(&(rp.tu3), &(maps255[pComponents[pIndices[i + 2]].tv].x));
        if (rp.lpSrcTexture) {
            ModShadow::TreeShadowRasterTriangle triangle = {
                {{rp.x1,rp.y1,rp.tu1,rp.tv1},{rp.x2,rp.y2,rp.tu2,rp.tv2},{rp.x3,rp.y3,rp.tu3,rp.tv3}},
                rp.lpSrcTexture,rp.lpDstTexture,LONG(rp.dwSrcPitch),LONG(rp.dwDstPitch)};
            ModShadow::RasterTreeShadowAlpha(&triangle);
        } else pGrPU->RasterizeTriangle(rp);
    }

    return range.dwNumPlots * 4;
}
/////////////////////////////////////////////////////////////////////////////////////
int CGeometryContainer::GetMemoryCount() const
{
    int nMemoryCount = sizeof(*this);
    nMemoryCount += plots4.GetSize() * sizeof(SXYZW4);
    nMemoryCount += norms4.GetSize() * sizeof(SXYZW4);
    nMemoryCount += maps.GetSize() * sizeof(CPlot2D);
    nMemoryCount += components.GetSize() * sizeof(SVertexComponents);
    nMemoryCount += lightcomponents.GetSize() * sizeof(SGvNvComponents);
    nMemoryCount += indices.GetSize() * sizeof(WORD);
    nMemoryCount += ranges.size() * sizeof(SGeometryRangeSW);
    return nMemoryCount;
}
/////////////////////////////////////////////////////////////////////////////////////
