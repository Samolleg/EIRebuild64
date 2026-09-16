#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "BasicChunk.h"
#include "ldbFormat.h"
/////////////////////////////////////////////////////////////////////////////////////
void SLDBPrototype::AddPoint(float fpX, float fpY)
{
    ptUsePlaces[dwNumValidPlaces] = CPlot2D(fpX, fpY);
    dwNumValidPlaces++;
}
/////////////////////////////////////////////////////////////////////////////////////
void SLDBPrototype::Serialize(CStructureSaver &file)
{
    file.Add(0, szName);
    file.AddRaw(1, ptUsePlaces, sizeof(ptUsePlaces));
    file.Add(2, dwNumValidPlaces);
    file.Add(3, fpScaleMn);
    file.Add(4, dwSwitchTime);
    file.Add(5, szMaterial);
    file.Add(6, szSwitchSound);
    file.Add(7, szDescription);
}
/////////////////////////////////////////////////////////////////////////////////////
