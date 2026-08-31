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

    bool RefreshStatus();

    bool HasPendingInventory() const;
    bool RefreshInventory();

    bool IsValid() const;

    bool GetActiveStatus() const;
    bool GetRawActiveStatus() const;
    bool IsDemolishing() const;
    bool HasResource() const;

    bool HasFamilyMoney() const;
    float GetFamilyMoney() const;

    // Writes directly to the resident family's savings, for Apartment /
    // Village house / Fenceless village house only. Returns false if this
    // building isn't residential or no family has moved in yet.
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

    void MarkSeenThisTick();
    int MarkMissingThisTick();
    int GetMissingTicks() const;

    int MarkPresentTick();
    int GetPresentTicks() const;

    void MarkListVisible();
    bool IsListVisible() const;

    void AdvanceActiveStability();
    int GetActiveStabilityTicks() const;

    void PromoteActiveStatus();

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
    bool m_rawActiveStatus;

    bool m_activeStatus;
    bool m_previousRawActiveStatus;
    int m_activeStabilityTicks;

    bool m_hasResource;
    bool m_isResidential;
    bool m_hasFamilyMoney;
    float m_familyMoney;
    uintptr_t m_familyAddress; // cached by RefreshInventory(); 0 if no family moved in yet

    int m_missingTicks;
    int m_presentTicks;
    bool m_listVisible;

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