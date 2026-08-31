#pragma once

#include <cstdint>
#include <string>

struct BuildingListItem
{
    uintptr_t address = 0;

    std::wstring displayName;
    std::wstring dictionaryName;
    std::wstring persistentId;

    uint32_t type = 0;

    bool active = false;
    bool hasResource = false;
};

struct ResourceListItem
{
    int32_t id = -1;

    std::wstring name;

    float amount = 0.0f;
};