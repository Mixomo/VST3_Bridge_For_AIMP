#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "OutProcClient.h"

#include <algorithm>
#include <cstring>
#include <sstream>

namespace
{
    HANDLE createInheritableEvent(bool manualReset)
    {
        SECURITY_ATTRIBUTES attributes { sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
        return CreateEventW(&attributes, manualReset ? TRUE : FALSE, FALSE, nullptr);
    }

    std::wstring quote(const std::wstring& value)
    {
        return L"\"" + value + L"\"";
    }
}

bool OutProcClient::start(HMODULE bridgeModule)
{
    if (hostIsAlive())
        return true;

    stop();
    submittedBlocks = 0;
    completedBlocks = 0;
    missedBlocks = 0;
    staleBlocks = 0;
    queueFullBlocks = 0;
    maxClientCallbackMicros = 0;
    clientCallbacksOver1ms = 0;
    clientCallbacksOver3ms = 0;
    emergencyBypassBlocks = 0;
    requestSequence = 0;

    SECURITY_ATTRIBUTES attributes { sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
    mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, &attributes, PAGE_READWRITE, 0,
                                 static_cast<DWORD>(vst3BridgeIpcMappingSize()), nullptr);
    requestEvent = createInheritableEvent(false);
    shutdownEvent = createInheritableEvent(true);
    showEvent = createInheritableEvent(false);

    if (mapping == nullptr || requestEvent == nullptr || shutdownEvent == nullptr || showEvent == nullptr)
    {
        closeHandles();
        return false;
    }

    header = static_cast<VST3BridgeIpcHeader*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0));
    if (header == nullptr)
    {
        closeHandles();
        return false;
    }

    std::memset(header, 0, sizeof(*header));
    header->magic = VST3_BRIDGE_IPC_MAGIC;
    header->version = VST3_BRIDGE_IPC_VERSION;
    header->slotCount = VST3_BRIDGE_IPC_SLOT_COUNT;
    header->maxAudioBytes = VST3_BRIDGE_IPC_MAX_AUDIO_BYTES;
    inputStaging.resize(VST3_BRIDGE_IPC_MAX_AUDIO_BYTES);
    LARGE_INTEGER frequency {};
    QueryPerformanceFrequency(&frequency);
    qpcFrequency = frequency.QuadPart;

    const auto hostPath = getHostPath(bridgeModule);
    std::wstringstream command;
    command << quote(hostPath)
            << L" --mapping " << reinterpret_cast<std::uintptr_t>(mapping)
            << L" --request " << reinterpret_cast<std::uintptr_t>(requestEvent)
            << L" --shutdown " << reinterpret_cast<std::uintptr_t>(shutdownEvent)
            << L" --show " << reinterpret_cast<std::uintptr_t>(showEvent);

    auto commandLine = command.str();
    STARTUPINFOW startup { sizeof(STARTUPINFOW) };
    PROCESS_INFORMATION processInfo {};
    const BOOL created = CreateProcessW(hostPath.c_str(), commandLine.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW | NORMAL_PRIORITY_CLASS,
                                        nullptr, nullptr, &startup, &processInfo);
    if (!created)
    {
        closeHandles();
        return false;
    }

    CloseHandle(processInfo.hThread);
    processHandle = processInfo.hProcess;

    jobHandle = CreateJobObjectW(nullptr, nullptr);
    if (jobHandle != nullptr)
    {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits {};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(jobHandle, JobObjectExtendedLimitInformation, &limits, sizeof(limits))
            || !AssignProcessToJobObject(jobHandle, processHandle))
        {
            CloseHandle(jobHandle);
            jobHandle = nullptr;
        }
    }

    return true;
}

void OutProcClient::stop()
{
    if (shutdownEvent != nullptr)
        SetEvent(shutdownEvent);

    if (processHandle != nullptr)
        WaitForSingleObject(processHandle, 1500);

    closeHandles();
}

bool OutProcClient::process(void* samples, int numFrames, int bitsPerSample, int numChannels, int sampleRate)
{
    LARGE_INTEGER startTicks {};
    QueryPerformanceCounter(&startTicks);

    if (processHandle == nullptr || header == nullptr || samples == nullptr || numFrames <= 0
        || numChannels <= 0 || bitsPerSample <= 0)
        return false;

    const std::uint64_t bytes = static_cast<std::uint64_t>(numFrames)
                              * static_cast<std::uint64_t>(numChannels)
                              * static_cast<std::uint64_t>(bitsPerSample / 8);
    if (bytes == 0 || bytes > VST3_BRIDGE_IPC_MAX_AUDIO_BYTES)
        return false;

    std::memcpy(inputStaging.data(), samples, static_cast<std::size_t>(bytes));
    const auto sequence = ++requestSequence;
    const auto expectedSequence = sequence > VST3_BRIDGE_IPC_PIPELINE_BLOCKS
                                ? sequence - VST3_BRIDGE_IPC_PIPELINE_BLOCKS
                                : 0;
    bool copiedResponse = false;
    int emptySlot = -1;

    for (std::uint32_t i = 0; i < VST3_BRIDGE_IPC_SLOT_COUNT; ++i)
    {
        auto& slot = header->slots[i];
        const LONG state = InterlockedCompareExchange(&slot.state, VST3_BRIDGE_SLOT_EMPTY, VST3_BRIDGE_SLOT_EMPTY);

        if (state == VST3_BRIDGE_SLOT_RESPONSE)
        {
            const bool isExpected = slot.status == 0
                                 && slot.sequence == expectedSequence
                                 && slot.audioBytes == bytes
                                 && slot.numFrames == static_cast<std::uint32_t>(numFrames)
                                 && slot.bitsPerSample == static_cast<std::uint32_t>(bitsPerSample)
                                 && slot.numChannels == static_cast<std::uint32_t>(numChannels)
                                 && slot.sampleRate == static_cast<std::uint32_t>(sampleRate);

            if (isExpected)
            {
                std::memcpy(samples, vst3BridgeIpcAudio(header, i), static_cast<std::size_t>(bytes));
                copiedResponse = true;
                ++completedBlocks;
                InterlockedExchange(&slot.state, VST3_BRIDGE_SLOT_EMPTY);
                if (emptySlot < 0)
                    emptySlot = static_cast<int>(i);
            }
            else if (slot.sequence <= expectedSequence)
            {
                ++staleBlocks;
                InterlockedExchange(&slot.state, VST3_BRIDGE_SLOT_EMPTY);
                if (emptySlot < 0)
                    emptySlot = static_cast<int>(i);
            }
        }
        else if (state == VST3_BRIDGE_SLOT_EMPTY && emptySlot < 0)
        {
            emptySlot = static_cast<int>(i);
        }
    }

    if (!copiedResponse)
    {
        if (expectedSequence > 0)
        {
            // Preserve uninterrupted playback when Windows starves the helper.
            // The current input is already in samples, so this is a dry bypass.
            ++missedBlocks;
            ++emergencyBypassBlocks;
        }
        else
            std::memset(samples, 0, static_cast<std::size_t>(bytes));
    }

    if (emptySlot >= 0)
    {
        auto& slot = header->slots[static_cast<std::uint32_t>(emptySlot)];
        std::memcpy(vst3BridgeIpcAudio(header, static_cast<std::uint32_t>(emptySlot)),
                    inputStaging.data(),
                    static_cast<std::size_t>(bytes));
        slot.status = 0;
        slot.audioBytes = static_cast<std::uint32_t>(bytes);
        slot.numFrames = static_cast<std::uint32_t>(numFrames);
        slot.bitsPerSample = static_cast<std::uint32_t>(bitsPerSample);
        slot.numChannels = static_cast<std::uint32_t>(numChannels);
        slot.sampleRate = static_cast<std::uint32_t>(sampleRate);
        slot.sequence = sequence;
        InterlockedExchange(&slot.state, VST3_BRIDGE_SLOT_REQUEST);
        SetEvent(requestEvent);
        ++submittedBlocks;
    }
    else
        ++queueFullBlocks;

    LARGE_INTEGER endTicks {};
    QueryPerformanceCounter(&endTicks);
    const auto elapsedMicros = static_cast<std::uint64_t>(
        (endTicks.QuadPart - startTicks.QuadPart) * 1000000LL / qpcFrequency);
    auto previousMax = maxClientCallbackMicros.load(std::memory_order_relaxed);
    while (elapsedMicros > previousMax
           && !maxClientCallbackMicros.compare_exchange_weak(previousMax, elapsedMicros,
                                                             std::memory_order_relaxed))
    {
    }
    if (elapsedMicros > 1000)
        clientCallbacksOver1ms.fetch_add(1, std::memory_order_relaxed);
    if (elapsedMicros > 3000)
        clientCallbacksOver3ms.fetch_add(1, std::memory_order_relaxed);

    return copiedResponse;
}

void OutProcClient::showEditor()
{
    if (hostIsAlive() && showEvent != nullptr)
        SetEvent(showEvent);
}

bool OutProcClient::isRunning() const
{
    return hostIsAlive();
}

void OutProcClient::closeHandles()
{
    if (header != nullptr)
        UnmapViewOfFile(header);
    header = nullptr;
    inputStaging.clear();

    for (auto* handle : { &mapping, &requestEvent, &shutdownEvent, &showEvent, &processHandle, &jobHandle })
    {
        if (*handle != nullptr)
            CloseHandle(*handle);
        *handle = nullptr;
    }
}

std::wstring OutProcClient::getHostPath(HMODULE bridgeModule) const
{
    wchar_t modulePath[MAX_PATH] {};
    GetModuleFileNameW(bridgeModule, modulePath, MAX_PATH);
    std::wstring path(modulePath);
    const auto slash = path.find_last_of(L"\\/");
    return (slash == std::wstring::npos ? std::wstring() : path.substr(0, slash + 1))
         + L"VST3BridgeHost.exe";
}

bool OutProcClient::hostIsAlive() const
{
    return processHandle != nullptr && WaitForSingleObject(processHandle, 0) == WAIT_TIMEOUT;
}
