#pragma once
/////////////////////////////////////////////////////////////////////////////////////
#include "Database.h"
#include "sdbFormat.h"
/////////////////////////////////////////////////////////////////////////////////////
struct SSpellDatabase
{
public:
    CDatabase<SSDBSpellPrototype> spells;
    CDatabase<SSDBModifier> runes;
    //
    std::vector<SSDBComplexSpell> cspells;
    std::vector<SSDBComplexSpell> carmors;
    std::vector<SSDBComplexSpell> cweapons;

public:
    void Clear()
    {
        SSpellDatabase::~SSpellDatabase();
        ::new (this) SSpellDatabase;
    }
    BOOL Load(LPCSTR pszPath, LPCSTR pszFName);
};
/////////////////////////////////////////////////////////////////////////////////////
extern SSpellDatabase theSpellDatabase;
