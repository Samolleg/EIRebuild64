#pragma once

#include "plot.h"
#include <vector>

class CStructureSaver;

struct SLDBPrototype
{
    CString szName;         // prototype name
    CPlot2D ptUsePlaces[2]; // places it can be used from
    DWORD dwNumValidPlaces;
    float fpScaleMn;       // multiplier on distance due to different size of levers
    DWORD dwSwitchTime;    // время переключения в сегментах
    CString szMaterial;    // имя материала, из которого этот рычаг сделан
    CString szSwitchSound; // имя звука для проигрывания во время открывания
    CString szDescription; // описание этого рычага
public:
    SLDBPrototype() { dwNumValidPlaces = 0; }
    void AddPoint(float fpX, float fpY);
    LPCSTR GetCodeName() const { return szName; }
    LPCSTR GetCode() const { return szName; }

    void Serialize(CStructureSaver &file);
};
