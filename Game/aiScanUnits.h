#pragma once
/**********************************************************************
 *<
        FILE: aiScanUnits.H

        DESCRIPTION:  Senses

 *>	Copyright (c) 1998, Nival Enterntainemnt
 **********************************************************************/

#include "plot.h"
#include "mapFormat.h"
#include "collision.h"
#include "sArray.h"
#include "mapObject.h"
#include "aiGraph.inl"
#include "aiObjects.h"
#include "aiSensesConst.h"

///////////////////////////////////////////////////////////////////
// constants
const int N_AI_MARK_CELL_SIZE = 8; // size in meters
////////////////////////////////////////////////////////////////////////////////////
// максимальное время жизни одного следа в сегментах, должно быть степенью двойки
const int N_MAX_MARK_LIFETIME = 1024;
// количество сегментов между двумя операциями установки следа для юнита, время жизни следа
// должно быть больше N_MARKING_INTERVAL
const int N_MARKING_INTERVAL = 16;
// количество подсписков по плеерам для footsteps storing, must be power of 2
const int N_AI_MARKS_PLAYER_NUM = 16;
/////////////////////////////////////////////////////////////////////////////////////
class CUnitServer;
class CAIMap;
class CAIMarkManager;
/////////////////////////////////////////////////////////////////////////////////////
inline CPlot3D cast2D3D(const CPlot2D &p)
{
    return CPlot3D(p.x, p.y, 0);
}
inline CPlot2D cast3D2D(const CPlot3D &p)
{
    return CPlot2D(p.x, p.y);
}
/////////////////////////////////////////////////////////////////////////////////////
// класс для представления множества целей, которые должны игнорироваться монстром
class CAIIgnoredTargetsList
{
    struct SIgnoredTarget
    {
        CMapObjectPtr pTarget;
        int nTime;
        SIgnoredTarget() = default;
        SIgnoredTarget(CMapObject *pT, int nT)
            : pTarget(pT)
            , nTime(nT)
        {
        }
    };
    CAArray<SIgnoredTarget> ignoredList;

public:
    void Clear() { ignoredList.RemoveAll(); }
    bool IsIgnored(CMapObject *pTarget);
    void Segment();
    void AddIgnoredTarget(CMapObject *pTarget, int nTime);
    void RemoveReferencesOn(CMapObject *pTarget);
    void Load(CSave &file);
    void Store(CSave &file);
};
/////////////////////////////////////////////////////////////////////////////////////
class CAIUnitEnvironment;
class CAIDelayedSoundsList
{
    struct SSound
    {
        CPlot3D ptSrc;
        int nDelay;
        DWORD dwNewEnemies;
        CString szDescription; // this additional information may degrade perfomance
        SSound() = default;
        SSound(const CPlot3D &ptFrom, int nTime, const char *pszText)
            : ptSrc(ptFrom)
            , nDelay(nTime)
            , szDescription(pszText)
            , dwNewEnemies(0)
        {
        }
        void Load(CSave &file);
        void Store(CSave &file);
    };
    CAArray<SSound> soundsQueue;

public:
    void Clear() { soundsQueue.RemoveAll(); }
    void AddSound(const CPlot3D &ptSrc, int nDelay, const char *pszText);
    void AddNewEnemies(DWORD dwNewEnemies, int nDelay, const char *pszText);
    void RemoveDiplomacyMsgs();
    void Segment(CUnit *pUnit, CAIUnitEnvironment &env);
    void Load(CSave &file);
    void Store(CSave &file);
};
/////////////////////////////////////////////////////////////////////////////////////
// SAIMark - универсальный след
struct SAIMark
{
    CPlot2D ptPosition;      // позиция следа
    CPlot2D ptPointPosition; // точка, в которую указывает след
    int nTime;               // время, когда след был оставлен
    float fpStrength[2];     // сила следа (1-обычный след) [0] - как footstep, percept via
                             // tracking(vision) [1] - как smell
    int nUnitID;             // чей след
    int nPlayer;             // номер плеера юнита nUnitID

    SAIMark *pNext, *pPrev; // двусвязный список
    SAIMark *pNextDel;      // список следов на удаление

    SAIMark() = default;
    // система для распределения памяти под следы
    static SAIMark *pFree;
    static SAIMark *GetNewMark();
    void ReleaseMark()
    {
        pNext = pFree;
        pFree = this;
    }
    // persistance
    void Load(CSave &file);
    void Store(CSave &file);
};
typedef SAIMark *PAIMark;
/////////////////////////////////////////////////////////////////////////////////////
// CAIUnitEnvironment - описание тактической обстановки юнита
class CAIUnitEnvironment
{
    CAIGridMesh footStepGrid, unitGrid;

private:
    CPlot2D ptPointOfInterest; // некая точка, представляющая интерес для монстра ( след, источник
                               // звука, etc )
    int nPoIPower;             // интересность ptPointOfInterest
    int nPoIPowerDecrease;     // rate of nPoIPowerDecrease
    int nDelayListsUpdate;     // переменная для ускорения перестройки списков соседних юнитов
public:
    CMapObjectPtrList units;        // list of Units on tne map near hero
    CMapObjectPtrList visibleUnits; // Visible units list
    CMapObjectPtrList seenCorpses;  // трупы, которые уже были исследованы и больше так не беспокоят
    CMapObjectPtrList freshCorpses; // только что увиденные останки
    CAIIgnoredTargetsList ignoredList;  // список игнорируемых целей
    CAIDelayedSoundsList delayedSounds; // список звуков, услышанных юнитом, переварены они будут
                                        // через некоторое время

    void Clear();

public:
    // проводит обновление общеполитической обстановки
    void Segment(CUnitServer *pHero);
    void RemoveReferencesOn(CMapObject *pTarget);
    BOOL IsUnitVisible(CUnit *pWatcher, CUnit *pUnit);
    void ForceNewListsForming() { nDelayListsUpdate = 0; }
    void FreshCorpsesAreExplored() { freshCorpses.RemoveAll(); }
    // Lists of visible units and potentially visible units
    CMapObjectPtrList *GetVisibleList() { return &visibleUnits; };
    CMapObjectPtrList *GetUnitsList() { return &units; };
    // target find
    void GetTarget(CUnit *pAttacker, CUnitList &res);
    void GetTarget(CUnit *pAttacker, const CPlot3D &ptNear, CUnitList &res);
    void GetAggressiveTarget(CUnit *pAttacker, CUnitList &res, const CPlot3D &ptSwarm,
                             float fpSwarmRadius);
    CUnit *GetNearestEnemy(CUnit *pAttacker); // can return target from invalid targets list
    // calc notability, returns distance, if negative unit will be noticed
    void CalcNotability(CUnitServer *pHero, SUnitVisibilityInfo &res);
    // clear information on footsteps or something unit saw
    void ClearPoI()
    {
        nPoIPower = 0;
        nPoIPowerDecrease = 0;
    }
    // юнит услышал некий звук, возможно привлекший его внимание, сила этого event`a AI_PPOWER_SOUND
    void HearSound(const CPlot3D &ptInterest, int nDelay, const char *pszDescription);
    void HearNewEnemies(DWORD dwEnemyMask, int nDelay, const char *pszDescription);
    void RemoveDiplomacyMsgs() { delayedSounds.RemoveDiplomacyMsgs(); }
    // set new PoI, at pos with power nPower, and power decrement nDecrease
    BOOL AddPoI(const CPlot3D &pos, int nPower, int nDecrease);
    int GetPoIPower() const { return nPoIPower; }
    const CPlot2D &GetPoI() const { return ptPointOfInterest; }
    void AddVisibleUnit(CUnit *pHero, CUnit* pVisible);
    void SeeNewCorpse(CUnit *pUnit, CUnit *pCorpse);
    // ignored list interface
    bool IsIgnored(CMapObject *pTarget) { return ignoredList.IsIgnored(pTarget); }
    void AddIgnoredTarget(CMapObject *pTarget, int nTime)
    {
        ignoredList.AddIgnoredTarget(pTarget, nTime);
    }
    // load&store
    void Load(CSave &file);
    void Store(CSave &file);
    friend class CAIMarkManager;
};
/////////////////////////////////////////////////////////////////////////////////////
// SAIMarkCell - one cell of footsteps grid
struct SAIMarkCell
{
    SAIMark *pMark[N_AI_MARKS_PLAYER_NUM];
    void Clear() { memset(pMark, 0, sizeof(SAIMark *) * N_AI_MARKS_PLAYER_NUM); }
#ifdef _DEBUG
    PAIMark &operator[](int i)
    {
        ASSERT(i >= 0 && i < N_AI_MARKS_PLAYER_NUM);
        return pMark[i];
    }
#else
    operator SAIMark **() { return pMark; }
#endif
};
/////////////////////////////////////////////////////////////////////////////////////
// CAIMarkManager - управление следами
class CAIMarkManager
{
    CAIMap *pAIMap; // pointer to AI map
    int nSegment;   // current segment number

    // переменные для сканирования, имитирующего зрение
    CPlot3D ptLookFrom;    // point from which unit looks
    CPlot3D ptUnitDir;     // direction of unit
    int nTileX, nTileY;    // coordinates on aimap of unit
    bool bVisionInitiated; // to avoid useless StartUpVision() calls

    // переменные для отслеживания следов
    SAIMark *pDeleteLists[N_MAX_MARK_LIFETIME][N_AI_MARKS_PLAYER_NUM]; // списки следов для удаления
    CArray2D<SAIMarkCell> cells; // список следов в каждом списке отсортирован по id

    SAIMarkCell *GetCell(float fpX, float fpY);
    void Clear();
    // функции для зрения
    // формирование списков units
    void FormLists(CUnit *pUnit, CAIUnitEnvironment &env);
    // непосредственно просмотр юнитов на предмет видимости
    void Scan(CUnit *pUnit, CAIUnitEnvironment &env);
    // инициализировать переменные, используемые для зрения
    void StartUpVision(CUnit *pUnit);
    void PrepareVision(CUnit *pUnit);
    void WantVision(CUnit *pUnit)
    {
        if (!bVisionInitiated)
            StartUpVision(pUnit);
    }
    // функции для определения факта обнаружения по нескольким параметрам, RollVision -
    // учитывает препятствия, для работы необходим StartupVision
    BOOL RollVisionLow(const CPlot3D &ptToTarget, float fpDist, float fpSenseRadius,
                       float fpVisionArc);
    BOOL RollVision(CPlot3D &dst, float fpSenseRadius, float fpVisionArc);
    // RollDetect учитывается только расстояние
    BOOL RollDetect(float fpDist, float fpSenseRadius);
    // функции для следов
    void PerceptFootstepList(SAIMark *pMark, CUnitServer *pUnitServer, CAIUnitEnvironment &env);
    void FootstepPerception(CUnitServer *pUnitServer, CAIUnitEnvironment &env);
    SAIMark *MakeNewMark(const CPlot3D &pos, int nPlayerNum);

public:
    CAIMarkManager() { Clear(); }
    ~CAIMarkManager();
    // размер карты в метрах
    BOOL SetMap(CAIMap *pMap, float fpXSize, float fpYSize);
    // добавить след, вернуть указатель на этот след, этот указатель должен быть действителен
    // в течение не меньше N_MARKING_INTERVAL сегментов
    SAIMark *AddFootStep(const CPlot3D &pos, CUnitServer *pUnitServer);
    // убрать все следы, оставленные юнитом nID
    void RemoveFootSteps(CUnitServer *pUnitServer);
    // произвести специфический звук( например, звук колдования или лязг меча при атаке )
    void ProduceSoundFootstep(CUnitServer *pUnitSrc, float fpStrength,
                              int nDelay = AI_HEAR_FOOTSTEP_DELAY, const CPlot3D *pSoundPos = 0);
    // прочувствовать новую тактическую обстановку
    void Feel(CUnitServer *pHero, CAIUnitEnvironment &env);
    // порезать старые запахи по необходимости
    void Segment(int nCurSegment);
    // load&store
    void Load(CSave &file);
    void Store(CSave &file);
};
/////////////////////////////////////////////////////////////////////////////////////
extern void BuildVisionMask(CUnit *pWatcher, CUnit *pTarget, SVisibilityMask &res);
