#pragma once
/**********************************************************************
 *<
        FILE: Sector.H

        DESCRIPTION:  Sector viewer header

        CREATED BY: Ivanov Evgeny

 *>	Copyright (c) 1998, Nival Entertainment
 **********************************************************************/
#include <cstdint>
#include <vector>
#include "Base3D.h"
#include "CMappedResource.h"
#include "Geometry.h"
#include "BoundCube.h"
#include "mapFormat.h"
#include "typedefs.h"

class CTerrain;
class CDXGeometryProcessor;
//-------------------------- Mapping coordinates of tile -------------------------------------

struct SUVInfo
{
    float fpU[9]; // U coordinates
    float fpV[9]; // V coordinates
};

#pragma pack(1)
struct STXVertex
{
    SLVertex3D *pLVert;
    float fpU, fpV;
};
#pragma pack()

struct SAnimatedTileInfo
{
    STXVertex *pTXVertexes; // pointer to TX vertexes
    int nTileRot;           // base rotation of tile
    int nTilePos;           // texture offset of tile
    int nCurPhase;          // current phase
    int nPhasesTotal;       // total count of phases
    int nAnimProbability;   // probability to play animation phase ( per cent to 100 )
};
//---------------------------- Sectors-based rectangle ---------------------------------------

struct SSectorRect
{
    int nSecX0, nSecY0;
    int nSecX1, nSecY1;

    int nCellX0, nCellY0;
    int nCellX1, nCellY1;
};
void GetSectorsRectangle(int x0, int y0, int x1, int y1, int nSectorsX, int nSectorsY,
                         SSectorRect *pRect);
void GetSectorsRectangle(float x0, float y0, float x1, float y1, int nSectorsX, int nSectorsY,
                         SSectorRect *pRect);

//------------------------------------ Typedefs ----------------------------------------------

// Array of texture vertexes
typedef CSimplyArray<STXVertex, STXVertex &> CSTXVertexArray;
typedef CSimplyArray<SAnimatedTileInfo, SAnimatedTileInfo &> CTileAnimArray;

typedef unsigned ESectorState;
typedef BYTE ETileMasks;
//------------------------------------- CSector ----------------------------------------------

struct EIRenderTerrainSectorV5;

class CSector
{
public:
    enum
    {
        ESS_NO = 0x0000,       // Sector is free
        ESS_GEOMETRY = 0x0001, // Sector geometry loaded
        ESS_D3D = 0x0002,      // D3D values calculated
        ESS_LIGHTED = 0x0004,  // Light calculated
    };
    enum
    {
        ETF_UP = 1,
        ETF_DOWN = 2,
        ETF_LEFT = 4,
        ETF_RIGHT = 8
    };

private:
    bool bHaveSpecular;          // this sector have a specular lighting component
    ESectorState state;          // state of sector data
    int nMapX, nMapY;            // Coordinates of sector in global map
    BOOL bHaveLayers;            // TRUE if sector have layer
    BOOL bHaveLayerTiles;        // TRUE if sector layer have tiles ( obsolete )	// Сейчас значения
                                 // не имеет
    LPBYTE pResourceData;        // pointer to sector's resource data
    CTerrain *pTerrain;          // pointer to terrain object
    int nTextures, nTilesPerTex; // Texturing info

    // Clipping
    SBoundingVolume boundVolume;   // main bounding volume to check clipping
    int nClipFlag;                 // main clipping mode
    SBoundingVolume subPartBVs[4]; // subparts bounding volumes
    int subPartClipFlags[4];       // subparts clipping modes

    float fpMinZ, fpMaxZ;     // z boundaries, valid with ESS_D3D
    CByteArray m_aPresentObj; // objects ID entered in this sector

    // Level of detail
    int nLOD;
    BOOL bLUHigher, bRDHigher;

    CArray<CPlot3D, CPlot3D &> mesh;            // geometry only( for ESS_GEOMETRY state )
    CArray<SLVertex3D, SLVertex3D &> lVertexes; // full vertexes information( for other states )
    CIntArray nTileIndexes;                     // tile indexes
    BYTE m_aTileRotation[N_TILES_PSECTOR_S * N_TILES_PSECTOR_S];
    CIntArray nObjectIDs; // layers object ID's

    CArray<CByteArray, CByteArray &> tilesMasks[4];           // arrays of tiles masks ( for LOD )
    CArray<CSTXVertexArray, CSTXVertexArray &> txVertexes[4]; // Texture vertexes

    CTileAnimArray tilesAnimation; // Tiles animation information
    uint64_t rendererPhaseRevision = 1;
    bool rendererUvsDeferred = false;
    CIntArray tilesCounts;         // Number of tiles each type

    // For sector manager
    int nMngAccessTime;
    int nRelightDelay;

    // Static data
    static CXYZWArray xyzwVertexes;
    static CXYZWArray xyzwVertexesTransformed;
    static CByteArray clips;

    // Terraforming
    bool m_isModifid;
    float m_fShiftHieght;

private:
    // Compute light of vertex
    void LightVertexWithSun(CLight *pSunLight, CPlot3D *pPlot, SLVertex3D *pVertex,
                            CTerrain *pTerrain, BOOL bIsLayerVertex);
    void LightVertex(CPlot3D *pPlot, SLVertex3D *pVertex, CTerrain *pTerrain, BOOL bIsLayerVertex);

    // Add sector state
    void AddState(ESectorState st) { state |= st; }

    // Clear sector
    void Clear();

    // Tile information
    int GetMatInd(int nTileIndex);
    int GetTilePos(int nTileIndex);

    // Process shifting
    void ShiftLayers();

public:
    CSector();
    ~CSector();

    // Load full 3D data
    void LoadD3D();
    void Save(LPBYTE pData);
    void GetSaveVertex(int nNum, SSectVert2 *pVertexToSave);

    // Load/free sector
    BOOL Load(CMappedResource *pResource, int nMapX, int nMapY, int nTextures, int nTilesPerTex,
              char *pszBaseName, CTerrain *pTerrain);
    void Free();

    // Draw sector
    BOOL IsVisible();
    BOOL IsHaveMaterial(int nMat);
    void SetClipFlag(CRenderer *pRend);
    void SetBorderLODsFlags();
    BOOL Transform(CDXGeometryProcessor *pGPU);
    BOOL Draw(CRenderer *pRend, CTerrain *pTerrain, int nMat,
              D3DMATRIX *pMatrix); // Draw sector with material nTex

    // Light sector
    void SetLight(CLight *pLight, CTerrain *pTerrain, BOOL bIsDynOnly = FALSE, int x1 = 0,
                  int y1 = 0, int x2 = N_CL_PSECTOR_S, int y2 = N_CL_PSECTOR_S);

    // For sector manager
    void SetAccessTime(int nTime) { nMngAccessTime = nTime; }
    int GetAccessTime() { return nMngAccessTime; }

    // Segement processing
    void Segment(CTerrain *pTerrain, int nSegmentNumber);

    // Source render values: read-only; no animation/RNG advancement.
    void CopyRendererState(EIRenderTerrainSectorV5& row,std::vector<uint32_t>& phases) const;
    void CopyRendererSector(void* destination);
    // Information methods
    int GetMapX() { return nMapX; }
    int GetMapY() { return nMapY; }
    float GetAverageZ() { return (fpMinZ + fpMaxZ) * 0.5f; }
    BOOL IsHaveLayers() { return bHaveLayers; }

    // State management
    BOOL IsHaveState(ESectorState st) { return state & st; }
    BOOL SetState(ESectorState st, CTerrain *);
    void InvalidateState(ESectorState st) { state &= ~st; }
    void SetRelightDelay(int nNewRelightDelay);
    void RelightSegment();

    // Level of detail
    int GetLOD() { return nLOD; }
    void SetLOD(int nLOD); // LOD in range 0( max LOD ) .. 2( min LOD )

    // Geometry data
    SLVertex3D *GetLVertexPtr() { return lVertexes.GetData(); }
    SLVertex3D *GetLVertex(int nX, int nY) { return &lVertexes[nY * N_V_PSECTOR_S + nX]; }
    CPlot3D *GetVertex(int nX, int nY) { return &mesh[nY * N_V_PSECTOR_S + nX]; }
    CPlot3D *GetMeshPtr() { return mesh.GetData(); }
    const int *GetObjectIDs() { return nObjectIDs.GetData(); }
    const int *GetTileIndexes() { return nTileIndexes.GetData(); }
    int GetTilesCount(int nType)
    {
        ASSERT(state >= ESS_D3D);
        return tilesCounts[nType];
    }

    // Modifid state
    bool IsModifid() { return m_isModifid; }
    void SetModify(bool modify) { m_isModifid = modify; }

    // Tile operations
    void SetTile(int nX, int nY, int nRot, int nTile);
    void GetTile(int nX, int nY, int *nRot, int *nTile);
};

// ------------------------------ Implementation ---------------------------------------------

inline BOOL CSector::IsHaveMaterial(int nMat)
{
    return txVertexes[0][nMat].GetSize() | txVertexes[1][nMat].GetSize() |
           txVertexes[2][nMat].GetSize() | txVertexes[3][nMat].GetSize();
}
