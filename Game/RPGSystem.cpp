#include "StdAfx.h"
#include "ModArena.h"
#include "ModNativeCrtRandom.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "RPGSystem.h"
#include "ModRuntime.h"
#include "ModPackage.h"
#include "ModSpellRules.h"
#include "ModProgression.h"

#include "aiSettings.h"
#include "GeneralSettings.h"
#include "NetProtocol.h"
#include "NetUpdate.h"
#include "RandomGen.h"
#include "console.h"
#include "PerkDatabase.h"
#include "GSVars.h"
#include "Unit.h"
#include "UnitDatabase.h"
#include <stdexcept>
#include "UnitServer.h"
#include "ackFormat.h"
#include "ItemDatabase.h"
#include "Item.h"

#include <numeric>
/////////////////////////////////////////////////////////////////////////////////////
// power and logs
const float FP_POWER_BASE = 1.1f;
const float FP_POWER_LN = float(log(FP_POWER_BASE));
const float FP_HP_LOG_BASE = float(log(1.61f));
/////////////////////////////////////////////////////////////////////////////////////
static ostream &rpgLog = NConsole::rpg;
/////////////////////////////////////////////////////////////////////////////////////
void SRPGScienceStats::Load(CSave &file)
{
    file.Read(dwTools);
    file.Read(dwKeyNumber);
    file.Read(dwSkill);
}
/////////////////////////////////////////////////////////////////////////////////////
void SRPGScienceStats::Store(CSave &file)
{
    file.Write(dwTools);
    file.Write(dwKeyNumber);
    file.Write(dwSkill);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL SRPGScienceStats::CanOpen(const SRPGScienceStats &door) const
{
    if (dwTools & door.dwTools)
    {
        if ((dwTools & door.dwTools & RPGTOOL_KEY) == RPGTOOL_KEY)
        {
            if (dwKeyNumber == door.dwKeyNumber)
                return TRUE;
        }
        if (dwTools & door.dwTools & (RPGTOOL_HAND | RPGTOOL_PICKLOCK | RPGTOOL_CROWBAR))
            return door.dwSkill == 0 || static_cast<LONG>(dwSkill) >= static_cast<LONG>(door.dwSkill);
    }
    return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL SRPGScienceStats::CanDisarmTrap() const
{
    // CRAP
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// SRPGGeneralAttack
/////////////////////////////////////////////////////////////////////////////////////
// существенно упрощенная функция расчета повреждений для быстрых прикидок
WORD SRPGGeneralAttack::CalcDamage(const SRPGGeneralAttack &def) const
{
    WORD wRes = 0;
    for (int i = 0; i < RPG_NUM_ATTACK_TYPES; i++)
    {
        WORD wTest = wAttacks[i] - def.wAttacks[i], wMask;
        wMask = wTest;
        wMask = -(wMask >> 15);
        wRes += wTest & ~wMask;
    }
    return wRes;
}
/////////////////////////////////////////////////////////////////////////////////////
WORD SRPGGeneralAttack::GetMaxDamage() const
{
    WORD wRes = 0;
    for (int i = 0; i < RPG_NUM_ATTACK_TYPES; i++)
        wRes = Max(wRes, wAttacks[i]);
    return wRes;
}
/////////////////////////////////////////////////////////////////////////////////////
static const ERPGHitLocations armorHLs[][3] = {
    { RPGHL_HEAD, RPGHL_UNKNOWN, RPGHL_UNKNOWN }, // RPGAST_HELM
    { RPGHL_BODY, RPGHL_LHAND, RPGHL_RHAND },     // RPGAST_PLATE
    { RPGHL_UNKNOWN, RPGHL_LLEG, RPGHL_RLEG },    // RPGAST_LEGGINGS
    { RPGHL_BODY, RPGHL_LHAND, RPGHL_RHAND },     // RPGAST_SHIRT
    { RPGHL_UNKNOWN, RPGHL_LLEG, RPGHL_RLEG },    // RPGAST_PANTS
    { RPGHL_UNKNOWN, RPGHL_LLEG, RPGHL_RLEG },    // RPGAST_BOOTS
    { RPGHL_UNKNOWN, RPGHL_LHAND, RPGHL_RHAND }   // RPGAST_GLOVES
};
static const ERPGArmorTypes hlArmors[][2][2] = {
    { { RPGAST_UNKNOWN, RPGAST_UNKNOWN }, { RPGAST_HELM, RPGAST_UNKNOWN } }, // head
    { { RPGAST_SHIRT, RPGAST_UNKNOWN }, { RPGAST_PLATE, RPGAST_UNKNOWN } },  // body
    { { RPGAST_SHIRT, RPGAST_GLOVES }, { RPGAST_PLATE, RPGAST_UNKNOWN } },   // left hand
    { { RPGAST_SHIRT, RPGAST_GLOVES }, { RPGAST_PLATE, RPGAST_UNKNOWN } },   // right hand
    { { RPGAST_PANTS, RPGAST_BOOTS }, { RPGAST_LEGGINGS, RPGAST_UNKNOWN } }, // left leg
    { { RPGAST_PANTS, RPGAST_BOOTS }, { RPGAST_LEGGINGS, RPGAST_UNKNOWN } }  // right leg
};
/////////////////////////////////////////////////////////////////////////////////////
inline float LogBase(float fpVal)
{
    return fpVal < 1e-3f ? 0 : float(log(fpVal)) / FP_POWER_LN;
}
inline float PowerBase(float fpVal)
{
    return float(pow(FP_POWER_BASE, fpVal));
}
inline float Log(float fVal, float fBase)
{
    return static_cast<float>(log(fVal) / log(fBase));
}
/////////////////////////////////////////////////////////////////////////////////////
// SRPGAttackPortion
/////////////////////////////////////////////////////////////////////////////////////
// create attack portion, which will ANNIHILATE ANY UNIT!
void SRPGAttackPortion::MakeDeadlyAttack()
{
    Clear();
    for (int i = 0; i < RPG_NUM_ATTACK_TYPES; i++)
        fpAttacks[i] = 1;
    nSkill = 0;
    fpToHit = FLT_MAX / 100;
    fpDamage = FLT_MAX / 100;
    hl = RPGHL_ALL;
    bAreaEffect = FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
bool SRPGAttackPortion::IsMagicAttack() const
{
    return fpAttacks[RPGAT_TERMICAL] != 0 || fpAttacks[RPGAT_CHEMICAL] != 0 ||
           fpAttacks[RPGAT_ELECTRICAL] != 0;
}
/////////////////////////////////////////////////////////////////////////////////////
// SRPGHitLocation
/////////////////////////////////////////////////////////////////////////////////////
// export HL stats to user HL stats
void SRPGHitLocation::Export(SRPGUserHitLocation *lpStats) const
{
    DWORD i, j;
    // HL general
    lpStats->hlt = hlt;
    lpStats->cCrippleMask = BYTE(dwCrippleMask);
    lpStats->state = state;
    lpStats->wHP = (WORD)MINT(fpHP);
    lpStats->wMaxHP = (WORD)MINT(fpMaxHP);
    // resistances/absorbtions
    for (i = 0; i < RPG_NUM_ATTACK_TYPES; i++)
    {
        float fpAbsorbtion = 0;
        // sumup resistances and absorbtions for i-th attack type from all armor layes
        for (j = 0; j < RPG_NUM_ARMOR_LAYERS; j++)
            fpAbsorbtion += layers[j].fpAbsorbtions[i];
        // export total resistance and absorbtion for i-th attack type
        lpStats->wAbsorbtions[i] = (WORD)MINT(fpAbsorbtion);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// export common stats to user common stats
void SRPGCommonStats::Export(SRPGUserCommonStats *lpStats) const
{
    // export vital stats (HP & MP)
    lpStats->nHP = (short)MINT(ssHP.fpCurrent);
    lpStats->nMaxHP = (short)MINT(ssHP.fpMax);
    lpStats->wMP = (WORD)MINT(ssMP.fpCurrent);
    lpStats->wMaxMP = (WORD)MINT(ssMP.fpMax);
    // export resistances/absorbtions
    for (DWORD i = 0; i < RPG_NUM_ATTACK_TYPES; i++)
        lpStats->wAbsorbtions[i] = (WORD)MINT(psAbsorbtions[i].fpEffectiveValue);
}
/////////////////////////////////////////////////////////////////////////////////////
// export unit stats to user unit stats
void SRPGUnitStats::Export(const CUnit *pUnit, SRPGUserUnitStats *lpStats) const
{
    DWORD i;
    // exp. + char. pts.
    lpStats->dwExperience = (DWORD)MINT(fpExperience);
    lpStats->nFreeExp = (long)MINT(GetRealExp() - fSpentExp);
    // primary attributes
    lpStats->cStrength = (BYTE)MINT(psStrength.fpEffectiveValue);
    lpStats->cDexterity = (BYTE)MINT(psDexterity.fpEffectiveValue);
    lpStats->cIntelligence = (BYTE)MINT(psIntelligence.fpEffectiveValue);
    // secondary attributes
    i = Float2Int(fpSpeeds[0] * 255.0f / FP_RPG_MAX_SPEED);
    lpStats->cSpeeds[0] = BYTE(Min(DWORD(255), i));
    i = Float2Int(fpSpeeds[1] * 255.0f / FP_RPG_MAX_SPEED);
    lpStats->cSpeeds[1] = BYTE(Min(DWORD(255), i));
    i = Float2Int(fpSpeeds[2] * 255.0f / FP_RPG_MAX_SPEED);
    lpStats->cSpeeds[2] = BYTE(Min(DWORD(255), i));
    i = Float2Int(fpSpeeds[3] * 255.0f / FP_RPG_MAX_SPEED);
    lpStats->cSpeeds[3] = BYTE(Min(DWORD(255), i));
    i = Float2Int(move.fpEffectiveValue * 255.0f / 10.0f);
    lpStats->cMove = BYTE(Min(DWORD(255), i));
    lpStats->cAction = (BYTE)MINT(action.fpEffectiveValue);
    lpStats->wEncumbrance = (WORD)MINT(fpMaxEncumbrance);
    lpStats->cLocomotion = (BYTE)dwLocomotion;
    // native attack weapon params
    lpStats->cNAWRange = static_cast<BYTE>(MINT(fpNAWRange));
    lpStats->cNAWToHit = static_cast<BYTE>(MINT(fpNAWToHit));
    lpStats->cNAWToHitRandom = static_cast<BYTE>(MINT(fpNAWToHitRandom));
    lpStats->cNAWParry = static_cast<BYTE>(MINT(fpNAWParry));
    lpStats->cNAWWeight = static_cast<BYTE>(MINT(fpNAWWeight));
    lpStats->cNAWDamageMin = static_cast<BYTE>(fpNAWDamageMin);
    lpStats->cNAWDamageRnd = static_cast<BYTE>(fpNAWDamageRnd);
    lpStats->cNAWWeaponType = static_cast<BYTE>(rpgNAWWeaponType);
    // senses and percepts
    for (i = 0; i < RPG_NUM_SENSES; i++)
    {
        lpStats->cSenses[i] = (BYTE)MINT(senses[i].fpEffectiveValue);
        lpStats->cPercepts[i] = (BYTE)MINT(percepts[i].fpEffectiveValue);
    }
    // hit locations
    for (i = 0; i < RPG_NUM_HIT_LOCATIONS; i++)
        hls[i].Export(&(lpStats->hls[i]));
    // skills
    memcpy(lpStats->cManualSkills, cManualSkills, sizeof(cManualSkills));
    memcpy(lpStats->cAutoSkills, cAutoSkills, sizeof(cAutoSkills));
    memcpy(lpStats->cPerks, cPerks, sizeof(cPerks));
    lpStats->bFlags = bFlags;
}
/////////////////////////////////////////////////////////////////////////////////////
// export unit absorbs for different attack types
void SRPGUnitStats::ExportAbsorbs(CUnit *pUnit, SRPGCommonStats *lpCommonStats,
                                  SRPGGeneralAttack &res) const
{
    int i, k;
    bool bHero = IsIDofPersonage(pUnit->GetID());
    const SRPGHitLocation &hl = hls[RPGHL_BODY];
    for (i = 0; i < RPG_NUM_ATTACK_TYPES; i++)
    {
        float fpRes = lpCommonStats->psAbsorbtions[i].fpEffectiveValue;
        // pass damage through all armor layers of this HL
        int nStartArmorLayer = 0;
        if (bHero)
            nStartArmorLayer = RPG_NUM_ARMOR_LAYERS - 1;
        for (k = nStartArmorLayer; k >= 0; k--)
        {
            if (hl.layers[k].state < RPGALS_DEAD)
                continue; // this armor layer crippled or unused
            // add absorbtion
            fpRes += hl.layers[k].fpAbsorbtions[i];
        }
        res.wAttacks[i] = Float2Int(fpRes);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// LPGAMEBASE SRPGUnitStats::Wear( LPGAMEBASE lpItemArmor )
// wear new armor
// 	1. remove old armor
// 	2. wear new one
// 	3. recalc all affected stats
void SRPGUnitStats::Wear(LPGAMEBASE lpItemArmor)
{
    CItemArmor *lpArmor = (CItemArmor *)lpItemArmor;
    int j, k;

    for (j = 0; j < 3; j++)
    {
        if (armorHLs[lpArmor->GetArmorType()][j] == RPGHL_UNKNOWN)
            continue;
        SRPGHitLocation &hl = hls[armorHLs[lpArmor->GetArmorType()][j]];
        SRPGArmorLayer &al = hls[armorHLs[lpArmor->GetArmorType()][j]]
                                 .layers[lpArmor->GetALIndex()];
        if (hl.state != RPGALS_NOTUSED)
        {
            // recalc armor layer
            float fpAbsorbtions[RPG_NUM_ATTACK_TYPES];
            lpArmor->GetAbsorbtions(fpAbsorbtions, int(j != 0));
            for (k = 0; k < RPG_NUM_ATTACK_TYPES; k++)
                al.fpAbsorbtions[k] += fpAbsorbtions[k];
            al.dwCrippleMask |= lpArmor->GetCrippleMask(int(j != 0));
            al.state = RPGALS_ACTIVE;
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void SRPGUnitStats::Remove(LPGAMEBASE lpItemArmor)
{
    if (!lpItemArmor)
        return;
    CItemArmor *lpArmor = (CItemArmor *)lpItemArmor;
    int j, k;

    for (j = 0; j < 3; j++)
    {
        if (armorHLs[lpArmor->GetArmorType()][j] == RPGHL_UNKNOWN)
            continue;
        SRPGHitLocation &hl = hls[armorHLs[lpArmor->GetArmorType()][j]];
        SRPGArmorLayer &al = hls[armorHLs[lpArmor->GetArmorType()][j]]
                                 .layers[lpArmor->GetALIndex()];
        if (hl.state != RPGALS_NOTUSED)
        {
            // recalc armor layer
            float fpAbsorbtions[RPG_NUM_ATTACK_TYPES];
            lpArmor->GetAbsorbtions(fpAbsorbtions, 0);
            for (k = 0; k < RPG_NUM_ATTACK_TYPES; k++)
                al.fpAbsorbtions[k] -= fpAbsorbtions[k];
            al.dwCrippleMask &= ~(lpArmor->GetCrippleMask(0));
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void SRPGUnitStats::RemoveAllArmors()
{
    for (int i = 0; i < RPG_NUM_HIT_LOCATIONS; ++i)
    {
        // layer 2
        hls[i].layers[2].dwCrippleMask = 0;
        memset(hls[i].layers[2].fpAbsorbtions, 0, sizeof(hls[i].layers[2].fpAbsorbtions));
        memset(hls[i].layers[2].fpResistances, 0, sizeof(hls[i].layers[2].fpResistances));
        // layer 1
        hls[i].layers[1].dwCrippleMask = 0;
        memset(hls[i].layers[1].fpAbsorbtions, 0, sizeof(hls[i].layers[1].fpAbsorbtions));
        memset(hls[i].layers[1].fpResistances, 0, sizeof(hls[i].layers[1].fpResistances));
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void SRPGUnitStats::RecalcHitLocations(CUnit *pUnit)
{
    int i, j;
    // remove all data from armored hit locations
    for (i = 0; i < RPG_NUM_HIT_LOCATIONS; ++i)
    {
        for (j = 1; j < RPG_NUM_ARMOR_LAYERS; ++j) // skip skin - begin from '1' armor layer
            memset(&(hls[i].layers[j]), 0, sizeof(hls[i].layers[j]));
    }
    // wear all armors again - don't wear broken armors
    for (i = 0; i < RPG_NUM_ARMOR_TYPES; ++i)
    {
        CItemArmor *pArmor = pUnit->GetQArmor(i);
        if (pArmor && !pArmor->IsBroken())
            Wear(pArmor);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// common stats clearing
void SRPGCommonStats::Clear()
{
    memset(&ssHP, 0, sizeof(ssHP));
    memset(&ssMP, 0, sizeof(ssMP));
    nRegenerateCounter = 0;
    for (int i = 0; i < RPG_NUM_ATTACK_TYPES; ++i)
        psAbsorbtions[i].fpBaseValue = psAbsorbtions[i].fpEffectiveValue = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
// user common stats clearing
void SRPGUserCommonStats::Clear()
{
    cKnowLevel = KNOW_NONE;
    nHP = nMaxHP = 0;
    wMP = wMaxMP = 0;
    memset(wAbsorbtions, 0, sizeof(wAbsorbtions));
}
/////////////////////////////////////////////////////////////////////////////////////
// unit stats clearing
void SRPGUnitStats::Clear()
{
    fpExperience = 0;
    fExpPenalty = 0;
    fSpentExp = 0;
    memset(&psStrength, 0, sizeof(psStrength));
    memset(&psDexterity, 0, sizeof(psDexterity));
    memset(&psIntelligence, 0, sizeof(psIntelligence));
    memset(&move, 0, sizeof(move));
    memset(&action, 0, sizeof(action));
    fpMaxEncumbrance = 0;
    // native attack weapon params
    fpNAWRange = 0;
    fpNAWToHit = 0;
    fpNAWToHitRandom = 1;
    fpNAWParry = 0;
    fpNAWWeight = 0;
    fpNAWDamageMin = 0;
    fpNAWDamageRnd = 0;
    memset(fpNAWAttacks, 0, sizeof(fpNAWAttacks));
    rpgNAWWeaponType = RPGWST_HANDS;
    //
    memset(senses, 0, sizeof(senses));
    memset(percepts, 0, sizeof(percepts));
    for (DWORD i = 0; i < RPG_NUM_HIT_LOCATIONS; i++)
        hls[i].Clear();
    // skills
    memset(cManualSkills, 0, sizeof(cManualSkills));
    memset(cAutoSkills, 0, sizeof(cAutoSkills));
    memset(cPerks, 0, sizeof(cPerks));
    bFlags = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
// user unit common stats clearing
void SRPGUserUnitStats::Clear()
{
    memset(this, 0, sizeof(*this)); // shit happens
    cKnowLevel = KNOW_NONE;
    // exp/char pts
    dwExperience = 0;
    // primary attributes
    cStrength = 0;
    cDexterity = 0;
    cIntelligence = 0;
    // secondary attributes
    cMove = 0;
    cAction = 0;
    wEncumbrance = 0;
    //
    wAttackerPrototype = RPGWST_UNKNOWN;
    //
    memset(cSenses, 0, sizeof(cSenses));
    memset(cPercepts, 0, sizeof(cPercepts));
    // hit locations
    for (DWORD i = 0; i < RPG_NUM_HIT_LOCATIONS; i++)
        hls[i].Clear();
    // skills
    memset(cManualSkills, 0, sizeof(cManualSkills));
    memset(cAutoSkills, 0, sizeof(cAutoSkills));
    memset(cPerks, 0, sizeof(cPerks));
    bFlags = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
// hit location clearing
void SRPGHitLocation::Clear()
{
    hlt = RPGHLT_UNKNOWN;
    dwCrippleMask = 0;
    state = RPGALS_NOTUSED;
    fpHP = fpMaxHP = 0;
    for (DWORD i = 0; i < RPG_NUM_ARMOR_LAYERS; i++)
        layers[i].Clear();
}
/////////////////////////////////////////////////////////////////////////////////////
// user hit location clearing
void SRPGUserHitLocation::Clear()
{
    hlt = RPGHLT_UNKNOWN;
    cCrippleMask = 0;
    memset(wAbsorbtions, 0, sizeof(wAbsorbtions));
    state = RPGALS_NOTUSED;
    wHP = wMaxHP = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
// void SRPGArmorLayer::Clear()
// armor layer clearing
void SRPGArmorLayer::Clear()
{
    dwCrippleMask = 0;
    MemSetDWord((DWORD *)fpAbsorbtions, 0, RPG_NUM_ATTACK_TYPES);
    state = RPGALS_NOTUSED;
    std::fill_n(lpItems, RPG_NUM_ARMORS_IN_HL, nullptr);
}
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
// **                   Client <=> Server synchronization                        ** //
// ******************************************************************************** //
// CRAP - store variables separately
void UpdateTo(SRPGUserCommonStats &stats, CUpdateHistory &pkt)
{
    pkt.Write(&stats, sizeof(stats));
}
/////////////////////////////////////////////////////////////////////////////////////
void UpdateFrom(SRPGUserCommonStats &stats, CUpdateHistory &pkt)
{
    pkt.Read(&stats, sizeof(stats));
}
/////////////////////////////////////////////////////////////////////////////////////
void UpdateTo(SRPGUserUnitStats &stats, CUpdateHistory &pkt)
{
    pkt.Write(&stats, sizeof(stats));
}
/////////////////////////////////////////////////////////////////////////////////////
void UpdateFrom(SRPGUserUnitStats &stats, CUpdateHistory &pkt)
{
    pkt.Read(&stats, sizeof(stats));
}
/////////////////////////////////////////////////////////////////////////////////////

/*static const WORD W_EXPERIENCE_MASK         = 0x0001;
static const WORD W_CHARACTER_POINTS_MASK   = 0x0002;
static const WORD W_PRIMARY_MASK            = 0x0004;
static const WORD W_HP_MASK                 = 0x0008;
static const WORD W_HPMAX_MASK              = 0x0010;
static const WORD W_MP_MASK                 = 0x0020;
static const WORD W_MPMAX_MASK              = 0x0040;
static const WORD W_HITLOCATIONS_HP_MASK    = 0x0080;
static const WORD W_HITLOCATIONS_STATE_MASK = 0x0100;
static const WORD W_HITLOCATIONS_ARMOR_MASK = 0x0200;
static const WORD W_MOVE_MASK               = 0x0400;
static const WORD W_ACTIONS_MASK            = 0x0800;
static const WORD W_ENCUMBRANCE_MASK        = 0x1000;
static const WORD W_RESISTANCES_MASK        = 0x2000;
static const WORD W_ARMOR_MASK              = 0x4000;
static const WORD W_SENSES_MASK             = 0x8000;
/////////////////////////////////////////////////////////////////////////////////////
// void UpdateTo( SRPGUserCommonStats* pSrvStats, SRPGUserCommonStats* pClStats, BYTE cKnowLevel,
LPVOID& pDstBuf )
// write to destination buffer difference between two user common stats
// store common stats only:
// 	1. HP/MaxHP, MP/MaxMP
// 	2. general absorbtions and resistances
void UpdateTo( SRPGUserCommonStats* pSrvStats, SRPGUserCommonStats* pClStats, BYTE cKnowLevel,
CPacket &pkt )
{
        // setup knowledge level
        pkt << cKnowLevel;
        if ( cKnowLevel == KNOW_NONE )
                return;                             // return, if don't know anything
        // Check for full update
        SRPGUserCommonStats invalidStats;
        if ( pClStats == NULL )
        {
                // This trick is used to avoid additional check in each pSrv<->pCl comparsion
                memset( &invalidStats, 0xFE, sizeof( invalidStats ) );
                pClStats = &invalidStats;
        }
        //
        WORD wMask = 0;
        LPVOID pDstOld = pDstBuf;             // pointer to store wMask here at the end of update
        pDstBuf = LPBYTE( pDstBuf ) + sizeof( WORD );
        // health/mana
        if ( cKnowLevel >= KNOW_HPMP )
        {
                // HP
                if ( pSrvStats->nHP != pClStats->nHP )
                {
                        wMask |= W_HP_MASK;
                        pkt << pSrvStats->nHP;
                }
                // max HP
                if ( pSrvStats->nMaxHP != pClStats->nMaxHP )
                {
                        wMask |= W_HPMAX_MASK;
                        pkt << pSrvStats->nMaxHP;
                }
                // MP
                if ( pSrvStats->wMP != pClStats->wMP )
                {
                        wMask |= W_MP_MASK;
                        pkt << pSrvStats->wMP;
                }
                // max MP
                if ( pSrvStats->wMaxMP != pClStats->wMaxMP )
                {
                        wMask |= W_MPMAX_MASK;
                        pkt << pSrvStats->wMaxMP;
                }
        }
        // update general resistances and absorbtions
        if ( cKnowLevel >= KNOW_RESISTANCES )
        {
                if (
                           memcmp( pSrvStats->wAbsorbtions, pClStats->wAbsorbtions,
sizeof(pSrvStats->wAbsorbtions) ) ||
                         )
                {
                        wMask |= W_RESISTANCES_MASK;
                        pkt.Write( &pSrvStats->wAbsorbtions, sizeof(pSrvStats->wAbsorbtions) );
                }
        }
        // store update mask
        *((WORD*)pDstOld) = wMask;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL UpdateFrom( SRPGUserCommonStats* pStats, CBufferManager* pBM )
// update user common stats from buffer manager
// 	1. HP/maxHP, MP/maxMP
// 	2. general absorbtions and resistances
BOOL UpdateFrom( SRPGUserCommonStats* pStats, CPacket &pkt )
{
        BYTE cKnowLevel;
        // get knowledge level
        pkt >> cKnowLevel;
        pStats->cKnowLevel = cKnowLevel;
        // check for complete unknown stats
        if ( cKnowLevel == KNOW_NONE )
                return pkt.IsOk();                        // return with TRUE, if don't know
anything
        // mask
        WORD wMask;
        pkt >> wMask;
        // get secondary state HP and MP
        if ( wMask & W_HP_MASK )              // HP
                pkt >> pStats->nHP;
        if ( wMask & W_HPMAX_MASK )           // max HP
                pkt >> pStats->nMaxHP;
        if ( wMask & W_MP_MASK )              // MP
                pkt >> pStats->nMP;
        if ( wMask & W_MPMAX_MASK )           // max MP
                pkt >> pStats->nMaxMP;
        // Get resistances and absorbtions
        if ( wMask & W_RESISTANCES_MASK )
        {
                pkt.Read( &pStats->wAbsorbtions, sizeof(pStats->wAbsorbtions) );
        }
        //
        return pkt.IsOk();
}
/////////////////////////////////////////////////////////////////////////////////////
// void UpdateTo( SRPGUserUnitStats* pSrvStats, SRPGUserUnitStats* pClStats, BYTE cKnowLevel,
LPVOID& pDstBuf )
// update differencies between two sets of user unit stats
// 	1. exp + char pts
// 	2. primary attributes
// 	3. secondary attributes
// 	4. senses and percepts
// 	5. hit locations: HP/maxHP, state, cripple mask, absorbtions and protections
void UpdateTo( SRPGUserUnitStats* pSrvStats, SRPGUserUnitStats* pClStats, BYTE cKnowLevel, CPacket
&pkt )
{
        DWORD i;
        //
        pkt << cKnowLevel;
        if ( cKnowLevel == KNOW_NONE )
                return;                             // return, if don't know anything
        // Check for full update
        SRPGUserUnitStats invalidStats;
        if ( pClStats == NULL )
        {
                // This trick is used to avoid additional check in each pSrv<->pCl comparsion
                memset( &invalidStats, 0x00, sizeof( invalidStats ) );
                pClStats = &invalidStats;
        }
        // setup pointers
        WORD wMask = 0;
        LPVOID pDstOld = pDstBuf;             // here 'wMask' will be stored
        pDstBuf = LPBYTE( pDstBuf ) + sizeof( WORD );
        // store expirience
        if (
                         ( cKnowLevel >= KNOW_EXPIRIENCE ) &&
                   ( pSrvStats->dwExperience != pClStats->dwExperience )
                 )
        {
                wMask |= W_EXPERIENCE_MASK;
                pkt << pSrvStats->dwExperience;
        }
        // store character points
        if (
                         ( cKnowLevel >= KNOW_CHAR_POINTS ) &&
                   ( pSrvStats->cCharacterPoints != pClStats->cCharacterPoints )
                 )
        {
                wMask |= W_CHARACTER_POINTS_MASK;
                pkt << pSrvStats->cCharacterPoints;
        }
        // primary attributes
        if (
                   ( cKnowLevel >= KNOW_PRIMARY ) &&
                         ( ( pSrvStats->cStrength != pClStats->cStrength )         ||
                           ( pSrvStats->cDexterity != pClStats->cDexterity )       ||
                           ( pSrvStats->cIntelligence != pClStats->cIntelligence ) ||
                           ( pSrvStats->cLocomotion != pClStats->cLocomotion ) )
                 )
        {
                wMask |= W_PRIMARY_MASK;
                pkt << pSrvStats->cStrength <<pSrvStats->cDexterity << pSrvStats->cIntelligence;
        }
        // secondary attributes: move points, action points and encumbrance
        if ( cKnowLevel >= KNOW_SECONDARY )
        {
                // move points
                if ( pSrvStats->cMove != pClStats->cMove )
                {
                        wMask |= W_MOVE_MASK;
                        pkt << pSrvStats->cMove;
                }
                // action points
                if ( pSrvStats->cAction != pClStats->cAction )
                {
                        wMask |= W_ACTIONS_MASK;
                        pkt << pSrvStats->cAction;
                }
                // encimbrance
                if ( pSrvStats->wEncumbrance != pClStats->wEncumbrance )
                {
                        wMask |= W_ENCUMBRANCE_MASK;
                        pkt << pSrvStats->wEncumbrance;
                }
        }
        // senses and percepts
        if (
                   ( cKnowLevel >= KNOW_SENSES ) &&
                         ( memcmp( pSrvStats->cSenses, pClStats->cSenses, sizeof(pSrvStats->cSenses)
) || memcmp( pSrvStats->cPercepts, pClStats->cPercepts, sizeof(pSrvStats->cPercepts) ) )
                 )
        {
                wMask |= W_SENSES_MASK;
                pkt.Write( &pSrvStats->cSenses, sizeof(pSrvStats->cSenses) );
                pkt.Write( &pSrvStats->cPercepts, sizeof(pSrvStats->cPercepts) );
        }
        // Update hit locations
        if ( cKnowLevel >= KNOW_HL )
        {
                // first, set update flags
                for ( i=0; i<RPG_NUM_HIT_LOCATIONS; i++ )
                {
                        if ( pSrvStats->hls[i].wHP != pClStats->hls[i].wHP )
                                wMask |= W_HITLOCATIONS_HP_MASK;  // HP update
                        if (
                                   ( pSrvStats->hls[i].wMaxHP != pClStats->hls[i].wMaxHP ) ||
                                   ( pSrvStats->hls[i].state != pClStats->hls[i].state ) ||
                                   ( pSrvStats->hls[i].cCrippleMask != pClStats->hls[i].cCrippleMask
)
                                 )
                                wMask |= W_HITLOCATIONS_STATE_MASK;  // state update: maxHP, state
and cripple mask if ( ( cKnowLevel >= KNOW_ARMORS ) && ( memcmp( pSrvStats->hls[i].wAbsorbtions,
pClStats->hls[i].wAbsorbtions, sizeof(pSrvStats->hls[i].wAbsorbtions) ) ||
                                 )
                                wMask |= W_HITLOCATIONS_ARMOR_MASK;  // absorbtions/resistances
update
                }
                // then, perform update in accordance with flags
                // HP
                if ( wMask & W_HITLOCATIONS_HP_MASK )
                {
                        for ( i=0; i<RPG_NUM_HIT_LOCATIONS; i++ )
                                pkt << pSrvStats->hls[i].wHP;
                }
                // state: state, maxHP, cripple mask
                if ( wMask & W_HITLOCATIONS_STATE_MASK )
                {
                        for ( i=0; i<RPG_NUM_HIT_LOCATIONS; i++ )
                        {
                                pkt << pSrvStats->hls[i].state;
                                pkt << pSrvStats->hls[i].wMaxHP;
                                pkt << pSrvStats->hls[i].cCrippleMask;
                        }
                }
                // armor characteristics: absorbtions and resistances
                if ( wMask & W_HITLOCATIONS_ARMOR_MASK )
                {
                        for ( i=0; i<RPG_NUM_HIT_LOCATIONS; i++ )
                        {
                                pkt.Write( &pSrvStats->hls[i].wAbsorbtions,
sizeof(pSrvStats->hls[i].wAbsorbtions) );
                        }
                }
        }
        // Update skills
        if ( cKnowLevel >= KNOW_SKILLS )
        {

        }
        // set valid mask
        *((WORD*)pDstOld) = wMask;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL UpdateFrom( SRPGUserUnitStats* pStats, CBufferManager* pBM )
// Update user unit stats from buffer manager
// 	1. exp + char pts
// 	2. primary attributes
// 	3. secondary attributes
// 	4. senses and percepts
// 	5. hit locations: HP/maxHP, state, cripple mask, absorbtions and protections
BOOL UpdateFrom( SRPGUserUnitStats* pStats, CPacket &pkt )
{
        BYTE cKnowLevel;
        DWORD i;
        //
        pkt >> cKnowLevel;
        pStats->cKnowLevel = cKnowLevel;
        if ( cKnowLevel == KNOW_NONE )
                return TRUE;                        // don't know anything - exit with TRUE
        // mask
        WORD wMask;
        pkt >> wMask;
        // get expirience
        if ( wMask & W_EXPERIENCE_MASK )
                pkt >> pStats->dwExperience;
        // get character points
        if ( wMask & W_CHARACTER_POINTS_MASK )
                pkt >> pStats->cCharacterPoints;
        // primary attibutes
        if ( wMask & W_PRIMARY_MASK )
        {
                pkt >> pStats->cStrength >> pStats->cDexterity >> pStats->cIntelligence;
        }
        // secondary attributes: move points, action points and encumbrance
        if ( cKnowLevel >= KNOW_SECONDARY )
        {
                // move points
                if ( wMask & W_MOVE_MASK )
                {
                        pkt >> pStats->cMove;
                }
                // action points
                if ( wMask & W_ACTIONS_MASK )
                        pkt >> pStats->cAction;
                // encimbrance
                if ( wMask & W_ENCUMBRANCE_MASK )
                        pkt >> pStats->wEncumbrance;
        }
        // senses and percepts
        if ( wMask & W_SENSES_MASK )
        {
                pkt.Read( &pStats->cSenses, sizeof(pStats->cSenses) );
                pkt.Read( &pStats->cPercepts, sizeof(pStats->cPercepts) );
        }
        // hit locations
        // HP
        if ( wMask & W_HITLOCATIONS_HP_MASK )
        {
                for ( i=0; i<RPG_NUM_HIT_LOCATIONS; i++ )
                        pkt >> pStats->hls[i].wHP;
        }
        // state: state, maxHP, cripple mask
        if ( wMask & W_HITLOCATIONS_STATE_MASK )
        {
                for ( i=0; i<RPG_NUM_HIT_LOCATIONS; i++ )
                {
                        pkt >> pStats->hls[i].state;
                        pkt >> pStats->hls[i].wMaxHP;
                        pkt >> pStats->hls[i].cCrippleMask;
                }
        }
        // armor characteristics: absorbtions and resistances
        if ( wMask & W_HITLOCATIONS_ARMOR_MASK )
        {
                for ( i=0; i<RPG_NUM_HIT_LOCATIONS; i++ )
                {
                        pkt.Read( &pStats->hls[i].wAbsorbtions, sizeof(pStats->hls[i].wAbsorbtions)
);
                }
        }
        // Get skills
        if ( cKnowLevel >= KNOW_SKILLS )
        {
        }
        return bRv;
}
*/
/////////////////////////////////////////////////////////////////////////////////////
bool SRPGCommonStats::Regenerate(SRPGUnitStats &rpgUnitStats, CUnit *pUnit, bool bHPOnly)
{
    if(auto*mod=ModRuntime::Current()){
        if(!ModEncounter::Arena(theWorldServer))return mod->Regenerate(*this,rpgUnitStats,pUnit,bHPOnly);
        ModArena::Regeneration observation(*this,rpgUnitStats,pUnit);
        bool changed=mod->Regenerate(*this,rpgUnitStats,pUnit,bHPOnly);
        changed=observation.Finish(*this,rpgUnitStats,pUnit)||changed;
        ModArena::Tick(pUnit);return changed;
    }
    ++nRegenerateCounter;
    if ((nRegenerateCounter < 15) && (nRegenerateCounter > 0))
        return false;
    nRegenerateCounter = 0;
    //
    bool bRetVal = false;
    if (!bHPOnly)
    {
        // calc mana regenerate rate with perks and regenerate mana
        if (ssMP.fpCurrent < ssMP.fpMax)
        {
            float fMPRegenerateRate = ssMP.fpRegenerate;
            fMPRegenerateRate += fMPRegenerateRate * rpgUnitStats.GetPerkModifier(RPGPRK_SPIRIT) *
                                 0.01f;
            RegenerateMP(fMPRegenerateRate);
            //
            bRetVal = true;
        }
    }
    // calc health regenerate rate with perks and regenerate health
    if (ssHP.fpCurrent < ssHP.fpMax)
    {
        float fHPRegenerateRate = ssHP.fpRegenerate;
        fHPRegenerateRate += fHPRegenerateRate * rpgUnitStats.GetPerkModifier(RPGPRK_VITALITY) *
                             0.01f;
        if (pUnit)
        {
            float fSpellRegeneration = pUnit->GetEffectValue(SPELL_ID_REGENERATION);
            if (fSpellRegeneration > 0)
                fHPRegenerateRate *= fSpellRegeneration;
        }
        rpgUnitStats.Heal(this, ssHP.fpMax * fHPRegenerateRate);
        //
        bRetVal = true;
    }
    // regenerate
    return bRetVal;
}
/////////////////////////////////////////////////////////////////////////////////////
void SRPGUnitStats::AddExperience(float fExp)
{
    fExpPenalty += fExp;
    if (fExpPenalty > 0)
    {
        fpExperience += fExpPenalty;
        fExpPenalty = 0;
    }
}
// gain experience and recalc it to character points.
// also, recalc all stats
double SRPGUnitStats::GainExperience(float fpGainedExperience, bool useIntelligence)
{
    const auto *mod = ModRuntime::Current();
    float limit = mod ? mod->ExperienceLimit() : -1.0f;
    if (limit >= 0)
    {
        const float multiplier = float(pow(1.04, psIntelligence.fpEffectiveValue - 25.0f));
        limit *= multiplier;
        if (useIntelligence)
            fpGainedExperience *= multiplier;
    }
    else if (useIntelligence)
        fpGainedExperience *= GetExpMultiplier();

    // Return the native increment before the cap so a deferred balance retains
    // rounding error, but never banks XP rejected by the campaign limit.
    const double before = double(fpExperience) + fExpPenalty;
    AddExperience(fpGainedExperience);
    const double increment = (double(fpExperience) + fExpPenalty) - before;
    if (limit >= 0)
        fpExperience = Min(fpExperience, limit);
    return increment;
}
/////////////////////////////////////////////////////////////////////////////////////
void SRPGUnitStats::GainExperienceInCombat(CUnit *pUnit, float fpGainedExperience)
{
    CMapObjectSpecific *pMOSpec = pUnit->GetMapObjectSpecific();
    if (pMOSpec->WhoAmI() != HPC_MAPOBJECTSERVERSPECIFIC)
        return;
    SRPGCommonStats *pCommonStats = static_cast<CMapObjectServer *>(pMOSpec)->GetRPGStats();
    float fPercentage = pCommonStats->ssHP.fpCurrent / pCommonStats->ssHP.fpMax;
    GainExperience(fpGainedExperience);
    // recalc HP and MP
    RedistributeHPMP(pCommonStats, pUnit);
}
/////////////////////////////////////////////////////////////////////////////////////
void SRPGUnitStats::AddExpPenalty(float fPenalty)
{
    fExpPenalty -= fPenalty;
}
/////////////////////////////////////////////////////////////////////////////////////
// resurrect requested hit location: set state as ACTIVE and HP as MaxHP
void SRPGUnitStats::Resurrect(ERPGHitLocations hlType)
{
    if (hls[hlType].state > RPGALS_NOTUSED)
    {
        hls[hlType].state = RPGALS_ACTIVE;
        hls[hlType].fpHP = hls[hlType].fpMaxHP;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// resurrect all hit locations
void SRPGUnitStats::ResurrectAll()
{
    for (int i = 0; i < RPG_NUM_HIT_LOCATIONS; ++i)
        Resurrect(static_cast<ERPGHitLocations>(i));
}
/////////////////////////////////////////////////////////////////////////////////////
float SRPGUnitStats::GetHealthMultiplier() const
{
    return psStrength.fpEffectiveValue / theAIGeneralSettings.fHPVal1;
}
/////////////////////////////////////////////////////////////////////////////////////
float SRPGUnitStats::GetStaminaMultiplier() const
{
    return psDexterity.fpEffectiveValue / theAIGeneralSettings.fMPVal1;
}
/////////////////////////////////////////////////////////////////////////////////////
float SRPGUnitStats::GetExpMultiplier() const { if(auto*mod=ModRuntime::Current())return mod->ExperienceMultiplier(*this);return float(pow(1.04,GetIntelligenceBonus())); }
float SRPGUserUnitStats::GetExpMultiplier() const { if(auto*mod=ModRuntime::Current())return mod->ExperienceMultiplier(*this);return float(pow(1.04,GetIntelligenceBonus())); }
float SRPGUnitStats::GetManaBonusMultiplier() const
{
    if(auto*mod=ModRuntime::Current())return mod->ManaBonus(*this);
    return 25.0f / psIntelligence.fpEffectiveValue;
}
float SRPGUnitStats::GetSpellAddonManaBonusMultiplier() const
{
    if(auto*mod=ModRuntime::Current())return mod->SpellAddonManaBonus(*this);
    return 25.0f / psIntelligence.fpEffectiveValue;
}
/////////////////////////////////////////////////////////////////////////////////////
float SRPGUnitStats::GetDamageModifier(const CUnit *pUnit) const
{
    if (pUnit)
    {
        float fStrengthen = pUnit->GetEffectValue(SPELL_ID_STRENGTHEN) * 0.01f * 0.3f;
        float fWeaken = pUnit->GetEffectValue(SPELL_ID_WEAKEN) * 0.01f * 0.3f;
        return (1 + fStrengthen) / (1 + fWeaken);
    }
    else
        return 1.0f;
}
/////////////////////////////////////////////////////////////////////////////////////
float SRPGUnitStats::GetModifiedDamage(const CUnit *pUnit, float fDamage) const
{
    if(pUnit)if(auto*mod=ModRuntime::Current())return mod->ModifiedDamage(*this,*pUnit,fDamage,float(ModNativeCrtRandom::Random(100)));
    return Max(1.0f, fDamage * GetDamageModifier(pUnit));
}
/////////////////////////////////////////////////////////////////////////////////////
float SRPGUnitStats::GetHealthModifier(const CUnit *pUnit) const
{
    if (pUnit)
    {
        float fStrengthen = pUnit->GetEffectValue(SPELL_ID_STRENGTHEN) * 0.01f * 0.5f;
        float fWeaken = pUnit->GetEffectValue(SPELL_ID_WEAKEN) * 0.01f * 0.5f;
        return (1 + fStrengthen) / (1 + fWeaken);
    }
    else
        return 1.0f;
}
/////////////////////////////////////////////////////////////////////////////////////
float SRPGUnitStats::GetModifiedHealth(const CUnit *pUnit, float fHealth)
{
    if (auto *mod=ModRuntime::Current()) return mod->GetModifiedHealth(*this,*pUnit,fHealth);
    return Max(1.0f, fHealth * GetHealthModifier(pUnit));
}
/////////////////////////////////////////////////////////////////////////////////////
float SRPGUnitStats::RecalcHP() const
{
    if (auto *mod = ModRuntime::Current()) return mod->CalcBaseHealth(*this);
    float fBaseHP = GetHealthMultiplier() * theAIGeneralSettings.fHPVal2 *
                    pow(theAIGeneralSettings.fHPVal4,
                        Log(fpExperience / theAIGeneralSettings.fHPVal3,
                            theAIGeneralSettings.fHPVal5));
    // add perk
    fBaseHP *= (1.0f + GetPerkModifier(RPGPRK_HEALTH) * 0.01f);

    return fBaseHP;
}
/////////////////////////////////////////////////////////////////////////////////////
float SRPGUnitStats::RecalcMP() const
{
    if (auto *mod = ModRuntime::Current()) return mod->CalcBaseMana(*this);
    float fBaseMP = GetStaminaMultiplier() * theAIGeneralSettings.fMPVal2 *
                    pow(theAIGeneralSettings.fMPVal4,
                        Log(fpExperience / theAIGeneralSettings.fMPVal3,
                            theAIGeneralSettings.fMPVal5));
    // add perk
    fBaseMP *= (1.0f + GetPerkModifier(RPGPRK_MANA) * 0.01f);

    return fBaseMP;
}
/////////////////////////////////////////////////////////////////////////////////////
float SRPGUnitStats::CollectMaxHPWithoutDeadHLs(float fMaxHP) const
{
    float fpHP = 0;
    //
    for (DWORD i = 0; i < RPG_NUM_HIT_LOCATIONS; i++)
    {
        if (hls[i].state == RPGALS_CRIPPLED) // collect HP all crippled HLs
            fpHP += hls[i].fpVitality * fMaxHP;
    }
    //
    return fMaxHP - fpHP;
}
/////////////////////////////////////////////////////////////////////////////////////
float SRPGUnitStats::RecalcMoveDamageModifier(bool bHero) const
{
    float fMinPercentage = 1;
    for (int i = 0; i < RPG_NUM_HIT_LOCATIONS; ++i)
    {
        if (hls[i].state == RPGALS_NOTUSED)
            continue;
        if ((hls[i].hlt == RPGHLT_LEG) && (hls[i].fpHP != hls[i].fpMaxHP))
            fMinPercentage = Min(fMinPercentage, hls[i].fpHP / hls[i].fpMaxHP);
    }
    //
    if (fMinPercentage <= theAIGeneralSettings.fDamageLevel2)
        return theAIGeneralSettings.fDamageLevel2Value;
    else if (fMinPercentage <= theAIGeneralSettings.fDamageLevel1)
        return theAIGeneralSettings.fDamageLevel1Value;
    else
        return 1.0f;
}
/////////////////////////////////////////////////////////////////////////////////////
float SRPGUnitStats::RecalcActionDamageModifier(bool bHero) const
{
    float fMinPercentage = 1;
    for (int i = 0; i < RPG_NUM_HIT_LOCATIONS; ++i)
    {
        if (hls[i].state == RPGALS_NOTUSED)
            continue;
        if ((hls[i].hlt == RPGHLT_HAND) && (hls[i].fpHP != hls[i].fpMaxHP))
            fMinPercentage = Min(fMinPercentage, hls[i].fpHP / hls[i].fpMaxHP);
    }
    //
    if (fMinPercentage <= theAIGeneralSettings.fDamageLevel2)
        return theAIGeneralSettings.fDamageLevel2Value;
    else if (fMinPercentage <= theAIGeneralSettings.fDamageLevel1)
        return theAIGeneralSettings.fDamageLevel1Value;
    else
        return 1.0f;
}
/////////////////////////////////////////////////////////////////////////////////////
ECanRun SRPGUnitStats::CanRun(bool bHero) const
{
    float fMinPercentage = 1;
    for (int i = 0; i < RPG_NUM_HIT_LOCATIONS; ++i)
    {
        if (hls[i].state == RPGALS_NOTUSED)
            continue;
        if ((hls[i].hlt == RPGHLT_LEG) && (hls[i].fpHP != hls[i].fpMaxHP))
            fMinPercentage = Min(fMinPercentage, hls[i].fpHP / hls[i].fpMaxHP);
    }
    //
    const CModPackage *mod = ModRuntime::Current();
    const bool skipWeight = mod && mod->SpellAddon().Feature17() == 1;
    if (bHero && !skipWeight && (fpCurrEncumbrance > fpMaxEncumbrance))
        return ECR_OVERLOADED;
    if (fMinPercentage < theAIGeneralSettings.fDamageLevelRunLimit)
        return ECR_INJURED;

    return ECR_CAN_RUN;
}
/////////////////////////////////////////////////////////////////////////////////////
float SRPGUnitStats::GetMaxEncumbrance() const
{
    float fMaxEncumbrance = psStrength.fpEffectiveValue * 12.0f;
    fMaxEncumbrance += fMaxEncumbrance * GetPerkModifier(RPGPRK_LIFT) * 0.01f;
    return fMaxEncumbrance;
}
/////////////////////////////////////////////////////////////////////////////////////
float SRPGUnitStats::GetActionPoints() const
{
    float fActions = 10.0f + psDexterity.fpEffectiveValue * 0.2f;
    return fActions * (1.0f + GetActionModifier()) * RecalcActionDamageModifier(true);
}
/////////////////////////////////////////////////////////////////////////////////////
void SRPGUnitStats::StrengthChanged(SRPGCommonStats *lpCommonStats, const CUnit *pUnit)
{
    bool bHero = IsIDofPersonage(pUnit->GetID());
    //
    if (bHero)
        lpCommonStats->ssHP.fpMax = RecalcHP();
    else
        lpCommonStats->ssHP.fpMax = fBaseHP;
    lpCommonStats->ssHP.fpMax = GetModifiedHealth(pUnit, lpCommonStats->ssHP.fpMax);
    DistributeHPProportional(lpCommonStats->ssHP.fpMax);
    lpCommonStats->ssHP.fpCurrent = CollectHP(lpCommonStats->ssHP.fpMax);
    // shrink can be applied only to alive monsters
    if (lpCommonStats->ssHP.fpCurrent > 0)
        bFlags |= RPGSTAT_SHRINK;
}
/////////////////////////////////////////////////////////////////////////////////////
void SRPGUnitStats::RecalcStats(SRPGCommonStats *lpCommonStats, CUnit *pUnit)
{
    if (auto *mod=ModRuntime::Current()) {
        if(!lpCommonStats || !pUnit || !pUnit->GetUnitSpecific() || !pUnit->GetUnitSpecific()->GetManager())throw std::runtime_error("Full recalc requires an owned native unit");
        bool refreshAI=mod->RecalcUnitStats(*this,*lpCommonStats,*pUnit);
        if(refreshAI) pUnit->RecalcAIParams();
        pUnit->SetUpdateMask(CMapObject::VLD_MASK_STATS);pUnit->SetUpdateMask(CMapObject::VLD_MASK_UNIT_STATS);
        return;
    }
    float fSpellSpeedAdd = 0;
    const CSpellEffectArray &effects = pUnit->GetSpellEffects();
    bool bHero = IsIDofPersonage(pUnit->GetID());
    int i;
    // primary stats
    psStrength.fpEffectiveValue = psStrength.fpBaseValue;
    psDexterity.fpEffectiveValue = psDexterity.fpBaseValue;
    psIntelligence.fpEffectiveValue = psIntelligence.fpBaseValue;
    // senses and percepts
    for (i = 0; i < RPG_NUM_SENSES; i++)
    {
        senses[i].fpEffectiveValue = senses[i].fpBaseValue;
        percepts[i].fpEffectiveValue = percepts[i].fpBaseValue;
    }
    // add perks
    senses[RPGS_INFRA].fpEffectiveValue += GetNightVisionModifier();
    psStrength.fpEffectiveValue += GetStrengthModifier();
    psDexterity.fpEffectiveValue += GetDexterityModifier();
    psIntelligence.fpEffectiveValue += GetIntelligenceModifier();
    /*
    // take in account auto skills
    float fSenseValue = float( cAutoSkills[RPGASKL_AWARENESS] ) * 0.01f;
    float fPerceptValue = float( cAutoSkills[RPGASKL_STEALTH] ) * 0.01f;
    for ( i=0; i<RPG_NUM_SENSES; i++ )
    {
            senses[i].fpEffectiveValue += senses[i].fpEffectiveValue * fSenseValue;
            percepts[i].fpEffectiveValue = Max( 0.0f, percepts[i].fpEffectiveValue * ( 1.0f - 0.5f *
    fPerceptValue ) );
    }
    */
    // add spells....
    for (i = 0; i < RPG_NUM_ATTACK_TYPES; ++i)
        lpCommonStats->psAbsorbtions[i].fpEffectiveValue = lpCommonStats->psAbsorbtions[i]
                                                               .fpBaseValue;
    float fSpellEffectModifiers[40];
    MemSetDWord(reinterpret_cast<DWORD *>(fSpellEffectModifiers), 0, 40);
    bool negativeMagic=ModRuntime::Current()&&ModRuntime::Current()->CombatSetting("AllowMagicNegative");
    // collect effects
    for (i = 0; i < effects.GetSize(); i++)
    {
        const CSpellEffect &effect = effects[i];
        int nEffectType = effect.GetEffectType();
        if (nEffectType < 40)
            fSpellEffectModifiers[nEffectType] = negativeMagic?ModSpells::StrongestSignedEffect(fSpellEffectModifiers[nEffectType],effect.GetPower()):Max(fSpellEffectModifiers[nEffectType],effect.GetPower());
    }
    // apply effects
    // SPELL_ID_PROT_FIRE :         // protection from fire
    lpCommonStats->psAbsorbtions[RPGAT_TERMICAL].fpEffectiveValue += negativeMagic?ModSpells::StrongestSignedEffect(fSpellEffectModifiers[SPELL_ID_PROT_FIRE],fSpellEffectModifiers[SPELL_ID_ANTIMAGIC]):Max(fSpellEffectModifiers[SPELL_ID_PROT_FIRE],fSpellEffectModifiers[SPELL_ID_ANTIMAGIC]);
    // SPELL_ID_PROT_ELECTRIC :     // protection from electricity
    lpCommonStats->psAbsorbtions[RPGAT_ELECTRICAL].fpEffectiveValue += Max(
        fSpellEffectModifiers[SPELL_ID_PROT_ELECTRIC], fSpellEffectModifiers[SPELL_ID_ANTIMAGIC]);
    // SPELL_ID_PROT_ACID :         // protection from acid (from WATER)
    lpCommonStats->psAbsorbtions[RPGAT_CHEMICAL].fpEffectiveValue += Max(
        fSpellEffectModifiers[SPELL_ID_PROT_ACID], fSpellEffectModifiers[SPELL_ID_ANTIMAGIC]);
    /*
            // SPELL_ID_STRENGTHEN :
            psStrength.fpEffectiveValue += fSpellEffectModifiers[SPELL_ID_STRENGTHEN];
            // SPELL_ID_WEAKEN :
            psStrength.fpEffectiveValue = Max( 0.0f, psStrength.fpEffectiveValue -
       fSpellEffectModifiers[SPELL_ID_WEAKEN] );
    */
    // SPELL_ID_EAGLE_SIGHT :
    senses[RPGS_SIGHT].fpEffectiveValue += fSpellEffectModifiers[SPELL_ID_EAGLE_SIGHT];
    // SPELL_ID_INFRAVISION :
    senses[RPGS_INFRA].fpEffectiveValue += fSpellEffectModifiers[SPELL_ID_INFRAVISION];
    // SPELL_ID_DETECT_LIFE :
    senses[RPGS_SENSELIFE].fpEffectiveValue += fSpellEffectModifiers[SPELL_ID_DETECT_LIFE];
    // SPELL_ID_INVISIBILITY :
    percepts[RPGS_SIGHT].fpEffectiveValue = Max(0.0f,
                                                percepts[RPGS_SIGHT].fpEffectiveValue -
                                                    fSpellEffectModifiers[SPELL_ID_INVISIBILITY] /
                                                        100.0f);
    // SPELL_ID_SILENCE :
    percepts[RPGS_HEARING].fpEffectiveValue = Max(0.0f,
                                                  percepts[RPGS_HEARING].fpEffectiveValue -
                                                      fSpellEffectModifiers[SPELL_ID_SILENCE]);
    // SPELL_ID_LICHDOM :
    percepts[RPGS_SENSELIFE].fpEffectiveValue = Max(0.0f,
                                                    percepts[RPGS_SENSELIFE].fpEffectiveValue -
                                                        fSpellEffectModifiers[SPELL_ID_LICHDOM]);
    // SPELL_ID_STENCH :
    percepts[RPGS_SMELL].fpEffectiveValue += fSpellEffectModifiers[SPELL_ID_STENCH];
    // SPELL_ID_SPEED :
    fSpellSpeedAdd = fSpellEffectModifiers[SPELL_ID_SPEED] - fSpellEffectModifiers[SPELL_ID_SLOW];
    //
    if (bHero)
    {
        // recalc HP
        lpCommonStats->ssHP.fpMax = RecalcHP();
        // recalc MP
        lpCommonStats->ssMP.fpMax = RecalcMP();
    }
    else
        lpCommonStats->ssHP.fpMax = fBaseHP;
    // modify with spells
    lpCommonStats->ssHP.fpMax = GetModifiedHealth(pUnit, lpCommonStats->ssHP.fpMax);
    // hit locations HP distribution
    DistributeHP(lpCommonStats->ssHP.fpMax);
    // set circular vision arc for Hero
    if (bHero)
        fpVisionArc = -2; // circular vision!
    // encumbrance
    // max encumbrance
    if (bHero)
        fpMaxEncumbrance = GetMaxEncumbrance();
    else
        fpMaxEncumbrance = 1000000;
    //
    float fpEncumbrance = 0, fpEncumbranceModifier = 1;
    if (bHero)
    {
        /*
        fpEncumbrance = pUnit->GetItemsWeight() / fpMaxEncumbrance;
        fpEncumbranceModifier = static_cast<float>( pow( 0.7, fpEncumbrance ) );
        */
        fpEncumbranceModifier = 1;
    }
    fpCurrEncumbrance = pUnit->GetItemsWeight();
    // damage modifiers: move and action
    // move modifier
    if (dwLocomotion == 1) // check this parameter only for units with locamotion '1' (walk)
        fpMoveDamageModifier = RecalcMoveDamageModifier(bHero);
    else // for units with another locomotion (fly, levitate) this parameter always '1.0f'
        fpMoveDamageModifier = 1;
    move.fpEffectiveValue = move.fpBaseValue * fpEncumbranceModifier * fpMoveDamageModifier;
    // action modifier
    fpActionDamageModifier = RecalcActionDamageModifier(bHero);
    if (bHero)
        action.fpBaseValue = 10.0f +
                             psDexterity.fpEffectiveValue * 0.2f; // action points for heroes
    else
        action.fpBaseValue = 15; // default value for monsters
    action.fpEffectiveValue = action.fpBaseValue * (1.0f + GetActionModifier()) *
                              fpActionDamageModifier;
    action.fpEffectiveValue = Max(1.0f, action.fpEffectiveValue + fSpellSpeedAdd);
    // collect HP data from all hit locations
    lpCommonStats->ssHP.fpCurrent = CollectHP(lpCommonStats->ssHP.fpMax);
}
/////////////////////////////////////////////////////////////////////////////////////
void SRPGUnitStats::RecalcInternalHeroStats(float fHeroItemsWeight)
{
    if (auto *mod = ModRuntime::Current()) { mod->RecalcInternalHeroStats(*this, fHeroItemsWeight); return; }
    int i;
    // primary stats
    psStrength.fpEffectiveValue = psStrength.fpBaseValue;
    psDexterity.fpEffectiveValue = psDexterity.fpBaseValue;
    psIntelligence.fpEffectiveValue = psIntelligence.fpBaseValue;
    // senses and percepts
    for (i = 0; i < RPG_NUM_SENSES; i++)
    {
        senses[i].fpEffectiveValue = senses[i].fpBaseValue;
        percepts[i].fpEffectiveValue = percepts[i].fpBaseValue;
    }
    // add perks
    senses[RPGS_INFRA].fpEffectiveValue += GetNightVisionModifier();
    psStrength.fpEffectiveValue += GetStrengthModifier();
    psDexterity.fpEffectiveValue += GetDexterityModifier();
    psIntelligence.fpEffectiveValue += GetIntelligenceModifier();
    /*
    // take in account auto skills
    float fSenseValue = float( cAutoSkills[RPGASKL_AWARENESS] ) * 0.01f;
    float fPerceptValue = float( cAutoSkills[RPGASKL_STEALTH] ) * 0.01f;
    for ( i=0; i<RPG_NUM_SENSES; i++ )
    {
            senses[i].fpEffectiveValue += senses[i].fpEffectiveValue * fSenseValue;
            percepts[i].fpEffectiveValue = Max( 0.0f, percepts[i].fpEffectiveValue * ( 1.0f - 0.5f *
    fPerceptValue ) );
    }
    */
    // set circular vision arc for Hero
    fpVisionArc = -2; // circular vision!
    // hit locations HP distribution
    DistributeHP(RecalcHP());
    // encumbrance
    fpCurrEncumbrance = fHeroItemsWeight;
    float fpEncumbrance = 0.0f;      // fHeroItemsWeight / ( 10.0f*psStrength.fpEffectiveValue );
    float fpEncumbranceModifier = 1; // static_cast<float>( pow( 0.7, fpEncumbrance ) );
    fpMaxEncumbrance = GetMaxEncumbrance();
    // damage modifiers: move and action
    // move modifier
    fpMoveDamageModifier = RecalcMoveDamageModifier(true);
    move.fpEffectiveValue = move.fpBaseValue * fpEncumbranceModifier * fpMoveDamageModifier;
    // action modifier
    fpActionDamageModifier = RecalcActionDamageModifier(true);
    action.fpBaseValue = 10.0f + psDexterity.fpEffectiveValue * 0.2f; // action points for heroes
    action.fpEffectiveValue = action.fpBaseValue * (1.0f + GetActionModifier()) *
                              fpActionDamageModifier;
    // action
    // collect HP data from all hit locations
    //	lpCommonStats->ssHP.fpCurrent = CollectHP( lpCommonStats->ssHP.fpMax );
}
/////////////////////////////////////////////////////////////////////////////////////
// нанести повреждения по вещи и послать нотификации в соответствующих случаях
static void DamageItem(CItem *pItem, CUnit *pUnit, float fDamage, DWORD dwCrippledNotify,
                       DWORD dwCriticalNotify)
{
    if ((pItem == 0) || (pUnit == 0) || (fDamage <= 0))
        return;

    float fOldHP = pItem->GetHP();
    float fCriticalHP = pItem->GetMaxHP() * theAIGeneralSettings.fItemDurabilityCritical;
    pItem->ApplyDamage(fDamage);
    if (pItem->IsBroken())
    {
        // notify about crippled item
        CUnitSpecific *pSpec = pUnit->GetUnitSpecific();
        if (pSpec->WhoAmI() == HPC_UNITSERVERSPECIFIC)
            static_cast<CUnitServer *>(pSpec)->InformFailure(dwCrippledNotify);
    }
    else if ((fOldHP >= fCriticalHP) && (pItem->GetHP() < fCriticalHP))
    {
        // notify about pre-crippled item
        CUnitSpecific *pSpec = pUnit->GetUnitSpecific();
        if (pSpec->WhoAmI() == HPC_UNITSERVERSPECIFIC)
            static_cast<CUnitServer *>(pSpec)->InformFailure(dwCriticalNotify);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// process damage from attack portion.
// return TRUE, if damaged
// FALSE, if all damage was absorbed/resisted
BOOL SRPGCommonStats::ProcessAttackPortion(SRPGAttackPortion *lpAttackPortion)
{
    for (DWORD j = 0; j < RPG_NUM_ATTACK_TYPES; j++)
    {
        float fpPassedDamage = lpAttackPortion->fpAttacks[j] * lpAttackPortion->fpDamage -
                               psAbsorbtions[j].fpEffectiveValue;
        if (fpPassedDamage > 0)
            ssHP.fpCurrent -= fpPassedDamage;
    }
    ssHP.fpCurrent = Max(-5.0f * ssHP.fpMax, ssHP.fpCurrent);

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL SRPGUnitStats::ProcessAttackPortionSingle(CUnit *pUnit, SRPGCommonStats *lpCommonStats,
                                               const SRPGAttackPortion *lpAttackPortion,
                                               CItem *pWeapon)
{
    bool bHero = IsIDofPersonage(pUnit->GetID());
    float fpPassedDamage = 0;
    int i, j;
    BOOL bDamaged = FALSE;
    // here we'll process single-hl damage
    SRPGHitLocation &hl = hls[lpAttackPortion->hl];

    if (hl.state < RPGALS_DEAD)
        return FALSE; // this HL already crippled or not used at all
    for (j = 0; j < RPG_NUM_ATTACK_TYPES; j++)
    {
        fpPassedDamage = lpAttackPortion->fpAttacks[j] * lpAttackPortion->fpDamage;
        // check for damage to be > 0
        if (fpPassedDamage <= 0)
            continue;
        // pass damage through all armor layers of this HL
        int nStartArmorLayer = bHero ? RPG_NUM_ARMOR_LAYERS - 1 : -1;
        for (i = nStartArmorLayer; i >= 0; i--)
        {
            if (hl.layers[i].state < RPGALS_DEAD)
                continue; // this armor layer crippled or unused
            // damage calculation
            fpPassedDamage = fpPassedDamage -
                             hl.layers[i].fpAbsorbtions[j] * lpAttackPortion->fpDamagePercentage;
            // durability check for armor, weared on this armor layer
            ApplyDamageToArmors(pUnit, lpAttackPortion->hl, i, Max(0.0f, fpPassedDamage));
            //
            if (fpPassedDamage <= 0)
                break;
        }
        // apply damage to weapon
        if (pWeapon)
        {
            float fItemDamage = lpAttackPortion->fpAttacks[j] * lpAttackPortion->fpDamage -
                                Max(0.0f, fpPassedDamage);
            DamageItem(pWeapon, pUnit, fItemDamage, ACK_HERO_NOTIFY_WEAPON_CRIPPLED,
                       ACK_HERO_NOTIFY_WEAPON_CRITICAL);
        }
        // check for damage to be > 0
        if (fpPassedDamage <= 0)
            continue;
        // process passed damage in this HL
        bDamaged = TRUE;
        if (lpAttackPortion->fBackStabMultiplier > 1) // apply backstab multiplier
            fpPassedDamage *= lpAttackPortion->fBackStabMultiplier;
        hl.fpHP -= fpPassedDamage;
        //
        if (!(ModRuntime::Current()&&ModRuntime::Current()->CombatSetting("DisableCrippledHL")) &&
            (((fpPassedDamage >= hl.fpMaxHP) && (hl.dwCrippleMask & (1 << j))) ||
            (hl.fpHP < -5.0f * hl.fpMaxHP)))
        {
            // notify about crippled legs/hands
            if (hl.state > RPGALS_CRIPPLED)
            {
                DWORD dwAckType = 0;
                if (hl.hlt == RPGHLT_HAND)
                    dwAckType = ACK_HERO_NOTIFY_ARM_CRIPPLED;
                else if (hl.hlt == RPGHLT_LEG)
                    dwAckType = ACK_HERO_NOTIFY_LEG_CRIPPLED;
                CUnitSpecific *pSpec = pUnit->GetUnitSpecific();
                if ((dwAckType != 0) && (pSpec->WhoAmI() == HPC_UNITSERVERSPECIFIC))
                    static_cast<CUnitServer *>(pSpec)->InformFailure(dwAckType);
            }
            hl.state = RPGALS_CRIPPLED; // crippled
            hl.fpHP = -5.0f * hl.fpMaxHP;
        }
        else if (hl.fpHP <= 0)
            hl.state = RPGALS_DEAD; // only dead (can be crippled)
        // report log
        switch (j)
        {
        case RPGAT_IMPALLING:
            rpgLog << "impaling";
            break;
        case RPGAT_SLASHING:
            rpgLog << "slashing";
            break;
        case RPGAT_CRUSHING:
            rpgLog << "crushing";
            break;
        case RPGAT_TERMICAL:
            rpgLog << "termical";
            break;
        case RPGAT_CHEMICAL:
            rpgLog << "chemical";
            break;
        case RPGAT_ELECTRICAL:
            rpgLog << "electrical";
            break;
        case RPGAT_GENERAL:
            rpgLog << "general";
            break;
        default:
            rpgLog << "mistake";
            break;
        }
        rpgLog << " passed damage = " << fpPassedDamage;

        if (hl.state == RPGALS_CRIPPLED)
            rpgLog << " HL was crippled";
        else if (hl.state == RPGALS_DEAD)
            rpgLog << " HL is dead";
        rpgLog << std::endl;
    }
    if (lpAttackPortion->fBackStabMultiplier != 1)
        bFlags |= RPGSTAT_BACKSTAB;
    // collect HP from all hit locations
    lpCommonStats->ssHP.fpCurrent = CollectHP(lpCommonStats->ssHP.fpMax);

    return bDamaged;
}
/////////////////////////////////////////////////////////////////////////////////////
// for desired hit location pass damage though all active armor layers from top to bottom
// for each armor layer:
// 		1. durability check for weared armor
// 		2. fpPassedDamage = fpPassedDamage - fpAbsorbtions[type]
// after armor layers pass, apply passed damage to hit location's HP.
// check for crippling, dead, etc.
// collect total HP (soul) from all hit location's stats
#include "ModCombat.inl"
BOOL SRPGUnitStats::ProcessAttackPortion(CUnit *pUnit, SRPGCommonStats *lpCommonStats,
                                         const SRPGAttackPortion *lpAttackPortion, CItem *pWeapon)
{
    // check for correct hit location
    if (lpAttackPortion->hl >= RPGHL_UNKNOWN)
        return FALSE;
    if ((lpAttackPortion->hl == RPGHL_HEAD) && (lpAttackPortion->hl != RPGHL_ALL))
        bFlags |= RPGSTAT_CRITICAL;
    if (lpAttackPortion->fpAttacks[RPGAT_ELECTRICAL])
        bFlags |= RPGSTAT_ELECTRIC;
    //
    SRPGAttackPortion ap = *lpAttackPortion;
    float fpPassedDamage = 0;
    float fpTotalDamage = 0;
    int i;
    // first of all, pass through general absorbtions/resistances
    if (ap.fpDamage == 0)
        return FALSE;
    const float fAbsorptionMiltiplier = pUnit->IsHero()
                                            ? 1.0f
                                            : theAIGeneralSettings
                                                  .fAbsorptionLevels[theSettings.nDifficultyLevel];
    for (i = 0; i < RPG_NUM_ATTACK_TYPES; i++)
    {
        fpPassedDamage = Max(0.0f, ap.fpDamage * ap.fpAttacks[i] -
                                       lpCommonStats->psAbsorbtions[i].fpEffectiveValue *
                                           ap.fpDamagePercentage * fAbsorptionMiltiplier);
        fpTotalDamage += fpPassedDamage;
        ap.fpAttacks[i] = fpPassedDamage / ap.fpDamage;
        // apply damage to weapon
        if (pWeapon)
        {
            float fItemDamage = lpAttackPortion->fpAttacks[i] * lpAttackPortion->fpDamage -
                                Max(0.0f, fpPassedDamage);
            DamageItem(pWeapon, pUnit, fItemDamage, ACK_HERO_NOTIFY_WEAPON_CRIPPLED,
                       ACK_HERO_NOTIFY_WEAPON_CRITICAL);
        }
    }
    if (fpTotalDamage <= 0)
        return FALSE;
    // if damage on the all hit locations, process it as special case
    BOOL bCheck = FALSE;
    if (ap.hl == RPGHL_ALL)
    {
        // calculate percentage to remove from each HL
        float fVitality = 0;
        for (i = 0; i < RPG_NUM_HIT_LOCATIONS; i++)
        {
            if (hls[i].state <= RPGALS_CRIPPLED)
                continue;
            else
                fVitality += hls[i].fpVitality;
        }
        float fpDamage = ap.fpDamage / (lpCommonStats->ssHP.fpMax * fVitality);
        //
        for (i = 0; i < RPG_NUM_HIT_LOCATIONS; i++)
        {
            ap.fpDamagePercentage = lpAttackPortion->fpDamagePercentage * hls[i].fpMaxHP *
                                    fpDamage / lpAttackPortion->fpDamage;
            ap.hl = ERPGHitLocations(i);
            ap.fpDamage = hls[i].fpMaxHP * fpDamage;
            bCheck = ProcessAttackPortionSingle(pUnit, lpCommonStats, &ap, pWeapon) || bCheck;
        }
    }
    else
        bCheck = ProcessAttackPortionSingle(pUnit, lpCommonStats, &ap, pWeapon);
    //
    pUnit->RemoveCrippledItems();
    //
    return bCheck;
}
/////////////////////////////////////////////////////////////////////////////////////
// apply damage to armor - durability recalculation
void SRPGUnitStats::ApplyDamageToArmors(CUnit *pUnit, ERPGHitLocations hlType, int nArmorLayerIndex,
                                        float fDamage)
{
    if (!IsIDofPersonage(pUnit->GetID()))
        return; // apply armor damage only for heroes
    if (nArmorLayerIndex == 0)
        return;
    //
    --nArmorLayerIndex;
    //
    switch (hlType)
    {
    case RPGHL_HEAD: // {skin - skip it}, {none}, {helm}
    case RPGHL_BODY: // {skin - skip it}, {shirt}, {plate}
        if (hlArmors[hlType][nArmorLayerIndex][0] != RPGAST_UNKNOWN)
        {
            CItemArmor *pArmor = pUnit->GetQArmor(hlArmors[hlType][nArmorLayerIndex][0]);
            DamageItem(pArmor, pUnit, fDamage, ACK_HERO_NOTIFY_ARMOR_CRIPPLED,
                       ACK_HERO_NOTIFY_ARMOR_CRITICAL);
        }
        break;
    case RPGHL_LHAND: // {skin - skip it}, {shirt, gloves}, {plate}
    case RPGHL_RHAND:
    case RPGHL_LLEG: // {skin - skip it}, {pants, boots}, {leggings}
    case RPGHL_RLEG:
        if (nArmorLayerIndex == 1)
        {
            CItemArmor *pArmor = pUnit->GetQArmor(hlArmors[hlType][nArmorLayerIndex][0]);
            DamageItem(pArmor, pUnit, fDamage, ACK_HERO_NOTIFY_ARMOR_CRIPPLED,
                       ACK_HERO_NOTIFY_ARMOR_CRITICAL);
        }
        else // nArmorLayerIndex == 0
        {
            CItemArmor *pArmor = pUnit->GetQArmor(hlArmors[hlType][nArmorLayerIndex][0]);
            DamageItem(pArmor, pUnit, fDamage, ACK_HERO_NOTIFY_ARMOR_CRIPPLED,
                       ACK_HERO_NOTIFY_ARMOR_CRITICAL);
            pArmor = pUnit->GetQArmor(hlArmors[hlType][nArmorLayerIndex][1]);
            DamageItem(pArmor, pUnit, fDamage, ACK_HERO_NOTIFY_ARMOR_CRIPPLED,
                       ACK_HERO_NOTIFY_ARMOR_CRITICAL);
        }
        break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
bool SRPGUnitStats::Heal(float fMaxHP, float fHeal)
{
    int i;
    if (fabs(fHeal) <= 1e-3)
        return true;
    // calculate percentage to heal in each damaged HL
    float fVitality = 0;
    for (i = 0; i < RPG_NUM_HIT_LOCATIONS; i++)
    {
        if ((hls[i].state <= RPGALS_CRIPPLED) || (hls[i].fpHP >= hls[i].fpMaxHP))
            continue;
        else
            fVitality += hls[i].fpVitality;
    }
    if (fVitality == 0)
        return true;
    float fDamage = fHeal / (fMaxHP * fVitality);
    // and now in 'fpHealth' we have number of percents to heal in each HL
    for (i = 0; i < RPG_NUM_HIT_LOCATIONS; i++)
    {
        if ((hls[i].state <= RPGALS_CRIPPLED) || (hls[i].fpHP >= hls[i].fpMaxHP))
            continue;
        hls[i].fpHP = Min(hls[i].fpHP + fDamage * hls[i].fpMaxHP, hls[i].fpMaxHP);
        if (hls[i].fpHP >= 1e-3f) // restore dead hit location after heal
            hls[i].state = RPGALS_ACTIVE;
    }
    return true;
}
// heal unit on the 'fpHeal' HPs
BOOL SRPGUnitStats::Heal(SRPGCommonStats *lpCommonStats, float fpHeal)
{
    Heal(lpCommonStats->ssHP.fpMax, fpHeal);
    // collect HP from all hit locations
    lpCommonStats->ssHP.fpCurrent = CollectHP(lpCommonStats->ssHP.fpMax);

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL SRPGUnitStats::RegenerateHP( SRPGCommonStats* lpCommonStats )
// regenerate health
BOOL SRPGUnitStats::RegenerateHP(SRPGCommonStats *lpCommonStats)
{
    for (DWORD i = 0; i < RPG_NUM_HIT_LOCATIONS; i++)
    {
        if (hls[i].state == RPGALS_ACTIVE) // can regenerate only ACTIVE hls
            hls[i].fpHP = Min(hls[i].fpMaxHP,
                              hls[i].fpHP + lpCommonStats->ssHP.fpRegenerate * hls[i].fpMaxHP);
    }

    lpCommonStats->ssHP.fpCurrent = CollectHP(lpCommonStats->ssHP.fpMax);

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// float SRPGUnitStats::CollectHP()
// collect HP from all hit locations (with damage multiplier)
float SRPGUnitStats::CollectHP(float fpSoulMaxHP) const
{
    float fpHP = 0;
    //
    for (DWORD i = 0; i < RPG_NUM_HIT_LOCATIONS; i++)
    {
        if (hls[i].state >= RPGALS_CRIPPLED) // collect HP from all HL but unused
        {
            if (hls[i].fpHP >= 0)
                fpHP += ((hls[i].fpMaxHP - hls[i].fpHP) / hls[i].fpMaxHP) * hls[i].fpVitality *
                        fpSoulMaxHP;
            else
                fpHP += hls[i].fpVitality * fpSoulMaxHP;
        }
    }
    //
    return fpSoulMaxHP - fpHP;
}
/////////////////////////////////////////////////////////////////////////////////////
// distribute HP from soul to each (active) hit location
void SRPGUnitStats::FixHls(CUnit* unit, float maxHp)
{
    (void)maxHp; // Native FixHls requires this argument but does not use it.
    if (hls[0].fpHPWeight <= 0 || hls[1].fpHPWeight <= 0 || hls[2].fpHPWeight <= 0 ||
        hls[0].fpHPWeight <= -100000.0f || hls[1].fpHPWeight <= -100000.0f ||
        hls[2].fpHPWeight <= -100000.0f || hls[3].fpHPWeight <= -100000.0f ||
        hls[4].fpHPWeight <= -100000.0f || hls[5].fpHPWeight <= -100000.0f)
    {
        for (DWORD i = 0; i < RPG_NUM_HIT_LOCATIONS; ++i)
        {
            if (hls[i].state >= RPGALS_DEAD)
            {
                hls[i].dwCrippleMask = 0;
                hls[i].state = RPGALS_ACTIVE;
                hls[i].fpHP = 1000;
                hls[i].fpMaxHP = 1000;
                hls[i].fpHP = hls[i].fpMaxHP;
                const SUDBUnitPrototype* prototype = unit->GetPrototype();
                if (!prototype)
                    break;
                const SUDBRaceModel* race = theUnitDatabase.races[prototype->dwRaceIndex];
                if (!race)
                    throw std::runtime_error("FixHls race record unavailable");
                const SUDBHitLocationType* type = theUnitDatabase.hlts[race->hls[i].dwHLTypeIndex];
                if (!type)
                    throw std::runtime_error("FixHls hit-location type unavailable");
                hls[i].hlt = type->hlt;
                hls[i].fpHPWeight = race->hls[i].fpWeight;
                hls[i].fpVitality = race->hls[i].fpVitality;
                hls[i].dwCrippleMask = type->dwCrippleMask;
                hls[i].layers[0].dwCrippleMask = 0;
                hls[i].layers[0].state = RPGALS_NOTUSED;
                hls[i].layers[1].state = RPGALS_NOTUSED;
                hls[i].layers[2].state = RPGALS_NOTUSED;
                hls[i].state = RPGALS_ACTIVE;
            }
        }
        for (DWORD i = 0; i < RPG_NUM_HIT_LOCATIONS; ++i)
        {
            float weightHP = hls[i].fpMaxHP / hls[RPGHL_BODY].fpHPWeight;
            if (hls[i].state > RPGALS_NOTUSED)
            {
                hls[i].fpMaxHP = weightHP * hls[i].fpHPWeight;
                if (hls[i].fpMaxHP != 0)
                    hls[i].fpHP = hls[i].fpHP < hls[i].fpMaxHP ? hls[i].fpHP : hls[i].fpMaxHP;
                else
                    hls[i].fpHP = hls[i].fpMaxHP;
            }
        }
    }
}
void SRPGUnitStats::DistributeHP(float fpSoulMaxHP)
{
    float fWeightHP = fpSoulMaxHP / hls[RPGHL_BODY].fpHPWeight;
    for (DWORD i = 0; i < RPG_NUM_HIT_LOCATIONS; i++)
    {
        if (hls[i].state > RPGALS_NOTUSED) // distribute HP to all used hit locations
        {
            hls[i].fpMaxHP = fWeightHP * hls[i].fpHPWeight;
            hls[i].fpHP = Min(hls[i].fpHP, hls[i].fpMaxHP);
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// distribute HP from soul to each (active) hit location and change current HP proportional with
// distribution
void SRPGUnitStats::DistributeHPProportional(float fpSoulMaxHP)
{
    float fWeightHP = fpSoulMaxHP / hls[RPGHL_BODY].fpHPWeight;
    for (DWORD i = 0; i < RPG_NUM_HIT_LOCATIONS; i++)
    {
        if (hls[i].state > RPGALS_NOTUSED) // distribute HP to all used hit locations
        {
            float fPercentage = fWeightHP * hls[i].fpHPWeight / hls[i].fpMaxHP;
            hls[i].fpMaxHP = fWeightHP * hls[i].fpHPWeight;
            hls[i].fpHP = Min(hls[i].fpHP * fPercentage, hls[i].fpMaxHP);
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// heal all dead and active hit locations
void SRPGUnitStats::HealAll()
{
    for (DWORD i = 0; i < RPG_NUM_HIT_LOCATIONS; i++)
    {
        if (hls[i].state >= RPGALS_DEAD) // distribute HP only to active or dead hit locations
            hls[i].fpHP = hls[i].fpMaxHP;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// return nearest active hit location
ERPGHitLocations SRPGUnitStats::GetNearestHitLocation(ERPGHitLocations hl) const
{
    if (hls[hl].state == RPGALS_ACTIVE) // exact hit location
        return hl;
    else // choose nearest match
    {
        switch (hl)
        {
        case RPGHL_HEAD:
            if (hls[RPGHL_BODY].state == RPGALS_ACTIVE)
                return RPGHL_BODY;
            else if (hls[RPGHL_LHAND].state == RPGALS_ACTIVE)
                return RPGHL_LHAND;
            else if (hls[RPGHL_RHAND].state == RPGALS_ACTIVE)
                return RPGHL_RHAND;
            break;
        case RPGHL_BODY:
            if (hls[RPGHL_HEAD].state == RPGALS_ACTIVE)
                return RPGHL_HEAD;
            else if (hls[RPGHL_LHAND].state == RPGALS_ACTIVE)
                return RPGHL_LHAND;
            else if (hls[RPGHL_RHAND].state == RPGALS_ACTIVE)
                return RPGHL_RHAND;
            else if (hls[RPGHL_LLEG].state == RPGALS_ACTIVE)
                return RPGHL_LLEG;
            else if (hls[RPGHL_RLEG].state == RPGALS_ACTIVE)
                return RPGHL_RLEG;
            break;
        case RPGHL_LHAND:
            if (hls[RPGHL_RHAND].state == RPGALS_ACTIVE)
                return RPGHL_RHAND;
            else if (hls[RPGHL_BODY].state == RPGALS_ACTIVE)
                return RPGHL_BODY;
            break;
        case RPGHL_RHAND:
            if (hls[RPGHL_LHAND].state == RPGALS_ACTIVE)
                return RPGHL_LHAND;
            else if (hls[RPGHL_BODY].state == RPGALS_ACTIVE)
                return RPGHL_BODY;
            break;
        case RPGHL_LLEG:
            if (hls[RPGHL_RLEG].state == RPGALS_ACTIVE)
                return RPGHL_RLEG;
            else if (hls[RPGHL_BODY].state == RPGALS_ACTIVE)
                return RPGHL_BODY;
            break;
        case RPGHL_RLEG:
            if (hls[RPGHL_LLEG].state == RPGALS_ACTIVE)
                return RPGHL_LLEG;
            else if (hls[RPGHL_BODY].state == RPGALS_ACTIVE)
                return RPGHL_BODY;
            break;
        }
        // can't find desired or nearest hit location - try to select any active
        int selectedHLs[RPG_NUM_HIT_LOCATIONS];
        int nHLCount = 0;
        for (int i = 0; i < RPG_NUM_HIT_LOCATIONS; ++i)
        {
            if (hls[i].state == RPGALS_ACTIVE)
                selectedHLs[nHLCount++] = i;
        }

        if (nHLCount > 0)
            return static_cast<ERPGHitLocations>(selectedHLs[random.Get(nHLCount - 1)]);
        else
            return RPGHL_BODY;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
float SRPGUnitStats::GetBaseBackstabDamageMultiplier() const
{
    float fBackStabMultiplier = theAIGeneralSettings.fpBackstabAdd;
    //	fBackStabMultiplier += theAIGeneralSettings.fpBackstabMult * Max( 0.0001f, GetAutoSkill(
    // RPGASKL_BACKSTAB ) );
    fBackStabMultiplier += GetPerkModifier(RPGPRK_BACKSTAB) * 0.01f;
    return fBackStabMultiplier;
}
float SRPGUnitStats::GetBackstabDamageMultiplier(CUnit *pUnit) const
{
    // отказались от backstab как skill и поэтому items больше не влияют на него
    return GetBaseBackstabDamageMultiplier();
}
/////////////////////////////////////////////////////////////////////////////////////
float SRPGUnitStats::GetBaseManualSkill(ERPGManualSkills skill) const
{
    return cManualSkills[skill];
}
/////////////////////////////////////////////////////////////////////////////////////
float SRPGUnitStats::GetManualSkill(ERPGManualSkills skill) const
{
    switch (skill)
    {
    case RPGMSKL_SCIENCE:
        return cManualSkills[RPGMSKL_SCIENCE] + GetDexterityBonus();
    case RPGMSKL_STEALING:
        return cManualSkills[RPGMSKL_STEALING] + GetDexterityBonus();
    case RPGMSKL_TAME:
        return cManualSkills[RPGMSKL_TAME] + GetIntelligenceBonus();
    default:
        ASSERT(0 && "Unsupported manual skill");
    }
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
float SRPGUnitStats::GetManualSkillLevel(ERPGManualSkills skill, CUnit *pUnit) const
{
    // skill for non-personage
    if (!IsIDofPersonage(pUnit->GetID()))
        return cManualSkills[skill];
    // calc skill for personage
    float fItemSkillModifier = 0;
    for (int i = 0; i < QI_MAX_ITEMS; ++i)
    {
        const CItemQuick *pItem = pUnit->GetQItem(i);
        if (pItem)
            fItemSkillModifier = Max(fItemSkillModifier, float(pItem->GetSkillModifier(skill)));
    }
    //
    return GetManualSkill(skill) + fItemSkillModifier;
}
/////////////////////////////////////////////////////////////////////////////////////
float SRPGUnitStats::GetAutoSkillLevel(ERPGAutoSkills skill, CUnit *pUnit) const
{
    if (skill >= RPGASKL_FORCE_DWORD)
        ASSERT(0 && "Unsupported auto skill");
    // skill for non-personage
    if (!IsIDofPersonage(pUnit->GetID()))
        return cAutoSkills[skill];
    // calc skill for personage
    const CItemWeapon *pWeapon = pUnit->GetCurrentQWeapon();
    float fWeaponSkillModifier = 0;
    if (pWeapon)
        fWeaponSkillModifier = pWeapon->GetSkillModifier(skill);

    return GetAutoSkill(skill) + fWeaponSkillModifier;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL SRPGUnitStats::CanAddPerk(const CPerkDatabase *pPDB, const SPDBPerk *pPerk) const
{
    auto* mod=ModRuntime::Current();
    const bool native=mod && mod->CombatSetting("JMVPerks");
    if(!pPDB || !pPerk)return FALSE;
    if (!pPerk->bActive)
        return FALSE;
    // check primary attributes
    if ((psStrength.fpEffectiveValue < pPerk->fpAttributes[0]) ||
        (psDexterity.fpEffectiveValue < pPerk->fpAttributes[1]) ||
        (psIntelligence.fpEffectiveValue < pPerk->fpAttributes[2]))
        return FALSE;
    // check required skill level
    if (!pPerk->szSkillRequired.IsEmpty() && pPerk->szSkillRequired != "none")
    {
        if (!ModProgression::Index(int(pPerk->rpgAutoSkill),int(RPG_NUM_ACTUAL_AUTO_SKILLS)))
            return FALSE;
        if (cAutoSkills[pPerk->rpgAutoSkill] < pPerk->dwSkillLevel)
            return FALSE;
    }
    // check required perk
    if (!pPerk->szPerkRequired.IsEmpty() && pPerk->szPerkRequired != "none")
    {
        int nIndex = pPDB->GetPerkIndex(pPerk->szPerkRequired);
        if(!ModProgression::Index(nIndex,int(RPG_NUM_PERKS)*3))return FALSE;
        int nBaseIndex = nIndex / 3;
        int nFractIndex = nIndex % 3;
        if (native ? !ModProgression::Minimum(*this,nIndex) : cPerks[nBaseIndex] != nFractIndex + 1)
            return FALSE;
    }
    // check exclusive perk
    {
        int nIndex = pPDB->GetPerkIndex(pPerk);
        if(!ModProgression::Index(nIndex,int(RPG_NUM_PERKS)*3))return FALSE;
        int nBaseIndex = nIndex / 3;
        for (int i = 0; i < (native?35:int(RPGPRK_LASTPERK)); ++i)
        {
            const SPDBPerk *pPerk2 = GetPerk(i);
            if (pPerk2 != 0)
            {
                if ((pPerk2->nPerkExclusive == pPerk->nPerkExclusive) &&
                    (nBaseIndex != pPDB->GetPerkIndex(pPerk2) / 3))
                    return FALSE;
            }
        }
    }
    // check for such (or greater) perk exist
    int nIndex = pPDB->GetPerkIndex(pPerk->szCode);
    if(!ModProgression::Index(nIndex,int(RPG_NUM_PERKS)*3))return FALSE;
    int nBaseIndex = nIndex / 3;
    int nFractIndex = nIndex % 3;
    if (nFractIndex <= (cPerks[nBaseIndex] - 1))
        return FALSE;
    //
    if(native)return ModProgression::Additional(*this,
        [&](int field){return mod->ProgressionRequirementCode(pPerk->szCode,field);},
        [&](const char* code){return pPDB->GetPerkIndex(code);});
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL SRPGUnitStats::CanAddPerkLater(const CPerkDatabase *pPDB, const SPDBPerk *pPerk,
                                    bool bCheckExist) const
{
    if (!pPerk->bActive)
        return FALSE;
    // check primary attributes
    if ((psStrength.fpEffectiveValue < pPerk->fpAttributes[0]) ||
        (psDexterity.fpEffectiveValue < pPerk->fpAttributes[1]) ||
        (psIntelligence.fpEffectiveValue < pPerk->fpAttributes[2]))
        return FALSE;
    // check required perk
    if (!pPerk->szPerkRequired.IsEmpty() && pPerk->szPerkRequired != "none")
    {
        const SPDBPerk *pPerkReq = pPDB->GetPerk(pPerk->szPerkRequired);
        if (CanAddPerkLater(pPDB, pPerkReq, false) == FALSE)
            return FALSE;
    }
    // check exclusive perk
    {
        int nIndex = pPDB->GetPerkIndex(pPerk);
        int nBaseIndex = nIndex / 3;
        for (int i = 0; i < RPGPRK_LASTPERK; ++i)
        {
            const SPDBPerk *pPerk2 = GetPerk(i);
            if (pPerk2 != 0)
            {
                if ((pPerk2->nPerkExclusive == pPerk->nPerkExclusive) &&
                    (nBaseIndex != pPDB->GetPerkIndex(pPerk2) / 3))
                    return FALSE;
            }
        }
    }
    // check for such (or greater) perk exist
    if (bCheckExist)
    {
        int nIndex = pPDB->GetPerkIndex(pPerk->szCode);
        int nBaseIndex = nIndex / 3;
        int nFractIndex = nIndex % 3;
        if (nFractIndex <= (cPerks[nBaseIndex] - 1))
            return FALSE;
    }
    //
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void SRPGUnitStats::AddPerk(const CPerkDatabase *pPDB, const SPDBPerk *pPerk)
{
    float fOldMaxHP = RecalcHP();
    float fOldCurHP = CollectHP(fOldMaxHP);
    //
    int nIndex = pPDB->GetPerkIndex(pPerk);
    int nBaseIndex = nIndex / 3;
    int nFractIndex = nIndex % 3;

    if (cPerks[nBaseIndex] != 0)
    {
        if (nFractIndex <= (cPerks[nBaseIndex] - 1))
            return;
    }
    //
    cPerks[nBaseIndex] = BYTE(nFractIndex + 1);
    //
    float fNewMaxHP = RecalcHP();
    if ((fOldMaxHP > 1) && (fNewMaxHP > 1))
    {
        float fPercentage = fOldCurHP / fOldMaxHP;
        float fNewCurHP = CollectHP(fNewMaxHP);
        Heal(fNewMaxHP, fNewMaxHP * fPercentage - fNewCurHP);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
float SRPGUnitStats::GetNightVisionModifier() const
{
    float fPerkValue = GetPerkModifier(RPGPRK_INFRA);
    if (fPerkValue != 0)
        return senses[RPGS_INFRA].fpEffectiveValue * fPerkValue / 100.0f;
    else
        return 0;
}
float SRPGUnitStats::GetActionModifier() const
{
    return GetPerkModifier(RPGPRK_QUICKNESS) / 100.0f;
}
/////////////////////////////////////////////////////////////////////////////////////
float SRPGUnitStats::GetWSLBaseSkill(ERPGWeaponTypes rpgWeaponType) const
{
    if (const auto *mod = ModRuntime::Current())
        if (mod->CombatSetting("JMVAttacks"))
            return GetBaseAutoSkill((rpgWeaponType == RPGWST_BOW || rpgWeaponType == RPGWST_CROSSBOW ||
                rpgWeaponType == RPGWST_HANDS3 || rpgWeaponType == RPGWST_HANDS7)
                ? RPGASKL_ARCHERY : RPGASKL_MELEE);

    if (rpgWeaponType < RPGWST_BOW)
        return GetBaseAutoSkill(RPGASKL_MELEE);
    else if ((rpgWeaponType == RPGWST_BOW) || (rpgWeaponType == RPGWST_CROSSBOW))
        return GetBaseAutoSkill(RPGASKL_ARCHERY);
    else if (rpgWeaponType == RPGWST_HANDS)
        return GetBaseAutoSkill(RPGASKL_MELEE);
    else
        return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
float SRPGUnitStats::GetWSLPerkValue(ERPGWeaponTypes rpgWeaponType) const
{
    return GetPerkModifier(rpgWeaponType);
}
/////////////////////////////////////////////////////////////////////////////////////
float SRPGUnitStats::GetWSLWeaponModifier(DWORD dwWeaponSubtypeID) const
{
    const auto *mod = ModRuntime::Current();
    if (mod && mod->CombatSetting("JMVAttacks"))
    {
        const SIDBWeaponShape *pShape = theItemDatabase.weapons[CItem::GetShape(dwWeaponSubtypeID)];
        if (!pShape) return 0;
        const ERPGWeaponTypes rpgWeaponType = ERPGWeaponTypes(pShape->dwTypeID);
        const ERPGAutoSkills skill = (rpgWeaponType == RPGWST_BOW || rpgWeaponType == RPGWST_CROSSBOW ||
            rpgWeaponType == RPGWST_HANDS3 || rpgWeaponType == RPGWST_HANDS7)
            ? RPGASKL_ARCHERY : RPGASKL_MELEE;
        return pShape->GetSkillModifier(skill);
    }

    const SIDBWeaponShape *pShape = theItemDatabase.weapons[CItem::GetShape(dwWeaponSubtypeID)];
    if (!pShape) return 0;
    ERPGWeaponTypes rpgWeaponType = ERPGWeaponTypes(pShape->dwTypeID);
    ERPGAutoSkills rpgAutoSkill = RPGASKL_FORCE_DWORD;
    if ((rpgWeaponType < RPGWST_BOW) || (rpgWeaponType == RPGWST_HANDS))
        rpgAutoSkill = RPGASKL_MELEE;
    else if ((rpgWeaponType == RPGWST_BOW) || (rpgWeaponType == RPGWST_CROSSBOW))
        rpgAutoSkill = RPGASKL_ARCHERY;

    if (rpgAutoSkill >= RPGASKL_FORCE_DWORD)
    {
        ASSERT(0 && "Unsupported auto skill");
        return 0;
    }

    float fWeaponSkillModifier = 0;
    if (pShape)
        fWeaponSkillModifier = pShape->GetSkillModifier(rpgAutoSkill);

    return fWeaponSkillModifier;
}
/////////////////////////////////////////////////////////////////////////////////////
float SRPGUnitStats::GetBaseWeaponSkill(ERPGWeaponTypes rpgWeaponType) const
{
    if (rpgWeaponType < RPGWST_BOW)
        return GetPerkModifier(rpgWeaponType) + GetBaseAutoSkill(RPGASKL_MELEE);
    else if ((rpgWeaponType == RPGWST_BOW) || (rpgWeaponType == RPGWST_CROSSBOW))
        return GetPerkModifier(rpgWeaponType) + GetBaseAutoSkill(RPGASKL_ARCHERY);
    else if (rpgWeaponType == RPGWST_HANDS)
        return GetBaseAutoSkill(RPGASKL_MELEE);

    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
float SRPGUnitStats::GetWeaponSkill(ERPGWeaponTypes rpgWeaponType) const
{
    if (rpgWeaponType < RPGWST_BOW)
        return GetPerkModifier(rpgWeaponType) + GetAutoSkill(RPGASKL_MELEE);
    else if ((rpgWeaponType == RPGWST_BOW) || (rpgWeaponType == RPGWST_CROSSBOW))
        return GetPerkModifier(rpgWeaponType) + GetAutoSkill(RPGASKL_ARCHERY);
    else if (rpgWeaponType == RPGWST_HANDS)
        return GetAutoSkill(RPGASKL_MELEE);

    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
float SRPGUnitStats::GetWeaponSkillLevel(ERPGWeaponTypes rpgWeaponType, CUnit *pUnit) const
{
    if (const auto *mod = ModRuntime::Current())
        if (mod->CombatSetting("JMVAttacks"))
            return GetAutoSkillLevel((rpgWeaponType == RPGWST_BOW || rpgWeaponType == RPGWST_CROSSBOW ||
                rpgWeaponType == RPGWST_HANDS3 || rpgWeaponType == RPGWST_HANDS7)
                ? RPGASKL_ARCHERY : RPGASKL_MELEE, pUnit);

    if (rpgWeaponType < RPGWST_BOW)
        return GetPerkModifier(rpgWeaponType) + GetAutoSkillLevel(RPGASKL_MELEE, pUnit);
    else if ((rpgWeaponType == RPGWST_BOW) || (rpgWeaponType == RPGWST_CROSSBOW))
        return GetPerkModifier(rpgWeaponType) + GetAutoSkillLevel(RPGASKL_ARCHERY, pUnit);
    else if (rpgWeaponType == RPGWST_HANDS)
        return GetAutoSkillLevel(RPGASKL_MELEE, pUnit);

    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
float SRPGUnitStats::GetBaseAutoSkill(ERPGAutoSkills rpgAutoSkill) const
{
    return cAutoSkills[rpgAutoSkill];
}
/////////////////////////////////////////////////////////////////////////////////////
float SRPGUnitStats::GetAutoSkill(ERPGAutoSkills rpgAutoSkill) const
{
    float fBaseSkill = static_cast<float>(cAutoSkills[rpgAutoSkill]);

    switch (thePerkDatabase.GetSkill(rpgAutoSkill)->szBaseAttributes[0][0])
    {
    case 's': // str
        fBaseSkill += GetStrengthBonus();
        break;
    case 'd': // dex
        fBaseSkill += GetDexterityBonus();
        break;
    case 'i': // int
        fBaseSkill += GetIntelligenceBonus();
        break;
    }

    return fBaseSkill;
}
/////////////////////////////////////////////////////////////////////////////////////
static const float fHLTPercentages[RPG_NUM_HIT_LOCATION_TYPES] = { 10, 30, 15, 15 };
void SRPGUserUnitStats::GetAbsorbtions(const SRPGUserCommonStats *pCommonStats, float *fAbsorbtions,
                                       bool bHero) const
{
    memset(fAbsorbtions, 0, sizeof(float) * RPG_NUM_ATTACK_TYPES);
    float fTotalPercentage = 0;
    // absorbtions from HLs (for heroes only)
    if (bHero)
    {
        for (int i = 0; i < RPG_NUM_HIT_LOCATIONS; ++i)
        {
            if (hls[i].state > RPGALS_CRIPPLED)
            {
                float fPercentage = fHLTPercentages[hls[i].hlt];
                for (int j = 0; j < RPG_NUM_ATTACK_TYPES; ++j)
                    fAbsorbtions[j] += fPercentage * hls[i].wAbsorbtions[j];
                fTotalPercentage += fPercentage;
            }
        }
    }
    // base absorbtions
    fTotalPercentage = fTotalPercentage > 0 ? 1.0f / fTotalPercentage : 0;
    const float fAbsorptionsMultiplier =
        bHero ? 1.0f : theAIGeneralSettings.fAbsorptionLevels[theSettings.nDifficultyLevel];
    for (int i = 0; i < RPG_NUM_ATTACK_TYPES; ++i)
        fAbsorbtions[i] = ((fAbsorbtions[i] * fTotalPercentage) + pCommonStats->wAbsorbtions[i]) *
                          fAbsorptionsMultiplier;
}
/////////////////////////////////////////////////////////////////////////////////////
float SRPGUserUnitStats::GetManaBonusMultiplier() const
{
    if(auto*mod=ModRuntime::Current())return mod->ManaBonus(*this);
    return 25.0f / float(cIntelligence);
}
/////////////////////////////////////////////////////////////////////////////////////
float SRPGUserUnitStats::GetSightRange(float fNightPower) const
{
    if (const auto *mod = ModRuntime::Current())
        if (mod->CombatSetting("JMVAttacks"))
        {
            const float nightSight = float(cSenses[RPGS_INFRA]) * fNightPower / 100 + (1 - fNightPower);
            return cSenses[RPGS_SIGHT] * (1 < nightSight ? 1 : nightSight);
        }

    float fBaseSight = cSenses[RPGS_SIGHT];
    return (fBaseSight * (float(cSenses[RPGS_INFRA]) * fNightPower / 100 + (1 - fNightPower)));
}
/////////////////////////////////////////////////////////////////////////////////////
float SRPGUserUnitStats::GetWeaponSkill(ERPGWeaponTypes rpgWeaponType) const
{
    if (rpgWeaponType < RPGWST_BOW)
        return GetPerkModifier(rpgWeaponType) + GetAutoSkill(RPGASKL_MELEE);
    else if ((rpgWeaponType == RPGWST_BOW) || (rpgWeaponType == RPGWST_CROSSBOW))
        return GetPerkModifier(rpgWeaponType) + GetAutoSkill(RPGASKL_ARCHERY);
    else if (rpgWeaponType == RPGWST_HANDS)
        return GetAutoSkill(RPGASKL_MELEE);

    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
float SRPGUserUnitStats::GetWeaponSkillLevel(ERPGWeaponTypes rpgWeaponType, CUnit *pUnit) const
{
    if (rpgWeaponType < RPGWST_BOW)
        return GetPerkModifier(rpgWeaponType) + GetAutoSkillLevel(RPGASKL_MELEE, pUnit);
    else if ((rpgWeaponType == RPGWST_BOW) || (rpgWeaponType == RPGWST_CROSSBOW))
        return GetPerkModifier(rpgWeaponType) + GetAutoSkillLevel(RPGASKL_ARCHERY, pUnit);
    else if (rpgWeaponType == RPGWST_HANDS)
        return GetAutoSkillLevel(RPGASKL_MELEE, pUnit);

    return 0;
}
float SRPGUserUnitStats::GetWeaponSkillLevel(ERPGWeaponTypes rpgWeaponType,
                                             const SIDBWeaponShape *pShape) const
{
    if (const auto *mod = ModRuntime::Current())
        if (mod->CombatSetting("JMVAttacks"))
            return GetAutoSkillLevel((rpgWeaponType == RPGWST_BOW || rpgWeaponType == RPGWST_CROSSBOW ||
                rpgWeaponType == RPGWST_HANDS3 || rpgWeaponType == RPGWST_HANDS7)
                ? RPGASKL_ARCHERY : RPGASKL_MELEE, pShape);

    if (rpgWeaponType < RPGWST_BOW)
        return GetPerkModifier(rpgWeaponType) + GetAutoSkillLevel(RPGASKL_MELEE, pShape);
    else if ((rpgWeaponType == RPGWST_BOW) || (rpgWeaponType == RPGWST_CROSSBOW))
        return GetPerkModifier(rpgWeaponType) + GetAutoSkillLevel(RPGASKL_ARCHERY, pShape);
    else if (rpgWeaponType == RPGWST_HANDS)
        return GetAutoSkillLevel(RPGASKL_MELEE, pShape);

    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
float SRPGUserUnitStats::GetAutoSkill(ERPGAutoSkills rpgAutoSkill) const
{
    float fBaseSkill = static_cast<float>(cAutoSkills[rpgAutoSkill]);

    switch (thePerkDatabase.GetSkill(rpgAutoSkill)->szBaseAttributes[0][0])
    {
    case 's': // str
        fBaseSkill += GetStrengthBonus();
        break;
    case 'd': // dex
        fBaseSkill += GetDexterityBonus();
        break;
    case 'i': // int
        fBaseSkill += GetIntelligenceBonus();
        break;
    }

    return fBaseSkill;
}
/////////////////////////////////////////////////////////////////////////////////////
float SRPGUserUnitStats::GetAutoSkillLevel(ERPGAutoSkills rpgAutoSkill, CUnit *pUnit) const
{
    if (rpgAutoSkill >= RPGASKL_FORCE_DWORD)
        ASSERT(0 && "Unsupported auto skill");

    const CItemWeapon *pWeapon = pUnit->GetCurrentQWeapon();
    float fWeaponSkillModifier = 0;
    if (pWeapon)
        fWeaponSkillModifier = pWeapon->GetSkillModifier(rpgAutoSkill);

    return GetAutoSkill(rpgAutoSkill) + fWeaponSkillModifier;
}
float SRPGUserUnitStats::GetAutoSkillLevel(ERPGAutoSkills rpgAutoSkill,
                                           const SIDBWeaponShape *pShape) const
{
    if (rpgAutoSkill >= RPGASKL_FORCE_DWORD)
        ASSERT(0 && "Unsupported auto skill");

    float fWeaponSkillModifier = 0;
    if (pShape)
        fWeaponSkillModifier = pShape->GetSkillModifier(rpgAutoSkill);

    return GetAutoSkill(rpgAutoSkill) + fWeaponSkillModifier;
}
/////////////////////////////////////////////////////////////////////////////////////
float SRPGUnitStats::GetBaseMagicSkill(ERPGMagicSubtypes subtype) const
{
    float fBaseSkill = 0;

    if (IsValInRange<bool>(subtype, RPGMST_FIRE, RPGMST_ACID))
        fBaseSkill = cAutoSkills[RPGASKL_ELEMENTAL];
    else if (IsValInRange<bool>(subtype, RPGMST_ILLUSION, RPGMST_DIVINATION))
        fBaseSkill = cAutoSkills[RPGASKL_SENSE];
    else if (IsValInRange<bool>(subtype, RPGMST_ENCHANTMENTS, RPGMST_DOMINATION))
        fBaseSkill = cAutoSkills[RPGASKL_ASTRAL];

    return fBaseSkill;
}
/////////////////////////////////////////////////////////////////////////////////////
float SRPGUnitStats::GetMagicSkill(ERPGMagicSubtypes subtype) const
{
    if (subtype == RPGMST_DIVINATION)
        subtype = RPGMST_ILLUSION;
    else if ((subtype == RPGMST_HEALING) || (subtype == RPGMST_DOMINATION))
        subtype = RPGMST_ENCHANTMENTS;
    //
    int nPerkIndex = RPGPRK_FIRE + subtype;
    float fBaseSkill = GetBaseMagicSkill(subtype);
    fBaseSkill += GetPerkModifier(nPerkIndex);
    return fBaseSkill + GetIntelligenceBonus();
}
/////////////////////////////////////////////////////////////////////////////////////
float SRPGUnitStats::GetSkillPrice(int nSkillLevel)
{
    return theAIGeneralSettings.fSkillVal1 *
           pow(1.61, float(nSkillLevel) / theAIGeneralSettings.fSkillVal2);
}
/////////////////////////////////////////////////////////////////////////////////////
int SRPGUnitStats::GetSkillPrice(int nFrom, int nTo)
{
    if (auto *mod = ModRuntime::Current()) return mod->GetSkillPriceDiff(nFrom, nTo);
    return int(GetSkillPrice(nTo) - GetSkillPrice(nFrom));
}
/////////////////////////////////////////////////////////////////////////////////////
int SRPGUnitStats::GetPerkPrice(const SPDBPerk *pPerk)
{
    if (auto *mod = ModRuntime::Current()) return mod->GetPerkPrice(*this, *pPerk);
    int nIndex = thePerkDatabase.GetPerkIndex(pPerk);
    int nBase = nIndex / 3;
    int nFraction = nIndex % 3;
    float fBasePrice = 0;
    if (nFraction > 0)
    {
        const SPDBPerk *pPrevPerk = thePerkDatabase.GetPerk(nBase * 3 + nFraction - 1);
        fBasePrice = GetSkillPrice(pPrevPerk->dwCost, pPerk->dwCost);
    }
    else
        fBasePrice = GetSkillPrice(0, pPerk->dwCost);
    // now we must count all active perks and recalc price with the next formula
    // fBasePrice * pow( theAIGeneralSettings.fPerkPowerBase, nTotalPerkCount );
    int nTotalPerkCount = std::accumulate(&(cPerks[0]), &(cPerks[RPG_NUM_PERKS]), BYTE(0));
    fBasePrice *= pow(theAIGeneralSettings.fPerkPowerBase, nTotalPerkCount);
    float fDecBase = floor(log10(fBasePrice));
    fDecBase = pow(10, fDecBase);
    if (fBasePrice / fDecBase < 4)
    {
        if (fDecBase <= 10)
            return ceil(fBasePrice);
        else
        {
            fDecBase *= 0.1f;
            return floor(fBasePrice / fDecBase + 0.5f) * fDecBase;
        }
    }
    else
        return floor(fBasePrice / fDecBase + 0.5f) * fDecBase;
}
/////////////////////////////////////////////////////////////////////////////////////
const SPDBPerk *SRPGUnitStats::GetPerk(int nPerk) const
{
    if (cPerks[nPerk] == 0)
        return 0;
    return thePerkDatabase.GetPerk(nPerk * 3 + cPerks[nPerk] - 1);
}
/////////////////////////////////////////////////////////////////////////////////////
float SRPGUnitStats::GetPerkModifier(int nPerk) const
{
    const SPDBPerk *pPerk = GetPerk(nPerk);
    if (pPerk)
        return pPerk->dwModifier;
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
const SPDBPerk *SRPGUserUnitStats::GetPerk(int nPerk) const
{
    if (cPerks[nPerk] == 0)
        return 0;
    return thePerkDatabase.GetPerk(nPerk * 3 + cPerks[nPerk] - 1);
}
/////////////////////////////////////////////////////////////////////////////////////
float SRPGUserUnitStats::GetPerkModifier(int nPerk) const
{
    const SPDBPerk *pPerk = GetPerk(nPerk);
    if (pPerk)
        return pPerk->dwModifier;
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
float SRPGUserUnitStats::GetStrengthModifier() const
{
    return GetPerkModifier(RPGPRK_STRENGTH);
}
/////////////////////////////////////////////////////////////////////////////////////
float SRPGUserUnitStats::GetDexterityModifier() const
{
    return GetPerkModifier(RPGPRK_DEXTERITY);
}
/////////////////////////////////////////////////////////////////////////////////////
float SRPGUserUnitStats::GetIntelligenceModifier() const
{
    return GetPerkModifier(RPGPRK_INTELLIGENCE);
}
/////////////////////////////////////////////////////////////////////////////////////
float SRPGUserUnitStats::GetDamageModifier(const CUnit *pUnit) const
{
    if (pUnit)
    {
        float fStrengthen = pUnit->GetEffectValue(SPELL_ID_STRENGTHEN) * 0.01f * 0.3f;
        float fWeaken = pUnit->GetEffectValue(SPELL_ID_WEAKEN) * 0.01f * 0.3f;
        return (1 + fStrengthen) / (1 + fWeaken);
    }
    else
        return 1.0f;
}
/////////////////////////////////////////////////////////////////////////////////////
float SRPGUserUnitStats::GetModifiedDamage(const CUnit *pUnit, float fDamage) const
{
    if(pUnit)if(auto*mod=ModRuntime::Current())return mod->ModifiedDamage(*this,*pUnit,fDamage);
    return Max(1.0f, fDamage * GetDamageModifier(pUnit));
}
/////////////////////////////////////////////////////////////////////////////////////
float SRPGUnitStats::GetStrengthModifier() const
{
    return GetPerkModifier(RPGPRK_STRENGTH);
}
/////////////////////////////////////////////////////////////////////////////////////
float SRPGUnitStats::GetDexterityModifier() const
{
    return GetPerkModifier(RPGPRK_DEXTERITY);
}
/////////////////////////////////////////////////////////////////////////////////////
float SRPGUnitStats::GetIntelligenceModifier() const
{
    return GetPerkModifier(RPGPRK_INTELLIGENCE);
}
/////////////////////////////////////////////////////////////////////////////////////


static const float PS_MIN_STAT_VALUE = 15;
static const float PS_MAX_STAT_VALUE = 35;
static const float PS_DIF_STAT_VALUE = PS_MAX_STAT_VALUE - PS_MIN_STAT_VALUE;
void SRPGUnitStats::RecalcComplectionFromPrimaryStats(float &fStrength, float &fGrace, float &fTall)
{
    // primary stat changing range = [15..35]

    // fStrength = ( psStrength.fpEffectiveValue - PS_MIN_STAT_VALUE ) / PS_DIF_STAT_VALUE;
    // fGrace = 1.0f - ( psDexterity.fpEffectiveValue - PS_MIN_STAT_VALUE ) / PS_DIF_STAT_VALUE;
    float s = (psStrength.fpEffectiveValue - PS_MIN_STAT_VALUE) / PS_DIF_STAT_VALUE;
    float d = (psDexterity.fpEffectiveValue - PS_MIN_STAT_VALUE) / PS_DIF_STAT_VALUE;
    float str = s * 0.7f + (1 - d) * 0.3f;
    float grc = s * 0.2f + (1 - d) * 0.8f;
    fStrength = str;
    fGrace = 0.2f + grc * 0.8f;
}
/////////////////////////////////////////////////////////////////////////////////////
void SRPGUnitStats::RedistributeHPMP(SRPGCommonStats *pCommonStats, CUnit *pUnit)
{
    if (auto* mod=ModRuntime::Current())
        mod->RedistributeResources(*this,*pCommonStats,*pUnit);
    else
    {
        pCommonStats->ssHP.fpMax = GetModifiedHealth(pUnit, RecalcHP());
        pCommonStats->ssMP.fpMax = RecalcMP();
    }
    // redistribute HP and heal to achive last HP percentage
    DistributeHPProportional(pCommonStats->ssHP.fpMax);
    pCommonStats->ssHP.fpCurrent = CollectHP(pCommonStats->ssHP.fpMax);
}
/////////////////////////////////////////////////////////////////////////////////////
bool SRPGUnitStats::PresentSkill(ERPGAutoSkills skill, BYTE cAddLevel, CUnit *pUnit)
{
    CMapObjectSpecific *pMOSpec = pUnit->GetMapObjectSpecific();
    if (pMOSpec->WhoAmI() != HPC_MAPOBJECTSERVERSPECIFIC)
        return false;
    SRPGCommonStats *pCommonStats = static_cast<CMapObjectServer *>(pMOSpec)->GetRPGStats();
    // add skill and spent exp
    int nAddSpentExp = SRPGUnitStats::GetSkillPrice(cAutoSkills[skill],
                                                    cAutoSkills[skill] + cAddLevel);
    cAutoSkills[skill] += cAddLevel;
    fExpPenalty += nAddSpentExp;
    if (fExpPenalty > 0)
    {
        fpExperience += fExpPenalty;
        fSpentExp += fExpPenalty;
        fExpPenalty = 0;
    }
    // redistribute HP and MP
    RedistributeHPMP(pCommonStats, pUnit);
    // recalc all stats with new values
    RecalcStats(pCommonStats, pUnit);
    //
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
bool SRPGUnitStats::PresentSkill(ERPGManualSkills skill, BYTE cAddLevel, CUnit *pUnit)
{
    CMapObjectSpecific *pMOSpec = pUnit->GetMapObjectSpecific();
    if (pMOSpec->WhoAmI() != HPC_MAPOBJECTSERVERSPECIFIC)
        return false;
    SRPGCommonStats *pCommonStats = static_cast<CMapObjectServer *>(pMOSpec)->GetRPGStats();
    // add skill and spent exp
    int nAddSpentExp = SRPGUnitStats::GetSkillPrice(cManualSkills[skill],
                                                    cManualSkills[skill] + cAddLevel);
    cManualSkills[skill] += cAddLevel;
    fExpPenalty += nAddSpentExp;
    if (fExpPenalty > 0)
    {
        fpExperience += fExpPenalty;
        fSpentExp += fExpPenalty;
        fExpPenalty = 0;
    }
    // redistribute HP and MP
    RedistributeHPMP(pCommonStats, pUnit);
    // recalc all stats with new values
    RecalcStats(pCommonStats, pUnit);
    //
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
bool SRPGUnitStats::PresentStrength(int nValue, CUnit *pUnit)
{
    CMapObjectSpecific *pMOSpec = pUnit->GetMapObjectSpecific();
    if (pMOSpec->WhoAmI() != HPC_MAPOBJECTSERVERSPECIFIC)
        return false;
    SRPGCommonStats *pCommonStats = static_cast<CMapObjectServer *>(pMOSpec)->GetRPGStats();
    //
    psStrength.fpBaseValue += nValue;
    // redistribute HP and MP
    RedistributeHPMP(pCommonStats, pUnit);
    // add 0.2 to strength complection (base and current)
    const_cast<SComplection &>(pUnit->GetBaseComplection()).strength += 0.2f;
    const_cast<SComplection &>(pUnit->GetComplection()).strength += 0.2f;
    pUnit->SetUpdateMask(CMapObject::VLD_MASK_COMPLECTION);
    // recalc all stats with new values
    RecalcStats(pCommonStats, pUnit);
    pUnit->SetUpdateMask(CMapObject::VLD_MASK_UNIT_STATS);
    //
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
bool SRPGUnitStats::PresentDexterity(int nValue, CUnit *pUnit)
{
    CMapObjectSpecific *pMOSpec = pUnit->GetMapObjectSpecific();
    if (pMOSpec->WhoAmI() != HPC_MAPOBJECTSERVERSPECIFIC)
        return false;
    SRPGCommonStats *pCommonStats = static_cast<CMapObjectServer *>(pMOSpec)->GetRPGStats();
    //
    psDexterity.fpBaseValue += nValue;
    // redistribute HP and MP
    RedistributeHPMP(pCommonStats, pUnit);
    // sub 0.2 from grace complection (base and current)
    {
        SComplection &comp = const_cast<SComplection &>(pUnit->GetBaseComplection());
        comp.grace = Max(0.0f, comp.grace - 0.2f);
    }
    {
        SComplection &comp = const_cast<SComplection &>(pUnit->GetComplection());
        comp.grace = Max(0.0f, comp.grace - 0.2f);
    }
    pUnit->SetUpdateMask(CMapObject::VLD_MASK_COMPLECTION);
    // recalc all stats with new values
    RecalcStats(pCommonStats, pUnit);
    pUnit->SetUpdateMask(CMapObject::VLD_MASK_UNIT_STATS);
    //
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
bool SRPGUnitStats::PresentIntelligence(int nValue, CUnit *pUnit)
{
    CMapObjectSpecific *pMOSpec = pUnit->GetMapObjectSpecific();
    if (pMOSpec->WhoAmI() != HPC_MAPOBJECTSERVERSPECIFIC)
        return false;
    SRPGCommonStats *pCommonStats = static_cast<CMapObjectServer *>(pMOSpec)->GetRPGStats();
    //
    psIntelligence.fpBaseValue += nValue;
    // redistribute HP and MP
    RedistributeHPMP(pCommonStats, pUnit);
    // recalc all stats with new values
    RecalcStats(pCommonStats, pUnit);
    //
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
