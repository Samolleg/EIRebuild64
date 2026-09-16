#pragma once

#include <vector>
#include <unordered_map>

class CStructureSaver;

// --------------------------------------------------------------------------------------------
// hero acks
const DWORD ACK_HERO_ACK_SELECTION = 0;
const DWORD ACK_HERO_ACK_MOVE = 1;            // from server
const DWORD ACK_HERO_ACK_ATTACK = 2;          // from server
const DWORD ACK_HERO_ACK_CAST = 3;            // from server
const DWORD ACK_HERO_ACK_LOOT = 4;            // from server
const DWORD ACK_HERO_ACK_USE_OBJECT = 5;      // from server
const DWORD ACK_HERO_ACK_STEAL_FROM_UNIT = 6; // from server
const DWORD ACK_HERO_ACK_FOLLOW = 7;
const DWORD ACK_HERO_ACK_USE_POTION = 8;
const DWORD ACK_HERO_ACK_CHANGE_POSE = 9;
const DWORD ACK_HERO_ACK_ATTACK_TOO_BIG = 10;
// hero refuses
const DWORD ACK_HERO_REFUSE_NO_WAY_TO_ATTACK = 11;          // from server
const DWORD ACK_HERO_REFUSE_TARGET_TOO_HIGH = 12;           // from server
const DWORD ACK_HERO_REFUSE_CANT_CHANGE_POSE = 13;          // from server
const DWORD ACK_HERO_REFUSE_PATH_NOT_FOUND = 14;            // from server
const DWORD ACK_HERO_REFUSE_CANT_CAST = 15;                 // from server
const DWORD ACK_HERO_REFUSE_CANT_TELEPORT = 16;             // from server
const DWORD ACK_HERO_REFUSE_SCIENCE_FAILED = 17;            // from server
const DWORD ACK_HERO_REFUSE_CANT_SEE_TARGET = 18;           // from server
const DWORD ACK_HERO_REFUSE_TOO_COMPLEX_SPELL_TO_CAST = 19; // from server
const DWORD ACK_HERO_REFUSE_CANT_RUN_OVERLOADED = 20;
const DWORD ACK_HERO_REFUSE_CANT_RUN_INJURED = 21;
// hero refuses не проигрываемые, но передаваемые
const DWORD ACK_HERO_REFUSE_TAME = 22;          // не удалось потеймить (скила не хватило)
const DWORD ACK_HERO_REFUSE_CHANGE_WEAPON = 23; // не используется (оружие можно менять всегда)
const DWORD ACK_HERO_REFUSE_SPELL_LINK = 24;
const DWORD ACK_HERO_REFUSE_SPELL_LINK_ABORTED = 25;
const DWORD ACK_HERO_REFUSE_SPELL_CHARM = 26;
const DWORD ACK_HERO_REFUSE_CANT_START_REST =
    27; // юниты под управлением игрока не должны выполнять rest, этот облом не случается для них
// hero notifies
const DWORD ACK_HERO_NOTIFY_DECIDED_TO_ATTACK = 28;
const DWORD ACK_HERO_NOTIFY_OUT_OF_STAMINA = 29;
const DWORD ACK_HERO_NOTIFY_ARM_CRIPPLED = 30;
const DWORD ACK_HERO_NOTIFY_LEG_CRIPPLED = 31;
const DWORD ACK_HERO_NOTIFY_ATTACKED_IN_DEFENCE = 32;
const DWORD ACK_HERO_NOTIFY_WAITING_FOLLOWERS = 33;
const DWORD ACK_HERO_NOTIFY_STEAL_EMPTY = 34;
const DWORD ACK_HERO_NOTIFY_ARMOR_CRIPPLED = 35;
const DWORD ACK_HERO_NOTIFY_WEAPON_CRIPPLED = 36;
const DWORD ACK_HERO_NOTIFY_ARMOR_CRITICAL = 37;
const DWORD ACK_HERO_NOTIFY_WEAPON_CRITICAL = 38;
// whisper
const DWORD ACK_HERO_WHISPER_YES = 39;
const DWORD ACK_HERO_WHISPER_NO = 40;
// hero bored
const DWORD ACK_HERO_BORED = 41;
// hero scenario
const DWORD ACK_HERO_SCENARIO = 42;
const DWORD ACK_HERO_SCENARIO_BLOCK = 43;
// NPCs
const DWORD ACK_NPC_SELECTED_FOR_TALKING = 44;
const DWORD ACK_NPC_WHILE_IN_REST = 45;
// unit races and unit prototypes
const DWORD ACK_UNIT_BECOME_AGGRESSIVE = 46;
const DWORD ACK_UNIT_BECOME_SUSPECTIOUS = 47;
const DWORD ACK_UNIT_BECOME_KILLER = 48;
const DWORD ACK_UNIT_WHILE_IN_REST = 49;
const DWORD ACK_UNIT_WHILE_IN_AGGRESSIVE = 50;
const DWORD ACK_CLEAR = 0xFF;
const DWORD ACK_NOTHING = 0xFFFFFFFF;

// --------------------------------------------------------------------------------------------
inline bool IsHeroAck(DWORD dwAck)
{
    return dwAck <= ACK_HERO_BORED;
}
inline bool IsHeroRefuse(DWORD dwAck)
{
    return ((dwAck >= ACK_HERO_REFUSE_NO_WAY_TO_ATTACK) &&
            (dwAck <= ACK_HERO_REFUSE_CANT_RUN_INJURED));
}
inline bool IsAckGuarantie(DWORD dwAck)
{
    return (
        (dwAck == ACK_HERO_NOTIFY_ARM_CRIPPLED) || (dwAck == ACK_HERO_NOTIFY_LEG_CRIPPLED) ||
        (dwAck == ACK_HERO_NOTIFY_OUT_OF_STAMINA) || (dwAck == ACK_HERO_NOTIFY_ARMOR_CRIPPLED) ||
        (dwAck == ACK_HERO_NOTIFY_WEAPON_CRIPPLED) || (dwAck == ACK_HERO_NOTIFY_ARMOR_CRITICAL) ||
        (dwAck == ACK_HERO_NOTIFY_WEAPON_CRITICAL) || (dwAck == ACK_HERO_SCENARIO));
}
inline bool IsAckOutOfCharacter(DWORD dwAck)
{
    return true;
}
// --------------------------------------------------------------------------------------------
const DWORD ACK_RUNING = 0x00000001;
const DWORD ACK_WALKING = 0x00000002;
const DWORD ACK_SNEAKING = 0x00000004;
const DWORD ACK_CRAWLING = 0x00000008;
const DWORD ACK_GIPATH = 0x00000010;
const DWORD ACK_INGOS = 0x00000020;
const DWORD ACK_SUSLANGER = 0x00000040;
const DWORD ACK_DAY = 0x00000080;
const DWORD ACK_NIGHT = 0x00000100;
const DWORD ACK_OUTDOORS = 0x00000200;
const DWORD ACK_CAVE = 0x00000400;
const DWORD ACK_SKYRAIN = 0x00000800;
const DWORD ACK_SKYSNOW = 0x00001000;
const DWORD ACK_SKYCLEAR = 0x00002000;

// --------------------------------------------------------------------------------------------
struct SAckEntry
{
public:
    int nProbability;          // вероятность проигрывания этого звука
    std::string szFileName;    // имя файла со звуком
    std::string szDescription; // просто текстовое описание
    std::string szKey;         // ключ для идентификации в ELK
public:
    void Serialize(CStructureSaver &file);
};
typedef std::vector<SAckEntry> SAckEntryVector;

// --------------------------------------------------------------------------------------------
struct SAckEntryBored : public SAckEntry
{
public:
    DWORD dwUsageFlags;

public:
    // general flags check
    bool IsFlags(DWORD dwFlags) const { return (dwUsageFlags & dwFlags) == dwFlags; }
    // state
    DWORD IsRuning() const { return dwUsageFlags & ACK_RUNING; }
    DWORD IsWalking() const { return dwUsageFlags & ACK_WALKING; }
    DWORD IsSneaking() const { return dwUsageFlags & ACK_SNEAKING; }
    DWORD IsCrawling() const { return dwUsageFlags & ACK_CRAWLING; }
    // allod
    DWORD IsOnGipath() const { return dwUsageFlags & ACK_GIPATH; }
    DWORD IsOnIngos() const { return dwUsageFlags & ACK_INGOS; }
    DWORD IsOnSuslanger() const { return dwUsageFlags & ACK_SUSLANGER; }
    // time
    DWORD IsAtDay() const { return dwUsageFlags & ACK_DAY; }
    DWORD IsAtNight() const { return dwUsageFlags & ACK_NIGHT; }
    // outdoors/cave
    DWORD IsOutdoors() const { return dwUsageFlags & ACK_OUTDOORS; }
    DWORD IsInCave() const { return dwUsageFlags & ACK_CAVE; }
    // weather
    DWORD IsWeatherRain() const { return dwUsageFlags & ACK_SKYRAIN; }
    DWORD IsWeatherSnow() const { return dwUsageFlags & ACK_SKYSNOW; }
    DWORD IsWeatherClear() const { return dwUsageFlags & ACK_SKYCLEAR; }

    void Serialize(CStructureSaver &file);
};
typedef std::vector<SAckEntryBored> SAckEntryBoredVector;
// --------------------------------------------------------------------------------------------
struct SAckBase
{
public:
    std::string szName;
    std::unordered_map<DWORD, SAckEntryVector *> acks;

public:
    void Serialize(CStructureSaver &file);

    const std::string &GetName() const { return szName; }
    const std::string &GetCode() const { return szName; }
};
// --------------------------------------------------------------------------------------------
struct SAckHeroAck : public SAckBase
{
public:
    // acks
    SAckEntryVector ackSelection;
    SAckEntryVector ackMove;
    SAckEntryVector ackAttack;
    SAckEntryVector ackCast;
    SAckEntryVector ackLoot;
    SAckEntryVector ackUseObject;
    SAckEntryVector ackStealFromUnit;
    SAckEntryVector ackFollow;
    SAckEntryVector ackUsePotion;
    SAckEntryVector ackChangePosition;
    SAckEntryVector ackAttackTooBig;
    // refuses
    SAckEntryVector ackNoWayToAttack;
    SAckEntryVector ackTargetTooHigh;
    SAckEntryVector ackCantChangePosition;
    SAckEntryVector ackPathNotFound;
    SAckEntryVector ackCantCast;
    SAckEntryVector ackCantTeleport;
    SAckEntryVector ackScienceFailed;
    SAckEntryVector ackCantSeeTarget;
    SAckEntryVector ackTooComplexSpellToCast;
    SAckEntryVector ackCantRunOverloaded;
    SAckEntryVector ackCantRunInjured;
    // notifies
    SAckEntryVector ackDeciedToAttackUnit;
    SAckEntryVector ackOutOfStamina;
    SAckEntryVector ackArmCrippled;
    SAckEntryVector ackLegCrippled;
    SAckEntryVector ackArmorCrippled;
    SAckEntryVector ackWeaponCrippled;
    SAckEntryVector ackAttackedInDefence;
    SAckEntryVector ackWaitingFollowers;
    SAckEntryVector ackStealEmpty;
    // whisper
    SAckEntryVector ackWhisperYes;
    SAckEntryVector ackWhisperNo;
    // bored
    SAckEntryBoredVector ackBored;
    // scenario
    SAckEntryVector ackScenario;

public:
    void Serialize(CStructureSaver &file);
};
// --------------------------------------------------------------------------------------------
struct SAckNPC : public SAckBase
{
public:
    SAckEntryVector ackSelectedForTalking;
    SAckEntryVector ackWhileInRestState;

public:
    void Serialize(CStructureSaver &file);
};
// --------------------------------------------------------------------------------------------
struct SAckUnitGeneral : public SAckBase
{
public:
    SAckEntryVector ackBecomeAggressive;
    SAckEntryVector ackBecameSuspectious;
    SAckEntryVector ackBecomeKiller;
    SAckEntryVector ackWhileInRestState;
    SAckEntryVector ackWhileInAggressiveState;

public:
    void Serialize(CStructureSaver &file);
};
