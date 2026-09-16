#pragma once

#include "Database.h"
#include "ldbFormat.h"

struct SLeverDatabase
{
public:
    CDatabase<SLDBPrototype> levers;

public:
    BOOL Load(LPCSTR pszPath);
};

extern SLeverDatabase theLeverDatabase;
