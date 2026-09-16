#pragma once

#include "Figure.h"
#include "Material.h"
#include "Texture.h"

/////////////////////////////////////////////////////////////////////////////////////
const float VIEWING_ANGLE_TAN = 0.4815746188f;
/////////////////////////////////////////////////////////////////////////////////////
// class CI3DFigure : public CFigureLoaded
// interface figure
class CI3DFigure : public CFigureLoaded
{
private:
    BOOL bVisible;

    D3DVECTOR ambientLight;
    D3DVECTOR directionalLight;

    SHMatrix scale;

    LPMATERIAL pMaterial;
    LPTEXTURE pTexture;

public:
    CI3DFigure() {}
    virtual ~CI3DFigure() {}
    //
    virtual BOOL Create();
    virtual void Destroy();

    void Show(BOOL bShow) { bVisible = bShow; }
    BOOL IsVisible() { return bVisible; }

    BOOL Draw(CRenderer *pRend);

    void SetMaterial(LPMATERIAL pMat) { pMaterial = pMat; }
    LPMATERIAL GetMaterial() { return pMaterial; }
    void SetTexture(LPTEXTURE pTex) { pTexture = pTex; }
    LPTEXTURE GetTexture() { return pTexture; }

    void SetScale(float fScale);
    void SetXScale(float fScale);
    void SetYScale(float fScale);
    void SetZScale(float fScale);
    float GetXScale();
    float GetYScale();
    float GetZScale();

    void SetAmbient(float r, float g, float b) { ambientLight = D3DVECTOR(r, g, b); }
    const D3DVECTOR &GetAmbient() const { return ambientLight; }
    void SetDirectional(float r, float g, float b) { directionalLight = D3DVECTOR(r, g, b); }

    void SetRelativeToScreen(float xScr, float yScr, float zDist);
    void GetRelativeToScreen(float &xScr, float &yScr, float &zDist);

    void MakeBar(float fDepth, float fPixInMeter, float fL, float fT, float fR, float fB,
                 int nMapL = 0, int nMapT = 0, int nMapR = 256, int nMapB = 256);
    void MakeTilingBar(float fDepth, float fPixInMeter, float fL, float fT, float fR, float fB,
                       int nMapL = 0, int nMapT = 0, int nMapR = 256, int nMapB = 256,
                       int nTilesX = 1, int nTilesY = 1);
    void MakeBarFrame(float fDepth, float fPixInMeter, float fL, float fT, float fR, float fB,
                      float fFrameW, int nMapL = 0, int nMapT = 0, int nMapR = 256,
                      int nMapB = 256);
    void MakeBarFrame2(float fDepth, float fPixInMeter, float fL, float fT, float fR, float fB,
                       float fFrameW, int nMapL = 0, int nMapT = 0, int nMapR = 256,
                       int nMapB = 256);
    void MakeCircle(float fDepth, float fPixInMeter, float fX, float fY, float fR, int nEdges,
                    int nMapX = 128, int nMapY = 128, int nMapR = 128);
    void MakeCircleFrame(float fDepth, float fPixInMeter, float fX, float fY, float fR, int nEdges,
                         float fFrameW, int nMapL = 0, int nMapT = 0, int nMapR = 256,
                         int nMapB = 256);
    void MakeTriangle(float fDepth, float fPixInMeter, float fX1, float fY1, float fX2, float fY2,
                      float fX3, float fY3, float fMapX1, float fMapY1, float fMapX2, float fMapY2,
                      float fMapX3, float fMapY3);

    void SetFlatNorms();
    void MapAfterModify();

    void SetPlotsSize(int num);
    void SetPlot(int i, int j, float x, float y)
    {
        plots4[i].x[j] = x;
        plots4[i].y[j] = y;
    }
    void GetPlot(int i, int j, float &x, float &y)
    {
        x = plots4[i].x[j];
        y = plots4[i].y[j];
    }
    void SetMapsSize(int num);
    void SetMap(int i, float u, float v)
    {
        maps[i].x = u;
        maps[i].y = v;
    }
    void SetIndicesSize(int num);
    void SetIndex(int i, int idx) { indices[i] = idx; }
    void SetComponentsSize(int num);
    void SetComponent(int i, int g, int t)
    {
        components[i].gv = g;
        components[i].tv = t;
    }

    // identification
    virtual LPCSTR GetGameClassName() const { return "CI3DFigure"; }
    virtual EClassIDs WhoAmI() const { return HPC_INTERFACEFIGURE; }
};
/////////////////////////////////////////////////////////////////////////////////////
typedef CI3DFigure *LPI3DFIGURE;
