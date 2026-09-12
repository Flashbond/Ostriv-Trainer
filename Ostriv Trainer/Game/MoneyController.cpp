#include "MoneyController.h"
#include "../Core/RemoteMemory.h"
#include "OstrivOffsets.h"

MoneyController::MoneyController(RemoteMemory& memory)
    : m_memory(memory), m_statePointerAddress(0)
{
}

bool MoneyController::Resolve()
{
    m_statePointerAddress = 0;

    uintptr_t candidate = m_memory.GetModuleBase() + Ostriv::MONEY_STATE_POINTER_OFFSET;

    // Sanity-check the slot actually holds a plausible pointer before
    // trusting it — cheap and catches an offset that's silently wrong
    // (e.g. after a future game update) instead of quietly resolving to
    // garbage.
    uintptr_t statePointer = 0;
    if (!m_memory.Read(candidate, statePointer) || statePointer == 0)
        return false;

    if (statePointer < Ostriv::MIN_VALID_POINTER || statePointer > Ostriv::MAX_VALID_POINTER)
        return false;

    m_statePointerAddress = candidate;
    return true;
}

bool MoneyController::IsResolved() const
{
    return m_statePointerAddress != 0;
}

bool MoneyController::ResolveMoneyAddress(uintptr_t& outAddress) const
{
    outAddress = 0;

    if (!m_statePointerAddress)
        return false;

    uintptr_t statePointer = 0;
    if (!m_memory.Read(m_statePointerAddress, statePointer) || statePointer == 0)
        return false;

    outAddress = statePointer + Ostriv::MONEY_OFFSET;
    return true;
}

bool MoneyController::GetMoney(double& money) const
{
    money = 0.0;

    uintptr_t address = 0;
    if (!ResolveMoneyAddress(address))
        return false;

    return m_memory.Read(address, money);
}

bool MoneyController::SetMoney(double money)
{
    uintptr_t address = 0;
    if (!ResolveMoneyAddress(address))
        return false;

    return m_memory.Write(address, money);
}