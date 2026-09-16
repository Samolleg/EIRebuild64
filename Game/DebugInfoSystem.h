#pragma once

#include <string>
#include <map>

#include "IDebugInfoSystem.h"

#include "I3DObject.h"
// --------------------------------------------------------------------------------------------
// Debug output surface
// --------------------------------------------------------------------------------------------
class CDebugInfoSystem : public CDebugInfoSystemInterface
{
private:
    std::map<std::string, std::string> datamap;

    CInterface3D *pI3D;
    CI3DSurface *pSurface; // 2d surface
    CFont *pDebugFont;     // console font

    CRect rcVirtual;
    CRect rcReal;

    bool bVisible; // is visible?

    DWORD dwLastRedrawTime;

public:
    virtual void Set(LPCSTR key, LPCSTR val);
    virtual void Remove(LPCSTR key);
    // virtual void Draw( int x, int y, int xstep, int ystep, HDC hDC );

    void Init(CInterface3D *pI);
    void Fini();

    void SetRect(CRect rc);
    void RedrawSurface();

    void Draw2D();

    void Show(bool bShow);
    void Toggle() { Show(!bVisible); }
    bool IsVisible() { return bVisible; }
};
// --------------------------------------------------------------------------------------------
extern CDebugInfoSystem theDebugInfoSystem;
