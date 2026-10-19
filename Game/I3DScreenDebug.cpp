#include "StdAfx.h"
#include "CorePerf.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "I3DScreenDebug.h"
#include "WorldInterface.h"
#include "WorldClient.h"
#include "Camera.h"
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenDebug::OnActivate()
{
    AddHandler(dbgnew CI3DHandlerSystemKeys(pI3D));
    AddHandler(dbgnew CI3DHandlerDebugActions(pI3D));
    AddHandler(dbgnew CI3DHandlerNewCamera(pI3D));
    theCamera.ResetFlag(CAM_CANT_SCROLL);

    theCamera.SetAnchorPoint(CPlot3D(30, 30, 0));
    theCamera.Rotate(CPlot3D(1, 0, 0), ToRadian(225.0f));
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenDebug::OnDeactivate()
{
    theCamera.SetFlag(CAM_CANT_SCROLL);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CI3DScreenDebug::Draw(CRenderer *pRend)
{
    CorePerf::Scope coreTiming(CorePerf::ScreenDebug);
    IWorldClient.Draw();
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
