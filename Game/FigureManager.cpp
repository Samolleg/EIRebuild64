#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "StringProcessing.h"

#include "FigureManager.h"
#include "AnimationSystem.h"
#include "AnimationSystem2.h"
#include "FileRegistry.h"
#if defined(_A4) || defined(__OBJECT_CONVERTOR_A4__)
#include "AnimationSystem3.h"
#endif // defined(_A4) || defined(__OBJECT_CONVERTOR_A4__)
#include "World.h"
#include <io.h>

#include "GeometryManager.h"

#include "figFormat.h"

#include "I3DFigure.h"
#include "sky.h"

/////////////////////////////////////////////////////////////////////////////////////
CModelResourceEntry::CModelResourceEntry(class CResourceManager *_pManager)
    : pManager1(_pManager)
    , pManager2(0)
    , pModelFile(0)
    , pBonesFile(0)
    , pAnimsFile(0)
{
    pResModel = dbgnew CWin32MappedResource();
    pResBones = dbgnew CWin32MappedResource();
    pResAllAnimations = dbgnew CWin32MappedResource();
}
/////////////////////////////////////////////////////////////////////////////////////
CModelResourceEntry::CModelResourceEntry(class CGeometryManager *_pManager)
    : pManager1(0)
    , pManager2(_pManager)
    , pModelFile(0)
    , pBonesFile(0)
    , pAnimsFile(0)
{
    pResModel = dbgnew CWin32MappedResource();
    pResBones = dbgnew CWin32MappedResource();
    pResAllAnimations = dbgnew CWin32MappedResource();
}
/////////////////////////////////////////////////////////////////////////////////////
CModelResourceEntry::~CModelResourceEntry()
{
    //	ASSERT( (pResModel == 0) && (pBonesFile == 0) && (pAnimsFile == 0) );
    for (auto pos = resAnimations.begin(); pos != resAnimations.end(); ++pos)
        delete pos->second;
    resAnimations.clear();
    pResModel->CloseResource();
    pResBones->CloseResource();
    pResAllAnimations->CloseResource();

    delete pResModel;
    pResModel = 0;
    delete pResBones;
    pResBones = 0;
    delete pResAllAnimations;
    pResAllAnimations = 0;
    // close files
    if (pModelFile)
    {
        if (pManager1)
            pManager1->CloseFile(pModelFile);
        else if (pManager2)
            pManager2->CloseFile(pModelFile);
    }
    if (pBonesFile)
    {
        if (pManager1)
            pManager1->CloseFile(pBonesFile);
        else if (pManager2)
            pManager2->CloseFile(pBonesFile);
    }
    if (pAnimsFile)
    {
        if (pManager1)
            pManager1->CloseFile(pAnimsFile);
        else if (pManager2)
            pManager2->CloseFile(pAnimsFile);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CModelResourceEntry::OpenModelResources(BYTE *pModel, BYTE *pBones, BYTE *pAnimations)
{
    if (pModel)
        pResModel->OpenResourceMem(pModel);
    if (pBones)
        pResBones->OpenResourceMem(pBones);
    if (pAnimations)
        pResAllAnimations->OpenResourceMem(pAnimations);
    pModelFile = pModel;
    pBonesFile = pBones;
    pAnimsFile = pAnimations;
}
/////////////////////////////////////////////////////////////////////////////////////
CMappedResource *CModelResourceEntry::GetAnimation(const std::string &szName)
{
    auto pos = resAnimations.find(szName);
    if (pos == resAnimations.end())
    {
        BYTE *pData = pResAllAnimations->GetFile(szName.c_str());
        if (pData)
        {
            resAnimations[szName] = dbgnew CWin32MappedResource();
            resAnimations[szName]->OpenResourceMem(pData);
        }
        pos = resAnimations.find(szName);
    }
    //
    if (pos != resAnimations.end())
        return pos->second;
    else
        return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
CFigureManager::CFigureManager()
{
#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
}
/////////////////////////////////////////////////////////////////////////////////////
CFigureManager::~CFigureManager()
{
    /*
for ( auto pos =  models.begin(); pos != models.end(); ++pos )
delete pos->second;
            */
    Destroy();
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CFigureManager::AddNewResource(LPCSTR pszResourceFileName)
{
    if (_access(pszResourceFileName, 4) == -1)
        return FALSE;
    // first, check for such resource
    for (auto pos = resources.begin(); pos != resources.end(); ++pos)
    {
        if (pos->first == pszResourceFileName)
            return TRUE;
    }
    // then, add it and open
    resources.emplace_back(CResourcePair(pszResourceFileName, dbgnew CWin32MappedResource()));
    if (!resources.back().second->OpenResource(pszResourceFileName))
    {
        delete resources.back().second;
        resources.pop_back();
        return FALSE;
    }

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// create manager's internal data structures
BOOL CFigureManager::Create()
{
    if (CResourceManager::Create() == FALSE)
        return FALSE;

    CString szResourcesPath = ((CWorld *)GetWorld())->GetResourcePath();
    if (!AddGraphicsResources("figures.res"))
        return FALSE;
    AddNewResource(szResourcesPath + "figures.res");
    AddNewResource(szResourcesPath + "menus.res");
    LoadMenuRects(theMenuRects);
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
CModelResourceEntry *CFigureManager::GetModel(const std::string &szName)
{
    auto pos = models.find(szName);
    if (pos == models.end())
    {
        char buff[512];
        strcpy(buff, szName.c_str());
        strcat(buff, ".mod");
        BYTE *pModel = GetFile(buff);
        strcpy(buff, szName.c_str());
        strcat(buff, ".bon");
        BYTE *pBones = GetFile(buff);
        strcpy(buff, szName.c_str());
        strcat(buff, ".anm");
        BYTE *pAnims = GetFile(buff);
        if (pModel) // we need at rest a model to open resource (bones and animations are optional)
        {
            models[szName] = dbgnew CModelResourceEntry(this);
            models[szName]->OpenModelResources(pModel, pBones, pAnims);
            pos = models.find(szName);
        }
    }
    //
    if (pos != models.end())
        return pos->second;
    else
        return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
CMappedResource *CFigureManager::GetModelAnimation(const std::string &szModel,
                                                   const std::string &szAnimation)
{
    CModelResourceEntry *pModel = GetModel(szModel);
    if (pModel)
        return pModel->GetAnimation(szAnimation);
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
// LPGAMEBASE CFigureManager::CreateInstant( ETypeIDs tidTypeID )
// create instant of the figure with desired type ID
LPGAMEBASE CFigureManager::CreateInstant(ETypeIDs tidTypeID)
{
    LPGAMEBASE pBase = NULL;
    switch (tidTypeID)
    {
#if !defined(_A4) && !defined(__OBJECT_CONVERTOR__)
    case FTID_INTERFACE:
        pBase = dbgnew CI3DFigure();
        break;
    case FTID_SKY:
        pBase = dbgnew CSkyFigure;
        break;
#endif // !defined(_A4) && !defined(__OBJECT_CONVERTOR__)
    case FTID_BODY:
        pBase = dbgnew CFigureMapLoaded();
        break;
    case FTID_HEAPBODY:
        pBase = dbgnew CFigureLoaded();
        break;
    case ASTID_ANIMATION_TRACK:
        pBase = dbgnew CAnimationTrack();
        break;
    case ASTID_ANIMATION_NODE:
        pBase = dbgnew CAnimationNode();
        break;
    case ASTID_ANIMATION_SYSTEM2:
        pBase = dbgnew CAnimationSystem2();
        break;
#if defined(_A4) || defined(__OBJECT_CONVERTOR_A4__)
    case ASTID_ANIMATION_NODE3:
        pBase = dbgnew CAnimationNode3();
        break;
    case ASTID_ANIMATION_SYSTEM3:
        pBase = dbgnew CAnimationSystem3();
        break;
#endif // defined(_A4) || defined(__OBJECT_CONVERTOR_A4__)
    }
    if (pBase == NULL)
        return NULL;
    pBase->SetTypeID(tidTypeID);

    return pBase;
}
/////////////////////////////////////////////////////////////////////////////////////
// LPGAMEBASE CFigureManager::CreateData( ETypeIDs tidTypeID, int nNewID )
// create figure and specific for it anse set specific for figure
LPGAMEBASE CFigureManager::CreateData(ETypeIDs tidTypeID, int nNewID)
{
    switch (tidTypeID)
    {
    case FTID_INTERFACE: // interface figure
    case FTID_BODY:      // standard figure
    case FTID_HEAPBODY:  // figure loaded to heap
    case FTID_SKY: {
        LPFIGURE lpFigure = LPFIGURE(CResourceManager::CreateData(tidTypeID, nNewID));
        if (lpFigure)
        {
            LPGAMEBASE lpSpecific =
                ((CWorld *)GetWorld())->GetSpecificManager()->CreateData(SPTID_FIGURE_SPECIFIC);
            lpFigure->SetFigureSpecific(lpSpecific);
        }
        return lpFigure;
    }
    default: // animation system objects
        return CResourceManager::CreateData(tidTypeID, nNewID);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// load bounding volume in the new format (8 models)
void CFigureManager::JustLoadBoundingVolume(SBoundingVolume &bv, LPBYTE lpData, float fpStrength,
                                            float fpGrace, float fpTall, DWORD dwSignature)
{
    if (dwSignature == FIG_SIGNATURE_FIG8)
    {
        CPlot3D bbCenter, bbMin, bbMax;
        CPlot3D ptTemp[2];
        float radius;
        // process bounding volume
        // center
        CPlot3D *plots = reinterpret_cast<CPlot3D *>(lpData);
        ptTemp[0] = plots[0] + fpStrength * (plots[1] - plots[0]);
        ptTemp[1] = plots[2] + fpStrength * (plots[3] - plots[2]);
        bbCenter = ptTemp[0] + fpGrace * (ptTemp[1] - ptTemp[0]);
        plots = reinterpret_cast<CPlot3D *>(lpData + 4 * sizeof(CPlot3D));
        ptTemp[0] = plots[0] + fpStrength * (plots[1] - plots[0]);
        ptTemp[1] = plots[2] + fpStrength * (plots[3] - plots[2]);
        ptTemp[0] += fpGrace * (ptTemp[1] - ptTemp[0]);
        bbCenter += fpTall * (ptTemp[0] - bbCenter);
        //
        lpData += 8 * sizeof(CPlot3D);
        // min
        plots = reinterpret_cast<CPlot3D *>(lpData);
        ptTemp[0] = plots[0] + fpStrength * (plots[1] - plots[0]);
        ptTemp[1] = plots[2] + fpStrength * (plots[3] - plots[2]);
        bbMin = ptTemp[0] + fpGrace * (ptTemp[1] - ptTemp[0]);
        plots = reinterpret_cast<CPlot3D *>(lpData + 4 * sizeof(CPlot3D));
        ptTemp[0] = plots[0] + fpStrength * (plots[1] - plots[0]);
        ptTemp[1] = plots[2] + fpStrength * (plots[3] - plots[2]);
        ptTemp[0] += fpGrace * (ptTemp[1] - ptTemp[0]);
        bbMin += fpTall * (ptTemp[0] - bbMin);
        //
        lpData += 8 * sizeof(CPlot3D);
        // max
        plots = reinterpret_cast<CPlot3D *>(lpData);
        ptTemp[0] = plots[0] + fpStrength * (plots[1] - plots[0]);
        ptTemp[1] = plots[2] + fpStrength * (plots[3] - plots[2]);
        bbMax = ptTemp[0] + fpGrace * (ptTemp[1] - ptTemp[0]);
        plots = reinterpret_cast<CPlot3D *>(lpData + 4 * sizeof(CPlot3D));
        ptTemp[0] = plots[0] + fpStrength * (plots[1] - plots[0]);
        ptTemp[1] = plots[2] + fpStrength * (plots[3] - plots[2]);
        ptTemp[0] += fpGrace * (ptTemp[1] - ptTemp[0]);
        bbMax += fpTall * (ptTemp[0] - bbMax);
        //
        lpData += 8 * sizeof(CPlot3D);
        // radius
        float fpTemp[2];
        float *rds = reinterpret_cast<float *>(lpData);
        fpTemp[0] = rds[0] + fpStrength * (rds[1] - rds[0]);
        fpTemp[1] = rds[2] + fpStrength * (rds[3] - rds[2]);
        radius = fpTemp[0] + fpGrace * (fpTemp[1] - fpTemp[0]);
        rds = reinterpret_cast<float *>(lpData + 4 * sizeof(float));
        fpTemp[0] = rds[0] + fpStrength * (rds[1] - rds[0]);
        fpTemp[1] = rds[2] + fpStrength * (rds[3] - rds[2]);
        fpTemp[0] += fpGrace * (fpTemp[1] - fpTemp[0]);
        radius += fpTall * (fpTemp[0] - radius);
        //
        lpData += 8 * sizeof(float);
        // set data
        bv.SetCenter(bbCenter);
        bv.SetRadius(radius);
        bv.SetBox(bbMin, bbMax);
    }
    else
    {
        CPlot3D bbCenter, bbMin, bbMax;
        float radius;
        //
        bbCenter = *reinterpret_cast<CPlot3D *>(lpData);
        lpData += sizeof(CPlot3D);
        bbMin = *reinterpret_cast<CPlot3D *>(lpData);
        lpData += sizeof(CPlot3D);
        bbMax = *reinterpret_cast<CPlot3D *>(lpData);
        lpData += sizeof(CPlot3D);
        radius = *reinterpret_cast<float *>(lpData);
        lpData += sizeof(float);
        // set data
        bv.SetCenter(bbCenter);
        bv.SetRadius(radius);
        bv.SetBox(bbMin, bbMax);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// load bounding volume in the old (6 models) format
void CFigureManager::LoadOldBoundingVolume(SBoundingVolume &bv, LPBYTE lpData, float fpStrength,
                                           float fpGrace, float fpTall)
{
    CPlot3D bbCenter, bbMin, bbMax;
    CPlot3D ptSmall, ptTall;
    CPlot3D *ptSmalls;
    CPlot3D *ptTalls;
    float valTall;
    float *smalls;
    float *talls;
    float radius;
    // process bounding volumes ( small: weak, strong, fat; tall: weak, strong, fat )
    // bbCenter
    ptSmalls = (CPlot3D *)lpData;
    ptTalls = ((CPlot3D *)lpData) + 3;
    bbCenter = ptSmalls[0] + (ptSmalls[1] - ptSmalls[0]) * fpStrength;
    bbCenter += (ptSmalls[2] - bbCenter) * fpGrace;
    ptTall = ptTalls[0] + (ptTalls[1] - ptTalls[0]) * fpStrength;
    ptTall += (ptTalls[2] - ptTall) * fpGrace;
    bbCenter += (ptTall - bbCenter) * fpTall;
    lpData += 6 * sizeof(CPlot3D);
    // bbMin
    ptSmalls = (CPlot3D *)lpData;
    ptTalls = ((CPlot3D *)lpData) + 3;
    bbMin = ptSmalls[0] + (ptSmalls[1] - ptSmalls[0]) * fpStrength;
    bbMin += (ptSmalls[2] - bbMin) * fpGrace;
    ptTall = ptTalls[0] + (ptTalls[1] - ptTalls[0]) * fpStrength;
    ptTall += (ptTalls[2] - ptTall) * fpGrace;
    bbMin += (ptTall - bbMin) * fpTall;
    lpData += 6 * sizeof(CPlot3D);
    // bbMax
    ptSmalls = (CPlot3D *)lpData;
    ptTalls = ((CPlot3D *)lpData) + 3;
    bbMax = ptSmalls[0] + (ptSmalls[1] - ptSmalls[0]) * fpStrength;
    bbMax += (ptSmalls[2] - bbMax) * fpGrace;
    ptTall = ptTalls[0] + (ptTalls[1] - ptTalls[0]) * fpStrength;
    ptTall += (ptTalls[2] - ptTall) * fpGrace;
    bbMax += (ptTall - bbMax) * fpTall;
    lpData += 6 * sizeof(CPlot3D);
    // radius
    smalls = (float *)lpData;
    talls = ((float *)lpData) + 3;
    radius = smalls[0] + (smalls[1] - smalls[0]) * fpStrength;
    radius += (smalls[2] - radius) * fpGrace;
    valTall = talls[0] + (talls[1] - talls[0]) * fpStrength;
    valTall += (talls[2] - valTall) * fpGrace;
    radius += (valTall - radius) * fpTall;
    lpData += 6 * sizeof(float);
    // set this data to bounding volume
    bv.SetCenter(bbCenter);
    bv.SetRadius(radius);
    bv.SetBox(bbMin, bbMax);
}
/////////////////////////////////////////////////////////////////////////////////////
// load bounding volume for specified figure (mask + name)
BOOL CFigureManager::LoadBoundingVolume(SBoundingVolume &bv, CModelResourceEntry *pModel,
                                        LPCSTR pszMask, LPCSTR pszName, float fpStrength,
                                        float fpGrace, float fpTall)
{
    BYTE *pData = 0;
    if (pModel)
        pData = pModel->GetModel()->GetFile(pszName);
    else
    {
        char buff[512];
        strcpy(buff, pszMask);
        strcat(buff, pszName);
        strcat(buff, ".fig");
        pData = GetFile(buff);
    }

    if (pData == NULL)
        return FALSE;
    BYTE *pBaseData = pData;
    // here we'll load figure's data
    SFigureFormatHeader hdr;
    memcpy(&hdr, pData, sizeof(hdr));
    if ((hdr.dwSignature & FIG_SIGNATURE_MASK) == FIG_SIGNATURE_FIGN)
    {
        pData += sizeof(hdr) + 2 * sizeof(ETypeIDs);
        JustLoadBoundingVolume(bv, pData, fpStrength, fpGrace, fpTall, hdr.dwSignature);
    }
    else
    {
        pData += 2 * sizeof(ETypeIDs);
        LoadOldBoundingVolume(bv, pData, fpStrength, fpGrace, fpTall);
    }
    //
    //
    if (pModel)
        pModel->GetModel()->CloseFile(pBaseData);
    else
        CloseFile(pBaseData);

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// LPFIGURE CFigureManager::CreateI3DFigure( LPCSTR pszName )
// create interface figure
LPFIGURE CFigureManager::CreateI3DFigure(LPCSTR pszName)
{
    // acquire pointer to figure
    LPFIGURE lpFigure = (LPFIGURE)CreateData(FTID_INTERFACE);
    // empty figure
    if (!pszName)
        return lpFigure;
    // set name and mask for figure
    lpFigure->SetName(pszName);
    lpFigure->SetMask("");
    // here we'll load figure's data
    CString szFileName;
    LPBYTE lpData;

    szFileName.Format("%s.fig", pszName);
    BYTE *pBaseData = lpData = GetFile(szFileName);
    if (lpData == NULL)
    {
        Release((LPGAMEBASE &)(lpFigure));
        return NULL;
    }
    // load figure's data
    lpFigure->LoadD3DComplex(lpData, 0, 0, 0);
    lpFigure->GetBoundingVolume()->dwFlags = 0;

    CloseFile(pBaseData);

    return lpFigure;
}

/////////////////////////////////////////////////////////////////////////////////////
// create figure for unit with specified strength, grace and tall in 0..1 interval
LPFIGURE CFigureManager::CreateFigure(LPCSTR pszMask, LPCSTR pszName, CModelResourceEntry *pModel,
                                      float strength, float grace, float tall, BOOL bLoadData,
                                      BOOL bHeapAllocation)
{
    // acquire pointer to figure
    LPFIGURE lpFigure = (LPFIGURE)CreateData(bHeapAllocation ? FTID_HEAPBODY : FTID_BODY);
    // set name and mask for figure
    lpFigure->SetName(pszName);
    lpFigure->SetMask(pszMask);
    //

    return lpFigure;
}
/////////////////////////////////////////////////////////////////////////////////////
// resize figure with new complection via 'lpFigure->LoadD3DComplex' call
BOOL CFigureManager::ResizeFigure(LPFIGURE lpFigure, CModelResourceEntry *pModel, float strength,
                                  float grace, float tall)
{
    LPBYTE lpData;
    if (pModel)
        lpData = pModel->GetModel()->GetFile(lpFigure->GetName());
    else
    {
        CString szFileName;
        // open data file
        szFileName.Format("%s%s.fig", lpFigure->GetMaskName(), lpFigure->GetName());
        lpData = GetFile(szFileName);
    }
    if (lpData == NULL)
        return ReportError("Can't open file \"%s%s.fig\"\n", lpFigure->GetMaskName(),
                           lpFigure->GetName());
    //
    lpFigure->LoadD3DComplex(lpData, strength, grace, tall);

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CFigureManager::MakeAnimationNode( SAnimationNode* lpNode, LPCSTR pszMask, float strength,
// float grace, float tall ) make animation node from resource
BOOL CFigureManager::MakeAnimationNode(SAnimationNode *lpNode, LPCSTR pszMask, float strength,
                                       float grace, float tall)
{
    LPBYTE lpData = NULL;
    CString szFileName;
    // check param
    if (lpNode == NULL)
        return FALSE;
    //
    lpNode->SetMaskName(pszMask);
    // open resource file (node)
    szFileName.Format("%s%s%s.ani", pszMask, lpNode->GetNodeName(), lpNode->GetAnimationName());
    if (lpData == NULL)
        lpNode->SetDefaultInitValues();
    else
    {
        // load node
        lpNode->SetComplection(strength, grace, tall);
        lpNode->LoadComplex(lpData);
    }
    // open resource file (bone)
    szFileName.Format("%s%s.bon", pszMask, lpNode->GetNodeName());
    DWORD dwSize = 0;
    lpData = GetFile(szFileName, &dwSize);
    if (lpData == NULL)
        return ReportError("CFigureManager::MakeAnimationNode.\n Can't load bone file \"%s\"\n",
                           szFileName);
    // load bone
    lpNode->LoadBone(lpData, strength, grace, tall);

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CFigureManager::LoadAnimation( SAnimationNode* lpNode )
// load animation with current complection
BOOL CFigureManager::LoadAnimation(SAnimationNode *lpNode)
{
    LPBYTE lpData = NULL;
    CString szFileName;
    // check param
    if (lpNode == NULL)
        return FALSE;
    // open resource file (node)
    szFileName.Format("%s%s%s.ani", lpNode->GetMaskName(), lpNode->GetNodeName(),
                      lpNode->GetAnimationName());
    lpData = GetFile(szFileName);
    if (lpData != NULL)
    {
        lpNode->LoadComplex(lpData);
        return TRUE;
    }
    //	else
    //		ReportWarning( "Can't load animation node \"%s\".\n", szFileName );

    return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CFigureManager::ResizeAnimationNode( SAnimationNode* lpNode, float strength, float grace,
// float tall ) make new bone from specified strength, grace and tall
BOOL CFigureManager::ResizeAnimationNode(SAnimationNode *lpNode, float strength, float grace,
                                         float tall)
{
    LPBYTE lpData = NULL;
    CString szFileName;
    // check param
    if (lpNode == NULL)
        return FALSE;
    // open resource file (bone)
    szFileName.Format("%s%s.bon", lpNode->GetMaskName(), lpNode->GetNodeName());
    lpData = GetFile(szFileName);
    if (lpData == NULL)
        return ReportError("CFigureManager::ResizeAnimationNode.\n Can't load bone file %s\n",
                           szFileName);
    // load bone
    lpNode->LoadBone(lpData, strength, grace, tall);

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CFigureManager::LoadSkeleton( CAnimationSystem* lpAnimations, LPCSTR pszFile )
// load skeleton links
BOOL CFigureManager::LoadSkeleton(CAnimationSystem *lpAnimations, LPCSTR pszFile)
{
    LPBYTE lpData = NULL;
    CString szFileName;
    CMemFile memfile;
    DWORD dwSize;
    //
    BYTE *pBaseData = lpData = GetFile(pszFile, &dwSize);
    if (lpData == NULL)
        return ReportError("CFigureManager::LoadSkeleton.\n Can't load skeleton file %s\n",
                           szFileName);
    // load bone
    memfile.Attach(lpData, dwSize);
    lpAnimations->LoadSkeleton(memfile);
    memfile.Detach();

    CloseFile(pBaseData);

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// load skeleton and form animation nodes and links
BOOL CFigureManager::LoadSkeleton(CAnimationSystem2 *pAnimationSystem, LPCSTR pszMaskName)
{
    CModelResourceEntry *pModel = GetModel(pszMaskName);
    BYTE *pData;
    DWORD ownedLinkSize = 0;
    if (pModel)
        pData = pModel->GetModel()->GetFile(pszMaskName, &ownedLinkSize);
    else
        pData = GetFile(CString(pszMaskName) + ".lnk", &ownedLinkSize);
    pAnimationSystem->PublishOwnedLinks(pszMaskName, pData, ownedLinkSize);
    if (pData == 0)
        return ReportError("Can't load skeleton file for \"%s\"\n", pszMaskName);
    BYTE *pBaseData = pData;
    // number of animation nodes
    DWORD num = *((DWORD *)pData);
    pData += sizeof(DWORD);
    // read all nodes
    CString szNodeName, szParentNodeName;
    for (int i = 0; i < num; ++i)
    {
        // load name and parent name
        LoadString(szNodeName, pData);
        LoadString(szParentNodeName, pData);
        // add new node
        pAnimationSystem->AddNewNode(pModel, szNodeName, szParentNodeName);
    }
    //
    pAnimationSystem->SetupHeadNode();
    //
    if (pModel)
        pModel->GetModel()->CloseFile(pBaseData);
    else
        CloseFile(pBaseData);
    //
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
#if defined(_A4) || defined(__OBJECT_CONVERTOR_A4__)
// load skeleton and form animation nodes and links
BOOL CFigureManager::LoadSkeleton(CAnimationSystem3 *pAnimationSystem, LPCSTR pszMaskName)
{
    CSmartPtr<CModelResourceEntry> pModel = GetModel(pszMaskName);
    BYTE *pData;
    if (pModel)
        pData = pModel->GetModel()->GetFile(pszMaskName);
    else
        pData = GetFile(CString(pszMaskName) + ".lnk");
    if (pData == 0)
        return ReportError("Can't load skeleton file for \"%s\"\n", pszMaskName);
    BYTE *pBaseData = pData;
    // number of animation nodes
    DWORD num = *((DWORD *)pData);
    pData += sizeof(DWORD);
    // read all nodes
    CString szNodeName, szParentNodeName;
    for (int i = 0; i < num; ++i)
    {
        // load name and parent name
        LoadString(szNodeName, pData);
        LoadString(szParentNodeName, pData);
        // add new node
        pAnimationSystem->AddNewNode(szNodeName);
    }
    //
    if (pModel)
        pModel->GetModel()->CloseFile(pBaseData);
    else
        CloseFile(pBaseData);
    //
    return TRUE;
}
#endif // defined(_A4) || defined(__OBJECT_CONVERTOR_A4__)
/////////////////////////////////////////////////////////////////////////////////////
// load animation track through it's own load function
BOOL CFigureManager::LoadAnimationTrack(CAnimationNode *lpNode, CAnimationTrack *lpTrack,
                                        LPCSTR pszAnimationName)
{
    char szBuffer[512];

    if (pszAnimationName)
    {
        strcpy(szBuffer, lpNode->GetMaskName());
        strcat(szBuffer, lpNode->GetName());
        strcat(szBuffer, pszAnimationName);
        strcat(szBuffer, ".ast");

        LPBYTE lpData = GetFile(szBuffer);
        if (lpData != NULL)
        {
            lpTrack->Load(lpData);
            return TRUE;
        }
        else
            return FALSE;
    }
    else
        return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
// load animation node bone from mapped file through the function CAnimationNode::LoadBone
BOOL CFigureManager::LoadAnimationNodeBone(CModelResourceEntry *pModel, CAnimationNode *lpNode)
{
    BYTE *lpData = 0;
    DWORD dwSize = 0;
    if (pModel)
        lpData = pModel->GetBones()->GetFile(lpNode->GetNodeName(), &dwSize);
    else
    {
        CString szFileName = CString(lpNode->GetMaskName()) + CString(lpNode->GetName()) + ".bon";
        lpData = GetFile(szFileName, &dwSize);
    }
    lpNode->PublishOwnedBone(lpData, dwSize);
    if (lpData != NULL)
    {
        if (dwSize == 72) // old (6 models) bone
            lpNode->LoadOldBone(lpData);
        else // new (8 models) bone
            lpNode->LoadBone(lpData);
        //
        return TRUE;
    }
    else
        return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
#if defined(_A4) || defined(__OBJECT_CONVERTOR_A4__)
BOOL CFigureManager::LoadAnimationTrack(CAnimationNode3 *pNode, LPCSTR pszAnimationName)
{
    char szBuffer[512];

    if (pszAnimationName)
    {
        strcpy(szBuffer, pNode->GetMaskName().c_str());
        strcat(szBuffer, pNode->GetNodeName().c_str());
        strcat(szBuffer, pszAnimationName);
        strcat(szBuffer, ".ast");

        LPBYTE pData = GetFile(szBuffer);
        if (pData != 0)
        {
            pNode->Load(pData);
            return TRUE;
        }
        else
            return FALSE;
    }
    else
        return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CFigureManager::LoadAnimationNodeBone(CAnimationNode3 *pNode)
{
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CFigureManager::LoadAnimation(CAnimationSystem3 *pAnimationSystem, LPCSTR pszMaskName,
                                   const SADEntry *pADEntry)
{
    if (pADEntry == 0)
        return pAnimationSystem->ClearCurrentAnimation();
    //
    CSmartPtr<CModelResourceEntry> pModel = GetModel(pszMaskName);
    if (pModel == 0)
    {
        ReportWarning("Can't open model \"%s\"\n", pszMaskName);
        return FALSE;
    }
    //
    CMappedResource *pAnimation = pModel->GetAnimation(pADEntry->pszAnimationName);
    if (pAnimation == 0)
    {
        ReportWarning("Can't open animation \"%s\" for model \"%s\"\n", pADEntry->pszAnimationName,
                      pszMaskName);
        return FALSE;
    }
    //
    return pAnimationSystem->LoadAnimation(pAnimation, pADEntry, pModel);
}
#endif // defined(_A4) || defined(__OBJECT_CONVERTOR_A4__)
/////////////////////////////////////////////////////////////////////////////////////
BOOL CFigureManager::LoadAnimation(CAnimationSystem2 *pAnimationSystem, LPCSTR pszMaskName,
                                   const SADEntry *pADEntry)
{
    if (pADEntry == 0)
        return pAnimationSystem->ClearCurrentAnimation();
    //
    CModelResourceEntry *pModel = GetModel(pszMaskName);
    if (pModel == 0)
    {
        ReportWarning("Can't open model \"%s\"\n", pszMaskName);
        return FALSE;
    }
    //
    CMappedResource *pAnimation = pModel->GetAnimation(pADEntry->pszAnimationName);
    if (pAnimation == 0)
    {
        ReportWarning("Can't open animation \"%s\" for model \"%s\"\n", pADEntry->pszAnimationName,
                      pszMaskName);
        return FALSE;
    }
    //
    return pAnimationSystem->LoadAnimation(pAnimation, pADEntry);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CFigureManager::LoadMenuRects(SI3DMenuRects &rects)
{
    CFileRegistry reg;
    BYTE *pData = GetFile("menus.reg");
    if (pData == 0)
        return ReportError("Can't open file \"menus.reg\" with the main/esc menu rects\n");
    if (reg.Open(pData) == FALSE)
        return ReportError("Can't open \"menus.reg\" as .reg with the main/esc menu rects\n");
    //
    CIntArray ints;
    //
    // MainMenu
    //
    // MainMenu NewGame
    ints.RemoveAll();
    reg.GetIntArray("MainMenu", "NewGame", ints);
    if (ints.GetSize() != 4)
        return ReportError("Can't get rect for MM NewGame\n");
    SetRect(&rects.rcMMNewGame, ints[0], ints[1], ints[2], ints[3]);
    // MainMenu LoadGame
    ints.RemoveAll();
    reg.GetIntArray("MainMenu", "LoadGame", ints);
    if (ints.GetSize() != 4)
        return ReportError("Can't get rect for MM LoadGame\n");
    SetRect(&rects.rcMMLoad, ints[0], ints[1], ints[2], ints[3]);
    // MainMenu ExitGame
    ints.RemoveAll();
    reg.GetIntArray("MainMenu", "ExitGame", ints);
    if (ints.GetSize() != 4)
        return ReportError("Can't get rect for MM ExitGame\n");
    SetRect(&rects.rcMMExit, ints[0], ints[1], ints[2], ints[3]);
    // MainMenu Options
    ints.RemoveAll();
    reg.GetIntArray("MainMenu", "Options", ints);
    if (ints.GetSize() != 4)
        return ReportError("Can't get rect for MM Options\n");
    SetRect(&rects.rcMMOptions, ints[0], ints[1], ints[2], ints[3]);
    // MainMenu Credits
    ints.RemoveAll();
    reg.GetIntArray("MainMenu", "Credits", ints);
    if (ints.GetSize() != 4)
        return ReportError("Can't get rect for MM Credits\n");
    SetRect(&rects.rcMMCredits, ints[0], ints[1], ints[2], ints[3]);
    // MainMenu Multiplayer
    ints.RemoveAll();
    reg.GetIntArray("MainMenu", "Multiplayer", ints);
    if (ints.GetSize() != 4)
        return ReportError("Can't get rect for MM Multiplayer\n");
    SetRect(&rects.rcMMMultiplayer, ints[0], ints[1], ints[2], ints[3]);
    //
    // EscMenu
    //
    // EscMenu SaveGame
    ints.RemoveAll();
    reg.GetIntArray("EscMenu", "SaveGame", ints);
    if (ints.GetSize() != 4)
        return ReportError("Can't get rect for EM SaveGame\n");
    SetRect(&rects.rcEMSave, ints[0], ints[1], ints[2], ints[3]);
    // EscMenu LoadGame
    ints.RemoveAll();
    reg.GetIntArray("EscMenu", "LoadGame", ints);
    if (ints.GetSize() != 4)
        return ReportError("Can't get rect for EM LoadGame\n");
    SetRect(&rects.rcEMLoad, ints[0], ints[1], ints[2], ints[3]);
    // EscMenu Options
    ints.RemoveAll();
    reg.GetIntArray("EscMenu", "Options", ints);
    if (ints.GetSize() != 4)
        return ReportError("Can't get rect for EM Options\n");
    SetRect(&rects.rcEMOptions, ints[0], ints[1], ints[2], ints[3]);
    // EscMenu ResumeGame
    ints.RemoveAll();
    reg.GetIntArray("EscMenu", "ResumeGame", ints);
    if (ints.GetSize() != 4)
        return ReportError("Can't get rect for EM ResumeGame\n");
    SetRect(&rects.rcEMReturn, ints[0], ints[1], ints[2], ints[3]);
    // EscMenu ExitMM
    ints.RemoveAll();
    reg.GetIntArray("EscMenu", "ExitMM", ints);
    if (ints.GetSize() != 4)
        return ReportError("Can't get rect for EM ExitMM\n");
    SetRect(&rects.rcEMExit, ints[0], ints[1], ints[2], ints[3]);

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CFigureManager::FreeUnusedResources()
{
    std::vector<std::string> toErase;
    for (auto pos = models.begin(); pos != models.end(); ++pos)
    {
        if (pos->second->GetRef() == 1)
            toErase.push_back(pos->first);
    }
    for (auto it = toErase.begin(); it != toErase.end(); ++it)
        models.erase(*it);
}
/////////////////////////////////////////////////////////////////////////////////////
