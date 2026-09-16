#pragma once
#include <stdint.h>

// One envelope, independent of how many foreground lines overlap.
struct ScenarioSpeechDucking
{
    float gain = 1.0f;
    uint32_t lastTick = 0;
    unsigned int active = 0;
    bool initialized = false;

    void Update(unsigned int audible, uint32_t now)
    {
        const uint32_t elapsed = initialized ? now - lastTick : 0;
        initialized = true;
        lastTick = now;
        active = audible;
        const float target = audible != 0 ? 0.5f : 1.0f;
        const float step = 0.5f * static_cast<float>(elapsed) /
            (target < gain ? 150.0f : 350.0f);
        if (gain < target)
            gain = gain + step < target ? gain + step : target;
        else if (gain > target)
            gain = gain - step > target ? gain - step : target;
    }
};
