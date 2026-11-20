#pragma once
#include <Windows.h>
#include <cmath>
#include <iostream>
#include <vector>
#include <unordered_map>
#include <synchapi.h>
#include <cstring>

#include "client_dll.hpp"
#include "buttons.hpp"
#include "offsets.hpp"
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

// OPT: batched read — one __try block for a whole contiguous buffer instead of N
// individual read<T>() calls. Used for the bone array (22 bones in one shot).
inline bool read_raw(uintptr_t address, void* out, size_t size) {
    if (!address || !out || !size) return false;
    __try {
        memcpy(out, reinterpret_cast<void*>(address), size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
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

// OPT: per-player cached metadata. Refreshed only when the pawn list is rebuilt
// (every ~3s), not every frame. Team never changes mid-round; name rarely does.
struct PlayerMeta {
    int  team = 0;
    char name[128] = {};
    bool nameValid = false;
};

// OPT: bone stride for CS2 CTransform (0x20 bytes: Vector3 pos + pad).
// Verify against your build if you see garbage bone positions.
static constexpr size_t BONE_STRIDE = 0x20;
static constexpr int    MAX_BONES = 128;

// OPT: pre-allocated scratch buffer for the one-shot bone read.
// Kept thread-local-ish (static, single overlay thread) so we don't allocate per frame.
struct BoneBuffer {
    alignas(16) unsigned char data[MAX_BONES * BONE_STRIDE];
};
static BoneBuffer g_boneBuf;

bool getBonePosition(std::uintptr_t pawn, int boneIndex, Vector3& out, uintptr_t boneArray) {
    if (boneIndex < 0 || boneIndex > 127)
        return false;
    Vector3 bone = read<Vector3>(boneArray + (boneIndex * BONE_STRIDE));

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

// OPT: read the entire bone array in ONE __try/memcpy, then index locally.
// This is the big FPS win: 1 syscall/exception frame instead of 22 per player.
inline bool readBoneArray(uintptr_t boneArray, int maxBone /*e.g. 22*/) {
    if (!boneArray || maxBone <= 0 || maxBone > MAX_BONES) return false;
    return read_raw(boneArray, g_boneBuf.data, (size_t)maxBone * BONE_STRIDE);
}

inline bool getBoneFromBuffer(int boneIndex, Vector3& out) {
    if (boneIndex < 0 || boneIndex >= MAX_BONES) return false;
    const float* p = reinterpret_cast<const float*>(g_boneBuf.data + (size_t)boneIndex * BONE_STRIDE);
    float x = p[0], y = p[1], z = p[2];
    if (std::isnan(x) || std::isnan(y) || std::isnan(z)) return false;
    if (x == 0.0f && y == 0.0f && z == 0.0f) return false;
    if (std::abs(x) > 15000.0f || std::abs(y) > 15000.0f || std::abs(z) > 15000.0f) return false;
    out.x = x; out.y = y; out.z = z;
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

    // OPT: single read for flags; GetAsyncKeyState is cheap enough.
    const auto flags = read<std::uint32_t>(local_pawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_fFlags);
    const bool in_air = flags & (1 << 0);
    const bool space_pressed = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
    const auto force_jump = read<DWORD>(clientBase + cs2_dumper::buttons::jump);

    if (space_pressed && in_air) {
        // OPT: removed Sleep(5) — it stalls the whole overlay thread for 5ms every
        // jump tick. Write immediately; the game reads the jump state next tick anyway.
        write(clientBase + cs2_dumper::buttons::jump, 65537);
    }
    else if (space_pressed && !in_air) {
        write(clientBase + cs2_dumper::buttons::jump, 256);
    }
    else if (!space_pressed && force_jump == 65537) {
        write(clientBase + cs2_dumper::buttons::jump, 256);
    }
}

// OPT: new signature — takes cached player metadata and the per-frame bone cache
// so it doesn't re-read team/health via RPM for every candidate.
// OPT: new signature — takes cached player metadata and the per-frame bone cache
// so it doesn't re-read team/health via RPM for every candidate.
void runAimbot(uintptr_t clientBase,
    uintptr_t local_pawn,
    const std::vector<uintptr_t>& cachedPawns,
    std::unordered_map<uintptr_t, uintptr_t>& boneArrayCache,
    const std::unordered_map<uintptr_t, PlayerMeta>& metaCache,
    const Vector3& localEye,
    int localteam)
{
    if (!settings.aimbotEnabled || !(GetAsyncKeyState(VK_RBUTTON) & 0x8000)) return;

    float bestFov = settings.aimbotFov;
    Vector3 bestTargetPos = { 0, 0, 0 };
    bool targetFound = false;
    QAngle currentAngle = read<QAngle>(clientBase + cs2_dumper::offsets::client_dll::dwViewAngles);

    for (uintptr_t CSPlayerPawnPtr : cachedPawns) {
        // OPT: team from cache instead of RPM per frame
        int team = localteam;
        auto mit = metaCache.find(CSPlayerPawnPtr);
        if (mit != metaCache.end()) team = mit->second.team;

        if (settings.teamCheck && team == localteam) continue;

        // OPT: health still needs to be live (damage changes), but one read is cheap.
        int health = read<int>(CSPlayerPawnPtr + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iHealth);
        if (health <= 0 || health > 100) continue;

        uintptr_t boneArray = 0;
        auto bit = boneArrayCache.find(CSPlayerPawnPtr);
        if (bit != boneArrayCache.end()) boneArray = bit->second;

        if (boneArray == 0) continue;

        Vector3 headPos;
        if (!getBonePosition(CSPlayerPawnPtr, 7, headPos, boneArray)) continue;

        bool isSpotted = read<bool>(CSPlayerPawnPtr + cs2_dumper::schemas::client_dll::C_CSPlayerPawn::m_entitySpottedState + 0x8);
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

    if (targetFound) {
        QAngle targetAngle = calcAngle(localEye, bestTargetPos);
        while (targetAngle.y > 180.0f) targetAngle.y -= 360.0f;
        while (targetAngle.y < -180.0f) targetAngle.y += 360.0f;

        QAngle newAngle;
        float smooth = (settings.aimbotSmooth > 0.0f) ? settings.aimbotSmooth : 1.0f;
        newAngle.x = currentAngle.x + (targetAngle.x - currentAngle.x) / smooth;
        newAngle.y = currentAngle.y + (targetAngle.y - currentAngle.y) / smooth;
        newAngle.z = 0.0f;

        write(clientBase + cs2_dumper::offsets::client_dll::dwViewAngles, newAngle);
    }
}

void RunTriggerbot(uintptr_t clientBase, uintptr_t local_pawn, uintptr_t entityList, int localTeam, int localHealth) {
    if (!settings.triggerbotEnabled) {
        if (settings.isTriggering) {
            write(clientBase + cs2_dumper::buttons::attack, 256);
            settings.isTriggering = false;
        }
        return;
    }

    bool shouldShoot = false;
    if (localHealth > 0) {
        int crosshairEntIndex = read<int>(local_pawn + cs2_dumper::schemas::client_dll::C_CSPlayerPawn::m_iIDEntIndex);
        if (crosshairEntIndex != -1) {
            uintptr_t triggerListEntry2 = read<uintptr_t>(entityList + 0x8 * ((crosshairEntIndex & 0x7FFF) >> 9) + 0x10);
            if (triggerListEntry2) {
                uintptr_t targetPawn = read<uintptr_t>(triggerListEntry2 + 0x70 * (crosshairEntIndex & 0x1FF));
                if (targetPawn) {
                    int targetHealth = read<int>(targetPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iHealth);
                    int targetTeam = read<int>(targetPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum);
                    if (targetHealth > 0 && targetHealth <= 100) {
                        if (!settings.teamCheck || targetTeam != localTeam) shouldShoot = true;
                    }
                }
            }
        }
    }

    if (shouldShoot) {
        if (!settings.isTriggering) {
            // OPT: removed Sleep(17 + rand()%12) — that stalls the whole overlay thread
            // (and thus your ESP/aimbot) for 17-29ms every time the triggerbot engages.
            // If you need a humanization delay, do it with a timestamp:
            //   static DWORD lastShot = 0;
            //   DWORD now = GetTickCount();
            //   if (now - lastShot < (DWORD)(17 + rand() % 12)) return;
            //   lastShot = now;
            write(clientBase + cs2_dumper::buttons::attack, 65537);
            settings.isTriggering = true;
        }
    }
    else {
        if (settings.isTriggering) {
            write(clientBase + cs2_dumper::buttons::attack, 256);
            settings.isTriggering = false;
        }
    }
}