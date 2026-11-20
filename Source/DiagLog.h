#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <share.h>
#include <memory>
#include <algorithm>

#include "BridgeLog.h"

// Per-callback transport diagnostics for the AIMP DSP DLL (always on).
//
// The audio thread only fills a preallocated lock-free ring; a background
// thread writes it to %TEMP%\vst3bridge_audio_<stamp>_<pid>.csv, so logging
// never blocks AIMP. Capped at kMaxRows rows per session. Disable with
// %TEMP%\vst3bridge_nodiag.flag.
struct DiagEntry
{
    std::int64_t qpc;
    std::uint32_t threadId;
    std::int32_t threadPriority;
    std::uint32_t frames;
    std::uint32_t sampleRate;
    std::uint16_t bitsPerSample;
    std::uint16_t channels;
    std::uint16_t blocks;
    std::uint16_t wetBlocks;
    std::uint32_t callMicros;
    std::uint32_t maxWaitMicros;
    std::uint32_t maxHostMicros;
    std::uint32_t hostThreadInfo;
    std::int32_t firstRawSample;
    std::uint32_t dryFrames;
    std::uint32_t resets;
    std::uint32_t zeroOutFrames;
    std::uint32_t zeroInFrames;
    std::uint32_t timedOut;
    std::uint32_t hostBusy;
};

class DiagLog
{
public:
    ~DiagLog() { stop(); }

    void start()
    {
        stop();
        wchar_t temp[MAX_PATH] {};
        if (GetTempPathW(MAX_PATH, temp) == 0)
            return;
        wchar_t flag[MAX_PATH] {};
        swprintf_s(flag, L"%svst3bridge_nodiag.flag", temp);
        if (GetFileAttributesW(flag) != INVALID_FILE_ATTRIBUTES)
            return;

        wchar_t stamp[64] {};
        bridgelog::sessionStamp(stamp);
        swprintf_s(path, L"%svst3bridge_rack-audio_%s_%llu.csv", temp, stamp,
                   static_cast<unsigned long long>(GetTickCount64() % 100000));
        entries = std::make_unique<DiagEntry[]>(capacity);
        head.store(0);
        tail.store(0);
        dropped.store(0);
        LARGE_INTEGER frequency {};
        QueryPerformanceFrequency(&frequency);
        qpcFrequency = frequency.QuadPart;
        stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        writer = CreateThread(nullptr, 0, writerEntry, this, 0, nullptr);
        enabled.store(writer != nullptr, std::memory_order_release);
        if (writer != nullptr)
            bridgelog::writef("rack-aimp", "Audio CSV: %S", path);
    }

    void stop()
    {
        enabled.store(false, std::memory_order_release);
        if (writer != nullptr)
        {
            SetEvent(stopEvent);
            WaitForSingleObject(writer, 5000);
            CloseHandle(writer);
            writer = nullptr;
        }
        if (stopEvent != nullptr)
        {
            CloseHandle(stopEvent);
            stopEvent = nullptr;
        }
    }

    bool isEnabled() const { return enabled.load(std::memory_order_acquire); }
    std::int64_t frequency() const { return qpcFrequency; }

    void push(const DiagEntry& entry) noexcept
    {
        if (!isEnabled())
            return;
        const auto h = head.load(std::memory_order_relaxed);
        if (h - tail.load(std::memory_order_acquire) >= capacity)
        {
            dropped.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        entries[h % capacity] = entry;
        head.store(h + 1, std::memory_order_release);
    }

private:
    static constexpr std::uint64_t capacity = 1u << 16;
    static constexpr std::uint64_t kMaxRows = 600000;   // ~60 MB, hours of playback

    static DWORD WINAPI writerEntry(void* context)
    {
        static_cast<DiagLog*>(context)->writerLoop();
        return 0;
    }

    void writerLoop()
    {
        FILE* file = nullptr;
        // Shared read/write so the log can be inspected while AIMP is playing.
        file = _wfsopen(path, L"w", _SH_DENYNO);
        if (file == nullptr)
            return;
        std::fprintf(file, "# qpcFrequency=%lld\n", static_cast<long long>(qpcFrequency));
        std::fprintf(file, "timeMs,threadId,threadPriority,frames,sampleRate,bps,channels,blocks,wetBlocks,"
                           "callUs,maxWaitUs,maxHostUs,hostThreadInfo,firstRawSample,dryFrames,resets,zeroOutFrames,zeroInFrames,timedOut,hostBusy\n");
        std::int64_t origin = 0;
        std::uint64_t rows = 0;
        // 10-second summaries for the event log.
        auto summaryStart = GetTickCount64();
        std::uint64_t sumCalls = 0, sumFrames = 0, sumDry = 0, sumFullyWet = 0;
        std::uint32_t sumMaxCall = 0, sumMaxWait = 0, lastTimedOut = 0, lastBusy = 0, lastResets = 0;
        std::uint32_t sampleRate = 0;
        int prioMin = 99, prioMax = -99;
        for (;;)
        {
            const bool stopping = WaitForSingleObject(stopEvent, 250) == WAIT_OBJECT_0;
            auto t = tail.load(std::memory_order_relaxed);
            const auto h = head.load(std::memory_order_acquire);
            for (; t < h; ++t)
            {
                const auto& e = entries[t % capacity];
                ++sumCalls;
                sumFrames += e.frames;
                sumDry += e.dryFrames;
                sumFullyWet += e.wetBlocks;
                sumMaxCall = (std::max)(sumMaxCall, e.callMicros);
                sumMaxWait = (std::max)(sumMaxWait, e.maxWaitMicros);
                sampleRate = e.sampleRate;
                prioMin = (std::min)(prioMin, static_cast<int>(e.threadPriority));
                prioMax = (std::max)(prioMax, static_cast<int>(e.threadPriority));
                if (rows == kMaxRows)
                    std::fprintf(file, "# row limit reached; further callbacks not logged\n");
                if (rows++ >= kMaxRows)
                    continue;
                if (origin == 0)
                {
                    origin = e.qpc;
                    std::fprintf(file, "# originQpc=%lld\n", static_cast<long long>(origin));
                }
                std::fprintf(file, "%.3f,%lu,%d,%lu,%lu,%u,%u,%u,%u,%lu,%lu,%lu,%lu,%ld,%lu,%lu,%lu,%lu,%lu,%lu\n",
                             (e.qpc - origin) * 1000.0 / qpcFrequency,
                             static_cast<unsigned long>(e.threadId), e.threadPriority,
                             static_cast<unsigned long>(e.frames), static_cast<unsigned long>(e.sampleRate),
                             e.bitsPerSample, e.channels, e.blocks, e.wetBlocks,
                             static_cast<unsigned long>(e.callMicros), static_cast<unsigned long>(e.maxWaitMicros),
                             static_cast<unsigned long>(e.maxHostMicros), static_cast<unsigned long>(e.hostThreadInfo),
                             static_cast<long>(e.firstRawSample),
                             static_cast<unsigned long>(e.dryFrames), static_cast<unsigned long>(e.resets),
                             static_cast<unsigned long>(e.zeroOutFrames), static_cast<unsigned long>(e.zeroInFrames),
                             static_cast<unsigned long>(e.timedOut), static_cast<unsigned long>(e.hostBusy));
            }
            tail.store(t, std::memory_order_release);
            std::fflush(file);
            if (GetTickCount64() - summaryStart >= 10000 || (stopping && sumCalls > 0))
            {
                if (sumCalls > 0)
                {
                    const auto& last = entries[(t + capacity - 1) % capacity];
                    bridgelog::writef("rack-aimp",
                        "Audio 10s: calls=%llu audio=%.1fs @%u Hz, fully processed calls=%llu, dry frames=%llu, "
                        "max call=%u us, max wait=%u us, caller priority %d..%d, host timeouts +%u, host busy +%u, pipeline resets +%u",
                        sumCalls, sampleRate ? static_cast<double>(sumFrames) / sampleRate : 0.0, sampleRate,
                        sumFullyWet, sumDry, sumMaxCall, sumMaxWait,
                        prioMin, prioMax,
                        last.timedOut - lastTimedOut, last.hostBusy - lastBusy, last.resets - lastResets);
                    lastTimedOut = last.timedOut;
                    lastBusy = last.hostBusy;
                    lastResets = last.resets;
                }
                else
                    bridgelog::write("rack-aimp", "Audio 10s: no DSP calls from AIMP");
                summaryStart = GetTickCount64();
                sumCalls = sumFrames = sumDry = sumFullyWet = 0;
                sumMaxCall = sumMaxWait = 0;
                prioMin = 99;
                prioMax = -99;
            }
            if (stopping)
                break;
        }
        std::fprintf(file, "# dropped=%llu\n", static_cast<unsigned long long>(dropped.load()));
        std::fclose(file);
    }

    std::atomic<bool> enabled { false };
    std::unique_ptr<DiagEntry[]> entries;
    std::atomic<std::uint64_t> head { 0 };
    std::atomic<std::uint64_t> tail { 0 };
    std::atomic<std::uint64_t> dropped { 0 };
    std::int64_t qpcFrequency = 1;
    HANDLE writer = nullptr;
    HANDLE stopEvent = nullptr;
    wchar_t path[MAX_PATH] {};
};
