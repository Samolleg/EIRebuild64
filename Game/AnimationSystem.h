#pragma once
/////////////////////////////////////////////////////////////////////////////////////
#include "GameBase.h"

#include "CMappedResource.h"

#include "Plot.h"
#include "Quaternion.h"

#include "adFormat.h"
/////////////////////////////////////////////////////////////////////////////////////
class CFigure;
class CAnimationSystem;

struct SAnimationNode;
typedef CSimplyArray<SAnimationNode *, SAnimationNode *&> CAnimationNodePtrArray;
/////////////////////////////////////////////////////////////////////////////////////
// struct SPositionTrackTag
// contain position key frame for the single figure
struct SPositionTrackTag
{
public:
    int nStartFrame;  // starting frame of this tag
    int nEndFrame;    // finishing frame of this tag
    CPlot3D position; // position
    CPlot3D dr;       // shift
public:
    void Save(CFile &file);
    void Load(CFile &file);
    void Load(LPBYTE &lpData);
};
typedef CSimplyArray<SPositionTrackTag, SPositionTrackTag &> CPositionTrackTagArray;
/////////////////////////////////////////////////////////////////////////////////////
// struct SRotationTrackTag
// contain rotation key frame for the single figure
struct SRotationTrackTag
{
public:
    int nStartFrame;      // starting frame of this tag
    int nEndFrame;        // finishing frame of this tag
    CQuaternion rotation; // rotation, respect to parent in start frame
    CPlot3D axis;         // work information - need during convertion
    float angle;          // the same
public:
    void Save(CFile &file);
    void Load(CFile &file);
    void Load(LPBYTE &lpData);
};
typedef CSimplyArray<SRotationTrackTag, SRotationTrackTag &> CRotationTrackTagArray;

// --------------------------------------------------------------------------------------------
// contain morphing key frame for the single figure
// --------------------------------------------------------------------------------------------
struct SMorphingTrackTag
{
public:
    int nStartFrame;     // starting frame of this tag
    int nEndFrame;       // finishing frame of this tag
    CPlot3DArray shifts; // morphing shift
public:
    void Save(CFile &file);
    void Load(CFile &file);
    void Load(LPBYTE &lpData);
};
typedef CArray<SMorphingTrackTag, SMorphingTrackTag &> SMorphingTrackTagArray;
/////////////////////////////////////////////////////////////////////////////////////
static DWORD ANF_START = 0x01;     // start animation node
static DWORD ANF_END = 0x02;       // end animation node
static DWORD ANF_CYCLED = 0x04;    // cycled animation node
static DWORD ANF_NEED_NEXT = 0x08; // need to load next animation
static DWORD ANF_HEAD_NODE = 0x10; // head node of this animation structure
/////////////////////////////////////////////////////////////////////////////////////
struct SAnimationNode
{
public:
    CAnimationSystem *lpAnimationSystem;
    // different names (node, parent node, animation, next animation)
    CString szName;          // name of this node (for resolving)
    CString szParentName;    // name of the parent node
    CString szMaskName;      // mask name
    SADEntry *lpADEntry;     // current animation databse entry
    SADEntry *lpNextADEntry; // current animation databse entry
    // flags
    DWORD dwFlags; // flags
    // parent <=> children link info
    SAnimationNode *pParent;         // parent of this animation node
    CAnimationNodePtrArray children; // children of this animation node
    // figure link information
    CFigure *lpFigure; // figure, this node is attached to
    // general animation info
    int nTime;            // current animation time
    double fpTime;        // current animation time diff
    int nAnimationLength; // length of the current animation (in frames)
    // tags info
    CRotationTrackTagArray rotations; // rotation tags
    int nCurrentRotationTag;          // current rotation tag number
    CPositionTrackTagArray positions; // position tags
    int nCurrentPositionTag;          // current position tag number
    CArray<CPlot3D, CPlot3D &> scales;
    int nCurrentScaleTag;
    SMorphingTrackTagArray morphings; // morphing tags
    int nCurrentMorphingTag;          // current morphing tag number
    // step info
    CQuaternion dq;        // current rotation step
    CPlot3D dr;            // current position step
    CQuaternion rotation;  // current rotation state
    CPlot3D position;      // current position state
    CPlot3DArray morphing; // current morphing state
    // init state info
    CQuaternion initRotation;  // initial rotation
    CPlot3D initPosition;      // initial position
    CPlot3DArray initMorphing; // initial morphing
    // bone (for debug only)
    CPlot3D bone; // this node bone
    // complection
    float strength; // strenght
    float grace;    // grace
    float tall;     // tall
private:
    void PrepareIncrementStep();
    void PrepareFPStep(double &fpFrame);
    BOOL Step();
    BOOL SetFigurePlacement();
    void MorphingIncrementStep(int nFrom, int nTo);
    void ResetMorphing();

public:
    SAnimationNode();
    ~SAnimationNode();
    // initialization
    void Init();
    void SetInitState();
    void SetDefaultInitValues();
    // animation sequence
    void SetMaskName(LPCSTR pszMaskName) { szMaskName = pszMaskName; }
    void SetAnimation(SADEntry *lpNewADEntry) { lpADEntry = lpNewADEntry; }
    void SetNextAnimation(SADEntry *lpNewNextADEntry) { lpNextADEntry = lpNewNextADEntry; }
    // get names
    LPCSTR GetNodeName() const { return szName; }
    LPCSTR GetNodeParentName() const { return szParentName; }
    LPCSTR GetAnimationName() const { return lpADEntry == NULL ? "" : lpADEntry->pszAnimationName; }
    LPCSTR GetNextAnimationName() const
    {
        return lpNextADEntry == NULL ? NULL : lpNextADEntry->pszAnimationName;
    }
    LPCSTR GetMaskName() const { return szMaskName; }
    // general flag functions
    void SetFlag(DWORD dwFlag) { dwFlags |= dwFlag; }
    void ResetFlag(DWORD dwFlag) { dwFlags &= ~dwFlag; }
    // BOOL IsFlag( DWORD dwFlag ) { return ( dwFlags & dwFlag ? TRUE : FALSE ); }
    DWORD CheckFlag(DWORD dwFlag) { return (dwFlags & dwFlag); }
    // flag functions
    void SetNeedNextAnimation() { SetFlag(ANF_NEED_NEXT); }
    void ResetNeedNextAnimation() { ResetFlag(ANF_NEED_NEXT); }
    BOOL IsNeedNextAnimation() { return CheckFlag(ANF_NEED_NEXT) != 0; }
    //
    BOOL IncrementStep();
    BOOL FPStep(double fpFrame);
    BOOL StepFwd(double fpFrame);
    BOOL RecalcSkeleton();
    //
    BOOL LinkWithFigures(CSimplyArray<CFigure *, CFigure *> &figures);
    BOOL LinkWithFigure(CFigure *lpFigure);
    BOOL RemoveNodeRecurse(SAnimationNode *lpNode);
    SAnimationNode *RemoveNodeRecurse(LPCSTR pszNodeName);
    //
    void SetComplection(float strength, float grace, float tall);
    // save/ load functions
    void LoadRecurse(LPCSTR pszMask, float strength, float grace, float tall);
    void LoadComplex(CFile &file);
    void LoadComplex(LPBYTE &lpData);
    void LoadNextAnimation();
    void LoadLinks(CFile &file);
    void LoadBone(CFile &file, float strength, float grace, float tall);
    void LoadBone(LPBYTE lpData, float strength, float grace, float tall);
    // animation changing
    void ChangeAnimationNow();
    void ResetCurrentTags();

    void CountNodes(int &nNodes);
    void GetNodeNameRecurse(CStringArray &szNames);
};
/////////////////////////////////////////////////////////////////////////////////////
inline void SAnimationNode::SetComplection(float strength, float grace, float tall)
{
    this->strength = strength;
    this->grace = grace;
    this->tall = tall;
}

// --------------------------------------------------------------------------------------------
class CAnimationSystem
{
    // private :
public:
    LPGAMEBASE lpObject3D; // object3d, this animation system belong to
    CSimplyArray<SAnimationNode *, SAnimationNode *&>
        nodes;                     // all animation nodes in this animation system
    SAnimationNode *pHeadNode;     // head animation node
    double time;                   // current time
    CString szAnimationName;       // current animation name
    CStringArray szNextAnimations; // array of next animations
    SADEntry *lpADEntry;           // current animation databse entry
    CADEntryPtrArray adEntries;    // animation databse entries
    int nAnimationChangedCounter;

public:
    CAnimationSystem();
    ~CAnimationSystem();
    // create/destroy
    BOOL Create();
    void Destroy();
    // object3d registration
    void SetObject3D(LPGAMEBASE lpNewObject3D);
    LPGAMEBASE GetObject3D() { return lpObject3D; }
    // animation system initialization
    void Init();
    //
    void SetAnimation(SADEntry *lpNewEntry);
    void SetNextAnimation(SADEntry *lpNewEntry, BOOL bNow = FALSE);
    void SetAnimationForNode(SAnimationNode *lpNode);
    BOOL SetNextAnimationForNode(SAnimationNode *lpNode);
    void AddNextAnimation(CString &szName);
    void AddNextAnimation(SADEntry *lpNewEntry);
    void InitAnimationChangedCounter();
    // animation progress functions
    BOOL AnimationPosition(float time);
    BOOL AnimationStepFwd(double dt);
    BOOL AnimationStepBwd(float dt);
    // work with nodes
    SAnimationNode *FindNode(LPCSTR pszNodeName);
    BOOL AddNewNode(LPCSTR pszNodeName, LPCSTR pszParentNodeName);
    BOOL AddNode(SAnimationNode *lpNode);
    BOOL RemoveNode(SAnimationNode *lpNode);
    SAnimationNode *RemoveNode(LPCSTR pszNodeName);
    // linking
    BOOL LinkWithFigures(CSimplyArray<CFigure *, CFigure *> &figures);
    BOOL LinkWithFigure(CFigure *lpFigure);
    void RelinkNodes();
    BOOL Unlink(CFigure *lpFigure);
    BOOL UnlinkAllFigures();
    // resizing
    void ResizeNodes(float strength, float grace, float tall);
    void RecalcSkeleton();
    // save/load services
    void Load(LPCSTR pszMask, SADEntry *lpNewADEntry, float strength, float grace, float tall);
    void LoadSkeleton(LPCSTR pszName);
    void LoadSkeleton(CFile &file);
    void GetNodeNames(CStringArray &szNames);
    void GetLinkedNodeNames(CStringArray &szNames);

    void DeleteNodes();
};
