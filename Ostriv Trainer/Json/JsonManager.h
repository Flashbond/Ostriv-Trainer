#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>

struct ResourceLock
{
    int32_t resourceId = -1;
    float amount = 0.0f;
};

struct BuildingRecord
{
    std::wstring uniqueId;
    std::wstring dictionaryName;
    std::wstring persistentId;
    std::wstring customName;

    bool active = false;

    std::vector<ResourceLock> resourceLocks;
};

class JsonManager
{
public:
    JsonManager();

    bool Load();
    bool Save() const;

    bool GetAlwaysOnTop() const;
    void SetAlwaysOnTop(bool value);

    bool GetShowOnlyOwnedTypes() const;
    void SetShowOnlyOwnedTypes(bool value);

    bool GetMoneyLocked() const;
    double GetMoneyLockAmount() const;
    void SetMoneyLock(bool locked, double amount);

    bool Exists() const;

    // O(1) — backed by an index rebuilt only on Load()/AddBuilding(), never
    // on every call.
    BuildingRecord* FindBuilding(const std::wstring& uniqueId);
    const BuildingRecord* FindBuilding(const std::wstring& uniqueId) const;

    bool AddBuilding(const BuildingRecord& record);
    bool UpdateBuilding(const BuildingRecord& record);

    // Creates or replaces the lock for this resourceId on this building.
    // Returns false if the building isn't registered yet.
    bool SetResourceLock(const std::wstring& uniqueId, int32_t resourceId, float amount);

    // Returns false if the building isn't registered, or had no lock for
    // this resourceId to begin with.
    bool ClearResourceLock(const std::wstring& uniqueId, int32_t resourceId);

    // Every building that currently has at least one resource lock.
    // Rebuilt incrementally as locks are set/cleared — never recomputed by
    // scanning all buildings.
    const std::vector<BuildingRecord*>& GetLockedRecords() const;

    const std::vector<std::unique_ptr<BuildingRecord>>& GetBuildings() const;

private:
    std::wstring GetFilePath() const;

private:
    // unique_ptr storage: pointers handed out by FindBuilding()/indexed in
    // m_index/m_lockedRecords stay valid even as m_buildings grows and
    // reallocates its own internal array.
    std::vector<std::unique_ptr<BuildingRecord>> m_buildings;

    std::unordered_map<std::wstring, BuildingRecord*> m_index;
    std::vector<BuildingRecord*> m_lockedRecords;

    bool m_showOnlyOwnedTypes = true;

    bool m_alwaysOnTop = false;
    bool m_loaded = false;

    bool m_moneyLocked = false;
    double m_moneyLockAmount = 0.0;
};