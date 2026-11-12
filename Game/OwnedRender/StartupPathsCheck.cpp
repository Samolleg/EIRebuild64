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
        L"D:\\Profiles\\Base\\Res\\textures.res", L"D:\\Profiles\\Base\\Res\\redress.res", L"D:\\Profiles\\Base\\Res\\menus.res"};
    require(selected.mounts.archives == expected);
    require(selected.mounts.loose_roots == (RuntimeDiagnostics::Enabled() ? std::vector<std::wstring>{
        L"D:\\Profiles\\Mods\\Selected\\Maps", L"D:\\Profiles\\Base\\Maps"} : std::vector<std::wstring>{}));
    require(selected.world_map_layouts == L"D:\\Profiles\\Mods\\Selected\\world-map-layouts.json");
    const auto stock = OwnedRender::CaptureStartupPaths(L"C:/App/Engine",
        L"E:\\Stock", L"E:\\Stock\\Res", (const PackagePlan*)nullptr);
    require(stock.mounts.archives.size() == 4 && stock.mounts.loose_roots.size() == (RuntimeDiagnostics::Enabled() ? 1u : 0u));
    require(stock.mounts.archives.front() == L"E:\\Stock\\Res\\figures.res");
    require(stock.world_map_layouts == L"E:\\Stock\\world-map-layouts.json");
    owned::EmbeddedHostConfiguration selectedHost, stockHost;
    selectedHost.configure(selected);
    stockHost.configure(stock);
    for (int invalid = 0; invalid < 4; ++invalid) {
        auto broken = selected;
        if (invalid == 0) broken.mounts.archives.clear();
        if (invalid == 1) broken.mounts.loose_roots = {L"relative"};
        if (invalid == 2) broken.renderer_config = L"relative";
        if (invalid == 3) broken.world_map_layouts = L"relative";
        bool rejected = false;
        try { owned::EmbeddedHostConfiguration host; host.configure(broken); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected);
    }
    for (const auto* invalid : {L"C:relative", L"\\root-relative", L""}) {
        bool rejected = false;
        try { RuntimePaths64::ResolveSetting(invalid); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected);
    }
    std::puts("PASS: startup plan; selected mod; stock; menu assets; ignore-base; duplicate priority; absolute settings paths; ambiguous paths rejected");
}

int wmain() { try { CheckStartupPlan(); return 0; } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; } }
