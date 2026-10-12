#pragma once
/////////////////////////////////////////////////////////////////////////////////////
#include "GameBase.h"

#include "Object.h"
#include "Plot.h"
#include "Quaternion.h"

#include "adFormat.h"
#include "OwnedRender/E1SkeletonSources.h"
#include <memory>
namespace OwnedRender {
struct E1AnimationSelection;
class E1AnimationSelectionCache;
}

/////////////////////////////////////////////////////////////////////////////////////
class CFigure;
class CAnimationNode;
class CUnit;
class CModelResourceEntry;
/////////////////////////////////////////////////////////////////////////////////////
// главная идея смены анимаций заключается в том, что этим занимается CAnimationSystem2,
// а не CAnimationNode. Учет времени тоже производится в CAnimationSystem2 и при необходимости
// сменить анимацию, это делается для всех CAnimationNode одновременно.
// Example:
// make step on dT, but animation will end on dT1 < dT
// dt = dT - dT1
// step on dT1
// change animation
// step on dt (with new animation!)
/////////////////////////////////////////////////////////////////////////////////////
class CAnimationTrack : public CGameBase
{
private:
    const SADEntry *lpADEntry; // animation database entry of this animation track
    CAnimationNode *lpNode;    // animation node, this track belong to
    // frames
    DWORD dwNumPositionFrames; // number of position frames
    CPlot3D *positions;        // position frames
    DWORD dwNumRotationFrames; // number of rotation frames
    CQuaternion *rotations;    // rotation frames
    DWORD dwNumMorphings;      // number of morphing plots in each frame
    CSimplyArray<CPlot3D *, CPlot3D *> morphings;
    // time information
    float fpTime; // current time
public:
    // current state (in Local Coordinate System /LCS/)
    CPlot3D position;      // current position
    CQuaternion rotation;  // current rotation
    CPlot3DArray morphing; // current morphing
public:
    CAnimationTrack();
    virtual ~CAnimationTrack();
    // create/destroy functions
    virtual BOOL Create();
    virtual void Destroy();
    //
    void Clear();
    void ClearAnimation();
    //
    const SADEntry *GetADEntry() const { return lpADEntry; }
    void SetADEntry(const SADEntry *pADEntry) { lpADEntry = pADEntry; }
    void SetAnimation(const SADEntry *lpNewADEntry);
    void SetNode(CAnimationNode *lpNewNode) { lpNode = lpNewNode; }
    CAnimationNode *GetNode() { return lpNode; }
    //
    void Step(float fpStep);
    void SetTime(float fpNewTime);
    float GetTime() const { return fpTime; }
    //
    BOOL Load(LPBYTE &lpData, DWORD dwDataSize);
    // class identification functions
    virtual LPCSTR GetGameClassName() const { return "CAnimationTrack"; };
    virtual EClassIDs WhoAmI() const { return HPC_ANIMATION_TRACK; };
};
/////////////////////////////////////////////////////////////////////////////////////
class CAnimationNode : public CGameBase
{
private:
    // names
    CString szName;       // name of this node
    CString szParentName; // name of the parent node
    CString szMaskName;   // mask name
    // links (parent and children)
    CAnimationNode *lpParent;                           // parent node
    CList<CAnimationNode *, CAnimationNode *> children; // child nodes list
    CFigure *lpFigure;                                  // figure, linked to this node
    // animation tracks
    CAnimationTrack *lpCurrent;   // current animation track: this is only pointer, which can be set
                                  // to main/next/alternate
    CAnimationTrack *lpMain;      // main animation track
    CAnimationTrack *lpNext;      // next animation track (to switch to)
    CAnimationTrack *lpAlternate; // alternate animation (for info-window, etc.)
    //
    OwnedRender::E1SelectedSkeletonBytes ownedBone;
    CPlot3D bone; // bone of this animation node
    // complection
    float strength; // strenght
    float grace;    // grace
    float tall;     // tall
public:
    // current placement information (used only during interpolation between current and next
    // animations)
    CPlot3D position;     // current position during interpolation
    CQuaternion rotation; // current rotation during interpolation
    CXYZW4Array morphing; // current morphing during interpolation
public:
    CAnimationNode();
    virtual ~CAnimationNode();
    // create/destroy functions
    virtual BOOL Create();
    virtual void Destroy();
    void Clear();
    // parent/children functions
    void SetParentNode(CAnimationNode *lpNewNode) { lpParent = lpNewNode; }
    CAnimationNode *GetParentNode() { return lpParent; }
    BOOL AddChildNode(CAnimationNode *lpNewNode);
    BOOL RemoveChildNode(CAnimationNode *lpNewNode);
    void RemoveAllChildren();
    // tracks control
    void SetAnimation(const SADEntry *lpADEntry);
    void SetNextAnimation(const SADEntry *lpADEntry);
    void SetAlternateAnimation(const SADEntry *lpADEntry);
    void SetMainAsCurrent() { lpCurrent = lpMain; }
    void SetNextAsCurrent() { lpCurrent = lpNext; }
    void SetAlternateAsCurrent() { lpCurrent = lpAlternate; }
    CAnimationTrack *GetCurrentTrack() { return lpCurrent; }
    void RecalcSkeleton(const CPlot3D &ptCenterShift);
    void CreateMainTrack();
    //
    void ClearCurrentTrack(const SADEntry *pADEntry);
    void LoadCurrentTrack(BYTE *pData, DWORD dwDataSize, const SADEntry *pADEntry);
    // name functions
    void SetName(LPCSTR pszNewName) { szName = pszNewName; }
    void SetParentName(LPCSTR pszNewName) { szParentName = pszNewName; }
    void SetMaskName(LPCSTR pszNewName) { szMaskName = pszNewName; }
    const CString &GetNodeName() const { return szName; }
    const CString &GetParentName() const { return szParentName; }
    const CString &GetMaskName() const { return szMaskName; }
    virtual LPCSTR GetName() const { return szName; }
    // complection
    void SetComplection(CModelResourceEntry *pModel, float fpStrength, float fpGrace, float fpTall);
    void GetComplection(float &fpStrength, float &fpGrace, float &fpTall);
    // figure control
    void SetFigure(CFigure *lpFigure);
    CFigure *GetFigure() { return lpFigure; }
    void SetCurrentPlacement(CFigure *lpFigure);
    const CPlot3D &GetPosition() const { return position; }
    const CQuaternion &GetOrientation() const { return rotation; }
    //
    void PublishOwnedBone(const void* data, DWORD size) noexcept {
        ownedBone.Publish(GetName(), data, size);
    }
    const OwnedRender::E1SelectedSkeletonBytes& GetOwnedBone() const noexcept { return ownedBone; }
    void LoadBone(LPBYTE &lpData);
    void LoadOldBone(LPBYTE &lpData);
    // class identification functions
    virtual LPCSTR GetGameClassName() const { return "CAnimationNode"; };
    virtual EClassIDs WhoAmI() const { return HPC_ANIMATION_NODE; };
};
/////////////////////////////////////////////////////////////////////////////////////
typedef void (*AnimationSwitchCallback)(CUnit *pUnit);
class CObject3D;
/////////////////////////////////////////////////////////////////////////////////////
class CAnimationSystem2 : public CGameBase
{
    friend class CObject3D;
    OwnedRender::E1SkeletonSources ownedSkeleton;
    std::unique_ptr<OwnedRender::E1AnimationSelectionCache> ownedAnimationSelection;

private:
    CObject3D *lpObject3D;                             // object3d, this animation system belong to
    CAnimationNode *lpHeadNode;                        // head animation node
    CList<CAnimationNode *, CAnimationNode *> nodes;   // all nodes as llist
    const SADEntry *lpADEntry;                         // current animation entry
    CList<const SADEntry *, const SADEntry *> entries; // animations queue
    float fpTime;
    //
    AnimationSwitchCallback SwitchCallback;
    // complection (to use during node adding/removing)
    float strength;
    float grace;
    float tall;
    //
    bool bNeedRecalc; // do we need recalc skeleton, or no
public:
    CAnimationSystem2();
    virtual ~CAnimationSystem2();
    // create/destroy functions
    virtual BOOL Create();
    virtual void Destroy();
    void Clear();
    void DeleteNodes();
    //
    void SetObject3D(CObject3D *lpNewObject3D) { lpObject3D = lpNewObject3D; }
    //
    const SADEntry *GetCurrentAnimation() const { return lpADEntry; }
    CAnimationNode *GetHeadNode() const { return lpHeadNode; }
    // set/add animations
    void SetAnimation(const SADEntry *lpNewADEntry);
    void SetNextAnimation(const SADEntry *lpNewADEntry);
    void AddNextAnimation(const SADEntry *lpNewADEntry);
    void ClearAnimationsQueue() { entries.RemoveAll(); }
    int GetAnimationQueueSize() const { return entries.GetCount(); }
    void ClearAllAnimations();
    void SetMainAsCurrent();
    void SetNextAsCurrent();
    void SetAlternateAsCurrent();
    // entire animation loading/clearing
    BOOL ClearCurrentAnimation();
    BOOL LoadAnimation(CMappedResource *pAnimation, const SADEntry *pADEntry);
    // work with nodes
    CAnimationNode *FindNode(LPCSTR pszNodeName);
    BOOL AddNewNode(CModelResourceEntry *pModel, LPCSTR pszNodeName, LPCSTR pszParentNodeName);
    BOOL AddNode(CModelResourceEntry *pModel, CAnimationNode *lpNode);
    BOOL RemoveNode(CAnimationNode *lpNode);
    BOOL RemoveNode(LPCSTR pszNodeName);
    BOOL SetupHeadNode();
    void LoadSkeleton(LPCSTR pszMaskName);
    void PublishOwnedLinks(const char* name, const void* data, DWORD size) noexcept {
        ownedSkeleton.PublishLinks(name, data, size);
    }
    std::shared_ptr<const owned::ModelCpuAsset> GetOwnedSkeleton() noexcept;
    bool GetOwnedCurrentAnimationSelection(OwnedRender::E1AnimationSelection& output,
                                           const char** failedStage = nullptr) noexcept;
    void GetNodeNames(CStringArray &szNames) const;
    // linking
    BOOL LinkWithFigures(CSimplyArray<CFigure *, CFigure *> &figures);
    BOOL LinkWithFigure(CFigure *lpFigure);
    void RelinkNodes();
    BOOL UnlinkFigure(CFigure *lpFigure);
    BOOL UnlinkAllFigures();
    // resizing
    void RecalcSkeleton();
    void RecalcSkeletonWithCurrentTime();
    void SetComplection(float fpStrength, float fpGrace, float fpTall);
    //
    void SetAnimationSwitchCallback(AnimationSwitchCallback NewCallback)
    {
        SwitchCallback = NewCallback;
    }
    // step...
    void Step(float fpDT);
    void SetTime(float fpNewTime);
    float GetTime() const { return fpTime; }
    // class identification functions
    virtual LPCSTR GetGameClassName() const { return "CAnimationSystem2"; };
    virtual EClassIDs WhoAmI() const { return HPC_ANIMATION_NODE; };
};
/////////////////////////////////////////////////////////////////////////////////////
