#pragma once

#include "camp_layout.h"
#include <cstdint>
#include <windows.h>
struct ID3D11Device;

namespace owned {
struct NativeOwnedTextureView;
struct NativeGeometryView;
struct FixedState;
struct SurfaceImage;

bool install_camp_inventory(std::uint8_t* image,bool input_dispatch) noexcept;
bool uninstall_camp_inventory() noexcept;
void camp_inventory_output(unsigned width, unsigned height, unsigned frame_size = 72) noexcept;
// Applied at the next authoritative camp draw, without reactivating gameplay.
void camp_inventory_set_wide(bool wide) noexcept;
const char* camp_inventory_notice() noexcept;
bool camp_inventory_wheel(POINT client_point, short delta, UINT flags) noexcept;
// Only owned scrollbar mouse events are consumed. Keys remain native.
bool camp_inventory_input(HWND window,UINT message,WPARAM wparam,LPARAM lparam,
                          bool renderer_controls) noexcept;
void camp_inventory_cancel_drag() noexcept;
// Resource/device owners call this before invalidation or teardown.
void camp_inventory_retire_textures() noexcept;
void camp_inventory_texture_manager_destroying(void* owner) noexcept;
// Final owned resource-scope release; joins the CPU worker outside DllMain.
void camp_inventory_shutdown_icons() noexcept;
#ifdef EID3D11_EMBEDDED
void camp_inventory_begin_icon_frame() noexcept;
void camp_inventory_embedded_texture_manager(void* owner) noexcept;
#endif
// Only the actual bound modern icon identity/generation is admitted.
bool camp_inventory_texture(std::uint64_t id, std::uint64_t generation,
                            NativeOwnedTextureView& value) noexcept;
void camp_inventory_texture_failed() noexcept;
void camp_inventory_observe_texture(const NativeGeometryView&,const FixedState&,const SurfaceImage*) noexcept;
bool camp_inventory_observation_enabled() noexcept;
void camp_inventory_observe_count_time_clock(std::int64_t begin) noexcept;
void camp_inventory_observe_close_call(const char* phase,std::int64_t begin,std::int64_t end) noexcept;
void camp_inventory_observe_modal_idle(std::int64_t begin,std::int64_t end) noexcept;

// Owned RGB for the actual item in a synchronous part9 preview. Alpha stays native.
bool camp_inventory_preview_specular_rgb(std::uint32_t& rgb) noexcept;

// Temporary scroll observation; scalar only, consumed/reset at every Present.
// Texture-miss completion is nested in whole native-item draw wall time.
struct CampScrollCostSnapshot {
    struct Grid {
        bool observed = false;
        int first = 0, count = 0, category = 0, columns = 0, rows = 0;
    };
    bool observed = false, valid = true;
    int mode = 0;
    std::uint64_t generation = 0, first_qpc = 0, last_qpc = 0;
    std::uint64_t item_calls = 0, item_wall_qpc = 0, item_max_wall_qpc = 0;
    std::uint64_t miss_calls = 0, miss_wall_qpc = 0, miss_max_wall_qpc = 0;
    std::uint64_t figure_misses = 0;
    std::array<Grid, 2> grids{}; // Stash, vendor; no borrowed state survives.
};
CampScrollCostSnapshot camp_inventory_take_scroll_cost() noexcept;

// Published by the actual camp background draw, consumed once by its text pass.
struct CampDecoration {
    struct Panel { RECT bounds{}; float corner = 15; };
    std::array<Panel, 10> panels{};
    unsigned count = 0;
    RECT title_plate{};
    // Published from the existing owned rail draw and BG section layout.
    std::array<RECT, 6> thumbs{};
    unsigned thumb_count = 0;
    struct Divider { RECT bounds{}; bool quarter_turn = false; };
    std::array<Divider, 6> dividers{};
    unsigned divider_count = 0;
    // Three visual slots share one layer; blueprint and native result content
    // alternate in the right slot while ordinary/comparison remain independent.
    std::array<RECT, 3> cards{};
    unsigned card_count = 0;
    RECT construction_icon{};
    bool construction_icon_visible = false;
    float pixel_unit = 1;
};
bool camp_inventory_take_decoration(CampDecoration& value) noexcept;
void camp_inventory_art_failed() noexcept;

struct CampBlitPiece { RECT source, destination; };
inline constexpr unsigned kCampMappedPieces = 17; // Fourteen learned rows plus three other bands.
inline constexpr unsigned kCampCutRegions = 4 * 4 * 4;
inline constexpr unsigned kCampBlitPieces = kCampMappedPieces * kCampCutRegions;
// Returns -1 outside a camp surface, zero for an intentionally omitted native
// strip, otherwise the number of pieces. No recorded draw stream is retained.
int camp_inventory_blit(const RECT& destination, const RECT& source,
                        CampBlitPiece (&pieces)[kCampBlitPieces]) noexcept;
bool camp_inventory_clear(const RECT& source, RECT& destination) noexcept;
// Internal proxy presentation boundary; never retains native draw arguments.
using CampPopupDraw = void (*)(void*);
enum class CampRenderRequest { begin_interface, invalidate_popup, draw_popup, clear_popup };
using CampPopupSink = bool (*)(void*, CampRenderRequest, CampPopupDraw, void*);
void camp_inventory_set_popup_sink(void* context, CampPopupSink sink,
    ID3D11Device* device = nullptr) noexcept;
void camp_inventory_popup_failed() noexcept;
void camp_inventory_popup_published(bool visible) noexcept;

} // namespace owned

#include <memory>
#include <string>
#include <vector>
namespace owned {
struct PreparedCampIcon;
void camp_inventory_embedded_owner(void*, std::uint64_t generation, bool high_resolution) noexcept;
std::shared_ptr<const PreparedCampIcon> camp_inventory_request_icon(const char*, ID3D11Device*) noexcept;
void camp_inventory_prioritize_icons(unsigned lane, const std::vector<std::string>& visible,
                                      const std::vector<std::string>& selected);
}
