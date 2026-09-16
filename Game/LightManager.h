#pragma once

#include "Light.h"
#include "BaseManager.h"
#include "SmartPtr.h"

#include <list>
// --------------------------------------------------------------------------------------------
//
// --------------------------------------------------------------------------------------------
class CLightManager : public CBaseManager
{
private:
    D3DCOLORVALUE dcvAmbientLight;
    CSmartPtr<CLight> lpSunLight;
    //
    CHashMapIterator<int, CGameBase *> hmi; // hash map iterator
    //
    BOOL bLightsActive;
    // quest lights list
    std::list<int> nQuestLights;

protected:
    // data create/release
    virtual LPGAMEBASE CreateInstant(ETypeIDs tidTypeID);

public:
    CLightManager();
    virtual ~CLightManager();
    //
    BOOL SetAmbientLight(const D3DCOLORVALUE &dcvAmbientLight);
    D3DCOLORVALUE GetAmbientLight() const { return dcvAmbientLight; }
    BOOL SetSunLight(LPLIGHT lpNewSunLight);
    LPLIGHT GetSunLight() { return lpSunLight; }
    BOOL SetLight(LPLIGHT lpNewSunLight, const D3DCOLORVALUE &dcvAmbientLight);
    //
    LPLIGHT CreateLight(ETypeIDs tid);
    //
    virtual LPGAMEBASE CreateData(ETypeIDs tidTypeID, int id = -1);
    //
    void RemoveQuestLightID(int nQLID) { nQuestLights.remove(nQLID); }
    void AddQuestLightID(int nQLID) { nQuestLights.push_back(nQLID); }
    const std::list<int> &GetQuestLightIDs() const { return nQuestLights; }
    // class identification information
    virtual LPCSTR GetGameClassName() const { return "CLightManager"; };
    virtual EClassIDs WhoAmI() const { return HPC_LIGHTMANAGER; }
};
// --------------------------------------------------------------------------------------------
inline LPLIGHT CLightManager::CreateLight(ETypeIDs tid)
{
    return static_cast<CLight *>(CreateData(tid));
}
