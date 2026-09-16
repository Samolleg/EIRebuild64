#pragma once
/////////////////////////////////////////////////////////////////////////////////////
#include "sdbFormat.h"
#include "SaveLoader.h"
/////////////////////////////////////////////////////////////////////////////////////
// spell effect - contains characteristics of some spell
class CSpellEffect
{
public:
    BYTE nSpellID;     // spell ID (see SpellSystem.h spell_id_xxx for particular values)
    BYTE nVisStrength; // visible strength of effect ( for visualization only )
    long nDuration;    // effect duration (in segments: 0..1 - instant, >1 - continuous)
    float fpPower;     // power of this effect
    float fpRadius;    // area of effect
    bool bQuiet;       // spell does not produce any sound
public:
    CSpellEffect();
    float GetPower() const { return fpPower; }
    int GetEffectType() const { return (int)nSpellID; }
    //
    int Segment();
    //
    void Load(CSave &file);
    void Store(CSave &file);
};
/////////////////////////////////////////////////////////////////////////////////////
typedef CArray<CSpellEffect, CSpellEffect &> CSpellEffectArray;
