#pragma once

#include "Plot.h"

#include "GraphicsProcessor.h"

#include <string>
#include <vector>
#include <map>
#include <memory>
/////////////////////////////////////////////////////////////////////////////////////
class CMaterial;
class CLight;
class CTexture;
/////////////////////////////////////////////////////////////////////////////////////
const RECT RCNULL = { 0, 0, 0, 0 };
/////////////////////////////////////////////////////////////////////////////////////
// ** flexible vertex format description and elements sequence
// **
// ** position                   x, y, z          3 x float
// ** reciprocal homogeneous w   rhw              float
// ** blending weights           beta(1-5)        (1-5) x float
// ** vertex normal              nx, ny, nz       3 x float
// ** diffuse RGBA color         diffuse          DWORD
// ** specular RGBA color        specular         DWORD
// ** texture coords set 1       tu, tv, ...      (1-5) x float (usually 2)
// ** ............               .......          .....
// ** texture coords set 8       tu, tv, ...      (1-5) x float (usually 2)
/////////////////////////////////////////////////////////////////////////////////////
struct SRangeExternals
{
public:
    ETypeIDs tidTexture;
    ETypeIDs tidMaterial;
    SBoundingVolume bv;
    RECT rcScreen;

public:
    ~SRangeExternals() { memset(this, 0, sizeof(*this)); }
};
/////////////////////////////////////////////////////////////////////////////////////
namespace owned { struct FigureCpuAsset; struct NativeFigureStreams; }
class IGeometry : public CRefCount
{
protected:
    // identification
    const std::string szName; // object, this class geometry contain, name
    // extents
    std::map<const std::string, SRangeExternals> extents;
    RECT rcExtents; // screen extents
public:
    IGeometry(const std::string &szNewName)
        : szName(szNewName)
    {
    }
    virtual ~IGeometry() {}
    virtual std::shared_ptr<const owned::FigureCpuAsset> GetOwnedFigure(const std::string& name) const = 0;
    //
    std::string GetName() const { return szName; }
    // work with extents
    const SRangeExternals &GetRangeExternals(const std::string &szRangeName) const
    {
        return extents.find(szRangeName)->second;
    };
    const RECT &GetScreenExtents(const std::string &szRangeName) const
    {
        return extents.find(szRangeName)->second.rcScreen;
    }
    const RECT &GetScreenExtents() const { return rcExtents; }
    virtual void RecalcScreenExtents() = 0;
    // check for range existance
    virtual bool IsHaveRange(const std::string &szRangeName) const = 0;
    // get names of all added ranges
    virtual void GetRangeNames(std::vector<std::string> &szNames) const = 0;
    // add new range by name
    virtual bool AddRange(const std::string &szRangeName) = 0;
    // remove range from the geometry
    virtual bool RemoveRange(const std::string &szRangeName) = 0;
    // range geometry data access
    virtual bool GetRangeGeometryData(const std::string &szRangeName, const SXYZW4 *&plots,
                                      const SVertexComponents *&comps, const WORD *&inds,
                                      DWORD &dwNumPlots, DWORD &dwNumIndices) const = 0;
    // Borrow the actual current complection streams for synchronous native draw.
    virtual bool GetNativeFigureStreams(const std::string& name,
        owned::NativeFigureStreams& output) const = 0;
    // process lighting for the range
    virtual bool ProcessLighting(const std::string &szRangeName,
                                 CSimplyArray<CLight *, CLight *> &lights, CMaterial *pMaterial,
                                 BYTE cFog, CDWORDArray &colors, CDWORDArray &speculars) = 0;
    virtual bool ProcessLighting(const std::string &szRangeName, DWORD dwColor, DWORD dwSpecular,
                                 CDWORDArray &colors, CDWORDArray &speculars) = 0;
    // render range w/ morphing
    virtual int Render(const std::string &szRangeName, DWORD dwRenderFlags, CDWORDArray &colors,
                       CDWORDArray &speculars, CPlot3DArray &morphs, float fUShift = 0,
                       float fVShift = 0) = 0;
    // render range with multitexture
    // shadow rendering functions
    virtual DWORD ProjectShadow(const std::string &szRangeName, const CPlot3DArray &morphs,
                                const SHMatrix &matrix, CPlot2D *buffer, CTRect<float> *pRect) = 0;
    virtual DWORD RenderShadow(const std::string &szRangeName, CPoint *shadowpoints,
                               SShadowTextureRenderParam &rp) = 0;
};
typedef CSmartPtr<IGeometry> IGeometryPtr;
/////////////////////////////////////////////////////////////////////////////////////
class IGeometryManager : public CRefCount
{
public:
    virtual ~IGeometryManager() {}
    //
    virtual int GetNumElements() const = 0;
    virtual int GetUsedMemoryCount() const = 0;
    // get already existed geometry or create and load new one
    virtual IGeometry *GetGeometry(const std::string &szName, float fStrength, float fGrace,
                                   float fTall, bool bNew = false) = 0;
    // load new geometry with particular range names
    virtual IGeometry *GetGeometry(const std::string &szName, std::vector<std::string> &szNames,
                                   float fStrength, float fGrace, float fTall,
                                   bool bNew = false) = 0;
    // release all geometries by calling 'Release' function for each.
    virtual void ReleaseAllGeometries() = 0;
    // change complection for entire geometry (for all ranges)
    virtual IGeometry *ChangeComplection(IGeometry *pGC, float fStrength, float fGrace,
                                         float fTall) = 0;
};
// --------------------------------------------------------------------------------------------
typedef CSmartPtr<IGeometryManager> IGeometryManagerPtr;
