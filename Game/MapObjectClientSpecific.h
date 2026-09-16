#pragma once

#include "SoundTypes.h"
#include "MapObject.h"
#include "Particle.h"
#include "Light.h"
/////////////////////////////////////////////////////////////////////////////////////
struct SMOSpellEffectShow
{
    int nSpellID;
    int nVisStrength;
    CSmartPtr<CParticle> pParticle;
    bool bCorrect;
    SMOSpellEffectShow()
    {
        nSpellID = 0;
        nVisStrength = 0x10;
        bCorrect = false;
    }
};
/////////////////////////////////////////////////////////////////////////////////////
struct SMOSpellEffectSound
{
    int nSpellID;
    HSOUND hSnd;
    HSNDLIST hSndList;
    int nSoundTimeLeft;
    bool bCorrect;
    SMOSpellEffectSound()
    {
        nSpellID = 0;
        bCorrect = false;
        hSnd = SND_EMPTY;
        hSndList = SND_EMPTY;
        nSoundTimeLeft = 0;
    }
};
/////////////////////////////////////////////////////////////////////////////////////
class CMapObjectClientSpecific : public CMapObjectSpecific
{
    friend class CMapObject;

private:
    float fpTime;
    int nStepTimeCount;
    CArray<SMOSpellEffectShow, SMOSpellEffectShow &> effectsShow;
    CArray<SMOSpellEffectSound, SMOSpellEffectSound &> effectsSound;
    CLightPtr pLight; // attached light
    HSOUND hLastSpellEffectTick;

    void UpdateEffectParticles(bool bStart);
    void UpdateEffectSounds(bool bStart);

public:
    CMapObjectClientSpecific();
    virtual ~CMapObjectClientSpecific();
    // create/destroy
    virtual BOOL Create();
    virtual void Destroy();
    //
    virtual void UpdateSpellEffectVisualization(bool bStart);
    //
    // light functions
    void SetLight(const D3DCOLORVALUE &dcvColor, const float fpRange, bool bSpecular = false);
    void SetLight(CLight *pNewLight) { pLight = pNewLight; }
    CLight *GetLight() { return pLight; }
    void LightOn();
    void LightOff();
    //
    virtual int Segment();
    //
    virtual void ReceiveDamage(int nAmount, DWORD dwFlags);
    //
    virtual BOOL IsDead() const
    {
        return (((CMapObject *)lpMapObject)->rpgUserCommonStats.nHP <= 0);
    };
    virtual BOOL IsCorpse() const
    {
        return (((CMapObject *)lpMapObject)->rpgUserCommonStats.nHP <
                -5 * ((CMapObject *)lpMapObject)->rpgUserCommonStats.nMaxHP);
    }
    //
    virtual void SetQuestState(EQuestState qs);
    //
    // class identification functions
    virtual LPCSTR GetGameClassName() const { return "CMapObjectClientSpecific"; };
    virtual EClassIDs WhoAmI() const { return HPC_MAPOBJECTCLIENTSPECIFIC; };
};
