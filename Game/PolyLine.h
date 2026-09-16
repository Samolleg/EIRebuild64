#pragma once

#include "Plot.h"
/////////////////////////////////////////////////////////////////////////////////////
class CDXGeometryProcessor;
class CRenderer;
/////////////////////////////////////////////////////////////////////////////////////
struct SLineVertex
{
    enum
    {
        fvfFlags = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_SPECULAR
    };
    float x, y, z, rhw;
    DWORD color;
    DWORD specular;
};
/////////////////////////////////////////////////////////////////////////////////////
class CPolyLine
{
private:
    int nID;           // id of this polyline
    CXYZW4Array plots; // plots of this polyline
    int nNumPlots;     // real number of plots
    DWORD color;       // color of this polyline
    int nStyle;

public:
    CPolyLine();
    ~CPolyLine();
    // id functions
    void SetID(int nNewID) { nID = nNewID; }
    int GetID() const { return nID; }
    //
    void SetColor(DWORD newColor) { color = newColor; }
    void SetStyle(int nNewStyle) { nStyle = nNewStyle; }
    // work with plots
    void MakeCircle(const CPlot3D &ptCenter, float fRadius, int nSegmens = 20);
    int AddPlot(const CPlot3D &plot);
    void SetPlotAt(int index, CPlot3D &plot);
    int RemoveLastPlot()
    {
        nNumPlots--;
        return nNumPlots;
    }
    int RemoveAll()
    {
        nNumPlots = 0;
        return nNumPlots;
    }
    CPlot3D GetPlot(int nIndex);
    int GetNPlots() { return nNumPlots; };
    // drawing
    BOOL Draw(CDXGeometryProcessor *pGPU, CRenderer *pRend);
};
/////////////////////////////////////////////////////////////////////////////////////
inline CPolyLine::CPolyLine()
{
    nID = -1;
    nStyle = PS_SOLID;
    color = RGBA_MAKE(255, 0, 0, 255);
    nNumPlots = 0;
}
inline CPolyLine::~CPolyLine()
{
    nID = -1;
    plots.RemoveAll();
}
/////////////////////////////////////////////////////////////////////////////////////
