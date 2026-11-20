#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "OutProcClient.h"
#include "BridgeRuntime.h"
#include "BridgeLog.h"

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

    // Returns the plug-in a start-up smoke test must cover, or an empty string.
    juce::String startupSmokeTestTarget(const bridge::RuntimePaths& paths)
    {
        const auto settings = bridge::ConfigStore(paths).load();
        if (settings.startupMode == "none") return {};
        const auto selected = settings.activePlugin;
        if (selected.path.isEmpty()) return {};
        const auto file = selected.resolve(paths);
        return file.exists() ? file.getFullPathName() : juce::String();
    }

    // The smoke test launches a complete host and waits for it to exit
    // (~1.5-2 s). It only needs to pass once per AIMP process for a given
    // plug-in; repeating it on every DSP restart blocked AIMP each time.
    juce::String& smokeTestPassedFor()
    {
        static juce::String passed;
        return passed;
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
        const auto smokeStart = GetTickCount64();
        bridgelog::writef("aimp", "Smoke test started (pid %lu)", process.dwProcessId);
        const auto wait = WaitForSingleObject(process.hProcess, 5000);
        if (wait != WAIT_OBJECT_0)
        {
            // Still running after 5 s: the plug-in is slow to load (some take
            // several seconds), not crashing. Treating that as a failure forced
            // every slow plug-in into safe-start bypass, so it never processed
            // audio in AIMP. Only a crash or an error exit selects safe start.
            TerminateProcess(process.hProcess, 8);
            WaitForSingleObject(process.hProcess, 1000);
            CloseHandle(process.hProcess);
            bridgelog::write("aimp", "Smoke test still running after 5000 ms: slow plug-in load, treated as passed");
            return true;
        }
        DWORD exitCode = 8;
        GetExitCodeProcess(process.hProcess, &exitCode);
        CloseHandle(process.hProcess);
        bridgelog::writef("aimp", "Smoke test finished in %llu ms, exit code 0x%08lX -> %s",
                          GetTickCount64() - smokeStart, exitCode, exitCode == 0 ? "passed" : "FAILED (safe start)");
        return exitCode == 0;
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
        bridgelog::write("aimp", "Start failed: audio client allocation");
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

    SECURITY_ATTRIBUTES attributes { sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
    mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, &attributes, PAGE_READWRITE, 0,
                                 static_cast<DWORD>(vst3BridgeIpcMappingSize()), nullptr);
    requestEvent = createInheritableEvent(false);
    responseEvent = createInheritableEvent(false);
    shutdownEvent = createInheritableEvent(true);
    showEvent = createInheritableEvent(false);
    dspStartEvent = createInheritableEvent(false);
    controlEvent = createInheritableEvent(false);

    if (mapping == nullptr || requestEvent == nullptr || responseEvent == nullptr || shutdownEvent == nullptr || showEvent == nullptr
        || dspStartEvent == nullptr || controlEvent == nullptr)
    {
        bridgelog::writef("aimp", "Start failed: IPC objects (error %lu)", GetLastError());
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
    const auto smokeTarget = startupSmokeTestTarget(paths) + "|" + juce::String(hostPath.c_str());
    bool safeStart = false;
    if (smokeTarget != "|" + juce::String(hostPath.c_str()) && smokeTarget != smokeTestPassedFor())
    {
        safeStart = !runStartupSmokeTest(hostPath, paths);
        smokeTestPassedFor() = safeStart ? juce::String() : smokeTarget;
    }
    else
        bridgelog::write("aimp", "Smoke test skipped (no plug-in selected, or already passed in this session)");
    if (!launchHost(architecture, safeStart))
    {
        bridgelog::write("aimp", "Start failed: host launch");
        closeHandles();
        return false;
    }
    diagLog.start();
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
            << L" --response " << reinterpret_cast<std::uintptr_t>(responseEvent)
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
        bridgelog::writef("aimp", "CreateProcess failed (error %lu): %S", GetLastError(), hostPath.c_str());
        return false;
    }

    CloseHandle(processInfo.hThread);
    processHandle = processInfo.hProcess;
    bridgelog::writef("aimp", "Host launched: pid %lu, %s, %s: %S", processInfo.dwProcessId,
                      architecture == static_cast<int>(bridge::Architecture::x86) ? "x86" : "x64",
                      safeStart ? "SAFE START (plug-in bypassed)" : "normal start", hostPath.c_str());

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
    diagLog.stop();
    stopping = true;
    if (controlEvent != nullptr) SetEvent(controlEvent);
    stopHost();
    if (controlThread != nullptr) { WaitForSingleObject(controlThread, 1500); CloseHandle(controlThread); controlThread = nullptr; }
    closeHandles();
}

void OutProcClient::stopHost()
{
    if (shutdownEvent != nullptr) SetEvent(shutdownEvent);
    if (processHandle != nullptr)
    {
        const auto stopStart = GetTickCount64();
        DWORD exitCode = STILL_ACTIVE;
        GetExitCodeProcess(processHandle, &exitCode);
        if (exitCode != STILL_ACTIVE)
            bridgelog::writef("aimp", "Host had already exited (exit code 0x%08lX)", exitCode);
        if (WaitForSingleObject(processHandle, 1500) != WAIT_OBJECT_0)
        {
            TerminateProcess(processHandle, 9);
            WaitForSingleObject(processHandle, 1000);
            bridgelog::write("aimp", "Host did not exit within 1500 ms; terminated");
        }
        else if (exitCode == STILL_ACTIVE)
        {
            GetExitCodeProcess(processHandle, &exitCode);
            bridgelog::writef("aimp", "Host exited in %llu ms (exit code 0x%08lX)", GetTickCount64() - stopStart, exitCode);
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
        bridgelog::writef("aimp", "Host restart requested: %s", safeRestart ? "safe restart (Reload Bridge)" : "architecture switch");
        stopHost();
        ResetEvent(shutdownEvent);
        header->hostAudioReady.store(0, std::memory_order_release);
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

    std::int32_t firstRaw = 0;
    // Frames whose every channel is exactly zero (16-bit only; diagnostics).
    auto countZeroFrames = [&]() -> std::uint32_t
    {
        if (bitsPerSample != 16)
            return 0;
        const auto* pcm16 = static_cast<const std::int16_t*>(samples);
        std::uint32_t zeros = 0;
        for (int f = 0; f < numFrames; ++f)
        {
            bool zero = true;
            for (int c = 0; c < numChannels && zero; ++c)
                zero = pcm16[static_cast<std::size_t>(f) * numChannels + c] == 0;
            zeros += zero ? 1u : 0u;
        }
        return zeros;
    };
    std::uint32_t zeroIn = 0;
    if (diagLog.isEnabled())
    {
        std::memcpy(&firstRaw, samples, static_cast<std::size_t>((std::min)(4, (std::max)(1, bitsPerSample / 8))));
        zeroIn = countZeroFrames();
    }

    const auto processed = audioClient.process(samples, numFrames, bitsPerSample, numChannels,
                                               sampleRate, header, requestSequence,
                                               requestEvent, responseEvent);

    const auto& stats = audioClient.getStats();
    submittedBlocks.store(stats.submitted, std::memory_order_relaxed);
    completedBlocks.store(stats.completed, std::memory_order_relaxed);
    missedBlocks.store(stats.timedOut, std::memory_order_relaxed);
    staleBlocks.store(stats.rejected, std::memory_order_relaxed);
    queueFullBlocks.store(stats.hostBusy, std::memory_order_relaxed);
    emergencyBypassBlocks.store(stats.dryFrames, std::memory_order_relaxed);

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

    if (diagLog.isEnabled())
    {
        const auto& call = audioClient.getLastCall();
        DiagEntry entry {};
        entry.qpc = startTicks.QuadPart;
        entry.threadId = GetCurrentThreadId();
        entry.threadPriority = GetThreadPriority(GetCurrentThread());
        entry.frames = static_cast<std::uint32_t>(numFrames);
        entry.sampleRate = static_cast<std::uint32_t>(sampleRate);
        entry.bitsPerSample = static_cast<std::uint16_t>(bitsPerSample);
        entry.channels = static_cast<std::uint16_t>(numChannels);
        entry.blocks = call.blocks;
        entry.wetBlocks = call.wetBlocks;
        entry.callMicros = static_cast<std::uint32_t>(elapsedMicros);
        entry.maxWaitMicros = static_cast<std::uint32_t>(call.maxWaitTicks * 1000000 / qpcFrequency);
        entry.maxHostMicros = call.maxHostMicros;
        entry.hostThreadInfo = header->hostThreadInfo.load(std::memory_order_relaxed);
        entry.firstRawSample = firstRaw;
        entry.dryFrames = call.dryFrames;
        entry.resets = static_cast<std::uint32_t>(audioClient.getStats().resets);
        entry.zeroOutFrames = countZeroFrames();
        entry.zeroInFrames = zeroIn;
        entry.timedOut = static_cast<std::uint32_t>(stats.timedOut);
        entry.hostBusy = static_cast<std::uint32_t>(stats.hostBusy);
        diagLog.push(entry);
    }

    return processed;
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
    for (auto* handle : { &mapping, &requestEvent, &responseEvent, &shutdownEvent, &showEvent, &dspStartEvent,
                          &controlEvent, &processHandle, &jobHandle })
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
