#include "E1MobSource.h"
#include "owned_resource_runtime.h"
#include "retained_static_scene.h"
#include "../GameJournal.h"
#include <string>

namespace {
// The open source of the running MOB load. Loads nest (a main zone load runs
// the MOB parser inside it), so each scope saves and restores the outer one.
std::uint64_t transaction = 0;
bool parsed = false;
unsigned admitted = 0;
std::string label;
}

namespace OwnedRender {
E1MobSource::E1MobSource(const char* terrain, const char* mob) noexcept
    : previousTransaction(transaction), previousParsed(parsed) {
    transaction = terrain && mob && *terrain && *mob
        ? owned::retained_static_begin_source(owned::MobSourceRole::main, terrain, mob) : 0;
    parsed = false; admitted = 0;
    try { label = std::string("main ") + (terrain ? terrain : "") + " " + (mob ? mob : ""); } catch (...) {}
}

E1MobSource::E1MobSource(const char* secondaryMob) noexcept
    : previousTransaction(transaction), previousParsed(parsed) {
    transaction = secondaryMob && *secondaryMob
        ? owned::retained_static_begin_source(owned::MobSourceRole::secondary, secondaryMob) : 0;
    parsed = false; admitted = 0;
    try { label = std::string("secondary ") + (secondaryMob ? secondaryMob : ""); } catch (...) {}
}

E1MobSource::~E1MobSource() {
    const std::uint64_t finished = transaction;
    owned::retained_static_finish_source(transaction, success, parsed);
    // RS02c: one line per MOB load until the static scene publishes.
    try {
        const auto identity = owned::retained_static_scene_identity_snapshot();
        GameJournal::Event("RENDER", "mob_source",
            "%s transaction=%llu success=%d parsed=%d admitted=%u scene=%s pending=%zu main=%s",
            label.c_str(), static_cast<unsigned long long>(finished), success ? 1 : 0, parsed ? 1 : 0,
            admitted, owned::retained_scene_identity_status_name(identity.status),
            static_cast<std::size_t>(identity.pending_transactions), identity.main_mob_resource.c_str());
    } catch (...) {}
    transaction = previousTransaction;
    parsed = previousParsed;
}

void E1MobSource::Parsed(const char* mob) noexcept {
    if (transaction && mob && *mob)
        parsed = owned::retained_static_record_parser_source(transaction, mob);
}

void E1MobSource::Admitted(std::uint32_t nid, std::uint32_t type, int id) noexcept {
    if (!transaction) return;
    owned::retained_static_record_admission(transaction, nid, type, id);
    ++admitted;
}
}
