#pragma once

#include "Database.h"
#include "idbFormat.h"
#include <map>

struct SItemDatabase
{
private:
    // Parsed from items.idb field 28; owned by the database, never saved with items.
    std::vector<std::map<std::string, int>> quickMaterialGraphics;

public:
    std::string sourceArchive; // actual items.idb provider; transient, not serialized
    CDatabase<SIDBMaterial> materials;
    CDatabase<SIDBWeaponShape> weapons;
    CDatabase<SIDBArmorShape> armors;
    CDatabase<SIDBQuickItemShape> quickItems;
    CDatabase<SIDBQuestItemShape> questItems;
    CDatabase<SIDBLootItemShape> lootItems;

public:
    void Clear()
    {
        // Keep the old owner alive if constructing the empty containers throws.
        SItemDatabase empty;
        *this = std::move(empty);
    }
    BOOL Load(LPCSTR pszPath, LPCSTR pszFName);
    int GetQuickGraphicsIndex(DWORD shape, DWORD material) const;
};

extern SItemDatabase theItemDatabase;
