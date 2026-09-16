#pragma once
/////////////////////////////////////////////////////////////////////////////////////
enum EScriptParserError
{
    SC_NO_ERROR,
    SC_UNEXPECTED_EOF,
    SC_WRONG_LEXEME,
    SC_UNEXPECTED_SYMBOL,
    SC_EXPECTED_SYMBOL_NOT_FOUND,
    SC_NOTENOUGH_PARAMETERS,
    SC_TOOMUCH_PARAMETERS,
    SC_PARAMETER_EXPECTED,
    SC_ONLY_GLOBALVAR_ALLOWED,
    SC_FLOAT_FUNCTION_EXPECTED,
    SC_EXPECTED_IF_SECTION,
    SC_NO_CONDITIONS,
    SC_UNKNOWN_PARAMETER_TYPE,
    SC_DUPLICATE_PARAMETER_NAME,
    SC_MULTIPLE_WORLDSCRIPTS,
    SC_DUPLICATE_SCRIPT,
    SC_MULTIPLE_SCRIPT_DECLARATION,
    SC_CANT_READ_FILE
};

// описание ошибки в скрипте
struct SScriptError
{
    CString text;              // содержание ошибки
    EScriptParserError eError; // код ошибки
    int nLine;                 // номер линии в которой найдена ошибка
};
/////////////////////////////////////////////////////////////////////////////////////
