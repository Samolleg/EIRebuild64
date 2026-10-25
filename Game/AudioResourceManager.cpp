#define MA_NO_ENCODING
#define MA_NO_FLAC
#define MA_NO_VORBIS
#include "../third_party/miniaudio/miniaudio.h"
#include "AudioResourceManager.h"

#include <atomic>
#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <new>
#include <string>
#include <windows.h>

#ifndef JMV_AUDIO_RESOURCES_DIAGNOSTICS
#define JMV_AUDIO_RESOURCES_DIAGNOSTICS 0
#endif
#if JMV_AUDIO_RESOURCES_DIAGNOSTICS
#include "EngineDiagnostics.h"
#define LOGINFO EngineDiagnostics::Event
#endif

namespace AudioResources
{
namespace
{
const size_t PredecodedLimit = 8u * 1024u * 1024u;
const size_t StreamHeadroom = 1024u * 1024u;
struct alignas(std::max_align_t) AllocationHeader { size_t size; };
std::atomic<size_t> UsedBytes(0), PeakBytes(0);
std::atomic<unsigned> Denied(0), Reads(0), OwnerReads(0);
DWORD OwnerThread = 0;
std::mutex TableMutex;
ma_resource_manager ResourceManager = {};
ma_default_vfs DiskVfs = {};
ma_vfs_callbacks Vfs = {};
bool Initialized = false;
unsigned Generation = 1, Serial = 0;
unsigned Captures = 0, Hits = 0, Evictions = 0;
DWORD LastLogTick = 0;

bool Reserve(size_t bytes)
{
    size_t old = UsedBytes.load();
    do
    {
        if (bytes > CacheLimitBytes || old > CacheLimitBytes - bytes)
        {
            ++Denied;
            return false;
        }
    } while (!UsedBytes.compare_exchange_weak(old, old + bytes));
    size_t peak = PeakBytes.load();
    while (peak < old + bytes &&
           !PeakBytes.compare_exchange_weak(peak, old + bytes)) {}
    return true;
}

void* Allocate(size_t size, void*)
{
    if (size == 0) size = 1;
    if (size > SIZE_MAX - sizeof(AllocationHeader) || !Reserve(size))
        return nullptr;
    AllocationHeader* header = static_cast<AllocationHeader*>(
        malloc(sizeof(AllocationHeader) + size));
    if (header == nullptr)
    {
        UsedBytes.fetch_sub(size);
        return nullptr;
    }
    header->size = size;
    return header + 1;
}

void Free(void* memory, void*)
{
    if (memory == nullptr) return;
    AllocationHeader* header = static_cast<AllocationHeader*>(memory) - 1;
    const size_t size = header->size;
    free(header);
    UsedBytes.fetch_sub(size);
}

void* Reallocate(void* memory, size_t size, void* user)
{
    if (memory == nullptr) return Allocate(size, user);
    if (size == 0) { Free(memory, user); return nullptr; }
    AllocationHeader* header = static_cast<AllocationHeader*>(memory) - 1;
    const size_t oldSize = header->size;
    if (size > SIZE_MAX - sizeof(AllocationHeader)) return nullptr;
    if (size > oldSize && !Reserve(size - oldSize)) return nullptr;
    AllocationHeader* resized = static_cast<AllocationHeader*>(
        realloc(header, sizeof(AllocationHeader) + size));
    if (resized == nullptr)
    {
        if (size > oldSize) UsedBytes.fetch_sub(size - oldSize);
        return nullptr;
    }
    resized->size = size;
    if (size < oldSize) UsedBytes.fetch_sub(oldSize - size);
    return resized + 1;
}
ma_allocation_callbacks Allocations = { nullptr, Allocate, Reallocate, Free };

std::string Normalize(const char* name)
{
    std::string result(name != nullptr ? name : "");
    for (char& c : result)
    {
        if (c == '\\') c = '/';
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return result;
}

uint16_t U16(const unsigned char* p) { return p[0] | (uint16_t(p[1]) << 8); }
uint32_t U32(const unsigned char* p)
{
    return p[0] | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
           (uint32_t(p[3]) << 24);
}

// Bounded metadata read only. Unknown/compressed formats take the paged path;
// no decoder or whole-file MP3 length scan runs on the requesting thread.
size_t SmallWaveBytes(const void* data, size_t size, uint32_t& rate, float& duration)
{
    const unsigned char* p = static_cast<const unsigned char*>(data);
    if (size < 12 || memcmp(p, "RIFF", 4) || memcmp(p + 8, "WAVE", 4)) return 0;
    uint16_t channels = 0, block = 0, format = 0;
    uint32_t dataBytes = 0;
    size_t offset = 12;
    while (offset <= size - 8)
    {
        const uint32_t bytes = U32(p + offset + 4);
        if (bytes > size - offset - 8) return 0;
        if (!memcmp(p + offset, "fmt ", 4) && bytes >= 16)
        {
            format = U16(p + offset + 8);
            channels = U16(p + offset + 10);
            rate = U32(p + offset + 12);
            block = U16(p + offset + 20);
        }
        if (!memcmp(p + offset, "data", 4)) dataBytes = bytes;
        offset += 8 + static_cast<size_t>(bytes);
        if (bytes & 1u) { if (offset == size) break; ++offset; }
    }
    if ((format != 1 && format != 3) || channels == 0 || channels > 32 ||
        block == 0 || rate == 0 || dataBytes == 0 || dataBytes % block != 0)
        return 0;
    const uint64_t frames = dataBytes / block;
    duration = static_cast<float>(frames) / rate;
    const uint64_t bytes = frames * channels * sizeof(float);
    return bytes <= PredecodedLimit ? static_cast<size_t>(bytes) : 0;
}
}

struct Resource
{
    std::string name;
    char path[80];
    void* encoded;
    size_t bytes;
    uintptr_t providerIdentity;
    ma_resource_manager_data_source preload;
    unsigned pins;
    unsigned openFiles;
    DWORD lastUse;
    uint32_t rate;
    float duration;
    bool decoded;
    bool preloadInitialized;
    bool retired;
    Resource() : encoded(nullptr), bytes(0), providerIdentity(0), pins(0),
        openFiles(0), lastUse(0), rate(0), duration(0), decoded(false),
        preloadInitialized(false), retired(false) { path[0] = 0; }
};

namespace
{
std::map<std::string, Resource*> Names;
std::map<std::string, Resource*> Paths;
struct File
{
    Resource* resource;
    ma_vfs_file disk;
    size_t cursor;
};

ma_result Open(ma_vfs*, const char* path, ma_uint32 mode, ma_vfs_file* result)
{
    if (path == nullptr || result == nullptr) return MA_INVALID_ARGS;
    *result = nullptr;
    File* file = static_cast<File*>(Allocate(sizeof(File), nullptr));
    if (file == nullptr) return MA_OUT_OF_MEMORY;
    *file = File();
    {
        std::lock_guard<std::mutex> guard(TableMutex);
        auto found = Paths.find(path);
        if (found != Paths.end())
        {
            if ((mode & MA_OPEN_MODE_WRITE) != 0) { Free(file, nullptr); return MA_ACCESS_DENIED; }
            file->resource = found->second;
            ++file->resource->openFiles;
        }
    }
    if (file->resource == nullptr)
    {
        // Never reinterpret a retired cache URI as a relative disk file.
        if (!strncmp(path, "jmv-audio/", 10)) { Free(file, nullptr); return MA_DOES_NOT_EXIST; }
        const ma_result status = ma_vfs_open(&DiskVfs, path, mode, &file->disk);
        if (status != MA_SUCCESS) { Free(file, nullptr); return status; }
    }
    *result = file;
    return MA_SUCCESS;
}

ma_result OpenW(ma_vfs*, const wchar_t* path, ma_uint32 mode, ma_vfs_file* result)
{
    if (path == nullptr || result == nullptr) return MA_INVALID_ARGS;
    *result = nullptr;
    File* file = static_cast<File*>(Allocate(sizeof(File), nullptr));
    if (file == nullptr) return MA_OUT_OF_MEMORY;
    *file = File();
    const ma_result status = ma_vfs_open_w(&DiskVfs, path, mode, &file->disk);
    if (status != MA_SUCCESS) { Free(file, nullptr); return status; }
    *result = file;
    return MA_SUCCESS;
}

ma_result Close(ma_vfs*, ma_vfs_file handle)
{
    File* file = static_cast<File*>(handle);
    if (file == nullptr) return MA_INVALID_ARGS;
    ma_result status = MA_SUCCESS;
    if (file->resource != nullptr)
    {
        std::lock_guard<std::mutex> guard(TableMutex);
        --file->resource->openFiles;
    }
    else status = ma_vfs_close(&DiskVfs, file->disk);
    Free(file, nullptr);
    return status;
}

ma_result Read(ma_vfs*, ma_vfs_file handle, void* dst, size_t bytes, size_t* read)
{
    File* file = static_cast<File*>(handle);
    if (read) *read = 0;
    if (file == nullptr || (dst == nullptr && bytes != 0)) return MA_INVALID_ARGS;
    ++Reads;
    if (GetCurrentThreadId() == OwnerThread) ++OwnerReads;
    if (file->resource == nullptr) return ma_vfs_read(&DiskVfs, file->disk, dst, bytes, read);
    const size_t count = (std::min)(bytes, file->resource->bytes - file->cursor);
    if (count) memcpy(dst, static_cast<const char*>(file->resource->encoded) + file->cursor, count);
    file->cursor += count;
    if (read) *read = count;
    return count == bytes ? MA_SUCCESS : MA_AT_END;
}

ma_result Write(ma_vfs*, ma_vfs_file, const void*, size_t, size_t* written)
{
    if (written) *written = 0;
    return MA_ACCESS_DENIED;
}

ma_result Seek(ma_vfs*, ma_vfs_file handle, ma_int64 offset, ma_seek_origin origin)
{
    File* file = static_cast<File*>(handle);
    if (file == nullptr) return MA_INVALID_ARGS;
    if (file->resource == nullptr) return ma_vfs_seek(&DiskVfs, file->disk, offset, origin);
    const ma_int64 base = origin == ma_seek_origin_start ? 0 :
        origin == ma_seek_origin_current ? static_cast<ma_int64>(file->cursor) :
        static_cast<ma_int64>(file->resource->bytes);
    if ((offset > 0 && base > INT64_MAX - offset) ||
        (offset < 0 && base < INT64_MIN - offset)) return MA_BAD_SEEK;
    const ma_int64 next = base + offset;
    if (next < 0 || static_cast<uint64_t>(next) > file->resource->bytes) return MA_BAD_SEEK;
    file->cursor = static_cast<size_t>(next);
    return MA_SUCCESS;
}

ma_result Tell(ma_vfs*, ma_vfs_file handle, ma_int64* cursor)
{
    File* file = static_cast<File*>(handle);
    if (file == nullptr || cursor == nullptr) return MA_INVALID_ARGS;
    if (file->resource == nullptr) return ma_vfs_tell(&DiskVfs, file->disk, cursor);
    *cursor = file->cursor;
    return MA_SUCCESS;
}

ma_result Info(ma_vfs*, ma_vfs_file handle, ma_file_info* info)
{
    File* file = static_cast<File*>(handle);
    if (file == nullptr || info == nullptr) return MA_INVALID_ARGS;
    if (file->resource == nullptr) return ma_vfs_info(&DiskVfs, file->disk, info);
    info->sizeInBytes = file->resource->bytes;
    return MA_SUCCESS;
}

void Destroy(Resource* resource)
{
    // No VFS/table lock across uninit: its job may need Open/Close to finish.
    if (resource->preloadInitialized)
        ma_resource_manager_data_source_uninit(&resource->preload);
    {
        std::lock_guard<std::mutex> guard(TableMutex);
        Paths.erase(resource->path);
    }
    Free(resource->encoded, nullptr);
    resource->~Resource();
    Free(resource, nullptr);
}

bool EvictOne()
{
    Resource* oldest = nullptr;
    {
        std::lock_guard<std::mutex> guard(TableMutex);
        for (auto& entry : Names)
        {
            Resource* resource = entry.second;
            if (resource->pins == 0 &&
                (oldest == nullptr || resource->lastUse < oldest->lastUse)) oldest = resource;
        }
        if (oldest == nullptr) return false;
        Names.erase(oldest->name);
        oldest->retired = true;
    }
    Destroy(oldest);
    ++Evictions;
    return true;
}

bool MakeRoom(size_t required)
{
    if (required > CacheLimitBytes) return false;
    while (UsedBytes.load() > CacheLimitBytes - required)
        if (!EvictOne()) return false;
    return true;
}
}

bool Initialize()
{
    if (Initialized) return true;
    OwnerThread = GetCurrentThreadId();
    ma_default_vfs_init(&DiskVfs, &Allocations);
    Vfs = { Open, OpenW, Close, Read, Write, Seek, Tell, Info };
    ma_resource_manager_config config = ma_resource_manager_config_init();
    config.pVFS = &Vfs;
    config.allocationCallbacks = Allocations;
    config.decodedFormat = ma_format_f32;
    config.jobThreadCount = 2;
    const ma_result result = ma_resource_manager_init(&config, &ResourceManager);
    Initialized = result == MA_SUCCESS;
    LogStatistics(Initialized ? "init" : "init_failed", true);
    return Initialized;
}

ma_resource_manager* Manager() { return Initialize() ? &ResourceManager : nullptr; }
const ma_allocation_callbacks* Allocator() { return &Allocations; }

bool Contains(const char* name)
{
    if (name == nullptr) return false;
    std::lock_guard<std::mutex> guard(TableMutex);
    auto found = Names.find(Normalize(name));
    if (found == Names.end()) return false;
    found->second->lastUse = GetTickCount();
    ++Hits;
    return true;
}

bool Capture(const char* name, const void* data, uint32_t size, uintptr_t providerIdentity)
{
    if (name == nullptr || *name == 0 || data == nullptr || size == 0 ||
        size > CacheLimitBytes || !Initialize()) return false;
    if (Contains(name)) return true;
    uint32_t rate = 0;
    float duration = 0;
    const size_t pcmBytes = SmallWaveBytes(data, size, rate, duration);
    if (!MakeRoom(static_cast<size_t>(size) + pcmBytes + StreamHeadroom)) return false;
    void* storage = Allocate(sizeof(Resource), nullptr);
    if (storage == nullptr) return false;
    Resource* resource = new(storage) Resource();
    resource->encoded = Allocate(size, nullptr);
    if (resource->encoded == nullptr)
    {
        resource->~Resource(); Free(resource, nullptr); return false;
    }
    memcpy(resource->encoded, data, size);
    resource->name = Normalize(name);
    resource->bytes = size;
    resource->providerIdentity = providerIdentity;
    resource->rate = rate;
    resource->duration = duration;
    resource->decoded = pcmBytes != 0;
    resource->lastUse = GetTickCount();
    sprintf_s(resource->path, "jmv-audio/%u/%u", Generation, ++Serial);
    {
        std::lock_guard<std::mutex> guard(TableMutex);
        Names.emplace(resource->name, resource);
        Paths.emplace(resource->path, resource);
    }
    if (resource->decoded)
    {
        // 0.11.25 processes LOAD_DATA_BUFFER_NODE inline for ASYNC|WAIT_INIT.
        // Post without WAIT_INIT and wait on an explicit init notification:
        // initial decode/allocation stays on jobs, before another admission.
        ma_async_notification_event ready = {};
        if (ma_async_notification_event_init(&ready) != MA_SUCCESS)
        {
            { std::lock_guard<std::mutex> guard(TableMutex); Names.erase(resource->name); }
            Destroy(resource); return false;
        }
        ma_resource_manager_pipeline_notifications notifications = {};
        notifications.init.pNotification = &ready;
        const ma_uint32 flags = MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_DECODE |
            MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_ASYNC;
        const ma_result result = ma_resource_manager_data_source_init(
            &ResourceManager, resource->path, flags, &notifications, &resource->preload);
        if (result == MA_SUCCESS)
        {
            resource->preloadInitialized = true;
            ma_async_notification_event_wait(&ready);
        }
        ma_async_notification_event_uninit(&ready);
        const ma_result initResult = result == MA_SUCCESS
            ? ma_resource_manager_data_source_result(&resource->preload) : result;
        if (initResult != MA_SUCCESS && initResult != MA_BUSY)
        {
            { std::lock_guard<std::mutex> guard(TableMutex); Names.erase(resource->name); }
            Destroy(resource);
            LogStatistics("capture_failed", true);
            return false;
        }
    }
    ++Captures;
    LogStatistics("capture");
    return true;
}

Handle Pin(const char* name)
{
    if (name == nullptr) return nullptr;
    std::lock_guard<std::mutex> guard(TableMutex);
    auto found = Names.find(Normalize(name));
    if (found == Names.end()) return nullptr;
    ++found->second->pins;
    found->second->lastUse = GetTickCount();
    return found->second;
}

void Unpin(Handle resource)
{
    if (resource == nullptr) return;
    bool destroy = false;
    {
        std::lock_guard<std::mutex> guard(TableMutex);
        if (resource->pins != 0) --resource->pins;
        resource->lastUse = GetTickCount();
        destroy = resource->pins == 0 && resource->retired;
    }
    if (destroy) Destroy(resource);
}

uint32_t SampleRate(Handle resource) { return resource != nullptr ? resource->rate : 0; }
float Duration(Handle resource) { return resource != nullptr ? resource->duration : 0; }
bool IsDecoded(Handle resource) { return resource != nullptr && resource->decoded; }

int InitSound(ma_engine* engine, Handle resource, bool looping, ma_sound_group* group, ma_sound* sound)
{
    if (resource == nullptr || engine == nullptr) return MA_INVALID_ARGS;
    if (!MakeRoom(StreamHeadroom)) return MA_OUT_OF_MEMORY;
    ma_uint32 flags = MA_SOUND_FLAG_ASYNC | MA_SOUND_FLAG_WAIT_INIT |
        (resource->decoded ? MA_SOUND_FLAG_DECODE : MA_SOUND_FLAG_STREAM);
    if (looping) flags |= MA_SOUND_FLAG_LOOPING;
    const ma_result result = ma_sound_init_from_file(engine, resource->path, flags, group, nullptr, sound);
    if (result == MA_SUCCESS)
    {
        ma_format format;
        ma_uint32 channels = 0, rate = 0;
        if (ma_sound_get_data_format(sound, &format, &channels, &rate, nullptr, 0) == MA_SUCCESS)
            resource->rate = rate;
        ma_uint64 frames = 0;
        if (rate != 0 && ma_sound_get_length_in_pcm_frames(sound, &frames) == MA_SUCCESS)
            resource->duration = static_cast<float>(frames) / rate;
    }
    LogStatistics(result == MA_SUCCESS ? "start" : "start_failed", result != MA_SUCCESS);
    return result;
}

int InitFileSound(ma_engine* engine, const char* path, uint32_t flags, ma_sound* sound)
{
    if (!Initialize() || !MakeRoom(StreamHeadroom)) return MA_OUT_OF_MEMORY;
    // Keep the switch/marker initialization boundary, but run even the initial
    // music load on jobs. STREAM without ASYNC decodes its prefill on caller.
    return ma_sound_init_from_file(engine, path, flags | MA_SOUND_FLAG_LOOPING |
        MA_SOUND_FLAG_ASYNC | MA_SOUND_FLAG_WAIT_INIT, nullptr, nullptr, sound);
}

int InitFileDecoder(const char* path, void* decoder)
{
    if (!Initialize() || !MakeRoom(StreamHeadroom)) return MA_OUT_OF_MEMORY;
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
    config.allocationCallbacks = Allocations;
    return ma_decoder_init_vfs(&Vfs, path, &config, static_cast<ma_decoder*>(decoder));
}

bool PrepareStreamMemory(size_t bytes) { return Initialize() && MakeRoom(bytes); }

void ClearCache()
{
    ++Generation;
    for (;;)
    {
        Resource* resource = nullptr;
        {
            std::lock_guard<std::mutex> guard(TableMutex);
            if (Names.empty()) break;
            auto first = Names.begin();
            resource = first->second;
            Names.erase(first);
            resource->retired = true;
            if (resource->pins != 0) resource = nullptr;
        }
        if (resource != nullptr) Destroy(resource);
    }
    LogStatistics("cache_clear", true);
}

void Shutdown()
{
    // Caller detaches all sounds and video rings before destroying job threads.
    ClearCache();
    if (Initialized)
    {
        ma_resource_manager_uninit(&ResourceManager);
        Initialized = false;
    }
    LogStatistics("shutdown", true);
}

Statistics GetStatistics()
{
    Statistics stats = {};
    stats.bytes = UsedBytes.load();
    stats.peakBytes = PeakBytes.load();
    stats.deniedAllocations = Denied.load();
    stats.reads = Reads.load();
    stats.ownerReads = OwnerReads.load();
    stats.captures = Captures;
    stats.hits = Hits;
    stats.evictions = Evictions;
    stats.generation = Generation;
    std::lock_guard<std::mutex> guard(TableMutex);
    stats.entries = static_cast<unsigned>(Names.size());
    for (const auto& entry : Paths) stats.pinned += entry.second->pins != 0 ? 1u : 0u;
    return stats;
}

void LogStatistics(const char* reason, bool force)
{
#if JMV_AUDIO_RESOURCES_DIAGNOSTICS
    const DWORD now = GetTickCount();
    if (!force && now - LastLogTick < 2000) return;
    LastLogTick = now;
    const Statistics s = GetStatistics();
    LOGINFO("AUDIO_RESOURCES reason=%s limit_bytes=%u bytes=%u peak=%u entries=%u pinned=%u captures=%u hits=%u evictions=%u denied=%u vfs_reads=%u owner_reads=%u generation=%u workers=2 fx_decode=jobs music_decode=jobs video_pcm=bounded_queue",
        reason, static_cast<unsigned>(CacheLimitBytes), static_cast<unsigned>(s.bytes),
        static_cast<unsigned>(s.peakBytes), s.entries, s.pinned, s.captures, s.hits,
        s.evictions, s.deniedAllocations, s.reads, s.ownerReads, s.generation);
#else
    (void)reason; (void)force;
#endif
}
}
