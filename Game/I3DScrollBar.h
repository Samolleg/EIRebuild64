// I3DScrollBar.h: interface for the CI3DScrollBar class.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include "I3DObject.h"

///////////////////////////////////////////////////////////////////////////////////
enum ESBMode
{
    SBM_FREE = 0,
    SBM_TRACK,
    SBM_SCROLL_UP,
    SBM_SCROLL_DOWN
};
///////////////////////////////////////////////////////////////////////////////////
const int N_SCROLL_WIDTH = 10;
const int N_ENDPOINT_SIZEY = 8;
const int N_ENDPOINT_SIZEX = 2;
const int N_POINTER_SIZEX = 21;
const int N_POINTER_SIZEY = 10;
///////////////////////////////////////////////////////////////////////////////////
class CI3DScrollBar : public CI3DObject
{
protected:
    int m_nRange;
    float m_fSpeed;
    int m_nCurPos;
    CRect m_rectView;
    CRect m_rectPointer;
    CPoint m_ptInc;
    float m_fDepth;
    bool m_isUpdated;
    ESBMode m_eMode;
    float fToScroll;

public:
    CI3DScrollBar(CInterface3D *pI, CRect rect, float fDepth = 10, int nRange = 0,
                  float fSpeed = 1);
    virtual ~CI3DScrollBar() {}

    int GetRange();
    BOOL GetPos(int &nPos); // if position changed return true
    virtual void SetPos(int nPos);
    void SetPosToEnd()
    {
        SetPos(m_nRange);
        m_isUpdated = true;
    }
    void ForcePointerUpdate();
    void SetRange(int nRange);
    void SetSpeed(float fSpeed) { m_fSpeed = fSpeed; }
    virtual void SetRect(CRect rect);

    // messages
    virtual void OnMouseMove(UINT nFlags, CPoint point);
    virtual void OnLButtonDown(UINT nFlags, CPoint point);
    virtual void OnLButtonUp(UINT nFlags, CPoint point);
    virtual void OnIdle();
    virtual BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
};
///////////////////////////////////////////////////////////////////////////////////
class CI3DHScrollBar : public CI3DScrollBar
{
public:
    CI3DHScrollBar(CInterface3D *pI, CRect rect, float fDepth = 10, int nRange = 100,
                   float fSpeed = 1);
    virtual ~CI3DHScrollBar() {}

    virtual void SetPos(int nPos);
    virtual void SetRect(CRect rect);

    // messages
    virtual void OnMouseMove(UINT nFlags, CPoint point);
};
///////////////////////////////////////////////////////////////////////////////////
class CI3DSwitcher : public CI3DObject
{
    CStringArray szStates;
    int mnCurState;
    bool mbUpdate;
    CRect rcClick;

public:
    CI3DSwitcher(CInterface3D *pI, CRect rectMake, LPCTSTR pszDefaultState);
    virtual ~CI3DSwitcher() {}

    void AddState(LPCTSTR pszState);
    bool GetState(int &nState);
    void SetState(int nState);

    // messages
    virtual void OnLButtonDown(UINT nFlags, CPoint point);
    virtual void OnLButtonDblClk(UINT nFlags, CPoint point);
};
///////////////////////////////////////////////////////////////////////////////////
enum EEditBoxFlags
{
    EBF_DIGITS = 1,
    EBF_LETTERS = 2,
    EBF_SYMBOLS = 4,
    EBF_DOTS = 8,
    EBF_ALL = 15
};
class CI3DEditBox : public CI3DObject
{
    CRect rcBox;
    EEditBoxFlags flags;
    bool bCentered;
    int nFont;
    int nCharLimit;
    COLORREF crText;
    COLORREF crCursor;

    CString szString;
    int nCursorPos;
    bool bEdited;
    bool bActive;
    bool bUpdated;

private:
    void BeginEdit(bool bClear = true);

public:
    CI3DEditBox(CInterface3D *pI, CRect rectMake);
    virtual ~CI3DEditBox() {}

    void SetParams(int nFlags, bool bCentered, int nFont, int nCharLimit, COLORREF crText,
                   COLORREF crCursor);
    void SetBox(CRect rcNewBox);
    void SetString(LPCTSTR pszString);
    LPCTSTR GetString() { return szString; }
    bool IsEdited() { return bEdited; }
    void Activate(bool bActive);
    bool IsActive() { return bActive; }
    bool IsUpdated()
    {
        bool bU = bUpdated;
        bUpdated = false;
        return bU;
    }

    void Redraw();

    // messages
    virtual void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
    virtual void OnChar(UINT nChar, UINT nRepCnt, UINT nFlags);
};
