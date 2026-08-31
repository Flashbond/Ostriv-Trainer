#include "CameraController.h"
#include "../Core/RemoteMemory.h"
#include "../Core/DetourHook.h"
#include "../Game/OstrivOffsets.h"

namespace
{
    constexpr SIZE_T kSharedStateSize = 0x100;

    void EmitMovRaxImm64(std::vector<uint8_t>& code, uint64_t value)
    {
        code.push_back(0x48);
        code.push_back(0xB8);
        for (int i = 0; i < 8; ++i)
            code.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFF));
    }

    // mov [r15 + disp32], eax
    void EmitMovR15DispEax(std::vector<uint8_t>& code, uint32_t disp)
    {
        code.push_back(0x41);
        code.push_back(0x89);
        code.push_back(0x87);
        for (int i = 0; i < 4; ++i)
            code.push_back(static_cast<uint8_t>((disp >> (i * 8)) & 0xFF));
    }
}

CameraController::CameraController(RemoteMemory& memory)
    : m_memory(memory), m_hook(std::make_unique<DetourHook>(memory)), m_sharedState(0)
{
}

CameraController::~CameraController()
{
    Uninstall();
}

bool CameraController::Install()
{
    m_lastError.clear();

    if (m_hook->IsInstalled())
    {
        m_lastError = L"already installed";
        return false;
    }

    uintptr_t moduleBase = m_memory.GetModuleBase();
    if (moduleBase == 0)
    {
        m_lastError = L"module base is not available";
        return false;
    }

    m_sharedState = m_memory.Allocate(kSharedStateSize, PAGE_READWRITE);
    if (!m_sharedState)
    {
        m_lastError = L"failed to allocate shared state in the target process";
        return false;
    }

    uintptr_t hookAddress = moduleBase + Ostriv::CAMERA_UPDATE_RVA;

    std::vector<uint8_t> originalBytes(
        Ostriv::CAMERA_ORIGINAL_BYTES,
        Ostriv::CAMERA_ORIGINAL_BYTES + Ostriv::CAMERA_HOOK_SIZE);

    std::vector<uint8_t> caveBody = BuildCaveBody(m_sharedState);

    if (!m_hook->Install(hookAddress, originalBytes, caveBody))
    {
        m_lastError = m_hook->GetLastError();
        m_memory.Free(m_sharedState);
        m_sharedState = 0;
        return false;
    }

    uint8_t zeroFlag = 0;
    m_memory.Write(m_sharedState + Ostriv::CAVE_DATA_FLAG_OFFSET, zeroFlag);

    return true;
}

void CameraController::Uninstall()
{
    if (m_hook)
        m_hook->Uninstall();

    if (m_sharedState)
    {
        m_memory.Free(m_sharedState);
        m_sharedState = 0;
    }
}

bool CameraController::IsInstalled() const
{
    return m_hook && m_hook->IsInstalled();
}

const std::wstring& CameraController::GetLastError() const
{
    return m_lastError;
}

bool CameraController::CenterOn(float x, float z)
{
    if (!IsInstalled() || m_sharedState == 0)
        return false;

    // Write the coordinates before the flag, so the hook can never fire
    // mid-write and read a half-updated position.
    if (!m_memory.Write(m_sharedState + Ostriv::CAVE_DATA_X_OFFSET, x))
        return false;

    if (!m_memory.Write(m_sharedState + Ostriv::CAVE_DATA_Z_OFFSET, z))
        return false;

    uint8_t flag = 1;
    return m_memory.Write(m_sharedState + Ostriv::CAVE_DATA_FLAG_OFFSET, flag);
}

std::vector<uint8_t> CameraController::BuildCaveBody(uintptr_t sharedStateAddress)
{
    std::vector<uint8_t> code;

    // push rax — the original instruction doesn't touch rax, but we borrow
    // it below, so it must be restored before falling through to it.
    code.push_back(0x50);

    // mov rax, sharedStateAddress
    EmitMovRaxImm64(code, sharedStateAddress);

    // cmp byte ptr [rax], 1
    code.push_back(0x80);
    code.push_back(0x38);
    code.push_back(0x01);

    // jne <past the teleport block, straight to "pop rax"> — patched below
    // once the teleport block's length is known.
    size_t jneOpcodeIndex = code.size();
    code.push_back(0x75);
    code.push_back(0x00); // placeholder

    // --- teleport block: only runs when the flag is set ---

    // mov eax, [rax + CAVE_DATA_X_OFFSET]
    code.push_back(0x8B);
    code.push_back(0x40);
    code.push_back(static_cast<uint8_t>(Ostriv::CAVE_DATA_X_OFFSET));

    EmitMovR15DispEax(code, static_cast<uint32_t>(Ostriv::CAMERA_CURRENT_X_OFFSET));
    EmitMovR15DispEax(code, static_cast<uint32_t>(Ostriv::CAMERA_TARGET_X_OFFSET));

    // mov rax, sharedStateAddress — reload: the load above clobbered rax's
    // low 32 bits (eax IS the low half of rax).
    EmitMovRaxImm64(code, sharedStateAddress);

    // mov eax, [rax + CAVE_DATA_Z_OFFSET]
    code.push_back(0x8B);
    code.push_back(0x40);
    code.push_back(static_cast<uint8_t>(Ostriv::CAVE_DATA_Z_OFFSET));

    EmitMovR15DispEax(code, static_cast<uint32_t>(Ostriv::CAMERA_CURRENT_Z_OFFSET));
    EmitMovR15DispEax(code, static_cast<uint32_t>(Ostriv::CAMERA_TARGET_Z_OFFSET));

    // mov rax, sharedStateAddress
    EmitMovRaxImm64(code, sharedStateAddress);

    // mov byte ptr [rax], 0 — consume the request
    code.push_back(0xC6);
    code.push_back(0x00);
    code.push_back(0x00);

    // --- end of teleport block; jne above lands exactly here ---
    size_t teleportBlockEnd = code.size();

    int32_t rel = static_cast<int32_t>(teleportBlockEnd - (jneOpcodeIndex + 2));
    code[jneOpcodeIndex + 1] = static_cast<uint8_t>(rel); // comfortably fits rel8 (~57 bytes)

    // pop rax
    code.push_back(0x58);

    return code;
}