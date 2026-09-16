#pragma once

#include "Base3D.h"
#include "Plot.h"

class CMaterial;
typedef CMaterial *LPMATERIAL;
class CLight;
typedef CLight *LPLIGHT;
// --------------------------------------------------------------------------------------------
const DWORD LIT_CHANGED = MAKE_FLAG(B3D_LAST_BIT, 0);
const DWORD LIT_IN_VIEW = MAKE_FLAG(B3D_LAST_BIT, 1);
const DWORD LIT_CREATED = MAKE_FLAG(B3D_LAST_BIT, 2);
const DWORD LIT_SPECULAR = MAKE_FLAG(B3D_LAST_BIT, 3);
const DWORD LIT_INACTIVE = MAKE_FLAG(B3D_LAST_BIT, 4);
// this is dynamic light, which will lit even STATIC objects each frame
const DWORD LIT_DYNAMIC = MAKE_FLAG(B3D_LAST_BIT, 5);
// this light can't be turned off during day/night changes
const DWORD LIT_QUEST = MAKE_FLAG(B3D_LAST_BIT, 6);
// this light will not cast shadow
const DWORD LIT_UNSHADOW = MAKE_FLAG(B3D_LAST_BIT, 7);
// этот источник является главным при отбрасывании тени
const DWORD LIT_MAGICSHADOW = MAKE_FLAG(B3D_LAST_BIT, 8);

const DWORD LIT_LAST_BIT = MAKE_LAST_BIT(B3D_LAST_BIT, 8);

// const DWORD LIT_CHANGED = 0x00000001;
// const DWORD LIT_IN_VIEW = 0x00000002;
// const DWORD LIT_CREATED = 0x00000004;

// const DWORD LIT_SPECULAR = 0x00000008;
// const DWORD LIT_INACTIVE = 0x00000010;
// --------------------------------------------------------------------------------------------

// --------------------------------------------------------------------------------------------
//
// --------------------------------------------------------------------------------------------
class CLight : public CBase3D
{
protected:
    D3DLIGHT7 light;                  // structure defining the light
    float fpAddHeight;                // additional height
    float range2;                     // range*range
    float rcpr_range2;                // 1.0f/range2
    CPlot3D rd;                       // respect direction
    D3DCOLORVALUE dcMaterialDiffuse;  // light color * material diffuse reflection
    D3DCOLORVALUE dcMaterialSpecular; // light color * material specular reflection
protected:
    void TurnChangedOn() { SetFlag(LIT_CHANGED); }
    void TurnChangedOff() { ResetFlag(LIT_CHANGED); }
    void TurnCreatedOn() { SetFlag(LIT_CREATED); }
    void TurnCreatedOff() { ResetFlag(LIT_CREATED); }
    void TurnInViewportOn() { SetFlag(LIT_IN_VIEW); }
    void TurnInViewportOff() { ResetFlag(LIT_IN_VIEW); }

public:
    CLight();
    virtual ~CLight();
    // create/destroy
    virtual BOOL Create();
    virtual void Destroy();
    //
    void SetAddHeight(const float height) { fpAddHeight = height; }
    float GetAddHeight() const { return fpAddHeight; }
    // setting by-type parameters
    BOOL SetPointLight(const D3DVECTOR &color, const D3DVECTOR &position);
    BOOL SetDirectionalLight(const D3DVECTOR &color, const D3DVECTOR &direction);
    BOOL SetParallelPointLight(const D3DVECTOR &color, const D3DVECTOR &position);
    BOOL SetSpotLight(const D3DVECTOR &color, const D3DVECTOR &position, const D3DVECTOR &direction,
                      const float umbra, const float penumbra);
    // some light functions
    D3DCOLORVALUE LightVertex(float factor, const D3DCOLORVALUE &dcvEmissive,
                              const CPlot3D &normale);
    D3DCOLORVALUE LightVertex(float factor, const D3DCOLORVALUE &dcvEmissive,
                              const CPlot3D &normale, const CPlot3D &dr, float ldr);
    D3DCOLORVALUE LightVertexForTerrain(float factor, const D3DCOLORVALUE &dcvEmissive,
                                        const CPlot3D &normale, const CPlot3D &dr, float ldr);
    float GetLuminanceInPoint(const CPlot3D &plot);
    // checks
    BOOL IsChanged() const { return CheckFlag(LIT_CHANGED) != 0; }
    BOOL IsCreated() const { return CheckFlag(LIT_CREATED) != 0; }
    BOOL IsInViewport() const { return CheckFlag(LIT_IN_VIEW) != 0; }
    // setup with material
    void SetupWithMaterial(LPMATERIAL lpMaterial, float fCoeff = 1.0f);
    // set functions
    // colors
    void SetDiffuse(const float r, const float g, const float b)
    {
        light.dcvDiffuse.r = r;
        light.dcvDiffuse.g = g;
        light.dcvDiffuse.b = b;
    }
    void SetDiffuse(const D3DCOLORVALUE &color) { SetDiffuse(color.r, color.g, color.b); }
    void SetDiffuse(const D3DVECTOR &color) { SetDiffuse(color.x, color.y, color.z); }
    void SetSpecular(const float r, const float g, const float b)
    {
        light.dcvSpecular.r = r;
        light.dcvSpecular.g = g;
        light.dcvSpecular.b = b;
    }
    void SetSpecular(const D3DCOLORVALUE &color) { SetSpecular(color.r, color.g, color.b); }
    void SetSpecular(const D3DVECTOR &color) { SetSpecular(color.x, color.y, color.z); }
    void SetAmbient(const float r, const float g, const float b)
    {
        light.dcvAmbient.r = r;
        light.dcvAmbient.g = g;
        light.dcvAmbient.b = b;
    }
    void SetAmbient(const D3DCOLORVALUE &color) { SetAmbient(color.r, color.g, color.b); }
    void SetAmbient(const D3DVECTOR &color) { SetAmbient(color.x, color.y, color.z); }
    // position/direction/range
    void SetPosition(const float x, const float y, const float z)
    {
        light.dvPosition.x = x;
        light.dvPosition.y = y;
        light.dvPosition.z = z;
    }
    template <class POS>
    void SetPosition(const POS &pos)
    {
        SetPosition(pos.x, pos.y, pos.z);
    }
    void SetDirection(const float x, const float y, const float z)
    {
        light.dvDirection.x = x;
        light.dvDirection.y = y;
        light.dvDirection.z = z;
    }
    template <class DIR>
    void SetDirection(const DIR &dir)
    {
        SetDirection(dir.x, dir.y, dir.z);
    }
    void SetRespectDirection(const CPlot3D &rd) { this->rd = rd; }
    void SetRange(const float range)
    {
        light.dvRange = range;
        range2 = range * range;
        rcpr_range2 = 1.0f / range2;
    }

    // attenuation/falloff/angles
    void SetAttenuation0(const float val) { light.dvAttenuation0 = val; }
    void SetAttenuation1(const float val) { light.dvAttenuation1 = val; }
    void SetAttenuation2(const float val) { light.dvAttenuation2 = val; }
    void SetAttenuations(const float val0, const float val1, const float val2)
    {
        SetAttenuation0(val0);
        SetAttenuation1(val1);
        SetAttenuation2(val2);
    }
    void SetFalloff(const float val) { light.dvFalloff = val; }
    void SetPhi(const float val) { light.dvPhi = val; }
    void SetTheta(const float val) { light.dvTheta = val; }
    void SetSpotlightAngles(const float phi, const float theta)
    {
        SetPhi(phi);
        SetTheta(theta);
    }
    // get functions
    // colors
    const D3DCOLORVALUE &GetDiffuse() const { return light.dcvDiffuse; }
    const D3DCOLORVALUE &GetSpecular() const { return light.dcvSpecular; }
    const D3DCOLORVALUE &GetAmbient() const { return light.dcvAmbient; }
    // position/direction/range
    const D3DVECTOR &GetPosition() const { return light.dvPosition; }
    const D3DVECTOR &GetDirection() const { return light.dvDirection; }
    const CPlot3D &GetRespectDirection() const { return rd; }
    //	const CPlot3D& GetPosition() const { return reinterpret_cast<CPlot3D&>(light.dvPosition); }
    //	const CPlot3D& GetDirection() const { return reinterpret_cast<CPlot3D&>(light.dvDirection);
    //}
    float GetRange() const { return light.dvRange; }
    float GetRange2() const { return range2; }
    float GetRcprRange2() const { return rcpr_range2; }
    // attenuation/falloff/angles
    float GetAttenuation0() const { return light.dvAttenuation0; }
    float GetAttenuation1() const { return light.dvAttenuation1; }
    float GetAttenuation2() const { return light.dvAttenuation2; }
    float GetFalloff() const { return light.dvFalloff; }
    float GetPhi() const { return light.dvPhi; }
    float GetTheta() const { return light.dvTheta; }
    //
    const D3DCOLORVALUE &GetMaterialDiffuse() const { return dcMaterialDiffuse; }
    const D3DCOLORVALUE &GetMaterialSpecular() const { return dcMaterialSpecular; }
    // access to D3D light
    const D3DLIGHT7 &GetLight() const { return light; }
    // class identification functions
    virtual LPCSTR GetGameClassName() const { return "CLight"; }
    virtual EClassIDs WhoAmI() const { return HPC_LIGHT; }
};
typedef CSmartPtr<CLight> CLightPtr;

// --------------------------------------------------------------------------------------------
typedef CSimplyArray<CLight *, CLight *> CLightPtrArray;
