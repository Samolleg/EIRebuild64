#pragma once
namespace SaveSchema { class Writer; class Reader; }
/////////////////////////////////////////////////////////////////////////////////////
#include "GameBase.h"
#include "sdbFormat.h"
#include "SpellEffect.h"
#include "RPGSystemConsts.h"

#include <list>
#include <string>
/////////////////////////////////////////////////////////////////////////////////////
// all types (spell/spell container/spell effect/spell effect container) consist of two parts:
// high word contain main prototype type
// low word contain this modification type
// ZB: 2nd fireball (type = 5) will be as follow: 0x00050002;
/////////////////////////////////////////////////////////////////////////////////////
// Modificator for any spell effect and spell consist of two parts:
// 1. high word of it contain modificator ID
// 2. low word contain modificator value
const DWORD SPELL_MOD_ID_MASK = 0xFFFF0000;  // mask to extract modificator's ID
const DWORD SPELL_MOD_VAL_MASK = 0x0000FFFF; // mask to extract modificator's value

const DWORD SPELL_MOD_RANGE = 0x00010000;     // range
const DWORD SPELL_MOD_RADIUS = 0x00020000;    // radius (for are effect only)
const DWORD SPELL_MOD_DURATION = 0x00030000;  // duration (for continuous only)
const DWORD SPELL_MOD_MANAECON = 0x00040000;  // mana economy (or mana spent)
const DWORD SPELL_MOD_ADDTARGET = 0x00050000; // additional targets (creates multitarget spell)
const DWORD SPELL_MOD_RESOLVE = 0x00060000;   // target resolving (friend/foe/specific race)
const DWORD SPELL_MOD_ITEM_TRIG = 0x10000000; // for items only: trigger effect
const DWORD SPELL_MOD_ITEM_CONT = 0x20000000; // for items only: continuous effect
/////////////////////////////////////////////////////////////////////////////////////
const DWORD SPELL_RESOLVE_ON_UNIT = 0x10000000;
const DWORD SPELL_RESOLVE_ON_TILE = 0x20000000;
const DWORD SPELL_FILTER_FRIENDS = 0x00000001;
const DWORD SPELL_FILTER_ENEMY = 0x00000002;
const DWORD SPELL_FILTER_HUMAN = 0x00000004;
const DWORD SPELL_FILTER_GOBLIN = 0x00000008;
const DWORD SPELL_FILTER_ORC = 0x00000010;
const DWORD SPELL_FILTER_LIZARD = 0x00000020;
const DWORD SPELL_FILTER_UNDEAD = 0x00000040;
// item-only modifiers
const DWORD SPELL_ITEM_TRIGGER = 0x00010000;
const DWORD SPELL_ITEM_CONTINUOUS = 0x00020000;
/////////////////////////////////////////////////////////////////////////////////////
//const int SPELL_ID_FIREARROW = 0, SPELL_ID_LIGHTNING = 1, SPELL_ID_ACIDRAY = 2,
//          SPELL_ID_FIREBALL = 3, SPELL_ID_INVOKE_LGHTNG = 4, SPELL_ID_COLUMN_ACID = 5,
//          SPELL_ID_FIREWALL = 6, SPELL_ID_WALL_LGHTNG = 7, SPELL_ID_POISONFOG = 8,
//          SPELL_ID_PROT_FIRE = 9, SPELL_ID_PROT_ELECTRIC = 10, SPELL_ID_PROT_ACID = 11,
//          SPELL_ID_EAGLE_SIGHT = 12, SPELL_ID_INFRAVISION = 13, SPELL_ID_DETECT_LIFE = 14,
//          SPELL_ID_INVISIBILITY = 15, SPELL_ID_SILENCE = 16, SPELL_ID_LICHDOM = 17,
//          SPELL_ID_FIREWORKS = 18, SPELL_ID_STENCH = 19, SPELL_ID_CLAIRVOYENCE = 20,
//          SPELL_ID_POSSESSION = 21, SPELL_ID_SHAPECHANGE = 22, SPELL_ID_VISIONFOG = 23,
//          SPELL_ID_HEALING = 24, SPELL_ID_STUN = 25, SPELL_ID_ANTIMAGIC = 26, SPELL_ID_LINK = 27,
//          SPELL_ID_TELEPORT = 28, SPELL_ID_STRENGTHEN = 29, SPELL_ID_WEAKEN = 30,
//          SPELL_ID_REGENERATION = 31, SPELL_ID_FEEBLEMIND = 32, SPELL_ID_SPEED = 33,
//          SPELL_ID_SLOW = 34, SPELL_ID_COMPREHEND = 35, SPELL_ID_CHARM = 36, SPELL_ID_ENLARGE = 37,
//          SPELL_ID_SHRINK = 38, SPELL_ID_CAMPFIRE = 39, SPELL_ID_RICKMAGIC = 40,
//          SPELL_ID_CURSEMAGIC = 41;
/////////////////////////////////////////////////////////////////////////////////////
class CSpellManager;
class CUnit;
/////////////////////////////////////////////////////////////////////////////////////
// contain integral values and spell effects
class CSpell : public CGameBase
{
    friend class CSpellContainer;

private:
    CSpellEffect effect; // effect of this spell
    // integral values
    float fpSpeed;      // spell movement speed
    float fpRange;      // range (0 - caster only, 1..3 - close combat, >3 - ranged)
    DWORD dwNumTargets; // number of targets (0 - don't req. target, 1 - simply one-target, >1 -
                        // multitarget)
    DWORD dwResolve;    // target resolving: friend/foe/specific race modifiers
    float fpManaCost;   // mana cost - some function of mods and mana cost of all effects
    DWORD dwSlots;      // spell slots capacity of this spell
    float fpPrice;      // price of this spell
    DWORD dwActions;    // action points to cast this spell
    DWORD dwComplexity;
    //
    char runes[8]; // zero-ended (or 8 bytes) array of bytes, which represent rune indices
public:
    CSpell();
    virtual ~CSpell();
    // create/destroy functions
    virtual BOOL Create();
    virtual void Destroy();
    //
    BOOL Setup(const SSDBSpellPrototype *pPrototype);
    //
    void operator=(const CSpell &sp) { ASSERT(0); }
    // duplication
    CSpell *Duplicate(CSpellManager *pSpellManager) const;
    void Duplicate(CSpell *pDest) const;
    // access to private integral values
    float GetSpeed() const { return fpSpeed; }
    float GetRange() const { return fpRange; }
    DWORD GetNumTargets() const { return dwNumTargets; }
    DWORD GetResolve() const { return dwResolve & 0x0FFFFFFF; }
    DWORD GetResolveFlags() const { return dwResolve; }
    const CSpellEffect &GetEffect() const { return effect; }
    DWORD GetFilters() const { return dwResolve & 0x0000FFFF; }
    float GetManaCost() const { return fpManaCost; }
    DWORD GetNumSlots() const { return dwSlots; }
    float GetPrice() const { return fpPrice; }
    DWORD GetActionPoints() const { return dwActions; }
    // spell effect data
    DWORD GetDuration() const { return effect.nDuration; }
    float GetPower() const { return effect.fpPower; }
    void SetPower(float value) { effect.fpPower = value; }
    float GetRadius() const { return effect.fpRadius; }
    float GetArea() const { return FP_PI * effect.fpRadius * effect.fpRadius; }
    void GetSpellEffect(CSpellEffect &res) const { res = effect; }
    //
    const SSDBSpellPrototype *GetPrototype() const;
    ERPGMagicSchools GetMagicSchool() const { return GetPrototype()->msSchool; }
    ERPGMagicSubtypes GetMagicSubtype() const { return GetPrototype()->mstSubtype; }
    ERPGAttackTypes GetAttackType() const;
    //
    bool IsValidTarget(CUnit *pCaster, CUnit *pTarget);
    bool CanInsertToItem() const;
    BOOL IsOnUnit() const { return BOOL(dwResolve & SPELL_RESOLVE_ON_UNIT); }
    BOOL IsOnTile() const { return BOOL(dwResolve & SPELL_RESOLVE_ON_TILE); }
    BOOL IsRequireTrace() const { return GetPrototype()->dwRequireTrace; }
    DWORD IsItemContinuous() const { return dwResolve & SPELL_ITEM_CONTINUOUS; }
    DWORD IsItemTrigger() const { return dwResolve & SPELL_ITEM_TRIGGER; }
    bool IsOffensive() const;
    //
    const char *GetRunes() const { return runes; }
    void SetNewRunes(const char *pcNewRunes);
    int GetRunesCount() const;
    DWORD GetComplexity() const { return dwComplexity; }
    //
    virtual void Load(CSave &file);
    virtual void Store(CSave &file);
    // class identification functions
    virtual LPCSTR GetGameClassName() const { return "CSpell"; }
    virtual EClassIDs WhoAmI() const { return HPC_SPELL; }
public:
    virtual void Store(SaveSchema::Writer &) const;
    virtual void Load(SaveSchema::Reader &);
    virtual void DiscardSave();
};
/////////////////////////////////////////////////////////////////////////////////////
typedef CSimplyArray<CSpell *, CSpell *> CSpellPtrArray;
/////////////////////////////////////////////////////////////////////////////////////
// class CSpellContainer : public CGameBase
// contains all spell modificators and spell effect containers
// can recalculate all of this data to integral params and store it in spell
class CSpellContainer : public CGameBase
{
    friend class CSpellConstructor;

private:
    // modificators and spell effects
    const SSDBSpellPrototype *pPrototype;  // spell prototype
    std::list<const SSDBModifier *> runes; // added modificators
    std::string szRuneIndices;             // sorted array of modificator indices, organized string
    CSpell spell;                          // integral values, recalculated from mods and prototype
    CString szName;                        // spell name
public:
    CSpellContainer();
    virtual ~CSpellContainer();
    // create/destroy functions
    virtual BOOL Create();
    virtual void Destroy();
    virtual void FreeReferences();
    //
    void SetPrototype(const SSDBSpellPrototype *pNewPrototype) { pPrototype = pNewPrototype; }
    const SSDBSpellPrototype *GetPrototype() const { return pPrototype; }
    // work with modificators and effects
    BOOL AddRune(const SSDBModifier *pRune);
    BOOL RemoveRune(const SSDBModifier *pRune);
    BOOL IsHaveRunes() const { return !runes.empty(); }
    BOOL CanAddRune(const SSDBModifier *pRune);
    // recalculate modificators and effects to integral values
    BOOL Recalculate();
    //
    const CSpell &GetSpell() const { return spell; }
    //
    DWORD GetBaseSpellType() const { return ((GetSubtypeID() >> 16) & 0x0000FFFF); }
    DWORD GetSpellSubtype() const { return (GetSubtypeID() & 0x0000FFFF); }
    void SetSpellSubtypeID(DWORD dwSubtype) { spell.SetSubtypeID(dwSubtype); }
    //
    void SetName(LPCSTR pszName) { szName = pszName; }
    virtual LPCSTR GetName() const { return szName; }
    const char *GetRuneIndices() const
    {
        return szRuneIndices.c_str();
    } // return zero-ended string with modifier indices
    // class identification functions
    virtual LPCSTR GetGameClassName() const { return "CSpellContainer"; }
    virtual EClassIDs WhoAmI() const { return HPC_SPELL_CONTAINER; }
public:
    virtual void Store(SaveSchema::Writer &) const;
    virtual void Load(SaveSchema::Reader &);
    virtual void DiscardSave();
};
/////////////////////////////////////////////////////////////////////////////////////
