#include "camp_inventory.h"
#include "camp_presentation.h"
#include "camp_session.h"
#include "model_draw_lifetime.h"
#include "model_preview_presentation.h"
#include "addon_hook_chain.h"
#include "engine_bridge.h"
#include "owned_asset_loader.h"
#include "owned_resource_runtime.h"
#include "widescreen.h"
#include "renderer.h"
#include "camp_icon_decode.h"
#include "scene_core_types.h"
#include "x64_remote_link.h"
#include "x64_remote_protocol.h"

#include <array>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <map>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#ifdef EID3D11_EMBEDDED
#include "camp_inventory_embedded.inl"
#else
namespace owned {
void bridge_append_probe_event(const char*, const char*) noexcept;
namespace {

// The supported engine's camp contracts were checked against the shipped
// BE0FC330 image and the active JMV addon registration sites. Native icon,
// preview, GDI text and gameplay operations remain temporary ingress; this
// module owns layout, cell enumeration, picking and scroll positions.
constexpr std::uintptr_t kBase = 0x400000;
std::uint8_t* game = nullptr;
camp::frontend::CampSession session;
camp::frontend::CardPublication publication;
void*& screen = session.owner; // Borrowed only during the authoritative camp session.
camp::Layout layout;
bool valid = false;
bool install_failed = false;
bool requested_wide = true;
bool applied_wide = true;
const char* failure = nullptr;
unsigned output_width = 800, output_height = 600, frame_size = 72;
bool camp_drawing = false;
bool preview_glow_admitted = false;
const char* preview_glow_failure = nullptr;
bool addon_hover_selection = false;
std::array<bool,3> cards_drawn{};
bool any_cards() noexcept {return std::any_of(cards_drawn.begin(),cards_drawn.end(),[](bool shown){return shown;});}
void* comparison_owner=nullptr; // One fully constructed native owner, never a child or input target.
bool& comparison_current=publication.comparison;
int& comparison_modifiers=publication.comparison_modifiers;
void* blueprint_owner=nullptr; // Independent copied visual Party; never participates in a transaction.
int& blueprint_modifiers=publication.blueprint_modifiers;
int& result_modifiers=publication.result_modifiers;
using ConstructorCard=camp::frontend::ConstructorCard;
ConstructorCard& constructor_card=publication.constructor;
void refresh_comparison(void* item);
void destroy_comparison() noexcept;
void* popup_sink_context = nullptr;
CampPopupSink popup_sink = nullptr;
ID3D11Device* icon_device = nullptr; // Borrowed only during the registered main-thread scene.
bool popup_failed = false;
bool popup_submit(CampRenderRequest request, CampPopupDraw draw, void* context) {
    if(popup_sink && popup_sink(popup_sink_context,request,draw,context))return true;
    if(draw)camp_inventory_popup_failed();
    return false;
}
CampDecoration decoration;
bool decoration_pending = false, art_failed = false;
int belt_first = 0;

// Only the optional addon CampDraw (alchemy) adapter path stays pinned to its
// reviewed JMV build. Every other addon owner the camp depends on is found
// from the addon's own hook-framework callbacks at our game entries, in
// whichever addon build is loaded (JMV 1.35, Dejavu), without per-build RVAs.
std::uint8_t* supported_addon = nullptr;
bool addon_icon_lookup = false;
std::array<unsigned,3> icon_catalogue_cursor{};
struct IconTabCursor { int mode = -1, category = -1; unsigned next = 0; };
std::array<IconTabCursor,3> icon_tab_cursors{};
int spell_capacity=8;
bool restore_perk_viewport() noexcept;
bool current_camp_input() noexcept;
bool write_bytes(std::uint8_t*, const void*, std::size_t) noexcept;

const std::uint8_t* addon_callback(const EntryResolution& owner) noexcept {
    return owner.owner==EntryOwner::framework_chain&&addon_module_contains(owner.callback)?owner.callback:nullptr;
}
// The addon's stdcall-to-member adapters forward their arguments to one body:
//   55 8B EC 51 8B 4D 08 E8 rel32 59 5D C2 04 00       (one argument)
//   55 8B EC FF 75 0C 8B 4D 08 E8 rel32 5D C2 08 00    (two arguments)
// Returns that body when the callback is such an adapter inside the addon.
std::uint8_t* addon_adapter_body(const EntryResolution& owner,unsigned arguments) noexcept {
    const auto* callback=addon_callback(owner);
    if(!callback)return nullptr;
    static constexpr std::uint8_t one_head[]{0x55,0x8b,0xec,0x51,0x8b,0x4d,0x08,0xe8},one_tail[]{0x59,0x5d,0xc2,0x04,0};
    static constexpr std::uint8_t two_head[]{0x55,0x8b,0xec,0xff,0x75,0x0c,0x8b,0x4d,0x08,0xe8},two_tail[]{0x5d,0xc2,0x08,0};
    const bool one=arguments==1;
    const std::size_t head=one?sizeof(one_head):sizeof(two_head);
    if(std::memcmp(callback,one?one_head:two_head,head)||
       std::memcmp(callback+head+4,one?one_tail:two_tail,one?sizeof(one_tail):sizeof(two_tail)))return nullptr;
    std::int32_t relative=0;
    std::memcpy(&relative,callback+head,sizeof(relative));
    auto* body=const_cast<std::uint8_t*>(callback)+head+4+relative;
    return addon_module_contains(body)?body:nullptr;
}
// The single occurrence of `bytes` in [begin, begin+window) of addon code.
std::uint8_t* addon_unique(std::uint8_t* begin,std::size_t window,std::initializer_list<std::uint8_t> bytes) noexcept {
    if(!begin||!addon_module_contains(begin)||!addon_module_contains(begin+window-1))return nullptr;
    std::uint8_t* found=nullptr;
    for(std::size_t i=0;i+bytes.size()<=window;++i)
        if(!std::memcmp(begin+i,bytes.begin(),bytes.size())){if(found)return nullptr;found=begin+i;}
    return found;
}

template<class T> T& field(void* p, std::size_t offset) noexcept {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(p) + offset);
}
template<class Fn> Fn function(std::uintptr_t va) noexcept {
    return reinterpret_cast<Fn>(game + va - kBase);
}
using DrawFn = BOOL(__thiscall*)(void*, void*);
using VoidFn = void(__thiscall*)(void*);
using HitFn = int(__thiscall*)(void*, POINT);
using PointFn = void(__thiscall*)(void*, POINT*);
using ItemDrawFn = void(__thiscall*)(void*, void*, void*);
void* current_person() noexcept {
    return reinterpret_cast<void*(__thiscall*)(void*)>(field<void**>(screen,0)[0xcc/4])(screen);
}
int spell_count() noexcept {
    void* person=current_person();
    const int count=person?field<int>(person,0x764):0;
    return std::clamp(count,0,spell_capacity);
}
int spell_first() noexcept {
    belt_first=camp::frontend::spell_first(belt_first,spell_count());
    return belt_first;
}

using Role = camp::frontend::Role;
using camp::frontend::role_card;
bool card_content_visible(Role role) noexcept {
    return camp::frontend::card_content_visible(role,constructor_card);
}
using ScrollList = camp::frontend::List;
using ScrollRail = camp::frontend::Rail;
ScrollRail scroll_rail(ScrollList list) noexcept;
using HoverKind=camp::frontend::HoverKind;
auto& ordinary_hover=session.hover;
int result_help=0; // Actual completed help argument for this Draw; native formatting discards it.
thread_local HoverKind hover_setter=HoverKind::none;
bool& hover_focus=session.focus;
bool& hover_controls=session.controls;
void reset_hover() noexcept {
    ordinary_hover.reset();
}
bool ordinary_owner(void* self) noexcept {
    return screen&&layout.wide&&valid&&self==field<void*>(screen,0x164);
}
bool current_result_owner(void* self) noexcept {
    return camp_drawing&&screen&&layout.wide&&valid&&self==field<void*>(screen,0x1dc);
}
thread_local Role drawing = Role::none;
thread_local void* tip_object = nullptr;
unsigned item_modifiers() noexcept {
    return ((GetKeyState(VK_CONTROL)&0x8000)?1u:0u)|((GetKeyState(VK_MENU)&0x8000)?2u:0u);
}

struct Scope {
    Role old_role = drawing;
    camp::Composition old_visual = camp::visual;
    bool old_composing = camp::composing;
    explicit Scope(Role role) noexcept { drawing = role; camp::visual = {}; camp::composing = false; }
    explicit Scope(camp::Placement p) noexcept {
        camp::visual = camp::composition(layout, p); camp::composing = true;
    }
    Scope(camp::Placement p, const camp::Rect& clip) noexcept {
        camp::visual = camp::clipped_composition(layout, p, clip); camp::composing = true;
    }
    ~Scope() { drawing = old_role; camp::visual = old_visual; camp::composing = old_composing; }
};

void reject(const char* reason) noexcept {
    camp_inventory_cancel_drag();
    reset_hover();
    comparison_current=false;
    if(!restore_perk_viewport())reason="perk_viewport_restore_failed";
    valid = false;
    camp_inventory_retire_textures();
    if (!failure) {
        failure = reason;
        bridge_append_probe_event("camp_inventory_rejected", reason);
    }
}

struct CampTexture {
    void* compatibility = nullptr; // One native reference; never an identity.
    std::string diagnostic_name;
    std::uint64_t diagnosed_visit = 0;
    std::shared_ptr<const OwnedVisualSurface> surface;
    CampTexture() = default;
    CampTexture(const CampTexture&) = delete;
    CampTexture& operator=(const CampTexture&) = delete;
    CampTexture(CampTexture&& other) noexcept
        : compatibility(std::exchange(other.compatibility, nullptr)),
          diagnostic_name(std::move(other.diagnostic_name)), diagnosed_visit(other.diagnosed_visit),
          surface(std::move(other.surface)) {}
    ~CampTexture() {
        if (surface) proxy_retire_camp_texture(surface->id, surface->generation);
        if (compatibility) function<VoidFn>(0x455980)(compatibility);
    }
};
std::map<std::pair<std::uint64_t, std::string>, CampTexture> camp_textures;
std::uint64_t camp_visit = 0;
bool close_observation_pending = false;
std::int64_t close_count_time_begin = 0;
std::int64_t close_observation_qpc = 0;

// The worker owns immutable named GPU resources. Native bindings and immediate
// context calls stay on the game thread; draw only adopts completed values.
using CampIconData = PreparedCampIcon;

// The game thread's command to the core-mode host (scene_core.h ids), sent
// in ring order with the Renderer's own commands.
template <class C> void core_send(const C& command) {
    std::vector<std::uint8_t> bytes{0};
    scene_wire::encode_into(bytes, command);
#if !defined(EID3D11_EMBEDDED) && !defined(EID3D11_SHADER_TOOL)
    x64remote::scene_message(static_cast<std::uint16_t>(scene_core::wire_command<C>::id), bytes.data(), bytes.size());
#endif
}

class CampIconLoader {
    struct Entry {
        std::shared_ptr<const CampIconData> data;
        std::string failure;
        bool reported = false, pending = true;
        std::uint64_t serial = 0; // core mode: the host request
    };
    std::mutex mutex_;
    std::condition_variable wake_;
    std::map<std::string, Entry> entries_;
    std::deque<std::string> pending_;
    std::array<std::vector<std::string>,4> visible_; // Grids plus synchronous preview/quick requests.
    std::array<std::deque<std::string>,3> selected_;
    unsigned visible_lane_ = 0, selected_lane_ = 0;
    std::array<std::thread,2> workers_;
    bool stopping_ = false;
    std::uint64_t world_epoch_ = 0;
    // Core mode (claude/s3-store): the x64 host decodes the icons and owns
    // their GPU homes (Host/core_camp.cpp). This process keeps each entry's
    // request serial and the canvas the host answered: no pixels, no texture.
    bool core_started_ = false;
    std::uint64_t core_serial_ = 0;
    long core_answers_read_ = 0;
    std::map<std::uint64_t, std::string> core_requested_; // unanswered serial -> name
    std::vector<std::uint64_t> core_priority_sent_;

    // Icon GPU homes (256..2048 square BGRA, up to 16 MB each) serve the
    // screens of one world; kept for the session they grew with every
    // inventory ever streamed. A new world epoch drops every finished or
    // queued entry; an icon a worker is decoding finishes into its kept
    // entry. Bound native textures hold their own GPU reference.
    void retire_previous_world_locked() {
        const auto epoch = bridge_current_world_epoch();
        if (epoch == world_epoch_) return;
        world_epoch_ = epoch;
        pending_.clear();
        for (auto& lane : visible_) lane.clear();
        for (auto& tab : selected_) tab.clear();
        for (auto entry = entries_.begin(); entry != entries_.end();) {
            const bool decoding = !entry->second.pending && !entry->second.data && entry->second.failure.empty();
            if (decoding) ++entry; else entry = entries_.erase(entry);
        }
    }

    void run(ResourceStoreConfig mounts, bool high_resolution, ComPtr<ID3D11Device> device) noexcept {
        SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
        ResourceStore store;
        ResourceFailure opening;
        bool opened = false;
        try { opened = store.open(mounts, opening); }
        catch (...) { opening.message = "owned icon resource scope allocation failed"; }
        for (;;) {
            std::string name;
            {
                std::unique_lock lock(mutex_);
                wake_.wait(lock, [&] { return stopping_ || !pending_.empty(); });
                if (stopping_) return;
                // Service both visible sides together; no lane drains ahead
                // of another. Running tasks contain immutable names only.
                for (unsigned step=0;step<visible_.size() && name.empty();++step) {
                    const unsigned lane=(visible_lane_+step)%visible_.size();
                    for (const auto& candidate : visible_[lane])
                        if (entries_.at(candidate).pending) { name=candidate; break; }
                    if (!name.empty()) visible_lane_=(lane+1)%visible_.size();
                }
                for (unsigned step=0;step<selected_.size() && name.empty();++step) {
                    const unsigned lane=(selected_lane_+step)%selected_.size();
                    auto& tab=selected_[lane];
                    while (!tab.empty()) {
                        auto candidate=std::move(tab.front()); tab.pop_front();
                        if (entries_.at(candidate).pending) { name=std::move(candidate); break; }
                    }
                    if (!name.empty()) selected_lane_=(lane+1)%selected_.size();
                }
                while (name.empty() && !pending_.empty()) {
                    auto candidate = std::move(pending_.front()); pending_.pop_front();
                    if (entries_.at(candidate).pending) name = std::move(candidate);
                }
                if (name.empty()) continue;
                entries_.at(name).pending = false;
            }
            std::shared_ptr<const CampIconData> data;
            std::string error;
            try {
                if (!opened) error = opening.message;
                else {
                    auto result = std::make_shared<CampIconData>();
                    SurfaceImage image;
                    error = decode_camp_icon(store,name,high_resolution,image,result->description);
                    if (error.empty() && FAILED(create_completed_surface_texture(device.Get(),image,result->texture,result->srv)))
                        error="owned icon GPU creation failed";
                    if (error.empty()) data = std::move(result);
                }
            } catch (...) { error = "owned icon decode allocation failed"; }
            std::lock_guard lock(mutex_);
            auto& entry = entries_.at(name);
            entry.data = std::move(data); entry.failure = std::move(error);
        }
    }

    // Core mode: the host's answers (Control::camp_icon_answers) complete
    // their entries. A ring that overflowed lost answers: every outstanding
    // request is sent again (the host answers a known serial again).
    void core_poll_locked() {
#if !defined(EID3D11_EMBEDDED) && !defined(EID3D11_SHADER_TOOL)
        auto& control = x64remote::control();
        const long written = control.camp_icon_answers_written;
        if (written - core_answers_read_ > long(x64remote::kCampIconAnswers)) {
            core_answers_read_ = written;
            for (const auto& [serial, name] : core_requested_)
                core_send(scene_core::CampIconRequest{serial, world_epoch_, name});
        }
        for (; core_answers_read_ < written; ++core_answers_read_) {
            MemoryBarrier();
            const auto answer = control.camp_icon_answers[static_cast<std::uint32_t>(core_answers_read_) % x64remote::kCampIconAnswers];
            const auto requested = core_requested_.find(answer.serial);
            if (requested == core_requested_.end()) continue;
            const auto found = entries_.find(requested->second);
            if (found != entries_.end() && found->second.serial == answer.serial) {
                auto& entry = found->second;
                if (!answer.canvas) entry.failure = "the core-mode host could not decode the icon (host log)";
                else {
                    auto result = std::make_shared<CampIconData>();
                    auto& description = result->description;
                    description.width = description.height = answer.canvas; description.pitch = answer.canvas * 2;
                    description.format.dwSize = sizeof(DDPIXELFORMAT);
                    description.format.dwFlags = DDPF_RGB | DDPF_ALPHAPIXELS;
                    description.format.dwRGBBitCount = 16; description.format.dwRBitMask = 0x7c00;
                    description.format.dwGBitMask = 0x03e0; description.format.dwBBitMask = 0x001f;
                    description.format.dwRGBAlphaBitMask = 0x8000;
                    description.alpha_fully_opaque = answer.opaque != 0;
                    result->core_serial = answer.serial;
                    entry.data = std::move(result);
                }
            }
            core_requested_.erase(requested);
        }
#endif
    }
    std::shared_ptr<const CampIconData> core_request(const std::string& name, bool visible) {
        if (!core_started_) {
            scene_core::CampIconMounts mounts;
            if (!bridge_camp_texture_mounts(mounts.mounts)) {
                reject("icon_texture_manager_mount_contract_unavailable");
                return {};
            }
            mounts.high_resolution = addon_icon_lookup;
            core_send(mounts);
            core_started_ = true;
        }
        std::lock_guard lock(mutex_);
        retire_previous_world_locked();
        core_poll_locked();
        auto [where, inserted] = entries_.try_emplace(name);
        auto& entry = where->second;
        if (inserted) {
            // The host decodes it now: the entry is "decoding" until answered.
            entry.serial = ++core_serial_; entry.pending = false;
            core_requested_[entry.serial] = name;
            core_send(scene_core::CampIconRequest{entry.serial, world_epoch_, name});
        }
        if (visible && !entry.data && entry.failure.empty() &&
            std::find(visible_[3].begin(),visible_[3].end(),name)==visible_[3].end())
            visible_[3].push_back(name);
        if (!entry.failure.empty() && !entry.reported) {
            entry.reported = true;
            const std::string detail = name + ": " + entry.failure;
            bridge_append_probe_event("camp_icon_omitted", detail.c_str());
        }
        return entry.data;
    }
    // Core mode: the unanswered icons the camp shows now go to the host's
    // worker first; sent when that set changes.
    void core_send_priority_locked() {
        std::vector<std::uint64_t> serials;
        for (const auto& lane : visible_)
            for (const auto& name : lane) {
                const auto found = entries_.find(name);
                if (found != entries_.end() && core_requested_.count(found->second.serial) &&
                    std::find(serials.begin(),serials.end(),found->second.serial)==serials.end())
                    serials.push_back(found->second.serial);
            }
        if (serials == core_priority_sent_) return;
        core_priority_sent_ = serials;
        core_send(scene_core::CampIconPriority{std::move(serials)});
    }
public:
    std::shared_ptr<const CampIconData> request(const std::string& name, bool visible = false) {
        if (x64remote::core_active()) return core_request(name, visible);
        if (!workers_[0].joinable()) {
            ResourceStoreConfig mounts;
            if (!bridge_camp_texture_mounts(mounts)) {
                reject("icon_texture_manager_mount_contract_unavailable");
                return {};
            }
            if (!icon_device) return {};
            ComPtr<ID3D11Device> device = icon_device;
            for (auto& worker : workers_)
                worker=std::thread([this,mounts,high_resolution=addon_icon_lookup,device]() mutable {
                    run(std::move(mounts),high_resolution,std::move(device));
                });
        }
        std::lock_guard lock(mutex_);
        retire_previous_world_locked();
        auto [where, inserted] = entries_.try_emplace(name);
        if (inserted) { pending_.push_back(name); wake_.notify_all(); }
        auto& entry = where->second;
        if (visible && entry.pending && std::find(visible_[3].begin(),visible_[3].end(),name)==visible_[3].end())
            visible_[3].push_back(name);
        if (!entry.failure.empty() && !entry.reported) {
            entry.reported = true;
            const std::string detail = name + ": " + entry.failure;
            bridge_append_probe_event("camp_icon_omitted", detail.c_str());
        }
        return entry.data;
    }
    // The icon has no owned home and never will this world (its decode
    // failed: no x4/x2/plain member in the icon mounts, or a bad file).
    bool failed(const std::string& name) {
        std::lock_guard lock(mutex_);
        const auto found = entries_.find(name);
        return found != entries_.end() && !found->second.failure.empty();
    }
    void begin_priority_frame(const std::array<bool,3>& reset) {
        std::lock_guard lock(mutex_);
        for (auto& lane : visible_) lane.clear();
        for (unsigned lane=0;lane<selected_.size();++lane) if (reset[lane]) selected_[lane].clear();
    }
    void prioritize(unsigned lane, const std::vector<std::string>& visible,
        const std::vector<std::string>& selected) {
        for (const auto& name : visible) request(name);
        for (const auto& name : selected) request(name);
        std::lock_guard lock(mutex_);
        visible_[lane]=visible;
        if (x64remote::core_active()) { core_send_priority_locked(); return; }
        auto& tab = selected_[lane];
        for (const auto& name : selected) if (entries_.at(name).pending &&
            std::find(tab.begin(),tab.end(),name)==tab.end()) tab.push_back(name);
        wake_.notify_all();
    }
    void shutdown() noexcept {
        { std::lock_guard lock(mutex_); stopping_ = true; wake_.notify_all(); }
        for (auto& worker : workers_) if (worker.joinable()) worker.join();
        entries_.clear(); pending_.clear();
        for (auto& lane : visible_) lane.clear();
        visible_lane_=selected_lane_=0;
        for (auto& tab : selected_) tab.clear();
        core_started_ = false; core_requested_.clear(); core_priority_sent_.clear();
        stopping_ = false;
    }
    ~CampIconLoader() { shutdown(); }
} camp_icon_loader;

std::string item_icon_name(void* item) {
    if (!item) return {};
    struct IconName {
        const char* text = nullptr;
        ~IconName() { if (text) function<VoidFn>(0x700970)(this); }
    } name;
    // Same hidden CString return and vslot as the admitted DrawItem.
    reinterpret_cast<void*(__thiscall*)(void*,void*)>(field<void**>(item,0)[0x4c/4])(item,&name);
    return name.text ? std::string(name.text) : std::string{};
}

// Scan the authoritative complete inventories, independently of sorted/visible
// rows. Bounded name copies keep opening responsive and continue every frame,
// including idle panels and categories the player never visits. Repeated scans
// also pick up purchases/crafting without caching mutable item identities.
void stream_camp_icon_names() {
    const std::array<void*,3> inventories{field<void*>(screen,0x1e0),
        field<bool>(screen,0x195)?field<void*>(screen,0x1ec):nullptr,
        field<bool>(screen,0x196)?field<void*>(screen,0x1f0):nullptr};
    for (unsigned lane = 0; lane < inventories.size() && valid; ++lane) {
        void* inventory = inventories[lane];
        void* all = inventory ? field<void*>(inventory,0x1ac) : nullptr;
        const int count = all ? field<int>(all,4) : 0;
        if (count <= 0) { icon_catalogue_cursor[lane] = 0; continue; }
        auto** items = field<void**>(all,0);
        auto& cursor = icon_catalogue_cursor[lane];
        if (cursor >= unsigned(count)) cursor = 0;
        const auto end = (std::min)(unsigned(count),cursor+16);
        for (; cursor < end && valid; ++cursor) {
            const auto name = item_icon_name(items[cursor]);
            if (!name.empty()) camp_icon_loader.request(name);
        }
        if (cursor >= unsigned(count)) cursor = 0;
    }
}

CampScrollCostSnapshot scroll_cost;
bool scroll_cost_enabled() noexcept {
    static const bool enabled=[] {
        char value[8]{};
        return GetEnvironmentVariableA("EI_RENDERER_ATTRIBUTION",value,sizeof(value))&&value[0]=='1';
    }();
    return enabled;
}
std::uint64_t scroll_cost_now() noexcept {
    LARGE_INTEGER qpc{};
    if(!QueryPerformanceCounter(&qpc)||qpc.QuadPart<=0){scroll_cost.valid=false;return 0;}
    return static_cast<std::uint64_t>(qpc.QuadPart);
}
std::uint64_t scroll_cost_begin() noexcept {
    const auto begin=scroll_cost_now();
    if(!scroll_cost.first_qpc)scroll_cost.first_qpc=begin;
    return begin;
}
void scroll_cost_finish(std::uint64_t begin,std::uint64_t& total,std::uint64_t& maximum) noexcept {
    const auto end=scroll_cost_now();
    if(!begin||end<begin){scroll_cost.valid=false;return;}
    const auto elapsed=end-begin;
    total+=elapsed;maximum=(std::max)(maximum,elapsed);scroll_cost.last_qpc=end;
}

struct ItemTextureScope;
thread_local ItemTextureScope* item_texture_scope = nullptr;
struct ItemTextureScope {
    ItemTextureScope* previous = item_texture_scope;
    void* owner;
    void* preview_item = nullptr; // Borrowed only by this synchronous part9 draw.
    bool preview_glow_ready = false;
    std::uint32_t preview_specular_rgb = 0;
    ModelDrawReference pending_figure; // Released by close(), including the native SEH exit.
    bool measure_scroll = false, scroll_miss_open = false;
    bool active = true;
    std::shared_ptr<const OwnedVisualSurface> surface;
    CampTexture* binding = nullptr; // Borrowed only during this synchronous draw.
    unsigned observed_draws = 0, background_draws = 0;
    explicit ItemTextureScope(void* self,bool measure=false) noexcept
        : owner(self),measure_scroll(measure) { item_texture_scope = this; }
    void close() noexcept {
        if (!std::exchange(active, false)) return;
        item_texture_scope = previous;
        preview_item = nullptr;
        pending_figure.reset();
        surface.reset(); binding = nullptr;
    }
    ~ItemTextureScope() { close(); }
};

struct ScrollMissTiming {
    ItemTextureScope& scope; // Stack-borrowed for this lookup only.
    bool started=false;
    std::uint64_t begin=0;
    explicit ScrollMissTiming(ItemTextureScope& current) noexcept : scope(current) {}
    void start() noexcept {
        if(!scope.measure_scroll)return;
        started=true;scope.scroll_miss_open=true;++scroll_cost.miss_calls;
        begin=scroll_cost_begin();
    }
    ~ScrollMissTiming() {
        if(!started)return;
        // Outside the lookup's try/catch: includes cache/scope/output publication
        // or normal rejection cleanup. Native faults still propagate unchanged.
        scroll_cost_finish(begin,scroll_cost.miss_wall_qpc,scroll_cost.miss_max_wall_qpc);
        scope.scroll_miss_open=false;
    }
};

bool preview_glow_contract() noexcept {
    const auto match = [&](std::uintptr_t va, std::initializer_list<std::uint8_t> bytes) {
        return !std::memcmp(game + va - kBase, bytes.begin(), bytes.size());
    };
    // Read-only prototype lookup and native frame-duration getter; the latter
    // reads milliseconds through the renderer's admitted vtable entry.
    return match(0x68ba20,{0x8b,0x15,0x14,0x85,0x7c,0,0x56,0x8b,0x35,0x10,0x85,0x7c,0,
        0xb8,0xe1,0x83,0x0f,0x3e,0x2b,0xd6,0x8b,0x49,0x18,0xf7,0xea,0xc1,0xfa,0x05,
        0x8b,0xc2,0xc1,0xe8,0x1f,0xc1,0xe9,0x10,0x03,0xd0,0x3b,0xca,0x73,0x0c,
        0x8b,0xd1,0xc1,0xe2,0x05,0x03,0xd1,0x8d,0x04,0x96,0x5e,0xc3,0x33,0xc0,0x5e,0xc3}) &&
        match(0x600270,{0x83,0xec,0x08,0xe8,0xe8,0x06,0xf0,0xff,0x8b,0x10,0x8b,0xc8,
            0xff,0x52,0x18,0x89,0x44,0x24,0,0xc7,0x44,0x24,0x04,0,0,0,0,
            0xdf,0x6c,0x24,0,0xd8,0x0d,0x0c,0x04,0x74,0,0x83,0xc4,0x08,0xc3}) &&
        match(0x500960,{0xb8,0x60,0xf0,0x7a,0,0xc3}) &&
        match(0x73d954,{0x90,0x0a,0x50,0}) &&
        match(0x500a90,{0x8b,0x41,0x34,0xc3}) &&
        match(0x5eccf6,{0x83,0x7e,0x14,0x09,0x0f,0x84,0,0x05,0,0}) &&
        match(0x5ecd34,{0x8b,0x06,0xd9,0x5d,0x0c,0x8b,0x58,0x1c,0x8b,0x48,0x10,0x8b,0x40,0x44}) &&
        match(0x5ecd50,{0x81,0xf9,0x04,0x30,0,0,0x74,0x0c,0x81,0xf9,0x05,0x30,0,0,0x0f,0x85,0xa9,0,0,0}) &&
        match(0x5ecd6c,{0x8b,0xc8,0xe8,0xad,0xec,0x09,0,0x8b,0x40,0x14,0x83,0xf8,0x08,0x0f,0x8d,0x87,0,0,0}) &&
        match(0x5ecd7f,{0xd9,0x45,0x0c,0xdc,0x0d,0xf0,0x03,0x74,0,0xc1,0xe0,0x04,
            0x05,0x58,0x03,0x74,0,0xd8,0x43,0x14,0xdd,0x05,0xb8,0x02,0x74,0,
            0xdc,0xc0,0x89,0x45,0xd4,0xe8,0x07,0xc8,0x0f,0,0xd9,0x53,0x14}) &&
        match(0x5ecd88,{0xc1,0xe0,0x04,0x05,0x58,0x03,0x74,0,0xd8,0x43,0x14}) &&
        match(0x5ecda3,{0xd9,0x53,0x14,0xd9,0xfe,0x8b,0x5d,0xd4,0xdc,0x05,0xc8,0xba,0x73,0,
            0xdc,0x0d,0xe8,0x03,0x74,0}) &&
        match(0x73bac8,{0,0,0,0,0,0,0xf0,0x3f}) &&
        match(0x7403e8,{0,0,0,0x40,0x33,0x33,0xc3,0x3f}) &&
        match(0x7403f0,{0,0,0,0,0,0,0x14,0x40}) &&
        match(0x7402b8,{0x18,0x2d,0x44,0x54,0xfb,0x21,0x09,0x40}) &&
        match(0x74040c,{0x6f,0x12,0x83,0x3a}) &&
        match(0x73c370,{0,0,0x7f,0x43});
}

const char* advance_preview_specular_rgb(void* item, std::uint32_t& rgb) noexcept {
    if (!preview_glow_admitted) return "native_palette_phase_contract_differs";
    __try {
        const int type = field<int>(item,0x10);
        // The shipped glow producer admits weapons and armour only.
        if (type != 0x3004 && type != 0x3005) return nullptr;
        void* spell = field<void*>(item,0x44);
        if (!spell) return nullptr;
        void* prototype = function<void*(__thiscall*)(void*)>(0x68ba20)(spell);
        if (!prototype) return "spell_prototype_unavailable";
        const int subtype = field<int>(prototype,0x14);
        if (subtype < 0 || subtype >= 8) return "spell_palette_unavailable";
        void* data = field<void*>(item,0x1c);
        if (!data) return "item_phase_unavailable";
        const float phase = field<float>(data,0x14);
        if (!std::isfinite(phase)) return "item_phase_invalid";
        const auto* palette = reinterpret_cast<const float*>(game + 0x340358 + subtype * 16);
        for (unsigned channel = 0; channel < 3; ++channel)
            if (!std::isfinite(palette[channel]) || palette[channel] < 0 || palette[channel] > 1)
                return "spell_palette_invalid";
        const float elapsed = function<float(__thiscall*)(void*)>(0x600270)(screen);
        if (!std::isfinite(elapsed)) return "native_frame_time_invalid";
        // Native part9 skips the pulse step. Advance only its Info-owned copy,
        // once per draw, using the native frame duration and phase lifecycle.
        float next_phase = phase;
        if (!advance_model_preview_glow(next_phase, elapsed,
                {palette[0], palette[1], palette[2]}, rgb)) return "native_item_glow_update_failed";
        field<float>(data,0x14) = next_phase;
        return nullptr;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return "native_item_glow_update_failed";
    }
}

// The native miss path reaches this site once, with its non-null new figure,
// before trim can release the LRU's only reference. Nested draws have their
// own scope; this reference is never retained as a geometry cache.
void __stdcall retain_pending_item_figure(void* self, void* figure) noexcept {
    if (!item_texture_scope || item_texture_scope->owner != self || self != screen || !layout.wide)
        return;
    item_texture_scope->pending_figure.hold(figure,
        [](void* value) noexcept { ++field<int>(value, 4); },
        [](void* value) noexcept {
            reinterpret_cast<VoidFn>(field<void**>(value, 0)[0x10 / 4])(value);
        });
    if(item_texture_scope->measure_scroll)++scroll_cost.figure_misses;
}

void* figure_trim_forwarder = nullptr;
__declspec(naked) void item_figure_trim() {
    __asm {
        pushfd
        pushad
        push dword ptr [ebp - 24h]
        push eax
        call retain_pending_item_figure
        popad
        popfd
        jmp dword ptr [figure_trim_forwarder]
    }
}

CampTexture* ready_item_texture(const std::string& name, std::uint64_t generation) {
    const auto key = std::make_pair(generation,name);
    auto found = camp_textures.find(key);
    if (found != camp_textures.end()) return &found->second;
    const auto data = camp_icon_loader.request(name);
    if (!data || !valid) return nullptr;
    CampTexture texture;
    texture.diagnostic_name = name;
    void* manager = bridge_camp_texture_owner();
    if (!manager) { reject("icon_texture_owner_unavailable"); return nullptr; }
    texture.compatibility = function<void*(__thiscall*)(void*,DWORD,DWORD,int)>(0x691d80)(manager,data->description.width,data->description.height,0x20);
    if (!texture.compatibility) { reject("icon_binding_creation_failed"); return nullptr; }
    ++field<int>(texture.compatibility,4);
    if (field<void*>(texture.compatibility,0) != game + 0x343d24 || field<int>(texture.compatibility,4) != 1) {
        reject("icon_native_reference_contract_differs"); return nullptr;
    }
    // This binding belongs to the persistent owned GPU home, rather than the
    // native map unload cycle. The manager still destroys it at normal teardown.
    field<DWORD>(texture.compatibility,8) |= 0x40000;
    void* wrapper = field<void*>(texture.compatibility,0x5c);
    auto* surface = wrapper ? field<IDirectDrawSurface7*>(wrapper,0x10) : nullptr;
    if (!proxy_publish_camp_texture(surface,*data,texture.surface) || bridge_camp_texture_generation()!=generation) {
        reject("icon_completed_surface_unavailable"); return nullptr;
    }
    return &camp_textures.emplace(key,std::move(texture)).first->second;
}

// The inline caller still owns its CString and EH state5. No C++ exception
// crosses that native frame; pending/failed values never invoke native decode.
bool __stdcall resolve_item_texture(const char* name, void* self, void** output) noexcept {
    if (!item_texture_scope || item_texture_scope->owner != self || self != screen || !layout.wide)
        return false;
    *output = nullptr;
    if (!valid) return true;
    const auto generation = bridge_camp_texture_generation();
    if (!name || !generation) { reject("icon_resource_generation_unavailable"); return true; }
    ScrollMissTiming miss(*item_texture_scope);
    try {
        // GPU homes stream independently of scrolling. Only a drawn, ready
        // icon acquires the native compatibility binding.
        const auto key = std::make_pair(generation,std::string(name));
        if (camp_textures.find(key)==camp_textures.end()) {
            if (!camp_icon_loader.request(key.second,true)) {
                // An icon whose decode failed for good (sw_12.0, hl_05.1,
                // hl_05.2: no member in any mounted archive) takes the game's
                // own lookup, as the stock game does: the texture LRU's
                // CreateObject (the addon's x4/x2/plain loader in Dejavu)
                // makes its canvas for the name, white where nothing loads.
                if (camp_icon_loader.failed(key.second)) { *output = nullptr; return false; }
                return true;
            }
            miss.start();
        }
        auto* texture = ready_item_texture(key.second,generation);
        if (!texture) return true;
        item_texture_scope->surface = texture->surface;
        item_texture_scope->binding = texture;
        *output = texture->compatibility;
    } catch (...) { reject("icon_owned_admission_allocation_failed"); }
    return true;
}

void* item_texture_forwarder = nullptr;
void* item_texture_cleanup = nullptr;
__declspec(naked) void item_texture_lookup() {
    __asm {
        push edx
        push ecx
        push eax
        push 0
        mov edx, esp
        push edx
        push ecx
        push eax
        call resolve_item_texture
        test al, al
        je stock_lookup
        pop ebx
        add esp, 12
        jmp dword ptr [item_texture_cleanup]
    stock_lookup:
        add esp, 4
        pop eax
        pop ecx
        pop edx
        jmp dword ptr [item_texture_forwarder]
    }
}

int mode() noexcept { return screen ? field<int>(screen, 0x190) : 0; }
camp::Rect stage() noexcept {
    return camp::frontend::stage(layout,mode());
}
camp::Rect actions() noexcept {
    return camp::frontend::actions(layout,mode());
}
camp::Rect heading() noexcept { return layout.heading; }
bool root_action_visible(int index) noexcept {
    return mode()!=0||field<bool>(screen,index==0?0x210:0x211);
}

camp::Rect context_plate(unsigned card) noexcept {
    const auto& r=card==0?layout.ordinary_info:card==1?layout.result_info:layout.comparison_info;
    // The shared rectangle is already the complete uniformly mapped native card.
    return r;
}

RECT decoration_pixels(camp::Rect r) noexcept {
    return camp::frontend::pixels(layout,r);
}

bool fresh_hover() noexcept {
    return ordinary_hover.fresh(session.allowed(current_camp_input()));
}
bool context_visible(unsigned card) noexcept {
    void* owner=card==2?comparison_owner:field<void*>(screen,card==0?0x164:0x1dc);
    if(!owner||!field<BOOL>(owner,0xe8))return false;
    if(card==1)return constructor_card!=ConstructorCard::none;
    if(card==2)return comparison_current&&(GetKeyState(VK_SHIFT)&0x8000)&&hover_focus&&!hover_controls&&current_camp_input();
    return fresh_hover();
}

void snapshot_decoration() noexcept {
    camp::frontend::BackgroundState state;state.mode=mode();
    const auto shown=[](std::size_t offset){
        void* child=field<void*>(screen,offset);return child&&field<BOOL>(child,0xe8);
    };
    const bool spells=state.mode==2||state.mode==3;
    const bool shop=field<bool>(screen,spells?0x195:0x196);
    state.quick=shown(0x1e4);state.stats=shown(0x1d8);state.perks=shown(0x1f4);state.wagon=shown(0x1e0);
    state.merchant=shop&&shown(spells?0x1ec:0x1f0);
    state.trade=shop&&shown(spells?0x1f8:0x200);
    state.construction=shop&&shown(spells?0x1fc:0x204);
    state.repair=field<bool>(screen,0x196)&&shown(0x208);
    state.cards=cards_drawn;state.empty_construction=constructor_card==ConstructorCard::empty;
    decoration=camp::frontend::background_decoration(layout,state,[](ScrollList list){return scroll_rail(list);});
    decoration_pending=true;
}
Role role_of(void* object) noexcept {
    if (!screen || !object) return Role::none;
    if (object == screen) return Role::root;
    if (!valid) return Role::none;
    if(object==comparison_owner)return Role::comparison;
    if(object==blueprint_owner)return Role::blueprint;
    // Named child stores in OnActivate, including optional shop children.
    constexpr std::pair<std::size_t, Role> children[] = {
        {0x164, Role::info}, {0x16c, Role::background}, {0x1d8, Role::stats},
        {0x1dc, Role::help}, {0x1e0, Role::wagon}, {0x1e4, Role::quick},
        {0x1e8, Role::spells}, {0x1ec, Role::vendor}, {0x1f0, Role::vendor},
        {0x1f4, Role::perks}, {0x1f8, Role::trade}, {0x1fc, Role::construction},
        {0x200, Role::trade}, {0x204, Role::construction}, {0x208, Role::repair}};
    for (const auto& child : children) {
        // Optional child fields are only authoritative when their native
        // shop capability is enabled; old freed values are never consulted.
        if ((child.first == 0x1ec || child.first == 0x1f8 || child.first == 0x1fc) &&
            !field<bool>(screen, 0x195)) continue;
        if ((child.first == 0x1f0 || child.first >= 0x200) &&
            !field<bool>(screen, 0x196)) continue;
        if (field<void*>(screen, child.first) == object) return child.second;
    }
    return Role::none;
}

POINT virtual_point(void* object, POINT p) {
    function<PointFn>(0x5f7e00)(field<void*>(object, 0x98), &p);
    return p;
}
bool coordinate_contract(void* self) {
    POINT origin{0, 0}, corner{800, 600};
    auto convert = function<PointFn>(0x5f7d20);
    convert(field<void*>(self, 0x98), &origin);
    convert(field<void*>(self, 0x98), &corner);
    const int expected_left = int(std::lround(-layout.left * layout.unit));
    return std::abs(origin.x - expected_left) <= 1 && origin.y == 0 &&
        std::abs(corner.x - int(std::lround((800 - layout.left) * layout.unit))) <= 1 &&
        std::abs(corner.y - int(output_height)) <= 1;
}
camp::Rect area_rect(void* object, int index) noexcept {
    auto** areas = field<void**>(object, 0x128);
    const auto& r = field<RECT>(areas[index], 0x14);
    return {float(r.left), float(r.top), float(r.right), float(r.bottom)};
}

camp::Placement perk_band(float x,float y) noexcept {
    return camp::frontend::perk_band(layout,x,y);
}

camp::frontend::InputState input_state(void* object,Role role) noexcept {
    camp::frontend::InputState s;
    s.role=role;s.mode=mode();s.first=belt_first;s.capacity=spell_capacity;
    s.areas=field<int>(object,0x12c);
    s.actions={{root_action_visible(0),root_action_visible(1)}};
    return s;
}
camp::Placement area_placement(void* object, Role role, int index) noexcept {
    const auto s=input_state(object,role);
    const int native=camp::frontend::area_template(s,index);
    if(native<0||native>=s.areas)return {};
    return camp::frontend::area(layout,s,index,area_rect(object,native));
}
bool belt_visible(const camp::Rect& r,bool equipment=false) noexcept {
    return camp::frontend::belt_visible(layout,r,equipment);
}
POINT inverse_point(camp::Placement p, POINT point) noexcept {
    return camp::frontend::inverse(p,point);
}
BOOL native_area_contains(void* area, POINT point);
int hit(void* object, POINT physical) {
    const Role role=role_of(object);if(role==Role::none)return -2;if(!valid)return -1;
    if(role==Role::spells)spell_first();
    const auto s=input_state(object,role);
    return camp::frontend::hit(layout,s,virtual_point(object,physical),
        [&](int i){return area_rect(object,i);},
        [&](int i,POINT q){return native_area_contains(field<void**>(object,0x128)[i],q)!=FALSE;});
}
struct Grid {
    void* object = nullptr;
    void* background = nullptr;
    void* text = nullptr;
};
std::array<Grid, 3> grids{};
void* spell_frame = nullptr;
void* spell_background = nullptr;
std::array<void*,2> quick_backgrounds{};
void* camp_background = nullptr;
void* popup_background = nullptr;
std::array<void*,2> scroll_art{};
std::array<void*,7> camp_panels{};

Grid* grid_for(void* object) noexcept {
    for (auto& grid : grids) if (grid.object == object) return &grid;
    return nullptr;
}
void form_sorted(void* object) {
    const auto table = field<void**>(object, 0);
    reinterpret_cast<VoidFn>(table[0x68 / 4])(object);
}
int first_item(void* object) noexcept {
    const int category=field<int>(object,0x150);
    float& position=field<float>(object,0x15c+std::size_t(category)*4);
    const int first=camp::frontend::first_item(layout,role_of(object)==Role::vendor,field<int>(object,0x1a0),position);
    position=float(first);return first;
}

void begin_icon_priority_frame() {
    std::array<bool,3> reset{};
    for (unsigned lane=0;lane<grids.size();++lane) {
        void* object = grids[lane].object;
        const int category = object && field<BOOL>(object,0xe8) ? field<int>(object,0x150) : -1;
        auto& cursor = icon_tab_cursors[lane];
        reset[lane] = cursor.mode != mode() || cursor.category != category;
        if (reset[lane]) cursor = {mode(),category,0};
    }
    camp_icon_loader.begin_priority_frame(reset);
}

void prioritize_grid_icons(Grid& grid, int first, int count, int capacity) {
    const unsigned lane = unsigned(&grid-grids.data());
    auto** items = field<void**>(grid.object,0x19c);
    std::vector<std::string> visible, selected;
    for (int i=first;i<(std::min)(count,first+capacity);++i) {
        auto name = item_icon_name(items[i]);
        if (!name.empty()) visible.push_back(std::move(name));
    }
    auto& cursor = icon_tab_cursors[lane].next;
    if (cursor >= unsigned(count)) cursor = 0;
    const unsigned end = (std::min)(unsigned(count),cursor+16);
    for (;cursor<end;++cursor) {
        auto name = item_icon_name(items[cursor]);
        if (!name.empty()) selected.push_back(std::move(name));
    }
    camp_icon_loader.prioritize(lane,visible,selected);
}

bool perk_list(ScrollList list) noexcept {return list==ScrollList::learned||list==ScrollList::available;}
bool drag_admitted=false;
auto& scroll_drag=session.drag;

// The current person's equipped spells as of the previous camp frame. A spell
// object that was not equipped then is newly equipped; the belt follows it
// once when it lands beyond the eighth cell. Reordering, removal, manual
// wheel/drag scrolling and a person change never move the belt.
struct BeltSnapshot {
    void* person=nullptr;
    int count=0;
    std::array<void*,16> spells{};
} belt_snapshot;
void follow_equipped_spell() noexcept {
    void* person=current_person();
    BeltSnapshot now{person,person?spell_count():0,{}};
    auto** spells=person?field<void**>(person,0x760):nullptr;
    if(!spells)now.count=0;
    else std::copy_n(spells,now.count,now.spells.begin());
    const bool same=person&&person==belt_snapshot.person;
    int added=-1;
    for(int i=0;same&&i<now.count;++i)
        if(std::find(belt_snapshot.spells.begin(),belt_snapshot.spells.begin()+belt_snapshot.count,now.spells[i])==
           belt_snapshot.spells.begin()+belt_snapshot.count)added=i;
    belt_snapshot=now;
    if(added<8||mode()==0||(scroll_drag.window&&scroll_drag.list==ScrollList::spells))return;
    if(added>=belt_first+8)belt_first=added-7;
    else if(added<belt_first)belt_first=added;
    spell_first();
}

bool current_camp_input() noexcept {
    if(!drag_admitted||!game||!screen||!layout.wide||!valid||!requested_wide)return false;
    // Shipped callers pass this inline global object, not a pointer variable.
    void* camp_interface=game+0x3c7d50;
    return function<void*(__thiscall*)(void*)>(0x5f7590)(camp_interface)==screen&&
        field<void*>(camp_interface,0x0c)==nullptr;
}
void* scroll_owner(ScrollList list) noexcept {
    if(!screen)return nullptr;
    switch(list){
    case ScrollList::stash:return field<void*>(screen,0x1e0);
    case ScrollList::spell_vendor:return field<bool>(screen,0x195)?field<void*>(screen,0x1ec):nullptr;
    case ScrollList::item_vendor:return field<bool>(screen,0x196)?field<void*>(screen,0x1f0):nullptr;
    case ScrollList::spells:return field<void*>(screen,0x1e8);
    case ScrollList::learned:case ScrollList::available:return field<void*>(screen,0x1f4);
    }
    return nullptr;
}
camp::frontend::Scroll scroll_snapshot(ScrollList list) noexcept {
    void* owner=scroll_owner(list);
    if(!owner||!field<BOOL>(owner,0xe8))return {};
    camp::frontend::Scroll scroll;scroll.visible=true;
    if(list==ScrollList::spells){scroll.page=8;scroll.total=spell_count();scroll.position=spell_first();}
    else if(perk_list(list)){
        const bool learned=list==ScrollList::learned;
        const auto begin=field<std::uintptr_t>(owner,learned?0x168:0x15c);
        const auto end=field<std::uintptr_t>(owner,learned?0x16c:0x160);
        scroll.total=int((end-begin)/24);scroll.page=learned?14:23;
        scroll.position=field<int>(owner,learned?0x158:0x154);
    }else{
        const bool vendor=list!=ScrollList::stash;const int columns=layout.grid_columns(vendor);
        scroll.page=layout.grid_rows(vendor);scroll.total=(field<int>(owner,0x1a0)+columns-1)/columns;
        scroll.position=first_item(owner)/columns;scroll.category=field<int>(owner,0x150);
    }
    return scroll;
}
ScrollRail scroll_rail(ScrollList list) noexcept {return camp::frontend::rail(layout,list,scroll_snapshot(list));}
void scroll_to(ScrollList list,int position) noexcept {
    const auto bar=scroll_rail(list);
    position=std::clamp(position,0,bar.maximum);
    if(list==ScrollList::spells){belt_first=position;return;}
    void* owner=scroll_owner(list);
    if(perk_list(list)){
        field<int>(owner,list==ScrollList::learned?0x158:0x154)=position;
        function<VoidFn>(0x626d70)(owner);
        return;
    }
    const int category=field<int>(owner,0x150);
    field<float>(owner,0x15c+category*4)=float(position*layout.grid_columns(list!=ScrollList::stash));
    field<float>(owner,0x174)=0;field<float>(owner,0x178)=0;
}
void move_scroll_drag(POINT point) noexcept {
    camp::frontend::move_drag(session,layout,mode(),current_camp_input(),point,scroll_snapshot,scroll_to);
}
using SurfaceCtor = void*(__thiscall*)(void*, void*, int, int, int, int, int, bool);
using BarFn = void(__thiscall*)(void*, float, float, float, float, float, float, int, int, int, int);
using FrameFn = void(__thiscall*)(void*, float, float, float, float, float, float, float, int, int, int, int);
using TilingFn = void(__thiscall*)(void*, float,float,float,float,float,float,int,int,int,int,int,int);
void* add_figure(void* object) {
    void* figure=function<void*(__thiscall*)(void*, const char*)>(0x5ff3c0)(object, nullptr);
    if(!figure)reject("camp_frame_allocation_failed");
    return figure;
}
void prepare_grid(Grid& grid, void* object) {
    grid.object = object;
    const bool vendor = role_of(object) == Role::vendor;
    const auto r = layout.grid(vendor);
    grid.background = add_figure(object);
    if(!grid.background)return;
    const float top = float(field<int>(object, 0x180));
    function<BarFn>(0x5e70e0)(grid.background, field<float>(object,0x184)*2, 100, 50, top, 150, top + 100, 14, 14, 114, 114);
    // The native object owns this text surface and tears it down with its
    // other surfaces. Its dimensions are the grid, not the old 700x20 strip.
    void* memory = function<void*(__cdecl*)(std::size_t)>(0x7005af)(0x38);
    if (!memory) { reject("grid_text_allocation_failed"); return; }
    grid.text = function<SurfaceCtor>(0x5fcc00)(memory, field<void*>(object, 0x98),
        int(r.left), int(r.top), int(r.right), int(r.bottom), 5, true);
    function<void*(__thiscall*)(void*, void*)>(0x5ffa90)(object, grid.text);
}

// Each entry uses an instruction-complete prologue, with a verified addon
// dispatcher forwarded where present. No presumed stock detour is replaced.
struct Hook {
    std::uintptr_t va = 0;
    std::size_t bytes = 0;
    std::array<std::uint8_t, 24> stock{};
    const void* replacement = nullptr;
    void* forwarder = nullptr;
    std::array<std::uint8_t, 16> original{};
    bool installed = false;
    bool addon_entry = false;
    std::uint8_t* address = nullptr; // addon_entry: the discovered addon body
    EntryResolution owner{};
    std::size_t patch_bytes = 0;
    Hook() = default;
    template<class Fn> Hook(std::uintptr_t entry, std::size_t length,
                           std::array<std::uint8_t, 24> expected, Fn target, bool addon = false) noexcept
        : va(entry), bytes(length), stock(expected), replacement(reinterpret_cast<const void*>(target)), addon_entry(addon) {}
};
enum HookId { activate_id, deactivate_id, camp_draw_id, object_draw_id, draw2d_id,
              tips_id, tips2d_id, point_id, hit_id, inside_id, inventory_draw_id, inventory_mouse_id,
              idle_id, wheel_id, set_to_item_id, quick_draw_id, spell_draw_id, spell_mouse_id, spell_current_id, slot_click_id, unit_draw_id,
              trade_draw_id, repair_draw_id, spell_construction_id, item_construction_id,
              item_draw_id,
              info_item_id,info_help_id,info_skill_id,info_text_id,info_draw_id,
              item_texture_id, figure_trim_id, texture_manager_destroy_id,
              addon_info_help_id,addon_info_skill_id,
              close_world_segment_id,close_visibility_id,close_update_player_id,hook_count };
std::array<Hook, hook_count> hooks;
std::uint8_t* hook_entry(const Hook& hook) noexcept {
    return hook.addon_entry?hook.address:game+hook.va-kBase;
}
template<class Fn> Fn original(HookId id) noexcept { return reinterpret_cast<Fn>(hooks[id].forwarder); }

// Temporary, default-off attribution within the measured native server update.
// Forward the admitted function ABI and existing addon entry owner unchanged.
std::int64_t close_call_begin() noexcept {
    LARGE_INTEGER begin{};
    if (close_observation_pending) QueryPerformanceCounter(&begin);
    return begin.QuadPart;
}
void close_call_end(const char* phase, std::int64_t begin) noexcept {
    if (!begin) return;
    LARGE_INTEGER end{}; QueryPerformanceCounter(&end);
    camp_inventory_observe_close_call(phase, begin, end.QuadPart);
}
void __fastcall close_world_segment(void* self, void*, bool force_script) {
    const auto begin = close_call_begin();
    __try { original<void(__thiscall*)(void*,bool)>(close_world_segment_id)(self, force_script); }
    __finally { close_call_end("server_world_segment", begin); }
}
void __fastcall close_visibility(void* self, void*) {
    const auto begin = close_call_begin();
    __try { original<VoidFn>(close_visibility_id)(self); }
    __finally { close_call_end("server_visibility", begin); }
}
BOOL __fastcall close_update_player(void* self, void*, void* player) {
    const auto begin = close_call_begin();
    BOOL result = FALSE;
    __try { result = original<BOOL(__thiscall*)(void*,void*)>(close_update_player_id)(self, player); }
    __finally { close_call_end("server_update_player", begin); }
    return result;
}
template<class T> void call_hover_setter(HookId id,void* self,T value,HoverKind context) {
    const auto previous=hover_setter;
    hover_setter=context;
    __try {original<void(__thiscall*)(void*,T)>(id)(self,value);}
    __finally {hover_setter=previous;}
}
void __fastcall info_text(void* self,void*) {
    bridge_item_card_text_describe(self,original<VoidFn>(info_text_id));
    if(self==comparison_owner)comparison_modifiers=int(item_modifiers());
    if(self==blueprint_owner)blueprint_modifiers=int(item_modifiers());
    if(screen&&layout.wide&&valid&&self==field<void*>(screen,0x1dc))result_modifiers=int(item_modifiers());
}
void __fastcall info_item(void* self,void*,void* item) {
    using Fn=void(__thiscall*)(void*,void*);
    if(current_result_owner(self))result_help=0;
    if(!ordinary_owner(self)||hover_setter!=HoverKind::none){original<Fn>(info_item_id)(self,item);return;}
    call_hover_setter(info_item_id,self,item,HoverKind::item);
    if(!item){reset_hover();return;}
    ordinary_hover.publish(HoverKind::item,true);
    refresh_comparison(item);
}
void observe_help(HookId id,void* self,int help) {
    using Fn=void(__thiscall*)(void*,int);
    if(!ordinary_owner(self)||hover_setter!=HoverKind::none){
        original<Fn>(id)(self,help);
        if(current_result_owner(self))result_help=help;
        return;
    }
    call_hover_setter(id,self,help,HoverKind::help);
    if(!help){reset_hover();return;}
    ordinary_hover.publish(HoverKind::help,true,help);
}
void __fastcall info_help(void* self,void*,int help) {observe_help(info_help_id,self,help);}
void __fastcall addon_info_help(void* self,void*,int help) {observe_help(addon_info_help_id,self,help);}
void observe_skill(HookId id,void* self,void* descriptor) {
    using Fn=void(__thiscall*)(void*,void*);
    if(!ordinary_owner(self)||hover_setter!=HoverKind::none){original<Fn>(id)(self,descriptor);return;}
    const int type=field<int>(descriptor,0);
    if(type<0)reset_hover();
    else if(type>7)reject("ordinary_hover_target_type_differs");
    else{
        ordinary_hover.publish(HoverKind::skill,true);
    }
    // Native live stats/text refresh still occurs for every skill call.
    call_hover_setter(id,self,descriptor,HoverKind::skill);
}
void __fastcall info_skill(void* self,void*,void* descriptor) {observe_skill(info_skill_id,self,descriptor);}
void __fastcall addon_info_skill(void* self,void*,void* descriptor) {observe_skill(addon_info_skill_id,self,descriptor);}
void publish_addon_hover() {
    // This BG boundary follows the admitted native selection and formatting.
    // The addon inlines SetItem, so its completed fresh selection is the owner.
    if(void* item=field<void*>(screen,0x168)){
        ordinary_hover.publish(HoverKind::item,true);
        refresh_comparison(item);
    }
}
BOOL __fastcall info_draw(void* self,void*,void* v) {
    const auto role=role_of(self);
    if(!screen||!layout.wide||role_card(role)<0)return original<DrawFn>(info_draw_id)(self,v);
    if(!valid||!camp::frontend::card_visible(role,constructor_card,cards_drawn,session.allowed(current_camp_input())))return TRUE;
    Scope scope(role);
    return original<DrawFn>(info_draw_id)(self,v);
}
void native_item_draw(void* self, void* renderer, void* item, ItemTextureScope* scope) {
    // No C++ objects in this frame: /EHsc alone does not unwind their
    // destructors for a native SEH exception. Do not catch or suppress it.
    const auto begin=scope->measure_scroll?scroll_cost_begin():0;
    // The admitted DrawItem part field selects its slot background before
    // the item itself. Part9 draws the preview directly.
    scope->background_draws=field<int>(item,0x14)==9?0u:1u;
    if(scope->measure_scroll)++scroll_cost.item_calls;
    bool completed=false;
    __try {
        original<ItemDrawFn>(item_draw_id)(self, renderer, item);
        completed=true;
    } __finally {
        const bool incomplete=!completed||scope->scroll_miss_open;
        scope->close();
        if(scope->measure_scroll){
            if(incomplete)scroll_cost.valid=false;
            scroll_cost_finish(begin,scroll_cost.item_wall_qpc,scroll_cost.item_max_wall_qpc);
        }
    }
}
void __fastcall texture_manager_destroy(void* self, void*) {
    camp_inventory_texture_manager_destroying(self);
    original<VoidFn>(texture_manager_destroy_id)(self);
}
BOOL native_area_contains(void* area, POINT point) {
    return original<BOOL(__thiscall*)(void*, POINT)>(point_id)(area, point);
}

struct PerkInstruction {
    std::uint8_t* address=nullptr;
    std::array<std::uint8_t,8> native{},wide{};
    unsigned size=0;
};
std::array<PerkInstruction,8> perk_instructions{};
bool perk_viewport_changed=false;

bool restore_perk_viewport() noexcept {
    if(!perk_viewport_changed)return true;
    bool restored=true;
    for(const auto& instruction:perk_instructions){
        if(!std::memcmp(instruction.address,instruction.native.data(),instruction.size))continue;
        if(std::memcmp(instruction.address,instruction.wide.data(),instruction.size)||
            !write_bytes(instruction.address,instruction.native.data(),instruction.size))restored=false;
    }
    if(restored){
        perk_viewport_changed=false;
        // Refresh the still-live authoritative producer before clearing its child.
        if(screen)function<VoidFn>(0x626d70)(field<void*>(screen,0x1f4));
    }
    return restored;
}

bool prepare_perk_viewport() noexcept {
    const std::uint8_t redraw[]{0x6a,0xff,0x68,0x58,0xcc,0x72,0,0x64,0xa1,0,0,0,0,0x50,0x64,0x89,0x25,0,0,0,0,0x83,0xec,0x40};
    const std::uint8_t ranges[]{0x8b,0x91,0x6c,0x01,0,0,0x8b,0x81,0x68,0x01,0,0,0x2b,0xd0,0xb8,0xab,0xaa,0xaa,0x2a,0xf7,0xea,0xc1,0xfa,0x02};
    const auto owner=resolve_entry_owner(game+0x2261a0,redraw,sizeof(redraw),7,&addon_module_contains);
    if(owner.owner==EntryOwner::rejected||std::memcmp(game+0x226d70,ranges,sizeof(ranges)))return false;
    const bool addon=owner.owner==EntryOwner::framework_chain;
    // The addon's perk redraw replaces the native one through a one-argument
    // adapter; its body keeps the native header/row rectangles and row count.
    std::uint8_t* const body=addon?addon_adapter_body(owner,1):nullptr;
    const std::uint8_t prologue[]{0x55,0x8b,0xec,0x6a,0xff};
    if(addon&&(!body||std::memcmp(body,prologue,sizeof(prologue))))return false;
    void* perks=field<void*>(screen,0x1f4);
    if(!perks||field<void*>(perks,0x14c)!=screen||field<int>(perks,0x12c)!=44)return false;
    // These consumers accept logical34..47 and bound-check the actual vector;
    // neither uses a returned logical ID as an index into the44-area array.
    const auto match=[&](std::uintptr_t va,std::initializer_list<std::uint8_t> bytes){
        return !std::memcmp(game+va-kBase,bytes.begin(),bytes.size());};
    if(!match(0x6256b9,{0xe8,0x02,0xa6,0xfd,0xff})||!match(0x625726,{0x83,0xf8,0x22})||
       !match(0x625690,{0x53,0x56,0x8b,0x74,0x24,0x14,0x57,0x8b,0xf9,0xc7,0x06,0xff,0xff,0xff,0xff})||
       !match(0x626920,{0x51,0x8b,0x44,0x24,0x10,0x53,0x55,0x56,0x8b,0xf1,0x57})||
       !match(0x626c30,{0x8b,0x51,0x04,0x33,0xc0,0x89,0x42,0x0c})||
       !match(0x626c50,{0x55,0x56,0x8b,0xf1,0x57,0x8b,0x86,0x9c,0x01,0,0})||
       !match(0x6257d2,{0x8d,0x4c,0x01,0xde})||!match(0x6257e9,{0x3b,0xca})||
       !match(0x6257eb,{0x73,0x66})||!match(0x62693d,{0xe8,0x7e,0x93,0xfd,0xff})||
       !match(0x626af3,{0x83,0xfb,0x22})||!match(0x626b94,{0x8d,0x5c,0x19,0xde})||
       !match(0x626bac,{0x3b,0xda})||!match(0x626bae,{0x73,0x5a})||
       !match(0x626d00,{0x83,0xff,0x0b})||!match(0x626d03,{0x73,0x64})||
       !match(0x626dd6,{0x8b,0x91,0x60,0x01,0,0})||!match(0x626ddc,{0x8b,0xb1,0x5c,0x01,0,0})||
       !match(0x626df3,{0x8d,0x54,0x02,0xe9})||!match(0x626df7,{0x8b,0x81,0x54,0x01,0,0}))return false;
    unsigned n=0;
    bool matched=true;
    const auto add=[&](std::uint8_t* address,std::initializer_list<std::uint8_t> bytes,unsigned immediate,std::uint8_t value){
        auto& instruction=perk_instructions[n++];instruction={};instruction.address=address;
        instruction.size=unsigned(bytes.size());std::copy(bytes.begin(),bytes.end(),instruction.native.begin());
        instruction.wide=instruction.native;instruction.wide[immediate]=value;
        matched=(!std::memcmp(address,instruction.native.data(),instruction.size))&&matched;
    };
    if(addon){
        // Each instruction occurs once in the redraw body (JMV 1.35 and
        // Dejavu builds place them at different offsets).
        const auto site=[&](std::initializer_list<std::uint8_t> bytes,unsigned immediate,std::uint8_t value){
            std::uint8_t* address=addon_unique(body,0x800,bytes);
            if(!address){matched=false;return;}
            add(address,bytes,immediate,value);
        };
        site({0xc7,0x45,0x84,0xcd,0,0,0},3,145);
        site({0xc7,0x45,0x8c,0xdc,0,0,0},3,160);
        site({0xc7,0x45,0xe0,0xe6,0,0,0},3,170);
        site({0xc7,0x45,0xe8,0xf5,0,0,0},3,185);
        site({0x83,0xf8,0x0a},2,14);
        if(!matched)return false;
    }else{
        add(game+0x226259,{0xc7,0x44,0x24,0x3c,0xcd,0,0,0},4,145);
        add(game+0x226265,{0xc7,0x44,0x24,0x44,0xdc,0,0,0},4,160);
        add(game+0x22662c,{0xb9,0xe6,0,0,0},1,170);
        add(game+0x226618,{0xbb,0xf5,0,0,0},1,185);
        add(game+0x22676b,{0x83,0xf8,0x0a},2,14);
    }
    for(const auto rva:{0x226d8d,0x226db8,0x226e72})
        add(game+rva,{0x8d,0x54,0x02,0xf6},3,0xf2);
    if(!matched)return false;
    perk_viewport_changed=true;
    for(const auto& instruction:perk_instructions)
        if(!write_bytes(instruction.address,instruction.wide.data(),instruction.size)){
            restore_perk_viewport();return false;
        }
    function<VoidFn>(0x626d70)(perks);
    return true;
}

bool prepare_spell_contract() noexcept {
    const auto entry=[&](std::uintptr_t va,std::initializer_list<std::uint8_t> bytes){
        return resolve_entry_owner(game+va-kBase,bytes.begin(),bytes.size(),5,&addon_module_contains);
    };
    const auto init=entry(0x622a80,{0x51,0x8a,0x44,0x24,0x08,0x53,0x33,0xdb,0x56,0x3a,0xc3,0x57,0x8b,0xf1,0x74,0x08,0x89,0x9e,0x50,0x01,0,0,0xeb,0x0a});
    const auto wheel_owner=entry(0x5e28b0,{0x33,0xc0,0xc2,0x10,0});
    const auto equip=entry(0x60fca0,{0x51,0x53,0x55,0x56,0x57,0x8b,0xf9,0x33,0xdb,0x53,0x68,0xb0,0x37,0x79,0,0x8b,0x8f,0x98,0,0,0});
    // The addon's extended spell slots install the current/draw/mouse/click/
    // wheel owners together; all five are stock or all five are its callbacks.
    const bool extended=hooks[spell_draw_id].owner.owner!=EntryOwner::stock;
    const auto owned=[&](const EntryResolution& owner,std::initializer_list<std::uint8_t> prologue)->std::uint8_t*{
        const auto* callback=addon_callback(owner);
        return callback&&!std::memcmp(callback,prologue.begin(),prologue.size())?const_cast<std::uint8_t*>(callback):nullptr;
    };
    const auto* current=owned(hooks[spell_current_id].owner,{0x55,0x8b,0xec,0x56,0xff,0x75,0x0c,0x8b,0x75,0x08,0x56});
    const auto* draw=owned(hooks[spell_draw_id].owner,{0x55,0x8b,0xec,0x83,0xe4,0xc0,0x83,0xec,0x74});
    const auto* mouse=owned(hooks[spell_mouse_id].owner,{0x55,0x8b,0xec,0x83,0xe4,0xc0,0x83,0xec,0x38,0x56,0x8b,0x75,0x08,0x57,0x8b,0x7d,0x14,0xc7,0x07,0,0,0,0});
    const auto* click=owned(hooks[slot_click_id].owner,{0x55,0x8b,0xec,0x83,0xe4,0xf8,0x83,0xec,0x0c,0x53,0x56,0x8b,0x75,0x08,0x57,0x85,0xf6,0x0f,0x84,0xae,0x01,0,0});
    const auto* wheel=owned(wheel_owner,{0x55,0x8b,0xec,0x83,0xe4,0xf8,0x8b,0x45,0x08,0x83,0xec,0x0c,0x56,0x85,0xc0,0x0f,0x84,0x37,0x01,0,0});
    if(extended){
        // The owned wide draw/pick replace the addon's draw/pick, so they must
        // share its contract: the current index at +0x154, at most 16 of the
        // person's spells (+0x764), and slot type 1 for an empty visible cell.
        // Its init lays out the 8+2 areas checked again at activation.
        if(!current||!draw||!mouse||!click||!wheel||!addon_callback(init)||
           !addon_unique(const_cast<std::uint8_t*>(current),0x40,{0x8b,0x96,0x54,0x01,0,0,0x85,0xd2,0x78,0x73})||
           !addon_unique(const_cast<std::uint8_t*>(draw),0x100,{0x8b,0xb1,0x64,0x07,0,0,0x83,0xfe,0x10,0x7c,0x05,0xbe,0x10,0,0,0})||
           !addon_unique(const_cast<std::uint8_t*>(mouse),0x100,{0xc7,0x07,0x01,0,0,0}))return false;
    }else{
        // Without extended slots an addon init only forwards to the native one.
        for(HookId id:{spell_current_id,spell_mouse_id,slot_click_id})
            if(hooks[id].owner.owner!=EntryOwner::stock)return false;
        if(wheel_owner.owner!=EntryOwner::stock||(init.owner!=EntryOwner::stock&&!addon_callback(init)))return false;
    }
    // Equip is the addon's perks owner (its body applies 8/16 itself).
    const std::uint8_t body_prologue[]{0x55,0x8b,0xec,0x6a,0xff};
    const auto* equip_body=addon_adapter_body(equip,2);
    if(equip.owner!=EntryOwner::stock&&(!equip_body||std::memcmp(equip_body,body_prologue,sizeof(body_prologue))))return false;
    const auto match=[&](std::uintptr_t va,std::initializer_list<std::uint8_t> bytes){return !std::memcmp(game+va-kBase,bytes.begin(),bytes.size());};
    if(!match(0x622c21,{0xff,0x90,0xcc,0,0,0})||!match(0x622c29,{0x8b,0x86,0x54,0x01,0,0})||
       !match(0x622c35,{0x3b,0x83,0x64,0x07,0,0})||!match(0x622c4b,{0xc7,0x82,0x20,0x02,0,0,0x01,0,0,0})||
       !match(0x622c9f,{0x89,0xba,0x20,0x02,0,0})||!match(0x622cbb,{0x8b,0x83,0x60,0x07,0,0})||
       !match(0x622cc1,{0x8b,0x96,0x50,0x01,0,0})||!match(0x622cce,{0x8b,0x86,0x48,0x01,0,0})||
       !match(0x5ee322,{0x8d,0x8e,0x6c,0xff,0xff,0xff})||!match(0x5ee32e,{0xff,0x50,0x64})||
       !match(0x5ee335,{0x8b,0x8e,0xb8,0,0,0})||!match(0x5ee33b,{0x8b,0xb6,0xb4,0,0,0})||
       !match(0x5ee345,{0xff,0x92,0xa8,0,0,0})||!match(0x5ee367,{0xff,0x92,0xb0,0,0,0}))return false;
    spell_capacity=extended?16:8;
    return true;
}

bool addon_hover_contract() noexcept {
    const auto match=[&](std::uintptr_t rva,std::initializer_list<std::uint8_t> bytes){
        return !std::memcmp(supported_addon+rva,bytes.begin(),bytes.size());
    };
    // Actual CampDraw adapter, fresh selection, equivalence/formatter boundary,
    // live help/skill arguments, and validation-before-child-draw order.
    return match(0x77360,{0x55,0x8b,0xec,0xff,0x75,0x0c,0x8b,0x4d,0x08,0xe8,0x12,0,0,0,0x85,0xc0,0x0f,0x95,0xc0,0x5d,0xc2,0x08,0})&&
        match(0x773c3,{0xc7,0x87,0x68,0x01,0,0,0,0,0,0})&&
        match(0x77669,{0xe8,0x62,0x98,0x01,0,0x85,0xc0,0x74,0x10,0x53,0x8b,0xc8,0xe8,0x76,0xa6,0x01,0,0x85,0xc0,0x0f,0x85,0xc0,0,0,0})&&
        match(0x7769f,{0x8b,0xce,0xe8,0xba,0xb1,0x01,0})&&
        match(0x776bd,{0x50,0xe8,0x8d,0x44,0,0})&&
        match(0x7772a,{0x8d,0x45,0xd0,0x50,0xe8,0x7d,0x85,0xfc,0xff})&&
        match(0x77742,{0x8b,0xcf,0xe8,0xc7,0x45,0,0,0x8b,0x75,0xc8,0x8b,0xcf,0x56,0xe8,0x9c,0x91,0x01,0})&&
        match(0x92860,{0x68,0x60,0xe7,0x5e,0,0x51,0xe8,0x45,0xdf,0xff,0xff,0xc3})&&
        match(0x907b0,{0x58,0x59,0x87,0x04,0x24,0xff,0xe0})&&
        match(0x7bcd1,{0xc2,0x04,0})&&match(0x474a7,{0xc2,0x04,0});
}
bool prepare_hover_contract() noexcept {
    // Addon setters are forwarded through their member adapters unchanged.
    const auto allowed=[&](HookId id,unsigned arguments){
        const auto& owner=hooks[id].owner;
        return owner.owner==EntryOwner::stock||addon_adapter_body(owner,arguments);
    };
    const std::uint8_t compare[]{0x8b,0x11,0x57,0xff,0x52,0x38,0x85,0xc0,0x75,0x2a};
    const std::uint8_t changed[]{0x8b,0xcb,0xe8,0x4b,0,0,0};
    const std::uint8_t canonical[]{0x8b,0x43,0x14,0x8d,0x54,0x24,0x60};
    const std::uint8_t getter[]{0x8b,0x81,0x94,0x01,0,0,0x85,0xc0,0x74,0x09,
        0x8b,0x81,0x90,0x01,0,0,0x8b,0,0xc3,0x33,0xc0,0xc3};
    return (hooks[camp_draw_id].owner.owner==EntryOwner::stock||
            (addon_hover_selection&&hooks[addon_info_help_id].installed&&hooks[addon_info_skill_id].installed))&&
        // The addon calls its help body directly too; that body is observed.
        (hooks[info_help_id].owner.owner==EntryOwner::stock||hooks[addon_info_help_id].installed)&&
        hooks[info_item_id].owner.owner==EntryOwner::stock&&
        !std::memcmp(game+0x1ee6e2,compare,sizeof(compare))&&
        !std::memcmp(game+0x1ee70e,changed,sizeof(changed))&&
        !std::memcmp(game+0x1ee720,getter,sizeof(getter))&&
        !std::memcmp(game+0x1f16df,canonical,sizeof(canonical))&&
        allowed(info_text_id,1)&&allowed(info_skill_id,2)&&allowed(info_help_id,2);
}

bool comparison_contract() noexcept {
    const auto match=[&](std::uintptr_t va,std::initializer_list<std::uint8_t> bytes){return !std::memcmp(game+va-kBase,bytes.begin(),bytes.size());};
    // An addon info draw (Dejavu's larger preview) is the native preview draw
    // with another depth/size: GetItem, then pScreen(+0x14C)->DrawItem at
    // nX(+0x1C0)/nY(+0x1C4). It is forwarded under the card placement.
    auto* draw=const_cast<std::uint8_t*>(addon_callback(hooks[info_draw_id].owner));
    const bool draw_owner=hooks[info_draw_id].owner.owner==EntryOwner::stock||
        (draw&&addon_unique(draw,0x80,{0xdb,0x86,0xc0,0x01,0,0})&&addon_unique(draw,0x80,{0xdb,0x86,0xc4,0x01,0,0})&&
         addon_unique(draw,0x80,{0x8b,0x8e,0x4c,0x01,0,0}));
    return draw_owner&&
        match(0x7005af,{0x56,0x57,0x8b,0x7c,0x24,0x0c,0x57,0xe8,0x16,0xa6,0xfe,0xff,0x8b,0xf0,0x59,0x85,0xf6})&&
        match(0x7005d8,{0xff,0x74,0x24,0x04,0xe8,0xcc,0xa2,0xfe,0xff,0x59,0xc3})&&
        match(0x60e210,{0x8b,0x44,0x24,0x04,0x56,0x8b,0xf1,0x50,0xe8,0xe3,0x04,0xff,0xff,0x8b,0x4c,0x24,0x0c,
            0xc7,0x06,0x58,0x1b,0x74,0,0x89,0x8e,0x4c,0x01,0,0,0xc7,0x86,0x94,0,0,0,0x24,0x1b,0x74,0,0x8b,0xc6,0x5e,0xc2,0x08,0})&&
        match(0x661050,{0x6a,0xff,0x68,0x0c,0x01,0x73,0,0x64,0xa1,0,0,0,0,0x50,0x64,0x89,0x25,0,0,0,0})&&
        match(0x6fe925,{0x8b,0xc1,0x33,0xc9,0xc7,0,0x5c,0x5f,0x74,0,0x89,0x48,0x04,0x89,0x48,0x10,0x89,0x48,0x0c,0x89,0x48,0x08,0xc3})&&
        match(0x60d22a,{0xc7,0x06,0xdc,0x04,0x74,0,0xc7,0x86,0x94,0,0,0,0xa8,0x04,0x74,0})&&
        match(0x5ee440,{0x6a,0xff,0x68,0x9b,0x94,0x72,0,0x64,0xa1,0,0,0,0,0x50,0x64,0x89,0x25,0,0,0,0})&&
        match(0x5ee380,{0x6a,0xff,0x68,0x74,0x94,0x72,0,0x64,0xa1,0,0,0,0,0x50,0x64,0x89,0x25,0,0,0,0})&&
        match(0x5ee420,{0x56,0x8b,0xf1,0xe8,0x58,0xff,0xff,0xff,0xf6,0x44,0x24,0x08,0x01,0x74,0x09})&&
        match(0x5ee410,{0xe9,0x1b,0x05,0x01,0})&&
        match(0x6611d0,{0x6a,0xff,0x68,0x72,0x01,0x73,0,0x64,0xa1,0,0,0,0,0x50,0x64,0x89,0x25,0,0,0,0})&&
        match(0x6fe958,{0xb8,0x58,0x9d,0x73,0,0xe8,0x16,0xda,0xfe,0xff,0x51,0x56,0x8b,0xf1,0x89,0x75,0xf0})&&
        match(0x60fccf,{0x8b,0x45,0x10,0x05,0xfc,0xcf,0xff,0xff})&&
        match(0x60fe20,{0xe6,0xfc,0x60,0,0x18,0xfd,0x60,0,0xfa,0xfd,0x60,0,0xf2,0xfd,0x60,0,0x0c,0xfe,0x60,0})&&
        match(0x60fd18,{0x8b,0xcd,0xe8,0xc1,0x3c,0x09,0,0x89,0x44,0x24,0x10,0x8b,0x86,0x54,0x07,0,0,0x85,0xc0,0x7e,0x30,0x8b,0x86,0x50,0x07,0,0})&&
        match(0x6a39e0,{0x8b,0x15,0x1c,0x87,0x7c,0,0x56,0x8b,0x35,0x18,0x87,0x7c,0,0xb8,0x1f,0x85,0xeb,0x51,0x2b,0xd6,0x8b,0x49,0x18})&&
        match(0x60fede,{0x8b,0x8e,0x74,0x07,0,0,0x3b,0xc1,0x73,0x07,0x49,0x89,0x8e,0x74,0x07,0,0})&&
        match(0x60fefd,{0x8b,0x8e,0x74,0x07,0,0,0x3b,0xc8,0x72,0x07,0x48,0x89,0x86,0x74,0x07,0,0})&&
        match(0x60e94d,{0xc7,0x87,0x74,0x07,0,0,0xff,0xff,0xff,0xff});
}

bool constructor_contract() noexcept {
    const auto match=[&](std::uintptr_t va,std::initializer_list<std::uint8_t> bytes){
        return !std::memcmp(game+va-kBase,bytes.begin(),bytes.size());};
    // Complete native helpers, their AL-tested calls, and the input Party list
    // producers establish classification and layout once, before any scan.
    if(!match(0x5ee050,{0x8b,0x44,0x24,0x04,0x81,0x78,0x10,0x07,0x30,0,0,0x74,0x05,0x32,0xc0,0xc2,0x04,0,
            0x83,0x78,0x4c,0x02,0x74,0x05,0x32,0xc0,0xc2,0x04,0,0x83,0x78,0x50,0x03,0x0f,0x94,0xc0,0xc2,0x04,0})||
       !match(0x5ee0d0,{0x8b,0x44,0x24,0x04,0x81,0x78,0x10,0x07,0x30,0,0,0x74,0x05,0x32,0xc0,0xc2,0x04,0,
            0x83,0x78,0x4c,0x02,0x74,0x05,0x32,0xc0,0xc2,0x04,0,0x8b,0x40,0x50,0x85,0xc0,0x74,0x0f,0x83,0xf8,0x01,
            0x74,0x0a,0x83,0xf8,0x02,0x74,0x05,0x32,0xc0,0xc2,0x04,0,0xb0,0x01,0xc2,0x04,0})||
       !match(0x611b80,{0x8b,0x86,0xd4,0x01,0,0})||!match(0x611b95,{0x8b,0x48,0x44})||
       !match(0x611bb4,{0x8b,0x48,0x40,0x8b,0x16,0x6a,0x02,0x8b,0x3c,0xa9,0x8b,0xce})||
       !match(0x611c05,{0xe8,0x46,0xc4,0xfd,0xff,0x84,0xc0})||
       !match(0x611e25,{0x8b,0x86,0xc0,0x01,0,0})||!match(0x611e3a,{0x8b,0x48,0x44})||
       !match(0x611e59,{0x8b,0x48,0x40,0x8b,0x16,0x6a,0x06,0x8b,0x3c,0xa9,0x8b,0xce})||
       !match(0x611eaa,{0xe8,0x21,0xc2,0xfd,0xff,0x84,0xc0})||
       !match(0x611cdc,{0x6a,0x67,0xe9,0x9c,0x02,0,0})||
       !match(0x611f7d,{0x6a,0x66,0x8b,0x8e,0xdc,0x01,0,0,0xe8,0x86,0x09,0xfe,0xff})||
       !match(0x60f0fc,{0xb8,0x67,0,0,0})||!match(0x60f130,{0xb8,0x66,0,0,0})||
       !match(0x60f3da,{0x8b,0xce,0xe8,0x4f,0x25,0,0,0x8b,0x7c,0x24,0x38,0x8b,0xce,0x57,0xe8,0x03,0x1b,0xff,0xff}))return false;
    const std::uint8_t validation[]{0x6a,0xff,0x68,0xd0,0xb8,0x72,0,0x64,0xa1,0,0,0,0,0x50,0x64,0x89,0x25,0,0,0,0,0x83,0xec,0x24};
    const auto owner=resolve_entry_owner(game+0x211930,validation,sizeof(validation),7,&addon_module_contains);
    if(owner.owner==EntryOwner::stock)return true;
    // The addon's validation (one-argument adapter) keeps the native body:
    // weapon/armour input Party lists (+0x1D4/+0x1C0, size +0x44, data +0x40)
    // classified by the native helpers 0x5EE050/0x5EE0D0 through its thunks,
    // the 103 fallback, and the 102 fallback through its own help body.
    std::uint8_t* body=addon_adapter_body(owner,1);
    const std::uint8_t prologue[]{0x55,0x8b,0xec,0x6a,0xff};
    if(!body||std::memcmp(body,prologue,sizeof(prologue)))return false;
    const auto call_target=[](const std::uint8_t* call){
        std::int32_t relative=0;std::memcpy(&relative,call+1,sizeof(relative));return call+5+relative;};
    // A call to the addon's `push ecx-object; push native; call invoker` thunk
    // for `native` within 0x80 bytes after `site`.
    const auto calls_native=[&](const std::uint8_t* site,std::uint32_t native){
        if(!site)return false;
        for(const auto* call=site;call<site+0x80;++call){
            if(*call!=0xe8)continue;
            const auto* thunk=call_target(call);
            if(!addon_module_contains(thunk)||!addon_module_contains(thunk+20))continue;
            const std::uint8_t head[]{0x55,0x8b,0xec,0xff,0x75,0x08,0x68};
            const std::uint8_t tail[]{0x5d,0xc2,0x04,0};
            std::uint32_t target=0;std::memcpy(&target,thunk+7,sizeof(target));
            if(!std::memcmp(thunk,head,sizeof(head))&&target==native&&thunk[11]==0x51&&thunk[12]==0xe8&&
               !std::memcmp(thunk+17,tail,sizeof(tail)))return true;
        }
        return false;
    };
    const auto* help=addon_unique(body,0x1200,{0x6a,0x66,0x8b,0x8f,0xdc,0x01,0,0,0xe8});
    const auto* help_body=addon_adapter_body(hooks[info_help_id].owner,2);
    return addon_unique(body,0x1200,{0x8b,0x8f,0xd4,0x01,0,0,0x8b,0x51,0x44})&&
        calls_native(addon_unique(body,0x1200,{0x8b,0x41,0x40,0x8b,0xcf,0x6a,0x02,0x8b,0x1c,0x90}),0x5ee050)&&
        addon_unique(body,0x1200,{0x8b,0x8f,0xc0,0x01,0,0,0x8b,0x51,0x44})&&
        calls_native(addon_unique(body,0x1200,{0x8b,0x41,0x40,0x8b,0xcf,0x6a,0x06,0x8b,0x1c,0x90}),0x5ee0d0)&&
        addon_unique(body,0x1200,{0x6a,0x67,0xe9})&&
        help&&help_body&&call_target(help+8)==help_body;
}

void destroy_comparison_stage(void* self,int stage) noexcept {
    if(stage>=5){function<void*(__thiscall*)(void*,unsigned)>(0x5ee420)(self,1);return;}
    if(stage>=4)function<VoidFn>(0x6fe958)(static_cast<std::uint8_t*>(self)+0x1dc);
    if(stage>=3)function<VoidFn>(0x6fe958)(static_cast<std::uint8_t*>(self)+0x1c8);
    if(stage>=2)function<VoidFn>(0x6611d0)(static_cast<std::uint8_t*>(self)+0x150);
    if(stage>=1)function<VoidFn>(0x5ee410)(self);
    function<void(__cdecl*)(void*)>(0x7005d8)(self);
}
void destroy_comparison() noexcept {
    comparison_current=false;
    comparison_modifiers=-1;
    if(void* self=std::exchange(comparison_owner,nullptr))destroy_comparison_stage(self,5);
}
void destroy_blueprint() noexcept {
    constructor_card=ConstructorCard::none;
    blueprint_modifiers=result_modifiers=-1;
    if(void* self=std::exchange(blueprint_owner,nullptr))destroy_comparison_stage(self,5);
}
void* make_comparison() {
    // Stage advances only after a native constructor returns successfully.
    void* self=function<void*(__cdecl*)(std::size_t)>(0x7005af)(0x1f0);
    if(!self)return nullptr;
    int stage=0;
    bool completed=false;
    __try {
        function<void*(__thiscall*)(void*,void*,void*)>(0x60e210)(self,field<void*>(screen,0x98),screen);stage=1;
        function<void*(__thiscall*)(void*)>(0x661050)(static_cast<std::uint8_t*>(self)+0x150);stage=2;
        function<void*(__thiscall*)(void*)>(0x6fe925)(static_cast<std::uint8_t*>(self)+0x1c8);stage=3;
        function<void*(__thiscall*)(void*)>(0x6fe925)(static_cast<std::uint8_t*>(self)+0x1dc);stage=4;
        field<void*>(self,0)=game+0x3404dc;field<void*>(self,0x94)=game+0x3404a8;stage=5;
        if(field<void*>(self,0x118)||field<int>(self,0x11c)||field<int>(self,0x120)||field<int>(self,0x124))return nullptr;
        void* raw=function<void*(__cdecl*)(std::size_t)>(0x7005af)(0x28);
        if(!raw)return nullptr;
        auto* data=reinterpret_cast<void**>((reinterpret_cast<std::uintptr_t>(raw)+0x23)&~std::uintptr_t(31));
        data[-1]=raw;data[0]=nullptr;
        // Transfer one empty slot to the native Surface array before its single Init.
        field<void*>(self,0x118)=data;field<int>(self,0x120)=1;
        function<void(__thiscall*)(void*,int,int)>(0x5ee440)(self,600,100);
        if(field<int>(self,0x11c)!=1||!field<void**>(self,0x118)[0])return nullptr;
        completed=true;return self;
    } __finally {if(!completed)destroy_comparison_stage(self,stage);}
}
void refresh_comparison(void* item) {
    if(!comparison_owner||!item||!(GetKeyState(VK_SHIFT)&0x8000)||!session.allowed(current_camp_input()))return;
    void* person=current_person();if(!person)return;
    const int index=field<int>(person,0x774),count=field<int>(person,0x734);
    void* weapon=index>=0&&index<count?field<void**>(person,0x730)[index]:nullptr;
    void* counterpart=camp::frontend::comparison_item<void*>(item,field<int>(item,0x10),weapon,field<int>(person,0x754),
        [&](int i){return field<void**>(person,0x750)[i];},
        [&](void* armor){return function<int(__thiscall*)(void*)>(0x6a39e0)(armor);});
    camp::frontend::publish_comparison(publication,counterpart,int(item_modifiers()),
        [&](void* value){original<void(__thiscall*)(void*,void*)>(info_item_id)(comparison_owner,value);},
        [&]{return function<void*(__thiscall*)(void*)>(0x5ee720)(comparison_owner);},
        [&]{info_text(comparison_owner,nullptr);});
}
void refresh_constructor() {
    const auto get_item=function<void*(__thiscall*)(void*)>(0x5ee720);
    void* result=field<void*>(screen,0x1dc);
    const int screen_mode=mode();
    void* selected=nullptr;
    if((screen_mode==3||screen_mode==5)&&field<BOOL>(result,0xe8)&&result_help){
        void* party=field<void*>(screen,screen_mode==3?0x1d4:0x1c0);
        selected=camp::frontend::last_blueprint<void*>(field<int>(party,0x44),
            [&](int i){return field<void**>(party,0x40)[i];},
            [&](void* item){return function<bool(__thiscall*)(void*,void*)>(screen_mode==3?0x5ee050:0x5ee0d0)(screen,item);});
    }
    camp::frontend::publish_constructor(publication,screen_mode,field<BOOL>(result,0xe8)!=FALSE,result_help,
        get_item(result),selected,int(item_modifiers()),
        [&](void* value){original<void(__thiscall*)(void*,void*)>(info_item_id)(blueprint_owner,value);},
        [&]{return get_item(blueprint_owner);},
        [&](bool actual){info_text(actual?result:blueprint_owner,nullptr);},[](const char* reason){reject(reason);});
    if(constructor_card==ConstructorCard::result&&!(fresh_hover()&&ordinary_hover.kind==HoverKind::item))
        refresh_comparison(get_item(result));
}
bool activation_contract(void* self) noexcept {
    __try {
        // Twelve root areas are created only after the native missing-shop
        // early return. Checking them precedes consulting any child fields.
        if (field<int>(self, 0x12c) != 12 || mode() < 0 || mode() > 6) return false;
        auto** children=field<void**>(self,0x148);
        const int child_count=field<int>(self,0x14c);
        if(!children||child_count<8)return false;
        const auto is_child=[&](void* child){for(int i=0;i<child_count;++i)if(children[i]==child)return true;return false;};
        for(std::size_t offset:{std::size_t(0x16c),std::size_t(0x164),std::size_t(0x1dc),std::size_t(0x1d8),std::size_t(0x1f4)})
            if(!is_child(field<void*>(self,offset)))return false;
        for (std::size_t offset : {std::size_t(0x1e0), std::size_t(0x1e4), std::size_t(0x1e8)}) {
            void* child = field<void*>(self, offset);
            if (!child || !is_child(child) || field<void*>(child, 0x14c) != self) return false;
        }
        void* wagon = field<void*>(self, 0x1e0);
        void* slots = field<void*>(self, 0x1e4);
        void* spells = field<void*>(self, 0x1e8);
        for (std::size_t offset : {std::size_t(0x1ec), std::size_t(0x1f0)}) {
            if (!field<bool>(self, offset==0x1ec?0x195:0x196))continue;
            void* vendor=field<void*>(self,offset);
            if(!vendor||!is_child(vendor)||field<void*>(vendor,0x14c)!=self||field<int>(vendor,0x12c)!=9||
               field<int>(vendor,0x10c)!=18||field<int>(vendor,0x150)<0||field<int>(vendor,0x150)>=6)return false;
        }
        const auto rotate_left=area_rect(self,2),rotate_right=area_rect(self,3);
        return rotate_left.left==300&&rotate_left.top==470&&rotate_left.right==330&&rotate_left.bottom==500&&
            rotate_right.left==470&&rotate_right.top==470&&rotate_right.right==500&&rotate_right.bottom==500&&
            field<int>(slots,0x10c)==16&&field<int>(wagon, 0x12c) == 9 && field<int>(slots, 0x12c) == 15 &&
            field<int>(spells, 0x12c) == (spell_capacity==16?10:8) && field<int>(wagon, 0x10c) == 18 &&
            field<int>(wagon, 0x150) >= 0 && field<int>(wagon, 0x150) < 6;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void clear_owned_layout() noexcept {
    camp_inventory_cancel_drag();
    destroy_comparison();
    destroy_blueprint();
    session.retire_input();ordinary_hover.screen_mode=-1;result_help=0;
    if(!restore_perk_viewport())reject("perk_viewport_restore_failed");
    grids = {}; spell_frame = nullptr; spell_background = nullptr; quick_backgrounds = {};
    camp_background=nullptr;camp_panels={};
    popup_background=nullptr;scroll_art={};decoration_pending=false; cards_drawn={};
    popup_submit(CampRenderRequest::invalidate_popup,nullptr,nullptr);
}

bool release_owned_layout() noexcept {
    if(!restore_perk_viewport()){reject("perk_viewport_restore_failed");return false;}
    if(screen){
        if(camp_background||camp_panels[0]){
            void* background_owner=field<void*>(screen,0x16c);
            if(camp_background)function<BOOL(__thiscall*)(void*,void*)>(0x5ff700)(background_owner,camp_background);
            if(popup_background)function<BOOL(__thiscall*)(void*,void*)>(0x5ff700)(background_owner,popup_background);
            for(void* art:scroll_art)if(art)function<BOOL(__thiscall*)(void*,void*)>(0x5ff700)(background_owner,art);
            for(void* panel:camp_panels)if(panel)function<BOOL(__thiscall*)(void*,void*)>(0x5ff700)(background_owner,panel);
        }
        for(auto& grid:grids)if(grid.object){
            if(grid.background)function<BOOL(__thiscall*)(void*,void*)>(0x5ff700)(grid.object,grid.background);
            if(grid.text){
                auto** surfaces=field<void**>(grid.object,0x118);
                for(int i=0;i<field<int>(grid.object,0x11c);++i)if(surfaces[i]==grid.text){
                    function<void(__thiscall*)(void*,int)>(0x5ffbf0)(grid.object,i);break;
                }
            }
        }
        if(spell_frame||spell_background){
            void* spells=field<void*>(screen,0x1e8);
            if(spell_frame)function<BOOL(__thiscall*)(void*,void*)>(0x5ff700)(spells,spell_frame);
            if(spell_background)function<BOOL(__thiscall*)(void*,void*)>(0x5ff700)(spells,spell_background);
        }
        for(void* background:quick_backgrounds)if(background)
            function<BOOL(__thiscall*)(void*,void*)>(0x5ff700)(field<void*>(screen,0x1e4),background);
    }
    clear_owned_layout();
    return true;
}

void prepare_owned_layout(void* self) {
    // A completed activation must have the named native camp children,
    // their parent, and the supported area/list ABI before wide work starts.
    if(!prepare_spell_contract()){reject("spell_slot_owner_contract_differs");return;}
    if (!activation_contract(self)) {
        reject("activation_child_or_slot_contract_differs"); return;
    }
    auto* wagon = field<void*>(self, 0x1e0);
    auto* spells = field<void*>(self, 0x1e8);
    if (!coordinate_contract(self)) { reject("native_centered_virtual_mapping_differs"); return; }
    if(!prepare_hover_contract()){reject("ordinary_hover_owner_contract_differs");return;}
    if(!comparison_contract()){reject("comparison_info_family_contract_differs");return;}
    if(!constructor_contract()){reject("constructor_input_or_publication_contract_differs");return;}
    comparison_owner=make_comparison();
    if(!comparison_owner){reject("comparison_info_initialization_failed");return;}
    blueprint_owner=make_comparison();
    if(!blueprint_owner){reject("blueprint_info_initialization_failed");return;}
    if(!prepare_perk_viewport()){reject("perk_viewport_producer_contract_differs");return;}
    void* background_owner=field<void*>(self,0x16c);
    camp_background=add_figure(background_owner);
    if(!camp_background)return;
    function<TilingFn>(0x5e7680)(camp_background,32,100,layout.left,0,layout.left+layout.width,600,
                                14,14,114,114,int(std::ceil(layout.width/100)),6);
    popup_background=add_figure(background_owner);
    if(!popup_background)return;
    // Isolated card depth is cleared again before the native preview.
    function<BarFn>(0x5e70e0)(popup_background,32.f,100,0,0,200,300,14,14,114,114);
    for(auto& art:scroll_art){art=add_figure(background_owner);if(!art)return;}
    function<BarFn>(0x5e70e0)(scroll_art[0],32,100,0,0,12,100,14,14,114,114);
    function<FrameFn>(0x5e8450)(scroll_art[1],24,100,0,0,12,100,1.5f,14,14,114,114);
    for(unsigned i=0;i<camp_panels.size();++i){
        camp_panels[i]=add_figure(background_owner);
        if(!camp_panels[i])return;
        const auto r=camp::frontend::background_panel(layout,int(i));
        function<BarFn>(0x5e70e0)(camp_panels[i],28,100,r.left,r.top,r.right,r.bottom,14,14,114,114);
    }
    prepare_grid(grids[0], wagon);
    if(!valid)return;
    unsigned next = 1;
    if (field<bool>(self, 0x195)) prepare_grid(grids[next++], field<void*>(self, 0x1ec));
    if (field<bool>(self, 0x196)) prepare_grid(grids[next++], field<void*>(self, 0x1f0));
    if(!valid)return;
    spell_frame = add_figure(spells);
    if(!spell_frame)return;
    function<FrameFn>(0x5e8450)(spell_frame, 8, 100, 0, 0, 100, 100, 15, 14, 142, 114, 242);
    spell_background = add_figure(spells);
    if(!spell_background)return;
    function<BarFn>(0x5e70e0)(spell_background,16,100,0,0,100,100,14,142,114,242);
    for(int i=0;i<2;++i){
        quick_backgrounds[i]=add_figure(field<void*>(self,0x1e4));
        if(!quick_backgrounds[i])return;
        function<BarFn>(0x5e70e0)(quick_backgrounds[i],16,100,0,0,100,100,14+i*128,14,114+i*128,114);
    }
    bridge_append_probe_event("camp_inventory_active", "owned_grid_layout_pick_scroll_native_item_visuals_temporary");
}

void __fastcall activate(void* self, void*) {
    // Icon bindings belong to the texture-role generation, not this panel.
    original<VoidFn>(activate_id)(self);
    ++camp_visit;
    session.activate(self); valid = true; failure = nullptr; icon_catalogue_cursor={}; icon_tab_cursors={}; belt_first = 0; belt_snapshot={};
    clear_owned_layout();
    layout = camp::make_layout(float(output_width), float(output_height), float(frame_size));
    applied_wide = requested_wide;
    layout.wide = layout.wide && applied_wide;
    if (layout.wide) prepare_owned_layout(self);
}
bool camp_diagnostic_enabled() noexcept {
    static const bool enabled=[](){char value[8]{};return GetEnvironmentVariableA("EI_RENDERER_CAMP_DIAGNOSTIC",value,sizeof(value))>0 && value[0]=='1';}();
    return enabled;
}
void __fastcall deactivate(void* self, void*) {
    const bool observe=camp_diagnostic_enabled();
    if(observe && screen==self)close_observation_pending=true;
    const int current_mode=observe?mode():0;
    LARGE_INTEGER begin{},native_begin{},end{},frequency{};
    if(observe)QueryPerformanceCounter(&begin);
    if(screen==self&&!restore_perk_viewport())reject("perk_viewport_restore_failed");
    if(screen==self)camp_inventory_cancel_drag();
    if (screen == self) { session.deactivate(self); valid = false; clear_owned_layout(); }
    if(observe)QueryPerformanceCounter(&native_begin);
    original<VoidFn>(deactivate_id)(self);
    if(observe){
        QueryPerformanceCounter(&end);QueryPerformanceFrequency(&frequency);
        close_observation_qpc=end.QuadPart;
        const double scale=frequency.QuadPart?1000.0/frequency.QuadPart:0;
        char detail[256]{};
        std::snprintf(detail,sizeof(detail),"mode=%d begin=%lld end=%lld layout_ms=%.3f native_ms=%.3f",current_mode,begin.QuadPart,end.QuadPart,
            (native_begin.QuadPart-begin.QuadPart)*scale,(end.QuadPart-native_begin.QuadPart)*scale);
        bridge_append_probe_event("camp_close_boundary",detail);
    }
}
BOOL __fastcall camp_draw(void* self, void*, void* visualizator) {
    publication.begin();result_help=0;
    if(screen==self&&layout.wide){
        if(session.hover.screen_mode!=mode())result_modifiers=-1;
        session.begin(mode(),item_modifiers()|((GetKeyState(VK_SHIFT)&0x8000)?4u:0u),current_camp_input());
    }
    if(scroll_drag.window&&(!current_camp_input()||mode()!=scroll_drag.screen_mode))camp_inventory_cancel_drag();
    decoration_pending=false; cards_drawn={};
    popup_submit(CampRenderRequest::invalidate_popup,nullptr,nullptr);
    const auto next_layout=camp::make_layout(float(output_width),float(output_height),float(frame_size));
    if (self == screen && (requested_wide != applied_wide || layout.width!=next_layout.width ||
                          layout.unit!=next_layout.unit || layout.cell!=next_layout.cell)) {
        // F10 publishes a request after the previous frame's UI flush.
        // Only our appended resources are removed; the active native
        // children, selected mode and transaction remain authoritative.
        if(!release_owned_layout())return TRUE;
        layout = next_layout;
        applied_wide = requested_wide;
        layout.wide = layout.wide && applied_wide;
        valid = true; failure = nullptr;
        if (layout.wide) prepare_owned_layout(self);
        bridge_append_probe_event("camp_inventory_layout_changed",
            applied_wide ? "wide_grid_selected" : "original_ui_comparison");
    }
    if (self == screen && layout.wide && !valid) return TRUE;
    if (self == screen && layout.wide) {
        try {
            begin_icon_priority_frame();
        } catch (...) { reject("icon_owned_stream_allocation_failed"); }
        if (!valid) return TRUE;
    }
    if(mode()==0)belt_first=0;
    if(self==screen&&layout.wide)follow_equipped_spell();
    // Camp visuals belong after world effects, even when no card is visible.
    if(self==screen&&layout.wide&&
       !popup_submit(CampRenderRequest::begin_interface,nullptr,nullptr)){
        reject("camp_interface_composition_failed");return TRUE;
    }
    struct DrawingScope { bool old=camp_drawing; DrawingScope(){camp_drawing=true;} ~DrawingScope(){camp_drawing=old;} } current;
    const BOOL result=original<DrawFn>(camp_draw_id)(self, visualizator);
    if (self==screen && layout.wide && valid) {
        try {
            // Draw established current row/tab priorities before background work.
            stream_camp_icon_names();
        } catch (...) { reject("icon_owned_stream_allocation_failed"); }
    }
    if(valid&&layout.wide&&cards_drawn[2])info_draw(comparison_owner,nullptr,visualizator);
    if(valid&&layout.wide&&cards_drawn[1]&&constructor_card==ConstructorCard::blueprint)
        info_draw(blueprint_owner,nullptr,visualizator);
    if(!ordinary_hover.touched)reset_hover();
    return result;
}

int __fastcall hit_test(void* self, void*, POINT p) {
    if (screen && layout.wide) {
        const int result = hit(self, p);
        if (result != -2) return result;
    }
    return original<HitFn>(hit_id)(self, p);
}
BOOL __fastcall inside(void* self, void*, POINT p) {
    const Role role=role_of(self);
    if(screen&&layout.wide&&role!=Role::none)
        return valid&&camp::frontend::inside(layout,role,mode(),virtual_point(self,p));
    return original<HitFn>(inside_id)(self,p);
}
BOOL __fastcall point_in_area(void* area, void*, POINT point) {
    void* object = widescreen_interface_owned() ? const_cast<void*>(widescreen_input_area_object()) : tip_object;
    const Role role = screen && valid && layout.wide && object ? role_of(object) : Role::none;
    if (role != Role::none) {
        auto** areas = field<void**>(object, 0x128);
        const int count = field<int>(object, 0x12c);
        for (int i = 0; i < count; ++i) if (areas[i] == area) {
            const auto s=input_state(object,role);
            return camp::frontend::point_in_area(layout,s,i,area_placement(object,role,i),point,
                [&](int native,POINT q){return native_area_contains(areas[native],q)!=FALSE;},true);
        }
    }
    return original<BOOL(__thiscall*)(void*, POINT)>(point_id)(area, point);
}
void __fastcall tips2d(void* self, void*) {
    void* previous = tip_object;
    if (screen && layout.wide && role_of(self) != Role::none) tip_object = self;
    original<VoidFn>(tips2d_id)(self);
    tip_object = previous;
}
void __fastcall tips(void* self, void*, void* v) {
    void* previous = tip_object;
    if (screen && layout.wide && role_of(self) != Role::none) tip_object = self;
    original<void(__thiscall*)(void*, void*)>(tips_id)(self, v);
    tip_object = previous;
}

camp::Placement uniform(Role role) noexcept {
    return camp::frontend::uniform(layout,mode(),role);
}

void draw_scrollbar(ScrollList list,void* v) {
    camp::frontend::draw_rail(scroll_rail(list),[&](int i,camp::Placement p){
        Scope placed(p);function<DrawFn>(0x5e6d90)(scroll_art[i],v);
    },[&](camp::Rect thumb){if(camp_drawing&&decoration_pending)decoration.thumbs[decoration.thumb_count++]=decoration_pixels(thumb);});
}

BOOL __fastcall object_draw(void* self, void*, void* v) {
    const Role role = role_of(self);
    if (!layout.wide || !screen || role == Role::none) return original<DrawFn>(object_draw_id)(self, v);
    if (!valid) return TRUE;
    Scope scope(role);
    if(role_card(role)>=0){
        const unsigned card=unsigned(role_card(role));
        if(!camp::frontend::card_visible(role,constructor_card,cards_drawn,session.allowed(current_camp_input())))return TRUE;
        Scope placed(uniform(role));
        void* arguments[]{self,v};
        popup_submit(CampRenderRequest::draw_popup,[](void* context){
            auto** args=static_cast<void**>(context);
            original<DrawFn>(object_draw_id)(args[0],args[1]);
        },arguments);
        return TRUE;
    }
    if(role==Role::background){
        Scope raw(Role::none);
        if(addon_hover_selection)publish_addon_hover();
        refresh_constructor();
        if(!valid)return TRUE;
        cards_drawn=camp::frontend::visible_cards(ordinary_hover,session.allowed(current_camp_input()),
            field<BOOL>(field<void*>(screen,0x164),0xe8)!=FALSE,
            field<BOOL>(field<void*>(screen,0x1dc),0xe8)!=FALSE,constructor_card,
            context_visible(2));
        camp::frontend::draw_background(layout,mode(),
            [&](camp::Rect,int,int){function<DrawFn>(0x5e6d90)(camp_background,v);},
            [&](int i,camp::Placement p){Scope placed(p);function<DrawFn>(0x5e6d90)(camp_panels[i],v);});
        if(any_cards()){
            void* arguments[]{popup_background,v};
            popup_submit(CampRenderRequest::clear_popup,[](void* context){
                auto** args=static_cast<void**>(context);
                camp::frontend::draw_card_backgrounds(layout,cards_drawn,[&](camp::Placement p){
                    Scope placed(p);function<DrawFn>(0x5e6d90)(args[0],args[1]);
                });
            },arguments);
        }
        if(camp_drawing)snapshot_decoration();
        return TRUE;
    }
    auto** figures = field<void**>(self, 0x108);
    const int count = field<int>(self, 0x10c);
    // Spell backgrounds were one horizontal tiled figure. The owned row
    // draws a native square frame at each authoritative empty or occupied slot.
    if(role==Role::spells){
        Scope raw(Role::none);const int first=spell_first();const int selected=field<int>(self,0x154);
        camp::frontend::draw_spell_frames(layout,first,spell_capacity,spell_count(),selected,[&](camp::Placement p){
            Scope placement(p);function<DrawFn>(0x5e6d90)(spell_background,v);function<DrawFn>(0x5e6d90)(spell_frame,v);
        },[&](bool visible,camp::Rect destination){
            field<BOOL>(figures[0],0x220)=visible;if(!visible)return;
            float x=0,y=0,z=0;function<void(__thiscall*)(void*,float&,float&,float&)>(0x5e7010)(figures[0],x,y,z);
            Scope placement(camp::Placement{{x-50,y-50,x+50,y+50},destination});function<DrawFn>(0x5e6d90)(figures[0],v);
        },[&]{draw_scrollbar(ScrollList::spells,v);});
        return TRUE;
    }
    if(role==Role::quick)camp::frontend::draw_quick_backgrounds(layout,belt_first,[&](int index,camp::Placement p){
        Scope placement(p);function<DrawFn>(0x5e6d90)(quick_backgrounds[index],v);
    });
    camp::frontend::draw_object_figures(layout,mode(),belt_first,role,count,[&](int i,camp::Placement p){
        if(role==Role::quick&&(figures[i]==quick_backgrounds[0]||figures[i]==quick_backgrounds[1]))return;
        Scope placement(p);function<DrawFn>(0x5e6d90)(figures[i],v);
    },[&](int i){return area_placement(self,role,i);},[&](int i){
        float x=0,y=0,z=0;function<void(__thiscall*)(void*,float&,float&,float&)>(0x5e7010)(figures[i],x,y,z);
        return std::pair<float,float>{x,y};
    },[&](int i){return root_action_visible(i);});
    if(role==Role::perks){draw_scrollbar(ScrollList::learned,v);draw_scrollbar(ScrollList::available,v);}
    // Preserve the native map-preview part of the object draw contract.
    Scope placement(uniform(role));
    auto** previews = field<void**>(self, 0x138);
    for (int i = 0; i < field<int>(self, 0x13c); ++i)
        function<DrawFn>(0x5fc2f0)(previews[i], v);
    return TRUE;
}

void __fastcall draw2d(void* self, void*) {
    const Role role = role_of(self);
    if (!screen || !layout.wide || role == Role::none) { original<VoidFn>(draw2d_id)(self); return; }
    if (!valid) return;
    if(role_card(role)>=0&&!camp::frontend::card_visible(role,constructor_card,cards_drawn,session.allowed(current_camp_input())))return;
    Scope scope(role);
    if (auto* grid = grid_for(self)) {
        // Both old 700x20 number/price strips are retired from wide drawing.
        reinterpret_cast<VoidFn>(field<void**>(grid->text, 0)[0])(grid->text);
        return;
    }
    original<VoidFn>(draw2d_id)(self);
    if(role==Role::root&&cards_drawn[2])draw2d(comparison_owner,nullptr);
    if(role==Role::root&&cards_drawn[1]&&constructor_card==ConstructorCard::blueprint)draw2d(blueprint_owner,nullptr);
}

struct DrawItem { void* item; float x, y, z, size; int part; };
void __fastcall item_draw(void* self, void*, void* v, DrawItem* item) {
    if (self == screen && layout.wide && !valid) return;
    if (self != screen || !layout.wide || !item) {
        original<ItemDrawFn>(item_draw_id)(self, v, item); return;
    }
    // The actual info Draw establishes the card role before its preview call.
    const int card=role_card(drawing);
    if(item->part==9&&(card<0||!cards_drawn[card]))return;
    const auto p=camp::frontend::item_placement(layout,mode(),belt_first,drawing,item->x,item->y,item->size,item->part);
    if(item->part==0&&item->y<100&&!camp::frontend::belt_visible(layout,p.destination,mode()==0))return;
    if (item->part == 9) {
        Scope placement(p);
        ItemTextureScope texture(self);
        texture.preview_item = item->item;
        struct Arguments { void* self; void* visualizator; DrawItem* item; ItemTextureScope* texture; } args{self,v,item,&texture};
        popup_submit(CampRenderRequest::draw_popup,[](void* context){
            auto& a=*static_cast<Arguments*>(context);
            native_item_draw(a.self,a.visualizator,a.item,a.texture);
        },&args);
        return;
    }
    Scope placement(p, {item->x - 35, item->y - 35, item->x + 35, item->y + 35});
    ItemTextureScope texture(self);
    native_item_draw(self, v, item, &texture);
}

BOOL __fastcall inventory_draw(void* self, void*, void* v) {
    auto* grid = grid_for(self);
    if (!screen || !layout.wide || !grid) return original<DrawFn>(inventory_draw_id)(self, v);
    if (!valid) return TRUE;
    form_sorted(self);
    const int first = first_item(self), count = field<int>(self, 0x1a0);
    auto** items = field<void**>(self, 0x19c);
    auto** figures = field<void**>(self, 0x108);
    const bool vendor = role_of(self) == Role::vendor;
    const int columns=layout.grid_columns(vendor),rows=layout.grid_rows(vendor);
    try { prioritize_grid_icons(*grid,first,count,columns*rows); }
    catch (...) { reject("icon_visible_priority_allocation_failed"); return TRUE; }
    const bool measure_scroll=scroll_cost_enabled();
    if(measure_scroll){
        const auto generation=bridge_camp_texture_generation();
        const int current_mode=mode();
        if(scroll_cost.observed&&(scroll_cost.generation!=generation||scroll_cost.mode!=current_mode))
            scroll_cost.valid=false;
        scroll_cost.observed=true;scroll_cost.generation=generation;scroll_cost.mode=current_mode;
        auto& grid_cost=scroll_cost.grids[vendor?1:0];
        const int category=field<int>(self,0x150);
        if(!generation||current_mode<0||current_mode>=7||first<0||count<0||category<0||category>=6||columns<=0||rows<=0)
            scroll_cost.valid=false;
        if(grid_cost.observed&&(grid_cost.first!=first||grid_cost.count!=count||grid_cost.category!=category||
            grid_cost.columns!=columns||grid_cost.rows!=rows))scroll_cost.valid=false;
        grid_cost={true,first,count,category,columns,rows};
    }
    const float top=float(field<int>(self,0x180));
    Scope raw(Role::none);
    camp::frontend::draw_grid(layout,vendor,first,top,[&](const camp::frontend::GridCell& cell){
        Scope placement(cell.placement);
        function<DrawFn>(0x5e6d90)(grid->background,v);
        function<DrawFn>(0x5e6d90)(figures[10],v);
        if(cell.index>=count||!items[cell.index])return;
        DrawItem record{items[cell.index],100,top+50,field<float>(self,0x184)*1.5f,100,field<int>(self,0x148)};
        Scope icon(cell.placement,cell.icon_clip);
        ItemTextureScope texture(screen,measure_scroll);
        native_item_draw(screen,v,&record,&texture);
    },[&](int i,camp::Rect target){
        Scope placement(camp::Placement{area_rect(self,i+2),target});
        function<DrawFn>(0x5e6d90)(figures[i+2],v);
    },[&]{draw_scrollbar(!vendor?ScrollList::stash:self==field<void*>(screen,0x1ec)?ScrollList::spell_vendor:ScrollList::item_vendor,v);});
    // Match native lifetime order: finish every figure/icon draw before
    // acquiring the text DC, then release it before surface composition.
    function<void*(__thiscall*)(void*)>(0x5fdc00)(grid->text);
    function<VoidFn>(0x5fe2f0)(grid->text);
    using TextFn = int(__thiscall*)(void*, const char*, RECT*, int, COLORREF, int, BOOL);
    const int part = field<int>(self, 0x148);
    const int price_type = field<int>(self, 0x1b0);
    const auto table = field<void**>(screen, 0);
    camp::frontend::grid_labels(layout,vendor,first,count,[&](int,int index,RECT quantity_rect,RECT price_rect){
        void* item=items[index];if(!item)return;
        char number[32];const int quantity=field<int>(item,0x48);
        if(quantity!=1){std::snprintf(number,sizeof(number),"%d",quantity);
            function<TextFn>(0x5fde20)(grid->text,number,&quantity_rect,1,RGB(228,215,167),DT_RIGHT,TRUE);}
        if(price_type!=9&&reinterpret_cast<bool(__thiscall*)(void*,void*,int)>(table[0xac/4])(screen,item,part)){
            const int price=reinterpret_cast<int(__thiscall*)(void*,void*,int)>(table[0xd8/4])(screen,item,price_type);
            std::snprintf(number,sizeof(number),"%d",price);
            function<TextFn>(0x5fde20)(grid->text,number,&price_rect,1,RGB(228,215,167),DT_CENTER,TRUE);
        }
    });
    function<VoidFn>(0x5fdc60)(grid->text);
    return TRUE;
}

void* __fastcall inventory_mouse(void* self, void*, POINT point, int* slot) {
    if (!screen || !layout.wide || !grid_for(self))
        return original<void*(__thiscall*)(void*, POINT, int*)>(inventory_mouse_id)(self, point, slot);
    if (!valid || !field<BOOL>(self, 0xe8)) return nullptr;
    const POINT p = virtual_point(self, point);
    const bool vendor=role_of(self)==Role::vendor;
    const auto r = layout.grid(vendor);
    if (!r.contains(float(p.x), float(p.y))) return nullptr;
    const int part = field<int>(self, 0x148);
    *slot = part == 1 ? 22 : part == 2 ? 24 : part == 3 ? 23 : 0;
    form_sorted(self);
    const int index = camp::frontend::grid_pick(layout,vendor,first_item(self),p);
    return index < field<int>(self, 0x1a0) ? field<void**>(self, 0x19c)[index] : nullptr;
}

void __fastcall idle(void* self, void*) {
    if (!screen || !layout.wide || !grid_for(self)) { original<VoidFn>(idle_id)(self); return; }
    if (!valid) return;
    const int category = field<int>(self, 0x150);
    const float position = field<float>(self, 0x15c + category * 4);
    const int lock = field<int>(self, 0x154);
    const float request = field<float>(self, 0x174);
    const float previous_elapsed = field<float>(self, 0x158);
    field<int>(self, 0x154) = 0; field<float>(self, 0x174) = 0;
    // Preserve the native/addon authoritative sorting refresh, while retiring
    // its horizontal seven-visible-item scrolling and clamp.
    original<VoidFn>(idle_id)(self);
    field<int>(self, 0x154) = lock;
    field<float>(self, 0x158) = previous_elapsed;
    field<float>(self, 0x15c + category * 4) = position;
    field<float>(self, 0x174) = 0; field<float>(self, 0x178) = 0;
    const int rows=camp::frontend::idle_rows(request,lock,field<float>(self,0x158),function<float(__thiscall*)(void*)>(0x600270)(self));
    field<float>(self, 0x15c + category * 4) += rows * layout.grid_columns(role_of(self)==Role::vendor);
    first_item(self);
}
BOOL __fastcall wheel(void* handler, void*, UINT flags, short delta, POINT point) {
    // This entry receives the CI3DHandler secondary base (+94h), proven by
    // its lea ecx,[esi-94h] before HitTest; the forwarding ABI stays intact.
    auto* self = static_cast<std::uint8_t*>(handler) - 0x94;
    if (!screen || !layout.wide || !grid_for(self))
        return original<BOOL(__thiscall*)(void*, UINT, short, POINT)>(wheel_id)(handler, flags, delta, point);
    if (!valid) return TRUE;
    const float before = field<float>(self, 0x174);
    const BOOL result = original<BOOL(__thiscall*)(void*, UINT, short, POINT)>(wheel_id)(handler, flags, delta, point);
    // Native positive wheel adds to the horizontal position. Up now moves
    // toward earlier rows; the existing native sound/dispatch still runs.
    field<float>(self, 0x174) = before - (field<float>(self, 0x174) - before);
    return result;
}

void __fastcall set_to_item(void* self, void*, void* item) {
    using Fn = void(__thiscall*)(void*, void*);
    if (!screen || !layout.wide || !valid || !grid_for(self) || !item) {
        original<Fn>(set_to_item_id)(self, item); return;
    }
    std::array<float, 6> previous{};
    for (int i = 0; i < 6; ++i) previous[i] = field<float>(self, 0x15c + i * 4);
    // Native code remains authoritative for category, sorting and item
    // equivalence. Its final index-8+2 strip clamp is replaced below.
    original<Fn>(set_to_item_id)(self, item);
    const int category = field<int>(self, 0x150);
    field<float>(self, 0x15c + category * 4) = previous[category];
    int first = first_item(self), target = -1;
    auto** items = field<void**>(self, 0x19c);
    const int count = field<int>(self, 0x1a0);
    const bool vendor=role_of(self)==Role::vendor;

    // BE0FC330 SetToItem uses this Compare slot and keeps the last match.
    const auto compare = reinterpret_cast<BOOL(__thiscall*)(void*, void*)>(field<void**>(item, 0)[0x38 / 4]);
    for (int i = 0; i < count; ++i) if (compare(item, items[i])) target = i;
    first=camp::frontend::reveal_item(layout,vendor,first,target);
    field<float>(self, 0x15c + category * 4) = float(first);
    first_item(self);
}

BOOL child_draw(HookId id, void* self, void* v) {
    if (!screen || !layout.wide || role_of(self) == Role::none) return original<DrawFn>(id)(self, v);
    if (!valid) return TRUE;
    const auto role = role_of(self);
    Scope scope(role);
    camp::frontend::ChildVisualScope placement(layout,mode(),role);
    return original<DrawFn>(id)(self,v);
}
BOOL __fastcall quick_draw(void* self, void*, void* v) { return child_draw(quick_draw_id, self, v); }
BOOL __fastcall spell_draw(void* self, void*, void* v) {
    if(!screen||!layout.wide||role_of(self)!=Role::spells)return original<DrawFn>(spell_draw_id)(self,v);
    if(!valid)return TRUE;
    Scope role(Role::spells);
    const int first=spell_first();
    // Borrow from the actual current person only for this synchronous draw.
    void* person=current_person();
    const int count=person?std::clamp(field<int>(person,0x764),0,spell_capacity):0;
    auto** spells=person?field<void**>(person,0x760):nullptr;
    camp::frontend::draw_spell_items(layout,first,count,float(field<int>(self,0x150)),[&](int i,float x,float y,camp::Placement p,camp::Rect clip){
        DrawItem record{spells[i],x,y,12,100,field<int>(self,0x148)};
        Scope placement(p,clip);ItemTextureScope texture(screen);native_item_draw(screen,v,&record,&texture);
    });
    return object_draw(self,nullptr,v);
}
void* __fastcall spell_mouse(void* self,void*,POINT point,int* slot) {
    using Fn=void*(__thiscall*)(void*,POINT,int*);
    if(!screen||!layout.wide||role_of(self)!=Role::spells)return original<Fn>(spell_mouse_id)(self,point,slot);
    *slot=0;
    if(!valid||!field<BOOL>(self,0xe8))return nullptr;
    const int index=hit(self,point);
    if(index<0||index>=spell_capacity)return nullptr;
    *slot=1; // Empty visible spell cells retain the native equip-slot action.
    void* person=current_person();
    const int count=person?std::clamp(field<int>(person,0x764),0,spell_capacity):0;
    return index<count?field<void**>(person,0x760)[index]:nullptr;
}
void __fastcall spell_current(void* self,void*,void* item) {
    original<void(__thiscall*)(void*,void*)>(spell_current_id)(self,item);
    if(!screen||!layout.wide||!valid||role_of(self)!=Role::spells)return;
    const int selected=field<int>(self,0x154);
    belt_first=camp::frontend::spell_reveal(belt_first,selected,spell_count());
}
void __fastcall slot_click(void* handler,void*,UINT flags,POINT point) {
    using Fn=void(__thiscall*)(void*,UINT,POINT);
    auto* self=static_cast<std::uint8_t*>(handler)-0x94;
    if(!screen||!layout.wide||role_of(self)!=Role::spells){original<Fn>(slot_click_id)(handler,flags,point);return;}
    if(!valid)return;
    int slot=0;
    void* item=spell_mouse(self,nullptr,point,&slot);
    void* parent=field<void*>(self,0x14c);
    auto** table=field<void**>(parent,0);
    const int part=field<int>(self,0x148);
    camp::frontend::slot_click(item,slot,part,
        [&](void* value,int source){reinterpret_cast<void(__thiscall*)(void*,void*,int)>(table[0xa8/4])(parent,value,source);},
        [&](int value,int source){reinterpret_cast<void(__thiscall*)(void*,int,int)>(table[0xb0/4])(parent,value,source);});
}
BOOL __fastcall trade_draw(void* self, void*, void* v) { return child_draw(trade_draw_id, self, v); }
BOOL __fastcall repair_draw(void* self, void*, void* v) { return child_draw(repair_draw_id, self, v); }
BOOL __fastcall spell_construction(void* self, void*, void* v) { return child_draw(spell_construction_id, self, v); }
BOOL __fastcall item_construction(void* self, void*, void* v) { return child_draw(item_construction_id, self, v); }
void __fastcall unit_draw(void* self, void*, void* v, void* unit, float angle, float x, bool armors) {
    using Fn = void(__thiscall*)(void*, void*, void*, float, float, bool);
    if (self == screen && layout.wide && valid) {
        Scope role(Role::quick);
        Scope placement(uniform(Role::quick));
        original<Fn>(unit_draw_id)(self, v, unit, angle, x, armors);
    } else original<Fn>(unit_draw_id)(self, v, unit, angle, x, armors);
}

bool write_bytes(std::uint8_t* address, const void* bytes, std::size_t count) noexcept {
    DWORD protection = 0;
    if (!VirtualProtect(address, count, PAGE_EXECUTE_READWRITE, &protection)) return false;
    std::uint8_t before[5]{};
    const std::size_t noted = count < sizeof(before) ? count : sizeof(before);
    std::memcpy(before, address, noted);
    std::memcpy(address, bytes, count);
    DWORD ignored = 0;
    const bool restored = VirtualProtect(address, count, protection, &ignored) != FALSE;
    FlushInstructionCache(GetCurrentProcess(), address, count);
    owned::bridge_note_owned_patch(address, before, count);  // first 5 bytes compared
    return restored;
}
void jump(std::uint8_t* bytes, const void* address, const void* target) noexcept {
    bytes[0] = 0xe9;
    const auto relative = std::int32_t(reinterpret_cast<std::intptr_t>(target) - reinterpret_cast<std::intptr_t>(address) - 5);
    std::memcpy(bytes + 1, &relative, 4);
}

} // namespace

CampScrollCostSnapshot camp_inventory_take_scroll_cost() noexcept {
    return std::exchange(scroll_cost,CampScrollCostSnapshot{});
}

void camp_inventory_retire_textures() noexcept {
    for (auto* scope = item_texture_scope; scope; scope = scope->previous) {
        scope->surface.reset(); scope->binding=nullptr;
    }
    camp_textures.clear();
}
void camp_inventory_shutdown_icons() noexcept { camp_icon_loader.shutdown(); }
void camp_inventory_texture_manager_destroying(void* owner) noexcept {
    retire_model_texture_manager(owner, bridge_camp_texture_manager,
        camp_inventory_retire_textures, camp_inventory_shutdown_icons);
}

bool camp_inventory_texture(std::uint64_t id, std::uint64_t generation,
                            NativeOwnedTextureView& value) noexcept {
    const auto* scope = item_texture_scope;
    if (!scope || !scope->surface || scope->surface->id != id) return true;
    if (!valid || scope->surface->generation != generation) {
        reject("icon_bound_generation_differs"); return false;
    }
    value = {};
    value.identity = id;
    value.generation = generation;
    value.surface = scope->surface.get();
    value.opaque_mips = scope->surface->alpha_fully_opaque ? 1u : 0u;
    value.camp_surface_alias = true;
    return true;
}

// Temporary observation of the item submission after its slot background,
// once per completed binding per visit. No texture copies or state changes.
void camp_inventory_observe_texture(const NativeGeometryView& view,const FixedState& state,
                                    const SurfaceImage* bound) noexcept {
    if(!camp_diagnostic_enabled() || !item_texture_scope || !item_texture_scope->binding) return;
    if(++item_texture_scope->observed_draws<=item_texture_scope->background_draws)return;
    auto& binding=*item_texture_scope->binding;
    if(binding.diagnosed_visit==camp_visit)return;
    binding.diagnosed_visit=camp_visit;
    float u_min=0,u_max=0,v_min=0,v_max=0;
    if(view.texture_coordinates && view.texture_coordinate_count){
        u_min=u_max=view.texture_coordinates[0].u;
        v_min=v_max=view.texture_coordinates[0].v;
        for(unsigned i=1;i<view.texture_coordinate_count;++i){
            const auto& uv=view.texture_coordinates[i];
            u_min=(std::min)(u_min,uv.u);u_max=(std::max)(u_max,uv.u);
            v_min=(std::min)(v_min,uv.v);v_max=(std::max)(v_max,uv.v);
        }
    }
    char detail[640]{};
    std::snprintf(detail,sizeof(detail),"visit=%llu draw=%u name=%s expected=%llu bound=%llu owned=%llu generation=%llu uploaded=%llu serial=%llu gpu=%d released=%d canvas=%u color=%lu,%lu,%lu alpha=%lu,%lu,%lu uv=%.5f,%.5f..%.5f,%.5f shift=%.5f,%.5f flags=%lu",
        camp_visit,item_texture_scope->observed_draws,binding.diagnostic_name.c_str(),binding.surface?binding.surface->id:0,bound?bound->id:0,view.owned_texture.identity,
        bound?bound->generation:0,bound?bound->uploaded_generation:0,bound?bound->texture_serial:0,bound&&bound->srv?1:0,bound&&bound->cpu_released?1:0,
        bound?bound->width:0,state.stage[0][D3DTSS_COLOROP],state.stage[0][D3DTSS_COLORARG1],state.stage[0][D3DTSS_COLORARG2],
        state.stage[0][D3DTSS_ALPHAOP],state.stage[0][D3DTSS_ALPHAARG1],state.stage[0][D3DTSS_ALPHAARG2],
        u_min,v_min,u_max,v_max,view.u_shift,view.v_shift,view.flags);
    bridge_append_probe_event("camp_icon_draw_boundary",detail);
}

bool camp_inventory_observation_enabled() noexcept { return camp_diagnostic_enabled(); }
void camp_inventory_observe_count_time_clock(std::int64_t begin) noexcept {
    close_count_time_begin=begin;
}
void camp_inventory_observe_close_call(const char* phase,std::int64_t begin,std::int64_t end) noexcept {
    // A nested idle may consume pending before its containing call returns.
    if(!close_observation_pending && !(begin<=close_observation_qpc && close_observation_qpc<=end))return;
    LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);
    char detail[192]{};
    std::snprintf(detail,sizeof(detail),"visit=%llu phase=%s begin=%lld end=%lld elapsed_ms=%.3f",
        camp_visit,phase,begin,end,frequency.QuadPart?1000.0*double(end-begin)/frequency.QuadPart:0.0);
    bridge_append_probe_event("camp_close_outer_call",detail);
}
void camp_inventory_observe_modal_idle(std::int64_t begin,std::int64_t end) noexcept {
    const bool pending=std::exchange(close_observation_pending,false);
    if(!pending && !(begin<=close_observation_qpc && close_observation_qpc<=end))return;
    LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);
    const double scale=frequency.QuadPart?1000.0/frequency.QuadPart:0;
    char detail[224]{};
    std::snprintf(detail,sizeof(detail),"visit=%llu clock=%lld begin=%lld end=%lld count_time_ms=%.3f idle_ms=%.3f",
        camp_visit,close_count_time_begin,begin,end,close_count_time_begin?(begin-close_count_time_begin)*scale:0.0,(end-begin)*scale);
    bridge_append_probe_event("camp_close_outer_boundary",detail);
}

void camp_inventory_texture_failed() noexcept { reject("icon_owned_gpu_admission_failed"); }

bool camp_inventory_take_decoration(CampDecoration& value) noexcept {
    if(!decoration_pending||!screen||!layout.wide||!valid||drawing==Role::none||!camp::primary_output)return false;
    if(camp_drawing)return false;
    value=decoration;
    if(!any_cards())value.card_count=0;
    decoration_pending=false; return true;
}
void camp_inventory_art_failed() noexcept {
    if(!art_failed)bridge_append_probe_event("camp_art_unavailable","embedded_art_decode_or_gpu_creation_failed");
    art_failed=true;
}

void camp_inventory_output(unsigned width, unsigned height, unsigned size) noexcept {
    if(width!=output_width||height!=output_height||size!=frame_size)camp_inventory_cancel_drag();
    output_width = width; output_height = height; frame_size = size;
}
void camp_inventory_set_wide(bool wide) noexcept {
    if(wide==requested_wide)return;
    camp_inventory_cancel_drag();reset_hover();requested_wide=wide;
}
const char* camp_inventory_notice() noexcept {
    if (install_failed && output_width * 10 >= output_height * 16)
        return "Camp grid hooks unavailable - native camp layout is not owned (see bridge.jsonl)";
    if (!screen) return nullptr;
    if (layout.wide && !valid) return "Camp layout unavailable - see camp_inventory_rejected in bridge.jsonl";
    if (!applied_wide) return "Camp: original UI comparison";
    if (layout.wide && popup_failed) return "Camp popup unavailable - see camp_popup_unavailable in bridge.jsonl";
    if(layout.wide&&!drag_admitted)return "Camp scrollbar input unavailable - see camp_drag_unavailable in bridge.jsonl";
    if (layout.wide && art_failed) return "Camp artwork unavailable - see camp_art_unavailable in bridge.jsonl";
    return layout.wide ? (layout.compact ? "Camp: compact 16:10 grid" : "Camp: wide grid") : nullptr;
}
bool camp_inventory_wheel(POINT point,short delta,UINT flags) noexcept {
    if(!session.allowed(current_camp_input()))return false;
    return camp::frontend::wheel(session,layout,virtual_point(screen,point),delta,current_camp_input(),
        scroll_snapshot,[&](ScrollList list,camp::frontend::Scroll scroll,int steps){
            if(unsigned(list)<3){
                // Keep native row animation/sound, using the shared fractional-wheel accumulator.
                const float before=field<float>(scroll_owner(list),0x174);
                original<BOOL(__thiscall*)(void*,UINT,short,POINT)>(wheel_id)(
                    static_cast<std::uint8_t*>(scroll_owner(list))+0x94,flags,short(-steps*WHEEL_DELTA),point);
                field<float>(scroll_owner(list),0x174)=before+steps;
            }else scroll_to(list,scroll.position+steps);
        });
}
void camp_inventory_cancel_drag() noexcept {camp::frontend::cancel_drag(session);}

bool camp_inventory_input(HWND window,UINT message,WPARAM wparam,LPARAM lparam,bool renderer_controls) noexcept {
    return camp::frontend::input(session,layout,screen?mode():-1,current_camp_input(),window,
        message,wparam,lparam,renderer_controls,scroll_snapshot,scroll_to);
}
// The addon inventory allocator (CTextureLRUAllocator::CreateObject) owns
// 0x5EC020 through a one-argument adapter followed directly by its body. The
// body looks up "<name>.x4.mmp", then ".x2.mmp", then ".mmp" and selects the
// 256/1024/2048 canvases (unhuma 0x300|0x100) that camp_icon_canvas copies.
// Same instructions in the JMV 1.35 and Dejavu builds, at shifted offsets.
bool addon_icon_allocator(const EntryResolution& owner) noexcept {
    auto* callback=const_cast<std::uint8_t*>(addon_callback(owner));
    const std::uint8_t adapter[]{0x55,0x8b,0xec,0x8b,0x4d,0x08,0xe8,0x05,0,0,0,0x5d,0xc2,0x08,0};
    if(!callback||std::memcmp(callback,adapter,sizeof(adapter)))return false;
    std::uint8_t* const body=callback+0x10;
    constexpr std::size_t window=0x1900;
    if(!addon_module_contains(body+window-1))return false;
    const char* const suffixes[3]{".x4.mmp",".x2.mmp",".mmp"};
    std::size_t first[3]{window,window,window};
    for(std::size_t i=0;i+5<=window;++i){
        if(body[i]!=0x68)continue;
        const char* text=nullptr;
        std::memcpy(&text,body+i+1,sizeof(text));
        if(!addon_module_contains(text)||!addon_module_contains(text+8))continue;
        for(int k=0;k<3;++k)if(first[k]==window&&!std::strcmp(text,suffixes[k]))first[k]=i;
    }
    const auto contains=[&](std::initializer_list<std::uint8_t> bytes){
        for(std::size_t i=0;i+bytes.size()<=window;++i)
            if(!std::memcmp(body+i,bytes.begin(),bytes.size()))return true;
        return false;
    };
    return first[0]<first[1]&&first[1]<first[2]&&first[2]<window&&
        contains({0xbe,0,1,0,0})&&contains({0xbe,0,4,0,0})&&contains({0xbe,0,8,0,0})&&
        contains({0x81,0xe6,0,3,0,0,0x81,0xc6,0,1,0,0});
}
// The addon's loading-screen step (0x5DA400) unloads textures through a body
// that skips a texture whose [+8] carries the native persistent bit 0x40000,
// the bit the game sets at 0x510DD8 and resident icon bindings rely on.
bool addon_unload_keeps_persistent() noexcept {
    const std::uint8_t step[]{0x55,0x8b,0xec,0x6a,0xff,0x68,0xe1,0x86,0x72,0,0x64,0xa1,0,0,0,0,0x50,0x64,0x89,0x25,0,0,0,0};
    const auto owner=resolve_entry_owner(game+0x1da400,step,sizeof(step),5,&addon_module_contains);
    auto* callback=const_cast<std::uint8_t*>(addon_callback(owner));
    const std::uint8_t head[]{0x55,0x8b,0xec,0x51,0x83,0x3d,0x1c,0x55,0x7c,0,0,0x75,0x1b,0x83,0x7d,0x08,0,0x75,0x15,0xe8};
    if(!callback||std::memcmp(callback,head,sizeof(head))||callback[0x18]!=0x85||callback[0x19]!=0xc0||
       callback[0x1a]!=0x74||callback[0x1c]!=0xe8||callback[0x21]!=0x8b||callback[0x22]!=0xc8||callback[0x23]!=0xe8)
        return false;
    std::int32_t relative=0;
    std::memcpy(&relative,callback+0x24,sizeof(relative));
    return addon_unique(callback+0x28+relative,0x100,{0xf7,0x46,0x08,0,0,0x04,0,0x75,0x40});
}

bool install_camp_inventory(std::uint8_t* image,bool input_dispatch) noexcept {
    if (!image || game) return false;
    game = image;
    preview_glow_admitted = preview_glow_contract();
    install_failed = true;
    supported_addon=static_cast<std::uint8_t*>(bridge_supported_addon_image());
    const std::array<std::uint8_t,32> addon_digest{0x67,0x83,0x13,0xd7,0xfd,0xeb,0xdf,0xc4,
        0x9f,0xa0,0x1d,0xc5,0xaf,0xb8,0x31,0x80,0x56,0x6e,0xa7,0x90,0xd3,0x74,0x98,0x8f,
        0x2a,0xd9,0xc8,0x4c,0x78,0x13,0x1d,0x57};
    if(!bridge_exact_loaded_module(supported_addon,0x1dd000,addon_digest))supported_addon=nullptr;
    const std::uint8_t input_screen[]{0x8b,0x41,0x34,0x8b,0x51,0x38,0x3b,0xc2,0x74,0x06,0x8b,0xca,0x8b,0x41,0xfc,0xc3,0x33,0xc0,0xc3};
    const std::uint8_t input_down[]{0x8b,0xc1,0x8b,0x48,0x0c,0xc6,0x40,0x7f,0,0x85,0xc9,0x75,0x13,0x8b,0x48,0x34,
        0x8b,0x50,0x38,0x3b,0xca,0x74,0x1d,0x8b,0x42,0xfc,0x8d,0x88,0x94,0,0,0};
    const std::uint8_t input_global[]{0xb9,0x50,0x7d,0x7c,0,0xe8,0x11,0xfb,0x19,0};
    drag_admitted=input_dispatch&&!std::memcmp(game+0x1f7590,input_screen,sizeof(input_screen))&&
        !std::memcmp(game+0x1f76a0,input_down,sizeof(input_down))&&
        !std::memcmp(game+0x57b85,input_global,sizeof(input_global));
    if(!drag_admitted)bridge_append_probe_event("camp_drag_unavailable",input_dispatch?
        "native_top_screen_or_capture_contract_differs":"active_message_call_owner_differs");
    // Validate the inline lifetime/cleanup/store and ordinary allocator/ref
    // contracts without modifying any existing allocator or release dispatcher.
    const std::uint8_t cleanup[]{0x8d,0x4d,0xc8,0xc7,0x45,0xfc,0xff,0xff,0xff,0xff,0xe8,0x33,0x3d,0x11,0};
    const std::uint8_t stem_state[]{0xc7,0x45,0xfc,0x05,0,0,0,0x89,0x45,0xd8};
    const std::uint8_t null_checks[]{0x85,0xff,0x0f,0x84,0xac,0x0a,0,0,0x85,0xdb,0x0f,0x84,0xa4,0x0a,0,0};
    const std::uint8_t store[]{0x89,0x9f,0x80,0x02,0,0};
    const std::uint8_t allocator[]{0x6a,0xff,0x68,0x36,0x93,0x72,0};
    const std::uint8_t icon_binding[]{0x6a,0x20,0x68,0,1,0,0,0x68,0,1,0,0,0x8b,0xcb,
        0x89,0x5c,0x24,0x34,0xe8,0x22,0x5d,0x0a,0};
    const std::uint8_t interface_constructor[]{0x83,0xec,0x6c,0x53,0x55,0x8b,0xe9,0x56,
        0x8b,0xb4,0x24,0x84,0,0,0,0x57,0x8b,0x45,0,0x6a,0xff,0x56,0xff,0x50,0x5c};
    const std::uint8_t persistent_texture[]{0x81,0x48,0x08,0,0,0x04,0};
    const std::uint8_t release[]{0x8b,0xc1,0x8b,0x48,0x04,0x49,0x89,0x48,0x04,0x75,0x09,0x8b,0x48,0x14,0x50,0x8b,0x11,0xff,0x52,0x60,0xc3};
    const std::uint8_t figure_miss[]{0xe8,0x05,0xf5,0xff,0xff,0x85,0xc0,0x89,0x45,0xdc,0x75,0x04,0x33,0xff,0xeb,0x6b};
    const std::uint8_t figure_trim[]{0x8b,0x45,0xf0,0xc6,0x45,0xfc,0,0x8d,0x88,0x70,0x01,0,0,0xe8,0x02,0x64,0,0,0x8b,0x7d,0xdc};
    const std::uint8_t figure_addref[]{0x8b,0x46,0x04,0x5f,0x40,0x89,0x46,0x04,0x8b,0xc6,0x5e,0xc3};
    // Post-validation action authority; no new hook or guessed validity field.
    const std::uint8_t action_validity[]{0x88,0x9e,0x10,0x02,0,0,0x88,0x9e,0x11,0x02,0,0};
    const std::uint8_t lower_left_tile[]{0xe8,0x50,0xd0,0xfd,0xff,0x68,0xe4,0,0,0,0x68,0xe4,0,0,0,0x6a,0x1c};
    const std::uint8_t lower_right_tile[]{0xe8,0x04,0xd0,0xfd,0xff,0x8b,0xf8,0x68,0xe4,0,0,0,0x68,0xe4,0,0,0};
    const std::uint8_t catalogue[]{0x8b,0x86,0xac,0x01,0,0,0x33,0xdb,0x8b,0x48,0x04,
        0x85,0xc9,0x0f,0x8e,0xd5,0,0,0,0x8b,0x00};
    const std::uint8_t icon_name[]{0x8b,0x0e,0x8d,0x45,0xc8,0x50,0x8b,0x11,0xff,0x52,0x4c,
        0x8b,0x4d,0xf0,0x8b,0x00};
    const std::uint8_t string_release[]{0x56,0x8b,0xf1,0x8b,0x06,0x8d,0x48,0xf4};
    const auto allocator_owner = resolve_entry_owner(game + 0x1ec020,allocator,sizeof(allocator),7,&addon_module_contains);
    addon_icon_lookup = addon_icon_allocator(allocator_owner) && addon_unload_keeps_persistent();
    if (std::memcmp(game + 0x22131b,catalogue,sizeof(catalogue)) ||
        std::memcmp(game + 0x1ecb33,icon_name,sizeof(icon_name)) ||
        std::memcmp(game + 0x300970,string_release,sizeof(string_release)) ||
        (allocator_owner.owner != EntryOwner::stock && !addon_icon_lookup) ||
        std::memcmp(game + 0x1ecc2e, cleanup, sizeof(cleanup)) ||
        std::memcmp(game + 0x1ecb43, stem_state, sizeof(stem_state)) ||
        std::memcmp(game + 0x1ecc3d, null_checks, sizeof(null_checks)) ||
        std::memcmp(game + 0x1ecc4d, store, sizeof(store)) ||
        std::memcmp(game + 0x1ec047, icon_binding, sizeof(icon_binding)) ||
        std::memcmp(game + 0x291d80, interface_constructor, sizeof(interface_constructor)) ||
        std::memcmp(game + 0x110dd8, persistent_texture, sizeof(persistent_texture)) ||
        std::memcmp(game + 0x55980, release, sizeof(release)) ||
        std::memcmp(game + 0x1ecaa6, figure_miss, sizeof(figure_miss)) ||
        std::memcmp(game + 0x1ecb0c, figure_trim, sizeof(figure_trim)) ||
        std::memcmp(game + 0x1ec004, figure_addref, sizeof(figure_addref)) ||
        std::memcmp(game + 0x21197f, action_validity, sizeof(action_validity)) ||
        std::memcmp(game + 0x22236b, lower_left_tile, sizeof(lower_left_tile)) ||
        std::memcmp(game + 0x2223b7, lower_right_tile, sizeof(lower_right_tile)) ||
        allocator_owner.owner == EntryOwner::rejected) {
        bridge_append_probe_event("camp_inventory_hook_rejected", "item_lifetime_or_native_action_contract_differs");
        game = nullptr; return false;
    }
    // Full expected entry bytes below are from the supported shipped image.
    // They also bound the resolver's validation of the addon's stock replay.
    hooks = {{
        {0x60c870, 5, {0x55,0x8b,0xec,0x6a,0xff,0x68,0x39,0xb7,0x72,0,0x64,0xa1,0,0,0,0,0x50,0x64,0x89,0x25,0,0,0,0}, &activate},
        {0x60e6b0, 7, {0x6a,0xff,0x68,0x48,0xb8,0x72,0,0x64,0xa1,0,0,0,0,0x50,0x64,0x89,0x25,0,0,0,0,0x83,0xec,0x1c}, &deactivate},
        {0x60f050, 7, {0x6a,0xff,0x68,0x68,0xb8,0x72,0,0x64,0xa1,0,0,0,0,0x50,0x64,0x89,0x25,0,0,0,0,0x83,0xec,0x1c}, &camp_draw},
        {0x5feb90, 5, {0x53,0x8b,0x5c,0x24,0x08,0x56,0x8b,0xf1,0x57,0x33,0xff,0x8b,0x86,0x0c,0x01,0,0,0x85,0xc0,0x7e,0x1a,0x8b,0x86,0x08}, &object_draw},
        {0x5fecc0, 6, {0x56,0x57,0x8b,0xf9,0x33,0xf6,0x8b,0x87,0x1c,0x01,0,0,0x85,0xc0,0x7e,0x1e,0x8b,0x87,0x18,0x01,0,0,0x8b,0x0c}, &draw2d},
        {0x5febf0, 6, {0x83,0xec,0x0c,0x55,0x8b,0xe9,0x8b,0x8d,0x98,0,0,0,0x8b,0x41,0x68,0x89,0x44,0x24,0x08,0x8b,0x51,0x6c,0x8d,0x44}, &tips},
        {0x5fed00, 7, {0x83,0xec,0x10,0x53,0x57,0x8b,0xf9,0x8b,0x8f,0x98,0,0,0,0x8b,0x41,0x68,0x89,0x44,0x24,0x08,0x8b,0x51,0x6c,0x8d}, &tips2d},
        {0x5fc8f0, 7, {0x83,0xec,0x0c,0x8b,0x54,0x24,0x14,0x53,0x55,0x56,0x8b,0xf1,0x57,0x8b,0x7c,0x24,0x20,0x8b,0x46,0x28,0x8b,0x4e,0x2c,0x2b}, &point_in_area},
        {0x5ffcc0, 7, {0x83,0xec,0x08,0x8b,0x44,0x24,0x0c,0x56,0x8b,0xf1,0x8d,0x54,0x24,0x04,0x8b,0x4c,0x24,0x14,0x57,0x89,0x4c,0x24,0x0c,0x8b}, &hit_test},
        {0x5ffd30, 7, {0x83,0xec,0x08,0x8b,0x44,0x24,0x0c,0x56,0x8b,0xf1,0x8d,0x54,0x24,0x04,0x8b,0x4c,0x24,0x14,0x52,0x89,0x4c,0x24,0x0c,0x8b}, &inside},
        {0x621b50, 7, {0x6a,0xff,0x68,0x58,0xca,0x72,0,0x64,0xa1,0,0,0,0,0x50,0x64,0x89,0x25,0,0,0,0,0x83,0xec,0x58}, &inventory_draw},
        {0x621f30, 5, {0x53,0x56,0x8b,0xf1,0x57,0x8b,0x86,0xe8,0,0,0,0x85,0xc0,0x0f,0x84,0xac,0,0,0,0x8b,0x7c,0x24,0x14,0x8b}, &inventory_mouse},
        {0x620a50, 6, {0x55,0x8b,0xec,0x83,0xec,0x10,0x53,0x56,0x8b,0xf1,0xe8,0x11,0xf8,0xfd,0xff,0x8b,0x96,0x54,0x01,0,0,0x33,0xdb,0xd9}, &idle},
        {0x6210f0, 5, {0x8b,0x44,0x24,0x10,0x56,0x8b,0xf1,0x50,0x8b,0x4c,0x24,0x14,0x51,0x8d,0x8e,0x6c,0xff,0xff,0xff,0xe8,0xb8,0xeb,0xfd,0xff}, &wheel},
        {0x621570, 5, {0x83,0xec,0x1c,0x56,0x57,0x8b,0x7c,0x24,0x28,0x8b,0xf1,0x85,0xff,0x89,0x74,0x24,0x18,0x0f,0x84,0x1a,0x03,0,0,0x53}, &set_to_item},
        {0x622420, 6, {0x81,0xec,0x98,0,0,0,0x53,0x55,0x56,0x8b,0xf1,0x57,0x8b,0x8e,0x4c,0x01,0,0,0x8b,0x01,0xff,0x90,0xcc,0}, &quick_draw},
        {0x622c10, 5, {0x83,0xec,0x1c,0x53,0x55,0x56,0x8b,0xf1,0x57,0x8b,0x8e,0x4c,0x01,0,0,0x8b,0x01,0xff,0x90,0xcc,0,0,0,0x8b}, &spell_draw},
        {0x622d40, 5, {0x53,0x8b,0x5c,0x24,0x10,0x56,0x57,0x8b,0xf9,0xc7,0x03,0,0,0,0,0x8b,0x87,0xe8,0,0,0,0x85,0xc0,0x74}, &spell_mouse},
        {0x622bc0, 10, {0x56,0x8b,0xf1,0x57,0x8b,0x8e,0x4c,0x01,0,0,0x8b,0x01,0xff,0x90,0xcc,0,0,0,0xc7,0x86,0x54,0x01,0,0}, &spell_current},
        {0x5ee310, 5, {0x56,0x8d,0x54,0x24,0x0c,0x8b,0xf1,0x52,0x8b,0x54,0x24,0x14,0x8b,0x86,0x6c,0xff,0xff,0xff,0x8d,0x8e,0x6c,0xff,0xff,0xff}, &slot_click},
        {0x5ec300, 9, {0x55,0x8b,0xec,0x81,0xec,0x10,0x01,0,0,0x53,0x56,0x8b,0x75,0x0c,0x8b,0xd9,0x85,0xf6,0x89,0x5d,0xd0,0x0f,0x84,0xc5}, &unit_draw},
        {0x627390, 7, {0x6a,0xff,0x68,0x98,0xcc,0x72,0,0x64,0xa1,0,0,0,0,0x50,0x64,0x89,0x25,0,0,0,0,0x83,0xec,0x68}, &trade_draw},
        {0x627c90, 7, {0x6a,0xff,0x68,0xd8,0xcc,0x72,0,0x64,0xa1,0,0,0,0,0x50,0x64,0x89,0x25,0,0,0,0,0x83,0xec,0x68}, &repair_draw},
        {0x6284a0, 5, {0x55,0x8b,0xec,0x6a,0xff,0x68,0x18,0xcd,0x72,0,0x64,0xa1,0,0,0,0,0x50,0x64,0x89,0x25,0,0,0,0}, &spell_construction},
        {0x628f80, 7, {0x83,0xec,0x58,0x8b,0x44,0x24,0x5c,0x53,0x55,0x56,0x57,0x33,0xff,0x8b,0xf1,0x50,0x89,0x7c,0x24,0x1c,0xc6,0x44,0x24,0x16}, &item_construction},
        {0x5eca10, 5, {0x55,0x8b,0xec,0x6a,0xff,0x68,0x90,0x93,0x72,0,0x64,0xa1,0,0,0,0,0x50,0x64,0x89,0x25,0,0,0,0}, &item_draw},
        {0x5ee6c0, 8, {0x53,0x8b,0xd9,0x57,0x8b,0x7c,0x24,0x0c,0x8b,0x83,0x94,0x01,0,0,0x85,0xc0,0x74,0x1a,0x8b,0x83,0x90,0x01,0,0}, &info_item},
        {0x5f2910, 7, {0x6a,0xff,0x68,0x28,0x98,0x72,0,0x64,0xa1,0,0,0,0,0x50,0x64,0x89,0x25,0,0,0,0,0x83,0xec,0x1c}, &info_help},
        {0x5f1590, 7, {0x6a,0xff,0x68,0x03,0x98,0x72,0,0x64,0xa1,0,0,0,0,0x50,0x64,0x89,0x25,0,0,0,0,0x81,0xec,0x84}, &info_skill},
        {0x5ee760, 7, {0x6a,0xff,0x68,0x2e,0x95,0x72,0,0x64,0xa1,0,0,0,0,0x50,0x64,0x89,0x25,0,0,0,0,0x83,0xec,0x7c}, &info_text},
        {0x5ee5c0, 6, {0x8b,0x81,0x94,0x01,0,0,0x83,0xec,0x18,0x85,0xc0,0x74,0x5d,0x8b,0x81,0x90,0x01,0,0,0x8b,0,0x85,0xc0,0x74}, &info_draw},
        {0x5ecb4d, 6, {0x8d,0x99,0x80,0x01,0,0}, &item_texture_lookup},
        {0x5ecb13, 6, {0x8d,0x88,0x70,0x01,0,0}, &item_figure_trim},
        {0x691630, 6, {0x55,0x57,0x8b,0xf9,0x33,0xed}, &texture_manager_destroy},
        // The addon help/skill member bodies behind those setters' adapters.
        {0,5,{0x55,0x8b,0xec,0x6a,0xff},&addon_info_help,true},
        {0,5,{0x55,0x8b,0xec,0x6a,0xff},&addon_info_skill,true},
        {0x58dcd0,7,{0x6a,0xff,0x68,0x88,0x5c,0x72,0,0x64,0xa1,0,0,0,0,0x50,0x64,0x89,0x25,0,0,0,0,0x83,0xec,0x40},&close_world_segment},
        {0x66e130,7,{0x6a,0xff,0x68,0,0x0f,0x73,0,0x64,0xa1,0,0,0,0,0x50,0x64,0x89,0x25,0,0,0,0,0x83,0xec,0x44},&close_visibility},
        {0x58b750,5,{0x55,0x8b,0xec,0x6a,0xff,0x68,0x66,0x5a,0x72,0,0x64,0xa1,0,0,0,0,0x50,0x64,0x89,0x25,0,0,0,0},&close_update_player}
    }};
    for (auto& hook : hooks) {
        if (!camp_diagnostic_enabled() && (&hook == &hooks[close_world_segment_id] ||
            &hook == &hooks[close_visibility_id] || &hook == &hooks[close_update_player_id])) continue;
        // Keep the owned tooltip wrappers' original caller and area scope.
        if(widescreen_interface_owned() && (&hook==&hooks[tips_id] || &hook==&hooks[tips2d_id]))continue;
        // Result validation can call local help independently of the CampDraw owner.
        if(hook.addon_entry){
            const bool help=&hook==&hooks[addon_info_help_id];
            hook.address=addon_adapter_body(hooks[help?info_help_id:info_skill_id].owner,2);
            if(!hook.address||(!help&&!addon_hover_selection))continue;
        }
        auto* entry = hook_entry(hook);
        const bool icon_site = &hook == &hooks[item_texture_id];
        const bool inline_site = icon_site || &hook == &hooks[figure_trim_id];
        const auto stock_bytes = inline_site || hook.addon_entry || &hook == &hooks[texture_manager_destroy_id] ? hook.bytes : hook.stock.size();
        const auto owner = resolve_entry_owner(entry, hook.stock.data(), stock_bytes, hook.bytes, &addon_module_contains);
        hook.owner=owner;
        if(&hook==&hooks[camp_draw_id]){
            addon_hover_selection=owner.owner==EntryOwner::framework_chain&&supported_addon&&owner.callback==supported_addon+0x77360;
            if((owner.owner!=EntryOwner::stock&&!addon_hover_selection)||
                (addon_hover_selection&&!addon_hover_contract())){
                bridge_append_probe_event("camp_inventory_hook_rejected","ordinary_hover_draw_owner_contract_differs");
                uninstall_camp_inventory();return false;
            }
        }
        // An inline site is not a function ABI. Preserve any foreign detour by
        // rejecting it; function entry owners can use the existing forwarder.
        if (owner.owner == EntryOwner::rejected || ((inline_site||hook.addon_entry) && owner.owner != EntryOwner::stock)) {
            char detail[128]; std::snprintf(detail, sizeof(detail), "entry=%08lx reason=%s", static_cast<unsigned long>(hook.va), owner.reason ? owner.reason : "inline_detour_unavailable");
            bridge_append_probe_event("camp_inventory_hook_rejected", detail);
            uninstall_camp_inventory(); return false;
        }
        const std::size_t copied = owner.owner == EntryOwner::stock ? hook.bytes : 0;
        // A chained owner resumes after its own patch. Change only its jump,
        // leaving the following instructions and its NOP fill intact.
        hook.patch_bytes = copied ? hook.bytes : 5;
        auto* code = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, copied + 5, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        if (!code) { uninstall_camp_inventory(); return false; }
        hook.forwarder = code;
        if (copied) std::memcpy(code, entry, copied);
        jump(code + copied, code + copied, copied ? entry + hook.bytes : owner.dispatcher);
        DWORD old = 0;
        if (!VirtualProtect(code, copied + 5, PAGE_EXECUTE_READ, &old)) { uninstall_camp_inventory(); return false; }
        FlushInstructionCache(GetCurrentProcess(), code, copied + 5);
        std::memcpy(hook.original.data(), entry, hook.patch_bytes);
    }
    item_texture_forwarder = hooks[item_texture_id].forwarder;
    item_texture_cleanup = game + 0x1ecc2e;
    figure_trim_forwarder = hooks[figure_trim_id].forwarder;
    for (auto& hook : hooks) {
        if(!hook.forwarder)continue;
        auto* entry = hook_entry(hook);
        std::uint8_t replacement[16]; std::memset(replacement, 0x90, sizeof(replacement));
        jump(replacement, entry, hook.replacement);
        hook.installed = true;
        if (!write_bytes(entry, replacement, hook.patch_bytes)) { uninstall_camp_inventory(); return false; }
    }
    bridge_append_probe_event("camp_inventory_installed",addon_hover_selection?
        "verified_entry_owners_default_wide_grid camp_draw=addon callback_rva=00077360 hover=completed_selection_and_local_helpers":
        "verified_entry_owners_default_wide_grid camp_draw=stock hover=stock_setters");
    install_failed = false;
    return true;
}
bool uninstall_camp_inventory() noexcept {
    camp_icon_loader.shutdown();
    camp_inventory_cancel_drag();
    if(!restore_perk_viewport())return false;
    camp_inventory_retire_textures();
    bool restored = true;
    for (auto& hook : hooks) {
        if (!hook.installed) continue;
        auto* entry = hook_entry(hook);
        std::uint8_t replacement[16]; std::memset(replacement, 0x90, sizeof(replacement)); jump(replacement, entry, hook.replacement);
        if (std::memcmp(entry, hook.original.data(), hook.patch_bytes) == 0) { hook.installed = false; continue; }
        if (std::memcmp(entry, replacement, hook.patch_bytes) != 0 || !write_bytes(entry, hook.original.data(), hook.patch_bytes)) {
            restored = false; continue;
        }
        hook.installed = false;
    }
    if (!restored) return false;
    if(!release_owned_layout())return false;
    for (auto& hook : hooks) {
        if (hook.forwarder) VirtualFree(hook.forwarder, 0, MEM_RELEASE);
        hook.forwarder = nullptr;
    }
    game = nullptr; screen = nullptr; valid = false;
    preview_glow_admitted = false;
    preview_glow_failure = nullptr;
    supported_addon=nullptr;
    addon_hover_selection=false;
    drag_admitted=false;
    item_texture_forwarder = item_texture_cleanup = nullptr;
    figure_trim_forwarder = nullptr;
    return true;
}



int camp_inventory_blit(const RECT& destination, const RECT& source, CampBlitPiece (&pieces)[kCampBlitPieces]) noexcept {
    return camp::frontend::SurfaceMapper{layout,mode(),drawing,screen&&layout.wide&&valid,
        card_content_visible(drawing),cards_drawn,output_width,output_height}.blit(destination,source,pieces);
}
bool camp_inventory_clear(const RECT& source, RECT& destination) noexcept {
    return camp::frontend::SurfaceMapper{layout,mode(),drawing,screen&&layout.wide&&valid,
        card_content_visible(drawing),cards_drawn,output_width,output_height}.clear(source,destination);
}

void camp_inventory_set_popup_sink(void* context, CampPopupSink sink, ID3D11Device* device) noexcept {
    popup_sink_context=context; popup_sink=sink; icon_device=device;
}
bool camp_inventory_preview_specular_rgb(std::uint32_t& rgb) noexcept {
    auto* scope = item_texture_scope;
    if (!scope || !scope->active || !scope->preview_item || scope->owner != screen ||
        role_card(drawing) < 0 || !valid || !layout.wide) return false;
    if (!scope->preview_glow_ready) {
        // Mark failures/no-glow ready too: component submissions must never
        // repeat the phase step or observe different RGB within this draw.
        scope->preview_glow_ready = true;
        if (const char* reason = advance_preview_specular_rgb(scope->preview_item,scope->preview_specular_rgb)) {
            // Omit unavailable RGB instead of inheriting the shared visual's glow.
            if (!preview_glow_failure) {
                preview_glow_failure = reason;
                bridge_report_event("camp_preview_glow_omitted",reason);
            }
        }
    }
    rgb = scope->preview_specular_rgb;
    return true;
}
void camp_inventory_popup_published(bool visible) noexcept { if(!visible)cards_drawn={}; }
void camp_inventory_popup_failed() noexcept {
    cards_drawn={};
    if(!popup_failed)bridge_append_probe_event("camp_popup_unavailable","owned_card_target_or_flush_failed");
    popup_failed=true;
}

} // namespace owned

#endif // EID3D11_EMBEDDED
