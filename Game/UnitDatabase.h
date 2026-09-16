#pragma once
/////////////////////////////////////////////////////////////////////////////////////
#include "Database.h"
#include "udbFormat.h"
/////////////////////////////////////////////////////////////////////////////////////
class CSoundManager;
struct SUnitDatabase
{
public:
    CDatabase<SUDBUnitPrototype> prototypes;
    CDatabase<SUDBNPCPrototype> npcs;
    CDatabase<SUDBRaceModel> races;
    CDatabase<SUDBHitLocationType> hlts;

public:
    void SetupWithSounds(CSoundManager *pSoundManager);
    void Clear()
    {
        SUnitDatabase::~SUnitDatabase();
        ::new (this) SUnitDatabase;
    }

    BOOL Load(LPCSTR pszPath, LPCSTR pszFName);
};
/////////////////////////////////////////////////////////////////////////////////////
extern SUnitDatabase theUnitDatabase;
