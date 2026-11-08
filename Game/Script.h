#pragma once
#include "SaveSchema.h"
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
#include "ModCampReferences.h"
#include "ScriptCompiler.h"

namespace SaveSchema { struct ScriptCodec; }
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
struct SScriptLightInfo
{
    CEffectPointLight *pLight;
    int nScriptID;
    BOOL bAutoDelete;
    BOOL bIsMoving;
    int nSegmentsToMove;
    CPlot3D moveVector;

    void Clear();
    SScriptLightInfo()
        : pLight(NULL), nScriptID(0), bAutoDelete(FALSE), bIsMoving(FALSE),
          nSegmentsToMove(0), moveVector(0, 0, 0) {}
    ~SScriptLightInfo() { Clear(); }
    void Load(CSave &file) = delete;
    void Store(CSave &file) = delete;
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
    SParticleInfo()
        : pParticle(NULL), nScriptID(0), bAutoDelete(FALSE), bIsMoving(FALSE),
          nSegmentsToMove(0), moveVector(0, 0, 0), nLifeTime(0) {}
    ~SParticleInfo() { Clear(); }
    void Load(CSave &file) = delete;
    void Store(CSave &file) = delete;
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
    SFXInfo()
        : pSound(NULL), nScriptID(0), cp(0, 0, 0), fpMinRadius(0), fpMaxRadius(0),
          bIsRandomized(FALSE), fpRandRadius(0), nMinSegment(0), nMaxSegment(0), nDelay(0) {}
    ~SFXInfo() { Clear(); }
    void Load(CSave &file) = delete;
    void Store(CSave &file) = delete;
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
    void Load(CSave &file) = delete;
    void Store(CSave &file) = delete;
};

struct SScriptArea
{
    struct SScriptAreaElement
    {
        void Load(SaveSchema::Reader &);
        void Store(SaveSchema::Writer &) const;
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
        void Load(CSave &file) = delete;
        void Store(CSave &file) = delete;
    };
    CAArray<SScriptAreaElement, SScriptAreaElement &> areas;
    int nID;

    void AddSquare(float fpLowX, float fpLowY, float fpHighX, float fpHighY);
    void AddRound(float fpCenterX, float fpCenterY, float fpRadius);
    bool IsInArea(float fpX, float fpY);
    void Load(CSave &file) = delete;
    void Store(CSave &file) = delete;
};
namespace SaveSchema {
template<> struct ValueCodec<SScriptArea::SScriptAreaElement> { static constexpr std::uint32_t Id = 0x1620; static ValueType Get(); };
}
//--------------------------------- Script related objects -----------------------------------

// возможные типы параметров
class CScriptThreadState
{
    friend class CScript;
    friend struct SaveSchema::ScriptCodec;
    friend struct ModCampReferences::Access;
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
        , nSleepStart(-1)
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
    BOOL Segment(CScript *pScript, CScript *pCommandOwner = nullptr);
    void CheckObjectRefs();
    // вернет TRUE,  если в скрипте больше нет команд
    BOOL IsFinished() { return nCurCommand >= pThread->commands.GetSize(); }
    // начать выполнение скрипта, обнуляет указатель текущей команды, устанавливает
    // новые значения переменных и инициализирует указатель на скрипт
    void RunThread(SScriptThread *pNewThread, SScriptVariablesSet *newVars = NULL);
    // прилинковать OneScript, только для возможности выполнения команды KillScript()
    void LinkOneScript(SOneScript *pNewOScript) { pLinkedOneScript = pNewOScript; }
    void Load(CSave &file) = delete;
    void Store(CSave &file) = delete;
};

// объект, описывающий один тригер, при выполнении всех условий, назначенных на этот тригер
// начинает выполнятся некая последовательность команд
class CScript : public CScriptCompiler
{
    friend class SaveSchema::Slot;
    friend struct SaveSchema::ScriptCodec;
    friend struct ModCampReferences::Access;
    bool detachedSaveState = false;
    // описатели объектов, управляемых из скрипта
    CAArray<SScriptLightInfo> lights;
    CList<SParticleInfo> particles;
    CList<SFXInfo> sfxes;
    CList<SLightningInfo> lightnings;
    // для автоматического назначения уникальных идентификаторов партикл сорсам, если задан
    // отрицательный ID
    int nPrevNegativeParticleSourceID;
    int nPrevNegativeSoundID;

    // список активных скриптов
    CList<SOneScript> runningScripts;
    // список активных тредов
    CList<CScriptThreadState> runningThreads;

    CAArray<SScriptArea> scriptAreas;

    // набор временных переменных типа CScriptObjectGroup для временного использования, нужны
    // для функций, возвращающих группы, по умолчанию в списке находится один элемент ( дл
    // предотвращения дорогих вызовов RemoveAll() каждый ReleaseTemp() ), элемент добавляетс
    // конструктором и не должен никем трогатс
    CList<CScriptObjectGroup> tempGroups;
    // аналогичная система для переменных типа SScriptObject
    CList<CScriptObject> tempObjects;
    // счетчик количества вложенных for() для корректной реализации ReleaseTemp()
    int nForCount;

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
    bool CanDeferCommand() const { return nForCount == 0; }
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
    bool SetParticleFlow(const SSCCmd &cmd);
    void RefreshArenaWaveSources(int wave,float radius,int tick);
    void RefreshFinalGolemSources(int tick);
    bool SetParticleSourcePower(const SSCCmd &cmd);
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
    // Request-scoped MOB command list; never queues a thread or changes world-script globals.
    bool RunFragment(const CString &source, float &result, CString &error);
    bool McpArenaThread(const char* name,const char* marker,float value,bool tail,bool validateOnly);


    // получить скрипт по его имени, после добавления новых скриптов указатель может стать
    // некорректным, NULL, если скрипт не найден, функция сама добавляет префикс для вторичных
    // скриптов, если нужно

    // если WorldScript еще не создан он будет создан, если считано более одного скрипта
    // вернется указатель только на один из них

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

    // запустить WorldScript(), можно использовать если LoadText был вызван с параметром bRun
    // == FALSE, если был
    void RunWorldScript()
    {
        RunThreadDelayed(GetWorldScript());
        bLoaded = TRUE;
    }
    // Serializing data
    // Load only into a fresh detached staging script. P17 adopts after graph success.
    void AdoptSaveState();
    void DiscardSaveState();
    void LoadScript(SaveSchema::Reader &);
    void StoreScript(SaveSchema::Writer &) const;
    void LoadScript(CSave &file) = delete;
    void StoreScript(CSave &file) = delete;

    // эта функция не добавляет к имени скрипта ничего, только для load/store

};

