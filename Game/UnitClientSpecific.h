#pragma once
namespace SaveSchema { class Writer; class Reader; }
namespace owned { struct VisualAssemblyAsset; struct VisualClipVisibility; struct VisualClipSelection; }
/////////////////////////////////////////////////////////////////////////////////////
#include "Unit.h"
#include "SoundTypes.h"
#include "Texture.h"

class CParticle;
/////////////////////////////////////////////////////////////////////////////////////
struct SItemMMPData
{
public:
    ETypeIDs tidTypeID; // typeID: ITID_ITEM_WEAPON, ITID_ITEM_ARMOR, etc.
    DWORD dwSubtypeID;  // subtype
    DWORD dwType;       // type: RPGAST_PLATE, RPGAST_HELM, etc.
public:
    SItemMMPData()
        : tidTypeID(TID_EMPTY)
        , dwSubtypeID(-1)
        , dwType(-1)
    {
    }
};
/////////////////////////////////////////////////////////////////////////////////////
struct SAckDescriptor
{
    const SAckEntry *pAck;
    DWORD dwType;
    int nBackCounter;
    HSOUND hSound;
    bool bBlockUnit;
};
/////////////////////////////////////////////////////////////////////////////////////
class CUnitClientSpecific : public CUnitSpecific
{
    friend class CUnit;
    friend class CParticle; // weapon attachment nodes

private:
    CParticle *pCastingParticle;  // attached particles
    CParticle *pWeaponParticles[2] = { nullptr, nullptr }; // owned transient staff effects
    void RefreshWeaponParticles();
    CParticle *pPathParticle[2];  // attached particles for path
    float fLastTime;              // last animation time <= for SFX playing
    const SADEntry *pLastADEntry; // entry of the last checked animation
    float fpPrevSetPosTime;       // previous time of set exact position
    CAIFullRoute route;           // low level moving description
    bool bWeaponHidden;           // is weapon hidden ?
    bool bBriefingWeaponForcedHidden = false;
    bool bWeaponHiddenBeforeBriefing = false;
    bool bArrowHidden;
    bool bFirstSetpos;
    float fFadeOutAlpha; // alpha-value для проявления монстра
    int nTemporalHighlightSegmentsCounter;
    int nBloodyPrintsType;
    int nBloodyPrintsCounter;
    //
    HSOUND hLastStoredIdleSFX;
    HSOUND hLastStoredAttackSFX;
    HSOUND hLastAttackSound;   // this can be ack or attack sound
    const SAckEntry *pLastAck; // last played ack
    DWORD dwLastAckTime;       // time of the last played ack begin
    //
    std::list<SAckDescriptor> acks;
    //
    DWORD armors[RPG_NUM_ARMOR_TYPES]; // armors, currently weared (shape + material)
    DWORD weapons[QI_MAX_WEAPONS];     // weapons. the same format...
    BYTE woundTextureData[RPG_NUM_HIT_LOCATIONS];
    bool bWoundTextureDataCached;
    //
    // std::unique_ptr<DWORD[]> pMouthMimicTexture;
    ITexturePtr pMouthMimicTexture;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> pMouthMimicData;
    bool bMouthSpeakingMode;
    int nMouthSwitchTicks;
    int nCurrentMouthState;

private:
    DWORD GetCurrentWeapon() const
    {
        return lpUnit->dwCurrentWeapon < QI_MAX_WEAPONS ? weapons[lpUnit->dwCurrentWeapon] : -1;
    }
    DWORD GetWeapon(DWORD dwIndex) const
    {
        return dwIndex < QI_MAX_WEAPONS ? weapons[dwIndex] : -1;
    }
    const SUDBRaceModel *GetRace() const;
    void HideArrow(bool bHide = true);
    void ProcessHideUnhide();
    // texture composition
    BOOL AssembleTexture(BOOL bPrimaryTexture, CString &szTextureName);
    void FormItemMMPData(BOOL bPrimaryTexture, CArray<SItemMMPData, SItemMMPData &> &userdata);
    bool FormWoundsMMPData(CBYTEArray &userdata);
    //
    void DeleteCastingParticle();
    void HideWeapon(bool bHide);
    std::string GetWeaponParticleNode(bool left) const;
    // sound playing
    void StepsSound();
    BOOL GetActionSound(const SADEntry *pADEntry, float fTime1, float fTime2, CString &szSFXName,
                        BYTE cSoundSet = 0) const;
    BOOL GetStepSound(int nTerrainType, CString &szSFXName) const;
    void SetUnitPosition(float fpTime, float fpStep);
    // animations
    void PumpAnimation(bool bSet);
    DWORD GetAnimationWeaponMask();
    //
    void MouthOpen(bool bForce = false);
    void MouthClose(bool bForce = false);

public:
    void PopulateOwnedEquipment(owned::VisualAssemblyAsset&) const;
    void PopulateOwnedVisibility(owned::VisualClipVisibility&, const owned::VisualClipSelection&, double) const;
    int GetOwnedEquipmentPoint(bool left, const char* anchor, CPlot3D& point) const;
    DWORD GetWeaponSubtype(DWORD index) const { return GetWeapon(index); }
    void SetBriefingWeaponHidden(bool hidden);
    CUnitClientSpecific();
    virtual ~CUnitClientSpecific();
    // phase changed
    virtual void NewAnimationWasReceived(float fSegmentTimeCounter);
    void PoseChanged();
    // create/destroy
    virtual BOOL Create();
    virtual void Destroy();
    BOOL FinalDataLoading();
    //
    virtual int GetHeroLevel() const;
    // mouth mimic
    bool LoadMouthMimicTexture();
    void ReleaseMouthMimicTexture();
    void MouthSpeak(bool bSpeak)
    {
        bMouthSpeakingMode = bSpeak;
        if (!bSpeak)
            MouthClose(true);
    }
    // to copy server behaviour implement NeedUpdateRoute & UpdateRoute
    // virtual void ExecMoveUnit( float fpStep );
    virtual CAIFullRoute *GetRoute() { return &route; }
    // wear/remove armor
    virtual BOOL NotifyItemsUpdated();
    virtual BOOL NotifyWeaponUpdated();
    virtual bool Wear(DWORD dwArmor);
    virtual bool Remove(ERPGArmorTypes type);
    BOOL AssemblePrimaryTexture(CString &szTextureName);
    BOOL AssembleSecondaryTexture(CString &szTextureName);
    virtual void SetArmors(DWORD *pArmors, DWORD dwNumArmors);
    virtual void SetWeapons(DWORD *pWeapons, DWORD dwNumWeapons);
    virtual bool AddWeapon(DWORD dwWeapon);
    virtual bool RemoveWeapon(DWORD dwWeapon);
    //
    virtual bool HaveLowShapes() const;
    virtual bool HaveHighShapes() const;
    bool IsCurrentWeaponABow() const
    {
        DWORD dwWeapon = GetCurrentWeapon();
        if (dwWeapon == -1)
            return false;
        return (CItemWeapon::GetWeaponType(dwWeapon) == RPGWST_BOW) ||
               (CItemWeapon::GetWeaponType(dwWeapon) == RPGWST_CROSSBOW);
    }
    // wounds
    virtual BOOL NotifyHitPointsUpdated();
    BOOL AssembleWoundsTexture(const CBYTEArray *requiredData = nullptr);
    BOOL EnsureWoundsTextureReady();
    // hook on update inventory
    virtual void InventoryUpdated();
    // hook on set owner
    virtual void SetOwner(CPlayer *lpNewOwner);
    //
    virtual BOOL ChangeWeapon(DWORD dwToWeapon);
    bool GetCastPointPosition(CPlot3D &position); // retrieve cast point position in model space
    void SetPathParticles(CParticle *pP1, CParticle *pP2);
    //
    void TemporalHighlight(int nNumSegments);
    // segment processing
    virtual int Segment();
    virtual void SetExactPosition(float fpTime);
    virtual void NewRouteReceived();
    virtual void StartFadeOut();
    //
    virtual void HLDamaged(ERPGHitLocations hl, float fpDamage, BYTE cFlags);
    virtual void ElectricDamage(float fpStrength);
    virtual void Dead();
    virtual void GetAttackParams(SAttackParams &ap) const;
    //
    float GetFadeOutAlpha() const { return fFadeOutAlpha; }
    // acknowledgements
    bool BeginPlayAck(SAckDescriptor &ack);
    virtual void AddAck(const SAckEntry *pAck, DWORD dwType);
    virtual void ClearAcks();
    //
    // class identification functions
    virtual LPCSTR GetGameClassName() const { return "CUnitClientSpecific"; };
    virtual EClassIDs WhoAmI() const { return HPC_UNITCLIENTSPECIFIC; };
public:
    virtual void Store(SaveSchema::Writer &) const;
    virtual void Load(SaveSchema::Reader &);
    virtual void DiscardSave();
};
/////////////////////////////////////////////////////////////////////////////////////
void FormBodypartNames(const SUDBUnitPrototype *pPrototype, const std::vector<DWORD> &armors,
                       const std::vector<DWORD> &weapons, std::vector<std::string> &szNames,
                       std::vector<std::string> &szBPNames);
/////////////////////////////////////////////////////////////////////////////////////
