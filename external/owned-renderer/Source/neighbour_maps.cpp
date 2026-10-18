#include "neighbour_maps.h"
#ifdef EID3D11_EMBEDDED
#include "embedded_host.h"
#endif
#include "owned_resource_runtime.h"
#include "owned_terrain_front_end.h"
#include "retained_static_scene.h"
#include "worker_priority.h"
#include <windows.h>
#include <chrono>
#include <unordered_set>
#include <atomic>
#include <condition_variable>
#include <fstream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace owned {
namespace {
std::atomic<bool> enabled{false}; // default awaits the director's GPU-budget route
std::atomic<double> fog_state[17]{};
std::atomic<std::uint64_t> fog_revision{0};
std::mutex device_lock;
// A raw reference, released only by neighbour_maps_set_device: a static
// COM pointer would Release the device from the DLL's static destructors at
// process exit, after the threads that may hold D3D locks were killed.
ID3D11Device* stream_device = nullptr;
std::mutex resident_lock;
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
    std::uint64_t epoch = 0;
    // Each started stream has its own id; a stream whose id is no longer
    // current is stopped and never touches the shared state again, so a
    // stream that does not exit in time can be left to finish detached.
    std::uint64_t run = 0, exited = 0;
    // Layout-order slots: a decoded upload, or empty after a failed decode.
    std::map<std::size_t, std::unique_ptr<NeighbourMapUpload>> ready;
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
bool profile_matches(const ResourceStoreConfig& mounts) {
    for(const auto& path:mounts.archives) {
        const auto slash=path.find_last_of(L"/\\");
        auto name=path.substr(slash==std::wstring::npos?0:slash+1);
        for(auto& c:name)if(c>=L'A'&&c<=L'Z')c+=32;
        if(name==L"databasejmv.res")return true;
    }
    // Dejavu ships databaseJmv.res in the addon's res folder without listing
    // it among the visual archives.
    const auto layout=layout_file();
    const auto database=layout.substr(0,layout.find_last_of(L"/\\")+1)+L"res\\databaseJmv.res";
    const auto attributes=GetFileAttributesW(database.c_str());
    return attributes!=INVALID_FILE_ATTRIBUTES&&!(attributes&FILE_ATTRIBUTE_DIRECTORY);
}
void set_error(Stream& s,const std::string& error) {
    std::lock_guard<std::mutex> guard(s.lock);s.error=error;
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
    // Which models/textures failed to load (issue #136 gaps).
    if(!map->failures.empty()) {
        upload->decoration_summary+=" load_failures="+std::to_string(map->failures.size());
        for(std::size_t i=0;i<map->failures.size()&&i<3;++i)upload->decoration_summary+=(i?",":":")+map->failures[i];
    }
    return upload;
}
// After the weld: decode every terrain command and decoration node into
// dedicated geometry pages on this worker, so the render thread only adopts
// them. A failed page drops the whole prebuild (the render thread then decodes
// as before); a mesh the builder refuses decodes at upload.
void prebuild_neighbour(NeighbourMapUpload& upload) noexcept {
    NativePrebuiltBuilder builder;
    {std::lock_guard<std::mutex> guard(device_lock);builder.device=stream_device;}
    if(!builder.device)return;
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
        // Static LOD levels once per figure; every shape of it shares them.
        std::map<const FigureCpuAsset*,NativeMeshLods> lods;
        const auto lod_start=std::chrono::steady_clock::now();
        double lod_seconds=0;
        for(std::size_t i=0;i<upload.decorations.size() && !builder.failed;++i) {
            const auto& candidate=*upload.decorations[i];
            for(const auto& node:candidate.nodes) {
                if(builder.failed)break;
                // A cut-out figure (its texture has transparency) gets its
                // impostor bake input once per texture.
                if(node.texture&&(node.texture->opaque_mips&1u)==0) {
                    const auto found=upload.impostor_sources.try_emplace({node.figure,node.texture_identity});
                    if(found.second) {
                        found.first->second.node=&node;
                        build_impostor_source(*node.figure,found.first->second.source); // empty: not bakeable
                    }
                }
                const auto inserted=upload.prebuilt_figures.try_emplace(neighbour_figure_shape(node.figure,candidate.source->shape));
                if(!inserted.second)continue;
                auto level=lods.find(node.figure);
                if(level==lods.end()) {
                    const auto begin=std::chrono::steady_clock::now();
                    level=lods.emplace(node.figure,NativeMeshLods{}).first;
                    ++upload.lod_stats.figures;upload.lod_stats.full_triangles+=node.figure->indices.size()/3;
                    if(build_static_lods(*node.figure,level->second)) {
                        ++upload.lod_stats.reduced_figures;
                        for(std::uint32_t k=0;k<level->second.levels;++k)upload.lod_stats.level_triangles[k]+=level->second.indices[k].size()/3;
                    }
                    lod_seconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
                }
                auto view=neighbour_figure_view(*node.figure,node.positions.data(),candidate.source->shape);
                view.lods=&level->second;
                if(!build_native_prebuilt_mesh(builder,view,inserted.first->second))inserted.first->second=NativePrebuiltMesh{};
            }
        }
        upload.lod_stats.milliseconds=lod_seconds*1000.0;
        upload.lod_stats.prebuild_milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-lod_start).count();
        if(!finish_native_prebuilt_pages(builder) || builder.failed){drop();return;}
        upload.prebuilt_pages=std::move(builder.pages);
        // Its atlas pages and decoration textures, once each, unless resident.
        std::unordered_set<std::string> seen;
        const auto texture=[&](const NativeOwnedTextureView& view) {
            if(!view.resource)return;
            const auto key=native_owned_archive_texture_key(*view.resource,view.generation,false);
            if(!seen.insert(key).second || neighbour_texture_is_resident(key))return;
            NativePrebuiltTexture made;
            if(build_native_prebuilt_texture(builder.device.Get(),view,made))upload.prebuilt_textures.push_back(std::move(made));
        };
        const auto& map_terrain=*upload.map->terrain;
        for(const auto& page:map_terrain.pages) {
            if(!page.texture || page.mips.empty())continue;
            NativeOwnedTextureView view{};
            view.identity=page.texture->identity.container_incarnation;view.generation=1;
            view.resource=&page.texture->identity;view.value_source=1;
            view.format=static_cast<NativeOwnedTextureFormat>(page.texture->format);
            view.mips=page.mips.data();view.mip_count=static_cast<std::uint32_t>(page.mips.size());
            texture(view);
        }
        for(const auto& candidate:upload.decorations)for(const auto& node:candidate->nodes) {
            if(!node.texture || node.texture_mips.empty())continue;
            NativeOwnedTextureView view{};
            view.identity=node.texture_identity;view.generation=1;view.resource=&node.texture->identity;
            view.value_source=1;view.format=node.texture_format;
            view.mips=node.texture_mips.data();view.mip_count=static_cast<std::uint32_t>(node.texture_mips.size());
            texture(view);
        }
    } catch(...) {drop();}
}
std::string read_layout(const std::wstring& file) {
    std::ifstream input(file,std::ios::binary|std::ios::ate);
    const auto bytes=input.tellg();
    if(!input || bytes<0 || bytes>4u*1024u*1024u)throw std::runtime_error("Invalid neighbour layout file size");
    std::string json(std::size_t(bytes),'\0');input.seekg(0);
    if(!input.read(json.data(),std::streamsize(json.size())))throw std::runtime_error("Cannot read neighbour layout");
    return json;
}
void run(Stream& s,std::string active,std::uint64_t epoch,ResourceStoreConfig mounts,std::uint64_t id) {
    const BackgroundWorkerPriority priority;
    // Under s.lock: this stream was stopped or replaced.
    const auto stopped=[&s,id] {return s.stop || s.run!=id;};
    const auto set_error=[&s,id](Stream&,const std::string& error) {
        std::lock_guard<std::mutex> guard(s.lock);if(s.run==id)s.error=error;
    };
    try {
        if(!profile_matches(mounts))throw std::runtime_error("No verified neighbour resource profile");
        const auto file=layout_file(); const auto root=file.substr(0,file.find_last_of(L"/\\")+1);
        const auto neighbours=select_neighbour_layout(parse_neighbour_layout(read_layout(file)),"deja-vu",map_name(active));
        {std::lock_guard<std::mutex> guard(s.lock);if(s.run==id)s.total=neighbours.size();}
        std::size_t next_claim=0; // guarded by s.lock
        // Ordered weld: the decoder holding neighbour `next_weld` welds it
        // against the active map's edge strips and every earlier welded
        // neighbour's, exactly as the render thread did at upload, then
        // appends its own strips. Only the turn holder touches `strips`.
        std::size_t next_weld=0;               // guarded by s.lock
        std::vector<NeighbourEdgeStrip> strips; // turn holder only
        std::shared_ptr<const TerrainCpuAsset> active_terrain;
        const auto weld=[&](NeighbourMapUpload& upload) {
            if(!active_terrain) {
                // The active map decodes in parallel inside the game's load;
                // its terrain is published before the load returns.
                const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
                for(;;) {
                    const auto source=retained_static_terrain_source_snapshot();
                    if(source.map && source.terrain && source.map->generation==epoch){active_terrain=source.terrain;break;}
                    {std::lock_guard<std::mutex> guard(s.lock);if(stopped())return false;}
                    if(std::chrono::steady_clock::now()>deadline)
                        throw std::runtime_error("active map terrain was not published");
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                strips=neighbour_edge_strips(*active_terrain,0,0);
            }
            // Freshly decoded and not yet shared: nothing else reads it.
            auto& terrain=const_cast<TerrainCpuAsset&>(*upload.map->terrain);
            const double x=upload.selection.x,y=upload.selection.y;
            neighbour_weld_edge_heights(terrain,x,y,strips);
            neighbour_weld_edge_normals(terrain,x,y,strips);
            // Its liquid flow reads the active map's liquid across the shared
            // edge (whole-metre offsets only: the grids then share vertices).
            if(x==std::floor(x)&&y==std::floor(y))
                owned_terrain_set_outside_liquid(terrain,int(x),int(y),*active_terrain);
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
                    s.wake.wait(guard,[&]{return stopped() || next_claim>=neighbours.size() || s.resident<kDecoders;});
                    if(stopped() || next_claim>=neighbours.size())break;
                    index=next_claim++;++s.resident;
                }
                std::unique_ptr<NeighbourMapUpload> upload;
                try {upload=decode_neighbour(loader,root,neighbours,index,epoch,mounts);}
                catch(const std::exception& e) {set_error(s,neighbours[index].placement.zone+": "+e.what());}
                catch(...) {set_error(s,neighbours[index].placement.zone+": neighbour decode allocation failed");}
                {
                    std::unique_lock<std::mutex> guard(s.lock);
                    s.wake.wait(guard,[&]{return stopped() || next_weld==index;});
                }
                try {if(upload && !weld(*upload))upload.reset();}
                catch(const std::exception& e) {upload.reset();set_error(s,neighbours[index].placement.zone+": "+e.what());}
                catch(...) {upload.reset();set_error(s,neighbours[index].placement.zone+": neighbour weld allocation failed");}
                {
                    // The next neighbour may weld while this one builds its pages.
                    std::lock_guard<std::mutex> guard(s.lock);
                    ++next_weld;
                }
                s.wake.notify_all();
                if(upload) {
                    prebuild_neighbour(*upload);
                    upload->ground_cover_inputs=prepare_ground_cover_neighbour_inputs(*upload);
                }
                {
                    std::lock_guard<std::mutex> guard(s.lock);
                    if(stopped())break; // a new stream reset the resident count
                    if(!upload)--s.resident; // only a delivered map stays resident
                    s.ready[index]=std::move(upload);
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
        for(std::uint32_t k=0;k<edge.count;++k) {
            bool first=true;
            each_base_copy(terrain,edge.gx0+k*edge.dx,edge.gy0+k*edge.dy,[&](const TerrainVertex& v) {
                if(first){strip.normals[k]={v.normal_x,v.normal_y,v.normal_z};strip.positions[k]={float(v.x+x),float(v.y+y),v.z};}first=false;
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
void neighbour_maps_enable(bool value) noexcept {enabled.store(value,std::memory_order_release);}
bool neighbour_layout_origin(const std::string& mpr,const ResourceStoreConfig& mounts,double& x,double& y) noexcept try {
    x=y=0;
    if(!profile_matches(mounts))return false;
    const auto& active=neighbour_layout_active(parse_neighbour_layout(read_layout(layout_file())),"deja-vu",map_name(mpr));
    if(!std::isfinite(active.x)||!std::isfinite(active.y))return false;
    x=active.x;y=active.y;return true;
} catch(...) {x=y=0;return false;}
void neighbour_texture_resident(const std::string& key,bool resident) noexcept try {
    std::lock_guard<std::mutex> guard(resident_lock);
    if(resident)resident_textures.insert(key);else resident_textures.erase(key);
} catch(...) {}
void neighbour_textures_clear() noexcept {std::lock_guard<std::mutex> guard(resident_lock);resident_textures.clear();}
bool neighbour_texture_is_resident(const std::string& key) noexcept {
    std::lock_guard<std::mutex> guard(resident_lock);return resident_textures.count(key)!=0;
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
    {std::lock_guard<std::mutex> guard(s.lock);++s.run;s.epoch=0;s.error.clear();s.next_take=0;s.total=0;s.resident=0;}
}
void neighbour_maps_stop_epoch(std::uint64_t epoch) noexcept {
    auto& s=stream();
    {std::lock_guard<std::mutex> guard(s.lock);if(!epoch || s.epoch!=epoch)return;}
    neighbour_maps_stop();
}
void neighbour_maps_begin(const std::string& mpr,std::uint64_t epoch,const ResourceStoreConfig& mounts) noexcept {
    auto& s=stream();
    {std::lock_guard<std::mutex> guard(s.lock);if(s.epoch==epoch)return;}
    neighbour_maps_stop();if(!enabled.load(std::memory_order_acquire))return;
    std::uint64_t id=0;
    try {
        {std::lock_guard<std::mutex> guard(s.lock);s.epoch=epoch;s.stop=false;s.finished=false;
            s.ready.clear();s.next_take=0;s.total=0;s.resident=0;s.in_upload=false;id=++s.run;}
        s.worker=std::thread(run,std::ref(s),mpr,epoch,mounts,id);
    } catch(...) {
        std::lock_guard<std::mutex> guard(s.lock);
        s.error="Neighbour map worker creation failed";s.finished=true;s.exited=(std::max)(s.exited,id);
    }
}
std::unique_ptr<NeighbourMapUpload> neighbour_maps_take(std::uint64_t epoch) noexcept {
    auto& s=stream();std::lock_guard<std::mutex> guard(s.lock);
    if(s.epoch!=epoch || s.in_upload)return nullptr;
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
    return s.epoch!=epoch || (s.finished && s.ready.empty() && !s.in_upload);
}
bool neighbour_maps_remaining(std::uint64_t epoch,std::size_t& remaining) noexcept {
    auto& s=stream();std::lock_guard<std::mutex> guard(s.lock);
    remaining=0;
    if(s.epoch!=epoch || (s.finished && s.ready.empty() && !s.in_upload))return true;
    if(!s.total)return false; // layout not read yet
    remaining=(s.total>s.next_take?s.total-s.next_take:0)+(s.in_upload?1:0);
    if(!remaining)remaining=1; // taken, the stream not finished yet
    return true;
}
std::string neighbour_maps_error(std::uint64_t epoch) {
    auto& s=stream();std::lock_guard<std::mutex> guard(s.lock);return s.epoch==epoch?s.error:std::string{};
}
}
