#include "ProcessManager.h"
#include <TlHelp32.h>

ProcessManager::ProcessManager() : m_processId(0) {}

bool ProcessManager::FindProcess(const std::wstring& processName) {
    m_processId = 0;
    m_processName = processName;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W processEntry{};
    processEntry.dwSize = sizeof(processEntry);

    if (!Process32FirstW(snapshot, &processEntry)) {
        CloseHandle(snapshot);
        return false;
    }

    do {
        if (_wcsicmp(processEntry.szExeFile, processName.c_str()) == 0) {
            m_processId = processEntry.th32ProcessID;
            CloseHandle(snapshot);
            return true;
        }
    } while (Process32NextW(snapshot, &processEntry));

    CloseHandle(snapshot);
    return false;
}

DWORD ProcessManager::GetProcessId() const { return m_processId; }
const std::wstring& ProcessManager::GetProcessName() const { return m_processName; }
bool ProcessManager::IsFound() const { return m_processId != 0; }