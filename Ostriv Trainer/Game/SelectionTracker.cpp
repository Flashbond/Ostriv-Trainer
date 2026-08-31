#include "SelectionTracker.h"
#include "../Core/RemoteMemory.h"
#include "../Core/DetourHook.h"
#include "OstrivOffsets.h"

namespace
{
    // Layout of the shared-memory block written into the target process:
    //   +0x00 (8 bytes): last captured building pointer (RCX at hook time)
    //   +0x08 (8 bytes): hit counter, incremented every time the hook fires
    constexpr uintptr_t kBuildingPtrOffset = 0x00;
    constexpr uintptr_t kCounterOffset = 0x08;
    constexpr SIZE_T kSharedStateSize = 0x1000;
}

SelectionTracker::SelectionTracker(RemoteMemory& memory)
    : m_memory(memory),
    m_hook(std::make_unique<DetourHook>(memory)),
    m_sharedState(0),
    m_selectedBuildingAddress(0),
    m_lastCounter(0)
{
}

SelectionTracker::~SelectionTracker()
{
    Uninstall();
}

bool SelectionTracker::Install()
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

    uintptr_t hookAddress = moduleBase + Ostriv::SELECTION_HOOK_RVA;

    std::vector<uint8_t> originalBytes(
        Ostriv::SELECTION_HOOK_ORIGINAL_BYTES,
        Ostriv::SELECTION_HOOK_ORIGINAL_BYTES + Ostriv::SELECTION_HOOK_SIZE);

    std::vector<uint8_t> caveBody = BuildCaveBody(m_sharedState);

    if (!m_hook->Install(hookAddress, originalBytes, caveBody))
    {
        m_lastError = m_hook->GetLastError();
        m_memory.Free(m_sharedState);
        m_sharedState = 0;
        return false;
    }

    uint64_t zero = 0;
    m_memory.Write(m_sharedState + kBuildingPtrOffset, zero);
    m_memory.Write(m_sharedState + kCounterOffset, zero);

    m_selectedBuildingAddress = 0;
    m_lastCounter = 0;

    return true;
}

void SelectionTracker::Uninstall()
{
    if (m_hook)
        m_hook->Uninstall();

    if (m_sharedState)
    {
        m_memory.Free(m_sharedState);
        m_sharedState = 0;
    }

    m_selectedBuildingAddress = 0;
    m_lastCounter = 0;
}

bool SelectionTracker::IsInstalled() const
{
    return m_hook && m_hook->IsInstalled();
}

bool SelectionTracker::Update()
{
    if (!IsInstalled() || m_sharedState == 0)
    {
        m_selectedBuildingAddress = 0;
        return false;
    }

    uint64_t selected = 0;
    uint64_t counter = 0;

    if (!m_memory.Read(m_sharedState + kBuildingPtrOffset, selected))
        return false; // transient read failure — keep the last known state

    if (!m_memory.Read(m_sharedState + kCounterOffset, counter))
        return false;

    // The hook did not fire since the last poll: no building panel was
    // rendered this interval, which means nothing is selected anymore.
    if (counter == m_lastCounter)
    {
        m_selectedBuildingAddress = 0;
        return true;
    }

    m_lastCounter = counter;
    m_selectedBuildingAddress = static_cast<uintptr_t>(selected);

    return true;
}

uintptr_t SelectionTracker::GetSelectedBuildingAddress() const
{
    return m_selectedBuildingAddress;
}

std::vector<uint8_t> SelectionTracker::BuildCaveBody(uintptr_t sharedStateAddress)
{
    std::vector<uint8_t> code;

    // pushfq / push rax — save flags and rax, both are clobbered below.
    code.push_back(0x9C);
    code.push_back(0x50);

    // mov rax, sharedStateAddress
    code.push_back(0x48);
    code.push_back(0xB8);
    for (int i = 0; i < 8; ++i)
        code.push_back(static_cast<uint8_t>((sharedStateAddress >> (i * 8)) & 0xFF));

    // mov [rax+0x00], rcx — RCX holds the selected building pointer here.
    code.push_back(0x48);
    code.push_back(0x89);
    code.push_back(0x08);

    // inc qword ptr [rax+0x08] — bump the hit counter every time this fires.
    code.push_back(0x48);
    code.push_back(0xFF);
    code.push_back(0x40);
    code.push_back(static_cast<uint8_t>(kCounterOffset));

    // pop rax / popfq — restore what we clobbered, before the original instruction runs.
    code.push_back(0x58);
    code.push_back(0x9D);

    return code;
}
const std::wstring& SelectionTracker::GetLastError() const { return m_lastError; }