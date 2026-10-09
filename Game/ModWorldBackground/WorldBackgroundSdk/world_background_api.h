#pragma once
#include <stdint.h>
#include <windows.h>

// C ABI, x86, pack 4. No native Terrain pointers, D3D objects, STL or ownership
// transfer. Source arrays are copied before Publish returns. See docs below.
#define ET_ABI_VERSION 0x00010000u
#define ET_CAP_RESIDENT_MESH 1u
#define ET_CAP_XY_INSTANCES 2u
#define ET_CAP_OPAQUE_ALPHA 4u
#define ET_CAP_VISUAL_CALLBACK 8u
#define ET_CAP_UV_TRANSFORM 16u
#define ET_CAPS_V1 31u
#define ET_OPAQUE 1u
#define ET_ALPHA 2u
#pragma pack(push,4)
typedef struct ET_Vertex { float x,y,z; uint32_t diffuse,specular; float u,v; } ET_Vertex;
typedef struct ET_Frame {
    uint32_t cbSize,mapEpoch,serial;
    float worldToClip[16]; // row-major, column-vector, Direct3D clip z in [0,w]
    float activeX,activeY,activeWidth,activeHeight;
    double visualSeconds;
} ET_Frame;
typedef HRESULT (WINAPI *ET_Update)(void* context,const ET_Frame* frame);
typedef struct ET_Client {uint32_t cbSize,requiredCaps;ET_Update update;void* context;} ET_Client;
typedef struct ET_Mesh {
    uint32_t cbSize,vertexCount;
    const ET_Vertex* vertices; // expanded triangle list, <=65535 vertices
    uint32_t textureWidth,textureHeight;
    const uint32_t* argb; // packed A8R8G8B8; no names/shared resource identity
    uint32_t pass;
    float originX,originY; // absolute world XY; never changes source z
} ET_Mesh;
typedef struct ET_Stats {
    uint32_t cbSize,mapEpoch,frames,meshes;
    uint64_t cpuBytes,vertexUploads,draws;
    HRESULT lastError;
} ET_Stats;
typedef struct ET_Api {
    uint32_t cbSize,version,capabilities;
    HRESULT (WINAPI *Register)(const ET_Client*,uint32_t* client);
    HRESULT (WINAPI *Unregister)(uint32_t client);
    HRESULT (WINAPI *Publish)(uint32_t client,const ET_Mesh*,uint64_t* instance);
    HRESULT (WINAPI *Place)(uint32_t client,uint64_t instance,float x,float y,float u,float v);
    HRESULT (WINAPI *Remove)(uint32_t client,uint64_t instance);
    HRESULT (WINAPI *GetStats)(uint32_t client,ET_Stats* stats);
} ET_Api;
#pragma pack(pop)
#ifdef __cplusplus
extern "C" {
#endif
HRESULT WINAPI EnduranceTerrainQuery(uint32_t version,uint32_t bytes,void* api);
#ifdef __cplusplus
}
static_assert(sizeof(ET_Vertex)==28,"ET vertex ABI");
static_assert(sizeof(ET_Frame)==100,"ET frame ABI");
#ifdef _M_IX86
static_assert(sizeof(ET_Client)==16&&sizeof(ET_Mesh)==36&&sizeof(ET_Api)==36,"ET x86 ABI");
#endif
#endif

// Register on the render thread, outside DllMain. Keep client code/context alive
// until Unregister succeeds. Only one client. update runs once at opaque terrain
// completion; Publish/Place/Remove allowed ONLY inside update. Callback failure
// suppresses this frame's extra passes, never the active terrain. Unregister is
// owner-thread only, outside callback/draw; no later callbacks after success.
// Map epoch change invalidates all instance handles before update. Republishing
// is client's decision. Transparent pass uses the same immutable frame snapshot.
// All functions return HRESULT; handles from earlier registrations never revive.
// V1 accepts PREPARED visual meshes. It does not claim native MPR decoding,
// GPU water deformation, GPU lighting, GPU shadow calculation or environmental
// parity. Future client must prepare those layers or negotiate a richer ABI.
