#include "E1FigureSources.h"
#include "owned_asset_loader.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace {
unsigned checks = 0;
void check(bool value, const char* label) {
    ++checks;
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", label); std::exit(1); }
}
}
int wmain(int argc, wchar_t** argv) {
    check(argc == 2, "pass a read-only model archive");
    owned::ResourceStore store;
    owned::ResourceStoreConfig config;
    config.archives.emplace_back(argv[1]);
    owned::ResourceFailure failure;
    check(store.open(config, failure), "open actual RES");
    owned::ResourceBlob selected;
    owned::FigureCpuAsset reference;
    std::string partName, modelName;
    for (const auto& name : store.archive_entry_names()) {
        if (name.size() < 4 || name.substr(name.size()-4) != ".mod") continue;
        owned::ResourceBlob outer;
        owned::MemoryResourceArchive archive;
        if (!store.resolve(name, outer, failure) || !archive.open(outer, failure)) continue;
        for (const auto& part : archive.entry_names()) {
            owned::ResourceBlob candidate;
            if (archive.resolve(part, candidate, failure) &&
                owned::parse_figure_asset(candidate, reference, failure) &&
                reference.shape_count == 8 && !reference.indices.empty()) {
                selected = std::move(candidate); partName = part; modelName = name; break;
            }
        }
        if (selected.bytes) break;
    }
    check(bool(selected.bytes), "real eight-shape FIG found");
    std::printf("Selected %s/%s (%zu bytes)\n", modelName.c_str(), partName.c_str(), selected.bytes->size());
    const auto size = static_cast<std::uint32_t>(selected.bytes->size());
    auto bytes = *selected.bytes;
    OwnedRender::E1FigureSources sources, other;
    check(!sources.Find(partName), "no figure invented before completed load");
    check(sources.Publish(partName, bytes.data(), size), "selected FIG accepted by production decoder");
    const auto first = sources.Find(partName);
    check(first && first->shape_count == 8 && first->indices == reference.indices &&
        first->position_groups == reference.position_groups && first->normal_groups == reference.normal_groups &&
        first->texture_number == reference.texture_number && first->material_group == reference.material_group,
        "all shapes, indices and authored bindings retained");
    check(first->positions.size() == reference.positions.size() &&
        std::memcmp(first->positions.data(), reference.positions.data(),
            first->positions.size()*sizeof(owned::NativePackedGroup)) == 0,
        "renderer decodes the selected source without host morphing");
    check(sources.Publish(partName, selected.bytes->data(), size) && sources.Find(partName) == first,
        "equal bytes from another address reuse immutable identity");
    check(sources.ConfirmPositionSource(partName, selected.bytes->data(), size) && sources.Find(partName) == first,
        "complection-only update preserves eight endpoint source");
    check(other.Publish(partName, selected.bytes->data(), size) &&
        other.Find(partName)->identity.key() != first->identity.key(), "independent containers cannot alias identity");
    bytes[0] ^= 0xff;
    check(first->indices == reference.indices, "borrowed source mutation cannot alter published asset");
    check(!sources.ConfirmPositionSource(partName, bytes.data(), size) && !sources.Find(partName),
        "changed full source cannot stand for partial native position update");
    check(other.Find(partName) != nullptr, "failed update leaves other container intact");
    check(sources.Publish(partName, selected.bytes->data(), size), "full replacement recovers invalidated source");
    check(sources.Find(partName)->identity.key() != first->identity.key(), "replacement has fresh identity");
    check(!sources.Publish(partName, bytes.data(), size) && !sources.Find(partName),
        "decoder rejection retires previous source");
    check(sources.Publish(partName, selected.bytes->data(), size), "recover after rejected FIG");
    check(!sources.Publish(partName, selected.bytes->data(), 3) && !sources.Find(partName),
        "truncated member cannot retain old geometry");
    check(!sources.Publish(partName, nullptr, size) && !sources.Find(partName), "missing selection rejected");
    check(sources.Publish(partName, selected.bytes->data(), size), "reload before removal");
    sources.Remove(partName);
    check(!sources.Find(partName) && first->indices == reference.indices,
        "range removal retires lookup while prior immutable assembly stays valid");
    sources.Remove(partName);
    check(!sources.ConfirmPositionSource(partName, selected.bytes->data(), size),
        "position edit cannot fabricate a previously unloaded range");
    std::shared_ptr<const owned::FigureCpuAsset> retained;
    {
        OwnedRender::E1FigureSources transient;
        check(transient.Publish(partName, selected.bytes->data(), size), "temporary geometry container load");
        retained = transient.Find(partName);
    }
    check(retained && retained->indices == reference.indices, "assembly may outlive container destruction");
    std::printf("E1FigureSourcesCheck: %u checks PASS\n", checks);
}
