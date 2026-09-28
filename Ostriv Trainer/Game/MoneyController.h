#pragma once

#include <cstdint>
#include "../Offsets/OffsetResolver.h"

class RemoteMemory;

// Reads and writes the player's global money value. The underlying state
// pointer's own address is found once via a signature scan (the pointer
// itself moves every session, but the instruction that loads it doesn't).
class MoneyController
{
public:
    explicit MoneyController(RemoteMemory& memory);

    // One-time offset check. Call once after attaching.
    bool Resolve(const ResolvedOffsets& offsets);

    bool IsResolved() const;

    bool GetMoney(double& money) const;
    bool SetMoney(double money);

private:
    bool ResolveMoneyAddress(uintptr_t& outAddress) const;

private:
    RemoteMemory& m_memory;
    uintptr_t m_statePointerAddress;
};