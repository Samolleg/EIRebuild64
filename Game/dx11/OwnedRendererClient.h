#pragma once
#include "../../../owned-renderer-c40-rebuild/Include/ei_renderer_api.h"
#include "RenderJournalState.h"
#include <SpriteBatch.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <map>
#include <array>
#include <unordered_map>

// Source client: the renderer module remains loaded until process exit. This
// also keeps outstanding COM vtables and the proxy's CRT handlers valid.
class OwnedRendererClient {
    HMODULE module = nullptr;
    EIRenderApiV5 terrainApi = {};
    EIRenderMeshReportApiV1 meshReportApi = {};
    std::string figureModel,figurePart;
    std::unordered_map<uint64_t,std::string> meshFailures;
    EIRenderApiV4& figureApi = terrainApi.base;
    EIRenderApiV3& sourceApi = figureApi.base;
    uint64_t figureScene = 0, nextFigureIdentity = 1;
    uint64_t figureFrame = 0, geometryIdentity = 0, geometryRevision = 0;
    bool figureScopeActive = false;
    EIRenderFigureV4 figureScope = {};
    uint64_t figureIdentity = 0;
    struct PartEntry {uint64_t identity, lastUsed;};
    std::map<std::pair<uint64_t,std::string>,PartEntry> figureIdentities;
    struct MeshEntry {uint64_t handle, revision, lastUsed;};
    std::unordered_map<uint64_t,MeshEntry> figureMeshes;
    struct TextureKey {
        ID3D11ShaderResourceView* image;uint32_t role;
        bool operator==(const TextureKey& b)const noexcept{return image==b.image&&role==b.role;}
    };
    struct TextureHash {size_t operator()(const TextureKey& k)const noexcept {
        return std::hash<uintptr_t>{}(reinterpret_cast<uintptr_t>(k.image))^std::hash<uint32_t>{}(k.role);
    }};
    struct TextureEntry {
        uint64_t handle, identity, lastUsed;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> image;
    };
    std::unordered_map<TextureKey,TextureEntry,TextureHash> figureTextures;
    uint64_t nextTextureIdentity = 1;
    void CollectFigureResources() {
        // After EndWorld the synchronous queue has drained. Unused resources
        // survive 120 frames; sweep every 30, so the maximum idle age is 149.
        if(figureFrame%30)return;
        for(auto i=figureMeshes.begin();i!=figureMeshes.end();) {
            if(figureFrame-i->second.lastUsed<120){++i;continue;}
            const HRESULT hr=figureApi.release_mesh(host,i->second.handle);
            if(FAILED(hr)&&hr!=E_HANDLE)throw hr;
            i=figureMeshes.erase(i);
        }
        for(auto i=figureTextures.begin();i!=figureTextures.end();) {
            if(figureFrame-i->second.lastUsed<120){++i;continue;}
            const HRESULT hr=figureApi.release_texture(host,i->second.handle);
            if(FAILED(hr)&&hr!=E_HANDLE)throw hr;
            i=figureTextures.erase(i);
        }
        for(auto i=figureIdentities.begin();i!=figureIdentities.end();)
            if(figureFrame-i->second.lastUsed>=120)i=figureIdentities.erase(i);else ++i;
    }
    struct PacketCounts {
        uint64_t accepted = 0, rejected = 0, legacy = 0;
        std::map<std::string,uint64_t> reasons;
        bool operator==(const PacketCounts& b) const {return accepted==b.accepted&&rejected==b.rejected&&legacy==b.legacy&&reasons==b.reasons;}
    };
    std::array<PacketCounts,4> packets{}, previousPackets{}, packetTotals{};
    bool packetHistory = false;
    std::array<ULONGLONG,4> packetLastMs{};
    std::array<bool,4> packetKnown{};
    static bool SameReasonSet(const PacketCounts& a,const PacketCounts& b) {
        if(a.reasons.size()!=b.reasons.size())return false;
        auto x=a.reasons.begin(),y=b.reasons.begin();
        for(;x!=a.reasons.end();++x,++y)if(x->first!=y->first)return false;
        return true;
    }
    void PacketLine(const char* event,unsigned i,const PacketCounts& p) {
        const char* names[]={"static","characters","equipment","terrain"};
        std::string reasons;
        for(const auto& reason:p.reasons) {
            if(!reasons.empty())reasons+=",";
            reasons+=reason.first+":"+std::to_string(reason.second);
        }
        GameJournal::Event("RENDER",event,"scene=%llu category=%s accepted=%llu rejected=%llu legacy=%llu reason=%s",
            figureScene,names[i],p.accepted,p.rejected,p.legacy,reasons.empty()?"none":reasons.c_str());
    }
    void FinishPackets() {
        if(!packetHistory)return;
        for(unsigned i=0;i<4;++i)PacketLine("packets_summary",i,packetTotals[i]);
        packetTotals={};packetKnown={};packetLastMs={};packetHistory=false;
    }
    void PublishPackets(ULONGLONG now=GetTickCount64()) {
        for(unsigned i=0;i<4;++i) {
            const auto& p=packets[i];auto& total=packetTotals[i];
            total.accepted+=p.accepted;total.rejected+=p.rejected;total.legacy+=p.legacy;
            for(const auto& reason:p.reasons)total.reasons[reason.first]+=reason.second;
            // Counts fluctuate each frame; only a changed set of reasons is
            // actionable. Delayed changes are compared with the last log.
            if((!packetKnown[i]||!SameReasonSet(p,previousPackets[i]))&&
               (!packetKnown[i]||now-packetLastMs[i]>=5000)) {
                PacketLine("packets",i,p);previousPackets[i]=p;
                packetLastMs[i]=now;packetKnown[i]=true;
            }
        }
        packetHistory=true;
    }
    EIRenderApiV2& worldApi = sourceApi.base;
    EIRenderApiV1& api = worldApi.base;
    EIRenderHost* host = nullptr;
    EIRenderStatusApiV2 statusApi = {};
    RenderJournalState journalState;
    DWORD ownerThread = 0;
    HRESULT previousStatusError = S_OK;
    void JournalRendererState() noexcept {
        // Loading-video workers use API1 only; they do not read F10/world state.
        if (!host || GetCurrentThreadId() != ownerThread) return;
        EIRenderStatusV2 status = {};
        status.size = sizeof(status); status.version = EI_RENDERER_STATUS_VERSION_V2;
        HRESULT hr = statusApi.get_status_v2(host, &status);
        if (SUCCEEDED(hr) && !RenderJournalState::Valid(status)) hr = E_UNEXPECTED;
        if (FAILED(hr)) {
            if (hr != previousStatusError)
                GameJournal::Event("RENDER", "status", "reason=query_failed hr=%08lx", static_cast<unsigned long>(hr));
            previousStatusError = hr;
            return;
        }
        if (previousStatusError != S_OK)
            GameJournal::Event("RENDER", "status", "reason=query_recovered");
        previousStatusError = S_OK;
        journalState.Publish(status);
    }
public:
    OwnedRendererClient() = default;
    OwnedRendererClient(const OwnedRendererClient&) = delete;
    OwnedRendererClient& operator=(const OwnedRendererClient&) = delete;
    ~OwnedRendererClient() { Shutdown(); }
    void Shutdown() noexcept {
        FinishPackets();
        // Release the client's SRV leases before destroying the host/device.
        figureTextures.clear();figureMeshes.clear();figureIdentities.clear();meshFailures.clear();
        if(host){api.destroy_host(host);host=nullptr;}
        figureScene=0;figureScopeActive=false;
    }

    HRESULT Initialize(HWND window, unsigned width, unsigned height) {
        wchar_t executable[32768] = {};
        const DWORD count = GetModuleFileNameW(nullptr, executable, _countof(executable));
        if (!count || count >= _countof(executable)) return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
        std::wstring path(executable, count);
        const auto slash = path.find_last_of(L"\\/");
        if (slash == std::wstring::npos) return HRESULT_FROM_WIN32(ERROR_BAD_PATHNAME);
        path.resize(slash + 1);
        path += L"RDDraw.dll";
        module = LoadLibraryExW(path.c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!module) return HRESULT_FROM_WIN32(GetLastError());
        const auto query = reinterpret_cast<EIRenderQueryApiFn>(GetProcAddress(module, "EI_Renderer_QueryApi"));
        if (!query) return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
        HRESULT result = query(EI_RENDERER_TERRAIN_API_VERSION, sizeof(terrainApi), &terrainApi);
        const uint64_t required = EI_RENDERER_CAP_HOST_DEVICE | EI_RENDERER_CAP_SOURCE_UI |
                                  EI_RENDERER_CAP_D3D11_TRANSITION | EI_RENDERER_CAP_DISPLAY_SETTINGS | EI_RENDERER_CAP_SOURCE_WORLD | EI_RENDERER_CAP_SOURCE_INPUTS | EI_RENDERER_CAP_SOURCE_FIGURES | EI_RENDERER_CAP_SOURCE_REVISIONS | EI_RENDERER_CAP_SOURCE_TERRAIN;
        if (FAILED(result)) return result;
        if (api.size != sizeof(terrainApi) || api.version != EI_RENDERER_TERRAIN_API_VERSION ||
            (api.capabilities & required) != required || !api.create_host || !api.destroy_host ||
            !api.acquire_d3d11_bridge || !api.begin_frame || !api.draw_ui || !api.present ||
            !worldApi.begin_world || !worldApi.end_world || !worldApi.record_draw || !sourceApi.set_terrain ||
            !sourceApi.set_scene || !sourceApi.terrain_drawn || !sourceApi.bind_world_pixels ||
            !figureApi.register_mesh || !figureApi.release_mesh || !figureApi.register_texture ||
            !figureApi.release_texture || !figureApi.submit_figure || !figureApi.release_instance ||
            !terrainApi.set_terrain_source || !terrainApi.submit_terrain || !terrainApi.release_terrain)
            return E_NOINTERFACE;
        result = query(EI_RENDERER_STATUS_API_VERSION, sizeof(statusApi), &statusApi);
        if (FAILED(result)) return result;
        if (statusApi.base.size != sizeof(statusApi) || statusApi.base.version != EI_RENDERER_STATUS_API_VERSION ||
            !statusApi.get_status_v2) return E_NOINTERFACE;
        result=query(EI_RENDERER_MESH_REPORT_API_VERSION,sizeof(meshReportApi),&meshReportApi);
        if(FAILED(result)||!meshReportApi.register_mesh_report)return E_NOINTERFACE;
        const EIRenderHostDescV1 desc = {sizeof(desc), window, width, height};
        result = api.create_host(&desc, &host);
        if (SUCCEEDED(result)) { ownerThread = GetCurrentThreadId(); JournalRendererState(); }
        return result;
    }

    void BeginFigure(const EIRenderFigureV4& input,const char* name,const char* model=nullptr) {
        figurePart=name?name:"";figureModel=model?model:"unknown";
        figureScope=input;figureScope.scene_generation=figureScene;
        const auto key=std::make_pair(input.object_id,std::string(name?name:""));
        auto found=figureIdentities.find(key);
        if(found==figureIdentities.end())found=figureIdentities.emplace(key,PartEntry{nextFigureIdentity++,figureFrame}).first;
        found->second.lastUsed=figureFrame;
        figureIdentity=found->second.identity;figureScope.part=uint32_t(figureIdentity);
        geometryIdentity=geometryRevision=0;
        figureScopeActive=figureScene!=0;
    }
    void SelectFigureGeometry(uint64_t identity,uint64_t revision) noexcept {
        geometryIdentity=identity;geometryRevision=revision;
    }
    void EndFigure() noexcept {figureScopeActive=false;}
    bool FigureActive() const noexcept {return figureScopeActive;}
    EIRenderFigureV4 FigureValues() const {return figureScope;}
    void FigureFallback(const char* reason) {
        if(!figureScopeActive)return;
        auto& count=packets[figureScope.category];++count.rejected;++count.legacy;
        ++count.reasons[reason];
    }
    bool SubmitFigure(EIRenderMeshV4 mesh,EIRenderFigureV4 packet) {
        if(!figureScopeActive)return false;
        // A raw/CPU-deformed caller without source identity/revision stays on
        // the legacy path. Never assume its scratch buffers are immutable.
        if(!geometryIdentity||!geometryRevision){FigureFallback("unversioned_geometry");return false;}
        // Known unsupported layers must not allocate unused registrations.
        if(packet.secondary){FigureFallback("secondary_layer_not_supported");return false;}
        mesh.scene_generation=figureScene;mesh.identity=geometryIdentity;mesh.source_revision=geometryRevision;
        auto found=figureMeshes.find(geometryIdentity);
        HRESULT hr=S_OK;
        if(found==figureMeshes.end()||found->second.revision!=geometryRevision) {
            uint64_t handle=0;EIRenderMeshReportV1 report{sizeof(report),1,{}};
            // The original registration entry remains compatible; Initialize
            // queries the independent table for precise validator failures.
            hr=meshReportApi.register_mesh_report?meshReportApi.register_mesh_report(host,&mesh,&handle,&report):figureApi.register_mesh(host,&mesh,&handle);
            if(FAILED(hr)) {
                const std::string reason=report.reason[0]?report.reason:"mesh_registration_failed";
                const std::string key=std::to_string(geometryRevision)+":"+std::to_string(hr)+":"+reason;
                auto failed=meshFailures.find(geometryIdentity);
                if(failed==meshFailures.end()||failed->second!=key) {
                    meshFailures[geometryIdentity]=key;
                    GameJournal::Event("RENDER","mesh_rejected",
                        "object=%llu model=%s part_name=%s part=%u scene=%llu geometry=%llu revision=%llu hr=%08lx positions=%u normals=%u uv=%u components=%u indices=%u reason=%s",
                        figureScope.object_id,figureModel.c_str(),figurePart.c_str(),figureScope.part,figureScene,
                        geometryIdentity,geometryRevision,static_cast<unsigned long>(hr),mesh.position_groups,mesh.normal_groups,mesh.uv_count,mesh.component_count,mesh.index_count,reason.c_str());
                }
                FigureFallback(reason.c_str());return false;
            }
            meshFailures.erase(geometryIdentity);
            found=figureMeshes.insert_or_assign(geometryIdentity,MeshEntry{handle,geometryRevision,figureFrame}).first;
        }
        found->second.lastUsed=figureFrame;
        packet.mesh=found->second.handle;packet.scene_generation=figureScene;
        if(packet.primary) {
            const TextureKey key{packet.primary,figureScope.texture_role};
            auto image=figureTextures.find(key);
            if(image==figureTextures.end()) {
                // Hold this shared image in the EXE until eviction, preventing
                // address reuse. The DLL still borrows SRV only during submit.
                EIRenderTextureV4 texture={sizeof(texture),4,figureScene,nextTextureIdentity++,1,figureScope.texture_role};
                uint64_t handle=0;hr=figureApi.register_texture(host,&texture,&handle);
                if(FAILED(hr)){FigureFallback("texture_registration_failed");return false;}
                TextureEntry entry{handle,texture.identity,figureFrame,{}};entry.image=packet.primary;
                image=figureTextures.emplace(key,std::move(entry)).first;
            }
            image->second.lastUsed=figureFrame;
            packet.texture=image->second.handle;packet.texture_generation=1;
        }
        EIRenderFigureResultV4 result={sizeof(result),4,0,{}};
        hr=figureApi.submit_figure(host,&packet,&result);
        if(result.accepted) {
            ++packets[figureScope.category].accepted;
            // A device failure after ownership transfers must terminate this
            // frame instead of replaying its geometry through the old path.
            if(FAILED(hr))throw hr;
            return true;
        }
        FigureFallback(result.reason[0]?result.reason:"figure_packet_invalid");return false;
    }

    HRESULT AcquireBridge(EIRenderD3D11BridgeV1& bridge) {
        bridge = {};
        bridge.size = sizeof(bridge);
        return api.acquire_d3d11_bridge(host, &bridge);
    }
    HRESULT BeginFrame(const float* color, float depth) { return api.begin_frame(host, color, depth); }
    HRESULT Present() { const HRESULT hr = api.present(host); JournalRendererState(); return hr; }
    HRESULT BeginWorld(const EIRenderWorldFrameV2& frame, uint32_t& skyConsumed) {
        const HRESULT hr=worldApi.begin_world(host, &frame, &skyConsumed);
        if(SUCCEEDED(hr)) {
            if(figureScene!=frame.world_generation){FinishPackets();figureIdentities.clear();figureMeshes.clear();figureTextures.clear();meshFailures.clear();figureFrame=0;packetHistory=false;}
            ++figureFrame;
            figureScene=frame.world_generation;packets={};figureScopeActive=false;
        }
        return hr;
    }
    HRESULT EndWorld() { const HRESULT hr = worldApi.end_world(host); if(SUCCEEDED(hr))CollectFigureResources(); PublishPackets(); JournalRendererState(); return hr; }
    HRESULT SetTerrain(const EIRenderTerrainSourceV5& input){return terrainApi.set_terrain_source(host,&input);}
    void ReleaseTerrain() {if(host){const HRESULT hr=terrainApi.release_terrain(host);if(FAILED(hr))throw hr;}}
    void TerrainFallback(const char* reason) {
        auto& counts=packets[3];++counts.rejected;++counts.legacy;++counts.reasons[reason];
    }
    bool SubmitTerrain(EIRenderTerrainFrameV5 input) {
        input.size=sizeof(input);input.version=5;input.scene_generation=figureScene;
        EIRenderTerrainResultV5 result{sizeof(result),5,0,0,{}};
        const HRESULT hr=terrainApi.submit_terrain(host,&input,&result);
        if(FAILED(hr))throw hr;
        auto& counts=packets[3];
        if(result.accepted){++counts.accepted;return true;}
        ++counts.rejected;++counts.legacy;++counts.reasons[result.reason];return false;
    }
    HRESULT SetScene(const EIRenderSceneV3& input){return sourceApi.set_scene(host,&input);}
    HRESULT TerrainDrawn(){const HRESULT hr = sourceApi.terrain_drawn(host); JournalRendererState(); return hr;}
    HRESULT BindWorldPixels(bool fog){return sourceApi.bind_world_pixels(host,fog?1u:0u);}
    HRESULT RecordDraw(uint32_t triangles) { return worldApi.record_draw(host, triangles); }
    HRESULT DrawUi(ID3D11ShaderResourceView* image, const RECT& destination,
                   const RECT* source, unsigned flags, COLORREF key = 0) {
        EIRenderUiBlitV1 request = {};
        request.size = sizeof(request);
        request.image = image;
        request.destination = destination;
        if (source) { request.source = *source; request.has_source = 1; }
        request.flags = flags;
        request.color_key = key;
        return api.draw_ui(host, &request);
    }
};

// Preserve existing Begin/End ordering during the UI migration. Deferred draws
// retain their image until End; immediate draws interleave with old world GPU
// consumers. Both modes submit to the DLL's UI pipeline, not DXTK SpriteBatch.
class OwnedSpriteBatch {
    struct DrawCommand {
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> image;
        RECT destination, source;
        bool has_source;
        unsigned flags;
    };
    OwnedRendererClient& renderer;
    std::vector<DrawCommand> pending;
    bool immediate = false, active = false;
    unsigned flags = 0;
public:
    explicit OwnedSpriteBatch(OwnedRendererClient& client) : renderer(client) {}
    void Begin(DirectX::SpriteSortMode mode = DirectX::SpriteSortMode_Deferred,
               ID3D11BlendState* blend = nullptr) {
        if (active) throw E_UNEXPECTED;
        flags = 0;
        if (blend) {
            D3D11_BLEND_DESC desc = {};
            blend->GetDesc(&desc);
            if (!desc.RenderTarget[0].BlendEnable) flags |= EI_RENDERER_UI_OPAQUE;
            else if (desc.RenderTarget[0].SrcBlend == D3D11_BLEND_SRC_ALPHA)
                flags |= EI_RENDERER_UI_STRAIGHT_ALPHA;
        }
        immediate = mode == DirectX::SpriteSortMode_Immediate;
        active = true;
    }
    void Draw(ID3D11ShaderResourceView* image, const RECT& destination, const RECT* source = nullptr) {
        if (!active) throw E_UNEXPECTED;
        if (immediate) {
            const HRESULT result = renderer.DrawUi(image, destination, source, flags);
            if (FAILED(result)) throw result;
        } else {
            DrawCommand command = {};
            command.image = image;
            command.destination = destination;
            if (source) { command.source = *source; command.has_source = true; }
            command.flags = flags;
            pending.push_back(std::move(command));
        }
    }
    void End() {
        if (!active) throw E_UNEXPECTED;
        active = false;
        // Clear retained images even if a draw fails. No stale frame replay.
        std::vector<DrawCommand> commands;
        commands.swap(pending);
        for (const auto& command : commands) {
            const HRESULT result = renderer.DrawUi(command.image.Get(), command.destination,
                command.has_source ? &command.source : nullptr, command.flags);
            if (FAILED(result)) throw result;
        }
    }
};
