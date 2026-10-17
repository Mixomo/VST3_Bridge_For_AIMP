#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "OutProcClient.h"
#include "BridgeRuntime.h"

#include <cstring>
#include <exception>
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
    try
    {
        if (!audioClient.prepare())
            return false;
    }
    catch (const std::exception&)
    {
        return false;
    }
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
    lastUnderruns = 0;
    lastOverruns = 0;
    lastStaleBlocks = 0;
    lastProcessedBlocks = 0;

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
    new (header) VST3BridgeIpcHeader();
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
    if (controlThread == nullptr)
    {
        stop();
        return false;
    }
    return true;
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
    if (controlThread != nullptr)
    {
        WaitForSingleObject(controlThread, INFINITE);
        CloseHandle(controlThread);
        controlThread = nullptr;
    }
    stopHost();
    closeHandles();
}

void OutProcClient::stopHost()
{
    if (shutdownEvent != nullptr) SetEvent(shutdownEvent);
    if (processHandle != nullptr)
    {
        if (WaitForSingleObject(processHandle, 1500) != WAIT_OBJECT_0)
        {
            TerminateProcess(processHandle, 9);
            WaitForSingleObject(processHandle, 1000);
        }
        CloseHandle(processHandle);
        processHandle = nullptr;
    }
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
        const auto safeRestart = header->safeRestartRequested.exchange(0, std::memory_order_acq_rel) != 0;
        if (!safeRestart && requested != static_cast<long>(bridge::Architecture::x86)
            && requested != static_cast<long>(bridge::Architecture::x64))
            continue;
        const auto architecture = safeRestart
            ? static_cast<int>(header->hostArchitecture)
            : static_cast<int>(requested);
        stopHost();
        ResetEvent(shutdownEvent);
        header->sessionEpoch.fetch_add(1, std::memory_order_acq_rel);
        for (auto& slot : header->slots)
        {
            auto processing = VST3_BRIDGE_SLOT_PROCESSING;
            slot.state.compare_exchange_strong(processing, VST3_BRIDGE_SLOT_EMPTY,
                                               std::memory_order_acq_rel);
        }
        if (!launchHost(architecture, safeRestart))
            continue;
        if (safeRestart)
        {
            InterlockedIncrement(&header->showGuiSequence);
            SetEvent(showEvent);
        }
    }
}

bool OutProcClient::process(void* samples, int numFrames, int bitsPerSample, int numChannels, int sampleRate)
{
    LARGE_INTEGER startTicks {};
    QueryPerformanceCounter(&startTicks);

    if (samples == nullptr || numFrames <= 0 || numChannels <= 0 || sampleRate <= 0)
        return false;

    if (header == nullptr)
        return false;

    const auto previousSequence = requestSequence;
    const auto submitted = audioClient.process(samples, numFrames, bitsPerSample, numChannels,
                                               sampleRate, header, requestSequence);
    const auto submittedCount = requestSequence - previousSequence;
    if (submitted)
    {
        SetEvent(requestEvent);
        submittedBlocks.fetch_add(submittedCount, std::memory_order_relaxed);
    }

    const auto underruns = header->underruns.load(std::memory_order_relaxed);
    const auto overruns = header->overruns.load(std::memory_order_relaxed);
    const auto stale = header->staleBlocks.load(std::memory_order_relaxed);
    const auto completed = header->processedBlocks.load(std::memory_order_relaxed);
    missedBlocks.fetch_add(underruns - lastUnderruns, std::memory_order_relaxed);
    emergencyBypassBlocks.fetch_add(underruns - lastUnderruns, std::memory_order_relaxed);
    queueFullBlocks.fetch_add(overruns - lastOverruns, std::memory_order_relaxed);
    staleBlocks.fetch_add(stale - lastStaleBlocks, std::memory_order_relaxed);
    completedBlocks.fetch_add(completed - lastProcessedBlocks, std::memory_order_relaxed);
    lastUnderruns = underruns;
    lastOverruns = overruns;
    lastStaleBlocks = stale;
    lastProcessedBlocks = completed;

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

    return submitted;
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
