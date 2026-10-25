#pragma once
#include "VideoPlayer.h"

namespace Video {
struct Presentation {
    bool present = true;
    bool loading = false;
};
// P24 provides this endpoint. Registration, calls and destruction are on the
// host thread. The adapter owns the renderer gate and aspect-fit placement.
class MovieRenderer {
public:
    virtual ~MovieRenderer() = default;
    virtual bool SubmitMovie(const void* owner, std::shared_ptr<const Frame>, const Presentation&) = 0;
    virtual void ClearMovie(const void* owner) = 0;
};
void SetMovieRenderer(MovieRenderer*); // unregister before destroying renderer
void StopMovies(); // host shutdown, before destroying the audio engine
class Movie final : private FrameSink {
public:
    Movie();
    ~Movie();
    Movie(const Movie&) = delete;
    Movie& operator=(const Movie&) = delete;
    bool BeginPlay(const char* name, bool stopSounds = true, bool hideCursor = true, bool loop = false);
    bool PlayNextFrame(bool present);
    void StopPlaying();
    static bool Play(const char* name, unsigned long framePause = 0, bool skipMovie = true,
                     bool skipFrame = true, bool clearSurface = true,
                     bool stopSounds = true, bool useOverlays = true);
private:
    friend void StopMovies();
    friend void SetMovieRenderer(MovieRenderer*);
    Movie* next = nullptr;
    bool Submit(std::shared_ptr<const Frame>) override;
    std::unique_ptr<Player> player;
    std::shared_ptr<const Frame> lastFrame;
    Presentation presentation;
    bool restoreCursor = false;
    bool missingRendererLogged = false;
};
}
