#include "E1MobSource.h"
#include "owned_resource_runtime.h"
#include "retained_static_scene.h"

namespace {
// The open source of the running MOB load. Loads nest (a main zone load runs
// the MOB parser inside it), so each scope saves and restores the outer one.
std::uint64_t transaction = 0;
bool parsed = false;
}

namespace OwnedRender {
E1MobSource::E1MobSource(const char* terrain, const char* mob) noexcept
    : previousTransaction(transaction), previousParsed(parsed) {
    transaction = terrain && mob && *terrain && *mob
        ? owned::retained_static_begin_source(owned::MobSourceRole::main, terrain, mob) : 0;
    parsed = false;
}

E1MobSource::E1MobSource(const char* secondaryMob) noexcept
    : previousTransaction(transaction), previousParsed(parsed) {
    transaction = secondaryMob && *secondaryMob
        ? owned::retained_static_begin_source(owned::MobSourceRole::secondary, secondaryMob) : 0;
    parsed = false;
}

E1MobSource::~E1MobSource() {
    owned::retained_static_finish_source(transaction, success, parsed);
    transaction = previousTransaction;
    parsed = previousParsed;
}

void E1MobSource::Parsed(const char* mob) noexcept {
    if (transaction && mob && *mob)
        parsed = owned::retained_static_record_parser_source(transaction, mob);
}

void E1MobSource::Admitted(std::uint32_t nid, std::uint32_t type, int id) noexcept {
    if (transaction) owned::retained_static_record_admission(transaction, nid, type, id);
}
}
