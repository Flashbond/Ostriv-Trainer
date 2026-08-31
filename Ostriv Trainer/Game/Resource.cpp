#include "Resource.h"

Resource::Resource()
    : m_id(-1),
    m_amount(0.0f),
    m_sourceIndex(-1)
{
}

Resource::Resource(
    int32_t id,
    const std::wstring& name,
    float amount,
    int sourceIndex)
    : m_id(id),
    m_name(name),
    m_amount(amount),
    m_sourceIndex(sourceIndex)
{
}

int32_t Resource::GetId() const
{
    return m_id;
}

const std::wstring& Resource::GetName() const
{
    return m_name;
}

float Resource::GetAmount() const
{
    return m_amount;
}

int Resource::GetSourceIndex() const
{
    return m_sourceIndex;
}

void Resource::SetAmount(float amount)
{
    m_amount = amount;
}