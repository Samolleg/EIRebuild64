// 2DScreenWait.h: interface for the C2DScreenWait class.
//
//////////////////////////////////////////////////////////////////////
#pragma once

#ifdef __VIDEO__
#endif // __VIDEO__

#include "dx11/Surface.h"

// this class looks like namespace...
class C2DScreenWait
{
private:
    static CRect rectOnScreen;
    static std::unique_ptr<CDXGISurface> pSurface;
    static bool bIgnoreSteps;

#ifdef __VIDEO__
#else
    static void LoadLogo();
#endif

public:
    static const char *pszProgressName;
    static void Step(int nNumb);
    static void Clear();
    static void Ignore(bool b) { bIgnoreSteps = b; }
};
