#pragma once

#include <Windows.h>
#include <cstdint>
#include <vector>
#include <string>
class RemoteMemory;

// Generic x64 inline hook: allocates a code cave near the target address,
// writes [caveBody][original bytes][jump back] into it, and patches a 5-byte
// JMP at the target address. Game-specific byte sequences and shared state
// are entirely the caller's responsibility — this class only handles the
// mechanical detour work.
class DetourHook
{
public:
    explicit DetourHook(RemoteMemory& memory);
    ~DetourHook();

    DetourHook(const DetourHook&) = delete;
    DetourHook& operator=(const DetourHook&) = delete;

    // hookAddress: the absolute address to patch.
    // originalBytes: the exact bytes currently at hookAddress (size == hookSize).
    //                Used both to verify the address matches what we expect and
    //                to re-execute them inside the trampoline.
    // caveBody: extra code to run inside the cave BEFORE the original bytes
    //           (e.g. "store rcx into shared state"). Do not include the
    //           original bytes or the trailing jump-back — Install() appends
    //           those automatically.
    // caveSize: size of the cave to allocate near the hook address (0x1000 is
    //           usually enough).
    bool Install(
        uintptr_t hookAddress,
        const std::vector<uint8_t>& originalBytes,
        const std::vector<uint8_t>& caveBody,
        SIZE_T caveSize = 0x1000);

    bool Uninstall();

    bool IsInstalled() const;
    uintptr_t GetCodeCave() const;
    const std::wstring& GetLastError() const;
private:
    bool BuildTrampoline(const std::vector<uint8_t>& caveBody);
    bool PatchJump();

private:
    RemoteMemory& m_memory;

    uintptr_t m_hookAddress;
    uintptr_t m_codeCave;

    std::vector<uint8_t> m_originalBytes;

    bool m_installed;
    std::wstring m_lastError;
};