#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <string>

class RemoteMemory;
class DetourHook;

// Tracks which building the player currently has selected in-game.
//
// The hooked instruction only executes while the building info panel is
// actually being rendered. When the player deselects, the hook simply stops
// firing — the game does NOT reset the shared state back to 0. Update()
// detects this by comparing a hit counter between polls: if the counter
// hasn't advanced since the last call, nothing was rendered this interval,
// so the selection is treated as cleared.
class SelectionTracker
{
public:
    explicit SelectionTracker(RemoteMemory& memory);
    ~SelectionTracker();

    SelectionTracker(const SelectionTracker&) = delete;
    SelectionTracker& operator=(const SelectionTracker&) = delete;

    bool Install();
    void Uninstall();

    bool IsInstalled() const;

    // Call once per timer tick, before reading GetSelectedBuildingAddress().
    bool Update();

    // Cheap getter for the address resolved by the last Update() call.
    // Returns 0 if nothing is selected.
    uintptr_t GetSelectedBuildingAddress() const;
    // Empty if Install() succeeded or has not been called yet.
    const std::wstring& GetLastError() const;

private:
    static std::vector<uint8_t> BuildCaveBody(uintptr_t sharedStateAddress);

private:
    RemoteMemory& m_memory;
    std::unique_ptr<DetourHook> m_hook;

    uintptr_t m_sharedState;

    uintptr_t m_selectedBuildingAddress;
    uint64_t m_lastCounter;
    std::wstring m_lastError;
};