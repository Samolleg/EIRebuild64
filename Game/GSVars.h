#pragma once

#include "SaveLoader.h"
#include <unordered_map>

/////////////////////////////////////////////////////////////////////////////////////
// typedef std::hash<char*> CharHash;
typedef std::unordered_map<std::string, float> CGSVarsHash;

// --------------------------------------------------------------------------------------------
// набор переменных
// --------------------------------------------------------------------------------------------
class CGSVariableSet : public CGSVarsHash
{
public:
    float GetVar(const char *pszName);
    void SetVar(const char *pszName, float fpVal);
    void DelVar(const char *pszName);
    void GetVars(const char *pszName, CGSVariableSet &res);
    void Load(CSave &file);
    void Store(CSave &file);
};
void ClearLMPPartyVariables(CGSVariableSet &vars);
/////////////////////////////////////////////////////////////////////////////////////
// функция пребразования глобального имени персонажа в его идентификатор
int GetPersonageIDByName(const char *pszGlobalName);
bool IsIDofPersonage(int nID);
/////////////////////////////////////////////////////////////////////////////////////
