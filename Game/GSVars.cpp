#include "StdAfx.h"
#include "GameJournal.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "GSVars.h"
#include "ModRuntime.h"

/////////////////////////////////////////////////////////////////////////////////////
// диапазон в котором выдаются значения функцией GetPersonageIDByName
const int N_FIRST_PERS_ID = 1000000000;
const int N_PERS_ID_NUMBER = 1000000000;
/////////////////////////////////////////////////////////////////////////////////////
// CGSVariableSet
/////////////////////////////////////////////////////////////////////////////////////
float CGSVariableSet::GetVar(const char *pszName)
{
    const_iterator it = find(pszName);
    if (it != end())
        return it->second;
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CGSVariableSet::SetVar(const char *pszName, float fpVal)
{
    if (pszName[0] == 0)
        return;
    if (fpVal == 0)
        DelVar(pszName);
    else
    {
        GameJournal::Variable(this, pszName, find(pszName) != end(), GetVar(pszName), fpVal, false);
        (*this)[pszName] = fpVal;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// Native PersistentGlobalVars applies only at the three LMP clear consumers.
// A typed mod session owns addon behavior; ordinary stock launches keep clear().
void ClearLMPPartyVariables(CGSVariableSet &vars)
{
    if (!ModRuntime::Current())
    {
        vars.clear();
        return;
    }
    CGSVariableSet globals;
    vars.GetVars("gvar.", globals);
    vars.clear();
    for (const auto &entry : globals)
    {
        // Retail SetVar's x87 equal/unordered branch deletes zero and NaN.
        // Keep this adapter local; do not change the general variable API.
        float value = entry.second;
        vars.SetVar(entry.first.c_str(), value == value ? value : 0.0f);
    }
}
void CGSVariableSet::DelVar(const char *pszName)
{
    GameJournal::Variable(this, pszName, find(pszName) != end(), GetVar(pszName), 0, true);
    erase(pszName);
}
/////////////////////////////////////////////////////////////////////////////////////
void CGSVariableSet::GetVars(const char *pszName, CGSVariableSet &resVars)
{
    int nLeng = strlen(pszName);
    resVars.clear();
    for (auto it = begin(); it != end(); ++it)
    {
        if (strncmp(pszName, it->first.c_str(), nLeng) == 0)
            resVars[it->first] = it->second;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CGSVariableSet::Load(CSave &file)
{
    clear();
    for (;;)
    {
        float fpVal;
        CString szName;
        file.Read(szName);
        if (szName == "")
            break;
        file.Read(fpVal);
        (*this)[szName.GetString()] = fpVal;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CGSVariableSet::Store(CSave &file)
{
    for (auto it = begin(); it != end(); ++it)
    {
        file.Write(CString(it->first.c_str()));
        file.Write(it->second);
    }
    file.Write(CString(""));
}
/////////////////////////////////////////////////////////////////////////////////////
// personage ID functions
int GetPersonageIDByName(const char *pszGlobalName)
{
    const unsigned char *s = (const unsigned char *)pszGlobalName;
    unsigned long h = 0;
    for (; *s; s++)
        h = 5 * h + toupper(*s);
    return N_FIRST_PERS_ID + (h % N_PERS_ID_NUMBER);
}
/////////////////////////////////////////////////////////////////////////////////////
bool IsIDofPersonage(int nID)
{
    return nID >= N_FIRST_PERS_ID && nID < N_FIRST_PERS_ID + N_PERS_ID_NUMBER;
}
/////////////////////////////////////////////////////////////////////////////////////
