#include "E1SkeletonSources.h"
#include "owned_asset_loader.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <limits>

namespace {
unsigned checks = 0;
void check(bool value, const char* label) {
    ++checks;
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", label); std::exit(1); }
}
void u32(std::vector<unsigned char>& b, unsigned value) {
    for (unsigned i=0; i<4; ++i) b.push_back(static_cast<unsigned char>(value>>(i*8)));
}
void name(std::vector<unsigned char>& b, const char* s) {
    u32(b, static_cast<unsigned>(std::strlen(s))); b.insert(b.end(), s, s+std::strlen(s));
}
}
int wmain(int argc, wchar_t** argv) {
    using namespace OwnedRender;
    check(argc == 2, "read-only actual archive supplied");
    owned::ResourceStore store;
    owned::ResourceStoreConfig config;
    config.archives.emplace_back(argv[1]);
    owned::ResourceFailure failure;
    check(store.open(config, failure), "open actual RES");
    owned::ResourceBlob mod, bon, links;
    owned::ModelCpuAsset reference;
    std::string visual;
    for (const auto& entry : store.archive_entry_names()) {
        if (entry.size()<4 || entry.substr(entry.size()-4)!=".mod") continue;
        visual = entry.substr(0,entry.size()-4);
        if (store.resolve(entry, mod, failure) && store.resolve(visual+".bon", bon, failure) &&
            owned::decode_model_skeleton_resources(mod, bon, visual, reference, failure)) break;
        visual.clear();
    }
    check(!visual.empty(), "real model skeleton found");
    std::printf("Selected %s (%zu nodes)\n", visual.c_str(), reference.nodes.size());
    owned::MemoryResourceArchive models, bones;
    check(models.open(mod,failure) && bones.open(bon,failure) && models.resolve(visual,links,failure), "exact selected members");
    std::vector<int> owners(reference.nodes.size());
    std::vector<E1SkeletonNode> nodes;
    const void* head = nullptr;
    for (std::size_t i=0;i<reference.links.size();++i) {
        const auto& link=reference.links[i];
        E1SkeletonNode node;
        node.node=&owners[i]; node.name=link.child; node.parentName=link.parent;
        if (link.parent.empty() && !head) head=node.node;
        if (link.parent_index!=UINT_MAX) node.parent=&owners[link.parent_index];
        owned::ResourceBlob bytes;
        if (bones.resolve(node.name,bytes,failure))
            check(node.bone.Publish(node.name.c_str(),bytes.bytes->data(),static_cast<unsigned>(bytes.bytes->size())), "selected actual BON captured");
        else check(node.bone.Publish(node.name.c_str(),nullptr,0), "actual missing BON recorded");
        nodes.push_back(node);
    }
    E1SkeletonSources sources;
    check(!sources.Complete(nodes,head), "no skeleton fabricated before LNK selection");
    sources.PublishLinks(visual.c_str(),links.bytes->data(),static_cast<unsigned>(links.bytes->size()));
    auto first=sources.Complete(nodes,head);
    check(bool(first), "selected records assemble through production decoder");
    check(first->nodes.size()==reference.nodes.size(), "node indices preserved");
    for(std::size_t i=0;i<nodes.size();++i)
        check(first->nodes[i].bone_positions==reference.nodes[i].bone_positions &&
            first->nodes[i].transform_available==reference.nodes[i].transform_available,
            "selected decoder matches container decoder");
    check(sources.Complete(nodes,head)==first, "unchanged completion reuses immutable result");
    auto changed=nodes;
    changed[0].parentName += "wrong";
    check(!sources.Complete(changed,head), "changed parent declaration retires old completion");
    check(bool(sources.Complete(nodes,head)), "valid completion recovers");
    changed=nodes; changed[0].parent=&sources;
    check(!sources.Complete(changed,head), "actual parent mismatch rejected");
    check(!sources.Complete(nodes,&sources), "wrong completed head rejected");
    changed=nodes; changed.pop_back();
    check(!sources.Complete(changed,head), "partial skeleton rejected");
    changed=nodes; changed[0].bone.Clear();
    check(!sources.Complete(changed,head), "unknown load is not a missing bone");
    changed=nodes;
    float malformed=1;
    changed[0].bone.Publish("bad",&malformed,sizeof(malformed));
    check(!sources.Complete(changed,head), "truncated selected BON cannot reuse old skeleton");
    sources.PublishLinks(visual.c_str(),links.bytes->data(),3);
    check(!sources.Complete(nodes,head), "truncated LNK rejected");
    sources.Clear();
    check(!sources.Complete(nodes,head) && !first->nodes.empty(), "retirement preserves already retained immutable result");

    // Non-topological LNK: the child remains unlinked, matching real insertion.
    std::vector<unsigned char> lnk; u32(lnk,3);
    name(lnk,"child");name(lnk,"root");name(lnk,"root");name(lnk,"");name(lnk,"leaf");name(lnk,"root");
    int child=0,root=0,leaf=0;
    float endpoints[24]{};
    nodes={{&child,nullptr,"child","root",{}},{&root,nullptr,"root","",{}},{&leaf,&root,"leaf","root",{}}};
    for(auto& node:nodes) check(node.bone.Publish(node.name.c_str(),endpoints,sizeof(endpoints)),"fixture BON copied");
    sources.PublishLinks("fixture",lnk.data(),static_cast<unsigned>(lnk.size()));
    auto unordered=sources.Complete(nodes,&root);
    check(unordered && unordered->links[0].parent_index==UINT_MAX, "later parent remains unlinked");
    nodes[0].parent=&root;
    check(!sources.Complete(nodes,&root), "relinked hierarchy cannot masquerade as loaded LNK");
    nodes[0].parent=nullptr;
    nodes[1].bone.Publish("root",nullptr,0);
    auto missing=sources.Complete(nodes,&root);
    check(missing && !missing->nodes[1].transform_available && !missing->nodes[2].transform_available &&
        missing->nodes[0].transform_available, "missing parent BON propagates only through effective links");
    float legacy[18]; for(unsigned i=0;i<18;++i)legacy[i]=static_cast<float>(i);
    check(nodes[1].bone.Publish("root",legacy,sizeof(legacy)), "legacy six endpoints selected");
    auto six=sources.Complete(nodes,&root);
    check(six && six->nodes[1].bone_positions.size()==24, "legacy BON expanded in renderer");
    const auto& expanded=six->nodes[1].bone_positions;
    for(float strength : {0.0f,0.4f,1.0f}) for(float grace : {0.0f,0.7f,1.0f}) for(float tall : {0.0f,0.8f,1.0f}) {
        for(unsigned axis=0;axis<3;++axis) {
            const float a=legacy[axis]+strength*(legacy[3+axis]-legacy[axis]);
            const float b=legacy[9+axis]+strength*(legacy[12+axis]-legacy[9+axis]);
            const float low=a+grace*(legacy[6+axis]-a), high=b+grace*(legacy[15+axis]-b);
            float decoded=0;
            for(unsigned i=0;i<8;++i) decoded+=expanded[i*3+axis]*((i&1)?strength:1-strength)*((i&2)?grace:1-grace)*((i&4)?tall:1-tall);
            check(std::abs(decoded-(low+tall*(high-low)))<0.0001f,"legacy host formula equals eight endpoints");
        }
    }
    const auto held=nodes[1].bone.Get();
    nodes[1].bone.Publish("root",legacy,sizeof(legacy));
    check(nodes[1].bone.Get()==held && sources.Complete(nodes,&root)==six,"same BON bytes keep source and skeleton identity");
    legacy[0]+=2;
    nodes[1].bone.Publish("root",legacy,sizeof(legacy));
    auto replacement=sources.Complete(nodes,&root);
    check(replacement && replacement!=six && six->nodes[1].bone_positions[0]!=replacement->nodes[1].bone_positions[0],"changed selected BON replaces immutable skeleton");
    check(replacement->bone_container.key()!=six->bone_container.key(),"changed BON invalidates renderer pose/bounds cache identity");
    endpoints[0]=std::numeric_limits<float>::quiet_NaN();
    nodes[1].bone.Publish("root",endpoints,sizeof(endpoints));
    check(!sources.Complete(nodes,&root),"nonfinite BON rejected");
    std::printf("E1SkeletonSourcesCheck: %u checks PASS\n",checks);
}
