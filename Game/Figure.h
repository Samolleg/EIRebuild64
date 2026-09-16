#pragma once

#include "Base3D.h"
#include "Plot.h"
#include "Quaternion.h"
#include "typedefs.h"
/////////////////////////////////////////////////////////////////////////////////////
const DWORD FIG_STATIC = MAKE_FLAG(B3D_LAST_BIT,
                                   0); // figure is static (don't require relighting each frame)
const DWORD FIG_LIGHTED = MAKE_FLAG(B3D_LAST_BIT,
                                    1); // figure already was lighted on previous frame(s)
const DWORD FIG_LOADED = MAKE_FLAG(B3D_LAST_BIT, 2); // all figure's data already loaded
const DWORD FIG_VISIBLE = MAKE_FLAG(
    B3D_LAST_BIT,
    3); // figure visible in this (prev) frame (this flag will be set during transformation)
const DWORD FIG_HIDDEN = MAKE_FLAG(B3D_LAST_BIT, 4); // general figure's invisibility
const DWORD FIG_USE_DEFAULT_COLORS = MAKE_FLAG(B3D_LAST_BIT, 5);

const DWORD FIG_LAST_BIT = MAKE_LAST_BIT(B3D_LAST_BIT, 5);
/////////////////////////////////////////////////////////////////////////////////////
class CFigure;
class CDXGeometryProcessor;
class CGraphicsProcessor;
struct SShadowTextureRenderParam;

// --------------------------------------------------------------------------------------------
// base specific class for figure information
// --------------------------------------------------------------------------------------------
class CFigureSpecific : public CGameBase
{
protected:
    CFigure *pFigure;

public:
    CFigureSpecific();
    virtual ~CFigureSpecific();
    // figure registration
    void RegisterFigure(CFigure *lpFigure);
    //
#if defined(_A4) || defined(__OBJECT_CONVERTOR_A4__)
    virtual void SetScale(const CPlot3D &pt) {}
#endif // defined(_A4) || defined(__OBJECT_CONVERTOR_A4__)
};
/////////////////////////////////////////////////////////////////////////////////////
inline void CFigureSpecific::RegisterFigure(CFigure *lpNewFigure)
{
    pFigure = lpNewFigure;
}
inline CFigureSpecific::CFigureSpecific()
{
    pFigure = 0;
}
inline CFigureSpecific::~CFigureSpecific()
{
    RegisterFigure(0);
}

// --------------------------------------------------------------------------------------------
// contain a complete set of data to visualize single figure
// --------------------------------------------------------------------------------------------
class CFigure : public CBase3D
{
    friend class CObject3DClientSpecific;
    friend class CFigureClientSpecific;

private:
    CFigureSpecific *lpFigureSpecific; // figure specific information
    LPGAMEBASE lpObject;               // pointer to parent's obejct3D
    // plots and relative data
    SXYZW4 *plots4; // array of X4Y4Z4W4 structures (SXYZW4)
    int nNumPlots;  // number of plots (number of SXYZW4 structures)
    SXYZW4 *norms4; // the same, but for normales
    int nNumNorms;  // number of normales
    // ready-to-use information
    CPlot2D *maps; // texture mapping coords
    int nMaps;     // number of maps
    SVertexComponents
        *components; // indices of geom, norm and tex vertices from data above to recompose tlvertex
    int nComponents; // number of components
    SGvNvComponents *lightcomponents; // indices of geom and norm verteces to perform lighting
    int nLightComponents;             // number of light components
    WORD *indices;                    // ready-to-use triangle vertex indices
    int nIndices;                     // number of indices
    //
    float fpUShift; // texture map 'u' shift
    float fpVShift; // texture map 'v' shift
    //
    DWORD dwDefaultColor;    // default diffuse color
    DWORD dwDefaultSpecular; // default specular color
    //
    float fRedPower; // степень покраснения данной фигуры
    //
    D3DMATRIX d3dmatrix;

protected:
    // name
    CString name;
    CString szMask;
    // morphing data
    CPlot3DArray morphs; // morphing data
    // colors
    CDWORDArray colors;    // result color components
    CDWORDArray speculars; // result specular components
    // material and texture IDs
    ETypeIDs tidMaterialTypeID; // material type ID
    ETypeIDs tidTextureTypeID;  // texture type ID
    // bounding volumes information
    CPlot3D bbCenter, bbMin, bbMax; // bounding box center, min and max coords
    float radius;                   // bounding sphere radius (center in before line)
    SBoundingVolume bv;

public:
    // private :
    void RotateForDraw(const CQuaternion &A); // make 'afters' plots from 'plots'
    void PrepareD3DRotateMatrix(const CQuaternion &q);

protected:
    void MapPlots(SXYZW4 *newplots4, int nNewNumPlots)
    {
        plots4 = newplots4;
        nNumPlots = nNewNumPlots;
    }
    void MapNorms(SXYZW4 *newnorms4, int nNewNumNorms)
    {
        norms4 = newnorms4;
        nNumNorms = nNewNumNorms;
        colors.SetSize(nNewNumNorms * 4);
        speculars.SetSize(nNewNumNorms * 4);
    }
    void MapMaps(CPlot2D *newmaps, int nNumMaps)
    {
        maps = newmaps;
        nMaps = nNumMaps;
    }
    void MapComponents(SVertexComponents *newcomponents, int nNumComponents)
    {
        components = newcomponents;
        nComponents = nNumComponents;
    }
    void MapLightComponents(SGvNvComponents *newlightcomponents, int nNumLightComponents)
    {
        lightcomponents = newlightcomponents;
        nLightComponents = nNumLightComponents;
    }
    void MapIndices(WORD *newindices, int nNumIndices)
    {
        indices = newindices;
        nIndices = nNumIndices;
    }
    //
    void JustLoadBoundingVolume(LPBYTE &lpData, float strength, float grace, float tall,
                                DWORD dwSignature);
    void LoadOldBoundingVolume(LPBYTE &lpData, float strength, float grace, float tall);

public:
    CFigure();
    CFigure(const CFigure &A);
    virtual ~CFigure();
    // operators
    virtual void operator=(const CFigure &A);
    // create/destroy
    virtual BOOL Create();
    virtual void Destroy();
    // figure specific
    void SetFigureSpecific(LPGAMEBASE lpNewFigureSpecific);
    CFigureSpecific *GetFigureSpecific() { return lpFigureSpecific; }
    // object
    void SetObject3D(LPGAMEBASE lpNewObject3D);
    LPGAMEBASE GetObject3D() { return lpObject; }
    //
    void SetRedPower(float fNewRedPower) { fRedPower = fNewRedPower; }
    float GetRedPower() const { return fRedPower; }
    //
    void SetMorphs(CPlot3DArray &morphings);
    // 'u'/'v' shift functions
    void SetUShift(const float fpVal) { fpUShift = fpVal; }
    void SetVShift(const float fpVal) { fpVShift = fpVal; }
    float GetUShift() const { return fpUShift; }
    float GetVShift() const { return fpVShift; }
    // default color functions
    void SetDefaultColor(DWORD dwColor) { dwDefaultColor = dwColor; }
    void SetDefaultSpecular(DWORD dwColor) { dwDefaultSpecular = dwColor; }
    DWORD GetDefaultColor() const { return dwDefaultColor; }
    DWORD GetDefaultSpecular() const { return dwDefaultSpecular; }
    void UseDefaultColors(BOOL bUse)
    {
        if (bUse)
            SetFlag(FIG_USE_DEFAULT_COLORS);
        else
            ResetFlag(FIG_USE_DEFAULT_COLORS);
    }
    // material/texture typeID acquiring
    ETypeIDs GetMaterialTypeID() const { return tidMaterialTypeID; }
    void SetMaterialTypeID(ETypeIDs tid) { tidMaterialTypeID = tid; }
    ETypeIDs GetTextureTypeID() const { return tidTextureTypeID; }
    void SetTextureTypeID(ETypeIDs tid) { tidTextureTypeID = tid; }
    // name functions
    virtual LPCSTR GetName() const { return name; }
    void SetName(LPCSTR pszName) { name = pszName; }
    LPCSTR GetMaskName() const { return szMask; }
    void SetMask(LPCSTR pszNewMask) { szMask = pszNewMask; }
    // bounding volume functions
    const CPlot3D &GetBVCenter() const { return bv.ptCenter; }
    const CPlot3D &GetBVMin() const { return bv.ptMin; }
    const CPlot3D &GetBVMax() const { return bv.ptMax; }
    float GetBVRadius() const { return bv.fpRadius; }
    LPBOUNDINGVOLUME GetBoundingVolume() { return &bv; }
    const SBoundingVolume &GetBV() const { return bv; }
    void RecalcBoundingBoxWithRotation(float &maxx, float &maxy, float &maxz, float &minx,
                                       float &miny, float &minz);
    //
    void SetFigureLighted() { SetFlag(FIG_LIGHTED); }
    void ResetFigureLighted() { ResetFlag(FIG_LIGHTED); }
    // drawing
    virtual void PrepareForDraw(const CQuaternion &q, const CPlot3D &sp);
    DWORD ProjectShadow(CDXGeometryProcessor *pGPU, const SHMatrix &matrix, CPlot2D *buffer,
                        CTRect<float> *pRect);
    DWORD RenderShadow(CGraphicsProcessor *pGrPU, CPoint *points, SShadowTextureRenderParam &rp);
    // mesh access functions
    SXYZW4 GetPlot(int nInd) const { return plots4[nInd]; }
    SXYZW4 *GetPlots() { return plots4; }
    int GetNumPlots() const { return nNumPlots; }
    WORD *GetIndices() { return indices; }
    int GetNumIndices() const { return nIndices; }
    // service : save/load
    virtual void Save(CFile &file) const;
    virtual void LoadD3DComplex(LPBYTE &lpData, float strength, float grace, float tall);
    // identification
    virtual LPCSTR GetGameClassName() const { return "CFigure"; }
    virtual EClassIDs WhoAmI() const { return HPC_FIGURE; }
};
/////////////////////////////////////////////////////////////////////////////////////
inline CFigure::CFigure()
    : lpObject(0)
{
    Create();
#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
}
inline CFigure::CFigure(const CFigure &A)
{
    (*this) = A;
}

// --------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------
class CFigureLoaded : public CFigure
{
    friend class CFakeUnit;

protected:
    // plots and relative data
    CXYZW4Array plots4; // array of X4Y4Z4W4 structures (SXYZW4)
    CXYZW4Array norms4; // the same, but for normales
    // ready-to-use information
    CVertexComponentsArray
        components; // indices of geom, norm and tex vertices from data above to recompose tlvertex
    CGvNvComponentsArray lightcomponents; // indices of geom and norm verteces to perform lighting
    CWORDArray indices;                   // ready-to-use triangle vertex indices
    CPlot2DArray maps;                    // texture mapping coords
    //
    void SetupDataPointers();

private:
    virtual void LoadNewFigure(LPBYTE &lpData, float strength, float grace, float tall);
    virtual void LoadOldFigure(LPBYTE &lpData, float strength, float grace, float tall);

public:
    // create/destroy
    virtual BOOL Create();
    virtual void Destroy();
    //
    virtual void LoadD3DComplex(LPBYTE &lpData, float strength, float grace, float tall);
    virtual void Load(CFile &file);
    virtual void MirrorLeftRight();
    virtual void MirrorUpDown();

    void RecalcBV();

private:
    void FlipFaces();
};

// --------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------
class CFigureMapLoaded : public CFigure
{
private:
    // plots and relative data
    CXYZW4Array plots4; // array of X4Y4Z4W4 structures (SXYZW4)
    CXYZW4Array norms4; // the same, but for normales
private:
    virtual void LoadNewFigure(LPBYTE &lpData, float strength, float grace, float tall);
    virtual void LoadOldFigure(LPBYTE &lpData, float strength, float grace, float tall);

public:
    // create/destroy
    virtual BOOL Create();
    virtual void Destroy();
    //
    virtual void LoadD3DComplex(LPBYTE &lpData, float strength, float grace, float tall);
};
// --------------------------------------------------------------------------------------------
typedef CFigure *LPFIGURE;
typedef CArray<CFigure, CFigure &> CFigureArray;
typedef CArray<CFigure *, CFigure *> CFigurePtrArray;
