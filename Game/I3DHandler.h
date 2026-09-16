#pragma once

#include "I3DMain.h"
#include "plot.h"
/////////////////////////////////////////////////////////////////////////////////////
class CI3DHandler
{
protected:
    CInterface3D *pI3D;
    bool bProcessed;

public:
    CI3DHandler(CInterface3D *pI)
        : pI3D(pI)
    {
    }
    virtual ~CI3DHandler() {}

    void SetProcessed(bool bProc = true) { bProcessed = bProc; }
    bool GetProcessed() { return bProcessed; }

    virtual void ResetInputState() {}

    virtual void OnLButtonDown(UINT nFlags, CPoint point) {}
    virtual void OnLButtonUp(UINT nFlags, CPoint point) {}
    virtual void OnMouseMove(UINT nFlags, CPoint point) {}
    virtual void OnRButtonDown(UINT nFlags, CPoint point) {}
    virtual void OnRButtonUp(UINT nFlags, CPoint point) {}
    virtual void OnLButtonDblClk(UINT nFlags, CPoint point) { OnLButtonDown(nFlags, point); }
    virtual void OnRButtonDblClk(UINT nFlags, CPoint point) { OnRButtonDown(nFlags, point); }
    virtual void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags) {}
    virtual void OnChar(UINT nChar, UINT nRepCnt, UINT nFlags) {}
    virtual void OnKeyUp(UINT nChar, UINT nRepCnt, UINT nFlags) {}
    virtual BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt) { return FALSE; }
};
/////////////////////////////////////////////////////////////////////////////////////
class CI3DHandlerCamera : public CI3DHandler
{
private:
    CPoint ptRButDown;
    bool bRButDown;
    float fUpDown;
    float fLeftRight;
    float fPageUpDown;

public:
    CI3DHandlerCamera(CInterface3D *pI);

    virtual void OnMouseMove(UINT nFlags, CPoint point);
    virtual void OnRButtonDown(UINT nFlags, CPoint point);
    virtual void OnRButtonUp(UINT nFlags, CPoint point);
    virtual void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
    virtual void OnKeyUp(UINT nChar, UINT nRepCnt, UINT nFlags);
};
/////////////////////////////////////////////////////////////////////////////////////
class CI3DHandlerNewCamera : public CI3DHandler
{
private:
    CPoint ptRButDown;
    CPoint ptLastPos;
    bool bRButDown;
    float fUpDown;
    float fLeftRight;
    float fPageUpDown;

public:
    CI3DHandlerNewCamera(CInterface3D *pI);

    virtual void ResetInputState();

    virtual void OnMouseMove(UINT nFlags, CPoint point);
    virtual void OnRButtonDown(UINT nFlags, CPoint point);
    virtual void OnRButtonUp(UINT nFlags, CPoint point);
    virtual void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
    virtual void OnKeyUp(UINT nChar, UINT nRepCnt, UINT nFlags);
    virtual BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
};
/////////////////////////////////////////////////////////////////////////////////////
class CI3DHandlerMECamera : public CI3DHandler
{
private:
    CPlot2D scroll;

public:
    CI3DHandlerMECamera(CInterface3D *pI);

    virtual void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
    virtual void OnKeyUp(UINT nChar, UINT nRepCnt, UINT nFlags);
};
/////////////////////////////////////////////////////////////////////////////////////
class CI3DHandlerSystemKeys : public CI3DHandler
{
public:
    CI3DHandlerSystemKeys(CInterface3D *pI)
        : CI3DHandler(pI)
    {
    }

    virtual void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
    virtual void OnKeyUp(UINT nChar, UINT nRepCnt, UINT nFlags);
};
/////////////////////////////////////////////////////////////////////////////////////
class CI3DHandlerDebugActions : public CI3DHandler
{
private:
    CArray<int, int> nUnitIDs;

public:
    CI3DHandlerDebugActions(CInterface3D *pI);

    virtual void OnLButtonDown(UINT nFlags, CPoint point);
    virtual void OnRButtonDown(UINT nFlags, CPoint point);
    virtual void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
    virtual void OnKeyUp(UINT nChar, UINT nRepCnt, UINT nFlags);
};
