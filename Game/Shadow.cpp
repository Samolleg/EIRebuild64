#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "Shadow.h"
/////////////////////////////////////////////////////////////////////////////////////
inline float mixedMult(const CPlot3D &a, const CPlot3D &b, const CPlot3D &c)
{
    return a.x * b.y * c.z + a.y * b.z * c.x + a.z * b.x * c.y - a.z * b.y * c.x - a.y * b.x * c.z -
           a.x * b.z * c.y;
}
/////////////////////////////////////////////////////////////////////////////////////
// мощная работа по наложению тени ( no welding yet )
typedef SShadowPoint SSPoint;
typedef SShadowTriangle SShadowTriangle;
// calc average point
static void CalcMiddlePoint(SSPoint &a, SSPoint &b, SSPoint &res, float fpA, float fpB)
{
    float fp1 = 1 / (fpB - fpA);
    res.point.x = (a.point.x * fpB - b.point.x * fpA) * fp1;
    res.point.y = (a.point.y * fpB - b.point.y * fpA) * fp1;
    res.point.z = (a.point.z * fpB - b.point.z * fpA) * fp1;
    res.uv[0] = (a.uv[0] * fpB - b.uv[0] * fpA) * fp1;
    res.uv[1] = (a.uv[1] * fpB - b.uv[1] * fpA) * fp1;
}

static SSPoint splitP1, splitP2;
// function returns number of resulting triangles
static int SplitTriangle(SShadowTriangle &tri, SShadowTriangle &tri1, float fp0, float fp1,
                         float fp2)
{
    int nCase = IsPositive(fp0) + IsPositive(fp1) * 2 + IsPositive(fp2) * 4;
    switch (nCase)
    {
    case 0:
        return 0;
    case 1:
        CalcMiddlePoint(tri.pnts[0], tri.pnts[1], splitP1, fp0, fp1);
        CalcMiddlePoint(tri.pnts[0], tri.pnts[2], splitP2, fp0, fp2);
        tri.pnts[1] = splitP1;
        tri.pnts[2] = splitP2;
        return 1;
    case 2:
        CalcMiddlePoint(tri.pnts[0], tri.pnts[1], splitP1, fp0, fp1);
        CalcMiddlePoint(tri.pnts[1], tri.pnts[2], splitP2, fp1, fp2);
        tri.pnts[0] = splitP1;
        tri.pnts[2] = splitP2;
        return 1;
    case 3:
        CalcMiddlePoint(tri.pnts[1], tri.pnts[2], splitP1, fp1, fp2);
        CalcMiddlePoint(tri.pnts[0], tri.pnts[2], splitP2, fp0, fp2);
        tri.pnts[2] = splitP2;
        tri1.pnts[0] = tri.pnts[1];
        tri1.pnts[1] = splitP1;
        tri1.pnts[2] = splitP2;
        return 2;
    case 4:
        CalcMiddlePoint(tri.pnts[1], tri.pnts[2], splitP1, fp1, fp2);
        CalcMiddlePoint(tri.pnts[0], tri.pnts[2], splitP2, fp0, fp2);
        tri.pnts[1] = splitP1;
        tri.pnts[0] = splitP2;
        return 1;
    case 5:
        CalcMiddlePoint(tri.pnts[0], tri.pnts[1], splitP1, fp0, fp1);
        CalcMiddlePoint(tri.pnts[1], tri.pnts[2], splitP2, fp1, fp2);
        tri.pnts[1] = splitP2;
        tri1.pnts[0] = tri.pnts[0];
        tri1.pnts[1] = splitP1;
        tri1.pnts[2] = splitP2;
        return 2;
    case 6:
        CalcMiddlePoint(tri.pnts[0], tri.pnts[1], splitP1, fp0, fp1);
        CalcMiddlePoint(tri.pnts[0], tri.pnts[2], splitP2, fp0, fp2);
        tri.pnts[0] = splitP2;
        tri1.pnts[0] = tri.pnts[1];
        tri1.pnts[1] = splitP2;
        tri1.pnts[2] = splitP1;
        return 2;
    case 7:
        return 1;
    }
    ASSERT(0);
    return 0;
}

/////////////////////////////////////////////////////////////////////////////////////
WORD CShadowBuilder::AddPoint(SShadowPoint &pnt)
{
    (*dstVertex)[nDstVertexCount / 4].x[nDstVertexCount & 3] = pnt.point.x;
    (*dstVertex)[nDstVertexCount / 4].y[nDstVertexCount & 3] = pnt.point.y;
    (*dstVertex)[nDstVertexCount / 4].z[nDstVertexCount & 3] = pnt.point.z; // + 0.0015f;
    (*dstUV)[nDstVertexCount].x = pnt.uv[0];
    (*dstUV)[nDstVertexCount].y = pnt.uv[1];
    return nDstVertexCount++;
}

/////////////////////////////////////////////////////////////////////////////////////
void CShadowBuilder::FinalAddTriangle(SShadowTriangle &tri)
{
    (*dstIndex)[nDstIndexCount++] = AddPoint(tri.pnts[0]);
    (*dstIndex)[nDstIndexCount++] = AddPoint(tri.pnts[1]);
    (*dstIndex)[nDstIndexCount++] = AddPoint(tri.pnts[2]);
}

/////////////////////////////////////////////////////////////////////////////////////
void CShadowBuilder::AddShadowTriangle3(SShadowTriangle &tri)
{
    int rv;
    static SShadowTriangle tri1;
    rv = SplitTriangle(tri, tri1, 1 - tri.pnts[0].uv[1], 1 - tri.pnts[1].uv[1],
                       1 - tri.pnts[2].uv[1]);
    if (rv > 0)
        FinalAddTriangle(tri);
    if (rv > 1)
        FinalAddTriangle(tri1);
}

/////////////////////////////////////////////////////////////////////////////////////
void CShadowBuilder::AddShadowTriangle2(SShadowTriangle &tri)
{
    int rv;
    static SShadowTriangle tri1;
    rv = SplitTriangle(tri, tri1, 1 - tri.pnts[0].uv[0], 1 - tri.pnts[1].uv[0],
                       1 - tri.pnts[2].uv[0]);
    if (rv > 0)
        AddShadowTriangle3(tri);
    if (rv > 1)
        AddShadowTriangle3(tri1);
}

/////////////////////////////////////////////////////////////////////////////////////
void CShadowBuilder::AddShadowTriangle1(SShadowTriangle &tri)
{
    int rv;
    static SShadowTriangle tri1;
    rv = SplitTriangle(tri, tri1, tri.pnts[0].uv[1], tri.pnts[1].uv[1], tri.pnts[2].uv[1]);
    if (rv > 0)
        AddShadowTriangle2(tri);
    if (rv > 1)
        AddShadowTriangle2(tri1);
}

/////////////////////////////////////////////////////////////////////////////////////
void CShadowBuilder::AddShadowTriangle0(SShadowTriangle &tri)
{
    int rv;
    static SShadowTriangle tri1;
    rv = SplitTriangle(tri, tri1, tri.pnts[0].uv[0], tri.pnts[1].uv[0], tri.pnts[2].uv[0]);
    if (rv > 0)
        AddShadowTriangle1(tri);
    if (rv > 1)
        AddShadowTriangle1(tri1);
}
/////////////////////////////////////////////////////////////////////////////////////
// clip by some plane
void CShadowBuilder::AddShadowTriangle(SShadowTriangle &tri)
{
    int rv;
    static SShadowTriangle tri1;
    float fp1 = fpC0 + tri.pnts[0].point * vecC;
    float fp2 = fpC0 + tri.pnts[1].point * vecC;
    float fp3 = fpC0 + tri.pnts[2].point * vecC;
    rv = SplitTriangle(tri, tri1, fp1, fp2, fp3);
    if (rv > 0)
        AddShadowTriangle0(tri);
    if (rv > 1)
        AddShadowTriangle0(tri1);
}
/////////////////////////////////////////////////////////////////////////////////////
void CShadowBuilder::GeneratePoint(SShadowPoint &res, const CPlot3D &pnt)
{
    res.point = pnt;
    res.uv[0] = fpU0 + pnt * vecU;
    res.uv[1] = fpV0 + pnt * vecV;
}
void CShadowBuilder::GeneratePoint(SShadowPoint &res, const SXYZW &pnt)
{
    res.point.x = pnt.x;
    res.point.y = pnt.y;
    res.point.z = pnt.z;
    res.uv[0] = fpU0 + res.point * vecU;
    res.uv[1] = fpV0 + res.point * vecV;
}
/////////////////////////////////////////////////////////////////////////////////////
CShadowBuilder::CShadowBuilder()
{
    dstVertex = 0;
    dstUV = 0;
    dstIndex = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CShadowBuilder::CheckSphere(const CPlot3D &ptCenter, float fpRadius)
{
    float fpV = fpV0 + ptCenter * vecV;
    float fpDV = fpRadius * fpVleng;
    if (fpV - fpDV > 1 || fpV + fpDV < 0)
        return false;
    float fpU = fpU0 + ptCenter * vecU;
    float fpDU = fpRadius * fpUleng;
    if (fpU - fpDU > 1 || fpU + fpDU < 0)
        return false;
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
void CShadowBuilder::Setup(CXYZW4Array &dst, CPlot2DArray &dstUVs, CWORDArray &dstIdx,
                           CXYZW4Array &src, const CPlot3D &_light, const CPlot3D &_ptCenter,
                           float fpRadius)
{
    light = _light;
    ptCenter = _ptCenter;
    dstVertex = &dst;
    dstIndex = &dstIdx;
    dstUV = &dstUVs;
    nDstVertexCount = 0;
    nDstIndexCount = 0;
    dstUV->SetSize(0);     // ARRAY BOUNDS CHECK
    dstVertex->SetSize(0); // ARRAY BOUNDS CHECK
    dstIndex->SetSize(0);  // ARRAY BOUNDS CHECK
    // initialize basic points
    SXYZW4 &src4 = src[0];
    CPlot3D p00(src4.x[3], src4.y[3], src4.z[3]), p01(src4.x[2], src4.y[2], src4.z[2]);
    CPlot3D p10(src4.x[0], src4.y[0], src4.z[0]);
    // find u, v, p10grow vertices
    light /= fabs(light);
    vecU = p01 - p00;
    vecU = vecU - (vecU * light) * light;
    vecU /= fabs2(vecU);
    vecV = p10 - p00;
    vecV = vecV - (vecV * light) * light;
    vecV /= fabs2(vecV);
    ptGrow = light / fabs(light.z);
    ptBase = p00;
    fpU0 = -ptBase * vecU;
    fpV0 = -ptBase * vecV;
    //
    fpUleng = fabs(vecU);
    fpVleng = fabs(vecV);
    // set up additional clip plane
    vecC = light;
    fpC0 = -((ptCenter - light * (fpRadius * 0.33f)) * vecC);
}
/////////////////////////////////////////////////////////////////////////////////////
void CShadowBuilder::AddTriangle(const CPlot3D &a, const CPlot3D &b, const CPlot3D &c)
{
    static SShadowTriangle tri;
    /*float nX, nY, nZ;
    float ax = c.x-b.x, ay = c.y-b.y, az = c.z-b.z;
    float bx = a.x-b.x, by = a.y-b.y, bz = a.z-b.z;
    nX = ay*bz - az*by;
    nY = az*bx - ax*bz;
    nZ = ax*by - ay*bx;
    float fpL = nX * light.x + nY * light.y + nZ * light.z;
    if ( fpL >= 0 )
            return;*/
    GeneratePoint(tri.pnts[0], a);
    GeneratePoint(tri.pnts[1], b);
    GeneratePoint(tri.pnts[2], c);
    AddShadowTriangle(tri);
}
/////////////////////////////////////////////////////////////////////////////////////
void CShadowBuilder::AddTriangle(const SXYZW &a, const SXYZW &b, const SXYZW &c)
{
    static SShadowTriangle tri;
    /*float nX, nY, nZ;
    float ax = c.x-b.x, ay = c.y-b.y, az = c.z-b.z;
    float bx = a.x-b.x, by = a.y-b.y, bz = a.z-b.z;
    nX = ay*bz - az*by;
    nY = az*bx - ax*bz;
    nZ = ax*by - ay*bx;
    float fpL = nX * light.x + nY * light.y + nZ * light.z;
    if ( fpL >= 0 )
            return;*/
    GeneratePoint(tri.pnts[0], a);
    GeneratePoint(tri.pnts[1], b);
    GeneratePoint(tri.pnts[2], c);
    AddShadowTriangle(tri);
}
/////////////////////////////////////////////////////////////////////////////////////
void CShadowBuilder::AllocForTriangles(int nTris)
{
    dstUV->SetSize(nDstVertexCount + nTris * 3);             // nDstVertexCount + 7 );
    dstVertex->SetSize(nDstVertexCount + nTris * 3 / 4 + 1); // ( nDstVertexCount + 3 ) / 4 + 1 );
    dstIndex->SetSize(nDstIndexCount + nTris * 3);
}
/////////////////////////////////////////////////////////////////////////////////////
void CShadowBuilder::Finish()
{
    int nMustDie;
    CXYZW4Array &dst = *dstVertex;
    CPlot2DArray &dstUVs = *dstUV;
    CWORDArray &dstIdx = *dstIndex;
    dst.SetSize((nDstVertexCount + 3) / 4);
    dstIdx.SetSize(nDstIndexCount);
    for (nMustDie = nDstVertexCount; nMustDie < dst.GetSize() * 4; nMustDie++)
    {
        dst[nMustDie / 4].x[nMustDie & 3] = dst[0].x[0];
        dst[nMustDie / 4].y[nMustDie & 3] = dst[0].y[0];
        dst[nMustDie / 4].z[nMustDie & 3] = dst[0].z[0];
    }
    dstVertex = 0;
    dstUV = 0;
    dstIndex = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
