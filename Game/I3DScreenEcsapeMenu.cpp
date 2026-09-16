#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "I3DScreenEcsapeMenu.h"
#include "I3DScreenMessageBox.h"
#include "I3DScreenMainMenu.h"
#include "I3DScreenSaveLoad.h"
#include "I3DScreenOptions.h"
#include "I3DMapObjTranslation.h"
#include "I3DMain.h"
#include "WorldInterface.h"
#include "I3DMenuRects.h"
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenEscapeMenu::OnActivate()
{
    SetRect(0, 0, 800, 600);

    CI3DMapObjTranslation *pObj = AddMapObjTranslation();
    pObj->Init("unmoco1", "escMenu00", "escMenu00labels");
    pObj->SetRelativeToScreen(400, 700, 4.8f);
    pObj->SetOrientation(
        CQuaternion(CPlot3D(1, 0, 0),
                    FP_PI2 * 1.00f) /* CQuaternion( CPlot3D(0,0,1), g_PI_DIV_2 * 2.00f ) */);

    CI3DPolyArea *pArea;

    pArea = AddArea();
    pArea->MakeRect(theMenuRects.rcEMSave);
    pArea = AddArea();
    pArea->MakeRect(theMenuRects.rcEMLoad);
    pArea = AddArea();
    pArea->MakeRect(theMenuRects.rcEMReturn);
    pArea = AddArea();
    pArea->MakeRect(theMenuRects.rcEMExit);
    pArea = AddArea();
    pArea->MakeRect(theMenuRects.rcEMOptions);

    // CRAP - here we should store current pause state and pause zone
    GetMapObjTranslation(0)->PlaySpecialAnmation(1);
    pI3D->BeginFX("buttons\\menu\\shift.wav");
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenEscapeMenu::OnDeactivate()
{
    // CRAP here we should restore pause state of zone
    GetMapObjTranslation(0)->PlaySpecialAnmation(0);
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenEscapeMenu::OnExitFromModal(DWORD dwRetValue)
{
    switch (dwRetValue)
    {
    case 0x00100002:
    case 0x00100001:
    case 0x00110001:
        pI3D->ExitFromModal();
        break;
    case 0x00010001:
        IWorldConnector.ReqGoMainMenu();
        break;
    }
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenEscapeMenu::OnIdle()
{
    CI3DScreen::OnIdle();
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenEscapeMenu::OnMouseMove(UINT nFlags, CPoint point)
{
#ifdef _DEBUG
    CString szText;
    szText.Format("Mouse position(%i,%i)\n", point.x, point.y);
//	TRACE0( szText );
#endif
    CI3DMapObjTranslation *pObj = GetMapObjTranslation(0);
    switch (HitTest(point))
    {
    case 0:
        if (m_nLastButton != 0 && !IWorldConnector.IsLMP())
        {
            pI3D->BeginFX("buttons\\menu\\stone.wav");
            pObj->PlaySpecialAnmation(2);
        }
        m_nLastButton = 0;
        break;
    case 1:
        if (m_nLastButton != 1 && !IWorldConnector.IsLMP())
        {
            pI3D->BeginFX("buttons\\menu\\stone.wav");
            pObj->PlaySpecialAnmation(3);
        }
        m_nLastButton = 1;
        break;
    case 2:
        if (m_nLastButton != 2)
        {
            pI3D->BeginFX("buttons\\menu\\stone.wav");
            pObj->PlaySpecialAnmation(5);
        }
        m_nLastButton = 2;
        break;
    case 3:
        if (m_nLastButton != 3)
        {
            pI3D->BeginFX("buttons\\menu\\stone.wav");
            pObj->PlaySpecialAnmation(6);
        }
        m_nLastButton = 3;
        break;
    case 4:
        if (m_nLastButton != 4)
        {
            pI3D->BeginFX("buttons\\menu\\stone.wav");
            pObj->PlaySpecialAnmation(4);
        }
        m_nLastButton = 4;
        break;

        /*		case 5:
                                pObj->PlaySpecialAnmation(6);
                                break;*/
    default:
        if (m_nLastButton != -1)
            pObj->PlayIdleAnmation();
        m_nLastButton = -1;
        break;
    }
    CI3DScreen::OnMouseMove(nFlags, point);
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenEscapeMenu::OnLButtonDown(UINT nFlags, CPoint point)
{
    int ht = HitTest(point);
    if (ht >= 0)
    {
        pI3D->BeginFX("buttons\\menu\\ok.wav");
        SetProcessed();
    }
    switch (ht)
    {
    case 0:
        if (!IWorldConnector.IsLMP())
        {
            CI3DScreenSaveLoad *pScreen = dbgnew CI3DScreenSaveLoad(pI3D);
            pScreen->SetParams(true, true);
            pI3D->DoModal(pScreen);
        }
        break;
    case 1:
        if (!IWorldConnector.IsLMP())
        {
            CI3DScreenSaveLoad *pScreen = dbgnew CI3DScreenSaveLoad(pI3D);
            pScreen->SetParams(false, true);
            pI3D->DoModal(pScreen);
        }
        break;
    case 2:
        pI3D->ExitFromModal();
        break;
    case 3: {
        CI3DScreenMessageBox *pScr = dbgnew CI3DScreenMessageBox(pI3D, "quit", "quit_msg",
                                                                 0x00010000);
        pI3D->DoModal(pScr);
    }
    break;
    case 4: {
        CI3DScreenOptions *pScreen = dbgnew CI3DScreenOptions(pI3D);
        pScreen->SetParams(true);
        pI3D->DoModal(pScreen);
    }
    break;
    }
    CI3DScreen::OnLButtonDown(nFlags, point);
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenEscapeMenu::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    CI3DScreen::OnKeyDown(nChar, nRepCnt, nFlags);
    switch (nChar)
    {
    case VK_ESCAPE:
        pI3D->BeginFX("buttons\\menu\\ok.wav");
        pI3D->ExitFromModal();
        SetProcessed();
        break;
    }
}
////////////////////////////////////////////////////////////////////////////////