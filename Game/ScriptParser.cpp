#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "Script.h"
#include "ScriptParser.h"
#include "RandomGen.h"
#include "ModMobCommands.h"
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

struct SLexemeInfo
{
    LPCSTR pszAscii; // ASCII writting of lexeme
    ESCCmd cmd;
    LPCSTR pszParams;
    LPCSTR pszProcParams;
};

static SLexemeInfo baseLexemes[] = {
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
static SLexemeInfo* ModLexemes(){
 if(!ModRuntime::Current())return baseLexemes;
 static std::vector<SLexemeInfo> entries=[](){
  std::vector<SLexemeInfo> result;for(auto*p=baseLexemes;p->pszAscii;++p)result.push_back(*p);
  for(const auto&e:ModMob::registry)if(e.ready){
   const bool returnsValue=e.format[0]&&strchr("FSOG",e.format[0]);
   SLexemeInfo value={e.name,static_cast<ESCCmd>(e.id),returnsValue?e.format:nullptr,returnsValue?nullptr:e.format};
   bool replaced=false;for(auto&old:result)if(!_stricmp(old.pszAscii,e.name)){old=value;replaced=true;break;}if(!replaced)result.push_back(value);
  }
  result.push_back({nullptr,SC_UNKNOWN,nullptr,nullptr});return result;
 }();
 return entries.data();
}
#define validLexemes ModLexemes()


// узнать является ли некий идентификатор (newCmd) идентификатором функции с указанным типом
// возвращаемого значения (eReturnType) или нет
// если да функция возвращает укатель на параметры иначе	NULL
static const char *GetFunctionParams(ESCCmd newCmd, EScriptParameterType eReturnType)
{
    SLexemeInfo *pInfo = validLexemes;
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
static const char *GetProcedureParams(ESCCmd newCmd)
{
    SLexemeInfo *pInfo = validLexemes;
    while (pInfo->pszAscii)
    {
        if (pInfo->cmd == newCmd && pInfo->pszProcParams)
            return pInfo->pszProcParams;
        pInfo++;
    }
    return NULL;
}

// вернуть параметры, те что есть ( либо от функции, либо от процедуры
static const char *GetCommandParams(ESCCmd newCmd)
{
    SLexemeInfo *pInfo = validLexemes;
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

static LPCSTR GetCmdName(ESCCmd cmd)
{
    SLexemeInfo *pInfo = validLexemes;
    while (pInfo->pszAscii)
    {
        if (pInfo->cmd == cmd)
            return pInfo->pszAscii;
        pInfo++;
    }
    return "Unknown command";
}

static void GetLexemeCmd(LPCSTR pszLexeme, ESCCmd *pCmd, int *pnTrailer)
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
    SLexemeInfo *pInfo = validLexemes;
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
        if(ModRuntime::Current()&&isalpha(static_cast<unsigned char>(pInfo->pszAscii[0]))&&(isalnum(next)||next=='_'||next=='#'))continue;
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
BOOL __cdecl CScript::ScriptError(int nLine, EScriptParserError eError, const char *pszText, ...)
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

BOOL CScript::CheckNextLexeme(CLexemeList &lexemes, POSITION &pos, int cmdForCheck)
{
    if (!pos)
        return ScriptError(lexemes.GetTail().nLine, SC_UNEXPECTED_EOF, "Unexpected end of file");
    SLexeme lexInfo = lexemes.GetNext(pos);
    CString lex = lexInfo.lexeme;
    ESCCmd cmd;
    GetLexemeCmd(lex, &cmd, NULL);
    if (cmd != cmdForCheck)
        return ScriptError(lexInfo.nLine, SC_WRONG_LEXEME, "Wrong lexeme %s", lex);
    return TRUE;
}

BOOL CScript::CreateLexemesList(CString &szFile, CLexemeList &lexemes)
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
        int nLexLen = static_cast<int>(Min<ptrdiff_t>(255, bufPtr - lexemeStartPtr));
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
                GetLexemeCmd(lexeme, NULL, &nTrailer);
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

BOOL CScript::LoadFunction(SSCCmd &res, POSITION &pos, CLexemeList &lexemes, int newCmd,
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
                           GetCmdName(SC_OPEN_FUNCTION));

    while (pos)
    {
        lexInfo = lexemes.GetNext(pos);
        lex = lexInfo.lexeme;
        GetLexemeCmd(lex, &cmd, NULL);
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
                                       "Expected \'%s\'", GetCmdName(SC_OPEN_FUNCTION));
            }
            else
            {
                if (cmd != SC_COMMA)
                    return ScriptError(lexInfo.nLine, SC_EXPECTED_SYMBOL_NOT_FOUND,
                                       "Expected \'%s\'", GetCmdName(SC_COMMA));
            }

            lexInfo = lexemes.GetNext(pos);
            lex = lexInfo.lexeme;
            GetLexemeCmd(lex, &cmd, NULL);

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
            ReportError("wrong type char in CScript::LoadFunction");
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
                pszNewParams = GetFunctionParams(cmd, funcParam.eType);
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

BOOL CScript::LoadConditionsList(SScriptThread &res, POSITION &pos, CLexemeList &lexemes,
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
        GetLexemeCmd(lex, &cmd, NULL);

        if (cmd == SC_CLOSE_FUNCTION)
            break;
        pszNewParams = GetFunctionParams(cmd, ptFloat);
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

BOOL CScript::LoadOneScript(SOneScript &res, POSITION &pos, CLexemeList &lexemes)
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
        GetLexemeCmd(lex, &cmd, NULL);
        if (cmd == SC_CLOSE_FUNCTION)
            break;
        if (cmd == SC_IF)
        {
            lexInfo = lexemes.GetNext(pos);
            lex = lexInfo.lexeme;
            GetLexemeCmd(lex, &cmd, NULL);
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
            GetLexemeCmd(lex, &cmd, NULL);
            if (cmd != SC_THEN)
            {
                ScriptError(lexInfo.nLine, SC_EXPECTED_SYMBOL_NOT_FOUND, "then expected after if");
                return FALSE;
            }
            lexInfo = lexemes.GetNext(pos);
            lex = lexInfo.lexeme;
            GetLexemeCmd(lex, &cmd, NULL);
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

BOOL CScript::LoadCommandsList(SScriptThread &res, POSITION &pos, CLexemeList &lexemes,
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
        GetLexemeCmd(lex, &cmd, NULL);
        if (cmd == SC_CLOSE_FUNCTION)
            break;
        BOOL bRv = FALSE;
        const char *pszProcParams = GetProcedureParams(cmd);
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

BOOL CScript::LoadAssignOperator(SSCCmd &res, POSITION &pos, CLexemeList &lexemes,
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
        ReportError("Uknown variable type at CScript::LoadAssignOperator");
        return FALSE;
        break;
    }
    res.params[0] = (float)nVarNumber;
    return bRv;
}

BOOL CScript::LoadParameters(SOneScript &res, POSITION &pos, CLexemeList &lexemes, BOOL bSign)
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

BOOL CScript::LoadText(CString &szFile, BOOL bRun)
{
    int nScriptIdx;
    BOOL bRv = TRUE;
    CLexemeList lexemes;
    SScriptThread thread;
    SOneScript objScript;

    scriptError.nLine = 0;
    scriptError.eError = SC_NO_ERROR;
    scriptError.text = "No errors found";

    if(ModRuntime::Current()){
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
        GetLexemeCmd(lex, &cmd, NULL);
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

    if (pWorldActions && bRun)
        RunThreadDelayed(pWorldActions);
    bLoaded = TRUE;
    nSecondaryCount++;
    return TRUE;
}

BOOL CScript::AppendText(CString &szFile)
{
    if (!LoadText(szFile, FALSE))
        return FALSE;
    nSecondaryCount--;
    return TRUE;
}

BOOL CScript::Load(const char *pszFileName)
{
    CString file;
    if (FileToCString(pszFileName, file))
        return LoadText(file);
    /*	{ // check for StoreText()
                    if( LoadText( file ) )
                    {
                            StoreText( file );
                            CStdioFile f1;
                            f1.Open( "decompile.scr", CFile::modeCreate | CFile::modeWrite |
       CFile::typeText ); f1.WriteString( file ); f1.Close(); return TRUE; } else return FALSE;
            }*/
    return ScriptError(0, SC_CANT_READ_FILE, "Unable to read file \"%s\"", pszFileName);
}

int CScript::GetScriptIdx(const char *pszName)
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

CString CScript::SignName(const CString &src)
{
    if (nSecondaryCount == 0)
        return src;
    CString res;
    res.Format("!%i!%s", nSecondaryCount, src);
    return res;
}

const char *CScript::CutNumber(const char *pszName)
{
    int nNumber;
    const char *pszRes = ReadNumber(pszName, nNumber);
    if (nNumber > 0 && nNumber != nSecondaryCount - 1)
    { // return zero string then
        while (pszRes[0])
            pszRes++;
    }
    return pszRes;
}

const char *CScript::ReadNumber(const char *pszName, int &nNumber)
{
    CString str;
    const char *pszNumber, *pszAllName;
    pszAllName = pszName;
    nNumber = -1;
    if (pszName[0] == '!')
    {
        pszName++;
        str = pszName;
        pszNumber = str;
        while (pszNumber[0])
        {
            if (pszNumber[0] == '!')
            {
                str.SetAt(pszNumber - str, 0);
                nNumber = atoi(str);
                return pszName + 1;
            }
            pszNumber++;
            pszName++;
        }
        return pszAllName;
    }
    return pszName;
}

void CScript::StoreTextVars(BOOL bOneLine, CString &res, SScriptVariablesSet &vars,
                            const char *pszParams, BOOL bStoreGlobalVars)
{
    int i, nFloatParam = 0, nStringParam = 0, nObjectParam = 0, nGroupParam = 0;
    int nScriptNumber;
    CString szVarName, szFormat, szNextLine, szVarType;
    CAArray<CString> varDefinitions;

    if (!bOneLine)
        szNextLine = "\n";
    res = res + " (" + szNextLine;
    if (!pszParams)
    {
        for (i = 0; i < vars.fVars.GetSize(); i++)
            szFormat = szFormat + "f";
        for (i = 0; i < vars.szVars.GetSize(); i++)
            szFormat = szFormat + "s";
        for (i = 0; i < vars.oVars.GetSize(); i++)
            szFormat = szFormat + "o";
        for (i = 0; i < vars.gVars.GetSize(); i++)
            szFormat = szFormat + "g";
        pszParams = szFormat;
    }
    while (pszParams[0])
    {
        switch (pszParams[0])
        {
        case 'f':
            szVarName = vars.fVars.GetName(nFloatParam++);
            szVarType = "float";
            break;
        case 's':
            szVarName = vars.szVars.GetName(nStringParam++);
            szVarType = "string";
            break;
        case 'o':
            szVarName = vars.oVars.GetName(nObjectParam++);
            szVarType = "object";
            break;
        case 'g':
            szVarName = vars.gVars.GetName(nGroupParam++);
            szVarType = "group";
            break;
        }
        if (bStoreGlobalVars)
        {
            szVarName = ReadNumber(szVarName, nScriptNumber);
            // если это вторичный скрипт и это не его глобальная переменная писать ее не надо
            if (nSecondaryCount > 1 && nScriptNumber != nSecondaryCount - 1)
                szVarName = "";
            if (szVarName != "")
                varDefinitions.Add("  " + szVarName + " : " + szVarType);
        }
        else
        {
            szVarName = CutNumber(szVarName);
            if (szVarName != "")
                varDefinitions.Add("  " + szVarName + " : " + szVarType);
        }
        pszParams++;
    }
    // copy vars definitions from array to result text
    for (i = 0; i < varDefinitions.GetSize(); i++)
    {
        if (i != varDefinitions.GetSize() - 1)
            res = res + varDefinitions[i] + "," + szNextLine;
        else
            res = res + varDefinitions[i] + " ";
    }
    if (szFormat.GetLength() == 0)
        res = res + ")" + szNextLine;
    else
        res = res + szNextLine + ")" + szNextLine;
}

void CScript::StoreTextFunction(CString &res, SSCCmd cmd, SOneScript *pOneScript)
{
    BOOL bFound;
    int i, nFloatParam = 0, nStringParam = 0, nObjectParam = 0, nGroupParam = 0;
    CString szSuppose;       // значение параметра в тексте
    CString szFormat;        // формат для записи оператора присвоения и запуска
    SOneScript *pFindScript; // для определения формата вызова скрипта в случае RUNSCRIPT
    EScriptParameterType eType;
    const char *pszParams = GetCommandParams((ESCCmd)cmd.command);
    std::string concreteModFormat;
    if(ModRuntime::Current())if(auto*entry=ModMob::Find(cmd.command))if(entry->ready){
        const char*format=entry->format;if(*format&&strchr("FSOG",*format))++format;
        concreteModFormat=ModMob::Expand(format,cmd);pszParams=concreteModFormat.c_str();
    }
    // особые случаи : runscript & assignment
    if (cmd.command == SC_RUNSCRIPT)
    {
        res = res + CutNumber(cmd.szParams[0]) + "( ";
        pFindScript = GetScript(cmd.szParams[0]);
        nStringParam = 1;
        nFloatParam = 1;
        if (pFindScript)
        {
            szFormat = pFindScript->szScriptParams;
            pszParams = szFormat;
        }
    }
    else if (cmd.command == SC_GLOBALASSIGN)
    {
        switch ((int)cmd.params[1])
        {
        case 0:
            res = res + CutNumber(globalVars.fVars.GetName((int)cmd.params[0])) + " = ";
            break;
        case 1:
            res = res + CutNumber(globalVars.szVars.GetName((int)cmd.params[0])) + " = ";
            break;
        case 2:
            res = res + CutNumber(globalVars.oVars.GetName((int)cmd.params[0])) + " = ";
            break;
        case 3:
            res = res + CutNumber(globalVars.gVars.GetName((int)cmd.params[0])) + " = ";
            break;
        }
        cmd.params.RemoveAt(0);
        cmd.params.RemoveAt(0);
        // Assignment metadata occupies the first two float slots. Keep exported references aligned.
        for(int i=0;i<cmd.localParameters.GetSize();++i)if(cmd.localParameters[i].eType==ptFloat)cmd.localParameters[i].nNumber-=2;
        for(int i=0;i<cmd.globalParameters.GetSize();++i)if(cmd.globalParameters[i].eType==ptFloat)cmd.globalParameters[i].nNumber-=2;
        for(int i=0;i<cmd.funcParameters.GetSize();++i)if(cmd.funcParameters[i].eType==ptFloat)cmd.funcParameters[i].nNumber-=2;
        if (cmd.params.GetSize())
            szFormat = "f";
        else if (cmd.szParams.GetSize())
            szFormat = "s";
        else if (cmd.oParams.GetSize())
            szFormat = "o";
        else if (cmd.gParams.GetSize())
            szFormat = "g";
        pszParams = szFormat;
    }
    else
        res = res + GetCmdName((ESCCmd)cmd.command) + "( ";
    if (pszParams == NULL)
        return;
    while (pszParams[0])
    {
        int nNumber;
        switch (pszParams[0])
        {
        case 'f': {
            eType = ptFloat;
            nNumber = nFloatParam++;
            float fValue = cmd.params[nFloatParam - 1];
            if (fValue == Float2Int(fValue))
                szSuppose.Format("%i", Float2Int(fValue));
            else
                szSuppose.Format("%g", fValue);
        }
        break;
        case 's':
            eType = ptString;
            nNumber = nStringParam++;
            szSuppose = CString("\"") + cmd.szParams[nStringParam - 1] + "\"";
            break;
        case 'O':
        case 'o':
            eType = ptObject;
            nNumber = nObjectParam++;
            szSuppose = "ShitHappens";
            break;
        case 'g':
            eType = ptGroup;
            nNumber = nGroupParam++;
            szSuppose = "ShitHappens";
            break;
        }
        bFound = FALSE;
        for (i = 0; i < cmd.localParameters.GetSize(); i++)
        {
            if (cmd.localParameters[i].eType == eType && cmd.localParameters[i].nNumber == nNumber)
            {
                switch (eType)
                {
                case ptFloat:
                    res = res + pOneScript->vars.fVars.GetName(cmd.localParameters[i].nVarNumber);
                    break;
                case ptString:
                    res = res + pOneScript->vars.szVars.GetName(cmd.localParameters[i].nVarNumber);
                    break;
                case ptObject:
                    res = res + pOneScript->vars.oVars.GetName(cmd.localParameters[i].nVarNumber);
                    break;
                case ptGroup:
                    res = res + pOneScript->vars.gVars.GetName(cmd.localParameters[i].nVarNumber);
                    break;
                }
                bFound = TRUE;
                break;
            }
        }
        for (i = 0; i < cmd.globalParameters.GetSize(); i++)
        {
            if (cmd.globalParameters[i].eType == eType &&
                cmd.globalParameters[i].nNumber == nNumber)
            {
                switch (eType)
                {
                case ptFloat:
                    res = res +
                          CutNumber(globalVars.fVars.GetName(cmd.globalParameters[i].nVarNumber));
                    break;
                case ptString:
                    res = res +
                          CutNumber(globalVars.szVars.GetName(cmd.globalParameters[i].nVarNumber));
                    break;
                case ptObject:
                    res = res +
                          CutNumber(globalVars.oVars.GetName(cmd.globalParameters[i].nVarNumber));
                    break;
                case ptGroup:
                    res = res +
                          CutNumber(globalVars.gVars.GetName(cmd.globalParameters[i].nVarNumber));
                    break;
                }
                bFound = TRUE;
                break;
            }
        }
        for (i = 0; i < cmd.funcParameters.GetSize(); i++)
        {
            if (cmd.funcParameters[i].eType == eType && cmd.funcParameters[i].nNumber == nNumber)
            {
                StoreTextFunction(res, *cmd.funcParameters[i].cmd, pOneScript);
                bFound = TRUE;
                break;
            }
        }
        if (!bFound)
            res = res + szSuppose;
        pszParams++;
        if (pszParams[0])
            res = res + ", ";
    }
    if (cmd.command != SC_GLOBALASSIGN)
        res = res + " ) ";
}

// вернуть строку из nNumber пробелов
static CString GetNSpaces(int nNumber)
{
    CString res;
    for (int i = 0; i < nNumber; i++)
        res = res + " ";
    return res;
}

void CScript::StoreTextThread(int nIndent, CString &res, SScriptThread &thread,
                              SOneScript *pOneScript)
{
    int i;
    res = res + GetNSpaces(nIndent) + "(\n";
    for (i = 0; i < thread.commands.GetSize(); i++)
    {
        res = res + GetNSpaces(nIndent + 2);
        StoreTextFunction(res, thread.commands[i], pOneScript);
        if (thread.commands[i].command == SC_FOR || thread.commands[i].command == SC_FORIF)
        {
            ASSERT(thread.commands[i].pLoopBody);
            res = res + "\n";
            StoreTextThread(nIndent + 2, res, *thread.commands[i].pLoopBody, pOneScript);
        }
        else
            res = res + "\n";
    }
    res = res + GetNSpaces(nIndent) + ")\n";
}

void CScript::StoreText(CString &res)
{
    int i, k;
    POSITION pos;
    res = "";
    // save global vars
    res = "GlobalVars";
    StoreTextVars(FALSE, res, globalVars, NULL, TRUE);
    res = res + "\n";
    // declare all scripts
    for (i = 0; i < scripts.GetSize(); i++)
    {
        if (scripts[i].conditions.GetSize() == 0)
            continue; // скрипты без условий обычно заголовки от первичных скриптов
        res = res + "DeclareScript " + CutNumber(scripts[i].szScriptName);
        StoreTextVars(TRUE, res, scripts[i].vars, scripts[i].szScriptParams);
        res = res + "\n";
    }
    res = res + "\n";
    // write down all scripts
    for (i = 0; i < scripts.GetSize(); i++)
    {
        if (scripts[i].conditions.GetSize() == 0)
            continue; // скрипты без условий обычно заголовки от первичных скриптов
        res = res + "Script " + CutNumber(scripts[i].szScriptName) + "\n(\n";
        for (k = 0; k < scripts[i].conditions.GetSize(); k++)
        {
            res = res + "  if\n";
            StoreTextThread(2, res, *scripts[i].conditions[k].pConditions, &scripts[i]);
            res = res + "  then\n";
            StoreTextThread(2, res, *scripts[i].conditions[k].pActions, &scripts[i]);
        }
        res = res + ")\n\n";
    }
    // write worldscript
    pos = worldActions.GetHeadPosition();
    if (pos)
    {
        res = res + "WorldScript\n";
        StoreTextThread(0, res, worldActions.GetAt(pos), NULL);
    }
}

void CScript::PrepareForSecondary()
{
    StopExecution();
    for (int i = 0; i < scripts.GetSize(); i++)
        scripts[i].conditions.SetSize(0);
    worldActions.RemoveAll();
}

// ---------------------------------- File loading --------------------------------------------
BOOL FileToCString(const char *pszFileName, CString &res)
{
    BOOL bRv;
    TRY
    {
        res = "";
        bRv = TRUE;
        CStdioFile fin(pszFileName, CFile::modeRead | CFile::shareDenyWrite | CFile::typeText);
        char readBuf[2048];
        while (fin.ReadString(readBuf, sizeof(readBuf)) != NULL)
            res = res + CString(readBuf); // + "\n";
        fin.Close();
    }
    CATCH(CFileException, e)
    {
        bRv = FALSE;
    }
    END_CATCH;
    return bRv;
}
