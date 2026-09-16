#pragma once

#include "I3DScreen.h"
#include "Plot.h"
/////////////////////////////////////////////////////////////////////////////////////
class CI3DScreenDebug : public CI3DScreen
{
    int nVis;

public:
    CI3DScreenDebug(CInterface3D *pI)
        : CI3DScreen(pI)
    {
    }
    virtual void OnActivate();
    virtual void OnDeactivate();

    virtual BOOL Draw(CRenderer *pRend);
};
