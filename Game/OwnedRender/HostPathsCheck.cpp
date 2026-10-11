// Standalone console contract test; does not initialize or launch the game.
#include "ResourceBootstrap.h"
#include "debug_overlay_config.h"
#include <cstdio>
#include <fstream>
#include <iterator>

int wmain(int argc, wchar_t** argv) {
    try {
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
