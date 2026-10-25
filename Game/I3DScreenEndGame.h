#pragma once

#include "I3DScreen.h"
#include "CMappedResource.h"
#include "MoviePlayback.h"
#include <list>
////////////////////////////////////////////////////////////////////////////////////////////////
class CI3DTetxRTF;

struct SShowText
{
    bool isScroll;
    float fValue;
    CString szTitle;
    CString szFile;
};
typedef std::list<SShowText> CShowTextList;

class CI3DScreenEndGame : public CI3DScreen
{
    CI3DTetxRTF *m_pText;
    CI3DSurface *m_pTitle;
    BOOL m_isFontSmooth;
    CMappedResource m_resText;
    CShowTextList m_aTexts;
    float m_fWaitTime;
    CShowTextList::const_iterator m_itCurText;

    CString mszMovieName;
    Video::Movie movie;

    bool bFromMainMenu;
    bool bCreditsDone;
    DWORD dwOldBGColor;

private:
    void Exit();

public:
    CI3DScreenEndGame(CInterface3D *pI, bool bMain, LPCTSTR pszMovieName)
        : CI3DScreen(pI)
        , bFromMainMenu(bMain)
        , mszMovieName(pszMovieName)
    {
    }

    virtual void OnLButtonDown(UINT nFlags, CPoint point);
    virtual BOOL Draw(CRenderer *pRend);
    virtual void OnActivate();
    virtual void OnDeactivate();
    virtual void OnExitFromModal(DWORD dwRetValue);
    virtual void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
};
