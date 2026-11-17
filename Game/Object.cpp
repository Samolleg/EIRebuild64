#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include <math.h>

#include "Object.h"
#include "environment_asset.h"
#include "Object3DClientSpecific.h"
#include "model_draw.h"
#include "OwnedRender/E1Scene.h"
#include "World.h"
#include "particle.h"

#include "FigureManager.h"
#include "Renderer.h"
#include "MapObjectManager.h"

#include <vector>
#include <string>
/////////////////////////////////////////////////////////////////////////////////////
// функци€, выставл€юща€ флажки дл€ bv в зависимости от имени фигуры
static void SetBVFlagsByName(SBoundingVolume &bv, const char *pszName)
{
    if (strnicmp(pszName, "BASE", 4) == 0)
        bv.SetBridgeFlag(TRUE);
    if (strnicmp(pszName, "CROWN", 5) == 0)
    {
        bv.SetPassability(AI_PASS_EASY);
        bv.SetOpacity(AI_OPACITY_25);
    }
    if (strnicmp(pszName, "EMPTY", 5) == 0)
    {
        bv.SetIgnore(true);
        bv.SetPassability(AI_PASS_EASY);
        bv.SetOpacity(AI_OPACITY_25);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// ***********************************************************************************
// class CObject3D functions
// this class contain a visual and animation information about the object
// ***********************************************************************************
//////////////////////////////////////////////////////////////////////////////////////
CObject3D::CObject3D()
{
    lpObjectSpecific = NULL;
    lpAnimationSystem = NULL;

    complection.strength = 0;
    complection.grace = 0;
    complection.tall = 0;

    ptCenterShift = VNULL;
}
/////////////////////////////////////////////////////////////////////////////////////
CObject3D::~CObject3D()
{
    Destroy();
}
/////////////////////////////////////////////////////////////////////////////////////
void CObject3D::FreeReferences()
{
    DetachParticles();
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CObject3D::Create()
{
    SetCP(CPlot3D(0, 0, 0));
    SetOrientation(QNULL);
    lpObjectSpecific = NULL;
    ResetAllFlags();
    lpADEntry = 0;
    pADFileHdr = 0;
    complection.strength = 0;
    complection.grace = 0;
    complection.tall = 0;
    pGeometry = 0;
    ptCenterShift = VNULL;
    memset(&bv, 0, sizeof(bv));
    fbvs.RemoveAll();

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CObject3D::Destroy()
{
    OwnedRender::E1Scene().DestroyResource(this);
    pGeometry = 0;
    DetachParticles();
    // unlink all figures from animation system
    if (lpAnimationSystem)
    {
        lpAnimationSystem->UnlinkAllFigures();
        SetAnimationSystem2(NULL);
    }
    // remove figures
    for (int i = 0; i < figures.GetSize(); i++)
    {
        if (figures[i])
        {
            figures[i]->DecRefCount();
            figures[i] = NULL;
        }
    }
    figures.RemoveAll();
    environmentAsset.reset();
    environmentSelection.clear();
    // remove unloaded figures
    for (int i = 0; i < ufigures.GetSize(); i++)
    {
        if (ufigures[i])
        {
            ufigures[i]->Release();
            ufigures[i] = NULL;
        }
    }
    ufigures.RemoveAll();
    // release object specific (and function Destroy in object specific will remove all internal
    // info /like figures/)
    if (lpObjectSpecific)
        lpObjectSpecific->Release();
    lpObjectSpecific = NULL;
    //
    lpADEntry = 0;
    pADFileHdr = 0;
    //
    ResetAllFlags();
}
/////////////////////////////////////////////////////////////////////////////////////
// void CObject3D::SetObjectSpecific( LPGAMEBASE lpNewObjectSpecific )
// sets object3d specific class and registering it
void CObject3D::SetObjectSpecific(LPGAMEBASE lpNewObjectSpecific)
{
    if (lpObjectSpecific)
    {
        lpObjectSpecific->RegisterObject3D(NULL);
        lpObjectSpecific->Release();
    }

    lpObjectSpecific = (CObject3DSpecific *)lpNewObjectSpecific;

    if (lpObjectSpecific)
    {
        lpObjectSpecific->RegisterObject3D(this);
        lpObjectSpecific->AddRef();
    }
}
const SADEntry *CObject3D::GetCurrentAnimation() const
{
    return lpAnimationSystem->GetCurrentAnimation();
}
const SADEntry *CObject3D::GetCurrentADEntry() const
{
    return lpAnimationSystem->GetCurrentAnimation();
}
/////////////////////////////////////////////////////////////////////////////////////
void CObject3D::operator=(const CObject3D &object)
{
    environmentAsset=object.environmentAsset;
    environmentSelection=object.environmentSelection;
    figures.Copy(object.figures);
    CBase3D::operator=(object);
}
/////////////////////////////////////////////////////////////////////////////////////
void CObject3D::Save(CFile &file) const
{
    // save figure identification information

    // call superclass save function
    CBase3D::Save(file);
}
/////////////////////////////////////////////////////////////////////////////////////
void CObject3D::Load(CFile &file)
{
    // load figure identification information

    // call superclass load function
    CBase3D::Load(file);
}
/////////////////////////////////////////////////////////////////////////////////////
void CObject3D::PrepareForDraw(const CQuaternion &q)
{
    rotation1 = q * rotation;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CObject3D::Draw()
// draw object = unit?
BOOL CObject3D::Draw(CRenderer *pRend)
{
    if (!owned::model_draw_writes_pick_state(owned::model_draw_domain))
        return lpObjectSpecific ? lpObjectSpecific->Draw(pRend, nullptr) : TRUE;
    rcScreenRect.SetRectEmpty();

    if (lpObjectSpecific)
        return lpObjectSpecific->Draw(pRend, &rcScreenRect);
    else
        return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CObject3D::Draw()
// draw object
BOOL CObject3D::DrawWithoutBBox(CRenderer *pRend)
{
    if (lpObjectSpecific)
        return lpObjectSpecific->Draw(pRend, 0);
    else
        return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CObject3D::DrawRealisticShadow()
// draw realistic soft shadow with SW rendering
BOOL CObject3D::DrawRealisticShadow2(CRenderer *pRend)
{
    if (lpObjectSpecific)
        return lpObjectSpecific->DrawRealisticShadow2(pRend);
    else
        return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// void CObject3D::AddFigure( CFigure* lpFigure )
// adding figure with specified material and texture typeID to the object
// perform it in four stages :
// 		1. scan for such texture and add one, if need
// 		2. scan for such material and add one, if need
// 		3. scan for such figure
// 		4. add figure to the specified place
BOOL CObject3D::AddFigure(CFigure *lpFigure)
{
    // scan for such figure
    for (int i = 0; i < figures.GetSize(); i++)
    {
        if (figures[i] == lpFigure)
            return TRUE;
    }
    figures.Add(lpFigure);
    lpFigure->AddRef();
    lpFigure->SetObject3D(this);
    // link with animation system
    if (lpAnimationSystem)
        lpAnimationSystem->LinkWithFigure(lpFigure);
    // add figure to the specified place
    if (lpObjectSpecific)
        return lpObjectSpecific->AddFigure(lpFigure);
    else
        return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CObject3D::AddUnloadedFigure(CFigure *lpFigure)
{
    // scan for such figure
    for (int i = 0; i < ufigures.GetSize(); i++)
    {
        if (ufigures[i] == lpFigure)
            return TRUE;
    }
    //
    ufigures.Add(lpFigure);
    lpFigure->AddRef();
    lpFigure->SetObject3D(this);
    // link with animation system
    if (lpAnimationSystem)
        lpAnimationSystem->LinkWithFigure(lpFigure);

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CObject3D::RemoveFigure( CFigure* lpFigure )
// trying to remove specified figure from object (both from 'figures' and from appropriate set)
BOOL CObject3D::RemoveFigure(CFigure *lpFigure)
{
    int j;

    if (lpObjectSpecific)
        lpObjectSpecific->RemoveFigure(lpFigure);
    // search in figures
    for (j = 0; j < figures.GetSize(); ++j)
    {
        if (figures[j] == lpFigure)
        {
            figures.RemoveAt(j);
            lpFigure->SetObject3D(NULL);
            if (lpAnimationSystem)
                lpAnimationSystem->UnlinkFigure(lpFigure);
            lpFigure->Release();
            return TRUE;
        }
    }
    // search in unloaded figures
    for (j = 0; j < ufigures.GetSize(); ++j)
    {
        if (ufigures[j] == lpFigure)
        {
            ufigures.RemoveAt(j);
            if (lpAnimationSystem)
                lpAnimationSystem->UnlinkFigure(lpFigure);
            lpFigure->Release();
            return TRUE;
        }
    }
    //
    return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
// CFigure* CObject3D::RemoveFigure( LPCSTR pszFigureName )
// remove figure from object by name
CFigure *CObject3D::RemoveFigure(LPCSTR pszFigureName)
{
    CFigure *lpFigure = FindFigure(pszFigureName);
    if (lpFigure)
    {
        RemoveFigure(lpFigure);
        return lpFigure;
    }
    else
        return NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
// CFigure* CObject3D::FindFigure( nFigureTypeID )
// search for figure with specified type id
// returns NULL, if no such figure or pointer to one, if it is
CFigure *CObject3D::FindFigure(int nFigureTypeID)
{
    int i;
    // search in figures
    for (i = 0; i < figures.GetSize(); i++)
    {
        if (figures[i]->GetTypeID() == nFigureTypeID)
            return figures[i];
    }
    // search in unloaded figures
    for (i = 0; i < ufigures.GetSize(); i++)
    {
        if (ufigures[i]->GetTypeID() == nFigureTypeID)
            return ufigures[i];
    }

    return NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
// CFigure* CObject3D::FindFigure( LPCSTR pszFigureName )
// trying to find figure by name
CFigure *CObject3D::FindFigure(LPCSTR pszFigureName)
{
    int i;
    // search in figures
    for (i = 0; i < figures.GetSize(); i++)
    {
        if (strcmp(figures[i]->GetName(), pszFigureName) == 0)
            return figures[i];
    }
    // search in unloaded figures
    for (i = 0; i < ufigures.GetSize(); i++)
    {
        if (strcmp(ufigures[i]->GetName(), pszFigureName) == 0)
            return ufigures[i];
    }

    return NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
// CFigure* CObject3D::MatchFigure( LPCSTR pszMatchName )
// find figure, which name begins from 'pszMatchName'
CFigure *CObject3D::MatchFigure(LPCSTR pszMatchName)
{
    int i;
    // search in figures
    for (i = 0; i < figures.GetSize(); i++)
    {
        if (strstr(figures[i]->GetName(), pszMatchName) == figures[i]->GetName())
            return figures[i];
    }
    // search in unloaded figures
    for (i = 0; i < ufigures.GetSize(); i++)
    {
        if (strstr(ufigures[i]->GetName(), pszMatchName) == ufigures[i]->GetName())
            return ufigures[i];
    }

    return NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
void CObject3D::MatchFigures(LPCSTR pszMatchName, CSimplyArray<CFigure *, CFigure *> &figs)
{
    int i;
    // search in figures
    for (i = 0; i < figures.GetSize(); i++)
    {
        if (strstr(figures[i]->GetName(), pszMatchName) == figures[i]->GetName())
            figs.Add(figures[i]);
    }
    // search in unloaded figures
    for (i = 0; i < ufigures.GetSize(); i++)
    {
        if (strstr(ufigures[i]->GetName(), pszMatchName) == ufigures[i]->GetName())
            figs.Add(ufigures[i]);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// void CObject3D::SetSpecific( LPGAMEBASE lpNewSpecific )
// set object specific, if it's type == SPTID_OBJECT3D_SPECIFIC
void CObject3D::SetSpecific(LPGAMEBASE lpNewSpecific)
{
    if (lpNewSpecific && (lpNewSpecific->GetTypeID() == SPTID_OBJECT3D_SPECIFIC))
        SetObjectSpecific(lpNewSpecific);
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL LoadByTemplate( LPCSTR pszTemplateName, CStringArray& szNames, BOOL bLoadData )
// load all unit geometry data by skeleton name
BOOL CObject3D::LoadByTemplate(LPCSTR pszTemplateName, CStringArray &szNames, BOOL bLoadData)
{
    if (LoadEnvironment(pszTemplateName, &szNames)) return TRUE;
    CString szString, szTemplateName = pszTemplateName;
    //
    SetMaskName(pszTemplateName);
    // load geometry
    if (lpAnimationSystem)
    {
#if !(defined(_A4) || defined(__OBJECT_CONVERTOR_A4__))
        lpAnimationSystem->SetComplection(complection.strength, complection.grace,
                                          complection.tall);
#endif // !( defined(_A4) || defined(__OBJECT_CONVERTOR_A4__) )
        lpAnimationSystem->LoadSkeleton(pszTemplateName);
    }
    if ((szNames.GetSize() == 0) && lpAnimationSystem)
    {
        lpAnimationSystem->GetNodeNames(szNames);
        pGeometry = GetWorld()->GetGeometryManager()->GetGeometry(pszTemplateName,
                                                                  complection.strength,
                                                                  complection.grace,
                                                                  complection.tall);
    }
    else
    {
        std::vector<std::string> szNames1(szNames.GetSize());
        for (int i = 0; i < szNames.GetSize(); ++i)
            szNames1[i] = szNames[i];
        pGeometry = GetWorld()->GetGeometryManager()->GetGeometry(pszTemplateName, szNames1,
                                                                  complection.strength,
                                                                  complection.grace,
                                                                  complection.tall);
    }
    // load figures data (and link it with animation system)
    LoadFigures(pszTemplateName, szNames, bLoadData);
    // for BRIDGES we need step animation to get right figure placements for passability
    // also, we need it for units
    if (((WhoAmI() == HPC_BUILDING) || (WhoAmI() == HPC_UNIT)) && lpAnimationSystem)
    {
        lpAnimationSystem->SetAnimation(0);
        // #if !( defined(_A4) || defined(__OBJECT_CONVERTOR_A4__) )
        lpAnimationSystem->RecalcSkeletonWithCurrentTime();
        // #endif // !( defined(_A4) || defined(__OBJECT_CONVERTOR_A4__) )
    }
    // fast recalculate of bounding volume (approximate values)
    RecalcBoundingVolumeFast();
    //
    if (bLoadData || pGeometry)
        SetFlag(OBJ_LOADED);
    else
        ResetFlag(OBJ_LOADED);

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// load figures by their names and link with animation system, if 'load data'
BOOL CObject3D::LoadFigures(LPCSTR pszMask, CStringArray &szNames, BOOL bLoadData)
{
    CFigureManager *lpFigureManager = (CFigureManager *)((CWorld *)GetWorld())->GetFigureManager();
    CModelResourceEntry *pModel = lpFigureManager->GetModel(pszMask);
    //
    SetMaskName(pszMask);
    // check for valid figure manager
    if (lpFigureManager == NULL)
        return ReportError("CObject3D::LoadFigures.\n No valid figure manager.\n");
    // load figures via figure manager
    for (int i = 0; i < szNames.GetSize(); i++)
    {
        LPFIGURE lpFigure = LoadFigure(szNames[i], pModel, bLoadData);
        if (!lpFigure)
            ReportWarning(
                "CObject3D::LoadFigures.\n Can't create object \"%s\" with bodypart \"%s\".\n",
                pszMask, szNames[i]);
    }

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// load figures by their names
CFigure *CObject3D::LoadFigure(LPCSTR pszFigureName, CModelResourceEntry *pModel, BOOL bLoadData)
{
    CFigureManager *lpFigureManager = (CFigureManager *)((CWorld *)GetWorld())->GetFigureManager();
    //
    LPFIGURE lpFigure = lpFigureManager->CreateFigure(szMaskName, pszFigureName, pModel,
                                                      complection.strength, complection.grace,
                                                      complection.tall, bLoadData,
                                                      CheckFlag(OBJ_HEAPGEOMETRY));
    if (lpFigure)
    {
        if (pGeometry)
        {
            if (pGeometry->AddRange(pszFigureName) == FALSE)
                return 0;

            const SRangeExternals &rangeExt = pGeometry->GetRangeExternals(pszFigureName);
            lpFigure->SetMaterialTypeID(rangeExt.tidMaterial);
            lpFigure->SetTextureTypeID(rangeExt.tidTexture);
            *lpFigure->GetBoundingVolume() = rangeExt.bv;
        }
        //
        SetBVFlagsByName(*lpFigure->GetBoundingVolume(), pszFigureName);
        AddFigure(lpFigure);
        //
        lpFigure->GetBoundingVolume()->SetBone(lpFigure->GetCP());
        //
        return lpFigure;
    }
    else
        ReportWarning("CObject3D::LoadFigure.\n Can't load figure \"%s\" for object \"%s\".\n",
                      pszFigureName, szMaskName);

    return NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CObject3D::FinalDataLoading()
// load all unloaded figures
BOOL CObject3D::FinalDataLoading()
{
    if (CheckFlag(OBJ_LOADED))
        return TRUE;

    CFigureManager *lpFigureManager = (CFigureManager *)((CWorld *)GetWorld())->GetFigureManager();
    CModelResourceEntry *pModel = lpFigureManager->GetModel(GetMaskName());
    for (int i = 0; i < ufigures.GetSize(); i++)
    {
        lpFigureManager->ResizeFigure(ufigures[i], pModel, complection.strength, complection.grace,
                                      complection.tall);
        ufigures[i]->DecRefCount();
        AddFigure(ufigures[i]);
    }
    ufigures.RemoveAll();
    //	animations.LinkWithFigures( figures );
    //	if ( lpAnimationSystem )
    //	{
    //		lpAnimationSystem->LinkWithFigures( figures );
    //		lpAnimationSystem->RecalcSkeletonWithCurrentTime();
    //	}
    if (WhoAmI() != HPC_UNIT)
    {
        SetFlagsToAllFigures(FIG_STATIC);
        SetFlag(OBJ_STATIC);
    }
    GetObjectSpecific()->FinalDataLoading();

    SetFlag(OBJ_LOADED);

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// void CObject3D::SetComplection( SComplection& complection )
// set this object complection
void CObject3D::SetComplection(SComplection &newcomplection)
{
    int i;
    // copy complection params
    if ((complection.strength != newcomplection.strength) ||
        (complection.grace != newcomplection.grace) || (complection.tall != newcomplection.tall))
    {
        complection = newcomplection;
        // change complection for geometry
        if (pGeometry)
        {
            pGeometry = GetWorld()->GetGeometryManager()->ChangeComplection(pGeometry,
                                                                            complection.strength,
                                                                            complection.grace,
                                                                            complection.tall);
            //			pGeometry->ChangeComplection( complection.strength,
            // complection.grace, complection.tall );
            for (i = 0; i < figures.GetSize(); ++i)
            {
                const SBoundingVolume &fbv = pGeometry->GetRangeExternals(figures[i]->GetName()).bv;
                figures[i]->GetBoundingVolume()->ptCenter = fbv.ptCenter;
                figures[i]->GetBoundingVolume()->ptMin = fbv.ptMin;
                figures[i]->GetBoundingVolume()->ptMax = fbv.ptMax;
                figures[i]->GetBoundingVolume()->fpRadius = fbv.fpRadius;
            }
        }
        // resize animations
        if (lpObjectSpecific && lpObjectSpecific->WhoAmI() == HPC_OBJECT3DCLIENTSPECIFIC)
            static_cast<CObject3DClientSpecific *>(lpObjectSpecific)->InvalidateOwnedAppearance();
        if (lpAnimationSystem)
            lpAnimationSystem->SetComplection(complection.strength, complection.grace,
                                              complection.tall);
        // fast recalculate of bounding volume
        RecalcBoundingVolumeFast();
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// void CObject3D::SetFlagsToAllFigures( DWORD dwFlags )
// set this flags to all figures
void CObject3D::SetFlagsToAllFigures(DWORD dwFlags)
{
    for (int i = 0; i < figures.GetSize(); i++)
        figures[i]->SetFlag(dwFlags);
}
/////////////////////////////////////////////////////////////////////////////////////
// void CObject3D::ResetFlagsToAllFigures( DWORD dwFlags )
// reset this flags from all figures
void CObject3D::ResetFlagsFromAllFigures(DWORD dwFlags)
{
    for (int i = 0; i < figures.GetSize(); i++)
        figures[i]->ResetFlag(dwFlags);
}
/////////////////////////////////////////////////////////////////////////////////////
int CObject3D::GetFigureIndexByName(LPCSTR pszFigureName)
{
    for (int i = 0; i < szBodyPartNames.GetSize(); i++)
    {
        if (szBodyPartNames[i] == pszFigureName)
            return i;
    }
    return -1;
}
/////////////////////////////////////////////////////////////////////////////////////
// void CObject3D::LoadFigureBoundingVolumes( LPCSTR pszTemplate )
// load bounding volume
void CObject3D::LoadFigureBoundingVolumes(LPCSTR pszTemplate)
{
    if (LoadEnvironment(pszTemplate, nullptr)) return;
#if !(defined(_A4) || defined(__OBJECT_CONVERTOR_A4__))
    CFigureManager *pFigureManager = (CFigureManager *)((CWorld *)GetWorld())->GetFigureManager();
    float maxx = -FLT_MAX, maxy = -FLT_MAX, maxz = -FLT_MAX;
    float minx = FLT_MAX, miny = FLT_MAX, minz = FLT_MAX;
    int i = 0;
    // acquire information about figures (bodyparts)
    if (lpAnimationSystem)
    {
        lpAnimationSystem->SetComplection(complection.strength, complection.grace,
                                          complection.tall);
        lpAnimationSystem->LoadSkeleton(pszTemplate);
        lpAnimationSystem->SetAnimation(NULL);
        lpAnimationSystem->RecalcSkeletonWithCurrentTime();
        fbvs.SetSize(lpAnimationSystem->nodes.GetCount());

        szBodyPartNames.SetSize(lpAnimationSystem->nodes.GetCount());
        //
        CModelResourceEntry *pModel = pFigureManager->GetModel(pszTemplate);
        POSITION pos = lpAnimationSystem->nodes.GetHeadPosition();
        while (pos)
        {
            CAnimationNode *pNode = lpAnimationSystem->nodes.GetNext(pos);

            pFigureManager->LoadBoundingVolume(fbvs[i], pModel, pszTemplate, pNode->GetName(),
                                               complection.strength, complection.grace,
                                               complection.tall);
            fbvs[i].SetBone(pNode->position);
            szBodyPartNames[i] = pNode->GetName();
            // max
            maxx = Max(maxx, fbvs[i].ptMax.x + fbvs[i].ptCenter.x + fbvs[i].ptBone.x);
            maxy = Max(maxy, fbvs[i].ptMax.y + fbvs[i].ptCenter.y + fbvs[i].ptBone.y);
            maxz = Max(maxz, fbvs[i].ptMax.z + fbvs[i].ptCenter.z + fbvs[i].ptBone.z);
            // min
            minx = Min(minx, fbvs[i].ptMin.x + fbvs[i].ptCenter.x + fbvs[i].ptBone.x);
            miny = Min(miny, fbvs[i].ptMin.y + fbvs[i].ptCenter.y + fbvs[i].ptBone.y);
            minz = Min(minz, fbvs[i].ptMin.z + fbvs[i].ptCenter.z + fbvs[i].ptBone.z);
            // aint we got a bridge here
            SetBVFlagsByName(fbvs[i], pNode->GetName());

            i++;
        }

        lpAnimationSystem->DeleteNodes();
    }
    // set general 'bv'
    if (GetTypeID() == MOTID_HUMAN) // correction for humans :)
    {
        maxx = maxy = 0.5f;
        minx = miny = -0.5f;
    }
    CPlot3D plot = CPlot3D((maxx + minx) * 0.5f, (maxy + miny) * 0.5f, (maxz + minz) * 0.5f);
    bv.SetCenter(plot);
    bv.SetBox(CPlot3D(minx - plot.x, miny - plot.y, minz - plot.z),
              CPlot3D(maxx - plot.x, maxy - plot.y, maxz - plot.z));
    bv.SetRadius(fabs(bv.ptMax));
#endif // !( defined(_A4) || defined(__OBJECT_CONVERTOR_A4__) )
}
/////////////////////////////////////////////////////////////////////////////////////
//	void CalculateBoundingVolume();
//  calcs new bounding volume around object, FAKE VERSION !!!
void CObject3D::CalculateBoundingVolume()
{
    bv.SetCenter(GetCP());
    bv.SetBox(CPlot3D(-0.3f, -0.5f, -1.f), CPlot3D(0.3f, 0.5f, 1.f));
}
/////////////////////////////////////////////////////////////////////////////////////
// void CObject3D::RecalcBoundingVolume()
// recalc bounding volume with current figures placement
void CObject3D::RecalcBoundingVolume()
{
    if (CheckFlag(OBJ_LOADED) == 0)
        return;
    float maxx = -FLT_MAX, maxy = -FLT_MAX, maxz = -FLT_MAX;
    float minx = FLT_MAX, miny = FLT_MAX, minz = FLT_MAX;
    int i;
    //
    for (i = 0; i < figures.GetSize(); i++)
    {
        figures[i]->PrepareForDraw(rotation, cp);
        figures[i]->RecalcBoundingBoxWithRotation(maxx, maxy, maxz, minx, miny, minz);
    }
    // after this calculation we have min and max values in camera space
    CPlot3D ptMin(minx, miny, minz);
    CPlot3D ptMax(maxx, maxy, maxz);
    CPlot3D ptCenter = (ptMin + ptMax) * 0.5f;
    bv.SetBox(ptMin - ptCenter, ptMax - ptCenter);
    bv.SetRadius(fabs(ptMin - ptCenter));
    bv.SetCenter(ptCenter - cp);
}
/////////////////////////////////////////////////////////////////////////////////////
void CObject3D::RecalcBoundingVolumeFast()
{
    float maxx = -FLT_MAX, maxy = -FLT_MAX, maxz = -FLT_MAX;
    float minx = FLT_MAX, miny = FLT_MAX, minz = FLT_MAX;
    int i;
    // loaded figures
    for (i = 0; i < figures.GetSize(); i++)
    {
        const SBoundingVolume &fbv = figures[i]->GetBV();
        const CPlot3D &fcp = figures[i]->GetCP();
        // CRAP - old bodypart
        if (fabs2(fcp) > 1e9f)
            continue;
        // end of CRAP

        minx = Min(minx, fcp.x + fbv.ptCenter.x + fbv.ptMin.x);
        maxx = Max(maxx, fcp.x + fbv.ptCenter.x + fbv.ptMax.x);

        miny = Min(miny, fcp.y + fbv.ptCenter.y + fbv.ptMin.y);
        maxy = Max(maxy, fcp.y + fbv.ptCenter.y + fbv.ptMax.y);

        minz = Min(minz, fcp.z + fbv.ptCenter.z + fbv.ptMin.z);
        maxz = Max(maxz, fcp.z + fbv.ptCenter.z + fbv.ptMax.z);
    }
    // unloaded (bounding volume only) figures
    for (i = 0; i < ufigures.GetSize(); i++)
    {
        const SBoundingVolume &fbv = ufigures[i]->GetBV();
        const CPlot3D &fcp = ufigures[i]->GetCP();
        // CRAP
        (*((CPlot3D *)(&fcp))).SetPlot(0, 0, 0);
        //
        // CRAP - old bodypart
        if (fabs2(fcp) > 1e9f)
            continue;
        // end of CRAP

        minx = Min(minx, fcp.x + fbv.ptCenter.x + fbv.ptMin.x);
        maxx = Max(maxx, fcp.x + fbv.ptCenter.x + fbv.ptMax.x);

        miny = Min(miny, fcp.y + fbv.ptCenter.y + fbv.ptMin.y);
        maxy = Max(maxy, fcp.y + fbv.ptCenter.y + fbv.ptMax.y);

        minz = Min(minz, fcp.z + fbv.ptCenter.z + fbv.ptMin.z);
        maxz = Max(maxz, fcp.z + fbv.ptCenter.z + fbv.ptMax.z);
    }
    // after this calculation we have min and max values in camera space
    if (figures.GetSize() || ufigures.GetSize())
    {
        CPlot3D ptMin(minx, miny, minz);
        CPlot3D ptMax(maxx, maxy, maxz);
        CPlot3D ptCenter = (ptMin + ptMax) * 0.5f;
        bv.SetBox(ptMin - ptCenter, ptMax - ptCenter);
        bv.SetRadius(fabs(ptMin - ptCenter));
        bv.SetCenter(ptCenter);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CObject3D::SetNextAnimation(const SADEntry *lpNewEntry, BOOL bNow)
{
    //	animations.SetNextAnimation( lpNewEntry, bNow );
    if (lpAnimationSystem)
        lpAnimationSystem->SetAnimation(lpNewEntry);
}
/////////////////////////////////////////////////////////////////////////////////////
#if !(defined(_A4) || defined(__OBJECT_CONVERTOR_A4__))
// set animation system (with registration)
void CObject3D::SetAnimationSystem2(CAnimationSystem2 *lpNewAnimationSystem)
{
    // release old animation system
    if (lpAnimationSystem)
    {
        lpAnimationSystem->SetObject3D(NULL);
        lpAnimationSystem->Release();
    }
    // set new one
    lpAnimationSystem = lpNewAnimationSystem;
    // register new animation system
    if (lpAnimationSystem)
    {
        lpAnimationSystem->AddRef();
        lpAnimationSystem->SetObject3D(this);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
#else
// set animation system (with registration)
void CObject3D::SetAnimationSystem2(CAnimationSystem3 *lpNewAnimationSystem)
{
    // release old animation system
    if (lpAnimationSystem)
    {
        lpAnimationSystem->SetObject3D(NULL);
        lpAnimationSystem->Release();
    }
    // set new one
    lpAnimationSystem = lpNewAnimationSystem;
    // register new animation system
    if (lpAnimationSystem)
    {
        lpAnimationSystem->AddRef();
        lpAnimationSystem->SetObject3D(this);
    }
}
#endif // !( defined(_A4) || defined(__OBJECT_CONVERTOR_A4__) )
/////////////////////////////////////////////////////////////////////////////////////
BOOL CObject3D::LinkFigure(CFigure *lpFigure)
{
    if (lpAnimationSystem)
        return lpAnimationSystem->LinkWithFigure(lpFigure);
    else
        return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CObject3D::LoadAnimation(LPCSTR pszTemplate, const SADEntry *lpNewEntry)
{
    if (environmentAsset) return TRUE;
    if (lpAnimationSystem)
        lpAnimationSystem->SetAnimation(lpNewEntry);

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CObject3D::InitAnimation()
{
    if (environmentAsset) return;
    if (lpAnimationSystem)
    {
        lpAnimationSystem->SetTime(0);
#if !(defined(_A4) || defined(__OBJECT_CONVERTOR_A4__))
        lpAnimationSystem->RecalcSkeleton();
#else
        lpAnimationSystem->RecalcSkeletonWithCurrentTime();
#endif // !( defined(_A4) || defined(__OBJECT_CONVERTOR_A4__) )
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CObject3D::AnimationStepFwd(float fpStep)
{
    if (lpAnimationSystem)
        lpAnimationSystem->Step(fpStep);
}
/////////////////////////////////////////////////////////////////////////////////////
CPlot3D CObject3D::GetFigureWorldPosition(CFigure *pFigure) const
{
    float fSX, fSY, fSZ;
    lpObjectSpecific->GetScaleCoeffs(fSX, fSY, fSZ);
    CPlot3D ptFigureCP = rotation.Rotate(pFigure->GetCP());
    return (GetCP() + CPlot3D(ptFigureCP.x * fSX, ptFigureCP.y * fSY, ptFigureCP.z * fSZ));
}
/////////////////////////////////////////////////////////////////////////////////////
void CObject3D::AttachParticleSource(CParticle *pParticleSource)
{
    // предотвращение циклического вызова AttachCarrier/AttachParticleSource
    if (particles.Find(pParticleSource))
        return;
#ifdef _DEBUG
    if (pParticleSource)
    {
        if (pParticleSource->WhoAmI() != HPC_PARTICLE)
            ReportError(
                "CObject3D::AttachParticleSource.\n Attaching object \"%s\" instead of particle.\n",
                pParticleSource->GetGameClassName());
    }
#endif // _DEBUG
       // attach new one
    particles.AddTail(pParticleSource);
    // increase ref count
    if (pParticleSource)
    {
        pParticleSource->AddRef();
        // attach here this object to particle source
        pParticleSource->AttachCarrier(this);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CObject3D::DetachParticleSource(CParticle *pParticleSource)
{
    POSITION pos = particles.Find(pParticleSource);
    if (pos)
    {
        particles.RemoveAt(pos);
        if (pParticleSource)
        {
            pParticleSource->AttachCarrier(NULL);
            pParticleSource->Release();
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CObject3D::DetachParticles()
{
    while (POSITION pos = particles.GetHeadPosition())
        DetachParticleSource(particles.GetAt(pos));
}
/////////////////////////////////////////////////////////////////////////////////////
const SADBFileHeader *CObject3D::SetADFileHdr() const
{
    if (pADFileHdr)
        return pADFileHdr;
    CMapObjectManager *pMOM = static_cast<CMapObjectManager *>(GetManager());
    if (pMOM)
        pADFileHdr = ptr_cast<SADBFileHeader>(pMOM->GetFile(szMaskName + ".adb"));
    return pADFileHdr;
}
/////////////////////////////////////////////////////////////////////////////////////
float CObject3D::GetHeightCorrection(float fTall) const
{
    if (WhoAmI() != HPC_UNIT)
        return 1.0f;
    // acquire pointer to ADB file header
    if (SetADFileHdr() == 0)
        return 1.0f;
    //
    return pADFileHdr->GetHeightCorrection(fTall);
}
/////////////////////////////////////////////////////////////////////////////////////
void CObject3D::GetAllAnimationEntries(CADEntryPtrArray &szArray) const
{
    // acquire pointer to ADB file header
    if (SetADFileHdr() == 0)
        return;
    //
    SADEntry *entries = reinterpret_cast<SADEntry *>(LPBYTE(pADFileHdr) +
                                                     pADFileHdr->ShiftToEntry(0));
    for (int i = 0; i < pADFileHdr->dwCount; ++i)
        szArray.Add(&entries[i]);
}
/////////////////////////////////////////////////////////////////////////////////////
const SADEntry *CObject3D::GetAnimationEntry(LPCSTR pszAnimationName) const
{
    // acquire pointer to ADB file header
    if (SetADFileHdr() == 0)
        return 0;
    //
    const SADEntry *entries = reinterpret_cast<const SADEntry *>(LPBYTE(pADFileHdr) +
                                                                 pADFileHdr->ShiftToEntry(0));
    for (int i = 0; i < pADFileHdr->dwCount; ++i)
    {
        if (strcmp(entries[i].pszAnimationName, pszAnimationName) == 0)
            return &entries[i];
    }

    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
const SADEntry *CObject3D::GetAnimationEntryByIndex(DWORD cIndex) const
{
    // acquire pointer to ADB file header
    if (SetADFileHdr() == 0)
        return 0;
    //
    const SADEntry *entries = reinterpret_cast<const SADEntry *>(LPBYTE(pADFileHdr) +
                                                                 pADFileHdr->ShiftToEntry(0));
    if (cIndex < pADFileHdr->dwCount)
        return reinterpret_cast<const SADEntry *>(LPBYTE(pADFileHdr) +
                                                  pADFileHdr->ShiftToEntry(cIndex));
    else
        return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
const SADEntry *CObject3D::GetSpecialAnimationEntry(DWORD dwSpecialIndex) const
{
    // acquire pointer to ADB file header
    if (SetADFileHdr() == 0)
        return 0;
    //
    DWORD dwModifierSpecial = ADB_MODIFIER_SPECIAL_1 * dwSpecialIndex;
    const SADEntry *entries = reinterpret_cast<const SADEntry *>(LPBYTE(pADFileHdr) +
                                                                 pADFileHdr->ShiftToEntry(0));
    for (int i = 0; i < int(pADFileHdr->dwCount); ++i)
    {
        if (((entries[i].dwFlags & ADB_ACTIONS) == ADB_ACTION_SPECIAL) &&
            ((entries[i].dwFlags & ADB_MODIFIERS) == dwModifierSpecial))
            return &entries[i];
    }

    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
const SADEntry *CObject3D::GetAnimationEntry(DWORD dwCaps) const
{
    // acquire pointer to ADB file header
    if (SetADFileHdr() == 0)
        return 0;
    //
    const SADEntry *entries = reinterpret_cast<const SADEntry *>(LPBYTE(pADFileHdr) +
                                                                 pADFileHdr->ShiftToEntry(0));
    for (int i = 0; i < pADFileHdr->dwCount; ++i)
    {
        if (entries[i].Match(dwCaps))
            return &entries[i];
    }

    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
const SADEntry *CObject3D::GetRandomAnimationEntry(DWORD dwCaps) const
{
    // acquire pointer to ADB file header
    if (SetADFileHdr() == 0)
        return 0;
    //
    CSimplyArray<const SADEntry *, const SADEntry *> entryArray;
    const SADEntry *entries = reinterpret_cast<const SADEntry *>(LPBYTE(pADFileHdr) +
                                                                 pADFileHdr->ShiftToEntry(0));
    for (int i = 0; i < int(pADFileHdr->dwCount); ++i)
    {
        if (entries[i].Match(dwCaps) && CanPlayAnimation(&(entries[i])))
            entryArray.Add(&entries[i]);
    }
    //
    if (entryArray.GetSize() > 1)
    {
        DWORD dwProbability = Random(100);
        const SADEntry *pReservedADEntry = entryArray[Random(entryArray.GetSize())];
        // remove all entries with probability less, then random value
        for (int i = 0; i < entryArray.GetSize(); ++i)
        {
            if (entryArray[i]->dwProbability < dwProbability)
            {
                entryArray.RemoveAt(i);
                --i;
            }
        }
        if (entryArray.GetSize() > 0)
            return entryArray[Random(entryArray.GetSize())];
        else
            return pReservedADEntry;
    }
    else if (entryArray.GetSize() == 1)
        return entryArray[0];

    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
const SADEntry *CObject3D::GetAnimationEntryNoWeapon(DWORD dwCaps) const
{
    // acquire pointer to ADB file header
    if (SetADFileHdr() == 0)
        return 0;
    //
    const SADEntry *entries = reinterpret_cast<const SADEntry *>(LPBYTE(pADFileHdr) +
                                                                 pADFileHdr->ShiftToEntry(0));
    for (int i = 0; i < pADFileHdr->dwCount; ++i)
    {
        if (entries[i].MatchNoWeapon(dwCaps))
            return &entries[i];
    }

    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
const SADEntry *CObject3D::GetRandomAnimationEntryNoWeapon(DWORD dwCaps) const
{
    // acquire pointer to ADB file header
    if (SetADFileHdr() == 0)
        return 0;
    //
    CSimplyArray<const SADEntry *, const SADEntry *> entryArray;
    const SADEntry *entries = reinterpret_cast<const SADEntry *>(LPBYTE(pADFileHdr) +
                                                                 pADFileHdr->ShiftToEntry(0));
    for (int i = 0; i < int(pADFileHdr->dwCount); ++i)
    {
        if (entries[i].MatchNoWeapon(dwCaps) && CanPlayAnimation(&(entries[i])))
            entryArray.Add(&entries[i]);
    }
    //
    if (entryArray.GetSize())
        return entryArray[Random(entryArray.GetSize())];

    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CObject3D::SetDefaultColor(DWORD dwColor)
{
    for (int i = 0; i < figures.GetSize(); ++i)
        figures[i]->SetDefaultColor(dwColor);
}
/////////////////////////////////////////////////////////////////////////////////////
void CObject3D::SetDefaultSpecular(DWORD dwColor)
{
    for (int i = 0; i < figures.GetSize(); ++i)
        figures[i]->SetDefaultSpecular(dwColor);
}
/////////////////////////////////////////////////////////////////////////////////////

CParticle* CObject3D::FindPowerHitGlow(int hit){for(POSITION pos=particles.GetHeadPosition();pos;){auto*p=particles.GetNext(pos);if(p&&p->IsPowerHitGlow(hit))return p;}return nullptr;}

bool CObject3D::LoadEnvironment(LPCSTR name, const CStringArray* selected)
{
    if (WhoAmI()==HPC_UNIT) return false;
    auto asset=owned::environment::select(name);
    if (!asset) { environmentAsset.reset();environmentSelection.clear();return false; }
    if (WhoAmI()!=HPC_BUILDING && WhoAmI()!=HPC_STONE && WhoAmI()!=HPC_STUFF_OBJECT)
        throw std::runtime_error("environment GLB: this object class is not in the rigid slice");
    std::vector<std::string> selection;
    if (selected) for(INT_PTR i=0;i<selected->GetSize();++i) {
        const std::string part=(*selected)[i];
        if(std::none_of(asset->parts.begin(),asset->parts.end(),[&](const auto& p){return _stricmp(p.name.c_str(),part.c_str())==0;}))
            throw std::runtime_error("environment GLB: unknown selected part "+part);
        selection.push_back(part);
    }
    environmentAsset=asset;environmentSelection=std::move(selection);SetMaskName(name);
    fbvs.SetSize(asset->parts.size());szBodyPartNames.SetSize(asset->parts.size());
    for(size_t i=0;i<asset->parts.size();++i) {
        const auto& part=asset->parts[i];
        CPlot3D lo(part.minimum[0],part.minimum[1],part.minimum[2]);
        CPlot3D hi(part.maximum[0],part.maximum[1],part.maximum[2]);
        auto& box=fbvs[i];box=SBoundingVolume{};const auto center=(lo+hi)*.5f;
        box.SetCenter(center);box.SetBox(lo-center,hi-center);box.SetRadius(fabs(hi-center));
        box.SetBone(CPlot3D(0,0,0));box.SetIgnore(!part.solid);
        if(!part.solid){box.SetPassability(AI_PASS_EASY);box.SetOpacity(AI_OPACITY_25);}
        szBodyPartNames[i]=part.name.c_str();
    }
    const CPlot3D lo(asset->minimum[0],asset->minimum[1],asset->minimum[2]);
    const CPlot3D hi(asset->maximum[0],asset->maximum[1],asset->maximum[2]);
    const auto center=(lo+hi)*.5f;bv=SBoundingVolume{};
    bv.SetCenter(center);bv.SetBox(lo-center,hi-center);bv.SetRadius(fabs(hi-center));
    SetFlag(OBJ_LOADED|OBJ_STATIC);
    return true;
}
