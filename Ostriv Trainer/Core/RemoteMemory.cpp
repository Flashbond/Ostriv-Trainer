#include "RemoteMemory.h"
#include <TlHelp32.h>
#include <algorithm>
#include <sstream>
#include <psapi.h>

RemoteMemory::RemoteMemory() : m_processHandle(nullptr), m_moduleBase(0) {}
RemoteMemory::~RemoteMemory() { Detach(); }

bool RemoteMemory::Attach(DWORD processId) {
    Detach();

    m_processHandle = OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_QUERY_INFORMATION, FALSE, processId);
    if (!m_processHandle) return false;

    m_moduleBase = 0;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processId);
    if (snapshot == INVALID_HANDLE_VALUE) {
        Detach();
        return false;
    }

    MODULEENTRY32W moduleEntry{};
    moduleEntry.dwSize = sizeof(moduleEntry);

    if (Module32FirstW(snapshot, &moduleEntry)) {
        m_moduleBase = reinterpret_cast<uintptr_t>(moduleEntry.modBaseAddr);
    }

    CloseHandle(snapshot);

    if (m_moduleBase == 0) {
        Detach();
        return false;
    }

    return true;
}

void RemoteMemory::Detach() {
    if (m_processHandle) {
        CloseHandle(m_processHandle);
        m_processHandle = nullptr;
    }
    m_moduleBase = 0;
}

bool RemoteMemory::IsAttached() const { return m_processHandle != nullptr; }
HANDLE RemoteMemory::GetProcessHandle() const { return m_processHandle; }
uintptr_t RemoteMemory::GetModuleBase() const { return m_moduleBase; }

bool RemoteMemory::ReadBytes(uintptr_t address, void* buffer, SIZE_T size) const {
    if (!IsAttached() || address == 0 || buffer == nullptr || size == 0) return false;

    SIZE_T bytesRead = 0;
    return ReadProcessMemory(m_processHandle, reinterpret_cast<LPCVOID>(address), buffer, size, &bytesRead) && bytesRead == size;
}

bool RemoteMemory::WriteBytes(uintptr_t address, const void* buffer, SIZE_T size) const {
    if (!IsAttached() || address == 0 || buffer == nullptr || size == 0) return false;

    SIZE_T bytesWritten = 0;
    return WriteProcessMemory(m_processHandle, reinterpret_cast<LPVOID>(address), buffer, size, &bytesWritten) && bytesWritten == size;
}

bool RemoteMemory::ReadString(uintptr_t address, std::string& value, SIZE_T maxLength) const {
    value.clear();
    if (!IsAttached() || address == 0 || maxLength == 0) return false;

    std::vector<char> buffer(maxLength + 1, '\0');
    SIZE_T bytesRead = 0;

    if (!ReadProcessMemory(m_processHandle, reinterpret_cast<LPCVOID>(address), buffer.data(), maxLength, &bytesRead)) return false;

    buffer[maxLength] = '\0';
    SIZE_T length = 0;

    while (length < bytesRead && buffer[length] != '\0') ++length;

    value.assign(buffer.data(), length);
    return true;
}

bool RemoteMemory::ReadUTF16String(uintptr_t address, std::wstring& value, SIZE_T charCount) const {
    value.clear();
    if (!IsAttached() || address == 0 || charCount == 0) return false;

    std::vector<wchar_t> buffer(charCount + 1, L'\0');
    SIZE_T bytesRead = 0;
    SIZE_T byteCount = charCount * sizeof(wchar_t);

    if (!ReadProcessMemory(m_processHandle, reinterpret_cast<LPCVOID>(address), buffer.data(), byteCount, &bytesRead)) return false;

    SIZE_T actualChars = bytesRead / sizeof(wchar_t);
    if (actualChars > charCount) actualChars = charCount;

    buffer[actualChars] = L'\0';
    SIZE_T length = 0;

    while (length < actualChars && buffer[length] != L'\0') ++length;

    value.assign(buffer.data(), length);
    return true;
}

uintptr_t RemoteMemory::FindPattern(const std::string& signature) const {
    if (!IsAttached() || m_moduleBase == 0) return 0;

    MODULEINFO moduleInfo{};
    if (!GetModuleInformation(m_processHandle, reinterpret_cast<HMODULE>(m_moduleBase), &moduleInfo, sizeof(moduleInfo))) {
        return 0;
    }

    DWORD moduleSize = moduleInfo.SizeOfImage;
    if (moduleSize == 0) return 0;

    std::vector<uint8_t> moduleBytes(moduleSize);
    SIZE_T bytesRead = 0;
    if (!ReadProcessMemory(m_processHandle, reinterpret_cast<LPCVOID>(m_moduleBase), moduleBytes.data(), moduleSize, &bytesRead) || bytesRead == 0) {
        return 0;
    }

    std::vector<std::pair<uint8_t, bool>> patternBytes;
    std::stringstream ss(signature);
    std::string byteStr;

    while (ss >> byteStr) {
        if (byteStr == "?" || byteStr == "??") {
            patternBytes.push_back({ 0, true });
        }
        else {
            patternBytes.push_back({ static_cast<uint8_t>(std::stoul(byteStr, nullptr, 16)), false });
        }
    }

    if (patternBytes.empty()) return 0;

    for (size_t i = 0; i <= bytesRead - patternBytes.size(); ++i) {
        bool found = true;
        for (size_t j = 0; j < patternBytes.size(); ++j) {
            if (!patternBytes[j].second && moduleBytes[i + j] != patternBytes[j].first) {
                found = false;
                break;
            }
        }
        if (found) {
            return m_moduleBase + i;
        }
    }

    return 0;
}

uintptr_t RemoteMemory::ResolveRipRelative(uintptr_t instructionAddress, int offset, int instructionSize) const {
    if (!IsAttached() || instructionAddress == 0) return 0;

    int32_t ripOffset = 0;
    if (!Read(instructionAddress + offset, ripOffset)) {
        return 0;
    }

    return instructionAddress + instructionSize + ripOffset;
}

uintptr_t RemoteMemory::Allocate(SIZE_T size, DWORD protection) const {
    if (!IsAttached() || size == 0) return 0;
    return reinterpret_cast<uintptr_t>(VirtualAllocEx(m_processHandle, nullptr, size, MEM_COMMIT | MEM_RESERVE, protection));
}

uintptr_t RemoteMemory::AllocateNear(uintptr_t target, SIZE_T size, DWORD protection) const {
    if (!IsAttached() || target == 0 || size == 0) return 0;

    constexpr uintptr_t RANGE = 0x70000000;
    constexpr uintptr_t STEP = 0x10000;

    uintptr_t start = (target > RANGE) ? (target - RANGE) : 0x10000;
    uintptr_t end = target + RANGE;

    for (uintptr_t address = start; address < end; address += STEP) {
        LPVOID memory = VirtualAllocEx(
            m_processHandle,
            reinterpret_cast<LPVOID>(address),
            size,
            MEM_RESERVE | MEM_COMMIT,
            protection);

        if (memory)
            return reinterpret_cast<uintptr_t>(memory);
    }

    return 0;
}

bool RemoteMemory::Free(uintptr_t address, SIZE_T size) const {
    if (!IsAttached() || address == 0) return false;
    return VirtualFreeEx(m_processHandle, reinterpret_cast<LPVOID>(address), 0, MEM_RELEASE) != FALSE;
}

bool RemoteMemory::Protect(uintptr_t address, SIZE_T size, DWORD newProtection, DWORD& oldProtection) const {
    if (!IsAttached() || address == 0 || size == 0) return false;
    return VirtualProtectEx(m_processHandle, reinterpret_cast<LPVOID>(address), size, newProtection, &oldProtection) != FALSE;
}

bool RemoteMemory::FlushInstructionCache(uintptr_t address, SIZE_T size) const {
    if (!IsAttached() || address == 0) return false;
    return ::FlushInstructionCache(m_processHandle, reinterpret_cast<LPCVOID>(address), size) != FALSE;
}