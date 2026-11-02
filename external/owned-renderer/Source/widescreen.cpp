// Owned widescreen for the fingerprinted stock vanilla image. The on-disk
// widescreen site group and the live stock layout bytes must both match.
// Foreign layout patches are rejected; supported entry dispatchers are
// forwarded through the existing verified entry-owner resolver.
//
// The owned project widescreen proposal supplies the centred 4:3 virtual
// space, edge-anchored HUD, hor+ projection and surface-local text mapping.
// The real output surface supplies aspect. This vanilla port preserves its
// explicit per-call anchor and paired cursor mapping, with native HUD and
// character-pane publication chained above the owned widget wrappers.
//
// On addon clones the addon's own 4:3-centred widescreen layout writes are
// recognised site by site, returned to stock bytes (as owned, restorable
// patches) and replaced by this port. The addon's other hooks stay and are
// chained. Anything unrecognised leaves the addon layout in place.
#include "widescreen.h"
#include "hud_tutorial_layout.h"
#include "hud_message_layout.h"
#include <climits>

#include "addon_hook_chain.h"
#include "engine_bridge.h"
#include "game_sites.h"
#include "world_fog.h"

#include <windows.h>
#include <ddraw.h>
#include <intrin.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <limits>
#include <vector>

namespace owned {

std::uint64_t bridge_hud_screen_generation() noexcept;
bool bridge_read_world_environment(WorldEnvironment& environment) noexcept;
bool game_hud_editing() noexcept;
void bridge_append_probe_event(const char* event, const char* detail) noexcept;

namespace {

using widescreen::Anchor;
using widescreen::Anchors;

constexpr std::uintptr_t kImageBase = 0x00400000;

// --- World sites ---------------------------------------------------------------

// CDXGeometryProcessor::CreateClipScaleMatrix (stock: identity, _22 = -1).
// Two callers: CVisualizatorD3D::Create (the main 4:3 projection, return
// 00500D1F) and SetSceneClipRect (builds its projection from the true rect
// aspect already, return 005016E5, keeps the stock matrix outside the
// synchronous full-world display refresh).
constexpr std::uintptr_t kClipScaleEntry = 0x004F1330;
constexpr std::uintptr_t kClipScaleCreateReturn = 0x00500D1F;

// The draw square (CWorldClient::Draw: CTerrain::SetClip(cam.x, cam.y, 100)
// twice as `push 42C80000h`; Draw3DScene: four x87 operands naming the
// 100.0f constant at 0073BAB0 for the object box).
constexpr std::uintptr_t kTerrainSquarePush[] = {0x0056A02C, 0x0056A0B8};
constexpr std::uintptr_t kObjectSquareOperand[] = {0x0056B0EA, 0x0056B13C, 0x0056B17A, 0x0056B1E1};
constexpr std::uintptr_t kStockSquareConstant = 0x0073BAB0;
constexpr float kStockSquare = 100.0f;

// --- Interface sites -------------------------------------------------------------

// CInterface3D conversions (__thiscall, one pointer argument, ret 4).
constexpr std::uintptr_t kVirtualToScreenRectEntry = 0x005F7CA0;
constexpr std::uintptr_t kVirtualToScreenPointEntry = 0x005F7D20;
constexpr std::uintptr_t kVirtualToScreenSizeEntry = 0x005F7D60;
constexpr std::uintptr_t kScreenToVirtualRectEntry = 0x005F7DA0;
constexpr std::uintptr_t kScreenToVirtualPointEntry = 0x005F7E00;
constexpr std::uintptr_t kScreenToVirtualSizeEntry = 0x005F7E30;
// Layout ingress (wrapped, then the stock body runs).
constexpr std::uintptr_t kMakeRectEntry = 0x005FC9E0;       // CI3DPolyArea::MakeRect(int x4), ret 10h
constexpr std::uintptr_t kAddPointEntry = 0x005FCA70;       // CI3DPolyArea::AddPoint(CPoint), ret 8
constexpr std::uintptr_t kRelativeToScreenEntry = 0x005E6FA0;  // CI3DFigure::SetRelativeToScreen(float x3)
constexpr std::uintptr_t kTranslationRelativeEntry = 0x005FC450;  // CI3DMapObjTranslation::SetRelativeToScreen
constexpr std::uintptr_t kMakeBarEntry = 0x005E70E0;
constexpr std::uintptr_t kMakeTilingBarEntry = 0x005E7680;
constexpr std::uintptr_t kMakeBarFrameEntry = 0x005E7D60;
constexpr std::uintptr_t kMakeBarFrame2Entry = 0x005E8450;
constexpr std::uintptr_t kMakeTriangleEntry = 0x005E99D0;
constexpr std::uintptr_t kGetPixelEntry = 0x005FD240;       // CI3DSurface::GetPixel(int, int)
constexpr std::uintptr_t kAddFigureEntry = 0x005FF3C0;      // CI3DObject::AddFigure(const char*)

// Calls whose rectangle or point is surface-local: `mov ecx, [reg+98h]`
// (the interface) becomes `xor ecx, ecx` so our conversion takes the local
// branch (edit-box, server-list, save-name and chat carets; tooltip offset).
constexpr std::uintptr_t kLocalInterfaceLoads[] = {
    0x0065C51F, 0x0065C544, 0x0061DB0E, 0x0061DB3E, 0x006569F7, 0x00656A08,
    0x005E3839, 0x005E386C, 0x0065E77E};
// CI3DSurface::DrawText: `mov ecx, [edi+4]` before its surface-local rect conversion.
constexpr std::uintptr_t kDrawTextInterfaceLoad = 0x005FDE94;
// Cursor-delta conversions: the RMB camera handler and the global-map RMB rotation.
constexpr std::uintptr_t kCameraDeltaCall = 0x005EB249;
constexpr std::uintptr_t kGlobalMapDeltaCall = 0x0063008C;
// Global map zone lookup on its half-size hit surface (OnMouseMove, OnLButtonDown).
constexpr std::uintptr_t kGlobalMapPixelCalls[] = {0x0063023B, 0x0062FB1F};
// CInterface3D::Draw2D: the full-screen 2D surface (push 1 virtual; 600; 800).
constexpr std::uintptr_t kDraw2DSurfaceArgs[] = {0x005F5469, 0x005F5567};
// CInterface3D::TextSize: the inline ScreenToVirtual of the measured text.
constexpr std::uintptr_t kTextSizeInline = 0x005F83CF;
constexpr std::uintptr_t kTextSizeInlineEnd = 0x005F841F;
constexpr std::uintptr_t kStringDestructor = 0x00700970;
// CInterface3D::Init: fScale = |lfHeight| * [0073D09C] (0.01).
constexpr std::uintptr_t kFontScaleOperand = 0x005F4A51;
// CI3DToolTip::Process: right (cmp/add 800) and bottom (cmp 600) clamps.
constexpr std::uintptr_t kTooltipRight = 0x0065E7C3;
constexpr std::uintptr_t kTooltipRightNegative = 0x0065E7CA;
constexpr std::uintptr_t kTooltipBottom = 0x0065E7F1;
// CI3DObjectMap::OnLButtonUp: placed cursor minus the map centre (700, 80).
constexpr std::uintptr_t kMapCentreSiteA = 0x006452EC;  // add eax,-700; add ecx,-80
constexpr std::uintptr_t kMapCentreSiteB = 0x00645430;  // add edx,-700; add eax,-80
// CI3DObjectInfo::Draw: the portrait's inline screen x (mov ecx, 100).
constexpr std::uintptr_t kPortraitScreenX = 0x00646608;
// CI3DScreenTutorial::RedrawPage: `push 0; call AddFigure` after a page rect.
constexpr std::uintptr_t kTutorialRectSite = 0x00659994;

// Stock layout ranges used by the existing widescreen proposal. Validate
// all of them before installation; this vanilla port never restores a
// foreign addon's layout to presumed stock bytes.
struct Range { std::uintptr_t va; std::uint8_t size; };
constexpr Range kStockLayoutRanges[] = {
    {0x005FC8F0, 7}, {0x005FFCC0, 7}, {0x005FFD30, 7}, {0x005FEBF0, 6},
    {0x005FC9E0, 5}, {0x005E9E77, 5}, {0x005E7C8D, 5}, {0x005E75D1, 5}, {0x005E835E, 5},
    {0x005FCA70, 5}, {0x005E6FA0, 5}, {0x005F7CA0, 5}, {0x005F7D20, 5}, {0x005F7D60, 5},
    {0x0065C51F, 6}, {0x0065C544, 6}, {0x0061DB0E, 6}, {0x0061DB3E, 6}, {0x006569F7, 6},
    {0x00656A08, 6}, {0x005E3839, 6}, {0x005E386C, 6}, {0x0065E77E, 6}, {0x0065E7C3, 4},
    {0x0065E7CA, 4}, {0x00641F20, 5}, {0x0064291F, 5}, {0x00642930, 5}, {0x00642B09, 5},
    {0x00641F51, 4}, {0x00641F61, 4}, {0x00642AFC, 4}, {0x00642D23, 4}, {0x00642D7F, 4},
    {0x006452ED, 4}, {0x00645432, 4}, {0x0064644C, 6}, {0x0064648E, 6}, {0x00646499, 6},
    {0x00645719, 5}, {0x00645F76, 5}, {0x00645F80, 5}, {0x00646299, 5}, {0x006462A0, 5},
    {0x00646B1D, 5}, {0x00646608, 4}, {0x006480EB, 4}, {0x00648147, 4}, {0x00646251, 4},
    {0x0063C880, 4}, {0x0063C894, 4}, {0x0063C800, 5}, {0x0063CA2F, 5}, {0x007428AC, 4},
    {0x0063CC13, 4}, {0x0063CC07, 4}, {0x0063F378, 5}, {0x0063F452, 5}, {0x0063F38E, 4},
    {0x0063F633, 4}, {0x0063E306, 4}, {0x0063E31A, 4}, {0x0063E4B0, 5}, {0x0063E6F7, 5},
    {0x0063BB2B, 4}, {0x0063BAFC, 4}, {0x005FC9F4, 8}, {0x0063BF80, 4}, {0x0063BF71, 4},
    {0x0063C050, 5}, {0x0063C0E3, 5}, {0x0063C16C, 5}, {0x00649190, 5}, {0x00649541, 5},
    {0x0064919F, 4}, {0x006491B3, 4}, {0x00649910, 5}, {0x00649A85, 5}, {0x00648520, 5},
    {0x006488D7, 5}, {0x00648531, 4}, {0x00648545, 4}, {0x00742B08, 4}, {0x005FD237, 9},
    {0x0063023C, 4}, {0x0062FB20, 4}, {0x005F546A, 1}, {0x005F546E, 4}, {0x005F5473, 4},
    {0x005F5568, 1}, {0x005F556C, 4}, {0x005F5571, 4}, {0x005F83CF, 80}, {0x005F4A4F, 6},
    {0x005F4F81, 4}, {0x004F135A, 19}, {0x005FDE94, 3}, {0x005F7D9E, 194}, {0x005EB249, 5},
    {0x0063008C, 5}, {0x00659994, 7}, {0x00742A18, 4},
    // Owner field operands must also remain stock in the running image.
    {0x00642C98,6},{0x00642C8B,6},{0x00642C0A,6},
    {0x0064805D,6},{0x00648050,6},{0x00647FCA,6},
    {0x0064A09D,6},{0x0064A090,6},{0x0064A00A,6},
    {0x00641C30,28},{0x006410EE,12},
    // Current localized clock tooltip lines/key and its active native area.
    {0x005FC690,7},{0x0065E552,6},{0x0065E917,6},{0x0065E9F4,6},{0x0065E9FF,3},
    {0x0065E68F,6},{0x0065E6A8,6},{0x0065E95E,6},{0x0065EA80,32},
    // Native tooltip sizing/font and paired surface DC publication.
    {0x0065E8F1,24},{0x0065E75C,6},{0x005F837E,4},{0x00709003,3},
    {0x005F8396,16},{0x005FDC00,87},{0x005FDC60,65},{0x005FDE20,6},
    // Fade samples semantic owner values; never a per-frame pose/rebuild serial.
    {0x0063D2EE,6},{0x0063DA28,12},{0x00531920,9},{0x0053199A,3},
    {0x0063CF4C,27},{0x0063CF73,6},{0x0063CF79,3},{0x00531976,7},{0x006A4230,9},
    {0x00635806,6},{0x0063588A,6},{0x00649921,6},{0x00649A96,6},
    {0x006488EE,6},{0x00642E93,5},{0x00642E9C,6},{0x00642EA2,6},{0x00642EAF,6},
    {0x006410FA,20},{0x00641114,20},{0x006411DD,26},{0x00641231,7},
    {0x0064633D,18},{0x00647B9E,6},{0x00647BA8,3},{0x00647BEF,4},{0x00647BFC,4},{0x00647E62,3},
    // CI3DFigure::Draw reads the final native Show() flag of selected portrait frames.
    {0x005E6D9C,12},{0x00640F4E,6},
    // CInterface3D normal 2D draw precedes screen/child tips; screen self call.
    {0x005F53B0,19},{0x006010A1,7},
    // Current original-item slot map, native capture release, wheel state.
    {0x0063CD29,25},{0x0063CCBF,41},{0x0063D5B9,15},{0x0063D916,11},
    {0x0063DB29,37},{0x0063DAB3,16},{0x0063DAF3,16},{0x0063DB5C,22},{0x0063E283,16},
    {0x00645640,159},{0x0074265C,4},
    {0x005FDA80,6}, {0x0065D0A0,7}, {0x0065DAA0,5}, {0x004EF5C0,16}, {0x00649E20,6}, {0x00649FF0,6},
};

// HUD widget methods run with their anchor (x, y). post_rect: the method
// stores the widget's CI3DObject rect (+D8..+E4) from layout constants; it
// is moved to placed space when the method returns. The prologue is the
// stock bytes our wrapper replays (whole instructions, no relative branch).
using widescreen::kTopLeft;
using widescreen::kTopRight;
using widescreen::kBottomLeft;
using widescreen::kBottomRight;
constexpr Anchors kChat{Anchor::content_start, Anchor::start};
using widescreen::kBottomCentre;
enum class InputScope { none, area, rect };
struct WidgetMethod { std::uintptr_t va; std::uint8_t prologue; Anchors anchor; bool post_rect; bool by_vtable = false; InputScope input = InputScope::none; bool handler_this = false; };
constexpr WidgetMethod kWidgetMethods[] = {
    // CI3DObjectMap (minimap, top right)
    {0x00641F20, 5, kTopRight, true},   // Init
    {0x00642930, 8, kTopRight, false},  // ScrollFigures
    {0x00642B40, 6, kTopRight, false},  // OnIdle
    {0x0065CF30, 7, kChat, false}, // ScrollText::AddText (includes inlined loot producers)
    {0x0065E880, 6, {}, false}, // ToolTip::Update (invalidates the appended clock line)
    {0x00642E80, 5, kTopRight, false},  // ModifyMapFigure
    {0x00643DC0, 9, kTopRight, false},  // PlaceUnits
    {0x00644F60, 5, kTopRight, false},  // ShowCurrentMode
    {0x00645000, 5, kTopRight, false},  // InvertClosedState
    // CI3DObjectInfo (portrait window, top left)
    {0x006456E0, 7, kTopLeft, true},    // Init
    {0x00645F80, 5, kTopLeft, false},   // ScrollFigures
    {0x006462A0, 6, kTopLeft, false},   // Draw
    {0x00647FB0, 6, kTopLeft, false},   // OnIdle
    {0x006481D0, 5, kTopLeft, false},   // ShowCurrentMode
    {0x00647EA0, 6, kTopLeft, false},   // ActivateMode
    // CI3DObjectItems (bottom right)
    {0x0063C800, 7, kBottomRight, true},   // constructor
    {0x0063CAE0, 6, kBottomRight, false},  // AddItem
    {0x0063CE10, 6, kBottomRight, false},  // PlaceItem
    {0x0063D2B0, 7, kBottomRight, false},  // SetUnit
    {0x0063CD20, 5, kBottomRight, false},  // ShowSelectItem
    {0x0063D9D0, 7, kBottomRight, false},  // OnIdle
    // CI3DObjectSpells (bottom right)
    {0x0063E2A0, 7, kBottomRight, true},   // constructor
    {0x0063E4B0, 5, kBottomRight, false},  // AddSpell
    {0x0063E820, 7, kBottomRight, false},  // SetUnit (the JMV addon replaces it through its hook framework)
    {0x0063E750, 5, kBottomRight, false},  // ShowSelectSpell
    {0x0063EC90, 6, kBottomRight, false},  // ChangeSpell
    // CI3DObjectSkills (bottom left)
    {0x0063F300, 7, kBottomLeft, true},    // constructor
    {0x0063F500, 5, kBottomLeft, false},   // AddSkill
    {0x0063F800, 6, kBottomLeft, false},   // SetUnit
    {0x0063F730, 5, kBottomLeft, false},   // ShowSelectSkill
    {0x0063FAF0, 5, kBottomLeft, false},   // ChangeSkill(const char*)
    {0x0063FB50, 6, kBottomLeft, false},   // ChangeSkill(int)
    // CI3DObjectWeapons (bottom left)
    {0x0063BAB0, 7, kBottomLeft, true},    // constructor
    {0x0063BCC0, 6, kBottomLeft, false},   // AddWeapon
    {0x0063C050, 6, kBottomLeft, false},   // ShowSelectWeapon
    {0x0063C180, 7, kBottomLeft, false},   // SetUnit
    {0x0063C650, 6, kBottomLeft, false},   // ChangeWeapon
    {0x0063C600, 9, kBottomLeft, false},   // OnIdle
    // CI3DObjectCommands (stance wheel, bottom left)
    {0x00649190, 5, kBottomLeft, true},    // Init
    {0x00649910, 7, kBottomLeft, false},   // SetMode
    {0x00649550, 9, kBottomLeft, false},   // OnIdle
    {0x00649780, 8, kBottomLeft, false},   // ChangePose
    {0x00649A90, 6, kBottomLeft, false},   // SetAggr
    // CI3DObjectClock (bottom right)
    {0x00648520, 5, kBottomRight, true},   // Init
    {0x006488E0, 5, kBottomRight, false},  // OnIdle
    // Party portraits share their frame, bars and input around the bottom centre.
    {0x0063FE70, 7, kBottomCentre, true},  // constructor
    {0x00640060, 5, kBottomCentre, false}, // AddFace
    {0x006407E0, 6, kBottomCentre, false}, // AddFaces
    // OnIdle reads back placed coordinates; do not place them a second time.
    // Generic visual and input methods: static HUD class anchors only.
    {0x005FEB90, 6, {}, false, true},  // Draw (also reached by owned HUD lane calls)
    {0x005FFCC0, 7, {}, false, true, InputScope::area},  // HitTest
    {0x005FFD30, 7, {}, false, true, InputScope::rect},  // IsInside
    {0x005FEBF0, 6, {}, false, true, InputScope::area},  // DrawTips
    // CI3DObject::Draw2D (the objects' 2D surfaces): anchored by the object's class
    {0x005FECC0, 6, {}, false, true},
    {0x005FED00, 5, {}, false, true, InputScope::area}, // Draw2DTips: cursor placement remains physical
    // Mission notifications: child text has a custom Draw and direct scrollbar conversions.
    {0x0065D120, 5, {}, false, true},
    {0x00649E20, 6, {}, false}, // Output layout producer
    {0x00649FF0, 6, {}, false}, // Native slide/arrow update
    {0x0065B240, 5, {}, false, true, InputScope::rect, true},
    {0x0065B340, 8, {}, false, true, InputScope::rect, true},
    {0x0065B1A0, 11, {}, false, true, InputScope::rect, true},
    {0x0065B430, 5, {}, false, true, InputScope::rect, true},
    // CI3DChat (messages and the input line, at the 4:3 content's top left)
    {0x005E2A30, 7, kChat, false},         // NewPhrase
    {0x005E29E0, 10, kChat, false},        // Draw2D
    {0x005E3700, 6, kChat, false},         // RedrawChatPhrase
};
// The HUD widget classes (CBase3D vtables) for CI3DObject::Draw2D.
struct WidgetVtable { std::uintptr_t va; Anchors anchor; };
constexpr WidgetVtable kWidgetVtables[] = {
    {0x00742530, kBottomLeft},   // CI3DObjectCommands
    {0x007425C8, kBottomRight},  // CI3DObjectClock
    {0x00742660, kTopRight},     // CI3DObjectMap
    {0x007426F8, kTopLeft},      // CI3DObjectInfo
    {0x0074279C, kBottomLeft},   // CI3DObjectWeapons
    {0x0074283C, kBottomRight},  // CI3DObjectItems
    {0x007428E4, kBottomRight},  // CI3DObjectSpells
    {0x00742980, kBottomLeft},   // CI3DObjectSkills
    {0x00742A18, kBottomCentre}, // party portraits
};
constexpr std::size_t kWidgetMethodCount = sizeof(kWidgetMethods) / sizeof(kWidgetMethods[0]);
constexpr std::uintptr_t kInfoDrawEntry = 0x006462A0;
// CI3DObject::rect (CRect at +D8: left, top, right, bottom).
constexpr std::size_t kObjectRectOffset = 0xD8;
// CI3DScreenTutorial: mbScript +164, mszTutorial +168 (CString), mnCurrentPage +184.
constexpr std::size_t kTutorialScriptOffset = 0x164;
constexpr std::size_t kTutorialNameOffset = 0x168;
constexpr std::size_t kTutorialPageOffset = 0x184;

// --- State -----------------------------------------------------------------------

std::uint8_t* g_image = nullptr;
std::vector<std::uint8_t> g_file;  // on-disk game.exe; .text/.rdata/.data file offset == RVA
bool g_installed = false;
bool g_surface_known = false;
thread_local bool t_display_world_refresh = false;
WidescreenSettings g_settings{};
MessagePanelSettings g_message_panel{};
std::array<HudPanelSettings, 7> g_hud_panels{};
std::array<HudFadeSettings,kHudPanels.size()> g_hud_fades{};
std::array<float,2> g_corner_scales{{100.f,100.f}};
HudFadeTiming g_hud_fade_timing{};
struct HudFadeState {
    ULONGLONG active_at=0;
    float opacity=1.f,start_opacity=1.f,target_opacity=1.f;
    ULONGLONG transition_at=0;
    bool hovered=false;
    std::vector<std::uint64_t> native_values,content_values;
};
std::array<HudFadeState,kHudPanels.size()> g_hud_fade_state{};
QuestItemSettings g_quest_items{};
std::uint64_t g_hud_fade_generation=0;
void advance_hud_fade(HudFadeState& state,ULONGLONG now) noexcept {
    const int duration=state.target_opacity>state.start_opacity?g_hud_fade_timing.in_ms:g_hud_fade_timing.out_ms;
    const float progress=duration?(std::min)(1.f,float(now-state.transition_at)/float(duration)):1.f;
    const float eased=progress*progress*(3.f-2.f*progress);
    state.opacity=state.start_opacity+(state.target_opacity-state.start_opacity)*eased;
}
void set_hud_fade_target(HudFadeState& state,float target,ULONGLONG now) noexcept {
    if(state.target_opacity==target)return;
    advance_hud_fade(state,now);
    state.start_opacity=state.opacity;state.target_opacity=target;state.transition_at=now;
    if((target>state.start_opacity?g_hud_fade_timing.in_ms:g_hud_fade_timing.out_ms)==0)state.opacity=target;
}
int g_clock_hover_area=-1;
int g_clock_tip_area=-1,g_clock_tip_minute=-1;
void reset_hud_fade_lifetime() noexcept {
    const auto generation=bridge_hud_screen_generation();
    if(generation==g_hud_fade_generation)return;
    g_hud_fade_generation=generation;g_hud_fade_state={};g_clock_hover_area=-1;g_clock_tip_area=g_clock_tip_minute=-1;
    const auto now=GetTickCount64();for(auto& state:g_hud_fade_state)state.active_at=state.transition_at=now;
}
void compare_hud_activity(HudPanel panel,std::vector<std::uint64_t>& previous,const std::vector<std::uint64_t>& values) {
    if(previous==values)return;
    previous=values;widescreen_hud_activity(panel);
}

float g_combat_text_scale = 1.f;
void* g_hud_frame_owner=nullptr;
void (*g_hud_frame_sink)(void*) noexcept=nullptr;
widescreen::Layout g_layout = widescreen::make_layout(800.0f, 600.0f);
float g_object_square = kStockSquare;
float g_landscape_radius = 500.f;
float g_addon_object_radius = 0.f;  // the addon's object draw radius, kept on takeover
float g_addon_landscape_radius = 0.f;  // the addon's landscape draw radius, kept on takeover
bool g_square_owned = false;
bool g_interface_owned = false;
bool (*g_addon_owned)(const void*) = &addon_module_contains;
std::int32_t g_content_width = 800, g_content_height = 600;

std::uint8_t* at(std::uintptr_t va) noexcept { return g_image + (va - kImageBase); }
const std::uint8_t* stock(std::uintptr_t va) noexcept { return g_file.data() + (va - kImageBase); }
bool in_file(std::uintptr_t va, std::size_t size) noexcept {
    return va >= kImageBase + 0x1000 && va - kImageBase + size <= g_file.size();
}
bool stock_bytes(std::uintptr_t va, std::size_t size) noexcept {
    return in_file(va, size) && std::memcmp(at(va), stock(va), size) == 0;
}

// One record per owned write range. Surface-dependent writes update the
// same record; reverse restoration also handles overlapping startup writes.
struct OwnedPatch {
    std::uint8_t* address = nullptr;
    std::size_t size = 0;
    DWORD protection = 0;
    std::uint8_t original[80]{}, current[80]{};
};
std::vector<OwnedPatch> g_patches;

bool raw_write(std::uint8_t* where, const void* bytes, std::size_t size,
               DWORD restore_protection, DWORD& previous, bool& modified) noexcept {
    modified = false;
    if (!VirtualProtect(where, size, PAGE_EXECUTE_READWRITE, &previous)) return false;
    std::memcpy(where, bytes, size);
    modified = true;
    DWORD ignored = 0;
    const bool restored = VirtualProtect(where, size,
        restore_protection ? restore_protection : previous, &ignored) != FALSE;
    const bool flushed = FlushInstructionCache(GetCurrentProcess(), where, size) != FALSE;
    return restored && flushed;
}

bool write_code(std::uint8_t* where, const void* bytes, std::size_t size) noexcept {
    if (!size || size > sizeof(OwnedPatch::original)) return false;
    OwnedPatch* patch = nullptr;
    for (auto& owned : g_patches)
        if (owned.address == where && owned.size == size) { patch = &owned; break; }
    const bool added = !patch;
    if (patch && std::memcmp(where, patch->current, size) != 0) return false;
    if (!patch) {
        OwnedPatch owned{};
        owned.address = where;
        owned.size = size;
        std::memcpy(owned.original, where, size);
        std::memcpy(owned.current, where, size);
        try { g_patches.push_back(owned); } catch (...) { return false; }
        patch = &g_patches.back();
    }
    std::uint8_t previous_bytes[80];
    std::memcpy(previous_bytes, patch->current, size);
    std::memcpy(patch->current, bytes, size);
    DWORD previous = 0;
    bool modified = false;
    const bool result = raw_write(where, bytes, size, patch->protection, previous, modified);
    if (modified && !patch->protection) patch->protection = previous;
    if (!modified) {
        if (added) g_patches.pop_back();
        else std::memcpy(patch->current, previous_bytes, size);
    }
    else owned::bridge_note_owned_patch(where, previous_bytes, size);  // ownership verification
    return result;
}

bool restore_patches() noexcept {
    while (!g_patches.empty()) {
        auto& patch = g_patches.back();
        if (std::memcmp(patch.address, patch.current, patch.size) != 0) return false;
        DWORD previous = 0;
        bool modified = false;
        if (!raw_write(patch.address, patch.original, patch.size,
                       patch.protection, previous, modified)) {
            if (modified) std::memcpy(patch.current, patch.original, patch.size);
            return false;
        }
        g_patches.pop_back();
    }
    return true;
}
std::int32_t relative_to(const std::uint8_t* from_next, const void* target) noexcept {
    return static_cast<std::int32_t>(reinterpret_cast<std::intptr_t>(target) -
                                     reinterpret_cast<std::intptr_t>(from_next));
}
bool write_branch(std::uintptr_t va, std::uint8_t opcode, const void* target, std::size_t fill = 5) noexcept {
    std::uint8_t bytes[16];
    bytes[0] = opcode;
    const std::int32_t relative = relative_to(at(va) + 5, target);
    std::memcpy(bytes + 1, &relative, 4);
    for (std::size_t index = 5; index < fill; ++index) bytes[index] = 0x90;
    return write_code(at(va), bytes, fill);
}
bool write_i32(std::uintptr_t va, std::int32_t value) noexcept { return write_code(at(va), &value, 4); }

void event(const char* name, const char* detail) noexcept { bridge_append_probe_event(name, detail); }

// While taking over the addon's layout, a failed install rolls back to the
// addon's own widescreen; the image is not stretched, so no banner.
bool g_addon_takeover = false;
std::int32_t* g_addon_virtual_shift = nullptr;

void fail(const char* reason) noexcept {
    event("widescreen_failed", reason);
    if (!widescreen::notice && !g_addon_takeover)
        widescreen::notice = "EID3D11: widescreen sites differ - the image may be stretched (see bridge.jsonl)";
}

// Executable code we generate (stubs, trampolines, thunks): written once
// during install, then read/execute.
struct CodeArena {
    std::uint8_t* base = nullptr;
    std::size_t used = 0, size = 0;
    std::uint8_t* take(std::size_t bytes) noexcept {
        if (!base) {
            size = 0x4000;
            base = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
            if (!base) return nullptr;
        }
        used = (used + 15) & ~std::size_t(15);
        if (used + bytes > size) return nullptr;
        std::uint8_t* out = base + used;
        used += bytes;
        return out;
    }
    bool seal() noexcept {
        if (!base) return true;
        DWORD old = 0;
        return VirtualProtect(base, size, PAGE_EXECUTE_READ, &old) &&
            FlushInstructionCache(GetCurrentProcess(), base, size);
    }
};
CodeArena g_arena;

// Replayed stock prologue plus `jmp entry+len`, or a jump to the addon
// hook-framework dispatcher found at the entry. `patch_bytes` is what the
// entry jump may overwrite: the replayed prologue, or only the 5-byte jump
// over the addon's own (possibly shorter) patch, keeping its NOP fill.
void* make_forwarder(std::uintptr_t va, std::size_t prologue, std::size_t& patch_bytes, const char*& reason) noexcept {
    const EntryResolution owner = resolve_entry_owner(at(va), stock(va), 16, 5, g_addon_owned);
    if (owner.owner == EntryOwner::rejected) { reason = owner.reason; return nullptr; }
    const std::size_t copied = owner.owner == EntryOwner::stock ? prologue : 0;
    patch_bytes = copied ? prologue : 5;
    std::uint8_t* code = g_arena.take(copied + 5);
    if (!code) { reason = "code_arena_full"; return nullptr; }
    if (copied) std::memcpy(code, at(va), copied);
    code[copied] = 0xe9;
    const void* target = copied ? static_cast<const void*>(at(va) + prologue) : owner.dispatcher;
    const std::int32_t relative = relative_to(code + copied + 5, target);
    std::memcpy(code + copied + 1, &relative, 4);
    return code;
}

// --- World -----------------------------------------------------------------------

void __fastcall clip_scale_matrix(void*, void*, float* matrix) {
    widescreen::Layout layout;
    auto basis=widescreen::ProjectionBasis::stock_aspect;
    if (g_surface_known && _ReturnAddress() == at(kClipScaleCreateReturn)) {
        layout=g_layout;
    } else if (g_surface_known && t_display_world_refresh && _ReturnAddress() == at(0x005016E5)) {
        layout=g_layout;
        basis=widescreen::ProjectionBasis::actual_rect;
    }
    widescreen::make_clip_scale_matrix(matrix,layout,basis);
}

// --- Addon widescreen takeover -------------------------------------------------------
//
// The supported addon (JMV, Dejavu) applies a 4:3-centred widescreen at
// WinMain, before DirectDraw exists, as one all-or-nothing set of game.exe
// writes. Its other features use separate entry hooks, which stay and are
// chained. The set below is every game site that layout writes, with the
// exact form each write takes; values derived from the resolution are holes
// (patterns) or must agree on one virtual shift (shift sites). Game sites
// only: nothing in addon.dll is located by address. Each recognised site is
// returned to stock bytes as an owned patch, so uninstall restores the
// addon's bytes. One unrecognised or missing write leaves the addon layout.
enum class AddonWrite : std::uint8_t {
    jump,        // E9 into addon.dll
    pattern,     // fixed bytes, ?? = resolution-dependent
    shift_i32,   // base + sign * virtual shift
    shift_f32,
    operand,     // u32: the named game address, or an address in addon.dll
    tutorial,    // code hook: E9 wrapper, NOP fill; wrapper calls into addon.dll
    value,       // resolution value or optional pad: any bytes, not counted
    radius_f32,  // draw-radius push immediate: finite positive float, not counted
    radius_ptr,  // draw-radius operand: address in addon.dll, not counted
};
struct AddonSite { std::uintptr_t va; std::uint8_t size; AddonWrite kind; std::int32_t base; std::int8_t sign; const char* pattern; };
constexpr AddonSite J(std::uintptr_t va) { return {va, 5, AddonWrite::jump, 0, 0, nullptr}; }
constexpr AddonSite P(std::uintptr_t va, std::uint8_t size, const char* pattern) { return {va, size, AddonWrite::pattern, 0, 0, pattern}; }
constexpr AddonSite I(std::uintptr_t va, std::int32_t base, std::int8_t sign) { return {va, 4, AddonWrite::shift_i32, base, sign, nullptr}; }
constexpr AddonSite F(std::uintptr_t va, std::int32_t base, std::int8_t sign) { return {va, 4, AddonWrite::shift_f32, base, sign, nullptr}; }
constexpr AddonSite O(std::uintptr_t va, std::uintptr_t game) { return {va, 4, AddonWrite::operand, static_cast<std::int32_t>(game), 0, nullptr}; }
constexpr AddonSite V(std::uintptr_t va) { return {va, 4, AddonWrite::value, 0, 0, nullptr}; }
constexpr const char* kLocalLoad = "90 90 90 90 31 C9";
// The shift is read from the minimap click-area right edge (800 + shift).
constexpr std::uintptr_t kAddonShiftSite = 0x00641F61;
constexpr AddonSite kAddonLayoutSites[] = {
    // Conversion and layout ingress entries.
    J(0x005FC9E0), J(0x005E9E77), J(0x005E7C8D), J(0x005E75D1), J(0x005E835E), J(0x005FCA70),
    J(0x005E6FA0), J(0x005F7CA0), J(0x005F7D20), J(0x005F7D60),
    // Surface-local caret, header, tooltip and info-window clip loads.
    P(0x0065C51F, 6, kLocalLoad), P(0x0065C544, 6, kLocalLoad), P(0x0061DB0E, 6, kLocalLoad),
    P(0x0061DB3E, 6, kLocalLoad), P(0x006569F7, 6, kLocalLoad), P(0x00656A08, 6, kLocalLoad),
    P(0x005E3839, 6, kLocalLoad), P(0x005E386C, 6, kLocalLoad), P(0x0065E77E, 6, kLocalLoad),
    P(0x0064644C, 6, kLocalLoad), P(0x0064648E, 6, kLocalLoad), P(0x00646499, 6, kLocalLoad),
    // Tooltip right clamp.
    I(0x0065E7C3, 800, 1), I(0x0065E7CA, -800, -1),
    // Minimap.
    J(0x00641F20), J(0x0064291F), J(0x00642930), J(0x00642B09),
    I(0x00641F51, 615, 1), I(kAddonShiftSite, 800, 1), I(0x00642AFC, 700, 1),
    F(0x00642D23, 713, 1), F(0x00642D7F, 850, 1), I(0x006452ED, -700, -1), I(0x00645432, -700, -1),
    // Info window.
    J(0x00645719), J(0x00645F76), J(0x00645F80), J(0x00646299), J(0x006462A0), J(0x00646B1D),
    I(0x00646608, 100, -1), F(0x006480EB, -50, -1), F(0x00648147, 87, -1), I(0x00646251, 20, -1),
    // Items.
    I(0x0063C880, 520, 1), I(0x0063C894, 720, 1), J(0x0063C800), J(0x0063CA2F),
    F(0x007428AC, 695, 1), F(0x0063CC13, 680, 1), F(0x0063CC07, 720, 1),
    // Skills.
    J(0x0063F378), J(0x0063F452), I(0x0063F38E, 40, -1), F(0x0063F633, 20, -1),
    // Spells.
    I(0x0063E306, 760, 1), I(0x0063E31A, 800, 1), J(0x0063E4B0), J(0x0063E6F7),
    // Weapons: bar bounds live either in a game code pad or in addon.dll.
    I(0x0063BB2B, 240, -1), I(0x0063BAFC, 80, -1), V(0x005FC9F4), V(0x005FC9F8),
    O(0x0063BF80, 0x005FC9F4), O(0x0063BF71, 0x005FC9F8),
    J(0x0063C050), J(0x0063C0E3), J(0x0063C16C),
    // Stance wheel.
    J(0x00649190), J(0x00649541), I(0x0064919F, 0, -1), I(0x006491B3, 80, -1),
    J(0x00649910), J(0x00649A85),
    // Clock.
    J(0x00648520), J(0x006488D7), I(0x00648531, 720, 1), I(0x00648545, 800, 1), F(0x00742B08, 800, 1),
    // Global map hit surface: shifted GetPixel entry and its two callers.
    P(0x005FD237, 9, "2D ?? ?? ?? ?? 89 44 24 04"), P(0x0063023C, 4, "F7 CF FC FF"),
    P(0x0062FB20, 4, "13 D7 FC FF"),
    // Draw2D full-screen surface (absolute rect; height; width).
    P(0x005F546A, 1, "00"), V(0x005F546E), V(0x005F5473),
    P(0x005F5568, 1, "00"), V(0x005F556C), V(0x005F5571),
    // TextSize inline ScreenToVirtual (effective width, height).
    P(0x005F83CF, 80, "8B 74 24 30 C7 44 24 28 FF FF FF FF B8 20 03 00 00 F7 EB B9 ?? ?? ?? ?? F7 F9 89 06 "
                      "B8 58 02 00 00 F7 ED B9 ?? ?? ?? ?? F7 F9 89 46 04 8D 4C 24 34 E8 6B 85 10 00 "
                      "8B 4C 24 20 89 F0 5F 5E 5D 5B 64 89 0D 00 00 00 00 83 C4 1C C2 0C 00 90 90 90"),
    // Font divisor operand (a game code pad or addon.dll).
    P(0x005F4A4F, 2, "D8 0D"), O(0x005F4A51, 0x005F4F81), V(0x005F4F81),
    // Clip-scale matrix _11 in the function epilogue.
    P(0x004F135A, 19, "C7 00 ?? ?? ?? ?? C7 40 14 00 00 80 BF 8B E5 5D C2 04 00"),
    // DrawText surface-local conversion.
    P(0x005FDE94, 3, "31 C9 90"),
    // ScreenToVirtual rect and point bodies (shift, effective width, height).
    P(0x005F7D9E, 98, "31 C9 55 53 56 8B 74 24 10 8B 06 85 C9 74 05 2D ?? ?? ?? ?? BB 20 03 00 00 F7 EB BD "
                      "?? ?? ?? ?? F7 FD 89 06 8B 46 08 85 C9 74 05 2D ?? ?? ?? ?? F7 EB F7 FD 89 46 08 8B "
                      "46 04 BB 58 02 00 00 F7 EB BD ?? ?? ?? ?? F7 FD 89 46 04 8B 46 0C F7 EB F7 FD 89 46 "
                      "0C 5E 5B 5D C2 04 00 90 90 90 90 90 31 C9"),
    P(0x005F7E00, 96, "51 56 8B 74 24 0C 8B 06 85 C9 0F 84 05 00 00 00 2D ?? ?? ?? ?? B9 20 03 00 00 F7 E9 "
                      "B9 ?? ?? ?? ?? F7 F9 89 06 E9 0B 00 00 00 90 90 90 90 31 C9 E9 CB FF FF FF B8 58 02 "
                      "00 00 F7 6E 04 B9 ?? ?? ?? ?? F7 F9 89 46 04 5E 59 C2 04 00 90 90 90 90 90 90 90 90 "
                      "90 90 90 90 90 90 90 90 90 90 90 90"),
    // Unshifted cursor-delta conversions (camera, global map).
    P(0x005EB249, 5, "E8 B0 CB 00 00"), P(0x0063008C, 5, "E8 6D 7D FC FF"),
    // Tutorial page rectangles.
    {0x00659994, 7, AddonWrite::tutorial, 0, 0, nullptr},
    // Addon draw-radius settings, superseded by the owned draw square.
    {0x0056A02D, 4, AddonWrite::radius_f32, 0, 0, nullptr}, {0x0056A0B9, 4, AddonWrite::radius_f32, 0, 0, nullptr},
    {0x0056B0EA, 4, AddonWrite::radius_ptr, 0, 0, nullptr}, {0x0056B13C, 4, AddonWrite::radius_ptr, 0, 0, nullptr},
    {0x0056B17A, 4, AddonWrite::radius_ptr, 0, 0, nullptr}, {0x0056B1E1, 4, AddonWrite::radius_ptr, 0, 0, nullptr},
};

bool counted(AddonWrite kind) noexcept {
    return kind != AddonWrite::value && kind != AddonWrite::radius_f32 && kind != AddonWrite::radius_ptr;
}
int hex_digit(char c) noexcept {
    return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}
bool pattern_matches(const std::uint8_t* bytes, std::size_t size, const char* pattern) noexcept {
    std::size_t index = 0;
    for (const char* p = pattern; *p;) {
        if (*p == ' ') { ++p; continue; }
        if (index >= size || !p[1]) return false;
        if (p[0] != '?') {
            const int high = hex_digit(p[0]), low = hex_digit(p[1]);
            if (high < 0 || low < 0 || bytes[index] != high * 16 + low) return false;
        }
        ++index;
        p += 2;
    }
    return index == size;
}
std::int32_t read_i32(const std::uint8_t* bytes) noexcept { std::int32_t v; std::memcpy(&v, bytes, 4); return v; }
float read_f32(const std::uint8_t* bytes) noexcept { float v; std::memcpy(&v, bytes, 4); return v; }
bool in_addon(std::uint32_t address) noexcept {
    return address && g_addon_owned && g_addon_owned(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(address)));
}
// Committed, readable memory wholly inside one region.
bool copy_committed(void* out, const void* from, std::size_t size) noexcept {
    MEMORY_BASIC_INFORMATION memory{};
    if (!VirtualQuery(from, &memory, sizeof(memory)) || memory.State != MEM_COMMIT || !memory.Protect ||
        (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
        return false;
    const auto* first = static_cast<const std::uint8_t*>(from);
    if (first + size > static_cast<const std::uint8_t*>(memory.BaseAddress) + memory.RegionSize) return false;
    std::memcpy(out, from, size);
    return true;
}
// The code-hook wrapper: pushad; pushfd; push return; mov ebp, esp; push ebp;
// push argument; call callback. The site holds jmp wrapper plus NOP fill.
bool addon_tutorial_hook(const std::uint8_t* site, std::uintptr_t va, std::size_t size) noexcept {
    if (site[0] != 0xe9) return false;
    for (std::size_t index = 5; index < size; ++index) if (site[index] != 0x90) return false;
    const std::uint8_t* wrapper = site + 5 + read_i32(site + 1);
    std::uint8_t code[20];
    if (!copy_committed(code, wrapper, sizeof(code))) return false;
    const std::uint32_t resume = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(at(va + size)));
    if (code[0] != 0x60 || code[1] != 0x9c || code[2] != 0x68 ||
        static_cast<std::uint32_t>(read_i32(code + 3)) != resume ||
        code[7] != 0x89 || code[8] != 0xe5 || code[9] != 0x55 || code[10] != 0x68 || code[15] != 0xe8)
        return false;
    const auto callback = reinterpret_cast<std::uintptr_t>(wrapper + 20 + read_i32(code + 16));
    return in_addon(static_cast<std::uint32_t>(callback));
}
std::uint32_t game_address(std::uintptr_t va) noexcept {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(at(va)));
}
bool addon_write_matches(const AddonSite& site, std::int32_t shift) noexcept {
    const std::uint8_t* bytes = at(site.va);
    switch (site.kind) {
    case AddonWrite::jump: return bytes[0] == 0xe9 &&
        in_addon(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(bytes + 5 + read_i32(bytes + 1))));
    case AddonWrite::pattern: return pattern_matches(bytes, site.size, site.pattern);
    case AddonWrite::shift_i32: return read_i32(bytes) == site.base + site.sign * shift;
    case AddonWrite::shift_f32: return read_f32(bytes) == static_cast<float>(site.base + site.sign * shift);
    case AddonWrite::operand: {
        const auto address = static_cast<std::uint32_t>(read_i32(bytes));
        return address == game_address(static_cast<std::uintptr_t>(site.base)) || in_addon(address);
    }
    case AddonWrite::tutorial: return addon_tutorial_hook(bytes, site.va, site.size);
    case AddonWrite::value: return true;
    case AddonWrite::radius_f32: {
        const float radius = read_f32(bytes);
        return at(site.va - 1)[0] == 0x68 && std::isfinite(radius) && radius > 0.f;
    }
    case AddonWrite::radius_ptr: return in_addon(static_cast<std::uint32_t>(read_i32(bytes)));
    }
    return false;
}

// The addon's virtual-shift global, read from the code its info-window draw
// jump enters (`mov eax,[shift]; neg eax; mov [current],eax; push 6462ACh;
// ret`, the stub the engine bridge's addon discovery also verifies). Only
// accepted when it lies in addon.dll and holds the shift the layout used.
std::int32_t* find_addon_virtual_shift(std::int32_t shift) noexcept {
    const std::uint8_t* entry = at(kInfoDrawEntry);
    if (entry[0] != 0xe9) return nullptr;
    const std::uint8_t* stub = entry + 5 + read_i32(entry + 1);
    std::uint8_t code[30];
    if (!in_addon(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(stub))) ||
        !copy_committed(code, stub, sizeof(code)) ||
        !pattern_matches(code, sizeof(code), "81 EC 00 01 00 00 53 8B D9 56 33 F6 A1 ?? ?? ?? ?? F7 D8 A3 ?? ?? ?? ?? 68 AC 62 64 00 C3"))
        return nullptr;
    const auto address = static_cast<std::uint32_t>(read_i32(code + 13));
    std::int32_t value = 0;
    if (!in_addon(address) || !copy_committed(&value, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(address)), sizeof(value)) ||
        value != shift)
        return nullptr;
    return reinterpret_cast<std::int32_t*>(static_cast<std::uintptr_t>(address));
}

// Recognise the addon's layout writes; on success return the number of sites
// to restore (0 = nothing of the addon's layout is present).
std::size_t plan_addon_takeover(std::int32_t& shift, const char*& reason, char* detail, std::size_t detail_size) noexcept {
    shift = 0;
    reason = nullptr;
    std::size_t total = 0, applied = 0, restore = 0;
    std::uintptr_t first_stock = 0;
    for (const AddonSite& site : kAddonLayoutSites) {
        if (!in_file(site.va, site.size)) { reason = "addon_site_outside_image"; return 0; }
        if (counted(site.kind)) ++total;
        if (stock_bytes(site.va, site.size)) { if (counted(site.kind) && !first_stock) first_stock = site.va; continue; }
        if (counted(site.kind)) ++applied;
    }
    if (!applied) {
        // No addon layout. Only the draw-radius writes may still be recognised.
        for (const AddonSite& site : kAddonLayoutSites)
            if (!counted(site.kind) && site.kind != AddonWrite::value && !stock_bytes(site.va, site.size) &&
                g_addon_owned && addon_write_matches(site, 0))
                ++restore;
        return restore;
    }
    // Foreign bytes without one addon layout jump are not the addon's: the
    // stock-port checks below reject them as before.
    bool addon_evidence = false;
    for (const AddonSite& site : kAddonLayoutSites)
        addon_evidence = addon_evidence || (site.kind == AddonWrite::jump && g_addon_owned &&
                                            !stock_bytes(site.va, site.size) && addon_write_matches(site, 0));
    if (!addon_evidence) return 0;
    // Addon builds differ in which sites they patch (JMV 1.35 and the
    // 2026-10-08 Dejavu each leave one stock); a stock site needs no restore.
    (void)total;
    (void)first_stock;
    shift = read_i32(at(kAddonShiftSite)) - 800;
    if (shift <= 0 || shift > 4096) { reason = "addon_virtual_shift_invalid"; return 0; }
    g_addon_virtual_shift = find_addon_virtual_shift(shift);
    for (const AddonSite& site : kAddonLayoutSites) {
        if (stock_bytes(site.va, site.size)) continue;
        if (!addon_write_matches(site, shift)) {
            if (!counted(site.kind)) continue;  // draw radius: left to the draw-square check
            // A jump into allocated memory is a hook-framework trampoline for an
            // addon feature (JMV 1.35 AddSpell), not layout: leave it for the
            // interface install to chain, which rolls everything back if it can't.
            if (site.kind == AddonWrite::jump && at(site.va)[0] == 0xe9) {
                const void* target = at(site.va) + 5 + read_i32(at(site.va) + 1);
                HMODULE module = nullptr;
                MEMORY_BASIC_INFORMATION memory{};
                if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                        static_cast<LPCWSTR>(target), &module) &&
                    VirtualQuery(target, &memory, sizeof(memory)) && memory.State == MEM_COMMIT &&
                    (memory.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)))
                    continue;
            }
            const std::uint8_t* b = at(site.va);
            std::snprintf(detail, detail_size, "addon_layout_unrecognised at=%08X bytes=%02X%02X%02X%02X%02X target=%08X",
                          static_cast<unsigned>(site.va), b[0], b[1], b[2], b[3], b[4],
                          static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(b + 5 + read_i32(b + 1))));
            reason = detail;
            return 0;
        }
        ++restore;
    }
    return restore;
}

// Stock bytes back at every recognised site, as owned patches.
bool restore_addon_layout(std::int32_t shift) noexcept {
    // The addon's spell bar builder keeps reading its shift after the layout
    // is gone (extra right-edge width on every spell area): run it unshifted,
    // as on a 4:3 screen. Owned, so uninstall restores the addon's value.
    if (shift && g_addon_virtual_shift) {
        const std::int32_t unshifted = 0;
        if (!write_code(reinterpret_cast<std::uint8_t*>(g_addon_virtual_shift), &unshifted, sizeof(unshifted))) return false;
    }
    for (const AddonSite& site : kAddonLayoutSites) {
        if (stock_bytes(site.va, site.size) || !addon_write_matches(site, shift)) continue;
        // Without the addon layout only its draw-radius writes are restored.
        if (shift == 0 && (counted(site.kind) || site.kind == AddonWrite::value)) continue;
        // Keep the addon's configured object radius: the owned draw square
        // replaces its pointer but not the distance it asked for.
        if (site.kind == AddonWrite::radius_ptr) {
            float radius = 0.f;
            const auto* source = reinterpret_cast<const void*>(static_cast<std::uintptr_t>(read_i32(at(site.va))));
            if (copy_committed(&radius, source, sizeof(radius)) && std::isfinite(radius) && radius > 0.f && radius <= 10000.f)
                g_addon_object_radius = (std::max)(g_addon_object_radius, radius);
        }
        if (site.kind == AddonWrite::radius_f32) {
            const float radius = read_f32(at(site.va));
            if (radius <= 10000.f) g_addon_landscape_radius = (std::max)(g_addon_landscape_radius, radius);
        }
        for (std::size_t offset = 0; offset < site.size; offset += sizeof(OwnedPatch::original)) {
            const std::size_t chunk = (std::min)(site.size - offset, sizeof(OwnedPatch::original));
            if (!write_code(at(site.va + offset), stock(site.va + offset), chunk)) return false;
        }
    }
    return true;
}

bool install_world() noexcept {
    if (!stock_bytes(kClipScaleEntry, 0x3d)) return false;
    if (!write_branch(kClipScaleEntry, 0xe9, reinterpret_cast<const void*>(&clip_scale_matrix))) return false;
    event("widescreen_world_installed", "stock_vanilla");
    return true;
}

// The draw square follows the real surface aspect, after checking its
// stock operands once. Later writes must still match our owned bytes.
bool apply_draw_square() noexcept {
    const float factor = widescreen::draw_square_factor(g_layout.width, g_layout.height);
    bool terrain_stock = true, objects_stock = true;
    for (const auto va : kTerrainSquarePush) terrain_stock = terrain_stock && (g_square_owned || stock_bytes(va, 5));
    for (const auto va : kObjectSquareOperand) objects_stock = objects_stock && (g_square_owned || stock_bytes(va, 4));
    if (!terrain_stock || !objects_stock) {
        fail("draw_square_foreign");
        return false;
    }
    const float terrain_square = (std::max)(g_landscape_radius, g_addon_landscape_radius) * factor;
    const float object_radius = (std::max)(kStockSquare, g_addon_object_radius);
    g_object_square = object_radius * factor;
    for (const auto va : kTerrainSquarePush)
        if (!write_code(at(va) + 1, &terrain_square, sizeof(terrain_square))) return false;
    const auto pointer = g_object_square != kStockSquare
        ? static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&g_object_square))
        : static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(at(kStockSquareConstant)));
    for (const auto va : kObjectSquareOperand)
        if (!write_code(at(va), &pointer, sizeof(pointer))) return false;
    g_square_owned = true;
    char detail[48];
    std::snprintf(detail, sizeof(detail), "terrain=%.1f objects=%.1f", terrain_square,g_object_square);
    event("widescreen_draw_square", detail);
    return true;
}

// --- Interface mapping -------------------------------------------------------------

// Shipped current-screen membership, not retained pointer identity. Modal screens
// replace the mission's drawing/input role even while that mission stays alive.
bool mission_message_panel(const std::uint8_t* screen,const void*& output,const void*& text) noexcept {
    output=text=nullptr;
    if(!screen || *reinterpret_cast<void* const*>(screen)!=at(0x00740764))return false;
    const auto* panel=*reinterpret_cast<const std::uint8_t* const*>(screen+0x1BC);
    const auto* child=*reinterpret_cast<const std::uint8_t* const*>(screen+0x1B8);
    if(!panel || !child || *reinterpret_cast<void* const*>(panel)!=at(0x00742498) ||
        *reinterpret_cast<void* const*>(child)!=at(0x00742E9C) ||
        *reinterpret_cast<const void* const*>(panel+0x148)!=screen ||
        !*reinterpret_cast<const int*>(panel+0xE8))return false;
    output=panel;text=child;return true;
}
// Runtime evidence (c72 exception log): every fault reading +0x1BC/+0x1B8 of
// the current mission screen came from widgets entered inside its OnActivate,
// before those members hold pointers. No message panel exists until it returns.
bool g_mission_activating = false;
bool current_message_panel(const void*& output, const void*& text) noexcept {
    output = text = nullptr;
    if (!g_interface_owned || !g_image || g_mission_activating) return false;
    __try {
        const auto* ui_interface = at(0x007C7D50);
        if (!ui_interface[0x30]) return false;
        auto* begin = *reinterpret_cast<void* const* const*>(ui_interface + 0x34);
        auto* end = *reinterpret_cast<void* const* const*>(ui_interface + 0x38);
        if (begin == end) return false;
        const auto* screen = static_cast<const std::uint8_t*>(end[-1]);
        return mission_message_panel(screen,output,text);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

struct PanelRole { unsigned member; std::uintptr_t vtable; Anchors anchor; };
constexpr std::array<PanelRole,11> kPanelRoles{{
    {0x19C,0x007428E4,kBottomRight}, {0x1A0,0x00742980,kBottomLeft},
    {0x1AC,0x00742660,kTopRight}, {0x1A8,0x007426F8,kTopLeft},
    {0x1A4,0x00742A18,kBottomCentre}, {0x194,0x0074279C,kBottomLeft},
    {0x198,0x0074283C,kBottomRight}, {0,0,{}}, {0,0,kBottomRight},
    {0x1B4,0x00742530,kBottomLeft}, {0x1B0,0x007425C8,kBottomRight}
}};
const std::uint8_t* mission_panel(unsigned index,const std::uint8_t* screen) noexcept {
    if(index>=kPanelRoles.size() || !kPanelRoles[index].member || !screen ||
        *reinterpret_cast<void* const*>(screen)!=at(0x00740764))return nullptr;
    const auto& role=kPanelRoles[index];
    const auto* object=*reinterpret_cast<const std::uint8_t* const*>(screen+role.member);
    if(!object || *reinterpret_cast<void* const*>(object)!=at(role.vtable) ||
        *reinterpret_cast<const void* const*>(object+0x148)!=screen ||
        !*reinterpret_cast<const int*>(object+0xE8))return nullptr;
    return object;
}
const std::uint8_t* current_panel(unsigned index) noexcept {
    // Not during the mission screen's OnActivate (as current_message_panel).
    if(!g_interface_owned || !g_image || g_mission_activating || index>=kPanelRoles.size() || !kPanelRoles[index].member)return nullptr;
    __try {
        const auto* ui_interface=at(0x007C7D50);
        if(!ui_interface[0x30])return nullptr;
        auto* begin=*reinterpret_cast<void* const* const*>(ui_interface+0x34);
        auto* end=*reinterpret_cast<void* const* const*>(ui_interface+0x38);
        if(begin==end)return nullptr;
        const auto* screen=static_cast<const std::uint8_t*>(end[-1]);
        return mission_panel(index,screen);
    } __except(EXCEPTION_EXECUTE_HANDLER){return nullptr;}
}
const std::uint8_t* current_clock_tip() noexcept {
    const auto* clock=current_panel(unsigned(HudPanel::game_speed));
    if(!clock || g_clock_hover_area<0)return nullptr;
    __try {   // game memory, as the neighbouring readers
        if(unsigned(g_clock_hover_area)>=*reinterpret_cast<const unsigned*>(clock+0x12C))return nullptr;
        const auto* area=(*reinterpret_cast<const std::uint8_t* const* const*>(clock+0x128))[g_clock_hover_area];
        const auto* tip=*reinterpret_cast<const std::uint8_t* const*>(area+0x30);
        if(!tip || *reinterpret_cast<const void* const*>(tip+0x170)!=area)return nullptr;
        const int mode=*reinterpret_cast<const int*>(tip+0x180);
        return mode>=2 && mode<=4?tip:nullptr;
    } __except(EXCEPTION_EXECUTE_HANDLER){return nullptr;}
}
int panel_index(const void* object) noexcept {
    if(!object)return -1;
    __try {
        const auto vtable=*reinterpret_cast<void* const*>(object);
        for(unsigned i=0;i<kPanelRoles.size();++i)
            if(vtable==at(kPanelRoles[i].vtable) && current_panel(i)==object)return int(i);
    } __except(EXCEPTION_EXECUTE_HANDLER){}
    return -1;
}
enum class DockSide { left,right,top,bottom,floating };
// A panel rect is stored in whole placed units (adjust_rect rounds), so a
// panel laid out against a screen edge can sit up to half a unit (times the
// UI scale) inside it: at 3840x2160 the default top-left panel lands 1.2 px
// in. Edge docking tolerates one placed unit, not half a pixel.
float edge_slack() noexcept { return (std::max)(.5f,g_layout.unit); }
DockSide stock_header_side(HudPanel panel) noexcept {
    return panel==HudPanel::minimap?DockSide::right:panel==HudPanel::character_info?DockSide::left:DockSide::top;
}
int button_side(HudPanel panel) noexcept {return panel==HudPanel::messages?g_message_panel.buttons_side:g_hud_panels[unsigned(panel)].buttons_side;}
float border_padding(HudPanel panel) noexcept {
    return (panel==HudPanel::messages?g_message_panel.border_padding:g_hud_panels[unsigned(panel)].border_padding)*g_layout.unit*g_layout.scale;
}
float border_inset(HudPanel panel) noexcept {return (std::max)(0.f,border_padding(panel));}
float button_multiplier(HudPanel panel) noexcept {return (panel==HudPanel::messages?g_message_panel.buttons_scale_percent:g_hud_panels[unsigned(panel)].buttons_scale_percent)/100.f;}
DockSide panel_header_side(unsigned index) noexcept {
    const auto panel=HudPanel(index);const auto& s=g_hud_panels[index];const int side=s.buttons_side;
    if(side==1 || (side==0 && s.x_percent<0.f && s.y_percent<0.f))return stock_header_side(panel);
    if(side>=2)return DockSide(side-2);
    if(s.x_percent<0.f)return stock_header_side(panel);
    if(s.x_percent<=.01f)return DockSide::left;
    if(s.x_percent>=99.99f)return DockSide::right;
    if(s.y_percent>=99.99f)return DockSide::bottom;
    if(s.y_percent<=.01f)return DockSide::top;
    return DockSide::floating;
}
bool slot_panel(unsigned panel) noexcept {return panel<2 || panel==4 || panel==5 || panel==6;}
unsigned native_slot_count(unsigned panel,const void* object) noexcept {
    const auto* b=static_cast<const std::uint8_t*>(object);
    const unsigned areas=*reinterpret_cast<const unsigned*>(b+0x12C);
    if(panel<2)return (std::min)(areas,*reinterpret_cast<const unsigned*>(b+0x14C));
    if(panel==4)return areas;
    if(panel==5)return (std::min)(areas,*reinterpret_cast<const unsigned*>(b+0x10C));
    // Items: areas 0-3 are the quick items; 4-7 the quest items, laid out as
    // their own widget (quest_slot_bounds).
    unsigned count=0;const auto* mapping=reinterpret_cast<const int*>(b+0x16C);
    for(unsigned i=0;i<(std::min)(areas,4u);++i)if(mapping[i]>=0)count=i+1;
    return count;
}
bool active_slot(unsigned panel,unsigned slot,const void* object) noexcept {
    const auto* b=static_cast<const std::uint8_t*>(object);
    if(panel==6?slot>=(std::min)(*reinterpret_cast<const unsigned*>(b+0x12C),8u):slot>=native_slot_count(panel,object))return false;
    if(panel!=6)return true;
    const int index=reinterpret_cast<const int*>(b+0x16C)[slot];
    return index>=0 && unsigned(index)<*reinterpret_cast<const unsigned*>(b+0x10C);
}
unsigned layout_slot_count(unsigned panel,const void* object) noexcept {
    const auto& s=g_hud_panels[panel];const unsigned count=native_slot_count(panel,object);
    return s.preview && game_hud_editing()?(std::max)(count,panel==6?(std::min)(s.preview_count,4u):s.preview_count):count;
}
void slot_dimensions(unsigned panel,unsigned slot,const void* object,float& width,float& height) noexcept {
    const auto* b=static_cast<const std::uint8_t*>(object);const unsigned areas=*reinterpret_cast<const unsigned*>(b+0x12C);
    if(panel==5){width=40.f;height=80.f;return;}
    if(areas) {
        const auto* array=*reinterpret_cast<const std::uint8_t* const* const*>(b+0x128);
        const auto r=*reinterpret_cast<const RECT*>(array[slot<areas?slot:0]+0x14);
        width=float(r.right-r.left);height=float(r.bottom-r.top);return;
    }
    width=panel==4?56.f:panel==6?50.f:40.f;height=panel==4?90.f:panel==5?80.f:panel==6?50.f:40.f;
}
int slot_direction(unsigned panel) noexcept {
    const int d=g_hud_panels[panel].direction;
    return panel!=4?d:d==1?1:d==2?0:d==4?3:2;
}
// Slots per line (column for vertical bars, row for horizontal ones); 0 = one line.
unsigned slots_per_line(unsigned panel) noexcept {return panel==4?0u:g_hud_panels[panel].slots_per_line;}
// Further lines grow away from the anchored edge: vertical bars toward the
// screen interior across x, horizontal bars up from the bottom (or down).
bool lines_grow_negative(unsigned panel) noexcept {
    const auto anchor=kPanelRoles[panel].anchor;
    return slot_direction(panel)>=2?anchor.y==Anchor::end:anchor.x==Anchor::end;
}
void slot_extent(unsigned panel,const void* object,float& width,float& height) noexcept {
    const unsigned count=layout_slot_count(panel,object);width=height=0.f;
    const bool horizontal=slot_direction(panel)>=2;const unsigned per=slots_per_line(panel);
    float length=0.f,line=0.f,cross=0.f;unsigned lines=count?1u:0u;
    for(unsigned i=0;i<count;++i) {float w,h;slot_dimensions(panel,i,object,w,h);
        if(per && i && i%per==0){length=(std::max)(length,line);line=0.f;++lines;}
        const float size=horizontal?w:h;
        // Signed spacing may overlap slots, but never reverse their order.
        const bool line_end=i+1==count || (per && (i+1)%per==0);
        line+=line_end?size:(std::max)(1.f,size+g_hud_panels[panel].padding);
        cross=(std::max)(cross,horizontal?h:w);
    }
    length=(std::max)(length,line);
    const float across=lines?cross+float(lines-1)*(std::max)(1.f,cross+g_hud_panels[panel].padding):0.f;
    if(horizontal){width=length;height=across;}else{height=length;width=across;}
}
RECT panel_source_rect(unsigned index,const void* object) noexcept {
    const auto* bytes=static_cast<const std::uint8_t*>(object);
    RECT r=*reinterpret_cast<const RECT*>(bytes+kObjectRectOffset);
    if(index!=4)return r;
    const unsigned count=*reinterpret_cast<const unsigned*>(bytes+0x12C);
    const auto* areas=*reinterpret_cast<const std::uint8_t* const* const*>(bytes+0x128);
    if(count) {
        r=*reinterpret_cast<const RECT*>(areas[0]+0x14);
        for(unsigned i=1;i<count;++i){const auto a=*reinterpret_cast<const RECT*>(areas[i]+0x14);
            r.left=(std::min)(r.left,a.left);r.top=(std::min)(r.top,a.top);
            r.right=(std::max)(r.right,a.right);r.bottom=(std::max)(r.bottom,a.bottom);}
    }
    return r;
}
RECT panel_native_rect(unsigned index,const void* object) noexcept {
    RECT r=panel_source_rect(index,object);
    if(index==unsigned(HudPanel::minimap) || index==unsigned(HudPanel::character_info)) {
        const auto side=panel_header_side(index);
        const float buttons=button_multiplier(HudPanel(index));const bool vertical=side==DockSide::left || side==DockSide::right;
        const float count=index==unsigned(HudPanel::minimap)?4.f:5.f;
        const LONG content_height=index==unsigned(HudPanel::minimap)?165:225;
        const float gap=(std::max)(0.f,g_hud_panels[index].border_padding)/(g_hud_panels[index].scale_percent/100.f);
        // Padding surrounds the content only; the button strip adds its own size (#186).
        r.right=r.left+(std::max)(1L,std::lround(vertical?165.f+2.f*gap+20.f*buttons:(std::max)(165.f+2.f*gap,count*20.f*buttons)));
        r.bottom=r.top+(std::max)(1L,std::lround(vertical?(std::max)(float(content_height)+2.f*gap,count*30.f*buttons):float(content_height)+2.f*gap));
        return r;
    }
    if(!slot_panel(index))return r;
    float width,height;slot_extent(index,object,width,height);
    if(game_hud_editing() && (width<=0.f || height<=0.f))slot_dimensions(index,0,object,width,height);
    if(width>0.f && height>0.f){r.right=r.left+std::lround(width);r.bottom=r.top+std::lround(height);}
    return r;
}
bool horizontal_bar(unsigned index) noexcept {return slot_panel(index) && slot_direction(index)>=2;}
void panel_native_size(unsigned,const LONG* r,float& width,float& height) noexcept {
    width=float(r[2]-r[0])*g_layout.unit;height=float(r[3]-r[1])*g_layout.unit;
}

widescreen::HudComposition panel_composition(unsigned index,const void* object) noexcept {
    const auto anchor=kPanelRoles[index].anchor;
    if(index>=unsigned(HudPanel::stance)) {
        auto h=widescreen::hud_composition(g_layout,anchor);
        h.scale=h.scale_y=g_layout.scale*g_corner_scales[index-unsigned(HudPanel::stance)]/100.f;
        h.pivot_x=widescreen::placed_to_screen_x(g_layout,widescreen::anchor_pivot_x(g_layout,anchor.x));
        h.pivot_y=widescreen::placed_to_screen_y(g_layout,widescreen::anchor_pivot_y(g_layout,anchor.y));
        // Original corner controls stay at the client edges, outside HUD caps.
        // Drawing and inverse input both consume this same composition.
        h.offset_x=anchor.x==Anchor::start?-g_layout.safe_left:g_layout.width-g_layout.safe_right;
        h.opacity=widescreen_hud_opacity(HudPanel(index));return h;
    }
    const auto& s=g_hud_panels[index];
    const RECT native=panel_native_rect(index,object);const auto* r=&native.left;
    const float left=widescreen::placed_to_screen_x(g_layout,float(r[0]));
    const float top=widescreen::placed_to_screen_y(g_layout,float(r[1]));
    const float scale=g_layout.scale*s.scale_percent/100.f;
    const widescreen::HudComposition h{scale,
        widescreen::placed_to_screen_x(g_layout,widescreen::anchor_pivot_x(g_layout,anchor.x)),
        widescreen::placed_to_screen_y(g_layout,widescreen::anchor_pivot_y(g_layout,anchor.y)),scale};
    float width,height;panel_native_size(index,r,width,height);width*=scale;height*=scale;
    const RECT source=panel_source_rect(index,object);
    const float source_width=float(source.right-source.left)*g_layout.unit*scale;
    const float source_height=float(source.bottom-source.top)*g_layout.unit*scale;
    const auto factor=[](Anchor a){return a==Anchor::end?1.f:a==Anchor::centre?.5f:0.f;};
    float ax=factor(anchor.x),ay=factor(anchor.y),cell_width=width,cell_height=height;
    if(slot_panel(index)) {
        const int direction=slot_direction(index);
        if(index==4 && s.direction==0)ax=.5f,ay=1.f;
        else if(direction>=2)ax=direction==2?0.f:1.f;
        else ay=direction==1?0.f:1.f;
        slot_dimensions(index,0,object,cell_width,cell_height);
        cell_width*=g_layout.unit*scale;cell_height*=g_layout.unit*scale;
    }
    const float base_x=widescreen::compose_x(left,h)+source_width*ax;
    const float base_y=widescreen::compose_y(top,h)+source_height*ay;
    const float placed_left=s.x_percent<0.f?base_x-width*ax:
        (std::max)(0.f,g_layout.width-cell_width)*s.x_percent/100.f+cell_width*ax-width*ax;
    float placed_top=s.y_percent<0.f?base_y-height*ay:
        (std::max)(0.f,g_layout.height-cell_height)*s.y_percent/100.f+cell_height*ay-height*ay;
    if((index==unsigned(HudPanel::minimap) || index==unsigned(HudPanel::character_info)) &&
       panel_header_side(index)!=DockSide::left && panel_header_side(index)!=DockSide::right) {
        // A horizontal strip stays outside the complete content rectangle.
        const float header=30.f*g_layout.unit*scale*button_multiplier(HudPanel(index));
        const float maximum=(std::max)(header,g_layout.height-height-header);
        placed_top=std::clamp(placed_top,header,maximum);
    }
    auto composed=widescreen::HudComposition{scale,left,top,scale,placed_left-left,placed_top-top};
    composed.opacity=widescreen_hud_opacity(HudPanel(index));return composed;
}
bool panel_bounds(unsigned index,const void* object,HudPanelBounds& bounds) noexcept {
    if(!object)return false;
    const RECT native=panel_native_rect(index,object);const auto* r=&native.left;
    const auto h=panel_composition(index,object);
    float width,height;panel_native_size(index,r,width,height);
    bounds={widescreen::compose_x(widescreen::placed_to_screen_x(g_layout,float(r[0])),h),
        widescreen::compose_y(widescreen::placed_to_screen_y(g_layout,float(r[1])),h),
        width*h.scale,height*h.scale,width,height};
    return bounds.width>0.f && bounds.height>0.f;
}
struct HeaderLayout { HudPanelBounds panel,body,background; float left,top,width,height,scale; DockSide side; };
HeaderLayout header_layout(HudPanel panel,HudPanelBounds b) noexcept {
    const float content_scale=panel==HudPanel::messages?g_layout.scale:g_layout.scale*g_hud_panels[unsigned(panel)].scale_percent/100.f;
    const float scale=content_scale*button_multiplier(panel);
    const float gap=border_inset(panel);
    // The button strip is exactly its buttons: padding insets only the
    // content, so the strip sits flush against the frame (#186).
    const float thickness=(std::max)(1.f,(panel==HudPanel::messages?20.f:30.f)*g_layout.unit*scale);
    DockSide side;
    if(panel!=HudPanel::messages)side=panel_header_side(unsigned(panel));
    else {
        const int override_side=button_side(panel);
        side=override_side==1?stock_header_side(panel):override_side>=2?DockSide(override_side-2):
            b.left<=edge_slack()?DockSide::left:b.left+b.width>=g_layout.width-edge_slack()?DockSide::right:
            b.top<=edge_slack()?DockSide::top:b.top+b.height>=g_layout.height-edge_slack()?DockSide::bottom:DockSide::floating;
    }
    float left=b.left,top=b.top,width=b.width,height=thickness;auto body=b;
    if(side==DockSide::left || side==DockSide::right) {
        width=(std::max)(1.f,(panel==HudPanel::messages?30.f:20.f)*g_layout.unit*scale);height=b.height;
        body.width=(std::max)(0.f,b.width-width);
        if(side==DockSide::right)left=b.left+b.width-width;else body.left+=width;
    } else if(panel==HudPanel::messages) {
        if(side==DockSide::bottom){top=b.top+b.height-thickness;body.height-=thickness;}
        else if(side==DockSide::top){body.top+=thickness;body.height-=thickness;}
        else top=b.top-thickness;
    } else if(side==DockSide::bottom)top=b.top+b.height;
    else top=b.top-thickness;
    // Padding reserves space for controls/content, never a hole in the fill.
    const auto background=body;
    body.left+=gap;body.top+=gap;body.width=(std::max)(1.f,body.width-2.f*gap);body.height=(std::max)(1.f,body.height-2.f*gap);
    return {b,body,background,left,top,width,height,scale,side};
}
HeaderLayout header_layout(HudPanel panel) noexcept {
    HudPanelBounds b{};widescreen_hud_panel_bounds(panel,b);return header_layout(panel,b);
}
void header_control_target(HudPanel panel,unsigned area,const HeaderLayout& header,float& x,float& y) noexcept {
    const bool vertical=header.side==DockSide::left || header.side==DockSide::right;
    const float unit=g_layout.unit*header.scale;
    const float cell_w=panel==HudPanel::messages?30.f:20.f,cell_h=panel==HudPanel::messages?20.f:30.f;
    const int slot=panel==HudPanel::messages?(area==0?2:int(area)-1):panel==HudPanel::minimap?(area==0?0:area==5?1:area==3?2:3):int(area);
    x=header.left+(vertical?(header.side==DockSide::left?cell_w*.5f*unit:header.width-cell_w*.5f*unit):(cell_w*.5f+cell_w*slot)*unit);
    y=header.top+(vertical?(cell_h*.5f+cell_h*slot)*unit:header.height*.5f);
    // Preserve stock spacing on its original side. All other orientations pack
    // actual cell widths/heights, so rotating a strip introduces no padding.
    if(header.side==stock_header_side(panel) && button_multiplier(panel)==1.f) {
        if(panel==HudPanel::minimap)y=header.top+(area==0?15.f:area==5?85.f:area==3?115.f:145.f)*unit;
        else if(panel==HudPanel::character_info)y=header.top+(area==0?15.f:85.f+30.f*area)*unit;
        else if(area==0)x=header.left+header.width*.5f;
        else x=header.left+(area==1?20.f:50.f)*unit;
    }
    // Signed gaps may overlap the frame. Keep complete controls on the window,
    // and use these exact centres for drawing and native hit inversion.
    const float half_x=cell_w*.5f*unit,half_y=cell_h*.5f*unit;
    x=std::clamp(x,half_x,(std::max)(half_x,g_layout.width-half_x));
    y=std::clamp(y,half_y,(std::max)(half_y,g_layout.height-half_y));
}
void clip_to_panel(widescreen::HudComposition& h,const HeaderLayout& header,bool frame=false) noexcept {
    const auto b=header.panel;const float margin=frame?5.f*g_layout.unit*h.scale:0.f;
    h.clip_native_screen=true;
    h.clip_left=(std::max)(0.f,b.left-margin);h.clip_top=(std::max)(0.f,b.top-margin);
    h.clip_right=(std::min)(g_layout.width,b.left+b.width+margin);h.clip_bottom=(std::min)(g_layout.height,b.top+b.height+margin);
    if(frame) {
        if(b.left<=edge_slack())h.clip_left=5.f*g_layout.unit*h.scale;
        if(b.top<=edge_slack())h.clip_top=5.f*g_layout.unit*h.scale;
        if(b.left+b.width>=g_layout.width-edge_slack())h.clip_right=g_layout.width-5.f*g_layout.unit*h.scale;
        if(b.top+b.height>=g_layout.height-edge_slack())h.clip_bottom=g_layout.height-5.f*g_layout.unit*h.scale;
    } else {
        // Clip to the whole background, not the padded content: the panel's
        // fill shares this composition, and clipping it to the content cut a
        // padding-wide gap between fill and frame (#186). Content keeps its size.
        h.clip_left=(std::max)(0.f,header.background.left);h.clip_top=(std::max)(0.f,header.background.top);
        h.clip_right=(std::min)(g_layout.width,header.background.left+header.background.width);
        h.clip_bottom=(std::min)(g_layout.height,header.background.top+header.background.height);
    }
}
float slide_fraction(HudPanel panel,const void* object) noexcept {
    const auto* b=static_cast<const std::uint8_t*>(object);
    const unsigned offset=panel==HudPanel::minimap?0x19C:panel==HudPanel::character_info?0x170:0x150;
    const int total=*reinterpret_cast<const int*>(b+offset+4);
    return total>0?std::clamp(float(*reinterpret_cast<const int*>(b+offset))/float(total),0.f,1.f):0.f;
}
bool slide_closing(HudPanel panel,const void* object) noexcept {
    const unsigned offset=panel==HudPanel::minimap?0x1A8:panel==HudPanel::character_info?0x17C:0x15C;
    return *reinterpret_cast<const float*>(static_cast<const std::uint8_t*>(object)+offset)>0.f;
}
void visible_panel_bounds(HudPanel panel,const void* object,const HeaderLayout& header,HudPanelBounds& b) noexcept {
    tutorial_visible_bounds(panel,slide_fraction(panel,object),header,b);
}
void slide_delta(const HeaderLayout& header,float fraction,float& x,float& y,bool inset_header=false) noexcept {
    x=y=0.f;
    if(header.side==DockSide::left)x=-fraction*(header.panel.width-header.width);
    else if(header.side==DockSide::right)x=fraction*(header.panel.width-header.width);
    else y=(header.side==DockSide::bottom?1.f:-1.f)*fraction*(header.panel.height-
        (inset_header && (header.side==DockSide::top || header.side==DockSide::bottom)?header.height:0.f));
}
void remap_body_slide(HudPanel panel,const void* object,widescreen::HudComposition& h) noexcept {
    const auto header=header_layout(panel);const float fraction=slide_fraction(panel,object);
    const auto* bytes=static_cast<const std::uint8_t*>(object);
    const unsigned offset=panel==HudPanel::minimap?0x19C:0x170;
    const float native=float(*reinterpret_cast<const int*>(bytes+offset))*g_layout.unit*h.scale;
    h.offset_x-=panel==HudPanel::minimap?native:-native;
    float x,y;slide_delta(header,fraction,x,y);h.offset_x+=x;h.offset_y+=y;
}
void compose_arrow(HudPanel panel,const void* object,widescreen::HudComposition& h) noexcept {
    const auto header=header_layout(panel);const bool closing=slide_closing(panel,object);
    // Header control composition already relocates area 0 and its visual.
    // Preserve the native CP/mesh offset: the loaded arrow's origin is not its
    // visible centre. Recentring that origin discards the producer compensation.
    const int outward=header.side==DockSide::left?1:header.side==DockSide::right?3:header.side==DockSide::bottom?0:2;
    const int native=panel==HudPanel::minimap?(closing?1:3):panel==HudPanel::character_info?(closing?3:1):(closing?0:2);
    h.rotation_quarters=(outward+(closing?2:0)-native+4)%4;
    header_control_target(panel,0,header,h.rotation_x,h.rotation_y);
}
widescreen::HudComposition panel_body_composition(unsigned panel,const void* object,bool background=false) noexcept {
    auto h=panel_composition(panel,object);
    auto header=header_layout(HudPanel(panel));
    if(background)header.body=header.background;
    // Native content centres do not move when its toolbar changes sides.
    const float centre_x=panel==unsigned(HudPanel::minimap)?700.f:100.f;
    const float centre_y=panel==unsigned(HudPanel::minimap)?80.f:110.f;
    const auto anchor=kPanelRoles[panel].anchor;
    const float native_x=widescreen::placed_to_screen_x(g_layout,widescreen::layout_to_placed_x(g_layout,anchor,centre_x,false));
    const float native_y=widescreen::placed_to_screen_y(g_layout,widescreen::layout_to_placed_y(g_layout,anchor,centre_y,false));
    h.offset_x+=header.body.left+header.body.width*.5f-widescreen::compose_x(native_x,h);
    h.offset_y+=header.body.top+header.body.height*.5f-widescreen::compose_y(native_y,h);
    remap_body_slide(HudPanel(panel),object,h);clip_to_panel(h,header);
    if(slide_fraction(HudPanel(panel),object)>=1.f)h.clip_right=h.clip_left;
    return h;
}
bool header_control_composition(HudPanel panel,unsigned area,const void* object,widescreen::HudComposition& h) noexcept {
    if(area!=0 && slide_fraction(panel,object)>=1.f)return false;
    int slot=-1;
    if(panel==HudPanel::minimap) {if(area==0)slot=0;else if(area==5)slot=1;else if(area==3)slot=2;else if(area==4)slot=3;}
    else if(panel==HudPanel::character_info && area<5)slot=int(area);
    else if(panel==HudPanel::messages && area<3)slot=area==0?2:int(area)-1;
    if(slot<0)return false;
    const auto* bytes=static_cast<const std::uint8_t*>(object);
    auto* a=(*reinterpret_cast<const std::uint8_t* const* const*>(bytes+0x128))[area];
    const auto* rect=reinterpret_cast<const LONG*>(a+0x14);const auto* shift=reinterpret_cast<const LONG*>(a+0x28);
    const float x=widescreen::placed_to_screen_x(g_layout,float(rect[0]+rect[2])*.5f+float(shift[0]));
    const float y=widescreen::placed_to_screen_y(g_layout,float(rect[1]+rect[3])*.5f+float(shift[1]));
    const auto header=header_layout(panel);
    h.scale=h.scale_y=header.scale;
    float target_x,target_y;header_control_target(panel,area,header,target_x,target_y);
    h.offset_x+=target_x-widescreen::compose_x(x,h);h.offset_y+=target_y-widescreen::compose_y(y,h);
    h.clip_native_screen=false;return true;
}
widescreen::HudComposition object_composition(const void* object,Anchors anchor) noexcept {
    const int index=panel_index(object);
    if(index==int(HudPanel::minimap) || index==int(HudPanel::character_info))return panel_body_composition(unsigned(index),object);
    return index>=0 ? panel_composition(unsigned(index),object) : widescreen::hud_composition(g_layout,anchor);
}

MessagePanelBounds message_panel_bounds() noexcept {
    const float width = (std::min)(g_layout.width, 440.f*g_layout.unit*g_layout.scale*g_message_panel.width_percent/100.f);
    float height = (std::min)(g_layout.height, 100.f*g_layout.unit*g_layout.scale*g_message_panel.height_percent/100.f);
    const float left=std::clamp(g_layout.width*g_message_panel.centre_x_percent/100.f-width*.5f,0.f,g_layout.width-width);
    // Explicit side overrides use the same scaled three-cell footprint as docking.
    const int side=button_side(HudPanel::messages);
    const bool vertical=side==2 || side==3 || (side==0 && (left<=.5f || left+width>=g_layout.width-.5f));
    const float unit=g_layout.unit*g_layout.scale,buttons=button_multiplier(HudPanel::messages);
    if(vertical)height=(std::max)(height,(std::min)(g_layout.height,60.f*unit*buttons));
    // Horizontal controls must leave a positive text/scrollbar aperture at
    // the smallest editor size, including independently enlarged buttons.
    else {
        const float top=std::clamp(g_layout.height*g_message_panel.top_y_percent/100.f,0.f,g_layout.height-height);
        if(side==1 || side==4 || side==5 || (side==0 && (top<=.5f || top+height>=g_layout.height-.5f)))
            height=(std::max)(height,(std::min)(g_layout.height,(30.f+20.f*buttons)*unit));
    }
    return {left,std::clamp(g_layout.height*g_message_panel.top_y_percent/100.f,0.f,g_layout.height-height),width,height};
}
widescreen::HudComposition message_panel_composition() noexcept {
    const auto b=message_panel_bounds();
    const float left=widescreen::placed_to_screen_x(g_layout,180.f), top=widescreen::placed_to_screen_y(g_layout,0.f);
    auto h=widescreen::HudComposition{g_layout.scale,left,top,g_layout.scale,b.left-left,b.top-top};
    h.opacity=widescreen_hud_opacity(HudPanel::messages);return h;
}
bool message_panel_object(const void* object) noexcept {
    const void* output=nullptr; const void* text=nullptr;
    return current_message_panel(output,text) && (object==output || object==text);
}


thread_local const void* t_visual_object=nullptr;
thread_local const void* t_input_object=nullptr;
thread_local Anchors t_anchor{};  // the widget method on this thread, else centred
thread_local int t_raw = 0;       // inside a figure builder: nested ingress passes through

// Native layout stays at 100%; owned composition scales all visual output.
float placed_x(float x) noexcept { return widescreen::native_x(g_layout,t_anchor,x); }
float placed_y(float y) noexcept { return widescreen::native_y(g_layout,t_anchor,y); }
thread_local widescreen::HudComposition t_input_composition{};
thread_local InputScope t_input_scope = InputScope::none;
bool inside_composed_screen(float x,float y,const widescreen::HudComposition& h) noexcept {
    if(h.clip_native_screen && h.clip_right>=0.f)return x>=h.clip_left && x<h.clip_right && y>=h.clip_top && y<h.clip_bottom;
    return !h.clip_native_screen || (x>=widescreen::compose_x(0.f,h) && y>=widescreen::compose_y(0.f,h) &&
        x<widescreen::compose_x(g_layout.width,h) && y<widescreen::compose_y(g_layout.height,h));
}
POINT inverse_point(POINT p,const widescreen::HudComposition& h) noexcept {
    if (!widescreen::composition_changed(h)) return p;
    const float x=h.pivot_x+(widescreen::placed_to_screen_x(g_layout,float(p.x))-h.offset_x-h.pivot_x)/h.scale;
    const float y=h.pivot_y+(widescreen::placed_to_screen_y(g_layout,float(p.y))-h.offset_y-h.pivot_y)/h.scale_y;
    p.x=static_cast<LONG>(std::floor(widescreen::screen_to_placed_x(g_layout,x)));
    p.y=static_cast<LONG>(std::floor(widescreen::screen_to_placed_y(g_layout,y)));
    return p;
}
POINT input_point(POINT p) noexcept {return inverse_point(p,t_input_composition);}

// Quest items (items panel areas 4-7): the stock slot layout, scaled by the
// widget scale, at the stock position (anchored like the items panel) or at
// the widget's own x/y (#184).
RECT quest_area_rect(const std::uint8_t* object,unsigned slot) noexcept {
    const auto* area=(*reinterpret_cast<const std::uint8_t* const* const*>(object+0x128))[slot];
    const auto r=*reinterpret_cast<const RECT*>(area+0x14);const auto* shift=reinterpret_cast<const LONG*>(area+0x28);
    return {r.left+shift[0],r.top+shift[1],r.right+shift[0],r.bottom+shift[1]};
}
bool quest_slot_bounds(unsigned slot,const void* object,HudPanelBounds& out) noexcept {
    const auto* bytes=static_cast<const std::uint8_t*>(object);
    if(slot<4 || slot>=8 || *reinterpret_cast<const unsigned*>(bytes+0x12C)<8)return false;
    RECT group=quest_area_rect(bytes,4);
    for(unsigned i=5;i<8;++i){const RECT a=quest_area_rect(bytes,i);
        group.left=(std::min)(group.left,a.left);group.top=(std::min)(group.top,a.top);
        group.right=(std::max)(group.right,a.right);group.bottom=(std::max)(group.bottom,a.bottom);}
    const RECT r=quest_area_rect(bytes,slot);
    const float scale=g_layout.scale*g_quest_items.scale_percent/100.f,unit=g_layout.unit*scale;
    const float width=float(r.right-r.left)*unit,height=float(r.bottom-r.top)*unit;
    float left,top;
    if(g_quest_items.x_percent<0.f || g_quest_items.y_percent<0.f) {
        const auto anchor=kPanelRoles[unsigned(HudPanel::items)].anchor;
        const widescreen::HudComposition h{scale,
            widescreen::placed_to_screen_x(g_layout,widescreen::anchor_pivot_x(g_layout,anchor.x)),
            widescreen::placed_to_screen_y(g_layout,widescreen::anchor_pivot_y(g_layout,anchor.y)),scale};
        left=widescreen::compose_x(widescreen::placed_to_screen_x(g_layout,float(r.left)),h);
        top=widescreen::compose_y(widescreen::placed_to_screen_y(g_layout,float(r.top)),h);
    } else {
        const float group_width=float(group.right-group.left)*unit,group_height=float(group.bottom-group.top)*unit;
        left=(std::max)(0.f,g_layout.width-group_width)*g_quest_items.x_percent/100.f+float(r.left-group.left)*unit;
        top=(std::max)(0.f,g_layout.height-group_height)*g_quest_items.y_percent/100.f+float(r.top-group.top)*unit;
    }
    out={left,top,width,height,width/unit,height/unit};
    return true;
}
bool quest_group_bounds(const void* object,HudPanelBounds& out) noexcept {
    HudPanelBounds b;if(!quest_slot_bounds(4,object,b))return false;
    float right=b.left+b.width,bottom=b.top+b.height;out=b;
    for(unsigned i=5;i<8;++i)if(quest_slot_bounds(i,object,b)){
        out.left=(std::min)(out.left,b.left);out.top=(std::min)(out.top,b.top);
        right=(std::max)(right,b.left+b.width);bottom=(std::max)(bottom,b.top+b.height);}
    const float scale=g_layout.scale*g_quest_items.scale_percent/100.f;
    out.width=right-out.left;out.height=bottom-out.top;out.native_width=out.width/scale;out.native_height=out.height/scale;
    return true;
}
bool slot_screen_bounds(unsigned panel,unsigned slot,const void* object,HudPanelBounds& out) noexcept {
    if(panel==6 && slot>=4)return quest_slot_bounds(slot,object,out);
    if(!slot_panel(panel) || slot>=layout_slot_count(panel,object))return false;
    HudPanelBounds b;if(!panel_bounds(panel,object,b))return false;
    const int direction=slot_direction(panel);const bool horizontal=direction>=2;
    const float unit=g_layout.unit*g_layout.scale*g_hud_panels[panel].scale_percent/100.f;
    float width,height;slot_dimensions(panel,slot,object,width,height);width*=unit;height*=unit;
    const unsigned per=slots_per_line(panel),count=layout_slot_count(panel,object);
    const unsigned line=per?slot/per:0u;
    float prefix=0.f;
    for(unsigned i=line*(per?per:0u);i<slot;++i){float w,h;slot_dimensions(panel,i,object,w,h);prefix+=(std::max)(1.f,(horizontal?w:h)+g_hud_panels[panel].padding)*unit;}
    out={b.left+(b.width-width)*.5f,b.top+(b.height-height)*.5f,width,height,width/unit,height/unit};
    if(per && count>per) {
        // Each line is a cell of the widest slot across the bar; line 0 on the anchored side.
        float cross=0.f;
        for(unsigned i=0;i<count;++i){float w,h;slot_dimensions(panel,i,object,w,h);cross=(std::max)(cross,horizontal?h:w);}
        const float cell=cross*unit,step=(std::max)(1.f,cross+g_hud_panels[panel].padding)*unit;
        const float offset=float(line)*step;
        if(horizontal) out.top=lines_grow_negative(panel)?b.top+b.height-cell-offset+(cell-height)*.5f:b.top+offset+(cell-height)*.5f;
        else out.left=lines_grow_negative(panel)?b.left+b.width-cell-offset+(cell-width)*.5f:b.left+offset+(cell-width)*.5f;
    }
    if(direction==0)out.top=b.top+b.height-prefix-height;
    else if(direction==1)out.top=b.top+prefix;
    else if(direction==2)out.left=b.left+prefix;
    else out.left=b.left+b.width-prefix-width;
    return true;
}
bool bar_slot_composition(unsigned panel,unsigned slot,const void* object,widescreen::HudComposition& h) noexcept {
    const auto* bytes=static_cast<const std::uint8_t*>(object);
    const unsigned count=*reinterpret_cast<const unsigned*>(bytes+0x12C);
    if(!slot_panel(panel) || panel==4 || slot>=count)return false;
    const auto* area=(*reinterpret_cast<const std::uint8_t* const* const*>(bytes+0x128))[slot];
    const auto r=*reinterpret_cast<const RECT*>(area+0x14);const auto* shift=reinterpret_cast<const LONG*>(area+0x28);
    const float native_x=widescreen::placed_to_screen_x(g_layout,.5f*float(r.left+r.right)+float(shift[0]));
    const float native_y=widescreen::placed_to_screen_y(g_layout,(panel==5?float(r.bottom)-40.f:.5f*float(r.top+r.bottom))+float(shift[1]));
    HudPanelBounds b;if(!slot_screen_bounds(panel,slot,object,b))return false;
    if(panel==6 && slot>=4) {
        // Quest slots: the quest widget's scale and fade.
        const float scale=g_layout.scale*g_quest_items.scale_percent/100.f;
        h.scale=h.scale_y=scale;h.opacity=widescreen_hud_opacity(HudPanel::quest_items);
    }
    h.offset_x+=b.left+b.width*.5f-widescreen::compose_x(native_x,h);
    h.offset_y+=b.top+b.height*.5f-widescreen::compose_y(native_y,h);return true;
}
bool portrait_slot_composition(unsigned slot,const void* object,widescreen::HudComposition& h) noexcept {
    const auto* bytes=static_cast<const std::uint8_t*>(object);
    const unsigned count=*reinterpret_cast<const unsigned*>(bytes+0x12C);if(slot>=count)return false;
    const auto* area=(*reinterpret_cast<const std::uint8_t* const* const*>(bytes+0x128))[slot];
    const auto r=*reinterpret_cast<const RECT*>(area+0x14);const auto* shift=reinterpret_cast<const LONG*>(area+0x28);
    HudPanelBounds b;if(!slot_screen_bounds(4,slot,object,b))return false;
    h.offset_x+=b.left-widescreen::compose_x(widescreen::placed_to_screen_x(g_layout,float(r.left+shift[0])),h);
    h.offset_y+=b.top-widescreen::compose_y(widescreen::placed_to_screen_y(g_layout,float(r.top+shift[1])),h);return true;
}
int portrait_camera_slot(const void* object) noexcept {
    const auto* bytes=static_cast<const std::uint8_t*>(object);
    const auto* screen=*reinterpret_cast<const std::uint8_t* const*>(bytes+0x148);
    auto* units=*reinterpret_cast<const std::uint8_t* const* const*>(screen+0x16C);
    const int count=(std::min)(*reinterpret_cast<const int*>(screen+0x170),*reinterpret_cast<const int*>(bytes+0x14C));
    const DWORD carrier=*reinterpret_cast<const DWORD*>(at(0x0079B3C4));
    for(int i=0;i<count;++i)if(units[i] && *reinterpret_cast<const DWORD*>(units[i]+0x0C)==carrier)return i;
    return -1;
}
// 2D surfaces: full size around the anchor; the renderer composes them at the
// UI scale around the same pivot (widescreen::hud_blit).
float surface_x(float x) noexcept { return widescreen::native_x(g_layout,t_anchor,x); }
float surface_y(float y) noexcept { return widescreen::native_y(g_layout,t_anchor,y); }
LONG pixel_x(float placed) noexcept { return widescreen::pixel_x(g_layout,placed); }
LONG pixel_y(float placed) noexcept { return widescreen::pixel_y(g_layout,placed); }
LONG local_pixels(LONG v) noexcept { return widescreen::local_pixels(g_layout,v); }
LONG local_units(LONG v) noexcept { return widescreen::local_units(g_layout,v); }
// A real (cursor) pixel to placed units; floor keeps hit tests monotonic.
LONG cursor_placed_x(LONG v) noexcept {
    return widescreen::cursor_x(g_layout,v);
}
LONG cursor_placed_y(LONG v) noexcept {
    return widescreen::cursor_y(g_layout,v);
}

// CInterface3D::VirtualToScreen: layout (under the current widget anchor, at
// full size) to pixels; a null interface (the local call sites) scales a
// surface-local rectangle.
void __fastcall virtual_to_screen_rect(const void* self, void*, RECT* r) {
    if (!self) {
        widescreen::local_rect_to_pixels(g_layout,*r);
        return;
    }
    widescreen::native_surface_rect(g_layout,t_anchor,*r);
}
void __fastcall virtual_to_screen_point(const void* self, void*, POINT* p) {
    if (!self) { p->x = local_pixels(p->x); p->y = local_pixels(p->y); return; }
    p->x = pixel_x(surface_x(static_cast<float>(p->x)));
    p->y = pixel_y(surface_y(static_cast<float>(p->y)));
}
void __fastcall virtual_to_screen_size(const void*, void*, SIZE* s) {
    s->cx = local_pixels(s->cx); s->cy = local_pixels(s->cy);
}
// CInterface3D::ScreenToVirtual: a cursor position (pixels) to placed units;
// a null interface converts surface-local pixels.
void __fastcall screen_to_virtual_rect(const void* self, void*, RECT* r) {
    if (!self) {
        widescreen::pixels_rect_to_local(g_layout,*r);
        return;
    }
    widescreen::pixels_rect_to_placed(g_layout,*r);
}
void __fastcall screen_to_virtual_point(const void* self, void*, POINT* p) {
    if (!self) { p->x = local_units(p->x); p->y = local_units(p->y); return; }
    p->x = cursor_placed_x(p->x); p->y = cursor_placed_y(p->y);
    if (t_input_scope == InputScope::rect) *p = input_point(*p);
}
void __fastcall screen_to_virtual_size(const void*, void*, SIZE* s) {
    s->cx = local_units(s->cx); s->cy = local_units(s->cy);
}
// A cursor movement (real pixels) to placed units, without the UI scale so
// rotation speed does not follow the interface size.
void __fastcall cursor_delta(const void*, void*, POINT* p) {
    p->x = widescreen::local_units(g_layout,p->x);
    p->y = widescreen::local_units(g_layout,p->y);
}
// CI3DScreenGlobalMap: the zone hit surface covers the 4:3 content at half
// size; the caller passes the cursor halved.
using GetPixelFn = DWORD(__thiscall*)(void*, int, int);
DWORD __fastcall global_map_pixel(void* surface, void*, int half_x, int half_y) {
    const float content_left = 0.5f * g_layout.width - 400.0f * g_layout.unit;
    const float content_top = 0.5f * g_layout.height - 300.0f * g_layout.unit;
    const int x = widescreen::hit_surface_pixel(float(half_x),content_left,2.f);
    const int y = widescreen::hit_surface_pixel(float(half_y),content_top,2.f);
    return reinterpret_cast<GetPixelFn>(at(kGetPixelEntry))(surface, x, y);
}

// Layout ingress: argument transforms in front of the stock bodies.
void* g_point_in_area = nullptr;
BOOL __fastcall point_in_area(void* area, void*, POINT p) {
    // Tooltip placement keeps the actual cursor; only its membership check is inverted.
    if (t_input_scope == InputScope::area) {
        auto h=t_input_composition;
        const int panel=panel_index(t_input_object);
        if(panel>=0 && slot_panel(unsigned(panel)) && panel!=4) {
            const auto* object=static_cast<const std::uint8_t*>(t_input_object);
            const auto count=*reinterpret_cast<const unsigned*>(object+0x12C);
            auto* array=*reinterpret_cast<void* const* const*>(object+0x128);
            for(unsigned i=0;i<count;++i)if(array[i]==area) {
                HudPanelBounds slot;
                const float px=widescreen::placed_to_screen_x(g_layout,float(p.x)),py=widescreen::placed_to_screen_y(g_layout,float(p.y));
                if(!active_slot(unsigned(panel),i,object) || !slot_screen_bounds(unsigned(panel),i,object,slot) ||
                   px<slot.left || px>=slot.left+slot.width || py<slot.top || py>=slot.top+slot.height)return FALSE;
                if(!bar_slot_composition(unsigned(panel),i,object,h))return FALSE;
                break;
            }
        }
        if(panel==int(HudPanel::portraits)) {
            const auto* object=static_cast<const std::uint8_t*>(t_input_object);
            const unsigned count=*reinterpret_cast<const unsigned*>(object+0x12C);
            auto* areas=*reinterpret_cast<void* const* const*>(object+0x128);
            for(unsigned i=0;i<count;++i)if(areas[i]==area){if(!portrait_slot_composition(i,object,h))return FALSE;break;}
        }
        const void* message_output=nullptr;const void* message_text=nullptr;
        if(panel==int(HudPanel::minimap) || panel==int(HudPanel::character_info) || (current_message_panel(message_output,message_text) && t_input_object==message_output)) {
            const auto* object=static_cast<const std::uint8_t*>(t_input_object);
            const auto count=*reinterpret_cast<const unsigned*>(object+0x12C);
            auto* array=*reinterpret_cast<void* const* const*>(object+0x128);
            const auto kind=panel>=0?HudPanel(panel):HudPanel::messages;
            for(unsigned i=0;i<count;++i)if(array[i]==area) {
                if(i!=0 && slide_fraction(kind,object)>=1.f)return FALSE;
                auto base=kind==HudPanel::messages ? message_panel_composition() : panel_composition(unsigned(kind),object);
                if(header_control_composition(kind,i,object,base))h=base;
                break;
            }
        }
        if(!inside_composed_screen(widescreen::placed_to_screen_x(g_layout,float(p.x)),widescreen::placed_to_screen_y(g_layout,float(p.y)),h))return FALSE;
        p = inverse_point(p,h);
    }
    return reinterpret_cast<BOOL(__thiscall*)(void*, POINT)>(g_point_in_area)(area, p);
}
void* g_make_rect = nullptr;
void* g_add_point = nullptr;
void* g_relative_to_screen = nullptr;
void* g_translation_relative = nullptr;
void* g_make_bar = nullptr;
void* g_make_tiling_bar = nullptr;
void* g_make_bar_frame = nullptr;
void* g_make_bar_frame2 = nullptr;
void* g_make_triangle = nullptr;

int placed_int_x(int x) noexcept { return widescreen::layout_int_x(g_layout,t_anchor,x); }
int placed_int_y(int y) noexcept { return widescreen::layout_int_y(g_layout,t_anchor,y); }

void __fastcall make_rect(void* area, void*, int x1, int y1, int x2, int y2) {
    if (!t_raw) { x1 = placed_int_x(x1); y1 = placed_int_y(y1); x2 = placed_int_x(x2); y2 = placed_int_y(y2); }
    reinterpret_cast<void(__thiscall*)(void*, int, int, int, int)>(g_make_rect)(area, x1, y1, x2, y2);
}
void __fastcall add_point(void* area, void*, int x, int y) {
    if (!t_raw) { x = placed_int_x(x); y = placed_int_y(y); }
    reinterpret_cast<void(__thiscall*)(void*, int, int)>(g_add_point)(area, x, y);
}
// 3D figures: the placed point (the stock formula projects it through the
// clip scale). Depth and native size stay independent of live HUD scale.
void figure_point(float& x, float& y, float& z) noexcept {
    if (t_raw) return;
    x = placed_x(x);
    y = placed_y(y);
}
void __fastcall relative_to_screen(void* figure, void*, float x, float y, float z) {
    figure_point(x, y, z);
    reinterpret_cast<void(__thiscall*)(void*, float, float, float)>(g_relative_to_screen)(figure, x, y, z);
}
void __fastcall translation_relative(void* translation, void*, float x, float y, float z) {
    figure_point(x, y, z);
    reinterpret_cast<void(__thiscall*)(void*, float, float, float)>(g_translation_relative)(translation, x, y, z);
}
// Flat bars: corners to placed space at fixed native size. A centred bar
// entirely left of 0 or right
// of 800 is a pillar filler (the JMV addon's camp/character-screen side
// panels): it reaches the screen edge.
void bar_corners(float& l, float& t, float& r, float& b) noexcept {
    widescreen::native_bar_corners(g_layout,t_anchor,t_raw,l,t,r,b);
}
struct RawScope { RawScope() noexcept { ++t_raw; } ~RawScope() { --t_raw; } };
void __fastcall make_bar(void* figure, void*, float depth, float pixels, float l, float t, float r, float b,
                         int map_l, int map_t, int map_r, int map_b) {
    bar_corners(l, t, r, b);
    RawScope raw;
    reinterpret_cast<void(__thiscall*)(void*, float, float, float, float, float, float, int, int, int, int)>(g_make_bar)(
        figure, depth, pixels, l, t, r, b, map_l, map_t, map_r, map_b);
}
void __fastcall make_tiling_bar(void* figure, void*, float depth, float pixels, float l, float t, float r, float b,
                                int map_l, int map_t, int map_r, int map_b, int tiles_x, int tiles_y) {
    bar_corners(l, t, r, b);
    RawScope raw;
    reinterpret_cast<void(__thiscall*)(void*, float, float, float, float, float, float, int, int, int, int, int, int)>(
        g_make_tiling_bar)(figure, depth, pixels, l, t, r, b, map_l, map_t, map_r, map_b, tiles_x, tiles_y);
}
using BarFrameFn = void(__thiscall*)(void*, float, float, float, float, float, float, float, int, int, int, int);
void __fastcall make_bar_frame(void* figure, void*, float depth, float pixels, float l, float t, float r, float b,
                               float frame, int map_l, int map_t, int map_r, int map_b) {
    bar_corners(l, t, r, b);
    RawScope raw;
    reinterpret_cast<BarFrameFn>(g_make_bar_frame)(figure, depth, pixels, l, t, r, b, frame, map_l, map_t, map_r, map_b);
}
void __fastcall make_bar_frame2(void* figure, void*, float depth, float pixels, float l, float t, float r, float b,
                                float frame, int map_l, int map_t, int map_r, int map_b) {
    bar_corners(l, t, r, b);
    RawScope raw;
    reinterpret_cast<BarFrameFn>(g_make_bar_frame2)(figure, depth, pixels, l, t, r, b, frame, map_l, map_t, map_r, map_b);
}
void __fastcall make_triangle(void* figure, void*, float depth, float pixels, float x1, float y1, float x2, float y2,
                              float x3, float y3, float u1, float v1, float u2, float v2, float u3, float v3) {
    if (!t_raw) {
        x1 = placed_x(x1); y1 = placed_y(y1);
        x2 = placed_x(x2); y2 = placed_y(y2);
        x3 = placed_x(x3); y3 = placed_y(y3);
    }
    RawScope raw;
    reinterpret_cast<void(__thiscall*)(void*, float, float, float, float, float, float, float, float, float, float, float, float, float, float)>(
        g_make_triangle)(figure, depth, pixels, x1, y1, x2, y2, x3, y3, u1, v1, u2, v2, u3, v3);
}

// Message backing storage changes at the current game-owned surface boundary.
// Preserve both native objects; prepare a complete new COM surface before swap.
void* g_scroll_text_set_rect=nullptr;
bool g_message_surface_failed=false;
thread_local bool t_message_layout=false;
float g_message_mesh_width=0.f,g_message_mesh_height=0.f,g_message_mesh_buttons_scale=0.f,g_message_mesh_padding=0.f;
DockSide g_message_mesh_side=DockSide::floating;
void compose_message_body(const void* output,widescreen::HudComposition& h,bool background=false) noexcept {
    auto header=header_layout(HudPanel::messages);float x,y;
    if(background)header.body=header.background;
    slide_delta(header,slide_fraction(HudPanel::messages,output),x,y,true);h.offset_x+=x;h.offset_y+=y;
    clip_to_panel(h,header);
    if(slide_fraction(HudPanel::messages,output)>=1.f)h.clip_right=h.clip_left;
}

std::uint64_t g_message_mesh_generation=0;

bool resize_text_surface(void* object,int width,int height,bool& replaced) noexcept {
    replaced=false;
    auto* bytes=static_cast<std::uint8_t*>(object);
    if(!*reinterpret_cast<const unsigned*>(bytes+0x11C))return false;
    auto* surface=(*reinterpret_cast<std::uint8_t***>(bytes+0x118))[0];
    if(!surface || *reinterpret_cast<void**>(surface+0x1C) || *reinterpret_cast<unsigned*>(surface+0x20))return false;
    auto* wrapper=*reinterpret_cast<std::uint8_t**>(surface+0x10);
    if(!wrapper)return false;
    auto* old=*reinterpret_cast<IDirectDrawSurface7**>(wrapper+0x10);
    if(!old)return false;
    DDSURFACEDESC2 old_desc{};old_desc.dwSize=sizeof(old_desc);
    if(FAILED(old->GetSurfaceDesc(&old_desc)))return false;
    if(old_desc.dwWidth==unsigned(width) && old_desc.dwHeight==unsigned(height))return true;
    DDSURFACEDESC2 desc{};desc.dwSize=sizeof(desc);
    desc.dwFlags=DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT|DDSD_PIXELFORMAT;
    desc.dwWidth=width;desc.dwHeight=height;desc.ddsCaps=old_desc.ddsCaps;desc.ddpfPixelFormat=old_desc.ddpfPixelFormat;
    auto* owner=*reinterpret_cast<void**>(wrapper+0x0C);
    using Create=IDirectDrawSurface7*(__thiscall*)(void*,DDSURFACEDESC2*);
    auto* replacement=reinterpret_cast<Create>(at(0x004EF5C0))(owner,&desc);
    if(!replacement)return false;
    DDCOLORKEY key{};
    const bool keyed=SUCCEEDED(old->GetColorKey(DDCKEY_SRCBLT,&key));
    bool ready=!keyed || SUCCEEDED(replacement->SetColorKey(DDCKEY_SRCBLT,&key));
    DDBLTFX fill{};fill.dwSize=sizeof(fill);fill.dwFillColor=keyed?key.dwColorSpaceLowValue:0;
    ready=ready && SUCCEEDED(replacement->Blt(nullptr,nullptr,nullptr,DDBLT_COLORFILL|DDBLT_WAIT,&fill));
    // Native text requires a DIB as well as pixels; allocation failure is still HRESULT here.
    HDC dc=nullptr;
    if(ready)ready=SUCCEEDED(replacement->GetDC(&dc));
    if(dc) {
        RECT r{0,0,width,height};
        HBRUSH brush=CreateSolidBrush(*reinterpret_cast<COLORREF*>(surface+0x34));
        const bool cleared=brush && FillRect(dc,&r,brush)!=0;
        if(brush)DeleteObject(brush);
        ready=SUCCEEDED(replacement->ReleaseDC(dc)) && cleared && ready;
    }
    DDSURFACEDESC2 actual{};actual.dwSize=sizeof(actual);
    ready=ready && SUCCEEDED(replacement->GetSurfaceDesc(&actual));
    if(!ready){replacement->Release();return false;}
    *reinterpret_cast<IDirectDrawSurface7**>(wrapper+0x10)=replacement; // transfer CreateSurface's reference
    *reinterpret_cast<unsigned*>(wrapper+0x14)=actual.dwWidth;
    *reinterpret_cast<unsigned*>(wrapper+0x18)=actual.dwHeight;
    *reinterpret_cast<unsigned*>(wrapper+0x1C)=actual.ddpfPixelFormat.dwRGBBitCount;
    *reinterpret_cast<unsigned*>(surface+0x14)=actual.dwWidth;
    *reinterpret_cast<unsigned*>(surface+0x18)=actual.dwHeight;
    auto* rect=reinterpret_cast<RECT*>(surface+0x24);
    rect->right=rect->left+width;rect->bottom=rect->top+height;
    *reinterpret_cast<int*>(at(0x007C7E78))+=int(actual.lPitch*actual.dwHeight-old_desc.lPitch*old_desc.dwHeight);
    old->Release();replaced=true;return true;
}

void message_text_rect(void* object) noexcept {
    const auto* r=reinterpret_cast<const RECT*>(static_cast<const std::uint8_t*>(object)+kObjectRectOffset);
    const float scale=g_message_panel.text_scale_percent/100.f;
    auto* wrap=reinterpret_cast<RECT*>(static_cast<std::uint8_t*>(object)+0x18C);
    const RECT desired=message_text_wrap(*r,scale);
    const int width=(std::max)(1,int(std::ceil(float(r->right-r->left)*g_layout.unit/scale)));
    const int height=(std::max)(1,int(std::ceil(float(r->bottom-r->top)*g_layout.unit/scale)));
    bool replaced=false;const bool ready=resize_text_surface(object,width,height,replaced);
    if(!ready && !g_message_surface_failed)event("message_reflow_surface_unavailable","Text omitted: replacement backing storage could not be prepared.");
    g_message_surface_failed=!ready;
    if(ready && (replaced || std::memcmp(wrap,&desired,sizeof(desired))!=0)) {
        *wrap=desired;
        reinterpret_cast<void(__thiscall*)(void*)>(at(0x0065DAA0))(object);
    }
}
void __fastcall scroll_text_set_rect(void* object,void*,RECT* rect) {
    const void* output=nullptr;const void* text=nullptr;
    if(!current_message_panel(output,text) || text!=object) {
        reinterpret_cast<void(__thiscall*)(void*,RECT*)>(g_scroll_text_set_rect)(object,rect);return;
    }
    RECT desired=message_geometry(header_layout(HudPanel::messages),g_layout.unit*g_layout.scale,g_layout.unit).text;
    const auto* current=reinterpret_cast<const RECT*>(static_cast<const std::uint8_t*>(object)+kObjectRectOffset);
    if(std::memcmp(current,&desired,sizeof(desired))!=0)
        reinterpret_cast<void(__thiscall*)(void*,RECT*)>(g_scroll_text_set_rect)(object,&desired);
    message_text_rect(object);
}

void refresh_message_layout() noexcept {
    if(t_message_layout || !g_scroll_text_set_rect)return;
    const void* output=nullptr;const void* text=nullptr;
    if(!current_message_panel(output,text))return;
    const auto generation=bridge_hud_screen_generation();
    if(!generation)return;
    t_message_layout=true;
    auto* bytes=const_cast<std::uint8_t*>(static_cast<const std::uint8_t*>(output));
    const auto b=message_panel_bounds();const float unit=g_layout.unit*g_layout.scale;
    const float width=b.width/unit;
    const auto geometry=message_geometry(header_layout(HudPanel::messages),unit,g_layout.unit);
    const auto background=geometry.background;
    const auto header=header_layout(HudPanel::messages);
    const float frame_height=b.height/unit;
    auto** figures=*reinterpret_cast<void***>(bytes+0x108);
    auto** areas=*reinterpret_cast<void***>(bytes+0x128);
    const bool rebuild=generation!=g_message_mesh_generation || width!=g_message_mesh_width || frame_height!=g_message_mesh_height || header.side!=g_message_mesh_side || header.scale!=g_message_mesh_buttons_scale || g_message_panel.border_padding!=g_message_mesh_padding;
    if(rebuild) {
        make_bar(figures[0],nullptr,20.f,100.f,background.left,background.top,background.right,background.bottom,0,0,255,255);
        const auto toolbar=geometry.toolbar;
        make_bar(figures[1],nullptr,20.f,100.f,toolbar.left,toolbar.top,toolbar.right,toolbar.bottom,0,0,255,255);
        // The frame is generated by the owned atlas primitive pass.
        make_rect(areas[3],nullptr,geometry.panel.left,geometry.panel.top,geometry.panel.right,geometry.panel.bottom);
        g_message_mesh_generation=generation;g_message_mesh_width=width;g_message_mesh_height=frame_height;g_message_mesh_side=header.side;g_message_mesh_buttons_scale=header.scale;g_message_mesh_padding=g_message_panel.border_padding;
    }
    relative_to_screen(figures[0],nullptr,geometry.background_x,geometry.background_y,20.f);
    relative_to_screen(figures[1],nullptr,180.f+width*.5f,10.f,20.f);
    relative_to_screen(figures[3],nullptr,200.f,10.f,10.f);
    relative_to_screen(figures[4],nullptr,230.f,10.f,10.f);
    relative_to_screen(figures[5],nullptr,180.f+width*.5f,frame_height*.5f,18.f);
    // Mode areas stay fixed with the toolbar; body membership follows the current viewport.
    for(unsigned i=1;i<4;++i){auto* shift_xy=reinterpret_cast<LONG*>(static_cast<std::uint8_t*>(areas[i])+0x28);shift_xy[0]=shift_xy[1]=0;}
    RECT unused{};scroll_text_set_rect(const_cast<void*>(text),nullptr,&unused);
    t_message_layout=false;
}

struct HeaderMesh {std::uint64_t generation=0;DockSide side=DockSide::floating;float width=0.f,height=0.f,body_width=0.f,body_height=0.f;};
std::array<HeaderMesh,2> g_header_mesh{};
void refresh_docked_headers() noexcept {
    const auto generation=bridge_hud_screen_generation();if(!generation || !g_make_bar)return;
    for(unsigned i=0;i<2;++i) {
        const unsigned panel=i==0?unsigned(HudPanel::minimap):unsigned(HudPanel::character_info);
        const auto* object=current_panel(panel);if(!object)continue;
        const auto header=header_layout(HudPanel(panel));const float unit=g_layout.unit*header.scale;
        const float width=header.width/unit,height=header.height/unit;
        const float body_unit=g_layout.unit*g_layout.scale*g_hud_panels[panel].scale_percent/100.f;
        const float body_width=header.background.width/body_unit,body_height=header.background.height/body_unit;
        auto& mesh=g_header_mesh[i];
        if(mesh.generation==generation && mesh.side==header.side && mesh.width==width && mesh.height==height &&
           mesh.body_width==body_width && mesh.body_height==body_height)continue;
        const auto* array=*reinterpret_cast<void* const* const*>(object+0x108);
        auto* area=(*reinterpret_cast<const std::uint8_t* const* const*>(object+0x128))[i==0?6:5];
        const auto* shift=reinterpret_cast<const LONG*>(area+0x28);
        const auto previous=t_anchor;const auto previous_composition=widescreen::hud_blit;
        t_anchor=kPanelRoles[panel].anchor;
        const float x=i==0?790.f:10.f,y=i==0?80.f:110.f;
        make_bar(array[i==0?4:1],nullptr,20.f,100.f,x-width*.5f,y-height*.5f,x+width*.5f,y+height*.5f,0,0,255,255);
        relative_to_screen(array[i==0?4:1],nullptr,x+float(shift[0]),y,20.f);
        // ScrollFigures restores these native centres/depths each idle. Build
        // geometry around the same owner values, so idle cannot shrink the fill.
        const float centre_x=i==0?700.f:100.f,centre_y=i==0?80.f:110.f,depth=i==0?20.f:60.f;
        make_bar(array[i==0?3:0],nullptr,depth,100.f,centre_x-body_width*.5f,centre_y-body_height*.5f,
                 centre_x+body_width*.5f,centre_y+body_height*.5f,0,0,255,255);
        relative_to_screen(array[i==0?3:0],nullptr,centre_x+float(shift[0]),centre_y,depth);
        t_anchor=previous;widescreen::hud_blit=previous_composition;
        mesh={generation,header.side,width,height,body_width,body_height};
    }
}

// --- Widget method anchors -----------------------------------------------------------
//
// Each wrapped method entry jumps to a stub `push index; jmp widget_entry`.
// widget_entry records the caller's return address and the previous anchor,
// sets the method's anchor and enters the method through its forwarder with
// widget_exit as the return address; widget_exit restores the anchor (and
// moves a rect the method stored) and returns to the real caller. This works
// for any argument count and keeps eax/edx/st0.

struct AnchorFrame { void* caller; Anchors previous; widescreen::HudComposition composition, input_composition; InputScope input_scope; const void* visual_object; const void* input_object; void* object; std::uint32_t method; RECT rect; };
// Spell bar methods after construction. Stock ones never write the object
// rect; the addon's extended spell bar rewrites it (720,0,800,600, raw) as a
// hit rect for its own columns while building its slots, which still end at
// the stock 510. The owned bar is anchored by the constructed rect and does
// its own hit testing, so the outermost spell-bar call puts that rect back.
bool spell_rect_method(std::uintptr_t va) noexcept {
    return va == 0x0063E4B0 || va == 0x0063E820 || va == 0x0063E750 || va == 0x0063EC90;
}
constexpr int kAnchorDepth = 128;
thread_local AnchorFrame t_frames[kAnchorDepth];
thread_local int t_depth = 0;
void* g_widget_forwarders[kWidgetMethodCount]{};
void* g_widget_stubs[kWidgetMethodCount]{};

void adjust_rect(void* object) noexcept {
    auto* rect = reinterpret_cast<LONG*>(static_cast<std::uint8_t*>(object) + kObjectRectOffset);
    rect[0] = std::lround(placed_x(static_cast<float>(rect[0])));
    rect[1] = std::lround(placed_y(static_cast<float>(rect[1])));
    rect[2] = std::lround(placed_x(static_cast<float>(rect[2])));
    rect[3] = std::lround(placed_y(static_cast<float>(rect[3])));
}

void set_anchor(const Anchors& anchor) noexcept {
    t_anchor = anchor;
    widescreen::hud_blit = widescreen::hud_composition(g_layout, anchor);
}
// CI3DObject::Draw2D serves every interface object: the HUD widget classes are
// told apart by their vtable (class identity, a constant of the image).
Anchors vtable_anchor(const void* object, bool inherit = true) noexcept {
    __try {
        const auto vtable = reinterpret_cast<std::uintptr_t>(*static_cast<void* const*>(object));
        for (const auto& entry : kWidgetVtables)
            if (vtable == reinterpret_cast<std::uintptr_t>(at(entry.va))) return entry.anchor;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return inherit ? t_anchor : Anchors{};
}
widescreen::HudComposition message_input_composition() noexcept {
    auto h=message_panel_composition();const auto header=header_layout(HudPanel::messages);
    clip_to_panel(h,header);
    const void* output=nullptr;const void* text=nullptr;
    if(current_message_panel(output,text))compose_message_body(output,h);
    return h;
}
// Draw owns one stack argument; Draw2D owns none. Keep their omission ABIs separate.
BOOL __fastcall omit_message_draw(void*,void*,void*) { return TRUE; }
void __fastcall omit_message_draw2d(void*,void*) {}
void __fastcall draw_object_surfaces(void* object,void*) {
    const auto* bytes=static_cast<const std::uint8_t*>(object);
    const auto* array=*reinterpret_cast<const std::uint8_t* const* const*>(bytes+0x118);
    const auto count=*reinterpret_cast<const unsigned*>(bytes+0x11C);
    const bool weapons=object==current_panel(unsigned(HudPanel::weapons));
    const auto previous=widescreen::hud_blit;
    auto* critical=reinterpret_cast<CRITICAL_SECTION*>(at(0x007AEE78));
    EnterCriticalSection(critical);
    HRESULT result=S_OK;
    for(unsigned i=0;i<count;++i) {
        const auto* surface=array[i];const auto flags=*reinterpret_cast<const unsigned*>(surface+0x0C);
        if(!(flags&4))continue;
        const auto* wrapper=*reinterpret_cast<const std::uint8_t* const*>(surface+0x10);
        if(!wrapper)continue;
        const auto* owner=*reinterpret_cast<const std::uint8_t* const*>(wrapper+0x0C);
        const auto* target_wrapper=*reinterpret_cast<const std::uint8_t* const*>(owner+0x18);
        auto* source=*reinterpret_cast<IDirectDrawSurface7* const*>(wrapper+0x10);
        auto* target=*reinterpret_cast<IDirectDrawSurface7* const*>(target_wrapper+0x10);
        if(source==target)continue;
        RECT destination=*reinterpret_cast<const RECT*>(surface+0x24);
        RECT source_rect{0,0,destination.right-destination.left,destination.bottom-destination.top};
        if(weapons) {
            auto h=object_composition(object,kPanelRoles[unsigned(HudPanel::weapons)].anchor);
            if(!bar_slot_composition(unsigned(HudPanel::weapons),i,object,h))continue;
            const auto* area=(*reinterpret_cast<const std::uint8_t* const* const*>(bytes+0x128))[i];
            const auto r=*reinterpret_cast<const RECT*>(area+0x14);
            const auto* shift=reinterpret_cast<const LONG*>(area+0x28);
            // The number source is indexed by weapon ordinal, but its addon
            // destination predates owned slot layout. Place its authored local
            // text at this weapon's left edge and lower half before composition.
            const LONG left=static_cast<LONG>(std::lround(widescreen::placed_to_screen_x(g_layout,float(r.left+shift[0]))));
            const LONG top=static_cast<LONG>(std::lround(widescreen::placed_to_screen_y(g_layout,float(r.bottom-40+shift[1]))));
            destination={left,top,left+source_rect.right,top+source_rect.bottom};
            widescreen::hud_blit=h;
        }
        DDBLTFX fx{};fx.dwSize=sizeof(fx);
        fx.ddckSrcColorkey.dwColorSpaceLowValue=fx.ddckSrcColorkey.dwColorSpaceHighValue=*reinterpret_cast<const DWORD*>(surface+0x34);
        const HRESULT hr=target->Blt(&destination,source,(flags&2)?nullptr:&source_rect,
            DDBLT_WAIT|DDBLT_ASYNC|((flags&1)?DDBLT_KEYSRCOVERRIDE:0),&fx);
        widescreen::hud_blit=previous;
        if(FAILED(hr))result=hr;
    }
    LeaveCriticalSection(critical);
    static bool reported=false;
    if(FAILED(result) && !reported){event("hud_text_blit_failed","Owned HUD text surface submission failed.");reported=true;}
}
// Keep native tooltip text/activation ownership. Only extend its current backing
// for the clock line, then submit the complete surface beside the scaled widget.
bool campaign_minutes(int& minute) noexcept {
    // GSGetVar, also used by save/global-map time. Its receiver is the native
    // script interface; local environment.hour intentionally loses the day.
    static int contract=0;
    if (!contract) {
        std::uint8_t expected[]={0xb9,0,0,0,0,0xe8,0x96,0x0e,0xff,0xff,0x8b,0x40,0x1c,
            0x8b,0x4c,0x24,0x04,0x51,0x8d,0x88,0xc4,0x00,0x00,0x00,0xe8,0x83,0x7f,0xee,0xff,0xc2,0x04,0x00};
        const auto address=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(at(0x007AF5B8)));
        std::memcpy(expected+1,&address,4);
        contract=std::memcmp(at(0x00578B50),expected,sizeof(expected))==0 ? 1 : -1;
        if (contract<0) event("clock_time_unavailable","Campaign-time getter contract mismatch; clock date omitted.");
    }
    if (contract<0) return false;
    __try {
        using GetVar=float(__thiscall*)(void*,const char*);
        return widescreen::campaign_minute(double(reinterpret_cast<GetVar>(at(0x00578B50))(at(0x007AFEA5),"gtime")),minute);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void __fastcall draw_clock_tooltip(void* object,void*) {
    auto* tip=static_cast<std::uint8_t*>(object);
    if(!*reinterpret_cast<const unsigned*>(tip+0x11C))return;
    auto* surface=(*reinterpret_cast<std::uint8_t***>(tip+0x118))[0];
    if(!surface)return;
    int minute=0;
    if(campaign_minutes(minute)) {
        const int native_width=(std::max)(*reinterpret_cast<const int*>(tip+0x174),*reinterpret_cast<const int*>(tip+0x160));
        const int native_height=*reinterpret_cast<const int*>(tip+0x178);
        const int count=*reinterpret_cast<const int*>(tip+0x150);
        const int line_height=*reinterpret_cast<const int*>(tip+0x15C);
        const auto text_layout=widescreen::clock_text_layout(native_height,count,line_height);
        const int top=text_layout.top,height=text_layout.height;
        // Native creation rounds rectangle endpoints separately; compare with
        // our required extended backing, not a rounded native-height estimate.
        const bool new_surface=*reinterpret_cast<const unsigned*>(surface+0x18)!=unsigned(local_pixels(height));
        if(new_surface || g_clock_tip_area!=g_clock_hover_area || g_clock_tip_minute!=minute) {
            char time[96]{};
            // Native GDI text is CP1251. Requested Russian heading and the
            // same campaign day/hour/minute arithmetic used in save labels.
            widescreen::format_clock_time(time,sizeof(time),minute);
            using SurfaceDC=void(__thiscall*)(void*);
            const auto get_dc=reinterpret_cast<SurfaceDC>(at(0x005FDC00));
            const auto release_dc=reinterpret_cast<SurfaceDC>(at(0x005FDC60));
            get_dc(surface);
            const auto* owner=*reinterpret_cast<const std::uint8_t* const*>(surface+4);
            const auto* font_object=*reinterpret_cast<const std::uint8_t* const*>(owner+0x70);
            const HFONT font=*reinterpret_cast<HFONT const*>(font_object+4);
            const HDC dc=*reinterpret_cast<HDC const*>(surface+8);
            const HGDIOBJ previous=dc?SelectObject(dc,font):nullptr;
            SIZE extent{};
            const bool measured=previous && previous!=HGDI_ERROR && GetTextExtentPoint32A(dc,time,int(std::strlen(time)),&extent);
            if(previous && previous!=HGDI_ERROR)SelectObject(dc,previous);
            release_dc(surface);
            bool replaced=false;
            const int width=widescreen::clock_text_width(g_layout,native_width,extent.cx);
            if(measured && resize_text_surface(object,local_pixels(width),local_pixels(height),replaced)) {
                reinterpret_cast<void(__thiscall*)(void*)>(at(0x0065E880))(object);
                get_dc(surface);
                if(*reinterpret_cast<HDC const*>(surface+8)) {
                    RECT line{6,top,width-6,top+line_height};
                    using DrawText=int(__thiscall*)(void*,const char*,RECT*,int,COLORREF,int,BOOL);
                    reinterpret_cast<DrawText>(at(0x005FDE20))(surface,time,&line,0,RGB(228,215,167),DT_LEFT,FALSE);
                    g_clock_tip_area=g_clock_hover_area;g_clock_tip_minute=minute;
                }
                release_dc(surface);
            } else {
                static bool reported=false;
                if(!reported){event("clock_time_unavailable","Native clock tooltip time line could not be prepared.");reported=true;}
            }
        }
    }
    const auto& rect=*reinterpret_cast<const RECT*>(surface+0x24);
    HudPanelBounds clock;
    if(!widescreen_hud_panel_bounds(HudPanel::game_speed,clock))return;
    // Match the native-size tooltip policy; only its placement follows the HUD.
    const float width=float(rect.right-rect.left),height=float(rect.bottom-rect.top);
    const float gap=4.f*g_layout.unit;
    const float left=std::clamp(clock.left+clock.width-width,0.f,(std::max)(0.f,g_layout.width-width));
    const float top=std::clamp(clock.top-height-gap,0.f,(std::max)(0.f,g_layout.height-height));
    widescreen::hud_blit={1.f,float(rect.left),float(rect.top),1.f,left-float(rect.left),top-float(rect.top)};
    draw_object_surfaces(object,nullptr);
}
widescreen::HudComposition message_text_composition(const void* object) noexcept {
    auto h=message_panel_composition();
    const auto* bytes=static_cast<const std::uint8_t*>(object);
    const auto* surface=(*reinterpret_cast<const std::uint8_t* const* const*>(bytes+0x118))[0];
    const auto* rect=reinterpret_cast<const RECT*>(surface+0x24);
    const float x=float(rect->left),y=float(rect->top);
    const float final_x=widescreen::compose_x(x,h),final_y=widescreen::compose_y(y,h);
    h.scale*=g_message_panel.text_scale_percent/100.f;h.scale_y=h.scale;
    h.pivot_x=x;h.pivot_y=y;h.offset_x=final_x-x;h.offset_y=final_y-y;
    const auto* r=reinterpret_cast<const RECT*>(bytes+kObjectRectOffset);
    auto panel_h=message_panel_composition();const void* output=nullptr;const void* text=nullptr;
    if(current_message_panel(output,text)){compose_message_body(output,h);compose_message_body(output,panel_h);}
    h.clip_left=(std::max)(h.clip_left,widescreen::compose_x(widescreen::placed_to_screen_x(g_layout,float(r->left)),panel_h));
    h.clip_right=(std::min)(h.clip_right,widescreen::compose_x(widescreen::placed_to_screen_x(g_layout,float(r->right-14)),panel_h));
    h.clip_top=(std::max)(h.clip_top,widescreen::compose_y(widescreen::placed_to_screen_y(g_layout,float(r->top)),panel_h));
    h.clip_bottom=(std::max)(h.clip_top,(std::min)(h.clip_bottom,widescreen::compose_y(widescreen::placed_to_screen_y(g_layout,float(r->bottom)),panel_h)));
    return h;
}
BOOL __fastcall clipped_membership(void*,void*,POINT) { return FALSE; }
BOOL __fastcall docked_membership(void* object,void*,POINT point) {
    const int index=panel_index(object);const auto kind=index>=0?HudPanel(index):HudPanel::messages;
    const auto header=header_layout(kind);auto b=header.panel;
    HudAssetFrame frame;if(widescreen_hud_asset_frame(kind,frame))b=frame.bounds;
    HudPanelBounds controls;widescreen_hud_header_bounds(kind,controls);
    const bool on_controls=point.x>=controls.left && point.x<controls.left+controls.width && point.y>=controls.top && point.y<controls.top+controls.height;
    if(slide_fraction(kind,object)>=1.f)return on_controls;
    return on_controls || (point.x>=b.left && point.x<b.left+b.width && point.y>=b.top && point.y<b.top+b.height);
}
BOOL __fastcall bar_membership(void* object,void*,POINT point) {
    const int index=panel_index(object);HudPanelBounds b;
    if(index<0 || !widescreen_hud_panel_bounds(HudPanel(index),b))return FALSE;
    const unsigned slots=index==int(HudPanel::items)?8u:native_slot_count(unsigned(index),object);
    for(unsigned i=0;i<slots;++i) {
        if(!active_slot(unsigned(index),i,object))continue;
        HudPanelBounds slot;if(!slot_screen_bounds(unsigned(index),i,object,slot))continue;
        if(point.x>=slot.left && point.x<slot.left+slot.width && point.y>=slot.top && point.y<slot.top+slot.height)return TRUE;
    }
    return FALSE;
}
void* g_item_down=nullptr;void* g_item_double=nullptr;void* g_item_up=nullptr;void* g_item_move=nullptr;
bool item_event_inside(void* handler,POINT point) noexcept {
    const auto* object=static_cast<const std::uint8_t*>(handler)-0x94;
    return current_panel(unsigned(HudPanel::items))!=object || bar_membership(const_cast<std::uint8_t*>(object),nullptr,point);
}
void __fastcall item_down(void* handler,void*,UINT flags,POINT point) {
    if(item_event_inside(handler,point))reinterpret_cast<void(__thiscall*)(void*,UINT,POINT)>(g_item_down)(handler,flags,point);
}
void __fastcall item_double(void* handler,void*,UINT flags,POINT point) {
    if(item_event_inside(handler,point))reinterpret_cast<void(__thiscall*)(void*,UINT,POINT)>(g_item_double)(handler,flags,point);
}
void __fastcall item_move(void* handler,void*,UINT flags,POINT point) {
    if(item_event_inside(handler,point))reinterpret_cast<void(__thiscall*)(void*,UINT,POINT)>(g_item_move)(handler,flags,point);
}
void __fastcall item_up(void* handler,void*,UINT flags,POINT point) {
    if(item_event_inside(handler,point)) {
        reinterpret_cast<void(__thiscall*)(void*,UINT,POINT)>(g_item_up)(handler,flags,point);return;
    }
    // Native captured Up releases before HitTest. Preserve that lifecycle even
    // when the owned layout's padding or empty preview slot rejects the point.
    auto* ui=*reinterpret_cast<std::uint8_t**>(static_cast<std::uint8_t*>(handler)+4);
    auto** capture=reinterpret_cast<void**>(ui+0x0C);
    if(*capture==handler)*capture=nullptr;
}
void* g_map_wheel=nullptr;
BOOL __fastcall map_wheel(void* handler,void*,UINT flags,short delta,POINT point) {
    auto* object=static_cast<std::uint8_t*>(handler)-0x94;
    if(game_hud_editing())return TRUE;
    if(current_panel(unsigned(HudPanel::minimap))==object &&
       slide_fraction(HudPanel::minimap,object)<1.f && docked_membership(object,nullptr,point)) {
        auto& zoom=*reinterpret_cast<float*>(object+0x16C);
        const float maximum=*reinterpret_cast<const float*>(object+0x170);
        zoom=std::clamp(zoom*std::pow(2.5f,.1f*float(delta)/float(WHEEL_DELTA)),1.f,maximum);
        *(static_cast<std::uint8_t*>(handler)+8)=1;return TRUE;
    }
    return reinterpret_cast<BOOL(__thiscall*)(void*,UINT,short,POINT)>(g_map_wheel)(handler,flags,delta,point);
}
void* __stdcall widget_enter(std::uint32_t method, void* caller, void* object, const POINT* point) noexcept {
    if (t_depth >= kAnchorDepth) std::abort();  // a recursion the HUD never does
    t_frames[t_depth++] = {caller, t_anchor, widescreen::hud_blit, t_input_composition, t_input_scope, t_visual_object, t_input_object, object, method, {}};
    if (object && spell_rect_method(kWidgetMethods[method].va))
        std::memcpy(&t_frames[t_depth - 1].rect, static_cast<const std::uint8_t*>(object) + kObjectRectOffset, sizeof(RECT));
    const auto& entry = kWidgetMethods[method];
    if(entry.va==0x0065CF30 && point) {
        const void* output=nullptr;const void* child=nullptr;
        const char* text=*reinterpret_cast<const char* const*>(point);
        if(text && *text && current_message_panel(output,child) && object==child)
            widescreen_hud_activity(HudPanel::messages);
    }
    // ProcessIdle/DrawTips still establish delay, activation and current key.
    // Replace only this active clock tooltip surface at its normal Draw2D call.
    if(entry.va==0x005FECC0 && t_input_object==current_panel(unsigned(HudPanel::game_speed)) && object==current_clock_tip())
        return reinterpret_cast<void*>(&draw_clock_tooltip);
    if(entry.va==0x005FED00 && caller==at(0x006010A8) && g_hud_frame_sink)
        g_hud_frame_sink(g_hud_frame_owner);
    const void* primary=entry.handler_this?static_cast<std::uint8_t*>(object)-0x94:object;
    if (entry.input != InputScope::none) {
        t_input_object=primary;
        t_input_composition = message_panel_object(primary) ? message_input_composition() :
                              object_composition(primary,vtable_anchor(primary,false));
        t_input_scope = entry.input;
    } else {
        t_visual_object=object;
        set_anchor(entry.by_vtable ? vtable_anchor(object) : entry.anchor);
        if (message_panel_object(object)) {
            widescreen::hud_blit=message_panel_composition();
            const void* output=nullptr;const void* text=nullptr;
            if(current_message_panel(output,text) && text==object) {
                if((entry.va==0x005FEB90 || entry.va==0x005FECC0 || entry.va==0x0065D120) && slide_fraction(HudPanel::messages,output)>=1.f)
                    return entry.va==0x005FECC0?reinterpret_cast<void*>(&omit_message_draw2d):reinterpret_cast<void*>(&omit_message_draw);
                compose_message_body(output,widescreen::hud_blit);
            }
            if(entry.va==0x005FECC0 && current_message_panel(output,text) && text==object) {
                if(g_message_surface_failed)return reinterpret_cast<void*>(&omit_message_draw2d);
                widescreen::hud_blit=message_text_composition(object);
                return reinterpret_cast<void*>(&draw_object_surfaces);
            }
        }
        else {
            widescreen::hud_blit=object_composition(object,t_anchor);
            if(entry.va==0x005FECC0 && object==current_panel(unsigned(HudPanel::weapons)))
                return reinterpret_cast<void*>(&draw_object_surfaces);
        }
    }
    if(entry.va==0x005FEBF0 || entry.va==0x005FED00)widescreen::hud_blit.opacity=1.f;
    // IsInside receives a physical POINT by value. A clipped child rectangle
    // must not leave an invisible hit region above a relocated closed panel.
    if(entry.va==0x005FFD30) {
        const int panel=panel_index(primary);
        if(panel>=0 && slot_panel(unsigned(panel)))return reinterpret_cast<void*>(&bar_membership);
        if(panel==int(HudPanel::minimap) || panel==int(HudPanel::character_info))return reinterpret_cast<void*>(&docked_membership);
        const void* output=nullptr;const void* text=nullptr;
        if(current_message_panel(output,text) && primary==output)return reinterpret_cast<void*>(&docked_membership);
    }
    if (entry.va==0x005FFD30 && !inside_composed_screen(float(point->x),float(point->y),t_input_composition))
        return reinterpret_cast<void*>(&clipped_membership);
    return g_widget_forwarders[method];
}
void* __stdcall widget_leave() noexcept {
    const AnchorFrame frame = t_frames[--t_depth];
    if(kWidgetMethods[frame.method].va==0x0065E880 && frame.object==current_clock_tip())g_clock_tip_minute=-1;
    if(kWidgetMethods[frame.method].va==0x00649E20 || kWidgetMethods[frame.method].va==0x00649FF0)refresh_message_layout();
    if(kWidgetMethods[frame.method].va==0x00642E80 && frame.object==current_panel(unsigned(HudPanel::minimap)) && widescreen_hud_fade_enabled(HudPanel::minimap)) {
        try {
            const auto* object=static_cast<const std::uint8_t*>(frame.object);
            const std::vector<std::uint64_t> values{*reinterpret_cast<const DWORD*>(object+0x14C),*reinterpret_cast<const DWORD*>(object+0x150)};
            compare_hud_activity(HudPanel::minimap,g_hud_fade_state[unsigned(HudPanel::minimap)].native_values,values);
        } catch(const std::bad_alloc&){widescreen_hud_activity(HudPanel::minimap);}
    }
    if (kWidgetMethods[frame.method].post_rect && frame.object) adjust_rect(frame.object);
    else if (frame.object && spell_rect_method(kWidgetMethods[frame.method].va) &&
             std::memcmp(&frame.rect, static_cast<const std::uint8_t*>(frame.object) + kObjectRectOffset, sizeof(RECT)) != 0) {
        bool outermost = true;
        for (int index = 0; index < t_depth; ++index)
            outermost = outermost && !(t_frames[index].object == frame.object && spell_rect_method(kWidgetMethods[t_frames[index].method].va));
        if (outermost) std::memcpy(static_cast<std::uint8_t*>(frame.object) + kObjectRectOffset, &frame.rect, sizeof(RECT));
    }
    t_anchor=frame.previous;
    widescreen::hud_blit=frame.composition;
    t_input_composition=frame.input_composition;
    t_input_scope = frame.input_scope;
    t_visual_object=frame.visual_object;t_input_object=frame.input_object;
    return frame.caller;
}
void* g_widget_enter = reinterpret_cast<void*>(&widget_enter);
void* g_widget_leave = reinterpret_cast<void*>(&widget_leave);
void widget_exit();
void* g_widget_exit = reinterpret_cast<void*>(&widget_exit);

__declspec(naked) void widget_entry() {
    __asm {
        // [esp] = method index, [esp+4] = caller return address, ecx = this
        push ecx
        push edx
        lea eax, [esp + 16]          // original arguments (POINT for IsInside)
        push eax
        push ecx                     // object
        push dword ptr [esp + 20]    // caller
        push dword ptr [esp + 20]    // method
        call dword ptr [g_widget_enter]
        pop edx
        pop ecx
        add esp, 4                   // drop the method index
        push eax
        mov eax, dword ptr [g_widget_exit]
        mov dword ptr [esp + 4], eax // return into widget_exit
        pop eax
        jmp eax
    }
}
__declspec(naked) void widget_exit() {
    __asm {
        sub esp, 4                   // the caller's return address goes here
        push eax
        push edx
        call dword ptr [g_widget_leave]
        mov dword ptr [esp + 8], eax
        pop edx
        pop eax
        ret
    }
}

const void* owned_widget_entry(const void* entry, const void* jump_target) noexcept {
    if (entry == at(0x005FC8F0) && jump_target == reinterpret_cast<const void*>(&point_in_area))
        return jump_target;
    for (std::size_t index = 0; index < kWidgetMethodCount; ++index)
        if (entry == at(kWidgetMethods[index].va) &&
            g_widget_stubs[index] && g_widget_stubs[index] == jump_target)
            return g_widget_stubs[index];
    return nullptr;
}

// --- Tutorial page rectangles ------------------------------------------------------
//
// Shipped page rectangles identify HUD roles, not current positions or skill
// ordinals. Resolve against the mission captured immediately before this modal.
// The resulting geometry belongs to this page until its native rebuild/teardown.
const std::uint8_t* tutorial_mission(const std::uint8_t* tutorial) noexcept {
    if(!g_interface_owned || !g_image || *reinterpret_cast<void* const*>(tutorial)!=at(0x00740E28))return nullptr;
    const auto* ui=at(0x007C7D50);
    if(!ui[0x30] || *reinterpret_cast<const void* const*>(tutorial+0x98)!=ui)return nullptr;
    auto* begin=*reinterpret_cast<const std::uint8_t* const* const*>(ui+0x34);
    auto* end=*reinterpret_cast<const std::uint8_t* const* const*>(ui+0x38);
    if(end-begin<2 || end[-1]!=tutorial || *reinterpret_cast<void* const*>(end[-2])!=at(0x00740764))return nullptr;
    return end[-2];
}
struct RetailTutorialReader {
    const std::uint8_t* mission;
    const std::uint8_t* object=nullptr;
    unsigned panel=0;
    bool select(HudPanel role,HudPanelBounds& b) noexcept {
        panel=unsigned(role);
        if(role==HudPanel::messages) {
            const void* output=nullptr;const void* text=nullptr;
            if(!mission_message_panel(mission,output,text))return false;
            object=static_cast<const std::uint8_t*>(output);
            const auto bounds=message_panel_bounds();b={bounds.left,bounds.top,bounds.width,bounds.height};return true;
        }
        object=mission_panel(panel,mission);return panel_bounds(panel,object,b);
    }
    unsigned count() const noexcept {return native_slot_count(panel,object);}
    int skill_type(unsigned slot) const noexcept {
        const auto* types=*reinterpret_cast<const int* const*>(object+0x15C);
        return types && *reinterpret_cast<const unsigned*>(object+0x160)==count()?types[slot]:INT_MIN;
    }
    RECT slot_rect(unsigned slot) const noexcept {
        const auto* areas=*reinterpret_cast<const std::uint8_t* const* const*>(object+0x128);
        auto r=*reinterpret_cast<const RECT*>(areas[slot]+0x14);
        const auto* shift=reinterpret_cast<const LONG*>(areas[slot]+0x28);
        OffsetRect(&r,shift[0],shift[1]);return r;
    }
    bool slot_bounds(unsigned slot,HudPanelBounds& b) noexcept {return slot_screen_bounds(panel,slot,object,b);}
    const widescreen::Layout& layout() const noexcept {return g_layout;}
    widescreen::Anchors anchor() const noexcept {return kPanelRoles[panel].anchor;}
    widescreen::HudComposition composition() noexcept {return panel_composition(panel,object);}
    HeaderLayout header(const HudPanelBounds& b) noexcept {return header_layout(HudPanel(panel),b);}
    float slide() const noexcept {return slide_fraction(HudPanel(panel),object);}
    void visible(const HeaderLayout& h,HudPanelBounds& b) noexcept {visible_panel_bounds(HudPanel(panel),object,h,b);}
    void control_target(unsigned area,const HeaderLayout& h,float& x,float& y) noexcept {header_control_target(HudPanel(panel),area,h,x,y);}
};
bool tutorial_target_bounds(const TutorialTarget& target,const std::uint8_t* mission,HudPanelBounds& b) noexcept {
    RetailTutorialReader reader{mission};return resolve_tutorial_target(target,reader,b);
}
void __stdcall tutorial_rect(const std::uint8_t* tutorial, std::uint8_t* site_stack) noexcept {
    const TutorialTarget* target=nullptr;
    __try {
        if (!*(tutorial + kTutorialScriptOffset)) return;
        const char* name = *reinterpret_cast<const char* const*>(tutorial + kTutorialNameOffset);
        const int page = *reinterpret_cast<const int*>(tutorial + kTutorialPageOffset);
        if (!name) return;
        // Fingerprinted RedrawPage locals before AddFigure/MakeBarFrame.
        auto* left = reinterpret_cast<int*>(site_stack + 0x64);
        auto* top = reinterpret_cast<int*>(site_stack + 0x68);
        auto* right = reinterpret_cast<int*>(site_stack + 0x58);
        auto* bottom = reinterpret_cast<int*>(site_stack + 0x1c);
        target=tutorial_target(name,page,RECT{*left,*top,*right,*bottom});
        if(!target)return; // Non-HUD help retains its authored layout.
        HudPanelBounds bounds;const auto* mission=tutorial_mission(tutorial);
        if(!mission || !tutorial_target_bounds(*target,mission,bounds)) {
            *left=*top=*right=*bottom=-10000;
            event("tutorial_target_missing",target->name);return;
        }
        *left=std::lround(widescreen::screen_to_placed_x(g_layout,bounds.left));
        *top=std::lround(widescreen::screen_to_placed_y(g_layout,bounds.top));
        *right=std::lround(widescreen::screen_to_placed_x(g_layout,bounds.left+bounds.width));
        *bottom=std::lround(widescreen::screen_to_placed_y(g_layout,bounds.top+bounds.height));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if(target) {
            *reinterpret_cast<int*>(site_stack+0x64)=*reinterpret_cast<int*>(site_stack+0x68)=
                *reinterpret_cast<int*>(site_stack+0x58)=*reinterpret_cast<int*>(site_stack+0x1c)=-10000;
            event("tutorial_target_missing",target->name);
        }
    }
}
void* g_tutorial_rect = reinterpret_cast<void*>(&tutorial_rect);
void* g_add_figure = nullptr;
__declspec(naked) void tutorial_thunk() {
    __asm {
        pushad
        lea eax, [esp + 36]          // esp at the call site (pushad 32 + our return 4)
        push eax
        push esi                     // the tutorial screen
        call dword ptr [g_tutorial_rect]
        popad
        push 0
        mov ecx, esi
        call dword ptr [g_add_figure]
        ret
    }
}

// --- Values that follow the surface --------------------------------------------------

float g_font_scale = 0.01f;          // CInterface3D::Init fScale multiplier
std::int32_t g_map_dx = -700, g_map_dy = -80, g_map_inverse_scale = 65536;
__declspec(naked) void map_centre_a() {
    __asm {
        add eax, dword ptr [g_map_dx]
        imul eax, dword ptr [g_map_inverse_scale]
        sar eax, 16
        add ecx, dword ptr [g_map_dy]
        imul ecx, dword ptr [g_map_inverse_scale]
        sar ecx, 16
        ret
    }
}
__declspec(naked) void map_centre_b() {
    __asm {
        add edx, dword ptr [g_map_dx]
        imul edx, dword ptr [g_map_inverse_scale]
        sar edx, 16
        add eax, dword ptr [g_map_dy]
        imul eax, dword ptr [g_map_inverse_scale]
        sar eax, 16
        ret
    }
}

void apply_hud_scale_values() noexcept {
    // Commands use original event pixels, so this direct minimap conversion
    // follows the desired visual centre and inverts its scale.
    g_map_dx = -static_cast<std::int32_t>(std::lround(widescreen::layout_to_placed_x(g_layout, kTopRight, 700.0f)));
    g_map_dy = -static_cast<std::int32_t>(std::lround(widescreen::layout_to_placed_y(g_layout, kTopRight, 80.0f)));
    float scale=g_layout.scale;
    if(const auto* object=current_panel(unsigned(HudPanel::minimap))) {
        const auto h=panel_body_composition(unsigned(HudPanel::minimap),object);
        const auto centre=widescreen::map_centre(g_layout,h);
        g_map_dx=centre.dx;g_map_dy=centre.dy;
        scale=h.scale;
    }
    g_map_inverse_scale = static_cast<std::int32_t>(std::lround(65536.0f / scale));
}

bool apply_interface_values() noexcept {
    const widescreen::Layout& l = g_layout;
    g_content_width = std::lround(800.0f * l.unit);
    g_content_height = std::lround(600.0f * l.unit);
    // Fonts: stock size 104 * w / (|lfHeight| * 0.01) / 800 with w the screen
    // width; the interface unit replaces w / 800.
    g_font_scale = widescreen::interface_font_scale(l);
    // Draw2D: an absolute full interface-screen surface
    // (push 0 = pixel rect; push 4; push height; push width).
    for (const auto va : kDraw2DSurfaceArgs) {
        std::uint8_t bytes[14] = {0x6a, 0x00, 0x6a, 0x04, 0x68, 0, 0, 0, 0, 0x68, 0, 0, 0, 0};
        const std::int32_t height = std::lround(l.height), width = std::lround(l.width);
        std::memcpy(bytes + 5, &height, 4);
        std::memcpy(bytes + 10, &width, 4);
        if (!write_code(at(va), bytes, sizeof(bytes))) return false;
    }
    // TextSize: measured interface pixels to local units.
    {
        std::uint8_t code[80];
        std::size_t n = 0;
        auto put = [&](std::initializer_list<std::uint8_t> bytes) { for (auto b : bytes) code[n++] = b; };
        auto put32 = [&](std::int32_t v) { std::memcpy(code + n, &v, 4); n += 4; };
        const std::int32_t width_divisor = widescreen::text_width_divisor(l), height_divisor = widescreen::text_height_divisor(l);
        put({0x8b, 0x74, 0x24, 0x30});                     // mov esi, [esp+30h]
        put({0xc7, 0x44, 0x24, 0x28, 0xff, 0xff, 0xff, 0xff});  // mov [esp+28h], -1
        put({0x69, 0xc3}); put32(800);                     // imul eax, ebx, 800
        put({0x99, 0xb9}); put32(width_divisor);           // cdq; mov ecx, 800 * unit
        put({0xf7, 0xf9, 0x89, 0x06});                     // idiv ecx; mov [esi], eax
        put({0x69, 0xc5}); put32(600);                     // imul eax, ebp, 600
        put({0x99, 0xb9}); put32(height_divisor);          // cdq; mov ecx, 600 * unit
        put({0xf7, 0xf9, 0x89, 0x46, 0x04});               // idiv ecx; mov [esi+4], eax
        put({0x8d, 0x4c, 0x24, 0x34, 0xe8});               // lea ecx, [esp+34h]; call ~CString
        put32(relative_to(at(kTextSizeInline) + n + 4, at(kStringDestructor)));
        put({0x8b, 0x4c, 0x24, 0x20, 0x8b, 0xc6, 0x5f, 0x5e, 0x5d, 0x5b});  // mov ecx,[esp+20h]; mov eax,esi; pop x4
        put({0x64, 0x89, 0x0d, 0, 0, 0, 0, 0x83, 0xc4, 0x1c, 0xc2, 0x0c, 0x00});  // mov fs:[0],ecx; add esp,1Ch; ret 0Ch
        while (n < kTextSizeInlineEnd - kTextSizeInline) code[n++] = 0x90;
        if (!write_code(at(kTextSizeInline), code, n)) return false;
    }
    // Tooltips may reach the right and bottom screen edges.
    if (!write_i32(kTooltipRight, widescreen::tooltip_right(l)) ||
        !write_i32(kTooltipRightNegative, -widescreen::tooltip_right(l)) ||
        !write_i32(kTooltipBottom, widescreen::tooltip_bottom(l))) return false;
    // Minimap clicks: placed cursor minus the placed map centre, back to
    // layout units (16.16 fixed point).
    apply_hud_scale_values();
    // The portrait's inline screen x (100 in the left-anchored window).
    return write_i32(kPortraitScreenX, widescreen::portrait_native_x(l));
}

// --- Install ---------------------------------------------------------------------------

struct Hook { std::uintptr_t va; std::uint8_t prologue; const void* replacement; void** forwarder; };

bool install_interface() noexcept {
    // Reject foreign layout bytes before any interface write. No addon
    // detection is used as permission to overwrite a changed range. An entry
    // the addon hooks for its own features (its hook-framework dispatcher) is
    // accepted and never overwritten: the forwarders below chain it.
    for (const Range& range : kStockLayoutRanges) {
        if (!stock_bytes(range.va, range.size) &&
            !(range.size >= 5 && in_file(range.va, 16) &&
              resolve_entry_owner(at(range.va), stock(range.va), 16, 5, g_addon_owned).owner ==
                  EntryOwner::framework_chain)) {
            char detail[64];
            std::snprintf(detail, sizeof(detail), "foreign_layout_at=%08X", static_cast<unsigned>(range.va));
            fail(detail);
            return false;
        }
    }
    // Full replacements, local call sites and ingress wrappers.
    const struct { std::uintptr_t va; const void* target; } replacements[] = {
        {kVirtualToScreenRectEntry, reinterpret_cast<const void*>(&virtual_to_screen_rect)},
        {kVirtualToScreenPointEntry, reinterpret_cast<const void*>(&virtual_to_screen_point)},
        {kVirtualToScreenSizeEntry, reinterpret_cast<const void*>(&virtual_to_screen_size)},
        {kScreenToVirtualRectEntry, reinterpret_cast<const void*>(&screen_to_virtual_rect)},
        {kScreenToVirtualPointEntry, reinterpret_cast<const void*>(&screen_to_virtual_point)},
        {kScreenToVirtualSizeEntry, reinterpret_cast<const void*>(&screen_to_virtual_size)},
    };
    for (const auto& replacement : replacements)
        if (!write_branch(replacement.va, 0xe9, replacement.target)) { fail("conversion_patch_failed"); return false; }
    for (const auto va : kLocalInterfaceLoads) {
        const std::uint8_t local[6] = {0x90, 0x90, 0x90, 0x90, 0x31, 0xc9};
        if (at(va)[0] != 0x8b || !write_code(at(va), local, 6)) { fail("local_site_patch_failed"); return false; }
    }
    {
        const std::uint8_t local[3] = {0x31, 0xc9, 0x90};
        if (!write_code(at(kDrawTextInterfaceLoad), local, 3)) { fail("drawtext_patch_failed"); return false; }
    }
    if (!write_branch(kCameraDeltaCall, 0xe8, reinterpret_cast<const void*>(&cursor_delta)) ||
        !write_branch(kGlobalMapDeltaCall, 0xe8, reinterpret_cast<const void*>(&cursor_delta))) {
        fail("delta_patch_failed");
        return false;
    }
    for (const auto va : kGlobalMapPixelCalls)
        if (!write_branch(va, 0xe8, reinterpret_cast<const void*>(&global_map_pixel))) { fail("global_map_patch_failed"); return false; }
    {
        const auto pointer = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&g_font_scale));
        if (!write_code(at(kFontScaleOperand), &pointer, 4)) { fail("font_patch_failed"); return false; }
    }
    if (!write_branch(kMapCentreSiteA, 0xe8, reinterpret_cast<const void*>(&map_centre_a), 8) ||
        !write_branch(kMapCentreSiteB, 0xe8, reinterpret_cast<const void*>(&map_centre_b), 9)) {
        fail("map_click_patch_failed");
        return false;
    }
    g_add_figure = at(kAddFigureEntry);
    if (!write_branch(kTutorialRectSite, 0xe8, reinterpret_cast<const void*>(&tutorial_thunk), 7)) {
        fail("tutorial_patch_failed");
        return false;
    }
    const Hook hooks[] = {
        {0x0065D0A0,7,reinterpret_cast<const void*>(&scroll_text_set_rect),&g_scroll_text_set_rect},
        {0x0063DAA0,5,reinterpret_cast<const void*>(&item_down),&g_item_down},
        {0x0063DAE0,5,reinterpret_cast<const void*>(&item_double),&g_item_double},
        {0x0063DB20,5,reinterpret_cast<const void*>(&item_up),&g_item_up},
        {0x0063E270,5,reinterpret_cast<const void*>(&item_move),&g_item_move},
        {0x00645640,5,reinterpret_cast<const void*>(&map_wheel),&g_map_wheel},
        {0x005FC8F0, 7, reinterpret_cast<const void*>(&point_in_area), &g_point_in_area},
        {kMakeRectEntry, 6, reinterpret_cast<const void*>(&make_rect), &g_make_rect},
        {kAddPointEntry, 6, reinterpret_cast<const void*>(&add_point), &g_add_point},
        {kRelativeToScreenEntry, 7, reinterpret_cast<const void*>(&relative_to_screen), &g_relative_to_screen},
        {kTranslationRelativeEntry, 7, reinterpret_cast<const void*>(&translation_relative), &g_translation_relative},
        {kMakeBarEntry, 6, reinterpret_cast<const void*>(&make_bar), &g_make_bar},
        {kMakeTilingBarEntry, 6, reinterpret_cast<const void*>(&make_tiling_bar), &g_make_tiling_bar},
        {kMakeBarFrameEntry, 6, reinterpret_cast<const void*>(&make_bar_frame), &g_make_bar_frame},
        {kMakeBarFrame2Entry, 6, reinterpret_cast<const void*>(&make_bar_frame2), &g_make_bar_frame2},
        {kMakeTriangleEntry, 6, reinterpret_cast<const void*>(&make_triangle), &g_make_triangle},
    };
    std::size_t hook_patch_bytes[std::size(hooks)]{};
    for (std::size_t index = 0; index < std::size(hooks); ++index) {
        const Hook& hook = hooks[index];
        const char* reason = "";
        *hook.forwarder = make_forwarder(hook.va, hook.prologue, hook_patch_bytes[index], reason);
        if (!*hook.forwarder) {
            char detail[96];
            std::snprintf(detail, sizeof(detail), "ingress_entry=%08X reason=%s", static_cast<unsigned>(hook.va), reason);
            fail(detail);
            return false;
        }
    }
    // Widget methods: each forwarder, stub and entry jump.
    std::size_t wrapped = 0;
    std::size_t widget_patch_bytes[kWidgetMethodCount]{};
    for (std::size_t index = 0; index < kWidgetMethodCount; ++index) {
        const WidgetMethod& method = kWidgetMethods[index];
        const char* reason = "";
        g_widget_forwarders[index] = make_forwarder(method.va, method.prologue, widget_patch_bytes[index], reason);
        std::uint8_t* stub = g_widget_forwarders[index] ? g_arena.take(10) : nullptr;
        if (!stub) {
            char detail[96];
            std::snprintf(detail, sizeof(detail), "widget_method=%08X reason=%s", static_cast<unsigned>(method.va), reason);
            fail(detail);
            return false;
        }
        stub[0] = 0x68;
        const auto method_index = static_cast<std::uint32_t>(index);
        std::memcpy(stub + 1, &method_index, 4);
        stub[5] = 0xe9;
        const std::int32_t relative = relative_to(stub + 10, reinterpret_cast<const void*>(&widget_entry));
        std::memcpy(stub + 6, &relative, 4);
        g_widget_stubs[index] = stub;
        ++wrapped;
    }
    if (!g_arena.seal()) { fail("code_arena_seal_failed"); return false; }
    for (std::size_t index = 0; index < std::size(hooks); ++index)
        if (!write_branch(hooks[index].va, 0xe9, hooks[index].replacement, hook_patch_bytes[index])) { fail("ingress_patch_failed"); return false; }
    for (std::size_t index = 0; index < kWidgetMethodCount; ++index)
        if (g_widget_stubs[index] && !write_branch(kWidgetMethods[index].va, 0xe9, g_widget_stubs[index], widget_patch_bytes[index])) {
            fail("widget_patch_failed");
            return false;
        }
    // Later detours at these entries (the engine bridge's HUD hooks) forward
    // to our wrappers as they forward to addon dispatchers.
    set_owned_entry_resolver(&owned_widget_entry);
    char detail[96];
    std::snprintf(detail, sizeof(detail), "stock_vanilla widgets=%u/%u ui_scale=%d",
                  static_cast<unsigned>(wrapped),
                  static_cast<unsigned>(kWidgetMethodCount), g_settings.ui_scale_percent);
    event("widescreen_interface_installed", detail);
    return true;
}

} // namespace

void widescreen_install_image(std::uint8_t* image, std::vector<std::uint8_t> file,
                              bool (*addon_owned)(const void*), const WidescreenSettings& settings) noexcept {
    if (g_installed) return;
    g_installed = true;
    g_settings = settings;
    g_image = image;
    g_file = std::move(file);
    g_addon_owned = addon_owned;
    // A failed takeover restores every owned patch, which puts the addon's
    // own layout back; only a failed restore is shown as a banner.
    // Either way the addon keeps its own layout and ours is off.
    const auto roll_back_takeover = [](const char* reason) noexcept {
        bridge_note_feature_off(bridge_feature_off_hud_layout);
        if (widescreen_uninstall()) event("widescreen_addon_owned", reason);
        else { g_addon_takeover = false; fail("addon_takeover_rollback_failed"); }
        g_addon_takeover = false;
    };
    if (image) {
        std::int32_t shift = 0;
        const char* reason = nullptr;
        char detail[64]{};
        const std::size_t restore = plan_addon_takeover(shift, reason, detail, sizeof(detail));
        if (reason) {
            // The addon keeps its own layout; nothing was written.
            bridge_note_feature_off(bridge_feature_off_hud_layout);
            event("widescreen_addon_owned", reason);
            g_image = nullptr;
            std::vector<std::uint8_t>().swap(g_file);
            return;
        }
        g_addon_takeover = shift != 0;
        if (restore && !restore_addon_layout(shift)) {
            fail("addon_layout_restore_failed");
            if (g_addon_takeover) { roll_back_takeover("addon_layout_restore_failed"); return; }
            widescreen_uninstall();
            return;
        }
        if (restore) {
            std::snprintf(detail, sizeof(detail), "restored_sites=%u virtual_shift=%d spell_shift=%s",
                          static_cast<unsigned>(restore), static_cast<int>(shift),
                          !shift ? "none" : g_addon_virtual_shift ? "cleared" : "not_found");
            event(shift ? "widescreen_addon_takeover" : "widescreen_addon_draw_radius", detail);
        }
    }
    if (!image || !install_world()) {
        fail("clip_scale_entry_foreign");
        if (g_addon_takeover) { roll_back_takeover("takeover_world_failed"); return; }
        widescreen_uninstall();
        return;
    }
    if (!install_interface() || !apply_draw_square() || !apply_interface_values()) {
        fail("installation_incomplete");
        if (g_addon_takeover) { roll_back_takeover("takeover_install_failed"); return; }
        widescreen_uninstall();
        return;
    }
    g_addon_takeover = false;
    g_interface_owned = true;
    widescreen::interface_owned = true;
}

void widescreen_install(bool sites_ok, const WidescreenSettings& settings) noexcept {
    if (g_installed) return;
    if (!sites_ok) { g_installed = true; fail("site_bytes_mismatch"); return; }
    std::vector<std::uint8_t> file;
    const std::wstring path = running_image_path();
    if (path.empty() || !read_whole_file(path.c_str(), file)) { g_installed = true; fail("image_unreadable"); return; }
    widescreen_install_image(reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr)), std::move(file),
                             &addon_module_contains, settings);
}

void widescreen_set_surface(unsigned width, unsigned height) noexcept {
    if (!width || !height) return;
    if(g_layout.width!=float(width) || g_layout.height!=float(height)) {
        g_message_mesh_generation=0;g_header_mesh={};
    }
    g_layout = widescreen::make_layout(static_cast<float>(width), static_cast<float>(height), g_settings.hud,
                                       static_cast<float>(g_settings.ui_scale_percent) / 100.0f);
    if (!g_interface_owned) return;
    if (!apply_draw_square() || !apply_interface_values()) {
        // The bridge may already forward into our widgets. Keep their code
        // alive until bridge teardown, and make this failed layout visible.
        fail("surface_layout_write_failed");
        return;
    }
    g_surface_known = true;
}

const char* widescreen_display_blocker() noexcept {
    if(!g_interface_owned || !g_surface_known)return "Live display Apply needs the owned widescreen interface.";
    __try {
        const auto* ui=at(0x007C7D50);
        if(!*reinterpret_cast<const void* const*>(ui+8))return "The native display owner is unavailable.";
    } __except(EXCEPTION_EXECUTE_HANDLER){return "The native display owner graph could not be read.";}
    return nullptr;
}

static POINT display_placed_shift(const widescreen::Layout& before,const widescreen::Layout& after,Anchors anchor) noexcept {
    // Absolute rounded pivots make integer motion telescope on a return trip.
    return {std::lround(widescreen::anchor_pivot_x(after,anchor.x))-std::lround(widescreen::anchor_pivot_x(before,anchor.x)),
            std::lround(widescreen::anchor_pivot_y(after,anchor.y))-std::lround(widescreen::anchor_pivot_y(before,anchor.y))};
}
static POINT display_surface_shift(const widescreen::Layout& before,const widescreen::Layout& after,Anchors anchor) noexcept {
    return {std::lround(widescreen::placed_to_screen_x(after,widescreen::anchor_pivot_x(after,anchor.x)))-
            std::lround(widescreen::placed_to_screen_x(before,widescreen::anchor_pivot_x(before,anchor.x))),
            std::lround(widescreen::placed_to_screen_y(after,widescreen::anchor_pivot_y(after,anchor.y)))-
            std::lround(widescreen::placed_to_screen_y(before,widescreen::anchor_pivot_y(before,anchor.y)))};
}
static bool display_shift_fits(LONG value,LONG shift) noexcept {
    const auto result=std::int64_t(value)+shift;
    return result>=(std::numeric_limits<LONG>::min)() && result<=(std::numeric_limits<LONG>::max)();
}
static bool display_rect_fits(const RECT& rect,POINT shift) noexcept {
    return display_shift_fits(rect.left,shift.x) && display_shift_fits(rect.right,shift.x) &&
           display_shift_fits(rect.top,shift.y) && display_shift_fits(rect.bottom,shift.y);
}
static bool display_roundable(float value) noexcept {
    return std::isfinite(value) && double(value)>=(std::numeric_limits<LONG>::min)() && double(value)<=(std::numeric_limits<LONG>::max)();
}
static bool display_round_shift_fits(float before,float after) noexcept {
    if(!display_roundable(before) || !display_roundable(after))return false;
    const auto shift=std::int64_t(std::lround(after))-std::lround(before);
    return shift>=(std::numeric_limits<LONG>::min)() && shift<=(std::numeric_limits<LONG>::max)();
}
static const char* display_object_blocker(const std::uint8_t* object,const void* ui,
                                        const widescreen::Layout& after,Anchors anchor) noexcept {
    const float bx=widescreen::anchor_pivot_x(g_layout,anchor.x),by=widescreen::anchor_pivot_y(g_layout,anchor.y);
    const float ax=widescreen::anchor_pivot_x(after,anchor.x),ay=widescreen::anchor_pivot_y(after,anchor.y);
    if(!display_round_shift_fits(bx,ax) || !display_round_shift_fits(by,ay) ||
       !display_round_shift_fits(widescreen::placed_to_screen_x(g_layout,bx),widescreen::placed_to_screen_x(after,ax)) ||
       !display_round_shift_fits(widescreen::placed_to_screen_y(g_layout,by),widescreen::placed_to_screen_y(after,ay)))
        return "The native interface anchor is out of range. Display unchanged.";
    const POINT placed=display_placed_shift(g_layout,after,anchor),pixels=display_surface_shift(g_layout,after,anchor);
    if(!display_rect_fits(*reinterpret_cast<const RECT*>(object+kObjectRectOffset),placed))
        return "The native widget rectangle is out of range. Display unchanged.";
    const float cx=(ax-bx)*(1.f/400.f)*widescreen::kViewingAngleTan;
    const float cy=(ay-by)*(1.f/400.f)*widescreen::kViewingAngleTan;
    auto* figures=*reinterpret_cast<const std::uint8_t* const* const*>(object+0x108);
    const unsigned figure_count=*reinterpret_cast<const unsigned*>(object+0x10C);
    if(figure_count && !figures)return "The native widget figure array is unavailable.";
    for(unsigned n=0;n<figure_count;++n){
        if(!figures[n])return "A native widget figure is unavailable.";
        const auto* cp=reinterpret_cast<const float*>(figures[n]+0x1C);
        if(!std::isfinite(cp[0]) || !std::isfinite(cp[1]) || !std::isfinite(cp[2]) ||
           !std::isfinite(cp[0]+cx*cp[2]) || !std::isfinite(cp[1]+cy*cp[2]))
            return "A native widget figure placement is out of range. Display unchanged.";
    }
    auto* translations=*reinterpret_cast<const std::uint8_t* const* const*>(object+0x138);
    const unsigned translation_count=*reinterpret_cast<const unsigned*>(object+0x13C);
    if(translation_count && !translations)return "The native interface preview array is unavailable.";
    for(unsigned n=0;n<translation_count;++n){
        if(!translations[n])return "A native interface preview is unavailable.";
        const auto* unit=*reinterpret_cast<const std::uint8_t* const*>(translations[n]);
        if(!unit)continue; // A constructed translation may not yet have an Init owner.
        const auto* cp=reinterpret_cast<const float*>(unit+0x1C);
        if(!std::isfinite(cp[0]) || !std::isfinite(cp[1]) || !std::isfinite(cp[2]) ||
           !std::isfinite(cp[0]+cx*cp[2]) || !std::isfinite(cp[1]+cy*cp[2]))
            return "A native interface preview placement is out of range. Display unchanged.";
    }
    auto* surfaces=*reinterpret_cast<const std::uint8_t* const* const*>(object+0x118);
    const unsigned surface_count=*reinterpret_cast<const unsigned*>(object+0x11C);
    if(surface_count && !surfaces)return "The native widget surface array is unavailable.";
    for(unsigned n=0;n<surface_count;++n)
        if(!surfaces[n] ||
           !display_rect_fits(*reinterpret_cast<const RECT*>(surfaces[n]+0x24),pixels))
            return "A native widget surface or rectangle is unavailable.";
    auto* areas=*reinterpret_cast<const std::uint8_t* const* const*>(object+0x128);
    const unsigned area_count=*reinterpret_cast<const unsigned*>(object+0x12C);
    if(area_count && !areas)return "The native widget hit-area array is unavailable.";
    for(unsigned n=0;n<area_count;++n){
        const auto* area=areas[n];
        if(!area || *reinterpret_cast<const void* const*>(area)!=ui)
            return "A native widget hit-area owner is unavailable.";
        // The native constructor leaves an invalid area's RECT uninitialised.
        if(area[0x25] && !display_rect_fits(*reinterpret_cast<const RECT*>(area+0x14),placed))
            return "A native widget hit rectangle is out of range. Display unchanged.";
        const auto* points=*reinterpret_cast<const POINT* const*>(area+4);
        const unsigned point_count=*reinterpret_cast<const unsigned*>(area+8);
        if(point_count && !points)return "The native widget hit polygon is unavailable.";
        for(unsigned p=0;p<point_count;++p)
            if(!display_shift_fits(points[p].x,placed.x) || !display_shift_fits(points[p].y,placed.y))
                return "A native widget hit polygon is out of range. Display unchanged.";
        if(const auto* tip=*reinterpret_cast<const std::uint8_t* const*>(area+0x30))
            if(const char* blocker=display_object_blocker(tip,ui,after,anchor))return blocker;
    }
    return nullptr;
}

const char* widescreen_display_span_blocker(unsigned width,unsigned height) noexcept {
    const auto after=widescreen::make_layout(float(width),float(height),g_settings.hud,g_layout.scale);
    if(after.unit!=g_layout.unit)return "The requested native canvas changes the interface raster unit. Display unchanged.";
    __try {
        const auto* ui=at(0x007C7D50);
        const auto* dd=*reinterpret_cast<const std::uint8_t* const*>(ui+8);
        const auto& rect=*reinterpret_cast<const RECT*>(dd+0x24);
        if(!display_shift_fits(rect.left,LONG(width)) || !display_shift_fits(rect.top,LONG(height)))
            return "The native display rectangle is out of range. Display unchanged.";
        auto* begin=*reinterpret_cast<const std::uint8_t* const* const*>(ui+0x34);
        auto* end=*reinterpret_cast<const std::uint8_t* const* const*>(ui+0x38);
        for(auto* entry=begin;entry!=end;++entry){
            const auto* screen=*entry;
            if(const char* blocker=display_object_blocker(screen,ui,after,{}))return blocker;
            auto* objects=*reinterpret_cast<const std::uint8_t* const* const*>(screen+0x148);
            for(unsigned i=0;i<*reinterpret_cast<const unsigned*>(screen+0x14C);++i)
                if(const char* blocker=display_object_blocker(objects[i],ui,after,vtable_anchor(objects[i],false)))return blocker;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){return "The native interface arrays could not be read. Display unchanged.";}
    return nullptr;
}

static void refresh_display_world(unsigned width,unsigned height) noexcept {
    using GetVisualizer=void*(__cdecl*)();
    using SetClip=void(__thiscall*)(void*,RECT*,float);
    RECT clip{0,0,LONG(width),LONG(height)};
    t_display_world_refresh=true;
    __try {reinterpret_cast<SetClip>(at(0x005015A0))(reinterpret_cast<GetVisualizer>(at(0x00500960))(),&clip,0.f);}
    __finally {t_display_world_refresh=false;}
}

static void display_move_object(std::uint8_t* object,const widescreen::Layout& before,
                            const widescreen::Layout& after,Anchors anchor) noexcept {
    const POINT placed=display_placed_shift(before,after,anchor),pixels=display_surface_shift(before,after,anchor);
    // Retain current integer geometry and its consumed ingress rounding.
    // This can differ by one placed unit from a fresh native initialisation.
    OffsetRect(reinterpret_cast<RECT*>(object+kObjectRectOffset),placed.x,placed.y);
    const float cx=(widescreen::anchor_pivot_x(after,anchor.x)-widescreen::anchor_pivot_x(before,anchor.x))*(1.f/400.f)*widescreen::kViewingAngleTan;
    const float cy=(widescreen::anchor_pivot_y(after,anchor.y)-widescreen::anchor_pivot_y(before,anchor.y))*(1.f/400.f)*widescreen::kViewingAngleTan;
    auto** figures=*reinterpret_cast<std::uint8_t***>(object+0x108);
    for(unsigned n=0;n<*reinterpret_cast<unsigned*>(object+0x10C);++n){
        // SetRelativeToScreen's verified CP mapping; positive Y is screen-down.
        auto* cp=reinterpret_cast<float*>(figures[n]+0x1C);
        cp[0]+=cx*cp[2];cp[1]+=cy*cp[2];
    }
    auto** translations=*reinterpret_cast<std::uint8_t***>(object+0x138);
    for(unsigned n=0;n<*reinterpret_cast<unsigned*>(object+0x13C);++n){
        if(auto* unit=*reinterpret_cast<std::uint8_t**>(translations[n])){
            // Interface-owned previews use the same placed-to-CP mapping.
            auto* cp=reinterpret_cast<float*>(unit+0x1C);
            cp[0]+=cx*cp[2];cp[1]+=cy*cp[2];
        }
    }
    auto** surfaces=*reinterpret_cast<std::uint8_t***>(object+0x118);
    for(unsigned n=0;n<*reinterpret_cast<unsigned*>(object+0x11C);++n)
        OffsetRect(reinterpret_cast<RECT*>(surfaces[n]+0x24),pixels.x,pixels.y);
    auto** areas=*reinterpret_cast<std::uint8_t***>(object+0x128);
    for(unsigned n=0;n<*reinterpret_cast<unsigned*>(object+0x12C);++n){
        auto* area=areas[n];
        if(area[0x25])OffsetRect(reinterpret_cast<RECT*>(area+0x14),placed.x,placed.y);
        auto* points=*reinterpret_cast<POINT**>(area+4);
        for(unsigned p=0;p<*reinterpret_cast<unsigned*>(area+8);++p){points[p].x+=placed.x;points[p].y+=placed.y;}
        // +28/+2C are local slide offsets, not placed coordinates.
        if(auto* tip=*reinterpret_cast<std::uint8_t**>(area+0x30))
            display_move_object(tip,before,after,anchor);
    }
}

void widescreen_refresh_display(unsigned width,unsigned height) noexcept {
    const auto before=g_layout;
    const auto after=widescreen::make_layout(float(width),float(height),g_settings.hud,g_layout.scale);
    auto* ui=at(0x007C7D50);
    auto** begin=*reinterpret_cast<std::uint8_t***>(ui+0x34);
    auto** end=*reinterpret_cast<std::uint8_t***>(ui+0x38);
    // Modal parents remain alive and must have the same span when resumed.
    for(auto** entry=begin;entry!=end;++entry){
        auto* screen=*entry;
        display_move_object(screen,before,after,{});
        auto** objects=*reinterpret_cast<std::uint8_t***>(screen+0x148);
        for(unsigned i=0;i<*reinterpret_cast<const unsigned*>(screen+0x14C);++i)
            display_move_object(objects[i],before,after,vtable_anchor(objects[i],false));
    }
    // Keep the captured modal backdrops and their backing pixels alive. Draw
    // stretches each complete stored snapshot into the new native aperture.
    for(unsigned offset:{0x54u,0x58u})if(auto* surface=*reinterpret_cast<std::uint8_t**>(ui+offset)){
        *reinterpret_cast<RECT*>(surface+0x24)={0,0,LONG(width),LONG(height)};
        *reinterpret_cast<DWORD*>(surface+0x0C)|=2u; // BS_STRETCH
    }
    // Refresh owned composition/minimap conversion after their current native
    // owners have moved. Font units and cached raster backing extents stay fixed.
    widescreen_set_surface(width,height);
    *reinterpret_cast<unsigned*>(ui)=width;*reinterpret_cast<unsigned*>(ui+4)=height;
    auto* dd=*reinterpret_cast<std::uint8_t**>(ui+8);
    auto& native_rect=*reinterpret_cast<RECT*>(dd+0x24);native_rect.right=native_rect.left+LONG(width);native_rect.bottom=native_rect.top+LONG(height);
    using Extract=BOOL(__thiscall*)(void*);
    for(unsigned offset:{0x14u,0x18u,0x58u})if(void* wrapper=*reinterpret_cast<void**>(dd+offset))
        reinterpret_cast<Extract>(at(0x004EFEA0))(wrapper);
    refresh_display_world(width,height);
}

void widescreen_set_landscape_radius(float draw_distance) noexcept {
    const float radius=(std::max)(500.f,std::clamp(draw_distance,100.f,1000.f));
    if(radius==g_landscape_radius)return;
    g_landscape_radius=radius;
    if(g_interface_owned && g_surface_known && !apply_draw_square())fail("landscape_radius_write_failed");
}
void widescreen_set_ui_scale(int percent) noexcept {
    percent = std::clamp(percent, 50, 120);
    if (!g_interface_owned || g_settings.ui_scale_percent == percent) return;
    g_settings.ui_scale_percent = percent;
    g_layout.scale = static_cast<float>(percent) / 100.0f;
    apply_hud_scale_values();
    set_anchor(t_anchor);
}

bool widescreen_hud_header_bounds(HudPanel panel,HudPanelBounds& bounds) noexcept {
    if(panel!=HudPanel::minimap && panel!=HudPanel::character_info && panel!=HudPanel::messages)return false;
    HudPanelBounds body;if(!widescreen_hud_panel_bounds(panel,body))return false;
    const auto h=header_layout(panel);
    const void* object=current_panel(unsigned(panel));
    if(panel==HudPanel::messages){const void* text=nullptr;if(!current_message_panel(object,text))return false;}
    const bool closed=slide_fraction(panel,object)>=1.f;
    const float unit=g_layout.unit*h.scale,cw=(panel==HudPanel::messages?30.f:20.f)*unit,ch=(panel==HudPanel::messages?20.f:30.f)*unit;
    float x,y;header_control_target(panel,0,h,x,y);
    float left=x-cw*.5f,top=y-ch*.5f,right=x+cw*.5f,bottom=y+ch*.5f;
    if(!closed) {
        left=(std::min)(left,h.left);top=(std::min)(top,h.top);right=(std::max)(right,h.left+h.width);bottom=(std::max)(bottom,h.top+h.height);
        const unsigned map_areas[]={5,3,4};
        const unsigned count=panel==HudPanel::minimap?3u:panel==HudPanel::character_info?4u:2u;
        for(unsigned i=0;i<count;++i){header_control_target(panel,panel==HudPanel::minimap?map_areas[i]:i+1,h,x,y);
            left=(std::min)(left,x-cw*.5f);top=(std::min)(top,y-ch*.5f);right=(std::max)(right,x+cw*.5f);bottom=(std::max)(bottom,y+ch*.5f);}
    }
    bounds={left,top,right-left,bottom-top,(right-left)/h.scale,(bottom-top)/h.scale};return true;
}
void widescreen_apply_hud_figure_layout(const void* figure) noexcept {
    const int panel=panel_index(t_visual_object);
    if(panel<0) {
        const void* output=nullptr;const void* text=nullptr;
        if(!current_message_panel(output,text) || t_visual_object!=output)return;
        const auto* object=static_cast<const std::uint8_t*>(output);
        const auto* array=*reinterpret_cast<void* const* const*>(object+0x108);
        for(unsigned i=0;i<*reinterpret_cast<const unsigned*>(object+0x10C);++i)if(array[i]==figure) {
            auto h=message_panel_composition();
            if(i==2 || i==3 || i==4) {
                header_control_composition(HudPanel::messages,i==2?0:i-2,object,h);
                if(i==2)compose_arrow(HudPanel::messages,object,h);
            }
            else if(i==1) {
                const auto header=header_layout(HudPanel::messages);
                h.scale=h.scale_y=header.scale;
                const float x=widescreen::placed_to_screen_x(g_layout,180.f+header.panel.width/(g_layout.unit*g_layout.scale)*.5f);
                const float y=widescreen::placed_to_screen_y(g_layout,10.f);
                h.offset_x+=header.left+header.width*.5f-widescreen::compose_x(x,h);
                h.offset_y+=header.top+header.height*.5f-widescreen::compose_y(y,h);
            } else {
                compose_message_body(output,h,i==0);
            }
            widescreen::hud_blit=h;return;
        }
        return;
    }
    const auto* object=static_cast<const std::uint8_t*>(t_visual_object);
    const auto count=*reinterpret_cast<const unsigned*>(object+0x10C);
    const auto* array=*reinterpret_cast<void* const* const*>(object+0x108);
    for(unsigned i=0;i<count;++i)if(array[i]==figure) {
        auto h=object_composition(object,kPanelRoles[panel].anchor);
        if(slot_panel(unsigned(panel)) && panel!=4) {
            int slot=int(i);
            if(panel==6) {slot=-1;const auto* mapping=reinterpret_cast<const int*>(object+0x16C);
                for(unsigned j=0;j<8;++j)if(mapping[j]==int(i)){slot=int(j);break;}}
            if(slot>=0 && bar_slot_composition(unsigned(panel),unsigned(slot),object,h))widescreen::hud_blit=h;
        }
        else if(panel==int(HudPanel::portraits)) {
            const int slot=i?int((i-1)/5):portrait_camera_slot(object);
            if(slot>=0)portrait_slot_composition(unsigned(slot),object,h);
            widescreen::hud_blit=h;
        }
        else if(panel==int(HudPanel::minimap) || panel==int(HudPanel::character_info)) {
            const auto kind=HudPanel(panel);const auto header=header_layout(kind);
            int area=-1;const bool map=kind==HudPanel::minimap;
            if(i==unsigned(map?5:2))area=0;
            else if(map) {if(i==6)area=3;else if(i==7)area=4;else if(i==11)area=5;}
            else if(i>=3 && i<=6)area=int(i)-2;
            h=panel_composition(unsigned(panel),object);
            if(area>=0) {
                header_control_composition(kind,unsigned(area),object,h);
                if(area==0)compose_arrow(kind,object,h);
            }
            else if(i==unsigned(map?4:1)) {
                h.scale=h.scale_y=header.scale;
                auto* a=(*reinterpret_cast<const std::uint8_t* const* const*>(object+0x128))[map?6:5];
                const auto* shift=reinterpret_cast<const LONG*>(a+0x28);
                const float x=widescreen::placed_to_screen_x(g_layout,widescreen::layout_to_placed_x(g_layout,kPanelRoles[panel].anchor,(map?790.f:10.f)+float(shift[0]),false));
                const float y=widescreen::placed_to_screen_y(g_layout,widescreen::layout_to_placed_y(g_layout,kPanelRoles[panel].anchor,map?80.f:110.f,false));
                h.offset_x+=header.left+header.width*.5f-widescreen::compose_x(x,h);
                h.offset_y+=header.top+header.height*.5f-widescreen::compose_y(y,h);
            } else if(i==unsigned(map?9:7))clip_to_panel(h,header,true);
            else {
                h=panel_body_composition(unsigned(panel),object,i==unsigned(map?3:0));
            }
            widescreen::hud_blit=h;
        }
        return;
    }
}

void widescreen_hud_preview_slots(HudPanel panel,std::vector<HudPreviewSlot>& slots) {
    slots.clear();const unsigned index=unsigned(panel);
    if(panel==HudPanel::quest_items) {
        // Empty quest slots always show while editing: the widget has nothing else to drag.
        const auto* object=current_panel(unsigned(HudPanel::items));if(!game_hud_editing() || !object)return;
        for(unsigned i=4;i<8;++i)if(!active_slot(6,i,object)){HudPanelBounds b;if(quest_slot_bounds(i,object,b))slots.push_back({b,i-4});}
        return;
    }
    if(!game_hud_editing() || !slot_panel(index) || !g_hud_panels[index].preview)return;
    const auto* object=current_panel(index);if(!object)return;
    for(unsigned i=0;i<layout_slot_count(index,object);++i)if(!active_slot(index,i,object)) {
        HudPanelBounds b;if(slot_screen_bounds(index,i,object,b))slots.push_back({b,i});
    }
}
void widescreen_set_hud_fades(const std::array<HudFadeSettings,kHudPanels.size()>& settings,const std::array<float,2>& corner_scales,HudFadeTiming timing) noexcept {
    reset_hud_fade_lifetime();
    for(unsigned i=0;i<settings.size();++i) {
        if(settings[i].enabled!=g_hud_fades[i].enabled){auto& state=g_hud_fade_state[i];state.active_at=state.transition_at=GetTickCount64();state.opacity=state.start_opacity=state.target_opacity=1.f;}
        g_hud_fades[i]={settings[i].enabled,std::clamp(settings[i].transparency_percent,0.f,100.f)};
    }
    for(unsigned i=0;i<2;++i)g_corner_scales[i]=std::clamp(corner_scales[i],50.f,300.f);
    g_hud_fade_timing={std::clamp(timing.out_ms,0,10000),std::clamp(timing.in_ms,0,2000),std::clamp(timing.idle_ms,0,60000)};
}
bool widescreen_hud_fade_enabled(HudPanel panel) noexcept {return g_hud_fades[unsigned(panel)].enabled;}
bool widescreen_hud_hovered(HudPanel panel) noexcept {return g_hud_fade_state[unsigned(panel)].hovered;}
void widescreen_mission_activation(bool active) noexcept {g_mission_activating=active;}
void widescreen_hud_activity(HudPanel panel) noexcept {
    reset_hud_fade_lifetime();auto& state=g_hud_fade_state[unsigned(panel)];state.active_at=GetTickCount64();
    set_hud_fade_target(state,1.f,state.active_at);
}
float widescreen_hud_opacity(HudPanel panel) noexcept {
    reset_hud_fade_lifetime();const auto& settings=g_hud_fades[unsigned(panel)];
    if(!settings.enabled || game_hud_editing())return 1.f;
    return g_hud_fade_state[unsigned(panel)].opacity;
}
void widescreen_hud_content_values(HudPanel panel,const std::vector<std::uint64_t>& values) noexcept {
    if(!widescreen_hud_fade_enabled(panel))return;
    reset_hud_fade_lifetime();
    try{compare_hud_activity(panel,g_hud_fade_state[unsigned(panel)].content_values,values);}
    catch(const std::bad_alloc&){widescreen_hud_activity(panel);}
}
void widescreen_update_hud_fades(float mouse_x,float mouse_y) noexcept {
    reset_hud_fade_lifetime();g_clock_hover_area=-1;
    if(const auto* clock=current_panel(unsigned(HudPanel::game_speed));clock && mouse_x>=0.f && mouse_y>=0.f) {
        const POINT point{LONG(mouse_x),LONG(mouse_y)};
        g_clock_hover_area=reinterpret_cast<int(__thiscall*)(const void*,POINT)>(at(0x005FFCC0))(clock,point);
    }
    const auto now=GetTickCount64();
    for(unsigned i=0;i<kHudPanels.size();++i) {
        auto& state=g_hud_fade_state[i];state.hovered=false;
        advance_hud_fade(state,now);
        HudPanelBounds bounds;
        if(widescreen_hud_panel_bounds(HudPanel(i),bounds)) {
            HudAssetFrame frame;
            if(widescreen_hud_asset_frame(HudPanel(i),frame))bounds=frame.bounds;
            state.hovered=mouse_x>=bounds.left && mouse_y>=bounds.top && mouse_x<bounds.left+bounds.width && mouse_y<bounds.top+bounds.height;
            HudPanelBounds controls;
            if(widescreen_hud_header_bounds(HudPanel(i),controls)) {
                const bool on_controls=mouse_x>=controls.left && mouse_y>=controls.top && mouse_x<controls.left+controls.width && mouse_y<controls.top+controls.height;
                const void* object=current_panel(i);
                if(i==unsigned(HudPanel::messages)){const void* text=nullptr;current_message_panel(object,text);}
                state.hovered=object && slide_fraction(HudPanel(i),object)>=1.f?on_controls:state.hovered || on_controls;
            }
        }
        if(state.hovered)state.active_at=now;
        if(!g_hud_fades[i].enabled || game_hud_editing()) {
            state.opacity=state.start_opacity=state.target_opacity=1.f;state.transition_at=now;
        } else {
            // Finish a wake before fading out, including an idle delay of zero.
            const bool waking=state.target_opacity>state.start_opacity && state.opacity<state.target_opacity;
            const float target=state.hovered || waking || now-state.active_at < unsigned(g_hud_fade_timing.idle_ms)?
                1.f:1.f-g_hud_fades[i].transparency_percent/100.f;
            set_hud_fade_target(state,target,now);
        }
        if(!g_hud_fades[i].enabled)continue;
        const auto* object=current_panel(i);if(!object)continue;
        try {
            std::vector<std::uint64_t> values;
            if(i==unsigned(HudPanel::minimap)) {
                // Native camera translation, separate from heading and zoom.
                values={*reinterpret_cast<const DWORD*>(object+0x14C),*reinterpret_cast<const DWORD*>(object+0x150)};
            } else if(i==unsigned(HudPanel::stance)) {
                values={*reinterpret_cast<const DWORD*>(object+0x14C),*reinterpret_cast<const DWORD*>(object+0x150)};
            } else if(i==unsigned(HudPanel::game_speed)) {
                values={*reinterpret_cast<const DWORD*>(object+0x154),std::uint64_t(*(object+0x158)!=0)};
            } else if(i==unsigned(HudPanel::portraits)) {
                const auto* screen=*reinterpret_cast<const std::uint8_t* const*>(object+0x148);
                const auto* units=*reinterpret_cast<const std::uint8_t* const* const*>(screen+0x16C);
                const unsigned count=(std::min)(*reinterpret_cast<const unsigned*>(screen+0x170),*reinterpret_cast<const unsigned*>(object+0x14C));
                for(unsigned n=0;n<count;++n)if(const auto* unit=units[n]) {
                    values.push_back(*reinterpret_cast<const DWORD*>(unit+0x0C));
                    values.push_back(*reinterpret_cast<const DWORD*>(unit+8)&0x10u);
                    for(unsigned offset:{0x17Eu,0x180u,0x182u,0x184u})values.push_back(*reinterpret_cast<const WORD*>(unit+offset));
                }
            } else if(i==unsigned(HudPanel::character_info)) {
                if(slide_fraction(HudPanel::character_info,object)>=1.f)continue;
                auto* unit=*reinterpret_cast<const std::uint8_t* const*>(object+0x150);
                if(!unit)unit=*reinterpret_cast<const std::uint8_t* const*>(object+0x14C);
                if(unit) {
                    values.push_back(*reinterpret_cast<const DWORD*>(unit+0x0C));
                    for(unsigned offset:{0x17Eu,0x180u,0x182u,0x184u})values.push_back(*reinterpret_cast<const WORD*>(unit+offset));
                    for(unsigned n=0;n<6;++n){const auto* limb=unit+0x374+n*0x1C;
                        values.push_back(*reinterpret_cast<const DWORD*>(limb));
                        values.push_back(*reinterpret_cast<const WORD*>(limb+4));
                        values.push_back(*reinterpret_cast<const WORD*>(limb+6));}
                }
            } else if(slot_panel(i)) {
                values={*reinterpret_cast<const DWORD*>(object+0x150)};
                if(i==unsigned(HudPanel::items)) {
                    auto* unit=*reinterpret_cast<void* const*>(object+0x154);
                    for(unsigned slot=0;slot<4;++slot) {
                        // Resolve now: producer descriptors can outlive a depleted item.
                        const auto* item=unit?reinterpret_cast<const std::uint8_t*(__thiscall*)(void*,DWORD)>(at(0x00531920))(unit,slot):nullptr;
                        values.push_back(item?*reinterpret_cast<const DWORD*>(item+0x0C):0u);
                        values.push_back(item?*reinterpret_cast<const DWORD*>(item+0x48):0u);
                        if(item && *reinterpret_cast<const void* const*>(item+0x44) &&
                           reinterpret_cast<int(__thiscall*)(const void*)>(at(0x006A4230))(item)==5) {
                            values.push_back(std::uint64_t(int(*reinterpret_cast<const float*>(item+0x28))));
                            values.push_back(std::uint64_t(int(*reinterpret_cast<const float*>(item+0x2C))));
                        } else values.insert(values.end(),{0,0});
                    }
                }
            } else continue;
            compare_hud_activity(HudPanel(i),state.native_values,values);
        } catch(const std::bad_alloc&) {widescreen_hud_activity(HudPanel(i));}
    }
}

void widescreen_set_quest_items(const QuestItemSettings& settings) noexcept {
    g_quest_items.scale_percent=std::clamp(settings.scale_percent,50.f,300.f);
    g_quest_items.x_percent=settings.x_percent<0.f?-1.f:std::clamp(settings.x_percent,0.f,100.f);
    g_quest_items.y_percent=settings.y_percent<0.f?-1.f:std::clamp(settings.y_percent,0.f,100.f);
}
void widescreen_set_hud_panels(const std::array<HudPanelSettings,7>& settings) noexcept {
    for(unsigned i=0;i<settings.size();++i) {
        g_hud_panels[i].scale_percent=std::clamp(settings[i].scale_percent,50.f,300.f);
        g_hud_panels[i].x_percent=std::clamp(settings[i].x_percent,-1.f,100.f);
        g_hud_panels[i].y_percent=std::clamp(settings[i].y_percent,-1.f,100.f);
        g_hud_panels[i].buttons_scale_percent=std::clamp(settings[i].buttons_scale_percent,50.f,200.f);
        g_hud_panels[i].buttons_side=std::clamp(settings[i].buttons_side,0,5);
        g_hud_panels[i].direction=std::clamp(settings[i].direction,0,i==4?4:3);
        g_hud_panels[i].padding=std::clamp(settings[i].padding,-100.f,100.f);
        g_hud_panels[i].border_padding=std::clamp(settings[i].border_padding,-100.f,100.f);
        g_hud_panels[i].preview=settings[i].preview;
        g_hud_panels[i].preview_count=std::clamp(settings[i].preview_count,1u,i==5||i==6?4u:32u);
        g_hud_panels[i].slots_per_line=i==4?0u:(std::min)(settings[i].slots_per_line,32u);
    }
    apply_hud_scale_values();refresh_docked_headers();
}
bool widescreen_hud_panel_bounds(HudPanel panel,HudPanelBounds& bounds) noexcept {
    if(panel==HudPanel::quest_items) {
        const auto* object=current_panel(unsigned(HudPanel::items));if(!object)return false;
        bool any=game_hud_editing();for(unsigned i=4;i<8 && !any;++i)any=active_slot(6,i,object);
        return any && quest_group_bounds(object,bounds);
    }
    if(panel==HudPanel::messages) {
        MessagePanelBounds b;
        if(!widescreen_message_panel_bounds(b))return false;
        bounds={b.left,b.top,b.width,b.height,440.f*g_layout.unit,100.f*g_layout.unit};return true;
    }
    const unsigned i=unsigned(panel);
    const auto* object=current_panel(i);
    if(!object)return false;
    return panel_bounds(i,object,bounds);
}

void widescreen_hud_position_percent(HudPanel panel,float left,float top,float width,float height,float& x,float& y) noexcept {
    const unsigned i=unsigned(panel);const auto* object=current_panel(i);
    float ax=0.f,ay=0.f,cw=width,ch=height;
    if(object && slot_panel(i)) {
        const int d=slot_direction(i);const auto anchor=kPanelRoles[i].anchor;
        ax=anchor.x==Anchor::end?1.f:anchor.x==Anchor::centre?.5f:0.f;
        ay=anchor.y==Anchor::end?1.f:anchor.y==Anchor::centre?.5f:0.f;
        if(i==4 && g_hud_panels[i].direction==0)ax=.5f,ay=1.f;
        else if(d>=2)ax=d==2?0.f:1.f;else ay=d==1?0.f:1.f;
        const auto r=panel_native_rect(i,object);slot_dimensions(i,0,object,cw,ch);
        cw*=width/(std::max)(1.f,float(r.right-r.left));ch*=height/(std::max)(1.f,float(r.bottom-r.top));
    }
    x=g_layout.width>cw?(left+ax*(width-cw))*100.f/(g_layout.width-cw):0.f;
    y=g_layout.height>ch?(top+ay*(height-ch))*100.f/(g_layout.height-ch):0.f;
    x=std::clamp(x,0.f,100.f);y=std::clamp(y,0.f,100.f);
}

bool widescreen_replaces_hud_frame(const void* figure) noexcept {
    const int panel=panel_index(t_visual_object);
    const std::uint8_t* object=nullptr;HudPanel kind=HudPanel::messages;
    if(panel>=0)object=static_cast<const std::uint8_t*>(t_visual_object),kind=HudPanel(panel);
    else {
        const void* output=nullptr;const void* text=nullptr;
        if(!current_message_panel(output,text))return false;
        if(t_visual_object==text)return slide_fraction(HudPanel::messages,output)>=1.f;
        if(t_visual_object!=output)return false;
        object=static_cast<const std::uint8_t*>(output);
    }
    const unsigned count=*reinterpret_cast<const unsigned*>(object+0x10C);
    const auto* figures=*reinterpret_cast<void* const* const*>(object+0x108);
    for(unsigned i=0;i<count;++i)if(figures[i]==figure) {
        if(kind==HudPanel::portraits)return i>=4 && (i-4)%5==0;
        if(kind==HudPanel::character_info && i==8)return true; // Visual height ruler, no input area.
        if(kind!=HudPanel::minimap && kind!=HudPanel::character_info && kind!=HudPanel::messages)return false;
        const unsigned frame=kind==HudPanel::minimap?9:kind==HudPanel::character_info?7:5;
        const unsigned arrow=kind==HudPanel::minimap?5:2;
        return i==frame || (i!=arrow && slide_fraction(kind,object)>=1.f);
    }
    return false;
}
bool widescreen_hud_asset_frame(HudPanel panel,HudAssetFrame& frame) noexcept {
    // These are the only panels with a native perimeter frame. Bars/items/
    // weapons remain borderless; portraits use their per-character selection frame.
    if(panel!=HudPanel::minimap && panel!=HudPanel::character_info && panel!=HudPanel::messages)return false;
    HudPanelBounds b;if(!widescreen_hud_panel_bounds(panel,b))return false;
    const auto header=header_layout(panel);float fraction=0.f;
    {
        const void* object=panel==HudPanel::messages?nullptr:current_panel(unsigned(panel));
        if(panel==HudPanel::messages){const void* text=nullptr;if(!current_message_panel(object,text))return false;}
        // A panel whose own shell is hidden (the frame and its arrow both
        // Show(FALSE), e.g. the Dejavu notification panel at rest) has no
        // perimeter to replace. CI3DFigure+0x220 is the final Show() flag.
        const auto* bytes=static_cast<const std::uint8_t*>(object);
        const unsigned count=*reinterpret_cast<const unsigned*>(bytes+0x10C);
        const auto* figures=*reinterpret_cast<const std::uint8_t* const* const*>(bytes+0x108);
        const unsigned frame_index=panel==HudPanel::minimap?9:panel==HudPanel::character_info?7:5;
        const unsigned arrow_index=panel==HudPanel::minimap?5:2;
        if(count>frame_index && count>arrow_index && figures && figures[frame_index] && figures[arrow_index] &&
           !*reinterpret_cast<const BOOL*>(figures[frame_index]+0x220) &&
           !*reinterpret_cast<const BOOL*>(figures[arrow_index]+0x220))return false;
        fraction=slide_fraction(panel,object);visible_panel_bounds(panel,object,header,b);
    }
    unsigned edges=15;
    const float slack=edge_slack();
    if(b.left<=slack)edges&=~8u;if(b.top<=slack)edges&=~1u;
    if(b.left+b.width>=g_layout.width-slack)edges&=~2u;if(b.top+b.height>=g_layout.height-slack)edges&=~4u;
    const unsigned dock=header.side==DockSide::left && b.left<=slack?8u:
        header.side==DockSide::right && b.left+b.width>=g_layout.width-slack?2u:
        header.side==DockSide::top && b.top<=slack?1u:
        header.side==DockSide::bottom && b.top+b.height>=g_layout.height-slack?4u:0u;
    if(dock && fraction>=1.f)edges=dock;
    frame={b,5.f*g_layout.unit*g_layout.scale,edges,false,widescreen_hud_opacity(panel)};return true;
}
std::size_t widescreen_hud_portrait_slots(HudPortraitSlot* slots,std::size_t capacity) noexcept {
    const auto* object=current_panel(unsigned(HudPanel::portraits));
    if(!object || !slots)return 0;
    __try {
        const auto* screen=*reinterpret_cast<const std::uint8_t* const*>(object+0x148);
        const auto* units=*reinterpret_cast<const std::uint8_t* const* const*>(screen+0x16C);
        const auto* areas=*reinterpret_cast<const std::uint8_t* const* const*>(object+0x128);
        const unsigned count=(std::min)({*reinterpret_cast<const unsigned*>(screen+0x170),
            *reinterpret_cast<const unsigned*>(object+0x14C),*reinterpret_cast<const unsigned*>(object+0x12C),unsigned(capacity)});
        std::size_t published=0;
        for(unsigned i=0;i<count;++i) {
            if(!units[i])continue;
            auto h=panel_composition(unsigned(HudPanel::portraits),object);
            if(!portrait_slot_composition(i,object,h))continue;
            const auto r=*reinterpret_cast<const RECT*>(areas[i]+0x14);
            const auto* shift=reinterpret_cast<const LONG*>(areas[i]+0x28);
            HudPanelBounds b{widescreen::compose_x(widescreen::placed_to_screen_x(g_layout,float(r.left+shift[0])),h),
                widescreen::compose_y(widescreen::placed_to_screen_y(g_layout,float(r.top+shift[1])),h),
                float(r.right-r.left)*g_layout.unit*h.scale,float(r.bottom-r.top)*g_layout.unit*h.scale};
            slots[published++]={units[i],*reinterpret_cast<const std::int32_t*>(units[i]+0x0C),b};
        }
        return published;
    } __except(EXCEPTION_EXECUTE_HANDLER){return 0;}
}
// CI3DObjectFaces (shipped 1a4b3fe0): figures[0] is the camera marker and
// each face i owns figures 5i+1 (face), +2 HP, +3 MP, +4 selection frame,
// +5 hit flash. The faces vector begins at +0x158, stride 0x78 (PlayTex
// 0x641050); OnIdle compares (0x641461) and stores (0x641465) nCurTex at [face+0x44]
// right before ChangeFaceTexture(i, nCurTex) at 0x641470; ChangeFaceTexture
// 0x640670 reads szTexName (CString) at [face+0x40].
bool widescreen_hud_portrait_face(const void* figure,HudPortraitFace& face) noexcept {
    face={};
    if(!figure || panel_index(t_visual_object)!=int(HudPanel::portraits))return false;
    const auto* object=static_cast<const std::uint8_t*>(t_visual_object);
    __try {
        const unsigned count=*reinterpret_cast<const unsigned*>(object+0x10C);
        const auto* figures=*reinterpret_cast<const void* const* const*>(object+0x108);
        const auto* screen=*reinterpret_cast<const std::uint8_t* const*>(object+0x148);
        const auto* units=*reinterpret_cast<const std::uint8_t* const* const*>(screen+0x16C);
        const unsigned faces=(std::min)(*reinterpret_cast<const unsigned*>(object+0x14C),
            *reinterpret_cast<const unsigned*>(screen+0x170));
        const auto* states=*reinterpret_cast<const std::uint8_t* const*>(object+0x158);
        for(unsigned i=0;i<faces && 5*i+1<count;++i) {
            if(figures[5*i+1]!=figure)continue;
            if(!units[i] || !states)return false;
            const auto* state=states+std::size_t(i)*0x78;
            face.slot=i;face.unit=units[i];
            face.id=*reinterpret_cast<const std::int32_t*>(units[i]+0x0C);
            face.expression=*reinterpret_cast<const std::int32_t*>(state+0x44);
            const char* name=*reinterpret_cast<const char* const*>(state+0x40);
            if(name)for(std::size_t c=0;c+1<sizeof(face.texture) && name[c];++c)face.texture[c]=name[c];
            return true;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){face={};}
    return false;
}
// Portrait slot i's frame (its area on screen), shown or not.
static bool portrait_slot_frame(const std::uint8_t* object,unsigned i,HudAssetFrame& frame) noexcept {
    const unsigned figure_count=*reinterpret_cast<const unsigned*>(object+0x10C);
    const unsigned area_count=*reinterpret_cast<const unsigned*>(object+0x12C);
    if(i>=(std::min)(area_count,figure_count?((figure_count-1)/5):0u))return false;
    const auto* areas=*reinterpret_cast<const std::uint8_t* const* const*>(object+0x128);
    const auto r=*reinterpret_cast<const RECT*>(areas[i]+0x14);
    const auto* shift=reinterpret_cast<const LONG*>(areas[i]+0x28);
    auto h=panel_composition(unsigned(HudPanel::portraits),object);
    if(!portrait_slot_composition(i,object,h))return false;
    HudPanelBounds b{widescreen::compose_x(widescreen::placed_to_screen_x(g_layout,float(r.left+shift[0])),h),
        widescreen::compose_y(widescreen::placed_to_screen_y(g_layout,float(r.top+shift[1])),h),
        float(r.right-r.left)*g_layout.unit*h.scale,float(r.bottom-r.top)*g_layout.unit*h.scale};
    frame={b,5.f*g_layout.unit*g_layout.scale,15,true,widescreen_hud_opacity(HudPanel::portraits)};
    return true;
}
bool widescreen_hud_portrait_frame(unsigned slot,HudAssetFrame& frame) noexcept {
    const auto* object=current_panel(unsigned(HudPanel::portraits));
    return object && portrait_slot_frame(object,slot,frame);
}
void widescreen_hud_asset_frames(std::vector<HudAssetFrame>& frames) {
    frames.clear();
    for(const auto panel:{HudPanel::minimap,HudPanel::character_info,HudPanel::messages}) {
        HudAssetFrame frame;if(widescreen_hud_asset_frame(panel,frame))frames.push_back(frame);
    }
    const auto* object=current_panel(unsigned(HudPanel::portraits));
    if(!object)return;
    const unsigned figure_count=*reinterpret_cast<const unsigned*>(object+0x10C);
    const unsigned area_count=*reinterpret_cast<const unsigned*>(object+0x12C);
    const auto* figures=*reinterpret_cast<const std::uint8_t* const* const*>(object+0x108);
    const unsigned count=(std::min)(area_count,figure_count?((figure_count-1)/5):0u);
    for(unsigned i=0;i<count;++i) {
        // The final Show() flag is authoritative, including multi-selection.
        // Read current owner values here; retain no engine pointers or selection copy.
        if(!*reinterpret_cast<const BOOL*>(figures[5*i+4]+0x220))continue;
        HudAssetFrame frame;
        if(portrait_slot_frame(object,i,frame))frames.push_back(frame);
    }
}

void widescreen_set_hud_frame_sink(void* owner,void (*sink)(void*) noexcept) noexcept {
    g_hud_frame_owner=owner;g_hud_frame_sink=sink;
}
void widescreen_clear_hud_frame_sink(void* owner) noexcept {
    if(g_hud_frame_owner==owner){g_hud_frame_owner=nullptr;g_hud_frame_sink=nullptr;}
}
void widescreen_set_combat_text_scale(int percent) noexcept {
    g_combat_text_scale=std::clamp(percent,50,300)/100.f;
}
float widescreen_combat_text_scale() noexcept { return g_combat_text_scale; }
void widescreen_set_message_panel(const MessagePanelSettings& settings) noexcept {
    g_message_panel.width_percent=std::clamp(settings.width_percent,50.f,800.f);
    g_message_panel.height_percent=std::clamp(settings.height_percent,50.f,600.f);
    g_message_panel.centre_x_percent=std::clamp(settings.centre_x_percent,0.f,100.f);
    g_message_panel.top_y_percent=std::clamp(settings.top_y_percent,0.f,100.f);
    g_message_panel.text_scale_percent=std::clamp(settings.text_scale_percent,50.f,300.f);
    g_message_panel.buttons_scale_percent=std::clamp(settings.buttons_scale_percent,50.f,200.f);
    g_message_panel.buttons_side=std::clamp(settings.buttons_side,0,5);
    g_message_panel.border_padding=std::clamp(settings.border_padding,-100.f,100.f);
    refresh_message_layout();
}
bool widescreen_message_panel_bounds(MessagePanelBounds& bounds) noexcept {
    const void* output=nullptr; const void* text=nullptr;
    if (!current_message_panel(output,text)) return false;
    bounds=message_panel_bounds();return true;
}

bool widescreen_uninstall() noexcept {
    // Called before consumers install on failed startup, or after the bridge
    // restores its detours on explicit unload. Never release a live wrapper.
    if (t_depth || !restore_patches()) { fail("patch_restore_incomplete"); return false; }
    if (g_arena.base && !VirtualFree(g_arena.base, 0, MEM_RELEASE)) {
        fail("code_arena_release_failed");
        return false;
    }
    set_owned_entry_resolver(nullptr);
    g_arena = {};
    for (auto& forwarder : g_widget_forwarders) forwarder = nullptr;
    for (auto& stub : g_widget_stubs) stub = nullptr;
    g_point_in_area = nullptr;
    g_item_down=g_item_double=g_item_up=g_item_move=g_map_wheel=nullptr;
    g_hud_fade_state={};g_hud_fade_generation=0;g_clock_hover_area=-1;g_clock_tip_area=g_clock_tip_minute=-1;
    g_scroll_text_set_rect=nullptr;g_message_mesh_generation=0;g_message_mesh_width=g_message_mesh_height=0.f;g_header_mesh={};
    g_make_rect = g_add_point = g_relative_to_screen = g_translation_relative = nullptr;
    g_make_bar = g_make_tiling_bar = g_make_bar_frame = g_make_bar_frame2 = g_make_triangle = nullptr;
    g_add_figure = nullptr;
    g_interface_owned = widescreen::interface_owned = false;
    g_surface_known = g_square_owned = false;
    g_image = nullptr;
    std::vector<std::uint8_t>().swap(g_file);
    std::vector<OwnedPatch>().swap(g_patches);
    g_layout = widescreen::make_layout(800.0f, 600.0f);
    g_content_width = 800;
    g_content_height = 600;
    set_anchor({});
    t_input_composition = {};
    t_input_scope = InputScope::none;
    event("widescreen_uninstalled", "owned_patches_restored");
    return true;
}

const widescreen::Layout& widescreen_layout() noexcept { return g_layout; }
bool widescreen_interface_owned() noexcept { return g_interface_owned; }
const void* widescreen_input_area_object() noexcept {
    return g_interface_owned && t_input_scope == InputScope::area ? t_input_object : nullptr;
}
const void* widescreen_info_draw_entry() noexcept {
    for (std::size_t index = 0; index < kWidgetMethodCount; ++index)
        if (kWidgetMethods[index].va == kInfoDrawEntry) return g_widget_stubs[index];
    return nullptr;
}
const void* widescreen_virtual_to_screen_rect() noexcept {
    return reinterpret_cast<const void*>(&virtual_to_screen_rect);
}
const void* widescreen_cursor_delta() noexcept { return reinterpret_cast<const void*>(&cursor_delta); }
const std::int32_t* widescreen_content_width() noexcept { return &g_content_width; }
const std::int32_t* widescreen_content_height() noexcept { return &g_content_height; }

} // namespace owned
