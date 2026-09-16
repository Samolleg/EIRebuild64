#pragma once

#include <string> //save error
#include <Windows.h>
#include <Dwmapi.h>
#include <memory>
#include <SpriteFont.h>
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include <d3d11.h>
#pragma comment(lib, "d3d11.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam,
                                                             LPARAM lParam);

class Paint
{
private:
    ID3D11Device *g_pd3dDevice = NULL;
    ID3D11DeviceContext *g_pd3dDeviceContext = NULL;
    IDXGISwapChain *g_pSwapChain = NULL;
    ID3D11RenderTargetView *g_mainRenderTargetView = NULL;

    // std::unique_ptr<DirectX::SpriteFont> m_font;
    // std::unique_ptr<DirectX::SpriteBatch> m_spriteBatch;

    HWND targetWnd;
    int width;
    int height;

    int d3dInit(HWND hWnd);
    void CreateRenderTarget();

public:
    Paint();
    void Cleanup();
    Paint(HWND hWnd, HWND targetWnd, int width, int height);
    int render();
};