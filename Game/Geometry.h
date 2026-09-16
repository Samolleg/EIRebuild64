#pragma once

/**********************************************************************
 *<
        FILE: Geometry.H

        DESCRIPTION:  3D geometry classes header

        CREATED BY: Ivanov Evgeny

 *>	Copyright (c) 1998, Nival Enterntainemnt
 **********************************************************************/

#include "Light.h"
#include "PtrList.h"
#include "Plot.h"

// ------------------------------ Constants --------------------------------------------------
// Maximim number of vertexes to send in rasteriser per single DrawPrimitive
const int N_HARDWARE_V_MAX = 256;
// ---------------------------- Common defines -----------------------------------------------
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
// -------------------------- Classes declarations -------------------------------------------

struct SVector3D;
struct SLVertex3D;
class CTerrain;

// -------------------------- Functions declarations -----------------------------------------
/*template <class T> inline void swap( T& arg1, T& arg2 );*/
// Calculates the Z value for point at given plane
float GetZValue(float fpX, float fpY, CPlot3D *pV1, CPlot3D *pV2, CPlot3D *pV3);
// ----------------------------- 3D L Vertex class -------------------------------------------

typedef CSimplyPtrList<CLight *> CLightPtrList;

#pragma pack(1)

struct SLVertex3D
{
    float sx, sy, sz, rhw; // transformed coordinates
    BYTE clip;             // clipping information
    DWORD dwColor;         // Vertex color
    DWORD dwSpecular;      // Specular color of the vertex

    CPlot3D baseVertex; // Base coordinates( for water, grass etc )
    WORD wObjectID;     // Map object for this vertex
    float fpDepth;      // Depth of the base vertex

    CPlot3D vNorm;             // Normal to vertex
    float baseR, baseG, baseB; // Base vertex color
    float emmR, emmG, emmB;    // Emissive vertex color
    BYTE cAlpha;               // Alpha component for this vertex

    CLightPtrList lightsList; // List of lights that can affect this vertex
};

typedef CArray<SLVertex3D *, SLVertex3D *> CLVertexPtrArray;
typedef CArray<CPlot3D *, CPlot3D *> CPlot3DPtrArray;

#pragma pack()
// -------------------- Inline functions implementations -------------------------------------
/*
template <class T> inline void swap( T& arg1, T& arg2 )
{
        T temp;
        temp = arg1;
        arg1 = arg2;
        arg2 = temp;
}*/
