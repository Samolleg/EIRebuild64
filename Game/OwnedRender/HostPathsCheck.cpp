// Standalone console contract test; does not initialize or launch the game.
#include "ResourceBootstrap.h"
#include "StartupPaths.h"
#include "debug_overlay_config.h"
#include <cstdio>
#include <fstream>
#include <iterator>

struct PackagePlan {
    std::string Directory() const { return "D:\\Profiles\\Mods\\Selected"; }
    bool ResourcePlan(const std::string& category, std::vector<std::string>& paths, bool& ignore) const {
        ignore = category == "figures.res";
        if (category == "figures.res") paths = {"D:\\Profiles\\Mods\\Selected\\a.res", "D:\\Profiles\\Mods\\Selected\\a.res"};
        if (category == "menus.res") paths = {"D:\\Profiles\\Mods\\Selected\\menu.res"};
        if (category == "textures.res") paths = {"D:\\Profiles\\Mods\\Selected\\tex.res"};
        return true;
    }
};

void CheckStartupPlan() {
    auto require = [](bool value) { if (!value) throw std::runtime_error("startup plan mismatch"); };
    const PackagePlan package;
    const auto selected = OwnedRender::CaptureStartupPaths(L"C:/App/Engine",
        L"D:\\Profiles\\Base", L"D:\\Profiles\\Base\\Res", &package);
    const std::vector<std::wstring> expected = {
        L"D:\\Profiles\\Mods\\Selected\\a.res", L"D:\\Profiles\\Mods\\Selected\\a.res",
        L"D:\\Profiles\\Mods\\Selected\\menu.res", L"D:\\Profiles\\Mods\\Selected\\tex.res",
        L"D:\\Profiles\\Base\\Res\\menus.res", L"D:\\Profiles\\Base\\Res\\textures.res"};
    require(selected.mounts.archives == expected);
    require(selected.mounts.loose_roots == std::vector<std::wstring>{
        L"D:\\Profiles\\Mods\\Selected\\Maps", L"D:\\Profiles\\Base\\Maps"});
    require(selected.world_map_layouts == L"D:\\Profiles\\Mods\\Selected\\world-map-layouts.json");
    const auto stock = OwnedRender::CaptureStartupPaths(L"C:/App/Engine",
        L"E:\\Stock", L"E:\\Stock\\Res", (const PackagePlan*)nullptr);
    require(stock.mounts.archives.size() == 3 && stock.mounts.loose_roots.size() == 1);
    require(stock.mounts.archives.front() == L"E:\\Stock\\Res\\figures.res");
    require(stock.world_map_layouts == L"E:\\Stock\\world-map-layouts.json");
    for (const auto* invalid : {L"C:relative", L"\\root-relative", L""}) {
        bool rejected = false;
        try { RuntimePaths64::ResolveSetting(invalid); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected);
    }
    std::puts("PASS: startup plan; selected mod; stock; menu assets; ignore-base; duplicate priority; absolute settings paths; ambiguous paths rejected");
}

int wmain(int argc, wchar_t** argv) {
    try {
        CheckStartupPlan();
        if (argc != 2) return 11; // Existing isolated output directory.
        const std::wstring directory = argv[1];
        const auto expected = directory + L"\\rddraw.ini";
        const auto legacy = directory + L"\\eid3d11.ini";
        if (GetFileAttributesW(expected.c_str()) != INVALID_FILE_ATTRIBUTES ||
            GetFileAttributesW(legacy.c_str()) != INVALID_FILE_ATTRIBUTES) return 12;
        const std::string sentinel = "legacy sentinel: must remain unchanged\n";
        { std::ofstream output(legacy, std::ios::binary); output << sentinel; if (!output) return 13; }
        bool missing = false;
        try { owned::renderer_config_path(); }
        catch (const std::logic_error&) { missing = true; }
        if (!missing) return 1;
        owned::ResourceStoreConfig mounts;
        mounts.loose_roots = {L"C:\\fixture\\mod\\Maps", L"C:\\fixture\\Maps"};
        mounts.archives = {L"C:\\fixture\\mod\\override.res", L"C:\\fixture\\base.res",
                           L"C:\\fixture\\mod\\override.res"};
        bool relative = false;
        try { OwnedRender::ConfigureResources(L"relative", L"C:\\fixture\\world-map-layouts.json", mounts); }
        catch (const std::invalid_argument&) { relative = true; }
        if (!relative) return 2;
        bool nul = false;
        try { OwnedRender::ConfigureResources(std::wstring(L"C:\\fixture\0other", 16),
                  L"C:\\fixture\\world-map-layouts.json", mounts); }
        catch (const std::invalid_argument&) { nul = true; }
        if (!nul) return 3;
        OwnedRender::ConfigureResources(directory,
            L"C:\\fixture\\mod\\world-map-layouts.json", mounts);
        if (owned::renderer_config_path() != expected) return 4;
        auto snapshot = owned::embedded_host_configuration().snapshot();
        if (snapshot.mounts.archives != mounts.archives || snapshot.mounts.loose_roots != mounts.loose_roots ||
            snapshot.world_map_layouts != L"C:\\fixture\\mod\\world-map-layouts.json") return 5;
        snapshot.mounts.archives.clear();
        if (owned::embedded_host_configuration().snapshot().mounts.archives != mounts.archives) return 6;
        bool repeated = false;
        try { OwnedRender::ConfigureResources(L"C:\\other\\Engine", L"C:\\other\\world-map-layouts.json", mounts); }
        catch (const std::logic_error&) { repeated = true; }
        if (!repeated || owned::renderer_config_path() != expected) return 7;
        owned::RendererConfig config;
        config.register_integer("terrain_quality", "Terrain", "World", 1, 0, 3, 1);
        if (!config.load(owned::renderer_config_path().c_str()) ||
            !config.set_integer("terrain_quality", 3) || !config.save(owned::renderer_config_path().c_str())) return 14;
        // Exercise replacement of an existing file as well as initial creation.
        if (!config.set_integer("terrain_quality", 2) || !config.save(owned::renderer_config_path().c_str())) return 15;
        owned::RendererConfig loaded;
        loaded.register_integer("terrain_quality", "Terrain", "World", 1, 0, 3, 1);
        if (!loaded.load(expected.c_str()) || loaded.find("terrain_quality")->integer_value != 2) return 16;
        std::ifstream input(legacy, std::ios::binary);
        if (std::string(std::istreambuf_iterator<char>(input), {}) != sentinel) return 17;
        std::puts("PASS: path rejection; rddraw selection; ordered mounts; immutable snapshot; reconfiguration rejection; INI create/replace/readback; legacy INI unchanged");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 10;
    }
}
