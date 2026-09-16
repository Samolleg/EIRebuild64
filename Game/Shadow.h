#pragma once
/////////////////////////////////////////////////////////////////////////////////////
#include "plot.h"
/////////////////////////////////////////////////////////////////////////////////////
struct SShadowPoint
{
    CPlot3D point;
    float uv[2];
    operator D3DTLVERTEX()
    {
        D3DTLVERTEX res;
        res.sx = point.x;
        res.sy = point.y;
        res.sz = point.z;
        res.tu = uv[0];
        res.tv = uv[1];
        return res;
    }
};
/////////////////////////////////////////////////////////////////////////////////////
struct SShadowTriangle
{
    SShadowPoint pnts[3];
};
/////////////////////////////////////////////////////////////////////////////////////
class CShadowBuilder
{
    CXYZW4Array *dstVertex;
    CPlot2DArray *dstUV;
    CWORDArray *dstIndex;
    CPlot3D vecU, vecV, vecC, ptBase, ptGrow, light, ptCenter;
    float fpU0, fpV0, fpC0, fpUleng, fpVleng;
    int nDstVertexCount;
    int nDstIndexCount;
    void FinalAddTriangle(SShadowTriangle &tri);
    void AddShadowTriangle3(SShadowTriangle &tri);
    void AddShadowTriangle2(SShadowTriangle &tri);
    void AddShadowTriangle1(SShadowTriangle &tri);
    void AddShadowTriangle0(SShadowTriangle &tri);
    void AddShadowTriangle(SShadowTriangle &tri);
    void GeneratePoint(SShadowPoint &res, const CPlot3D &pnt);
    void GeneratePoint(SShadowPoint &res, const SXYZW &pnt);
    WORD AddPoint(SShadowPoint &pnt);

public:
    CShadowBuilder();
    void Setup(CXYZW4Array &dst, CPlot2DArray &dstUVs, CWORDArray &dstIdx, CXYZW4Array &src,
               const CPlot3D &_light, const CPlot3D &_ptCenter, float fpRadius);
    bool CheckSphere(const CPlot3D &ptCenter, float fpRadius);
    const CPlot3D &GetGrow() const { return ptGrow; }
    void AddTriangle(const CPlot3D &ptA, const CPlot3D &ptB, const CPlot3D &ptC);
    void AddTriangle(const SXYZW &ptA, const SXYZW &ptB, const SXYZW &ptC);
    void AllocForTriangles(int nTris);
    void Finish();
};
/////////////////////////////////////////////////////////////////////////////////////
