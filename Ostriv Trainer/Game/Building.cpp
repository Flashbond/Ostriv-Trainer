#include "Building.h"

#include "../Core/RemoteMemory.h"
#include "OstrivOffsets.h"
#include "ResourceManager.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>

std::unordered_map<std::string, uintptr_t> Building::s_dictionaryCache;
bool Building::s_isDictionaryLoaded = false;
std::vector<std::wstring> Building::s_knownTypeNames;
static std::mutex g_dictionaryInitMutex;

Building::Building(RemoteMemory& memory, ResourceManager& resourceManager)
    : m_memory(memory),
    m_resourceManager(resourceManager),
    m_address(0),
    m_parentAddress(0),
    m_parentOffset(0),
    m_type(0),
    m_kind(BuildingKind::Normal),
    m_isActive(false),
    m_isDemolishing(false),
    m_activeStatus(false),
    m_hasResource(false),
    m_isResidential(false),
    m_hasFamilyMoney(false),
    m_familyMoney(0.0f),
    m_familyAddress(0),
    m_identityResolved(false),
    m_positionX(0.0f),
    m_positionY(0.0f),
    m_inventory(memory, resourceManager)
{
}

bool Building::ResolveIdentity(uintptr_t address)
{
    if (m_identityResolved)
        return true;

    m_address = address;

    if (!ReadInventoryType())
        return false;

    // ">=" against the floor, not "==" — see the constant's comment in
    // OstrivOffsets.h for why an exact match silently breaks on any
    // building whose inventory has grown past its starting capacity.
    if (m_type < Ostriv::TARGET_INVENTORY_TYPE && m_type != Ostriv::ROWHOUSE_INVENTORY_TYPE)
        return false;

    if (!ReadBuildingId())
        return false;

    if (!IsRelevantBuildingId(m_id))
        return false;

    if (!ReadBuildingName())
        return false;

    if (m_id.find(L"fenceless") != std::wstring::npos)
        m_dictionaryName = L"Fenceless village house";

    if (m_type == Ostriv::ROWHOUSE_INVENTORY_TYPE)
    {
        if (m_id.rfind(L"building_rowhouse_corner-", 0) == 0)
        {
            m_dictionaryName = L"Rowhouse (corner)";
            m_kind = BuildingKind::Container;
        }
        else if (m_id.rfind(L"building_rowhouse-", 0) == 0)
        {
            m_dictionaryName = L"Rowhouse";
            m_kind = BuildingKind::Container;
        }
        else
        {
            return false;
        }
    }
    else
    {
        m_kind = ClassifyBuildingKind(m_id);
    }

    m_isResidential =
        m_dictionaryName == L"Apartment" ||
        m_dictionaryName == L"Village house" ||
        m_dictionaryName == L"Fenceless village house";

    uintptr_t stateSource = m_address;

    if (m_kind == BuildingKind::Child)
    {
        if (!ResolveParentAddress())
            return false;

        stateSource = m_parentAddress;
    }

    if (m_kind == BuildingKind::Container)
    {
        if (!ReadPositionFrom(m_address))
            return false;

        m_uniqueId = MakePositionBasedId(m_positionX, m_positionY);
    }
    else
    {
        if (!ReadUniqueId())
            return false;

        if (!ReadPositionFrom(stateSource))
            return false;
    }

    m_persistentId = m_uniqueId + L"_" + m_dictionaryName;
    m_displayName = m_dictionaryName + L"_" + m_uniqueId;

    m_identityResolved = true;

    return true;
}

bool Building::IsIdentityResolved() const { return m_identityResolved; }

bool Building::RefreshStatus()
{
    uintptr_t stateSource = m_address;
    if (m_kind == BuildingKind::Child)
        stateSource = m_parentAddress;

    if (!ReadActiveAndDemolishingState(stateSource))
        return false;

    m_activeStatus = m_isActive && !m_isDemolishing;

    return true;
}

bool Building::HasPendingInventory() const
{
    if (m_kind == BuildingKind::Container)
        return false;

    if (m_isResidential)
        return true;

    uint8_t hasResourceValue = 0;
    if (!m_memory.Read(m_address + Ostriv::INVENTORY_COUNT_OFFSET, hasResourceValue))
        return false;

    return hasResourceValue != 0;
}

bool Building::RefreshInventory()
{
    m_hasFamilyMoney = false;
    m_familyMoney = 0.0f;

    uint8_t goodsCount = 0;
    bool hasGoods = m_memory.Read(m_address + Ostriv::INVENTORY_COUNT_OFFSET, goodsCount) && goodsCount != 0;

    if (hasGoods)
    {
        if (m_inventory.Update(m_address))
            m_hasResource = true;
        else
        {
            m_inventory.Clear();
            m_hasResource = false;
        }
    }
    else
    {
        m_inventory.Clear();
        m_hasResource = false;
    }

    if (m_isResidential)
    {
        uintptr_t familyAddress = 0;

        if (m_memory.Read(m_address + Ostriv::INGAME_BUILDING_FAMILY_POINTER_OFFSET, familyAddress) && familyAddress != 0)
        {
            m_familyAddress = familyAddress;

            float money = 0.0f;
            if (m_memory.Read(familyAddress + Ostriv::FAMILY_MONEY_OFFSET, money))
            {
                m_familyMoney = money;
                m_hasFamilyMoney = true;
            }
        }
        else
        {
            m_familyAddress = 0;
        }
    }

    return m_hasResource || m_hasFamilyMoney;
}

bool Building::SetFamilyMoney(float amount)
{
    if (!m_isResidential || m_familyAddress == 0)
        return false;

    if (!m_memory.Write(m_familyAddress + Ostriv::FAMILY_MONEY_OFFSET, amount))
        return false;

    m_familyMoney = amount;
    m_hasFamilyMoney = true;
    return true;
}

bool Building::IsValid() const { return m_address != 0; }
bool Building::GetActiveStatus() const { return m_activeStatus; }
bool Building::IsDemolishing() const { return m_isDemolishing; }
bool Building::HasResource() const { return m_hasResource; }
bool Building::HasFamilyMoney() const { return m_hasFamilyMoney; }
float Building::GetFamilyMoney() const { return m_familyMoney; }
uintptr_t Building::GetAddress() const { return m_address; }
uint32_t Building::GetType() const { return m_type; }
Building::BuildingKind Building::GetKind() const { return m_kind; }
const std::wstring& Building::GetDictionaryName() const { return m_dictionaryName; }
const std::wstring& Building::GetId() const { return m_id; }
const std::wstring& Building::GetUniqueId() const { return m_uniqueId; }
const std::wstring& Building::GetPersistentId() const { return m_persistentId; }
const std::wstring& Building::GetDisplayName() const { return m_displayName; }
float Building::GetPositionX() const { return m_positionX; }
float Building::GetPositionY() const { return m_positionY; }
Inventory& Building::GetInventory() { return m_inventory; }
const Inventory& Building::GetInventory() const { return m_inventory; }

Building::BuildingKind Building::ClassifyBuildingKind(const std::wstring& id)
{
    if (id.find(L"apartment") != std::wstring::npos)
    {
        m_parentOffset = Ostriv::INGAME_BUILDING_PARENT_OFFSET_APARTMENT;
        return BuildingKind::Child;
    }

    if (id.find(L"rowhouse_shop") != std::wstring::npos)
    {
        m_parentOffset = Ostriv::INGAME_BUILDING_PARENT_OFFSET_SHOP;
        return BuildingKind::Child;
    }

    return BuildingKind::Normal;
}

bool Building::IsRelevantBuildingId(const std::wstring& id)
{
    if (id.rfind(L"building_", 0) != 0) return false;
    if (id.find(L"deposit") != std::wstring::npos) return false;
    if (id.find(L"clay_pit") != std::wstring::npos) return false;
    if (id.find(L"sand_pit") != std::wstring::npos) return false;
    if (id.find(L"resource") != std::wstring::npos) return false;
    if (id.find(L"bench") != std::wstring::npos) return false;
    if (id.find(L"gazebo") != std::wstring::npos) return false;
    return true;
}

bool Building::ReadBuildingName()
{
    std::string selectedId(m_id.begin(), m_id.end());

    if (selectedId.empty())
        return false;

    uintptr_t foundEntry = 0;

    if (!FindBuildingEntry(selectedId, foundEntry)) {
        m_dictionaryName = m_id;
        return true;
    }

    uintptr_t nameAddress = 0;
    int32_t nameLength = 0;

    if (!m_memory.Read(foundEntry + Ostriv::BUILDING_DICTIONARY_NAME_PTR_OFFSET, nameAddress) ||
        !m_memory.Read(foundEntry + Ostriv::BUILDING_DICTIONARY_NAME_LENGTH_OFFSET, nameLength) ||
        nameAddress == 0 || nameLength <= 0 || nameLength > 256)
    {
        m_dictionaryName = m_id;
        return true;
    }

    if (!m_memory.ReadUTF16String(nameAddress, m_dictionaryName, nameLength)) {
        m_dictionaryName = m_id;
    }

    return true;
}

bool Building::ReadBuildingId() {
    uintptr_t idAddress = 0;
    int32_t idLength = 0;

    if (!m_memory.Read(m_address + Ostriv::INGAME_BUILDING_ID_PTR_OFFSET, idAddress))
        return false;

    if (!m_memory.Read(m_address + Ostriv::INGAME_BUILDING_ID_LENGTH_OFFSET, idLength))
        return false;

    if (idAddress == 0 || idLength <= 0 || idLength > Ostriv::MAX_BUILDING_ID_LENGTH)
        return false;

    std::vector<char> buffer(idLength + 1, '\0');
    SIZE_T bytesRead = 0;

    if (!ReadProcessMemory(m_memory.GetProcessHandle(), reinterpret_cast<LPCVOID>(idAddress), buffer.data(), idLength, &bytesRead) || bytesRead == 0)
        return false;

    size_t actualLength = 0;
    while (actualLength < bytesRead && buffer[actualLength] != '\0') {
        ++actualLength;
    }

    std::string asciiId(buffer.data(), actualLength);
    m_id = std::wstring(asciiId.begin(), asciiId.end());

    return true;
}

bool Building::FindBuildingEntry(const std::string& selectedId, uintptr_t& foundEntry)
{
    foundEntry = 0;

    if (selectedId.empty())
        return false;

    auto it = s_dictionaryCache.find(selectedId);
    if (it != s_dictionaryCache.end())
    {
        foundEntry = it->second;
        return true;
    }

    return false;
}

bool Building::BuildDictionaryCache(RemoteMemory& memory)
{
    if (s_isDictionaryLoaded)
        return true;

    std::lock_guard<std::mutex> lock(g_dictionaryInitMutex);

    if (s_isDictionaryLoaded)
        return true;

    uintptr_t moduleBase = memory.GetModuleBase();

    uintptr_t table = 0;
    int32_t count = 0;

    if (!memory.Read(moduleBase + Ostriv::BUILDING_DICTIONARY_TABLE_POINTER_OFFSET, table))
        return false;

    if (!memory.Read(moduleBase + Ostriv::BUILDING_DICTIONARY_TABLE_COUNT_OFFSET, count))
        return false;

    if (!table || count <= 0)
        return false;

    if (count > Ostriv::BUILDING_DICTIONARY_TABLE_MAX_COUNT)
        return false;

    std::unordered_map<std::string, uintptr_t> tempCache;
    tempCache.reserve(static_cast<size_t>(count));

    std::vector<std::wstring> typeNames;
    typeNames.reserve(static_cast<size_t>(count));

    for (int32_t i = 0; i < count; ++i)
    {
        uintptr_t entry = table + static_cast<uintptr_t>(i) * Ostriv::BUILDING_DICTIONARY_ENTRY_SIZE;
        uintptr_t idPtr = 0;

        if (!memory.Read(entry + Ostriv::BUILDING_DICTIONARY_ID_PTR_OFFSET, idPtr) || !idPtr)
            continue;

        char buffer[256] = { 0 };
        SIZE_T bytesRead = 0;

        if (!ReadProcessMemory(memory.GetProcessHandle(), reinterpret_cast<LPCVOID>(idPtr), buffer, sizeof(buffer) - 1, &bytesRead) || bytesRead == 0)
            continue;

        buffer[bytesRead] = '\0';
        std::string id(buffer);

        tempCache.emplace(id, entry);

        std::wstring wideId(id.begin(), id.end());
        if (!IsRelevantBuildingId(wideId))
            continue;

        uintptr_t nameAddress = 0;
        int32_t nameLength = 0;
        std::wstring displayName;

        bool resolved =
            memory.Read(entry + Ostriv::BUILDING_DICTIONARY_NAME_PTR_OFFSET, nameAddress) &&
            memory.Read(entry + Ostriv::BUILDING_DICTIONARY_NAME_LENGTH_OFFSET, nameLength) &&
            nameAddress != 0 && nameLength > 0 && nameLength <= 256 &&
            memory.ReadUTF16String(nameAddress, displayName, nameLength);

        if (!resolved)
            displayName = wideId;

        if (displayName.empty())
            continue;

        typeNames.push_back(std::move(displayName));
    }

    if (std::find(typeNames.begin(), typeNames.end(), L"Fenceless village house") == typeNames.end())
        typeNames.push_back(L"Fenceless village house");

    std::sort(typeNames.begin(), typeNames.end());

    s_dictionaryCache = std::move(tempCache);
    s_knownTypeNames = std::move(typeNames);
    s_isDictionaryLoaded = true;
    return true;
}

const std::vector<std::wstring>& Building::GetKnownTypeNames() { return s_knownTypeNames; }

void Building::ResetDictionaryCache()
{
    std::lock_guard<std::mutex> lock(g_dictionaryInitMutex);
    s_dictionaryCache.clear();
    s_knownTypeNames.clear();
    s_isDictionaryLoaded = false;
}

bool Building::ReadInventoryType()
{
    return m_memory.Read(m_address + Ostriv::BUILDING_INVENTORY_TYPE_OFFSET, m_type);
}

bool Building::ReadActiveAndDemolishingState(uintptr_t sourceAddress)
{
    float activeValue = 0.0f;
    uint8_t demolishingValue = 0;

    if (!m_memory.Read(sourceAddress + Ostriv::INGAME_BUILDING_ACTIVE_STATUS_OFFSET, activeValue)) return false;
    if (!m_memory.Read(sourceAddress + Ostriv::INGAME_BUILDING_DEMOLISHING_STATUS_OFFSET, demolishingValue)) return false;

    m_isActive = std::abs(activeValue - 1.0f) < 0.001f;
    m_isDemolishing = (demolishingValue == 1);

    return true;
}

bool Building::ReadPositionFrom(uintptr_t sourceAddress)
{
    return m_memory.Read(sourceAddress + Ostriv::INGAME_BUILDING_POSITION_X_OFFSET, m_positionX) &&
        m_memory.Read(sourceAddress + Ostriv::INGAME_BUILDING_POSITION_Y_OFFSET, m_positionY);
}

bool Building::ResolveParentAddress()
{
    if (m_parentAddress != 0)
        return true;

    if (m_parentOffset == 0)
        return false;

    uintptr_t parent = 0;
    if (!m_memory.Read(m_address + m_parentOffset, parent) || parent == 0)
        return false;

    if (parent < Ostriv::MIN_VALID_POINTER || parent > Ostriv::MAX_VALID_POINTER)
        return false;

    m_parentAddress = parent;
    return true;
}

bool Building::ReadUniqueId(uintptr_t* outUniqueId)
{
    DWORD64 invArrayPtr = 0;

    if (!m_memory.Read(m_address + Ostriv::INVENTORY_ARRAY_OFFSET, invArrayPtr) || invArrayPtr == 0)
    {
        if (outUniqueId) *outUniqueId = 0;
        m_uniqueId = L"0";
        return false;
    }

    uint64_t idValue = 0;
    if (!m_memory.Read(invArrayPtr + Ostriv::INVENTORY_ID_OFFSET, idValue) || idValue == 0)
    {
        if (outUniqueId) *outUniqueId = 0;
        m_uniqueId = L"0";
        return false;
    }

    if (outUniqueId) *outUniqueId = static_cast<uintptr_t>(idValue);

    m_uniqueId = std::to_wstring(idValue);
    return true;
}

std::wstring Building::MakePositionBasedId(float x, float y)
{
    int64_t ix = static_cast<int64_t>(x);
    int64_t iy = static_cast<int64_t>(y);
    return L"pos_" + std::to_wstring(ix) + L"_" + std::to_wstring(iy);
}