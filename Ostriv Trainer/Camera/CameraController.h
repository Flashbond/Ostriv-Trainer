#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class RemoteMemory;
class DetourHook;

// Smoothly moves the in-game camera to a given world position.
//
// Hooks the instruction that writes the camera's per-frame X position and
// injects a small conditional block: if a "teleport requested" flag is set
// in shared state, it overwrites both the camera's current and target X/Z
// with the requested position and clears the flag; otherwise the original
// instruction runs unmodified, every frame, at effectively zero cost.
class CameraController
{
public:
    explicit CameraController(RemoteMemory& memory);
    ~CameraController();

    CameraController(const CameraController&) = delete;
    CameraController& operator=(const CameraController&) = delete;

    bool Install();
    void Uninstall();

    bool IsInstalled() const;
    const std::wstring& GetLastError() const;

    // Requests that the camera jump to (x, z) on the next frame the hook
    // executes. Returns false if the hook is not installed.
    bool CenterOn(float x, float z);

private:
    static std::vector<uint8_t> BuildCaveBody(uintptr_t sharedStateAddress);

private:
    RemoteMemory& m_memory;
    std::unique_ptr<DetourHook> m_hook;

    uintptr_t m_sharedState;
    std::wstring m_lastError;
};