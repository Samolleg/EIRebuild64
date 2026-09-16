#include "StdAfx.h"

#include "TLPipelineFPU.h"
#include "Polygon.h"
#include "dx11\RendererDX11.h"
/////////////////////////////////////////////////////////////////////////////////////
#ifdef _DEBUG
#define new DEBUG_NEW
#endif // _DEBUG
/////////////////////////////////////////////////////////////////////////////////////
CTLPipelineFPU theTLPipelineFPU;
/////////////////////////////////////////////////////////////////////////////////////
CTLPipeline *GetTLPipeline()
{
    return &theTLPipelineFPU;
}
/////////////////////////////////////////////////////////////////////////////////////
CTLPipelineFPU::CTLPipelineFPU()
{
    dcvAmbientColor.r = 0;
    dcvAmbientColor.g = 0;
    dcvAmbientColor.b = 0;
    dcvAmbientColor.a = 0;
    fpClipHH = 0;
    fpClipHW = 0;
    fpFogStart = 0;
    fpFogEnd = 0;
    fpRcprFogDiff255 = 255.0f;
    memset(&projection, 0, sizeof(D3DMATRIX));
}
/////////////////////////////////////////////////////////////////////////////////////
CTLPipelineFPU::~CTLPipelineFPU()
{
    SetSunlight(NULL);
}
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
//                                Tool functions                                    //
// ******************************************************************************** //
//////////////////////////////////////////////////////////////////////////////////////
// void CTLPipelineFPU::SetClipRect( float fpClipWidth, float fpClipHeight )
// set clip rect for correct TL pipeline work
void CTLPipelineFPU::SetClipRect(float fpClipWidth, float fpClipHeight)
{
    fpClipHH = fpClipHeight * 0.5f;
    fpClipHW = fpClipWidth * 0.5f;
}
/////////////////////////////////////////////////////////////////////////////////////
// void CTLPipelineFPU::SetSunlight( CLight* lpLight )
// set sunlight (to avid pass it to each lit function)
void CTLPipelineFPU::SetSunlight(CLight *lpLight)
{
    // release old sunlight
    if (lpSunlight)
        lpSunlight->Release();
    // assign new
    lpSunlight = lpLight;
    // increase ref count for new light
    if (lpSunlight)
        lpSunlight->AddRef();
}
/////////////////////////////////////////////////////////////////////////////////////
// void CTLPipelineFPU::SetAmbientLight( float r, float g, float b )
// set ambient light (to avid pass it to each lit function)
void CTLPipelineFPU::SetAmbientLight(float r, float g, float b)
{
    dcvAmbientColor.r = r;
    dcvAmbientColor.g = g;
    dcvAmbientColor.b = b;
    dcvAmbientColor.a = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
// void CTLPipelineFPU::SetProjectionMatrix( D3DMATRIX& proj )
// set projection matrix
void CTLPipelineFPU::SetProjectionMatrix(D3DMATRIX &proj)
{
    memcpy(&projection, &proj, sizeof(D3DMATRIX));
}
/////////////////////////////////////////////////////////////////////////////////////
// set fog range
void CTLPipelineFPU::SetFogRange(float fpNewFogStart, float fpNewFogEnd)
{
    fpFogStart = fpNewFogStart;
    fpFogEnd = fpNewFogEnd;
    fpRcprFogDiff255 = 255.0f / (fpFogEnd - fpFogStart);
}
/////////////////////////////////////////////////////////////////////////////////////
void CTLPipelineFPU::CalculateBoundingVolume(SBoundingVolume *lpBV, SXYZW4 *plots, int nNumPlots)
{
    float maxx = -FLT_MAX, maxy = -FLT_MAX, maxz = -FLT_MAX;
    float minx = +FLT_MAX, miny = +FLT_MAX, minz = +FLT_MAX;
    // calculate min/max for x, y, z
    int i = nNumPlots;
    while (i)
    {
        i--;
        // maxx
        maxx = Max(maxx, plots[i].x[0]);
        maxx = Max(maxx, plots[i].x[1]);
        maxx = Max(maxx, plots[i].x[2]);
        maxx = Max(maxx, plots[i].x[3]);
        // minx
        minx = Min(minx, plots[i].x[0]);
        minx = Min(minx, plots[i].x[1]);
        minx = Min(minx, plots[i].x[2]);
        minx = Min(minx, plots[i].x[3]);
        // maxy
        maxy = Max(maxy, plots[i].y[0]);
        maxy = Max(maxy, plots[i].y[1]);
        maxy = Max(maxy, plots[i].y[2]);
        maxy = Max(maxy, plots[i].y[3]);
        // miny
        miny = Min(miny, plots[i].y[0]);
        miny = Min(miny, plots[i].y[1]);
        miny = Min(miny, plots[i].y[2]);
        miny = Min(miny, plots[i].y[3]);
        // maxz
        maxz = Max(maxz, plots[i].z[0]);
        maxz = Max(maxz, plots[i].z[1]);
        maxz = Max(maxz, plots[i].z[2]);
        maxz = Max(maxz, plots[i].z[3]);
        // minz
        minz = Min(minz, plots[i].z[0]);
        minz = Min(minz, plots[i].z[1]);
        minz = Min(minz, plots[i].z[2]);
        minz = Min(minz, plots[i].z[3]);
    }
    // create bounding volume
    CPlot3D ptCenter((maxx + minx) * 0.5f, (maxy + miny) * 0.5f, (maxz + minz) * 0.5f);
    CPlot3D ptMax(maxx - ptCenter.x, maxy - ptCenter.y, maxz - ptCenter.z);
    CPlot3D ptMin(minx - ptCenter.x, miny - ptCenter.y, minz - ptCenter.z);
    lpBV->SetCenter(ptCenter);
    lpBV->SetBox(ptMin, ptMax);
    lpBV->SetRadius(fabs(ptMax));
}
/////////////////////////////////////////////////////////////////////////////////////
void CTLPipelineFPU::CalculateBoundingVolume(SBoundingVolume *lpBV, SXYZW *plots, int nNumPlots)
{
    float maxx = -FLT_MAX, maxy = -FLT_MAX, maxz = -FLT_MAX;
    float minx = +FLT_MAX, miny = +FLT_MAX, minz = +FLT_MAX;
    // calculate min/max for x, y, z
    int i = nNumPlots;
    while (i)
    {
        i--;
        // x
        maxx = Max(maxx, plots[i].x);
        minx = Min(minx, plots[i].x);
        // y
        maxy = Max(maxy, plots[i].y);
        miny = Min(miny, plots[i].y);
        // z
        maxz = Max(maxz, plots[i].z);
        minz = Min(minz, plots[i].z);
    }
    // create bounding volume
    CPlot3D ptCenter((maxx + minx) * 0.5f, (maxy + miny) * 0.5f, (maxz + minz) * 0.5f);
    CPlot3D ptMax(maxx - ptCenter.x, maxy - ptCenter.y, maxz - ptCenter.z);
    CPlot3D ptMin(minx - ptCenter.x, miny - ptCenter.y, minz - ptCenter.z);
    lpBV->SetCenter(ptCenter);
    lpBV->SetBox(ptMin, ptMax);
    lpBV->SetRadius(fabs(ptMax));
}
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
//                      Transformation functions                                    //
// ******************************************************************************** //
//////////////////////////////////////////////////////////////////////////////////////
// transform vertices from SXYZW4 to STLVertex1 (with data swizzling) with homogenezation and w/o
// clip check assuming, that 'w' component in source vertices are equal to '1'
int CTLPipelineFPU::TransformVertices(const SHMatrix &matrix, SXYZW4 *lpSrc, STLVertex1 *lpDst,
                                      DWORD *speculars, const int nCount)
{
    const float _11 = matrix._11, _12 = matrix._21, _13 = matrix._31, _14 = matrix._41;
    const float _21 = matrix._12, _22 = matrix._22, _23 = matrix._32, _24 = matrix._42;
    const float _31 = matrix._13, _32 = matrix._23, _33 = matrix._33, _34 = matrix._43;
    const float _41 = matrix._14, _42 = matrix._24, _43 = matrix._34, _44 = matrix._44;
    float xp[4], yp[4], zp[4], wp[4], rhw[4];
    float x[4], y[4], z[4];
    int i, i4;

    for (i4 = 0, i = 0; i < nCount; i++)
    {
        // load data
        // x, y, z
        Copy16Bytes(x, lpSrc[i].x);
        Copy16Bytes(y, lpSrc[i].y);
        Copy16Bytes(z, lpSrc[i].z);
        // transforming
        // 'xp' element (4x)
        xp[0] = _11 * x[0] + _21 * y[0] + _31 * z[0] + _41;
        xp[1] = _11 * x[1] + _21 * y[1] + _31 * z[1] + _41;
        xp[2] = _11 * x[2] + _21 * y[2] + _31 * z[2] + _41;
        xp[3] = _11 * x[3] + _21 * y[3] + _31 * z[3] + _41;
        // 'yp' element (4x)
        yp[0] = _12 * x[0] + _22 * y[0] + _32 * z[0] + _42;
        yp[1] = _12 * x[1] + _22 * y[1] + _32 * z[1] + _42;
        yp[2] = _12 * x[2] + _22 * y[2] + _32 * z[2] + _42;
        yp[3] = _12 * x[3] + _22 * y[3] + _32 * z[3] + _42;
        // 'zp' element (4x)
        zp[0] = _13 * x[0] + _23 * y[0] + _33 * z[0] + _43;
        zp[1] = _13 * x[1] + _23 * y[1] + _33 * z[1] + _43;
        zp[2] = _13 * x[2] + _23 * y[2] + _33 * z[2] + _43;
        zp[3] = _13 * x[3] + _23 * y[3] + _33 * z[3] + _43;
        // 'wp' element (4x)
        wp[0] = _14 * x[0] + _24 * y[0] + _34 * z[0] + _44;
        wp[1] = _14 * x[1] + _24 * y[1] + _34 * z[1] + _44;
        wp[2] = _14 * x[2] + _24 * y[2] + _34 * z[2] + _44;
        wp[3] = _14 * x[3] + _24 * y[3] + _34 * z[3] + _44;
        // homogenezation and storing
        // 'w' element (4x)
        rhw[0] = 1.0f / wp[0];
        rhw[1] = 1.0f / wp[1];
        rhw[2] = 1.0f / wp[2];
        rhw[3] = 1.0f / wp[3];
        // 1st element
        lpDst[i4].sx = xp[0] * rhw[0];
        lpDst[i4].sy = yp[0] * rhw[0];
        lpDst[i4].sz = zp[0] * rhw[0];
        lpDst[i4].rhw = rhw[0];
        lpDst[i4].specular = speculars[i4];
        ++i4;
        // 2nd element
        lpDst[i4].sx = xp[1] * rhw[1];
        lpDst[i4].sy = yp[1] * rhw[1];
        lpDst[i4].sz = zp[1] * rhw[1];
        lpDst[i4].rhw = rhw[1];
        lpDst[i4].specular = speculars[i4];
        ++i4;
        // 3rd element
        lpDst[i4].sx = xp[2] * rhw[2];
        lpDst[i4].sy = yp[2] * rhw[2];
        lpDst[i4].sz = zp[2] * rhw[2];
        lpDst[i4].rhw = rhw[2];
        lpDst[i4].specular = speculars[i4];
        ++i4;
        // 4th element
        lpDst[i4].sx = xp[3] * rhw[3];
        lpDst[i4].sy = yp[3] * rhw[3];
        lpDst[i4].sz = zp[3] * rhw[3];
        lpDst[i4].rhw = rhw[3];
        lpDst[i4].specular = speculars[i4];
        ++i4;
    }

    return TRV_VISIBLE_NOCLIP;
}
/////////////////////////////////////////////////////////////////////////////////////
// transform vertices from SXYZW4 to STLVertex1 (with data swizzling) with homogenezation and w/o
// clip check assuming, that 'w' component in source vertices are equal to '1'
int CTLPipelineFPU::TransformVertices(const SHMatrix &matrix, SXYZW4 *lpSrc, STLVertex1 *lpDst,
                                      const int nCount)
{
    const float _11 = matrix._11, _12 = matrix._21, _13 = matrix._31, _14 = matrix._41;
    const float _21 = matrix._12, _22 = matrix._22, _23 = matrix._32, _24 = matrix._42;
    const float _31 = matrix._13, _32 = matrix._23, _33 = matrix._33, _34 = matrix._43;
    const float _41 = matrix._14, _42 = matrix._24, _43 = matrix._34, _44 = matrix._44;
    float xp[4], yp[4], zp[4], wp[4], rhw[4];
    float x[4], y[4], z[4];
    int i, i4;

    for (i4 = 0, i = 0; i < nCount; i++)
    {
        // load data
        // x, y, z
        Copy16Bytes(x, lpSrc[i].x);
        Copy16Bytes(y, lpSrc[i].y);
        Copy16Bytes(z, lpSrc[i].z);
        // transforming
        // 'xp' element (4x)
        xp[0] = _11 * x[0] + _21 * y[0] + _31 * z[0] + _41;
        xp[1] = _11 * x[1] + _21 * y[1] + _31 * z[1] + _41;
        xp[2] = _11 * x[2] + _21 * y[2] + _31 * z[2] + _41;
        xp[3] = _11 * x[3] + _21 * y[3] + _31 * z[3] + _41;
        // 'yp' element (4x)
        yp[0] = _12 * x[0] + _22 * y[0] + _32 * z[0] + _42;
        yp[1] = _12 * x[1] + _22 * y[1] + _32 * z[1] + _42;
        yp[2] = _12 * x[2] + _22 * y[2] + _32 * z[2] + _42;
        yp[3] = _12 * x[3] + _22 * y[3] + _32 * z[3] + _42;
        // 'zp' element (4x)
        zp[0] = _13 * x[0] + _23 * y[0] + _33 * z[0] + _43;
        zp[1] = _13 * x[1] + _23 * y[1] + _33 * z[1] + _43;
        zp[2] = _13 * x[2] + _23 * y[2] + _33 * z[2] + _43;
        zp[3] = _13 * x[3] + _23 * y[3] + _33 * z[3] + _43;
        // 'wp' element (4x)
        wp[0] = _14 * x[0] + _24 * y[0] + _34 * z[0] + _44;
        wp[1] = _14 * x[1] + _24 * y[1] + _34 * z[1] + _44;
        wp[2] = _14 * x[2] + _24 * y[2] + _34 * z[2] + _44;
        wp[3] = _14 * x[3] + _24 * y[3] + _34 * z[3] + _44;
        // homogenezation and storing
        // 'w' element (4x)
        rhw[0] = 1.0f / wp[0];
        rhw[1] = 1.0f / wp[1];
        rhw[2] = 1.0f / wp[2];
        rhw[3] = 1.0f / wp[3];
        // 1st element
        lpDst[i4].sx = xp[0] * rhw[0];
        lpDst[i4].sy = yp[0] * rhw[0];
        lpDst[i4].sz = zp[0] * rhw[0];
        lpDst[i4++].rhw = rhw[0];
        // 2nd element
        lpDst[i4].sx = xp[1] * rhw[1];
        lpDst[i4].sy = yp[1] * rhw[1];
        lpDst[i4].sz = zp[1] * rhw[1];
        lpDst[i4++].rhw = rhw[1];
        // 3rd element
        lpDst[i4].sx = xp[2] * rhw[2];
        lpDst[i4].sy = yp[2] * rhw[2];
        lpDst[i4].sz = zp[2] * rhw[2];
        lpDst[i4++].rhw = rhw[2];
        // 4th element
        lpDst[i4].sx = xp[3] * rhw[3];
        lpDst[i4].sy = yp[3] * rhw[3];
        lpDst[i4].sz = zp[3] * rhw[3];
        lpDst[i4++].rhw = rhw[3];
    }

    return TRV_VISIBLE_NOCLIP;
}
/////////////////////////////////////////////////////////////////////////////////////
// transform vertices from SXYZW4 to SXYZW4 with homogenezation and w/o clip check
// assuming, that 'w' component in source vertices are equal to '1'
int CTLPipelineFPU::TransformVertices(const SHMatrix &matrix, SXYZW4 *lpSrc, SXYZW4 *lpDst,
                                      const int nCount)
{
    const float _11 = matrix._11, _12 = matrix._21, _13 = matrix._31, _14 = matrix._41;
    const float _21 = matrix._12, _22 = matrix._22, _23 = matrix._32, _24 = matrix._42;
    const float _31 = matrix._13, _32 = matrix._23, _33 = matrix._33, _34 = matrix._43;
    const float _41 = matrix._14, _42 = matrix._24, _43 = matrix._34, _44 = matrix._44;
    float xp[4], yp[4], zp[4], wp[4], rhw[4];
    float x[4], y[4], z[4];

    for (int i = 0; i < nCount; i++)
    {
        // load data
        // x, y, z
        Copy16Bytes(x, lpSrc[i].x);
        Copy16Bytes(y, lpSrc[i].y);
        Copy16Bytes(z, lpSrc[i].z);
        // transforming
        // 'xp' element (4x)
        xp[0] = _11 * x[0] + _21 * y[0] + _31 * z[0] + _41;
        xp[1] = _11 * x[1] + _21 * y[1] + _31 * z[1] + _41;
        xp[2] = _11 * x[2] + _21 * y[2] + _31 * z[2] + _41;
        xp[3] = _11 * x[3] + _21 * y[3] + _31 * z[3] + _41;
        // 'yp' element (4x)
        yp[0] = _12 * x[0] + _22 * y[0] + _32 * z[0] + _42;
        yp[1] = _12 * x[1] + _22 * y[1] + _32 * z[1] + _42;
        yp[2] = _12 * x[2] + _22 * y[2] + _32 * z[2] + _42;
        yp[3] = _12 * x[3] + _22 * y[3] + _32 * z[3] + _42;
        // 'zp' element (4x)
        zp[0] = _13 * x[0] + _23 * y[0] + _33 * z[0] + _43;
        zp[1] = _13 * x[1] + _23 * y[1] + _33 * z[1] + _43;
        zp[2] = _13 * x[2] + _23 * y[2] + _33 * z[2] + _43;
        zp[3] = _13 * x[3] + _23 * y[3] + _33 * z[3] + _43;
        // 'wp' element (4x)
        wp[0] = _14 * x[0] + _24 * y[0] + _34 * z[0] + _44;
        wp[1] = _14 * x[1] + _24 * y[1] + _34 * z[1] + _44;
        wp[2] = _14 * x[2] + _24 * y[2] + _34 * z[2] + _44;
        wp[3] = _14 * x[3] + _24 * y[3] + _34 * z[3] + _44;
        // homogenezation and storing
        // 'w' element (4x)
        lpDst[i].w[0] = rhw[0] = 1.0f / wp[0];
        lpDst[i].w[1] = rhw[1] = 1.0f / wp[1];
        lpDst[i].w[2] = rhw[2] = 1.0f / wp[2];
        lpDst[i].w[3] = rhw[3] = 1.0f / wp[3];
        // 'x' element (4x)
        lpDst[i].x[0] = xp[0] * rhw[0];
        lpDst[i].x[1] = xp[1] * rhw[1];
        lpDst[i].x[2] = xp[2] * rhw[2];
        lpDst[i].x[3] = xp[3] * rhw[3];
        // 'y' element (4x)
        lpDst[i].y[0] = yp[0] * rhw[0];
        lpDst[i].y[1] = yp[1] * rhw[1];
        lpDst[i].y[2] = yp[2] * rhw[2];
        lpDst[i].y[3] = yp[3] * rhw[3];
        // 'z' element (4x)
        lpDst[i].z[0] = zp[0] * rhw[0];
        lpDst[i].z[1] = zp[1] * rhw[1];
        lpDst[i].z[2] = zp[2] * rhw[2];
        lpDst[i].z[3] = zp[3] * rhw[3];
    }

    return TRV_VISIBLE_NOCLIP;
}
/////////////////////////////////////////////////////////////////////////////////////
// transform vertices from SXYZW4 to SXYZW (with data swizzling) with homogenezation and w/o clip
// check assuming, that 'w' component in source vertices are equal to '1'
int CTLPipelineFPU::TransformVertices(const SHMatrix &matrix, SXYZW4 *lpSrc, SXYZW *lpDst,
                                      const int nCount)
{
    const float _11 = matrix._11, _12 = matrix._21, _13 = matrix._31, _14 = matrix._41;
    const float _21 = matrix._12, _22 = matrix._22, _23 = matrix._32, _24 = matrix._42;
    const float _31 = matrix._13, _32 = matrix._23, _33 = matrix._33, _34 = matrix._43;
    const float _41 = matrix._14, _42 = matrix._24, _43 = matrix._34, _44 = matrix._44;
    float xp[4], yp[4], zp[4], wp[4], rhw[4];
    float x[4], y[4], z[4];
    int i, i4;

    for (i4 = 0, i = 0; i < nCount; i++)
    {
        // load data
        // x, y, z
        Copy16Bytes(x, lpSrc[i].x);
        Copy16Bytes(y, lpSrc[i].y);
        Copy16Bytes(z, lpSrc[i].z);
        // transforming
        // 'xp' element (4x)
        xp[0] = _11 * x[0] + _21 * y[0] + _31 * z[0] + _41;
        xp[1] = _11 * x[1] + _21 * y[1] + _31 * z[1] + _41;
        xp[2] = _11 * x[2] + _21 * y[2] + _31 * z[2] + _41;
        xp[3] = _11 * x[3] + _21 * y[3] + _31 * z[3] + _41;
        // 'yp' element (4x)
        yp[0] = _12 * x[0] + _22 * y[0] + _32 * z[0] + _42;
        yp[1] = _12 * x[1] + _22 * y[1] + _32 * z[1] + _42;
        yp[2] = _12 * x[2] + _22 * y[2] + _32 * z[2] + _42;
        yp[3] = _12 * x[3] + _22 * y[3] + _32 * z[3] + _42;
        // 'zp' element (4x)
        zp[0] = _13 * x[0] + _23 * y[0] + _33 * z[0] + _43;
        zp[1] = _13 * x[1] + _23 * y[1] + _33 * z[1] + _43;
        zp[2] = _13 * x[2] + _23 * y[2] + _33 * z[2] + _43;
        zp[3] = _13 * x[3] + _23 * y[3] + _33 * z[3] + _43;
        // 'wp' element (4x)
        wp[0] = _14 * x[0] + _24 * y[0] + _34 * z[0] + _44;
        wp[1] = _14 * x[1] + _24 * y[1] + _34 * z[1] + _44;
        wp[2] = _14 * x[2] + _24 * y[2] + _34 * z[2] + _44;
        wp[3] = _14 * x[3] + _24 * y[3] + _34 * z[3] + _44;
        // homogenezation and storing
        // 'w' element (4x)
        rhw[0] = 1.0f / wp[0];
        rhw[1] = 1.0f / wp[1];
        rhw[2] = 1.0f / wp[2];
        rhw[3] = 1.0f / wp[3];
        // 1st element
        lpDst[i4].x = xp[0] * rhw[0];
        lpDst[i4].y = yp[0] * rhw[0];
        lpDst[i4].z = zp[0] * rhw[0];
        lpDst[i4++].w = rhw[0];
        // 2nd element
        lpDst[i4].x = xp[1] * rhw[1];
        lpDst[i4].y = yp[1] * rhw[1];
        lpDst[i4].z = zp[1] * rhw[1];
        lpDst[i4++].w = rhw[1];
        // 3rd element
        lpDst[i4].x = xp[2] * rhw[2];
        lpDst[i4].y = yp[2] * rhw[2];
        lpDst[i4].z = zp[2] * rhw[2];
        lpDst[i4++].w = rhw[2];
        // 4th element
        lpDst[i4].x = xp[3] * rhw[3];
        lpDst[i4].y = yp[3] * rhw[3];
        lpDst[i4].z = zp[3] * rhw[3];
        lpDst[i4++].w = rhw[3];
    }

    return TRV_VISIBLE_NOCLIP;
}
/////////////////////////////////////////////////////////////////////////////////////
// int CTLPipelineFPU::TransformVertices( const SHMatrix& matrix, SXYZW* lpSrc, SXYZW* lpDst, const
// int nCount ) transform vertices from SXYZW to SXYZW with homogenezation and w/o clip check
// assuming, that 'w' component in source vertices are equal to '1'
int CTLPipelineFPU::TransformVertices(const SHMatrix &matrix, SXYZW *lpSrc, SXYZW *lpDst,
                                      const int nCount)
{
    const float _11 = matrix._11, _12 = matrix._21, _13 = matrix._31, _14 = matrix._41;
    const float _21 = matrix._12, _22 = matrix._22, _23 = matrix._32, _24 = matrix._42;
    const float _31 = matrix._13, _32 = matrix._23, _33 = matrix._33, _34 = matrix._43;
    const float _41 = matrix._14, _42 = matrix._24, _43 = matrix._34, _44 = matrix._44;
    float xp, yp, zp, wp, rhw;
    float x, y, z;
    int i;

    // transform 'multiple-to-4' amount of vertices
    for (i = 0; i < (nCount & (~0x3)); i++)
    {
        // 1st
        // load data
        x = lpSrc[i].x;
        y = lpSrc[i].y;
        z = lpSrc[i].z;
        // transform
        xp = _11 * x + _21 * y + _31 * z + _41;
        yp = _12 * x + _22 * y + _32 * z + _42;
        zp = _13 * x + _23 * y + _33 * z + _43;
        wp = _14 * x + _24 * y + _34 * z + _44;
        // homogenezation and storing
        lpDst[i].w = rhw = 1.0f / wp;
        lpDst[i].x = xp * rhw;
        lpDst[i].y = yp * rhw;
        lpDst[i].z = zp * rhw;
        i++;
        // 2nd
        // load data
        x = lpSrc[i].x;
        y = lpSrc[i].y;
        z = lpSrc[i].z;
        // transform
        xp = _11 * x + _21 * y + _31 * z + _41;
        yp = _12 * x + _22 * y + _32 * z + _42;
        zp = _13 * x + _23 * y + _33 * z + _43;
        wp = _14 * x + _24 * y + _34 * z + _44;
        // homogenezation and storing
        lpDst[i].w = rhw = 1.0f / wp;
        lpDst[i].x = xp * rhw;
        lpDst[i].y = yp * rhw;
        lpDst[i].z = zp * rhw;
        i++;
        // 3rd
        // load data
        x = lpSrc[i].x;
        y = lpSrc[i].y;
        z = lpSrc[i].z;
        // transform
        xp = _11 * x + _21 * y + _31 * z + _41;
        yp = _12 * x + _22 * y + _32 * z + _42;
        zp = _13 * x + _23 * y + _33 * z + _43;
        wp = _14 * x + _24 * y + _34 * z + _44;
        // homogenezation and storing
        lpDst[i].w = rhw = 1.0f / wp;
        lpDst[i].x = xp * rhw;
        lpDst[i].y = yp * rhw;
        lpDst[i].z = zp * rhw;
        i++;
        // 4th
        // load data
        x = lpSrc[i].x;
        y = lpSrc[i].y;
        z = lpSrc[i].z;
        // transform
        xp = _11 * x + _21 * y + _31 * z + _41;
        yp = _12 * x + _22 * y + _32 * z + _42;
        zp = _13 * x + _23 * y + _33 * z + _43;
        wp = _14 * x + _24 * y + _34 * z + _44;
        // homogenezation and storing
        lpDst[i].w = rhw = 1.0f / wp;
        lpDst[i].x = xp * rhw;
        lpDst[i].y = yp * rhw;
        lpDst[i].z = zp * rhw;
    }
    // transform the rest of vertices
    for (; i < nCount; i++)
    {
        // load data
        x = lpSrc[i].x;
        y = lpSrc[i].y;
        z = lpSrc[i].z;
        // transform
        xp = _11 * x + _21 * y + _31 * z + _41;
        yp = _12 * x + _22 * y + _32 * z + _42;
        zp = _13 * x + _23 * y + _33 * z + _43;
        wp = _14 * x + _24 * y + _34 * z + _44;
        // homogenezation and storing
        lpDst[i].w = rhw = 1.0f / wp;
        lpDst[i].x = xp * rhw;
        lpDst[i].y = yp * rhw;
        lpDst[i].z = zp * rhw;
    }

    return TRV_VISIBLE_NOCLIP;
}
/////////////////////////////////////////////////////////////////////////////////////
// transform vertices from SXYZW4 to STLVertex1 (with data swizzling) with homogenezation and clip
// check assuming, that 'w' component in source vertices are equal to '1' return values:
// 		TRV_INVISIBLE      <= all vertices clipped. data set invisible. 'dst' vertices are
// in projection space
//		TRV_VISIBLE_CLIP   <= vertices visible, but some of them are clipped. 'dst' vertices
// are in projection space (for further clipping) 		TRV_VISIBLE_NOCLIP <= all vertices
// are visible w/o clipping. 'dst' vertices are in screen space
int CTLPipelineFPU::TransformVertices(const SHMatrix &matrix, SXYZW4 *lpSrc, STLVertex1 *lpDst,
                                      BYTE *lpClip, DWORD *speculars, const int nCount)
{
    const float _11 = matrix._11, _12 = matrix._21, _13 = matrix._31, _14 = matrix._41;
    const float _21 = matrix._12, _22 = matrix._22, _23 = matrix._32, _24 = matrix._42;
    const float _31 = matrix._13, _32 = matrix._23, _33 = matrix._33, _34 = matrix._43;
    const float _41 = matrix._14, _42 = matrix._24, _43 = matrix._34, _44 = matrix._44;
    int nRetFlag = TRV_VISIBLE_NOCLIP, nRetFlag1 = TRV_CLIP_ALL;
    D3DVALUE x, y, z, xp, yp, zp, wp, wp1;
    int i, i4;

    for (i4 = 0, i = 0; i < nCount; i++)
    {
        // first element
        x = lpSrc[i].x[0];
        y = lpSrc[i].y[0];
        z = lpSrc[i].z[0];
        //
        lpDst[i4].sx = xp = _11 * x + _21 * y + _31 * z + _41;
        lpDst[i4].sy = yp = _12 * x + _22 * y + _32 * z + _42;
        lpDst[i4].sz = zp = _13 * x + _23 * y + _33 * z + _43;
        lpDst[i4].rhw = wp = _14 * x + _24 * y + _34 * z + _44;
        lpDst[i4].specular = speculars[i4];
        // load data for second element
        x = lpSrc[i].x[1];
        // compose flags for first element
        nRetFlag |= lpClip[i4] = CheckForViewingFrustum(xp, yp, zp, wp);
        nRetFlag1 &= lpClip[i4++];
        // second element
        y = lpSrc[i].y[1];
        z = lpSrc[i].z[1];
        lpDst[i4].sx = xp = _11 * x + _21 * y + _31 * z + _41;
        lpDst[i4].sy = yp = _12 * x + _22 * y + _32 * z + _42;
        lpDst[i4].sz = zp = _13 * x + _23 * y + _33 * z + _43;
        lpDst[i4].rhw = wp = _14 * x + _24 * y + _34 * z + _44;
        lpDst[i4].specular = speculars[i4];
        // load data for third element
        x = lpSrc[i].x[2];
        // compose flags for second element
        nRetFlag |= lpClip[i4] = CheckForViewingFrustum(xp, yp, zp, wp);
        nRetFlag1 &= lpClip[i4++];
        // third element
        y = lpSrc[i].y[2];
        z = lpSrc[i].z[2];
        lpDst[i4].sx = xp = _11 * x + _21 * y + _31 * z + _41;
        lpDst[i4].sy = yp = _12 * x + _22 * y + _32 * z + _42;
        lpDst[i4].sz = zp = _13 * x + _23 * y + _33 * z + _43;
        lpDst[i4].rhw = wp = _14 * x + _24 * y + _34 * z + _44;
        lpDst[i4].specular = speculars[i4];
        // load data for forth element
        x = lpSrc[i].x[3];
        // compose flags for third element
        nRetFlag |= lpClip[i4] = CheckForViewingFrustum(xp, yp, zp, wp);
        nRetFlag1 &= lpClip[i4++];
        // forth element
        y = lpSrc[i].y[3];
        z = lpSrc[i].z[3];
        lpDst[i4].sx = xp = _11 * x + _21 * y + _31 * z + _41;
        lpDst[i4].sy = yp = _12 * x + _22 * y + _32 * z + _42;
        lpDst[i4].sz = zp = _13 * x + _23 * y + _33 * z + _43;
        lpDst[i4].rhw = wp = _14 * x + _24 * y + _34 * z + _44;
        lpDst[i4].specular = speculars[i4];
        // compose flags for 4th element
        nRetFlag |= lpClip[i4] = CheckForViewingFrustum(xp, yp, zp, wp);
        nRetFlag1 &= lpClip[i4++];
    }

    if (nRetFlag1)
        return TRV_INVISIBLE;
    else if (nRetFlag != TRV_VISIBLE_NOCLIP)
        return TRV_VISIBLE_CLIP;
    else
    {
        register float fpClipHW1 = fpClipHW;
        register float fpClipHH1 = fpClipHH;
        for (i4 = 0, i = 0; i < nCount; i++)
        {
            // 0th element
            wp1 = 1.0f / lpDst[i4].rhw;
            lpDst[i4].sx = (1.0f + (lpDst[i4].sx * wp1)) * fpClipHW1;
            lpDst[i4].sy = (1.0f - (lpDst[i4].sy * wp1)) * fpClipHH1;
            lpDst[i4].sz = lpDst[i4].sz * wp1;
            lpDst[i4++].rhw = wp1;
            // 1st element
            wp1 = 1.0f / lpDst[i4].rhw;
            lpDst[i4].sx = (1.0f + (lpDst[i4].sx * wp1)) * fpClipHW1;
            lpDst[i4].sy = (1.0f - (lpDst[i4].sy * wp1)) * fpClipHH1;
            lpDst[i4].sz = lpDst[i4].sz * wp1;
            lpDst[i4++].rhw = wp1;
            // 2nd element
            wp1 = 1.0f / lpDst[i4].rhw;
            lpDst[i4].sx = (1.0f + (lpDst[i4].sx * wp1)) * fpClipHW1;
            lpDst[i4].sy = (1.0f - (lpDst[i4].sy * wp1)) * fpClipHH1;
            lpDst[i4].sz = lpDst[i4].sz * wp1;
            lpDst[i4++].rhw = wp1;
            // 3rd element
            wp1 = 1.0f / lpDst[i4].rhw;
            lpDst[i4].sx = (1.0f + (lpDst[i4].sx * wp1)) * fpClipHW1;
            lpDst[i4].sy = (1.0f - (lpDst[i4].sy * wp1)) * fpClipHH1;
            lpDst[i4].sz = lpDst[i4].sz * wp1;
            lpDst[i4++].rhw = wp1;
        }

        return TRV_VISIBLE_NOCLIP;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// transform vertices from SXYZW4 to STLVertex1 (with data swizzling) with homogenezation and clip
// check assuming, that 'w' component in source vertices are equal to '1' return values:
// 		TRV_INVISIBLE      <= all vertices clipped. data set invisible. 'dst' vertices are
// in projection space
//		TRV_VISIBLE_CLIP   <= vertices visible, but some of them are clipped. 'dst' vertices
// are in projection space (for further clipping) 		TRV_VISIBLE_NOCLIP <= all vertices
// are visible w/o clipping. 'dst' vertices are in screen space
int CTLPipelineFPU::TransformVertices(const SHMatrix &matrix, SXYZW4 *lpSrc, STLVertex1 *lpDst,
                                      BYTE *lpClip, const int nCount)
{
    const float _11 = matrix._11, _12 = matrix._21, _13 = matrix._31, _14 = matrix._41;
    const float _21 = matrix._12, _22 = matrix._22, _23 = matrix._32, _24 = matrix._42;
    const float _31 = matrix._13, _32 = matrix._23, _33 = matrix._33, _34 = matrix._43;
    const float _41 = matrix._14, _42 = matrix._24, _43 = matrix._34, _44 = matrix._44;
    int nRetFlag = TRV_VISIBLE_NOCLIP, nRetFlag1 = TRV_CLIP_ALL;
    D3DVALUE x, y, z, xp, yp, zp, wp, wp1;
    int i, i4;

    for (i4 = 0, i = 0; i < nCount; i++)
    {
        // first element
        x = lpSrc[i].x[0];
        y = lpSrc[i].y[0];
        z = lpSrc[i].z[0];
        //
        lpDst[i4].sx = xp = _11 * x + _21 * y + _31 * z + _41;
        lpDst[i4].sy = yp = _12 * x + _22 * y + _32 * z + _42;
        lpDst[i4].sz = zp = _13 * x + _23 * y + _33 * z + _43;
        lpDst[i4].rhw = wp = _14 * x + _24 * y + _34 * z + _44;
        // load data for second element
        x = lpSrc[i].x[1];
        // compose flags for first element
        nRetFlag |= lpClip[i4] = CheckForViewingFrustum(xp, yp, zp, wp);
        nRetFlag1 &= lpClip[i4++];
        // second element
        y = lpSrc[i].y[1];
        z = lpSrc[i].z[1];
        lpDst[i4].sx = xp = _11 * x + _21 * y + _31 * z + _41;
        lpDst[i4].sy = yp = _12 * x + _22 * y + _32 * z + _42;
        lpDst[i4].sz = zp = _13 * x + _23 * y + _33 * z + _43;
        lpDst[i4].rhw = wp = _14 * x + _24 * y + _34 * z + _44;
        // load data for third element
        x = lpSrc[i].x[2];
        // compose flags for second element
        nRetFlag |= lpClip[i4] = CheckForViewingFrustum(xp, yp, zp, wp);
        nRetFlag1 &= lpClip[i4++];
        // third element
        y = lpSrc[i].y[2];
        z = lpSrc[i].z[2];
        lpDst[i4].sx = xp = _11 * x + _21 * y + _31 * z + _41;
        lpDst[i4].sy = yp = _12 * x + _22 * y + _32 * z + _42;
        lpDst[i4].sz = zp = _13 * x + _23 * y + _33 * z + _43;
        lpDst[i4].rhw = wp = _14 * x + _24 * y + _34 * z + _44;
        // load data for forth element
        x = lpSrc[i].x[3];
        // compose flags for third element
        nRetFlag |= lpClip[i4] = CheckForViewingFrustum(xp, yp, zp, wp);
        nRetFlag1 &= lpClip[i4++];
        // forth element
        y = lpSrc[i].y[3];
        z = lpSrc[i].z[3];
        lpDst[i4].sx = xp = _11 * x + _21 * y + _31 * z + _41;
        lpDst[i4].sy = yp = _12 * x + _22 * y + _32 * z + _42;
        lpDst[i4].sz = zp = _13 * x + _23 * y + _33 * z + _43;
        lpDst[i4].rhw = wp = _14 * x + _24 * y + _34 * z + _44;
        nRetFlag |= lpClip[i4] = CheckForViewingFrustum(xp, yp, zp, wp);
        nRetFlag1 &= lpClip[i4++];
    }

    if (nRetFlag1)
        return TRV_INVISIBLE;
    else if (nRetFlag != TRV_VISIBLE_NOCLIP)
        return TRV_VISIBLE_CLIP;
    else
    {
        register float fpClipHW1 = fpClipHW;
        register float fpClipHH1 = fpClipHH;
        for (i4 = 0, i = 0; i < nCount; i++)
        {
            // 0th element
            wp1 = 1.0f / lpDst[i4].rhw;
            lpDst[i4].sx = (1.0f + (lpDst[i4].sx * wp1)) * fpClipHW1;
            lpDst[i4].sy = (1.0f - (lpDst[i4].sy * wp1)) * fpClipHH1;
            lpDst[i4].sz = lpDst[i4].sz * wp1;
            lpDst[i4++].rhw = wp1;
            // 1st element
            wp1 = 1.0f / lpDst[i4].rhw;
            lpDst[i4].sx = (1.0f + (lpDst[i4].sx * wp1)) * fpClipHW1;
            lpDst[i4].sy = (1.0f - (lpDst[i4].sy * wp1)) * fpClipHH1;
            lpDst[i4].sz = lpDst[i4].sz * wp1;
            lpDst[i4++].rhw = wp1;
            // 2nd element
            wp1 = 1.0f / lpDst[i4].rhw;
            lpDst[i4].sx = (1.0f + (lpDst[i4].sx * wp1)) * fpClipHW1;
            lpDst[i4].sy = (1.0f - (lpDst[i4].sy * wp1)) * fpClipHH1;
            lpDst[i4].sz = lpDst[i4].sz * wp1;
            lpDst[i4++].rhw = wp1;
            // 3rd element
            wp1 = 1.0f / lpDst[i4].rhw;
            lpDst[i4].sx = (1.0f + (lpDst[i4].sx * wp1)) * fpClipHW1;
            lpDst[i4].sy = (1.0f - (lpDst[i4].sy * wp1)) * fpClipHH1;
            lpDst[i4].sz = lpDst[i4].sz * wp1;
            lpDst[i4++].rhw = wp1;
        }

        return TRV_VISIBLE_NOCLIP;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// transform vertices from SXYZW4 to SXYZW4 with homogenezation and clip check
// assuming, that 'w' component in source vertices are equal to '1'
// return values:
// 		TRV_INVISIBLE      <= all vertices clipped. data set invisible. 'dst' vertices are
// in projection space
//		TRV_VISIBLE_CLIP   <= vertices visible, but some of them are clipped. 'dst' vertices
// are in projection space (for further clipping) 		TRV_VISIBLE_NOCLIP <= all vertices
// are visible w/o clipping. 'dst' vertices are in screen space
int CTLPipelineFPU::TransformVertices(const SHMatrix &matrix, SXYZW4 *lpSrc, SXYZW4 *lpDst,
                                      BYTE *lpClip, const int nCount)
{
    const float _11 = matrix._11, _12 = matrix._21, _13 = matrix._31, _14 = matrix._41;
    const float _21 = matrix._12, _22 = matrix._22, _23 = matrix._32, _24 = matrix._42;
    const float _31 = matrix._13, _32 = matrix._23, _33 = matrix._33, _34 = matrix._43;
    const float _41 = matrix._14, _42 = matrix._24, _43 = matrix._34, _44 = matrix._44;
    int nRetFlag = TRV_VISIBLE_NOCLIP, nRetFlag1 = TRV_CLIP_ALL;
    float xp[4], yp[4], zp[4], wp[4], rhw[4];
    float x[4], y[4], z[4];
    int i, i4;
    // transform vertices to projection space and perform clip check
    for (i = 0, i4 = 0; i < nCount; i++)
    {
        // load data
        // x, y, z
        Copy16Bytes(x, lpSrc[i].x);
        Copy16Bytes(y, lpSrc[i].y);
        Copy16Bytes(z, lpSrc[i].z);
        // transforming and storing
        // 'xp' element (4x)
        xp[0] = _11 * x[0] + _21 * y[0] + _31 * z[0] + _41;
        xp[1] = _11 * x[1] + _21 * y[1] + _31 * z[1] + _41;
        xp[2] = _11 * x[2] + _21 * y[2] + _31 * z[2] + _41;
        xp[3] = _11 * x[3] + _21 * y[3] + _31 * z[3] + _41;
        Copy16Bytes(lpDst[i].x, xp);
        // 'yp' element (4x)
        yp[0] = _12 * x[0] + _22 * y[0] + _32 * z[0] + _42;
        yp[1] = _12 * x[1] + _22 * y[1] + _32 * z[1] + _42;
        yp[2] = _12 * x[2] + _22 * y[2] + _32 * z[2] + _42;
        yp[3] = _12 * x[3] + _22 * y[3] + _32 * z[3] + _42;
        Copy16Bytes(lpDst[i].y, yp);
        // 'zp' element (4x)
        zp[0] = _13 * x[0] + _23 * y[0] + _33 * z[0] + _43;
        zp[1] = _13 * x[1] + _23 * y[1] + _33 * z[1] + _43;
        zp[2] = _13 * x[2] + _23 * y[2] + _33 * z[2] + _43;
        zp[3] = _13 * x[3] + _23 * y[3] + _33 * z[3] + _43;
        Copy16Bytes(lpDst[i].z, zp);
        // 'wp' element (4x)
        wp[0] = _14 * x[0] + _24 * y[0] + _34 * z[0] + _44;
        wp[1] = _14 * x[1] + _24 * y[1] + _34 * z[1] + _44;
        wp[2] = _14 * x[2] + _24 * y[2] + _34 * z[2] + _44;
        wp[3] = _14 * x[3] + _24 * y[3] + _34 * z[3] + _44;
        Copy16Bytes(lpDst[i].w, wp);
        // viewing frustum clip check
        nRetFlag |= lpClip[i4] = CheckForViewingFrustum(xp[0], yp[0], zp[0], wp[0]);
        nRetFlag1 &= lpClip[i4++];
        nRetFlag |= lpClip[i4] = CheckForViewingFrustum(xp[1], yp[1], zp[1], wp[1]);
        nRetFlag1 &= lpClip[i4++];
        nRetFlag |= lpClip[i4] = CheckForViewingFrustum(xp[2], yp[2], zp[2], wp[2]);
        nRetFlag1 &= lpClip[i4++];
        nRetFlag |= lpClip[i4] = CheckForViewingFrustum(xp[3], yp[3], zp[3], wp[3]);
        nRetFlag1 &= lpClip[i4++];
    }
    // general clip check and post-transform (if no clips)
    if (nRetFlag1)
        return TRV_INVISIBLE;
    else if (nRetFlag != TRV_VISIBLE_NOCLIP)
        return TRV_VISIBLE_CLIP;
    else
    {
        // cast from projection space to screen space (with homogenezation)
        register float fpClipHW1 = fpClipHW;
        register float fpClipHH1 = fpClipHH;
        for (i = 0; i < nCount; i++)
        {
            // load data
            // x, y, z
            Copy16Bytes(x, lpSrc[i].x);
            Copy16Bytes(y, lpSrc[i].y);
            Copy16Bytes(wp, lpSrc[i].w);
            // 'w' element (4x)
            lpDst[i].w[0] = rhw[0] = 1.0f / wp[0];
            lpDst[i].w[1] = rhw[1] = 1.0f / wp[1];
            lpDst[i].w[2] = rhw[2] = 1.0f / wp[2];
            lpDst[i].w[3] = rhw[3] = 1.0f / wp[3];
            // 'x' element (4x)
            lpDst[i].x[0] = (1.0f + x[0] * rhw[0]) * fpClipHW1;
            lpDst[i].x[1] = (1.0f + x[1] * rhw[1]) * fpClipHW1;
            lpDst[i].x[2] = (1.0f + x[2] * rhw[2]) * fpClipHW1;
            lpDst[i].x[3] = (1.0f + x[3] * rhw[3]) * fpClipHW1;
            // 'y' element (4x)
            lpDst[i].y[0] = (1.0f - y[0] * rhw[0]) * fpClipHH1;
            lpDst[i].y[1] = (1.0f - y[1] * rhw[1]) * fpClipHH1;
            lpDst[i].y[2] = (1.0f - y[2] * rhw[2]) * fpClipHH1;
            lpDst[i].y[3] = (1.0f - y[3] * rhw[3]) * fpClipHH1;
            // 'z' element (4x)
            lpDst[i].z[0] *= rhw[0];
            lpDst[i].z[1] *= rhw[1];
            lpDst[i].z[2] *= rhw[2];
            lpDst[i].z[3] *= rhw[3];
        }

        return TRV_VISIBLE_NOCLIP;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// transform vertices from SXYZW4 to SXYZW (with data swizzling) with homogenezation and clip check
// assuming, that 'w' component in source vertices are equal to '1'
// return values:
// 		TRV_INVISIBLE      <= all vertices clipped. data set invisible. 'dst' vertices are
// in projection space
//		TRV_VISIBLE_CLIP   <= vertices visible, but some of them are clipped. 'dst' vertices
// are in projection space (for further clipping) 		TRV_VISIBLE_NOCLIP <= all vertices
// are visible w/o clipping. 'dst' vertices are in screen space
int CTLPipelineFPU::TransformVertices(const SHMatrix &matrix, SXYZW4 *lpSrc, SXYZW *lpDst,
                                      BYTE *lpClip, const int nCount)
{
    const float _11 = matrix._11, _12 = matrix._21, _13 = matrix._31, _14 = matrix._41;
    const float _21 = matrix._12, _22 = matrix._22, _23 = matrix._32, _24 = matrix._42;
    const float _31 = matrix._13, _32 = matrix._23, _33 = matrix._33, _34 = matrix._43;
    const float _41 = matrix._14, _42 = matrix._24, _43 = matrix._34, _44 = matrix._44;
    int nRetFlag = TRV_VISIBLE_NOCLIP, nRetFlag1 = TRV_CLIP_ALL;
    D3DVALUE x, y, z, xp, yp, zp, wp, wp1;
    int i, i4;

    for (i4 = 0, i = 0; i < nCount; i++)
    {
        // first element
        x = lpSrc[i].x[0];
        y = lpSrc[i].y[0];
        z = lpSrc[i].z[0];
        //
        lpDst[i4].x = xp = _11 * x + _21 * y + _31 * z + _41;
        lpDst[i4].y = yp = _12 * x + _22 * y + _32 * z + _42;
        lpDst[i4].z = zp = _13 * x + _23 * y + _33 * z + _43;
        lpDst[i4].w = wp = _14 * x + _24 * y + _34 * z + _44;
        // load data for second element
        x = lpSrc[i].x[1];
        // compose flags for first element
        nRetFlag |= lpClip[i4] = CheckForViewingFrustum(xp, yp, zp, wp);
        nRetFlag1 &= lpClip[i4++];
        // second element
        y = lpSrc[i].y[1];
        z = lpSrc[i].z[1];
        lpDst[i4].x = xp = _11 * x + _21 * y + _31 * z + _41;
        lpDst[i4].y = yp = _12 * x + _22 * y + _32 * z + _42;
        lpDst[i4].z = zp = _13 * x + _23 * y + _33 * z + _43;
        lpDst[i4].w = wp = _14 * x + _24 * y + _34 * z + _44;
        // load data for third element
        x = lpSrc[i].x[2];
        // compose flags for second element
        nRetFlag |= lpClip[i4] = CheckForViewingFrustum(xp, yp, zp, wp);
        nRetFlag1 &= lpClip[i4++];
        // third element
        y = lpSrc[i].y[2];
        z = lpSrc[i].z[2];
        lpDst[i4].x = xp = _11 * x + _21 * y + _31 * z + _41;
        lpDst[i4].y = yp = _12 * x + _22 * y + _32 * z + _42;
        lpDst[i4].z = zp = _13 * x + _23 * y + _33 * z + _43;
        lpDst[i4].w = wp = _14 * x + _24 * y + _34 * z + _44;
        // load data for forth element
        x = lpSrc[i].x[3];
        // compose flags for third element
        nRetFlag |= lpClip[i4] = CheckForViewingFrustum(xp, yp, zp, wp);
        nRetFlag1 &= lpClip[i4++];
        // forth element
        y = lpSrc[i].y[3];
        z = lpSrc[i].z[3];
        lpDst[i4].x = xp = _11 * x + _21 * y + _31 * z + _41;
        lpDst[i4].y = yp = _12 * x + _22 * y + _32 * z + _42;
        lpDst[i4].z = zp = _13 * x + _23 * y + _33 * z + _43;
        lpDst[i4].w = wp = _14 * x + _24 * y + _34 * z + _44;
        nRetFlag |= lpClip[i4] = CheckForViewingFrustum(xp, yp, zp, wp);
        nRetFlag1 &= lpClip[i4++];
    }

    if (nRetFlag1)
        return TRV_INVISIBLE;
    else if (nRetFlag != TRV_VISIBLE_NOCLIP)
        return TRV_VISIBLE_CLIP;
    else
    {
        register float fpClipHW1 = fpClipHW;
        register float fpClipHH1 = fpClipHH;
        for (i4 = 0, i = 0; i < nCount; i++)
        {
            // 0th element
            wp1 = 1.0f / lpDst[i4].w;
            lpDst[i4].x = (1.0f + (lpDst[i4].x * wp1)) * fpClipHW1;
            lpDst[i4].y = (1.0f - (lpDst[i4].y * wp1)) * fpClipHH1;
            lpDst[i4].z = lpDst[i4].z * wp1;
            lpDst[i4++].w = wp1;
            // 1st element
            wp1 = 1.0f / lpDst[i4].w;
            lpDst[i4].x = (1.0f + (lpDst[i4].x * wp1)) * fpClipHW1;
            lpDst[i4].y = (1.0f - (lpDst[i4].y * wp1)) * fpClipHH1;
            lpDst[i4].z = lpDst[i4].z * wp1;
            lpDst[i4++].w = wp1;
            // 2nd element
            wp1 = 1.0f / lpDst[i4].w;
            lpDst[i4].x = (1.0f + (lpDst[i4].x * wp1)) * fpClipHW1;
            lpDst[i4].y = (1.0f - (lpDst[i4].y * wp1)) * fpClipHH1;
            lpDst[i4].z = lpDst[i4].z * wp1;
            lpDst[i4++].w = wp1;
            // 3rd element
            wp1 = 1.0f / lpDst[i4].w;
            lpDst[i4].x = (1.0f + (lpDst[i4].x * wp1)) * fpClipHW1;
            lpDst[i4].y = (1.0f - (lpDst[i4].y * wp1)) * fpClipHH1;
            lpDst[i4].z = lpDst[i4].z * wp1;
            lpDst[i4++].w = wp1;
        }

        return TRV_VISIBLE_NOCLIP;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// int CTLPipelineFPU::TransformVertices( const SHMatrix& matrix, SXYZW* lpSrc, SXYZW* lpDst, BYTE*
// lpClip, const int nCount ) transform vertices from SXYZW to SXYZW with homogenezation and clip
// check assuming, that 'w' component in source vertices are equal to '1' return values:
// 		TRV_INVISIBLE      <= all vertices clipped. data set invisible. 'dst' vertices are
// in projection space
//		TRV_VISIBLE_CLIP   <= vertices visible, but some of them are clipped. 'dst' vertices
// are in projection space (for further clipping) 		TRV_VISIBLE_NOCLIP <= all vertices
// are visible w/o clipping. 'dst' vertices are in screen space
int CTLPipelineFPU::TransformVertices(const SHMatrix &matrix, SXYZW *lpSrc, SXYZW *lpDst,
                                      BYTE *lpClip, const int nCount)
{
    const float _11 = matrix._11, _12 = matrix._21, _13 = matrix._31, _14 = matrix._41;
    const float _21 = matrix._12, _22 = matrix._22, _23 = matrix._32, _24 = matrix._42;
    const float _31 = matrix._13, _32 = matrix._23, _33 = matrix._33, _34 = matrix._43;
    const float _41 = matrix._14, _42 = matrix._24, _43 = matrix._34, _44 = matrix._44;
    int nRetFlag = TRV_VISIBLE_NOCLIP, nRetFlag1 = TRV_CLIP_ALL;
    float xp, yp, zp, wp, rhw;
    float x, y, z;
    int i;

    // transform 'multiple-to-4' amount of vertices
    for (i = 0; i < (nCount & (~0x3)); i++)
    {
        // 1st
        // load data
        x = lpSrc[i].x;
        y = lpSrc[i].y;
        z = lpSrc[i].z;
        // transform, clip check and data storing
        lpDst[i].x = xp = _11 * x + _21 * y + _31 * z + _41;
        lpDst[i].y = yp = _12 * x + _22 * y + _32 * z + _42;
        lpDst[i].z = zp = _13 * x + _23 * y + _33 * z + _43;
        lpDst[i].w = wp = _14 * x + _24 * y + _34 * z + _44;
        nRetFlag |= lpClip[i] = CheckForViewingFrustum(xp, yp, zp, wp);
        nRetFlag1 &= lpClip[i];
        i++;
        // 2nd
        // load data
        x = lpSrc[i].x;
        y = lpSrc[i].y;
        z = lpSrc[i].z;
        // transform, clip check and data storing
        lpDst[i].x = xp = _11 * x + _21 * y + _31 * z + _41;
        lpDst[i].y = yp = _12 * x + _22 * y + _32 * z + _42;
        lpDst[i].z = zp = _13 * x + _23 * y + _33 * z + _43;
        lpDst[i].w = wp = _14 * x + _24 * y + _34 * z + _44;
        nRetFlag |= lpClip[i] = CheckForViewingFrustum(xp, yp, zp, wp);
        nRetFlag1 &= lpClip[i];
        i++;
        // 3rd
        // load data
        x = lpSrc[i].x;
        y = lpSrc[i].y;
        z = lpSrc[i].z;
        // transform, clip check and data storing
        lpDst[i].x = xp = _11 * x + _21 * y + _31 * z + _41;
        lpDst[i].y = yp = _12 * x + _22 * y + _32 * z + _42;
        lpDst[i].z = zp = _13 * x + _23 * y + _33 * z + _43;
        lpDst[i].w = wp = _14 * x + _24 * y + _34 * z + _44;
        nRetFlag |= lpClip[i] = CheckForViewingFrustum(xp, yp, zp, wp);
        nRetFlag1 &= lpClip[i];
        i++;
        // 4th
        // load data
        x = lpSrc[i].x;
        y = lpSrc[i].y;
        z = lpSrc[i].z;
        // transform, clip check and data storing
        lpDst[i].x = xp = _11 * x + _21 * y + _31 * z + _41;
        lpDst[i].y = yp = _12 * x + _22 * y + _32 * z + _42;
        lpDst[i].z = zp = _13 * x + _23 * y + _33 * z + _43;
        lpDst[i].w = wp = _14 * x + _24 * y + _34 * z + _44;
        nRetFlag |= lpClip[i] = CheckForViewingFrustum(xp, yp, zp, wp);
        nRetFlag1 &= lpClip[i];
    }
    // transform the rest of vertices
    for (; i < nCount; i++)
    {
        // load data
        x = lpSrc[i].x;
        y = lpSrc[i].y;
        z = lpSrc[i].z;
        // transform, clip check and data storing
        lpDst[i].x = xp = _11 * x + _21 * y + _31 * z + _41;
        lpDst[i].y = yp = _12 * x + _22 * y + _32 * z + _42;
        lpDst[i].z = zp = _13 * x + _23 * y + _33 * z + _43;
        lpDst[i].w = wp = _14 * x + _24 * y + _34 * z + _44;
        nRetFlag |= lpClip[i] = CheckForViewingFrustum(xp, yp, zp, wp);
        nRetFlag1 &= lpClip[i];
    }
    // general clip check and post-transform (if no clips)
    if (nRetFlag1)
        return TRV_INVISIBLE;
    else if (nRetFlag != TRV_VISIBLE_NOCLIP)
        return TRV_VISIBLE_CLIP;
    else
    {
        // cast vertices from projection space to screen space (with homogenezation)
        const register float fpClipHW1 = fpClipHW;
        const register float fpClipHH1 = fpClipHH;
        // transform 'multiple-to-4' amount of vertices
        for (i = 0; i < (nCount & (~0x3)); i++)
        {
            // 1st element
            lpDst[i].w = rhw = 1.0f / lpDst[i].w;
            lpDst[i].x = (1.0f + (lpDst[i].x * rhw)) * fpClipHW1;
            lpDst[i].y = (1.0f - (lpDst[i].y * rhw)) * fpClipHH1;
            lpDst[i].z *= rhw;
            i++;
            // 2nd element
            lpDst[i].w = rhw = 1.0f / lpDst[i].w;
            lpDst[i].x = (1.0f + (lpDst[i].x * rhw)) * fpClipHW1;
            lpDst[i].y = (1.0f - (lpDst[i].y * rhw)) * fpClipHH1;
            lpDst[i].z *= rhw;
            i++;
            // 3rd element
            lpDst[i].w = rhw = 1.0f / lpDst[i].w;
            lpDst[i].x = (1.0f + (lpDst[i].x * rhw)) * fpClipHW1;
            lpDst[i].y = (1.0f - (lpDst[i].y * rhw)) * fpClipHH1;
            lpDst[i].z *= rhw;
            i++;
            // 4th element
            lpDst[i].w = rhw = 1.0f / lpDst[i].w;
            lpDst[i].x = (1.0f + (lpDst[i].x * rhw)) * fpClipHW1;
            lpDst[i].y = (1.0f - (lpDst[i].y * rhw)) * fpClipHH1;
            lpDst[i].z *= rhw;
        }
        // transform the rest of vertices
        for (; i < nCount; i++)
        {
            lpDst[i].w = rhw = 1.0f / lpDst[i].w;
            lpDst[i].x = (1.0f + (lpDst[i].x * rhw)) * fpClipHW1;
            lpDst[i].y = (1.0f - (lpDst[i].y * rhw)) * fpClipHH1;
            lpDst[i].z *= rhw;
        }

        return TRV_VISIBLE_NOCLIP;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
int CTLPipelineFPU::SimplyTransform(const SHMatrix &matrix, SXYZW *lpSrc, SXYZW *lpDst,
                                    const int nCount)
{
    const float _11 = matrix._11, _12 = matrix._21, _13 = matrix._31, _14 = matrix._41;
    const float _21 = matrix._12, _22 = matrix._22, _23 = matrix._32, _24 = matrix._42;
    const float _31 = matrix._13, _32 = matrix._23, _33 = matrix._33, _34 = matrix._43;
    const float _41 = matrix._14, _42 = matrix._24, _43 = matrix._34, _44 = matrix._44;
    float x, y, z;
    int i;

    // transform 'multiple-to-4' amount of vertices
    for (i = 0; i < (nCount & (~0x3)); i++)
    {
        // 1st
        // load data
        x = lpSrc[i].x;
        y = lpSrc[i].y;
        z = lpSrc[i].z;
        // transform, clip check and data storing
        lpDst[i].x = _11 * x + _21 * y + _31 * z + _41;
        lpDst[i].y = _12 * x + _22 * y + _32 * z + _42;
        lpDst[i].z = _13 * x + _23 * y + _33 * z + _43;
        lpDst[i].w = _14 * x + _24 * y + _34 * z + _44;
        i++;
        // 2nd
        // load data
        x = lpSrc[i].x;
        y = lpSrc[i].y;
        z = lpSrc[i].z;
        // transform, clip check and data storing
        lpDst[i].x = _11 * x + _21 * y + _31 * z + _41;
        lpDst[i].y = _12 * x + _22 * y + _32 * z + _42;
        lpDst[i].z = _13 * x + _23 * y + _33 * z + _43;
        lpDst[i].w = _14 * x + _24 * y + _34 * z + _44;
        i++;
        // 3rd
        // load data
        x = lpSrc[i].x;
        y = lpSrc[i].y;
        z = lpSrc[i].z;
        // transform, clip check and data storing
        lpDst[i].x = _11 * x + _21 * y + _31 * z + _41;
        lpDst[i].y = _12 * x + _22 * y + _32 * z + _42;
        lpDst[i].z = _13 * x + _23 * y + _33 * z + _43;
        lpDst[i].w = _14 * x + _24 * y + _34 * z + _44;
        i++;
        // 4th
        // load data
        x = lpSrc[i].x;
        y = lpSrc[i].y;
        z = lpSrc[i].z;
        // transform, clip check and data storing
        lpDst[i].x = _11 * x + _21 * y + _31 * z + _41;
        lpDst[i].y = _12 * x + _22 * y + _32 * z + _42;
        lpDst[i].z = _13 * x + _23 * y + _33 * z + _43;
        lpDst[i].w = _14 * x + _24 * y + _34 * z + _44;
    }
    // transform the rest of vertices
    for (; i < nCount; i++)
    {
        // load data
        x = lpSrc[i].x;
        y = lpSrc[i].y;
        z = lpSrc[i].z;
        // transform, clip check and data storing
        lpDst[i].x = _11 * x + _21 * y + _31 * z + _41;
        lpDst[i].y = _12 * x + _22 * y + _32 * z + _42;
        lpDst[i].z = _13 * x + _23 * y + _33 * z + _43;
        lpDst[i].w = _14 * x + _24 * y + _34 * z + _44;
    }
    //
    return TRV_VISIBLE_NOCLIP;
}
/////////////////////////////////////////////////////////////////////////////////////
int CTLPipelineFPU::SimplyTransform(const SHMatrix &matrix, SXYZW *lpSrc, SXYZW *lpDst,
                                    BYTE *lpClip, const int nCount)
{
    const float _11 = matrix._11, _12 = matrix._21, _13 = matrix._31, _14 = matrix._41;
    const float _21 = matrix._12, _22 = matrix._22, _23 = matrix._32, _24 = matrix._42;
    const float _31 = matrix._13, _32 = matrix._23, _33 = matrix._33, _34 = matrix._43;
    const float _41 = matrix._14, _42 = matrix._24, _43 = matrix._34, _44 = matrix._44;
    int nRetFlag = TRV_VISIBLE_NOCLIP, nRetFlag1 = TRV_CLIP_ALL;
    float xp, yp, zp, wp;
    float x, y, z;
    int i;

    // transform 'multiple-to-4' amount of vertices
    for (i = 0; i < (nCount & (~0x3)); i++)
    {
        // 1st
        // load data
        x = lpSrc[i].x;
        y = lpSrc[i].y;
        z = lpSrc[i].z;
        // transform, clip check and data storing
        lpDst[i].x = xp = _11 * x + _21 * y + _31 * z + _41;
        lpDst[i].y = yp = _12 * x + _22 * y + _32 * z + _42;
        lpDst[i].z = zp = _13 * x + _23 * y + _33 * z + _43;
        lpDst[i].w = wp = _14 * x + _24 * y + _34 * z + _44;
        nRetFlag |= lpClip[i] = CheckForViewingFrustum(xp, yp, zp, wp);
        nRetFlag1 &= lpClip[i];
        i++;
        // 2nd
        // load data
        x = lpSrc[i].x;
        y = lpSrc[i].y;
        z = lpSrc[i].z;
        // transform, clip check and data storing
        lpDst[i].x = xp = _11 * x + _21 * y + _31 * z + _41;
        lpDst[i].y = yp = _12 * x + _22 * y + _32 * z + _42;
        lpDst[i].z = zp = _13 * x + _23 * y + _33 * z + _43;
        lpDst[i].w = wp = _14 * x + _24 * y + _34 * z + _44;
        nRetFlag |= lpClip[i] = CheckForViewingFrustum(xp, yp, zp, wp);
        nRetFlag1 &= lpClip[i];
        i++;
        // 3rd
        // load data
        x = lpSrc[i].x;
        y = lpSrc[i].y;
        z = lpSrc[i].z;
        // transform, clip check and data storing
        lpDst[i].x = xp = _11 * x + _21 * y + _31 * z + _41;
        lpDst[i].y = yp = _12 * x + _22 * y + _32 * z + _42;
        lpDst[i].z = zp = _13 * x + _23 * y + _33 * z + _43;
        lpDst[i].w = wp = _14 * x + _24 * y + _34 * z + _44;
        nRetFlag |= lpClip[i] = CheckForViewingFrustum(xp, yp, zp, wp);
        nRetFlag1 &= lpClip[i];
        i++;
        // 4th
        // load data
        x = lpSrc[i].x;
        y = lpSrc[i].y;
        z = lpSrc[i].z;
        // transform, clip check and data storing
        lpDst[i].x = xp = _11 * x + _21 * y + _31 * z + _41;
        lpDst[i].y = yp = _12 * x + _22 * y + _32 * z + _42;
        lpDst[i].z = zp = _13 * x + _23 * y + _33 * z + _43;
        lpDst[i].w = wp = _14 * x + _24 * y + _34 * z + _44;
        nRetFlag |= lpClip[i] = CheckForViewingFrustum(xp, yp, zp, wp);
        nRetFlag1 &= lpClip[i];
    }
    // transform the rest of vertices
    for (; i < nCount; i++)
    {
        // load data
        x = lpSrc[i].x;
        y = lpSrc[i].y;
        z = lpSrc[i].z;
        // transform, clip check and data storing
        lpDst[i].x = xp = _11 * x + _21 * y + _31 * z + _41;
        lpDst[i].y = yp = _12 * x + _22 * y + _32 * z + _42;
        lpDst[i].z = zp = _13 * x + _23 * y + _33 * z + _43;
        lpDst[i].w = wp = _14 * x + _24 * y + _34 * z + _44;
        nRetFlag |= lpClip[i] = CheckForViewingFrustum(xp, yp, zp, wp);
        nRetFlag1 &= lpClip[i];
    }
    // general clip check and post-transform (if no clips)
    if (nRetFlag1)
        return TRV_INVISIBLE;
    else if (nRetFlag != TRV_VISIBLE_NOCLIP)
        return TRV_VISIBLE_CLIP;
    else
        return TRV_VISIBLE_NOCLIP;
}
/////////////////////////////////////////////////////////////////////////////////////
//// int CTLPipelineFPU::SimplyTransform( const SHMatrix& matrix, SXYZW4* lpSrc, SXYZW4* lpDst,
/// const int nCount ) / just multiply verticves to the matrix. w/o homogenezation and clip check.
/// from SXYZW4 to SXYZW4
// int CTLPipelineFPU::SimplyTransform( const SHMatrix& matrix, SXYZW4* lpSrc, SXYZW4* lpDst, const
// int nCount )
//{
//	const float _11 = matrix._11, _12 = matrix._21, _13 = matrix._31, _14 = matrix._41;
//	const float _21 = matrix._12, _22 = matrix._22, _23 = matrix._32, _24 = matrix._42;
//	const float _31 = matrix._13, _32 = matrix._23, _33 = matrix._33, _34 = matrix._43;
//	const float _41 = matrix._14, _42 = matrix._24, _43 = matrix._34, _44 = matrix._44;
//	float x, y, z;
//	int i;

//	for ( i=0; i<nCount; i++ )
//	{
//		 0
//		x = lpSrc[i].x[0];
//		y = lpSrc[i].y[0];
//		z = lpSrc[i].z[0];
//		lpDst[i].x[0] = _11*x + _21*y + _31*z + _41;
//		lpDst[i].y[0] = _12*x + _22*y + _32*z + _42;
//		lpDst[i].z[0] = _13*x + _23*y + _33*z + _43;
//		lpDst[i].w[0] = _14*x + _24*y + _34*z + _44;
//		 1
//		x = lpSrc[i].x[1];
//		y = lpSrc[i].y[1];
//		z = lpSrc[i].z[1];
//		lpDst[i].x[1] = _11*x + _21*y + _31*z + _41;
//		lpDst[i].y[1] = _12*x + _22*y + _32*z + _42;
//		lpDst[i].z[1] = _13*x + _23*y + _33*z + _43;
//		lpDst[i].w[1] = _14*x + _24*y + _34*z + _44;
//		 2
//		x = lpSrc[i].x[2];
//		y = lpSrc[i].y[2];
//		z = lpSrc[i].z[2];
//		lpDst[i].x[2] = _11*x + _21*y + _31*z + _41;
//		lpDst[i].y[2] = _12*x + _22*y + _32*z + _42;
//		lpDst[i].z[2] = _13*x + _23*y + _33*z + _43;
//		lpDst[i].w[2] = _14*x + _24*y + _34*z + _44;
//		 3
//		x = lpSrc[i].x[3];
//		y = lpSrc[i].y[3];
//		z = lpSrc[i].z[3];
//		lpDst[i].x[3] = _11*x + _21*y + _31*z + _41;
//		lpDst[i].y[3] = _12*x + _22*y + _32*z + _42;
//		lpDst[i].z[3] = _13*x + _23*y + _33*z + _43;
//		lpDst[i].w[3] = _14*x + _24*y + _34*z + _44;
//	}

//	return TRV_VISIBLE_NOCLIP;
//}
///////////////////////////////////////////////////////////////////////////////////////
//// int CTLPipelineFPU::SimplyTransform( const SHMatrix& matrix, SXYZW4* lpSrc, SXYZW* lpDst, const
/// int nCount ) / just multiply verticves to the matrix. w/o homogenezation and clip check. from
/// SXYZW4 to SXYZW
// int CTLPipelineFPU::SimplyTransform( const SHMatrix& matrix, SXYZW4* lpSrc, SXYZW* lpDst, const
// int nCount )
//{
//	const float _11 = matrix._11, _12 = matrix._21, _13 = matrix._31, _14 = matrix._41;
//	const float _21 = matrix._12, _22 = matrix._22, _23 = matrix._32, _24 = matrix._42;
//	const float _31 = matrix._13, _32 = matrix._23, _33 = matrix._33, _34 = matrix._43;
//	const float _41 = matrix._14, _42 = matrix._24, _43 = matrix._34, _44 = matrix._44;
//	float x, y, z;
//	int i, i4;

//	for ( i=0, i4=0; i<nCount; i++ )
//	{
//		 0
//		x = lpSrc[i].x[0];
//		y = lpSrc[i].y[0];
//		z = lpSrc[i].z[0];
//		lpDst[i4].x   = _11*x + _21*y + _31*z + _41;
//		lpDst[i4].y   = _12*x + _22*y + _32*z + _42;
//		lpDst[i4].z   = _13*x + _23*y + _33*z + _43;
//		lpDst[i4++].w = _14*x + _24*y + _34*z + _44;
//		 1
//		x = lpSrc[i].x[1];
//		y = lpSrc[i].y[1];
//		z = lpSrc[i].z[1];
//		lpDst[i4].x   = _11*x + _21*y + _31*z + _41;
//		lpDst[i4].y   = _12*x + _22*y + _32*z + _42;
//		lpDst[i4].z   = _13*x + _23*y + _33*z + _43;
//		lpDst[i4++].w = _14*x + _24*y + _34*z + _44;
//		 2
//		x = lpSrc[i].x[2];
//		y = lpSrc[i].y[2];
//		z = lpSrc[i].z[2];
//		lpDst[i4].x   = _11*x + _21*y + _31*z + _41;
//		lpDst[i4].y   = _12*x + _22*y + _32*z + _42;
//		lpDst[i4].z   = _13*x + _23*y + _33*z + _43;
//		lpDst[i4++].w = _14*x + _24*y + _34*z + _44;
//		 3
//		x = lpSrc[i].x[3];
//		y = lpSrc[i].y[3];
//		z = lpSrc[i].z[3];
//		lpDst[i4].x   = _11*x + _21*y + _31*z + _41;
//		lpDst[i4].y   = _12*x + _22*y + _32*z + _42;
//		lpDst[i4].z   = _13*x + _23*y + _33*z + _43;
//		lpDst[i4++].w = _14*x + _24*y + _34*z + _44;
//	}

//	return TRV_VISIBLE_NOCLIP;
//}
///////////////////////////////////////////////////////////////////////////////////////
//// int CTLPipelineFPU::SimplyTransform( const SHMatrix& matrix, SXYZW* lpSrc, SXYZW* lpDst, const
/// int nCount ) / just multiply verticves to the matrix. w/o homogenezation and clip check. from
/// SXYZW to SXYZW
// int CTLPipelineFPU::SimplyTransform( const SHMatrix& matrix, SXYZW* lpSrc, SXYZW* lpDst, const
// int nCount )
//{
//	const float _11 = matrix._11, _12 = matrix._21, _13 = matrix._31, _14 = matrix._41;
//	const float _21 = matrix._12, _22 = matrix._22, _23 = matrix._32, _24 = matrix._42;
//	const float _31 = matrix._13, _32 = matrix._23, _33 = matrix._33, _34 = matrix._43;
//	const float _41 = matrix._14, _42 = matrix._24, _43 = matrix._34, _44 = matrix._44;
//	float x, y, z;
//	int i;

//	for ( i=0; i<(nCount & (~0x3));  )
//	{
//		 0
//		x = lpSrc[i].x;
//		y = lpSrc[i].y;
//		z = lpSrc[i].z;
//		lpDst[i].x   = _11*x + _21*y + _31*z + _41;
//		lpDst[i].y   = _12*x + _22*y + _32*z + _42;
//		lpDst[i].z   = _13*x + _23*y + _33*z + _43;
//		lpDst[i++].w = _14*x + _24*y + _34*z + _44;
//		 1
//		x = lpSrc[i].x;
//		y = lpSrc[i].y;
//		z = lpSrc[i].z;
//		lpDst[i].x   = _11*x + _21*y + _31*z + _41;
//		lpDst[i].y   = _12*x + _22*y + _32*z + _42;
//		lpDst[i].z   = _13*x + _23*y + _33*z + _43;
//		lpDst[i++].w = _14*x + _24*y + _34*z + _44;
//		 2
//		x = lpSrc[i].x;
//		y = lpSrc[i].y;
//		z = lpSrc[i].z;
//		lpDst[i].x   = _11*x + _21*y + _31*z + _41;
//		lpDst[i].y   = _12*x + _22*y + _32*z + _42;
//		lpDst[i].z   = _13*x + _23*y + _33*z + _43;
//		lpDst[i++].w = _14*x + _24*y + _34*z + _44;
//		 3
//		x = lpSrc[i].x;
//		y = lpSrc[i].y;
//		z = lpSrc[i].z;
//		lpDst[i].x   = _11*x + _21*y + _31*z + _41;
//		lpDst[i].y   = _12*x + _22*y + _32*z + _42;
//		lpDst[i].z   = _13*x + _23*y + _33*z + _43;
//		lpDst[i++].w = _14*x + _24*y + _34*z + _44;
//	}
//	 the rest
//	for ( ; i<nCount; )
//	{
//		x = lpSrc[i].x;
//		y = lpSrc[i].y;
//		z = lpSrc[i].z;
//		lpDst[i].x   = _11*x + _21*y + _31*z + _41;
//		lpDst[i].y   = _12*x + _22*y + _32*z + _42;
//		lpDst[i].z   = _13*x + _23*y + _33*z + _43;
//		lpDst[i++].w = _14*x + _24*y + _34*z + _44;
//	}

//	return TRV_VISIBLE_NOCLIP;
//}
/////////////////////////////////////////////////////////////////////////////////////
// int CTLPipelineFPU::TransformVertices4NC( const D3DMATRIX& matrix, const SXYZW4* pSrc, SXYZW4*
// pDst, const int nCount ) transform vertices without performing any clip operations
int CTLPipelineFPU::TransformVertices4NC(const D3DMATRIX &matrix, SXYZW4 *pSrc, SXYZW4 *pDst,
                                         const int nCount)
{
    const D3DVALUE _11 = matrix._11, _12 = matrix._12, _13 = matrix._13, _14 = matrix._14;
    const D3DVALUE _21 = matrix._21, _22 = matrix._22, _23 = matrix._23, _24 = matrix._24;
    const D3DVALUE _31 = matrix._31, _32 = matrix._32, _33 = matrix._33, _34 = matrix._34;
    const D3DVALUE _41 = matrix._41, _42 = matrix._42, _43 = matrix._43, _44 = matrix._44;
    D3DVALUE x, y, z, xp, yp, zp, wp, wp1;
    int i;

    for (i = 0; i < nCount; i++)
    {
        // first element
        x = pSrc[i].x[0];
        y = pSrc[i].y[0];
        z = pSrc[i].z[0];
        xp = _11 * x + _21 * y + _31 * z + _41;
        yp = _12 * x + _22 * y + _32 * z + _42;
        zp = _13 * x + _23 * y + _33 * z + _43;
        wp = _14 * x + _24 * y + _34 * z + _44;
        // load data for second element
        x = pSrc[i].x[1];
        y = pSrc[i].y[1];
        z = pSrc[i].z[1];
        // continue processing first element
        wp1 = 1.0f / wp;
        pDst[i].x[0] = xp * wp1;
        pDst[i].y[0] = yp * wp1;
        pDst[i].z[0] = zp * wp1;
        pDst[i].w[0] = wp1;
        // second element
        xp = _11 * x + _21 * y + _31 * z + _41;
        yp = _12 * x + _22 * y + _32 * z + _42;
        zp = _13 * x + _23 * y + _33 * z + _43;
        wp = _14 * x + _24 * y + _34 * z + _44;
        // load data for third element
        x = pSrc[i].x[2];
        y = pSrc[i].y[2];
        z = pSrc[i].z[2];
        // continue processing second element
        wp1 = 1.0f / wp;
        pDst[i].x[1] = xp * wp1;
        pDst[i].y[1] = yp * wp1;
        pDst[i].z[1] = zp * wp1;
        pDst[i].w[1] = wp1;
        // third element
        xp = _11 * x + _21 * y + _31 * z + _41;
        yp = _12 * x + _22 * y + _32 * z + _42;
        zp = _13 * x + _23 * y + _33 * z + _43;
        wp = _14 * x + _24 * y + _34 * z + _44;
        // load data for forth element
        x = pSrc[i].x[3];
        y = pSrc[i].y[3];
        z = pSrc[i].z[3];
        // continue processing third element
        wp1 = 1.0f / wp;
        pDst[i].x[2] = xp * wp1;
        pDst[i].y[2] = yp * wp1;
        pDst[i].z[2] = zp * wp1;
        pDst[i].w[2] = wp1;
        // forth element
        xp = _11 * x + _21 * y + _31 * z + _41;
        yp = _12 * x + _22 * y + _32 * z + _42;
        zp = _13 * x + _23 * y + _33 * z + _43;
        wp = _14 * x + _24 * y + _34 * z + _44;
        wp1 = 1.0f / wp;
        pDst[i].x[3] = xp * wp1;
        pDst[i].y[3] = yp * wp1;
        pDst[i].z[3] = zp * wp1;
        pDst[i].w[3] = wp1;
    }

    return TRV_VISIBLE_NOCLIP;
}
/////////////////////////////////////////////////////////////////////////////////////
// int CTLPipelineFPU::TransformVertices4C( const D3DMATRIX& matrix, const SXYZW4* pSrc, SXYZW4*
// pDst, BYTE* pClip, const int nCount ) transform vertices with performing clipping check and store
// results in 'pClip' byte array 0, if vertex inside viewing frustrum, TRV_CLIP_... flags set -
// outside returns 'TRV_INVISIBLE', 'TRV_VISIBLE_NOCLIP' or 'TRV_VISIBLE_CLIP' respectivelly
int CTLPipelineFPU::TransformVertices4C(const D3DMATRIX &matrix, SXYZW4 *pSrc, SXYZW4 *pDst,
                                        BYTE *pClip, const int nCount)
{
    const D3DVALUE _11 = matrix._11, _12 = matrix._12, _13 = matrix._13, _14 = matrix._14;
    const D3DVALUE _21 = matrix._21, _22 = matrix._22, _23 = matrix._23, _24 = matrix._24;
    const D3DVALUE _31 = matrix._31, _32 = matrix._32, _33 = matrix._33, _34 = matrix._34;
    const D3DVALUE _41 = matrix._41, _42 = matrix._42, _43 = matrix._43, _44 = matrix._44;
    int nRetFlag = TRV_VISIBLE_NOCLIP, nRetFlag1 = TRV_CLIP_ALL;
    D3DVALUE x, y, z, xp, yp, zp, wp, wp1;
    int i, i4;

    for (i4 = 0, i = 0; i < nCount; i++)
    {
        // first element
        x = pSrc[i].x[0];
        y = pSrc[i].y[0];
        z = pSrc[i].z[0];
        pDst[i].x[0] = xp = _11 * x + _21 * y + _31 * z + _41;
        pDst[i].y[0] = yp = _12 * x + _22 * y + _32 * z + _42;
        pDst[i].z[0] = zp = _13 * x + _23 * y + _33 * z + _43;
        pDst[i].w[0] = wp = _14 * x + _24 * y + _34 * z + _44;
        // load data for second element
        x = pSrc[i].x[1];
        y = pSrc[i].y[1];
        z = pSrc[i].z[1];
        // continue processing first element
        nRetFlag |= pClip[i4] = CheckForViewingFrustum(xp, yp, zp, wp);
        nRetFlag1 &= pClip[i4++];
        // second element
        pDst[i].x[1] = xp = _11 * x + _21 * y + _31 * z + _41;
        pDst[i].y[1] = yp = _12 * x + _22 * y + _32 * z + _42;
        pDst[i].z[1] = zp = _13 * x + _23 * y + _33 * z + _43;
        pDst[i].w[1] = wp = _14 * x + _24 * y + _34 * z + _44;
        // load data for third element
        x = pSrc[i].x[2];
        y = pSrc[i].y[2];
        z = pSrc[i].z[2];
        // continue processing second element
        nRetFlag |= pClip[i4] = CheckForViewingFrustum(xp, yp, zp, wp);
        nRetFlag1 &= pClip[i4++];
        // third element
        pDst[i].x[2] = xp = _11 * x + _21 * y + _31 * z + _41;
        pDst[i].y[2] = yp = _12 * x + _22 * y + _32 * z + _42;
        pDst[i].z[2] = zp = _13 * x + _23 * y + _33 * z + _43;
        pDst[i].w[2] = wp = _14 * x + _24 * y + _34 * z + _44;
        // load data for forth element
        x = pSrc[i].x[3];
        y = pSrc[i].y[3];
        z = pSrc[i].z[3];
        // continue processing third element
        nRetFlag |= pClip[i4] = CheckForViewingFrustum(xp, yp, zp, wp);
        nRetFlag1 &= pClip[i4++];
        // forth element
        pDst[i].x[3] = xp = _11 * x + _21 * y + _31 * z + _41;
        pDst[i].y[3] = yp = _12 * x + _22 * y + _32 * z + _42;
        pDst[i].z[3] = zp = _13 * x + _23 * y + _33 * z + _43;
        pDst[i].w[3] = wp = _14 * x + _24 * y + _34 * z + _44;
        nRetFlag |= pClip[i4] = CheckForViewingFrustum(xp, yp, zp, wp);
        nRetFlag1 &= pClip[i4++];
    }

    if (nRetFlag1)
        return TRV_INVISIBLE;
    else if (nRetFlag != TRV_VISIBLE_NOCLIP)
        return TRV_VISIBLE_CLIP;
    else
    {
        // cast from projection space to screen space (with homogenezation)
        register float fpClipHW1 = fpClipHW;
        register float fpClipHH1 = fpClipHH;
        for (i = 0; i < nCount; i++)
        {
            // 0th
            wp1 = 1.0f / pDst[i].w[0];
            pDst[i].x[0] = (1.0f + (pDst[i].x[0] * wp1)) * fpClipHW1;
            pDst[i].y[0] = (1.0f - (pDst[i].y[0] * wp1)) * fpClipHH1;
            pDst[i].z[0] = pDst[i].z[0] * wp1;
            pDst[i].w[0] = wp1;
            // 1st
            wp1 = 1.0f / pDst[i].w[1];
            pDst[i].x[1] = (1.0f + (pDst[i].x[1] * wp1)) * fpClipHW1;
            pDst[i].y[1] = (1.0f - (pDst[i].y[1] * wp1)) * fpClipHH1;
            pDst[i].z[1] = pDst[i].z[1] * wp1;
            pDst[i].w[1] = wp1;
            // 2nd
            wp1 = 1.0f / pDst[i].w[2];
            pDst[i].x[2] = (1.0f + (pDst[i].x[2] * wp1)) * fpClipHW1;
            pDst[i].y[2] = (1.0f - (pDst[i].y[2] * wp1)) * fpClipHH1;
            pDst[i].z[2] = pDst[i].z[2] * wp1;
            pDst[i].w[2] = wp1;
            // 3rd
            wp1 = 1.0f / pDst[i].w[3];
            pDst[i].x[3] = (1.0f + (pDst[i].x[3] * wp1)) * fpClipHW1;
            pDst[i].y[3] = (1.0f - (pDst[i].y[3] * wp1)) * fpClipHH1;
            pDst[i].z[3] = pDst[i].z[3] * wp1;
            pDst[i].w[3] = wp1;
        }

        return TRV_VISIBLE_NOCLIP;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// int CTLPipelineFPU::TransformVerticesNC( const D3DMATRIX& matrix, SXYZW4* pSrc, SXYZW* pDst,
// const int nCount ) transform vertices from SXYZW4 to 4 SXYZW without performing any clip
// operations
int CTLPipelineFPU::TransformVerticesNC(const D3DMATRIX &matrix, SXYZW4 *pSrc, SXYZW *pDst,
                                        const int nCount)
{
    const D3DVALUE _11 = matrix._11, _12 = matrix._12, _13 = matrix._13, _14 = matrix._14;
    const D3DVALUE _21 = matrix._21, _22 = matrix._22, _23 = matrix._23, _24 = matrix._24;
    const D3DVALUE _31 = matrix._31, _32 = matrix._32, _33 = matrix._33, _34 = matrix._34;
    const D3DVALUE _41 = matrix._41, _42 = matrix._42, _43 = matrix._43, _44 = matrix._44;
    D3DVALUE x, y, z, xp, yp, zp, wp, wp1;
    int i, i4;

    for (i4 = 0, i = 0; i < nCount; i++)
    {
        // first element
        x = pSrc[i].x[0];
        y = pSrc[i].y[0];
        z = pSrc[i].z[0];
        xp = _11 * x + _21 * y + _31 * z + _41;
        yp = _12 * x + _22 * y + _32 * z + _42;
        zp = _13 * x + _23 * y + _33 * z + _43;
        wp = _14 * x + _24 * y + _34 * z + _44;
        // load data for second element
        x = pSrc[i].x[1];
        // continue processing first element
        wp1 = 1.0f / wp;
        pDst[i4].x = xp * wp1;
        pDst[i4].y = yp * wp1;
        pDst[i4].z = zp * wp1;
        pDst[i4++].w = wp1;
        // second element
        y = pSrc[i].y[1];
        z = pSrc[i].z[1];
        xp = _11 * x + _21 * y + _31 * z + _41;
        yp = _12 * x + _22 * y + _32 * z + _42;
        zp = _13 * x + _23 * y + _33 * z + _43;
        wp = _14 * x + _24 * y + _34 * z + _44;
        // load data for third element
        x = pSrc[i].x[2];
        // continue processing second element
        wp1 = 1.0f / wp;
        pDst[i4].x = xp * wp1;
        pDst[i4].y = yp * wp1;
        pDst[i4].z = zp * wp1;
        pDst[i4++].w = wp1;
        // third element
        y = pSrc[i].y[2];
        z = pSrc[i].z[2];
        xp = _11 * x + _21 * y + _31 * z + _41;
        yp = _12 * x + _22 * y + _32 * z + _42;
        zp = _13 * x + _23 * y + _33 * z + _43;
        wp = _14 * x + _24 * y + _34 * z + _44;
        // load data for forth element
        x = pSrc[i].x[3];
        // continue processing third element
        wp1 = 1.0f / wp;
        pDst[i4].x = xp * wp1;
        pDst[i4].y = yp * wp1;
        pDst[i4].z = zp * wp1;
        pDst[i4++].w = wp1;
        // forth element
        y = pSrc[i].y[3];
        z = pSrc[i].z[3];
        xp = _11 * x + _21 * y + _31 * z + _41;
        yp = _12 * x + _22 * y + _32 * z + _42;
        zp = _13 * x + _23 * y + _33 * z + _43;
        wp = _14 * x + _24 * y + _34 * z + _44;
        //
        wp1 = 1.0f / wp;
        pDst[i4].x = xp * wp1;
        pDst[i4].y = yp * wp1;
        pDst[i4].z = zp * wp1;
        pDst[i4++].w = wp1;
    }

    return TRV_VISIBLE_NOCLIP;
}
/////////////////////////////////////////////////////////////////////////////////////
// int CTLPipelineFPU::TransformVerticesC( const D3DMATRIX& matrix, SXYZW4* pSrc, SXYZW* pDst, BYTE*
// pClip, const int nCount ) transform vertices from SXYZW4 to 4 SXYZW without performing any clip
// operations
int CTLPipelineFPU::TransformVerticesC(const D3DMATRIX &matrix, SXYZW4 *pSrc, SXYZW *pDst,
                                       BYTE *pClip, const int nCount)
{
    const D3DVALUE _11 = matrix._11, _12 = matrix._12, _13 = matrix._13, _14 = matrix._14;
    const D3DVALUE _21 = matrix._21, _22 = matrix._22, _23 = matrix._23, _24 = matrix._24;
    const D3DVALUE _31 = matrix._31, _32 = matrix._32, _33 = matrix._33, _34 = matrix._34;
    const D3DVALUE _41 = matrix._41, _42 = matrix._42, _43 = matrix._43, _44 = matrix._44;
    int nRetFlag = TRV_VISIBLE_NOCLIP, nRetFlag1 = TRV_CLIP_ALL;
    D3DVALUE x, y, z, xp, yp, zp, wp, wp1;
    int i, i4;

    for (i4 = 0, i = 0; i < nCount; i++)
    {
        // first element
        x = pSrc[i].x[0];
        y = pSrc[i].y[0];
        z = pSrc[i].z[0];
        //
        pDst[i4].x = xp = _11 * x + _21 * y + _31 * z + _41;
        pDst[i4].y = yp = _12 * x + _22 * y + _32 * z + _42;
        pDst[i4].z = zp = _13 * x + _23 * y + _33 * z + _43;
        pDst[i4].w = wp = _14 * x + _24 * y + _34 * z + _44;
        // load data for second element
        x = pSrc[i].x[1];
        // compose flags for first element
        nRetFlag |= pClip[i4] = CheckForViewingFrustum(xp, yp, zp, wp);
        nRetFlag1 &= pClip[i4++];
        // second element
        y = pSrc[i].y[1];
        z = pSrc[i].z[1];
        pDst[i4].x = xp = _11 * x + _21 * y + _31 * z + _41;
        pDst[i4].y = yp = _12 * x + _22 * y + _32 * z + _42;
        pDst[i4].z = zp = _13 * x + _23 * y + _33 * z + _43;
        pDst[i4].w = wp = _14 * x + _24 * y + _34 * z + _44;
        // load data for third element
        x = pSrc[i].x[2];
        // compose flags for second element
        nRetFlag |= pClip[i4] = CheckForViewingFrustum(xp, yp, zp, wp);
        nRetFlag1 &= pClip[i4++];
        // third element
        y = pSrc[i].y[2];
        z = pSrc[i].z[2];
        pDst[i4].x = xp = _11 * x + _21 * y + _31 * z + _41;
        pDst[i4].y = yp = _12 * x + _22 * y + _32 * z + _42;
        pDst[i4].z = zp = _13 * x + _23 * y + _33 * z + _43;
        pDst[i4].w = wp = _14 * x + _24 * y + _34 * z + _44;
        // load data for forth element
        x = pSrc[i].x[3];
        // compose flags for third element
        nRetFlag |= pClip[i4] = CheckForViewingFrustum(xp, yp, zp, wp);
        nRetFlag1 &= pClip[i4++];
        // forth element
        y = pSrc[i].y[3];
        z = pSrc[i].z[3];
        pDst[i4].x = xp = _11 * x + _21 * y + _31 * z + _41;
        pDst[i4].y = yp = _12 * x + _22 * y + _32 * z + _42;
        pDst[i4].z = zp = _13 * x + _23 * y + _33 * z + _43;
        pDst[i4].w = wp = _14 * x + _24 * y + _34 * z + _44;
        nRetFlag |= pClip[i4] = CheckForViewingFrustum(xp, yp, zp, wp);
        nRetFlag1 &= pClip[i4++];
    }

    if (nRetFlag1)
        return TRV_INVISIBLE;
    else if (nRetFlag != TRV_VISIBLE_NOCLIP)
        return TRV_VISIBLE_CLIP;
    else
    {
        register float fpClipHW1 = fpClipHW;
        register float fpClipHH1 = fpClipHH;
        for (i4 = 0, i = 0; i < nCount; i++)
        {
            // 0th element
            wp1 = 1.0f / pDst[i4].w;
            pDst[i4].x = (1.0f + (pDst[i4].x * wp1)) * fpClipHW1;
            pDst[i4].y = (1.0f - (pDst[i4].y * wp1)) * fpClipHH1;
            pDst[i4].z = pDst[i4].z * wp1;
            pDst[i4++].w = wp1;
            // 1st element
            wp1 = 1.0f / pDst[i4].w;
            pDst[i4].x = (1.0f + (pDst[i4].x * wp1)) * fpClipHW1;
            pDst[i4].y = (1.0f - (pDst[i4].y * wp1)) * fpClipHH1;
            pDst[i4].z = pDst[i4].z * wp1;
            pDst[i4++].w = wp1;
            // 2nd element
            wp1 = 1.0f / pDst[i4].w;
            pDst[i4].x = (1.0f + (pDst[i4].x * wp1)) * fpClipHW1;
            pDst[i4].y = (1.0f - (pDst[i4].y * wp1)) * fpClipHH1;
            pDst[i4].z = pDst[i4].z * wp1;
            pDst[i4++].w = wp1;
            // 3rd element
            wp1 = 1.0f / pDst[i4].w;
            pDst[i4].x = (1.0f + (pDst[i4].x * wp1)) * fpClipHW1;
            pDst[i4].y = (1.0f - (pDst[i4].y * wp1)) * fpClipHH1;
            pDst[i4].z = pDst[i4].z * wp1;
            pDst[i4++].w = wp1;
        }

        return TRV_VISIBLE_NOCLIP;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// int CTLPipelineFPU::Transform2DParticles( const D3DMATRIX& matrix, SXYZW* pSrc, D3DTLVERTEX*
// pDst, BYTE* pClip, const int nCount ) transform 2d particled and store results in special format
int CTLPipelineFPU::Transform2DParticles(const D3DMATRIX &matrix, SXYZW *pSrc, D3DTLVERTEX *pDst,
                                         BYTE *pClip, const int nCount)
{
    const D3DVALUE _11 = matrix._11, _12 = matrix._12, _13 = matrix._13, _14 = matrix._14;
    const D3DVALUE _21 = matrix._21, _22 = matrix._22, _23 = matrix._23, _24 = matrix._24;
    const D3DVALUE _31 = matrix._31, _32 = matrix._32, _33 = matrix._33, _34 = matrix._34;
    const D3DVALUE _41 = matrix._41, _42 = matrix._42, _43 = matrix._43, _44 = matrix._44;
    int nRetFlag = TRV_VISIBLE_NOCLIP, nRetFlag1 = TRV_CLIP_ALL;
    int i, i4;
    //
    D3DVALUE x, y, z, xp, yp, zp, wp, wp1, radius, radx, rady;
    for (i = 0, i4 = 0; i < nCount; i++)
    {
        // Get the untransformed vertex position
        x = pSrc[i].x;
        y = pSrc[i].y;
        z = pSrc[i].z;
        radius = pSrc[i].w;
        // Transform it through the current matrix set
        xp = _11 * x + _21 * y + _31 * z + _41;
        yp = _12 * x + _22 * y + _32 * z + _42;
        zp = _13 * x + _23 * y + _33 * z + _43;
        wp = _14 * x + _24 * y + _34 * z + _44;
        wp1 = (wp == 0) ? 1000000 : 1.0f / wp;

        radius *= wp1;
        radx = radius;
        rady = radius * fpClipHW / fpClipHH;
        radius *= fpClipHW;
        // Finally, scale the vertices to screen coords. This step first
        // "flattens" the coordinates from 3D space to 2D device coordinates,
        // by dividing each coordinate by the wp value. Then, the x- and
        // y-components are transformed from device coords to screen coords.
        // Note 1: device coords range from -1 to +1 in the viewport.
        // Note 2: the sz-coordinate will be used in the z-buffer.
        x = (1.0f + (xp * wp1)) * fpClipHW;
        y = (1.0f - (yp * wp1)) * fpClipHH;
        z = zp * wp1;

        pDst[i4].sx = x - radius;
        pDst[i4].sy = y - radius;
        pDst[i4].sz = z;
        pDst[i4].rhw = wp1;
        nRetFlag |= pClip[i4] = CheckForViewingFrustum(xp - radx, yp - rady, zp, wp);
        nRetFlag1 &= pClip[i4++];

        pDst[i4].sx = x + radius;
        pDst[i4].sy = y - radius;
        pDst[i4].sz = z;
        pDst[i4].rhw = wp1;
        nRetFlag |= pClip[i4] = CheckForViewingFrustum(xp + radx, yp - rady, zp, wp);
        nRetFlag1 &= pClip[i4++];

        pDst[i4].sx = x + radius;
        pDst[i4].sy = y + radius;
        pDst[i4].sz = z;
        pDst[i4].rhw = wp1;
        nRetFlag |= pClip[i4] = CheckForViewingFrustum(xp + radx, yp + rady, zp, wp);
        nRetFlag1 &= pClip[i4++];

        pDst[i4].sx = x - radius;
        pDst[i4].sy = y + radius;
        pDst[i4].sz = z;
        pDst[i4].rhw = wp1;
        nRetFlag |= pClip[i4] = CheckForViewingFrustum(xp - radx, yp + rady, zp, wp);
        nRetFlag1 &= pClip[i4++];
    }

    if (nRetFlag1)
        return TRV_INVISIBLE;
    else if (nRetFlag == TRV_VISIBLE_NOCLIP)
        return TRV_VISIBLE_NOCLIP;
    else
        return TRV_VISIBLE_CLIP;
}
/////////////////////////////////////////////////////////////////////////////////////
// int CTLPipelineFPU::SimplyTransform4NC( const D3DMATRIX& matrix, SXYZW4* pSrc, SXYZW4* pDst, int
// nCount ) simply multiply 'src' (SXYZW4) vertices to 'matrix' and store results in 'dst' (SXYZW4)
// vertices w/o homogenezation and w/o clip check
int CTLPipelineFPU::SimplyTransform4NC(const D3DMATRIX &matrix, SXYZW4 *pSrc, SXYZW4 *pDst,
                                       int nCount)
{
    const D3DVALUE _11 = matrix._11, _12 = matrix._12, _13 = matrix._13, _14 = matrix._14;
    const D3DVALUE _21 = matrix._21, _22 = matrix._22, _23 = matrix._23, _24 = matrix._24;
    const D3DVALUE _31 = matrix._31, _32 = matrix._32, _33 = matrix._33, _34 = matrix._34;
    const D3DVALUE _41 = matrix._41, _42 = matrix._42, _43 = matrix._43, _44 = matrix._44;
    float x, y, z;
    int i;

    for (i = 0; i < nCount; i++)
    {
        // 0
        x = pSrc[i].x[0];
        y = pSrc[i].y[0];
        z = pSrc[i].z[0];
        pDst[i].x[0] = _11 * x + _21 * y + _31 * z + _41;
        pDst[i].y[0] = _12 * x + _22 * y + _32 * z + _42;
        pDst[i].z[0] = _13 * x + _23 * y + _33 * z + _43;
        pDst[i].w[0] = _14 * x + _24 * y + _34 * z + _44;
        // 1
        x = pSrc[i].x[1];
        y = pSrc[i].y[1];
        z = pSrc[i].z[1];
        pDst[i].x[1] = _11 * x + _21 * y + _31 * z + _41;
        pDst[i].y[1] = _12 * x + _22 * y + _32 * z + _42;
        pDst[i].z[1] = _13 * x + _23 * y + _33 * z + _43;
        pDst[i].w[1] = _14 * x + _24 * y + _34 * z + _44;
        // 2
        x = pSrc[i].x[2];
        y = pSrc[i].y[2];
        z = pSrc[i].z[2];
        pDst[i].x[2] = _11 * x + _21 * y + _31 * z + _41;
        pDst[i].y[2] = _12 * x + _22 * y + _32 * z + _42;
        pDst[i].z[2] = _13 * x + _23 * y + _33 * z + _43;
        pDst[i].w[2] = _14 * x + _24 * y + _34 * z + _44;
        // 3
        x = pSrc[i].x[3];
        y = pSrc[i].y[3];
        z = pSrc[i].z[3];
        pDst[i].x[3] = _11 * x + _21 * y + _31 * z + _41;
        pDst[i].y[3] = _12 * x + _22 * y + _32 * z + _42;
        pDst[i].z[3] = _13 * x + _23 * y + _33 * z + _43;
        pDst[i].w[3] = _14 * x + _24 * y + _34 * z + _44;
    }

    return TRV_VISIBLE_NOCLIP;
}
/////////////////////////////////////////////////////////////////////////////////////
// int CTLPipelineFPU::SimplyTransformNC( const D3DMATRIX& matrix, SXYZW4* pSrc, SXYZW* pDst, int
// nCount ) simply multiply 'src' (SXYZW4) vertices to 'matrix' and store results in 'dst' (SXYZW)
// vertices w/o homogenezation and w/o clip check
int CTLPipelineFPU::SimplyTransformNC(const D3DMATRIX &matrix, SXYZW4 *pSrc, SXYZW *pDst,
                                      int nCount)
{
    const D3DVALUE _11 = matrix._11, _12 = matrix._12, _13 = matrix._13, _14 = matrix._14;
    const D3DVALUE _21 = matrix._21, _22 = matrix._22, _23 = matrix._23, _24 = matrix._24;
    const D3DVALUE _31 = matrix._31, _32 = matrix._32, _33 = matrix._33, _34 = matrix._34;
    const D3DVALUE _41 = matrix._41, _42 = matrix._42, _43 = matrix._43, _44 = matrix._44;
    float x, y, z;
    int i, i4;

    for (i = 0, i4 = 0; i < nCount; i++)
    {
        // 0
        x = pSrc[i].x[0];
        y = pSrc[i].y[0];
        z = pSrc[i].z[0];
        pDst[i4].x = _11 * x + _21 * y + _31 * z + _41;
        pDst[i4].y = _12 * x + _22 * y + _32 * z + _42;
        pDst[i4].z = _13 * x + _23 * y + _33 * z + _43;
        pDst[i4++].w = _14 * x + _24 * y + _34 * z + _44;
        // 1
        x = pSrc[i].x[1];
        y = pSrc[i].y[1];
        z = pSrc[i].z[1];
        pDst[i4].x = _11 * x + _21 * y + _31 * z + _41;
        pDst[i4].y = _12 * x + _22 * y + _32 * z + _42;
        pDst[i4].z = _13 * x + _23 * y + _33 * z + _43;
        pDst[i4++].w = _14 * x + _24 * y + _34 * z + _44;
        // 2
        x = pSrc[i].x[2];
        y = pSrc[i].y[2];
        z = pSrc[i].z[2];
        pDst[i4].x = _11 * x + _21 * y + _31 * z + _41;
        pDst[i4].y = _12 * x + _22 * y + _32 * z + _42;
        pDst[i4].z = _13 * x + _23 * y + _33 * z + _43;
        pDst[i4++].w = _14 * x + _24 * y + _34 * z + _44;
        // 3
        x = pSrc[i].x[3];
        y = pSrc[i].y[3];
        z = pSrc[i].z[3];
        pDst[i4].x = _11 * x + _21 * y + _31 * z + _41;
        pDst[i4].y = _12 * x + _22 * y + _32 * z + _42;
        pDst[i4].z = _13 * x + _23 * y + _33 * z + _43;
        pDst[i4++].w = _14 * x + _24 * y + _34 * z + _44;
    }

    return TRV_VISIBLE_NOCLIP;
}
/////////////////////////////////////////////////////////////////////////////////////
// int CTLPipelineFPU::SimplyTransform4C( const D3DMATRIX& matrix, SXYZW4* pSrc, SXYZW4* pDst, BYTE*
// pClip, int nCount ) simply multiply 'src' (SXYZW4) vertices to 'matrix' and store results in
// 'dst' (SXYZW4) vertices w/o homogenezation and with clip check
int CTLPipelineFPU::SimplyTransform4C(const D3DMATRIX &matrix, SXYZW4 *pSrc, SXYZW4 *pDst,
                                      BYTE *pClip, int nCount)
{
    const D3DVALUE _11 = matrix._11, _12 = matrix._12, _13 = matrix._13, _14 = matrix._14;
    const D3DVALUE _21 = matrix._21, _22 = matrix._22, _23 = matrix._23, _24 = matrix._24;
    const D3DVALUE _31 = matrix._31, _32 = matrix._32, _33 = matrix._33, _34 = matrix._34;
    const D3DVALUE _41 = matrix._41, _42 = matrix._42, _43 = matrix._43, _44 = matrix._44;
    int nRetFlag = TRV_VISIBLE_NOCLIP, nRetFlag1 = TRV_CLIP_ALL;
    float x, y, z, xp, yp, zp, wp;
    int i, i4;

    for (i = 0, i4 = 0; i < nCount; i++)
    {
        // 0
        x = pSrc[i].x[0];
        y = pSrc[i].y[0];
        z = pSrc[i].z[0];
        xp = pDst[i].x[0] = _11 * x + _21 * y + _31 * z + _41;
        yp = pDst[i].y[0] = _12 * x + _22 * y + _32 * z + _42;
        zp = pDst[i].z[0] = _13 * x + _23 * y + _33 * z + _43;
        wp = pDst[i].w[0] = _14 * x + _24 * y + _34 * z + _44;
        // load data for next element
        x = pSrc[i].x[1];
        y = pSrc[i].y[1];
        z = pSrc[i].z[1];
        // continue processing this element
        nRetFlag |= pClip[i4] = CheckForViewingFrustum(xp, yp, zp, wp);
        nRetFlag1 &= pClip[i4++];
        // 1
        xp = pDst[i].x[1] = _11 * x + _21 * y + _31 * z + _41;
        yp = pDst[i].y[1] = _12 * x + _22 * y + _32 * z + _42;
        zp = pDst[i].z[1] = _13 * x + _23 * y + _33 * z + _43;
        wp = pDst[i].w[1] = _14 * x + _24 * y + _34 * z + _44;
        // load data for next element
        x = pSrc[i].x[2];
        y = pSrc[i].y[2];
        z = pSrc[i].z[2];
        // continue processing this element
        nRetFlag |= pClip[i4] = CheckForViewingFrustum(xp, yp, zp, wp);
        nRetFlag1 &= pClip[i4++];
        // 2
        xp = pDst[i].x[2] = _11 * x + _21 * y + _31 * z + _41;
        yp = pDst[i].y[2] = _12 * x + _22 * y + _32 * z + _42;
        zp = pDst[i].z[2] = _13 * x + _23 * y + _33 * z + _43;
        wp = pDst[i].w[2] = _14 * x + _24 * y + _34 * z + _44;
        // load data for next element
        x = pSrc[i].x[3];
        y = pSrc[i].y[3];
        z = pSrc[i].z[3];
        // continue processing this element
        nRetFlag |= pClip[i4] = CheckForViewingFrustum(xp, yp, zp, wp);
        nRetFlag1 &= pClip[i4++];
        // 3
        xp = pDst[i].x[3] = _11 * x + _21 * y + _31 * z + _41;
        yp = pDst[i].y[3] = _12 * x + _22 * y + _32 * z + _42;
        zp = pDst[i].z[3] = _13 * x + _23 * y + _33 * z + _43;
        wp = pDst[i].w[3] = _14 * x + _24 * y + _34 * z + _44;
        nRetFlag |= pClip[i4] = CheckForViewingFrustum(xp, yp, zp, wp);
        nRetFlag1 &= pClip[i4++];
    }

    if (nRetFlag1)
        return TRV_INVISIBLE;
    else if (nRetFlag == TRV_VISIBLE_NOCLIP)
        return TRV_VISIBLE_NOCLIP;
    else
        return TRV_VISIBLE_CLIP;
}
/////////////////////////////////////////////////////////////////////////////////////
// int CTLPipelineFPU::SimplyTransformC( const D3DMATRIX& matrix, SXYZW4* pSrc, SXYZW* pDst, BYTE*
// pClip, int nCount ) simply multiply 'src' (SXYZW4) vertices to 'matrix' and store results in
// 'dst' (SXYZW) vertices w/o homogenezation and with clip check
int CTLPipelineFPU::SimplyTransformC(const D3DMATRIX &matrix, SXYZW4 *pSrc, SXYZW *pDst,
                                     BYTE *pClip, int nCount)
{
    const D3DVALUE _11 = matrix._11, _12 = matrix._12, _13 = matrix._13, _14 = matrix._14;
    const D3DVALUE _21 = matrix._21, _22 = matrix._22, _23 = matrix._23, _24 = matrix._24;
    const D3DVALUE _31 = matrix._31, _32 = matrix._32, _33 = matrix._33, _34 = matrix._34;
    const D3DVALUE _41 = matrix._41, _42 = matrix._42, _43 = matrix._43, _44 = matrix._44;
    int nRetFlag = TRV_VISIBLE_NOCLIP, nRetFlag1 = TRV_CLIP_ALL;
    float x, y, z, xp, yp, zp, wp;
    int i, i4;

    for (i = 0, i4 = 0; i < nCount; i++)
    {
        // 0
        x = pSrc[i].x[0];
        y = pSrc[i].y[0];
        z = pSrc[i].z[0];
        xp = pDst[i4].x = _11 * x + _21 * y + _31 * z + _41;
        yp = pDst[i4].y = _12 * x + _22 * y + _32 * z + _42;
        zp = pDst[i4].z = _13 * x + _23 * y + _33 * z + _43;
        wp = pDst[i4].w = _14 * x + _24 * y + _34 * z + _44;
        // load data for next element
        x = pSrc[i].x[1];
        y = pSrc[i].y[1];
        z = pSrc[i].z[1];
        // continue processing this element
        nRetFlag |= pClip[i4] = CheckForViewingFrustum(xp, yp, zp, wp);
        nRetFlag1 &= pClip[i4++];
        // 1
        xp = pDst[i4].x = _11 * x + _21 * y + _31 * z + _41;
        yp = pDst[i4].y = _12 * x + _22 * y + _32 * z + _42;
        zp = pDst[i4].z = _13 * x + _23 * y + _33 * z + _43;
        wp = pDst[i4].w = _14 * x + _24 * y + _34 * z + _44;
        // load data for next element
        x = pSrc[i].x[2];
        y = pSrc[i].y[2];
        z = pSrc[i].z[2];
        // continue processing this element
        nRetFlag |= pClip[i4] = CheckForViewingFrustum(xp, yp, zp, wp);
        nRetFlag1 &= pClip[i4++];
        // 2
        xp = pDst[i4].x = _11 * x + _21 * y + _31 * z + _41;
        yp = pDst[i4].y = _12 * x + _22 * y + _32 * z + _42;
        zp = pDst[i4].z = _13 * x + _23 * y + _33 * z + _43;
        wp = pDst[i4].w = _14 * x + _24 * y + _34 * z + _44;
        // load data for next element
        x = pSrc[i].x[3];
        y = pSrc[i].y[3];
        z = pSrc[i].z[3];
        // continue processing this element
        nRetFlag |= pClip[i4] = CheckForViewingFrustum(xp, yp, zp, wp);
        nRetFlag1 &= pClip[i4++];
        // 3
        xp = pDst[i4].x = _11 * x + _21 * y + _31 * z + _41;
        yp = pDst[i4].y = _12 * x + _22 * y + _32 * z + _42;
        zp = pDst[i4].z = _13 * x + _23 * y + _33 * z + _43;
        wp = pDst[i4].w = _14 * x + _24 * y + _34 * z + _44;
        nRetFlag |= pClip[i4] = CheckForViewingFrustum(xp, yp, zp, wp);
        nRetFlag1 &= pClip[i4++];
    }

    if (nRetFlag1)
        return TRV_INVISIBLE;
    else if (nRetFlag == TRV_VISIBLE_NOCLIP)
        return TRV_VISIBLE_NOCLIP;
    else
        return TRV_VISIBLE_CLIP;
}
/////////////////////////////////////////////////////////////////////////////////////
// transform shadow on plane ZERO and convert x and y to int
int CTLPipelineFPU::TransformShadow(const SHMatrix &matrix, SXYZW4 *lpSrc, POINT *lpDst, int nCount)
{
    const float _11 = matrix._11, _12 = matrix._12, _13 = matrix._13, _14 = matrix._14;
    const float _21 = matrix._21, _22 = matrix._22, _23 = matrix._23, _24 = matrix._24;
    const float _31 = matrix._31, _32 = matrix._32, _33 = matrix._33, _34 = matrix._34;
    const float _41 = matrix._41, _42 = matrix._42, _43 = matrix._43, _44 = matrix._44;
    float x[4], y[4], z[4];
    int i, i4;

    for (i = 0, i4 = 0; i < nCount; i++)
    {
        Copy16Bytes(x, lpSrc[i].x);
        Copy16Bytes(y, lpSrc[i].y);
        Copy16Bytes(z, lpSrc[i].z);
        // 1st
        lpDst[i4].x = MINT(_11 * x[0] + _21 * y[0] + _31 * z[0] + _41);
        lpDst[i4].y = MINT(_12 * x[0] + _22 * y[0] + _32 * z[0] + _42);
        i4++;
        // 2nd
        lpDst[i4].x = MINT(_11 * x[1] + _21 * y[1] + _31 * z[1] + _41);
        lpDst[i4].y = MINT(_12 * x[1] + _22 * y[1] + _32 * z[1] + _42);
        i4++;
        // 3rd
        lpDst[i4].x = MINT(_11 * x[2] + _21 * y[2] + _31 * z[2] + _41);
        lpDst[i4].y = MINT(_12 * x[2] + _22 * y[2] + _32 * z[2] + _42);
        i4++;
        // 4th
        lpDst[i4].x = MINT(_11 * x[3] + _21 * y[3] + _31 * z[3] + _41);
        lpDst[i4].y = MINT(_12 * x[3] + _22 * y[3] + _32 * z[3] + _42);
        i4++;
    }

    return TRV_VISIBLE_NOCLIP;
}
/////////////////////////////////////////////////////////////////////////////////////
// int CTLPipelineFPU::TransformShadow( const SHMatrix& matrix, SXYZW4* lpSrc, CPlot2D* lpDst, int
// nCount ) transform shadow on plane ZERO
int CTLPipelineFPU::TransformShadow(const SHMatrix &matrix, SXYZW4 *lpSrc, CPlot2D *lpDst,
                                    int nCount, CTRect<float> *pRect)
{
    const float _11 = matrix._11, _12 = matrix._21, _13 = matrix._31, _14 = matrix._41;
    const float _21 = matrix._12, _22 = matrix._22, _23 = matrix._32, _24 = matrix._42;
    const float _31 = matrix._13, _32 = matrix._23, _33 = matrix._33, _34 = matrix._43;
    const float _41 = matrix._14, _42 = matrix._24, _43 = matrix._34, _44 = matrix._44;
    float x[4], y[4], z[4];
    int i, i4;

    for (i = 0, i4 = 0; i < nCount; i++)
    {
        Copy16Bytes(x, lpSrc[i].x);
        Copy16Bytes(y, lpSrc[i].y);
        Copy16Bytes(z, lpSrc[i].z);
        // 1st
        lpDst[i4].x = _11 * x[0] + _21 * y[0] + _31 * z[0] + _41;
        lpDst[i4].y = _12 * x[0] + _22 * y[0] + _32 * z[0] + _42;
        pRect->left = Min(lpDst[i4].x, pRect->left);
        pRect->right = Max(lpDst[i4].x, pRect->right);
        pRect->top = Min(lpDst[i4].y, pRect->top);
        pRect->bottom = Max(lpDst[i4].y, pRect->bottom);
        i4++;
        // 2nd
        lpDst[i4].x = _11 * x[1] + _21 * y[1] + _31 * z[1] + _41;
        lpDst[i4].y = _12 * x[1] + _22 * y[1] + _32 * z[1] + _42;
        pRect->left = Min(lpDst[i4].x, pRect->left);
        pRect->right = Max(lpDst[i4].x, pRect->right);
        pRect->top = Min(lpDst[i4].y, pRect->top);
        pRect->bottom = Max(lpDst[i4].y, pRect->bottom);
        i4++;
        // 3rd
        lpDst[i4].x = _11 * x[2] + _21 * y[2] + _31 * z[2] + _41;
        lpDst[i4].y = _12 * x[2] + _22 * y[2] + _32 * z[2] + _42;
        pRect->left = Min(lpDst[i4].x, pRect->left);
        pRect->right = Max(lpDst[i4].x, pRect->right);
        pRect->top = Min(lpDst[i4].y, pRect->top);
        pRect->bottom = Max(lpDst[i4].y, pRect->bottom);
        i4++;
        // 4th
        lpDst[i4].x = _11 * x[3] + _21 * y[3] + _31 * z[3] + _41;
        lpDst[i4].y = _12 * x[3] + _22 * y[3] + _32 * z[3] + _42;
        pRect->left = Min(lpDst[i4].x, pRect->left);
        pRect->right = Max(lpDst[i4].x, pRect->right);
        pRect->top = Min(lpDst[i4].y, pRect->top);
        pRect->bottom = Max(lpDst[i4].y, pRect->bottom);
        i4++;
    }

    return TRV_VISIBLE_NOCLIP;
}
/////////////////////////////////////////////////////////////////////////////////////
// void CTLPipelineFPU::CastProjectionToScreenNC( D3DTLVERTEX* vertices, int nVertices )
// transform vertices from projection space to screen w/o care about 'wp'
void CTLPipelineFPU::CastProjectionToScreenNC(D3DTLVERTEX *vertices, int nVertices)
{
    register float fpClipHW1 = fpClipHW;
    register float fpClipHH1 = fpClipHH;
    float rhw;
    int i;

    for (i = 0; i < nVertices; i++)
    {
        rhw = vertices[i].rhw = 1.0f / vertices[i].rhw;
        vertices[i].sx = (1.0f + vertices[i].sx * rhw) * fpClipHW1;
        vertices[i].sy = (1.0f - vertices[i].sy * rhw) * fpClipHH1;
        vertices[i].sz = vertices[i].sz * rhw;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// void CTLPipelineFPU::CastProjectionToScreenC( D3DTLVERTEX* vertices, int nVertices )
// transform vertices from projection space to screen with care about 'wp'
void CTLPipelineFPU::CastProjectionToScreenC(D3DTLVERTEX *vertices, int nVertices)
{
    register float fpClipHW1 = fpClipHW;
    register float fpClipHH1 = fpClipHH;
    float rhw;
    int i;
    // do unrolled loop
    for (i = 0; i < nVertices - (nVertices & 0x1); i++)
    {
        rhw = vertices[i].rhw = (vertices[i].rhw == 0) ? 1000000.0f : 1.0f / vertices[i].rhw;
        vertices[i].sx = (1.0f + vertices[i].sx * rhw) * fpClipHW1;
        vertices[i].sy = (1.0f - vertices[i].sy * rhw) * fpClipHH1;
        vertices[i].sz = vertices[i].sz * rhw;
        i++;

        rhw = vertices[i].rhw = (vertices[i].rhw == 0) ? 1000000.0f : 1.0f / vertices[i].rhw;
        vertices[i].sx = (1.0f + vertices[i].sx * rhw) * fpClipHW1;
        vertices[i].sy = (1.0f - vertices[i].sy * rhw) * fpClipHH1;
        vertices[i].sz = vertices[i].sz * rhw;
    }
    // do remained elements
    for (i; i < nVertices; i++)
    {
        rhw = vertices[i].rhw = (vertices[i].rhw == 0) ? 1000000.0f : 1.0f / vertices[i].rhw;
        vertices[i].sx = (1.0f + vertices[i].sx * rhw) * fpClipHW1;
        vertices[i].sy = (1.0f - vertices[i].sy * rhw) * fpClipHH1;
        vertices[i].sz = vertices[i].sz * rhw;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// void CTLPipelineFPU::CastProjectionToScreenNC( SXYZW* plots, int nPlots )
// transform vertices from projection space to screen w/o care about 'wp'
void CTLPipelineFPU::CastProjectionToScreenNC(SXYZW *plots, int nPlots)
{
    register float fpClipHW1 = fpClipHW;
    register float fpClipHH1 = fpClipHH;
    float rhw;
    int i;

    for (i = 0; i < nPlots; i++)
    {
        rhw = plots[i].w = 1.0f / plots[i].w;
        plots[i].x = (1.0f + plots[i].x * rhw) * fpClipHW1;
        plots[i].y = (1.0f - plots[i].y * rhw) * fpClipHH1;
        plots[i].z = plots[i].z * rhw;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// void CTLPipelineFPU::CastProjectionToScreenC( SXYZW* plots, int nPlots )
// transform vertices from projection space to screen with care about 'wp'
void CTLPipelineFPU::CastProjectionToScreenC(SXYZW *plots, int nPlots)
{
    ASSERT(0);
    return;
    register float fpClipHW1 = fpClipHW;
    register float fpClipHH1 = fpClipHH;
    float rhw;
    int i;
    // do unrolled loop
    for (i = 0; i < nPlots - (nPlots & 0x1); i++)
    {
        rhw = plots[i].w = (plots[i].w == 0) ? 1000000.0f : 1.0f / plots[i].w;
        plots[i].x = (1.0f + plots[i].x * rhw) * fpClipHW1;
        plots[i].y = (1.0f - plots[i].y * rhw) * fpClipHH1;
        plots[i].z = plots[i].z * rhw;
        i++;

        rhw = plots[i].w = (plots[i].w == 0) ? 1000000.0f : 1.0f / plots[i].w;
        plots[i].x = (1.0f + plots[i].x * rhw) * fpClipHW1;
        plots[i].y = (1.0f - plots[i].y * rhw) * fpClipHH1;
        plots[i].z = plots[i].z * rhw;
    }
    // do remained elements
    for (i; i < nPlots; i++)
    {
        rhw = plots[i].w = (plots[i].w == 0) ? 1000000.0f : 1.0f / plots[i].w;
        plots[i].x = (1.0f + plots[i].x * rhw) * fpClipHW1;
        plots[i].y = (1.0f - plots[i].y * rhw) * fpClipHH1;
        plots[i].z = plots[i].z * rhw;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// void CTLPipelineFPU::AddFog( D3DTLVERTEX* vertices, int nSize )
// add fog to all vertices
void CTLPipelineFPU::AddFog(D3DTLVERTEX *vertices, int nSize)
{
    register float fpPrj43 = projection._43;
    register float fpPrj34 = projection._34;
    register float fpPrj33 = projection._33;
    register float fpFogStart1 = fpFogStart;
    DWORD dwVal, dwSpecular;
    int i = nSize;
    float z, val;

    while (i) // for (  i=0; i<nSize; i++ )
    {
        i--;
        //
        z = fpPrj43 / (vertices[i].sz * fpPrj34 - fpPrj33);
        if (z < fpFogStart1)
            continue;
        val = (z - fpFogStart1) * fpRcprFogDiff255;
        val = 255.0f - Min(255.0f, val);
        dwSpecular = vertices[i].specular & 0xFF000000;
        dwVal = MINT(val) << 24;
        MINIMIZE_UINT(dwVal, dwSpecular);
        vertices[i].specular = (vertices[i].specular & 0x00FFFFFF) | dwVal;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// void CTLPipelineFPU::MakeVertex( const float alpha, D3DTLVERTEX* pVertex0, D3DTLVERTEX* pVertex1,
// D3DTLVERTEX* pResultVertex ) interpolate two vertices with coeff 'alpha'
void CTLPipelineFPU::MakeVertex(const float alpha, D3DTLVERTEX *pVertex0, D3DTLVERTEX *pVertex1,
                                D3DTLVERTEX *pResultVertex)
{
    int r1, g1, b1, a1, r2, g2, b2, a2;
    // process geometry
    pResultVertex->sx = pVertex0->sx + alpha * (pVertex1->sx - pVertex0->sx);
    pResultVertex->sy = pVertex0->sy + alpha * (pVertex1->sy - pVertex0->sy);
    pResultVertex->sz = pVertex0->sz + alpha * (pVertex1->sz - pVertex0->sz);
    pResultVertex->rhw = pVertex0->rhw + alpha * (pVertex1->rhw - pVertex0->rhw);
    // process mapping
    pResultVertex->tu = pVertex0->tu + alpha * (pVertex1->tu - pVertex0->tu);
    pResultVertex->tv = pVertex0->tv + alpha * (pVertex1->tv - pVertex0->tv);
    // process diffuse color
    // red
    r1 = RGBA_GETRED(pVertex0->color);
    r2 = RGBA_GETRED(pVertex1->color);
    r1 += MINT(alpha * (r2 - r1));
    // green
    g1 = RGBA_GETGREEN(pVertex0->color);
    g2 = RGBA_GETGREEN(pVertex1->color);
    g1 += MINT(alpha * (g2 - g1));
    // blue
    b1 = RGBA_GETBLUE(pVertex0->color);
    b2 = RGBA_GETBLUE(pVertex1->color);
    b1 += MINT(alpha * (b2 - b1));
    // alpha
    a1 = RGBA_GETALPHA(pVertex0->color);
    a2 = RGBA_GETALPHA(pVertex1->color);
    a1 += MINT(alpha * (a2 - a1));
    // compose result diffuse color
    pResultVertex->color = RGBA_MAKE(r1, g1, b1, a1);
    // process specular
    // red
    r1 = RGBA_GETRED(pVertex0->specular);
    r2 = RGBA_GETRED(pVertex1->specular);
    r1 += MINT(alpha * (r2 - r1));
    // green
    g1 = RGBA_GETGREEN(pVertex0->specular);
    g2 = RGBA_GETGREEN(pVertex1->specular);
    g1 += MINT(alpha * (g2 - g1));
    // blue
    b1 = RGBA_GETBLUE(pVertex0->specular);
    b2 = RGBA_GETBLUE(pVertex1->specular);
    b1 += MINT(alpha * (b2 - b1));
    // alpha
    a1 = RGBA_GETALPHA(pVertex0->specular);
    a2 = RGBA_GETALPHA(pVertex1->specular);
    a1 += MINT(alpha * (a2 - a1));
    // compose result specular color
    pResultVertex->specular = RGBA_MAKE(r1, g1, b1, a1);
}
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
//                            Clipping functions                                    //
// ******************************************************************************** //
//////////////////////////////////////////////////////////////////////////////////////
__inline BYTE IsInvisible(BYTE clip1, BYTE clip2, BYTE clip3)
{
    return (clip1 & clip2 & clip3 & TRV_CLIP_ALL);
}
__inline BYTE IsNeedClip(BYTE clip1, BYTE clip2, BYTE clip3)
{
    return (clip1 | clip2 | clip3);
}
__inline void CTLPipelineFPU::MakeMinXVertex(D3DTLVERTEX *pVertex0, D3DTLVERTEX *pVertex1,
                                             D3DTLVERTEX *pResultVertex)
{
    float alpha = (pVertex0->sx + pVertex0->rhw) /
                  ((pVertex0->sx + pVertex0->rhw) - (pVertex1->sx + pVertex1->rhw));
    CTLPipelineFPU::MakeVertex(alpha, pVertex0, pVertex1, pResultVertex);
}
__inline void CTLPipelineFPU::MakeMaxXVertex(D3DTLVERTEX *pVertex0, D3DTLVERTEX *pVertex1,
                                             D3DTLVERTEX *pResultVertex)
{
    float alpha = (pVertex0->sx - pVertex0->rhw) /
                  ((pVertex0->sx - pVertex0->rhw) - (pVertex1->sx - pVertex1->rhw));
    CTLPipelineFPU::MakeVertex(alpha, pVertex0, pVertex1, pResultVertex);
}
__inline void CTLPipelineFPU::MakeMinYVertex(D3DTLVERTEX *pVertex0, D3DTLVERTEX *pVertex1,
                                             D3DTLVERTEX *pResultVertex)
{
    float alpha = (pVertex0->sy + pVertex0->rhw) /
                  ((pVertex0->sy + pVertex0->rhw) - (pVertex1->sy + pVertex1->rhw));
    CTLPipelineFPU::MakeVertex(alpha, pVertex0, pVertex1, pResultVertex);
}
__inline void CTLPipelineFPU::MakeMaxYVertex(D3DTLVERTEX *pVertex0, D3DTLVERTEX *pVertex1,
                                             D3DTLVERTEX *pResultVertex)
{
    float alpha = (pVertex0->sy - pVertex0->rhw) /
                  ((pVertex0->sy - pVertex0->rhw) - (pVertex1->sy - pVertex1->rhw));
    CTLPipelineFPU::MakeVertex(alpha, pVertex0, pVertex1, pResultVertex);
}
__inline void CTLPipelineFPU::MakeMinZVertex(D3DTLVERTEX *pVertex0, D3DTLVERTEX *pVertex1,
                                             D3DTLVERTEX *pResultVertex)
{
    float alpha = pVertex0->sz / (pVertex0->sz - pVertex1->sz);
    CTLPipelineFPU::MakeVertex(alpha, pVertex0, pVertex1, pResultVertex);
}
__inline void CTLPipelineFPU::MakeMaxZVertex(D3DTLVERTEX *pVertex0, D3DTLVERTEX *pVertex1,
                                             D3DTLVERTEX *pResultVertex)
{
    float alpha = (pVertex0->sz - pVertex0->rhw) /
                  ((pVertex0->sz - pVertex0->rhw) - (pVertex1->sz - pVertex1->rhw));
    CTLPipelineFPU::MakeVertex(alpha, pVertex0, pVertex1, pResultVertex);
}
/////////////////////////////////////////////////////////////////////////////////////
void CTLPipelineFPU::ClipMinX(D3DTLVERTEX *vertices, BYTE *clips, int &nVertices, WORD *indices,
                              int &nIndices)
{
    int i, index1, index2;
    BYTE clip1, clip2;

    nNewIndices = 0;
    // process vertices
    index2 = indices[0];
    clip2 = clips[index2];
    for (i = 0; i < nIndices; i++)
    {
        index1 = index2;
        index2 = indices[(i + 1) % nIndices];
        clip1 = clip2;
        clip2 = clips[index2];

        if (!(clip1 & TRV_CLIP_MINX))
            clipindices[nNewIndices++] = index1;

        if ((clip1 ^ clip2) & TRV_CLIP_MINX)
        {
            MakeMinXVertex(&vertices[index1], &vertices[index2], &vertices[nVertices]);
            clips[nVertices] = CheckForViewingFrustum(vertices[nVertices].sx,
                                                      vertices[nVertices].sy,
                                                      vertices[nVertices].sz,
                                                      vertices[nVertices].rhw);
            clipindices[nNewIndices++] = nVertices;
            nVertices++;
        }
    }

    memcpy(indices, clipindices.GetData(), sizeof(WORD) * nNewIndices);
    nIndices = nNewIndices;
}
/////////////////////////////////////////////////////////////////////////////////////
void CTLPipelineFPU::ClipMaxX(D3DTLVERTEX *vertices, BYTE *clips, int &nVertices, WORD *indices,
                              int &nIndices)
{
    int i, index1, index2;
    BYTE clip1, clip2;

    nNewIndices = 0;
    // process vertices
    index2 = indices[0];
    clip2 = clips[index2];
    for (i = 0; i < nIndices; i++)
    {
        index1 = index2;
        index2 = indices[(i + 1) % nIndices];
        clip1 = clip2;
        clip2 = clips[index2];

        if (!(clip1 & TRV_CLIP_MAXX))
            clipindices[nNewIndices++] = index1;

        if ((clip1 ^ clip2) & TRV_CLIP_MAXX)
        {
            MakeMaxXVertex(&vertices[index1], &vertices[index2], &vertices[nVertices]);
            clips[nVertices] = CheckForViewingFrustum(vertices[nVertices].sx,
                                                      vertices[nVertices].sy,
                                                      vertices[nVertices].sz,
                                                      vertices[nVertices].rhw);
            clipindices[nNewIndices++] = nVertices;
            nVertices++;
        }
    }

    memcpy(indices, clipindices.GetData(), sizeof(WORD) * nNewIndices);
    nIndices = nNewIndices;
}
/////////////////////////////////////////////////////////////////////////////////////
void CTLPipelineFPU::ClipMinY(D3DTLVERTEX *vertices, BYTE *clips, int &nVertices, WORD *indices,
                              int &nIndices)
{
    int i, index1, index2;
    BYTE clip1, clip2;

    nNewIndices = 0;
    // process vertices
    index2 = indices[0];
    clip2 = clips[index2];
    for (i = 0; i < nIndices; i++)
    {
        index1 = index2;
        index2 = indices[(i + 1) % nIndices];
        clip1 = clip2;
        clip2 = clips[index2];

        if (!(clip1 & TRV_CLIP_MINY))
            clipindices[nNewIndices++] = index1;

        if ((clip1 ^ clip2) & TRV_CLIP_MINY)
        {
            MakeMinYVertex(&vertices[index1], &vertices[index2], &vertices[nVertices]);
            clips[nVertices] = CheckForViewingFrustum(vertices[nVertices].sx,
                                                      vertices[nVertices].sy,
                                                      vertices[nVertices].sz,
                                                      vertices[nVertices].rhw);
            clipindices[nNewIndices++] = nVertices;
            nVertices++;
        }
    }

    memcpy(indices, clipindices.GetData(), sizeof(WORD) * nNewIndices);
    nIndices = nNewIndices;
}
/////////////////////////////////////////////////////////////////////////////////////
void CTLPipelineFPU::ClipMaxY(D3DTLVERTEX *vertices, BYTE *clips, int &nVertices, WORD *indices,
                              int &nIndices)
{
    int i, index1, index2;
    BYTE clip1, clip2;

    nNewIndices = 0;
    // process vertices
    index2 = indices[0];
    clip2 = clips[index2];
    for (i = 0; i < nIndices; i++)
    {
        index1 = index2;
        index2 = indices[(i + 1) % nIndices];
        clip1 = clip2;
        clip2 = clips[index2];

        if (!(clip1 & TRV_CLIP_MAXY))
            clipindices[nNewIndices++] = index1;

        if ((clip1 ^ clip2) & TRV_CLIP_MAXY)
        {
            MakeMaxYVertex(&vertices[index1], &vertices[index2], &vertices[nVertices]);
            clips[nVertices] = CheckForViewingFrustum(vertices[nVertices].sx,
                                                      vertices[nVertices].sy,
                                                      vertices[nVertices].sz,
                                                      vertices[nVertices].rhw);
            clipindices[nNewIndices++] = nVertices;
            nVertices++;
        }
    }

    memcpy(indices, clipindices.GetData(), sizeof(WORD) * nNewIndices);
    nIndices = nNewIndices;
}
/////////////////////////////////////////////////////////////////////////////////////
void CTLPipelineFPU::ClipMinZ(D3DTLVERTEX *vertices, BYTE *clips, int &nVertices, WORD *indices,
                              int &nIndices)
{
    int i, index1, index2;
    BYTE clip1, clip2;

    nNewIndices = 0;
    // process vertices
    index2 = indices[0];
    clip2 = clips[index2];
    for (i = 0; i < nIndices; i++)
    {
        index1 = index2;
        index2 = indices[(i + 1) % nIndices];
        clip1 = clip2;
        clip2 = clips[index2];

        if (!(clip1 & TRV_CLIP_MINZ))
            clipindices[nNewIndices++] = index1;

        if ((clip1 ^ clip2) & TRV_CLIP_MINZ)
        {
            MakeMinZVertex(&vertices[index1], &vertices[index2], &vertices[nVertices]);
            clips[nVertices] = CheckForViewingFrustum(vertices[nVertices].sx,
                                                      vertices[nVertices].sy,
                                                      vertices[nVertices].sz,
                                                      vertices[nVertices].rhw);
            clipindices[nNewIndices++] = nVertices;
            nVertices++;
        }
    }

    memcpy(indices, clipindices.GetData(), sizeof(WORD) * nNewIndices);
    nIndices = nNewIndices;
}
/////////////////////////////////////////////////////////////////////////////////////
void CTLPipelineFPU::ClipMaxZ(D3DTLVERTEX *vertices, BYTE *clips, int &nVertices, WORD *indices,
                              int &nIndices)
{
    int i, index1, index2;
    BYTE clip1, clip2;

    nNewIndices = 0;
    // process vertices
    index2 = indices[0];
    clip2 = clips[index2];
    for (i = 0; i < nIndices; i++)
    {
        index1 = index2;
        index2 = indices[(i + 1) % nIndices];
        clip1 = clip2;
        clip2 = clips[index2];

        if (!(clip1 & TRV_CLIP_MAXZ))
            clipindices[nNewIndices++] = index1;

        if ((clip1 ^ clip2) & TRV_CLIP_MAXZ)
        {
            MakeMaxZVertex(&vertices[index1], &vertices[index2], &vertices[nVertices]);
            clips[nVertices] = CheckForViewingFrustum(vertices[nVertices].sx,
                                                      vertices[nVertices].sy,
                                                      vertices[nVertices].sz,
                                                      vertices[nVertices].rhw);
            clipindices[nNewIndices++] = nVertices;
            nVertices++;
        }
    }

    memcpy(indices, clipindices.GetData(), sizeof(WORD) * nNewIndices);
    nIndices = nNewIndices;
}
/////////////////////////////////////////////////////////////////////////////////////
int CTLPipelineFPU::ClipFace(D3DTLVERTEX *vertices, BYTE *clips, WORD *indices, int &nVertices,
                             int &nIndices)
{
    BYTE clip1 = clips[indices[0]], clip2 = clips[indices[1]], clip3 = clips[indices[2]];
    BYTE clip = IsNeedClip(clip1, clip2, clip3);
    int i;

    nIndices = 3;

    if (IsInvisible(clip1, clip2, clip3))
    {
        nIndices = 0;
        return -1;
    }
    else
    {
        if (clip)
        {
            // clip
            if (clip & TRV_CLIP_MINZ)
                ClipMinZ(vertices, clips, nVertices, indices, nIndices);
            if (clip & TRV_CLIP_MAXZ)
                ClipMaxZ(vertices, clips, nVertices, indices, nIndices);
            if (clip & TRV_CLIP_MINX)
                ClipMinX(vertices, clips, nVertices, indices, nIndices);
            if (clip & TRV_CLIP_MAXX)
                ClipMaxX(vertices, clips, nVertices, indices, nIndices);
            if (clip & TRV_CLIP_MINY)
                ClipMinY(vertices, clips, nVertices, indices, nIndices);
            if (clip & TRV_CLIP_MAXY)
                ClipMaxY(vertices, clips, nVertices, indices, nIndices);

            // triangulate
            nIndices = 0;
            for (i = 2; i < nNewIndices; i++)
            {
                indices[nIndices++] = clipindices[0];
                indices[nIndices++] = clipindices[i - 1];
                indices[nIndices++] = clipindices[i];
            }
            return 1;
        }
        else
        {
            nIndices = 3;
            return 0;
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CTLPipelineFPU::ClipFaces(D3DTLVERTEX *vertices, BYTE *cClips, WORD *wInputIndices,
                               WORD *wOutputIndices, int &nNumVertices, int &nNumIndices)
{
    // perform clipping
    int nIndices = 0, nIndices1 = 0;
    int nVertices = nNumVertices;
    for (int i = 0; i < nNumIndices;)
    {
        wOutputIndices[nIndices1 + 0] = wInputIndices[i++];
        wOutputIndices[nIndices1 + 1] = wInputIndices[i++];
        wOutputIndices[nIndices1 + 2] = wInputIndices[i++];
        CTLPipelineFPU::ClipFace(vertices, cClips, wOutputIndices + nIndices1, nVertices, nIndices);
        nIndices1 += nIndices;
    }

    nNumVertices = nVertices;
    nNumIndices = nIndices1;
}
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
//                            Lighting functions                                    //
// ******************************************************************************** //
//////////////////////////////////////////////////////////////////////////////////////
// void CTLPipelineFPU::LightVerticesGeneralPt...
// light vertices (specular and diffuse components) with sunlight and point lights
void CTLPipelineFPU::LightVerticesGeneralPt(SXYZW4 *plots, SXYZW4 *norms, LPDWORD colors,
                                            LPDWORD speculars, SGvNvComponents *components,
                                            int nComponents, CLight **lights, int nLights,
                                            float fpMaterialPower, const D3DVECTOR &dvEmissive,
                                            const CPlot3D &dc, const BYTE cFog,
                                            DWORD bFlatPointLights)
{
    int i, j, hindexgv, lindexgv, hindexnv, lindexnv, gv, nv;
    D3DCOLORVALUE color, specular;
    CPlot3D nl, nr, nc, dc1, lcp, dl, dr;
    float cosAlpha, cosBeta;
    float dl2, k, kdist;

    const D3DCOLORVALUE &sdf = lpSunlight->GetMaterialDiffuse();
    const D3DCOLORVALUE &ssp = lpSunlight->GetMaterialSpecular();
    const CPlot3D &snl = lpSunlight->GetRespectDirection();

    for (i = 0; i < nComponents; i++)
    {
        gv = components[i].gv;
        nv = components[i].nv;

        hindexgv = gv >> 2;
        lindexgv = gv & 0x3;

        hindexnv = nv >> 2;
        lindexnv = nv & 0x3;
        // process sunlight (moonlight)
        nr = CPlot3D(norms[hindexnv].x[lindexnv], norms[hindexnv].y[lindexnv],
                     norms[hindexnv].z[lindexnv]);
        cosAlpha = nr * snl;

        color.r = Max(dcvAmbientColor.r, sdf.r * cosAlpha);
        color.g = Max(dcvAmbientColor.g, sdf.g * cosAlpha);
        color.b = Max(dcvAmbientColor.b, sdf.b * cosAlpha);
        color.a = sdf.a;

        dr.SetPlot(plots[hindexgv].x[lindexgv], plots[hindexgv].y[lindexgv],
                   plots[hindexgv].z[lindexgv]);
        dc1.SetPlot(dc - dr);
        nc.SetPlot(dc1 * (1.0f / fabs(dc1)));
        cosBeta = ((cosAlpha + cosAlpha) * nr - snl) * nc;
        if (cosBeta > 0)
        {
            cosBeta = 1.0f - fpMaterialPower * (1.0f - cosBeta);
            //			cosBeta = (float)pow( cosBeta, fpMaterialPower );
            specular.r = Max(0.0f, ssp.r * cosBeta);
            specular.g = Max(0.0f, ssp.g * cosBeta);
            specular.b = Max(0.0f, ssp.b * cosBeta);
            specular.a = cFog;
        }
        else
        {
            specular.r = 0;
            specular.g = 0;
            specular.b = 0;
            specular.a = cFog;
        }

        for (j = 0; j < nLights; j++)
        {
            dl = lights[j]->GetRespectCP() - dr;

            dl2 = dl * dl;
            kdist = 1.0f - dl2 * lights[j]->GetRcprRange2();
            if (kdist < 0)
                continue;
            nl = dl * (1.0f / float(sqrt(dl2)));

            cosAlpha = nr * nl;
            k = kdist * cosAlpha;

            const D3DCOLORVALUE &df = lights[j]->GetMaterialDiffuse();
            const D3DCOLORVALUE &sp = lights[j]->GetMaterialSpecular();

            color.r = Max(color.r, df.r * k);
            color.g = Max(color.g, df.g * k);
            color.b = Max(color.b, df.b * k);

            cosBeta = ((cosAlpha + cosAlpha) * nr - nl) * nc;
            if (cosBeta < 0)
                continue;
            cosBeta = 1.0f - fpMaterialPower * (1.0f - cosBeta);
            //			cosBeta = (float)pow( cosBeta, fpMaterialPower );
            k = kdist * cosBeta;
            specular.r = Max(specular.r, sp.r * k);
            specular.g = Max(specular.g, sp.g * k);
            specular.b = Max(specular.b, sp.b * k);
        }

        color.r = Min(1.0f, color.r + dvEmissive.x) * 255.0f;
        color.g = Min(1.0f, color.g + dvEmissive.y) * 255.0f;
        color.b = Min(1.0f, color.b + dvEmissive.z) * 255.0f;
        color.a *= 255.0f;

        colors[i] = RGBA_MAKE(MINT(color.r), MINT(color.g), MINT(color.b), MINT(color.a));
        speculars[i] = RGBA_MAKE(MINT(specular.r * 255.0f), MINT(specular.g * 255.0f),
                                 MINT(specular.b * 255.0f), MINT(specular.a));
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// void CTLPipelineFPU::LightVerticesDfPt
// light diffuse component of vertices with sunlight and point lights
void CTLPipelineFPU::LightVerticesDfPt(SXYZW4 *plots, SXYZW4 *norms, LPDWORD colors,
                                       SGvNvComponents *components, int nComponents,
                                       CLight **lights, int nLights, const D3DVECTOR &dvEmissive)
{
    int i, j, hindexgv, lindexgv, hindexnv, lindexnv, gv, nv;
    D3DCOLORVALUE color;
    CPlot3D nl, nr, nc, dc1, lcp, dl, dr;
    float cosAlpha;
    float dl2, k, kdist;

    const D3DCOLORVALUE &sdf = lpSunlight->GetMaterialDiffuse();
    const CPlot3D &snl = lpSunlight->GetRespectDirection();

    for (i = 0; i < nComponents; i++)
    {
        gv = components[i].gv;
        nv = components[i].nv;

        hindexgv = gv >> 2;
        lindexgv = gv & 0x3;

        hindexnv = nv >> 2;
        lindexnv = nv & 0x3;
        // process sunlight (moonlight)
        nr = CPlot3D(norms[hindexnv].x[lindexnv], norms[hindexnv].y[lindexnv],
                     norms[hindexnv].z[lindexnv]);
        cosAlpha = nr * snl;

        color.r = Max(dcvAmbientColor.r, sdf.r * cosAlpha);
        color.g = Max(dcvAmbientColor.g, sdf.g * cosAlpha);
        color.b = Max(dcvAmbientColor.b, sdf.b * cosAlpha);
        color.a = sdf.a;

        dr.SetPlot(plots[hindexgv].x[lindexgv], plots[hindexgv].y[lindexgv],
                   plots[hindexgv].z[lindexgv]);

        for (j = 0; j < nLights; j++)
        {
            lcp = lights[j]->GetRespectCP();
            dl = lcp - dr;

            dl2 = dl * dl;
            kdist = 1.0f - dl2 * lights[j]->GetRcprRange2();
            if (kdist < 0)
                continue;
            nl.SetPlot(dl * (1.0f / float(sqrt(dl2))));

            cosAlpha = nr * nl;
            k = kdist * cosAlpha;

            const D3DCOLORVALUE &df = lights[j]->GetMaterialDiffuse();

            color.r = Max(color.r, df.r * k);
            color.g = Max(color.g, df.g * k);
            color.b = Max(color.b, df.b * k);
        }

        color.r = Min(1.0f, color.r + dvEmissive.x) * 255.0f;
        color.g = Min(1.0f, color.g + dvEmissive.y) * 255.0f;
        color.b = Min(1.0f, color.b + dvEmissive.z) * 255.0f;
        color.a *= 255.0f;

        colors[i] = RGBA_MAKE(MINT(color.r), MINT(color.g), MINT(color.b), MINT(color.a));
        // colors[i] = RGBA_MAKE(255, 255, 255, 255);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// void CTLPipelineFPU::LightVerticesSpPt...
// light vertices with smoothing groups
void CTLPipelineFPU::LightVerticesSpPt(SXYZW4 *plots, SXYZW4 *norms, LPDWORD speculars,
                                       SGvNvComponents *components, int nComponents,
                                       CLight **lights, int nLights, float fpMaterialPower,
                                       const D3DVECTOR &dvEmissive, const CPlot3D &dc,
                                       const BYTE cFog)
{
    int i, j, hindexgv, lindexgv, hindexnv, lindexnv, gv, nv;
    D3DCOLORVALUE specular;
    CPlot3D nl, nr, nc, dc1, lcp, dl, dr;
    float cosAlpha, cosBeta;
    float dl2, k, kdist;

    const D3DCOLORVALUE &ssp = lpSunlight->GetMaterialSpecular();
    const CPlot3D &snl = lpSunlight->GetRespectDirection();

    for (i = 0; i < nComponents; i++)
    {
        gv = components[i].gv;
        nv = components[i].nv;

        hindexgv = gv >> 2;
        lindexgv = gv & 0x3;

        hindexnv = nv >> 2;
        lindexnv = nv & 0x3;
        // process sunlight (moonlight)
        nr.SetPlot(norms[hindexnv].x[lindexnv], norms[hindexnv].y[lindexnv],
                   norms[hindexnv].z[lindexnv]);
        dr.SetPlot(plots[hindexgv].x[lindexgv], plots[hindexgv].y[lindexgv],
                   plots[hindexgv].z[lindexgv]);
        dc1.SetPlot(dc - dr);
        nc.SetPlot(dc1 * (1.0f / fabs(dc1)));

        cosAlpha = nr * snl;
        cosBeta = ((cosAlpha + cosAlpha) * nr - snl) * nc;

        if (cosBeta > 0)
        {
            cosBeta = 1.0f - fpMaterialPower * (1.0f - cosBeta);
            //			cosBeta = (float)pow( cosBeta, fpMaterialPower );
            specular.r = Max(0.0f, ssp.r * cosBeta);
            specular.g = Max(0.0f, ssp.g * cosBeta);
            specular.b = Max(0.0f, ssp.b * cosBeta);
            specular.a = cFog;
        }
        else
        {
            specular.r = 0;
            specular.g = 0;
            specular.b = 0;
            specular.a = cFog;
        }

        for (j = 0; j < nLights; j++)
        {
            lcp = lights[j]->GetRespectCP();
            dl = lcp - dr;

            dl2 = dl * dl;
            kdist = 1.0f - dl2 * lights[j]->GetRcprRange2();
            if (kdist < 0)
                continue;
            nl.SetPlot(dl * (1.0f / float(sqrt(dl2))));

            const D3DCOLORVALUE &sp = lights[j]->GetMaterialSpecular();

            cosAlpha = nr * nl;
            cosBeta = ((cosAlpha + cosAlpha) * nr - nl) * nc;
            if (cosBeta < 0)
                continue;
            cosBeta = 1.0f - fpMaterialPower * (1.0f - cosBeta);
            //			cosBeta = (float)pow( cosBeta, fpMaterialPower );
            k = kdist * cosBeta;
            specular.r = Max(specular.r, sp.r * k);
            specular.g = Max(specular.g, sp.g * k);
            specular.b = Max(specular.b, sp.b * k);
        }

        speculars[i] = RGBA_MAKE(MINT(specular.r * 255.0f), MINT(specular.g * 255.0f),
                                 MINT(specular.b * 255.0f), MINT(specular.a));
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// void CTLPipelineFPU::LightVerticesGeneral...
// light vertices (specular and diffuse components) with sunlight and w/o point lights
void CTLPipelineFPU::LightVerticesGeneral(SXYZW4 *plots, SXYZW4 *norms, LPDWORD colors,
                                          LPDWORD speculars, SGvNvComponents *components,
                                          int nComponents, float fpMaterialPower,
                                          const D3DVECTOR &dvEmissive, const CPlot3D &dc,
                                          const BYTE cFog)
{
    int i, hindexgv, lindexgv, hindexnv, lindexnv, gv, nv;
    D3DCOLORVALUE color, specular;
    float cosAlpha, cosBeta;
    CPlot3D nr, dc1, dr;

    const D3DCOLORVALUE &sdf = lpSunlight->GetMaterialDiffuse();
    const D3DCOLORVALUE &ssp = lpSunlight->GetMaterialSpecular();
    const CPlot3D &snl = lpSunlight->GetRespectDirection();

    for (i = 0; i < nComponents; i++)
    {
        gv = components[i].gv;
        nv = components[i].nv;

        hindexgv = gv >> 2;
        lindexgv = gv & 0x3;

        hindexnv = nv >> 2;
        lindexnv = nv & 0x3;
        // process sunlight (moonlight)
        nr.SetPlot(norms[hindexnv].x[lindexnv], norms[hindexnv].y[lindexnv],
                   norms[hindexnv].z[lindexnv]);
        cosAlpha = nr * snl;

        color.r = Max(dcvAmbientColor.r, sdf.r * cosAlpha);
        color.g = Max(dcvAmbientColor.g, sdf.g * cosAlpha);
        color.b = Max(dcvAmbientColor.b, sdf.b * cosAlpha);
        color.a = sdf.a;

        dr.SetPlot(plots[hindexgv].x[lindexgv], plots[hindexgv].y[lindexgv],
                   plots[hindexgv].z[lindexgv]);
        dc1.SetPlot(dc - dr);
        //		nc = dc1*(1.0f/fabs(dc1));
        cosBeta = ((cosAlpha + cosAlpha) * nr - snl) * (dc1 * (1.0f / fabs(dc1)));
        if (cosBeta > 0)
        {
            cosBeta = 1.0f - fpMaterialPower * (1.0f - cosBeta);
            //			cosBeta = (float)pow( cosBeta, fpMaterialPower );
            specular.r = Max(0.0f, ssp.r * cosBeta) * 255.0f;
            specular.g = Max(0.0f, ssp.g * cosBeta) * 255.0f;
            specular.b = Max(0.0f, ssp.b * cosBeta) * 255.0f;
            specular.a = cFog;
        }
        else
        {
            specular.r = 0;
            specular.g = 0;
            specular.b = 0;
            specular.a = cFog;
        }

        color.r = Min(1.0f, color.r + dvEmissive.x) * 255.0f;
        color.g = Min(1.0f, color.g + dvEmissive.y) * 255.0f;
        color.b = Min(1.0f, color.b + dvEmissive.z) * 255.0f;
        color.a *= 255.0f;

        colors[i] = RGBA_MAKE(MINT(color.r), MINT(color.g), MINT(color.b), MINT(color.a));
        speculars[i] = RGBA_MAKE(MINT(specular.r), MINT(specular.g), MINT(specular.b),
                                 MINT(specular.a));
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// void CTLPipelineFPU::LightVerticesDf
// light diffuse component of vertices with sunlight
void CTLPipelineFPU::LightVerticesDf(SXYZW4 *plots, SXYZW4 *norms, LPDWORD colors,
                                     SGvNvComponents *components, int nComponents,
                                     const D3DVECTOR &dvEmissive)
{
    int i, hindexgv, lindexgv, hindexnv, lindexnv, gv, nv;
    D3DCOLORVALUE color;
    float cosAlpha;
    CPlot3D nr;

    const D3DCOLORVALUE &sdf = lpSunlight->GetMaterialDiffuse();
    const CPlot3D &snl = lpSunlight->GetRespectDirection();

    for (i = 0; i < nComponents; i++)
    {
        gv = components[i].gv;
        nv = components[i].nv;

        hindexgv = gv >> 2;
        lindexgv = gv & 0x3;

        hindexnv = nv >> 2;
        lindexnv = nv & 0x3;
        // process sunlight (moonlight)
        nr.SetPlot(norms[hindexnv].x[lindexnv], norms[hindexnv].y[lindexnv],
                   norms[hindexnv].z[lindexnv]);
        cosAlpha = nr * snl;

        color.r = Max(dcvAmbientColor.r, sdf.r * cosAlpha);
        color.g = Max(dcvAmbientColor.g, sdf.g * cosAlpha);
        color.b = Max(dcvAmbientColor.b, sdf.b * cosAlpha);
        color.a = sdf.a;

        color.r = Min(1.0f, color.r + dvEmissive.x) * 255.0f;
        color.g = Min(1.0f, color.g + dvEmissive.y) * 255.0f;
        color.b = Min(1.0f, color.b + dvEmissive.z) * 255.0f;
        color.a *= 255.0f;

        colors[i] = RGBA_MAKE(MINT(color.r), MINT(color.g), MINT(color.b), MINT(color.a));
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// void CTLPipelineFPU::LightVerticesSp...
// light specular component of vertices with sunlight
void CTLPipelineFPU::LightVerticesSp(SXYZW4 *plots, SXYZW4 *norms, LPDWORD speculars,
                                     SGvNvComponents *components, int nComponents,
                                     float fpMaterialPower, const D3DVECTOR &dvEmissive,
                                     const CPlot3D &dc, const BYTE cFog)
{
    int i, hindexgv, lindexgv, hindexnv, lindexnv, gv, nv;
    float cosAlpha, cosBeta;
    D3DCOLORVALUE specular;
    CPlot3D nr, dc1, dr;

    const D3DCOLORVALUE &ssp = lpSunlight->GetMaterialSpecular();
    const CPlot3D &snl = lpSunlight->GetRespectDirection();

    for (i = 0; i < nComponents; i++)
    {
        gv = components[i].gv;
        nv = components[i].nv;

        hindexgv = gv >> 2;
        lindexgv = gv & 0x3;

        hindexnv = nv >> 2;
        lindexnv = nv & 0x3;
        // process sunlight (moonlight)
        nr.SetPlot(norms[hindexnv].x[lindexnv], norms[hindexnv].y[lindexnv],
                   norms[hindexnv].z[lindexnv]);
        dr.SetPlot(plots[hindexgv].x[lindexgv], plots[hindexgv].y[lindexgv],
                   plots[hindexgv].z[lindexgv]);
        dc1.SetPlot(dc - dr);

        cosAlpha = nr * snl;
        cosBeta = ((cosAlpha + cosAlpha) * nr - snl) * (dc1 * (1.0f / fabs(dc1)));

        if (cosBeta > 0)
        {
            cosBeta = 1.0f - fpMaterialPower * (1.0f - cosBeta);
            //			cosBeta = (float)pow( cosBeta, fpMaterialPower );
            specular.r = Max(0.0f, ssp.r * cosBeta) * 255.0f;
            specular.g = Max(0.0f, ssp.g * cosBeta) * 255.0f;
            specular.b = Max(0.0f, ssp.b * cosBeta) * 255.0f;
            specular.a = cFog;
        }
        else
        {
            specular.r = 0;
            specular.g = 0;
            specular.b = 0;
            specular.a = cFog;
        }

        speculars[i] = RGBA_MAKE(MINT(specular.r), MINT(specular.g), MINT(specular.b),
                                 MINT(specular.a));
    }
}
/////////////////////////////////////////////////////////////////////////////////////
