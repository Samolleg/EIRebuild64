/**********************************************************************
 *<
        FILE: Tarrain.H

        DESCRIPTION:  Tarrain manager implementation

        CREATED BY: Ivanov Evgeny

 *>	Copyright (c) 1998, Nival Enterntainemnt
 **********************************************************************/
#include "StdAfx.h"

#include "dx11/Batchers.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "Sector.h"
#include "Terrain.h"
#include "ModTerrainShadow.h"
#include "mapFormat.h"

#include "WorldClient.h"
#include "Camera.h"
#include "MaterialManager.h"
#include "TextureManager.h"
#include "LightManager.h"
#include "RandomGen.h"
#include "Shadow.h"

#include "SoundManager.h"
#include "dx11/RendererDX11.h"
#include "dx11/OwnedTerrainInputs.inl"
/////////////////////////////////////////////////////////////////////////////////////

// Sector manager constants
const int N_MIN_KEEP_TIME = 15 * 16; // Minimal number of segments to keep sector loaded

// Wind info constants
const float FP_MAX_WAVE_Z = 0.3f * FP_CL_MP_SIZE;
const float FP_WAVE_LEN_MUL = FP_PI / 7.f;
const float FP_WAVE_PHASE_MUL = -FP_WAVE_LEN_MUL / 3.f;

// LOD constants
const float LOD_1_DISTANCE =
    40.f; // The difference between this values cannot be lower that N_CL_PSECTOR_S( 32 )
const float LOD_2_DISTANCE = 75.f;

// Radius of sphere to trace ray to sun
const float FP_SUNTRACE_LEN = float(3 * N_CL_PSECTOR_S - 1) * FP_CL_MP_SIZE;

/////////////////////////////////////////////////////////////////////////////////////
SUVInfo *CTerrain::GetUVInfo(int nRot, int nTileInd, int nMat)
{
    int nOffset = nMat % (nTextures + N_NUMBER_OF_LONG_TEXTURES);
    //	if tile is long
    if (nOffset >= nTextures && nOffset < nTextures + N_NUMBER_OF_LONG_TEXTURES)
        return &uvLongCoords[nRot][nTileInd];
    else
        return &uvCoords[nRot][nTileInd];
}

/////////////////////////////////////////////////////////////////////////////////////
BOOL CTerrain::Create()
{
    Clear();
    CreateObjectFindInfo();
    CreateWindNoise();
    CreateSineTable();

    SetCP(VNULL);
    SetOrientation(QNULL);

    return TRUE;
}

/////////////////////////////////////////////////////////////////////////////////////
void CTerrain::Destroy()
{
    Free();
    // Clear lights hash table
    for (auto pos = lightInfo.begin(); pos != lightInfo.end(); ++pos)
        pos->second.pLight->Release();
    lightInfo.ClearHashTable();
    sectorLRU.RemoveAll();
    Clear();
}
/////////////////////////////////////////////////////////////////////////////////////
void CTerrain::Clear()
{
    nLongTile = 0;
    renderState = ETRS_NO;
    textures.SetSize(0);
    materials.SetSize(0);
    memset(&d3dmatrix, 0, sizeof(d3dmatrix));
    bIsLoaded = FALSE;
    szBaseName = "none";
    nXSectors = -1;
    nYSectors = -1;
    nTexSize = -1;
    nTileSize = -1;
    nTilesPerTex = -1;
    nSegmentCount = 1;
    nSegmentsToRelight = -1;
    nCurSegmentsToRelight = -1;
    memset(uvCoords, 0, sizeof(uvCoords));
    memset(uvLongCoords, 0, sizeof(uvLongCoords));
    sectorPos = NULL;
    // Set default lighting variables
    dcvAmbient.r = 0;
    dcvAmbient.g = 0;
    dcvAmbient.b = 0;
    fpCA = 0;
    fpCB = 0;
    fpAC = 0;
    fpBC = 0;
    nAddCellsX = -1;
    nAddCellsY = -1;
    pSunLight = 0;
    // Set default wind
    CPlot3D windDirection = CPlot3D(0.0f, 0.0f, 1.f);
    SetWind(&windDirection, 0.f);
    fpWindPhase = 2.f * FP_PI;
    // Set default LOD
    nMaxLOD = 0;
    // Default sectors clipping
    nClipMinSectorY = 0;
    viewableSectorsMinX.SetSize(0);
    viewableSectorsMaxX.SetSize(0);
    // Ambient sounds
    for (int i = 0; i < N_LASRC_NUM; i++)
    {
        hSndAmbient[i] = SND_EMPTY;
        nAmbientTerrainType[i] = -1;
    }
    szAllodDependentSndDir = "none";
}

/////////////////////////////////////////////////////////////////////////////////////
void CTerrain::CreateWindNoise()
{
    int i, j;
    fpNoises.SetSize(N_V_PSECTOR);
    float *pNoise = fpNoises.GetData();
    for (i = 0; i < N_V_PSECTOR; i++)
        *pNoise++ = 0.0f;

    for (int nNum = 0; nNum < 40; nNum++)
    {
        int nCircleX = rand() % (N_CL_PSECTOR_S + 1);
        int nCircleY = rand() % (N_CL_PSECTOR_S + 1);
        float fpCircleX = float(nCircleX);
        float fpCircleY = float(nCircleY);
        float fpRadius = float(rand() % 5 + 1);
        float fpRadius2 = fpRadius * fpRadius;

        pNoise = fpNoises.GetData();
        float fpY = 0.0f;
        for (j = 0; j < N_CL_PSECTOR_S + 1; j++, fpY += 1.f)
        {
            float fpX = 0.0f;
            for (i = 0; i < N_CL_PSECTOR_S + 1; i++, fpX += 1.f)
            {
                float fpDx = fpX - fpCircleX, fpDy = fpY - fpCircleY;

                float fpDist2 = fpDx * fpDx + fpDy * fpDy;
                if (fpDist2 < fpRadius2)
                {
                    float fpDist = float(sqrt(fpDist2));
                    //*pNoise += ( 1.f - fpDist / fpRadius ) * FP_PI/2.f;
                    *pNoise += (1.f - fpDist2 / fpRadius2) * FP_PI / 2.f;
                    //*pNoise += FP_PI/2.f;
                }
                pNoise++;
            }
        }
    }
    // Duplicate borders, up-down
    float *pNoise1, *pNoise2, *pNoise3, *pNoise4;
    pNoise1 = fpNoises.GetData() + 1;
    pNoise2 = pNoise1 + N_V_PSECTOR - (N_CL_PSECTOR_S + 1);
    for (i = 0; i < N_CL_PSECTOR_S + 1 - 2; i++)
    {
        float fpMidNoise = (*pNoise1 + *pNoise2) / 2.f;
        *pNoise1 = fpMidNoise;
        *pNoise2 = fpMidNoise;
        pNoise1++;
        pNoise2++;
    }
    // Duplicate borders, left-right
    pNoise1 = fpNoises.GetData() + (N_CL_PSECTOR_S + 1);
    pNoise2 = pNoise1 + (N_CL_PSECTOR_S + 1) - 1;
    for (i = 0; i < N_CL_PSECTOR_S + 1 - 2; i++)
    {
        float fpMidNoise = (*pNoise1 + *pNoise2) / 2.f;
        *pNoise1 = fpMidNoise;
        *pNoise2 = fpMidNoise;
        pNoise1 += (N_CL_PSECTOR_S + 1);
        pNoise2 += (N_CL_PSECTOR_S + 1);
    }
    // Duplicate borders, corners
    pNoise1 = fpNoises.GetData();
    pNoise2 = pNoise1 + (N_CL_PSECTOR_S + 1) - 1;
    pNoise3 = fpNoises.GetData() + N_V_PSECTOR - 1;
    pNoise4 = pNoise3 - (N_CL_PSECTOR_S + 1) + 1;
    float fpMidNoise = (*pNoise1 + *pNoise2 + *pNoise3 + *pNoise4) / 4.f;
    *pNoise1 = fpMidNoise;
    *pNoise2 = fpMidNoise;
    *pNoise3 = fpMidNoise;
    *pNoise4 = fpMidNoise;
}

/////////////////////////////////////////////////////////////////////////////////////
void CTerrain::CreateSineTable()
{
    float *pSine = sineTable;
    for (int i = 0; i < 512; i++)
        *pSine++ = float(sin(i / 512.0 * 2 * pi));
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CTerrain::CreateTextures(int nTexCount)
{
    textures.SetSize(nTexCount);
    materials.SetSize(nTexCount);

    for (int i = 0; i < nTexCount + N_NUMBER_OF_LONG_TEXTURES; i++)
    {
        CString szTexName;
        if (i < nTexCount)
            szTexName.Format("%s%3.3i", szBaseName.GetString(), i);
        else
        {
            szTexName.Format("%sL%2.2i", szBaseName.GetString(), i - nTexCount);
            if (((CTextureManager *)theWorld.GetTextureManager())
                    ->CheckFileExist(szTexName + ".mmp"))
            {
                nLongTile++;
                textures.Add(NULL);
                materials.Add(NULL);
            }
            else
                break;
        }
        textures[i] = ((CTextureManager *)theWorld.GetTextureManager())
                          ->CreateTexture(TTID_TERRAIN, szTexName);
        textures[i]->AddRef();
        materials[i] = ((CMaterialManager *)theWorld.GetMaterialManager())
                           ->CreateMaterial(MTID_WOOD);
        materials[i]->AddRef();
        materials[i]->SetMaterialName("Wood");
        materials[i]->SetDiffuse(D3DVECTOR(0.3f));
        materials[i]->SetAmbient(D3DVECTOR(0.7f));
        materials[i]->SetSpecular(D3DVECTOR(0.2f));
        materials[i]->SetEmissive(D3DVECTOR(0.0f));
        materials[i]->SetAlpha(1);
        materials[i]->SetPower(5);
        if (!((CTextureManager *)theWorld.GetTextureManager())->LoadTexture(textures[i]))
            return FALSE;
    }
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CTerrain::ReleaseTextures()
{
    for (int i = 0; i < textures.GetSize(); i++)
        textures[i]->Release();
    textures.SetSize(0);
    for (int i = 0; i < materials.GetSize(); i++)
        materials[i]->Release();
    materials.SetSize(0);
}
/////////////////////////////////////////////////////////////////////////////////////
void CTerrain::PrepareD3DRotateMatrix(const CQuaternion &q)
{
    Identity(*reinterpret_cast<SHMatrix *>(&d3dmatrix));
    q.DecompEulerMatrix(d3dmatrix);
    d3dmatrix._41 = respectCP.GetX();
    d3dmatrix._42 = respectCP.GetY();
    d3dmatrix._43 = respectCP.GetZ();
}
/////////////////////////////////////////////////////////////////////////////////////
void CTerrain::PrepareForDraw(const CQuaternion &q)
{
    rotation1 = q * rotation;
    memset(&d3dmatrix, 0, sizeof(d3dmatrix));
    PrepareD3DRotateMatrix(rotation1);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CTerrain::DrawBase(CRenderer *pRend)
{
    CreateCurrentPlacement();
    pRend->GetGPU()->PushMainMatrix(GetPlacement());
    BOOL bRet = Draw(pRend, 0, nTextures + N_NUMBER_OF_LONG_TEXTURES - 1);
    pRend->GetGPU()->PopMainMatrix();

    return bRet;
}
BOOL CTerrain::DrawTransparent(CRenderer *pRend)
{
    CreateCurrentPlacement();
    pRend->GetGPU()->PushMainMatrix(GetPlacement());
    BOOL bRet = Draw(pRend, nTextures + N_NUMBER_OF_LONG_TEXTURES,
                     (nTextures + N_NUMBER_OF_LONG_TEXTURES) * 2 - 1);
    pRend->GetGPU()->PopMainMatrix();

    return bRet;
}

BOOL CTerrain::Draw(CRenderer *pRend)
{
    return Draw(pRend, 0, (nTextures + N_NUMBER_OF_LONG_TEXTURES) * 2 - 1);
}

BOOL CTerrain::Draw(CRenderer *pRend, int nMatFrom, int nMapTo)
{
    if (!bIsLoaded)
        return FALSE;

    int xs, ys;
    pRend->SetProjectionMatrixOne();
    // Set LOD and clip flags for each visible sector
    for (ys = 0; ys < viewableSectorsMinX.GetSize(); ys++)
    {
        for (xs = viewableSectorsMinX[ys]; xs <= viewableSectorsMaxX[ys]; xs++)
        {
            CSector *pSector = GetSector(xs, ys + nClipMinSectorY, CSector::ESS_LIGHTED);
            if (!pSector)
                continue;
            SetLODForSector(pSector);
            pSector->SetClipFlag(pRend);
        }
    }
    if(nMatFrom==0 && nMapTo==nTextures+N_NUMBER_OF_LONG_TEXTURES-1 && TryOwnedTerrain(pRend))
        return TRUE;
    // Transform sectors
    for (ys = 0; ys < viewableSectorsMinX.GetSize(); ys++)
    {
        for (xs = viewableSectorsMinX[ys]; xs <= viewableSectorsMaxX[ys]; xs++)
        {
            CSector *pSector = GetSector(xs, ys + nClipMinSectorY, CSector::ESS_LIGHTED);
            if (pSector == NULL || pSector->IsVisible() == FALSE)
                continue;
            pSector->SetBorderLODsFlags();
            pSector->Transform(pRend->GetGPU());
        }
    }
    CBatchers *batchers = pRend->GetD3D()->GetBatchers();
    theRenderer.SetRenderState(D3DRENDERSTATE_SPECULARENABLE, TRUE);
    // Draw sectors by materials
    for (int i = nMatFrom; i <= nMapTo; i++)
    {
        BOOL bMatSetted = FALSE;
        batchers->GetBatcher()->Begin();
        for (ys = 0; ys < viewableSectorsMinX.GetSize(); ys++)
        {
            for (xs = viewableSectorsMinX[ys]; xs <= viewableSectorsMaxX[ys]; xs++)
            {
                CSector *pSector = GetSector(xs, ys + nClipMinSectorY, CSector::ESS_LIGHTED);
                if (!pSector)
                    continue;
                if (pSector->IsVisible() && pSector->IsHaveMaterial(i))
                {
                    if (!bMatSetted)
                    {
                        pRend->BindTexture(0,
                                           textures[i % (nTextures + N_NUMBER_OF_LONG_TEXTURES)]);
                        bMatSetted = TRUE;
                    }
                    if (!pSector->Draw(pRend, this, i, NULL /*&d3dmatrix*/))
                        return FALSE;
                }
            }
        }
        batchers->GetBatcher()->End();
        {
            STLVertex1 *pVertices;
            DWORD dwVertexCount;
            LPWORD pIndices;
            DWORD dwIndexCount;
            batchers->GetBatcher()->GetVertices(pVertices, dwVertexCount, pIndices, dwIndexCount);
            pRend->RenderTriangleSet(pVertices, dwVertexCount, pIndices, dwIndexCount);
        }
    }
    theRenderer.SetRenderState(D3DRENDERSTATE_SPECULARENABLE, FALSE);

    return TRUE;
}

/////////////////////////////////////////////////////////////////////////////////////
void CTerrain::SetMaxLOD(int nLOD)
{
    nMaxLOD = nLOD;
    if (nMaxLOD < 0)
        nMaxLOD = 0;
    if (nMaxLOD > 2)
        nMaxLOD = 2;
}

void CTerrain::SetLODForSector(CSector *pSector)
{
    // Calculate distance to sector
    CPlot3D cpResult, cpSector;

    cpSector = CPlot3D((float(pSector->GetMapX()) + 0.5f) * float(N_CL_PSECTOR_S) * FP_CL_MP_SIZE,
                       (float(pSector->GetMapY()) + 0.5f) * float(N_CL_PSECTOR_S) * FP_CL_MP_SIZE,
                       pSector->GetAverageZ());
    cpResult = GetCP() + cpSector - theCamera.GetCP();

    float fpDist2 = cpResult * cpResult;

    // Set appropriate LOD
    if ((fpDist2 < theSettings.fpTerrainLODs[0] * theSettings.fpTerrainLODs[0]) && (nMaxLOD <= 0))
        pSector->SetLOD(0);
    else if ((fpDist2 < theSettings.fpTerrainLODs[1] * theSettings.fpTerrainLODs[1]) &&
             (nMaxLOD <= 1))
        pSector->SetLOD(1);
    else
        pSector->SetLOD(2);
}

/////////////////////////////////////////////////////////////////////////////////////

inline void CTerrainObject::Set(SMapObject &mapObj)
{
    eType = mapObj.eType;
    fpR = mapObj.fpR;
    fpG = mapObj.fpG;
    fpB = mapObj.fpB;
    fpOpacity = mapObj.fpOpacity;
    fpTransparencyInv = 1.f / (1.f - fpOpacity);
    cAlpha = BYTE(fpOpacity * float(0xFF));

    dcvEmissive.r = Min(fpR * mapObj.fpSelfIllum, 1.0f);
    dcvEmissive.g = Min(fpG * mapObj.fpSelfIllum, 1.0f);
    dcvEmissive.b = Min(fpB * mapObj.fpSelfIllum, 1.0f);

    fpWindMultiplier = mapObj.fpWaveMultiplier;
    fpWarpSpeed = mapObj.fpWarpSpeed;
    fGlobalShift = 0; // must save/load on 'Save/Load game'
}

/////////////////////////////////////////////////////////////////////////////////////

void CTerrain::Save()
{
    int x, y;
    for (x = 0; x < nXSectors; x++)
        for (y = 0; y < nYSectors; y++)
        {
            CSector *pSector = GetSector(x, y, CSector::ESS_GEOMETRY, TRUE);
            pSector = GetSector(x, y, CSector::ESS_D3D, TRUE);
            if (pSector && pSector->IsModifid())
            {
                char szSectorName[256];
                GetSectorName(x, y, szBaseName.GetBuffer(0), szSectorName);
                LPBYTE pSectorData = mapRes.GetFile(szSectorName);
                pSector->Save(pSectorData);
            }
        }
    mapRes.Flush();
}

BOOL CTerrain::Load(LPCSTR pszFileName, LPCSTR pszAllodName, bool bCave)
{
    Free();
    CString szResourceName = pszFileName;

    // Opening resource
    if (!mapRes.OpenResource(szResourceName))
        return FALSE;

    CStringArray aResNames;
    mapRes.GetFileList(aResNames, "*.mp");
    ASSERT(aResNames.GetSize() == 1);

    // New method
    int nInd = szResourceName.ReverseFind('.');
    szBaseNameFull = nInd > 0 ? szResourceName.Left(nInd) : szResourceName;
    szBaseName = aResNames[0].Left(aResNames[0].Find('.'));

    /*	Old method
            int nInd = szResourceName.ReverseFind( '.' );
            szBaseNameFull = nInd > 0 ? szResourceName.Left( nInd ) : szResourceName;
            nInd = szBaseNameFull.ReverseFind( '\\' );
            szBaseName = nInd > 0 ? szBaseNameFull.Right( szBaseNameFull.GetLength() - nInd - 1 ) :
       szBaseNameFull;
    */
    // Reading map header
    SMapHeader mapHeader;
    DWORD dwHdSize;
    LPBYTE pMapHeader = mapRes.GetFile(szBaseName + PSZ_MAPFILE_EXT, &dwHdSize);
    if (!pMapHeader)
    {
        Free();
        return ReportError("Unable to open file \"" + szResourceName + "\"");
    }
    if (dwHdSize < sizeof(mapHeader))
    {
        Free();
        return ReportError("Wrong file format: " + szResourceName);
    }
    memcpy(&mapHeader, pMapHeader, sizeof(mapHeader));
    if (mapHeader.dwSignature != DW_SMAPHEADER_SIGNATURE ||
        dwHdSize != DWORD(sizeof(mapHeader) + mapHeader.wObjectsCount * sizeof(SMapObject)) +
                        mapHeader.dwTiles * sizeof(int) +
                        mapHeader.dwAnimatedTiles * sizeof(STileAnimInfo))
    {
        Free();
        return ReportError("Wrong file format: " + szResourceName);
    }

    SetMapSize(int(mapHeader.dwXSectorsCount), int(mapHeader.dwYSectorsCount));
    fpMaxX = float(nXSectors * N_CL_PSECTOR_S * FP_CL_MP_SIZE); // мировые координаты начала сектора
    fpMaxY = float(nYSectors * N_CL_PSECTOR_S * FP_CL_MP_SIZE);
    fpMaxZ = mapHeader.fpMaxZ;

    nTexSize = int(mapHeader.dwTexSize);   // размер текстуры ( сейчас 512 )
    nTileSize = int(mapHeader.dwTileSize); // ( сейчас 64 )
    nTextures = int(mapHeader.dwTextures); // количество текстур ( которые 512x512 )
    nTilesPerTexSide = nTexSize / nTileSize; // сколько тайлов по стороне одной текстуры
    nTilesPerTex = nTilesPerTexSide * nTilesPerTexSide; // сколько всего тайлов в одной текстуре

    // Reading objects
    SMapObject *pObj = (SMapObject *)(pMapHeader + sizeof(mapHeader));
    objects.SetSize(mapHeader.wObjectsCount);
    for (int nObj = 0; nObj < int(mapHeader.wObjectsCount); nObj++)
    {
        objects[nObj].Set(*pObj);
        pObj++;
    }
    // Reading tiles terrain IDs
    BYTE *pTileIDs = (BYTE *)pObj;
    tileIDs.SetSize(int(mapHeader.dwTiles) + N_NUMBER_OF_LONG_TILES);
    memcpy(tileIDs.GetData(), pTileIDs, int(mapHeader.dwTiles) * sizeof(int));
    for (int i = 0; i < N_NUMBER_OF_LONG_TILES; i++)
        tileIDs[mapHeader.dwTiles + i] = TERRTYPE_HIGHROCK;

    // Reading tiles animation info
    tileAnimationsPhases.SetSize(mapHeader.dwTiles);
    memset(tileAnimationsPhases.GetData(), 0, mapHeader.dwTiles * sizeof(int));
    STileAnimInfo *pAnimInfoData = (STileAnimInfo *)((BYTE *)pObj +
                                                     int(mapHeader.dwTiles) * sizeof(int));
    for (int i = 0; i < int(mapHeader.dwAnimatedTiles); i++)
    {
        tileAnimationsPhases[pAnimInfoData->wAnimatedTileIndex] = int(
            pAnimInfoData->wAnimationPhases);
        pAnimInfoData++;
    }

    // Create textures
    if (!CreateTextures(nTextures))
    {
        Free();
        return FALSE;
    }

    // Create uvCoords
    float fpUVTileSize = 1.f / float(nTilesPerTexSide);
    float fpUVMargin = (fpUVTileSize / float(nTileSize)) * 8.0f;

    // Создаём координаты маппинга для "обычных" тайлов
    for (int nRot = 0; nRot < 4; nRot++)
    {
        uvCoords[nRot] = dbgnew SUVInfo[nTilesPerTex];
        int nTile = 0;
        for (int j = 0; j < nTilesPerTexSide; j++)
        {
            for (int i = 0; i < nTilesPerTexSide; i++)
                CreateUVInfo(uvCoords[nRot][nTile++], nRot, i, j, fpUVTileSize, fpUVMargin);
        }
    }

    // Создаём координаты маппинга для "длинных" тайлов
    for (int nRot = 0; nRot < 4; nRot++)
    {
        uvLongCoords[nRot] = dbgnew SUVInfo[nTilesPerTex];
        int nTile = 0;
        for (int j = 0; j < nTilesPerTexSide; j++)
            CreateUVInfo(uvLongCoords[nRot][nTile++], nRot, j, 0, fpUVTileSize, fpUVMargin, true);
    }

    assert(nTexSize % nTileSize == 0);

    bIsLoaded = TRUE;
    if(!bCave) {
        const int width=nXSectors*N_CL_PSECTOR_S+1,height=nYSectors*N_CL_PSECTOR_S+1;
        if(static_cast<size_t>(width)*height<=2000000) {
            std::vector<float> heights(static_cast<size_t>(width)*height);
            for(int sy=0;sy<nYSectors;sy++)for(int sx=0;sx<nXSectors;sx++) {
                SSectVert2 *vertices=GetSectorGeometryData(sx,sy);
                for(int y=0;y<N_V_PSECTOR_S;y++)for(int x=0;x<N_V_PSECTOR_S;x++)
                    heights[(sy*N_CL_PSECTOR_S+y)*width+sx*N_CL_PSECTOR_S+x]=GetFloatValueByRange(vertices[y*N_V_PSECTOR_S+x].usZ,GetMaxZ());
            }
            modTerrainShadow=std::make_shared<ModTerrainShadow>();
            modTerrainShadow->Initialize(width,height,std::move(heights));
        }
    }


    // prepare ambient sounds table
    if (bCave)
        szAllodDependentSndDir = "Dungeon";
    else if (stricmp(pszAllodName, "Ingos") == 0)
        szAllodDependentSndDir = "Ingos";
    else if (stricmp(pszAllodName, "Suslanger") == 0)
        szAllodDependentSndDir = "Suslanger";
    else
        szAllodDependentSndDir = "Gipat";
    PrepareAmbientSounds();

    // Render state:
    SetRenderState(CTerrain::ETRS_FULL);

    // Prepare layer shifting info
    m_aSectorShifts.SetSize(nXSectors * nYSectors);
    for (int i = 0; i < nXSectors * nYSectors; i++)
    {
        m_aSectorShifts[i].aObjShift.SetSize(objects.GetSize());
        memset(m_aSectorShifts[i].aObjShift.GetData(), 0,
               sizeof(float) * m_aSectorShifts[i].aObjShift.GetSize());
    }

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CTerrain::Free()
{
    ChangeRendererRevision();
    ReleaseOwnedTerrain(&theRenderer);
    rendererSectorRows.clear();rendererSectorPhases.clear();
    modTerrainShadow.reset();
    for (int i = 0; i < N_LASRC_NUM; i++)
    {
        hSndAmbient[i] = theWorld.GetSoundManager()->CancelFX(hSndAmbient[i]);
        nAmbientTerrainType[i] = -1;
    }
    while (!sectorLRU.IsEmpty())
        delete sectorLRU.RemoveHead();
    ReleaseTextures();
    mapRes.CloseResource();
    ClearMapSize();
    bIsLoaded = FALSE;
    // Free fpUVCoords
    for (int nRot = 0; nRot < 4; nRot++)
    {
        if (uvCoords[nRot])
            delete uvCoords[nRot];
        uvCoords[nRot] = NULL;
    }
    // Free fpLongUVCoords
    for (int nRot = 0; nRot < 4; nRot++)
    {
        if (uvLongCoords[nRot])
            delete uvLongCoords[nRot];
        uvLongCoords[nRot] = NULL;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CTerrain::CreateUVInfo(SUVInfo &uvInfo, int nRot, int x, int y, float fpUVTileSize,
                            float fpUVMargin, bool isLong)
{
    assert(N_CL_PER_TILE_S == 2);
    float fYTileSize = (isLong) ? 1.0f : fpUVTileSize;

    for (int j = 0; j < 9; j += 3)
    {
        uvInfo.fpU[j] = fpUVMargin;
        uvInfo.fpU[j + 1] = fpUVTileSize / 2.f;
        uvInfo.fpU[j + 2] = fpUVTileSize - fpUVMargin;
    }
    for (int i = 0; i < 3; i++)
    {
        uvInfo.fpV[i] = fpUVMargin;
        uvInfo.fpV[i + 3] = fYTileSize / 2.f;
        uvInfo.fpV[i + 6] = fYTileSize - fpUVMargin;
    }

    // Rotate coords
    float fpTU, fpTV;
    for (int n = 0; n < nRot; n++)
    {
        // Rotate corner points
        fpTU = uvInfo.fpU[0];
        fpTV = uvInfo.fpV[0];

        uvInfo.fpU[0] = uvInfo.fpU[2];
        uvInfo.fpV[0] = uvInfo.fpV[2];
        uvInfo.fpU[2] = uvInfo.fpU[8];
        uvInfo.fpV[2] = uvInfo.fpV[8];
        uvInfo.fpU[8] = uvInfo.fpU[6];
        uvInfo.fpV[8] = uvInfo.fpV[6];

        uvInfo.fpU[6] = fpTU;
        uvInfo.fpV[6] = fpTV;

        // Rotate middle points
        fpTU = uvInfo.fpU[1];
        fpTV = uvInfo.fpV[1];

        uvInfo.fpU[1] = uvInfo.fpU[5];
        uvInfo.fpV[1] = uvInfo.fpV[5];
        uvInfo.fpU[5] = uvInfo.fpU[7];
        uvInfo.fpV[5] = uvInfo.fpV[7];
        uvInfo.fpU[7] = uvInfo.fpU[3];
        uvInfo.fpV[7] = uvInfo.fpV[3];

        uvInfo.fpU[3] = fpTU;
        uvInfo.fpV[3] = fpTV;
    }

    // Add offsets
    float fpUOff = float(x) * fpUVTileSize;
    float fpVOff = (isLong) ? 0 : float(y) * fpUVTileSize;

    for (int i = 0; i < 9; i++)
    {
        uvInfo.fpU[i] += fpUOff;
        uvInfo.fpV[i] += fpVOff;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
inline void AddObjFindInfo(SObjFindInfo &findInfo, int x, int y)
{
    if (x >= 0 && y >= 0 && x < N_CL_PSECTOR_S / 2 && y < N_CL_PSECTOR_S / 2)
        findInfo.positions[findInfo.nPosNum++] = y * (N_CL_PSECTOR_S / 2) + x;
    assert(findInfo.nPosNum <= 4);
}

void CTerrain::CreateObjectFindInfo()
{
    objFindInfo.SetSize((N_CL_PSECTOR_S + 1) * (N_CL_PSECTOR_S + 1));
    SObjFindInfo *pInfo = objFindInfo.GetData();
    for (int j = 0; j < N_CL_PSECTOR_S + 1; j++)
        for (int i = 0; i < N_CL_PSECTOR_S + 1; i++)
        {
            SObjFindInfo findInfo{};
            findInfo.nPosNum = 0;

            if ((i & 1) == 0 && (j & 1) == 0)
            {
                AddObjFindInfo(findInfo, i / 2 - 1, j / 2 - 1);
                AddObjFindInfo(findInfo, i / 2 - 1, j / 2);
                AddObjFindInfo(findInfo, i / 2, j / 2 - 1);
                AddObjFindInfo(findInfo, i / 2, j / 2);
            }
            else if ((i & 1) == 0 && (j & 1) == 1)
            {
                AddObjFindInfo(findInfo, i / 2 - 1, j / 2);
                AddObjFindInfo(findInfo, i / 2, j / 2);
            }
            else if ((i & 1) == 1 && (j & 1) == 0)
            {
                AddObjFindInfo(findInfo, i / 2, j / 2 - 1);
                AddObjFindInfo(findInfo, i / 2, j / 2);
            }
            else
                AddObjFindInfo(findInfo, i / 2, j / 2);

            *pInfo++ = findInfo;

            assert(findInfo.nPosNum > 0);
        }
}

/////////////////////////////////////////////////////////////////////////////////////
void CTerrain::SetWindForce(float fpForce)
{
    fpForce = Max(fpForce, 0.f);
    fpForce = Min(fpForce, 1.f);
    fpWindForce01 = fpForce;
    fpWindForce = fpForce * FP_MAX_WAVE_Z;
    fpWindPhaseAdd = fpForce * FP_WAVE_PHASE_MUL;

    float fpWaveLenMult = float(asin(1.f - fpForce)) + FP_PI / 1.999f;
    windDirection.Normalize();
    windDirection.x *= FP_WAVE_LEN_MUL * fpWaveLenMult;
    windDirection.y *= FP_WAVE_LEN_MUL * fpWaveLenMult;
}

void CTerrain::SetWind(CPlot3D *pWindDirection, float _fpWindForce /* 0..1 */)
{
    windDirection = *pWindDirection;
    SetWindForce(_fpWindForce);
}
/////////////////////////////////////////////////////////////////////////////////////

void CTerrain::SetMapSize(int _nMapSX, int _nMapSY)
{
    ClearMapSize();
    nXSectors = _nMapSX;
    nYSectors = _nMapSY;
    sectorPos = dbgnew POSITION *[nXSectors];
    for (int i = 0; i < nXSectors; i++)
    {
        sectorPos[i] = dbgnew POSITION[nYSectors];
        memset(sectorPos[i], 0, sizeof(POSITION) * nYSectors);
    }
}

void CTerrain::ClearMapSize()
{
    if (sectorPos)
    {
        for (int i = 0; i < nXSectors; i++)
            delete[] sectorPos[i];
        delete[] sectorPos;
        sectorPos = NULL;
    }
    nXSectors = -1;
    nYSectors = -1;
}

/////////////////////////////////////////////////////////////////////////////////////
CSector *CTerrain::GetSector(int nMapX, int nMapY, ESectorState eReqState, BOOL bTryToSetState)
{
    if (nMapX < 0 || nMapY < 0 || nMapX >= nXSectors || nMapY >= nYSectors)
        return NULL;
    assert(nSegmentCount >= 0);
    POSITION pos = sectorPos[nMapX][nMapY];
    if (pos)
    {
        CSector *pSector = sectorLRU.GetAt(pos);
        // Sector finded, advance it in LRU list
        if (pSector->GetAccessTime() < nSegmentCount)
        {
            pSector->SetAccessTime(nSegmentCount);
            sectorLRU.RemoveAt(pos);
            sectorLRU.AddHead(pSector);
            sectorPos[nMapX][nMapY] = sectorLRU.GetHeadPosition();
        }
        if (bTryToSetState)
            pSector->SetState(eReqState, this);
        if (pSector->IsHaveState(eReqState))
            return pSector;
        else
            return NULL;
    }
    if (!bTryToSetState)
        return NULL;

    CSector *pSector = NULL;
    if (!sectorLRU.IsEmpty())
        pSector = sectorLRU.GetTail();

    if (pSector == NULL || pSector->GetAccessTime() >= nSegmentCount - N_MIN_KEEP_TIME)
    {
        pSector = dbgnew CSector;
    }
    else
    {
        sectorPos[pSector->GetMapX()][pSector->GetMapY()] = NULL;
        sectorLRU.RemoveTail();
        while (!sectorLRU.IsEmpty() &&
               sectorLRU.GetTail()->GetAccessTime() < nSegmentCount - N_MIN_KEEP_TIME)
        {
            CSector *pS = sectorLRU.RemoveTail();
            sectorPos[pS->GetMapX()][pS->GetMapY()] = NULL;
            delete pS;
        }
    }
    // Load sector
    if (!pSector->Load(&mapRes, nMapX, nMapY, nTextures, nTilesPerTex, LPSTR(LPCSTR(szBaseName)),
                       this))
    {
        delete pSector;
        return NULL;
    }
    // Add sector to LRU list
    pSector->SetAccessTime(nSegmentCount);
    sectorLRU.AddHead(pSector);
    sectorPos[nMapX][nMapY] = sectorLRU.GetHeadPosition();
    // Return loaded sector
    if (bTryToSetState)
        pSector->SetState(eReqState, this);

    if (pSector->IsHaveState(eReqState))
        return pSector;
    else
        return NULL;
}

void CTerrain::ClearSectorsCache()
{
    while (sectorLRU.IsEmpty() == FALSE)
    {
        CSector *pSector = sectorLRU.RemoveHead();
        sectorPos[pSector->GetMapX()][pSector->GetMapY()] = NULL;
        delete pSector;
    }
}

/////////////////////////////////////////////////////////////////////////////////////
CPlot3D *CTerrain::GetVertex(int nX, int nY, BOOL bFromLayer)
{
    if (nX < 0 || nY < 0 || nX > GetMaxX() || nY > GetMaxY())
        return NULL;
    SVertexInfo infoVertex(nX, nY);

    CSector *pSectorCur = GetSector(infoVertex.nSectorX, infoVertex.nSectorY,
                                    CSector::ESS_GEOMETRY);
    if (!pSectorCur)
        return NULL;

    int nOffset;
    if (bFromLayer)
    {
        if (!pSectorCur->IsHaveLayers())
            return NULL;
        nOffset = N_V_PSECTOR;
    }
    else
        nOffset = 0;
    // Get vertex
    return pSectorCur->GetMeshPtr() + infoVertex.GetOffset() + nOffset;
}

static float VecMul(CPlot3D &vecCenter, CPlot3D &vec1, CPlot3D &vec2)
{
    return (vec1.x - vecCenter.x) * (vec2.y - vecCenter.y) -
           (vec1.y - vecCenter.y) * (vec2.x - vecCenter.x);
}

float CTerrain::GetZValue(float fpX, float fpY, BOOL bFromLayer)
{
    CPlot3D *pVLU, *pVRU, *pVLD, *pVRD;
    int nCX = Float2Int(fpX / FP_CL_MP_SIZE);
    int nCY = Float2Int(fpY / FP_CL_MP_SIZE);
    int nCount = 0;

    while (1)
    {
        nCount++;
        pVLU = GetVertex(nCX, nCY, bFromLayer);
        pVRU = GetVertex(nCX + 1, nCY, bFromLayer);
        pVLD = GetVertex(nCX, nCY + 1, bFromLayer);
        pVRD = GetVertex(nCX + 1, nCY + 1, bFromLayer);

        if (pVLU && pVRD)
        {
            if (pVRU && !pVLD)
                return ::GetZValue(fpX, fpY, pVLU, pVRU, pVRD);
            if (!pVRU && pVLD)
                return ::GetZValue(fpX, fpY, pVLU, pVRD, pVLD);
            if (pVRU && pVLD)
            {
                // this CRAP is REQUIRED because due to some CRAPPY reason there ARE
                // polygons with normals having negative z in terrain mesh
                if (VecMul(pVRU[0], pVRD[0], pVLU[0]) <= 0)
                    return (pVLD->z + pVRD->z) / 2;
                if (VecMul(pVLD[0], pVLU[0], pVRD[0]) <= 0)
                    return (pVLD->z + pVRD->z) / 2;
                // end of CRAP
                float fDXU = pVRU->x - fpX, fDYU = pVRU->y - fpY;
                float fDXD = pVLD->x - fpX, fDYD = pVLD->y - fpY;
                if ((fpX - pVLU->x) * (pVRD->y - pVLU->y) - (fpY - pVLU->y) * (pVRD->x - pVLU->x) >
                    0)
                {
                    if (nCount < 10) //  вдруг зациклится из-за ошибок округлени
                    {
                        if (fDXU * (pVLU->y - pVRU->y) - fDYU * (pVLU->x - pVRU->x) > 0)
                        {
                            nCY--;
                            continue;
                        }
                        if (fDXU * (pVRD->y - pVRU->y) - fDYU * (pVRD->x - pVRU->x) < 0)
                        {
                            nCX++;
                            continue;
                        }
                    }
                    return ::GetZValue(fpX, fpY, pVLU, pVRU, pVRD);
                }
                else
                {
                    if (nCount < 10) //  вдруг зациклится из-за ошибок округления
                    {
                        if (fDXD * (pVLU->y - pVLD->y) - fDYD * (pVLU->x - pVLD->x) < 0)
                        {
                            nCX--;
                            continue;
                        }
                        if (fDXD * (pVRD->y - pVLD->y) - fDYD * (pVRD->x - pVLD->x) > 0)
                        {
                            nCY++;
                            continue;
                        }
                    }
                    return ::GetZValue(fpX, fpY, pVLU, pVRD, pVLD);
                }
            }
        }
        else
            return 0;
    }
    return 0.0f;
}

// Return base Z coordinate for given vertex and depth
void CTerrain::GetZValue(float fpX, float fpY, float *pZTerrain, float *pDepth)
{
    assert(pDepth);
    float fpLayerZ = GetZValue(fpX, fpY, TRUE);
    *pZTerrain = GetZValue(fpX, fpY, FALSE);
    if (fpLayerZ > *pZTerrain)
        *pDepth = fpLayerZ - *pZTerrain;
    else
        *pDepth = 0.f;
}

// Get z values for each SXYZW from base terrain level, for unvisible terrain parts returns 0
struct SCoordInfo
{
    int nSecX, nSecY;
    int nCellX, nCellY;
};

void CTerrain::GetProjection(CXYZW4Array &coords)
{
    assert(FP_CL_MP_SIZE == 1.f);

    static CSimplyArray<SCoordInfo, SCoordInfo &> coordInfo;
    if (coords.GetSize() == 0)
        return;

    unsigned int nOldControl87 = _control87(0, 0);
    _control87(_RC_CHOP, _MCW_RC);

    coordInfo.SetSize(coords.GetSize() * 4);
    SXYZW4 *pCoord = coords.GetData();
    SCoordInfo *pCInfo = coordInfo.GetData();
    int nMinSX = 0x7FFFFFFF, nMinSY = 0x7FFFFFFF;
    int nMaxSX = 0x80000000, nMaxSY = 0x80000000;
    for (int i = 0; i < coords.GetSize(); i++, pCoord++)
    {
        for (int kk = 0; kk < 4; kk++, pCInfo++)
        {
            pCInfo->nCellX = Float2Int(pCoord->x[kk] + 0.5f);
            pCInfo->nCellY = Float2Int(pCoord->y[kk] + 0.5f);
            pCoord->z[kk] = 0.f;
            pCInfo->nSecX = pCInfo->nCellX / N_CL_PSECTOR_S;
            pCInfo->nSecY = pCInfo->nCellY / N_CL_PSECTOR_S;
            pCInfo->nCellX -= pCInfo->nSecX * N_CL_PSECTOR_S;
            pCInfo->nCellY -= pCInfo->nSecY * N_CL_PSECTOR_S;

            nMinSX = Min(nMinSX, pCInfo->nSecX);
            nMaxSX = Max(nMaxSX, pCInfo->nSecX);
            nMinSY = Min(nMinSY, pCInfo->nSecY);
            nMaxSY = Max(nMaxSY, pCInfo->nSecY);
        }
    }

    _control87(nOldControl87, 0xFFFFFFFF);

    for (int j = nMinSY; j <= nMaxSY; j++)
        for (int i = nMinSX; i <= nMaxSX; i++)
        {
            CSector *pSector = GetSector(i, j, CSector::ESS_GEOMETRY, FALSE);
            if (!pSector)
                continue;
            pCoord = coords.GetData();
            pCInfo = coordInfo.GetData();
            for (int k = 0; k < coords.GetSize(); k++, pCoord++)
            {
                for (int kk = 0; kk < 4; kk++, pCInfo++)
                {
                    if (pCInfo->nSecX != i || pCInfo->nSecY != j)
                        continue;
                    const CPlot3D *pPlot = pSector->GetMeshPtr() +
                                           pCInfo->nCellY * (N_CL_PSECTOR_S + 1) + pCInfo->nCellX;
                    pCoord->z[kk] = (pPlot->z + (pPlot + 1)->z + (pPlot + N_CL_PSECTOR_S + 1)->z +
                                     (pPlot + N_CL_PSECTOR_S + 1 + 1)->z) *
                                    0.25f;
                }
            }
        }
}

// Get terrain type using map header terrain type info for given X, Y coordinates
ETERRTYPE CTerrain::GetTerrainType(float fpX, float fpY)
{
    int nX = Float2Int(fpX / FP_CL_MP_SIZE);
    int nY = Float2Int(fpY / FP_CL_MP_SIZE);
    if (nX < 0 || nY < 0 || nX >= nXSectors * N_CL_PSECTOR_S || nY >= nYSectors * N_CL_PSECTOR_S)
        return TERRTYPE_UNDEFINED;
    int nSecX = nX / N_CL_PSECTOR_S;
    int nSecY = nY / N_CL_PSECTOR_S;
    int nTileX = (nX % N_CL_PSECTOR_S) / 2;
    int nTileY = (nY % N_CL_PSECTOR_S) / 2;
    CSector *pSector = GetSector(nSecX, nSecY, CSector::ESS_D3D);
    int nTileOffset = nTileY * (N_CL_PSECTOR_S / 2) + nTileX;
    if (pSector && pSector->IsHaveLayers())
    {
        int nObjID = *(pSector->GetObjectIDs() + nTileOffset);
        if (nObjID != -1)
        {
            float fpLayerZ = GetZValue(fpX, fpY, TRUE);
            float fpTerrainZ = GetZValue(fpX, fpY, FALSE);
            if (fpLayerZ > fpTerrainZ)
                return (
                    ETERRTYPE)tileIDs[*(pSector->GetTileIndexes() + N_TILES_PSECTOR + nTileOffset)];
        }
    }
    if (pSector)
        return (ETERRTYPE)tileIDs[*(pSector->GetTileIndexes() + nTileOffset)];
    return TERRTYPE_UNDEFINED;
}

/////////////////////////////////////////////////////////////////////////////////////

int _cdecl SectorsByCoordsCompare(const void *elem1, const void *elem2)
{
    CSector *pS1 = (CSector *)elem1, *pS2 = (CSector *)elem2;
    if (pS1->GetMapY() < pS2->GetMapY())
        return -1;
    if (pS1->GetMapY() > pS2->GetMapY())
        return 1;
    if (pS1->GetMapX() < pS2->GetMapX())
        return -1;
    if (pS1->GetMapX() > pS2->GetMapX())
        return 1;
    return 0;
}

void CTerrain::SetSun(CLight *pLight, D3DCOLORVALUE &_dcvAmbient)
{
    dcvAmbient = _dcvAmbient;

    if (!pLight)
        return;

    D3DVECTOR dvDir = pLight->GetDirection();
    float fpX = -dvDir.x;
    float fpY = -dvDir.y;
    float fpZ = -dvDir.z;

    if (fabs(fpX) < 1e-2f)
        fpX = float(_copysign(1e-2f, fpX));
    if (fabs(fpY) < 1e-2f)
        fpY = float(_copysign(1e-2f, fpY));
    if (fabs(fpZ) < 1e-2f)
        fpZ = float(_copysign(1e-2f, fpZ));

    // Normalize
    float fpMult = 1.f / FSqrt(fpX * fpX + fpY * fpY + fpZ * fpZ);
    fpX *= fpMult;
    fpY *= fpMult;
    fpZ *= fpMult;

    fpCA = fpZ / fpX;
    fpCB = fpZ / fpY;
    fpAC = fpX / fpZ;
    fpBC = fpY / fpZ;
    nAddCellsX = Float2Int(fpX * FP_SUNTRACE_LEN / FP_CL_MP_SIZE);
    nAddCellsY = Float2Int(fpY * FP_SUNTRACE_LEN / FP_CL_MP_SIZE);
    pSunLight = pLight;
    if(modTerrainShadow)modTerrainShadow->Request(nAddCellsX,nAddCellsY,fpCA,fpCB,dvDir.z<0);


    if (nSegmentsToRelight <= 0)
    {
        POSITION pos = sectorLRU.GetHeadPosition();
        while (pos)
            sectorLRU.GetNext(pos)->InvalidateState(CSector::ESS_LIGHTED);
    }
    else
    {
        // Gather array of cashed ESS_LIGHTED sectors
        static CSimplyArray<CSector *, CSector *> sArray;
        sArray.SetSize(0); // ARRAY BOUNDS CHECK
        POSITION pos = sectorLRU.GetHeadPosition();
        while (pos)
        {
            CSector *pSector = sectorLRU.GetNext(pos);
            if (pSector->IsHaveState(CSector::ESS_LIGHTED))
                sArray.Add(pSector);
        }
        // Set relight delays for each lighted sector
        qsort(sArray.GetData(), sArray.GetSize(), sizeof(CSector *), SectorsByCoordsCompare);
        if (sArray.GetSize())
        {
            float fpRelightInc = float(nSegmentsToRelight - 1) / float(sArray.GetSize());
            float fpRelightDelay = 0.0f;
            for (int i = 0; i < sArray.GetSize(); i++)
            {
                int nRelightDelay = MINT(fpRelightDelay);
                fpRelightDelay += fpRelightInc;
                sArray[i]->SetRelightDelay(nRelightDelay);
                if (nRelightDelay == 0)
                    sArray[i]->InvalidateState(CSector::ESS_LIGHTED);
            }
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////
void CTerrain::SetAutoRelightDelay(int nSegmentsNumber)
{
    nSegmentsToRelight = nSegmentsNumber;
    if (nSegmentsToRelight < 0)
        nCurSegmentsToRelight = -1;
    if (nCurSegmentsToRelight < nSegmentsToRelight)
        nCurSegmentsToRelight = nSegmentsToRelight;
}

/////////////////////////////////////////////////////////////////////////////////////

BOOL CTerrain::TraceToSun(CPlot3D *pFrom)
{
    // Get starting values
    int nX0 = nVXIndex;
    int nY0 = nVYIndex;
    int nX1 = nX0 + nAddCellsX;
    int nY1 = nY0 + nAddCellsY;
    const CPlot3D *pPlot = pSector->GetMeshPtr() + nY0 * (N_CL_PSECTOR_S + 1) + nX0;
    int nSectorX = pSector->GetMapX();
    int nSectorY = pSector->GetMapY();

    int dx, dy;
    float fpDzX, fpDzY;
    int a = nX1 - nX0;
    int b = nY1 - nY0;
    if (a < 0 || (a == 0 && IsNegative(fpCA)))
        dx = -1, a = -a, fpDzX = -fpCA;
    else
        dx = 1, fpDzX = fpCA;
    if (b < 0 || (b == 0 && IsNegative(fpCB)))
        dy = -1, b = -b, fpDzY = -fpCB;
    else
        dy = 1, fpDzY = fpCB;
    int two_a = a << 1;
    int two_b = b << 1;
    int xcrit = -b + two_a;
    int nAddLine = dy * (N_CL_PSECTOR_S + 1);
    int eps = 0;

    // This piece of code removes troubles that occure when sunlight vector components are about 0.f
    if (IsNegative(fpDzX) || IsNegative(fpDzY))
        return IsNegative(pSunLight->GetDirection().dvZ);

    float fpZCur = pFrom->z;

    for (int i = 0; i < Max(a, b); i++)
    {
        assert(fpZCur > -1e-4f);
        assert(pPlot->z > -1e-4f);
        if (Float2DW(pPlot->z) > Float2DW(fpZCur))
            return FALSE;
        if (eps <= xcrit)
        {
            nX0 += dx;
            fpZCur += fpDzX;
            eps += two_b;
            if (nX0 == -1)
            {
                nX0 = N_CL_PSECTOR_S - 1;
                nSectorX--;
                goto MoveToNewSector;
            }
            if (nX0 == N_CL_PSECTOR_S + 1)
            {
                nX0 = 1;
                nSectorX++;
                goto MoveToNewSector;
            }
            pPlot += dx;
        }
        if (eps >= a || a <= b)
        {
            nY0 += dy;
            fpZCur += fpDzY;
            eps -= two_a;
            if (nY0 == -1)
            {
                nY0 = N_CL_PSECTOR_S - 1;
                nSectorY--;
                goto MoveToNewSector;
            }
            if (nY0 == N_CL_PSECTOR_S + 1)
            {
                nY0 = 1;
                nSectorY++;
                goto MoveToNewSector;
            }
            pPlot += nAddLine;
        }
        continue;
    MoveToNewSector:
        CSector *pSectorCur = GetSector(nSectorX, nSectorY, CSector::ESS_GEOMETRY);
        if (!pSectorCur)
            return TRUE;
        pPlot = pSectorCur->GetMeshPtr() + nY0 * (N_CL_PSECTOR_S + 1) + nX0;
    }
    return TRUE;
}

// ------------------ Trace ray to first terrain's surface for visible terrain part
// ------------------------
BOOL CTerrain::TraceRayForVisibleTerrain(const CPlot3D *pPtStart, const CQuaternion *pDirection,
                                         const float fpMaxRayLen, CPlot3D *pResult)
{
    if (!IsLoaded())
        return FALSE;

    unsigned int nOldControl87 = _control87(0, 0);
    _control87(_RC_DOWN, _MCW_RC);

    CPlot3D ptOrt(0, 0, 1);
    ptOrt = pDirection->Rotate(ptOrt);

    float fpMaxOrt = Max(fabs(ptOrt.x), fabs(ptOrt.y));
    float fpMult = (fpMaxOrt > 1e-5) ? (1 / fpMaxOrt) : 1.f;
    ptOrt *= fpMult;

    CPlot3D ptStart(*pPtStart);
    ptStart *= 1 / FP_CL_MP_SIZE;

    int nSectorX = Float2Int(ptStart.x / float(N_CL_PSECTOR_S));
    int nSectorY = Float2Int(ptStart.y / float(N_CL_PSECTOR_S));
    float fpSectorSubX = float(nSectorX) * float(N_CL_PSECTOR_S);
    float fpSectorSubY = float(nSectorY) * float(N_CL_PSECTOR_S);

    CSector *pSector = GetSector(nSectorX, nSectorY, CSector::ESS_LIGHTED, FALSE);
    if (pSector && !pSector->IsVisible())
        pSector = NULL;
    int nMaxRayLen = Float2Int(fpMaxRayLen / FP_CL_MP_SIZE);
    for (int i = 0; i < nMaxRayLen; i++)
    {
        if (pSector)
        {
            int nX = Float2Int(ptStart.x - fpSectorSubX);
            int nY = Float2Int(ptStart.y - fpSectorSubY);
            if (nX < 0 || nY < 0 || nX >= N_CL_PSECTOR_S || nY >= N_CL_PSECTOR_S)
            {
                nSectorX = Float2Int(ptStart.x / float(N_CL_PSECTOR_S));
                nSectorY = Float2Int(ptStart.y / float(N_CL_PSECTOR_S));
                fpSectorSubX = float(nSectorX) * float(N_CL_PSECTOR_S);
                fpSectorSubY = float(nSectorY) * float(N_CL_PSECTOR_S);
                pSector = GetSector(nSectorX, nSectorY, CSector::ESS_LIGHTED, FALSE);
                if (pSector && !pSector->IsVisible())
                    pSector = NULL;
                i--;
                continue;
            }
            const CPlot3D *pV1 = pSector->GetMeshPtr() + nY * N_V_PSECTOR_S + nX;
            float fpMidZ = (pV1->z + (pV1 + 1)->z + (pV1 + N_V_PSECTOR_S)->z +
                            (pV1 + N_V_PSECTOR_S + 1)->z) *
                           (1.f / 4.f);
            if (ptStart.z < fpMidZ)
            {
                if (pResult)
                {
                    pResult->x = ptStart.x;
                    pResult->y = ptStart.y;
                    pResult->z = fpMidZ;
                }
                _control87(nOldControl87, 0xFFFFFFFF);
                return TRUE;
            }
        }
        ptStart += ptOrt;
    }
    _control87(nOldControl87, 0xFFFFFFFF);
    return FALSE;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////

// Set clip area, pClipPoints array must have size of 4 and be arranged to form convex figure
void CTerrain::SetClip(CPlot2D *pClipPoints)
{
    // Find minY
    float fpMinY1 = Min(pClipPoints->y, (pClipPoints + 1)->y);
    float fpMinY2 = Min((pClipPoints + 2)->y, (pClipPoints + 3)->y);
    float fpMinY = Min(fpMinY1, fpMinY2);
    fpMinY = Max(fpMinY, 0.5f);
    nClipMinSectorY = Float2Int(fpMinY - 0.5f) / N_CL_PSECTOR_S;
    // Find maxY
    float fpMaxY1 = Max(pClipPoints->y, (pClipPoints + 1)->y);
    float fpMaxY2 = Max((pClipPoints + 2)->y, (pClipPoints + 3)->y);
    float fpMaxY = Max(fpMaxY1, fpMaxY2);
    int nClipMaxSectorY = Float2Int(fpMaxY - 0.5f) / N_CL_PSECTOR_S;
    if (nClipMaxSectorY >= nYSectors)
        nClipMaxSectorY = nYSectors - 1;
    // Find minX
    float fpMinX1 = Min(pClipPoints->x, (pClipPoints + 1)->x);
    float fpMinX2 = Min((pClipPoints + 2)->x, (pClipPoints + 3)->x);
    float fpMinX = Min(fpMinX1, fpMinX2);
    fpMinX = Max(fpMinX, 0.5f);
    int nClipMinSectorX = Float2Int(fpMinX - 0.5f) / N_CL_PSECTOR_S;
    // Find maxY
    float fpMaxX1 = Max(pClipPoints->x, (pClipPoints + 1)->x);
    float fpMaxX2 = Max((pClipPoints + 2)->x, (pClipPoints + 3)->x);
    float fpMaxX = Max(fpMaxX1, fpMaxX2);
    int nClipMaxSectorX = Float2Int(fpMaxX - 0.5f) / N_CL_PSECTOR_S;
    if (nClipMaxSectorX >= nXSectors)
        nClipMaxSectorX = nXSectors - 1;

    // Set size of viewable sectors arrays
    int nXSize = nClipMaxSectorX - nClipMinSectorX + 1;
    int nYSize = nClipMaxSectorY - nClipMinSectorY + 1;
    viewableSectorsMinX.SetSize(nYSize);
    viewableSectorsMaxX.SetSize(nYSize);
    for (int i = 0; i < nYSize; i++)
    {
        viewableSectorsMinX[i] = nXSectors - 1;
        viewableSectorsMaxX[i] = 0;
    }
    // Fill viewable sectors arrays
    float fpA[4] = {}, fpB[4] = {}, fpC[4] = {}, fpRes[4] = {};
    GetLineEq(pClipPoints->x, pClipPoints->y, (pClipPoints + 1)->x, (pClipPoints + 1)->y, fpA, fpB,
              fpC);
    GetLineEq((pClipPoints + 1)->x, (pClipPoints + 1)->y, (pClipPoints + 2)->x,
              (pClipPoints + 2)->y, fpA + 1, fpB + 1, fpC + 1);
    GetLineEq((pClipPoints + 2)->x, (pClipPoints + 2)->y, (pClipPoints + 3)->x,
              (pClipPoints + 3)->y, fpA + 2, fpB + 2, fpC + 2);
    GetLineEq((pClipPoints + 3)->x, (pClipPoints + 3)->y, pClipPoints->x, pClipPoints->y, fpA + 3,
              fpB + 3, fpC + 3);

    float fpCurY = float(nClipMinSectorY + 1) * N_CL_PSECTOR_S;
    float fpStartX = float(nClipMinSectorX + 1) * N_CL_PSECTOR_S;
    for (int j = 1; j < nYSize; j++, fpCurY += N_CL_PSECTOR_S)
    {
        float fpCurX = fpStartX;
        for (int i = 1; i < nXSize; i++, fpCurX += N_CL_PSECTOR_S)
        {
            fpRes[0] = fpA[0] * fpCurX + fpB[0] * fpCurY + fpC[0];
            fpRes[1] = fpA[1] * fpCurX + fpB[1] * fpCurY + fpC[1];
            fpRes[2] = fpA[2] * fpCurX + fpB[2] * fpCurY + fpC[2];
            fpRes[3] = fpA[3] * fpCurX + fpB[3] * fpCurY + fpC[3];

            if ((IsNegative(fpRes[0]) && IsNegative(fpRes[1]) && IsNegative(fpRes[2]) &&
                 IsNegative(fpRes[3])) ||
                (IsPositive(fpRes[0]) && IsPositive(fpRes[1]) && IsPositive(fpRes[2]) &&
                 IsPositive(fpRes[3])))
            {
                if (viewableSectorsMinX[j - 1] > i - 1)
                    viewableSectorsMinX[j - 1] = i - 1;
                if (viewableSectorsMaxX[j - 1] < i)
                    viewableSectorsMaxX[j - 1] = i;
                if (viewableSectorsMinX[j] > i - 1)
                    viewableSectorsMinX[j] = i - 1;
                if (viewableSectorsMaxX[j] < i)
                    viewableSectorsMaxX[j] = i;
            }
        }
    }
}

void CTerrain::SetClip(float fpX1, float fpY1, float fpRadius1, float fpX2, float fpY2,
                       float fpRadius2)
{
    float fpA, fpB, fpC;
    GetLineEq(fpX1, fpY1, fpX2, fpY2, &fpA, &fpB, &fpC);

    fpRadius1 *= 0.5f;
    fpRadius2 *= 0.5f;

    CPlot2D pts[4];
    pts[0].x = fpX1 + fpA * fpRadius1;
    pts[0].y = fpY1 + fpB * fpRadius1;
    pts[1].x = fpX1 - fpA * fpRadius1;
    pts[1].y = fpY1 - fpB * fpRadius1;
    pts[2].x = fpX2 - fpA * fpRadius2;
    pts[2].y = fpY2 - fpB * fpRadius2;
    pts[3].x = fpX2 + fpA * fpRadius2;
    pts[3].y = fpY2 + fpB * fpRadius2;
    SetClip(pts);
}

inline void SetRange(int &a, int nMin, int nMax)
{
    if (a < nMin)
        a = nMin;
    else if (a > nMax)
        a = nMax;
}

void CTerrain::SetClip(float fpX, float fpY, float fpRadius)
{
    assert(fpRadius > 0);
    int nMinX = Float2Int(fpX - fpRadius - 0.5f) / N_CL_PSECTOR_S;
    SetRange(nMinX, 0, nXSectors - 1);
    int nMaxX = Float2Int(fpX + fpRadius - 0.5f) / N_CL_PSECTOR_S;
    SetRange(nMaxX, 0, nXSectors - 1);
    int nMinY = Float2Int(fpY - fpRadius - 0.5f) / N_CL_PSECTOR_S;
    SetRange(nMinY, 0, nYSectors - 1);
    int nMaxY = Float2Int(fpY + fpRadius - 0.5f) / N_CL_PSECTOR_S;
    SetRange(nMaxY, 0, nYSectors - 1);
    int nSize = nMaxY - nMinY + 1;
    if (nSize < 0)
        nSize = 0;
    viewableSectorsMinX.SetSize(nSize);
    viewableSectorsMaxX.SetSize(nSize);
    for (int i = 0; i < nSize; i++)
    {
        viewableSectorsMinX[i] = nMinX;
        viewableSectorsMaxX[i] = nMaxX;
    }
    nClipMinSectorY = nMinY;
}
// ------------------------------- Light sources management
// ------------------------------------------------

// Add light source
void CTerrain::AddLight(CLight *pLight)
{
    if (IsLoaded())
    {
        assert(lightInfo.Seek(pLight->GetID()) == NULL);
        SLightInfo li;
        li.pLight = pLight;
        pLight->AddRef();
        GetAffectedRegion(pLight, NULL, &li.affectedRect);
        lightInfo.SetAt(pLight->GetID(), li);
        UpdateLight(pLight, NULL, &li.affectedRect, FALSE);
        RelightRegion(&li.affectedRect);
    }
}
// Remove light source
void CTerrain::RemoveLight(CLight *pLight)
{
    if (IsLoaded())
    {
        assert(lightInfo.Seek(pLight->GetID()) != NULL);
        SLightInfo li = lightInfo.RemoveAt(pLight->GetID());
        assert(li.pLight == pLight);
        UpdateLight(pLight, NULL, &li.affectedRect, TRUE);
        RelightRegion(&li.affectedRect);
        pLight->Release();
    }
}
// Move light source
void CTerrain::MoveLight(CLight *pLight, CPlot3D *pMoveVector)
{
    if (IsLoaded())
    {
        SLightInfo *pLi = lightInfo.Seek(pLight->GetID());
        assert(pLi);
        GetAffectedRegion(pLight, pMoveVector, &pLi->affectedRect);
        UpdateLight(pLight, pMoveVector, &pLi->affectedRect, FALSE);
        CPlot3D oldCP = pLight->GetCP();
        pLight->SetCP(oldCP + *pMoveVector);
        RelightRegion(&pLi->affectedRect);
        GetAffectedRegion(pLight, NULL, &pLi->affectedRect);
        pLight->SetCP(oldCP); // because light will be moved outside
    }
}

// Set lights sources to sector
void CTerrain::UpdateLightsForSector(CSector *pSector)
{
    // Iterate trough known light sources
    for (auto pos = lightInfo.begin(); pos != lightInfo.end(); ++pos)
    {
        SLightInfo &li = pos->second;
        if (pSector->GetMapX() >= li.affectedRect.nSecX0 &&
            pSector->GetMapX() <= li.affectedRect.nSecX1 &&
            pSector->GetMapY() >= li.affectedRect.nSecY0 &&
            pSector->GetMapY() <= li.affectedRect.nSecY1)
        {
            UpdateLightForSector(pSector, li.pLight, li.pLight->GetCP(), &li.affectedRect, FALSE);
        }
    }
}

// Get region affected with this light source
void CTerrain::GetAffectedRegion(CLight *pLight, CPlot3D *pMoveVector, SSectorRect *pRectAffect)
{
    assert(pLight->GetTypeID() == LTID_POINTLIGHT);
    float fpRadius = pLight->GetRange();
    CPlot3D cp = pLight->GetCP();
    float fpX0 = cp.x - fpRadius, fpY0 = cp.y - fpRadius, fpX1 = cp.x + fpRadius,
          fpY1 = cp.y + fpRadius;
    if (pMoveVector)
    {
        if (IsNegative(pMoveVector->x))
            fpX0 += pMoveVector->x;
        else
            fpX1 += pMoveVector->x;
        if (IsNegative(pMoveVector->y))
            fpY0 += pMoveVector->y;
        else
            fpY1 += pMoveVector->y;
    }
    GetSectorsRectangle(fpX0, fpY0, fpX1, fpY1, nXSectors, nYSectors, pRectAffect);
}

// Relight region only if region have been lighted before
void CTerrain::RelightRegion(SSectorRect *pRect)
{
    for (int nSj = pRect->nSecY0; nSj <= pRect->nSecY1; nSj++)
        for (int nSi = pRect->nSecX0; nSi <= pRect->nSecX1; nSi++)
        {
            CSector *pSector = GetSector(nSi, nSj, CSector::ESS_LIGHTED, FALSE);
            if (pSector)
            {
                int nCellX0 = (nSi == pRect->nSecX0) ? pRect->nCellX0 : 0;
                int nCellY0 = (nSj == pRect->nSecY0) ? pRect->nCellY0 : 0;
                int nCellX1 = (nSi == pRect->nSecX1) ? pRect->nCellX1 : N_CL_PSECTOR_S;
                int nCellY1 = (nSj == pRect->nSecY1) ? pRect->nCellY1 : N_CL_PSECTOR_S;
                pSector->SetLight(pSunLight, this, TRUE, nCellX0, nCellY0, nCellX1, nCellY1);
            }
        }
}
// Update light source in sector's vertexes
void CTerrain::UpdateLightForSector(CSector *pSector, CLight *pLight, const CPlot3D &cp,
                                    SSectorRect *pRect, BOOL bIsRemove)
{
    int nCellX0 = (pSector->GetMapX() == pRect->nSecX0) ? pRect->nCellX0 : 0;
    int nCellY0 = (pSector->GetMapY() == pRect->nSecY0) ? pRect->nCellY0 : 0;
    int nCellX1 = (pSector->GetMapX() == pRect->nSecX1) ? pRect->nCellX1 : N_CL_PSECTOR_S;
    int nCellY1 = (pSector->GetMapY() == pRect->nSecY1) ? pRect->nCellY1 : N_CL_PSECTOR_S;

    float fpRadius2 = pLight->GetRange() * pLight->GetRange();

    for (int j = nCellY0; j <= nCellY1; j++)
    {
        SLVertex3D *pVert = pSector->GetLVertexPtr() + j * (N_CL_PSECTOR_S + 1) + nCellX0;
        CPlot3D *pPlot = pSector->GetMeshPtr() + j * (N_CL_PSECTOR_S + 1) + nCellX0;

        for (int i = nCellX0; i <= nCellX1; i++, pVert++, pPlot++)
        {
            float fpDx = pPlot->x - cp.x, fpDy = pPlot->y - cp.y, fpDz = pPlot->z - cp.z;

            CLight *pVertLight;
            POSITION pos = pVert->lightsList.GetHeadPosition();
            while (pos)
            {
                POSITION posOld = pos;
                pVertLight = pVert->lightsList.GetNext(pos);
                if (pVertLight == pLight)
                {
                    pos = posOld;
                    break;
                }
            }
            float fpLen2 = fpDx * fpDx + fpDy * fpDy + fpDz * fpDz;
            if (!bIsRemove && fpLen2 < fpRadius2)
            {
                if (pos == NULL)
                    pVert->lightsList.AddHead(pLight); // Add light source if not exist
            }
            else
            {
                if (pos)
                    pVert->lightsList.RemoveAt(pos); // Remove light source if exist
            }
        }
    }
}
// Update light source in sectors
void CTerrain::UpdateLight(CLight *pLight, CPlot3D *pMoveVector, SSectorRect *pRect, BOOL bIsRemove)
{
    CPlot3D cp = pLight->GetCP();
    if (pMoveVector)
        cp += *pMoveVector;

    for (int nSj = pRect->nSecY0; nSj <= pRect->nSecY1; nSj++)
        for (int nSi = pRect->nSecX0; nSi <= pRect->nSecX1; nSi++)
        {
            CSector *pSector = GetSector(nSi, nSj, CSector::ESS_D3D, FALSE);
            if (pSector)
                UpdateLightForSector(pSector, pLight, cp, pRect, bIsRemove);
        }
}

// Fog

BYTE CTerrain::GetFogAlpha(const CPlot3D &plot) const
{
    BYTE cRet = 0xFF;

    if (IsHaveRenderState(CTerrain::ETRS_BORDER_FOG))
    {
        float fpMMinX = plot.x - theSettings.fpTerrainBorderFogDistance - FP_CL_MP_SIZE;
        float fpMMinY = plot.y - theSettings.fpTerrainBorderFogDistance - FP_CL_MP_SIZE;
        float fpMMaxX = GetMaxX() - theSettings.fpTerrainBorderFogDistance - plot.x - FP_CL_MP_SIZE;
        float fpMMaxY = GetMaxY() - theSettings.fpTerrainBorderFogDistance - plot.y - FP_CL_MP_SIZE;

        BOOL bMarginned = (Float2DW(fpMMinX) & 0x80000000) | (Float2DW(fpMMinY) & 0x80000000) |
                          (Float2DW(fpMMaxX) & 0x80000000) | (Float2DW(fpMMaxY) & 0x80000000);
        if (bMarginned)
        {
            fpMMinX = Min(fpMMinX, 0.0f);
            fpMMinY = Min(fpMMinY, 0.0f);
            fpMMaxX = Min(fpMMaxX, 0.0f);
            fpMMaxY = Min(fpMMaxY, 0.0f);
            float fpMarginLen = FSqrt(fpMMinX * fpMMinX + fpMMinY * fpMMinY + fpMMaxX * fpMMaxX +
                                      fpMMaxY * fpMMaxY);
            fpMarginLen = Min(fpMarginLen, theSettings.fpTerrainBorderFogDistance);
            float fpAlpha = fpMarginLen / theSettings.fpTerrainBorderFogDistance * 255;
            cRet = 255 - Float2Int(fpAlpha);
        }
    }
    return cRet;
}

/////////////////////////////////////////////////////////////////////////////////////
int CTerrain::Segment()
{
    // Relight
    if (nCurSegmentsToRelight > 0)
        if (--nCurSegmentsToRelight == 0)
        {
            nCurSegmentsToRelight = nSegmentsToRelight;
            SetSun(GetSunLight(), GetAmbient());
        }
    POSITION pos = sectorLRU.GetHeadPosition();
    while (pos)
    {
        CSector *pSector = sectorLRU.GetNext(pos);
        if (pSector->IsHaveState(CSector::ESS_LIGHTED))
            pSector->RelightSegment();
    }
    // Ambient sounds
    if (IsHaveRenderState(ETRS_AMBIENT_SOUNDS))
        ProcessAmbientSounds();
    // Layers shifting
    nSegmentCount++;
    fpWindPhase = fpWindPhase + fpWindPhaseAdd;
    // Segment for each visible sector
    for (int ys = 0; ys < viewableSectorsMinX.GetSize(); ys++)
    {
        for (int xs = viewableSectorsMinX[ys]; xs <= viewableSectorsMaxX[ys]; xs++)
        {
            CSector *pSector = GetSector(xs, ys + nClipMinSectorY, CSector::ESS_LIGHTED, FALSE);
            if (pSector && pSector->IsVisible())
                pSector->Segment(this, nSegmentCount);
        }
    }

    return 1;
}

/////////////////////////////////////////////////////////////////////////////////////
// --------------------------- Ambient sounds
// --------------------------------------------------------------

struct SAmbientInfo
{
    const char *pszDirName; // directory name for find sound file
    int nNum;               // number of random sounds
    float fpScaleCoeff;     // weight calculating factor
    int nSameAs;            // reference to parent record
    float fpMaxPan;         // maximal absolute pan value
    int nPercentToPlay;     // probability to play random sound per segment
};

static SAmbientInfo ambientInfo[] = {
    // TERRTYPE_GRASS		= 0,
    { "Grass", -1, 1.0f, -1, 30, 6 },
    // TERRTYPE_GROUND		= 1,
    { "Ground", -1, 1.0f, -1, 30, 6 },
    // TERRTYPE_STONE		= 2,
    { "Stone", -1, 1.0f, -1, 30, 6 },
    // TERRTYPE_SAND		= 3,
    { "sand", -1, 1.0f, -1, 30, 6 },
    // TERRTYPE_ROCK		= 4,
    { "rock", -1, 1.5f, -1, 100, 4 },
    // TERRTYPE_FIELD		= 5,
    { "field", -1, 1, -1, 30, 6 },
    // TERRTYPE_WATER		= 6,
    { "water", -1, 3.0f, -1, 100, 6 },
    // TERRTYPE_ROAD		= 7,
    { "road", -1, 3.0f, -1, 30, 6 },
    // TERRTYPE_UNDEFINED	= 8
    { "", 0, 0, -1, 100, 0 },
    // TERRTYPE_SNOW		= 9,
    { "snow", -1, 1.0f, -1, 30, 6 },
    // TERRTYPE_ICE			= 10,
    { "Ice", -1, 3.0f, -1, 30, 6 },
    // TERRTYPE_DRYGRASS	= 11,
    { "DryGrass", -1, 1.0f, -1, 30, 6 },
    // TERRTYPE_SNOWBALLS	= 12,
    { "SnowBall", -1, 1.0f, -1, 30, 6 },
    // TERRTYPE_LAVA		= 13,
    { "lava", -1, 3.0f, -1, 100, 0 },
    // TERRTYPE_SWAMP		= 14,
    { "swamp", -1, 3.0f, -1, 100, 7 },
    // TERRTYPE_HIROCK		= 15,
    { "rock", -1, 1.5f, -1, 100, 4 },
};

void CTerrain::PrepareAmbientSounds()
{
    for (int i = 0; i < ARRAY_SIZE(ambientInfo); i++)
    {
        if (ambientInfo[i].nSameAs >= 0)
            continue;
        ambientInfo[i].nNum = 0;
        while (1)
        {
            char buf[256];

            sprintf(buf, "ambient\\%s\\%s\\day\\%i.wav", szAllodDependentSndDir.GetString(),
                    ambientInfo[i].pszDirName, ambientInfo[i].nNum + 1);
            if (NULL == theWorld.GetSoundManager()->GetFXFileFromResources(buf))
                break;
            ambientInfo[i].nNum++;
        }
    }
}

const int N_TERR_SAMPLES = 16;
const int N_SECTORS_IN_SAMPLE_S = 3;

struct SVolumeInfo
{
    int nType;
    CPlot3D dirVector;
    float fpVolume;
    int nVolume, nPan, nCount;
    HSOUND hSnd;
};

int _cdecl VICompare(const void *elem1, const void *elem2)
{
    if (((SVolumeInfo *)elem1)->fpVolume < ((SVolumeInfo *)elem2)->fpVolume)
        return 1;
    if (((SVolumeInfo *)elem1)->fpVolume > ((SVolumeInfo *)elem2)->fpVolume)
        return -1;
    return 0;
}

void CTerrain::ProcessAmbientSounds()
{
    int nT, i;
    CPlot3D cameraCP, cameraDir;
    theWorld.GetSoundManager()->GetFXListener(&cameraCP, &cameraDir);
    int nCamSecX = Float2Int(cameraCP.x / FP_CL_MP_SIZE) / N_CL_PSECTOR_S;
    int nCamSecY = Float2Int(cameraCP.y / FP_CL_MP_SIZE) / N_CL_PSECTOR_S;

    const float FP_MAX_DIST = (float(N_SECTORS_IN_SAMPLE_S / 2) + 0.5f) * float(N_CL_PSECTOR_S) *
                              FP_CL_MP_SIZE;
    const float FP_MAX_DIST2 = FP_MAX_DIST * FP_MAX_DIST;

    // Prepare volume info table
    SVolumeInfo volumeInfo[TERRTYPE_LAST];
    for (nT = 0; nT < TERRTYPE_LAST; nT++)
    {
        volumeInfo[nT].nType = nT;
        volumeInfo[nT].fpVolume = 0;
        volumeInfo[nT].nCount = 0;
    }
    // Gather sectors information
    struct SSectorSndInfo
    {
        int nType;
        float fpVolume;
        CPlot3D center;
        CPlot3D dirVector;
    };
    SSectorSndInfo sectorInfo[N_SECTORS_IN_SAMPLE_S][N_SECTORS_IN_SAMPLE_S];
    for (int j = 0; j < N_SECTORS_IN_SAMPLE_S; j++)
        for (int i = 0; i < N_SECTORS_IN_SAMPLE_S; i++)
        {
            int nSX = nCamSecX - N_SECTORS_IN_SAMPLE_S / 2 + i;
            int nSY = nCamSecY - N_SECTORS_IN_SAMPLE_S / 2 + j;
            SSectorSndInfo &sInfo = sectorInfo[i][j];

            CSector *pSector = GetSector(nSX, nSY, CSector::ESS_D3D);
            if (!pSector)
            {
                sInfo.nType = -1;
                sInfo.fpVolume = 0;
                continue;
            }
            // calc center & direction
            sInfo.center.x = (float(nSX) + 0.5f) * float(N_CL_PSECTOR_S) * FP_CL_MP_SIZE;
            sInfo.center.y = (float(nSY) + 0.5f) * float(N_CL_PSECTOR_S) * FP_CL_MP_SIZE;
            sInfo.center.z = Max(GetZValue(sInfo.center.x, sInfo.center.y, TRUE),
                                 GetZValue(sInfo.center.x, sInfo.center.y, FALSE));
            sInfo.dirVector = sInfo.center - cameraCP;
            // fill terrain types weights table
            float fpWeights[TERRTYPE_LAST] = {};
            MemSetDWord((LPDWORD)fpWeights, Float2Int(0.0f), TERRTYPE_LAST);
            for (nT = 0; nT < TERRTYPE_LAST; nT++)
            {
                int nIndex = ambientInfo[nT].nSameAs < 0 ? nT : ambientInfo[nT].nSameAs;
                fpWeights[nIndex] += float(pSector->GetTilesCount(nT)) *
                                     ambientInfo[nT].fpScaleCoeff;
            }
            // find most weighted terrain type
            float fpMaxWeight = -1;
            for (nT = 0; nT < TERRTYPE_LAST; nT++)
                if (fpWeights[nT] > fpMaxWeight)
                {
                    fpMaxWeight = fpWeights[nT];
                    sInfo.nType = nT;
                }
            // calc volume
            float fpDist2 = Min(FP_MAX_DIST2, sInfo.dirVector.x * sInfo.dirVector.x +
                                                  sInfo.dirVector.y * sInfo.dirVector.y);

            sInfo.fpVolume = 100.f * ((FP_MAX_DIST2 - fpDist2) / FP_MAX_DIST2);
            volumeInfo[sInfo.nType].fpVolume += sInfo.fpVolume * sInfo.fpVolume;
            volumeInfo[sInfo.nType].dirVector = sInfo.dirVector;
            volumeInfo[sInfo.nType].nCount++;
        }
    // Get most loud terrain types and it's volume and pan
    qsort(volumeInfo, TERRTYPE_LAST, sizeof(SVolumeInfo), VICompare);
    int nNumUsedInfo;
    for (nNumUsedInfo = 0; nNumUsedInfo < N_LASRC_NUM; nNumUsedInfo++)
    {
        if (volumeInfo[nNumUsedInfo].fpVolume <= 1e-3f)
            break;
        CPlot3D &dirVector = volumeInfo[nNumUsedInfo].dirVector;
        for (int j = 0; j < N_SECTORS_IN_SAMPLE_S; j++)
            for (int i = 0; i < N_SECTORS_IN_SAMPLE_S; i++)
            {
                SSectorSndInfo &sInfo = sectorInfo[i][j];
                if (sInfo.nType == volumeInfo[nNumUsedInfo].nType)
                    dirVector += sInfo.dirVector * sInfo.fpVolume;
            }
        // Get final volume
        volumeInfo[nNumUsedInfo].nVolume = Float2Int(FSqrt(volumeInfo[nNumUsedInfo].fpVolume));
        if (volumeInfo[nNumUsedInfo].nVolume > 100)
            volumeInfo[nNumUsedInfo].nVolume = 100;

        assert(volumeInfo[nNumUsedInfo].nVolume <= 100);
        // Get final pan
        float fpPan;
        float fpDirMod = FSqrt(dirVector.x * dirVector.x + dirVector.y * dirVector.y);
        float fpScalPro = dirVector.x * cameraDir.x + dirVector.y * cameraDir.y;
        if (fpScalPro > fpDirMod)
            fpPan = 0;
        else
        {
            float fpAngle = ACos(CorrectCosAngle(fpScalPro / fpDirMod));
            if (fpAngle > FP_PI2)
                fpAngle = FP_PI - fpAngle;
            fpPan = (100.f / FP_PI2) * fpAngle;
            if (dirVector.x * cameraDir.y < dirVector.y * cameraDir.x)
                fpPan = -fpPan;
        }
        assert(fpPan >= -100.f && fpPan <= 100.f);
        SAmbientInfo &ambInfo = ambientInfo[volumeInfo[nNumUsedInfo].nType];
        float fpMaxPan = ambInfo.fpMaxPan;
        volumeInfo[nNumUsedInfo].nPan = Float2Int(Min(Max(fpPan, -fpMaxPan), fpMaxPan));

        // Create random FX sources
        int nRandomFactor = ambInfo.nPercentToPlay;
        if (int(random.Get(1, 100)) <= nRandomFactor)
        {
            // Get random sector
            int nRndSX = 0;
            int nRndSY = 0;
            do
            {
                nRndSX = random.Get(0, N_SECTORS_IN_SAMPLE_S - 1);
                nRndSY = random.Get(0, N_SECTORS_IN_SAMPLE_S - 1);
            } while (sectorInfo[nRndSX][nRndSY].nType != volumeInfo[nNumUsedInfo].nType);
            // Get sound file name
            if (ambInfo.nNum)
            {
                char pszSoundName[256];
                sprintf(pszSoundName, "ambient\\%s\\%s\\%s\\%i.wav",
                        szAllodDependentSndDir.GetString(), ambInfo.pszDirName,
                        theWorld.GetTimeName().GetString(), random.Get(1, ambInfo.nNum));
                CPlot3D srcCP = sectorInfo[nRndSX][nRndSY].dirVector + cameraCP;
                const float FP_SS2 = float(N_CL_PSECTOR_S) * FP_CL_MP_SIZE * 0.5f;
                srcCP.x += random.GetFloat(-FP_SS2, FP_SS2);
                srcCP.y += random.GetFloat(-FP_SS2, FP_SS2);
                theWorld.GetSoundManager()->BeginFX(pszSoundName, ESNDPR_AMBIENT, srcCP, FP_SS2 * 2,
                                                    FP_MAX_DIST, FALSE);
            }
        }
    }
    // Create or update sound sources
    for (int nVolInfo = 0; nVolInfo < nNumUsedInfo; nVolInfo++)
    {
        for (i = 0; i < N_LASRC_NUM; i++)
            if (nAmbientTerrainType[i] == volumeInfo[nVolInfo].nType)
            {
                nAmbientTerrainType[i] = -1;
                volumeInfo[nVolInfo].hSnd = hSndAmbient[i];
                theWorld.GetSoundManager()->UpdateFX(volumeInfo[nVolInfo].hSnd,
                                                     volumeInfo[nVolInfo].nVolume,
                                                     volumeInfo[nVolInfo].nPan);
                break;
            }
        if (i == N_LASRC_NUM)
        {
            char pszSoundName[256];
            sprintf(pszSoundName, "ambient\\%s\\%s\\circle.wav", szAllodDependentSndDir.GetString(),
                    ambientInfo[volumeInfo[nVolInfo].nType].pszDirName);
            volumeInfo[nVolInfo].hSnd = theWorld.GetSoundManager()->BeginFX(
                pszSoundName, 1000, // highest priority for all cyclic sounds
                volumeInfo[nVolInfo].nVolume, volumeInfo[nVolInfo].nPan, TRUE, TRUE);
        }
    }
    // Remove obsolete sound sources
    for (i = 0; i < N_LASRC_NUM; i++)
        if (nAmbientTerrainType[i] >= 0)
        {
            hSndAmbient[i] = theWorld.GetSoundManager()->CancelFX(hSndAmbient[i]);
            nAmbientTerrainType[i] = -1;
        }
    // Create table with current sound sources
    for (int nVolInfo = 0; nVolInfo < nNumUsedInfo; nVolInfo++)
    {
        hSndAmbient[nVolInfo] = volumeInfo[nVolInfo].hSnd;
        nAmbientTerrainType[nVolInfo] = volumeInfo[nVolInfo].nType;
    }
}

void CTerrain::DisableAmbientSounds()
{
    RemoveRenderState(ETRS_AMBIENT_SOUNDS);
    for (int i = 0; i < N_LASRC_NUM; i++)
    {
        hSndAmbient[i] = theWorld.GetSoundManager()->CancelFX(hSndAmbient[i]);
        nAmbientTerrainType[i] = -1;
    }
}
////////////////////////////////////////////////////////////////////////////////////////////////////////
// Terraforming
void CTerrain::TerraformVertex(int nX, int nY, float zIncrement, bool needCalcNormal,
                               bool isAbsoluteHeight)
{
    CPlot3D *pWorkVertex = GetVertex(nX, nY);

    if (!pWorkVertex)
        return;

    if (isAbsoluteHeight)
        pWorkVertex->z = zIncrement;
    else
        pWorkVertex->z += zIncrement;

    if (pWorkVertex->z < 0)
        pWorkVertex->z = 0;
    SetVertex(nX, nY, *pWorkVertex);

    if (needCalcNormal)
        for (int i = -1; i <= 1; i++)
            for (int j = -1; j <= 1; j++)
                CalculateNormals(nX + i, nY + j);
}

void CTerrain::TerraformVertexHieght(CPlot3D ptPoint)
{
    int nVertexX = int(ptPoint.x + 0.5f);
    int nVertexY = int(ptPoint.y + 0.5f);
    TerraformVertex(nVertexX, nVertexY, ptPoint.z, true, true);
}

void CTerrain::TerraformVertex(CPlot3D ptPoint, float zIncrement)
{
    if (zIncrement != 0)
    {
        int nVertexX = int(ptPoint.x + 0.5f);
        int nVertexY = int(ptPoint.y + 0.5f);
        TerraformVertex(nVertexX, nVertexY, zIncrement);
    }
}

void CTerrain::SetVertex(int nX, int nY, CPlot3D vertexToSet)
{
    float fCX = float(nX) * FP_CL_MP_SIZE;
    float fCY = float(nY) * FP_CL_MP_SIZE;

    if (fabs(vertexToSet.x - fCX) < 0.95f && fabs(vertexToSet.y - fCY) < 0.95f)
    {
        CPlot3DPtrArray aVertexs;
        GetDupleVertex(nX, nY, aVertexs);

        if (vertexToSet.z < 0)
            vertexToSet.z = 0;

        for (int i = 0; i < aVertexs.GetSize(); i++)
            *aVertexs[i] = vertexToSet;
        ChangeRendererRevision();
    }
}

void CTerrain::GetDupleVertexInfo(int nX, int nY, CVertexInfoArray &aVertexs)
{
    aVertexs.RemoveAll();
    if (nX < 0 || nY < 0 || nX > GetMaxX() || nY > GetMaxY())
        return;
    SVertexInfo infoVertex(nX, nY), infoWorkVertex;
    aVertexs.Add(infoVertex);
    ASSERT(infoWorkVertex.Check());

    if (infoVertex.nLocalX == 0 && infoVertex.nSectorX > 0)
    {
        infoWorkVertex = infoVertex;
        infoWorkVertex.nSectorX--;
        infoWorkVertex.nLocalX = N_CL_PSECTOR_S;
        aVertexs.Add(infoWorkVertex);
        ASSERT(infoWorkVertex.Check());
    }
    if (infoVertex.nLocalY == 0 && infoVertex.nSectorY > 0)
    {
        infoWorkVertex = infoVertex;
        infoWorkVertex.nSectorY--;
        infoWorkVertex.nLocalY = N_CL_PSECTOR_S;
        aVertexs.Add(infoWorkVertex);
        ASSERT(infoWorkVertex.Check());
    }
    if (infoVertex.nLocalY == 0 && infoVertex.nLocalX == 0 && infoVertex.nSectorX > 0 &&
        infoVertex.nSectorY > 0)
    {
        infoWorkVertex = infoVertex;
        infoWorkVertex.nSectorY--;
        infoWorkVertex.nSectorX--;
        infoWorkVertex.nLocalY = infoWorkVertex.nLocalX = N_CL_PSECTOR_S;
        aVertexs.Add(infoWorkVertex);
        ASSERT(infoWorkVertex.Check());
    }
    if (infoVertex.nLocalX == N_CL_PSECTOR_S && infoVertex.nSectorX < nXSectors - 1)
    {
        infoWorkVertex = infoVertex;
        infoWorkVertex.nSectorX++;
        infoWorkVertex.nLocalX = 0;
        aVertexs.Add(infoWorkVertex);
        ASSERT(infoWorkVertex.Check());
    }
    if (infoVertex.nLocalY == N_CL_PSECTOR_S && infoVertex.nSectorY < nYSectors - 1)
    {
        infoWorkVertex = infoVertex;
        infoWorkVertex.nSectorY++;
        infoWorkVertex.nLocalY = 0;
        aVertexs.Add(infoWorkVertex);
        ASSERT(infoWorkVertex.Check());
    }
    if (infoVertex.nLocalY == N_CL_PSECTOR_S && infoVertex.nLocalX == N_CL_PSECTOR_S &&
        infoVertex.nSectorX < nXSectors - 1 && infoVertex.nSectorY < nYSectors - 1)
    {
        infoWorkVertex = infoVertex;
        infoWorkVertex.nSectorY++;
        infoWorkVertex.nSectorX++;
        infoWorkVertex.nLocalY = infoWorkVertex.nLocalX = 0;
        aVertexs.Add(infoWorkVertex);
        ASSERT(infoWorkVertex.Check());
    }
    ASSERT(infoWorkVertex.Check());
}

void CTerrain::GetDupleVertex(int nX, int nY, CPlot3DPtrArray &aVertexs)
{
    aVertexs.RemoveAll();
    CVertexInfoArray aInfoVertexs;
    GetDupleVertexInfo(nX, nY, aInfoVertexs);

    for (int i = 0; i < aInfoVertexs.GetSize(); i++)
    {
        CSector *pSector = GetSector(aInfoVertexs[i].nSectorX, aInfoVertexs[i].nSectorY,
                                     CSector::ESS_GEOMETRY | CSector::ESS_D3D);
        pSector = GetSector(aInfoVertexs[i].nSectorX, aInfoVertexs[i].nSectorY, CSector::ESS_D3D,
                            TRUE);
        pSector->InvalidateState(CSector::ESS_LIGHTED);
        pSector->SetModify(true);
        aVertexs.Add(pSector->GetVertex(aInfoVertexs[i].nLocalX, aInfoVertexs[i].nLocalY));
    }
}

void CTerrain::GetDupleLVertex(int nX, int nY, CLVertexPtrArray &aVertexs)
{
    aVertexs.RemoveAll();
    CVertexInfoArray aInfoVertexs;
    GetDupleVertexInfo(nX, nY, aInfoVertexs);

    for (int i = 0; i < aInfoVertexs.GetSize(); i++)
    {
        CSector *pSector = GetSector(aInfoVertexs[i].nSectorX, aInfoVertexs[i].nSectorY,
                                     CSector::ESS_GEOMETRY | CSector::ESS_D3D);
        if (!pSector)
            continue;
        pSector = GetSector(aInfoVertexs[i].nSectorX, aInfoVertexs[i].nSectorY, CSector::ESS_D3D,
                            TRUE);
        pSector->InvalidateState(CSector::ESS_LIGHTED);
        pSector->SetModify(true);
        aVertexs.Add(pSector->GetLVertex(aInfoVertexs[i].nLocalX, aInfoVertexs[i].nLocalY));
    }
}
// Normal building
void CTerrain::CalculateNormals(int nX, int nY)
{
    ChangeRendererRevision();
    CPlot3D *aBar[3][3] = {};
    aBar[0][0] = NULL;
    aBar[0][1] = GetVertex(nX, nY - 1);
    aBar[0][2] = NULL;

    aBar[1][0] = GetVertex(nX - 1, nY);
    aBar[1][1] = GetVertex(nX, nY);
    aBar[1][2] = GetVertex(nX + 1, nY);

    aBar[2][0] = NULL;
    aBar[2][1] = GetVertex(nX, nY + 1);
    aBar[2][2] = NULL;

    CPlot3D vNorm(0, 0, 0);
    AddToNormal(&vNorm, aBar[1][0], aBar[0][1], aBar[1][1]);
    AddToNormal(&vNorm, aBar[0][1], aBar[1][2], aBar[1][1]);
    AddToNormal(&vNorm, aBar[1][2], aBar[2][1], aBar[1][1]);
    AddToNormal(&vNorm, aBar[2][1], aBar[1][0], aBar[1][1]);
    vNorm.Normalize();

    CLVertexPtrArray aVertexs;
    GetDupleLVertex(nX, nY, aVertexs);
    for (int i = 0; i < aVertexs.GetSize(); i++)
        memmove(&aVertexs[i]->vNorm, &vNorm, sizeof(CPlot3D));
}

void CTerrain::AddToNormal(CPlot3D *pNorm, CPlot3D *p1, CPlot3D *p2, CPlot3D *p3)
{
    if (!pNorm || !p1 || !p2 || !p3)
        return;

    // Get plane eq
    float fpE1 = p2->x - p1->x;
    float fpE2 = p2->y - p1->y;
    float fpE4 = p3->x - p1->x;
    float fpE5 = p3->y - p1->y;

    float fpA, fpB, fpC;
    fpC = fpE1 * fpE5 - fpE2 * fpE4;

    float fpE3 = p2->z - p1->z;
    float fpE6 = p3->z - p1->z;

    fpA = fpE2 * fpE6 - fpE3 * fpE5;
    fpB = fpE3 * fpE4 - fpE1 * fpE6;

    float fpLen = (float)sqrt(fpA * fpA + fpB * fpB + fpC * fpC);
    if (fpLen != 0.0f)
    {
        pNorm->x += fpA / fpLen;
        pNorm->y += fpB / fpLen;
        pNorm->z += fpC / fpLen;
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// Изменение уровня объекта
//
void CTerrain::SetObjectHieght(int nObjID, float fObjHieght)
{
    if (nObjID < 0 || nObjID >= objects.GetSize())
        return;
    CTerrainObject *pObj = GetObject(nObjID);
    float fpShift = fObjHieght - pObj->fGlobalShift;
    pObj->fGlobalShift += fpShift;
    for (int j = 0; j < nXSectors * nYSectors; j++)
        m_aSectorShifts[j].aObjShift[nObjID] += fpShift;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
// Работа с тайлами
//
void CTerrain::SetTile(CPlot3D ptToSet, int nRot, int nTile)
{
    if (ptToSet.x < GetMaxX() && ptToSet.y < GetMaxY())
    {
        int nTileX = int(ptToSet.x) / 2;
        int nTileY = int(ptToSet.y) / 2;
        SetTile(nTileX, nTileY, nRot, nTile);
    }
}

void CTerrain::SetTile(int nX, int nY, int nRot, int nTile, bool needUpdate)
{
    int nSectorX = nX / N_TILES_PSECTOR_S;
    int nSectorY = nY / N_TILES_PSECTOR_S;
    CSector *pSector = GetSector(nSectorX, nSectorY, CSector::ESS_GEOMETRY);
    if (pSector)
        pSector->SetTile(nX % N_TILES_PSECTOR_S, nY % N_TILES_PSECTOR_S, nRot, nTile);
    else
        return;

    if (needUpdate)
    {
        Save();
        sectorLRU.RemoveAt(sectorLRU.Find(pSector));
        sectorPos[pSector->GetMapX()][pSector->GetMapY()] = NULL;
        delete pSector;
    }
}

void CTerrain::GetTile(CPlot3D ptToSet, int *pnRot, int *pnTile)
{
    if (ptToSet.x < GetMaxX() && ptToSet.y < GetMaxY())
    {
        int nTileX = int(ptToSet.x) / 2;
        int nTileY = int(ptToSet.y) / 2;
        GetTile(nTileX, nTileY, pnRot, pnTile);
    }
}

void CTerrain::GetTile(int nX, int nY, int *pnRot, int *pnTile)
{
    int nSectorX = nX / N_TILES_PSECTOR_S;
    int nSectorY = nY / N_TILES_PSECTOR_S;
    CSector *pSector = GetSector(nSectorX, nSectorY, CSector::ESS_GEOMETRY);
    pSector->GetTile(nX % N_TILES_PSECTOR_S, nY % N_TILES_PSECTOR_S, pnRot, pnTile);
}

CRect CTerrain::GetTileRect(int nTile)
{
    CRect rect;
    int nTilePos;
    if (nTile / nTilesPerTex < nTextures)
    {
        nTilePos = nTile % nTilesPerTex;
        rect.top = (nTilePos / 8) * 64;
        rect.left = (nTilePos % 8) * 64;
        rect.bottom = rect.top + 64;
        rect.right = rect.left + 64;
    }
    else
    {
        nTile -= nTilesPerTex * nTextures;
        nTilePos = nTile % 8;
        rect.top = 0;
        rect.left = (nTilePos % 8) * 64;
        rect.bottom = 512;
        rect.right = rect.left + 64;
    }
    return rect;
}
/////////////////////////////////////////////////////////////////////////////////////
int CTerrain::GetTileNumbers()
{
    return tileIDs.GetSize();
}
/////////////////////////////////////////////////////////////////////////////////////
SSectVert2 *CTerrain::GetSectorGeometryData(int nX, int nY)
{
    char szSectorName[256];
    GetSectorName(nX, nY, szBaseName.GetBuffer(0), szSectorName);
    LPBYTE pSectorData = mapRes.GetFile(szSectorName);
    pSectorData += sizeof SSectorHeader;
    return reinterpret_cast<SSectVert2 *>(pSectorData);
}
inline static SSectVert2 *GetGeomertyVector(SSectVert2 *pFirstVertex, int nX, int nY)
{
    return pFirstVertex + nY * N_V_PSECTOR_S + nX;
}
/////////////////////////////////////////////////////////////////////////////////////
void CTerrain::FixHoles()
{
    for (int nSecX = 0; nSecX < nXSectors - 1; nSecX++)
        for (int nSecY = 0; nSecY < nYSectors - 1; nSecY++)
        {
            SSectVert2 *pCurSector = GetSectorGeometryData(nSecX, nSecY);
            SSectVert2 *pSectorR = GetSectorGeometryData(nSecX + 1, nSecY);
            SSectVert2 *pSectorD = GetSectorGeometryData(nSecX, nSecY + 1);
            SSectVert2 *pSectorRD = GetSectorGeometryData(nSecX + 1, nSecY + 1);

            if (pSectorR)
                for (int i = 0; i < N_V_PSECTOR_S - 1; i++)
                    *GetGeomertyVector(pCurSector, N_V_PSECTOR_S - 1,
                                       i) = *GetGeomertyVector(pSectorR, 0, i);

            if (pSectorD)
                for (int i = 0; i < N_V_PSECTOR_S - 1; i++)
                    *GetGeomertyVector(pCurSector, i,
                                       N_V_PSECTOR_S - 1) = *GetGeomertyVector(pSectorD, i, 0);

            SSectVert2 temp = *GetGeomertyVector(pCurSector, N_V_PSECTOR_S - 1, N_V_PSECTOR_S - 1);
            if (pSectorR)
                *GetGeomertyVector(pSectorR, 0, N_V_PSECTOR_S - 1) = temp;
            if (pSectorD)
                *GetGeomertyVector(pSectorD, N_V_PSECTOR_S - 1, 0) = temp;
            if (pSectorRD)
                *GetGeomertyVector(pSectorRD, 0, 0) = temp;
        }
    mapRes.Flush();
    ClearSectorsCache();
}
/////////////////////////////////////////////////////////////////////////////////////
void CTerrain::BuildShadowMesh(CShadowBuilder &builder, CXYZW4Array &src)
{
    // initialize basic points
    SXYZW4 &src4 = src[0];
    CPlot3D p00(src4.x[3], src4.y[3], src4.z[3]), p01(src4.x[2], src4.y[2], src4.z[2]);
    CPlot3D p10(src4.x[0], src4.y[0], src4.z[0]);
    // find box, find min z, find box, find min z - until convergence
    CPlot3D ptGrow = builder.GetGrow();
    int nLowX, nHighX, nLowY, nHighY, nX, nY;
    float fpCurZ = p00.z, fpPrevZ;
    fpPrevZ = fpCurZ;
    nLowX = 0x7fffffff;
    nLowY = 0x7fffffff;
    nHighX = 0;
    nHighY = 0;
    do
    {
        float f;
        p10 += ptGrow * (fpPrevZ - fpCurZ);
        p00 += ptGrow * (fpPrevZ - fpCurZ);
        p01 += ptGrow * (fpPrevZ - fpCurZ);
        f = Min(p00.x, Min(p10.x, Min(p01.x, Min(p10.x + p01.x - p00.x)))) - 1;
        nLowX = Min(nLowX, Float2Int(f / FP_CL_MP_SIZE));
        f = Min(p00.y, Min(p10.y, Min(p01.y, Min(p10.y + p01.y - p00.y)))) - 1;
        nLowY = Min(nLowY, Float2Int(f / FP_CL_MP_SIZE));
        f = Max(p00.x, Max(p10.x, Max(p01.x, Max(p10.x + p01.x - p00.x)))) + 1;
        nHighX = Max(nHighX, Float2Int(f / FP_CL_MP_SIZE));
        f = Max(p00.y, Max(p10.y, Max(p01.y, Max(p10.y + p01.y - p00.y)))) + 1;
        nHighY = Max(nHighY, Float2Int(f / FP_CL_MP_SIZE));
        nLowX = Max(0, nLowX);
        nLowY = Max(0, nLowY);
        nHighX = Min(int(GetMaxX() - 1), nHighX);
        nHighY = Min(int(GetMaxY() - 1), nHighY);
        fpPrevZ = fpCurZ;
        for (nY = nLowY; nY <= nHighY; nY++)
            for (nX = nLowX; nX <= nHighX; nX++)
                fpCurZ = Min(fpCurZ, GetVertex(nX, nY)->z);
    } while (fpPrevZ != fpCurZ);
    // for every triangle in the box
    int nMaxTriangles = (nHighX - nLowX + 1) * (nHighY - nLowY + 1) * 4 +
                        (nHighX + nHighY - nLowX - nLowY) * 20 + 20;
    builder.AllocForTriangles(nMaxTriangles);
    for (nY = nLowY; nY < nHighY; nY++)
    {
        for (nX = nLowX; nX < nHighX; nX++)
        {
            CPlot3D *pLU = GetVertex(nX, nY);
            CPlot3D *pLD = GetVertex(nX, nY + 1);
            CPlot3D *pRU = GetVertex(nX + 1, nY);
            CPlot3D *pRD = GetVertex(nX + 1, nY + 1);
            builder.AddTriangle(*pLU, *pRU, *pRD);
            builder.AddTriangle(*pLU, *pRD, *pLD);
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////

void CTerrain::DrawModTerrainShadow(CRenderer *pRend) {
    if(!modTerrainShadow)return;
    if(modTerrainShadow->Tick(GetTickCount()))
        pRend->GetD3D()->UpdateTerrainShadow(modTerrainShadow->width,modTerrainShadow->height,modTerrainShadow->pixels.data());
    if(!modTerrainShadow->ready)return;
    pRend->FlushBuffers();
    pRend->GetD3D()->SetTerrainShadowPass(true);
    DrawBase(pRend);
    pRend->FlushBuffers();
    pRend->GetD3D()->SetTerrainShadowPass(false);
}
