#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../../stdafx.h"
#include "E3Combat.h"
#include "E1Scene.h"
#include "../Unit.h"
#include "../UnitServer.h"
#include "../WorldServer.h"
#include "../SpellDatabase.h"
#include "../SpellSystem.h"
#include "../Item.h"
#include "../ModRuntime.h"
#include "../ModPackage.h"
#include "combat_ui.h"
#include <algorithm>
#include <cmath>
#include <climits>

namespace {
CUnit* ServerUnit(int id) {
    return theWorldServer.GetServerMode() != SERV_MODE_NONE && theWorldServer.GetMapObjectManager()
        ? theWorldServer.GetUnit(id) : nullptr;
}
// Read completed commands and selected motivation; never run AI or planning.
void PublishTargets(CUnit* unit, owned::CombatUnitValue& value) {
    if (value.dead || value.health <= 0) return;
    auto* serverUnit = ServerUnit(unit->GetID());
    auto* server = serverUnit ? dynamic_cast<CUnitServer*>(serverUnit->GetUnitSpecific()) : nullptr;
    if (!server) return;
    unsigned targets = 0;
    const auto addTarget = [&](CMapObject* target) {
        if (!target || target->WhoAmI() != HPC_UNIT || !target->GetID()) return;
        const auto id = static_cast<std::int32_t>(target->GetID());
        for (unsigned i = 0; i < targets; ++i) if (value.attack_targets[i] == id) return;
        if (targets < value.attack_targets.size()) value.attack_targets[targets++] = id;
    };
    for (const auto* command : {&server->GetScriptCommand(), &server->GetNextScriptCommand()}) {
        bool offensive = command->IsAttack();
        if (command->IsCast()) {
            CSpell* spell = nullptr;
            const unsigned slot = static_cast<unsigned>(command->nQSpellNum);
            if (slot & 0x80000000u) {
                const unsigned quick = slot & 0x7fffffffu;
                auto* item = quick < QI_MAX_ITEMS ? serverUnit->GetQItem(quick) : nullptr;
                if (item && (item->GetQuickItemType() == IDB_WAND || item->GetQuickItemType() == IDB_SCROLL))
                    spell = item->GetSpell(0);
            } else if (slot < serverUnit->GetSpellSlotCount()) spell = serverUnit->GetQSpell(slot);
            offensive = spell && spell->IsOffensive();
        }
        if (!offensive) continue;
        value.attacking = true;
        if (command->cmd != UC_CASTPOINT) addTarget(command->pTarget);
    }
    if (server->logic.GetCurrentMotivationType() == AI_UM_AGGRESSION) {
        value.attacking = true;
        addTarget(server->logic.GetAggressionTarget());
    }
}
}
namespace OwnedRender {
void E3CombatCatalog() {
    std::array<owned::CombatEffectDefinition, 256> catalog{};
    const bool hd = (ModRuntime::Current() && ModRuntime::Current()->CombatSetting("JMVGraphics2"));
    const int count = (std::min)(256, int(theSpellDatabase.spells.GetNumEntries()));
    for (int i = 0; i < count; ++i) {
        const auto* spell = theSpellDatabase.spells[i];
        if (!spell) continue;
        auto& definition = catalog[i];
        const char* code = spell->GetCodeName();
        if (!code) continue;
        strncpy_s(definition.name.data(), definition.name.size(), code, _TRUNCATE);
        definition.valid = definition.name[0] != 0;
        definition.hd_icon = hd; definition.icon = spell->dwTextureTypeIndex;
        for (const char* harmful : {"stench","stun","weak","feeblemind","slow","firewall","litnwall","acid_fog"})
            if (_stricmp(code,harmful) == 0) definition.debuff = true;
    }
    owned::combat_ui_publish_catalog(catalog);
}
bool E3CombatValue(CUnit* pUnit, owned::CombatUnitValue& value) {
    if (!pUnit) return false;
    const auto resource = E1Scene().Resource(pUnit);
    if (!resource.serial) return false;
    value = {};
    value.id = pUnit->GetID(); value.incarnation = resource.serial;
    value.health = pUnit->GetHP(); value.maximum_health = pUnit->GetMaxHP();
    value.dead = pUnit->IsDead() != FALSE;
    const auto& effects = pUnit->GetSpellEffects();
    const CUnit* server = ServerUnit(pUnit->GetID());
    bool paired = server && server->GetSpellEffects().GetSize() == effects.GetSize() && effects.GetSize() <= 256;
    if (paired) for (int i = 0; i < effects.GetSize(); ++i)
        if (effects[i].nSpellID != server->GetSpellEffects()[i].nSpellID || !std::isfinite(server->GetSpellEffects()[i].fpPower)) { paired = false; break; }
    std::array<float,256> maximum{};
    if (paired) for (int i = 0; i < effects.GetSize(); ++i) {
        const int id = effects[i].nSpellID;
        if (id >= 0 && id < 256)
            maximum[id] = (std::max)(maximum[id], server->GetSpellEffects()[i].fpPower);
    }
    for (int i = 0; i < effects.GetSize() && value.effect_count < value.effects.size(); ++i) {
        const auto& input = effects[i]; const int id = input.nSpellID;
        if (id < 0 || id >= 256) continue;
        auto& output = value.effects[value.effect_count++];
        output.id = static_cast<std::uint8_t>(id); output.ticks = input.nDuration;
        if (!paired) continue;
        output.amount = server->GetSpellEffects()[i].fpPower;
        output.whole_amount = maximum[id] > 10.f;
        if (id <= SPELL_ID_POISONFOG || id == SPELL_ID_VAMP) {
            const auto* spell = theSpellDatabase.spells[id];
            if (!spell) continue;
            const auto subtype = spell->mstSubtype;
            const auto* stats = pUnit->GetUserUnitStats();
            float base = 0;
            if (subtype >= RPGMST_FIRE && subtype <= RPGMST_ACID) base = stats->cAutoSkills[RPGASKL_ELEMENTAL];
            else if (subtype >= RPGMST_ILLUSION && subtype <= RPGMST_DIVINATION) base = stats->cAutoSkills[RPGASKL_SENSE];
            else if (subtype >= RPGMST_ENCHANTMENTS && subtype <= RPGMST_DOMINATION) base = stats->cAutoSkills[RPGASKL_ASTRAL];
            else continue;
            const float component = base + float(stats->GetPerkModifier(RPGPRK_FIRE + subtype));
            const double sum = double(component) + double(int(stats->cIntelligence) - 25);
            if (sum < INT_MIN || sum > INT_MAX) continue;
            const int skill = int(sum);
            output.amount *= float(double(.8f) + (double(float(skill)) * 30.0 / 120.0) / 100.0);
        }
        output.amount_available = std::isfinite(output.amount);
    }
    PublishTargets(pUnit, value);
    return true;
}
void E3CombatPublishUnit(CUnit* unit) {
    owned::CombatUnitValue value;
    if (E3CombatValue(unit, value)) owned::combat_ui_publish_unit(value);
}
void E3CombatBeginFrame(bool paused) noexcept {
    owned::combat_ui_begin_embedded_frame(paused);
}
}
