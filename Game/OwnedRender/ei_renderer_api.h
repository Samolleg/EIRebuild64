#ifndef EI_RENDERER_API_H
#define EI_RENDERER_API_H

/* Source-host ABI. No STL objects, allocator ownership, or C++ exceptions cross
   the DLL boundary. Calls and borrowed inputs are synchronous. Calls use the
   creating thread, or the existing loading-video worker only while the shared
   ID3D11Multithread context is protected. Teardown uses the creating thread.
   The DLL stays loaded until process exit; destroy releases the host/GPU.
   Original game.exe continues to use the standard DDraw/D3D7 exports. */
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <stdint.h>

#define EI_RENDERER_API_VERSION 1u
#define EI_RENDERER_WORLD_API_VERSION 2u
#define EI_RENDERER_CAP_SOURCE_WORLD UINT64_C(16)
#define EI_RENDERER_WORLD_SKY 1u
#define EI_RENDERER_WORLD_CAVE 2u
#define EI_RENDERER_WORLD_FOG 4u
#define EI_RENDERER_WORLD_TERRAIN 8u /* API3: actual loaded terrain, not draw admission */
#define EI_RENDERER_CAP_HOST_DEVICE UINT64_C(1)
#define EI_RENDERER_CAP_SOURCE_UI UINT64_C(2)
#define EI_RENDERER_CAP_D3D11_TRANSITION UINT64_C(4)
/* Host creation applies the configured display mode on the HWND owner thread.
   The bridge swapchain reports the actual client extent for source layout. */
#define EI_RENDERER_CAP_DISPLAY_SETTINGS UINT64_C(8)
#define EI_RENDERER_UI_STRAIGHT_ALPHA 1u
#define EI_RENDERER_UI_OPAQUE 2u
#define EI_RENDERER_UI_POINT 4u
#define EI_RENDERER_UI_COLOR_KEY 8u

#pragma pack(push, 8)
typedef struct EIRenderHost EIRenderHost;
typedef struct EIRenderHostDescV1 {
    uint32_t size;
    HWND window;
    uint32_t width, height; /* bootstrap size; display capability selects actual client */
} EIRenderHostDescV1;

/* Transitional COM leases, each AddRef'd on success; caller releases them.
   The host owns creation/presentation/teardown. No second device or swapchain.
   Target leases must be refreshed after BeginFrame, before any old GPU consumer.
   This capability does not mean retained world/effect migration is complete. */
typedef struct EIRenderD3D11BridgeV1 {
    uint32_t size;
    ID3D11Device* device;
    ID3D11DeviceContext* context;
    IDXGISwapChain* swap_chain;
    ID3D11RenderTargetView* color_target;
    ID3D11DepthStencilView* depth_target;
} EIRenderD3D11BridgeV1;

typedef struct EIRenderUiBlitV1 {
    uint32_t size;
    ID3D11ShaderResourceView* image; /* borrowed; same host device */
    RECT destination;
    RECT source;
    uint32_t has_source;
    uint32_t flags; /* default: premultiplied alpha, linear clamp */
    uint32_t color_key; /* COLORREF, exact RGB key; alpha ignored */
} EIRenderUiBlitV1;

typedef struct EIRenderApiV1 {
    uint32_t size, version;
    uint64_t capabilities;
    HRESULT (__cdecl *create_host)(const EIRenderHostDescV1*, EIRenderHost**);
    HRESULT (__cdecl *destroy_host)(EIRenderHost*);
    HRESULT (__cdecl *acquire_d3d11_bridge)(EIRenderHost*, EIRenderD3D11BridgeV1*);
    HRESULT (__cdecl *begin_frame)(EIRenderHost*, const float color[4], float depth);
    HRESULT (__cdecl *draw_ui)(EIRenderHost*, const EIRenderUiBlitV1*);
    HRESULT (__cdecl *present)(EIRenderHost*);
} EIRenderApiV1;
/* API2 values are copied synchronously at the actual world draw boundary.
   Matrix is row-major, multiplying column vectors: clip = M * world.
   No native addresses, STL, retained engine pointers or save fields cross ABI.
   Region: 0 unknown, 1 Gipat, 2 Ingos, 3 Suslanger. Time is simulation time,
   hours unwrapped; weather 0 clear/1 rain/2 snow, transition progress clamped,
   rain_progress is the raw pre-particle-update transition (2 if inactive).
   world_generation is a transient source scene lifecycle revision. */
typedef struct EIRenderWorldFrameV2 {
    uint32_t size, flags;
    uint64_t world_generation;
    float world_to_clip[16];
    float eye[3];
    uint32_t region;
    double game_hours, world_time;
    int32_t weather_current, weather_next;
    float weather_progress, rain_progress;
    float ambient[3], sunlight_direction[3], sunlight_diffuse[3], fog_color[3];
    float fog_range[3]; /* start, end, 255/(end-start) from source TL owner */
    float fog_projection[3]; /* P43, P34, P33 of that same projection */
    float wind_direction[2], wind_force; /* existing normalized terrain wind */
} EIRenderWorldFrameV2;

/* BeginFrame -> BeginWorld -> compatibility geometry -> EndWorld -> UI -> Present.
   BeginWorld draws the owned sky first and reports whether the stock sky is
   consumed (existing sky_clouds=0 keeps stock). EndWorld composes to output.
   AcquireBridge target leases change at both seams; source must refresh them.
   World calls use creator thread only; protected loading video keeps API1 calls.
   API1 layout/query remains supported; API2 embeds it unchanged at offset zero. */
typedef struct EIRenderApiV2 {
    EIRenderApiV1 base;
    HRESULT (__cdecl *begin_world)(EIRenderHost*, const EIRenderWorldFrameV2*, uint32_t* sky_consumed);
    HRESULT (__cdecl *end_world)(EIRenderHost*);
    HRESULT (__cdecl *record_draw)(EIRenderHost*, uint32_t triangles);
} EIRenderApiV2;
/* API3: source inputs are borrowed for this call only. A selected map-member
   read first reports its exact size (destination=null), then fills DLL-owned
   memory. No engine mapping/pointer, callback or allocator is retained.
   Resource paths are the completed first-wins mod/base chain, not a profile.
   Terrain coordinates are the source DrawBase coordinates (identity world).
   All scene rows are transient render values, never save/network fields. */
#define EI_RENDERER_SOURCE_API_VERSION 3u
#define EI_RENDERER_CAP_SOURCE_INPUTS UINT64_C(32)
typedef HRESULT (__cdecl *EIRenderReadMemberV3)(void*, const char*, void*, uint32_t, uint32_t*);
typedef struct EIRenderTerrainV3 {
    uint32_t size;
    uint64_t world_generation;
    const char* map_header;
    const char* map_resource;
    EIRenderReadMemberV3 read_member;
    void* read_context;
    EIRenderReadMemberV3 read_texture;
    void* texture_context;
    const wchar_t* const* loose_roots;
    uint32_t loose_count;
    const wchar_t* const* archives;
    uint32_t archive_count;
    const float* layer_shifts;
    uint32_t layer_count;
} EIRenderTerrainV3;
typedef struct EIRenderObjectV3 {
    uint32_t id, family;
    const char* visual;
    float position[3];
    float bounds[6]; /* world envelope, only when bounds_valid */
    uint32_t bounds_valid;
} EIRenderObjectV3;
typedef struct EIRenderUnitV3 {
    uint32_t id, blood, flags; /* dead1/moving2/controlled4/shown8 */
    uint32_t pose, locomotion;
    const char* visual;
    float position[3], radius, height, fade, visibility, loudness, vision, lowest;
} EIRenderUnitV3;
typedef struct EIRenderPointLightV3 { float position[3], color[3], radius; } EIRenderPointLightV3;
typedef struct EIRenderSceneV3 {
    uint32_t size;
    uint64_t world_generation;
    const EIRenderObjectV3* objects; uint32_t object_count;
    const EIRenderUnitV3* units; uint32_t unit_count;
    const EIRenderPointLightV3* lights; uint32_t light_count;
} EIRenderSceneV3;
typedef struct EIRenderApiV3 {
    EIRenderApiV2 base;
    HRESULT (__cdecl *set_terrain)(EIRenderHost*, const EIRenderTerrainV3*);
    HRESULT (__cdecl *set_scene)(EIRenderHost*, const EIRenderSceneV3*);
    HRESULT (__cdecl *terrain_drawn)(EIRenderHost*);
    HRESULT (__cdecl *bind_world_pixels)(EIRenderHost*, uint32_t fog_enabled);
} EIRenderApiV3;
/* Optional read-only status query through the existing QueryApi entry point.
   This is not a new world/geometry API: API1/2/3 prefixes stay byte-identical.
   Query on the host's creating thread; borrowed fixed-size output owns no COM.
   drawn means the cover draw pass was submitted, not visual acceptance. */
#define EI_RENDERER_STATUS_API_VERSION 0x10003u
#define EI_RENDERER_STATUS_VERSION 1u
#define EI_RENDERER_STATUS_FEATURES 128u
typedef struct EIRenderFeatureStatusV1 {
    char name[64];
    uint32_t enabled, active;
    char reason[64];
} EIRenderFeatureStatusV1;
typedef struct EIRenderStatusV1 {
    uint32_t size, version;
    uint32_t ground_cover_drawn;
    char ground_cover_reason[160];
    uint32_t grid_x, grid_y, atlas_ready, atlas_total;
    uint32_t feature_count;
    EIRenderFeatureStatusV1 features[EI_RENDERER_STATUS_FEATURES];
} EIRenderStatusV1;
typedef struct EIRenderStatusApiV1 {
    uint32_t size, version;
    HRESULT (__cdecl *get_status)(EIRenderHost*, EIRenderStatusV1*);
} EIRenderStatusApiV1;
/* Same status query, extended table size; the V1 table/payload remains valid.
   Parameters carry their actual value and parent name. active means the
   primary source consumer is connected, not GPU/visual acceptance. */
#define EI_RENDERER_STATUS_VERSION_V2 2u
#define EI_RENDERER_STATUS_ROWS_V2 512u
typedef struct EIRenderFeatureStatusV2 {
    char name[64], param[64], value[32];
    uint32_t enabled, active;
    char reason[64];
} EIRenderFeatureStatusV2;
typedef struct EIRenderStatusV2 {
    uint32_t size, version, ground_cover_drawn;
    char ground_cover_reason[160];
    uint32_t grid_x, grid_y, atlas_ready, atlas_total, feature_count;
    EIRenderFeatureStatusV2 features[EI_RENDERER_STATUS_ROWS_V2];
} EIRenderStatusV2;
typedef struct EIRenderStatusApiV2 {
    EIRenderStatusApiV1 base;
    HRESULT (__cdecl *get_status_v2)(EIRenderHost*, EIRenderStatusV2*);
} EIRenderStatusApiV2;
/* API4 geometry is copied synchronously at registration. Streams contain the
   source's completed shape (including its authored packed W lanes). Pose and
   morph are render values, never engine pointers. Future depth consumers use
   the same mesh, morph and object_to_world; no_shadow is explicit.
   Texture registration retains VALUE metadata only. submit_figure borrows a
   completed SRV from the API1 shared device for this call only, drains the
   native queue and releases its material references before returning. No
   duplicate GPU image, engine pointer, callback or allocator is retained. */
#define EI_RENDERER_FIGURE_API_VERSION 4u
#define EI_RENDERER_CAP_SOURCE_FIGURES UINT64_C(64)
#define EI_RENDERER_CAP_SOURCE_REVISIONS UINT64_C(128)
#define EI_RENDERER_FIGURE_STATIC 0u
#define EI_RENDERER_FIGURE_CHARACTER 1u
#define EI_RENDERER_FIGURE_EQUIPMENT 2u
typedef struct EIRenderPackedV4 { float x[4], y[4], z[4], w[4]; } EIRenderPackedV4;
typedef struct EIRenderComponentV4 { uint16_t position, normal, texture; } EIRenderComponentV4;
typedef struct EIRenderMeshV4 {
    uint32_t size, version;
    uint64_t scene_generation, identity;
    uint64_t source_revision; /* nonzero, monotonic for this shared source identity */
    const EIRenderPackedV4* positions; uint32_t position_groups;
    const EIRenderPackedV4* normals; uint32_t normal_groups;
    const float* uv; uint32_t uv_count;
    const EIRenderComponentV4* components; uint32_t component_count;
    const uint16_t* indices; uint32_t index_count;
} EIRenderMeshV4;
typedef struct EIRenderTextureV4 {
    uint32_t size, version;
    uint64_t scene_generation, identity, generation;
    uint32_t role; /* actual source texture type ID: primary/secondary */
} EIRenderTextureV4;
typedef struct EIRenderFigureV4 {
    uint32_t size, version;
    uint64_t scene_generation, object_id, mesh, texture, texture_generation;
    uint32_t part, category, visible, no_shadow, highlighted, texture_role;
    float shape[3]; /* completed source shape; mesh is already evaluated */
    float object_to_world[16], object_to_clip[16]; /* row-major HLSL matrices */
    const float* morph_xyz; uint32_t morph_count;
    const uint32_t* diffuse; const uint32_t* specular;
    float material_diffuse[3], material_alpha, emissive[4], material_specular[4];
    float u_shift, v_shift;
    uint32_t flags, default_diffuse, default_specular;
    const uint32_t* render_states; uint32_t render_state_count;
    const uint32_t* texture_states; uint32_t texture_state_count;
    ID3D11ShaderResourceView* primary;
    ID3D11ShaderResourceView* secondary;
} EIRenderFigureV4;
typedef struct EIRenderFigureResultV4 {
    uint32_t size, version, accepted;
    char reason[64];
} EIRenderFigureResultV4;
typedef struct EIRenderApiV4 {
    EIRenderApiV3 base;
    HRESULT (__cdecl *register_mesh)(EIRenderHost*, const EIRenderMeshV4*, uint64_t*);
    HRESULT (__cdecl *release_mesh)(EIRenderHost*, uint64_t);
    HRESULT (__cdecl *register_texture)(EIRenderHost*, const EIRenderTextureV4*, uint64_t*);
    HRESULT (__cdecl *release_texture)(EIRenderHost*, uint64_t);
    HRESULT (__cdecl *submit_figure)(EIRenderHost*, const EIRenderFigureV4*, EIRenderFigureResultV4*);
    HRESULT (__cdecl *release_instance)(EIRenderHost*, uint64_t scene_generation, uint64_t object_id, uint32_t part);
} EIRenderApiV4;
/* API5 reuses API3's selected map/atlas owner. Registration is keyed by a
   nonzero source revision; all callbacks and frame pointers end at the call.
   Opaque ownership is all-or-none before any draw. Once accepted, a device
   failure ends the frame and never replays legacy geometry. */
#define EI_RENDERER_TERRAIN_API_VERSION 5u
#define EI_RENDERER_CAP_SOURCE_TERRAIN UINT64_C(256)
typedef struct EIRenderTerrainSourceV5 {
    EIRenderTerrainV3 base;
    uint64_t source_revision;
    /* Optional completed base XYZ + normal XYZ (1089 rows of six floats).
       S_FALSE means the sector is unloaded: immutable selected MPR is exact. */
    HRESULT (__cdecl *read_base_vertices)(void*,uint32_t,float*,uint32_t);
    void* vertex_context;
} EIRenderTerrainSourceV5;
typedef struct EIRenderTerrainSectorV5 {
    uint32_t sector, lod, subparts, stitch;
    uint64_t phase_revision;
    const uint32_t* phases; uint32_t phase_count;
} EIRenderTerrainSectorV5;
typedef struct EIRenderTerrainFrameV5 {
    uint32_t size, version;
    uint64_t scene_generation, source_revision;
    float object_to_world[16], object_to_clip[16];
    float camera[3], lod_distances[2];
    uint32_t maximum_lod, render_state;
    float wind[2], wind_force, wind_normalized, wind_phase, precise_time;
    const EIRenderTerrainSectorV5* sectors; uint32_t sector_count;
    const uint32_t* render_states; uint32_t render_state_count;
    const uint32_t* texture_states; uint32_t texture_state_count;
} EIRenderTerrainFrameV5;
typedef struct EIRenderTerrainResultV5 {
    uint32_t size, version, accepted, commands;
    char reason[64];
} EIRenderTerrainResultV5;
typedef struct EIRenderApiV5 {
    EIRenderApiV4 base;
    HRESULT (__cdecl *set_terrain_source)(EIRenderHost*, const EIRenderTerrainSourceV5*);
    HRESULT (__cdecl *submit_terrain)(EIRenderHost*, const EIRenderTerrainFrameV5*, EIRenderTerrainResultV5*);
    HRESULT (__cdecl *release_terrain)(EIRenderHost*);
} EIRenderApiV5;
/* Independent versioned diagnostic query; API1-5 sizes stay unchanged.
   The result contains values only. A rejected registration preserves handle. */
#define EI_RENDERER_MESH_REPORT_API_VERSION 0x10004u
typedef struct EIRenderMeshReportV1 {
    uint32_t size, version;
    char reason[128];
} EIRenderMeshReportV1;
typedef struct EIRenderMeshReportApiV1 {
    uint32_t size, version;
    HRESULT (__cdecl *register_mesh_report)(EIRenderHost*,const EIRenderMeshV4*,uint64_t*,EIRenderMeshReportV1*);
} EIRenderMeshReportApiV1;
#pragma pack(pop)

typedef HRESULT (__cdecl *EIRenderQueryApiFn)(uint32_t version, uint32_t size, void*);
#ifdef __cplusplus
extern "C" {
#endif
HRESULT __cdecl EI_Renderer_QueryApi(uint32_t version, uint32_t size, void*);
#ifdef __cplusplus
}
#endif
#endif
