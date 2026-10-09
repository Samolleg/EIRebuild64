#pragma once

#include "Object.h"
#include "Material.h"
#include "Texture.h"

/////////////////////////////////////////////////////////////////////////////////////
const DWORD OCS_RESHADOW = MAKE_FLAG(CGB_LAST_BIT, 0);

const DWORD OCS_LAST_BIT = MAKE_LAST_BIT(CGB_LAST_BIT, 0);
/////////////////////////////////////////////////////////////////////////////////////
// struct SMaterialFigureSet
// contain figure <=> material respection information (one material with several figures)
struct SMaterialFigureSet
{
public:
    CMaterial *lpMaterial;
    CSimplyArray<CFigure *, CFigure *&> figures;

public:
    SMaterialFigureSet();
    ~SMaterialFigureSet();

    BOOL IsEmpty() const { return (figures.GetSize() == 0); }

    BOOL AddFigure(CFigure *lpFigure);
    BOOL RemoveFigure(CFigure *lpFigure);
    BOOL RemoveAll();
};
/////////////////////////////////////////////////////////////////////////////////////
// struct STextureMaterialSet
// contain texture <=> SMaterialFigureSet respection information (one texture with several
// SMaterialFigureSet)
struct STextureMaterialSet
{
public:
    CTexture *lpTexture;
    CSimplyArray<SMaterialFigureSet *, SMaterialFigureSet *&> matgroups;

public:
    STextureMaterialSet();
    ~STextureMaterialSet();

    BOOL IsEmpty() const;

    BOOL AddMaterial(CMaterial *lpMaterial);
    BOOL AddFigure(CFigure *lpFigure, int nMaterialTypeID);
    BOOL RemoveFigure(CFigure *lpFigure);
    void RemoveAll();
};
/////////////////////////////////////////////////////////////////////////////////////
class CObject3DClientSpecific : public CObject3DSpecific
{
private:
    STextureMaterialSet
        *sets[2]; // object's figures, sorted by texture/material (primary and secondary sets)
    //
    CSimplyArray<CTexture *, CTexture *&> textures;    // textures of this objects
    CSimplyArray<CMaterial *, CMaterial *&> materials; // materials of this objects
    int pos;                                           // for textures/materials iteration only
    // shadow rendering information
    static CSimplyArray<CPlot2D, CPlot2D &> shadowbuffer; // projected shadow temporary buffer
    static CSimplyArray<CPoint, CPoint &>
        shadowresult; // projected and resized shadow vertices temporary buffer

    CXYZW4Array shadowvertices;         // geometry vertices for shadow rendering
    CPlot2DArray shadowmaps;            // shadow maps
    CWORDArray shadowindices;           // indices for shadow rendering
    ITexturePtr pShadowTexture;         // shadow rendering texture
    DWORD modShadowLastBuild = 0;
    CTRect<float> shadowrect;           // shadow rect
    mutable SBoundingVolume bvShadowBV; // shadow bounding volume (from last build)
    //
    CRect rcRect; // screen bounding rect
    ITexturePtr pWoundsTexture;
    CString szPrimaryTextureName;   // primary texture name (for further loading)
    CString szSecondaryTextureName; // secondary texture name (for further loading)
    //
    float fpTime;
    DWORD dwLastLightTime; // time, then this object was relighted (in milliseconds)
    //
    float fScaleX, fScaleY, fScaleZ;

private:
    void CreateShadowShiftScaleMatrix(SHMatrix &matrix);

public:
    CObject3DClientSpecific();
    virtual ~CObject3DClientSpecific();
    // create/destroy
    virtual BOOL Create();
    virtual void Destroy();
    //
    LPTEXTURE GetShadowTexture() { return pShadowTexture; }
    //
    void SetLastLightTime(DWORD dwNewTime) { dwLastLightTime = dwNewTime; }
    DWORD GetLastLightTime() const { return dwLastLightTime; }
    //
    void SetScale(float fSX, float fSY, float fSZ)
    {
        fScaleX = fSX;
        fScaleY = fSY;
        fScaleZ = fSZ;
    }
    virtual void GetScaleCoeffs(float &fSX, float &fSY, float &fSZ) const
    {
        fSX = fScaleX;
        fSY = fScaleY;
        fSZ = fScaleZ;
    }
    bool GetScaleMatrix(SHMatrix &matrix) const;
    // figures-in-object management
    virtual BOOL AddFigure(CFigure *lpFigure);
    virtual BOOL RemoveFigure(CFigure *lpFigure);
    virtual CFigure *RemoveFigure(LPCSTR pszFigureName);
    virtual CFigure *FindFigure(int nFigureTypeID);
    virtual CFigure *FindFigure(LPCSTR pszFigureName);
    // test for selection
    virtual BOOL CanSelectRoughly(const CPoint &point) const;
    virtual BOOL IsCanSelect(const CPoint &point);
    virtual BOOL IsCanSelect(const CRect &rect);
    //
    void RecalcScreenRect();
    // texture/material-in-object
    CTexture *AddNewTexture(ETypeIDs tidTextureTypeID);
    CTexture *FindTexture(ETypeIDs tidTextureTypeID);
    CTexture *ChangeTexture(CTexture *lpNewTexture);
    BOOL RebindSecondaryTexture(LPCSTR name);
    BOOL RebindPrimaryTexture(LPCSTR name);
    BOOL EnsurePrivatePrimaryAtlas(DWORD unitID);
    CMaterial *AddNewMaterial(ETypeIDs tidTextureTypeID, ETypeIDs tidMaterialTypeID);
    CMaterial *FindMaterial(ETypeIDs tidTextureTypeID, ETypeIDs tidMaterialTypeID);
    //
    const CString &GetPrimaryTextureName() const { return szPrimaryTextureName; }
    const CString &GetSecondaryTextureName() const { return szSecondaryTextureName; }
    //
    BOOL LoadTextures(LPCSTR pszPrimaryTextureName, LPCSTR pszSecondaryTextureName);
    void SetMaterials();
    void StepInvisibility(float fpTime);
    void SetAsVisible();
    void StepStun(float fpTime);
    void SetUnstun();
    // wounds
    BOOL LoadWounds(LPCSTR pszWoundsTextureName, const CBYTEArray *expectedData = nullptr);
    void ReleaseWounds();
    const CTexture *GetWoundsTexture() const { return pWoundsTexture; }
    // texture/material iterating
    CMaterial *GetFirstMaterial();
    CMaterial *GetNextMaterial();
    CTexture *GetFirstTexture();
    CTexture *GetNextTexture();
    CTexture *GetPrimaryTexture();
    CTexture *GetSecondaryTexture();
    // selection
    virtual void Select(BOOL bSelect);
    virtual void Preselect(BOOL bSelect);
    // highlight function
    void Highlight(BOOL bHighlight);
    // drawing
    virtual BOOL Draw(CRenderer *pRend, CRect *pRect);
    virtual BOOL DrawRealisticShadow2(CRenderer *pRend);
    // shadow functions
    bool IsNeedShadow() const;       // is this object cast shadow in current settings ?
    bool IsNeedRecalcShadow() const; // is this object require shadow recalculation in this frame ?
    void RemoveShadowInformation();
    bool IsShadowVisible() const;
    //
    virtual void FinalDataLoading();
    // class identification functions
    virtual LPCSTR GetGameClassName() const { return "CObject3DClientSpecific"; };
    virtual EClassIDs WhoAmI() const { return HPC_OBJECT3DCLIENTSPECIFIC; };
};
/////////////////////////////////////////////////////////////////////////////////////
inline bool CObject3DClientSpecific::GetScaleMatrix(SHMatrix &matrix) const
{
    if ((fScaleX == 1) && (fScaleY == 1) && (fScaleZ == 1))
        return false;
    Zero(matrix);
    matrix._11 = fScaleX;
    matrix._22 = fScaleY;
    matrix._33 = fScaleZ;
    matrix._44 = 1.0f;
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
// creates shadow shift=scale matrix from current min/max values
inline void CObject3DClientSpecific::CreateShadowShiftScaleMatrix(SHMatrix &matrix)
{
    float fpScaleX = 63.0f / (shadowrect.Width());
    float fpScaleY = 63.0f / (shadowrect.Height());
    float fpShiftX = -shadowrect.minx * fpScaleX;
    float fpShiftY = -shadowrect.miny * fpScaleY;

    Identity(matrix);
    matrix._11 = fpScaleX;
    matrix._22 = fpScaleY;
    matrix._41 = fpShiftX;
    matrix._42 = fpShiftY;
}
/////////////////////////////////////////////////////////////////////////////////////
inline CTexture *CObject3DClientSpecific::GetPrimaryTexture()
{
    if (sets[0])
        return sets[0]->lpTexture;
    else
        return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
inline CTexture *CObject3DClientSpecific::GetSecondaryTexture()
{
    if (sets[1])
        return sets[1]->lpTexture;
    else
        return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
