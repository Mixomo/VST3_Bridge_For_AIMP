#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>

// Always-on event log, one file per process in %TEMP%:
//   vst3bridge_<role>_<yyyymmdd_hhmmss>_<pid>.log
// role is "aimp" for the DSP DLL inside AIMP and "host" for the isolated host.
// Lines carry local wall-clock time with milliseconds so events from AIMP and
// the host can be lined up. Only kernel32 calls are used, so it is safe to call
// from DllMain. Never call it per audio callback: it writes to disk.
// Disable with %TEMP%\vst3bridge_nolog.flag. Old files are pruned on start.
namespace bridgelog
{
    constexpr DWORD kMaxBytes = 16u * 1024u * 1024u;
    constexpr std::size_t kKeepFiles = 40;

    struct State
    {
        SRWLOCK lock = SRWLOCK_INIT;
        HANDLE file = INVALID_HANDLE_VALUE;
        bool opened = false;
        bool disabled = false;
        DWORD written = 0;
        wchar_t path[MAX_PATH] {};
    };

    inline State& state()
    {
        static State s;
        return s;
    }

    inline bool tempDirectory(wchar_t (&temp)[MAX_PATH])
    {
        return GetTempPathW(MAX_PATH, temp) != 0;
    }

    // Keeps the newest kKeepFiles vst3bridge_* logs/CSVs; flags are never touched.
    inline void pruneOldFiles(const wchar_t* temp)
    {
        struct Item { FILETIME time; wchar_t name[MAX_PATH]; };
        std::vector<Item> items;
        wchar_t pattern[MAX_PATH] {};
        swprintf_s(pattern, L"%svst3bridge_*", temp);
        WIN32_FIND_DATAW data {};
        HANDLE find = FindFirstFileW(pattern, &data);
        if (find == INVALID_HANDLE_VALUE)
            return;
        do
        {
            const auto length = wcslen(data.cFileName);
            const bool isLog = length > 4 && (_wcsicmp(data.cFileName + length - 4, L".log") == 0
                                              || _wcsicmp(data.cFileName + length - 4, L".csv") == 0);
            if (isLog && (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
            {
                Item item {};
                item.time = data.ftLastWriteTime;
                wcscpy_s(item.name, data.cFileName);
                items.push_back(item);
            }
        } while (FindNextFileW(find, &data));
        FindClose(find);
        if (items.size() <= kKeepFiles)
            return;
        std::sort(items.begin(), items.end(), [](const Item& a, const Item& b)
        {
            return CompareFileTime(&a.time, &b.time) > 0;
        });
        for (std::size_t i = kKeepFiles; i < items.size(); ++i)
        {
            wchar_t full[MAX_PATH] {};
            swprintf_s(full, L"%s%s", temp, items[i].name);
            DeleteFileW(full);   // fails harmlessly for files still open elsewhere
        }
    }

    // File name stamp shared by this process's log and CSV.
    inline void sessionStamp(wchar_t (&out)[64])
    {
        static SYSTEMTIME start = []
        {
            SYSTEMTIME now {};
            GetLocalTime(&now);
            return now;
        }();
        swprintf_s(out, L"%04u%02u%02u_%02u%02u%02u_%lu", start.wYear, start.wMonth, start.wDay,
                   start.wHour, start.wMinute, start.wSecond, GetCurrentProcessId());
    }

    inline void writeRaw(State& s, const char* text, DWORD length)
    {
        if (s.file == INVALID_HANDLE_VALUE || s.written >= kMaxBytes)
            return;
        DWORD done = 0;
        WriteFile(s.file, text, length, &done, nullptr);
        s.written += done;
        if (s.written >= kMaxBytes)
        {
            static const char capped[] = "-- log size limit reached; further lines dropped --\r\n";
            WriteFile(s.file, capped, static_cast<DWORD>(sizeof(capped) - 1), &done, nullptr);
        }
    }

    inline void openLocked(State& s, const char* role)
    {
        s.opened = true;
        wchar_t temp[MAX_PATH] {};
        if (!tempDirectory(temp))
            return;
        wchar_t flag[MAX_PATH] {};
        swprintf_s(flag, L"%svst3bridge_nolog.flag", temp);
        if (GetFileAttributesW(flag) != INVALID_FILE_ATTRIBUTES)
        {
            s.disabled = true;
            return;
        }
        pruneOldFiles(temp);
        wchar_t stamp[64] {};
        sessionStamp(stamp);
        swprintf_s(s.path, L"%svst3bridge_%S_%s.log", temp, role, stamp);
        s.file = CreateFileW(s.path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (s.file == INVALID_HANDLE_VALUE)
            return;
        wchar_t exe[MAX_PATH] {};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        char header[1024] {};
        const int n = sprintf_s(header, "=== VST3 Bridge %s log; pid=%lu; exe=%S; %s ===\r\n", role,
                                GetCurrentProcessId(), exe, sizeof(void*) == 8 ? "x64" : "x86");
        if (n > 0)
            writeRaw(s, header, static_cast<DWORD>(n));
    }

    inline void write(const char* role, const char* message)
    {
        auto& s = state();
        AcquireSRWLockExclusive(&s.lock);
        if (!s.opened)
            openLocked(s, role);
        if (!s.disabled && s.file != INVALID_HANDLE_VALUE)
        {
            SYSTEMTIME now {};
            GetLocalTime(&now);
            char line[2048] {};
            int n = sprintf_s(line, "%02u:%02u:%02u.%03u [%5lu] ", now.wHour, now.wMinute, now.wSecond,
                              now.wMilliseconds, GetCurrentThreadId());
            if (n > 0)
            {
                const auto room = sizeof(line) - static_cast<std::size_t>(n) - 3;
                const auto length = (std::min)(std::strlen(message), room);
                std::memcpy(line + n, message, length);
                n += static_cast<int>(length);
                line[n++] = '\r';
                line[n++] = '\n';
                writeRaw(s, line, static_cast<DWORD>(n));
            }
        }
        ReleaseSRWLockExclusive(&s.lock);
    }

    inline void writef(const char* role, const char* format, ...)
    {
        char buffer[1536] {};
        va_list args;
        va_start(args, format);
        vsprintf_s(buffer, format, args);
        va_end(args);
        write(role, buffer);
    }
}
