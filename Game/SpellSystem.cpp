#include "StdAfx.h"
#include "SaveSchema.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "SpellSystem.h"
#include "SpellManager.h"
#include "Unit.h"
#include "ModRuntime.h"
#include "ModPackage.h"
#include "ModSpellRules.h"
#include "ModSpellTargeting.h"

#include <functional>
#include <algorithm>
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
// **********                  class CSpell functions                    ********** //
// ******************************************************************************** //
/////////////////////////////////////////////////////////////////////////////////////
CSpell::CSpell()
{
    Create();
#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
}
/////////////////////////////////////////////////////////////////////////////////////
CSpell::~CSpell()
{
    Destroy();
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CSpell::Create()
{
    //
    fpSpeed = 0;
    fpRange = 0;
    dwNumTargets = 0;
    dwResolve = 0;
    fpManaCost = 0;
    dwSlots = 0;
    fpPrice = 0;
    dwActions = 0;
    dwComplexity = 1;
    //
    memset(runes, 0, sizeof(runes));

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// void CSpell::Destroy()
void CSpell::Destroy()
{
    FreeReferences();
    // delete runes...
    memset(runes, 0, sizeof(runes));
}
/////////////////////////////////////////////////////////////////////////////////////
void CSpell::Duplicate(CSpell *pDest) const
{
    // destroy old data
    pDest->Destroy();
    // copy effect
    pDest->effect = effect;
    // copy integral values
    pDest->fpSpeed = fpSpeed;
    pDest->fpRange = fpRange;
    pDest->dwNumTargets = dwNumTargets;
    pDest->dwResolve = dwResolve;
    pDest->fpManaCost = fpManaCost;
    pDest->dwSlots = dwSlots;
    pDest->fpPrice = fpPrice;
    pDest->dwActions = dwActions;
    pDest->dwComplexity = dwComplexity;
    //
    pDest->SetNewRunes(runes);
    //
    pDest->SetSubtypeID(GetSubtypeID());
}
/////////////////////////////////////////////////////////////////////////////////////
// CSpell* CSpell::Duplicate() const
// diplicate spell:
// 		1. create empty spell
// 		2. copy this spell data to new one
CSpell *CSpell::Duplicate(CSpellManager *pSpellManager) const
{
    CSpell *pSpell = pSpellManager->CreateSpell();
    Duplicate(pSpell);

    return pSpell;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CSpell::Setup(const SSDBSpellPrototype *pPrototype)
{
    // setup main spell's data
    fpSpeed = pPrototype->fpSpeed;
    fpRange = pPrototype->fpRange;
    if (pPrototype->dwTargeting == 'u')
        dwResolve = SPELL_RESOLVE_ON_UNIT;
    else if (pPrototype->dwTargeting == 't')
        dwResolve = SPELL_RESOLVE_ON_TILE;
    dwNumTargets = pPrototype->dwNumTargets;
    fpManaCost = pPrototype->fpMana;
    dwSlots = pPrototype->dwSlots;
    fpPrice = pPrototype->fpPrice;
    dwActions = pPrototype->dwActions;
    dwComplexity = pPrototype->dwComplexity;
    // setup spell effects
    effect.nVisStrength = 0x10;
    effect.nSpellID = GetSubtypeID() >> 16;
    effect.fpPower = pPrototype->fpEffect;
    effect.fpRadius = pPrototype->fpArea;
    effect.nDuration = pPrototype->dwDuration;

    SetNewRunes(0);

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
static bool AreFriends(CUnit *p1, CUnit *p2)
{
    return (ModRuntime::Current() && p1 == p2) || (!p1->IsEnemy(p2->GetPlayerNum()) && !p2->IsEnemy(p1->GetPlayerNum()));
}
/////////////////////////////////////////////////////////////////////////////////////
bool CSpell::IsValidTarget(CUnit *pCaster, CUnit *pTarget)
{
    DWORD dwFilter = GetFilters();
    if (dwFilter == 0)
        return true;
    if (pTarget == 0 || pTarget->WhoAmI() != HPC_UNIT)
        return false;
    if (dwFilter & SPELL_FILTER_FRIENDS)
    {
        if (pCaster && !AreFriends(pCaster, pTarget))
            return false;
    }
    if (dwFilter & SPELL_FILTER_ENEMY)
    {
        if (pCaster && AreFriends(pCaster, pTarget))
            return false;
    }
    const char *pszMaskName = pTarget->GetMaskName();
    if (ModRuntime::Current() && ModSpells::ExcludesUndead(dwFilter, pszMaskName)) return false;
    if ((dwFilter & SPELL_FILTER_HUMAN) &&
        (strcmp(pszMaskName, "unhuma") == 0 || strcmp(pszMaskName, "unhufe") == 0))
        return false;
    if ((dwFilter & SPELL_FILTER_GOBLIN) && (strcmp(pszMaskName, "unmogo") == 0))
        return false;
    if ((dwFilter & SPELL_FILTER_ORC) &&
        (strcmp(pszMaskName, "unorma") == 0 || strcmp(pszMaskName, "unorfe") == 0))
        return false;
    if ((dwFilter & SPELL_FILTER_LIZARD) && (strcmp(pszMaskName, "unmoli") == 0))
        return false;
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
ERPGAttackTypes CSpell::GetAttackType() const
{
    if(ModRuntime::Current()){int attack=ModSpellIdentity::AttackOverride(int(GetSubtypeID()>>16));if(attack>=0)return ERPGAttackTypes(attack);}
    switch (ModRuntime::Current()?ModSpells::ElementalBase(GetSubtypeID() >> 16):int(GetSubtypeID() >> 16))
    {
    case SPELL_ID_INVOKE_LGHTNG:
    case SPELL_ID_LIGHTNING:
    case SPELL_ID_WALL_LGHTNG:
    case SPELL_ID_CURSEMAGIC:
        return RPGAT_ELECTRICAL;
    case SPELL_ID_FIREBALL:
    case SPELL_ID_FIREARROW:
    case SPELL_ID_FIREWALL:
    case SPELL_ID_RICKMAGIC:
        return RPGAT_TERMICAL;
    case SPELL_ID_ACIDRAY:
    case SPELL_ID_COLUMN_ACID:
    case SPELL_ID_POISONFOG:
        return RPGAT_CHEMICAL;
    default:
        ASSERT(0);
        break;
    }
    return RPGAT_GENERAL;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CSpell::IsOffensive() const
{
    if(auto* mod=ModRuntime::Current())return ModSpellTargeting::Offensive(int(GetSubtypeID()>>16),
        mod->CombatSetting("JMVSelfCast")!=0,mod->CombatSetting("JMVParticles")!=0,&mod->SpellAddon());
    switch (int(GetSubtypeID() >> 16))
    {
    case SPELL_ID_FIREARROW:
    case SPELL_ID_LIGHTNING:
    case SPELL_ID_ACIDRAY:
    case SPELL_ID_FIREBALL:
    case SPELL_ID_INVOKE_LGHTNG:
    case SPELL_ID_COLUMN_ACID:
    case SPELL_ID_FIREWALL:
    case SPELL_ID_WALL_LGHTNG:
    case SPELL_ID_POISONFOG:
    case SPELL_ID_LICHDOM:
    case SPELL_ID_STENCH:
    case SPELL_ID_STUN:
    case SPELL_ID_WEAKEN:
    case SPELL_ID_FEEBLEMIND:
    case SPELL_ID_SLOW:
    case SPELL_ID_CHARM:
    case SPELL_ID_SHRINK:
    case SPELL_ID_RICKMAGIC:
    case SPELL_ID_CURSEMAGIC:
        return true;
    }
    return false;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CSpell::CanInsertToItem() const
{
    for (int i = 0; i < 8 && runes[i] != 0; ++i)
    {
        const SSDBModifier *rune = static_cast<CSpellManager *>(lpManager)->GetModifier(runes[i] -
                                                                                        1);
        if ((rune->dwCode == 0x00006369) || (rune->dwCode == 0x00007469)) // 'ic' or 'it'
            return true;
    }

    return false;
}
/////////////////////////////////////////////////////////////////////////////////////
// set new runes from zero-ended array. delete old runes
void CSpell::SetNewRunes(const char *pcNewRunes)
{
    memset(runes, 0, sizeof(runes));
    //
    if (pcNewRunes)
    {
        for (int i = 0; i < 8 && pcNewRunes[i] != 0; ++i)
            runes[i] = pcNewRunes[i];
    }
}
/////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////
const SSDBSpellPrototype *CSpell::GetPrototype() const
{
    return theSpellDatabase.spells[GetSubtypeID() >> 16];
}
/////////////////////////////////////////////////////////////////////////////////////
int CSpell::GetRunesCount() const
{
    for (int i = 0; i < 8; ++i)
    {
        if (runes[i] == 0)
            return i;
    }
    return 8;
}
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
// **********              class CSpellContainer functions               ********** //
// ******************************************************************************** //
/////////////////////////////////////////////////////////////////////////////////////
CSpellContainer::CSpellContainer()
{
    Create();
#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
}
/////////////////////////////////////////////////////////////////////////////////////
CSpellContainer::~CSpellContainer()
{
    Destroy();
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CSpellContainer::Create()
// create
BOOL CSpellContainer::Create()
{
    pPrototype = 0;
    runes.clear();
    spell.Create();

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// void CSpellContainer::Destroy()
// destroy
void CSpellContainer::Destroy()
{
    FreeReferences();
    pPrototype = 0;
    runes.clear();
    spell.Destroy();
}
/////////////////////////////////////////////////////////////////////////////////////
void CSpellContainer::FreeReferences()
{
}
/////////////////////////////////////////////////////////////////////////////////////
// add rune to spell (+ check spell capacity)
// return TRUE if can add, FALSE in other case
BOOL CSpellContainer::AddRune(const SSDBModifier *pRune)
{
    if (!pRune)
        return ReportError("Invalid rune");
    //
    runes.push_back(pRune);

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// remove rune from spell.
BOOL CSpellContainer::RemoveRune(const SSDBModifier *pRune)
{
    runes.remove(pRune);
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CSpellContainer::CanAddRune(const SSDBModifier *pRune)
{
    if (!pPrototype || !pRune)
        return FALSE;
    //
    switch (pRune->pszCode[0])
    {
    case 'r': // range
        return pPrototype->bRuneAllowed[SDBAR_RANGE];
    case 't': // targets
        return pPrototype->bRuneAllowed[SDBAR_TARGETS];
    case 'a': // area
        return pPrototype->bRuneAllowed[SDBAR_AREA];
    case 'e': // effect
        return pPrototype->bRuneAllowed[SDBAR_EFFECT];
    case 'd': // duration
        return pPrototype->bRuneAllowed[SDBAR_DURATION];
    }
    //
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// recalculate values of this spell
BOOL CSpellContainer::Recalculate()
{
    // setup start values
    spell.fpSpeed = pPrototype->fpSpeed;
    spell.fpRange = pPrototype->fpRange;
    if (pPrototype->dwTargeting == 'u')
        spell.dwResolve = SPELL_RESOLVE_ON_UNIT;
    else if (pPrototype->dwTargeting == 't')
        spell.dwResolve = SPELL_RESOLVE_ON_TILE;
    spell.dwNumTargets = pPrototype->dwNumTargets;
    spell.fpManaCost = pPrototype->fpMana;
    spell.dwSlots = pPrototype->dwSlots;
    spell.fpPrice = pPrototype->fpPrice;
    spell.dwActions = pPrototype->dwActions;

    spell.effect.nVisStrength = 0x10;
    spell.effect.nSpellID = theSpellDatabase.spells.GetEntryIndex(GetPrototype()->GetCodeName());
    spell.effect.fpPower = pPrototype->fpEffect;
    spell.effect.fpRadius = pPrototype->fpArea;
    spell.effect.nDuration = pPrototype->dwDuration;
    spell.dwComplexity = pPrototype->dwComplexity;
    //
    spell.SetSubtypeID(spell.effect.nSpellID << 16);
    // apply runes
    szRuneIndices.clear();
    for (auto iter = runes.begin(); iter != runes.end(); ++iter)
    {
        const SSDBModifier *pRune = *iter;
        //
        auto*mod=ModRuntime::Current();
        bool handled=mod&&mod->CombatSetting("NewRunes")&&ModSpells::ApplyRune(*pRune,*pPrototype,spell.dwActions,spell.effect.fpPower,spell.effect.nDuration,spell.dwResolve);
        if(!handled) switch (pRune->pszCode[0])
        {
        case 'r': // range
            spell.fpRange += pPrototype->fpRange * pRune->fpValue;
            if(!mod||!mod->CombatSetting("AllowMagicNegative"))spell.fpRange = Max(0.0f, spell.fpRange); // can't be negative
            break;
        case 't': // num targets
            spell.dwNumTargets += DWORD(pPrototype->dwNumTargets * pRune->fpValue);
            break;
        case 'a': // area
            spell.effect.fpRadius += pPrototype->fpArea * pRune->fpValue;
            if(!mod||!mod->CombatSetting("AllowMagicNegative"))spell.effect.fpRadius = Max(0.0f, spell.effect.fpRadius);
            break;
        case 'e': // effect power
            spell.effect.fpPower += pPrototype->fpEffect * pRune->fpValue;
            if(!mod||!mod->CombatSetting("AllowMagicNegative"))spell.effect.fpPower = Max(0.0f, spell.effect.fpPower);
            break;
        case 'd': // duration
            spell.effect.nDuration += static_cast<DWORD>(pPrototype->dwDuration * pRune->fpValue);
            spell.effect.nDuration = Max(0L, spell.effect.nDuration);
            break;
        case 'm': // mana
            spell.fpManaCost += pPrototype->fpMana * pRune->fpValue;
            if(!mod||!mod->CombatSetting("AllowMagicNegative"))spell.fpManaCost = Max(0.0f, spell.fpManaCost);
            break;
        case 'f': // filters
            switch (pRune->pszCode[1])
            {
            case 'f':
                spell.dwResolve |= SPELL_FILTER_FRIENDS;
                break;
            case 'e':
                spell.dwResolve |= SPELL_FILTER_ENEMY;
                break;
            case 'h':
                spell.dwResolve |= SPELL_FILTER_HUMAN;
                break;
            case 'g':
                spell.dwResolve |= SPELL_FILTER_GOBLIN;
                break;
            case 'o':
                spell.dwResolve |= SPELL_FILTER_ORC;
                break;
            case 'l':
                spell.dwResolve |= SPELL_FILTER_LIZARD;
                break;
            }
            break;
        case 'i': // item-only modificators
            switch (pRune->pszCode[1])
            {
            case 'c':
                spell.dwResolve |= SPELL_ITEM_CONTINUOUS;
                break;
            case 't':
                spell.dwResolve |= SPELL_ITEM_TRIGGER;
                break;
            }
            break;
        }
        ++spell.dwSlots;
        spell.dwComplexity += pRune->dwComplexity;
        spell.fpManaCost += pRune->fpMana;
        spell.fpPrice += pRune->fpPrice;
        szRuneIndices += static_cast<char>(
            static_cast<CSpellManager *>(GetManager())->GetModifierIndex(pRune->GetCodeName()) + 1);
    }
    // now lets recalculate area modifier to modify area instead of radius
    if (spell.effect.fpRadius > 0)
        spell.effect.fpRadius = sqrt(spell.effect.fpRadius / FP_PI);
    //
    spell.fpManaCost = Max(pPrototype->fpMana, spell.fpManaCost);

    if (!szRuneIndices.empty())
        std::sort(szRuneIndices.begin(), szRuneIndices.end());
    //
    spell.SetNewRunes(szRuneIndices.c_str());

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////

// Local spell payload has only scalar fields; IDs/type/flags are in StoreList.
void CSpell::StoreLocal(CSave &file) const
{
    file.Write(effect.nSpellID);
    file.Write(effect.nVisStrength);
    file.Write(effect.nDuration);
    file.Write(effect.fpPower);
    file.Write(effect.fpRadius);
    file.Write(effect.bQuiet);
    file.Write(fpSpeed);
    file.Write(fpRange);
    file.Write(dwNumTargets);
    file.Write(dwResolve);
    file.Write(fpManaCost);
    file.Write(dwSlots);
    file.Write(fpPrice);
    file.Write(dwActions);
    file.Write(dwComplexity);
    for (auto &rune : runes) file.Write(rune);
}
void CSpell::LoadLocal(CSave &file)
{
    file.Read(effect.nSpellID);
    file.Read(effect.nVisStrength);
    file.Read(effect.nDuration);
    file.Read(effect.fpPower);
    file.Read(effect.fpRadius);
    file.Read(effect.bQuiet);
    file.Read(fpSpeed);
    file.Read(fpRange);
    file.Read(dwNumTargets);
    file.Read(dwResolve);
    file.Read(fpManaCost);
    file.Read(dwSlots);
    file.Read(fpPrice);
    file.Read(dwActions);
    file.Read(dwComplexity);
    for (auto &rune : runes) file.Read(rune);
}
