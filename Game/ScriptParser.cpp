#include <afx.h>
#include <afxtempl.h>
#include <algorithm>
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "ScriptCompiler.h"
#include "ScriptParser.h"
#include "ModMobIfElse.h"
#include <vector>

/*==========================================================================================================
        Scripts parser implementation

        Nival Interactive, 1999
==========================================================================================================*/

// ------------------------------ Load scripts helpers
// -----------------------------------------------------

LPCSTR PSZ_COMMENT_START = "/*";
LPCSTR PSZ_COMMENT_END = "*/";
LPCSTR PSZ_COMMENT_LINE = "//";
char C_STRING_SEPARATOR = '\"';

// ------------------------- Simple preprocessor ---------------------------
struct SDefine
{
    CString id, text;
};

typedef CList<SDefine, SDefine &> CDefineList;

static void ReplaceDefines(CString &text, CDefineList &defines)
{
    int nIdx;
    POSITION pos = defines.GetHeadPosition();
    while (pos)
    {
        SDefine &def = defines.GetNext(pos);
        while (1)
        {
            nIdx = text.Find(def.id);
            if (nIdx < 0)
                break;
            text = text.Left(nIdx) + def.text +
                   text.Right(text.GetLength() - nIdx - def.id.GetLength());
        }
    }
}

// insert to keep list sorted
static void InsertDefine(CDefineList &defines, SDefine &def)
{
    POSITION pos, prevpos = NULL;
    pos = defines.GetHeadPosition();
    while (pos)
    {
        prevpos = pos;
        if (defines.GetNext(pos).id.GetLength() < def.id.GetLength())
            break;
    }
    if (prevpos == NULL)
        defines.AddTail(def);
    else
        defines.InsertBefore(prevpos, def);
}

static void PreProcess(CString &text)
{
    CList<SDefine, SDefine &> defines;
    CString res, line;
    SDefine def;
    char *szText = text.LockBuffer(), *szLine, *szID;
    while (szText[0])
    {
        szLine = szText;
        while (szText[0] && szText[0] != '\n')
            szText++;
        if (szText[0])
        {
            szText[0] = 0;
            szText++;
        }
        line = szLine;
        if (strncmp(szLine, "#define", strlen("#define")) == 0)
        {
            def.id = "";
            def.text = "";
            szLine += strlen("#define");
            while (szLine[0] && isspace(static_cast<unsigned char>(szLine[0])))
                szLine++;
            szID = szLine;
            while (szLine[0] && !isspace(static_cast<unsigned char>(szLine[0])))
                szLine++;
            if (szLine[0])
            {
                szLine[0] = 0;
                szLine++;
            }
            def.id = szID;
            while (szLine[0] && isspace(static_cast<unsigned char>(szLine[0])))
                szLine++;
            def.text = szLine;
            InsertDefine(defines, def);
            line = "";
        }
        else
        {
            ReplaceDefines(line, defines);
        }
        res = res + line + "\n";
    }
    text.UnlockBuffer();
    text = res;
}

// **************************************************
//      Load script from file
// **************************************************
BOOL __cdecl CScriptCompiler::ScriptError(int nLine, EScriptParserError eError, const char *pszText, ...)
{
    char buffer[1024];
    va_list va;
    // compose error string
    va_start(va, pszText);
    wvsprintf(buffer, pszText, va);
    va_end(va);

    scriptError.nLine = nLine;
    scriptError.text = buffer;
    scriptError.eError = eError;
    return FALSE;
}

BOOL CScriptCompiler::CheckNextLexeme(CLexemeList &lexemes, POSITION &pos, int cmdForCheck)
{
    if (!pos)
        return ScriptError(lexemes.GetTail().nLine, SC_UNEXPECTED_EOF, "Unexpected end of file");
    SLexeme lexInfo = lexemes.GetNext(pos);
    CString lex = lexInfo.lexeme;
    ESCCmd cmd;
    GetLexemeCmd(language, lex, &cmd, NULL);
    if (cmd != cmdForCheck)
        return ScriptError(lexInfo.nLine, SC_WRONG_LEXEME, "Wrong lexeme %s", lex);
    return TRUE;
}

BOOL CScriptCompiler::CreateLexemesList(CString &szFile, CLexemeList &lexemes)
{
    BOOL bRv = TRUE;
    LPCSTR bufPtr = szFile; //.GetBuffer();
    lexemes.RemoveAll();
    int nCommentLevel = 0, nLine = 1;
    while (*bufPtr)
    {
        // Skip first whitespaces
        while (*bufPtr && isspace(static_cast<unsigned char>(*bufPtr)))
        {
            if (bufPtr[0] == '\n')
                nLine++;
            bufPtr++;
        }
        // Skip lexeme
        BOOL bStringStart = FALSE;
        LPCSTR lexemeStartPtr = bufPtr;
        while (*bufPtr && (!isspace(static_cast<unsigned char>(*bufPtr)) || (*bufPtr == ' ' && bStringStart)))
        {
            if (*bufPtr == C_STRING_SEPARATOR)
                bStringStart = !bStringStart;
            if ((*bufPtr == ',' || bufPtr[0] == ')' || bufPtr[0] == '(' || bufPtr[0] == ':' ||
                 bufPtr[0] == '=') &&
                bufPtr != lexemeStartPtr && !bStringStart)
                break;
            if (*bufPtr == '\n')
                break;
            bufPtr++;
        }
        // Create draft lexeme
        char lexeme[256];
        int nLexLen = static_cast<int>(std::min<ptrdiff_t>(255, bufPtr - lexemeStartPtr));
        strncpy(lexeme, lexemeStartPtr, nLexLen);
        if (nLexLen == 0)
            continue;
        lexeme[nLexLen] = 0;
        // Check lexeme for comments
        if (strncmp(lexeme, PSZ_COMMENT_START, strlen(PSZ_COMMENT_START)) == 0)
        {
            bRv = FALSE;
            while (*bufPtr)
            {
                if (*bufPtr == '\n')
                    nLine++;
                if (*bufPtr == C_STRING_SEPARATOR)
                    bStringStart = !bStringStart;
                if (!bStringStart && strncmp(bufPtr, PSZ_COMMENT_END, strlen(PSZ_COMMENT_END)) == 0)
                {
                    bufPtr += strlen(PSZ_COMMENT_END);
                    bRv = TRUE;
                    break;
                }
                bufPtr++;
            }
            if (!bRv)
            {
                bRv = ScriptError(nLine, SC_UNEXPECTED_EOF, "End of file before \'%s\'",
                                  PSZ_COMMENT_END);
                break;
            }
        }
        else
        {
            if (strnicmp(lexeme, PSZ_COMMENT_LINE, strlen(PSZ_COMMENT_LINE)) == 0)
            {
                while (*bufPtr)
                {
                    if (bufPtr[0] == '\n')
                        break;
                    bufPtr++;
                }
                continue;
            }
            if (nCommentLevel == 0)
            {
                // Create final lexeme
                int nTrailer;
                GetLexemeCmd(language, lexeme, NULL, &nTrailer);
                bufPtr -= nTrailer;
                lexemes.AddTail(SLexeme(CString(lexeme, strlen(lexeme) - nTrailer), nLine));
            }
        }
    }
    return bRv;
}

static int CharCount(const char *str, char c)
{
    int nRes = 0;
    while (str[0])
    {
        if (str[0] == c)
            nRes++;
        str++;
    }
    return nRes;
}

BOOL CScriptCompiler::LoadFunction(SSCCmd &res, POSITION &pos, CLexemeList &lexemes, int newCmd,
                           SScriptVariablesSet &vars, const char *pszParams, BOOL bLoadAssign,
                           BOOL bLoadRunScript)
{
    int nParam = 0, nStringParam = 0, nObjectParam = 0, nFloatParam = 0, nGroupParam = 0;
    SLexeme lexInfo;
    CString lex, lexSigned, szParamType;
    ESCCmd cmd;
    SScriptParam scrParam, gScrParam;
    BOOL bFailed;
    SScriptFuncParam funcParam;
    const char *pszNewParams;

    if (bLoadAssign)
        nFloatParam = 2;
    if (bLoadRunScript)
    {
        nFloatParam = 1;
        nStringParam = 1;
    }

    res.command = newCmd;
    res.params.SetSize(CharCount(pszParams, 'f') + nFloatParam);
    res.szParams.SetSize(CharCount(pszParams, 's') + nStringParam);
    res.oParams.SetSize(CharCount(pszParams, 'o') + CharCount(pszParams, 'O'));
    res.gParams.SetSize(CharCount(pszParams, 'g'));
    res.localParameters.SetSize(0);
    res.globalParameters.SetSize(0);
    res.funcParameters.SetSize(0);

    if (!pos)
        return ScriptError(lexemes.GetTail().nLine, SC_EXPECTED_SYMBOL_NOT_FOUND, "Expected \'%s\'",
                           GetCmdName(language, SC_OPEN_FUNCTION));

    while (pos)
    {
        lexInfo = lexemes.GetNext(pos);
        lex = lexInfo.lexeme;
        GetLexemeCmd(language, lex, &cmd, NULL);
        if (!bLoadAssign)
        {
            if (cmd == SC_CLOSE_FUNCTION)
            {
                if (pszParams[0] != 0 && pszParams[0] != '?' && pszParams[0] != '+')
                    return ScriptError(lexInfo.nLine, SC_NOTENOUGH_PARAMETERS,
                                       "Not enough parameters");
                res.params.SetSize(nFloatParam);res.szParams.SetSize(nStringParam);res.oParams.SetSize(nObjectParam);res.gParams.SetSize(nGroupParam);
                return TRUE;
            }
            if (nParam == 0)
            {
                if (cmd != SC_OPEN_FUNCTION)
                    return ScriptError(lexInfo.nLine, SC_EXPECTED_SYMBOL_NOT_FOUND,
                                       "Expected \'%s\'", GetCmdName(language, SC_OPEN_FUNCTION));
            }
            else
            {
                if (cmd != SC_COMMA)
                    return ScriptError(lexInfo.nLine, SC_EXPECTED_SYMBOL_NOT_FOUND,
                                       "Expected \'%s\'", GetCmdName(language, SC_COMMA));
            }

            lexInfo = lexemes.GetNext(pos);
            lex = lexInfo.lexeme;
            GetLexemeCmd(language, lex, &cmd, NULL);

            if (nParam == 0 && cmd == SC_CLOSE_FUNCTION)
            { // function has no parameters
                if (pszParams[0] != 0 && pszParams[0] != '?' && pszParams[0] != '+')
                    return ScriptError(lexInfo.nLine, SC_NOTENOUGH_PARAMETERS,
                                       "Not enough parameters");
                res.params.SetSize(nFloatParam);res.szParams.SetSize(nStringParam);res.oParams.SetSize(nObjectParam);res.gParams.SetSize(nGroupParam);
                return TRUE;
            }
        }

        if(*pszParams=='?')++pszParams;else if(*pszParams=='+')--pszParams;
        bFailed = FALSE;
        lexSigned = SignName(lex); // для обработки глобальных переменных вторичных скриптов
        switch (pszParams[0])
        {
        case 'f':
            if(res.params.GetSize()<=nFloatParam)res.params.SetSize(nFloatParam+1);
            scrParam.eType = ptFloat;
            gScrParam.eType = ptFloat;
            scrParam.nVarNumber = vars.fVars.GetVariableIndex(lex);
            gScrParam.nVarNumber = globalVars.fVars.GetVariableIndex(lex);
            if (gScrParam.nVarNumber < 0)
                gScrParam.nVarNumber = globalVars.fVars.GetVariableIndex(lexSigned);
            scrParam.nNumber = nFloatParam;
            gScrParam.nNumber = nFloatParam;
            if (cmd == SC_DIGIT)
                res.params[nFloatParam] = (float)atof(lex);
            else
                bFailed = TRUE;
            nFloatParam++;
            szParamType = "float";
            break;
        case 's':
            if(res.szParams.GetSize()<=nStringParam)res.szParams.SetSize(nStringParam+1);
            scrParam.eType = ptString;
            gScrParam.eType = ptString;
            scrParam.nVarNumber = vars.szVars.GetVariableIndex(lex);
            gScrParam.nVarNumber = globalVars.szVars.GetVariableIndex(lex);
            if (gScrParam.nVarNumber < 0)
                gScrParam.nVarNumber = globalVars.szVars.GetVariableIndex(lexSigned);
            scrParam.nNumber = nStringParam;
            gScrParam.nNumber = nStringParam;
            if (cmd == SC_STRING)
                res.szParams[nStringParam] = lex.Mid(1, lex.GetLength() - 2);
            else
                bFailed = TRUE;
            nStringParam++;
            szParamType = "string";
            break;
        case 'o':
            if(res.oParams.GetSize()<=nObjectParam)res.oParams.SetSize(nObjectParam+1);
            scrParam.eType = ptObject;
            gScrParam.eType = ptObject;
            scrParam.nVarNumber = vars.oVars.GetVariableIndex(lex);
            gScrParam.nVarNumber = globalVars.oVars.GetVariableIndex(lex);
            if (gScrParam.nVarNumber < 0)
                gScrParam.nVarNumber = globalVars.oVars.GetVariableIndex(lexSigned);
            scrParam.nNumber = nObjectParam;
            gScrParam.nNumber = nObjectParam;
            bFailed = TRUE;
            nObjectParam++;
            szParamType = "object";
            break;
        case 'O': // этот параметр должен быть глобальной переменной, обычно для использования в
                  // цикле
            scrParam.nVarNumber = -1;
            gScrParam.eType = ptObject;
            gScrParam.nVarNumber = globalVars.oVars.GetVariableIndex(lex);
            if (gScrParam.nVarNumber < 0)
                gScrParam.nVarNumber = globalVars.oVars.GetVariableIndex(lexSigned);
            if (gScrParam.nVarNumber < 0)
                return ScriptError(lexInfo.nLine, SC_ONLY_GLOBALVAR_ALLOWED,
                                   "Only global variables are allowed to be loop counters");
            gScrParam.nNumber = nObjectParam;
            bFailed = TRUE;
            nObjectParam++;
            szParamType = "object";
            break;
        case 'g':
            if(res.gParams.GetSize()<=nGroupParam)res.gParams.SetSize(nGroupParam+1);
            scrParam.eType = ptGroup;
            gScrParam.eType = ptGroup;
            scrParam.nVarNumber = vars.gVars.GetVariableIndex(lex);
            gScrParam.nVarNumber = globalVars.gVars.GetVariableIndex(lex);
            if (gScrParam.nVarNumber < 0)
                gScrParam.nVarNumber = globalVars.gVars.GetVariableIndex(lexSigned);
            scrParam.nNumber = nGroupParam;
            gScrParam.nNumber = nGroupParam;
            bFailed = TRUE;
            nGroupParam++;
            szParamType = "group";
            break;
        case 0:
            return ScriptError(lexInfo.nLine, SC_TOOMUCH_PARAMETERS, "Too much parameters");
            break;
        default:
            ReportInvariant("wrong type char in CScript::LoadFunction");
            bFailed = TRUE;
            break;
        }
        if (bFailed)
        {
            if (scrParam.nVarNumber < 0 && gScrParam.nVarNumber < 0)
            { // last chance - function as a parameter
                funcParam.Clear();
                funcParam.nNumber = scrParam.nNumber; // место куда записывать результат функции
                funcParam.eType = scrParam.eType;
                pszNewParams = GetFunctionParams(language, cmd, funcParam.eType);
                if (pszNewParams)
                {
                    if (!LoadFunction(*funcParam.cmd, pos, lexemes, cmd, vars, pszNewParams))
                        return FALSE;
                    res.funcParameters.Add(funcParam);
                }
                else
                {
                    return ScriptError(lexInfo.nLine, SC_PARAMETER_EXPECTED,
                                       "Parameter of type %s expected", (const char *)szParamType);
                }
            }
            else
            { // we got a parameter
                if (scrParam.nVarNumber < 0)
                    res.globalParameters.Add(gScrParam);
                else
                    res.localParameters.Add(scrParam);
            }
        }
        if (bLoadAssign)
            return TRUE;
        pszParams++;
        nParam++;
    }
    return ScriptError(lexemes.GetTail().nLine, SC_UNEXPECTED_EOF, "Unexpected end of file");
}

BOOL CScriptCompiler::LoadConditionsList(SScriptThread &res, POSITION &pos, CLexemeList &lexemes,
                                 SScriptVariablesSet &vars)
{
    SLexeme lexInfo;
    CString lex;
    ESCCmd cmd;
    const char *pszNewParams;
    SSCCmd scriptCmd;

    res.ClearCommands();
    while (1)
    {
        BOOL bRv = FALSE;

        if (!pos)
            return ScriptError(lexInfo.nLine, SC_UNEXPECTED_EOF, "Unexpected end of file");
        scriptCmd.Clear();
        lexInfo = lexemes.GetNext(pos);
        lex = lexInfo.lexeme;
        GetLexemeCmd(language, lex, &cmd, NULL);

        if (cmd == SC_CLOSE_FUNCTION)
            break;
        pszNewParams = GetFunctionParams(language, cmd, ptFloat);
        if (pszNewParams)
        {
            if (!LoadFunction(scriptCmd, pos, lexemes, cmd, vars, pszNewParams))
                return FALSE;
        }
        else
            return ScriptError(lexInfo.nLine, SC_FLOAT_FUNCTION_EXPECTED,
                               "Function returning float expected");
        res.Add(scriptCmd);
    }
    return TRUE;
}

BOOL CScriptCompiler::LoadOneScript(SOneScript &res, POSITION &pos, CLexemeList &lexemes)
{
    SLexeme lexInfo;
    CString lex;
    ESCCmd cmd;
    while (1)
    {
        SScriptCondition condition;
        if (!pos)
            return ScriptError(lexemes.GetTail().nLine, SC_UNEXPECTED_EOF,
                               "Unexpected end of file");
        lexInfo = lexemes.GetNext(pos);
        lex = lexInfo.lexeme;
        GetLexemeCmd(language, lex, &cmd, NULL);
        if (cmd == SC_CLOSE_FUNCTION)
            break;
        if (cmd == SC_IF)
        {
            lexInfo = lexemes.GetNext(pos);
            lex = lexInfo.lexeme;
            GetLexemeCmd(language, lex, &cmd, NULL);
            if (cmd != SC_OPEN_FUNCTION)
            {
                ScriptError(lexInfo.nLine, SC_EXPECTED_SYMBOL_NOT_FOUND, "If section, ( expected");
                return FALSE;
            }
            // allocate new thread in list of all threads and load conditions in it
            condition.pConditions = AllocateNewThread();
            if (!LoadConditionsList(*condition.pConditions, pos, lexemes, res.vars))
                return FALSE;
            lexInfo = lexemes.GetNext(pos);
            lex = lexInfo.lexeme;
            GetLexemeCmd(language, lex, &cmd, NULL);
            if (cmd != SC_THEN)
            {
                ScriptError(lexInfo.nLine, SC_EXPECTED_SYMBOL_NOT_FOUND, "then expected after if");
                return FALSE;
            }
            lexInfo = lexemes.GetNext(pos);
            lex = lexInfo.lexeme;
            GetLexemeCmd(language, lex, &cmd, NULL);
            if (cmd != SC_OPEN_FUNCTION)
            {
                ScriptError(lexInfo.nLine, SC_EXPECTED_SYMBOL_NOT_FOUND, "Expected ( after then");
                return FALSE;
            }

            // allocate new thread in list of all threads and load commands in it
            condition.pActions = AllocateNewThread();
            if (!LoadCommandsList(*condition.pActions, pos, lexemes, &res.vars))
                return FALSE;
            res.conditions.Add(condition);
        }
        else
        {
            ScriptError(lexInfo.nLine, SC_EXPECTED_IF_SECTION, "expected if section");
            return FALSE;
        }
    }
    if (res.conditions.GetSize() == 0)
        return ScriptError(lexInfo.nLine, SC_NO_CONDITIONS,
                           "In script %s should be at least one condtion",
                           (const char *)res.szScriptName);
    return TRUE;
}

BOOL CScriptCompiler::LoadCommandsList(SScriptThread &res, POSITION &pos, CLexemeList &lexemes,
                               SScriptVariablesSet *pVars)
{
    int i;
    SLexeme lexInfo;
    CString lex, lexSigned;
    ESCCmd cmd;
    SSCCmd command;
    SScriptVariablesSet zeroVars;
    if (!pVars)
        pVars = &zeroVars;
    SScriptVariablesSet &vars = *pVars;

    res.ClearCommands();
    //	res.Link( this );
    while (1)
    {
        if (!pos)
            return ScriptError(lexemes.GetTail().nLine, SC_UNEXPECTED_EOF,
                               "Unexpected end of file");
        command.Clear();
        lexInfo = lexemes.GetNext(pos);
        lex = lexInfo.lexeme;
        GetLexemeCmd(language, lex, &cmd, NULL);
        if (cmd == SC_CLOSE_FUNCTION)
            break;
        BOOL bRv = FALSE;
        const char *pszProcParams = GetProcedureParams(language, cmd);
        if (pszProcParams)
        {
            bRv = LoadFunction(command, pos, lexemes, cmd, vars, pszProcParams);
            if (cmd == SC_FOR || cmd == SC_FORIF)
            {
                if (!CheckNextLexeme(lexemes, pos, SC_OPEN_FUNCTION))
                    return FALSE;
                command.pLoopBody = AllocateNewThread();
                if (!LoadCommandsList(*command.pLoopBody, pos, lexemes, pVars))
                    return FALSE;
            }
        }
        else
        { // это не стандартная функция
            lexSigned = SignName(lex);
            bRv = FALSE;
            for (i = 0; i < scripts.GetSize(); i++)
            { // может это вызов одного из скриптов ???
                if (scripts[i].szScriptName == lex || scripts[i].szScriptName == lexSigned)
                {
                    bRv = TRUE;
                    if (!LoadFunction(command, pos, lexemes, SC_RUNSCRIPT, vars,
                                      scripts[i].szScriptParams, FALSE, TRUE))
                        return FALSE;
                    if (scripts[i].szScriptName == lex)
                        command.szParams[0] = lex;
                    else
                        command.szParams[0] = lexSigned;
                    command.params[0] = (float)i;
                    break;
                }
            }
            if (!bRv)
            { // может оператор присвоения
                if (globalVars.IsVariableName(lex))
                    bRv = LoadAssignOperator(command, pos, lexemes, vars, lex);
                else if (globalVars.IsVariableName(lexSigned))
                    bRv = LoadAssignOperator(command, pos, lexemes, vars, lexSigned);
                else
                    return ScriptError(lexInfo.nLine, SC_UNEXPECTED_SYMBOL,
                                       "Unexpected token \'%s\'", lex);
            }
        }
        if (!bRv)
            return FALSE;
        else
            res.Add(command);
    }
    return TRUE;
}

BOOL CScriptCompiler::LoadAssignOperator(SSCCmd &res, POSITION &pos, CLexemeList &lexemes,
                                 SScriptVariablesSet &vars, CString &szVarName)
{
    BOOL bRv;
    int nVarNumber;
    EScriptParameterType eType;
    SLexeme lexInfo;
    CString lex;
    res.Clear();
    if (!CheckNextLexeme(lexemes, pos, SC_EQUAL))
        return FALSE;
    bRv = globalVars.GetVariable(szVarName, nVarNumber, eType);
    ASSERT(bRv);
    switch (eType)
    {
    case ptFloat:
        bRv = LoadFunction(res, pos, lexemes, SC_GLOBALASSIGN, vars, "f", 1);
        res.params[1] = 0;
        break;
    case ptString:
        bRv = LoadFunction(res, pos, lexemes, SC_GLOBALASSIGN, vars, "s", 1);
        res.params[1] = 1;
        break;
    case ptObject:
        bRv = LoadFunction(res, pos, lexemes, SC_GLOBALASSIGN, vars, "o", 1);
        res.params[1] = 2;
        break;
    case ptGroup:
        bRv = LoadFunction(res, pos, lexemes, SC_GLOBALASSIGN, vars, "g", 1);
        res.params[1] = 3;
        break;
    default:
        ReportInvariant("Uknown variable type at CScript::LoadAssignOperator");
        return FALSE;
        break;
    }
    res.params[0] = (float)nVarNumber;
    return bRv;
}

BOOL CScriptCompiler::LoadParameters(SOneScript &res, POSITION &pos, CLexemeList &lexemes, BOOL bSign)
{
    SLexeme lexInfo;
    CString lex, lex1;

    BOOL bRv = TRUE, bFirst, bAdded;
    bRv = FALSE;
    bFirst = TRUE;
    while (pos)
    {
        lexInfo = lexemes.GetNext(pos);
        lex = lexInfo.lexeme;
        if (lex == ")")
        {
            bRv = TRUE;
            break;
        }
        if (!bFirst)
        {
            if (lex != ",")
                return ScriptError(lexInfo.nLine, SC_EXPECTED_SYMBOL_NOT_FOUND,
                                   "Expected , or ) after variable");
            lexInfo = lexemes.GetNext(pos);
            lex = lexInfo.lexeme;
        }
        if (!CheckNextLexeme(lexemes, pos, SC_COLON))
            return FALSE;
        if (bSign)
            lex = SignName(lex);
        lexInfo = lexemes.GetNext(pos);
        lex1 = lexInfo.lexeme;
        if (lex1 == "float")
            bAdded = res.AddParameter(lex, ptFloat);
        else if (lex1 == "string")
            bAdded = res.AddParameter(lex, ptString);
        else if (lex1 == "object")
            bAdded = res.AddParameter(lex, ptObject);
        else if (lex1 == "group")
            bAdded = res.AddParameter(lex, ptGroup);
        else
            return ScriptError(lexInfo.nLine, SC_UNKNOWN_PARAMETER_TYPE, "Unknown parameter type");
        if (!bAdded)
            return ScriptError(lexInfo.nLine, SC_DUPLICATE_PARAMETER_NAME,
                               "Duplicate variable name %s", (const char *)lex);
        bFirst = FALSE;
    }
    return bRv;
}

BOOL CScriptCompiler::ParseText(CString &szFile, ScriptLanguage context, SScriptThread *&worldThread)
{
    language = context;
    worldThread = nullptr;
    int nScriptIdx;
    BOOL bRv = TRUE;
    CLexemeList lexemes;
    SScriptThread thread;
    SOneScript objScript;

    scriptError.nLine = 0;
    scriptError.eError = SC_NO_ERROR;
    scriptError.text = "No errors found";

    if(language.extended){
        auto lowered=MobIfElse::Lower(std::string((LPCSTR)szFile,szFile.GetLength()));
        if(!lowered.error.empty())return ScriptError(lowered.line,SC_UNEXPECTED_SYMBOL,"if/else: %s",lowered.error.c_str());
        if(lowered.changed)szFile=lowered.text.c_str();
    }
    PreProcess(szFile);

    bRv = CreateLexemesList(szFile, lexemes);
    if (!bRv)
        return FALSE;

    SScriptThread *pWorldActions = NULL;
    // Parse unit scripts and create groups
    POSITION pos = lexemes.GetHeadPosition();
    while (pos)
    {
        SLexeme lexInfo = lexemes.GetNext(pos);
        CString lex = lexInfo.lexeme, lex1;
        ESCCmd cmd;
        GetLexemeCmd(language, lex, &cmd, NULL);
        switch (cmd)
        {
        case SC_WORLDSCRIPT:
            if (!CheckNextLexeme(lexemes, pos, SC_OPEN_FUNCTION))
                return FALSE;
            if (pWorldActions)
                return ScriptError(lexInfo.nLine, SC_MULTIPLE_WORLDSCRIPTS,
                                   "Only one WorldScript() per file allowed");
            pWorldActions = AllocateNewWorldThread();
            if (!LoadCommandsList(*pWorldActions, pos, lexemes))
                return FALSE;
            break;
        case SC_GLOBALVARS:
            objScript.Clear();
            if (!CheckNextLexeme(lexemes, pos, SC_OPEN_FUNCTION))
                return FALSE;
            if (!LoadParameters(objScript, pos, lexemes, TRUE))
                return FALSE;
            objScript.vars.SetZeroValues(); // this guarantees that execution will start in
                                            // identical environment every time
            globalVars.AddVars(objScript.vars);
            break;
        case SC_DECLARESCRIPT:
            objScript.Clear();
            lexInfo = lexemes.GetNext(pos);
            lex = lexInfo.lexeme;
            objScript.szScriptName = SignName(lex);
            if (GetScriptIdx(objScript.szScriptName) != -1)
                return ScriptError(lexInfo.nLine, SC_DUPLICATE_SCRIPT, "Duplicate script %s",
                                   (const char *)lex);
            if (!CheckNextLexeme(lexemes, pos, SC_OPEN_FUNCTION))
                return FALSE;
            if (!LoadParameters(objScript, pos, lexemes))
                return FALSE;
            scripts.Add(objScript);
            break;
        case SC_ONESCRIPT:
            objScript.Clear();
            lexInfo = lexemes.GetNext(pos);
            lex = lexInfo.lexeme;
            objScript.szScriptName = SignName(lex);
            if (!CheckNextLexeme(lexemes, pos, SC_OPEN_FUNCTION))
                return FALSE;
            nScriptIdx = GetScriptIdx(objScript.szScriptName);
            if (nScriptIdx < 0)
            { //  no forward declaration
                if (!LoadParameters(objScript, pos, lexemes))
                    return FALSE;
                if (!CheckNextLexeme(lexemes, pos, SC_OPEN_FUNCTION))
                    return FALSE;
                if (!LoadOneScript(objScript, pos, lexemes))
                    return FALSE;
                scripts.Add(objScript);
            }
            else
            { // forward declaration present
                if (scripts[nScriptIdx].conditions.GetSize())
                    return ScriptError(lexInfo.nLine, SC_MULTIPLE_SCRIPT_DECLARATION,
                                       "Multiple declaration of script %s",
                                       scripts[nScriptIdx].szScriptName);
                if (!LoadOneScript(scripts[nScriptIdx], pos, lexemes))
                    return FALSE;
            }
            break;
        default:
            return ScriptError(lexInfo.nLine, SC_UNEXPECTED_SYMBOL, "Unexpected \'%s\'", lex);
            break;
        }
    }

    worldThread = pWorldActions;
    return TRUE;
}





int CScriptCompiler::GetScriptIdx(const char *pszName)
{
    for (int i = 0; i < scripts.GetSize(); i++)
        if (scripts[i].szScriptName == pszName)
            return i;
    if (pszName[0] == '!')
    {
        pszName++;
        while (pszName[0])
        {
            if (pszName[0] == '!')
            {
                pszName++;
                return GetScriptIdx(pszName);
            }
            pszName++;
        }
    }
    return -1;
}

CString CScriptCompiler::SignName(const CString &src)
{
    if (nSecondaryCount == 0)
        return src;
    CString res;
    res.Format("!%i!%s", nSecondaryCount, src);
    return res;
}


BOOL CScriptCompiler::CompileText(CString &text, ScriptLanguage context, SScriptThread *&worldThread)
{
    if (!ParseText(text, context, worldThread))
        return FALSE;
    FinishTextLoad();
    return TRUE;
}
