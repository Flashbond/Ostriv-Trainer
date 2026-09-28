#pragma once

#include <vector>
#include <utility>
#include <cstdint>
#include <string>

class RemoteMemory;

class ResourceManager
{
public:
    ResourceManager(RemoteMemory&, uintptr_t tableAddress);

    bool GetResourceName(int32_t resourceId, std::wstring& name) const;

    // Scans every plausible resource id once and caches the ones that
    // resolve to a real name — used to populate the "add resource"
    // dropdown. Read-only, call once after connecting.
    void BuildResourceCache();
    const std::vector<std::pair<int32_t, std::wstring>>& GetKnownResources() const;

private:
    RemoteMemory& m_memory;
    uintptr_t m_tableAddress;

    std::vector<std::pair<int32_t, std::wstring>> m_knownResources;
    bool m_cacheBuilt = false;
};