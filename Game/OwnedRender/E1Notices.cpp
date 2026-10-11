#include "E1Notices.h"
#include "../GameJournal.h"
#include <windows.h>

namespace { OwnedRender::E1SaveClashNotice saveClash; }
namespace OwnedRender {
void E1ObserveSaveReplacement(int id, const void* previous, const void* incoming) noexcept {
    if (saveClash.Observe(previous,incoming,GetTickCount64()))
        GameJournal::Event("RENDER","save_id_clash","id=%d saved object replaces map object",id);
}
void E1ClearSaveNotice() noexcept { saveClash.Clear(); }
}
namespace owned {
const char* bridge_save_id_clash_notice() noexcept {
    return saveClash.Current(GetTickCount64()) ?
        "Save ID clash: a saved object replaced a map object (see game journal)" : nullptr;
}
void bridge_neighbour_diagnostic(const char* reason) noexcept {
    // The journal bounds and aggregates repeated worker failures by reason.
    GameJournal::Count("RENDER","neighbour_maps",reason ? reason : "unspecified");
}
}
