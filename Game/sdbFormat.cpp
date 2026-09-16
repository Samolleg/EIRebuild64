#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "sdbFormat.h"

#include "BasicChunk.h"
/////////////////////////////////////////////////////////////////////////////////////
void SSDBModifier::Serialize(CStructureSaver &file)
{
    file.Add(0, szName);
    file.Add(1, dwCode);
    file.Add(2, fpPrice);
    file.AddRaw(3, &smtType, sizeof(smtType));
    file.Add(4, fpMana);
    file.Add(5, fpValue);
    file.Add(6, dwComplexity);
    file.Add(7, szShop);
    file.Add(8, dwAvailableInShop);
}
/////////////////////////////////////////////////////////////////////////////////////
void SSDBSpellPrototype::Serialize(CStructureSaver &file)
{
    file.Add(0, szName);
    file.Add(1, szCodeName);
    file.Add(2, szSubtype);
    file.Add(3, fpPrice);
    file.AddRaw(4, &msSchool, sizeof(msSchool));
    file.Add(5, fpMana);
    file.Add(6, dwSlots);
    file.Add(7, fpSpeed);
    file.Add(8, fpRange);
    file.Add(9, fpArea);
    file.Add(10, fpEffect);
    file.Add(11, dwTargeting);
    file.Add(12, dwNumTargets);
    file.Add(13, dwDuration);
    file.Add(14, dwActions);
    file.Add(15, dwRequireTrace);
    file.AddT(16, basemods);
    file.AddT(17, mods);
    file.Add(18, dwTextureTypeIndex);
    file.AddRaw(19, &mstSubtype, sizeof(mstSubtype));

    file.AddRaw(20, bRuneAllowed, sizeof(bRuneAllowed));

    file.Add(21, dwComplexity);

    file.Add(22, dwAvailableInShop);
    //
    file.Add(23, fLightR);
    file.Add(24, fLightG);
    file.Add(25, fLightB);
    file.Add(26, fLightRadius);
    file.Add(27, fFadeoutTime);
}
/////////////////////////////////////////////////////////////////////////////////////
void SSDBComplexSpell::Serialize(CStructureSaver &file)
{
    file.Add(1, szPrototype);
    file.AddT(2, required);
    file.AddT(3, optional);
    file.Add(4, szPower);
    file.Add(5, dwAvailableInShop);
}
/////////////////////////////////////////////////////////////////////////////////////
