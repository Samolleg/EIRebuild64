#include "ScriptCompiler.h"

CScriptObject::CScriptObject()
{
    pObject = NULL;
}

void CScriptObject::Clear()
{
    if (pObject)
    {
        releaseObject(pObject);
    }
    pObject = NULL;
}

void CScriptObject::Copy(const CScriptObject &a)
{
    pObject = a.pObject;
    retainObject = a.retainObject;
    releaseObject = a.releaseObject;
    if (pObject)
        retainObject(pObject);
}

void SScriptVariablesSet::SetZeroValues()
{
    int i;
    for (i = 0; i < fVars.GetSize(); i++)
        fVars[i] = 0;
    for (i = 0; i < szVars.GetSize(); i++)
        szVars[i] = "";
    for (i = 0; i < oVars.GetSize(); i++)
        oVars[i].Reset();
    for (i = 0; i < gVars.GetSize(); i++)
        gVars[i].Clear();
}

BOOL SScriptVariablesSet::IsVariableName(const char *pszName)
{
    if (fVars.GetVariableIndex(pszName) >= 0)
        return TRUE;
    if (szVars.GetVariableIndex(pszName) >= 0)
        return TRUE;
    if (oVars.GetVariableIndex(pszName) >= 0)
        return TRUE;
    if (gVars.GetVariableIndex(pszName) >= 0)
        return TRUE;
    return FALSE;
}

BOOL SScriptVariablesSet::GetVariable(const char *pszName, int &nNumber,
                                      EScriptParameterType &eType)
{
    eType = ptFloat;
    nNumber = fVars.GetVariableIndex(pszName);
    if (nNumber >= 0)
        return TRUE;
    eType = ptString;
    nNumber = szVars.GetVariableIndex(pszName);
    if (nNumber >= 0)
        return TRUE;
    eType = ptObject;
    nNumber = oVars.GetVariableIndex(pszName);
    if (nNumber >= 0)
        return TRUE;
    eType = ptGroup;
    nNumber = gVars.GetVariableIndex(pszName);
    if (nNumber >= 0)
        return TRUE;
    return FALSE;
}

void SScriptVariablesSet::AddVars(const SScriptVariablesSet &a)
{
    fVars.AddVars(a.fVars);
    szVars.AddVars(a.szVars);
    oVars.AddVars(a.oVars);
    gVars.AddVars(a.gVars);
}

void SSCCmd::Clear()
{
    command = SC_UNKNOWN;
    params.SetSize(0);
    szParams.SetSize(0);
    oParams.SetSize(0);
    gParams.SetSize(0);
    localParameters.SetSize(0);
    globalParameters.SetSize(0);
    funcParameters.SetSize(0);
    pLoopBody = NULL;
}

BOOL SOneScript::AddParameter(CString &szParam, EScriptParameterType eType)
{
    BOOL bRv;
    CString szType;
    switch (eType)
    {
    case ptFloat:
        bRv = vars.fVars.AddVariable(szParam);
        szType = "f";
        break;
    case ptString:
        bRv = vars.szVars.AddVariable(szParam);
        szType = "s";
        break;
    case ptObject:
        bRv = vars.oVars.AddVariable(szParam);
        szType = "o";
        break;
    case ptGroup:
        bRv = vars.gVars.AddVariable(szParam);
        szType = "g";
        break;
    }
    if (bRv)
        szScriptParams = szScriptParams + szType;
    return bRv;
}

SScriptThread *CScriptCompiler::AllocateNewThread()
{
    SScriptThread *pRes = &threads.GetAt(threads.AddTail(SScriptThread()));
    pRes->SetNumber(threads.GetCount());
    return pRes;
}

SScriptThread *CScriptCompiler::AllocateNewWorldThread()
{
    SScriptThread *pRes = &worldActions.GetAt(worldActions.AddTail(SScriptThread()));
    pRes->SetNumber(worldActions.GetCount() | 0x80000000);
    return pRes;
}

SScriptThread *CScriptCompiler::GetThreadByIndex(int nIdx)
{
    POSITION pos;
    if (nIdx & 0x80000000)
    {
        pos = worldActions.GetHeadPosition();
        while (pos)
        {
            SScriptThread &res = worldActions.GetNext(pos);
            if (res.GetNumber() == nIdx)
                return &res;
        }
    }
    else
    {
        pos = threads.GetHeadPosition();
        while (pos)
        {
            SScriptThread &res = threads.GetNext(pos);
            if (res.GetNumber() == nIdx)
                return &res;
        }
    }
    return NULL;
}

SOneScript *CScriptCompiler::GetScriptByName(const char *szName)
{
    if (!szName || szName[0] == 0)
        return NULL;
    for (int i = 0; i < scripts.GetSize(); i++)
    {
        if (scripts[i].szScriptName == szName)
            return &scripts[i];
    }
    return NULL;
}

HScriptThread CScriptCompiler::GetWorldScript()
{
    if (worldActions.GetHeadPosition() == NULL)
        worldActions.AddHead(SScriptThread());
    return &worldActions.GetHead();
}

SOneScript *CScriptCompiler::GetScript(const char *szName)
{
    for (int i = 0; i < scripts.GetSize(); i++)
    {
        if (scripts[i].szScriptName == szName)
            return &scripts[i];
    }
    return NULL;
}

BOOL CScriptCompiler::ParseGlobalVars(CString szVars)
{
    SOneScript objScript;
    CLexemeList lexemes;

    objScript.Clear();
    szVars = szVars + "\n)";
    if (!CreateLexemesList(szVars, lexemes))
        return FALSE;
    POSITION pos = lexemes.GetHeadPosition();
    if (!LoadParameters(objScript, pos, lexemes, TRUE))
        return FALSE;
    if (pos != 0)
        return ScriptError(lexemes.GetAt(pos).nLine, SC_UNEXPECTED_SYMBOL,
                           "Symbols behind variables declaration");
    objScript.vars.SetZeroValues(); // this guarantees that execution will start in identical
                                    // environment every time
    globalVars.AddVars(objScript.vars);
    return TRUE;
}

BOOL CScriptCompiler::CreateOneScript(const char *pszOneScriptName, CString szParams)
{
    SOneScript objScript;
    CLexemeList lexemes;

    objScript.szScriptName = SignName(pszOneScriptName);
    if (GetScriptIdx(objScript.szScriptName) != -1)
        return ScriptError(0, SC_DUPLICATE_SCRIPT, "Duplicate script %s", pszOneScriptName);
    szParams = szParams + "\n)";
    if (!CreateLexemesList(szParams, lexemes))
        return FALSE;
    POSITION pos = lexemes.GetHeadPosition();
    if (!LoadParameters(objScript, pos, lexemes))
        return FALSE;
    scripts.Add(objScript);
    return TRUE;
}

BOOL CScriptCompiler::CreateCondition(const char *pszOneScriptName, HScriptThread &pIfThread,
                              HScriptThread &pThenThread)
{
    SOneScript *pOneScript;
    SScriptCondition condition;
    pOneScript = GetScript(SignName(pszOneScriptName));
    if (!pOneScript)
        return FALSE;
    // allocate new thread in list of all threads and load conditions in it
    condition.pConditions = AllocateNewThread();
    pIfThread = condition.pConditions;
    // allocate new thread in list of all threads and load commands in it
    condition.pActions = AllocateNewThread();
    pThenThread = condition.pActions;
    pOneScript->conditions.Add(condition);
    return TRUE;
}

BOOL CScriptCompiler::ParseIfThread(const char *pszOneScriptName, HScriptThread pThread, CString szSrc)
{
    CLexemeList lexemes;
    SOneScript *pOneScript;

    pOneScript = GetScript(SignName(pszOneScriptName));
    if (!pOneScript)
        return FALSE;
    szSrc = szSrc + "\n)";
    if (!CreateLexemesList(szSrc, lexemes))
        return FALSE;
    POSITION pos = lexemes.GetHeadPosition();
    if (!LoadConditionsList(*pThread, pos, lexemes, pOneScript->vars))
        return FALSE;
    if (pos != 0)
        return ScriptError(lexemes.GetAt(pos).nLine, SC_UNEXPECTED_SYMBOL,
                           "Symbols behind conditions list");
    return TRUE;
}

BOOL CScriptCompiler::ParseCommandThread(const char *pszOneScriptName, HScriptThread pThread, CString szSrc)
{
    CLexemeList lexemes;
    SOneScript *pOneScript = NULL;

    if (pszOneScriptName)
    {
        pOneScript = GetScript(SignName(pszOneScriptName));
        if (!pOneScript)
            return FALSE;
    }
    szSrc = szSrc + "\n)";
    if (!CreateLexemesList(szSrc, lexemes))
        return FALSE;
    POSITION pos = lexemes.GetHeadPosition();
    if (pOneScript)
    {
        if (!LoadCommandsList(*pThread, pos, lexemes, &pOneScript->vars))
            return FALSE;
    }
    else
    {
        if (!LoadCommandsList(*pThread, pos, lexemes, NULL))
            return FALSE;
    }
    if (pos != 0)
        return ScriptError(lexemes.GetAt(pos).nLine, SC_UNEXPECTED_SYMBOL,
                           "Symbols behind commands list");
    return TRUE;
}

CScriptCompiler::CScriptCompiler(ScriptLanguage context, void (&report)(const char *))
    : language(context), reportInvariant(report)
{
    ClearCompiled();
}

void CScriptCompiler::ResetCompilationStatus()
{
    bLoaded = FALSE;
    nSecondaryCount = 0;
    scriptError.text = "Unknown error";
    scriptError.eError = SC_NO_ERROR;
    scriptError.nLine = 0;
}

void CScriptCompiler::ClearCompilationGlobals()
{
    worldActions.RemoveAll();
    globalVars.Clear();
}

void CScriptCompiler::ClearCompilationScripts()
{
    scripts.RemoveAll();
    while (!threads.IsEmpty())
        threads.RemoveHead();
}

void CScriptCompiler::ClearCompiled()
{
    ResetCompilationStatus();
    ClearCompilationGlobals();
    ClearCompilationScripts();
}
