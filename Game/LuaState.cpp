#include "LuaState.h"
#include "luajit-x86/LuaJit.h"
#include "luajit-x86/lj_obj.h"
#include "Shared/Common.h"
#include "Notification.h"
#include "luajit-x86/lua.hpp"
#include "StdAfx.h"
#include "GameJournal.h"
#include "GameBase.h"
#include "RpgSystemConsts.h"
#include "RpgSystem.h"
#include "Basic.h"
#include "GeneralSettings.h"
#include "Unit.h"
#include "PerkDatabase.h"
#include "ItemDatabase.h"
#include "Unit.h"
#include "UnitServer.h"
#include "Player.h"
#include "SoundManager.h"
#include "WorldClient.h"
#include "WorldServer.h"
//#include "EIClasses/EIClasses.h"
//#include "EIClasses/item.h"
//#include "EIClasses/mfc.h"
//#include "EIClasses/GSVars.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <functional>
#include <sstream>

//using namespace EIClasses;
//using namespace Lua;

Lua::CLuaState Lua::State;
#define STATE ((lua_State *)state)

Lua::CLuaState::~CLuaState()
{
    if (STATE)
    {
        lua_close(STATE);
    }
}
//void AddTrailingSlash(string& s)
//{
//	if (!s.empty() && s[s.length() - 1] != '\\')
//		s += '\\';
//}
void Lua::CLuaState::Initialize(const std::string &path, const std::string &fileName)
{
    std::string pathWithSlash = path;
    //AddTrailingSlash(pathWithSlash);

		if (!pathWithSlash.empty() && pathWithSlash[pathWithSlash.length() - 1] != '\\')
			pathWithSlash += '\\';


    for (size_t i = 0; i < pathWithSlash.length(); i++)
        if (pathWithSlash[i] == '\\')
            pathWithSlash[i] = '/';

    // Путь до папки с модом для возможности загрузки eilib.lua
    std::stringstream ss;
    ss << "package.path = package.path .. \""
       << ";" << pathWithSlash << "?.lua"
       << ";" << pathWithSlash << "lua/?.lua"
       << "\"";

    luaL_dostring(STATE, ss.str().c_str());

    ReportError(DoFile(pathWithSlash + fileName));
}


void Lua::CLuaState::ReportOnError(int status) // Не выправил
{
  //  if (status && !lua_isnil(STATE, -1))
		//ErrorMsgBoxWide(1, AnsiToWide(lua_tostring(STATE, -1), 0).c_str());
}

static int Traceback(lua_State *state)
{
    if (!lua_isstring(state, 1))
    {
        if (lua_isnoneornil(state, 1) ||
            !luaL_callmeta(state, 1, "__tostring") ||
            !lua_isstring(state, -1))
            return 1;
        lua_remove(state, 1);
    }
    luaL_traceback(state, state, lua_tostring(state, 1), 1);
    return 1;
}

int Lua::CLuaState::DoCall(int args, int results)
{
    GameJournal::Callback journalCallback;
    GameJournal::Source journalSource("Lua.legacy");
    int base = lua_gettop(STATE) - args;
    lua_pushcfunction(STATE, Traceback);
    lua_insert(STATE, base);
    int status = lua_pcall(STATE, args, results, base);
    lua_remove(STATE, base);
    if (status != LUA_OK)
    {
        GameJournal::Event("ERROR", "lua", "text=%s", GameJournal::Quote(lua_tostring(STATE, -1)).c_str());
        lua_gc(STATE, LUA_GCCOLLECT, 0);
    }
    return status;
}

int Lua::CLuaState::DoFile(const std::string &path)
{
    return luaL_loadfile(STATE, path.c_str()) || DoCall(0, LUA_MULTRET);
}

// -----------------------------------------------------------------------------------------------------------
// lua_push / lua_pop wrappers
// -----------------------------------------------------------------------------------------------------------

template <class T>
void LuaPush(lua_State *state, T value)
{
#ifdef _DEBUG
    throw std::runtime_error("Invalid template param of LuaPush");
#endif
}

template <class T>
T LuaPop(lua_State *state)
{
#ifdef _DEBUG
    throw std::runtime_error("Invalid template param of LuaPop");
#endif
    return T();
}

#define ImplPushPop(type, luaType, func)              \
    template <>                                       \
    inline void LuaPush(lua_State *state, type value) \
    {                                                 \
        lua_push##func(state, (luaType)value);        \
    }                                                 \
    template <>                                       \
    inline type LuaPop(lua_State *state)              \
    {                                                 \
        auto result = (type)lua_to##func(state, -1);  \
        lua_pop(state, 1);                            \
        return result;                                \
    }

// clang-format off
ImplPushPop(float,        lua_Number,   number)
ImplPushPop(double,       lua_Number,   number)
ImplPushPop(uint8_t,      lua_Integer,  integer)
ImplPushPop(uint16_t,     lua_Integer,  integer)
ImplPushPop(uint32_t,     lua_Integer,  integer)
ImplPushPop(int8_t,       lua_Integer,  integer)
ImplPushPop(int16_t,      lua_Integer,  integer)
ImplPushPop(int32_t,      lua_Integer,  integer)
ImplPushPop(const char *, const char *, string)

#undef ImplPushPop

inline void LuaPush(lua_State *state, bool value)
{
    lua_pushboolean(state, value ? 1 : 0);
}

// clang-format on

inline void LuaPush(lua_State *state, const CString &value)
{
    lua_pushlstring(state, value.GetString(), value.GetLength());
}

inline void LuaPush(lua_State *state, const std::string &value)
{
    lua_pushlstring(state, value.c_str(), value.length());
}

inline void LuaPush(lua_State *state, lua_CFunction func)
{
    lua_pushcfunction(state, func);
}

template <>
inline bool LuaPop(lua_State *state)
{
    auto result = lua_toboolean(state, -1) ? true : false;
    lua_pop(state, 1);
    return result;
}

template <>
inline CString LuaPop(lua_State *state)
{
    CString result(lua_tostring(state, -1));
    lua_pop(state, 1);
    return result;
}

template <class K, class V>
void LuaPush(lua_State *state, K key, V value)
{
    LuaPush(state, key);
    LuaPush(state, value);
    lua_settable(state, -3);
}

inline void LuaPush(lua_State *state, std::function<void()> values)
{
    lua_newtable(state);
    values();
}

inline void LuaPush(lua_State *state, const std::string &key, std::function<void()> values)
{
    LuaPush<const std::string &>(state, key);
    lua_newtable(state);
    values();
    lua_settable(state, -3);
}

// Addr к названию функции добавлено умышленно, иначе баг с неправильным выбором функции
inline void LuaPushAddr(lua_State *state, const void *addr, bool strict = false)
{
// В дебаг режиме это укажет на попытку передать в Lua нулевой указатель
// и заодно позволяет увидеть нормальный stacktrace
#ifdef _DEBUG
    if (strict && addr == nullptr)
        throw std::runtime_error("Unexpected attempt to push nullptr in LuaState");
#endif
    LuaPush(state, (uint32_t)addr);
}

// -----------------------------------------------------------------------------------------------------------
// Pre-defined functions which will be passed in lua
// -----------------------------------------------------------------------------------------------------------

namespace {

// Working with pointers to base types from Lua

// clang-format off
#define ImplMemFunc(funcName, type, luaType)            \
int Lua ## funcName ## FromMem(lua_State *state)        \
{                                                       \
    auto ptr = (type *)luaL_checkinteger(state, 1);     \
    lua_push ## luaType(state, (type)*ptr);             \
    return 1;                                           \
}                                                       \
int Lua ## funcName ## ToMem(lua_State *state)          \
{                                                       \
    auto *ptr = (type *)luaL_checkinteger(state, 1);    \
    auto value = (type)luaL_check ## luaType(state, 2); \
    *ptr = value;                                       \
    return 0;                                           \
}

ImplMemFunc(Float,  float,    number)
ImplMemFunc(Double, double,   number)
ImplMemFunc(UInt32, uint32_t, integer)
ImplMemFunc(UInt16, uint16_t, integer)
ImplMemFunc(UInt8,  uint8_t,  integer)
ImplMemFunc(Int32,  int32_t,  integer)
ImplMemFunc(Int16,  int16_t,  integer)
ImplMemFunc(Int8,   int8_t,   integer)

#undef ImplMemFunc
    
int LuaStrFromMem(lua_State *state)
{
    auto str = (CString *)luaL_checkinteger(state, 1);
    lua_pushstring(state, str->GetString());
    return 1;
}

// clang-format on

//int LuaStrToMem(lua_State *state)
//{
//    auto ptr = (CString *)luaL_checkinteger(state, 1);
//    auto str = luaL_checkstring(state, 2);
//    ptr->Assign(str);
//    return 0;
//}

int LuaRound(lua_State *state)
{
    auto num = (double)luaL_checknumber(state, 1);
    LuaPush(state, round(num));
    return 1;
}

int LuaGetEffectValue(lua_State *state)
{
    auto *unit = (CUnit *)luaL_checkinteger(state, 1);
    auto spellId = (ERPGSpellID)luaL_checkinteger(state, 2);
    LuaPush(state, unit->GetEffectValue(spellId));
    return 1;
}

int LuaGetPerkModifierForServer(lua_State *state)
{
    auto stats = (SRPGUnitStats *)luaL_checkinteger(state, 1);
    auto perkId = (ERPGPerks)luaL_checkinteger(state, 2);
    LuaPush(state, stats->GetPerkModifier(perkId));
    return 1;
}

int LuaGetPerkModifierForClient(lua_State *state)
{
    auto stats = (SRPGUserUnitStats *)luaL_checkinteger(state, 1);
    auto perkId = (ERPGPerks)luaL_checkinteger(state, 2);
    LuaPush(state, stats->GetPerkModifier(perkId));
    return 1;
}

int LuaHeal(lua_State *state)
{
    auto *unitStats = (SRPGUnitStats *)luaL_checkinteger(state, 1);
    auto commonStats = (SRPGCommonStats *)luaL_checkinteger(state, 2);
    auto value = (float)luaL_checknumber(state, 3);
    unitStats->Heal(commonStats, value);
    return 0;
}

int LuaRegenMp(lua_State *state)
{
    auto commonStats = (SRPGCommonStats *)luaL_checkinteger(state, 1);
    auto value = (float)luaL_checknumber(state, 2);
    commonStats->RegenerateMP(value);
    return 0;
}

int LuaDistributeHp(lua_State *state)
{
    auto stats = (SRPGUnitStats *)luaL_checkinteger(state, 1);
    auto maxHp = (float)luaL_checknumber(state, 2);
    stats->DistributeHP(maxHp);
    return 0;
}
int LuaDistributeHpProportional(lua_State *state)
{
    auto stats = (SRPGUnitStats *)luaL_checkinteger(state, 1);
    auto maxHp = (float)luaL_checknumber(state, 2);
    stats->DistributeHPProportional(maxHp);
    return 0;
}
//int LuaFixHls(lua_State* state)
//{
//	auto stats = (SRPGUnitStats*)luaL_checkinteger(state, 1);
//	auto unit = (CUnit*)luaL_checkinteger(state, 2);
//	auto maxHp = (float)luaL_checknumber(state, 3);
//	stats->FixHls(unit, maxHp);
//	return 0;
//}

int LuaGetLeftHand(lua_State* state) {
	auto unit = (CUnit*)luaL_checkinteger(state, 1);
	if(unit && unit != (CUnit*)0x00000001)
		LuaPush(state, -1);
	//LuaPush(state, unit->GetLeftHand()); // Функционал жамевю
	else
		LuaPush(state, -1);
	return 1;
}
int LuaGetDifficulty(lua_State* state) {
	//auto unit = (CUnit*)luaL_checkinteger(state, 1);
	LuaPush(state, theSettings.nDifficultyLevel);
	return 1;
}
//CSoundManager* __stdcall GetFknSoundManager2()
//{ // рабочая тема для поиска гетов по Dwordчтотоам
//	CSoundManager** SoundManager = (CSoundManager * *)0x007AF784;
//	return *SoundManager;
//}
//int LuaPlaySound(lua_State* state) { //Контент жамевю
//	CUnit* unit = (CUnit*)luaL_checkinteger(state, 1);
//	auto name = (const char*)luaL_checklstring(state, 2, nullptr);
//	auto min = (int)luaL_checkinteger(state, 3);
//	auto max = (int)luaL_checkinteger(state, 4);
//	theWorld->pSoundManager->BeginFX3d(name, 0, unit->GetCP(), min, max, false, false);
//	LuaPush(state, theSettings.nDifficultyLevel);
//	return 1;
//}

//int LuaGetHlsPercent(lua_State* state)//Функция Жамевю
//{
////	auto stats = (SRPGUnitStats*)luaL_checkinteger(state, 1);
//	CUnit* unit = (CUnit*)luaL_checkinteger(state, 1);
//	int hit_loc = (int)luaL_checknumber(state, 2);
//	float result = 1;
//	for (int i = 0; i < RPG_NUM_HIT_LOCATIONS; i++) {
//		if ((&unit->GetUserUnitStats())->hls[i].hlt == hit_loc) {
//			if ((&unit->GetUserUnitStats())->hls[i].maxHp > 0) {
//				if ( float((&unit->GetUserUnitStats())->hls[i].hp) < 10000000)
//					result = min(result, max(0.0001f, float( (&unit->userUnitStats)->hls[i].hp+0.0001f) ) / float((&unit->userUnitStats)->hls[i].maxHp));
//
//			}
//		}
//	}
//	//if (result>0 && (hit_loc == RPGHLT_HAND || hit_loc == RPGHLT_LEG)) {
//	//	result = result / 2;
//	//}
//	//Console->WriteLineF("LuaGetHlsPercent: Unit name %s || result %f || hit_loc %i", unit->GetPrototype()->name, result, hit_loc);
//
//	LuaPush(state, result);
//	return 1;
//}
int LuaGetUnitItemsWeight(lua_State* state)
{
	//	auto stats = (SRPGUnitStats*)luaL_checkinteger(state, 1);
	auto unit = (CUnit*)luaL_checkinteger(state, 1);
	int numSpells = 0;
	for (int i = 0; i < QI_MAX_SPELLS; i++) {

		if (unit->GetQSpell(i))
			numSpells += 1;
	}
	float weight = unit->GetItemsWeight(); // Эта хуйня не считается только в обозе ибо клиент а в сервере сука считает, надеюсь инфоокно норм пересчитаю
	//CPersonage* pPers = unit->GetPersonage(ui(unit), 0);
	LuaPush(state, weight + numSpells);
	return 1;
}



int LuaCollectHp(lua_State *state)
{
    auto stats = (SRPGUnitStats *)luaL_checkinteger(state, 1);
    auto maxHp = (float)luaL_checknumber(state, 2);
    LuaPush(state, stats->CollectHP(maxHp));
    return 1;
}

int LuaGetPerkRecord(lua_State *state)
{
    auto index = (int)luaL_checkinteger(state, 1);
    LuaPushAddr(state, thePerkDatabase.GetPerk(index));
    return 1;
}
int LuaAddPerkRecord(lua_State* state)
{
	auto* unit = (CUnit*)luaL_checkinteger(state, 1);
	auto index = (int)luaL_checkinteger(state, 2);
	auto level = (int)luaL_checkinteger(state, 3);
	CUnitServer* unitServer = (CUnitServer*)unit->GetUnitSpecific();
	unitServer->GetRPGStats()->AddPerk(&thePerkDatabase, thePerkDatabase.GetPerk(index * 3 + level));
	return 1;
}
int LuaGetPerkIndex(lua_State *state)
{
    auto perk = (SPDBPerk *)luaL_checkinteger(state, 1);
    LuaPush(state, thePerkDatabase.GetPerkIndex(perk));
    return 1;
}

int LuaGetWeaponRecord(lua_State *state)
{
    auto index = (int)luaL_checkinteger(state, 1);
    LuaPushAddr(state, theItemDatabase.weapons[index]);
    return 1;
}

int LuaGetArmorRecord(lua_State *state)
{
    auto index = (int)luaL_checkinteger(state, 1);
    LuaPushAddr(state, theItemDatabase.armors[index]);
    return 1;
}

int LuaGetMaterialRecord(lua_State *state)
{
    auto index = (int)luaL_checkinteger(state, 1);
    LuaPushAddr(state, theItemDatabase.materials[index]);
    return 1;
}

int LuaGetQuickRecord(lua_State *state)
{
    auto index = (int)luaL_checkinteger(state, 1);
    LuaPushAddr(state, theItemDatabase.quickItems[index]);
    return 1;
}

int LuaGetClassId(lua_State *state)
{
    auto gameBase = (CGameBase *)luaL_checkinteger(state, 1);
    LuaPush(state, (uint32_t)gameBase->WhoAmI());
    return 1;
}

int LuaGetItemsWeight(lua_State *state)
{
    auto *unit = (CUnit *)luaL_checkinteger(state, 1);
	float weight = unit->GetItemsWeight();

	if (weight == 0){
		CUnit * servUnit = theWorldServer.GetUnit(unit->GetID());
		if(servUnit)
			weight = servUnit->GetItemsWeight();
	}
	//if(unit){
	//	CString name = unit->GetPrototype()->name;
	//	name = unit->GetPrototype()->name;
	//}
    LuaPush(state, weight);
    return 1;
}

int LuaGetItemShapeEntry(lua_State *state)
{
    auto item = (CItem *)luaL_checkinteger(state, 1);
    LuaPushAddr(state, item->GetShapeEntry());
    return 1;
}

int LuaGetWeapon(lua_State *state)
{
    auto *unit = (CUnit *)luaL_checkinteger(state, 1);
    auto index = luaL_checkinteger(state, 2);
    //LuaPushAddr(state, unit->GetWeapon(index));
    LuaPushAddr(state, unit->GetQWeapon(index));
    return 1;
}
static DWORD __stdcall GetWeap(DWORD WeapNum, CUnitClientSpecific *unitSpec)
{
    DWORD Weap;
    //Weap = 123456;//test
    //if (WeapNum == -1 || WeapNum > 4)
    //    return -1;
    __asm {
        push eax
        push ecx
        push esi
        push unitSpec
        push WeapNum
        mov esi, unitSpec
        mov     eax, [esi+1Ch]
        //mov     eax, [eax+26Ch]               //OldWeaponNum
        mov     eax, WeapNum //OldWeaponNum
        mov     ecx, [esi+eax*4+0BCh] //PrewWeapID to ecx
        //mov     ecx, [esi+WeapNum*4+0BCh] //PrewWeapID to ecx
        mov     Weap, ecx
        pop  eax
        pop  ecx
        pop  esi
        pop  unitSpec
        pop  WeapNum
    }
    //Console->WriteLineF("WeapNum: %d  || CItemWeapon weapons[]: %d || (CItemWeapon *)Weap: %d", WeapNum, (CItemWeapon *)(unitSpec->unit->weapons[WeapNum]), (CItemWeapon *)Weap);

    return Weap;
}
int LuaGetWeaponDW(lua_State *state)
{
    auto *unit = (CUnit *)luaL_checkinteger(state, 1);
    auto index = luaL_checkinteger(state, 2);
    LuaPush(state, GetWeap(index, (CUnitClientSpecific *)unit->GetUnitSpecific()) );
    //LuaPushAddr(state, unit->GetQWeapon(index));
    return 1;
}
int LuaGetArmor(lua_State *state)
{
    auto *unit = (CUnit *)luaL_checkinteger(state, 1);
    auto index = luaL_checkinteger(state, 2);
    LuaPushAddr(state, unit->GetQArmor(index));
    return 1;
}

int LuaGetQuick(lua_State *state)
{
    auto *unit = (CUnit *)luaL_checkinteger(state, 1);
    auto index = luaL_checkinteger(state, 2);
    LuaPushAddr(state, unit->GetQItem(index));
    return 1;
}

int LuaGetSpell(lua_State *state)
{
    auto *unit = (CUnit *)luaL_checkinteger(state, 1);
    auto index = luaL_checkinteger(state, 2);
    LuaPushAddr(state, unit->GetQSpell(index));
    return 1;
}

int LuaConsoleWriteLine(lua_State *state)
{
    //Console->WriteLine(luaL_checkstring(state, 1));
    return 0;
}

int LuaIDiv(lua_State *state)
{
    auto lhs = luaL_checkinteger(state, 1);
    auto rhs = luaL_checkinteger(state, 2);
    LuaPush(state, lhs / rhs);
    return 1;
}

int LuaIMod(lua_State *state)
{
    auto lhs = luaL_checkinteger(state, 1);
    auto rhs = luaL_checkinteger(state, 2);
    LuaPush(state, lhs % rhs);
    return 1;
}

//int LuaGetGSVar(lua_State *state)
//{
//    auto *unit = (CUnit *)luaL_checkinteger(state, 1);
//    auto varName = luaL_checklstring(state, 2, nullptr);
//
//    // GSVar'ы могут существовать только у управляемых игроком персонажей
//    if (!unit->IsHero())
//    {
//        lua_pushnil(state);
//        return 1;
//    }
//
//    auto *vars = &unit->GetOwner()->party.vars;
//    CGSVariableSetHashIter hashIter(vars);
//    for (auto it = hashIter.First(); it; it = hashIter.Next())
//    {
//        if (it->name == varName)
//        {
//            LuaPush(state, it->value);
//            return 1;
//        }
//    }
//
//    lua_pushnil(state);
//    return 1;
//}

int LuaSetGSVar(lua_State *state)
{
	CUnit *unit = (CUnit *)luaL_checkinteger(state, 1);
    auto varName = luaL_checklstring(state, 2, nullptr);
    auto value = (float)luaL_checknumber(state, 3);

    // GSVar'ы могут существовать только у управляемых игроком персонажей
#ifdef _DEBUG
    assert(unit->IsHeroByPlayer());
#else
    if (!unit->IsHero())
        return 0;
#endif

    unit->GetOwner()->party.vars.SetVar(varName, value);
    return 0;
}

int LuaUnitIsHost(lua_State *state)
{
	CUnit *unit = (CUnit *)luaL_checkinteger(state, 1);

    CPlayer *player = theWorldServer.GetPlayer(0);
    if (player)
    {
        CParty *party = &player->party;
        if (party)
        {
            for (int i = 0; i < party->people.GetSize(); i++)
            {
                if (unit->GetID() == party->people[i].nID)
                {
                    LuaPush(state, true);
                    return 1;
                }
            }
        }
    }

    LuaPush(state, false);
    return 1;
}

//int LuaTempDumpVars(lua_State *state)
//{
//	CUnit *unit = (CUnit *)luaL_checkinteger(state, 1);
//
//    // GSVar'ы могут существовать только у управляемых игроком персонажей
//#ifdef _DEBUG
//    assert(unit->IsHeroByPlayer());
//#else
//    if (!unit->IsHeroByPlayer())
//        return 0;
//#endif
//
//    CGSVariableSetHashIter hashIter(unit->GetOwner()->GetParty()->vars.GetVarsHash());
//    for (auto it = hashIter.First(); it; it = hashIter.Next())
//        Console->WriteLineF("Name: \"%s\" = %.2f", it->name.GetString(), it->value);
//
//    return 0;
//}

} // namespace

// -----------------------------------------------------------------------------------------------------------
// CLuaState
// -----------------------------------------------------------------------------------------------------------

Lua::CLuaState::CLuaState()
    : state(luaL_newstate())
{
    assert(state != nullptr);

    // Allowed libraries enables here...
    // Enable all of them
    luaL_openlibs(STATE);

    // Embedded functions
#define PushFunc(name)         \
    LuaPush(STATE, Lua##name); \
    lua_setglobal(STATE, #name)

    PushFunc(Round);
    PushFunc(GetEffectValue);
    PushFunc(GetPerkModifierForServer);
    PushFunc(GetPerkModifierForClient);
    PushFunc(Heal);
    PushFunc(RegenMp);
    PushFunc(DistributeHp);
    PushFunc(DistributeHpProportional);
    PushFunc(GetLeftHand);
    PushFunc(GetDifficulty);
    //PushFunc(PlaySound);
    //PushFunc(FixHls);
    //PushFunc(GetHlsPercent);
    PushFunc(GetUnitItemsWeight);
	PushFunc(CollectHp);
    PushFunc(GetPerkIndex);

	PushFunc(GetPerkRecord);
	PushFunc(AddPerkRecord);
	PushFunc(GetWeaponRecord);
    PushFunc(GetArmorRecord);
    PushFunc(GetMaterialRecord);
    PushFunc(GetQuickRecord);

    PushFunc(GetClassId);
    PushFunc(GetItemsWeight);
    PushFunc(GetItemShapeEntry);
    PushFunc(GetWeapon);
    PushFunc(GetArmor);
    PushFunc(GetQuick);
    PushFunc(GetSpell);
    PushFunc(ConsoleWriteLine);

    PushFunc(IDiv);
    PushFunc(IMod);

    //PushFunc(GetGSVar);
    PushFunc(SetGSVar);
    PushFunc(UnitIsHost);
    //PushFunc(TempDumpVars);

    PushFunc(StrFromMem);
    PushFunc(FloatFromMem);
    PushFunc(DoubleFromMem);
    PushFunc(UInt32FromMem);
    PushFunc(UInt16FromMem);
    PushFunc(UInt8FromMem);
    PushFunc(Int32FromMem);
    PushFunc(Int16FromMem);
    PushFunc(Int8FromMem);

    //PushFunc(StrToMem);
    PushFunc(FloatToMem);
    PushFunc(DoubleToMem);
    PushFunc(UInt32ToMem);
    PushFunc(UInt16ToMem);
    PushFunc(UInt8ToMem);
    PushFunc(Int32ToMem);
    PushFunc(Int16ToMem);
    PushFunc(Int8ToMem);
#undef PushFunc
}

int Lua::CLuaState::GetSkillPriceDiff(int from, int to)
{
    lua_getglobal(STATE, "GetSkillPriceDiff");
    GameJournal::SetCallback("GetSkillPriceDiff");
    LuaPush(STATE, from);
    LuaPush(STATE, to);
    ReportOnError(DoCall(2, 1));
    return LuaPop<int>(STATE);
}

float Lua::CLuaState::CalcBaseHealth(SRPGUnitStats *stats)
{
    lua_getglobal(STATE, "CalcBaseHealth");
    GameJournal::SetCallback("CalcBaseHealth");
    LuaPushAddr(STATE, stats);
    ReportOnError(DoCall(1, 1));
    return LuaPop<float>(STATE);
}
float Lua::CLuaState::CalcBaseHealthJmv(SRPGUnitStats *stats, CUnit* unit, const char* unitName)
{
    lua_getglobal(STATE, "CalcBaseHealthJmv");
    GameJournal::SetCallback("CalcBaseHealthJmv");
    LuaPushAddr(STATE, stats);
	LuaPushAddr(STATE, unit);
	LuaPush(STATE, unitName);
	ReportOnError(DoCall(3, 1));
    return LuaPop<float>(STATE);
}

float Lua::CLuaState::CalcBaseMana(SRPGUnitStats *stats)
{
    lua_getglobal(STATE, "CalcBaseMana");
    GameJournal::SetCallback("CalcBaseMana");
    LuaPushAddr(STATE, stats);
    ReportOnError(DoCall(1, 1));
    return LuaPop<float>(STATE);
}
float Lua::CLuaState::CalcBaseManaJmv(SRPGUnitStats *stats, CUnit* unit, const char* unitName)
{
    lua_getglobal(STATE, "CalcBaseManaJmv");
    GameJournal::SetCallback("CalcBaseManaJmv");
    LuaPushAddr(STATE, stats);
	LuaPushAddr(STATE, unit);
	LuaPush(STATE, unitName);
	ReportOnError(DoCall(3, 1));
	return LuaPop<float>(STATE);
}

void Lua::CLuaState::CalcRunningManaCost(SRPGUnitStats *unitStats, CUnit *unit, SRPGCommonStats *commonStats)
{
    lua_getglobal(STATE, "CalcRunningManaCost");
    GameJournal::SetCallback("CalcRunningManaCost");
    LuaPushAddr(STATE, unitStats);
    LuaPushAddr(STATE, unit);
    LuaPushAddr(STATE, commonStats);
    ReportOnError(DoCall(3, 0));
}

const char* Lua::CLuaState::GetPerkRequiredJmv(const char* perkName, int num)
{
	lua_getglobal(STATE, "GetPerkRequiredJmv");
    GameJournal::SetCallback("GetPerkRequiredJmv");
	LuaPush(STATE, perkName);
	LuaPush(STATE, num);
	ReportOnError(DoCall(2, 1));
	return LuaPop<const char*>(STATE);
}

int Lua::CLuaState::GetPerkPrice(SRPGUnitStats *stats, SPDBPerk *perk)
{
    lua_getglobal(STATE, "GetPerkPrice");
    GameJournal::SetCallback("GetPerkPrice");
    LuaPushAddr(STATE, stats);
    LuaPushAddr(STATE, perk);
    ReportOnError(DoCall(2, 1));
    return LuaPop<int>(STATE);
}

int Lua::CLuaState::GetPerkVal(SRPGUnitStats *stats, int index)
{
    lua_getglobal(STATE, "GetPerkVal");
    GameJournal::SetCallback("GetPerkVal");
    LuaPushAddr(STATE, stats);
    LuaPush(STATE, index);
    ReportOnError(DoCall(2, 1));
    return LuaPop<int>(STATE);
}

float Lua::CLuaState::GetModifiedHealth(SRPGUnitStats *stats, CUnit *unit, float health)
{
    lua_getglobal(STATE, "GetModifiedHealth");
    GameJournal::SetCallback("GetModifiedHealth");
    LuaPushAddr(STATE, stats);
    LuaPushAddr(STATE, unit);
    LuaPush(STATE, health);
    ReportOnError(DoCall(3, 1));
    return LuaPop<float>(STATE);
}

float Lua::CLuaState::GetModifiedDamageOnServer(SRPGUnitStats *stats,
                                                CUnit *unit,
                                                float damage,
												float *spellEffects)
{
    lua_getglobal(STATE, "GetModifiedDamageOnServer");
    GameJournal::SetCallback("GetModifiedDamageOnServer");
    LuaPushAddr(STATE, stats);
    LuaPushAddr(STATE, unit);
    LuaPush(STATE, damage);
	LuaPushAddr(STATE, spellEffects);
	ReportOnError(DoCall(4, 1));
    return LuaPop<float>(STATE);
}
float Lua::CLuaState::GetModifiedDamageOnServerJmv(SRPGUnitStats *stats,
                                                CUnit *unit,
                                                float damage,
                                                float rand,
												int leftHandNum,
												bool leftHand,
												const char* unitName, uint32_t weapid, int weapnum
)
{
    lua_getglobal(STATE, "GetModifiedDamageOnServerJmv");
    GameJournal::SetCallback("GetModifiedDamageOnServerJmv");
    LuaPushAddr(STATE, stats);
    LuaPushAddr(STATE, unit);
    LuaPush(STATE, damage);
    LuaPush(STATE, rand);
    LuaPush(STATE, leftHandNum);
    LuaPush(STATE, leftHand);
	LuaPush(STATE, unitName);
	LuaPush(STATE, weapid);
	LuaPush(STATE, weapnum);
	ReportOnError(DoCall(9, 1));
    return LuaPop<float>(STATE);
}

float Lua::CLuaState::GetCritOnServerJmv(SRPGUnitStats *stats,
                                                CUnit *unit,
												int leftHandNum,
												bool leftHand,
												const char* unitName, int TypeID, int weapNum)
{
    lua_getglobal(STATE, "GetCritOnServerJmv");
    GameJournal::SetCallback("GetCritOnServerJmv");
    LuaPushAddr(STATE, stats);
    LuaPushAddr(STATE, unit);
    LuaPush(STATE, leftHandNum);
    LuaPush(STATE, leftHand);
	LuaPush(STATE, unitName);
	LuaPush(STATE, TypeID);
	LuaPush(STATE, weapNum);
	ReportOnError(DoCall(7, 1));
    return LuaPop<float>(STATE);
}
float Lua::CLuaState::GetCritOnClientJmv(CUnit* unit,
	float critPerkMod,
	int leftHandNum,
	bool leftHand,
	const char* unitName, int weapNum, int TypeID)
{
	lua_getglobal(STATE, "GetCritOnClientJmv");
    GameJournal::SetCallback("GetCritOnClientJmv");
	LuaPushAddr(STATE, unit);
	LuaPush(STATE, critPerkMod);
	LuaPush(STATE, leftHandNum);
	LuaPush(STATE, leftHand);
	LuaPush(STATE, unitName);
	LuaPush(STATE, TypeID);
	LuaPush(STATE, weapNum);
	ReportOnError(DoCall(7, 1));
	return LuaPop<float>(STATE);
}
float Lua::CLuaState::GetCritDamageOnClientJmv(CUnit* unit,
	float critPerkMod,
	int leftHandNum,
	bool leftHand,
	const char* unitName, int weapNum, int TypeID)
{
	lua_getglobal(STATE, "GetCritDamageOnClientJmv");
    GameJournal::SetCallback("GetCritDamageOnClientJmv");
	LuaPushAddr(STATE, unit);
	LuaPush(STATE, critPerkMod);
	LuaPush(STATE, leftHandNum);
	LuaPush(STATE, leftHand);
	LuaPush(STATE, unitName);
	LuaPush(STATE, TypeID);
	LuaPush(STATE, weapNum);
	ReportOnError(DoCall(7, 1));
	return LuaPop<float>(STATE);
}
float Lua::CLuaState::GetModifiedDamageOnClient(SRPGUserUnitStats *stats,
                                                CUnit *unit,
                                                float damage, 
												float* spellEffects)
{
    lua_getglobal(STATE, "GetModifiedDamageOnClient");
    GameJournal::SetCallback("GetModifiedDamageOnClient");
    LuaPushAddr(STATE, stats);
    LuaPushAddr(STATE, unit);
    LuaPush(STATE, damage);
	LuaPushAddr(STATE, spellEffects);
	ReportOnError(DoCall(4, 1));
	return LuaPop<float>(STATE);
}
float Lua::CLuaState::GetModifiedDamageOnClientJmv(SRPGUserUnitStats *stats,
	CUnit *unit,
                                                float damage,
                                                float rand,
												const char* unitName, uint32_t weapid, int weapnum,
												float* spellEffects)
{
    lua_getglobal(STATE, "GetModifiedDamageOnClientJmv");
    GameJournal::SetCallback("GetModifiedDamageOnClientJmv");
    LuaPushAddr(STATE, stats);
    LuaPushAddr(STATE, unit);
    LuaPush(STATE, damage);
    LuaPush(STATE, rand);
	LuaPush(STATE, unitName);
	LuaPush(STATE, weapid);
	LuaPush(STATE, weapnum);
	LuaPushAddr(STATE, spellEffects);
	ReportOnError(DoCall(8, 1));
	return LuaPop<float>(STATE);
}

bool Lua::CLuaState::RegenerateHpAndMp(SRPGCommonStats *commonStats,
                                       SRPGUnitStats *unitStats,
                                       CUnit *unit,
                                       bool hpOnly)
{
    lua_getglobal(STATE, "RegenerateHpAndMp");
    GameJournal::SetCallback("RegenerateHpAndMp");
    LuaPushAddr(STATE, commonStats);
    LuaPushAddr(STATE, unitStats);
    LuaPushAddr(STATE, unit);
    LuaPush(STATE, hpOnly);
    ReportOnError(DoCall(4, 1));
    return LuaPop<bool>(STATE);
}
bool Lua::CLuaState::RegenerateHpAndMpJmv(SRPGCommonStats *commonStats,
                                       SRPGUnitStats *unitStats,
                                       CUnit *unit,
                                       bool hpOnly, 
									   bool inDanger)
{
    lua_getglobal(STATE, "RegenerateHpAndMpJmv");
    GameJournal::SetCallback("RegenerateHpAndMpJmv");
    LuaPushAddr(STATE, commonStats);
    LuaPushAddr(STATE, unitStats);
    LuaPushAddr(STATE, unit);
    LuaPush(STATE, hpOnly);
    LuaPush(STATE, inDanger);
    ReportOnError(DoCall(5, 1));
    return LuaPop<bool>(STATE);
}

void Lua::CLuaState::RecalcUnitStats(SRPGUnitStats *unitStats,
                                     SRPGCommonStats *commonStats,
                                     CUnit *unit,
                                     float *spellEffects)
{
    lua_getglobal(STATE, "RecalcUnitStats");
    GameJournal::SetCallback("RecalcUnitStats");
    LuaPushAddr(STATE, unitStats);
    LuaPushAddr(STATE, commonStats);
    LuaPushAddr(STATE, unit);
	LuaPushAddr(STATE, spellEffects);
	ReportOnError(DoCall(4, 0));
}
void Lua::CLuaState::RecalcUnitStatsJmv(SRPGUnitStats* unitStats,
	SRPGCommonStats* commonStats,
	CUnit* unit,
	float* spellEffects,
	int HumanStatsRecalc,
	int ArmorRecalc,
	const char* unitName,
	bool inDanger)
{
	lua_getglobal(STATE, "RecalcUnitStatsJmv");
    GameJournal::SetCallback("RecalcUnitStatsJmv");
	LuaPushAddr(STATE, unitStats);
	LuaPushAddr(STATE, commonStats);
	LuaPushAddr(STATE, unit);
	LuaPushAddr(STATE, spellEffects);
	LuaPush(STATE, HumanStatsRecalc);
	LuaPush(STATE, ArmorRecalc);
	LuaPush(STATE, unitName);
	LuaPush(STATE, inDanger);
	ReportOnError(DoCall(8, 0));
}

void Lua::CLuaState::RecalcInitialJmvStatsExt(SRPGUnitStats* unitStats,
	SRPGCommonStats* commonStats,
	CUnit* unit,
	const char* unitName)
{
	lua_getglobal(STATE, "RecalcInitialJmvStatsExt");
    GameJournal::SetCallback("RecalcInitialJmvStatsExt");
	LuaPushAddr(STATE, unitStats);
	LuaPushAddr(STATE, commonStats);
	LuaPushAddr(STATE, unit);
	LuaPush(STATE, unitName);
	ReportOnError(DoCall(4, 0));
}



float Lua::CLuaState::RecalcHeadDamageModifierLS(SRPGUnitStats *unitStats)
{
    lua_getglobal(STATE, "RecalcHeadDamageModifierLS");
    GameJournal::SetCallback("RecalcHeadDamageModifierLS");
    LuaPushAddr(STATE, unitStats);
	ReportOnError(DoCall(1, 1));
	return LuaPop<float>(STATE);
}

float Lua::CLuaState::GetAnatomyModifierLS(SRPGUnitStats* unitStats)
{
	lua_getglobal(STATE, "GetAnatomyModifierLS");
    GameJournal::SetCallback("GetAnatomyModifierLS");
	LuaPushAddr(STATE, unitStats);
	ReportOnError(DoCall(1, 1));
	return LuaPop<float>(STATE);
}

float Lua::CLuaState::GetWeaponsMasterLS(SRPGUnitStats* unitStats)
{
	lua_getglobal(STATE, "GetWeaponsMasterLS");
    GameJournal::SetCallback("GetWeaponsMasterLS");
	LuaPushAddr(STATE, unitStats);
	ReportOnError(DoCall(1, 1));
	return LuaPop<float>(STATE);
}

float Lua::CLuaState::GetWiseManDamageModiferLS(CUnit* unit, SRPGUnitStats* unitStats, int intelligence)
{
	lua_getglobal(STATE, "GetWiseManDamageModiferLS");
    GameJournal::SetCallback("GetWiseManDamageModiferLS");
	LuaPushAddr(STATE, unit);
	LuaPushAddr(STATE, unitStats);
	LuaPush(STATE, intelligence);
	ReportOnError(DoCall(3, 1));
	return LuaPop<float>(STATE);
}

void Lua::CLuaState::RecalcInternalHeroStats(SRPGUnitStats *stats, float itemsWeigh)
{
    lua_getglobal(STATE, "RecalcInternalHeroStats");
    GameJournal::SetCallback("RecalcInternalHeroStats");
    LuaPushAddr(STATE, stats);
    LuaPush(STATE, itemsWeigh);
    ReportOnError(DoCall(2, 0));
}

float Lua::CLuaState::CheckShieldBlock(SRPGUserUnitStats*unitStats,
                                              CUnit *unit)
{
    lua_getglobal(STATE, "CheckShieldBlock");
    GameJournal::SetCallback("CheckShieldBlock");
    LuaPushAddr(STATE, unitStats);
    LuaPushAddr(STATE, unit);
    ReportOnError(DoCall(2, 1));
    return LuaPop<float>(STATE);
}

float Lua::CLuaState::GetShieldBlockChance(SRPGUserUnitStats*unitStats,
                                              CUnit *unit)
{
    lua_getglobal(STATE, "GetShieldBlockChance");
    GameJournal::SetCallback("GetShieldBlockChance");
    LuaPushAddr(STATE, unitStats);
    LuaPushAddr(STATE, unit);
    ReportOnError(DoCall(2, 1));
    return LuaPop<float>(STATE);
}

int Lua::CLuaState::GetDualAnims(const char* animName, int prop)
{
	lua_getglobal(STATE, "GetDualAnims");
    GameJournal::SetCallback("GetDualAnims");
	LuaPush(STATE, animName);
	LuaPush(STATE, prop);
	ReportOnError(DoCall(2, 1));
	return LuaPop<int>(STATE);
}

float Lua::CLuaState::GetDamageModAttr(int weapon_type, int strength, int dexterity, int intelligence, float mod)
{
	lua_getglobal(STATE, "GetDamageModAttr");
    GameJournal::SetCallback("GetDamageModAttr");
	LuaPush(STATE, weapon_type);
	LuaPush(STATE, strength);
	LuaPush(STATE, dexterity);
	LuaPush(STATE, intelligence);
	LuaPush(STATE, mod);
	ReportOnError(DoCall(5, 1));
	return LuaPop<float>(STATE);
}

float Lua::CLuaState::CheckWeaponParry(SRPGUserUnitStats* unitStats,
	CUnit* unit, int num)
{
	lua_getglobal(STATE, "CheckWeaponParry");
    GameJournal::SetCallback("CheckWeaponParry");
	LuaPushAddr(STATE, unitStats);
	LuaPushAddr(STATE, unit);
	LuaPush(STATE, num);
	ReportOnError(DoCall(3, 1));
	return LuaPop<float>(STATE);
}
float Lua::CLuaState::GetWeaponParryChance(SRPGUserUnitStats* unitStats,
	CUnit* unit, int num)
{
	lua_getglobal(STATE, "GetWeaponParryChance");
    GameJournal::SetCallback("GetWeaponParryChance");
	LuaPushAddr(STATE, unitStats);
	LuaPushAddr(STATE, unit);
	LuaPush(STATE, num);
	ReportOnError(DoCall(3, 1));
	return LuaPop<float>(STATE);
}
float Lua::CLuaState::CheckShieldParry(SRPGUserUnitStats* unitStats,
	CUnit* unit)
{
	lua_getglobal(STATE, "CheckShieldParry");
    GameJournal::SetCallback("CheckShieldParry");
	LuaPushAddr(STATE, unitStats);
	LuaPushAddr(STATE, unit);
	ReportOnError(DoCall(2, 1));
	return LuaPop<float>(STATE);
}
float Lua::CLuaState::GetShieldParryChance(SRPGUserUnitStats* unitStats,
	CUnit* unit)
{
	lua_getglobal(STATE, "GetShieldParryChance");
    GameJournal::SetCallback("GetShieldParryChance");
	LuaPushAddr(STATE, unitStats);
	LuaPushAddr(STATE, unit);
	ReportOnError(DoCall(2, 1));
	return LuaPop<float>(STATE);
}
float Lua::CLuaState::GetAttackModiferFromHL(int hit_location)
{
	lua_getglobal(STATE, "GetAttackModiferFromHL");
    GameJournal::SetCallback("GetAttackModiferFromHL");
	LuaPush(STATE, hit_location);
	ReportOnError(DoCall(1, 1));
	return LuaPop<float>(STATE);
}

float Lua::CLuaState::GetArmorPropsInside(const char* itemName, int prop)
{
	lua_getglobal(STATE, "GetArmorPropsInside");
    GameJournal::SetCallback("GetArmorPropsInside");
	LuaPush(STATE, itemName);
	LuaPush(STATE, prop);
	ReportOnError(DoCall(2, 1));
	return LuaPop<float>(STATE);
}
float Lua::CLuaState::GetWeaponPropsInside(const char* itemName, int prop)
{
	lua_getglobal(STATE, "GetWeaponPropsInside");
    GameJournal::SetCallback("GetWeaponPropsInside");
	LuaPush(STATE, itemName);
	LuaPush(STATE, prop);
	ReportOnError(DoCall(2, 1));
	return LuaPop<float>(STATE);
}
float Lua::CLuaState::GetMaterialPropsInside(const char* itemName, int prop)
{
	lua_getglobal(STATE, "GetMaterialPropsInside");
    GameJournal::SetCallback("GetMaterialPropsInside");
	LuaPush(STATE, itemName);
	LuaPush(STATE, prop);
	ReportOnError(DoCall(2, 1));
	return LuaPop<float>(STATE);
}
int Lua::CLuaState::GetRegenBonusFromPerkMagicBlood(int level)
{
	lua_getglobal(STATE, "GetRegenBonusFromPerkMagicBlood");
    GameJournal::SetCallback("GetRegenBonusFromPerkMagicBlood");
	LuaPush(STATE, level);
	ReportOnError(DoCall(1, 1));
	return LuaPop<int>(STATE);
}
float Lua::CLuaState::GetComplexSummStats(CUnit *unit, int stat)
{
	lua_getglobal(STATE, "GetComplexSummStats");
    GameJournal::SetCallback("GetComplexSummStats");
	LuaPushAddr(STATE, unit);
	LuaPush(STATE, stat);
	ReportOnError(DoCall(2, 1));
	return LuaPop<float>(STATE);
}float Lua::CLuaState::GetJewelryPropsInside(const char* itemName, int prop)
{
	lua_getglobal(STATE, "GetJewelryPropsInside");
    GameJournal::SetCallback("GetJewelryPropsInside");
	LuaPush(STATE, itemName);
	LuaPush(STATE, prop);
	ReportOnError(DoCall(2, 1));
	return LuaPop<float>(STATE);
}float Lua::CLuaState::GetUnitPerksInside(const char* unitName, int prop)
{
	lua_getglobal(STATE, "GetUnitPerksInside");
    GameJournal::SetCallback("GetUnitPerksInside");
	LuaPush(STATE, unitName);
	LuaPush(STATE, prop);
	ReportOnError(DoCall(2, 1));
	return LuaPop<float>(STATE);
}
float Lua::CLuaState::GetShopConstructLimit(int shop, int prop)
{
	lua_getglobal(STATE, "GetShopConstructLimit");
    GameJournal::SetCallback("GetShopConstructLimit");
	LuaPush(STATE, shop);
	LuaPush(STATE, prop);
	ReportOnError(DoCall(2, 1));
	return LuaPop<float>(STATE);
}
float Lua::CLuaState::GetSpellAdds(const char* name, int prop)
{
	lua_getglobal(STATE, "GetSpellAdds");
    GameJournal::SetCallback("GetSpellAdds");
	LuaPush(STATE, name);
	LuaPush(STATE, prop);
	ReportOnError(DoCall(2, 1));
	return LuaPop<float>(STATE);
}
bool Lua::CLuaState::IsForbiddenItem(const char* itemName, int prop, int shop)
{
	lua_getglobal(STATE, "IsForbiddenItem");
    GameJournal::SetCallback("IsForbiddenItem");
	LuaPush(STATE, itemName);
	LuaPush(STATE, prop);
	LuaPush(STATE, shop);
	ReportOnError(DoCall(3, 1));
	return LuaPop<float>(STATE);
}
float Lua::CLuaState::GetUnitStatsInside(const char* statName, int prop)
{
	lua_getglobal(STATE, "GetUnitStatsInside");
    GameJournal::SetCallback("GetUnitStatsInside");
	LuaPush(STATE, statName);
	LuaPush(STATE, prop);
	ReportOnError(DoCall(2, 1));
	return LuaPop<float>(STATE);
}

void Lua::CLuaState::CalcAttackParamsOnClient(SRPGUserUnitStats *unitStats,
                                              CUnit *unit,
                                              SAttackParams *attackParams,
                                              float *spellEffects, int leftHandNum)
{
    lua_getglobal(STATE, "CalcAttackParamsOnClient");
    GameJournal::SetCallback("CalcAttackParamsOnClient");
    LuaPushAddr(STATE, unitStats);
    LuaPushAddr(STATE, unit);
    LuaPushAddr(STATE, attackParams);
    LuaPushAddr(STATE, spellEffects);
    LuaPush(STATE, leftHandNum);
    ReportOnError(DoCall(5, 0));
}



void Lua::CLuaState::CalcAttackParamsOnServer(SRPGUnitStats *unitStats,
                                              CUnit *unit,
                                              SAttackParams *attackParams,
                                              float *spellEffects)
{
    lua_getglobal(STATE, "CalcAttackParamsOnServer");
    GameJournal::SetCallback("CalcAttackParamsOnServer");
    LuaPushAddr(STATE, unitStats);
    LuaPushAddr(STATE, unit);
    LuaPushAddr(STATE, attackParams);
    LuaPushAddr(STATE, spellEffects);
    ReportOnError(DoCall(4, 0));
}

void Lua::CLuaState::ApplySpellEffectsOnServer(SRPGUnitStats *unitStats,
											  SRPGCommonStats *commonStats,
                                              float *spellEffects)
{
    lua_getglobal(STATE, "ApplySpellEffectsOnServer");
    GameJournal::SetCallback("ApplySpellEffectsOnServer");
    LuaPushAddr(STATE, unitStats);
    LuaPushAddr(STATE, commonStats);
    LuaPushAddr(STATE, spellEffects);
    ReportOnError(DoCall(3, 0));
}



float Lua::CLuaState::GetHitLocDecr(SRPGUnitStats* unitStats, CUnit* unit, bool server)
{
	lua_getglobal(STATE, "GetHitLocDecr");
    GameJournal::SetCallback("GetHitLocDecr");
	LuaPushAddr(STATE, unitStats);
	LuaPushAddr(STATE, unit);
	LuaPush(STATE, server);
	ReportOnError(DoCall(3, 1));
	return LuaPop<float>(STATE);
}
float Lua::CLuaState::GetHitLocDecrDamage(SRPGUnitStats* unitStats, CUnit* unit, bool server)
{
	lua_getglobal(STATE, "GetHitLocDecrDamage");
    GameJournal::SetCallback("GetHitLocDecrDamage");
	LuaPushAddr(STATE, unitStats);
	LuaPushAddr(STATE, unit);
	LuaPush(STATE, server);
	ReportOnError(DoCall(3, 1));
	return LuaPop<float>(STATE);
}


float Lua::CLuaState::GetOverloadDefenceOnServer(float currEncumbrance, float maxEncumbrance)
{
	lua_getglobal(STATE, "GetOverloadDefenceOnServer");
    GameJournal::SetCallback("GetOverloadDefenceOnServer");
	LuaPush(STATE, currEncumbrance);
	LuaPush(STATE, maxEncumbrance);
	ReportOnError(DoCall(2, 1));
	return LuaPop<float>(STATE);
}
float Lua::CLuaState::GetOverloadAttackOnServer(float currEncumbrance, float maxEncumbrance)
{
	lua_getglobal(STATE, "GetOverloadAttackOnServer");
    GameJournal::SetCallback("GetOverloadAttackOnServer");
	LuaPush(STATE, currEncumbrance);
	LuaPush(STATE, maxEncumbrance);
	ReportOnError(DoCall(2, 1));
	return LuaPop<float>(STATE);
}
float Lua::CLuaState::OverloadManacostModsOnServer(float currEncumbrance, float maxEncumbrance)
{
	lua_getglobal(STATE, "OverloadManacostModsOnServer");
    GameJournal::SetCallback("OverloadManacostModsOnServer");
	LuaPush(STATE, currEncumbrance);
	LuaPush(STATE, maxEncumbrance);
	ReportOnError(DoCall(2, 1));
	return LuaPop<float>(STATE);
}
float Lua::CLuaState::GetOverloadDamageOnServer(float currEncumbrance, float maxEncumbrance)
{
	lua_getglobal(STATE, "GetOverloadDamageOnServer");
    GameJournal::SetCallback("GetOverloadDamageOnServer");
	LuaPush(STATE, currEncumbrance);
	LuaPush(STATE, maxEncumbrance);
	ReportOnError(DoCall(2, 1));
	return LuaPop<float>(STATE);
}
float Lua::CLuaState::GetOverloadBlockOnServer(float currEncumbrance, float maxEncumbrance)
{
	lua_getglobal(STATE, "GetOverloadBlockOnServer");
    GameJournal::SetCallback("GetOverloadBlockOnServer");
	LuaPush(STATE, currEncumbrance);
	LuaPush(STATE, maxEncumbrance);
	ReportOnError(DoCall(2, 1));
	return LuaPop<float>(STATE);
}
float Lua::CLuaState::GetOverloadParryOnServer(float currEncumbrance, float maxEncumbrance)
{
	lua_getglobal(STATE, "GetOverloadParryOnServer");
    GameJournal::SetCallback("GetOverloadParryOnServer");
	LuaPush(STATE, currEncumbrance);
	LuaPush(STATE, maxEncumbrance);
	ReportOnError(DoCall(2, 1));
	return LuaPop<float>(STATE);
}

float Lua::CLuaState::GetOverloadPercentOnServer(SRPGUnitStats* unitStats)
{
	lua_getglobal(STATE, "GetOverloadPercentOnServer");
    GameJournal::SetCallback("GetOverloadPercentOnServer");
	LuaPushAddr(STATE, unitStats);
	ReportOnError(DoCall(1, 1));
	return LuaPop<float>(STATE);
}

float Lua::CLuaState::GetOverloadPercentCurOnServer(SRPGUnitStats* unitStats)
{
	lua_getglobal(STATE, "GetOverloadPercentCurOnServer");
    GameJournal::SetCallback("GetOverloadPercentCurOnServer");
	LuaPushAddr(STATE, unitStats);
	ReportOnError(DoCall(1, 1));
	return LuaPop<float>(STATE);
}


float Lua::CLuaState::GetManaBonusMultiplier(void *stats, bool isServer)
{
    if (isServer)
        lua_getglobal(STATE, "GetManaBonusOnServer");
    else
        lua_getglobal(STATE, "GetManaBonusOnClient");
    GameJournal::SetCallback(isServer?"GetManaBonusOnServer":"GetManaBonusOnClient");
    LuaPushAddr(STATE, stats);
    ReportOnError(DoCall(1, 1));
    return LuaPop<float>(STATE);
}

float Lua::CLuaState::GetExpMultiplier(void *stats, bool isServer)
{
    if (isServer)
        lua_getglobal(STATE, "GetExpMultiplierOnServer");
    else
        lua_getglobal(STATE, "GetExpMultiplierOnClient");
    GameJournal::SetCallback(isServer?"GetExpMultiplierOnServer":"GetExpMultiplierOnClient");
    LuaPushAddr(STATE, stats);
    ReportOnError(DoCall(1, 1));
    return LuaPop<float>(STATE);
}

float Lua::CLuaState::GetExpMultiplier2(float intelligence, bool isServer)
{
	lua_getglobal(STATE, "GetExpMultiplier2");
    GameJournal::SetCallback("GetExpMultiplier2");
    LuaPush(STATE, intelligence);
    ReportOnError(DoCall(1, 1));
    return LuaPop<float>(STATE);
}

void Lua::CLuaState::RecalcShopsPriceMultipliers(std::vector<float> *mults, bool isLmp)
{
    lua_getglobal(STATE, "RecalcShopsPriceMultipliers");
    GameJournal::SetCallback("RecalcShopsPriceMultipliers");
    LuaPushAddr(STATE, mults->data());
    LuaPush(STATE, isLmp);
    ReportOnError(DoCall(2, 0));
}

float Lua::CLuaState::DistributeLmpExp(float exp, int playersNum)
{
    lua_getglobal(STATE, "DistributeLmpExp");
    GameJournal::SetCallback("DistributeLmpExp");
    LuaPush(STATE, exp);
    LuaPush(STATE, playersNum);
    ReportOnError(DoCall(2, 1));
    return LuaPop<float>(STATE);
}
