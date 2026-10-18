#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "TerrainPrints.h"

#include "Renderer.h"
#include "WorldClient.h"
#include "TextureManager.h"
#include "Terrain.h"
#include "dx11/RendererDX11.h"
/////////////////////////////////////////////////////////////////////////////////////
int CTerrainPrints::Segment()
{
    fSizeX += fSizeInc;
    fSizeY += fSizeInc;
    --nLifeTimeCounter;
    fAlpha = fPower *
             (1.0f - Max(0.0f, float(nFadeoutTime - nLifeTimeCounter) / float(nFadeoutTime)));
    return nLifeTimeCounter;
}
/////////////////////////////////////////////////////////////////////////////////////
void CTerrainPrints::SetTexture(const char *pszName)
{
    pTexture = theWorld.GetTextureManager()->GetTexture(pszName);
    if (pTexture == 0)
    {
        pTexture = theWorld.GetTextureManager()->CreateTexture(TTID_SPELL_MAIN, pszName);
        theWorld.GetTextureManager()->LoadTexture(pTexture);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
int CTerrainPrints::Draw()
{
    if (!bVisible)
        return 1;

    if (!bProjected) // project...
    {
        CXYZW4Array srcRect;
        float fZ = theWorld.GetBridgeZValue(fX, fY);

        srcRect.SetSize(1);

        float fCos = cos(fGeomRotate);
        float fSin = sin(fGeomRotate);

        float x = +fSizeX;
        float y = -fSizeY;
        srcRect[0].x[0] = fX + x * fCos - y * fSin;
        srcRect[0].y[0] = fY + x * fSin + y * fCos;
        srcRect[0].z[0] = fZ;
        srcRect[0].w[0] = 1;

        x = -fSizeX;
        y = -fSizeY;
        srcRect[0].x[1] = fX + x * fCos - y * fSin;
        srcRect[0].y[1] = fY + x * fSin + y * fCos;
        srcRect[0].z[1] = fZ;
        srcRect[0].w[1] = 1;

        x = -fSizeX;
        y = +fSizeY;
        srcRect[0].x[2] = fX + x * fCos - y * fSin;
        srcRect[0].y[2] = fY + x * fSin + y * fCos;
        srcRect[0].z[2] = fZ;
        srcRect[0].w[2] = 1;

        x = +fSizeX;
        y = +fSizeY;
        srcRect[0].x[3] = fX + x * fCos - y * fSin;
        srcRect[0].y[3] = fY + x * fSin + y * fCos;
        srcRect[0].z[3] = fZ;
        srcRect[0].w[3] = 1;

        theWorld.CastShadowToTerrain(CPlot3D(0, 0, -1), CPlot3D(fX, fY, fZ + 1), 0, srcRect,
                                     vertices, maps, indices, true);
        fCos = cos(fMapRotate);
        fSin = sin(fMapRotate);
        for (int i = 0; i < vertices.GetSize() * 4; ++i)
        {
            x = (maps[i].x - 0.5f) * fCos - (maps[i].y - 0.5f) * fSin + 0.5f;
            y = (maps[i].x - 0.5f) * fSin + (maps[i].y - 0.5f) * fCos + 0.5f;
            maps[i].x = x * fsx + fdx;
            maps[i].y = y * fsy + fdy;
            i++;

            x = (maps[i].x - 0.5f) * fCos - (maps[i].y - 0.5f) * fSin + 0.5f;
            y = (maps[i].x - 0.5f) * fSin + (maps[i].y - 0.5f) * fCos + 0.5f;
            maps[i].x = x * fsx + fdx;
            maps[i].y = y * fsy + fdy;
            i++;

            x = (maps[i].x - 0.5f) * fCos - (maps[i].y - 0.5f) * fSin + 0.5f;
            y = (maps[i].x - 0.5f) * fSin + (maps[i].y - 0.5f) * fCos + 0.5f;
            maps[i].x = x * fsx + fdx;
            maps[i].y = y * fsy + fdy;
            i++;

            x = (maps[i].x - 0.5f) * fCos - (maps[i].y - 0.5f) * fSin + 0.5f;
            y = (maps[i].x - 0.5f) * fSin + (maps[i].y - 0.5f) * fCos + 0.5f;
            maps[i].x = x * fsx + fdx;
            maps[i].y = y * fsy + fdy;
        }
        //
        for (int i = 0; i < vertices.GetSize(); ++i)
        {
            vertices[i].z[0] += 0.01f;
            vertices[i].z[1] += 0.01f;
            vertices[i].z[2] += 0.01f;
            vertices[i].z[3] += 0.01f;
        }
        //
        bProjected = true;
    }
    //
    dwColor = (dwColor & 0x00ffffff) | (DWORD(MINT(fAlpha * 255)) << 24);
    if (!theRenderer.BindTexture(0, pTexture)) return TRV_VISIBLE_NOCLIP;

    owned::EmbeddedPrintInput input;
    std::vector<std::array<float, 16>> groups(vertices.GetSize());
    std::vector<std::array<float, 2>> uv(maps.GetSize());
    for (int i = 0; i < vertices.GetSize(); ++i)
        std::memcpy(groups[i].data(), &vertices[i], sizeof(SXYZW4));
    for (int i = 0; i < maps.GetSize(); ++i) uv[i] = {maps[i].x, maps[i].y};
    input.groups = groups.data(); input.group_count = static_cast<std::uint32_t>(groups.size());
    input.maps = uv.data(); input.map_count = static_cast<std::uint32_t>(uv.size());
    input.indices = indices.GetData(); input.index_count = indices.GetSize(); input.color = dwColor;
    const auto& projection = theRenderer.GetGPU()->GetMatrix(MAT_STACK_PROJSPACE);
    std::memcpy(input.projection.object_to_clip.data(), &projection, sizeof(input.projection.object_to_clip));
    const RECT& clip = theRenderer.GetWindowRect();
    input.projection.clip_half_width = float(clip.right - clip.left) * .5f;
    input.projection.clip_half_height = float(clip.bottom - clip.top) * .5f;
    // c74 applies terrain world fog; the cached centre fog byte is not a vertex fog input.
    theRenderer.GetD3D()->SubmitC74Print(input);
    return TRV_VISIBLE_NOCLIP;
}
/////////////////////////////////////////////////////////////////////////////////////
void CTerrainPrints::SetGeometryPlacement(float _fX, float _fY, float _fSizeX, float _fSizeY)
{
    fX = _fX;
    fY = _fY;
    fSizeX = _fSizeX;
    fSizeY = _fSizeY;
    cFogValue = theWorld.GetTerrain()->GetFogAlpha(CPlot3D(fX, fY, 0));
}
/////////////////////////////////////////////////////////////////////////////////////
void CTerrainPrints::Store(CSave &file)
{
    // geometry-relative data
    file.Write(fX);
    file.Write(fY);
    file.Write(fSizeX);
    file.Write(fSizeY);
    file.Write(fSizeInc);
    // life control data
    file.Write(nLifeTimeCounter);
    // texture-relative data
    const auto szName = pTexture->GetName();
    file.WriteString(szName);
    file.Write(fdx);
    file.Write(fdy);
    file.Write(fsx);
    file.Write(fsy);
    file.Write(fMapRotate);
    file.Write(fGeomRotate);
    // color relative data
    file.Write(cFogValue);
    file.Write(fPower);
    file.Write(nFadeoutTime);
    file.Write(dwColor);
}
/////////////////////////////////////////////////////////////////////////////////////
void CTerrainPrints::Load(CSave &file)
{
    // geometry-relative data
    file.Read(fX);
    file.Read(fY);
    file.Read(fSizeX);
    file.Read(fSizeY);
    file.Read(fSizeInc);
    // life control data
    file.Read(nLifeTimeCounter);
    // texture-relative data
    char buff[256];
    file.ReadString(buff, 256);
    SetTexture(buff);
    file.Read(fdx);
    file.Read(fdy);
    file.Read(fsx);
    file.Read(fsy);
    file.Read(fMapRotate);
    file.Read(fGeomRotate);
    // color relative data
    file.Read(cFogValue);
    file.Read(fPower);
    file.Read(nFadeoutTime);
    file.Read(dwColor);
    //
    bProjected = false;
}
/////////////////////////////////////////////////////////////////////////////////////
CBloodPrints::CBloodPrints(float fX, float fY, float fSize, float fFinalSize, float fPower,
                           int nLifetime, int nFadeoutTime, DWORD dwType, DWORD dwColor,
                           bool _bSmall)
    : CTerrainPrints(dwColor, fPower, nLifetime, nFadeoutTime, true)
{
    SetGeometryPlacement(fX, fY, fSize, fSize);
    DWORD dwBloodType = (dwType & 0xFFFF0000) >> 16;
    DWORD dwBloodSubtype = dwType & 0x0000FFFF;
    // setup texture placement
    float dx = 0.5f * (dwBloodType & 0x1) + 0.25f * (dwBloodSubtype & 0x1);
    float dy = 0.5f * (dwBloodType >> 1) + 0.25f * (dwBloodSubtype >> 1);
    float sx = 0.25f;
    float sy = 0.25f;
    SetTexturePlacement(dx, dy, sx, sy);
    // mapping and geometry start rotation angle
    SetMapRotate(float(Random(4)) * FP_PI2);
    SetGeomRotate(Random(MINT(FP_2PI * 1000.0f)) * 0.001f);
    // texture...
    SetTexture("bloodprints");
    //
    nIncrementingTime = Min(3 * 15, nLifetime / 2);
    // geometry per-segment rotation step
    fGeomRotateStep = ToRadian(30.0f / float(nLifetime));
    // geometry per-segment size increment
    SetupSizeIncrement(fFinalSize, nIncrementingTime);
    //
    nBloodType = dwBloodType;
    bSmall = _bSmall;
}
/////////////////////////////////////////////////////////////////////////////////////
int CBloodPrints::Segment()
{
    if (--nIncrementingTime < 0)
        SetSizeInc(0);
    else // set projected to false because we've changed size of this prints
        SetProjected(false);
    //
    SetGeomRotate(GetGeomRotate() + fGeomRotateStep);
    //
    return CTerrainPrints::Segment();
}
/////////////////////////////////////////////////////////////////////////////////////
void CBloodPrints::Store(CSave &file)
{
    CTerrainPrints::Store(file);
    file.Write(fGeomRotateStep);
    file.Write(nIncrementingTime);
    file.Write(nBloodType);
    file.Write(bSmall);
}
/////////////////////////////////////////////////////////////////////////////////////
void CBloodPrints::Load(CSave &file)
{
    CTerrainPrints::Load(file);
    file.Read(fGeomRotateStep);
    file.Read(nIncrementingTime);
    file.Read(nBloodType);
    file.Read(bSmall);
}
/////////////////////////////////////////////////////////////////////////////////////
CFootPrints::CFootPrints(float fX, float fY, float fSizeX, float fSizeY, float fRotate,
                         float fPower, int nLifetime, int nFadeoutTime, DWORD dwType,
                         float _fDetection, bool bVisible, int _nBloodType)
    : CTerrainPrints(0xFFFFFFFF, fPower, nLifetime, nFadeoutTime, bVisible)
    , fDetection(_fDetection)
    , nBloodType(_nBloodType)
{
    SetGeometryPlacement(fX, fY, fSizeX, fSizeY);
    // CRAP для тестов пока (0, 0, 1, 1)
    // 8x8 of step pairs (8x16 pixels each footprint)
    DWORD dwFootType = dwType & 0xFFFF;
    DWORD dwFootSubtype = (dwType & 0xFFFF0000) >> 16;
    float dx = 0.125f * float(dwFootType & 7) + 0.0625f * float(dwFootSubtype);
    float dy = 0.125f * float(dwFootType >> 3);
    float sx = 0.0625f;
    float sy = 0.125f;
    SetTexturePlacement(dx, dy, sx, sy);
    //	SetTexturePlacement( 0, 0, 1, 1 );
    // CRAP ends
    SetMapRotate(0);
    SetGeomRotate(fRotate);
    //
    if (nBloodType == -1)
        SetTexture("footprints");
    else
    {
        char buffer[64];
        sprintf(buffer, "footprints%d", nBloodType + 1);
        SetTexture(buffer);
    }
    //
    SetSizeInc(0);
}
/////////////////////////////////////////////////////////////////////////////////////
void CFootPrints::Store(CSave &file)
{
    CTerrainPrints::Store(file);
    file.Write(fDetection);
    file.Write(nBloodType);
}
/////////////////////////////////////////////////////////////////////////////////////
void CFootPrints::Load(CSave &file)
{
    CTerrainPrints::Load(file);
    file.Read(fDetection);
    file.Read(nBloodType);
}
/////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////
CFirePrints::CFirePrints(float fX, float fY, float fSizeX, float fSizeY, float fRotate,
                         float fPower, int nLifetime, int nFadeoutTime)
    : CTerrainPrints(0xFFFFFFFF, fPower, nLifetime, nFadeoutTime, true)
{
    SetGeometryPlacement(fX, fY, fSizeX, fSizeY);
    DWORD dwType = Random(4);
    // setup texture placement
    float dx = 0.5f * (dwType & 0x1);
    float dy = 0.5f * (dwType >> 1);
    float sx = 0.5f;
    float sy = 0.5f;
    SetTexturePlacement(dx, dy, sx, sy);
    // mapping and geometry start rotation angle
    SetMapRotate(float(dwType) * FP_PI2);
    SetGeomRotate(fRotate);
    // texture...
    SetTexture("fireprints");
    // geometry per-segment size increment
    SetSizeInc(0);
}
/////////////////////////////////////////////////////////////////////////////////////
