#pragma once
/////////////////////////////////////////////////////////////////////////////////////
#include "SaveLoader.h"
/////////////////////////////////////////////////////////////////////////////////////
// константы, связанные с погодой и небом зоны для GlobalMap & WorldServer
enum EGMZoneWeather
{
    GMW_NONE,
    GMW_RAIN,
    GMW_SNOW,
    GMW_TORNADO
};
/////////////////////////////////////////////////////////////////////////////////////
enum EGMZoneSky
{
    GMS_NORMAL,
    GMS_CAVE
};
/////////////////////////////////////////////////////////////////////////////////////
struct SZoneExit
{
public:
    float fPassTime;                  // time for default passing from center to this exit
    float x1, y1, x2, y2;             // rectangle for deploying
    float ex1, ey1, ex2, ey2, eangle; // rectangle for exit
    CString szZone;                   // zone to exit
    int nZoneEntrance;                // corresponding exit in neighbour zone
    float fCameraRotate;              // how to place camera when we enter
    bool bPersRotate;                 // do we need to rotate personages in right direction
    float fPersRotate;                // right direction
public:
    void Load(CSave &file);
    void Store(CSave &file);
    bool IsIn(float fpX, float fpY);
    void GetExitVertices(float *pRes);
};
/////////////////////////////////////////////////////////////////////////////////////
