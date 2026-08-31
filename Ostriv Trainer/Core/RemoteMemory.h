#pragma once

#include <Windows.h>
#include <cstdint>
#include <string>
#include <vector>

class RemoteMemory {
public:
    RemoteMemory();
    ~RemoteMemory();

    RemoteMemory(const RemoteMemory&) = delete;
    RemoteMemory& operator=(const RemoteMemory&) = delete;

    bool Attach(DWORD processId);
    void Detach();

    bool IsAttached() const;
    HANDLE GetProcessHandle() const;
    uintptr_t GetModuleBase() const;

    template<typename T>
    bool Read(uintptr_t address, T& value) const {
        if (!IsAttached() || address == 0) return false;

        SIZE_T bytesRead = 0;
        return ReadProcessMemory(m_processHandle, reinterpret_cast<LPCVOID>(address), &value, sizeof(T), &bytesRead) && bytesRead == sizeof(T);
    }

    template<typename T>
    bool Write(uintptr_t address, const T& value) const {
        if (!IsAttached() || address == 0) return false;

        SIZE_T bytesWritten = 0;
        return WriteProcessMemory(m_processHandle, reinterpret_cast<LPVOID>(address), &value, sizeof(T), &bytesWritten) && bytesWritten == sizeof(T);
    }

    bool ReadBytes(uintptr_t address, void* buffer, SIZE_T size) const;
    bool WriteBytes(uintptr_t address, const void* buffer, SIZE_T size) const;

    bool ReadString(uintptr_t address, std::string& value, SIZE_T maxLength = 256) const;
    bool ReadUTF16String(uintptr_t address, std::wstring& value, SIZE_T charCount) const;

    uintptr_t FindPattern(const std::string& signature) const;
    uintptr_t ResolveRipRelative(uintptr_t instructionAddress, int offset, int instructionSize) const;

    uintptr_t Allocate(SIZE_T size, DWORD protection = PAGE_READWRITE) const;
    uintptr_t AllocateNear(uintptr_t target, SIZE_T size, DWORD protection = PAGE_EXECUTE_READWRITE) const;
    bool Free(uintptr_t address, SIZE_T size = 0) const;

    bool Protect(uintptr_t address, SIZE_T size, DWORD newProtection, DWORD& oldProtection) const;
    bool FlushInstructionCache(uintptr_t address, SIZE_T size) const;

private:
    HANDLE m_processHandle;
    uintptr_t m_moduleBase;
};