#include "StdAfx.h"
#include "WideScreen.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "DXGeometryProcessor.h"

#include "Plot.h"
#include "Quaternion.h"

#include <string>
inline void CreateViewMatrixLH(SHMatrix *pRes, const CPlot3D &pos, const CQuaternion &rot);
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ************************************************************************************************************************
// //
// **
// ** projection matrix creation. RH => right-handed coordinate system, LH => left-handed coordinate
// system
// **
// **
// **
// ************************************************************************************************************************
// //
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// perspective projection matrix
inline void CreatePerspectiveProjectionMatrixLH(SHMatrix *pRes, float fov, float fAspect,
                                                float fNear, float fFar)
{
    float c = static_cast<float>(cos(fov * 0.5));
    float s = static_cast<float>(sin(fov * 0.5));
    float Q = s * fFar / (fFar - fNear);
    //
    Zero(*pRes);
    pRes->_11 = c;
    pRes->_22 = c * fAspect;
    pRes->_33 = Q;
    pRes->_34 = -Q * fNear;
    pRes->_43 = s;

    return;
    SHMatrix add, temp;
    Identity(add);
    add._11 = 1;
    add._22 = 1;
    add._33 = 1;
    add._44 = 1;

    temp = (*pRes) * add;
    *pRes = temp;

    CPlot3D pos = { 0, 0, 0 };
    CQuaternion rot = { 0, 0, 0, 1 };
    CreateViewMatrixLH(&add, pos, rot);
    temp = (*pRes) * add;
    *pRes = temp;

    //
    // DX
    // float alpha = tan(fov * 0.5);
    // float beta = fFar / (fFar - fNear);
    // Zero(*pRes);
    // pRes->_11 = alpha * fAspect;
    // pRes->_22 = alpha;
    // pRes->_33 = beta;
    // pRes->_34 = 1;
    // pRes->_43 = -fNear * beta ;
    // OGL
    // Zero(*pRes);
    // float gamma = (fFar - fNear);
    // pRes->_11 = alpha * fAspect;
    // pRes->_22 = alpha;
    // pRes->_33 = -(fNear + fFar) / gamma;
    // pRes->_34 = 1;
    // pRes->_43 = 2 * fNear * fFar / gamma;
}
inline void CreatePerspectiveProjectionMatrixRH(SHMatrix *pRes, float fov, float fAspect,
                                                float fNear, float fFar)
{
    float c = static_cast<float>(cos(fov * 0.5));
    float s = static_cast<float>(sin(fov * 0.5));
    float Q = s * fFar / (fFar - fNear);
    //
    Zero(*pRes);
    pRes->_11 = c;
    pRes->_22 = c * fAspect;
    pRes->_33 = -Q;
    pRes->_34 = -Q * fNear;
    pRes->_43 = s;
}
// orthographics projection
inline void CreateOrthographicProjectionMatrixLH(SHMatrix *pRes, float fWidth, float fHeight,
                                                 float fNear, float fFar)
{
    Zero(*pRes);
    pRes->_11 = 2.0f / fWidth;
    pRes->_14 = 0;
    pRes->_22 = 2.0f / fHeight;
    pRes->_24 = 0;
    pRes->_33 = 1.0f / (fFar - fNear);
    pRes->_34 = -fNear / (fFar - fNear);
    pRes->_44 = 1.0f;
}
inline void CreateOrthographicProjectionMatrixRH(SHMatrix *pRes, float fWidth, float fHeight,
                                                 float fNear, float fFar)
{
    Zero(*pRes);
    pRes->_11 = 2.0f / fWidth;
    pRes->_14 = 0;
    pRes->_22 = 2.0f / fHeight;
    pRes->_24 = 0;
    pRes->_33 = -1.0f / (fFar - fNear);
    pRes->_34 = -fNear / (fFar - fNear);
    pRes->_44 = 1.0f;
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ************************************************************************************************************************
// //
// **
// ** viewing matrix creation. RH => right-handed coordinate system, LH => left-handed coordinate
// system
// **
// **
// **
// ************************************************************************************************************************
// //
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// low-level creation
inline void CreateViewMatrix(SHMatrix *pRes, const CPlot3D &ptX, const CPlot3D &ptY,
                             const CPlot3D &ptZ, const CPlot3D &ptO)
{
    pRes->_11 = ptX.x;
    pRes->_12 = ptX.y;
    pRes->_13 = ptX.z;
    pRes->_14 = -(ptX * ptO);
    pRes->_21 = ptY.x;
    pRes->_22 = ptY.y;
    pRes->_23 = ptY.z;
    pRes->_24 = -(ptY * ptO);
    pRes->_31 = ptZ.x;
    pRes->_32 = ptZ.y;
    pRes->_33 = ptZ.z;
    pRes->_34 = -(ptZ * ptO);
    pRes->_41 = pRes->_42 = pRes->_43 = 0.0f;
    pRes->_44 = 1.0f;
}
// from position and rotation as quaternion
inline void CreateViewMatrixRH(SHMatrix *pRes, const CPlot3D &pos, const CQuaternion &rot)
{
    // create view axises
    CPlot3D ptX, ptY, ptZ;
    rot.RotateX(ptX);
    rot.RotateY(ptY);
    rot.RotateZ(ptZ);
    // create matrix
    CreateViewMatrix(pRes, ptX, -ptY, -ptZ, pos);
}
inline void CreateViewMatrixLH(SHMatrix *pRes, const CPlot3D &pos, const CQuaternion &rot)
{
    // create view axises
    CPlot3D ptX, ptY, ptZ;
    rot.RotateX(ptX);
    rot.RotateY(ptY);
    rot.RotateZ(ptZ);
    // create matrix
    CreateViewMatrix(pRes, ptX, ptY, ptZ, pos);
}
inline void CreateViewMatrixLH2(SHMatrix *pRes, const CPlot3D &pos, const CQuaternion &rot)
{
    // create view axises
    CPlot3D ptX, ptY, ptZ;
    rot.RotateX(ptX);
    rot.RotateY(ptY);
    rot.RotateZ(ptZ);
    // create matrix
    CreateViewMatrix(pRes, ptX, -ptY, ptZ, pos);
}
// from position and rotation as matrix
inline void CreateViewMatrixRH(SHMatrix *pRes, const CPlot3D &pos, const SHMatrix &rot)
{
    // create view axises
    CPlot3D ptX, ptY, ptZ;
    rot.RotateVector(CPlot3D(1, 0, 0), ptX);
    rot.RotateVector(CPlot3D(0, 1, 0), ptY);
    rot.RotateVector(CPlot3D(0, 0, 1), ptZ);
    // create matrix
    CreateViewMatrix(pRes, ptX, -ptY, -ptZ, pos);
}
inline void CreateViewMatrixLH(SHMatrix *pRes, const CPlot3D &pos, const SHMatrix &rot)
{
    // create view axises
    CPlot3D ptX, ptY, ptZ;
    rot.RotateVector(CPlot3D(1, 0, 0), ptX);
    rot.RotateVector(CPlot3D(0, 1, 0), ptY);
    rot.RotateVector(CPlot3D(0, 0, 1), ptZ);
    // create matrix
    CreateViewMatrix(pRes, ptX, ptY, ptZ, pos);
}
// from 'look-at'
inline void CreateViewMatrixRH(SHMatrix *pRes, const CPlot3D &ptFrom, const CPlot3D &ptTo,
                               const CPlot3D &ptUp)
{
    CPlot3D ptX, ptY = ptUp, ptZ = ptTo - ptFrom;
    float len = fabs(ptZ);
    // norm 'z' vector
    ptZ.Normalize();
    // calc and norm 'x' vector
    ptX = ptY ^ ptZ;
    ptX.Normalize();
    // calc and norm 'y' vector (to ensure, that we have orthonormal basis)
    ptY = ptZ ^ ptX;
    ptY.Normalize();
    //
    CreateViewMatrix(pRes, ptX, -ptY, -ptZ, ptFrom);
}
inline void CreateViewMatrixLH(SHMatrix *pRes, const CPlot3D &ptFrom, const CPlot3D &ptTo,
                               const CPlot3D &ptUp)
{
    CPlot3D ptX, ptY = ptUp, ptZ = ptTo - ptFrom;
    float len = fabs(ptZ);
    // norm 'z' vector
    ptZ.Normalize();
    // calc and norm 'x' vector
    ptX = ptY ^ ptZ;
    ptX.Normalize();
    // calc and norm 'y' vector (to ensure, that we have orthonormal basis)
    ptY = ptZ ^ ptX;
    ptY.Normalize();
    //
    CreateViewMatrix(pRes, ptX, ptY, ptZ, ptFrom);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
CDXGeometryProcessor::CDXGeometryProcessor()
    : lpGeometryProcessor(new CTLPipelineFPU())
{
}
/////////////////////////////////////////////////////////////////////////////////////
CDXGeometryProcessor::~CDXGeometryProcessor()
{
    delete lpGeometryProcessor;
}
/////////////////////////////////////////////////////////////////////////////////////
// creates PERSPECTIVE projection matrix. VCS => DCS
void CDXGeometryProcessor::CreateProjectionMatrix(SHMatrix &matrix, float fov, float fAspect,
                                                  float fNearPlane, float fFarPlane)
{
    CreatePerspectiveProjectionMatrixLH(&matrix, fov, fAspect, fNearPlane, fFarPlane);
}
void CDXGeometryProcessor::CreateProjectionMatrix2(SHMatrix &matrix, float fov, float fAspect,
                                                   float fNearPlane, float fFarPlane)
{
    CreatePerspectiveProjectionMatrixLH(&matrix, fov, fAspect, fNearPlane, fFarPlane);
}
/////////////////////////////////////////////////////////////////////////////////////
// void CDXGeometryProcessor::CreateCameraMatrix( SHMatrix& matrix, LPBASE3D lpCamera )
// creates camera matrix from camera. WCS => VCS
void CDXGeometryProcessor::CreateCameraMatrix(SHMatrix &matrix, LPBASE3D lpCamera)
{
    CreateViewMatrixLH(&matrix, lpCamera->GetCP(), lpCamera->GetOrientation());
}
void CDXGeometryProcessor::CreateCameraMatrix2(SHMatrix &matrix, LPBASE3D lpCamera)
{
    CreateViewMatrixLH2(&matrix, lpCamera->GetCP(), lpCamera->GetOrientation());
}
/////////////////////////////////////////////////////////////////////////////////////
// void CDXGeometryProcessor::CreateCameraMatrix( SHMatrix& matrix, const CPlot3D& ptFrom, const
// CPlot3D& ptTo, const CPlot3D& ptUp ) creates camera matrix from 3 key vectors. WCS => VCS
void CDXGeometryProcessor::CreateCameraMatrix(SHMatrix &matrix, const CPlot3D &ptFrom,
                                              const CPlot3D &ptTo, const CPlot3D &ptUp)
{
    CreateViewMatrixLH(&matrix, ptFrom, ptTo, ptUp);
}
void CDXGeometryProcessor::CreateCameraMatrix2(SHMatrix &matrix, const CPlot3D &ptFrom,
                                               const CPlot3D &ptTo, const CPlot3D &ptUp)
{
    CreateViewMatrixRH(&matrix, ptFrom, ptTo, ptUp);
}
/////////////////////////////////////////////////////////////////////////////////////
// void CDXGeometryProcessor::CreateClipScaleMatrix( SHMatrix* lpMatrix )
// create cip space scaling matrix. normal monitor's clip scale are 4 : 3. rescale DCS
void CDXGeometryProcessor::CreateClipScaleMatrix(SHMatrix &matrix)
{
    owned::widescreen::make_clip_scale_matrix(&matrix._11,WideScreen::Layout());
}
/////////////////////////////////////////////////////////////////////////////////////
// create viewport scaling matrix. NDCS => SCS
void CDXGeometryProcessor::CreateViewportScaleMatrix(SHMatrix &matrix, const RECT &rcRect)
{
    float fpClipHW = static_cast<float>((rcRect.right - rcRect.left) >> 1); // clip half width
    float fpClipHH = static_cast<float>((rcRect.bottom - rcRect.top) >> 1); // clip half height
    Identity(matrix);
    matrix._11 = fpClipHW;
    matrix._14 = fpClipHW;
    matrix._22 = -fpClipHH;
    matrix._24 = fpClipHH;
}
/////////////////////////////////////////////////////////////////////////////////////
// void CDXGeometryProcessor::CreateShadowProjectionMatrix( SHMatrix& matrix, const CPlot3D&
// direction ) create matrix to project object to the plane 'z = 0' with projection direction =
// 'direction'
void CDXGeometryProcessor::CreateShadowProjectionMatrix(SHMatrix &matrix, const CPlot3D &direction)
{
    Identity(matrix);
    matrix._33 = 0;
    matrix._13 = -direction.x / direction.z;
    matrix._23 = -direction.y / direction.z;
}
/////////////////////////////////////////////////////////////////////////////////////
// void CDXGeometryProcessor::CreateShadowProjectionMatrixWithCorrection( SHMatrix& matrix, const
// CPlot3D& direction ) create matrix to project object to the plane 'z = 0'. projection direction
// will be rotated to be in plane ZX and have angle with plane XY as 'direction'
void CDXGeometryProcessor::CreateShadowProjectionMatrixWithCorrection(SHMatrix &matrix,
                                                                      const CPlot3D &direction)
{
    CQuaternion quat;
    CPlot3D dir;

    CreateDirectionCorrection(direction, quat);
    quat.Rotate(direction, dir);

    Identity(matrix);
    matrix._33 = 0;
    matrix._13 = -dir.x / dir.z;
    matrix._23 = -dir.y / dir.z;
}
/////////////////////////////////////////////////////////////////////////////////////
// void CDXGeometryProcessor::CreateDirectionCorrection( const CPlot3D& direction, CQuaternion& quat
// ) create quaternion with rotation to rotate 'direction' to be in plane ZX and have angle with
// plane XY as 'direction'
void CDXGeometryProcessor::CreateDirectionCorrection(const CPlot3D &direction, CQuaternion &quat)
{
    CPlot3D ptDir(direction.x, direction.y, 0);
    CPlot3D ptOrient(1, 0, 0);
    CPlot3D ptAxis(0, 0, 1);
    float angle;
    ptDir *= 1.0f / fabs(ptDir);
    angle = ptDir * ptOrient;
    angle = static_cast<float>(acos(CorrectCosAngle(angle)));
    if (ptDir.x * ptOrient.y < ptDir.y * ptOrient.x)
        ptAxis.SetPlot(0, 0, -1);
    quat.FromAxisAngle(ptAxis, angle);
}
/////////////////////////////////////////////////////////////////////////////////////
int CDXGeometryProcessor::CheckVisibility(const SBoundingVolume &bv)
{
    return CheckVisibility(bv, GetMatrix(MAT_STACK_PROJSPACE));
}
int CDXGeometryProcessor::CheckVisibility(const SBoundingVolume &bv, const SHMatrix &mProj)
{
    BYTE nClipFlags, nClipFlags1, nClipFlags2;
    SXYZW ptHVector;
    CPlot3D pt;
    // check bounding sphere
    if (bv.IsSphere())
    {
        nClipFlags1 = TRV_VISIBLE_NOCLIP;
        nClipFlags2 = TRV_CLIP_ALL;
        //
        pt.x = bv.ptCenter.x + bv.fpRadius;
        pt.y = bv.ptCenter.y + bv.fpRadius;
        pt.z = bv.ptCenter.z + bv.fpRadius;
        mProj.Rotate4Vector(pt, ptHVector);
        nClipFlags1 |= nClipFlags = CheckForViewingFrustum(ptHVector.x, ptHVector.y, ptHVector.z,
                                                           ptHVector.w);
        nClipFlags2 &= nClipFlags;
        //
        pt.x = bv.ptCenter.x - bv.fpRadius;
        pt.y = bv.ptCenter.y + bv.fpRadius;
        pt.z = bv.ptCenter.z + bv.fpRadius;
        mProj.Rotate4Vector(pt, ptHVector);
        nClipFlags1 |= nClipFlags = CheckForViewingFrustum(ptHVector.x, ptHVector.y, ptHVector.z,
                                                           ptHVector.w);
        nClipFlags2 &= nClipFlags;
        //
        pt.x = bv.ptCenter.x + bv.fpRadius;
        pt.y = bv.ptCenter.y - bv.fpRadius;
        pt.z = bv.ptCenter.z + bv.fpRadius;
        mProj.Rotate4Vector(pt, ptHVector);
        nClipFlags1 |= nClipFlags = CheckForViewingFrustum(ptHVector.x, ptHVector.y, ptHVector.z,
                                                           ptHVector.w);
        nClipFlags2 &= nClipFlags;
        //
        pt.x = bv.ptCenter.x - bv.fpRadius;
        pt.y = bv.ptCenter.y - bv.fpRadius;
        pt.z = bv.ptCenter.z + bv.fpRadius;
        mProj.Rotate4Vector(pt, ptHVector);
        nClipFlags1 |= nClipFlags = CheckForViewingFrustum(ptHVector.x, ptHVector.y, ptHVector.z,
                                                           ptHVector.w);
        nClipFlags2 &= nClipFlags;
        //
        pt.x = bv.ptCenter.x + bv.fpRadius;
        pt.y = bv.ptCenter.y + bv.fpRadius;
        pt.z = bv.ptCenter.z - bv.fpRadius;
        mProj.Rotate4Vector(pt, ptHVector);
        nClipFlags1 |= nClipFlags = CheckForViewingFrustum(ptHVector.x, ptHVector.y, ptHVector.z,
                                                           ptHVector.w);
        nClipFlags2 &= nClipFlags;
        //
        pt.x = bv.ptCenter.x - bv.fpRadius;
        pt.y = bv.ptCenter.y + bv.fpRadius;
        pt.z = bv.ptCenter.z - bv.fpRadius;
        mProj.Rotate4Vector(pt, ptHVector);
        nClipFlags1 |= nClipFlags = CheckForViewingFrustum(ptHVector.x, ptHVector.y, ptHVector.z,
                                                           ptHVector.w);
        nClipFlags2 &= nClipFlags;
        //
        pt.x = bv.ptCenter.x + bv.fpRadius;
        pt.y = bv.ptCenter.y - bv.fpRadius;
        pt.z = bv.ptCenter.z - bv.fpRadius;
        mProj.Rotate4Vector(pt, ptHVector);
        nClipFlags1 |= nClipFlags = CheckForViewingFrustum(ptHVector.x, ptHVector.y, ptHVector.z,
                                                           ptHVector.w);
        nClipFlags2 &= nClipFlags;
        //
        pt.x = bv.ptCenter.x - bv.fpRadius;
        pt.y = bv.ptCenter.y - bv.fpRadius;
        pt.z = bv.ptCenter.z - bv.fpRadius;
        mProj.Rotate4Vector(pt, ptHVector);
        nClipFlags1 |= nClipFlags = CheckForViewingFrustum(ptHVector.x, ptHVector.y, ptHVector.z,
                                                           ptHVector.w);
        nClipFlags2 &= nClipFlags;
        //
        if (nClipFlags2)
            return TRV_INVISIBLE;
        else if (!nClipFlags1)
            return TRV_VISIBLE_NOCLIP;
    }
    // check bounding box
    if (bv.IsBox())
    {
        nClipFlags1 = TRV_VISIBLE_NOCLIP;
        nClipFlags2 = TRV_CLIP_ALL;
        // AAA
        pt.x = bv.ptCenter.x + bv.ptMax.x;
        pt.y = bv.ptCenter.y + bv.ptMax.y;
        pt.z = bv.ptCenter.z + bv.ptMax.z;
        mProj.Rotate4Vector(pt, ptHVector);
        nClipFlags1 |= nClipFlags = CheckForViewingFrustum(ptHVector.x, ptHVector.y, ptHVector.z,
                                                           ptHVector.w);
        nClipFlags2 &= nClipFlags;
        // AAI
        pt.x = bv.ptCenter.x + bv.ptMax.x;
        pt.y = bv.ptCenter.y + bv.ptMax.y;
        pt.z = bv.ptCenter.z + bv.ptMin.z;
        mProj.Rotate4Vector(pt, ptHVector);
        nClipFlags1 |= nClipFlags = CheckForViewingFrustum(ptHVector.x, ptHVector.y, ptHVector.z,
                                                           ptHVector.w);
        nClipFlags2 &= nClipFlags;
        // AIA
        pt.x = bv.ptCenter.x + bv.ptMax.x;
        pt.y = bv.ptCenter.y + bv.ptMin.y;
        pt.z = bv.ptCenter.z + bv.ptMax.z;
        mProj.Rotate4Vector(pt, ptHVector);
        nClipFlags1 |= nClipFlags = CheckForViewingFrustum(ptHVector.x, ptHVector.y, ptHVector.z,
                                                           ptHVector.w);
        nClipFlags2 &= nClipFlags;
        // AII
        pt.x = bv.ptCenter.x + bv.ptMax.x;
        pt.y = bv.ptCenter.y + bv.ptMin.y;
        pt.z = bv.ptCenter.z + bv.ptMin.z;
        mProj.Rotate4Vector(pt, ptHVector);
        nClipFlags1 |= nClipFlags = CheckForViewingFrustum(ptHVector.x, ptHVector.y, ptHVector.z,
                                                           ptHVector.w);
        nClipFlags2 &= nClipFlags;
        // IAA
        pt.x = bv.ptCenter.x + bv.ptMin.x;
        pt.y = bv.ptCenter.y + bv.ptMax.y;
        pt.z = bv.ptCenter.z + bv.ptMax.z;
        mProj.Rotate4Vector(pt, ptHVector);
        nClipFlags1 |= nClipFlags = CheckForViewingFrustum(ptHVector.x, ptHVector.y, ptHVector.z,
                                                           ptHVector.w);
        nClipFlags2 &= nClipFlags;
        // IAI
        pt.x = bv.ptCenter.x + bv.ptMin.x;
        pt.y = bv.ptCenter.y + bv.ptMax.y;
        pt.z = bv.ptCenter.z + bv.ptMin.z;
        mProj.Rotate4Vector(pt, ptHVector);
        nClipFlags1 |= nClipFlags = CheckForViewingFrustum(ptHVector.x, ptHVector.y, ptHVector.z,
                                                           ptHVector.w);
        nClipFlags2 &= nClipFlags;
        // IIA
        pt.x = bv.ptCenter.x + bv.ptMin.x;
        pt.y = bv.ptCenter.y + bv.ptMin.y;
        pt.z = bv.ptCenter.z + bv.ptMax.z;
        mProj.Rotate4Vector(pt, ptHVector);
        nClipFlags1 |= nClipFlags = CheckForViewingFrustum(ptHVector.x, ptHVector.y, ptHVector.z,
                                                           ptHVector.w);
        nClipFlags2 &= nClipFlags;
        // III
        pt.x = bv.ptCenter.x + bv.ptMin.x;
        pt.y = bv.ptCenter.y + bv.ptMin.y;
        pt.z = bv.ptCenter.z + bv.ptMin.z;
        mProj.Rotate4Vector(pt, ptHVector);
        nClipFlags1 |= nClipFlags = CheckForViewingFrustum(ptHVector.x, ptHVector.y, ptHVector.z,
                                                           ptHVector.w);
        nClipFlags2 &= nClipFlags;
        //
        if (nClipFlags2)
            return TRV_INVISIBLE;
        else if (!nClipFlags1)
            return TRV_VISIBLE_NOCLIP;
    }

    return TRV_VISIBLE_CLIP; // return common value - 'visible with clip'
}
/////////////////////////////////////////////////////////////////////////////////////
int CDXGeometryProcessor::TransformVertices(SXYZW4 *pSrc, SXYZW *pDst, BYTE *pClips,
                                            const int nNumSrc, const SBoundingVolume *pBV)
{
    int nRetCode;
    if (pBV)
        nRetCode = CheckVisibility(*pBV);
    else
        nRetCode = TRV_VISIBLE_CLIP;
    //
    switch (nRetCode)
    {
    case TRV_INVISIBLE:
        return TRV_INVISIBLE;
    case TRV_VISIBLE_NOCLIP:
        GetGeometryProcessor()->TransformVertices(GetMatrix(MAT_STACK_SCRNSPACE), pSrc, pDst,
                                                  nNumSrc);
        return TRV_VISIBLE_NOCLIP;
    case TRV_VISIBLE_CLIP:
        nRetCode = GetGeometryProcessor()->TransformVertices(GetMatrix(MAT_STACK_PROJSPACE), pSrc,
                                                             pDst, pClips, nNumSrc);
        return nRetCode;
    }

    return TRV_INVISIBLE;
}
/////////////////////////////////////////////////////////////////////////////////////
int CDXGeometryProcessor::TransformVertices(SXYZW *pSrc, SXYZW *pDst, BYTE *pClips,
                                            const int nNumSrc, const SBoundingVolume *pBV)
{
    int nRetCode;
    if (pBV)
        nRetCode = CheckVisibility(*pBV);
    else
        nRetCode = TRV_VISIBLE_CLIP;
    //
    switch (nRetCode)
    {
    case TRV_INVISIBLE:
        return TRV_INVISIBLE;
    case TRV_VISIBLE_NOCLIP:
        GetGeometryProcessor()->TransformVertices(GetMatrix(MAT_STACK_SCRNSPACE), pSrc, pDst,
                                                  nNumSrc);
        return TRV_VISIBLE_NOCLIP;
    case TRV_VISIBLE_CLIP:
        nRetCode = GetGeometryProcessor()->TransformVertices(GetMatrix(MAT_STACK_PROJSPACE), pSrc,
                                                             pDst, pClips, nNumSrc);
        return nRetCode;
    }

    return TRV_INVISIBLE;
}
/////////////////////////////////////////////////////////////////////////////////////
int CDXGeometryProcessor::TransformVertices(SXYZW4 *pSrc, STLVertex1 *pDst, BYTE *pClips,
                                            DWORD *speculars, const int nNumSrc,
                                            const SBoundingVolume *pBV)
{
    int nRetCode;
    if (pBV)
        nRetCode = CheckVisibility(*pBV);
    else
        nRetCode = TRV_VISIBLE_CLIP;
    //
    switch (nRetCode)
    {
    case TRV_INVISIBLE:
        return TRV_INVISIBLE;
    case TRV_VISIBLE_NOCLIP:
        GetGeometryProcessor()->TransformVertices(GetMatrix(MAT_STACK_SCRNSPACE), pSrc, pDst,
                                                  speculars, nNumSrc);
        return TRV_VISIBLE_NOCLIP;
    case TRV_VISIBLE_CLIP:
        nRetCode = GetGeometryProcessor()->TransformVertices(GetMatrix(MAT_STACK_PROJSPACE), pSrc,
                                                             pDst, pClips, speculars, nNumSrc);
        return nRetCode;
    }

    return TRV_INVISIBLE;
}
/////////////////////////////////////////////////////////////////////////////////////
int CDXGeometryProcessor::TransformVertices(SXYZW4 *pSrc, STLVertex1 *pDst, BYTE *pClips,
                                            const int nNumSrc, const SBoundingVolume *pBV)
{
    int nRetCode;
    if (pBV)
        nRetCode = CheckVisibility(*pBV);
    else
        nRetCode = TRV_VISIBLE_CLIP;
    //
    switch (nRetCode)
    {
    case TRV_INVISIBLE:
        return TRV_INVISIBLE;
    case TRV_VISIBLE_NOCLIP:
        GetGeometryProcessor()->TransformVertices(GetMatrix(MAT_STACK_SCRNSPACE), pSrc, pDst,
                                                  nNumSrc);
        return TRV_VISIBLE_NOCLIP;
    case TRV_VISIBLE_CLIP:
        nRetCode = GetGeometryProcessor()->TransformVertices(GetMatrix(MAT_STACK_PROJSPACE), pSrc,
                                                             pDst, pClips, nNumSrc);
        return nRetCode;
    }

    return TRV_INVISIBLE;
}
/////////////////////////////////////////////////////////////////////////////////////
int CDXGeometryProcessor::TransformVertices2Projection(SXYZW4 *pSrc, SXYZW *pDst, BYTE *pClips,
                                                       const int nNumSrcVertices)
{
    SHMatrix matrix = GetMatrix(MAT_STACK_PROJSPACE);
    Transpose(matrix);
    return lpGeometryProcessor->SimplyTransformC(*reinterpret_cast<D3DMATRIX *>(&matrix), pSrc,
                                                 pDst, pClips, nNumSrcVertices);
}
int CDXGeometryProcessor::TransformVertices2Projection(SXYZW4 *pSrc, SXYZW4 *pDst, BYTE *pClips,
                                                       const int nNumSrcVertices)
{
    SHMatrix matrix = GetMatrix(MAT_STACK_PROJSPACE);
    Transpose(matrix);
    return lpGeometryProcessor->SimplyTransform4C(*reinterpret_cast<D3DMATRIX *>(&matrix), pSrc,
                                                  pDst, pClips, nNumSrcVertices);
}
/////////////////////////////////////////////////////////////////////////////////////
int CDXGeometryProcessor::TransformVertices2Projection(SXYZW *pSrc, SXYZW *pDst, BYTE *pClips,
                                                       const int nNumSrcVertices,
                                                       const SBoundingVolume *pBV)
{
    int nRetCode;
    if (pBV)
        nRetCode = CheckVisibility(*pBV);
    else
        nRetCode = TRV_VISIBLE_CLIP;
    //
    switch (nRetCode)
    {
    case TRV_INVISIBLE:
        return TRV_INVISIBLE;
    case TRV_VISIBLE_NOCLIP:
        GetGeometryProcessor()->SimplyTransform(GetMatrix(MAT_STACK_PROJSPACE), pSrc, pDst,
                                                nNumSrcVertices);
        return TRV_VISIBLE_NOCLIP;
    case TRV_VISIBLE_CLIP:
        nRetCode = GetGeometryProcessor()->SimplyTransform(GetMatrix(MAT_STACK_PROJSPACE), pSrc,
                                                           pDst, pClips, nNumSrcVertices);
        return nRetCode;
    }

    return TRV_INVISIBLE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CDXGeometryProcessor::PushMatrix(const SHMatrix &matrix, const DWORD dwMatrixStackID)
{
#ifdef _DEBUG
    ASSERT((dwMatrixStackID < MAT_NUM_STACKS) &&
           (mstacks[dwMatrixStackID].dwNumMatrices <= MAT_MAX_NUM_MATRICES));
    /*if (mstack.dwNumMatrices > 16)
    {
        ASSERT(0);
    }*/
#endif // _DEBUG
    SMatrixStack &mstack = mstacks[dwMatrixStackID];

    if (mstack.dwNumMatrices == 0) // first matrix
    {
        mstack.dwNumMatrices = 1;
        mstack[0] = matrix;
    }
    else // next matrices
    {
        mstack.dwNumMatrices++;
        mstack[mstack.dwNumMatrices - 1].Multiply(mstack[mstack.dwNumMatrices - 2], matrix);
    }
    // debug
    mstack.topMatrix = GetMatrix(dwMatrixStackID);
}