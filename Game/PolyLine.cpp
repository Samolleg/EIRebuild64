#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "PolyLine.h"

#include "DXGeometryProcessor.h"
#include "Renderer.h"

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
static CSimplyArray<SLineVertex, SLineVertex &> verts;
void DrawPolyLine(CRenderer *pRend, const CXYZWArray &plots, DWORD dwColor)
{
    verts.SetSize(plots.GetSize()); // ARRAY BOUNDS CHECK
    //
    // pRend->SetRenderState(D3DRENDERSTATE_SHADEMODE, D3DSHADE_FLAT);
    // pRend->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_DISABLE);
    // pRend->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

    for (int i = 0; i < plots.GetSize(); ++i)
    {
        Copy16Bytes(&(verts[i].x), &(plots[i].x));
        verts[i].color = dwColor | 0xff000000;
        verts[i].specular = 0xff000000;
    }

    // pRend->DrawPrimitive(D3DPT_LINESTRIP, SLineVertex::fvfFlags, verts.GetData(),
    // verts.GetSize(),
    //                    0);

    // pRend->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    // pRend->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    // pRend->SetRenderState(D3DRENDERSTATE_SHADEMODE, D3DSHADE_GOURAUD);
}
/////////////////////////////////////////////////////////////////////////////////////
static CByteArray clips;
static CXYZWArray dst;
static CXYZWArray dst1;
/////////////////////////////////////////////////////////////////////////////////////
BOOL CPolyLine::Draw(CDXGeometryProcessor *pGPU, CRenderer *pRend)
{
    dst.SetSize(plots.GetSize() * 4); // ARRAY BOUNDS CHECK
    clips.SetSize(plots.GetSize() * 4);

    // pDX11->GetConstantBuffers()
    // pGPU->GetMatrix(MAT_STACK_PROJSPACE)
    DrawPolyLine(pRend, dst, color);

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
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
    CPlot3D rVector(fRadius, 0, 0);
    CQuaternion qRot(CPlot3D(0, 0, 1), ToRadian((float)360 / nSegmens));

    for (int i = 0; i <= 360 / nSegmens; i++)
    {
        rVector = qRot.Rotate(rVector);
        AddPlot(ptCenter + rVector);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
