#pragma once

namespace WorldSfxPause
{
// World effects use the positional route; flat UI and streaming music do not.
inline bool Applies(bool spatial, bool speech, bool dialog)
{
    return spatial && !speech && !dialog;
}

inline bool CanResume(bool app, bool modalSpeech, bool world)
{
    return !app && !modalSpeech && !world;
}
}
