#include "neighbour_maps.h"
#include "environment_material.h"
#include "neighbour_profile.h"
#ifdef EID3D11_EMBEDDED
#include "embedded_host.h"
#endif
#include "neighbour_piece.h"
#include "ground_cover_neighbour.h"
#include "owned_resource_runtime.h"
#include "owned_asset_loader.h"
#include "owned_terrain_front_end.h"
#include "owned_resource_store.h"
#include "retained_static_scene.h"
#include "worker_priority.h"
#include "native_instance_flags.h"
#include "terrain_face_atlas.h"
#include "engine_bridge.h"
#include "astral_rift.h"
#include <windows.h>
#include <chrono>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <deque>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <atomic>
#include <condition_variable>
#include <fstream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace owned {
void bridge_append_probe_event(const char* event,const char* detail) noexcept;
namespace {
std::atomic<bool> enabled{false}; // default awaits the director's GPU-budget route
std::atomic<double> fog_state[17]{};
std::atomic<std::uint64_t> fog_revision{0};
std::mutex device_lock;
// A raw reference, released only by neighbour_maps_set_device: a static
// COM pointer would Release the device from the DLL's static destructors at
// process exit, after the threads that may hold D3D locks were killed.
ID3D11Device* stream_device = nullptr;
// D3D12 backend: the executor's preparer (under device_lock). Never destroyed
// (as stream_device, no device reference is released at process exit);
// neighbour_maps_set_d3d12(nullptr) drops it at the renderer's release.
std::shared_ptr<D3D12Preparer>& stream_preparer() {
    static auto* preparer=new std::shared_ptr<D3D12Preparer>();
    return *preparer;
}
std::mutex resident_lock;
// The active map's saved-layout entry as the stream last read it (its name
// and origin): the layout file is only known where the addon is loaded (the
// game process), which hands it to the stream.
std::mutex active_layout_lock;
std::string active_layout_name;
double active_layout_x=0,active_layout_y=0;
std::unordered_set<std::string> resident_textures;
// Up to kDecoders neighbours decode at once, each on its own loader; at most
// kDecoders decoded maps exist at a time (in decode, waiting, or in upload),
// so the CPU working set stays bounded (about 50-70 MB per decoded map).
// Uploads leave in layout order, so edge welding never depends on timing.
constexpr unsigned kDecoders = 2;
struct Stream {
    std::mutex lock;
    std::condition_variable wake;
    std::thread worker;
    bool stop = false, in_upload = false, finished = false;
    // Restarted for the coming world of a same-map reload: nothing is taken
    // and it does not settle until the world change (neighbour_maps_restart).
    bool held = false;
    std::uint64_t epoch = 0;
    // Each started stream has its own id; a stream whose id is no longer
    // current is stopped and never touches the shared state again, so a
    // stream that does not exit in time can be left to finish detached.
    std::uint64_t run = 0, exited = 0;
    // Layout-order slots: a neighbour's piece, or empty after a failed decode.
    std::map<std::size_t, std::unique_ptr<NeighbourPiece>> ready;
    // Set when the stream stops: ends a tile family load in flight early.
    std::atomic<bool> cancel{false};
    std::size_t next_take = 0;
    std::size_t total = 0; // neighbours in the selected layout, once read (0 before)
    unsigned resident = 0; // decoded maps alive (in decode, waiting or in upload)
    std::string error;
};
// Never destroyed: a game closed in-world (Alt+F4) exits without releasing the
// device, so neighbour_maps_stop() never runs, and destroying a still-joinable
// std::thread from the DLL's static destructors calls std::terminate.
Stream& stream() { static Stream* s = new Stream; return *s; }
std::wstring layout_file() {
#ifdef EID3D11_EMBEDDED
    return embedded_host_configuration().snapshot().world_map_layouts;
#else
    const auto module = GetModuleHandleW(L"addon.dll");
    if (!module) throw std::runtime_error("Neighbour maps: loaded addon unavailable");
    wchar_t path[32768]; const DWORD n = GetModuleFileNameW(module, path, 32768);
    if (!n || n >= 32768) throw std::runtime_error("Neighbour maps: addon path unavailable");
    const std::wstring file(path,n); const auto separator=file.find_last_of(L"/\\");
    if(separator==std::wstring::npos)throw std::runtime_error("Neighbour maps: addon directory unavailable");
    return file.substr(0,separator+1)+L"world-map-layouts.json";
#endif
}
std::wstring selected_path(const std::wstring& root, const std::string& value, const char* extension) {
    if (value.empty() || value.size() >= 32768 || value.find('\0') != std::string::npos)
        throw std::runtime_error("Invalid neighbour layout path");
    const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),int(value.size()),nullptr,0);
    if(n<=0)throw std::runtime_error("Invalid UTF8 neighbour path");
    std::wstring path(std::size_t(n),0);
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),int(value.size()),&path[0],n);
    for(auto& c:path)if(c==L'/')c=L'\\';
    const auto lower=neighbour_key(value);
    if(lower.size()<4 || lower.substr(lower.size()-4)!=extension)throw std::runtime_error("Wrong neighbour map extension");
    const bool absolute=(path.size()>2 && path[1]==L':' && path[2]==L'\\') ||
        (path.size()>2 && path[0]==L'\\' && path[1]==L'\\');
    if(!absolute) {
        if(path[0]==L'\\' || path.find(L':')!=std::wstring::npos)throw std::runtime_error("Ambiguous neighbour path");
        path=root+path;
    }
    return path;
}
std::string map_name(std::string name) {
    const auto slash=name.find_last_of("/\\");
    if(slash!=std::string::npos)name.erase(0,slash+1);
    name=neighbour_key(std::move(name));
    if(name.size()>4 && name.substr(name.size()-4)==".mpr")name.resize(name.size()-4);
    return name;
}
void set_error(Stream& s,const std::string& error) {
    std::lock_guard<std::mutex> guard(s.lock);s.error=error;
}
// Astral rift models (astral_rift.h): the stream's nodes stop at the authored
// FIGs, one per assembly part. Their generated clump parts get the node of
// the veil they belong to with the clump geometry, and every animated node
// its motion margin in its box (fog culling and visibility). False: an
// assembly that does not have that shape (the decoration is omitted).
static bool prepare_neighbour_astral_nodes(RetainedStaticSourceCandidate& candidate) {
    if (!candidate.assembly || !astral_model(*candidate.assembly)) return true;
    const auto& assembly = *candidate.assembly;
    const std::size_t authored = candidate.nodes.size();
    if (authored > assembly.parts.size()) return false;
    for (std::size_t index = authored; index < assembly.parts.size(); ++index) {
        const auto& part = assembly.parts[index];
        const unsigned veil = astral_clumps(assembly, part);
        if (!veil || !part.geometry) return false;
        std::size_t source = 0;
        while (source < authored && (astral_veil(assembly, assembly.parts[source]) != veil ||
            assembly.parts[source].skeleton_node != part.skeleton_node)) ++source;
        if (source == authored) return false;
        RetainedPreparedNode node = candidate.nodes[source];
        const auto& figure = *part.geometry;
        node.figure = &figure;
        node.positions = figure.positions;
        node.normals = figure.normals;
        node.geometry_identity ^= static_cast<std::uintptr_t>(0x9e3779b9u);
        node.geometry_revision[2] = figure.position_groups;
        node.geometry_revision[3] = figure.normal_groups;
        node.geometry_revision[4] = static_cast<std::uint32_t>(figure.indices.size());
        candidate.nodes.push_back(std::move(node));
    }
    for (std::size_t index = 0; index < candidate.nodes.size(); ++index) {
        const float margin = astral_margin(assembly, assembly.parts[index]);
        if (margin <= 0) continue;
        auto& bounds = candidate.nodes[index].bounds;
        for (unsigned axis = 0; axis < 3; ++axis) { bounds.minimum[axis] -= margin; bounds.maximum[axis] += margin; }
        bounds.radius += margin;
    }
    return true;
}

std::unique_ptr<NeighbourMapUpload> decode_neighbour(OwnedResourceLoader& loader,const std::wstring& root,
    const std::vector<NeighbourSelection>& neighbours,std::size_t index,std::uint64_t epoch,const ResourceStoreConfig& mounts) {
    const auto& n=neighbours[index];
    ResourceFailure failure;
    OwnedMapLoadRequest request;
    request.generation=epoch;request.mounts=mounts;
    request.terrain_resource=n.placement.mpr+".mpr";
    request.map_resource=n.placement.zone+".mob";
    request.selected_terrain_file=selected_path(root,n.placement.mpr_path,".mpr");
    request.terrain_only=n.placement.mob_path.empty();
    if(!request.terrain_only)request.selected_map_file=selected_path(root,n.placement.mob_path,".mob");
    request.family_allowlist={53,54,55,60};
    request.backdrop_objects=true;
    OwnedResourceRequestToken token;
    if(!loader.request(request,failure,&token))throw std::runtime_error(failure.message);
    std::shared_ptr<const OwnedMapSnapshot> map;
    if(loader.wait_publish(token,map)!=OwnedResourceWaitResult::ready || !map)
        throw std::runtime_error("Neighbour map worker did not publish");
    loader.retire_generation(epoch);
    if(!map->terrain || !map->mob)throw std::runtime_error(map->failures.empty()?"Missing neighbour terrain/MOB":map->failures.front());
    if(map->terrain->sector_count_x*32.0!=n.placement.width ||
        map->terrain->sector_count_y*32.0!=n.placement.height)throw std::runtime_error("MPR/catalog size mismatch");
    auto upload=std::make_unique<NeighbourMapUpload>();upload->epoch=epoch;upload->selection=n;upload->map=map;
    for(const auto& other:neighbours)upload->selected.push_back({other.x,other.y,
        other.x+other.placement.width,other.y+other.placement.height});
    upload->decoded_bytes=map->metrics.peak_owned_bytes;
    std::map<std::string,std::uint32_t> omitted;std::map<std::uint32_t,std::uint32_t> drawn;
    for(const auto& object:map->objects) {
        if(!neighbour_decoration_family(object.family))continue;
        std::shared_ptr<RetainedStaticSourceCandidate> candidate;
        const char* why=neighbour_decoration_omission(object,*map);
        RetainedStaticPrepareDiagnostic prepared;
        if(!why && !prepare_retained_static_candidate(map,map->terrain,object,candidate,&prepared))
            why=retained_static_prepare_result_name(prepared.result);
        if(!why && !prepare_neighbour_astral_nodes(*candidate))why="astral_nodes";
        if(why){++upload->omitted_decorations;++omitted[why];continue;}
        // Trees (family 53, the class the main map's sway is given to) take
        // the main map's sway evidence; the assembly is still this worker's.
        if(object.family==53&&candidate->assembly)
            sway_tree_assembly(*std::const_pointer_cast<VisualAssemblyAsset>(candidate->assembly));
        ++drawn[object.family];
        for(const auto& node:candidate->nodes)upload->decoded_bytes+=
            node.positions.capacity()*sizeof(NativePackedGroup)+node.normals.capacity()*sizeof(NativePackedGroup);
        upload->decorations.push_back(std::move(candidate));
    }
    upload->decoration_summary="decorations=53:"+std::to_string(drawn[53])+",54:"+std::to_string(drawn[54])+
        ",55:"+std::to_string(drawn[55])+",60:"+std::to_string(drawn[60])+" omitted_reasons=";
    for(const auto& [reason,count]:omitted)upload->decoration_summary+=reason+":"+std::to_string(count)+",";
    if(omitted.empty())upload->decoration_summary+="none";else upload->decoration_summary.pop_back();
    // Emit the complete bounded list once per decoded map. Keep details out
    // of the renderer's 1536-byte upload summary; aggregate repeated reasons
    // and chunk them so the journal's counter budget is not spent per asset.
    if(!map->failures.empty()) {
        std::map<std::string,std::size_t> reasons;
        for(const auto& failure:map->failures)++reasons[failure];
        constexpr std::size_t limit=256, line_limit=1024, reason_limit=512;
        const auto shown=(std::min)(reasons.size(),limit);
        upload->decoration_summary+=" load_failures="+std::to_string(map->failures.size())+
            " failure_reasons="+std::to_string(reasons.size())+
            " failure_reasons_omitted="+std::to_string(reasons.size()-shown);
        const std::string prefix="map="+n.placement.mpr+" load_failure_details=";
        std::string line=prefix;std::size_t emitted=0;
        for(const auto& [reason,count]:reasons) {
            if(emitted==limit)break;
            std::string item=reason.size()>reason_limit?reason.substr(0,reason_limit)+"...":reason;
            item+=" count="+std::to_string(count);
            if(line.size()>prefix.size() && line.size()+item.size()+3>line_limit) {
                bridge_neighbour_diagnostic(line.c_str());line=prefix;
            }
            if(line.size()>prefix.size())line+=" | ";
            line+=item;++emitted;
        }
        if(line.size()>prefix.size())bridge_neighbour_diagnostic(line.c_str());
    }
    return upload;
}
// A decoration's crown impostor object (static_lod.h): every node with a
// figure and a texture, led by its first cut-out node, baked together once
// per model (the key: each part's figure and texture, and which one leads)
// and switched as one on the object's sphere. That sphere (centre of the
// bounding box of every part vertex at the decoration's shape, radius to the
// farthest) is the one nativeImpostorSelect and the bake both use.
bool neighbour_impostor_plan(const RetainedStaticSourceCandidate& candidate,NeighbourMapUpload::ImpostorPlan& plan) noexcept try {
    plan={};
    // Astral rift models keep their triangles: a bake would freeze the veils.
    if(candidate.assembly&&astral_model(*candidate.assembly))return false;
    // A single coverage atlas cannot represent blended or independently
    // emissive layers. Those objects retain their ordinary material-aware LODs.
    for(const auto& node:candidate.nodes)if(node.environment_material) {
        const auto& m=*node.environment_material;
        if(m.alpha==2||m.unlit||m.textures[3])return false;
    }
    const auto part=[](const RetainedPreparedNode& node){
        const auto& m=node.object_local;
        // Node placements are translations (retained_static_submission.cpp).
        return node.figure&&(node.texture||node.surface)&&!node.positions.empty()&&m[0]==1&&m[5]==1&&m[10]==1&&
            m[1]==0&&m[2]==0&&m[4]==0&&m[6]==0&&m[8]==0&&m[9]==0;
    };
    // Cut-out: the archive texture's top level; for a game surface the
    // archive texture of the same name (build_active_statics), never the
    // surface's own alpha scan (zone13 after travel: per-object surfaces
    // classed houses cut-out, 1112 crown impostors).
    const auto cut_out=[](const RetainedPreparedNode& node){
        if(node.environment_material)return node.environment_material->alpha==1;
        return node.texture?(node.texture->opaque_mips&1u)==0:node.archive_cut_out==1;
    };
    std::size_t triangles=0;bool led=false;
    for(std::size_t i=0;i<candidate.nodes.size();++i) {
        const auto& node=candidate.nodes[i];
        if(!part(node))continue;
        triangles+=node.figure->indices.size()/3;
        if(!led&&cut_out(node)){led=true;plan.lead=static_cast<std::uint32_t>(i);}
    }
    if(!led||triangles<kImpostorMinTriangles)return false;
    constexpr float big=std::numeric_limits<float>::max();
    float low[3]={big,big,big},high[3]={-big,-big,-big};
    const auto each_point=[&](auto&& visit){
        for(const auto& node:candidate.nodes) {
            if(!part(node))continue;
            const std::size_t count=std::size_t(node.figure->position_groups)*4u;
            if(node.positions.size()*4u<count)return false;
            for(const auto& component:node.figure->components) {
                if(component.position>=count)return false;
                const auto& group=node.positions[component.position>>2];const auto lane=component.position&3u;
                visit(group.x[lane]+node.object_local[3],group.y[lane]+node.object_local[7],group.z[lane]+node.object_local[11]);
            }
        }
        return true;
    };
    if(!each_point([&](float x,float y,float z){
        const float p[3]={x,y,z};
        for(unsigned axis=0;axis<3;++axis){low[axis]=(std::min)(low[axis],p[axis]);high[axis]=(std::max)(high[axis],p[axis]);}
    }))return false;
    const float centre[3]={(low[0]+high[0])*.5f,(low[1]+high[1])*.5f,(low[2]+high[2])*.5f};
    float radius=0;
    each_point([&](float x,float y,float z){
        const float dx=x-centre[0],dy=y-centre[1],dz=z-centre[2];
        radius=(std::max)(radius,std::sqrt(dx*dx+dy*dy+dz*dz));
    });
    if(!(radius>0)||!std::isfinite(radius+centre[0]+centre[1]+centre[2]))return false;
    plan.spheres.assign(candidate.nodes.size(),{});
    for(std::size_t i=0;i<candidate.nodes.size();++i) {
        const auto& node=candidate.nodes[i];
        if(!part(node))continue;
        plan.spheres[i]={centre[0]-node.object_local[3],centre[1]-node.object_local[7],centre[2]-node.object_local[11],radius};
        plan.key+=node.figure->identity.key()+"|"+std::to_string(node.texture_identity)+(i==plan.lead?"*;":";");
    }
    // A decoration whose parts morph (eight shapes) keeps its vertices in
    // the sphere's unit frame, where the bake image lives: prebuild_decorations
    // groups its model's shapes by them (neighbour_impostor_group).
    for(const auto& node:candidate.nodes)plan.morphs=plan.morphs||(part(node)&&node.figure->shape_count==8);
    if(plan.morphs) {
        if(!candidate.source){plan={};return false;}
        each_point([&](float x,float y,float z){
            plan.unit_points.push_back((x-centre[0])/radius);plan.unit_points.push_back((y-centre[1])/radius);
            plan.unit_points.push_back((z-centre[2])/radius);
        });
    }
    return true;
} catch(...) {
    plan={};
    return false;
}
// Morphing decorations' impostors (plan.morphs): one bake per group of a
// model's shapes. Keyed by model alone, every palm of a map showed the
// first-baked palm's image stretched into its own sphere (far palms of other
// shapes lost their wide crowns, save1027 oasis, 2026-11-08); one bake per
// exact shape (per-object designer values, nearly all distinct) took the
// save1207 session's atlas from 114 to ~1400 slices. A shape joins the first
// group whose representative's vertices all lie within one bake texel of
// its own in the sphere's unit frame (the image each object draws into its
// own sphere), taken in shape order, so the groups and their keys (the
// representative's exact shape) are the same on every load of the map.
void neighbour_impostor_group(const NeighbourMapUpload& upload,std::vector<NeighbourMapUpload::ImpostorPlan>& plans,float shape_texels) {
    // One frame texel in the unit frame: the frame's kImpostorFrameTexels span
    // the sphere's diameter at the bake margin (.94, kImpostorFrameHlsl).
    // shape_texels: the allowed error, in frame texels (prebuild_decorations).
    const float texel=shape_texels*2.f/(.94f*kImpostorFrameTexels);
    std::map<std::string,std::vector<std::size_t>> models;
    for(std::size_t i=0;i<plans.size();++i)if(!plans[i].key.empty()&&plans[i].morphs)models[plans[i].key].push_back(i);
    for(auto& [model,members]:models) {
        const auto shape=[&](std::size_t i){return upload.decorations[i]->source->shape;};
        std::sort(members.begin(),members.end(),[&](std::size_t a,std::size_t b){
            const auto sa=shape(a),sb=shape(b);return sa!=sb?sa<sb:a<b;});
        std::vector<std::size_t> representatives;
        for(const std::size_t i:members) {
            const auto& points=plans[i].unit_points;
            std::size_t chosen=i;
            for(const std::size_t r:representatives) {
                const auto& other=plans[r].unit_points;
                if(other.size()!=points.size())continue;
                bool within=true;
                for(std::size_t k=0;within&&k<points.size();k+=3) {
                    const float dx=points[k]-other[k],dy=points[k+1]-other[k+1],dz=points[k+2]-other[k+2];
                    within=dx*dx+dy*dy+dz*dz<=texel*texel;
                }
                if(within){chosen=r;break;}
            }
            if(chosen==i)representatives.push_back(i);
            std::uint32_t bits[3]{};std::memcpy(bits,shape(chosen).data(),sizeof(bits));
            plans[i].representative=chosen;
            plans[i].key+="shape:"+std::to_string(bits[0])+","+std::to_string(bits[1])+","+std::to_string(bits[2])+";";
        }
    }
    for(auto& plan:plans)std::vector<float>().swap(plan.unit_points);
}
// The bake input of a plan's object, from this decoration's parts in its lead
// node's frame (empty parts: not bakeable).
void neighbour_impostor_object(const RetainedStaticSourceCandidate& candidate,const NeighbourMapUpload::ImpostorPlan& plan,
    NeighbourMapUpload::ImpostorObject& object) noexcept try {
    const auto& lead=candidate.nodes[plan.lead];
    object.sphere=plan.spheres[plan.lead];
    for(std::size_t i=0;i<candidate.nodes.size();++i) {
        if(!(plan.spheres[i][3]>0))continue;
        const auto& node=candidate.nodes[i];
        const std::array<float,3> offset{node.object_local[3]-lead.object_local[3],node.object_local[7]-lead.object_local[7],
            node.object_local[11]-lead.object_local[11]};
        NativeImpostorPart made;
        if(!build_impostor_part(*node.figure,node.positions.data(),offset,made,node.normals.empty()?nullptr:node.normals.data())){object.parts.clear();object.nodes.clear();return;}
        object.parts.push_back(std::move(made));object.nodes.push_back(&node);
    }
} catch(...) {
    object.parts.clear();object.nodes.clear();
}
// After the weld: decode every terrain command and decoration node into
// dedicated geometry pages on this worker, so the render thread only adopts
// them. A failed page drops the whole prebuild (the render thread then decodes
// as before); a mesh the builder refuses decodes at upload.
void prebuild_neighbour(NeighbourMapUpload& upload) noexcept {
    NativePrebuiltBuilder builder;
    {std::lock_guard<std::mutex> guard(device_lock);builder.device=stream_device;if(!builder.device)builder.d3d12=stream_preparer();}
    if(!builder.device&&!builder.d3d12)return;
    const auto drop=[&upload] {
        upload.prebuilt_pages.clear();upload.prebuilt_terrain_keys.clear();
        upload.prebuilt_terrain.clear();upload.prebuilt_figures.clear();
    };
    try {
        struct Terrain {NativePrebuiltBuilder* builder;NeighbourMapUpload* upload;} terrain{&builder,&upload};
        bridge_prepare_neighbour_terrain(upload,[](void* opaque,std::uint32_t sector,std::uint32_t material,const NativeGeometryView& view) {
            auto& t=*static_cast<Terrain*>(opaque);
            NativePrebuiltMesh mesh;
            if(t.builder->failed || !build_native_prebuilt_mesh(*t.builder,view,mesh))return;
            t.upload->prebuilt_terrain_keys.push_back((sector<<16)|material);
            t.upload->prebuilt_terrain.push_back(mesh);
        },&terrain);
        // Neighbour maps are seen only far away, where a frame texel is far
        // under a pixel: their shape groups allow 3 texels (the active map 1).
        prebuild_decorations(upload,builder,3.f);
        if(!finish_native_prebuilt_pages(builder) || builder.failed){drop();return;}
        upload.prebuilt_pages=std::move(builder.pages);
        // Its atlas pages and decoration textures, once each. Always created:
        // its piece carries no mips, so adoption must find every texture it binds
        // (one already resident wins there and this copy is dropped).
        std::unordered_set<std::string> seen;
        const auto& map_terrain=*upload.map->terrain;
        for(const auto& page:map_terrain.pages) {
            if(!page.texture || page.mips.empty())continue;
            NativeOwnedTextureView view{};
            view.identity=page.texture->identity.container_incarnation;view.generation=1;
            view.resource=&page.texture->identity;view.value_source=1;
            view.format=static_cast<NativeOwnedTextureFormat>(page.texture->format);
            view.mips=page.mips.data();view.mip_count=static_cast<std::uint32_t>(page.mips.size());
            prebuild_texture(builder,upload,seen,view);
        }
        prebuild_decoration_textures(upload,builder,seen);
    } catch(...) {drop();}
}
} // namespace
// The decorations' meshes: static LOD levels once per figure (every shape of
// it shares them), one mesh per figure and exact shape, and each object's
// impostor plan and bake input once per model.
void prebuild_decorations(NeighbourMapUpload& upload,NativePrebuiltBuilder& builder,float shape_texels) {
        std::map<const FigureCpuAsset*,NativeMeshLods> lods;
        const auto lod_start=std::chrono::steady_clock::now();
        double lod_seconds=0;
        upload.impostor_plans.assign(upload.decorations.size(),{});
        for(std::size_t i=0;i<upload.decorations.size();++i) {
            auto& plan=upload.impostor_plans[i];
            if(!(upload.decorations[i]->impostor_inputs_complete && neighbour_impostor_plan(*upload.decorations[i],plan)))plan={};
            plan.representative=i;
        }
        neighbour_impostor_group(upload,upload.impostor_plans,shape_texels);
        for(std::size_t i=0;i<upload.decorations.size() && !builder.failed;++i) {
            const auto& candidate=*upload.decorations[i];
            // An object with a cut-out part gets its impostor bake input once
            // per model (per shape group: its representative's parts); each of
            // its parts' meshes holds the object's own sphere.
            const auto& plan=upload.impostor_plans[i];
            if(!plan.key.empty()) {
                const auto found=upload.impostor_objects.try_emplace(plan.key);
                if(found.second)neighbour_impostor_object(*upload.decorations[plan.representative],
                    upload.impostor_plans[plan.representative],found.first->second);
            }
            for(std::size_t n=0;n<candidate.nodes.size();++n) {
                const auto& node=candidate.nodes[n];
                if(builder.failed)break;
                const auto* sphere=!plan.key.empty()&&plan.spheres[n][3]>0?&plan.spheres[n]:nullptr;
                const auto inserted=upload.prebuilt_figures.try_emplace(neighbour_figure_shape(node.figure,candidate.source->shape,sphere));
                if(!inserted.second)continue;
                auto level=lods.find(node.figure);
                if(level==lods.end()) {
                    const auto begin=std::chrono::steady_clock::now();
                    level=lods.emplace(node.figure,NativeMeshLods{}).first;
                    ++upload.lod_stats.figures;upload.lod_stats.full_triangles+=node.figure->indices.size()/3;
                    // No reduced levels for astral rift veils and clumps (their
                    // vertex stage animates the full mesh).
                    const bool astral=candidate.assembly&&n<candidate.assembly->parts.size()&&
                        astral_kind(*candidate.assembly,candidate.assembly->parts[n]);
                    if(!astral&&build_static_lods(*node.figure,level->second)) {
                        ++upload.lod_stats.reduced_figures;
                        for(std::uint32_t k=0;k<level->second.levels;++k)upload.lod_stats.level_triangles[k]+=level->second.indices[k].size()/3;
                    }
                    lod_seconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
                }
                auto view=neighbour_figure_view(*node.figure,node.positions.data(),candidate.source->shape);
                view.lods=&level->second;
                if(sphere&&!view.world.shape_normals)view.world.impostor_sphere=*sphere;
                if(!build_native_prebuilt_mesh(builder,view,inserted.first->second))inserted.first->second=NativePrebuiltMesh{};
            }
        }
        upload.lod_stats.milliseconds=lod_seconds*1000.0;
        upload.lod_stats.prebuild_milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-lod_start).count();
}
// One texture the stream creates for its piece, once per cache key.
void prebuild_texture(NativePrebuiltBuilder& builder,NeighbourMapUpload& upload,
    std::unordered_set<std::string>& seen,const NativeOwnedTextureView& view) {
    if(!view.resource)return;
    // D3D12 uncompressed members are keyed by their view (native_owned_mip_texture_key).
    UINT pixel_bytes=0;
    const bool by_view=builder.d3d12&&native_owned_uncompressed_format(view.format,pixel_bytes)!=DXGI_FORMAT_UNKNOWN;
    const auto key=by_view?native_owned_mip_texture_key(view,view.mip_count==1):
        native_owned_archive_texture_key(*view.resource,view.generation,false);
    if(!seen.insert(key).second)return;
    NativePrebuiltTexture made;
    if(build_native_prebuilt_texture(builder.device.Get(),builder.d3d12.get(),view,made))upload.prebuilt_textures.push_back(std::move(made));
}
void prebuild_decoration_textures(NeighbourMapUpload& upload,NativePrebuiltBuilder& builder,
    std::unordered_set<std::string>& seen) {
    for(const auto& candidate:upload.decorations)if(candidate->assembly)for(const auto& part:candidate->assembly->parts)
        if(part.environment_material)for(const auto& texture:part.environment_material->textures)if(texture)prebuild_texture(builder,upload,seen,texture->view());
    for(const auto& candidate:upload.decorations)for(const auto& node:candidate->nodes) {
        if(!node.texture || node.texture_mips.empty())continue;
        NativeOwnedTextureView view{};
        view.identity=node.texture_identity;view.generation=1;view.resource=&node.texture->identity;
        view.value_source=1;view.format=node.texture_format;
        view.mips=node.texture_mips.data();view.mip_count=static_cast<std::uint32_t>(node.texture_mips.size());
        prebuild_texture(builder,upload,seen,view);
    }
}
namespace {
std::string read_layout(const std::wstring& file) {
    std::ifstream input(file,std::ios::binary|std::ios::ate);
    const auto bytes=input.tellg();
    if(!input || bytes<0 || bytes>4u*1024u*1024u)throw std::runtime_error("Invalid neighbour layout file size");
    std::string json(std::size_t(bytes),'\0');input.seekg(0);
    if(!input.read(json.data(),std::streamsize(json.size())))throw std::runtime_error("Cannot read neighbour layout");
    return json;
}
void run(Stream& s,std::string active,std::uint64_t epoch,ResourceStoreConfig mounts,std::uint64_t id,std::string no_sea_maps,
    NeighbourStreamSource source_inputs) {
    const BackgroundWorkerPriority priority;
    // Under s.lock: this stream was stopped or replaced.
    const auto stopped=[&s,id] {return s.stop || s.run!=id;};
    const auto set_error=[&s,id](Stream&,const std::string& error) {
        std::lock_guard<std::mutex> guard(s.lock);if(s.run==id)s.error=error;
    };
    try {
        const auto& file=source_inputs.layout_file;
        if(file.empty())throw std::runtime_error("Neighbour maps: loaded addon unavailable (no layout file)");
        if(!neighbour_profile_matches(mounts,file))throw std::runtime_error("No verified neighbour resource profile");
        const auto root=file.substr(0,file.find_last_of(L"/\\")+1);
        const auto layout=parse_neighbour_layout(read_layout(file));
        {
            const auto& entry=neighbour_layout_active(layout,"deja-vu",map_name(active));
            if(std::isfinite(entry.x)&&std::isfinite(entry.y)) {
                std::lock_guard<std::mutex> guard(active_layout_lock);
                active_layout_name=map_name(active);active_layout_x=entry.x;active_layout_y=entry.y;
            }
        }
        const auto neighbours=select_neighbour_layout(layout,"deja-vu",map_name(active));
        {std::lock_guard<std::mutex> guard(s.lock);if(s.run==id)s.total=neighbours.size();}
        std::size_t next_claim=0; // guarded by s.lock
        // Ordered weld: the decoder holding neighbour `next_weld` welds it
        // against the active map's edge strips and every earlier welded
        // neighbour's, exactly as the render thread did at upload, then
        // appends its own strips. Only the turn holder touches `strips`.
        std::size_t next_weld=0;               // guarded by s.lock
        std::vector<NeighbourEdgeStrip> strips; // turn holder only
        // Liquid across map seams (turn holder only). Every placed map at a
        // whole-metre offset from the active map (only those share vertices)
        // takes part. A river crossing a seam between two neighbours must run
        // one way whichever is welded first (save1163: gipat2|zone9 halves
        // ran apart), so each seam's direction comes from both maps' drain
        // potentials as each map's field gives them with no roles yet (its
        // seams marked only): forecast[i], read from neighbour i's own file
        // when a neighbour welded before it adjoins it. welded[i] holds its
        // final flow, which later maps' edges blend to. The active map's field
        // never changes, so its neighbours follow its flow.
        std::shared_ptr<const TerrainCpuAsset> active_terrain;
        TerrainLiquidRim active_rim;
        std::vector<std::shared_ptr<const TerrainLiquidRim>> forecast(neighbours.size()),welded(neighbours.size());
        std::vector<std::uint8_t> forecast_read(neighbours.size(),0);
        const auto whole=[&](std::size_t j){return neighbours[j].x==std::floor(neighbours[j].x)&&neighbours[j].y==std::floor(neighbours[j].y);};
        const auto touches=[&](std::size_t a,std::size_t b) {
            const auto& p=neighbours[a];const auto& q=neighbours[b];
            return p.x<=q.x+q.placement.width&&q.x<=p.x+p.placement.width&&p.y<=q.y+q.placement.height&&q.y<=p.y+p.placement.height;
        };
        // Map j's placed neighbours (the active map first, then layout order),
        // with the rims chosen by rim(k); offsets in j's grid.
        const auto beside=[&](std::size_t j,const auto& rim) {
            const int jx=int(neighbours[j].x),jy=int(neighbours[j].y);
            std::vector<TerrainAdjoiningLiquid> list;
            TerrainAdjoiningLiquid a;a.offset_x=jx;a.offset_y=jy;
            a.xmax=int(active_terrain->sector_count_x*32);a.ymax=int(active_terrain->sector_count_y*32);
            a.rim=rim(SIZE_MAX);a.flows=a.fixed=true;
            list.push_back(a);
            for(std::size_t k=0;k<neighbours.size();++k) {
                if(k==j||!whole(k))continue;
                TerrainAdjoiningLiquid n;n.offset_x=jx-int(neighbours[k].x);n.offset_y=jy-int(neighbours[k].y);
                n.xmax=int(neighbours[k].placement.width);n.ymax=int(neighbours[k].placement.height);
                n.rim=rim(k);n.flows=n.rim&&n.rim==welded[k].get();
                list.push_back(n);
            }
            return list;
        };
        const auto no_rims=[](std::size_t){return static_cast<const TerrainLiquidRim*>(nullptr);};
        // Neighbour j's rim with only its seams marked.
        const auto forecast_rim=[&](std::size_t j,TerrainCpuAsset& terrain) {
            owned_terrain_set_outside_liquid(terrain,beside(j,no_rims),nullptr);
            auto rim=std::make_shared<TerrainLiquidRim>(owned_terrain_liquid_rim(terrain));
            return std::shared_ptr<const TerrainLiquidRim>(rim->heights.empty()?nullptr:std::move(rim));
        };
        // Neighbour j's forecast read from its own MPR, before it is decoded.
        // A failure is logged and leaves that map's seams with no roles.
        const auto read_forecast=[&](std::size_t j)->std::shared_ptr<const TerrainLiquidRim> {
            ResourceStore store;ResourceBlob blob;ResourceFailure failure;
            TerrainCpuAsset terrain;
            const auto name=neighbours[j].placement.mpr+".mpr";
            if(!store.read_loose_file(selected_path(root,neighbours[j].placement.mpr_path,".mpr"),name,blob,failure)||
               !parse_terrain_asset(blob,name,terrain,failure)||!validate_terrain_asset_descriptor_once(terrain)) {
                bridge_append_probe_event("liquid_forecast",(name+": "+(failure.message.empty()?std::string("terrain rejected"):failure.message)+
                    "; its seams take no flow roles").c_str());
                return nullptr;
            }
            return forecast_rim(j,terrain);
        };
        const auto weld=[&](NeighbourMapUpload& upload,std::size_t i) {
            if(!active_terrain) {
                // The active map decodes in parallel inside the game's load;
                // its terrain is published before the load returns.
                const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
                for(;;) {
                    if(source_inputs.active_terrain && (active_terrain=source_inputs.active_terrain()))break;
                    {std::lock_guard<std::mutex> guard(s.lock);if(stopped())return false;}
                    if(std::chrono::steady_clock::now()>deadline)
                        throw std::runtime_error("active map terrain was not published");
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                strips=neighbour_edge_strips(*active_terrain,0,0);
                active_rim=owned_terrain_liquid_rim(*active_terrain);
            }
            // Freshly decoded and not yet shared: nothing else reads it.
            auto& terrain=const_cast<TerrainCpuAsset&>(*upload.map->terrain);
            const double x=upload.selection.x,y=upload.selection.y;
            // Its own forecast from the decoded terrain before any weld edits
            // it (the same data its file gives).
            if(whole(i)&&!forecast_read[i]){forecast[i]=forecast_rim(i,terrain);forecast_read[i]=1;}
            neighbour_weld_edge_heights(terrain,x,y,strips);
            neighbour_weld_edge_normals(terrain,x,y,strips);
            neighbour_weld_edge_shading(terrain,x,y,strips);
            // Its liquid reads the liquid of every map beside it across their
            // shared edges, each seam's role decided as above.
            if(whole(i)) {
                for(std::size_t j=i+1;j<neighbours.size();++j)
                    if(whole(j)&&!forecast_read[j]&&touches(i,j)){forecast_read[j]=1;forecast[j]=read_forecast(j);}
                const auto rims=[&](std::size_t k)->const TerrainLiquidRim* {
                    if(k==SIZE_MAX)return active_rim.heights.empty()?nullptr:&active_rim;
                    return welded[k]?welded[k].get():forecast[k].get();
                };
                owned_terrain_set_outside_liquid(terrain,beside(i,rims),forecast[i].get());
                if(forecast[i]) {
                    auto rim=std::make_shared<TerrainLiquidRim>(owned_terrain_liquid_rim(terrain));
                    rim->potential=forecast[i]->potential; // the seam rule reads the forecast
                    welded[i]=std::move(rim);
                }
            }
            for(auto& strip:neighbour_edge_strips(terrain,x,y))strips.push_back(std::move(strip));
            upload.welded=true;
            return true;
        };
        const auto decoder=[&]() {
            const BackgroundWorkerPriority priority;
            OwnedResourceLoader loader;ResourceFailure failure;
            if(!loader.start(1,failure)){set_error(s,failure.message);return;}
            for(;;) {
                std::size_t index=0;
                {
                    // Claim the lowest unclaimed neighbour once a decoded-map
                    // slot is free. Claimed indices are always the lowest
                    // unfinished ones, so in-order delivery cannot stall.
                    std::unique_lock<std::mutex> guard(s.lock);
                    // Held for the coming world, finished pieces wait for the
                    // world change without a decode slot, so every neighbour
                    // prepares during the game's load (the 64-bit host only:
                    // neighbour_client_restart).
                    const auto slot_free=[&] {
                        unsigned busy=s.resident;
                        if(s.held)for(const auto& slot:s.ready)if(slot.second&&busy)--busy;
                        return busy<kDecoders;
                    };
                    s.wake.wait(guard,[&]{return stopped() || next_claim>=neighbours.size() || slot_free();});
                    if(stopped() || next_claim>=neighbours.size())break;
                    index=next_claim++;++s.resident;
                }
                const auto load_start=std::chrono::steady_clock::now();
                std::unique_ptr<NeighbourMapUpload> upload;
                try {upload=decode_neighbour(loader,root,neighbours,index,epoch,mounts);}
                catch(const std::exception& e) {set_error(s,neighbours[index].placement.zone+": "+e.what());}
                catch(...) {set_error(s,neighbours[index].placement.zone+": neighbour decode allocation failed");}
                {
                    std::unique_lock<std::mutex> guard(s.lock);
                    s.wake.wait(guard,[&]{return stopped() || next_weld==index;});
                }
                try {if(upload && !weld(*upload,index))upload.reset();}
                catch(const std::exception& e) {upload.reset();set_error(s,neighbours[index].placement.zone+": "+e.what());}
                catch(...) {upload.reset();set_error(s,neighbours[index].placement.zone+": neighbour weld allocation failed");}
                {
                    // The next neighbour may weld while this one builds its pages.
                    std::lock_guard<std::mutex> guard(s.lock);
                    ++next_weld;
                }
                s.wake.notify_all();
                // The piece the renderer adopts; the decoded map dies here.
                std::unique_ptr<NeighbourPiece> piece;
                if(upload) {
                    try {
                        prebuild_neighbour(*upload);
                        upload->ground_cover_inputs=prepare_ground_cover_neighbour_inputs(*upload->map,mounts,&s.cancel);
                        upload->liquid_surf=prepare_liquid_surf_neighbour(upload->ground_cover_inputs.get(),
                            upload->selection.placement.mpr,no_sea_maps);
                        ID3D11Device* device=nullptr;std::shared_ptr<D3D12Preparer> d3d12;
                        {std::lock_guard<std::mutex> guard(device_lock);device=stream_device;if(device)device->AddRef();else d3d12=stream_preparer();}
                        piece=make_neighbour_piece(*upload,device,d3d12.get());
                        if(device)device->Release();
                        piece->lod_stats.load_milliseconds=std::chrono::duration<double,std::milli>(
                            std::chrono::steady_clock::now()-load_start).count();
                        // Its face grid from the welded terrain (the faces its batches
                        // draw): the renderer builds its face atlas from it when the
                        // face cache is on, without the decoded terrain.
                        if(piece && upload->map->terrain) {
                            auto grid=std::make_shared<TerrainFaceGrid>();std::string failure;
                            if(build_terrain_face_grid(*upload->map->terrain,*grid,failure))piece->face_grid=std::move(grid);
                        }
                    }
                    catch(const std::exception& e) {set_error(s,neighbours[index].placement.zone+": "+e.what());}
                    catch(...) {set_error(s,neighbours[index].placement.zone+": neighbour piece allocation failed");}
                    upload.reset();
                }
                {
                    std::lock_guard<std::mutex> guard(s.lock);
                    if(stopped())break; // a new stream reset the resident count
                    if(!piece)--s.resident; // only a delivered piece stays resident
                    s.ready[index]=std::move(piece);
                }
                s.wake.notify_all();
            }
            loader.shutdown();
        };
        std::vector<std::thread> decoders;
        try {
            const auto count=(std::min<std::size_t>)(kDecoders,neighbours.size());
            for(std::size_t i=0;i<count;++i)decoders.emplace_back(decoder);
        } catch(...) {set_error(s,"Neighbour decoder thread creation failed");}
        for(auto& thread:decoders)thread.join();
    } catch(const std::exception& e) {set_error(s,e.what());}
    catch(...) {set_error(s,"Neighbour map allocation failed");}
    {
        std::lock_guard<std::mutex> guard(s.lock);
        if(s.run==id)s.finished=true;
        s.exited=(std::max)(s.exited,id);
    }
    s.wake.notify_all();
}
}
const char* neighbour_decoration_omission(const StaticObjectSource& object,
    const OwnedMapSnapshot& map) noexcept {
    if(!neighbour_decoration_family(object.family))return "family";
    if(object.rejection!=StaticObjectRejection::none)return static_object_rejection_name(object.rejection);
    if(!object.nid || object.nid>0x7fffffffu)return "source_values";
    return retained_static_structure_omission(object,map);
}
namespace {
// Sector seams store a shared grid vertex once per touching sector.
template<class Terrain,class Visit> void each_base_copy(Terrain& terrain,std::uint32_t gx,std::uint32_t gy,Visit&& visit) {
    for(std::uint32_t j=0;j<2;++j) {
        if(j && (!gy || gy%32))continue;
        const std::uint32_t sy=gy/32-j;
        if(sy>=terrain.sector_count_y)continue;
        for(std::uint32_t i=0;i<2;++i) {
            if(i && (!gx || gx%32))continue;
            const std::uint32_t sx=gx/32-i;
            if(sx>=terrain.sector_count_x)continue;
            auto& vertices=terrain.sectors[std::size_t(sy)*terrain.sector_count_x+sx].vertices;
            const std::size_t index=std::size_t(gy-sy*32)*33+(gx-sx*32);
            if(index<vertices.size())visit(vertices[index]);
        }
    }
}
// The (sector, base index) of each stored copy of grid vertex (gx,gy); visit
// returns true to continue past the first copy.
template<class Terrain,class Visit> void each_base_slot(Terrain& terrain,std::uint32_t gx,std::uint32_t gy,Visit&& visit) {
    for(std::uint32_t j=0;j<2;++j) {
        if(j && (!gy || gy%32))continue;
        const std::uint32_t sy=gy/32-j;
        if(sy>=terrain.sector_count_y)continue;
        for(std::uint32_t i=0;i<2;++i) {
            if(i && (!gx || gx%32))continue;
            const std::uint32_t sx=gx/32-i;
            if(sx>=terrain.sector_count_x)continue;
            auto& sector=terrain.sectors[std::size_t(sy)*terrain.sector_count_x+sx];
            if(!visit(sector,(gy-sy*32)*33u+(gx-sx*32)))return;
        }
    }
}
struct EdgeSide {double line,start;std::uint32_t count;std::uint32_t gx0,gy0,dx,dy;};
EdgeSide edge_side(const TerrainCpuAsset& terrain,double x,double y,unsigned side) noexcept {
    const std::uint32_t w=terrain.sector_count_x*32,h=terrain.sector_count_y*32;
    switch(side) {
    case 0: return {x,y,h+1,0,0,0,1};
    case 1: return {x+w,y,h+1,w,0,0,1};
    case 2: return {y,x,w+1,0,0,1,0};
    default: return {y+h,x,w+1,0,h,1,0};
    }
}
}
std::vector<NeighbourEdgeStrip> neighbour_edge_strips(const TerrainCpuAsset& terrain,double x,double y) {
    std::vector<NeighbourEdgeStrip> strips(4);
    for(unsigned side=0;side<4;++side) {
        const auto edge=edge_side(terrain,x,y,side);
        auto& strip=strips[side];strip.side=side;strip.line=edge.line;strip.start=edge.start;
        strip.normals.resize(edge.count);strip.positions.resize(edge.count);
        strip.slopes.resize(edge.count);strip.flatness.resize(edge.count);
        for(std::uint32_t k=0;k<edge.count;++k) {
            bool first=true;
            each_base_copy(terrain,edge.gx0+k*edge.dx,edge.gy0+k*edge.dy,[&](const TerrainVertex& v) {
                if(first){strip.normals[k]={v.normal_x,v.normal_y,v.normal_z};strip.positions[k]={float(v.x+x),float(v.y+y),v.z};}first=false;
            });
            each_base_slot(terrain,edge.gx0+k*edge.dx,edge.gy0+k*edge.dy,[&](const TerrainSectorCpuAsset& sector,std::uint32_t base) {
                strip.slopes[k]=owned_terrain_slope_normal(terrain,sector,base);
                strip.flatness[k]=owned_terrain_facet_flatness(terrain,sector,base);
                return false;
            });
        }
    }
    return strips;
}
void neighbour_weld_edge_heights(TerrainCpuAsset& terrain,double x,double y,
    const std::vector<NeighbourEdgeStrip>& strips) {
    const int w=int(terrain.sector_count_x*32),h=int(terrain.sector_count_y*32);
    for(unsigned side=0;side<4;++side) {
        const auto edge=edge_side(terrain,x,y,side);
        // Inward step from this side (0 x0: +x, 1 x1: -x, 2 y0: +y, 3 y1: -y).
        const int ix=side==0?1:side==1?-1:0,iy=side==2?1:side==3?-1:0;
        for(const auto& strip:strips) {
            if(strip.side!=(side^1u) || strip.line!=edge.line || strip.positions.empty() ||
               strip.start!=std::floor(strip.start) || edge.start!=std::floor(edge.start))continue;
            const double last=double(strip.positions.size()-1);
            terrain.welded_sides|=std::uint8_t(1u<<side);
            for(std::uint32_t k=0;k<edge.count;++k) {
                const double along=edge.start+k-strip.start;
                if(along<0 || along>last)continue;
                const auto& target=strip.positions[std::size_t(along)];
                const int gx=int(edge.gx0+k*edge.dx),gy=int(edge.gy0+k*edge.dy);
                std::array<float,3> own{};bool found=false;
                each_base_copy(terrain,std::uint32_t(gx),std::uint32_t(gy),[&](const TerrainVertex& v){
                    if(!found){own={float(v.x+x),float(v.y+y),v.z};found=true;}});
                const std::array<float,3> delta{target[0]-own[0],target[1]-own[1],target[2]-own[2]};
                if(!found || !std::isfinite(delta[0]+delta[1]+delta[2]) ||
                   (delta[0]==0.f&&delta[1]==0.f&&delta[2]==0.f))continue;
                for(int j=0;j<kNeighbourHeightWeldBand;++j) {
                    const int px=gx+ix*j,py=gy+iy*j;
                    if(px<0||py<0||px>w||py>h)break;
                    const float t=1.f-float(j)/float(kNeighbourHeightWeldBand),share=t*t*(3-2*t);
                    each_base_copy(terrain,std::uint32_t(px),std::uint32_t(py),[&](TerrainVertex& v){
                        v.x+=delta[0]*share;v.y+=delta[1]*share;v.z+=delta[2]*share;});
                }
            }
        }
    }
}
std::size_t neighbour_weld_edge_normals(TerrainCpuAsset& terrain,double x,double y,
    const std::vector<NeighbourEdgeStrip>& strips) {
    std::size_t welded=0;
    for(unsigned side=0;side<4;++side) {
        const auto edge=edge_side(terrain,x,y,side);
        for(const auto& strip:strips) {
            if(strip.side!=(side^1u) || strip.line!=edge.line || strip.normals.empty())continue;
            const double last=double(strip.normals.size()-1);
            for(std::uint32_t k=0;k<edge.count;++k) {
                const double along=edge.start+k-strip.start;
                if(along<0 || along>last)continue;
                const auto i=std::size_t(along);const float f=float(along-double(i));
                const auto& a=strip.normals[i];const auto& b=strip.normals[(std::min)(i+1,strip.normals.size()-1)];
                std::array<float,3> n{a[0]+(b[0]-a[0])*f,a[1]+(b[1]-a[1])*f,a[2]+(b[2]-a[2])*f};
                const float length=std::sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
                if(!(length>0.f))continue;
                for(auto& c:n)c/=length;
                each_base_copy(terrain,edge.gx0+k*edge.dx,edge.gy0+k*edge.dy,[&](TerrainVertex& v) {
                    v.normal_x=n[0];v.normal_y=n[1];v.normal_z=n[2];
                });
                ++welded;
            }
        }
    }
    return welded;
}
std::size_t neighbour_weld_edge_shading(TerrainCpuAsset& terrain,double x,double y,
    const std::vector<NeighbourEdgeStrip>& strips) {
    // The values of the welded grid (heights already moved onto the strips).
    owned_terrain_prepare_shading(terrain);
    std::size_t welded=0;
    for(unsigned side=0;side<4;++side) {
        const auto edge=edge_side(terrain,x,y,side);
        for(const auto& strip:strips) {
            if(strip.side!=(side^1u) || strip.line!=edge.line || strip.slopes.empty() ||
               strip.start!=std::floor(strip.start) || edge.start!=std::floor(edge.start))continue;
            const double last=double(strip.slopes.size()-1);
            for(std::uint32_t k=0;k<edge.count;++k) {
                const double along=edge.start+k-strip.start;
                if(along<0 || along>last)continue;
                const auto at=std::size_t(along);
                bool any=false;
                each_base_slot(terrain,edge.gx0+k*edge.dx,edge.gy0+k*edge.dy,[&](TerrainSectorCpuAsset& sector,std::uint32_t base) {
                    if(base<sector.base_slope_normals.size())sector.base_slope_normals[base]=strip.slopes[at];
                    if(base<sector.base_facet_flatness.size())sector.base_facet_flatness[base]=strip.flatness[at];
                    any=true;
                    return true;
                });
                welded+=any?1u:0u;
            }
        }
    }
    return welded;
}
void neighbour_maps_enable(bool value) noexcept {enabled.store(value,std::memory_order_release);}
bool neighbour_maps_enabled() noexcept {return enabled.load(std::memory_order_acquire);}
std::wstring neighbour_layout_file() {return layout_file();}
std::string neighbour_map_name(const std::string& mpr) {return map_name(mpr);}
bool neighbour_layout_origin(const std::string& mpr,double& x,double& y) noexcept try {
    x=y=0;
    const auto name=map_name(mpr);
    std::lock_guard<std::mutex> guard(active_layout_lock);
    if(name.empty()||name!=active_layout_name)return false;
    x=active_layout_x;y=active_layout_y;return true;
} catch(...) {x=y=0;return false;}
void neighbour_texture_resident(const std::string& key,bool resident) noexcept try {
    std::lock_guard<std::mutex> guard(resident_lock);
    if(resident)resident_textures.insert(key);else resident_textures.erase(key);
} catch(...) {}
void neighbour_textures_clear() noexcept {std::lock_guard<std::mutex> guard(resident_lock);resident_textures.clear();}
bool neighbour_texture_is_resident(const std::string& key) noexcept {
    std::lock_guard<std::mutex> guard(resident_lock);return resident_textures.count(key)!=0;
}
void neighbour_maps_set_d3d12(std::shared_ptr<D3D12Preparer> preparer) noexcept {
    std::lock_guard<std::mutex> guard(device_lock);stream_preparer()=std::move(preparer);
}
void neighbour_maps_set_device(ID3D11Device* device) noexcept {
    std::lock_guard<std::mutex> guard(device_lock);
    if(device)device->AddRef();
    if(stream_device)stream_device->Release();
    stream_device=device;
}
void neighbour_border_fog_publish(const NeighbourBorderFog& fog) noexcept {
    const double values[17]{fog.active.x0,fog.active.y0,fog.active.x1,fog.active.y1,
        fog.edges.x0,fog.edges.y0,fog.edges.x1,fog.edges.y1,fog.range,
        fog.contacts.lo[0],fog.contacts.lo[1],fog.contacts.lo[2],fog.contacts.lo[3],
        fog.contacts.hi[0],fog.contacts.hi[1],fog.contacts.hi[2],fog.contacts.hi[3]};
    for(int i=0;i<17;++i)fog_state[i].store(values[i],std::memory_order_relaxed);
    fog_revision.fetch_add(1,std::memory_order_release);
}
std::uint64_t neighbour_border_fog_revision() noexcept {
    return fog_revision.load(std::memory_order_acquire);
}
namespace {
std::mutex receiver_lock;
std::vector<NativeWorldBounds> receiver_cells;
}
void neighbour_receivers_publish(std::vector<NativeWorldBounds> cells) noexcept {
    std::lock_guard<std::mutex> guard(receiver_lock);receiver_cells.swap(cells);
}
void neighbour_receivers_append_visible(const std::array<float,16>& world_to_clip,
    std::vector<NativeWorldBounds>& receivers) noexcept {
    try {
        std::lock_guard<std::mutex> guard(receiver_lock);
        for(const auto& cell:receiver_cells)
            if(cell.valid && retained_world_bounds_visible(cell,world_to_clip))receivers.push_back(cell);
    } catch(...) {}
}
NeighbourBorderFog neighbour_border_fog() noexcept {
    double v[17];for(int i=0;i<17;++i)v[i]=fog_state[i].load(std::memory_order_relaxed);
    NeighbourContacts contacts;
    for(int side=0;side<4;++side){contacts.lo[side]=v[9+side];contacts.hi[side]=v[13+side];}
    return {{v[0],v[1],v[2],v[3]},{v[4],v[5],v[6],v[7]},contacts,v[8]};
}
void neighbour_maps_stop() noexcept {
    auto& s=stream();
    std::uint64_t id=0;
    s.cancel.store(true,std::memory_order_relaxed);
    {std::lock_guard<std::mutex> guard(s.lock);s.stop=true;s.ready.clear();s.in_upload=false;id=s.run;}
    s.wake.notify_all();
    if(s.worker.joinable()) {
        // Every wait in the stream wakes on stop; a decode or page build in
        // flight finishes first. Never wait without bound: a stream still
        // running after 5 s is left to finish detached (its id is retired
        // below, so it touches nothing shared).
        bool exited=false;
        {
            std::unique_lock<std::mutex> guard(s.lock);
            exited=s.wake.wait_for(guard,std::chrono::seconds(5),[&]{return s.exited>=id;});
        }
        if(exited)s.worker.join();
        else {s.worker.detach();OutputDebugStringA("EID3D11 neighbour maps: stream did not stop within 5 s; detached\n");}
    }
    {std::lock_guard<std::mutex> guard(s.lock);++s.run;s.epoch=0;s.held=false;s.error.clear();s.next_take=0;s.total=0;s.resident=0;}
}
void neighbour_maps_stop_epoch(std::uint64_t epoch) noexcept {
    auto& s=stream();
    {std::lock_guard<std::mutex> guard(s.lock);if(!epoch || s.epoch!=epoch)return;}
    neighbour_maps_stop();
}
// water_surf_no_sea_maps as the renderer last published it (neighbour_maps_set_no_sea_maps).
std::mutex g_no_sea_lock;std::string g_no_sea_maps="zone7";
void neighbour_maps_set_no_sea_maps(const std::string& maps) noexcept {
    try {std::lock_guard<std::mutex> guard(g_no_sea_lock);if(g_no_sea_maps!=maps)g_no_sea_maps=maps;} catch(...) {}
}
std::string neighbour_maps_no_sea_maps() {std::lock_guard<std::mutex> guard(g_no_sea_lock);return g_no_sea_maps;}
// The mounts of the current world (the stream's): the active statics worker
// reads archive textures by name from them.
std::mutex g_world_mounts_lock;std::shared_ptr<const ResourceStoreConfig> g_world_mounts;
std::shared_ptr<const ResourceStoreConfig> neighbour_world_mounts() noexcept {
    std::lock_guard<std::mutex> guard(g_world_mounts_lock);return g_world_mounts;
}
static void start_stream(const std::string& mpr,std::uint64_t epoch,const ResourceStoreConfig& mounts,
    NeighbourStreamSource source,bool restart) noexcept {
    try {auto copy=std::make_shared<const ResourceStoreConfig>(mounts);
        std::lock_guard<std::mutex> guard(g_world_mounts_lock);g_world_mounts=std::move(copy);} catch(...) {}
    auto& s=stream();
    std::string no_sea_maps;
    try {std::lock_guard<std::mutex> guard(g_no_sea_lock);no_sea_maps=g_no_sea_maps;} catch(...) {}
    {
        std::lock_guard<std::mutex> guard(s.lock);
        if(s.epoch==epoch) {
            if(!restart)return;
            // Nothing taken from it yet: it serves the coming world as it is.
            if(s.next_take==0 && !s.in_upload){s.held=true;s.wake.notify_all();return;}
        }
    }
    neighbour_maps_stop();if(!enabled.load(std::memory_order_acquire))return;
    std::uint64_t id=0;
    try {
        {std::lock_guard<std::mutex> guard(s.lock);s.epoch=epoch;s.held=restart;s.stop=false;s.finished=false;
            s.ready.clear();s.next_take=0;s.total=0;s.resident=0;s.in_upload=false;id=++s.run;}
        s.cancel.store(false,std::memory_order_relaxed);
        s.worker=std::thread(run,std::ref(s),mpr,epoch,mounts,id,no_sea_maps,std::move(source));
    } catch(...) {
        std::lock_guard<std::mutex> guard(s.lock);
        s.error="Neighbour map worker creation failed";s.finished=true;s.exited=(std::max)(s.exited,id);
    }
}
void neighbour_maps_begin(const std::string& mpr,std::uint64_t epoch,const ResourceStoreConfig& mounts,
    NeighbourStreamSource source) noexcept {
    start_stream(mpr,epoch,mounts,std::move(source),false);
}
void neighbour_maps_restart(const std::string& mpr,std::uint64_t epoch,const ResourceStoreConfig& mounts,
    NeighbourStreamSource source) noexcept {
    start_stream(mpr,epoch,mounts,std::move(source),true);
}
void neighbour_maps_world_changed(std::uint64_t epoch) noexcept {
    auto& s=stream();
    {
        std::lock_guard<std::mutex> guard(s.lock);
        if(s.held){s.held=false;return;} // restarted for this new world: keep it
        if(!epoch || s.epoch!=epoch)return;
    }
    neighbour_maps_stop();
}
std::unique_ptr<NeighbourPiece> neighbour_maps_take(std::uint64_t epoch) noexcept {
    auto& s=stream();std::lock_guard<std::mutex> guard(s.lock);
    if(s.epoch!=epoch || s.in_upload || s.held)return nullptr;
    // The next neighbour in layout order only; failed ones are skipped.
    for(auto slot=s.ready.find(s.next_take);slot!=s.ready.end();slot=s.ready.find(s.next_take)) {
        auto upload=std::move(slot->second);s.ready.erase(slot);++s.next_take;
        if(upload){s.in_upload=true;return upload;}
    }
    return nullptr;
}
void neighbour_maps_uploaded(std::uint64_t epoch) noexcept {
    auto& s=stream();
    {std::lock_guard<std::mutex> guard(s.lock);if(s.epoch==epoch && s.in_upload){s.in_upload=false;if(s.resident)--s.resident;}}
    s.wake.notify_all();
}
bool neighbour_maps_settled(std::uint64_t epoch) noexcept {
    auto& s=stream();std::lock_guard<std::mutex> guard(s.lock);
    return s.epoch!=epoch || (!s.held && s.finished && s.ready.empty() && !s.in_upload);
}
bool neighbour_maps_remaining(std::uint64_t epoch,std::size_t& remaining) noexcept {
    auto& s=stream();std::lock_guard<std::mutex> guard(s.lock);
    remaining=0;
    if(s.epoch!=epoch || (!s.held && s.finished && s.ready.empty() && !s.in_upload))return true;
    if(!s.total)return false; // layout not read yet
    remaining=(s.total>s.next_take?s.total-s.next_take:0)+(s.in_upload?1:0);
    if(!remaining)remaining=1; // taken, the stream not finished yet
    return true;
}
std::string neighbour_maps_error(std::uint64_t epoch) {
    auto& s=stream();std::lock_guard<std::mutex> guard(s.lock);return s.epoch==epoch?s.error:std::string{};
}

namespace {
struct ActiveStaticsJob {
    std::uint64_t generation = 0;
    std::vector<ActiveStaticsModelRequest> requests;
    std::mutex lock;
    std::unique_ptr<NeighbourPiece> piece;   // under lock
    std::vector<std::uint64_t> models;       // under lock: piece decoration object -> model key
    std::vector<std::uint64_t> failed_models;// under lock
    bool done = false, failed = false;       // under lock
    std::string failure;                     // under lock: step and candidate counts
};
std::mutex active_statics_lock;
std::shared_ptr<ActiveStaticsJob> active_statics_job;
void build_active_statics(const std::shared_ptr<ActiveStaticsJob>& job) noexcept {
    const BackgroundWorkerPriority priority;
    std::unique_ptr<NeighbourPiece> piece;
    std::string failure="exception";
    try {
        NeighbourMapUpload upload;upload.epoch=job->generation;
        // One candidate per requested model, from the live assembly the game
        // resolved (no load here): the candidate index is the model's piece
        // object (NeighbourPieceDecoration::object).
        std::deque<StaticObjectSource> sources;
        std::vector<std::uint64_t> models,failed_models;
        std::uint32_t surface_models=0,archive_classed=0,sway_classed=0;
        // A surface part's cut-out class from the archive texture of its
        // name (the content the game surface was made from), once per name.
        std::shared_ptr<const ResourceStoreConfig> mounts;
        {std::lock_guard<std::mutex> guard(g_world_mounts_lock);mounts=g_world_mounts;}
        ResourceStore store;bool store_open=false;
        if(mounts){ResourceFailure failure;store_open=store.open(*mounts,failure);}
        std::unordered_map<std::string,std::int8_t> archive_classes;
        const auto archive_cut_out=[&](const std::string& name)->std::int8_t {
            if(!store_open || name.empty())return -1;
            const auto found=archive_classes.find(name);
            if(found!=archive_classes.end())return found->second;
            ResourceBlob blob;ResourceFailure failure;TextureCpuAsset texture;
            std::int8_t value=-1;
            if(store.resolve(name+".mmp",blob,failure) && parse_texture_asset(blob,texture,failure))
                value=(texture.opaque_mips&1u)?0:1;
            archive_classes.emplace(name,value);
            return value;
        };
        for(const auto& request:job->requests) {
            sources.emplace_back();
            std::shared_ptr<RetainedStaticSourceCandidate> candidate;
            if(!prepare_live_static_candidate(request,sources.back(),static_cast<std::int32_t>(upload.decorations.size()),candidate))
                {failed_models.push_back(request.key);++upload.omitted_decorations;continue;}
            // No archive texture of that name: only a tree (its live class
            // sways) may be cut-out, by its surface's alpha; a building never.
            const bool tree=candidate->assembly && candidate->assembly->sways;
            for(auto& node:candidate->nodes) {
                if(!node.surface)continue;
                node.archive_cut_out=archive_cut_out(node.texture_name);
                if(node.archive_cut_out>=0){++archive_classed;continue;}
                node.archive_cut_out=tree && !node.surface->alpha_fully_opaque?1:0;++sway_classed;
            }
            if(!candidate->impostor_inputs_complete)++surface_models;
            models.push_back(request.key);upload.decorations.push_back(std::move(candidate));
        }
        upload.decoration_summary="models="+std::to_string(upload.decorations.size())+" refused="+
            std::to_string(failed_models.size())+" lod_only_surface_models="+std::to_string(surface_models)+
            " surface_parts_classed archive="+std::to_string(archive_classed)+" sway_rule="+std::to_string(sway_classed);
        {std::lock_guard<std::mutex> guard(job->lock);job->models=models;job->failed_models=failed_models;}
        NativePrebuiltBuilder builder;
        ID3D11Device* device=nullptr;
        {std::lock_guard<std::mutex> guard(device_lock);device=stream_device;if(device)device->AddRef();else builder.d3d12=stream_preparer();}
        builder.device=device;
        failure=upload.decorations.empty()?"no_candidates":(!device && !builder.d3d12)?"no_device":"prebuild";
        if(!upload.decorations.empty() && (device || builder.d3d12)) {
            prebuild_decorations(upload,builder,1.f);
            // No textures: every draw binds its posed record's texture, and
            // the impostor bake reads the archive textures those made
            // resident under the same cache keys.
            if(finish_native_prebuilt_pages(builder) && !builder.failed) {
                upload.prebuilt_pages=std::move(builder.pages);
                failure="piece";
                piece=make_neighbour_piece(upload,device,builder.d3d12.get());
            }
        }
        failure+=" "+upload.decoration_summary;
        if(device)device->Release();
    } catch(const std::exception& e) {piece.reset();try{failure+=std::string(": ")+e.what();}catch(...){}}
    catch(...) {piece.reset();}
    std::lock_guard<std::mutex> guard(job->lock);
    job->failed=!piece;job->piece=std::move(piece);job->done=true;job->failure=std::move(failure);
    job->requests.clear();
}
} // namespace
void active_statics_begin(std::uint64_t generation, std::vector<ActiveStaticsModelRequest> requests) noexcept try {
    auto job=std::make_shared<ActiveStaticsJob>();
    job->generation=generation;job->requests=std::move(requests);
    {std::lock_guard<std::mutex> guard(active_statics_lock);active_statics_job=job;}
    // A superseded build finishes into its own job, which nobody takes.
    std::thread([job]{build_active_statics(job);}).detach();
} catch(...) {}
std::unique_ptr<NeighbourPiece> active_statics_take(std::uint64_t generation, bool& failed, std::string& failure,
    std::vector<std::uint64_t>& models, std::vector<std::uint64_t>& failed_models) noexcept {
    failed=false;
    std::shared_ptr<ActiveStaticsJob> job;
    {std::lock_guard<std::mutex> guard(active_statics_lock);job=active_statics_job;}
    if(!job || job->generation!=generation)return nullptr;
    std::lock_guard<std::mutex> guard(job->lock);
    if(!job->done)return nullptr;
    failed=job->failed;job->failed=false;
    try{failure=job->failure;models.swap(job->models);failed_models.swap(job->failed_models);}catch(...){}
    {std::lock_guard<std::mutex> guard2(active_statics_lock);if(active_statics_job==job)active_statics_job.reset();}
    return std::move(job->piece);
}
void active_statics_stop() noexcept {
    std::lock_guard<std::mutex> guard(active_statics_lock);active_statics_job.reset();
}
}
