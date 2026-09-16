#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "Polygon.h"
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
//                         CFace functions and methods
// ******************************************************************************** //
//////////////////////////////////////////////////////////////////////////////////////
// calc plane equation - normale 'n' and coeff 'd' : n*X - d = 0
void CFace::CalcPlane(const CXYZW4Array &plots)
{
    int index0l = geoms[0] & 0x3, index0h = geoms[0] >> 2;
    int index1l = geoms[1] & 0x3, index1h = geoms[1] >> 2;
    int index2l = geoms[2] & 0x3, index2h = geoms[2] >> 2;
    CPlot3D plot0 = CPlot3D(plots[index0h].x[index0l], plots[index0h].y[index0l],
                            plots[index0h].z[index0l]);
    CPlot3D plot1 = CPlot3D(plots[index1h].x[index1l], plots[index1h].y[index1l],
                            plots[index1h].z[index1l]);
    CPlot3D plot2 = CPlot3D(plots[index2h].x[index2l], plots[index2h].y[index2l],
                            plots[index2h].z[index2l]);
    CPlot3D pt1 = plot2 - plot1;
    float fLen = fabs2(pt1);
    if (fLen > 1e-10)
        pt1 *= 1.0f / sqrt(fLen);
    CPlot3D pt2 = plot0 - plot1;
    fLen = fabs2(pt2);
    if (fLen > 1e-10)
        pt2 *= 1.0f / sqrt(fLen);
    // calc normale
    n = pt1 ^ pt2;
    // normalize
    fLen = fabs(n);
    if (fLen == 0)
        n = VNULL;
    else
        n *= 1.0f / fLen; //  n = n/fabs(n);
    d = n * plot1;        //  must be '-d', but for futher suitability we'll leave '+d'
}
/////////////////////////////////////////////////////////////////////////////////////
void CFace::CalcPlane(const CPlot3DArray &plots)
{
    CPlot3D pt1 = plots[geoms[2]] - plots[geoms[1]];
    float fLen = fabs2(pt1);
    if (fLen > 1e-10)
        pt1 *= 1.0f / sqrt(fLen);
    CPlot3D pt2 = plots[geoms[0]] - plots[geoms[1]];
    fLen = fabs2(pt2);
    if (fLen > 1e-10)
        pt2 *= 1.0f / sqrt(fLen);
    n = pt1 ^ pt2;
    // normalize
    fLen = fabs(n);
    if (fLen == 0)
        n = VNULL;
    else
        n *= 1.0f / fLen;    //  n = n/fabs(n);
    d = n * plots[geoms[1]]; //  must be '-d', but for futher suitability we'll leave '+d'
}
/////////////////////////////////////////////////////////////////////////////////////
void CFace::Save(CFile &file) const
{
    file.Write(geoms, 3 * sizeof(DWORD));
    file.Write(texes, 3 * sizeof(DWORD));
    file.Write(norms, 3 * sizeof(DWORD));
}
/////////////////////////////////////////////////////////////////////////////////////
void CFace::Load(CFile &file)
{
    file.Read(geoms, 3 * sizeof(DWORD));
    file.Read(texes, 3 * sizeof(DWORD));
    file.Read(norms, 3 * sizeof(DWORD));
}
/////////////////////////////////////////////////////////////////////////////////////
