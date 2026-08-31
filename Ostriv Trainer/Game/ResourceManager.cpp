#include "ResourceManager.h"
#include "../Core/RemoteMemory.h"
#include "OstrivOffsets.h"

#include <algorithm>

namespace
{
    bool IsPlausibleResourceName(const std::wstring& name)
    {
        if (name.empty() || name.size() > 64)
            return false;

        for (wchar_t ch : name)
        {
            bool ok =
                (ch >= L'A' && ch <= L'Z') ||
                (ch >= L'a' && ch <= L'z') ||
                (ch >= L'0' && ch <= L'9') ||
                ch == L' ' || ch == L'\'' || ch == L'-' || ch == L'.' ||
                ch == L'(' || ch == L')' || ch == L',' || ch == L'/';

            if (!ok)
                return false;
        }

        return true;
    }
}

bool IsExcludedResourceName(const std::wstring& name)
{
    // Not real inventory materials — internal/service entries that
    // happen to pass the character filter because they're plain text.
    static const wchar_t* const excluded[] = {
        L"reforestation",
        L"Bull",
        L"Chicken",
        L"Live chicken",
        L"Pig",
        L"Pig (boar)",
        L"Cow",
        L"Ox",
        L"None",
        L"Sheep",
        L"Sheep (ram)",
        L"Horse",
        L"Horse (female)",
        L"Draft horse",
        
    };

    for (const wchar_t* entry : excluded)
    {
        if (_wcsicmp(name.c_str(), entry) == 0)
            return true;
    }

    return false;
}

ResourceManager::ResourceManager(RemoteMemory& memory, uintptr_t moduleBase)
    : m_memory(memory), m_moduleBase(moduleBase) {
}

bool ResourceManager::GetResourceName(int32_t resourceId, std::wstring& name) const {
    name.clear();

    if (resourceId < 0 || resourceId > 5000) return false;

    uintptr_t entryAddress = m_moduleBase + Ostriv::RESOURCE_TABLE_OFFSET + (static_cast<uintptr_t>(resourceId) * Ostriv::RESOURCE_ENTRY_SIZE);

    uintptr_t nameAddress = 0;
    int32_t nameLength = 0;

    if (!m_memory.Read(entryAddress + Ostriv::RESOURCE_NAME_PTR_OFFSET, nameAddress)) return false;
    if (!m_memory.Read(entryAddress + Ostriv::RESOURCE_NAME_LENGTH_OFFSET, nameLength)) return false;

    if (nameAddress == 0 || nameLength <= 0 || nameLength > 256) return false;

    return m_memory.ReadUTF16String(nameAddress, name, static_cast<size_t>(nameLength));
}

void ResourceManager::BuildResourceCache()
{
    if (m_cacheBuilt)
        return;

    m_knownResources.clear();

    for (int32_t id = 0; id <= 5000; ++id)
    {
        std::wstring name;
        if (GetResourceName(id, name) && IsPlausibleResourceName(name) && !IsExcludedResourceName(name))
            m_knownResources.emplace_back(id, name);
    }

    std::sort(m_knownResources.begin(), m_knownResources.end(),
        [](const auto& a, const auto& b) { return a.second < b.second; });

    m_cacheBuilt = true;
}

const std::vector<std::pair<int32_t, std::wstring>>& ResourceManager::GetKnownResources() const
{
    return m_knownResources;
}
