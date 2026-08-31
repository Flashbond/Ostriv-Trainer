#include "DetourHook.h"
#include "RemoteMemory.h"

#include <cstring>

DetourHook::DetourHook(RemoteMemory& memory)
    : m_memory(memory), m_hookAddress(0), m_codeCave(0), m_installed(false)
{
}

DetourHook::~DetourHook()
{
    Uninstall();
}

bool DetourHook::Install(
    uintptr_t hookAddress,
    const std::vector<uint8_t>& originalBytes,
    const std::vector<uint8_t>& caveBody,
    SIZE_T caveSize)
{
    m_lastError.clear();

    if (m_installed)
    {
        m_lastError = L"already installed";
        return false;
    }

    if (hookAddress == 0 || originalBytes.empty())
    {
        m_lastError = L"invalid hook address or empty original bytes";
        return false;
    }

    m_hookAddress = hookAddress;
    m_originalBytes = originalBytes;

    // Before patching, verify the bytes at hookAddress match what we expect.
    // If the game updated and offsets shifted, we stop here instead of corrupting memory.
    std::vector<uint8_t> currentBytes(originalBytes.size());
    if (!m_memory.ReadBytes(hookAddress, currentBytes.data(), currentBytes.size()))
    {
        m_lastError = L"failed to read bytes at the hook address";
        return false;
    }

    if (currentBytes != originalBytes)
    {
        // A leftover JMP from a previous run that exited without calling
        // Uninstall() (crash, debugger detach) looks like this: the first
        // byte is our own 0xE9 patch opcode. We know the true original
        // bytes, so we can safely restore them and continue as if fresh.
        if (currentBytes[0] == 0xE9 && currentBytes.size() >= 5)
        {
            if (!m_memory.WriteBytes(hookAddress, originalBytes.data(), originalBytes.size()) ||
                !m_memory.FlushInstructionCache(hookAddress, originalBytes.size()))
            {
                m_lastError = L"detected a stale hook but failed to restore original bytes";
                return false;
            }
        }
        else
        {
            m_lastError = L"byte mismatch at the hook address (offset likely wrong or game version changed)";
            return false;
        }
    }

    // The code cave MUST be executable (PAGE_EXECUTE_READWRITE) — otherwise DEP
    // will crash the target process the moment it jumps into the hook.
    m_codeCave = m_memory.AllocateNear(hookAddress, caveSize, PAGE_EXECUTE_READWRITE);
    if (!m_codeCave)
    {
        m_lastError = L"AllocateNear found no free region near the hook address";
        return false;
    }

    if (!BuildTrampoline(caveBody))
    {
        m_lastError = L"failed to write the trampoline into the code cave";
        m_memory.Free(m_codeCave);
        m_codeCave = 0;
        return false;
    }

    if (!PatchJump())
    {
        m_lastError = L"failed to patch the JMP at the hook address";
        m_memory.Free(m_codeCave);
        m_codeCave = 0;
        return false;
    }

    m_installed = true;
    return true;
}

bool DetourHook::BuildTrampoline(const std::vector<uint8_t>& caveBody)
{
    std::vector<uint8_t> code = caveBody;

    // Üzerine yazdığımız orijinal komutu tekrar çalıştır.
    code.insert(code.end(), m_originalBytes.begin(), m_originalBytes.end());

    // Hook adresinin hemen sonrasına geri dön.
    uintptr_t returnAddress = m_hookAddress + m_originalBytes.size();
    uintptr_t jumpFrom = m_codeCave + code.size();

    int32_t rel = static_cast<int32_t>(returnAddress - (jumpFrom + 5));

    code.push_back(0xE9);
    code.push_back(static_cast<uint8_t>(rel & 0xFF));
    code.push_back(static_cast<uint8_t>((rel >> 8) & 0xFF));
    code.push_back(static_cast<uint8_t>((rel >> 16) & 0xFF));
    code.push_back(static_cast<uint8_t>((rel >> 24) & 0xFF));

    if (!m_memory.WriteBytes(m_codeCave, code.data(), code.size()))
        return false;

    return m_memory.FlushInstructionCache(m_codeCave, code.size());
}

bool DetourHook::PatchJump()
{
    int64_t disp = static_cast<int64_t>(m_codeCave) - static_cast<int64_t>(m_hookAddress + 5);
    int32_t rel = static_cast<int32_t>(disp);

    std::vector<uint8_t> patch(m_originalBytes.size(), 0x90); // artan byte'lar NOP ile doldurulur
    patch[0] = 0xE9;
    std::memcpy(patch.data() + 1, &rel, sizeof(rel));

    if (!m_memory.WriteBytes(m_hookAddress, patch.data(), patch.size()))
        return false;

    return m_memory.FlushInstructionCache(m_hookAddress, patch.size());
}

bool DetourHook::Uninstall()
{
    if (!m_installed) return true;

    bool restored = m_memory.WriteBytes(m_hookAddress, m_originalBytes.data(), m_originalBytes.size());
    if (restored)
        m_memory.FlushInstructionCache(m_hookAddress, m_originalBytes.size());

    if (m_codeCave)
    {
        m_memory.Free(m_codeCave);
        m_codeCave = 0;
    }

    m_installed = false;
    return restored;
}

bool DetourHook::IsInstalled() const { return m_installed; }
uintptr_t DetourHook::GetCodeCave() const { return m_codeCave; }
const std::wstring& DetourHook::GetLastError() const { return m_lastError; }