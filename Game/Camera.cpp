#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "Camera.h"
#include "CameraTerrainCollision.h"
#include "camera_rotation.h"
#include "FrameDiagnostics.h"
#include "ServerConsts.h"
#include "ModRuntime.h"
#include "ModPackage.h"
#include "FileRegistry.h"
#include "ModConfiguration.h"

#include "WorldClient.h"
#include "WorldServer.h"
#include "SoundManager.h"
#include "Terrain.h"
/////////////////////////////////////////////////////////////////////////////////////
static const float CAMERA_ADD_HEIGHT = 0.6f;
/////////////////////////////////////////////////////////////////////////////////////
CCamera theCamera;
/////////////////////////////////////////////////////////////////////////////////////
template <class TYPE>
inline void ToZero(TYPE x)
{
    ZeroMemory(&x, sizeof(x));
}
/////////////////////////////////////////////////////////////////////////////////////
CCamera::CCamera()
{
    scroll.value = VNULL;
    scroll.desired = VNULL;
    scroll.fpAccelerationCoeff = 1.0f / 7.5f;

    rotate.value = 0;
    rotate.desired = 0;
    rotate.fpAccelerationCoeff = 1.0f / 3.0f;

    pitch.value = 0;
    pitch.desired = 0;
    pitch.fpAccelerationCoeff = 1.0f / 3.0f;

    zoom.value = 0;
    zoom.desired = 0;
    zoom.fpAccelerationCoeff = 1.0f / 3.0f;

    ptAnchorPoint = CPlot3D(10, 10, 2);
    fAnchorHeight = 2;
    //
    qRotate = QNULL;

    //
    Create();

#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
}
/////////////////////////////////////////////////////////////////////////////////////
CCamera::~CCamera()
{
    Destroy();
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CCamera::Create()
// create camera and set viewing frustum
BOOL CCamera::Create(SGeneralSettings *pNewSettings, float dvNewFrontClip, float dvNewBackClip,
                     float dvNewViewingAngle)
{
    ImportShortcuts(*pNewSettings);
    //
    pSettings = pNewSettings;
    fDistance = pSettings->fpCameraDefaultDistanceToCarrier;
    ptAnchorPoint = CPlot3D(pSettings->fpCameraDefaultXDeploy, pSettings->fpCameraDefaultYDeploy,
                            pSettings->fpCameraDefaultHeight);
    // set viewing frustum
    SetViewingFrustum(dvNewFrontClip, dvNewBackClip, dvNewViewingAngle);
    //
    SetFlag(CAM_CANT_SCROLL);
    // shortcuts for mission and briefing zones
    shortcuts[7].fDistance = pSettings->fpCameraDefaultDistanceToCarrier;
    shortcuts[7].fPitch = fPitch = ToRadian(45.0f);
    shortcuts[6].fDistance = pSettings->fpCameraDefaultDistanceToCarrier;
    shortcuts[6].fPitch = fPitch = ToRadian(45.0f);
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CCamera::Create()
{
    dwCarrierID = -1;
    // set default viewing frustum
    SetViewingFrustum(1, 2000, D3DVALUE(pi / 3.5));

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// void CCamera::Destroy()
// destroy camera: remove all objects and release carrier
void CCamera::Destroy()
{
    ResetFalloutMode();
    dwCarrierID = -1;
}
/////////////////////////////////////////////////////////////////////////////////////
// void CCamera::Prepare()
// Prepare camera for objects input
void CCamera::Prepare()
{
    // we are using this camera to look on the all of the objects via it.
    // thus, is we rotated camera around axis 'A' on the 'alpha' degrees, this
    // equals to we rotated all the objects (with central points) around axis '-A'
    // on the 'alpha' degrees.
    rotation1 = rotation;
    rotation1.ReverseAxis();
    respectCP = VNULL;
    CreateCurrentRespectPlacement();
    //
}
/////////////////////////////////////////////////////////////////////////////////////
// void CCamera::SetViewingFrustum( D3DVALUE dvNewFrontClip, D3DVALUE dvNewBackClip, D3DVALUE
// dvNewViewingAngle ) set new params for viewing frustum
void CCamera::SetViewingFrustum(D3DVALUE dvNewFrontClip, D3DVALUE dvNewBackClip,
                                D3DVALUE dvNewViewingAngle)
{
    dvFrontClip = dvNewFrontClip;
    dvBackClip = dvNewBackClip;
    dvViewingAngle = dvNewViewingAngle;
}
/////////////////////////////////////////////////////////////////////////////////////
// void CCamera::GetViewingFrustum( D3DVALUE& dvFrontClip, D3DVALUE& dvBackClip, D3DVALUE&
// dvViewingAngle ) get params of the viewing frustum
void CCamera::GetViewingFrustum(D3DVALUE &dvFrontClip, D3DVALUE &dvBackClip,
                                D3DVALUE &dvViewingAngle)
{
    dvFrontClip = this->dvFrontClip;
    dvBackClip = this->dvBackClip;
    dvViewingAngle = this->dvViewingAngle;
}
/////////////////////////////////////////////////////////////////////////////////////
// CObject3D* CCamera::SetCarrier( CObject3D* lpNewCarrier )
// sets new carrier for camera
void CCamera::SetCarrier(CObject3D *pCarrier)
{
    if (pCarrier)
        SetCarrier(pCarrier->GetID());
    else
        SetCarrier(-1);
}
/////////////////////////////////////////////////////////////////////////////////////
void CCamera::SetCarrier(DWORD dwNewCarrierID)
{
    if ((dwCarrierID = dwNewCarrierID) != -1)
        scroll.value = scroll.desired = VNULL;
}
/////////////////////////////////////////////////////////////////////////////////////
// void CCamera::RotateTowardCarrier()
// rotate camera toward carrier
void CCamera::RotateTowardCarrier(BOOL bImmediatly)
{
    CBase3D *pCarrier = (CBase3D *)theWorld.GetMapObjectManager()->Find(dwCarrierID);
    if (pCarrier)
    {
        CPlot3D dir = pCarrier->GetCP() - GetCP();
        CPlot3D z = GetZAxis();
        CPlot3D axis;
        float angle;

        dir.z += CAMERA_ADD_HEIGHT;

        dir *= 1.0f / fabs(dir);
        if (fabs(z - dir) > 0.001)
        {
            axis = z ^ dir;
            if (fabs(axis) > 0.001)
            {
                axis *= 1.0f / fabs(axis);
                angle = z * dir;
                if (angle > 1.0f)
                    angle = 1.0f;
                else if (angle < -1.0f)
                    angle = -1.0f;
                angle = float(acos(angle));
                if (!bImmediatly)
                    angle /= 10.0f;
                Rotate(axis, angle);
            }
        }
        CorrectDistanceToCarrier();
        AutoLeveling();
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// void CCamera::AutoLeveling()
// camera leveling with respect to terrain plane
void CCamera::AutoLeveling()
{
    CPlot3D z = GetZAxis();
    CPlot3D x = GetXAxis();
    CPlot3D dir = CPlot3D(z.y, -z.x, 0);
    CPlot3D axis;
    float angle;

    dir *= 1.0f / fabs(dir);
    angle = x * dir;
    if (angle > 1.0f)
        angle = 1.0f;
    else if (angle < -1.0f)
        angle = -1.0f;
    angle = float(acos(angle));
    //
    if (fabs(angle) < ToRadian(1.0f))
        return;
    // add inertia to autoleveling
    angle /= 10.0f;
    axis = x ^ dir;
    axis *= 1.0f / fabs(axis);
    if (fabs(axis) > 0.1)
        Rotate(axis, angle);

    CorrectHeight();
}
/////////////////////////////////////////////////////////////////////////////////////
void CCamera::EstimateAverageHeight(const CPlot3D &ptPos, const CPlot3D &ptAxisX,
                                    const CPlot3D &ptAxisY, const float fpStep,
                                    const float fpSemiaxisX, const float fpSemiaxisY,
                                    float &fpAverageZ, float &fpMinZ, float &fpMaxZ)
{
    float fpZValue = theWorld.GetSurfaceValue(ptPos.x, ptPos.y);
    int nCounter = 1; // static_cast<int>( 0.5f * ( fpSemiaxisX * fpSemiaxisY * 4.0f / fpStep ) );

    fpAverageZ = fpMaxZ = fpMinZ = fpZValue;
    fpAverageZ *= static_cast<float>(nCounter);
    for (float x = ptPos.x - fpSemiaxisX; x <= ptPos.x + fpSemiaxisX; x += fpStep)
    {
        if ((x < 0) || (x >= theWorld.GetTerrain()->GetMaxX()))
            continue;
        for (float y = ptPos.y - fpSemiaxisY; y <= ptPos.y + fpSemiaxisY; y += fpStep)
        {
            if ((y < 0) || (y >= theWorld.GetTerrain()->GetMaxY()))
                continue;
            fpZValue = theWorld.GetSurfaceValue(x, y);
            fpMinZ = Min(fpMinZ, fpZValue);
            fpMaxZ = Max(fpMaxZ, fpZValue);
            fpAverageZ += fpZValue;

            nCounter++;
        }
    }

    fpAverageZ /= static_cast<float>(nCounter);
    /*
    float fSplineHeight = GetSplineHeight( ptPos, 8, 8 );
    TRACE2( "Average = %g\t Spline = %g\n", fpAverageZ, fSplineHeight );
    */
}
/////////////////////////////////////////////////////////////////////////////////////
float CCamera::GetSplineHeight(const CPlot3D &ptPos, float fpSemiaxisX, float fpSemiaxisY)
{
    fpSemiaxisX *= 1.0f / 3.0f;
    fpSemiaxisY *= 1.0f / 3.0f;
    float fBaseX = floor(ptPos.x / fpSemiaxisX) * fpSemiaxisX;
    float fBaseY = floor(ptPos.y / fpSemiaxisY) * fpSemiaxisY;
    // setup spline patch
    if ((fBaseX != spline.fBaseX) || (fBaseY != spline.fBaseY))
    {
        CTerrain *pTerrain = theWorld.GetTerrain();
        float fStepX = fpSemiaxisX * 2.0f;
        float fStepY = fpSemiaxisY * 2.0f;
        float fX1 = fBaseX - fpSemiaxisX * 2.0f;
        float fY1 = fBaseY - fpSemiaxisY * 2.0f;
        float fX[4], fY[4];
        fX[0] = Max(0.0f, Min(fX1, pTerrain->GetMaxX()));
        fX[1] = Max(0.0f, Min(fX1 + fStepX, pTerrain->GetMaxX()));
        fX[2] = Max(0.0f, Min(fX1 + 2.0f * fStepX, pTerrain->GetMaxX()));
        fX[3] = Max(0.0f, Min(fX1 + 3.0f * fStepX, pTerrain->GetMaxX()));
        fY[0] = Max(0.0f, Min(fY1, pTerrain->GetMaxY()));
        fY[1] = Max(0.0f, Min(fY1 + fStepY, pTerrain->GetMaxY()));
        fY[2] = Max(0.0f, Min(fY1 + 2.0f * fStepY, pTerrain->GetMaxY()));
        fY[3] = Max(0.0f, Min(fY1 + 3.0f * fStepY, pTerrain->GetMaxY()));
        //
        spline.q1.Setup(CPlot3D(fX[0], fY[0], theWorld.GetSurfaceValue(fX[0], fY[0])),
                        CPlot3D(fX[1], fY[0], theWorld.GetSurfaceValue(fX[1], fY[0])),
                        CPlot3D(fX[2], fY[0], theWorld.GetSurfaceValue(fX[2], fY[0])),
                        CPlot3D(fX[3], fY[0], theWorld.GetSurfaceValue(fX[3], fY[0])));
        //
        spline.q2.Setup(CPlot3D(fX[0], fY[1], theWorld.GetSurfaceValue(fX[0], fY[1])),
                        CPlot3D(fX[1], fY[1], theWorld.GetSurfaceValue(fX[1], fY[1])),
                        CPlot3D(fX[2], fY[1], theWorld.GetSurfaceValue(fX[2], fY[1])),
                        CPlot3D(fX[3], fY[1], theWorld.GetSurfaceValue(fX[3], fY[1])));
        //
        spline.q3.Setup(CPlot3D(fX[0], fY[2], theWorld.GetSurfaceValue(fX[0], fY[2])),
                        CPlot3D(fX[1], fY[2], theWorld.GetSurfaceValue(fX[1], fY[2])),
                        CPlot3D(fX[2], fY[2], theWorld.GetSurfaceValue(fX[2], fY[2])),
                        CPlot3D(fX[3], fY[2], theWorld.GetSurfaceValue(fX[3], fY[2])));
        //
        spline.q4.Setup(CPlot3D(fX[0], fY[3], theWorld.GetSurfaceValue(fX[0], fY[3])),
                        CPlot3D(fX[1], fY[3], theWorld.GetSurfaceValue(fX[1], fY[3])),
                        CPlot3D(fX[2], fY[3], theWorld.GetSurfaceValue(fX[2], fY[3])),
                        CPlot3D(fX[3], fY[3], theWorld.GetSurfaceValue(fX[3], fY[3])));
        spline.fX1 = fBaseX;
        spline.fY1 = fBaseY;
        spline.fX2 = fBaseX + 2.0f * fpSemiaxisX;
        spline.fY2 = fBaseY + 2.0f * fpSemiaxisY;
        spline.fBaseX = fBaseX;
        spline.fBaseY = fBaseY;
    }
    // get patch value
    float t = (ptPos.x - spline.fX1) / (spline.fX2 - spline.fX1);
    float s = (ptPos.y - spline.fY1) / (spline.fY2 - spline.fY1);
    CPlot3D ptValue = spline.GetValue(t, s);

    return ptValue.z;
}
/////////////////////////////////////////////////////////////////////////////////////
// void CCamera::CorrectHeight()
void CCamera::CorrectHeight()
{
    CameraTerrainCollision::GuardEye(&theWorld, this);
}
/////////////////////////////////////////////////////////////////////////////////////
// zoom/unzoom camera
BOOL CCamera::Zoom(float fValue)
{
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// correct distance to carrier with respect to 'distance' member field
void CCamera::CorrectDistanceToCarrier()
{
}
/////////////////////////////////////////////////////////////////////////////////////
CPlot3D CCamera::GetIntersectionWithZero()
{
    CPlot3D axisZ = GetZAxis();

    if (fabs(axisZ.z) <= 1e-5)
        return cp;
    else
    {
        float alpha = -cp.z / axisZ.z;
        if (alpha <= 0)
            return cp;
        else
        {
            return (cp + axisZ * alpha);
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
int CCamera::Segment()
{
    return 1;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CCamera::BeginCameraPlacementsSave( LPCSTR pszFileName )
// setup mode for camera saving
BOOL CCamera::BeginCameraPlacementsSave(LPCSTR pszFileName)
{
    CFileFind find;
    CFile file;

    BOOL bCheck = find.FindFile(".\\camera\\camera.cam");
    if (bCheck == TRUE)
    {
        int nMaxNumber = 0;
        int nNumber = 0;
        bCheck = find.FindFile(".\\camera\\camera0*.cam");
        while (bCheck)
        {
            bCheck = find.FindNextFile();
            CString szNumber = find.GetFileTitle();
            szNumber = szNumber.Right(4);
            nNumber = atoi(szNumber);
            if (nNumber > nMaxNumber)
                nMaxNumber = nNumber;
        }
        CString szPrevCameraFileName;
        szPrevCameraFileName.Format(".\\camera\\camera%.4d.cam", nMaxNumber + 1);
        bCheck = MoveFile(".\\camera\\camera.cam", szPrevCameraFileName);
    }

    theWorld.SetFlag(WC_SAVE_CAMERA);

    if (file.Open(CString(".\\camera\\") + pszFileName, CFile::modeCreate | CFile::modeWrite) ==
        FALSE)
        return FALSE;

    szPlacementsFileName = CString(".\\camera\\") + pszFileName;
    SetFlag(CAM_SAVE_PLACEMENTS);

    file.Close();

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CCamera::SaveCurrentCameraPlacement( float fpTime )
// save one step of camera placement
BOOL CCamera::SaveCurrentCameraPlacement(float fpTime)
{
    SCameraPlacement cameraplacement;
    CFile file;

    if (!CheckFlag(CAM_SAVE_PLACEMENTS))
        return FALSE;
    if (file.Open(szPlacementsFileName, CFile::modeWrite | CFile::modeNoTruncate) == FALSE)
        return FALSE;

    file.SeekToEnd();

    cameraplacement.fpTime = fpTime;
    cameraplacement.plot = cp;
    cameraplacement.quat = rotation;

    file.Write(&cameraplacement, sizeof(cameraplacement));

    file.Close();

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CCamera::EndCameraPlacementsSave()
// end of camera saving mode
BOOL CCamera::EndCameraPlacementsSave()
{
    ResetFlag(CAM_SAVE_PLACEMENTS);
    szPlacementsFileName.Empty();

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CCamera::LoadCameraPlacements( LPCSTR pszFileName )
// load previously saved camera placements
BOOL CCamera::LoadCameraPlacements(LPCSTR pszFileName, int nRemove)
{
    CFile file;

    CString cameraPath = CString(theWorld.GetWorkPath()) + "camera\\" + pszFileName;
    std::string resolvedCamera = ModRuntime::CameraPath((LPCSTR)cameraPath);
    if (file.Open(resolvedCamera.c_str(), CFile::modeRead) ==
        FALSE)
        return FALSE;

    placements.SetSize(file.GetLength() / sizeof(SCameraPlacement));
    file.Read(placements.GetData(), placements.GetSize() * sizeof(SCameraPlacement));

    // CRAP
    if (nRemove && (placements.GetSize() > nRemove))
    {
        for (int i = 1; i < nRemove; i++)
        {
            if ((placements[i].fpTime - placements[i - 1].fpTime) > 500)
            {
                for (int j = 0; j < i; j++)
                {
                    placements[j].plot = placements[i].plot;
                    placements[j].quat = placements[i].quat;
                }
                break;
            }
        }
    }
    // end of CRAP

    file.Close();

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CCamera::PlayCamera(float fpTime)
{
    float fpCurrTime, fpNextTime, dt;

    if (!CheckFlag(CAM_USE_PLACEMENTS) || !placements.GetSize())
        return FALSE;

    if (nCurrentPlacementIndex < placements.GetUpperBound())
    {
        fpNextTime = placements[nCurrentPlacementIndex + 1].fpTime + fpCameraTimeShift;
        while ((fpTime > fpNextTime))
        {
            nCurrentPlacementIndex++;
            if (nCurrentPlacementIndex < placements.GetUpperBound())
                fpNextTime = placements[nCurrentPlacementIndex + 1].fpTime + fpCameraTimeShift;
            else
            {
                cp = placements[placements.GetUpperBound()].plot;
                rotation = placements[placements.GetUpperBound()].quat;
                EndPlayCamera();
                return FALSE;
            }
        }
        fpCurrTime = placements[nCurrentPlacementIndex].fpTime + fpCameraTimeShift;
        if (fpNextTime - fpCurrTime != 0)
            dt = (fpTime - fpCurrTime) / (fpNextTime - fpCurrTime);
        else
            dt = 0;
        //
        cp = placements[nCurrentPlacementIndex].plot +
             dt * (placements[nCurrentPlacementIndex + 1].plot -
                   placements[nCurrentPlacementIndex].plot);
        rotation.Slerp(dt, placements[nCurrentPlacementIndex].quat,
                       placements[nCurrentPlacementIndex + 1].quat);

        return TRUE;
    }
    else if (placements.GetSize())
    {
        cp = placements[placements.GetUpperBound()].plot;
        rotation = placements[placements.GetUpperBound()].quat;
    }

    return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CCamera::InitCameraPlacement()
// setup camera to first placement...
BOOL CCamera::InitCameraPlacement()
{
    if (placements.GetSize())
    {
        cp = placements[0].plot;
        rotation = placements[0].quat;
        return TRUE;
    }

    return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CCamera::LockScrollPoint(const CPlot3D &ptScrollPoint)
{
}
/////////////////////////////////////////////////////////////////////////////////////
void CCamera::UnlockScrollPoint()
{
    ResetFlag(CAM_POINT_LOCKED);
}
/////////////////////////////////////////////////////////////////////////////////////
void CCamera::DirectRotate(const CPlot2D &ptRotate)
{
    return;

    CPlot2D ptVector = ptRotate - ptLastDRPoint;

    ptLastDRPoint = ptRotate;
}
/////////////////////////////////////////////////////////////////////////////////////
void CCamera::SetScrollValue(const CPlot2D &ptScroll)
{
    CPlot3D xaxis = GetXAxis();
    CPlot3D yaxis = GetYAxis();
    xaxis.z = 0;
    xaxis *= 1.0f / fabs(xaxis) * ptScroll.x;
    yaxis.z = 0;
    yaxis *= 1.0f / fabs(yaxis) * ptScroll.y;
    scroll.desired = xaxis + yaxis;
}
void CCamera::SetRotateValue(const float fpValue)
{
    if (IsFalloutModeActive()) return;
    rotate.desired = fpValue;
    if (fabs(fpValue) > fabs(rotate.value))
        rotate.value = rotate.desired;
}
void CCamera::SetPitchValue(const float fpValue)
{
    if (IsFalloutModeActive()) return;
    pitch.desired = fpValue;
    if (fabs(fpValue) > fabs(pitch.value))
        pitch.value = pitch.desired;
}
void CCamera::ApplyMouseYaw(float delta)
{
    if (IsFalloutModeActive()) return;
    owned::CameraQuaternion orientation;
    qRotate.GetComponents(orientation.w, orientation.x, orientation.y, orientation.z);
    owned::apply_camera_mouse_yaw(orientation, delta);
    qRotate = CQuaternion(orientation.w, orientation.x, orientation.y, orientation.z);
}
void CCamera::ApplyMousePitch(float delta)
{
    if (IsFalloutModeActive()) return;
    const bool alternate = CheckFlag(CAM_ALTERNATIVE_RESTRICTIONS) != 0;
    owned::apply_camera_mouse_pitch(fPitch, delta,
        ToRadian(alternate ? pSettings->fpCameraBriefingMinPitch : pSettings->fpCameraDefaultMinPitch),
        ToRadian(alternate ? pSettings->fpCameraBriefingMaxPitch : pSettings->fpCameraDefaultMaxPitch));
}
void CCamera::NormalizeMouseDelta(CPoint& delta, int width, int height)
{
    if (width != mouseCarryWidth || height != mouseCarryHeight)
    {
        mouseCarryX = mouseCarryY = 0;
        mouseCarryWidth = width;
        mouseCarryHeight = height;
    }
    int deltaX = static_cast<int>(delta.x);
    int deltaY = static_cast<int>(delta.y);
    owned::CameraMouseCarry carry{mouseCarryX, mouseCarryY};
    owned::normalize_camera_mouse_delta(deltaX, deltaY, width, height, carry);
    delta.x = deltaX;
    delta.y = deltaY;
    mouseCarryX = carry.x;
    mouseCarryY = carry.y;
}
void CCamera::SetZoomValue(const float fpValue)
{
    zoom.desired = fpValue;
}
void CCamera::StopAllMoving()
{
    SetScrollValue(CPlot2D(0, 0));
    SetPitchValue(0);
    SetRotateValue(0);
    SetZoomValue(0);
}
/////////////////////////////////////////////////////////////////////////////////////
void CCamera::SetAnchorPoint(const CPlot3D &pt)
{
    ptAnchorPoint = pt;

    /*
    float fAverageZ = GetSplineHeight( pt, 8, 8 );
    */

    float fAverageZ, fMinZ, fMaxZ;
    EstimateAverageHeight(pt, VNULL, VNULL, theSettings.fCameraTerrainSensetiveArea * 2.0f / 4.0f,
                          theSettings.fCameraTerrainSensetiveArea,
                          theSettings.fCameraTerrainSensetiveArea, fAverageZ, fMinZ, fMaxZ);
    ptAnchorPoint.z = fAverageZ + fAnchorHeight;
}
/////////////////////////////////////////////////////////////////////////////////////
void CCamera::MoveAnchor(const CPlot3D &pt)
{
    CPlot3D ptNewAnchor = ptAnchorPoint + pt;
    ptNewAnchor.x = Min(Max(ptNewAnchor.x, 0.0f), theWorld.GetTerrain()->GetMaxX() - 1);
    ptNewAnchor.y = Min(Max(ptNewAnchor.y, 0.0f), theWorld.GetTerrain()->GetMaxY() - 1);
    SetAnchorPoint(ptNewAnchor);
}
/////////////////////////////////////////////////////////////////////////////////////
void CCamera::MoveAnchorXY(const CPlot3D &pt)
{
    ptAnchorPoint.x = Min(Max(ptAnchorPoint.x + pt.x, 0.0f), theWorld.GetTerrain()->GetMaxX() - 1);
    ptAnchorPoint.y = Min(Max(ptAnchorPoint.y + pt.y, 0.0f), theWorld.GetTerrain()->GetMaxY() - 1);
}
/////////////////////////////////////////////////////////////////////////////////////
void CCamera::ReconstructCameraPlacement()
{
    SetOrientation(qRotate);
    Rotate(GetXAxis(), -(ToRadian(90.0f) + fPitch));
    cp = ptAnchorPoint - GetZAxis() * fDistance;
    CameraTerrainCollision::ConstrainOrbit(this);
}
/////////////////////////////////////////////////////////////////////////////////////
const CPlot3D &CCamera::GetObservationPosition() const
{
    if (CheckFlag(CAM_USE_PLACEMENTS | CAM_CANT_SCROLL))
        return GetCP();
    else
        return GetAnchorPoint();
}
/////////////////////////////////////////////////////////////////////////////////////
void CCamera::DampReleasedPan(float seconds, float releaseTime)
{
    if (seconds > 0.0f && scroll.desired.x == 0.0f &&
        scroll.desired.y == 0.0f && scroll.desired.z == 0.0f)
        scroll.value *= expf(-seconds / releaseTime);
}

void CCamera::Step(float fpStep)
{
    FrameDiagnostics::Scope timing(FrameDiagnostics::Camera);
    FrameDiagnostics::CameraStep(fpStep * DW_DEFAULT_SEGMENT_TIME,
        fabs2(scroll.desired) > 0 || fabs2(scroll.value) > 0 ||
        rotate.desired != 0 || rotate.value != 0 || pitch.desired != 0 || pitch.value != 0 ||
        zoom.desired != 0 || zoom.value != 0,
        CheckFlag(CAM_CANT_SCROLL | CAM_USE_PLACEMENTS | CAM_FPS_MODE));
    // set sound listener
    {
        CPlot3D ptListener = GetCP();
        if (CheckFlag(CAM_USE_PLACEMENTS | CAM_CANT_SCROLL) == 0)
        {
            ptListener = GetAnchorPoint();
            if (theWorld.GetTerrain())
                ptListener.z = theWorld.GetSurfaceValue(ptListener.x, ptListener.y) + fAnchorHeight;
        }
        theWorld.GetSoundManager()->SetFXListener(ptListener, GetZAxis());
    }

    //
    if (CheckFlag(CAM_CANT_SCROLL | CAM_USE_PLACEMENTS))
        return;
    // camera FPS mode
    CObject3D *pCarrier = (CObject3D *)theWorld.GetMapObjectManager()->Find(dwCarrierID);
    if (CheckFlag(CAM_FPS_MODE) && pCarrier)
    {
        float fHeight = pCarrier->GetBoundingVolume().ptCenter.z +
                        pCarrier->GetBoundingVolume().ptMax.z;
        CPlot3D ptCarrier = pCarrier->GetCP();
        ptCarrier.z += fHeight;
        SetCP(ptCarrier);
        CPlot3D ptDir = -pCarrier->GetYAxis();
        ptDir.z = 0;
        ptDir *= 1.0f / fabs(ptDir);

        if (fabs2(lastFPSdir - ptDir) > 0.001)
        {
            SetOrientationZ(ptDir);
            lastFPSdir = ptDir;
        }
        AutoLeveling();

        return;
    }
    //
    if (pCarrier)
        MoveAnchorXY((pCarrier->GetCP() - ptAnchorPoint) * 0.2f);
    // scrolling
    scroll.Accelerate(fpStep);
    if (fabs2(scroll.value))
    {
        float fScrollSpeed;
        if (CheckFlag(CAM_ALTERNATIVE_RESTRICTIONS))
        {
            fScrollSpeed = 0.2f + (fDistance - pSettings->fpCameraBriefingMinDistanceToCarrier) /
                                      (1.125f * (pSettings->fpCameraBriefingMaxDistanceToCarrier -
                                                 pSettings->fpCameraBriefingMinDistanceToCarrier));
        }
        else
        {
            fScrollSpeed = 0.2f + (fDistance - pSettings->fpCameraMinDistanceToCarrier) /
                                      (1.125f * (pSettings->fpCameraMaxDistanceToCarrier -
                                                 pSettings->fpCameraMinDistanceToCarrier));
        }
        MoveAnchorXY(scroll.value * fpStep * fScrollSpeed);
        ResetTracking();
        // scroll restriction
        if (CheckFlag(CAM_RESTRICTED))
        {
            CPlot3D pt = ptAnchorPoint - ptRestrictionCenter;
            pt.z = 0;
            float fLen2 = fabs2(pt);
            if (fLen2 > fRestrictionRadius * fRestrictionRadius)
            {
                pt *= 1.0f / sqrt(fLen2);
                pt = ptRestrictionCenter + pt * fRestrictionRadius;
                ptAnchorPoint.x = pt.x;
                ptAnchorPoint.y = pt.y;
            }
        }
    }
    {
        // подтянуть anchor point по координате Z
        /*
        float fAverageZ = GetSplineHeight( ptAnchorPoint, 8, 8 );
        */
        float fAverageZ, fMinZ, fMaxZ;
        EstimateAverageHeight(ptAnchorPoint, VNULL, VNULL,
                              theSettings.fCameraTerrainSensetiveArea * 2.0f / 4.0f,
                              theSettings.fCameraTerrainSensetiveArea,
                              theSettings.fCameraTerrainSensetiveArea, fAverageZ, fMinZ, fMaxZ);
        if (pCarrier)
            fAverageZ = pCarrier->GetCPZ();
        ptAnchorPoint.z += 0.1f * (fAverageZ + fAnchorHeight - ptAnchorPoint.z);
    }
    // pitching
    pitch.Accelerate(fpStep);
    if (pitch.value)
    {
        fPitch += pitch.value * fpStep;
        // apply pitch restrictions
        if (CheckFlag(CAM_ALTERNATIVE_RESTRICTIONS))
        {
            if (fPitch > ToRadian(pSettings->fpCameraBriefingMaxPitch))
                fPitch = ToRadian(pSettings->fpCameraBriefingMaxPitch);
            if (fPitch < ToRadian(pSettings->fpCameraBriefingMinPitch))
                fPitch = ToRadian(pSettings->fpCameraBriefingMinPitch);
        }
        else
        {
            if (fPitch > ToRadian(pSettings->fpCameraDefaultMaxPitch))
                fPitch = ToRadian(pSettings->fpCameraDefaultMaxPitch);
            if (fPitch < ToRadian(pSettings->fpCameraDefaultMinPitch))
                fPitch = ToRadian(pSettings->fpCameraDefaultMinPitch);
        }
    }
    // rotating
    rotate.Accelerate(fpStep);
    if (rotate.value)
        qRotate *= CQuaternion(CPlot3D(0, 0, 1), rotate.value * fpStep);
    // zooming
    zoom.Accelerate(fpStep);
    if (zoom.value)
        fDistance += zoom.value * fpStep;
    // apply zoom restrictions
    if (CheckFlag(CAM_ALTERNATIVE_RESTRICTIONS))
    {
        if (fDistance < pSettings->fpCameraBriefingMinLimitDistanceToCarrier)
            fDistance = pSettings->fpCameraBriefingMinLimitDistanceToCarrier;
        else if (fDistance < pSettings->fpCameraBriefingMinDistanceToCarrier)
        {
            fDistance += fpStep * 1.1f *
                         (pSettings->fpCameraBriefingMinDistanceToCarrier - fDistance) /
                         (pSettings->fpCameraBriefingMinDistanceToCarrier -
                          pSettings->fpCameraBriefingMinLimitDistanceToCarrier);
        }
        else if (fDistance > pSettings->fpCameraBriefingMaxLimitDistanceToCarrier)
            fDistance = pSettings->fpCameraBriefingMaxLimitDistanceToCarrier;
        else if (fDistance > pSettings->fpCameraBriefingMaxDistanceToCarrier)
        {
            fDistance -= fpStep * 1.1f *
                         (fDistance - pSettings->fpCameraBriefingMaxDistanceToCarrier) /
                         (pSettings->fpCameraBriefingMaxLimitDistanceToCarrier -
                          pSettings->fpCameraBriefingMaxDistanceToCarrier);
        }
    }
    else
    {
        if (fDistance < pSettings->fpCameraMinLimitDistanceToCarrier)
            fDistance = pSettings->fpCameraMinLimitDistanceToCarrier;
        else if (fDistance < pSettings->fpCameraMinDistanceToCarrier)
        {
            fDistance += fpStep * 1.1f * (pSettings->fpCameraMinDistanceToCarrier - fDistance) /
                         (pSettings->fpCameraMinDistanceToCarrier -
                          pSettings->fpCameraMinLimitDistanceToCarrier);
        }
        else if (fDistance > pSettings->fpCameraMaxLimitDistanceToCarrier)
            fDistance = pSettings->fpCameraMaxLimitDistanceToCarrier;
        else if (fDistance > pSettings->fpCameraMaxDistanceToCarrier)
        {
            fDistance -= fpStep * 1.1f * (fDistance - pSettings->fpCameraMaxDistanceToCarrier) /
                         (pSettings->fpCameraMaxLimitDistanceToCarrier -
                          pSettings->fpCameraMaxDistanceToCarrier);
        }
    }
    //
    ReconstructCameraPlacement();
    // apply non-restricted earthquake effect
    if (IsEarthquake())
    {
        float fVal = 0;

        for (auto pos = earthquakes.begin(); pos != earthquakes.end();)
        {
            // exp(-x*0.3*15)*cos(fps*(Pi/2)*x)*2; <= x in seconds
            fVal += exp(-pos->fTime * pos->fAttenuationCoeff) *
                    cos(4.0f * FP_PI2 * pos->fTime / 15.0f) * pos->fAmplitude;
            pos->fTime += fpStep;
            if (pos->fTime >= pos->fDuration)
                pos = earthquakes.erase(pos);
            else
                ++pos;
        }

        cp.z += fVal;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CCamera::AddDistance(const float fpValue)
{
    fDistance *= (float)pow(1.05f, fpValue);
    if (!pSettings)
        return;
    if (CheckFlag(CAM_ALTERNATIVE_RESTRICTIONS))
    {
        if (fDistance < pSettings->fpCameraBriefingMinDistanceToCarrier)
            fDistance = pSettings->fpCameraBriefingMinDistanceToCarrier;
        else if (fDistance > pSettings->fpCameraBriefingMaxDistanceToCarrier)
            fDistance = pSettings->fpCameraBriefingMaxDistanceToCarrier;
    }
    else
    {
        if (fDistance < pSettings->fpCameraMinDistanceToCarrier)
            fDistance = pSettings->fpCameraMinDistanceToCarrier;
        else if (fDistance > pSettings->fpCameraMaxDistanceToCarrier)
            fDistance = pSettings->fpCameraMaxDistanceToCarrier;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
bool CCamera::SetCurrentAsShortcut(int nIndex)
{
    if (nIndex >= CAM_MAX_SHORTCUTS)
        return false;

    shortcuts[nIndex].fDistance = fDistance;
    shortcuts[nIndex].fPitch = fPitch;
    shortcuts[nIndex].qRotate = qRotate;
    //
    ExportShortcuts(*pSettings);
    //

    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CCamera::ActivateShortcut(int nIndex)
{
    if (nIndex >= CAM_MAX_SHORTCUTS)
        return false;

    fDistance = shortcuts[nIndex].fDistance;
    fPitch = shortcuts[nIndex].fPitch;
    // qRotate = shortcuts[nIndex].qRotate;

    scroll.desired = scroll.value = VNULL;
    rotate.desired = rotate.value = 0;
    pitch.desired = pitch.value = 0;
    zoom.desired = zoom.value = 0;

    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
void CCamera::InitPlacement(float fRotateAngle)
{
    fDistance = pSettings->fpCameraDefaultDistanceToCarrier;
    qRotate.FromAxisAngle(CPlot3D(0, 0, 1), fRotateAngle);
    fPitch = ToRadian(45.0f);

    ReconstructCameraPlacement();
}
/////////////////////////////////////////////////////////////////////////////////////
void CCamera::InitRotate(float fRotateAngle)
{
    qRotate.FromAxisAngle(CPlot3D(0, 0, 1), fRotateAngle);
    ReconstructCameraPlacement();
}
void CCamera::DiscardFieldCampCamera()
{
    campPlacementValid = campEntered = false;
    campTerrain = nullptr;
    campRequestTime = 0;
    CameraTerrainCollision::DiscardCampState();
}

void CCamera::CaptureFieldCampCamera()
{
    DiscardFieldCampCamera();
    if (this != &theCamera || !theWorld.GetTerrain()) return;
    campPlacement.position = GetCP();
    campPlacement.orientation = GetOrientation();
    campPlacement.anchor = ptAnchorPoint;
    campPlacement.anchorHeight = fAnchorHeight;
    campPlacement.distance = fDistance;
    campPlacement.orbit = qRotate;
    campPlacement.pitch = fPitch;
    campPlacement.carrier = dwCarrierID;
    campPlacement.mode = falloutMode;
    campPlacement.baselineValid = falloutBaselineValid;
    campPlacement.baseline = falloutBaselineRotation;
    campPlacement.baselinePitch = falloutBaselinePitch;
    campPlacement.telephoto = falloutTelephotoMultiplier;
    campTerrain = theWorld.GetTerrain();
    campRequestTime = GetTickCount();
    campPlacementValid = true;
    CameraTerrainCollision::SaveCampState(&theWorld, this);
}

bool CCamera::RestoreFieldCampCamera()
{
    if (!campPlacementValid || theWorldServer.FieldCampReturnHeroID() < 0) return false;
    if (this != &theCamera || !theWorld.GetTerrain() || campTerrain != theWorld.GetTerrain())
    {
        DiscardFieldCampCamera();
        return false;
    }
    // Camp recreates units; retain only their stable ID, never a unit pointer.
    SetCarrier(campPlacement.carrier);
    ptAnchorPoint = campPlacement.anchor;
    fAnchorHeight = campPlacement.anchorHeight;
    fDistance = campPlacement.distance;
    qRotate = campPlacement.orbit;
    fPitch = campPlacement.pitch;
    SetCP(campPlacement.position);
    SetOrientation(campPlacement.orientation);
    falloutMode = campPlacement.mode;
    falloutBriefingMode = FALLOUT_STOCK;
    falloutBriefing = false;
    falloutBaselineValid = campPlacement.baselineValid;
    falloutBaselineRotation = campPlacement.baseline;
    falloutBaselinePitch = campPlacement.baselinePitch;
    falloutTelephotoMultiplier = campPlacement.telephoto;
    scroll.value = scroll.desired = VNULL;
    rotate.value = rotate.desired = 0.0f;
    pitch.value = pitch.desired = 0.0f;
    zoom.value = zoom.desired = 0.0f;
    mouseCarryX = mouseCarryY = 0;
    mouseCarryWidth = mouseCarryHeight = 0;
    CameraTerrainCollision::RestoreCampState(&theWorld, this);
    return true;
}

void CCamera::UpdateFieldCampCamera()
{
    if (!campPlacementValid) return;
    if (!theWorld.GetTerrain() || campTerrain != theWorld.GetTerrain() ||
        !theWorldServer.IsFieldCampPending())
    {
        DiscardFieldCampCamera();
        return;
    }
    if (theWorldServer.IsInFieldCamp() || theWorldServer.FieldCampReturnHeroID() >= 0)
        campEntered = true;
    if (!campEntered && GetTickCount() - campRequestTime > 30000)
        DiscardFieldCampCamera();
    else
        RestoreFieldCampCamera();
}

void CCamera::ResetFalloutMode()
{
    DiscardFieldCampCamera();
    CameraTerrainCollision::Reset();
    falloutMode = FALLOUT_STOCK;
    falloutBriefingMode = FALLOUT_STOCK;
    falloutBriefing = false;
    falloutBaselineValid = false;
}

void CCamera::ReapplyFalloutAfterMove()
{
    if (!IsFalloutModeActive() || !ModRuntime::Current()) return;
    falloutBaselineRotation = qRotate;
    falloutBaselinePitch = fPitch;
    falloutBaselineValid = true;
    ApplyFalloutPlacement();
}

void CCamera::ApplyFalloutPlacement()
{
    if (!IsFalloutModeActive() || !ModRuntime::Current()) return;
    const auto &settings = ModRuntime::Current()->FalloutCameraSettings();
    float yaw = settings.yaw, angle = settings.pitch, multiplier = settings.telephoto;
    if (!(yaw == yaw)) yaw = 45.0f;
    if (!(angle == angle)) angle = 35.264f;
    if (!(multiplier == multiplier)) multiplier = 2.0f;
    falloutTelephotoMultiplier = Max(1.0f, Min(4.0f, multiplier));
    yaw = -yaw;
    while (yaw >= 180.0f) yaw -= 360.0f;
    while (yaw < -180.0f) yaw += 360.0f;
    if (falloutMode == FALLOUT_NORTHEAST) yaw += 180.0f;
    while (yaw >= 180.0f) yaw -= 360.0f;
    while (yaw < -180.0f) yaw += 360.0f;
    angle = Max(5.0f, Min(80.0f, angle));
    rotate.value = rotate.desired = 0.0f;
    pitch.value = pitch.desired = 0.0f;
    qRotate.FromAxisAngle(CPlot3D(0, 0, 1), ToRadian(yaw));
    fPitch = ToRadian(angle);
    ReconstructCameraPlacement();
}

void CCamera::NormalizeNorth(float angle)
{
    if (falloutBriefing) return;
    falloutMode = FALLOUT_STOCK;
    if (falloutBaselineValid) {
        rotate.value = rotate.desired = 0.0f;
        pitch.value = pitch.desired = 0.0f;
        qRotate = falloutBaselineRotation;
        fPitch = falloutBaselinePitch;
        ReconstructCameraPlacement();
    }
    falloutBaselineValid = false;
    InitRotate(angle);
}

void CCamera::ToggleFalloutMode(float normalizeAngle)
{
    if (!ModRuntime::Current()) { InitRotate(normalizeAngle); return; }
    if (falloutBriefing) return;
    if (falloutMode == FALLOUT_STOCK) {
        falloutBaselineRotation = qRotate;
        falloutBaselinePitch = fPitch;
        falloutBaselineValid = true;
        InitRotate(normalizeAngle);
        falloutMode = FALLOUT_SOUTHWEST;
        ApplyFalloutPlacement();
    } else if (falloutMode == FALLOUT_SOUTHWEST) {
        InitRotate(normalizeAngle);
        falloutMode = FALLOUT_NORTHEAST;
        ApplyFalloutPlacement();
    } else {
        falloutMode = FALLOUT_STOCK;
        InitRotate(normalizeAngle);
        if (falloutBaselineValid) {
            rotate.value = rotate.desired = 0.0f;
            pitch.value = pitch.desired = 0.0f;
            qRotate = falloutBaselineRotation;
            fPitch = falloutBaselinePitch;
            ReconstructCameraPlacement();
        }
        falloutBaselineValid = false;
    }
}

void CCamera::SuspendFalloutForBriefing()
{
    if (falloutBriefing) return;
    falloutBriefing = true;
    falloutBriefingMode = falloutMode;
    if (!IsFalloutModeActive()) return;
    falloutMode = FALLOUT_STOCK;
    if (falloutBaselineValid) {
        qRotate = falloutBaselineRotation;
        fPitch = falloutBaselinePitch;
        ReconstructCameraPlacement();
    }
}

void CCamera::ResumeFalloutAfterBriefing()
{
    if (!falloutBriefing) return;
    falloutBriefing = false;
    falloutMode = falloutBriefingMode;
    falloutBriefingMode = FALLOUT_STOCK;
    if (IsFalloutModeActive()) ReapplyFalloutAfterMove();
}
/////////////////////////////////////////////////////////////////////////////////////
float CCamera::GetRotateAngle()
{
    CPlot3D axis;
    float angle;
    qRotate.DecompAxisAngle(axis, angle);
    if (axis.z < 0)
        angle = FP_2PI - angle;
    return angle;
}
/////////////////////////////////////////////////////////////////////////////////////
void CCamera::Load(CSave &file)
{
    DiscardFieldCampCamera();
    CameraTerrainCollision::Reset();
    DWORD dwCameraFlags, dwNewCarrierID;

    file.Read(&ptAnchorPoint, sizeof(ptAnchorPoint));
    file.Read(fAnchorHeight);
    file.Read(&qRotate, sizeof(qRotate));
    file.Read(fPitch);
    file.Read(fDistance);
    file.Read(&scroll, sizeof(scroll));
    file.Read(&rotate, sizeof(rotate));
    file.Read(&pitch, sizeof(pitch));
    file.Read(&zoom, sizeof(zoom));
    file.Read(dwCameraFlags);
    file.Read(dwNewCarrierID);
    //
    if (file.GetVersion() < 111)
        file.Read(shortcuts, sizeof(shortcuts));
    else
        file.Read(shortcuts + 4, sizeof(SCameraShortcut) * 4);
    //
    if (dwCameraFlags & CAM_FPS_MODE)
        SetFPSMode(dwNewCarrierID);
    else
        SetCarrier(dwNewCarrierID);
}
/////////////////////////////////////////////////////////////////////////////////////
void CCamera::Store(CSave &file)
{
    DWORD dwCameraFlags = GetFlags();

    file.Write(&ptAnchorPoint, sizeof(ptAnchorPoint));
    file.Write(fAnchorHeight);
    file.Write(&qRotate, sizeof(qRotate));
    file.Write(fPitch);
    file.Write(fDistance);
    file.Write(&scroll, sizeof(scroll));
    file.Write(&rotate, sizeof(rotate));
    file.Write(&pitch, sizeof(pitch));
    file.Write(&zoom, sizeof(zoom));
    file.Write(dwCameraFlags);
    file.Write(dwCarrierID);
    //
    file.Write(shortcuts + 4, sizeof(SCameraShortcut) * 4);
    // file.Write( shortcuts, sizeof(shortcuts) );
}
/////////////////////////////////////////////////////////////////////////////////////
void CCamera::ImportShortcuts(const SGeneralSettings &settings)
{
    for (int i = 0; i < SETTING_NUM_CAMERA_SHORTCUTS; ++i)
    {
        shortcuts[i].fDistance = settings.camerashortcuts[i].fDistance;
        shortcuts[i].fPitch = settings.camerashortcuts[i].fPitch;
        shortcuts[i].qRotate.FromAxisAngle(CPlot3D(settings.camerashortcuts[i].fAxisX,
                                                   settings.camerashortcuts[i].fAxisY,
                                                   settings.camerashortcuts[i].fAxisZ),
                                           settings.camerashortcuts[i].fAngle);
    }
}
void CCamera::ExportShortcuts(SGeneralSettings &settings)
{
    for (int i = 0; i < SETTING_NUM_CAMERA_SHORTCUTS; ++i)
    {
        settings.camerashortcuts[i].fDistance = shortcuts[i].fDistance;
        settings.camerashortcuts[i].fPitch = shortcuts[i].fPitch;
        CPlot3D ptAxis;
        float fAngle;
        shortcuts[i].qRotate.DecompAxisAngle(ptAxis, fAngle);
        settings.camerashortcuts[i].fAxisX = ptAxis.x;
        settings.camerashortcuts[i].fAxisY = ptAxis.y;
        settings.camerashortcuts[i].fAxisZ = ptAxis.z;
        settings.camerashortcuts[i].fAngle = fAngle;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CCamera::LockCameraControl(bool bLock)
{
    if (bLock)
        SetFlag(CAM_CANT_SCROLL);
    else
    {
        ResetFlag(CAM_CANT_SCROLL);
        /*
        // restore camera control params from the current
        const RECT &rect = theRenderer.GetD3D()->GetWindowRect();
        CPlot3D ptPoint = theWorld.GetTerrainXYZFromScreenXY( (rect.right - rect.left)/2,
        (rect.bottom - rect.top)/2 ); CPlot3D ptRod = ptPoint - GetCP(); fDistance = fabs( ptRod );
        ptRod *= 1.0f/fDistance;
        fPitch = asin( ptRod.z );
        float fRotateAngle = acos( ptRod.x );
        qRotate.FromAxisAngle( CPlot3D(0, 0, 1), fRotateAngle );
        */
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CCamera::PushCameraPlacement()
{
    SCameraPlacement placement;
    placement.plot = GetCP();
    placement.quat = GetOrientation();
    cameraPlacements.push(placement);
}
/////////////////////////////////////////////////////////////////////////////////////
void CCamera::PopCameraPlacement(float fTime)
{
    if (!cameraPlacements.empty())
    {
        const SCameraPlacement &placement = cameraPlacements.top();
        CPlot3D ptZDir = placement.quat.RotateZ();
        MoveTo(placement.plot, placement.plot + ptZDir, fTime);
        cameraPlacements.pop();
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CCamera::MoveTo(const CPlot3D &ptPos, const CPlot3D &ptLookAt, float fTime)
{
    LockCameraControl(true);

    SetCP(ptPos);
    OrientCamera(ptLookAt - ptPos);
}
/////////////////////////////////////////////////////////////////////////////////////
bool CCamera::OrientCamera(float fDirX, float fDirY, float fDirZ)
{
    float fLen = fabs2(fDirX, fDirY, fDirZ);
    if (fLen <= 1e-18)
        return false;
    else
    {
        fLen = 1.0f / sqrt(fLen);
        fDirX *= fLen;
        fDirY *= fLen;
        fDirZ *= fLen;
    }
    float fPitch = asin(-fDirZ);
    float fAngle = acos(fDirY / hypot(fDirX, fDirY));
    if ((fDirX == 0) || (fAngle == 0))
        qRotate = QNULL;
    else
        qRotate.FromAxisAngle(CPlot3D(0, 0, Sign(-fDirX)), fAngle);
    SetOrientation(qRotate);
    Rotate(GetXAxis(), -(ToRadian(90.0f) + fPitch));

    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
void CCamera::SetScrollRestriction(const CPlot3D &ptPos, float fRadius)
{
    ptRestrictionCenter = ptPos;
    fRestrictionRadius = fRadius;
    SetFlag(CAM_RESTRICTED);
}
/////////////////////////////////////////////////////////////////////////////////////
const float FP_MIN_QUAKE_RADIUS = 8;
const float FP_MAX_QUAKE_RADIUS = 20;
void CCamera::BeginEarthquake(const CPlot3D &ptPlace, float fDuration, float fAmplitude,
                              float fAttenuation)
{
    const CPlot3D ptDist = GetObservationPosition() - ptPlace;
    float fDist = hypot(ptDist.x, ptDist.y);
    if (fDist < FP_MAX_QUAKE_RADIUS)
    {
        float fCoeff = fAmplitude;
        if (fDist > FP_MIN_QUAKE_RADIUS)
            fCoeff *= (1.0f -
                       (fDist - FP_MIN_QUAKE_RADIUS) / (FP_MAX_QUAKE_RADIUS - FP_MIN_QUAKE_RADIUS));

        SEarthquakeEffect eq = { 0, fDuration * 15.0f * 2, fCoeff, fAttenuation };
        earthquakes.push_back(eq);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CCamera::SetCameraAs(const SCameraShortcut &pos)
{
    fDistance = pos.fDistance;
    fPitch = pos.fPitch;
    // qRotate = pos.qRotate;

    scroll.desired = scroll.value = VNULL;
    rotate.desired = rotate.value = 0;
    pitch.desired = pitch.value = 0;
    zoom.desired = zoom.value = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CCamera::GetCameraPlacement(SCameraShortcut &pos) const
{
    pos.fDistance = fDistance;
    pos.fPitch = fPitch;
    pos.qRotate = qRotate;
}
/////////////////////////////////////////////////////////////////////////////////////
