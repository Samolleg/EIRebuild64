#pragma once
#include "world_background_stream.h"
// Optional input transport; querying this interface does NOT imply GPU lighting.
#define ET_QUERY_LIGHTING_INPUTS_1 0x4c490001u
#define ET_LIGHT_INPUT_STORAGE 1u
#define ET_LIGHT_ENV_SUN 1u
#define ET_LIGHT_ENV_CLOCK 8u
#define ET_LIGHT_ENV_POSITION 16u
#define ET_LIGHT_ENV_ATMOSPHERE 32u
#define ET_LIGHT_LAYER 1u
#define ET_LIGHT_COVERED 2u
#define ET_LIGHT_HAS_OVERLAY 4u
#pragma pack(push,4)
typedef struct ET_LightVertex1 {
    float normal[3],emission[3],depth,inverseTransparency;
    uint16_t ownerMaterial; uint8_t alpha,flags;
} ET_LightVertex1;
typedef struct ET_LightSector1 {
    uint32_t x,y;
    const float* heights; // 1089 ground samples, y-major, including duplicate edges
    const ET_LightVertex1* ground; // 1089; required
    const ET_LightVertex1* overlay; // null or 1089
} ET_LightSector1;
typedef struct ET_LightMap1 {
    uint64_t key,revision; // client-assigned identity, stable within mapEpoch
    uint32_t sectorsX,sectorsY,sectorCount;
    const ET_LightSector1* sectors; // only present sectors; arbitrary order
    float worldX,worldY; // saved-layout origin, no Z offset
} ET_LightMap1;
typedef struct ET_LightRef1 {uint32_t sectorX,sectorY,vertex,layer;} ET_LightRef1;
typedef struct ET_LightBinding1 {
    uint64_t meshKey,mapKey;
    uint32_t count; const ET_LightRef1* vertices; // original source vertex order
} ET_LightBinding1;
typedef struct ET_LightSnapshot1 {
    uint32_t cbSize,mapCount,bindingCount;
    const ET_LightMap1* maps;
    const ET_LightBinding1* bindings;
} ET_LightSnapshot1;
typedef struct ET_LightEnvironment1 {
    uint32_t cbSize,mapEpoch,frameSerial,valid;
    uint64_t revision;
    float sunlight[3],direction[3],ambient[3],sunPosition[3];
    float atmosphereRange[3],atmosphereProjection[3]; // start,end,scale255; P43,P34,P33
    double visualSeconds; // producer simulation clock, not QPC
} ET_LightEnvironment1;
typedef struct ET_LightStatus1 {
    uint32_t cbSize,capabilities,mapEpoch,frameSerial;
    uint64_t geometryRevision,environmentRevision,retainedInputBytes;
    uint32_t mapCount,bindingCount,gpuBound,reserved;
} ET_LightStatus1;
typedef struct ET_LightingApi1 {
    uint32_t cbSize,version,capabilities,nativeCapabilities;
    HRESULT (WINAPI *Submit)(uint32_t,const ET_StreamSubmit1*,const ET_LightSnapshot1*,uint64_t*);
    HRESULT (WINAPI *UpdateEnvironment)(uint32_t,const ET_LightEnvironment1*);
    HRESULT (WINAPI *GetStatus)(uint32_t,ET_LightStatus1*);
} ET_LightingApi1;
#pragma pack(pop)
#ifdef __cplusplus
static_assert(sizeof(ET_LightVertex1)==36&&sizeof(ET_LightRef1)==16,"lighting POD ABI");
static_assert(sizeof(ET_LightEnvironment1)==104&&sizeof(ET_LightStatus1)==56,"environment ABI");
#ifdef _M_IX86
static_assert(sizeof(ET_LightSector1)==20&&sizeof(ET_LightMap1)==40&&sizeof(ET_LightBinding1)==24&&sizeof(ET_LightSnapshot1)==20&&sizeof(ET_LightingApi1)==28,"lighting x86 ABI");
#endif
#endif
