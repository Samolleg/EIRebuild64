#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "Plot.h"
#include "AnimationSystem.h"
#include "Figure.h"
#include "Object.h"
#include "World.h"
#include "FigureManager.h"
/////////////////////////////////////////////////////////////////////////////////////
//************************************************************************************
//                         Save/Load functions for tags
//************************************************************************************
//////////////////////////////////////////////////////////////////////////////////////
void SPositionTrackTag::Save(CFile &file)
{
    file.Write(&nStartFrame, sizeof(nStartFrame));
    file.Write(&nEndFrame, sizeof(nEndFrame));
    file.Write(&position, sizeof(position));
    file.Write(&dr, sizeof(dr));
}
/////////////////////////////////////////////////////////////////////////////////////
void SPositionTrackTag::Load(CFile &file)
{
    file.Read(&nStartFrame, sizeof(nStartFrame));
    file.Read(&nEndFrame, sizeof(nEndFrame));
    file.Read(&position, sizeof(position));
    file.Read(&dr, sizeof(dr));
}
/////////////////////////////////////////////////////////////////////////////////////
void SPositionTrackTag::Load(LPBYTE &lpData)
{
    nStartFrame = *(int *)(lpData + 0);
    nEndFrame = *(int *)(lpData + 4);
    position.SetPlot(*((CPlot3D *)(lpData + 8)));
    dr.SetPlot(*((CPlot3D *)(lpData + 20)));

    lpData += 32;
}
/////////////////////////////////////////////////////////////////////////////////////
void SRotationTrackTag::Save(CFile &file)
{
    file.Write(&nStartFrame, sizeof(nStartFrame));
    file.Write(&nEndFrame, sizeof(nEndFrame));
    rotation.Save(file);
    file.Write(&axis, sizeof(axis));
    file.Write(&angle, sizeof(angle));
}
/////////////////////////////////////////////////////////////////////////////////////
void SRotationTrackTag::Load(CFile &file)
{
    file.Read(&nStartFrame, sizeof(nStartFrame));
    file.Read(&nEndFrame, sizeof(nEndFrame));
    rotation.Load(file);
    file.Read(&axis, sizeof(axis));
    file.Read(&angle, sizeof(angle));
}
/////////////////////////////////////////////////////////////////////////////////////
void SRotationTrackTag::Load(LPBYTE &lpData)
{
    nStartFrame = *(int *)(lpData + 0);
    nEndFrame = *(int *)(lpData + 4);
    lpData += 8;
    rotation.Load(lpData);
    axis.SetPlot(*((CPlot3D *)lpData));
    angle = *(float *)(lpData + 12);

    lpData += 16;
}
/////////////////////////////////////////////////////////////////////////////////////
void SMorphingTrackTag::Save(CFile &file)
{
    DWORD num;

    file.Write(&nStartFrame, sizeof(nStartFrame));
    file.Write(&nEndFrame, sizeof(nEndFrame));
    num = shifts.GetSize();
    file.Write(&num, sizeof(num));
    if (num != 0)
        file.Write(shifts.GetData(), sizeof(CPlot3D) * num);
}
/////////////////////////////////////////////////////////////////////////////////////
void SMorphingTrackTag::Load(CFile &file)
{
    DWORD num;

    file.Read(&nStartFrame, sizeof(nStartFrame));
    file.Read(&nEndFrame, sizeof(nEndFrame));
    file.Read(&num, sizeof(num));
    shifts.SetSize(num);
    if (num != 0)
        file.Read(shifts.GetData(), sizeof(CPlot3D) * num);
}
/////////////////////////////////////////////////////////////////////////////////////
void SMorphingTrackTag::Load(LPBYTE &lpData)
{
    DWORD num;

    nStartFrame = *(int *)(lpData + 0);
    nEndFrame = *(int *)(lpData + 4);
    num = *(DWORD *)(lpData + 8);
    shifts.SetSize(num);
    if (num != 0)
        memcpy(shifts.GetData(), lpData + 12, sizeof(CPlot3D) * num);

    lpData += 12 + sizeof(CPlot3D) * num;
}
/////////////////////////////////////////////////////////////////////////////////////
//************************************************************************************
//                       SAnimationNode Save/Load functions
//************************************************************************************
//////////////////////////////////////////////////////////////////////////////////////
void SAnimationNode::LoadComplex(CFile &file)
{
    CPlot3D plot1, plot2;
    DWORD num;
    int i;
    // read init rotation
    initRotation.Load(file);
    // read init position
    file.Read(&initPosition, sizeof(initPosition));
    // read init morphing
    file.Read(&num, sizeof(num));
    initMorphing.SetSize(num);
    if (num > 0)
        file.Read(initMorphing.GetData(), sizeof(initMorphing[0]) * num);
    // general animation information
    file.Read(&nAnimationLength, sizeof(nAnimationLength));
    // load rotation tags
    file.Read(&num, sizeof(num));
    rotations.SetSize(num);
    for (i = 0; i < rotations.GetSize(); i++)
        rotations[i].Load(file);
    // load position tags
    file.Read(&num, sizeof(num));
    positions.SetSize(num);
    for (i = 0; i < positions.GetSize(); i++)
        positions[i].Load(file);
    // load morphing tags
    file.Read(&num, sizeof(num));
    morphings.SetSize(num);
    for (i = 0; i < morphings.GetSize(); i++)
        morphings[i].Load(file);
}
/////////////////////////////////////////////////////////////////////////////////////
void SAnimationNode::LoadComplex(LPBYTE &lpData)
{
    DWORD num;
    int i;
    // read init rotation
    initRotation.Load(lpData);
    // read init position
    initPosition.SetPlot(*((CPlot3D *)lpData));
    lpData += sizeof(CPlot3D);
    // read init morphing
    num = *(DWORD *)lpData;
    initMorphing.SetSize(num);
    morphing.SetSize(num);
    if (num > 0)
    {
        memcpy(initMorphing.GetData(), lpData + 4, sizeof(initMorphing[0]) * num);
        memcpy(morphing.GetData(), lpData + 4, sizeof(morphing[0]) * num);
    }
    lpData += 4 + sizeof(initMorphing[0]) * num;
    // general animation information
    nAnimationLength = *(int *)lpData;
    lpData += 4;
    // load rotation tags
    num = *(DWORD *)lpData;
    lpData += 4;
    rotations.SetSize(num);
    for (i = 0; i < rotations.GetSize(); i++)
        rotations[i].Load(lpData);
    // load position tags
    num = *(DWORD *)lpData;
    lpData += 4;
    positions.SetSize(num);
    for (i = 0; i < positions.GetSize(); i++)
        positions[i].Load(lpData);
    // load morphing tags
    num = *(DWORD *)lpData;
    lpData += 4;
    morphings.SetSize(num);
    for (i = 0; i < morphings.GetSize(); i++)
        morphings[i].Load(lpData);
}
/////////////////////////////////////////////////////////////////////////////////////
void SAnimationNode::LoadRecurse(LPCSTR pszMask, float strength, float grace, float tall)
{
    ((CFigureManager *)((CWorld *)lpAnimationSystem->GetObject3D()->GetWorld())->GetFigureManager())
        ->MakeAnimationNode(this, pszMask, strength, grace, tall);
    //	((CFigureManager*)theWorld.GetFigureManager())->MakeAnimationNode( this, pszMask, strength,
    // grace, tall );

    for (int i = 0; i < children.GetSize(); i++)
    {
        if (children[i])
            children[i]->LoadRecurse(pszMask, strength, grace, tall);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void SAnimationNode::LoadLinks(CFile &file)
{
    int num;
    // load name
    file.Read(&num, sizeof(num));
    if (num != 0)
        file.Read(szName.GetBufferSetLength(num), num);
    else
        szName.Empty();
    // load parent name
    file.Read(&num, sizeof(num));
    if (num != 0)
        file.Read(szParentName.GetBufferSetLength(num), num);
    else
        szParentName.Empty();
}
/////////////////////////////////////////////////////////////////////////////////////
void SAnimationNode::LoadBone(CFile &file, float strength, float grace, float tall)
{
    CPlot3D plot1, plot2;
    CPlot3D plots[6];
    //
    this->strength = strength;
    this->grace = grace;
    this->tall = tall;
    //
    file.Read(plots, sizeof(CPlot3D) * 6);
    plot1 = plots[0] + strength * (plots[1] - plots[0]);
    plot1 += grace * (plots[2] - plot1);
    plot2 = plots[3] + strength * (plots[4] - plots[3]);
    plot2 += grace * (plots[5] - plot2);
    bone = plot1 + tall * (plot2 - plot1);
}
/////////////////////////////////////////////////////////////////////////////////////
void SAnimationNode::LoadBone(LPBYTE lpData, float strength, float grace, float tall)
{
    CPlot3D plot1, plot2;
    CPlot3D *plots = (CPlot3D *)lpData;
    //
    this->strength = strength;
    this->grace = grace;
    this->tall = tall;
    //
    plot1 = plots[0] + strength * (plots[1] - plots[0]);
    plot1 += grace * (plots[2] - plot1);
    plot2 = plots[3] + strength * (plots[4] - plots[3]);
    plot2 += grace * (plots[5] - plot2);
    bone = plot1 + tall * (plot2 - plot1);
}
/////////////////////////////////////////////////////////////////////////////////////
//************************************************************************************
//                        SAnimationNode functions and methods
//************************************************************************************
//////////////////////////////////////////////////////////////////////////////////////
SAnimationNode::SAnimationNode()
{
    lpAnimationSystem = NULL;
    dwFlags = 0;
    lpFigure = NULL;
    fpTime = 0;
    nTime = 0;
    nAnimationLength = 0;
    nCurrentRotationTag = -1;
    nCurrentPositionTag = -1;
    nCurrentMorphingTag = -1;
    nCurrentScaleTag = -1;
    pParent = NULL;

    bone = VNULL;
    dr = VNULL;
    dq = QNULL;

    lpADEntry = NULL;
    lpNextADEntry = NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
SAnimationNode::~SAnimationNode()
{
    int i;
    // unlink from figure
    if (lpFigure)
    {
        lpFigure->DecRefCount();
        lpFigure = NULL;
    }
    // delete all children
    for (i = 0; i < children.GetSize(); i++)
    {
        if (children[i])
        {
            delete children[i];
            children[i] = NULL;
        }
    }
    children.RemoveAll();
    lpAnimationSystem = NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL SAnimationNode::RecalcSkeleton()
// recalc skeleton...
BOOL SAnimationNode::RecalcSkeleton()
{
    double fpTimeStep = 0;
    PrepareFPStep(fpTimeStep);
    Step();
    for (int i = 0; i < children.GetSize(); i++)
        children[i]->RecalcSkeleton();
    SetFigurePlacement();

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL SAnimationNode::Step()
// do one step with already prepared data
BOOL SAnimationNode::Step()
{
    CPlot3D dl;
    if (pParent)
    {
        dl = pParent->rotation.Rotate(bone);
        position = pParent->position + dl;
        rotation = dq * pParent->rotation;
    }
    else
    {
        position = dr;
        rotation = dq;
    }

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// void SAnimationNode::PrepareIncrementStep()
// prepare data for increment step
void SAnimationNode::PrepareIncrementStep()
{
    nTime++;
    if (nTime >= nAnimationLength)
    {
        while (nTime >= nAnimationLength)
            nTime -= nAnimationLength;

        nCurrentMorphingTag = nTime;
        nCurrentPositionTag = nTime;
        nCurrentRotationTag = nTime;
        nCurrentScaleTag = nTime;

        if ((nTime >= nAnimationLength) && (nAnimationLength > 0) && IsNeedNextAnimation())
            ChangeAnimationNow();
    }
    // step morphing tags
    if ((morphings.GetSize() > 0) && (nCurrentMorphingTag < morphings.GetSize()))
    {
        MorphingIncrementStep(nCurrentMorphingTag, nCurrentMorphingTag + 1);
        nCurrentMorphingTag++;
    }
    // step position tags
    if ((positions.GetSize() > 0) && (nCurrentPositionTag < positions.GetUpperBound()))
    {
        nCurrentPositionTag++;
        position = positions[nCurrentPositionTag].position;
    }
    // step rotation tags
    if ((rotations.GetSize() > 0) && (nCurrentRotationTag < rotations.GetUpperBound()))
    {
        nCurrentRotationTag++;
        dq = rotations[nCurrentRotationTag].rotation;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL SAnimationNode::IncrementStep()
// do increment step:
//   1. prepare increment step data
//   2. do step
//   3. do the same for each children
//   4. set figure placement
BOOL SAnimationNode::IncrementStep()
{
    int i, nMorphings;

    PrepareIncrementStep();
    // do step with prepared data
    Step();
    // do morphing
    if (nCurrentMorphingTag < morphings.GetSize())
    {
        nMorphings = morphings[nCurrentMorphingTag].shifts.GetSize();

        for (i = 0; i < nMorphings; i++)
            morphing[i] += morphings[nCurrentMorphingTag].shifts[i];
    }
    // position children of this node
    for (i = 0; i < children.GetSize(); i++)
        children[i]->IncrementStep();

    SetFigurePlacement();

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void SAnimationNode::MorphingIncrementStep(int nFrom, int nTo)
{
    if (morphings.GetSize())
    {
        for (int i = nFrom; i < nTo; i++)
        {
            for (int j = 0; j < morphings[i].shifts.GetSize(); j++)
                morphing[j] += morphings[i].shifts[j];
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void SAnimationNode::ResetMorphing()
{
    morphing.Copy(initMorphing);
    //	MemSetDWord( (DWORD*)morphing.GetData(), 0, morphing.GetSize()*3 );
}
/////////////////////////////////////////////////////////////////////////////////////
// void SAnimationNode::PrepareFPStep( double fpFrame )
// prepare data for fp step (0 < fpFrame < 1)
void SAnimationNode::PrepareFPStep(double &fpFrame)
{
    CPlot3D axis;
    double fpFrame1;
    //
    fpTime += fpFrame;
    if (fpTime >= 1.0f)
    {
        nTime++;
        fpTime--;
        // step morphing tags
        if ((morphings.GetSize() > 0) && (nCurrentMorphingTag < morphings.GetSize()))
        {
            if (nTime >= morphings[nCurrentMorphingTag].nEndFrame)
                nCurrentMorphingTag++;
        }
        // step position tags
        if ((positions.GetSize() > 0) && (nCurrentPositionTag < positions.GetUpperBound()))
        {
            if (nTime >= positions[nCurrentPositionTag].nEndFrame)
                nCurrentPositionTag++;
        }
        // step rotation tags
        if ((rotations.GetSize() > 0) && (nCurrentRotationTag < rotations.GetUpperBound()))
        {
            if (nTime >= rotations[nCurrentRotationTag].nEndFrame)
                nCurrentRotationTag++;
        }
        // step scale tags
        if ((scales.GetSize() > 0) && (nCurrentScaleTag < scales.GetUpperBound()))
        {
            if (nTime >= nCurrentScaleTag + 1)
                nCurrentScaleTag++;
        }
        //
        if ((nTime >= nAnimationLength) && (nAnimationLength > 0) && IsNeedNextAnimation())
        {
            ChangeAnimationNow();
            nTime++;
        }
    }
    fpFrame1 = fpTime;
    ///////////////////////////////////////////////////////////////////////////////////
    // check for animation cycle
    if ((nTime >= nAnimationLength) && (nAnimationLength > 0) && lpADEntry->IsCycled())
    {
        while (nTime >= nAnimationLength)
            nTime -= nAnimationLength;

        ResetMorphing();
        MorphingIncrementStep(0, nTime);
        nCurrentMorphingTag = nTime;
        nCurrentPositionTag = nTime;
        nCurrentRotationTag = nTime;
        nCurrentScaleTag = nTime;
    }
    ////////////////////////////////////////////////////////////////////////////////////
    // prepare position step
    if ((positions.GetSize() > 0) && (nCurrentPositionTag < positions.GetUpperBound()))
    {
        // set shift step
        if (nCurrentPositionTag < positions.GetSize())
        {
            dr = positions[nCurrentPositionTag].dr * float(fpFrame1);
            dr = positions[nCurrentPositionTag].position + dr;
        }
        else
            dr = positions[0].position;
    }
    else if (positions.GetSize())
        dr = positions[positions.GetUpperBound()].position;
    ////////////////////////////////////////////////////////////////////////////////////
    // prepare rotation step
    if ((rotations.GetSize() > 0) && (nCurrentRotationTag < rotations.GetUpperBound()))
    {
        // set rotation step
        if (nCurrentRotationTag < rotations.GetSize())
        {
            dq.FromAxisAngle(rotations[nCurrentRotationTag].axis,
                             rotations[nCurrentRotationTag].angle * float(fpFrame1));
            dq *= rotations[nCurrentRotationTag].rotation;
        }
        else
            dq = QNULL;
    }
    else if (rotations.GetSize())
        dq = rotations[rotations.GetUpperBound()].rotation;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL SAnimationNode::FPStep( double fpFrame )
// do fp step (see increment step)
BOOL SAnimationNode::FPStep(double fpFrame)
{
    float fpMorphFrame;
    int i, nMorphings;

    // do morphing step
    if (nCurrentMorphingTag < morphings.GetSize())
    {
        fpMorphFrame = float(fpFrame);
        nMorphings = morphings[nCurrentMorphingTag].shifts.GetSize();

        for (i = 0; i < nMorphings; i++)
            morphing[i] += fpMorphFrame * morphings[nCurrentMorphingTag].shifts[i];
    }
    // prepare step
    PrepareFPStep(fpFrame);
    // do step with prepared data
    Step();
    // position children of this node
    for (i = 0; i < children.GetSize(); i++)
        children[i]->FPStep(fpFrame);

    SetFigurePlacement();

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL SAnimationNode::LinkWithFigures( CSimplyArray<CFigure*, CFigure*>& figures )
// function for linking with appropriate figure
BOOL SAnimationNode::LinkWithFigures(CSimplyArray<CFigure *, CFigure *> &figures)
{
    int i;
    // find figure for this link
    for (i = 0; i < figures.GetSize(); i++)
    {
        if (strcmp(figures[i]->GetName(), szName) == 0)
        {
            lpFigure = figures[i];
            lpFigure->AddRef();
            SetFigurePlacement();
            break;
        }
    }
    // link all children
    for (i = 0; i < children.GetSize(); i++)
    {
        if (children[i])
            children[i]->LinkWithFigures(figures);
    }

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL SAnimationNode::LinkWithFigure(CFigure *lpLinkFigure)
{
    int i;
    // find figure for this link
    if (strcmp(lpLinkFigure->GetName(), szName) == 0)
    {
        lpFigure = lpLinkFigure;
        lpFigure->AddRef();
        SetFigurePlacement();
        return TRUE;
    }
    // link all children
    for (i = 0; i < children.GetSize(); i++)
    {
        if (children[i])
            children[i]->LinkWithFigure(lpLinkFigure);
    }

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL SAnimationNode::SetFigurePlacement()
// set figure placement from this node current state
BOOL SAnimationNode::SetFigurePlacement()
{
    if (lpFigure)
    {
        lpFigure->SetCP(position);
        lpFigure->SetOrientation(rotation);
        lpFigure->SetMorphs(morphing);

        return TRUE;
    }

    return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
// void SAnimationNode::SetInitState()
// set init state for position, rotation and morphing from appropriate init values
void SAnimationNode::SetInitState()
{
    if (pParent)
    {
        rotation = pParent->rotation * rotations[0].rotation;
        CPlot3D dl = pParent->rotation.Rotate(bone);
        position = pParent->position + dl;
    }
    else
    {
        rotation = rotations[0].rotation;
        position = rotation.Rotate(bone);
    }

    morphing.Copy(initMorphing);

    // current tags (position, rotation and morphing)
    nCurrentPositionTag = 0;
    nCurrentRotationTag = 0;
    nCurrentScaleTag = 0;
    nCurrentMorphingTag = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
// void SAnimationNode::Init()
// init this node state
void SAnimationNode::Init()
{
    int i;
    // set time to 0
    fpTime = 0;
    // set initial animation values
    SetInitState();
    // apply this state to figure
    SetFigurePlacement();
    // do the same for each child
    for (i = 0; i < children.GetSize(); i++)
        children[i]->Init();
}
/////////////////////////////////////////////////////////////////////////////////////
// void SAnimationNode::SetDefaultInitValues()
// set default values for null-position and null-rotation
void SAnimationNode::SetDefaultInitValues()
{
    // position
    if (positions.GetSize() == 0)
    {
        positions.SetSize(1);
        positions[0].position = VNULL;
        positions[0].dr = VNULL;
        positions[0].nStartFrame = 0;
        positions[0].nEndFrame = 1;
    }
    // rotation
    if (rotations.GetSize() == 0)
    {
        rotations.SetSize(1);
        rotations[0].rotation = QNULL;
        rotations[0].axis = CPlot3D(1, 0, 0);
        rotations[0].angle = 0;
        rotations[0].nStartFrame = 0;
        rotations[0].nEndFrame = 1;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// void SAnimationNode::CountNodes( int& nNodes )
// count number of nodes (for save skeleton function only)
void SAnimationNode::CountNodes(int &nNodes)
{
    int i;

    nNodes++;
    for (i = 0; i < children.GetSize(); i++)
    {
        if (children[i])
            children[i]->CountNodes(nNodes);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// void SAnimationNode::LoadNextAnimation()
// load next animation from szNextAnimationName
void SAnimationNode::LoadNextAnimation()
{
    SetAnimation(lpNextADEntry);
    //	ResetNeedNextAnimation();
    ((CFigureManager *)((CWorld *)lpAnimationSystem->GetObject3D()->GetWorld())->GetFigureManager())
        ->LoadAnimation(this);
}
/////////////////////////////////////////////////////////////////////////////////////
// void SAnimationNode::ChangeAnimationNow()
// change animation NOW, without waiting for the current animation end
void SAnimationNode::ChangeAnimationNow()
{
    lpAnimationSystem->SetNextAnimationForNode(this);
    //
    if (lpADEntry == lpNextADEntry)
    {
        ResetNeedNextAnimation();
        return;
    }
    LoadNextAnimation();
    ResetCurrentTags();
    nTime = 0;
    fpTime = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void SAnimationNode::GetNodeNameRecurse(CStringArray &szNames)
{
    szNames.Add(GetNodeName());
    for (int i = 0; i < children.GetSize(); i++)
        children[i]->GetNodeNameRecurse(szNames);
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL SAnimationNode::RemoveNodeRecurse( SAnimationNode* lpNode )
// remove node from animation nodes tree structure
BOOL SAnimationNode::RemoveNodeRecurse(SAnimationNode *lpNode)
{
    int i;

    for (i = 0; i < children.GetSize(); i++)
    {
        if (children[i] == lpNode)
        {
            children.RemoveAt(i);
            return TRUE;
        }
        else
            children[i]->RemoveNodeRecurse(lpNode);
    }

    return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL SAnimationNode::RemoveNodeRecurse( LPCSTR pszNodeName )
// remove node from animation nodes tree structure
SAnimationNode *SAnimationNode::RemoveNodeRecurse(LPCSTR pszNodeName)
{
    SAnimationNode *lpNode = NULL;
    int i;

    for (i = 0; i < children.GetSize(); i++)
    {
        if (children[i]->szName == pszNodeName)
        {
            lpNode = children[i];
            children.RemoveAt(i);
            return lpNode;
        }
        else
            children[i]->RemoveNodeRecurse(pszNodeName);
    }

    return NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
void SAnimationNode::ResetCurrentTags()
{
    nCurrentRotationTag = 0;
    nCurrentPositionTag = 0;
    nCurrentScaleTag = 0;
    nCurrentMorphingTag = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
//************************************************************************************
//                     CAnimationSystem functions and methods
//************************************************************************************
//////////////////////////////////////////////////////////////////////////////////////
CAnimationSystem::CAnimationSystem()
{
    lpObject3D = NULL;
    pHeadNode = NULL;
    time = -1;
    szAnimationName.Empty();
    szNextAnimations.RemoveAll();
    lpADEntry = NULL;
    adEntries.RemoveAll();
}
/////////////////////////////////////////////////////////////////////////////////////
CAnimationSystem::~CAnimationSystem()
{
    Destroy();
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CAnimationSystem::Create()
{
    DeleteNodes();
    time = 0;

    szAnimationName = "none";
    szNextAnimations.RemoveAll();
    nAnimationChangedCounter = 0;

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CAnimationSystem::Destroy()
{
    DeleteNodes();
    SetObject3D(NULL);
    szNextAnimations.RemoveAll();
}
/////////////////////////////////////////////////////////////////////////////////////
// void CAnimationSystem::SetObject3D( LPGAMEBASE lpNewObject3D )
// set object 3d, this animation system belong to
void CAnimationSystem::SetObject3D(LPGAMEBASE lpNewObject3D)
{
    lpObject3D = lpNewObject3D;
}
/////////////////////////////////////////////////////////////////////////////////////
// void CAnimationSystem::DeleteNodes()
// delete all animation nodes
void CAnimationSystem::DeleteNodes()
{
    if (pHeadNode)
    {
        delete pHeadNode;
        pHeadNode = NULL;
        nodes.RemoveAll();
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// void CAnimationSystem::Init()
// init animation system state
void CAnimationSystem::Init()
{
    if (pHeadNode)
        pHeadNode->Init();

    time = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CAnimationSystem::AnimationPosition( double time )
// set animation state to the desired time
BOOL CAnimationSystem::AnimationPosition(float fpTime)
{
    time = fpTime;

    Init();

    if (time > 0)
        AnimationStepFwd(time);

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CAnimationSystem::AnimationStepFwd( double dt )
// progress animation one step forward with desired time step
BOOL CAnimationSystem::AnimationStepFwd(double dt)
{
    double fpFloorTime = floor(time);
    double dTime;

    if (pHeadNode)
    {
        //		pHeadNode->FPStep( 1.0f );
        if (time + dt <= fpFloorTime + 1)
            pHeadNode->FPStep(dt);
        else
        {
            dTime = fpFloorTime + 1.0 - time;
            if (dTime > 1e-5)
                pHeadNode->FPStep(dTime); // shift to integer position
            dTime = dt - dTime;
            while (dTime >= 1)
            {
                pHeadNode->FPStep(1); // do neccessary amount of increment steps
                dTime--;
            }
            if (dTime > 1e-5)
                pHeadNode->FPStep(dTime); // shift to fp position to fit in desired one
        }
    }
    time += dt;

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CAnimationSystem::AnimationStepBwd( float dt )
// progress animation one step backward with desired time step
BOOL CAnimationSystem::AnimationStepBwd(float dt)
{
    time -= dt;

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CAnimationSystem::LinkWithFigures( CSimplyArray<CFigure*, CFigure*>& figures )
// link animation node with appropriate figures
BOOL CAnimationSystem::LinkWithFigures(CSimplyArray<CFigure *, CFigure *> &figures)
{
    if (pHeadNode)
        pHeadNode->LinkWithFigures(figures);

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// void CAnimationSystem::Load( LPCSTR pszMask, LPCSTR pszAnimName, float strength, float grace,
// float tall ) load animation nodes with complection fit
void CAnimationSystem::Load(LPCSTR pszMask, SADEntry *lpNewADEntry, float strength, float grace,
                            float tall)
{
    if (pHeadNode)
    {
        SetAnimation(lpNewADEntry);
        pHeadNode->LoadRecurse(pszMask, strength, grace, tall);
        InitAnimationChangedCounter();
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// void CAnimationSystem::LoadSkeleton( LPCSTR pszName )
// load skeleton structure
void CAnimationSystem::LoadSkeleton(LPCSTR pszName)
{
    // delete old animation nodes
    DeleteNodes();
    //
    ((CFigureManager *)((CWorld *)lpObject3D->GetWorld())->GetFigureManager())
        ->LoadSkeleton(this, pszName);
}
/////////////////////////////////////////////////////////////////////////////////////
// void CAnimationSystem::LoadSkeleton( CFile& file )
// load skeleton structure
void CAnimationSystem::LoadSkeleton(CFile &file)
{
    SAnimationNode *pNode = NULL;
    int i, j, num;
    // first, read number of nodes to load
    file.Read(&num, sizeof(num));
    nodes.SetSize(num);
    // read nodes and compose it to the list
    for (i = 0; i < num; i++)
    {
        // create one animation node and load link information
        pNode = dbgnew SAnimationNode;
        pNode->LoadLinks(file);
        // trying to link this node with another
        for (j = i - 1; j >= 0; j--)
        {
            if (pNode->szParentName == nodes[j]->szName)
            {
                pNode->pParent = nodes[j];
                nodes[j]->children.Add(pNode);
                break;
            }
        }
        // add this node to the tile of the list
        nodes[i] = pNode;
        pNode->lpAnimationSystem = this;
    }
    //
    pHeadNode = nodes[0];
}
/////////////////////////////////////////////////////////////////////////////////////
void CAnimationSystem::GetNodeNames(CStringArray &szNames)
{
    if (pHeadNode)
        pHeadNode->GetNodeNameRecurse(szNames);
}
/////////////////////////////////////////////////////////////////////////////////////
SAnimationNode *CAnimationSystem::FindNode(LPCSTR pszNodeName)
{
    int i;

    for (i = 0; i < nodes.GetSize(); i++)
    {
        if (nodes[i]->GetNodeName() == pszNodeName)
            return nodes[i];
    }

    return NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CAnimationSystem::AddNode( SAnimationNode* lpNode )
// add existed node with linking
// return TRUE if can link, else return FALSE and don't add
BOOL CAnimationSystem::AddNode(SAnimationNode *lpNode)
{
    int i;

    for (i = nodes.GetUpperBound(); i >= 0; i--)
    {
        if (lpNode->szParentName == nodes[i]->szName)
        {
            lpNode->pParent = nodes[i];
            nodes[i]->children.Add(lpNode);
            nodes.Add(lpNode);
            lpNode->lpAnimationSystem = this;

            return TRUE;
        }
    }

    return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
// create new node (empty) and trying to link it with desired parent.
// if can, return TRUE, else delete node and return FALSE
BOOL CAnimationSystem::AddNewNode(LPCSTR pszNodeName, LPCSTR pszParentNodeName)
{
    SAnimationNode *pNode = dbgnew SAnimationNode;
    // set link info
    pNode->szName = pszNodeName;
    pNode->szParentName = pszParentNodeName;
    // trying to link this node with another
    if (AddNode(pNode) == FALSE)
    {
        delete pNode;
        return FALSE;
    }
    else
        return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CAnimationSystem::RemoveNode( SAnimationNode* lpNode )
// remove desired node from animation system
BOOL CAnimationSystem::RemoveNode(SAnimationNode *lpNode)
{
    int i;

    for (i = 0; i < nodes.GetSize(); i++)
    {
        if (nodes[i] == lpNode)
        {
            nodes.RemoveAt(i);
            if (pHeadNode)
                pHeadNode->RemoveNodeRecurse(lpNode);
            lpNode->lpAnimationSystem = NULL;

            return TRUE;
        }
    }

    return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
// SAnimationNode* CAnimationSystem::RemoveNode( LPCSTR pszNodeName )
// remove desired node (by name) from animation system
SAnimationNode *CAnimationSystem::RemoveNode(LPCSTR pszNodeName)
{
    SAnimationNode *lpNode = NULL;
    int i;

    for (i = 0; i < nodes.GetSize(); i++)
    {
        if (nodes[i]->szName == pszNodeName)
        {
            nodes.RemoveAt(i);
            if (pHeadNode)
                lpNode = pHeadNode->RemoveNodeRecurse(pszNodeName);
            if (lpNode)
                lpNode->lpAnimationSystem = NULL;

            return lpNode;
        }
    }

    return NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
// void CAnimationSystem::RelinkNodes()
// relink nodes with each other
void CAnimationSystem::RelinkNodes()
{
    int i, j;
    // remove old links
    for (i = 0; i < nodes.GetSize(); i++)
    {
        nodes[i]->children.RemoveAll();
        nodes[i]->pParent = NULL;
    }
    // relink
    for (i = 0; i < nodes.GetSize(); i++)
    {
        for (j = 0; j < nodes.GetSize(); j++)
        {
            if (nodes[i]->szParentName == nodes[j]->szName)
            {
                nodes[i]->pParent = nodes[j];
                nodes[j]->children.Add(nodes[i]);
            }
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// void CAnimationSystem::ResizeNodes( float strength, float grace, float tall )
// resize all animation nodes
void CAnimationSystem::ResizeNodes(float strength, float grace, float tall)
{
    for (int i = 0; i < nodes.GetSize(); i++)
        ((CFigureManager *)((CWorld *)lpObject3D->GetWorld())->GetFigureManager())
            ->ResizeAnimationNode(nodes[i], strength, grace, tall);
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CAnimationSystem::Unlink( CFigure* lpFigure )
// unlink specific figure from appropriate animation node
BOOL CAnimationSystem::Unlink(CFigure *lpFigure)
{
    for (int i = 0; i < nodes.GetSize(); i++)
    {
        if (nodes[i]->lpFigure == lpFigure)
        {
            nodes[i]->lpFigure->DecRefCount();
            nodes[i]->lpFigure = NULL;
            return TRUE;
        }
    }
    return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CAnimationSystem::UnlinkAllFigures()
// unlink all figures from animation system
BOOL CAnimationSystem::UnlinkAllFigures()
{
    for (int i = 0; i < nodes.GetSize(); i++)
    {
        if (nodes[i]->lpFigure != NULL)
        {
            nodes[i]->lpFigure->DecRefCount();
            nodes[i]->lpFigure = NULL;
        }
    }
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// void CAnimationSystem::RecalcSkeleton()
// recalc skeleton...
void CAnimationSystem::RecalcSkeleton()
{
    if (pHeadNode)
        pHeadNode->RecalcSkeleton();
}
/////////////////////////////////////////////////////////////////////////////////////
void CAnimationSystem::SetAnimation(SADEntry *lpNewEntry)
{
    lpADEntry = lpNewEntry;
    for (int i = 0; i < nodes.GetSize(); i++)
        nodes[i]->SetAnimation(lpNewEntry);
}
/////////////////////////////////////////////////////////////////////////////////////
void CAnimationSystem::SetNextAnimation(SADEntry *lpNewEntry, BOOL bNow)
{
    if (pHeadNode)
    {
        int i;

        AddNextAnimation(lpNewEntry);
        if (bNow)
        {
            for (i = 0; i < nodes.GetSize(); i++)
                nodes[i]->ChangeAnimationNow();
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CAnimationSystem::GetLinkedNodeNames(CStringArray &szNames)
{
    for (int i = 0; i < nodes.GetSize(); i++)
    {
        if (nodes[i]->lpFigure != NULL)
            szNames.Add(nodes[i]->szName);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CAnimationSystem::InitAnimationChangedCounter()
{
    nAnimationChangedCounter = nodes.GetSize();
    /*
    for ( int i=0; i<nodes.GetSize(); i++ )
    {
            if ( nodes[i]->nAnimationLength > 0 )
                    nAnimationChangedCounter++;
    }
    */
}
/////////////////////////////////////////////////////////////////////////////////////
void CAnimationSystem::SetAnimationForNode(SAnimationNode *lpNode)
{
    lpNode->SetNextAnimation(lpADEntry);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CAnimationSystem::SetNextAnimationForNode(SAnimationNode *lpNode)
{
    if (!szNextAnimations.GetSize() && !adEntries.GetSize())
        return FALSE;
    // change animation for node, if 'nAnimationChangedCounter' more then one, and decrease counter
    if (nAnimationChangedCounter)
    {
        ((CObject3D *)lpObject3D)->SetCurrentAnimationEntry(adEntries[0]);
        lpNode->SetNextAnimation(adEntries[0]);
        //		lpNode->SetNextAnimationName( szNextAnimations[0] );
        lpNode->SetNeedNextAnimation();
        nAnimationChangedCounter--;
    }
    // if 'nAnimationChangedCounter' eq to zero, then remove first animation in queue and set
    // counter to number of nodes
    if (!nAnimationChangedCounter)
    {
        InitAnimationChangedCounter();
        //		szNextAnimations.RemoveAt( 0 );
        adEntries.RemoveAt(0);
    }

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CAnimationSystem::AddNextAnimation(CString &szName)
{
    int i = nodes.GetSize();
    while (i)
    {
        i--;
        nodes[i]->SetNeedNextAnimation();
    }

    szNextAnimations.Add('u' + szName);
}
/////////////////////////////////////////////////////////////////////////////////////
void CAnimationSystem::AddNextAnimation(SADEntry *lpNewEntry)
{
    int i = nodes.GetSize();
    while (i)
    {
        i--;
        nodes[i]->SetNeedNextAnimation();
    }

    adEntries.Add(lpNewEntry);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CAnimationSystem::LinkWithFigure(CFigure *lpFigure)
{
    if (pHeadNode)
        pHeadNode->LinkWithFigure(lpFigure);

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
