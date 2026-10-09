#pragma once

#include <stddef.h>
#include <stdint.h>

#ifndef miniaudio_h
#include "../third_party/miniaudio/miniaudio.h"
#endif

// All entry/cache mutations run on the game-side owner. The VFS and allocator
// are thread-safe; miniaudio jobs never call the native mapped-resource API.
namespace AudioResources
{
const size_t CacheLimitBytes = 64u * 1024u * 1024u;
struct Resource;
typedef Resource* Handle;
struct Statistics
{
    size_t bytes;
    size_t peakBytes;
    unsigned entries;
    unsigned pinned;
    unsigned captures;
    unsigned hits;
    unsigned evictions;
    unsigned deniedAllocations;
    unsigned reads;
    unsigned ownerReads;
    unsigned generation;
};

bool Initialize();
ma_resource_manager* Manager();
const ma_allocation_callbacks* Allocator();
bool Capture(const char* name, const void* data, uint32_t size,
             uintptr_t providerIdentity = 0);
bool Contains(const char* name);
Handle Pin(const char* name);
void Unpin(Handle resource);
uint32_t SampleRate(Handle resource);
float Duration(Handle resource);
bool IsDecoded(Handle resource);
int InitSound(ma_engine* engine, Handle resource, bool looping,
              ma_sound_group* group, ma_sound* sound);
int InitFileSound(ma_engine* engine, const char* path, uint32_t flags,
                  ma_sound* sound);
int InitFileDecoder(const char* path, void* decoder);
bool PrepareStreamMemory(size_t bytes);
void ClearCache();
void Shutdown();
Statistics GetStatistics();
void LogStatistics(const char* reason, bool force = false);
}
