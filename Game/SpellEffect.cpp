#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "SpellEffect.h"
#include "SpellManager.h"
#include "SaveLoader.h"

/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
// **********               class CSpellEffect functions                 ********** //
// ******************************************************************************** //
/////////////////////////////////////////////////////////////////////////////////////
CSpellEffect::CSpellEffect()
{
    nSpellID = 0;
    nVisStrength = 0;
    nDuration = 0;
    fpPower = 0;
    bQuiet = false;
}
/////////////////////////////////////////////////////////////////////////////////////
int CSpellEffect::Segment()
{
    nDuration--;
    return (int)(nDuration > 0);
}
/////////////////////////////////////////////////////////////////////////////////////
void CSpellEffect::Load(CSave &file)
{
    file.Read(nSpellID);
    file.Read(nVisStrength);
    file.Read(nDuration);
    file.Read(fpPower);
    file.Read(fpRadius);
}
/////////////////////////////////////////////////////////////////////////////////////
void CSpellEffect::Store(CSave &file)
{
    file.Write(nSpellID);
    file.Write(nVisStrength);
    file.Write(nDuration);
    file.Write(fpPower);
    file.Write(fpRadius);
}
/////////////////////////////////////////////////////////////////////////////////////
