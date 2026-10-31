#include "StdAfx.h"
#include "SettlementMob.h"
#include "WorldServer.h"
#include "PlayerServer.h"
#include "UnitServer.h"
#include "GameJournal.h"
#include <algorithm>
#include <cmath>

namespace Settlement {
std::vector<DialogueTopic> World::DialogueTopics(CWorldServer &world, int player, int target) const
{
    std::vector<DialogueTopic> result;
    auto *owner = world.GetPlayer(player);
    auto *unit = world.GetUnit(target);
    if (!owner || !unit || unit->IsDead()) return result;
    for (const auto &var : owner->party.vars) {
        if (var.second != 1 || var.first.compare(0, 2, "b.") != 0) continue;
        const auto dot = var.first.find('.', 2);
        if (dot == std::string::npos || var.first.find('.', dot + 1) != std::string::npos) continue;
        DialogueTopic topic;
        topic.key = var.first; topic.actor = var.first.substr(2, dot - 2); topic.name = var.first.substr(dot + 1);
        Name actor, name;
        if (!ParseName(topic.actor, actor) || !ParseName(topic.name, name) || actor.settlement != name.settlement ||
            !Contains(actor.settlement)) continue;
        const auto &def = GetDefinition(actor.settlement);
        const auto binding = def.briefings.find(name.local);
        // Shop-only bindings belong to the trade stage; never enter the camp UI.
        if (!def.peace.Enabled() || AreaAt(unit->GetCP()) != actor.settlement ||
            binding == def.briefings.end() || binding->second.text.empty()) continue;
        if (Object(world, 0, topic.actor, true) != target) continue;
        topic.binding = binding->second;
        result.push_back(std::move(topic));
    }
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) { return a.key < b.key; });
    return result;
}

bool World::DialogueAvailable(CWorldServer &world, int player, int target) const
{
    try {
    const auto topics = DialogueTopics(world, player, target);
    if (topics.empty()) return false;
    Name name; ParseName(topics.front().name, name);
    auto *npc = world.GetUnit(target);
    // Availability before approaching does not waive the later range check.
    return npc && !npc->IsEnemy(player) && AreaAt(npc->GetCP()) == name.settlement &&
        !instances.at(name.settlement).alarm;
    } catch (const std::exception &error) {
        // Hover and cursor queries must not turn a bad authored GS key into an
        // exception escaping the mission screen. The journal coalesces repeats.
        GameJournal::Notification("settlement_dialogue", error.what(), "availability rejected");
        return false;
    }
}

bool World::DialogueReady(CWorldServer &world, int player, int target) const
{
    if (DialogueActive() || world.IsFieldCampPending() || world.GetServerMode() != SERV_MODE_LOCAL || world.curInterface != IFACE_MISSION) return false;
    const auto topics = DialogueTopics(world, player, target);
    if (topics.empty()) return false;
    Name name; ParseName(topics.front().name, name);
    auto *owner = world.GetPlayer(player);
    auto *npc = world.GetUnit(target);
    auto *hero = owner && owner->party.people.GetSize() ? world.GetUnit(owner->party.people[0].nID) : nullptr;
    if (!hero || hero->IsDead() || hero->CheckFlag(UNIT_BLOCKED) || !npc ||
        !CanInteract(world, player, name.settlement, npc) || AreaAt(hero->GetCP()) != name.settlement ||
        distanceXY2(hero->GetCP(), npc->GetCP()) > 16 || std::abs(hero->GetCP().z-npc->GetCP().z)>2 ||
        world.aiMap.TraceRelief(hero->GetUnitHeadPosition(), npc->GetUnitHeadPosition()) < .99f) return false;
    return true;
}
std::uint64_t World::BeginDialogue(CWorldServer &world, int player, int target)
{
    if (!DialogueReady(world, player, target)) return 0;
    const auto topics = DialogueTopics(world, player, target);
    Name name; ParseName(topics.front().name, name);
    auto *owner = world.GetPlayer(player);
    auto *npc = world.GetUnit(target), *hero = world.GetUnit(owner->party.people[0].nID);
    if (++nextDialogue == 0) ++nextDialogue;
    dialogue = {};
    dialogue.token = nextDialogue; dialogue.settlement = name.settlement; dialogue.player = player;
    dialogue.target = target; dialogue.targetIdentity = npc->GetRTID();
    dialogue.hero = hero->GetID(); dialogue.heroIdentity = hero->GetRTID();
    auto *control = static_cast<CUnitServer *>(hero->GetUnitSpecific());
    if (control->GetScriptCommand().IsMove() || control->GetNextScriptCommand().IsMove()) {
        if (auto *playerControl = control->logic.GetPlayer()) playerControl->DoStop(control->logic);
        control->StopGroupSearchMotion();
    }
    GameJournal::Event("SETTLEMENT", "dialogue_open", "namespace=%s token=%llu target=%d hero=%d",
        Prefix(name.settlement).c_str(), static_cast<unsigned long long>(dialogue.token), target, dialogue.hero);
    return dialogue.token;
}

bool World::DialogueValid(CWorldServer &world, std::uint64_t token) const
{
    if (!token || dialogue.token != token) return false;
    auto *npc = world.GetUnit(dialogue.target), *hero = world.GetUnit(dialogue.hero);
    return npc && hero && !hero->IsDead() && !hero->CheckFlag(UNIT_BLOCKED) &&
        npc->GetRTID() == dialogue.targetIdentity && hero->GetRTID() == dialogue.heroIdentity &&
        AreaAt(hero->GetCP()) == dialogue.settlement && distanceXY2(hero->GetCP(), npc->GetCP()) <= 16 &&
        CanInteract(world, dialogue.player, dialogue.settlement, npc);
}

bool World::SelectDialogueTopic(CWorldServer &world, std::uint64_t token, const std::string &key)
{
    if (!DialogueValid(world, token) || !dialogue.topic.empty()) return false;
    const auto topics = DialogueTopics(world, dialogue.player, dialogue.target);
    if (std::none_of(topics.begin(), topics.end(), [&](const auto &topic) { return topic.key == key; })) return false;
    dialogue.topic = key;
    GameJournal::Event("SETTLEMENT", "dialogue_topic", "token=%llu topic=%s",
        static_cast<unsigned long long>(token), GameJournal::Quote(key.c_str()).c_str());
    return true;
}

bool World::AcceptDialogueCompletion(int player, const char *key)
{
    if (!DialogueActive() || dialogue.player != player || dialogue.topic != key ||
        !dialogue.completing || dialogue.consumed) return false;
    dialogue.consumed = true;
    return true;
}
bool World::FinishDialogue(CWorldServer &world, std::uint64_t token)
{
    if (!DialogueValid(world, token) || dialogue.topic.empty() || dialogue.consumed) return false;
    const auto key = dialogue.topic;
    dialogue.completing = true;
    struct CompletionScope { bool &flag; ~CompletionScope() { flag = false; } } completion{dialogue.completing};
    world.BriefingComplete(dialogue.player, key.c_str());
    auto *player = world.GetPlayer(dialogue.player);
    return dialogue.consumed && player && player->party.vars.GetVar(key.c_str()) == 2;
}
void World::EndDialogue(std::uint64_t token)
{
    if (!token || dialogue.token != token) return;
    GameJournal::Event("SETTLEMENT", "dialogue_close", "token=%llu completion_consumed=%d",
        static_cast<unsigned long long>(token), dialogue.consumed);
    dialogue = {};
}
}
