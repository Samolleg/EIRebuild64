#pragma once
#include "RPGSystemConsts.h"
#include <vector>
/////////////////////////////////////////////////////////////////////////////////////
class CStructureSaver;
/////////////////////////////////////////////////////////////////////////////////////
enum ESDBSpellModifierTypes
{
    SDBSMT_RANGE,
    SDBSMT_TARGETS,
    SDBSMT_AREA,
    SDBSMT_EFFECT,
    SDBSMT_DURATION,
    SDBSMT_MANA,
    SDBSMT_FILTER,
    SDBSMT_ITEM,
    SDBSMT_UNKNOWN,

    SDBSMT_FORCE_DWORD = 0x7FFFFFFF
};
/////////////////////////////////////////////////////////////////////////////////////
const DWORD SDB_NUM_ALLOWANCES = 5;
const DWORD SDB_TOTAL_NUM_RUNE_TYPES = 8;
enum SDBAllowedRunes
{
    SDBAR_RANGE,
    SDBAR_TARGETS,
    SDBAR_AREA,
    SDBAR_EFFECT,
    SDBAR_DURATION,
    SDBAR_MANA,
    SDBAR_FILTER,
    SDBAR_ITEM,

    SDBAR_FORCE_DWORD = 0x7fffffff
};
/////////////////////////////////////////////////////////////////////////////////////
struct SSDBModifier
{
public:
    CString szName;
    union
    {
        DWORD dwCode;    // identification code of this modifier
        char pszCode[4]; // string name of this code
    };
    float fpPrice; // price
    ESDBSpellModifierTypes
        smtType;   // type: range, targets, area, effect, duration, mana, filter, item
    float fpMana;  // mana cost
    float fpValue; // modificator value: base = base + base*value
    DWORD dwComplexity;
    CString szShop;
    //
    DWORD dwAvailableInShop; // set bit for each shop, this material available in
public:
    LPCSTR GetCodeName() const { return pszCode; }
    LPCSTR GetCode() const { return pszCode; }

    ESDBSpellModifierTypes GetRuneType() const { return smtType; }
    SDBAllowedRunes GetAllowedRuneType() const { return SDBAllowedRunes(smtType); }

    bool IsAvailableInShop(int nNumber) const { return (dwAvailableInShop & (1 << nNumber)) != 0; }

    void Serialize(CStructureSaver &file);
};
/////////////////////////////////////////////////////////////////////////////////////
struct SSDBSpellPrototype
{
public:
    CString szName;            // this spell name
    CString szCodeName;        // code name - short version of the name above
    CString szSubtype;         // magic school subtype
    float fpPrice;             // base price
    ERPGMagicSchools msSchool; // magic school type: elemental, senses, astral
    ERPGMagicSubtypes
        mstSubtype;       // magic subtype:
                          // fire/lightning/acid/illusion/divination/enchantments/healing/domination
    float fpMana;         // base mana cost
    DWORD dwSlots;        // number of slots, which this spell will occupy in item
    float fpSpeed;        // speed of this spell movement (m/sec)
    float fpRange;        // range of this spell action
    float fpArea;         // area of spell activation
    float fpEffect;       // common effect power
    DWORD dwTargeting;    // targeting type: unit/terrain/self
    DWORD dwNumTargets;   // number of targets. default = 1
    DWORD dwDuration;     // duration (in segments: 15 segments/sec)
    DWORD dwActions;      // action points to cast this spell
    DWORD dwRequireTrace; // doe this spell require trace to obstacles ?
    DWORD dwComplexity;
    std::vector<DWORD>
        basemods; // base modifiers, which can't be removed from this spell during craping
    std::vector<DWORD> mods;         // special modifiers
    DWORD dwTextureTypeIndex;        // texture type index to create texture name of this spell icon
    float fLightR, fLightG, fLightB; // light color
    float fLightRadius;              // light base radius
    float fFadeoutTime;              // spell light post-effect fadeout time
    // allowed runes
    bool bRuneAllowed[SDB_NUM_ALLOWANCES];
    //
    DWORD dwAvailableInShop; // set bit for each shop, this material available in
public:
    LPCSTR GetCodeName() const { return szCodeName; }
    LPCSTR GetCode() const { return szCodeName; }

    bool IsAvailableInShop(int nNumber) const { return (dwAvailableInShop & (1 << nNumber)) != 0; }

    void Serialize(CStructureSaver &file);
};
/////////////////////////////////////////////////////////////////////////////////////
struct SSDBComplexSpell
{
public:
    // basic prototype
    std::string szPrototype;
    // runes
    std::vector<std::string> required; // required runes
    std::vector<std::string> optional; // optinal runes - add randomply
    std::string szPower;               // power rune - add after 'optinal' to complete spell
    // available in shop...
    DWORD dwAvailableInShop; // set bit for each shop, this spell available in
                             //
public:
    bool IsAvailableInShop(int nNumber) const { return (dwAvailableInShop & (1 << nNumber)) != 0; }

    void Serialize(CStructureSaver &file);
};
/////////////////////////////////////////////////////////////////////////////////////
