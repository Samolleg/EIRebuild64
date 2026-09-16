#pragma once

#include "GameBase.h"

#include <stack>
/////////////////////////////////////////////////////////////////////////////////////
class CMaterial;
typedef CMaterial *LPMATERIAL;
/////////////////////////////////////////////////////////////////////////////////////
const DWORD MAT_CHANGED = MAKE_FLAG(CGB_LAST_BIT, 0);
const DWORD MAT_USE_SPECULAR = MAKE_FLAG(CGB_LAST_BIT, 1);

const DWORD MAT_LAST_BIT = MAKE_LAST_BIT(CGB_LAST_BIT, 1);
/////////////////////////////////////////////////////////////////////////////////////
class CMaterial : public CGameBase
{
private:
    CString szName;
    D3DMATERIAL7 material; // main material data to use with Direct3D: color-light components
    std::stack<D3DCOLORVALUE> dcvStoreEmissive; // d3d color value to store emissive component
    // obsolete variables - leaved here for partial backward compatibility
    CString szTextureMapName;
    float dvTextureMapStrength;
    CString szBumpMapName;
    float dvBumpMapStrength;

public:
    CMaterial();
    CMaterial(const D3DMATERIAL7 &matNewMaterial)
        : material(matNewMaterial)
    {
    }
    virtual ~CMaterial();
    //
    virtual BOOL Create();
    virtual void Destroy();
    //
    void Setup(const D3DMATERIAL7 &matNewMaterial) { material = matNewMaterial; }
    //
    const D3DMATERIAL7 &GetMaterial() const { return material; }
    //
    void StoreEmissive();
    void RestoreEmissive();
    //
    void SetMaterialName(LPCSTR pszMaterialName) { szName = pszMaterialName; }
    const CString &GetMaterialName() const { return szName; }
    LPCSTR GetName() const { return szName; }

    void SetDiffuse(const D3DVECTOR &color);
    void SetDiffuse(const D3DCOLORVALUE &color) { material.diffuse = color; }
    D3DVECTOR GetDiffuse() const;
    const D3DCOLORVALUE &GetCompleteDiffuse() const { return material.diffuse; }
    void SetAlpha(const float &alpha);
    float GetAlpha() const;
    void SetAmbient(const D3DVECTOR &color);
    D3DVECTOR GetAmbient() const;
    const D3DCOLORVALUE &GetCompleteAmbient() const { return material.ambient; }
    void SetEmissive(const D3DVECTOR &color);
    D3DVECTOR GetEmissive() const;
    const D3DCOLORVALUE &GetCompleteEmissive() const { return material.emissive; }
    void SetSpecular(const D3DVECTOR &color);
    void SetSpecular(float r, float g, float b);
    D3DVECTOR GetSpecular() const;
    const D3DCOLORVALUE &GetCompleteSpecular() const { return material.specular; }
    void SetPower(const float &power);
    float GetPower() const;
    // obsolete functions
    void SetTextureMapName(LPCSTR pszTextureMapName) { szTextureMapName = pszTextureMapName; }
    const CString &GetTextureMapName() const { return szTextureMapName; }
    void SetTextureMapStrength(float strength) { dvTextureMapStrength = strength; }
    float GetTextureMapStrength() const { return dvTextureMapStrength; }
    void SetBumpMapName(LPCSTR pszBumpMapName) { szBumpMapName = pszBumpMapName; }
    CString GetBumpMapName() const { return szBumpMapName; }
    void SetBumpMapStrength(float strength) { dvBumpMapStrength = strength; }
    float GetBumpMapStrength() const { return dvBumpMapStrength; }
    //
    virtual LPCSTR GetGameClassName() const { return "CMaterial"; };
    virtual EClassIDs WhoAmI() const { return HPC_MATERIAL; };
};
/////////////////////////////////////////////////////////////////////////////////////
typedef CArray<CMaterial, CMaterial &> CMaterialArray;
/////////////////////////////////////////////////////////////////////////////////////
inline void CMaterial::StoreEmissive()
{
    dcvStoreEmissive.push(material.dcvEmissive);
}
inline void CMaterial::RestoreEmissive()
{
    if (!dcvStoreEmissive.empty())
    {
        material.dcvEmissive = dcvStoreEmissive.top();
        dcvStoreEmissive.pop();
    }
}

// --------------------------------------------------------------------------------------------
// set diffuse components
// --------------------------------------------------------------------------------------------
inline void CMaterial::SetDiffuse(const D3DVECTOR &color)
{
    material.diffuse.r = color.x;
    material.diffuse.g = color.y;
    material.diffuse.b = color.z;
}

// --------------------------------------------------------------------------------------------
// get diffuse components
// --------------------------------------------------------------------------------------------
inline D3DVECTOR CMaterial::GetDiffuse() const
{
    D3DVECTOR ret;
    ret.x = material.diffuse.r;
    ret.y = material.diffuse.g;
    ret.z = material.diffuse.b;
    return ret;
}
// --------------------------------------------------------------------------------------------
// set alpha value
// --------------------------------------------------------------------------------------------
inline void CMaterial::SetAlpha(const float &alpha)
{
    material.diffuse.a = alpha;
}
inline float CMaterial::GetAlpha() const
{
    return material.diffuse.a;
}

// --------------------------------------------------------------------------------------------
// set ambient components
// --------------------------------------------------------------------------------------------
inline void CMaterial::SetAmbient(const D3DVECTOR &color)
{
    material.ambient.r = color.x;
    material.ambient.g = color.y;
    material.ambient.b = color.z;
}
// --------------------------------------------------------------------------------------------
// get ambient components
// --------------------------------------------------------------------------------------------
inline D3DVECTOR CMaterial::GetAmbient() const
{
    D3DVECTOR ret;
    ret.x = material.ambient.r;
    ret.y = material.ambient.g;
    ret.z = material.ambient.b;
    return ret;
}

// --------------------------------------------------------------------------------------------
// set emissive components
// --------------------------------------------------------------------------------------------
inline void CMaterial::SetEmissive(const D3DVECTOR &color)
{
    material.emissive.r = color.x;
    material.emissive.g = color.y;
    material.emissive.b = color.z;
}
// --------------------------------------------------------------------------------------------
// get emissive components
// --------------------------------------------------------------------------------------------
inline D3DVECTOR CMaterial::GetEmissive() const
{
    D3DVECTOR ret;
    ret.x = material.emissive.r;
    ret.y = material.emissive.g;
    ret.z = material.emissive.b;
    return ret;
}

// --------------------------------------------------------------------------------------------
// set specular components
// --------------------------------------------------------------------------------------------
inline void CMaterial::SetSpecular(const D3DVECTOR &color)
{
    material.specular.r = color.x;
    material.specular.g = color.y;
    material.specular.b = color.z;
}
inline void CMaterial::SetSpecular(float r, float g, float b)
{
    material.specular.r = r;
    material.specular.g = g;
    material.specular.b = b;
}
// --------------------------------------------------------------------------------------------
// get specular components
// --------------------------------------------------------------------------------------------
inline D3DVECTOR CMaterial::GetSpecular() const
{
    D3DVECTOR ret;
    ret.x = material.specular.r;
    ret.y = material.specular.g;
    ret.z = material.specular.b;
    return ret;
}

// --------------------------------------------------------------------------------------------
inline void CMaterial::SetPower(const float &power)
{
    material.power = power;
}
inline float CMaterial::GetPower() const
{
    return material.power;
}
