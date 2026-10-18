#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
#include "BinkVideo.h"
#include "OwnedRender/E1World.h"
#include "GameMain.h"
#include "I3DCursor.h"
#include "WorldClient.h"
#include "SoundManager.h"
#include "Renderer.h"
#include "dx11/RendererDX11.h"
#include "MiniaudioOneShotSfx.h"
#include "ModRuntime.h"
#include "ModPackage.h"

// Only the stable public prefix is shared by the retail and newer Bink SDKs.
// Do not use the SDK 1.9 structure to access the retail 1.0 private fields.
struct CBinkVideo::Player
{
    struct Header { unsigned width, height, frames, frame; };
    HMODULE module = nullptr;
    Header *movie = nullptr;
    Header *(__stdcall *open)(const char *, unsigned) = nullptr;
    void (__stdcall *close)(Header *) = nullptr;
    int (__stdcall *decode)(Header *) = nullptr;
    void (__stdcall *next)(Header *) = nullptr;
    int (__stdcall *wait)(Header *) = nullptr;
    int (__stdcall *copy)(Header *, void *, int, unsigned, unsigned, unsigned, unsigned) = nullptr;
    void (__stdcall *seek)(Header *, unsigned, int) = nullptr;
    int (__stdcall *pause)(Header *, int) = nullptr;
    std::vector<unsigned char> pixels;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
    bool decoded = false, finished = false, paused = false, loadingPicture = false;
    std::string moviePath;
    std::shared_ptr<owned::ProgressMovieFrames> progressSource;
    std::uint64_t imageGeneration = 0;
    unsigned displayedFrame = 0;
    std::unique_ptr<OwnedSpriteBatch> batch;
    ~Player() { if (movie) close(movie); if (module) FreeLibrary(module); }
    FARPROC Symbol(const char *decorated, const char *plain)
    {
        FARPROC p = GetProcAddress(module, decorated);
        return p ? p : GetProcAddress(module, plain);
    }
    bool Load()
    {
        // The game may already have loaded Bink. The standalone EXE can also
        // live outside the game directory, with the game as working directory.
        bool selected=false;
        const auto* mod=ModRuntime::Resources();const std::string candidate=mod?mod->Directory()+"\\binkw32.dll":std::string();
        if(!ModSettingsPolicy::ModBink(candidate,
            [](const std::string& p){DWORD a=GetFileAttributesA(p.c_str());return a!=INVALID_FILE_ATTRIBUTES&&!(a&FILE_ATTRIBUTE_DIRECTORY);},
            [&](const std::string& p){module=LoadLibraryA(p.c_str());return module!=nullptr;},selected))return false;
        if(!selected)GetModuleHandleExA(0, "binkw32.dll", &module);
        if (!module)
        {
            char path[MAX_PATH] = {};
            DWORD n = GetModuleFileNameA(nullptr, path, MAX_PATH);
            if (n && n < MAX_PATH)
            {
                char *slash = strrchr(path, '\\');
                if (slash && slash - path + 13 < MAX_PATH)
                {
                    strcpy(slash + 1, "binkw32.dll");
                    module = LoadLibraryA(path);
                }
            }
        }
        if (!module)
        {
            char path[MAX_PATH] = {};
            DWORD n = GetCurrentDirectoryA(MAX_PATH, path);
            if (n && n < MAX_PATH && n + 12 < MAX_PATH)
            {
                if (n && path[n - 1] != '\\') path[n++] = '\\';
                strcpy(path + n, "binkw32.dll");
                module = LoadLibraryA(path);
            }
        }
        if (!module) return false;
        open = reinterpret_cast<decltype(open)>(Symbol("_BinkOpen@8", "BinkOpen"));
        close = reinterpret_cast<decltype(close)>(Symbol("_BinkClose@4", "BinkClose"));
        decode = reinterpret_cast<decltype(decode)>(Symbol("_BinkDoFrame@4", "BinkDoFrame"));
        next = reinterpret_cast<decltype(next)>(Symbol("_BinkNextFrame@4", "BinkNextFrame"));
        wait = reinterpret_cast<decltype(wait)>(Symbol("_BinkWait@4", "BinkWait"));
        copy = reinterpret_cast<decltype(copy)>(Symbol("_BinkCopyToBuffer@28", "BinkCopyToBuffer"));
        seek = reinterpret_cast<decltype(seek)>(Symbol("_BinkGoto@12", "BinkGoto"));
        pause = reinterpret_cast<decltype(pause)>(Symbol("_BinkPause@8", "BinkPause"));
        return open && close && decode && next && wait && copy && seek && pause;
    }
    void SelectSilentAudioTrack()
    {
        // Retail 1.0t uses one track ID; -1 explicitly selects no sound.
        auto single = reinterpret_cast<void (__stdcall *)(unsigned)>(
            GetProcAddress(module, "_BinkSetSoundTrack@4"));
        if (single) single(0xFFFFFFFFu);
        else
        {
            auto multiple = reinterpret_cast<void (__stdcall *)(unsigned, unsigned *)>(
                GetProcAddress(module, "_BinkSetSoundTrack@8"));
            if (multiple) multiple(0, nullptr);
        }
    }
    unsigned ConfigureAudio(const char *name)
    {
        MiniaudioOneShotSfx::BeginBinkAudioMovie(name);
        auto set = reinterpret_cast<int (__stdcall *)(void *, uintptr_t)>(
            Symbol("_BinkSetSoundSystem@8", "BinkSetSoundSystem"));
        auto sound = theWorld.GetSoundManager();
        if (set && sound && MiniaudioOneShotSfx::IsSupportedBinkAudioAbi(reinterpret_cast<void *>(set)))
        {
            MiniaudioOneShotSfx::PrepareNativeManager(sound);
            void *adapter = MiniaudioOneShotSfx::GetBinkMiniaudioSystemOpen();
            if (adapter && set(adapter, 0))
            {
                return 0;
            }
        }
        MiniaudioOneShotSfx::MuteBinkAudioMovie("adapter_unavailable");
        SelectSilentAudioTrack();
        // BINKSNDTRACK applies the explicit disabled track selection.
        return 0x00004000u;
    }
    // A separate, silent decoder keeps the selected movie alive after Clear
    // joins the loading player. The renderer requests only adjacent frames.
    struct FrameSource : owned::ProgressMovieFrames {
        HMODULE module = nullptr;
        Header* movie = nullptr;
        Header* (__stdcall* open)(const char*, unsigned);
        void (__stdcall* close)(Header*);
        int (__stdcall* decode)(Header*);
        int (__stdcall* copy)(Header*, void*, int, unsigned, unsigned, unsigned, unsigned);
        void (__stdcall* seek)(Header*, unsigned, int);
        int (__stdcall* sound)(Header*, int) = nullptr;
        std::string path;
        unsigned width, height, frames;
        std::vector<unsigned char> pixels;
        explicit FrameSource(Player& p) : open(p.open), close(p.close), decode(p.decode),
            copy(p.copy), seek(p.seek), path(p.moviePath), width(p.movie->width),
            height(p.movie->height), frames(p.movie->frames) {
            if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                    reinterpret_cast<LPCSTR>(p.open), &module)) return;
            sound = reinterpret_cast<decltype(sound)>(p.Symbol("_BinkSetSoundOnOff@8", "BinkSetSoundOnOff"));
        }
        ~FrameSource() override { if (movie) close(movie); if (module) FreeLibrary(module); }
        bool read(std::uint32_t frame, std::vector<std::uint8_t>& bgra) override {
            if (!module || !sound || frame >= frames) return false;
            if (!movie) {
                movie = open(path.c_str(), 0);
                if (!movie) return false;
                sound(movie, 0);
                if (movie->width != width || movie->height != height || movie->frames != frames) return false;
            }
            if (movie->width != width || movie->height != height || movie->frames != frames) return false;
            pixels.resize(std::size_t(width) * height * 4);
            seek(movie, frame + 1, 0);
            decode(movie);
            copy(movie, pixels.data(), width * 4, height, 0, 0, 0x80000003u);
            for (std::size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 255;
            bgra = pixels;
            return true;
        }
    };
    void PublishProgress() {
        if (!loadingPicture || !displayedFrame) return;
        if (!progressSource) progressSource = std::make_shared<FrameSource>(*this);
        owned::ProgressMovie value;
        value.path = moviePath; value.width = movie->width; value.height = movie->height;
        value.frames = movie->frames;
        // Bridge convention: one past the 1-based displayed frame.
        value.current = displayedFrame + 1;
        value.source = progressSource;
        OwnedRender::E1SetProgressMovie(value);
    }
    void Decode()
    {
        const auto renderAccess = theRenderer.GetD3D()->GetOwnedRenderer().LockFrame();
        decode(movie);
        copy(movie, pixels.data(), movie->width * 4, movie->height, 0, 0, 0x80000003u);
        // BINKSURFACE32 is BGRX; SpriteBatch requires opaque alpha.
        for (size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 255;
        theRenderer.GetD3D()->GetDeviceContext()->UpdateSubresource(
            texture.Get(), 0, nullptr, pixels.data(), movie->width * 4, 0);
        decoded = true;
    }
    void Draw(const CRect &rect, bool present)
    {
        if (!decoded) return;
        auto dx = theRenderer.GetD3D();
        const auto renderAccess = dx->GetOwnedRenderer().LockFrame();
        RECT dest = {rect.left, rect.top, rect.right, rect.bottom};
        if (present)
        {
            // The embedded renderer has no RDDraw UI host: present the frame
            // as a renderer image (fitted to the output; also the loading
            // picture the renderer holds while the next world prepares).
            dx->BeginFrame();
            PublishProgress();
            dx->ClearColorBuffer(0);
            dx->PresentLoadingImage(pixels, movie->width, movie->height,
                (std::uint64_t(1) << 62) | std::uint64_t(reinterpret_cast<std::uintptr_t>(this)),
                ++imageGeneration);
            dx->EndFrame();
        }
        else dx->GetSpriteBatch()->Draw(view.Get(), dest);
    }
};

bool CBinkVideo::DecoderAvailable() { Player probe; return probe.Load(); }

CBinkVideo::CBinkVideo() : player(nullptr), rectOnScreen(0, 0, 0, 0), bWasCursorHided(false) {}
CBinkVideo::~CBinkVideo() { StopPlaying(); }

bool CBinkVideo::BeginPlay(LPCSTR name, bool stopSounds, bool hideCursor)
{
    StopPlaying();
    if (!name || !*name || !theRenderer.GetD3D()) return false;
    std::unique_ptr<Player> p(new Player);
    if (!p->Load()) return false;
    if (stopSounds && theWorld.GetSoundManager()) theWorld.GetSoundManager()->StopAllSounds(true);
    const unsigned audioFlags = p->ConfigureAudio(name);
    std::string moviePath = ModRuntime::MoviePath(name);
    p->movie = p->open(moviePath.c_str(), audioFlags);
    if (!p->movie && MiniaudioOneShotSfx::HasBinkAudioMovieFailed() && audioFlags == 0)
    {
        p->SelectSilentAudioTrack();
        p->movie = p->open(moviePath.c_str(), 0x00004000u);
    }
    if (p->movie && MiniaudioOneShotSfx::HasBinkAudioMovieFailed())
    {
        auto mute = reinterpret_cast<int (__stdcall *)(Player::Header *, int)>(
            p->Symbol("_BinkSetSoundOnOff@8", "BinkSetSoundOnOff"));
        if (mute) mute(p->movie, 0);
    }
    if (!p->movie) return false;
    p->moviePath = moviePath;
    unsigned w = p->movie->width, h = p->movie->height;
    if (!w || !h || w > 8192 || h > 8192 || !p->movie->frames) return false;
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = w; desc.Height = h; desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    auto device = theRenderer.GetD3D()->GetDevice();
    if (FAILED(device->CreateTexture2D(&desc, nullptr, p->texture.GetAddressOf())) ||
        FAILED(device->CreateShaderResourceView(p->texture.Get(), nullptr, p->view.GetAddressOf()))) return false;
    p->pixels.resize(size_t(w) * h * 4);
    DWORD sw = 0, sh = 0;
    theRenderer.GetCurrentMode(sw, sh);
    // Preserve movie aspect ratio at 4K and widescreen resolutions.
    double scale = (std::min)(double(sw) / w, double(sh) / h);
    int dw = int(w * scale), dh = int(h * scale);
    rectOnScreen = CRect((int(sw)-dw)/2, (int(sh)-dh)/2, (int(sw)+dw)/2, (int(sh)+dh)/2);
    player = p.release();
    bWasCursorHided = hideCursor && theCursor.IsShown();
    if (hideCursor) theCursor.Show(FALSE);
    return true;
}

bool CBinkVideo::PlayNextFrame(bool present, bool freeze)
{
    if (!player) return false;
    Player &p = *player;
    if (!p.finished && !p.paused && !p.wait(p.movie))
    {
        if (p.decoded && p.displayedFrame >= p.movie->frames && !freeze) p.finished = true;
        else
        {
            p.Decode();
            p.displayedFrame = p.movie->frame;
            // Loading screens loop (BinkNextFrame wraps after the last frame),
            // as the retail wait screen does for the whole load.
            if (freeze || p.displayedFrame < p.movie->frames) p.next(p.movie);
        }
    }
    p.Draw(rectOnScreen, present);
    return !p.finished || freeze;
}

bool CBinkVideo::Play(LPCSTR name, DWORD framePause, bool skipMovie,
                      bool skipFrame, bool clearSurface, bool stopSounds, bool useOverlays)
{
    CBinkVideo video;
    if (!video.BeginPlay(name, stopSounds)) return false;
    bool done = false, interrupted = false;
    DWORD pauseStart = 0;
    bool holding = false;
    while (!done)
    {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                PostQuitMessage(int(msg.wParam));
                done = interrupted = true;
                break;
            }
            bool input = msg.message == WM_KEYDOWN || msg.message == WM_LBUTTONDOWN || msg.message == WM_RBUTTONDOWN;
            if (input && (skipMovie || (holding && skipFrame))) { done = interrupted = true; break; }
            if (msg.message >= WM_KEYFIRST && msg.message <= WM_KEYLAST) continue;
            if (msg.message >= WM_MOUSEFIRST && msg.message <= WM_MOUSELAST) continue;
            TranslateMessage(&msg); DispatchMessage(&msg);
        }
        if (done || !IsWindow(NGame::hWnd)) break;
        bool inactive = IsIconic(NGame::hWnd) || GetForegroundWindow() != NGame::hWnd;
        if (inactive != video.player->paused)
        {
            video.player->pause(video.player->movie, inactive ? 1 : 0);
            video.player->paused = inactive;
        }
        if (inactive) { Sleep(10); continue; }
        if (!video.PlayNextFrame(true))
        {
            if (!holding) { holding = true; pauseStart = GetTickCount(); }
            if (GetTickCount() - pauseStart >= framePause) done = true;
        }
        Sleep(1);
    }
    video.StopPlaying();
    NGame::DestroyInputMessages();
    return !interrupted;
}

void CBinkVideo::StopPlaying()
{
    if (player && player->loadingPicture) OwnedRender::E1SetProgressMovie({});
    delete player; player = nullptr;
    if (bWasCursorHided) { theCursor.Show(TRUE); bWasCursorHided = false; }
}
int CBinkVideo::GetFramesCount() { return player ? int(player->movie->frames) : 0; }
void CBinkVideo::SetScreenPlace(CPoint p)
{
    if (player) rectOnScreen = CRect(p.x, p.y, p.x + player->movie->width, p.y + player->movie->height);
}
void CBinkVideo::DrawFrame(int frame, bool blit, bool present)
{
    if (!player || frame < 1 || unsigned(frame) > player->movie->frames) return;
    player->seek(player->movie, unsigned(frame), 0);
    player->finished = false;
    player->Decode();
    player->displayedFrame = player->movie->frame;
    if (player->displayedFrame < player->movie->frames) player->next(player->movie);
    if (blit) player->Draw(rectOnScreen, present);
}
void CBinkVideo::MarkLoadingPicture()
{
    if (player) player->loadingPicture = true;
}
