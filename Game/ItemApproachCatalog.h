#pragma once

namespace ItemApproachCatalog {
// Exact, case-sensitive model membership, as in Ataman's containsModel().
// Catalog bindings are checked against the currently loaded item database.
bool UsesWorldXY(const char* model);
void Reset();
}
