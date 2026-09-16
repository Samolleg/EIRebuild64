/**********************************************************************
 *<
        FILE: Collision.cpp

        DESCRIPTION:  Collision Detection procedures

        CREATED BY: Mikhail Semyonov

 *>	Copyright (c) 1998, Nival Enterntainemnt
 **********************************************************************/

#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "collision.h"

///////////////////////////////////////////////////////////////////
// FUNCTION:	DetectCollisionBoxes
// DESCRIPTION:	Detects the collision of two ortogonal parallelepipeds
//					P0 and P1
// PARAMETERS:  ptCenter0, ptCenter1 - centers of boxes,
//					pBox0, pBox1 - this boxes objects (extents are there)
//					pQuaBox0, pQuaBox1 - quaternions of boxes
// RETURNS:	    TRUE if parallelepipeds do intersect, otherwise FALSE
///////////////////////////////////////////////////////////////////
BOOL DetectCollisionBoxes(CPlot3D ptCenter0, CPlot3D ptCenter1, const SBoundingVolume *pBox0,
                          const SBoundingVolume *pBox1, CQuaternion *pQuaBox0,
                          CQuaternion *pQuaBox1)
{
    // 1) Calculate d, d'
    // 2) Calculate {cij}
    // 3) Procede tests

    // A - matrix of quat. axes of P0
    // B - matrix of quat. axes of P1
    // C0 - Plot3D - centrum of P0
    // C1 - Plot3D - centrum of P1
    // D = C1 - C0

    // *-*-*-*-*-*-*-* Calculate everything for tests *-*-*-*-*-*-*-*

    // Axises of parallelepipeds
    CPlot3D ptA0 = pQuaBox0->RotateX(), ptA1 = pQuaBox0->RotateY(), ptA2 = pQuaBox0->RotateZ(),
            ptB0 = pQuaBox1->RotateX(), ptB1 = pQuaBox1->RotateY(), ptB2 = pQuaBox1->RotateZ();

    // Calculate extents of boxes
    // ptMax - gets only positive values
    D3DVALUE
    a0 = pBox0->ptMax.GetX(), // extents of P0
        a1 = pBox0->ptMax.GetY(), a2 = pBox0->ptMax.GetZ(),
    b0 = pBox1->ptMax.GetX(), // extents of P1
        b1 = pBox1->ptMax.GetY(), b2 = pBox1->ptMax.GetZ();

    // calculate D = C1 - C0
    CPlot3D ptD = ptCenter1 - ptCenter0;

    // d0: 	D = d0*A
    // d1:  D = d1*B
    // calculate d0, d1
    CPlot3D pt_dA = ExcludeVector3D(&ptA0, &ptA1, &ptA2, &ptD),
            pt_dB = ExcludeVector3D(&ptB0, &ptB1, &ptB2, &ptD);

    D3DVALUE
    dA0 = pt_dA.GetX(), dA1 = pt_dA.GetY(), dA2 = pt_dA.GetZ(), dB0 = pt_dB.GetX(),
    dB1 = pt_dB.GetY(), dB2 = pt_dB.GetZ();

    // calculate {cij} : Bi = ci*A
    CPlot3D ptC0 = ExcludeVector3D(&ptA0, &ptA1, &ptA2, &ptB0), // c00, c10, c20
        ptC1 = ExcludeVector3D(&ptA0, &ptA1, &ptA2, &ptB1),     // c01, c11, c21
        ptC2 = ExcludeVector3D(&ptA0, &ptA1, &ptA2, &ptB2);     // c02, c12, c22

    D3DVALUE
    c00 = ptC0.GetX(), c10 = ptC0.GetY(), c20 = ptC0.GetZ(), c01 = ptC1.GetX(), c11 = ptC1.GetY(),
    c21 = ptC1.GetZ(), c02 = ptC2.GetX(), c12 = ptC2.GetY(), c22 = ptC2.GetZ();

    // *-*-*-*-*-*-*-* Tests *-*-*-*-*-*-*-*

    if (
        // six face tests
        (a0 + fabs(b0 * c00) + fabs(b1 * c01) + fabs(b2 * c02) < fabs(dA0)) ||
        (a1 + fabs(b0 * c10) + fabs(b1 * c11) + fabs(b2 * c12) < fabs(dA1)) ||
        (a2 + fabs(b0 * c20) + fabs(b1 * c21) + fabs(b2 * c22) < fabs(dA2)) ||

        (b0 + fabs(a0 * c00) + fabs(a1 * c10) + fabs(a2 * c20) < fabs(dB0)) ||
        (b1 + fabs(a0 * c01) + fabs(a1 * c11) + fabs(a2 * c21) < fabs(dB1)) ||
        (b2 + fabs(a0 * c02) + fabs(a1 * c12) + fabs(a2 * c22) < fabs(dB2)) ||

        // nine edge tests
        (fabs(a1 * c20) + fabs(a2 * c10) + fabs(b1 * c02) + fabs(b2 * c01) <
         fabs(dA2 * c10 - dA1 * c20)) ||
        (fabs(a1 * c21) + fabs(a2 * c11) + fabs(b0 * c02) + fabs(b2 * c00) <
         fabs(dA2 * c11 - dA1 * c21)) ||
        (fabs(a1 * c22) + fabs(a2 * c12) + fabs(b0 * c01) + fabs(b1 * c00) <
         fabs(dA2 * c12 - dA1 * c22)) ||

        (fabs(a0 * c20) + fabs(a2 * c00) + fabs(b1 * c12) + fabs(b2 * c11) <
         fabs(dA0 * c20 - dA2 * c00)) ||
        (fabs(a0 * c21) + fabs(a2 * c01) + fabs(b0 * c12) + fabs(b2 * c10) <
         fabs(dA0 * c21 - dA2 * c01)) ||
        (fabs(a0 * c22) + fabs(a2 * c02) + fabs(b0 * c11) + fabs(b1 * c10) <
         fabs(dA0 * c22 - dA2 * c02)) ||

        (fabs(a0 * c10) + fabs(a1 * c00) + fabs(b1 * c22) + fabs(b2 * c21) <
         fabs(dA1 * c00 - dA2 * c10)) ||
        (fabs(a0 * c11) + fabs(a1 * c01) + fabs(b0 * c22) + fabs(b2 * c20) <
         fabs(dA1 * c01 - dA2 * c11)) ||
        (fabs(a0 * c12) + fabs(a1 * c02) + fabs(b0 * c21) + fabs(b1 * c20) <
         fabs(dA1 * c02 - dA2 * c12)))
    {
        // If any of nonintersection tests is true then the parallelepipeds do not
        // intersect

        return FALSE; // do not intersect
    }

    // If all 15 tests are false then the parallelepipeds do intersect
    return TRUE;
}

///////////////////////////////////////////////////////////////////
// FUNCTION:	DetectCollisionSpheres
// DESCRIPTION:	Detects if two speres intersect
// PARAMETERS:	ptCenter0, ptCenter1 - centers of two spheres
//					fpRadius0, fpRadius1 - radiuses of two spheres
// RETURNS:		TRUE if spheres do intersect, FALSE otherwise
///////////////////////////////////////////////////////////////////
BOOL DetectCollisionSpheres(CPlot3D ptCenter0, CPlot3D ptCenter1, D3DVALUE fpRadius0,
                            D3DVALUE fpRadius1)
{
    if (fabs(ptCenter1 - ptCenter0) < fpRadius1 + fpRadius0)
        return TRUE; // spheres do intersect

    return FALSE; // spheres do not intersect
}

///////////////////////////////////////////////////////////////////
// FUNCTION:	DetectCollisionBoxInterval
// DESCRIPTION:	Detects if parallelepiped and interval do intersect
// PARAMETERS:  ptBoxCenter - center plot of paral-d,
//					pBox - object that contains extent of the box
//					pQuaBox - quaternion of the box,
//					pIntlCenter - center of the interval,
//					IntlVector - vector of the interval (with center in
// pIntlCenter)
// RETURNS:		TRUE if objects do intersect, otherwise - FALSE
///////////////////////////////////////////////////////////////////
BOOL DetectCollisionBoxInterval(CPlot3D ptBoxCenter, const SBoundingVolume *pBox,
                                CQuaternion *pQuaBox, CPlot3D ptIntlCenter, CPlot3D ptIntlVector)
{
    // Axes of parallelepiped
    CPlot3D ptA0 = pQuaBox->RotateX(), ptA1 = pQuaBox->RotateY(), ptA2 = pQuaBox->RotateZ();
    // Axis of the interval
    // B / |B| - must be normal
    CPlot3D ptB = ptIntlVector / fabs(ptIntlVector);

    // Calculate extents of box
    // ptMax - gets only positive values
    D3DVALUE
    a0 = pBox->ptMax.GetX(), // extents of P
        a1 = pBox->ptMax.GetY(), a2 = pBox->ptMax.GetZ();
    D3DVALUE b = fabs(ptIntlVector) / 2; // extent of the interval

    // calculate D = C1 - C0
    CPlot3D ptD = ptIntlCenter - ptBoxCenter;

    // dA: 	D = dA0*A0 + dA1*A1 + dA2*A2
    // calculate dA,
    CPlot3D pt_dA = ExcludeVector3D(&ptA0, &ptA1, &ptA2, &ptD);

    D3DVALUE
    dA0 = pt_dA.GetX(), dA1 = pt_dA.GetY(), dA2 = pt_dA.GetZ();

    // calculate {cij} : B = c00*A0 + c10*A1 + c20*A2
    CPlot3D ptC = ExcludeVector3D(&ptA0, &ptA1, &ptA2, &ptB); // c00, c10, c20

    D3DVALUE
    c00 = ptC.GetX(), c10 = ptC.GetY(), c20 = ptC.GetZ();

    // *-*-*-*-*-*-*-* Tests *-*-*-*-*-*-*-*

    if (
        // six face tests
        (a0 + fabs(b * c00) < fabs(dA0)) || (a1 + fabs(b * c10) < fabs(dA1)) ||
        (a2 + fabs(b * c20) < fabs(dA2)) ||

        // nine edge tests
        (fabs(a1 * c20) + fabs(a2 * c10) < fabs(dA2 * c10 - dA1 * c20)) ||

        (fabs(a0 * c20) + fabs(a2 * c00) < fabs(dA0 * c20 - dA2 * c00)) ||

        (fabs(a0 * c10) + fabs(a1 * c00) < fabs(dA1 * c00 - dA2 * c10)))
    {
        // If any of nonintersection tests is true then the parallelepiped
        // and the interval do not intersect

        return FALSE; // do not intersect
    }

    return TRUE; // do intersect
}

///////////////////////////////////////////////////////////////////
// FUNCTION:	ExcludeVector3D
// DESCRIPTION:	function solves the equation D = x0*A0 + x1*A1 + x2*A2,
//					where Ai - vectors, xi - scalars
// PARAMETERS:  ptA0 .. ptA1 - vectors Ai, ptD - vector D
// RETURNS:		returns vector x as CPlot3D
///////////////////////////////////////////////////////////////////
CPlot3D ExcludeVector3D(const CPlot3D *pPtA0, const CPlot3D *pPtA1, const CPlot3D *pPtA2,
                        const CPlot3D *pPtD)
{
    D3DVALUE x0 = 0.0, // Elements of vector x
        x1 = 0.0, x2 = 0.0;

    D3DVALUE // Determine elements of vectors
        a00 = pPtA0->GetX(),
        a01 = pPtA0->GetY(), a02 = pPtA0->GetZ(), a10 = pPtA1->GetX(), a11 = pPtA1->GetY(),
        a12 = pPtA1->GetZ(), a20 = pPtA2->GetX(), a21 = pPtA2->GetY(), a22 = pPtA2->GetZ(),
        d0 = pPtD->GetX(), d1 = pPtD->GetY(), d2 = pPtD->GetZ();

    // calculation *-*-*-*-*-*-*-*
    D3DVALUE fpDen = a20 * a11 * a02 - a21 * a10 * a02 + a12 * a21 * a00 + a22 * a10 * a01 -
                     a11 * a22 * a00 - a20 * a12 * a01; // denominator

    assert(fpDen != 0.0);

    x0 = -(a20 * a12 * d1 - a20 * a11 * d2 + a11 * a22 * d0 + a21 * a10 * d2 - a12 * a21 * d0 -
           a22 * a10 * d1) /
         fpDen;
    x1 = (a21 * a00 * d2 - a21 * d0 * a02 - a00 * a22 * d1 - a20 * a01 * d2 + d0 * a22 * a01 +
          a20 * d1 * a02) /
         fpDen;
    x2 = (a00 * a12 * d1 - a00 * a11 * d2 + a01 * a10 * d2 - d1 * a10 * a02 + d0 * a11 * a02 -
          d0 * a12 * a01) /
         fpDen;

    return CPlot3D(x0, x1, x2);
}

///////////////////////////////////////////////////////////////////
// FUNCTION:	DistancePlotIntervalSquare
// DESCRIPTION: Calculates square of distance between plot and line given by an interval
// PARAMETERS:	ptPlot - plot, ptIntlA, ptIntlB - interval plots
// RETURNS:		value of the square of distance
///////////////////////////////////////////////////////////////////
D3DVALUE DistancePlotIntervalSquare(const CPlot3D ptPlot, const CPlot3D ptIntlA,
                                    const CPlot3D ptIntlB)
{
    // h2 = 1/4c2 * (2b2*c2 - a4 + 2c2*a2 + 2b2*a2 - c4 - b4 ) =
    // 	= 1/2 * (b2 + a2 - 1/2 * c2) - 1/4c2 * (a4 + b4)
    D3DVALUE
    fpSqA = DistancePlotPlotSquare(ptPlot, ptIntlA),
    fpSqB = DistancePlotPlotSquare(ptPlot, ptIntlB),
    fpSqC = DistancePlotPlotSquare(ptIntlA, ptIntlB);

    D3DVALUE fpSqInterval = (fpSqA + fpSqB - fpSqC / 2) / 2 -
                            (fpSqA * fpSqA + fpSqB * fpSqB) / (4 * fpSqC);

    return fpSqInterval;
}

///////////////////////////////////////////////////////////////////
// FUNCTION:	DistancePlotPlotSquare
// DESCRIPTION:	calculates square of distance between two plots
// PARAMETERS:	ptA, ptB - plots
// RETURNS:		value of square of the distance
///////////////////////////////////////////////////////////////////
inline D3DVALUE DistancePlotPlotSquare(const CPlot3D ptA, const CPlot3D ptB)
{
    return square(ptA.GetX() - ptB.GetX()) + square(ptA.GetY() - ptB.GetY()) +
           square(ptA.GetZ() - ptB.GetZ());
}
