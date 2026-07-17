#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include "HostWindow.h"
#include "BridgeRuntime.h"
#include "OutProcProtocol.h"
#include "VST3HostEngine.h"

#include <atomic>
#include <cstdint>
#include <memory>

namespace
{
    HANDLE parseHandle(int argc, wchar_t** argv, const wchar_t* name)
    {
        for (int i = 1; i + 1 < argc; ++i)
            if (wcscmp(argv[i], name) == 0)
                return reinterpret_cast<HANDLE>(_wcstoui64(argv[i + 1], nullptr, 10));
        return nullptr;
    }

    juce::String parseString(int argc, wchar_t** argv, const wchar_t* name)
    {
        for (int i = 1; i + 1 < argc; ++i)
            if (wcscmp(argv[i], name) == 0) return juce::String(argv[i + 1]);
        return {};
    }

    bool hasFlag(int argc, wchar_t** argv, const wchar_t* name)
    {
        for (int i = 1; i < argc; ++i)
            if (wcscmp(argv[i], name) == 0) return true;
        return false;
    }

    bool pumpUntilEditorInitialised(HostWindow& window, DWORD timeoutMs)
    {
        const auto deadline = GetTickCount64() + timeoutMs;
        while (!window.isEditorInitialised() && GetTickCount64() < deadline)
        {
            MSG message {};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            Sleep(5);
        }
        return window.isEditorInitialised();
    }

    struct AudioContext
    {
        VST3BridgeIpcHeader* header = nullptr;
        HANDLE requestEvent = nullptr;
        HANDLE shutdownEvent = nullptr;
    };

    DWORD WINAPI audioThreadMain(void* parameter)
    {
        auto* context = static_cast<AudioContext*>(parameter);
        HANDLE waits[] = { context->shutdownEvent, context->requestEvent };

        while (true)
        {
            const DWORD result = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
            if (result == WAIT_OBJECT_0)
                break;
            if (result != WAIT_OBJECT_0 + 1)
                break;

            auto* header = context->header;
            if (header == nullptr || header->magic != VST3_BRIDGE_IPC_MAGIC
                || header->version != VST3_BRIDGE_IPC_VERSION
                || header->slotCount != VST3_BRIDGE_IPC_SLOT_COUNT
                || header->maxAudioBytes != VST3_BRIDGE_IPC_MAX_AUDIO_BYTES)
            {
                continue;
            }

            while (true)
            {
                int requestSlot = -1;
                std::uint64_t requestSequence = UINT64_MAX;
                for (std::uint32_t i = 0; i < VST3_BRIDGE_IPC_SLOT_COUNT; ++i)
                {
                    auto& candidate = header->slots[i];
                    if (InterlockedCompareExchange(&candidate.state, VST3_BRIDGE_SLOT_REQUEST, VST3_BRIDGE_SLOT_REQUEST)
                            == VST3_BRIDGE_SLOT_REQUEST
                        && candidate.sequence < requestSequence)
                    {
                        requestSlot = static_cast<int>(i);
                        requestSequence = candidate.sequence;
                    }
                }

                if (requestSlot < 0)
                    break;

                auto& slot = header->slots[static_cast<std::uint32_t>(requestSlot)];
                if (slot.audioBytes == 0 || slot.audioBytes > VST3_BRIDGE_IPC_MAX_AUDIO_BYTES)
                {
                    slot.status = 1;
                }
                else
                {
                    try
                    {
                        VST3HostEngine::getInstance().processAudio(
                            vst3BridgeIpcAudio(header, static_cast<std::uint32_t>(requestSlot)),
                            static_cast<int>(slot.numFrames),
                            static_cast<int>(slot.bitsPerSample),
                            static_cast<int>(slot.numChannels),
                            static_cast<int>(slot.sampleRate));
                        slot.status = 0;
                    }
                    catch (...)
                    {
                        slot.status = 1;
                    }
                }

                InterlockedExchange(&slot.state, VST3_BRIDGE_SLOT_RESPONSE);
            }
        }

        return 0;
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    int argc = 0;
    auto** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr)
        return 1;

    HANDLE mapping = parseHandle(argc, argv, L"--mapping");
    HANDLE requestEvent = parseHandle(argc, argv, L"--request");
    HANDLE shutdownEvent = parseHandle(argc, argv, L"--shutdown");
    HANDLE showEvent = parseHandle(argc, argv, L"--show");
    HANDLE dspStartEvent = parseHandle(argc, argv, L"--dsp-start");
    HANDLE controlEvent = parseHandle(argc, argv, L"--control");
    const juce::File packageRoot(parseString(argc, argv, L"--bridge-root"));
    const juce::File profile(parseString(argc, argv, L"--profile"));
    const juce::File selfTestPlugin(parseString(argc, argv, L"--self-test-plugin"));
    const bool smokeTestStartup = hasFlag(argc, argv, L"--smoke-test-startup");
    const bool safeStart = hasFlag(argc, argv, L"--safe-start");
    LocalFree(argv);

    if (smokeTestStartup)
    {
        juce::initialiseJuce_GUI();
        const auto paths = bridge::RuntimePaths::detect(juce::File::getSpecialLocation(juce::File::currentExecutableFile),
                                                         packageRoot, profile);
        VST3HostEngine::createInstance();
        VST3HostEngine::getInstance().init(instance, paths);
        bool ready = false;
        {
            VST3HostEngine::getInstance().writeLog("Startup smoke creating bridge window");
            HostWindow window(true);
            VST3HostEngine::getInstance().writeLog("Startup smoke bridge window created; pumping editor messages");
            ready = pumpUntilEditorInitialised(window, 3000);
            window.finishPreload(false);
        }
        VST3HostEngine::destroyInstance();
        juce::shutdownJuce_GUI();
        return ready ? 0 : 7;
    }

    if (selfTestPlugin != juce::File())
    {
        juce::initialiseJuce_GUI();
        const auto paths = bridge::RuntimePaths::detect(juce::File::getSpecialLocation(juce::File::currentExecutableFile));
        VST3HostEngine::createInstance();
        VST3HostEngine::getInstance().init(instance, paths);
        const auto loaded = VST3HostEngine::getInstance().loadPlugin(selfTestPlugin.getFullPathName());
        VST3HostEngine::destroyInstance();
        juce::shutdownJuce_GUI();
        return loaded ? 0 : 6;
    }

    if (mapping == nullptr || requestEvent == nullptr || shutdownEvent == nullptr || showEvent == nullptr
        || dspStartEvent == nullptr || controlEvent == nullptr)
        return 2;

    auto* header = static_cast<VST3BridgeIpcHeader*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0));
    if (header == nullptr)
        return 3;

    juce::initialiseJuce_GUI();
    const auto paths = bridge::RuntimePaths::detect(juce::File::getSpecialLocation(juce::File::currentExecutableFile),
                                                     packageRoot, profile);
    VST3HostEngine::createInstance();
    VST3HostEngine::getInstance().init(instance, paths, !safeStart);
    VST3HostEngine::getInstance().setHostArchitectureRequester([header, controlEvent](bridge::Architecture architecture)
    {
        InterlockedExchange(&header->requestedArchitecture, static_cast<long>(architecture));
        SetEvent(controlEvent);
    });
    VST3HostEngine::getInstance().writeLog("DPI awareness context="
        + juce::String(AreDpiAwarenessContextsEqual(GetThreadDpiAwarenessContext(), DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)
            ? "PerMonitorV2" : "other")
        + "; initialDpi=" + juce::String(GetDpiForSystem()));
    auto hostWindow = std::make_unique<HostWindow>(true);
    if (!pumpUntilEditorInitialised(*hostWindow, 3000))
    {
        VST3HostEngine::getInstance().writeLog("GUI preload failed");
        return 4;
    }
    hostWindow->finishPreload(false);
    VST3HostEngine::getInstance().writeLog("GUI preload completed");
    VST3HostEngine::getInstance().writeLog("Out-of-process host ready for audio and GUI events");

    AudioContext context { header, requestEvent, shutdownEvent };
    HANDLE audioThread = CreateThread(nullptr, 0, audioThreadMain, &context, 0, nullptr);
    const auto showHostWindow = [&hostWindow]
    {
        if (!hostWindow) hostWindow = std::make_unique<HostWindow>();
        hostWindow->bringToFrontOrFlash();
    };
    const auto serviceGuiRequest = [&header, &showHostWindow]
    {
        const auto requested = InterlockedCompareExchange(&header->showGuiSequence, 0, 0);
        const auto handled = InterlockedCompareExchange(&header->handledGuiSequence, 0, 0);
        if (requested == handled) return;
        VST3HostEngine::getInstance().writeLog("Out-of-process host servicing GUI request sequence " + juce::String(requested));
        showHostWindow();
        InterlockedExchange(&header->handledGuiSequence, requested);
        VST3HostEngine::getInstance().writeLog("Out-of-process host showed GUI window");
    };

    bool running = audioThread != nullptr;
    while (running)
    {
        HANDLE waits[] = { shutdownEvent, showEvent, dspStartEvent, audioThread };
        const DWORD result = MsgWaitForMultipleObjects(4, waits, FALSE, 100, QS_ALLINPUT);
        if (result == WAIT_OBJECT_0 || result == WAIT_OBJECT_0 + 3)
            running = false;
        else if (result == WAIT_OBJECT_0 + 1)
        {
            VST3HostEngine::getInstance().writeLog("Out-of-process host received show GUI event");
            serviceGuiRequest();
        }
        else if (result == WAIT_OBJECT_0 + 2)
        {
            VST3HostEngine::getInstance().writeLog("Out-of-process host received DSP start event");
            if (VST3HostEngine::getInstance().getSettingsSnapshot().openEditorOnStart)
            {
                showHostWindow();
                VST3HostEngine::getInstance().writeLog("Out-of-process host auto-opened GUI window");
            }
        }
        else if (result == WAIT_OBJECT_0 + 4)
        {
            MSG message {};
            for (int i = 0; i < 64 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE); ++i)
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            serviceGuiRequest();
        }
        else if (result == WAIT_TIMEOUT)
            serviceGuiRequest();
        else
            running = false;
    }

    SetEvent(shutdownEvent);
    if (audioThread != nullptr)
    {
        WaitForSingleObject(audioThread, 1500);
        CloseHandle(audioThread);
    }

    hostWindow.reset();
    VST3HostEngine::destroyInstance();
    juce::shutdownJuce_GUI();
    UnmapViewOfFile(header);
    return 0;
}
