#include "JsonManager.h"

#include <nlohmann/json.hpp>

#include <Windows.h>
#include <algorithm>
#include <filesystem>
#include <fstream>

using json = nlohmann::json;

namespace
{
    std::string ToUtf8(const std::wstring& value)
    {
        if (value.empty())
            return {};

        int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
        if (size <= 0)
            return {};

        std::string result(size, '\0');
        WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
        return result;
    }

    std::wstring FromUtf8(const std::string& value)
    {
        if (value.empty())
            return {};

        int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
        if (size <= 0)
            return {};

        std::wstring result(size, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), result.data(), size);
        return result;
    }
}

JsonManager::JsonManager()
{
}

bool JsonManager::Load()
{
    if (m_loaded)
        return true; // already loaded this session — in-memory state (including any pre-connect toggles) is authoritative, don't re-read

    m_buildings.clear();
    m_index.clear();
    m_lockedRecords.clear();
    m_showOnlyOwnedTypes = true;
    m_alwaysOnTop = false;

    if (!Exists())
        return true;

    std::ifstream file(GetFilePath(), std::ios::binary);
    if (!file.is_open())
        return false;

    json root;
    try { file >> root; }
    catch (...) { return false; }

    if (!root.is_object())
        return false;

    if (root.contains("settings") && root["settings"].is_object())
    {
        const auto& settings = root["settings"];
        if (settings.contains("showOnlyOwnedTypes") && settings["showOnlyOwnedTypes"].is_boolean())
            m_showOnlyOwnedTypes = settings["showOnlyOwnedTypes"].get<bool>();

        if (settings.contains("alwaysOnTop") && settings["alwaysOnTop"].is_boolean())
            m_alwaysOnTop = settings["alwaysOnTop"].get<bool>();
    }

    if (!root.contains("buildings") || !root["buildings"].is_array())
        return true;

    for (const auto& item : root["buildings"])
    {
        if (!item.is_object())
            continue;

        BuildingRecord record;

        if (item.contains("uniqueId") && item["uniqueId"].is_string())
            record.uniqueId = FromUtf8(item["uniqueId"].get<std::string>());

        if (record.uniqueId.empty())
            continue;

        if (item.contains("dictionaryName") && item["dictionaryName"].is_string())
            record.dictionaryName = FromUtf8(item["dictionaryName"].get<std::string>());

        if (item.contains("persistentId") && item["persistentId"].is_string())
            record.persistentId = FromUtf8(item["persistentId"].get<std::string>());

        if (item.contains("customName") && item["customName"].is_string())
            record.customName = FromUtf8(item["customName"].get<std::string>());

        if (item.contains("active") && item["active"].is_boolean())
            record.active = item["active"].get<bool>();

        if (item.contains("resourceLocks") && item["resourceLocks"].is_array())
        {
            for (const auto& lockItem : item["resourceLocks"])
            {
                if (!lockItem.is_object())
                    continue;

                ResourceLock lock;

                if (lockItem.contains("resourceId") && lockItem["resourceId"].is_number_integer())
                    lock.resourceId = lockItem["resourceId"].get<int32_t>();

                if (lockItem.contains("amount") && lockItem["amount"].is_number())
                    lock.amount = lockItem["amount"].get<float>();

                if (lock.resourceId != -1)
                    record.resourceLocks.push_back(lock);
            }
        }

        auto ptr = std::make_unique<BuildingRecord>(std::move(record));
        BuildingRecord* rawPtr = ptr.get();

        m_index[rawPtr->uniqueId] = rawPtr;
        if (!rawPtr->resourceLocks.empty())
            m_lockedRecords.push_back(rawPtr);

        m_buildings.push_back(std::move(ptr));
    }

    return true;
}

bool JsonManager::Save() const
{
    json root;

    root["version"] = 1;

    root["settings"] = json::object();
    root["settings"]["showOnlyOwnedTypes"] = m_showOnlyOwnedTypes;
    root["settings"]["alwaysOnTop"] = m_alwaysOnTop;

    root["buildings"] = json::array();

    for (const auto& buildingPtr : m_buildings)
    {
        const BuildingRecord& building = *buildingPtr;

        json item;
        item["uniqueId"] = ToUtf8(building.uniqueId);
        item["dictionaryName"] = ToUtf8(building.dictionaryName);
        item["persistentId"] = ToUtf8(building.persistentId);
        item["customName"] = ToUtf8(building.customName);
        item["active"] = building.active;

        item["resourceLocks"] = json::array();
        for (const auto& lock : building.resourceLocks)
        {
            json lockItem;
            lockItem["resourceId"] = lock.resourceId;
            lockItem["amount"] = lock.amount;
            item["resourceLocks"].push_back(std::move(lockItem));
        }

        root["buildings"].push_back(std::move(item));
    }

    std::ofstream file(GetFilePath(), std::ios::binary | std::ios::trunc);
    if (!file.is_open())
        return false;

    file << root.dump(4);
    return file.good();
}

bool JsonManager::Exists() const
{
    return std::filesystem::exists(GetFilePath());
}

BuildingRecord* JsonManager::FindBuilding(const std::wstring& uniqueId)
{
    if (uniqueId.empty())
        return nullptr;

    auto it = m_index.find(uniqueId);
    return it != m_index.end() ? it->second : nullptr;
}

const BuildingRecord* JsonManager::FindBuilding(const std::wstring& uniqueId) const
{
    if (uniqueId.empty())
        return nullptr;

    auto it = m_index.find(uniqueId);
    return it != m_index.end() ? it->second : nullptr;
}

bool JsonManager::AddBuilding(const BuildingRecord& record)
{
    if (record.uniqueId.empty())
        return false;

    if (FindBuilding(record.uniqueId))
        return false;

    auto ptr = std::make_unique<BuildingRecord>(record);
    BuildingRecord* rawPtr = ptr.get();

    m_index[rawPtr->uniqueId] = rawPtr;
    if (!rawPtr->resourceLocks.empty())
        m_lockedRecords.push_back(rawPtr);

    m_buildings.push_back(std::move(ptr));
    return true;
}

bool JsonManager::UpdateBuilding(const BuildingRecord& record)
{
    if (record.uniqueId.empty())
        return false;

    BuildingRecord* existing = FindBuilding(record.uniqueId);
    if (!existing)
        return false;

    *existing = record;

    bool nowLocked = !existing->resourceLocks.empty();
    bool inList = std::find(m_lockedRecords.begin(), m_lockedRecords.end(), existing) != m_lockedRecords.end();

    if (nowLocked && !inList)
        m_lockedRecords.push_back(existing);
    else if (!nowLocked && inList)
        m_lockedRecords.erase(std::remove(m_lockedRecords.begin(), m_lockedRecords.end(), existing), m_lockedRecords.end());

    return true;
}

bool JsonManager::SetResourceLock(const std::wstring& uniqueId, int32_t resourceId, float amount)
{
    BuildingRecord* record = FindBuilding(uniqueId);
    if (!record)
        return false;

    bool wasLocked = !record->resourceLocks.empty();

    for (auto& lock : record->resourceLocks)
    {
        if (lock.resourceId == resourceId)
        {
            lock.amount = amount;
            return true;
        }
    }

    record->resourceLocks.push_back({ resourceId, amount });

    if (!wasLocked)
        m_lockedRecords.push_back(record);

    return true;
}

bool JsonManager::ClearResourceLock(const std::wstring& uniqueId, int32_t resourceId)
{
    BuildingRecord* record = FindBuilding(uniqueId);
    if (!record)
        return false;

    auto it = std::remove_if(record->resourceLocks.begin(), record->resourceLocks.end(),
        [resourceId](const ResourceLock& lock) { return lock.resourceId == resourceId; });

    if (it == record->resourceLocks.end())
        return false;

    record->resourceLocks.erase(it, record->resourceLocks.end());

    if (record->resourceLocks.empty())
        m_lockedRecords.erase(std::remove(m_lockedRecords.begin(), m_lockedRecords.end(), record), m_lockedRecords.end());

    return true;
}

const std::vector<BuildingRecord*>& JsonManager::GetLockedRecords() const
{
    return m_lockedRecords;
}

const std::vector<std::unique_ptr<BuildingRecord>>& JsonManager::GetBuildings() const
{
    return m_buildings;
}

bool JsonManager::GetAlwaysOnTop() const { return m_alwaysOnTop; }
void JsonManager::SetAlwaysOnTop(bool value) { m_alwaysOnTop = value; }

bool JsonManager::GetShowOnlyOwnedTypes() const { return m_showOnlyOwnedTypes; }
void JsonManager::SetShowOnlyOwnedTypes(bool value) { m_showOnlyOwnedTypes = value; }

std::wstring JsonManager::GetFilePath() const
{
    wchar_t path[MAX_PATH] = {};
    DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);

    if (length == 0 || length == MAX_PATH)
        return L"ostriv_trainer.json";

    std::filesystem::path exePath(path);
    return (exePath.parent_path() / L"ostriv_trainer.json").wstring();
}