#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "Inventory.h"

class RemoteMemory;
class ResourceManager;

class Building
{
public:
    enum class BuildingKind
    {
        Normal,
        Child,
        Container
    };

    Building(RemoteMemory& memory, ResourceManager& resourceManager);

    bool ResolveIdentity(uintptr_t address);
    bool IsIdentityResolved() const;

    // Reads isActive/isDemolishing from this building's own address
    // (Normal/Container) or its parent's (Child) — a Child's status is
    // never meaningful on its own object, only on the RowHouse that owns
    // it. A single fresh reading, taken as-is — no debouncing.
    bool RefreshStatus();

    bool HasPendingInventory() const;
    bool RefreshInventory();

    bool IsValid() const;

    bool GetActiveStatus() const;
    bool IsDemolishing() const;
    bool HasResource() const;

    bool HasFamilyMoney() const;
    float GetFamilyMoney() const;
    bool SetFamilyMoney(float amount);

    uintptr_t GetAddress() const;

    uint32_t GetType() const;
    BuildingKind GetKind() const;

    const std::wstring& GetDictionaryName() const;
    const std::wstring& GetId() const;
    const std::wstring& GetUniqueId() const;
    const std::wstring& GetPersistentId() const;
    const std::wstring& GetDisplayName() const;

    float GetPositionX() const;
    float GetPositionY() const;

    Inventory& GetInventory();
    const Inventory& GetInventory() const;

    static bool BuildDictionaryCache(RemoteMemory& memory);
    static const std::vector<std::wstring>& GetKnownTypeNames();
    static void ResetDictionaryCache();

private:
    bool ReadBuildingName();
    bool ReadInventoryType();
    bool ReadBuildingId();

    bool ReadActiveAndDemolishingState(uintptr_t sourceAddress);
    bool ReadPositionFrom(uintptr_t sourceAddress);
    bool ResolveParentAddress();

    bool ReadUniqueId(uintptr_t* outUniqueId = 0);
    static std::wstring MakePositionBasedId(float x, float y);

    bool FindBuildingEntry(const std::string& selectedId, uintptr_t& foundEntry);

    static bool IsRelevantBuildingId(const std::wstring& id);
    BuildingKind ClassifyBuildingKind(const std::wstring& id);

    static std::unordered_map<std::string, uintptr_t> s_dictionaryCache;
    static bool s_isDictionaryLoaded;
    static std::vector<std::wstring> s_knownTypeNames;

private:
    RemoteMemory& m_memory;
    ResourceManager& m_resourceManager;

    uintptr_t m_address;
    uintptr_t m_parentAddress;
    uintptr_t m_parentOffset;

    uint32_t m_type;
    BuildingKind m_kind;

    bool m_isActive;
    bool m_isDemolishing;
    bool m_activeStatus;

    bool m_hasResource;
    bool m_isResidential;
    bool m_hasFamilyMoney;
    float m_familyMoney;
    uintptr_t m_familyAddress;

    bool m_identityResolved;

    std::wstring m_dictionaryName;
    std::wstring m_id;
    std::wstring m_uniqueId;
    std::wstring m_persistentId;
    std::wstring m_displayName;

    float m_positionX;
    float m_positionY;

    Inventory m_inventory;
};