#pragma once

#include "I3DScreen.h"
#include <list>
#include "hud_layout_context.h"
class CI3DScreen;
/////////////////////////////////////////////////////////////////////////////////////
class CI3DScreenTutorial : public CI3DScreen
{
private:
    bool mbScript;
    CString mszTutorial;

    CString mszHeader;
    CStringArray mszPages;
    int mnCurrentPage;

    std::list<CI3DFigure *> mpRects;
    std::list<CI3DFigure *> mpIcons;

    float fTime;
    owned::HudLayoutContext missionLayout;
    bool missionLayoutAvailable=false;

private:
    void RedrawPage();
    void Exit();

public:
    CI3DScreenTutorial(CInterface3D *pI, LPCTSTR pszTutorial, int nPage, bool bScript = false)
        : CI3DScreen(pI)
        , mszTutorial(pszTutorial)
        , mnCurrentPage(nPage)
        , mbScript(bScript)
    {
    }
    void CaptureMission(CI3DScreen* mission);
    virtual void OnActivate();
    virtual void OnDeactivate();

    virtual BOOL Draw(CRenderer *pRend);
    virtual void OnIdle();
    virtual void OnLButtonDown(UINT nFlags, CPoint point);
    virtual void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
};
