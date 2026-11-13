#include "pch.h"
#include <Windows.h>
#include <iostream>
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <array>    
#include <mutex>
#include "MinHook.h"
#pragma comment(lib, "d3dcompiler.lib")

#include <cmath>
#include "buttons.hpp"
#include "client_dll.hpp"
#include "offsets.h"

#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

std::vector<UINT> g_LoggedIndexCounts;

template <typename T>
T read(uintptr_t address) {
    if (!address) return T{};
    __try {
        return *reinterpret_cast<T*>(address);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return T{};
    }
}

template <typename T>
void write(uintptr_t address, T value) {
    if (!address) return;
    __try {
        *reinterpret_cast<T*>(address) = value;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static ID3D11Device* pd3dDevice = nullptr;
static ID3D11DeviceContext* pd3dDeviceContext = nullptr;
static IDXGISwapChain* swapChain = nullptr;
static ID3D11RenderTargetView* mainRenderTargetView = nullptr;
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

bool CreateDeviceD3D(HWND hWnd) {
    RECT rc;
    GetClientRect(hWnd, &rc);
    if (rc.right == 0 || rc.bottom == 0) {
        printf("Window has zero size! Aborting.\n");
        return false;
    }

    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 1;
    sd.BufferDesc.Width = rc.right;
    sd.BufferDesc.Height = rc.bottom;
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

    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
        D3D_FEATURE_LEVEL_9_3,
        D3D_FEATURE_LEVEL_9_2,
        D3D_FEATURE_LEVEL_9_1
    };

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0,
        featureLevelArray, _countof(featureLevelArray),
        D3D11_SDK_VERSION, &sd, &swapChain, &pd3dDevice,
        &featureLevel, &pd3dDeviceContext
    );

    if (FAILED(hr)) {
        printf("DX11 Init Failed! HRESULT: 0x%X\n", hr);
        return false;
    }

    ID3D11Texture2D* pBackBuffer = nullptr;
    if (FAILED(swapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer))) || !pBackBuffer)
        return false;

    pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &mainRenderTargetView);
    pBackBuffer->Release();
    return true;
}

void InitImGui(HWND hWnd) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(hWnd);
    ImGui_ImplDX11_Init(pd3dDevice, pd3dDeviceContext);
}

struct Vector3 { float x, y, z; };
struct Vector2 { float x, y; };
struct QAngle { float x, y, z; };
struct oldOrigin { float cords[3]; };
struct nameBuf { char data[128]; };

struct CheatSettings {
    bool espEnabled = true; // Added ESP toggle
    bool teamCheck = true;
    bool triggerbotEnabled = false;
    bool aimbotEnabled = false;
    bool bhopEnabled = true;

    float aimbotSmooth = 3.5f;
    float aimbotFov = 5.0f;

    bool isTriggering = false;
} settings;

bool getBonePosition(std::uintptr_t pawn, int boneIndex, Vector3& out, uintptr_t boneArray) {
    if (boneIndex < 0 || boneIndex > 127)
        return false;
    Vector3 bone = read<Vector3>(boneArray + (boneIndex * 0x20));

    if (std::isnan(bone.x) || std::isnan(bone.y) || std::isnan(bone.z))
        return false;
    if (bone.x == 0.0f && bone.y == 0.0f && bone.z == 0.0f)
        return false;
    if (std::abs(bone.x) > 15000.0f || std::abs(bone.y) > 15000.0f || std::abs(bone.z) > 15000.0f)
        return false;

    out.x = bone.x;
    out.y = bone.y;
    out.z = bone.z;
    return true;
}

bool worldToScreen(const Vector3& pos, Vector2& screen, const float matrix[16], int windowWidth, int windowHeight) {
    float w = matrix[12] * pos.x + matrix[13] * pos.y + matrix[14] * pos.z + matrix[15];
    if (w < 0.01f)
        return false;

    float x = matrix[0] * pos.x + matrix[1] * pos.y + matrix[2] * pos.z + matrix[3];
    float y = matrix[4] * pos.x + matrix[5] * pos.y + matrix[6] * pos.z + matrix[7];

    screen.x = (windowWidth / 2.0f) * (x / w + 1.0f);
    screen.y = (windowHeight / 2.0f) * (1.0f - (y / w));
    return true;
}

QAngle CalcAngle(const Vector3& src, const Vector3& dst) {
    QAngle angles;
    Vector3 delta = { dst.x - src.x, dst.y - src.y, dst.z - src.z };
    float hyp = sqrt(delta.x * delta.x + delta.y * delta.y);
    angles.x = atan2f(-delta.z, hyp) * (180.0f / 3.14159265358979323846f);
    angles.y = atan2f(delta.y, delta.x) * (180.0f / 3.14159265358979323846f);
    angles.z = 0.0f;
    return angles;
}

void HandleInputs() {
    // Shifted hotkeys up by one
    if (GetAsyncKeyState(VK_F1) & 1) settings.espEnabled = !settings.espEnabled;
    if (GetAsyncKeyState(VK_F2) & 1) settings.teamCheck = !settings.teamCheck;
    if (GetAsyncKeyState(VK_F3) & 1) settings.triggerbotEnabled = !settings.triggerbotEnabled;
    if (GetAsyncKeyState(VK_F4) & 1) settings.aimbotEnabled = !settings.aimbotEnabled;
    if (GetAsyncKeyState(VK_F5) & 1) settings.bhopEnabled = !settings.bhopEnabled;

    if (GetAsyncKeyState(VK_OEM_MINUS) & 1) {
        settings.aimbotSmooth -= 0.5f;
        if (settings.aimbotSmooth < 1.0f) settings.aimbotSmooth = 1.0f;
    }
    if (GetAsyncKeyState(VK_OEM_PLUS) & 1) {
        settings.aimbotSmooth += 0.5f;
        if (settings.aimbotSmooth > 15.0f) settings.aimbotSmooth = 15.0f;
    }
    if (GetAsyncKeyState(VK_OEM_4) & 1) {
        settings.aimbotFov -= 1.0f;
        if (settings.aimbotFov < 1.0f) settings.aimbotFov = 1.0f;
    }
    if (GetAsyncKeyState(VK_OEM_6) & 1) {
        settings.aimbotFov += 1.0f;
        if (settings.aimbotFov > 45.0f) settings.aimbotFov = 45.0f;
    }
}

void RunBhop(uintptr_t local_pawn, uintptr_t clientBase) {
    if (!settings.bhopEnabled) return;

    const auto flags = read<std::uint32_t>(local_pawn + exitscam::schemas::client_dll::C_BaseEntity::m_fFlags);
    const bool in_air = flags & (1 << 0);
    const bool space_pressed = GetAsyncKeyState(VK_SPACE);
    const auto force_jump = read<DWORD>(clientBase + exitscam::buttons::jump);

    if (space_pressed && in_air) {
        Sleep(5);
        write(clientBase + exitscam::buttons::jump, 65537);
    }
    else if (space_pressed && !in_air) {
        write(clientBase + exitscam::buttons::jump, 256);
    }
    else if (!space_pressed && force_jump == 65537) {
        write(clientBase + exitscam::buttons::jump, 256);
    }
}

void RunAimbot(uintptr_t clientBase, uintptr_t local_pawn, const std::vector<uintptr_t>& cachedPawns, std::unordered_map<uintptr_t, uintptr_t>& boneArrayCache, const Vector3& localEye, int localteam) {
    if (!settings.aimbotEnabled || !(GetAsyncKeyState(VK_RBUTTON) & 0x8000)) return;

    float bestFov = settings.aimbotFov;
    Vector3 bestTargetPos = { 0, 0, 0 };
    bool targetFound = false;
    QAngle currentAngle = read<QAngle>(clientBase + exitscam::offsets::client_dll::dwViewAngles);

    for (uintptr_t CSPlayerPawnPtr : cachedPawns) {
        int team = read<int>(CSPlayerPawnPtr + exitscam::schemas::client_dll::C_BaseEntity::m_iTeamNum);
        int health = read<int>(CSPlayerPawnPtr + exitscam::schemas::client_dll::C_BaseEntity::m_iHealth);

        if (settings.teamCheck && team == localteam) continue;
        if (health <= 0 || health > 100) continue;

        uintptr_t boneArray = boneArrayCache[CSPlayerPawnPtr];
        Vector3 headPos;
        if (boneArray != 0 && getBonePosition(CSPlayerPawnPtr, 7, headPos, boneArray)) {
            bool isSpotted = read<bool>(CSPlayerPawnPtr + exitscam::schemas::client_dll::C_CSPlayerPawn::m_entitySpottedState + 0x8);
            if (!isSpotted) continue;

            QAngle targetAngle = CalcAngle(localEye, headPos);
            float fovX = std::abs(targetAngle.x - currentAngle.x);
            float fovY = std::abs(targetAngle.y - currentAngle.y);
            float totalFov = sqrt(fovX * fovX + fovY * fovY);

            if (totalFov < bestFov) {
                bestFov = totalFov;
                bestTargetPos = headPos;
                targetFound = true;
            }
        }
    }

    if (targetFound) {
        QAngle targetAngle = CalcAngle(localEye, bestTargetPos);
        while (targetAngle.y > 180.0f) targetAngle.y -= 360.0f;
        while (targetAngle.y < -180.0f) targetAngle.y += 360.0f;

        QAngle newAngle;
        newAngle.x = currentAngle.x + (targetAngle.x - currentAngle.x) / settings.aimbotSmooth;
        newAngle.y = currentAngle.y + (targetAngle.y - currentAngle.y) / settings.aimbotSmooth;
        newAngle.z = 0.0f;

        write(clientBase + exitscam::offsets::client_dll::dwViewAngles, newAngle);
    }
}

void RunTriggerbot(uintptr_t clientBase, uintptr_t local_pawn, uintptr_t entityList, int localTeam, int localHealth) {
    if (!settings.triggerbotEnabled) {
        if (settings.isTriggering) {
            write(clientBase + exitscam::buttons::attack, 256);
            settings.isTriggering = false;
        }
        return;
    }

    bool shouldShoot = false;
    if (localHealth > 0) {
        int crosshairEntIndex = read<int>(local_pawn + exitscam::schemas::client_dll::C_CSPlayerPawn::m_iIDEntIndex);
        if (crosshairEntIndex != -1) {
            uintptr_t triggerListEntry2 = read<uintptr_t>(entityList + 0x8 * ((crosshairEntIndex & 0x7FFF) >> 9) + 0x10);
            if (triggerListEntry2) {
                uintptr_t targetPawn = read<uintptr_t>(triggerListEntry2 + 0x70 * (crosshairEntIndex & 0x1FF));
                if (targetPawn) {
                    int targetHealth = read<int>(targetPawn + exitscam::schemas::client_dll::C_BaseEntity::m_iHealth);
                    int targetTeam = read<int>(targetPawn + exitscam::schemas::client_dll::C_BaseEntity::m_iTeamNum);
                    if (targetHealth > 0 && targetHealth <= 100) {
                        if (!settings.teamCheck || targetTeam != localTeam) shouldShoot = true;
                    }
                }
            }
        }
    }

    if (shouldShoot) {
        if (!settings.isTriggering) {
            Sleep(17 + (rand() % 12));
            write(clientBase + exitscam::buttons::attack, 65537);
            settings.isTriggering = true;
        }
    }
    else {
        if (settings.isTriggering) {
            write(clientBase + exitscam::buttons::attack, 256);
            settings.isTriggering = false;
        }
    }
}

typedef void(__stdcall* tDrawIndexed)(ID3D11DeviceContext*, UINT, UINT, INT);
tDrawIndexed oDrawIndexed = nullptr;

// Your chams shader (needs to be created after device creation)
ID3D11PixelShader* g_pChamsShader = nullptr;

/* void __stdcall hkDrawIndexed(ID3D11DeviceContext* pContext, UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation)
{
    // Do absolutely nothing. Just return.
    // If the game crashes now, the hook placement is wrong.
    return;
}
*/

ID3D11PixelShader* CreateSolidColorShader(ID3D11Device* device, float r, float g, float b, float a) {
    const char* shaderCode = R"(
        float4 main() : SV_Target {
            return float4(%f, %f, %f, %f);
        }
    )";

    char buffer[512];
    sprintf_s(buffer, shaderCode, r, g, b, a);

    ID3DBlob* pBlob = nullptr;
    ID3DBlob* pError = nullptr;

    HRESULT hr = D3DCompile(buffer, strlen(buffer), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &pBlob, &pError);

    if (FAILED(hr)) {
        printf("Shader compilation failed: %s\n", (char*)pError->GetBufferPointer());
        pError->Release();
        return nullptr;
    }

    ID3D11PixelShader* pShader = nullptr;
    device->CreatePixelShader(pBlob->GetBufferPointer(), pBlob->GetBufferSize(), nullptr, &pShader);

    pBlob->Release();
    return pShader;
}

DWORD WINAPI MainThread(HMODULE hModule)
{
    AllocConsole();
    FILE* f;
    freopen_s(&f, "CONOUT$", "w", stdout);
    printf("Console Created\n");

    uintptr_t clientBase = 0;
    while (!(clientBase = reinterpret_cast<uintptr_t>(GetModuleHandleA("client.dll")))) {
        Sleep(100);
    }

    HWND hwnd = NULL;
    printf("Waiting for CS2 window...\n");

    while (!hwnd) {
        EnumWindows([](HWND hWnd, LPARAM lParam) -> BOOL {
            DWORD processId = 0;
            GetWindowThreadProcessId(hWnd, &processId);

            if (processId == GetCurrentProcessId()) {
                if (IsWindowVisible(hWnd)) {
                    RECT rect;
                    GetClientRect(hWnd, &rect);
                    if (rect.right > 100 && rect.bottom > 100) {
                        char className[256];
                        GetClassNameA(hWnd, className, 256);
                        if (strcmp(className, "ConsoleWindowClass") != 0) {
                            *(HWND*)lParam = hWnd;
                            return FALSE;
                        }
                    }
                }
            }
            return TRUE;
            }, (LPARAM)&hwnd);

        if (!hwnd) {
            Sleep(500);
        }
    }

    std::cout << "CS2 Window Found: " << hwnd << std::endl;

    RECT cr;
    GetClientRect(hwnd, &cr);

    while (cr.right <= 0 || cr.bottom <= 0) {
        Sleep(500);
        GetClientRect(hwnd, &cr);
    }

    POINT tl = { 0, 0 };
    ClientToScreen(hwnd, &tl);

    WNDCLASSEX wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"DiscordOverlay";
    RegisterClassEx(&wc);

    HWND hwndOverlay = CreateWindowEx(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        wc.lpszClassName, L"", WS_POPUP, tl.x, tl.y, cr.right, cr.bottom,
        nullptr, nullptr, wc.hInstance, nullptr);

    if (!hwndOverlay) {
        printf("Failed to create overlay window! Error: %d\n", GetLastError());
        FreeConsole();
        return 0;
    }

    SetLayeredWindowAttributes(hwndOverlay, RGB(0, 0, 0), 0, LWA_COLORKEY);

    ShowWindow(hwndOverlay, SW_SHOW);
    UpdateWindow(hwndOverlay);

    bool dxInitialized = false;
    for (int attempt = 0; attempt < 20; attempt++) {
        MSG initMsg;
        while (PeekMessage(&initMsg, hwndOverlay, 0, 0, PM_REMOVE)) {
            TranslateMessage(&initMsg);
            DispatchMessage(&initMsg);
        }

        if (CreateDeviceD3D(hwndOverlay)) {
            dxInitialized = true;
            break;
        }

        printf("DX11 attempt %d failed.\n", attempt + 1);
        Sleep(250);
    }

    if (!dxInitialized) {
        printf("DX11 Init Failed after 20 attempts. Aborting.\n");
        DestroyWindow(hwndOverlay);
        FreeConsole();
        return 0;
    }


   /*
    MH_Initialize();
    void** pVTable = *(void***)pd3dDeviceContext;
    MH_CreateHook(pVTable[12], &hkDrawIndexed, reinterpret_cast<LPVOID*>(&oDrawIndexed));
    MH_EnableHook(pVTable[12]);
   */
    const auto entityList = read<uintptr_t>(clientBase + exitscam::offsets::client_dll::dwEntityList);
    const auto viewMatrixPtr = clientBase + exitscam::offsets::client_dll::dwViewMatrix;

    MSG msg;
    ZeroMemory(&msg, sizeof(msg));

    std::unordered_map<uintptr_t, uintptr_t> boneArrayCache;
    std::vector<uintptr_t> cachedControllers;
    std::vector<uintptr_t> cachedPawns;
    DWORD lastCacheTime = 0;

    while (msg.message != WM_QUIT)
    {
        // Safely print the logged index counts from the main thread
        if (!g_LoggedIndexCounts.empty()) {
            printf("Logged IndexCounts > 3000:\n");
            for (UINT val : g_LoggedIndexCounts) {
                printf("- %u\n", val);
            }
            g_LoggedIndexCounts.clear(); // Clear so it doesn't spam
        }
        if (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            continue;
        }

        if (GetAsyncKeyState(VK_F11) & 1)
            break;

        RECT gameRect;
        GetClientRect(hwnd, &gameRect);
        POINT gameTl = { 0, 0 };
        ClientToScreen(hwnd, &gameTl);
        SetWindowPos(hwndOverlay, HWND_TOPMOST, gameTl.x, gameTl.y, gameRect.right, gameRect.bottom, SWP_NOACTIVATE);

        if (GetForegroundWindow() != hwnd) {
            Sleep(50);
            continue;
        }

        RECT rect;
        GetClientRect(hwnd, &rect);
        int centerX = rect.right / 2;
        int centerY = rect.bottom / 2;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImDrawList* draw_list = ImGui::GetBackgroundDrawList();

        HandleInputs();

        const auto local_player_pawn = read<uintptr_t>(clientBase + exitscam::offsets::client_dll::dwLocalPlayerPawn);

        if (local_player_pawn != 0) {
            std::array<float, 16> viewMatrix = read<std::array<float, 16>>(viewMatrixPtr);
            int localHealth = read<int>(local_player_pawn + exitscam::schemas::client_dll::C_BaseEntity::m_iHealth);
            int localTeam = read<int>(local_player_pawn + exitscam::schemas::client_dll::C_BaseEntity::m_iTeamNum);

            RunBhop(local_player_pawn, clientBase);

            uintptr_t listEntry = read<uintptr_t>(entityList + 0x10);

            if (listEntry && (GetTickCount64() - lastCacheTime > 500 || cachedControllers.empty())) {
                cachedControllers.clear();
                cachedPawns.clear();
                boneArrayCache.clear();

                for (int i = 1; i < 64; i++) {
                    uintptr_t playerController = read<uintptr_t>(listEntry + i * 0x70);
                    if (!playerController) continue;

                    uintptr_t playerPawnID = read<uintptr_t>(playerController + exitscam::schemas::client_dll::CCSPlayerController::m_hPlayerPawn);
                    if (!playerPawnID) continue;

                    uintptr_t listEntry2 = read<uintptr_t>(entityList + 0x8 * ((playerPawnID & 0x7FFF) >> 9) + 0x10);
                    if (!listEntry2) continue;

                    uintptr_t CSPlayerPawnPtr = read<uintptr_t>(listEntry2 + 0x70 * (playerPawnID & 0x1FF));
                    if (!CSPlayerPawnPtr) continue;

                    cachedControllers.push_back(playerController);
                    cachedPawns.push_back(CSPlayerPawnPtr);
                }
                lastCacheTime = GetTickCount64();
            }

            oldOrigin localPos = read<oldOrigin>(local_player_pawn + exitscam::schemas::client_dll::C_BasePlayerPawn::m_vOldOrigin);
            Vector3 localEye = { localPos.cords[0], localPos.cords[1], localPos.cords[2] + 64.0f };

            // FOV Circle (Purple)
            if (settings.aimbotEnabled) {
                float radiusInPixels = (rect.bottom / 2.0f) * tanf(settings.aimbotFov * (3.14159265f / 180.0f));
                draw_list->AddCircle(ImVec2((float)centerX, (float)centerY), radiusInPixels, IM_COL32(200, 100, 255, 255), 0, 1.5f);
            }

            // ESP Drawing Loop wrapped in settings.espEnabled check
            if (settings.espEnabled) {
                for (size_t p = 0; p < cachedPawns.size(); p++) {
                    uintptr_t playerController = cachedControllers[p];
                    uintptr_t CSPlayerPawnPtr = cachedPawns[p];

                    if (CSPlayerPawnPtr == local_player_pawn) continue;

                    oldOrigin player = read<oldOrigin>(CSPlayerPawnPtr + exitscam::schemas::client_dll::C_BasePlayerPawn::m_vOldOrigin);
                    int health = read<int>(CSPlayerPawnPtr + exitscam::schemas::client_dll::C_BaseEntity::m_iHealth);
                    int team = read<int>(CSPlayerPawnPtr + exitscam::schemas::client_dll::C_BaseEntity::m_iTeamNum);
                    if (settings.teamCheck && team == localTeam) continue;
                    if (!health || health > 100) continue;

                    Vector3 feetPos = { player.cords[0], player.cords[1], player.cords[2] };
                    Vector3 headPos = { feetPos.x, feetPos.y, feetPos.z + 65.0f };
                    Vector2 feet, head;

                    float dx = localEye.x - feetPos.x;
                    float dy = localEye.y - feetPos.y;
                    float dz = localEye.z - feetPos.z;
                    int distance = (int)(sqrt(dx * dx + dy * dy + dz * dz) * 0.01905f);

                    if (worldToScreen(feetPos, feet, viewMatrix.data(), rect.right, rect.bottom) && worldToScreen(headPos, head, viewMatrix.data(), rect.right, rect.bottom)) {
                        int topY = static_cast<int>(head.y);
                        int bottomY = static_cast<int>(feet.y);
                        if (topY > bottomY) std::swap(topY, bottomY);
                        topY -= 5;

                        int boxHeight = bottomY - topY;
                        if (boxHeight < 5) continue;

                        int boxWidth = static_cast<int>(boxHeight / 2.25f);
                        int leftX = static_cast<int>(head.x - (boxWidth / 2));
                        int rightX = static_cast<int>(head.x + (boxWidth / 2));

                        draw_list->AddRect(ImVec2((float)leftX, (float)topY), ImVec2((float)rightX, (float)bottomY), IM_COL32(200, 100, 255, 255), 0.0f, 0, 1.5f);

                        int healthBarWidth = 3;
                        int healthBarX = leftX - 6;
                        int healthBarHeight = bottomY - topY;
                        for (int i = 0; i < healthBarHeight; i++) {
                            float pixelHealth = 100.0f - ((float)i / healthBarHeight) * 100.0f;
                            if (pixelHealth <= health) {
                                int r, g, b;
                                if (pixelHealth > 50) { r = (int)(255 * (100 - pixelHealth) / 50.0f); g = 255; }
                                else { r = 255; g = (int)(255 * pixelHealth / 50.0f); }
                                b = 0;
                                draw_list->AddLine(ImVec2((float)healthBarX, (float)(topY + i)), ImVec2((float)(healthBarX + healthBarWidth), (float)(topY + i)), IM_COL32(r, g, b, 255), 1.0f);
                            }
                        }

                        nameBuf nb = read<nameBuf>(playerController + exitscam::schemas::client_dll::CBasePlayerController::m_iszPlayerName);
                        draw_list->AddText(ImVec2((float)head.x - (ImGui::CalcTextSize(nb.data).x / 2), (float)topY - 18), IM_COL32(200, 100, 255, 255), nb.data);

                        char distStr[32];
                        sprintf_s(distStr, "%dm", distance);
                        draw_list->AddText(ImVec2((float)head.x - (ImGui::CalcTextSize(distStr).x / 2), (float)bottomY + 5), IM_COL32(200, 100, 255, 255), distStr);

                        uintptr_t boneArray = 0;
                        auto it = boneArrayCache.find(CSPlayerPawnPtr);
                        if (it != boneArrayCache.end()) {
                            boneArray = it->second;
                        }
                        else {
                            uintptr_t gsn = read<uintptr_t>(CSPlayerPawnPtr + exitscam::schemas::client_dll::C_BaseEntity::m_pGameSceneNode);
                            if (gsn > 0x10000 && gsn < 0x00007FFFFFFFFFFF) {
                                boneArray = read<uintptr_t>(gsn + 0x1c0);
                                if (boneArray > 0x10000 && boneArray < 0x00007FFFFFFFFFFF) {
                                    boneArrayCache[CSPlayerPawnPtr] = boneArray;
                                }
                            }
                        }

                        if (boneArray != 0) {
                            static const int bonePairs[][2] = {
                                { 7, 6 },{ 6, 4 },{ 4, 3 },{ 3, 2 },{ 2, 1 },
                                { 4, 12 },{ 12, 13 },{ 13, 14 },{ 14, 15 },{ 4, 8 },
                                { 8, 9 },{ 9, 10 },{ 10, 11 },{ 1, 20 },{ 20, 21 },
                                { 21, 22 },{ 1, 17 },{ 17, 18 },{ 18, 19 },
                            };

                            Vector2 screenBones[23];
                            bool valid[23] = { false };

                            for (int i = 1; i <= 22; i++) {
                                Vector3 bonePos;
                                if (getBonePosition(CSPlayerPawnPtr, i, bonePos, boneArray)) {
                                    if (worldToScreen(bonePos, screenBones[i], viewMatrix.data(), rect.right, rect.bottom)) {
                                        if (screenBones[i].x >= 0 && screenBones[i].x <= rect.right && screenBones[i].y >= 0 && screenBones[i].y <= rect.bottom) {
                                            valid[i] = true;
                                        }
                                    }
                                }
                            }

                            for (auto& pair : bonePairs) {
                                int a = pair[0];
                                int b = pair[1];
                                if (valid[a] && valid[b]) {
                                    draw_list->AddLine(ImVec2(screenBones[a].x, screenBones[a].y), ImVec2(screenBones[b].x, screenBones[b].y), IM_COL32(255, 0, 0, 255), 2.0f);
                                }
                            }

                            if (valid[7]) {
                                draw_list->AddCircle(ImVec2(screenBones[7].x, screenBones[7].y), 5.0f, IM_COL32(255, 0, 0, 255), 0, 1.5f);
                            }
                        }
                    }
                }
            }

            RunAimbot(clientBase, local_player_pawn, cachedPawns, boneArrayCache, localEye, localTeam);
            RunTriggerbot(clientBase, local_player_pawn, entityList, localTeam, localHealth);
        }

        ImGui::Render();
        pd3dDeviceContext->OMSetRenderTargets(1, &mainRenderTargetView, nullptr);
        const float clear_color_with_alpha[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        pd3dDeviceContext->ClearRenderTargetView(mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        swapChain->Present(0, 0);
        Sleep(1);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    DestroyWindow(hwndOverlay);
    FreeConsole();
    FreeLibraryAndExitThread(hModule, 0);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        {
            HANDLE hThread = CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)MainThread, hModule, 0, nullptr);
            if (hThread) CloseHandle(hThread);
        }
        break;
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}