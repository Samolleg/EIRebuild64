#include "../external/owned-renderer/Source/gltf_asset.h"
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
owned::ResourceBlob read(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("cannot open input file");
    const auto size = input.tellg();
    if (size <= 0 || size > 512 * 1024 * 1024) throw std::runtime_error("input size limit (512 MiB)");
    auto bytes = std::make_shared<std::vector<std::uint8_t>>(static_cast<std::size_t>(size));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char *>(bytes->data()), bytes->size()))
        throw std::runtime_error("cannot read input file");
    owned::ResourceBlob blob;
    blob.bytes = bytes;
    blob.identity.canonical_name = "model-check";
    return blob;
}
}
int wmain(int argc, wchar_t **argv) {
    if (argc != 4 || std::wstring(argv[2]) != L"--profile") {
        std::cerr << "Usage: EIRebuild64ModelCheck.exe <glb> --profile <human.v1.json>\n";
        return 2;
    }
    try {
        const auto asset = owned::gltf::decode(read(argv[1]), read(argv[3]),
            [](const std::string &, const void *data, std::uint32_t size) {
                owned::ResourceBlob blob;
                const auto *begin = static_cast<const std::uint8_t *>(data);
                blob.bytes = std::make_shared<std::vector<std::uint8_t>>(begin, begin + size);
                return blob;
            });
        std::cout << "ACCEPTED: " << asset->geometry.vertices.size() << " vertices, "
                  << asset->geometry.indices.size() / 3 << " triangles, "
                  << asset->geometry.joints.size() << " evaluation nodes\n";
        if(asset->rigid) {
            std::cout << "Rigid item: " << asset->socket << "; " << asset->anchors.size()
                      << " anchors; radius " << asset->radius << "; no animation allocation\n";
            return 0;
        }
        std::cout << "Animation: " << asset->animation->clips.size() << " clips, "
                  << asset->animation->morphs.size() << " morph vertices, "
                  << asset->gpu_animation->bytes.size() << " GPU bytes; idle radius "
                  << asset->radius << ", animation envelope " << asset->animation_radius << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "REJECTED: " << error.what() << '\n';
        return 1;
    }
}
