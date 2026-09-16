#pragma once
////////////////////////////////////////////////////////////////////////////////////
#ifndef __NO_MFC__
#include "mm_malloc.h"
#include "SimplyArray.h"
#endif // __NO_MFC__
////////////////////////////////////////////////////////////////////////////////////
// animation description DWORD format:
// 1st WORD - completelly weapon flags! non-exclusive.
// 2nd WORD:
// 		2 bits - state: attack, neutral, warry, lie. Exclusive
//    4 bits - action: attack, cast, move, idle, die, cross, suffer, special. Exclusive
//    8 bits - action modifiers
// 		2 bits - action animation stage: start, cycle, end, unique. Exclusive
//
// direction table 1 (attack directions)
// vertical directions  : up, middle, down
// horizontal directions: left, middle, right
//  UL | UM | UR
//  ------------
//  ML | MM | MR
//  ------------
//  DL | DM | DR
//
// direction table 2 (hit/death directions)
//     | FM |
//  ------------
//  ML | MM | MR
//  ------------
//     | BM |
//
const DWORD ADB_WEAPON_SWORD = 0x00000001;
const DWORD ADB_WEAPON_AXE = 0x00000002;
const DWORD ADB_WEAPON_DAGGER = 0x00000004;
const DWORD ADB_WEAPON_SPEAR = 0x00000008;
const DWORD ADB_WEAPON_HAMMER = 0x00000010;
const DWORD ADB_WEAPON_BOW = 0x00000020;
const DWORD ADB_WEAPON_CROSSBOW = 0x00000040;
const DWORD ADB_WEAPONS = 0x00007FFF;
// state constants. exclusive.
const DWORD ADB_STATE_NEUTRAL = 0x00000000;
const DWORD ADB_STATE_REST = 0x00008000;
const DWORD ADB_STATE_ATTACK = 0x00010000;
const DWORD ADB_STATE_WARRY = 0x00020000;
const DWORD ADB_STATE_LIE = 0x00030000;
const DWORD ADB_STATES = 0x00038000;
// action constants. exclusive
const DWORD ADB_ACTION_SPECIAL = 0x00040000;
const DWORD ADB_ACTION_ATTACK = 0x00080000;
const DWORD ADB_ACTION_CAST = 0x000C0000;
const DWORD ADB_ACTION_RUN = 0x00100000;
const DWORD ADB_ACTION_WALK = 0x00140000;
const DWORD ADB_ACTION_IDLE = 0x00180000;
const DWORD ADB_ACTION_DEATH = 0x001C0000;
const DWORD ADB_ACTION_SUFFER = 0x00200000;
const DWORD ADB_ACTION_CROSS = 0x00240000;
const DWORD ADB_ACTIONS = 0x003C0000;
// action modifiers. exclusive
// move walk
const DWORD ADB_MODIFIER_WALK_LAME1 = 0x00400000;
const DWORD ADB_MODIFIER_WALK_LAME2 = 0x00800000;
// directions 1 (attack/cast directions)
const DWORD ADB_MODIFIER_HDIRECTION1_LEFT = 0x00400000;
const DWORD ADB_MODIFIER_HDIRECTION1_MIDDLE = 0x00800000;
const DWORD ADB_MODIFIER_HDIRECTION1_RIGHT = 0x00C00000;
const DWORD ADB_MODIFIER_VDIRECTION1_UP = 0x01000000;
const DWORD ADB_MODIFIER_VDIRECTION1_MIDDLE = 0x02000000;
const DWORD ADB_MODIFIER_VDIRECTION1_DOWN = 0x03000000;
// direction 2 (hit/death directions) and modifier_actions (for hit/death: from moving, from
// another...)
const DWORD ADB_MODIFIER_DIRECTION2_MM = 0x00000000;
const DWORD ADB_MODIFIER_DIRECTION2_FM = 0x00400000;
const DWORD ADB_MODIFIER_DIRECTION2_BM = 0x00800000;
const DWORD ADB_MODIFIER_DIRECTION2_ML = 0x00C00000;
const DWORD ADB_MODIFIER_DIRECTION2_MR = 0x01000000;
const DWORD ADB_MODIFIER_DIRECTION2_MASK = 0x01C00000;
//
const DWORD ADB_MODIFIER_ACTION_RUN = 0x02000000;
const DWORD ADB_MODIFIER_ACTION_WALK = 0x04000000;
const DWORD ADB_MODIFIER_ACTION_STAY = 0x06000000;
// idle modifiers
const DWORD ADB_MODIFIER_IDLE_STAY = 0x00400000; // idle: just stay
const DWORD ADB_MODIFIER_IDLE_LOOK = 0x00800000; // idle: lookaround
// cross modifiers
const DWORD ADB_MODIFIER_CROSS_TO_NEUTRAL = 0x00400000;
const DWORD ADB_MODIFIER_CROSS_TO_ATTACK = 0x00800000;
const DWORD ADB_MODIFIER_CROSS_TO_WARRY = 0x00C00000;
const DWORD ADB_MODIFIER_CROSS_TO_LIE = 0x01000000;
const DWORD ADB_MODIFIER_CROSS_TO_REST = 0x01400000;
// special modifiers
const DWORD ADB_MODIFIER_SPECIAL_1 = 0x00400000;
const DWORD ADB_MODIFIER_SPECIAL_2 = 0x00800000;
const DWORD ADB_MODIFIER_SPECIAL_3 = 0x00C00000;
const DWORD ADB_MODIFIER_SPECIAL_4 = 0x01000000;
const DWORD ADB_MODIFIER_SPECIAL_5 = 0x01400000;
const DWORD ADB_MODIFIER_SPECIAL_6 = 0x01800000;
const DWORD ADB_MODIFIER_SPECIAL_7 = 0x01C00000;
const DWORD ADB_MODIFIER_SPECIAL_8 = 0x02000000;
const DWORD ADB_MODIFIER_SPECIAL_9 = 0x02400000;
const DWORD ADB_MODIFIER_SPECIAL_10 = 0x02800000;
const DWORD ADB_MODIFIER_SPECIAL_11 = 0x02C00000;
const DWORD ADB_MODIFIER_SPECIAL_12 = 0x03000000;
const DWORD ADB_MODIFIER_SPECIAL_13 = 0x03400000;
const DWORD ADB_MODIFIER_SPECIAL_14 = 0x03800000;
const DWORD ADB_MODIFIER_SPECIAL_15 = 0x03C00000;
const DWORD ADB_MODIFIER_SPECIAL_16 = 0x04000000;
//
const DWORD ADB_MODIFIERS = 0x3FC00000;
// action animation stage. exclusive
const DWORD ADB_STAGE_START = 0x40000000;
const DWORD ADB_STAGE_CYCLE = 0x80000000;
const DWORD ADB_STAGE_END = 0xC0000000;
const DWORD ADB_STAGE_UNIQUE = 0x00000000;
const DWORD ADB_STAGES = 0xC0000000;
//
//
// special flags (dwFlags2)
const DWORD ADB_FLAGS2_HIDES = 0x00000001;
const DWORD ADB_FLAGS2_STEPS = 0x00000002;
const DWORD ADB_FLAGS2_HIT = 0x00000004;
const DWORD ADB_FLAGS2_SFXES = 0x00000008;
const DWORD ADB_FLAGS2_ALLOW_LOW_SHAPE = 0x00000010;
const DWORD ADB_FLAGS2_ALLOW_HIGH_SHAPE = 0x00000020;
////////////////////////////////////////////////////////////////////////////////////
const DWORD ADB_SIGNATURE = 0x00424441; // 'ADB'
////////////////////////////////////////////////////////////////////////////////////
struct SADEntry;
////////////////////////////////////////////////////////////////////////////////////
struct SADBFileHeader
{
public:
    DWORD dwSignature;     // adb file signature
    DWORD dwCount;         // number of database entries
    char pszModelName[24]; // model name, which animation for...
    float fpMinHeight;     // min height of the model
    float fpMiddleHeight;  // middle height of the model - animation model
    float fpMaxHeight;     // max height of the model
public:
    DWORD ShiftToEntry(DWORD dwNumber) const;
    SADEntry *GetEntry(DWORD dwNumber) const;
    float GetHeightCorrection(float fTall) const
    {
        return fpMiddleHeight != 0
                   ? (fpMinHeight + fTall * (fpMaxHeight - fpMinHeight)) / fpMiddleHeight
                   : 1.0f;
    }
};
////////////////////////////////////////////////////////////////////////////////////
struct SADEntry
{
public:
    char pszAnimationName[16]; // animation file name (max = 16 characters)
    DWORD dwEntryIndex;        // entry index in enties array
    DWORD dwFlags;             // animation flags (see above for constants)
    DWORD dwFlags2; // flags, which match special cases: have steps, hit, spec. sfx, hides, etc.
    DWORD dwProbability;     // probability of this animation in the case of some similar animations
    DWORD dwAnimationLength; // animation length (in frames: 1 frame = 1/15 sec)
    float fpSpeed;           // movement speed during animation (in m/s)
    DWORD dwHides[2];        // frames for unhide/hide 'arrow' object
    DWORD dwSteps[4];        // frame numbers to play 'step' sfx ('0' -  sequence end)
    DWORD dwHit;             // frame number to play 'hit' sfx
    DWORD dwSFXFrame;        // frame, which we must play special SFXes on
    DWORD dwSFXIndices[4];   // SFX names, as digits: 1, 2, etc.
public:
    DWORD GetState() const { return dwFlags & ADB_STATES; }
    DWORD GetAction() const { return dwFlags & ADB_ACTIONS; }
    DWORD GetWeapons() const { return dwFlags & ADB_WEAPONS; }
    DWORD GetStage() const { return dwFlags & ADB_STAGES; }

    DWORD IsHaveUnhide() const { return dwFlags2 & ADB_FLAGS2_HIDES; }
    DWORD IsHaveSteps() const { return dwFlags2 & ADB_FLAGS2_STEPS; }
    DWORD IsHaveHit() const { return dwFlags2 & ADB_FLAGS2_HIT; }
    DWORD IsHaveSFX() const { return dwFlags2 & ADB_FLAGS2_SFXES; }

    DWORD IsUnique() const { return ((dwFlags & ADB_STAGES) != ADB_STAGE_CYCLE); }
    DWORD IsCycled() const { return ((dwFlags & ADB_STAGES) == ADB_STAGE_CYCLE); }

    DWORD Match(DWORD dwCaps) const;
    DWORD MatchNoWeapon(DWORD dwCaps) const;

    float GetHeightCorrection(float fpTall) const;
    float GetMovementSpeed(float fpTall) const;
};
////////////////////////////////////////////////////////////////////////////////////
inline float SADEntry::GetHeightCorrection(float fTall) const
{
    const SADBFileHeader *pHdr = (const SADBFileHeader *)(int(this) -
                                                          dwEntryIndex * sizeof(SADEntry) -
                                                          sizeof(SADBFileHeader));
    return pHdr->GetHeightCorrection(fTall);
}
////////////////////////////////////////////////////////////////////////////////////
inline float SADEntry::GetMovementSpeed(float fTall) const
{
    return fpSpeed * GetHeightCorrection(fTall);
}
////////////////////////////////////////////////////////////////////////////////////
inline DWORD SADEntry::Match(DWORD dwCaps) const
{
    /*
    return ( ( (dwFlags & dwCaps & ADB_WEAPONS) == (dwCaps & ADB_WEAPONS) ) &&
                   ( (dwFlags & ADB_STATES) == (dwCaps & ADB_STATES) )   &&
                                     ( (dwFlags & ADB_ACTIONS) == (dwCaps & ADB_ACTIONS) ) &&
                                     ( (dwFlags & ADB_STAGES) == (dwCaps & ADB_STAGES) )   &&
                                     ( (dwFlags & ADB_MODIFIERS) == (dwCaps & ADB_MODIFIERS) ) );
    */
    return (((((dwCaps & ADB_WEAPONS) == 0) && ((dwFlags & ADB_WEAPONS) == 0)) ||
             (((dwCaps & ADB_WEAPONS) != 0) &&
              ((dwFlags & dwCaps & ADB_WEAPONS) == (dwCaps & ADB_WEAPONS)))) &&
            ((dwFlags & ADB_STATES) == (dwCaps & ADB_STATES)) &&
            ((dwFlags & ADB_ACTIONS) == (dwCaps & ADB_ACTIONS)) &&
            ((dwFlags & ADB_STAGES) == (dwCaps & ADB_STAGES)) &&
            ((dwFlags & ADB_MODIFIERS) == (dwCaps & ADB_MODIFIERS)));
}
inline DWORD SADEntry::MatchNoWeapon(DWORD dwCaps) const
{
    return (((dwFlags & ADB_STATES) == (dwCaps & ADB_STATES)) &&
            ((dwFlags & ADB_ACTIONS) == (dwCaps & ADB_ACTIONS)) &&
            ((dwFlags & ADB_STAGES) == (dwCaps & ADB_STAGES)) &&
            ((dwFlags & ADB_MODIFIERS) == (dwCaps & ADB_MODIFIERS)));
}
/////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////
inline DWORD SADBFileHeader::ShiftToEntry(DWORD dwNumber) const
{
    return sizeof(SADBFileHeader) + dwNumber * sizeof(SADEntry);
}
////////////////////////////////////////////////////////////////////////////////////
inline SADEntry *SADBFileHeader::GetEntry(DWORD dwNumber) const
{
    return (SADEntry *)(DWORD(this) + ShiftToEntry(dwNumber));
}
////////////////////////////////////////////////////////////////////////////////////
#ifndef __NO_MFC__
typedef SADEntry *LPADENTRY;
typedef CSimplyArray<LPADENTRY, LPADENTRY> CADEntryPtrArray;
#endif // __NO_MFC__
////////////////////////////////////////////////////////////////////////////////////
