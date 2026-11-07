#pragma once

#include "I3DScreen.h"
/////////////////////////////////////////////////////////////////////////////////////
enum
{
    MBF_OK = 1,
    MBF_CANCEL = 2,
    MBF_ENTER = 4,
    MBF_PROGRESS = 8,
};
class CI3DEditBox;
class CI3DScreenMessageBox : public CI3DScreen
{
    friend struct LiveUiAccess;
protected:
    CString szTitle;
    CString szText;
    DWORD dwParam;
    DWORD dwFlags;
    int nTextFormat;
    CI3DEditBox *pEdit;
    LPCTSTR pszTextBuffer;

public:
    CI3DScreenMessageBox(CInterface3D *pI)
        : CI3DScreen(pI)
        , dwParam(0)
    {
    }
    CI3DScreenMessageBox(CInterface3D *pI, LPCTSTR pszTitleID, LPCTSTR pszTextID, DWORD dwPar,
                         DWORD dwFlags = MBF_OK | MBF_CANCEL, int nFormat = DT_CENTER);
    CI3DScreenMessageBox(CInterface3D *pI, LPCTSTR pszTutorialID);
    virtual void OnActivate();
    virtual void OnDeactivate();

    void SetParam(DWORD dwPar) { dwParam = dwPar; }
    void SetTitle(LPCTSTR pszTitle) { szTitle = pszTitle; }
    void SetText(LPCTSTR pszText) { szText = pszText; }

    virtual void OnLButtonDown(UINT nFlags, CPoint point);
    virtual void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
};
