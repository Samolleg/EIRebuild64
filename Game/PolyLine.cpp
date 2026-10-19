#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "PolyLine.h"

#include "DXGeometryProcessor.h"
#include "Renderer.h"
#include "dx11/RendererDX11.h"

#include "WorldClient.h"

/////////////////////////////////////////////////////////////////////////////////////
// int CPolyLine::AddPlot( CPlot3D& plot )
// add plot to polyline
int CPolyLine::AddPlot(const CPlot3D &plot)
{
    // grow array if need
    if (nNumPlots == plots.GetSize() * 4)
        plots.SetSize(plots.GetSize() + 1);
    // store plot
    int hindex = nNumPlots >> 2;
    int lindex = nNumPlots & 0x3;

    plots[hindex].x[lindex] = plot.x;
    plots[hindex].y[lindex] = plot.y;
    plots[hindex].z[lindex] = plot.z;
    plots[hindex].w[lindex] = 1;

    nNumPlots++;
    // extend array to be multiple to 4
    if (nNumPlots < plots.GetSize() * 4)
    {
        for (int i = nNumPlots; i < plots.GetSize() * 4; i++)
        {
            int hindex = i >> 2;
            int lindex = i & 0x3;

            plots[hindex].x[lindex] = plot.x;
            plots[hindex].y[lindex] = plot.y;
            plots[hindex].z[lindex] = plot.z;
            plots[hindex].w[lindex] = 1;
        }
    }

    return nNumPlots;
}
/////////////////////////////////////////////////////////////////////////////////////
// int CPolyLine::SetPlotAt( int index, CPlot3D& plot )
// set plot at desired index (such index MUST exist)
void CPolyLine::SetPlotAt(int index, CPlot3D &plot)
{
    int hindex = index >> 2;
    int lindex = index & 0x3;

    plots[hindex].x[lindex] = plot.x;
    plots[hindex].y[lindex] = plot.y;
    plots[hindex].z[lindex] = plot.z;
    plots[hindex].w[lindex] = 1;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CPolyLine::Draw(CDXGeometryProcessor *pGPU, CRenderer *pRend)
{
    if (nNumPlots < 2)
        return TRUE;
    if (!pGPU || !pRend || !pGPU->IsHaveMatrix(MAT_STACK_PROJSPACE))
        return FALSE;

    // Unpack only the authored points, never the padding lanes. Projection and
    // clipping stay in the renderer's shared fixed-function translator.
    std::vector<owned::FixedFunctionVertex> vertices(nNumPlots);
    for (int i = 0; i < nNumPlots; ++i)
    {
        const auto& group = plots[i >> 2];
        const int lane = i & 3;
        vertices[i] = {group.x[lane], group.y[lane], group.z[lane], group.w[lane],
            color | 0xff000000u, 0xff000000u, 0, 0};
    }
    return pRend->GetD3D()->SubmitC74FixedFunction(D3DPT_LINESTRIP, vertices.data(),
        DWORD(vertices.size()), nullptr, 0, pGPU->GetMatrix(MAT_STACK_PROJSPACE), true) ? TRUE : FALSE;
}
//
CPlot3D CPolyLine::GetPlot(int nIndex)
{
    CPlot3D plot;
    int hindex = nIndex >> 2;
    int lindex = nIndex & 0x3;

    plot.x = plots[hindex].x[lindex];
    plot.y = plots[hindex].y[lindex];
    plot.z = plots[hindex].z[lindex];

    return plot;
}
/////////////////////////////////////////////////////////////////////////////////////
void CPolyLine::MakeCircle(const CPlot3D &ptCenter, float fRadius, int nSegmens)
{
    if (nSegmens < 3)
        return;
    CPlot3D rVector(fRadius, 0, 0);
    CQuaternion qRot(CPlot3D(0, 0, 1), ToRadian((float)360 / nSegmens));
    const int first = nNumPlots;
    for (int i = 0; i < nSegmens; i++)
    {
        AddPlot(ptCenter + rVector);
        rVector = qRot.Rotate(rVector);
    }
    AddPlot(GetPlot(first));
}
/////////////////////////////////////////////////////////////////////////////////////
