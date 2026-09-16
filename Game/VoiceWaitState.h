#pragma once
#include <atomic>
#include <cstdint>

// Production default; diagnostic builds must explicitly opt in.
#ifndef JMV_VOICE_WAIT_DIAGNOSTICS
#define JMV_VOICE_WAIT_DIAGNOSTICS 0
#endif

// Standalone lifetime publication. Readers never touch ma_sound or SVoice.
#pragma pack(push, 8)
namespace VoiceWait
{
class Registry
{
    struct Slot
    {
        std::atomic<uint32_t> ticket{0};
        std::atomic<uintptr_t> manager{0};
        std::atomic<uint32_t> sound{0};
    };
    Slot slots[64];
    std::atomic<uint32_t> generation{0};
public:
    // The existing audio control owner serializes Publish/End for each slot.
    // Never wrap a generation: stale readers must not observe an ABA match.
    uint32_t Publish(unsigned slot, uintptr_t manager, uint32_t sound)
    {
        if (slot >= 64) return 0;
        slots[slot].ticket.store(0);
        uint32_t g = generation.load();
        do {
            if (g >= 0x1FFFFFEu) return 0;
        } while (!generation.compare_exchange_weak(g, g + 1));
        const uint32_t ticket = ((g + 1) << 6) | slot;
        slots[slot].manager.store(manager);
        slots[slot].sound.store(sound);
        slots[slot].ticket.store(ticket);
        return ticket;
    }
    uint32_t End(unsigned slot)
    {
        return slot < 64 ? slots[slot].ticket.exchange(0) : 0;
    }
    uint32_t Capture(uintptr_t manager, uint32_t sound) const
    {
        if (!manager || sound == 0xFFFFFFFFu) return 0;
        for (const auto& slot : slots)
        {
            const uint32_t first = slot.ticket.load();
            if (first && slot.manager.load() == manager &&
                slot.sound.load() == sound && first == slot.ticket.load())
                return first;
        }
        return 0;
    }
    bool Active(uint32_t ticket) const
    {
        return ticket && slots[ticket & 63].ticket.load() == ticket;
    }
};

// Use only the native per-thread Sleep scratch word, not command-shared params.
// Negative values below -1 distinguish our ticket from native segment numbers.
inline bool Pending(int state) { return state < -1; }
inline uint32_t Ticket(int state) { return static_cast<uint32_t>(-(state + 1)); }
inline int Encode(uint32_t ticket) { return -1 - static_cast<int>(ticket); }
inline bool AfterLoad(int& state)
{
    if (!Pending(state)) return false;
    state = -1; // Audio handles are not saved. Replay this unfinished line once.
    return true;
}
enum class StepResult { Started, Waiting, Finished, Failed };
template<class Start, class Active>
StepResult Step(int& state, Start start, Active active)
{
    if (Pending(state))
    {
        if (active(Ticket(state))) return StepResult::Waiting;
        state = -1;
        return StepResult::Finished;
    }
    state = -1;
    const uint32_t ticket = start();
    if (!ticket || ticket > 0x7FFFFFBFu) return StepResult::Failed;
    state = Encode(ticket);
    return StepResult::Started;
}
}
#pragma pack(pop)
