#pragma once
#include <cstdint>
class CUnit;
namespace owned { struct CombatUnitValue; }
namespace OwnedRender {
// Synchronous source reads only; c74 owns icons, text, plates and effect layout.
bool E3CombatValue(CUnit* unit, owned::CombatUnitValue& value);
void E3CombatCatalog();
void E3CombatPublishUnit(CUnit* unit);
void E3CombatBeginFrame(bool paused) noexcept;
}
