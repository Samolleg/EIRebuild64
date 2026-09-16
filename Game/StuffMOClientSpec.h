#pragma once
/////////////////////////////////////////////////////////////////////////////////////
#include "SmartPtr.h"
#include "StuffMapObjects.h"
#include "SoundTypes.h"
/////////////////////////////////////////////////////////////////////////////////////
class CLight;
class CParticle;
/////////////////////////////////////////////////////////////////////////////////////
class CTorchObjectClient : public CTorchObjectSpecific
{
private:
    CSmartPtr<CLight> pLight;
    CSmartPtr<CParticle> pParticles;
    HSOUND sfx;

public:
    CTorchObjectClient();
    virtual ~CTorchObjectClient();
    //
    virtual BOOL Create();
    virtual void Destroy();
    //
    virtual void SetParticles(ETypeIDs tidParticlesTypeID, const CPlot3D &position,
                              const float fpScale);
    virtual void SetLight(const CPlot3D &position, const float range, const D3DCOLORVALUE &color);
    virtual void SetSFX(LPCSTR pszSFXName);
    //
    virtual void TurnOn();
    virtual void TurnOff();
    virtual void StartupEffect();
    //
    virtual int Segment() { return 1; }
    // class identification functions
    virtual LPCSTR GetGameClassName() const { return "CTorchObjectClientSpecific"; };
    virtual EClassIDs WhoAmI() const { return HPC_TORCHCLIENTSPECIFIC; };
};
/////////////////////////////////////////////////////////////////////////////////////
