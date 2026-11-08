#include "StdAfx.h"
#include "Script.h"
#include "ModRuntime.h"
#include "ModMobRegistry.h"
#include "ScriptCommandArguments.h"
#include "ScriptLanguageRuntime.h"
BOOL CScript::AppendText(CString &szFile)
{
    if (!LoadText(szFile, FALSE))
        return FALSE;
    nSecondaryCount--;
    return TRUE;
}

BOOL CScript::Load(const char *pszFileName)
{
    CString file;
    if (FileToCString(pszFileName, file))
        return LoadText(file);
    /*	{ // check for StoreText()
                    if( LoadText( file ) )
                    {
                            StoreText( file );
                            CStdioFile f1;
                            f1.Open( "decompile.scr", CFile::modeCreate | CFile::modeWrite |
       CFile::typeText ); f1.WriteString( file ); f1.Close(); return TRUE; } else return FALSE;
            }*/
    return ScriptError(0, SC_CANT_READ_FILE, "Unable to read file \"%s\"", pszFileName);
}

const char *CScript::CutNumber(const char *pszName)
{
    int nNumber;
    const char *pszRes = ReadNumber(pszName, nNumber);
    if (nNumber > 0 && nNumber != nSecondaryCount - 1)
    { // return zero string then
        while (pszRes[0])
            pszRes++;
    }
    return pszRes;
}

const char *CScript::ReadNumber(const char *pszName, int &nNumber)
{
    CString str;
    const char *pszNumber, *pszAllName;
    pszAllName = pszName;
    nNumber = -1;
    if (pszName[0] == '!')
    {
        pszName++;
        str = pszName;
        pszNumber = str;
        while (pszNumber[0])
        {
            if (pszNumber[0] == '!')
            {
                str.SetAt(pszNumber - str, 0);
                nNumber = atoi(str);
                return pszName + 1;
            }
            pszNumber++;
            pszName++;
        }
        return pszAllName;
    }
    return pszName;
}

void CScript::StoreTextVars(BOOL bOneLine, CString &res, SScriptVariablesSet &vars,
                            const char *pszParams, BOOL bStoreGlobalVars)
{
    int i, nFloatParam = 0, nStringParam = 0, nObjectParam = 0, nGroupParam = 0;
    int nScriptNumber;
    CString szVarName, szFormat, szNextLine, szVarType;
    CAArray<CString> varDefinitions;

    if (!bOneLine)
        szNextLine = "\n";
    res = res + " (" + szNextLine;
    if (!pszParams)
    {
        for (i = 0; i < vars.fVars.GetSize(); i++)
            szFormat = szFormat + "f";
        for (i = 0; i < vars.szVars.GetSize(); i++)
            szFormat = szFormat + "s";
        for (i = 0; i < vars.oVars.GetSize(); i++)
            szFormat = szFormat + "o";
        for (i = 0; i < vars.gVars.GetSize(); i++)
            szFormat = szFormat + "g";
        pszParams = szFormat;
    }
    while (pszParams[0])
    {
        switch (pszParams[0])
        {
        case 'f':
            szVarName = vars.fVars.GetName(nFloatParam++);
            szVarType = "float";
            break;
        case 's':
            szVarName = vars.szVars.GetName(nStringParam++);
            szVarType = "string";
            break;
        case 'o':
            szVarName = vars.oVars.GetName(nObjectParam++);
            szVarType = "object";
            break;
        case 'g':
            szVarName = vars.gVars.GetName(nGroupParam++);
            szVarType = "group";
            break;
        }
        if (bStoreGlobalVars)
        {
            szVarName = ReadNumber(szVarName, nScriptNumber);
            // если это вторичный скрипт и это не его глобальная переменная писать ее не надо
            if (nSecondaryCount > 1 && nScriptNumber != nSecondaryCount - 1)
                szVarName = "";
            if (szVarName != "")
                varDefinitions.Add("  " + szVarName + " : " + szVarType);
        }
        else
        {
            szVarName = CutNumber(szVarName);
            if (szVarName != "")
                varDefinitions.Add("  " + szVarName + " : " + szVarType);
        }
        pszParams++;
    }
    // copy vars definitions from array to result text
    for (i = 0; i < varDefinitions.GetSize(); i++)
    {
        if (i != varDefinitions.GetSize() - 1)
            res = res + varDefinitions[i] + "," + szNextLine;
        else
            res = res + varDefinitions[i] + " ";
    }
    if (szFormat.GetLength() == 0)
        res = res + ")" + szNextLine;
    else
        res = res + szNextLine + ")" + szNextLine;
}

void CScript::StoreTextFunction(CString &res, SSCCmd cmd, SOneScript *pOneScript)
{
    BOOL bFound;
    int i, nFloatParam = 0, nStringParam = 0, nObjectParam = 0, nGroupParam = 0;
    CString szSuppose;       // значение параметра в тексте
    CString szFormat;        // формат для записи оператора присвоения и запуска
    SOneScript *pFindScript; // для определения формата вызова скрипта в случае RUNSCRIPT
    EScriptParameterType eType;
    const char *pszParams = GetCommandParams(language, (ESCCmd)cmd.command);
    std::string concreteModFormat;
    if(language.extended)if(auto*entry=ModMob::Find(cmd.command))if(entry->ready){
        const char*format=entry->format;if(*format&&strchr("FSOG",*format))++format;
        concreteModFormat=ModMob::Expand(format,cmd);pszParams=concreteModFormat.c_str();
    }
    // особые случаи : runscript & assignment
    if (cmd.command == SC_RUNSCRIPT)
    {
        res = res + CutNumber(cmd.szParams[0]) + "( ";
        pFindScript = GetScript(cmd.szParams[0]);
        nStringParam = 1;
        nFloatParam = 1;
        if (pFindScript)
        {
            szFormat = pFindScript->szScriptParams;
            pszParams = szFormat;
        }
    }
    else if (cmd.command == SC_GLOBALASSIGN)
    {
        switch ((int)cmd.params[1])
        {
        case 0:
            res = res + CutNumber(globalVars.fVars.GetName((int)cmd.params[0])) + " = ";
            break;
        case 1:
            res = res + CutNumber(globalVars.szVars.GetName((int)cmd.params[0])) + " = ";
            break;
        case 2:
            res = res + CutNumber(globalVars.oVars.GetName((int)cmd.params[0])) + " = ";
            break;
        case 3:
            res = res + CutNumber(globalVars.gVars.GetName((int)cmd.params[0])) + " = ";
            break;
        }
        cmd.params.RemoveAt(0);
        cmd.params.RemoveAt(0);
        // Assignment metadata occupies the first two float slots. Keep exported references aligned.
        for(int i=0;i<cmd.localParameters.GetSize();++i)if(cmd.localParameters[i].eType==ptFloat)cmd.localParameters[i].nNumber-=2;
        for(int i=0;i<cmd.globalParameters.GetSize();++i)if(cmd.globalParameters[i].eType==ptFloat)cmd.globalParameters[i].nNumber-=2;
        for(int i=0;i<cmd.funcParameters.GetSize();++i)if(cmd.funcParameters[i].eType==ptFloat)cmd.funcParameters[i].nNumber-=2;
        if (cmd.params.GetSize())
            szFormat = "f";
        else if (cmd.szParams.GetSize())
            szFormat = "s";
        else if (cmd.oParams.GetSize())
            szFormat = "o";
        else if (cmd.gParams.GetSize())
            szFormat = "g";
        pszParams = szFormat;
    }
    else
        res = res + GetCmdName(language, (ESCCmd)cmd.command) + "( ";
    if (pszParams == NULL)
        return;
    while (pszParams[0])
    {
        int nNumber;
        switch (pszParams[0])
        {
        case 'f': {
            eType = ptFloat;
            nNumber = nFloatParam++;
            float fValue = cmd.params[nFloatParam - 1];
            if (fValue == Float2Int(fValue))
                szSuppose.Format("%i", Float2Int(fValue));
            else
                szSuppose.Format("%g", fValue);
        }
        break;
        case 's':
            eType = ptString;
            nNumber = nStringParam++;
            szSuppose = CString("\"") + cmd.szParams[nStringParam - 1] + "\"";
            break;
        case 'O':
        case 'o':
            eType = ptObject;
            nNumber = nObjectParam++;
            szSuppose = "ShitHappens";
            break;
        case 'g':
            eType = ptGroup;
            nNumber = nGroupParam++;
            szSuppose = "ShitHappens";
            break;
        }
        bFound = FALSE;
        for (i = 0; i < cmd.localParameters.GetSize(); i++)
        {
            if (cmd.localParameters[i].eType == eType && cmd.localParameters[i].nNumber == nNumber)
            {
                switch (eType)
                {
                case ptFloat:
                    res = res + pOneScript->vars.fVars.GetName(cmd.localParameters[i].nVarNumber);
                    break;
                case ptString:
                    res = res + pOneScript->vars.szVars.GetName(cmd.localParameters[i].nVarNumber);
                    break;
                case ptObject:
                    res = res + pOneScript->vars.oVars.GetName(cmd.localParameters[i].nVarNumber);
                    break;
                case ptGroup:
                    res = res + pOneScript->vars.gVars.GetName(cmd.localParameters[i].nVarNumber);
                    break;
                }
                bFound = TRUE;
                break;
            }
        }
        for (i = 0; i < cmd.globalParameters.GetSize(); i++)
        {
            if (cmd.globalParameters[i].eType == eType &&
                cmd.globalParameters[i].nNumber == nNumber)
            {
                switch (eType)
                {
                case ptFloat:
                    res = res +
                          CutNumber(globalVars.fVars.GetName(cmd.globalParameters[i].nVarNumber));
                    break;
                case ptString:
                    res = res +
                          CutNumber(globalVars.szVars.GetName(cmd.globalParameters[i].nVarNumber));
                    break;
                case ptObject:
                    res = res +
                          CutNumber(globalVars.oVars.GetName(cmd.globalParameters[i].nVarNumber));
                    break;
                case ptGroup:
                    res = res +
                          CutNumber(globalVars.gVars.GetName(cmd.globalParameters[i].nVarNumber));
                    break;
                }
                bFound = TRUE;
                break;
            }
        }
        for (i = 0; i < cmd.funcParameters.GetSize(); i++)
        {
            if (cmd.funcParameters[i].eType == eType && cmd.funcParameters[i].nNumber == nNumber)
            {
                StoreTextFunction(res, *cmd.funcParameters[i].cmd, pOneScript);
                bFound = TRUE;
                break;
            }
        }
        if (!bFound)
            res = res + szSuppose;
        pszParams++;
        if (pszParams[0])
            res = res + ", ";
    }
    if (cmd.command != SC_GLOBALASSIGN)
        res = res + " ) ";
}

// вернуть строку из nNumber пробелов
static CString GetNSpaces(int nNumber)
{
    CString res;
    for (int i = 0; i < nNumber; i++)
        res = res + " ";
    return res;
}

void CScript::StoreTextThread(int nIndent, CString &res, SScriptThread &thread,
                              SOneScript *pOneScript)
{
    int i;
    res = res + GetNSpaces(nIndent) + "(\n";
    for (i = 0; i < thread.commands.GetSize(); i++)
    {
        res = res + GetNSpaces(nIndent + 2);
        StoreTextFunction(res, thread.commands[i], pOneScript);
        if (thread.commands[i].command == SC_FOR || thread.commands[i].command == SC_FORIF)
        {
            ASSERT(thread.commands[i].pLoopBody);
            res = res + "\n";
            StoreTextThread(nIndent + 2, res, *thread.commands[i].pLoopBody, pOneScript);
        }
        else
            res = res + "\n";
    }
    res = res + GetNSpaces(nIndent) + ")\n";
}

void CScript::StoreText(CString &res)
{
    SetLanguage(CurrentScriptLanguage());
    int i, k;
    POSITION pos;
    res = "";
    // save global vars
    res = "GlobalVars";
    StoreTextVars(FALSE, res, globalVars, NULL, TRUE);
    res = res + "\n";
    // declare all scripts
    for (i = 0; i < scripts.GetSize(); i++)
    {
        if (scripts[i].conditions.GetSize() == 0)
            continue; // скрипты без условий обычно заголовки от первичных скриптов
        res = res + "DeclareScript " + CutNumber(scripts[i].szScriptName);
        StoreTextVars(TRUE, res, scripts[i].vars, scripts[i].szScriptParams);
        res = res + "\n";
    }
    res = res + "\n";
    // write down all scripts
    for (i = 0; i < scripts.GetSize(); i++)
    {
        if (scripts[i].conditions.GetSize() == 0)
            continue; // скрипты без условий обычно заголовки от первичных скриптов
        res = res + "Script " + CutNumber(scripts[i].szScriptName) + "\n(\n";
        for (k = 0; k < scripts[i].conditions.GetSize(); k++)
        {
            res = res + "  if\n";
            StoreTextThread(2, res, *scripts[i].conditions[k].pConditions, &scripts[i]);
            res = res + "  then\n";
            StoreTextThread(2, res, *scripts[i].conditions[k].pActions, &scripts[i]);
        }
        res = res + ")\n\n";
    }
    // write worldscript
    pos = worldActions.GetHeadPosition();
    if (pos)
    {
        res = res + "WorldScript\n";
        StoreTextThread(0, res, worldActions.GetAt(pos), NULL);
    }
}

void CScript::PrepareForSecondary()
{
    StopExecution();
    for (int i = 0; i < scripts.GetSize(); i++)
        scripts[i].conditions.SetSize(0);
    worldActions.RemoveAll();
}

// ---------------------------------- File loading --------------------------------------------
BOOL FileToCString(const char *pszFileName, CString &res)
{
    BOOL bRv;
    TRY
    {
        res = "";
        bRv = TRUE;
        CStdioFile fin(pszFileName, CFile::modeRead | CFile::shareDenyWrite | CFile::typeText);
        char readBuf[2048];
        while (fin.ReadString(readBuf, sizeof(readBuf)) != NULL)
            res = res + CString(readBuf); // + "\n";
        fin.Close();
    }
    CATCH(CFileException, e)
    {
        bRv = FALSE;
    }
    END_CATCH;
    return bRv;
}
