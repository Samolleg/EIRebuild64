#pragma once
/////////////////////////////////////////////////////////////////////////////////////
#include "Figure.h"
#include "Material.h"
#include "Renderer.h"
/////////////////////////////////////////////////////////////////////////////////////
class CSkyFigure : public CFigureLoaded
{
    CFloatArray normZSqrts;

public:
    CSkyFigure();
    //
    void RecalcVertexLighting(const CPlot3D &ptDir, D3DCOLORVALUE ambient, D3DCOLORVALUE sunlight,
                              D3DCOLORVALUE fogColor);
    // class identification functions
    virtual LPCSTR GetGameClassName() const { return "CSkyObject"; };
    virtual EClassIDs WhoAmI() const { return HPC_SKY; };
};
/////////////////////////////////////////////////////////////////////////////////////
class CSkyObject
{
private:
    CSmartPtr<CSkyFigure> pSky;
    CSmartPtr<CMaterial> pMaterial;
    CTextureDescriptor texture;
    BOOL bRotateSky;

public:
    CSkyObject();

    void SetSkyRotate(BOOL bRotate) { bRotateSky = bRotate; }
    BOOL IsRotateSky() const { return bRotateSky; }

    void Draw(CRenderer *pRend, const CPlot3D &ptSkyPos);
    void RecalcVertexLighting(const CPlot3D &ptDir, D3DCOLORVALUE ambient, D3DCOLORVALUE sunlight,
                              D3DCOLORVALUE fogColor);
    void SetMaterial(CMaterial *_pMaterial) { pMaterial = _pMaterial; }
    void SetSkyFigure(CSkyFigure *_pSky) { pSky = _pSky; }
    void SetTexture(LPCSTR pszName) { texture.SetDesc(TCTID_STATIC_TEXTURE, pszName); }
};
/////////////////////////////////////////////////////////////////////////////////////
