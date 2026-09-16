#pragma once

/**********************************************************************
 *<
        FILE: mapFormat.H

        DESCRIPTION:	.MP file format description

        CREATED BY:		Ivanov Evgeny

        HISTORY:		started 2 July 1998

 *>	Copyright (c) 1998, Nival Enterntainemnt
 **********************************************************************/
// Includes
#include <stdio.h>
// Terrain object types constants
#pragma pack(1)
enum ETERR_OBJ_TYPE
{
    TOT_UNDEF = 0,           // Unidentified
    TOT_TERRAIN = 1,         // Base terrain type
    TOT_WATER_NOTEXTURE = 2, // Water without texture
    TOT_WATER = 3,           // Textured water
    TOT_GRASS = 4            // Textured grass
};
#pragma pack()

// Base constants
const int N_NUMBER_OF_LONG_TEXTURES = 2; // Number of long textures
const int N_TILE_IN_LONG_TEXTURES = 8;   // Number of tiles in long texture
const int N_NUMBER_OF_LONG_TILES =       // Number of long textures
    N_TILE_IN_LONG_TEXTURES * N_NUMBER_OF_LONG_TEXTURES;

const int N_CL_PSECTOR_S = 32;   // Number of cells per sector side
const float FP_CL_MP_SIZE = 1.f; // Cell size in .MP units
const int N_CL_PER_TILE_S = 2;   // Number of cells per texture side

// Calculated constants
const int N_TILE_IN_TEXTURE = 64; // Number of tile in texture ( material )

const int N_CL_PSECTOR = // Number if cells per sector
    N_CL_PSECTOR_S * N_CL_PSECTOR_S;
const int N_TILES_PSECTOR_S = // Number of tiles per sector side
    N_CL_PSECTOR_S / N_CL_PER_TILE_S;
const int N_TILES_PSECTOR = // Number of tiles per sector
    N_TILES_PSECTOR_S * N_TILES_PSECTOR_S;
const int N_V_PSECTOR_S = // Number of vertexes per sector side
    N_CL_PSECTOR_S + 1;
const int N_V_PSECTOR = // Number of vertexes per sector
    N_V_PSECTOR_S * N_V_PSECTOR_S;
const int N_CL_PER_TILE = // Number if cells per tile
    N_CL_PER_TILE_S * N_CL_PER_TILE_S;

#pragma pack(1)

// .MP file header structure
const LPCSTR PSZ_MAPFILE_EXT = ".MP";
const DWORD DW_SMAPHEADER_SIGNATURE = 0xce4af672;

struct SMapHeader
{
    DWORD dwSignature;     // Map file signature
    float fpMaxZ;          // Max Z value of the map
    DWORD dwXSectorsCount; // Numbers of sectors
    DWORD dwYSectorsCount;
    DWORD dwTextures;      // Number of textures in map
    DWORD dwTexSize;       // Size of the texture
    DWORD dwTiles;         // Number of tiles in map
    DWORD dwTileSize;      // Size of tile
    WORD wObjectsCount;    // Number of objects in the map
    DWORD dwAnimatedTiles; // Number of animated tiles indexes
    // SMapObject	objectArray[wObjectsCount]  // Array of objects descriptions
    // int          idArray[dwTiles]			// Terrian type ID's
    // STileAnimInfo animArray[ dwAnimatedTiles]// Tile animation info
};

const int N_MAPOBJECT_RESERVED_FIELDS = 3;
struct SMapObject
{
    ETERR_OBJ_TYPE eType; // Type of object
    float fpR, fpG, fpB;  // Diffuse color of object
    float fpOpacity;      // Opacity of object
    float fpSelfIllum;    // Self illumination of object
    float fpWaveMultiplier;
    float fpWarpSpeed;
    float fpReserved[N_MAPOBJECT_RESERVED_FIELDS];
};

struct STileAnimInfo
{
    WORD wAnimatedTileIndex; // Index of animated tile
    WORD wAnimationPhases;   // Number of animation phases
};
// TileID
typedef WORD TILE_ID;
const WORD TILE_UNDEF = 0xFFFF;

inline TILE_ID FormTileID(int nRot, int nTileInd)
{
    assert(nRot >= 0 && nRot <= 3);
    assert(nTileInd >= 0 && nTileInd <= 0x3FFF);
    return (nRot << 14) | nTileInd;
}
inline int GetTileRot(TILE_ID idTile)
{
    return idTile >> 14;
}
inline int GetTileInd(TILE_ID idTile)
{
    return idTile & 0x3FFF;
}

// .SEC file header structure
const LPCSTR PSZ_SECTORFILE_EXT = ".SEC";
const DWORD DW_SSECTORHEADER_SIGNATURE = 0xcf4bf773;
const DWORD DW_SSECTORHEADER_SIGNATURE_NORMALS = DW_SSECTORHEADER_SIGNATURE + 1;

const BYTE C_SECTORLAYERED = 0x1;     // Sector contains layers
const BYTE C_SECTORLAYER_TILED = 0x3; // Sector contains layers and layers contains tiles
// Sector header
struct SSectorHeader
{
    DWORD dwSignature; // Sector file signature
                       // Cells vertexes
    BYTE cFlags;       // Sector flags
                       // SSectVert cellVertexes[ N_CL_PSECTOR_S+1 ][ N_CL_PSECTOR_S+1 ];
    // or
    // SSectVert2 cellVertexes[ N_CL_PSECTOR_S+1 ][ N_CL_PSECTOR_S+1 ];
    // if normals already be built

    // if cFlags & C_SECTORLAYERED				// Layer vertexes
    // SSectVert cellVertexes[ N_CL_PSECTOR_S+1 ][ N_CL_PSECTOR_S+1 ];
    // or
    // SSectVert2 cellVertexes[ N_CL_PSECTOR_S+1 ][ N_CL_PSECTOR_S+1 ];

    // Cells tiles identifiers
    // TILE_ID		wTiles[ N_CL_PSECTOR_S/2 ][ N_CL_PSECTOR_S/2 ];

    // if cFlags & C_SECTORLAYER_TILED			// Layer tiles
    // TILE_ID		wTiles[ N_CL_PSECTOR_S/2 ][ N_CL_PSECTOR_S/2 ];
    // Object ID's for each tile
    // WORD wObjectID[ N_CL_PSECTOR_S/2 ][ N_CL_PSECTOR_S/2 ];
};
// Estimated sector size = 4880[*2] bytes;

typedef signed char SBYTE;
typedef unsigned short UINT16;
typedef unsigned int UINT32;
struct SSectVert
{
    SBYTE sbXOff, sbYOff; // offsets from regular cells positions
    UINT16 usZ;           // height of the vertex
};

struct SSectVert2
{
    SBYTE sbXOff, sbYOff; // offsets from regular cells positions
    UINT16 usZ;           // height of the vertex
    UINT32 uNormal;       // normal for the vertex
};

// -------------------------- Functions ------------------------------------------------------
const float FP_XFLOOR_EPS = 1e-5f;
inline int xfloor(float fpVar)
{
    int nRet = int(fpVar);
    if (fabs(float(nRet) - fpVar) > 1.f - FP_XFLOOR_EPS)
    {
        fpVar += fpVar * FP_XFLOOR_EPS;
        nRet = int(fpVar);
    }
    return nRet;
}

inline UINT32 GetNormal(float fpX, float fpY, float fpZ)
{
    assert(fpX >= -1.0f && fpX <= 1.0f);
    assert(fpY >= -1.0f && fpY <= 1.0f);
    assert(fpZ >= 0.0f && fpZ <= 1.0f);
    return (unsigned(xfloor(fpZ * 1000.f)) << 22) |
           (unsigned(xfloor(fpX * 1000.f + 1000.f)) << 11) |
           unsigned(xfloor(fpY * 1000.f + 1000.f));
}
inline void GetNormal(UINT32 uNormal, float &fpX, float &fpY, float &fpZ)
{
    fpZ = float(uNormal >> 22) / 1000.f;
    fpX = (float((uNormal >> 11) & 0x7FF) - 1000.f) / 1000.f;
    fpY = (float(uNormal & 0x7FF) - 1000.f) / 1000.f;
}
// Convert float coords to SSectVert offsets
inline SBYTE GetOffset(float fpActualCoord, float fpRegularCoord)
{
#ifdef _DEBUG
    int i = xfloor((fpActualCoord - fpRegularCoord) * float(0x7E) / (FP_CL_MP_SIZE / 2.f));
    assert(i >= -127 && i <= 127);
    return (SBYTE)i;
#else
    return SBYTE(xfloor((fpActualCoord - fpRegularCoord) * float(0x7E) / (FP_CL_MP_SIZE / 2.f)));
#endif
}
// Convert SSectVert offsets to actial float values
inline float GetCoord(SBYTE sbOffset, float fpRegularCoord)
{
    return float(sbOffset) * (FP_CL_MP_SIZE / 2.f) / float(0x7E) + fpRegularCoord;
}
// Convert float value between two another into 16bit
inline UINT16 GetUShortValueByRange(float fpActual, float fpMax)
{
    assert(fpActual >= -1.e-5f);
    assert(fpMax - fpActual >= -1.e-5f);
    return UINT16(xfloor(fpActual * float(0xFFFE) / fpMax));
}
// Convert 16bit value between to float values into float
inline float GetFloatValueByRange(UINT16 us, float fpMax)
{
    return float(us) * fpMax / float(0xFFFE);
}
// Get sector [i,j] file name
inline void GetSectorName(int i, int j, char *pszBaseName, char *pszSectName)
{
    sprintf(pszSectName, "%s%3.3i%3.3i%s", pszBaseName, i, j, PSZ_SECTORFILE_EXT);
}

#pragma pack()
