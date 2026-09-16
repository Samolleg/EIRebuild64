#pragma once
//
// MEUnitStats.h: interface for the CMEUnitStats class.
//
//////////////////////////////////////////////////////////////////////

#include "RPGSystemConsts.h"

class CUnit;

class CMEUnitSpecific
{
public:
    struct SUnitStats
    {
        int nHP;
        int nMaxHP;

        int nMP;
        int nMaxMP;

        // secondary attributes
        float move;            // move points (coeff for all movement actions)
                               // // только эффективные
        float action;          // action points (coeff for all non-movement actions /ZB: attack/)	//
                               // только эффективные
        float fpSpeeds[4];     // Monsters only	// speed in 4 different movement type
        float fpVisionArc;     // Monsters only	// vision arc - cosine( arc )
        float fpPeripheral;    // Monsters only	// peripharal sight distance
        float fpPeripheralArc; // Monsters only	// peripheral sight arc. by default = 180 degrees

        //
        float fpAttackDistance; // Monsters only	// minimal attack distance
        BYTE dwAIClassStay;     // AI class стоя
        BYTE dwAIClassLie;      // AI class лёжа

        // Native attack weapon (NAW) params // Monsters only
        float fpNAWRange;
        float fpNAWToHit;
        float fpNAWParry;
        float fpNAWWeight;
        float fpNAWDamageMin;
        float fpNAWDamageRnd;
        float fpNAWAttacks[RPG_NUM_ATTACK_TYPES];

        BYTE nAbsorbtion; // Monsters only

        //
        float senses[RPG_NUM_SENSES];   // чувствительность по каждому органу чувств // только
                                        // эффективные
        float percepts[RPG_NUM_SENSES]; // степень заметности для каждого органа чувств // только
                                        // эффективные

        // skills
        BYTE cManualSkills[RPG_NUM_MANUAL_SKILLS]; // manual skills
        BYTE cMagicalSkills[3];
    } stats;

    CStringArray aQuestItems;
    CStringArray aQuickItems;
    CStringArray aSpells;
    CStringArray aWeapons; // Human only
    CStringArray aArmors;  // Human only
    BOOL needImport;
    CMEUnitSpecific()
        : needImport(FALSE)
    {
    }

    void Export(CUnit *pUnit);
    void Import(CUnit *pUnit);
};
