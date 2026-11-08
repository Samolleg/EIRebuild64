#pragma once
#include <afx.h>
#include "ScriptParser.h"
struct SLexemeInfo
{
    LPCSTR pszAscii; // ASCII writting of lexeme
    ESCCmd cmd;
    LPCSTR pszParams;
    LPCSTR pszProcParams;
};
// A language value, not a mod/package. Both consumers use these exact tables.
struct ScriptLanguage {
    bool extended;
    static ScriptLanguage Legacy() { return {false}; }
    static ScriptLanguage Supported() { return {true}; }
    const SLexemeInfo *Lexemes() const;
};
const char *GetFunctionParams(ScriptLanguage language, ESCCmd cmd, int returnType);
const char *GetProcedureParams(ScriptLanguage language, ESCCmd cmd);
const char *GetCommandParams(ScriptLanguage language, ESCCmd cmd);
LPCSTR GetCmdName(ScriptLanguage language, ESCCmd cmd);
void GetLexemeCmd(ScriptLanguage language, LPCSTR text, ESCCmd *cmd, int *trailer);
