#pragma once
#include <Windows.h>
#include <cmath>
#include <iostream>
#include <vector>
#include <unordered_map>
#include <synchapi.h>

#include "client_dll.hpp"
#include "buttons.hpp"
#include "offsets.h"
#include "input.h"

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

struct Vector3 { float x, y, z; };
struct Vector2 { float x, y; };
struct QAngle { float x, y, z; };
struct oldOrigin { float cords[3]; };
struct nameBuf { char data[128]; };


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

QAngle calcAngle(const Vector3& src, const Vector3& dst) {
    QAngle angles;
    Vector3 delta = { dst.x - src.x, dst.y - src.y, dst.z - src.z };
    float hyp = sqrt(delta.x * delta.x + delta.y * delta.y);
    angles.x = atan2f(-delta.z, hyp) * (180.0f / 3.14159265358979323846f);
    angles.y = atan2f(delta.y, delta.x) * (180.0f / 3.14159265358979323846f);
    angles.z = 0.0f;
    return angles;
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

void runAimbot(uintptr_t clientBase, uintptr_t local_pawn, const std::vector<uintptr_t>& cachedPawns, std::unordered_map<uintptr_t, uintptr_t>& boneArrayCache, const Vector3& localEye, int localteam) {
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

            QAngle targetAngle = calcAngle(localEye, headPos);
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
        QAngle targetAngle = calcAngle(localEye, bestTargetPos);
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