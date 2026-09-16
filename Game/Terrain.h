#pragma once
class ModTerrainShadow;
/**********************************************************************
 *<
        FILE: Tarrain.H

        DESCRIPTION:  Tarrain manager header

        CREATED BY: Ivanov Evgeny

 *>	Copyright (c) 1998, Nival Enterntainemnt
 **********************************************************************/
#include "Base3D.h"
#include "Sector.h"
#include "mapFormat.h"
#include "SoundTypes.h"
#include "ServerConsts.h"

class CMaterial;
class CTexture;
class CShadowBuilder;

// Number of looping ambient sources
const int N_LASRC_NUM = 3;
// const int N_LASRC_NUM = 1;
// Fog width
const float FP_BORDER_ALPHA_FADE = 32.0f;

// ------------------------------ Terrain types -----------------------------------------------

enum ETERRTYPE
{
    TERRTYPE_GRASS = 0,
    TERRTYPE_GROUND = 1,
    TERRTYPE_STONE = 2,
    TERRTYPE_SAND = 3,
    TERRTYPE_ROCK = 4,
    TERRTYPE_FIELD = 5,
    TERRTYPE_WATER = 6,
    TERRTYPE_ROAD = 7,
    TERRTYPE_SNOW = 9,
    TERRTYPE_ICE = 10,
    TERRTYPE_DRYGRASS = 11,
    TERRTYPE_SNOWBALLS = 12,
    TERRTYPE_LAVA = 13,
    TERRTYPE_SWAMP = 14,
    TERRTYPE_UNDEFINED = 8,
    TERRTYPE_HIGHROCK = 15,

    TERRTYPE_LAST = 16
};

// ------------------------- Object find info structure ---------------------------------------

struct SObjFindInfo
{
    int nPosNum;
    int positions[4];
};

struct SSectorShiftInfo
{
    CArray<float, float> aObjShift;
};
typedef CArray<SSectorShiftInfo, SSectorShiftInfo &> CSectorShiftInfoArray;

// ------------------------- Vertex info structure --------------------------------------------
struct SVertexInfo
{
public:
    int nSectorX;
    int nSectorY;
    int nLocalX;
    int nLocalY;

public:
    inline void FillFromGlobal(int nX, int nY);
    inline SVertexInfo &operator=(const SVertexInfo &vertex);
    int GetOffset() { return nLocalY * (N_CL_PSECTOR_S + 1) + nLocalX; }
    void Clear() { nSectorX = nSectorY = nLocalX = nLocalY = 0; }
    bool Check();

public:
    SVertexInfo(int nX, int nY) { FillFromGlobal(nX, nY); }
    SVertexInfo() { Clear(); }
};

inline bool SVertexInfo::Check()
{
    return nSectorX >= 0 && nSectorY >= 0 && nLocalX >= 0 && nLocalY >= 0 && nLocalY <= 32 &&
           nLocalX <= 32;
}

inline SVertexInfo &SVertexInfo::operator=(const SVertexInfo &vertex)
{
    nSectorX = vertex.nSectorX;
    nSectorY = vertex.nSectorY;
    nLocalX = vertex.nLocalX;
    nLocalY = vertex.nLocalY;
    return *this;
}

inline void SVertexInfo::FillFromGlobal(int nX, int nY)
{
    nSectorX = nX / N_CL_PSECTOR_S;
    nSectorY = nY / N_CL_PSECTOR_S;
    nLocalX = nX - nSectorX * N_CL_PSECTOR_S;
    if (!nLocalX && nSectorX)
    {
        --nSectorX;
        nLocalX = N_CL_PSECTOR_S;
    }
    nLocalY = nY - nSectorY * N_CL_PSECTOR_S;
    if (!nLocalY && nSectorY)
    {
        --nSectorY;
        nLocalY = N_CL_PSECTOR_S;
    }
}

typedef CArray<SVertexInfo *, SVertexInfo *> CVertexInfoPtrArray;
typedef CArray<SVertexInfo, SVertexInfo &> CVertexInfoArray;

// ------------------------- Light information structure --------------------------------------
struct SLightInfo
{
    CLight *pLight;
    SSectorRect affectedRect;

    SLightInfo() = default;
    SLightInfo(const int) { pLight = NULL; } // dummy constructor for HashMap
};
// ----------------------------- Terrain object -----------------------------------------------
class CTerrainObject
{
public:
    ETERR_OBJ_TYPE eType;    // Type of object
    float fpR, fpG, fpB;     // Diffuse color of object ( 0..1 )
    float fpOpacity;         // Opacity of object ( 0..1 )
    float fpTransparencyInv; // 1 / Transparency of object( 0..1 )

    BYTE cAlpha;               // Integer alpha value for current opacity
    D3DCOLORVALUE dcvEmissive; // Emissive color of object

    float fpWindMultiplier; // Multiplier for wind amplitude ( 0..1 )
    float fpWarpSpeed;      // Speed of warping

    float fGlobalShift; // Смещение относительно файла террейна

    void Set(SMapObject &);
};

// ------------------------------ Terrain -----------------------------------------------------
typedef unsigned ETerrRenderState;
class CTerrain : public CBase3D
{
public:
    enum
    {
        ETRS_NO = 0x0000,             // No any special effects
        ETRS_WAVES = 0x0001,          // Water or grass waves is enabled
        ETRS_ANIMATED_TILES = 0x0002, // Animate tiles
        ETRS_BORDER_FOG = 0x0004,     // Set fog to map borders
        ETRS_AMBIENT_SOUNDS = 0x0008, // Create ambient sounds
        ETRS_TRACE_SHADOWS = 0x0010,  // Build terrain shadows

        ETRS_FULL = 0xFFFF // All render options are enabled
    };

private:
    ETerrRenderState renderState;                     // render options
    D3DMATRIX d3dmatrix;                              // D3D matrix for IDirect3D transformation
    CSimplyArray<CTexture *, CTexture *> textures;    // Array of textures
    CSimplyArray<CMaterial *, CMaterial *> materials; // Array of materials

    CString szBaseName;           // Base name of the map
    CString szBaseNameFull;       // Base name with path
    BOOL bIsLoaded;               // Is map loaded?
    int nXSectors, nYSectors;     // Number of sectors in the map
    float fpMaxX, fpMaxY, fpMaxZ; // Max values of the map
    int nTexSize;                 // Size of textures
    int nTextures;                // Number of textures in map
    int nTileSize;                // Size of tiles
    int nTilesPerTexSide;         // Tiles per texture side
    int nTilesPerTex;             // Tiles per texture

    // Objects
    CSimplyArray<CTerrainObject, CTerrainObject &> objects;

    // Tile info ID
    CIntArray tileIDs;
    CIntArray
        tileAnimationsPhases; // number of animation phases for each tile, 0 if have no animation

    // Resource file
    CWin32MappedResource mapRes;

    // Precalulated tables
    SUVInfo *uvCoords[4];     // mapping coordinates of tiles
    SUVInfo *uvLongCoords[4]; // mapping coordinates of long tiles
    CSimplyArray<SObjFindInfo, SObjFindInfo &>
        objFindInfo; // for finding object for geometry vertex

    // Sector manager
    POSITION **sectorPos;
    CList<CSector *, CSector *> sectorLRU;
    int nSegmentCount;

    // Light calculation information
    std::shared_ptr<ModTerrainShadow> modTerrainShadow;
    int nSegmentsToRelight;
    int nCurSegmentsToRelight;
    D3DCOLORVALUE dcvAmbient; // Ambient color
    float fpCA, fpAC;         // Line equation coeffs
    float fpCB, fpBC;
    int nAddCellsX, nAddCellsY; // Number of cells to calculate fake sun projection
    CSector *pSector;
    int nVXIndex, nVYIndex;
    CLight *pSunLight; // pointer to sun
    CHashMap<int, SLightInfo> lightInfo;

    // Wind calculation information
    CPlot3D windDirection;
    float fpWindForce, fpWindForce01; // fpWindForce01 in range 0..1
    float fpWindPhase, fpWindPhaseAdd;
    CFloatArray fpNoises;
    float sineTable[512];

    // LOD
    int nMaxLOD;

    // Sectors clipping	// Видимые сектора
    int nClipMinSectorY;
    CIntArray viewableSectorsMinX, viewableSectorsMaxX;

    // Ambient sounds
    HSOUND hSndAmbient[N_LASRC_NUM];
    int nAmbientTerrainType[N_LASRC_NUM];
    CString szAllodDependentSndDir;
    //
    int nLongTile; // Number of long Tile 0 or 1 or 2 NOT GREAT!!!

public:
    //
    bool LongTilePresent() { return nLongTile > 0; }

    // Layer shifting
    CSectorShiftInfoArray m_aSectorShifts;

    // Constr/desctr
    CTerrain() { Clear(); }
    ~CTerrain() { Destroy(); }
    // CGameBase methods
    LPCSTR GetGameClassName() const { return "CTerrain"; }
    EClassIDs WhoAmI() const { return HPC_TERRAIN; }
    // CBase3D methods
    BOOL Draw(CRenderer *lpScene);
    BOOL DrawBase(CRenderer *lpScene);
    void DrawModTerrainShadow(CRenderer *pRend);
    BOOL DrawTransparent(CRenderer *lpScene);

    // 3D engine mehods
    void PrepareForDraw(const CQuaternion &q);
    void PrepareD3DRotateMatrix(const CQuaternion &q);
    virtual BOOL Create();
    virtual void Destroy();

    // Map management methods
    BOOL Load(LPCSTR pszFileName, LPCSTR pszAllodName, bool bCave);
    void Save();
    void Free();
    BOOL IsLoaded() const { return bIsLoaded; }
    void ClearSectorsCache();

    // Fixing
    void FixHoles();
    SSectVert2 *GetSectorGeometryData(int nX, int nY);

    // World settings
    void SetWind(CPlot3D *pWindDirection, float fpWindForce /* 0..1 */);
    void SetWindForce(float fpForce);
    float GetWindForce() { return fpWindForce01; }
    const CPlot3D &GetWind() { return windDirection; }

    // Lights
    void SetSun(CLight *pLight, D3DCOLORVALUE &dcvAmbient);
    void AddLight(CLight *pLight);
    void RemoveLight(CLight *pLight);
    void MoveLight(CLight *pLight, CPlot3D *pDirection);
    void SetAutoRelightDelay(int nSegmentsNumber = -1);

    D3DCOLORVALUE &GetAmbient() { return dcvAmbient; }
    CLight *GetSunLight() { return pSunLight; }
    void UpdateLightsForSector(CSector *);

    // Level of detail - 0(max detail) .. 2(min detail)
    void SetMaxLOD(int nLOD);
    int GetMaxLOD() { return nMaxLOD; }

    // Render state
    void SetRenderState(ETerrRenderState rS) { renderState |= rS; }
    void RemoveRenderState(ETerrRenderState rS) { renderState &= ~rS; }
    BOOL IsHaveRenderState(ETerrRenderState rS) const { return (renderState & rS) == rS; }
    void EnableAmbientSounds() { SetRenderState(ETRS_AMBIENT_SOUNDS); }
    void DisableAmbientSounds();

    // Information
    float GetZValue(float fpX, float fpY, BOOL bFromLayer = FALSE);
    void GetZValue(float fpX, float fpY, float *pZTerrain, float *pDepth = NULL);
    ETERRTYPE GetTerrainType(float fpX, float fpY);
    ETERRTYPE GetTerrainType(int nTileIndex)
    {
        return (tileIDs[nTileIndex] < nTextures * nTilesPerTex) ? (ETERRTYPE)tileIDs[nTileIndex]
                                                                : TERRTYPE_HIGHROCK;
    }
    void GetProjection(CXYZW4Array &coords); // Get z values for each SXYZW from base terrain level

    CPlot3D GetSize() const { return CPlot3D(fpMaxX, fpMaxY, fpMaxZ); }
    float GetMaxZ() const { return fpMaxZ; }
    float GetMinZ() const { return 0.f; }
    float GetMaxX() const { return fpMaxX; }
    float GetMaxY() const { return fpMaxY; }
    int GetXSectors() const { return nXSectors; }
    int GetYSectors() const { return nYSectors; }
    int GetSectorsNumber() const { return nXSectors * nYSectors; }
    int GetObjectsNumber() const { return objects.GetSize(); }

    float GetAverageZ() const { return fpMaxZ / 2.f; }
    D3DMATRIX &GetTransformationMatrix() { return d3dmatrix; }
    int GetTileNumbers();
    CRect GetTileRect(int nTile);
    int GetShortTextureNumbers() const { return nTextures; }
    const CString &GetBaseName() const { return szBaseName; }
    // Segement processing
    virtual int Segment();

    // Draw support methods
    SUVInfo *GetUVInfo(
        int nRot, int nTileInd,
        int nMat = 0); // nMat for detect long tile, if tile is not long don't set this argument
    int GetTileAnimationPhases(int nTileInd)
    {
        return (nTileInd < nTilesPerTex * nTextures) ? tileAnimationsPhases[nTileInd] : 0;
    }
    SObjFindInfo *GetObjFindInfo(int nGeomVertPos) { return &objFindInfo[nGeomVertPos]; }
    CTerrainObject *GetObject(int nObjIndex) { return &objects[nObjIndex]; }
    BYTE GetObjectAlpha(int nObjIndex) { return objects[nObjIndex].cAlpha; }
    void GetWindInfo(CPlot3D *pWindDirection, float *pWindForce, float *pWindPhase);
    const float *GetWindNoiseInfo() { return fpNoises.GetData(); }
    float FastSine(float fpVar);

    // Set clip area, pClipPoints array must have size of 4 and be arranged to form convex figure
    void SetClip(CPlot2D *pClipPoints);
    void SetClip(float fpX, float fpY, float fpRadius);
    void SetClip(float fpX1, float fpY1, float fpRadius1, float fpX2, float fpY2, float fpRadius2);

    // Ray trace methods
    void SetSector(CSector *pS) { pSector = pS; }
    void SetVXIndex(int nVXInd) { nVXIndex = nVXInd; }
    void SetVYIndex(int nVYInd) { nVYIndex = nVYInd; }
    BOOL TraceToSun(CPlot3D *pFrom);

    BOOL TraceRayForVisibleTerrain(const CPlot3D *pPtStart, const CQuaternion *pDirection,
                                   const float fpMaxRayLen, CPlot3D *pResult);

    // Shadow meshes
    void BuildShadowMesh(CShadowBuilder &builder, CXYZW4Array &src);

    // Sector access
    CSector *GetSector(int nX, int nY, ESectorState eReqState, BOOL bTryToSetState = TRUE);

    // Vertex access
    CPlot3D *GetVertex(int nX, int nY, BOOL bFromLayer = FALSE);
    void SetVertex(int nX, int nY, CPlot3D vertexToSet);
    void GetDupleLVertex(int nX, int nY, CLVertexPtrArray &aVertexs);
    void GetDupleVertex(int nX, int nY, CPlot3DPtrArray &aVertexs);
    void GetDupleVertexInfo(int nX, int nY, CVertexInfoArray &aVertexs);

    // Terraforming
    void TerraformVertex(CPlot3D ptPoint, float zIncrement); // In ptPoint actual only x & y
    void
    TerraformVertexHieght(CPlot3D ptPoint); // In ptPoint actual only x & y, z означает новую высоту
    void TerraformVertex(int nX, int nY, float zIncrement, bool needCalcNormal = true,
                         bool isAbsoluteHeight = false);

    // Normal building
    void CalculateNormals(int nX, int nY); // calculate normals for vertex in ( nX, nY )
    void AddToNormal(CPlot3D *pNorm, CPlot3D *p1, CPlot3D *p2, CPlot3D *p3);

    // Tile modification
    void SetTile(CPlot3D ptToSet, int nRot, int nTile); // Interface function
    void SetTile(int nX, int nY, int nRot, int nTile, bool needUpdate = true);
    void GetTile(CPlot3D ptToSet, int *pnRot, int *pnTile);
    void GetTile(int nX, int nY, int *pnRot, int *pnTile);

    // Water
    void SetObjectHieght(int nObjID, float fObjHieghtInc);
    // void SetObjectHieght( int nObjID, float fObjHieghtInc, float fSec ){ SetObjectHieght( nObjID,
    // fObjHieghtInc, int(fSec*float(DW_SEGMENT_TIME)) ); }

    // Fog
    BYTE GetFogAlpha(const CPlot3D &plot) const;

private:
    // Initialization
    void Clear();
    void CreateUVInfo(SUVInfo &uvInfo, int nRot, int x, int y, float fpUVTileSize, float fpUVMargin,
                      bool isLong = false);
    void CreateObjectFindInfo();
    void CreateWindNoise();
    void CreateSineTable();

    // It's clear enough :)
    BOOL Draw(CRenderer *lpScene, int nMatFrom, int nMapTo);

    // Texures
    BOOL CreateTextures(int nTexCount);
    void ReleaseTextures();

    // Sector manager
    void SetMapSize(int _nMapSX, int _nMapSY);
    void ClearMapSize();
    void SetLODForSector(CSector *);

    // Lights
    void GetAffectedRegion(CLight *pLight, CPlot3D *pMoveVector, SSectorRect *pRectAffect);
    void RelightRegion(SSectorRect *pRect);
    void UpdateLightForSector(CSector *pSector, CLight *pLight, const CPlot3D &cp,
                              SSectorRect *pRect, BOOL bIsRemove);
    void UpdateLight(CLight *pLight, CPlot3D *pMoveVector, SSectorRect *pRect, BOOL bIsRemove);

    // Ambient sounds
    void ProcessAmbientSounds();
    void PrepareAmbientSounds();
};

// ------------------------ Inline functions implementation ----------------------------------
inline void CTerrain::GetWindInfo(CPlot3D *pWindDirection, float *pWindForce, float *pWindPhase)
{
    *pWindDirection = windDirection;
    *pWindForce = fpWindForce;
    *pWindPhase = fpWindPhase;
}

inline float CTerrain::FastSine(float fpVar)
{
    int nIndex = Float2Int(float(fpVar * (512 / 2 / pi)));
    return sineTable[nIndex & 511];
}
