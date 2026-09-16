#pragma once

// --------------------------------------------------------------------------------------------
//
// --------------------------------------------------------------------------------------------
class CDebugInfoSystemInterface
{
public:
    virtual void Set(LPCSTR pszKey, LPCSTR val) = 0;
    virtual void Remove(LPCSTR key) = 0;

    // virtual void Draw( int x, int y, int xstep, int ystep, HDC hDC ) = 0;
};
// --------------------------------------------------------------------------------------------
CDebugInfoSystemInterface *GetDebugInfoSystem();
