#pragma once
#include "world_background_api.h"

// Additive ABI. Query the same export with ET_ABI_VERSION_2 and sizeof(ET_Api2).
// This is a prepared-visual-data interface, NOT a native MPR simulation API.
#define ET_ABI_VERSION_2 0x00020000u
#define ET_CAP_INDEXED_INPUT 32u
#define ET_CAP_VISUAL_UPDATE 64u
#define ET_CAP_ACTIVE_RELATIVE 128u
#define ET_CAP_MATERIAL 256u
#define ET_CAP_FRAME_ENVIRONMENT 512u
#define ET_CAPS_V2 (ET_CAPS_V1 | 32u | 64u | 128u | 256u | 512u)
// Intentionally NOT advertised: reject clients that require these algorithms.
#define ET_CAP_NATIVE_WATER 0x10000u
#define ET_CAP_NATIVE_LIGHTING 0x20000u
#define ET_CAP_NATIVE_TERRAIN_SHADOWS 0x40000u
#define ET_ENV_FOG_STATES 1u
#define ET_MATERIAL_ALPHA_TEST 1u
#define ET_MATERIAL_ADDITIVE 2u
#define ET_UPDATE_VERTICES 1u
#define ET_UPDATE_TEXTURE 2u
#pragma pack(push,4)
typedef struct ET_Environment2 {
    uint32_t valid, fogEnabled, fogARGB, fogTableMode, fogVertexMode;
    float fogStart, fogEnd, fogDensity;
    // Raw ACTIVE device state; NONE modes do not specify native CPU fog math.
    // No sun, ambient, native water clock or shadow map is fabricated here.
} ET_Environment2;
typedef struct ET_Frame2 {
    ET_Frame view;
    // view.worldToClip is ACTIVE-LOCAL to clip, not world-layout to clip.
    // view.activeX/Y are NATIVE origin, NOT the origin in the saved world layout.
    // view.visualSeconds is QPC wall time, NOT pause-aware simulation time.
    ET_Environment2 environment;
} ET_Frame2;
typedef HRESULT (WINAPI *ET_Update2)(void*,const ET_Frame2*);
typedef struct ET_Client2 { uint32_t cbSize,requiredCaps; ET_Update2 update; void* context; } ET_Client2;
typedef struct ET_Material2 { uint32_t pass,flags,alphaRef; } ET_Material2;
typedef struct ET_Mesh2 {
    uint32_t cbSize,vertexCount;
    const ET_Vertex* vertices;
    uint32_t indexCount;
    const uint16_t* indices; // optional triangle list; expanded count <= 65535
    uint32_t textureWidth,textureHeight;
    const uint32_t* argb; // tightly packed rows A8R8G8B8, straight alpha
    ET_Material2 material;
    float worldX,worldY; // neighbor origin in the SAVED layout; no Z translation
} ET_Mesh2;
typedef struct ET_VisualUpdate2 {
    uint32_t cbSize,flags,vertexCount;
    const ET_Vertex* vertices; // same original vertex count/topology as Publish
    const uint32_t* argb; // same dimensions; copied only with UPDATE_TEXTURE
} ET_VisualUpdate2;
typedef struct ET_Api2 {
    uint32_t cbSize,version,capabilities;
    HRESULT (WINAPI *Register)(const ET_Client2*,uint32_t*);
    HRESULT (WINAPI *Unregister)(uint32_t);
    HRESULT (WINAPI *SetWorldOrigin)(uint32_t,float activeWorldX,float activeWorldY);
    HRESULT (WINAPI *Publish)(uint32_t,const ET_Mesh2*,uint64_t*);
    HRESULT (WINAPI *UpdateVisual)(uint32_t,uint64_t,const ET_VisualUpdate2*);
    HRESULT (WINAPI *Place)(uint32_t,uint64_t,float worldX,float worldY,float u,float v);
    HRESULT (WINAPI *Remove)(uint32_t,uint64_t);
    HRESULT (WINAPI *GetStats)(uint32_t,ET_Stats*);
    HRESULT (WINAPI *Reset)(uint32_t); // outside callback; drops all meshes now
} ET_Api2;
#pragma pack(pop)
typedef HRESULT (WINAPI *ET_Query)(uint32_t,uint32_t,void*);
#ifdef __cplusplus
static_assert(sizeof(ET_Environment2)==32&&sizeof(ET_Frame2)==132,"ET v2 frame ABI");
#ifdef _M_IX86
static_assert(sizeof(ET_Client2)==16&&sizeof(ET_Mesh2)==52&&sizeof(ET_VisualUpdate2)==20&&sizeof(ET_Api2)==48,"ET v2 x86 ABI");
#endif
#endif

// Register/GetStats may run on any thread OUTSIDE DllMain. DDraw binds the
// client to the first terrain render callback thread. Unregister/Reset must run
// on that thread after binding (any thread before first callback), outside a
// callback. Successful Unregister guarantees no further use of code/context.
// Do not wait for another thread to call API while inside your callback.
// Mutations other than Reset are legal ONLY inside update, on that render thread.
// Update runs once at opaque completion. Alpha uses the same frozen snapshot.
// SetWorldOrigin is mandatory after each epoch or Reset, before Publish/draw.
// Return S_FALSE while your matching map snapshot is unavailable (draw nothing).
// All arrays are deep copied; no caller D3D objects/pointers are retained.
