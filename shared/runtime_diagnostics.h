#pragma once
#include <Windows.h>
#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include "config.h"

// Diagnostic events only. Callers supply constant scope and JSON field names;
// string values must be fixed reason codes, never names, chat or credentials.
namespace RuntimeDiagnostics {
inline SRWLOCK lock = SRWLOCK_INIT;
inline HANDLE file = INVALID_HANDLE_VALUE;
inline bool attempted = false;
inline DWORD started = 0, bytesWritten = 0;
constexpr DWORD MaxBytes = 1024 * 1024;

inline void Open() {
    attempted = true;
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (!length || length >= MAX_PATH) return;
    auto* slash = std::wcsrchr(path, L'\\');
    if (!slash) return;
    slash[1] = L'\0';
    const size_t baseLength = std::wcslen(path);
    if (baseLength + 100 >= MAX_PATH) return;
    std::swprintf(path + baseLength, MAX_PATH - baseLength, L"CoopAndreas_diagnostics");
    if (!CreateDirectoryW(path, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return;
    SYSTEMTIME now{}; GetLocalTime(&now);
    const size_t directoryLength = std::wcslen(path);
    std::swprintf(path + directoryLength, MAX_PATH - directoryLength,
        L"\\%04u%02u%02u-%02u%02u%02u-%03u-%lu.log",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
        now.wSecond, now.wMilliseconds, GetCurrentProcessId());
    file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    started = GetTickCount();
    char header[256]{};
    const int count = std::snprintf(header, sizeof header,
        "{\"elapsed_ms\":0,\"scope\":\"runtime\",\"event\":\"start\","
        "\"version\":\"%s\",\"build\":\"%s %s\"}\r\n",
        COOPANDREAS_VERSION, __DATE__, __TIME__);
    DWORD written = 0;
    if (count > 0 && count < int(sizeof header))
        WriteFile(file, header, DWORD(count), &written, nullptr);
    bytesWritten = written;
}

// format is a JSON fields fragment, for example: "\"event\":\"capture\",\"id\":%d".
inline void Write(const char* scope, const char* format, ...) {
    if (!scope || !format) return;
    size_t scopeLength = 0;
    for (; scope[scopeLength]; ++scopeLength) {
        const char c = scope[scopeLength];
        if (scopeLength >= 48 || !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return;
    }
    char fields[768]{};
    va_list args; va_start(args, format);
    const int fieldCount = std::vsnprintf(fields, sizeof fields, format, args);
    va_end(args);
    if (fieldCount <= 0 || fieldCount >= int(sizeof fields)) return;
    AcquireSRWLockExclusive(&lock);
    if (!attempted) Open();
    if (file != INVALID_HANDLE_VALUE && bytesWritten < MaxBytes) {
        char record[1024]{};
        const int count = std::snprintf(record, sizeof record,
            "{\"elapsed_ms\":%lu,\"scope\":\"%s\",%s}\r\n",
            GetTickCount() - started, scope, fields);
        if (count > 0 && count < int(sizeof record) && DWORD(count) <= MaxBytes - bytesWritten) {
            DWORD written = 0;
            WriteFile(file, record, DWORD(count), &written, nullptr);
            bytesWritten += written;
        }
    }
    ReleaseSRWLockExclusive(&lock);
}
// The OS closes the handle at process exit. No static destructor participates
// in the game's existing loader/shutdown callback ordering.
}
