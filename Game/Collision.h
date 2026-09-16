#pragma once
/**********************************************************************
 *<
        FILE: Collision.H

        DESCRIPTION:  header file for collision detection functons

        CREATED BY: Mikhail Semyonov

 *>	Copyright (c) 1998, Nival Enterntainemnt
 **********************************************************************/

#include "plot.h"
#include "quaternion.h"

///////////////////////////////////////////////////////////////////
// functions declared in Collision.cpp

// Detects the collision of two ortogonal parallelepipeds P0 and P1
BOOL DetectCollisionBoxes(CPlot3D ptCenter0, CPlot3D ptCenter1, const SBoundingVolume *pBox0,
                          const SBoundingVolume *pBox1, CQuaternion *pQuaBox0,
                          CQuaternion *pQuaBox1);

// Detects if two speres intersect
BOOL DetectCollisionSphres(CPlot3D ptCenter0, CPlot3D ptCenter1, D3DVALUE fpRadius0,
                           D3DVALUE fpRadius1);

// Detects if parallelepiped and interval do intersect
BOOL DetectCollisionBoxInterval(CPlot3D ptBoxCenter, const SBoundingVolume *pBox,
                                CQuaternion *pQuaBox, CPlot3D ptIntlCenter, CPlot3D ptIntlVector);

// Function solves the equation D = x0*A0 + x1*A1 + x2*A2,
//					where Ai - vectors, xi - scalars
CPlot3D ExcludeVector3D(const CPlot3D *, const CPlot3D *, const CPlot3D *, const CPlot3D *);

// Calculates square of distance between plot and interval
D3DVALUE DistancePlotIntervalSquare(const CPlot3D ptPlot, const CPlot3D ptIntA,
                                    const CPlot3D ptIntlB);

// Calculates square of distance between two plots
D3DVALUE DistancePlotPlotSquare(const CPlot3D ptA, const CPlot3D ptB);
