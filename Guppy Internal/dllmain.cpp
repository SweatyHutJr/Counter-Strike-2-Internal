#include <iostream>
#include <array>    
#include <cstdio>
#include <string>
#include "buttons.hpp"
#include "client_dll.hpp"
#include "offsets.h"
#include "input.h"
#include "cheats.h"
#include "DX11ImGui.h"

std::vector<UINT> g_LoggedIndexCounts;
static volatile bool running = true;
static FILE* consoleFile = nullptr;
static HWND hwndOverlay = nullptr;
static bool classRegistered = false;

static bool RunOverlay(HMODULE hModule)
{
    AllocConsole();
    freopen_s(&consoleFile, "CONOUT$", "w", stdout);
    { CONSOLE_CURSOR_INFO ci{ sizeof(ci), FALSE }; SetConsoleCursorInfo(GetStdHandle(STD_OUTPUT_HANDLE), &ci); }

    uintptr_t clientBase = 0;
    while (!(clientBase = reinterpret_cast<uintptr_t>(GetModuleHandleA("client.dll")))) {
        if (!running) return false;
        Sleep(100);
    }

    HWND hwnd = NULL;
    while (!hwnd) {
        if (!running) return false;
        EnumWindows([](HWND hWnd, LPARAM lParam) -> BOOL {
            DWORD processId = 0;
            GetWindowThreadProcessId(hWnd, &processId);
            if (processId != GetCurrentProcessId()) return TRUE;
            if (!IsWindowVisible(hWnd)) return TRUE;
            RECT rect;
            GetClientRect(hWnd, &rect);
            if (rect.right <= 100 || rect.bottom <= 100) return TRUE;
            char className[256];
            GetClassNameA(hWnd, className, 256);
            if (strcmp(className, "ConsoleWindowClass") != 0) {
                *(HWND*)lParam = hWnd;
                return FALSE;
            }
            return TRUE;
            }, (LPARAM)&hwnd);
        if (!hwnd) Sleep(500);
    }

    RECT cr;
    GetClientRect(hwnd, &cr);
    while (cr.right <= 0 || cr.bottom <= 0) {
        if (!running) return false;
        Sleep(500);
        GetClientRect(hwnd, &cr);
    }

    POINT tl = { 0, 0 };
    ClientToScreen(hwnd, &tl);

    WNDCLASSEX wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"DiscordOverlay";
    if (!RegisterClassEx(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        printf("RegisterClassEx failed: %lu\n", GetLastError());
        return false;
    }
    classRegistered = true;

    hwndOverlay = CreateWindowEx(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        wc.lpszClassName, L"", WS_POPUP, tl.x, tl.y, cr.right, cr.bottom,
        nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwndOverlay) {
        printf("Failed to create overlay window! Error: %d\n", GetLastError());
        return false;
    }

    SetLayeredWindowAttributes(hwndOverlay, RGB(0, 0, 0), 0, LWA_COLORKEY);
    ShowWindow(hwndOverlay, SW_SHOW);
    UpdateWindow(hwndOverlay);

    bool dxInitialized = false;
    for (int attempt = 0; attempt < 20; attempt++) {
        if (!running) return false;
        MSG initMsg;
        while (PeekMessage(&initMsg, hwndOverlay, 0, 0, PM_REMOVE)) {
            TranslateMessage(&initMsg);
            DispatchMessage(&initMsg);
        }
        if (CreateDeviceD3D(hwndOverlay)) { dxInitialized = true; break; }
        printf("DX11 attempt %d failed.\n", attempt + 1);
        Sleep(250);
    }
    if (!dxInitialized) {
        printf("DX11 Init Failed after 20 attempts. Aborting.\n");
        return false;
    }

    InitImGui(hwndOverlay);

    const auto entityList = read<uintptr_t>(clientBase + exitscam::offsets::client_dll::dwEntityList);
    const auto viewMatrixPtr = clientBase + exitscam::offsets::client_dll::dwViewMatrix;

    MSG msg;
    ZeroMemory(&msg, sizeof(msg));

    std::unordered_map<uintptr_t, uintptr_t> boneArrayCache;
    std::unordered_map<uintptr_t, PlayerMeta> metaCache;
    std::vector<uintptr_t> cachedControllers;
    std::vector<uintptr_t> cachedPawns;

    ULONGLONG lastCacheTime = 0;
    RECT lastGameRect = {};
    POINT lastGameTl = {};

    while (running && msg.message != WM_QUIT)
    {
        if (!g_LoggedIndexCounts.empty()) {
            printf("Logged IndexCounts > 3000:\n");
            for (UINT val : g_LoggedIndexCounts) printf("- %u\n", val);
            g_LoggedIndexCounts.clear();
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

        if (memcmp(&gameRect, &lastGameRect, sizeof(RECT)) != 0 ||
            memcmp(&gameTl, &lastGameTl, sizeof(POINT)) != 0) {
            SetWindowPos(hwndOverlay, HWND_TOPMOST, gameTl.x, gameTl.y, gameRect.right, gameRect.bottom, SWP_NOACTIVATE);
            lastGameRect = gameRect;
            lastGameTl = gameTl;
        }

        if (GetForegroundWindow() != hwnd) { Sleep(50); continue; }

        if (!pd3dDevice || !pd3dDeviceContext || !swapChain || !mainRenderTargetView) {
            printf("NULL D3D ptrs: dev=%p ctx=%p sc=%p rtv=%p\n",
                pd3dDevice, pd3dDeviceContext, swapChain, mainRenderTargetView);
            Sleep(50); continue;
        }
        if (!ImGui::GetCurrentContext()) {
            printf("No ImGui context!\n"); Sleep(50); continue;
        }

        RECT rect;
        GetClientRect(hwnd, &rect);
        int centerX = rect.right / 2;
        int centerY = rect.bottom / 2;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImDrawList* draw_list = ImGui::GetBackgroundDrawList();

        handleInputs();

        const auto local_player_pawn = read<uintptr_t>(clientBase + exitscam::offsets::client_dll::dwLocalPlayerPawn);

        if (local_player_pawn != 0) {
            std::array<float, 16> viewMatrix = read<std::array<float, 16>>(viewMatrixPtr);
            int localHealth = read<int>(local_player_pawn + exitscam::schemas::client_dll::C_BaseEntity::m_iHealth);
            int localTeam = read<int>(local_player_pawn + exitscam::schemas::client_dll::C_BaseEntity::m_iTeamNum);

            RunBhop(local_player_pawn, clientBase);

            uintptr_t listEntry = read<uintptr_t>(entityList + 0x10);

            if (listEntry && (GetTickCount64() - lastCacheTime > 3000 || cachedControllers.empty())) {
                cachedControllers.clear();
                cachedPawns.clear();

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

                    PlayerMeta meta;
                    meta.team = read<int>(CSPlayerPawnPtr + exitscam::schemas::client_dll::C_BaseEntity::m_iTeamNum);
                    nameBuf nbTmp = read<nameBuf>(playerController + exitscam::schemas::client_dll::CBasePlayerController::m_iszPlayerName);
                    memcpy(meta.name, nbTmp.data, sizeof(meta.name));
                    meta.nameValid = true;
                    metaCache[CSPlayerPawnPtr] = meta;
                }

                for (auto it2 = boneArrayCache.begin(); it2 != boneArrayCache.end(); ) {
                    if (std::find(cachedPawns.begin(), cachedPawns.end(), it2->first) == cachedPawns.end())
                        it2 = boneArrayCache.erase(it2);
                    else ++it2;
                }
                for (auto it2 = metaCache.begin(); it2 != metaCache.end(); ) {
                    if (std::find(cachedPawns.begin(), cachedPawns.end(), it2->first) == cachedPawns.end())
                        it2 = metaCache.erase(it2);
                    else ++it2;
                }

                lastCacheTime = GetTickCount64();
            }

            oldOrigin localPos = read<oldOrigin>(local_player_pawn + exitscam::schemas::client_dll::C_BasePlayerPawn::m_vOldOrigin);
            Vector3 localEye = { localPos.cords[0], localPos.cords[1], localPos.cords[2] + 64.0f };

            if (settings.aimbotEnabled) {
                float radiusInPixels = (rect.bottom / 2.0f) * tanf(settings.aimbotFov * (3.14159265f / 180.0f));
                draw_list->AddCircle(ImVec2((float)centerX, (float)centerY), radiusInPixels, IM_COL32(200, 100, 255, 255), 0, 1.5f);
            }

            if (settings.espEnabled) { 
                for (size_t p = 0; p < cachedPawns.size(); p++) {
                    uintptr_t playerController = cachedControllers[p];
                    uintptr_t CSPlayerPawnPtr = cachedPawns[p];

                    if (CSPlayerPawnPtr == local_player_pawn) continue;

                    oldOrigin player = read<oldOrigin>(CSPlayerPawnPtr + exitscam::schemas::client_dll::C_BasePlayerPawn::m_vOldOrigin);
                    int health = read<int>(CSPlayerPawnPtr + exitscam::schemas::client_dll::C_BaseEntity::m_iHealth);
                    int team = 0;
                    auto mIt = metaCache.find(CSPlayerPawnPtr);
                    if (mIt != metaCache.end()) team = mIt->second.team;
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

                        const char* nameStr = "";
                        if (mIt != metaCache.end() && mIt->second.nameValid)
                            nameStr = mIt->second.name;
                        draw_list->AddText(ImVec2((float)head.x - (ImGui::CalcTextSize(nameStr).x / 2), (float)topY - 18), IM_COL32(200, 100, 255, 255), nameStr);

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

                            if (readBoneArray(boneArray, 22)) {
                                for (int i = 1; i <= 22; i++) {
                                    Vector3 bonePos;
                                    if (getBoneFromBuffer(i, bonePos)) {
                                        if (worldToScreen(bonePos, screenBones[i], viewMatrix.data(), rect.right, rect.bottom)) {
                                            if (screenBones[i].x >= 0 && screenBones[i].x <= rect.right && screenBones[i].y >= 0 && screenBones[i].y <= rect.bottom) {
                                                valid[i] = true;
                                            }
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

            runAimbot(clientBase, local_player_pawn, cachedPawns, boneArrayCache, metaCache, localEye, localTeam);
            RunTriggerbot(clientBase, local_player_pawn, entityList, localTeam, localHealth);
        }

        ImGui::Render();
        pd3dDeviceContext->OMSetRenderTargets(1, &mainRenderTargetView, nullptr);
        const float clear_color_with_alpha[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        pd3dDeviceContext->ClearRenderTargetView(mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        HRESULT hrPresent = swapChain->Present(0, 0);
        if (hrPresent == DXGI_ERROR_DEVICE_REMOVED || hrPresent == DXGI_ERROR_DEVICE_RESET) {
            printf("Device removed/reset on Present (0x%X). Breaking.\n", hrPresent);
            break;
        }

        renderMenu();
        Sleep(8);
    }

    return true;
}

DWORD WINAPI MainThread(HMODULE hModule)
{
    RunOverlay(hModule);

    if (ImGui::GetCurrentContext()) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }

    if (mainRenderTargetView) { mainRenderTargetView->Release(); mainRenderTargetView = nullptr; }
    if (swapChain) { swapChain->Release(); swapChain = nullptr; }
    if (pd3dDeviceContext) { pd3dDeviceContext->Release(); pd3dDeviceContext = nullptr; }
    if (pd3dDevice) { pd3dDevice->Release(); pd3dDevice = nullptr; }

    if (hwndOverlay && IsWindow(hwndOverlay)) DestroyWindow(hwndOverlay);
    hwndOverlay = nullptr;

    if (classRegistered) {
        UnregisterClass(L"DiscordOverlay", GetModuleHandle(nullptr));
        classRegistered = false;
    }

    if (consoleFile) { fclose(consoleFile); consoleFile = nullptr; }
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
        running = false;
        break;
    }
    return TRUE;
}