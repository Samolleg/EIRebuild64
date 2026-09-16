#pragma once
/*==========================================================================================================

        Scripts classes header

        Written by Ivanov Evgeny
        Nival Interactive, 1998

==========================================================================================================*/

#include "SaveLoader.h"
#include "SoundManager.h"
#include "sArray.h"
#include "ScriptErrors.h"
#include "MapObject.h"

class CWorld;
class CPlayerClient;
class CParticle;
class CMapObjectServer;
class CUnitServer;
class CEffectPointLight;
class CEffectParticle;
class CEffectSound;
class CEffectLightning;
class CUnit;

// function to load file into string
extern BOOL FileToCString(const char *pszFileName, CString &res);

// ------------------------------- Helper structures
// -------------------------------------------------------

/*// camera activation structure
struct SCameraActivate
{
        int nGroupToActivate;
        CPlot3D pointToActivate;
        float fpRadius2;
};
*/
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

struct SScriptLightInfo
{
    CEffectPointLight *pLight;
    int nScriptID;
    BOOL bAutoDelete;
    BOOL bIsMoving;
    int nSegmentsToMove;
    CPlot3D moveVector;

    void Clear();
    SScriptLightInfo() { pLight = NULL; }
    ~SScriptLightInfo() { Clear(); }
    void Load(CSave &file);
    void Store(CSave &file);
};

// структура, описывающая партикл эффект, принадлежащий скрипту, этот эффект будет освобожден
// только после команды скрипта, наличие этой структуры позволяет передвигать эффект в
// пространстве и тп
struct SParticleInfo
{
    CEffectParticle *pParticle;
    int nScriptID;
    BOOL bAutoDelete;
    BOOL bIsMoving;
    int nSegmentsToMove;
    CPlot3D moveVector;
    int nLifeTime;

    void Clear();
    SParticleInfo() { pParticle = NULL; }
    ~SParticleInfo() { Clear(); }
    void Load(CSave &file);
    void Store(CSave &file);
};

struct SFXInfo
{
    CEffectSound *pSound;
    int nScriptID;
    CString szSoundName;
    CPlot3D cp;
    float fpMinRadius, fpMaxRadius;

    BOOL bIsRandomized;
    float fpRandRadius;
    int nMinSegment, nMaxSegment;
    int nDelay;

    void Clear();
    SFXInfo() { pSound = NULL; }
    ~SFXInfo() { Clear(); }
    void Load(CSave &file);
    void Store(CSave &file);
};

struct SLightningInfo
{
    CEffectLightning *pLightning;
    int nScriptID;

    void Clear();
    SLightningInfo()
    {
        nScriptID = -1;
        pLightning = NULL;
    }
    ~SLightningInfo() { Clear(); }
    void Load(CSave &file);
    void Store(CSave &file);
};

struct SScriptArea
{
    struct SScriptAreaElement
    {
        bool bRect;
        union
        {
            struct
            {
                float fpLowX, fpLowY, fpHighX, fpHighY;
            };
            struct
            {
                float fpCenterX, fpCenterY, fpRadius2;
            };
        };
        bool IsInArea(float fpX, float fpY);
        void Load(CSave &file);
        void Store(CSave &file);
    };
    CAArray<SScriptAreaElement, SScriptAreaElement &> areas;
    int nID;

    void AddSquare(float fpLowX, float fpLowY, float fpHighX, float fpHighY);
    void AddRound(float fpCenterX, float fpCenterY, float fpRadius);
    bool IsInArea(float fpX, float fpY);
    void Load(CSave &file);
    void Store(CSave &file);
};
//--------------------------------- Script related objects -----------------------------------

// возможные типы параметров
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

    void Clear(); // удалить ссылку на объект, если такая была
    void Copy(const CScriptObject &a);
    void CheckObjectValid(); // проверить, что объект, на который ссылается pObject все еще валиден
public:
    CScriptObject();
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
    void Load(CSave &file);
    void Store(CSave &file);
};

// группа объектов, иногда бывает полезно, элементы всегда находятся в отсортированом
// по возрастанию порядке
class CScriptObjectGroup
{
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
    void Load(CSave &file);
    void Store(CSave &file);
};

// множество переменных скрипта одного типа
// множество переменных определено в момент компиляции скрипта и не может быть изменено потом,
// так как в структурах SScriptParameter присутствует номер переменной (nVarNumber)
template <class VAR_TYPE>
class CScriptVars
{
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
    VAR_TYPE &operator[](int nIdx) { return vars[nIdx]; }
    void AddVars(const CScriptVars<VAR_TYPE> &a);
    // скопировать только значения переменных, без имен
    void CopyValues(const CScriptVars<VAR_TYPE> &src) { vars = src.vars; }
    void Load(CSave &file);
    void Store(CSave &file);
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
    void Load(CSave &file);
    void Store(CSave &file);
};

// описание одного параметра - переменной
struct SScriptParam
{
    EScriptParameterType eType; // тип параметра - число, строка или объект
    int nNumber;                // номер параметра в params SSCCmd
    int nVarNumber;             // номер переменной в ScriptVariablesSet
    SScriptParam()
    {
        nNumber = 0;
        nVarNumber = 0;
    }
    void Load(CSave &file) { file.Read(&eType, sizeof(SScriptParam)); }
    void Store(CSave &file) { file.Write(&eType, sizeof(SScriptParam)); }
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
    void Load(CSave &file);
    void Store(CSave &file);
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
        cmd = dbgnew SSCCmd();
    }
    SScriptFuncParam(const SScriptFuncParam &a)
    {
        eType = a.eType;
        nNumber = a.nNumber;
        cmd = dbgnew SSCCmd(*a.cmd);
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
    void Load(CSave &file)
    {
        file.Read(&eType, sizeof(eType));
        file.Read(&nNumber, sizeof(nNumber));
        cmd->Load(file);
    }
    void Store(CSave &file)
    {
        file.Write(&eType, sizeof(eType));
        file.Write(&nNumber, sizeof(nNumber));
        cmd->Store(file);
    }
};

// объект, описывающий один тред команд скрипта или множество условий( event`ov из секции if )
struct SScriptThread
{
    CAArray<SSCCmd, SSCCmd &> commands;
    int nIdx;

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
    void Load(CSave &file)
    {
        file.Read(&nIdx, sizeof(nIdx));
        LoadCArray<SSCCmd, SSCCmd &>(commands, file);
    }
    void Store(CSave &file)
    {
        file.Write(&nIdx, sizeof(nIdx));
        StoreCArray<SSCCmd, SSCCmd &>(commands, file);
    }
};

typedef SScriptThread *HScriptThread;

// объект, выполняющий команды треда скрипта
class CScriptThreadState
{
    SScriptThread *pThread;
    int nCurCommand;
    int nSegment, nSleepStart;
    SOneScript *pLinkedOneScript; // для KillScript()
    SScriptVariablesSet variables;

    // выполнить один сегмент скрипта pThread с переменными и LinkedOneScript из этого threadstate
    // используется в циклах for()
    void RunOneSegment(CScript *pScript, SScriptThread *pNewThread);

public:
    CScriptThreadState()
        : nCurCommand(0)
        , nSegment(0)
        , pThread(0)
        , pLinkedOneScript(0)
    {
    }
    void Clear()
    {
        nCurCommand = 0;
        nSegment = 0;
        pThread = 0;
    }

    // последовательно выполняет список команд, вернет FALSE, если в списке больше нет команд
    BOOL Segment(CScript *pScript);
    void CheckObjectRefs();
    // вернет TRUE,  если в скрипте больше нет команд
    BOOL IsFinished() { return nCurCommand >= pThread->commands.GetSize(); }
    // начать выполнение скрипта, обнуляет указатель текущей команды, устанавливает
    // новые значения переменных и инициализирует указатель на скрипт
    void RunThread(SScriptThread *pNewThread, SScriptVariablesSet *newVars = NULL);
    // прилинковать OneScript, только для возможности выполнения команды KillScript()
    void LinkOneScript(SOneScript *pNewOScript) { pLinkedOneScript = pNewOScript; }
    void Load(CSave &file);
    void Store(CSave &file);
};

// объект, описывающий один тригер, при выполнении всех условий, назначенных на этот тригер
// начинает выполнятся некая последовательность команд
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
    void Load(CSave &file);
    void Store(CSave &file);
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
    void Load(CSave &file);
    void Store(CSave &file);
};

class CScript
{
    // описатели объектов, управляемых из скрипта
    CAArray<SScriptLightInfo> lights;
    CList<SParticleInfo> particles;
    CList<SFXInfo> sfxes;
    CList<SLightningInfo> lightnings;
    // для автоматического назначения уникальных идентификаторов партикл сорсам, если задан
    // отрицательный ID
    int nPrevNegativeParticleSourceID;
    int nPrevNegativeSoundID;
    // список всех тредов, которые могут быть запущены скриптами
    CList<SScriptThread> threads;
    // список всех возможных скриптов
    CAArray<SOneScript> scripts;
    // множество глобальных переменных
    SScriptVariablesSet globalVars;
    // список активных скриптов
    CList<SOneScript> runningScripts;
    // список активных тредов
    CList<CScriptThreadState> runningThreads;
    // действия, выполняемые для всего мира
    CList<SScriptThread> worldActions;
    CAArray<SScriptArea> scriptAreas;
    // TRUE if script was Loaded and able to run, этот флаг также используется для отделени
    // вторичных скриптов от первичных
    BOOL bLoaded;
    // счетчик вторичных скриптов, используется для обхода проблемы имен глобальных переменных
    // и имен вторичных скриптов
    int nSecondaryCount;
    // набор временных переменных типа CScriptObjectGroup для временного использования, нужны
    // для функций, возвращающих группы, по умолчанию в списке находится один элемент ( дл
    // предотвращения дорогих вызовов RemoveAll() каждый ReleaseTemp() ), элемент добавляетс
    // конструктором и не должен никем трогатс
    CList<CScriptObjectGroup> tempGroups;
    // аналогичная система для переменных типа SScriptObject
    CList<CScriptObject> tempObjects;
    // счетчик количества вложенных for() для корректной реализации ReleaseTemp()
    int nForCount;
    // добавить в имя переменной/скрипта номер вторичного скрипта для поддержания уникальности
    // имен
    CString SignName(const CString &src);

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

    // отрезать от имени номер скрипта, если он там есть и равен nSecondaryScript-1 (для записи
    // вторичных скриптов в виде текста), если номер есть и он не равен nSecondaryScript-1 вернетс
    // ноль
    const char *CutNumber(const char *pszName);
    // считать номер из строки, выделенный символами ! в начале строки и поместить его в nNumber
    // возвращается строка без этого номера, если номера нет nNumber == -1 и возвращается вс
    // строка
    const char *ReadNumber(const char *pszName, int &nNumber);
    // записать параметры функции/глобальные переменные в текстовом виде, bOneLine - записывать
    // параметры в одной строке
    void StoreTextVars(BOOL bOneLine, CString &res, SScriptVariablesSet &vars,
                       const char *pszParams = NULL, BOOL bStoreGlobalVars = FALSE);
    // записать вызов функции/процедуры с параметрами, cmd изменяется внутри функции, поэтому
    // копируетс
    void StoreTextFunction(CString &res, SSCCmd cmd, SOneScript *pOneScript);
    // записать команды одного треда в res
    void StoreTextThread(int nIndent, CString &res, SScriptThread &thread, SOneScript *pOneScript);

    // преобразовать строку в последовательность лексем
    BOOL CreateLexemesList(CString &szFile, CLexemeList &lexemes);
    // создать новый поток команд, в том числе назначить ему уникальный номер для Load/Store, так
    // как потоки удаляются только все вместе в качестве уникального номера потока используется его
    // порядковый номер
    SScriptThread *AllocateNewThread();
    SScriptThread *AllocateNewWorldThread();
    // получить поток команд по его номеру
public:
    // доступ к running scripts для load/save
    int GetRunningScriptIndex(SOneScript *pScript);
    SOneScript *GetRunningScript(int nNumber);
    // управление временными переменными для групп, используются для функций, возвращающих группы
    CScriptObjectGroup *GetTempGroup()
    {
        return &tempGroups.GetAt(tempGroups.AddTail(CScriptObjectGroup()));
    }
    // управление временными переменными для групп, используются для функций, возвращающих группы
    CScriptObject *GetTempObject()
    {
        return &tempObjects.GetAt(tempObjects.AddTail(CScriptObject()));
    }
    // эта функция должна вызываться после каждой операции над SSCCmd самого верхнего уровн
    void ReleaseTemp();
    SScriptArea &GetArea(int nID);
    void DeleteArea(int nID);
    // для предотвращения уничтожения временных переменных при использовании циклов
    void IncForFlag() { nForCount++; }
    void DecForFlag() { nForCount--; }

    // сбросить все содержимое скрипт системы
    void Clear();
    // рестартовать скрипт
    void RestartScript();

    CScript();
    ~CScript() { Clear(); }
    void Destroy() { Clear(); }
    void CreatePointLight(SSCCmd *pCmd);
    void MovePointLight(SSCCmd *pCmd);
    void DeletePointLight(SSCCmd *pCmd);
    void CreateParticleSource(SSCCmd *pCmd);
    void MoveParticleSource(SSCCmd *pCmd);
    void DeleteParticleSource(SSCCmd *pCmd);
    void AttachParticleSource(int nScriptID, int nObjID);
    void SetParticleSourceSize(SSCCmd *pCmd);
    void CreateSFX(SSCCmd *pCmd);
    void CreateSFXSource(SSCCmd *pCmd);
    void DeleteSFXSource(SSCCmd *pCmd);
    void CreateLightning(SSCCmd *pCmd);
    void DeleteLightning(SSCCmd *pCmd);
    void SetCamera(SSCCmd *pCmd);
    void SetCameraOrientation(SSCCmd *pCmd);

    void RunScript(SSCCmd *pCmd);
    // сразу выполняется один сегмент треда и тред ставится в очередь
    void RunThread(SScriptThread *pThread, SOneScript *pOneScript = NULL);
    // тред начнет выполнятся в порядке общей очереди при вызове CScript::Segment()
    void RunThreadDelayed(SScriptThread *pThread);

    SScriptVariablesSet &GetGlobalVars() { return globalVars; }
    // получить скрипт по его имени, после добавления новых скриптов указатель может стать
    // некорректным, NULL, если скрипт не найден, функция сама добавляет префикс для вторичных
    // скриптов, если нужно
    SOneScript *GetScript(const char *szName);
    // если WorldScript еще не создан он будет создан, если считано более одного скрипта
    // вернется указатель только на один из них
    HScriptThread GetWorldScript();
    // запущен ли скрипт pszName, вернет TRUE если запущен
    BOOL IsScriptRunning(const char *pszName);
    // а запущчено ли чего-нибудь
    BOOL IsSomethingRunning() { return runningThreads.GetCount() || runningScripts.GetCount(); }
    // остановить выполнение всех скриптов и тредов
    void StopExecution();

    // добавить новые глобальные переменные для скрипта, переменные описаны в строке szVars
    // в обычном формате( например "light:float, b:string" ), duplicate names are not controlled
    BOOL ParseGlobalVars(CString szVars);
    // создать скрипт по имени pszOneScriptName с параметрами pszParams , параметры в обычном
    // формате
    BOOL CreateOneScript(const char *pszOneScriptName, CString szParams);
    // добавить условие к скрипту pszOneScriptName, указатели на условия и команды заносятся в
    // pIfThread & pThenThread respectively, return FALSE if OneScript pszOneScriptName not found
    BOOL CreateCondition(const char *pszOneScriptName, HScriptThread &pIfThread,
                         HScriptThread &pThenThread);
    // скомпилировать услови
    BOOL ParseIfThread(const char *pszOneScriptName, HScriptThread pThread, CString szSrc);
    // скомпилировать команды (секцию then, либо WorldScript())
    BOOL ParseCommandThread(const char *pszOneScriptName, HScriptThread pThread, CString szSrc);
    void AddSecondaryCounter() { nSecondaryCount++; }

    void Segment();
    void CheckObjectRefs();

    // Load/Store
    // считать скрипт из памяти, первичный либо вторичный( если один LoadText уже был )
    // вторичный скрипт может иметь глобальных переменных ( своих и первичного скрипта )
    // WorldScript ставится в очередь на выполнение сразу после считывания, если bRun == TRUE
    BOOL LoadText(CString &szScriptText, BOOL bRun = TRUE);
    BOOL AppendText(CString &szScriptText);
    // считать скрипт из файла
    BOOL Load(const char *pszFileName);
    // декомпилировать скрипт( возможно вторичный)
    void StoreText(CString &res);
    // PrepareForSecondary() предназначена для считывания вторичного скрипта
    // с возможностью корректной декомпляции с помощью StoreText. необходимо сначала считать
    // первичный скрипт, затем вызвать эту функцию, которая удалит информацию, относящуюся к
    // первичному скрипту и неиспользуемую во вторичном, затем считать вторичный скрипт с
    // помощью Load/LoadText, после этого StoreText() будет давать корректные результаты
    void PrepareForSecondary();
    SScriptError GetLastError() { return scriptError; }
    // запустить WorldScript(), можно использовать если LoadText был вызван с параметром bRun
    // == FALSE, если был
    void RunWorldScript()
    {
        RunThreadDelayed(GetWorldScript());
        bLoaded = TRUE;
    }
    // Serializing data
    void LoadScript(CSave &file);
    void StoreScript(CSave &file);
    SScriptThread *GetThreadByIndex(int nIdx);
    // эта функция не добавляет к имени скрипта ничего, только для load/store
    SOneScript *GetScriptByName(const char *szName);
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
template <class VAR_TYPE>
void CScriptVars<VAR_TYPE>::Load(CSave &file)
{
    int i = 0;
    file.Read(&i, sizeof(i));
    vars.SetSize(i);
    varNames.SetSize(i);
    for (i = 0; i < vars.GetSize(); i++)
    {
        LoadObject<VAR_TYPE>(vars[i], file);
        LoadObject<CString>(varNames[i], file);
    }
}

template <class VAR_TYPE>
void CScriptVars<VAR_TYPE>::Store(CSave &file)
{
    int i, nZero = 0;
    i = vars.GetSize();
    file.Write(&i, sizeof(i));
    for (i = 0; i < vars.GetSize(); i++)
    {
        StoreObject<VAR_TYPE>(vars[i], file);
        if (i < varNames.GetSize())
            StoreObject<CString>(varNames[i], file);
        else
            file.Write(&nZero, 1);
    }
}
