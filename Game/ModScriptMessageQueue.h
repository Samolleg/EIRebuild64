#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

// No native pointers are dereferenced by producers. Only the UI owner drains.
// Preserve layout even when included after the engine ABI pack(4) headers.
#pragma pack(push, 8)
namespace ScriptMessages
{
inline bool Format(const char* text, std::string& out)
{
    out.clear();
    if (!text) return false;
    // Reset to the ordinary journal font/color; authored MOB text is plain text.
    out = "\aF0\aC\xE4\xD7\xA7";
    bool visible = false;
    size_t i = 0;
    for (; i < 4096 && text[i]; ++i)
    {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if ((c < 32 && c != '\n' && c != '\r' && c != '\t') || c == 127)
            return false; // No partial native formatting escapes or embedded controls.
        if (c == '\r') { if (text[i+1] != '\n') out += '\n'; }
        else out += char(c);
        if (c > 32) visible = true;
    }
    if (text[i] || !visible) return false;
    if (out.back() != '\n') out += '\n';
    return true;
}
class Queue
{
    std::mutex mutex;
    void* owner = nullptr;
    std::vector<std::string> pending;
    std::atomic<bool> ready{false};
public:
    // UI lifecycle only. A new instance invalidates undelivered old messages.
    size_t Bind(void* value)
    {
        std::lock_guard<std::mutex> lock(mutex);
        const size_t dropped = pending.size();
        owner = value; pending.clear(); ready.store(false);
        return dropped;
    }
    size_t Unbind(void* value)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (owner != value) return 0;
        const size_t dropped = pending.size();
        owner = nullptr; pending.clear(); ready.store(false);
        return dropped;
    }
    const char* Push(std::string text)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!owner) return "Mission message panel is not ready";
        if (pending.size() >= 64) return "Message queue is full (64 pending messages)";
        pending.push_back(std::move(text)); ready.store(true);
        return nullptr;
    }
    std::vector<std::string> Take(void* value)
    {
        std::vector<std::string> result;
        if (!ready.load()) return result; // No mutex or allocation on empty UI ticks.
        std::lock_guard<std::mutex> lock(mutex);
        if (owner != value) return result;
        result.swap(pending); ready.store(false);
        return result;
    }
};
}
#pragma pack(pop)
