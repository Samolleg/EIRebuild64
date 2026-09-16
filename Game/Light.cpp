#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "Light.h"
#include "Material.h"
/////////////////////////////////////////////////////////////////////////////////////
CLight::CLight()
{
    ZeroMemory(&light, sizeof(light));
    fpAddHeight = 0.0f;
    // default to no attenuation with distance
    SetAttenuations(1, 0, 0);
    // default to no max distance
    SetRange(D3DLIGHT_RANGE_MAX);
    // default to linear ramp from inner to outer cone
    SetFalloff(1);

#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
}
/////////////////////////////////////////////////////////////////////////////////////
CLight::~CLight()
{
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CLight::Create()
// initiali creation
BOOL CLight::Create()
{
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// void CLight::Destroy()
// final destroy
void CLight::Destroy()
{
    TurnCreatedOff();
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CLight::SetPointLight( const D3DVECTOR & color, const D3DVECTOR & position )
// set params for the light as 'point light'
BOOL CLight::SetPointLight(const D3DVECTOR &color, const D3DVECTOR &position)
{
    light.dltType = D3DLIGHT_POINT;
    SetDiffuse(color);
    SetPosition(position);
    SetCP(CPlot3D(position.x, position.y, position.z));

    TurnChangedOn();

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CLight::SetDirectionalLight( const D3DVECTOR& color, const D3DVECTOR& direction )
// set params for the light as 'directional light'
BOOL CLight::SetDirectionalLight(const D3DVECTOR &color, const D3DVECTOR &direction)
{
    // Normalize direction
    CPlot3D ptDir(direction.x, direction.y, direction.z);
    ptDir.Normalize();
    //
    light.dltType = D3DLIGHT_DIRECTIONAL;
    SetDiffuse(color);
    SetDirection(D3DVECTOR(ptDir.x, ptDir.y, ptDir.z));
    SetOrientation(ptDir);

    TurnChangedOn();

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CLight::SetParallelPointLight( const D3DVECTOR& color, const D3DVECTOR& position )
// set params for the light as 'parallel point light'
BOOL CLight::SetParallelPointLight(const D3DVECTOR &color, const D3DVECTOR &position)
{
    light.dltType = D3DLIGHT_PARALLELPOINT;
    SetDiffuse(color);
    SetPosition(position);
    SetCP(CPlot3D(position.x, position.y, position.z));

    TurnChangedOn();

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CLight::SetSpotLight( const D3DVECTOR& color, const D3DVECTOR& position, const D3DVECTOR&
// direction,
// const float umbra, const float penumbra) set params for the light as 'spot light'
BOOL CLight::SetSpotLight(const D3DVECTOR &color, const D3DVECTOR &position,
                          const D3DVECTOR &direction, const float umbra, const float penumbra)
{
    light.dltType = D3DLIGHT_SPOT;
    SetDiffuse(color);
    SetPosition(position);
    SetCP(CPlot3D(position.x, position.y, position.z));
    SetDirection(direction);
    SetOrientation(CPlot3D(direction.x, direction.y, direction.z));

    TurnChangedOn();

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// setup dcMaterialDiffuse and dcMaterialSpecular components with material
void CLight::SetupWithMaterial(LPMATERIAL lpMaterial, float fCoeff)
{
    dcMaterialDiffuse.r = lpMaterial->GetCompleteDiffuse().r * GetDiffuse().r * fCoeff;
    dcMaterialDiffuse.g = lpMaterial->GetCompleteDiffuse().g * GetDiffuse().g * fCoeff;
    dcMaterialDiffuse.b = lpMaterial->GetCompleteDiffuse().b * GetDiffuse().b * fCoeff;
    dcMaterialDiffuse.a = lpMaterial->GetAlpha();

    dcMaterialSpecular.r = lpMaterial->GetCompleteSpecular().r * GetDiffuse().r * fCoeff;
    dcMaterialSpecular.g = lpMaterial->GetCompleteSpecular().g * GetDiffuse().g * fCoeff;
    dcMaterialSpecular.b = lpMaterial->GetCompleteSpecular().b * GetDiffuse().b * fCoeff;
    dcMaterialSpecular.a = 1.0f;
}
/////////////////////////////////////////////////////////////////////////////////////
// lit vertex for directional light
D3DCOLORVALUE CLight::LightVertex(float factor, const D3DCOLORVALUE &dcvEmissive,
                                  const CPlot3D &normale)
{
    float cosAlpha = -(light.dvDirection.x * normale.x + light.dvDirection.y * normale.y +
                       light.dvDirection.z * normale.z);
    D3DCOLORVALUE color;

    if (cosAlpha <= 0)
        return dcvEmissive;

    cosAlpha *= factor;
    color.r = Min(light.dcvDiffuse.r * cosAlpha + dcvEmissive.r, 1.0f);
    color.g = Min(light.dcvDiffuse.g * cosAlpha + dcvEmissive.g, 1.0f);
    color.b = Min(light.dcvDiffuse.b * cosAlpha + dcvEmissive.b, 1.0f);

    return color;
}
/////////////////////////////////////////////////////////////////////////////////////
// D3DCOLORVALUE CLight::LightVertex( const D3DCOLORVALUE& dcvEmissive, const CPlot3D& normale,
// const CPlot3D& dr ) lit vertex for point light
D3DCOLORVALUE CLight::LightVertex(float factor, const D3DCOLORVALUE &dcvEmissive,
                                  const CPlot3D &normale, const CPlot3D &dr, float ldr)
{
    D3DCOLORVALUE color;
    float kdist = 1.0f - ldr * ldr * rcpr_range2;

    if (kdist < 0)
        return dcvEmissive;
    else
    {
        kdist *= (normale * dr) * factor;
        color.r = Min(light.dcvDiffuse.r * kdist + dcvEmissive.r, 1.0f);
        color.g = Min(light.dcvDiffuse.g * kdist + dcvEmissive.g, 1.0f);
        color.b = Min(light.dcvDiffuse.b * kdist + dcvEmissive.b, 1.0f);
    }

    return color;
}

/////////////////////////////////////////////////////////////////////////////////////
// D3DCOLORVALUE CLight::LightVertex( const D3DCOLORVALUE& dcvEmissive, const CPlot3D& normale,
// const CPlot3D& dr ) lit vertex for point light, uses heuristic light calculation function
D3DCOLORVALUE CLight::LightVertexForTerrain(float factor, const D3DCOLORVALUE &dcvEmissive,
                                            const CPlot3D &normale, const CPlot3D &dr, float ldr)
{
    D3DCOLORVALUE color;
    float kdist = 1.0f - ldr * ldr * rcpr_range2;

    if (kdist < 0)
        return dcvEmissive;
    else
    {
        float fpScalPro = normale * dr;
        kdist *= select_le(fpScalPro, 0, 1 + fpScalPro, 1) * factor;
        color.r = Min(light.dcvDiffuse.r * kdist + dcvEmissive.r, 1.0f);
        color.g = Min(light.dcvDiffuse.g * kdist + dcvEmissive.g, 1.0f);
        color.b = Min(light.dcvDiffuse.b * kdist + dcvEmissive.b, 1.0f);
    }

    return color;
}
/////////////////////////////////////////////////////////////////////////////////////
float CLight::GetLuminanceInPoint(const CPlot3D &plot)
{
    if (GetTypeID() == LTID_POINTLIGHT)
    {
        float ldr2 = fabs2(plot, GetCP());
        if (ldr2 < range2)
        {
            float fLuminance = 0.6f * light.dcvDiffuse.g + 0.3f * light.dcvDiffuse.r +
                               0.1f * light.dcvDiffuse.b;
            return fLuminance * (1.0f - ldr2 * rcpr_range2);
        }
        else
            return 0;
    }
    else
        return 0.6f * light.dcvDiffuse.g + 0.3f * light.dcvDiffuse.r + 0.1f * light.dcvDiffuse.b;
}
/////////////////////////////////////////////////////////////////////////////////////
