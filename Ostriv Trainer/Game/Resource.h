#pragma once

#include <cstdint>
#include <string>

class Resource
{
public:
    Resource();

    Resource(
        int32_t id,
        const std::wstring& name,
        float amount,
        int sourceIndex
    );

    int32_t GetId() const;

    const std::wstring& GetName() const;

    float GetAmount() const;

    int GetSourceIndex() const;

    void SetAmount(float amount);

private:
    int32_t m_id;
    std::wstring m_name;
    float m_amount;

    // Physical inventory slot containing this resource.
    int m_sourceIndex;
};