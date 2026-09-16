#pragma once

#include "SmartPtr.h"

#include "GeometryInterfaces.h"
#include "FigureManager.h"

#include <map>
/////////////////////////////////////////////////////////////////////////////////////
class CLight;
class CMaterial;
class CGeometryManager;
class CTLPipeline;
/////////////////////////////////////////////////////////////////////////////////////
struct SGeometryRangeSW
{
public:
    DWORD dwPlotsStart;           // index of the first plot quad in this range
    DWORD dwNumPlots;             // number of plot quads in this range
    DWORD dwNormsStart;           // index of the first normale quad in this range
    DWORD dwNumNorms;             // number of norm quads in this range
    DWORD dwMapsStart;            // index of the first maps in this range
    DWORD dwNumMaps;              // number of maps in this range
    DWORD dwComponentsStart;      // index of the first components in this range
    DWORD dwNumComponents;        // number of components in this range
    DWORD dwLightComponentsStart; // index of the first light components in this range
    DWORD dwNumLightComponents;   // number of light components in this range
    DWORD dwIndicesStart;         // index of the first indices in this range
    DWORD dwNumIndices;           // number of indices in this range
public:
    SGeometryRangeSW() { memset(this, 0, sizeof(SGeometryRangeSW)); }
};
/////////////////////////////////////////////////////////////////////////////////////
class CGeometryContainer : public IGeometry
{
private:
    // identification
    float fStrength;
    float fGrace;
    float fTall;
    // geometry data
    CXYZW4Array plots4; // array of X4Y4Z4W4 structures (SXYZW4)
    CXYZW4Array norms4; // the same, but for normales
    CPlot2DArray maps;  // texture mapping coords
    CVertexComponentsArray
        components; // indices of geom, norm and tex vertices from data above to recompose tlvertex
    CGvNvComponentsArray lightcomponents; // indices of geom and norm verteces to perform lighting
    CWORDArray indices;                   // ready-to-use triangle vertex indices
    // ranges
    std::map<const std::string, SGeometryRangeSW> ranges;
    // geometry manager
    CGeometryManager *pGeometryManager; // pointer to geometry manager, this geomety created by
    CSmartPtr<CModelResourceEntry> pModel;
    CTLPipeline *pTL;

public:
    CGeometryContainer(CGeometryManager *pManager, const std::string &szName, float fStrength,
                       float fGrace, float fTall, CModelResourceEntry *pModel);
    virtual ~CGeometryContainer() {}
    // for use with geometry manager
    bool AddRange(const std::string &szRangeName, SXYZW4 *plots4, DWORD dwNumPlots, SXYZW4 *norms4,
                  DWORD dwNumNorms, CPlot2D *maps, DWORD dwNumMaps, SVertexComponents *components,
                  DWORD dwNumComponents, SGvNvComponents *lightcomponents,
                  DWORD dwNumLightComponents, WORD *indices, DWORD dwNumIndices);
    // get
    float GetStrength() const { return fStrength; }
    float GetGrace() const { return fGrace; }
    float GetTall() const { return fTall; }
    SRangeExternals &GetRangeExternals(const std::string &szRangeName)
    {
        return extents[szRangeName];
    }
    // set
    void SetNewComplection(float fStrength, float fGrace, float fTall);
    bool ChangeRangeGeometry(const std::string &szRangeName, SXYZW4 *plots4, DWORD dwNumPlots);
    //
    int GetMemoryCount() const;
    //
    virtual void RecalcScreenExtents();
    // check for range existance
    virtual bool IsHaveRange(const std::string &szRangeName) const;
    // get names of all added ranges
    virtual void GetRangeNames(std::vector<std::string> &szNames) const;
    // add new range by name
    virtual bool AddRange(const std::string &szRangeName);
    // remove existed range by name
    virtual bool RemoveRange(const std::string &szRangeName);
    // range geometry data access
    virtual bool GetRangeGeometryData(const std::string &szRangeName, const SXYZW4 *&plots,
                                      const SVertexComponents *&comps, const WORD *&inds,
                                      DWORD &dwNumPlots, DWORD &dwNumIndices) const;
    // process lighting for selected range
    virtual bool ProcessLighting(const std::string &szRangeName,
                                 CSimplyArray<CLight *, CLight *> &lights, CMaterial *pMaterial,
                                 BYTE cFog, CDWORDArray &colors, CDWORDArray &speculars);
    virtual bool ProcessLighting(const std::string &szRangeName, DWORD dwColor, DWORD dwSpecular,
                                 CDWORDArray &colors, CDWORDArray &speculars);
    // render range
    virtual int Render(const std::string &szRangeName, DWORD dwRenderFlags, CDWORDArray &colors,
                       CDWORDArray &speculars, CPlot3DArray &morphs, float fUShift = 0,
                       float fVShift = 0);
    // render range with multitexture
    // shadow rendering functions
    virtual DWORD ProjectShadow(const std::string &szRangeName, const CPlot3DArray &morphs,
                                const SHMatrix &matrix, CPlot2D *buffer, CTRect<float> *pRect);
    virtual DWORD RenderShadow(const std::string &szRangeName, CPoint *shadowpoints,
                               SShadowTextureRenderParam &rp);
};
// --------------------------------------------------------------------------------------------
typedef CSmartPtr<CGeometryContainer> CGeometryContainerPtr;
