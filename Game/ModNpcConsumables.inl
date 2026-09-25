#include "ModNpcConsumables.h"
#include "WorldServer.h"
#include "WorldClient.h"
#include "UnitServer.h"
#include "UnitDatabase.h"
#include "SpellManager.h"
#include "Player.h"
#include "ModPackage.h"
#include "ModRuntime.h"
#include <unordered_map>
#include <cctype>
#define WorldServer (&theWorldServer)
#define WorldClient (&theWorld)
#define JMV_NPC_COMBAT_CONSUMABLES_DIAGNOSTICS 0
namespace {

enum ENpcCombatConsumableKind
{
	NPC_CONSUMABLE_REGENERATION = 0,
	NPC_CONSUMABLE_HEALING,
	NPC_CONSUMABLE_STRENGTH,
	NPC_CONSUMABLE_SPEED,
	NPC_CONSUMABLE_ENLARGE,
	NPC_CONSUMABLE_COUNT
};

enum ENpcCombatConsumablePhase
{
	NPC_CONSUMABLE_READY = 0,
	NPC_CONSUMABLE_WAIT_EFFECT,
	NPC_CONSUMABLE_WAIT_END
};

enum ENpcCombatConsumableGate
{
	NPC_CONSUMABLE_GATE_READY = 0,
	NPC_CONSUMABLE_GATE_NO_CHARGES,
	NPC_CONSUMABLE_GATE_WAIT_EFFECT,
	NPC_CONSUMABLE_GATE_WAIT_END,
	NPC_CONSUMABLE_GATE_HP_ABOVE_75,
	NPC_CONSUMABLE_GATE_HP_ABOVE_40,
	NPC_CONSUMABLE_GATE_NOT_DAMAGED,
	NPC_CONSUMABLE_GATE_HEALING_COOLDOWN,
	NPC_CONSUMABLE_GATE_NOT_ATTACKING,
	NPC_CONSUMABLE_GATE_EFFECT_ACTIVE
};

const int NPC_CONSUMABLE_EFFECT_IDS[NPC_CONSUMABLE_COUNT] = {
	31, -1, 29, 33, 37
};
const int NPC_CONSUMABLE_POLL_SEGMENTS = 4;
const int NPC_CONSUMABLE_ZONE_CHECK_SEGMENTS = 64;
const int NPC_CONSUMABLE_HEALING_COOLDOWN_SEGMENTS = 109;
const int NPC_CONSUMABLE_MAX_VISIT_TOKEN = 511;
const int NPC_CONSUMABLE_STATE_TOKEN_SHIFT = 15;
const int NPC_CONSUMABLE_SELECTION_AUTO = 3;

struct SNpcCombatConsumableSlot
{
	std::string formula;
	int maxCharges;
	int remainingCharges;
	int selection;
	int phase;
#if JMV_NPC_COMBAT_CONSUMABLES_DIAGNOSTICS
	int diagnosticGate;
#endif

	SNpcCombatConsumableSlot()
		: maxCharges(0), remainingCharges(0), selection(0), phase(0)
	{
#if JMV_NPC_COMBAT_CONSUMABLES_DIAGNOSTICS
		diagnosticGate = -1;
#endif
	}
};

struct SNpcCombatConsumableState
{
	CUnit *unit;
	CUnitServer *server;
	int visitToken;
	int nextPollTick;
	int nextHealingTick;
	bool initialized;
	SNpcCombatConsumableSlot slots[NPC_CONSUMABLE_COUNT];

	SNpcCombatConsumableState()
		: unit(nullptr), server(nullptr), visitToken(0), nextPollTick(0),
		  nextHealingTick(0), initialized(false)
	{
	}
};

static std::unordered_map<int, SNpcCombatConsumableState>
	G_npcCombatConsumableStates;
static std::string G_npcCombatConsumableZone;
static int G_npcCombatConsumableVisitToken = 0;
static bool G_npcCombatConsumableFreshVisit = false;
static CGSVariableSet *G_npcCombatConsumableVars = nullptr;
static int G_npcCombatConsumableNextZoneCheckTick = 0;
static int G_npcCombatConsumableLastObservedTick = 0;
static std::string NPC_NormalizeMobName(const char *rawName)
{
	if (!rawName || !rawName[0])
		return std::string();

	std::string name(rawName);
	const size_t separator = name.find_last_of("\\/");
	if (separator != std::string::npos)
		name.erase(0, separator + 1);

	const size_t extension = name.find_last_of('.');
	if (extension != std::string::npos)
		name.erase(extension);

	for (size_t i = 0; i < name.size(); ++i)
	{
		unsigned char ch = static_cast<unsigned char>(name[i]);
		ch = static_cast<unsigned char>(std::tolower(ch));
		name[i] = (std::isalnum(ch) || ch == '_')
			? static_cast<char>(ch)
			: '_';
	}
	return name;
}

static std::string NPC_CurrentMobName()
{
	if (!WorldServer)
		return std::string();
	CString *mobName = &theWorldServer.szMobFileName;
	return mobName
		? NPC_NormalizeMobName((LPCSTR)*mobName)
		: std::string();
}

static std::string NPC_VisitKey(const std::string &zone)
{
	return std::string("jmv.npccons.visit.") + zone;
}

static std::string NPC_UnitKey(const char *field,
							   const std::string &zone,
							   int unitId)
{
	return std::string("jmv.npccons.") + field + "." + zone + "." +
		std::to_string(static_cast<long long>(unitId));
}

static uint32_t NPC_ReadPacked(CGSVariableSet &vars, const std::string &key)
{
	const float value = vars.GetVar(key.c_str());
	if (!std::isfinite(value) || value <= 0.0f || value > 16777215.0f)
		return 0;
	return static_cast<uint32_t>(static_cast<double>(value) + 0.5);
}

static const char *NPC_PrototypeName(CUnit *unit)
{
	const SUDBUnitPrototype *prototype = unit ? unit->GetPrototype() : nullptr;
	const char *name = prototype ? (LPCSTR)prototype->szName : nullptr;
	return name ? name : "";
}

static const char *NPC_KindName(int kind)
{
	switch (kind)
	{
	case NPC_CONSUMABLE_REGENERATION: return "regeneration";
	case NPC_CONSUMABLE_HEALING: return "healing";
	case NPC_CONSUMABLE_STRENGTH: return "strength";
	case NPC_CONSUMABLE_SPEED: return "speed";
	case NPC_CONSUMABLE_ENLARGE: return "enlarge";
	default: return "none";
	}
}

static const char *NPC_PhaseName(int phase)
{
	switch (phase)
	{
	case NPC_CONSUMABLE_READY: return "ready";
	case NPC_CONSUMABLE_WAIT_EFFECT: return "wait_effect";
	case NPC_CONSUMABLE_WAIT_END: return "wait_end";
	default: return "none";
	}
}

static const char *NPC_ConditionDetail(int kind)
{
	switch (kind)
	{
	case NPC_CONSUMABLE_REGENERATION: return "condition=hp_below_75";
	case NPC_CONSUMABLE_HEALING: return "condition=hp_below_40_and_cooldown_ready";
	case NPC_CONSUMABLE_STRENGTH: return "condition=attack_state";
	case NPC_CONSUMABLE_SPEED: return "condition=attack_state";
	case NPC_CONSUMABLE_ENLARGE: return "condition=damaged";
	default: return "condition=unknown";
	}
}

static const char *NPC_GateName(int gate)
{
	switch (gate)
	{
	case NPC_CONSUMABLE_GATE_READY: return "gate=ready";
	case NPC_CONSUMABLE_GATE_NO_CHARGES: return "gate=no_charges";
	case NPC_CONSUMABLE_GATE_WAIT_EFFECT: return "gate=wait_effect";
	case NPC_CONSUMABLE_GATE_WAIT_END: return "gate=wait_end";
	case NPC_CONSUMABLE_GATE_HP_ABOVE_75: return "gate=hp_not_below_75";
	case NPC_CONSUMABLE_GATE_HP_ABOVE_40: return "gate=hp_not_below_40";
	case NPC_CONSUMABLE_GATE_NOT_DAMAGED: return "gate=not_damaged";
	case NPC_CONSUMABLE_GATE_HEALING_COOLDOWN: return "gate=healing_cooldown";
	case NPC_CONSUMABLE_GATE_NOT_ATTACKING: return "gate=not_attacking";
	case NPC_CONSUMABLE_GATE_EFFECT_ACTIVE: return "gate=effect_active";
	default: return "gate=unknown";
	}
}

static void NPC_Log(const char *eventName,
					CUnit *unit,
					CUnitServer *server,
					const SNpcCombatConsumableState *state,
					int kind,
					int tick,
					float hp,
					float maxHp,
					int valueBefore,
					int valueAfter,
					const char *detail)
{
#if JMV_NPC_COMBAT_CONSUMABLES_DIAGNOSTICS
	const SNpcCombatConsumableSlot *slot =
		state && kind >= 0 && kind < NPC_CONSUMABLE_COUNT
		? &state->slots[kind]
		: nullptr;
	const float hpPercent = maxHp > 0.0f ? hp * 100.0f / maxHp : -1.0f;
	g_Log.Info(
		"NpcCombatConsumable",
		"feature=\"NpcCombatConsumables\" event=%s zone=%s visit=%d tick=%d id=%d prototype=\"%s\" kind=%s selection=%d phase=%s charges=%d/%d hp=%.3f/%.3f hp_pct=%.2f logic_state=%d next_heal=%d before=%d after=%d formula=\"%s\" detail=%s",
		eventName ? eventName : "UNKNOWN",
		G_npcCombatConsumableZone.c_str(),
		state ? state->visitToken : G_npcCombatConsumableVisitToken,
		tick,
		unit ? unit->GetID() : -1,
		NPC_PrototypeName(unit),
		NPC_KindName(kind),
		slot ? slot->selection : 0,
		slot ? NPC_PhaseName(slot->phase) : "none",
		slot ? slot->remainingCharges : -1,
		slot ? slot->maxCharges : -1,
		hp,
		maxHp,
		hpPercent,
		server ? static_cast<int>(server->logic.GetScriptState()) : -1,
		state ? state->nextHealingTick : 0,
		valueBefore,
		valueAfter,
		slot ? slot->formula.c_str() : "",
		detail ? detail : "none");
#else
	(void)eventName;
	(void)unit;
	(void)server;
	(void)state;
	(void)kind;
	(void)tick;
	(void)hp;
	(void)maxHp;
	(void)valueBefore;
	(void)valueAfter;
	(void)detail;
#endif
}

static void NPC_LogGate(CUnit *unit,
						CUnitServer *server,
						SNpcCombatConsumableState *state,
						int kind,
						int gate,
						int tick,
						float hp,
						float maxHp)
{
#if JMV_NPC_COMBAT_CONSUMABLES_DIAGNOSTICS
	SNpcCombatConsumableSlot &slot = state->slots[kind];
	if (slot.diagnosticGate == gate)
		return;
	const int previousGate = slot.diagnosticGate;
	slot.diagnosticGate = gate;
	NPC_Log("GATE", unit, server, state, kind, tick, hp, maxHp,
			previousGate, gate, NPC_GateName(gate));
#else
	(void)unit;
	(void)server;
	(void)state;
	(void)kind;
	(void)gate;
	(void)tick;
	(void)hp;
	(void)maxHp;
#endif
}

static int NPC_ParseKind(const char *kind)
{
	if (!kind)
		return -1;
	if (_stricmp(kind, "regeneration") == 0)
		return NPC_CONSUMABLE_REGENERATION;
	if (_stricmp(kind, "healing") == 0)
		return NPC_CONSUMABLE_HEALING;
	if (_stricmp(kind, "strength") == 0)
		return NPC_CONSUMABLE_STRENGTH;
	if (_stricmp(kind, "speed") == 0)
		return NPC_CONSUMABLE_SPEED;
	if (_stricmp(kind, "enlarge") == 0)
		return NPC_CONSUMABLE_ENLARGE;
	return -1;
}

static void NPC_ResolveVisit(CGSVariableSet &vars, int tick)
{
	if (G_npcCombatConsumableFreshVisit)
	{
		const std::string visitKey =
			NPC_VisitKey(G_npcCombatConsumableZone);
		int previous = static_cast<int>(NPC_ReadPacked(vars, visitKey));
		if (previous < 0 || previous > NPC_CONSUMABLE_MAX_VISIT_TOKEN)
			previous = 0;
		G_npcCombatConsumableVisitToken =
			previous % NPC_CONSUMABLE_MAX_VISIT_TOKEN + 1;
		vars.SetVar(visitKey.c_str(),
					static_cast<float>(G_npcCombatConsumableVisitToken));
		G_npcCombatConsumableFreshVisit = false;
		NPC_Log("VISIT_RESTOCK", nullptr, nullptr, nullptr, -1,
			tick, -1.0f, -1.0f, previous,
			G_npcCombatConsumableVisitToken,
			"counter=visit_token");
	}
	else if (G_npcCombatConsumableVisitToken == 0)
	{
		const std::string visitKey =
			NPC_VisitKey(G_npcCombatConsumableZone);
		const int storedVisitToken =
			static_cast<int>(NPC_ReadPacked(vars, visitKey));
		G_npcCombatConsumableVisitToken = storedVisitToken;
		if (G_npcCombatConsumableVisitToken < 1 ||
			G_npcCombatConsumableVisitToken >
				NPC_CONSUMABLE_MAX_VISIT_TOKEN)
		{
			G_npcCombatConsumableVisitToken = 1;
			vars.SetVar(visitKey.c_str(), 1.0f);
		}
		NPC_Log(storedVisitToken == G_npcCombatConsumableVisitToken
					? "VISIT_RESUME"
					: "VISIT_INITIALIZE",
			nullptr, nullptr, nullptr, -1, tick, -1.0f, -1.0f,
			storedVisitToken, G_npcCombatConsumableVisitToken,
			"counter=visit_token");
	}
}

static CGSVariableSet *NPC_EnsureVisitContext()
{
	if (!WorldServer)
		return nullptr;

	const int tick = WorldServer->GetSegmentTimeCounter();
	const bool clockRestarted =
		tick < G_npcCombatConsumableLastObservedTick;
	const bool checkZone = G_npcCombatConsumableZone.empty() ||
		clockRestarted || tick >= G_npcCombatConsumableNextZoneCheckTick;
	G_npcCombatConsumableLastObservedTick = tick;

	if (checkZone)
	{
		const std::string zone = NPC_CurrentMobName();
		if (zone.empty())
			return nullptr;

		G_npcCombatConsumableNextZoneCheckTick =
			tick + NPC_CONSUMABLE_ZONE_CHECK_SEGMENTS;
		if (zone != G_npcCombatConsumableZone)
		{
			// Mission transitions normally arrive through RestartScript. This
			// fallback also handles base maps, which use LoadScriptFromMOB.
			if (!G_npcCombatConsumableZone.empty())
				G_npcCombatConsumableFreshVisit = true;
			G_npcCombatConsumableStates.clear();
			G_npcCombatConsumableZone = zone;
			G_npcCombatConsumableVisitToken = 0;
			G_npcCombatConsumableVars = nullptr;
			NPC_Log("ZONE_CONTEXT", nullptr, nullptr, nullptr, -1,
				tick, -1.0f, -1.0f, -1, -1,
				"source=periodic_or_initial_check");
		}
	}

	if (!G_npcCombatConsumableVars)
	{
		CPlayer *player = WorldServer->GetPlayer(0);
		if (!player)
			return nullptr;
		G_npcCombatConsumableVars = &player->party.vars;
	}

	CGSVariableSet &vars = *G_npcCombatConsumableVars;
	NPC_ResolveVisit(vars, tick);
	return &vars;
}

static bool NPC_LoadSlotConfig(CUnit *unit,
								int kind,
								int selection,
								SNpcCombatConsumableSlot &slot)
{
	const int variant = selection == NPC_CONSUMABLE_SELECTION_AUTO
		? 0
		: selection;
	ModConsumableConfig config;
	if (!ModRuntime::Current()->GetNpcCombatConsumableConfig(
			G_npcCombatConsumableZone.c_str(),
			unit->GetID(),
			NPC_PrototypeName(unit),
			kind + 1,
			variant,
			config))
		return false;
	if ((selection == NPC_CONSUMABLE_SELECTION_AUTO) != config.automatic)
		return false;

	slot.formula = config.spellFormula;
	slot.maxCharges = config.charges;
	return true;
}

static void NPC_PersistState(CGSVariableSet &vars,
							 int unitId,
							 const SNpcCombatConsumableState &state)
{
	uint32_t packedState =
		static_cast<uint32_t>(state.visitToken) <<
		NPC_CONSUMABLE_STATE_TOKEN_SHIFT;
	uint32_t packedMeta = 0;
	for (int kind = 0; kind < NPC_CONSUMABLE_COUNT; ++kind)
	{
		const SNpcCombatConsumableSlot &slot = state.slots[kind];
		packedState |=
			static_cast<uint32_t>(slot.remainingCharges & 7) << (kind * 3);
		packedMeta |=
			static_cast<uint32_t>(slot.selection & 3) << (kind * 2);
		packedMeta |=
			static_cast<uint32_t>(slot.phase & 3) << (10 + kind * 2);
	}

	vars.SetVar(NPC_UnitKey("state", G_npcCombatConsumableZone, unitId).c_str(),
				static_cast<float>(packedState));
	vars.SetVar(NPC_UnitKey("meta", G_npcCombatConsumableZone, unitId).c_str(),
				static_cast<float>(packedMeta));

	const std::string healingKey =
		NPC_UnitKey("heal", G_npcCombatConsumableZone, unitId);
	if (state.nextHealingTick > 0)
		vars.SetVar(healingKey.c_str(),
					static_cast<float>(state.nextHealingTick));
	else
		vars.DelVar(healingKey.c_str());

	NPC_Log("STATE_PERSIST", state.unit, state.server, &state, -1,
		WorldServer ? WorldServer->GetSegmentTimeCounter() : -1,
		-1.0f, -1.0f, -1, -1, "storage=party_gs");
}

static SNpcCombatConsumableState *NPC_GetState(
	CUnit *unit, CUnitServer *server, CGSVariableSet &vars)
{
	SNpcCombatConsumableState &state =
		G_npcCombatConsumableStates[unit->GetID()];
	state.unit = unit;
	state.server = server;
	if (state.initialized &&
		state.visitToken == G_npcCombatConsumableVisitToken)
		return &state;

	state = SNpcCombatConsumableState();
	state.unit = unit;
	state.server = server;
	state.visitToken = G_npcCombatConsumableVisitToken;
	state.initialized = true;

	const uint32_t packedState = NPC_ReadPacked(
		vars, NPC_UnitKey("state", G_npcCombatConsumableZone, unit->GetID()));
	const int storedVisitToken = static_cast<int>(
		packedState >> NPC_CONSUMABLE_STATE_TOKEN_SHIFT);

	bool persistAutomatic = false;
	if (storedVisitToken == state.visitToken)
	{
		const uint32_t packedMeta = NPC_ReadPacked(
			vars, NPC_UnitKey("meta", G_npcCombatConsumableZone, unit->GetID()));
		state.nextHealingTick = static_cast<int>(NPC_ReadPacked(
			vars, NPC_UnitKey("heal", G_npcCombatConsumableZone,
							  unit->GetID())));
		for (int kind = 0; kind < NPC_CONSUMABLE_COUNT; ++kind)
		{
			SNpcCombatConsumableSlot &slot = state.slots[kind];
			slot.remainingCharges =
				static_cast<int>((packedState >> (kind * 3)) & 7);
			slot.selection =
				static_cast<int>((packedMeta >> (kind * 2)) & 3);
			slot.phase =
				static_cast<int>((packedMeta >> (10 + kind * 2)) & 3);
			if (slot.selection == 0 ||
				!NPC_LoadSlotConfig(unit, kind, slot.selection, slot))
			{
				slot = SNpcCombatConsumableSlot();
			}
			else
			{
				NPC_Log("STATE_RESTORE", unit, server, &state, kind,
					WorldServer ? WorldServer->GetSegmentTimeCounter() : -1,
					-1.0f, -1.0f, storedVisitToken, state.visitToken,
					"storage=party_gs");
			}
		}
	}
	else
	{
		for (int kind = 0; kind < NPC_CONSUMABLE_COUNT; ++kind)
		{
			SNpcCombatConsumableSlot &slot = state.slots[kind];
			if (NPC_LoadSlotConfig(
					unit, kind, NPC_CONSUMABLE_SELECTION_AUTO, slot))
			{
				slot.selection = NPC_CONSUMABLE_SELECTION_AUTO;
				slot.remainingCharges = slot.maxCharges;
				persistAutomatic = true;
				NPC_Log("AUTO_STOCK", unit, server, &state, kind,
					WorldServer ? WorldServer->GetSegmentTimeCounter() : -1,
					-1.0f, -1.0f, 0, slot.remainingCharges,
					"source=lua_manual_config");
			}
		}
	}

	if (persistAutomatic)
		NPC_PersistState(vars, unit->GetID(), state);
	return &state;
}

static bool NPC_FormulaEquals(const std::string &left, const char *right)
{
	return right && left == right;
}

static bool NPC_FindRegistrationSelection(CUnit *unit,
									  int kind,
									  const char *formula,
									  int charges,
									  int &selection,
									  ModConsumableConfig &found)
{
	for (int candidate = NPC_CONSUMABLE_SELECTION_AUTO;
		candidate >= 1;
		--candidate)
	{
		const int variant = candidate == NPC_CONSUMABLE_SELECTION_AUTO
			? 0
			: candidate;
		ModConsumableConfig config;
		if (!ModRuntime::Current()->GetNpcCombatConsumableConfig(
				G_npcCombatConsumableZone.c_str(), unit->GetID(),
				NPC_PrototypeName(unit), kind + 1, variant, config))
			continue;
		if (config.charges == charges &&
			NPC_FormulaEquals(config.spellFormula, formula))
		{
			selection = candidate;
			found = config;
			return true;
		}
	}
	return false;
}

static bool NPC_Register(CUnit *unit,
						 const char *kindName,
						 const char *formula,
						 int charges)
{
	const int kind = NPC_ParseKind(kindName);
	if (!unit || kind < 0 || !formula || !formula[0] ||
		charges < 1 || charges > 7)
		return false;

	CUnitSpecific *specific = unit->GetUnitSpecific();
	if (!specific || specific->WhoAmI() != HPC_UNITSERVERSPECIFIC)
		return false;
	CUnitServer *server = static_cast<CUnitServer *>(specific);

	CGSVariableSet *vars = NPC_EnsureVisitContext();
	if (!vars)
		return false;

	SNpcCombatConsumableState *state = NPC_GetState(unit, server, *vars);

	// Explicit Lua zone/prototype assignments are authoritative. They are
	// applied automatically at visit start and must not be replaced by a
	// legacy MOB registration for the same slot.
	ModConsumableConfig automaticConfig;
	if (ModRuntime::Current()->GetNpcCombatConsumableConfig(
			G_npcCombatConsumableZone.c_str(), unit->GetID(),
			NPC_PrototypeName(unit), kind + 1, 0, automaticConfig) &&
		automaticConfig.automatic)
	{
		NPC_Log("REGISTER_MANUAL_OVERRIDE", unit, server, state, kind,
			WorldServer ? WorldServer->GetSegmentTimeCounter() : -1,
			-1.0f, -1.0f, charges,
			state->slots[kind].remainingCharges,
			"source=lua_manual_config");
		return true;
	}

	int selection = 0;
	ModConsumableConfig config;
	if (!NPC_FindRegistrationSelection(
			unit, kind, formula, charges, selection, config))
	{
		NPC_Log("REGISTER_FAILED", unit, server, state, kind,
			WorldServer ? WorldServer->GetSegmentTimeCounter() : -1,
			-1.0f, -1.0f, charges, 0,
			"reason=catalog_and_prototype_profile_missing");

		return false;
	}

	SNpcCombatConsumableSlot &slot = state->slots[kind];
	if (slot.selection == selection &&
		slot.maxCharges == config.charges &&
		slot.formula == config.spellFormula)
	{
		NPC_Log("REGISTER_KEEP", unit, server, state, kind,
			WorldServer ? WorldServer->GetSegmentTimeCounter() : -1,
			-1.0f, -1.0f, slot.remainingCharges,
			slot.remainingCharges,
			config.prototypeFallback
				? "source=mob_prototype_fallback_existing_state"
				: "source=mob_catalog_existing_state");
		return true;
	}

	const int previousCharges = slot.remainingCharges;
	slot.formula = config.spellFormula;
	slot.maxCharges = config.charges;
	slot.remainingCharges = config.charges;
	slot.selection = selection;
	slot.phase = NPC_CONSUMABLE_READY;
#if JMV_NPC_COMBAT_CONSUMABLES_DIAGNOSTICS
	slot.diagnosticGate = -1;
#endif
	if (kind == NPC_CONSUMABLE_HEALING)
		state->nextHealingTick = 0;
	NPC_Log("REGISTER_STOCK", unit, server, state, kind,
		WorldServer ? WorldServer->GetSegmentTimeCounter() : -1,
		-1.0f, -1.0f, previousCharges, slot.remainingCharges,
		config.prototypeFallback
			? "source=mob_prototype_fallback"
			: "source=mob_catalog");
	NPC_PersistState(*vars, unit->GetID(), *state);
	return true;
}

static bool NPC_HasConfiguredSlot(const SNpcCombatConsumableState &state)
{
	for (int kind = 0; kind < NPC_CONSUMABLE_COUNT; ++kind)
		if (state.slots[kind].selection != 0)
			return true;
	return false;
}

static bool NPC_Cast(CUnit *unit,
					 CUnitServer *server,
					 const std::string &formula,
					 const char *&failureReason)
{
	failureReason = "none";
	CSpellManager *spellManager = WorldServer->GetSpellManager();
	if (!spellManager)
	{
		failureReason = "spell_manager_missing";
		return false;
	}

	CSmartPtr<CSpell> spell = spellManager->CreateSpell(formula.c_str());
	if (spell.IsEmpty())
	{
		failureReason = "create_spell_failed";
		return false;
	}

	server->InformFailure(ACK_HERO_ACK_USE_POTION);
	if (WorldClient)
		WorldClient->ReceiveDamage(unit->GetID(), 0, HLDAMAGE_POTION);

	const CPlot3D point = unit->GetCP();
	WorldServer->CastSpell(spell, nullptr, point, unit, point);
	return true;
}

static int NPC_ConditionGate(int kind,
							 SRPGCommonStats *stats,
							 CUnit *unit,
							 CUnitServer *server,
							 int tick,
							 const SNpcCombatConsumableState &state)
{
	switch (kind)
	{
	case NPC_CONSUMABLE_REGENERATION:
		if (stats->ssHP.fpCurrent >= stats->ssHP.fpMax * 0.75f)
			return NPC_CONSUMABLE_GATE_HP_ABOVE_75;
		if (unit->HasSpellEffect(NPC_CONSUMABLE_EFFECT_IDS[kind]))
			return NPC_CONSUMABLE_GATE_EFFECT_ACTIVE;
		return NPC_CONSUMABLE_GATE_READY;
	case NPC_CONSUMABLE_HEALING:
		if (stats->ssHP.fpCurrent >= stats->ssHP.fpMax * 0.4f)
			return NPC_CONSUMABLE_GATE_HP_ABOVE_40;
		if (tick < state.nextHealingTick)
			return NPC_CONSUMABLE_GATE_HEALING_COOLDOWN;
		return NPC_CONSUMABLE_GATE_READY;
	case NPC_CONSUMABLE_STRENGTH:
	case NPC_CONSUMABLE_SPEED:
		if (server->logic.GetScriptState() != US_ATTACK)
			return NPC_CONSUMABLE_GATE_NOT_ATTACKING;
		if (unit->HasSpellEffect(NPC_CONSUMABLE_EFFECT_IDS[kind]))
			return NPC_CONSUMABLE_GATE_EFFECT_ACTIVE;
		return NPC_CONSUMABLE_GATE_READY;
	case NPC_CONSUMABLE_ENLARGE:
		if (stats->ssHP.fpCurrent >= stats->ssHP.fpMax)
			return NPC_CONSUMABLE_GATE_NOT_DAMAGED;
		if (unit->HasSpellEffect(NPC_CONSUMABLE_EFFECT_IDS[kind]))
			return NPC_CONSUMABLE_GATE_EFFECT_ACTIVE;
		return NPC_CONSUMABLE_GATE_READY;
	default:
		return NPC_CONSUMABLE_GATE_NOT_ATTACKING;
	}
}

static void NPC_Update(SRPGCommonStats *stats, CUnit *unit)
{
	if (!true || !stats || !unit ||
		unit->IsHero() || unit->GetOwner() ||
		stats->ssHP.fpMax <= 0.0f || stats->ssHP.fpCurrent <= 0.0f)
		return;

	CUnitSpecific *specific = unit->GetUnitSpecific();
	if (!specific || specific->WhoAmI() != HPC_UNITSERVERSPECIFIC)
		return;
	CUnitServer *server = static_cast<CUnitServer *>(specific);

	CGSVariableSet *vars = NPC_EnsureVisitContext();
	if (!vars)
		return;
	SNpcCombatConsumableState *state = NPC_GetState(unit, server, *vars);
	if (!NPC_HasConfiguredSlot(*state))
		return;

	const int tick = WorldServer->GetSegmentTimeCounter();
	if (tick < state->nextPollTick)
		return;
	state->nextPollTick = tick + NPC_CONSUMABLE_POLL_SEGMENTS +
		(unit->GetID() & 3);

	bool changed = false;
	for (int kind = 0; kind < NPC_CONSUMABLE_COUNT; ++kind)
	{
		SNpcCombatConsumableSlot &slot = state->slots[kind];
		if (slot.selection == 0)
			continue;

		const int effectId = NPC_CONSUMABLE_EFFECT_IDS[kind];
		if (slot.phase == NPC_CONSUMABLE_WAIT_EFFECT)
		{
			if (effectId >= 0 && unit->HasSpellEffect(effectId))
			{
				const int previousPhase = slot.phase;
				slot.phase = NPC_CONSUMABLE_WAIT_END;
				changed = true;
				NPC_Log("EFFECT_SEEN", unit, server, state, kind, tick,
					stats->ssHP.fpCurrent, stats->ssHP.fpMax,
					previousPhase, slot.phase, "counter=phase");
			}
			else
			{
				NPC_LogGate(unit, server, state, kind,
					NPC_CONSUMABLE_GATE_WAIT_EFFECT, tick,
					stats->ssHP.fpCurrent, stats->ssHP.fpMax);
			}
			continue;
		}
		if (slot.phase == NPC_CONSUMABLE_WAIT_END)
		{
			if (effectId >= 0 && unit->HasSpellEffect(effectId))
			{
				NPC_LogGate(unit, server, state, kind,
					NPC_CONSUMABLE_GATE_WAIT_END, tick,
					stats->ssHP.fpCurrent, stats->ssHP.fpMax);
				continue;
			}
			const int previousPhase = slot.phase;
			slot.phase = NPC_CONSUMABLE_READY;
			changed = true;
			NPC_Log("EFFECT_ENDED", unit, server, state, kind, tick,
				stats->ssHP.fpCurrent, stats->ssHP.fpMax,
				previousPhase, slot.phase, "counter=phase");
		}

		if (slot.remainingCharges <= 0)
		{
			NPC_LogGate(unit, server, state, kind,
				NPC_CONSUMABLE_GATE_NO_CHARGES, tick,
				stats->ssHP.fpCurrent, stats->ssHP.fpMax);
			continue;
		}
		const int gate = NPC_ConditionGate(
			kind, stats, unit, server, tick, *state);
		NPC_LogGate(unit, server, state, kind, gate, tick,
			stats->ssHP.fpCurrent, stats->ssHP.fpMax);
		if (gate != NPC_CONSUMABLE_GATE_READY)
			continue;
		const int chargesBefore = slot.remainingCharges;
		const char *failureReason = "none";
		if (!NPC_Cast(unit, server, slot.formula, failureReason))
		{
			NPC_Log("CAST_FAILED", unit, server, state, kind, tick,
				stats->ssHP.fpCurrent, stats->ssHP.fpMax,
				chargesBefore, slot.remainingCharges, failureReason);
			continue;
		}

		--slot.remainingCharges;
		if (kind == NPC_CONSUMABLE_HEALING)
		{
			state->nextHealingTick =
				tick + NPC_CONSUMABLE_HEALING_COOLDOWN_SEGMENTS;
		}
		else if (slot.remainingCharges > 0)
		{
			slot.phase = NPC_CONSUMABLE_WAIT_EFFECT;
		}
		NPC_Log("CAST", unit, server, state, kind, tick,
			stats->ssHP.fpCurrent, stats->ssHP.fpMax,
			chargesBefore, slot.remainingCharges,
			NPC_ConditionDetail(kind));
		changed = true;
	}

	if (changed)
		NPC_PersistState(*vars, unit->GetID(), *state);
}

static void NPC_BeginVisit(WorldLoadOrigin origin, const std::string &zone)
{
    G_npcCombatConsumableStates.clear();
    G_npcCombatConsumableZone = zone;
    G_npcCombatConsumableVisitToken = 0;
    G_npcCombatConsumableFreshVisit = origin == WorldLoadOrigin::ZoneEntry;
    G_npcCombatConsumableVars = nullptr;
    G_npcCombatConsumableNextZoneCheckTick = 0;
    G_npcCombatConsumableLastObservedTick = 0;
    NPC_Log("LOAD_RUNTIME_RESET", nullptr, nullptr, nullptr, -1, 0,
        -1.0f, -1.0f, -1, -1,
        origin == WorldLoadOrigin::SavedGame ? "reason=save_load_resume" : "reason=mission_zone_entry");
}

static void NPC_OnScriptRestart()
{
    // A script restart is not a new visit. Load/NewZone own the boundary.
    const std::string zone = NPC_CurrentMobName();
    if (!zone.empty() && zone != G_npcCombatConsumableZone)
        NPC_BeginVisit(WorldLoadOrigin::ZoneEntry, zone);
}

} // namespace

bool JMV_RegisterNpcCombatConsumable(CUnit *unit,
									 const char *kind,
									 const char *formula,
									 int charges)
{
	// The command must remain recognized even when the feature is turned off,
	// otherwise a map compiled with it aborts before later quest commands.
	return !true ||
		NPC_Register(unit, kind, formula, charges);
}

void JMV_NpcConsumablesUpdate(SRPGCommonStats*s,CUnit*u){if(ModRuntime::Current()&&theWorldServer.GetPlayerManager())NPC_Update(s,u);}
void JMV_NpcConsumablesLoaded(WorldLoadOrigin origin){if(ModRuntime::Current())NPC_BeginVisit(origin,NPC_CurrentMobName());}
void JMV_NpcConsumablesRestart(){if(ModRuntime::Current())NPC_OnScriptRestart();}
void JMV_NpcConsumablesReset(){G_npcCombatConsumableStates.clear();G_npcCombatConsumableZone.clear();G_npcCombatConsumableVars=nullptr;G_npcCombatConsumableVisitToken=0;G_npcCombatConsumableFreshVisit=false;G_npcCombatConsumableNextZoneCheckTick=0;G_npcCombatConsumableLastObservedTick=0;}
bool JMV_CheckNpcConsumablePacking(){CGSVariableSet vars;SNpcCombatConsumableState state;state.visitToken=511;state.nextHealingTick=109;for(int i=0;i<5;i++){state.slots[i].remainingCharges=7;state.slots[i].selection=3;state.slots[i].phase=2;}NPC_PersistState(vars,123,state);auto bits=NPC_ReadPacked(vars,NPC_UnitKey("state",G_npcCombatConsumableZone,123));auto meta=NPC_ReadPacked(vars,NPC_UnitKey("meta",G_npcCombatConsumableZone,123));if((bits>>15)!=511)return false;for(int i=0;i<5;i++)if(((bits>>(i*3))&7)!=7||((meta>>(i*2))&3)!=3||((meta>>(10+i*2))&3)!=2)return false;return NPC_ParseKind("HEALING")==1&&NPC_NormalizeMobName("Maps/ZONE_1.mob")=="zone_1";}

bool JMV_CheckNpcVisitLifecycle()
{
    struct Reset { ~Reset(){JMV_NpcConsumablesReset();} } reset;
    int prototype=-1;
    for(int i=0;i<theUnitDatabase.prototypes.GetNumEntries();++i)
        if(theUnitDatabase.prototypes.GetEntry(i)->szName=="CyclopsA16"){prototype=i;break;}
    if(prototype<0)return false;
    CUnit unit;unit.SetID(4515);unit.SetSubtypeID(prototype);CUnitServer server;
    CGSVariableSet vars;
    NPC_BeginVisit(WorldLoadOrigin::SavedGame,"zone5_1");
    vars.SetVar(NPC_VisitKey("zone5_1").c_str(),41);
    NPC_ResolveVisit(vars,100);
    SNpcCombatConsumableState spent;spent.visitToken=41;spent.nextHealingTick=1234;
    spent.slots[NPC_CONSUMABLE_STRENGTH].selection=NPC_CONSUMABLE_SELECTION_AUTO;
    spent.slots[NPC_CONSUMABLE_STRENGTH].remainingCharges=0;
    NPC_PersistState(vars,unit.GetID(),spent);
    CMemorySave saved;saved.StartSaving();vars.Store(saved);
    auto restore=[&](){vars.clear();saved.Seek(0);saved.StartLoading();vars.Load(saved);return saved.IsOk()&&saved.GetPosition()==saved.GetSize();};
    if(!restore())return false;
    // There is no elapsed-wall-clock input: arbitrarily delayed loads resume.
    NPC_BeginVisit(WorldLoadOrigin::SavedGame,"zone5_1");NPC_ResolveVisit(vars,1000000);
    auto*state=NPC_GetState(&unit,&server,vars);
    if(state->visitToken!=41||state->slots[NPC_CONSUMABLE_STRENGTH].remainingCharges!=0||state->nextHealingTick!=1234)return false;
    // The same saved map entered as a zone is a fresh visit even immediately.
    NPC_BeginVisit(WorldLoadOrigin::ZoneEntry,"zone5_1");NPC_ResolveVisit(vars,1000000);
    state=NPC_GetState(&unit,&server,vars);
    if(state->visitToken!=42||state->slots[NPC_CONSUMABLE_STRENGTH].remainingCharges!=1)return false;
    NPC_ResolveVisit(vars,1000001);if(G_npcCombatConsumableVisitToken!=42)return false;
    // Roll back to an earlier save after spending/restocking in a later visit.
    if(!restore())return false;
    NPC_BeginVisit(WorldLoadOrigin::SavedGame,"zone5_1");NPC_ResolveVisit(vars,1);
    state=NPC_GetState(&unit,&server,vars);
    if(state->visitToken!=41||state->slots[NPC_CONSUMABLE_STRENGTH].remainingCharges!=0)return false;
    // Save without markers gets exactly one initial pack.
    vars.clear();NPC_BeginVisit(WorldLoadOrigin::SavedGame,"zone5_1");NPC_ResolveVisit(vars,0);
    state=NPC_GetState(&unit,&server,vars);
    if(state->visitToken!=1||state->slots[NPC_CONSUMABLE_STRENGTH].remainingCharges!=1)return false;
    return true;
}
#undef WorldServer
#undef WorldClient
#undef JMV_NPC_COMBAT_CONSUMABLES_DIAGNOSTICS
