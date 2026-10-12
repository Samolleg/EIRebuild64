#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
#include "BinkVideo.h"
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
    bool decoded = false, finished = false, paused = false;
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
    unsigned ConfigureAudio(const char *name)
    {
        MiniaudioOneShotSfx::BeginBinkAudioMovie(name);
        auto set = reinterpret_cast<int (__stdcall *)(void *, uintptr_t)>(
            Symbol("_BinkSetSoundSystem@8", "BinkSetSoundSystem"));
        auto tracks = reinterpret_cast<void (__stdcall *)(unsigned, unsigned *)>(
            Symbol("_BinkSetSoundTrack@8", "BinkSetSoundTrack"));
        auto sound = theWorld.GetSoundManager();
        if (set && sound && MiniaudioOneShotSfx::IsSupportedBinkAudioAbi(reinterpret_cast<void *>(set)))
        {
            MiniaudioOneShotSfx::PrepareNativeManager(sound);
            void *adapter = MiniaudioOneShotSfx::GetBinkMiniaudioSystemOpen();
            if (adapter && set(adapter, 0))
            {
                unsigned track = 0;
                if (tracks) tracks(1, &track);
                return 0;
            }
        }
        MiniaudioOneShotSfx::MuteBinkAudioMovie("adapter_unavailable");
        if (tracks) tracks(0, nullptr);
        // BINKSNDTRACK: the explicit empty track selection keeps video silent.
        return 0x00004000u;
    }
    void Decode()
    {
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
        RECT dest = {rect.left, rect.top, rect.right, rect.bottom};
        if (present)
        {
            // A separate batch also allows movie sequences invoked from an UI draw callback.
            if (!batch) batch.reset(new OwnedSpriteBatch(dx->GetOwnedRenderer()));
            dx->BeginFrame();
            dx->ClearColorBuffer(0);
            batch->Begin(DirectX::SpriteSortMode_Immediate);
            batch->Draw(view.Get(), dest);
            batch->End();
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
        auto tracks = reinterpret_cast<void (__stdcall *)(unsigned, unsigned *)>(
            p->Symbol("_BinkSetSoundTrack@8", "BinkSetSoundTrack"));
        if (tracks) tracks(0, nullptr);
        p->movie = p->open(moviePath.c_str(), 0x00004000u);
    }
    if (p->movie && MiniaudioOneShotSfx::HasBinkAudioMovieFailed())
    {
        auto mute = reinterpret_cast<int (__stdcall *)(Player::Header *, int)>(
            p->Symbol("_BinkSetSoundOnOff@8", "BinkSetSoundOnOff"));
        if (mute) mute(p->movie, 0);
    }
    if (!p->movie) return false;
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
        if (p.decoded && p.displayedFrame >= p.movie->frames) p.finished = true;
        else
        {
            p.Decode();
            p.displayedFrame = p.movie->frame;
            if (p.displayedFrame < p.movie->frames) p.next(p.movie);
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
