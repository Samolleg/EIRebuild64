/**********************************************************************************************
 *<
        FILE: Geometry.Cpp

        DESCRIPTION:  3D geometry classes implementation

        CREATED BY: Ivanov Evgeny

 *>	Copyright (c) 1998, Nival Enterntainemnt
 *********************************************************************************************/
#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "mapFormat.h"
#include "Geometry.h"
#include "BoundCube.h"

// ----------------------------- Get Z Value -----------------------------------------
float GetZValue(float fpX, float fpY, CPlot3D *pV1, CPlot3D *pV2, CPlot3D *pV3)
{
    float fpE1 = pV2->x - pV1->x;
    float fpE2 = pV2->y - pV1->y;
    float fpE4 = pV3->x - pV1->x;
    float fpE5 = pV3->y - pV1->y;

    float fpC = fpE1 * fpE5 - fpE2 * fpE4;
    if (fabs(fpC) < 1e-2f)
        return (pV1->z + pV2->z + pV3->z) / 3.f;

    float fpE3 = pV2->z - pV1->z;
    float fpE6 = pV3->z - pV1->z;

    float fpA = fpE2 * fpE6 - fpE3 * fpE5;
    float fpB = fpE3 * fpE4 - fpE1 * fpE6;

    float fpD = -(fpA * pV1->x + fpB * pV1->y + fpC * pV1->z);

    return -(fpA * fpX + fpB * fpY + fpD) / fpC;
}

// ----------------------------- BoundCube methods -----------------------------------------

#define GETPLOT(Arg1, Arg2, Arg3) \
    pQuat->Rotate(CPlot3D(pBox->##Arg1, pBox->##Arg2, pBox->##Arg3)) + *pCp;

#define PLANE_EQ(nmb, P1, P2, P3)         \
    fpE1 = P2.x - P1.x;                   \
    fpE2 = P2.y - P1.y;                   \
    fpE3 = P2.z - P1.z;                   \
    fpE4 = P3.x - P1.x;                   \
    fpE5 = P3.y - P1.y;                   \
    fpE6 = P3.z - P1.z;                   \
                                          \
    fpA##nmb = fpE2 * fpE6 - fpE3 * fpE5; \
    fpB##nmb = fpE3 * fpE4 - fpE1 * fpE6; \
    fpC##nmb = fpE1 * fpE5 - fpE2 * fpE4; \
    fpD##nmb = -(fpA##nmb * P1.x + fpB##nmb * P1.y + fpC##nmb * P1.z);

#define CHECK_COORD(pt)        \
    if (pt.x < boundbox.tln.x) \
        boundbox.tln.x = pt.x; \
    if (pt.y < boundbox.tln.y) \
        boundbox.tln.y = pt.y; \
    if (pt.z < boundbox.tln.z) \
        boundbox.tln.z = pt.z; \
    if (pt.x > boundbox.brf.x) \
        boundbox.brf.x = pt.x; \
    if (pt.y > boundbox.brf.y) \
        boundbox.brf.y = pt.y; \
    if (pt.z > boundbox.brf.z) \
        boundbox.brf.z = pt.z;

// Recalculate bounding cube variables with given bounding box and rotation
void CBoundCube::Recalc(CBox *pBox, CQuaternion *pQuat, CPlot3D *pCp)
{
    CPlot3D p0 = GETPLOT(tln.x, tln.y, tln.z);
    CPlot3D p1 = GETPLOT(tln.x, brf.y, tln.z);
    CPlot3D p2 = GETPLOT(brf.x, brf.y, tln.z);
    CPlot3D p4 = GETPLOT(tln.x, tln.y, brf.z);
    CPlot3D p6 = GETPLOT(brf.x, brf.y, brf.z);
    CPlot3D p7 = GETPLOT(brf.x, tln.y, brf.z);

    float fpE1, fpE2, fpE3, fpE4, fpE5, fpE6;

    PLANE_EQ(0, p0, p1, p2);
    PLANE_EQ(1, p0, p4, p1);
    PLANE_EQ(2, p0, p7, p4);
    PLANE_EQ(3, p6, p2, p1);
    PLANE_EQ(4, p6, p7, p2);
    PLANE_EQ(5, p6, p4, p7);

    // Get bbox
    boundbox.tln = p0;
    boundbox.brf = p0;
    CHECK_COORD(p1);
    CHECK_COORD(p2);
    CHECK_COORD(p4);
    CHECK_COORD(p6);
    CHECK_COORD(p7);
}

#define CHECK_PLANE(nmb)                                                   \
    if (fpA##nmb * fpX + fpB##nmb * fpY + fpC##nmb * fpZ + fpD##nmb > 0.f) \
        return FALSE;

// Check for vertex is inside bounding cube
BOOL CBoundCube::CheckInside(float fpX, float fpY, float fpZ)
{
    // Check z aligned planes in first
    CHECK_PLANE(5);
    CHECK_PLANE(0);
    // Check other planes
    CHECK_PLANE(1);
    CHECK_PLANE(2);
    CHECK_PLANE(3);
    CHECK_PLANE(4);
    return TRUE;
}

// ------------------------- Static variables for PtrList.h
// ------------------------------------------------
struct SSimplyPtrNodeBlock;
SSimplyPtrNodeBlock *pFreeBlocks = NULL;
SSimplyPtrNodeBlock *pUsedBlocks = NULL;
