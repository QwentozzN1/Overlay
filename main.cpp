#include <windows.h>
#include <d3d11.h>
#include <tchar.h>
#include <chrono>
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

int selectedCrosshair = 1; // По умолчанию выбираем крестик, чтобы толщина была наглядна
float crosshairSize = 6.0f;
float crosshairThickness = 2.0f; // Новая переменная: толщина линий прицела
float crosshairColor[4] = { 0.0f, 1.0f, 0.4f, 1.0f };
bool enableOverlayDraw = true;
bool showMenu = true;
int selectedMap = 0; // 0 - Mirage, 1 - Dust II

float roundTimeRemaining = 115.0f;
bool roundTimerActive = false;
auto lastTimerTick = std::chrono::steady_clock::now();

static ID3D11Device*           g_pd3dDevice = NULL;
static ID3D11DeviceContext*    g_pd3dDeviceContext = NULL;
static IDXGISwapChain*         g_pSwapChain = NULL;
static ID3D11RenderTargetView* g_mainRenderTargetView = NULL;
static HWND                    g_hwnd = NULL;

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

void ToggleClickThrough(bool enable)
{
    if (!g_hwnd) return;
    LONG_PTR exStyle = GetWindowLongPtr(g_hwnd, GWL_EXSTYLE);
    if (enable)
        SetWindowLongPtr(g_hwnd, GWL_EXSTYLE, exStyle | WS_EX_TRANSPARENT);
    else
        SetWindowLongPtr(g_hwnd, GWL_EXSTYLE, exStyle & ~WS_EX_TRANSPARENT);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"CS2PracticeOverlay";
    ::RegisterClassExW(&wc);

    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    g_hwnd = ::CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED,
        wc.lpszClassName,
        L"CS2 Practice Assistant",
        WS_POPUP,
        0, 0, screenWidth, screenHeight,
        NULL, NULL, hInstance, NULL
    );

    if (!g_hwnd)
    {
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    SetLayeredWindowAttributes(g_hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);

    if (!CreateDeviceD3D(g_hwnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ::ShowWindow(g_hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(g_hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.10f, 0.15f, 0.95f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.1f, 0.6f, 1.0f, 0.3f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.1f, 0.5f, 0.9f, 0.8f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.2f, 0.6f, 1.0f, 1.0f);

    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    ToggleClickThrough(false);

    bool done = false;
    while (!done)
    {
        MSG msg;
        while (::PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done) break;

        auto now = std::chrono::steady_clock::now();
        std::chrono::duration<float> elapsed = now - lastTimerTick;
        lastTimerTick = now;

        if (roundTimerActive && roundTimeRemaining > 0.0f)
        {
            roundTimeRemaining -= elapsed.count();
            if (roundTimeRemaining <= 0.0f)
            {
                roundTimeRemaining = 0.0f;
                roundTimerActive = false;
            }
        }

        static bool insertPressedLastFrame = false;
        bool insertPressedCurrentFrame = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
        if (insertPressedCurrentFrame && !insertPressedLastFrame)
        {
            showMenu = !showMenu;
            ToggleClickThrough(!showMenu);
        }
        insertPressedLastFrame = insertPressedCurrentFrame;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        if (enableOverlayDraw)
        {
            ImDrawList* drawList = ImGui::GetBackgroundDrawList();
            ImVec2 center((float)screenWidth / 2.0f, (float)screenHeight / 2.0f);
            ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(crosshairColor[0], crosshairColor[1], crosshairColor[2], crosshairColor[3]));

            if (selectedCrosshair == 0)
            {
                // Точка (для точки размер регулирует радиус)
                drawList->AddCircleFilled(center, crosshairSize, col);
            }
            else if (selectedCrosshair == 1)
            {
                // Крестик с учетом размера и толщины
                float len = crosshairSize * 3.0f;
                drawList->AddLine(ImVec2(center.x - len, center.y), ImVec2(center.x + len, center.y), col, crosshairThickness);
                drawList->AddLine(ImVec2(center.x, center.y - len), ImVec2(center.x, center.y + len), col, crosshairThickness);
            }
        }

        if (showMenu)
        {
            ImGui::SetNextWindowPos(ImVec2(100, 100), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(480, 480), ImGuiCond_FirstUseEver);
            ImGui::Begin("CS2 // PRACTICE & UTILITY ASSISTANT [INSERT]", &done, ImGuiWindowFlags_NoResize);
            
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "STATUS: ACTIVE [Practice Mode]");
            ImGui::Text("Press INSERT to toggle menu");
            ImGui::Separator();

            if (ImGui::CollapsingHeader("Crosshair Settings", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Checkbox("Enable Custom Crosshair", &enableOverlayDraw);
                const char* crosshairs[] = { "Dot", "Cross" };
                ImGui::Combo("Shape", &selectedCrosshair, crosshairs, 2);
                ImGui::SliderFloat("Size", &crosshairSize, 1.0f, 15.0f);
                ImGui::SliderFloat("Thickness", &crosshairThickness, 1.0f, 6.0f); // Ползунок толщины
                ImGui::ColorEdit4("Color", crosshairColor);
            }

            if (ImGui::CollapsingHeader("Round Timer (Practice)", ImGuiTreeNodeFlags_DefaultOpen))
            {
                int minutes = (int)(roundTimeRemaining) / 60;
                int seconds = (int)(roundTimeRemaining) % 60;
                ImGui::Text("Time Left: %02d:%02d", minutes, seconds);

                if (ImGui::Button(roundTimerActive ? "Pause Timer" : "Start Round Timer"))
                {
                    roundTimerActive = !roundTimerActive;
                }
                ImGui::SameLine();
                if (ImGui::Button("Reset (1:55)"))
                {
                    roundTimeRemaining = 115.0f;
                    roundTimerActive = false;
                }
            }

            if (ImGui::CollapsingHeader("Grenade Utility Guide (Nade Spots)", ImGuiTreeNodeFlags_DefaultOpen))
            {
                const char* maps[] = { "Mirage", "Dust II" };
                ImGui::Combo("Select Map", &selectedMap, maps, 2);
                ImGui::Separator();

                if (selectedMap == 0)
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Mirage Key Smokes:");
                    ImGui::BulletText("Window Smoke: Stand at T-Spawn trash cans, aim at top ledge, Left+Right Click jumpthrow.");
                    ImGui::BulletText("B-Apps Smoke: Stand near market corner, aim at antenna, Left Click throw.");
                    ImGui::BulletText("Connector Smoke: T-Roof corner, aim above roof edge, Jumpthrow.");
                }
                else
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Dust II Key Smokes:");
                    ImGui::BulletText("A-Cross (Xbox) Smoke: Stand by T-Spawn doors, aim at electrical box edge, Jumpthrow.");
                    ImGui::BulletText("B-Doors Smoke: Stand outside B-Tunnels wall, aim at minaret top, Left Click throw.");
                }
            }

            ImGui::End();
        }

        ImGui::Render();
        const float clear_color_with_alpha[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(g_hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}

bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    HRESULT res = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, createDeviceFlags, featureLevelArray, 2,
        D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED)
        res = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_WARP, NULL, createDeviceFlags, featureLevelArray, 2,
            D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK) return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = NULL; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = NULL; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = NULL; }
}

void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget() 
{ 
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = NULL; } 
}

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (g_pd3dDevice != NULL && wParam != SIZE_MINIMIZED)
        {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}