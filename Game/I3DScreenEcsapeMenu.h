#pragma once

#include "I3DScreen.h"
/////////////////////////////////////////////////////////////////////////////////////
class CI3DScreenEscapeMenu : public CI3DScreen
{
private:
    int m_nLastButton;

public:
    CI3DScreenEscapeMenu(CInterface3D *pI)
        : CI3DScreen(pI)
    {
    }
    virtual void OnActivate();
    virtual void OnDeactivate();
    virtual void OnExitFromModal(DWORD dwRetValue);

    virtual void OnIdle();
    virtual void OnLButtonDown(UINT nFlags, CPoint point);
    virtual void OnMouseMove(UINT nFlags, CPoint point);
    virtual void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
};
