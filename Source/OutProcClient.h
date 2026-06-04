#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "OutProcProtocol.h"

class OutProcClient
{
public:
    bool start(HMODULE bridgeModule);
    void stop();
    bool process(void* samples, int numFrames, int bitsPerSample, int numChannels, int sampleRate);
    void showEditor();
    bool isRunning() const;
    std::uint64_t getSubmittedBlocks() const { return submittedBlocks.load(); }
    std::uint64_t getCompletedBlocks() const { return completedBlocks.load(); }
    std::uint64_t getMissedBlocks() const { return missedBlocks.load(); }
    std::uint64_t getStaleBlocks() const { return staleBlocks.load(); }
    std::uint64_t getQueueFullBlocks() const { return queueFullBlocks.load(); }
    std::uint64_t getMaxClientCallbackMicros() const { return maxClientCallbackMicros.load(); }
    std::uint64_t getClientCallbacksOver1ms() const { return clientCallbacksOver1ms.load(); }
    std::uint64_t getClientCallbacksOver3ms() const { return clientCallbacksOver3ms.load(); }
    std::uint64_t getEmergencyBypassBlocks() const { return emergencyBypassBlocks.load(); }

private:
    void closeHandles();
    std::wstring getHostPath(HMODULE bridgeModule) const;
    bool hostIsAlive() const;

    HANDLE mapping = nullptr;
    HANDLE requestEvent = nullptr;
    HANDLE shutdownEvent = nullptr;
    HANDLE showEvent = nullptr;
    HANDLE processHandle = nullptr;
    HANDLE jobHandle = nullptr;
    VST3BridgeIpcHeader* header = nullptr;
    std::uint64_t requestSequence = 0;
    std::int64_t qpcFrequency = 0;
    std::vector<unsigned char> inputStaging;
    std::atomic<std::uint64_t> submittedBlocks { 0 };
    std::atomic<std::uint64_t> completedBlocks { 0 };
    std::atomic<std::uint64_t> missedBlocks { 0 };
    std::atomic<std::uint64_t> staleBlocks { 0 };
    std::atomic<std::uint64_t> queueFullBlocks { 0 };
    std::atomic<std::uint64_t> maxClientCallbackMicros { 0 };
    std::atomic<std::uint64_t> clientCallbacksOver1ms { 0 };
    std::atomic<std::uint64_t> clientCallbacksOver3ms { 0 };
    std::atomic<std::uint64_t> emergencyBypassBlocks { 0 };
};
