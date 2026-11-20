#pragma once

#include "I3DScreen.h"
#include "Item.h"
#include "LRUStorage.h"

#include "Unit.h"
#include "Party.h"
#include <vector>
/////////////////////////////////////////////////////////////////////////////////////
enum EItemScreenParts
{
    ISP_CAMPSLOTS,
    ISP_WAGON,
    ISP_SPELLASSORT,
    ISP_ITEMASSORT,
    ISP_SPELLTRADE,
    ISP_ITEMTRADE,
    ISP_SPELLCONSTR,
    ISP_ITEMCONSTR,
    ISP_ITEMREPAIR,

    ISP_UNKNOWN
};
/////////////////////////////////////////////////////////////////////////////////////
enum EItemScreenModes
{
    // camp
    ISM_WEAPONS,
    ISM_SPELLS,
    // spell constructor
    ISM_SPELLTRADE,
    ISM_SPELLCONSTR,
    // item constructor
    ISM_ITEMTRADE,
    ISM_ITEMCONSTR,
    ISM_ITEMREPAIR,
    // exit
    ISM_EXIT,

    ISM_TOTAL,

    // multiplayer - choose personage
    ISM_CHOOSEPERS,
    ISM_SKIN,
    ISM_STATS

};
/////////////////////////////////////////////////////////////////////////////////////
enum EItemScreenKoeffs
{
    ISK_SPELL_BUY = 0,
    ISK_SPELL_SELL,
    ISK_SPELL_CREATE,
    ISK_SPELL_DESTROY,
    ISK_ITEM_BUY,
    ISK_ITEM_SELL,
    ISK_ITEM_CREATE,
    ISK_ITEM_DESTROY,
    ISK_ITEM_REPAIR,

    ISK_TOTAL
};
/////////////////////////////////////////////////////////////////////////////////////
enum EItemSlotTypes
{
    IST_UNKNOWN = 0,
    IST_SPELL,
    IST_WEAPON,
    IST_QITEM,
    IST_ARMOR_HELM,
    IST_ARMOR_PLATE,
    IST_ARMOR_LEGGINGS,
    IST_ARMOR_SHIRT,
    IST_ARMOR_PANTS,
    IST_ARMOR_BOOTS,
    IST_ARMOR_GLOVES,
    IST_ITEM_BUY,
    IST_ITEM_SELL,
    IST_ITEM_READY,
    IST_ITEM_INSTR,
    IST_ITEM_MAT,
    IST_ITEM_SPELL,
    IST_SPELL_BUY,
    IST_SPELL_SELL,
    IST_SPELL_READY,
    IST_SPELL_PROT,
    IST_SPELL_MOD,
    IST_WAGON,
    IST_ITEM_ASSORT,
    IST_SPELL_ASSORT,
    IST_ITEM_REPAIR,
    IST_MAX_COMPLEXITY,
    IST_MAX_MANA,
    IST_EXCH_BUY,
    IST_EXCH_SELL
};
/////////////////////////////////////////////////////////////////////////////////////
struct SCampSkillMouseInfo
{
    int nType;  // 0 - attribute 1 - auto skill, 2 - manual skill, 3 - perk, 4 - HL, 5 -
                // attack/defense
    int nSkill; // number of skill
    ERPGHitLocations eHL;
    CItemWeapon *pWeapon;
    SRPGUnitStats *pStats;  // stats of unit
    CUnit *pUnit = nullptr; // unit owning these displayed stats
    CString szNonlocalName; // nonlocal name of skill or hl
};
/////////////////////////////////////////////////////////////////////////////////////
// class describing rotation of item
class CRotateItem : public CObjectBase
{
public:
    float fAngle;
    float fSpeed;
    bool bIncludePrice;
    float fFlashVal;
    OBJECT_BASIC_METHODS(CRotateItem)
};
/////////////////////////////////////////////////////////////////////////////////////
// struct to pass for drawing item
struct SDrawItem
{
public:
    CItem *pItem;
    float fX;
    float fY;
    float fZ;
    float fPixSize;
    EItemScreenParts eFrom;
};
/////////////////////////////////////////////////////////////////////////////////////
// struct to pass for drawing human's head
struct SDrawHead
{
public:
    CString szPrototype;
    float fX;
    float fY;
    float fZ;
    float fPixSize;
};
/////////////////////////////////////////////////////////////////////////////////////
class CI3DFigureLRUAllocator
{
public:
    static CI3DFigure *CreateObject(const char *pszName);
    static void DestroyObject(CI3DFigure *pFig);
};
/////////////////////////////////////////////////////////////////////////////////////
class CTextureLRUAllocator
{
public:
    static CTexture *CreateObject(const char *pszName);
    static bool EnsureReady(CTexture *pTex);
    static void DestroyObject(CTexture *pFig);
};
/////////////////////////////////////////////////////////////////////////////////////
class CI3DObjectCampBG;
class CI3DObjectItemInfo;
class CPersonage;
class CParty;
/////////////////////////////////////////////////////////////////////////////////////
// all inventory screens ( camp, constructors ) are derived from this screen
struct CampItemModelCache;
class CI3DItemScreen : public CI3DScreen
{
    friend struct E4CampAccess;
protected:
    CI3DObjectItemInfo *pItemInfo;
    CItem *pMouseItem;
    std::shared_ptr<CampItemModelCache> campItemModels;
    CI3DObjectCampBG *pCampBG;

    CLRUStorage<CI3DFigure, CI3DFigureLRUAllocator> figuresLRU;
    CLRUStorage<CTexture, CTextureLRUAllocator> texturesLRU;

    CItem *MoveItem(CItem *pItem, CParty *pDst, CParty *pSrc, int nDstLimit = 0, bool bStack = true,
                    bool bIncludePrice = false);
    CItem *MoveAllItems(CParty *pDst, CParty *pSrc);

public:
    CI3DItemScreen(CInterface3D *pI)
        : CI3DScreen(pI)
        , pMouseItem(0)
    {
    }

    void DrawItem(CRenderer *pRend, SDrawItem &item, std::vector<std::string>* visibleLegacyIcons=nullptr);
    void DrawUnit(CRenderer *pRend, CUnit *pDrawUnit, float fAngle = 0, float fX = 400,
                  bool bArmors = false);
    void DrawHead(CRenderer *pRend, SDrawHead &head);

    virtual void ClickOnItem(CItem *pItem, EItemScreenParts eFrom) {}
    virtual bool CanClickOnItem(CItem *pItem, EItemScreenParts eFrom) { return false; }
    virtual void ClickOnSlot(int nSlotType, EItemScreenParts eFrom) {}

    void AddBackground();

    // for spell constructor
    bool IsItemPrototype(CItem *pItem);
    bool IsItemModifier(CItem *pItem);
    bool IsItemSpell(CItem *pItem);

    // for item constructor
    bool IsItemInstruction(CItem *pItem);
    bool IsItemMaterial(CItem *pItem);
    bool IsItemConstructed(CItem *pItem);

    virtual void GetMaxManaAndComplexity(ERPGMagicSubtypes subtype, int &nComplexity, int &nMana)
    {
        nComplexity = 0;
        nMana = 0;
    }
    virtual void UpdateWeapons() {}
    virtual void UpdateAttributes() {}
    virtual void RefreshProgression() {UpdateAttributes();}
    virtual void NextPersonage() {}
    virtual void PrevPersonage() {}
    virtual CUnit *GetUnit(int num) { return 0; }
    virtual CPersonage *GetCurrentPers() { return 0; }
    virtual void SetPrototype(LPCTSTR pszPrototype) {}
    virtual bool GetCreationPrimaryPoints(int &points) { points = -1; return false; }
    virtual bool CanEditCreationPrimary() { return false; }
    virtual bool CanAdjustCreationPrimary(int primary, int delta) { return false; }
    virtual bool AdjustCreationPrimary(int primary, int delta) { return false; }
    virtual float GetItemsWeight() { return 0; }

    virtual int GetItemPrice(CItem *pItem, EItemScreenKoeffs eKoeffType) { return 0; }
};
/////////////////////////////////////////////////////////////////////////////////////
class CI3DItemObject : public CI3DObject
{
    friend struct E4CampAccess;
protected:
    EItemScreenParts ePartType;
    CI3DItemScreen *pScreen;

public:
    CI3DItemObject(CInterface3D *pI, CI3DItemScreen *pScr)
        : CI3DObject(pI)
        , pScreen(pScr)
    {
    }

    void SetPartType(EItemScreenParts eType) { ePartType = eType; }

    virtual CItem *GetMouseItem(CPoint point, int &nSlotType)
    {
        nSlotType = 0;
        return 0;
    }
    virtual void OnLButtonDown(UINT nFlags, CPoint point);
};
/////////////////////////////////////////////////////////////////////////////////////
// standard window for displaying info about item ( used in camp, constructors )
class CI3DObjectItemInfo : public CI3DItemObject
{
    friend struct E4CampAccess;
private:
    CParty itemParty;
    int nX, nY;

    CStringArray szStrings;
    enum
    {
        WEIGHT,
        MATERIAL,
        ENERGY,
        DURABILITY,
        ABSORPTION,
        SPELL,
        PRESS_CTRL,
        MANA,
        EFFECT,
        SPEED,
        RANGE,
        AREA,
        DURATION,
        TARGETS,
        SLOTS,
        DAMAGE,
        SKILL,
        MATTYPE,
        MAGICSCHOOL,
        MODIFIERS,
        ACTIONPTS,
        INSTANT,
        AREAUNIT,
        RESIST,
        VULNERABLE,
        ABSORPARMOR,
        METERS,
        SECONDS,
        FIXEDEFF,
        WSLDEXT,
        WSLSKILL,
        WSLPERK,
        WSLWEAPON,
        WSLSUM,
        WSLBOWDEFENSE,
        MAXCOMPL,
        MAXMANA,
        ITEMTYPE,
        MODATTACK,
        MODDEFENSE,
        MSLFIRE,
        MSLLIGHTNING,
        MSLACID,
        MSLILLUSION,
        MSLENCHANTMENTS,
        TOTAL
    };
    CStringArray szSkillStr;
    enum
    {
        HEALTHMULT,
        ENCUMBRANCE,
        STAMINAMULT,
        ACTIONS,
        BONUSCP,
        SKILL2,
        BASETOHIT,
        DAMAGEMULT,
        SPELLCOMPLEX,
        USECOMPLEX,
        TOHITBONUS,
        SPELLBONUS,
        NIGHTVISION,
        ATTRBONUS,
        HEALTH,
        STAMINA,
        HEALTHRGNR,
        STAMINARGNR,
        COST,
        REQUIRES,
        DEPENDENT,
        PERKTAKEN,
        BSTABMULT,
        ATTRIBPLUS,
        BONUSMANA,
        TOTALSKILL
    };

private:
    void FormTextInfo();
    void FormSpellInfo();
    void FormMaterialInfo();
    void FormInstructionInfo();
    void FormPrototypeInfo();
    void FormModifierInfo();
    void FormPotionInfo();

    CItem *GetItem();
    CSpell *GetSpell();

public:
    CI3DObjectItemInfo(CInterface3D *pI, CI3DItemScreen *pScr)
        : CI3DItemObject(pI, pScr)
    {
    }
    ~CI3DObjectItemInfo();
    void Init(int x, int y);

    static void FormAbsorptionStrings(float *fValues, CStringArray &szResArray, bool bRound = true);

    void SetHelp(int nHelpID);
    void SetSkill(SCampSkillMouseInfo *pSkillInfo);
    void SetModSkill(SCampSkillMouseInfo *pSkillInfo);

    void SetItem(CItem *pItem);
    virtual BOOL Draw(CRenderer *pRend);

    virtual void ResetInputState();

    virtual void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
    virtual void OnKeyUp(UINT nChar, UINT nRepCnt, UINT nFlags);
};
/////////////////////////////////////////////////////////////////////////////////////
class CI3DObjectCampBG : public CI3DObject
{
public:
    CI3DObjectCampBG(CInterface3D *pI)
        : CI3DObject(pI)
    {
    }
    void Init();
    void SetHeight(int nHeight);
};
