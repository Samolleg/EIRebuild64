#include "StdAfx.h"
#include "Game/CMappedResource.h"
#include "Game/CMMIOResource.h"
#include "Game/ModRuntime.h"
#include "Game/ModPackage.h"
#include "Game/SaveModIdentity.h"
#include "Navigation/QueryFailure.h"
#include "Pak/Pak.h"
#include "external/owned-renderer/Source/owned_resource_store.h"
#include "Game/OwnedRender/StartupPaths.h"
#include "Game/InstalledGameRoot.h"
#include "Game/ItemApproachArchiveIndex.h"
#include "external/owned-renderer/Source/critter_base_mounts.h"
#include "external/owned-renderer/Source/neighbour_profile.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>

// The archive classes, file mappings, registry reader and cache key are real.
// The isolated check has no mod host, journal UI, world, renderer or audio.
namespace ModRuntime {
CModPackage* Resources() { return nullptr; }
std::string MapPath(const char* path) { return path; }
std::string ConfigPath(const char* path) { return path; }
}
bool CModPackage::ResourcePlan(const std::string&, std::vector<std::string>&, bool&) const { return false; }
bool ReportError(const char*, ...) { return false; }
void ReportWarning(const char*, ...) {}
namespace GameJournal { void Event(const char*, const char*, const char*, ...) noexcept {} }
namespace owned { void bridge_append_probe_event(const char*, const char*) noexcept {} }

namespace {
namespace fs = std::filesystem;
unsigned checks = 0;
void Check(bool ok, const char* why) { ++checks; if (!ok) throw std::runtime_error(why); }
using Bytes = std::vector<BYTE>;

struct Guarded {
    BYTE* allocation = nullptr;
    BYTE* data = nullptr;
    explicit Guarded(const Bytes& bytes) {
        SYSTEM_INFO info; GetSystemInfo(&info);
        const auto page = info.dwPageSize;
        const auto used = ((bytes.size() + page - 1) / page + 1) * page;
        allocation = static_cast<BYTE*>(VirtualAlloc(nullptr, used + page, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        if (!allocation) throw std::bad_alloc();
        DWORD old = 0;
        if (!VirtualProtect(allocation + used, page, PAGE_NOACCESS, &old)) throw std::runtime_error("guard page");
        data = allocation + used - bytes.size();
        if (!bytes.empty()) memcpy(data, bytes.data(), bytes.size());
    }
    ~Guarded() { if (allocation) VirtualFree(allocation, 0, MEM_RELEASE); }
};
void Store(const fs::path& file, const Bytes& bytes) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (!out) throw std::runtime_error("fixture write");
}
SResHeader Header(const Bytes& bytes) { SResHeader h; memcpy(&h, bytes.data(), sizeof(h)); return h; }
void Header(Bytes& bytes, const SResHeader& h) { memcpy(bytes.data(), &h, sizeof(h)); }
SResHashEntry Entry(const Bytes& bytes, unsigned index = 0) {
    SResHashEntry e; memcpy(&e, bytes.data() + Header(bytes).dwHashTableOffset + index * sizeof(e), sizeof(e)); return e;
}
void Entry(Bytes& bytes, const SResHashEntry& e, unsigned index = 0) {
    memcpy(bytes.data() + Header(bytes).dwHashTableOffset + index * sizeof(e), &e, sizeof(e));
}
Bytes Archive(const std::string& name) {
    static_assert(sizeof(SResHeader) == 16 && sizeof(SResHashEntry) == 22, "RES wire sizes");
    SResHeader h{SRESHEADER_SIGNATURE, 1, 17, DWORD(name.size())};
    Bytes bytes(17 + sizeof(SResHashEntry) + name.size());
    Header(bytes,h); bytes[16] = 73;
    SResHashEntry e{}; e.dwNextIndex = DWORD(-1); e.dwOffset = 16; e.dwSize = 1; e.wNameLen = WORD(name.size());
    Entry(bytes,e); memcpy(bytes.data() + 17 + sizeof(e), name.data(), name.size());
    return bytes;
}
void Valid(CMappedResource& archive, const std::string& name) {
    DWORD size = 0; auto* data = archive.GetFile(name.c_str(), &size);
    Check(data && size == 1 && *data == 73, "valid payload changed");
    archive.CloseFile(data);
    CStringArray names;
    archive.GetFileList(names); Check(names.GetSize() == 1 && names[0] == name.c_str(), "enumeration changed");
    archive.GetFileList(names, "*.bin"); Check(names.GetSize() == 1 && names[0] == name.c_str(), "long masked enumeration");
    Check(!archive.GetFile("absent.bin"), "missing name matched");
}
void ResourceChecks(const fs::path& directory) {
    const auto file = directory / "fixture.res";
    for (const auto length : {5u,1023u,1024u,1025u,65535u}) {
        const auto name = std::string(length - 4, 'a') + ".bin";
        auto bytes = Archive(name); Guarded memory(bytes);
        CMappedResource buffered(true), inMemory(true);
        CWin32MappedResource mapped(true), nested(true);
        Check(inMemory.OpenResourceMem(memory.data, bytes.size()), "buffered memory open"); Valid(inMemory,name);
        Check(nested.OpenResourceMem(memory.data, bytes.size()), "nested open"); Valid(nested,name);
        Store(file,bytes);
        Check(buffered.OpenResource(file.string().c_str()), "buffered file open"); Valid(buffered,name);
        Check(mapped.OpenResource(file.string().c_str()), "mapped file open"); Valid(mapped,name);
        CMMIOResource mmio;
        Check(mmio.OpenResource(file.string().c_str(), nullptr, 0), "MMIO open");
        CStringArray names; mmio.GetFileList(names,"*.bin");
        Check(names.GetSize() == 1 && names[0] == name.c_str(), "MMIO enumeration");
    }
    auto badHeader = [&](Bytes bytes) {
        Guarded guard(bytes); CMappedResource a(true); CWin32MappedResource b(true);
        Check(!a.OpenResourceMem(guard.data,bytes.size()), "bad header accepted in memory");
        Check(!b.OpenResourceMem(guard.data,bytes.size()), "bad header accepted nested");
        Store(file,bytes);
        Check(!a.OpenResource(file.string().c_str()), "bad header accepted buffered");
        Check(!b.OpenResource(file.string().c_str()), "bad header accepted mapped");
        Check(!a.GetFile("a.bin") && !b.GetFile("a.bin"), "stale failed-open state");
    };
    badHeader(Bytes(15,0));
    auto bytes = Archive("a.bin"); auto h = Header(bytes); h.dwSignature = 0; Header(bytes,h); badHeader(bytes);
    bytes = Archive("a.bin"); h = Header(bytes); h.dwEntryCount = 0xffffffffu; Header(bytes,h); badHeader(bytes);
    bytes = Archive("a.bin"); h = Header(bytes); h.dwHashTableOffset = 0xffffffffu; Header(bytes,h); badHeader(bytes);
    auto badEntry = [&](Bytes malformed) {
        Guarded guard(malformed); CMappedResource a(true); CWin32MappedResource b(true);
        Check(a.OpenResourceMem(guard.data,malformed.size()) && b.OpenResourceMem(guard.data,malformed.size()), "dead entry unmounted archive");
        for (auto* reader : {&a, static_cast<CMappedResource*>(&b)}) {
            Check(!reader->GetFile("a.bin"), "bad entry returned");
            CStringArray names; reader->GetFileList(names,"*.bin"); Check(names.IsEmpty(), "bad entry enumerated");
        }
        Store(file,malformed);
        CMappedResource buffered(true); CWin32MappedResource mapped(true);
        Check(buffered.OpenResource(file.string().c_str()) && mapped.OpenResource(file.string().c_str()), "dead file record unmounted archive");
        Check(!buffered.GetFile("a.bin") && !mapped.GetFile("a.bin"), "bad file entry returned");
    };
    bytes=Archive("a.bin"); auto e=Entry(bytes);e.dwNameOffset=0xffffffffu;Entry(bytes,e);badEntry(bytes);
    bytes=Archive("a.bin");e=Entry(bytes);e.wNameLen=1025;Entry(bytes,e);badEntry(bytes);
    bytes=Archive("a.bin");e=Entry(bytes);e.dwOffset=0xffffffffu;Entry(bytes,e);badEntry(bytes);
    bytes=Archive("a.bin");e=Entry(bytes);e.dwSize=0xffffffffu;Entry(bytes,e);badEntry(bytes);
    bytes=Archive("a.bin");bytes.back()=0;badEntry(bytes);
    // A bad collision successor must not hide this valid record or loop forever.
    for (DWORD next : {DWORD(0),DWORD(99)}) {
        bytes=Archive("a.bin");e=Entry(bytes);e.dwNextIndex=next;Entry(bytes,e);
        Guarded guard(bytes);CWin32MappedResource a(true);
        Check(a.OpenResourceMem(guard.data,bytes.size()), "bad chain unmounted archive");Valid(a,"a.bin");
    }
    // Legacy nested archives do not rely on the stored name-pool byte count.
    bytes=Archive("a.bin");h=Header(bytes);h.dwNameDataSize=0;Header(bytes,h);
    Guarded guard(bytes);CWin32MappedResource nested(true);
    Check(nested.OpenResourceMem(guard.data,bytes.size()), "nested pool size regression");Valid(nested,"a.bin");
    const auto good=Archive("a.bin");CMappedResource reopened(true);
    Check(reopened.OpenResourceMem(const_cast<BYTE*>(good.data()),good.size()), "reopen setup");
    Check(!reopened.OpenResourceMem(nullptr,0) && !reopened.GetFile("a.bin"), "failed reopen exposed prior bytes");
    h={SRESHEADER_SIGNATURE,0,sizeof(SResHeader),0};Bytes empty(sizeof(h));Header(empty,h);
    CWin32MappedResource zero(true);Check(zero.OpenResourceMem(empty.data(),empty.size()), "empty archive rejected");
    CStringArray names;zero.GetFileList(names);Check(names.IsEmpty()&&!zero.GetFile("x"), "empty archive query");
}
void IdentityChecks(const fs::path& directory, const fs::path& configPath) {
    CFileRegistry original;
    Check(original.Open(configPath.string().c_str()), "identity source config");
    const auto identity=SaveModIdentity::Read(original);
    Check(!identity.empty(), "identity missing from real mod metadata");
    const auto relocated=directory/"relocated"/"renamed_mod";
    fs::create_directories(relocated);
    fs::copy_file(configPath,relocated/"config.reg",fs::copy_options::overwrite_existing);
    CFileRegistry moved;Check(moved.Open((relocated/"config.reg").string().c_str()), "relocated config");
    Check(SaveModIdentity::BelongsTo(identity,SaveModIdentity::Read(moved)), "mod relocation changed identity");
    Check(!SaveModIdentity::BelongsTo(identity,SaveModIdentity::Make("Other mod","Other author")), "other mod admitted");
    const auto a=SaveModIdentity::Make("A:B","C"), b=SaveModIdentity::Make("A","B:C");
    Check(a!=b, "identity fields ambiguous");
    Check(!SaveModIdentity::BelongsTo(a,SaveModIdentity::Make("A:B","D")), "different author admitted");
    Check(!SaveModIdentity::BelongsTo("",SaveModIdentity::Make("","C")), "missing identity admitted");
    Check(SaveModIdentity::CurrentEnvelope(SaveModIdentity::SlotRoot), "new envelope refused");
    Check(!SaveModIdentity::CurrentEnvelope("EIRebuild64.slot.3"), "path envelope admitted");
}
void PakChecks(const fs::path& directory) {
    const auto input = directory / "pak-input";
    fs::create_directories(input);
    const Bytes expected(8192, BYTE('x'));
    Store(input / "example.bin", expected);
    const auto archivePath = directory / "diagnostics-test.pak";
    ei::pak::Pack(input, archivePath);
    CMappedResource archive(true);
    Check(archive.OpenResource(archivePath.string().c_str()), "PAK mount without diagnostics prerequisite");
    DWORD size = 0;
    const auto* bytes = archive.GetFile("example.bin", &size);
    Check(bytes && size == expected.size() && memcmp(bytes, expected.data(), size) == 0,
        "PAK decoding returns identical bytes under launch policy");
    CStringArray names; archive.GetFileList(names);
    Check(names.GetSize() == 1, "PAK enumeration independent of diagnostics");
}
void BasePakChecks(const fs::path& directory) {
    const auto root=directory/"base-pak";fs::create_directories(root/"base-source");
    const auto logical=root/"base.res", packed=root/"base.pak";
    Store(logical,Archive("shared.bin"));
    const auto read=[&](CMappedResource& reader,int expected){
        Check(reader.OpenResource(logical.string().c_str()),"logical base archive open");
        DWORD size=0;auto* p=reader.GetFile("SHARED.BIN",&size);
        Check(p&&size==1&&*p==expected,"logical base payload/CP1251 lookup");
        reader.CloseFile(p);reader.CloseResource();
    };
    CMappedResource raw(true), normal;CWin32MappedResource mapped(true);
    read(raw,73);read(mapped,73);
    Store(root/"base-source/shared.bin",Bytes{84});
    Store(root/"base-source/base-only.bin",Bytes{85});
    ei::pak::Pack(root/"base-source",packed);
    read(raw,84);read(normal,84);read(mapped,84);
    fs::remove(logical);read(raw,84);read(mapped,84);
    fs::create_directories(root/"mod-source");
    Store(root/"mod-source/shared.bin",Bytes{99});
    ei::pak::Pack(root/"mod-source",root/"mod.pak");
    struct Package {
        std::string mod;bool ignore;
        bool ResourcePlan(const std::string& category,std::vector<std::string>& paths,bool& skip) const {
            paths.clear();skip=ignore;
            if(category=="figures.res")paths.push_back(mod);
            return true;
        }
    } package{(root/"mod.pak").string(),false};
    fs::copy_file(packed,root/"figures.pak");
    owned::ResourceStoreConfig captured;
    OwnedRender::CaptureGraphicsMounts(captured,root,&package,[](const std::string& p){return fs::path(p).wstring();});
    Check(captured.archives.size()==5&&captured.archives[0]==(root/"mod.pak").wstring()&&captured.archives[1]==(root/"figures.pak").wstring(),"startup keeps mod ahead of resolved base");
    package.ignore=true;captured={};
    OwnedRender::CaptureGraphicsMounts(captured,root,&package,[](const std::string& p){return fs::path(p).wstring();});
    Check(captured.archives.size()==1&&captured.archives[0]==(root/"mod.pak").wstring(),"ignore-original suppresses all base mounts");
    owned::ResourceStoreConfig config;config.archives={(root/"mod.pak").wstring(),logical.wstring()};
    owned::ResourceStore store;owned::ResourceFailure failure;owned::ResourceBlob blob;
    Check(store.open(config,failure),"renderer opens logical PAK-only base");
    Check(store.resolve("shared.bin",blob,failure)&&(*blob.bytes)[0]==99,"mod wins over base PAK");
    Check(store.resolve("base-only.bin",blob,failure)&&(*blob.bytes)[0]==85,"missing mod entry falls through to base PAK");
    Check(!store.resolve("absent.bin",blob,failure),"missing stays missing");store.close();
    config.archives={logical.wstring(),(root/"mod.pak").wstring()};
    Check(store.open(config,failure)&&store.resolve("shared.bin",blob,failure)&&(*blob.bytes)[0]==84,"archive positions are never globally sorted by format");store.close();
    fs::create_directories(root/"loose");Store(root/"loose/shared.bin",Bytes{100});
    config.loose_roots={(root/"loose").wstring()};
    Check(store.open(config,failure)&&store.resolve("shared.bin",blob,failure)&&(*blob.bytes)[0]==100,"loose precedence retained");store.close();
    Store(root/"mod-source/_deleted.txt",Bytes{'b','a','s','e','-','o','n','l','y','.','b','i','n','\n'});
    ei::pak::Pack(root/"mod-source",root/"mod.pak");config.loose_roots.clear();
    config.archives={(root/"mod.pak").wstring(),logical.wstring()};
    Check(store.open(config,failure)&&!store.resolve("base-only.bin",blob,failure),"mod tombstone blocks base");store.close();
    Store(logical,Archive("shared.bin"));Store(packed,Bytes{0,1,2});
    Check(!raw.OpenResource(logical.string().c_str()),"corrupt selected PAK does not reopen old RES");
    Check(!mapped.OpenResource(logical.string().c_str()),"mapped reader does not reopen old RES");
    config.archives={logical.wstring()};
    Check(!store.open(config,failure)||!store.resolve("shared.bin",blob,failure),"renderer never falls back to shadowed RES");store.close();
    fs::create_directories(root/"source.res");Store(root/"source.pak",Bytes{0});
    Check(ei::pak::ResolveArchivePath(root/"source.res")==root/"source.res","source folder stays a folder");
    std::cout<<"BASE_PAK priority/payload/mapped/renderer/tombstone PASS\n";
}
void ItemCatalogAndCritterChecks(const fs::path& directory) {
    const auto root=directory/"item-catalog";fs::create_directories(root/"source");
    const auto logical=root/"models.res", packed=root/"models.pak";
    Store(logical,Archive("item.mod"));
    { ItemApproachArchiveIndex index;Check(index.Has(logical.string(),"ITEM.MOD"),"catalog legacy RES index");
      Check(!index.Has(logical.string(),"item.bon"),"catalog missing member rejected"); }
    Store(root/"source/item.mod",Bytes{1});Store(root/"source/item.bon",Bytes{2});
    Store(root/"source/items.idb",Bytes{3});ei::pak::Pack(root/"source",packed);
    const auto live=CMappedResource::LivePayloadBytes();
    { ItemApproachArchiveIndex index;
      Check(index.Has(logical.string(),"item.bon"),"catalog resolves logical RES to PAK sibling");
      Check(index.Has(packed.string(),"items.idb"),"catalog accepts actual PAK DB provider");
      Check(CMappedResource::LivePayloadBytes()==live,"catalog membership loads no member payload");
      Store(root/"foreign.res",Archive("unrelated.bin"));
      Check(!index.Has((root/"foreign.res").string(),"items.idb"),"catalog does not borrow unrelated DB members"); }
    Store(root/"source/_deleted.txt",Bytes{'i','t','e','m','.','m','o','d','\n'});
    fs::remove(root/"source/item.mod");ei::pak::Pack(root/"source",packed);
    { ItemApproachArchiveIndex index;Check(!index.Has(logical.string(),"item.mod"),"catalog tombstone hides old RES member"); }
    Store(packed,Bytes{0,1,2});
    { ItemApproachArchiveIndex index;Check(!index.Has(logical.string(),"item.mod"),"catalog corrupt PAK cannot fall back to RES"); }
    fs::remove(packed);Store(logical,Bytes{0,1,2});
    { ItemApproachArchiveIndex index;Check(!index.Has(logical.string(),"item.mod"),"catalog malformed optional RES stays unavailable without fatal reporting"); }
    owned::ResourceStoreConfig mounts;
    mounts.archives={L"C:/mod/figures.pak",L"C:/mod/textures.pak",L"C:/Base/Res/FIGURES.PAK",L"C:/Base/Res/textures.pak"};
    auto base=owned::critter_base_mounts(mounts);
    Check(base.archives.size()==2&&base.archives[0]==mounts.archives[2]&&base.archives[1]==mounts.archives[3],"larva selects last Base PAK families");
    mounts.archives={L"C:/Base/Res/figures.res",L"C:/Base/Res/textures.res"};
    Check(owned::critter_base_mounts(mounts).archives==mounts.archives,"larva retains RES family selection");
    mounts.archives={L"C:/mod/samfigures.pak",L"C:/mod/textures.pak"};
    Check(owned::critter_base_mounts(mounts).archives.empty(),"larva does not match another figure family by suffix");
    std::cout<<"ITEM_CATALOG index/PAK/RES/provider/tombstone/no_payload and CRITTER_BASE PASS\n";
}
void PakPathChecks(const fs::path& directory) {
    const auto root=fs::absolute(directory/"pak-paths");
    const auto game=root/"portable",base=game/"Base",engine=game/"Engine64";
    fs::create_directories(base/"Res");fs::create_directories(engine);
    const auto before=fs::current_path();fs::current_path(root);
    Check(InstalledGameData::FindRoot(engine).empty(),"empty portable root rejected");
    Store(base/"Res/database.pak",Bytes{1});
    Check(InstalledGameData::FindRoot(engine).empty(),"database alone does not make a data root");
    fs::create_directories(base/"Maps");
    Check(InstalledGameData::FindRoot(engine)==base,"PAK-only portable root found without settings");
    fs::rename(base/"Res/database.pak",base/"Res/database.res");
    Check(InstalledGameData::FindRoot(engine)==base,"RES-only portable root retained");
    Store(base/"Res/database.pak",Bytes{2});
    Check(InstalledGameData::FindRoot(engine)==base,"both archive formats preserve data-root identity");
    const auto classic=root/"classic";fs::create_directories(classic/"Res");fs::create_directories(classic/"Maps");
    Store(classic/"Res/database.pak",Bytes{3});
    Check(InstalledGameData::FindRoot(classic/"bin/Engine64")==classic,"classic root accepts PAK");
    fs::current_path(before);
    owned::ResourceStoreConfig mounts;
    const auto mod=root/"MODS/Dejavu";fs::create_directories(mod/"res");
    const auto layout=(mod/"world-map-layouts.json").wstring();
    Check(!owned::neighbour_profile_matches(mounts,layout),"unverified profile rejected");
    Store(mod/"res/databaseJmv.pak",Bytes{4});
    Check(owned::neighbour_profile_matches(mounts,layout),"PAK-only neighbour profile detected outside visual mounts");
    fs::rename(mod/"res/databaseJmv.pak",mod/"res/databaseJmv.res");
    Check(owned::neighbour_profile_matches(mounts,layout),"RES neighbour profile retained");
    mounts.archives={L"C:/fixture/DATABASEJMV.PAK"};
    Check(owned::neighbour_profile_matches(mounts,L""),"configured PAK identity is case insensitive");
    mounts.archives={L"C:/fixture/database.pak"};
    Check(!owned::neighbour_profile_matches(mounts,L""),"ordinary database cannot enable neighbours");
    Check(!owned::neighbour_profile_matches(mounts,L"world-map-layouts.json"),"relative layout does not probe process CWD");
    std::cout<<"PAK_PATHS portable/classic/profile/CWD PASS\n";
}
void NavigationChecks() {
    using namespace Navigation;
    Request r;r.source={1,2,3};r.goal={10,2,3};r.maxLength=5;r.goalRadius=.15f;r.height=1.6f;
    Location start,goal;start.position=r.source;start.surface=1;start.polygon=10;start.generation=2;
    goal.position=r.goal;goal.surface=11;goal.polygon=20;goal.generation=2;r.start=&start;r.goalSupport=&goal;
    QueryFailure failure(r,2500,2,3,0,3,Status::TooLong);
    auto matches=[&](const Request& request){return failure.Matches(request,1000,2,3,0,3);};
    Check(matches(r), "identical failure missed");
    auto next=r;next.maxLength=20;Check(!matches(next), "larger maxLength reused TooLong");
    next=r;next.goalRadius=1;Check(!matches(next), "changed goal radius reused failure");
    next=r;next.areaCosts[3]=2;Check(!matches(next), "changed costs reused failure");
    next=r;next.source.x+=.01f;Check(!matches(next), "nearby source reused failure");
    next=r;next.goal.y+=.01f;Check(!matches(next), "nearby goal reused failure");
    next=r;next.height=.4f;Check(!matches(next), "changed clearance reused failure");
    next=r;next.excludedFlags=32;Check(!matches(next), "changed excluded flags reused failure");
    next=r;next.goalPolicy=GoalPolicy::ClosestReachable;Check(!matches(next), "changed goal policy reused failure");
    next=r;next.start=nullptr;Check(!matches(next), "missing start reused failure");
    next=r;next.goalSupport=nullptr;Check(!matches(next), "missing goal support reused failure");
    auto otherStart=start;otherStart.polygon++;next=r;next.start=&otherStart;Check(!matches(next), "changed start reused failure");
    auto otherGoal=goal;otherGoal.surface++;next=r;next.goalSupport=&otherGoal;Check(!matches(next), "changed goal floor reused failure");
    Check(!failure.Matches(r,2500,2,3,0,3), "expiry ignored");
    Check(!failure.Matches(r,1000,4,3,0,3), "mesh generation ignored");
    Check(!failure.Matches(r,1000,2,4,0,3), "scene revision ignored");
    Check(!failure.Matches(r,1000,2,3,1,3), "profile ignored");
    Check(!failure.Matches(r,1000,2,3,0,4), "AI class ignored");
    std::vector<Obstacle> obstacles;next=r;next.obstacles=&obstacles;Check(!matches(next), "obstacle query reused failure");
    Route route;next=r;next.reuseRoute=&route;Check(!matches(next), "detour query reused failure");
    Check(!QueryFailure::Cacheable(Status::Updating)&&!QueryFailure::Cacheable(Status::Limit), "temporary search status cached");
    QueryFailure noGoal(r,2500,2,3,0,3,Status::NoGoal);next=r;next.goalRadius=2;
    Check(!noGoal.Matches(next,1000,2,3,0,3), "larger tolerance reused NoGoal");
    // Copies must own their original supports after the caller and vector move.
    const Location savedStart=start,savedGoal=goal;
    std::vector<QueryFailure> stored;stored.push_back(failure);stored.reserve(16);
    start.surface=77;goal.surface=88;next=r;next.start=&savedStart;next.goalSupport=&savedGoal;
    Check(stored[0].Matches(next,1000,2,3,0,3), "cache supports dangle after copy");
}
void RealArchive(const fs::path& path) {
    CWin32MappedResource resource(true);
    Check(resource.OpenResource(path.string().c_str()), "real archive open");
    CStringArray names;resource.GetFileList(names);Check(!names.IsEmpty(), "real archive empty");
    ItemApproachArchiveIndex catalog;
    for(INT_PTR i=0;i<names.GetSize();++i)
        Check(catalog.Has(path.string(),names[i].GetString()),"catalog index agrees with real archive membership");
    unsigned nestedCount=0;
    for(INT_PTR i=0;i<names.GetSize();++i) {
        const std::string name=names[i].GetString();
        const auto extension=fs::path(name).extension().string();
        if(extension!=".mod"&&extension!=".MOD"&&extension!=".bon"&&extension!=".BON"&&extension!=".anm"&&extension!=".ANM")continue;
        DWORD size=0;auto* data=resource.GetFile(name.c_str(),&size);
        if(!data)continue; // historical dead hash entries may be listed but not reachable
        CWin32MappedResource child(true);
        Check(child.OpenResourceMem(data,size), "real nested archive rejected");
        CStringArray children;child.GetFileList(children);Check(!children.IsEmpty(), "real nested archive empty");
        ++nestedCount;if(nestedCount==8)break;
    }
    std::cout<<"REAL_ARCHIVE entries="<<names.GetSize()<<" nested="<<nestedCount<<"\n";
}
}
int main(int argc,char** argv) {
    try {
        if(argc<3)throw std::runtime_error("Usage: check-audit-fixes OUTPUT_DIR MOD_CONFIG [ARCHIVE...]");
        if(!AfxWinInit(GetModuleHandleW(nullptr),nullptr,GetCommandLineA(),0))throw std::runtime_error("MFC init");
        const fs::path output=argv[1];fs::create_directories(output);
        ResourceChecks(output);IdentityChecks(output,argv[2]);NavigationChecks();PakChecks(output);BasePakChecks(output);PakPathChecks(output);ItemCatalogAndCritterChecks(output);
        for(int i=3;i<argc;++i)if(std::string(argv[i])!="--diagnostics")RealArchive(argv[i]);
        std::cout<<"AUDIT_FIXES_SUMMARY checks="<<checks<<" errors=0 diagnostics="<<RuntimeDiagnostics::Enabled()<<"\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<"FAIL after="<<checks<<" reason="<<e.what()<<"\n";}
      catch(CException* e){e->Delete();std::cerr<<"MFC failure\n";}
    return 1;
}
