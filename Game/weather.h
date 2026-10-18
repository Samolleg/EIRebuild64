#pragma once
/**********************************************************************
 *<
        FILE: Weather.H

        DESCRIPTION:  Weather effects

        CREATED BY: Andy Gulin

 *>	Copyright (c) 1999, Nival Enterntainemnt
 **********************************************************************/

#include "gamebase.h"
#include "ServerConsts.h"
#include "SoundTypes.h"
#include "typedefs.h"
#include "Renderer.h" //only for descriptor

class CWorldClient;

const int N_WEATHER_POWER_GRADATIONS = 64;
// time of weather changes, depends on sound length
const int N_WEATHER_CLEAR_TO_RAIN = 20;
const int N_WEATHER_CLEAR_TO_SNOW = 40;
const int N_WEATHER_RAIN_TO_CLEAR = 20;
const int N_WEATHER_SNOW_TO_CLEAR = 40;

class CWeather : public CGameBase
{
    CTextureDescriptor pTexture; // texture of weather effect particles
    CWorldClient *pWorld;

    HSOUND hSnd;
    CXYZWArray transCoords;
    CXYZW4Array srcVertexes;
    CByteArray clips, kinds;
    CTLVertexArray tlVertexes;
    CWordArray tFaces;
    CIntArray order;
    int nCurrentVertice;
    CPlot3D ptWind;
    EWeather curWeather, nextWeather;
    int nPowerCheck;
    float fpWeatherStartTime, fpWeatherTransformTime;
    // set sizes of each array and fill required information
    void InitArrays();
    void AddPoint(float fpX, float fpY, float fpZ);
    BOOL DrawRain(CRenderer *pTarget);
    BOOL DrawSnow(CRenderer *pTarget);
    void SetupPower(float fpTime);
    void SwitchWeather();

public:
    void Clear();
    CWeather()
    {
        pWorld = NULL;
        hSnd = NULL;
        Clear();
    }
    ~CWeather() { Clear(); }
    virtual BOOL Create()
    {
        ASSERT(0 && "Can`t create without world");
        return FALSE;
    }
    BOOL Create(CWorldClient *pWrld)
    {
        // Rain/snow geometry belongs to the renderer; no host particle buffers.
        pWorld = pWrld;
        return TRUE;
    }
    virtual void Destroy() { Clear(); }

    void SetWeather(EWeather weather, float fpTransformTime);
    void SetWind(const CPlot3D &newWindVector, float fpNewWindForce);
    BOOL Draw(CRenderer *pTarget);

    float GetRenderWeatherStart() const { return fpWeatherStartTime; }
    EWeather GetCurrentWeather() const { return curWeather; }
    EWeather GetNextWeather() const { return nextWeather; }
    // Read transition before the particle draw updates power; inactive is >1,
    // matching the renderer's existing rain-density reader. No weather mutation.
    float GetRenderTransitionProgress(float tick) const
    {
        return fpWeatherTransformTime != 0 ? (tick - fpWeatherStartTime) / fpWeatherTransformTime : 2.f;
    }

    virtual LPCSTR GetGameClassName() const { return "CWeather"; };
    virtual EClassIDs WhoAmI() const { return HPC_WEATHER; }
};
