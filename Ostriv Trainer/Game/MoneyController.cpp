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

    uintptr_t instruction = m_memory.FindPattern(Ostriv::MONEY_STATE_POINTER_SIGNATURE);
    if (!instruction)
        return false;

    uintptr_t resolved = m_memory.ResolveRipRelative(
        instruction,
        Ostriv::MONEY_STATE_POINTER_DISPLACEMENT_OFFSET,
        Ostriv::MONEY_STATE_POINTER_INSTRUCTION_LENGTH);

    if (!resolved)
        return false;

    m_statePointerAddress = resolved;
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