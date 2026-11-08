#include "ScriptCompiler.h"
#include "ModMobRegistry.h"
#include <vector>
#include <cctype>
static const char C_STRING_SEPARATOR = '"';


static const SLexemeInfo baseLexemes[] = {
    { "(", SC_OPEN_FUNCTION, NULL, NULL },
    { ")", SC_CLOSE_FUNCTION, NULL, NULL },
    { ",", SC_COMMA, NULL, NULL },
    { ":", SC_COLON, NULL, NULL },
    { "=", SC_EQUAL, NULL, NULL },
    { "WorldScript", SC_WORLDSCRIPT, NULL, NULL },
    { "Script", SC_ONESCRIPT, NULL, NULL },
    { "DeclareScript", SC_DECLARESCRIPT, NULL, NULL },
    { "GlobalVars", SC_GLOBALVARS, NULL, NULL },
    { "if", SC_IF, NULL, NULL },
    { "then", SC_THEN, NULL, NULL },
    { "ActivateTrap", SC_ACTIVATETRAP, NULL, "of" },
    { "AddLoot", SC_ADDLOOT, NULL, "fss" },
    { "AddRectToArea", SC_ADDRECTTOAREA, NULL, "fffff" },
    { "AddRoundToArea", SC_ADDROUNDTOAREA, NULL, "ffff" },
    { "AddMob", SC_ADDMOB, NULL, "s" },
    { "AddObject", SC_ADDOBJECT, NULL, "go" },
    { "AddUnitToParty", SC_ADDUNITTOPARTY, NULL, "fss" },
    { "AddUnitToServer", SC_ADDUNITTOSERVER, NULL, "sssfff" },
    { "AddUnitUnderControl", SC_ADDUNITUNDERCONTROL, NULL, "fo" },
    { "AttachParticleSource", SC_ATTACHPARTICLESOURCE, NULL, "ff" },
    { "AttachParticles", SC_ATTACHPARTICLESOURCE1, NULL, "fo" },
    { "Attack", SC_ATTACK, NULL, "oo" },
    { "BlockUnit", SC_BLOCKUNIT, NULL, "of" },
    { "Cast", SC_CAST, NULL, "off" },
    { "CastSpellPoint", SC_CASTSPELLPOINT, NULL, "sffff" },
    { "CastSpellUnit", SC_CASTSPELLUNIT, NULL, "sffo" },
    { "ConsoleFloat", SC_CONSOLEFLOAT, NULL, "f" },
    { "ConsoleString", SC_CONSOLESTRING, NULL, "s" },
    { "CopyItems", SC_COPYITEMS, NULL, "fss" },
    { "CopyLoot", SC_COPYLOOT, NULL, "fss" },
    { "CopyStats", SC_COPYSTATS, NULL, "fss" },
    { "Crawl", SC_CRAWL, NULL, "o" },
    { "CreateFX", SC_CREATEFX, NULL, "fffffs" },
    { "CreateFXSource", SC_CREATEFXSOURCE, NULL, "ffffffs" },
    { "CreateLightning", SC_CREATELIGHTNING, NULL, "ffffffff" },
    { "CreateParticleSource", SC_CREATEPARTICLESOURCE, NULL, "fffffs" },
    { "CreateParty", SC_CREATEPARTY, NULL, "fs" },
    { "CreatePointLight", SC_CREATEPOINTLIGHT, NULL, "ffffffff" },
    { "CreateRandomizedFXSource", SC_CREATERANDOMIZEDFXSOURCE, NULL, "fffffffffs" },
    { "DeleteArea", SC_DELETEAREA, NULL, "f" },
    { "DeleteFXSource", SC_DELETEFXSOURCE, NULL, "f" },
    { "DeleteLightning", SC_DELETELIGHTNING, NULL, "f" },
    { "DeleteParticleSource", SC_DELETEPARTICLESOURCE, NULL, "f" },
    { "DeletePointLight", SC_DELETEPOINTLIGHT, NULL, "f" },
    { "Follow", SC_FOLLOW, NULL, "oo" },
    { "EnableLever", SC_ENABLELEVER, NULL, "of" },
    { "EraseQuestItem", SC_ERASEQUESTITEM, NULL, "ff" },
    { "FixItems", SC_FIXITEMS, NULL, "" },
    { "FixWorldTime", SC_FIXWORLDTIME, NULL, "f" },
    { "GSSetVar", SC_GSSETVAR, NULL, "fsf" },
    { "GSSetVarMax", SC_GSSETVARMAX, NULL, "fsf" },
    { "GSDelVar", SC_GSDELVAR, NULL, "fs" },
    { "GiveDexterity", SC_GIVEDEXTERITY, NULL, "of" },
    { "GiveIntelligence", SC_GIVEINTELLIGENCE, NULL, "of" },
    { "GiveItem", SC_GIVEITEM, NULL, "fs" },
    { "GiveMoney", SC_GIVEMONEY, NULL, "ff" },
    { "GiveQuestItem", SC_GIVEQUESTITEM, NULL, "fs" },
    { "GiveSkill", SC_GIVESKILL, NULL, "osf" },
    { "GiveStrength", SC_GIVESTRENGTH, NULL, "of" },
    { "GiveUnitQuestItem", SC_GIVEUNITQUESTITEM, NULL, "os" },
    { "GiveUnitSpell", SC_GIVEUNITSPELL, NULL, "os" },
    { "GodMode", SC_GODMODE, NULL, "ff" },
    { "Guard", SC_GUARD, NULL, "offf" },
    { "HideObject", SC_HIDEOBJECT, NULL, "of" },
    { "Idle", SC_IDLE, NULL, "o" },
    { "InvokeAlarm", SC_INVOKEALARM, NULL, "fff" },
    { "InflictDamage", SC_INFLICTDAMAGE, NULL, "of" },
    { "KillScript", SC_KILLSCRIPT, NULL, "" },
    { "KillUnit", SC_KILLUNIT, NULL, "o" },
    { "LeaveToZone", SC_LEAVETOZONE, NULL, "fsf" },
    { "Lie", SC_LIE, NULL, "o" },
    { "MoveParticleSource", SC_MOVEPARTICLESOURCE, NULL, "ffffff" },
    { "MovePointLight", SC_MOVEPOINTLIGHT, NULL, "ffffff" },
    { "MoveToPoint", SC_MOVETO, NULL, "off" },
    { "MoveToObject", SC_MOVETOOBJECT, NULL, "oo" },
    { "PlayAnimation", SC_PLAYANIMATION, NULL, "os" },
    { "PlayFX", SC_PLAYSFX, NULL, "ffffs" },
    { "PlayCamera", SC_PLAYCAMERA, NULL, "s" },
    { "PlayMovie", SC_PLAYMOVIE, NULL, "s" },
    { "PlayMusic", SC_PLAYMUSIC, NULL, "fs" },
    { "SetPlayerAggression", SC_SETPLAYERAGGRESSION, NULL, "ff" },
    { "QStart", SC_QSTART, NULL, "s" },
    { "QFinish", SC_QFINISH, NULL, "" },
    { "QObjArea", SC_QOBJAREA, NULL, "f" },
    { "QObjSeeObject", SC_QOBJSEEOBJECT, NULL, "s" },
    { "QObjSeeUnit", SC_QOBJSEEUNIT, NULL, "s" },
    { "QObjKillUnit", SC_QOBJKILLUNIT, NULL, "s" },
    { "QObjKillGroup", SC_QOBJKILLGROUP, NULL, "s" },
    { "QObjUse", SC_QOBJUSE, NULL, "sf" },
    { "QObjGetItem", SC_QOBJGETITEM, NULL, "f" },
    { "QuestComplete", SC_QUESTCOMPLETE, NULL, "fs" },
    { "RecalcMercBriefings", SC_RECALCMERCBRIEFINGS, NULL, "" },
    { "RedeployParty", SC_REDEPLOYPARTY, NULL, "f" },
    { "RemoveParty", SC_REMOVEPARTY, NULL, "fs" },
    { "RemoveObject", SC_REMOVEOBJECT, NULL, "go" },
    { "RemoveObjectFromServer", SC_REMOVEOBJECTFROMSERVER, NULL, "o" },
    { "RemoveQuestItem", SC_REMOVEQUESTITEM, NULL, "of" },
    { "RemoveUnitFromControl", SC_REMOVEUNITFROMCONTROL, NULL, "fo" },
    { "RemoveUnitFromParty", SC_REMOVEUNITFROMPARTY, NULL, "fs" },
    { "RemoveUnitFromServer", SC_REMOVEUNITFROMSERVER, NULL, "o" },
    { "ResetTarget", SC_RESETTARGET, NULL, "o" },
    { "Rest", SC_REST, NULL, "o" },
    { "RotateTo", SC_ROTATETO, NULL, "off" },
    { "Run", SC_RUN, NULL, "o" },
    { "RunWorldTime", SC_RUNWORLDTIME, NULL, "f" },
    { "SendEvent", SC_SENDEVENT, NULL, "ff" },
    { "SendStringEvent", SC_SENDSTRINGEVENT, NULL, "fs" },
    { "Sentry", SC_SENTRY, NULL, "off" },
    { "SetBackGroundColor", SC_SETBG, NULL, "fff" },
    { "SetCameraOrientation", SC_SETCAMERAORIENTATION, NULL, "f" },
    { "SetCameraPosition", SC_SETCAMERA, NULL, "fff" },
    { "SetCP", SC_SETCP, NULL, "offf" },
    { "SetCPFast", SC_SETCPFAST, NULL, "offf" },
    { "SetCurrentParty", SC_SETCURRENTPARTY, NULL, "fs" },
    { "SetDiplomacy", SC_SETDIPLOMACY, NULL, "fff" },
    { "SetDirectionToObject", SC_SETDIRTOOBJECT, NULL, "f" },
    { "SetEnemy", SC_SETENEMY, NULL, "of" },
    { "SetParticleSourceSize", SC_SETPARTICLESOURCESIZE, NULL, "ff" },
    { "SetPlayer", SC_SETPLAYER, NULL, "of" },
    { "SetScience", SC_SETSCIENCE, NULL, "offffff" },
    { "SetSpellAggression", SC_SETSPELLAGGRESSION, NULL, "of" },
    { "SetSunLight", SC_SETSUNLIGHT, NULL, "fffffffff" },
    { "SetWaterLevel", SC_SETWATERLEVEL, NULL, "fff" },
    { "SetWind", SC_SETWIND, NULL, "ffff" },
    { "Sleep", SC_SLEEP, NULL, "f" },
    { "SleepUntil", SC_SLEEPUNTIL, NULL, "f" },
    { "SleepUntilIdle", SC_SLEEPUNTILIDLE, NULL, "o" },
    { "Stand", SC_STAND, NULL, "o" },
    { "StartAnimation", SC_STARTANIMATION, NULL, "os" },
    { "ShowCredits", SC_SHOWCREDITS, NULL, "s" },
    { "ShowBitmap", SC_SHOWBMP, NULL, "fs" },
    { "SwitchLeverState", SC_SWITCHLEVERSTATE, NULL, "of" },
    { "SwitchLeverStateEx", SC_SWITCHLEVERSTATEEX, NULL, "off" },
    { "UMAg", SC_UM_AG, NULL, "of" },
    { "UMAggression", SC_UM_AGGRESSION, NULL, "o" },
    { "UMClear", SC_UM_CLEAR, NULL, "o" },
    { "UMCorpseWatcher", SC_UM_CORPSEWATCHER, NULL, "o" },
    { "UMFear", SC_UM_FEAR, NULL, "of" },
    { "UMFollow", SC_UM_FOLLOW, NULL, "oo" },
    { "UMGuard", SC_UM_GUARD, NULL, "offf" },
    { "UMGuardEx", SC_UM_GUARDEX, NULL, "offff" },
    { "UMPatrol", SC_UM_PATROL, NULL, "o" },
    { "UMPatrolAddPoint", SC_UM_PATROLADDPOINT, NULL, "off" },
    { "UMPatrolAddPointLook", SC_UM_PATROLADDPOINTLOOK, NULL, "offf" },
    { "UMPatrolClear", SC_UM_PATROLCLEAR, NULL, "o" },
    { "UMPlayer", SC_UM_PLAYER, NULL, "o" },
    { "UMRevenge", SC_UM_REVENGE, NULL, "o" },
    { "UMSentry", SC_UM_SENTRY, NULL, "off" },
    { "UMStandard", SC_UM_STANDARD, NULL, "o" },
    { "UMSuspection", SC_UM_SUSPECTION, NULL, "o" },
    { "WaitEndAnimation", SC_WAITENDANIMATION, NULL, "" },
    //	{ "WaitEndMoving", SC_WAITENDMOVING ,                       NULL,   "o" },
    { "WaitSegment", SC_WAITSEGMENT, NULL, "f" },
    { "Walk", SC_WALK, NULL, "o" },

    { "For", SC_FOR, NULL, "Og" },
    { "ForIf", SC_FORIF, NULL, "Ogf" },

    { "Every", SC_EVERY, "FOgf", NULL },
    { "Any", SC_ANY, "FOgf", NULL },
    { "Sum", SC_SUM, "FOgf", NULL },

    { "IsEqual", SC_ISEQUAL, "Fff", NULL },
    { "IsLess", SC_ISLESS, "Fff", NULL },
    { "IsGreater", SC_ISGREATER, "Fff", NULL },
    { "IsEqualString", SC_ISEQUALSTRING, "Fss", NULL },
    { "Add", SC_ADD, "Fff", NULL },
    { "Sub", SC_SUB, "Fff", NULL },
    { "Mul", SC_MUL, "Fff", NULL },
    { "Div", SC_DIV, "Fff", NULL },
    { "Not", SC_NOT, "Ff", NULL },
    { "HaveItem", SC_HAVEITEM, "Fff", NULL },
    { "GetObject", SC_GETOBJECT, "Of", NULL },
    { "GetObjectByName", SC_GETOBJECTBYNAME, "Os", NULL },
    { "GetObjectByID", SC_GETOBJECTBYID, "Os", NULL },
    { "IsInSquare", SC_ISINSQUARE, "Fffffff", NULL },
    { "UnitInSquare", SC_UNITINSQUARE, "Foffff", NULL },
    { "GetMercsNumber", SC_GETMERCSNUMBER, "Ff", NULL },
    { "GetZValue", SC_GETZVALUE, "Fff", NULL },
    { "GetBSZValue", SC_GETBSZVALUE, "Fff", NULL },
    { "HP", SC_HP, "Fo", NULL },
    { "MaxHP", SC_MAXHP, "Fo", NULL },
    { "Mana", SC_MANA, "Fo", NULL },
    { "MaxMana", SC_MAXMANA, "Fo", NULL },
    { "DistanceUnitUnit", SC_DISTANCEUNITUNIT, "Foo", NULL },
    { "DistanceUnitPoint", SC_DISTANCEUNITPOINT, "Foff", NULL },
    { "Random", SC_RANDOM, "Ff", NULL },
    { "GetLeverState", SC_GETLEVERSTATE, "Fo", NULL },
    { "GetX", SC_GETX, "Fo", NULL },
    { "GetY", SC_GETY, "Fo", NULL },
    { "GetZ", SC_GETZ, "Fo", NULL },
    { "GetFutureX", SC_GETFUTUREX, "Fof", NULL },
    { "GetFutureY", SC_GETFUTUREY, "Fof", NULL },
    { "GroupSize", SC_GROUPSIZE, "Fg", NULL },
    { "GroupAdd", SC_GROUPADD, "Ggg", NULL },
    { "GroupSub", SC_GROUPSUB, "Ggg", NULL },
    { "GroupCross", SC_GROUPCROSS, "Ggg", NULL },
    { "GroupHas", SC_GROUPHAS, "Fgo", NULL },
    //	{ "GSHasVar", SC_GSHASVAR ,                                   "Ffs",  NULL },
    { "GSGetVar", SC_GSGETVAR, "Ffs", NULL },
    { "IsUnitInWater", SC_ISUNITINWATER, "Fo", NULL },
    { "UnitSee", SC_UNITSEE, "Go", NULL },
    { "GroupSee", SC_GROUPSEE, "Gg", NULL },
    { "PlayerSee", SC_PLAYERSEE, "Gf", NULL },
    { "GetDiplomacy", SC_GETDIPLOMACY, "Fff", NULL },
    { "IsEnemy", SC_ISENEMY, "Fof", NULL },
    { "IsAlive", SC_ISALIVE, "Fo", NULL },
    { "IsCameraPlaying", SC_ISCAMERAPLAYING, "F", NULL },
    { "IsDead", SC_ISDEAD, "Fo", NULL },
    { "IsPlayerInDanger", SC_ISPLAYERINDANGER, "Ff", NULL },
    { "IsPlayerInSafety", SC_ISPLAYERINSAFETY, "Ff", NULL },
    { "GetPlayerUnits", SC_GETPLAYERUNITS, "Gf", NULL },
    { "GetUnitOfPlayer", SC_GETUNITOFPLAYER, "Off", NULL },
    { "GetPlayer", SC_GETPLAYER, "Fo", NULL },
    { "GetObjectID", SC_GETOBJECTID, "Fo", NULL },
    { "GetAIClass", SC_GETAICLASS, "Fo", NULL },
    { "GetLootItemsCount", SC_GETLOOTITEMSCOUNT, "Ff", NULL },
    { "GetMoney", SC_GETMONEY, "Ff", NULL },
    { "IsAlarm", SC_ISALARM, "Ff", NULL },
    { "AlarmTime", SC_ALARMTIME, "Ff", NULL },
    { "AlarmPosX", SC_ALARMPOSX, "Ff", NULL },
    { "AlarmPosY", SC_ALARMPOSY, "Ff", NULL },
    { "IsNight", SC_ISNIGHT, "F", NULL },
    { "GetWorldTime", SC_GETWORLDTIME, "F", NULL },
    { "IsInArea", SC_ISINAREA, "Ffff", NULL },
    { "WasLooted", SC_WASLOOTED, "Fo", NULL },
    { "IsUnitVisible", SC_UNITISVISIBLE, "Fo", NULL },
    { "GetLeader", SC_GETLEADER, "O", NULL },
    { "IsUnitBlocked", SC_ISBLOCKED, "Fo", NULL },
    { 0, SC_UNKNOWN, NULL }
};
const SLexemeInfo* ScriptLanguage::Lexemes() const {
 if(!extended)return baseLexemes;
 static std::vector<SLexemeInfo> entries=[](){
  std::vector<SLexemeInfo> result;for(auto*p=baseLexemes;p->pszAscii;++p)result.push_back(*p);
  for(const auto&e:ModMob::registry)if(e.ready){
   const bool returnsValue=e.format[0]&&strchr("FSOG",e.format[0]);
   SLexemeInfo value={e.name,static_cast<ESCCmd>(e.id),returnsValue?e.format:nullptr,returnsValue?nullptr:e.format};
   bool replaced=false;for(auto&old:result)if(!_stricmp(old.pszAscii,e.name)){old=value;replaced=true;break;}if(!replaced)result.push_back(value);
  }
  result.push_back({nullptr,SC_UNKNOWN,nullptr,nullptr});return result;
 }();return entries.data();
}



// узнать является ли некий идентификатор (newCmd) идентификатором функции с указанным типом
// возвращаемого значения (eReturnType) или нет
// если да функция возвращает укатель на параметры иначе	NULL
const char *GetFunctionParams(ScriptLanguage language, ESCCmd newCmd, int eReturnType)
{
    const SLexemeInfo *pInfo = language.Lexemes();
    while (pInfo->pszAscii)
    {
        if (pInfo->cmd == newCmd && pInfo->pszParams)
        {
            switch (pInfo->pszParams[0])
            {
            case 'F':
                if (eReturnType == ptFloat)
                    return pInfo->pszParams + 1;
                break;
            case 'S':
                if (eReturnType == ptString)
                    return pInfo->pszParams + 1;
                break;
            case 'O':
                if (eReturnType == ptObject)
                    return pInfo->pszParams + 1;
                break;
            case 'G':
                if (eReturnType == ptGroup)
                    return pInfo->pszParams + 1;
                break;
            }
        }
        pInfo++;
    }
    return NULL;
}

// аналогично GetFunctionParams
const char *GetProcedureParams(ScriptLanguage language, ESCCmd newCmd)
{
    const SLexemeInfo *pInfo = language.Lexemes();
    while (pInfo->pszAscii)
    {
        if (pInfo->cmd == newCmd && pInfo->pszProcParams)
            return pInfo->pszProcParams;
        pInfo++;
    }
    return NULL;
}

// вернуть параметры, те что есть ( либо от функции, либо от процедуры
const char *GetCommandParams(ScriptLanguage language, ESCCmd newCmd)
{
    const SLexemeInfo *pInfo = language.Lexemes();
    while (pInfo->pszAscii)
    {
        if (pInfo->cmd == newCmd)
        {
            if (pInfo->pszProcParams)
                return pInfo->pszProcParams;
            if (pInfo->pszParams)
                return pInfo->pszParams + 1;
        }
        pInfo++;
    }
    return NULL;
}

LPCSTR GetCmdName(ScriptLanguage language, ESCCmd cmd)
{
    const SLexemeInfo *pInfo = language.Lexemes();
    while (pInfo->pszAscii)
    {
        if (pInfo->cmd == cmd)
            return pInfo->pszAscii;
        pInfo++;
    }
    return "Unknown command";
}

void GetLexemeCmd(ScriptLanguage language, LPCSTR pszLexeme, ESCCmd *pCmd, int *pnTrailer)
{
    int nMaxFoundLen; // длина самой длиной подходящей лексемы
    // Check for float digit
    if (isdigit(static_cast<unsigned char>(*pszLexeme)) || *pszLexeme == '.' || *pszLexeme == '-')
    {
        int nPointNum = 0;
        if (*pszLexeme++ == '.')
            nPointNum++;
        for (; *pszLexeme && (isdigit(static_cast<unsigned char>(*pszLexeme)) || *pszLexeme == '.'); pszLexeme++)
            if (*pszLexeme == '.' && nPointNum++)
                break;
        if (pCmd)
            *pCmd = SC_DIGIT;
        if (pnTrailer)
            for (*pnTrailer = 0; *pszLexeme++; (*pnTrailer)++)
                ;
        return;
    }
    // Check for string
    if (*pszLexeme == C_STRING_SEPARATOR)
    {
        pszLexeme++;
        while (*pszLexeme && *pszLexeme != C_STRING_SEPARATOR)
            pszLexeme++;
        if (*pszLexeme)
        {
            if (pCmd)
                *pCmd = SC_STRING;
            if (pnTrailer)
                for (*pnTrailer = -1; *pszLexeme++; (*pnTrailer)++)
                    ;
            return;
        }
    }
    // Check for keyword
    const SLexemeInfo *pInfo = language.Lexemes();
    if (pnTrailer)
        *pnTrailer = 0;
    if (pCmd)
        *pCmd = SC_UNKNOWN;
    nMaxFoundLen = -1;
    for (; pInfo->pszAscii; pInfo++)
    {
        int nValidLexLen = strlen(pInfo->pszAscii);
        if (nValidLexLen < nMaxFoundLen || nValidLexLen > int(strlen(pszLexeme)))
            continue;
        unsigned char next=static_cast<unsigned char>(pszLexeme[nValidLexLen]);
        if(language.extended&&isalpha(static_cast<unsigned char>(pInfo->pszAscii[0]))&&(isalnum(next)||next=='_'||next=='#'))continue;
        if (strnicmp(pszLexeme, pInfo->pszAscii, nValidLexLen) == 0)
        {
            nMaxFoundLen = nValidLexLen;
            if (pnTrailer)
                *pnTrailer = strlen(pszLexeme) - nValidLexLen;
            if (pCmd)
                *pCmd = pInfo->cmd;
        }
    }
    return;
}

