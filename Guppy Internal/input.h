#pragma once
#include <Windows.h>

struct cheatSettings {
    bool espEnabled = true;
    bool teamCheck = true;
    bool triggerbotEnabled = false;
    bool aimbotEnabled = false;
    bool bhopEnabled = true;

    float aimbotSmooth = 1.0f;
    float aimbotFov = 10.0f;

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

static void emitLine(const std::string& inner) {
    const int W = 67;
    std::string s = inner;
    if ((int)s.size() < W) s.append(W - s.size(), ' ');
    else if ((int)s.size() > W) s.resize(W);
    printf("|%s|\n", s.c_str());
}

static const char* onoff(bool b) { return b ? "[ ON  ]" : "[ OFF ]"; }

void renderMenu() {
    COORD topLeft = { 0, 0 };
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), topLeft);

    printf("+===================================================================+\n");
    emitLine("");
    emitLine("      ,----..");
    emitLine("     /   /   \\                ,-.----.  ,-.----.");
    emitLine("    |   :     :          ,--, \\    /  \\ \\    /  \\");
    emitLine("    .   |  ;. /        ,'_ /| |   :    ||   :    |");
    emitLine("    .   ; /--`    .--. |  | : |   | .\\ :|   | .\\ :     .--,");
    emitLine("    ;   | ;  __ ,'_ /| :  . | .   : |: |.   : |: |   /_ ./|");
    emitLine("    |   : |.' .'|  ' | |  . . |   |  \\ :|   |  \\ :, ' , ' :");
    emitLine("    .   | '_.' :|  | ' |  | | |   : .  ||   : .  /___/ \\: |");
    emitLine("    '   ; : \\  |:  | : ;  ; | :     |`-':     |`-'.  \\  ' |");
    emitLine("    '   | '/  .''  :  `--'   \\:   : :   :   : :    \\  ;   :");
    emitLine("    |   :    /  :  ,      .-./|   | :   |   | :     \\  \\  ;");
    emitLine("     \\   \\ .'    `--`----'    `---'.|   `---'.|      :  \\  \\");
    emitLine("      `---`                     `---`     `---`       \\  ' ;");
    emitLine("                                                       `--`");
    printf("+===================================================================+\n");
    emitLine("");
    emitLine("   >> FEATURES");

    char buf[128];
    snprintf(buf, sizeof(buf), "   |- [F1]  ESP                ............  %s", onoff(settings.espEnabled));
    emitLine(buf);
    snprintf(buf, sizeof(buf), "   |- [F2]  TeamCheck          ............  %s", onoff(settings.teamCheck));
    emitLine(buf);
    snprintf(buf, sizeof(buf), "   |- [F3]  Triggerbot         ............  %s", onoff(settings.triggerbotEnabled));
    emitLine(buf);
    snprintf(buf, sizeof(buf), "   |- [F4]  Aimbot             ............  %s", onoff(settings.aimbotEnabled));
    emitLine(buf);
    snprintf(buf, sizeof(buf), "   |- [F5]  Bunnyhop           ............  %s", onoff(settings.bhopEnabled));
    emitLine(buf);

    emitLine("");
    emitLine("   >> AIMBOT TWEAKS");

    snprintf(buf, sizeof(buf), "   |- KEY [   Decrease FOV      ............  %.1f", settings.aimbotFov);
    emitLine(buf);
    snprintf(buf, sizeof(buf), "   |- KEY ]   Increase FOV      ............  %.1f", settings.aimbotFov);
    emitLine(buf);
    snprintf(buf, sizeof(buf), "   |- KEY -   Decrease Smooth   ............  %.1f", settings.aimbotSmooth);
    emitLine(buf);
    snprintf(buf, sizeof(buf), "   |- KEY +   Increase Smooth   ............  %.1f", settings.aimbotSmooth);
    emitLine(buf);

    emitLine("");
    emitLine("   >> CONTROLS");
    emitLine("   |- [RMB]   Hold to aimbot   ............  HOLD");
    emitLine("   |- [LMB]   Fire / shoot     ............  HOLD");
    emitLine("   |- [SPACE] Hold to bunnyhop ............  HOLD");
    emitLine("");

    printf("+===================================================================+\n");
    emitLine("   >> EXIT");
    emitLine("   |- [F11] Quit cheat         ............  EXIT");
    printf("+===================================================================+\n");
}