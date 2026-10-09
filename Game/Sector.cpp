/**********************************************************************
 *<
        FILE: Sector.Cpp

        DESCRIPTION:  Sector viewer implementation

        CREATED BY: Ivanov Evgeny

 *>	Copyright (c) 1998, Nival Enterntainemnt
 **********************************************************************/
#include "StdAfx.h"

#include "dx11/Batchers.h"
static const char LOCAL_FILE[] = __FILE__;
static uint64_t NextTerrainPhaseRevision(){static uint64_t value=1;return ++value;}

/////////////////////////////////////////////////////////////////////////////////////
#include "Terrain.h"
#include "mapFormat.h"

#include "RandomGen.h"
#include "WorldClient.h"
#include "Renderer.h"
#include "DXGeometryProcessor.h"
#include "dx11/RendererDX11.h"
#include "ModTerrainUV.h"

const int N_VERT_PTILE = 9;
const int N_FACES_PTILE = 8;
const int N_SECTOR_LAYER_OFFSET = N_V_PSECTOR;

// Underlayer lighting constants
const float FP_MAX_LIGHTED_DEPTH = 15.f;
const float FP_MAX_LIGHTED_DEPTH_INV = 1.f / FP_MAX_LIGHTED_DEPTH;
float SectorBackgroundInverseDepth(){return FP_MAX_LIGHTED_DEPTH_INV;}
// Use this def to calculate depth sun absorbtion using square function instead of linear
#define SQUARE_DEPTH_LIGHT_ABSORBTION

// ---------------------------- Random animated tiles table ----------------------------------

static int nRandomTileAnimsProbability[] = {
    // TERRTYPE_GRASS
    1000,
    // TERRTYPE_GROUND
    1000,
    // TERRTYPE_STONE
    1000,
    // TERRTYPE_SAND
    1000,
    // TERRTYPE_ROCK
    1000,
    // TERRTYPE_FIELD
    1000,
    // TERRTYPE_WATER
    1000,
    // TERRTYPE_ROAD
    1000,
    // TERRTYPE_UNDEFINED
    1000,
    // TERRTYPE_SNOW
    1000,
    // TERRTYPE_ICE
    1000,
    // TERRTYPE_DRYGRASS
    1000,
    // TERRTYPE_SNOWBALLS
    1000,
    // TERRTYPE_LAVA
    1000,
    // TERRTYPE_SWAMP
    5,
};

//---------------------------- Sectors-based rectangle ---------------------------------------

void GetSectorsRectangle(int x0, int y0, int x1, int y1, int nSectorsX, int nSectorsY,
                         SSectorRect *pRect)
{
    // Clipping with terrain size
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x0 > nSectorsX * N_CL_PSECTOR_S)
        x0 = nSectorsX * N_CL_PSECTOR_S;
    if (y0 > nSectorsY * N_CL_PSECTOR_S)
        y0 = nSectorsY * N_CL_PSECTOR_S;
    // Set lower coordinates
    pRect->nSecX0 = x0 / N_CL_PSECTOR_S;
    pRect->nSecY0 = y0 / N_CL_PSECTOR_S;
    pRect->nCellX0 = x0 % N_CL_PSECTOR_S;
    pRect->nCellY0 = y0 % N_CL_PSECTOR_S;
    // Check for overriding vertexes
    if (pRect->nCellX0 == 0 && pRect->nSecX0 > 0)
    {
        pRect->nCellX0 = N_CL_PSECTOR_S;
        pRect->nSecX0--;
    }
    if (pRect->nCellY0 == 0 && pRect->nSecY0 > 0)
    {
        pRect->nCellY0 = N_CL_PSECTOR_S;
        pRect->nSecY0--;
    }
    // Set upper coordinates
    pRect->nSecX1 = x1 / N_CL_PSECTOR_S;
    pRect->nSecY1 = y1 / N_CL_PSECTOR_S;
    pRect->nCellX1 = x1 % N_CL_PSECTOR_S;
    pRect->nCellY1 = y1 % N_CL_PSECTOR_S;
}

void GetSectorsRectangle(float x0, float y0, float x1, float y1, int nSectorsX, int nSectorsY,
                         SSectorRect *pRect)
{
    GetSectorsRectangle(int(x0 / FP_CL_MP_SIZE), int(y0 / FP_CL_MP_SIZE),
                        int(x1 / FP_CL_MP_SIZE + 0.5f), int(y1 / FP_CL_MP_SIZE + 0.5f), nSectorsX,
                        nSectorsY, pRect);
}

/////////////////////////////////////////////////////////////////////////////////////
CSector::CSector()
{
    Clear();
    xyzwVertexes.SetSize((N_V_PSECTOR * 3) / 4);
    xyzwVertexesTransformed.SetSize(xyzwVertexes.GetSize());
    clips.SetSize(xyzwVertexes.GetSize());
}

CSector::~CSector()
{
    Free();
}

CXYZWArray CSector::xyzwVertexes;
CXYZWArray CSector::xyzwVertexesTransformed;
CByteArray CSector::clips;

/////////////////////////////////////////////////////////////////////////////////////
BOOL CSector::IsVisible()
{
    return nClipFlag == TRV_VISIBLE_NOCLIP || nClipFlag == TRV_VISIBLE_CLIP;
}

void CSector::SetClipFlag(CRenderer *pRend)
{
    nClipFlag = pRend->CheckVisibility(boundVolume);
    if (nClipFlag == TRV_FAILED)
        nClipFlag = TRV_INVISIBLE;
    if (nClipFlag == TRV_INVISIBLE)
        return;
    for (int i = 0; i < 4; i++)
    {
        subPartClipFlags[i] = pRend->CheckVisibility(subPartBVs[i]);
        if (subPartClipFlags[i] == TRV_FAILED)
            subPartClipFlags[i] = TRV_INVISIBLE;
    }
}

void CSector::SetBorderLODsFlags()
{
    // Check border sectors LODs
    bLUHigher = bRDHigher = FALSE;
    if (nLOD == 0)
    {
        // Check up and down borders
        CSector *pSector;
        pSector = pTerrain->GetSector(nMapX, nMapY - 1, CSector::ESS_LIGHTED, FALSE);
        if (pSector && pSector->nLOD == 1 && pSector->IsVisible())
            bLUHigher = TRUE;
        pSector = pTerrain->GetSector(nMapX, nMapY + 1, CSector::ESS_LIGHTED, FALSE);
        if (pSector && pSector->nLOD == 1 && pSector->IsVisible())
            bRDHigher = TRUE;
    }
    if (nLOD == 1)
    {
        // Check left and right borders
        CSector *pSector;
        pSector = pTerrain->GetSector(nMapX - 1, nMapY, CSector::ESS_LIGHTED, FALSE);
        if (pSector && pSector->nLOD == 2 && pSector->IsVisible())
            bLUHigher = TRUE;
        pSector = pTerrain->GetSector(nMapX + 1, nMapY, CSector::ESS_LIGHTED, FALSE);
        if (pSector && pSector->nLOD == 2 && pSector->IsVisible())
            bRDHigher = TRUE;
    }
}

inline void Copy12Bytes(void *fpDst, const void *fpSrc)
{
    _asm
    {
		mov ecx, [ fpSrc ]
		mov edx, [ fpDst ]
		fild qword ptr [ ECX ]
		mov eax, [ ECX + 8 ]
		fistp qword ptr [ EDX ]
		mov [ EDX + 8 ], eax
    }
}

/////////////////////////////////////////////////////////////////////////////////////
///*
BOOL CSector::Transform(CDXGeometryProcessor *pGPU)
{
    int nXStep = (nLOD >= 1 ? 2 : 1);
    int nYStep = (nLOD >= 2 ? 2 : 1);

    for (int nSubPart = 0; nSubPart < 4; nSubPart++)
    {
        if (subPartClipFlags[nSubPart] == TRV_INVISIBLE)
            continue;
        int nIndexBase = (nSubPart / 2) * (N_V_PSECTOR_S * (N_V_PSECTOR_S / 2)) +
                         (nSubPart % 2) * (N_V_PSECTOR_S / 2);
        // Prepare arrays to transform
        SXYZW *pTr = xyzwVertexes.GetData();
        BOOL bLayerRequest = bHaveLayers;
        int nLayerOffset = 0;

    ComposeLayer:
        for (int j = 0; j <= N_V_PSECTOR_S / 2; j += nYStep)
        {
            CPlot3D *pPlot = mesh.GetData() + nIndexBase + nLayerOffset + j * N_V_PSECTOR_S;
            for (int i = 0; i <= N_V_PSECTOR_S / 2; i += nXStep)
            {
                Copy12Bytes(pTr, pPlot);
                pTr++;
                pPlot += nXStep;
            }
        }
        if (bLayerRequest)
        {
            nLayerOffset = N_V_PSECTOR;
            bLayerRequest = FALSE;
            goto ComposeLayer;
        }
        int nVToTransform = pTr - xyzwVertexes.GetData();
        // Call transformation
        subPartClipFlags[nSubPart] = pGPU->TransformVertices2Projection(
            xyzwVertexes.GetData(), xyzwVertexesTransformed.GetData(), clips.GetData(),
            nVToTransform, &subPartBVs[nSubPart]);
        if (subPartClipFlags[nSubPart] == TRV_FAILED || subPartClipFlags[nSubPart] == TRV_INVISIBLE)
        {
            subPartClipFlags[nSubPart] = TRV_INVISIBLE;
            continue;
        }
        // Fill transformed values
        pTr = xyzwVertexesTransformed.GetData();
        LPBYTE pClip = clips.GetData();
        bLayerRequest = bHaveLayers;
        nLayerOffset = 0;

    DecomposeLayer:
        for (int j = 0; j <= N_V_PSECTOR_S / 2; j += nYStep)
        {
            SLVertex3D *pVert = lVertexes.GetData() + nIndexBase + nLayerOffset + j * N_V_PSECTOR_S;
            if (subPartClipFlags[nSubPart] == TRV_VISIBLE_CLIP)
            {
                for (int i = 0; i <= N_V_PSECTOR_S / 2; i += nXStep)
                {
                    Copy128Bits(&pVert->sx, pTr);
                    pVert->clip = *pClip;

                    pTr++;
                    pClip++;
                    pVert += nXStep;
                }
            }
            else
            {
                for (int i = 0; i <= N_V_PSECTOR_S / 2; i += nXStep)
                {
                    Copy128Bits(&pVert->sx, pTr);
                    pVert->clip = 0;
                    pTr++;
                    pVert += nXStep;
                }
            }
        }
        if (bLayerRequest)
        {
            nLayerOffset = N_V_PSECTOR;
            bLayerRequest = FALSE;
            goto DecomposeLayer;
        }
    }
    return nClipFlag;
}

/////////////////////////////////////////////////////////////////////////////////////
static int RemoveClippedVertexes(D3DTLVERTEX *pV, WORD *pFaces, int nVCount, int nFCount)
{
    static CWORDArray remapFaces;
    static CBYTEArray exFlags;

    remapFaces.SetSize(nVCount); // ARRAY BOUNDS CHECK
    exFlags.SetSize(nVCount);    // ARRAY BOUNDS CHECK
    memset(exFlags.GetData(), 0, nVCount * sizeof(exFlags[0]));

    for (int i = 0; i < nFCount; i++)
        exFlags[pFaces[i]] = 0xFF;

    int nNewIdx = 0;
    for (int i = 0; i < nVCount; i++)
    {
        if (exFlags[i] == 0)
            continue;
        Copy32Bytes(&pV[nNewIdx], &pV[i]);
        remapFaces[i] = nNewIdx;
        nNewIdx++;
    }

    for (int i = 0; i < nFCount; i++)
        pFaces[i] = remapFaces[pFaces[i]];

    return nNewIdx;
}

/////////////////////////////////////////////////////////////////////////////////////
inline void MakeTL(D3DTLVERTEX *pTL, LPBYTE pClip, STXVertex *pTX)
{
    /*
    pTL->tu = pTX->fpU;
    pTL->tv = pTX->fpV;
    SLVertex3D* pLV = pTX->pLVert;
    pTL->color = pLV->dwColor;
    pTL->specular = pLV->dwSpecular;
    pTL->sx = pLV->sx;
    pTL->sy = pLV->sy;
    pTL->sz = pLV->sz;
    pTL->rhw = pLV->rhw;
    *pClip = pLV->clip;
    */
    ///*
    _asm
        {       // pTL->tu = pTX->fpU;
          // pTL->tv = pTX->fpV;
		mov         ecx, [pTX] // ecx - pTX
		mov         edx, [pTL] // edx - pTL
		mov         eax, [ecx] // eax - pLV
		fild qword ptr [ecx+4]
		fistp qword ptr [edx+18h]
                // SLVertex3D* pLV = pTX->pLVert;
                // pTL->color = pLV->dwColor;
                // pTL->specular = pLV->dwSpecular;
		fild qword ptr [eax+11h]
		fistp qword ptr [edx+10h]
                // pTL->sx = pLV->sx;
                // pTL->sy = pLV->sy;
                // pTL->sz = pLV->sz;
                // pTL->rhw = pLV->rhw;
                // *pClip = pLV->clip;
		fild qword ptr [eax+0h]
		mov         edi,dword ptr [pClip]
		fistp qword ptr [edx]
		mov         cl,byte ptr [eax+10h]
		fild qword ptr [eax+0h+8]
		fistp qword ptr [edx+8]
		mov         byte ptr [edi],cl
        }
    ;
    //*/
}

/////////////////////////////////////////////////////////////////////////////////////
BOOL CSector::Draw(CRenderer *pRend, CTerrain *pTerrain, int nMat, D3DMATRIX *pMatrix)
{
    CTLPipeline *pGeometryProcessor = pRend->GetGPU()->GetGeometryProcessor();
    if (nClipFlag == TRV_INVISIBLE)
        return TRUE;

    unsigned shadowWidth=0,shadowHeight=0;
    const bool terrainShadow=pRend->GetD3D()->GetTerrainShadowSize(shadowWidth,shadowHeight);
    auto makeVertex=[&](D3DTLVERTEX *dst,LPBYTE clip,STXVertex *src) {
        MakeTL(dst,clip,src);
        if(terrainShadow) {
            // sx/sy are already projected. Carry map UV through the existing clipper.
            const CPlot3D &world=mesh[src->pLVert-lVertexes.GetData()];
            ModTerrainUV::Assign(*dst,world.x,world.y,shadowWidth,shadowHeight);
        }
    };

    // Static data
    static CTLVertexArray tlVertexes;
    static CWordArray tFaces, tClippedFaces;
    static CByteArray clips;

    static int nVPerTile[] = { 9, 6, 4 };
    static int nFPerTile[] = { 8, 4, 2 };

    ///////////////////////////////
    // Processing shiftings layers
    ///////////////////////////////
    if (bHaveLayers)
        ShiftLayers();
    // Draw by subparts
    for (int nSubPart = 0; nSubPart < 4; nSubPart++)
    {
        // check subpart for visibility
        if (subPartClipFlags[nSubPart] == TRV_INVISIBLE)
            continue;

        BOOL bNeedClip = (subPartClipFlags[nSubPart] == TRV_VISIBLE_CLIP);

        // Calc sizes of arrays
        int nTilesToDraw = txVertexes[nSubPart][nMat].GetSize() / N_VERT_PTILE;
        if (nTilesToDraw == 0)
            continue;
        int nVToDraw = nTilesToDraw * nVPerTile[nLOD];
        int nWToDraw = nTilesToDraw * nFPerTile[nLOD] * 3;
        int i;

        // Set draft sizes of arrays
        tlVertexes.SetSize(nVToDraw * 4);
        clips.SetSize(tlVertexes.GetSize());
        tFaces.SetSize(nWToDraw);
        tClippedFaces.SetSize(nWToDraw * 4);

        // Prepare vertexes
        {
            D3DTLVERTEX *pDst = tlVertexes.GetData();
            STXVertex *pSrc = txVertexes[nSubPart][nMat].GetData();
            LPBYTE pClip = clips.GetData();

            switch (nLOD)
            {
            case 0:
                for (i = 0; i < nVToDraw / 8; i++)
                {
                    makeVertex(pDst + 0, pClip + 0, pSrc + 0);
                    makeVertex(pDst + 1, pClip + 1, pSrc + 1);
                    makeVertex(pDst + 2, pClip + 2, pSrc + 2);
                    makeVertex(pDst + 3, pClip + 3, pSrc + 3);
                    makeVertex(pDst + 4, pClip + 4, pSrc + 4);
                    makeVertex(pDst + 5, pClip + 5, pSrc + 5);
                    makeVertex(pDst + 6, pClip + 6, pSrc + 6);
                    makeVertex(pDst + 7, pClip + 7, pSrc + 7);
                    pDst += 8;
                    pClip += 8;
                    pSrc += 8;
                }
                for (i = 0; i < nVToDraw % 8; i++)
                    makeVertex(pDst++, pClip++, pSrc++);
                break;
            case 1:
                for (i = 0; i < nTilesToDraw; i++)
                {
                    makeVertex(pDst++, pClip++, pSrc);
                    pSrc += 2;
                    makeVertex(pDst++, pClip++, pSrc++);
                    makeVertex(pDst++, pClip++, pSrc);
                    pSrc += 2;
                    makeVertex(pDst++, pClip++, pSrc++);
                    makeVertex(pDst++, pClip++, pSrc);
                    pSrc += 2;
                    makeVertex(pDst++, pClip++, pSrc++);
                }
                break;
            case 2:
                for (i = 0; i < nTilesToDraw; i++)
                {
                    makeVertex(pDst++, pClip++, pSrc);
                    pSrc += 2;
                    makeVertex(pDst++, pClip++, pSrc);
                    pSrc += 4;
                    makeVertex(pDst++, pClip++, pSrc);
                    pSrc += 2;
                    makeVertex(pDst++, pClip++, pSrc++);
                }
                break;
            }
        }
        // Prepare faces
        ETileMasks *pMask;
        WORD *pDst = tFaces.GetData();
        int nFInd = 0, nVInd = 0;

        switch (nLOD)
        {
        case 0:
            if (bLUHigher || bRDHigher)
            {
                pMask = tilesMasks[nSubPart][nMat].GetData();
                for (i = 0; i < nTilesToDraw; i++)
                {
                    if (bLUHigher && (*pMask & CSector::ETF_UP))
                    {
                        pDst[nFInd + 0] = nVInd + 0;
                        pDst[nFInd + 1] = nVInd + 2;
                        pDst[nFInd + 2] = nVInd + 4;

                        pDst[nFInd + 3] = nVInd + 0;
                        pDst[nFInd + 4] = nVInd + 4;
                        pDst[nFInd + 5] = nVInd + 3;

                        pDst[nFInd + 6] = nVInd + 2;
                        pDst[nFInd + 7] = nVInd + 5;
                        pDst[nFInd + 8] = nVInd + 4;

                        pDst[nFInd + 9] = nVInd + 3;
                        pDst[nFInd + 10] = nVInd + 4;
                        pDst[nFInd + 11] = nVInd + 7;

                        pDst[nFInd + 12] = nVInd + 3;
                        pDst[nFInd + 13] = nVInd + 7;
                        pDst[nFInd + 14] = nVInd + 6;

                        pDst[nFInd + 15] = nVInd + 4;
                        pDst[nFInd + 16] = nVInd + 5;
                        pDst[nFInd + 17] = nVInd + 8;

                        pDst[nFInd + 18] = nVInd + 4;
                        pDst[nFInd + 19] = nVInd + 8;
                        pDst[nFInd + 20] = nVInd + 7;

                        nFInd += 21;
                        nVInd += 9;
                    }
                    else if (bRDHigher && (*pMask & CSector::ETF_DOWN))
                    {
                        pDst[nFInd + 0] = nVInd + 0;
                        pDst[nFInd + 1] = nVInd + 1;
                        pDst[nFInd + 2] = nVInd + 4;

                        pDst[nFInd + 3] = nVInd + 0;
                        pDst[nFInd + 4] = nVInd + 4;
                        pDst[nFInd + 5] = nVInd + 3;

                        pDst[nFInd + 6] = nVInd + 1;
                        pDst[nFInd + 7] = nVInd + 2;
                        pDst[nFInd + 8] = nVInd + 5;

                        pDst[nFInd + 9] = nVInd + 1;
                        pDst[nFInd + 10] = nVInd + 5;
                        pDst[nFInd + 11] = nVInd + 4;

                        pDst[nFInd + 12] = nVInd + 3;
                        pDst[nFInd + 13] = nVInd + 4;
                        pDst[nFInd + 14] = nVInd + 6;

                        pDst[nFInd + 15] = nVInd + 4;
                        pDst[nFInd + 16] = nVInd + 8;
                        pDst[nFInd + 17] = nVInd + 6;

                        pDst[nFInd + 18] = nVInd + 4;
                        pDst[nFInd + 19] = nVInd + 5;
                        pDst[nFInd + 20] = nVInd + 8;

                        nFInd += 21;
                        nVInd += 9;
                    }
                    else
                    {
                        pDst[nFInd + 0] = nVInd + 0;
                        pDst[nFInd + 1] = nVInd + 1;
                        pDst[nFInd + 2] = nVInd + 4;

                        pDst[nFInd + 3] = nVInd + 0;
                        pDst[nFInd + 4] = nVInd + 4;
                        pDst[nFInd + 5] = nVInd + 3;

                        pDst[nFInd + 6] = nVInd + 1;
                        pDst[nFInd + 7] = nVInd + 2;
                        pDst[nFInd + 8] = nVInd + 5;

                        pDst[nFInd + 9] = nVInd + 1;
                        pDst[nFInd + 10] = nVInd + 5;
                        pDst[nFInd + 11] = nVInd + 4;

                        pDst[nFInd + 12] = nVInd + 3;
                        pDst[nFInd + 13] = nVInd + 4;
                        pDst[nFInd + 14] = nVInd + 7;

                        pDst[nFInd + 15] = nVInd + 3;
                        pDst[nFInd + 16] = nVInd + 7;
                        pDst[nFInd + 17] = nVInd + 6;

                        pDst[nFInd + 18] = nVInd + 4;
                        pDst[nFInd + 19] = nVInd + 5;
                        pDst[nFInd + 20] = nVInd + 8;

                        pDst[nFInd + 21] = nVInd + 4;
                        pDst[nFInd + 22] = nVInd + 8;
                        pDst[nFInd + 23] = nVInd + 7;

                        nFInd += 24;
                        nVInd += 9;
                    }
                    pMask++;
                }
            }
            else
                for (i = 0; i < nTilesToDraw; i++)
                {
                    pDst[nFInd + 0] = nVInd + 0;
                    pDst[nFInd + 1] = nVInd + 1;
                    pDst[nFInd + 2] = nVInd + 4;

                    pDst[nFInd + 3] = nVInd + 0;
                    pDst[nFInd + 4] = nVInd + 4;
                    pDst[nFInd + 5] = nVInd + 3;

                    pDst[nFInd + 6] = nVInd + 1;
                    pDst[nFInd + 7] = nVInd + 2;
                    pDst[nFInd + 8] = nVInd + 5;

                    pDst[nFInd + 9] = nVInd + 1;
                    pDst[nFInd + 10] = nVInd + 5;
                    pDst[nFInd + 11] = nVInd + 4;

                    pDst[nFInd + 12] = nVInd + 3;
                    pDst[nFInd + 13] = nVInd + 4;
                    pDst[nFInd + 14] = nVInd + 7;

                    pDst[nFInd + 15] = nVInd + 3;
                    pDst[nFInd + 16] = nVInd + 7;
                    pDst[nFInd + 17] = nVInd + 6;

                    pDst[nFInd + 18] = nVInd + 4;
                    pDst[nFInd + 19] = nVInd + 5;
                    pDst[nFInd + 20] = nVInd + 8;

                    pDst[nFInd + 21] = nVInd + 4;
                    pDst[nFInd + 22] = nVInd + 8;
                    pDst[nFInd + 23] = nVInd + 7;

                    nFInd += 24;
                    nVInd += 9;
                }
            break;
        case 1:
            if (bLUHigher || bRDHigher)
            {
                pMask = tilesMasks[nSubPart][nMat].GetData();
                for (i = 0; i < nTilesToDraw; i++)
                {
                    if (bLUHigher && (*pMask & CSector::ETF_LEFT))
                    {
                        pDst[nFInd + 0] = nVInd + 0;
                        pDst[nFInd + 1] = nVInd + 1;
                        pDst[nFInd + 2] = nVInd + 3;

                        pDst[nFInd + 3] = nVInd + 0;
                        pDst[nFInd + 4] = nVInd + 3;
                        pDst[nFInd + 5] = nVInd + 4;

                        pDst[nFInd + 6] = nVInd + 3;
                        pDst[nFInd + 7] = nVInd + 5;
                        pDst[nFInd + 8] = nVInd + 4;

                        nFInd += 9;
                        nVInd += 6;
                    }
                    else if (bRDHigher && (*pMask & CSector::ETF_RIGHT))
                    {
                        pDst[nFInd + 0] = nVInd + 0;
                        pDst[nFInd + 1] = nVInd + 1;
                        pDst[nFInd + 2] = nVInd + 2;

                        pDst[nFInd + 3] = nVInd + 1;
                        pDst[nFInd + 4] = nVInd + 5;
                        pDst[nFInd + 5] = nVInd + 2;

                        pDst[nFInd + 6] = nVInd + 2;
                        pDst[nFInd + 7] = nVInd + 5;
                        pDst[nFInd + 8] = nVInd + 4;

                        nFInd += 9;
                        nVInd += 6;
                    }
                    else
                    {
                        pDst[nFInd + 0] = nVInd + 0;
                        pDst[nFInd + 1] = nVInd + 1;
                        pDst[nFInd + 2] = nVInd + 3;

                        pDst[nFInd + 3] = nVInd + 0;
                        pDst[nFInd + 4] = nVInd + 3;
                        pDst[nFInd + 5] = nVInd + 2;

                        pDst[nFInd + 6] = nVInd + 2;
                        pDst[nFInd + 7] = nVInd + 3;
                        pDst[nFInd + 8] = nVInd + 5;

                        pDst[nFInd + 9] = nVInd + 2;
                        pDst[nFInd + 10] = nVInd + 5;
                        pDst[nFInd + 11] = nVInd + 4;

                        nFInd += 12;
                        nVInd += 6;
                    }
                    pMask++;
                }
            }
            else
                for (i = 0; i < nTilesToDraw; i++)
                {
                    pDst[nFInd + 0] = nVInd + 0;
                    pDst[nFInd + 1] = nVInd + 1;
                    pDst[nFInd + 2] = nVInd + 3;

                    pDst[nFInd + 3] = nVInd + 0;
                    pDst[nFInd + 4] = nVInd + 3;
                    pDst[nFInd + 5] = nVInd + 2;

                    pDst[nFInd + 6] = nVInd + 2;
                    pDst[nFInd + 7] = nVInd + 3;
                    pDst[nFInd + 8] = nVInd + 5;

                    pDst[nFInd + 9] = nVInd + 2;
                    pDst[nFInd + 10] = nVInd + 5;
                    pDst[nFInd + 11] = nVInd + 4;

                    nFInd += 12;
                    nVInd += 6;
                }
            break;
        case 2:
            for (i = 0; i < nTilesToDraw; i++)
            {
                pDst[nFInd + 0] = nVInd + 0;
                pDst[nFInd + 1] = nVInd + 1;
                pDst[nFInd + 2] = nVInd + 3;

                pDst[nFInd + 3] = nVInd + 0;
                pDst[nFInd + 4] = nVInd + 3;
                pDst[nFInd + 5] = nVInd + 2;

                nFInd += 6;
                nVInd += 4;
            }
            break;
        }
        if (nFInd > 0 && bNeedClip)
        {
            pGeometryProcessor->ClipFaces(tlVertexes.GetData(), clips.GetData(), tFaces.GetData(),
                                          tClippedFaces.GetData(), nVInd, nFInd);
            ASSERT(nFInd <= tClippedFaces.GetSize());
            nVInd = RemoveClippedVertexes(tlVertexes.GetData(), tClippedFaces.GetData(), nVInd,
                                          nFInd);
        }
        if (nFInd > 0)
        {
            pRend->GetD3D()->GetBatchers()->GetBatcher()->AddVertices(
                reinterpret_cast<STLVertex1 *>(tlVertexes.GetData()), nVInd,
                bNeedClip ? tClippedFaces.GetData() : tFaces.GetData(), nFInd);
        }
        //        if (nFInd > 0)
        //        {
        //            // Not all faces clipped out, render prepared arrays
        //            if (!pRend->RenderTriangleSet(reinterpret_cast<STLVertex1
        //            *>(tlVertexes.GetData()),
        //                                          nVInd,
        //                                          bNeedClip ? tClippedFaces.GetData() :
        //                                          tFaces.GetData(), nFInd))
        //            {
        //#ifdef _DEBUG
        //                // Find bad index
        //                for (int i = 0; i < nFInd; i++)
        //                {
        //                    WORD wBad = tFaces[i];
        //                    if (wBad >= nVInd)
        //                        wBad = wBad;
        //                }
        //#endif
        //                return FALSE;
        //            }
        //        }
    }
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////////////
void CSector::SetLOD(int _nLOD)
{
    assert("Wrong LOD value" && _nLOD >= 0 && _nLOD <= 2);
    nLOD = _nLOD;
}

inline int CSector::GetMatInd(int nTileIndex)
{
    int nMat = nTileIndex / nTilesPerTex;
    if (nMat >= nTextures)
        nMat = (nTileIndex - (nTextures * nTilesPerTex)) / N_TILE_IN_LONG_TEXTURES + nTextures;
    return nMat;
}

inline int CSector::GetTilePos(int nTileIndex)
{
    if (nTileIndex / nTilesPerTex < nTextures)
        return nTileIndex % nTilesPerTex;
    else
        return (nTileIndex - (nTilesPerTex * nTextures)) % N_TILE_IN_LONG_TEXTURES;
}

/////////////////////////////////////////////////////////////////////////////////////
static BOOL WrongFormat(const char *pName)
{
    return ReportError("Wrong format resource: " + CString(pName));
}

/////////////////////////////////////////////////////////////////////////////////////
// ----------------------------------- Load sector
// ---------------------------------------------------------
BOOL CSector::Load(CMappedResource *pResource, int _nMapX, int _nMapY, int _nTextures,
                   int _nTilesPerTex, char *pszBaseName, CTerrain *_pTerrain)
{
    Free();
    nMapX = _nMapX;
    nMapY = _nMapY;
    pTerrain = _pTerrain;
    nTextures = _nTextures;
    nTilesPerTex = _nTilesPerTex;

    // Get pointer to sector's data
    char pszSectorName[256];
    SSectorHeader header;
    DWORD dwSize;
    GetSectorName(nMapX, nMapY, pszBaseName, pszSectorName);
    pResourceData = pResource->GetFile(pszSectorName, &dwSize);
    if (!pResourceData)
        return ReportError("Unable to locate resource: " + CString(pszSectorName));
    if (dwSize < sizeof(header))
        return WrongFormat(pszSectorName);

    // Read sector header
    memcpy(&header, pResourceData, sizeof(header));
    pResourceData += sizeof(header);
    if (header.dwSignature != DW_SSECTORHEADER_SIGNATURE_NORMALS)
        return WrongFormat(pszSectorName);

    bHaveLayers = header.cFlags & C_SECTORLAYERED;
    bHaveLayerTiles = header.cFlags & C_SECTORLAYER_TILED;
    DWORD dwSectorSize = sizeof(header) + sizeof(SSectVert2) * N_V_PSECTOR +
                         sizeof(TILE_ID) * N_TILES_PSECTOR;
    if (bHaveLayers)
        dwSectorSize += sizeof(SSectVert2) * N_V_PSECTOR + sizeof(WORD) * N_TILES_PSECTOR;
    if (bHaveLayerTiles)
        dwSectorSize += sizeof(TILE_ID) * N_TILES_PSECTOR;
    if (dwSize != dwSectorSize)
        return WrongFormat(pszSectorName);

    // Read object tile IDs
    if (bHaveLayers)
    {
        nObjectIDs.SetSize(N_TILES_PSECTOR);
        LPBYTE pTemp = pResourceData + sizeof(SSectVert2) * N_V_PSECTOR * 2 +
                       sizeof(TILE_ID) * N_TILES_PSECTOR;
        if (bHaveLayerTiles)
            pTemp += sizeof(TILE_ID) * N_TILES_PSECTOR;
        LPWORD pwIDs = LPWORD(pTemp);
        int *pnIDs = nObjectIDs.GetData();
        for (int i = 0; i < N_TILES_PSECTOR; i++)
        {
            *pnIDs++ = (*pwIDs == TILE_UNDEF) ? -1 : int(*pwIDs);
            pwIDs++;
        }
    }
    else
        nObjectIDs.SetSize(0);

    // Read geometry
    if (bHaveLayers)
        mesh.SetSize(N_V_PSECTOR * 2);
    else
        mesh.SetSize(N_V_PSECTOR);

    LPBYTE pSector = pResourceData;
    BOOL bLayerReadReq = bHaveLayers;
    CPlot3D *pPlot = mesh.GetData();
    SSectVert2 *pVert2 = (SSectVert2 *)pSector;
    BOOL bProcessLayer = FALSE;
    int *pnObjectIDs = nObjectIDs.GetData();

ReadLayerGeometry:
    int nVertexPos = 0;
    float fpXOff = float(nMapX) * float(N_CL_PSECTOR_S) *
                   FP_CL_MP_SIZE; // координаты начала сектора
    float fpYOff = float(nMapY) * float(N_CL_PSECTOR_S) * FP_CL_MP_SIZE;
    for (int j = 0; j < N_V_PSECTOR_S; j++)
    {
        float fpIOff = fpXOff;
        for (int i = 0; i < N_V_PSECTOR_S; i++)
        {
            // set z coordinate
            pPlot->z = GetFloatValueByRange(pVert2->usZ, pTerrain->GetMaxZ());
            // Convert vertex
            if (bProcessLayer)
            {
                // Get object ID
                SObjFindInfo *findInfo = pTerrain->GetObjFindInfo(nVertexPos);
                int nObjectID = pnObjectIDs[findInfo->positions[0]];
                for (int nPos = 1; nPos < findInfo->nPosNum; nPos++)
                {
                    int nNewObjectID = pnObjectIDs[findInfo->positions[nPos]];
                    if (nNewObjectID == -1)
                        continue;
                    if (nObjectID == -1 || pTerrain->GetObject(nNewObjectID)->fpWindMultiplier <
                                               pTerrain->GetObject(nObjectID)->fpWindMultiplier)
                        nObjectID = nNewObjectID;
                }
                // Check for object existance for this vertex
                if (nObjectID != -1)
                {
                    if (pTerrain->GetObject(nObjectID)->eType == TOT_GRASS)
                    {
                        CPlot3D *pPlotBelow = mesh.GetData() + nVertexPos;
                        pPlot->x = pPlotBelow->x;
                        pPlot->y = pPlotBelow->y;
                    }
                    else
                    {
                        pPlot->x = fpIOff;
                        pPlot->y = fpYOff;
                        // pPlot->x = GetCoord( pVert2->sbXOff, fpIOff );
                        // pPlot->y = GetCoord( pVert2->sbYOff, fpYOff );
                    }
                }
                else
                {
                    pPlot->x = fpIOff;
                    pPlot->y = fpYOff;
                    pPlot->z = pTerrain->GetMinZ();
                }
            }
            else
            {
                pPlot->x = GetCoord(pVert2->sbXOff, fpIOff);
                pPlot->y = GetCoord(pVert2->sbYOff, fpYOff);
            }
            fpIOff += FP_CL_MP_SIZE;
            pPlot++;
            pVert2++;
            nVertexPos++;
        }
        fpYOff += FP_CL_MP_SIZE;
    }
    // Read layer geometry
    if (bLayerReadReq)
    {
        pSector += sizeof(SSectVert2) * N_V_PSECTOR;
        bLayerReadReq = FALSE;
        bProcessLayer = TRUE;
        goto ReadLayerGeometry;
    }
    // Set sector state
    AddState(ESS_GEOMETRY);
    return TRUE;
}

// ----------------------------------- Load D3D data
// ---------------------------------------------------------
void CSector::LoadD3D()
{
    assert(IsHaveState(ESS_GEOMETRY));
    LPBYTE pSector = pResourceData;

    // Create lvertexes
    if (bHaveLayers)
        lVertexes.SetSize(N_V_PSECTOR * 2);
    else
        lVertexes.SetSize(N_V_PSECTOR);

    fpMinZ = 1e30f;
    fpMaxZ = 0.0f;

    BOOL bLayerReadReq = bHaveLayers;
    SLVertex3D *pLVert = lVertexes.GetData();
    CPlot3D *pPlot = mesh.GetData();
    SSectVert2 *pVert2 = (SSectVert2 *)pSector;
    BOOL bProcessLayer = FALSE;
    int *pnObjectIDs = nObjectIDs.GetData();

ReadLayerLVertexes:
    // «агрузка LVertex-ов и вычесление дл€ них нормалей
    int nVertexPos = 0;
    for (int j = 0; j < N_V_PSECTOR_S; j++)
    {
        for (int i = 0; i < N_V_PSECTOR_S; i++)
        {
            fpMinZ = Min(pPlot->z, fpMinZ);
            fpMaxZ = Max(pPlot->z, fpMaxZ);

            pLVert->cAlpha = 0xFF;
            pLVert->emmR = 0;
            pLVert->emmG = 0;
            pLVert->emmB = 0;
            pLVert->wObjectID = WORD(-1);
            // Convert vertex
            if (bProcessLayer)
            {
                // Get object ID
                SObjFindInfo *findInfo = pTerrain->GetObjFindInfo(nVertexPos);
                int nObjectID = pnObjectIDs[findInfo->positions[0]];
                for (int nPos = 1; nObjectID == -1 && nPos < findInfo->nPosNum; nPos++)
                    nObjectID = pnObjectIDs[findInfo->positions[nPos]];
                pLVert->wObjectID = WORD(nObjectID);
                if (nObjectID != -1)
                {
                    CTerrainObject *pTerrObj = pTerrain->GetObject(nObjectID);
                    pLVert->cAlpha = pTerrObj->cAlpha;
                    pLVert->emmR = pTerrObj->dcvEmissive.r;
                    pLVert->emmG = pTerrObj->dcvEmissive.g;
                    pLVert->emmB = pTerrObj->dcvEmissive.b;
                }
                pLVert->baseVertex.x = pPlot->x;
                pLVert->baseVertex.y = pPlot->y;
                pLVert->baseVertex.z = pPlot->z;
                pLVert[-N_SECTOR_LAYER_OFFSET].fpDepth = pPlot->z - pPlot[-N_SECTOR_LAYER_OFFSET].z;
            }
            GetNormal(pVert2->uNormal, pLVert->vNorm.x, pLVert->vNorm.y, pLVert->vNorm.z);
            // advance pointers
            pLVert++;
            pPlot++;
            pVert2++;
            nVertexPos++;
        }
    }

    // Read layer geometry vertexes
    if (bLayerReadReq)
    {
        pSector += sizeof(SSectVert2) * N_V_PSECTOR;
        bLayerReadReq = FALSE;
        bProcessLayer = TRUE;
        goto ReadLayerLVertexes;
    }

    // Calculating number of tiles in each material group and fill tile indexes array
    pSector += sizeof(SSectVert2) * N_V_PSECTOR;
    int nMatCount = (nTextures + N_NUMBER_OF_LONG_TEXTURES) * 2; //  оличество материалов
    int matCounts[4][256];

    int nAnimCount = 0;
    ZERO(matCounts);

    tilesCounts.SetSize(TERRTYPE_LAST);
    MemSetInt(tilesCounts.GetData(), 0, TERRTYPE_LAST);

    nTileIndexes.SetSize(bHaveLayerTiles ? N_TILES_PSECTOR * 2 : N_TILES_PSECTOR);
    int *pTileInd = nTileIndexes.GetData();
    TILE_ID *pTId = (TILE_ID *)pSector;

    BYTE *pTileRot = m_aTileRotation;
    for (int j = 0; j < N_TILES_PSECTOR_S; j++)
        for (int i = 0; i < N_TILES_PSECTOR_S; i++)
        {
            *pTileRot++ = GetTileRot(*pTId);
            int nTileInd = GetTileInd(*pTId++);
            int nMatInd = GetMatInd(nTileInd);
            int nSubPart = 2 * ((2 * j) / N_TILES_PSECTOR_S) + (2 * i) / N_TILES_PSECTOR_S;
            *pTileInd++ = nTileInd;
            if (pTerrain->GetTileAnimationPhases(nTileInd))
                nAnimCount++;
            matCounts[nSubPart][nMatInd]++;
            tilesCounts[pTerrain->GetTerrainType(nTileInd)]++;
        }
    //	Same for optional second layer
    if (bHaveLayerTiles)
    {
        int *pObjID = nObjectIDs.GetData();
        for (int j = 0; j < N_TILES_PSECTOR_S; j++)
            for (int i = 0; i < N_TILES_PSECTOR_S; i++)
            {
                if (*pObjID++ != -1)
                {
                    assert(*pTId != TILE_UNDEF);
                    int nTileInd = GetTileInd(*pTId);
                    int nMatInd = GetMatInd(nTileInd) + (nTextures + N_NUMBER_OF_LONG_TEXTURES);
                    int nSubPart = 2 * ((2 * j) / N_TILES_PSECTOR_S) + (2 * i) / N_TILES_PSECTOR_S;
                    *pTileInd = nTileInd;
                    if (pTerrain->GetTileAnimationPhases(nTileInd))
                        nAnimCount++;
                    matCounts[nSubPart][nMatInd]++;
                    tilesCounts[pTerrain->GetTerrainType(nTileInd)]++;
                }
                else
                {
                    // The render pass skips layer tiles without an object, even if a tile ID remains.
                    *pTileInd = -1;
                }
                pTileInd++;
                pTId++;
            }
    }

    // Setting sizes of arrays
    for (int j = 0; j < 4; j++)
    {
        tilesMasks[j].SetSize(nMatCount);
        txVertexes[j].SetSize(nMatCount);
        for (int i = 0; i < nMatCount; i++)
        {
            tilesMasks[j][i].SetSize(matCounts[j][i]);
            txVertexes[j][i].SetSize(matCounts[j][i] * N_VERT_PTILE);
        }
    }

    tilesAnimation.SetSize(nAnimCount);
    rendererPhaseRevision=NextTerrainPhaseRevision();
    if (bHaveLayerTiles)
        bHaveLayerTiles = TRUE;

    // Creating faces by materials groups
    ZERO(matCounts); // used as tiles counters

    pTId = (TILE_ID *)pSector;
    BOOL bLayerTilesReq = bHaveLayerTiles;
    int nGeomIndexAdd = 0;
    int nMatOffset = 0;
    nAnimCount = 0;
    bProcessLayer = FALSE;
CreateLayerTiles:
    for (int j = 0; j < N_TILES_PSECTOR_S; j++)
    {
        for (int i = 0; i < N_TILES_PSECTOR_S; i++)
        {
            // Get pointer to terrain object and alpha value of tile vertexes
            CTerrainObject *pTerrObj;
            if (bProcessLayer)
            {
                int nObjIndex = *pnObjectIDs++;
                if (nObjIndex == -1)
                {
                    pTId++;
                    continue;
                }
                pTerrObj = pTerrain->GetObject(nObjIndex);
            }
            if (*pTId == TILE_UNDEF)
            {
                pTId++;
                continue;
            }
            int nGeomVIndex = j * ((N_V_PSECTOR_S)*N_CL_PER_TILE_S) + i * N_CL_PER_TILE_S +
                              nGeomIndexAdd;

            // Check is tile present
            // Get UV info for this tile
            int nTileInd = GetTileInd(*pTId);
            int nMatInd = GetMatInd(nTileInd) + nMatOffset;
            int nSubPart = 2 * ((2 * j) / N_TILES_PSECTOR_S) + (2 * i) / N_TILES_PSECTOR_S;

            int nTileRot = GetTileRot(*pTId);
            int nTilePos = GetTilePos(nTileInd);
            SUVInfo *pUVInfo = pTerrain->GetUVInfo(nTileRot, nTilePos, nMatInd);
            pTId++;

            // Setup texture, pointers to geometry vertexes and faces
            assert(N_CL_PER_TILE_S == 2);
            int nTIndex = matCounts[nSubPart][nMatInd];
            int nVPos = nTIndex * N_VERT_PTILE;
            int nFPos = nTIndex * N_FACES_PTILE * 3;

            STXVertex *pTXVert = &txVertexes[nSubPart][nMatInd][nVPos];
            SLVertex3D *plVertexes = &lVertexes[nGeomVIndex];

            // Setup animation info
            int nAnimPhases = pTerrain->GetTileAnimationPhases(nTileInd);
            if (nAnimPhases)
            {
                SAnimatedTileInfo *pAInf = &tilesAnimation[nAnimCount++];
                pAInf->pTXVertexes = pTXVert;
                pAInf->nTileRot = nTileRot;
                pAInf->nTilePos = nTilePos;
                pAInf->nCurPhase = random.Get(nAnimPhases - 1);
                pAInf->nPhasesTotal = nAnimPhases;
                pAInf->nAnimProbability =
                    nRandomTileAnimsProbability[pTerrain->GetTerrainType(nTileInd)];
                pUVInfo = pTerrain->GetUVInfo(nTileRot, nTilePos + pAInf->nCurPhase);
            }

            // Fill TXVert
            int n = 0;
            for (int nj = 0; nj < 3; nj++)
            {
                for (int ni = 0; ni < 3; ni++)
                {
                    // Setting texture coordinates
                    pTXVert->fpU = pUVInfo->fpU[n];
                    pTXVert->fpV = 1.f - pUVInfo->fpV[n];

                    // Setting pointer to geometry vertex
                    SLVertex3D *pLVert = plVertexes + ni;
                    pTXVert->pLVert = pLVert;
                    pTXVert++;
                    n++;
                }
                plVertexes += N_V_PSECTOR_S;
            }

            // Setup masks
            BYTE *pMask = &tilesMasks[nSubPart][nMatInd][nTIndex];
            if (j == 0)
                *pMask = CSector::ETF_UP;
            else if (j == N_TILES_PSECTOR_S - 1)
                *pMask = CSector::ETF_DOWN;
            else
                *pMask = 0;
            if (i == 0)
                *pMask |= CSector::ETF_LEFT;
            else if (i == N_TILES_PSECTOR_S - 1)
                *pMask |= CSector::ETF_RIGHT;

            // Tile created
            matCounts[nSubPart][nMatInd]++;
        }
    }
    // Read layer tiles
    if (bLayerTilesReq)
    {
        bLayerTilesReq = FALSE;
        nGeomIndexAdd = N_V_PSECTOR;
        nMatOffset = nTextures + N_NUMBER_OF_LONG_TEXTURES;
        pnObjectIDs = nObjectIDs.GetData();
        bProcessLayer = TRUE;
        goto CreateLayerTiles;
    }
    // Create bounding volumes
    float fpSectorSize = float(N_CL_PSECTOR_S) * FP_CL_MP_SIZE;
    float fpXOff = float(nMapX) * fpSectorSize;
    float fpYOff = float(nMapY) * fpSectorSize;
    float fpHalfSectorSize = fpSectorSize / 2.f;
    fpMaxZ += FP_CL_MP_SIZE;
    fpMinZ -= FP_CL_MP_SIZE; // Add margin for waves
    float fpCenterZ = (fpMaxZ - fpMinZ) / 2.f + fpMinZ;
    boundVolume.SetCenter(CPlot3D(fpXOff + fpHalfSectorSize, fpYOff + fpHalfSectorSize, fpCenterZ));
    boundVolume.SetBox(CPlot3D(-fpHalfSectorSize, -fpHalfSectorSize, fpMinZ - fpCenterZ),
                       CPlot3D(fpHalfSectorSize, fpHalfSectorSize, fpMaxZ - fpCenterZ));

    for (int j = 0; j < 2; j++)
        for (int i = 0; i < 2; i++)
        {
            int s = j * 2 + i;
            subPartBVs[s].SetCenter(
                CPlot3D(fpXOff + fpHalfSectorSize * 0.5f + float(i) * fpHalfSectorSize,
                        fpYOff + fpHalfSectorSize * 0.5f + float(j) * fpHalfSectorSize, fpCenterZ));
            subPartBVs[s].SetBox(CPlot3D(-fpHalfSectorSize * 0.5f, -fpHalfSectorSize * 0.5f,
                                         fpMinZ - fpCenterZ),
                                 CPlot3D(fpHalfSectorSize * 0.5f, fpHalfSectorSize * 0.5f,
                                         fpMaxZ - fpCenterZ));
        }

    // Fill LVertexes with lights
    pTerrain->UpdateLightsForSector(this);
    AddState(ESS_D3D);

    // Calculate ObjID entered in this sector
    if (bHaveLayers)
    {
        SLVertex3D *pVert = lVertexes.GetData() + N_SECTOR_LAYER_OFFSET;
        for (int j = 0; j < N_V_PSECTOR_S * N_V_PSECTOR_S; j++, pVert++)
        {
            int nObjID = int(pVert->wObjectID);
            if (nObjID != int(WORD(-1)))
            {
                int i;
                for (i = 0; i < m_aPresentObj.GetSize(); i++)
                    if (m_aPresentObj[i] == nObjID)
                        break;
                if (i == m_aPresentObj.GetSize())
                    m_aPresentObj.Add(nObjID);
            }
        }
    }
    // set shifts
    if (bHaveLayers)
    {
        SSectorShiftInfo
            &aSectorShiftInfo = pTerrain->m_aSectorShifts[nMapY * pTerrain->GetXSectors() + nMapX];

        for (int i = 0; i < m_aPresentObj.GetSize(); i++)
        {
            CTerrainObject *pObj = pTerrain->GetObject(m_aPresentObj[i]);
            aSectorShiftInfo.aObjShift[m_aPresentObj[i]] = pObj->fGlobalShift;
        }
        ShiftLayers();
    }
}

/////////////////////////////////////////////////////////////////////////////////////
void CSector::ShiftLayers()
{
    ASSERT(bHaveLayers);
    SSectorShiftInfo
        &aSectorShiftInfo = pTerrain->m_aSectorShifts[nMapY * pTerrain->GetXSectors() + nMapX];
    for (;;)
    {
        int i = 0;
        for (; i < m_aPresentObj.GetSize(); i++)
            if (aSectorShiftInfo.aObjShift[m_aPresentObj[i]] != 0)
                break;
        if (i == m_aPresentObj.GetSize())
            break;
        SLVertex3D *pVert = lVertexes.GetData() + N_SECTOR_LAYER_OFFSET;
        CPlot3D *pPt = mesh.GetData() + N_SECTOR_LAYER_OFFSET;
        int nShiftObjID = m_aPresentObj[i];
        float fpShift = aSectorShiftInfo.aObjShift[m_aPresentObj[i]];
        for (int j = 0; j < N_V_PSECTOR_S * N_V_PSECTOR_S; j++, pVert++, pPt++)
        {
            int nObjID = int(pVert->wObjectID);
            if (nObjID == nShiftObjID) //!= int(WORD(-1)) )
            {
                pVert->baseVertex.z += fpShift; // aSectorShiftInfo.aObjShift[nObjID];
                pPt->z += fpShift;
            }
        }
        aSectorShiftInfo.aObjShift[m_aPresentObj[i]] = 0;
    }
}

/////////////////////////////////////////////////////////////////////////////////////
void CSector::LightVertex(CPlot3D *pPlot, SLVertex3D *pVertex, CTerrain *pTerrain,
                          BOOL bIsLayerVertex)
{
    // Decompose vertex color values
    D3DCOLORVALUE color = {};
    color.r = pVertex->baseR;
    color.g = pVertex->baseG;
    color.b = pVertex->baseB;
    // Loop trough light sources
    D3DCOLORVALUE dcvAddLight = { 0, 0, 0, 0 };
    POSITION pos = pVertex->lightsList.GetHeadPosition();
    while (pos)
    {
        CLight *pLight = pVertex->lightsList.GetNext(pos);
        CPlot3D cp = pLight->GetCP();
        // Calculate light vector
        CPlot3D vLight(cp.x - pPlot->x, cp.y - pPlot->y, cp.z - pPlot->z);
        float vLightLen = FSqrt(vLight.x * vLight.x + vLight.y * vLight.y + vLight.z * vLight.z);
        float vLenSqInv = 1.f / vLightLen;
        vLight.x *= vLenSqInv;
        vLight.y *= vLenSqInv;
        vLight.z *= vLenSqInv;
        // Calculating depth multiplier
        float fpDirFactor = 1.f;
        if (bIsLayerVertex == FALSE && bHaveLayers)
        {
            int nObjectID = int(pVertex[N_SECTOR_LAYER_OFFSET].wObjectID);
            if (nObjectID != int(WORD(-1)) && IsPositive(pVertex->fpDepth))
            {
                fpDirFactor = 1.f -
#ifdef SQUARE_DEPTH_LIGHT_ABSORBTION
                              vLight.z * pVertex->fpDepth *
#endif
                                  pVertex->fpDepth * FP_MAX_LIGHTED_DEPTH_INV *
                                  pTerrain->GetObject(nObjectID)->fpTransparencyInv * vLight.z;
                fpDirFactor = Max(fpDirFactor, 0.0f);
            }
        }
        D3DCOLORVALUE emm = { pVertex->emmR, pVertex->emmG, pVertex->emmB, 1 };
        D3DCOLORVALUE newColor = pLight->LightVertexForTerrain(fpDirFactor, emm,
                                                               *((CPlot3D *)&pVertex->vNorm),
                                                               vLight, vLightLen);
        if (pLight->CheckFlag(LIT_SPECULAR))
        {
            dcvAddLight.r = Max(dcvAddLight.r, newColor.r);
            dcvAddLight.g = Max(dcvAddLight.g, newColor.g);
            dcvAddLight.b = Max(dcvAddLight.b, newColor.b);
            bHaveSpecular = true;
        }
        else
        {
            color.r = Max(color.r, newColor.r);
            color.g = Max(color.g, newColor.g);
            color.b = Max(color.b, newColor.b);
        }
    }
    //
    color.r = Min(Max(color.r, dcvAddLight.r), 1.0f);
    color.g = Min(Max(color.g, dcvAddLight.g), 1.0f);
    color.b = Min(Max(color.b, dcvAddLight.b), 1.0f);
    //
    // Compose light values for vertex
    pVertex->dwColor = RGBA_MAKE(MINT(color.r * float(0xFF)), MINT(color.g * float(0xFF)),
                                 MINT(color.b * float(0xFF)), pVertex->cAlpha);
    // Set specular component for vertex
    if (bHaveSpecular)
    {
        pVertex->dwSpecular = RGBA_MAKE(int(dcvAddLight.r * 255), int(dcvAddLight.g * 255),
                                        int(dcvAddLight.b * 255), pTerrain->GetFogAlpha(*pPlot));
    }
    else
        pVertex->dwSpecular = RGBA_MAKE(0, 0, 0, pTerrain->GetFogAlpha(*pPlot));
}
/////////////////////////////////////////////////////////////////////////////////////

void CSector::LightVertexWithSun(CLight *pSunLight, CPlot3D *pPlot, SLVertex3D *pVertex,
                                 CTerrain *pTerrain, BOOL bIsLayerVertex)
{
    // Do not trace underlayer vertexes in order to avoid darkening near water edges
    // BOOL bTraceCheckReq = !( bIsLayerVertex && IsNegative(
    // pVertex[-N_SECTOR_LAYER_OFFSET].fpDepth ) );
    BOOL bTraceCheckReq = (bIsLayerVertex == FALSE &&
                           pTerrain->IsHaveRenderState(CTerrain::ETRS_TRACE_SHADOWS));
    D3DCOLORVALUE dcvAmbient = pTerrain->GetAmbient();
    float fpDirFactor = 1;
    // Calc ambient light value
    D3DCOLORVALUE emm = {};
    emm.r = pVertex->emmR;
    emm.g = pVertex->emmG;
    emm.b = pVertex->emmB;

    if (bHaveLayers && bIsLayerVertex == FALSE)
    {
        SLVertex3D *pUpLayerVertex = pVertex + N_SECTOR_LAYER_OFFSET;
        int nObjectID = int(pUpLayerVertex->wObjectID);
        if (nObjectID != int(WORD(-1)) && IsPositive(pVertex->fpDepth))
        {
            // That is underlayer vertex, calculate ambient and direct multipliers
            float fpDepthPrecalc = pVertex->fpDepth *
#ifdef SQUARE_DEPTH_LIGHT_ABSORBTION
                                   pVertex->fpDepth *
#endif
                                   FP_MAX_LIGHTED_DEPTH_INV *
                                   pTerrain->GetObject(nObjectID)->fpTransparencyInv;
            float fpAmbFactor = Max(1 - fpDepthPrecalc, 0.0f);
            dcvAmbient.r *= fpAmbFactor;
            dcvAmbient.g *= fpAmbFactor;
            dcvAmbient.b *= fpAmbFactor;

            fpDirFactor = fpDepthPrecalc *
#ifdef SQUARE_DEPTH_LIGHT_ABSORBTION
                          pSunLight->GetCP().z *
#endif
                          pSunLight->GetCP().z;

            fpDirFactor = Max(1 - fpDirFactor, 0.0f);

            emm.r = Max(pUpLayerVertex->emmR, pVertex->emmR);
            emm.g = Max(pUpLayerVertex->emmG, pVertex->emmG);
            emm.b = Max(pUpLayerVertex->emmB, pVertex->emmB);
        }
    }
    // Perform ray trace
    if (bTraceCheckReq && !pTerrain->TraceToSun(pPlot))
    {
        pVertex->baseR = Min(dcvAmbient.r + emm.r, 1.0f);
        pVertex->baseG = Min(dcvAmbient.g + emm.g, 1.0f);
        pVertex->baseB = Min(dcvAmbient.b + emm.b, 1.0f);
        return;
    }
    D3DCOLORVALUE dcvDir = pSunLight->LightVertex(fpDirFactor, emm, *((CPlot3D *)&pVertex->vNorm));
    pVertex->baseR = Max(dcvDir.r, dcvAmbient.r);
    pVertex->baseG = Max(dcvDir.g, dcvAmbient.g);
    pVertex->baseB = Max(dcvDir.b, dcvAmbient.b);
}

/////////////////////////////////////////////////////////////////////////////////////

void CSector::SetLight(CLight *pLight, CTerrain *pTerrain, BOOL bIsDynOnly, int x1, int y1, int x2,
                       int y2)
{
    assert(IsHaveState(ESS_D3D));

    bHaveSpecular = false;

    unsigned int nOldControl87 = _control87(0, 0);
    _control87(_RC_CHOP, _MCW_RC);

    pTerrain->SetSector(this);
    if (bHaveLayers)
    {
        SLVertex3D *pLayerVert = lVertexes.GetData() + N_SECTOR_LAYER_OFFSET + y1 * (N_V_PSECTOR_S);
        CPlot3D *pLayerPlot = mesh.GetData() + N_SECTOR_LAYER_OFFSET + y1 * (N_V_PSECTOR_S);
        for (int j = y1; j <= y2; j++)
        {
            pTerrain->SetVYIndex(j);
            pLayerVert += x1;
            pLayerPlot += x1;
            for (int i = x1; i <= x2; i++)
            {
                pTerrain->SetVXIndex(i);
                if (pLayerVert->wObjectID != int(WORD(-1)))
                {
                    if (!bIsDynOnly)
                        LightVertexWithSun(pLight, pLayerPlot, pLayerVert, pTerrain, TRUE);
                    LightVertex(pLayerPlot, pLayerVert, pTerrain, TRUE);
                }
                pLayerVert++;
                pLayerPlot++;
            }
            pLayerVert += (N_CL_PSECTOR_S - x2);
            pLayerPlot += (N_CL_PSECTOR_S - x2);
        }
    }
    SLVertex3D *pVert = lVertexes.GetData() + y1 * (N_V_PSECTOR_S);
    CPlot3D *pPlot = mesh.GetData() + y1 * (N_V_PSECTOR_S);
    for (int j = y1; j <= y2; j++)
    {
        pTerrain->SetVYIndex(j);
        pVert += x1;
        pPlot += x1;
        for (int i = x1; i <= x2; i++)
        {
            pTerrain->SetVXIndex(i);
            if (!bIsDynOnly)
                LightVertexWithSun(pLight, pPlot, pVert, pTerrain, FALSE);
            LightVertex(pPlot, pVert, pTerrain, FALSE);
            pVert++;
            pPlot++;
        }
        pVert += (N_CL_PSECTOR_S - x2);
        pPlot += (N_CL_PSECTOR_S - x2);
    }
    AddState(ESS_LIGHTED);

    _control87(nOldControl87, 0xFFFFFFFF);

    if (bIsDynOnly == FALSE)
        nRelightDelay = -1;
}

/////////////////////////////////////////////////////////////////////////////////////
void CSector::Free()
{
    lVertexes.SetSize(0);
    for (int j = 0; j < 4; j++)
    {
        for (int i = 0; i < tilesMasks[j].GetSize(); i++)
            tilesMasks[j][i].SetSize(0);
        for (int i = 0; i < txVertexes[j].GetSize(); i++)
            txVertexes[j][i].SetSize(0);
    }
    Clear();
}

/////////////////////////////////////////////////////////////////////////////////////
void CSector::Clear()
{
    nMapX = -1;
    nMapY = -1;
    nLOD = 0;
    bLUHigher = bRDHigher = FALSE;
    bHaveLayers = FALSE;
    bHaveLayerTiles = FALSE;
    pResourceData = NULL;
    pTerrain = NULL;
    nTextures = -1;
    nTilesPerTex = -1;
    state = ESS_NO;
    nMngAccessTime = 0;
    nRelightDelay = 0;
    nClipFlag = TRV_INVISIBLE;
    m_isModifid = false;
    m_fShiftHieght = 0.0f;
    rendererPhaseRevision=NextTerrainPhaseRevision();
    bHaveSpecular = false;
}

/////////////////////////////////////////////////////////////////////////////////////

BOOL CSector::SetState(ESectorState st, CTerrain *pTerrain)
{
    if (IsHaveState(st))
        return TRUE;
    if (st >= ESS_D3D && !IsHaveState(ESS_D3D))
        LoadD3D();
    if (st >= ESS_LIGHTED && !IsHaveState(ESS_LIGHTED))
        SetLight(pTerrain->GetSunLight(), pTerrain);
    return IsHaveState(st);
}

void CSector::SetRelightDelay(int nNewRelightDelay)
{
    if (nRelightDelay < 0 || nNewRelightDelay < nRelightDelay)
        nRelightDelay = nNewRelightDelay;
}

void CSector::RelightSegment()
{
    if (nRelightDelay >= 0 && --nRelightDelay <= 0)
    {
        nRelightDelay = -1;
        InvalidateState(ESS_LIGHTED);
    }
}

/////////////////////////////////////////////////////////////////////////////////////
const float FP_SPECULAR_POWER = 50; // 30.f;

void CSector::Segment(CTerrain *pTerrain, int nSegmentNumber)
{
    /////////////////////////////
    // Processing animated tiles
    /////////////////////////////
    if (nSegmentNumber % 3 == 0 && pTerrain->IsHaveRenderState(CTerrain::ETRS_ANIMATED_TILES))
    {
        SAnimatedTileInfo *pAInf = tilesAnimation.GetData();
        for (int i = 0; i < tilesAnimation.GetSize(); i++, pAInf++)
        {
            if (pAInf->nCurPhase == 0)
                if (pAInf->nAnimProbability < 1000)
                    if (random.Get(1, 1000) > pAInf->nAnimProbability)
                        continue;
            pAInf->nCurPhase = (pAInf->nCurPhase + 1) % pAInf->nPhasesTotal;
            rendererPhaseRevision=NextTerrainPhaseRevision();
            SUVInfo *pUVInfo = pTerrain->GetUVInfo(pAInf->nTileRot,
                                                   pAInf->nTilePos + pAInf->nCurPhase);
            STXVertex *pTXVert = pAInf->pTXVertexes;
            for (int j = 0; j < 9; j++)
            {
                pTXVert->fpU = pUVInfo->fpU[j];
                pTXVert->fpV = 1.f - pUVInfo->fpV[j];
                pTXVert++;
            }
        }
    }
    ///////////////////////////////
    // Processing waves
    ///////////////////////////////
    if (!bHaveLayers || !pTerrain->IsHaveRenderState(CTerrain::ETRS_WAVES))
        return;
    // Get wind info
    CPlot3D windDirection;
    float windForce, windPhase;
    pTerrain->GetWindInfo(&windDirection, &windForce, &windPhase);
    float fpAddSinX = windDirection.x * FP_CL_MP_SIZE, fpAddSinY = windDirection.y * FP_CL_MP_SIZE;
    // Get pointer to layer vertexes
    CPlot3D *pPlot = mesh.GetData() + N_SECTOR_LAYER_OFFSET;
    SLVertex3D *pVert = lVertexes.GetData() + N_SECTOR_LAYER_OFFSET;
    // Get pointer to noise
    const float *pNoise = pTerrain->GetWindNoiseInfo();
    // Get initial value for single
    float fpSineValY = pPlot->x * windDirection.x + pPlot->y * windDirection.y;
    fpSineValY = float(fmod(fpSineValY + 2 * FP_PI, 2 * FP_PI));
    // Set initial object ID
    int nObjectID = -1;
    ETERR_OBJ_TYPE eObjectType = TOT_UNDEF;
    float fpStartObjectPhase = 0.0f;
    float fpObjectWindForce = 0.0f;
    float fpWarpSpeed = 0.0f;
    float fpObjR = 0.0f;
    float fpObjG = 0.0f;
    float fpObjB = 0.0f;
    // Loop trough all layer vertexes
    for (int j = 0; j < N_V_PSECTOR_S; j++)
    {
        float fpSineVal = fpSineValY;
        for (int i = 0; i < N_V_PSECTOR_S; i++)
        {
            int nObjID = int(pVert->wObjectID);
            if (nObjID != int(WORD(-1)))
            {
                if (nObjID != nObjectID)
                {
                    // Get terrain object for this vertex
                    nObjectID = nObjID;
                    CTerrainObject *pObject = pTerrain->GetObject(nObjectID);
                    eObjectType = pObject->eType;
                    fpObjectWindForce = windForce * pObject->fpWindMultiplier;
                    // CRAP
                    fpWarpSpeed = 0.05f; // pObject->fpWarpSpeed * 0.01f;
                    float fpObjSpecular = pObject->fpWindMultiplier * pTerrain->GetWindForce() *
                                          FP_SPECULAR_POWER;
                    fpObjR = fpObjSpecular * pObject->fpR;
                    fpObjG = fpObjSpecular * pObject->fpG;
                    fpObjB = fpObjSpecular * pObject->fpB;
                    float fpNewPhase = windPhase * pObject->fpWindMultiplier;
                    fpNewPhase = float(fmod(fpNewPhase, 2 * FP_PI));
                    float fpDPhase = fpNewPhase - fpStartObjectPhase;
                    fpStartObjectPhase = fpNewPhase;
                    fpSineValY += fpDPhase;
                    fpSineVal += fpDPhase;
                }
                float fpSinAlpha = fpSineVal + *pNoise;
                float fpSine = pTerrain->FastSine(fpSinAlpha);
                // That should be used on unregular network
                // float fpSin = float( sin( pVert->fpX * windDirection.x
                //	+ pVert->fpY * windDirection.y + windPhase + *pNoise ) );
                if (IsPositive(fpSine))
                {
                    // Set specular component
                    pVert->dwSpecular = RGBA_MAKE(BYTE(Float2Int(fpSine * fpObjR)),
                                                  BYTE(Float2Int(fpSine * fpObjG)),
                                                  BYTE(Float2Int(fpSine * fpObjB)),
                                                  RGBA_GETALPHA(pVert->dwSpecular));
                }
                else
                    pVert->dwSpecular = RGBA_MAKE(0, 0, 0, RGBA_GETALPHA(pVert->dwSpecular));

                float fpWaveValue = fpSine * fpObjectWindForce;
                if (eObjectType == TOT_GRASS)
                {
                    // Do not set wave to grass borders
                    if (IsNegative(pVert->fpDepth))
                    {
                        // Set X & Y of the vertex according the wave value
                        pPlot->x = fpWaveValue + pVert->baseVertex.x;
                        pPlot->y = fpWaveValue + pVert->baseVertex.y;
                    }
                }
                else
                {
                    // Set Z of the vertex according the wave value
                    pPlot->z = fpWaveValue * 0.25f + pVert->baseVertex.z;
                    fpSinAlpha = theWorld.GetPreciseTime() *
                                 fpWarpSpeed; //( i + j ) * 3.14159f;// +
                    pPlot->x = pTerrain->FastSine(i * 3.1415926f / 4 + fpSinAlpha) *
                                   fpObjectWindForce * 3 +
                               pVert->baseVertex.x;
                    pPlot->y = pTerrain->FastSine(j * 3.1415926f / 4 + fpSinAlpha) *
                                   fpObjectWindForce * 3 +
                               pVert->baseVertex.y;
                }
            }
            pVert++;
            pPlot++;
            pNoise++;
            fpSineVal += fpAddSinX;
        }
        fpSineValY += fpAddSinY;
    }
}

//-------------------------------------------------------------------------------------------------------
void CSector::Save(LPBYTE pData)
{
    pData += sizeof SSectorHeader;
    for (int i = 0; i < N_V_PSECTOR_S * N_V_PSECTOR_S; i++, pData += sizeof SSectVert2)
        GetSaveVertex(i, (SSectVert2 *)pData);

    if (bHaveLayers)
        pData += sizeof(SSectVert2) * N_V_PSECTOR_S * N_V_PSECTOR_S;

    TILE_ID *pTID = (TILE_ID *)pData;
    for (int i = 0; i < N_TILES_PSECTOR; i++, pTID++)
        *pTID = FormTileID(m_aTileRotation[i], nTileIndexes[i]);

    m_isModifid = false;
}

void CSector::CopyRendererState(EIRenderTerrainSectorV5& row,std::vector<uint32_t>& phases) const
{
    row.lod=nLOD;row.subparts=0;row.stitch=(bLUHigher?1u:0u)|(bRDHigher?2u:0u);
    if(nClipFlag!=TRV_INVISIBLE)for(unsigned i=0;i<4;++i)
        if(subPartClipFlags[i]!=TRV_INVISIBLE)row.subparts|=1u<<i;
    if(row.phase_revision!=rendererPhaseRevision || phases.size()!=tilesAnimation.GetSize()){
        phases.resize(tilesAnimation.GetSize());
        for(size_t i=0;i<phases.size();++i)phases[i]=tilesAnimation[int(i)].nCurPhase;
    }
    row.phase_revision=rendererPhaseRevision;row.phases=phases.data();row.phase_count=uint32_t(phases.size());
}
void CSector::CopyRendererSector(void* destination)
{
    LPBYTE bytes=static_cast<LPBYTE>(destination)+sizeof(SSectorHeader);
    if(IsHaveState(ESS_D3D))for(int i=0;i<N_V_PSECTOR;++i)
        GetSaveVertex(i,reinterpret_cast<SSectVert2*>(bytes+i*sizeof(SSectVert2)));
    bytes+=sizeof(SSectVert2)*N_V_PSECTOR*(bHaveLayers?2:1);
    TILE_ID* tiles=reinterpret_cast<TILE_ID*>(bytes);
    for(int i=0;i<N_TILES_PSECTOR;++i)tiles[i]=FormTileID(m_aTileRotation[i],nTileIndexes[i]);
    // Deliberately does not call Save: source modified/save state is preserved.
}

void CSector::GetSaveVertex(int nNum, SSectVert2 *pVertexToSave)
{
    CPlot3D *pVertex = &mesh[nNum];
    SLVertex3D *pLVertex = &lVertexes[nNum];

    // Converte normal
    pVertexToSave->uNormal = GetNormal(pLVertex->vNorm.x, pLVertex->vNorm.y, pLVertex->vNorm.z);

    // Converte z offset
    pVertexToSave->usZ = GetUShortValueByRange(pVertex->z, pTerrain->GetMaxZ());

    // Converte x,y offset
    float fCX = float((nMapX * (N_V_PSECTOR_S - 1)) + nNum % N_V_PSECTOR_S) * FP_CL_MP_SIZE;
    float fCY = float((nMapY * (N_V_PSECTOR_S - 1)) + nNum / N_V_PSECTOR_S) * FP_CL_MP_SIZE;
    pVertexToSave->sbXOff = GetOffset(pVertex->x, fCX);
    pVertexToSave->sbYOff = GetOffset(pVertex->y, fCY);
}

//-------------------------------------------------------------------------------------------------------
void CSector::SetTile(int nX, int nY, int nRot, int nTile)
{
    nTileIndexes[nY * N_TILES_PSECTOR_S + nX] = nTile;
    m_aTileRotation[nY * N_TILES_PSECTOR_S + nX] = nRot;
    pTerrain->ChangeRendererRevision();
    m_isModifid = true;
}

void CSector::GetTile(int nX, int nY, int *nRot, int *nTile)
{
    *nTile = nTileIndexes[nY * N_TILES_PSECTOR_S + nX];
    *nRot = m_aTileRotation[nY * N_TILES_PSECTOR_S + nX];
}
