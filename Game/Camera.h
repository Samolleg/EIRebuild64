#pragma once
#include "SaveSchema.h"
/////////////////////////////////////////////////////////////////////////////////////
#include "Plot.h"
#include "Quaternion.h"
#include "Base3D.h"
#include "GeneralSettings.h"
#include "SmartPtr.h"
#include "Spline.h"

#include <stack>
#include <list>
/////////////////////////////////////////////////////////////////////////////////////
const DWORD CAM_FPS_MODE = MAKE_FLAG(B3D_LAST_BIT, 0);
const DWORD CAM_SAVE_PLACEMENTS = MAKE_FLAG(B3D_LAST_BIT, 1);
const DWORD CAM_USE_PLACEMENTS = MAKE_FLAG(B3D_LAST_BIT, 2);
const DWORD CAM_CANT_SCROLL = MAKE_FLAG(B3D_LAST_BIT, 3);
const DWORD CAM_POINT_LOCKED = MAKE_FLAG(B3D_LAST_BIT, 4);
const DWORD CAM_RESTRICTED = MAKE_FLAG(B3D_LAST_BIT, 5);
const DWORD CAM_ALTERNATIVE_RESTRICTIONS = MAKE_FLAG(B3D_LAST_BIT, 6);
const DWORD CAM_LAST_BIT = MAKE_LAST_BIT(B3D_LAST_BIT, 6);
//
const DWORD CAM_MAX_SHORTCUTS = 8;
/////////////////////////////////////////////////////////////////////////////////////
class CObject3D;
/////////////////////////////////////////////////////////////////////////////////////
struct SBezierPatch
{
public:
    SBezierCurve q1, q2, q3, q4;
    float fX1, fY1, fX2, fY2;
    float fBaseX, fBaseY;

public:
    CPlot3D GetValue(float t, float s)
    {
        SBezierCurve temp;
        temp.Setup(q1(t), q2(t), q3(t), q4(t));
        return temp(s);
    }
};
/////////////////////////////////////////////////////////////////////////////////////
struct SSplinePatch
{
public:
    CBSpline3D q1, q2, q3, q4;
    float fX1, fY1, fX2, fY2;
    float fBaseX, fBaseY;

public:
    CPlot3D GetValue(const float t, const float s)
    {
        CBSpline3D temp{};
        temp.Setup(q1(t), q2(t), q3(t), q4(t));
        return temp(s);
    }
};
/////////////////////////////////////////////////////////////////////////////////////
struct SCameraPlacement
{
    void Load(SaveSchema::Reader &);
    void Store(SaveSchema::Writer &) const;
    float fpTime;
    float step;
    CPlot3D plot;
    CQuaternion quat;
};
/////////////////////////////////////////////////////////////////////////////////////
struct SCameraShortcut
{
    void Load(SaveSchema::Reader &);
    void Store(SaveSchema::Writer &) const;
public:
    float fDistance;     // rod in polar coords
    CQuaternion qRotate; // rotation around Z axis
    float fPitch;        // pitch angle
public:
    SCameraShortcut()
        : fDistance(30)
        , qRotate(QNULL)
        , fPitch(ToRadian(45.0f))
    {
    }
};
/////////////////////////////////////////////////////////////////////////////////////
template <class TYPE>
struct SCameraMovement
{
public:
    TYPE value;
    TYPE desired;
    float fpAccelerationCoeff;

public:
    void Accelerate(float fpCoeff);
};
/////////////////////////////////////////////////////////////////////////////////////
template <class TYPE>
inline void SCameraMovement<TYPE>::Accelerate(float fpCoeff)
{
    TYPE acceleration = (desired - value) * fpAccelerationCoeff;
    if (fabs(acceleration) < 1e-3)
        value = desired;
    else
    {
        float fpLast = value * value;
        value += acceleration * fpCoeff;
        float fpCurr = value * value;
        float fpDesired = desired * desired;
        if (((fpLast < fpDesired) && (fpCurr > fpDesired)) ||
            ((fpLast > fpDesired) && (fpCurr < fpDesired)))
            value = desired;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
struct SEarthquakeEffect
{
    float fTime, fDuration;
    float fAmplitude;
    float fAttenuationCoeff;
};
/////////////////////////////////////////////////////////////////////////////////////
class CCamera : public CBase3D
{
    friend class SaveSchema::Slot;
private:
    // different camera restrictions and params
    SGeneralSettings *pSettings; // here we'll use only camera settings
    // camera scrolling/rotation/zooming params
    SCameraMovement<CPlot3D> scroll; // camera scroll value/desired values
    SCameraMovement<float> rotate;   // camera rotate value/desired values
    SCameraMovement<float> pitch;    // camera pitch value/desired values
    SCameraMovement<float> zoom;     // camera zoom value/desired values
    CPlot2D ptLastDRPoint;
    long long mouseCarryX = 0, mouseCarryY = 0;
    int mouseCarryWidth = 0, mouseCarryHeight = 0;
    // camera anchor point params
    CPlot3D ptAnchorPoint; // point on the terrain, this camera attached to
    float fAnchorHeight;   // anchor point height over 'averaged' terrain
    // camera position params
    float fDistance; // rod in polar coords
    CQuaternion qRotate;
    float fPitch; // pitch angle
    enum FalloutMode { FALLOUT_STOCK, FALLOUT_SOUTHWEST, FALLOUT_NORTHEAST };
    FalloutMode falloutMode = FALLOUT_STOCK;
    FalloutMode falloutBriefingMode = FALLOUT_STOCK;
    bool falloutBriefing = false;
    bool falloutBaselineValid = false;
    CQuaternion falloutBaselineRotation;
    float falloutBaselinePitch = 0.0f;
    float falloutTelephotoMultiplier = 2.0f;
    // Field-camp placement: value fields persist; terrain and uptime are rebound.
    struct CampPlacement
    {
        CPlot3D position, anchor;
        CQuaternion orientation, orbit, baseline;
        float anchorHeight, distance, pitch, baselinePitch, telephoto;
        DWORD carrier;
        FalloutMode mode;
        bool baselineValid;
    } campPlacement;
    bool campPlacementValid = false, campEntered = false;
    void *campTerrain = nullptr;
    DWORD campRequestTime = 0;
    // some camera effects
    std::list<SEarthquakeEffect> earthquakes;
    // scroll restrictions
    CPlot3D ptRestrictionCenter;
    float fRestrictionRadius;
    // carrier's ID, this camera attached to
    DWORD dwCarrierID;
    // shotcuts
    SCameraShortcut shortcuts[CAM_MAX_SHORTCUTS];
    //
    std::stack<SCameraPlacement> cameraPlacements;
    //
    SSplinePatch spline;
    // viewing frustum parameters
    float dvFrontClip;    // front clipping plane of the viewing frustum (Z.B. 1)
    float dvBackClip;     // back clipping plane of the viewing frustum (Z.B. 200)
    float dvViewingAngle; // angle of the viewing frustum (Z.B. pi/3.5)
    // placements to play
    CString szPlacementsFileName; // file name to load camera placements to
    CSimplyArray<SCameraPlacement, SCameraPlacement> placements;
    float fpCameraTimeShift;
    int nCurrentPlacementIndex;
    CPlot3D lastFPSdir;

private:
    void Prepare(); // prepare camera for objects input
    void CorrectDistanceToCarrier();

public:
    bool detachedSave = false;
    CCamera();
    virtual ~CCamera();
    //
    void SetViewingFrustum(float dvNewFrontClip, float dvNewBackClip, float dvNewViewingAngle);
    void GetViewingFrustum(float &dvFrontClip, float &dvBackClip, float &dvViewingAngle);
    //
    BOOL Create(SGeneralSettings *pNewSettings, float dvNewFrontClip, float dvNewBackClip,
                float dvNewViewingAngle);
    virtual BOOL Create();
    virtual void Destroy();
    //
    void InitPlacement(float fRotateAngle = 0);
    void InitRotate(float fRotateAngle = 0);
    void ToggleFalloutMode(float normalizeAngle = 0);
    void NormalizeNorth(float angle = 0);
    void ReapplyFalloutAfterMove();
    void ApplyFalloutPlacement();
    void SuspendFalloutForBriefing();
    void ResumeFalloutAfterBriefing();
    void ResetFalloutMode();
    void CaptureFieldCampCamera();
    bool RestoreFieldCampCamera();
    void UpdateFieldCampCamera();
    void DiscardFieldCampCamera();
    bool IsFalloutModeActive() const { return falloutMode != FALLOUT_STOCK; }
    float GetFalloutTelephotoMultiplier() const { return falloutTelephotoMultiplier; }
    //
    void SetAnchorPoint(const CPlot3D &pt);
    const CPlot3D &GetAnchorPoint() const { return ptAnchorPoint; }
    float GetOrbitAnchorHeight() const { return fAnchorHeight; }
    void MoveAnchor(const CPlot3D &pt);
    void MoveAnchorXY(const CPlot3D &pt);
    void ReconstructCameraPlacement();
    // camera control
    void SetScrollValue(const CPlot2D &ptScroll);
    void SetRotateValue(const float fpValue);
    void SetPitchValue(const float fpValue);
    void ApplyMouseYaw(float delta);
    void ApplyMousePitch(float delta);
    void SetZoomValue(const float fpValue);
    void AddDistance(const float fpValue);
    void StopAllMoving();
    //
    bool IsPitching() const { return ((pitch.desired != 0) || (pitch.value != 0)); }
    bool IsRotating() const { return ((rotate.desired != 0) || (rotate.value != 0)); }
    bool IsZooming() const { return ((zoom.desired != 0) || (zoom.value != 0)); }
    bool IsScrolling() const { return ((scroll.desired != VNULL) || (scroll.value != VNULL)); }
    float GetRotateAngle();
    //
    void LockScrollPoint(const CPlot3D &ptScrollPoint);
    void UnlockScrollPoint();
    void DirectRotate(const CPlot2D &ptRotate);
    void SetLastDRPoint(const CPlot2D &point) { ptLastDRPoint = point; }
    void NormalizeMouseDelta(CPoint& delta, int width, int height);
    //
    void SetFPSMode(CObject3D *lpNewCarrier);
    void SetFPSMode(DWORD dwNewCarrierID);
    void SetCarrier(CObject3D *lpNewCarrier);
    void SetCarrier(DWORD dwNewCarrierID);
    DWORD GetCarrierID() const { return dwCarrierID; }
    void ResetTracking()
    {
        ResetFlag(CAM_FPS_MODE);
        SetCarrier(-1);
    }
    void RotateTowardCarrier(BOOL bImmediatly = FALSE);
    void RotateToward(const CPlot3D &pt, BOOL bImmediatly = FALSE);
    void AutoLeveling();
    BOOL Zoom(float fpValue = 1.0f);
    void CorrectHeight();
    // effects
    void BeginEarthquake(const CPlot3D &ptPlace, float fDuration, float fAmplitude = 2.0f,
                         float fAttenuation = 0.25f);
    bool IsEarthquake() const { return !earthquakes.empty(); }
    // shortcut functions
    bool SetCurrentAsShortcut(int nIndex);
    bool ActivateShortcut(int nIndex);
    void SetCameraAs(const SCameraShortcut &pos);
    void GetCameraPlacement(SCameraShortcut &pos) const;
    //
    CPlot3D GetIntersectionWithZero();
    //
    void EstimateAverageHeight(const CPlot3D &ptPos, const CPlot3D &ptAxisX, const CPlot3D &ptAxisY,
                               const float fpStep, const float fpSemiaxisX, const float fpSemiaxisY,
                               float &fpAverageZ, float &fpMinZ, float &fpMaxZ);
    float GetSplineHeight(const CPlot3D &ptPos, const float fpSemiaxisX, const float fpSemiaxisY);
    // work with camera in briefings
    void LockCameraControl(bool bLock);
    void PushCameraPlacement();
    void PopCameraPlacement(float fTime = 0);
    void MoveTo(const CPlot3D &ptPos, const CPlot3D &ptLookAt, float fTime = 0);
    bool OrientCamera(float fDirX, float fDirY, float fDirZ);
    bool OrientCamera(const CPlot3D &ptDir) { return OrientCamera(ptDir.x, ptDir.y, ptDir.z); }
    void SetScrollRestriction(const CPlot3D &ptPos, float fRadius);
    void ResetScrollRestriction() { ResetFlag(CAM_RESTRICTED); }
    // segment
    virtual int Segment();
    void Step(float fpStep);
    void DampReleasedPan(float seconds, float releaseTime);
    const CPlot3D &GetObservationPosition() const;
    // camera placements...
    BOOL BeginCameraPlacementsSave(LPCSTR pszFileName);
    BOOL SaveCurrentCameraPlacement(float fpTime);
    BOOL EndCameraPlacementsSave();
    BOOL LoadCameraPlacements(LPCSTR pszFileName, int nRemove = 0);
    void BeginPlayCamera(float fpTime)
    {
        SetFlag(CAM_USE_PLACEMENTS);
        fpCameraTimeShift = fpTime - placements[0].fpTime;
        nCurrentPlacementIndex = 0;
    }
    BOOL PlayCamera(float fpTime);
    BOOL InitCameraPlacement();
    void EndPlayCamera() { ResetFlag(CAM_USE_PLACEMENTS); }
    BOOL IsPlaying() const { return CheckFlag(CAM_USE_PLACEMENTS); }
    // data access
    float GetDistance() const { return fDistance; }
    float GetPitch() const { return fPitch; }
    // save/load placement
    void Load(CSave &file);
    void Store(CSave &file);
    void ImportShortcuts(const SGeneralSettings &settings);
    void ExportShortcuts(SGeneralSettings &settings);
    // identification information
    virtual LPCSTR GetGameClassName() const { return "CCamera"; };
    virtual EClassIDs WhoAmI() const { return HPC_CAMERA; };
public:
    virtual void Store(SaveSchema::Writer &) const;
    virtual void Load(SaveSchema::Reader &);
    virtual void DiscardSave();
};
/////////////////////////////////////////////////////////////////////////////////////
inline void CCamera::SetFPSMode(CObject3D *lpNewCarrier)
{
    if (lpNewCarrier)
        SetFlag(CAM_FPS_MODE);
    else
        ResetFlag(CAM_FPS_MODE);
    SetCarrier(lpNewCarrier);
}
/////////////////////////////////////////////////////////////////////////////////////
inline void CCamera::SetFPSMode(DWORD dwNewCarrierID)
{
    if (dwNewCarrierID != -1)
        SetFlag(CAM_FPS_MODE);
    else
        ResetFlag(CAM_FPS_MODE);
    SetCarrier(dwNewCarrierID);
}
/////////////////////////////////////////////////////////////////////////////////////
extern CCamera theCamera;
