#pragma once

#include "I3DScreen.h"
#include "I3DScreenMessageBox.h"
/////////////////////////////////////////////////////////////////////////////////////
class CI3DScrollText;
class CI3DScreenMainMenu : public CI3DScreen
{
private:
    int m_nPrevHitTest, m_nVis;
    bool m_isRain;
    bool m_isDay;
    float m_fTime;

protected:
    CI3DScrollText *m_pScrollText;

public:
    float m_fFogDist;

private:
    void LoadSurfaces();

public:
    CI3DScreenMainMenu(CInterface3D *pI)
        : CI3DScreen(pI)
    {
    }
    virtual void OnActivate();
    virtual void OnDeactivate();
    virtual void OnExitFromModal(DWORD dwRetValue);

    virtual BOOL Draw(CRenderer *pRend);

    // message
    virtual void OnIdle();
    virtual void OnLButtonDown(UINT nFlags, CPoint point);
    virtual void OnMouseMove(UINT nFlags, CPoint point);
    virtual void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
};
/////////////////////////////////////////////////////////////////////////////////////
class CI3DLevelMessageBox : public CI3DScreenMessageBox
{
    int nDifficulty;

public:
    CI3DLevelMessageBox(CInterface3D *pI, LPCTSTR pszTitleID, LPCTSTR pszTextID, DWORD dwPar);
    virtual void OnActivate();
    virtual void OnLButtonDown(UINT nFlags, CPoint point);
    virtual void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
};
