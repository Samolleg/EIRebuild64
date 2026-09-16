#pragma once

#include "gamebase.h"
#include "BaseManager.h"

#include "Plot.h"
#include "TextureDescriptor.h"
#include <unordered_map>
/////////////////////////////////////////////////////////////////////////////////////
struct SVisibilityMask;
/////////////////////////////////////////////////////////////////////////////////////
struct SFogParticle
{
    short nX, nY;
    CPlot3D pos;
    float fpPower;
    char nState;
    char nPhase;
};
/////////////////////////////////////////////////////////////////////////////////////
class CVisibilityFog : public CGameBase
{
protected:
    typedef std::unordered_map<int, SFogParticle> CParticleHash;
    struct FEraser
    {
        CParticleHash &hash;
        FEraser(CParticleHash &h)
            : hash(h)
        {
        }
        void operator()(int i) { hash.erase(i); }
    };

protected:
    SBoundingVolume bv;
    CParticleHash particles;
    bool bBVIsCorrect;

    void BuildBoundingVolume();

public:
    void Clear();
    CVisibilityFog() { Clear(); }
    ~CVisibilityFog() { Clear(); }
    virtual BOOL Create()
    {
        Clear();
        return TRUE;
    }
    virtual void Destroy() { Clear(); }
    BOOL Draw(CRenderer *pTarget);
    virtual int Segment();
    void AddNewVisibilityData(SVisibilityMask &mask);

    virtual LPCSTR GetGameClassName() const { return "CVisibilityFog"; };
    virtual EClassIDs WhoAmI() const { return HPC_VISIBILITYFOG; }
};
/////////////////////////////////////////////////////////////////////////////////////
class CVisFogManager : public CBaseManager
{
    CTextureDescriptor pTexture;
    CList<CVisibilityFog *, CVisibilityFog *> fogs;

public:
    CVisFogManager() {}
    virtual void Destroy();
    virtual ~CVisFogManager() { Destroy(); }
    // Draw
    BOOL Draw(CRenderer *pTarget);
    virtual BOOL Create(CWorld *lpWorld) { return CBaseManager::Create(lpWorld); }
    virtual int Segment();
    //
    CVisibilityFog *CreateFog();
    // class identification information
    virtual LPCSTR GetGameClassName() const { return "CVisFogManager"; };
    virtual EClassIDs WhoAmI() const { return HPC_VISFOGMANAGER; }

protected:
    virtual BOOL Create();
    LPGAMEBASE CreateInstant(ETypeIDs tidTypeID);
};
/////////////////////////////////////////////////////////////////////////////////////
