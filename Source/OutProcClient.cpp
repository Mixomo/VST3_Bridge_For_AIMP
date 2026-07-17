#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "OutProcClient.h"
#include "BridgeRuntime.h"

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

    int initialHostArchitecture(const bridge::RuntimePaths& paths)
    {
        bridge::ConfigStore store(paths);
        const auto settings = store.load();
        if (settings.startupMode == "none") return static_cast<int>(bridge::currentArchitecture());
        const auto selected = settings.activePlugin;
        const auto canonical = bridge::canonicalPath(selected.resolve(paths));
        for (const auto& plugin : settings.plugins)
            if (plugin.canonicalPath.equalsIgnoreCase(canonical)
                && (plugin.architecture == bridge::Architecture::x86 || plugin.architecture == bridge::Architecture::x64))
                return static_cast<int>(plugin.architecture);
        return static_cast<int>(bridge::currentArchitecture());
    }

    bool startupNeedsSmokeTest(const bridge::RuntimePaths& paths)
    {
        const auto settings = bridge::ConfigStore(paths).load();
        if (settings.startupMode == "none") return false;
        const auto selected = settings.activePlugin;
        return selected.path.isNotEmpty() && selected.resolve(paths).exists();
    }

    bool runStartupSmokeTest(const std::wstring& hostPath, const bridge::RuntimePaths& paths)
    {
        std::wstringstream command;
        command << quote(hostPath) << L" --smoke-test-startup"
                << L" --bridge-root " << quote(std::wstring(paths.packageRoot.getFullPathName().toWideCharPointer()))
                << L" --profile " << quote(std::wstring(paths.aimpProfile.getFullPathName().toWideCharPointer()));
        auto commandLine = command.str();
        STARTUPINFOW startup { sizeof(STARTUPINFOW) };
        PROCESS_INFORMATION process {};
        if (!CreateProcessW(hostPath.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
                            CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, nullptr, nullptr, &startup, &process))
            return false;
        CloseHandle(process.hThread);
        const auto wait = WaitForSingleObject(process.hProcess, 5000);
        if (wait != WAIT_OBJECT_0)
        {
            TerminateProcess(process.hProcess, 8);
            WaitForSingleObject(process.hProcess, 1000);
        }
        DWORD exitCode = 8;
        GetExitCodeProcess(process.hProcess, &exitCode);
        CloseHandle(process.hProcess);
        return wait == WAIT_OBJECT_0 && exitCode == 0;
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
    dspStartEvent = createInheritableEvent(false);
    controlEvent = createInheritableEvent(false);

    if (mapping == nullptr || requestEvent == nullptr || shutdownEvent == nullptr || showEvent == nullptr
        || dspStartEvent == nullptr || controlEvent == nullptr)
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

    wchar_t modulePath[32768] {};
    GetModuleFileNameW(bridgeModule, modulePath, static_cast<DWORD>(std::size(modulePath)));
    const auto moduleFile = juce::File::createFileWithoutCheckingPath(juce::String(modulePath));
    const auto paths = bridge::RuntimePaths::detect(moduleFile);
    module = bridgeModule;
    stopping = false;
    const auto architecture = initialHostArchitecture(paths);
    const auto hostPath = getHostPath(module, architecture);
    const bool safeStart = startupNeedsSmokeTest(paths) && !runStartupSmokeTest(hostPath, paths);
    if (!launchHost(architecture, safeStart))
    {
        closeHandles();
        return false;
    }
    controlThread = CreateThread(nullptr, 0, controlThreadEntry, this, 0, nullptr);
    return controlThread != nullptr;
}

bool OutProcClient::launchHost(int architecture, bool safeStart)
{
    const auto hostPath = getHostPath(module, architecture);
    if (hostPath.empty() || !juce::File(juce::String(hostPath.c_str())).existsAsFile()) return false;
    wchar_t modulePath[32768] {};
    GetModuleFileNameW(module, modulePath, static_cast<DWORD>(std::size(modulePath)));
    const auto paths = bridge::RuntimePaths::detect(juce::File(juce::String(modulePath)));
    std::wstringstream command;
    command << quote(hostPath)
            << L" --mapping " << reinterpret_cast<std::uintptr_t>(mapping)
            << L" --request " << reinterpret_cast<std::uintptr_t>(requestEvent)
            << L" --shutdown " << reinterpret_cast<std::uintptr_t>(shutdownEvent)
            << L" --show " << reinterpret_cast<std::uintptr_t>(showEvent)
            << L" --dsp-start " << reinterpret_cast<std::uintptr_t>(dspStartEvent)
            << L" --control " << reinterpret_cast<std::uintptr_t>(controlEvent);
    if (safeStart) command << L" --safe-start";
    command << L" --bridge-root " << quote(std::wstring(paths.packageRoot.getFullPathName().toWideCharPointer()))
            << L" --profile " << quote(std::wstring(paths.aimpProfile.getFullPathName().toWideCharPointer()));

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

    header->hostArchitecture = architecture;
    return true;
}

void OutProcClient::stop()
{
    stopping = true;
    if (controlEvent != nullptr) SetEvent(controlEvent);
    stopHost();
    if (controlThread != nullptr) { WaitForSingleObject(controlThread, 1500); CloseHandle(controlThread); controlThread = nullptr; }
    closeHandles();
}

void OutProcClient::stopHost()
{
    if (shutdownEvent != nullptr) SetEvent(shutdownEvent);
    if (processHandle != nullptr) { WaitForSingleObject(processHandle, 1500); CloseHandle(processHandle); processHandle = nullptr; }
    if (jobHandle != nullptr) { CloseHandle(jobHandle); jobHandle = nullptr; }
}

DWORD WINAPI OutProcClient::controlThreadEntry(void* context)
{
    static_cast<OutProcClient*>(context)->controlLoop();
    return 0;
}

void OutProcClient::controlLoop()
{
    while (!stopping)
    {
        if (WaitForSingleObject(controlEvent, INFINITE) != WAIT_OBJECT_0 || stopping) break;
        const auto requested = InterlockedExchange(&header->requestedArchitecture, 0);
        if (requested != static_cast<long>(bridge::Architecture::x86)
            && requested != static_cast<long>(bridge::Architecture::x64)) continue;
        stopHost();
        ResetEvent(shutdownEvent);
        for (auto& slot : header->slots) InterlockedExchange(&slot.state, VST3_BRIDGE_SLOT_EMPTY);
        requestSequence = 0;
        launchHost(static_cast<int>(requested));
    }
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

bool OutProcClient::showEditor()
{
    if (!hostIsAlive() || showEvent == nullptr || header == nullptr) return false;
    InterlockedIncrement(&header->showGuiSequence);
    return SetEvent(showEvent) != FALSE;
}

void OutProcClient::notifyDspStarted()
{
    if (hostIsAlive() && dspStartEvent != nullptr)
        SetEvent(dspStartEvent);
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

    for (auto* handle : { &mapping, &requestEvent, &shutdownEvent, &showEvent, &dspStartEvent, &controlEvent, &processHandle, &jobHandle })
    {
        if (*handle != nullptr)
            CloseHandle(*handle);
        *handle = nullptr;
    }
}

std::wstring OutProcClient::getHostPath(HMODULE bridgeModule, int architecture) const
{
    wchar_t modulePath[MAX_PATH] {};
    GetModuleFileNameW(bridgeModule, modulePath, MAX_PATH);
    const auto paths = bridge::RuntimePaths::detect(juce::File(juce::String(modulePath)));
    return std::wstring(paths.helper(static_cast<bridge::Architecture>(architecture)).getFullPathName().toWideCharPointer());
}

bool OutProcClient::hostIsAlive() const
{
    return processHandle != nullptr && WaitForSingleObject(processHandle, 0) == WAIT_TIMEOUT;
}
