#pragma once
#include <stdint.h>
// CPU-only C ABI. Spans remain valid during the callback; consumer copies them.
// Same decoder and endpoint order as the game. No Qt/STL or ownership crosses.
#define EI_ENVIRONMENT_ABI 1u
#if defined(_WIN32)
#define EI_ENV_CALL __cdecl
#else
#define EI_ENV_CALL
#endif
typedef struct EiEnvironmentPart {
    uint32_t size,abi;
    const char* name;
    const char* logical_name;
    uint32_t vertices,indices,shapes,role,passability,opacity,render;
    const float* positions; // [shape][vertex][XYZ], game Z-up, model-local
    const float* normals;   // same dimensions, normalized endpoints
    const float* uv;        // [vertex][UV], common prepared material transform
    const uint16_t* triangles;
    const float* collision; // [shape][min XYZ,max XYZ]
    const char* texture_key;
    const uint8_t* texture; // in-memory MMP descriptor of the decoded albedo
    uint64_t texture_bytes;
    uint32_t alpha_mode,two_sided;
    float alpha_cutoff,alpha_factor;
} EiEnvironmentPart;
typedef int32_t (EI_ENV_CALL *EiEnvironmentSink)(void*,const EiEnvironmentPart*);
typedef int32_t (EI_ENV_CALL *EiEnvironmentDecode)(const void*,uint64_t,const char*,EiEnvironmentSink,void*,char*,uint32_t);
typedef int32_t (EI_ENV_CALL *EiModelPoseSink)(void*,const char*,uint32_t,const float*,const float*);
typedef int32_t (EI_ENV_CALL *EiModelOpen)(const void*,uint64_t,const char*,const char*,const char*,const char*,EiEnvironmentSink,void*,char*,uint32_t,void**);
typedef int32_t (EI_ENV_CALL *EiModelOpenForm)(const void*,uint64_t,const char*,const char*,const char*,const char*,const float*,EiEnvironmentSink,void*,char*,uint32_t,void**);
typedef int32_t (EI_ENV_CALL *EiModelPose)(void*,float,EiModelPoseSink,void*);
typedef void (EI_ENV_CALL *EiModelClose)(void*);
#ifdef __cplusplus
#ifndef EI_ENVIRONMENT_EXPORT
#define EI_ENVIRONMENT_EXPORT
#endif
extern "C" EI_ENVIRONMENT_EXPORT int32_t EI_ENV_CALL ataman_environment_decode(const void*,uint64_t,const char*,EiEnvironmentSink,void*,char*,uint32_t) noexcept;
#endif
