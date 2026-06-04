#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include "HostWindow.h"
#include "OutProcProtocol.h"
#include "VST3HostEngine.h"

#include <atomic>
#include <cstdint>
#include <fstream>
#include <memory>

namespace
{
    void appendHostLog(const char* message)
    {
        char tempPath[MAX_PATH] {};
        if (GetTempPathA(MAX_PATH, tempPath) == 0)
            return;
        std::ofstream stream(std::string(tempPath) + "dsp_vst3_bridge.log", std::ios::app);
        if (stream)
            stream << message << "\r\n";
    }

    HANDLE parseHandle(int argc, wchar_t** argv, const wchar_t* name)
    {
        for (int i = 1; i + 1 < argc; ++i)
            if (wcscmp(argv[i], name) == 0)
                return reinterpret_cast<HANDLE>(_wcstoui64(argv[i + 1], nullptr, 10));
        return nullptr;
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

    int argc = 0;
    auto** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr)
        return 1;

    HANDLE mapping = parseHandle(argc, argv, L"--mapping");
    HANDLE requestEvent = parseHandle(argc, argv, L"--request");
    HANDLE shutdownEvent = parseHandle(argc, argv, L"--shutdown");
    HANDLE showEvent = parseHandle(argc, argv, L"--show");
    LocalFree(argv);

    if (mapping == nullptr || requestEvent == nullptr || shutdownEvent == nullptr || showEvent == nullptr)
        return 2;

    auto* header = static_cast<VST3BridgeIpcHeader*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0));
    if (header == nullptr)
        return 3;

    juce::initialiseJuce_GUI();
    VST3HostEngine::createInstance();
    VST3HostEngine::getInstance().init(instance);
    appendHostLog("Out-of-process host ready for audio and GUI events");

    AudioContext context { header, requestEvent, shutdownEvent };
    HANDLE audioThread = CreateThread(nullptr, 0, audioThreadMain, &context, 0, nullptr);
    std::unique_ptr<HostWindow> hostWindow;

    bool running = audioThread != nullptr;
    while (running)
    {
        HANDLE waits[] = { shutdownEvent, showEvent, audioThread };
        const DWORD result = MsgWaitForMultipleObjects(3, waits, FALSE, INFINITE, QS_ALLINPUT);
        if (result == WAIT_OBJECT_0 || result == WAIT_OBJECT_0 + 2)
            running = false;
        else if (result == WAIT_OBJECT_0 + 1)
        {
            appendHostLog("Out-of-process host received show GUI event");
            if (!hostWindow)
                hostWindow = std::make_unique<HostWindow>();
            hostWindow->setVisible(true);
            hostWindow->toFront(true);
            appendHostLog("Out-of-process host showed GUI window");
        }
        else if (result == WAIT_OBJECT_0 + 3)
        {
            MSG message {};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
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
