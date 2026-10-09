#pragma once
#include "world_background_api_v2.h"
#define ET_QUERY_STREAM_1 0x53540001u
#define ET_STREAM_KEEP 0u
#define ET_STREAM_REPLACE 1u
#define ET_STREAM_GEOMETRY 2u
#define ET_STREAM_PREPARED 1u
#define ET_STREAM_UPLOADING 2u
#define ET_STREAM_READY 3u
#define ET_STREAM_COMMITTED 4u
#define ET_STREAM_CANCELLED 5u
#define ET_STREAM_ERROR 6u
#define ET_STREAM_PREPARING 7u
#pragma pack(push,4)
typedef struct ET_StreamItem1 {
    uint64_t key;
    uint32_t mode;
    ET_Mesh2 mesh;
} ET_StreamItem1;
typedef struct ET_StreamSubmit1 {
    uint32_t cbSize,mapEpoch;
    uint64_t revision;
    uint32_t count;
    const ET_StreamItem1* items;
} ET_StreamSubmit1;
typedef struct ET_StreamStatus1 {
    uint32_t cbSize,state,mapEpoch;
    HRESULT error;
    uint64_t ticket,revision,retainedBytes,pendingBytes,retiredBytes;
    uint64_t prepared,admitted,uploaded,cancelled,uploadBytes,uploadOperations;
    uint64_t prepareMicroseconds,uploadMicroseconds;
    uint64_t queued,readyLatencyMicroseconds,peakCpuPayloadBytes,maxUploadMicroseconds;
} ET_StreamStatus1;
typedef struct ET_StreamApi1 {
    uint32_t cbSize,version;
    HRESULT (WINAPI *Enable)(uint32_t);
    HRESULT (WINAPI *Submit)(uint32_t,const ET_StreamSubmit1*,uint64_t*);
    HRESULT (WINAPI *Poll)(uint32_t,uint64_t,ET_StreamStatus1*);
    HRESULT (WINAPI *Admit)(uint32_t,uint64_t);
    HRESULT (WINAPI *Commit)(uint32_t,uint64_t);
    HRESULT (WINAPI *Cancel)(uint32_t,uint64_t);
    HRESULT (WINAPI *Release)(uint32_t,uint64_t);
} ET_StreamApi1;
#pragma pack(pop)

#ifdef __cplusplus
static_assert(sizeof(ET_StreamStatus1)==152,"stream status ABI");
#ifdef _M_IX86
static_assert(sizeof(ET_StreamItem1)==64&&sizeof(ET_StreamSubmit1)==24&&sizeof(ET_StreamApi1)==36,"stream x86 ABI");
#endif
#endif
