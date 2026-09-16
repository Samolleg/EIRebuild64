#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "StuffMOClientSpec.h"
#include "ParticleManager.h"
#include "Particle.h"
#include "LightManager.h"
#include "Light.h"
#include "WorldClient.h"
#include "Terrain.h"
#include "SoundManager.h"
/////////////////////////////////////////////////////////////////////////////////////
CTorchObjectClient::CTorchObjectClient()
{
    pLight = 0;
    pParticles = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
CTorchObjectClient::~CTorchObjectClient()
{
    Destroy();
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CTorchObjectClient::Create()
{
    CTorchObjectSpecific::Create();

    pLight = 0;
    pParticles = 0;
    sfx = 0;

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CTorchObjectClient::Destroy()
{
    TurnOff();
    pLight = 0;
    if (pParticles) // finish effect, because in the 'TurnOff()' effect only holds!
        pParticles->FinishEffect();
    pParticles = 0;

    CTorchObjectSpecific::Destroy();
}
/////////////////////////////////////////////////////////////////////////////////////
// void CTorchObjectClient::SetParticles( ETypeIDs tidParticlesTypeID, const CPlot3D& position,
// const float fpScale ) create new particle source and setup parameters
void CTorchObjectClient::SetParticles(ETypeIDs tidParticlesTypeID, const CPlot3D &position,
                                      const float fpScale)
{
    CTorchObjectSpecific::SetParticles(tidParticlesTypeID, position, fpScale);
    CPlot3D position1 = position + lpTorch->GetCP();

    if (!pParticles.IsEmpty())
    {
        pParticles->FinishEffect();
        pParticles = 0; // <= this is smart pointer - it will call ReleaseInstant by himself
    }

    pParticles = ((CParticleManager *)theWorld.GetParticleManager())
                     ->CreateParticleSystem(tidParticlesTypeID);
    pParticles->SetCP(position1);
    pParticles->Scale(fpScale);
    pParticles->AttachCarrier(NULL);
}
/////////////////////////////////////////////////////////////////////////////////////
// прокрутим несколько сегментов для нормального состояния партикл эффекта
void CTorchObjectClient::StartupEffect()
{
    if (pParticles.IsEmpty())
        return;
    for (int i = 0; i < 60; i++)
        pParticles->Segment();
}
/////////////////////////////////////////////////////////////////////////////////////
// void CTorchObjectClient::SetLight( const CPlot3D& position, const float range, const
// D3DCOLORVALUE& color ) create new light and setup paramenters
void CTorchObjectClient::SetLight(const CPlot3D &position, const float range,
                                  const D3DCOLORVALUE &color)
{
    CTorchObjectSpecific::SetLight(position, range, color);
    CPlot3D position1 = position + lpTorch->GetCP();

    if (!pLight.IsEmpty())
    {
        theWorld.GetTerrain()->RemoveLight(pLight);
        pLight = 0; // <= this is smart pointer - it will call ReleaseInstant by himself
    }

    pLight = (CLight *)theWorld.GetLightManager()->CreateData(LTID_POINTLIGHT);

    D3DVECTOR dvColor = D3DVECTOR(color.r, color.g, color.b);
    D3DVECTOR dvPosition = D3DVECTOR(position1.x, position1.y, position1.z);
    pLight->SetPointLight(dvColor, dvPosition);
    pLight->SetRange(range);

    theWorld.GetTerrain()->AddLight(pLight);
}
/////////////////////////////////////////////////////////////////////////////////////
void CTorchObjectClient::SetSFX(LPCSTR pszSFXName)
{
    CTorchObjectSpecific::SetSFX(pszSFXName);
    if (sfx) // cancel old sfx
        theWorld.GetSoundManager()->CancelFX(sfx);
    sfx = theWorld.GetSoundManager()->BeginFX(szSFXName, 0, lpTorch->GetCP(), 1, fpLightRange * 5,
                                              TRUE);
}
/////////////////////////////////////////////////////////////////////////////////////
// void CTorchObjectClient::TurnOn()
// turn torch on: run particle effect, turn light on and add it to terrain, begin FX
void CTorchObjectClient::TurnOn()
{
    if (!pParticles.IsEmpty())
        pParticles->RunEffect();
    //
    if (!pLight.IsEmpty() && pLight->CheckFlag(LIT_INACTIVE))
    {
        pLight->ResetFlag(LIT_INACTIVE);
        theWorld.GetTerrain()->AddLight(pLight);
    }
    //
    if (sfx) // cancel any old SFX
        theWorld.GetSoundManager()->CancelFX(sfx);
    sfx = theWorld.GetSoundManager()->BeginFX(szSFXName, 0, lpTorch->GetCP(), 1, fpLightRange * 3,
                                              TRUE);
}
/////////////////////////////////////////////////////////////////////////////////////
void CTorchObjectClient::TurnOff()
{
    if (!pParticles.IsEmpty())
        pParticles->HoldEffect();
    //
    if (!pLight.IsEmpty() && !pLight->CheckFlag(LIT_INACTIVE))
    {
        pLight->SetFlag(LIT_INACTIVE);
        theWorld.GetTerrain()->RemoveLight(pLight);
    }
    //
    if (sfx)
    {
        theWorld.GetSoundManager()->CancelFX(sfx);
        sfx = 0;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
