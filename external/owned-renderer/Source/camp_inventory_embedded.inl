// The embedded host owns screen/item lifetimes. No retail offsets or installers.
namespace owned {
namespace {
void* embedded_camp_owner = nullptr;
void* embedded_texture_manager = nullptr;
std::uint64_t embedded_camp_generation = 0;
ID3D11Device* icon_device = nullptr;
bool addon_icon_lookup = false;
void reject(const char* reason) noexcept { bridge_report_event("camp_icon_omitted", reason); }
#include "camp_icon_loader.inl"
}
void camp_inventory_embedded_owner(void* owner, std::uint64_t generation,
                                    bool high_resolution) noexcept {
    embedded_camp_owner = owner;
    embedded_camp_generation = owner ? generation : 0;
    if (addon_icon_lookup != high_resolution) {
        camp_icon_loader.shutdown();
        addon_icon_lookup = high_resolution;
    }
}
std::uint64_t bridge_camp_texture_generation() noexcept { return embedded_camp_generation; }
void* bridge_camp_texture_owner() noexcept { return embedded_camp_owner; }
bool bridge_camp_texture_manager(void* owner) noexcept {
    return owner && owner == embedded_texture_manager;
}
bool bridge_camp_texture_mounts(ResourceStoreConfig& mounts) noexcept {
    try { return embedded_camp_owner && owned_visual_resource_mounts(mounts); }
    catch (...) { return false; }
}
std::shared_ptr<const PreparedCampIcon> camp_inventory_request_icon(
    const char* name, ID3D11Device* device) noexcept {
    if (!embedded_camp_owner || !embedded_camp_generation || !name || !*name || !device)
        return {};
    try {
        if (icon_device != device) { camp_icon_loader.shutdown(); icon_device = device; }
        return camp_icon_loader.request(name, true);
    } catch (...) { reject("owned_icon_request_failed"); return {}; }
}
void camp_inventory_begin_icon_frame() noexcept {
    camp_icon_loader.begin_priority_frame({true,true,true});
}
void camp_inventory_prioritize_icons(unsigned lane, const std::vector<std::string>& visible,
                                      const std::vector<std::string>& selected) {
    if (embedded_camp_owner && lane < 3) camp_icon_loader.prioritize(lane, visible, selected);
}
void camp_inventory_embedded_texture_manager(void* owner) noexcept {
    embedded_texture_manager = owner;
}
void camp_inventory_texture_manager_destroying(void* owner) noexcept {
    if (retire_model_texture_manager(owner, bridge_camp_texture_manager,
            camp_inventory_retire_textures, camp_inventory_shutdown_icons)) {
        embedded_texture_manager = nullptr;
        embedded_camp_generation = 0;
    }
}
void camp_inventory_shutdown_icons() noexcept {
    camp_icon_loader.shutdown(); icon_device = nullptr;
    embedded_camp_owner = nullptr; embedded_camp_generation = 0;
}
bool install_camp_inventory(std::uint8_t* image, bool) noexcept {
    // Host registration is authoritative; a retail image is never accepted.
    return !image && embedded_camp_owner && embedded_camp_generation;
}
bool uninstall_camp_inventory() noexcept { camp_inventory_shutdown_icons(); return true; }
}
