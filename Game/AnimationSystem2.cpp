#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "Object.h"
#include "AnimationSystem2.h"

#include "Figure.h"
#include "FigureManager.h"

#include "World.h"
#include "MapObjectManager.h"
#include "OwnedRender/E1Scene.h"
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
// **                   class CAnimationTrack functions                          ** //
// ******************************************************************************** //
/////////////////////////////////////////////////////////////////////////////////////
CAnimationTrack::CAnimationTrack()
{
    Clear();
#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
}
/////////////////////////////////////////////////////////////////////////////////////
CAnimationTrack::~CAnimationTrack()
{
    Destroy();
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CAnimationTrack::Create()
{
    Clear();
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CAnimationTrack::Destroy()
{
    Clear();
}
/////////////////////////////////////////////////////////////////////////////////////
void CAnimationTrack::Clear()
{
    lpADEntry = NULL;
    lpNode = NULL;
    // frames
    dwNumPositionFrames = 0;
    positions = NULL;
    dwNumRotationFrames = 0;
    rotations = NULL;
    dwNumMorphings = 0;
    morphings.RemoveAll();
    // current state (in Local Coordinate System /LCS/)
    position = VNULL;
    rotation = QNULL;
    morphing.RemoveAll();
    // time information
    fpTime = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CAnimationTrack::ClearAnimation()
{
    lpADEntry = NULL;
    // frames
    dwNumPositionFrames = 0;
    positions = NULL;
    dwNumRotationFrames = 0;
    rotations = NULL;
    dwNumMorphings = 0;
    morphings.RemoveAll();
    // current state (in Local Coordinate System /LCS/)
    position = VNULL;
    rotation = QNULL;
    morphing.RemoveAll();
    // time information
    fpTime = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
// set animation entry and map animation track data
void CAnimationTrack::SetAnimation(const SADEntry *lpNewADEntry)
{
    lpADEntry = lpNewADEntry;
    if (lpADEntry)
        ((CFigureManager *)GetManager())
            ->LoadAnimationTrack(lpNode, this, lpADEntry->pszAnimationName);
    else
        ((CFigureManager *)GetManager())->LoadAnimationTrack(lpNode, this, 0);
    fpTime = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
// map animation track data from memory
BOOL CAnimationTrack::Load(LPBYTE &lpData, DWORD dwDataSize)
{
    LPBYTE pData = lpData;
    DWORD dwOffset = 0;
    auto readCount = [&](DWORD &value) -> BOOL {
        if (dwOffset > dwDataSize || dwDataSize - dwOffset < sizeof(DWORD))
            return FALSE;
        memcpy(&value, pData + dwOffset, sizeof(value));
        dwOffset += sizeof(value);
        return TRUE;
    };

    DWORD numRotationFrames = 0;
    DWORD numPositionFrames = 0;
    DWORD numMorphingFrames = 0;
    DWORD numMorphingPoints = 0;
    if (!readCount(numRotationFrames) ||
        numRotationFrames > (dwDataSize - dwOffset) / sizeof(CQuaternion))
        return FALSE;
    CQuaternion *rotationData = numRotationFrames
                                    ? reinterpret_cast<CQuaternion *>(pData + dwOffset)
                                    : NULL;
    dwOffset += numRotationFrames * sizeof(CQuaternion);

    if (!readCount(numPositionFrames) ||
        numPositionFrames > (dwDataSize - dwOffset) / sizeof(CPlot3D))
        return FALSE;
    CPlot3D *positionData = numPositionFrames
                                ? reinterpret_cast<CPlot3D *>(pData + dwOffset)
                                : NULL;
    dwOffset += numPositionFrames * sizeof(CPlot3D);

    if (!readCount(numMorphingFrames))
        return FALSE;
    if (numMorphingFrames != 0 &&
        (!readCount(numMorphingPoints) ||
         numMorphingFrames > (dwDataSize - dwOffset) / sizeof(CPlot3D *) ||
         numMorphingPoints > (dwDataSize - dwOffset) / sizeof(CPlot3D) / numMorphingFrames))
        return FALSE;

    dwNumRotationFrames = numRotationFrames;
    rotations = rotationData;
    dwNumPositionFrames = numPositionFrames;
    positions = positionData;
    dwNumMorphings = numMorphingPoints;
    morphing.SetSize(dwNumMorphings); // ARRAY BOUNDS CHECK
    morphings.SetSize(numMorphingFrames); // set number of morphing frames; ARRAY BOUNDS CHECK
    if (morphings.GetSize())
    {
        for (int i = 0; i < morphings.GetSize(); i++)
        {
            morphings[i] = reinterpret_cast<CPlot3D *>(pData + dwOffset);
            dwOffset += dwNumMorphings * sizeof(CPlot3D);
        }
    }
    lpData = pData + dwOffset;
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// void CAnimationTrack::SetTime( float fpNewTime )
// set current position/rotation/morphing with respect to time
void CAnimationTrack::SetTime(float fpNewTime)
{
    double fpTimeInteger;
    float fpTimeFraction = (float)modf(fpNewTime, &fpTimeInteger);
    int nTime = MINT(float(fpTimeInteger));
    // position
    if (nTime + 1 < int(dwNumPositionFrames))
        position = positions[nTime] + fpTimeFraction * (positions[nTime + 1] - positions[nTime]);
    else if (dwNumPositionFrames != 0)
        position = positions[dwNumPositionFrames - 1];
    else
        position = VNULL;
    // rotation
    if (nTime + 1 < int(dwNumRotationFrames))
        rotation.Slerp(fpTimeFraction, rotations[nTime], rotations[nTime + 1]);
    else if (dwNumRotationFrames != 0)
        rotation = rotations[dwNumRotationFrames - 1];
    else
        rotation = QNULL;
    // morphing
    if (nTime + 1 < morphings.GetSize())
    {
        for (int i = 0; i < int(dwNumMorphings); i++)
            morphing[i] = morphings[nTime][i] +
                          fpTimeFraction * (morphings[nTime + 1][i] - morphings[nTime][i]);
    }
    else if (morphings.GetSize() != 0)
        memcpy(morphing.GetData(), morphings[morphings.GetUpperBound()],
               dwNumMorphings * sizeof(CPlot3D));
    //	else
    //		memset( morphing.GetData(), 0, morphing.GetSize() * sizeof(CPlot3D) );
    // set time
    fpTime = fpNewTime;
}
/////////////////////////////////////////////////////////////////////////////////////
// void CAnimationTrack::Step( float fpStep )
// add 'fpStep' to 'fpTime' and position animation to this time
void CAnimationTrack::Step(float fpStep)
{
    float fpNewTime = fpTime + fpStep;
    SetTime(fpNewTime);
}
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
// **                      class CAnimationNode functions                        ** //
// ******************************************************************************** //
/////////////////////////////////////////////////////////////////////////////////////
CAnimationNode::CAnimationNode()
{
    Clear();
#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
}
/////////////////////////////////////////////////////////////////////////////////////
CAnimationNode::~CAnimationNode()
{
    Destroy();
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CAnimationNode::Create()
{
    Clear();
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CAnimationNode::Destroy()
{
    // release figure
    SetFigure(NULL);
    // release animation tracks
    lpCurrent = NULL;
    if (lpMain) // main animation track
        lpMain->Release();
    lpMain = NULL;
    if (lpNext) // next animation track
        lpNext->Release();
    lpNext = NULL;
    if (lpAlternate) // alternate animation track
        lpAlternate->Release();
    lpAlternate = NULL;
    //
    Clear();
}
/////////////////////////////////////////////////////////////////////////////////////
void CAnimationNode::Clear()
{
    ownedBone.Clear();
    // names
    szName = "none";
    szParentName = "none";
    szMaskName = "none";
    // links (parent and children)
    lpParent = NULL;
    children.RemoveAll();
    lpFigure = NULL;
    // animation tracks
    lpCurrent = NULL;
    lpMain = NULL;
    lpNext = NULL;
    lpAlternate = NULL;
    // current placement information (used only during interpolation between current and next
    // animations)
    position = VNULL;
    rotation = QNULL;
    morphing.SetSize(0); // ARRAY BOUNDS CHECK
    // complection
    strength = 0;
    grace = 0;
    tall = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
// set complection for further use with data loading
void CAnimationNode::SetComplection(CModelResourceEntry *pModel, float fpStrength, float fpGrace,
                                    float fpTall)
{
    // set new complection
    strength = fpStrength;
    grace = fpGrace;
    tall = fpTall;
    // reload bone with new complection
    ((CFigureManager *)lpManager)->LoadAnimationNodeBone(pModel, this);
}
/////////////////////////////////////////////////////////////////////////////////////
// get current complection from node
void CAnimationNode::GetComplection(float &fpStrength, float &fpGrace, float &fpTall)
{
    fpStrength = strength;
    fpGrace = grace;
    fpTall = tall;
}
/////////////////////////////////////////////////////////////////////////////////////
// load complex data for animation node bone and recalc it with current complection
void CAnimationNode::LoadBone(LPBYTE &lpData)
{
    CPlot3D temp[2];
    CPlot3D *plots = reinterpret_cast<CPlot3D *>(lpData);
    bone = plots[0] + strength * (plots[1] - plots[0]);
    temp[1] = plots[2] + strength * (plots[3] - plots[2]);
    bone += grace * (temp[1] - bone);
    plots = reinterpret_cast<CPlot3D *>(lpData + 4 * sizeof(CPlot3D));
    temp[0] = plots[0] + strength * (plots[1] - plots[0]);
    temp[1] = plots[2] + strength * (plots[3] - plots[2]);
    temp[0] += grace * (temp[1] - temp[0]);
    bone += tall * (temp[0] - bone);
    //
    lpData += sizeof(CPlot3D) * 8;
}
/////////////////////////////////////////////////////////////////////////////////////
void CAnimationNode::LoadOldBone(LPBYTE &lpData)
{
    CPlot3D *plots = (CPlot3D *)lpData;
    CPlot3D plot;
    //
    bone = plots[0] + strength * (plots[1] - plots[0]);
    bone += grace * (plots[2] - bone);
    plot = plots[3] + strength * (plots[4] - plots[3]);
    plot += grace * (plots[5] - plot);
    bone += tall * (plot - bone);
    //
    lpData += sizeof(CPlot3D) * 6;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CAnimationNode::AddChildNode( CAnimationNode* lpNewNode )
// add child node to children list, but check for the node with the same name first
BOOL CAnimationNode::AddChildNode(CAnimationNode *lpNewNode)
{
    POSITION pos = children.GetHeadPosition();
    CAnimationNode *lpNode = NULL;
    // check for node with such name existance
    while (pos)
    {
        lpNode = children.GetNext(pos);
        if (lpNode->GetNodeName() == lpNewNode->GetNodeName())
            return FALSE; // return with false, if such node already exist in the children list
    }
    // add node
    children.AddTail(lpNewNode);
    lpNewNode->SetParentNode(this);
    //
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CAnimationNode::RemoveChildNode( CAnimationNode* lpNewNode )
// remove child node from this node. also, set perent to removed node to NULL
BOOL CAnimationNode::RemoveChildNode(CAnimationNode *lpNewNode)
{
    POSITION pos = children.GetHeadPosition();
    CAnimationNode *lpNode = NULL;
    POSITION lastpos = pos;
    // check for node with such name existance
    while (pos)
    {
        lpNode = children.GetNext(pos);
        if (lpNode == lpNewNode)
        {
            children.RemoveAt(lastpos);
            lpNode->SetParentNode(NULL);
            return TRUE;
        }
        lastpos = pos;
    }
    //
    return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
// void CAnimationNode::RemoveAllChildren()
// remove all children from this node and set parent for all removed children to NULL
void CAnimationNode::RemoveAllChildren()
{
    POSITION pos = children.GetHeadPosition();
    CAnimationNode *lpNode = NULL;
    //
    while (pos)
    {
        lpNode = children.GetNext(pos);
        lpNode->SetParentNode(NULL);
    }
    children.RemoveAll();
}
/////////////////////////////////////////////////////////////////////////////////////
// void CAnimationNode::SetFigure( CFigure* lpNewFigure )
// link figure with animation node
void CAnimationNode::SetFigure(CFigure *lpNewFigure)
{
    CFigure *pOldFigure = lpFigure;
    // set new one
    lpFigure = lpNewFigure;
    // add ref
    if (lpFigure)
    {
        lpFigure->AddRef();
        // set current placement for the new figure
        lpFigure->SetCP(position);
        lpFigure->SetOrientation(rotation);
    }
    // release old figure
    if (pOldFigure)
        pOldFigure->Release();
}
/////////////////////////////////////////////////////////////////////////////////////
// void CAnimationNode::SetAlternateAnimation( const SADEntry* lpADEntry )
// create and set alternate animation track
void CAnimationNode::SetAlternateAnimation(const SADEntry *lpADEntry)
{
    if (!lpAlternate) // create alternate animation track (if need)
    {
        lpAlternate = (CAnimationTrack *)lpManager->CreateData(ASTID_ANIMATION_TRACK);
        lpAlternate->AddRef();
        lpAlternate->SetNode(this);
    }
    // set animation to alternate track and initialize it
    lpAlternate->SetAnimation(lpADEntry);
}
/////////////////////////////////////////////////////////////////////////////////////
void CAnimationNode::CreateMainTrack()
{
    if (!lpMain) // create main animation track (if need)
    {
        lpMain = (CAnimationTrack *)lpManager->CreateData(ASTID_ANIMATION_TRACK);
        lpMain->AddRef();
        lpMain->SetNode(this);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// create and set current animation track
void CAnimationNode::SetAnimation(const SADEntry *lpADEntry)
{
    if (!lpMain) // create main animation track (if need)
    {
        lpMain = (CAnimationTrack *)lpManager->CreateData(ASTID_ANIMATION_TRACK);
        lpMain->AddRef();
        lpMain->SetNode(this);
    }
    // set animation to current track and initialize it
    lpMain->SetAnimation(lpADEntry);
}
/////////////////////////////////////////////////////////////////////////////////////
// void CAnimationNode::SetNextAnimation( const SADEntry* lpADEntry )
// create and set next animation track
void CAnimationNode::SetNextAnimation(const SADEntry *lpADEntry)
{
    if (!lpNext) // create next animation track (if need)
    {
        lpNext = (CAnimationTrack *)lpManager->CreateData(ASTID_ANIMATION_TRACK);
        lpNext->AddRef();
        lpNext->SetNode(this);
    }
    // set animation to next track and initialize it
    lpNext->SetAnimation(lpADEntry);
}
/////////////////////////////////////////////////////////////////////////////////////
// void CAnimationTrack::SetCurrentPlacement( CFigure* lpFigure )
// set current position/rotation/morphing to figure
void CAnimationNode::SetCurrentPlacement(CFigure *lpFigure)
{
    lpFigure->SetCP(position);
    lpFigure->SetOrientation(rotation);
    lpFigure->SetMorphs(lpCurrent->morphing);
}
/////////////////////////////////////////////////////////////////////////////////////
// void CAnimationNode::RecalcSkeleton( const CPlot3D& ptCenterShift )
// recalculate skeleton, based on current track position and rotation in LCS
void CAnimationNode::RecalcSkeleton(const CPlot3D &ptCenterShift)
{
    // first, recalc position and rotation in model coordinate system (MCS) for this node
    if (lpParent) // for child node placement defined in LCS and must be cast with parent placement
                  // and bone
    {
        lpParent->rotation.Rotate(bone, position);
        position += lpParent->position;
        rotation = lpParent->rotation;
        rotation *= lpCurrent->rotation;
    }
    else if (lpCurrent) // for top-level node position and rotation are absolute
    {
        if (lpCurrent->GetADEntry())
            position = lpCurrent->position * lpCurrent->GetADEntry()->GetHeightCorrection(tall);
        else
            position = lpCurrent->position;
        rotation = lpCurrent->rotation;
        // shift top-level object to 'ptCenterShift'
        position -= ptCenterShift;
    }
    // set placement to attached figure
    if (lpFigure)
        SetCurrentPlacement(lpFigure);
    // then, do it for all children
    POSITION pos = children.GetHeadPosition();
    CAnimationNode *lpNode = NULL;
    while (pos)
    {
        lpNode = children.GetNext(pos);
        lpNode->RecalcSkeleton(ptCenterShift);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CAnimationNode::ClearCurrentTrack(const SADEntry *pADEntry)
{
    lpCurrent->ClearAnimation();
    lpCurrent->SetADEntry(pADEntry);
}
/////////////////////////////////////////////////////////////////////////////////////
void CAnimationNode::LoadCurrentTrack(BYTE *pData, DWORD dwDataSize, const SADEntry *pADEntry)
{
    if (!lpCurrent->Load(pData, dwDataSize))
        lpCurrent->ClearAnimation();
    lpCurrent->SetADEntry(pADEntry);
}
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
//                   class CAnimationSystem2 functions                           ** //
// ******************************************************************************** //
/////////////////////////////////////////////////////////////////////////////////////
CAnimationSystem2::CAnimationSystem2()
{
    Clear();
#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
}
/////////////////////////////////////////////////////////////////////////////////////
CAnimationSystem2::~CAnimationSystem2()
{
    Destroy();
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CAnimationSystem2::Create()
{
    bNeedRecalc = true;
    Clear();
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CAnimationSystem2::Destroy()
{
    // release all animation nodes
    DeleteNodes();
    //
    Clear();
}
/////////////////////////////////////////////////////////////////////////////////////
void CAnimationSystem2::DeleteNodes()
{
    ownedSkeleton.Clear();
    POSITION pos = nodes.GetHeadPosition();
    CAnimationNode *lpNode = NULL;
    while (pos)
    {
        lpNode = nodes.GetNext(pos);
        lpNode->Release();
    }
    nodes.RemoveAll();
}
/////////////////////////////////////////////////////////////////////////////////////
void CAnimationSystem2::Clear()
{
    ownedSkeleton.Clear();
    SwitchCallback = 0;
    lpObject3D = NULL;
    lpHeadNode = NULL;
    nodes.RemoveAll();
    lpADEntry = NULL;
    entries.RemoveAll();
    fpTime = 0;
    strength = 0;
    grace = 0;
    tall = 0;
    bNeedRecalc = true;
}
/////////////////////////////////////////////////////////////////////////////////////
// set animation to all nodes as current and set this entry as current animation
void CAnimationSystem2::SetAnimation(const SADEntry *lpNewADEntry)
{
    POSITION pos = nodes.GetHeadPosition();
    while (pos)
    {
        CAnimationNode *pNode = nodes.GetNext(pos);
        pNode->CreateMainTrack();
        pNode->SetMainAsCurrent();
    }
    // load in the new format
    BOOL bRetVal = static_cast<CFigureManager *>(GetWorld()->GetFigureManager())
                       ->LoadAnimation(this, lpObject3D->GetMaskName(), lpNewADEntry);
    // load in the old format
    if (bRetVal == FALSE)
    {
        POSITION pos = nodes.GetHeadPosition();
        while (pos)
        {
            CAnimationNode *pNode = nodes.GetNext(pos);
            pNode->SetAnimation(lpNewADEntry);
        }
    }
    // reset animation time due to animation was changed directly
    SetTime(0);
    //
    lpADEntry = lpNewADEntry;
}
/////////////////////////////////////////////////////////////////////////////////////
// void CAnimationSystem2::SetNextAnimation( const SADEntry* lpNewADEntry )
// set animation to all nodes as next
void CAnimationSystem2::SetNextAnimation(const SADEntry *lpNewADEntry)
{
    POSITION pos = nodes.GetHeadPosition();
    CAnimationNode *lpNode = NULL;
    while (pos)
    {
        lpNode = nodes.GetNext(pos);
        lpNode->SetNextAnimation(lpNewADEntry);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// void CAnimationSystem2::AddNextAnimation( const SADEntry* lpNewADEntry )
// just add animation entry to the animation entry queue
void CAnimationSystem2::AddNextAnimation(const SADEntry *lpNewADEntry)
{
    entries.AddTail(lpNewADEntry);
}
/////////////////////////////////////////////////////////////////////////////////////
// CAnimationNode* CAnimationSystem2::FindNode( LPCSTR pszNodeName )
// find node by name (lowercase)
CAnimationNode *CAnimationSystem2::FindNode(LPCSTR pszNodeName)
{
    POSITION pos = nodes.GetHeadPosition();
    CAnimationNode *lpNode = NULL;
    while (pos)
    {
        lpNode = nodes.GetNext(pos);
        if (lpNode->GetNodeName() == pszNodeName)
            return lpNode;
    }

    return NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
// add new node (by name) and link it with parent (by name)
BOOL CAnimationSystem2::AddNewNode(CModelResourceEntry *pModel, LPCSTR pszNodeName,
                                   LPCSTR pszParentNodeName)
{
    CAnimationNode *lpNode = (CAnimationNode *)GetWorld()->GetFigureManager()->CreateData(
        ASTID_ANIMATION_NODE);
    lpNode->SetName(pszNodeName);
    lpNode->SetParentName(pszParentNodeName);
    if (AddNode(pModel, lpNode) == FALSE)
    {
        lpNode->Release();
        return FALSE;
    }
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// add node and link it (all names already set)
BOOL CAnimationSystem2::AddNode(CModelResourceEntry *pModel, CAnimationNode *lpNewNode)
{
    CAnimationNode *lpNode = FindNode(lpNewNode->GetParentName());
    if (lpNode)
        lpNode->AddChildNode(lpNewNode);
    nodes.AddTail(lpNewNode);
    lpNewNode->AddRef();
    lpNewNode->SetMaskName(((CObject3D *)lpObject3D)->GetMaskName());
    lpNewNode->SetComplection(pModel, strength, grace, tall);

    bNeedRecalc = true;

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CAnimationSystem2::RemoveNode( LPCSTR pszNodeName )
// remove animation node by name
BOOL CAnimationSystem2::RemoveNode(LPCSTR pszNodeName)
{
    CAnimationNode *lpNode = FindNode(pszNodeName);
    return RemoveNode(lpNode);
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CAnimationSystem2::RemoveNode( CAnimationNode* lpNode )
// remove animation node
BOOL CAnimationSystem2::RemoveNode(CAnimationNode *lpNode)
{
    if (!lpNode)
        return FALSE;
    CAnimationNode *lpParentNode = lpNode->GetParentNode();
    lpParentNode->RemoveChildNode(lpNode);
    lpNode->Release();
    bNeedRecalc = true;

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CAnimationSystem2::LinkWithFigures( CSimplyArray<CFigure*, CFigure*>& figures )
// link figures with appropriate animation nodes
BOOL CAnimationSystem2::LinkWithFigures(CSimplyArray<CFigure *, CFigure *> &figures)
{
    for (int i = 0; i < figures.GetSize(); i++)
        LinkWithFigure(figures[i]);
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CAnimationSystem2::LinkWithFigure( CFigure* lpFigure )
// find node with the name as figure have and link it
BOOL CAnimationSystem2::LinkWithFigure(CFigure *lpFigure)
{
    POSITION pos = nodes.GetHeadPosition();
    CAnimationNode *lpNode = NULL;
    bNeedRecalc = true;

    while (pos)
    {
        lpNode = nodes.GetNext(pos);
        if (lpNode->GetNodeName() == lpFigure->GetName())
        {
            lpNode->SetFigure(lpFigure);
            return TRUE;
        }
    }

    return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
// void CAnimationSystem2::RelinkNodes()
// relink all skeleton structure, based on names information
void CAnimationSystem2::RelinkNodes()
{
    POSITION pos = nodes.GetHeadPosition();
    CAnimationNode *lpLinkNode = NULL;
    CAnimationNode *lpNode = NULL;
    // first, remove all links from all nodes
    while (pos)
    {
        lpNode = nodes.GetNext(pos);
        lpNode->RemoveAllChildren();
    }
    // then, link each node with other
    pos = nodes.GetHeadPosition();
    while (pos)
    {
        lpNode = nodes.GetNext(pos);
        lpLinkNode = FindNode(lpNode->GetParentName());
        if (lpLinkNode)
            lpLinkNode->AddChildNode(lpNode);
    }
    bNeedRecalc = true;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CAnimationSystem2::SetupHeadNode()
{
    POSITION pos = nodes.GetHeadPosition();
    CAnimationNode *lpNode = NULL;
    //
    bNeedRecalc = true;
    while (pos)
    {
        lpNode = nodes.GetNext(pos);
        if (lpNode->GetParentName().GetLength() == 0)
        {
            lpHeadNode = lpNode;
            return TRUE;
        }
    }

    return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<const owned::ModelCpuAsset> CAnimationSystem2::GetOwnedSkeleton() noexcept try
{
    std::vector<OwnedRender::E1SkeletonNode> facts;
    POSITION pos = nodes.GetHeadPosition();
    while (pos)
    {
        CAnimationNode* node = nodes.GetNext(pos);
        facts.push_back({node, node->GetParentNode(), (LPCSTR)node->GetNodeName(),
            (LPCSTR)node->GetParentName(), node->GetOwnedBone()});
    }
    return ownedSkeleton.Complete(facts, lpHeadNode);
}
catch (...) { return nullptr; }

bool CAnimationSystem2::GetOwnedCurrentAnimationSelection(
    OwnedRender::E1AnimationSelection& output, const char** failedStage) noexcept try
{
    output = {};
    if (failedStage) *failedStage = nullptr;
    auto reject = [&](const char* stage) noexcept {
        if (failedStage) *failedStage = stage;
        return false;
    };
    const SADEntry* selected = lpADEntry;
    if (!selected)
    {
        output.visual.neutral = true;
        output.valid = true;
        return true;
    }
    if (!lpObject3D || !selected->pszAnimationName[0])
        return reject("selection.object_or_entry");
    const char* mask = lpObject3D->GetMaskName();
    CWorld* world = GetWorld();
    if (!mask || !*mask || !world)
        return reject("selection.mask_or_world");

    CString maskName(mask);
    const void* databaseBytes = nullptr;
    DWORD databaseSize = 0;
    auto* mapObjects = static_cast<CMapObjectManager*>(world->GetMapObjectManager());
    if (!mapObjects) return reject("selection.map_object_manager");
    if (!mapObjects->GetSelectedAnimationDatabase(maskName, selected,
        databaseBytes, databaseSize))
        return reject("selection.adb_selection");

    auto* figures = static_cast<CFigureManager*>(world->GetFigureManager());
    if (!figures) return reject("selection.figure_manager");
    CModelResourceEntry* model = figures->GetModel(std::string(mask));
    if (!model) return reject("selection.model_resource");
    const void* animationBytes = nullptr;
    DWORD animationSize = 0;
    if (!model->GetAnimationSourceBytes(selected->pszAnimationName,
        animationBytes, animationSize))
        return reject("selection.anm_resource");

    const std::string animationName(selected->pszAnimationName);
    const std::string databaseName = std::string(mask) + ".adb";
    const std::string animationResourceName = std::string(mask) + "/" + animationName;
    if (!OwnedRender::E1BuildAnimationSelection(databaseName, databaseBytes, databaseSize,
        animationName, animationResourceName, animationBytes, animationSize,
        selected->dwEntryIndex, output))
        return reject("selection.c56_decode");
    return true;
}
catch (...) {
    output = {};
    if (failedStage) *failedStage = "selection.exception";
    return false;
}

void CAnimationSystem2::LoadSkeleton(LPCSTR pszMaskName)
{
    bNeedRecalc = true;
    ((CFigureManager *)GetWorld()->GetFigureManager())->LoadSkeleton(this, pszMaskName);
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CAnimationSystem2::UnlinkFigure( CFigure* lpFigure )
// unlink figure from appropriate animation node
BOOL CAnimationSystem2::UnlinkFigure(CFigure *lpFigure)
{
    POSITION pos = nodes.GetHeadPosition();
    CAnimationNode *lpNode = NULL;
    CFigure *lpNodeFigure = NULL;
    //
    bNeedRecalc = true;
    while (pos)
    {
        lpNode = nodes.GetNext(pos);
        lpNodeFigure = lpNode->GetFigure();
        if (lpNodeFigure && (lpNodeFigure == lpFigure))
        {
            lpNode->SetFigure(NULL);
            return TRUE;
        }
    }

    return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CAnimationSystem2::UnlinkAllFigures()
// unlink all figures: just set figure to NULL for each animation node
BOOL CAnimationSystem2::UnlinkAllFigures()
{
    POSITION pos = nodes.GetHeadPosition();
    CAnimationNode *lpNode = NULL;
    //
    bNeedRecalc = true;
    while (pos)
    {
        lpNode = nodes.GetNext(pos);
        lpNode->SetFigure(NULL);
    }

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CAnimationSystem2::RecalcSkeleton()
{
    if (!bNeedRecalc)
        return;
    bNeedRecalc = false;
    if (lpHeadNode)
        lpHeadNode->RecalcSkeleton(lpObject3D->GetCenterShift());
}
/////////////////////////////////////////////////////////////////////////////////////
// void CAnimationSystem2::RecalcSkeletonWithCurrentTime()
// recal nodes in LCS with current time and recalc skeleton with node's LCS placements
void CAnimationSystem2::RecalcSkeletonWithCurrentTime()
{
    if (!bNeedRecalc)
        return;
    // set time to all animation nodes
    POSITION pos = nodes.GetHeadPosition();
    CAnimationNode *lpNode = NULL;
    //
    while (pos)
    {
        lpNode = nodes.GetNext(pos);
        lpNode->GetCurrentTrack()->SetTime(fpTime);
    }
    // recalc skeleton
    RecalcSkeleton();
}
/////////////////////////////////////////////////////////////////////////////////////
void CAnimationSystem2::SetMainAsCurrent()
{
    POSITION pos = nodes.GetHeadPosition();
    CAnimationNode *lpNode = NULL;
    //
    while (pos)
    {
        lpNode = nodes.GetNext(pos);
        lpNode->SetMainAsCurrent();
    }
    // setup time from current animation track from head node
    if (!nodes.IsEmpty())
        fpTime = nodes.GetHead()->GetCurrentTrack()->GetTime();
    else
        fpTime = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CAnimationSystem2::SetNextAsCurrent()
{
    POSITION pos = nodes.GetHeadPosition();
    CAnimationNode *lpNode = NULL;
    //
    while (pos)
    {
        lpNode = nodes.GetNext(pos);
        lpNode->SetNextAsCurrent();
    }
    // setup time from current animation track from head node
    if (!nodes.IsEmpty())
        fpTime = nodes.GetHead()->GetCurrentTrack()->GetTime();
    else
        fpTime = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CAnimationSystem2::SetAlternateAsCurrent()
{
    POSITION pos = nodes.GetHeadPosition();
    CAnimationNode *lpNode = NULL;
    //
    while (pos)
    {
        lpNode = nodes.GetNext(pos);
        lpNode->SetAlternateAsCurrent();
    }
    // setup time from current animation track from head node
    if (!nodes.IsEmpty())
        fpTime = nodes.GetHead()->GetCurrentTrack()->GetTime();
    else
        fpTime = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CAnimationSystem2::ClearAllAnimations()
{
    ClearAnimationsQueue();
    SetAnimation(0);
    bNeedRecalc = true;
}
/////////////////////////////////////////////////////////////////////////////////////
void CAnimationSystem2::Step(float fpDT)
{
    SetTime(fpTime + fpDT);
}
/////////////////////////////////////////////////////////////////////////////////////
// void CAnimationSystem2::SetTime( float fpNewTime )
// set time and change (if need) animation(s)
void CAnimationSystem2::SetTime(float fpNewTime)
{
    if (fpTime != fpNewTime)
        bNeedRecalc = true;
    const SADEntry *pOldEntry = lpADEntry;

    fpTime = fpNewTime;
    // if no current animation and next animation in queue exist, place it as current and reset time
    if (!lpADEntry && !entries.IsEmpty())
    {
        lpADEntry = entries.RemoveHead();
        fpTime = 0;
        fpNewTime = 0;
    }
    // correct time to fit in animation length and set next animation if need
    while (lpADEntry && (fpTime > lpADEntry->dwAnimationLength))
    {
        fpTime -= float(lpADEntry->dwAnimationLength);
        // call animation switch callback
        if (SwitchCallback)
            (*SwitchCallback)(reinterpret_cast<CUnit *>(lpObject3D));
        //
        if (!entries.IsEmpty())
            lpADEntry = entries.RemoveHead();
        else if (lpADEntry->IsUnique())
            fpTime = float(lpADEntry->dwAnimationLength);
        bNeedRecalc = true;
    }
    // set animation entry for all nodes if animation was changed
    if (pOldEntry != lpADEntry)
    {
        BOOL bRetVal = static_cast<CFigureManager *>(GetWorld()->GetFigureManager())
                           ->LoadAnimation(this, lpObject3D->GetMaskName(), lpADEntry);
        if (bRetVal == FALSE)
        {
            POSITION pos = nodes.GetHeadPosition();
            CAnimationNode *lpNode = NULL;
            //
            while (pos)
            {
                lpNode = nodes.GetNext(pos);
                lpNode->SetAnimation(lpADEntry);
            }
        }
        bNeedRecalc = true;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CAnimationSystem2::SetComplection(float fpStrength, float fpGrace, float fpTall)
{
    CModelResourceEntry *pModel = ((CFigureManager *)GetWorld()->GetFigureManager())
                                      ->GetModel(lpObject3D->GetMaskName());
    // set complection for each node
    POSITION pos = nodes.GetHeadPosition();
    while (pos)
    {
        CAnimationNode *pNode = nodes.GetNext(pos);
        pNode->SetComplection(pModel, fpStrength, fpGrace, fpTall);
    }
    // store complection for further use
    strength = fpStrength;
    grace = fpGrace;
    tall = fpTall;
    // recalc skeleton
    bNeedRecalc = true;
    RecalcSkeleton();
}
/////////////////////////////////////////////////////////////////////////////////////
void CAnimationSystem2::GetNodeNames(CStringArray &szNames) const
{
    POSITION pos = nodes.GetHeadPosition();
    CAnimationNode *lpNode = NULL;
    //
    while (pos)
    {
        lpNode = nodes.GetNext(pos);
        szNames.Add(lpNode->GetNodeName());
    }
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CAnimationSystem2::ClearCurrentAnimation()
{
    bNeedRecalc = true;
    POSITION pos = nodes.GetHeadPosition();
    while (pos)
    {
        CAnimationNode *pNode = nodes.GetNext(pos);
        pNode->ClearCurrentTrack(0);
    }
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CAnimationSystem2::LoadAnimation(CMappedResource *pAnimation, const SADEntry *pADEntry)
{
    bNeedRecalc = true;
    POSITION pos = nodes.GetHeadPosition();
    while (pos)
    {
        CAnimationNode *pNode = nodes.GetNext(pos);
        DWORD dwSize = 0;
        BYTE *pData = pAnimation->GetFile(pNode->GetNodeName(), &dwSize);
        if (pData == 0)
            pNode->ClearCurrentTrack(pADEntry);
        else
            pNode->LoadCurrentTrack(pData, dwSize, pADEntry);
    }
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
