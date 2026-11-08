#pragma once
#include <afx.h>
#include <afxtempl.h>
#include "sArray.h"
#include "ScriptErrors.h"
#include "ScriptLanguage.h"
#include "ModCampReferences.h"
namespace SaveSchema { class Reader; class Writer; class Slot; struct ScriptCodec; }
class CSave;
class CMapObject;
class CMapObjectPtr;
class CMapObjectServer;
class CUnit;
class CUnitServer;
typedef CList<CMapObjectPtr, CMapObject *> CMapObjectPtrList;
// lexeme info structure
struct SLexeme
{
    CString lexeme;
    int nLine;

    SLexeme()
    {
        lexeme = "None";
        nLine = -1;
    }
    SLexeme(CString lex, int nLn)
    {
        lexeme = lex;
        nLine = nLn;
    }
};
typedef CList<SLexeme> CLexemeList;

enum EScriptParameterType
{
    ptNone,
    ptFloat,
    ptString,
    ptObject,
    ptGroup
};

class CScript;
struct SOneScript;

// одна переменная скрипта описывающая объект
class CScriptObject
{
    CMapObject *pObject; // указатель на объект в случае ptObject, для этого объекта делаетс
                         // AddRef() для сохранения валидности ссылки

    // Runtime SetObject supplies the real reference operations.
    void (*retainObject)(CMapObject *) = nullptr;
    void (*releaseObject)(CMapObject *) = nullptr;
    void Clear(); // удалить ссылку на объект, если такая была
    void Copy(const CScriptObject &a);
    void CheckObjectValid(); // проверить, что объект, на который ссылается pObject все еще валиден
public:
    CScriptObject();
    void Reset() { Clear(); }
    ~CScriptObject() { Clear(); }
    CScriptObject(const CScriptObject &var) { Copy(var); }
    CScriptObject &operator=(const CScriptObject &var)
    {
        Clear();
        Copy(var);
        return *this;
    }
    void SetObject(CMapObject *pObj);
    CMapObject *GetObject() const { return pObject; }
    CMapObjectServer *GetSpecific();
    CUnit *GetUnit();
    CUnitServer *GetUnitSpecific();
    operator CMapObject *() const { return GetObject(); }
    operator CMapObjectServer *() { return GetSpecific(); }
    // для регулярной проверки верности ссылки
    void CheckObjectRefs() { CheckObjectValid(); }
    void Load(CSave &file) = delete;
    void Store(CSave &file) = delete;
};

// группа объектов, иногда бывает полезно, элементы всегда находятся в отсортированом
// по возрастанию порядке
class CScriptObjectGroup
{
    friend struct SaveSchema::ScriptCodec;
    CAArray<CScriptObject, CScriptObject &> members;

public:
    void Clear() { members.SetSize(0); }
    int GetIndex(CScriptObject &obj);
    // Add() проверяет, если в группе уже есть такой добавления не происходит
    void Add(CMapObject *pMO);
    void Add(CScriptObject &obj) { Add(obj.GetObject()); }
    void Remove(CScriptObject &obj);
    void Remove(CMapObject *pMO);
    int GetSize() const { return members.GetSize(); }
    void Add(CScriptObjectGroup &grpA,
             CScriptObjectGroup &grpB); // объединение групп *this = grpA + grpB
    void Sub(CScriptObjectGroup &grpA, CScriptObjectGroup &grpB);   // вычитание групп
    void Cross(CScriptObjectGroup &grpA, CScriptObjectGroup &grpB); // пересечение групп
    void Sort();            // отсортировать members по увеличению
    void CheckObjectRefs(); // проверить валидность всех ссылок и удалить устаревшие
    CScriptObject &GetAt(int i) { return members[i]; }
    void AddList(CMapObjectPtrList &list);  // добавить указатели на юниты из list в Group
    void MakeList(CMapObjectPtrList &list); // записать в список все юниты из группы
    BOOL HasMapObject(CMapObject *pSearch);
    void Load(CSave &file) = delete;
    void Store(CSave &file) = delete;
};

// множество переменных скрипта одного типа
// множество переменных определено в момент компиляции скрипта и не может быть изменено потом,
// так как в структурах SScriptParameter присутствует номер переменной (nVarNumber)
template <class VAR_TYPE>
class CScriptVars
{
    friend struct SaveSchema::ScriptCodec;
    CAArray<VAR_TYPE> vars;
    CAArray<CString> varNames; // имена переменных
public:
    BOOL GetVar(const char *szName, VAR_TYPE &res) const; // return FALSE if failed
    void GetVar(int nIdx, VAR_TYPE &res) const;
    VAR_TYPE *GetVar(int nIdx);
    void Clear()
    {
        vars.SetSize(0);
        varNames.SetSize(0);
    }
    BOOL AddVariable(const char *szName); // returns FALSE if variable with such name already exist
    int GetVariableIndex(const char *szName); // return -1 if no such variable found
    int GetSize() { return vars.GetSize(); }
    void SetAt(int i, VAR_TYPE &newVar) { vars[i] = newVar; }
    // получить имя i-й переменной
    const char *GetName(int nIdx) const { return varNames[nIdx]; }
    bool TryGetVarName(int index,const char*& name) const {
        name=nullptr;
        if(!ModCampReferences::MetadataAvailable(index,vars.GetSize(),varNames.GetSize(),varNames.GetData()!=nullptr))return false;
        name=varNames[index];return true;
    }
    VAR_TYPE &operator[](int nIdx) { return vars[nIdx]; }
    void AddVars(const CScriptVars<VAR_TYPE> &a);
    // скопировать только значения переменных, без имен
    void CopyValues(const CScriptVars<VAR_TYPE> &src) { vars = src.vars; }
    void Load(CSave &file) = delete;
    void Store(CSave &file) = delete;
};

// набор переменных всех типов
struct SScriptVariablesSet
{
    CScriptVars<float> fVars;
    CScriptVars<CString> szVars;
    CScriptVars<CScriptObject> oVars;
    CScriptVars<CScriptObjectGroup> gVars;

public:
    void Clear()
    {
        fVars.Clear();
        szVars.Clear();
        oVars.Clear();
        gVars.Clear();
    }
    void SetZeroValues();
    // поискать среди переменных переменную с именем pszName и записать индексы соответствующие
    // ей в nNumber/eType, вернет TRUE в случае успеха
    BOOL GetVariable(const char *pszName, int &nNumber, EScriptParameterType &eType);
    // среди переменных есть pszName
    BOOL IsVariableName(const char *pszName);
    // добавить переменных
    void AddVars(const SScriptVariablesSet &a);
    // для регулярной проверки верности ссылок
    void CheckObjectRefs();
    // скопировать только значения переменных, без имен
    void CopyValues(const SScriptVariablesSet &src);
    // запись/считывание
    void Load(CSave &file) = delete;
    void Store(CSave &file) = delete;
};

// описание одного параметра - переменной
struct SScriptParam
{
    void Load(SaveSchema::Reader &);
    void Store(SaveSchema::Writer &) const;
    EScriptParameterType eType; // тип параметра - число, строка или объект
    int nNumber;                // номер параметра в params SSCCmd
    int nVarNumber;             // номер переменной в ScriptVariablesSet
    SScriptParam()
    {
        eType = ptNone;
        nNumber = 0;
        nVarNumber = 0;
    }
    void Load(CSave &) = delete;
    void Store(CSave &) = delete;
};

struct SScriptFuncParam;
struct SScriptThread;

// script command/function with parameters including variables
struct SSCCmd
{
    int command;
    CAArray<float, float> params;                      // параметры команды/функции - числа
    CAArray<CString, CString &> szParams;              // параметры команды/функции - строчки
    CAArray<CScriptObject *, CScriptObject *> oParams; // параметры команды/функции - объекты
    CAArray<CScriptObjectGroup *, CScriptObjectGroup *>
        gParams; // параметры команды/функции - группы
                 // параметры - группы не копируются каждый раз, вместо этого передается указатель
    CAArray<SScriptParam, SScriptParam &> localParameters; // список параметров - переменных
    CAArray<SScriptParam, SScriptParam &>
        globalParameters; // список параметров - глобальных переменных
    CAArray<SScriptFuncParam, SScriptFuncParam &> funcParameters; // список параметров - функций
    SScriptThread *pLoopBody; // тело цикла для оператра for, сам thread принадлежит CScript

    // рассчитать один параметр - функцию
    void CalcFuncParam(CScript *pScript, SScriptVariablesSet &vars, SScriptFuncParam &funcParam);
    SScriptFuncParam *GetFirstFloatFuncParam();

public:
    SSCCmd()
    {
        command = 0;
        pLoopBody = NULL;
    }
    void Clear();
    // скопировать параметры, заданные parameters[] из vars в params/szParams/oParams
    void SetParameters(CAArray<SScriptParam, SScriptParam &> &parameters,
                       SScriptVariablesSet &vars);
    // выбрать параметры команды из параметров скрипта используя массив variableParameters
    // и рассчитать параметры - функции
    void CalcParameters(CScript *pScript, SScriptVariablesSet &vars);
    void ExecuteCommand(CScript *pScript);
    // вычисление функций с различными возвращаемыми значениями
    void CalcFunction(CScript *pScript, SScriptVariablesSet &vars, float &res);
    void CalcFunction(CScript *pScript, SScriptVariablesSet &vars, CString &res);
    void CalcFunction(CScript *pScript, SScriptVariablesSet &vars, CScriptObject **res);
    void CalcFunction(CScript *pScript, SScriptVariablesSet &vars, CScriptObjectGroup **res);
    void Load(CSave &file) = delete;
    void Store(CSave &file) = delete;
};

// описание одного параметра - функции
struct SScriptFuncParam
{
    EScriptParameterType eType; // тип параметра - число, строка или объект
    int nNumber;                // номер параметра в params SSCCmd
    SSCCmd *cmd;                // функция, которая используется для рассчета значения функции
public:
    SScriptFuncParam()
    {
        eType = ptNone;
        nNumber = 0;
        cmd = new SSCCmd();
    }
    SScriptFuncParam(const SScriptFuncParam &a)
    {
        eType = a.eType;
        nNumber = a.nNumber;
        cmd = new SSCCmd(*a.cmd);
    }
    SScriptFuncParam &operator=(const SScriptFuncParam &a)
    {
        eType = a.eType;
        nNumber = a.nNumber;
        *cmd = *a.cmd;
        return *this;
    }
    ~SScriptFuncParam() { delete cmd; }
    void Clear()
    {
        eType = ptNone;
        nNumber = 0;
        cmd->Clear();
    }
    void Load(CSave &file) = delete;
    void Store(CSave &file) = delete;
};

// объект, описывающий один тред команд скрипта или множество условий( event`ov из секции if )
struct SScriptThread
{
    CAArray<SSCCmd, SSCCmd &> commands;
    int nIdx = -1;

public:
    void Clear()
    {
        commands.RemoveAll();
        nIdx = -1;
    }
    void ClearCommands() { commands.RemoveAll(); }
    void Add(SSCCmd &cmd) { commands.Add(cmd); }
    void SetNumber(int nNumber) { nIdx = nNumber; }
    int GetNumber() { return nIdx; }
    void Load(CSave &file) = delete;
    void Store(CSave &file) = delete;
};

typedef SScriptThread *HScriptThread;

// объект, выполняющий команды треда скрипта
struct SScriptCondition
{
    SScriptThread *pConditions; // список условий, команды из этого списка преобразуются в услови
    SScriptThread
        *pActions; // список действий, которые должны быть выполнены при выполнении этого услови
public:
    SScriptCondition()
    {
        pActions = 0;
        pConditions = 0;
    }
    void Clear()
    {
        pActions = 0;
        pConditions = 0;
    }
    // проверить выполнение условий, если все ok, то запустить список команд из секции then
    void RunCheck(CScript *pScript, SOneScript *pOneScript);
    void Load(CSave &file) = delete;
    void Store(CSave &file) = delete;
};

// состояние объекта с соответсвующими ему conditions (один из допустимых скриптов объекта)
struct SOneScript
{
    int nDelFlag;
    CString szScriptName;
    CString szScriptParams; // описание типов параметров скрипта
    CAArray<SScriptCondition, SScriptCondition &> conditions;
    SScriptVariablesSet vars;

public:
    void Clear()
    {
        conditions.SetSize(0);
        vars.Clear();
        nDelFlag = 0;
        szScriptParams = "";
        szScriptName = "";
    }
    void CheckObjectRefs();
    void Segment(CScript *pScript);
    // приступить к выполнению скрипта, в том числе конвертировать условия из лексем в events
    // и установить их на соответсвующие места, перед выполнением этой функции должны быть
    // установлены параметры скрипта
    void RunScript() { nDelFlag = 0; }
    // параметры из cmd переносятся в vars, параметры переносятся по порядку начиная со второго
    // параметра-строки функция используется в реализации RunScript
    void SetParameters(SSCCmd &cmd);
    BOOL AddParameter(CString &szParam, EScriptParameterType eType);
    SOneScript() { nDelFlag = 0; }
    ~SOneScript() {}
    void Load(CSave &file) = delete;
    void Store(CSave &file) = delete;
};

class CScriptCompiler
{
    friend struct ModCampReferences::Access;
    friend class SaveSchema::Slot;
    friend struct SaveSchema::ScriptCodec;
protected:
    // список всех тредов, которые могут быть запущены скриптами
    CList<SScriptThread> threads;
    // список всех возможных скриптов
    CAArray<SOneScript> scripts;
    // множество глобальных переменных
    SScriptVariablesSet globalVars;
    // действия, выполняемые для всего мира
    CList<SScriptThread> worldActions;
    // TRUE if script was Loaded and able to run, этот флаг также используется для отделени
    // вторичных скриптов от первичных
    BOOL bLoaded;
    // счетчик вторичных скриптов, используется для обхода проблемы имен глобальных переменных
    // и имен вторичных скриптов
    int nSecondaryCount;
    ScriptLanguage language;
    void (*reportInvariant)(const char *);
    void ReportInvariant(const char *text) { reportInvariant(text); }
    // добавить в имя переменной/скрипта номер вторичного скрипта для поддержания уникальности
    // имен
    CString SignName(const CString &src);
    void ResetCompilationStatus();
    void ClearCompilationGlobals();
    void ClearCompilationScripts();
    BOOL ParseText(CString &text, ScriptLanguage context, SScriptThread *&worldThread);
    void FinishTextLoad() { bLoaded = TRUE; nSecondaryCount++; }


    SScriptError scriptError;
    BOOL __cdecl ScriptError(int nLine, EScriptParserError eError, const char *pszText, ...);

    // получить номер скрипта по его имени, -1 если не найден
    int GetScriptIdx(const char *pszName);
    // проверить, что следующая лексема - cmdForCheck, при этом pos передвигается на следующую
    // лексему cmdForCheck - ESCCmd
    BOOL CheckNextLexeme(CLexemeList &lexemes, POSITION &pos, int cmdForCheck);
    // считать функцию в res, вместе с параметрами, для оператора присвоения введен параметр
    // bLoadOne если этот параметр установлен считывается только один параметр и не нужна
    // закрывающая скобка newCmd - ESCCmd
    BOOL LoadFunction(SSCCmd &res, POSITION &pos, CLexemeList &lexemes, int newCmd,
                      SScriptVariablesSet &vars, const char *pszParams, BOOL bLoadAssign = FALSE,
                      BOOL bLoadRunScript = FALSE);
    // считать список условий
    BOOL LoadConditionsList(SScriptThread &res, POSITION &pos, CLexemeList &lexemes,
                            SScriptVariablesSet &vars);
    // считать список команд
    BOOL LoadCommandsList(SScriptThread &res, POSITION &pos, CLexemeList &lexemes,
                          SScriptVariablesSet *pVars = NULL);
    // считать список параметров (или список глобальных переменных), bSign показывает надо ли
    // подписывать переменные ( если функция используется для считывания глобальных переменных то
    // нужно
    BOOL LoadParameters(SOneScript &res, POSITION &pos, CLexemeList &lexemes, BOOL bSign = FALSE);
    // считать оператор присвоени
    BOOL LoadAssignOperator(SSCCmd &res, POSITION &pos, CLexemeList &lexemes,
                            SScriptVariablesSet &vars, CString &szVarName);
    // считать один Script()
    BOOL LoadOneScript(SOneScript &res, POSITION &pos, CLexemeList &lexemes);

    // преобразовать строку в последовательность лексем
    BOOL CreateLexemesList(CString &szFile, CLexemeList &lexemes);
    // создать новый поток команд, в том числе назначить ему уникальный номер для Load/Store, так
    // как потоки удаляются только все вместе в качестве уникального номера потока используется его
    // порядковый номер
    SScriptThread *AllocateNewThread();
    SScriptThread *AllocateNewWorldThread();
public:
    explicit CScriptCompiler(ScriptLanguage context, void (&reportInvariant)(const char *));
    ~CScriptCompiler() { ClearCompiled(); }
    CScriptCompiler(const CScriptCompiler &) = delete;
    CScriptCompiler &operator=(const CScriptCompiler &) = delete;
    void ClearCompiled();
    BOOL CompileText(CString &text, ScriptLanguage language, SScriptThread *&worldThread);
    SScriptError GetLastError() { return scriptError; }
    SScriptVariablesSet &GetGlobalVars() { return globalVars; }
    SOneScript *GetScript(const char *name);
    SOneScript *GetScriptByName(const char *name);
    HScriptThread GetWorldScript();
    SScriptThread *GetThreadByIndex(int index);
    BOOL ParseGlobalVars(CString text);
    BOOL CreateOneScript(const char *name, CString parameters);
    BOOL CreateCondition(const char *name, HScriptThread &conditions, HScriptThread &actions);
    BOOL ParseIfThread(const char *name, HScriptThread thread, CString text);
    BOOL ParseCommandThread(const char *name, HScriptThread thread, CString text);
    void SetLanguage(ScriptLanguage value) { language = value; }
    void AddSecondaryCounter() { nSecondaryCount++; }
};
//-----------------------  CScriptVars implementation -------------------------------
template <class VAR_TYPE>
BOOL CScriptVars<VAR_TYPE>::GetVar(const char *szName, VAR_TYPE &res) const
{
    int i;
    for (i = 0; i < varNames.GetSize(); i++)
    {
        if (varNames[i] == szName)
        {
            res = vars[i];
            return TRUE;
        }
    }
    return FALSE;
}

template <class VAR_TYPE>
VAR_TYPE *CScriptVars<VAR_TYPE>::GetVar(int nIdx)
{
    return &vars.ElementAt(nIdx);
}

template <class VAR_TYPE>
void CScriptVars<VAR_TYPE>::GetVar(int nIdx, VAR_TYPE &res) const
{
    res = vars[nIdx];
}

template <class VAR_TYPE>
int CScriptVars<VAR_TYPE>::GetVariableIndex(const char *szName)
{
    int i;
    for (i = 0; i < varNames.GetSize(); i++)
    {
        if (varNames[i] == szName)
            return i;
    }
    return -1;
}

template <class VAR_TYPE>
BOOL CScriptVars<VAR_TYPE>::AddVariable(const char *szName)
{
    if (GetVariableIndex(szName) >= 0)
        return FALSE;
    vars.SetSize(vars.GetSize() + 1);
    varNames.Add(CString(szName));
    return TRUE;
}

template <class VAR_TYPE>
void CScriptVars<VAR_TYPE>::AddVars(const CScriptVars<VAR_TYPE> &a)
{
    int i, nStart;
    nStart = vars.GetSize();
    ASSERT(a.vars.GetSize() == a.varNames.GetSize());
    vars.SetSize(nStart + a.vars.GetSize());
    varNames.SetSize(nStart + a.varNames.GetSize());
    for (i = 0; i < a.varNames.GetSize(); i++)
    {
        varNames[i + nStart] = a.varNames[i];
        vars[i + nStart] = a.vars[i];
    }
}

// loading & storing scriptvars



