#pragma once

#include "Database.h"
#include "idbFormat.h"

struct SItemDatabase
{
public:
    CDatabase<SIDBMaterial> materials;
    CDatabase<SIDBWeaponShape> weapons;
    CDatabase<SIDBArmorShape> armors;
    CDatabase<SIDBQuickItemShape> quickItems;
    CDatabase<SIDBQuestItemShape> questItems;
    CDatabase<SIDBLootItemShape> lootItems;

public:
    void Clear()
    {
        SItemDatabase::~SItemDatabase();
        ::new (this) SItemDatabase;
    }
    BOOL Load(LPCSTR pszPath, LPCSTR pszFName);
};

extern SItemDatabase theItemDatabase;
