#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "I3DHandler.h"

#include "Camera.h"
#include "WorldClient.h"
#include "WorldServer.h"
#include "WorldInterface.h"
#include "Terrain.h"
#include "ServerConsts.h"
#include "Unit.h"
#include "PlayerClient.h"
#include "A3Cursor.h"
#include "KeyMap.h"
#include "randomGen.h"
/////////////////////////////////////////////////////////////////////////////////////
CI3DHandlerCamera::CI3DHandlerCamera(CInterface3D *pI)
    : CI3DHandler(pI)
{
    bRButDown = false;
    fLeftRight = 0;
    fUpDown = 0;
    fPageUpDown = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DHandlerCamera::OnRButtonDown(UINT nFlags, CPoint point)
{
    ptRButDown = point;
    bRButDown = true;
    OnMouseMove(nFlags, point);
    SetProcessed();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DHandlerCamera::OnRButtonUp(UINT nFlags, CPoint point)
{
    bRButDown = false;
    theCamera.SetRotateValue(0);
    theCamera.SetPitchValue(0);
    theCamera.SetScrollValue(CPlot2D(0, 0));
    SetProcessed();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DHandlerCamera::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    switch (nChar)
    {
    case VK_UP:
        fUpDown = 1;
        SetProcessed();
        break;
    case VK_DOWN:
        fUpDown = -1;
        SetProcessed();
        break;
    case VK_LEFT:
        fLeftRight = -1;
        SetProcessed();
        break;
    case VK_RIGHT:
        fLeftRight = 1;
        SetProcessed();
        break;
    case VK_PRIOR:
        fPageUpDown = -1;
        SetProcessed();
        break;
    case VK_NEXT:
        fPageUpDown = 1;
        SetProcessed();
        break;
    }
    if (pI3D->IsCtrlDown())
    {
        theCamera.SetScrollValue(CPlot2D(fLeftRight, -fUpDown) *
                                 theSettings.cctCameraScroll.GetKeyboardValue());
        theCamera.SetRotateValue(0);
        theCamera.SetZoomValue(-fPageUpDown * theSettings.cctCameraZoom.GetKeyboardValue());
        theCamera.SetPitchValue(0);
    }
    else
    {
        theCamera.SetScrollValue(CPlot2D(0, 0));
        theCamera.SetRotateValue(fLeftRight * theSettings.cctCameraRotate.GetKeyboardValue());
        theCamera.SetZoomValue(fUpDown * theSettings.cctCameraZoom.GetKeyboardValue());
        theCamera.SetPitchValue(fPageUpDown * theSettings.cctCameraPitch.GetKeyboardValue());
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DHandlerCamera::OnKeyUp(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    switch (nChar)
    {
    case VK_UP:
    case VK_DOWN:
        fUpDown = 0;
        SetProcessed();
        break;
    case VK_LEFT:
    case VK_RIGHT:
        fLeftRight = 0;
        SetProcessed();
        break;
    case VK_PRIOR:
    case VK_NEXT:
        fPageUpDown = 0;
        SetProcessed();
        break;
    }
    if (pI3D->IsCtrlDown())
    {
        theCamera.SetScrollValue(CPlot2D(fLeftRight, -fUpDown) *
                                 theSettings.cctCameraScroll.GetKeyboardValue());
        theCamera.SetRotateValue(0);
        theCamera.SetZoomValue(-fPageUpDown * theSettings.cctCameraZoom.GetKeyboardValue());
        theCamera.SetPitchValue(0);
    }
    else
    {
        theCamera.SetScrollValue(CPlot2D(0, 0));
        theCamera.SetRotateValue(fLeftRight * theSettings.cctCameraRotate.GetKeyboardValue());
        theCamera.SetZoomValue(fUpDown * theSettings.cctCameraZoom.GetKeyboardValue());
        theCamera.SetPitchValue(fPageUpDown * theSettings.cctCameraPitch.GetKeyboardValue());
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DHandlerCamera::OnMouseMove(UINT nFlags, CPoint point)
{
    if (bRButDown)
    {
        pI3D->ScreenToVirtual(point);

        float x = point.x / 400.f - 1;
        float y = point.y / 300.f - 1;

        if (pI3D->IsCtrlDown())
        {
            theCamera.SetRotateValue(theSettings.fCameraXAxisDir * x *
                                     theSettings.cctCameraRotate.GetMouseValue());
            theCamera.SetPitchValue(theSettings.fCameraYAxisDir * y *
                                    theSettings.cctCameraPitch.GetMouseValue());
        }
        else
            theCamera.SetScrollValue(CPlot2D(x, y) * theSettings.cctCameraScroll.GetMouseValue());
        SetProcessed();
    }
}
/////////////////////////////////////////////////////////////////////////////////////
CI3DHandlerNewCamera::CI3DHandlerNewCamera(CInterface3D *pI)
    : CI3DHandler(pI)
{
    bRButDown = false;
    fLeftRight = 0;
    fUpDown = 0;
    fPageUpDown = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DHandlerNewCamera::OnRButtonDown(UINT nFlags, CPoint point)
{
    ptRButDown = point;
    bRButDown = true;
    ptLastPos = point;
    pI3D->ScreenToVirtual(ptLastPos);
    //	OnMouseMove( nFlags, point );
    theCursor.ResetIncrement();
    theCursor.LockPos(true);
    SetProcessed();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DHandlerNewCamera::OnRButtonUp(UINT nFlags, CPoint point)
{
    bRButDown = false;
    theCamera.SetRotateValue(0);
    theCamera.SetPitchValue(0);
    theCamera.SetScrollValue(CPlot2D(0, 0));
    theCursor.ResetIncrement();
    CPoint pt = theCursor.GetPos();
    theCursor.LockPos(false);
    SetCursorPos(pt.x, pt.y);
    SetProcessed();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DHandlerNewCamera::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    switch (NKeyMap::keyMap[nFlags & 0x1ff])
    {
    case KA_CAMERA_UP:
        fUpDown = 1;
        SetProcessed();
        break;
    case KA_CAMERA_DOWN:
        fUpDown = -1;
        SetProcessed();
        break;
    case KA_CAMERA_LEFT:
        fLeftRight = -1;
        SetProcessed();
        break;
    case KA_CAMERA_RIGHT:
        fLeftRight = 1;
        SetProcessed();
        break;
    case KA_CAMERA_ZOOM_IN:
        fPageUpDown = -1;
        SetProcessed();
        break;
    case KA_CAMERA_ZOOM_OUT:
        fPageUpDown = 1;
        SetProcessed();
        break;
    case KA_CAMERA1:
        if (pI3D->IsCtrlDown() || pI3D->IsAltDown())
            theCamera.SetCurrentAsShortcut(0);
        else
            theCamera.ActivateShortcut(0);
        SetProcessed();
        return;
    case KA_CAMERA2:
        if (pI3D->IsCtrlDown() || pI3D->IsAltDown())
            theCamera.SetCurrentAsShortcut(1);
        else
            theCamera.ActivateShortcut(1);
        SetProcessed();
        return;
    case KA_CAMERA3:
        if (pI3D->IsCtrlDown() || pI3D->IsAltDown())
            theCamera.SetCurrentAsShortcut(2);
        else
            theCamera.ActivateShortcut(2);
        SetProcessed();
        return;
    case KA_CAMERA4:
        if (pI3D->IsCtrlDown() || pI3D->IsAltDown())
            theCamera.SetCurrentAsShortcut(3);
        else
            theCamera.ActivateShortcut(3);
        SetProcessed();
        return;
    }
    if (!GetProcessed())
        return;
    if (pI3D->IsCtrlDown() || pI3D->IsAltDown())
    {
        theCamera.SetScrollValue(CPlot2D(0, 0));
        theCamera.SetRotateValue(fLeftRight * 1.0f *
                                 theSettings.cctCameraRotate.GetKeyboardValue());
        theCamera.SetZoomValue(fPageUpDown * theSettings.cctCameraZoom.GetKeyboardValue());
        theCamera.SetPitchValue(fUpDown * 50.0f * theSettings.cctCameraPitch.GetKeyboardValue());
    }
    else
    {
        theCamera.SetScrollValue(CPlot2D(fLeftRight, -fUpDown) *
                                 theSettings.cctCameraScroll.GetKeyboardValue());
        theCamera.SetRotateValue(0);
        theCamera.SetZoomValue(fPageUpDown * theSettings.cctCameraZoom.GetKeyboardValue());
        theCamera.SetPitchValue(0);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DHandlerNewCamera::OnKeyUp(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    switch (NKeyMap::keyMap[nFlags & 0x1ff])
    {
    case KA_CAMERA_UP:
    case KA_CAMERA_DOWN:
        fUpDown = 0;
        SetProcessed();
        break;
    case KA_CAMERA_LEFT:
    case KA_CAMERA_RIGHT:
        fLeftRight = 0;
        SetProcessed();
        break;
    case KA_CAMERA_ZOOM_IN:
    case KA_CAMERA_ZOOM_OUT:
        fPageUpDown = 0;
        SetProcessed();
        break;
    }
    if (!GetProcessed())
        return;
    if (pI3D->IsCtrlDown() || pI3D->IsAltDown())
    {
        theCamera.SetScrollValue(CPlot2D(0, 0));
        theCamera.SetRotateValue(fLeftRight * theSettings.cctCameraRotate.GetKeyboardValue());
        theCamera.SetZoomValue(fPageUpDown * theSettings.cctCameraZoom.GetKeyboardValue());
        theCamera.SetPitchValue(fUpDown * theSettings.cctCameraPitch.GetKeyboardValue());
    }
    else
    {
        theCamera.SetScrollValue(CPlot2D(fLeftRight, -fUpDown) *
                                 theSettings.cctCameraScroll.GetKeyboardValue());
        theCamera.SetRotateValue(0);
        theCamera.SetZoomValue(fPageUpDown * theSettings.cctCameraZoom.GetKeyboardValue());
        theCamera.SetPitchValue(0);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DHandlerNewCamera::OnMouseMove(UINT nFlags, CPoint point)
{
    if (bRButDown)
    {
        CPoint ptIncrement = theCursor.GetIncrement();
        pI3D->ScreenToVirtual(point);
        pI3D->ScreenToVirtualLocal(ptIncrement);

        float x = point.x / 400.f - 1;
        float y = point.y / 300.f - 1;

        if (pI3D->IsCtrlDown() || pI3D->IsAltDown())
        {
            CPlot2D pt(static_cast<float>(ptIncrement.x) / 400.0f,
                       static_cast<float>(ptIncrement.y) / 300.0f);
            theCamera.SetScrollValue(pt * theSettings.cctCameraScroll.GetMouseValue());
        }
        else
        {
            CPlot2D pt(static_cast<float>(ptIncrement.x) / 400.0f,
                       static_cast<float>(ptIncrement.y) / 300.0f);
            theCamera.SetRotateValue(theSettings.fCameraXAxisDir * pt.x * 10.0f *
                                     theSettings.cctCameraRotate.GetMouseValue());
            // we can start pitch only in the case of vertical increment greater then horizontal!
            float fValueX = abs(ptIncrement.x); // * theSettings.cctCameraRotate.GetMouseValue();
            float fValueY = abs(ptIncrement.y); // * theSettings.cctCameraPitch.GetMouseValue();
            if (theCamera.IsPitching() || (fValueY > fValueX))
                theCamera.SetPitchValue(theSettings.fCameraYAxisDir * pt.y * 500.0f *
                                        theSettings.cctCameraPitch.GetMouseValue());
            else
                theCamera.SetPitchValue(0);
        }
        theCursor.ResetIncrement();

        theCursor.SetMode(CT_CAMERA);

        SetProcessed();
    }
    else if (theSettings.nCameraBorderScrollArea > 0)
    {
        DWORD dwCursorFlags = 0;
        CPlot2D pt(0, 0);
        if (point.x < theSettings.nCameraBorderScrollArea)
        {
            pt.x = -1.f;
            dwCursorFlags |= 0x0001;
        }
        else if (point.x >= pI3D->GetWidth() - theSettings.nCameraBorderScrollArea)
        {
            pt.x = 1.f;
            dwCursorFlags |= 0x0010;
        }
        if (point.y < theSettings.nCameraBorderScrollArea)
        {
            pt.y = -1.f;
            dwCursorFlags |= 0x0100;
        }
        else if (point.y >= pI3D->GetHeight() - theSettings.nCameraBorderScrollArea)
        {
            pt.y = 1.f;
            dwCursorFlags |= 0x1000;
        }
        E2DCursorType type;
        switch (dwCursorFlags)
        {
        case 0x0000:
            type = CT_DEFAULT;
            break;
        case 0x0100:
            type = CT_SCROLL_U;
            break;
        case 0x0110:
            type = CT_SCROLL_UR;
            break;
        case 0x0010:
            type = CT_SCROLL_R;
            break;
        case 0x1010:
            type = CT_SCROLL_DR;
            break;
        case 0x1000:
            type = CT_SCROLL_D;
            break;
        case 0x1001:
            type = CT_SCROLL_DL;
            break;
        case 0x0001:
            type = CT_SCROLL_L;
            break;
        case 0x0101:
            type = CT_SCROLL_UL;
            break;
        }
        if (!pI3D->IsCtrlDown() && !pI3D->IsAltDown())
            pt = pt +
                 CPlot2D(fLeftRight, -fUpDown) * theSettings.cctCameraScroll.GetKeyboardValue();
        theCursor.SetMode(type);
        theCamera.SetScrollValue(pt);
        if (dwCursorFlags)
            SetProcessed();
    }
    ptLastPos = point;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CI3DHandlerNewCamera::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
    theCamera.AddDistance(-(float)zDelta / WHEEL_DELTA * theSettings.cctCameraZoom.GetMouseValue());
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DHandlerNewCamera::ResetInputState()
{
    bRButDown = false;
    fUpDown = 0;
    fLeftRight = 0;
    fPageUpDown = 0;
    theCursor.LockPos(false);
    theCursor.ResetIncrement();
    theCamera.StopAllMoving();
}
/////////////////////////////////////////////////////////////////////////////////////
CI3DHandlerMECamera::CI3DHandlerMECamera(CInterface3D *pI)
    : CI3DHandler(pI)
{
    scroll = CPlot2D(0, 0);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DHandlerMECamera::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    switch (nChar)
    {
    case VK_PRIOR:
        theCamera.SetZoomValue(1 * theSettings.cctCameraZoom.GetKeyboardValue());
        break;
    case VK_NEXT:
        theCamera.SetZoomValue(-1 * theSettings.cctCameraZoom.GetKeyboardValue());
        break;
    case VK_UP:
        scroll.y = -1;
        break;
    case VK_DOWN:
        scroll.y = 1;
        break;
    case VK_LEFT:
        scroll.x = -1;
        break;
    case VK_RIGHT:
        scroll.x = 1;
        break;
    }
    if (pI3D->IsCtrlDown())
    {
        theCamera.SetScrollValue(CPlot2D(0, 0));
        theCamera.SetRotateValue(scroll.x * theSettings.cctCameraRotate.GetKeyboardValue());
        theCamera.SetPitchValue(30.0f * scroll.y * theSettings.cctCameraPitch.GetKeyboardValue());
    }
    else
    {
        theCamera.SetScrollValue(scroll * theSettings.cctCameraScroll.GetKeyboardValue());
        theCamera.SetRotateValue(0);
        theCamera.SetPitchValue(0);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DHandlerMECamera::OnKeyUp(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    switch (nChar)
    {
    case VK_PRIOR:
    case VK_NEXT:
        theCamera.SetZoomValue(0);
        break;
    case VK_UP:
    case VK_DOWN:
        scroll.y = 0;
        break;
    case VK_LEFT:
    case VK_RIGHT:
        scroll.x = 0;
        break;
    }
    if (pI3D->IsCtrlDown())
    {
        theCamera.SetScrollValue(CPlot2D(0, 0));
        theCamera.SetRotateValue(scroll.x * theSettings.cctCameraRotate.GetKeyboardValue());
        theCamera.SetPitchValue(scroll.y * theSettings.cctCameraPitch.GetKeyboardValue());
    }
    else
    {
        theCamera.SetScrollValue(scroll * theSettings.cctCameraScroll.GetKeyboardValue());
        theCamera.SetRotateValue(0);
        theCamera.SetPitchValue(0);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DHandlerSystemKeys::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
#ifndef NODEBUGINFO
    SetProcessed();
    switch (NKeyMap::keyMap[nFlags & 0x1ff])
    {
    case KA_DEBUG_CAM_SAVE:
        if (theCamera.CheckFlag(CAM_SAVE_PLACEMENTS))
            IWorldClient.StopCameraSave();
        else
            IWorldClient.StartCameraSave("camera.cam");
        break;
    case KA_DEBUG_CAM_LOAD:
        IWorldClient.StartCameraPlay("camera.cam");
        break;
    case KA_DEBUG_CAM_CNTR:
        IWorldClient.StartCameraPlay("cam_cntr.cam");
        break;

    case KA_DEBUG_RND_TERRAIN:
        theWorld.ToggleFlag(WC_RENDER_TERRAIN);
        break;
    case KA_DEBUG_RND_SHADOWS:
        theWorld.ToggleFlag(WC_RENDER_SHADOWS);
        break;
    case KA_DEBUG_RND_UNITS:
        theWorld.ToggleFlag(WC_RENDER_UNITS);
        break;
    case KA_DEBUG_RND_IFACE:
        theInterface.ToggleRender();
        break;
    case KA_DEBUG_RND_OBJECTS:
        theWorld.ToggleFlag(WC_RENDER_OBJECTS);
        break;
    case KA_DEBUG_RND_EFFECTS:
        theWorld.ToggleFlag(WC_RENDER_EFFECTS);
        theWorld.ToggleFlag(WC_RENDER_FLYINGHP);
        break;
    case KA_DEBUG_RND_SKY:
        theWorld.ToggleFlag(WC_RENDER_SKY);
        break;

    case KA_DEBUG_WIREFRAME:
        theWorld.ToggleWireframe();
        break;
    case KA_DEBUG_BBOXES:
        theWorld.ToggleFlag(WC_RENDER_BOUNDBOXES);
        break;

    case KA_DEBUG_VISIBLE_ONLY:
        IWorld.FlipDrawVisibleOnly();
        break;

    case KA_DEBUG_ADD_TIME:
        theWorldServer.SetWorldTime((float)fmod(theWorldServer.GetWorldTime() - 1 + 24, 24));
        break;
    case KA_DEBUG_SUB_TIME:
        theWorldServer.SetWorldTime((float)fmod(theWorldServer.GetWorldTime() + 1 + 24, 24));
        break;

    case KA_DEBUG_ADD_WIND:
        if (theWorld.GetTerrain()->GetWindForce() <= 0.9f)
            theWorld.GetTerrain()->SetWindForce(theWorld.GetTerrain()->GetWindForce() + 0.1f);
        break;
    case KA_DEBUG_SUB_WIND:
        if (theWorld.GetTerrain()->GetWindForce() >= -0.9f)
            theWorld.GetTerrain()->SetWindForce(theWorld.GetTerrain()->GetWindForce() - 0.1f);
        break;

    case KA_DEBUG_WEATHER_CLEAR:
        IWorldServer.SetWeather(EW_CLEAR, N_WEATHER_SNOW_TO_CLEAR);
        break;
    case KA_DEBUG_WEATHER_RAIN:
        IWorldServer.SetWeather(EW_RAIN, N_WEATHER_CLEAR_TO_RAIN);
        break;
    case KA_DEBUG_WEATHER_SNOW:
        IWorldServer.SetWeather(EW_SNOW, N_WEATHER_CLEAR_TO_SNOW);
        break;
    case KA_DEBUG_WEATHER_LGTN: {
        CPoint point;
        CPlot3D ptFrom, ptTo;
        point.x = random.Get(180, 620);
        point.y = random.Get(150, 300);
        theInterface.VirtualToScreen(point);
        ptTo = theWorld.GetTerrainXYZFromScreenXY(point.x, point.y);
        ptTo.z -= 5.f;
        ptFrom.x = ptTo.x + random.GetFloat(-2, 2);
        ptFrom.y = ptTo.y + random.GetFloat(-2, 2);
        ptFrom.z = ptTo.z + 40.f;
        IWorld.InvokeLightning(ptFrom, ptTo);
    }
    break;

    default:
        SetProcessed(false);
        break;
    }
#endif // NODEBUGINFO
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DHandlerSystemKeys::OnKeyUp(UINT nChar, UINT nRepCnt, UINT nFlags)
{
}
/////////////////////////////////////////////////////////////////////////////////////
CI3DHandlerDebugActions::CI3DHandlerDebugActions(CInterface3D *pI)
    : CI3DHandler(pI)
{
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DHandlerDebugActions::OnLButtonDown(UINT nFlags, CPoint point)
{
    CMapObject *pTarget = theWorld.PickFirstUnit(point);
    if (!pI3D->IsCtrlDown())
    {
        if (pTarget)
        {
            nUnitIDs.Add(pTarget->GetID());
            theWorld.SelectObject(pTarget);
        }
        else
        {
            CPlot3D plot = theWorld.GetTerrainXYZFromScreen(point);
            for (int i = 0; i < nUnitIDs.GetSize(); i++)
            {
                CUnit *pU = IWorldClient.GetUnit(nUnitIDs[i]);
                if (pU)
                {
                    CArray<CUnit *, CUnit *> units;
                    units.Add(pU);
                    IWorldClient.GetPlayer()->UnitCommandMoveTo(units, plot, false);
                }
            }
        }
    }
    else
    {
        if (pTarget)
        {
            for (int i = 0; i < nUnitIDs.GetSize(); i++)
            {
                CUnit *pU = IWorldClient.GetUnit(nUnitIDs[i]);
                if (pU)
                {
                    CArray<CUnit *, CUnit *> units;
                    units.Add(pU);
                    IWorldClient.GetPlayer()->UnitCommandAttack(units, pTarget, RPGHL_ALL, false);
                }
            }
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DHandlerDebugActions::OnRButtonDown(UINT nFlags, CPoint point)
{
    nUnitIDs.RemoveAll();
    theWorld.ResetSelection();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DHandlerDebugActions::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    switch (nChar)
    {
    case VK_CONTROL:
        theCursor.SetMode(CT_ATTACK);
        break;
    case VK_SPACE:
        IWorld.CombatPause(!IWorld.IsCombatPause());
        break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DHandlerDebugActions::OnKeyUp(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    switch (nChar)
    {
    case VK_CONTROL:
        theCursor.SetMode(CT_DEFAULT);
        break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
