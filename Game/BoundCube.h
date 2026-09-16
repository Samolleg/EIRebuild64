#pragma once
/**********************************************************************
 *<
        FILE: BoundCube.H

        DESCRIPTION:  CBoundCube class header

        CREATED BY: Ivanov Evgeny

 *>	Copyright (c) 1998, Nival Enterntainemnt
 **********************************************************************/

#include "Plot.h"
#include "Quaternion.h"

struct SLVertex3D;

#define DECLARE_PLANE_EQ_CF(nmb) float fpA##nmb, fpB##nmb, fpC##nmb, fpD##nmb;

class CBoundCube
{
private:
    DECLARE_PLANE_EQ_CF(0);
    DECLARE_PLANE_EQ_CF(1);
    DECLARE_PLANE_EQ_CF(2);
    DECLARE_PLANE_EQ_CF(3);
    DECLARE_PLANE_EQ_CF(4);
    DECLARE_PLANE_EQ_CF(5);

public:
    CBox boundbox;

public:
    CBoundCube() = default;
    CBoundCube(CBox *pBox, CQuaternion *pQuat, CPlot3D *pCp) { Recalc(pBox, pQuat, pCp); }
    // Recalculate bounding cube variables with given bounding box and rotation
    void Recalc(CBox *pBox, CQuaternion *pQuat, CPlot3D *pCp);
    // Check for vertex is inside bounding cube
    BOOL IsInside(CPlot3D *pVertex) { return CheckInside(pVertex->x, pVertex->y, pVertex->z); }

private:
    BOOL CheckInside(float fpX, float fpY, float fpZ);
};
