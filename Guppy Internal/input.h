#pragma once
#include <Windows.h>

struct cheatSettings {
    bool espEnabled = true;
    bool teamCheck = true;
    bool triggerbotEnabled = false;
    bool aimbotEnabled = false;
    bool bhopEnabled = true;

    float aimbotSmooth = 3.5f;
    float aimbotFov = 5.0f;

    bool isTriggering = false;
} settings;


void handleInputs() {
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