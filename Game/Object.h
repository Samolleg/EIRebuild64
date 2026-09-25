#pragma once

#include <AfxTempl.h>

#include <math.h>

#include "GeometryInterfaces.h"

#include "AnimationSystem.h"
#include "AnimationSystem2.h"
#include "Figure.h"

/////////////////////////////////////////////////////////////////////////////////////
class CRenderer;
class CObject;
class CParticle;
class CModelResourceEntry;
class CAnimationSystem2;
/////////////////////////////////////////////////////////////////////////////////////
const DWORD OBJ_NOSHADOW = MAKE_FLAG(B3D_LAST_BIT, 0); // this object don't cast shadow
const DWORD OBJ_SELECTED = MAKE_FLAG(B3D_LAST_BIT, 1); // object selected (with outline)
const DWORD OBJ_VISIBLE = MAKE_FLAG(B3D_LAST_BIT,
                                    2); // object visible by camera during current frame
const DWORD OBJ_HIDDEN = MAKE_FLAG(B3D_LAST_BIT, 3);      // object hidden
const DWORD OBJ_LOADED = MAKE_FLAG(B3D_LAST_BIT, 4);      // all object's geometry already loaded
const DWORD OBJ_PRESELECTED = MAKE_FLAG(B3D_LAST_BIT, 5); // object pre-selected (highlight only)
const DWORD OBJ_STATIC = MAKE_FLAG(
    B3D_LAST_BIT, 6); // object are static - don't require relight and reshadow each frame
const DWORD OBJ_LIGHTED = MAKE_FLAG(
    B3D_LAST_BIT, 7); // object already lighted and don't require relight and reshadow
const DWORD OBJ_USE_DEFAULT_FOG = MAKE_FLAG(
    B3D_LAST_BIT, 8); // don't calculate fog from object's position for this object
const DWORD OBJ_HEAPGEOMETRY = MAKE_FLAG(B3D_LAST_BIT,
                                         9); // geometry placed in heap instead of file mapping
const DWORD OBJ_UNSELECTABLE = MAKE_FLAG(B3D_LAST_BIT, 10); // object cannot be selected
const DWORD OBJ_FLATPOINTLIGHTS = MAKE_FLAG(
    B3D_LAST_BIT, 11); // do not use normals in caluclations of point lights

const DWORD OBJ_LAST_BIT = MAKE_LAST_BIT(B3D_LAST_BIT, 11);
/////////////////////////////////////////////////////////////////////////////////////
#pragma pack(4)
struct SComplection
{
    float strength; // strength: 0 = weak, 1 = strong
    float grace;    // grace: 0 = grace, 1 = fat
    float tall;     // tall: 0 = small, 1 = tall
};
#pragma pack()
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
// **                          class CObject3DSpecific                           ** //
// ******************************************************************************** //
//////////////////////////////////////////////////////////////////////////////////////
class CObject3DSpecific : public CGameBase
{
protected:
    CObject3D *lpObject;

public:
    CObject3DSpecific();
    virtual ~CObject3DSpecific();
    //
    void RegisterObject3D(CObject3D *lpObject);
    // figures-in-object management
    virtual BOOL AddFigure(CFigure *lpFigure) = 0;
    virtual BOOL RemoveFigure(CFigure *lpFigure) = 0;
    virtual CFigure *RemoveFigure(LPCSTR pszFigureName) = 0;
    virtual CFigure *FindFigure(int nFigureTypeID) = 0;
    virtual CFigure *FindFigure(LPCSTR pszFigureName) = 0;
    //
    virtual void GetScaleCoeffs(float &fScaleX, float &fScaleY, float &fScaleZ) const
    {
        fScaleX = 1;
        fScaleY = 1;
        fScaleZ = 1;
    }
    // drawing (may be empty)
    virtual BOOL Draw(CRenderer *pRend, CRect *pRect)
    {
        ASSERT(0);
        return FALSE;
    }
    virtual BOOL DrawRealisticShadow2(CRenderer *pRend)
    {
        ASSERT(0);
        return FALSE;
    }
    //
    virtual void FinalDataLoading() {};
    // selection
    virtual BOOL CanSelectRoughly(const CPoint &point) const
    {
        ASSERT(0);
        return FALSE;
    }
    virtual BOOL IsCanSelect(const CPoint &point)
    {
        ASSERT(0);
        return FALSE;
    }
    virtual BOOL IsCanSelect(const CRect &rect)
    {
        ASSERT(0);
        return FALSE;
    }
    virtual void Select(BOOL bSelect) {}
    virtual void Preselect(BOOL bSelect) {}
};
/////////////////////////////////////////////////////////////////////////////////////
inline void CObject3DSpecific::RegisterObject3D(CObject3D *lpNewObject)
{
    lpObject = lpNewObject;
}
inline CObject3DSpecific::CObject3DSpecific()
{
    lpObject = NULL;
}
inline CObject3DSpecific::~CObject3DSpecific()
{
    RegisterObject3D(NULL);
}
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
// **                            class CObject3D                                 ** //
// ******************************************************************************** //
//////////////////////////////////////////////////////////////////////////////////////
class CObject3D : public CBase3D
{
    friend class CObject3DClientSpecific;
    friend class CObject3DServer;

protected:
    CObject3DSpecific *lpObjectSpecific; // specific (client/server)
#if !(defined(_A4) || defined(__OBJECT_CONVERTOR_A4__))
    CAnimationSystem2 *lpAnimationSystem;
#else
    CAnimationSystem3 *lpAnimationSystem;
#endif                         // !( defined(_A4) || defined(__OBJECT_CONVERTOR_A4__) )
    const SADEntry *lpADEntry; // current AD entry
    mutable const SADBFileHeader
        *pADFileHdr;    // file header of the animation database for this object
    CString szMaskName; // mask name of this object
    //
    CList<CParticle *, CParticle *> particles;  // list of attached particle sources
    SComplection complection;                   // complection of this object
    CSimplyArray<CFigure *, CFigure *> figures; // figures of this object
    CSimplyArray<CFigure *, CFigure *>
        ufigures;           // unloaded (w/o geometry data) figures of this object
    IGeometryPtr pGeometry; // geometry of this object
    //
    CPlot3D ptCenterShift; // shift to center to rotate around _REAL_ object's center
    CRect rcScreenRect;
    //
    CStringArray szBodyPartNames;

public:
    SBoundingVolume bv;                              // this object's bounding volume
    CArray<SBoundingVolume, SBoundingVolume &> fbvs; // figure bounding volumes
private:
    const SADBFileHeader *SetADFileHdr() const;

public:
    CObject3D();
    CObject3D(const CObject3D &A);
    virtual ~CObject3D();
    // create/destroy
    virtual void FreeReferences();
    virtual BOOL Create();
    virtual void Destroy();
    // complection
    void SetComplection(SComplection &complection);
    void GetComplection(SComplection &complection) const;
    const SComplection &GetComplection() const { return complection; }
    // mask name
    LPCSTR GetMaskName() const { return szMaskName; }
    void SetMaskName(LPCSTR pszMaskName) { szMaskName = pszMaskName; }
    //
    const CRect &GetScreenRect() const { return rcScreenRect; }
    // animation functions
    void SetNextAnimation(const SADEntry *lpNewEntry, BOOL bNow = FALSE);
    void SetCurrentAnimationEntry(const SADEntry *lpNewEntry) { lpADEntry = lpNewEntry; }
    const SADEntry *GetCurrentADEntry() const;
    const SADEntry *GetCurrentAnimation() const;
    void GetAllAnimationEntries(CADEntryPtrArray &szArray) const;
    const SADEntry *GetAnimationEntry(LPCSTR pszAnimationName) const;
    const SADEntry *GetAnimationEntryByIndex(DWORD cIndex) const;
    const SADEntry *GetSpecialAnimationEntry(DWORD dwSpecialIndex) const;
    const SADEntry *GetAnimationEntry(DWORD dwCaps) const;
    const SADEntry *GetRandomAnimationEntry(DWORD dwCaps) const;
    const SADEntry *GetAnimationEntryNoWeapon(DWORD dwCaps) const;
    const SADEntry *GetRandomAnimationEntryNoWeapon(DWORD dwCaps) const;
    float GetHeightCorrection(float fTall) const;
    virtual bool CanPlayAnimation(const SADEntry *pEntry) const { return true; }
    // object3d specific
    virtual void SetSpecific(LPGAMEBASE lpNewSpecific);
    void SetObjectSpecific(LPGAMEBASE lpNewObjectSpecific);
    CObject3DSpecific *GetObjectSpecific() { return lpObjectSpecific; }
    // animation system
#if !(defined(_A4) || defined(__OBJECT_CONVERTOR_A4__))
    void SetAnimationSystem2(CAnimationSystem2 *lpNewAnimationSystem);
    CAnimationSystem2 *GetAnimationSystem2() { return lpAnimationSystem; }
#else
    void SetAnimationSystem2(CAnimationSystem3 *lpNewAnimationSystem);
    CAnimationSystem3 *GetAnimationSystem2() { return lpAnimationSystem; }
#endif // !( defined(_A4) || defined(__OBJECT_CONVERTOR_A4__) )
    // operator '='
    virtual void operator=(const CObject3D &A);
    // figures-in-object management
    void SetDefaultColor(DWORD dwColor);
    void SetDefaultSpecular(DWORD dwColor);
    BOOL AddFigure(CFigure *lpFigure);
    BOOL AddUnloadedFigure(CFigure *lpFigure);
    BOOL RemoveFigure(CFigure *lpFigure);
    CFigure *RemoveFigure(LPCSTR pszFigureName);
    CFigure *FindFigure(int nFigureTypeID);
    CFigure *FindFigure(LPCSTR pszFigureName);
    CFigure *MatchFigure(LPCSTR pszMatchName);
    void MatchFigures(LPCSTR pszMatchName, CSimplyArray<CFigure *, CFigure *> &figs);
    CFigure *GetFigure(int i) { return figures[i]; }
    CPlot3D GetFigureWorldPosition(CFigure *pFigure) const;
    int GetNumFigures() const { return figures.GetSize(); }
    int GetTotalNumFigures() const { return figures.GetSize() + ufigures.GetSize(); }
    int GetFigureIndexByName(LPCSTR pszFigureName);
    LPCSTR GetFigureName(int i) { return figures[i]->GetName(); }
    BOOL LinkFigure(CFigure *lpFigure);
    const CStringArray &GetBodyPartNames() const { return szBodyPartNames; }
    const IGeometry *GetGeometry() const { return pGeometry; }
    IGeometry *GetGeometry() { return pGeometry; }
    // figures loading...
    BOOL LoadByTemplate(LPCSTR pszTemplateName, CStringArray &szNames, BOOL bLoadData = TRUE);
    BOOL LoadFigures(LPCSTR pszMask, CStringArray &szNames, BOOL bLoadData);
    CFigure *LoadFigure(LPCSTR pszFigureName, CModelResourceEntry *pModel, BOOL bLoadData);
    //
    void SetCenterShift(const CPlot3D &ptNewCenterShift) { ptCenterShift = ptNewCenterShift; }
    const CPlot3D GetCenterShift() const
    {
        return ptCenterShift * GetHeightCorrection(complection.tall);
    }
    // selection
    BOOL CanSelectRoughly(const CPoint &point) const
    {
        return lpObjectSpecific->CanSelectRoughly(point);
    }
    BOOL IsCanSelect(const CPoint &point) { return lpObjectSpecific->IsCanSelect(point); }
    BOOL IsCanSelect(const CRect &rect) { return lpObjectSpecific->IsCanSelect(rect); }
    void Select(BOOL bSelect);
    void Preselect(BOOL bSelect);
    //
    void SetFlagsToAllFigures(DWORD dwFlags);
    void ResetFlagsFromAllFigures(DWORD dwFlags);
    //
    virtual bool IsInvisible() const { return false; }
    // drawing and preparaion for it
    virtual void PrepareForDraw(const CQuaternion &q);
    virtual BOOL DrawWithoutBBox(CRenderer *pRend);
    virtual BOOL Draw(CRenderer *pRend);
    virtual BOOL DrawRealisticShadow2(CRenderer *pRend);
    virtual void SetExactPosition(float fpTime) {}
    // final data loading
    virtual BOOL FinalDataLoading();
    // bounding volume functions
    LPBOUNDINGVOLUME GetBoundingVolumeFromFigure(int i)
    {
        if (i < figures.GetSize())
            return figures[i]->GetBoundingVolume();
        else
            return ufigures[i - figures.GetSize()]->GetBoundingVolume();
    }
    const SBoundingVolume &GetBoundingVolume() const { return bv; }
    void CalculateBoundingVolume(); // FAKE
    void LoadFigureBoundingVolumes(LPCSTR pszTemplate);
    void RecalcBoundingVolume();
    void RecalcBoundingVolumeFast();
    // animation functions
    BOOL LoadAnimation(LPCSTR pszTemplate, const SADEntry *lpNewEntry = NULL);
    void InitAnimation();
    void AnimationStepFwd(float fpStep);
#if (defined __MAP_EDITOR__) || (defined __OBJECT_CONVERTOR_ONLY__)
    CSimplyArray<CFigure *, CFigure *> &GetFigures() { return figures; }
#endif // (defined __MAP_EDITOR__) || (defined __OBJECT_CONVERTOR_ONLY__)
       // particles
    void AttachParticleSource(CParticle *pParticleSource);
    void DetachParticleSource(CParticle *pParticleSource);
    void DetachParticles();
    CParticle* FindPowerHitGlow(int hit);
    // service : save/load
    virtual void Save(CFile &file) const;
    virtual void Load(CFile &file);
};
/////////////////////////////////////////////////////////////////////////////////////
inline CObject3D::CObject3D(const CObject3D &A)
    : CBase3D()
{
    (*this) = A;
}
// FIXME: WTF?
inline void CObject3D::GetComplection(SComplection &comp) const
{
    comp = complection;
}
inline void CObject3D::Select(BOOL bSelect)
{
    // set/reset pre-selection for object too
    if (bSelect && !CheckFlag(OBJ_SELECTED))
    {
        if (!CheckFlag(OBJ_PRESELECTED))
            lpObjectSpecific->Preselect(bSelect);

        SetFlag(OBJ_SELECTED);
        lpObjectSpecific->Select(bSelect);
    }
    else if (!bSelect && CheckFlag(OBJ_SELECTED))
    {
        ResetFlag(OBJ_SELECTED | OBJ_PRESELECTED);
        lpObjectSpecific->Select(bSelect);
        lpObjectSpecific->Preselect(bSelect);
    }
}
inline void CObject3D::Preselect(BOOL bSelect)
{
    if (CheckFlag(OBJ_SELECTED))
        return; // can't pre-select already SELECTED object
    if (bSelect && !CheckFlag(OBJ_PRESELECTED))
    {
        SetFlag(OBJ_PRESELECTED);
        lpObjectSpecific->Preselect(bSelect);
    }
    else if (!bSelect && CheckFlag(OBJ_PRESELECTED))
    {
        ResetFlag(OBJ_PRESELECTED);
        lpObjectSpecific->Preselect(bSelect);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
