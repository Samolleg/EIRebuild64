#include "neighbour_maps.h"
#include <iostream>
#include <stdexcept>
#include <unordered_map>

using namespace owned;
// The standalone check has no game journal.
namespace owned { void bridge_append_probe_event(const char*, const char*) noexcept {} }
static void require(bool valid, const char* reason) {
    if (!valid) throw std::runtime_error(reason);
}

int main() try {
    // glTF decoded images intentionally share the local view ID 1; their
    // ResourceIdentity, like every archive member's, owns the actual identity.
    ResourceIdentity body{}, hair{}, normal{};
    body.container_incarnation = 100; body.bytes = 64;
    body.canonical_name = "models/characters/test.glb#pixels";
    hair = body; hair.container_incarnation = 101;
    normal = body; normal.container_incarnation = 102;
    NativeOwnedTextureView view{};
    view.identity = view.generation = 1; view.value_source = 1;
    view.resource = &body;
    const auto bodyKey = native_owned_mip_texture_key(view, false);
    view.resource = &hair;
    const auto hairKey = native_owned_mip_texture_key(view, false);
    view.resource = &normal;
    const auto normalKey = native_owned_mip_texture_key(view, false);
    require(bodyKey != hairKey && bodyKey != normalKey && hairKey != normalKey,
            "different glTF images alias the same GPU cache entry");
    std::unordered_map<std::string, unsigned> cache;
    cache.emplace(hairKey, 0x00ff00); cache.emplace(bodyKey, 0xffccaa);
    cache.emplace(normalKey, 0x8080ff);
    require(cache.at(bodyKey) == 0xffccaa && cache.at(normalKey) == 0x8080ff,
            "base/normal image changed with upload order");
    require(bodyKey == native_owned_archive_texture_key(body, 1, false),
            "D3D11 and D3D12 image identity disagree");
    std::cout << "PASS glTF base/hair/normal images remain separate with the same local ID\n";

    ResourceIdentity member = body;
    member.container_file_key = 42; member.absolute_offset = 256;
    member.canonical_name = "terrain.mmp";
    view.resource = &member;
    const auto memberKey = native_owned_mip_texture_key(view, false);
    require(memberKey == native_owned_archive_texture_key(member, 1, false),
            "CPU-mip upload and prebuilt archive upload disagree");
    member.container_incarnation += 200; view.identity = 900;
    require(memberKey == native_owned_mip_texture_key(view, false),
            "same file member no longer shared across stores");
    member.absolute_offset += 64;
    require(memberKey != native_owned_mip_texture_key(view, false), "different archive members alias");
    member.absolute_offset -= 64; ++member.container_file_key;
    require(memberKey != native_owned_mip_texture_key(view, false), "file versions alias");
    std::cout << "PASS archive member/file version separation and cross-store reuse\n";

    view.resource = &body; view.identity = 1;
    require(bodyKey != native_owned_mip_texture_key(view, true), "generated/authored mip chains alias");
    view.generation = 2;
    require(bodyKey != native_owned_mip_texture_key(view, false), "generations alias");
    view.resource = nullptr; view.value_source = 2; view.generation = 1;
    require(native_owned_mip_texture_key(view, false) == "2:1|generation=1|mips=authored",
            "procedural texture identity changed");
    view.value_source = 3;
    require(native_owned_mip_texture_key(view, true) == "3:1|generation=1|mips=full",
            "static memory texture identity changed");
    std::cout << "PASS mip policy, generations and non-resource texture domains\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
}
