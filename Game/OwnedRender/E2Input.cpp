#include "E2Host.h"
#include "engine_bridge.h"
#include "debug_overlay.h"

namespace {
// Game thread. DebugOverlay registers after initialization and clears this
// before shutdown; no world pointer or ownership is retained.
owned::DebugOverlay* overlay = nullptr;
}
namespace OwnedRender {
bool E2CampInput(UINT message, WPARAM wparam, LPARAM lparam) noexcept {
    return overlay && overlay->process_camp_input(message, wparam, lparam);
}
}
namespace owned {
void bridge_set_camp_input_overlay(DebugOverlay* value) noexcept { overlay = value; }
}
