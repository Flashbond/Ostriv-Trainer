#pragma once

#include <Windows.h>
#include <string>

class ProcessManager
{
public:
    ProcessManager();

    bool FindProcess(const std::wstring& processName);

    DWORD GetProcessId() const;
    const std::wstring& GetProcessName() const;

    bool IsFound() const;

private:
    DWORD m_processId;
    std::wstring m_processName;
};