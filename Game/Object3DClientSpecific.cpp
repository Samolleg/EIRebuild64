#include "StdAfx.h"
#include "CorePerf.h"
#include "OwnedRender/E1PerfDiagnostics.h"
#include <algorithm>
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "WorldClient.h"

#include "Renderer.h"
#include "DXGeometryProcessor.h"

#include "TextureManager.h"
#include "ModSecondaryAtlas.h"
#include "ModPrimaryAtlas.h"
#include "ModTextureMemory.h"

#include "dx11/RendererDX11.h"
#include "MaterialManager.h"
#include "GraphicsProcessor.h"

#include "Object3DClientSpecific.h"
#include "FigureClientSpecific.h"
#include "AnimationSystem2.h"
#include "OwnedRender/E1Scene.h"
#include "OwnedRender/E1VisualFrame.h"
#include "OwnedRender/E1Surface.h"
#include "owned_asset_loader.h"
#include "engine_bridge.h"
#include "model_draw.h"

#ifndef _A4
#include "UnitClientSpecific.h"
#include "PlayerClient.h"
#include "WorldInterface.h"
#include "Terrain.h"
#else
#include "ViewObject.h"
#endif //_A4

#include "LightManager.h"
#include "Light.h"
// #include "GSVars.h"
/////////////////////////////////////////////////////////////////////////////////////
bool IsPrimaryTextureTID(ETypeIDs tid)
{
    switch (tid)
    {
    case TTID_UNIT_MAIN:
    case TTID_TREE_MAIN:
    case TTID_HOUSE_MAIN:
    case TTID_SPELL_MAIN:
    case TTID_STONE_MAIN:
    case TTID_OBJECT_MAIN:
    case TTID_WATER_MAIN:
    case TTID_TERRAIN:
    case TTID_ANIMATED1:
    case TTID_ANIMATED2:
    case TTID_ANIMATED3:
    case TTID_ANIMATED4:
        return true;
    default:
        return false;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
// struct STextureMaterialSet functions                                             //
// this struct contain material figure groups:                                      //
// figures for this texture, which (figure) are sorted by materials                 //
// ******************************************************************************** //
//////////////////////////////////////////////////////////////////////////////////////
STextureMaterialSet::STextureMaterialSet()
{
    lpTexture = NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
STextureMaterialSet::~STextureMaterialSet()
{
    RemoveAll();
    lpTexture = NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL STextureMaterialSet::AddMaterial( CMaterial* lpMaterial )
// adding new material in this set (with material set creation)
BOOL STextureMaterialSet::AddMaterial(CMaterial *lpMaterial)
{
    int i;
    // first, trying to find matgroup with such typeID and, if it is, return with FALSE
    for (i = 0; i < matgroups.GetSize(); i++)
    {
        if (matgroups[i]->lpMaterial &&
            (matgroups[i]->lpMaterial->GetTypeID() == lpMaterial->GetTypeID()))
            return FALSE;
    }
    // if no one, add new
    SMaterialFigureSet *pMatgroup = dbgnew SMaterialFigureSet;
    pMatgroup->lpMaterial = lpMaterial;
    pMatgroup->lpMaterial->AddRef();
    matgroups.Add(pMatgroup);
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL STextureMaterialSet::AddFigure( CFigure* lpFigure, int nMaterialTypeID )
// adds new figure to the specified material set
BOOL STextureMaterialSet::AddFigure(CFigure *lpFigure, int nMaterialTypeID)
{
    int i;
    // first, trying to find matgroup with such typeID and, if it is, add figure
    for (i = 0; i < matgroups.GetSize(); i++)
    {
        if (matgroups[i]->lpMaterial && (matgroups[i]->lpMaterial->GetTypeID() == nMaterialTypeID))
            return matgroups[i]->AddFigure(lpFigure);
    }
    // if no such material in the set, return with FALSE
    return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL STextureMaterialSet::RemoveFigure( CFigure* lpFigure )
// remove specified figure
BOOL STextureMaterialSet::RemoveFigure(CFigure *lpFigure)
{
    int i;

    for (i = 0; i < matgroups.GetSize(); i++)
    {
        if (matgroups[i]->RemoveFigure(lpFigure))
            return TRUE;
    }

    return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
// void STextureMaterialSet::RemoveAll()
// remove all material sets
void STextureMaterialSet::RemoveAll()
{
    int i;
    // delete all elements
    for (i = 0; i < matgroups.GetSize(); i++)
    {
        if (matgroups[i])
        {
            delete matgroups[i];
            matgroups[i] = NULL;
        }
    }
    // free array
    matgroups.RemoveAll();
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL STextureMaterialSet::IsEmpty() const
{
    for (int i = 0; i < matgroups.GetSize(); i++)
    {
        if (!(matgroups[i]->IsEmpty()))
            return FALSE;
    }
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
// struct SMaterialFigureSet functions                                              //
// this struct contain figure groups, associated with one material (and one texture)//
// ******************************************************************************** //
//////////////////////////////////////////////////////////////////////////////////////
SMaterialFigureSet::SMaterialFigureSet()
{
    lpMaterial = NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
SMaterialFigureSet::~SMaterialFigureSet()
{
    RemoveAll();
    if (lpMaterial)
    {
        lpMaterial->Release();
        lpMaterial = NULL;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL SMaterialFigureSet::AddFigure( CFigure* lpFigure )
// adds figure
BOOL SMaterialFigureSet::AddFigure(CFigure *lpFigure)
{
    int i;
    // first, trying to find such figue
    for (i = 0; i < figures.GetSize(); i++)
    {
        if (figures[i] == lpFigure)
            return FALSE;
    }
    // if no one, add it
    figures.Add(lpFigure);
    lpFigure->AddRef();

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL SMaterialFigureSet::RemoveFigure( CFigure* lpFigure )
// removes figure
BOOL SMaterialFigureSet::RemoveFigure(CFigure *lpFigure)
{
    int i;
    // trying to find such figue
    for (i = 0; i < figures.GetSize(); i++)
    {
        if (figures[i] == lpFigure)
        {
            figures.RemoveAt(i);
            lpFigure->Release();
            return TRUE;
        }
    }
    // if no one, return with FALSE
    return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL SMaterialFigureSet::RemoveAll()
// removes all figures
BOOL SMaterialFigureSet::RemoveAll()
{
    int i;
    // release figures
    for (i = 0; i < figures.GetSize(); i++)
    {
        if (figures[i])
        {
            figures[i]->Release();
            figures[i] = NULL;
        }
    }
    // free array
    figures.RemoveAll();

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
// 								     class CObject3DClientSpecific
// methods
// // this class handles client-specific 3d object data and work with it (ZB drawing)  //
// ******************************************************************************** //
//////////////////////////////////////////////////////////////////////////////////////
CObject3DClientSpecific::CObject3DClientSpecific()
{
    pos = -1;
    pShadowTexture = 0;
    pWoundsTexture = 0;
    shadowrect.SetRectEmpty();

    fpTime = 0;

#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
}
/////////////////////////////////////////////////////////////////////////////////////
CObject3DClientSpecific::~CObject3DClientSpecific()
{
    Destroy();
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CObject3DClientSpecific::Create()
{
    ownedAppearanceAttempted = false;
    ownedAppearanceRevision = 0;
    pos = -1;
    ResetAllFlags();
    // initialize extended textures (shadow and wounds)
    pShadowTexture = 0;
    pWoundsTexture = 0;
    //
    shadowrect.SetRectEmpty();
    memset(&bvShadowBV, 0, sizeof(bvShadowBV));
    //
    memset(sets, 0, sizeof(sets));
    //
    SetLastLightTime(0);
    //
    fScaleX = fScaleY = fScaleZ = 1.0f;
    //
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// void CObject3DClientSpecific::Destroy()
// destroy all internal data structures
void CObject3DClientSpecific::Destroy()
{
    ownedAppearanceAttempted = false;
    int i;
    // remove extended textures (shadow and wounds)
    pShadowTexture = 0;
    pWoundsTexture = 0;
    // remove sets (only 2 sets - primary and secondary)
    for (i = 0; i < 2; ++i)
    {
        if (sets[i])
            delete sets[i];
    }
    memset(sets, 0, sizeof(sets));
    // remove textures
    for (i = 0; i < textures.GetSize(); i++)
    {
        if (textures[i])
        {
            textures[i]->Release();
            textures[i] = 0;
        }
    }
    textures.RemoveAll();
    // remove materials
    for (i = 0; i < materials.GetSize(); i++)
    {
        if (materials[i])
        {
            materials[i]->Release();
            materials[i] = NULL;
        }
    }
    materials.RemoveAll();
    //
    if (lpObject && lpObject->GetAnimationSystem2())
        lpObject->GetAnimationSystem2()->ClearOwnedCurrentAnimationSelection();
    RegisterObject3D(NULL);
    SetLastLightTime(0);

    ResetAllFlags();
}
/////////////////////////////////////////////////////////////////////////////////////
// void CObject3DClientSpecific::AddFigure( CFigure* lpFigure, int nMaterialTypeID, int
// nTextureTypeID ) adding figure with specified material and texture typeID to the object perform
// it in four stages :
// 		1. scan for such texture and add one, if need
// 		2. scan for such material and add one, if need
// 		3. scan for such figure
// 		4. add figure to the specified place
BOOL CObject3DClientSpecific::AddFigure(CFigure *lpFigure)
{
    ETypeIDs tidMaterialTypeID = lpFigure->GetMaterialTypeID();
    ETypeIDs tidTextureTypeID = lpFigure->GetTextureTypeID();
    int i;
    // scan for such texture
    if (FindTexture(tidTextureTypeID) == NULL)
        AddNewTexture(tidTextureTypeID);
    // scan for such material
    if (FindMaterial(tidTextureTypeID, tidMaterialTypeID) == NULL)
        AddNewMaterial(tidTextureTypeID, tidMaterialTypeID);
    // add figure to the specified place
    for (i = 0; i < 2; ++i)
    {
        if (!sets[i])
            continue;
        if (sets[i]->lpTexture->GetTypeID() == tidTextureTypeID)
        {
            sets[i]->AddFigure(lpFigure, tidMaterialTypeID);
            InvalidateOwnedAppearance();
            return TRUE;
        }
    }
    return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CObject3DClientSpecific::RemoveFigure( CFigure* lpFigure )
// trying to remove specified figure from object (both from 'figures' and from appropriate set)
BOOL CObject3DClientSpecific::RemoveFigure(CFigure *lpFigure)
{
    int i;

    for (i = 0; i < 2; ++i)
    {
        if (!sets[i])
            continue;
        if (sets[i]->RemoveFigure(lpFigure))
        {
            InvalidateOwnedAppearance();
            return TRUE;
        }
    }

    return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
// CFigure* CObject3DClientSpecific::RemoveFigure( LPCSTR pszFigureName )
// remove figure from object by name
CFigure *CObject3DClientSpecific::RemoveFigure(LPCSTR pszFigureName)
{
    CFigure *lpFigure = FindFigure(pszFigureName);
    int i;
    //
    if (lpFigure == NULL)
        return NULL;
    //
    for (i = 0; i < 2; ++i)
    {
        if (!sets[i])
            continue;
        if (sets[i]->RemoveFigure(lpFigure))
        {
            InvalidateOwnedAppearance();
            return lpFigure;
        }
    }

    return NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
// CFigure* CObject3DClientSpecific::FindFigure( int nFigureTypeID )
// search for figure with specified type id
// returns NULL, if no such figure or pointer to one, if it is
CFigure *CObject3DClientSpecific::FindFigure(int nFigureTypeID)
{
    return ((CObject3D *)lpObject)->FindFigure(nFigureTypeID);
}
/////////////////////////////////////////////////////////////////////////////////////
// CFigure* CObject3D::FindFigure( LPCSTR pszFigureName )
// trying to find figure by name
CFigure *CObject3DClientSpecific::FindFigure(LPCSTR pszFigureName)
{
    return ((CObject3D *)lpObject)->FindFigure(pszFigureName);
}
/////////////////////////////////////////////////////////////////////////////////////
// CTexture* CObject3DClientSpecific::AddNewTexture( ETypeIDs tidTextureTypeID )
// add new texture with specified type id
CTexture *CObject3DClientSpecific::AddNewTexture(ETypeIDs tidTextureTypeID)
{
    STextureMaterialSet *pSet = 0;
    int i;
    // scan for such type texture
    for (i = 0; i < textures.GetSize(); i++)
    {
        if (textures[i]->GetTypeID() == tidTextureTypeID)
            return textures[i];
    }
    // add new with such type
    if (IsPrimaryTextureTID(tidTextureTypeID))
    {
        if (!sets[0])
        {
            pSet = dbgnew STextureMaterialSet();
            pSet->lpTexture = ((CWorldClient *)GetWorld())->CreateTexture(tidTextureTypeID);
            sets[0] = pSet;
            textures.Add(pSet->lpTexture);
            pSet->lpTexture->AddRef();
        }
        else
        {
            ReportWarning(
                "Adding new primary texture set with one exist\nFor object \"%s\" with ID=%d!\n",
                lpObject->GetMaskName(), lpObject->GetID());
            sets[0]->lpTexture->SetTypeID(tidTextureTypeID);
            pSet = sets[0];
        }
    }
    else
    {
        if (!sets[1])
        {
            pSet = dbgnew STextureMaterialSet();
            pSet->lpTexture = ((CWorldClient *)GetWorld())->CreateTexture(tidTextureTypeID);
            sets[1] = pSet;
            textures.Add(pSet->lpTexture);
            pSet->lpTexture->AddRef();
        }
        else
        {
            sets[1]->lpTexture->SetTypeID(tidTextureTypeID);
            pSet = sets[1];
        }
    }

    return pSet->lpTexture;
}
/////////////////////////////////////////////////////////////////////////////////////
// CMaterial* CObject3DClientSpecific::AddNewMaterial( ETypeIDs tidTextureTypeID, ETypeIDs
// tidMaterialTypeID ) add new material with specified type id for the specified texture
CMaterial *CObject3DClientSpecific::AddNewMaterial(ETypeIDs tidTextureTypeID,
                                                   ETypeIDs tidMaterialTypeID)
{
    int i, j;
    // add new with such type
    for (i = 0; i < 2; ++i)
    {
        if (!sets[i])
            continue;
        if (sets[i]->lpTexture && (sets[i]->lpTexture->GetTypeID() == tidTextureTypeID))
        {
            for (j = 0; j < sets[i]->matgroups.GetSize(); j++)
            {
                if (sets[i]->matgroups[j]->lpMaterial &&
                    (sets[i]->matgroups[j]->lpMaterial->GetTypeID() == tidMaterialTypeID))
                {
                    return sets[i]->matgroups[j]->lpMaterial;
                }
            }
            SMaterialFigureSet *pSet = dbgnew SMaterialFigureSet();
            pSet->lpMaterial = ((CWorldClient *)GetWorld())->CreateMaterial(tidMaterialTypeID);
            pSet->lpMaterial->AddRef();
            sets[i]->matgroups.Add(pSet);
            materials.Add(pSet->lpMaterial);
            pSet->lpMaterial->AddRef();

            return pSet->lpMaterial;
        }
    }

    return NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
// CTexture* CObject3DClientSpecific::FindTexture( ETypeIDs tidTextureTypeID )
// find texture with such type id. if no one, return NULL
CTexture *CObject3DClientSpecific::FindTexture(ETypeIDs tidTextureTypeID)
{
    int i;

    for (i = 0; i < textures.GetSize(); i++)
    {
        if (textures[i]->GetTypeID() == tidTextureTypeID)
            return textures[i];
    }

    return NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
// CMaterial* CObject3DClientSpecific::FindMaterial( ETypeIDs tidTextureTypeID, ETypeIDs
// tidMaterialTypeID ) find material with specified type id. if no one, return NULL
CMaterial *CObject3DClientSpecific::FindMaterial(ETypeIDs tidTextureTypeID,
                                                 ETypeIDs tidMaterialTypeID)
{
    int i, j;
    // find material with such type
    for (i = 0; i < 2; ++i)
    {
        if (!sets[i])
            continue;
        if (sets[i]->lpTexture && (sets[i]->lpTexture->GetTypeID() == tidTextureTypeID))
        {
            for (j = 0; j < sets[i]->matgroups.GetSize(); j++)
            {
                if (sets[i]->matgroups[j]->lpMaterial &&
                    (sets[i]->matgroups[j]->lpMaterial->GetTypeID() == tidMaterialTypeID))
                {
                    return sets[i]->matgroups[j]->lpMaterial;
                }
            }
        }
    }

    return NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
// CTexture* CObject3DClientSpecific::ChangeTexture( CTexture* lpNewTexture )
// change texture with typeID from lpNewTexture
CTexture *CObject3DClientSpecific::ChangeTexture(CTexture *lpNewTexture)
{
    CTexture *lpReturnTexture1 = NULL;
    CTexture *lpReturnTexture2 = NULL;
    int i;
    // check params
    if (lpNewTexture == NULL)
        return NULL;
    //
    for (i = 0; i < textures.GetSize(); i++)
    {
        if (textures[i]->GetTypeID() == lpNewTexture->GetTypeID())
        {
            // change texture in textures list
            lpReturnTexture1 = textures[i];
            textures[i] = lpNewTexture;
            textures[i]->AddRef();
            lpReturnTexture1->Release();
            break;
        }
    }
    // change texture in texture/material set
    for (i = 0; i < 2; ++i)
    {
        if (!sets[i])
            continue;
        if (sets[i]->lpTexture->GetTypeID() == lpNewTexture->GetTypeID())
        {
            lpReturnTexture2 = sets[i]->lpTexture;
            sets[i]->lpTexture = lpNewTexture;
            break;
        }
    }

#ifdef _DEBUG
    if (lpReturnTexture1 != lpReturnTexture2)
    {
        ReportError("CObject3D::ChangeTexture.\n Ambiguity in textures.\n");
        return NULL;
    }
#endif // _DEBUG

    if (lpReturnTexture1)
        InvalidateOwnedAppearance();
    return lpReturnTexture1;
}
/////////////////////////////////////////////////////////////////////////////////////
// CMaterial* CObject3DClientSpecific::GetFirstMaterial()
// get first material from used in this object
CMaterial *CObject3DClientSpecific::GetFirstMaterial()
{
    if (materials.GetSize() > 0)
    {
        pos = 0;
        return materials[pos];
    }
    else
    {
        pos = -1;
        return NULL;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// CMaterial* CObject3DClientSpecific::GetNextMaterial()
// get next material from used in this object
CMaterial *CObject3DClientSpecific::GetNextMaterial()
{
    pos++;
    if (materials.GetSize() > pos)
        return materials[pos];
    else
        return NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
// CTexture* CObject3DClientSpecific::GetFirstTexture()
// get first texture of this object
CTexture *CObject3DClientSpecific::GetFirstTexture()
{
    if (textures.GetSize() > 0)
    {
        pos = 0;
        return textures[pos];
    }
    else
    {
        pos = -1;
        return NULL;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// CTexture* CObject3DClientSpecific::GetNextTexture()
// get next texture of this object
CTexture *CObject3DClientSpecific::GetNextTexture()
{
    pos++;
    if (textures.GetSize() > pos)
        return textures[pos];
    else
        return NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
void CObject3DClientSpecific::RecalcScreenRect()
{
    CObject3D *pObject = (CObject3D *)lpObject;
    //
    rcRect.SetRectEmpty();
    for (int i = 0; i < pObject->figures.GetSize(); i++)
    {
        if (!pObject->figures[i]->CheckFlag(FIG_VISIBLE))
            continue;
        CRect rc = ((CFigureClientSpecific *)pObject->figures[i]->GetFigureSpecific())
                       ->Get2DClipRect();

        rc.NormalizeRect();
        rcRect.UnionRect(rc, rcRect);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// c56 consumes owned copies only. Drop a previously published value as soon as a host owner changes.
void CObject3DClientSpecific::InvalidateOwnedAppearance() noexcept
{
    const bool hadAttempt = ownedAppearanceAttempted;
    ownedAppearanceAttempted = false;
    if (!hadAttempt)
        return;
    const auto resource = OwnedRender::E1Scene().Resource(lpObject);
    if (resource.serial)
        OwnedRender::E1Scene().Scene().visual_reject(resource,
            owned::RetainedVisualStatus::unsupported_appearance,
            "appearance.source_changed", "host.object_appearance");
}
bool CObject3DClientSpecific::PublishOwnedAppearance() noexcept try
{
    CObject3D* object = static_cast<CObject3D*>(lpObject);
    auto* animation = object ? object->GetAnimationSystem2() : nullptr;
    IGeometry* geometry = object ? object->GetGeometry() : nullptr;
    const auto resource = OwnedRender::E1Scene().Resource(lpObject);
    auto& scene = OwnedRender::E1Scene().Scene();
    auto reject = [&](const char* predicate) {
        if (resource.serial)
            scene.visual_reject(resource, owned::RetainedVisualStatus::unsupported_appearance,
                                predicate, "host.object_appearance");
        return false;
    };
    if (!resource.serial || !animation || !geometry || object->GetNumFigures() <= 0)
        return reject("appearance.sources_ready");

    auto skeleton = animation->GetOwnedSkeleton();
    if (!skeleton || skeleton->links.empty())
        return reject("appearance.selected_skeleton");
    auto assembly = std::make_shared<owned::VisualAssemblyAsset>();
    assembly->skeleton = skeleton;
    assembly->revision = ++ownedAppearanceRevision;
    if (!assembly->revision)
        assembly->revision = ++ownedAppearanceRevision;
    const CPlot3D center = object->GetCenterShift();
    assembly->center_shift = {{center.x, center.y, center.z}};
    const SComplection& complection = object->GetComplection();
    assembly->shape = {{complection.strength, complection.grace, complection.tall}};
    GetScaleCoeffs(assembly->scale[0], assembly->scale[1], assembly->scale[2]);

    for (int index = 0; index < object->GetNumFigures(); ++index)
    {
        CFigure* figure = object->GetFigure(index);
        if (!figure || !figure->GetName() || !*figure->GetName())
            return reject("appearance.figure_owner");
        const std::string partName(figure->GetName());
        auto figureAsset = geometry->GetOwnedFigure(partName);
        if (!figureAsset)
            return reject("appearance.selected_fig");
        auto node = std::find_if(skeleton->links.begin(), skeleton->links.end(),
            [&](const owned::ModelLink& link) { return link.child == partName; });
        if (node == skeleton->links.end())
            return reject("appearance.figure_node");

        CTexture* texture = nullptr;
        CMaterial* material = nullptr;
        for (int setIndex = 0; setIndex < 2 && !material; ++setIndex)
        {
            STextureMaterialSet* set = sets[setIndex];
            if (!set || !set->lpTexture ||
                set->lpTexture->GetTypeID() != figure->GetTextureTypeID())
                continue;
            for (int groupIndex = 0; groupIndex < set->matgroups.GetSize() && !material;
                 ++groupIndex)
            {
                SMaterialFigureSet* group = set->matgroups[groupIndex];
                if (!group || !group->lpMaterial ||
                    group->lpMaterial->GetTypeID() != figure->GetMaterialTypeID())
                    continue;
                for (int figureIndex = 0; figureIndex < group->figures.GetSize(); ++figureIndex)
                    if (group->figures[figureIndex] == figure)
                    {
                        texture = set->lpTexture;
                        material = group->lpMaterial;
                        break;
                    }
            }
        }
        if (!texture || !material || !texture->GetSurface())
            return reject("appearance.selected_material_texture");
        auto surface = OwnedRender::E1SurfaceValue(texture->GetSurface()->OwnedSurfaceId());
        if (!surface)
            return reject("appearance.completed_surface");

        const D3DMATERIAL7& sourceMaterial = material->GetMaterial();
        auto materialAsset = std::make_shared<owned::MaterialCpuAsset>();
        materialAsset->source = owned::MaterialValueSource::completed_native;
        materialAsset->diffuse = {{sourceMaterial.diffuse.r, sourceMaterial.diffuse.g,
            sourceMaterial.diffuse.b, sourceMaterial.diffuse.a}};
        materialAsset->ambient = {{sourceMaterial.ambient.r, sourceMaterial.ambient.g,
            sourceMaterial.ambient.b, sourceMaterial.ambient.a}};
        materialAsset->specular = {{sourceMaterial.specular.r, sourceMaterial.specular.g,
            sourceMaterial.specular.b, sourceMaterial.specular.a}};
        materialAsset->emissive = {{sourceMaterial.emissive.r, sourceMaterial.emissive.g,
            sourceMaterial.emissive.b, sourceMaterial.emissive.a}};
        materialAsset->power = sourceMaterial.power;
        D3DCOLORVALUE recentEmissive{}, oldestEmissive{};
        if (material->GetStoredEmissive(recentEmissive, oldestEmissive))
        {
            materialAsset->has_previous_highlight_emissive = true;
            materialAsset->previous_highlight_emissive = {{recentEmissive.r,
                recentEmissive.g, recentEmissive.b, recentEmissive.a}};
            materialAsset->has_oldest_highlight_emissive = true;
            materialAsset->oldest_highlight_emissive = {{oldestEmissive.r,
                oldestEmissive.g, oldestEmissive.b, oldestEmissive.a}};
        }

        owned::VisualPartAsset part;
        part.name = partName;
        part.geometry = std::move(figureAsset);
        part.skeleton_node = static_cast<std::uint32_t>(node - skeleton->links.begin());
        part.surface = std::move(surface);
        part.texture_source_name = texture->GetName() ? texture->GetName() : "";
        part.material = std::move(materialAsset);
        part.texture_type = static_cast<std::uint32_t>(texture->GetTypeID());
        part.material_type = static_cast<std::uint32_t>(material->GetTypeID());
        part.material_flags = material->CheckFlag(MAT_USE_SPECULAR) ? MAT_USE_SPECULAR : 0;
        part.figure_flags = figure->CheckFlag(FIG_USE_DEFAULT_COLORS) ? FIG_USE_DEFAULT_COLORS : 0;
        const DWORD textureFlags[] = {TEX_ALPHA0, TEX_ALPHA1, TEX_ALPHA4, TEX_ALPHA8};
        for (DWORD textureFlag : textureFlags)
            if (texture->CheckFlag(textureFlag)) part.texture_flags |= textureFlag;
        part.diffuse = figure->GetDefaultColor();
        part.specular = figure->GetDefaultSpecular();
        // Empty FIG parts keep their names and skeleton nodes, but draw nothing.
        part.explicitly_hidden = figure->CheckFlag(FIG_HIDDEN) || part.geometry->indices.empty();
        assembly->parts.push_back(std::move(part));
    }
    // The retained scene claims this object's mesh (game.exe bridge:
    // visual_publish_appearance); the owned-visual frame then draws it.
    if (!scene.visual_declare_mesh(resource, object->GetNumFigures() > 0))
        return reject("appearance.declaration_publish");
    if (!scene.visual_publish_assembly(resource, std::move(assembly)))
        return reject("appearance.publish_current_resource");
    return true;
}
catch (...)
{
    const auto resource = OwnedRender::E1Scene().Resource(lpObject);
    if (resource.serial)
        OwnedRender::E1Scene().Scene().visual_reject(resource,
            owned::RetainedVisualStatus::unsupported_appearance,
            "appearance.host_snapshot_exception", "host.object_appearance");
    return false;
}
bool CObject3DClientSpecific::PublishOwnedPose() noexcept try
{
    CorePerf::Scope coreTiming(CorePerf::PosePublish);
    static OwnedRender::E1PerfCounter counter{"host_pose_publish"};
    OwnedRender::E1PerfScope timing(counter);
    CObject3D* object = static_cast<CObject3D*>(lpObject);
    const auto resource = OwnedRender::E1Scene().Resource(lpObject);
    auto& hostScene = OwnedRender::E1Scene();
    auto& scene = hostScene.Scene();
    auto reject = [&](const char* step) noexcept {
        OwnedRender::E1ReportOwnedPoseFailure(object ? object->GetID() : -1, step);
        if (resource.serial)
            scene.visual_reject(resource, owned::RetainedVisualStatus::unsupported_selector,
                step, "host.object_pose");
        return false;
    };
    if (!resource.serial) return reject("object_pose.resource");
    if (!object) return reject("object_pose.object");
    CAnimationSystem2* animation = object->GetAnimationSystem2();
    if (!animation) return reject("object_pose.animation_system");

    const auto assembly = scene.visual_assembly(resource);
    if (!assembly) return reject("object_pose.assembly");
    OwnedRender::E1AnimationSelection selected;
    const char* selectionFailure = nullptr;
    if (!animation->GetOwnedCurrentAnimationSelection(selected, &selectionFailure) ||
        !selected.valid)
        return reject(selectionFailure ? selectionFailure : "selection.result_invalid");
    if (!selected.visual.neutral && !hostScene.BindDatabase(resource, selected))
        return reject("object_pose.animation_database");

    const CPlot3D position = object->GetCP();
    const CQuaternion& orientation = object->GetOrientation();
    const SComplection& shape = object->GetComplection();
    owned::VisualObjectPose pose;
    pose.position = {{position.x, position.y, position.z}};
    pose.object_flags = object->GetFlags();
#ifndef _A4
    // Completed host poses use the object animation path. Encode the game's
    // unit detection policy in the render copy, not in the live object's flags.
    if (object->WhoAmI() == HPC_UNIT) {
        auto* client = IWorldClient.GetPlayer();
        const auto* player = client ? client->GetPlayer() : nullptr;
        const bool drawUndetected = !theSettings.nDrawVisibleOnly ||
            (player && player->curInterface != IFACE_MISSION);
        if (!drawUndetected && !object->CheckFlag(UNIT_VISIBLE))
            pose.object_flags |= OBJ_HIDDEN;
    }
#endif
    // Respect host source policy without a second shadow renderer.
    if (!IsNeedShadow()) pose.object_flags |= OBJ_NOSHADOW;
    float qw = 1.0f, qx = 0.0f, qy = 0.0f, qz = 0.0f;
    orientation.GetComponents(qw, qx, qy, qz);
    pose.quaternion = {{qw, qx, qy, qz}};
    pose.shape = {{shape.strength, shape.grace, shape.tall}};
    pose.selection = selected.visual;
    pose.phase = selected.visual.neutral ? 0.0 : animation->GetTime();
    pose.source_generation = selected.source_generation;
    pose.appearance_revision = assembly->revision;
    if (owned::bridge_change_driven_poses_enabled() && scene.visual_object_pose_current(resource, pose))
        return true;
    if (!hostScene.PublishObjectPose(resource, pose))
        return reject("object_pose.publish");
    CorePerf::VisualCount(2);
    return true;
}
catch (...) {
    OwnedRender::E1ReportOwnedPoseFailure(lpObject ? lpObject->GetID() : -1,
        "object_pose.exception");
    return false;
}
// BOOL CObject3DClientSpecific::Draw()
// draw
bool CObject3DClientSpecific::PrepareOwnedResources()
{
    lpObject->FinalDataLoading();
    // Only immutable shared providers are replayable here; never use a base
    // canonical alias to overwrite current private mouth/wound state.
    for(int i=0;i<2;++i) {
        CTexture* texture=sets[i]?sets[i]->lpTexture:nullptr;
        const bool shared=texture &&
            ((texture->GetTypeID()==TTID_UNIT_MAIN && ModPrimaryAtlas::Canonical(texture->GetName())) ||
             (texture->GetTypeID()==TTID_UNIT_WEAPON && ModSecondaryAtlas::Canonical(texture->GetName())));
        if(shared && !texture->GetSurface()) {
            auto* manager=static_cast<CTextureManager*>(static_cast<CWorldClient*>(GetWorld())->GetTextureManager());
            texture->ResetFlag(TEX_LOADED);
            if(!manager->LoadTexture(texture) || !texture->GetSurface())return FALSE;
        }
    }
    // A retained wound overlay must be restored by its unit owner before bind.
    if(pWoundsTexture && !pWoundsTexture->GetSurface()) {
        if(lpObject && lpObject->WhoAmI()==HPC_UNIT) {
            auto* unit=static_cast<CUnit*>(lpObject);
            auto* specific=static_cast<CUnitClientSpecific*>(unit->GetUnitSpecific());
            if(specific)specific->EnsureWoundsTextureReady();
        }
    }
    if(pWoundsTexture && pWoundsTexture->GetSurface())
        pWoundsTexture->GetSurface()->EnsureOwnedSurfacePublished();
    if (!ownedAppearanceAttempted)
    {
        ownedAppearanceAttempted = true;
        PublishOwnedAppearance();
    }
    return true;
}
bool CObject3DClientSpecific::PrepareOwnedVisual(CRenderer *pRend, bool visibleOnScreen)
{
    CorePerf::Partition coreParts(CorePerf::VisualResources);
    if (lpObject->CheckFlag(OBJ_HIDDEN)) {
        // Keep an already published caster from surviving a hide transition.
        const auto resource = OwnedRender::E1Scene().Resource(lpObject);
        if (resource.serial) PublishOwnedPose();
        return true;
    }
    bool resourcesReady = PrepareOwnedResources();
    if(resourcesReady && lpObject->WhoAmI()==HPC_UNIT && sets[0] &&
       sets[0]->lpTexture && sets[0]->lpTexture->GetSurface()) {
        const auto secondary=pWoundsTexture && pWoundsTexture->GetSurface()
            ? OwnedRender::E1SurfaceValue(pWoundsTexture->GetSurface()->OwnedSurfaceId()) : nullptr;
        OwnedRender::E1PublishSecondaryVisual(lpObject->GetID(),
            sets[0]->lpTexture->GetSurface()->OwnedSurfaceId(),secondary);
    }
    coreParts.Stage(CorePerf::Skeleton);
    if (lpObject->GetAnimationSystem2())
        lpObject->GetAnimationSystem2()->RecalcSkeletonWithCurrentTime();
    coreParts.Stage(CorePerf::VisualPose);
    const bool published = resourcesReady && PublishOwnedPose();
    if (!published) {
        const auto resource = OwnedRender::E1Scene().Resource(lpObject);
        if (resource.serial)
            OwnedRender::E1Scene().Scene().visual_reject(resource,
                owned::RetainedVisualStatus::unsupported_selector,
                "object_pose.current_draw_preparation", "host.object_pose");
    }
    if (!visibleOnScreen) return published;
    const auto type = lpObject->WhoAmI();
    auto* mapObject = dynamic_cast<CMapObject*>(lpObject);
    if (published && owned::bridge_skip_scenery_draw_walk_enabled() &&
        (type == HPC_BUILDING || type == HPC_TREE ||
         (type != HPC_UNIT && type != HPC_LEVER && type != HPC_TORCH && type != HPC_TRAP && lpObject->CheckFlag(OBJ_STATIC) && mapObject && !mapObject->IsScientific())) &&
        (!lpObject->GetAnimationSystem2() || !lpObject->GetAnimationSystem2()->GetCurrentAnimation()))
        return published;

    coreParts.Stage(CorePerf::Picking);
    // Picking uses current host geometry, independently of GPU acceptance.
    const bool selectable = lpObject->WhoAmI() == HPC_UNIT || lpObject->WhoAmI() == HPC_LEVER;
    rcRect.SetRectEmpty();
    if (selectable) lpObject->rcScreenRect.SetRectEmpty();
    CDXGeometryProcessor *gpu = pRend->GetGPU();
    lpObject->CreateCurrentPlacement();
    SHMatrix matrix;
    lpObject->GetPlacement(matrix);
    gpu->PushMainMatrix(matrix);
    gpu->PushMatrix(matrix, MAT_STACK_WORLDSPACE);
    Identity(matrix);
    const bool scaled = GetScaleMatrix(matrix);
    if (scaled) {
        gpu->PushMainMatrix(matrix);
        gpu->PushMatrix(matrix, MAT_STACK_WORLDSPACE);
    }
    bool anyVisible = false;
    auto* object = static_cast<CObject3D*>(lpObject);
    for (int index = 0; index < object->GetNumFigures(); ++index) {
        CFigure* figure = object->GetFigure(index);
        if (!figure || fabs2(figure->GetCP()) > 1e9f) continue;
        figure->CreateCurrentPlacement();
        figure->GetPlacement(matrix);
        gpu->PushMainMatrix(matrix);
        gpu->PushMatrix(matrix, MAT_STACK_WORLDSPACE);
        CRect rect;
        static_cast<CFigureClientSpecific*>(figure->GetFigureSpecific())->PrepareOwnedPicking(
            pRend, selectable ? &rect : nullptr);
        anyVisible |= figure->CheckFlag(FIG_VISIBLE) != 0;
        if (selectable && !rect.IsRectEmpty()) rcRect |= rect;
        gpu->PopMatrix(MAT_STACK_WORLDSPACE);
        gpu->PopMainMatrix();
    }
    if (scaled) {
        gpu->PopMatrix(MAT_STACK_WORLDSPACE);
        gpu->PopMainMatrix();
    }
    gpu->PopMatrix(MAT_STACK_WORLDSPACE);
    gpu->PopMainMatrix();
    if (selectable) lpObject->rcScreenRect = rcRect;
    if (anyVisible) lpObject->SetFlag(OBJ_VISIBLE);
    else lpObject->ResetFlag(OBJ_VISIBLE);
    return published;
}

BOOL CObject3DClientSpecific::Draw(CRenderer *pRend, CRect *pRect)
{
    if (owned::model_draw_is_preview()) {
        if (lpObject->CheckFlag(OBJ_HIDDEN)) return TRUE;
        if (!PrepareOwnedResources()) return FALSE;
        if (lpObject->GetAnimationSystem2())
            lpObject->GetAnimationSystem2()->RecalcSkeletonWithCurrentTime();
        return DrawModelParts(pRend, nullptr);
    }
    // Publication and picking precede this draw's owned scene commit.
    if (owned::model_draw_uses_retained_world(pRend->GetD3D()->IsOwnedWorldActive(),
            lpObject->GetID() > 0, owned::model_draw_domain)) {
        OwnedRender::E1ReportOwnedVisual(lpObject->GetID());
        if (pRect) *pRect = lpObject->rcScreenRect;
        return TRUE;
    }
    if (lpObject->CheckFlag(OBJ_HIDDEN))
        return TRUE;
    if (!PrepareOwnedResources()) return FALSE;
    if (!lpObject->CheckFlag(OBJ_STATIC)) lpObject->ResetFlag(OBJ_LIGHTED);
    if (pRect) { rcRect.SetRectEmpty(); lpObject->rcScreenRect.SetRectEmpty(); }
    if (lpObject->GetAnimationSystem2())
        lpObject->GetAnimationSystem2()->RecalcSkeletonWithCurrentTime();
    PublishOwnedPose();
    return DrawModelParts(pRend, pRect);
}

BOOL CObject3DClientSpecific::DrawModelParts(CRenderer *pRend, CRect *pRect)
{
    const bool preview = owned::model_draw_is_preview();
    const DWORD previousLighting = lpObject->CheckFlag(OBJ_LIGHTED);
    CDXGeometryProcessor *pGPU = pRend->GetGPU();
    CPlot3D objectCP = (lpObject)->GetCP();
    SHMatrix matrix;
    int nFigureClipFlag = 0;
    bool bHaveScale = false;
    //
    // Read membership only. The same c74 producer owns ordering/admission and
    // texture-set transitions for retail plans and source object previews.
    struct SourcePart { CFigure* figure; CMaterial* material; unsigned set; };
    std::vector<SourcePart> parts;
    for (unsigned set = 0; set < 2; ++set) {
        if (!sets[set]) continue;
        for (int group = 0; group < sets[set]->matgroups.GetSize(); ++group) {
            const auto* source = sets[set]->matgroups[group];
            for (int part = 0; part < source->figures.GetSize(); ++part)
                parts.push_back({source->figures[part], source->lpMaterial, set});
        }
    }
    //
    if (!lpObject->CheckFlag(OBJ_STATIC))
        lpObject->ResetFlag(OBJ_LIGHTED);
    //
    if (pRect != 0)
    {
        rcRect.SetRectEmpty();
        lpObject->rcScreenRect.SetRectEmpty();
    }
    //
    // collect all light sources to lit this object
    static CSimplyArray<CLight *, CLight *> lights;
    CManagerIterator mi;
    CLight *pLight = static_cast<CLight *>(mi.GetFirst(theWorld.GetLightManager()));
    lights.SetSize(16); // ARRAY BOUNDS CHECK
    lights.SetSize(0);  // ARRAY BOUNDS CHECK
    while (pLight)
    {
        if ((pRend->GetSunlight() != pLight) &&
            fabs2(lpObject->GetCP() - pLight->GetCP()) <= square(pLight->GetRange() + 10.0f) &&
            !pLight->CheckFlag(LIT_INACTIVE))
        {
            if (pLight->CheckFlag(LIT_DYNAMIC))
                lpObject->ResetFlag(OBJ_LIGHTED);
            lights.Add(pLight);
        }
        pLight = static_cast<CLight *>(mi.GetNext());
    }
    // object placement matrix
    lpObject->CreateCurrentPlacement();
    lpObject->GetPlacement(matrix);
    // DX11 relighting is unconditional, but must not invalidate a cached static shadow.
    const DWORD shadowLightingWasValid = lpObject->CheckFlag(OBJ_LIGHTED);
    lpObject->ResetFlag(OBJ_LIGHTED);
    pGPU->PushMainMatrix(matrix);
    if (!lpObject->CheckFlag(OBJ_LIGHTED)) // push matrix for lighting
        pGPU->PushMatrix(matrix, MAT_STACK_WORLDSPACE);
    // object scale matrix
    Identity(matrix);

    // TODO WORLD MATRIX SCALE
    bHaveScale = GetScaleMatrix(matrix);
    if (bHaveScale)
    {
        pGPU->PushMainMatrix(matrix);
        if (!lpObject->CheckFlag(OBJ_LIGHTED)) // push matrix for lighting
            pGPU->PushMatrix(matrix, MAT_STACK_WORLDSPACE);
    }
    // get fog value from terrain
    BYTE cFog = 255; // default fog - completelly unfogged
    if (!lpObject->CheckFlag(OBJ_USE_DEFAULT_FOG))
        cFog = ((CWorldClient *)GetWorld())->GetTerrain()->GetFogAlpha(objectCP);

    // draw object
    DWORD dwAllFiguresLighted = FIG_LIGHTED;
    DWORD dwObjectWasLighted = shadowLightingWasValid;
    owned::ModelPartResult result;
    const bool drawn = owned::produce_model_parts(parts.size(),
        [&](std::size_t index, owned::ModelPartFacts& facts) {
            const auto& part = parts[index];
            facts.texture_set = part.set;
            facts.inactive = fabs2(part.figure->GetCP()) > 1e9f;
            facts.hidden = part.figure->CheckFlag(FIG_HIDDEN) != 0;
            facts.visible = part.figure->CheckFlag(FIG_VISIBLE);
            return true;
        },
        [&](unsigned set) {
            return pRend->BindTexture(0, sets[set]->lpTexture) &&
                pRend->BindTexture(1, set == 0 ? pWoundsTexture : nullptr);
        },
        [&](std::size_t index, std::uint32_t& visible) {
            const auto& part = parts[index];
            CFigure* pFigure = part.figure;
            if (!preview && !lpObject->CheckFlag(OBJ_LIGHTED)) pFigure->ResetFlag(FIG_LIGHTED);
            pFigure->CreateCurrentPlacement();
            pFigure->GetPlacement(matrix);
            pGPU->PushMainMatrix(matrix);
            pGPU->PushMatrix(matrix, MAT_STACK_WORLDSPACE);
            pRend->SetProjectionMatrix();
            CRect rect(0, 0, 0, 0);
            const BOOL ok = pRend->DrawFigure(pFigure, lights, part.material, cFog,
                pRect ? &rect : nullptr,
                lpObject->WhoAmI() == HPC_UNIT ? sets[part.set]->lpTexture : nullptr, nullptr);
            visible = pFigure->CheckFlag(FIG_VISIBLE);
            if (pRect && !rect.IsRectEmpty() && visible) rcRect |= rect;
            pGPU->PopMatrix(MAT_STACK_WORLDSPACE);
            pGPU->PopMainMatrix();
            dwAllFiguresLighted &= pFigure->CheckFlag(FIG_LIGHTED);
            return ok != FALSE;
        }, result);
    nFigureClipFlag = result.visible;
    // pop scale matrix
    if (bHaveScale)
    {
        if (!lpObject->CheckFlag(OBJ_LIGHTED)) // pop matrix for lighting
            pGPU->PopMatrix(MAT_STACK_WORLDSPACE);
        pGPU->PopMainMatrix();
    }
    // pop object placement matrix
    if (!lpObject->CheckFlag(OBJ_LIGHTED)) // pop matrix for lighting
        pGPU->PopMatrix(MAT_STACK_WORLDSPACE);
    pGPU->PopMainMatrix();
    //
    if (preview) {
        if (previousLighting) lpObject->SetFlag(OBJ_LIGHTED);
        else lpObject->ResetFlag(OBJ_LIGHTED);
    }
    else if (drawn && dwAllFiguresLighted) // treat this object as lighted only if all figures was lighted!
    {
        lpObject->SetFlag(OBJ_LIGHTED);
        if (!dwObjectWasLighted)
        {
            SetLastLightTime(timeGetTime());
            SetFlag(OCS_RESHADOW);
        }
    }
    else
        lpObject->ResetFlag(OBJ_LIGHTED);
    //
    if (pRect != 0)
        *pRect = lpObject->rcScreenRect = rcRect;
    //
    if (owned::model_draw_writes_pick_state(owned::model_draw_domain)) {
        if (nFigureClipFlag) lpObject->SetFlag(OBJ_VISIBLE);
        else lpObject->ResetFlag(OBJ_VISIBLE);
    }

    // Always unwind object/part matrices and the wound binding on rejection.
    pRend->BindTexture(1, (CDXGISurface *)0);
    return drawn ? TRUE : ReportError("CObject3DClientSpecific::Draw.\n Owned model draw failed.\n");
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CObject3DClientSpecific::CanSelectRoughly(const CPoint &point) const
{
    if (!lpObject->CheckFlag(OBJ_VISIBLE))
        return FALSE;
    return lpObject->rcScreenRect.PtInRect(point);
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CObject3DClientSpecific::IsCanSelect( const CPoint& point )
// test for selection
BOOL CObject3DClientSpecific::IsCanSelect(const CPoint &point)
{
    CObject3D *pObject = (CObject3D *)lpObject;
    int i;

    if (!lpObject->CheckFlag(OBJ_VISIBLE))
        return FALSE;

    for (i = 0; i < pObject->figures.GetSize(); i++)
    {
        if (pObject->figures[i]->CheckFlag(FIG_VISIBLE) &&
            static_cast<CFigureClientSpecific *>(pObject->figures[i]->GetFigureSpecific())
                ->Get2DClipRect()
                .PtInRect(point))
            return TRUE;
    }

    return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CObject3DClientSpecific::IsCanSelect(const CRect &rect)
{
    CObject3D *pObject = (CObject3D *)lpObject;
    CRect rcIntersect;
    int i;

    if (!lpObject->CheckFlag(OBJ_VISIBLE))
        return FALSE;

    for (i = 0; i < pObject->figures.GetSize(); i++)
    {
        if (pObject->figures[i]->CheckFlag(FIG_VISIBLE))
        {
            rcIntersect.IntersectRect(static_cast<CFigureClientSpecific *>(
                                          pObject->figures[i]->GetFigureSpecific())
                                          ->Get2DClipRect(),
                                      rect);
            if (!rcIntersect.IsRectEmpty())
                return TRUE;
        }
    }

    return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CObject3DClientSpecific::RebindPrimaryTexture(LPCSTR name)
{
    CTexture* old=GetPrimaryTexture();
    if(!old || !name || !*name || old->GetTypeID()!=TTID_UNIT_MAIN)return FALSE;
    auto* manager=static_cast<CTextureManager*>(static_cast<CWorldClient*>(GetWorld())->GetTextureManager());
    CString nextName(name),previousName(old->GetName());
    const bool named=ModPrimaryAtlas::Canonical(name)||ModPrimaryAtlas::Private(name);
    bool created=false;
    const bool bound=ModSecondaryAtlas::Rebind(old,
        [&]() -> CTexture* {
            CTexture* provider=named?manager->FindTexture(old->GetTypeID(),name):nullptr;
            if(!provider){provider=manager->CreateTexture(old->GetTypeID());created=true;}
            return provider;
        },
        [&](CTexture* provider) {
            if(created)provider->SetName(name);
            if(!provider->GetSurface() && !manager->LoadTexture(provider))return false;
            return provider->GetSurface()!=nullptr;
        },
        [&](CTexture* provider) {
            if(GetPrimaryTexture()!=old || provider->GetTypeID()!=old->GetTypeID())return false;
            int matches=0;
            for(int i=0;i<textures.GetSize();++i)if(textures[i]->GetTypeID()==old->GetTypeID()){
                if(textures[i]!=old)return false;++matches;
            }
            return matches==1 && sets[0] && sets[0]->lpTexture==old &&
                (!sets[1] || !sets[1]->lpTexture || sets[1]->lpTexture->GetTypeID()!=old->GetTypeID());
        },
        [&](CTexture* provider){ChangeTexture(provider);},
        [&](){return GetPrimaryTexture();},
        [](CTexture* provider){provider->AddRef();},
        [](CTexture* provider){provider->Release();});
    // Draw binds the current primary and separate wound overlay on every draw;
    // there is no captured primary shader to rebuild or mutate here.
    if(bound){
        szPrimaryTextureName=nextName;
        if(previousName!=nextName)ModPrimaryAtlas::State().Remove(previousName);
    }
    return bound?TRUE:FALSE;
}
BOOL CObject3DClientSpecific::EnsurePrivatePrimaryAtlas(DWORD unitID)
{
    auto* manager=static_cast<CTextureManager*>(static_cast<CWorldClient*>(GetWorld())->GetTextureManager());
    if(!manager->SharePrimaryCharacterAtlas())return TRUE;
    CTexture* current=GetPrimaryTexture();if(!current)return FALSE;
    if(current->GetSurface() && ModPrimaryAtlas::Canonical(current->GetName()) && manager->ComposeTexturesInMemory()) {
        const std::string canonical(current->GetName());
        if(!lpObject || lpObject->WhoAmI()!=HPC_UNIT || lpObject->GetID()!=unitID)return FALSE;
        auto* unit=static_cast<CUnit*>(lpObject);
        auto* specific=static_cast<CUnitClientSpecific*>(unit->GetUnitSpecific());
        if(!specific || !ModTextureMemory::EnsureCpu(ModTextureMemory::State(),canonical,[&](std::string& name){
            CString rebuilt;const BOOL result=specific->AssemblePrimaryTexture(rebuilt);name=(LPCSTR)rebuilt;return result!=FALSE;
        }))return FALSE;
    }
    return ModPrimaryAtlas::EnsurePrivate(ModPrimaryAtlas::State(),unitID,current->GetName(),
        [&](){return current->GetSurface()!=nullptr;},
        [&](const char* name){return RebindPrimaryTexture(name)!=FALSE;})?TRUE:FALSE;
}
BOOL CObject3DClientSpecific::RebindSecondaryTexture(LPCSTR name)
{
    CTexture* old=GetSecondaryTexture();
    if(!old || !name || !*name || old->GetTypeID()!=TTID_UNIT_WEAPON)return FALSE;
    auto* manager=static_cast<CTextureManager*>(static_cast<CWorldClient*>(GetWorld())->GetTextureManager());
    // Prepare metadata before loading/committing. Failed loads retain the old name.
    CString nextName(name);
    const bool canonical=ModSecondaryAtlas::Canonical(name);
    bool created=false;
    const bool bound=ModSecondaryAtlas::Rebind(old,
        [&]() -> CTexture* {
            CTexture* provider=canonical?manager->FindTexture(old->GetTypeID(),name):nullptr;
            // Collision/capacity fallback keeps baseline disk names/composition,
            // but uses a fresh private provider instead of renaming a shared one.
            if(!provider){provider=manager->CreateTexture(old->GetTypeID());created=true;}
            return provider;
        },
        [&](CTexture* provider) {
            if(created)provider->SetName(name);
            if(!provider->GetSurface() && !manager->LoadTexture(provider))return false;
            return provider->GetSurface()!=nullptr; // LoadTexture can report TRUE without a surface.
        },
        [&](CTexture* provider) {
            if(GetSecondaryTexture()!=old || provider->GetTypeID()!=old->GetTypeID())return false;
            int matches=0;
            for(int i=0;i<textures.GetSize();++i)if(textures[i]->GetTypeID()==old->GetTypeID()){
                if(textures[i]!=old)return false;++matches;
            }
            return matches==1 && sets[1] && sets[1]->lpTexture==old &&
                (!sets[0] || !sets[0]->lpTexture || sets[0]->lpTexture->GetTypeID()!=old->GetTypeID());
        },
        [&](CTexture* provider){ChangeTexture(provider);},
        [&](){return GetSecondaryTexture();},
        [](CTexture* provider){provider->AddRef();},
        [](CTexture* provider){provider->Release();});
    if(bound)szSecondaryTextureName=nextName;
    return bound?TRUE:FALSE;
}
// BOOL CObject3DClientSpecific::LoadTextures( LPCSTR pszPrimaryTextureName, pszSecondaryTextureName
// ) load primary and secondary textures...
BOOL CObject3DClientSpecific::LoadTextures(LPCSTR pszPrimaryTextureName,
                                           LPCSTR pszSecondaryTextureName)
{
    CTextureManager *lpTextureManager = (CTextureManager *)((CWorldClient *)GetWorld())
                                            ->GetTextureManager();
    CTexture *lpTexture = NULL;
    CString szString;
    // store new texture names
    CTexture* primary=GetPrimaryTexture();
    const bool transactionalPrimary=pszPrimaryTextureName && *pszPrimaryTextureName &&
        CString(pszPrimaryTextureName).CompareNoCase("none")!=0 && primary &&
        primary->GetTypeID()==TTID_UNIT_MAIN && lpTextureManager->SharePrimaryCharacterAtlas();
    if(!transactionalPrimary)szPrimaryTextureName = pszPrimaryTextureName;
    CTexture* secondary=GetSecondaryTexture();
    const bool transactionalSecondary=pszSecondaryTextureName && *pszSecondaryTextureName &&
        CString(pszSecondaryTextureName).CompareNoCase("none")!=0 && secondary &&
        secondary->GetTypeID()==TTID_UNIT_WEAPON && lpTextureManager->ShareSecondaryCharacterAtlas();
    if(!transactionalSecondary)szSecondaryTextureName = pszSecondaryTextureName;
    // load primary texture

    if (pszPrimaryTextureName && *pszPrimaryTextureName &&
        CString(pszPrimaryTextureName).CompareNoCase("none") != 0)
    {
        if(transactionalPrimary){
            if(!RebindPrimaryTexture(pszPrimaryTextureName))return FALSE;
        }else{
            lpTexture = GetPrimaryTexture();
            if (lpTexture)
                lpTexture = lpTextureManager->FindTexture(lpTexture->GetTypeID(),
                                                          pszPrimaryTextureName);
            if (lpTexture)
                ChangeTexture(lpTexture);
            else
            {
                lpTexture = GetPrimaryTexture();
                if (lpTexture != NULL)
                {
                    lpTexture->SetName(pszPrimaryTextureName);
                    lpTextureManager->LoadTexture(lpTexture);
                }
            }
        }
    }
    // load secondary texture
    if (pszSecondaryTextureName && *pszSecondaryTextureName &&
        CString(pszSecondaryTextureName).CompareNoCase("none") != 0)
    {
        if(transactionalSecondary)return RebindSecondaryTexture(pszSecondaryTextureName);
        lpTexture = GetSecondaryTexture();
        if (lpTexture)
            lpTexture = lpTextureManager->FindTexture(lpTexture->GetTypeID(),
                                                      pszSecondaryTextureName);
        if (lpTexture)
            ChangeTexture(lpTexture);
        else
        {
            lpTexture = GetSecondaryTexture();
            if (lpTexture != NULL)
            {
                lpTexture->SetName(pszSecondaryTextureName);
                lpTextureManager->LoadTexture(lpTexture);
            }
        }
    }

    InvalidateOwnedAppearance();
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CObject3DClientSpecific::LoadWounds(LPCSTR pszWoundsTextureName, const CBYTEArray *expectedData)
{
    if (sets[0] == 0)
        return FALSE;
    CTextureManager *pTextureManager = theWorld.GetTextureManager();
    // find or create texture
    if (!pWoundsTexture)
    {
        pWoundsTexture = pTextureManager->FindTexture(TTID_SPELL_MAIN, pszWoundsTextureName);
        if (!pWoundsTexture)
            pWoundsTexture = pTextureManager->CreateTexture(TTID_SPELL_MAIN, pszWoundsTextureName);
        if (!pWoundsTexture)
            return ReportError("Can't create wounds texture for object \"%s\" ID = %d\n",
                               lpObject->GetGameClassName(), lpObject->GetID());
    }
    if(pWoundsTexture->GetTypeID()!=TTID_SPELL_MAIN ||
       std::strcmp(pWoundsTexture->GetName(),pszWoundsTextureName)!=0)return FALSE;
    if(expectedData && pWoundsTexture->GetSurface() &&
       pWoundsTexture->IsUserDataEqual(*expectedData))return TRUE;
    // GetUserData does not inspect the memory cache; an owner can supply its
    // expected userdata, which is also checked after the actual upload.
    CBYTEArray userdata;
    if(expectedData)userdata.Copy(*expectedData);
    else pTextureManager->GetUserData(pszWoundsTextureName,userdata);
    if(!pWoundsTexture->GetSurface() || !userdata.GetSize() ||
       !pWoundsTexture->IsUserDataEqual(userdata)) {
        struct RestoreWounds {
            CTexture* texture;bool ready=false;
            ~RestoreWounds(){if(!ready)texture->Unload();}
        } restoring{pWoundsTexture};
        pWoundsTexture->ResetFlag(TEX_LOADED);
        if(!pTextureManager->LoadTexture(pWoundsTexture) || !pWoundsTexture->GetSurface() ||
           (expectedData && !pWoundsTexture->IsUserDataEqual(*expectedData)))return FALSE;
        restoring.ready=true;
    }
    return pWoundsTexture->GetSurface()!=nullptr;
}
/////////////////////////////////////////////////////////////////////////////////////
void CObject3DClientSpecific::ReleaseWounds()
{
    pWoundsTexture = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
// setup all materials in this specific with appropriate material data
void CObject3DClientSpecific::SetMaterials()
{
    // set materials
    CMaterialManager *lpMaterialManager = (CMaterialManager *)((CWorldClient *)GetWorld())
                                              ->GetMaterialManager();
    CMaterial *lpMaterial = GetFirstMaterial();
    while (lpMaterial)
    {
        lpMaterialManager->SetMaterial(lpMaterial, lpMaterial->GetTypeID());
        lpMaterial = GetNextMaterial();
    }
    InvalidateOwnedAppearance();
}
/////////////////////////////////////////////////////////////////////////////////////
void CObject3DClientSpecific::Select(BOOL bSelect)
{
}
/////////////////////////////////////////////////////////////////////////////////////
void CObject3DClientSpecific::Preselect(BOOL bSelect)
{
    Highlight(bSelect);
}
/////////////////////////////////////////////////////////////////////////////////////
// set emissive component for all materials to 0 or 1
void CObject3DClientSpecific::Highlight(BOOL bHighlight)
{
    D3DVECTOR dvEmissive;
    int i;

    if (bHighlight)
    {
        dvEmissive.x = 1;
        dvEmissive.y = 1;
        dvEmissive.z = 1;

        for (i = 0; i < materials.GetSize(); i++)
        {
            materials[i]->StoreEmissive();
            materials[i]->SetEmissive(dvEmissive);
        }
    }
    else
    {
        for (i = 0; i < materials.GetSize(); i++)
            materials[i]->RestoreEmissive();
    }
    InvalidateOwnedAppearance();
}
/////////////////////////////////////////////////////////////////////////////////////
// void CObject3DClientSpecific::FinalDataLoading()
// load textures and create materials
void CObject3DClientSpecific::FinalDataLoading()
{
    //
    SetMaterials();
    //
    if (szSecondaryTextureName != "none")
        LoadTextures(szPrimaryTextureName, szSecondaryTextureName);
    else
        LoadTextures(szPrimaryTextureName, NULL);
}
/////////////////////////////////////////////////////////////////////////////////////
void CObject3DClientSpecific::StepInvisibility(float fpTimeStep)
{
    fpTime += fpTimeStep;
    float fRemain = (float)fmod(fpTime, 2.0f * 15.0f);

    if (fRemain > 15)
        fRemain = 30.0f - fRemain;

    fRemain *= (1.0f / 15.0f);

    fRemain = 0.2f + fRemain * 0.5f;

    for (int i = 0; i < materials.GetSize(); ++i)
        materials[i]->SetAlpha(fRemain);
}
/////////////////////////////////////////////////////////////////////////////////////
void CObject3DClientSpecific::SetAsVisible()
{
    for (int i = 0; i < materials.GetSize(); ++i)
        materials[i]->SetAlpha(1);
}
/////////////////////////////////////////////////////////////////////////////////////
void CObject3DClientSpecific::StepStun(float fpTimeStep)
{
    fpTime += fpTimeStep;
    float fRemain = (float)fmod(fpTime, 2.0f * 15.0f);
    if (fRemain > 15)
        fRemain = 30.0f - fRemain;
    fRemain *= (1.0f / 15.0f);
    fRemain = 0.2f + fRemain * 0.5f;
    //
    for (int i = 0; i < materials.GetSize(); ++i)
    {
        materials[i]->SetSpecular(fRemain, 0, 0);
        materials[i]->SetFlag(MAT_USE_SPECULAR);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CObject3DClientSpecific::SetUnstun()
{
    for (int i = 0; i < materials.GetSize(); ++i)
    {
        materials[i]->SetSpecular(0, 0, 0);
        materials[i]->ResetFlag(MAT_USE_SPECULAR);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
bool CObject3DClientSpecific::IsNeedShadow() const
{
#ifndef _A4
    if (theSettings.nShadowQuality <= 0)
        return false;
    switch (lpObject->WhoAmI())
    {
    case HPC_UNIT:
        return theSettings.nUnitShadow != 0;
    case HPC_TREE:
        return theSettings.nFloraShadow != 0;
    default:
        return theSettings.nBuildingShadow != 0;
    }
#else
    return (theSettings.nShadowQuality > 0);
#endif //_A4
}
/////////////////////////////////////////////////////////////////////////////////////
// check for shadow RECALCULATION necessarility
bool CObject3DClientSpecific::IsNeedRecalcShadow() const
{
    return ((lpObject->CheckFlag(OBJ_STATIC | OBJ_LIGHTED) == OBJ_STATIC) ||
            (lpObject->CheckFlag(OBJ_STATIC) == 0) || (CheckFlag(OCS_RESHADOW) != 0));
}
/////////////////////////////////////////////////////////////////////////////////////
void CObject3DClientSpecific::RemoveShadowInformation()
{
    modShadowLastBuild=0;
    shadowvertices.RemoveAll();
    shadowmaps.RemoveAll();
    shadowindices.RemoveAll();
    pShadowTexture = 0;
    shadowrect.SetRectEmpty();
    memset(&bvShadowBV, 0, sizeof(bvShadowBV));
}
/////////////////////////////////////////////////////////////////////////////////////
