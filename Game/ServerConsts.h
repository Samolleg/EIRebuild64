#pragma once
#include "SaveSchema.h"
/*==========================================================================================================

        Server constants

        Written by Ivanov Evgeny
        Nival Interactive, 1998

==========================================================================================================*/
#include "SaveLoader.h"
#include "BasicChunk.h"
#include "plot.h"
#include "quaternion.h"

/////////////////////////////////////////////////////////////////////////////////////
// different unit related consts
enum EUnitPose
{
    UP_LAY = 0,
    UP_CRAWL = 1,
    UP_STAND = 2,
    UP_RUN = 3,
    UP_REST = 4
};
/////////////////////////////////////////////////////////////////////////////////////
enum EUnitAnimationSpeed
{
    UAS_NORMAL_ = 0,
    UAS_FROZEN = 1,
    UAS_MOVEMENT = 2
};
/////////////////////////////////////////////////////////////////////////////////////
enum EUnitLocomotionSpeed
{
    ULS_NORMAL = 0,
    ULS_FROZEN = 1
};
/////////////////////////////////////////////////////////////////////////////////////
// атакуют ли юниты врагов по собственной инициативе, используется в AI, UMPlayer
enum EUnitAggression
{
    PAS_STEALTH,
    PAS_AGGRESSIVE,
};
/////////////////////////////////////////////////////////////////////////////////////
enum ETameLevel
{
    UTL_NONE = 0,
    UTL_NOTENEMY = 1,
    UTL_ALLY = 2,
    UTL_CONTROL = 3
};
/////////////////////////////////////////////////////////////////////////////////////
enum EUnitCastFailure
{
    UCS_OK,
    UCS_NOMANA,
    UCS_RANGE,
    UCS_NOSPELL,
    UCS_FEEBLED
};
/////////////////////////////////////////////////////////////////////////////////////
enum EWorldDangerState
{
    WDS_NORMAL,
    WDS_WARRY,
    WDS_COMBAT
};
/////////////////////////////////////////////////////////////////////////////////////
// Messages
const LPCSTR PSZ_MESSAGES_FILE_NAME = "SMessBase.reg";

// Time to one segment in ms, currently 15 per second
extern DWORD DW_SEGMENT_TIME;

// Default duration of one segment
const DWORD DW_DEFAULT_SEGMENT_TIME = 55;

// Maximum number of route points per single route update
const int N_RP_PER_UPDATE_MAX = 30;

// Moving delay when unit moves with its original speed
const int N_BASE_UNIT_DELAY = 20;

// Number of segments between two objects updates
const int N_CAMERA_UPDATE_SEGMENTS = 18;

// Visual range of camera
const float FP_VISUAL_RANGE = 100.f;

// types of supported weather
enum EWeather
{
    EW_CLEAR,
    EW_RAIN,
    EW_SNOW
};
// number of lightning sounds
const int N_WEATHER_NUMBER_OF_LSOUNDS = 3;

class CPersonage;
typedef CArray<CPersonage *, CPersonage *> CPersonages;
/////////////////////////////////////////////////////////////////////////////////////
// идентификатор 0 зарезервирован
struct SPartyID
{
    void Load(SaveSchema::Reader &);
    void Store(SaveSchema::Writer &) const;
    int nSomeNumber;

    SPartyID() { nSomeNumber = 0; }
    SPartyID(int nNum)
        : nSomeNumber(nNum)
    {
    }
    bool operator==(const SPartyID &id) const { return nSomeNumber == id.nSomeNumber; }
    bool IsEmpty() const { return nSomeNumber == 0; }
    void Serialize(CStructureSaver &file) { file.Add(1, nSomeNumber); }
    void Load(CSave &) { SaveSchema::RequireDynamicSchema(); }
    void Store(CSave &) { SaveSchema::RequireDynamicSchema(); }
};
/////////////////////////////////////////////////////////////////////////////////////
// имя переменной партии, содержащей глобальное время
const char szPartyGlobalTimeVarName[] = "gtime";
/////////////////////////////////////////////////////////////////////////////////////
// возможные интерфейсы
enum EInterface
{
    IFACE_NONE,
    IFACE_MISSION,
    IFACE_MAINMENU,
    IFACE_BASE,
    IFACE_GLOBAL_MAP,
    IFACE_CAMP,
    IFACE_AUTOPLAY,
    IFACE_DEBUG,
    IFACE_ENDGAME,
    IFACE_EXCHANGE
};
/////////////////////////////////////////////////////////////////////////////////////
enum ELMPAllod
{
    LMPA_GIPAT,
    LMPA_INGOS,
    LMPA_SUSLANGER,
    LMPA_RIK,
    LMPA_NONE
};
/////////////////////////////////////////////////////////////////////////////////////
// режимы оперирования сервера, от них зависит поведение сервера при
// подключении новых игроков
enum EServerMode
{
    SERV_MODE_NONE,
    SERV_MODE_LOCAL,
    SERV_MODE_LMP,
    SERV_MODE_MMP,
    SERV_MODE_INTERFACE, // server is running zone for mainmenu etc
};
/////////////////////////////////////////////////////////////////////////////////////
// информация о положении юнита для постановки на карту
struct SDeployUnitInfo
{
    int nID;
    CPlot3D ptPlace;
    CQuaternion orient;
    EUnitPose pose;
    DWORD dwCurrentWeapon;

    SDeployUnitInfo() = default;
    SDeployUnitInfo(int _nID, const CPlot3D &_ptPlace, const CQuaternion &_quat, EUnitPose _pose,
                    DWORD _dwWeapon)
        : nID(_nID)
        , ptPlace(_ptPlace)
        , orient(_quat)
        , pose(_pose)
        , dwCurrentWeapon(_dwWeapon)
    {
    }
};
/////////////////////////////////////////////////////////////////////////////////////
struct SDeployPlayerInfo
{
    CArray<SDeployUnitInfo> units;

    void Clear() { units.RemoveAll(); }
    SDeployUnitInfo *GetInfo(int nID)
    {
        for (int i = 0; i < units.GetSize(); i++)
            if (units[i].nID == nID)
                return &units[i];
        return 0;
    }
};
/////////////////////////////////////////////////////////////////////////////////////
