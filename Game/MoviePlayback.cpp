#include "StdAfx.h"
#include "MoviePlayback.h"
#include "AgentBackground.h"
#include "GameMain.h"
#include "WorldClient.h"
#include "SoundManager.h"
#include "I3DCursor.h"
#include "ModRuntime.h"
#include "EngineDiagnostics.h"
#include <fstream>
#include <algorithm>

namespace Video {
namespace {
MovieRenderer* renderer = nullptr;
Movie* firstMovie = nullptr;
bool Inactive() { return !AgentBackground::Enabled() && (IsIconic(NGame::hWnd) || GetForegroundWindow() != NGame::hWnd); }
}
void SetMovieRenderer(MovieRenderer* value) {
    if (renderer) for (auto* movie = firstMovie; movie; movie = movie->next) renderer->ClearMovie(movie);
    renderer = value;
}
void StopMovies() { for (auto* movie = firstMovie; movie; movie = movie->next) movie->StopPlaying(); }
Movie::Movie() : next(firstMovie) { firstMovie = this; }
Movie::~Movie() {
    StopPlaying();
    auto** link = &firstMovie;
    while (*link && *link != this) link = &(*link)->next;
    if (*link) *link = next;
}
bool Movie::BeginPlay(const char* name, bool stopSounds, bool hideCursor, bool loop) {
    StopPlaying();
    if (!name || !*name) return false;
    MSG quit;
    if (PeekMessage(&quit, nullptr, WM_QUIT, WM_QUIT, PM_NOREMOVE)) return false;
    // Preserve the host's resolution order; FFmpeg receives only selected bytes.
    const std::string path = ModRuntime::MoviePath(name);
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const auto length = file ? std::streamoff(file.tellg()) : std::streamoff(-1);
    if (length <= 0 || uint64_t(length) > size_t(-1)) {
        EngineDiagnostics::Event("Video open failed resource=%s reason=file", name);
        return false;
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), length)) {
        EngineDiagnostics::Event("Video open failed resource=%s reason=read", name);
        return false;
    }
    if (stopSounds && theWorld.GetSoundManager()) theWorld.GetSoundManager()->StopAllSounds(true);
    std::unique_ptr<Player> candidate(new Player);
    if (!candidate->Open(std::move(bytes), loop)) return false;
    player = std::move(candidate);
    presentation.loading = loop;
    missingRendererLogged = false;
    restoreCursor = hideCursor && theCursor.IsShown();
    if (hideCursor) theCursor.Show(FALSE);
    player->Play();
    return true;
}
bool Movie::Submit(std::shared_ptr<const Frame> frame) {
    lastFrame = frame;
    if (renderer) return renderer->SubmitMovie(this, std::move(frame), presentation);
    if (!missingRendererLogged) {
        EngineDiagnostics::Event("Video frame not drawn: renderer endpoint unavailable (P24)");
        missingRendererLogged = true;
    }
    // Explicit discard, no rendering fallback. Keep playback/skip/end alive.
    return true;
}
bool Movie::PlayNextFrame(bool present) {
    if (!player) return false;
    presentation.present = present;
    const auto state = player->GetState();
    if (state == State::Ended) {
        if (lastFrame) Submit(lastFrame); // framePause / credits hold last image
        return false;
    }
    if (Inactive()) player->Pause();
    else if (state == State::Paused) player->Play();
    if (!player->Update(*this)) return false;
    return player->GetState() != State::Ended;
}
void Movie::StopPlaying() {
    // Join the decoder and release PCM before any audio shutdown.
    if (player) player->Cancel();
    player.reset();
    lastFrame.reset();
    if (renderer) renderer->ClearMovie(this);
    if (restoreCursor) { theCursor.Show(TRUE); restoreCursor = false; }
}
bool Movie::Play(const char* name, unsigned long framePause, bool skipMovie,
                 bool skipFrame, bool clearSurface, bool stopSounds, bool useOverlays) {
    // These two legacy arguments were unused by the previous implementation.
    (void)clearSurface; (void)useOverlays;
    Movie video;
    if (!video.BeginPlay(name, stopSounds)) return false;
    bool done = false, interrupted = false, holding = false;
    ULONGLONG pauseElapsed = 0, lastTick = GetTickCount64();
    while (!done) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                PostQuitMessage(int(msg.wParam));
                done = interrupted = true;
                break;
            }
            const bool input = !AgentBackground::Enabled() && (msg.message == WM_KEYDOWN || msg.message == WM_LBUTTONDOWN || msg.message == WM_RBUTTONDOWN);
            if (input && (skipMovie || (holding && skipFrame))) {
                if (video.player) video.player->Skip();
                done = interrupted = true;
                break;
            }
            if (msg.message >= WM_KEYFIRST && msg.message <= WM_KEYLAST) continue;
            if (msg.message >= WM_MOUSEFIRST && msg.message <= WM_MOUSELAST) continue;
            TranslateMessage(&msg); DispatchMessage(&msg);
        }
        if (done) break;
        if (!IsWindow(NGame::hWnd) || !video.player) { interrupted = true; break; }
        const ULONGLONG now = GetTickCount64();
        if (holding && !Inactive()) pauseElapsed += now - lastTick;
        lastTick = now;
        const bool playing = video.PlayNextFrame(true);
        if (!video.player || video.player->GetState() == State::Failed) { interrupted = true; break; }
        if (!playing) { holding = true; if (pauseElapsed >= framePause) done = true; }
        Sleep(Inactive() ? 10 : 1);
    }
    video.StopPlaying();
    NGame::DestroyInputMessages();
    return !interrupted;
}
}
